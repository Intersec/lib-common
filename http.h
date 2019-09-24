/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
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

#ifndef IS_LIB_INET_HTTP_H
#define IS_LIB_INET_HTTP_H
#ifndef __cplusplus

#include "zlib-wrapper.h"
#include "el.h"
#include "net.h"
#include "container-qhash.h"
#include "ssl.h"

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

typedef enum http_method_t {
    HTTP_METHOD_ERROR = -1,
    /* rfc 2616: §5.1.1: Method */
    /* XXX be careful, this struct is correlated with IopHttpMethod
     * in core.iop */
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_GET,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_CONNECT,
    HTTP_METHOD__MAX,
} http_method_t;
extern lstr_t const http_method_str[HTTP_METHOD__MAX];

typedef enum http_code_t {
    HTTP_CODE_CONTINUE                 = 100,
    HTTP_CODE_SWITCHING_PROTOCOL       = 101,

    HTTP_CODE_OK                       = 200,
    HTTP_CODE_CREATED                  = 201,
    HTTP_CODE_ACCEPTED                 = 202,
    HTTP_CODE_NON_AUTHORITATIVE        = 203,
    HTTP_CODE_NO_CONTENT               = 204,
    HTTP_CODE_RESET_CONTENT            = 205,
    HTTP_CODE_PARTIAL_CONTENT          = 206,

    HTTP_CODE_MULTIPLE_CHOICES         = 300,
    HTTP_CODE_MOVED_PERMANENTLY        = 301,
    HTTP_CODE_FOUND                    = 302,
    HTTP_CODE_SEE_OTHER                = 303,
    HTTP_CODE_NOT_MODIFIED             = 304,
    HTTP_CODE_USE_PROXY                = 305,
    HTTP_CODE_TEMPORARY_REDIRECT       = 307,

    HTTP_CODE_BAD_REQUEST              = 400,
    HTTP_CODE_UNAUTHORIZED             = 401,
    HTTP_CODE_PAYMENT_REQUIRED         = 402,
    HTTP_CODE_FORBIDDEN                = 403,
    HTTP_CODE_NOT_FOUND                = 404,
    HTTP_CODE_METHOD_NOT_ALLOWED       = 405,
    HTTP_CODE_NOT_ACCEPTABLE           = 406,
    HTTP_CODE_PROXY_AUTH_REQUIRED      = 407,
    HTTP_CODE_REQUEST_TIMEOUT          = 408,
    HTTP_CODE_CONFLICT                 = 409,
    HTTP_CODE_GONE                     = 410,
    HTTP_CODE_LENGTH_REQUIRED          = 411,
    HTTP_CODE_PRECONDITION_FAILED      = 412,
    HTTP_CODE_REQUEST_ENTITY_TOO_LARGE = 413,
    HTTP_CODE_REQUEST_URI_TOO_LARGE    = 414,
    HTTP_CODE_UNSUPPORTED_MEDIA_TYPE   = 415,
    HTTP_CODE_REQUEST_RANGE_UNSAT      = 416,
    HTTP_CODE_EXPECTATION_FAILED       = 417,
    /* the status 429 was introduced in rfc 6585 $4 */
    HTTP_CODE_TOO_MANY_REQUESTS        = 429,

    HTTP_CODE_INTERNAL_SERVER_ERROR    = 500,
    HTTP_CODE_NOT_IMPLEMENTED          = 501,
    HTTP_CODE_BAD_GATEWAY              = 502,
    HTTP_CODE_SERVICE_UNAVAILABLE      = 503,
    HTTP_CODE_GATEWAY_TIMEOUT          = 504,
    HTTP_CODE_VERSION_NOT_SUPPORTED    = 505,
} http_code_t;
__attribute__((pure))
lstr_t http_code_to_str(http_code_t code);

