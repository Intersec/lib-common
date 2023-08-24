/***************************************************************************/
/*                                                                         */
/* Copyright 2023 INTERSEC SA                                              */
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
#include <lib-common/http.h>

static struct {
    el_t server;
    httpd_cfg_t *server_cfg;

    httpc_cfg_t *client_cfg;
    httpc_t *client;

    httpd_trigger_t *hello;
    int response_time;
    lstr_t hello_response;

    httpc_query_t query;
    bool query_sent;
    bool query_answered;
    httpc_status_t query_status;

    /* for el_wait_until */
    bool el_wait_timed_out;
} z_http2_g;

#define _G z_http2_g

#define HTTP2_TEST_NOACT_DELAY 20 /* msecs */

static void z_http2_el_wait(el_t ev, data_t data)
{
    _G.el_wait_timed_out = true;
}

#define el_wait_until(cond, timeout)                                         \
    do {                                                                     \
        el_t __el_tmr = el_timer_register(timeout, EL_TIMER_LOWRES, 0,       \
                                          &z_http2_el_wait, NULL);           \
                                                                             \
        _G.el_wait_timed_out = false;                                        \
        while (!((cond) || _G.el_wait_timed_out)) {                          \
            el_loop_timeout(timeout);                                        \
        }                                                                    \
        el_unregister(&__el_tmr);                                            \
    } while (false)

/* {{{ Tests */

static void z_http2_hello_generate_response(int len)
{
    static const char hello[26] = "abcdefghijklmnopqrstuvwxyz";
    int sz = (int)sizeof(hello);
    SB_8k(sb);

    for (int i = 0; i < len; i += MIN(sz, len - i)) {
        sb_add(&sb, hello, MIN(sz, len - i));
    }

    lstr_transfer_sb(&_G.hello_response, &sb, false);
}

static void z_http2_hello_query_reply(httpd_query_t *q)
{
    outbuf_t *ob;

    /* Send response headers */
    ob = httpd_reply_hdrs_start(q, HTTP_CODE_OK, true);
    ob_adds(ob, "Content-Type: text/plain\r\n");
    httpd_reply_hdrs_done(q, -1, false);

    ob_add(ob, _G.hello_response.data, _G.hello_response.len);

    httpd_reply_done(q);
}

static void z_http2_hello_query_reply_async(el_t ev, data_t data)
{
    httpd_query_t *q = data.ptr;

    if (q->owner) { /* Connection is still alive. */
        z_http2_hello_query_reply(q);
    }
    obj_release(&q);
}

static void z_http2_hello_query_on_done(httpd_query_t *q)
{
    obj_retain(q);

    if (_G.response_time >= 0) {
        el_timer_register(_G.response_time, 0, EL_TIMER_LOWRES,
                          &z_http2_hello_query_reply_async, q);
        return;
    }

    z_http2_hello_query_reply(q);
    obj_release(&q);
}

static void
z_http2_hello_query_hook(httpd_trigger_t *tcb, struct httpd_query_t *q,
                         const httpd_qinfo_t *qi)
{
    q->on_done = z_http2_hello_query_on_done;
    q->qinfo = httpd_qinfo_dup(qi);
    httpd_bufferize(q, 1 << 20);
}

static void z_http2_default_httpd_cfg(void)
{
    httpd_cfg_t *cfg = httpd_cfg_new();

    cfg->mode = HTTP_MODE_USE_HTTP2_ONLY;
    cfg->max_conns = 1;
    cfg->max_queries = 1;
    cfg->pipeline_depth = 1;
    cfg->noact_delay = HTTP2_TEST_NOACT_DELAY;

    _G.hello = httpd_trigger_new();
    _G.hello->cb = z_http2_hello_query_hook;
    httpd_trigger_register(cfg, GET, "hello", _G.hello);

    httpd_cfg_delete(&_G.server_cfg);
    _G.server_cfg = cfg;
}

static void z_http2_default_httpc_cfg(void)
{
    httpc_cfg_t *cfg = httpc_cfg_new();

    cfg->http_mode = HTTP_MODE_USE_HTTP2_ONLY;
    cfg->max_queries = 10;
    cfg->pipeline_depth = 1;
    cfg->noact_delay = HTTP2_TEST_NOACT_DELAY;

    _G.client_cfg = cfg;
}

static void
z_http2_hello_query_on_done_client(httpc_query_t *q, httpc_status_t st)
{
    _G.query_answered = true;
    _G.query_sent = false;
    _G.query_status = st;

    httpc_query_wipe(q);
}

static void z_http2_hello_query_send(void)
{
    httpc_query_t *q = &_G.query;

    httpc_query_init(q);
    httpc_bufferize(q, 1 << 20);
    q->on_done = &z_http2_hello_query_on_done_client;

    httpc_query_attach(q, _G.client);
    httpc_query_start(q, HTTP_METHOD_GET, LSTR("localhost"), LSTR("/hello"));
    httpc_query_hdrs_done(q, -1, false);
    httpc_query_done(q);

    _G.query_sent = true;
    _G.query_answered = false;
}

static int z_http2_connect_client(void)
{
    sockunion_t su;

    Z_ASSERT_N(addr_resolve("test", LSTR("127.0.0.1:1"), &su));

    if (getenv("Z_HTTP2_FIX_PORT")) {
        /* Occasionally, this helps in debug or network traces */
        sockunion_setport(&su, 1080);
    } else {
        sockunion_setport(&su, 0);
    }

    z_http2_default_httpd_cfg();

    _G.server = httpd_listen(&su, _G.server_cfg);
    Z_ASSERT_P(_G.server);

    z_http2_default_httpc_cfg();

    sockunion_setport(&su, getsockport(el_fd_get_fd(_G.server), AF_INET));

    _G.client = httpc_connect(&su, _G.client_cfg, NULL);
    Z_ASSERT_P(_G.client);

    el_wait_until(!_G.client->busy, 100);
    Z_ASSERT(!_G.client->busy);

    Z_HELPER_END;
}

static int
z_http2_do_simple_query(bool delayed, unsigned delay, unsigned repeat)
{
    Z_HELPER_RUN(z_http2_connect_client());

    z_http2_hello_generate_response(1024);

    if (!delayed) {
        _G.response_time = -1;
    } else {
        _G.response_time = delay;
        Z_ASSERT_LE(delay, _G.client_cfg->noact_delay / 2);
    }

    Z_ASSERT_LE(repeat, _G.client_cfg->max_queries);

    for (unsigned i = 0; i < repeat; i++) {
        z_http2_hello_query_send();

        el_wait_until(_G.query_answered, 100);
        Z_ASSERT(_G.query_answered);

        Z_ASSERT_EQ(_G.query_status, HTTPC_STATUS_OK);
    }

    httpc_cfg_delete(&_G.client_cfg);
    httpd_unlisten(&_G.server);

    lstr_wipe(&_G.hello_response);

    /* Wait to allow the transporting http2 to finalize. */
    el_wait_until(false, 100);
    Z_ASSERT(!el_has_pending_events());

    Z_HELPER_END;
}

Z_GROUP_EXPORT(http2) {

    Z_TEST(no_query, "no query") {
        Z_HELPER_RUN(z_http2_do_simple_query(false, 0, 0));
    } Z_TEST_END;

    Z_TEST(simple_query, "simple query") {
        Z_HELPER_RUN(z_http2_do_simple_query(false, 0, 1));
        /* repeat the query 10 times in a single run */
        Z_HELPER_RUN(z_http2_do_simple_query(false, 0, 10));
    } Z_TEST_END;

    Z_TEST(simple_query_async, "simple query (async delayed 10 ms)") {
        Z_TODO("failing test awaiting for a fix");
        Z_HELPER_RUN(z_http2_do_simple_query(true, 10,  1));
        /* repeat the query 10 times in a single run */
        Z_HELPER_RUN(z_http2_do_simple_query(true, 10, 10));
    } Z_TEST_END;

    Z_TEST(simple_query_async_no_delay, "simple_query (async no delay)") {
        Z_TODO("failing test awaiting for a fix");
        Z_HELPER_RUN(z_http2_do_simple_query(true, 0, 1));
        /* repeat the query 10 times in a single run */
        Z_HELPER_RUN(z_http2_do_simple_query(true, 0, 10));
    } Z_TEST_END;


} Z_GROUP_END;

/* }}} */
