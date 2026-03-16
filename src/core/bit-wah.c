/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <lib-common/arith.h>
#include <lib-common/bit-wah.h>

#define WAH_BITS_IN_BUCKET_DEFAULT (8 * (512ul << 20))

#ifdef NDEBUG

# define WAH_BITS_IN_BUCKET WAH_BITS_IN_BUCKET_DEFAULT

#else

#define WAH_CHECK_NORMALIZED  1

static struct {
    uint64_t bits_in_bucket;
    bool check_normalized;
} bit_wah_g = {
# define _G  bit_wah_g
    .bits_in_bucket = WAH_BITS_IN_BUCKET_DEFAULT,
};

/* In non release builds we want to be able to change the number of bits per
 * buckets for testing purpose.
 */
# define WAH_BITS_IN_BUCKET  _G.bits_in_bucket

#endif /* NDEBUG */

/* Word enumerator {{{ */

/** Get the words array of the bucket the enumerator is currently pointing to.
 *
 * This is a helper that translates the enumerator's current bucket index into
 * the actual (tab, len) pair, abstracting over the inlined/qvector bucket
 * representation.
 */
static wah_words_t wah_word_enum_get_cur_bucket(wah_word_enum_t *en)
{
    return wah_bucket_get_words(&en->map->_buckets.tab[en->bucket]);
}

/** Initialize an enumerator from the current position within a bucket.
 *
 * Reads the WAH chunk at en->pos in \p bucket and sets the enumerator state
 * accordingly:
 * - If the chunk has a non-zero run (head.words > 0), the enumerator enters
 *   WAH_ENUM_RUN state: en->current is set to all-0s or all-1s depending on
 *   head.bit, and remain_words tracks how many run words are left.
 * - If the chunk has no run but has literals (count > 0), the enumerator
 *   enters WAH_ENUM_LITERAL state: en->current is set to the first literal,
 *   en->pos is advanced past all the literals, and remain_words is set to the
 *   literal count.
 * - Otherwise (no run, no literals), we are past all stored chunks and the
 *   remaining data is the pending word: the enumerator enters
 *   WAH_ENUM_PENDING state.
 *
 * The en->reverse mask is XOR-ed into en->current to support the "reverse"
 * mode used by wah_for_each_0 (iterating over cleared bits).
 *
 * Called both at enumeration start and when transitioning between chunks in
 * wah_word_enum_next().
 */
static void
__wah_word_enum_start(wah_word_enum_t *en, const wah_words_t *bucket)
{
    if (bucket->tab[en->pos].head.words > 0) {
        en->state        = WAH_ENUM_RUN;
        en->remain_words = bucket->tab[en->pos].head.words;
        en->current      = 0 - bucket->tab[en->pos].head.bit;
    } else
    if (bucket->tab[en->pos + 1].count > 0) {
        en->state        = WAH_ENUM_LITERAL;
        en->remain_words = bucket->tab[en->pos + 1].count;
        en->current      = bucket->tab[en->pos + 2].literal;
        /* XXX Beware of the +=, at actual start pos will be 0 but when used
         * in wah_word_enum_next we must move relatively to the previous pos.
         */
        en->pos         += bucket->tab[en->pos + 1].count + 2;
        assert (en->pos <= bucket->len);
        assert ((int)en->remain_words <= en->pos);
    } else {
        en->state        = WAH_ENUM_PENDING;
        en->remain_words = 1;
        en->current      = en->map->_pending;
    }
    en->current ^= en->reverse;
}

/** Start a word-level enumeration over a WAH bitmap.
 *
 * Returns an enumerator positioned on the first 32-bit word of \p map.
 *
 * If \p reverse is true, every word value yielded by the enumerator will be
 * bitwise-inverted (used to iterate over 0-bits as if they were 1-bits).
 *
 * If the map is empty (len == 0), the enumerator starts in WAH_ENUM_END.
 */
wah_word_enum_t wah_word_enum_start(const wah_t *map, bool reverse)
{
    wah_word_enum_t en = {
        .map = map,
        .state = WAH_ENUM_END,
        .reverse = (uint32_t)0 - reverse,
    };
    wah_words_t bucket = wah_word_enum_get_cur_bucket(&en);

    if (map->len == 0) {
        en.state   = WAH_ENUM_END;
        en.current = en.reverse;
        return en;
    }
    __wah_word_enum_start(&en, &bucket);
    return en;
}

/** Advance the word enumerator to the next 32-bit word.
 *
 * Moves the enumerator forward by one word and updates en->current with the
 * new word value. Returns true if a new word is available, false when the
 * enumeration is complete (WAH_ENUM_END).
 *
 * Handles three kinds of transitions:
 * 1. Within a run or literal sequence: simply decrements remain_words (and
 *    for literals, loads the next literal value).
 * 2. From the end of a run to its trailing literals: reads the literal count
 *    from the chunk and transitions to WAH_ENUM_LITERAL.
 * 3. From the end of a literal sequence to the next chunk: if more chunks
 *    exist in the current bucket, starts the next chunk via
 *    __wah_word_enum_start(). If the bucket is exhausted, moves to the next
 *    bucket or to the pending word.
 */
bool wah_word_enum_next(wah_word_enum_t *en)
{
    wah_words_t bucket = wah_word_enum_get_cur_bucket(en);

    if (en->remain_words != 1) {
        en->remain_words--;
        if (en->state == WAH_ENUM_LITERAL) {
            en->current  = bucket.tab[en->pos - en->remain_words].literal;
            en->current ^= en->reverse;
        }
        return true;
    }

    switch (__builtin_expect(en->state, WAH_ENUM_RUN)) {
      case WAH_ENUM_END:
        return false;

      case WAH_ENUM_PENDING:
        en->state    = WAH_ENUM_END;
        en->current  = en->reverse;
        return false;

      default: /* WAH_ENUM_RUN */
        assert (en->state == WAH_ENUM_RUN);
        en->pos++;
        en->remain_words = bucket.tab[en->pos++].count;
        en->pos         += en->remain_words;
        assert (en->pos <= bucket.len);
        assert ((int)en->remain_words <= en->pos);
        en->state = WAH_ENUM_LITERAL;
        if (en->remain_words != 0) {
            en->current  = bucket.tab[en->pos - en->remain_words].literal;
            en->current ^= en->reverse;
            return true;
        }

        /* Transition to literal, so don't break here */
        /* FALLTHROUGH */

      case WAH_ENUM_LITERAL:
        if (en->pos == bucket.len) {
            if (en->bucket < en->map->_buckets.len - 1) {
                en->bucket++;
                bucket = wah_word_enum_get_cur_bucket(en);
                en->pos = 0;
            } else
            if ((en->map->len % WAH_BIT_IN_WORD)) {
                en->state = WAH_ENUM_PENDING;
                en->remain_words = 1;
                en->current  = en->map->_pending;
                en->current ^= en->reverse;
                return true;
            } else {
                en->state   = WAH_ENUM_END;
                en->current = en->reverse;
                return false;
            }
        }
        __wah_word_enum_start(en, &bucket);
        return true;
    }
}

/** Skip \p skip words forward in the word enumerator.
 *
 * This is an optimized way to advance the enumerator by multiple words at
 * once, avoiding the cost of calling wah_word_enum_next() for each word.
 *
 * In a run, this simply adjusts remain_words. The last word of each skip
 * batch is consumed via wah_word_enum_next() so that chunk transitions are
 * handled correctly and en->current is properly updated.
 *
 * Returns true if the enumerator is still valid after the skip, false if the
 * end was reached.
 */
static bool wah_word_enum_skip(wah_word_enum_t *en, uint32_t skip)
{
    uint32_t skippable = 0;

    while (skip != 0) {
        switch (__builtin_expect(en->state, WAH_ENUM_RUN)) {
          case WAH_ENUM_END:
            return false;

          case WAH_ENUM_PENDING:
            return wah_word_enum_next(en);

          default:
            skippable = MIN(skip, en->remain_words);
            skip -= skippable;

            /* XXX: Use next to skip the last word because:
             *  - if we reach the end of a run, this will automatically select
             *    the next run
             *  - if we end within a run of literal, this will properly update
             *    'en->current' with the next literal word
             */
            en->remain_words -= skippable - 1;
            wah_word_enum_next(en);
            break;
        }
    }
    return true;
}

/** Skip over consecutive all-zero words in the word enumerator.
 *
 * Advances the enumerator past all words whose value is 0 (after applying the
 * reverse mask). This is used during bit enumeration to efficiently jump over
 * large gaps of cleared bits without examining them one by one.
 *
 * For runs of zeros, the entire run is skipped at once. For literal zeros,
 * they are skipped one at a time. Returns the total number of zero-words that
 * were skipped.
 */
uint32_t wah_word_enum_skip0(wah_word_enum_t *en)
{
    uint32_t skipped = 0;

    while (en->current == 0) {
        switch (__builtin_expect(en->state, WAH_ENUM_RUN)) {
          case WAH_ENUM_END:
            return skipped;

          case WAH_ENUM_PENDING:
            skipped++;
            wah_word_enum_next(en);
            return skipped;

          case WAH_ENUM_RUN:
            skipped += en->remain_words;
            en->remain_words = 1;
            wah_word_enum_next(en);
            break;

          case WAH_ENUM_LITERAL:
            skipped++;
            wah_word_enum_next(en);
            break;
        }
    }
    return skipped;
}

/* }}} */
/* Bit enumerator {{{ */

/** Scan forward in the bit enumerator to find the next non-zero word.
 *
 * Called when en->current_word has been fully consumed (== 0). It advances
 * the underlying word enumerator until a word with at least one set bit is
 * found.
 *
 * For runs of 1-bits, sets remain_bits to the total bit count of the run and
 * immediately returns. For literal or pending words, sets remain_bits to 32
 * (or the pending bit count) and masks out any unused trailing bits.  For
 * runs of 0-bits, the key is advanced past them in bulk.
 *
 * Returns true if a non-zero word was found, false if the enumeration is
 * exhausted.
 */
bool wah_bit_enum_scan_word(wah_bit_enum_t *en)
{
    /* realign to a word boundary */
    assert (en->current_word == 0);
    en->key += en->remain_bits;
    assert (en->word_en.state <= WAH_ENUM_PENDING ||
            (en->key % WAH_BIT_IN_WORD) == 0);

    while (wah_word_enum_next(&en->word_en)) {
        en->current_word = en->word_en.current;
        if (en->word_en.state == WAH_ENUM_RUN) {
            if (en->current_word) {
                en->remain_bits  = en->word_en.remain_words * WAH_BIT_IN_WORD;
                en->word_en.remain_words = 1;
                return true;
            }
            en->key += en->word_en.remain_words * WAH_BIT_IN_WORD;
            en->word_en.remain_words = 1;
        } else {
            if (unlikely(en->word_en.state == WAH_ENUM_PENDING)) {
                en->remain_bits  = en->word_en.map->len % WAH_BIT_IN_WORD;
                en->current_word &= BITMASK_LT(uint32_t, en->remain_bits);
            } else {
                en->remain_bits  = WAH_BIT_IN_WORD;
            }
            if (likely(en->current_word)) {
                return true;
            }
            en->key += WAH_BIT_IN_WORD;
        }
    }
    return false;
}

/** Start a bit-level enumeration over a WAH bitmap.
 *
 * Returns an enumerator positioned on the first set bit (or first cleared bit
 * if \p reverse is true). This enumerator is used by wah_for_each_1 and
 * wah_for_each_0 macros. The en.key field gives the position of the current
 * bit.
 */
wah_bit_enum_t wah_bit_enum_start(const wah_t *wah, bool reverse)
{
    wah_bit_enum_t en;

    p_clear(&en, 1);
    en.word_en = wah_word_enum_start(wah, reverse);
    if (en.word_en.state != WAH_ENUM_END) {
        en.current_word = en.word_en.current;
        en.remain_bits  = WAH_BIT_IN_WORD;
        if (en.word_en.state == WAH_ENUM_PENDING) {
            en.remain_bits   = en.word_en.map->len % WAH_BIT_IN_WORD;
            en.current_word &= BITMASK_LT(uint32_t, en.remain_bits);
        }
        wah_bit_enum_scan(&en);
    }
    return en;
}

