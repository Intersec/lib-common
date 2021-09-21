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

#if !defined(IS_LIB_COMMON_IOP_RPC_H) || defined(IS_LIB_COMMON_IOP_RPC_HTTP_H)
#  error "you must include <lib-common/iop-rpc.h> instead"
#else
#define IS_LIB_COMMON_IOP_RPC_HTTP_H
#ifndef __cplusplus

#include <lib-common/iop-json.h>

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

#endif
#endif
