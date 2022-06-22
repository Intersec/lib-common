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

#ifndef IS_LIB_COMMON_QPS_HAT_H
#define IS_LIB_COMMON_QPS_HAT_H

#include <lib-common/sort.h>
#include <lib-common/qps.h>
#include <lib-common/qps-bitmap.h>

/** \defgroup qkv__ll__hat QPS HAT-Trie
 * \ingroup qkv__ll
 * \brief QPS HAT-Trie
 *
 * \{
 *
 * QHAT-Trie is a a HAT-Trie implementation built on the top of \ref qps. It
 * provides an ordered mapping between fixed-length keys (32bits) and
 * fixed-length values (<= 128 bits).
 *
 * \section nullables Nullable vs Non-nullable tries
 *
 * Hat tries can be either nullable or non-nullable. On non-nullable tries,
 * the initial value for all the entries is 0 and can never be NULL,
 * while it is NULL on nullable trie. That means that on a non-nullable trie
 * you cannot both storing 0 and know the exact list of keys you explicitly
 * set.
 *
 * For both nullable and non-nullable tries, 0 is a special value and if you
 * want to store a 0 in the trie, you must use the function \ref qhat_set0 (or
 * its alias \ref qhat_set0_path).
 *
 * Here are a more in-depth description of the API depending on the type of
 * the trie:
 *
 * \subsection nonnullable Non-nullable tries
 *
 * * \ref qhat_get: return indifferently NULL or a pointer to 0 if the value
 *   associated with the key is 0.
 * * \ref qhat_set: allocate a slot where to store the value.
 * * \ref qhat_set0 and \ref qhat_remove are synonyms and deallocate the slot
 *   associated with the given key.
 *
 * \subsection nullable Nullable tries
 * * \ref qhat_get: returns NULL if the key is marked as set, else return a
 *    pointer to the value.
 * * \ref qhat_set: mark the key as set and allocate a slot where to store its
 *   value.
 * * \ref qhat_set0: mark the key as set and deallocate the slot associated
 *   with the key.
 * * \ref qhat_remove: mark the key as unset and deallocate the slot
 *   associated with the key.
 *
 *
 * \section internal In depth: storing 0.
 *
 * This section explains why you must not use \ref qhat_set and \ref
 * qhat_set_path in order to store 0s in the trie. Note that this section use
 * a non-nullable trie for the sake of simplicity, but you should consider
 * this information to also apply to nullable tries.
 *
 * 0s are the default value of flat nodes. This means that whenever a compact
 * node is flatten, all the holes between the key defined in the compact node
 * are filled with 0s. Thus, if you allocated slots in the compact node to
 * store some 0s, you 0s become undistinguishable from those filling the holes
 * between the keys.
 *
 * In this situation, calling \ref qhat_get on any key that falls in the new
 * flat node will return a non-null pointer (on non-nullable tries, qhat_get
 * returns NULL when there's either no leaf associated with the key or if it
 * falls on a missing key of a compact node).
 *
 * Now, suppose you remove one key from that flat node. This will trigger the
 * optimizer that will check that there's enough non-zeros values in the flat
 * node to remain flat and thus it will be unflattened. The unflattener just
 * scan the flat node for keys with non-zero values. So what will happen is
 * that you'll have a new compact node that will not contain the key you
 * explicitly set to 0 using \ref qhat_set (or \ref qhat_set0) and didn't
 * explicitly removed. That means that using \ref qhat_get() on those key will
 * return NULL.
 *
 * So, what happened: you wanted to store 0s while this is the default value
 * and finally you get NULL, so you lose one information: "did I explicitly
 * set the value or not". That's why you have to use a *nullable* trie if you
 * want to have a distinction between stored 0 and not-set values.
 *
 * What also happened is that you allocated a useless slot in the compact node
 * to store the default value. This is a waste of memory since this didn't add
 * any bit of information. That's why you must use \ref qhat_set0 (or \ref
 * qhat_set0_path) when you want to store a 0 in the trie and by no mean use
 * \ref qhat_set (or \ref qhat_set_path).
 */

