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

#include <lib-common/log.h>
#include <lib-common/iop-json.h>
#include <lib-common/iop-rpc.h>
#include <lib-common/datetime.h>


static struct {
    logger_t logger;
} iop_http_g = {
#define _G  iop_http_g
    .logger = LOGGER_INIT_INHERITS(NULL, "rpc-http-client")
};

/* {{{ HTTP client */

void http_iop_channel_close_clients(http_iop_channel_t *channel)
{
    tab_for_each_entry(remote, &channel->remotes) {
        httpc_pool_close_clients(&remote->pool);
    }
}

/* }}} */
/* {{{ HTTP IOP query */

static http_iop_msg_t *http_iop_msg_init(http_iop_msg_t *query)
{
    p_clear(query, 1);
    httpc_query_init(&query->query);

    return query;
}

static void http_iop_msg_wipe(http_iop_msg_t *query)
{
    httpc_query_wipe(&query->query);
    p_delete(&query->args);
}

http_iop_msg_t *http_iop_msg_new(int len)
{
    http_iop_msg_t *msg;

    assert (len >= 0);

    msg = p_new_extra(http_iop_msg_t, len);
    return http_iop_msg_init(msg);
}

GENERIC_DELETE(http_iop_msg_t, http_iop_msg);

http_iop_channel_t *http_iop_channel_init(http_iop_channel_t *channel)
{
    p_clear(channel, 1);
    qv_init(&channel->remotes);
    htlist_init(&channel->queries);

    return channel;
}
void http_iop_channel_wipe(http_iop_channel_t *channel)
{
    lstr_wipe(&channel->user);
    lstr_wipe(&channel->password);
    qv_deep_wipe(&channel->remotes, http_iop_channel_remote_delete);
    htlist_deep_clear(&channel->queries, http_iop_msg_t, link,
                      http_iop_msg_delete);
    el_unregister(&channel->timeout_queries_el);
}

static void
on_connection_ready(httpc_pool_t *pool, httpc_t *httpc)
{
    http_iop_channel_remote_t *remote;
    htlist_t temp;

    remote = container_of(pool, http_iop_channel_remote_t, pool);
    htlist_move(&temp, &remote->channel->queries);
    while (!htlist_is_empty(&temp)) {
        http_iop_msg_t *msg;

        msg = htlist_pop_entry(&temp, http_iop_msg_t, link);
        http_iop_query_(remote->channel, msg, NULL);
        if (!httpc_pool_can_query(pool)) {
            htlist_splice(&remote->channel->queries, &temp);
            return;
        }
    }
    if (httpc_pool_has_ready(pool) && remote->channel->on_ready_cb) {
        (*remote->channel->on_ready_cb)(remote->channel);
    }
}

static void on_connect_error(const struct httpc_t *httpc, int errnum)
{
    http_iop_channel_remote_t *remote;

    remote = container_of(httpc->pool, http_iop_channel_remote_t, pool);

    if (remote->channel->on_connection_error_cb) {
        (*remote->channel->on_connection_error_cb)(remote, errnum);
    }
}

http_iop_channel_remote_t *
http_iop_channel_remote_init(http_iop_channel_remote_t *remote)
{
    p_clear(remote, 1);
    httpc_pool_init(&remote->pool);
    remote->pool.cfg = httpc_cfg_new();

    return remote;
}

void http_iop_channel_remote_wipe(http_iop_channel_remote_t *remote)
{
    httpc_pool_wipe(&remote->pool, true);
}

http_iop_channel_t *
http_iop_channel_create(const http_iop_channel_cfg_t *cfg, sb_t *err)
{
    http_iop_channel_t *res = http_iop_channel_new();

    res->query_timeout = OPT_DEFVAL(cfg->query_timeout, 10);
    res->response_max_size = OPT_DEFVAL(cfg->response_max_size, 1 << 20);
    res->encode_url = OPT_DEFVAL(cfg->encode_url, true);
    res->user = lstr_dup(cfg->user);
    res->password = lstr_dup(cfg->password);
    res->on_connection_error_cb = cfg->on_connection_error_cb;
    res->on_ready_cb = cfg->on_ready_cb;
    res->priv = cfg->priv;

    if (!cfg->urls.len) {
        sb_setf(err, "there must be at least one URL");
        goto error;
    }

    tab_for_each_entry(url, &cfg->urls) {
        http_iop_channel_remote_t *remote;

        remote = http_iop_channel_remote_new();
        if (parse_http_url(url.s, true, &remote->url) < 0 ||
            addr_info_str(&remote->pool.su, remote->url.host,
                          remote->url.port, AF_UNSPEC) < 0)
        {
            sb_setf(err, "cannot parse URL `%*pM`", LSTR_FMT_ARG(url));
            http_iop_channel_remote_delete(&remote);
            goto error;
        }

        remote->pool.host = lstr_fmt("%s:%d", remote->url.host,
                                     remote->url.port);
        remote->base_path = LSTR(remote->url.path_without_args);
        if (httpc_cfg_from_iop(remote->pool.cfg, cfg->iop_cfg) < 0) {
            sb_sets(err, "cannot create channel from IOP configuration");
            lstr_wipe(&remote->pool.host);
            http_iop_channel_remote_delete(&remote);
            goto error;
        }

        remote->pool.max_len = OPT_DEFVAL(cfg->max_conn, 1);
        remote->pool.on_ready = on_connection_ready;
        remote->pool.on_connect_error = on_connect_error;
        remote->channel = res;

        qv_append(&res->remotes, remote);
    }

    return res;

error:
    http_iop_channel_delete(&res);
    return NULL;
}

