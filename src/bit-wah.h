/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

#ifndef IS_LIB_COMMON_BIT_WAH_H
#define IS_LIB_COMMON_BIT_WAH_H

#include <lib-common/container-qvector.h>

/** \defgroup qkv__ll__wah Word Aligned Hybrid bitmaps.
 * \ingroup qkv__ll
 * \brief Word Aligned Hybrid bitmaps.
 *
 * \{
 *
 * WAH are a form of compressed bitmap highly compact in case the bitmap
 * contains a lot of successive bits at the same value (1 or 0). In that case,
 * the sequence is compressed as a single integer containing the number of
 * words (32bits) of the sequence.
 *
 * \section format WAH format
 *
 * A WAH is a sequence of chunks each one composed of two parts:
 * - a header of two words with:
 *   - a bit value (0 or 1)
 *   - the number M of successive words with that bit value (called a run)
 *   - the number N of following words that were not compressed
 * - N uncompressed words.
 *
 * As a consequence, each chunk encodes M + N words in 2 + N words. This means
 * the maximum overhead of the WAH is when M is zero, in which case, the
 * overhead is 2 words (8 bytes). As a consequence, it is always at least as
 * efficient as an uncompressed stored in term of memory usage.
 *
 * It's Hybrid because it contains both compressed and uncompressed data, and
 * aligned since everything is done at the work level: the header only
 * references a integral number of words of 32 bits.
 *
 * In memory, the chunks are stored in wah_word_t vectors called buckets.
 * Each bucket contains \ref bit_wah_g.bits_in_bucket bits of the bitmap,
 * except the last one that can be partially filled.
 * This is done like that in order to avoid having too big vectors in memory.
 *
 * \section usage Use cases
 *
 * A WAH does not support efficient random accesses (reading / writing at a
 * specific bit position) because the chunks encode a variable amount of words
 * in a variable amount of memory. However, it efficiently support both
 * sequential reading / writing.
 *
 * Bitwise operations are also supported but, with the exception of the
 * negation operator, they are not in place (they always require either a
 * brand new bitmap or a copy of one of the operands). Those operations are
 * efficient since they can deal with long runs with a single word read.
 */

/* Structures {{{ */

typedef struct wah_header_t {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned  bit   : 1;
    unsigned  words : 31;
#else
    unsigned  words : 31;
    unsigned  bit   : 1;
#endif
} wah_header_t;
#define WAH_MAX_WORDS_IN_RUN  ((1ull << 31) - 1)

typedef union wah_word_t {
    wah_header_t head;
    uint32_t     count;
    uint32_t     literal;
} wah_word_t;
qvector_t(wah_word, wah_word_t);

qvector_t(wah_word_vec, qv_t(wah_word));

typedef struct wah_t {
    uint64_t  len;
    uint64_t  active;

    int previous_run_pos;
    int last_run_pos;

    /* WARNING: the following fields should not be accessed directly, unless
     * you really know what you are doing. In most cases, you'll want to use
     * wah_get_storage. */
    qv_t(wah_word_vec) _buckets;
    uint32_t           _pending;
    wah_word_t         _padding[3]; /* Ensure sizeof(wah_t) == 64 */
} wah_t;

#define WAH_BIT_IN_WORD  bitsizeof(wah_word_t)

/* }}} */
/* Public API {{{ */


wah_t *wah_init(wah_t *map) __leaf;
wah_t *wah_new(void) __leaf;
void wah_wipe(wah_t *map) __leaf;
void wah_reset_map(wah_t *map);
GENERIC_DELETE(wah_t, wah);

wah_t *t_wah_new(int expected_first_bucket_size) __leaf;
wah_t *t_wah_dup(const wah_t *src) __leaf;
void wah_copy(wah_t *map, const wah_t *src) __leaf;
wah_t *wah_dup(const wah_t *src) __leaf;

/* Create a wah structure from existing wah-encoded bitmap.
 *
 * This generates read-only wah_t structures
 */
wah_t *mp_wah_init_from_data(mem_pool_t *mp, wah_t *wah, pstream_t data);
static inline wah_t *wah_init_from_data(wah_t *wah, pstream_t data)
{
    return mp_wah_init_from_data(NULL, wah, data);
}
wah_t *mp_wah_new_from_data(mem_pool_t *mp, pstream_t data);
static inline wah_t *wah_new_from_data(pstream_t data)
{
    return mp_wah_new_from_data(NULL, data);
}

