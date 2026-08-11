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
#include <lib-common/arith.h>
#include <lib-common/unix.h>
#include <lib-common/http.h>
#include <lib-common/http2.h>
#include <lib-common/net/hpack.h>

/* Observations are indexed by stream id; the raw tests use 1 and 3. */
#define Z_H2_NB_OBS_STREAMS 4

/* What the frames read back from the server say about one stream. */
typedef struct z_h2_stream_obs_t {
    int nb_hdrs;
    int nb_rst;
    int status;
    int rst_code;
} z_h2_stream_obs_t;

static struct {
    http_mode_t http_mode;

    el_t server;
    httpd_cfg_t *server_cfg;

    httpc_cfg_t *client_cfg;
    httpc_t *client;

    httpd_trigger_t *hello;
    httpd_trigger_t *post;
    int response_time;
    lstr_t hello_response;

    httpc_query_t query;
    bool query_sent;
    bool query_answered;
    httpc_status_t query_status;
    int query_code;
    bool query_has_clen;

    sb_t post_payload;
    int post_done_cnt;
    int hello_done_cnt;

    /* Raw HTTP/2 frame harness. raw_dec is connection-wide. */
    int raw_httpd_fd;
    sb_t raw_rbuf;
    hpack_dec_dtbl_t raw_dec;
    z_h2_stream_obs_t raw_obs[Z_H2_NB_OBS_STREAMS];
    bool raw_goaway;

    /* for el_wait_until */
    bool el_wait_timed_out;
} z_http_g;

#define _G z_http_g

#define HTTP_TEST_NOACT_DELAY 20 /* msecs */

static void z_http_el_wait(el_t ev, data_t data)
{
    _G.el_wait_timed_out = true;
}

#define el_wait_until(cond, timeout)                                         \
    do {                                                                     \
        el_t __el_tmr = el_timer_register(                                   \
            timeout, EL_TIMER_LOWRES, 0, &z_http_el_wait, NULL               \
        );                                                                   \
                                                                             \
        _G.el_wait_timed_out = false;                                        \
        while (!((cond) || _G.el_wait_timed_out)) {                          \
            el_loop_timeout(timeout);                                        \
        }                                                                    \
        el_unregister(&__el_tmr);                                            \
    } while (false)

/* {{{ Tests */

static void z_http_hello_generate_response(int len)
{
    static const char hello[] = "abcdefghijklmnopqrstuvwxyz";
    int sz = strlen(hello);
    SB_8k(sb);

    for (int i = 0; i < len; i += MIN(sz, len - i)) {
        sb_add(&sb, hello, MIN(sz, len - i));
    }

    lstr_transfer_sb(&_G.hello_response, &sb, false);
}

static void z_http_hello_query_reply(httpd_query_t *q)
{
    outbuf_t *ob;

    /* Send response headers */
    ob = httpd_reply_hdrs_start(q, HTTP_CODE_OK, true);
    ob_adds(ob, "Content-Type: text/plain\r\n");
    httpd_reply_hdrs_done(q, -1, false);

    ob_add(ob, _G.hello_response.data, _G.hello_response.len);

    httpd_reply_done(q);
}

static void z_http_hello_query_reply_async(el_t ev, data_t data)
{
    httpd_query_t *q = data.ptr;

    if (q->owner) { /* Connection is still alive. */
        z_http_hello_query_reply(q);
    }
    obj_release(&q);
}

static void z_http_hello_query_on_done(httpd_query_t *q)
{
    _G.hello_done_cnt++;
    obj_retain(q);

    if (_G.response_time >= 0) {
        el_timer_register(
            _G.response_time, 0, EL_TIMER_LOWRES,
            &z_http_hello_query_reply_async, q
        );
        return;
    }

    z_http_hello_query_reply(q);
    obj_release(&q);
}

