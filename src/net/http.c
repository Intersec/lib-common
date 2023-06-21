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

#include <lib-common/unix.h>
#include <lib-common/datetime.h>

#include <lib-common/http.h>
#include <lib-common/net/hpack.h>

#include <lib-common/iop.h>
#include <lib-common/core/core.iop.h>

#include <openssl/ssl.h>

#include "httptokens.h"

static struct {
    logger_t logger;
    unsigned http2_conn_count;
} http_g = {
#define _G  http_g
    .logger = LOGGER_INIT_INHERITS(NULL, "http"),
};

/*
 * rfc 2616 TODO list:
 *
 * ETags
 * Range requests
 *
 * Automatically transform chunked-encoding to C-L for HTTP/1.0
 *
 */

enum http_parse_code {
    PARSE_MISSING_DATA =  1,
    PARSE_OK           =  0,
    PARSE_ERROR        = -1,
};

struct http_date {
    time_t date;
    char   buf[sizeof("Date: Sun, 06 Nov 1994 08:49:37 GMT\r\n")];
};
static __thread struct http_date date_cache_g;

/* "()<>@,;:\<>/[]?={} \t" + 1..31 + DEL  */
static ctype_desc_t const http_non_token = {
    {
        0xffffffff, 0xfc009301, 0x38000001, 0xa8000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    }
};

static void httpd_mark_query_answered(httpd_query_t *q);

static void httpd_trigger_destroy(httpd_trigger_t *cb, unsigned delta)
{
    assert (cb->refcnt >= delta);

    cb->refcnt -= delta;
    if (cb->refcnt == 0) {
        lstr_wipe(&cb->auth_realm);
        if (cb->destroy) {
            cb->destroy(cb);
        } else {
            p_delete(&cb);
        }
    }
}

httpd_trigger_t *httpd_trigger_dup(httpd_trigger_t *cb)
{
    cb->refcnt += 2;
    return cb;
}

void httpd_trigger_delete(httpd_trigger_t **cbp)
{
    if (*cbp) {
        httpd_trigger_destroy(*cbp, 2);
        *cbp = NULL;
    }
}

void httpd_trigger_persist(httpd_trigger_t *cb)
{
    cb->refcnt |= 1;
}

void httpd_trigger_loose(httpd_trigger_t *cb)
{
    httpd_trigger_destroy(cb, cb->refcnt & 1);
}

/* zlib helpers {{{ */

#define HTTP_ZLIB_BUFSIZ   (64 << 10)

static void http_zlib_stream_reset(z_stream *s)
{
    s->next_in  = s->next_out  = NULL;
    s->avail_in = s->avail_out = 0;
}

#define http_zlib_inflate_init(w) \
    ({  typeof(*(w)) *_w = (w);                                   \
                                                                  \
        if (_w->zs.state == NULL) {                               \
            if (inflateInit2(&_w->zs, MAX_WBITS + 32) != Z_OK)    \
                logger_panic(&_G.logger, "zlib error");           \
        }                                                         \
        http_zlib_stream_reset(&_w->zs);                          \
        _w->compressed = true;                                    \
    })

#define http_zlib_reset(w) \
    ({  typeof(*(w)) *_w = (w);                                   \
                                                                  \
        if (_w->compressed) {                                     \
            http_zlib_stream_reset(&_w->zs);                      \
            inflateReset(&_w->zs);                                \
            _w->compressed = false;                               \
        }                                                         \
    })

#define http_zlib_wipe(w) \
    ({  typeof(*(w)) *_w = (w);                            \
                                                           \
        if (_w->zs.state)                                  \
            inflateEnd(&_w->zs);                           \
        _w->compressed = false;                            \
    })

static int http_zlib_inflate(z_stream *s, int *clen,
                             sb_t *out, pstream_t *in, int flush)
{
    int rc;

    s->next_in   = (Bytef *)in->s;
    s->avail_in  = ps_len(in);

    for (;;) {
        size_t sz = MAX(HTTP_ZLIB_BUFSIZ, s->avail_in * 4);

        s->next_out  = (Bytef *)sb_grow(out, sz);
        s->avail_out = sb_avail(out);

        rc = inflate(s, flush ? Z_FINISH : Z_SYNC_FLUSH);
        switch (rc) {
          case Z_BUF_ERROR:
          case Z_OK:
          case Z_STREAM_END:
            __sb_fixlen(out, (char *)s->next_out - out->data);
            if (*clen >= 0) {
                *clen -= (char *)s->next_in - in->s;
            }
            __ps_skip_upto(in, s->next_in);
            break;
          default:
            return rc;
        }

        if (rc == Z_STREAM_END && ps_len(in)) {
            return Z_STREAM_ERROR;
        }
        if (rc == Z_BUF_ERROR) {
            if (s->avail_in) {
                continue;
            }
            if (flush) {
                return Z_STREAM_ERROR;
            }
            return 0;
        }
        return 0;
    }
}

/* }}} */
/* RFC 2616 helpers {{{ */

#define PARSE_RETHROW(e)  ({                                                 \
            int __e = (e);                                                   \
            if (unlikely(__e)) {                                             \
                return __e;                                                  \
            }                                                                \
        })

static inline void http_skipspaces(pstream_t *ps)
{
    while (ps->s < ps->s_end && (ps->s[0] == ' ' || ps->s[0] == '\t'))
        ps->s++;
}

/* rfc 2616, §2.2: Basic rules */
static inline int http_getline(pstream_t *ps, unsigned max_len,
                               pstream_t *out)
{
    const char *p = memmem(ps->s, ps_len(ps), "\r\n", 2);

    if (unlikely(!p)) {
        *out = ps_initptr(NULL, NULL);
        if (ps_len(ps) > max_len) {
            return PARSE_ERROR;
        }
        return PARSE_MISSING_DATA;
    }
    *out = ps_initptr(ps->s, p);
    return __ps_skip_upto(ps, p + 2);
}