/** Get the raw data contained in a wah_t.
 *
 * This function must be used to get the data contained by a wah_t, in order
 * to, for example, write it on disk to persist the wah.
 *
 * It returns the vector of buckets contained by the wah. The buckets must be
 * written one after the other so that it can be reloaded by
 * \ref wah_new_from_data).
 *
 * \warning a wah must not have pending data if you want this to properly
 *          work; use \ref wah_pad32 to ensure that.
 */
const qv_t(wah_word_vec) *wah_get_storage(const wah_t *wah);
uint64_t wah_get_storage_len(const wah_t *wah);

void wah_add0s(wah_t *map, uint64_t count) __leaf;
void wah_add1s(wah_t *map, uint64_t count) __leaf;
void wah_add1_at(wah_t *map, uint64_t pos) __leaf;
void wah_add(wah_t *map, const void *data, uint64_t count) __leaf;
void wah_pad32(wah_t *map) __leaf;

void wah_and(wah_t *map, const wah_t *other) __leaf;
void wah_and_not(wah_t *map, const wah_t *other) __leaf;
void wah_not_and(wah_t *map, const wah_t *other) __leaf;
void wah_or(wah_t *map, const wah_t *other) __leaf;
void wah_not(wah_t *map) __leaf;

wah_t *wah_multi_or(const wah_t *src[], int len, wah_t * __restrict dest) __leaf;

/** Get the value of a bit in a WAH.
 *
 * \warning this function is really inefficient, and should be used with
 *          caution (or in tests context).
 */
__must_check__ __leaf
bool wah_get(const wah_t *map, uint64_t pos);

/* }}} */
/* WAH pools {{{ */

wah_t *wah_pool_acquire(void);
void wah_pool_release(wah_t **wah);

/* }}} */
/* Enumeration {{{ */

typedef enum wah_enum_state_t {
    WAH_ENUM_END,
    WAH_ENUM_PENDING,
    WAH_ENUM_LITERAL,
    WAH_ENUM_RUN,
} wah_enum_state_t;

typedef struct wah_word_enum_t {
    const wah_t     *map;
    wah_enum_state_t state;
    int              bucket;
    int              pos;
    uint32_t         remain_words;
    uint32_t         current;
    uint32_t         reverse;
} wah_word_enum_t;

wah_word_enum_t wah_word_enum_start(const wah_t *map, bool reverse) __leaf;
bool wah_word_enum_next(wah_word_enum_t *en) __leaf;
uint32_t wah_word_enum_skip0(wah_word_enum_t *en) __leaf;

/*
 * invariants for an enumerator not a WAH_ENUM_END:
 *  - current_word is non 0 and its last bit is set
 *  - if remain_bits <= WAH_BIT_IN_WORD we're consuming "current_word" else
 *    we're streaming ones.
 *  - remain_bits is the number of yet to stream bits including the current
 *    one.
 */
typedef struct wah_bit_enum_t {
    wah_word_enum_t word_en;
    uint64_t        key;
    uint64_t        remain_bits;
    uint32_t        current_word;
} wah_bit_enum_t;

bool wah_bit_enum_scan_word(wah_bit_enum_t *en) __leaf;

static ALWAYS_INLINE void wah_bit_enum_scan(wah_bit_enum_t *en)
{
    if (en->current_word == 0 && !wah_bit_enum_scan_word(en))
        return;

    assert (en->current_word);
    if (en->remain_bits <= WAH_BIT_IN_WORD) {
        uint32_t bit = bsf32(en->current_word);

        assert (bit < en->remain_bits);
        en->key           += bit;
        en->current_word >>= bit;
        en->remain_bits   -= bit;
    }
}

static ALWAYS_INLINE void wah_bit_enum_next(wah_bit_enum_t *en)
{
    en->key++;
    if (en->remain_bits <= WAH_BIT_IN_WORD)
        en->current_word >>= 1;
    en->remain_bits--;
    wah_bit_enum_scan(en);
}

wah_bit_enum_t wah_bit_enum_start(const wah_t *wah, bool reverse) __leaf;
void wah_bit_enum_skip1s(wah_bit_enum_t *en, uint64_t to_skip) __leaf;

#define wah_for_each_1(en, map)                                              \
    for (wah_bit_enum_t en = wah_bit_enum_start(map, false);                 \
         en.word_en.state != WAH_ENUM_END; wah_bit_enum_next(&en))

#define wah_for_each_0(en, map)                                              \
    for (wah_bit_enum_t en = wah_bit_enum_start(map, true);                  \
         en.word_en.state != WAH_ENUM_END; wah_bit_enum_next(&en))

/* }}} */
/* Debugging {{{ */

__cold
void wah_debug_print(const wah_t *wah, bool print_content);

/* }}} */
/** \} */
#endif
