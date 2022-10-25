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

#include <lib-common/net.h>
#include <lib-common/sctp-tools.h>
#include <lib-common/unix.h>

#include "netdb.h"

/* {{{ Types */

typedef enum sctp_msg_type_t {
    SCTP_MSG_TYPE_NOTIF = 1,
    SCTP_MSG_TYPE_DATA
} sctp_msg_type_t;

qvector_t(sctp_su, sockunion_t);

typedef struct sctp_msg_t {
    dlist_t  msg_list;
    lstr_t   msg;
    uint32_t ppid;              /* Payload Protocol ID */
} sctp_msg_t;

static sctp_msg_t * nonnull sctp_msg_init(sctp_msg_t * nonnull msg)
{
    p_clear(msg, 1);

    dlist_init(&msg->msg_list);
    return msg;
}
GENERIC_NEW(sctp_msg_t, sctp_msg);

static void sctp_msg_wipe(sctp_msg_t * nonnull msg)
{
    lstr_wipe(&msg->msg);
    dlist_remove(&msg->msg_list);
}
GENERIC_DELETE(sctp_msg_t, sctp_msg);

static inline void sctp_msg_list_wipe(dlist_t * nonnull l)
{
    dlist_for_each_entry(sctp_msg_t, msg, l, msg_list) {
        sctp_msg_delete(&msg);
    }
}

/** \brief SCTP connection private context.
 *
 * This structure is only known by this file. Any reference to this structure
 * in the user context is managed by the \p user_ctx structure.
 */
typedef struct sctp_conn_priv_t {
    el_t nonnull evh;

    /* Allows to differentiate a context for active connection and a context
     * listening for connections. */
    bool is_listening;

    /* Indicates that a full message has been read in the read buffer, and so
     * that the read buffer should be reset before the next read.
     */
    bool reset_rbuf;

    /* Read buffer. */
    sb_t rbuf;

    /* Queue of messages to be sent on this connection. */
    dlist_t msgs;

    sctp_on_connect_f on_connect_cb;

    sctp_on_disconnect_f on_disconnect_cb;

    sctp_on_data_f on_data_cb;

    sctp_on_accept_f on_accept_cb;

    /* Public context, can be modified by the user. */
    sctp_conn_t user_ctx;
} sctp_conn_priv_t;

static sctp_conn_priv_t * nonnull
sctp_conn_priv_init(sctp_conn_priv_t * nonnull conn)
{
    p_clear(conn, 1);

    sb_init(&conn->rbuf);
    dlist_init(&conn->msgs);
    return conn;
}
GENERIC_NEW(sctp_conn_priv_t, sctp_conn_priv);

static void sctp_conn_priv_wipe(sctp_conn_priv_t * nonnull conn)
{
    sb_wipe(&conn->rbuf);
    sctp_msg_list_wipe(&conn->msgs);
    lstr_wipe(&(conn->user_ctx.entity_id));
    lstr_wipe(&(conn->user_ctx.host));
    if (!conn->is_listening) {
        /* XXX: The context created by sctp_listen() doesn't use a specific
         * logger but use the global logger instead, do not remove it! */
        logger_delete(&conn->user_ctx.logger);
    }
}
GENERIC_DELETE(sctp_conn_priv_t, sctp_conn_priv);

#define GET_PRIV_CONN(pub_ctx_p) \
    container_of(pub_ctx_p, sctp_conn_priv_t, user_ctx)

/* }}} */
/* {{{ Globals */

static void sctp_conn_close_priv(sctp_conn_priv_t **pconn);

/* Main logger, used when the current context is not associated to an active
 * connection. If it is it should use the logger created specifically for
 * it. */
logger_t sctp_logger_g = LOGGER_INIT_INHERITS(NULL, "sctp");

/* }}} */
/* {{{ Utils */

static lstr_t t_sus_to_str(const qv_t(sctp_su) *sus)
{
    t_SB_1k(sb);

    tab_for_each_ptr(su, sus) {
        if (sb.len) {
            sb_adds(&sb, ", ");
        }
        sb_add_lstr(&sb, t_addr_fmt_lstr(su));
    }

    return LSTR_SB_V(&sb);
}