typedef enum http_wkhdr_t {
    HTTP_WKHDR_OTHER_HEADER = -1,

    /* rfc 2616: §4.5: General Header Fields */
#define HTTP_WKHDR__GENERAL_FIRST  HTTP_WKHDR_CACHE_CONTROL
    HTTP_WKHDR_CACHE_CONTROL,
    HTTP_WKHDR_CONNECTION,
    HTTP_WKHDR_DATE,
    HTTP_WKHDR_PRAGMA,
    HTTP_WKHDR_TRAILER,
    HTTP_WKHDR_TRANSFER_ENCODING,
    HTTP_WKHDR_UPGRADE,
    HTTP_WKHDR_VIA,
    HTTP_WKHDR_WARNING,
#define HTTP_WKHDR__GENERAL_LAST   HTTP_WKHDR_WARNING

#define HTTP_WKHDR__REQRES_FIRST   HTTP_WKHDR_ACCEPT
    /* rfc 2616: §5.3: Request Header Fields */
    HTTP_WKHDR_ACCEPT,
    HTTP_WKHDR_ACCEPT_CHARSET,
    HTTP_WKHDR_ACCEPT_ENCODING,
    HTTP_WKHDR_ACCEPT_LANGUAGE,
    HTTP_WKHDR_AUTHORIZATION,
    HTTP_WKHDR_EXPECT,
    HTTP_WKHDR_FROM,
    HTTP_WKHDR_HOST,
    HTTP_WKHDR_IF_MATCH,
    HTTP_WKHDR_IF_MODIFIED_SINCE,
    HTTP_WKHDR_IF_NONE_MATCH,
    HTTP_WKHDR_IF_RANGE,
    HTTP_WKHDR_IF_UNMODIFIED_SINCE,
    HTTP_WKHDR_MAX_FORMWARDS,
    HTTP_WKHDR_PROXY_AUTHORIZATION,
    HTTP_WKHDR_RANGE,
    HTTP_WKHDR_REFERER,
    HTTP_WKHDR_TE,
    HTTP_WKHDR_USER_AGENT,

    /* rfc 2616: §6.2: Response header Fields */
    HTTP_WKHDR_ACCEPT_RANGES,
    HTTP_WKHDR_AGE,
    HTTP_WKHDR_ETAG,
    HTTP_WKHDR_LOCATION,
    HTTP_WKHDR_PROXY_AUTHENTICATE,
    HTTP_WKHDR_RETRY_AFTER,
    HTTP_WKHDR_SERVER,
    HTTP_WKHDR_VARY,
    HTTP_WKHDR_WWW_AUTHENTICATE,
#define HTTP_WKHDR__REQRES_LAST    HTTP_WKHDR_WWW_AUTHENTICATE

    /* rfc 2616: §7.1: Entity Header Fields */
#define HTTP_WKHDR__ENTITY_FIRST   HTTP_WKHDR_ALLOW
    HTTP_WKHDR_ALLOW,
    HTTP_WKHDR_CONTENT_ENCODING,
    HTTP_WKHDR_CONTENT_LANGUAGE,
    HTTP_WKHDR_CONTENT_LENGTH,
    HTTP_WKHDR_CONTENT_LOCATION,
    HTTP_WKHDR_CONTENT_MD5,
    HTTP_WKHDR_CONTENT_RANGE,
    HTTP_WKHDR_CONTENT_TYPE,
    HTTP_WKHDR_EXPIRES,
    HTTP_WKHDR_LAST_MODIFIED,
#define HTTP_WKHDR__ENTITY_LAST    HTTP_WKHDR_LAST_MODIFIED

    /* Useful headers */
    HTTP_WKHDR_SOAPACTION,

    HTTP_WKHDR__MAX,
} http_wkhdr_t;
extern char const * nonnull const http_whdr_str[HTTP_WKHDR__MAX];
http_wkhdr_t http_wkhdr_from_ps(pstream_t ps);

#define HTTP_MK_VERSION(M, m)  (((M) << 8) | (m))
#define HTTP_1_0               HTTP_MK_VERSION(1, 0)
#define HTTP_1_1               HTTP_MK_VERSION(1, 1)
#define HTTP_MINOR(v)          ((v) & 0xf)
#define HTTP_MAJOR(v)          ((uint16_t)(v) >> 8)

typedef struct http_qhdr_t {
    int       wkhdr;
    pstream_t key;
    pstream_t val;
} http_qhdr_t;

enum http_parser_state {
    HTTP_PARSER_IDLE,
    HTTP_PARSER_BODY,
    HTTP_PARSER_CHUNK_HDR,
    HTTP_PARSER_CHUNK,
    HTTP_PARSER_CHUNK_TRAILER,
    HTTP_PARSER_CLOSE,
};

static inline const http_qhdr_t * nullable
http_qhdr_find(const http_qhdr_t * nonnull tab, size_t len, http_wkhdr_t wkhdr)
{
    /* scan from the end because the last header prevails */
    for (size_t i = len; i-- > 0; ) {
        if (tab[i].wkhdr == wkhdr)
            return tab + i;
    }
    return NULL;
}

/* {{{ HTTP Server */

struct httpd_query_t;
typedef struct httpd_qinfo_t httpd_qinfo_t;

typedef struct httpd_cfg_t          httpd_cfg_t;
typedef struct httpd_trigger_node_t httpd_trigger_node_t;
typedef struct httpd_trigger_t      httpd_trigger_t;

