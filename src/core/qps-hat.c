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

#include <lib-common/arith.h>
#include <lib-common/qps-hat.h>

/** \addtogroup qkv__ll__hat
 * \{
 */

#ifndef __doxygen_mode__

#define PATH_NODE(P)  QHAT_PATH_NODE(P)

#define IS_ZERO8(Val)    ((Val) == 0)
#define IS_ZERO16(Val)   ((Val) == 0)
#define IS_ZERO32(Val)   ((Val) == 0)
#define IS_ZERO64(Val)   ((Val) == 0)
#define IS_ZERO128(Val)  ((Val).l == 0 && (Val).h == 0)

#define SET_ZERO8(Val)    ((Val) = 0)
#define SET_ZERO16(Val)   ((Val) = 0)
#define SET_ZERO32(Val)   ((Val) = 0)
#define SET_ZERO64(Val)   ((Val) = 0)
#define SET_ZERO128(Val)  p_clear(&(Val), 1)

#define IS_ZERO(Size, Val)   (IS_ZERO##Size(Val))
#define SET_ZERO(Size, Val)  (SET_ZERO##Size(Val))

//#define QHAT_CHECK_CONSISTENCY  1

#ifdef QHAT_CHECK_CONSISTENCY
#   define CHECK_CONSISTENCY(Hat, Data)  qhat_check_consistency_(Hat, Data)
#else
#   define CHECK_CONSISTENCY(Hat, Data)
#endif

#define PATH_IN_PARENT_IDX(Path)  \
    qhat_get_key_bits((Path)->hat, (Path)->key, (Path)->depth)

#define PATH_GENERATION_CHANGED(Path)                                        \
    do {                                                                     \
        qhat_path_t *___path = (Path);                                       \
        qhat_t *___hat = ___path->hat;                                       \
        ___hat->struct_gen++;                                                \
        ___path->generation = ___path->hat->struct_gen;                      \
    } while (0)

#define PATH_STRUCTURE_CHANGED(Name, Path)                                   \
    do {                                                                     \
        qhat_path_t *__path = (Path);                                        \
        qhat_t *__hat = __path->hat;                                         \
        if (e_name_is_traced(3, Name)) {                                     \
            uint32_t __flags = 0;                                            \
            if (e_name_is_traced(4, Name)) {                                 \
                __flags |= QHAT_PRINT_KEYS;                                  \
            }                                                                \
            if (e_name_is_traced(5, Name)) {                                 \
                __flags |= QHAT_PRINT_VALUES;                                \
            }                                                                \
            qhat_debug_print(__hat, __flags);                                \
        }                                                                    \
        PATH_GENERATION_CHANGED(__path);                                     \
        CHECK_CONSISTENCY(__path->hat, false);                               \
    } while (0)

#define MOVED_TO_NEW_FLAT(Path, Count)                                       \
    if ((Path)->hat->do_stats) {                                             \
        qhat_t *__hat    = (Path)->hat;                                      \
        uint32_t __moved = (Count);                                          \
                                                                             \
        __hat->root->key_stored_count  -= __moved;                           \
        __hat->root->zero_stored_count += __hat->desc->leaves_per_flat       \
                                        - __moved;                           \
    }

#define MOVED_TO_COMPACT(Path, Count)                                        \
    if ((Path)->hat->do_stats) {                                             \
        qhat_t *__hat    = (Path)->hat;                                      \
        uint32_t __moved = (Count);                                          \
                                                                             \
        __hat->root->key_stored_count  += __moved;                           \
        __hat->root->zero_stored_count -= __hat->desc->leaves_per_flat       \
                                        - __moved;                           \
    }

#define QHAT_VALUE_LEN_SWITCH(Hat, Memory, CASE)                   \
    switch ((Hat)->desc->value_len_log) {                          \
      case 0: {                                                    \
        CASE(8,  Memory.compact8,  Memory.u8);                     \
      } break;                                                     \
      case 1: {                                                    \
        CASE(16, Memory.compact16, Memory.u16);                    \
      } break;                                                     \
      case 2: {                                                    \
        CASE(32, Memory.compact32, Memory.u32);                    \
      } break;                                                     \
      case 3: {                                                    \
        CASE(64, Memory.compact64, Memory.u64);                    \
      } break;                                                     \
      case 4: {                                                    \
        CASE(128, Memory.compact128, Memory.u128);                 \
      } break;                                                     \
      default:                                                     \
        e_panic("this should not happen");                         \
    }

qhat_desc_t qhat_descs_g[10];
qhat_128_t  qhat_default_zero_g;

#endif

/** \name Internal: utils
 * \{
 */
/* Utils {{{ */

static uint32_t qhat_compact_lookup(const qhat_compacthdr_t *header,
                                    uint32_t from, uint32_t key)
{
    uint32_t count = header->count - from;

    if (count == 0 || key > header->keys[header->count - 1]) {
        return header->count;
    } else
    if (count < 32) {
        for (uint32_t i = from; i < header->count; i++) {
            if (header->keys[i] >= key) {
                return i;
            }
        }
        return header->count;
    }
    return from + bisect32(key, header->keys + from, count, NULL);
}

static uint32_t qhat_depth_shift(const qhat_t *hat, uint32_t depth)
{
    /* depth 0: shift (20 + leaf_bits)
     * depth 1: shift (10 + leaf_bits)
     * depth 2: shift leaf_bits
     * depth 3: shift 0
     */
    if (depth != QHAT_DEPTH_MAX) {
        return (2 - depth) * QHAT_SHIFT + hat->desc->leaf_index_bits;
    } else {
        return 0;
    }
}

static uint32_t qhat_depth_prefix(const qhat_t *hat, uint32_t key,
                                  uint32_t depth)
{
    uint32_t shift = qhat_depth_shift(hat, depth);
    if (shift == bitsizeof(uint32_t)) {
        return 0;
    }
    return key & ~((1U << shift) - 1);
}

static uint32_t qhat_lshift(const qhat_t *hat, uint32_t key, uint32_t depth)
{
    uint32_t shift = qhat_depth_shift(hat, depth);
    if (shift == bitsizeof(uint32_t)) {
        return 0;
    }
    return key << shift;
}

static uint32_t qhat_get_key_bits(const qhat_t *hat, uint32_t key,
                                  uint32_t depth)
{
    if (depth == QHAT_DEPTH_MAX) {
        return key & hat->desc->leaf_index_mask;
    } else {
        uint32_t shift = qhat_depth_shift(hat, depth);
        if (shift == bitsizeof(uint32_t)) {
            return 0;
        } else {
            return (key >> shift) & QHAT_MASK;
        }
    }
}

/* }}} */
/** \} */
/** \name Internal: structure consistency
 * \{
 */
/* Check consistency {{{ */

#define CRITICAL(Cond)  do {                                                 \
        bool __cond = (Cond);                                                \
                                                                             \
        if (unlikely(!__cond)) {                                             \
            qhat_debug_print(hat, 0);                                        \
            e_panic("corrupted tree: %s", #Cond);                            \
        }                                                                    \
    } while (0)

#define SUBOPTIMAL(Cond)  if (!subopt) {                                     \
        bool __cond = (Cond);                                                \
                                                                             \
        if (unlikely(!__cond)) {                                             \
            e_warning("tree is suboptimal: %s", #Cond);                      \
            subopt = true;                                                   \
        }                                                                    \
    }


static __must_check__
bool qhat_flat_check_consistency(qhat_t *hat, uint32_t from, uint32_t to,
                                 qhat_node_const_memory_t memory,
                                 bool check_content)
{
    bool subopt = false;
    bool non_null = false;

    if (!check_content) {
        return !subopt;
    }
    for (uint32_t i = 0; i < hat->desc->leaves_per_flat; i++) {
#define CASE(Size, Compact, Flat)                                            \
        if (!IS_ZERO(Size, Flat[i])) {                                       \
            non_null = true;                                                 \
            break;                                                           \
        }
        QHAT_VALUE_LEN_SWITCH(hat, memory, CASE);
#undef CASE
    }
    SUBOPTIMAL(non_null);
    return !subopt;
}

static __must_check__
bool qhat_compact_check_consistency(qhat_t *hat, uint32_t from, uint32_t to,
                                    qhat_node_const_memory_t memory,
                                    bool check_content)
{
    bool subopt = false;
    uint32_t key;

    if (check_content) {
        SUBOPTIMAL(memory.compact->count > 0);
    } else
    if (memory.compact->count == 0) {
        return !subopt;
    }

    CRITICAL(memory.compact->count <= hat->desc->leaves_per_compact);
    key = memory.compact->keys[0];
    CRITICAL(key >= from);
    CRITICAL(key <= to);

    if (check_content) {
#define CASE(Size, Compact, Flat)  SUBOPTIMAL(!IS_ZERO(Size, Compact->values[0]));
        QHAT_VALUE_LEN_SWITCH(hat, memory, CASE);
#undef CASE
    }

    for (uint32_t i = 1; i < memory.compact->count; i++) {
        uint32_t k = memory.compact->keys[i];

        CRITICAL(k > key);
        CRITICAL(k >= from);
        CRITICAL(k <= to);
        key = k;

        if (check_content) {
#define CASE(Size, Compact, Flat)  SUBOPTIMAL(!IS_ZERO(Size, Compact->values[i]));
            QHAT_VALUE_LEN_SWITCH(hat, memory, CASE);
#undef CASE
        }
    }
    return !subopt;
}