static void http_iop_register_timeout_check(http_iop_channel_t *channel,
                                            uint16_t timeout);

static void http_iop_timeout_queries(el_t el, data_t data)
{
    http_iop_channel_t *channel = data.ptr;
    uint32_t next_check = UINT32_MAX;
    time_t now = lp_getsec();

    while (!htlist_is_empty(&channel->queries)) {
        http_iop_msg_t *msg;

        msg = htlist_first_entry(&channel->queries, http_iop_msg_t, link);
        if (msg->query_time + channel->query_timeout < now) {
            logger_trace(&_G.logger, 1, "canceling query `%*pM`: timeout "
                         "reached", LSTR_FMT_ARG(msg->rpc->name));
            msg->cb(msg, IC_MSG_CANCELED, (opt_http_code_t)OPT_NONE, NULL,
                    NULL);
            htlist_pop_entry(&channel->queries, http_iop_msg_t, link);
            http_iop_msg_delete(&msg);
        } else {
            next_check = MIN(lp_getsec() - msg->query_time, next_check);
            break;
        }
    }
    if (!htlist_is_empty(&channel->queries)) {
        http_iop_register_timeout_check(channel, next_check);
    } else {
        channel->timeout_queries_el = NULL;
    }
}

static void http_iop_register_timeout_check(http_iop_channel_t *channel,
                                            uint16_t timeout)
{
    channel->timeout_queries_el = el_unref(
        el_timer_register(timeout * 1000, 0, 0, &http_iop_timeout_queries,
                          channel)
    );
}

static inline bool http_code_is_successful(http_code_t code)
{
    return code >= HTTP_CODE_OK && code < HTTP_CODE_MULTIPLE_CHOICES;
}

static ic_status_t ic_status_from_httpc_status(httpc_status_t status)
{
    switch (status) {
    case HTTPC_STATUS_OK:
        return IC_MSG_OK;

    case HTTPC_STATUS_EXP100CONT:
        /* This does not seem possible to have this code here. This is only
         * set if the httpc_query has set expect100cont (which is not the
         * case in this module.
         */
        assert(false);
        /* FALLTHROUGH */

    case HTTPC_STATUS_INVALID:
        return IC_MSG_SERVER_ERROR;

    case HTTPC_STATUS_ABORT:
        return IC_MSG_ABORT;

    case HTTPC_STATUS_TOOLARGE:
        return IC_MSG_CANCELED;

    case HTTPC_STATUS_TIMEOUT:
        return IC_MSG_TIMEDOUT;
    }

    return IC_MSG_INVALID;
}

#define CONTENT_TYPE_JSON "application/json"