#define QHAT_SHIFT   10UL
#define QHAT_COUNT   (1UL << QHAT_SHIFT)
#define QHAT_MASK    (QHAT_COUNT - 1)
#define QHAT_SIZE    (4UL << QHAT_SHIFT)
#define QHAT_ROOTS   (1 << (bitsizeof(uint32_t) % QHAT_SHIFT))

#define QHAT_DEPTH_MAX    3

typedef union qhat_node_t {
    struct {
        unsigned page    :30;
        unsigned leaf    :1;
        unsigned compact :1;
    };
    uint32_t value;
} qhat_node_t;
#define QHAT_NULL_NODE  (qhat_node_t){ .value = 0 }

typedef struct qhat_root_t {
    /* Signature */
#define QPS_TRIE_SIG_12  "QPS_trie/v01.02"
#define QPS_TRIE_SIG     QPS_TRIE_SIG_12
    uint8_t     sig[16];

    /* Trie description */
    uint32_t    value_len;
    bool        is_nullable : 1;
    bool        do_stats : 1;
    qhat_node_t nodes[QHAT_ROOTS];

    /* Statistics */
    uint16_t    node_count;
    uint32_t    compact_count;
    uint32_t    flat_count;

    uint32_t    entry_count;
    uint32_t    key_stored_count;
    uint32_t    zero_stored_count;

    /* Null bitmap */
    qps_handle_t bitmap;
} qhat_root_t;

typedef struct qhat_t {
    qps_t       *qps;
    qps_bitmap_t bitmap;
    uint32_t     struct_gen;

    union {
        qhat_root_t *root;
        qps_hptr_t   root_cache;
    };
    const struct qhat_desc_t *desc;

    bool do_stats;
} qhat_t;

typedef struct qhat_path_t {
    qhat_t     *hat;
    uint32_t    key;

    int         depth;
    uint64_t    generation;

    qhat_node_t path[QHAT_DEPTH_MAX];
} qhat_path_t;

typedef const void *(*qhat_getter_f)(qhat_path_t *path);
typedef void *(*qhat_setter_f)(qhat_path_t *path);
typedef void  (*qhat_setter0_f)(qhat_path_t *path, void *ptr);
typedef bool  (*qhat_remover_f)(qhat_path_t *path, void *ptr);

typedef struct qhat_desc_t {
    uint8_t  value_len;
    uint8_t  value_len_log;
    uint8_t  root_node_count;

    uint8_t  leaf_index_bits;
    uint32_t leaf_index_mask;

    uint8_t  pages_per_flat;
    uint8_t  pages_per_compact;

    uint16_t leaves_per_compact;
    uint16_t leaves_per_flat;

    uint16_t split_compact_threshold;

    /* VTable */
    qhat_getter_f  getf;
    qhat_setter_f  setf;
    qhat_setter0_f set0f;
    qhat_remover_f removef;

    void (*flattenf)(qhat_path_t *path);
    void (*unflattenf)(qhat_path_t *path);
} qhat_desc_t;
extern qhat_desc_t qhat_descs_g[10];

typedef struct qhat_128_t {
    uint64_t l;
    uint64_t h;
} qhat_128_t;
extern qhat_128_t  qhat_default_zero_g;

#define QHAT_PATH_NODE(Path)  (Path)->path[(Path)->depth]

typedef struct qhat_compacthdr_t {
    uint32_t  count;
    uint16_t  parent_left;
    uint16_t  parent_right;
    uint32_t  keys[];
} qhat_compacthdr_t;

qps_handle_t qhat_create(qps_t *qps, uint32_t value_len, bool is_nullable)
    __leaf;
void qhat_init(qhat_t *hat, qps_t *qps, qps_handle_t handle);
void qhat_destroy(qhat_t *hat) __leaf;
void qhat_clear(qhat_t *hat) __leaf;
void qhat_unload(qhat_t *hat) __leaf;

/** \name Accessors
 * \{
 */