static __must_check__
bool qhat_node_check_consistency(qhat_t *hat, uint32_t key, uint32_t depth,
                                 qhat_node_const_memory_t memory, int c,
                                 bool check_content);

static bool qhat_node_check_child(qhat_t *hat, uint32_t key, uint32_t from,
                                  uint32_t to, uint32_t depth, qhat_node_t node,
                                  bool check_content)
{
    bool subopt = false;
    qhat_node_const_memory_t memory;
    uint32_t key_from;
    uint32_t key_to;

    if (!node.value) {
        return !subopt;
    }

    memory.raw = qps_pg_deref(hat->qps, node.page);
    key_from = key | qhat_lshift(hat, from, depth);
    key_to   = key | qhat_lshift(hat, to - 1, depth);
    key_to  += qhat_lshift(hat, 1, depth) - 1;

    if (node.leaf && node.compact) {
#define CASE(Size, Compact, Flat)  \
        CRITICAL(qps_pg_sizeof(hat->qps, node.page) \
                 == sizeof(*(Compact)) / QHAT_SIZE);
        QHAT_VALUE_LEN_SWITCH(hat, memory, CASE);
#undef CASE
        CRITICAL(memory.compact->parent_left == from);
        CRITICAL(memory.compact->parent_right == to);

        SUBOPTIMAL(qhat_compact_check_consistency(hat, key_from, key_to,
                                                  memory, check_content));
    } else
    if (node.leaf) {
        CRITICAL(qps_pg_sizeof(hat->qps, node.page)
                 == hat->desc->value_len * hat->desc->leaves_per_flat / QHAT_SIZE);
        CRITICAL(to == from + 1);
        SUBOPTIMAL(qhat_flat_check_consistency(hat, key_from, key_to,
                                               memory, check_content));
    } else {
        CRITICAL(qps_pg_sizeof(hat->qps, node.page) == 1);
        CRITICAL(to == from + 1);
        SUBOPTIMAL(qhat_node_check_consistency(hat, key_from, depth + 1,
                                               memory, QHAT_COUNT,
                                               check_content));
    }
    return !subopt;
}


static __must_check__
bool qhat_node_check_consistency(qhat_t *hat, uint32_t key, uint32_t depth,
                                 qhat_node_const_memory_t memory, int c,
                                 bool check_content)
{
    bool subopt = false;
    bool non_null = false;
    qhat_node_t node = QHAT_NULL_NODE;
    int from = 0;

    for (int i = 0; i < c; i++) {
        qhat_node_t current = memory.nodes[i];

        if (current.value) {
            non_null = true;
        }
        if (current.value == node.value) {
            continue;
        }
        qhat_node_check_child(hat, key, from, i, depth, node, check_content);
        node = current;
        from = i;
    }

    SUBOPTIMAL(qhat_node_check_child(hat, key, from, c, depth, node,
                                     check_content));
    if (check_content && c == QHAT_COUNT) {
        SUBOPTIMAL(non_null);
    }
    return !subopt;
}

#undef CHECK
#undef CRITICAL
#undef SUBOPTIMAL

static
bool qhat_check_consistency_(qhat_t *hat, bool check_content)
{
    qhat_node_const_memory_t memory = { .nodes = hat->root->nodes };

    return qhat_node_check_consistency(hat, 0, 0, memory,
                                       hat->desc->root_node_count,
                                       check_content);
}

bool qhat_check_consistency(qhat_t *hat)
{
    qps_hptr_deref(hat->qps, &hat->root_cache);
    return qhat_check_consistency_(hat, true);
}

/* }}} */
/** \} */
/** \name Internal: node manipulation and deref
 * \{
 */
/* {{{ */

static void qhat_update_parent(qhat_path_t *path, qhat_node_t to);

static ALWAYS_INLINE
qhat_node_memory_t qhat_node_w_deref_(qps_t *qps, qhat_node_t node)
{
    return (qhat_node_memory_t){
        .raw = qps_pg_deref(qps, node.page),
    };
}

static ALWAYS_INLINE
qhat_node_const_memory_t qhat_node_deref_(qps_t *qps, qhat_node_t node)
{
    return qhat_node_w_deref_(qps, node).cst;
}

static ALWAYS_INLINE
qhat_node_memory_t qhat_node_w_deref(qhat_path_t *path)
{
    return qhat_node_w_deref_(path->hat->qps, PATH_NODE(path));
}

static ALWAYS_INLINE
qhat_node_const_memory_t qhat_node_deref(const qhat_path_t *path)
{
    return qhat_node_deref_(path->hat->qps, PATH_NODE(path));
}

static
qhat_node_memory_t qhat_parent_w_deref(qhat_path_t *path, uint32_t *u32s)
{
    if (path->depth == 0) {
        void *ptr = path->hat->root_cache.data;

        qps_hptr_w_deref(path->hat->qps, &path->hat->root_cache);
        *u32s = path->hat->desc->root_node_count;
        if (ptr != path->hat->root_cache.data) {
            PATH_GENERATION_CHANGED(path);
        }
        return (qhat_node_memory_t){ .nodes = path->hat->root->nodes };
    } else {
        qhat_node_memory_t memory;
        path->depth--;
        *u32s = QHAT_COUNT;
        memory = qhat_node_w_deref(path);
        path->depth++;
        return memory;
    }
}

static
bool qhat_node_is_pure(const qhat_path_t *path)
{
    if (PATH_NODE(path).leaf) {
        if (PATH_NODE(path).compact) {
            qhat_node_const_memory_t memory = qhat_node_deref(path);
            return memory.compact->parent_left + 1 == memory.compact->parent_right;
        }
    }
    return true;
}

static void qhat_update_parent_pure(qhat_path_t *path, qhat_node_t to)
{
    uint32_t idx = PATH_IN_PARENT_IDX(path);
    uint32_t max;
    qhat_node_memory_t memory = qhat_parent_w_deref(path, &max);
    memory.nodes[idx] = to;
}

static void qhat_update_parent_compact(qhat_path_t *path, qhat_node_t to)
{
    qhat_node_const_memory_t memory = qhat_node_deref(path);
    const qhat_compacthdr_t *compact = memory.compact;
    uint32_t max;
    qhat_node_memory_t parent = qhat_parent_w_deref(path, &max);

    for (uint32_t i = compact->parent_left; i < compact->parent_right; i++) {
        parent.nodes[i] = to;
    }
}

static void qhat_update_parent(qhat_path_t *path, qhat_node_t to)
{
    if (qhat_node_is_pure(path)) {
        qhat_update_parent_pure(path, to);
    } else {
        qhat_update_parent_compact(path, to);
    }
}

static
bool qhat_node_is_empty(const qhat_path_t *path)
{
    qhat_node_t node = PATH_NODE(path);
    qhat_node_const_memory_t memory = qhat_node_deref(path);

    if (!node.leaf)
        return is_memory_zero(memory.nodes, QHAT_SIZE);
    if (node.compact)
        return memory.compact->count == 0;
#define CASE(Size, Compact, Flat) \
    return is_memory_zero(Flat, path->hat->desc->leaves_per_flat * Size / 8)
    QHAT_VALUE_LEN_SWITCH(path->hat, memory, CASE);
#undef CASE
}

static
uint32_t qhat_node_count(const qhat_path_t *path)
{
    const qhat_node_t node = PATH_NODE(path);
    qhat_node_const_memory_t memory = qhat_node_deref(path);

    if (!node.leaf)
        return count_non_zero32(&memory.nodes[0].value, QHAT_COUNT);
    if (node.compact)
        return memory.compact->count;

#define CASE(Size, Compact, Flat) \
    return count_non_zero##Size(Flat, path->hat->desc->leaves_per_flat)
    QHAT_VALUE_LEN_SWITCH(path->hat, memory, CASE);
#undef CASE
}

static ALWAYS_INLINE
bool qhat_leaf_is_full(const qhat_path_t *path)
{
    assert (likely(PATH_NODE(path).value != 0));
    assert (likely(PATH_NODE(path).leaf));

    if (PATH_NODE(path).compact) {
        qhat_node_const_memory_t memory = qhat_node_deref(path);
        return memory.compact->count == path->hat->desc->leaves_per_compact;
    } else {
        return false;
    }
}

static qhat_node_t qhat_alloc_leaf(qhat_t *hat, bool compact)
{
    uint32_t pages = compact ? hat->desc->pages_per_compact
                             : hat->desc->pages_per_flat;
    qps_pg_t page;
    page = qps_pg_map(hat->qps, pages);
    if (!compact && page) {
        qps_pg_zero(hat->qps, page, pages);
    }
    if (hat->do_stats) {
        if (compact) {
            hat->root->compact_count++;
        } else {
            hat->root->flat_count++;
        }
    }
    return (qhat_node_t){
        {
            .page    = page,
            .leaf    = true,
            .compact = compact,
        }
    };
}