static void
http_iop_on_query_done(httpc_query_t *http_query, httpc_status_t httpc_status)
{
    t_scope;
    http_iop_msg_t *msg;
    opt_http_code_t http_code = OPT_NONE;
    void *exn = NULL;
    void *res = NULL;
    ic_status_t ic_status;
    SB_1k(err);

    msg = container_of(http_query, http_iop_msg_t, query);

    logger_trace(&_G.logger, 1, "query `%*pM` finished (%d)",
                 LSTR_FMT_ARG(msg->rpc->name), httpc_status);
    if (http_query->qinfo) {
        logger_trace(&_G.logger, 2, "query `%*pM` HTTP code: %d",
                     LSTR_FMT_ARG(msg->rpc->name), http_query->qinfo->code);
        logger_trace(&_G.logger, 3, "payload: `%*pM`",
                     SB_FMT_ARG(&http_query->payload));
        OPT_SET(http_code, http_query->qinfo->code);
    }

    ic_status = ic_status_from_httpc_status(httpc_status);
    if (http_query->payload.len) {
        pstream_t ps = ps_initsb(&http_query->payload);
        const iop_struct_t *st = msg->rpc->result;
        void **dest = &res;
        bool content_type_json = false;
        lstr_t content_type = LSTR_NULL_V;
        const http_qhdr_t *ctype;

        if (!http_code_is_successful(http_query->qinfo->code)) {
            st = msg->rpc->exn;
            dest = &exn;
            ic_status = IC_MSG_EXN;
        }
        ctype = http_qhdr_find(http_query->qinfo->hdrs,
                               http_query->qinfo->hdrs_len,
                               HTTP_WKHDR_CONTENT_TYPE);
        if (ctype) {
            pstream_t v = ctype->val;

            /* TODO: should be factorized with is_ctype_json. */
            ps_skipspaces(&v);
            content_type_json = ps_startswithstr(&ctype->val,
                                                 CONTENT_TYPE_JSON);
            content_type = LSTR_PS_V(&ctype->val);
        }

        if (content_type_json) {
            if (t_iop_junpack_ptr_ps(&ps, st, dest, 0, &err) < 0) {
                logger_error(&_G.logger, "cannot unpack result of query "
                             "`%*pM`: %*pM", LSTR_FMT_ARG(msg->rpc->name),
                             SB_FMT_ARG(&err));
                ic_status = IC_MSG_INVALID;
                *dest = NULL;
            }
        } else {
            ic_status = IC_MSG_INVALID;
            logger_error(&_G.logger, "invalid or missing content-type "
                         "received from server for query `%*pM`: `%*pM` "
                         "(code %d)",
                         LSTR_FMT_ARG(msg->rpc->name),
                         LSTR_FMT_ARG(content_type),
                         http_query->qinfo->code);
        }
    } else if (httpc_status == HTTPC_STATUS_OK && http_query->qinfo &&
               http_code_is_successful(http_query->qinfo->code))
    {
        ic_status = IC_MSG_INVALID;
        logger_error(&_G.logger, "invalid reply from server, empty payload "
                     "for a succesful query");
    }

    msg->cb(msg, ic_status, http_code, res, exn);

    http_iop_msg_delete(&msg);
}

void http_iop_query_(http_iop_channel_t *channel, http_iop_msg_t *msg,
                     void *args)
{
    httpc_t *w = NULL;
    outbuf_t *ob;
    http_iop_channel_remote_t *remote;
    SB_1k(sb);
    SB_1k(query_data);
    void *query_args = args ? args : msg->args;

    tab_for_each_entry(r, &channel->remotes) {
        if ((w = httpc_pool_get(&r->pool))) {
            remote = r;
            break;
        }
    }
    if (!msg->query_time) {
        msg->query_time = lp_getsec();
    }
    if (!w) {
        logger_trace(&_G.logger, 1,
                     "no connection ready, query `%*pM` will be retried",
                     LSTR_FMT_ARG(msg->rpc->name));
        if (!msg->args) {
            msg->args = mp_iop_dup_desc_flags_sz(NULL, msg->rpc->args, args,
                                                 0, NULL);
        }
        htlist_add_tail(&channel->queries, &msg->link);
        if (!channel->timeout_queries_el) {
            http_iop_register_timeout_check(channel, channel->query_timeout);
        }
        return;
    }

    if (channel->response_max_size) {
        httpc_bufferize(&msg->query, channel->response_max_size);
    }
    msg->query.on_done = http_iop_on_query_done;
    httpc_query_attach(&msg->query, w);
    if (channel->encode_url) {
        sb_add_urlencode(&sb, remote->base_path.s, remote->base_path.len);
        sb_addc(&sb, '/');
        sb_add_urlencode(&sb, msg->iface_alias->name.s,
                         msg->iface_alias->name.len);
        sb_addc(&sb, '/');
        sb_add_urlencode(&sb, msg->rpc->name.s, msg->rpc->name.len);
    } else {
        sb_addf(&sb, "/%*pM/%*pM/%*pM", LSTR_FMT_ARG(remote->base_path),
                LSTR_FMT_ARG(msg->iface_alias->name),
                LSTR_FMT_ARG(msg->rpc->name));
    }

    httpc_query_start_flags(&msg->query, HTTP_METHOD_POST,
                            remote->pool.host, LSTR_SB_V(&sb), false);

    if (channel->user.len && channel->password.len) {
        httpc_query_hdrs_add_auth(&msg->query, channel->user,
                                  channel->password);
    }

    ob = httpc_get_ob(&msg->query);
    ob_adds(ob, "Content-Type: application/json\r\n");
    httpc_query_hdrs_done(&msg->query, -1, false);
    iop_sb_jpack(&query_data, msg->rpc->args, query_args, 0);
    ob_addsb(ob, &query_data);

    logger_trace(&_G.logger, 1, "%*pM/%*pM: `%*pM`",
                 LSTR_FMT_ARG(msg->iface_alias->name),
                 LSTR_FMT_ARG(msg->rpc->name), SB_FMT_ARG(&query_data));
    httpc_query_done(&msg->query);
}

/* }}} */