/* rfc 2616, §3.3.1: Full Date */
static char const * const days[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
static char const * const months[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

static inline void http_update_date_cache(struct http_date *out, time_t now)
{
    if (out->date != now) {
        struct tm tm;

        gmtime_r(&now, &tm);
        sprintf(out->buf, "Date: %s, %02d %s %04d %02d:%02d:%02d GMT\r\n",
                days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
                tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
}

void httpd_put_date_hdr(outbuf_t *ob, const char *hdr, time_t now)
{
    struct tm tm;

    gmtime_r(&now, &tm);
    ob_addf(ob, "%s: %s, %02d %s %04d %02d:%02d:%02d GMT\r\n",
            hdr, days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
            tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* rfc 2616: §4.2: Message Headers */

/* FIXME: deal with quotes and similar stuff in 'ps' */
static ALWAYS_INLINE bool http_hdr_equals(pstream_t ps, const char *v)
{
    size_t vlen = strlen(v);

    if (ps_len(&ps) != vlen) {
        return false;
    }
    for (size_t i = 0; i < vlen; i++) {
        if (tolower(ps.b[i]) != v[i]) {
            return false;
        }
    }
    return true;
}

static bool http_hdr_contains(pstream_t ps, const char *v)
{
    pstream_t tmp = ps_initptr(NULL, NULL);

    while (ps_get_ps_chr(&ps, ',', &tmp) == 0) {
        ps_trim(&tmp);
        __ps_skip(&ps, 1);

        if (http_hdr_equals(tmp, v)) {
            return true;
        }
    }
    ps_trim(&ps);
    return http_hdr_equals(ps, v);
}

/* rfc 2616: §5.1: Request Line */

static int t_urldecode(httpd_qinfo_t *rq, pstream_t ps)
{
    char *buf  = t_new_raw(char, ps_len(&ps) + 1);
    char *p    = buf;

    rq->vars = ps_initptr(NULL, NULL);

    while (!ps_done(&ps)) {
        int c = __ps_getc(&ps);

        if (c == '+') {
            c = ' ';
        } else
        if (c == '%') {
            c = RETHROW(ps_hexdecode(&ps));
        }
        if (c == '?') {
            *p++ = '\0';
            rq->vars = ps;
            break;
        }
        *p++ = c;
    }
    *p++ = '\0';

    path_simplify2(buf, true);
    rq->prefix = ps_initptr(NULL, NULL);
    rq->query  = ps_initstr(buf);
    return 0;
}

static int ps_get_ver(pstream_t *ps)
{
    int i = ps_geti(ps);

    PS_WANT(i >= 0 && i < 128);
    return i;
}

static int t_http_parse_request_line(pstream_t *ps, unsigned max_len,
                                     httpd_qinfo_t *req)
{
    pstream_t line, method, uri;

    do {
        PARSE_RETHROW(http_getline(ps, max_len, &line));
    } while (ps_len(&line) == 0);

    PS_CHECK(ps_get_ps_chr(&line, ' ', &method));
    __ps_skip(&line, 1);

    switch (http_get_token_ps(method)) {
#define CASE(c)  case HTTP_TK_##c: req->method = HTTP_METHOD_##c; break
        CASE(CONNECT);
        CASE(DELETE);
        CASE(GET);
        CASE(HEAD);
        CASE(OPTIONS);
        CASE(POST);
        CASE(PUT);
        CASE(TRACE);
      default:
        req->method = HTTP_METHOD_ERROR;
        return PARSE_ERROR;
#undef  CASE
    }

    uri = ps_initptr(NULL, NULL);
    PS_CHECK(ps_get_ps_chr(&line, ' ', &uri));
    __ps_skip(&line, 1);

    if (ps_skipstr(&uri, "http://") == 0 || ps_skipstr(&uri, "https://") == 0)
    {
        PS_CHECK(ps_get_ps_chr(&uri, '/', &req->host));
    } else {
        p_clear(&req->host, 1);
        if (uri.s[0] != '/' && !ps_memequal(&uri, "*", 1)) {
            return PARSE_ERROR;
        }
    }
    RETHROW(t_urldecode(req, uri));
    PS_CHECK(ps_skipstr(&line, "HTTP/"));
    if (ps_len(&line) == 0 || !isdigit(line.b[0])) {
        return PARSE_ERROR;
    }
    req->http_version  = RETHROW(ps_get_ver(&line)) << 8;
    if (ps_getc(&line) != '.' || ps_len(&line) == 0 || !isdigit(line.b[0])) {
        return PARSE_ERROR;
    }
    req->http_version |= RETHROW(ps_get_ver(&line));
    return ps_len(&line) ? PARSE_ERROR : 0;
}

/* rfc 2616: §6.1: Status Line */

static inline int
http_parse_status_line(pstream_t *ps, unsigned max_len, httpc_qinfo_t *qi)
{
    pstream_t line, code;

    PARSE_RETHROW(http_getline(ps, max_len, &line));

    if (ps_skipstr(&line, "HTTP/")) {
        return PARSE_ERROR;
    }
    if (ps_len(&line) == 0 || !isdigit(line.b[0])) {
        return PARSE_ERROR;
    }
    qi->http_version  = RETHROW(ps_get_ver(&line)) << 8;
    if (ps_getc(&line) != '.' || ps_len(&line) == 0 || !isdigit(line.b[0])) {
        return PARSE_ERROR;
    }
    qi->http_version |= RETHROW(ps_get_ver(&line));
    __ps_skip(&line, 1);

    if (ps_get_ps_chr(&line, ' ', &code) || ps_len(&code) != 3) {
        return PARSE_ERROR;
    }
    __ps_skip(&line, 1);

    qi->code = ps_geti(&code);
    if ((int)qi->code < 100 || (int)qi->code >= 600) {
        return PARSE_ERROR;
    }
    qi->reason = line;
    return PARSE_OK;
}

#undef PARSE_RETHROW

static void http_chunk_patch(outbuf_t *ob, char *buf, unsigned len)
{
    if (len == 0) {
        sb_shrink(&ob->sb, 12);
        ob->length      -= 12;
        ob->sb_trailing -= 12;
    } else {
        buf[0] = '\r';
        buf[1] = '\n';
        buf[2] = __str_digits_lower[(len >> 28) & 0xf];
        buf[3] = __str_digits_lower[(len >> 24) & 0xf];
        buf[4] = __str_digits_lower[(len >> 20) & 0xf];
        buf[5] = __str_digits_lower[(len >> 16) & 0xf];
        buf[6] = __str_digits_lower[(len >> 12) & 0xf];
        buf[7] = __str_digits_lower[(len >>  8) & 0xf];
        buf[8] = __str_digits_lower[(len >>  4) & 0xf];
        buf[9] = __str_digits_lower[(len >>  0) & 0xf];
        buf[10] = '\r';
        buf[11] = '\n';
    }
}

#define CLENGTH_RESERVE  12

static void
http_clength_patch(outbuf_t *ob, char s[static CLENGTH_RESERVE], unsigned len)
{
    (sprintf)(s, "%10d\r", len);
    s[CLENGTH_RESERVE - 1] = '\n';
}

/* }}} */
/* HTTPD Queries {{{ */

/*
 * HTTPD queries refcounting holds:
 *  - 1 for the fact that it has an owner.
 *  - 1 for the fact it hasn't been answered yet.
 *  - 1 for the fact it hasn't been parsed yet.
 * Hence it's obj_retained() on creation, always.
 */

httpd_qinfo_t *httpd_qinfo_dup(const httpd_qinfo_t *info)
{
    httpd_qinfo_t *res;
    size_t len = sizeof(info);
    void *p;
    intptr_t offs;

    len += sizeof(info->hdrs[0]) * info->hdrs_len;
    len += ps_len(&info->host);
    len += ps_len(&info->prefix);
    len += ps_len(&info->query);
    len += ps_len(&info->vars);
    len += ps_len(&info->hdrs_ps);

    res  = p_new_extra(httpd_qinfo_t, len);
    memcpy(res, info, offsetof(httpd_qinfo_t, host));
    res->hdrs          = (void *)&res[1];
    p                  = res->hdrs + res->hdrs_len;
    res->host.s        = p;
    res->host.s_end    = p = mempcpy(p, info->host.s, ps_len(&info->host));
    res->prefix.s      = p;
    res->prefix.s_end  = p = mempcpy(p, info->prefix.s, ps_len(&info->prefix));
    res->query.s       = p;
    res->query.s_end   = p = mempcpy(p, info->query.s, ps_len(&info->query));
    res->vars.s        = p;
    res->vars.s_end    = p = mempcpy(p, info->vars.s, ps_len(&info->vars));
    res->hdrs_ps.s     = p;
    res->hdrs_ps.s_end = mempcpy(p, info->hdrs_ps.s, ps_len(&info->hdrs_ps));

    offs = res->hdrs_ps.s - info->hdrs_ps.s;
    for (int i = 0; i < res->hdrs_len; i++) {
        http_qhdr_t       *lhs = &res->hdrs[i];
        const http_qhdr_t *rhs = &info->hdrs[i];

        lhs->wkhdr = rhs->wkhdr;
        lhs->key   = ps_initptr(rhs->key.s + offs, rhs->key.s_end + offs);
        lhs->val   = ps_initptr(rhs->val.s + offs, rhs->val.s_end + offs);
    }
    return res;
}

static httpd_query_t *httpd_query_create(httpd_t *w, httpd_trigger_t *cb)
{
    httpd_query_t *q;

    if (cb) {
        q = obj_new_of_class(httpd_query, cb->query_cls);
    } else {
        q = obj_new(httpd_query);
    }

    if (w->queries == 0) {
        q->ob = &w->ob;
    }
    /* ensure refcount is 3: owned, unanwsered, unparsed */
    obj_retain(q);
    obj_retain(q);
    q->owner = w;
    dlist_add_tail(&w->query_list, &q->query_link);
    if (cb) {
        q->trig_cb = httpd_trigger_dup(cb);
    }
    return q;
}

static ALWAYS_INLINE void httpd_query_detach(httpd_query_t *q)
{
    httpd_t *w = q->owner;

    if (w) {
        if (!q->own_ob) {
            q->ob = NULL;
        }
        dlist_remove(&q->query_link);
        if (q->parsed) {
            w->queries--;
        }
        w->queries_done -= q->answered;
        q->owner = NULL;
        obj_release(&q);
    }
}

static httpd_query_t *httpd_query_init(httpd_query_t *q)
{
    sb_init(&q->payload);
    q->http_version = HTTP_1_1;
    return q;
}

static void httpd_query_wipe(httpd_query_t *q)
{
    if (q->trig_cb) {
        if (q->trig_cb->on_query_wipe) {
            q->trig_cb->on_query_wipe(q);
        }
        httpd_trigger_delete(&q->trig_cb);
    }
    if (q->own_ob) {
        ob_delete(&q->ob);
    }
    httpd_qinfo_delete(&q->qinfo);
    sb_wipe(&q->payload);
    httpd_query_detach(q);
}

static void httpd_query_on_data_bufferize(httpd_query_t *q, pstream_t ps)
{
    size_t plen = ps_len(&ps);

    if (unlikely(plen + q->payload.len > q->payload_max_size)) {
        httpd_reject(q, REQUEST_ENTITY_TOO_LARGE,
                     "payload is larger than %d octets",
                     q->payload_max_size);
        return;
    }
    sb_add(&q->payload, ps.s, plen);
}

void httpd_bufferize(httpd_query_t *q, unsigned maxsize)
{
    const httpd_qinfo_t *inf = q->qinfo;

    q->payload_max_size = maxsize;
    q->on_data          = &httpd_query_on_data_bufferize;
    if (!inf) {
        return;
    }
    for (int i = inf->hdrs_len; i-- > 0; ) {
        if (inf->hdrs[i].wkhdr == HTTP_WKHDR_CONTENT_LENGTH) {
            uint64_t len = strtoull(inf->hdrs[i].val.s, NULL, 0);

            if (unlikely(len > maxsize)) {
                httpd_reject(q, REQUEST_ENTITY_TOO_LARGE,
                             "payload is larger than %d octets", maxsize);
            } else {
                sb_grow(&q->payload, len);
            }
            return;
        }
    }
}

OBJ_VTABLE(httpd_query)
    httpd_query.init     = httpd_query_init;
    httpd_query.wipe     = httpd_query_wipe;
OBJ_VTABLE_END()


/*---- low level httpd_query reply functions ----*/

outbuf_t *httpd_reply_hdrs_start(httpd_query_t *q, int code, bool force_uncacheable)
{
    outbuf_t *ob = httpd_get_ob(q);

    http_update_date_cache(&date_cache_g, lp_getsec());

    assert (!q->hdrs_started && !q->hdrs_done);

    q->answer_code = code;
    ob_addf(ob, "HTTP/1.%d %d %*pM\r\n", HTTP_MINOR(q->http_version),
            code, LSTR_FMT_ARG(http_code_to_str(code)));
    ob_add(ob, date_cache_g.buf, sizeof(date_cache_g.buf) - 1);
    ob_adds(ob, "Accept-Encoding: identity, gzip, deflate\r\n");

    /* XXX: For CORS purposes, allow all origins for now */
    ob_adds(ob, "Access-Control-Allow-Origin: *\r\n");

    if (q->owner && q->owner->connection_close) {
        if (!q->conn_close) {
            ob_adds(ob, "Connection: close\r\n");
            q->conn_close = true;
        }
    }
    if (force_uncacheable) {
        ob_adds(ob,
                "Cache-Control: no-store, no-cache, must-revalidate\r\n"
                "Pragma: no-cache\r\n");
    }
    q->hdrs_started = true;
    return ob;
}

void httpd_reply_hdrs_done(httpd_query_t *q, int clen, bool chunked)
{
    outbuf_t *ob = httpd_get_ob(q);

    assert (!q->hdrs_done);
    q->hdrs_done = true;

    if (clen >= 0) {
        ob_addf(ob, "Content-Length: %d\r\n\r\n", clen);
        return;
    }

    if (chunked) {
        if (likely(q->http_version != HTTP_1_0)) {
            q->chunked = true;
            ob_adds(ob, "Transfer-Encoding: chunked\r\n");
            /* XXX: no \r\n because http_chunk_patch adds it */
        } else {
            /* FIXME: we aren't allowed to fallback to the non chunked case
             *        here because it would break assumptions from the caller
             *        that it can stream the answer with returns in the event
             *        loop
             */
            if (!q->conn_close) {
                ob_adds(ob, "Connection: close\r\n");
                q->conn_close = true;
            }
            if (q->owner) {
                q->owner->connection_close = true;
            }
            ob_adds(ob, "\r\n");
        }
    } else {
        q->clength_hack = true;
        ob_adds(ob, "Content-Length: ");
        q->chunk_hdr_offs    = ob_reserve(ob, CLENGTH_RESERVE);
        ob_adds(ob, "\r\n");
        q->chunk_prev_length = ob->length;
    }
}

void httpd_reply_chunk_done_(httpd_query_t *q, outbuf_t *ob)
{
    assert (q->chunk_started);
    q->chunk_started = false;
    http_chunk_patch(ob, ob->sb.data + q->chunk_hdr_offs,
                     ob->length - q->chunk_prev_length);
}


__attribute__((format(printf, 4, 0)))
static void httpd_notify_status(httpd_t *w, httpd_query_t *q, int handler,
                              const char *fmt, va_list va);

void httpd_reply_done(httpd_query_t *q)
{
    va_list va;
    outbuf_t *ob = httpd_get_ob(q);

    assert (q->hdrs_done && !q->answered && !q->chunk_started);
    if (q->chunked) {
        ob_adds(ob, "\r\n0\r\n\r\n");
    }
    if (q->clength_hack) {
        http_clength_patch(ob, ob->sb.data + q->chunk_hdr_offs,
                           ob->length - q->chunk_prev_length);
        q->clength_hack = false;
    }
    httpd_notify_status(q->owner, q, HTTPD_QUERY_STATUS_ANSWERED, "", va);
    httpd_mark_query_answered(q);
}

static void httpd_set_mask(httpd_t *w);

void httpd_signal_write(httpd_query_t *q)
{
    httpd_t *w = q->owner;

    if (w) {
        assert (q->hdrs_done && !q->answered && !q->chunk_started);
        httpd_set_mask(w);
    }
}

/*---- high level httpd_query reply functions ----*/

static ALWAYS_INLINE void httpd_query_reply_100continue_(httpd_query_t *q)
{
    if (q->answered || q->hdrs_started) {
        return;
    }
    if (q->expect100cont) {
        ob_addf(httpd_get_ob(q), "HTTP/1.%d 100 Continue\r\n\r\n",
                HTTP_MINOR(q->http_version));
        q->expect100cont = false;
    }
}

void httpd_reply_100continue(httpd_query_t *q)
{
    httpd_query_reply_100continue_(q);
}

void httpd_reply_202accepted(httpd_query_t *q)
{
    if (q->answered || q->hdrs_started) {
        return;
    }

    httpd_reply_hdrs_start(q, HTTP_CODE_ACCEPTED, false);
    httpd_reply_hdrs_done(q, 0, false);
    httpd_reply_done(q);
}

void httpd_reject_(httpd_query_t *q, int code, const char *fmt, ...)
{
    va_list ap;
    outbuf_t *ob;

    if (q->answered || q->hdrs_started) {
        return;
    }

    ob = httpd_reply_hdrs_start(q, code, false);
    ob_adds(ob, "Content-Type: text/html\r\n");
    httpd_reply_hdrs_done(q, -1, false);

    ob_addf(ob, "<html><body><h1>%d - %*pM</h1><p>",
            code, LSTR_FMT_ARG(http_code_to_str(code)));
    va_start(ap, fmt);
    ob_addvf(ob, fmt, ap);
    va_end(ap);
    ob_adds(ob, "</p></body></html>\r\n");

    va_start(ap, fmt);
    httpd_notify_status(q->owner, q, HTTPD_QUERY_STATUS_ANSWERED, fmt, ap);
    va_end(ap);
    httpd_reply_done(q);
}

void httpd_reject_unauthorized(httpd_query_t *q, lstr_t auth_realm)
{
    const lstr_t body = LSTR("<html><body>"
                             "<h1>401 - Authentication required</h1>"
                             "</body></html>\r\n");
    va_list va;
    outbuf_t *ob;

    if (q->answered || q->hdrs_started) {
        return;
    }

    ob = httpd_reply_hdrs_start(q, HTTP_CODE_UNAUTHORIZED, false);
    ob_adds(ob, "Content-Type: text/html\r\n");
    ob_addf(ob, "WWW-Authenticate: Basic realm=\"%*pM\"\r\n",
            LSTR_FMT_ARG(auth_realm));
    httpd_reply_hdrs_done(q, body.len, false);
    ob_add(ob, body.s, body.len);

    httpd_notify_status(q->owner, q, HTTP_CODE_UNAUTHORIZED, "", va);
    httpd_reply_done(q);
}

/* }}} */
/* HTTPD Triggers {{{ */

static httpd_trigger_node_t *
httpd_trigger_node_new(httpd_trigger_node_t *parent, lstr_t path)
{
    httpd_trigger_node_t *node;
    uint32_t pos;

    pos = qm_put(http_path, &parent->childs, &path, NULL, 0);
    if (pos & QHASH_COLLISION) {
        return parent->childs.values[pos & ~QHASH_COLLISION];
    }

    parent->childs.values[pos] = node =
        p_new_extra(httpd_trigger_node_t, path.len + 1);
    qm_init_cached(http_path, &node->childs);
    memcpy(node->path, path.s, path.len + 1);

    /* Ensure the key point to a valid string since path may be deallocated */
    parent->childs.keys[pos] = LSTR_INIT_V(node->path, path.len);
    return node;
}

static void httpd_trigger_node_wipe(httpd_trigger_node_t *node);
GENERIC_DELETE(httpd_trigger_node_t, httpd_trigger_node);

static void httpd_trigger_node_wipe(httpd_trigger_node_t *node)
{
    httpd_trigger_delete(&node->cb);
    qm_deep_wipe(http_path, &node->childs, IGNORE, httpd_trigger_node_delete);
}

bool httpd_trigger_register_flags(httpd_trigger_node_t *n, const char *path,
                                  httpd_trigger_t *cb, bool overwrite)
{
    while (*path == '/')
        path++;
    while (*path) {
        const char  *q = strchrnul(path, '/');
        lstr_t s = LSTR_INIT(path, q - path);

        n = httpd_trigger_node_new(n, s);
        while (*q == '/')
            q++;
        path = q;
    }
    if (!overwrite && n->cb) {
        return false;
    }
    httpd_trigger_delete(&n->cb);
    n->cb = httpd_trigger_dup(cb);
    if (unlikely(cb->query_cls == NULL)) {
        cb->query_cls = obj_class(httpd_query);
    }
    return true;
}

static bool httpd_trigger_unregister__(httpd_trigger_node_t *n, const char *path,
                                       httpd_trigger_t *what, bool *res)
{
    while (*path == '/')
        path++;

    if (!*path) {
        if (!what || n->cb == what) {
            httpd_trigger_delete(&n->cb);
            *res = true;
        } else {
            *res = false;
        }
    } else {
        const char *q = strchrnul(path, '/');
        lstr_t      s = LSTR_INIT(path, q - path);
        int pos       = qm_find(http_path, &n->childs, &s);

        if (pos < 0) {
            return false;
        }
        if (httpd_trigger_unregister__(n->childs.values[pos], q, what, res)) {
            httpd_trigger_node_delete(&n->childs.values[pos]);
            qm_del_at(http_path, &n->childs, pos);
        }
    }
    return qm_len(http_path, &n->childs) == 0;
}

bool httpd_trigger_unregister_(httpd_trigger_node_t *n, const char *path,
                               httpd_trigger_t *what)
{
    bool res = false;

    httpd_trigger_unregister__(n, path, what, &res);
    return res;
}

/* XXX: assumes path is canonical wrt '/' and starts with one */
static httpd_trigger_t *
httpd_trigger_resolve(httpd_trigger_node_t *n, httpd_qinfo_t *req)
{
    httpd_trigger_t *res = n->cb;
    const char *p = req->query.s;
    const char *q = req->query.s_end;

    req->prefix = ps_initptr(p, p);
    while (p++ < q) {
        lstr_t s;
        int pos;

        s.s   = p;
        p     = memchr(p, '/', q - p) ?: q;
        s.len = p - s.s;
        pos   = qm_find(http_path, &n->childs, &s);
        if (pos < 0) {
            break;
        }
        n = n->childs.values[pos];
        if (n->cb) {
            res = n->cb;
            req->query.s = req->prefix.s_end = p;
        }
    }
    return res;
}

/* }}} */
/* HTTPD Parser {{{ */

static inline void t_ps_get_http_var_parse_elem(pstream_t elem, lstr_t *out)
{
    if (memchr(elem.p, '%', ps_len(&elem))) {
        sb_t sb;

        t_sb_init(&sb, ps_len(&elem));
        sb_add_lstr_urldecode(&sb, LSTR_PS_V(&elem));
        *out = lstr_init_(sb.data, sb.len, MEM_STACK);
    } else {
        *out = LSTR_PS_V(&elem);
    }
}

int t_ps_get_http_var(pstream_t *ps, lstr_t *key, lstr_t *value)
{
    pstream_t key_ps, value_ps;

    RETHROW(ps_get_ps_chr_and_skip(ps, '=', &key_ps));
    THROW_ERR_IF(ps_done(&key_ps));

    if (ps_get_ps_chr_and_skip(ps, '&', &value_ps) < 0) {
        RETHROW(ps_get_ps(ps, ps_len(ps), &value_ps));
    }

    t_ps_get_http_var_parse_elem(key_ps,   key);
    t_ps_get_http_var_parse_elem(value_ps, value);

    return 0;
}

__attribute__((format(printf, 4, 0)))
static void httpd_notify_status(httpd_t *w, httpd_query_t *q, int handler,
                              const char *fmt, va_list va)
{
    if (!q->status_sent) {
        q->status_sent = true;

        if (w && w->on_status) {
            (*w->on_status)(w, q, handler, fmt, va);
        }
    }
}

static void httpd_set_mask(httpd_t *w)
{
    int mask;

    /* XXX: upstream httpd objects (for http2 server) have no fd (ev). */
    if (!w->ev) {
        return;
    }

    if (w->queries >= w->cfg->pipeline_depth
    ||  w->ob.length >= (int)w->cfg->outbuf_max_size
    ||  w->state == HTTP_PARSER_CLOSE)
    {
        mask = 0;
    } else {
        mask = POLLIN;
    }

    if (!ob_is_empty(&w->ob)) {
        mask |= POLLOUT;
    }

    if (w->ssl) {
        if (SSL_want_read(w->ssl)) {
            mask |= POLLIN;
        }
        if (SSL_want_write(w->ssl)) {
            mask |= POLLOUT;
        }
    }

    el_fd_set_mask(w->ev, mask);
}

static void httpd_flush_answered(httpd_t *w)
{
    dlist_for_each_entry(httpd_query_t, q, &w->query_list, query_link) {
        if (q->own_ob) {
            ob_merge_delete(&w->ob, &q->ob);
            q->own_ob = false;
        }
        if (!q->answered) {
            q->ob = &w->ob;
            break;
        }
        if (likely(q->parsed)) {
            httpd_query_detach(q);
        }
    }
    httpd_set_mask(w);
}

static void httpd_query_done(httpd_t *w, httpd_query_t *q)
{
    struct timeval now;

    lp_gettv(&now);
    q->query_sec  = now.tv_sec;
    q->query_usec = now.tv_usec;
    q->parsed     = true;
    w->queries++;
    httpd_flush_answered(w);
    if (w->connection_close) {
        w->state = HTTP_PARSER_CLOSE;
    } else {
        w->state = HTTP_PARSER_IDLE;
    }
    w->chunk_length = 0;
    obj_release(&q);
}

static void httpd_mark_query_answered(httpd_query_t *q)
{
    assert (!q->answered);
    q->answered = true;
    q->on_data  = NULL;
    q->on_done  = NULL;
    q->on_ready = NULL;
    if (q->owner) {
        httpd_t *w = q->owner;

        w->queries_done++;
        if (dlist_is_first(&w->query_list, &q->query_link)) {
            httpd_flush_answered(w);
        }
    }
    q->expect100cont = false;
    obj_release(&q);
}

qvector_t(qhdr, http_qhdr_t);
static void httpd_do_any(httpd_t *w, httpd_query_t *q, httpd_qinfo_t *req);
static void httpd_do_trace(httpd_t *w, httpd_query_t *q, httpd_qinfo_t *req);

static int httpd_parse_idle(httpd_t *w, pstream_t *ps)
{
    t_scope;
    size_t start = w->chunk_length > 4 ? w->chunk_length - 4 : 0;
    httpd_qinfo_t req;
    const uint8_t *p;
    pstream_t buf;
    int clen = -1;
    bool chunked = false;
    httpd_query_t *q;
    qv_t(qhdr) hdrs;
    httpd_trigger_t *cb = NULL;
    struct timeval now;

    if ((p = memmem(ps->s + start, ps_len(ps) - start, "\r\n\r\n", 4)) == NULL) {
        if (ps_len(ps) > w->cfg->header_size_max) {
            q = httpd_query_create(w, NULL);
            httpd_reject(q, FORBIDDEN, "Headers exceed %d octets",
                         w->cfg->header_size_max);
            goto unrecoverable_error;
        }
        w->chunk_length = ps_len(ps);
        return PARSE_MISSING_DATA;
    }

    if (--w->max_queries == 0) {
        w->connection_close = true;
    }

    http_zlib_reset(w);
    req.hdrs_ps = ps_initptr(ps->s, p + 4);

    switch (t_http_parse_request_line(ps, w->cfg->header_line_max, &req)) {
      case PARSE_ERROR:
        q = httpd_query_create(w, NULL);
        httpd_reject(q, BAD_REQUEST, "Invalid request line");
        goto unrecoverable_error;

      case PARSE_MISSING_DATA:
        return PARSE_MISSING_DATA;

      default:
        break;
    }

    if ((unsigned)req.method < countof(w->cfg->roots)) {
        cb = httpd_trigger_resolve(&w->cfg->roots[req.method], &req);
    }
    q = httpd_query_create(w, cb);
    q->received_hdr_length = ps_len(&req.hdrs_ps);
    q->http_version = req.http_version;
    q->qinfo        = &req;
    buf = __ps_get_ps_upto(ps, p + 2);
    __ps_skip_upto(ps, p + 4);
    switch (req.http_version) {
      case HTTP_1_0:
        /* TODO: support old-style Keep-Alive ? */
        w->connection_close = true;
        break;
      case HTTP_1_1:
        break;
      default:
        httpd_reject(q, NOT_IMPLEMENTED,
                     "This server requires an HTTP/1.1 compatible client");
        goto unrecoverable_error;
    }

    lp_gettv(&now);
    q->query_sec  = now.tv_sec;
    q->query_usec = now.tv_usec;
    t_qv_init(&hdrs, 64);

    while (!ps_done(&buf)) {
        http_qhdr_t *qhdr = qv_growlen(&hdrs, 1);

        /* TODO: normalize, make "lists" */
        qhdr->key = ps_get_cspan(&buf, &http_non_token);
        if (ps_len(&qhdr->key) == 0 || __ps_getc(&buf) != ':') {
            httpd_reject(q, BAD_REQUEST,
                         "Header name is empty or not followed by a colon");
            goto unrecoverable_error;
        }
        qhdr->val.s = buf.s;
        for (;;) {
            ps_skip_afterchr(&buf, '\r');
            if (__ps_getc(&buf) != '\n') {
                httpd_reject(q, BAD_REQUEST,
                             "CR is not followed by a LF in headers");
                goto unrecoverable_error;
            }
            qhdr->val.s_end = buf.s - 2;
            if (ps_done(&buf)) {
                break;
            }
            if (buf.s[0] != '\t' && buf.s[0] != ' ') {
                break;
            }
            __ps_skip(&buf, 1);
        }
        ps_trim(&qhdr->val);

        switch ((qhdr->wkhdr = http_wkhdr_from_ps(qhdr->key))) {
          case HTTP_WKHDR_HOST:
            if (ps_len(&req.host) == 0) {
                req.host = qhdr->val;
            }
            qv_shrink(&hdrs, 1);
            break;

          case HTTP_WKHDR_EXPECT:
            q->expect100cont |= http_hdr_equals(qhdr->key, "100-continue");
            break;

          case HTTP_WKHDR_CONNECTION:
            w->connection_close |= http_hdr_contains(qhdr->val, "close");
            break;

          case HTTP_WKHDR_TRANSFER_ENCODING:
            /* rfc 2616: §4.4: != "identity" means chunked encoding */
            switch (http_get_token_ps(qhdr->val)) {
              case HTTP_TK_IDENTITY:
                chunked = false;
                break;
              case HTTP_TK_CHUNKED:
                chunked = true;
                break;
              default:
                httpd_reject(q, NOT_IMPLEMENTED,
                             "Transfer-Encoding %*pM is unimplemented",
                             (int)ps_len(&qhdr->val), qhdr->val.s);
                break;
            }
            break;

          case HTTP_WKHDR_CONTENT_LENGTH:
            clen = memtoip(qhdr->val.b, ps_len(&qhdr->val), &p);
            if (p != qhdr->val.b_end) {
                httpd_reject(q, BAD_REQUEST, "Content-Length is unparseable");
                goto unrecoverable_error;
            }
            break;

          case HTTP_WKHDR_CONTENT_ENCODING:
            switch (http_get_token_ps(qhdr->val)) {
              case HTTP_TK_DEFLATE:
              case HTTP_TK_GZIP:
              case HTTP_TK_X_GZIP:
                http_zlib_inflate_init(w);
                qv_shrink(&hdrs, 1);
                break;
              default:
                http_zlib_reset(w);
                break;
            }
            break;

          default:
            break;
        }
    }

    if (chunked) {
        /* rfc 2616: §4.4: if chunked, then ignore any Content-Length */
        w->chunk_length = clen = 0;
        w->state = HTTP_PARSER_CHUNK_HDR;
    } else {
        w->chunk_length = clen < 0 ? 0 : clen;
        w->state = HTTP_PARSER_BODY;
    }
    req.hdrs      = hdrs.tab;
    req.hdrs_len  = hdrs.len;

    switch (req.method) {
      case HTTP_METHOD_TRACE:
        httpd_do_trace(w, q, &req);
        break;
      case HTTP_METHOD_POST:
      case HTTP_METHOD_PUT:
        if (clen < 0) {
            httpd_reject(q, LENGTH_REQUIRED, "");
            goto unrecoverable_error;
        }
        /* FALLTHROUGH */
      default:
        httpd_do_any(w, q, &req);
        break;
    }
    if (q->qinfo == &req) {
        q->qinfo = NULL;
    }
    httpd_query_reply_100continue_(q);
    return PARSE_OK;

  unrecoverable_error:
    if (q->qinfo == &req) {
        q->qinfo = NULL;
    }
    w->connection_close = true;
    httpd_query_done(w, q);
    return PARSE_ERROR;
}

static inline int
httpd_flush_data(httpd_t *w, httpd_query_t *q, pstream_t *ps, bool done)
{
    q->received_body_length += ps_len(ps);

    if (q->on_data) {
        if (w->compressed && !ps_done(ps)) {
            t_scope;
            sb_t zbuf;

            t_sb_init(&zbuf, HTTP_ZLIB_BUFSIZ);
            if (http_zlib_inflate(&w->zs, &w->chunk_length, &zbuf, ps, done)) {
                goto zlib_error;
            }
            q->on_data(q, ps_initsb(&zbuf));
            return PARSE_OK;
        }
        q->on_data(q, *ps);
    }
    w->chunk_length -= ps_len(ps);
    ps->b = ps->b_end;
    return PARSE_OK;

  zlib_error:
    httpd_reject(q, BAD_REQUEST, "Invalid compressed data");
    w->connection_close = true;
    httpd_query_done(w, q);
    return PARSE_ERROR;
}

static int httpd_parse_body(httpd_t *w, pstream_t *ps)
{
    httpd_query_t *q = dlist_last_entry(&w->query_list, httpd_query_t, query_link);
    ssize_t plen = ps_len(ps);

    q->expect100cont = false;
    assert (w->chunk_length >= 0);
    if (plen >= w->chunk_length) {
        pstream_t tmp = __ps_get_ps(ps, w->chunk_length);

        RETHROW(httpd_flush_data(w, q, &tmp, true));
        if (q->on_done) {
            q->on_done(q);
        }
        httpd_query_done(w, q);
        return PARSE_OK;
    }

    if (plen >= w->cfg->on_data_threshold) {
        RETHROW(httpd_flush_data(w, q, ps, false));
    }
    return PARSE_MISSING_DATA;
}

/*
 * rfc 2616: §3.6.1: Chunked Transfer Coding
 *
 * - All chunked extensions are stripped (support is optionnal)
 * - trailer headers are ignored, as:
 *   + Clients must specifically ask for them (we won't)
 *   + or ignoring them should not modify behaviour (so we do ignore them).
 */
static int httpd_parse_chunk_hdr(httpd_t *w, pstream_t *ps)
{
    httpd_query_t *q = dlist_last_entry(&w->query_list, httpd_query_t, query_link);
    const char *orig = ps->s;
    pstream_t line, hex;
    uint64_t  len = 0;
    int res;

    q->expect100cont = false;
    res = http_getline(ps, w->cfg->header_line_max, &line);
    if (res > 0) {
        return res;
    }
    if (res < 0) {
        goto cancel_query;
    }
    http_skipspaces(&line);
    hex = ps_get_span(&line, &ctype_ishexdigit);
    http_skipspaces(&line);
    if (unlikely(ps_len(&line)) != 0 && unlikely(line.s[0] != ';')) {
        goto cancel_query;
    }
    if (unlikely(ps_len(&hex) == 0) || unlikely(ps_len(&hex) > 16)) {
        goto cancel_query;
    }
    for (const char *s = hex.s; s < hex.s_end; s++)
        len = (len << 4) | __str_digit_value[*s + 128];
    w->chunk_length = len;
    w->state = len ? HTTP_PARSER_CHUNK : HTTP_PARSER_CHUNK_TRAILER;
    q->received_body_length += ps->s - orig;
    return PARSE_OK;

  cancel_query:
    httpd_reject(q, BAD_REQUEST, "Chunked header is unparseable");
    w->connection_close = true;
    httpd_query_done(w, q);
    return PARSE_ERROR;
}

static int httpd_parse_chunk(httpd_t *w, pstream_t *ps)
{
    httpd_query_t *q = dlist_last_entry(&w->query_list, httpd_query_t, query_link);
    ssize_t plen = ps_len(ps);

    assert (w->chunk_length >= 0);
    if (plen >= w->chunk_length + 2) {
        pstream_t tmp = __ps_get_ps(ps, w->chunk_length);

        if (ps_skipstr(ps, "\r\n")) {
            httpd_reject(q, BAD_REQUEST, "Chunked header is unparseable");
            w->connection_close = true;
            httpd_query_done(w, q);
            return PARSE_ERROR;
        }
        RETHROW(httpd_flush_data(w, q, &tmp, false));
        w->state = HTTP_PARSER_CHUNK_HDR;
        return PARSE_OK;
    }
    if (plen >= w->cfg->on_data_threshold) {
        RETHROW(httpd_flush_data(w, q, ps, false));
    }
    return PARSE_MISSING_DATA;
}

static int httpd_parse_chunk_trailer(httpd_t *w, pstream_t *ps)
{
    httpd_query_t *q = dlist_last_entry(&w->query_list, httpd_query_t, query_link);
    const char *orig = ps->s;
    pstream_t line;

    do {
        int res = (http_getline(ps, w->cfg->header_line_max, &line));

        if (res < 0) {
            httpd_reject(q, BAD_REQUEST, "Trailer headers are unparseable");
            w->connection_close = true;
            httpd_query_done(w, q);
            return PARSE_ERROR;
        }
        if (res > 0) {
            return res;
        }
    } while (ps_len(&line));

    q->received_body_length += ps->s - orig;
    if (q->on_done) {
        q->on_done(q);
    }
    httpd_query_done(w, q);
    return PARSE_OK;
}

static int httpd_parse_close(httpd_t *w, pstream_t *ps)
{
    ps->b = ps->b_end;
    return PARSE_MISSING_DATA;
}


static int (*httpd_parsers[])(httpd_t *w, pstream_t *ps) = {
    [HTTP_PARSER_IDLE]          = httpd_parse_idle,
    [HTTP_PARSER_BODY]          = httpd_parse_body,
    [HTTP_PARSER_CHUNK_HDR]     = httpd_parse_chunk_hdr,
    [HTTP_PARSER_CHUNK]         = httpd_parse_chunk,
    [HTTP_PARSER_CHUNK_TRAILER] = httpd_parse_chunk_trailer,
    [HTTP_PARSER_CLOSE]         = httpd_parse_close,
};

/* }}} */
/* HTTPD {{{ */

httpd_cfg_t *httpd_cfg_init(httpd_cfg_t *cfg)
{
    core__httpd_cfg__t iop_cfg;

    p_clear(cfg, 1);

    dlist_init(&cfg->httpd_list);
    dlist_init(&cfg->http2_httpd_list);
    cfg->httpd_cls = obj_class(httpd);

    iop_init(core__httpd_cfg, &iop_cfg);
    /* Default configuration must succeed. */
    httpd_cfg_from_iop(cfg, &iop_cfg);

    for (int i = 0; i < countof(cfg->roots); i++) {
        qm_init_cached(http_path, &cfg->roots[i].childs);
    }
    return cfg;
}

static int
httpd_ssl_alpn_select_protocol_cb(SSL *ssl, const unsigned char **out,
                                  unsigned char *outlen,
                                  const unsigned char *in, unsigned int inlen,
                                  void *arg)
{
    http_mode_t mode = (intptr_t) arg;
    const byte *http2_found = NULL;
    const byte *http1_1_found = NULL;
    const byte *http1_0_found = NULL;
    bool look_for_h2 = mode == HTTP_MODE_USE_HTTP2_ONLY;
    pstream_t ps;
    const byte *chosen;

    /* XXX: This cb is invoked for clients that propose multiple protocols
     * (e.g., h2, http/1.1). Currently, we don't support HTTP version
     * negotiation so, in HTTP/2 (TLS) mode, we look for h2 only. */

    /* XXX: alpn protocol-list is a string of 8-bit length-prefixed byte
     * substrings. */
    ps = ps_init(in, inlen);
    while (!ps_done(&ps)) {
        int len = __ps_getc(&ps);

        if (!ps_has(&ps, len)) {
            break;
        }
        if (look_for_h2) {
            if (len == 2 && ps_startswithstr(&ps, "h2")) {
                http2_found = ps.b - 1;
                break;
            }
        } else {
            /* look for http/1.x */
            if (len == 8 && ps_startswithstr(&ps, "http/1.1")) {
                http1_1_found = ps.b - 1;
                break;
            }
            if (len == 8 && ps_startswithstr(&ps, "http/1.0")) {
                http1_0_found = ps.b - 1;
                break;
            }
        }
        __ps_skip(&ps, len);
    }
    chosen = http2_found ?: http1_1_found ?: http1_0_found;
    if (chosen) {
        *out = chosen + 1;
        *outlen = *chosen;
        return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_NOACK;
}


int httpd_cfg_from_iop(httpd_cfg_t *cfg, const core__httpd_cfg__t *iop_cfg)
{
    THROW_ERR_UNLESS(expect(!cfg->ssl_ctx));
    cfg->outbuf_max_size    = iop_cfg->outbuf_max_size;
    cfg->pipeline_depth     = iop_cfg->pipeline_depth;
    cfg->noact_delay        = iop_cfg->noact_delay;
    cfg->max_queries        = iop_cfg->max_queries;
    cfg->max_conns          = iop_cfg->max_conns_in;
    cfg->on_data_threshold  = iop_cfg->on_data_threshold;
    cfg->header_line_max    = iop_cfg->header_line_max;
    cfg->header_size_max    = iop_cfg->header_size_max;

    if (iop_cfg->tls) {
        SSL_CTX *ctx;
        core__tls_cert_and_key__t *data;
        SB_1k(errbuf);

        data = IOP_UNION_GET(core__tls_cfg, iop_cfg->tls, data);
        if (!data) {
            /* If a keyname has been provided in the configuration, it
             * should have been replaced by the actual TLS data. */
            logger_panic(&_G.logger, "TLS data are not provided");
        }

        ctx = ssl_ctx_new_tls(TLS_server_method(), data->key, data->cert,
                              SSL_VERIFY_NONE, NULL, &errbuf);
        httpd_cfg_set_ssl_ctx(cfg, ctx);
        if (!cfg->ssl_ctx) {
            logger_fatal(&_G.logger, "couldn't initialize SSL_CTX: %*pM",
                         SB_FMT_ARG(&errbuf));
        }
    }

    return 0;
}

void httpd_cfg_wipe(httpd_cfg_t *cfg)
{
    for (int i = 0; i < countof(cfg->roots); i++) {
        httpd_trigger_node_wipe(&cfg->roots[i]);
    }
    if (cfg->ssl_ctx) {
        SSL_CTX_free(cfg->ssl_ctx);
        cfg->ssl_ctx = NULL;
    }
    assert (dlist_is_empty(&cfg->httpd_list));
}

void httpd_cfg_set_ssl_ctx(httpd_cfg_t *nonnull cfg, SSL_CTX *nullable ctx)
{
    if (cfg->ssl_ctx) {
        SSL_CTX_free(cfg->ssl_ctx);
    }
    cfg->ssl_ctx = ctx;
    if (ctx) {
        SSL_CTX_set_alpn_select_cb(cfg->ssl_ctx,
                                   &httpd_ssl_alpn_select_protocol_cb,
                                   (void *)cfg->mode);
    }
}

static httpd_t *httpd_init(httpd_t *w)
{
    dlist_init(&w->query_list);
    dlist_init(&w->httpd_link);
    sb_init(&w->ibuf);
    ob_init(&w->ob);
    w->state = HTTP_PARSER_IDLE;
    return w;
}

static void httpd_wipe(httpd_t *w)
{
    if (w->on_status) {
        va_list va;

        dlist_for_each(it, &w->query_list) {
            httpd_notify_status(w, dlist_entry(it, httpd_query_t, query_link),
                              HTTPD_QUERY_STATUS_CANCEL, "Query cancelled", va);
        }
    }
    if (w->on_disconnect) {
        (*w->on_disconnect)(w);
    }
    el_unregister(&w->ev);
    sb_wipe(&w->ibuf);
    ob_wipe(&w->ob);
    http_zlib_wipe(w);
    dlist_for_each(it, &w->query_list) {
        httpd_query_detach(dlist_entry(it, httpd_query_t, query_link));
    }
    w->cfg->nb_conns--;
    dlist_remove(&w->httpd_link);
    httpd_cfg_delete(&w->cfg);
    lstr_wipe(&w->peer_address);
    SSL_free(w->ssl);
    w->ssl = NULL;
}

OBJ_VTABLE(httpd)
    httpd.init = httpd_init;
    httpd.wipe = httpd_wipe;
OBJ_VTABLE_END()

void httpd_close_gently(httpd_t *w)
{
    w->connection_close = true;
    if (w->state == HTTP_PARSER_IDLE) {
        w->state = HTTP_PARSER_CLOSE;
        /* let the event loop maybe destroy us later, not now */
        el_fd_set_mask(w->ev, POLLOUT);
    }
}

int t_httpd_qinfo_get_basic_auth(const httpd_qinfo_t *info,
                                 pstream_t *user, pstream_t *pw)
{
    for (int i = info->hdrs_len; i-- > 0; ) {
        const http_qhdr_t *hdr = info->hdrs + i;
        pstream_t v;
        char *colon;
        sb_t sb;
        int len;

        if (hdr->wkhdr != HTTP_WKHDR_AUTHORIZATION) {
            continue;
        }
        v = hdr->val;
        ps_skipspaces(&v);
        PS_CHECK(ps_skipcasestr(&v, "basic"));
        ps_trim(&v);

        len = ps_len(&v);
        t_sb_init(&sb, len + 1);
        PS_CHECK(sb_add_unb64(&sb, v.s, len));
        colon = strchr(sb.data, ':');
        if (!colon) {
            return -1;
        }
        *user    = ps_initptr(sb.data, colon);
        *colon++ = '\0';
        *pw      = ps_initptr(colon, sb_end(&sb));
        return 0;
    }

    *pw = *user = ps_initptr(NULL, NULL);
    return 0;
}

static int parse_qvalue(pstream_t *ps)
{
    int res;

    /* is there a ';' ? */
    if (ps_skipc(ps, ';') < 0) {
        return 1000;
    }
    ps_skipspaces(ps);

    /* parse q= */
    RETHROW(ps_skipc(ps, 'q'));
    ps_skipspaces(ps);
    RETHROW(ps_skipc(ps, '='));
    ps_skipspaces(ps);

    /* slopily parse 1[.000] || 0[.nnn] */
    switch (ps_getc(ps)) {
      case '0': res = 0; break;
      case '1': res = 1; break;
      default:
        return -1;
    }
    if (ps_skipc(ps, '.') == 0) {
        for (int i = 0; i < 3; i++) {
            if (ps_has(ps, 1) && isdigit(ps->s[0])) {
                res  = 10 * res + __ps_getc(ps) - '0';
            } else {
                res *= 10;
            }
        }
        if (res > 1000) {
            res = 1000;
        }
    } else {
        res *= 1000;
    }
    ps_skipspaces(ps);
    return res;
}

static int parse_accept_enc(pstream_t ps)
{
    int res_valid = 0, res_rej = 0, res_star = 0;
    int q;
    pstream_t v;

    ps_skipspaces(&ps);
    while (!ps_done(&ps)) {
        bool is_star = false;

        if (*ps.s == '*') {
            is_star = true;
            __ps_skip(&ps, 1);
        } else {
            v = ps_get_cspan(&ps, &http_non_token);
        }
        ps_skipspaces(&ps);
        q = RETHROW(parse_qvalue(&ps));
        switch (ps_getc(&ps)) {
          case ',':
            ps_skipspaces(&ps);
            break;
          case -1:
            break;
          default:
            return -1;
        }

        if (is_star) {
            res_star = q ? HTTPD_ACCEPT_ENC_ANY : 0;
        } else {
            switch (http_get_token_ps(v)) {
              case HTTP_TK_X_GZIP:
              case HTTP_TK_GZIP:
                if (q) {
                    res_valid |= HTTPD_ACCEPT_ENC_GZIP;
                } else {
                    res_rej   |= HTTPD_ACCEPT_ENC_GZIP;
                }
                break;
              case HTTP_TK_X_COMPRESS:
              case HTTP_TK_COMPRESS:
                if (q) {
                    res_valid |= HTTPD_ACCEPT_ENC_COMPRESS;
                } else {
                    res_rej   |= HTTPD_ACCEPT_ENC_COMPRESS;
                }
                break;
              case HTTP_TK_DEFLATE:
                if (q) {
                    res_valid |= HTTPD_ACCEPT_ENC_DEFLATE;
                } else {
                    res_rej   |= HTTPD_ACCEPT_ENC_DEFLATE;
                }
                break;
              default: /* Ignore "identity" or non RFC Accept-Encodings */
                break;
            }
        }
    }

    return (res_valid | res_star) & ~res_rej;
}

int httpd_qinfo_accept_enc_get(const httpd_qinfo_t *info)
{
    int res = 0;

    for (int i = info->hdrs_len; i-- > 0; ) {
        const http_qhdr_t *hdr = info->hdrs + i;

        if (hdr->wkhdr != HTTP_WKHDR_ACCEPT_ENCODING) {
            continue;
        }

        if ((res = parse_accept_enc(hdr->val)) >= 0) {
            return res;
        }
        /* ignore malformed header */
    }
    return 0;
}

static void httpd_do_any(httpd_t *w, httpd_query_t *q, httpd_qinfo_t *req)
{
    httpd_trigger_t *cb = q->trig_cb;
    pstream_t user, pw;

    if (ps_memequal(&req->query, "*", 1)) {
        httpd_reject(q, NOT_FOUND, "'*' not found");
        return;
    }

    if (cb) {
        if (cb->auth) {
            t_scope;

            if (unlikely(t_httpd_qinfo_get_basic_auth(req, &user, &pw) < 0)) {
                httpd_reject(q, BAD_REQUEST, "invalid Authentication header");
                return;
            }
            (*cb->auth)(cb, q, user, pw);
        }
        if (likely(!q->answered)) {
            (*cb->cb)(cb, q, req);
        }
    } else {
        int                   method = req->method;
        lstr_t                ms     = http_method_str[method];
        httpd_trigger_node_t *n      = &w->cfg->roots[method];

        if (n->cb || qm_len(http_path, &n->childs)) {
            SB_1k(escaped);

            sb_add_lstr_xmlescape(&escaped, LSTR_PS_V(&req->query));
            httpd_reject(q, NOT_FOUND,
                         "%*pM %*pM HTTP/1.%d", LSTR_FMT_ARG(ms),
                         SB_FMT_ARG(&escaped),
                         HTTP_MINOR(req->http_version));
        } else
        if (method == HTTP_METHOD_OPTIONS) {
            /* For CORS purposes, handle OPTIONS if not handled above */
            outbuf_t *ob = httpd_reply_hdrs_start(q, HTTP_CODE_NO_CONTENT,
                                                  false);

            ob_adds(ob, "Access-Control-Allow-Methods: "
                    "POST, GET, OPTIONS\r\n");
            ob_adds(ob, "Access-Control-Allow-Headers: "
                    "Authorization, Content-Type\r\n");

            httpd_reply_hdrs_done(q, 0, false);
            httpd_reply_done(q);
        } else {
            httpd_reject(q, NOT_IMPLEMENTED,
                         "no handler for %*pM", LSTR_FMT_ARG(ms));
        }
    }
}

static void httpd_do_trace(httpd_t *w, httpd_query_t *q, httpd_qinfo_t *req)
{
    httpd_reject(q, METHOD_NOT_ALLOWED, "TRACE method is not allowed");
}

static void httpd_do_close(httpd_t **w_)
{
    httpd_t *w = *w_;
    if (!dlist_is_empty(&w->query_list)) {
        httpd_query_t *q;
        q = dlist_last_entry(&w->query_list, httpd_query_t, query_link);
        if (!q->parsed) {
            obj_release(&q);
            if (!q->answered) {
                obj_release(&q);
            }
        }
    }
    obj_delete(w_);
}

static int httpd_on_event(el_t evh, int fd, short events, data_t priv)
{
    httpd_t *w = priv.ptr;
    pstream_t ps;

    if (events == EL_EVENTS_NOACT) {
        goto close;
    }

    if (events & POLLIN) {
        int ret;

        ret = w->ssl ?
            ssl_sb_read(&w->ibuf, w->ssl, 0):
            sb_read(&w->ibuf, fd, 0);
        if (ret <= 0) {
            if (ret == 0 || !ERR_RW_RETRIABLE(errno)) {
                goto close;
            }
            goto write;
        }

        ps = ps_initsb(&w->ibuf);
        do {
            ret = (*httpd_parsers[w->state])(w, &ps);
        } while (ret == PARSE_OK);
        sb_skip_upto(&w->ibuf, ps.s);
    }

  write:
    {
        int oldlen = w->ob.length;
        int ret;

        ret = w->ssl ?
            ob_write_with(&w->ob, fd, ssl_writev, w->ssl) :
            ob_write(&w->ob, fd);
        if (ret < 0 && !ERR_RW_RETRIABLE(errno)) {
            goto close;
        }

        if (!dlist_is_empty(&w->query_list)) {
            httpd_query_t *query = dlist_first_entry(&w->query_list,
                                                     httpd_query_t, query_link);
            if (!query->answered && query->on_ready != NULL
            && oldlen >= query->ready_threshold
            && w->ob.length < query->ready_threshold) {
                (*query->on_ready)(query);
            }
        }
    }

    if (unlikely(w->state == HTTP_PARSER_CLOSE)) {
        if (w->queries == 0 && ob_is_empty(&w->ob)) {
            /* XXX We call shutdown(…, SHUT_RW) to force TCP to flush our
             * writing buffer and protect our responses against a TCP RST
             * which could be emitted by close() if there is some pending data
             * in the read buffer (think about pipelining). */
            shutdown(fd, SHUT_WR);
            goto close;
        }
    } else {
        /* w->state == HTTP_PARSER_IDLE:
         *   queries > 0 means pending answer, client isn't lagging, we are.
         *
         * w->state != HTTP_PARSER_IDLE:
         *   queries is always > 0: the query being parsed has been created.
         *   So for this case, pending requests without answers exist iff
         *   queries > 1.
         */
        if (w->queries > (w->state != HTTP_PARSER_IDLE)) {
            el_fd_watch_activity(w->ev, POLLINOUT, 0);
        } else
        if (ob_is_empty(&w->ob)) {
            el_fd_watch_activity(w->ev, POLLINOUT, w->cfg->noact_delay);
        }
    }
    httpd_set_mask(w);
    return 0;

  close:
    httpd_do_close(&w);
    return 0;
}

static int
httpd_tls_handshake(el_t evh, int fd, short events, data_t priv)
{
    httpd_t *w = priv.ptr;

    switch (ssl_do_handshake(w->ssl, evh, fd, NULL)) {
      case SSL_HANDSHAKE_SUCCESS:
        el_fd_set_mask(evh, POLLIN);
        el_fd_set_hook(evh, httpd_on_event);
        break;
      case SSL_HANDSHAKE_PENDING:
        break;
      case SSL_HANDSHAKE_CLOSED:
        obj_delete(&w);
        break;
      case SSL_HANDSHAKE_ERROR:
        obj_delete(&w);
        return -1;
    }

    return 0;
}

static int
httpd_spawn_as_http2(int sockfd, sockunion_t *peer_su, httpd_cfg_t *cfg);

static int httpd_on_accept(el_t evh, int fd, short events, data_t priv)
{
    httpd_cfg_t *cfg = priv.ptr;
    int sock;
    sockunion_t su;

    while ((sock = acceptx_get_addr(fd, O_NONBLOCK, &su)) >= 0) {
        if (cfg->nb_conns >= cfg->max_conns) {
            close(sock);
        } else if (cfg->mode == HTTP_MODE_USE_HTTP2_ONLY) {
            return httpd_spawn_as_http2(sock, &su, cfg);
        } else {
            httpd_spawn(sock, cfg)->peer_su = su;
        }
    }
    return 0;
}

el_t httpd_listen(sockunion_t *su, httpd_cfg_t *cfg)
{
    int fd;

    fd = listenx(-1, su, 1, SOCK_STREAM, IPPROTO_TCP, O_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }
    return el_fd_register(fd, true, POLLIN, httpd_on_accept,
                          httpd_cfg_retain(cfg));
}

void httpd_unlisten(el_t *ev)
{
    if (*ev) {
        httpd_cfg_t *cfg = el_unregister(ev).ptr;

        dlist_for_each(it, &cfg->httpd_list) {
            httpd_close_gently(dlist_entry(it, httpd_t, httpd_link));
        }
        httpd_cfg_delete(&cfg);
    }
}

httpd_t *httpd_spawn(int fd, httpd_cfg_t *cfg)
{
    httpd_t *w = obj_new_of_class(httpd, cfg->httpd_cls);
    el_fd_f *el_cb = cfg->ssl_ctx ? &httpd_tls_handshake : &httpd_on_event;

    cfg->nb_conns++;
    w->cfg         = httpd_cfg_retain(cfg);
    w->ev          = el_fd_register(fd, true, POLLIN, el_cb, w);
    w->max_queries = cfg->max_queries;
    if (cfg->ssl_ctx) {
        w->ssl = SSL_new(cfg->ssl_ctx);
        assert (w->ssl);
        SSL_set_fd(w->ssl, fd);
        SSL_set_accept_state(w->ssl);
    }

    el_fd_watch_activity(w->ev, POLLINOUT, w->cfg->noact_delay);
    dlist_add_tail(&cfg->httpd_list, &w->httpd_link);
    if (w->on_accept) {
        (*w->on_accept)(w);
    }
    return w;
}

lstr_t   httpd_get_peer_address(httpd_t * w)
{
    if (!w->peer_address.len) {
        t_scope;

        w->peer_address = lstr_dup(t_addr_fmt_lstr(&w->peer_su));
    }

    return lstr_dupc(w->peer_address);
}

/* }}} */
/* HTTPC Parsers {{{ */

static httpc_qinfo_t *httpc_qinfo_dup(const httpc_qinfo_t *info)
{
    httpc_qinfo_t *res;
    size_t len = sizeof(info);
    void *p;
    intptr_t offs;

    len += sizeof(info->hdrs[0]) * info->hdrs_len;
    len += ps_len(&info->reason);
    len += ps_len(&info->hdrs_ps);

    res  = p_new_extra(httpc_qinfo_t, len);
    memcpy(res, info, offsetof(httpc_qinfo_t, hdrs_ps));
    res->hdrs          = (void *)&res[1];
    p                  = res->hdrs + res->hdrs_len;
    res->reason.s      = p;
    res->reason.s_end  = p = mempcpy(p, info->reason.s, ps_len(&info->reason));
    res->hdrs_ps.s     = p;
    res->hdrs_ps.s_end = mempcpy(p, info->hdrs_ps.s, ps_len(&info->hdrs_ps));

    offs = res->hdrs_ps.s - info->hdrs_ps.s;
    for (int i = 0; i < res->hdrs_len; i++) {
        http_qhdr_t       *lhs = &res->hdrs[i];
        const http_qhdr_t *rhs = &info->hdrs[i];

        lhs->wkhdr = rhs->wkhdr;
        lhs->key   = ps_initptr(rhs->key.s + offs, rhs->key.s_end + offs);
        lhs->val   = ps_initptr(rhs->val.s + offs, rhs->val.s_end + offs);
    }
    return res;
}

static void httpc_query_on_done(httpc_query_t *q, int status)
{
    httpc_t *w = q->owner;

    if (w) {
        if (--w->queries < w->cfg->pipeline_depth && w->max_queries && w->busy) {
            obj_vcall(w, set_ready, false);
        }
        q->owner = NULL;
    }
    dlist_remove(&q->query_link);
    /* XXX: call the httpc_t's notifier first to ensure qinfo is still set */
    if (w && w->on_query_done) {
        (*w->on_query_done)(w, q, status);
    }
    (*q->on_done)(q, status);
}
#define httpc_query_abort(q)  httpc_query_on_done(q, HTTPC_STATUS_ABORT)

static int httpc_query_ok(httpc_query_t *q)
{
    httpc_t *w = q->owner;

    httpc_query_on_done(q, HTTPC_STATUS_OK);
    if (w) {
        w->chunk_length = 0;
        w->state = HTTP_PARSER_IDLE;
    }
    return PARSE_OK;
}

static inline void httpc_qinfo_delete(httpc_qinfo_t **infop)
{
    p_delete(infop);
}

static int httpc_parse_idle(httpc_t *w, pstream_t *ps)
{
    t_scope;
    size_t start = w->chunk_length > 4 ? w->chunk_length - 4 : 0;
    httpc_qinfo_t req;
    const uint8_t *p;
    pstream_t buf;
    qv_t(qhdr) hdrs;
    httpc_query_t *q;
    bool chunked = false, conn_close = false;
    int clen = -1, res;

    if (ps_len(ps) > 0 && dlist_is_empty(&w->query_list)) {
        logger_trace(&_G.logger, 0, "UHOH spurious data from the HTTP "
                     "server: %*pM", (int)ps_len(ps), ps->s);
        return PARSE_ERROR;
    }

    if ((p = memmem(ps->s + start, ps_len(ps) - start, "\r\n\r\n", 4)) == NULL) {
        if (ps_len(ps) > w->cfg->header_size_max) {
            return PARSE_ERROR;
        }
        w->chunk_length = ps_len(ps);
        return PARSE_MISSING_DATA;
    }

    http_zlib_reset(w);
    req.hdrs_ps = ps_initptr(ps->s, p + 4);
    res = http_parse_status_line(ps, w->cfg->header_line_max, &req);
    if (res) {
        return res;
    }

    buf = __ps_get_ps_upto(ps, p + 2);
    __ps_skip_upto(ps, p + 4);
    t_qv_init(&hdrs, 64);

    while (!ps_done(&buf)) {
        http_qhdr_t *qhdr = qv_growlen(&hdrs, 1);

        /* TODO: normalize, make "lists" */
        qhdr->key = ps_get_cspan(&buf, &http_non_token);
        if (ps_len(&qhdr->key) == 0 || __ps_getc(&buf) != ':') {
            return PARSE_ERROR;
        }
        qhdr->val.s = buf.s;
        for (;;) {
            ps_skip_afterchr(&buf, '\r');
            if (__ps_getc(&buf) != '\n') {
                return PARSE_ERROR;
            }
            qhdr->val.s_end = buf.s - 2;
            if (ps_done(&buf)) {
                break;
            }
            if (buf.s[0] != '\t' && buf.s[0] != ' ') {
                break;
            }
            __ps_skip(&buf, 1);
        }
        ps_trim(&qhdr->val);

        switch ((qhdr->wkhdr = http_wkhdr_from_ps(qhdr->key))) {
          case HTTP_WKHDR_CONNECTION:
            conn_close |= http_hdr_contains(qhdr->val, "close");
            w->connection_close |= conn_close;
            break;

          case HTTP_WKHDR_TRANSFER_ENCODING:
            /* rfc 2616: §4.4: != "identity" means chunked encoding */
            switch (http_get_token_ps(qhdr->val)) {
              case HTTP_TK_IDENTITY:
                chunked = false;
                break;
              case HTTP_TK_CHUNKED:
                chunked = true;
                break;
              default:
                return PARSE_ERROR;
            }
            break;

          case HTTP_WKHDR_CONTENT_LENGTH:
            clen = memtoip(qhdr->val.b, ps_len(&qhdr->val), &p);
            if (p != qhdr->val.b_end) {
                return PARSE_ERROR;
            }
            break;

          case HTTP_WKHDR_CONTENT_ENCODING:
            switch (http_get_token_ps(qhdr->val)) {
              case HTTP_TK_DEFLATE:
              case HTTP_TK_GZIP:
              case HTTP_TK_X_GZIP:
                http_zlib_inflate_init(w);
                qv_shrink(&hdrs, 1);
                break;
              default:
                http_zlib_reset(w);
                break;
            }
            break;

          default:
            break;
        }
    }

    if (chunked) {
        /* rfc 2616: §4.4: if chunked, then ignore any Content-Length */
        w->chunk_length = 0;
        w->state = HTTP_PARSER_CHUNK_HDR;
    } else {
        /* rfc 2616: §4.4: support no Content-Length */
        if (clen < 0 && req.code == HTTP_CODE_NO_CONTENT) {
            /* due to code 204 (No Content) */
            w->chunk_length = 0;
        } else {
            /* or followed by close */
            w->chunk_length = clen;
        }
        w->state = HTTP_PARSER_BODY;
    }
    req.hdrs     = hdrs.tab;
    req.hdrs_len = hdrs.len;

    q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);

    if (req.code >= 100 && req.code < 200) {
        w->state = HTTP_PARSER_IDLE;

        /* rfc 2616: §10.1: A client MUST be prepared to accept one or more
         * 1xx status responses prior to a regular response.
         *
         * Since HTTP/1.0 did not define any 1xx status codes, servers MUST
         * NOT send a 1xx response to an HTTP/1.0 client except under
         * experimental conditions
         */
        if (req.http_version == HTTP_1_0) {
            return PARSE_ERROR;
        } else
        if (req.code != HTTP_CODE_CONTINUE) {
            return PARSE_OK;
        }

        if (q->expect100cont) {
            /* Temporary set the qinfo to the 100 Continue header.
             */
            q->qinfo = &req;
            (*q->on_100cont)(q);
            q->qinfo = NULL;
        }
        q->expect100cont = false;

        return PARSE_OK;
    }

    if (q->expect100cont && (req.code >= 200 && req.code < 300)) {
        return HTTPC_STATUS_EXP100CONT;
    }

    q->received_hdr_length = ps_len(&req.hdrs_ps);
    q->qinfo = httpc_qinfo_dup(&req);
    if (q->on_hdrs) {
        RETHROW((*q->on_hdrs)(q));
    }
    if (conn_close) {
        w->max_queries = 0;
        if (!w->busy) {
            obj_vcall(w, set_busy);
        }
        dlist_for_each_entry_continue(q, &w->query_list, query_link)
        {
            httpc_query_abort(q);
        }
        ob_wipe(&w->ob);
        ob_init(&w->ob);
    }

    return PARSE_OK;
}

static inline int
httpc_flush_data(httpc_t *w, httpc_query_t *q, pstream_t *ps, bool done)
{
    q->received_body_length += ps_len(ps);

    if (w->compressed && !ps_done(ps)) {
        t_scope;
        sb_t zbuf;

        t_sb_init(&zbuf, HTTP_ZLIB_BUFSIZ);
        if (http_zlib_inflate(&w->zs, &w->chunk_length, &zbuf, ps, done)) {
            return PARSE_ERROR;
        }
        RETHROW(q->on_data(q, ps_initsb(&zbuf)));
    } else {
        RETHROW(q->on_data(q, *ps));
        if (w->chunk_length >= 0) {
            w->chunk_length -= ps_len(ps);
        }
        ps->b = ps->b_end;
    }
    return PARSE_OK;
}

static int httpc_parse_body(httpc_t *w, pstream_t *ps)
{
    httpc_query_t *q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);
    ssize_t plen = ps_len(ps);

    if (plen >= w->chunk_length && w->chunk_length >= 0) {
        pstream_t tmp = __ps_get_ps(ps, w->chunk_length);

        RETHROW(httpc_flush_data(w, q, &tmp, true));
        return httpc_query_ok(q);
    }
    if (plen >= w->cfg->on_data_threshold) {
        RETHROW(httpc_flush_data(w, q, ps, false));
    }
    return PARSE_MISSING_DATA;
}

static int httpc_parse_chunk_hdr(httpc_t *w, pstream_t *ps)
{
    httpc_query_t *q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);
    const char *orig = ps->s;
    pstream_t line, hex;
    uint64_t  len = 0;
    int res;

    res = http_getline(ps, w->cfg->header_line_max, &line);
    if (res) {
        return res;
    }
    http_skipspaces(&line);
    hex = ps_get_span(&line, &ctype_ishexdigit);
    http_skipspaces(&line);
    if (unlikely(ps_len(&line)) != 0 && unlikely(line.s[0] != ';')) {
        return PARSE_ERROR;
    }
    if (unlikely(ps_len(&hex) == 0) || unlikely(ps_len(&hex) > 16)) {
        return PARSE_ERROR;
    }
    for (const char *s = hex.s; s < hex.s_end; s++)
        len = (len << 4) | __str_digit_value[*s + 128];
    w->chunk_length = len;
    w->state = len ? HTTP_PARSER_CHUNK : HTTP_PARSER_CHUNK_TRAILER;
    q->received_body_length += ps->s - orig;
    return PARSE_OK;
}

static int httpc_parse_chunk(httpc_t *w, pstream_t *ps)
{
    httpc_query_t *q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);
    ssize_t plen = ps_len(ps);

    assert (w->chunk_length >= 0);
    if (plen >= w->chunk_length + 2) {
        pstream_t tmp = __ps_get_ps(ps, w->chunk_length);

        if (ps_skipstr(ps, "\r\n")) {
            return PARSE_ERROR;
        }
        RETHROW(httpc_flush_data(w, q, &tmp, false));
        w->state = HTTP_PARSER_CHUNK_HDR;
        return PARSE_OK;
    }
    if (plen >= w->cfg->on_data_threshold) {
        RETHROW(httpc_flush_data(w, q, ps, false));
    }
    return PARSE_MISSING_DATA;
}

static int httpc_parse_chunk_trailer(httpc_t *w, pstream_t *ps)
{
    httpc_query_t *q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);
    const char *orig = ps->s;
    pstream_t line;

    do {
        int res = http_getline(ps, w->cfg->header_line_max, &line);

        if (res) {
            return res;
        }
    } while (ps_len(&line));

    q->received_body_length += ps->s - orig;
    return httpc_query_ok(q);
}

static int (*httpc_parsers[])(httpc_t *w, pstream_t *ps) = {
    [HTTP_PARSER_IDLE]          = httpc_parse_idle,
    [HTTP_PARSER_BODY]          = httpc_parse_body,
    [HTTP_PARSER_CHUNK_HDR]     = httpc_parse_chunk_hdr,
    [HTTP_PARSER_CHUNK]         = httpc_parse_chunk,
    [HTTP_PARSER_CHUNK_TRAILER] = httpc_parse_chunk_trailer,
};

/* }}} */
/* HTTPC {{{ */

int httpc_cfg_tls_init(httpc_cfg_t *cfg, sb_t *err)
{
    SSL_CTX *ctx;

    assert (cfg->ssl_ctx == NULL);

    ctx = ssl_ctx_new_tls(TLS_client_method(), LSTR_NULL_V, LSTR_NULL_V,
                          SSL_VERIFY_PEER, NULL, err);
    httpc_cfg_set_ssl_ctx(cfg, ctx);
    return cfg->ssl_ctx ? 0 : -1;
}

void httpc_cfg_tls_wipe(httpc_cfg_t *cfg)
{
    if (cfg->ssl_ctx) {
        SSL_CTX_free(cfg->ssl_ctx);
        cfg->ssl_ctx = NULL;
    }
}

int httpc_cfg_tls_add_verify_file(httpc_cfg_t *cfg, lstr_t path)
{
    return SSL_CTX_load_verify_locations(cfg->ssl_ctx, path.s, NULL) == 1
         ? 0 : -1;
}

httpc_cfg_t *httpc_cfg_init(httpc_cfg_t *cfg)
{
    core__httpc_cfg__t iop_cfg;

    p_clear(cfg, 1);

    cfg->httpc_cls = obj_class(httpc);
    iop_init(core__httpc_cfg, &iop_cfg);
    /* Default configuration cannot fail */
    IGNORE(httpc_cfg_from_iop(cfg, &iop_cfg));

    return cfg;
}

int httpc_cfg_from_iop(httpc_cfg_t *cfg, const core__httpc_cfg__t *iop_cfg)
{
    cfg->pipeline_depth    = iop_cfg->pipeline_depth;
    cfg->noact_delay       = iop_cfg->noact_delay;
    cfg->max_queries       = iop_cfg->max_queries;
    cfg->on_data_threshold = iop_cfg->on_data_threshold;
    cfg->header_line_max   = iop_cfg->header_line_max;
    cfg->header_size_max   = iop_cfg->header_size_max;

    if (iop_cfg->tls_on) {
        SB_1k(err);
        char path[PATH_MAX] = "/tmp/tls-cert-XXXXXX";
        int fd;
        int ret;

        if (!iop_cfg->tls_cert.s) {
            logger_error(&_G.logger, "tls: no certificate provided");
            return -1;
        }

        if (httpc_cfg_tls_init(cfg, &err) < 0) {
            logger_error(&_G.logger, "tls: init: %*pM", SB_FMT_ARG(&err));
            return -1;
        }

        if ((fd = mkstemp(path)) < 0) {
            logger_error(&_G.logger, "tls: failed to create a temporary path "
                         "to dump certificate: %m");
            return -1;
        }

        ret = xwrite(fd, iop_cfg->tls_cert.s, iop_cfg->tls_cert.len);
        p_close(&fd);
        if (ret < 0) {
            logger_error(&_G.logger, "tls: failed to dump certificate in "
                         "temporary file `%s`: %m", path);
            unlink(path);
            return -1;
        }

        ret = httpc_cfg_tls_add_verify_file(cfg, LSTR(path));
        unlink(path);
        if (ret < 0) {
            httpc_cfg_tls_wipe(cfg);
            logger_error(&_G.logger, "tls: failed to load certificate");
            return -1;
        }
    }

    return 0;
}

void httpc_cfg_wipe(httpc_cfg_t *cfg)
{
    httpc_close_http2_pool(cfg);
    httpc_cfg_tls_wipe(cfg);
}

void httpc_cfg_set_ssl_ctx(httpc_cfg_t *nonnull cfg, SSL_CTX *nullable ctx)
{
    httpc_cfg_tls_wipe(cfg);
    cfg->ssl_ctx = ctx;
    /* XXX: Currently, we only propose h2 protocol in HTTP/2 (TLS) mode */
    if (ctx && cfg->http_mode == HTTP_MODE_USE_HTTP2_ONLY) {
        const byte alpn[] = "\x02h2";
        unsigned int alpnlen = strlen((char *) alpn);

        if (SSL_CTX_set_alpn_protos(cfg->ssl_ctx, alpn, alpnlen) != 0) {
            logger_error(&_G.logger, "unable to set SSL ALPN protocols");
        }
    }
}

httpc_pool_t *httpc_pool_init(httpc_pool_t *pool)
{
    p_clear(pool, 1);
    dlist_init(&pool->ready_list);
    dlist_init(&pool->busy_list);
    return pool;
}

void httpc_pool_close_clients(httpc_pool_t *pool)
{
    dlist_t lst = DLIST_INIT(lst);

    dlist_splice(&lst, &pool->busy_list);
    dlist_splice(&lst, &pool->ready_list);
    dlist_for_each_entry(httpc_t, w, &lst, pool_link) {
        obj_release(&w);
    }
}

void httpc_pool_wipe(httpc_pool_t *pool, bool wipe_conns)
{
    dlist_t l = DLIST_INIT(l);

    dlist_splice(&l, &pool->busy_list);
    dlist_splice(&l, &pool->ready_list);
    dlist_for_each_entry(httpc_t, w, &l, pool_link) {
        if (wipe_conns) {
            obj_release(&w);
        } else {
            httpc_pool_detach(w);
        }
    }
    lstr_wipe(&pool->name);
    lstr_wipe(&pool->host);
    httpc_cfg_delete(&pool->cfg);
}

void httpc_pool_detach(httpc_t *w)
{
    if (w->pool) {
        w->pool->len--;
        if (w->pool->len_global) {
            (*w->pool->len_global)--;
        }
        dlist_remove(&w->pool_link);
        w->pool = NULL;
    }
}

void httpc_pool_attach(httpc_t *w, httpc_pool_t *pool)
{
    httpc_pool_detach(w);
    w->pool = pool;
    pool->len++;
    if (pool->len_global) {
        (*pool->len_global)++;
    }
    if (w->busy) {
        dlist_add(&pool->busy_list, &w->pool_link);
        if (pool->on_busy) {
            (*pool->on_busy)(pool, w);
        }
    } else {
        dlist_add(&pool->ready_list, &w->pool_link);
        if (pool->on_ready) {
            (*pool->on_ready)(pool, w);
        }
    }
}

httpc_t *httpc_pool_launch(httpc_pool_t *pool)
{
    if (pool->resolve_on_connect) {
        SB_1k(err);
        const char *what = pool->name.s ?: "httpc pool";

        assert(pool->host.s);
        if (addr_resolve_with_err(what, pool->host, &pool->su, &err) < 0) {
            logger_warning(&_G.logger, "%pL", &err);
            return NULL;
        }
    }

    return httpc_connect_as(&pool->su, pool->su_src, pool->cfg, pool);
}

static inline bool httpc_pool_reach_limit(httpc_pool_t *pool)
{
    return (pool->len >= pool->max_len ||
           (pool->len_global && *pool->len_global >= pool->max_len_global));
}

httpc_t *httpc_pool_get(httpc_pool_t *pool)
{
    httpc_t *httpc;

    if (!httpc_pool_has_ready(pool)) {
        if (httpc_pool_reach_limit(pool)) {
            return NULL;
        }
        httpc = RETHROW_P(httpc_pool_launch(pool));
        /* As we are establishing the connection, busy will be true until it
         * is connected. Thus, we will always return NULL here unless you
         * force this flag to false in the on_busy callback for some specific
         * reasons. */
        return httpc->busy ? NULL : httpc;
    }

    httpc = dlist_first_entry(&pool->ready_list, httpc_t, pool_link);
    dlist_move_tail(&pool->ready_list, &httpc->pool_link);
    return httpc;
}

bool httpc_pool_has_ready(httpc_pool_t * nonnull pool)
{
    return !dlist_is_empty(&pool->ready_list);
}

bool httpc_pool_can_query(httpc_pool_t * nonnull pool)
{
    return httpc_pool_has_ready(pool) || !httpc_pool_reach_limit(pool);
}

static httpc_t *httpc_init(httpc_t *w)
{
    dlist_init(&w->query_list);
    sb_init(&w->ibuf);
    ob_init(&w->ob);
    w->state = HTTP_PARSER_IDLE;
    return w;
}

static void httpc_disconnect_as_http2(httpc_t *w);

static void httpc_wipe(httpc_t *w)
{
    if (w->ev || w->http2_ctx) {
        obj_vcall(w, disconnect);
    }
    sb_wipe(&w->ibuf);
    http_zlib_wipe(w);
    ob_wipe(&w->ob);
    httpc_cfg_delete(&w->cfg);
    SSL_free(w->ssl);
}

static void httpc_disconnect(httpc_t *w)
{
    if (w->connected_as_http2) {
        httpc_disconnect_as_http2(w);
    }
    httpc_pool_detach(w);
    el_unregister(&w->ev);
    dlist_for_each(it, &w->query_list) {
        httpc_query_abort(dlist_entry(it, httpc_query_t, query_link));
    }
}

static void httpc_set_ready(httpc_t *w, bool first)
{
    httpc_pool_t *pool = w->pool;

    assert (w->busy);
    w->busy = false;
    if (pool) {
        dlist_move(&pool->ready_list, &w->pool_link);
        if (pool->on_ready) {
            (*pool->on_ready)(pool, w);
        }
    }
}

static void httpc_set_busy(httpc_t *w)
{
    httpc_pool_t *pool = w->pool;

    assert (!w->busy);
    w->busy = true;
    if (pool) {
        dlist_move(&pool->busy_list, &w->pool_link);
        if (pool->on_busy) {
            (*pool->on_busy)(pool, w);
        }
    }
}

OBJ_VTABLE(httpc)
    httpc.init       = httpc_init;
    httpc.disconnect = httpc_disconnect;
    httpc.wipe       = httpc_wipe;
    httpc.set_ready  = httpc_set_ready;
    httpc.set_busy   = httpc_set_busy;
OBJ_VTABLE_END()

void httpc_close_gently(httpc_t *w)
{
    w->connection_close = true;
    if (!w->busy) {
        obj_vcall(w, set_busy);
    }
    /* let the event loop maybe destroy us later, not now */
    el_fd_set_mask(w->ev, POLLOUT);
}

static void httpc_set_mask(httpc_t *w)
{
    int mask = POLLIN;

    if (w->connected_as_http2) {
        return;
    }
    if (!ob_is_empty(&w->ob)) {
        mask |= POLLOUT;
    }
    el_fd_set_mask(w->ev, mask);
}

static int httpc_on_event(el_t evh, int fd, short events, data_t priv)
{
    httpc_t *w = priv.ptr;
    httpc_query_t *q;
    pstream_t ps;
    int res, st = HTTPC_STATUS_INVALID;

    if (events == EL_EVENTS_NOACT) {
        if (!dlist_is_empty(&w->query_list)) {
            q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);
            if (q->expect100cont) {
                /* rfc 2616: §8.2.3: the client SHOULD NOT wait
                 * for an indefinite period before sending the request body
                 */
                (*q->on_100cont)(q);
                q->expect100cont = false;
                el_fd_watch_activity(evh, POLLINOUT, w->cfg->noact_delay);
                return 0;
            }
        }
        st = HTTPC_STATUS_TIMEOUT;
        goto close;
    }

    if (events & POLLIN) {
        res = w->ssl ? ssl_sb_read(&w->ibuf, w->ssl, 0)
                     : sb_read(&w->ibuf, fd, 0);
        if (res < 0) {
            if (!ERR_RW_RETRIABLE(errno)) {
                goto close;
            }
            goto write;
        }

        ps = ps_initsb(&w->ibuf);
        if (res == 0) {
            if (w->chunk_length >= 0 || w->state != HTTP_PARSER_BODY) {
                goto close;
            }
            assert (!dlist_is_empty(&w->query_list));
            /* rfc 2616: §4.4: support no Content-Length followed by close */
            w->chunk_length = ps_len(&ps);
        }

        do {
            res = (*httpc_parsers[w->state])(w, &ps);
        } while (res == PARSE_OK);
        if (res < 0) {
            st = res;
            goto close;
        }
        sb_skip_upto(&w->ibuf, ps.s);
    }

    if (unlikely(w->connection_close)) {
        if (dlist_is_empty(&w->query_list) && ob_is_empty(&w->ob)) {
            goto close;
        }
    }
  write:
    res = w->ssl ? ob_write_with(&w->ob, fd, ssl_writev, w->ssl)
                 : ob_write(&w->ob, fd);
    if (res < 0 && !ERR_RW_RETRIABLE(errno)) {
        goto close;
    }
    httpc_set_mask(w);
    return 0;

  close:
    httpc_pool_detach(w);
    if (!dlist_is_empty(&w->query_list)) {
        q = dlist_first_entry(&w->query_list, httpc_query_t, query_link);
        if (q->qinfo || st == HTTPC_STATUS_TIMEOUT) {
            httpc_query_on_done(q, st);
        }
    }
    obj_vcall(w, disconnect);
    obj_delete(&w);
    return 0;
}

static void httpc_on_connect_error(httpc_t *w, int errnum)
{
    if (w->pool && w->pool->on_connect_error) {
        (*w->pool->on_connect_error)(w, errnum);
    } else
    if (w->on_connect_error) {
        (*w->on_connect_error)(w, errnum);
    }

    obj_vcall(w, disconnect);
    obj_delete(&w);
}

static int
httpc_tls_handshake(el_t evh, int fd, short events, data_t priv)
{
    httpc_t *w = priv.ptr;
    X509    *cert;

    switch (ssl_do_handshake(w->ssl, evh, fd, NULL)) {
      case SSL_HANDSHAKE_SUCCESS:
        cert = SSL_get_peer_certificate(w->ssl);
        if (unlikely(cert == NULL)) {
            httpc_on_connect_error(w, ECONNREFUSED);
            return -1;
        }
        X509_free(cert);
        httpc_set_mask(w);
        el_fd_set_hook(evh, httpc_on_event);
        obj_vcall(w, set_ready, true);
        break;
      case SSL_HANDSHAKE_PENDING:
        break;
      case SSL_HANDSHAKE_CLOSED:
        httpc_on_connect_error(w, errno);
        break;
      case SSL_HANDSHAKE_ERROR:
        httpc_on_connect_error(w, errno);
        return -1;
    }

    return 0;
}

static int httpc_on_connect(el_t evh, int fd, short events, data_t priv)
{
    httpc_t *w   = priv.ptr;
    int      res;

    if (events == EL_EVENTS_NOACT) {
        httpc_on_connect_error(w, ETIMEDOUT);
        return -1;
    }

    res = socket_connect_status(fd);
    if (res > 0) {
        if (w->cfg->ssl_ctx) {
            w->ssl = SSL_new(w->cfg->ssl_ctx);
            assert (w->ssl);
            SSL_set_fd(w->ssl, fd);
            SSL_set_connect_state(w->ssl);
            el_fd_set_hook(evh, &httpc_tls_handshake);
        } else {
            el_fd_set_hook(evh, httpc_on_event);
            httpc_set_mask(w);
            obj_vcall(w, set_ready, true);
        }
    } else
    if (res < 0) {
        httpc_on_connect_error(w, errno);
    }
    return res;
}

httpc_t *httpc_connect(const sockunion_t *su, httpc_cfg_t *cfg,
                       httpc_pool_t *pool)
{
    return httpc_connect_as(su, NULL, cfg, pool);
}

static httpc_t *httpc_connect_as_http2(const sockunion_t *su,
                                       const sockunion_t *nullable su_src,
                                       httpc_cfg_t *cfg, httpc_pool_t *pool);

httpc_t *httpc_connect_as(const sockunion_t *su,
                          const sockunion_t * nullable su_src,
                          httpc_cfg_t *cfg, httpc_pool_t *pool)
{
    httpc_t *w;
    int fd;

    if (cfg->http_mode == HTTP_MODE_USE_HTTP2_ONLY) {
        return httpc_connect_as_http2(su, su_src, cfg, pool);
    }
    fd = RETHROW_NP(connectx_as(-1, su, 1, su_src, SOCK_STREAM, IPPROTO_TCP,
                                O_NONBLOCK, 0));
    w  = obj_new_of_class(httpc, cfg->httpc_cls);
    w->cfg         = httpc_cfg_retain(cfg);
    w->ev          = el_fd_register(fd, true, POLLOUT, &httpc_on_connect, w);
    w->max_queries = cfg->max_queries;
    el_fd_watch_activity(w->ev, POLLINOUT, w->cfg->noact_delay);
    w->busy        = true;
    if (pool) {
        httpc_pool_attach(w, pool);
    }
    return w;
}

httpc_t *httpc_spawn(int fd, httpc_cfg_t *cfg, httpc_pool_t *pool)
{
    httpc_t *w = obj_new_of_class(httpc, cfg->httpc_cls);

    w->cfg         = httpc_cfg_retain(cfg);
    w->ev          = el_fd_register(fd, true, POLLIN, &httpc_on_event, w);
    w->max_queries = cfg->max_queries;
    el_fd_watch_activity(w->ev, POLLINOUT, w->cfg->noact_delay);
    httpc_set_mask(w);
    if (pool) {
        httpc_pool_attach(w, pool);
    }
    return w;
}

/* }}} */
/* HTTPC Queries {{{ */

void httpc_query_init(httpc_query_t *q)
{
    p_clear(q, 1);
    dlist_init(&q->query_link);
    sb_init(&q->payload);
}

#define clear_fields_range_(type_t, v, f1, f2) \
    ({ type_t *__v = (v);                      \
       size_t off1 = offsetof(type_t, f1);     \
       size_t off2 = offsetof(type_t, f2);     \
       memset((uint8_t *)__v + off1, 0, off2 - off1); })
#define clear_fields_range(v, f1, f2) \
    clear_fields_range_(typeof(*(v)), v, f1, f2)

void httpc_query_reset(httpc_query_t *q)
{
    dlist_remove(&q->query_link);
    httpc_qinfo_delete(&q->qinfo);
    sb_reset(&q->payload);

    clear_fields_range(q, chunk_hdr_offs, on_hdrs);
}

void httpc_query_wipe(httpc_query_t *q)
{
    dlist_remove(&q->query_link);
    httpc_qinfo_delete(&q->qinfo);
    sb_wipe(&q->payload);
}

void httpc_query_attach(httpc_query_t *q, httpc_t *w)
{
    assert((w->ev || w->connected_as_http2) && w->max_queries > 0);
    assert (!q->hdrs_started && !q->hdrs_done);
    q->owner = w;
    dlist_add_tail(&w->query_list, &q->query_link);
    if (--w->max_queries == 0) {
        w->connection_close = true;
        if (!w->busy) {
            obj_vcall(w, set_busy);
        }
    }
    if (++w->queries >= w->cfg->pipeline_depth && !w->busy) {
        obj_vcall(w, set_busy);
    }
}

static int httpc_query_on_data_bufferize(httpc_query_t *q, pstream_t ps)
{
    size_t plen = ps_len(&ps);

    if (unlikely(plen + q->payload.len > q->payload_max_size)) {
        return HTTPC_STATUS_TOOLARGE;
    }
    sb_add(&q->payload, ps.s, plen);
    return 0;
}

void httpc_bufferize(httpc_query_t *q, unsigned maxsize)
{
    q->payload_max_size = maxsize;
    q->on_data          = &httpc_query_on_data_bufferize;
}

void httpc_query_start_flags(httpc_query_t *q, http_method_t m,
                             lstr_t host, lstr_t uri, bool httpc_encode_url)
{
    httpc_t  *w  = q->owner;
    outbuf_t *ob = &w->ob;
    int encode_at = 0;

    assert (!q->hdrs_started && !q->hdrs_done);

    ob_add(ob, http_method_str[m].s, http_method_str[m].len);
    ob_adds(ob, " ");
    if (w->cfg->use_proxy) {
        const char *s;

        if (lstr_ascii_istartswith(uri, LSTR("http://"))) {
            uri.s   += 7;
            uri.len -= 7;
            ob_add(ob, "http://", 7);
            s = memchr(uri.s, '/', uri.len);
            encode_at = (s) ? s - uri.s : uri.len;
        } else
        if (lstr_ascii_istartswith(uri, LSTR("https://"))) {
            uri.s   += 8;
            uri.len -= 8;
            ob_add(ob, "https://", 8);
            s = memchr(uri.s, '/', uri.len);
            encode_at = (s) ? s - uri.s : uri.len;
        } else {
            /* Path must be made absolute for HTTP 1.0 proxies */
            ob_addf(ob, "http://%*pM", LSTR_FMT_ARG(host));
            if (unlikely(!uri.len || uri.s[0] != '/')) {
                ob_adds(ob, "/");
            }
        }
    } else {
        assert (!lstr_startswith(uri, LSTR("http://"))
             && !lstr_startswith(uri, LSTR("https://")));
    }
    if (httpc_encode_url) {
        ob_add(ob, uri.s, encode_at);
        ob_add_urlencode(ob, uri.s + encode_at, uri.len - encode_at);
    } else {
        ob_add(ob, uri.s, uri.len);
    }
    ob_addf(ob, " HTTP/1.1\r\n" "Host: %*pM\r\n", LSTR_FMT_ARG(host));
    http_update_date_cache(&date_cache_g, lp_getsec());
    ob_add(ob, date_cache_g.buf, sizeof(date_cache_g.buf) - 1);
    ob_adds(ob, "Accept-Encoding: identity, gzip, deflate\r\n");
    if (w->connection_close) {
        ob_adds(ob, "Connection: close\r\n");
    }
    q->hdrs_started = true;
}

void httpc_query_hdrs_done(httpc_query_t *q, int clen, bool chunked)
{
    outbuf_t *ob = &q->owner->ob;

    assert (!q->hdrs_done);
    q->hdrs_done = true;

    if (q->expect100cont) {
        ob_adds(ob, "Expect: 100-continue\r\n");
    }
    if (clen >= 0) {
        ob_addf(ob, "Content-Length: %d\r\n\r\n", clen);
        return;
    }
    if (chunked) {
        q->chunked = true;
        ob_adds(ob, "Transfer-Encoding: chunked\r\n");
        /* XXX: no \r\n because http_chunk_patch adds it */
    } else {
        q->clength_hack = true;
        ob_adds(ob, "Content-Length: ");
        q->chunk_hdr_offs    = ob_reserve(ob, CLENGTH_RESERVE);
        ob_adds(ob, "\r\n");
        q->chunk_prev_length = ob->length;
    }
}

void httpc_query_chunk_done_(httpc_query_t *q, outbuf_t *ob)
{
    assert (q->chunk_started);
    q->chunk_started = false;
    http_chunk_patch(ob, ob->sb.data + q->chunk_hdr_offs,
                     ob->length - q->chunk_prev_length);
}

void httpc_query_done(httpc_query_t *q)
{
    outbuf_t *ob = &q->owner->ob;

    assert (q->hdrs_done && !q->query_done && !q->chunk_started);
    if (q->chunked) {
        ob_adds(ob, "\r\n0\r\n\r\n");
    }
    if (q->clength_hack) {
        http_clength_patch(ob, ob->sb.data + q->chunk_hdr_offs,
                           ob->length - q->chunk_prev_length);
        q->clength_hack = false;
    }
    q->query_done = true;
    httpc_set_mask(q->owner);
}

void httpc_query_hdrs_add_auth(httpc_query_t *q, lstr_t login, lstr_t passwd)
{
    outbuf_t *ob = &q->owner->ob;
    sb_t *sb;
    sb_b64_ctx_t ctx;
    int oldlen;

    assert (q->hdrs_started && !q->hdrs_done);

    sb = outbuf_sb_start(ob, &oldlen);

    sb_adds(sb, "Authorization: Basic ");
    sb_add_b64_start(sb, 0, -1, &ctx);
    sb_add_b64_update(sb, login.s, login.len, &ctx);
    sb_add_b64_update(sb, ":", 1, &ctx);
    sb_add_b64_update(sb, passwd.s, passwd.len, &ctx);
    sb_add_b64_finish(sb, &ctx);
    sb_adds(sb, "\r\n");

    outbuf_sb_end(ob, oldlen);
}

/* }}} */
/* {{{ HTTP2 Framing & Multiplexing Layer */
/* {{{ HTTP2 Constants */

#define PS_NODATA               ps_init(NULL, 0)
#define HTTP2_STREAM_ID_MASK    0x7fffffff

static const lstr_t http2_client_preface_g =
    LSTR_IMMED("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");

/* standard setting identifier values */
typedef enum setting_id_t {
    HTTP2_ID_HEADER_TABLE_SIZE      = 0x01,
    HTTP2_ID_ENABLE_PUSH            = 0x02,
    HTTP2_ID_MAX_CONCURRENT_STREAMS = 0x03,
    HTTP2_ID_INITIAL_WINDOW_SIZE    = 0x04,
    HTTP2_ID_MAX_FRAME_SIZE         = 0x05,
    HTTP2_ID_MAX_HEADER_LIST_SIZE   = 0x06,
} setting_id_t;

/* special values for stream id field */
enum {
    HTTP2_ID_NO_STREAM              = 0,
    HTTP2_ID_MAX_STREAM             = HTTP2_STREAM_ID_MASK,
};

/* length & size constants */
enum {
    HTTP2_LEN_FRAME_HDR             = 9,
    HTTP2_LEN_NO_PAYLOAD            = 0,
    HTTP2_LEN_PRIORITY_PAYLOAD      = 5,
    HTTP2_LEN_RST_STREAM_PAYLOAD    = 4,
    HTTP2_LEN_SETTINGS_ITEM         = 6,
    HTTP2_LEN_PING_PAYLOAD          = 8,
    HTTP2_LEN_GOAWAY_PAYLOAD_MIN    = 8,
    HTTP2_LEN_WINDOW_UPDATE_PAYLOAD = 4,
    HTTP2_LEN_CONN_WINDOW_SIZE_INIT = (1 << 16) - 1,
    HTTP2_LEN_WINDOW_SIZE_INIT      = (1 << 16) - 1,
    HTTP2_LEN_HDR_TABLE_SIZE_INIT   = 4096,
    HTTP2_LEN_MAX_FRAME_SIZE_INIT   = 1 << 14,
    HTTP2_LEN_MAX_FRAME_SIZE        = (1 << 24) - 1,
    HTTP2_LEN_MAX_SETTINGS_ITEMS    = HTTP2_ID_MAX_HEADER_LIST_SIZE,
    HTTP2_LEN_WINDOW_SIZE_LIMIT     = 0x7fffffff,
    HTTP2_LEN_MAX_WINDOW_UPDATE_INCR= 0x7fffffff,
};

/* standard frame type values */
typedef enum {
    HTTP2_TYPE_DATA                 = 0x00,
    HTTP2_TYPE_HEADERS              = 0x01,
    HTTP2_TYPE_PRIORITY             = 0x02,
    HTTP2_TYPE_RST_STREAM           = 0x03,
    HTTP2_TYPE_SETTINGS             = 0x04,
    HTTP2_TYPE_PUSH_PROMISE         = 0x05,
    HTTP2_TYPE_PING                 = 0x06,
    HTTP2_TYPE_GOAWAY               = 0x07,
    HTTP2_TYPE_WINDOW_UPDATE        = 0x08,
    HTTP2_TYPE_CONTINUATION         = 0x09,
} frame_type_t;

/* standard frame flag values */
enum {
    HTTP2_FLAG_NONE                 = 0x00,
    HTTP2_FLAG_ACK                  = 0x01,
    HTTP2_FLAG_END_STREAM           = 0x01,
    HTTP2_FLAG_END_HEADERS          = 0x04,
    HTTP2_FLAG_PADDED               = 0x08,
    HTTP2_FLAG_PRIORITY             = 0x20,
};

/* standard error codes */
typedef enum {
    HTTP2_CODE_NO_ERROR             = 0x0,
    HTTP2_CODE_PROTOCOL_ERROR       = 0x1,
    HTTP2_CODE_INTERNAL_ERROR       = 0x2,
    HTTP2_CODE_FLOW_CONTROL_ERROR   = 0x3,
    HTTP2_CODE_SETTINGS_TIMEOUT     = 0x4,
    HTTP2_CODE_STREAM_CLOSED        = 0x5,
    HTTP2_CODE_FRAME_SIZE_ERROR     = 0x6,
    HTTP2_CODE_REFUSED_STREAM       = 0x7,
    HTTP2_CODE_CANCEL               = 0x8,
    HTTP2_CODE_COMPRESSION_ERROR    = 0x9,
    HTTP2_CODE_CONNECT_ERROR        = 0xa,
    HTTP2_CODE_ENHANCE_YOUR_CALM    = 0xb,
    HTTP2_CODE_INADEQUATE_SECURITY  = 0xc,
    HTTP2_CODE_HTTP_1_1_REQUIRED    = 0xd,
} err_code_t;

/* }}} */
/* {{{ Primary Types */

/** Settings of HTTP2 framing layer as per RFC7540/RFC9113 */
typedef struct http2_settings_t {
    uint32_t                    header_table_size;
    uint32_t                    enable_push;
    opt_u32_t                   max_concurrent_streams;
    uint32_t                    initial_window_size;
    uint32_t                    max_frame_size;
    opt_u32_t                   max_header_list_size;
} http2_settings_t;

/* default setting values acc. to RFC7540/RFC9113 */
static http2_settings_t http2_default_settings_g = {
    .header_table_size = HTTP2_LEN_HDR_TABLE_SIZE_INIT,
    .enable_push = 1,
    .max_concurrent_streams = OPT_NONE,
    .initial_window_size = HTTP2_LEN_WINDOW_SIZE_INIT,
    .max_frame_size = HTTP2_LEN_MAX_FRAME_SIZE_INIT,
    .max_header_list_size = OPT_NONE,
};

/* stream state/info flags */
enum {
    STREAM_FLAG_INIT_HDRS = 1 << 0,
    STREAM_FLAG_EOS_RECV  = 1 << 1,
    STREAM_FLAG_EOS_SENT  = 1 << 2,
    STREAM_FLAG_RST_RECV  = 1 << 3,
    STREAM_FLAG_RST_SENT  = 1 << 4,
    STREAM_FLAG_PSH_RECV  = 1 << 5,
    STREAM_FLAG_CLOSED    = 1 << 6,
};

/** XXX: \p http2_stream_t is meant to be passed around by-value. For streams
 * in tracked state, the corresponding values are constructed from \p
 * qm_t(qstream_info) . */

typedef union http2_stream_ctx_t {
    httpc_http2_ctx_t *httpc_ctx;
    httpd_t *httpd;
} http2_stream_ctx_t;

typedef struct http2_stream_info_t {
    http2_stream_ctx_t  ctx;
    int32_t             recv_window;
    int32_t             send_window;
    uint8_t             flags;
} http2_stream_info_t;

qm_k32_t(qstream_info, http2_stream_info_t);

typedef struct http2_stream_t {
    uint32_t remove: 1;
    uint32_t id : 31;
    http2_stream_info_t info;
} http2_stream_t;

typedef struct http2_closed_stream_info_t {
    uint32_t    stream_id : 31;
    dlist_t     list_link;
} http2_closed_stream_info_t;

/** info parsed from the frame hdr */
typedef struct http2_frame_info_t {
    uint32_t    len;
    uint32_t    stream_id;
    uint8_t     type;
    uint8_t     flags;
} http2_frame_info_t;

typedef struct http2_client_t http2_client_t;
typedef struct http2_server_t http2_server_t;

/** HTTP2 connection object that can be configure as server or client. */
typedef struct http2_conn_t {
    el_t                nonnull ev;
    http2_settings_t    settings;
    http2_settings_t    peer_settings;
    unsigned            refcnt;
    unsigned            id;
    outbuf_t            ob;
    sb_t                ibuf;
    SSL                 * nullable ssl;
    /* hpack compression contexts */
    hpack_enc_dtbl_t    enc;
    hpack_dec_dtbl_t    dec;
    /* tracked streams */
    qm_t(qstream_info)  stream_info;
    dlist_t             closed_stream_info;
    uint32_t            client_streams;
    uint32_t            server_streams;
    uint32_t            closed_streams_info_cnt;
    /* backstream contexts */
    union {
        http2_client_t *nullable client_ctx;
        http2_server_t *nullable server_ctx;
    };
    /* flow control */
    int32_t             recv_window;
    int32_t             send_window;
    /* frame parser */
    http2_frame_info_t  frame;
    unsigned            cont_chunk;
    unsigned            promised_id;
    uint8_t             state;
    /* connection flags */
    bool                is_client : 1;
    bool                is_settings_acked : 1;
    bool                is_conn_err_recv: 1;
    bool                is_conn_err_sent: 1;
    bool                is_shutdown_recv: 1;
    bool                is_shutdown_sent: 1;
    bool                is_shutdown_soon_recv: 1;
    bool                is_shutdown_soon_sent: 1;
    bool                is_shutdown_commanded : 1;
} http2_conn_t;

/** Get effective HTTP2 settings */
static http2_settings_t http2_get_settings(http2_conn_t *w)
{
    return likely(w->is_settings_acked) ? w->settings
                                        : http2_default_settings_g;
}

typedef enum http2_header_info_flags_t {
    HTTP2_HDR_FLAG_HAS_SCHEME               =  1 << 0,
    HTTP2_HDR_FLAG_HAS_METHOD               =  1 << 1,
    HTTP2_HDR_FLAG_HAS_PATH                 =  1 << 2,
    HTTP2_HDR_FLAG_HAS_AUTHORITY            =  1 << 3,
    HTTP2_HDR_FLAG_HAS_STATUS               =  1 << 4,
    /* EXTRA: either unknown or duplicated or after a regular hdr */
    HTTP2_HDR_FLAG_HAS_EXTRA_PSEUDO_HDR     =  1 << 5,
    HTTP2_HDR_FLAG_HAS_REGULAR_HEADERS      =  1 << 6,
    HTTP2_HDR_FLAG_HAS_CONTENT_LENGTH       =  1 << 7,
} http2_header_info_flags_t;

typedef struct http2_header_info_t {
    http2_header_info_flags_t flags;
    lstr_t scheme;
    lstr_t method;
    lstr_t path;
    lstr_t authority;
    lstr_t status;
    lstr_t content_length;
} http2_header_info_t;

/* }}}*/
/* {{{ Logging */

/* TODO: add additional conn-related info to the log message */
#define http2_conn_log(/* const http_conn_t* */ w, /* int */ level,          \
                       /* const char* */ fmt, ...)                           \
    logger_log(&_G.logger, level, fmt, ##__VA_ARGS__)

#define http2_conn_trace(w, level, fmt, ...)                                 \
    http2_conn_log(w, LOG_TRACE + (level), "[h2c %u] " fmt, (w)->id,         \
                   ##__VA_ARGS__)

/* TODO: add additional stream-related info to the log message */
#define http2_stream_log(/* const http_conn_t* */ w,                         \
                         /* const stream_t* */ stream, /* int */ level,      \
                         /* const char* */ fmt, ...)                         \
    logger_log(&_G.logger, level, "[h2c %u, sid %d] " fmt, (w)->id,          \
               (stream)->id, ##__VA_ARGS__)

#define http2_stream_trace(w, stream, level, fmt, ...)                       \
    http2_stream_log(w, stream, LOG_TRACE + (level), fmt, ##__VA_ARGS__)

/* }}} */
/* {{{ Connection Management */

static http2_conn_t *http2_conn_init(http2_conn_t *w)
{
    p_clear(w, 1);
    w->id = ++_G.http2_conn_count;
    sb_init(&w->ibuf);
    ob_init(&w->ob);
    dlist_init(&w->closed_stream_info);
    qm_init(qstream_info, &w->stream_info);
    w->peer_settings = http2_default_settings_g;
    w->recv_window = HTTP2_LEN_CONN_WINDOW_SIZE_INIT;
    w->send_window = HTTP2_LEN_CONN_WINDOW_SIZE_INIT;
    hpack_enc_dtbl_init(&w->enc);
    hpack_dec_dtbl_init(&w->dec);
    hpack_enc_dtbl_init_settings(&w->enc, w->peer_settings.header_table_size);
    hpack_dec_dtbl_init_settings(&w->dec,
                                 http2_get_settings(w).header_table_size);
    return w;
}

static void http2_conn_wipe(http2_conn_t *w)
{
    hpack_dec_dtbl_wipe(&w->dec);
    hpack_enc_dtbl_wipe(&w->enc);
    ob_wipe(&w->ob);
    sb_wipe(&w->ibuf);
    qm_wipe(qstream_info, &w->stream_info);
    assert(dlist_is_empty(&w->closed_stream_info));
    SSL_free(w->ssl);
    w->ssl = NULL;
    el_unregister(&w->ev);
}

DO_REFCNT(http2_conn_t, http2_conn)

/** Return the maximum id of the (non-idle) server stream or 0 if none. */
static uint32_t http2_conn_max_server_stream_id(const http2_conn_t *w)
{
    /* Server streams have even ids: 2, 4, 6, and so on. Server streams with
     * ids superior than this http2_conn_max_server_stream_id are idle,
     * otherwise, they are non idle (either active or closed). So, the next
     * available idle server stream is (http2_conn_max_server_stream_id + 2).
     * Note, that initiating a stream above this value, i.e., skipping some
     * ids is possible and implies closing the streams with the skipped ids.
     * So, this threshold is tracked using the number of streams (non-idle)
     * sor far. So, 0 server stream => 0 max server stream id (the next idle
     * stream is 2), 1 server stream => 2 max server stream id (the next idle
     * stream is 4), and so on.
     */
    return 2 * w->server_streams;
}

/** Return the maximum id of the (non-idle) client stream or 0 if none. */
static uint32_t http2_conn_max_client_stream_id(const http2_conn_t *w)
{
    /* Client streams have odd ids: 1, 3, 5, and so on. Client streams with
     * ids superior than this http2_conn_max_client_stream_id are idle,
     * otherwise, they are non idle (either active or closed). So, the next
     * available idle client stream is (http2_conn_max_client_stream_id + 2)
     * except for client_streams = 0 where the next idle stream is 1. Note,
     * that initiating a stream above this value, i.e., skipping some ids is
     * possible and implies closing the streams with the skipped ids. So, this
     * threshold is tracked using the number of streams (non-idle) sor far.
     * So, 0 client stream => max client stream id = 0 (the next idle stream
     * is 1), 1 client stream => max client stream id = 1 (the next idle
     * stream is 3), 2 client streams => max client stream id = 3 (the next
     * idle stream is 5) and so on.
     */
    return 2 * w->client_streams - !!w->client_streams;
}

/** Return the maximum id of the (non-idle) peer stream or 0 if none. */
static uint32_t http2_conn_max_peer_stream_id(const http2_conn_t *w)
{
    return w->is_client ? http2_conn_max_server_stream_id(w)
                        : http2_conn_max_client_stream_id(w);
}

/* }}}*/
/* {{{ Send Buffer Framing */

typedef struct http2_frame_hdr_t {
    be32_t len : 24;
    uint8_t type;
    uint8_t flags;
    be32_t stream_id;
} __attribute__((packed)) http2_frame_hdr_t;

static void
http2_conn_send_common_hdr(http2_conn_t *w, unsigned len, uint8_t type,
                           uint8_t flags, uint32_t stream_id)
{
    http2_frame_hdr_t hdr;

    STATIC_ASSERT(sizeof(hdr) == HTTP2_LEN_FRAME_HDR);
    STATIC_ASSERT(HTTP2_LEN_MAX_FRAME_SIZE < (1 << 24));
    assert(len <= HTTP2_LEN_MAX_FRAME_SIZE);
    put_unaligned_be24(&hdr, len); /* XXX: hdr.len is a bit field */
    hdr.type = type;
    hdr.flags = flags;
    hdr.stream_id = cpu_to_be32(stream_id);
    ob_add(&w->ob, &hdr, sizeof(hdr));
}

static void http2_conn_send_preface(http2_conn_t *w)
{
    if (w->is_client) {
        OB_WRAP(sb_add_lstr, &w->ob, http2_client_preface_g);
    }
}

typedef struct {
    uint16_t id;
    uint32_t val;
} setting_item_t;

qvector_t(qsettings, setting_item_t);

static void http2_conn_send_init_settings(http2_conn_t *w)
{
    t_scope;
    http2_settings_t defaults;
    http2_settings_t init_settings;
    qv_t(qsettings) items;

#define STNG_ITEM(id_, val_)                                                 \
    (setting_item_t)                                                         \
    {                                                                        \
        .id = HTTP2_ID_##id_, .val = init_settings.val_                      \
    }

#define STNG_ITEM_OPT(id_, val_)                                             \
    (setting_item_t){.id = HTTP2_ID_##id_, .val = OPT_VAL(init_settings.val_)}

    t_qv_init(&items, HTTP2_LEN_MAX_SETTINGS_ITEMS);
    defaults = http2_default_settings_g;
    init_settings = w->settings;
    if (init_settings.header_table_size != defaults.header_table_size) {
        qv_append(&items, STNG_ITEM(HEADER_TABLE_SIZE, header_table_size));
    }
    if (w->is_client && init_settings.enable_push != defaults.enable_push) {
        qv_append(&items, STNG_ITEM(ENABLE_PUSH, enable_push));
    }
    if (OPT_ISSET(init_settings.max_concurrent_streams) &&
        !OPT_EQUAL(init_settings.max_concurrent_streams,
                   defaults.max_concurrent_streams))
    {
        qv_append(&items, STNG_ITEM_OPT(MAX_CONCURRENT_STREAMS,
                                        max_concurrent_streams));
    }
    if (init_settings.initial_window_size != defaults.initial_window_size) {
        qv_append(&items,
                  STNG_ITEM(INITIAL_WINDOW_SIZE, initial_window_size));
    }
    if (init_settings.max_frame_size != defaults.max_frame_size) {
        qv_append(&items, STNG_ITEM(MAX_FRAME_SIZE, max_frame_size));
    }
    if (OPT_ISSET(init_settings.max_header_list_size) &&
        !OPT_EQUAL(init_settings.max_header_list_size,
                   defaults.max_header_list_size))
    {
        qv_append(&items, STNG_ITEM_OPT(MAX_HEADER_LIST_SIZE,
                                        max_header_list_size));
    }
    assert(items.len <= HTTP2_LEN_MAX_SETTINGS_ITEMS);
    http2_conn_send_common_hdr(w, HTTP2_LEN_SETTINGS_ITEM * items.len,
                               HTTP2_TYPE_SETTINGS, HTTP2_FLAG_NONE,
                               HTTP2_ID_NO_STREAM);
    tab_for_each_ptr(item, &items) {
        OB_WRAP(sb_add_be16, &w->ob, item->id);
        OB_WRAP(sb_add_be32, &w->ob, item->val);
    }

#undef STNG_ITEM_OPT
#undef STNG_ITEM
}

typedef struct http2_min_goaway_payload_t {
    be32_t last_stream_id;
    be32_t error_code;
} http2_min_goaway_payload_t;

static void http2_conn_send_goaway(http2_conn_t *w, uint32_t last_stream_id,
                                   uint32_t error_code, lstr_t debug)
{
    http2_min_goaway_payload_t payload;
    int len = sizeof(payload) + debug.len;

    STATIC_ASSERT(sizeof(payload) == HTTP2_LEN_GOAWAY_PAYLOAD_MIN);
    assert(last_stream_id <= HTTP2_ID_MAX_STREAM);
    payload.last_stream_id = cpu_to_be32(last_stream_id);
    payload.error_code = cpu_to_be32(error_code);
    http2_conn_send_common_hdr(w, len, HTTP2_TYPE_GOAWAY, HTTP2_FLAG_NONE,
                               HTTP2_ID_NO_STREAM);
    ob_add(&w->ob, &payload, sizeof(payload));
    ob_add(&w->ob, debug.data, debug.len);
}

/** Send data block as 0 or more data frames. */
static void http2_conn_send_data_block(http2_conn_t *w, uint32_t stream_id,
                                       pstream_t blk, bool end_stream)
{
    pstream_t chunk;
    uint8_t flags;
    unsigned len;

    if (ps_done(&blk) && !end_stream) {
        /* Empty DATA frames have no effect except those which end streams */
        return;
    }
    /* HTTP2_LEN_MAX_FRAME_SIZE_INIT is also the minimum possible value so
     * peer must always accept frames of this size. */
    assert(w->send_window >= (int) ps_len(&blk));
    w->send_window -= ps_len(&blk);
    do {
        len = MIN(ps_len(&blk), HTTP2_LEN_MAX_FRAME_SIZE_INIT);
        chunk = __ps_get_ps(&blk, len);
        flags = ps_done(&blk) && end_stream ? HTTP2_FLAG_END_STREAM : 0;
        http2_conn_send_common_hdr(w, len, HTTP2_TYPE_DATA, flags, stream_id);
        OB_WRAP(sb_add_ps, &w->ob, chunk);
    } while (!ps_done(&blk));
}

/** Send header block as 1 header frame plus 0 or more continuation frames. */
static void http2_conn_send_headers_block(http2_conn_t *w, uint32_t stream_id,
                                          pstream_t blk, bool end_stream)
{
    pstream_t chunk;
    uint8_t type;
    uint8_t flags;
    unsigned len;

    assert(!ps_done(&blk));
    /* HTTP2_LEN_MAX_FRAME_SIZE_INIT is also the minimum possible value so
     * peer must always accept frames of this size. */
    type = HTTP2_TYPE_HEADERS;
    flags = end_stream ? HTTP2_FLAG_END_STREAM : HTTP2_FLAG_NONE;
    do {
        len = MIN(ps_len(&blk), HTTP2_LEN_MAX_FRAME_SIZE_INIT);
        chunk = __ps_get_ps(&blk, len);
        flags |= ps_done(&blk) ? HTTP2_FLAG_END_HEADERS : 0;
        http2_conn_send_common_hdr(w, len, type, flags, stream_id);
        OB_WRAP(sb_add_ps, &w->ob, chunk);
        type = HTTP2_TYPE_CONTINUATION;
        flags = HTTP2_FLAG_NONE;
    } while (!ps_done(&blk));
}

static void http2_conn_send_rst_stream(http2_conn_t *w, uint32_t stream_id,
                                       uint32_t error_code)
{
    assert(stream_id);
    http2_conn_send_common_hdr(w, HTTP2_LEN_RST_STREAM_PAYLOAD,
                               HTTP2_TYPE_RST_STREAM, HTTP2_FLAG_NONE,
                               stream_id);
    OB_WRAP(sb_add_be32, &w->ob, error_code);
}

static void http2_conn_send_window_update(http2_conn_t *w, uint32_t stream_id,
                                          uint32_t incr)
{
    assert(incr > 0 && incr <= 0x7fffffff);
    http2_conn_send_common_hdr(w, HTTP2_LEN_WINDOW_UPDATE_PAYLOAD,
                               HTTP2_TYPE_WINDOW_UPDATE, HTTP2_FLAG_NONE,
                               stream_id);
    OB_WRAP(sb_add_be32, &w->ob, incr);
}

static void
http2_conn_send_shutdown(http2_conn_t *w, lstr_t debug)
{
    uint32_t stream_id = http2_conn_max_peer_stream_id(w);

    assert(!w->is_shutdown_sent);
    w->is_shutdown_sent = true;
    http2_conn_send_goaway(w, stream_id, HTTP2_CODE_NO_ERROR, debug);
}

static int
http2_conn_send_error(http2_conn_t *w, uint32_t error_code, lstr_t debug)
{
    uint32_t stream_id = http2_conn_max_peer_stream_id(w);

    assert(error_code != HTTP2_CODE_NO_ERROR);
    assert(!w->is_conn_err_sent);
    w->is_conn_err_sent = true;
    http2_conn_send_goaway(w, stream_id, error_code, debug);

    return -1;
}

/* }}} */
/* {{{ Stream Management */

static bool http2_stream_id_is_server(uint32_t stream_id)
{
    assert(stream_id);
    assert(stream_id <= (uint32_t)HTTP2_ID_MAX_STREAM);
    return stream_id % 2 == 0;
}

static bool http2_stream_id_is_client(uint32_t stream_id)
{
    return !http2_stream_id_is_server(stream_id);
}

static int
http2_conn_is_peer_stream_id(const http2_conn_t *w, uint32_t stream_id)
{
    if (w->is_client) {
        return http2_stream_id_is_server(stream_id);
    } else {
        return http2_stream_id_is_client(stream_id);
    }
    return 0;
}


/** Check if \p stream_id is a stream that can be initiated by the peer. */
__unused__
static int
http2_conn_check_peer_stream_id(const http2_conn_t *w, uint32_t stream_id)
{
    THROW_ERR_UNLESS(http2_conn_is_peer_stream_id(w, stream_id));
    return 0;
}

/** Return true if the \p stream_id is a peer stream that is still in its idle
 * state. */
__unused__
static bool
http2_conn_peer_stream_id_is_idle(const http2_conn_t *w, uint32_t stream_id)
{
    return stream_id > http2_conn_max_peer_stream_id(w);
}

/** Return the number of streams (of the same class) upto to \p stream_id. */
static uint32_t http2_get_nb_streams_upto(uint32_t stream_id)
{
    assert(stream_id);
    assert(stream_id <= (uint32_t)HTTP2_ID_MAX_STREAM);

    return DIV_ROUND_UP(stream_id, 2);
}

/** Return the stream (info) with id = \p stream_id */
static http2_stream_t http2_stream_get(http2_conn_t *w, uint32_t stream_id)
{
    http2_stream_t stream = {.id = stream_id};
    http2_stream_info_t *info;
    uint32_t nb_streams;

    nb_streams = http2_stream_id_is_client(stream_id) ? w->client_streams
                                                      : w->server_streams;
    if (http2_get_nb_streams_upto(stream_id) > nb_streams) {
        /* stream is idle. */
        return stream;
    }
    info = qm_get_def_p(qstream_info, &w->stream_info, stream_id, NULL);
    if (info) {
        /* stream is non_idle. */
        stream.info = *info;
    } else {
        /* stream is closed<untracked state>. */
        stream.info.flags = STREAM_FLAG_CLOSED;
    }
    return stream;
}

/** Get the next idle (available) stream id. */
static uint32_t http2_stream_get_idle(http2_conn_t *w)
{
    uint32_t stream_id;

    /* XXX: only relevant on the client side since we don't support creating
     * server (pushed) streams (yet!). */
    assert(w->is_client);
    stream_id = 2 * (uint32_t)w->client_streams + 1;
    assert(stream_id <= (uint32_t)HTTP2_ID_MAX_STREAM);
    return stream_id;
}

static void
http2_closed_stream_info_create(http2_conn_t *w, const http2_stream_t *stream)
{
    http2_closed_stream_info_t *info = p_new(http2_closed_stream_info_t, 1);

    info->stream_id = stream->id;
    dlist_add_tail(&w->closed_stream_info, &info->list_link);
    w->closed_streams_info_cnt++;
}

static void
http2_stream_do_update_info(http2_conn_t *w, http2_stream_t *stream)
{
    unsigned flags = stream->info.flags;

    if (stream->remove) {
        qm_del_key(qstream_info, &w->stream_info, stream->id);
    } else {
        assert(flags && !(flags & STREAM_FLAG_CLOSED));
        qm_replace(qstream_info, &w->stream_info, stream->id, stream->info);
    }
}

static void http2_stream_do_on_events(http2_conn_t *w, http2_stream_t *stream,
                                      unsigned events)
{
    unsigned flags = stream->info.flags;

    assert(events);
    assert(!(flags & STREAM_FLAG_CLOSED));
    assert(!(flags & events));
    if (!flags) {
        /* Idle stream */
        uint32_t nb_streams;
        uint32_t new_nb_streams;

        nb_streams = http2_stream_id_is_client(stream->id)
                         ? w->client_streams
                         : w->server_streams;
        new_nb_streams = http2_get_nb_streams_upto(stream->id);
        assert(new_nb_streams > nb_streams);
        if (events == STREAM_FLAG_INIT_HDRS) {
            http2_stream_trace(w, stream, 2, "opened");
        } else if (events == (STREAM_FLAG_INIT_HDRS | STREAM_FLAG_EOS_RECV)) {
            http2_stream_trace(w, stream, 2, "half closed (remote)");
        } else if (events == (STREAM_FLAG_INIT_HDRS | STREAM_FLAG_EOS_SENT)) {
            http2_stream_trace(w, stream, 2, "half closed (local)");
        } else if (events == (STREAM_FLAG_PSH_RECV | STREAM_FLAG_RST_SENT)) {
            assert(w->is_client && !stream->id);
            http2_stream_trace(w, stream, 2, "closed [pushed, reset sent]");
        } else {
            assert(0 && "invalid events on idle stream");
        }
        /* RFC7541(RFC9113) § 5.1.1. Stream Identifiers */
        if (http2_stream_id_is_client(stream->id)) {
            w->client_streams = new_nb_streams;
        } else {
            w->server_streams = new_nb_streams;
        }
        stream->info.flags = events;
        stream->info.recv_window = http2_get_settings(w).initial_window_size;
        stream->info.send_window = w->peer_settings.initial_window_size;
        return;
    }
    if (events == STREAM_FLAG_EOS_RECV) {
        if (flags & STREAM_FLAG_EOS_SENT) {
            http2_stream_trace(w, stream, 2, "stream closed [eos recv]");
            stream->remove = true;
            p_clear(&stream->info.ctx, 1);
        } else {
            http2_stream_trace(w, stream, 2, "stream half closed (remote)");
        }
    } else if (events == STREAM_FLAG_EOS_SENT) {
        if (flags & STREAM_FLAG_EOS_RECV) {
            http2_stream_trace(w, stream, 2, "stream closed [eos sent]");
            http2_closed_stream_info_create(w, stream);
            p_clear(&stream->info.ctx, 1);
        } else {
            http2_stream_trace(w, stream, 2, "stream half closed (local)");
        }
    } else if (events == STREAM_FLAG_RST_RECV) {
        http2_stream_trace(w, stream, 2, "stream closed [reset recv]");
        stream->remove = true;
        p_clear(&stream->info.ctx, 1);
    } else if (events == STREAM_FLAG_RST_SENT) {
        http2_stream_trace(w, stream, 2, "stream closed [reset sent]");
        http2_closed_stream_info_create(w, stream);
        p_clear(&stream->info.ctx, 1);
    } else {
        assert(0 && "unexpected stream state transition");
    }
    stream->info.flags = flags | events;
}

/* }}}*/
/* {{{ Headers Packing/Unpacking (HPACK) */

static struct {
    lstr_t key;
    unsigned flag_seen;
    int offset;
} http2_pseudo_hdr_descs_g[] = {

#define PSEUDO_HDR(key_tok, key_)                                            \
    {                                                                        \
        .key = LSTR_IMMED(":" #key_),                                        \
        .flag_seen = HTTP2_HDR_FLAG_HAS_##key_tok,                           \
        .offset = offsetof(http2_header_info_t, key_),                       \
    }

    PSEUDO_HDR(METHOD, method),
    PSEUDO_HDR(SCHEME, scheme),
    PSEUDO_HDR(PATH, path),
    PSEUDO_HDR(AUTHORITY, authority),
    PSEUDO_HDR(STATUS, status),

#undef PSEUDO_HDR
};

/** Decode a header block.
 *
 * \param res: decoded headers info.
 * \return 0 if decoding succeed, -1 otherwise.
 *
 */
static int
t_http2_conn_decode_header_block(http2_conn_t *w, pstream_t in,
                                 http2_header_info_t *res, sb_t *buf)
{
    hpack_dec_dtbl_t *dec = &w->dec;
    http2_header_info_t info = {0};

    while (RETHROW(hpack_decoder_read_dts_update(dec, &in))) {
        /* read dynamic table size updates. */
    }
    while (!ps_done(&in)) {
        hpack_xhdr_t xhdr;
        int len;
        int keylen;
        byte *out;
        lstr_t key;
        lstr_t val;

        len = RETHROW(hpack_decoder_extract_hdr(dec, &in, &xhdr));
        out = (byte *)sb_grow(buf, len);
        /* XXX: Decoded header is unpacked into the following format:
         * <DECODED_KEY> + ": " + <DECODED_VALUE> + "\r\n".
         */
        len = RETHROW(hpack_decoder_write_hdr(dec, &xhdr, out, &keylen));
        key = LSTR_DATA_V(out, keylen);
        val = LSTR_DATA_V(out + keylen + 2, len - keylen - 4);
        http2_conn_trace(w, 2, "%*pM: %*pM", LSTR_FMT_ARG(key),
                         LSTR_FMT_ARG(val));
        THROW_ERR_IF(keylen < 1);
        if (unlikely(key.s[0] == ':')) {
            lstr_t *matched_phdr = NULL;

            if (info.flags & HTTP2_HDR_FLAG_HAS_REGULAR_HEADERS) {
                info.flags |= HTTP2_HDR_FLAG_HAS_EXTRA_PSEUDO_HDR;
            }
            carray_for_each_entry(phdr, http2_pseudo_hdr_descs_g) {
                if (lstr_equal(key, phdr.key)) {
                    if (!(phdr.flag_seen & info.flags)) {
                        matched_phdr = (lstr_t *)((byte *)(&info) + phdr.offset);

                        info.flags |= phdr.flag_seen;
                        *matched_phdr = t_lstr_dup(val);
                        break;
                    } else {
                        info.flags |= HTTP2_HDR_FLAG_HAS_EXTRA_PSEUDO_HDR;
                    }
                }
            }
            if (!matched_phdr) {
                /* unknown pseudo-hdr */
                info.flags |= HTTP2_HDR_FLAG_HAS_EXTRA_PSEUDO_HDR;
            }
        } else {
            info.flags |= HTTP2_HDR_FLAG_HAS_REGULAR_HEADERS;
            if (lstr_ascii_iequal(key, LSTR_IMMED_V("content-length"))) {
                info.flags |= HTTP2_HDR_FLAG_HAS_CONTENT_LENGTH;
                info.content_length = val;
            }
            buf->len += len;
        }
    }
    sb_set_trailing0(buf);
    /* Basic validation according to RFC9113 §8.3. */
   *res = info;
    return 0;
}

static void http2_headerlines_get_next_hdr(pstream_t *headerlines,
                                           lstr_t *key, lstr_t *val)
{
    pstream_t line = PS_NODATA;
    pstream_t ps = PS_NODATA;
    int rc;

    assert(!ps_done(headerlines));
    rc = ps_get_ps_upto_str_and_skip(headerlines, "\r\n", &line);
    assert(rc >= 0 && !ps_done(&line));
    rc = ps_get_ps_chr_and_skip(&line, ':', &ps);
    assert(rc >= 0);
    ps_trim(&ps);
    assert(!ps_done(&ps));
    *key = LSTR_PS_V(&ps);
    ps_trim(&line);
    assert(!ps_done(&line));
    *val = LSTR_PS_V(&line);
}

static void http2_conn_pack_single_hdr(http2_conn_t *w, lstr_t key,
                                       lstr_t val, sb_t *out_)
{
    hpack_enc_dtbl_t *enc = &w->enc;
    int len;
    int buflen;
    byte *out;

    buflen = hpack_buflen_to_write_hdr(key, val, 0);
    out = (byte *)sb_grow(out_, buflen);
    len = hpack_encoder_write_hdr(enc, key, val, 0, 0, 0, out);
    assert(len > 0);
    assert(len <= buflen);
    __sb_fixlen(out_, out_->len + len);
}

/* }}} */
/* {{{ Streaming API */

static void
http2_stream_on_headers_client(http2_conn_t *w, http2_stream_t stream,
                               httpc_http2_ctx_t *httpc_ctx,
                               http2_header_info_t *info,
                               pstream_t headerlines, bool eos);
static void
http2_stream_on_headers_server(http2_conn_t *w, http2_stream_t stream,
                               httpd_t *httpd, http2_header_info_t *info,
                               pstream_t headerlines, bool eos);

static void
http2_stream_on_headers(http2_conn_t *w, http2_stream_t stream,
                        http2_stream_ctx_t ctx, http2_header_info_t *info,
                        pstream_t headerlines, bool eos)
{
    if (w->is_client) {
        http2_stream_on_headers_client(w, stream, ctx.httpc_ctx, info,
                                       headerlines, eos);
    } else {
        http2_stream_on_headers_server(w, stream, ctx.httpd, info,
                                       headerlines, eos);
    }
}

static void
http2_stream_on_data_client(http2_conn_t *w, http2_stream_t stream,
                            httpc_http2_ctx_t *httpc_ctx, pstream_t data,
                            bool eos);
static void
http2_stream_on_data_server(http2_conn_t *w, http2_stream_t stream,
                            httpd_t *httpd, pstream_t data, bool eos);

static void
http2_stream_on_data(http2_conn_t *w, http2_stream_t stream,
                     http2_stream_ctx_t ctx, pstream_t data, bool eos)
{
    if (w->is_client) {
        http2_stream_on_data_client(w, stream, ctx.httpc_ctx,
                                    data, eos);
    } else {
        http2_stream_on_data_server(w, stream, ctx.httpd, data, eos);
    }
}

static void
http2_stream_on_reset_client(http2_conn_t *w, http2_stream_t stream,
                             httpc_http2_ctx_t *httpc_ctx, bool remote);
static void
http2_stream_on_reset_server(http2_conn_t *w, http2_stream_t stream,
                             httpd_t *httpd, bool remote);

static void http2_stream_on_reset(http2_conn_t *w, http2_stream_t stream,
                                  http2_stream_ctx_t ctx, bool remote)
{
    if (w->is_client) {
        http2_stream_on_reset_client(w, stream, ctx.httpc_ctx, remote);
    } else {
        if (ctx.httpd) {
            http2_stream_on_reset_server(w, stream, ctx.httpd, remote);
        }
    }
}

static void http2_conn_on_streams_can_write_client(http2_conn_t *w);
static void http2_conn_on_streams_can_write_server(http2_conn_t *w);

static void http2_conn_on_streams_can_write(http2_conn_t *w)
{
    if (w->is_client) {
            http2_conn_on_streams_can_write_client(w);
    } else {
        http2_conn_on_streams_can_write_server(w);
    }
}

static void http2_conn_on_close_client(http2_conn_t *w);
static void http2_conn_on_close_server(http2_conn_t *w);

static void http2_conn_on_close(http2_conn_t *w)
{
    if (w->is_client) {
        http2_conn_on_close_client(w);
    } else {
        http2_conn_on_close_server(w);
    }
}

static bool
http2_is_valid_response_hdr_to_send(lstr_t key_, lstr_t val, int *clen)
{
    pstream_t key = ps_initlstr(&key_);
    int rc;

    switch (http_wkhdr_from_ps(key)) {
    case HTTP_WKHDR_PRAGMA:
    case HTTP_WKHDR_CONNECTION:

        return false;
    case HTTP_WKHDR_CONTENT_LENGTH:
        rc = lstr_to_int(val, clen);
        assert(!rc);
        break;
    default:
        break;
    }
    return true;
}

static void
http2_stream_send_response_headers(http2_conn_t *w, http2_stream_t *stream,
                                   lstr_t status, pstream_t headerlines,
                                   httpd_http2_ctx_t *httpd_ctx, int *clen)
{
    t_scope;
    t_SB_1k(out);
    bool eos;

    *clen = -1;
    http2_conn_pack_single_hdr(w, LSTR_IMMED_V(":status"), status, &out);
    while (!ps_done(&headerlines)) {
        lstr_t key;
        lstr_t val;

        http2_headerlines_get_next_hdr(&headerlines, &key, &val);
        if (!http2_is_valid_response_hdr_to_send(key, val, clen)) {
            continue;
        }
        http2_conn_pack_single_hdr(w, key, val, &out);
    }
    eos = (*clen == 0);
    http2_conn_send_headers_block(w, stream->id, ps_initsb(&out), eos);
    if (eos) {
        http2_stream_do_on_events(w, stream, STREAM_FLAG_EOS_SENT);
    }
    http2_stream_do_update_info(w, stream);
}

static bool
http2_is_valid_request_hdr_to_send(lstr_t key_, lstr_t val, int *clen)
{
    pstream_t key = ps_initlstr(&key_);
    int rc;

    switch (http_wkhdr_from_ps(key)) {
    case HTTP_WKHDR_CONNECTION:
    case HTTP_WKHDR_TRANSFER_ENCODING:

        return false;
    case HTTP_WKHDR_CONTENT_LENGTH:
        rc = lstr_to_int(val, clen);
        assert(!rc);
        break;
    default:
        break;
    }
    return true;
}

static void
http2_stream_send_request_headers(http2_conn_t *w, http2_stream_t *stream,
                                  lstr_t method, lstr_t scheme, lstr_t path,
                                  lstr_t authority, pstream_t headerlines,
                                  httpc_http2_ctx_t *httpc_ctx, int *clen)
{
    SB_1k(out);
    unsigned events;
    bool eos;

    *clen = -1;
    http2_conn_pack_single_hdr(w, LSTR_IMMED_V(":method"), method, &out);
    http2_conn_pack_single_hdr(w, LSTR_IMMED_V(":scheme"), scheme, &out);
    http2_conn_pack_single_hdr(w, LSTR_IMMED_V(":path"), path, &out);
    if (authority.len) {
        http2_conn_pack_single_hdr(w, LSTR_IMMED_V(":authority"), authority,
                                   &out);
    }
    while (!ps_done(&headerlines)) {
        lstr_t key;
        lstr_t val;

        http2_headerlines_get_next_hdr(&headerlines, &key, &val);
        if (!http2_is_valid_request_hdr_to_send(key, val, clen)) {
            continue;
        }
        http2_conn_pack_single_hdr(w, key, val, &out);
    }
    eos = (*clen == 0);
    http2_conn_send_headers_block(w, stream->id, ps_initsb(&out), eos);
    events = STREAM_FLAG_INIT_HDRS | (eos ? STREAM_FLAG_EOS_SENT : 0);
    http2_stream_do_on_events(w, stream, events);
    stream->info.ctx.httpc_ctx = httpc_ctx;
    http2_stream_do_update_info(w, stream);
}

static void http2_stream_send_data(http2_conn_t *w, http2_stream_t *stream,
                                   pstream_t data, bool eos)
{
    int len = ps_len(&data);

    assert(stream->info.send_window >= len);
    stream->info.send_window -= len;
    http2_conn_send_data_block(w, stream->id, data, eos);
    if (eos) {
        http2_stream_do_on_events(w, stream, STREAM_FLAG_EOS_SENT);
    }
    http2_stream_do_update_info(w, stream);
}

#define http2_stream_send_reset(w, stream, fmt, ...)                         \
    do {                                                                     \
        http2_stream_error(w, stream, PROTOCOL_ERROR, fmt, ##__VA_ARGS__);   \
        http2_stream_do_update_info(w, stream);                              \
    } while (0)

#define http2_stream_send_reset_cancel(w, stream, fmt, ...)                  \
    do {                                                                     \
        http2_stream_error(w, stream, CANCEL, fmt, ##__VA_ARGS__);           \
        http2_stream_do_update_info(w, stream);                              \
    } while (0)

/* }}} */
/* {{{ Stream-Related Frame Handling */

#define http2_stream_conn_error(w, stream, error_code, fmt, ...)             \
    ({                                                                       \
        http2_stream_trace(w, stream, 2, "connection error :" fmt,           \
                           ##__VA_ARGS__);                                   \
        http2_stream_conn_error_(w, stream, HTTP2_CODE_##error_code, fmt,    \
                                 ##__VA_ARGS__);                             \
    })

__attribute__((format(printf, 4, 5)))
static int http2_stream_conn_error_(http2_conn_t *w,
                                    const http2_stream_t *stream,
                                    uint32_t error_code,
                                    const char *fmt, ...)
{
    t_scope;
    lstr_t debug;
    va_list ap;

    va_start(ap, fmt);
    debug = t_lstr_vfmt(fmt, ap);
    va_end(ap);
    return http2_conn_send_error(w, error_code, debug);
}

#define http2_stream_error(w, stream, error_code, fmt, ...)                  \
    do {                                                                     \
        http2_stream_trace(w, stream, 2, "stream error: " fmt,               \
                           ##__VA_ARGS__);                                   \
        http2_conn_send_rst_stream(w, (stream)->id,                          \
                                   HTTP2_CODE_##error_code);                 \
        http2_stream_do_on_events(w, stream, STREAM_FLAG_RST_SENT);          \
    } while (0)

static void
http2_stream_maintain_recv_window(http2_conn_t *w, http2_stream_t *stream)
{
    int incr;

    incr =
        http2_get_settings(w).initial_window_size - stream->info.recv_window;
    if (incr <= 0) {
        return;
    }
    http2_conn_send_window_update(w, stream->id, incr);
    stream->info.recv_window += incr;
}

static int
http2_stream_consume_recv_window(http2_conn_t *w, http2_stream_t *stream,
                                 unsigned delta)
{
    assert(delta <= http2_get_settings(w).max_frame_size);

    /* maintain the recv window at the initial_window_size settings each time
     * the peer sends DATA frame */
    stream->info.recv_window -= delta;
    http2_stream_maintain_recv_window(w, stream);
    return 0;
}

static int
http2_stream_do_recv_data(http2_conn_t *w, uint32_t stream_id, pstream_t data,
                          int initial_payload_len, bool eos)
{
    http2_stream_t stream = http2_stream_get(w, stream_id);
    unsigned flags = stream.info.flags;
    http2_stream_ctx_t ctx = stream.info.ctx;

    if (flags & STREAM_FLAG_CLOSED) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "DATA on closed stream");
    }
    if (flags & STREAM_FLAG_EOS_RECV) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "DATA on half-closed (remote) stream");
    }
    if (!flags) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "DATA on idle stream");
    }
    if (eos) {
        http2_stream_do_on_events(w, &stream, STREAM_FLAG_EOS_RECV);
    }
    RETHROW(http2_stream_consume_recv_window(w, &stream,
                                             initial_payload_len));
    http2_stream_do_update_info(w, &stream);
    if (!(flags & STREAM_FLAG_RST_SENT)) {
        http2_stream_on_data(w, stream, ctx, data, eos);
    }
    return 0;
}

static unsigned http2_valid_pseudo_hdr_combination_g[] = {
    0,
    HTTP2_HDR_FLAG_HAS_STATUS,
    HTTP2_HDR_FLAG_HAS_SCHEME | HTTP2_HDR_FLAG_HAS_PATH
        | HTTP2_HDR_FLAG_HAS_METHOD,
    HTTP2_HDR_FLAG_HAS_SCHEME | HTTP2_HDR_FLAG_HAS_PATH
        | HTTP2_HDR_FLAG_HAS_METHOD | HTTP2_HDR_FLAG_HAS_AUTHORITY,
};

static bool http2_stream_validate_recv_headrs(http2_header_info_t *info)
{
    unsigned flags = info->flags;

    if (flags & HTTP2_HDR_FLAG_HAS_EXTRA_PSEUDO_HDR) {
        return false;
    }
    flags &= ~(HTTP2_HDR_FLAG_HAS_CONTENT_LENGTH
               | HTTP2_HDR_FLAG_HAS_REGULAR_HEADERS);

    carray_for_each_entry(e, http2_valid_pseudo_hdr_combination_g) {
        if (e == flags) {
            return true;
        }
    }
    return false;
}

static int http2_stream_do_recv_headers(http2_conn_t *w, uint32_t stream_id,
                                        http2_header_info_t *info,
                                        pstream_t headerlines, bool eos)
{
    http2_stream_t stream = http2_stream_get(w, stream_id);
    http2_stream_ctx_t ctx = stream.info.ctx;
    unsigned flags = stream.info.flags;
    unsigned events = 0;

    if (http2_stream_id_is_server(stream_id)) {
        if (!(flags & STREAM_FLAG_PSH_RECV)) {
            return http2_stream_conn_error(
                w, &stream, PROTOCOL_ERROR,
                "HEADERS on server stream (invalid state)");
        }
        assert(w->is_client);
        /* Discard (responses) headers on server streams. This may happen for
         * a short period in the begining of communicaition since we don't
         * support them and the server must not send them once it acknowledges
         * our initial settings. However, it may start push such streams
         * before acknowledging our settings that disables them. */
        return 0;
    }
    if (flags & STREAM_FLAG_CLOSED) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "HEADERS on closed stream");
    }
    if (flags & STREAM_FLAG_EOS_RECV) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "HEADERS on half-closed (remote) stream");
    }
    if (w->is_client && !flags) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "HEADERS from server on idle client stream");
    }
    if (!http2_stream_validate_recv_headrs(info)) {
        if (!flags) {
            http2_stream_do_on_events(w, &stream, STREAM_FLAG_INIT_HDRS);
        }
        http2_stream_error(w, &stream, PROTOCOL_ERROR,
                           "HEADERS with invalid HTTP headers");
        http2_stream_do_update_info(w, &stream);
        http2_stream_on_reset(w, stream, ctx, false);
        return 0;
    }
    if (!flags) {
        events |= STREAM_FLAG_INIT_HDRS;
    }
    if (eos) {
        events |= STREAM_FLAG_EOS_RECV;
    }
    if (events) {
        http2_stream_do_on_events(w, &stream, events);
        http2_stream_do_update_info(w, &stream);
    } else {
        assert(flags);
    }
    if (!flags && w->is_shutdown_recv) {
        http2_stream_error(
            w, &stream, REFUSED_STREAM,
            "server is finalizing, no more stream is accepted");
        http2_stream_do_update_info(w, &stream);
        http2_stream_on_reset(w, stream, ctx, false);
    }
    if (!(flags & STREAM_FLAG_RST_SENT)) {
        http2_stream_on_headers(w, stream, ctx, info, headerlines, eos);
    }
    return 0;
}

