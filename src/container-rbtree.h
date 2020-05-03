/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_CONTAINER_RBTREE_H
#define IS_LIB_COMMON_CONTAINER_RBTREE_H

#include <lib-common/core.h>

/** Red-Back Trees.
 *
 * \section rbtree_principles Principles
 *
 * This module provides a generic Red-Black Tree implementation. Red-Black
 * Trees are an automatically balanced variant of Binary Search Trees
 * providing properly bounded complexity for both modification and lookup
 * (the complexitiy is always O(log n)).
 *
 * In order to use it, you need to inline a rb_node_t structure in you own
 * structure and then use the \ref rb_tree_t macro in order to declare the
 * tree type and the associated helper.
 *
 * \code
 * typedef struct my_node_t {
 *     // A node need a key. That key is retrieved using the get_key
 *     // macro/function passed to rb_tree_t. So, it can be either
 *     // in the structure or fetched using informations contained
 *     // in the structure.
 *     int key;
 *
 *     // You can use the name of you choice for the inlined node, you'll
 *     // just have to provide this name when invoking rb_tree_t.
 *     // That structure is the glue of the tree.
 *     rb_node_t node;
 * } my_node_t;
 * #define MY_NODE_GET_KEY(node)  ((node)->key)
 * rb_tree_t(my, my_node_t, int, node, MY_NODE_GET_KEY, CMP)
 * \endcode
 *
 * A Structure can be in serveral trees at once, but each tree must use a
 * different rb_node_t inlined in the structure.
 *
 * It is highly adviced that both the comparison and the get_key callback are
 * inlinable.
 *
 *
 * \section rbtree_insertion Insertion
 *
 * Insertion prototype may be misleading. It returns NULL if the node gets
 * inserted or the previously inserted entry in case of collision.
 *
 * Since you are requested to provide the new entry as parameter, you must
 * take care not leaking the provided entry if the insertion detects a
 * collision.
 *
 * If you want to avoid the useless allocation, you can also use rb_find_slot
 * to find out if there's a collision or not and get the pointer to the slot
 * where the new entry should be allocated:
 *
 * \code
 * bool collision;
 * rb_node_t *parent;
 * rb_node_t **slot = rb_find_slot(my, rb, key, &parent, &collision);
 *
 * if (!collision) {
 *     // Allocate the new entry
 *     my_node_t *new_node = my_node_new();
 *     new_node->key = key;
 *     rb_insert_at(my, rb, parent, slot, new_node);
 * } else {
 *     // Collision, retrieve existing entry
 *     my_node_t *old_node = rb_entry(n, *slot);
 * }
 * \endcode
 *
 *
 * \section rbtree_by_hand Do it by hand
 *
 * You can provide you own implementation for rb_find() and rb_find_slot() if
 * you want a more complex behavior than the default one. In that case, you
 * cannot use rb_tree_t and have to invoke __RBTREE_TYPE and __RBTREE_HELPERS
 * by hand.
 *
 * The rb_find_slot() prototype should conform to the default one in order to
 * be compatible with rb_insert() implementation provided by __RBTREE_HELPERS.
 */

typedef struct rb_node_t {
    uintptr_t __parent;
    struct rb_node_t *left, *right;
} rb_node_t;

void rb_add_node(rb_node_t **root, rb_node_t *parent, rb_node_t *node) __leaf;
void rb_del_node(rb_node_t **root, rb_node_t *) __leaf;

static inline rb_node_t *__rb_first_node(rb_node_t *root)
{
    if (!root)
        return NULL;
    while (root->left)
        root = root->left;
    return root;
}

static inline rb_node_t *__rb_last_node(rb_node_t *root)
{
    if (!root)
        return NULL;
    while (root->right)
        root = root->right;
    return root;
}

rb_node_t *__rb_next(rb_node_t *) __leaf;
rb_node_t *__rb_prev(rb_node_t *) __leaf;

