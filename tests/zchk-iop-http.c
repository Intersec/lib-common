/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <lib-common/z.h>
#include <lib-common/unix.h>
#include <lib-common/iop-rpc.h>

#include "iop/tstiop.iop.h"

static struct {
    http_mode_t http_mode;

    httpd_trigger__ic_t *itcb;
    el_t server;

    http_iop_channel_t *client_channel;

    int response_time;
    uint64_t async_slot;
    int async_arg_i;

    int val_received;
    int val_answered;

    bool query_sent;
    bool query_answered;
    int query_conn_error;
    ic_status__t query_status;
    http_code_t query_code;

    bool query_http_header;
    bool resp_http_header;

    /* for el_wait_until */
    bool el_wait_timed_out;
} z_iop_http_g;
#define _G z_iop_http_g

#define HTTP_TEST_NOACT_DELAY 20 /* msecs */

static void z_iop_http_el_wait(el_t ev, data_t data)
{
    _G.el_wait_timed_out = true;
}

#define el_wait_until(cond, timeout)                                         \
    do {                                                                     \
        el_t __el_tmr = el_timer_register(                                   \
            timeout, EL_TIMER_LOWRES, 0, &z_iop_http_el_wait, NULL           \
        );                                                                   \
                                                                             \
        _G.el_wait_timed_out = false;                                        \
        while (!((cond) || _G.el_wait_timed_out)) {                          \
            el_loop_timeout(timeout);                                        \
        }                                                                    \
        el_unregister(&__el_tmr);                                            \
    } while (false)

/* {{{ Tests */

static void
z_iop_httpc_on_connection_error(http_iop_channel_remote_t *remote, int errnum)
{
    _G.query_conn_error = errnum;
}

static void z_iop_httpc_on_ready(http_iop_channel_t *channel)
{
    /* Never actually called in our case because for HTT1X httpc connection is
     * used right away and never turns ready. */
}

static int z_iop_httpd_on_query_done(
    const httpd_trigger__ic_t *nonnull tcb, ichttp_query_t *nonnull iq
)
{
    const http_qhdr_t *qhdr;

    qhdr = http_qhdr_find_from_key(
        iq->qinfo->hdrs, iq->qinfo->hdrs_len, LSTR("X-ZCHK-IOP-HTTP-QUERY")
    );
    if (!qhdr || !lstr_equal(LSTR_PS_V(&qhdr->val), LSTR("1"))) {
        httpd_reject(
            &iq->super, BAD_REQUEST, "Missing X-ZCHK-IOP-HTTP-QUERY header"
        );
        return -1;
    }

    _G.query_http_header = true;
    return 0;
}

static void z_iop_httpd_on_reply_http_headers(
    const httpd_trigger__ic_t *nonnull tcb, ichttp_query_t *nonnull iq,
    http_code_t res_code
)
{
    outbuf_t *ob = httpd_get_ob(&iq->super);

    ob_adds(ob, "X-ZCHK-IOP-HTTP-RESP: 99\r\n");
}

static void z_iop_httpd_pre_hook(
    ichannel_t *nullable channel, uint64_t slot, ic__hdr__t *nullable hdr,
    data_t data, bool *nonnull hdr_modified
)
{
    /* XXX: Required. See t_ic_query_do_pre_hook(). */
    ic_hook_ctx_new(slot, 0);
}

static void z_iop_httpd_post_hook(
    ichannel_t *nullable channel, ic_status__t status,
    ic_hook_ctx_t *nonnull ctx, data_t data, const iop_struct_t *nullable st,
    const void *nullable value
)
{
    assert(st == &tstiop__iface__f_res__s);
    assert(((tstiop__iface__f_res__t *)value)->i > 0);
}

static void z_iop_http_reply(uint64_t slot, int arg_i)
{
    ic_reply(NULL, slot, tstiop__t, iface, f, .i = arg_i * 2);
}

static void z_iop_http_async_reply(el_t ev, data_t data)
{
    z_iop_http_reply(_G.async_slot, _G.async_arg_i);
}

