/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
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

#define CONCAT(Token, Size)    Token ## Size
#define FUNCNAME               CONCAT

#define _Compact(Size)      CONCAT(compact, Size)
#define Compact             _Compact(SIZE)

#define _Flat(Size)         CONCAT(u, Size)
#define Flat                _Flat(SIZE)

#define _get(Size)          FUNCNAME(qhat_get_path, Size)
#define get                 _get(SIZE)

#define _set(Size)          FUNCNAME(qhat_set_path, Size)
#define set                 _set(SIZE)

#define _set0(Size)         FUNCNAME(qhat_set0_path, Size)
#define set0                _set0(SIZE)

#define _remove(Size)       FUNCNAME(qhat_remove_path, Size)
#define remove              _remove(SIZE)

#define _get_null(Size)     FUNCNAME(qhat_get_path_null, Size)
#define get_null            _get_null(SIZE)

#define _set_null(Size)     FUNCNAME(qhat_set_path_null, Size)
#define set_null            _set_null(SIZE)

#define _set0_null(Size)    FUNCNAME(qhat_set0_path_null, Size)
#define set0_null           _set0_null(SIZE)

#define _remove_null(Size)  FUNCNAME(qhat_remove_path_null, Size)
#define remove_null         _remove_null(SIZE)

#define _lookup(Size)       FUNCNAME(qhat_lookup, Size)
#define lookup              _lookup(SIZE)

#define _update_path(Size)  FUNCNAME(qhat_update_path, Size)
#define update_path         _update_path(SIZE)

#define _init(Size)         FUNCNAME(qhat_props_from_len, Size)
#define init                _init(SIZE)

#define _flatten_leaf(Size) FUNCNAME(qhat_flatten_leaf, Size)
#define flatten_leaf        _flatten_leaf(SIZE)

#define _unflatten_leaf(Size)  FUNCNAME(qhat_unflatten_leaf, Size)
#define unflatten_leaf         _unflatten_leaf(SIZE)

#define __type(Size)        qhat_##Size##_t
#define _type(Size)         __type(Size)
#define type_t              _type(SIZE)

#define __compact(Size)     qhat_compact##Size##_t
#define _compact(Size)      __compact(Size)
#define compact_t           _compact(SIZE)


#define VALUE_LEN                (SIZE / 8)
#define VALUE_LEN_LOG            (bsr32(VALUE_LEN))
#define LEAVES_PER_FLAT          ((PAGES_PER_FLAT * QPS_PAGE_SIZE) / sizeof(type_t))
#define LEAF_INDEX_BITS          (bsr32(LEAVES_PER_FLAT))
#define LEAF_INDEX_MASK          BITMASK_LT(uint32_t, LEAF_INDEX_BITS)
#define LEAVES_PER_COMPACT       fieldsizeof(compact_t, values) / sizeof(type_t)
#define SPLIT_COMPACT_THRESHOLD  (3 * LEAVES_PER_COMPACT / 4)
#define ROOT_NODE_COUNT          (1U << ((32 - LEAF_INDEX_BITS) % QHAT_SHIFT))
#define PAGES_PER_COMPACT        (sizeof(compact_t) / QPS_PAGE_SIZE)


static NEVER_INLINE
void flatten_leaf(qhat_path_t *path)
{
    qhat_node_t new_node = qhat_alloc_leaf(path->hat, false);
    qhat_node_t old_node = PATH_NODE(path);
    qhat_node_const_memory_t memory = qhat_node_deref(path);
    qhat_node_memory_t new_memory;
    uint32_t prefix = 0, previous = 0;

    assert (old_node.leaf);
    assert (path->depth == QHAT_DEPTH_MAX - 1);
    assert (qhat_node_is_pure(path));

    PATH_NODE(path) = new_node;
    qhat_update_parent_pure(path, new_node);
    new_memory = qhat_node_w_deref(path);

    assert (memory.Compact->count <= LEAVES_PER_FLAT);
    for (uint32_t i = 0; i < memory.Compact->count; i++) {
        uint32_t key = memory.Compact->keys[i] & LEAF_INDEX_MASK;
        if (i == 0) {
            prefix = memory.Compact->keys[i] - key;
        } else {
            assert (memory.Compact->keys[i] - key == prefix);
            assert (previous < key);
        }
        previous = key;

        new_memory.Flat[key] = memory.Compact->values[i];
    }

    MOVED_TO_NEW_FLAT(path, memory.compact->count);
    qhat_unmap_node(path->hat, old_node);
    e_named_trace(3, "trie/node/flatten", "flattend node %u in %u",
                  old_node.page, new_node.page);
}