static ALWAYS_INLINE
void qhat_path_init(qhat_path_t *path, qhat_t *hat, uint32_t row)
{
#ifdef __cplusplus
    p_clear(path, 1);
    path->key = row;
    path->hat = hat;
#else
    *path = (qhat_path_t){ .hat = hat, .key = row };
#endif
}

/** Remove the value described by a path.
 *
 * \param[in,out] path Already resolved path to the key in the trie.
 * \param[out] ptr If specified, filled with the erased value (or with 0 if
 *                 the key was not present in the trie)
 * \return true if the key was present before removal.
 */
static ALWAYS_INLINE
bool qhat_remove_path(qhat_path_t *path, void *ptr)
{
    return (*path->hat->desc->removef)(path, ptr);
}

/** Remove the slot associated to the given key.
 *
 * \see qhat_remove_path
 */
static ALWAYS_INLINE
bool qhat_remove(qhat_t *hat, uint32_t row, void *ptr)
{
    qhat_path_t path;

    qhat_path_init(&path, hat, row);
    return qhat_remove_path(&path, ptr);
}

/** Get a read-write pointer to the value described by a path.
 *
 * This function takes an already resolved path to a key in a hat, allocates a
 * slot if necessary and returns a pointer to this slot. It also updates the
 * specified path according to the changes in the structure of the trie
 * implied by the allocation of the slot.
 *
 * \warning The path returned by this function is invalidated as soon as the
 * structure of the trie changes. Thus don't cache this path after a set or a
 * remove.
 *
 * \warning You must not set the returned slot to 0, if you wish to store 0s
 * in the trie, use \ref qhat_set0 or \ref qhat_set0_path.
 *
 * \param[in,out] path Already resolved path to the key in the trie.
 * \return A pointer to the slot associated to the key in the trie.
 */
static ALWAYS_INLINE
void *qhat_set_path(qhat_path_t *path)
{
    return (*path->hat->desc->setf)(path);
}

/** Get a read-only pointer to the value associated with a key.
 *
 * \see qhat_set_path
 */
static ALWAYS_INLINE
void *qhat_set(qhat_t *hat, uint32_t row)
{
    qhat_path_t path;

    qhat_path_init(&path, hat, row);
    return qhat_set_path(&path);
}

/** Clear an entry.
 *
 * This function (or \ref qhat_set0) must be called whenever you want to
 * store a 0 in the hat since 0s are special values.
 *
 * \param[in,out] path Already resolved path to the key in the trie.
 * \param[out] ptr If not null, filled with previous value of the entry.
 */
static ALWAYS_INLINE
void qhat_set0_path(qhat_path_t *path, void *ptr)
{
    (*path->hat->desc->set0f)(path, ptr);
}


/** Clear an entry.
 *
 * \see qhat_set0_path
 */
static ALWAYS_INLINE
void qhat_set0(qhat_t *hat, uint32_t row, void *ptr)
{
    qhat_path_t path;

    qhat_path_init(&path, hat, row);
    return qhat_set0_path(&path, ptr);
}

/** Get a read-only pointer to the value described by a path.
 *
 * \param[in] path Already resolved path to a key in a trie.
 * \return A pointer to the slot storing the value associated to the path,
 *         NULL if no slot has been allocated.
 */
static ALWAYS_INLINE
const void *qhat_get_path(qhat_path_t *path)
{
    return (*path->hat->desc->getf)(path);
}

/** Get a read-only pointer to the value associated with a key.
 *
 * \see qhat_get_path
 */
static ALWAYS_INLINE
const void *qhat_get(qhat_t *hat, uint32_t row)
{
    qhat_path_t path;

    qhat_path_init(&path, hat, row);
    return qhat_get_path(&path);
}

/** Check if an entry is NULL.
 */
static ALWAYS_INLINE
bool qhat_is_null(qhat_t *hat, uint32_t key)
{
    /* Check if the at is nullable without having to deref hat->root for
     * non-nullable tries.
     */
    if (!hat->bitmap.root) {
        return false;
    }

    qps_hptr_deref(hat->qps, &hat->root_cache);
    return !qps_bitmap_get(&hat->bitmap, key);
}