static void z_http_hello_query_hook(
    httpd_trigger_t *tcb, struct httpd_query_t *q, const httpd_qinfo_t *qi
)
{
    q->on_done = z_http_hello_query_on_done;
    q->qinfo = httpd_qinfo_dup(qi);
    httpd_bufferize(q, 1 << 20);
}

static void z_http_post_query_on_done(httpd_query_t *q)
{
    _G.post_done_cnt++;
    sb_setsb(&_G.post_payload, &q->payload);

    /* Send response headers */
    httpd_reply_hdrs_start(q, HTTP_CODE_NO_CONTENT, true);
    httpd_reply_hdrs_done(q, -1, false);

    httpd_reply_done(q);
}

static void z_http_post_query_hook(
    httpd_trigger_t *tcb, struct httpd_query_t *q, const httpd_qinfo_t *qi
)
{
    q->on_done = z_http_post_query_on_done;
    q->qinfo = httpd_qinfo_dup(qi);
    httpd_bufferize(q, 1 << 20);
}

/* Reset the server-side capture state before a test. */
static void z_http_reset_capture(void)
{
    static bool inited;

    if (!inited) {
        sb_init(&_G.post_payload);
        sb_init(&_G.raw_rbuf);
        _G.raw_httpd_fd = -1;
        inited = true;
    }
    sb_reset(&_G.post_payload);
    sb_reset(&_G.raw_rbuf);
    _G.post_done_cnt = 0;
    _G.hello_done_cnt = 0;
    p_clear(_G.raw_obs, countof(_G.raw_obs));
    _G.raw_goaway = false;
}

static void z_http_default_httpd_cfg(unsigned max_queries)
{
    httpd_cfg_t *cfg = httpd_cfg_new();

    z_http_reset_capture();

    cfg->mode = _G.http_mode;
    cfg->max_conns = 1;
    cfg->max_queries = max_queries;
    cfg->pipeline_depth = 1;
    cfg->noact_delay = HTTP_TEST_NOACT_DELAY;

    _G.hello = httpd_trigger_new();
    _G.hello->cb = z_http_hello_query_hook;
    httpd_trigger_register(cfg, GET, "hello", _G.hello);

    _G.post = httpd_trigger_new();
    _G.post->cb = z_http_post_query_hook;
    httpd_trigger_register(cfg, POST, "post", _G.post);

    httpd_cfg_delete(&_G.server_cfg);
    _G.server_cfg = cfg;
}

static void z_http_default_httpc_cfg(unsigned max_queries)
{
    httpc_cfg_t *cfg = httpc_cfg_new();

    cfg->http_mode = _G.http_mode;
    cfg->max_queries = max_queries;
    cfg->pipeline_depth = 1;
    cfg->noact_delay = HTTP_TEST_NOACT_DELAY;

    _G.client_cfg = cfg;
}

static void
z_http_hello_query_on_done_client(httpc_query_t *q, httpc_status_t st)
{
    _G.query_answered = true;
    _G.query_sent = false;
    _G.query_status = st;
    _G.query_code = q->qinfo->code;

    _G.query_has_clen = !!http_qhdr_find(
        q->qinfo->hdrs, q->qinfo->hdrs_len, HTTP_WKHDR_CONTENT_LENGTH
    );

    httpc_query_wipe(q);
}

static void z_http_hello_query_send(void)
{
    httpc_query_t *q = &_G.query;

    httpc_query_init(q);
    httpc_bufferize(q, 1 << 20);
    q->on_done = &z_http_hello_query_on_done_client;

    httpc_query_attach(q, _G.client);
    httpc_query_start(q, HTTP_METHOD_GET, LSTR("localhost"), LSTR("/hello"));
    httpc_query_hdrs_done(q, -1, false);
    httpc_query_done(q);

    _G.query_sent = true;
    _G.query_answered = false;
}

static void z_http_post_query_send(void)
{
    httpc_query_t *q = &_G.query;

    httpc_query_init(q);
    httpc_bufferize(q, 1 << 20);
    q->on_done = &z_http_hello_query_on_done_client;

    httpc_query_attach(q, _G.client);
    httpc_query_start(q, HTTP_METHOD_POST, LSTR("localhost"), LSTR("/post"));
    httpc_query_hdrs_done(q, -1, false);
    httpc_query_done(q);

    _G.query_sent = true;
    _G.query_answered = false;
}

static int z_http_connect_client(unsigned max_queries)
{
    sockunion_t su;

    Z_ASSERT_N(addr_resolve("test", LSTR("127.0.0.1:1"), &su));

    if (getenv("Z_HTTP_FIX_PORT")) {
        /* Occasionally, this helps in debug or network traces */
        sockunion_setport(&su, 1080);
    } else {
        sockunion_setport(&su, 0);
    }

    z_http_default_httpd_cfg(max_queries);

    _G.server = httpd_listen(&su, _G.server_cfg);
    Z_ASSERT_P(_G.server);

    z_http_default_httpc_cfg(max_queries);

    sockunion_setport(&su, getsockport(el_fd_get_fd(_G.server), AF_INET));

    _G.client = httpc_connect(&su, _G.client_cfg, NULL);
    Z_ASSERT_P(_G.client);

    el_wait_until(!_G.client->busy, 100);
    Z_ASSERT(!_G.client->busy);

    Z_HELPER_END;
}

static int z_http_do_simple_post(void)
{
    Z_HELPER_RUN(z_http_connect_client(1));

    z_http_post_query_send();

    el_wait_until(_G.query_answered, 100);
    Z_ASSERT(_G.query_answered);

    Z_ASSERT_EQ(_G.query_status, HTTPC_STATUS_OK);
    Z_ASSERT_EQ(_G.query_code, HTTP_CODE_NO_CONTENT);
    Z_ASSERT(!_G.query_has_clen);

    httpc_cfg_delete(&_G.client_cfg);
    httpd_unlisten(&_G.server);

    /* Wait to allow the transporting http to finalize. */
    el_wait_until(false, 100);
    Z_ASSERT(!el_has_pending_events());

    Z_HELPER_END;
}

static int
z_http_do_simple_query(bool delayed, unsigned delay, unsigned repeat)
{
    Z_HELPER_RUN(z_http_connect_client(repeat));

    z_http_hello_generate_response(1024);

    if (!delayed) {
        _G.response_time = -1;
    } else {
        _G.response_time = delay;
        Z_ASSERT_LE(delay, _G.client_cfg->noact_delay / 2);
    }

    Z_ASSERT_LE(repeat, _G.client_cfg->max_queries);

    for (unsigned i = 0; i < repeat; i++) {
        z_http_hello_query_send();

        el_wait_until(_G.query_answered, 100);
        Z_ASSERT(_G.query_answered);

        Z_ASSERT_EQ(_G.query_status, HTTPC_STATUS_OK);
    }

    httpc_cfg_delete(&_G.client_cfg);
    httpd_unlisten(&_G.server);

    lstr_wipe(&_G.hello_response);

    /* The http2 layer acts as a transport layer for the upper layer
     * (our http1.x api layer).
     * We need to wait for the hidden layer of connections on its
     * shutdown sequence to allow the transporting http to finalize.
     */
    el_wait_until(false, 100);
    Z_ASSERT(!el_has_pending_events());

    Z_HELPER_END;
}

/* {{{ Raw HTTP/2 frame harness */

/* Our own HTTP/2 client cannot send these requests: it asserts "clen >= 0"
 * and strips Transfer-Encoding, so the harness speaks raw frames. */

/* Longest wait for the server to react at all. */
#define Z_H2_RAW_REACT_DELAY 1000 /* msecs */

/* Quiet window after that reaction: a late duplicate must have time to
 * land. */
#define Z_H2_RAW_SETTLE_DELAY 50 /* msecs */

/* {{{ Frame and header-block builders */

/* Encode one header as a literal field without indexing, new name, raw
 * strings (RFC 7541 6.2.2). */
static void z_h2_add_hpack_hdr(sb_t *out, lstr_t key, lstr_t val)
{
    unsigned flags =
        HPACK_FLG_NOZIP_STR | HPACK_FLG_SKIP_TBLS | HPACK_FLG_NOADD_DTBL;
    hpack_enc_dtbl_t dtbl;
    byte *dst;
    int len;

    /* Stateless, but it still reads the size limits. */
    hpack_enc_dtbl_init(&dtbl);
    dst = (byte *)sb_grow(out, hpack_buflen_to_write_hdr(key, val, flags));
    len = hpack_encoder_write_hdr(&dtbl, key, val, 0, 0, flags, dst);
    assert(len > 0);
    __sb_fixlen(out, out->len + len);
    hpack_enc_dtbl_wipe(&dtbl);
}

/* Write a 9-octet frame header followed by its payload (RFC 9113 4.1). */
static void z_h2_add_frame(
    sb_t *out, uint8_t type, uint8_t flags, uint32_t stream_id,
    pstream_t payload
)
{
    byte *hdr = (byte *)sb_growlen(out, HTTP2_LEN_FRAME_HDR);

    put_unaligned_be24(hdr, ps_len(&payload));
    hdr[3] = type;
    hdr[4] = flags;
    put_unaligned_be32(hdr + 5, stream_id);
    sb_add_ps(out, payload);
}

/* Write a request HEADERS frame: pseudo-headers first and lowercase
 * (RFC 9113 8.3). A NULL \p clen omits Content-Length. */
static void z_h2_add_request(
    sb_t *out, uint32_t stream_id, lstr_t method, lstr_t path, lstr_t clen,
    bool eos
)
{
    uint8_t flags = HTTP2_FLAG_END_HEADERS;
    SB_1k(block);

    z_h2_add_hpack_hdr(&block, LSTR(":method"), method);
    z_h2_add_hpack_hdr(&block, LSTR(":scheme"), LSTR("http"));
    z_h2_add_hpack_hdr(&block, LSTR(":path"), path);
    z_h2_add_hpack_hdr(&block, LSTR(":authority"), LSTR("localhost"));
    if (clen.s) {
        z_h2_add_hpack_hdr(&block, LSTR("content-length"), clen);
    }
    if (eos) {
        flags |= HTTP2_FLAG_END_STREAM;
    }
    z_h2_add_frame(
        out, HTTP2_TYPE_HEADERS, flags, stream_id, ps_initsb(&block)
    );
}

/* Write a DATA frame. Pass LSTR("") for a zero-length frame. */
static void
z_h2_add_data(sb_t *out, uint32_t stream_id, lstr_t body, bool eos)
{
    z_h2_add_frame(
        out, HTTP2_TYPE_DATA, eos ? HTTP2_FLAG_END_STREAM : HTTP2_FLAG_NONE,
        stream_id, ps_initlstr(&body)
    );
}

/* }}} */
/* {{{ Raw connection driving */

/* Open a raw socket and send the client connection preface (RFC 9113 3.4).
 *
 * The empty SETTINGS frame keeps the default 65535-octet stream window,
 * which covers every body sent here: no WINDOW_UPDATE needed.
 */
static int z_h2_raw_setup(void)
{
    sockunion_t su;
    SB_1k(preface);

    /* Also makes _G.raw_httpd_fd a valid descriptor. */
    z_http_reset_capture();

    /* A failing Z_ASSERT jumps past the teardown: drop what it left open. */
    p_close(&_G.raw_httpd_fd);
    httpd_unlisten(&_G.server);

    _G.http_mode = HTTP_MODE_USE_HTTP2_ONLY;
    _G.response_time = -1; /* answer /hello synchronously */
    z_http_hello_generate_response(16);

    /* Same recovery: a failed test may have left a live table. */
    hpack_dec_dtbl_wipe(&_G.raw_dec);
    hpack_dec_dtbl_init(&_G.raw_dec);
    /* We advertise no settings: the server uses the default table size. */
    hpack_dec_dtbl_init_settings(&_G.raw_dec, HTTP2_LEN_HDR_TABLE_SIZE_INIT);

    Z_ASSERT_N(addr_resolve("test", LSTR("127.0.0.1:1"), &su));
    if (getenv("Z_HTTP_FIX_PORT")) {
        sockunion_setport(&su, 1080);
    } else {
        sockunion_setport(&su, 0);
    }

    /* Also resets the server-side capture state. */
    z_http_default_httpd_cfg(1);
    /* 20 ms is too tight: frames span several event loop turns. */
    _G.server_cfg->noact_delay = 1000;

    _G.server = httpd_listen(&su, _G.server_cfg);
    Z_ASSERT_P(_G.server);

    sockunion_setport(&su, getsockport(el_fd_get_fd(_G.server), AF_INET));
    _G.raw_httpd_fd = connectx(-1, &su, 1, SOCK_STREAM, IPPROTO_TCP, 0);
    Z_ASSERT_N(_G.raw_httpd_fd);

    sb_add_lstr(&preface, http2_client_preamble_g);
    z_h2_add_frame(
        &preface, HTTP2_TYPE_SETTINGS, HTTP2_FLAG_NONE, HTTP2_ID_NO_STREAM,
        ps_initstr("")
    );
    Z_ASSERT_N(xwrite(_G.raw_httpd_fd, preface.data, preface.len));

    Z_HELPER_END;
}

static int z_h2_raw_teardown(void)
{
    p_close(&_G.raw_httpd_fd);
    httpd_unlisten(&_G.server);

    hpack_dec_dtbl_wipe(&_G.raw_dec);
    lstr_wipe(&_G.hello_response);
    sb_wipe(&_G.raw_rbuf);
    sb_wipe(&_G.post_payload);

    /* Wait to allow the transporting http2 connection to finalize. */
    el_wait_until(!el_has_pending_events(), 100);
    Z_ASSERT(!el_has_pending_events());

    Z_HELPER_END;
}

/* Collect whatever the server has written so far. */
static void z_h2_raw_drain(void)
{
    for (;;) {
        int res =
            sb_recv(&_G.raw_rbuf, _G.raw_httpd_fd, BUFSIZ, MSG_DONTWAIT);

        if (res < 0) {
            if (errno == EINTR) {
                continue;
            }
            /* EAGAIN: nothing more for now. */
            return;
        }
        if (!res) {
            return; /* end of file */
        }
    }
}

/* Whether the server dispatched a query, or wrote a response or a refusal.
 * Frame types only, from a copy: decoding twice desynchronizes the decoder.
 */
static bool z_h2_raw_reacted(void)
{
    pstream_t ps;

    z_h2_raw_drain();
    if (_G.post_done_cnt || _G.hello_done_cnt) {
        return true;
    }
    ps = ps_initsb(&_G.raw_rbuf);
    for (;;) {
        http2_frame_info_t frame;
        pstream_t payload;

        if (http2_parse_frame_hdr(&ps, &frame) < 0 ||
            ps_get_ps(&ps, frame.len, &payload) < 0)
        {
            return false;
        }
        if (frame.type == HTTP2_TYPE_HEADERS ||
            frame.type == HTTP2_TYPE_RST_STREAM)
        {
            return true;
        }
    }
}

/* }}} */
/* {{{ Response frame walker */

/* Decode a response header block far enough to read its :status. */
static int z_h2_raw_get_status(pstream_t block, int *status)
{
    SB_1k(lines);
    pstream_t ps;
    pstream_t val = ps_init(NULL, 0);

    /* Dynamic table size updates come before the fields (RFC 7541 4.2). */
    for (;;) {
        int rc = hpack_decoder_read_dts_update(&_G.raw_dec, &block);

        Z_ASSERT_N(rc);
        if (!rc) {
            break;
        }
    }
    while (!ps_done(&block)) {
        hpack_xhdr_t xhdr;
        int len;
        int keylen;
        byte *out;

        Z_ASSERT_ZERO(
            hpack_decoder_extract_hdr(&_G.raw_dec, &block, &xhdr, &len)
        );
        out = (byte *)sb_grow(&lines, len);
        len = hpack_decoder_write_hdr(&_G.raw_dec, &xhdr, out, &keylen);
        Z_ASSERT_N(len);
        __sb_fixlen(&lines, lines.len + len);
    }
    /* :status comes first in a response header block (RFC 9113 8.3.2). */
    ps = ps_initsb(&lines);
    Z_ASSERT_N(ps_skipstr(&ps, ":status: "));
    Z_ASSERT_N(ps_get_ps_upto_str(&ps, "\r\n", &val));
    Z_ASSERT_N(lstr_to_int(LSTR_PS_V(&val), status));

    Z_HELPER_END;
}

/* Record the response HEADERS, RST_STREAM and GOAWAY per stream. Walked
 * frames are dropped: re-walking would desynchronize the HPACK decoder. */
static int z_h2_raw_observe(void)
{
    pstream_t ps = ps_initsb(&_G.raw_rbuf);
    const void *walked = ps.p;

    for (;;) {
        http2_frame_info_t frame;
        pstream_t payload;
        z_h2_stream_obs_t *obs;
        int status;

        if (http2_parse_frame_hdr(&ps, &frame) < 0) {
            break;
        }
        if (ps_get_ps(&ps, frame.len, &payload) < 0) {
            break;
        }
        /* Consumed whichever way the iteration ends. */
        walked = ps.p;
        if (frame.type == HTTP2_TYPE_GOAWAY) {
            _G.raw_goaway = true;
            continue;
        }
        if (!frame.stream_id) {
            continue;
        }
        /* Still decode it: a skipped block desynchronizes the table. */
        obs = frame.stream_id < countof(_G.raw_obs)
                  ? &_G.raw_obs[frame.stream_id]
                  : NULL;
        switch (frame.type) {
        case HTTP2_TYPE_HEADERS:
            /* No padding, no CONTINUATION: the payload is a whole block. */
            Z_ASSERT(frame.flags & HTTP2_FLAG_END_HEADERS);
            Z_ASSERT(
                !(frame.flags & (HTTP2_FLAG_PADDED | HTTP2_FLAG_PRIORITY))
            );
            Z_HELPER_RUN(z_h2_raw_get_status(payload, &status));
            if (obs && !obs->nb_hdrs++) {
                obs->status = status;
            }
            break;

        case HTTP2_TYPE_RST_STREAM:
            /* The error code is the whole payload (RFC 9113 6.4). */
            Z_ASSERT_EQ((int)ps_len(&payload), 4);
            if (obs && !obs->nb_rst++) {
                obs->rst_code = (int)get_unaligned_be32(payload.p);
            }
            break;

        default:
            break;
        }
    }
    sb_skip_upto(&_G.raw_rbuf, walked);

    Z_HELPER_END;
}

/* Hand \p frames to the server, let it answer, then walk what it wrote. */
static int z_h2_raw_exchange(const sb_t *frames)
{
    Z_ASSERT_N(xwrite(_G.raw_httpd_fd, frames->data, frames->len));
    el_wait_until(z_h2_raw_reacted(), Z_H2_RAW_REACT_DELAY);
    el_wait_until(false, Z_H2_RAW_SETTLE_DELAY);
    z_h2_raw_drain();
    Z_HELPER_RUN(z_h2_raw_observe());

    Z_HELPER_END;
}

/* }}} */
/* }}} */

static void z_http_tests(http_mode_t http_mode)
{
    _G.http_mode = http_mode;

    Z_TEST(no_query) {
        Z_HELPER_RUN(z_http_do_simple_query(false, 0, 0));
    }
    Z_TEST_END;

    Z_TEST(simple_query) {
        Z_HELPER_RUN(z_http_do_simple_query(false, 0, 1));

        /* Repeat the query 10 times in a single run. */
        Z_HELPER_RUN(z_http_do_simple_query(false, 0, 10));
    }
    Z_TEST_END;

    Z_TEST(simple_query_async, "simple query (async delayed 10 ms)") {
        Z_HELPER_RUN(z_http_do_simple_query(true, 10, 1));

        /* Repeat the query 10 times in a single run. */
        Z_HELPER_RUN(z_http_do_simple_query(true, 10, 10));
    }
    Z_TEST_END;

    Z_TEST(simple_query_async_no_delay, "simple_query (async no delay)") {
        Z_HELPER_RUN(z_http_do_simple_query(true, 0, 1));

        /* Repeat the query 10 times in a single run. */
        Z_HELPER_RUN(z_http_do_simple_query(true, 0, 10));
    }
    Z_TEST_END;

    Z_TEST(simple_post) {
        Z_HELPER_RUN(z_http_do_simple_post());
    }
    Z_TEST_END;
}

Z_GROUP_EXPORT(http)
{
    z_http_tests(HTTP_MODE_USE_HTTP1X_ONLY);
}
Z_GROUP_END;

Z_GROUP_EXPORT(http2)
{
    z_http_tests(HTTP_MODE_USE_HTTP2_ONLY);
}
Z_GROUP_END;

/* {{{ httpc_pool resolve_on_connect tests */

static struct {
    bool connect_error_called;
    int connect_error_errno;
} z_http_pool_g;

static void z_http_pool_on_connect_error(const httpc_t *w, int errnum)
{
    z_http_pool_g.connect_error_called = true;
    z_http_pool_g.connect_error_errno = errnum;
}

