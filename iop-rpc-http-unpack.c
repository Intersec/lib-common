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

#include "xmlr.h"
#include "iop-rpc.h"

void ichttp_cb_wipe(ichttp_cb_t *rpc)
{
    lstr_wipe(&rpc->name);
    lstr_wipe(&rpc->name_uri);
    lstr_wipe(&rpc->name_res);
    lstr_wipe(&rpc->name_exn);
}

static void ichttp_query_wipe(ichttp_query_t *q)
{
    ichttp_cb_delete(&q->cbe);
}

OBJ_VTABLE(ichttp_query)
    ichttp_query.wipe = ichttp_query_wipe;
OBJ_VTABLE_END()

static int t_parse_json(ichttp_query_t *iq, ichttp_cb_t *cbe, void **vout)
{
    httpd_trigger__ic_t *tcb = container_of(iq->trig_cb, httpd_trigger__ic_t, cb);
    const iop_struct_t  *st = cbe->fun->args;
    pstream_t      ps;
    iop_json_lex_t jll;
    int res = 0;
    SB_8k(buf);

    *vout = NULL;
    iop_jlex_init(t_pool(), &jll);
    ps = ps_initsb(&iq->payload);
    iop_jlex_attach(&jll, &ps);

    jll.flags = tcb->unpack_flags;

    if (iop_junpack_ptr(&jll, st, vout, true)) {
        sb_reset(&buf);
        iop_jlex_write_error(&jll, &buf);

        __ichttp_err_ctx_set(LSTR_SB_V(&buf));
        httpd_reject(obj_vcast(httpd_query, iq), BAD_REQUEST, "%s", buf.data);
        __ichttp_err_ctx_clear();
        res = -1;
        goto end;
    }
    iop_jlex_detach(&jll);

  end:
    iop_jlex_wipe(&jll);
    return res;
}

static int t_parse_soap(ichttp_query_t *iq,
                        ichttp_cb_t **cbout, void **vout)
{
#define xr  xmlr_g
#define XCHECK(expr)  XMLR_CHECK(expr, goto xmlerror)

    const char *buf = iq->payload.data;
    int         len = iq->payload.len;

    httpd_trigger__ic_t *tcb = container_of(iq->trig_cb, httpd_trigger__ic_t, cb);
    ichttp_cb_t *cbe;
    lstr_t s;
    int pos;

    /* Initialize the xmlReader object */
    XCHECK(xmlr_setup(&xr, buf, len));
    XCHECK(xmlr_node_open_s(xr, "Envelope"));
    if (XCHECK(xmlr_node_is_s(xr, "Header")))
        XCHECK(xmlr_next_sibling(xr));
    XCHECK(xmlr_node_open_s(xr, "Body"));
    XCHECK(xmlr_node_get_local_name(xr, &s));
    pos = qm_find(ichttp_cbs, &tcb->impl, &s);
    if (pos < 0) {
        __ichttp_reply_soap_err_cst(ichttp_query_to_slot(iq), false,
                                    "unknown rpc");
        goto error;
    }
    iq->cbe = *cbout = cbe = ichttp_cb_dup(tcb->impl.values[pos]);

    XCHECK(iop_xunpack_ptr_flags(xr, t_pool(), cbe->fun->args, vout,
                                 tcb->unpack_flags));
    /* Close opened elements */

    XCHECK(xmlr_node_close(xr)); /* </Body>     */
    XCHECK(xmlr_node_close(xr)); /* </Envelope> */
    xmlr_close(&xr);
    return 0;

  xmlerror:
    s = LSTR(xmlr_get_err() ?: "parsing error");
    __ichttp_reply_soap_err(ichttp_query_to_slot(iq), false, &s);
  error:
    xmlr_close(&xr);
    return -1;

#undef XCHECK
#undef xr
}

static int is_ctype_json(const httpd_qinfo_t *info)
{
    const http_qhdr_t *ctype;

    ctype = http_qhdr_find(info->hdrs, info->hdrs_len,
                           HTTP_WKHDR_CONTENT_TYPE);
    if (ctype) {
        pstream_t v = ctype->val;

        ps_skipspaces(&v);
        return ps_skipcasestr(&v, "application/json") == 0;
    }
    return false;
}