/** Skip \p to_skip set bits in the bit enumerator.
 *
 * Efficiently advances the enumerator past \p to_skip set bits. For runs of
 * 1-bits, arithmetic on remain_bits is sufficient. For literal words, the
 * bitcount determines how many bits can be skipped in the current word.
 * After the bulk skip, the remaining bits (if any) are consumed one-by-one
 * via wah_bit_enum_next().
 */
void wah_bit_enum_skip1s(wah_bit_enum_t *en, uint64_t to_skip)
{
    if (to_skip == 0) {
        return;
    }

    while (to_skip) {
        uint64_t bits;

        switch (en->word_en.state) {
          case WAH_ENUM_PENDING:
          case WAH_ENUM_LITERAL:
            bits = bitcount32(en->current_word);

            if (bits > to_skip) {
                goto end;
            }

            to_skip -= bits;
            en->current_word = 0;
            break;

          case WAH_ENUM_RUN:
            bits = MIN(to_skip, en->remain_bits);
            en->key         += bits;
            en->remain_bits -= bits;
            to_skip         -= bits;
            if (en->remain_bits < WAH_BIT_IN_WORD) {
                en->current_word = BITMASK_LT(uint32_t, en->remain_bits);
            }
            if (en->current_word) {
                return;
            }
            break;

          case WAH_ENUM_END:
            return;
        }

        if (!wah_bit_enum_scan_word(en)) {
            return;
        }
    }

  end:
    wah_bit_enum_scan(en);
    while (to_skip > 0 && en->word_en.state != WAH_ENUM_END) {
        wah_bit_enum_next(en);
        to_skip--;
    }
}

/* }}} */
/* Administrativia {{{ */

wah_t *wah_init(wah_t *map)
{
    STATIC_ASSERT(sizeof(wah_t) == 64);
    STATIC_ASSERT(sizeof(wah_bucket_t) == sizeof(qv_t(wah_word)));

    /* Inline length alias the qvector size member with the assertion that the
     * size of the qvector version of the bucket will always be greater than
     * the maximum number of inlined words.
     */
    STATIC_ASSERT(offsetof(wah_bucket_t, inlined.len) ==
                  offsetof(wah_bucket_t, qv.size));

    qv_init(&map->_buckets);
    wah_reset_map(map);

    return map;
}
DO_NEW(wah_t, wah);

GENERIC_INIT(wah_bucket_t, wah_bucket);

static inline void wah_bucket_wipe(wah_bucket_t *bucket)
{
    if (!wah_bucket_is_inlined(bucket)) {
        qv_wipe(&bucket->qv);
    }
}

void wah_wipe(wah_t *map)
{
    qv_deep_wipe(&map->_buckets, wah_bucket_wipe);
}

/** Convert inlined bucket to qvector.
 */
static void
wah_bucket_to_qv(mem_pool_t *mp, wah_bucket_t *bucket, int new_size)
{
    wah_word_t *words;
    wah_bucket_t tmp = *bucket;

    assert(wah_bucket_is_inlined(bucket));
    assert(new_size > WAH_BUCKET_INLINED_WORDS);

    mp_qv_init(mp, &bucket->qv, new_size);

    words = qv_growlen(&bucket->qv, tmp.inlined.len);
    p_copy(words, tmp.inlined.words, tmp.inlined.len);
}

/** Append a word into a WAH bucket.
 */
static void
wah_bucket_append(wah_t *map, wah_bucket_t *bucket, wah_word_t word)
{
    if (wah_bucket_is_inlined(bucket)) {
        if (bucket->inlined.len < WAH_BUCKET_INLINED_WORDS) {
            /* New word still fits in the inlined bucket. */
            bucket->inlined.words[bucket->inlined.len++] = word;
            return;
        }
        /* Bucket needs to be converted to qvector. */
        wah_bucket_to_qv(map->_buckets.mp, bucket,
                         WAH_BUCKET_INLINED_WORDS + 1);
    }

    qv_append(&bucket->qv, word);
}

/** Append \p len words from \p src into a bucket.
 *
 * Like wah_bucket_append() but for multiple words at once. Handles the
 * inlined-to-qvector promotion if the new words don't fit in the inlined
 * storage.
 */
static void
wah_bucket_extend(wah_t *map, wah_bucket_t *bucket, const wah_word_t *src,
                  int len)
{
    if (wah_bucket_is_inlined(bucket)) {
        if (bucket->inlined.len + len <= WAH_BUCKET_INLINED_WORDS) {
            p_copy(&bucket->inlined.words[bucket->inlined.len], src, len);
            bucket->inlined.len += len;
            return;
        }
        /* Bucket needs to be converted to qvector. */
        wah_bucket_to_qv(map->_buckets.mp, bucket, bucket->inlined.len + len);
    }

    qv_extend(&bucket->qv, src, len);
}

/** Make a bucket point to external data without copying.
 *
 * Used by wah_init_from_data() to create zero-copy buckets that reference the
 * original serialized WAH data. If the data fits in the inline storage, it is
 * copied into the bucket directly. Otherwise, a static qvector is set up that
 * borrows the pointer (the caller must ensure the data remains valid for the
 * lifetime of the WAH).
 *
 * The bucket must be freshly initialized (inlined, length 0).
 */
static void
wah_bucket_set_static(wah_bucket_t *bucket, wah_word_t *data, int len)
{
    assert(wah_bucket_is_inlined(bucket) && !bucket->inlined.len);
    if (len <= WAH_BUCKET_INLINED_WORDS) {
        p_copy(bucket->inlined.words, data, len);
        bucket->inlined.len = len;
    } else {
        qv_init_static(&bucket->qv, data, len);
    }
}

/** Create and append a new bucket to the WAH's bucket vector.
 *
 * Before allocating a new bucket, this function performs housekeeping on the
 * current last bucket:
 * - If the last bucket was a qvector that has been shrunk to fit in the
 *   inlined limit, it is converted back to an inlined bucket. The now-unused
 *   qvector allocation is recycled for the new bucket to avoid a free() /
 *   malloc() cycle.
 * - Otherwise, the last bucket's qvector is optimized (trimmed to fit).
 *
 * \p size is a hint for the initial capacity of the new bucket. If it fits
 * within WAH_BUCKET_INLINED_WORDS, an inlined bucket is used.
 */
static wah_bucket_t *wah_create_bucket(wah_t *map, int size)
{
    wah_bucket_t *bucket;

    if (map->_buckets.len > 0) {
        bucket = tab_last(&map->_buckets);
        if (!wah_bucket_is_inlined(bucket)) {
            if (unlikely(bucket->qv.len <= WAH_BUCKET_INLINED_WORDS)) {
                /* For some reason the bucket was shrunk and need to be
                 * converted back to inlined bucket.
                 */
                qv_t(wah_word) qv = bucket->qv;

                wah_bucket_init(bucket);
                p_copy(bucket->inlined.words, qv.tab, qv.len);
                bucket->inlined.len = qv.len;

                /* Instead of deleting the useless qv we reuse it for the new
                 * bucket. We assume that if the qv was allocated we will
                 * probably need it so let's not waste a free()/malloc()
                 * cycle. If the qv is still unused it will be moved to the
                 * next bucket by this code and so on.
                 */
                bucket = qv_growlen(&map->_buckets, 1);
                qv.len = 0;
                bucket->qv = qv;

                return bucket;
            }
            qv_optimize(&bucket->qv, 0, 0);
        }
    }

    bucket = qv_growlen(&map->_buckets, 1);
    if (size <= WAH_BUCKET_INLINED_WORDS) {
        wah_bucket_init(bucket);
    } else {
        mp_qv_init(map->_buckets.mp, &bucket->qv, size);
    }

    return bucket;
}

/** Create a new bucket pre-initialized with an empty WAH chunk header.
 *
 * This is the standard way to start a new bucket during sequential writing.
 * The bucket is created with 2 zero-words (the chunk header + literal count),
 * representing an empty chunk with no run and no literals. The last_run_pos
 * is reset to point at this new chunk.
 *
 * Used whenever the current bucket is full and a new one must be started
 * (e.g., in wah_push_pending(), wah_add_literal(), etc.).
 */
static wah_bucket_t *__wah_create_bucket(wah_t *map)
{
    wah_bucket_t *bucket = wah_create_bucket(map, 2);

    if (wah_bucket_is_inlined(bucket)) {
        /* Clear already performed, no need to wipe the first 2 words. Just
         * set len. */
        bucket->inlined.len = 2;
    } else {
        /* Potentially recycled qv, enforce 0 on those 2 words. */
        qv_growlen0(&bucket->qv, 2);
    }
    map->previous_run_pos = -1;
    map->last_run_pos     = 0;

    return bucket;
}

/** Reset a WAH to an empty state, reusing its bucket vector allocation.
 *
 * All existing buckets are wiped and the bucket vector is clipped to zero
 * length, but the outer allocation (map->_buckets.tab) is preserved. A single
 * fresh bucket is created with an empty chunk header, ready for new data to
 * be appended.
 */
void wah_reset_map(wah_t *map)
{
    wah_bucket_t *first_bucket;

    map->len                  = 0;
    map->active               = 0;
    map->previous_run_pos     = -1;
    map->last_run_pos         = 0;
    map->buckets_shift        = 0;
    map->_pending             = 0;

    tab_for_each_ptr(bucket, &map->_buckets) {
        wah_bucket_wipe(bucket);
    }
    qv_clip(&map->_buckets, 0);

    first_bucket = wah_create_bucket(map, 2);
    first_bucket->inlined.len = 2;
}

/* WAH copy requires an initialized wah as target
 */
void wah_copy(wah_t *map, const wah_t *src)
{
    int pos;
    qv_t(wah_bucket) buckets = map->_buckets;

    p_copy(map, src, 1);
    map->_buckets = buckets;

    if (src->_buckets.len < map->_buckets.len) {
        /* Wipe buckets which are not needed anymore in dst. */
        for (pos = map->_buckets.len; pos-- > src->_buckets.len; ) {
            wah_bucket_wipe(&map->_buckets.tab[pos]);
        }
        qv_splice(&map->_buckets, src->_buckets.len,
                  map->_buckets.len - src->_buckets.len, NULL, 0);
    } else {
        /* Create new buckets needed in dst. */
        int extra = src->_buckets.len - map->_buckets.len;
        wah_bucket_t *bucket = qv_growlen(&map->_buckets, extra);

        for (pos = 0; pos < extra; pos++) {
            wah_bucket_init(&bucket[pos]);
        }

        /* The original last bucket of the destination map should be the only
         * one to not be qv_optimize(), and thus should be reused as the last
         * bucket.
         */
        if (map->_buckets.len > extra) {
            SWAP(wah_bucket_t, bucket[-1], bucket[extra - 1]);
        }
    }
    assert(src->_buckets.len == map->_buckets.len);

    /* Copy each bucket from src to dst but the last.
     *
     * Each bucket from the first one to the second last are “closed” buckets
     * and thus must be copied identically without any memory overhead.
     *
     * The last bucket may or may not be extended after the copy, and so will
     * be copied separately while preserving any extra memory already
     * allocated for it.
     */
    for (pos = 0; pos < src->_buckets.len - 1; pos++) {
        int len;
        const wah_bucket_t *src_bucket = &src->_buckets.tab[pos];
        wah_bucket_t *dst_bucket = &map->_buckets.tab[pos];

        if (wah_bucket_is_inlined(src_bucket)) {
            wah_bucket_wipe(dst_bucket);
            *dst_bucket = *src_bucket;
            continue;
        }

        len = src_bucket->qv.len;
        if (wah_bucket_is_inlined(dst_bucket)) {
            mp_qv_init(map->_buckets.mp, &dst_bucket->qv, 0);
        }

        dst_bucket->qv.len = len;
        if (len <= dst_bucket->qv.size) {
            qv_optimize(&dst_bucket->qv, 0, 0);
        } else {
            /* We don't want the bucket to grow more than needed and thus we
             * cannot use qv_grow and similar.
             */
            dst_bucket->qv.tab = mp_irealloc_fallback(
                &dst_bucket->qv.mp, dst_bucket->qv.tab, 0,
                len * __qv_sz(&dst_bucket->qv), __qv_align(&dst_bucket->qv),
                MEM_RAW);
            dst_bucket->qv.size = len;
        }
        p_copy(dst_bucket->qv.tab, src_bucket->qv.tab, len);
    }

    if (src->_buckets.len) {
        /* The last bucket is treated separately as explained above. */
        const wah_bucket_t *src_bucket = &src->_buckets.tab[pos];
        wah_bucket_t *dst_bucket = &map->_buckets.tab[pos];

        assert(pos == src->_buckets.len - 1);

        if (wah_bucket_is_inlined(src_bucket)) {
            wah_bucket_wipe(dst_bucket);
            *dst_bucket = *src_bucket;
        } else {
            int len = src_bucket->qv.len;

            if (wah_bucket_is_inlined(dst_bucket)) {
                mp_qv_init(map->_buckets.mp, &dst_bucket->qv, 0);
            }

            if (len > dst_bucket->qv.size) {
                qv_grow(&dst_bucket->qv, len - dst_bucket->qv.len);
            }
            dst_bucket->qv.len = len;
            p_copy(dst_bucket->qv.tab, src_bucket->qv.tab, len);
        }
    }
}