static qhat_node_t qhat_alloc_node(qhat_t *hat)
{
    if (hat->do_stats) {
        hat->root->node_count++;
    }
    return (qhat_node_t){
        {
            .page    = qps_pg_map(hat->qps, 1),
            .leaf    = false,
            .compact = false,
        }
    };
}

static void qhat_unmap_node(qhat_t *hat, qhat_node_t node)
{
    if (hat->do_stats) {
        if (!node.leaf) {
            hat->root->node_count--;
        } else
        if (!node.compact) {
            hat->root->flat_count--;
        } else {
            hat->root->compact_count--;
        }
    }
    qps_pg_unmap(hat->qps, node.page);
}

/* }}} */
/** \} */
/** \name Internal: hat structure manipulation
 * \{
 */
/* {{{ */

static void qhat_create_leaf(qhat_path_t *path)
{
    qhat_node_memory_t memory;
    qhat_node_memory_t parent;
    qhat_node_t node;
    uint32_t idx;
    uint32_t max;
    assert (likely(PATH_NODE(path).value == 0));
    node = qhat_alloc_leaf(path->hat, true);
    PATH_NODE(path) = node;
    assert (likely(PATH_NODE(path).value != 0));

    memory = qhat_node_w_deref(path);
    parent = qhat_parent_w_deref(path, &max);

    idx = PATH_IN_PARENT_IDX(path);
    memory.compact->count = 0;
    memory.compact->parent_left  = 0;
    memory.compact->parent_right = max;
    for (uint32_t i = idx; i > 0; i--) {
        if (parent.nodes[i - 1].value == 0) {
            parent.nodes[i - 1] = node;
        } else {
            memory.compact->parent_left = i;
            break;
        }
    }
    for (uint32_t i = idx; i < max; i++) {
        if (parent.nodes[i].value == 0) {
            parent.nodes[i] = node;
        } else {
            memory.compact->parent_right = i;
            break;
        }
    }
}

static NEVER_INLINE
void qhat_split_leaf(qhat_path_t *path)
{
    qhat_node_t node;

    assert (path->depth < QHAT_DEPTH_MAX);
    assert (PATH_NODE(path).value != 0);
    assert (PATH_NODE(path).leaf);

    if (qhat_node_is_pure(path)) {
        /* The leaf is referenced by a single pointer from the parent
         * thus, in order to split this leaf, we must introduce new
         * pointer. So, we add a intermediate node between the parent and
         * the leaf with all its pointer going to the leaf.
         *
         * current state:
         * parent[i] ---------> leaf
         *
         * action:
         * parent[i] ----> dispatch  0 ---------> leaf
         *                           1 -------/
         *                           ... ----/
         *                      QHAT_COUNT -'
         */
        qhat_node_t new_node = qhat_alloc_node(path->hat);
        qhat_node_memory_t memory;
        assert (path->depth < QHAT_DEPTH_MAX - 1);
        node = PATH_NODE(path);
        qhat_update_parent_pure(path, new_node);
        PATH_NODE(path) = new_node;

        memory = qhat_node_w_deref(path);
        for (uint32_t i = 0; i < QHAT_COUNT; i++) {
            memory.nodes[i] = node;
        }
        path->depth++;
        PATH_NODE(path) = node;
        if (node.leaf && node.compact) {
            memory = qhat_node_w_deref(path);
            memory.compact->parent_left = 0;
            memory.compact->parent_right = QHAT_COUNT;
        }

        e_named_trace(2, "trie/insert/split", "add intermediate node %u "
                      "above node %u (depth: %d)",
                      new_node.page, node.page, path->depth);
        PATH_STRUCTURE_CHANGED("trie/insert/split", path);
    }

    assert (path->depth < QHAT_DEPTH_MAX);
    assert (PATH_NODE(path).value != 0);
    assert (PATH_NODE(path).leaf);

    {
        qhat_node_memory_t memory = qhat_node_w_deref(path);
        qhat_compacthdr_t *compact = memory.compact;
        uint32_t count = compact->count;
        uint32_t split;
        uint32_t prefix = 0;
        uint32_t sep;
        split = memory.compact->keys[count / 2];
        split = qhat_get_key_bits(path->hat, split, path->depth);
        if (split == compact->parent_left) {
            split++;
        }

        if (path->depth > 0) {
            prefix  = qhat_depth_prefix(path->hat, path->key,
                                        path->depth - 1);
        }
        prefix |= qhat_lshift(path->hat, split, path->depth);
        e_named_trace(4, "trie/insert/split", "key %x, splitting at prefix %x"
                      " (depth %d, split %x)", path->key, prefix, path->depth,
                      split);
        sep = qhat_compact_lookup(memory.compact, 0, prefix);

        if (sep == 0 || sep == count) {
            uint32_t max = 0;
            qhat_node_memory_t parent_memory = qhat_parent_w_deref(path, &max);
            uint32_t prev_parent_start = compact->parent_left;
            uint32_t prev_parent_end   = compact->parent_right;

            split = memory.compact->keys[count - 1];
            split = qhat_get_key_bits(path->hat, split, path->depth);
            if (split + 1 != compact->parent_right) {
                for (uint32_t i = split + 1; i < compact->parent_right; i++) {
                    parent_memory.u32[i] = 0;
                }
                compact->parent_right = split + 1;
            }
            split = memory.compact->keys[0];
            split = qhat_get_key_bits(path->hat, split, path->depth);
            if (split != compact->parent_left) {
                for (uint32_t i = compact->parent_left; i < split; i++) {
                    parent_memory.u32[i] = 0;
                }
                compact->parent_left = split;
            }
            e_named_trace(3, "trie/insert/split",
                          "split at value %u generates a single block "
                          " changing parent pointers: [%u->%u] -> [%u->%u]",
                          split, prev_parent_start, prev_parent_end -1,
                          compact->parent_left, compact->parent_right - 1);
            assert (compact->parent_left != prev_parent_start
                || compact->parent_right != prev_parent_end);
            assert (compact->parent_left < compact->parent_right);
            assert (compact->parent_left >= prev_parent_start);
            assert (compact->parent_right <= prev_parent_end);

            if (path->depth == PATH_MAX - 1
            && count > path->hat->desc->split_compact_threshold
            && compact->parent_left == compact->parent_right + 1) {
                PATH_STRUCTURE_CHANGED("trie/insert/split", path);
                (*path->hat->desc->flattenf)(path);
            }
        } else {
            uint32_t max = 0;
            qhat_node_t new_node;
            qhat_node_memory_t new_memory;
            qhat_node_memory_t parent_memory = qhat_parent_w_deref(path, &max);

            e_named_trace(3, "trie/insert/split", "split [%u-%u] at %u "
                          "(%d elements, depth %d)",
                          compact->parent_left, compact->parent_right - 1,
                          split, sep, path->depth);

            if (count - sep > path->hat->desc->split_compact_threshold
            && path->depth == QHAT_DEPTH_MAX - 1
            && split + 1 == compact->parent_right) {
                /* Create a new flat leaf */
                new_node = qhat_alloc_leaf(path->hat, false);
                parent_memory.nodes[split] = new_node;
                PATH_NODE(path) = new_node;

                compact->parent_right--;
                new_memory = qhat_node_w_deref(path);

#define CASE(Size, Compact, Flat)                                            \
                Compact->count = sep;                                        \
                for (uint32_t i = sep; i < count; i++) {                     \
                    uint32_t key = Compact->keys[i];                         \
                    key &= path->hat->desc->leaf_index_mask;                 \
                    new_memory.u##Size[key] = Compact->values[i];            \
                }

                QHAT_VALUE_LEN_SWITCH(path->hat, memory, CASE);
#undef CASE
                MOVED_TO_NEW_FLAT(path, count - sep);
            } else
            if (sep > path->hat->desc->split_compact_threshold
            && path->depth == QHAT_DEPTH_MAX - 1
            && (int)split == compact->parent_left + 1) {
                /* Replace the current leaf by a new flat leaf */
                new_node = qhat_alloc_leaf(path->hat, false);
                parent_memory.nodes[compact->parent_left] = new_node;
                PATH_NODE(path) = new_node;

                compact->parent_left++;
                new_memory = qhat_node_w_deref(path);

#define CASE(Size, Compact, Flat)                                            \
                for (uint32_t i = 0; i < (uint32_t)sep ; i++) {              \
                    uint32_t key = Compact->keys[i];                         \
                    key &= path->hat->desc->leaf_index_mask;                 \
                    new_memory.u##Size[key] = Compact->values[i];            \
                }                                                            \
                Compact->count = count - sep;                                \
                p_move(Compact->keys, Compact->keys + sep, count - sep);     \
                p_move(Compact->values, Compact->values + sep, count - sep);

                QHAT_VALUE_LEN_SWITCH(path->hat, memory, CASE);
#undef CASE
                MOVED_TO_NEW_FLAT(path, sep);
            } else {
                /* Create a new compact leaf */
                new_node = qhat_alloc_leaf(path->hat, true);
                for (uint32_t i = split; i < compact->parent_right; i++) {
                    parent_memory.nodes[i] = new_node;
                }
                PATH_NODE(path) = new_node;
                new_memory = qhat_node_w_deref(path);

#define CASE(Size, Compact, Flat)                                            \
                qhat_compact##Size##_t *new_compact = new_memory.compact##Size;\
                Compact->count = sep;                                        \
                new_compact->count = count - sep;                            \
                new_compact->parent_left = split;                            \
                new_compact->parent_right = compact->parent_right;           \
                Compact->parent_right = split;                               \
                p_copy(new_compact->values, Compact->values + sep, count - sep);\
                p_copy(new_compact->keys, Compact->keys + sep, count - sep);

                QHAT_VALUE_LEN_SWITCH(path->hat, memory, CASE);
#undef CASE
            }
        }
    }
}