static void IOP_RPC_IMPL(tstiop__t, iface, f)
{
    _G.val_received = arg->i;

    if (_G.response_time >= 0) {
        _G.async_slot = slot;
        _G.async_arg_i = arg->i;
        el_timer_register(
            _G.response_time, 0, EL_TIMER_LOWRES, &z_iop_http_async_reply,
            NULL
        );
        return;
    }

    z_iop_http_reply(slot, arg->i);
}

static void IOP_HTTP_RPC_CB(tstiop__t, iface, f)
{
    http_iop_channel_t *channel = http_iop_channel_from_msg(msg);

    assert(channel == _G.client_channel);

    _G.query_answered = true;
    _G.query_status = status;
    _G.query_code = OPT_DEFVAL(http_code, 0);

    if (status == IC_MSG_OK) {
        const httpc_qinfo_t *qinfo = msg->query.qinfo;
        const http_qhdr_t *qhdr;

        _G.val_answered = res->i;

        qhdr = http_qhdr_find_from_key(
            qinfo->hdrs, qinfo->hdrs_len, LSTR("X-ZCHK-IOP-HTTP-RESP")
        );
        _G.resp_http_header =
            (qhdr && lstr_equal(LSTR_PS_V(&qhdr->val), LSTR("99")));
    }
}

#define SCHEMA "http://example.com/tstiop"

static int z_iop_http_create_server(const iop_env_t *iop_env, sockunion_t *su)
{
    httpd_cfg_t *cfg = httpd_cfg_new();

    cfg->mode = _G.http_mode;
    cfg->max_conns = 1;
    cfg->max_queries = 1;
    cfg->pipeline_depth = 1;
    cfg->noact_delay = HTTP_TEST_NOACT_DELAY;

    _G.itcb =
        httpd_trigger__ic_new(iop_env, &tstiop__t__mod, SCHEMA, 2 << 20);
    _G.itcb->query_max_size = 2 << 20;
    _G.itcb->on_query_done = &z_iop_httpd_on_query_done;
    _G.itcb->on_reply_http_headers = &z_iop_httpd_on_reply_http_headers;
    httpd_trigger_register(cfg, POST, "iop", &_G.itcb->cb);
    ichttp_register_pre_post_hook(
        _G.itcb, tstiop__t, iface, f, &z_iop_httpd_pre_hook,
        &z_iop_httpd_post_hook, {}, {}
    );

    _G.server = httpd_listen(su, cfg);
    Z_ASSERT_P(_G.server);
    httpd_cfg_delete(&cfg);

    sockunion_setport(su, getsockport(el_fd_get_fd(_G.server), AF_INET));

    Z_HELPER_END;
}

static int
z_iop_http_create_client(const iop_env_t *iop_env, const sockunion_t *su)
{
    t_scope;
    SB_1k(err);
    http_iop_channel_cfg_t channel_cfg;
    const char *addr_s;
    int addr_len;
    lstr_t addr_lstr;
    lstr_t remote_url;
    iop_array_lstr_t remotes_url;
    core__httpc_cfg__t iop_cfg;

    addr_s = t_addr_fmt(su, &addr_len);
    addr_lstr = LSTR_INIT_V(addr_s, addr_len);
    remote_url = t_lstr_fmt("http://%*pM/iop", LSTR_FMT_ARG(addr_lstr));
    remotes_url = (iop_array_lstr_t)IOP_ARRAY(&remote_url, 1);

    iop_init(core__httpc_cfg, &iop_cfg);
    iop_cfg.use_http2 = _G.http_mode != HTTP_MODE_USE_HTTP1X_ONLY;
    iop_cfg.max_queries = 10;
    iop_cfg.pipeline_depth = 1;
    iop_cfg.noact_delay = HTTP_TEST_NOACT_DELAY;

    channel_cfg = (http_iop_channel_cfg_t){
        .name = LSTR("iop"),
        .urls = remotes_url,
        .iop_cfg = &iop_cfg,
        .iop_env = iop_env,
        .max_connections = OPT(1),
        .connection_timeout_msec = OPT(HTTP_TEST_NOACT_DELAY),
        .response_max_size = OPT(2 << 20),
        .on_connection_error_cb = &z_iop_httpc_on_connection_error,
        .on_ready_cb = &z_iop_httpc_on_ready,
    };

    _G.client_channel = http_iop_channel_create(&channel_cfg, &err);
    Z_ASSERT_P(_G.client_channel, "%*pM", SB_FMT_ARG(&err));

    Z_HELPER_END;
}