#define next_1(Word)  ({                                                     \
        int __bit = bsf64(Word);                                             \
                                                                             \
        Word >>= __bit;                                                      \
        __bit;                                                               \
    })

#define foreach_1(pos, word)                                                 \
    for (uint64_t __word = (word), pos = __word ? next_1(__word) : 0; __word;\
         pos += ({ RST_BIT(&__word, 0); next_1(__word); }))


static NEVER_INLINE
void unflatten_leaf(qhat_path_t *path)
{
    qhat_node_t new_node = qhat_alloc_leaf(path->hat, true);
    qhat_node_t old_node = PATH_NODE(path);
    qhat_node_const_memory_t memory = qhat_node_deref(path);
    qhat_node_memory_t new_memory;
    uint32_t prefix = qhat_depth_prefix(path->hat, path->key,
                                        QHAT_DEPTH_MAX - 1);
    uint32_t pos = 0;

    assert (old_node.leaf);
    assert (path->depth == QHAT_DEPTH_MAX - 1);
    assert (qhat_node_is_pure(path));
    assert (!qhat_leaf_is_full(path));

    PATH_NODE(path) = new_node;
    qhat_update_parent_pure(path, new_node);
    new_memory = qhat_node_w_deref(path);

    for (uint32_t i = 0; i < LEAVES_PER_FLAT; i++) {
#if SIZE == 128
        if (memory.Flat[i].h != 0 || memory.Flat[i].l != 0) {
#else
        if (memory.Flat[i] != 0) {
#endif
            assert (pos < LEAVES_PER_COMPACT);
            new_memory.Compact->keys[pos]   = prefix + i;
            new_memory.Compact->values[pos] = memory.Flat[i];
            pos++;
        }
    }
    assert (pos <= LEAVES_PER_COMPACT);
    new_memory.Compact->count = pos;
    new_memory.Compact->parent_left  = PATH_IN_PARENT_IDX(path);
    new_memory.Compact->parent_right = new_memory.Compact->parent_left + 1;

    MOVED_TO_COMPACT(path, new_memory.compact->count);
    qhat_unmap_node(path->hat, old_node);
    e_named_trace(3, "trie/node/unflatten", "unflattend node %u in %u",
                  old_node.page, new_node.page);
    PATH_STRUCTURE_CHANGED("trie/node/unflatten", path);
}

static ALWAYS_INLINE
void lookup(qhat_path_t *path)
{
    qhat_node_t (*nodes)[QHAT_DEPTH_MAX] = &path->path;
    qhat_t      *hat   = path->hat;
    qps_t       *qps   = hat->qps;
    uint32_t     key   = path->key;
    uint32_t     shift = 2 * QHAT_SHIFT + LEAF_INDEX_BITS;

    path->generation = hat->struct_gen;
    (*nodes)[0] = hat->root->nodes[shift == 32 ? 0 : key >> shift];
    if ((*nodes)[0].value == 0 || (*nodes)[0].leaf) {
        path->depth = 0;
        return;
    }

    shift -= QHAT_SHIFT;
    (*nodes)[1] = qhat_node_deref_(qps, (*nodes)[0]).nodes[(key >> shift) & QHAT_MASK];
    if ((*nodes)[1].value == 0 || (*nodes)[1].leaf) {
        path->depth = 1;
        return;
    }

    shift -= QHAT_SHIFT;
    (*nodes)[2] = qhat_node_deref_(qps, (*nodes)[1]).nodes[(key >> shift) & QHAT_MASK];
    path->depth = 2;
}

static ALWAYS_INLINE
void update_path(qhat_path_t *path, bool can_stat)
{
    qhat_t *hat = path->hat;

    if (can_stat && hat->do_stats) {
        qps_hptr_w_deref(hat->qps, &hat->root_cache);
    } else {
        qps_hptr_deref(hat->qps, &hat->root_cache);
    }
    if (path->generation != hat->struct_gen) {
        lookup(path);
    }
}

static const type_t *get(qhat_path_t *path)
{
    qhat_node_const_memory_t memory;
    update_path(path, false);
    if (PATH_NODE(path).value == 0) {
        return NULL;
    }

    memory = qhat_node_deref(path);

    if (PATH_NODE(path).compact) {
        uint32_t pos = qhat_compact_lookup(memory.compact, 0, path->key);
        if (pos >= memory.compact->count
        || memory.compact->keys[pos] != path->key) {
            return NULL;
        }
        return &memory.Compact->values[pos];
    } else {
        uint32_t pos = path->key & LEAF_INDEX_MASK;
        return &memory.Flat[pos];
    }
}

static const type_t *get_null(qhat_path_t *path)
{
    if (!qps_bitmap_get(&path->hat->bitmap, path->key)) {
        return NULL;
    }

    return get(path) ?: acast(const type_t, &qhat_default_zero_g);
}

static type_t *set(qhat_path_t *path)
{
    qhat_node_memory_t memory;
    update_path(path, true);

    for (;;) {
        if (PATH_NODE(path).value == 0) {
            e_named_trace(2, "trie/insert",
                          "no node found for key %u, allocating", path->key);
            qhat_create_leaf(path);
            PATH_STRUCTURE_CHANGED("trie/insert", path);
            break;
        } else
        if (qhat_leaf_is_full(path)) {
            if (path->depth == QHAT_DEPTH_MAX - 1 && qhat_node_is_pure(path)) {
                e_named_trace(2, "trie/insert",
                              "pure bucket full for key %u, flatten %u",
                              path->key, PATH_NODE(path).page);
                flatten_leaf(path);
            } else {
                e_named_trace(2, "trie/insert",
                              "bucket full for key %u, splitting %u",
                              path->key, PATH_NODE(path).page);
                qhat_split_leaf(path);
            }
        } else {
            break;
        }
        PATH_STRUCTURE_CHANGED("trie/insert", path);
        lookup(path);
    }

    memory = qhat_node_w_deref(path);

    if (PATH_NODE(path).compact) {
        uint32_t slot = qhat_compact_lookup(memory.compact, 0, path->key);
        assert (likely(slot <= memory.Compact->count));

        if (slot == memory.Compact->count || memory.Compact->keys[slot] != path->key) {
            if (slot != memory.Compact->count) {
                p_move(&memory.Compact->values[slot + 1],
                       &memory.Compact->values[slot],
                       memory.Compact->count - slot);
                p_move(&memory.Compact->keys[slot + 1],
                       &memory.Compact->keys[slot],
                       memory.Compact->count - slot);
            }
            memory.Compact->keys[slot] = path->key;
            p_clear(&memory.Compact->values[slot], 1);
            memory.Compact->count++;
            if (path->hat->do_stats) {
                path->hat->root->entry_count++;
                path->hat->root->key_stored_count++;
            }
        }
        return &memory.Compact->values[slot];
    } else {
        uint32_t pos = path->key & LEAF_INDEX_MASK;
        void  *val   = &memory.Flat[pos];

        if (path->hat->do_stats) {
            qhat_128_t zero = { 0, 0 };

            if (memcmp(val, &zero, VALUE_LEN) == 0) {
                path->hat->root->entry_count++;
                path->hat->root->zero_stored_count--;
            }
        }
        return val;
    }
}

static type_t *set_null(qhat_path_t *path)
{
    qps_bitmap_set(&path->hat->bitmap, path->key);
    return set(path);
}

static bool remove(qhat_path_t *path, type_t *ptr)
{
    bool has_value = true;
    qhat_node_memory_t memory;
    update_path(path, true);

    if (!PATH_NODE(path).leaf) {
        if (ptr != NULL) {
            p_clear(ptr, 1);
        }
        return false;
    }

    memory = qhat_node_w_deref(path);

    if (PATH_NODE(path).compact) {
        uint32_t slot = qhat_compact_lookup(memory.compact, 0, path->key);
        if (slot >= memory.compact->count
        || memory.compact->keys[slot] != path->key) {
            if (ptr != NULL) {
                p_clear(ptr, 1);
            }
            return false;
        }
        memory.compact->count--;
        if (path->hat->do_stats) {
            path->hat->root->entry_count--;
        }

        if (ptr != NULL) {
            *ptr = memory.Compact->values[slot];
        }
        if (slot != memory.Compact->count) {
            p_move(&memory.Compact->values[slot],
                   &memory.Compact->values[slot + 1],
                   memory.Compact->count - slot);
            p_move(&memory.Compact->keys[slot],
                   &memory.Compact->keys[slot + 1],
                   memory.Compact->count - slot);
        }
    } else {
        uint32_t pos = path->key & LEAF_INDEX_MASK;
        type_t  *val = &memory.Flat[pos];

        if (path->hat->do_stats) {
#if SIZE == 128
            if (val->h == 0 || val->l == 0) {
#else
            if (*val == 0) {
#endif
                path->hat->root->entry_count--;
                path->hat->root->zero_stored_count++;
            }
        }

        if (ptr != NULL) {
            *ptr = *val;
        }
        p_clear(val, 1);
    }

    qhat_optimize(path);
    return has_value;
}

static bool remove_null(qhat_path_t *path, type_t *ptr)
{
    if (!qps_bitmap_remove(&path->hat->bitmap, path->key)) {
        if (ptr != NULL) {
            p_clear(ptr, 1);
        }
        return false;
    }
    remove(path, ptr);
    return true;
}

static void set0(qhat_path_t *path, void *ptr)
{
    IGNORE(remove(path, ptr));
}

static void set0_null(qhat_path_t *path, void *ptr)
{
    if (!qps_bitmap_set(&path->hat->bitmap, path->key)) {
        return;
    }

    IGNORE(remove(path, ptr));
}

static void init(qhat_desc_t *desc, qhat_desc_t *desc_null)
{
    desc->value_len               = VALUE_LEN;
    desc->value_len_log           = VALUE_LEN_LOG;
    desc->leaves_per_compact      = LEAVES_PER_COMPACT;
    desc->pages_per_compact       = PAGES_PER_COMPACT;
    desc->split_compact_threshold = SPLIT_COMPACT_THRESHOLD;
    desc->leaves_per_flat         = LEAVES_PER_FLAT;
    desc->pages_per_flat          = PAGES_PER_FLAT;
    desc->leaf_index_bits         = LEAF_INDEX_BITS;
    desc->leaf_index_mask         = LEAF_INDEX_MASK;
    desc->root_node_count         = ROOT_NODE_COUNT;

    desc->getf    = (qhat_getter_f)&get;
    desc->setf    = (qhat_setter_f)&set;
    desc->set0f   = (qhat_setter0_f)&set0;
    desc->removef = (qhat_remover_f)&remove;

    desc->flattenf   = flatten_leaf;
    desc->unflattenf = unflatten_leaf;

    *desc_null = *desc;
    desc_null->getf = (qhat_getter_f)&get_null;
    desc_null->setf = (qhat_setter_f)&set_null;
    desc_null->set0f = (qhat_setter0_f)&set0_null;
    desc_null->removef = (qhat_remover_f)&remove_null;
}

#undef SIZE
#undef PAGES_PER_FLAT
