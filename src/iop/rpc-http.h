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

#if !defined(IS_LIB_COMMON_IOP_RPC_H) || defined(IS_LIB_COMMON_IOP_RPC_HTTP_H)
#  error "you must include <lib-common/iop-rpc.h> instead"
#else
#define IS_LIB_COMMON_IOP_RPC_HTTP_H
#ifndef __cplusplus

#include <lib-common/iop-json.h>

/*
 * RPC-HTTP: a library for wrapping and exposing IoP RPCs as HTTP-based API
 * end-points.
 *
 * At the server side, this api enables the calling code to wrap a set of RPCs
 * under a common server url such as http[s]://api.example.com/v1/.
 * This is done as follows:
 *  - create a httpd_trigger__ic_t (subclassed from httpd_trigger) using
 *    httpd_trigger__ic_new.
 *  - register the trigger for the server url using httpd_trigger_register.
 *  - register the IoP RPCs.
 *  <code>
 *  ...
 *    hrpc_trigger = httpd_trigger__ic_new(&iop_mod, api_schema, MAX_REQ_SZ);
 *    httpd_trigger_register(httpd_cfg, POST, server_url, &hrpc_trigger->cb);
 *    ichttp_register(hrpc_trigger, iop_mod_name, iop_interface, rpc1);
 *    ichttp_register(hrpc_trigger, iop_mod_name, iop_interface, rpc2);
 *  ...
 *  </code>
 * The trigger then handles the unpacking of http requests into the
 * corresponding rpc in two modes: XML mode or JSON mode. The XML mode is
 * compatible with SOAP. This step involves finding the rpc to invoke and
 * deserializing input parameters for the rpc. For the response,
 * the corresponding packing process is done automatically by ic_reply.
 *
 *
 * At the client side, this api enables the calling code to call HTTP RPCs and
 * consume their responses (in JSON format).
 */

/* {{{ Server-side rpc-http */

/**************************************************************************/
/* HTTP Queries                                                           */
/**************************************************************************/

typedef struct ichttp_cb_t {
    int              refcnt;
    int32_t          cmd;
    ic_cb_entry_t    e;
    const iop_rpc_t * nonnull fun;
    lstr_t           name;
    lstr_t           name_uri;
    lstr_t           name_res;
    lstr_t           name_exn;
} ichttp_cb_t;
GENERIC_INIT(ichttp_cb_t, ichttp_cb);
void ichttp_cb_wipe(ichttp_cb_t * nonnull rpc);
DO_REFCNT(ichttp_cb_t, ichttp_cb);

#define ICHTTP_QUERY_FIELDS(pfx)                                             \
    HTTPD_QUERY_FIELDS(pfx);                                                 \
    ichttp_cb_t * nonnull cbe;                                               \
    ic__hdr__t * nullable ic_hdr;                                            \
    size_t iop_res_size;                                                     \
    bool   json;                                                             \
    bool   iop_answered

#define ICHTTP_QUERY_METHODS(type_t) \
    HTTPD_QUERY_METHODS(type_t)

OBJ_CLASS(ichttp_query, httpd_query,
          ICHTTP_QUERY_FIELDS, ICHTTP_QUERY_METHODS);


/**************************************************************************/
/* HTTP Triggers                                                          */
/**************************************************************************/