static void qhat_optimize_parent(qhat_path_t *path)
{
    uint32_t max;
    qhat_node_memory_t memory = qhat_parent_w_deref(path, &max);
    uint32_t idx = PATH_IN_PARENT_IDX(path);
    uint32_t count = 0;
    bool changed = false;
    qhat_node_memory_t child;

    /* Node has been removed, try to find a leaf before or after
     * this one.
     */
    if (PATH_NODE(path).value == 0) {
        uint32_t before_count = 0;
        uint32_t before_idx   = idx;
        uint32_t after_count  = 0;
        uint32_t after_idx    = idx;
        for (uint32_t i = idx; i > 0; i--) {
            if (memory.nodes[i - 1].leaf) {
                PATH_NODE(path) = memory.nodes[i - 1];
                before_count    = qhat_node_count(path);
                before_idx      = i - 1;
                break;
            } else if (memory.nodes[i].value) {
                break;
           }
        }
        for (uint32_t i = idx + 1; i < max; i++) {
            if (memory.nodes[i].leaf) {
                PATH_NODE(path) = memory.nodes[i];
                after_count     = qhat_node_count(path);
                after_idx       = i - 1;
                break;
            } else if (memory.nodes[i].value) {
                break;
            }
        }
        if (before_count == 0 && after_count == 0) {
            return;
        } else
        if (after_count == 0 || before_count <= after_count) {
            idx   = before_idx;
            count = before_count;
        } else {
            idx   = after_idx;
            count = after_count;
        }
        PATH_NODE(path) = memory.nodes[idx];
    } else
    if (!PATH_NODE(path).leaf) {
        return;
    } else {
        count = qhat_node_count(path);
    }

    if (!PATH_NODE(path).compact) {
        if (count < path->hat->desc->leaves_per_flat / 2
        &&  count < (uint32_t)(2 * path->hat->desc->leaves_per_compact) / 3) {
            (*path->hat->desc->unflattenf)(path);
        } else {
            return;
        }
    }
    child = qhat_node_w_deref(path);

    for (uint32_t i = idx; i > 0; i--) {
        if (memory.nodes[i - 1].value == 0) {
            memory.nodes[i - 1] = PATH_NODE(path);
            child.compact->parent_left = i - 1;
            changed = true;
        } else {
            break;
        }
    }
    for (uint32_t i = idx + 1; i < max; i++) {
        if (memory.nodes[i].value == 0) {
            memory.nodes[i] = PATH_NODE(path);
            child.compact->parent_right = i + 1;
            changed = true;
        } else {
            break;
        }
    }
    if (changed) {
        PATH_STRUCTURE_CHANGED("trie/optimize", path);
    }

    if (changed && memory.nodes[0].value == PATH_NODE(path).value
    && memory.nodes[max - 1].value == PATH_NODE(path).value
    && path->depth > 0) {
        qhat_node_t leaf = PATH_NODE(path);
        path->depth--;
        qhat_update_parent_pure(path, leaf);
        e_named_trace(2, "trie/optimize", "removing dispatch node %u",
                      PATH_NODE(path).page);
        qhat_unmap_node(path->hat, PATH_NODE(path));
        child.compact->parent_left = PATH_IN_PARENT_IDX(path);
        child.compact->parent_right =  child.compact->parent_left + 1;
        PATH_NODE(path) = leaf;
        PATH_STRUCTURE_CHANGED("trie/optimize", path);
        qhat_optimize_parent(path);
    }
}

static void qhat_merge_nodes(qhat_path_t *path, qhat_node_t second)
{
    qhat_path_t second_path = *path;
    qhat_node_memory_t first_memory;
    qhat_node_const_memory_t second_memory;

    assert (PATH_NODE(path).leaf);
    assert (PATH_NODE(path).compact);
    assert (second.leaf);
    assert (second.compact);

    PATH_NODE(&second_path) = second;
    first_memory  = qhat_node_w_deref(path);
    second_memory = qhat_node_deref(&second_path);

#define CASE(Size, Compact, Flat)                                  \
    qhat_compact##Size##_t *first_compact = first_memory.compact##Size;      \
    p_copy(first_compact->keys + first_compact->count,                       \
           Compact->keys, Compact->count);                                   \
    p_copy(first_compact->values + first_compact->count,                     \
           Compact->values, Compact->count);                                 \
    first_compact->count += Compact->count;

    QHAT_VALUE_LEN_SWITCH(path->hat, second_memory, CASE);
#undef CASE

    e_named_trace(2, "trie/optimize/merge", "merged leaf %u in %u",
                  second.page, PATH_NODE(path).page);
    qhat_unmap_node(path->hat, second);
}

static NEVER_INLINE
void qhat_optimize(qhat_path_t *path)
{
    /* Kill useless branches */
    while ((!PATH_NODE(path).leaf || PATH_NODE(path).compact)
    &&     qhat_node_is_empty(path))
    {
        qhat_node_t node = PATH_NODE(path);
        qhat_update_parent(path, QHAT_NULL_NODE);
        e_named_trace(2, "trie/optimize", "removing empty bucket %u",
                      node.page);
        qhat_unmap_node(path->hat, node);
        PATH_STRUCTURE_CHANGED("trie/optimize", path);
        PATH_NODE(path) = QHAT_NULL_NODE;
        if (path->depth > 0) {
            path->depth--;
        } else {
            break;
        }
    }

    qhat_optimize_parent(path);
    if (!PATH_NODE(path).leaf || !PATH_NODE(path).compact
    || qhat_node_count(path) >= path->hat->desc->leaves_per_flat / 2) {
        return;
    }

    {
        const uint32_t limit = path->hat->desc->split_compact_threshold;
        uint32_t max;
        qhat_node_const_memory_t node_memory = qhat_node_deref(path);
        qhat_node_memory_t memory = qhat_parent_w_deref(path, &max);
        uint32_t from_idx, to_idx;
        uint32_t count = node_memory.compact->count;
        qhat_node_t node, previous_node;
        qhat_path_t new_path = *path;;

        from_idx = node_memory.compact->parent_left;
        to_idx   = node_memory.compact->parent_right;

        node = PATH_NODE(path);
        while (from_idx > 0) {
            uint32_t  current_count;
            qhat_node_t current_node = memory.nodes[from_idx - 1];

            assert (current_node.value != node.value);
            if (!current_node.leaf || !current_node.compact) {
                break;
            }
            PATH_NODE(&new_path) = current_node;
            node_memory          = qhat_node_deref(&new_path);
            current_count        = node_memory.compact->count;

            if (current_count + count > limit) {
                break;
            }
            from_idx = node_memory.compact->parent_left;
            count   += current_count;
            node     = current_node;
        }

        node = PATH_NODE(path);
        while (to_idx < max) {
            uint32_t  current_count;
            qhat_node_t current_node = memory.nodes[to_idx];

            assert (current_node.value != node.value);
            if (!current_node.leaf || !current_node.compact) {
                break;
            }
            PATH_NODE(&new_path) = current_node;
            node_memory          = qhat_node_deref(&new_path);
            current_count        = node_memory.compact->count;

            if (current_count + count > limit) {
                break;
            }
            to_idx = node_memory.compact->parent_right;
            count += current_count;
            node   = current_node;
        }

        if (memory.nodes[from_idx].value == memory.nodes[to_idx - 1].value) {
            return;
        }
        e_named_trace(3, "trie/optimize/merge", "merging siblings of %u "
                      "from parent id %u to parent id %u (depth %u, max %u)",
                      PATH_NODE(path).page, from_idx, to_idx, path->depth, max);

        PATH_NODE(path) = previous_node = memory.nodes[from_idx];
        for (uint32_t i = from_idx + 1; i < to_idx; i++) {
            qhat_node_t current_node = memory.nodes[i];
            if (current_node.value != previous_node.value
            && current_node.value != PATH_NODE(path).value) {
                qhat_merge_nodes(path, current_node);
            }
            previous_node = current_node;
            memory.nodes[i] = PATH_NODE(path);
        }
        assert (PATH_NODE(path).compact);
        memory = qhat_node_w_deref(path);
        memory.compact->parent_left  = from_idx;
        memory.compact->parent_right = to_idx;
        PATH_STRUCTURE_CHANGED("trie/optimize/merge", path);
    }
}