qm_kvec_t(http_path, lstr_t, httpd_trigger_node_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

enum httpd_query_status {
    HTTPD_QUERY_STATUS_CANCEL,
    HTTPD_QUERY_STATUS_ANSWERED,
};

/** Type of HTTP server.
 *
 * The httpd structure contains callbacks that let you be notified when a
 * connection is established and when a query is processed.
 *
 * - httpd_t#on_accept is called whenever a new connection has been accepted.
 *   The object passed as argument is the fully-filled connection handler
 *   (including its configuration and its file descriptor).
 * - httpd_t#on_status is called whenever the final state of a query is
 *   reached. This state can be:
 *     - the query has been answered, then the query object is passed and the
 *     status is ANSWERED.
 *     - the query has been detached, then the query object is passed and the
 *     status is set to CANCEL. Query are detached when the httpd_t get
 *     disconnected while still having pending queries.
 * - httpd_t#on_disconnect is called whenever the connection get closed.
 */
#define HTTPD_FIELDS(pfx) \
    OBJECT_FIELDS(pfx);                                                      \
    dlist_t            httpd_link;                                           \
    httpd_cfg_t       * nonnull cfg;                                         \
    el_t               nonnull ev;                                           \
    sb_t               ibuf;                                                 \
    z_stream           zs;                                                   \
                                                                             \
    bool               connection_close   : 1;                               \
    bool               compressed         : 1;                               \
    bool               want_write         : 1;                               \
    uint8_t            state;                                                \
    uint16_t           queries;                                              \
    uint16_t           queries_done;                                         \
    unsigned           max_queries;                                          \
    int                chunk_length;                                         \
                                                                             \
    dlist_t            query_list;                                           \
    outbuf_t           ob;                                                   \
    lstr_t             peer_address;/* do not use directly. Use the          \
                                     * httpd_get_peer_address function       \
                                     * instead */                            \
    sockunion_t        peer_su;                                              \
    SSL               * nullable ssl;                                        \
                                                                             \
    void             (*nullable on_accept)(httpd_t * nonnull w);             \
    void             (*nullable on_disconnect)(httpd_t * nonnull w);         \
                                                                             \
    __attribute__((format(printf, 4, 0)))                                    \
    void (* nullable on_status)(httpd_t * nonnull w,                         \
                                const struct httpd_query_t * nonnull q,      \
                                int status, const char * nonnull fmt,        \
                                va_list va);

#define HTTPD_METHODS(type_t) \
    OBJECT_METHODS(type_t)

OBJ_CLASS(httpd, object, HTTPD_FIELDS, HTTPD_METHODS);

/** type for HTTPD triggers authentication callbacks.
 * The authentication callback is always called as soon as there is one on a
 * given trigger descriptor.
 *
 * Though so that the authentication callback can allow non authenticated
 * content to be returned, it is ALSO called if there was no Authorization:
 * header in the HTTP query. In that case, \a user and \a pw are set to the
 * "NULL" pstream (meaning <code>{ NULL, NULL }</code>). In the other case
 * both \a user and \a pw point to valid NUL-terminated strings.
 *
 * If the Authorization field isn't valid, then of course the query is
 * rejected by the http library and the callback isn't called.
 *
 * This callback is fired as soon as the HTTP headers are received, meaning
 * that for many reasons the actual query may never happen (connection lost,
 * invalid formatting of the rest of the query, etc…). So be very careful if
 * you store allocated information in the query descriptor to either:
 * - use the httpd_trigger_t#query_cls trick, and deallocate this information
 *   in the class destructor;
 * - make good use of httpd_trigger_t#on_query_wipe to finalize the query.
 * so that this data gets properly deallocated.
 *
 * \param[in]  cb   the callback that was matched.
 * \param[in]  q    the descriptor of the incoming query.
 * \param[in]  user "user" part of the Authorization field.
 * \param[in]  pw   "password" part of the Authorization field.
 */
typedef void (httpd_trigger_auth_f)(httpd_trigger_t * nonnull cb,
                                    struct httpd_query_t * nonnull q,
                                    pstream_t user, pstream_t pw);

/** an HTTP trigger that can be fired on given path fragments.
 *
 * An httpd trigger captures everything under the path fragment it was
 * registered under with #httpd_trigger_register. Unless there is a better
 * match for the query path.
 *
 * Note that an httpd_trigger are meant to be allocated then registered into
 * one or more httpd trigger trees. Once registered at least once, the trigger
 * is owned by all those trees.
 *
 * If a trigger should survive the http tree it is registered into, then
 * #httpd_trigger_persist() must be called. #httpd_trigger_loose() is its
 * contrary, and if the trigger wasn't registered anywhere a call to this
 * function will destroy the trigger. Hence if you're not sure whether a
 * trigger you create will be registered or not, the proper sequence to avoid
 * leaks is:
 *
 * <code>
 *   httpd_trigger_t *cb = httpd_trigger_new();
 *
 *   cb->... = ...; // configure callback
 *   httpd_trigger_persist(cb);
 *   call_some_function_that_may_register_cb_somewhere_or_maybe_not(cb);
 *   httpd_trigger_loose(cb);
 * </code>
 */
struct httpd_trigger_t {
    unsigned              refcnt;
    lstr_t                auth_realm;
    httpd_trigger_auth_f * nullable auth;
    const object_class_t * nullable query_cls;

    void (* nonnull cb)(httpd_trigger_t * nonnull,
                        struct httpd_query_t * nonnull,
                        const httpd_qinfo_t * nonnull);
    void (* nullable destroy)(httpd_trigger_t * nonnull);
    void (* nullable on_query_wipe)(struct httpd_query_t * nonnull q);
};

struct httpd_trigger_node_t {
    qm_t(http_path)  childs;
    httpd_trigger_t * nullable cb;
    char             path[];
};

struct httpd_cfg_t {
    int      refcnt;
    unsigned nb_conns;

    unsigned outbuf_max_size;
    unsigned on_data_threshold;
    unsigned max_queries;
    unsigned noact_delay;
    unsigned max_conns;
    uint16_t pipeline_depth;
    unsigned header_line_max;
    unsigned header_size_max;
    lstr_t cert;
    lstr_t key;

    SSL_CTX * nullable ssl_ctx;
    dlist_t httpd_list;
    const object_class_t * nullable httpd_cls;
    httpd_trigger_node_t  roots[HTTP_METHOD_DELETE + 1];
};

struct core__httpd_cfg__t;

httpd_cfg_t * nonnull httpd_cfg_init(httpd_cfg_t * nonnull cfg);
int httpd_cfg_from_iop(httpd_cfg_t * nonnull cfg,
                       const struct core__httpd_cfg__t * nonnull iop_cfg);
void httpd_cfg_wipe(httpd_cfg_t * nonnull cfg);
DO_REFCNT(httpd_cfg_t, httpd_cfg);

el_t nullable httpd_listen(sockunion_t * nonnull su, httpd_cfg_t * nonnull);
void httpd_unlisten(el_t nullable * nonnull ev);
httpd_t * nonnull httpd_spawn(int fd, httpd_cfg_t * nonnull);

/** gently close an httpd connection.
 *
 * The httpd_t is never destroyed after this function call to ensure
 * consistent behavior. Instead it is scheduled for "writing" so that the
 * event loop destroys it in its next iteration.
 */
void     httpd_close_gently(httpd_t * nonnull w);

/** retrieve the peer address as a string */
lstr_t   httpd_get_peer_address(httpd_t * nonnull w);

GENERIC_NEW_INIT(httpd_trigger_t, httpd_trigger);
void httpd_trigger_persist(httpd_trigger_t * nonnull);
void httpd_trigger_loose(httpd_trigger_t * nonnull);
httpd_trigger_t * nonnull httpd_trigger_dup(httpd_trigger_t * nonnull cb);
void httpd_trigger_delete(httpd_trigger_t * nullable * nonnull cbp);

bool httpd_trigger_register_flags(httpd_trigger_node_t * nonnull,
                                  const char * nonnull path,
                                  httpd_trigger_t * nonnull cb,
                                  bool overwrite);
bool httpd_trigger_unregister_(httpd_trigger_node_t * nonnull,
                               const char * nonnull path,
                               httpd_trigger_t * nonnull cb);

static inline void
httpd_trigger_set_auth(httpd_trigger_t * nonnull cb,
                       httpd_trigger_auth_f * nonnull auth,
                       const char * nullable auth_realm)
{
    lstr_t s = LSTR(auth_realm ?: "Intersec HTTP Server");

    lstr_copy(&cb->auth_realm, s);
    cb->auth = auth;
}

#define httpd_trigger_register(cfg, m, p, cb) \
    httpd_trigger_register_flags(&(cfg)->roots[HTTP_METHOD_##m], p, cb, true)
#define httpd_trigger_register2(cfg, m, p, cb, fl) \
    httpd_trigger_register_flags(&(cfg)->roots[HTTP_METHOD_##m], p, cb, fl)
#define httpd_trigger_unregister2(cfg, m, p, cb) \
    httpd_trigger_unregister_(&(cfg)->roots[HTTP_METHOD_##m], p, cb)
#define httpd_trigger_unregister(cfg, m, p) \
    httpd_trigger_unregister2(cfg, m, p, NULL)


/* }}} */
/* {{{ HTTP Server Queries Related */

struct httpd_qinfo_t {
    http_method_t method;
    uint16_t      http_version;
    uint16_t      hdrs_len;

    pstream_t     host;
    pstream_t     prefix;
    pstream_t     query;
    pstream_t     vars;

    pstream_t     hdrs_ps;
    http_qhdr_t  * nonnull hdrs;
};

/** \typedef http_query_t.
 * \brief HTTP Query base class.
 *
 * An http_query_t is the base class for queries received on an #httpd_t.
 *
 * It is refcounted, and is valid until it's answered (no matter if the
 * underlying #httpd_t available as http_query_t#owner is still valid or not).
 *
 * As a consequence it means that nobody should ever suppose that a query is
 * still valid once #httpd_reply_done() #httpd_reject() or any function using
 * them internaly (#httpd_reply_202accepted() e.g.) is called.
 *
 * If this is required, then use #obj_retain() and #obj_release() accordingly
 * to ensure the liveness of the #httpd_query_t.
 *
 * <h1>How to use an #http_query_t</h1>
 *
 *   When a the Headers of an HTTP query is received, the matching
 *   #httpd_trigger_t is looked up, the #httpd_query_t (or a subclass if
 *   httpd_trigger_t#query_cls is set) is created. Then if there is a
 *   httpd_trigger_t#auth callback, it is called (possibly with empty
 *   #pstream_t's if there is no Authentication header).
 *
 *   If the authentication callback hasn't rejected the query, then the
 *   httpd_trigger_t#cb callback is called with the created query (no body
 *   still has been received at this point). This is the moment to setup the
 *   #httpd_query_t on_data/on_done/on_ready e.g. using #httpd_bufferize().
 *
 *   Note that the httpd_query_t#on_done hook may never ever be called if the
 *   HTTP query was invalid or the connection lost. That's why it's important
 *   to properly use the httpd_trigger_t#on_query_wipe hook (for when no
 *   subclassing of the #httpd_query_t has been done and its
 *   httpd_query_t#priv pointer is used) or properly augment the
 *   httpd_query_t#wipe implementation to wipe all memory clean.
 *
 * <h1>Important considerations</h1>
 *
 *   #httpd_query_t can be answered to asynchronously, but that does not mean
 *   that the underlying connection is still here. One can know at each time
 *   if there is still a connection looking at httpd_query_t#owner pointer. If
 *   it's NULL, the #httpd_t is dead. In that case it is correct to
 *   #obj_release() the query and go away since anything that would else be
 *   answered would be discarded anyway.
 */
#define HTTPD_QUERY_FIELDS(pfx)                                              \
    OBJECT_FIELDS(pfx);                                                      \
                                                                             \
    httpd_t            * nonnull owner;                                      \
    httpd_trigger_t    * nullable trig_cb;                                   \
    dlist_t             query_link;                                          \
                                                                             \
    /* User flags    */                                                      \
    bool                traced        : 1;                                   \
                                                                             \
    /* Input related */                                                      \
    bool                expect100cont : 1;                                   \
    bool                parsed        : 1;                                   \
                                                                             \
    /* Output related */                                                     \
    bool                own_ob        : 1;                                   \
    bool                hdrs_started  : 1;                                   \
    bool                hdrs_done     : 1;                                   \
    bool                chunk_started : 1;                                   \
    bool                clength_hack  : 1;                                   \
    bool                answered      : 1;                                   \
    bool                chunked       : 1;                                   \
    bool                conn_close    : 1;                                   \
    bool                status_sent   : 1;                                   \
                                                                             \
    uint16_t            answer_code;                                         \
    uint16_t            http_version;                                        \
    time_t              query_sec;                                           \
    unsigned            query_usec;                                          \
    unsigned            received_hdr_length;                                 \
    unsigned            received_body_length;                                \
                                                                             \
    int                 chunk_hdr_offs;                                      \
    int                 chunk_prev_length;                                   \
    unsigned            payload_max_size;                                    \
    int                 ready_threshold;                                     \
                                                                             \
    sb_t                payload;                                             \
    outbuf_t           * nullable ob;                                        \
    httpd_qinfo_t      * nullable qinfo;                                     \
    void               * nullable priv;                                      \
                                                                             \
    void              (*nullable on_data)(httpd_query_t * nonnull q,         \
                                          pstream_t ps);                     \
    void              (*nullable on_done)(httpd_query_t * nonnull q);        \
    void              (*nullable on_ready)(httpd_query_t * nonnull q)

#define HTTPD_QUERY_METHODS(type_t) \
    OBJECT_METHODS(type_t)

OBJ_CLASS(httpd_query, object, HTTPD_QUERY_FIELDS, HTTPD_QUERY_METHODS);

/**
 * XXX: the function can call httpd_reject.
 *
 * Setting the query's methods should always be done before calling this
 * function. The following code is buggy because this might generate two
 * answers:
 *
 *   ...
 *   httpd_bufferize(q, 10);
 *   q->on_done = <my function that answers the query>;
 *   ....
 *
 * To avoid errors, the following order should always be respected.
 *
 *   ...
 *   q->on_done = <my function that answers the query>;
 *   httpd_bufferize(q, 10);
 *   ... no more method setting for the query ...
 */
void httpd_bufferize(httpd_query_t * nonnull q, unsigned maxsize);

/*---- headers utils ----*/

httpd_qinfo_t * nonnull httpd_qinfo_dup(const httpd_qinfo_t * nonnull info);
static inline
void httpd_qinfo_delete(httpd_qinfo_t * nullable * nonnull infop)
{
    p_delete(infop);
}
int t_httpd_qinfo_get_basic_auth(const httpd_qinfo_t * nonnull info,
                                 pstream_t * nonnull user,
                                 pstream_t * nonnull pw);

enum {
    HTTPD_ACCEPT_ENC_GZIP     = 1U << 0,
    HTTPD_ACCEPT_ENC_DEFLATE  = 1U << 1,
    HTTPD_ACCEPT_ENC_COMPRESS = 1U << 2,

    HTTPD_ACCEPT_ENC_ANY      = 7U,
};

/* returns an HTTPD_ACCEPT_ENC* mask, or 0 if not header was preset */
int httpd_qinfo_accept_enc_get(const httpd_qinfo_t * nonnull info);

/*---- low level httpd_query reply functions ----*/

static inline outbuf_t * nonnull httpd_get_ob(httpd_query_t * nonnull q)
{
    if (unlikely(!q->ob)) {
        q->own_ob = true;
        q->ob     = ob_new();
    }
    return q->ob;
}

outbuf_t * nonnull httpd_reply_hdrs_start(httpd_query_t * nonnull q,
                                          int code, bool cacheable);
void httpd_put_date_hdr(outbuf_t * nonnull ob, const char * nonnull hdr,
                        time_t now);

/** Ends the headers, setups for the body streaming.
 *
 * \param[in]  q      the query
 * \param[in]  clen
 *   the content length if known. If positive or 0, \a chunked is ignored.
 *   if it is negative then the behaviour depends on \a chunked.
 * \param[in]  chunked
 *   true if you want to stream packets of data with returns to the event
 *   loop, else pass false (even when \a clen is negative).
 *   In this way, the content-length that #httpd_reply_hdrs_done() generates
 *   leaves a place-holder to be patched later. Of course this is only
 *   possible when the body will be generated without any return to the event
 *   loop (else the #httpd_t could begin the stream the Headers, which is
 *   incorrect).
 *
 *
 * If you don't intend to stream your answer bit by bit, but generate the body
 * at once, always pass \c false as the \a chunked value:
 * - it generates less traffic
 * - when the client advertises as HTTP/1.0, since it doesn't support chunked
 *   encoding, when \c true is passed we go in "ugly" mode, forcing the
 *   connection to be closed at the end of the answer, which is wrong but is
 *   the sole thing we can do.
 */
void httpd_reply_hdrs_done(httpd_query_t * nonnull q, int content_length,
                           bool chunked);
void httpd_reply_done(httpd_query_t * nonnull q);
void httpd_signal_write(httpd_query_t * nonnull q);

/** starts a new chunk.
 * Note that the http chunk has to be ended with #httpd_reply_chunk_done()
 * before going back to the event loop.
 */
static inline void httpd_reply_chunk_start(httpd_query_t * nonnull q,
                                           outbuf_t * nonnull ob)
{
    if (!q->chunked)
        return;
    assert (!q->chunk_started);
    q->chunk_started     = true;
    q->chunk_hdr_offs    = ob_reserve(ob, 12);
    q->chunk_prev_length = ob->length;
}

void httpd_reply_chunk_done_(httpd_query_t * nonnull q,
                             outbuf_t * nonnull ob);
static inline void httpd_reply_chunk_done(httpd_query_t * nonnull q,
                                          outbuf_t * nonnull ob)
{
    if (q->chunked) {
        httpd_reply_chunk_done_(q, ob);
        httpd_signal_write(q);
    }
}

/** Read a key/value pair of the "vars" part of an URL, from a p_stream.
 *
 * This functions reads a key/value pair from a pstream containing the "vars"
 * part of an URL.
 *
 * The result of the parsing is put in the two lstr_t given as argument.
 * Keys and values are URL-decoded; that's why the strings are t-allocated.
 *
 * In case of error, the pstream is placed where it encountered the error.
 * In case of success it is placed at the beginning of the next key/value
 * pair.
 *
 * For example, if the input pstream contains "cid1%3d1%26cid2=2&cid3=3",
 * the first call will read (key: "cid1=1&cid2", value: "2"), and the
 * second call will read (key: "cid3", value: "3") and entirely read the
 * pstream.
 * If called a third time, it will fail.
 *
 * \param[in] ps     The stream from which the vars are read. Should usually
 *                   be a copy of the 'vars' field of httpd_qinfo_t.
 * \param[out] key   A pointer on a lstr_t in which the URL-decoded key will
 *                   be t-allocated.
 * \param[out] value A pointer on a lstr_t in which the URL-decoded value will
 *                   be t-allocated.
 *
 * \return 0 on success, -1 if the content of the pstream does not starts with
 *         a valid URL key/value pair.
 */
int t_ps_get_http_var(pstream_t * nonnull ps, lstr_t * nonnull key,
                      lstr_t * nonnull value);

/*---- high level httpd_query reply functions ----*/

void httpd_reply_100continue(httpd_query_t * nonnull q);
void httpd_reply_202accepted(httpd_query_t * nonnull q);

__attribute__((format(printf, 3, 4)))
void httpd_reject_(httpd_query_t * nonnull q, int code,
                   const char * nonnull fmt, ...);
#define httpd_reject(q, code, fmt, ...) \
    httpd_reject_(q, HTTP_CODE_##code, fmt, ##__VA_ARGS__)
void httpd_reject_unauthorized(httpd_query_t * nonnull q, lstr_t auth_realm);


/*---- http-srv-static.c ----*/
void httpd_reply_make_index(httpd_query_t * nonnull q, int dirfd, bool head);
void httpd_reply_file(httpd_query_t * nonnull q, int dirfd,
                      const char * nonnull file, bool head);

httpd_trigger_t * nonnull
httpd_trigger__static_dir_new(const char * nonnull path);


/* }}} */
/* {{{ HTTP Client */

typedef struct httpc_pool_t httpc_pool_t;
typedef struct httpc_query_t httpc_query_t;

typedef struct httpc_cfg_t {
    int          refcnt;

    bool         use_proxy : 1;
    uint16_t     pipeline_depth;
    unsigned     noact_delay;
    unsigned     max_queries;
    unsigned     on_data_threshold;
    unsigned     header_line_max;
    unsigned     header_size_max;

    SSL_CTX * nullable ssl_ctx;

    const object_class_t * nonnull httpc_cls;
} httpc_cfg_t;

struct core__httpc_cfg__t;

httpc_cfg_t * nonnull httpc_cfg_init(httpc_cfg_t * nonnull cfg);
__must_check__
int httpc_cfg_from_iop(httpc_cfg_t * nonnull cfg,
                       const struct core__httpc_cfg__t * nonnull iop_cfg);
void httpc_cfg_wipe(httpc_cfg_t * nonnull cfg);
DO_REFCNT(httpc_cfg_t, httpc_cfg);

__must_check__
int httpc_cfg_tls_init(httpc_cfg_t * nonnull cfg, sb_t * nonnull err);
void httpc_cfg_tls_wipe(httpc_cfg_t * nonnull cfg);
int
httpc_cfg_tls_add_verify_file(httpc_cfg_t * nonnull cfg, lstr_t cert_path);

struct httpc_t;
/** On connect error callback.
 *
 * HTTP connections use non blocking socket and are asynchronous. In this API
 * when a connection fails, the connection is marked as disconnected and the
 * httpc_t is deleted (see httpc_on_connect implementation).
 * This callback is called when a http connection fails. \ref errnum is the
 * errno set by the getsockopt system call. EINTR and EINPROGRESS are not
 * considered as error.
 */
typedef void (on_connect_error_f)(const struct httpc_t * nonnull httpc,
                                  int errnum);

#define HTTPC_FIELDS(pfx) \
    OBJECT_FIELDS(pfx);                                                      \
    httpc_pool_t * nullable pool;                                            \
    httpc_cfg_t  * nonnull cfg;                                              \
    dlist_t       pool_link;                                                 \
    el_t          nonnull ev;                                                \
    sb_t          ibuf;                                                      \
    z_stream      zs;                                                        \
                                                                             \
    bool          connection_close : 1;                                      \
    bool          busy             : 1;                                      \
    bool          compressed       : 1;                                      \
    uint8_t       state;                                                     \
    uint16_t      queries;                                                   \
    int           chunk_length;                                              \
    unsigned      max_queries;                                               \
    unsigned      received_hdr_length;                                       \
    unsigned      received_body_length;                                      \
                                                                             \
    dlist_t       query_list;                                                \
    outbuf_t      ob;                                                        \
                                                                             \
    SSL * nullable ssl;                                                      \
                                                                             \
    void (*nullable on_query_done)(httpc_t * nonnull,                        \
                                   const httpc_query_t * nonnull,            \
                                   int status);                              \
    on_connect_error_f * nullable on_connect_error;

#define HTTPC_METHODS(type_t)                                                \
    OBJECT_METHODS(type_t);                                                  \
    void (*nonnull set_ready)(type_t * nonnull, bool first);                 \
    void (*nonnull set_busy)(type_t * nonnull);                              \
    void (*nonnull disconnect)(type_t * nonnull)

OBJ_CLASS(httpc, object, HTTPC_FIELDS, HTTPC_METHODS);

httpc_t * nonnull httpc_spawn(int fd, httpc_cfg_t * nonnull,
                              httpc_pool_t * nullable);
httpc_t * nullable httpc_connect_as(const sockunion_t * nonnull,
                                    const sockunion_t * nullable src_addr,
                                    httpc_cfg_t * nonnull,
                                    httpc_pool_t * nullable);
httpc_t * nullable httpc_connect(const sockunion_t * nonnull,
                                 httpc_cfg_t * nonnull,
                                 httpc_pool_t * nullable);

/** gently close an httpc connection.
 *
 * The httpc_t is never destroyed after this function call to ensure
 * consistent behavior. Instead it is scheduled for "writing" so that the
 * event loop destroys it in its next iteration.
 */
void     httpc_close_gently(httpc_t * nonnull);

struct httpc_pool_t {
    httpc_cfg_t * nonnull cfg;
    lstr_t       host;
    sockunion_t  su;
    sockunion_t * nullable su_src; /* to connect using a specific network interface */

    int          len;
    int          max_len;
    int         * nullable len_global;
    int          max_len_global;
    dlist_t      ready_list;
    dlist_t      busy_list;

    void (* nullable on_ready)(httpc_pool_t * nonnull, httpc_t * nonnull);
    void (* nullable on_busy)(httpc_pool_t * nonnull, httpc_t * nonnull);
    on_connect_error_f * nullable on_connect_error;
};

httpc_pool_t * nonnull httpc_pool_init(httpc_pool_t * nonnull);
void httpc_pool_close_clients(httpc_pool_t * nonnull);
void httpc_pool_wipe(httpc_pool_t * nonnull, bool wipe_conns);
GENERIC_NEW(httpc_pool_t, httpc_pool);
static inline void httpc_pool_delete(httpc_pool_t * nullable * nonnull hpcp,
                                     bool wipe_conns)
{
    if (*hpcp) {
        httpc_pool_wipe(*hpcp, wipe_conns);
        p_delete(hpcp);
    }
}

void httpc_pool_detach(httpc_t * nonnull w);
void httpc_pool_attach(httpc_t * nonnull w, httpc_pool_t * nonnull pool);
httpc_t * nullable httpc_pool_launch(httpc_pool_t * nonnull pool);
httpc_t * nullable httpc_pool_get(httpc_pool_t * nonnull pool);

/* }}} */
/* {{{ HTTP Client Queries */

typedef enum httpc_status_t {
    HTTPC_STATUS_OK,
    HTTPC_STATUS_INVALID    = -1,
    HTTPC_STATUS_ABORT      = -2,
    HTTPC_STATUS_TOOLARGE   = -3,
    HTTPC_STATUS_TIMEOUT    = -4,
    HTTPC_STATUS_EXP100CONT = -5,
} httpc_status_t;

typedef struct httpc_qinfo_t {
    http_code_t  code;
    uint16_t     http_version;
    uint16_t     hdrs_len;

    pstream_t    reason;
    pstream_t    hdrs_ps;
    http_qhdr_t * nonnull hdrs;
} httpc_qinfo_t;

struct httpc_query_t {
    httpc_t       * nullable owner;
    dlist_t        query_link;
    httpc_qinfo_t * nullable qinfo;
    sb_t           payload;
    unsigned       payload_max_size;
    unsigned       received_hdr_length;
    unsigned       received_body_length;

    int            chunk_hdr_offs;
    int            chunk_prev_length;
    bool           hdrs_started  : 1;
    bool           hdrs_done     : 1;
    bool           chunked       : 1;
    bool           chunk_started : 1;
    bool           clength_hack  : 1;
    bool           query_done    : 1;
    bool           expect100cont : 1;

    void (*nullable on_100cont)(httpc_query_t * nonnull q);
    int (*nullable on_hdrs)(httpc_query_t * nonnull q);
    int (*nullable on_data)(httpc_query_t * nonnull q, pstream_t ps);
    void (*nullable on_done)(httpc_query_t * nonnull q,
                             httpc_status_t status);
};

void httpc_query_init(httpc_query_t * nonnull q);
void httpc_query_reset(httpc_query_t * nonnull q);
void httpc_query_wipe(httpc_query_t * nonnull q);
/** Call this to schedule a given allocated #httpc_query_t on a #httpc_t.
 *
 * It is up to the caller to ensure that the httpc_t isn't disconnected
 * (httpc_t#ev != NULL) and can still send queries (httpc_t#max_queries > 0).
 *
 * The #httpc_query_t must not have been serialized yet.
 */
void httpc_query_attach(httpc_query_t * nonnull q, httpc_t * nonnull w);

void httpc_bufferize(httpc_query_t * nonnull q, unsigned maxsize);

static ALWAYS_INLINE outbuf_t * nonnull
httpc_get_ob(httpc_query_t * nonnull q)
{
    return &q->owner->ob;
}

void httpc_query_start_flags(httpc_query_t * nonnull q, http_method_t m,
                             lstr_t host, lstr_t uri, bool httpc_encode_url);
#define httpc_query_start(q, m, host, uri) \
    httpc_query_start_flags(q, m, host, uri, true)

/** Ends the headers, setups for the body streaming.
 *
 * \param[in]  q      the query
 * \param[in]  clen
 *   the content length if known. If positive or 0, \a chunked is ignored.
 *   if it is negative then the behaviour depends on \a chunked.
 * \param[in]  chunked
 *   true if you want to stream packets of data with returns to the event
 *   loop, else pass false (even when \a clen is negative).
 *   In this way, the content-length that #httpc_query_hdrs_done generates
 *   leaves a place-holder to be patched later. Of course this is only
 *   possible when the body will be generated without any return to the event
 *   loop (else the #httpd_t could begin the stream the Headers, which is
 *   incorrect).
 *
 * Unlike #httpd_reply_hdrs_done() it's not a problem to pass \c true for \a
 * chunked (except maybe for the additional space it takes) since we're an
 * HTTP/1.1 client.
 */
void httpc_query_hdrs_done(httpc_query_t * nonnull q, int clen, bool chunked);
void httpc_query_done(httpc_query_t * nonnull q);

/** starts a new chunk.
 * Note that the http chunk has to be ended with #httpc_query_chunk_done()
 * before going back to the event loop.
 */
static inline void httpc_query_chunk_start(httpc_query_t * nonnull q,
                                           outbuf_t * nonnull ob)
{
    if (!q->chunked)
        return;
    assert (!q->chunk_started);
    q->chunk_started     = true;
    q->chunk_hdr_offs    = ob_reserve(ob, 12);
    q->chunk_prev_length = ob->length;
}

void httpc_query_chunk_done_(httpc_query_t * nonnull q,
                             outbuf_t * nonnull ob);
static inline void httpc_query_chunk_done(httpc_query_t * nonnull q,
                                          outbuf_t * nonnull ob)
{
    if (q->chunked)
        httpc_query_chunk_done_(q, ob);
}

void httpc_query_hdrs_add_auth(httpc_query_t * nonnull q, lstr_t login,
                               lstr_t passwd);

static inline void httpc_query_hdrs_add(httpc_query_t * nonnull q, lstr_t hdr)
{
    outbuf_t *ob = &q->owner->ob;

    assert (q->hdrs_started && !q->hdrs_done);
    ob_add(ob, hdr.s, hdr.len);
    ob_adds(ob, "\r\n");
}

static inline void httpc_query_hdrs_adds(httpc_query_t * nonnull q,
                                         const char * nonnull hdr)
{
    httpc_query_hdrs_add(q, LSTR(hdr));
}

/* }}} */
/* {{{ HTTP module */

MODULE_DECLARE(http);

/* }}} */

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
#endif