/** Initiate computation of the number of nodes of the trie.
 *
 * Once this function has been called, the trie maintains a count of allocated
 * nodes. You can disable this computation by call this function again with
 * the enable flag set to false.
 */
void qhat_compute_counts(qhat_t *hat, bool enable) __leaf;

/** Get the memory consumption of the trie.
 *
 * \return memory consumption of the trie in bytes.
 */
uint64_t qhat_compute_memory(qhat_t *hat);

/** Get the amount of memory allocated but not used.
 */
uint64_t qhat_compute_memory_overhead(qhat_t *hat);

/** Perform a check on the structure of the HAT-Trie.
 */
bool qhat_check_consistency(qhat_t *hat) __leaf;

/** Remove stored zeros from the trie.
 */
void qhat_fix_stored0(qhat_t *hat) __leaf;

/** \} */
/* Enumeration API
 */
typedef uint8_t qhat_8_t;
typedef struct qhat_compact8_t {
    uint32_t  count;
    uint16_t  parent_left;
    uint16_t  parent_right;
    uint32_t  keys[8 * (QHAT_COUNT / 5) + 4];
    uint8_t   values[8 * (QHAT_COUNT / 5) + 4];
    uint8_t   padding[4];
} qhat_compact8_t;

typedef uint16_t qhat_16_t;
typedef struct qhat_compact16_t {
    uint32_t  count;
    uint16_t  parent_left;
    uint16_t  parent_right;
    uint32_t  keys[4 * (QHAT_COUNT / 3) - 1];
    uint16_t  values[4 * (QHAT_COUNT / 3) - 1];
    uint8_t   padding[4];
} qhat_compact16_t;

typedef uint32_t qhat_32_t;
typedef struct qhat_compact32_t {
    uint32_t  count;
    uint16_t  parent_left;
    uint16_t  parent_right;
    uint32_t  keys[QHAT_COUNT - 1];
    uint32_t  values[QHAT_COUNT - 1];
} qhat_compact32_t;

typedef uint64_t qhat_64_t;
typedef struct qhat_compact64_t {
    uint32_t  count;
    uint16_t  parent_left;
    uint16_t  parent_right;
    uint32_t  keys[QHAT_COUNT - 1];
    uint8_t   padding[4];
    uint64_t  values[QHAT_COUNT - 1];
} qhat_compact64_t;

typedef struct qhat_compact128_t {
    uint32_t    count;
    uint16_t    parent_left;
    uint16_t    parent_right;
    uint32_t    keys[QHAT_COUNT - 1];
    uint8_t     padding[4];
    qhat_128_t  values[QHAT_COUNT - 1];
    uint8_t     padding2[8];
} qhat_compact128_t;

typedef union qhat_node_const_memory_t {
    const void        *raw;
    const uint8_t     *u8;
    const uint16_t    *u16;
    const uint32_t    *u32;
    const uint64_t    *u64;
    const qhat_128_t  *u128;
    const qhat_node_t *nodes;
    const qhat_compacthdr_t *compact;
    const qhat_compact8_t   *compact8;
    const qhat_compact16_t  *compact16;
    const qhat_compact32_t  *compact32;
    const qhat_compact64_t  *compact64;
    const qhat_compact128_t *compact128;
} qhat_node_const_memory_t;

typedef union qhat_node_memory_t {
    void        *raw;
    uint8_t     *u8;
    uint16_t    *u16;
    uint32_t    *u32;
    uint64_t    *u64;
    qhat_128_t  *u128;
    qhat_node_t *nodes;
    qhat_compacthdr_t *compact;
    qhat_compact8_t   *compact8;
    qhat_compact16_t  *compact16;
    qhat_compact32_t  *compact32;
    qhat_compact64_t  *compact64;
    qhat_compact128_t *compact128;
    qhat_node_const_memory_t cst;
} qhat_node_memory_t;