int __t_ichttp_query_on_done_stage1(httpd_query_t *q, ichttp_cb_t **cbe,
                                    void **value, bool *soap)
{
    ichttp_query_t      *iq  = obj_vcast(ichttp_query, q);
    httpd_trigger__ic_t *tcb = container_of(iq->trig_cb, httpd_trigger__ic_t, cb);
    pstream_t            url = iq->qinfo->query;
    int                  res;

    *soap = false;
    ps_skipstr(&url, "/");

    if (ps_len(&url)) {
        const char *url_s = url.s;
        lstr_t s;

        if (ps_skip_uptochr(&url, '/') < 0) {
          not_found:
            httpd_reject(obj_vcast(httpd_query, iq), NOT_FOUND, "");
            return -1;
        }
        __ps_skip(&url, 1);
        if (ps_skip_uptochr(&url, '/') < 0) {
            s = LSTR_INIT_V(url_s, url.s_end - url_s);
        } else {
            s = LSTR_INIT_V(url_s, url.s - url_s);
        }
        res = qm_find(ichttp_cbs, &tcb->impl, &s);
        if (res < 0)
            goto not_found;
        iq->cbe = *cbe = ichttp_cb_dup(tcb->impl.values[res]);

        if (is_ctype_json(q->qinfo)) {
            iq->json = true;
            res = t_parse_json(iq, *cbe, value);
        } else {
            httpd_reject(obj_vcast(httpd_query, iq), NOT_ACCEPTABLE,
                         "Content-Type must be application/json");
            return -1;
        }
    } else {
        *soap = true;
        return 0;
    }
    if (unlikely(res < 0))
        return -1;
    return 0;
}

void __t_ichttp_query_on_done_stage2(httpd_query_t *q, ichttp_cb_t *cbe,
                                     void *value)
{
    ichttp_query_t      *iq  = obj_vcast(ichttp_query, q);
    httpd_trigger__ic_t *tcb = container_of(iq->trig_cb, httpd_trigger__ic_t,
                                            cb);
    ic__hdr__t   default_hdr = IOP_UNION_VA(ic__hdr, simple,
       .kind = LSTR_OPT(tcb->auth_kind),
       .payload = q->received_body_length,
       .source = LSTR("webservice"),
       .workspace_id = OPT_NONE,
    );
    ic_cb_entry_t       *e;

    pstream_t   login, pw;
    uint64_t    slot = ichttp_query_to_slot(iq);
    ichannel_t *pxy;
    ic__hdr__t *pxy_hdr = NULL;
    bool force_pxy_hdr = false;
    ic__hdr__t          *hdr;
    ichttp_query_t *ic_q = obj_vcast(ichttp_query, q);
    ic_msg_t *msg;

    if (ic_q && ic_q->ic_hdr) {
        hdr = ic_q->ic_hdr;
    } else {
        hdr = &default_hdr;

        if (t_httpd_qinfo_get_basic_auth(q->qinfo, &login, &pw) == 0) {
            hdr->simple.login    = LSTR_PS_V(&login);
            hdr->simple.password = LSTR_PS_V(&pw);
        }
        hdr->simple.host = httpd_get_peer_address(q->owner);
    }

    e = &cbe->e;
    if (ic_query_do_pre_hook(NULL, slot, e, hdr) < 0) {
        return;
    }

    switch (e->cb_type) {
      case IC_CB_NORMAL:
      case IC_CB_WS_SHARED:
        t_seal();

        if (e->cb_type == IC_CB_NORMAL_BLK) {
            assert (false);
        } else {
            (*e->u.iws_cb.cb)(NULL, slot, value, hdr);
        }
        if (cbe->fun->async)
            httpd_reply_202accepted(q);

        t_unseal();
        return;

      case IC_CB_PROXY_P:
        pxy     = e->u.proxy_p.ic_p;
        pxy_hdr = e->u.proxy_p.hdr_p;
        break;
      case IC_CB_PROXY_PP:
        pxy     = *e->u.proxy_pp.ic_pp;
        if (e->u.proxy_pp.hdr_pp)
            pxy_hdr = *e->u.proxy_pp.hdr_pp;
        break;
      case IC_CB_DYNAMIC_PROXY:
        {
            ic_dynproxy_t dynproxy;

            /* XXX dynproxy are allowed to return memory allocated on the
             * t_pool() and thus mustn't be wrapped in a local t_scope */
            dynproxy = (*e->u.dynproxy.get_ic)(hdr, e->u.dynproxy.priv);
            pxy      = dynproxy.ic;
            pxy_hdr  = dynproxy.hdr;
            force_pxy_hdr = pxy_hdr != NULL;
        }
        break;
      default:
        e_panic("should not happen");
        break;
    }

    if (unlikely(!pxy)) {
        __ichttp_reply_err(slot, IC_MSG_PROXY_ERROR, NULL);
        return;
    }

    msg = ic_msg_new(sizeof(uint64_t));

    if ((!ps_len(&login) || force_pxy_hdr) && pxy_hdr) {
        /* XXX on simple header we write the payload size of the HTTP
         * query */
        if (unlikely(IOP_UNION_IS(ic__hdr, pxy_hdr, simple))) {
            ic__simple_hdr__t *shdr = &pxy_hdr->simple;
            shdr->payload = iq->payload.len;
        }

        msg->hdr = pxy_hdr;
    } else {
        assert(!pxy_hdr);
        /* XXX We do not support header replacement with proxyfication */
        msg->hdr = hdr;
    }
    msg->cmd = cbe->cmd;
    msg->rpc = cbe->fun;
    msg->async = cbe->fun->async;

    if (!msg->async) {
        msg->cb = IC_PROXY_MAGIC_CB;
        put_unaligned_cpu64(msg->priv, slot);
    }
    __ic_bpack(msg, cbe->fun->args, value);
    __ic_query(pxy, msg);
    if (msg->async) {
        httpd_reply_202accepted(q);
    }
}