wah_t *wah_dup(const wah_t *src)
{
    wah_t *wah = wah_new();

    wah_copy(wah, src);
    return wah;
}

wah_t *t_wah_new(void)
{
    wah_t *map = t_new(wah_t, 1);

    t_qv_init(&map->_buckets, 1);
    wah_reset_map(map);

    return map;
}

wah_t *t_wah_dup(const wah_t *src)
{
    wah_t *map = t_new(wah_t, 1);

    t_qv_init(&map->_buckets, src->_buckets.len);
    wah_copy(map, src);
    return map;
}

size_t wah_memory_footprint(const wah_t *map)
{
    size_t res = sizeof(*map);

    res += map->_buckets.size * sizeof(*map->_buckets.tab);
    tab_for_each_ptr(bucket, &map->_buckets) {
        if (!wah_bucket_is_inlined(bucket)) {
            res += bucket->qv.size * sizeof(*bucket->qv.tab);
        }
    }

    return res;
}

/* }}} */
/* Operations {{{ */

/** Return the WAH length minus the buckets_shift.
 *
 * The buckets_shift accounts for extra bits stored in the first bucket when
 * it is "overfilled" (i.e., the WAH starts with a very long run of the same
 * bit). Subtracting the shift gives the length relative to the normal
 * bucketing scheme, which is used to compute bucket boundaries.
 */
static ALWAYS_INLINE uint64_t wah_shifted_len(const wah_t *map)
{
    return map->len - map->buckets_shift;
}

/** Return the total bit capacity of all current buckets, accounting for the
 *  shift.
 *
 * Each bucket nominally holds WAH_BITS_IN_BUCKET bits. The first bucket may
 * hold extra bits (buckets_shift). This function returns the total number of
 * bits that can be stored in the existing buckets before a new one must be
 * created.
 */
static ALWAYS_INLINE uint64_t wah_buckets_shifted_len(const wah_t *map)
{
    return map->_buckets.len * WAH_BITS_IN_BUCKET + map->buckets_shift;
}

/** Resolve a bit position to the bucket containing it.
 *
 * Given a bit position \p *pos within the WAH, returns the bucket that
 * contains that bit and adjusts \p *pos to be relative to that bucket. The
 * first bucket is special because it may be overfilled by buckets_shift extra
 * bits.
 */
static ALWAYS_INLINE wah_bucket_t *
wah_buckets_get_bucket(const wah_t *map, uint64_t *pos)
{
    wah_bucket_t *bucket;

    if (*pos < WAH_BITS_IN_BUCKET + map->buckets_shift) {
        bucket = &map->_buckets.tab[0];
    } else {
        *pos -= map->buckets_shift;
        bucket = &map->_buckets.tab[*pos / WAH_BITS_IN_BUCKET];
        *pos %= WAH_BITS_IN_BUCKET;
    }

    return bucket;
}

/** Get a pointer to the word at index \p pos within a bucket.
 *
 * Abstracts over the inlined vs. qvector bucket representation, returning a
 * pointer to the correct storage.
 */
static ALWAYS_INLINE
wah_word_t *wah_bucket_word_ptr(wah_bucket_t *bucket, int pos)
{
    if (wah_bucket_is_inlined(bucket)) {
        assert(pos < bucket->inlined.len);
        return &bucket->inlined.words[pos];
    } else {
        assert(pos < bucket->qv.len);
        return &bucket->qv.tab[pos];
    }
}

/** Get a pointer to the header of the last chunk in the WAH.
 *
 * The "last run" is the most recently appended chunk. Its position within the
 * last bucket is tracked by map->last_run_pos. This function returns the
 * header word of that chunk, which contains the run bit value and the run
 * length (number of homogeneous words).
 */
static ALWAYS_INLINE
wah_header_t *wah_last_run_header(const wah_t *map, wah_bucket_t *bucket)
{
    assert (map->last_run_pos >= 0);
    return &wah_bucket_word_ptr(bucket, map->last_run_pos)->head;
}

/** Get a pointer to the literal count of the last chunk in the WAH.
 *
 * In the WAH chunk layout, the word immediately after the header is the count
 * of uncompressed literal words that follow. This function returns a pointer
 * to that count word for the last chunk, allowing callers to increment it
 * when appending new literals to the current chunk.
 */
static ALWAYS_INLINE
uint32_t *wah_last_run_count(const wah_t *map)
{
    wah_bucket_t *bucket = tab_last(&map->_buckets);

    assert (map->last_run_pos >= 0);
    return &wah_bucket_word_ptr(bucket, map->last_run_pos + 1)->count;
}

/** Append a new WAH chunk header to the last bucket.
 *
 * Writes the two-word chunk prologue: the header word (containing the run bit
 * and run length) followed by a zero literal-count word. The caller is
 * expected to update last_run_pos and previous_run_pos before or after
 * calling this.
 */
static ALWAYS_INLINE
void wah_append_header(wah_t *map, wah_header_t head)
{
    wah_word_t word;
    wah_bucket_t *bucket = tab_last(&map->_buckets);

    word.head = head;
    wah_bucket_append(map, bucket, word);
    word.count = 0;
    wah_bucket_append(map, bucket, word);
}

/** Append a single literal word to the last bucket.
 *
 * This appends the raw 32-bit value to the end of the last bucket's word
 * array. The caller must also increment the literal count of the current
 * chunk header (via wah_last_run_count()) separately.
 */
static ALWAYS_INLINE
void wah_append_literal(wah_t *map, uint32_t val)
{
    wah_word_t word;
    wah_bucket_t *bucket = tab_last(&map->_buckets);

    word.literal = val;
    wah_bucket_append(map, bucket, word);
}

/** Debug-only: verify that the WAH is in normalized form.
 *
 * A normalized WAH never has two consecutive chunks that could be merged.
 * Specifically, a literal word that is all-0s or all-1s must not be adjacent
 * to a run of the same bit value (it should have been merged into the run).
 * Similarly, a run of length < 2 should not exist except at the very
 * beginning or end.
 *
 * Only active when WAH_CHECK_NORMALIZED is defined.
 */
static
void wah_check_normalized(const wah_t *map)
{
#ifdef WAH_CHECK_NORMALIZED

    if (likely(!_G.check_normalized)) {
        return;
    }

    tab_for_each_ptr(bucket, &map->_buckets) {
        uint64_t prev_word = UINT64_MAX;
        const wah_words_t bucket_words = wah_bucket_get_words(bucket);
        int pos = 0;

        while (pos < bucket_words.len) {
            wah_header_t *head  = &bucket_words.tab[pos++].head;
            uint32_t      count = bucket_words.tab[pos++].count;

            assert (head->words >= 2 || pos == bucket_words.len || pos == 2);
            if (prev_word == UINT32_MAX || prev_word == 0) {
                assert(prev_word != (head->bit ? UINT32_MAX : 0));
                prev_word = head->bit ? UINT32_MAX : 0;
            }

            for (uint32_t i = 0; i < count; i++) {
                if (prev_word == UINT32_MAX || prev_word == 0) {
                    assert(prev_word != bucket_words.tab[pos].literal);
                }
                prev_word = bucket_words.tab[pos++].literal;
            }
        }
    }
#endif
}

/** Debug-only: assert all WAH structural invariants.
 *
 * Verifies that:
 * - last_run_pos and previous_run_pos are consistent.
 * - Every bucket has at least 2 words (header + count).
 * - The literal count of the last chunk matches the actual number of words
 *   after it in the last bucket.
 * - active <= len, len >= buckets_shift, and buckets_shift is word-aligned.
 * - The total bit count is consistent with the number of buckets.
 * - The WAH is in normalized form (if WAH_CHECK_NORMALIZED is enabled).
 */
static ALWAYS_INLINE
void wah_check_invariant(const wah_t *map)
{
    wah_bucket_t *last_bucket = tab_last(&map->_buckets);

    assert(map->last_run_pos >= 0);
    assert(map->previous_run_pos >= -1);
    tab_for_each_ptr(bucket, &map->_buckets) {
        assert(wah_bucket_len(bucket) >= 2);
    }
    assert((int)*wah_last_run_count(map) + map->last_run_pos + 2 ==
           wah_bucket_len(last_bucket));
    assert(map->len >= map->active);
    assert(map->len >= map->buckets_shift);
    assert(map->buckets_shift % WAH_BIT_IN_WORD == 0);
    assert(map->len - map->buckets_shift >=
           WAH_BITS_IN_BUCKET * (map->_buckets.len - 1));
    assert(map->len - map->buckets_shift <=
           WAH_BITS_IN_BUCKET * map->_buckets.len + WAH_BIT_IN_WORD);
    wah_check_normalized(map);
}


/** Convert a single-word run into a literal, merging it with the previous
 *  chunk.
 *
 * In a WAH, a chunk must represent at least 2 run-words to be worthwhile
 * (since the header itself costs 2 words). When the last chunk has a run of
 * exactly 1 word (head.words == 1) and no trailing literals, this function
 * "flattens" it: the run word is converted into a literal value (0x00000000
 * or 0xFFFFFFFF) and appended to the previous chunk's literal section.
 *
 * If there is a previous chunk (last_run_pos > 0):
 *   - The last chunk's 2-word header is removed from the bucket.
 *   - The previous chunk's literal count is incremented.
 *   - last_run_pos is moved back to previous_run_pos.
 *   - the former run word is appended as a literal value.
 *
 * If there is no previous chunk (last_run_pos == 0):
 *   - The run is left untouched since we would waste one word by converting
 *     it to a literal.
 *
 * This is a key normalization step called before appending non-trivial
 * literals to ensure the last chunk is ready to accept them.
 */
static inline
void wah_flatten_last_run(wah_t *map)
{
    wah_bucket_t *bucket = tab_last(&map->_buckets);
    wah_header_t *head;

    head = wah_last_run_header(map, bucket);
    if (likely(head->words != 1 || map->last_run_pos <= 0)) {
        return;
    }
    assert(*wah_last_run_count(map) == 0);
    assert(wah_bucket_len(bucket) == map->last_run_pos + 2);

    if (wah_bucket_is_inlined(bucket)) {
        bucket->inlined.len -= 2;
        bucket->inlined.words[map->previous_run_pos + 1].count++;
    } else {
        bucket->qv.len -= 2;
        bucket->qv.tab[map->previous_run_pos + 1].count++;
    }
    map->last_run_pos     = map->previous_run_pos;
    map->previous_run_pos = -1;

    wah_append_literal(map, head->bit ? UINT32_MAX : 0);
    wah_check_invariant(map);
}