static int z_iop_http_create(const iop_env_t *iop_env)
{
    sockunion_t su;

    Z_ASSERT_N(addr_resolve("test", LSTR("127.0.0.1:1"), &su));

    if (getenv("Z_IOP_HTTP_FIX_PORT")) {
        /* Occasionally, this helps in debug or network traces */
        sockunion_setport(&su, 1080);
    } else {
        sockunion_setport(&su, 0);
    }

    Z_HELPER_RUN(z_iop_http_create_server(iop_env, &su));
    Z_HELPER_RUN(z_iop_http_create_client(iop_env, &su));

    Z_HELPER_END;
}

static void z_iop_http_query(int i)
{
    http_iop_msg_t *msg;

    _G.val_received = 0;
    _G.val_answered = 0;
    _G.query_sent = false;
    _G.query_answered = false;
    _G.query_status = 0;
    _G.query_code = 0;
    _G.query_http_header = false;
    _G.resp_http_header = false;

    msg = http_iop_msg_new(0);
    msg->http_headers = lstr_dup(LSTR("X-ZCHK-IOP-HTTP-QUERY: 1\r\n"));
    http_iop_query(_G.client_channel, msg, tstiop__t, iface, f, .i = i);

    _G.query_sent = true;
}

static int z_iop_http_finalize(void)
{
    http_iop_channel_delete(&_G.client_channel);
    httpd_unlisten(&_G.server);

    /* Wait to allow the transporting http to finalize. */
    el_wait_until(false, 100);
    Z_ASSERT(!el_has_pending_events());

    Z_HELPER_END;
}

static int z_iop_http_do_simple_query(
    const iop_env_t *iop_env, bool delayed, unsigned delay, unsigned repeat
)
{
    _G.query_conn_error = 0;

    Z_HELPER_RUN(z_iop_http_create(iop_env));

    if (!delayed) {
        _G.response_time = -1;
    } else {
        _G.response_time = delay;
        Z_ASSERT_LE(delay, (unsigned)(HTTP_TEST_NOACT_DELAY / 2));
    }

    for (unsigned i = 0; i < repeat; i++) {
        z_iop_http_query(20);

        el_wait_until(_G.query_answered, 100);

        Z_ASSERT_EQ(_G.val_received, 20);
        Z_ASSERT_EQ(_G.val_answered, 40);
        Z_ASSERT(_G.query_sent);
        Z_ASSERT(_G.query_answered);
        Z_ASSERT_EQ(_G.query_conn_error, 0);
        Z_ASSERT_EQ(_G.query_status, (unsigned)IC_MSG_OK);
        Z_ASSERT_EQ(_G.query_code, (unsigned)HTTP_CODE_OK);
        Z_ASSERT(_G.query_http_header);
        Z_ASSERT(_G.resp_http_header);
    }

    Z_HELPER_RUN(z_iop_http_finalize());

    Z_HELPER_END;
}

static void z_iop_http_tests(http_mode_t http_mode)
{
    iop_env_t *iop_env = iop_env_new();

    IOP_REGISTER_PACKAGES(iop_env, &tstiop__pkg);

    _G.http_mode = http_mode;

    Z_TEST(no_query) {
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, false, 0, 0));
    }
    Z_TEST_END;

    Z_TEST(simple_query) {
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, false, 0, 1));
        /* Repeat the query 10 times in a single run. */
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, false, 0, 10));
    }
    Z_TEST_END;

    Z_TEST(simple_query_async, "simple query (async delayed 10 ms)") {
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, true, 10, 1));
        /* Repeat the query 10 times in a single run. */
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, true, 10, 10));
    }
    Z_TEST_END;

    Z_TEST(simple_query_async_no_delay, "simple_query (async no delay)") {
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, true, 0, 1));
        /* Repeat the query 10 times in a single run. */
        Z_HELPER_RUN(z_iop_http_do_simple_query(iop_env, true, 0, 10));
    }
    Z_TEST_END;

    iop_env_delete(&iop_env);
}

