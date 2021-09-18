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

#if !defined(IS_LIB_COMMON_THR_H) || defined(IS_LIB_COMMON_THR_MPSC_H)
#  error "you must include thr.h instead"
#else
#define IS_LIB_COMMON_THR_MPSC_H

#if !defined(__x86_64__) && !defined(__i386__)
#  error "this file assumes a strict memory model and is probably buggy on !x86"
#endif

/*
 * This file provides an implementation of lock-free intrusive MPSC queue.
 *
 * MPSC stands for Multiple-Producer, Single-Consumer.
 *
 * It means that it's possible to concurently add elements to these queues
 * without taking any locks, but that only one single consumer is allowed at
 * any time.
 */

typedef struct mpsc_node_t  mpsc_node_t;
typedef struct mpsc_queue_t mpsc_queue_t;
typedef _Atomic(mpsc_node_t *) atomic_mpsc_node_t;

/** \brief node to embed into structures to be put in an mpsc queue.
 */
struct mpsc_node_t {
    atomic_mpsc_node_t next;
};

/** \brief head of an mpsc queue.
 */
struct mpsc_queue_t {
    mpsc_node_t  head;
    atomic_mpsc_node_t tail;
};

/** \brief static initializer for mpsc_queues.
 */
#define MPSC_QUEUE_INIT(name)   { .tail = &(name).head }

/** \brief initializes an mpsc_queue.
 */
static inline mpsc_queue_t *mpsc_queue_init(mpsc_queue_t *q)
{
    p_clear(q, 1);
    atomic_init(&q->tail, &q->head);
    return q;
}

/** \brief tells whether an mpsc queue looks empty or not.
 *
 * This should be only used from the only mpsc consumer thread.
 *
 * The function may return false positives but no false negatives: the
 * consumer thread is the sole consumer, hence if it sees a non empty queue
 * the queue can't be freed without it knowing about it.
 *
 * If the function returns true though, some other thread may enqueue
 * something at the same time and the function will return a false positive,
 * hence the name "the queue looks empty but maybe it isn't".
 *
 * \param[in]  q  the queue
 * \returns true if the queue looks empty, false else.
 */
static inline bool mpsc_queue_looks_empty(mpsc_queue_t *q)
{
    return atomic_load_explicit(&q->head.next, memory_order_relaxed) == NULL;
}

/** \brief enqueue a task in the queue.
 *
 * This function has a vital role as it does empty -> non empty transition
 * detections which make it suitable to use like:
 *
 * <code>
 *     if (mpsc_queue_push(some_q, some_node))
 *         schedule_processing_of(some_q);
 * </code>
 *
 * \param[in]   q    the queue
 * \param[in]   n    the node to push
 * \returns true if the queue was empty before the push, false else.
 */
static inline bool mpsc_queue_push(mpsc_queue_t *q, mpsc_node_t *n)
{
    mpsc_node_t *prev;

    atomic_store_explicit(&n->next, NULL, memory_order_release);
    prev = atomic_exchange_explicit(&q->tail, n, memory_order_seq_cst);
    atomic_store_explicit(&prev->next, n, memory_order_seq_cst);
    return prev == &q->head;
}

/** \brief type used to enumerate through a queue during a drain.
 */
typedef struct mpsc_it_t {
    mpsc_queue_t *q;
    mpsc_node_t  *h;
} mpsc_it_t;

/** \brief initiates the iterator to start a drain.
 *
 * \warning
 *   it's forbidden not to drain fully after this function has been called.
 *
 * a typical drain looks like that:
 *
 * <code>
 *   static void doit(mpsc_node_t *node, data_t data)
 *   {
 *       process_node(node);
 *       freenode(node);
 *   }
 *
 *   {
 *       mpsc_it_t it;
 *
 *       mpsc_queue_drain_start(&it, &path->to.your_queue);
 *       do {
 *           mpsc_node_t *n = mpsc_queue_drain_fast(&it, doit,
 *                                                  (data_t){ .ptr = NULL });
 *           process_node(n);
 *           // XXX do NOT freenode(n) here mpsc_queue_drain_end will use it
 *       } while (!mpsc_queue_drain_end(&it, freenode));
 *   }
 * </code>
 *
 * It is important to note that the queue may go through the draining loop
 * multiple times if jobs are enqueued while the queue is drained.
 *
 * \param[out]  it   the iterator to initialize
 * \param[in]   q    the queue to drain
 */
static inline void mpsc_queue_drain_start(mpsc_it_t *it, mpsc_queue_t *q)
{
    it->q = q;
    it->h = atomic_load_explicit(&q->head.next, memory_order_acquire);
    atomic_store_explicit(&q->head.next, NULL, memory_order_relaxed);
    /* breaks if someone called mpsc_queue_drain_start with the queue empty */
    assert (it->h);
}