static int
addr_list_to_su(qv_t(lstr) addrs, uint16_t defport, qv_t(sctp_su) *out)
{
    tab_for_each_entry(name, &addrs) {
        pstream_t addr = ps_initlstr(&name);
        pstream_t host;
        in_port_t port;
        sockunion_t su;

        ps_trim(&addr);
        RETHROW(addr_parse(addr, &host, &port, defport));
        RETHROW(addr_info(&su, AF_UNSPEC, host, port));
        qv_append(out, su);
    }

    return 0;
}

static sockunion_t *t_sctp_sus_array(const qv_t(sctp_su) *sus,
                                     int * nullable olen)
{
    sockunion_t *res = t_new_raw(sockunion_t, sus->len);
    size_t len = 0;
    size_t sl;

    tab_for_each_ptr(su, sus) {
        sl = sockunion_len(su);
        memcpy((byte *)res + len, su, sl);
        len += sl;
    }
    if (olen) {
        *olen = len;
    }
    return res;
}

/* }}} */
/* {{{ Internal */

static int
sctp_ep_init_sock(int fd, size_t sndbuf)
{
    int nodelay = 1;

    assert(fd >= 0);

    RETHROW(sctp_enable_events(fd, SCTP_DATA_IO_EV | SCTP_ASSOCIATION_EV |
                               SCTP_SEND_FAILURE_EV | SCTP_PEER_ERROR_EV |
                               SCTP_ADDRESS_EV | SCTP_SHUTDOWN_EV));

    RETHROW(setsockopt(fd, IPPROTO_SCTP, SCTP_NODELAY, &nodelay,
                       sizeof(nodelay)));
    RETHROW(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)));
    return 0;
}

static int sctp_send_queued_msgs(sctp_conn_priv_t **pconn)
{
    sctp_conn_priv_t *conn = *pconn;
    int      fd            = el_fd_get_fd(conn->evh);
    uint16_t stream_no     = 0;
    int      msg_flags     = 0;
    int      len;

    dlist_for_each_entry(sctp_msg_t, msg, &conn->msgs, msg_list) {
        errno = 0;
        len = sctp_sendmsg(fd, msg->msg.data, msg->msg.len, NULL, 0,
                           htonl(msg->ppid), msg_flags, stream_no, 0, 0);
        if (len < 0) {
            if (!ERR_RW_RETRIABLE(errno)) {
                logger_error(conn->user_ctx.logger,
                             "SCTP send message to fd: %d failed: %m", fd);
                sctp_conn_close_priv(pconn);
                return -1;
            }
            return 0;
        }

        logger_trace(conn->user_ctx.logger, 2,
                     "send message to %d succeed (%d bytes)",
                     fd, len);
        sctp_msg_delete(&msg);
    }
    el_fd_set_mask(conn->evh, POLLIN);
    return 0;
}

/* return -1 if we closed the connection, 0 otherwise */
static int sctp_handle_notification(sctp_conn_priv_t **pconn)
{
    sctp_conn_priv_t *conn = *pconn;
    const union sctp_notification *sn =
        (union sctp_notification *)conn->rbuf.data;

    if (conn->rbuf.len < ssizeof(sn->sn_header)) {
        logger_error(conn->user_ctx.logger, "invalid NOTIF: len = %d < %zd",
                     conn->rbuf.len, sizeof(sn->sn_header));
        return 0;
    }

    switch (sn->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE: {
        const struct sctp_assoc_change *sac = &sn->sn_assoc_change;

        logger_trace(conn->user_ctx.logger, 2, "got notif ASSOC_CHANGE with "
                     "state:%d error:%d sac:%d os:%d is:%d",
                     sac->sac_state, sac->sac_error, sac->sac_assoc_id,
                     sac->sac_outbound_streams,
                     sac->sac_inbound_streams);

        switch (sac->sac_state) {
        case SCTP_COMM_UP:
        case SCTP_RESTART:
            break;

        case SCTP_COMM_LOST:
        case SCTP_SHUTDOWN_COMP:
        case SCTP_CANT_STR_ASSOC:
            logger_trace(conn->user_ctx.logger, 2,
                         "SCTP notification (shutdown or lost)");
            sctp_conn_close_priv(pconn);
            return -1;
        }
    } break;

    case SCTP_SEND_FAILED: {
        const struct sctp_send_failed *ssf = &sn->sn_send_failed;

        logger_error(conn->user_ctx.logger,
                     "got send failed error %d with peer `%pL'",
                     ssf->ssf_error, &conn->user_ctx.entity_id);
    } break;

    case SCTP_REMOTE_ERROR: {
        const struct sctp_remote_error *sre = &sn->sn_remote_error;

        logger_error(conn->user_ctx.logger,
                     "got remote error %d with peer `%pL'",
                     sre->sre_error, &conn->user_ctx.entity_id);
    } break;

    case SCTP_PEER_ADDR_CHANGE: {
        const struct sctp_paddr_change *spc = &sn->sn_paddr_change;

        logger_trace(conn->user_ctx.logger, 2,
                     "got notif SCTP_PEER_ADDR_CHANGE with "
                     "state:%d error:%d assoc_id:%d",
                     spc->spc_state, spc->spc_error, spc->spc_assoc_id);
    } break;

    default:
        logger_trace(conn->user_ctx.logger, 2,
                     "got notif %d", sn->sn_header.sn_type);
        break;
    }

    return 0;
}

