/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_QPS_BITMAP_H
#define IS_LIB_COMMON_QPS_BITMAP_H

#include <lib-common/qps.h>

/** \defgroup qkv__ll__bitmap QPS Bitmap
 * \ingroup qkv__ll
 * \brief QPS Bitmap
 *
 * \{
 *
 * This bitmap implementation is a 3-level trie mapping a key to a bit. It
 * supports both simple bitmaps and "nullable" bitmap. The nullable
 * implementation associate a pair of bit to each key with the following
 * possible combinations:
 *   * 00: NULL
 *   * 01: unused combination
 *   * 10: bit set at 0
 *   * 11: bit set at 1
 */

#define QPS_BITMAP_ROOTS      64
#define QPS_BITMAP_DISPATCH   2048
#define QPS_BITMAP_WORD       (QPS_PAGE_SIZE / 8)
#define QPS_BITMAP_NULL_WORD  (2 * QPS_BITMAP_WORD)
#define QPS_BITMAP_LEAF       (8 * QPS_PAGE_SIZE)

/* Typedefs {{{ */

typedef qps_pg_t qps_bitmap_node_t;

typedef union qps_bitmap_key_t {
    struct {
#if __BYTE_ORDER == __BIG_ENDIAN
        unsigned root     : 6;
        unsigned dispatch : 11;
        unsigned word     : 9;
        unsigned bit      : 6;
#else
        unsigned bit      : 6;
        unsigned word     : 9;
        unsigned dispatch : 11;
        unsigned root     : 6;
#endif
    };
    struct {
#if __BYTE_ORDER == __BIG_ENDIAN
        unsigned root_null     : 6;
        unsigned dispatch_null : 11;
        unsigned word_null     : 10;
        unsigned bit_null      : 5;
#else
        unsigned bit_null      : 5;
        unsigned word_null     : 10;
        unsigned dispatch_null : 11;
        unsigned root_null     : 6;
#endif
    };
    uint32_t key;
} qps_bitmap_key_t;

typedef struct qps_bitmap_dispatch_node {
    qps_bitmap_node_t node        __attribute__((packed));
    uint16_t          active_bits __attribute__((packed));
} qps_bitmap_dispatch_t[QPS_BITMAP_DISPATCH];

#define QPS_BITMAP_SIG  "QPS_bmap/v01.00"
typedef struct qps_bitmap_root_t {
    /* Signature */
    uint8_t  sig[16];

    /* Structure description */
    bool is_nullable;
    qps_bitmap_node_t roots[QPS_BITMAP_ROOTS];
} qps_bitmap_root_t;

typedef struct qps_bitmap_t {
    qps_t *qps;
    uint32_t bitmap_gen;

    union {
        qps_bitmap_root_t *root;
        qps_hptr_t         root_cache;
    };
} qps_bitmap_t;

typedef enum qps_bitmap_state_t {
    QPS_BITMAP_0,
    QPS_BITMAP_1,
    QPS_BITMAP_NULL,
} qps_bitmap_state_t;

/* }}} */
/* Public API {{{ */

qps_handle_t qps_bitmap_create(qps_t *qps, bool is_nullable) __leaf;
void qps_bitmap_destroy(qps_bitmap_t *map) __leaf;
void qps_bitmap_clear(qps_bitmap_t *map) __leaf;

qps_bitmap_state_t qps_bitmap_get(qps_bitmap_t *map, uint32_t row) __leaf;
qps_bitmap_state_t qps_bitmap_set(qps_bitmap_t *map, uint32_t row) __leaf;
qps_bitmap_state_t qps_bitmap_reset(qps_bitmap_t *map, uint32_t row) __leaf;
qps_bitmap_state_t qps_bitmap_remove(qps_bitmap_t *map, uint32_t row) __leaf;

void qps_bitmap_compute_stats(qps_bitmap_t *map, size_t *memory,
                              uint32_t *entries, uint32_t *slots) __leaf;

static inline void
qps_bitmap_init(qps_bitmap_t *map, qps_t *qps, qps_handle_t handle)
{
    p_clear(map, 1);
    map->qps = qps;
    qps_hptr_init(qps, handle, &map->root_cache);
    assert (strequal(QPS_BITMAP_SIG, (const char *)map->root->sig));
}