qm_kvec_t(ichttp_cbs, lstr_t, ichttp_cb_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

typedef struct httpd_trigger__ic_t {
    httpd_trigger_t          cb;
    unsigned                 query_max_size;
    const char              * nonnull schema;
    const char              * nullable auth_kind;
    const iop_iface_alias_t * nonnull mod;
    qm_t(ichttp_cbs)         impl;
    unsigned                 xpack_flags;
    unsigned                 jpack_flags;
    unsigned                 unpack_flags;

    void (* nonnull on_reply)(const struct httpd_trigger__ic_t * nonnull,
                              const ichttp_query_t * nonnull, size_t res_size,
                              http_code_t res_code);
} httpd_trigger__ic_t;

/* compat for qrrd */
#define ichttp_trigger_cb_t    httpd_trigger__ic_t
#define httpd_trigger__ichttp  httpd_trigger__ic_new


/**************************************************************************/
/* APIs                                                                   */
/**************************************************************************/

httpd_trigger__ic_t * nonnull
httpd_trigger__ic_new(const iop_mod_t * nonnull mod,
                      const char * nonnull schema,
                      unsigned szmax);

/* helper for public interface: reject private fields in queries, and skip
 * private fields in answers */
static inline
void httpd_trigger__ic_set_public(httpd_trigger__ic_t * nonnull tcb)
{
    tcb->unpack_flags |= IOP_UNPACK_FORBID_PRIVATE;
    tcb->xpack_flags  |= IOP_XPACK_SKIP_PRIVATE;
    tcb->jpack_flags  |= IOP_JPACK_SKIP_PRIVATE;
}

/** \brief internal do not use directly, or know what you're doing. */
static inline ichttp_query_t * nonnull ichttp_slot_to_query(uint64_t slot)
{
    assert (ic_slot_is_http(slot));
    return (ichttp_query_t *)((uintptr_t)slot << 2);
}

/** \brief internal do not use directly, or know what you're doing. */
static inline uint64_t ichttp_query_to_slot(ichttp_query_t * nonnull iq)
{
    return IC_SLOT_FOREIGN_HTTP | ((uintptr_t)iq >> 2);
}

/** \brief internal do not use directly, or know what you're doing. */
void __ichttp_reply(uint64_t slot, int cmd, const iop_struct_t * nonnull,
                    const void * nonnull);
/** \brief internal do not use directly, or know what you're doing. */
void __ichttp_proxify(uint64_t slot, int cmd, const void * nonnull data,
                      int dlen);
/** \brief internal do not use directly, or know what you're doing. */
void __ichttp_reply_err(uint64_t slot, int err,
                        const lstr_t * nullable err_str);
/** \brief internal do not use directly, or know what you're doing. */
void __ichttp_reply_soap_err(uint64_t slot, bool serverfault,
                             const lstr_t * nullable err);
/** \brief internal do not use directly, or know what you're doing. */
void __ichttp_forward_reply(ichannel_t * nullable pxy_ic, uint64_t slot,
                            int cmd, const void * nullable res,
                            const void * nullable exn);

#define __ichttp_reply_soap_err_cst(slot, serverfault, err) \
    __ichttp_reply_soap_err(slot, serverfault, &LSTR_IMMED_V(err))

/** \brief internal do not use directly, or know what you're doing. */
int __t_ichttp_query_on_done_stage1(httpd_query_t * nonnull q,
                                    ichttp_cb_t * nullable * nonnull cbe,
                                    void * nullable * nonnull value,
                                    bool * nonnull soap);
/** \brief internal do not use directly, or know what you're doing. */
void __t_ichttp_query_on_done_stage2(httpd_query_t * nonnull q,
                                     ichttp_cb_t * nonnull cbe,
                                     void * nullable value);

/** \brief internal do not use directly, or know what you're doing. */
ichttp_cb_t * nonnull
__ichttp_register(httpd_trigger__ic_t * nonnull tcb,
                  const iop_iface_alias_t * nonnull alias,
                  const iop_rpc_t * nonnull fun, int32_t cmd,
                  const ic_cb_entry_t * nonnull entry);

/** \brief internal do not use directly, or know what you're doing. */
#define ___ichttp_register(tcb, _mod, _if, _rpc, _cb)                        \
    __ichttp_register(tcb, _mod##__##_if##__alias,                           \
                      IOP_RPC(_mod, _if, _rpc),                              \
                      IOP_RPC_CMD(_mod, _if, _rpc), _cb)

/** \brief register a local callback for an rpc on the given http iop trigger.
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  _cb
 *    the implementation callback. Its type should be:
 *    <tt>void (*)(IOP_RPC_IMPL_ARGS(_mod, _if, _rpc))</tt>.
 *    it can be the same implementation callback as the one used for an
 *    #ic_register call.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 *    it can be the same implementation callback as the one used for an
 *    #ic_register call.
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *)</tt>
 *    it can be the same implementation callback as the one used for an
 *    #ic_register call.
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ichttp_register_pre_post_hook_(tcb, _mod, _if, _rpc, _cb, _pre_cb,   \
                                       _post_cb,  _pre_arg, _post_arg)       \
    do {                                                                     \
        void (*__cb)(IOP_RPC_IMPL_ARGS(_mod, _if, _rpc)) = _cb;              \
        ic_cb_entry_t __cb_e = {                                             \
            .cb_type = IC_CB_NORMAL,                                         \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .cb = {                                                   \
                .cb  = (void *)__cb,                                         \
            } },                                                             \
        };                                                                   \
                                                                             \
        ___ichttp_register(tcb, _mod, _if, _rpc, &__cb_e);                   \
    } while (0)

/** \brief same as #ichttp_register_pre_post_hook_ but _pre and _post args
 *    will be transform into data_t ptr.
 */
#define ichttp_register_pre_post_hook_p_(tcb, _mod, _if, _rpc, _cb, _pre_cb, \
                                         _post_cb,  _pre_arg, _post_arg)     \
    ichttp_register_pre_post_hook_(tcb, _mod, _if, _rpc, _cb,                \
                                   _pre_cb, _post_cb,                        \
                                   { .ptr = _pre_arg }, { .ptr = _post_arg })