/* return:
 * -1 on error (connection closed)
 * 0 for RETRIABLE error
 * sctp_msg_type_t on a valid message
 */
static int sctp_receive_full_msg(sctp_conn_priv_t **pconn)
{
    struct sctp_sndrcvinfo sinfo;
    sctp_conn_priv_t *conn = *pconn;
    int len;
    int fd = el_fd_get_fd(conn->evh);
    sb_t *ibuf = &conn->rbuf;
    int msg_flags = 0;

    if (conn->reset_rbuf) {
        sb_reset(ibuf);
        conn->reset_rbuf = false;
    }
    p_clear(&sinfo, 1);

    /* support partial delivery : loop until we get the flag MSG_EOR which
     * indicates the end of record */
    for (;;) {
        errno = 0;
        len = sctp_recvmsg(fd, sb_grow(ibuf, BUFSIZ), BUFSIZ, NULL, NULL,
                           &sinfo, &msg_flags);
        if (len < 0) {
            __sb_fixlen(ibuf, ibuf->len);
            if (ERR_RW_RETRIABLE(errno)) {
                return 0;
            }
            logger_error(conn->user_ctx.logger, "recvmsg error %m");
            sctp_conn_close_priv(pconn);
            return -1;
        }
        __sb_fixlen(ibuf, ibuf->len + len);

        if (msg_flags & MSG_EOR) {
            break;
        }
    }

    if (msg_flags & MSG_NOTIFICATION) {
        conn->reset_rbuf = true;
        return SCTP_MSG_TYPE_NOTIF;
    }

    if (unlikely(msg_flags & MSG_CTRUNC)) {
        logger_error(conn->user_ctx.logger, "ancillary data truncated, "
                     "increase /proc/sys/net/core/optmem_max");
    } else if (sinfo.sinfo_flags & (SCTP_EOF | SCTP_ABORT)) {
        logger_error(conn->user_ctx.logger, "SCTP close request fd %d", fd);
        sctp_conn_close_priv(pconn);
        return -1;
    }

    logger_trace(conn->user_ctx.logger, 2,
                 "SCTP received %d bytes from fd: %d",
                 ibuf->len, fd);
    conn->reset_rbuf = true;

    return SCTP_MSG_TYPE_DATA;
}

static int sctp_conn_on_event_in(sctp_conn_priv_t **pconn)
{
    sctp_conn_priv_t *conn = *pconn;
    bool     data_received = false;

    for (;;) {
        int res;

        res = RETHROW(sctp_receive_full_msg(pconn));
        if (res == 0) {
            break;
        }

        switch ((sctp_msg_type_t)res) {
        case SCTP_MSG_TYPE_NOTIF:
            RETHROW(sctp_handle_notification(pconn));
            break;
        case SCTP_MSG_TYPE_DATA:
            data_received = true;
            if (conn->on_data_cb(&conn->user_ctx, &conn->rbuf, false) < 0) {
                logger_warning(conn->user_ctx.logger,
                               "user on_data function returned an error");
                goto error;
            }
            break;
        }
    }

    if (data_received) {
        if (conn->on_data_cb(&conn->user_ctx, NULL, true) < 0) {
            logger_warning(conn->user_ctx.logger,
                           "user on_data fn returned an error");
            goto error;
        }
    }

    return 0;
error:
    sctp_conn_close_priv(pconn);
    return -1;
}