/* {{{ Tree structure enumerator */

typedef struct qhat_tree_enumerator_t {
    /* The layout of this part must be the same as qhat_enumerator_t {{{ */
    /** Current key.
     */
    uint32_t    key;
    bool        end;
    bool        is_nullable;

    uint8_t     value_len;
    bool        compact;

    /* }}} */

    qhat_path_t path;

    /* Position of the element in the current leaf of the trie (attribute
     * "memory").
     *
     * If the current leaf is a "compact", it is both the index of the key
     * in the key array and of the value in the value array (attribute
     * "value_tab").
     *
     * If the current leaf is a "flat", it's only the index of the value as
     * there is no key array.
     */
    uint32_t pos;

    /* Pointer on the value of the current leaf. It can be either a "compact"
     * or a "flat". If the enumerator is up-to-date, then the value associated
     * to the current key should be in 'en->value_tab', at index 'en->pos'.
     *
     * This attribute is refreshed each time the enumerator enters a different
     * leaf of the trie, so it should always be up-to-date as long as the trie
     * isn't modified.
     */
    const void *value_tab;

    /* Only when the local array in memory is a "compact".
     * Number of elements in the compact.
     * May be unsynchronized with "memory.compact->count" if elements where
     * added or removed in the qhat. */
    uint32_t count;

    /* Current leaf of the enumerator, cached here for quicker access. */
    qhat_node_const_memory_t memory;
} qhat_tree_enumerator_t;

qhat_tree_enumerator_t qhat_get_tree_enumerator_at(qhat_t *hat,
                                                   uint32_t key) __leaf;
void qhat_tree_enumerator_refresh_path(qhat_tree_enumerator_t *en) __leaf;
void qhat_tree_enumerator_dispatch_up(qhat_tree_enumerator_t *en, uint32_t key,
                                      uint32_t new_key) __leaf;
void qhat_tree_enumerator_find_root(qhat_tree_enumerator_t *en,
                                    uint32_t key) __leaf;
void qhat_tree_enumerator_find_node(qhat_tree_enumerator_t *en,
                                    uint32_t key) __leaf;

const void *
qhat_tree_enumerator_get_value_unsafe(const qhat_tree_enumerator_t *en);
const void *
qhat_tree_enumerator_get_value(qhat_tree_enumerator_t *en, bool safe);

void qhat_tree_enumerator_find_entry(qhat_tree_enumerator_t *en);

void qhat_tree_enumerator_find_entry_from(qhat_tree_enumerator_t *en,
                                          uint32_t key);

/* Guaranteed to either enter a leaf or end the enumerator. */
static ALWAYS_INLINE
void qhat_tree_enumerator_find_up_down(qhat_tree_enumerator_t *en,
                                       uint32_t key)
{
    qhat_tree_enumerator_find_root(en, key);
}

void qhat_tree_enumerator_find_down_up(qhat_tree_enumerator_t *en,
                                       uint32_t key);

uint32_t qhat_tree_enumerator_next(qhat_tree_enumerator_t *en, bool safe);

void qhat_tree_enumerator_go_to(qhat_tree_enumerator_t *en, uint32_t key,
                                bool safe);

static ALWAYS_INLINE
qhat_tree_enumerator_t qhat_get_tree_enumerator(qhat_t *hat)
{
    return qhat_get_tree_enumerator_at(hat, 0);
}

/* }}} */
/* {{{ Hat enumeration (tree + bitmap) */

/** Qhat enumerator.
 *
 * Allows to scan keys and values of a QPS hat (either nullable or not).
 * The qhat can only be scanned forward: in increasing key order.
 *
 * The enumerator is meant to be created with one of the following functions:
 * - #qhat_get_enumerator
 * - #qhat_get_enumerator_at
 *
 * Public functions:
 * - #qhat_enumerator_next
 * - #qhat_enumerator_go_to
 * - #qhat_enumerator_get_path
 * - #qhat_enumerator_get_value_safe
 * - #qhat_get_enumeration_value
 *
 * Its public attributes are the following:
 * - #end true when the enumerator is done.
 * - #key current key for the enumerator (if not done).
 * - #value pointer to the value associated to the current key (if the value
 *   update was asked while using #qhat_enumerator_go_to or
 *   #qhat_enumerator_next).
 *
 * The other attributes are private and should not be used.
 */