/* }}} */
/* {{{ Bitmap enumerator */

typedef struct qps_bitmap_enumerator_t {
    /* The layout here starts the same way as qhat_tree_enumerator_t, so
     * we can easily access some properties of the enumerator like the key or
     * end variable without the hassle to synchronize things. */
    qps_bitmap_key_t key;
    bool end;

    /* This value is there to provide compatibility with
     * 'qhat_enumerator_t', as the QPS bitmap is also used as a
     * presence data structure for nullable QPS hat entries. */
    bool reserved;

    bool value;
    bool is_nullable;

    qps_bitmap_t *map;

    const uint64_t *leaf;
    const qps_bitmap_dispatch_t *dispatch;

    uint64_t current_word;
    uint32_t bitmap_gen;
} qps_bitmap_enumerator_t;

/* {{{ Private functions - nullable specialization. */

/* '_nu' suffix is for "nullable". */
static inline
void qps_bitmap_enumerator_find_dispatch_nu(qps_bitmap_enumerator_t *en,
                                            qps_bitmap_key_t key);

static inline
void qps_bitmap_enumerator_find_leaf_nu(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key);
static inline
void qps_bitmap_enumerator_find_bit_nu(qps_bitmap_enumerator_t *en,
                                       qps_bitmap_key_t key);
static inline
void qps_bitmap_enumerator_find_word_nu(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key);

static inline
void qps_bitmap_enumerator_dispatch_up_nu(qps_bitmap_enumerator_t *en,
                                          qps_bitmap_key_t key,
                                          qps_bitmap_key_t new_key)
{
    assert(en->is_nullable);
    if (key.root != new_key.root) {
        if (new_key.root == 0) {
            en->end = true;
        } else {
            qps_bitmap_enumerator_find_dispatch_nu(en, new_key);
        }
    } else
    if (key.dispatch != new_key.dispatch) {
        qps_bitmap_enumerator_find_leaf_nu(en, new_key);
    } else {
        qps_bitmap_enumerator_find_word_nu(en, new_key);
    }
}

static inline
void qps_bitmap_enumerator_find_dispatch_nu(qps_bitmap_enumerator_t *en,
                                            qps_bitmap_key_t key)
{
    assert(en->is_nullable);
    en->dispatch = NULL;
    for (unsigned i = key.root; i < QPS_BITMAP_ROOTS; i++) {
        if (en->map->root->roots[i] != 0) {
            en->key.key  = 0;
            en->key.root = i;
            en->dispatch
                = (const qps_bitmap_dispatch_t *)qps_pg_deref(en->map->qps,
                                                              en->map->root->roots[i]);
            if (key.root != i) {
                key = en->key;
            }
            qps_bitmap_enumerator_find_leaf_nu(en, key);
            return;
        }
    }
    en->end = true;
}

static inline
void qps_bitmap_enumerator_find_leaf_nu(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key)
{
    assert(en->is_nullable);
    en->leaf = NULL;
    assert (en->dispatch != NULL);
    for (unsigned i = key.dispatch; i < QPS_BITMAP_DISPATCH; i++) {
        if ((*en->dispatch)[i].node != 0) {
            en->key.word     = 0;
            en->key.bit      = 0;
            en->key.dispatch = i;
            en->leaf
                = (const uint64_t *)qps_pg_deref(en->map->qps,
                                                 (*en->dispatch)[i].node);
            if (key.dispatch != i) {
                key = en->key;
            }
            qps_bitmap_enumerator_find_word_nu(en, key);
            return;
        }
    }

    key = en->key;
    key.root++;
    key.dispatch = 0;
    key.word     = 0;
    key.bit      = 0;
    qps_bitmap_enumerator_dispatch_up_nu(en, en->key, key);
}

