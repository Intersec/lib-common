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

#if !defined(IS_LIB_COMMON_THR_H) || defined(IS_LIB_COMMON_THR_SPSC_H)
#  error "you must include thr.h instead"
#else
#define IS_LIB_COMMON_THR_SPSC_H

#if !defined(__x86_64__) && !defined(__i386__)
#  error "this file assumes a strict memory model and is probably buggy on !x86"
#endif

/*
 * This file provides an implementation of:
 * - unbounded: means that the queue allocates as many nodes as its high
 *   watermark, and never releases them, so ensure you control its maximum
 *   size externally.
 * - wait free: there is absolutely *no* atomic instruction involved.
 * - non blocking: dequeue returns "false" if it seems there is nothing in the
 *   queue instead of blocking. It can be a false negative.
 * - SPSC queue: single producer, single consumer. It means that while enqueue
 *   and dequeue can run concurrently, only one consumer and one producer can
 *   work the queue at the same time.
 *
 * The code is adapted from
 * http://www.1024cores.net/home/lock-free-algorithms/queues/unbounded-spsc-queue
 */

/*
 * Copyright (c) 2010-2011 Dmitry Vyukov. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DMITRY VYUKOV "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL DMITRY VYUKOV OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of Dmitry Vyukov
 */

typedef struct spsc_node_t  spsc_node_t;
typedef struct spsc_queue_t spsc_queue_t;
typedef _Atomic(spsc_node_t *) atomic_spsc_node_t;

struct spsc_node_t {
    atomic_spsc_node_t next;
    void              *value;
};


struct spsc_queue_t {
    /* Consumer part */
    atomic_spsc_node_t head;
    char         pad_after_head[64 - sizeof(spsc_node_t *)];
    /* Producer part */
    spsc_node_t *tail;
    spsc_node_t *first;
    spsc_node_t *head_copy;
};

spsc_queue_t *spsc_queue_init(spsc_queue_t *q, size_t v_size) __leaf;
void          spsc_queue_wipe(spsc_queue_t *q) __leaf;

static inline spsc_node_t *spsc_queue_alloc_node(spsc_queue_t *q)
{
    if (likely(q->first != q->head_copy)) {
        spsc_node_t *n = q->first;

        q->first = atomic_load(&n->next);
        return n;
    }
    q->head_copy = atomic_load(&q->head);
    if (likely(q->first != q->head_copy)) {
        spsc_node_t *n = q->first;

        q->first = atomic_load(&n->next);
        return n;
    }

    return p_new_raw(spsc_node_t, 1);
}

static ALWAYS_INLINE void spsc_queue_push(spsc_queue_t *q, void *v)
{
    spsc_node_t *n = spsc_queue_alloc_node(q);

    atomic_init(&n->next, NULL);
    n->value = v;
    atomic_store(&q->tail->next, n);
    q->tail = n;
}

static ALWAYS_INLINE bool spsc_queue_pop(spsc_queue_t *q, void *v, size_t v_size)
{
    spsc_node_t *head = atomic_load_explicit(&q->head, memory_order_relaxed);
    spsc_node_t *n = atomic_load(&head->next);

    if (n) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
        memcpy(v, &n->value, v_size);
#else
        memcpy(v, (char *)&n->value[1] - v_size, v_size);
#endif
        atomic_store(&q->head, n);
        return true;
    }
    return false;
}

static inline void *spsc_queue_pop_ptr(spsc_queue_t *q)
{
    spsc_node_t *head = atomic_load_explicit(&q->head, memory_order_relaxed);
    spsc_node_t *n = atomic_load(&head->next);

    if (n) {
        void *v = n->value;
        atomic_store(&q->head, n);
        return v;
    }
    return NULL;
}

#define spsc_t(name)    spsc__##name##_t

#define spsc_queue_t(name, type_t) \
    typedef struct { spsc_queue_t q; } spsc_t(name);                       \
                                                                           \
    static inline spsc_t(name) *spsc__##name##_init(spsc_t(name) *q) {     \
        STATIC_ASSERT(sizeof(type_t) <= 8);                                \
        spsc_queue_init(&q->q, sizeof(type_t));                            \
        return q;                                                          \
    }                                                                      \
    static inline void spsc__##name##_wipe(spsc_t(name) *q) {              \
        spsc_queue_wipe(&q->q);                                            \
    }                                                                      \
                                                                           \
    static inline void spsc__##name##_push(spsc_t(name) *q, type_t v) {    \
        spsc_queue_push(&q->q, (void *)(uintptr_t)v);                      \
    }                                                                      \
    static inline bool spsc__##name##_pop(spsc_t(name) *q, type_t *v) {    \
        return spsc_queue_pop(&q->q, v, sizeof(type_t));                   \
    }

#define spsc_queue_ptr_t(name, type_t) \
    spsc_queue_t(name, type_t *)                                           \
    static inline type_t *spsc__##name##_pop_ptr(spsc_t(name) *q) {        \
        return spsc_queue_pop_ptr(&q->q);                                  \
    }

#define spsc_init(name, q)      spsc__##name##_init(q)
#define spsc_wipe(name, q)      spsc__##name##_wipe(q)
#define spsc_push(name, q, v)   spsc__##name##_push(q, v)
#define spsc_pop(name,  q, v)   spsc__##name##_pop(q, v)
#define spsc_pop2(name, q)      spsc__##name##_pop_ptr(q)

#endif