Z_GROUP_EXPORT(iop_http)
{
    z_iop_http_tests(HTTP_MODE_USE_HTTP1X_ONLY);

    Z_TEST(ichttp_query_rpc_use_after_free) { /* {{{ */
        /* An in-flight ichttp_query_t dereferences cbe->fun (an iop_rpc_t
         * owned by the registered package) across event-loop turns, so a
         * concurrent iop_dso_close() would free it under the query.
         * ichttp_query_on_done() guards against this by pinning an
         * iop_env_ctx snapshot, which holds a reference on its DSOs and
         * defers the dlclose. This test pins a snapshot as the query does,
         * then closes the DSO under it. */
        t_scope;
        SB_1k(err);
        iop_env_t *iop_env = iop_env_new();
        iop_dso_t *dso;
        lstr_t path = t_lstr_fmt(
            "%*pM/iop/zchk-tstiop-plugin" SO_FILEEXT, LSTR_FMT_ARG(z_cmddir_g)
        );
        httpd_trigger__ic_t *tcb;
        const iop_mod_t *mod;
        const iop_iface_alias_t *alias;
        const iop_rpc_t *fun;
        ic_cb_entry_t entry;
        ichttp_cb_t *cbe;
        lstr_t rpc_name;
        int pos;

        /* Load `tstiop` from a DSO so its descriptors are unmapped on
         * unload. */
        dso = iop_dso_open(iop_env, path.s, &err);
        Z_ASSERT_P(
            dso, "cannot load plugin `%s`: %*pM", path.s, SB_FMT_ARG(&err)
        );

        {
            iop_env_ctx_scope(iop_env, iop_env_ctx);

            mod = iop_env_ctx_get_mod(iop_env_ctx, LSTR("tstiop.T"));
            Z_ASSERT_P(mod, "module tstiop.T not registered by the DSO");
        }
        alias = &mod->ifaces[0];
        Z_ASSERT_EQ(alias->iface->funs_len, 1);
        fun = &alias->iface->funs[0];

        /* Build the trigger and register the DSO's rpc; cbe->fun now points
         * into DSO memory. */
        tcb = httpd_trigger__ic_new(iop_env, mod, "test-schema", 1 << 20);
        p_clear(&entry, 1);
        entry.cb_type = IC_CB_NORMAL;
        entry.rpc = fun;
        __ichttp_register(
            tcb, alias, fun, (int32_t)((alias->tag << 16) | fun->tag), &entry
        );

        /* Mirror __t_ichttp_query_on_done_stage1: the query retains the cb.
         */
        rpc_name = t_lstr_fmt("%s.%sReq", alias->name.s, fun->name.s);
        pos = qm_find(ichttp_cbs, &tcb->impl, &rpc_name);
        Z_ASSERT_N(pos, "rpc `%pL` not registered in the trigger", &rpc_name);
        cbe = ichttp_cb_retain(tcb->impl.values[pos]);

        {
            /* Pin a ctx snapshot for the query's lifetime, exactly as
             * ichttp_query_on_done() does. */
            iop_env_ctx_guard_t guard = iop_env_ctx_acquire(iop_env);

            /* Concurrent unload: the query's pinned snapshot keeps the
             * descriptor mapped (deferred dlclose). */
            iop_dso_close(&dso);

            Z_ASSERT_LSTREQUAL(
                cbe->fun->name, LSTR("f"),
                "cbe->fun must stay valid while the query "
                "pins a ctx snapshot referencing its DSO"
            );
            iop_env_ctx_release(guard);
        }

        /* Teardown (mirrors httpd_trigger__ic_destroy). */
        ichttp_cb_release(&cbe);
        qm_deep_wipe(ichttp_cbs, &tcb->impl, IGNORE, ichttp_cb_delete);
        p_delete(&tcb);
        iop_env_delete(&iop_env);
    }
    Z_TEST_END;
    /* }}} */
}
Z_GROUP_END;

Z_GROUP_EXPORT(iop_http2)
{
    z_iop_http_tests(HTTP_MODE_USE_HTTP2_ONLY);
}
Z_GROUP_END;

/* }}} */