/** Flush the pending word into the WAH encoding, repeated \p words times.
 *
 * The "pending" word (map->_pending) holds the last partially or fully filled
 * 32-bit word that hasn't been committed to the WAH structure yet. This
 * function commits it.
 *
 * Two cases:
 * 1. Non-trivial pending (mixed 0s and 1s): The last run is flattened (to
 *    make room for literals), and the pending word is appended as \p words
 *    literal copies.
 *
 * 2. Trivial pending (all-0s or all-1s): The function tries to merge the
 *    words into the existing last run if the bit value matches (or the run
 *    is empty). If the run would become too short (< 2 words) after the
 *    merge, it is flattened. If additional words remain after filling the
 *    current run (due to WAH_MAX_WORDS_IN_RUN), new runs are created.
 *
 * After this call, map->_pending is reset to 0.
 *
 * This is the core routine that translates raw data into WAH-compressed
 * chunks. It only operates within a single bucket; the caller
 * (wah_push_pending()) handles bucket boundaries.
 */
static void __wah_push_pending(wah_t *map, uint64_t words)
{
    const bool is_trivial = map->_pending == UINT32_MAX || map->_pending == 0;

    if (!is_trivial) {
        wah_flatten_last_run(map);
        *wah_last_run_count(map) += words;
        while (words > 0) {
            wah_append_literal(map, map->_pending);
            words--;
        }
    } else {
        wah_bucket_t *bucket = tab_last(&map->_buckets);
        wah_header_t *head = wah_last_run_header(map, bucket);

        if (*wah_last_run_count(map) == 0
        && (!head->bit == !map->_pending || head->words == 0))
        {
            uint64_t to_add;

            to_add = MIN(words, WAH_MAX_WORDS_IN_RUN - head->words);

            /* Merge with previous */
            head->words += to_add;
            head->bit    = !!map->_pending;
            words -= to_add;
        }
        if (head->words < 2) {
            wah_flatten_last_run(map);
        }

        while (words) {
            /* Create a new run */
            wah_header_t new_head;
            uint64_t to_add = MIN(words, WAH_MAX_WORDS_IN_RUN);

            new_head.bit          = !!map->_pending;
            new_head.words        = to_add;
            words -= to_add;
            map->previous_run_pos = map->last_run_pos;
            map->last_run_pos     = wah_bucket_len(bucket);
            wah_append_header(map, new_head);
        }
    }
    map->_pending = 0;
}

/** Flush the pending word into the WAH, handling bucket boundaries.
 *
 * This is the bucket-aware wrapper around __wah_push_pending(). It splits the
 * work across bucket boundaries: when the current bucket is full, a new one
 * is created. The pending word value is preserved across iterations so that
 * each sub-call to __wah_push_pending() uses the same value.
 *
 * \p words is the number of 32-bit words to commit (each being a copy of the
 * pending word). \p active is the number of set bits being added (precomputed
 * by the caller).
 *
 * Precondition: map->len must be word-aligned (len % 32 == 0).
 */
static void wah_push_pending(wah_t *map, uint64_t words, uint64_t active)
{
    const uint32_t pending = map->_pending;

    assert(words > 0);
    assert(map->len % WAH_BIT_IN_WORD == 0);

    while (words) {
        uint64_t bucket_len = wah_shifted_len(map) % WAH_BITS_IN_BUCKET;
        uint64_t to_add;

        if (map->len && map->len >= wah_buckets_shifted_len(map)) {
            /* Current bucket is full, make a new one. */
            assert(bucket_len == 0);
            __wah_create_bucket(map);
        }

        to_add = MIN(words,
                     (WAH_BITS_IN_BUCKET - bucket_len) / WAH_BIT_IN_WORD);
        map->len += to_add * WAH_BIT_IN_WORD;
        map->_pending = pending;
        __wah_push_pending(map, to_add);
        words -= to_add;
    }

    map->active += active;
}

/** Update buckets_shift to allow the first bucket to exceed its normal
 * capacity.
 *
 * When a WAH is being built sequentially and consists entirely of the same
 * bit value (all 0s or all 1s so far), the first bucket is allowed to grow
 * beyond WAH_BITS_IN_BUCKET. The buckets_shift records how many extra bits
 * the first bucket contains beyond the normal limit.
 *
 * This avoids creating many tiny buckets for a bitmap that starts with a very
 * long run of the same bit.
 *
 * Must only be called when there is at most one bucket.
 */
static ALWAYS_INLINE void wah_set_buckets_shift(wah_t *map)
{
    assert(map->_buckets.len <= 1);
    assert(map->len % WAH_BIT_IN_WORD == 0);

    if (map->len > WAH_BITS_IN_BUCKET) {
        map->buckets_shift = map->len - WAH_BITS_IN_BUCKET;

        assert(map->len == wah_buckets_shifted_len(map));
    }
}

/** Push \p words all-zero words into the WAH.
 *
 * Optimized path for adding runs of zeros. If no 1-bits have been added yet
 * (active == 0), the first bucket is allowed to grow indefinitely (overfill
 * optimization), avoiding bucket creation overhead for bitmaps with a large
 * leading zero region.
 *
 * Otherwise, falls through to wah_push_pending() for normal bucket-bounded
 * insertion.
 */
static void wah_push_pending_0s(wah_t *map, uint64_t words)
{
    map->_pending = 0;
    if (!map->active) {
        /* As long as we add only 0s, the first bucket is allowed to grow
         * indefinitely.
         */
        assert(words > 0);
        map->len += words * WAH_BIT_IN_WORD;
        __wah_push_pending(map, words);
        wah_set_buckets_shift(map);

        return;
    }

    wah_push_pending(map, words, 0);
}

/** Push \p words all-ones words into the WAH.
 *
 * Symmetric to wah_push_pending_0s() but for runs of ones. If the WAH is
 * entirely filled with 1-bits so far (active == len), the first bucket is
 * allowed to overfill. Otherwise, wah_push_pending() handles the normal case.
 */
static void wah_push_pending_1s(wah_t *map, uint64_t words)
{
    map->_pending = UINT32_MAX;
    if (map->active == map->len) {
        /* As long as we add only 1s, the first bucket is allowed to grow
         * indefinitely.
         */
        assert(words > 0);
        map->len    += words * WAH_BIT_IN_WORD;
        map->active += words * WAH_BIT_IN_WORD;
        __wah_push_pending(map, words);
        wah_set_buckets_shift(map);

        return;
    }

    wah_push_pending(map, words, words * WAH_BIT_IN_WORD);
}

/** Append \p count zero-bits to the WAH bitmap.
 *
 * If the total (existing pending bits + count) doesn't fill a 32-bit word,
 * only map->len is advanced (the pending word already has zeros in those
 * positions). Otherwise, the pending word is flushed and full 32-bit words of
 * zeros are pushed via wah_push_pending_0s(). Any remaining sub-word tail
 * adjusts map->len without touching _pending.
 */
void wah_add0s(wah_t *map, uint64_t count)
{
    uint64_t remain = map->len % WAH_BIT_IN_WORD;

    wah_check_invariant(map);
    if (remain + count < WAH_BIT_IN_WORD) {
        map->len += count;
        wah_check_invariant(map);
        return;
    }
    if (remain > 0) {
        count    -= WAH_BIT_IN_WORD - remain;
        map->len += WAH_BIT_IN_WORD - remain;
        if (!map->active) {
            /* First bucket still full of 0s, continue to overfill. */
            wah_set_buckets_shift(map);
        } else if (map->len > wah_buckets_shifted_len(map)) {
            __wah_create_bucket(map);
        }
        __wah_push_pending(map, 1);
    }
    if (count >= WAH_BIT_IN_WORD) {
        uint64_t words = count / WAH_BIT_IN_WORD;

        wah_push_pending_0s(map, words);
        count -= words * WAH_BIT_IN_WORD;
    }
    map->len += count;
    wah_check_invariant(map);
}

void wah_pad32(wah_t *map)
{
    uint64_t padding = PAD32EXT(map->len);

    if (padding) {
        wah_add0s(map, padding);
    }
}

/** Append \p count one-bits to the WAH bitmap.
 *
 * Symmetric to wah_add0s(). For sub-word additions, the 1-bits are OR-ed
 * into map->_pending. For full words, wah_push_pending_1s() is called.
 * Any remaining sub-word tail sets the low bits of _pending.
 */
void wah_add1s(wah_t *map, uint64_t count)
{
    uint64_t remain = map->len % WAH_BIT_IN_WORD;

    wah_check_invariant(map);
    if (remain + count < WAH_BIT_IN_WORD) {
        map->_pending |= BITMASK_LT(uint32_t, count) << remain;
        map->len     += count;
        map->active  += count;
        wah_check_invariant(map);
        return;
    }
    if (remain > 0) {
        map->_pending |= BITMASK_GE(uint32_t, remain);
        map->len     += WAH_BIT_IN_WORD - remain;
        map->active  += WAH_BIT_IN_WORD - remain;
        count        -= WAH_BIT_IN_WORD - remain;
        if (map->active == map->len) {
            /* First bucket still full of 1s, continue to overfill. */
            wah_set_buckets_shift(map);
        } else if (map->len > wah_buckets_shifted_len(map)) {
            __wah_create_bucket(map);
        }
        __wah_push_pending(map, 1);
    }
    if (count >= WAH_BIT_IN_WORD) {
        uint64_t words = count / WAH_BIT_IN_WORD;

        wah_push_pending_1s(map, words);
        count -= words * WAH_BIT_IN_WORD;
    }
    map->_pending = BITMASK_LT(uint32_t, count);
    map->len    += count;
    map->active += count;
    wah_check_invariant(map);
}

/** Set a single bit at position \p pos.
 *
 * If \p pos is at or beyond the current length (the expected case for
 * sequential building), zeros are added up to that position and then a single
 * 1-bit is appended.
 *
 * If \p pos is within the already-written region (unexpected / slow path), a
 * temporary WAH with a single bit at \p pos is created and OR-ed into the
 * map.
 */
void wah_add1_at(wah_t *map, uint64_t pos)
{
    if (!expect(pos >= map->len)) {
        t_scope;
        wah_t *tmp = t_wah_new();

        wah_add1_at(tmp, pos);
        wah_or(map, tmp);
        return;
    }

    if (pos != map->len) {
        wah_add0s(map, pos - map->len);
    }
    wah_add1s(map, 1);
}

/** Read up to 64 bits from a raw byte buffer into a uint64_t.
 *
 * Reads \p count bits (up to 64) from \p src in little-endian order.
 *
 * Returns the advanced source pointer, and fills \p *res with the read value
 * and \p *bits with the number of bits actually read.
 *
 * For performance, the reads are done in decreasing chunk sizes (32, 24, 16,
 * 8 bits) to minimize the number of memory accesses. Unused high bits are
 * masked off.
 *
 * Used by wah_add_unaligned() and wah_add_aligned() to parse raw bitmap data
 * that may not be word-aligned.
 */