/** \brief fast path of the drain.
 *
 * \param[in]   it   the iterator to use
 * \param[in]   doit
 *     a function or macro that takes one mpsc_node_t as an argument: the node
 *     to process.
 * \param[in]   data  additionnal data to pass to \p doit.
 *
 * \returns
 *   a non NULL mpsc_node_t that may be the last one in the queue for
 *   processing purposes. mpsc_queue_drain_fast doesn't process this node, its
 *   up to the caller to do it.
 *   This node should NOT be freed, as mpsc_queue_drain_end will need it.
 */
static inline
mpsc_node_t *mpsc_queue_drain_fast(mpsc_it_t *it,
                                   void (*doit)(mpsc_node_t *, data_t data),
                                   data_t data)
{
    mpsc_node_t *h = it->h;
    mpsc_node_t *n;

    while (likely(n = atomic_load_explicit(&h->next, memory_order_acquire))) {
        (*doit)(h, data);
        h = n;
    }
    it->h = h;
    return h;
}

/** \brief implementation of mpsc_queue_drain_end.
 *
 * Do not use directly unless you want to override relax and know what you are
 * doing.
 */
static inline
bool __mpsc_queue_drain_end(mpsc_it_t *it, void (*freenode)(mpsc_node_t *),
                            void (*relax)(void))
{
    mpsc_queue_t *q = it->q;
    mpsc_node_t *h = it->h;
    mpsc_node_t *hq = h;

    if (h == atomic_load_explicit(&q->tail, memory_order_acquire)
    &&  atomic_compare_exchange_strong(&q->tail, &hq, &q->head))
    {
        it->h = NULL;
    } else {
        while (!(it->h = atomic_load_explicit(&h->next, memory_order_acquire))) {
            if (relax) {
                (*relax)();
            } else {
                cpu_relax();
            }
        }
    }
    if (freenode && h) {
        (*freenode)(h);
    }
    return it->h == NULL;
}

/** \brief test for the drain completion.
 *
 * \param[in]   it        the iterator to use
 * \param[in]   freenode
 *   a function or macro that will be called on the last node (actually the
 *   one #mpsc_queue_drain_fast returned) when the test for the mpsc queue
 *   emptyness has been done. If you don't need to free your nodes, use
 *   IGNORE.
 *
 * \returns
 *   true if the queue is really empty, in which case the drain is done, false
 *   else in which case the caller has to restart the drain.
 */
#define mpsc_queue_drain_end(it, freenode) \
    __mpsc_queue_drain_end(it, freenode, NULL)


/** \internal
 */
static inline __cold
bool mpsc_queue_pop_slow(mpsc_queue_t *q, mpsc_node_t *head, bool block)
{
    mpsc_node_t *tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    mpsc_node_t *next;

    if (head == tail) {
        atomic_store_explicit(&q->head.next, NULL, memory_order_relaxed);
        if (atomic_compare_exchange_strong(&q->tail, &tail, &q->head)) {
            return true;
        }
        atomic_store_explicit(&q->head.next, head, memory_order_relaxed);
    }

    next = atomic_load_explicit(&head->next, memory_order_relaxed);
    if (next == NULL) {
        if (!block) {
            return false;
        }
        while ((next = atomic_load_explicit(&head->next, memory_order_relaxed)) == NULL) {
            cpu_relax();
        }
    }
    atomic_store_explicit(&q->head.next, next, memory_order_relaxed);
    return true;
}

/** \brief pop one entry from the mpsc queue.
 *
 * \param[in] q      The queue.
 * \param[in] block  If the pop emptied the queue while an insertion is in
 *                   progress, wait for this insertion to be finished.
 *
 * \returns
 *   The node extracted from the queue or NULL if the queue is empty.
 *
 * \warning
 *   This API is unsafe and should be used only in specific use-cases where
 *   the enumeration of a queue must support imbrication (that is a call that
 *   enumerate the queue within an enumeration).
 */
static inline mpsc_node_t *mpsc_queue_pop(mpsc_queue_t *q, bool block)
{
    mpsc_node_t *head = atomic_load_explicit(&q->head.next, memory_order_relaxed);
    mpsc_node_t *next;

    if (head == NULL)
        return NULL;

    if (likely(next = atomic_load_explicit(&head->next, memory_order_relaxed))) {
        atomic_store_explicit(&q->head.next, next, memory_order_relaxed);
        return head;
    }
    if (block) {
        mpsc_queue_pop_slow(q, head, block);
        return head;
    }
    return mpsc_queue_pop_slow(q, head, block) ? head : NULL;
}

#endif