/* }}} */
/** \} */
/* Public API {{{ */

qps_handle_t qhat_create(qps_t *qps, uint32_t value_len, bool is_nullable)
{
    qps_hptr_t cache;
    qhat_root_t *hat = qps_hptr_alloc(qps, sizeof(qhat_root_t), &cache);

    if (value_len > 16) {
        e_panic("unsupported qhat value length: %u", value_len);
    }

    p_clear(hat, 1);
    memcpy(hat->sig, QPS_TRIE_SIG, countof(hat->sig));
    hat->value_len   = value_len;
    hat->is_nullable = is_nullable;

    if (is_nullable) {
        hat->bitmap = qps_bitmap_create(qps, false);
    }
    return cache.handle;
}

void qhat_init(qhat_t *hat, qps_t *qps, qps_handle_t handle)
{
    p_clear(hat, 1);
    hat->qps        = qps;
    hat->struct_gen = 1;
    qps_hptr_init(qps, handle, &hat->root_cache);
    hat->desc = &qhat_descs_g[bsr32(hat->root->value_len) << 1
                              | hat->root->is_nullable];

    /* Conversion from older version of the structure */
    if (memcmp(QPS_TRIE_SIG, hat->root->sig, sizeof(QPS_TRIE_SIG))) {
        logger_panic(&qps->logger, "cannot upgrade trie from `%*pM`",
                     (int)sizeof(QPS_TRIE_SIG) - 1, hat->root->sig);
    }
    hat->do_stats = hat->root->do_stats;

    if (hat->root->is_nullable) {
        qps_bitmap_init(&hat->bitmap, qps, hat->root->bitmap);
    }
}

static void qhat_delete_node(qhat_t *hat, qhat_node_t node);

static void qhat_wipe_dispatch_node(qhat_t *hat, qhat_node_const_memory_t memory,
                                    size_t max)
{
    qhat_node_t current = QHAT_NULL_NODE;
    for (size_t i = 0; i < max; i++) {
        if (memory.nodes[i].value != current.value) {
            current = memory.nodes[i];
            qhat_delete_node(hat, current);
        }
    }
}

static void qhat_delete_node(qhat_t *hat, qhat_node_t node)
{
    if (!node.value) {
        return;
    }
    if (!node.leaf) {
        qhat_node_const_memory_t memory = {
            .raw = qps_pg_deref(hat->qps, node.page),
        };
        e_named_trace(3, "trie/wipe", "wipe start childs of %u", node.value);
        qhat_wipe_dispatch_node(hat, memory, QHAT_COUNT);
        e_named_trace(3, "trie/wipe", "wipe done  childs of %u", node.value);
    }
    e_named_trace(2, "trie/wipe", "unmapping page %u", node.value);
    qhat_unmap_node(hat, node);
}

void qhat_clear(qhat_t *hat)
{
    qhat_node_const_memory_t root;
    bool do_stats = hat->do_stats;

    qps_hptr_w_deref(hat->qps, &hat->root_cache);

    /* Disable statistics, this will avoid useless updates of the counters
     * since we already know what their final values should be (0, 0, 0, 0...)
     */
    hat->do_stats = false;
    hat->root->do_stats = false;

    root.nodes = hat->root->nodes;
    e_named_trace(3, "trie/clear", "wipe start root");
    qhat_wipe_dispatch_node(hat, root, hat->desc->root_node_count);
    e_named_trace(3, "trie/clear", "wipe done  root");
    p_clear(hat->root->nodes, QHAT_ROOTS);
    hat->struct_gen++;

    if (hat->root->is_nullable) {
        qps_bitmap_clear(&hat->bitmap);
    }

    /* Reenable stats if they are computed, since we cleared the hat, it will
     * only reset the counters.
     */
    if (do_stats) {
        qhat_compute_counts(hat, true);
    }
}

void qhat_destroy(qhat_t *hat)
{
    if (hat != NULL) {
        qhat_node_const_memory_t root;

        qps_hptr_deref(hat->qps, &hat->root_cache);

        /* Disable stats during destruction of the trie. There's no need to
         * update hat->root->do_stats since its not read during and thus avoid
         * a useless w_deref.
         */
        hat->do_stats = false;
        root.nodes = hat->root->nodes;
        e_named_trace(3, "trie/wipe", "wipe start root");
        qhat_wipe_dispatch_node(hat, root, hat->desc->root_node_count);
        e_named_trace(3, "trie/wipe", "wipe done  root");

        if (hat->root->is_nullable) {
            qps_bitmap_destroy(&hat->bitmap);
        }
        qps_hptr_free(hat->qps, &hat->root_cache);
        e_named_trace(2, "trie/wipe", "trie wipe");
    }
}


static void qhat_unload_node(qhat_t *hat, qhat_node_t node);

static void qhat_unload_dispatch_node(qhat_t *hat,
                                      qhat_node_const_memory_t memory,
                                      size_t max)
{
    qhat_node_t current = QHAT_NULL_NODE;
    for (size_t i = 0; i < max; i++) {
        if (memory.nodes[i].value != current.value) {
            current = memory.nodes[i];
            qhat_unload_node(hat, current);
        }
    }
}

static void qhat_unload_node(qhat_t *hat, qhat_node_t node)
{
    if (!node.value) {
        return;
    }
    if (!node.leaf) {
        qhat_node_const_memory_t memory = {
            .raw = qps_pg_deref(hat->qps, node.page),
        };
        qhat_unload_dispatch_node(hat, memory, QHAT_COUNT);
    }
    qps_pg_unload(hat->qps, node.page);
}

void qhat_unload(qhat_t *hat)
{
    if (hat) {
        qhat_node_const_memory_t root;

        qps_hptr_deref(hat->qps, &hat->root_cache);
        root.nodes = hat->root->nodes;
        qhat_unload_dispatch_node(hat, root, hat->desc->root_node_count);
    }
}

#define SIZE                    8
#define PAGES_PER_FLAT          1
#include "qps-hat.in.c"

#define SIZE                    16
#define PAGES_PER_FLAT          1
#include "qps-hat.in.c"

#define SIZE                    32
#define PAGES_PER_FLAT          1
#include "qps-hat.in.c"

#define SIZE                    64
#define PAGES_PER_FLAT          2
#include "qps-hat.in.c"

#define SIZE                    128
#define PAGES_PER_FLAT          4
#include "qps-hat.in.c"

__attribute__((constructor))
static void qhat_initializes(void)
{
    qhat_props_from_len8(&qhat_descs_g[0], &qhat_descs_g[1]);
    qhat_props_from_len16(&qhat_descs_g[2], &qhat_descs_g[3]);
    qhat_props_from_len32(&qhat_descs_g[4], &qhat_descs_g[5]);
    qhat_props_from_len64(&qhat_descs_g[6], &qhat_descs_g[7]);
    qhat_props_from_len128(&qhat_descs_g[8], &qhat_descs_g[9]);
}

/* }}} */
/* Fix consistency {{{ */

