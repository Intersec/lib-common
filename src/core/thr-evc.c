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

#include <lib-common/thr.h>

#if defined(__linux__)

/* This code has been adapted from http://atomic-ptr-plus.sourceforge.net/

   Copyright 2004-2005 Joseph W. Seigh

   Permission to use, copy, modify and distribute this software and its
   documentation for any purpose and without fee is hereby granted, provided
   that the above copyright notice appear in all copies, that both the
   copyright notice and this permission notice appear in supporting
   documentation.  I make no representations about the suitability of this
   software for any purpose. It is provided "as is" without express or implied
   warranty.
*/

/* Note that this file is #include-able for speed in code that needs it */

#include <sys/syscall.h>
#include <linux/futex.h>

#if !defined(__x86_64__) && !defined(__i386__)
#  error "this file assumes a strict memory model and is probably buggy on !x86"
#endif

#define futex_wait_private(futex, val, ts)  ({                               \
        typeof(futex) __futex = (futex);                                     \
        typeof(val)   __val   = (val);                                       \
        typeof(ts)    __ts    = (ts);                                        \
        int __res;                                                           \
                                                                             \
        for (;;) {                                                           \
            __res = syscall(SYS_futex, (unsigned long)__futex,               \
                            FUTEX_WAIT_PRIVATE, __val, (unsigned long)__ts, 0);\
            if (__res < 0 && errno == EINTR) {                               \
                continue;                                                    \
            }                                                                \
            break;                                                           \
        }                                                                    \
        __res;                                                               \
    })

#define futex_wake_private(futex, nwake)  ({                                 \
        typeof(futex) __futex = (futex);                                     \
        typeof(nwake) __nwake = (nwake);                                     \
        int __res;                                                           \
                                                                             \
        for (;;) {                                                           \
            __res = syscall(SYS_futex, (unsigned long)__futex,               \
                            FUTEX_WAKE_PRIVATE, __nwake, 0, 0);              \
            if (__res < 0 && errno == EINTR) {                               \
                continue;                                                    \
            }                                                                \
            break;                                                           \
        }                                                                    \
        __res;                                                               \
    })

void thr_ec_signal_n(thr_evc_t *ec, int count)
{
    atomic_fetch_add(&ec->key, 1);

    if (atomic_load(&ec->waiters))
        futex_wake_private(&ec->key, count);
}

static void thr_ec_wait_cleanup(void *arg)
{
    thr_evc_t *ec = arg;
    atomic_fetch_sub(&ec->waiters, 1);
}

void thr_ec_timedwait(thr_evc_t *ec, uint64_t key, long timeout)
{
    int canceltype, res;

    atomic_thread_fence(memory_order_acq_rel);

    /*
     * XXX: futex only works on integers (32bits) so we have to check if the
     *      high 32bits word changed. We can do this in a racy way because we
     *      assume it's impossible for the low 32bits to cycle between this
     *      test and the call to futex_wait_private.
     */
    if (unlikely(key != atomic_load(&ec->key))) {
        pthread_testcancel();
        return;
    }

    atomic_fetch_add(&ec->waiters, 1);
    pthread_cleanup_push(&thr_ec_wait_cleanup, ec);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);

    if (timeout > 0) {
        struct timespec spec = {
            .tv_sec  = timeout / 1000,
            .tv_nsec = (timeout % 1000) * 1000000,
        };
        res = futex_wait_private(&ec->key, (uint32_t)key, &spec);
    } else {
        res = futex_wait_private(&ec->key, (uint32_t)key, NULL);
    }
    if (res == 0)
        sched_yield();

    /* XXX passing NULL to pthread_setcanceltype() breaks TSAN */
    pthread_setcanceltype(canceltype, &res);
    pthread_cleanup_pop(1);
}

thr_evc_t *thr_ec_init(thr_evc_t *ec)
{
    p_clear(ec, 1);
    atomic_init(&ec->key, 0);
    atomic_init(&ec->waiters, 0);
    return ec;
}

void thr_ec_wipe(thr_evc_t *ec)
{
}

#else

#include <pthread.h>
#include <lib-common/datetime.h>

thr_evc_t *thr_ec_init(thr_evc_t *ec)
{
    p_clear(ec, 1);
    pthread_mutex_init(&ec->mutex, NULL);
    pthread_cond_init(&ec->cond, NULL);
    return ec;
}

void thr_ec_wipe(thr_evc_t *ec)
{
    pthread_cond_destroy(&ec->cond);
    pthread_mutex_destroy(&ec->mutex);
}

static void thr_ec_wait_cleanup(void *arg)
{
    thr_evc_t *ec = arg;
    atomic_fetch_sub(&ec->waiters, 1);
}

void thr_ec_timedwait(thr_evc_t *ec, uint64_t key, long timeout)
{
    struct timespec ts;
    int canceltype;

    if (thr_ec_get(ec) != key) {
        return;
    }

    pthread_mutex_lock(&ec->mutex);
    if (thr_ec_get(ec) != key) {
        pthread_mutex_unlock(&ec->mutex);
        return;
    }

    if (timeout > 0) {
        uint64_t usec;
        struct timeval tv;

        lp_gettv(&tv);
        usec = tv.tv_usec + timeout * 1000;
        ts.tv_sec  = tv.tv_sec + usec / 1000000;
        ts.tv_nsec = (usec % 1000000) * 1000;
    }

    atomic_fetch_add(&ec->waiters, 1);
    pthread_cleanup_push(&thr_ec_wait_cleanup, ec);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &canceltype);

    while (thr_ec_get(ec) == key) {
        if (timeout > 0) {
            if (pthread_cond_timedwait(&ec->cond, &ec->mutex, &ts) < 0) {
                if (errno == ETIMEDOUT) {
                    break;
                }
                e_panic(E_UNIXERR("pthread_cond_timedwait"));
            }
        } else {
            if (pthread_cond_wait(&ec->cond, &ec->mutex) < 0) {
                e_panic(E_UNIXERR("pthread_cond_wait"));
            }
        }
    }
    pthread_setcanceltype(canceltype, NULL);
    pthread_cleanup_pop(1);

    pthread_mutex_unlock(&ec->mutex);
}

void thr_ec_signal_n(thr_evc_t *ec, int count)
{
    pthread_mutex_lock(&ec->mutex);

    atomic_fetch_add(&ec->key, 1);

    if (atomic_fetch_add(&ec->waiters, 0)) {
        if (count == INT_MAX) {
            pthread_cond_broadcast(&ec->cond);
        } else {
            while (count-- > 0) {
                pthread_cond_signal(&ec->cond);
            }
        }
    }
    pthread_mutex_unlock(&ec->mutex);
}

#endif