static int http2_stream_do_recv_priority(http2_conn_t *w, uint32_t stream_id,
                                         uint32_t stream_dependency)
{
    http2_stream_t stream = http2_stream_get(w, stream_id);

    /* Priority frames can be received in any stream state */
    /* XXX: we don't support stream prioritization. However, a minimal
     * processing is to check against self-dependency error. */
    if (stream_dependency == stream_id) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "frame error: PRIORITY with self-dependency [%d]",
            stream_dependency);
    }
    http2_stream_trace(w, &stream, 2, "PRIORITY [dependency on %d]",
                       stream_dependency);
    return 0;
}

static int
http2_stream_do_recv_rst_stream(http2_conn_t *w, uint32_t stream_id,
                                uint32_t error_code)
{
    http2_stream_t stream = http2_stream_get(w, stream_id);
    http2_stream_ctx_t ctx = stream.info.ctx;
    unsigned flags = stream.info.flags;

    if (flags & STREAM_FLAG_CLOSED) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "RST_STREAM on closed stream [code %u]", error_code);
    }
    if (!flags) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "RST_STREAM on idle stream [code %u]",
                                       error_code);
    }
    if (flags & STREAM_FLAG_RST_SENT) {
        http2_stream_trace(w, &stream, 2,
                           "RST_STREAM ingored (rst sent already) [code %u]",
                           error_code);
        http2_stream_do_on_events(w, &stream, STREAM_FLAG_RST_RECV);
        http2_stream_do_update_info(w, &stream);
        return 0;
    }
    http2_stream_do_on_events(w, &stream, STREAM_FLAG_RST_RECV);
    http2_stream_on_reset(w, stream, ctx, true);
    http2_stream_do_update_info(w, &stream);
    return 0;
}