static inline
void qps_bitmap_enumerator_find_word_nu(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key)
{
    assert(en->is_nullable);
    assert (en->leaf != NULL);
    for (unsigned i = key.word_null; i < QPS_BITMAP_NULL_WORD; i++) {
        if (en->leaf[i] != 0) {
            en->key.bit_null  = 0;
            en->key.word_null = i;
            en->current_word = en->leaf[i];

            if (key.word_null != i) {
                key = en->key;
            }
            qps_bitmap_enumerator_find_bit_nu(en, key);
            return;
        }
    }

    key = en->key;
    key.word = 0;
    key.bit  = 0;
    key.key += 1 << 15; /* bitsizeof(word) + bitsizeof(bit) */
    qps_bitmap_enumerator_dispatch_up_nu(en, en->key, key);
}

static inline
void qps_bitmap_enumerator_find_bit_nu(qps_bitmap_enumerator_t *en,
                                       qps_bitmap_key_t key)
{
    assert(en->is_nullable);

    while (en->current_word != 0) {
        unsigned bit = bsf64(en->current_word);

        en->value          = !(bit & 1);
        en->current_word >>= bit & ~1;
        en->key.bit_null  += (bit >> 1);

        if (en->key.bit_null >= key.bit_null) {
            return;
        }

        /* Skip bit */
        en->current_word  &= ~UINT64_C(3);
    }
    key = en->key;
    key.bit_null = 0;
    key.key     += 1 << 5;
    qps_bitmap_enumerator_dispatch_up_nu(en, en->key, key);
}

/* }}} */
/* {{{ Public functions - nullable specialization */

static inline bool
qps_bitmap_enumerator_is_sync(const qps_bitmap_enumerator_t *en)
{
    return en->bitmap_gen == en->map->bitmap_gen;
}

static inline
void qps_bitmap_enumerator_next_nu(qps_bitmap_enumerator_t *en, bool safe)
{
    qps_bitmap_key_t key = en->key;

    assert(en->is_nullable);

    /* Should have used the "safe" option. */
    assert(safe || qps_bitmap_enumerator_is_sync(en));
    if (safe && !qps_bitmap_enumerator_is_sync(en)) {
        en->bitmap_gen = en->map->bitmap_gen;
        qps_bitmap_enumerator_find_dispatch_nu(en, key);

        if (en->end || en->key.key != key.key) {
            /* The key was removed, the enumerator is already set on the next
             * element. */
            return;
        }
    }

    en->current_word &= ~UINT64_C(3);
    key.bit_null++;
    qps_bitmap_enumerator_find_bit_nu(en, key);
}

static inline
void qps_bitmap_enumerator_go_to_nu(qps_bitmap_enumerator_t *en, uint32_t row,
                                    bool safe)
{
    qps_bitmap_key_t key;

    assert(en->is_nullable);

    key.key = row;

    if (en->end) {
        return;
    }

    /* Should have used the "safe" option. */
    assert(safe || qps_bitmap_enumerator_is_sync(en));
    if (safe && !qps_bitmap_enumerator_is_sync(en)) {
        en->bitmap_gen = en->map->bitmap_gen;
        qps_bitmap_enumerator_find_dispatch_nu(en, key);
        return;
    }

    if (en->key.key == row) {
        return;
    }

    if (en->key.root < key.root) {
        qps_bitmap_enumerator_find_dispatch_nu(en, key);
    } else
    if (en->key.dispatch < key.dispatch) {
        qps_bitmap_enumerator_find_leaf_nu(en, key);
    } else
    if (en->key.word_null < key.word_null) {
        qps_bitmap_enumerator_find_word_nu(en, key);
    } else {
        qps_bitmap_enumerator_find_bit_nu(en, key);
    }
}

/* }}} */
/* {{{ Private functions - non-nullable specialization. */

static inline
void qps_bitmap_enumerator_find_bit_nn(qps_bitmap_enumerator_t *en,
                                       qps_bitmap_key_t key);
static inline
void qps_bitmap_enumerator_find_leaf_nn(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key);
static inline
void qps_bitmap_enumerator_find_word_nn(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key);

static inline
void qps_bitmap_enumerator_find_dispatch_nn(qps_bitmap_enumerator_t *en,
                                            qps_bitmap_key_t key);