/** \brief same as #ichttp_register_pre_post_hook_ but auto-computes the
 *    rpc name.
 */
#define ichttp_register_pre_post_hook(tcb, _m, _i, _r, _pre_cb, _post_cb,    \
                                      _pre_arg, _post_arg)                   \
    ichttp_register_pre_post_hook_(tcb, _m, _i, _r,                          \
                                   IOP_RPC_NAME(_m, _i, _r, impl),           \
                                   _pre_cb, _post_cb, _pre_arg, _post_arg)

/** \brief same as #ichttp_register_pre_post_hook_p_ but auto-computes the
 *    rpc name.
 */
#define ichttp_register_pre_post_hook_p(tcb, _m, _i, _r, _pre_cb, _post_cb,  \
                                        _pre_arg, _post_arg)                 \
    ichttp_register_pre_post_hook(tcb, _m, _i, _r, _pre_cb, _post_cb,        \
                                  { .ptr = _pre_arg }, { .ptr = _post_arg })

/** \brief same as #ichttp_register_pre_post_hook_ but doesn't register
 *    pre/post hooks.
 */
#define ichttp_register_(tcb, _mod, _if, _rpc, _cb)                          \
    ichttp_register_pre_post_hook_p_(tcb, _mod, _if, _rpc, _cb,              \
                                     NULL, NULL, NULL, NULL)

/** \brief same as #ichttp_register_ but auto-computes the rpc name. */
#define ichttp_register(tcb, _mod, _if, _rpc)                                \
    ichttp_register_(tcb, _mod, _if, _rpc,                                   \
                     IOP_RPC_NAME(_mod, _if, _rpc, impl))

/** \brief register a proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy_hdr_pre_post_hook
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *)</tt>
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ichttp_register_proxy_hdr_pre_post_hook(tcb, _mod, _if, _rpc, ic,    \
                                                hdr, _pre_cb, _post_cb,      \
                                                _pre_arg, _post_arg)         \
    do {                                                                     \
        ic_cb_entry_t __cb_e = {                                             \
            .cb_type = IC_CB_PROXY_P,                                        \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .proxy_p = { .ic_p = ic, .hdr_p = hdr } },                \
        };                                                                   \
                                                                             \
        ___ichttp_register(tcb, _mod, _if, _rpc, &__cb_e);                   \
    } while (0)

/** \brief same as #ichttp_register_proxy_hdr_pre_post_hook but auto-computes
 *     the rpc name.
 */