#define __RBTREE_TYPE(n, entry_t, link)                                      \
    /* This is the exact same structure for all instance, but keep it here   \
     * for type-safety.                                                      \
     */                                                                      \
    typedef struct rb_t(n) {                                                 \
       struct rb_node_t *root;                                               \
    } rb_t(n);                                                               \
    typedef entry_t rb_##n##_entry_t;                                        \
    GENERIC_FUNCTIONS(rb_##n##_t, rb_##n)                                    \
                                                                             \
    __unused__                                                               \
    static ALWAYS_INLINE entry_t *rb_##n##_entry(rb_node_t *node)            \
    {                                                                        \
        return (node) ? container_of(node, entry_t, link) : NULL;            \
    }

#define __RBTREE_LOOKUP(n, entry_t, key_t, link, get_key, compare)           \
    __unused__                                                               \
    static inline entry_t *rb_##n##_find(const rb_t(n) *rb, key_t k)         \
    {                                                                        \
        rb_node_t *node = rb->root;                                          \
                                                                             \
        while (node) {                                                       \
            entry_t *e = rb_entry(n, node);                                  \
            int cmp = compare(k, get_key(e));                                \
                                                                             \
            if (cmp < 0) {                                                   \
                node = node->left;                                           \
            } else                                                           \
            if (cmp > 0) {                                                   \
                node = node->right;                                          \
            } else {                                                         \
                return e;                                                    \
            }                                                                \
        }                                                                    \
        return NULL;                                                         \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline entry_t *rb_##n##_find_upper(const rb_t(n) *rb, key_t k)   \
    {                                                                        \
        entry_t *upper = NULL;                                               \
        rb_node_t *node = rb->root;                                          \
                                                                             \
        while (node) {                                                       \
            entry_t *e = rb_entry(n, node);                                  \
            int cmp = compare(k, get_key(e));                                \
                                                                             \
            if (cmp < 0) {                                                   \
                node = node->left;                                           \
                upper = e;                                                   \
            } else                                                           \
            if (cmp > 0) {                                                   \
                node = node->right;                                          \
            } else {                                                         \
                return e;                                                    \
            }                                                                \
        }                                                                    \
        return upper;                                                        \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline entry_t *rb_##n##_find_lower(const rb_t(n) *rb, key_t k)   \
    {                                                                        \
        entry_t *lower = NULL;                                               \
        rb_node_t *node = rb->root;                                          \
                                                                             \
        while (node) {                                                       \
            entry_t *e = rb_entry(n, node);                                  \
            int cmp = compare(k, get_key(e));                                \
                                                                             \
            if (cmp < 0) {                                                   \
                node = node->left;                                           \
            } else                                                           \
            if (cmp > 0) {                                                   \
                node  = node->right;                                         \
                lower = e;                                                   \
            } else {                                                         \
                return e;                                                    \
            }                                                                \
        }                                                                    \
        return lower;                                                        \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline rb_node_t **rb_##n##_find_slot(rb_t(n) *rb, key_t k,       \
                                                 rb_node_t **out_parent,     \
                                                 bool *collision)            \
    {                                                                        \
        rb_node_t **slot = &rb->root;                                        \
        rb_node_t *parent = NULL;                                            \
                                                                             \
        while (*slot) {                                                      \
            entry_t *slot_e = rb_entry(n, *slot);                            \
            int cmp = compare(k, get_key(slot_e));                           \
                                                                             \
            parent = *slot;                                                  \
            if (cmp < 0) {                                                   \
                slot = &(*slot)->left;                                       \
            } else                                                           \
            if (cmp > 0) {                                                   \
                slot = &(*slot)->right;                                      \
            } else {                                                         \
                *out_parent = NULL;                                          \
                *collision  = true;                                          \
                return slot;                                                 \
            }                                                                \
        }                                                                    \
        *out_parent = parent;                                                \
        *collision  = false;                                                 \
        return slot;                                                         \
    }

#define __RBTREE_HELPERS(n, entry_t, key_t, link, get_key, compare)          \
    __unused__                                                               \
    static ALWAYS_INLINE entry_t *rb_##n##_first(rb_t(n) *rb)                \
    {                                                                        \
        return rb_##n##_entry(__rb_first_node(rb->root));                    \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static ALWAYS_INLINE entry_t *rb_##n##_last(rb_t(n) *rb)                 \
    {                                                                        \
        return rb_##n##_entry(__rb_last_node(rb->root));                     \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static ALWAYS_INLINE entry_t *rb_##n##_next(entry_t *entry)              \
    {                                                                        \
        return rb_##n##_entry(__rb_next(&entry->link));                      \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static ALWAYS_INLINE entry_t *rb_##n##_prev(entry_t *entry)              \
    {                                                                        \
        return rb_##n##_entry(__rb_prev(&entry->link));                      \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline void rb_##n##_insert_at(rb_t(n) *rb, rb_node_t *parent,    \
                                          rb_node_t **slot, entry_t *e)      \
    {                                                                        \
        rb_add_node(&rb->root, parent, *slot = &e->link);                    \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline entry_t *rb_##n##_insert(rb_t(n) *rb, entry_t *e)          \
    {                                                                        \
        bool c;                                                              \
        rb_node_t *parent;                                                   \
        rb_node_t **slot = rb_find_slot(n, rb, get_key(e), &parent, &c);     \
                                                                             \
        if (c) {                                                             \
            return rb_entry(n, *slot);                                       \
        }                                                                    \
        rb_insert_at(n, rb, parent, slot, e);                                \
        return NULL;                                                         \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline void rb_##n##_remove(rb_t(n) *rb, entry_t *e)              \
    {                                                                        \
        rb_del_node(&rb->root, &e->link);                                    \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline entry_t *rb_##n##_remove_key(rb_t(n) *rb, key_t k)         \
    {                                                                        \
        entry_t *e = rb_find(n, rb, k);                                      \
                                                                             \
        if (e) {                                                             \
            rb_remove(n, rb, e);                                             \
        }                                                                    \
        return e;                                                            \
    }

#define rb_tree_t(n, entry_t, key_t, link, get_key, compare)                 \
    __RBTREE_TYPE(n, entry_t, link)                                          \
    __RBTREE_LOOKUP(n, entry_t, key_t, link, get_key, compare)               \
    __RBTREE_HELPERS(n, entry_t, key_t, link, get_key, compare)

#define rb_t(n)                       rb_##n##_t
#define rb_entry_t(n)                 rb_##n##_entry_t
#define rb_entry(n, s)                rb_##n##_entry(s)
#define rb_init(n, rb)                rb_##n##_init(rb)
#define rb_wipe(n, rb)                rb_##n##_wipe(rb)
#define rb_first(n, rb)               rb_##n##_first(rb)
#define rb_last(n, rb)                rb_##n##_last(rb)
#define rb_next(n, entry)             rb_##n##_next(entry)
#define rb_prev(n, entry)             rb_##n##_prev(entry)
#define rb_find(n, rb, v)             rb_##n##_find(rb, v)
#define rb_find_lower(n, rb, v)       rb_##n##_find_lower(rb, v)
#define rb_find_upper(n, rb, v)       rb_##n##_find_upper(rb, v)
#define rb_find_slot(n, rb, k, p, c)  rb_##n##_find_slot(rb, k, p, c)
#define rb_insert(n, rb, e)           rb_##n##_insert(rb, e)
#define rb_insert_at(n, rb, p, s, e)  rb_##n##_insert_at(rb, p, s, e)
#define rb_remove(n, rb, e)           rb_##n##_remove(rb, e)

#define rb_for_each(n, it, rb)                                               \
    for (rb_entry_t(n) *it = rb_first(n, rb); it; it = rb_next(n, it))

#define rb_for_each_safe(n, it, rb)                                          \
    for (rb_entry_t(n) *it = rb_first(n, rb), *__next;                       \
         it && ({ __next = rb_next(n, it); 1; }); it = __next)

#define rb_deep_wipe(n, rb, wipe)                                            \
    rb_for_each_safe(n, __it, rb) {                                          \
        rb_remove(n, rb, __it);                                              \
        wipe(&__it);                                                         \
    }

#endif