static int
http2_stream_do_recv_push_promise(http2_conn_t *w, uint32_t stream_id,
                                  http2_header_info_t *info,
                                  pstream_t headerlines, uint32_t promised_id)
{
    http2_stream_t stream = http2_stream_get(w, stream_id);
    http2_stream_t promised = http2_stream_get(w, promised_id);
    unsigned flags = stream.info.flags;

    assert(w->is_client);
    assert(!promised.info.flags);
    if (http2_stream_id_is_server(stream_id)) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "cannot accept promised stream %d on a server stream",
            promised_id);
    }
    if (flags & STREAM_FLAG_CLOSED) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "PUSH_STREAM on closed stream");
    }
    if (!flags) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "PUSH_STREAM on idle stream");
    }
    /* RFC 9113 §6.6. PUSH_PROMISE:
     * `PUSH_PROMISE frames MUST only be sent on a peer-initiated stream that
     * is in either the "open" or "half-closed (remote)" state. is in either
     * the "open" or "half-closed (remote)" state.`*/
    /* So, w.r.t the client, this means that push promise can be received only
     * on a stream that is either open or half-closed (local) [or closing by
     * RST_SENT]. */
    if (flags & STREAM_FLAG_EOS_RECV) {
        return http2_stream_conn_error(
            w, &stream, PROTOCOL_ERROR,
            "PUSH_STREAM on half-closed (remote) stream");
    }
    /* Refuse the pushed stream: not supported (yet). */
    promised.info.flags |= STREAM_FLAG_PSH_RECV;
    http2_stream_error(w, &promised, REFUSED_STREAM,
                       "refuse push promise (not supported)");
    http2_stream_do_update_info(w, &promised);
    return 0;
}

