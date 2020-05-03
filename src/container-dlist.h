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

#ifndef IS_LIB_COMMON_CONTAINER_DLIST_H
#define IS_LIB_COMMON_CONTAINER_DLIST_H

/* XXX Don't include core.h since dlist are used by core-mem-stack, itself
 * used by core.h
 */
#include <stdbool.h>

/* Module for double linked lists.
 *
 * These lists are very similar to those used in the Linux kernel.
 *
 * 1. Acyclic lists, with head and tail
 * ====================================
 *
 * Conceptually, a list is a pair of pointers `next`(=head) and `prev`(=tail)
 * pointing to the first and the last elements of the list. This pair of
 * pointer is of type `dlist_t`. Elements of the list are structures that
 * embed a `dlist_t` field. The `dlist_t` field of the elements is used to
 * link elements together: the offset of that field is used to get a pointer
 * to the enclosing structure (i.e. to the element itself) ─ search for
 * "container_of" for more insights. Thus, the list and its elements have the
 * same `dlist_t` type but are two very different concepts.
 *
 * Given a `some_t` structure, you can create a `some_t` list by adding a
 * `dlist_t` field into the `some_t` structure. The offset of the `dlist_t`
 * field into the `some_t` structure is used to retrieve the structure itself.
 *
 * 2. Cyclic lists
 * ===============
 *
 * It is possible to use dlist_t to implement cyclic lists where all elements
 * are part of the list. For example, in `core-mem-ring.c`, the `ring_pool_t`
 * has a `ring_blk_t *cblk` field that is a cyclic list of ring_blk_t
 * elements.
 *
 * 3. Naming convention
 * ====================
 *
 * In order to differentiate the `dlist_t` used for the lists and the ones
 * used for their elements, it is strongly recommended to apply the following
 * naming convention:
 *
 *     - List fields or variables *should* contain 'list' in their name.
 *     - Anchor fields of list elements *must* be named 'link' or contain
 *     'link' in their names.
 *
 * 4. Example
 * ==========
 *
 * For example, let's build a some_t list (FIFO ordered) with three elements
 * (the drawing shows the result with arrows being the `next` pointer). Here,
 * even if `e3->next == list`, we see that `list` is not an element of the
 * list. The list head is `e1` and its tail is `e3`.
 *
 *     typedef struct some_t {             │
 *         int some_field;                 │ Graphical representation:
 *         dlist_t link;                   │
 *     };                                  │    ┌─→ e1 ─→ e2 ─┐
 *     some_t e1, e2, e3;                  │ [list]           │
 *     dlist_t list;                       │    └─←── e3 ←────┘
 *     dlist_init(&list);                  │
 *     dlist_add_tail(&list, &e1.link);    │ Linear representation:
 *     dlist_add_tail(&list, &e2.link);    │ [list] = e1, e2, e3
 *     dlist_add_tail(&list, &e3.link);    │
 *
 * 5. FIFO and LIFO implementations
 * ================================
 *
 * To implement FIFO or LIFO, consider that the head of the list is the next
 * element to be removed, which is achieved with pop(). Then, adding elements
 * in a FIFO is done with add_tail() or move_tail() functions, while adding
 * elements in a LIFO is done with add() or move() functions.
 */
typedef struct dlist_t {
    struct dlist_t *next, *prev;
} dlist_t;

#define DLIST_INIT(name)  { .next = &(name), .prev = &(name) }
#define DLIST(name)       dlist_t name = DLIST_INIT(name)

/** Put a \p dlist_t element in a safe and detached state.
 *
 * Mandatory for list heads before adding any element. Macro-based variants
 * can be used.
 *
 * Recommended for list links.
 */
static inline void dlist_init(dlist_t *l)
{
    l->next = l->prev = l;
}

/** Repair a dlist after its head or one of its elements address changed.
 *
 * Can happen in case of copy or reallocation: the 'prev' address of the
 * 'next' element and the 'next' address of the 'prev' element are broken
 * because they still have the old pointer value. This function refreshes
 * both.
 *
 * XXX Won't work on empty lists or detached elements: for a detached dlist,
 * 'next' and 'prev' contain the old address of the `dlist_t` and cannot be
 * dereferenced.
 *
 * Example:
 *
 *   bool was_empty = dlist_is_empty(&ptr->link);
 *   ptr = realloc(ptr, 1209);
 *   if (was_empty) {
 *       dlist_init(&ptr->link);
 *   } else {
 *       __dlist_repair(&ptr->link);
 *   }
 *
 * \param[in, out] e  Element or list head that got its address changed.
 */