static int sctp_conn_on_event(el_t evh, int fd, short ev, el_data_t priv)
{
    sctp_conn_priv_t *conn = priv.ptr;

    if (ev & POLLIN) {
        RETHROW(sctp_conn_on_event_in(&conn));
    }

    if (ev & POLLOUT) {
        RETHROW(sctp_send_queued_msgs(&conn));
    }

    if (ev & POLLHUP) {
        logger_info(conn->user_ctx.logger, "got POLLHUP, closing connection");
        sctp_conn_close_priv(&conn);
        return -1;
    }

    return 0;
}

static int sctp_connecting(el_t evh, int fd, short ev, el_data_t priv)
{
    sctp_conn_priv_t *conn = priv.ptr;
    int               ret;
    int               err  = 0;
    socklen_t         len  = sizeof(err);

    ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &len);
    if (ret < 0 || err) {
        logger_trace(conn->user_ctx.logger, 4,
                     "error returned while connecting. Using error callback");
        if (len != sizeof(err)) {
            logger_warning(conn->user_ctx.logger,
                           "unexpected error code length (%d != %ld)",
                           len, sizeof(err));
        }
        conn->on_connect_cb(&conn->user_ctx, false, err);
        sctp_conn_close_priv(&conn);
        return -1;
    }

    if (conn->on_connect_cb(&conn->user_ctx, true, 0) < 0) {
        logger_error(conn->user_ctx.logger,
                     "user on_connect_cb returned an error");
        sctp_conn_close_priv(&conn);
        return -1;
    }

    el_fd_set_hook(conn->evh, sctp_conn_on_event);

    return 0;
}

static int sctp_srv_on_event(el_t evh, int fd, short ev, el_data_t priv)
{
    int sock;
    sctp_conn_priv_t *listen_ctx = priv.ptr;
    sctp_conn_t      *user_ctx   = &listen_ctx->user_ctx;

    while ((sock = acceptx(fd, O_NONBLOCK)) >= 0) {
        t_scope;
        sctp_conn_priv_t *conn;
        int ret;
        sockunion_t local = {.family = AF_INET};
        socklen_t size = sockunion_len(&local);
        char host[NI_MAXHOST];
        char serv[NI_MAXSERV];
        int port;
        logger_t *logger;
        lstr_t logger_name;

        logger_name = t_lstr_cat3(user_ctx->entity_id,
                                  LSTR("-"),
                                  t_lstr_fmt("%d", sock));

        logger = logger_new(&sctp_logger_g, logger_name,
                            LOG_INHERITS, 0);

        if (getpeername(sock, &local.sa, &size) < 0) {
            logger_error(logger, "cannot get peer name for `%*pM': %m",
                         LSTR_FMT_ARG(user_ctx->entity_id));
            close(sock);
            logger_delete(&logger);
            return -1;
        }

        if ((ret = getnameinfo(&local.sa, size, host, NI_MAXHOST,
                               serv, NI_MAXSERV,
                               NI_NUMERICHOST | NI_NUMERICSERV)) != 0)
        {
            logger_error(logger,
                         "cannot get peer informations for `%*pM': %s",
                         LSTR_FMT_ARG(user_ctx->entity_id),
                         gai_strerror(ret));
            close(sock);
            logger_delete(&logger);
            return -1;
        }

        port = atoi(serv);
        logger_info(logger, "connection for `%*pM' from %s:%d",
                    LSTR_FMT_ARG(user_ctx->entity_id),
                    host, port);


        conn = sctp_conn_priv_new();
        conn->evh                  = el_fd_register(sock, true, POLLIN,
                                                    sctp_conn_on_event, conn);
        conn->is_listening         = false;
        conn->on_disconnect_cb     = listen_ctx->on_disconnect_cb;
        conn->on_data_cb           = listen_ctx->on_data_cb;
        conn->user_ctx.entity_id   = lstr_dup(user_ctx->entity_id);
        conn->user_ctx.priv        = user_ctx->priv;
        conn->user_ctx.logger      = logger;
        conn->user_ctx.host        = lstr_dup(LSTR(host));
        conn->user_ctx.port        = port;

        listen_ctx->on_accept_cb(&conn->user_ctx);
    }

    return 0;
}

