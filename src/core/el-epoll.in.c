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

#include <pthread.h>
#include <sys/epoll.h>
#include <lib-common/unix.h>
#include <lib-common/thr.h>

static struct {
    int fd;
    int pending;
    int generation;
    struct epoll_event events[FD_SETSIZE];
} el_epoll_g = {
    .fd = -1,
};

static void el_fd_at_fork(void)
{
    p_close(&el_epoll_g.fd);
    el_epoll_g.generation++;
}

static void el_fd_initialize(void)
{
    if (unlikely(el_epoll_g.fd == -1)) {
#ifdef SIGPIPE
        signal(SIGPIPE, SIG_IGN);
#endif
        el_epoll_g.fd = epoll_create(1024);
        if (el_epoll_g.fd < 0)
            e_panic(E_UNIXERR("epoll_create"));
        fd_set_features(el_epoll_g.fd, O_CLOEXEC);
    }
}

el_t el_fd_register_d(int fd, bool own_fd, short events, el_fd_f *cb,
                      data_t priv)
{
    ev_t *ev = el_create(EV_FD, cb, priv, true);
    struct epoll_event event = {
        .data.ptr = ev,
        .events   = events,
    };

    el_fd_initialize();
    ev->fd.fd = fd;
    ev->fd.owned = own_fd;
    ev->fd.generation = el_epoll_g.generation;
    ev->events_wanted = events;
    ev->priority = EV_PRIORITY_NORMAL;
    if (unlikely(epoll_ctl(el_epoll_g.fd, EPOLL_CTL_ADD, fd, &event)))
        e_panic(E_UNIXERR("epoll_ctl"));
    return ev;
}

short el_fd_set_mask(ev_t *ev, short events)
{
    short old = ev->events_wanted;

    if (EV_IS_TRACED(ev)) {
        e_trace(0, "ev-fd(%p): set mask to %s%s", ev,
                events & POLLIN ? "IN" : "", events & POLLOUT ? "OUT" : "");
    }
    CHECK_EV_TYPE(ev, EV_FD);
    if (old != events && likely(ev->fd.generation == el_epoll_g.generation)) {
        struct epoll_event event = {
            .data.ptr = ev,
            .events   = ev->events_wanted = events,
        };
        if (unlikely(epoll_ctl(el_epoll_g.fd, EPOLL_CTL_MOD, ev->fd.fd,
                               &event))) {
            e_panic(E_UNIXERR("epoll_ctl"));
        }
    }
    return old;
}

data_t el_fd_unregister(ev_t **evp)
{
    if (*evp) {
        ev_t *ev = *evp;

        CHECK_EV_TYPE(ev, EV_FD);
        if (el_epoll_g.generation == ev->fd.generation) {
            epoll_ctl(el_epoll_g.fd, EPOLL_CTL_DEL, ev->fd.fd, NULL);
        }
        if (ev->fd.owned) {
            close(ev->fd.fd);
        }
        if (EV_FLAG_HAS(ev, FD_WATCHED)) {
            el_fd_act_timer_unregister(ev->priv.ptr);
        }
        if (EV_FLAG_HAS(ev, FD_FIRED)) {
            dlist_remove(&ev->ev_list);
        }
        return el_destroy(evp);
    }
    return (data_t)NULL;
}

static void el_loop_fds_poll(int timeout)
{
    el_bl_unlock();
    timeout = el_signal_has_pending_events() ? 0 : timeout;
    thr_enter_blocking_syscall();
    el_epoll_g.pending = epoll_wait(el_epoll_g.fd, el_epoll_g.events,
                                    countof(el_epoll_g.events), timeout);
    thr_exit_blocking_syscall();
    el_bl_lock();
    assert (el_epoll_g.pending >= 0 || ERR_RW_RETRIABLE(errno));
}

static bool el_fds_has_pending_events(void)
{
    if (el_epoll_g.pending == 0) {
        el_loop_fds_poll(0);
    }
    return el_epoll_g.pending != 0;
}

static void el_loop_fds(int timeout)
{
    ev_priority_t prio = EV_PRIORITY_LOW;
    int res, res2;
    uint64_t before, now;

    el_fd_initialize();

    if (el_epoll_g.pending == 0) {
        before = get_clock();
        el_loop_fds_poll(timeout);
        now    = get_clock();
        if (now - before > 100) {
            dlist_splice_tail(&_G.idle, &_G.idle_parked);
        }
    } else {
        now = get_clock();
    }

    res = res2 = el_epoll_g.pending;
    el_epoll_g.pending = 0;

    _G.has_run = false;
    el_timer_process(now);
    while (res-- > 0) {
        ev_t *ev = el_epoll_g.events[res].data.ptr;
        int  evs = el_epoll_g.events[res].events;

        if (unlikely(ev->type != EV_FD))
            continue;
        if (ev->priority > prio)
            prio = ev->priority;
        if (ev->priority == EV_PRIORITY_HIGH)
            el_fd_fire(ev, evs);
    }

    if (prio == EV_PRIORITY_HIGH) {
        return;
    }
    while (res2-- > 0) {
        ev_t *ev = el_epoll_g.events[res2].data.ptr;
        int  evs = el_epoll_g.events[res2].events;

        if (unlikely(ev->type != EV_FD))
            continue;
        if (ev->priority == prio)
            el_fd_fire(ev, evs);
    }
}
