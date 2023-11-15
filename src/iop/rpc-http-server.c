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

#include <lib-common/xmlr.h>
#include <lib-common/xmlpp.h>
#include <lib-common/iop-rpc.h>

/* {{{ RPC-HTTP UNPACK */

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
    iq->cbe = *cbout = cbe = ichttp_cb_retain(tcb->impl.values[pos]);

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
        iq->cbe = *cbe = ichttp_cb_retain(tcb->impl.values[res]);

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
    bool hdr_modified = false;
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
    if (ic_query_do_pre_hook(NULL, slot, e, hdr, &hdr_modified) < 0) {
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

    if (hdr_modified && expect(hdr)) {
        pxy_hdr = force_pxy_hdr ? pxy_hdr : hdr;
        force_pxy_hdr = true;
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
                             ichttp_cb_retain(cb)),
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

/* }}} */
/* {{{ RPC-HTTP PACK */

static __thread lstr_t err_ctx_g = LSTR_NULL;

lstr_t ichttp_err_ctx_get(void)
{
    return err_ctx_g;
}

void __ichttp_err_ctx_set(lstr_t err_ctx)
{
    assert (err_ctx_g.s == NULL);
    err_ctx_g = err_ctx;
}

void __ichttp_err_ctx_clear(void)
{
    err_ctx_g = LSTR_NULL_V;
}

static void ichttp_serialize_soap(sb_t *sb, ichttp_query_t *iq, int cmd,
                                  const iop_struct_t *st, const void *v)
{
    xmlpp_t pp;
    httpd_trigger__ic_t *tcb;

    tcb = container_of(iq->trig_cb, httpd_trigger__ic_t, cb);

    xmlpp_open_banner(&pp, sb);
    pp.nospace = true;
    xmlpp_opentag(&pp, "s:Envelope");
    xmlpp_putattr(&pp, "xmlns:s", "http://schemas.xmlsoap.org/soap/envelope/");
    xmlpp_putattr(&pp, "xmlns:n", tcb->schema);
    xmlpp_putattr(&pp, "xmlns:xsi",
                  "http://www.w3.org/2001/XMLSchema-instance");

    xmlpp_opentag(&pp, "s:Body");
    if (cmd == IC_MSG_OK) {
        ichttp_cb_t *cbe = iq->cbe;

        if (v) {
            if (iop_struct_is_class(st)) {
                const iop_struct_t *real_st = *(const iop_struct_t **)v;

                sb_addf(sb, "<n:%*pM xsi:type=\"n:%*pM\">",
                        LSTR_FMT_ARG(cbe->name_res),
                        LSTR_FMT_ARG(real_st->fullname));
            } else {
                sb_addf(sb, "<n:%*pM>", LSTR_FMT_ARG(cbe->name_res));
            }
            iop_xpack_flags(sb, st, v, tcb->xpack_flags);
            sb_addf(sb, "</n:%*pM>", LSTR_FMT_ARG(cbe->name_res));
        } else {
            sb_addf(sb, "<n:%*pM />", LSTR_FMT_ARG(cbe->name_res));
        }
    } else {
        ichttp_cb_t *cbe = iq->cbe;

        xmlpp_opentag(&pp, "s:Fault");
        xmlpp_opentag(&pp, "faultcode");
        xmlpp_puts(&pp,    "s:Server");
        xmlpp_opensib(&pp, "faultstring");
        xmlpp_opensib(&pp, "detail");

        /* FIXME handle union of exceptions which are an array of exceptions */
        if (v) {
            if (iop_struct_is_class(st)) {
                const iop_struct_t *real_st = *(const iop_struct_t **)v;

                sb_addf(sb, "<n:%*pM xsi:type=\"n:%*pM\">",
                        LSTR_FMT_ARG(cbe->name_exn),
                        LSTR_FMT_ARG(real_st->fullname));
            } else {
                sb_addf(sb, "<n:%*pM>", LSTR_FMT_ARG(cbe->name_exn));
            }
            iop_xpack_flags(sb, st, v, tcb->xpack_flags);
            sb_addf(sb, "</n:%*pM>", LSTR_FMT_ARG(cbe->name_exn));
        } else {
            sb_addf(sb, "<n:%*pM />", LSTR_FMT_ARG(cbe->name_exn));
        }
    }
    pp.can_do_attr = false;
    xmlpp_close(&pp);
    iq->iop_answered = true;
}

void
__ichttp_reply(uint64_t slot, int cmd, const iop_struct_t *st, const void *v)
{
    ichttp_query_t *iq = ichttp_slot_to_query(slot);
    httpd_query_t  *q  = obj_vcast(httpd_query, iq);
    httpd_trigger__ic_t *tcb;
    outbuf_t *ob;
    sb_t *out;
    int oldlen, gzenc, is_gzip;
    http_code_t code;
    size_t oblen;

    ic_query_do_post_hook(NULL, cmd, slot, st, v);
    gzenc = httpd_qinfo_accept_enc_get(q->qinfo);
    tcb = container_of(iq->trig_cb, httpd_trigger__ic_t, cb);

    switch (cmd) {
      case IC_MSG_OK:
        code = HTTP_CODE_OK;
        break;

      default:
        assert (false);
        /* FALLTHROUGH */

      case IC_MSG_EXN:
        /* Use INTERNAL_SERVER_ERROR for exceptions:
         *  - in SOAP, this is the error code that must always be used.
         *  - in REST, use it to allow clients to distinguish exceptions
         *    (other error cases must not return INTERNAL_SERVER_ERROR).
         */
        code = HTTP_CODE_INTERNAL_SERVER_ERROR;
        if (tcb->cb.on_query_exn) {
            tcb->cb.on_query_exn(q, st, v, &code);
        }
        break;
    }

    ob = httpd_reply_hdrs_start(q, code, true);


    if (iq->json) {
        ob_adds(ob, "Content-Type: application/json; charset=utf-8\r\n");
    } else {
        ob_adds(ob, "Content-Type: text/xml; charset=utf-8\r\n");
    }
    if (gzenc & HTTPD_ACCEPT_ENC_GZIP) {
        ob_adds(ob, "Content-Encoding: gzip\r\n");
        is_gzip = true;
    } else
    if (gzenc & HTTPD_ACCEPT_ENC_DEFLATE) {
        ob_adds(ob, "Content-Encoding: deflate\r\n");
        is_gzip = false;
    } else {
        /* Ignore compress we don't support it */
        gzenc = 0;
    }
    httpd_reply_hdrs_done(q, -1, false);
    oblen = ob->length;

    out = outbuf_sb_start(ob, &oldlen);

    if (gzenc) {
        t_scope;
        sb_t buf;

        t_sb_init(&buf, BUFSIZ);
        if (iq->json) {
            iop_jpack(st, v, iop_sb_write, &buf, tcb->jpack_flags);
            iq->iop_answered = true;
        } else {
            ichttp_serialize_soap(&buf, iq, cmd, st, v);
        }
        sb_add_compressed(out, buf.data, buf.len, Z_BEST_COMPRESSION, is_gzip);
    } else
    if (iq->json) {
        iop_jpack(st, v, iop_sb_write, out, tcb->jpack_flags);
        iq->iop_answered = true;
    } else {
        ichttp_serialize_soap(out, iq, cmd, st, v);
    }
    outbuf_sb_end(ob, oldlen);

    oblen = ob->length - oblen;
    if (tcb->on_reply)
        (*tcb->on_reply)(tcb, iq, oblen, code);
    httpd_reply_done(q);
}

void __ichttp_reply_soap_err(uint64_t slot, bool serverfault, const lstr_t *err)
{
    ichttp_query_t *iq = ichttp_slot_to_query(slot);
    httpd_query_t  *q  = obj_vcast(httpd_query, iq);
    httpd_trigger__ic_t *tcb;
    outbuf_t *ob;
    sb_t *out;
    int oldlen;
    size_t oblen;
    xmlpp_t pp;

    assert (!iq->json);

    /* SOAP specifies that failing queries must return error code
     * INTERNAL_SERVER_ERROR. */
    __ichttp_err_ctx_set(*err);
    ob = httpd_reply_hdrs_start(q, HTTP_CODE_INTERNAL_SERVER_ERROR, true);
    ob_adds(ob, "Content-Type: text/xml; charset=utf-8\r\n");
    httpd_reply_hdrs_done(q, -1, false);
    oblen = ob->length;

    out = outbuf_sb_start(ob, &oldlen);
    tcb = container_of(iq->trig_cb, ichttp_trigger_cb_t, cb);

    xmlpp_open_banner(&pp, out);
    pp.nospace = true;
    xmlpp_opentag(&pp, "s:Envelope");
    xmlpp_putattr(&pp, "xmlns:s",
                  "http://schemas.xmlsoap.org/soap/envelope/");

    xmlpp_opentag(&pp, "s:Body");
    xmlpp_opentag(&pp, "s:Fault");
    xmlpp_opentag(&pp, "s:faultcode");
    if (serverfault) {
        xmlpp_puts(&pp,"s:Server");
    } else {
        xmlpp_puts(&pp,"s:Client");
    }
    xmlpp_opensib(&pp, "s:faultstring");
    xmlpp_put(&pp, err->s, err->len);
    xmlpp_close(&pp);
    outbuf_sb_end(ob, oldlen);

    oblen = ob->length - oblen;
    if (tcb->on_reply)
        (*tcb->on_reply)(tcb, iq, oblen, HTTP_CODE_INTERNAL_SERVER_ERROR);
    httpd_reply_done(q);
    __ichttp_err_ctx_clear();
}

__attr_printf__(4, 5)
static void __ichttp_reject(uint64_t slot, ichttp_query_t *iq,
                            http_code_t rest_code, const char *fmt, ...)
{
    SB_1k(err);
    va_list ap;

    va_start(ap, fmt);
    sb_addvf(&err, fmt, ap);
    va_end(ap);

    if (iq->json) {
        /* In REST, INTERNAL_SERVER_ERROR is reserved for IOP exceptions
         * (cf. __ichttp_reply). */
        assert (rest_code != HTTP_CODE_INTERNAL_SERVER_ERROR);
        __ichttp_err_ctx_set(LSTR_SB_V(&err));
        httpd_reject_(obj_vcast(httpd_query, iq), rest_code,
                      "%*pM", SB_FMT_ARG(&err));
        __ichttp_err_ctx_clear();
    } else {
        /* SOAP always throws INTERNAL_SERVER_ERROR. */
        __ichttp_reply_soap_err(slot, true, &LSTR_SB_V(&err));
    }
}

void __ichttp_reply_err(uint64_t slot, int err, const lstr_t *err_str)
{
    ichttp_query_t *iq = ichttp_slot_to_query(slot);

    ic_query_do_post_hook(NULL, err, slot, NULL, NULL);

    switch (err) {
      case IC_MSG_OK:
      case IC_MSG_EXN:
        e_panic("should not happen");
      case IC_MSG_RETRY:
      case IC_MSG_ABORT:
      case IC_MSG_PROXY_ERROR:
        __ichttp_reject(slot, iq, HTTP_CODE_BAD_REQUEST,
                        "query temporary refused");
        break;
      case IC_MSG_INVALID:
      case IC_MSG_SERVER_ERROR:
        if (err_str && err_str->len) {
            __ichttp_reject(slot, iq, HTTP_CODE_BAD_REQUEST,
                            "%*pM", LSTR_FMT_ARG(*err_str));
        } else {
            __ichttp_reject(slot, iq, HTTP_CODE_BAD_REQUEST,
                            "query refused by server");
        }
        break;
      case IC_MSG_UNIMPLEMENTED:
        __ichttp_reject(slot, iq, HTTP_CODE_NOT_FOUND,
                        "query not implemented by server");
        break;
    }
}

void __ichttp_proxify(uint64_t slot, int cmd, const void *data, int dlen)
{
    ichttp_query_t  *iq  = ichttp_slot_to_query(slot);
    const iop_rpc_t *rpc = iq->cbe->fun;
    const iop_struct_t *st;
    pstream_t ps;
    void *v;

    iq->iop_res_size = IC_MSG_HDR_LEN + dlen;
    switch (cmd) {
      case IC_MSG_OK:
        st = rpc->result;
        break;
      case IC_MSG_EXN:
        st = rpc->exn;
        break;
      default:
        __ichttp_reply_err(slot, cmd, &LSTR_INIT_V(data, dlen));
        return;
    }

    {
        t_scope;

        v  = t_new_raw(char, st->size);
        ps = ps_init(data, dlen);
        if (unlikely(iop_bunpack(t_pool(), st, v, ps, false) < 0)) {
            lstr_t err_str = iop_get_err_lstr();
#ifndef NDEBUG
            if (!err_str.s)
                e_trace(0, "%s: answer with invalid encoding", rpc->name.s);
#endif
            __ichttp_reply_err(slot, IC_MSG_INVALID, &err_str);
        } else {
            __ichttp_reply(slot, cmd, st, v);
        }
    }
}

void __ichttp_forward_reply(ichannel_t *pxy_ic, uint64_t slot, int cmd,
                            const void *res, const void *exn)
{
    ichttp_query_t  *iq  = ichttp_slot_to_query(slot);
    const iop_rpc_t *rpc = iq->cbe->fun;
    const iop_struct_t *st;
    const void *v = (cmd == IC_MSG_OK) ? res : exn;
    sb_t *buf  = &pxy_ic->rbuf;
    int   dlen = get_unaligned_cpu32(buf->data + IC_MSG_DLEN_OFFSET);

    iq->iop_res_size = IC_MSG_HDR_LEN;
    switch (cmd) {
      case IC_MSG_OK:
        iq->iop_res_size += dlen;
        st = rpc->result;
        break;
      case IC_MSG_EXN:
        iq->iop_res_size += dlen;
        st = rpc->exn;
        break;
      default:
        __ichttp_reply_err(slot, cmd, exn);
        return;
    }

    __ichttp_reply(slot, cmd, st, v);
}

/* }}} */