static void sctp_conn_close_priv(sctp_conn_priv_t **pconn)
{
    sctp_conn_priv_t *conn = *pconn;

    logger_info(conn->user_ctx.logger, "closing connection with %pL",
                &conn->user_ctx.entity_id);

    el_unregister(&conn->evh);

    logger_trace(conn->user_ctx.logger, 4, "connection closed");
    if (conn->on_disconnect_cb) {
        conn->on_disconnect_cb(&conn->user_ctx);
    }

    sctp_conn_priv_delete(pconn);
}

/* }}} */
/* {{{ High level functions */

sctp_conn_t *sctp_connect(qv_t(lstr) source_addrs, qv_t(lstr) dest_addrs,
                          uint16_t port, size_t sndbuf, lstr_t entity_id,
                          void *priv,
                          sctp_on_connect_f on_connect_cb,
                          sctp_on_data_f on_data_cb,
                          sctp_on_disconnect_f on_disconnect_cb)
{
    t_scope;
    sctp_conn_priv_t *conn;
    sockunion_t      *sus_array;
    logger_t         *logger = &sctp_logger_g;
    int               fd;
    int               id = -1;
    qv_t(sctp_su)     sus;
    SB_1k(errbuf);

    assert(on_connect_cb);
    assert(on_data_cb);

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
        logger_error(logger, "unable to create socket %m");
        return NULL;
    }

    t_qv_init(&sus, 8);
    if (source_addrs.len) {
        lstr_t s;

        tab_for_each_entry(addr, &source_addrs) {
            sockunion_t su;

            if (addr_source_resolve("sourceAddrs", addr, &su, &errbuf) < 0) {
                logger_error(logger, "%*pM", SB_FMT_ARG(&errbuf));
                close(fd);
                return NULL;
            }
            qv_append(&sus, su);
        }

        s = t_sus_to_str(&sus);
        sus_array = t_sctp_sus_array(&sus, NULL);
        if (bindx(fd, sus_array, sus.len, SOCK_SEQPACKET, IPPROTO_SCTP,
                  O_NONBLOCK) < 0)
        {
            logger_error(logger, "SCTP bind on address(es) %pL failed: %m",
                         &s);
            close(fd);
            return NULL;
        }
        logger_info(logger, "outgoing connection: bind on `%pL` succeed "
                    "for %pL", &s, &entity_id);
        qv_clear(&sus);
    }

    if (addr_list_to_su(dest_addrs, port, &sus) < 0) {
        logger_error(logger, "failed to get sockunions");
        goto close_sock;
    }

    if (sctp_ep_init_sock(fd, sndbuf) < 0) {
        logger_error(logger, "set SCTP init message failed %m");
        goto close_sock;
    }
    fd_set_features(fd, O_NONBLOCK);

    sus_array = t_sctp_sus_array(&sus, NULL);
    if (sctp_connectx_ng(fd, (void *)sus_array, sus.len, &id) < 0 &&
        !ERR_CONNECT_RETRIABLE(errno))
    {
        logger_error(logger, "SCTP connectx error: %m");
        goto close_sock;
    }

    conn = sctp_conn_priv_new();
    conn->evh = el_fd_register(fd, true, POLLIN, sctp_connecting, conn);
    conn->is_listening               = false;
    conn->on_connect_cb              = on_connect_cb;
    conn->on_disconnect_cb           = on_disconnect_cb;
    conn->on_data_cb                 = on_data_cb;
    conn->user_ctx.entity_id         = lstr_dup(entity_id);
    conn->user_ctx.priv              = priv;
    conn->user_ctx.logger            = logger_new(&sctp_logger_g, entity_id,
                                                  LOG_INHERITS, 0);

    el_unref(conn->evh);

    return &conn->user_ctx;

