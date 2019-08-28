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

static int identity(tpl_t *out, sb_t *b, tpl_t **arr, int nb)
{
    if (out) {
        tpl_add_tpls(out, arr, nb);
        return 0;
    }
    while (--nb >= 0) {
        tpl_t *in = *arr++;
        if (in->op == TPL_OP_BLOB) {
            sb_addsb(b, &in->u.blob);
        } else {
            assert (in->op == TPL_OP_DATA);
            sb_add(b, in->u.data.data, in->u.data.len);
        }
    }
    return 0;
}

static int tst_seq(tpl_t *out, sb_t *blob, tpl_t **arr, int nb)
{
    const char *data1, *data2, *data3;
    int len1, len2, len3;
    tpl_t *in;

    if (!blob) {
        assert(out);
        blob = tpl_get_blob(out);
    }

    assert (nb == 3);

    in = *arr++;
    if (in->op == TPL_OP_BLOB) {
        data1 = in->u.blob.data;
        len1  = in->u.blob.len;
    } else {
        assert (in->op == TPL_OP_DATA);
        data1 = (char *)in->u.data.data;
        len1  = in->u.data.len;
    }
    in = *arr++;
    if (in->op == TPL_OP_BLOB) {
        data2 = in->u.blob.data;
        len2  = in->u.blob.len;
    } else {
        assert (in->op == TPL_OP_DATA);
        data2 = (char *)in->u.data.data;
        len2  = in->u.data.len;
    }
    in = *arr++;
    if (in->op == TPL_OP_BLOB) {
        data3 = in->u.blob.data;
        len3  = in->u.blob.len;
    } else {
        assert (in->op == TPL_OP_DATA);
        data3 = (char *)in->u.data.data;
        len3  = in->u.data.len;
    }

    sb_addf(blob, "1: %*pM, 2: %*pM, 3: %*pM",
            len1, data1, len2, data2, len3, data3);
    return 0;
}

int main(int argc, const char **argv)
{
    tpl_t *tpl, *fun, *res, *var;
    sb_t blob, b2;

    e_trace(0, "sizeof(tpl_t) = %zd", sizeof(tpl_t));
    sb_init(&blob);
    sb_init(&b2);
    sb_addnc(&blob, 4096, ' ');

    var = tpl_new();
    tpl_add_cstr(var, "var");
    tpl_add_data(var, blob.data, blob.len);
    tpl_dump(0, var, "var");

    tpl = tpl_new();
    tpl_add_cstr(tpl, "asdalskdjalskdjalskdjasldkjasdfoo");
    tpl_add_cstr(tpl, "foo");
    tpl_add_cstr(tpl, "foo");
    tpl_add_cstr(tpl, "foo");
    tpl_add_var(tpl, 0, 0);
    fun = tpl_add_apply(tpl, TPL_OP_APPLY, &identity);
    tpl_add_var(fun, 0, 0);
    tpl_copy_cstr(fun, "foo");
    tpl_copy_cstr(fun, "foo");
    tpl_copy_cstr(tpl, "foo");
    tpl_copy_cstr(tpl, "foo");
    tpl_add_tpl(tpl, var);
    tpl_dump(0, tpl, "source");

    res = tpl_dup(tpl);
    tpl_subst(&res, 1, NULL, 0, true);
    tpl_dump(0, res, "subst");
    tpl_delete(&res);

    res = tpl_dup(tpl);
    tpl_subst(&res, 0, &var, 1, TPL_LASTSUBST | TPL_KEEPVAR);
    tpl_dump(0, res, "subst");
    tpl_optimize(res);
    tpl_dump(0, res, "subst (opt)");
    tpl_delete(&res);

    if (tpl_fold(&b2, &tpl, 0, &var, 1, TPL_LASTSUBST)) {
        e_panic("fold failed");
    }
    assert(tpl == NULL);
    e_trace(0, "b2 size: %d", b2.len);

    sb_wipe(&blob);
    sb_wipe(&b2);

    tpl = tpl_new();
    tpl_add_cstr(tpl, "foo|");
    fun = tpl_add_apply(tpl, TPL_OP_APPLY_SEQ, &tst_seq);
    tpl_add_cstr(fun, "toto");
    res = tpl_new();
    tpl_add_cstr(res, "ta");
    tpl_add_cstr(res, "ta");
    tpl_add_tpl(fun, res);
    tpl_delete(&res);
    tpl_add_cstr(fun, "titi");

    tpl_dump(0, tpl, "apply seq");
    tpl_optimize(tpl);
    tpl_dump(0, tpl, "apply seq (opt)");
    sb_init(&blob);
    if (tpl_fold(&blob, &tpl, 0, NULL, 0, TPL_LASTSUBST)) {
        e_panic("fold failed");
    }
    assert(tpl == NULL);
    e_trace(0, "apply seq res: %s", blob.data);
    sb_wipe(&blob);

    return 0;
}