static inline
void qps_bitmap_enumerator_dispatch_up_nn(qps_bitmap_enumerator_t *en,
                                          qps_bitmap_key_t key,
                                          qps_bitmap_key_t new_key)
{
    if (key.root != new_key.root) {
        if (new_key.root == 0) {
            en->end = true;
        } else {
            qps_bitmap_enumerator_find_dispatch_nn(en, new_key);
        }
    } else
    if (key.dispatch != new_key.dispatch) {
        qps_bitmap_enumerator_find_leaf_nn(en, new_key);
    } else {
        qps_bitmap_enumerator_find_word_nn(en, new_key);
    }
}

static inline
void qps_bitmap_enumerator_find_dispatch_nn(qps_bitmap_enumerator_t *en,
                                            qps_bitmap_key_t key)
{
    en->dispatch = NULL;
    for (unsigned i = key.root; i < QPS_BITMAP_ROOTS; i++) {
        if (en->map->root->roots[i] != 0) {
            en->key.key  = 0;
            en->key.root = i;
            en->dispatch
                = (const qps_bitmap_dispatch_t *)qps_pg_deref(en->map->qps,
                                                              en->map->root->roots[i]);
            if (key.root != i) {
                key = en->key;
            }
            qps_bitmap_enumerator_find_leaf_nn(en, key);
            return;
        }
    }
    en->end = true;
}

static inline
void qps_bitmap_enumerator_find_leaf_nn(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key)
{
    en->leaf = NULL;
    assert (en->dispatch != NULL);
    for (unsigned i = key.dispatch; i < QPS_BITMAP_DISPATCH; i++) {
        if ((*en->dispatch)[i].node != 0) {
            en->key.word     = 0;
            en->key.bit      = 0;
            en->key.dispatch = i;
            en->leaf
                = (const uint64_t *)qps_pg_deref(en->map->qps,
                                                 (*en->dispatch)[i].node);
            if (key.dispatch != i) {
                key = en->key;
            }
            qps_bitmap_enumerator_find_word_nn(en, key);
            return;
        }
    }

    key = en->key;
    key.root++;
    key.dispatch = 0;
    key.word     = 0;
    key.bit      = 0;
    qps_bitmap_enumerator_dispatch_up_nn(en, en->key, key);
}

static inline
void qps_bitmap_enumerator_find_word_nn(qps_bitmap_enumerator_t *en,
                                        qps_bitmap_key_t key)
{
    assert (!en->is_nullable);
    assert (en->leaf != NULL);
    for (unsigned i = key.word; i < QPS_BITMAP_WORD; i++) {
        if (en->leaf[i] != 0) {
            en->key.bit  = 0;
            en->key.word = i;
            en->current_word = en->leaf[i];

            if (key.word != i) {
                key = en->key;
            }
            qps_bitmap_enumerator_find_bit_nn(en, key);
            return;
        }
    }

    key = en->key;
    key.word = 0;
    key.bit  = 0;
    key.key += 1 << 15; /* bitsizeof(word) + bitsizeof(bit) */
    qps_bitmap_enumerator_dispatch_up_nn(en, en->key, key);
}

static inline
void qps_bitmap_enumerator_find_bit_nn(qps_bitmap_enumerator_t *en,
                                       qps_bitmap_key_t key)
{
    while (en->current_word != 0) {
        unsigned bit = bsf64(en->current_word);

        en->current_word >>= bit;
        en->key.bit       += bit;

        if (en->key.bit >= key.bit) {
            return;
        }

        /* Skip bit */
        en->current_word  &= ~UINT64_C(1);
    }

    key = en->key;
    key.bit  = 0;
    key.key += 1 << 6;
    qps_bitmap_enumerator_dispatch_up_nn(en, en->key, key);
}

/* }}} */
/* {{{ Public functions - non-nullable specialization */