static const void *
wah_read_word(const uint8_t *src, uint64_t count, uint64_t *res, int *bits)
{
    uint64_t mask;

    if (count >= 64) {
        *res  = get_unaligned_le64(src);
        *bits = 64;
        return src + 8;
    }

    *res  = 0;
    *bits = 0;
    mask  = BITMASK_LT(uint64_t, count);
#define get_unaligned_le8(src)  (*src)
#define READ_SIZE(Size)                                                      \
    if (count > (Size - 8)) {                                                \
        const uint64_t to_read = MIN(count, Size);                           \
        *res   |= ((uint64_t)get_unaligned_le##Size(src)) << *bits;          \
        *bits  += to_read;                                                   \
        src    += Size / 8;                                                  \
        if (to_read == count) {                                              \
            *res &= mask;                                                    \
            return src;                                                      \
        }                                                                    \
        count -= to_read;                                                    \
    }
    READ_SIZE(32);
    READ_SIZE(24);
    READ_SIZE(16);
    READ_SIZE(8);
#undef READ_SIZE
    *res &= mask;
    return src;
}

/** Add a mixed (non-trivial) word of \p bits bits to the WAH.
 *
 * Decomposes the word into alternating runs of 0-bits and 1-bits using
 * bsf64() (bit-scan-forward) and adds each run via wah_add0s()/wah_add1s().
 * The word is progressively shifted right and inverted to toggle between
 * counting zeros and ones.
 *
 * This is the slow path used when a raw data word is neither all-0s nor
 * all-1s and cannot be added as a run.
 */
static void wah_add_bits(wah_t *map, uint64_t word, int bits)
{
    bool     on_0 = true;

    while (bits > 0) {
        if (word == 0) {
            if (on_0) {
                wah_add0s(map, bits);
            } else {
                wah_add1s(map, bits);
            }
            return;
        } else {
            int first = bsf64(word);
            if (first > bits) {
                first = bits;
            }
            if (first != 0) {
                if (on_0) {
                    wah_add0s(map, first);
                } else {
                    wah_add1s(map, first);
                }
                bits  -= first;
                word >>= first;
            }
            word = ~word;
            on_0 = !on_0;
        }
    }
}

/** Add raw bitmap data that is not word-aligned to the WAH.
 *
 * Processes \p count bits from \p src when the WAH's current position is not
 * on a 32-bit word boundary (or when the data is byte-aligned but not
 * word-aligned).
 *
 * For large chunks (>= 64 bits), it first checks if the data is a trivial run
 * (all-0s or all-1s) and uses bsf() to find the run length for efficient
 * compression. Non-trivial 64-bit words are decomposed via wah_add_bits().
 *
 * For the tail (< 64 bits), wah_read_word() is used to read the remaining
 * bytes.
 *
 * Returns the advanced source pointer past the consumed bytes.
 */
static
const void *wah_add_unaligned(wah_t *map, const uint8_t *src, uint64_t count)
{
    while (count >= 64) {
        ssize_t run_length = 64;
        bool    bit = false;
        uint64_t word = get_unaligned_le64(src);

        switch (word) {
          case 0:
            bit = false;
            break;

          case UINT64_MAX:
            bit = true;
            break;

          default:
            wah_add_bits(map, word, 64);
            goto end;
        }

        run_length = bsf(src, 0, count, bit);
        if (run_length < 0) {
            run_length = count;
        }
        run_length = ROUND_2EXP(run_length, 8);
        if (bit) {
            wah_add1s(map, run_length);
        } else {
            wah_add0s(map, run_length);
        }

      end:
        src   += run_length / 8;
        count -= run_length;
    }

    while (count > 0) {
        uint64_t word;
        int      bits = 0;

        src    = wah_read_word(src, count, &word, &bits);
        count -= bits;

        wah_add_bits(map, word, bits);
    }
    wah_check_invariant(map);
    return src;
}

/** Add raw 32-bit-aligned data directly as literal words to the WAH.
 *
 * This is the fast path for adding non-trivial word-aligned data. Rather than
 * decomposing each word into runs of 0s and 1s, the data is copied directly
 * into the last bucket's literal section. The last run is first flattened (so
 * that literals can be appended), then the literal count is incremented and
 * the raw words are bulk-copied.
 *
 * Handles bucket boundaries by creating new buckets as needed.
 *
 * \p count is in bytes and must be a multiple of 4.
 *
 * Precondition: the data must contain at least some mixed content (not
 * all-0s or all-1s) — otherwise wah_add0s/wah_add1s should be used.
 */
static void wah_add_literal(wah_t *map, const uint8_t *src, uint64_t count)
{
    wah_bucket_t *bucket = tab_last(&map->_buckets);

    wah_flatten_last_run(map);
    map->active += membitcount(src, count);

    while (count) {
        uint64_t bucket_len = wah_shifted_len(map) % WAH_BITS_IN_BUCKET;
        uint64_t to_add;

        if (map->len && map->len == wah_buckets_shifted_len(map)) {
            assert (bucket_len == 0);
            bucket = __wah_create_bucket(map);
        }

        to_add = MIN(count / 4,
                     (WAH_BITS_IN_BUCKET - bucket_len) / WAH_BIT_IN_WORD);

        *wah_last_run_count(map) += to_add;
        wah_bucket_extend(map, bucket, (wah_word_t *)src, to_add);

        count    -= to_add * 4;
        src      += to_add * 4;
        map->len += to_add * WAH_BIT_IN_WORD;
    }
    assert(map->active && map->active != map->len);
}

/** Add raw bitmap data that is word-aligned to the WAH.
 *
 * This is the main entry point for adding raw data when the WAH's current bit
 * position is on a 32-bit boundary. It processes 32-bit words one at a time:
 * - All-0s or all-1s words trigger a run-length scan (bsf()) to find the
 *   extent of the run and add it in bulk via wah_add0s/wah_add1s.
 * - Mixed words are added via wah_add_literal() (direct copy).
 *
 * Any remaining sub-word tail (< 32 bits) is loaded into map->_pending.
 */
static
void wah_add_aligned(wah_t *map, const uint8_t *src, uint64_t count)
{
    uint64_t exp_len = map->len + count;

    while (count >= 32) {
        ssize_t run_length = 32;
        bool    bit = false;

        switch (get_unaligned_le32(src)) {
          case 0:
            bit = false;
            break;

          case UINT32_MAX:
            bit = true;
            break;

          default:
            wah_add_literal(map, src, 4);
            goto end;
        }

        run_length = bsf(src, 0, ROUND_2EXP(count, 32), bit);
        if (run_length < 0) {
            run_length = count;
        }
        run_length = ROUND_2EXP(run_length, 32);
        if (bit) {
            wah_add1s(map, run_length);
        } else {
            wah_add0s(map, run_length);
        }

      end:
        src   += run_length / 8;
        count -= run_length;
    }
    wah_check_invariant(map);
    map->_pending = 0;

    if (count > 0) {
        uint64_t word = 0;
        int bits;

        wah_read_word(src, count, &word, &bits);
        assert ((size_t)bits == count);
        map->_pending = word;
        map->len    += bits;
        map->active += bitcount32(map->_pending);
        wah_check_invariant(map);
    }
    assert (map->len == exp_len);
}


/** Append \p count bits from raw bitmap \p data to the WAH.
 *
 * This is the main public function for ingesting raw (uncompressed) bitmap
 * data into a WAH. It dispatches to wah_add_unaligned() or wah_add_aligned()
 * depending on whether the WAH's current position is on a word boundary and
 * whether the data is byte-aligned.
 *
 * If the WAH is mid-word (has pending bits), the unaligned path is used first
 * to fill up to the next word boundary, then the aligned path handles the
 * rest.
 */
void wah_add(wah_t *map, const void *data, uint64_t count)
{
    uint32_t remain = WAH_BIT_IN_WORD - (map->len % WAH_BIT_IN_WORD);

    wah_check_invariant(map);
    if (remain != WAH_BIT_IN_WORD) {
        if (remain >= count || (remain % 8) != 0) {
            wah_add_unaligned(map, data, count);
            wah_check_invariant(map);
            return;
        } else {
            data   = wah_add_unaligned(map, data, remain);
            count -= remain;
        }
    }
    assert (map->len % WAH_BIT_IN_WORD == 0);
    wah_add_aligned(map, data, count);
    wah_check_invariant(map);
}

/** Push a run of \p Count all-ones words into the destination and advance
 *  both source enumerators past the same number of words.
 */
#define PUSH_1RUN(Count) ({                                                  \
            uint64_t __run = (Count);                                        \
                                                                             \
            wah_push_pending_1s(map, __run);                                 \
            wah_word_enum_skip(&other_en, __run);                            \
            wah_word_enum_skip(&src_en, __run);                              \
        })

/** Push a run of \p Count all-zero words into the destination and advance
 *  both source enumerators past the same number of words.
 */
#define PUSH_0RUN(Count) ({                                                  \
            uint64_t __run = (Count);                                        \
                                                                             \
            wah_push_pending_0s(map, __run);                                 \
            wah_word_enum_skip(&src_en, __run);                              \
            wah_word_enum_skip(&other_en, __run);                            \
        })

/** Copy literal words from \p data into the destination WAH, skipping \p run.
 *
 * Used during AND operations when one operand is a run of all-1s: the result
 * is simply the other operand's data. This function copies
 * min(run->remain_words, data->remain_words) words from \p data into \p map.
 *
 * The first word is handled specially (via push_pending) in case it is
 * trivial (all-0s or all-1s), to maintain WAH normalization. The remaining
 * words are bulk-copied as literals into the last bucket, and if the data
 * enumerator is in reverse mode, each copied word is bitwise-inverted.
 *
 * Both enumerators are advanced by the number of words consumed.
 */
static
void wah_copy_run(wah_t *map, wah_word_enum_t *run, wah_word_enum_t *data)
{
    uint64_t count = MIN(run->remain_words, data->remain_words);

    assert (count > 0);
    wah_word_enum_skip(run, count);

    if (unlikely(!data->current)) {
        wah_push_pending_0s(map, 1);
        wah_word_enum_next(data);
        count--;
    } else if (unlikely(data->current == UINT32_MAX)) {
        wah_push_pending_1s(map, 1);
        wah_word_enum_next(data);
        count--;
    }
    if (likely(count > 0)) {
        const wah_word_t *words;
        wah_bucket_t *bucket;
        wah_words_t bucket_words = wah_word_enum_get_cur_bucket(data);

        words = &bucket_words.tab[data->pos - data->remain_words];
        wah_word_enum_skip(data, count);

        wah_flatten_last_run(map);
        if (map->len && map->len == wah_buckets_shifted_len(map)) {
            __wah_create_bucket(map);
        }

        *wah_last_run_count(map) += count;
        bucket = tab_last(&map->_buckets);
        wah_bucket_extend(map, bucket, words, count);
        bucket_words = wah_bucket_get_words(bucket);
        if (data->reverse) {
            for (int i = bucket_words.len - count; i < bucket_words.len; i++)
            {
                bucket_words.tab[i].literal = ~bucket_words.tab[i].literal;
            }
        }
        map->len    += count * WAH_BIT_IN_WORD;
        map->active += membitcount(
            &bucket_words.tab[bucket_words.len - count],
            count * sizeof(wah_word_t));
    }
}

#define PUSH_COPY(Run, Data)  wah_copy_run(map, &(Run), &(Data))

/** Compute how many 32-bit words the shorter WAH still needs to produce to
 *  match the longer one's length, capped at WAH_MAX_WORDS_IN_RUN. Used to
 *  synthesize implicit trailing zeros when one WAH is shorter than the other.
 */
#define REMAIN_WORDS(Long, Map)  \
    MIN(((Long)->len - (Map)->len) / WAH_BIT_IN_WORD, WAH_MAX_WORDS_IN_RUN)

/** Core AND implementation supporting optional negation of either operand.
 *
 * Computes map = (map_not ? ~map : map) AND (other_not ? ~other : other).
 * This single implementation covers wah_and(), wah_and_not(), and
 * wah_not_and().
 *
 * The algorithm works by enumerating both WAHs word-by-word in lockstep.
 * For each step, the combination of enumerator states (run/literal/pending/
 * end) determines the action:
 *
 * - Run of 0s AND anything → run of 0s (short-circuit).
 * - Run of 1s AND literal → copy the literal (identity for AND).
 * - Run of 1s AND run of 1s → run of 1s.
 * - Literal AND literal → bitwise AND of the two words.
 * - Pending AND pending → bitwise AND, then set result length.
 *
 * When one WAH is shorter, its enumerator reaches WAH_ENUM_END and the
 * missing words are treated as implicit zeros (REMAIN_WORDS macro).
 *
 * The map is reset and rebuilt from scratch using a temporary duplicate of
 * the original map (allocated on the t_scope stack).
 */
