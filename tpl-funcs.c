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

#include "tpl.h"

int tpl_compute_len_copy(sb_t *b, tpl_t **args, int nb, int len)
{
    if (b) {
        while (--nb >= 0) {
            tpl_t *in = *args++;

            if (in->op == TPL_OP_BLOB) {
                sb_addsb(b, &in->u.blob);
                len += in->u.blob.len;
            } else {
                assert (in->op == TPL_OP_DATA);
                sb_add(b, in->u.data.data, in->u.data.len);
                len += in->u.data.len;
            }
        }
    } else {
        while (--nb >= 0) {
            tpl_t *in = *args++;

            if (in->op == TPL_OP_BLOB) {
                len += in->u.blob.len;
            } else {
                assert (in->op == TPL_OP_DATA);
                len += in->u.data.len;
            }
        }
    }
    return len;
}

/****************************************************************************/
/* Short formats                                                            */
/****************************************************************************/


/****************************************************************************/
/* Escapings                                                                */
/****************************************************************************/

int tpl_encode_xml(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *in = *args++;
        if (in->op == TPL_OP_DATA) {
            sb_add_xmlescape(blob, in->u.data.data, in->u.data.len);
        } else {
            assert (in->op == TPL_OP_BLOB);
            sb_add_xmlescape(blob, in->u.blob.data, in->u.blob.len);
        }
    }
    return 0;
}

int tpl_encode_url(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *in = *args++;
        if (in->op == TPL_OP_DATA) {
            sb_add_urlencode(blob, in->u.data.data, in->u.data.len);
        } else {
            assert (in->op == TPL_OP_BLOB);
            sb_add_urlencode(blob, in->u.blob.data, in->u.blob.len);
        }
    }
    return 0;
}

int tpl_encode_latin1(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    int res = 0;

    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *in = *args++;
        if (in->op == TPL_OP_DATA) {
            res |= sb_conv_to_latin1(blob, in->u.data.data, in->u.data.len, '.');
        } else {
            assert (in->op == TPL_OP_BLOB);
            res |= sb_conv_to_latin1(blob, in->u.blob.data, in->u.blob.len, '.');
        }
    }
    return res;
}

int tpl_encode_ira(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *arg = *args++;
        if (arg->op == TPL_OP_DATA) {
            sb_conv_to_gsm_hex(blob, arg->u.data.data, arg->u.data.len);
        } else {
            assert (arg->op == TPL_OP_BLOB);
            sb_conv_to_gsm_hex(blob, arg->u.blob.data, arg->u.blob.len);
        }
    }
    return 0;
}

int tpl_encode_ira_bin(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *arg = *args++;
        if (arg->op == TPL_OP_DATA) {
            sb_conv_to_gsm(blob, arg->u.data.data, arg->u.data.len);
        } else {
            assert (arg->op == TPL_OP_BLOB);
            sb_conv_to_gsm(blob, arg->u.blob.data, arg->u.blob.len);
        }
    }
    return 0;
}

int tpl_encode_ucs2be(tpl_t *out, sb_t *sb, tpl_t **args, int nb)
{
    if (!sb) {
        assert(out);
        sb = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *arg = *args++;
        if (arg->op == TPL_OP_DATA) {
            sb_conv_to_ucs2be(sb, arg->u.data.data, arg->u.data.len);
        } else {
            assert (arg->op == TPL_OP_BLOB);
            sb_conv_to_ucs2be(sb, arg->u.blob.data, arg->u.blob.len);
        }
    }
    return 0;
}

int tpl_encode_ucs2be_hex(tpl_t *out, sb_t *sb, tpl_t **args, int nb)
{
    if (!sb) {
        assert(out);
        sb = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *arg = *args++;
        if (arg->op == TPL_OP_DATA) {
            sb_conv_to_ucs2be_hex(sb, arg->u.data.data, arg->u.data.len);
        } else {
            assert (arg->op == TPL_OP_BLOB);
            sb_conv_to_ucs2be_hex(sb, arg->u.blob.data, arg->u.blob.len);
        }
    }
    return 0;
}

int tpl_encode_base64(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    sb_b64_ctx_t ctx;

    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    sb_add_b64_start(blob, 0, 0, &ctx);

    while (--nb >= 0) {
        tpl_t *arg = *args++;
        if (arg->op == TPL_OP_DATA) {
            sb_add_b64_update(blob, arg->u.data.data, arg->u.data.len, &ctx);
        } else {
            assert (arg->op == TPL_OP_BLOB);
            sb_add_b64_update(blob, arg->u.blob.data, arg->u.blob.len, &ctx);
        }
    }
    sb_add_b64_finish(blob, &ctx);

    return 0;
}

int tpl_encode_qp(tpl_t *out, sb_t *blob, tpl_t **args, int nb)
{
    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    while (--nb >= 0) {
        tpl_t *arg = *args++;
        if (arg->op == TPL_OP_DATA) {
            sb_add_qpe(blob, arg->u.data.data, arg->u.data.len);
        } else {
            assert (arg->op == TPL_OP_BLOB);
            sb_add_qpe(blob, arg->u.blob.data, arg->u.blob.len);
        }
    }
    return 0;
}
