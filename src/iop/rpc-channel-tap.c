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

#include <lib-common/log.h>

#include "rpc-channel-tap.h"

/* The whole IOP spy tap is debug-only. See rpc-channel-tap.h for the
 * rationale; the header provides no-op macros under NDEBUG. */
#ifndef NDEBUG

bool ic_tap_enabled_g;

typedef struct ic_tap_end_t {
    struct ic_tap_t *nonnull owner; /**< for teardown from the handler.*/
    el_t nullable el;               /**< watches the fd (owns it). */
} ic_tap_end_t;

/* One mirrored frame, pending in its pair's FIFO. */
typedef struct ic_tap_chunk_t {
    dlist_t link;
    bool is_tx; /**< which end (hence socket) carries it. */
    int offset; /**< bytes already sent (partial sends). */
    int len;
    byte data[];
} ic_tap_chunk_t;

typedef struct ic_tap_t {
    uint32_t ic_id;          /**< key in _G.taps (unique among live ics). */
    ic_tap_end_t tx;         /**< client end: locally-sent frames. */
    ic_tap_end_t rx;         /**< server end: frames received from peer. */
    el_t nullable listen_el; /**< transient listener until accept done. */
    uint64_t dropped;        /**< frames dropped on buffer overflow. */
    int queued;              /**< pending bytes, against IC_TAP_OUT_MAX. */

    /* FIFO of pending frames, shared by BOTH directions so the capture
     * keeps the true frame order: per-direction buffers could let a
     * locally-sent reply overtake the just-received query it answers. */
    dlist_t queue;
} ic_tap_t;

qm_k32_t(ic_tap, ic_tap_t *);

static struct {
    logger_t logger;

    qm_t(ic_tap) taps;
    sockunion_t su; /**< Based on IS_IC_TAP_IP, creating an ephemeral port. */
} ic_tap_g = {
#  define _G ic_tap_g
    .logger = LOGGER_INIT_INHERITS(NULL, "ic_tap"),
};

#  define IC_TAP_OUT_MAX (4 << 20) /* whole-pair mirror buffer cap. */

/* Send the mirror FIFO, in order, until a socket would block: a frame
 * is only emitted once every earlier frame -- whichever direction it
 * belongs to -- has left (head-of-line discipline), so the capture
 * order is the true frame order. One direction's mirror may thus stall
 * behind the other's backlog; only the mirror ever waits, never the
 * tapped IC. */
static void ic_tap_flush(ic_tap_t *t)
{
    ic_tap_end_t *head = NULL;

    while (!dlist_is_empty(&t->queue)) {
        ic_tap_chunk_t *chunk =
            dlist_first_entry(&t->queue, ic_tap_chunk_t, link);
        ssize_t res;

        head = chunk->is_tx ? &t->tx : &t->rx;
        if (!head->el) {
            /* The rx socket is not accepted yet: everything queues
             * behind its frames, keeping the order. */
            break;
        }
        res = send(
            el_fd_get_fd(head->el), chunk->data + chunk->offset,
            chunk->len - chunk->offset, MSG_DONTWAIT | MSG_NOSIGNAL
        );
        if (res <= 0) {
            break;
        }
        t->queued -= res;
        chunk->offset += res;
        if (chunk->offset < chunk->len) {
            break;
        }
        dlist_remove(&chunk->link);
        p_delete(&chunk);
        head = NULL;
    }

    /* Wait for writability on the head's socket only: waking up on the
     * other one could not send anything anyway. */
    if (t->tx.el) {
        el_fd_set_mask(t->tx.el, (head == &t->tx) ? POLLINOUT : POLLIN);
    }
    if (t->rx.el) {
        el_fd_set_mask(t->rx.el, (head == &t->rx) ? POLLINOUT : POLLIN);
    }
}

static void ic_tap_queue_wipe(ic_tap_t *t)
{
    while (!dlist_is_empty(&t->queue)) {
        ic_tap_chunk_t *chunk =
            dlist_first_entry(&t->queue, ic_tap_chunk_t, link);

        dlist_remove(&chunk->link);
        p_delete(&chunk);
    }
}

static void ic_tap_do_close(uint32_t ic_id)
{
    ic_tap_t *t = qm_get_def(ic_tap, &_G.taps, ic_id, NULL);

    if (!t) {
        return;
    }

    qm_del_key(ic_tap, &_G.taps, ic_id);
    el_unregister(&t->listen_el);
    el_unregister(&t->tx.el);
    el_unregister(&t->rx.el);
    ic_tap_queue_wipe(t);
    p_delete(&t);
}

static int ic_tap_on_event(el_t ev, int fd, short events, data_t priv)
{
    ic_tap_end_t *end = priv.ptr;

    if (events == EL_EVENTS_NOACT) {
        goto close;
    }
    if (events & POLLIN) {
        /* Drain (and discard) whatever the other end wrote; this is what
         * lets the peer end keep sending without stalling. */
        char buf[BUFSIZ];

        for (;;) {
            ssize_t res = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);

            if (res > 0) {
                continue;
            }
            if (res == 0 || !ERR_RW_RETRIABLE(errno)) {
                goto close;
            }
            break;
        }
    }
    if (events & POLLOUT) {
        ic_tap_flush(end->owner);
    }
    return 0;