void wah_and_(wah_t *map, const wah_t *other, bool map_not, bool other_not)
{
    t_scope;
    const wah_t *src = t_wah_dup(map);
    wah_word_enum_t src_en   = wah_word_enum_start(src, map_not);
    wah_word_enum_t other_en = wah_word_enum_start(other, other_not);

    wah_check_invariant(map);
    wah_reset_map(map);
    while (src_en.state != WAH_ENUM_END || other_en.state != WAH_ENUM_END) {
        if (src_en.state == WAH_ENUM_END) {
            src_en.remain_words = REMAIN_WORDS(other, map);
        } else
        if (other_en.state == WAH_ENUM_END) {
            other_en.remain_words = REMAIN_WORDS(src, map);
        }

        switch (src_en.state | (other_en.state << 2)) {
          case WAH_ENUM_END     | (WAH_ENUM_PENDING << 2):
          case WAH_ENUM_PENDING | (WAH_ENUM_END     << 2):
          case WAH_ENUM_PENDING | (WAH_ENUM_PENDING << 2):
            map->len     = MAX(other->len, src->len);
            map->_pending = src_en.current & other_en.current;
            map->active += bitcount32(map->_pending);
            wah_word_enum_next(&src_en);
            wah_word_enum_next(&other_en);
            break;

          case WAH_ENUM_RUN     | (WAH_ENUM_LITERAL << 2):
          case WAH_ENUM_END     | (WAH_ENUM_LITERAL << 2):
            if (src_en.current) {
                PUSH_COPY(src_en, other_en);
            } else {
                PUSH_0RUN(src_en.remain_words);
            }
            break;

          case WAH_ENUM_LITERAL | (WAH_ENUM_RUN     << 2):
          case WAH_ENUM_LITERAL | (WAH_ENUM_END     << 2):
            if (other_en.current) {
                PUSH_COPY(other_en, src_en);
            } else {
                PUSH_0RUN(other_en.remain_words);
            }
            break;

          case WAH_ENUM_RUN     | (WAH_ENUM_RUN     << 2):
          case WAH_ENUM_END     | (WAH_ENUM_RUN     << 2):
          case WAH_ENUM_RUN     | (WAH_ENUM_END     << 2):
            if (!other_en.current || !src_en.current) {
                uint64_t run = 0;

                if (!other_en.current) {
                    run = other_en.remain_words;
                }
                if (!src_en.current) {
                    run = MAX(run, src_en.remain_words);
                }
                PUSH_0RUN(run);
            } else {
                PUSH_1RUN(MIN(other_en.remain_words, src_en.remain_words));
            }
            break;

          default:
            map->_pending = src_en.current & other_en.current;
            if (!map->_pending) {
                wah_push_pending_0s(map, 1);
            } else if (map->_pending == UINT32_MAX) {
                wah_push_pending_1s(map, 1);
            } else {
                wah_push_pending(map, 1, bitcount32(map->_pending));
            }
            wah_word_enum_next(&src_en);
            wah_word_enum_next(&other_en);
            break;
        }
    }
    wah_check_invariant(map);

    assert (map->len == MAX(src->len, other->len));
    {
        uint64_t src_active = src->active;
        uint64_t other_active = other->active;

        if (map_not) {
            src_active = MAX(other->len, src->len) - src->active;
        }
        if (other_not) {
            other_active = MAX(other->len, src->len) - other->active;
        }
        assert (map->active <= MIN(src_active, other_active));
    }
}

void wah_and(wah_t *map, const wah_t *other)
{
    wah_and_(map, other, false, false);
}

void wah_and_not(wah_t *map, const wah_t *other)
{
    wah_and_(map, other, false, true);
}

void wah_not_and(wah_t *map, const wah_t *other)
{
    wah_and_(map, other, true, false);
}

qvector_t(wah_word_enum, wah_word_enum_t *);

/** Append \p words 32-bit words from a word enumerator into a destination
 *  WAH.
 *
 * Used by wah_multi_or() to copy data from a single source enumerator into
 * the result. Handles all enumerator states: runs are added via wah_add0s /
 * wah_add1s, literals and pending words are added via wah_add_aligned(). If
 * the enumerator runs out before \p words are consumed, the remainder is
 * filled with zeros.
 */
static void wah_add_en(wah_t *dest, wah_word_enum_t *en, uint64_t words)
{
    uint64_t exp_len = (words * WAH_BIT_IN_WORD) + dest->len;

    while (en->state != WAH_ENUM_END && words > 0) {
        uint32_t to_read  = MIN(words, en->remain_words);

        switch (en->state) {
          case WAH_ENUM_LITERAL: {
            wah_words_t bucket = wah_word_enum_get_cur_bucket(en);

            wah_add_aligned(
                dest,
                (const uint8_t *)&bucket.tab[en->pos - en->remain_words],
                to_read * WAH_BIT_IN_WORD);
          } break;

          case WAH_ENUM_PENDING:
            wah_add_aligned(dest, (const uint8_t *)&en->current,
                            WAH_BIT_IN_WORD);
            break;

          case WAH_ENUM_RUN:
            if (en->current) {
                wah_add1s(dest, to_read * WAH_BIT_IN_WORD);
            } else {
                wah_add0s(dest, to_read * WAH_BIT_IN_WORD);
            }
            break;

          case WAH_ENUM_END:
            break;
        }
        words -= to_read;
        wah_word_enum_skip(en, to_read);
    }

    if (words > 0) {
        wah_add0s(dest, words * WAH_BIT_IN_WORD);
    }
    assert (exp_len == dest->len);
}

enum {
    FLAG_RUN_0    = 0,
    FLAG_LITTERAL = 1,
    FLAG_RUN_1    = 0xff,
};

/** Compute a priority weight for a word enumerator, used by wah_multi_or().
 *
 * The weight determines which enumerator should be processed first in each
 * iteration of the multi-OR loop. The encoding is:
 * - Run of 1s: highest priority (0xff00000000 | remain_words). Processing a
 *   run of 1s first allows the entire region to be output immediately.
 * - Literal/pending: medium priority (0x0100000000 | remain_words).
 * - Run of 0s: lowest priority (0xffffffff - remain_words, so longer runs get
 *   lower values). A run of 0s in one operand means the OR result is just the
 *   other operand, so it's beneficial to identify the longest zero-run and
 *   skip it.
 * - End: zero.
 */
static uint64_t wah_word_enum_weight(const wah_word_enum_t *a)
{
    switch (a->state) {
      case WAH_ENUM_RUN:
        if (a->current) {
            return 0xff00000000UL | a->remain_words;
        } else {
            return 0xffffffff - a->remain_words;
        }

      case WAH_ENUM_LITERAL:
      case WAH_ENUM_PENDING:
        return 0x0100000000UL | a->remain_words;

      case WAH_ENUM_END:
        break;
    }

    return 0;
}

/** Compute the bitwise OR of \p len WAH bitmaps into \p dest.
 *
 * This is an optimized N-way OR that avoids the O(N) temporary copies that
 * N-1 pairwise OR operations would require. The algorithm works in chunks:
 *
 * 1. Each source WAH gets a word enumerator. Dead enumerators (ENUM_END) are
 *    removed eagerly.
 *
 * 2. At each step, the two highest-weight enumerators ("first" and "second")
 *    are identified. The weight heuristic prioritizes runs of 1s (which make
 *    the OR trivially all-1s), then literals, then runs of 0s (which are
 *    identity for OR and can be skipped).
 *
 * 3. Depending on the top-2 weights:
 *    - If second is a run of 0s: copy first's data directly (OR with 0 is
 *      identity), advancing all enumerators.
 *    - If only one enumerator remains: copy it entirely.
 *    - If first is a run: emit it directly and advance all.
 *    - Otherwise: buffer up to 1024 words, OR-ing all enumerators into the
 *      buffer, then flush the buffer into dest (grouping consecutive runs of
 *      0s, 1s, and literals).
 *
 * If \p dest is NULL, a new WAH is allocated and returned.
 */
wah_t *wah_multi_or(const wah_t *src[], int len, wah_t * restrict dest)
{
    t_scope;
    qv_t(wah_word_enum) enums;
    uint32_t buffer[1024];
    byte     buffer_flags[countof(buffer)];
    uint64_t exp_len = 0;
    uint64_t min_act = 0;
    uint64_t max_act = 0;

    if (!dest) {
        dest = wah_new();
    } else {
        wah_reset_map(dest);
    }

    t_qv_init(&enums, len);
    for (int i = 0; i < len; i++) {
        wah_word_enum_t *en = t_new_raw(wah_word_enum_t, 1);

        exp_len = MAX(exp_len, src[i]->len);
        min_act = MAX(min_act, src[i]->active);
        max_act += src[i]->active;
        wah_check_invariant(src[i]);
        *en = wah_word_enum_start(src[i], false);
        if (en->state != WAH_ENUM_END) {
            qv_append(&enums, en);
        }
    }
    max_act = MIN(exp_len, max_act);

    if (enums.len == 1) {
        wah_copy(dest, enums.tab[0]->map);
        return dest;
    }

#define CONSUME_ALL(amount, skip_first)  do {                                \
        const uint64_t __amount = (amount);                                  \
        const bool __skip_first = (skip_first);                              \
                                                                             \
        tab_for_each_pos_safe(pos, &enums) {                   \
            wah_word_enum_t *en = enums.tab[pos];                            \
                                                                             \
            if (!__skip_first || en != first) {                              \
                wah_word_enum_skip(en, __amount);                            \
            }                                                                \
            if (en->state == WAH_ENUM_END) {                                 \
                if (pos != enums.len - 1) {                                  \
                    enums.tab[pos] = enums.tab[enums.len - 1];               \
                }                                                            \
                enums.len--;                                                 \
            }                                                                \
        }                                                                    \
    } while (0)

    while (enums.len) {
        uint32_t bits = 0;
        uint32_t buf_pos = 0;
        uint32_t end_pos = 0;
        uint64_t first_weight  = 0;
        uint64_t second_weight = 0;
        wah_word_enum_t *first = NULL;
        wah_word_enum_t *second = NULL;

        tab_for_each_entry(e, &enums) {
            uint64_t w = wah_word_enum_weight(e);

            if (w > first_weight || !first) {
                second        = first;
                second_weight = first_weight;
                first = e;
                first_weight = w;
            } else
            if (w > second_weight || !second) {
                second        = e;
                second_weight = w;
            }
        }
        assert (first);
        assert (second || enums.len == 1);

        if (second && second->state == WAH_ENUM_RUN && !second->current) {
            wah_add_en(dest, first, second->remain_words);
            CONSUME_ALL(second->remain_words, true);
            continue;
        } else
        if (enums.len == 1 && first->state != WAH_ENUM_PENDING) {
            uint64_t to_consume = (first->map->len - dest->len) / 32;
            wah_add_en(dest, first, to_consume);
            if (first->state == WAH_ENUM_END) {
                enums.len--;
            }
            continue;
        } else
        if (first->state == WAH_ENUM_RUN) {
            if (first->current) {
                wah_add1s(dest, (uint64_t)first->remain_words * 32);
            } else {
                wah_add0s(dest, (uint64_t)first->remain_words * 32);
            }
            CONSUME_ALL(first->remain_words, false);
            continue;
        }

        p_clear(&buffer_flags, 1);
        tab_for_each_pos_safe(pos, &enums) {
            uint32_t     remain  = countof(buffer);
            uint32_t     en_bits = 0;
            wah_word_enum_t *en = enums.tab[pos];

            buf_pos = 0;
            while (en->state != WAH_ENUM_END && remain > 0) {
                uint32_t to_consume = MIN(remain, en->remain_words);

                switch (en->state) {
                  case WAH_ENUM_LITERAL: {
                    wah_words_t bucket = wah_word_enum_get_cur_bucket(en);
                    const uint32_t *data = (const uint32_t *)bucket.tab;

                    data = &data[en->pos - en->remain_words];
                    for (uint32_t i = 0; i < to_consume; i++) {
                        if (buffer_flags[buf_pos + i] != FLAG_RUN_1) {
                            if (buffer_flags[buf_pos + i] == FLAG_RUN_0) {
                                buffer[buf_pos + i]  = data[i];
                                buffer_flags[buf_pos + i] = FLAG_LITTERAL;
                            } else {
                                buffer[buf_pos + i] |= data[i];
                            }
                            if (buffer[buf_pos + i] == 0xffffffff) {
                                buffer_flags[buf_pos + i] = FLAG_RUN_1;
                            }
                        }
                    }
                    en_bits += to_consume * 32;
                  } break;

                  case WAH_ENUM_RUN:
                    if (en->current) {
                        memset(&buffer_flags[buf_pos], 0xff, to_consume);
                    }
                    en_bits += to_consume * 32;
                    break;

                  case WAH_ENUM_PENDING:
                    if (buffer_flags[buf_pos] != FLAG_RUN_1) {
                        if (buffer_flags[buf_pos] == FLAG_RUN_0) {
                            buffer[buf_pos] = en->current;
                            buffer_flags[buf_pos] = FLAG_LITTERAL;
                        } else {
                            buffer[buf_pos] |= en->current;
                            if (buffer[buf_pos] == 0xffffffff) {
                                buffer_flags[buf_pos] = FLAG_RUN_1;
                            }
                        }
                    }
                    en_bits += en->map->len % 32;
                    break;

                  case WAH_ENUM_END:
                    e_panic("this should not happen");
                }
                wah_word_enum_skip(en, to_consume);
                buf_pos += to_consume;
                remain  -= to_consume;
            }
            bits = MAX(bits, en_bits);
            if (en->state == WAH_ENUM_END) {
                if (pos != enums.len - 1) {
                    enums.tab[pos] = enums.tab[enums.len - 1];
                }
                enums.len--;
            }
        }
        assert (!enums.len || (bits % 32) == 0);

        buf_pos = 0;
        end_pos = DIV_ROUND_UP(bits, 32);
        while (buf_pos < end_pos) {
            byte val = buffer_flags[buf_pos];
            uint32_t end = buf_pos + 1;

            while (end < end_pos) {
                if (buffer_flags[end] != val) {
                    break;
                }
                end++;
            }

            switch (val) {
              case FLAG_RUN_1:
                wah_add1s(dest, 32 * (end - buf_pos));
                break;

              case FLAG_RUN_0:
                wah_add0s(dest, 32 * (end - buf_pos));
                break;

              case FLAG_LITTERAL:
                if (32 * (end - buf_pos) > bits) {
                    wah_add_aligned(dest,  (const uint8_t *)&buffer[buf_pos],
                                    bits);
                } else {
                    wah_add_literal(dest, (const uint8_t *)&buffer[buf_pos],
                                    4 * (end - buf_pos));
                }
                break;
            }

            bits -= 32 * (end - buf_pos);
            buf_pos = end;
        }
    }

    wah_check_invariant(dest);
    assert (dest->len    == exp_len);
    assert (dest->active >= min_act);
    assert (dest->active <= max_act);
    return dest;
}

