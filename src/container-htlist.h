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

#ifndef IS_LIB_COMMON_CONTAINER_HTLIST_H
#define IS_LIB_COMMON_CONTAINER_HTLIST_H

#include <lib-common/core.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

/* An implementation of anchor-based htlists.
 *
 * Htlists are basically single-linked lists with head and tail pointers.
 *
 * You can add prepend and append elements in O(1), or concatenate such
 * lists in O(1).
 *
 * Htlists point to a sublist of elements and can even overlap.
 *
 * Here is an example with 3 htlists and 7 htnodes:
 *
 *             l3-----------------.
 * l1-----------|-----.    l2-----|-----.
 *  |           |     |     |     |     |
 *  v           v     v     v     v     v
 *  x --> x --> x --> x --> x --> x --> x --> NULL
 *
 * You can add elements to l1 at the end, it won't break l3, and will
 * make it "longer". If you insert elements at the beginning of l2,
 * they will not be shared by l3.
 *
 * IOW, for an htlist, the tail pointer may point to an element whose
 * "next" pointer is _not_ NULL without breaking anything.
 *
 * The programmer is responsible for potential side effects when lists are
 * modified: in the example above, doing htlist_add_tail(l1, l3) will
 * result in a big mess:
 *
 *                      .---------.
 * l1-------------------'  l2-----|-----.
 *  |                       |     |     |
 *  v                       v     v     v
 *  x --> x --> x --> x     x --> x --> x --> NULL
 *              ^     |
 *              `-----'
 *
 * Note that an empty htlist looks like this:
 *
 *     l --> NULL
 *     |
 *     v
 *     ?? (may not be NULL, should not be dereferenced)
 *
 * For now, no function is provided to remove elements inside an
 * htlist, only pop at the start.  Such an operation would be
 * inefficient and rather ill defined anyway.
 */

typedef struct htnode_t {
    struct htnode_t * nullable next;
} htnode_t;

typedef struct htlist_t {
    htnode_t * nullable head;
    htnode_t * nullable tail;
} htlist_t;
GENERIC_INIT(htlist_t, htlist);

#define HTLIST(name)       htlist_t name = HTLIST_INIT(name)
#define HTLIST_INIT(name)  { .tail = NULL }

static inline bool htlist_is_empty(const htlist_t * nonnull l)
{
    return l->tail == NULL;
}

static inline void htlist_add(htlist_t * nonnull l, htnode_t * nonnull n)
{
    n->next = l->head;
    l->head = n;
    if (htlist_is_empty(l)) {
        l->tail = n;
    }
}

static inline void htlist_add_tail(htlist_t * nonnull l, htnode_t * nonnull n)
{
    if (htlist_is_empty(l)) {
        htlist_add(l, n);
    } else {
        n->next = l->tail->next;
        l->tail->next = n;
        l->tail  = n;
    }
}

/* Adding a node after another one. If prev is NULL, the new_node will be
 * added to the head */
static inline void
htlist_add_after(htlist_t * nonnull l, htnode_t * nullable prev,
                 htnode_t * nonnull new_node)
{
    if (!prev) {
        return htlist_add(l, new_node);
    }

    new_node->next = prev->next;
    prev->next = new_node;

    if (l->tail == prev) {
        l->tail = new_node;
    }
}

static inline htnode_t * nonnull htlist_pop(htlist_t * nonnull l)
{
    htnode_t *res = l->head;

    assert (!htlist_is_empty(l));

    l->head = res->next;
    if (l->tail == res) {
        l->tail = NULL;
    }
    return res;
}

static inline void htlist_splice(htlist_t * nonnull dst,
                                 htlist_t * nonnull src)
{
    if (!htlist_is_empty(src)) {
        src->tail->next = dst->head;
        dst->head = src->head;

        if (htlist_is_empty(dst)) {
            dst->tail = src->tail;
        }
    }
}

static inline void htlist_move(htlist_t * nonnull dst,
                               htlist_t * nonnull src)
{
    htlist_init(dst);
    htlist_splice(dst, src);
    htlist_init(src);
}

static inline void htlist_splice_tail(htlist_t * nonnull dst,
                                      htlist_t * nonnull src)
{
    if (!htlist_is_empty(src)) {
        src->tail->next = dst->tail->next;
        dst->tail->next = src->head;
        /* XXX: src->tail points to an actual element because src is
         * not empty.
         */
        dst->tail = src->tail;
    }
}

#define htlist_entry(ptr, type, member)    container_of(ptr, type, member)
#define htlist_entry_of(ptr, n, member)    htlist_entry(ptr, typeof(*(n)), member)
#define htlist_first_entry(l, type, member)  htlist_entry((l)->head, type, member)
#define htlist_last_entry(l, type, member)  htlist_entry((htnode_t *)(l)->tail, type, member)
#define htlist_pop_entry(hd, type, member) htlist_entry(htlist_pop(hd), type, member)

#define __htlist_for_each(pos, n, hd, doit)  \
     for (htnode_t *n##_end_ = (hd)->tail ? (hd)->tail->next : (pos),        \
          *n = (pos);                                                        \
          n != n##_end_ && ({ doit; 1; }); n = n->next)

#define htlist_for_each(n, hd)    __htlist_for_each((hd)->head, n, hd, )
#define htlist_for_each_start(pos, n, hd)    __htlist_for_each(pos, n, hd, )

#define htlist_for_each_entry(n, hd, member)  \
    __htlist_for_each((hd)->head, __real_##n, hd,                            \
                      n = htlist_entry_of(__real_##n, n, member))
#define htlist_for_each_entry_start(pos, n, hd, member) \
    __htlist_for_each(&(pos)->member, __real_##n, hd,                        \
                      n = htlist_entry_of(__real_##n, n, member))

#define htlist_add_entry_after(hd, prev, new_entity, member)  \
    do {                                                                     \
        typeof(prev) _ptr = prev;                                            \
        typeof(prev) _n_ptr = new_entity;                                    \
        htnode_t *_prev_node = _ptr ? &_ptr->member : NULL;                  \
                                                                             \
        htlist_add_after(hd, _prev_node, &_n_ptr->member);                   \
    } while (0)

#define htlist_deep_clear(ptr, type, member, delete)  \
    do {                                                                     \
        type *e, *prev = NULL;                                               \
        htlist_t *_ptr = ptr;                                                \
                                                                             \
        htlist_for_each_entry(e, _ptr, member) {                             \
            if (prev) {                                                      \
                delete(&prev);                                               \
            }                                                                \
            prev = e;                                                        \
        }                                                                    \
        if (prev) {                                                          \
            delete(&prev);                                                   \
        }                                                                    \
        _ptr->head = NULL;                                                   \
        _ptr->tail = _ptr->head;                                             \
    } while (0)

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