close:
    ic_tap_do_close(end->owner->ic_id);
    return 0;
}

/* Event-loop-driven accept: completes the loopback pair without ever
 * blocking the IC event loop. */
static int ic_tap_on_accept(el_t ev, int fd, short events, data_t priv)
{
    ic_tap_t *t = priv.ptr;
    int sfd;
    int feats = FD_FEAT_NONBLOCK | FD_FEAT_CLOEXEC | FD_FEAT_TCP_NODELAY;

    if (events == EL_EVENTS_NOACT) {
        goto close;
    }
    sfd = acceptx(fd, feats);
    if (sfd < 0) {
        /* Connection not queued yet: wait for the next POLLIN. */
        if (ERR_RW_RETRIABLE(errno)) {
            return 0;
        }
        goto close;
    }

    /* Pair established: drop the listener and start the rx end (flushing
     * any frames buffered while the accept was pending). */
    el_unregister(&t->listen_el);
    t->rx.el =
        el_unref(el_fd_register(sfd, true, POLLIN, &ic_tap_on_event, &t->rx));
    ic_tap_flush(t);
    return 0;

close:
    ic_tap_do_close(t->ic_id);
    return 0;
}

static ic_tap_t *nullable ic_tap_pair_open(const ichannel_t *ic)
{
    sockunion_t su = _G.su;
    int feats = FD_FEAT_NONBLOCK | FD_FEAT_CLOEXEC | FD_FEAT_TCP_NODELAY;
    int lfd = -1;
    int cfd = -1;
    int port;
    ic_tap_t *t;

    /* Build a self-connected loopback TCP pair on IS_IC_TAP_IP with ephemeral
     * ports, fully non-blocking: the connect is left in progress and the
     * accept is driven by the event loop (ic_tap_on_accept), so the IC
     * event loop is never stalled. Frames produced before the pair is
     * established are buffered (or dropped on overflow, i.e. a mid-stream
     * capture). */
    lfd = listenx(-1, &su, 1, SOCK_STREAM, IPPROTO_TCP, feats);
    if (lfd < 0) {
        goto error;
    }
    port = getsockport(lfd, su.family);
    if (port <= 0) {
        goto error;
    }
    sockunion_setport(&su, port);
    cfd = connectx(-1, &su, 1, SOCK_STREAM, IPPROTO_TCP, feats);
    if (cfd < 0) {
        goto error;
    }

    t = p_new(ic_tap_t, 1);
    t->ic_id = ic->id;
    t->tx.owner = t;
    t->rx.owner = t;
    dlist_init(&t->queue);

    t->tx.el =
        el_unref(el_fd_register(cfd, true, POLLIN, &ic_tap_on_event, &t->tx));
    t->listen_el =
        el_unref(el_fd_register(lfd, true, POLLIN, &ic_tap_on_accept, t));
    qm_add(ic_tap, &_G.taps, ic->id, t);

    logger_trace(
        &_G.logger, 1, "opening for ic %u (%*pM): loopback ports tx=%d rx=%d",
        ic->id, LSTR_FMT_ARG(ic->name), getsockport(cfd, su.family), port
    );
    return t;

error:
    p_close(&lfd);
    p_close(&cfd);
    return NULL;
}

void ic_unix_tap_open(const ichannel_t *ic)
{
    if (!ic->is_unix || ic_is_local(ic)) {
        return;
    }
    /* One tap per live ic (a stale one was closed at disconnect). */
    if (qm_get_def(ic_tap, &_G.taps, ic->id, NULL)) {
        return;
    }
    ic_tap_pair_open(ic);
}

void ic_unix_tap_frame(ichannel_t *ic, bool is_tx, const void *data, int len)
{
    ic_tap_t *t = qm_get_def(ic_tap, &_G.taps, ic->id, NULL);
    ic_tap_chunk_t *chunk;

    if (!t) {
        return;
    }

    if (t->queued + len > IC_TAP_OUT_MAX) {
        /* Drop whole frames only, so the mirror byte stream stays a valid
         * back-to-back sequence of frames and the dissector never desyncs.*/
        t->dropped++;
        return;
    }

    chunk = p_new_extra(ic_tap_chunk_t, len);
    chunk->is_tx = is_tx;
    chunk->len = len;
    memcpy(chunk->data, data, len);
    dlist_add_tail(&t->queue, &chunk->link);
    t->queued += len;
    ic_tap_flush(t);
}

void ic_unix_tap_close(const ichannel_t *ic)
{
    ic_tap_do_close(ic->id);
}

void ic_tap_initialize(void)
{
    const char *ip = getenv("IS_IC_TAP_IP");

    qm_init(ic_tap, &_G.taps);
    if (ip && *ip && addr_info_str(&_G.su, ip, 0, AF_INET) >= 0) {
        ic_tap_enabled_g = true;
        logger_debug(&_G.logger, "IOP tap enabled on %s", ip);
    }
}

void ic_tap_shutdown(void)
{
    qm_for_each_value(ic_tap, t, &_G.taps) {
        el_unregister(&t->tx.el);
        el_unregister(&t->rx.el);
        ic_tap_queue_wipe(t);
        p_delete(&t);
    }
    qm_wipe(ic_tap, &_G.taps);
    ic_tap_enabled_g = false;
}

#endif /* NDEBUG */