/** Compute map = map OR other, in place.
 *
 * Implemented by duplicating map, then calling wah_multi_or() on the
 * duplicate and other, writing the result back into map.
 */
void wah_or(wah_t *map, const wah_t *other)
{
    t_scope;
    const wah_t *srcs[] = { t_wah_dup(map), other };

    wah_multi_or(srcs, countof(srcs), map);
}

#undef PUSH_COPY
#undef PUSH_0RUN
#undef PUSH_1RUN

/** Compute the bitwise NOT of a WAH, in place.
 *
 * This is the only bitwise operation that can be done truly in-place because
 * the WAH structure doesn't change—only the data values are inverted.
 *
 * For each chunk: the run bit is flipped, and every literal word is
 * bitwise-negated. The pending word is also negated (masked to valid bits).
 * The active count becomes len - active.
 */
void wah_not(wah_t *map)
{
    wah_check_invariant(map);

    tab_for_each_ptr(bucket, &map->_buckets) {
        uint32_t pos = 0;
        wah_words_t words = wah_bucket_get_words(bucket);

        while (pos < (uint32_t)words.len) {
            wah_header_t *head  = &words.tab[pos++].head;
            uint32_t      count = words.tab[pos++].count;

            head->bit = !head->bit;
            for (uint32_t i = 0; i < count; i++) {
                words.tab[pos].literal = ~words.tab[pos].literal;
                pos++;
            }
        }
    }

    if ((map->len % WAH_BIT_IN_WORD) != 0) {
        map->_pending = ~map->_pending & BITMASK_LT(uint32_t, map->len);
    }
    map->active = map->len - map->active;
    wah_check_invariant(map);
}

/** Get the value of a single bit at position \p pos (O(n) — inefficient).
 *
 * Walks through the WAH chunks sequentially until the chunk containing \p pos
 * is found. For a run, returns the run's bit value. For a literal section,
 * locates the specific word and tests the bit.
 *
 * If \p pos falls within the pending bits (the last sub-word), it is read
 * directly from map->_pending.
 *
 * This function is O(number of chunks) and should only be used in tests or
 * debugging. For production iteration, use the bit enumerator.
 */
bool wah_get(const wah_t *map, uint64_t pos)
{
    wah_bucket_t *bucket;
    wah_words_t bucket_words;
    uint64_t count;
    uint32_t remain = map->len % WAH_BIT_IN_WORD;
    int i = 0;

    if (pos >= map->len) {
        return false;
    }
    if (pos >= map->len - remain) {
        pos %= WAH_BIT_IN_WORD;
        return map->_pending & (1 << pos);
    }

    bucket = wah_buckets_get_bucket(map, &pos);
    bucket_words = wah_bucket_get_words(bucket);

    while (i < bucket_words.len) {
        wah_header_t head  = bucket_words.tab[i++].head;
        uint32_t     words = bucket_words.tab[i++].count;

        count = head.words * WAH_BIT_IN_WORD;
        if (pos < count) {
            return !!head.bit;
        }
        pos -= count;

        count = words * WAH_BIT_IN_WORD;
        if (pos < count) {
            i   += pos / WAH_BIT_IN_WORD;
            pos %= WAH_BIT_IN_WORD;
            return !!(bucket_words.tab[i].literal & (1 << pos));
        }
        pos -= count;
        i   += words;
    }
    e_panic("this should not happen");
}

/* }}} */
/* Open/store existing WAH {{{ */

/** Context for wah_init_from_data(), tracking state while parsing serialized
 *  WAH data into buckets.
 */
typedef struct from_data_ctx_t {
    /** WAH being built from data.
     */
    wah_t *nonnull map;

    /** Always pointing at the first word of the current bucket.
     */
    wah_word_t *nonnull tab;

    /** Position of the next chunk to consider for the current bucket.
     */
    ssize_t pos;

    /** Number of words left in tab to process.
     */
    ssize_t size;

    /** Number of bits already included in the current bucket.
     */
    uint64_t bucket_bits_len;

    /** Optional allocated current bucket.
     *
     * In normal cases, the WAH buckets are created at once after “counting”
     * the right amount of bits and are statically pointing to the data
     * memory. In this case, ctx.bucket is always NULL.
     *
     * However, when a bucket needs to be split because it doesn't fit with
     * the current WAH_BITS_IN_BUCKET, its content will have to be duplicated
     * and allocated in memory, in which case ctx.bucket will point to this
     * allocated bucket. Once ctx.bucket is set, all the following chunks must
     * be copied in it until the bucket is full.
     */
    wah_bucket_t *nullable bucket;
} from_data_ctx_t;

/** Finalize the current bucket and prepare the context for the next one.
 *
 * Advances ctx->tab past the consumed words, resets the bucket-local
 * position and bit count. Unless this is the last bucket (end_of_wah),
 * also resets the run position tracking fields.
 *
 * Called whenever a bucket boundary is reached during deserialization.
 */
static inline void
from_data_ctx_reset_bucket(from_data_ctx_t *ctx, bool end_of_wah)
{
    ctx->tab  += ctx->pos;
    ctx->size -= ctx->pos;
    ctx->pos = 0;
    ctx->bucket = NULL;
    ctx->bucket_bits_len = 0;
    if (likely(!end_of_wah)) {
        /* If we were treating the last words we must not reset the
         * previous/last run positions.
         */
        ctx->map->previous_run_pos = -1;
        ctx->map->last_run_pos     = -1;
    }
}

/** Split a WAH chunk that spans a bucket boundary during deserialization.
 *
 * When loading serialized WAH data, a single chunk (header run + literals)
 * may encode more bits than fit in WAH_BITS_IN_BUCKET. This function breaks
 * it into multiple buckets:
 *
 * 1. If no allocated bucket exists yet, one is created and all previously
 *    scanned (but not yet bucketed) words are copied into it.
 *
 * 2. The header run is split: as many run-words as fit in the current bucket
 *    are written, then a new bucket is started for the remainder.
 *    Each bucket-boundary creates a new chunk header.
 *
 * 3. The literal words are similarly split across bucket boundaries: for each
 *    bucket, as many literals as fit are appended, and when the bucket is
 *    full, a new one is created with an empty run header to hold the
 *    remaining literals.
 *
 * This is a rare path that only triggers for WAH data that was serialized
 * with a larger bucket size than the current WAH_BITS_IN_BUCKET.
 */
static void
from_data_split_chunk(from_data_ctx_t *ctx, wah_header_t head, uint64_t words)
{
    wah_t *map = ctx->map;

    if (!ctx->bucket) {
        ctx->bucket = wah_create_bucket(map, 0);

        /* Copy all the previous chunks not already part of a bucket. */
        wah_bucket_extend(map, ctx->bucket, ctx->tab, ctx->pos);
    }
    assert(ctx->bucket_bits_len < WAH_BITS_IN_BUCKET);

    /* First we append all the bits of the header run and we split them in as
     * many buckets as needed.
     */
    for (;;) {
        wah_header_t to_add = head;
        uint64_t avail_words;

        avail_words = (WAH_BITS_IN_BUCKET - ctx->bucket_bits_len) /
            WAH_BIT_IN_WORD;
        to_add.words = MIN(head.words, avail_words);
        wah_bucket_append(map, ctx->bucket, (wah_word_t){ .head = to_add });

        head.words -= to_add.words;
        if (head.words) {
            /* Close this chunk, and create a new bucket. */
            wah_bucket_append(map, ctx->bucket, (wah_word_t){ .count = 0 });
            from_data_ctx_reset_bucket(ctx, false);
            ctx->bucket = wah_create_bucket(map, 0);
        } else {
            /* What's left of the run fits in the current bucket. */
            ctx->bucket_bits_len += to_add.words * WAH_BIT_IN_WORD;
            break;
        }
    }

    /* Chunk run has been handled, skip it. */
    map->previous_run_pos = map->last_run_pos;
    map->last_run_pos     = wah_bucket_len(ctx->bucket) - 1;
    ctx->pos += 2;

    /* We now have to split the uncompressed words if necessary. */
    for (;;) {
        uint64_t avail_words, to_add;

        avail_words = (WAH_BITS_IN_BUCKET - ctx->bucket_bits_len) /
            WAH_BIT_IN_WORD;
        to_add = MIN(words, avail_words);
        wah_bucket_append(map, ctx->bucket, (wah_word_t){ .count = to_add });
        if (to_add) {
            /* Copy and consume the selected literals. */
            wah_bucket_extend(map, ctx->bucket, &ctx->tab[ctx->pos], to_add);
            ctx->pos += to_add;
            words    -= to_add;
        }

        if (words) {
            /* Rotate the current bucket for the next batch of literals. */
            from_data_ctx_reset_bucket(ctx, false);
            ctx->bucket = wah_create_bucket(map, 0);

            map->last_run_pos = 0;
            wah_bucket_append(map, ctx->bucket,
                              (wah_word_t){ .head = { .words = 0 }});
        } else {
            ctx->bucket_bits_len += to_add * WAH_BIT_IN_WORD;
            break;
        }
    }
}