static void ichttp_query_on_done(httpd_query_t *q)
{
    t_scope;

    ichttp_query_t *iq = obj_vcast(ichttp_query, q);

    int             res;
    bool            soap = false;
    ichttp_cb_t    *cbe = NULL;
    void           *value = NULL;

    res = __t_ichttp_query_on_done_stage1(q, &cbe, &value, &soap);
    if (unlikely(res < 0))
        return;
    if (soap) {
        res = t_parse_soap(iq, &cbe, &value);
        if (unlikely(res < 0))
            return;
    }
    __t_ichttp_query_on_done_stage2(q, cbe, value);
}

static void httpd_trigger__ic_destroy(httpd_trigger_t *tcb)
{
    httpd_trigger__ic_t *cb = container_of(tcb, httpd_trigger__ic_t, cb);

    qm_deep_wipe(ichttp_cbs, &cb->impl, IGNORE, ichttp_cb_delete);
    p_delete(&cb);
}

static void httpd_trigger__ic_cb(httpd_trigger_t *tcb, httpd_query_t *q,
                                 const httpd_qinfo_t *req)
{
    httpd_trigger__ic_t *cb = container_of(tcb, httpd_trigger__ic_t, cb);

    q->on_done = ichttp_query_on_done;
    q->qinfo   = httpd_qinfo_dup(req);
    httpd_bufferize(q, cb->query_max_size);
}


httpd_trigger__ic_t *
httpd_trigger__ic_new(const iop_mod_t *mod, const char *schema,
                      unsigned szmax)
{
    httpd_trigger__ic_t *cb = p_new(httpd_trigger__ic_t, 1);

    cb->cb.cb          = &httpd_trigger__ic_cb;
    cb->cb.query_cls   = obj_class(ichttp_query);
    cb->cb.destroy     = &httpd_trigger__ic_destroy;
    cb->schema         = schema;
    cb->mod            = mod->ifaces;
    cb->query_max_size = szmax;
    cb->jpack_flags    = IOP_JPACK_NO_WHITESPACES;
    cb->xpack_flags    = IOP_XPACK_LITERAL_ENUMS;
    qm_init_cached(ichttp_cbs, &cb->impl);
    return cb;
}

static ichttp_cb_t *
ichttp_register_function(httpd_trigger__ic_t *tcb,
                         const iop_iface_alias_t *alias, const iop_rpc_t *fun,
                         int32_t cmd, const ic_cb_entry_t *_cb)
{
    ichttp_cb_t *cb = ichttp_cb_new();

    cb->cmd      = cmd;
    cb->fun      = fun;
    cb->name     = lstr_fmt("%s.%sReq",    alias->name.s, fun->name.s);
    cb->name_uri = lstr_fmt("%s/%s",       alias->name.s, fun->name.s);
    cb->name_res = lstr_fmt("%s.%sRes",    alias->name.s, fun->name.s);
    cb->name_exn = lstr_fmt("%s.%s.Fault", alias->name.s, fun->name.s);
    cb->e = *_cb;

    /* Register RPC name (takes ownership of "cb") */
    e_assert_n(panic, qm_add(ichttp_cbs, &tcb->impl, &cb->name, cb),
               "RPC %s.%s", alias->name.s, fun->name.s);
    /* Register RPC URI (duplicates "cb") */
    e_assert_n(panic, qm_add(ichttp_cbs, &tcb->impl, &cb->name_uri,
                             ichttp_cb_dup(cb)),
               "RPC %s.%s", alias->name.s, fun->name.s);

    return cb;
}

ichttp_cb_t *
__ichttp_register(httpd_trigger__ic_t *tcb, const iop_iface_alias_t *alias,
                  const iop_rpc_t *fun, int32_t cmd, const ic_cb_entry_t *cb)
{
    const unsigned fun_flags = fun->flags;

    if (TST_BIT(&fun_flags, IOP_RPC_HAS_ALIAS)) {
        const iop_rpc_attrs_t *attrs = iop_rpc_get_attrs(alias->iface, fun);

        for (int i = 0; i < attrs->attrs_len; i++) {
            const iop_rpc_attr_t attr = attrs->attrs[i];

            if (attr.type == IOP_RPC_ALIAS) {
                const iop_rpc_attr_arg_t arg = attr.args[0];
                const iop_rpc_t *fun_alias = (const iop_rpc_t *)arg.v.p;

                /* The alias callback will never leave the trigger hashtable
                 * and will be destroyed with it. */
                ichttp_register_function(tcb, alias, fun_alias, cmd, cb);
            }
        }
    }

    return ichttp_register_function(tcb, alias, fun, cmd, cb);
}