close_sock:
    p_close(&fd);
    return NULL;
}

sctp_conn_t *sctp_listen(const lstr__array_t *addrs, int port, size_t sndbuf,
                         lstr_t entity_id, void *priv,
                         sctp_on_accept_f on_accept_cb,
                         sctp_on_data_f on_data_cb,
                         sctp_on_disconnect_f on_disconnect_cb)
{
    t_scope;
    int               fd;
    lstr_t            sus_str;
    qv_t(sctp_su)     sus;
    logger_t         *logger = &sctp_logger_g;
    sctp_conn_priv_t *conn;
    SB_1k(err);

    t_qv_init(&sus, 8);
    tab_for_each_entry(addr, addrs) {
        sockunion_t su;

        sb_reset(&err);
        if (addr_resolve2("addrs", addr, 0, port, &su, NULL, NULL, &err) < 0)
        {
            logger_error(logger, "invalid address: %*pM", SB_FMT_ARG(&err));
            return NULL;
        }
        qv_append(&sus, su);
    }

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
        logger_error(logger, "unable to create socket %m");
        return NULL;
    }

    sus_str = t_sus_to_str(&sus);

    if (bindx(fd, t_sctp_sus_array(&sus, NULL), sus.len, SOCK_SEQPACKET,
              IPPROTO_SCTP, O_NONBLOCK) < 0)
    {
        logger_error(logger, "SCTP bind on address %*pM failed: %m",
                     LSTR_FMT_ARG(sus_str));
        close(fd);
        return NULL;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        logger_error(logger, "SCTP listen on address %*pM failed: %m",
                     LSTR_FMT_ARG(sus_str));
        close(fd);
        return NULL;
    }

    if (sctp_ep_init_sock(fd, sndbuf) < 0) {
        logger_error(logger, "set SCTP init message failed %m");
        close(fd);
        return NULL;
    }

    logger_info(logger, "listening on SCTP address(es): %*pM",
                LSTR_FMT_ARG(sus_str));

    conn = sctp_conn_priv_new();
    conn->evh                    = el_fd_register(fd, true, POLLIN,
                                                  sctp_srv_on_event, conn);;
    conn->is_listening           = true;
    conn->user_ctx.entity_id     = lstr_dup(entity_id);
    conn->user_ctx.priv          = priv;
    conn->user_ctx.logger        = logger;
    conn->on_accept_cb           = on_accept_cb;
    conn->on_data_cb             = on_data_cb;
    conn->on_disconnect_cb       = on_disconnect_cb;

    return &conn->user_ctx;
}

void sctp_send_msg(sctp_conn_t *pub_ctx, lstr_t payload,
                   uint32_t payload_protocol_id)
{
    sctp_conn_priv_t *conn = NULL;
    sctp_msg_t *msg;

    if (!pub_ctx) {
        logger_error(&sctp_logger_g, "invalid argument pub_ctx");
        assert(false);
        return;
    }

    conn = GET_PRIV_CONN(pub_ctx);

    if (conn->is_listening) {
        logger_error(conn->user_ctx.logger,
                     "this context is only for accepting new connections, "
                     "it cannot send messages!");
        assert(false);
        return;
    }

    msg = sctp_msg_new();
    msg->ppid = payload_protocol_id;
    msg->msg = lstr_dup(payload);

    dlist_add_tail(&conn->msgs, &msg->msg_list);

    el_fd_set_mask(conn->evh, POLLINOUT);
}

void sctp_conn_close(sctp_conn_t **ppub_ctx)
{
    sctp_conn_t *pub_ctx;
    sctp_conn_priv_t *conn = NULL;

    if (!ppub_ctx || !(*ppub_ctx)) {
        logger_error(&sctp_logger_g, "invalid argument ppub_ctx");
        return;
    }

    pub_ctx = *ppub_ctx;
    conn = GET_PRIV_CONN(pub_ctx);
    sctp_conn_close_priv(&conn);
    *ppub_ctx = NULL;
}

/* }}} */