static inline void __dlist_repair(dlist_t *e)
{
    e->next->prev = e;
    e->prev->next = e;
}

/** Put element `e` between `prev` and `next`.
 *
 * Low level API, beware.
 */
static inline void __dlist_add(dlist_t *e, dlist_t *prev, dlist_t *next)
{
    next->prev = e;
    e->next = next;
    e->prev = prev;
    prev->next = e;
}

/* XXX Private: remove all elements between `prev` and `next`. Do nothing to
 * clean the state of removed elements. The use of this fonction is strongly
 * discouraged for non-internal uses. */
static inline void
__dlist_remove(dlist_t *prev, dlist_t *next)
{
    next->prev = prev;
    prev->next = next;
}

/** Add element `e` at the head of list `l` (suited for LIFO). */
static inline void dlist_add(dlist_t *l, dlist_t *e)
{
    __dlist_add(e, l, l->next);
}

/** Add a new element `n` after an element `e`.
 *
 * Input:  ..., e_prev, e,    e_next, ...
 * Output: ..., e_prev, e, n, e_next, ...
 *
 * \param[in]  e  An element of a list.
 */
static inline void dlist_add_after(dlist_t *e, dlist_t *n)
{
    dlist_add(e, n);
}

/** Add a new element `e` at the tail of list `l` (suited for FIFO). */
static inline void dlist_add_tail(dlist_t *l, dlist_t *e)
{
    __dlist_add(e, l->prev, l);
}

/** Add a new element `n` before an element `e`.
 *
 * Input:  ..., e_prev,    e, e_next, ...
 * Output: ..., e_prev, n, e, e_next, ...
 *
 * This function can be used within a dlist_for_each_entry loop with `e` as
 * the current element.
 *
 * \param[in]  e  An element of a list.
 */
static inline void dlist_add_before(dlist_t *e, dlist_t *n)
{
    dlist_add_tail(e, n);
}

/** Remove element `e` from the list and re-init `e`. */
static inline void dlist_remove(dlist_t *e)
{
    __dlist_remove(e->prev, e->next);
    dlist_init(e);
}

/** Remove the head of list `l` (suited for FIFO and LIFO). */
static inline void dlist_pop(dlist_t *l)
{
    dlist_remove(l->next);
}

/** Remove element `e` from its list and add it at the head of `l`.
 *
 * (Suited if `l` is a LIFO).
 */
static inline void dlist_move(dlist_t *l, dlist_t *e)
{
    __dlist_remove(e->prev, e->next);
    dlist_add(l, e);
}

/** Remove element `e` from its list and add it at the tail of `l`.
 *
 * (Suited if `l` is a FIFO).
 */
static inline void dlist_move_tail(dlist_t *l, dlist_t *e)
{
    __dlist_remove(e->prev, e->next);
    dlist_add_tail(l, e);
}

/** Return true if `e` is the head of `l`. */
static inline bool dlist_is_first(const dlist_t *l, const dlist_t *e)
{
    return e->prev == l;
}

/** Return true if `e` is the tail of `l`. */
static inline bool dlist_is_last(const dlist_t *l, const dlist_t *e)
{
    return e->next == l;
}

/** Return true if the list has no element. */
static inline bool dlist_is_empty(const dlist_t *l)
{
    return l->next == l;
}

/** Return true if the list has exactly one element. */
static inline bool dlist_is_singular(const dlist_t *l)
{
    return l->next != l && l->next == l->prev;
}

/** Return true if the list has <= 1 element.
 *
 * Faster than \ref dlist_is_singular, can be used as a replacement when we
 * already know that the list is not empty.
 */
static inline bool dlist_is_empty_or_singular(const dlist_t *l)
{
    return l->next == l->prev;
}

static inline void
__dlist_splice2(dlist_t *prev, dlist_t *next, dlist_t *first, dlist_t *last)
{
    first->prev = prev;
    prev->next  = first;
    last->next  = next;
    next->prev  = last;
}

static inline void __dlist_splice(dlist_t *prev, dlist_t *next, dlist_t *src)
{
    __dlist_splice2(prev, next, src->next, src->prev);
    dlist_init(src);
}