#define ichttp_register_proxy_hdr_pre_post_hook_p(tcb, _mod, _if, _rpc, ic,  \
                                                  hdr, _pre_cb, _post_cb,    \
                                                  _pre_arg, _post_arg)       \
    ichttp_register_proxy_hdr_pre_post_hook(tcb, _mod, _if, _rpc, ic,        \
                                            hdr, _pre_cb, _post_cb,          \
                                            { .ptr = _pre_arg },             \
                                            { .ptr = _post_arg })

/** \brief register a proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy_hdr_pre_post_hook
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *)</tt>
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ichttp_register_proxy_pre_post_hook(tcb, _mod, _if, _rpc, ic,        \
                                            _pre_cb, _post_cb,               \
                                            _pre_arg, _post_arg)             \
    ichttp_register_proxy_hdr_pre_post_hook(tcb, _mod, _if, _rpc, ic, NULL,  \
                                            _pre_cb, _post_cb,               \
                                            _pre_arg, _post_arg)

/** \brief same as #ichttp_register_proxy_pre_post_hook but auto-computes
 *     the rpc name.
 */
#define ichttp_register_proxy_pre_post_hook_p(tcb, _mod, _if, _rpc, ic,      \
                                              hdr, _pre_cb, _post_cb,        \
                                              _pre_arg, _post_arg)           \
    ichttp_register_proxy_pre_post_hook(tcb, _mod, _if, _rpc, ic,            \
                                        hdr, _pre_cb, _post_cb,              \
                                        { .ptr = _pre_arg },                 \
                                        { .ptr = _post_arg })

/** \brief register a proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy_hdr
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 */
#define ichttp_register_proxy_hdr(tcb, _mod, _if, _rpc, ic, hdr)             \
    ichttp_register_proxy_hdr_pre_post_hook_p(tcb, _mod, _if, _rpc, ic, hdr, \
                                              NULL, NULL, NULL, NULL)
/** \brief register a proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  ic
 *   the #ichannel_t to unconditionnaly forward the incoming RPCs to.
 */
#define ichttp_register_proxy(tcb, _mod, _if, _rpc, ic) \
    ichttp_register_proxy_hdr(tcb, _mod, _if, _rpc, ic, NULL)

/** \brief register a pointed proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy_hdr_p
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  icp
 *   a pointer to an #ichannel_t. When this pointer points to #NULL, the
 *   request is rejected with an #IC_MSG_PROXY_ERROR status, else it's
 *   proxified to the pointed #ichannel_t.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 *    it can be the same implementation callback as the one used for an
 *    #ic_register call.
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *)</tt>
 *    it can be the same implementation callback as the one used for an
 *    #ic_register call.
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ichttp_register_proxy_hdr_p_pre_post_hook(tcb, _mod, _if, _rpc, ic,  \
                                                  hdr, _pre_cb, _post_cb,    \
                                                  _pre_arg, _post_arg)       \
    do {                                                                     \
        ic_cb_entry_t __cb_e = {                                             \
            .cb_type = IC_CB_PROXY_PP,                                       \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .proxy_pp = { .ic_pp = ic, .hdr_pp = hdr } },             \
        };                                                                   \
                                                                             \
        ___ichttp_register(tcb, _mod, _if, _rpc, &__cb_e);                   \
    } while (0)
/** \brief same as #ichttp_register_pre_post_hook_ but _pre and _post args
 *    will be transform into data_t ptr.
 */