/** Initialize a WAH from serialized (on-disk) data.
 *
 * Parses the raw WAH-encoded data and reconstructs the bucket structure. The
 * generated WAH is read-only and references memory from \p data (which must
 * remain valid for the WAH's lifetime).
 *
 * The algorithm walks through the serialized chunks and groups them into
 * buckets of at most WAH_BITS_IN_BUCKET bits each. Two optimizations:
 *
 * - Overfilling: if the WAH starts with a long run of the same bit value,
 *   the first bucket is allowed to exceed WAH_BITS_IN_BUCKET (tracked via
 *   buckets_shift) to avoid creating many trivial buckets.
 *
 * - Zero-copy: when possible, buckets point directly into the \p data
 *   buffer (via wah_bucket_set_static()) rather than copying the words.
 *   Copying only occurs when a chunk must be split across bucket boundaries
 *   (which should not happen for WAH correctly normalized).
 *
 * Returns \p map on success, NULL on malformed data.
 */
wah_t *wah_init_from_data(wah_t *map, pstream_t data)
{
    from_data_ctx_t ctx;
    bool overfill_bucket = true;

    p_clear(map, 1);
    qv_init(&map->_buckets);

    THROW_NULL_IF(ps_len(&data) % sizeof(wah_word_t));
    THROW_NULL_IF(ps_len(&data) < 2 * sizeof(wah_word_t));

    map->previous_run_pos = -1;
    map->last_run_pos = -1;

    p_clear(&ctx, 1);
    ctx.map = map;
    ctx.tab = (wah_word_t *)data.p;
    ctx.size = ps_len(&data) / sizeof(wah_word_t);

    if (likely(ctx.size >= 1)) {
        uint64_t bits_count = ctx.tab[0].head.words * WAH_BIT_IN_WORD;

        if (!bits_count && ctx.size >= 4 && ctx.tab[1].count == 1 &&
            (!ctx.tab[2].literal || ctx.tab[2].literal == UINT32_MAX) &&
            !!ctx.tab[2].literal == ctx.tab[3].head.bit)
        {
            /* Because of an old bug in wah_flatten_last_run() we may have an
             * overfilled WAH looking like that:
             *     { words: 0; bit: 0 }, { count: 1 }, { literal: 0x0 },
             *     { words: <a lot>; bit: 0 }, { count: 0 }
             *
             * The ugly if above detects this case…
             */
            bits_count = (1ULL + ctx.tab[3].head.words) * WAH_BIT_IN_WORD;
        }

        assert(WAH_BITS_IN_BUCKET < WAH_MAX_WORDS_IN_RUN * 8);
        if (bits_count <= WAH_BITS_IN_BUCKET) {
            /* The first bucket wasn't overfilled and thus is either not
             * filled of the same bit or was created before the overfilling
             * feature. In both case we must not try to overfill it.
             *
             * XXX: this early detection only works because WAH_BITS_IN_BUCKET
             * is currently lower than the maximum number of bits in a run.
             */
            overfill_bucket = false;
        }
    }

    while (ctx.pos < ctx.size - 1) {
        uint64_t active = 0;
        wah_header_t *head = &ctx.tab[ctx.pos].head;
        int64_t      words = ctx.tab[ctx.pos + 1].count;
        uint64_t chunk_len = WAH_BIT_IN_WORD * (head->words + words);

        THROW_NULL_IF(words > ctx.size || ctx.pos + 2 > ctx.size - words);

        if (head->bit) {
            active = WAH_BIT_IN_WORD * head->words;
        }
        if (words) {
            active += membitcount(&ctx.tab[ctx.pos + 2],
                                  words * sizeof(wah_word_t));
        }

        if (overfill_bucket && (
                /* the chunk is not full of 0s or 1s or… */
                (active && active != chunk_len) ||
                /* the WAH isn't empty and filled with opposite bit */
                (map->len && !!active != !!map->active)))
        {
            assert(!ctx.bucket);
            if (ctx.bucket_bits_len >= WAH_BITS_IN_BUCKET) {
                /* This chunk mark the end of an overfilled first bucket full
                 * of 0s or 1s and must not be included in the bucket.
                 */
                assert(!map->_buckets.len);
                assert((const void *)ctx.tab == data.p);
                ctx.bucket = wah_create_bucket(map, 0);

                /* The context must be fix up in order to restart from the
                 * current chunk and not the next one.
                 */
                wah_bucket_set_static(ctx.bucket, ctx.tab, ctx.pos);
                wah_set_buckets_shift(map);

                from_data_ctx_reset_bucket(&ctx, false);
            }
            overfill_bucket = false;
        }
        map->active += active;
        map->len += chunk_len;

        if (!overfill_bucket && unlikely(ctx.bucket_bits_len + chunk_len >
                                         WAH_BITS_IN_BUCKET))
        {
            /* This wah does not respect the max length of the buckets. */
            e_warning("WAH bucket len is exceeding expected len: %ju > %ju",
                      ctx.bucket_bits_len + chunk_len, WAH_BITS_IN_BUCKET);
            from_data_split_chunk(&ctx, *head, words);
        } else {
            ctx.bucket_bits_len += chunk_len;
            if (unlikely(ctx.bucket)) {
                /* We have an allocated bucket, add this chunk. */
                assert(!overfill_bucket);
                map->previous_run_pos = map->last_run_pos;
                map->last_run_pos     = wah_bucket_len(ctx.bucket);
                wah_bucket_extend(map, ctx.bucket, (wah_word_t *)head,
                                  words + 2);
            } else {
                /* No allocated bucket, the chunk will be added after. */
                map->previous_run_pos = map->last_run_pos;
                map->last_run_pos     = ctx.pos;

                /* Unlike a normal wah_t, last_run_pos has been initialized to
                 * -1 instead of 0 so previous_run_pos would correctly take -1
                 *  on the first iteration here.
                 */
                assert(map->last_run_pos > 0 || map->previous_run_pos < 0);
            }
            ctx.pos += 2 + words;
        }

        if (!overfill_bucket && ctx.bucket_bits_len >= WAH_BITS_IN_BUCKET) {
            /* The current bucket is full, close it. */
            assert(ctx.bucket_bits_len == WAH_BITS_IN_BUCKET);
            if (!ctx.bucket) {
                ctx.bucket = wah_create_bucket(map, 0);
                wah_bucket_set_static(ctx.bucket, ctx.tab, ctx.pos);
            }
            from_data_ctx_reset_bucket(&ctx, ctx.pos >= ctx.size);
        } else {
            assert(!ctx.bucket || !overfill_bucket);
        }
    }

    THROW_NULL_IF(ctx.pos != ctx.size);
    assert(!ctx.bucket);
    if (ctx.size) {
        ctx.bucket = wah_create_bucket(map, 0);
        wah_bucket_set_static(ctx.bucket, ctx.tab, ctx.size);
        if (overfill_bucket) {
            wah_set_buckets_shift(map);
        }
    }

    wah_check_invariant(map);
    return map;
}

wah_t *wah_new_from_data(pstream_t data)
{
    wah_t *map = p_new_raw(wah_t, 1);
    wah_t *ret;

    ret = wah_init_from_data(map, data);
    if (!ret) {
        wah_delete(&map);
    }
    return ret;
}

const qv_t(wah_bucket) *wah_get_storage(const wah_t *wah)
{
    assert (wah->len % WAH_BIT_IN_WORD == 0);
    return &wah->_buckets;
}

/** Return the total number of wah_word_t across all buckets.
 *
 * This is the serialized size of the WAH in words (multiply by
 * sizeof(wah_word_t) for bytes). Used by wah_get_storage_size() and
 * mp_wah_get_storage_lstr().
 */
uint64_t wah_get_storage_len(const wah_t *wah)
{
    uint64_t res = 0;

    tab_for_each_ptr(bucket, &wah->_buckets) {
        res += wah_bucket_len(bucket);
    }
    return res;
}

lstr_t mp_wah_get_storage_lstr(mem_pool_t *mp, const wah_t *wah)
{
    const qv_t(wah_bucket) *buckets;
    qv_t(wah_word) all_buckets;
    size_t storage_len;

    storage_len = wah_get_storage_len(wah);
    if (storage_len > MEM_ALLOC_MAX) {
        /* Cannot allocate so much memory. */
        return LSTR_NULL_V;
    }

    mp_qv_init(mp, &all_buckets, storage_len);
    buckets = wah_get_storage(wah);

    tab_for_each_ptr(bucket, buckets) {
        wah_words_t words = wah_bucket_get_words(bucket);
        qv_extend_tab(&all_buckets, &words);
    }

    return LSTR_DATA_V(all_buckets.tab,
                       all_buckets.len * sizeof(all_buckets.tab[0]));
}

lstr_t t_wah_get_storage_lstr(const wah_t *wah)
{
    return mp_wah_get_storage_lstr(t_pool(), wah);
}

/* }}} */
/* Printer {{{ */

static
uint64_t wah_debug_print_run(uint64_t pos, const wah_header_t head)
{
    if (head.words != 0) {
        fprintf(stderr, "\e[1;30m[%08x] \e[33mRUN %d \e[0m%u words "
                "(%llu bits)\n", (uint32_t)pos, head.bit, head.words,
                (uint64_t)head.words * 32ull);
    }
    return head.words * 32;
}

static
void wah_debug_print_literal(uint64_t pos, const uint32_t lit)
{
    fprintf(stderr, "\e[1;30m[%08x] \e[33mLITERAL \e[0m%08x\n",
            (uint32_t)pos, lit);
}

static
uint64_t wah_debug_print_literals(uint64_t pos, uint32_t len)
{
    if (len != 0) {
        fprintf(stderr, "\e[1;30m[%08x] \e[33mLITERAL \e[0m%u words\n",
                (uint32_t)pos, len);
    }
    return len * 32;
}

static
void wah_debug_print_pending(uint64_t pos, const uint32_t pending, int len)
{
    if (len > 0) {
        fprintf(stderr, "\e[1;30m[%08x] \e[33mPENDING \e[0m%d bits: %08x\n",
                (uint32_t)pos, len, pending);
    }
}


void wah_debug_print(const wah_t *wah, bool print_content)
{
    wah_words_t bucket_words = wah_bucket_get_words(&wah->_buckets.tab[0]);
    uint64_t pos = 0;
    uint32_t len = 0;
    int      off = 0;
    int      bucket_pos = 0;

    for (;;) {
        if (print_content) {
            for (uint32_t i = 0; i < len; i++) {
                wah_debug_print_literal(pos, bucket_words.tab[off++].literal);
                pos += 32;
            }
        } else {
            off += len;
            pos += wah_debug_print_literals(pos, len);
        }
        if (off < bucket_words.len) {
            pos += wah_debug_print_run(pos, bucket_words.tab[off++].head);
            len  = bucket_words.tab[off++].count;
        } else {
            if (++bucket_pos >= wah->_buckets.len) {
                break;
            }
            fprintf(stderr, "  \e[1;32m         CHANGE TO BUCKET %d\e[0m\n",
                    bucket_pos + 1);
            bucket_words = wah_bucket_get_words(
                &wah->_buckets.tab[bucket_pos]);
            off = 0;
            len = 0;
        }
    }
    wah_debug_print_pending(pos, wah->_pending, wah->len % 32);
}

/* }}} */
/* {{{ Testing helpers. */

#ifndef NDEBUG
void wah_set_bits_in_bucket(uint64_t nb_bits)
{
    _G.bits_in_bucket = nb_bits;
    assert (_G.bits_in_bucket % WAH_BIT_IN_WORD == 0);
}

void wah_reset_bits_in_bucket(void)
{
    wah_set_bits_in_bucket(WAH_BITS_IN_BUCKET_DEFAULT);
}

void wah_set_check_normalized(bool value)
{
    _G.check_normalized = value;
}
#endif /* NDEBUG */

/* }}} */