static inline
void qps_bitmap_enumerator_next_nn(qps_bitmap_enumerator_t *en, bool safe)
{
    qps_bitmap_key_t key = en->key;

    /* Should have used the "safe" option. */
    assert(safe || qps_bitmap_enumerator_is_sync(en));
    if (safe && !qps_bitmap_enumerator_is_sync(en)) {
        en->bitmap_gen = en->map->bitmap_gen;
        qps_bitmap_enumerator_find_dispatch_nn(en, key);

        if (en->end || en->key.key != key.key) {
            /* The key was removed, the enumerator is already set on the next
             * element. */
            return;
        }
    }

    assert(!en->is_nullable);
    en->current_word &= ~UINT64_C(1);
    key.bit++;
    qps_bitmap_enumerator_find_bit_nn(en, key);
}

static inline
void qps_bitmap_enumerator_go_to_nn(qps_bitmap_enumerator_t *en,
                                    uint32_t row, bool safe)
{
    qps_bitmap_key_t key;

    if (en->end) {
        return;
    }

    key.key = row;

    /* Should have used the "safe" option. */
    assert(safe || qps_bitmap_enumerator_is_sync(en));
    if (safe && !qps_bitmap_enumerator_is_sync(en)) {
        en->bitmap_gen = en->map->bitmap_gen;
        qps_bitmap_enumerator_find_dispatch_nn(en, key);
        return;
    }

    if (en->key.key == row) {
        return;
    }

    if (en->key.root < key.root) {
        qps_bitmap_enumerator_find_dispatch_nn(en, key);
    } else
    if (en->key.dispatch < key.dispatch) {
        qps_bitmap_enumerator_find_leaf_nn(en, key);
    } else {
        if (en->key.word < key.word) {
            qps_bitmap_enumerator_find_word_nn(en, key);
        } else {
            qps_bitmap_enumerator_find_bit_nn(en, key);
        }
    }
}

/* }}} */
/* {{{ Generic implementation */

static inline
qps_bitmap_enumerator_t qps_bitmap_get_enumerator_at(qps_bitmap_t *map,
                                                     uint32_t row)
{
    qps_bitmap_enumerator_t en;
    qps_bitmap_key_t key;

    p_clear(&en, 1);
    en.map = map;
    en.bitmap_gen = map->bitmap_gen;
    qps_hptr_deref(map->qps, &map->root_cache);

    en.is_nullable = en.map->root->is_nullable;
    if (!en.is_nullable) {
        en.value = 1;
    }

    key.key = row;
    if (en.is_nullable) {
        qps_bitmap_enumerator_find_dispatch_nu(&en, key);
    } else {
        qps_bitmap_enumerator_find_dispatch_nn(&en, key);
    }
    return en;
}

static inline
qps_bitmap_enumerator_t qps_bitmap_get_enumerator(qps_bitmap_t *map)
{
    return qps_bitmap_get_enumerator_at(map, 0);
}

static inline
void qps_bitmap_enumerator_next(qps_bitmap_enumerator_t *en, bool safe)
{
    if (en->is_nullable) {
        qps_bitmap_enumerator_next_nu(en, safe);
    } else {
        qps_bitmap_enumerator_next_nn(en, safe);
    }
}

static inline
void qps_bitmap_enumerator_go_to(qps_bitmap_enumerator_t *en, uint32_t row,
                                 bool safe)
{
    if (en->is_nullable) {
        qps_bitmap_enumerator_go_to_nu(en, row, safe);
    } else {
        qps_bitmap_enumerator_go_to_nn(en, row, safe);
    }
}
/* }}} */
/* {{{ For-each macros */

#define qps_bitmap_for_each_unsafe(en, map)                                  \
    for (qps_bitmap_enumerator_t en = qps_bitmap_get_enumerator(map);        \
         !en.end; qps_bitmap_enumerator_next(&en, false))

#define qps_bitmap_for_each_safe(en, map)                                    \
    for (qps_bitmap_enumerator_t en = qps_bitmap_get_enumerator(map);        \
         !en.end; qps_bitmap_enumerator_next(&en, true))

/* }}} */
/* Debugging tools {{{ */

void qps_bitmap_get_qps_roots(qps_bitmap_t *map, qps_roots_t *roots) __leaf;
void qps_bitmap_debug_print(qps_bitmap_t *map) __leaf;

/* }}} */

/** \} */
#endif