static int
http2_stream_do_recv_window_update(http2_conn_t *w, uint32_t stream_id,
                                   int32_t incr)
{
    http2_stream_t stream = http2_stream_get(w, stream_id);
    unsigned flags = stream.info.flags;
    int64_t new_size = (int64_t)stream.info.send_window + incr;

    assert(incr >= 0);
    if (flags & STREAM_FLAG_CLOSED) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "WINDOW_UPDATE on closed stream");
    }
    if (!flags) {
        return http2_stream_conn_error(w, &stream, PROTOCOL_ERROR,
                                       "WINDOW_UPDATE on idle stream");
    }
    if (!incr) {
        http2_stream_error(w, &stream, PROTOCOL_ERROR,
                           "frame error: WINDOW_UPDATE with 0 increment");
        http2_stream_do_update_info(w, &stream);
        return 0;
    }
    if (new_size > HTTP2_LEN_WINDOW_SIZE_LIMIT) {
        if (flags & STREAM_FLAG_RST_SENT) {
            http2_stream_trace(
                w, &stream, 2,
                "flow control: ignored WINDOW_UPDATE (already RST_SENT)");
            return 0;
        }
        http2_stream_error(
            w, &stream, FLOW_CONTROL_ERROR,
            "flow control: WINDOW_UPDATE cannot increment send-window beyond "
            "limit [cur %d, incr %d, new %jd]",
            stream.info.send_window, incr, new_size);
        http2_stream_do_update_info(w, &stream);
        return 0;
    }
    http2_stream_trace(w, &stream, 2,
                       "send-window incremented [new size %lld, incr %d]",
                       (long long)new_size, incr);
    stream.info.send_window += incr;
    http2_stream_do_update_info(w, &stream);
    return 0;
}