#define ichttp_register_proxy_hdr_p_pre_post_hook_p(tcb, _mod, _if, _rpc,    \
                                                    ic, hdr,                 \
                                                    _pre_cb, _post_cb,       \
                                                    _pre_arg, _post_arg)     \
    ichttp_register_proxy_hdr_p_pre_post_hook(tcb, _mod, _if, _rpc, ic,      \
                                              hdr, _pre_cb, _post_cb,        \
                                              { .ptr = _pre_arg },           \
                                              { .ptr = _post_arg })
/** \brief register a pointed proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy_hdr_p
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  icp
 *   a pointer to an #ichannel_t. When this pointer points to #NULL, the
 *   request is rejected with an #IC_MSG_PROXY_ERROR status, else it's
 *   proxified to the pointed #ichannel_t.
 * \param[in]  hdr    the #ic__hdr__t header to force when proxifying.
 */
#define ichttp_register_proxy_hdr_p(tcb, _mod, _if, _rpc, ic, hdr)           \
    ichttp_register_proxy_hdr_p_pre_post_hook_p(tcb, _mod, _if, _rpc, ic,    \
                                                hdr, NULL, NULL, NULL, NULL)
/** \brief register a pointed proxy for an rpc on the given http iop trigger.
 * \see #ic_register_proxy_p
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  icp
 *   a pointer to an #ichannel_t. When this pointer points to #NULL, the
 *   request is rejected with an #IC_MSG_PROXY_ERROR status, else it's
 *   proxified to the pointed #ichannel_t.
 */
#define ichttp_register_proxy_p(tcb, _mod, _if, _rpc, ic) \
    ichttp_register_proxy_hdr_p(tcb, _mod, _if, _rpc, ic, NULL)

/** \brief register a dynamic proxy for an rpc on the given http iop trigger.
 * \see #ic_register_dynproxy
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  cb
 *   a callback that returns an #ic_dynproxy_t (a pair of an #ichannel_t and
 *   an #ic__hdr__t). When profixying the callback is called. When it returns
 *   a #NULL #ichannel_t the request is rejected with a #IC_MSG_PROXY_ERROR
 *   status, else it's forwarded to this #ichannel_t using the returned
 *   #ic__hdr__t.
 * \param[in]  _priv
 *   an opaque pointer passed to the callback each time it's called.
 * \param[in]  _pre_cb
 *    the pre_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, uint64_t, const ic__hdr__t *, void *)</tt>
 * \param[in]  _post_cb
 *    the post_hook callback. Its type should be:
 *    <tt>void (*)(ichannel_t *, ic_status_t, ic_hook_ctx_t *, void *)</tt>
 * \param[in]  _pre_arg   argument we want to pass to pre_hook
 * \param[in]  _post_arg  argument we want to pass to post_hook
 */
#define ichttp_register_dynproxy_pre_post_hook(tcb, _mod, _if, _rpc, cb,     \
                                               priv_, _pre_cb, _post_cb,     \
                                               _pre_arg, _post_arg)          \
    do {                                                                     \
        ic_cb_entry_t __cb_e = {                                             \
            .cb_type = IC_CB_DYNAMIC_PROXY,                                  \
            .rpc = IOP_RPC(_mod, _if, _rpc),                                 \
            .pre_hook = _pre_cb,                                             \
            .post_hook = _post_cb,                                           \
            .pre_hook_args = _pre_arg,                                       \
            .post_hook_args = _post_arg,                                     \
            .u = { .dynproxy = {                                             \
                .get_ic = cb,                                                \
                .priv   = priv_,                                             \
            } },                                                             \
        };                                                                   \
                                                                             \
        ___ichttp_register(tcb, _mod, _if, _rpc, &__cb_e);                   \
    } while (0)

/** \brief same as #ichttp_register_dynproxy_pre_post_hook but _pre and _post
 *    args will be transform into data_t ptr.
 */