typedef union qhat_enumerator_t {
    struct {
        uint32_t    key;
        bool        end;
        bool        is_nullable;
        /* 2 bytes padding */

        qhat_tree_enumerator_t  trie;
        qps_bitmap_enumerator_t bitmap;
    };
    qhat_tree_enumerator_t t;
} qhat_enumerator_t;

/** Move the enumerator to the next key of the qhat.
 *
 * \param[in] safe If set, the enumerator will support when the QHAT is
 *                 modified (removal, insertions(?)) between uses
 *                 (next, go_to).
 */
void qhat_enumerator_next(qhat_enumerator_t *en, bool safe);

qhat_enumerator_t qhat_get_enumerator_at(qhat_t *trie, uint32_t key);

static ALWAYS_INLINE
qhat_enumerator_t qhat_get_enumerator(qhat_t *trie)
{
    return qhat_get_enumerator_at(trie, 0);
}

/** Move the enumerator to a given key if it is present in the qhat or to the
 * first following one otherwise.
 *
 * \warning The enumerator can only go forward. IOW the caller can only go to
 * a key greater than (or equal) to the last key he went to.
 *
 * \param[in] key Target key. If the key doesn't exist in the qhat, then the
 *                enumerator will be set to the first key of the enumerator
 *                that immediately follows the target key. If there is no
 *                greater key, then the enumerator will be ended (end ==
 *                true).
 *
 * \param[in] safe If set, the enumerator will support when the QHAT is
 *                 modified (removal, insertions(?)) between uses
 *                 (next, go_to).
 */
void qhat_enumerator_go_to(qhat_enumerator_t *en, uint32_t key, bool safe);

const void *qhat_enumerator_get_value(qhat_enumerator_t *en);
const void *qhat_enumerator_get_value_safe(qhat_enumerator_t *en);

/** Get the 'qhat_path_t' associated to the current key. */
qhat_path_t qhat_enumerator_get_path(const qhat_enumerator_t *en);

static ALWAYS_INLINE
qhat_t *qhat_enumerator_get_hat(qhat_enumerator_t *en)
{
    if (en->is_nullable) {
        return en->trie.path.hat;
    } else {
        return en->t.path.hat;
    }
}

#define qhat_for_each_safe(en, hat)                                          \
    for (qhat_enumerator_t en = qhat_get_enumerator(hat);                    \
         !en.end;                                                            \
         qhat_enumerator_next(&en, true))

#define qhat_for_each_limit_safe(en, hat, from, to)                          \
    for (qhat_enumerator_t en = qhat_get_enumerator_at(hat, from);           \
         !en.end && en.key < (to);                                           \
         qhat_enumerator_next(&en, true))

#define qhat_for_each_unsafe(en, hat)                                        \
    for (qhat_enumerator_t en = qhat_get_enumerator(hat);                    \
         !en.end;                                                            \
         qhat_enumerator_next(&en, false))

#define qhat_for_each_limit_unsafe(en, hat, from, to)                        \
    for (qhat_enumerator_t en = qhat_get_enumerator_at(hat, from);           \
         !en.end && en.key < (to);                                           \
         qhat_enumerator_next(&en, false))

#define qhat_for_each qhat_for_each_safe

/* }}} */
/* Debugging tools
 */

#define QHAT_PRINT_VALUES  1U
#define QHAT_PRINT_KEYS    2U
__cold
void qhat_debug_print(qhat_t *hat, uint32_t flags);
__cold
void qhat_debug_print_stream(qhat_t *hat, uint32_t flags, FILE *stream);

void qhat_get_qps_roots(qhat_t *hat, qps_roots_t *roots) __leaf;

/** \} */
#endif