/** Insert src at the head of dst.
 *
 * Input: [dst] = dh, ..., dt
 *        [src] = sh, ..., st
 *
 * Output: [dst] = sh, ..., st, dh, ..., dt
 *         [src] = []
 */
static inline void dlist_splice(dlist_t *dst, dlist_t *src)
{
    if (!dlist_is_empty(src)) {
        __dlist_splice(dst, dst->next, src);
    }
}

/** Insert src at the head of dst.
 *
 * Input: [dst] = dh, ..., dt
 *        [src] = sh, ..., st
 *
 * Output: [dst] = dh, ..., dt, sh, ..., st
 *         [src] = []
 */
static inline void dlist_splice_tail(dlist_t *dst, dlist_t *src)
{
    if (!dlist_is_empty(src)) {
        __dlist_splice(dst->prev, dst, src);
    }
}

/** Detach into `dst` the first elements of `src` until `e` (included).
 *
 * Input: [src] = s1, ..., si == e, sj, ..., sn
 *
 * Output: [dst] = s1, ..., si == e
 *         [src] = sj, ..., sn
 */
static inline void dlist_cut_at(dlist_t *src, dlist_t *e, dlist_t *dst)
{
    if (dlist_is_empty(src) || src == e) {
        dlist_init(dst);
    } else {
        dlist_t *e_next = e->next;

        dst->next = src->next;
        dst->next->prev = dst;
        dst->prev = e;
        e->next = dst;

        src->next = e_next;
        e_next->prev = src;
    }
}


/* Macros used to get a pointer on the enclosing structure. */
#define dlist_entry(ptr, type, member)  container_of(ptr, type, member)
#define dlist_entry_of(ptr, n, member)  dlist_entry(ptr, typeof(*n), member)
#define dlist_entry_safe(ptr, type, member) \
    ({ typeof(ptr) *__ptr = (ptr);          \
       __ptr ? dlist_entry(__ptr, type, member) : NULL })
#define dlist_entry_of_safe(ptr, n, member) \
    dlist_entry_safe(ptr, typeof(*n), member)

#define dlist_next_entry(e, mber)  dlist_entry((e)->mber.next, typeof(*e), mber)
#define dlist_prev_entry(e, mber)  dlist_entry((e)->mber.prev, typeof(*e), mber)
#define dlist_first_entry(l, type, mber)  dlist_entry((l)->next, type, mber)
#define dlist_last_entry(l, type, mber)   dlist_entry((l)->prev, type, mber)

/* Loops macros for dlists (iterate with a `dlist_t`). */
#define __dlist_for_each(pos, n, head, doit) \
    for (dlist_t *n = pos, *n##_next_ = n->next; \
         n != (head) && ({ doit; 1; }); \
         n = n##_next_, n##_next_ = n->next)

#define dlist_for_each(n, head) \
    __dlist_for_each((head)->next, n, head, )
#define dlist_for_each_start(pos, n, head) \
    __dlist_for_each(pos, n, head, )
#define dlist_for_each_continue(pos, n, head) \
    __dlist_for_each((pos)->next, n, head, )


#define __dlist_for_each_rev(pos, n, head, doit) \
    for (dlist_t *n = pos, *__prev = n->prev; \
         n != (head) && ({ doit; 1; }); \
         n = __prev, __prev = n->prev)

#define dlist_for_each_rev(n, head) \
    __dlist_for_each_rev((head)->prev, n, head, )
#define dlist_for_each_rev_start(pos, n, head) \
    __dlist_for_each_rev(pos, n, head, )
#define dlist_for_each_rev_continue(pos, n, head) \
    __dlist_for_each_rev((pos)->prev, n, head, )


/* Loops macros for dlists (iterate with a dlist entry that has to be declared
 * before the loop). */
#define __dlist_for_each_entry(pos, n, head, member) \
    __dlist_for_each(pos, __real_##n, head,                        \
                     n = dlist_entry_of(__real_##n, n, member))

#define dlist_for_each_entry(n, head, member) \
    __dlist_for_each_entry((head)->next, n, head, member)
#define dlist_for_each_entry_start(pos, n, head, member) \
    __dlist_for_each_entry(&(pos)->member, n, head, member)
#define dlist_for_each_entry_continue(pos, n, head, member) \
    __dlist_for_each_entry((pos)->member.next, n, head, member)


#endif