#define ichttp_register_dynproxy_pre_post_hook_p(tcb, _mod, _if, _rpc, cb,   \
                                                 priv_, _pre_cb, _post_cb,   \
                                                 _pre_arg, _post_arg)        \
    ichttp_register_dynproxy_pre_post_hook(tcb, _mod, _if, _rpc, cb,         \
                                           priv_, _pre_cb, _post_cb,         \
                                           { .ptr = _pre_arg },              \
                                           { .ptr = _post_arg })

/** \brief register a dynamic proxy for an rpc on the given http iop trigger.
 * \see #ic_register_dynproxy
 * \param[in]  tcb
 *    the #httpd_trigger__ic_t to register the callback implementation into.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _i     name of the interface of the RPC
 * \param[in]  _r     name of the rpc
 * \param[in]  cb
 *   a callback that returns an #ic_dynproxy_t (a pair of an #ichannel_t and
 *   an #ic__hdr__t). When profixying the callback is called. When it returns
 *   a #NULL #ichannel_t the request is rejected with a #IC_MSG_PROXY_ERROR
 *   status, else it's forwarded to this #ichannel_t using the returned
 *   #ic__hdr__t.
 * \param[in]  _priv
 *   an opaque pointer passed to the callback each time it's called.
 */
#define ichttp_register_dynproxy(tcb, _mod, _if, _rpc, cb, priv_)            \
    ichttp_register_dynproxy_pre_post_hook_p(tcb, _mod, _if, _rpc, cb,       \
                                             priv_, NULL, NULL, NULL, NULL)
/** when called in HTTPD status hook, get the query error context if some */
lstr_t ichttp_err_ctx_get(void);
/** set the error context */
void __ichttp_err_ctx_set(lstr_t err_ctx);
/** clear the error context */
void __ichttp_err_ctx_clear(void);

/* }}} */
/* {{{ Client-side rpc-http */

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnullability-completeness"
#endif

/* {{{ HTTP channel */

typedef struct http_iop_msg_t http_iop_msg_t;
typedef OPT_OF(http_code_t) opt_http_code_t;

/** IOP HTTP query callback.
 *
 * \param[in] msg       The HTTP query message built for the query.
 * \param[in] status    The status of the RPC call.
 * \param[in] http_code The HTTP code of the query.
 * \param[in] res       The result returned by the RPC (only set if status is
 *                      IC_MSG_OK).
 * \param[in] exn       The exception thrown by the RPC (only set if status
 *                      is IC_MSG_EXN).
 */
typedef void (*http_iop_cb_f)(http_iop_msg_t *msg, ic_status_t status,
                              opt_http_code_t http_code, void * nullable res,
                              void * nullable exn);

struct http_iop_msg_t {
    /** HTTP query used by the RPC call. */
    httpc_query_t query;

    /** IOP interface alias. */
    const iop_iface_alias_t *iface_alias;

    /** RPC called. */
    const iop_rpc_t *rpc;

    /** Query callback function. */
    http_iop_cb_f cb;

    /** RPC args, only set if the query needs to be retried. */
    void *args;

    /** Link to pending queries list. */
    htnode_t link;

    /** Timestamp at which the message was added to the channel. */
    struct timeval query_time;

    /** User defined data associated with the query. */
    byte priv[];
};

typedef struct http_iop_channel_t http_iop_channel_t;

typedef struct {
    /** The URL of this channel. */
    http_url_t url;

    /** Connection pool of this channel. */
    httpc_pool_t pool;

    /** Base path used by all query. */
    lstr_t base_path;

    /** The channel of the remote. */
    http_iop_channel_t *channel;
} http_iop_channel_remote_t;

qvector_t(http_iop_channel_remote, http_iop_channel_remote_t *);

/** Callback called when a connection to a remote cannot be established. */
typedef void (*on_connection_error_f)(http_iop_channel_remote_t *remote,
                                      int errnum);
/** Callback called when a connection of the channel is ready. */
typedef void (*on_ready_f)(http_iop_channel_t *);