/* }}} */
/* {{{ Stream-Related Frame Parsing */

#define HTTP2_THROW_ERR(w, error_code, fmt, ...)                             \
    do {                                                                     \
        return http2_conn_error(w, error_code, fmt, ##__VA_ARGS__);          \
    } while (0)

#define http2_conn_error(w, error_code, fmt, ...)                            \
    ({                                                                       \
        http2_conn_trace(w, 2, "connection error :" fmt, ##__VA_ARGS__);     \
        http2_conn_error_(w, HTTP2_CODE_##error_code, fmt, ##__VA_ARGS__);   \
    })

__attribute__((format(printf, 3, 4)))
static int http2_conn_error_(http2_conn_t *w, uint32_t error_code,
                             const char *fmt, ...)
{
    t_scope;
    lstr_t debug;
    va_list ap;

    va_start(ap, fmt);
    debug = t_lstr_vfmt(fmt, ap);
    va_end(ap);
    return http2_conn_send_error(w, error_code, debug);
}

__must_check__ static int
http2_parse_frame_hdr(pstream_t *ps, http2_frame_info_t *frame)
{
    const http2_frame_hdr_t *hdr;

    hdr = RETHROW_PN(ps_get_data(ps, sizeof(http2_frame_hdr_t)));

    /* XXX: hdr->len is a bitfield. */
    frame->len = get_unaligned_be24(hdr);
    frame->type = hdr->type;
    frame->flags = hdr->flags;
    frame->stream_id = HTTP2_STREAM_ID_MASK & cpu_to_be32(hdr->stream_id);

    return 0;
}

static void http2_conn_maintain_recv_window(http2_conn_t *w)
{
    int incr = HTTP2_LEN_WINDOW_SIZE_LIMIT - w->recv_window;

    if (incr <= 0) {
        return;
    }
    http2_conn_send_window_update(w, 0, incr);
    w->recv_window += incr;
}

static void
http2_conn_consume_recv_window(http2_conn_t *w, int len)
{
    /* Maintain the recv window at a specific level each time the peer
    * sends DATA frame. This effectively disables the flow control. */
    w->recv_window -= len;
    http2_conn_maintain_recv_window(w);
}

static int
http2_payload_get_trimmed_chunk(pstream_t payload, int frame_flags,
                                pstream_t *chunk)
{
    if (frame_flags & HTTP2_FLAG_PADDED) {
        int padding_sz;

        padding_sz = RETHROW(ps_getc(&payload));
        RETHROW(ps_shrink(&payload, padding_sz));
    }
    *chunk = payload;
    return 0;
}

static int http2_conn_parse_data(http2_conn_t *w, uint32_t stream_id,
                                 pstream_t payload, uint8_t flags)
{
    bool end_stream;
    int initial_payload_len;
    pstream_t chunk;

    initial_payload_len = ps_len(&payload);
    if (http2_payload_get_trimmed_chunk(payload, flags, &chunk) < 0) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: invalid padding on stream %d",
                        stream_id);
    }

    http2_conn_consume_recv_window(w, initial_payload_len);
    end_stream = flags & HTTP2_FLAG_END_STREAM;
    return http2_stream_do_recv_data(w, stream_id, chunk, initial_payload_len,
                                     end_stream);
}

/** Consolidate a header block from an already-validated multiframe.
 *
 * Note: \p multiframe is composed of either a HEADERS or PUSH_PROMISE frame
 * (followed by 0 or more CONTINUATION frame(s)). The multiframe components
 * are already parsed (and verified) but is kept in the connection buffer
 * until the coming of the END_OF_HEADERS flag where the embeded header block
 * is reconstructed before decoded by the HPACK decoder. The type of initial
 * frame is conveyed by \p promised_id.
 */
/* TODO: Bench performance against a version of the function that uses the
 * safe counterparts of __ps_* .*/
static void
http2_conn_construct_hdr_blk(http2_conn_t *w, pstream_t multiframe,
                             size_t initial_len, uint8_t flags,
                             uint32_t promised_id, sb_t *blk)
{
    pstream_t chunk;
    int chunk_len;

    chunk_len = initial_len;
    chunk = __ps_get_ps(&multiframe, chunk_len);
    if (flags & HTTP2_FLAG_PADDED) {
        uint8_t padding = __ps_getc(&chunk);

        __ps_shrink(&chunk, padding);
    }
    if (!promised_id) {
        /* block in HEADERS + 0 or more CONTINUATION(s). */
        if (flags & HTTP2_FLAG_PRIORITY) {
            __ps_skip(&chunk, 4 + 1); /* stream dependency (4) + weight (1) */
        }
    } else {
        /* block in PUSH_PROMISE + 0 or more CONTINUATION(s). */
        __ps_skip(&chunk, 4); /* promised_id (4) */
    }
    sb_add_ps(blk, chunk);
    while (!ps_done(&multiframe)) {
        chunk_len = __ps_get_be24(&multiframe);
        __ps_skip(&multiframe, HTTP2_LEN_FRAME_HDR - 3);
        chunk = __ps_get_ps(&multiframe, chunk_len);
        sb_add_ps(blk, chunk);
    }
}

static int http2_conn_do_on_end_headers(http2_conn_t *w, uint32_t stream_id,
                                        pstream_t ps, size_t initial_len,
                                        uint32_t flags, uint32_t promised_id)
{
    t_scope;
    http2_header_info_t info;
    bool end_stream;
    SB_8k(blk);
    SB_8k(headerlines);
    int rc;

    http2_conn_construct_hdr_blk(w, ps, initial_len, flags, promised_id,
                                 &blk);
    rc = t_http2_conn_decode_header_block(w, ps_initsb(&blk), &info,
                                          &headerlines);
    if (rc < 0) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "compression error: "
                        "invalid header block on stream %d",
                        stream_id);
    }
    if (promised_id) {
        /* We have block as PUSH + 0 or more CONTINUATION(s). */
        return http2_stream_do_recv_push_promise(
            w, stream_id, &info, ps_initsb(&headerlines), promised_id);
    }
    /* We have block as HEADERS + 0 or more CONTINUATION(s). */
    end_stream = flags & HTTP2_FLAG_END_STREAM;
    return http2_stream_do_recv_headers(w, stream_id, &info,
                                        ps_initsb(&headerlines), end_stream);
}

static bool http2_conn_is_server_push_enabled(http2_conn_t *w)
{
    return http2_get_settings(w).enable_push && w->peer_settings.enable_push;
}

static int http2_conn_parse_headers(http2_conn_t *w, uint32_t stream_id,
                                    pstream_t payload, uint8_t flags)
{
    pstream_t chunk;

    if (http2_payload_get_trimmed_chunk(payload, flags, &chunk) < 0) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: HEADERS with invalid padding");
    }

    if (flags & HTTP2_FLAG_PRIORITY) {
        uint32_t stream_dependency;

        if (ps_get_be32(&chunk, &stream_dependency) < 0) {
            HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR,
                            "frame error: "
                            "HEADERS is too short to read stream dependency");
        }
        stream_dependency &= HTTP2_STREAM_ID_MASK;

        /* XXX: we ignore stream (re)-prioritization scheme. However, a
         * minimal processing is to check against self-dependency error */
        if (stream_dependency == stream_id) {
            HTTP2_THROW_ERR(
                w, PROTOCOL_ERROR,
                "frame error: self-dependency in HEADERS on stream %d",
                stream_id);
        }
    }
    if (flags & HTTP2_FLAG_END_HEADERS) {
        return http2_conn_do_on_end_headers(w, stream_id, payload,
                                            ps_len(&payload), flags, 0);
    }
    return PARSE_OK;
}

static int http2_conn_parse_push_promise(http2_conn_t *w, uint32_t stream_id,
                                         pstream_t payload, uint8_t flags)
{
    pstream_t chunk;
    uint32_t promised_id;

    assert(w->is_client);
    if (http2_payload_get_trimmed_chunk(payload, flags, &chunk) < 0) {
        HTTP2_THROW_ERR(
            w, PROTOCOL_ERROR,
            "frame error: PUSH_PROMISE with invalid padding");
    }

    if (ps_get_be32(&chunk, &promised_id) < 0) {
        HTTP2_THROW_ERR(
            w, FRAME_SIZE_ERROR,
            "frame error: PUSH_PROMISE too short to read promised id");
    }
    promised_id &= HTTP2_STREAM_ID_MASK;

    if (http2_conn_check_peer_stream_id(w, promised_id)) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                                "frame error: promised_id is PUSH_PROMISE is "
                                "not server stream %d",
                                promised_id);
    }

    if (!http2_conn_peer_stream_id_is_idle(w, promised_id)) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: invalid promised stream %d "
                        "in PUSH_PROMISE on stream %d",
                        promised_id, stream_id);
    }
    if (!http2_conn_is_server_push_enabled(w)) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "settings error: unexpected PUSH_PROMISE on "
                        "stream %d (server push disabled)",
                        stream_id);
    }
    w->promised_id = promised_id;
    if (flags & HTTP2_FLAG_END_HEADERS) {
        return http2_conn_do_on_end_headers(
            w, stream_id, payload, ps_len(&payload), flags, promised_id);
    }
    return PARSE_OK;
}

static int http2_conn_parse_priority(http2_conn_t *w, uint32_t stream_id,
                                     pstream_t payload, uint8_t flags)
{
    int len = ps_len(&payload);
    uint32_t stream_dependency;
    int weight;

    if (ps_get_be32(&payload, &stream_dependency) < 0) {
        goto size_error;
    }
    stream_dependency &= HTTP2_STREAM_ID_MASK;

    weight = ps_getc(&payload);
    if (weight < 0) {
        goto size_error;
    }

    if (!ps_done(&payload)) {
        goto size_error;
    }

    RETHROW(http2_stream_do_recv_priority(w, stream_id, stream_dependency));
    return PARSE_OK;

size_error:
    HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR,
                    "frame error: PRIORITY with invalid size %d", len);
}

static int http2_conn_parse_rst_stream(http2_conn_t *w, uint32_t stream_id,
                                       pstream_t payload, uint8_t flags)
{
    uint32_t error_code;

    if (ps_get_be32(&payload, &error_code) < 0) {
        HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR,
                        "frame error: RST_STREAM with invalid size %jd",
                        ps_len(&payload));
    }
    RETHROW(http2_stream_do_recv_rst_stream(w, stream_id, error_code));
    return PARSE_OK;
}

/* }}} */
/* {{{ Connection-Related Frame Handling */

static int
http2_conn_on_peer_initial_window_size_changed(http2_conn_t *w, int32_t delta)
{
    int64_t new_size;

    if (!delta) {
        return PARSE_OK;
    }
    qm_for_each_pos(qstream_info, pos, &w->stream_info) {
        http2_stream_t stream = {
            .id = w->stream_info.keys[pos],
            .info = w->stream_info.values[pos],
        };
        unsigned flags = stream.info.flags;

        assert(flags && !(flags & STREAM_FLAG_CLOSED));
        new_size = (int64_t)stream.info.send_window + delta;
        if (new_size > HTTP2_LEN_WINDOW_SIZE_LIMIT) {
            HTTP2_THROW_ERR(
                w, FLOW_CONTROL_ERROR,
                "settings error: INITIAL_WINDOW_SIZE causes stream %d "
                "send-window to overflow (%jd out of range)",
                stream.id, new_size);
        }
        stream.info.send_window += delta;
        http2_stream_trace(
            w, &stream, 2,
            "send-window updated by SETTINGS [new size %d, delta %d]",
            stream.info.send_window, delta);
        w->stream_info.values[pos].send_window = stream.info.send_window;
    }
    return PARSE_OK;
}

static int
http2_conn_process_peer_settings(http2_conn_t *w, uint16_t id, uint32_t val)
{
    int32_t delta;

    switch (id) {
    case HTTP2_ID_HEADER_TABLE_SIZE:
        if (val != w->peer_settings.header_table_size) {
            w->enc.tbl_size_max = val;
        }
        w->peer_settings.header_table_size = val;
        break;

    case HTTP2_ID_ENABLE_PUSH:
        if (val > 1) {
            HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                            "settings error: invalid ENABLE_PUSH (%u)", val);
        }
        w->peer_settings.enable_push = val;
        break;

    case HTTP2_ID_MAX_CONCURRENT_STREAMS:
        OPT_SET(w->peer_settings.max_concurrent_streams, val);
        break;

    case HTTP2_ID_MAX_FRAME_SIZE:
        if (val < HTTP2_LEN_MAX_FRAME_SIZE_INIT ||
            val > HTTP2_LEN_MAX_FRAME_SIZE)
        {
            HTTP2_THROW_ERR(
                w, PROTOCOL_ERROR,
                "settings error: invalid FRAME_SIZE (%u out of range)", val);
        }
        w->peer_settings.max_frame_size = val;
        break;

    case HTTP2_ID_MAX_HEADER_LIST_SIZE:
        OPT_SET(w->peer_settings.max_header_list_size, val);
        break;

    case HTTP2_ID_INITIAL_WINDOW_SIZE:
        if (val > HTTP2_LEN_WINDOW_SIZE_LIMIT) {
            HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                            "settings error: invalid "
                            "INITIAL_WINDOW_SIZE (%u out of range)",
                            val);
        }

        /* XXX Make sure that the cast '(int32_t)val' is legitimate. */
        STATIC_ASSERT(HTTP2_LEN_WINDOW_SIZE_LIMIT == INT32_MAX);

        delta = (int32_t)val - w->peer_settings.initial_window_size;
        w->peer_settings.initial_window_size = val;
        RETHROW(http2_conn_on_peer_initial_window_size_changed(w, delta));
        break;

    default:
        http2_conn_trace(w, 2,
                         "ignored unknown setting from peer [id %d, val %u]",
                         id, val);
    }
    return PARSE_OK;
}

static int
http2_conn_parse_settings(http2_conn_t *w, pstream_t payload, uint8_t flags)
{
    size_t len = ps_len(&payload);
    int nb_items;

    if ((flags & HTTP2_FLAG_ACK) && len) {
        HTTP2_THROW_ERR(
            w, PROTOCOL_ERROR,
            "frame error: invalid SETTINGS (ACK_FLAG with non-zero payload)");
    }
    if (flags & HTTP2_FLAG_ACK) {
        if (w->is_settings_acked) {
            HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                            "frame error: invalid SETTINGS (ACK with "
                            "no previously sent SETTINGS)");
        }
        w->is_settings_acked = true;
        return PARSE_OK;
    }
    if (len % HTTP2_LEN_SETTINGS_ITEM != 0) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: invalid SETTINGS (payload size "
                        "not a multiple of 6)");
    }
    /* new peer settings */
    nb_items = len / HTTP2_LEN_SETTINGS_ITEM;
    for (int i = 0; i != nb_items; i++) {
        uint16_t id = __ps_get_be16(&payload);
        uint32_t val = __ps_get_be32(&payload);

        RETHROW(http2_conn_process_peer_settings(w, id, val));
    }
    http2_conn_send_common_hdr(w, HTTP2_LEN_NO_PAYLOAD, HTTP2_TYPE_SETTINGS,
                               HTTP2_FLAG_ACK, HTTP2_ID_NO_STREAM);
    return PARSE_OK;
}

static int
http2_conn_parse_ping(http2_conn_t *w, pstream_t payload, uint8_t flags)
{
    size_t len = ps_len(&payload);

    STATIC_ASSERT(HTTP2_LEN_PING_PAYLOAD == 8);
    if (len != HTTP2_LEN_PING_PAYLOAD) {
        HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR,
                        "frame error: invalid PING size");
    }
    if (w->frame.flags & HTTP2_FLAG_ACK) {
        /* TODO: correlate the acked frame with a sent one and estimate the
         * ping rtt. */
    } else {
        http2_conn_send_common_hdr(w, HTTP2_LEN_PING_PAYLOAD, HTTP2_TYPE_PING,
                                   HTTP2_FLAG_ACK, HTTP2_ID_NO_STREAM);
        ob_add(&w->ob, payload.p, HTTP2_LEN_PING_PAYLOAD);
    }
    return PARSE_OK;
}

static int
http2_conn_parse_goaway(http2_conn_t *w, pstream_t payload, uint8_t flags)
{
    size_t len = w->frame.len;
    uint32_t last_stream_id;
    uint32_t error_code;
    pstream_t debug;

    STATIC_ASSERT(HTTP2_LEN_GOAWAY_PAYLOAD_MIN
                  == sizeof last_stream_id + sizeof error_code);
    if (len < HTTP2_LEN_GOAWAY_PAYLOAD_MIN) {
    }
    if (ps_get_be32(&payload, &last_stream_id) < 0) {
        goto size_error;
    }
    last_stream_id &= HTTP2_STREAM_ID_MASK;

    if (ps_get_be32(&payload, &error_code)) {
        goto size_error;
    }
    debug = payload;
    http2_conn_trace(w, 2, "received GOAWAY "
                     "[last stream %d, error code %d, debug <%*pM>]",
                     last_stream_id, error_code, PS_FMT_ARG(&debug));

    if (error_code == HTTP2_CODE_NO_ERROR) {
        if (last_stream_id == HTTP2_ID_MAX_STREAM) {
            if (w->is_shutdown_recv) {
                HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                                "frame error: second shutdown GOAWAY");
            }
            w->is_shutdown_recv = true;
        } else {
            w->is_shutdown_soon_recv = true;
        }
    } else {
        w->is_conn_err_recv = true;
    }
    return PARSE_OK;

size_error:
    HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR, "frame error: invalid GOAWAY size");
}

static int http2_conn_parse_window_update(http2_conn_t *w, uint32_t stream_id,
                                          pstream_t payload, uint8_t flags)
{
    uint32_t incr;
    int64_t new_size;

    if (ps_get_be32(&payload, &incr) < 0 || !ps_done(&payload)) {
        HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR,
                        "frame error: invalid WINDOW_UPDATE size");
    }
    incr &= HTTP2_LEN_MAX_WINDOW_UPDATE_INCR;

    if (stream_id) {
        /* incr conn send-window */
        return http2_stream_do_recv_window_update(w, stream_id, incr);
    }
    if (!incr) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: 0 increment in WINDOW_UPDATE");
    }
    new_size = (int64_t)w->send_window + incr;
    if (new_size > HTTP2_LEN_WINDOW_SIZE_LIMIT) {
        HTTP2_THROW_ERR(w, FLOW_CONTROL_ERROR,
                        "flow control: "
                        "tried to increment send-window beyond limit "
                        "[cur %d, incr %d, new %jd]",
                        w->send_window, incr, new_size);
    }
    http2_conn_trace(w, 2, "send-window increment [new size %jd, incr %d]",
                     new_size, incr);
    w->send_window = new_size;
    return PARSE_OK;
}

/* }}} */
/* {{{ Receive Buffer Framing */

static bool http2_is_known_frame_type(uint8_t type)
{
    return type <= HTTP2_TYPE_CONTINUATION;
}

static int http2_conn_check_frame_type_role(http2_conn_t *w)
{
    if (!w->is_client && w->frame.type == HTTP2_TYPE_PUSH_PROMISE) {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "PUSH_PROMISE received from client");
    }
    return PARSE_OK;
}

/** Check if current frame type is compatible with the level `stream_id`.
 * Note: 0 => Connection-level frame, > 0 => Stream-level frame */
static int http2_conn_check_frame_type_level(http2_conn_t *w)
{
    uint8_t type = w->frame.type;
    uint32_t stream_id = w->frame.stream_id;

    switch (type) {
    case HTTP2_TYPE_DATA:
    case HTTP2_TYPE_HEADERS:
    case HTTP2_TYPE_PRIORITY:
    case HTTP2_TYPE_RST_STREAM:
    case HTTP2_TYPE_PUSH_PROMISE:
    case HTTP2_TYPE_CONTINUATION:
        if (likely(stream_id)) {
            return PARSE_OK;
        }
        break;

    case HTTP2_TYPE_SETTINGS:
    case HTTP2_TYPE_PING:
    case HTTP2_TYPE_GOAWAY:
        if (likely(!stream_id)) {
            return PARSE_OK;
        }
        break;

    case HTTP2_TYPE_WINDOW_UPDATE:
        return PARSE_OK;

    default:
        assert(false && "unexpected frame type");
    }
    HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                    "frame error: type %x incompatible with stream id %u",
                    type, stream_id);
}

static int http2_conn_check_frame_size(http2_conn_t *w, uint32_t len)
{
    uint32_t lim = http2_get_settings(w).max_frame_size;

    if (len > lim) {
        HTTP2_THROW_ERR(w, FRAME_SIZE_ERROR,
                        "frame error: size %u > setting limit %u", len, lim);
    }
    return PARSE_OK;
}


static int http2_conn_parse_preface(http2_conn_t *w, pstream_t *ps)
{
    /* XXX: the client preamble consists of the magic preface + the initial
     * settings frame. For the server, the preamble consists of the initial
     * settings frame, IoW, the server preface is empty. So, both client and
     * server send the connection PREAMBLE in reaction to parsing the other's
     * PREFACE!
     */
    if (!w->is_client) {
        size_t len = http2_client_preface_g.len;
        pstream_t preface_recv;

        if (ps_get_ps(ps, len, &preface_recv) < 0) {
            return PARSE_MISSING_DATA;
        }
        if (!lstr_equal(LSTR_PS_V(&preface_recv), http2_client_preface_g)) {
            HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                            "parse error: invalid preface");
        }
    }
    http2_conn_send_preface(w);
    http2_conn_send_init_settings(w);
    return PARSE_OK;
}

static int http2_conn_parse_init_settings_hdr(http2_conn_t *w, pstream_t *ps)
{
    if (http2_parse_frame_hdr(ps, &w->frame) < 0) {
        return PARSE_MISSING_DATA;
    }
    if (w->frame.len > http2_get_settings(w).max_frame_size ||
        w->frame.type != HTTP2_TYPE_SETTINGS ||
        w->frame.flags & HTTP2_FLAG_ACK ||
        w->frame.len % HTTP2_LEN_SETTINGS_ITEM != 0)
    {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "invalid preamble (not a setting frame)");
    }

    return PARSE_OK;
}

static int http2_conn_parse_common_hdr(http2_conn_t *w, pstream_t *ps)
{
    if (http2_parse_frame_hdr(ps, &w->frame) < 0) {
        return PARSE_MISSING_DATA;
    }

    RETHROW(http2_conn_check_frame_size(w, w->frame.len));
    if (http2_is_known_frame_type(w->frame.type)) {
        RETHROW(http2_conn_check_frame_type_level(w));
        RETHROW(http2_conn_check_frame_type_role(w));
    }

    return PARSE_OK;
}

static int http2_conn_parse_payload(http2_conn_t *w, pstream_t *ps)
{
    pstream_t payload;
    size_t len = w->frame.len;
    uint32_t stream_id = w->frame.stream_id;
    uint8_t flags = w->frame.flags;

    if (ps_get_ps(ps, len, &payload) < 0) {
        return PARSE_MISSING_DATA;
    }

    switch (w->frame.type) {
    case HTTP2_TYPE_DATA:
        return http2_conn_parse_data(w, stream_id, payload, flags);

    case HTTP2_TYPE_HEADERS:
        return http2_conn_parse_headers(w, stream_id, payload, flags);

    case HTTP2_TYPE_PRIORITY:
        return http2_conn_parse_priority(w, stream_id, payload, flags);

    case HTTP2_TYPE_RST_STREAM:
        return http2_conn_parse_rst_stream(w, stream_id, payload, flags);

    case HTTP2_TYPE_SETTINGS:
        return http2_conn_parse_settings(w, payload, flags);

    case HTTP2_TYPE_PUSH_PROMISE:
        return http2_conn_parse_push_promise(w, stream_id, payload, flags);

    case HTTP2_TYPE_PING:
        return http2_conn_parse_ping(w, payload, flags);

    case HTTP2_TYPE_GOAWAY:
        return http2_conn_parse_goaway(w, payload, flags);

    case HTTP2_TYPE_WINDOW_UPDATE:
        return http2_conn_parse_window_update(w, stream_id, payload, flags);

    case HTTP2_TYPE_CONTINUATION:
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: CONTINUATION with no previous "
                        "HEADERS or PUSH_PROMISE");

    default:
        break;
    }
    http2_conn_trace(w, 2, "discarded received frame with unknown type %d",
                     w->frame.type);
    return PARSE_OK;
}

/* TODO use some box drawing to explain the mechanics */
static int http2_conn_parse_cont_hdr(http2_conn_t *w, pstream_t ps)
{
    http2_frame_info_t frame;

    if (ps_skip(&ps, w->frame.len + w->cont_chunk) < 0 ||
        http2_parse_frame_hdr(&ps, &frame) < 0)
    {
        return PARSE_MISSING_DATA;
    }

    RETHROW(http2_conn_check_frame_size(w, frame.len));
    assert(w->frame.stream_id);
    if (frame.type != HTTP2_TYPE_CONTINUATION ||
        frame.stream_id != w->frame.stream_id)
    {
        HTTP2_THROW_ERR(w, PROTOCOL_ERROR,
                        "frame error: missing CONTINUATION");
    }
    w->frame.flags |= (frame.flags & HTTP2_FLAG_END_HEADERS);
    w->cont_chunk += HTTP2_LEN_FRAME_HDR + frame.len;
    return PARSE_OK;
}

static int http2_conn_parse_cont_fragment(http2_conn_t *w, pstream_t *ps)
{
    size_t initial_len = w->frame.len;
    size_t len = w->frame.len + w->cont_chunk;
    uint32_t stream_id = w->frame.stream_id;
    uint8_t flags = w->frame.flags;
    uint32_t promised_id = w->promised_id;

    assert(w->cont_chunk);
    if (ps_len(ps) < len) {
        return PARSE_MISSING_DATA;
    }
    if (flags & HTTP2_FLAG_END_HEADERS) {
        pstream_t payload;

        payload = __ps_get_ps(ps, len);
        assert((!!promised_id) ^ (w->frame.type == HTTP2_TYPE_HEADERS));
        return http2_conn_do_on_end_headers(w, stream_id, payload,
                                            initial_len, flags, promised_id);
    }
    /* XXX: No END_HEADERS yet: continue to keep the chunks in place to be
     * reassembled later in http2_conn_do_on_end_headers() when END_HEADERS
     * arrives. */
    return PARSE_OK;
}

static int http2_conn_parse_shutdown_sent(http2_conn_t *w, pstream_t *ps)
{
    __ps_skip(ps, ps_len(ps));
    return PARSE_MISSING_DATA;
}

/* }}} */
/* {{{ Connection IO Event Handlers */

/* parser state(s) */
typedef enum {
    HTTP2_PARSE_PREAMBLE             = 0,
    HTTP2_PARSE_INIT_SETTINGS_HDR    = 1,
    HTTP2_PARSE_COMMON_HDR           = 2,
    HTTP2_PARSE_PAYLOAD              = 3,
    HTTP2_PARSE_CONT_HDR             = 4,
    HTTP2_PARSE_CONT_FRAGMENT        = 5,
    HTTP2_PARSE_SHUTDOWN_SENT        = 6,
} parse_state_t;

static void http2_conn_do_on_eof_read(http2_conn_t *w, pstream_t *ps)
{
    if (w->is_conn_err_recv || w->is_conn_err_sent || w->is_shutdown_sent) {
        return;
    }
    if (!ps_done(ps)) {
        http2_conn_error(w, INTERNAL_ERROR, "unexpected eof");
    } else {
        http2_conn_send_shutdown(w, LSTR_EMPTY_V);
    }
    w->state = HTTP2_PARSE_SHUTDOWN_SENT;
}

