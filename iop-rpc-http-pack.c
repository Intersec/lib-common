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

#include "xmlpp.h"
#include "iop-rpc.h"

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
    tcb = container_of(iq->trig_cb, httpd_trigger__ic_t, cb);

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