struct http_iop_channel_t {
    lstr_t name;

    qv_t(http_iop_channel_remote) remotes;

    lstr_t user;
    lstr_t password;

    uint32_t response_max_size;
    bool     encode_url;

    on_connection_error_f on_connection_error_cb;
    on_ready_f            on_ready_cb;

    uint32_t connection_timeout_msec;
    el_t     queries_conn_timeout_el;

    void *priv;

    /** Channel queries waiting for connection.
     *
     * If no connection is available when a query is fired, queries will be
     * added to this list.
     * They will be retried as soon as a connection is ready.
     */
    htlist_t queries_waiting_conn;

#ifndef NDEBUG
    /* Guard for usage of HTTP IOP channel when the channel is being wiped. */
    bool wipe_guard;
#endif /* NDEBUG */
};

typedef struct http_iop_channel_cfg_t {
    /** Name of the http iop channel.
     *
     * This name is used for exploitability purposes only.
     */
    lstr_t name;

    /** The URLs of the remote.
     *
     * There must be at leat one URL. Others are used in a failover mode, in
     * their order of declaration.
     */
    iop_array_lstr_t urls;

    /** IOP HTTP client configuration. */
    const core__httpc_cfg__t * nonnull iop_cfg;

    /** Maximum number of allowed connections.
     *
     * Default is 1.
     */
    opt_u32_t max_connections;

    /** Maximum time allowed for a connection to etablish before a query is
     * automatically cancelled, in milliseconds.
     *
     * Default is 10 000 milliseconds, 10 seconds.
     *
     * Once the connection has been established, iop_cfg->noact_delay is used
     * to determine the inactivity timeout of the connection.
     */
    opt_u32_t connection_timeout_msec;

    /** Maximum query response size, in bytes.
     *
     * Default is 1 << 20.
     */
    opt_u32_t response_max_size;

    /** Whether the URL should be automatically encoded or not.
     *
     * Default is true.
     */
    opt_bool_t encode_url;

    /** User used for authentification.
     *
     * Leave empty to disable authentication.
     */
    lstr_t user;

    /** Password used for authentification.
     *
     * Must be set if \ref user is.
     */
    lstr_t password;

    /** On connection error callback. */
    on_connection_error_f nullable on_connection_error_cb;

    /** On connection ready callback. */
    on_ready_f nullable on_ready_cb;

    /** User defined data associated with this channel. */
    void * nullable priv;
} http_iop_channel_cfg_t;

/** Create an HTTP channel.
 *
 * \param[in]  channel       The channel to initialize.
 * \param[in]  url           The URL of the channel.
 * \param[in]  cfg           An HTTP client configuration.
 * \param[in]  max_conn      Maximum number of allowed connection.
 * \param[in]  query_timeout Maximum time in second before a query is
 *                           automatically cancelled.
 * \param[out] err           Error buffer.
 *
 * \return 0 on success, a negative value on error.
 */
http_iop_channel_t *http_iop_channel_create(const http_iop_channel_cfg_t *cfg,
                                            sb_t *err);

/** Delete an HTTP channel.
 *
 * \param[in] channel_ptr The pointer to the channel to delete.
 */
void http_iop_channel_delete(http_iop_channel_t **channel_ptr);

/** Close all the clients of an HTTP channel.
 *
 * \param[in] channel The channel.
 */
void http_iop_channel_close_clients(http_iop_channel_t *channel);

/* }}} */
/* {{{ Query */
/* {{{ Query struct */

http_iop_msg_t *http_iop_msg_new(int len);

#define http_iop_msg_p(_t, _v)                                               \
    ({                                                                       \
        http_iop_msg_t *_query = http_iop_msg_new(sizeof(_t));               \
                                                                             \
        *acast(_t, _query->priv) = *(_v);                                    \
        _query;                                                              \
    })
#define http_iop_msg(_t, ...)  http_iop_msg_p(_t, (&(_t){ __VA_ARGS__ }))