static void http2_conn_do_parse(http2_conn_t *w, bool eof)
{
    int rc;
    pstream_t ps;
    pstream_t ps_tmp;

    assert(!w->is_conn_err_recv);
    ps = ps_initsb(&w->ibuf);
    do {
        parse_state_t state = w->state;

        switch (state) {
        case HTTP2_PARSE_PREAMBLE:
            rc = http2_conn_parse_preface(w, &ps);
            if (rc == PARSE_OK) {
                state = HTTP2_PARSE_INIT_SETTINGS_HDR;
            }
            break;
        case HTTP2_PARSE_INIT_SETTINGS_HDR:
            rc = http2_conn_parse_init_settings_hdr(w, &ps);
            if (rc == PARSE_OK) {
                state = HTTP2_PARSE_PAYLOAD;
            }
            break;
        case HTTP2_PARSE_COMMON_HDR:
            rc = http2_conn_parse_common_hdr(w, &ps);
            if (rc == PARSE_OK) {
                state = HTTP2_PARSE_PAYLOAD;
            }
            break;
        case HTTP2_PARSE_PAYLOAD:
            ps_tmp = ps;
            rc = http2_conn_parse_payload(w, &ps);
            if (rc == PARSE_OK) {
                uint8_t t = w->frame.type;

                if (t == HTTP2_TYPE_HEADERS || t == HTTP2_TYPE_PUSH_PROMISE) {
                    if (w->frame.flags & HTTP2_FLAG_END_HEADERS) {
                        state = HTTP2_PARSE_COMMON_HDR;
                    } else {
                        /* reset to a new *multi-frame* in ibuf: composed of
                         * the initial payload (of the current HEADERS or
                         * PUSH_PROMISE) and a chunk that spans one ore
                         * CONTINUATION frame(s) */
                        w->cont_chunk = 0;
                        w->promised_id = 0;
                        ps = ps_tmp;
                        state = HTTP2_PARSE_CONT_HDR;
                    }
                } else {
                    state = HTTP2_PARSE_COMMON_HDR;
                }
            }
            break;
        case HTTP2_PARSE_CONT_HDR:
            rc = http2_conn_parse_cont_hdr(w, ps);
            if (rc == PARSE_OK) {
                state = HTTP2_PARSE_CONT_FRAGMENT;
            }
            break;
        case HTTP2_PARSE_CONT_FRAGMENT:
            rc = http2_conn_parse_cont_fragment(w, &ps);
            if (rc == PARSE_OK) {
                assert(w->frame.type == HTTP2_TYPE_HEADERS
                       || w->frame.type == HTTP2_TYPE_PUSH_PROMISE);
                if (w->frame.flags & HTTP2_FLAG_END_HEADERS) {
                    state = HTTP2_PARSE_COMMON_HDR;
                } else {
                    /* continue the *multi-frame* */
                    state = HTTP2_PARSE_CONT_HDR;
                }
            }
            break;
        case HTTP2_PARSE_SHUTDOWN_SENT:
            rc = http2_conn_parse_shutdown_sent(w, &ps);
            break;
        }
        if (rc == PARSE_ERROR) {
            assert(w->is_conn_err_sent);
            state = HTTP2_PARSE_SHUTDOWN_SENT;
            rc = PARSE_OK;
        }
        w->state = state;
    } while (rc == PARSE_OK);
    if (eof) {
        http2_conn_do_on_eof_read(w, &ps);
    }
    sb_skip_upto(&w->ibuf, ps.s);
}

static void http2_conn_do_close(http2_conn_t *w)
{
    qm_for_each_key(qstream_info, stream_id, &w->stream_info) {
        http2_stream_t stream = http2_stream_get(w, stream_id);

        http2_stream_on_reset(w, stream, stream.info.ctx, false);
        stream.remove = true;
        http2_stream_do_update_info(w, &stream);
    }
    while (!dlist_is_empty(&w->closed_stream_info)) {
        http2_closed_stream_info_t *info;

        info = dlist_first_entry(&w->closed_stream_info,
                                 http2_closed_stream_info_t, list_link);
        dlist_remove(&info->list_link);
        w->closed_streams_info_cnt--;
        p_delete(&info);
    }
    http2_conn_on_close(w);
    http2_conn_release(&w);
}

static int http2_conn_do_error_write(http2_conn_t *w)
{
    http2_conn_trace(w, 2, "write error");
    http2_conn_do_close(w);
    return 0;
}

static int http2_conn_do_write(http2_conn_t *w, int fd)
{
    int ret;

    ret = w->ssl ? ob_write_with(&w->ob, fd, ssl_writev, w->ssl)
                 : ob_write(&w->ob, fd);
    if (ret < 0 && !ERR_RW_RETRIABLE(errno)) {
        return -1;
    }
    return 0;
}

static void http2_conn_do_set_mask_and_watch(http2_conn_t *w)
{
    int mask = POLLIN;

    if (ob_is_empty(&w->ob) || w->send_window <= 0) {
        el_fd_watch_activity(w->ev, POLLINOUT, 10000);
    } else {
        el_fd_watch_activity(w->ev, POLLINOUT, 0);
    }
    if (!ob_is_empty(&w->ob)) {
        mask |= POLLOUT;
    }
    el_fd_set_mask(w->ev, mask);
}

static int http2_conn_do_inact_timeout(http2_conn_t *w)
{
    http2_conn_trace(w, 2, "inactivity timeout");
    http2_conn_do_close(w);
    return 0;
}

static int http2_conn_do_error_read(http2_conn_t *w)
{
    http2_conn_trace(w, 2, "reading error");
    http2_conn_do_close(w);
    return 0;
}

static int http2_conn_do_error_recv(http2_conn_t *w)
{
    http2_conn_trace(w, 2, "connection error received");
    http2_conn_do_close(w);
    return 0;
}

static int http2_conn_do_on_streams_can_write(http2_conn_t *w)
{
    if (w->state > HTTP2_PARSE_INIT_SETTINGS_HDR) {
        http2_conn_on_streams_can_write(w);
    }
    return 0;
}

static int http2_conn_on_event(el_t evh, int fd, short events, data_t priv)
{
    http2_conn_t *w = priv.ptr;
    int read = -1;

    if (events == EL_EVENTS_NOACT) {
        return http2_conn_do_inact_timeout(w);
    }
    if (events & POLLIN) {
        read = w->ssl ? ssl_sb_read(&w->ibuf, w->ssl, 0)
                      : sb_read(&w->ibuf, fd, 0);
        if ((read < 0) && !ERR_RW_RETRIABLE(errno)) {
            return http2_conn_do_error_read(w);
        }
        http2_conn_do_parse(w, !read);
    }
    if (w->is_conn_err_recv) {
        return http2_conn_do_error_recv(w);
    }
    if (w->is_shutdown_commanded && w->state != HTTP2_PARSE_SHUTDOWN_SENT) {
        http2_conn_send_shutdown(w, LSTR_EMPTY_V);
    }
    if (w->state == HTTP2_PARSE_SHUTDOWN_SENT) {
        if (ob_is_empty(&w->ob)) {
            shutdown(fd, SHUT_WR);
            http2_conn_do_close(w);
            return 0;
        }
        if (!read) {
            http2_conn_do_close(w);
            return 0;
        }
    } else {
        http2_conn_do_on_streams_can_write(w);
    }
    if (http2_conn_do_write(w, fd) < 0) {
        return http2_conn_do_error_write(w);
    }
    http2_conn_do_set_mask_and_watch(w);
    return 0;
}

static int http2_conn_do_connect_timeout(http2_conn_t *w)
{
    http2_conn_trace(w, 2, "socket connect: timeout");
    http2_conn_do_close(w);
    return 0;
}

static int http2_conn_do_connect_error(http2_conn_t *w)
{
    http2_conn_trace(w, 2, "socket connect: error");
    http2_conn_do_close(w);
    return 0;
}

static int http2_tls_handshake(el_t evh, int fd, short events, data_t priv)
{
    http2_conn_t *w = priv.ptr;
    int res = 0;

    switch (ssl_do_handshake(w->ssl, evh, fd, NULL)) {
    case SSL_HANDSHAKE_SUCCESS:
        if (w->is_client) {
            X509 *cert = SSL_get_peer_certificate(w->ssl);
            if (unlikely(cert == NULL)) {
                res = -1;
                break;
            }
            X509_free(cert);
        }
        /* XXX: write the connection preamble to w->ob */
        http2_conn_do_parse(w, false);
        el_fd_set_mask(evh, POLLINOUT);
        el_fd_set_hook(evh, http2_conn_on_event);
        break;
    case SSL_HANDSHAKE_PENDING:
        break;
    case SSL_HANDSHAKE_CLOSED:
    case SSL_HANDSHAKE_ERROR:
        res = -1;
    }
    if (res < 0) {
        http2_conn_do_connect_error(w);
    }
    return res;
}

static int http2_on_connect(el_t evh, int fd, short events, data_t priv)
{
    http2_conn_t *w = priv.ptr;
    int res;

    assert(w->is_client);
    if (events == EL_EVENTS_NOACT) {
        http2_conn_do_connect_timeout(w);
        return -1;
    }
    res = socket_connect_status(fd);
    if (res > 0) {
        if (w->ssl) {
            SSL_set_fd(w->ssl, fd);
            SSL_set_connect_state(w->ssl);
            el_fd_set_hook(evh, &http2_tls_handshake);
            el_fd_set_mask(evh, POLLINOUT);
            return res;
        }
        /* XXX: write the connection preamble to w->ob */
        http2_conn_do_parse(w, false);
        el_fd_set_hook(evh, http2_conn_on_event);
        el_fd_set_mask(w->ev, POLLINOUT);
        el_fd_watch_activity(w->ev, POLLINOUT, 0);
    } else if (res < 0) {
        http2_conn_do_connect_error(w);
    }
    return res;
}

/* }}} */
/* {{{ HTTP2 Server Adaptation */

typedef struct http2_server_t {
    http2_conn_t    * conn;
    httpd_cfg_t     * httpd_cfg;
    dlist_t         active_httpds;
    dlist_t         idle_httpds;
    dlist_t         http2_link;
} http2_server_t;

static http2_server_t *http2_server_init(http2_server_t *w)
{
    p_clear(w, 1);
    dlist_init(&w->active_httpds);
    dlist_init(&w->idle_httpds);
    dlist_init(&w->http2_link);
    return w;
}

static void http2_server_wipe(http2_server_t *w)
{
    httpd_cfg_delete(&w->httpd_cfg);
    dlist_remove(&w->http2_link);
}

GENERIC_NEW(http2_server_t, http2_server);
GENERIC_DELETE(http2_server_t, http2_server);

typedef struct httpd_http2_ctx_t {
    httpd_t *httpd;
    http2_server_t *server;
    dlist_t http2_link;

    /* offset into httpd's ob */
    int http2_sync_mark;
    /* request converted to chunked encoding: payload with no Content-Length
     * header */
    uint32_t http2_chunked : 1;
    uint32_t http2_stream_id : 31;
} httpd_http2_ctx_t;

static httpd_http2_ctx_t *httpd_http2_ctx_init(httpd_http2_ctx_t *ctx)
{
    p_clear(ctx, 1);
    dlist_init(&ctx->http2_link);
    return ctx;
}

static void httpd_http2_ctx_wipe(httpd_http2_ctx_t *ctx)
{
    dlist_remove(&ctx->http2_link);
}

GENERIC_NEW(httpd_http2_ctx_t, httpd_http2_ctx);
GENERIC_DELETE(httpd_http2_ctx_t, httpd_http2_ctx);

static int
httpd_spawn_as_http2(int fd, sockunion_t *peer_su, httpd_cfg_t *cfg)
{
    http2_server_t *w;
    http2_conn_t *conn;
    el_fd_f *el_cb;

    el_cb = cfg->ssl_ctx ? &http2_tls_handshake : &http2_conn_on_event;
    conn = http2_conn_new();
    if (cfg->ssl_ctx) {
        conn->ssl = SSL_new(cfg->ssl_ctx);
        SSL_set_fd(conn->ssl, fd);
        SSL_set_accept_state(conn->ssl);
   }
    conn->settings = http2_default_settings_g;
    cfg->nb_conns++;
    fd_set_features(fd, FD_FEAT_TCP_NODELAY);
    conn->ev = el_fd_register(fd, true, POLLIN, el_cb, conn);
    el_fd_watch_activity(conn->ev, POLLIN, cfg->noact_delay);
    w = http2_server_new();
    w->conn = conn;
    w->httpd_cfg = httpd_cfg_retain(cfg);
    dlist_add_tail(&cfg->http2_httpd_list, &w->http2_link);
    conn->server_ctx = w;
    return 0;
}

static httpd_t *
httpd_spawn_as_http2_stream(http2_server_t *server, uint32_t stream_id)
{
    httpd_t *w;
    httpd_cfg_t *cfg = server->httpd_cfg;
    httpd_http2_ctx_t *http2_ctx;

    w = obj_new_of_class(httpd, cfg->httpd_cls);
    w->cfg = httpd_cfg_retain(cfg);
    w->max_queries = 1;
    dlist_init(&w->httpd_link);
    w->http2_ctx = http2_ctx = httpd_http2_ctx_new();
    http2_ctx->httpd = w;
    http2_ctx->server = server;
    http2_ctx->http2_stream_id = stream_id;
    dlist_add_tail(&server->idle_httpds, &http2_ctx->http2_link);
    return w;
}

/** Streaming Layer Handlers */

static void http2_stream_close_httpd(http2_conn_t *w,httpd_t *httpd)
{
    httpd_http2_ctx_delete(&httpd->http2_ctx);
    httpd_do_close(&httpd);
}

static void
http2_stream_on_headers_server(http2_conn_t *w, http2_stream_t stream,
                               httpd_t *httpd, http2_header_info_t *info,
                               pstream_t headerlines, bool eos)
{
    http2_server_t *server = w->server_ctx;
    enum http_parser_state state;
    sb_t *ibuf;
    pstream_t ps;
    int res;

    if (!httpd) {
        httpd = httpd_spawn_as_http2_stream(server, stream.id);
        stream.info.ctx.httpd = httpd;
        http2_stream_do_update_info(w, &stream);
    }
    ibuf = &httpd->ibuf;
    state = httpd->state;
    switch (state) {
    case HTTP_PARSER_IDLE:
        if (!info->method.s) {
            goto malformed_err;
        }
        sb_addf(ibuf, "%*pM %*pM HTTP/1.1\r\n", LSTR_FMT_ARG(info->method),
                LSTR_FMT_ARG(info->path));
        sb_add_ps(ibuf, headerlines);
        switch (http_get_token_ps(ps_initlstr(&info->method))) {
        case HTTP_TK_POST:
        case HTTP_TK_PUT:
            if (!info->content_length.s) {
                sb_add_lstr(ibuf, eos ? LSTR_IMMED_V("Content-Length: 0\r\n")
                                      : LSTR_IMMED_V(
                                          "Transfer-Encoding: chunked\r\n"));
            }
            break;
        default:
            break;
        }
        sb_adds(ibuf, "\r\n");
        ps = ps_initsb(ibuf);
        res = httpd_parse_idle(httpd, &ps);
        if (res != PARSE_OK || !ps_done(&ps)) {
            goto malformed_err;
        }
        sb_skip_upto(&httpd->ibuf, ps.p);
        assert(httpd->state == HTTP_PARSER_BODY
               || httpd->state == HTTP_PARSER_CHUNK_HDR);
        if (eos) {
            res = httpd_parse_body(httpd, &ps);
            if (res != PARSE_OK) {
                goto malformed_err;
            }
        }
        return;
    case HTTP_PARSER_CHUNK_TRAILER:
        assert(0 && "TODO support trailer headers");
        break;
    default:
        break;
    }
malformed_err:
    http2_stream_send_reset(w, &stream,
                            "malformed request [invalid headers]");
    http2_stream_close_httpd(w, httpd);
}

static void
http2_stream_on_data_server(http2_conn_t *w, http2_stream_t stream,
                            httpd_t *httpd, pstream_t data, bool eos)
{
    pstream_t ps;
    int len;
    int res;

    assert(httpd->state == HTTP_PARSER_BODY
               || httpd->state == HTTP_PARSER_CHUNK_HDR);
    if (ps_done(&data) && !eos) {
        return;
    }
    switch(httpd->state) {
    case HTTP_PARSER_BODY:
        sb_add_ps(&httpd->ibuf, data);
        ps = ps_initsb(&httpd->ibuf);
        len = ps_len(&ps);
        if (httpd->chunk_length == len) {
            /* XXX: ensure that the last call to httpd_parse_body happens only
             * when eos arrives, possibly later in a 0-payload DATA frame. */
            if (!eos) {
                if (len <= 1) {
                    return;
                }
                __ps_clip(&ps, --len);
            }
        } else if (httpd->chunk_length < len) {
            /* mismatch: DATA frames > content-length */
            if (!eos) {
                http2_stream_send_reset(
                    w, &stream, "malformed response [DATA > Content-Length]");
            }
            http2_stream_close_httpd(w, httpd);
            return;
        }
        res = httpd_parse_body(httpd, &ps);
        switch (res) {
        case PARSE_MISSING_DATA:
            assert(httpd->state == HTTP_PARSER_BODY);
            sb_skip_upto(&httpd->ibuf, ps.p);
            if (eos) {
                /* mismatch: content-length > DATA frames.*/
                http2_stream_trace(w, &stream, 2,
                                   "malformed response [unexpected eos]");
                http2_stream_close_httpd(w, httpd);
                return;
            }
            break;
        case PARSE_OK:
            assert(httpd->state == HTTP_PARSER_CLOSE);
            assert(ps_done(&ps));
            assert(eos);
            sb_skip_upto(&httpd->ibuf, ps.p);
            return;
        case PARSE_ERROR:
            if (!eos) {
                http2_stream_send_reset(w, &stream,
                                        "malformed response [invalid payload "
                                        "format or compression]");
            }
            http2_stream_close_httpd(w, httpd);
            return;
        default:
            assert(0 && "unexpected result from httpd_parse_body");
        }
        break;
    case HTTP_PARSER_CHUNK_HDR:
        res = PARSE_OK;
        if (!ps_done(&data)) {
            char hdr[12];

            http_chunk_patch(NULL, hdr, ps_len(&data));
            sb_add(&httpd->ibuf, hdr + 2, 10);
            sb_add_ps(&httpd->ibuf, data);
            sb_adds(&httpd->ibuf, "\r\n");
            ps = ps_initsb(&httpd->ibuf);
            len = ps_len(&ps);
            res = httpd_parse_chunk_hdr(httpd, &ps);
            if (res == PARSE_OK) {
                res = httpd_parse_chunk(httpd, &ps);
            }
        }
        if (eos && res == PARSE_OK) {
            sb_adds(&httpd->ibuf, "0\r\n\r\n");
            ps = ps_initsb(&httpd->ibuf);
            len = ps_len(&ps);
            res = httpd_parse_chunk_hdr(httpd, &ps);
            if (res == PARSE_OK) {
                res = httpd_parse_chunk(httpd, &ps);
            }
        }
        break;
    default:
        assert(0 && "invalid parser state");
    }
}

static void
http2_stream_on_reset_server(http2_conn_t *w, http2_stream_t stream,
                             httpd_t *httpd, bool remote)
{
    http2_stream_close_httpd(w, httpd);
}

/** Extract code/headerline from an upstream server (httpd) response in \p
 * chunk.
 *
 * Note: non-defensive parsing due to hypotheses about the way our HTTP/1.x
 * code works (see above). These hypotheses are guarded by assertions for now.
 *
 * FIXME: add unit tests to verify our hypotheses OR use defensive parsing.
 */
static void http_get_http2_response_hdrs(pstream_t *chunk, lstr_t *code,
                                         pstream_t *headerlines)
{
    byte *p;
    pstream_t line;
    pstream_t control;

    p = memmem(chunk->p, ps_len(chunk), "\r\n\r\n", 4);
    assert(p);
    control = __ps_get_ps_upto(chunk, p + 2);
    __ps_skip(chunk, 2);
    p = memmem(control.p, ps_len(&control), "\r\n", 2);
    assert(p);
    line = __ps_get_ps_upto(&control, p);
    __ps_skip(&control, 2);
    __ps_skip(&line, sizeof("HTTP/1.x ") - 1);
    *code = LSTR_INIT_V(line.s, 3);
    __ps_skip(&line, 3);
    *headerlines = control;
}

static void
http2_conn_check_idle_httpd_invariants(http2_conn_t *w, httpd_t *httpd)
{
    assert(httpd->http2_ctx->http2_stream_id);
    /* don't support chunked httpd ob (yet) */
    assert(htlist_is_empty(&httpd->ob.chunks_list));
    /* one unique (non-answered) query or none if already answered. */
    assert(dlist_is_empty_or_singular(&httpd->query_list));
    if (ob_is_empty(&httpd->ob)) {
        /* no response was written yet */
        httpd_query_t *q __unused__;

        assert(dlist_is_singular(&httpd->query_list));
        q = dlist_first_entry(&httpd->query_list, httpd_query_t, query_link);
        assert(!q->parsed && !q->answered && !q->hdrs_done);
    }
}

/* Stream the response of idle httpd (headers are not sent yet) */
static void http2_conn_stream_idle_httpd(http2_conn_t *w, httpd_t *httpd)
{
    http2_server_t *ctx = w->server_ctx;
    http2_stream_t stream;
    pstream_t chunk;
    lstr_t code;
    pstream_t headerlines;
    int clen;
    httpd_http2_ctx_t *http2_ctx = httpd->http2_ctx;

    http2_conn_check_idle_httpd_invariants(w, httpd);

    if (ob_is_empty(&httpd->ob)) {
        /* httpd ob is empty: the current query is not answered yet. */
        return;
    }

    stream = http2_stream_get(w, http2_ctx->http2_stream_id);
    chunk = ps_initsb(&httpd->ob.sb);
    http_get_http2_response_hdrs(&chunk, &code, &headerlines);
    http2_stream_send_response_headers(w, &stream, code, headerlines,
                                       http2_ctx, &clen);
    /* TODO: support 1xx informational responses (100-continue) */
    assert(clen >= 0 && "TODO: support chunked respones");
    http2_ctx->http2_sync_mark = clen;
    OB_WRAP(sb_skip_upto, &httpd->ob, chunk.p);
    if (!clen) {
        /* headers-only response (no-payload). */
        assert(ob_is_empty(&httpd->ob));
        assert(stream.info.flags & STREAM_FLAG_EOS_SENT);
        http2_stream_close_httpd(w, httpd);
        return;
    }
    /* httpd becomes active: payload streaming phase (DATA). */
    dlist_move_tail(&ctx->active_httpds, &http2_ctx->http2_link);
}

static void
http2_conn_check_active_httpd_invariants(http2_conn_t *w, httpd_t *httpd)
{
    assert(httpd->http2_ctx->http2_stream_id);
    /* We don't support chunked httpd ob (yet) */
    assert(htlist_is_empty(&httpd->ob.chunks_list));
    /* We don't support chunked upstream responses yet */
    assert(httpd->http2_ctx->http2_sync_mark == httpd->ob.length);
}

/** Stream the response of active httpd (payload sending).
 * \param max_sz: max size of data to send in this sending opportunity.
 */
static void
http2_conn_stream_active_httpd(http2_conn_t *w, httpd_t *httpd, int max_sz)
{
    httpd_http2_ctx_t *http2_ctx = httpd->http2_ctx;
    uint32_t stream_id = http2_ctx->http2_stream_id;
    http2_stream_t stream;
    pstream_t chunk;
    int len;
    bool eos;

    /* Calling code: max_sz must not exceed connection send window. */ 
    assert(max_sz <= w->send_window);
    http2_conn_check_active_httpd_invariants(w, httpd);

    stream = http2_stream_get(w, stream_id);
    len = MIN3(http2_ctx->http2_sync_mark, stream.info.send_window, max_sz);
    if (len <= 0) {
        return;
    }
    chunk = ps_initsb(&httpd->ob.sb);
    __ps_clip(&chunk, len);
    http2_ctx->http2_sync_mark -= len;
    eos = http2_ctx->http2_sync_mark == 0;
    http2_stream_send_data(w, &stream, chunk, eos);
    OB_WRAP(sb_skip, &httpd->ob, len);
    if (eos) {
        /* No more data to send and stream was ended from our side. */
        assert(ob_is_empty(&httpd->ob));
        assert(stream.info.flags & STREAM_FLAG_EOS_SENT);
        if (stream.info.flags & STREAM_FLAG_EOS_RECV) {
            http2_stream_close_httpd(w, httpd);
        } else {
            /* Early response case: (usually an error response) */
        }
    }
}

static void http2_conn_on_streams_can_write_server(http2_conn_t *w)
{
    http2_server_t *ctx = w->server_ctx;
    dlist_t *httpds;
    bool can_progress;

    httpds = &ctx->idle_httpds;
    dlist_for_each_entry(httpd_http2_ctx_t, httpd, httpds, http2_link) {
        http2_conn_stream_idle_httpd(w, httpd->httpd);
    }
    httpds = &ctx->active_httpds;
    do {
#define OB_SEND_ALLOC   (8 << 10)
#define OB_HIGH_MARK    (1 << 20)
        /* A simple DATA send "scheduling" algorithm for active streams as we
         * don't have a sophisticated frame-aware scheduler:
         *  - To be fair, we allow each stream to send (i.e., output) up to
         *    OB_SEND_ALLOC per each opportunity.
         *  - We iterate over streams and continue this as long as one of
         *    them can progress.
         *  - However, we stop this once we have exceeded the OB_HIGH_MARK in
         *    the conn buffer.
         *  - This done because we don't want to delay too much the writing of
         *    generated responses to the underlying socket (e.g., acks to
         *    PING or SETTINGS in subsequent event callbacks to
         *    http2_conn_on_event().
         */
        can_progress = false;

        dlist_for_each_entry(httpd_http2_ctx_t, httpd, httpds, http2_link) {
            int ob_len = w->ob.length;
            int len;

            if (ob_len >= OB_HIGH_MARK || w->send_window <= 0) {
                can_progress = false;
                break;
            }
            len = MIN(w->send_window, OB_SEND_ALLOC);
            http2_conn_stream_active_httpd(w, httpd->httpd, len);
            if (w->ob.length - ob_len >= len) {
                can_progress = true;
            }
        }
#undef OB_HIGH_MARK
#undef OB_SEND_ALLOC
    } while (can_progress);
}

static void http2_conn_on_close_server(http2_conn_t *w)
{
    http2_server_delete(&w->server_ctx);
}

/* }}} */
/* {{{ HTTP2 Client Adapation */

typedef enum http2_ctx_active_substate_t {
    HTTP2_CTX_STATE_WAITING,
    HTTP2_CTX_STATE_PARSING,
    HTTP2_CTX_STATE_RESETTING,
} http2_ctx_active_substate_t;

typedef struct httpc_http2_ctx_t {
    httpc_t       *httpc;
    http2_conn_t  *conn;
    dlist_t       http2_link;
    uint32_t      http2_chunked: 1;
    uint32_t      http2_stream_id: 31;
    int           http2_sync_mark;
    uint8_t       substate;
    bool          disconnect_cmd;
} httpc_http2_ctx_t;

static httpc_http2_ctx_t *httpc_http2_ctx_init(httpc_http2_ctx_t *ctx)
{
    p_clear(ctx, 1);
    dlist_init(&ctx->http2_link);
    return ctx;
}

static void httpc_http2_ctx_wipe(httpc_http2_ctx_t *ctx)
{
    dlist_remove(&ctx->http2_link);
}

GENERIC_NEW(httpc_http2_ctx_t, httpc_http2_ctx);
GENERIC_DELETE(httpc_http2_ctx_t, httpc_http2_ctx);

static uint32_t peer_hash(const qhash_t *qh, const sockunion_t *su)
{
    return sockunion_hash(su);
}

static bool
peer_equals(const qhash_t *qh, const sockunion_t *su1, const sockunion_t *su2)
{
    return sockunion_equal(su1, su2);
}

qm_kvec_t(qhttp2_clients, sockunion_t, http2_client_t *, peer_hash,
          peer_equals);

typedef struct http2_pool_t {
    qm_t(qhttp2_clients) qclients;
} http2_pool_t;

typedef struct http2_client_t {
    int refcnt;
    http2_conn_t *conn;
    http2_pool_t *pool;
    sockunion_t peer_su;
    dlist_t active_httpcs;
    dlist_t idle_httpcs;
} http2_client_t;

/* {{{ http2_pool_t new/init/wipe/delete */

static http2_pool_t *http2_pool_init(http2_pool_t *pool)
{
    p_clear(pool, 1);
    qm_init(qhttp2_clients, &pool->qclients);
    return pool;
}

static void http2_pool_remove_client(http2_client_t *client)
{
    http2_pool_t *pool = client->pool;

    qm_del_key(qhttp2_clients, &pool->qclients, &client->peer_su);
    client->pool = NULL;
}

static void http2_pool_wipe(http2_pool_t *pool)
{
    qm_for_each_value(qhttp2_clients, client, &pool->qclients) {
        if (client->pool) {
            http2_pool_remove_client(client);
        }
    }
    qm_wipe(qhttp2_clients, &pool->qclients);
}

GENERIC_NEW(http2_pool_t, http2_pool);
GENERIC_DELETE(http2_pool_t, http2_pool);

/* }}} */
/* {{{ http2_client_t new/init/wipe/delete */

static http2_client_t *http2_client_init(http2_client_t *ctx)
{
    p_clear(ctx, 1);
    dlist_init(&ctx->active_httpcs);
    dlist_init(&ctx->idle_httpcs);
    return ctx;
}

static void http2_client_wipe(http2_client_t *ctx)
{
    assert(dlist_is_empty(&ctx->active_httpcs));
    assert(dlist_is_empty(&ctx->idle_httpcs));

    if (ctx->pool) {
        http2_pool_remove_client(ctx);
    }
}

DO_REFCNT(http2_client_t, http2_client);

/* }}} */

static http2_pool_t *http2_pool_get(httpc_cfg_t *cfg)
{
    if (!cfg->http2_pool) {
        cfg->http2_pool = http2_pool_new();
    }
    return cfg->http2_pool;
}