Z_GROUP_EXPORT(httpc_pool)
{

    Z_TEST(resolve_on_connect_multi_addr) {
        httpc_pool_t pool;

        httpc_pool_init(&pool);
        pool.cfg = httpc_cfg_new();
        pool.cfg->noact_delay = 20;
        pool.resolve_on_connect = true;
        pool.host = LSTR("127.0.0.1:1");
        pool.max_len = 10;
        pool.on_connect_error = &z_http_pool_on_connect_error;

        /* Launch triggers resolution; resolved should be populated */
        httpc_pool_launch(&pool);
        Z_ASSERT_GE(pool.resolved.len, 1);
        Z_ASSERT_EQ(pool.resolved_idx, 0);
        Z_ASSERT_EQ(pool.su.family, AF_INET);

        httpc_pool_wipe(&pool, true);

        el_wait_until(false, 50);
    }
    Z_TEST_END;

    Z_TEST(resolved_idx_advances_on_error) {
        httpc_pool_t pool;
        httpc_t *w;

        httpc_pool_init(&pool);
        pool.cfg = httpc_cfg_new();
        pool.cfg->noact_delay = 20;
        pool.resolve_on_connect = true;
        /* Port 1 is unlikely to be listening — connection will fail */
        pool.host = LSTR("127.0.0.1:1");
        pool.max_len = 10;
        pool.on_connect_error = &z_http_pool_on_connect_error;

        z_http_pool_g.connect_error_called = false;
        w = httpc_pool_launch(&pool);
        Z_ASSERT_P(w);
        Z_ASSERT_GE(pool.resolved.len, 1);
        Z_ASSERT_EQ(pool.resolved_idx, 0);

        /* Wait for connection to fail */
        el_wait_until(z_http_pool_g.connect_error_called, 5000);
        Z_ASSERT(z_http_pool_g.connect_error_called);
        Z_ASSERT_EQ(z_http_pool_g.connect_error_errno, ECONNREFUSED);
        Z_ASSERT_EQ(
            pool.resolved_idx, 1 % pool.resolved.len,
            "resolved_idx should advance on connect error"
        );

        httpc_pool_wipe(&pool, true);

        el_wait_until(false, 50);
    }
    Z_TEST_END;

    Z_TEST(pool_failover_to_working_addr) {
        httpc_pool_t pool;
        sockunion_t su;
        httpc_t *w;

        /* Start a real server to connect to */
        httpd_cfg_t *server_cfg = httpd_cfg_new();

        server_cfg->max_conns = 1;
        server_cfg->max_queries = 1;
        server_cfg->noact_delay = 100;

        Z_ASSERT_N(addr_resolve("test", LSTR("127.0.0.1:1"), &su));
        sockunion_setport(&su, 0);
        el_t server = httpd_listen(&su, server_cfg);
        Z_ASSERT_P(server);
        int port = getsockport(el_fd_get_fd(server), AF_INET);

        /* Set up a pool with resolve_on_connect. The host resolves to
         * 127.0.0.1 which is where our server is. First pre-fill resolved
         * with a bad address followed by the real one to simulate failover.
         */
        httpc_pool_init(&pool);
        pool.cfg = httpc_cfg_new();
        pool.cfg->noact_delay = 100;
        pool.max_len = 10;
        pool.on_connect_error = &z_http_pool_on_connect_error;

        /* Manually populate resolved: first a bad addr, then the good one */
        {
            sockunion_t bad_su;
            sockunion_t good_su;

            Z_ASSERT_N(
                addr_info(&bad_su, AF_INET, ps_initstr("127.0.0.1"), 1)
            );
            Z_ASSERT_N(
                addr_info(&good_su, AF_INET, ps_initstr("127.0.0.1"), port)
            );

            qv_append(&pool.resolved, bad_su);
            qv_append(&pool.resolved, good_su);
        }
        pool.resolved_idx = 0;
        pool.su = pool.resolved.tab[0];

        /* First attempt: connects to port 1 (bad) */
        z_http_pool_g.connect_error_called = false;
        w = httpc_connect_as(&pool.su, pool.su_src, pool.cfg, &pool);
        Z_ASSERT_P(w);

        el_wait_until(z_http_pool_g.connect_error_called, 5000);
        Z_ASSERT(z_http_pool_g.connect_error_called);
        Z_ASSERT_EQ(
            pool.resolved_idx, 1,
            "resolved_idx should advance past the failed address"
        );

        /* Second attempt: connects to the real server port (good) */
        pool.su = pool.resolved.tab[pool.resolved_idx];
        w = httpc_connect_as(&pool.su, pool.su_src, pool.cfg, &pool);
        Z_ASSERT_P(w);

        el_wait_until(!w->busy, 100);
        Z_ASSERT(!w->busy, "should have connected to good address");

        httpc_pool_wipe(&pool, true);
        httpd_unlisten(&server);
        httpd_cfg_delete(&server_cfg);

        el_wait_until(false, 100);
        Z_ASSERT(!el_has_pending_events());
    }
    Z_TEST_END;
}
Z_GROUP_END;

/* {{{ Raw HTTP/2 frame tests */

/* HTTP/2 only: these drive raw frames, so they cannot run over HTTP/1.x. */
Z_GROUP_EXPORT(http2_raw_frames)
{
    Z_TEST(
        post_clen_body,
        "a Content-Length delimited body, in one DATA frame carrying "
        "END_STREAM"
    )
    {
        SB_1k(frames);

        Z_HELPER_RUN(z_h2_raw_setup());

        z_h2_add_request(
            &frames, 1, LSTR("POST"), LSTR("/post"), LSTR("6"), false
        );
        z_h2_add_data(&frames, 1, LSTR("framed"), true);
        Z_HELPER_RUN(z_h2_raw_exchange(&frames));

        Z_ASSERT_EQ(_G.post_done_cnt, 1);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&_G.post_payload), LSTR("framed"));
        Z_ASSERT_EQ(_G.raw_obs[1].status, HTTP_CODE_NO_CONTENT);
        Z_ASSERT_ZERO(_G.raw_obs[1].nb_rst);

        Z_HELPER_RUN(z_h2_raw_teardown());
    }
    Z_TEST_END;
}
Z_GROUP_END;

/* }}} */

/* }}} */