/* }}} */

/** Send a query through an IOP HTTP channel.
 *
 * This is the low level function, you should use http_iop_query_cb or
 * http_iop_query.
 *
 * \param[in]  channel    the #http_iop_channel_t to send the query to.
 * \param[in]  msg        the #http_query_msg_t to fill.
 * \param[in]  query_data the IOP query args.
 */
void http_iop_query_(http_iop_channel_t *channel, http_iop_msg_t *msg,
                     void *query_data);

#define IOP_RPC_S(_mod, _if, _rpc, what)  _mod##__##_if(_rpc##_##what##__s)

/** \brief builds the argument list of the HTTP reply callback of an rpc.
 *
 * \see http_iop_cb_f for the arguments.
 *
 * \param[in]  _mod name of the package+module of the RPC
 * \param[in]  _if  name of the interface of the RPC
 * \param[in]  _rpc name of the rpc
*/
#define IOP_HTTP_RPC_CB_ARGS(_mod, _if, _rpc)                                \
    http_iop_msg_t *msg, ic_status_t status, opt_http_code_t http_code,      \
    IOP_RPC_T(_mod, _if, _rpc, res) * nullable res,                          \
    IOP_RPC_T(_mod, _if, _rpc, exn) * nullable exn

/** \brief builds an HTTP RPC Callback prototype.
 *
 * \see http_iop_cb_f for the arguments.
 *
 * \param[in]  _mod name of the package+module of the RPC
 * \param[in]  _if  name of the interface of the RPC
 * \param[in]  _rpc name of the rpc
 */
#define IOP_HTTP_RPC_CB(_mod, _if, _rpc)                                     \
    IOP_RPC_NAME(_mod, _if, _rpc, cb)(IOP_HTTP_RPC_CB_ARGS(_mod, _if, _rpc))

/** \brief helper to send a query to a given http channel.
 *
 * \param[in]  _ic    the #http_iop_channel_t to send the query to.
 * \param[in]  _msg   the #http_query_msg_t to fill.
 * \param[in]  _cb    the rpc reply callback to use
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define http_iop_query_cb(_http_channel, _msg, _cb, _mod, _if, _rpc, ...)    \
    do {                                                                     \
        IOP_RPC_T(_mod, _if, _rpc, args)  _args;                             \
        http_iop_msg_t *__msg = (_msg);                                      \
        void (*__cb)(IOP_HTTP_RPC_CB_ARGS(_mod, _if, _rpc)) = (_cb);         \
                                                                             \
        __msg->cb = (http_iop_cb_f)__cb;                                     \
        __msg->iface_alias = _mod##__##_if##__alias;                         \
        __msg->rpc = IOP_RPC(_mod, _if, _rpc);                               \
        assert(__msg->rpc);                                                  \
                                                                             \
        _args = (IOP_RPC_T(_mod, _if, _rpc, args)){ __VA_ARGS__ };           \
        http_iop_query_((_http_channel), __msg, &_args);                     \
   } while (0)

/** \brief helper to send a query to a given http channel, computes callback name..
 *
 * \param[in]  _ic    the #http_iop_channel_t to send the query to.
 * \param[in]  _msg   the #http_query_msg_t to fill.
 * \param[in]  _mod   name of the package+module of the RPC
 * \param[in]  _if    name of the interface of the RPC
 * \param[in]  _rpc   name of the rpc
 * \param[in]  ...
 *   the initializers of the value on the form <tt>.field = value</tt>
 */
#define http_iop_query(_http_channel, _msg, _mod, _if, _rpc, ...)            \
    http_iop_query_cb((_http_channel), (_msg),                               \
                      IOP_RPC_NAME(_mod, _if, _rpc, cb), _mod,               \
                      _if, _rpc, __VA_ARGS__)

/* }}} */

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

/* }}} */

#endif
#endif