void qhat_fix_stored0(qhat_t *hat)
{
    uint32_t c = 0;

    for (qhat_tree_enumerator_t en = qhat_tree_start_enumeration(hat);
         !en.end; qhat_tree_enumeration_next(&en, true, true))
    {
        if (en.compact) {
            const void *v = qhat_tree_enumeration_get_value_safe(&en);
#define CASE(Size, Compact, Flat)                                            \
            if (IS_ZERO(Size, *(const qhat_##Size##_t *)v)) {                \
                qhat_path_t path = en.path;                                  \
                path.key = en.key;                                           \
                qhat_remove_path##Size(&path, NULL);                         \
                c++;                                                         \
            }
            QHAT_VALUE_LEN_SWITCH(hat, NO_MEMORY, CASE);
#undef CASE
        }
    }

    if (c > 0) {
        e_info("found and removed %u stored 0", c);
    }
}

/* }}} */
/* Enumerator {{{ */

const void *qhat_tree_get_enumeration_value(qhat_tree_enumerator_t *en)
{
    if (en->compact) {
#define CASE(Size, Compact, Flat)  return &Compact->values[en->pos];
        QHAT_VALUE_LEN_SWITCH(en->path.hat, en->memory, CASE)
#undef CASE
    } else {
#define CASE(Size, Compact, Flat) return &Flat[en->pos];
        QHAT_VALUE_LEN_SWITCH(en->path.hat, en->memory, CASE)
#undef CASE
    }
}

#define QHAT_UPDATE_VALUE                                                    \
    if (en->pos != old_pos) {                                                \
        uint32_t count = en->pos - old_pos;                                  \
        en->value = ((const uint8_t *)en->value) + count * en->value_len;    \
    }

const void *qhat_tree_enumeration_get_value_safe(qhat_tree_enumerator_t *en)
{
    if (unlikely(en->path.generation != en->path.hat->struct_gen)) {
        qhat_tree_enumeration_refresh_path(en);
        return qhat_tree_get_enumeration_value(en);
    }
    if (unlikely(en->value == NULL)) {
        en->value = qhat_tree_get_enumeration_value(en);
    }
    if (en->compact) {
        if (unlikely(en->key > en->memory.compact->keys[en->pos])) {
            uint32_t old_pos = en->pos;

            /* Values has been added between the previous key and the
             * current one, shift pos accordingly.
             */
            while (en->key > en->memory.compact->keys[en->pos]) {
                en->pos++;
            }
            en->count += en->pos - old_pos;
            QHAT_UPDATE_VALUE;
        }
    }
    return en->value;
}

void qhat_tree_enumeration_find_entry(qhat_tree_enumerator_t *en)
{
    qhat_t  *hat     = en->path.hat;
    uint32_t new_key = en->path.key;
    uint32_t next    = 1;
    uint32_t shift;

    if (en->compact) {
        if (en->pos < en->count) {
            en->key = en->memory.compact->keys[en->pos];
            return;
        }
        next  = en->memory.compact->parent_right;
        next -= qhat_get_key_bits(hat, new_key, en->path.depth);
    } else
    if (en->pos < en->count) {
        en->key = en->path.key | en->pos;
        return;
    }

    shift = qhat_depth_shift(hat, en->path.depth);
    if (shift == 32) {
        en->end = true;
        return;
    }
    new_key += next << shift;
    qhat_tree_enumeration_dispatch_up(en, en->path.key, new_key);
}

void qhat_tree_enumeration_find_entry_from(qhat_tree_enumerator_t *en,
                                           uint32_t key)
{
    if (en->compact) {
        en->pos = qhat_compact_lookup(en->memory.compact, en->pos, key);
    } else {
        en->pos = key % en->count;
    }

    qhat_tree_enumeration_find_entry(en);
}

void qhat_tree_enumeration_find_down_up(qhat_tree_enumerator_t *en,
                                        uint32_t key)
{
    qhat_t  *hat        = en->path.hat;
    uint32_t last_key   = en->path.key;
    const uint32_t diff = key ^ last_key;
    uint32_t shift;

    assert (key >= en->path.key);
    if (key == en->path.key) {
        return;
    }

    shift = qhat_depth_shift(hat, en->path.depth);
    if (shift == 32) {
        if (en->memory.compact->keys[en->memory.compact->count - 1] < key) {
            en->end = true;
        } else {
            qhat_tree_enumeration_find_entry_from(en, key);
        }
        return;
    }
    if (en->compact) {
        uint32_t next  = en->memory.compact->parent_right;
        next     -= qhat_get_key_bits(hat, en->path.key, en->path.depth);
        last_key += next << shift;
    } else {
        last_key += 1 << shift;
    }

    if (key < last_key) {
        qhat_tree_enumeration_find_entry_from(en, key);
    } else
    if (qhat_get_key_bits(hat, diff, 0)) {
        qhat_tree_enumeration_find_root(en, key);
    } else
    if (en->path.depth >= 1 && qhat_get_key_bits(hat, diff, 1)) {
        en->path.depth = 0;
        qhat_tree_enumeration_find_node(en, key);
    } else
    if (en->path.depth >= 2 && qhat_get_key_bits(hat, diff, 2)) {
        en->path.depth = 1;
        qhat_tree_enumeration_find_node(en, key);
    } else {
        qhat_tree_enumeration_find_entry_from(en, key);
    }
}

uint32_t qhat_tree_enumeration_next(qhat_tree_enumerator_t *en,
                                    bool value, bool safe)
{
    uint32_t    old_pos = en->pos;
    qhat_node_t old_node;

    if (safe && en->pos < en->count) {
        uint32_t gen = en->path.generation;
        uint32_t key = en->key;

        /* Call the value getter to ensure we are back on sync with the latest
         * changes done on the structure.
         */
        IGNORE(qhat_tree_enumeration_get_value_safe(en));
        if (en->key != key || en->end) {
            return en->key;
        }

        if (unlikely(en->compact
        &&  (en->key != en->memory.compact->keys[en->pos]
            || en->count > en->memory.compact->count)))
        {
            /* Looks like en->key has been deleted, there's no need to
             * move pos since we already are on the next value.
             */
            if (gen == en->path.generation) {
                /* Decrease the count only if the path has *not* been
                 * refreshed (this a refresh also refreshed the count by
                 * itself).
                 */
                en->count--;
            }
            assert (en->count == en->memory.compact->count);
            if (en->pos < en->count) {
                QHAT_UPDATE_VALUE;
                return en->key = en->memory.compact->keys[en->pos];
            }
        } else
        if (en->compact) {
            assert (en->count == en->memory.compact->count);
        }
    }

    old_node = QHAT_PATH_NODE(&en->path);
    old_pos = en->pos;
    en->pos++;
    qhat_tree_enumeration_find_entry(en);
    if (value) {
        if (old_node.value != QHAT_PATH_NODE(&en->path).value) {
            old_pos = 0;
        }
        QHAT_UPDATE_VALUE;
    }
    return en->key;
}

void qhat_tree_enumeration_go_to(qhat_tree_enumerator_t *en, uint32_t key,
                                 bool value, bool safe)
{
    if (en->end || en->key >= key) {
        return;
    }
    if (unlikely(safe && en->path.generation != en->path.hat->struct_gen)) {
        qhat_tree_enumeration_find_up_down(en, key);

        if (value) {
            if (!en->end) {
                en->value = qhat_tree_get_enumeration_value(en);
            }
        }
    } else {
        uint32_t    old_pos  = en->pos;
        qhat_node_t old_node = QHAT_PATH_NODE(&en->path);

        if (unlikely(safe && en->compact)) {
            en->count = en->memory.compact->count;
            if (en->pos >= en->count) {
                en->pos = en->count - 1;
            }
            while (en->memory.compact->keys[en->pos] > en->key) {
                if (en->pos == 0) {
                    break;
                }
                en->pos--;
            }
            en->key = en->memory.compact->keys[en->pos];
        }

        if (key == en->key + 1) {
            qhat_tree_enumeration_next(en, false, false);
        } else {
            qhat_tree_enumeration_find_down_up(en, key);
        }
        if (value) {
            if (old_node.value != QHAT_PATH_NODE(&en->path).value) {
                old_pos = 0;
            }
            QHAT_UPDATE_VALUE;
        }
    }
}

#undef QHAT_UPDATE_VALUE

static void qhat_enumeration_catchup(qhat_enumerator_t *en, bool value,
                                     bool safe)
{
    if (en->bitmap.end) {
        en->end = true;
        return;
    }
    en->key = en->bitmap.key.key;
    if (value) {
        if (!en->trie.end && en->trie.key < en->key) {
            qhat_tree_enumeration_go_to(&en->trie, en->key, true, safe);
        }
        if (en->trie.end || en->trie.key != en->key) {
            en->value = &qhat_default_zero_g;
        } else {
            en->value = en->trie.value;
        }
    }
}

void qhat_enumeration_next(qhat_enumerator_t *en, bool value, bool safe)
{
    if (en->is_nullable) {
        assert (!en->bitmap.map->root->is_nullable);
        qps_bitmap_enumeration_next_nn(&en->bitmap);
        qhat_enumeration_catchup(en, value, safe);
    } else {
        qhat_tree_enumeration_next(&en->t, value, safe);
    }
}

qhat_enumerator_t qhat_start_enumeration_at(qhat_t *trie, uint32_t key)
{
    qhat_enumerator_t en;

    qps_hptr_deref(trie->qps, &trie->root_cache);
    if (trie->root->is_nullable) {
        p_clear(&en, 1);
        en.trie        = qhat_tree_start_enumeration_at(trie, key);
        en.bitmap      = qps_bitmap_start_enumeration_at_nn(&trie->bitmap, key);
        en.is_nullable = true;
        qhat_enumeration_catchup(&en, true, true);
    } else {
        en.t = qhat_tree_start_enumeration_at(trie, key);
        en.is_nullable = false;
    }
    return en;
}

void qhat_enumeration_go_to(qhat_enumerator_t *en, uint32_t key, bool value,
                            bool safe)
{
    if (en->is_nullable) {
        assert (!en->bitmap.map->root->is_nullable);
        qps_bitmap_enumeration_go_to_nn(&en->bitmap, key);
        qhat_enumeration_catchup(en, value, safe);
    } else {
        qhat_tree_enumeration_go_to(&en->t, key, value, safe);
    }
}

const void *qhat_enumeration_get_value_safe(qhat_enumerator_t *en)
{
    if (en->is_nullable) {
        if (!en->end && en->trie.key != en->key) {
            qhat_enumeration_catchup(en, true, true);
        } else {
            en->value = qhat_tree_enumeration_get_value_safe(&en->trie);
            if (en->is_nullable && en->value == NULL) {
                en->value = &qhat_default_zero_g;
            }
        }
        return en->value;
    } else {
        return qhat_tree_enumeration_get_value_safe(&en->t);
    }
}

const void *qhat_get_enumeration_value(qhat_enumerator_t *en)
{
    if (en->is_nullable) {
        if (!en->end && en->trie.key != en->key) {
            qhat_enumeration_catchup(en, true, false);
        } else {
            en->value = qhat_tree_get_enumeration_value(&en->trie);
            if (en->is_nullable && en->value == NULL) {
                en->value = &qhat_default_zero_g;
            }
        }
        return en->value;
    } else {
        return qhat_tree_get_enumeration_value(&en->t);
    }
}

qhat_path_t qhat_enumeration_get_path(const qhat_enumerator_t *en)
{
    qhat_path_t p;

    if (en->is_nullable) {
        if (!en->trie.end && en->key == en->trie.key) {
            p = en->trie.path;
        } else {
            qhat_path_init(&p, en->trie.path.hat, en->key);
        }
    } else {
        p = en->t.path;
    }
    p.key = en->key;
    return p;
}

static void qhat_tree_enumeration_enter_leaf(qhat_tree_enumerator_t *en,
                                             uint32_t key)
{
    en->memory = qhat_node_deref(&en->path);

    if (PATH_NODE(&en->path).compact) {
        en->compact = true;
        en->count   = en->memory.compact->count;
#define CASE(Size, Compact, Flat)  en->value = &Compact->values[0]
        QHAT_VALUE_LEN_SWITCH(en->path.hat, en->memory, CASE);
#undef CASE
    } else {
        en->compact = false;
        en->count   = en->path.hat->desc->leaves_per_flat;
#define CASE(Size, Compact, Flat)  en->value = &Flat[0]
        QHAT_VALUE_LEN_SWITCH(en->path.hat, en->memory, CASE);
#undef CASE
    }

    en->pos = 0;
    qhat_tree_enumeration_find_entry_from(en, key);
}

void qhat_tree_enumeration_find_root(qhat_tree_enumerator_t *en, uint32_t key)
{
    qhat_t *hat = en->path.hat;
    uint32_t root = qhat_get_key_bits(hat, key, 0);
    ssize_t  i;

    en->path.depth = 0;
    en->path.key   = 0;
    i = scan_non_zero32(&hat->root->nodes[0].value, root,
                        hat->desc->root_node_count);
    if (i >= 0) {
        PATH_NODE(&en->path) = hat->root->nodes[i];
        en->path.key         = qhat_lshift(hat, i, 0);

        if (root != i) {
            key = en->path.key;
        }

        if (PATH_NODE(&en->path).leaf) {
            qhat_tree_enumeration_enter_leaf(en, key);
        } else {
            qhat_tree_enumeration_find_node(en, key);
        }
    } else {
        en->end   = true;
        en->value = NULL;
    }
}

void qhat_tree_enumeration_dispatch_up(qhat_tree_enumerator_t *en, uint32_t key,
                                       uint32_t new_key)
{
    qhat_t  *hat = en->path.hat;
    uint32_t key_0 = qhat_get_key_bits(hat, key, 0);
    uint32_t key_1 = qhat_get_key_bits(hat, key, 1);

    uint32_t new_key_0 = qhat_get_key_bits(hat, new_key, 0);
    uint32_t new_key_1 = qhat_get_key_bits(hat, new_key, 1);

    if (new_key <= key) {
        en->end   = true;
        en->value = NULL;
    } else
    if (key_0 != new_key_0) {
        qhat_tree_enumeration_find_root(en, new_key);
    } else {
        if (key_1 != new_key_1) {
            en->path.depth = 0;
        } else {
            en->path.depth = 1;
        }
        qhat_tree_enumeration_find_node(en, new_key);
    }
}

void qhat_tree_enumeration_find_node(qhat_tree_enumerator_t *en, uint32_t key)
{
    qhat_t   *hat = en->path.hat;
    uint32_t  pos = qhat_get_key_bits(hat, key, en->path.depth + 1);
    uint32_t  new_key;
    uint32_t  shift;
    qhat_node_const_memory_t memory = qhat_node_deref(&en->path);
    ssize_t i;

    en->path.depth++;
    if ((i = scan_non_zero32(&memory.nodes[0].value, pos, QHAT_COUNT)) >= 0) {
        PATH_NODE(&en->path) = memory.nodes[i];
        en->path.key  = qhat_depth_prefix(hat, en->path.key, en->path.depth - 1);
        en->path.key |= qhat_lshift(hat, i, en->path.depth);

        if (pos != i) {
            key = en->path.key;
        }

        if (PATH_NODE(&en->path).leaf) {
            qhat_tree_enumeration_enter_leaf(en, key);
        } else {
            qhat_tree_enumeration_find_node(en, key);
        }
        return;
    }
    en->path.depth--;

    shift   = qhat_depth_shift(hat, en->path.depth);
    new_key = key + (1UL << shift);
    if (shift == 32) {
        en->end   = true;
        en->value = NULL;
    } else {
        qhat_tree_enumeration_dispatch_up(en, key, new_key);
    }
}

__flatten
qhat_tree_enumerator_t qhat_tree_start_enumeration_at(qhat_t *trie, uint32_t key)
{
    qhat_tree_enumerator_t en;
    qps_hptr_deref(trie->qps, &trie->root_cache);
    p_clear(&en, 1);
    en.path.hat        = trie;
    en.path.generation = trie->struct_gen;
    en.value_len   = en.path.hat->desc->value_len;
    en.is_nullable = en.path.hat->root->is_nullable;

    qhat_tree_enumeration_find_up_down(&en, key);
    if (!en.end) {
        en.value = qhat_tree_get_enumeration_value(&en);
    }
    return en;
}

void qhat_tree_enumeration_refresh_path(qhat_tree_enumerator_t *en)
{
    qhat_tree_enumeration_find_up_down(en, en->key);
}


/* }}} */
/** \name Debugging and introspection
 * \{
 */
/* {{{ */

#ifndef __doxygen_mode__

static void qhat_get_dispatch_nodes(qhat_t *hat, qhat_node_const_memory_t mem,
                                    size_t max, qps_roots_t *roots);

static void qhat_get_qps_nodes(qhat_t *hat, qhat_node_t node,
                               qps_roots_t *roots)
{
    if (!node.value) {
        return;
    }
    if (!node.leaf) {
        qhat_node_const_memory_t memory = {
            .raw = qps_pg_deref(hat->qps, node.page)
        };
        qhat_get_dispatch_nodes(hat, memory, QHAT_COUNT, roots);
    }
    qv_append(&roots->pages, node.page);
}


static void qhat_get_dispatch_nodes(qhat_t *hat, qhat_node_const_memory_t mem,
                                    size_t max, qps_roots_t *roots)
{
    qhat_node_t current = QHAT_NULL_NODE;
    for (size_t i = 0; i < max; i++) {
        if (mem.nodes[i].value != current.value) {
            current = mem.nodes[i];
            qhat_get_qps_nodes(hat, current, roots);
        }
    }
}

void qhat_get_qps_roots(qhat_t *hat, qps_roots_t *roots)
{
    qhat_node_const_memory_t root;

    qps_hptr_deref(hat->qps, &hat->root_cache);

    root.nodes = hat->root->nodes;
    qhat_get_dispatch_nodes(hat, root, hat->desc->root_node_count, roots);

    qv_append(&roots->handles, hat->root_cache.handle);

    if (hat->root->is_nullable) {
        qps_bitmap_get_qps_roots(&hat->bitmap, roots);
    }
}


static void qhat_compute_counts_(qhat_t *hat, qhat_root_t *root,
                                 qhat_node_const_memory_t mem, size_t max)
{
    qhat_node_t current = QHAT_NULL_NODE;

    for (size_t i = 0; i < max; i++) {
        if (mem.nodes[i].value == current.value) {
            continue;
        }

        current = mem.nodes[i];
        if (!current.value) {
            continue;
        }

        if (!current.leaf) {
            qhat_node_const_memory_t child = {
                .raw = qps_pg_deref(hat->qps, current.page)
            };

            qhat_compute_counts_(hat, root, child, QHAT_COUNT);
            root->node_count++;
        } else
        if (!current.compact) {
            qhat_node_const_memory_t child = {
                .raw = qps_pg_deref(hat->qps, current.page)
            };

            root->flat_count++;

#define CASE(Size, Compact, Flat)                                            \
            for (uint32_t j = 0; j < hat->desc->leaves_per_flat; j++) {      \
                if (!IS_ZERO(Size, Flat[j])) {                               \
                    root->entry_count++;                                     \
                } else {                                                     \
                    root->zero_stored_count++;                               \
                }                                                            \
            }
            QHAT_VALUE_LEN_SWITCH(hat, child, CASE);
#undef CASE
        } else {
            qhat_node_const_memory_t child = {
                .raw = qps_pg_deref(hat->qps, current.page)
            };

            root->compact_count++;
            root->entry_count      += child.compact->count;
            root->key_stored_count += child.compact->count;
        }
    }
}

void qhat_compute_counts(qhat_t *hat, bool enable)
{
    qhat_node_const_memory_t mem;
    qhat_root_t *root = qps_hptr_w_deref(hat->qps, &hat->root_cache);

    if ((bool)root->do_stats == enable) {
        return;
    }

    root->do_stats = enable;
    hat->do_stats  = enable;
    if (!enable) {
        return;
    }

    root->node_count        = 0;
    root->flat_count        = 0;
    root->compact_count     = 0;
    root->entry_count       = 0;
    root->key_stored_count  = 0;
    root->zero_stored_count = 0;

    mem.nodes = root->nodes;
    qhat_compute_counts_(hat, root, mem, hat->desc->root_node_count);
}

uint64_t qhat_compute_memory(qhat_t *hat)
{
    uint64_t memory = sizeof(qhat_root_t);
    const qhat_root_t *root;
    const qhat_desc_t *desc = hat->desc;

    root = (const qhat_root_t *)qps_hptr_deref(hat->qps, &hat->root_cache);
    if (!root->do_stats) {
        qhat_compute_counts(hat, true);
    }

    memory  = QPS_PAGE_SIZE * root->node_count;
    memory += desc->pages_per_compact * QPS_PAGE_SIZE * root->compact_count;
    memory += desc->pages_per_flat * QPS_PAGE_SIZE * root->flat_count;
    return memory;
}

uint64_t qhat_compute_memory_overhead(qhat_t *hat)
{
    uint64_t memory = 0;
    const qhat_root_t *root;
    const qhat_desc_t *desc = hat->desc;
    uint64_t compact_slots;

    root = (const qhat_root_t *)qps_hptr_deref(hat->qps, &hat->root_cache);
    if (!root->do_stats) {
        qhat_compute_counts(hat, true);
    }

    /* Overhead of flat nodes: storage of zeros */
    memory += desc->value_len * root->zero_stored_count;

    /* Overhead of compact nodes: storage of keys and empty entries */
    compact_slots = desc->leaves_per_compact * root->compact_count;
    memory += compact_slots * 4;
    memory += (compact_slots - root->key_stored_count) * desc->value_len;

    return memory;
}

static void qhat_debug_print_indent(int depth, bool final)
{
    static bool finals[QHAT_DEPTH_MAX + 1];

    finals[depth] = final;
    for (int i = 0; i < depth; i++) {
        if (finals[i]) {
            fprintf(stderr, "    ");
        } else {
            fprintf(stderr, "|   ");
        }
    }
}

static void qhat_debug_print_node(const qhat_t *hat, uint32_t flags,
                                  int depth, uint32_t prefix,
                                  qhat_node_t node, bool pure);

static void qhat_debug_print_dispatch_node(const qhat_t *hat, uint32_t flags,
                                           int depth, uint32_t prefix,
                                           const qhat_node_t *pointers,
                                           int end)
{
    qhat_node_t value    = pointers[0];
    int         previous = 0;
    bool        has_next;

    for (int i = 1; i < end; i++) {
        if (value.value != pointers[i].value) {
            if (value.value != 0) {
                has_next = (scan_non_zero32(&pointers[0].value, i, end) >= 0);
                qhat_debug_print_indent(depth, !has_next);
                if (i - previous > 1) {
                    fprintf(stderr, "+ [%x -> %x]\n", previous, i - 1);
                } else {
                    fprintf(stderr, "+ [%x]\n", previous);
                }
                qhat_debug_print_node(hat, flags, depth + 1,
                                      prefix | (previous << qhat_depth_shift(hat, depth)),
                                      value, (i - previous == 1));
            }
            previous = i;
            value    = pointers[i];
        }
    }
    if (value.value != 0) {
        int shift;
        qhat_debug_print_indent(depth, true);
        if (end -previous > 1) {
            fprintf(stderr, "+ [%x -> %x]\n", previous, end - 1);
        } else {
            fprintf(stderr, "+ [%x]\n", previous);
        }
        shift = qhat_depth_shift(hat, depth);
        if (shift == 32) {
            previous = 0;
        } else {
            previous <<= shift;
        }
        qhat_debug_print_node(hat, flags, depth + 1, prefix | previous,
                              value, (end - previous) == 1);
    }
}

static void qhat_debug_print_compact_leaf(const qhat_t *hat, uint32_t flags,
                                          int depth, uint32_t prefix,
                                          qhat_node_const_memory_t memory)
{
    uint32_t count    = memory.compact->count;
    uint32_t previous = memory.compact->keys[0];
    uint32_t start    = previous;
    int printed       = 0;

    qhat_debug_print_indent(depth, true);
    fprintf(stderr, "+ ");
    if (count == 0) {
        fprintf(stderr, "(empty)\n");
        return;
    }
    if ((flags & QHAT_PRINT_KEYS)) {
        for (uint32_t i = 1; i < count; i++) {
            uint32_t key = memory.compact->keys[i];
            if (key != previous + 1) {
                if (printed > 9) {
                    fprintf(stderr, "\n");
                    qhat_debug_print_indent(depth, true);
                    fprintf(stderr, "+ ");
                    printed = 0;
                }
                if (previous != start) {
                    fprintf(stderr, "%x-%x, ", start, previous);
                    printed += 2;
                } else {
                    fprintf(stderr, "%x, ", previous);
                    printed += 1;
                }
                start = key;
            }
            previous = key;
        }
        if (printed > 9) {
            fprintf(stderr, "\n");
            qhat_debug_print_indent(depth, true);
            fprintf(stderr, "+ ");
        }
        if (previous != start) {
            fprintf(stderr, "%x - %x\n", start, previous);
        } else {
            fprintf(stderr, "%x\n", previous);
        }
    } else
    if (count == 1) {
        fprintf(stderr, "%x\n", memory.compact->keys[0]);
    } else {
        fprintf(stderr, "%x -> %x\n", memory.compact->keys[0],
                memory.compact->keys[count - 1]);
    }
}

static ALWAYS_INLINE
bool qhat_debug_is_flat_default(const qhat_t *hat,
                                qhat_node_const_memory_t memory, uint8_t pos)
{
#define CASE(Size, Compact, Flat)  return !!IS_ZERO(Size, Flat[pos]);
    QHAT_VALUE_LEN_SWITCH(hat, memory, CASE);
#undef CASE
}

static void qhat_debug_print_flat_leaf(const qhat_t *hat, uint32_t flags,
                                       int depth, uint32_t prefix,
                                       qhat_node_const_memory_t memory)
{
    uint32_t start = 0;
    bool     value = qhat_debug_is_flat_default(hat, memory, 0);
    qhat_debug_print_indent(depth, true);
    fprintf(stderr, "+ ");
    if ((flags & QHAT_PRINT_KEYS)) {
        for (uint32_t i = 1; i < hat->desc->leaves_per_flat; i++) {
            bool new_value = qhat_debug_is_flat_default(hat, memory, i);
            if (new_value != value) {
                if (value) {
                    if (i - start != 1) {
                        fprintf(stderr, "%x - %x, ", prefix + start,
                                prefix + i - 1);
                    } else {
                        fprintf(stderr, "%x, ", prefix + start);
                    }
                }
                start = i;
                value = new_value;
            }
        }
        if (value) {
            if (hat->desc->leaves_per_flat - start != 1) {
                fprintf(stderr, "%x - %x\n", prefix + start,
                        (uint32_t)(prefix + hat->desc->leaves_per_flat - 1));
            } else {
                fprintf(stderr, "%x\n", prefix + start);
            }
        } else {
            fprintf(stderr, "\n");
        }
    } else {
        fprintf(stderr, "%x -> %x\n", prefix,
               (uint32_t)(prefix + hat->desc->leaves_per_flat - 1));
    }
}

static void qhat_debug_print_node(const qhat_t *hat, uint32_t flags,
                                  int depth, uint32_t prefix,
                                  qhat_node_t node, bool pure)
{
    qhat_node_const_memory_t memory;
    qhat_debug_print_indent(depth, false);
    fprintf(stderr, "%s node %u: prefix=%x/%x",
           (pure ? "Pure" : "Hybrid"), node.page, prefix,
           qhat_depth_prefix(hat, 0xffffffffu, depth - 1));
    if (node.leaf) {
        fprintf(stderr, ", leaf");
        if (node.compact) {
            memory.raw = qps_pg_deref(hat->qps, node.page);
            fprintf(stderr, " (compact, %d entries, parent %x -> %x)\n",
                    memory.compact->count, memory.compact->parent_left,
                    memory.compact->parent_right - 1);
            qhat_debug_print_compact_leaf(hat, flags, depth, prefix, memory);
        } else {
            fprintf(stderr, " (flat)\n");
            memory.raw = qps_pg_deref(hat->qps, node.page);
            qhat_debug_print_flat_leaf(hat, flags, depth, prefix, memory);
        }
    } else {
        fprintf(stderr, "\n");
        memory.raw = qps_pg_deref(hat->qps, node.page);
        qhat_debug_print_dispatch_node(hat, flags, depth, prefix,
                                       memory.nodes, QHAT_COUNT);
    }
}

#endif

void qhat_debug_print(qhat_t *hat, uint32_t flags)
{
    qps_hptr_deref(hat->qps, &hat->root_cache);
    fprintf(stderr, "Root: (%d)\n", hat->root_cache.handle);
    qhat_debug_print_dispatch_node(hat, flags, 0, 0U, hat->root->nodes,
                                   hat->desc->root_node_count);
    fprintf(stderr, "\n");
}

/* }}} */
/** \} */