static http2_conn_t *
http2_conn_connect_client_as(const sockunion_t *su, SSL_CTX *nullable ssl_ctx)
{
    http2_conn_t *w;
    int flags = O_NONBLOCK | FD_FEAT_TCP_NODELAY;
    int fd;

    fd = RETHROW_NP(
        connectx_as(-1, su, 1, NULL, SOCK_STREAM, IPPROTO_TCP, flags, 0));
    w = http2_conn_new();
    if (ssl_ctx) {
        w->ssl = SSL_new(ssl_ctx);
    }
    w->is_client = true;
    w->settings = http2_default_settings_g;
    w->ev = el_fd_register(fd, true, POLLOUT, &http2_on_connect, w);
    el_fd_watch_activity(w->ev, POLLINOUT, 10000);
    return w;
}

static http2_client_t *
http2_pool_get_client(httpc_cfg_t *cfg, const sockunion_t *peer_su)
{
    http2_client_t *client;
    http2_pool_t *pool = http2_pool_get(cfg);
    http2_conn_t *w;
    unsigned pos;

    pos = qm_reserve(qhttp2_clients, &pool->qclients, peer_su, 0);
    if (pos & QHASH_COLLISION) {
        /* We already have a client for this address. */
        pos &= ~QHASH_COLLISION;
        return pool->qclients.values[pos];
    }

    w = http2_conn_connect_client_as(peer_su, cfg->ssl_ctx);
    if (unlikely(!w)) {
        qm_del_at(qhttp2_clients, &pool->qclients, pos);
        return NULL;
    }

    client = http2_client_new();
    client->pool = pool;
    client->peer_su = *peer_su;
    client->conn = w;
    w->client_ctx = client;

    pool->qclients.values[pos] = client;

    return client;
}

static httpc_t *httpc_connect_as_http2(const sockunion_t *su,
                                       const sockunion_t *nullable su_src,
                                       httpc_cfg_t *cfg, httpc_pool_t *pool)
{
    httpc_t *w;
    http2_client_t *client;

    client = RETHROW_P(http2_pool_get_client(cfg, su));

    w = obj_new_of_class(httpc, cfg->httpc_cls);
    w->http2_ctx = httpc_http2_ctx_new();
    w->http2_ctx->httpc = w;
    w->http2_ctx->conn = client->conn;
    dlist_add_tail(&client->idle_httpcs, &w->http2_ctx->http2_link);
    w->connected_as_http2 = true;
    w->cfg = httpc_cfg_retain(cfg);
    w->max_queries = cfg->max_queries;
    w->busy = true;
    if (pool) {
        httpc_pool_attach(w, pool);
    }
    obj_vcall(w, set_ready, true);
    return w;
}

/** Streaming Layer Handlers */

/** Extract headerlines and special values from a downstream client (httpc)
 * request in \p chunk.
 *
 * Note: non-defensive parsing due to hypotheses about the way our HTTP/1.x
 * code works (see above). These hypotheses are guarded by assertions for now.
 *
 * FIXME: add unit tests to verify our hypotheses OR use defensive parsing.
 */
static void
http_get_http2_request_hdrs(pstream_t *chunk, lstr_t *method, lstr_t *scheme,
                            lstr_t *path, lstr_t *authority,
                            pstream_t *headerlines)
{
    byte *p;
    pstream_t line;
    pstream_t control;
    pstream_t ps;

    p = memmem(chunk->p, ps_len(chunk), "\r\n\r\n", 4);
    assert(p);
    control = __ps_get_ps_upto(chunk, p + 2);
    __ps_skip(chunk, 2);
    p = memmem(control.p, ps_len(&control), "\r\n", 2);
    assert(p);
    line = __ps_get_ps_upto(&control, p);
    __ps_skip(&control, 2);
    __ps_shrink(&line, sizeof(" HTTP/1.1") - 1);
    p = memchr(line.p, ' ', ps_len(&line));
    assert(p);
    ps = __ps_get_ps_upto(&line, p);
    __ps_skip(&line, 1);
    *method = LSTR_PS_V(&ps);
    if (line.b[0] == '/' || line.b[0] == '*') {
        *scheme = LSTR_NULL_V;
        *authority = LSTR_NULL_V;
    } else {
        assert(line.b[0]  == 'h');
        if (line.b[4] == ':') {
            *scheme = LSTR_INIT_V(line.s, 4); /* http */
            __ps_skip(&line, sizeof("http://") - 1);
        } else {
            assert(line.b[5] == ':');
            *scheme = LSTR_INIT_V(line.s, 5); /* https */
            __ps_skip(&line, sizeof("https://") - 1);
        }
        p = memchr(line.p, '/', ps_len(&line));
        assert(p);
        ps = __ps_get_ps_upto(&line, p);
        *authority = LSTR_PS_V(&ps);
    }
    *path = LSTR_PS_V(&line);
    if (!authority->len) {
        /* Get the host line "Host: the-host-value\r\n" */
        p = memmem(control.p, ps_len(&control), "\r\n", 2);
        line = __ps_get_ps_upto(&control, p + 2);
        __ps_skip(&line, strlen("Host: "));
        __ps_shrink(&line, 2);
        *authority = LSTR_PS_V(&line);
    }
    *headerlines = control;
}

static void
http2_conn_check_attachable_httpc_invariants(http2_conn_t *w, httpc_t *httpc)
{
    httpc_query_t *q __unused__;

    /* At least one query is attached, */
    assert(!dlist_is_empty(&httpc->query_list));
    /* for which, at least, the request headers are written to the buffer, */
    q = dlist_first_entry(&httpc->query_list, httpc_query_t, query_link);
    assert(q->hdrs_done);
    /* so, the output buffer is not empty, */
    assert(!ob_is_empty(&httpc->ob));
    /* however, not yet streamed to HTTP/2, so, no response is received
     * (parsed) yet. */
    assert(httpc->state == HTTP_PARSER_IDLE);
}

/* Attach an idle httpc (headers are not sent yet) to a HTTP/2 stream */
static void http2_stream_attach_httpc(http2_conn_t *w, httpc_t *httpc)
{
    http2_client_t *ctx = w->client_ctx;
    httpc_http2_ctx_t *http2_ctx = httpc->http2_ctx;
    http2_stream_t stream;
    pstream_t chunk;
    lstr_t method;
    lstr_t scheme;
    lstr_t authority;
    lstr_t path;
    pstream_t headerlines;
    int clen;

    http2_conn_check_attachable_httpc_invariants(w, httpc);

    http2_ctx->http2_stream_id = http2_stream_get_idle(w);
    stream = http2_stream_get(w, http2_ctx->http2_stream_id);
    chunk = ps_initsb(&httpc->ob.sb);
    http_get_http2_request_hdrs(&chunk, &method, &scheme, &path, &authority,
                                &headerlines);
    if (!scheme.len) {
        scheme = w->ssl ? LSTR_IMMED_V("https") : LSTR_IMMED_V("http");
    }
    http2_stream_send_request_headers(w, &stream, method, scheme, path,
                                      authority, headerlines,
                                      httpc->http2_ctx, &clen);
    assert(clen >= 0 && "TODO: support chunked requests");
    http2_ctx->http2_sync_mark = clen;
    OB_WRAP(sb_skip_upto, &httpc->ob, chunk.p);
    /* httpc becomes active: payload streaming phase (DATA). */
    dlist_move_tail(&ctx->active_httpcs, &http2_ctx->http2_link);
}

static void http2_conn_stream_idle_httpc(http2_conn_t *w, httpc_t *httpc)
{
    httpc_query_t *q;

    if (dlist_is_empty(&httpc->query_list)) {
        if (httpc->connection_close)  {
            httpc_http2_ctx_delete(&httpc->http2_ctx);
            obj_delete(&httpc);
        }
        return;
    }
    q = dlist_first_entry(&httpc->query_list, httpc_query_t, query_link);
    if (!q->hdrs_done) {
        return;
    }
    http2_stream_attach_httpc(w, httpc);
}

static void
http2_conn_stream_active_httpc(http2_conn_t *w, httpc_t *httpc, int max_sz)
{
    httpc_http2_ctx_t *http2_ctx = httpc->http2_ctx;
    uint32_t stream_id = http2_ctx->http2_stream_id;
    http2_stream_t stream;
    pstream_t chunk;
    int len;
    bool eos;

    assert(stream_id);
    if (http2_ctx->http2_sync_mark == 0) {
        return;
    }
    assert(http2_ctx->http2_sync_mark <= w->ob.length);
    stream = http2_stream_get(w, stream_id);
    if (stream.info.flags & (STREAM_FLAG_CLOSED | STREAM_FLAG_RST_SENT)) {
        /* XXX: stream was already reset or closed and we still have some
         * payload to remove from the httpc output buffer. */
        assert(0 && "TODO");
    }
    assert(max_sz <= w->send_window);
    len = MIN3(http2_ctx->http2_sync_mark, stream.info.send_window, max_sz);
    if (len <= 0) {
        return;
    }
    assert(htlist_is_empty(&httpc->ob.chunks_list)
           && "TODO: support chunked requests");
    chunk = ps_initsb(&httpc->ob.sb);
    __ps_clip(&chunk, len);
    http2_ctx->http2_sync_mark -= len;
    eos = http2_ctx->http2_sync_mark == 0;
    http2_stream_send_data(w, &stream, chunk, eos);
    OB_WRAP(sb_skip, &httpc->ob, len);
}

static void
http2_stream_reset_httpc_ob(http2_conn_t *w, httpc_t *httpc)
{
    httpc_http2_ctx_t *http2_ctx = httpc->http2_ctx;

    assert(!http2_ctx->http2_chunked && "TODO: support chunked requests");
    assert(htlist_is_empty(&httpc->ob.chunks_list));
    OB_WRAP(sb_skip, &httpc->ob, http2_ctx->http2_sync_mark);
}

/** Reset a steam-attached (active) httpc to the idle state to serve the next
 * query if any. */
static void
http2_stream_reset_httpc(http2_conn_t *w, httpc_t *httpc, bool query_error)
{
    http2_client_t *ctx = w->client_ctx;
    httpc_http2_ctx_t *http2_ctx = httpc->http2_ctx;

    assert(http2_ctx->http2_stream_id);
    if (query_error) {
        httpc_query_t *q;

        http2_stream_reset_httpc_ob(w, httpc);
        q = dlist_first_entry(&httpc->query_list, httpc_query_t, query_link);
        httpc_query_on_done(q, HTTPC_STATUS_INVALID);
        httpc->chunk_length = 0;
        httpc->state = HTTP_PARSER_IDLE;
    } else {
        assert(httpc->state == HTTP_PARSER_IDLE);
        assert(!http2_ctx->http2_sync_mark);
    }
    http2_ctx->http2_stream_id = 0;
    sb_reset(&httpc->ibuf);
    dlist_move_tail(&ctx->idle_httpcs, &http2_ctx->http2_link);
    if (httpc->connection_close)  {
        httpc_http2_ctx_delete(&httpc->http2_ctx);
        obj_delete(&httpc);
    } else if (httpc->http2_ctx->disconnect_cmd) {
        httpc_http2_ctx_delete(&httpc->http2_ctx);
    }
    return;
}

static void
http2_stream_on_headers_client(http2_conn_t *w, http2_stream_t stream,
                               httpc_http2_ctx_t *httpc_ctx,
                               http2_header_info_t *info,
                               pstream_t headerlines, bool eos)
{
    httpc_t *httpc = httpc_ctx->httpc;
    enum http_parser_state state = httpc->state;
    pstream_t ps;

    httpc_ctx->substate = HTTP2_CTX_STATE_PARSING;

    if (state == HTTP_PARSER_BODY) {
        /* XXX: we don't expect trailer headers since we don't ask for them so
         * we don't validate if they are really trailer headers or if they end
         * the stream, IoW, receiving headers here is an error anyway. */
        http2_stream_send_reset(w, &stream,
                                "malformed response [headers while "
                                "expecting body]");
        http2_stream_reset_httpc(w, httpc, true);
        return;
    }

    /* TODO: dependency on the above HTTP/1.x code: add a test or convert it
     * to an expect. */
    assert(state == HTTP_PARSER_IDLE &&
           "unexpected http2-forwarded httpc state");

    sb_addf(&httpc->ibuf, "HTTP/1.1 %pL Nothing But Code\r\n",
            &info->status);
    sb_add_ps(&httpc->ibuf, headerlines);
    sb_add(&httpc->ibuf, "\r\n", 2);
    ps = ps_initsb(&httpc->ibuf);
    if (httpc_parse_idle(httpc, &ps) != PARSE_OK ||
        httpc->state == HTTP_PARSER_CHUNK_HDR)
    {
        if (eos) {
            http2_stream_trace(w, &stream, 2,
                               "malformed response [invalid headers]");
        } else {
            http2_stream_send_reset(
                w, &stream, "malformed response [invalid headers]");
        }
        http2_stream_reset_httpc(w, httpc, true);
        return;
    }
    sb_skip_upto(&httpc->ibuf, ps.p);
    assert(ps_done(&ps));
    assert(httpc->state == HTTP_PARSER_IDLE ||
           httpc->state == HTTP_PARSER_BODY);
    if (eos) {
        bool query_error = false;

        if (httpc->state == HTTP_PARSER_IDLE) {
            http2_stream_trace(
                w, &stream, 2,
                "malformed response [1xx headers with eos]");
            query_error = true;
        } else {
            assert(httpc->state == HTTP_PARSER_BODY);
            if (httpc_parse_body(httpc, &ps) != PARSE_OK) {
                query_error = true;
                http2_stream_trace(w, &stream, 2,
                                   "malformed response [no-content]");
            }
        }
        http2_stream_reset_httpc(w, httpc, query_error);
        return;
    }

    if (httpc->http2_ctx->disconnect_cmd) {
        http2_stream_send_reset_cancel(w, &stream, "client disconnect");
        http2_stream_reset_httpc(w, httpc, true);
        return;
    }
    httpc_ctx->substate = HTTP2_CTX_STATE_WAITING;
}

static void
http2_stream_on_data_client(http2_conn_t *w, http2_stream_t stream,
                            httpc_http2_ctx_t *httpc_ctx, pstream_t data,
                            bool eos)
{
    httpc_t *httpc = httpc_ctx->httpc;
    pstream_t ps;
    int len;
    int res;

    assert(httpc->state == HTTP_PARSER_BODY);
    if (ps_done(&data) && !eos) {
        return;
    }
    sb_add_ps(&httpc->ibuf, data);
    ps = ps_initsb(&httpc->ibuf);
    len = ps_len(&ps);
    if (httpc->chunk_length < 0) {
        /* no Content-Length: responses */
        if (eos) {
            httpc->chunk_length = len;
        }
    } else {
        if (httpc->chunk_length == len) {
            /* XXX: ensure that the last call to httpc_parse_body happens only
             * when eos arrives, possibly later in a 0-payload DATA frame. */
            if (!eos) {
                if (len <= 1) {
                    return;
                }
                __ps_clip(&ps, --len);
            }
        } else if (httpc->chunk_length < len) {
            /* mismatch: DATA frames > content-length */
            if (!eos) {
                http2_stream_send_reset(
                    w, &stream, "malformed response [DATA > Content-Length]");
            }
            http2_stream_reset_httpc(w, httpc, true);
            return;
        }
    }
    httpc_ctx->substate = HTTP2_CTX_STATE_PARSING;
    res = httpc_parse_body(httpc, &ps);
    switch (res) {
    case PARSE_MISSING_DATA:
        assert(httpc->state == HTTP_PARSER_BODY);
        sb_skip_upto(&httpc->ibuf, ps.p);
        if (eos) {
            /* mismatch: content-length > DATA frames.*/
            http2_stream_trace(w, &stream, 2,
                               "malformed response [unexpected eos]");
            http2_stream_reset_httpc(w, httpc, false);
            return;
        }
        break;
    case PARSE_OK:
        assert(httpc->state == HTTP_PARSER_IDLE);
        assert(ps_done(&ps));
        assert(eos);
        sb_skip_upto(&httpc->ibuf, ps.p);
        http2_stream_reset_httpc(w, httpc, false);
        return;
    case PARSE_ERROR:
    case HTTPC_STATUS_TOOLARGE:
        if (!eos) {
            http2_stream_send_reset(
                w, &stream,
                "malformed response [invalid payload format or compression]");
        }
        http2_stream_reset_httpc(w, httpc, true);
        return;
    default:
        assert(0 && "unexpected result from httpc_parse_body");
    }
    if (httpc->http2_ctx->disconnect_cmd) {
        http2_stream_send_reset_cancel(w, &stream, "client disconnect");
        http2_stream_reset_httpc(w, httpc, true);
        return;
    }
    httpc_ctx->substate = HTTP2_CTX_STATE_WAITING;
}

static void
http2_stream_on_reset_client(http2_conn_t *w, http2_stream_t stream,
                             httpc_http2_ctx_t *httpc_ctx, bool remote)
{
    httpc_t *httpc = httpc_ctx->httpc;
    http2_stream_reset_httpc(w, httpc, true);
}

static void http2_conn_on_streams_can_write_client(http2_conn_t *w)
{
    http2_client_t *ctx = w->client_ctx;
    dlist_t *httpcs;
    bool can_progress;

    httpcs = &ctx->idle_httpcs;
    dlist_for_each_entry(httpc_http2_ctx_t, httpc, httpcs, http2_link) {
        http2_conn_stream_idle_httpc(w, httpc->httpc);
    }
    httpcs = &ctx->active_httpcs;
    do {
#define OB_SEND_ALLOC   (8 << 10)
#define OB_HIGH_MARK    (1 << 20)
        /* XXX: see http2_conn_on_streams_can_write_server() */
        can_progress = false;

        dlist_for_each_entry(httpc_http2_ctx_t, httpc, httpcs, http2_link) {
            int ob_len = w->ob.length;
            int len;

            if (ob_len >= OB_HIGH_MARK || w->send_window <= 0) {
                can_progress = false;
                break;
            }
            len = MIN(w->send_window, OB_SEND_ALLOC);
            http2_conn_stream_active_httpc(w, httpc->httpc, len);
            if (w->ob.length - ob_len >= len) {
                can_progress = true;
            }
        }
#undef OB_HIGH_MARK
#undef OB_SEND_ALLOC
    } while (can_progress);
}

static void httpc_disconnect_as_http2(httpc_t *httpc)
{
    httpc_http2_ctx_t *http2_ctx = httpc->http2_ctx;
    http2_conn_t *w = http2_ctx->conn;

    if (http2_ctx->http2_stream_id) {
        http2_stream_t stream =
            http2_stream_get(w, http2_ctx->http2_stream_id);

        http2_ctx->disconnect_cmd = true;
        if (http2_ctx->substate == HTTP2_CTX_STATE_WAITING) {
            http2_stream_send_reset_cancel(w, &stream, "client disconnect");
            http2_stream_reset_httpc(w, httpc, true);
        }
        return;
    }
    httpc_http2_ctx_delete(&httpc->http2_ctx);
}

static void http2_conn_close_httpcs(http2_client_t *ctx)
{
    dlist_t *httpcs;

    assert(dlist_is_empty(&ctx->active_httpcs));
    httpcs = &ctx->idle_httpcs;
    dlist_for_each_entry(httpc_http2_ctx_t, httpc, httpcs, http2_link) {
        httpc_t *w1 = httpc->httpc;

        obj_vcall(w1, disconnect);
        obj_delete(&w1);
    }
}

static void http2_conn_on_close_client(http2_conn_t *w)
{
    if (w->client_ctx) {
        http2_conn_close_httpcs(w->client_ctx);
        http2_client_delete(&w->client_ctx);
    }
}

void httpc_close_http2_pool(httpc_cfg_t *cfg)
{
    if (!cfg->http2_pool) {
        return;
    }
    qm_for_each_value(qhttp2_clients, client, &cfg->http2_pool->qclients) {
        client->pool = NULL;
        client->conn->is_shutdown_commanded = true;
        http2_conn_do_set_mask_and_watch(client->conn);
    }
    http2_pool_delete(&cfg->http2_pool);
}

/* }}} */
/* }}} */
/* {{{ HTTP Module */

static int http_initialize(void *arg)
{
    return 0;
}

static int http_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(http)
    MODULE_DEPENDS_ON(ssl);
MODULE_END()

/* }}} */
/* Tests {{{ */

#include <lib-common/z.h>

static bool has_reply_g;
static http_code_t code_g;
static sb_t body_g;
static httpc_query_t zquery_g;
static el_t zel_server_g;
static el_t zel_client_g;
static httpc_cfg_t zcfg_g;
static httpc_status_t zstatus_g;
static httpc_t *zhttpc_g;
static sb_t zquery_sb_g;

static int z_reply_100(el_t el, int fd, short mask, data_t data)
{
    SB_1k(buf);

    if (sb_read(&buf, fd, 1000) > 0) {
        char reply[] = "HTTP/1.1 100 Continue\r\n\r\n"
                       "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n"
                       "Coucou";

        IGNORE(xwrite(fd, reply, sizeof(reply) - 1));
    }
    return 0;
}

static int z_reply_keep(el_t el, int fd, short mask, data_t data)
{
    sb_reset(&zquery_sb_g);
    if (sb_read(&zquery_sb_g, fd, BUFSIZ) > 0) {
        char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n"
                       "Coucou";

        IGNORE(xwrite(fd, reply, sizeof(reply) - 1));
    }
    return 0;
}

static int z_reply_gzip_empty(el_t el, int fd, short mask, data_t data)
{
    SB_1k(buf);

    if (sb_read(&buf, fd, 1000) > 0) {
        char reply[] = "HTTP/1.1 202 Accepted\r\n"
                       "Content-Encoding: gzip\r\n"
                       "Content-Length: 0\r\n"
                       "\r\n";

        IGNORE(xwrite(fd, reply, sizeof(reply) - 1));
    }
    return 0;
}

static int z_reply_close_without_content_length(el_t el, int fd, short mask,
                                                data_t data)
{
    SB_1k(buf);

    if (sb_read(&buf, fd, 1000) > 0) {
        char reply[] = "HTTP/1.1 200 OK\r\n\r\n"
                       "Plop";
        char s[8192];

        IGNORE(xwrite(fd, reply, sizeof(reply) - 1));
        fd_set_features(fd, O_NONBLOCK);
        for (int i = 0; i < 4096; i++) {
            ssize_t len = ssizeof(s);
            char *ptr = s;

            memset(s, 'a' + (i % 26), sizeof(s));
            while (len > 0) {
                ssize_t res;

                if ((res = write(fd, ptr, len)) <= 0) {
                    if (res < 0 && !ERR_RW_RETRIABLE(errno)) {
                        logger_panic(&_G.logger, "write error: %m");
                    }
                    el_fd_loop(zhttpc_g->ev, 10, EV_FDLOOP_HANDLE_TIMERS);
                    continue;
                }
                ptr += res;
                len -= res;
            }
        }
        el_unregister(&zel_client_g);
    }
    return 0;
}

static int z_reply_no_content(el_t el, int fd, short mask, data_t data)
{
    SB_1k(buf);

    if (sb_read(&buf, fd, 1000) > 0) {
        char reply[] = "HTTP/1.1 204 No Content\r\n\r\n";

        IGNORE(xwrite(fd, reply, sizeof(reply) - 1));
    }
    return 0;
}

static int z_accept(el_t el, int fd, short mask, data_t data)
{
    int (* query_cb)(el_t, int, short, data_t) = data.ptr;
    int client = acceptx(fd, 0);

    if (client >= 0) {
        zel_client_g = el_fd_register(client, true, POLLIN, query_cb, NULL);
    }
    return 0;
}

static int z_query_on_hdrs(httpc_query_t *q)
{
    code_g = q->qinfo->code;
    return 0;
}

static int z_query_on_data(httpc_query_t *q, pstream_t ps)
{
    sb_add(&body_g, ps.s, ps_len(&ps));
    return 0;
}

static void z_query_on_done(httpc_query_t *q, httpc_status_t status)
{
    has_reply_g = true;
    zstatus_g = status;
}

enum z_query_flags {
    Z_QUERY_USE_PROXY = (1 << 0),
};

static int z_query_setup(int (* query_cb)(el_t, int, short, el_data_t),
                         enum z_query_flags flags, lstr_t host, lstr_t uri)
{
    sockunion_t su;
    int server;

    zstatus_g = HTTPC_STATUS_ABORT;
    has_reply_g = false;
    code_g = HTTP_CODE_INTERNAL_SERVER_ERROR;
    sb_init(&body_g);
    sb_init(&zquery_sb_g);

    Z_ASSERT_N(addr_resolve("test", LSTR("127.0.0.1:1"), &su));
    sockunion_setport(&su, 0);

    server = listenx(-1, &su, 1, SOCK_STREAM, IPPROTO_TCP, 0);
    Z_ASSERT_N(server);
    zel_server_g = el_fd_register(server, true, POLLIN, &z_accept, query_cb);

    sockunion_setport(&su, getsockport(server, AF_INET));

    httpc_cfg_init(&zcfg_g);
    zcfg_g.refcnt++;
    zcfg_g.use_proxy = (flags & Z_QUERY_USE_PROXY);
    zhttpc_g = httpc_connect(&su, &zcfg_g, NULL);
    Z_ASSERT_P(zhttpc_g);

    httpc_query_init(&zquery_g);
    httpc_bufferize(&zquery_g, 40 << 20);
    zquery_g.on_hdrs = &z_query_on_hdrs;
    zquery_g.on_data = &z_query_on_data;
    zquery_g.on_done = &z_query_on_done;

    httpc_query_attach(&zquery_g, zhttpc_g);
    httpc_query_start(&zquery_g, HTTP_METHOD_GET, host, uri);
    httpc_query_hdrs_done(&zquery_g, 0, false);
    httpc_query_done(&zquery_g);

    while (!has_reply_g) {
        el_loop_timeout(10);
    }
    Z_ASSERT_EQ(zstatus_g, HTTPC_STATUS_OK);
    Z_HELPER_END;
}

static void z_query_cleanup(void) {
    httpc_query_wipe(&zquery_g);
    el_unregister(&zel_server_g);
    el_unregister(&zel_client_g);
    el_loop_timeout(10);
    sb_wipe(&body_g);
    sb_wipe(&zquery_sb_g);
}

Z_GROUP_EXPORT(httpc) {
    Z_TEST(unexpected_100_continue, "test behavior when receiving 100") {
        Z_HELPER_RUN(z_query_setup(&z_reply_100, 0,
                                   LSTR("localhost"), LSTR("/")));

        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR("Coucou"));

        z_query_cleanup();
    } Z_TEST_END;

    Z_TEST(gzip_with_zero_length, "test Content-Encoding: gzip with Content-Length: 0") {
        Z_HELPER_RUN(z_query_setup(&z_reply_gzip_empty, 0,
                                   LSTR("localhost"), LSTR("/")));

        Z_ASSERT_EQ((http_code_t)HTTP_CODE_ACCEPTED , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR(""));

        z_query_cleanup();
    } Z_TEST_END;

    Z_TEST(close_with_no_content_length, "test close without Content-Length") {
        Z_HELPER_RUN(z_query_setup(&z_reply_close_without_content_length, 0,
                                   LSTR("localhost"), LSTR("/")));

        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_EQ(body_g.len, 8192 * 4096  + 4);
        Z_ASSERT_LSTREQUAL(LSTR_INIT_V(body_g.data, 4), LSTR("Plop"));
        sb_skip(&body_g, 4);
        for (int i = 0; i < body_g.len; i++) {
            Z_ASSERT_EQ(body_g.data[i], 'a' + ((i / 8192) % 26));
        }

        z_query_cleanup();
    } Z_TEST_END;

    Z_TEST(url_host_and_uri, "test hosts and URIs") {
        /* Normal usage, target separate host and URI */
        Z_HELPER_RUN(z_query_setup(&z_reply_keep, 0,
                                   LSTR("localhost"), LSTR("/coucou")));
        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR("Coucou"));
        Z_ASSERT(lstr_startswith(LSTR_SB_V(&zquery_sb_g),
            LSTR("GET /coucou HTTP/1.1\r\n"
                 "Host: localhost\r\n")));
        z_query_cleanup();

        /* Proxy that target separate host and URI, URI must be transform to
         * absolute */
        Z_HELPER_RUN(z_query_setup(&z_reply_keep, Z_QUERY_USE_PROXY,
                                   LSTR("localhost"), LSTR("/coucou")));
        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR("Coucou"));
        Z_ASSERT(lstr_startswith(LSTR_SB_V(&zquery_sb_g),
            LSTR("GET http://localhost/coucou HTTP/1.1\r\n"
                 "Host: localhost\r\n")));
        z_query_cleanup();

        /* same thing without leading / */
        Z_HELPER_RUN(z_query_setup(&z_reply_keep, Z_QUERY_USE_PROXY,
                                   LSTR("localhost"), LSTR("coucou")));
        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR("Coucou"));
        Z_ASSERT(lstr_startswith(LSTR_SB_V(&zquery_sb_g),
            LSTR("GET http://localhost/coucou HTTP/1.1\r\n"
                 "Host: localhost\r\n")));
        z_query_cleanup();

        /* Proxy with absolute HTTP URL */
        Z_HELPER_RUN(z_query_setup(&z_reply_keep, Z_QUERY_USE_PROXY,
                                   LSTR("localhost"),
                                   LSTR("http://localhost:80/coucou")));
        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR("Coucou"));
        Z_ASSERT(lstr_startswith(LSTR_SB_V(&zquery_sb_g),
            LSTR("GET http://localhost:80/coucou HTTP/1.1\r\n"
                 "Host: localhost\r\n")));
        z_query_cleanup();

        /* Same thing with HTTPS */
        Z_HELPER_RUN(z_query_setup(&z_reply_keep, Z_QUERY_USE_PROXY,
                                   LSTR("localhost"),
                                   LSTR("https://localhost:443/coucou")));
        Z_ASSERT_EQ((http_code_t)HTTP_CODE_OK , code_g);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&body_g), LSTR("Coucou"));
        Z_ASSERT(lstr_startswith(LSTR_SB_V(&zquery_sb_g),
            LSTR("GET https://localhost:443/coucou HTTP/1.1\r\n"
                 "Host: localhost\r\n")));
        z_query_cleanup();
    } Z_TEST_END;

    Z_TEST(no_content, "test a reply with NO_CONTENT code") {
        Z_HELPER_RUN(z_query_setup(&z_reply_no_content, 0,
                                   LSTR("localhost"), LSTR("/")));
        Z_ASSERT_EQ((http_code_t)HTTP_CODE_NO_CONTENT, code_g);
        z_query_cleanup();
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */
