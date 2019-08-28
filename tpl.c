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

/** \addtogroup templates
 * \{
 */

/** \file tpl.c
 * \brief Templating module implementation.
 */

tpl_t *tpl_new_op(tpl_op op)
{
    tpl_t *n  = p_new(tpl_t, 1);
    n->op     = op;
    n->refcnt = 1;
    if (op == TPL_OP_BLOB)
        sb_init(&n->u.blob);
    if (op & TPL_OP_BLOCK)
        qv_init(&n->u.blocks);

    n->is_const = (op == TPL_OP_BLOB) || (op == TPL_OP_DATA);
    return n;
}

tpl_t *tpl_new_var(uint16_t array, uint16_t index)
{
    tpl_t *var = tpl_new_op(TPL_OP_VAR);
    var->u.varidx = ((uint32_t)array << 16) | index;
    return var;
}

tpl_t *tpl_dup(const tpl_t *tpl)
{
    tpl_t *res = (tpl_t *)tpl;

    /* tpl can be NULL (the NULL template is a real value) */
    if (res) {
        res->refcnt++;
    }
    return res;
}

static void tpl_wipe(tpl_t *n)
{
    if (n->op == TPL_OP_BLOB)
        sb_wipe(&n->u.blob);
    if (n->op & TPL_OP_BLOCK) {
        tab_for_each_pos_safe(pos, &n->u.blocks) {
            tpl_delete(&n->u.blocks.tab[pos]);
        }
        qv_wipe(&n->u.blocks);
    }
}

void tpl_delete(tpl_t **tpl)
{
    if (*tpl) {
        if (--(*tpl)->refcnt > 0) {
            *tpl = NULL;
        } else {
            tpl_wipe(*tpl);
            p_delete(tpl);
        }
    }
}

/****************************************************************************/
/* Build the AST                                                            */
/****************************************************************************/

#ifndef __doxygen_mode__
#define tpl_can_append(t)  \
        (  ((t)->op & TPL_OP_BLOCK)                                     \
        && ((t)->op != TPL_OP_IFDEF || (t)->u.blocks.len < 2)           \
        )
#endif

void tpl_add_data(tpl_t *tpl, const void *data, int len)
{
    tpl_t *buf;

    assert (tpl_can_append(tpl));

    if (tpl_is_seq(tpl)) {
        qv_append(&tpl->u.blocks, buf = tpl_new_op(TPL_OP_DATA));
        buf->u.data = (struct tpl_data){ .data = data, .len = len };
        return;
    }

    if (len <= TPL_COPY_LIMIT_HARD) {
        tpl_copy_data(tpl, data, len);
        return;
    }

    if (tpl->u.blocks.len > 0
    &&  tpl->u.blocks.tab[tpl->u.blocks.len - 1]->refcnt == 1)
    {
        buf = tpl->u.blocks.tab[tpl->u.blocks.len - 1];
        if (buf->op == TPL_OP_BLOB && len <= TPL_COPY_LIMIT_SOFT) {
            sb_add(&buf->u.blob, data, len);
            return;
        }
    }
    qv_append(&tpl->u.blocks, buf = tpl_new_op(TPL_OP_DATA));
    buf->u.data = (struct tpl_data){ .data = data, .len = len };
}

sb_t *tpl_get_blob(tpl_t *tpl)
{
    tpl_t *buf;

    assert (tpl_can_append(tpl));

    buf = tpl->u.blocks.len > 0 ? tpl->u.blocks.tab[tpl->u.blocks.len - 1] : NULL;
    if (!buf || buf->op != TPL_OP_BLOB || buf->refcnt > 1) {
        qv_append(&tpl->u.blocks, buf = tpl_new_op(TPL_OP_BLOB));
    }
    return &buf->u.blob;
}

void tpl_copy_data(tpl_t *tpl, const void *data, int len)
{
    if (len > 0) {
        sb_add(tpl_get_blob(tpl), data, len);
    }
}

void tpl_add_byte(tpl_t *tpl, byte b)
{
      return tpl_copy_data(tpl, &b, 1);
}

void tpl_add_fmt(tpl_t *tpl, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_addvf(tpl_get_blob(tpl), fmt, ap);
    va_end(ap);
}

void tpl_add_var(tpl_t *tpl, uint16_t array, uint16_t index)
{
    tpl_t *var;

    assert (tpl_can_append(tpl));
    qv_append(&tpl->u.blocks, var = tpl_new_op(TPL_OP_VAR));
    var->u.varidx = ((uint32_t)array << 16) | index;
}

void tpl_embed_tpl(tpl_t *out, tpl_t **tplp)
{
    tpl_t *tpl = *tplp;
    assert (tpl_can_append(out));

    if ((tpl->op == TPL_OP_BLOCK && out->op == TPL_OP_BLOCK)
    ||  (tpl->op == TPL_OP_SEQ   && out->op == TPL_OP_SEQ))
    {
        tpl_add_tpls(out, tpl->u.blocks.tab, tpl->u.blocks.len);
        tpl_delete(tplp);
        return;
    }

    if (tpl->op == TPL_OP_BLOB && out->u.blocks.len > 0
    &&  tpl->u.blob.len <= TPL_COPY_LIMIT_SOFT)
    {
        tpl_t *buf = out->u.blocks.tab[out->u.blocks.len - 1];
        if (buf->op == TPL_OP_BLOB && buf->refcnt == 1) {
            sb_addsb(&buf->u.blob, &tpl->u.blob);
            tpl_delete(tplp);
            return;
        }
    }
    qv_append(&out->u.blocks, tpl);
    *tplp = NULL;
}

void tpl_add_tpl(tpl_t *out, const tpl_t *tpl)
{
    assert (tpl_can_append(out));

    if (tpl->op == TPL_OP_BLOCK && out->op == TPL_OP_BLOCK) {
        tpl_add_tpls(out, tpl->u.blocks.tab, tpl->u.blocks.len);
        return;
    }

    if (tpl->op == TPL_OP_BLOB && out->u.blocks.len > 0
    &&  tpl->u.blob.len <= TPL_COPY_LIMIT_SOFT)
    {
        tpl_t *buf = out->u.blocks.tab[out->u.blocks.len - 1];
        if (buf->op == TPL_OP_BLOB && buf->refcnt == 1) {
            sb_addsb(&buf->u.blob, &tpl->u.blob);
            return;
        }
    }
    qv_append(&out->u.blocks, tpl_dup(tpl));
}

void tpl_add_tpls(tpl_t *out, tpl_t **tpls, int nb)
{
    int pos = out->u.blocks.len;

    assert (tpl_can_append(out));
    qv_splice(&out->u.blocks, pos, 0, (tpl_t *const*)tpls, nb);
    for (int i = pos; i < pos + nb; i++) {
        out->u.blocks.tab[i]->refcnt++;
    }
}

tpl_t *tpl_add_ifdef(tpl_t *tpl, uint16_t array, uint16_t index)
{
    tpl_t *var;

    assert (tpl_can_append(tpl));
    qv_append(&tpl->u.blocks, var = tpl_new_op(TPL_OP_IFDEF));
    var->u.varidx = ((uint32_t)array << 16) | index;
    return var;
}

tpl_t *tpl_add_apply(tpl_t *tpl, tpl_op op, tpl_apply_f *f)
{
    tpl_t *app;

    assert (tpl_can_append(tpl));
    qv_append(&tpl->u.blocks, app = tpl_new_op(op));
    app->u.f = f;
    return app;
}

#ifndef __doxygen_mode__
static char const pad[] = "| | | | | | | | | | | | | | | | | | | | | | | | ";
#endif

static void tpl_dump2(int dbg, const tpl_t *tpl, int lvl)
{
#ifndef __doxygen_mode__
#define HAS_SUBST(tpl) \
    ((tpl->op & TPL_OP_BLOCK && !tpl->is_const) || tpl->op == TPL_OP_VAR)
#define TRACE(fmt, c, ...) \
    e_trace(dbg, "%.*s%c%c "fmt, 1 + 2 * lvl, pad, c, \
            HAS_SUBST(tpl) ? '*' : ' ', ##__VA_ARGS__)
#define TRACE_NULL() \
    e_trace(dbg, "%.*s NULL", 3 + 2 * lvl, pad)
#endif


    switch (tpl->op) {
      case TPL_OP_DATA:
        TRACE("DATA %5d bytes (%*pM...)", ' ', tpl->u.data.len,
              MIN(tpl->u.data.len, 16), tpl->u.data.data);
        return;

      case TPL_OP_BLOB:
        TRACE("BLOB %5d bytes (%*pM...)", ' ', tpl->u.blob.len,
              MIN((int)tpl->u.blob.len, 16), tpl->u.blob.data);
        return;

      case TPL_OP_VAR:
        TRACE("VAR  q=%02x, v=%02x", ' ', tpl->u.varidx >> 16,
              tpl->u.varidx & 0xffff);
        return;

      case TPL_OP_BLOCK:
        TRACE("BLOC %d tpls", '\\', tpl->u.blocks.len);
        for (int i = 0; i < tpl->u.blocks.len; i++) {
            tpl_dump2(dbg, tpl->u.blocks.tab[i], lvl + 1);
        }
        break;

      case TPL_OP_SEQ:
        TRACE("SEQ %d tpls", '\\', tpl->u.blocks.len);
        for (int i = 0; i < tpl->u.blocks.len; i++) {
            tpl_dump2(dbg, tpl->u.blocks.tab[i], lvl + 1);
        }
        break;

      case TPL_OP_IFDEF:
        TRACE("DEF? q=%02x, v=%02x", '\\', tpl->u.varidx >> 16,
              tpl->u.varidx & 0xffff);
        if (tpl->u.blocks.len <= 0 || !tpl->u.blocks.tab[0]) {
            TRACE_NULL();
        } else {
            tpl_dump2(dbg, tpl->u.blocks.tab[0], lvl + 1);
        }
        if (tpl->u.blocks.len <= 1 || !tpl->u.blocks.tab[1]) {
            TRACE_NULL();
        } else {
            tpl_dump2(dbg, tpl->u.blocks.tab[1], lvl + 1);
        }
        break;

      case TPL_OP_APPLY:
      case TPL_OP_APPLY_ASSOC:
        TRACE("FUNC %p (%d tpls)", '\\', tpl->u.f, tpl->u.blocks.len);
        for (int i = 0; i < tpl->u.blocks.len; i++) {
            tpl_dump2(dbg, tpl->u.blocks.tab[i], lvl + 1);
        }
        break;

      case TPL_OP_APPLY_SEQ:
        TRACE("FUNC_SEQ %p (%d tpls)", '\\', tpl->u.f, tpl->u.blocks.len);
        for (int i = 0; i < tpl->u.blocks.len; i++) {
            tpl_dump2(dbg, tpl->u.blocks.tab[i], lvl + 1);
        }
        break;
    }
}

void tpl_dump(int dbg, const tpl_t *tpl, const char *s)
{
    e_trace(dbg, " ,--[ %s ]--", s);
    if (tpl) {
        tpl_dump2(dbg, tpl, 0);
    } else {
        e_trace(dbg, " | NULL");
    }
    e_trace(dbg, " '-----------------");
}

/****************************************************************************/
/* Substitution helpers                                                     */
/****************************************************************************/

int tpl_get_short_data(tpl_t **tpls, int nb, const byte **data, int *len)
{
    if (nb != 1)
        return -1;

    switch (tpls[0]->op) {
      case TPL_OP_BLOB:
        *data = (byte *)tpls[0]->u.blob.data;
        *len  = tpls[0]->u.blob.len;
        return 0;
      case TPL_OP_DATA:
        *data = tpls[0]->u.data.data;
        *len  = tpls[0]->u.data.len;
        return 0;
      default:
        e_panic("unexpected op: %d", tpls[0]->op);
        return -1;
    }
}

/****************************************************************************/
/* Substitution and optimization                                            */
/****************************************************************************/

static int
tpl_apply(tpl_apply_f *f, tpl_t *out, sb_t *blob, tpl_t *in)
{
    if (in->op & TPL_OP_BLOCK)
        return (*f)(out, blob, in->u.blocks.tab, in->u.blocks.len);
    return (*f)(out, blob, &in, 1);
}

#ifndef __doxygen_mode__

#define GETVAR(id, vals, nb) \
    (((vals) && ((id) & 0xffff) < (uint16_t)(nb)) ? (vals)[(id) & 0xffff] : NULL)
#define NS(x)          x##_tpl
#define VAL_TYPE       tpl_t *
#define VAL_TYPE_P     tpl_t *
#define DEAL_WITH_VAR  tpl_combine_tpl
#define DEAL_WITH_VAR2 tpl_fold_sb_tpl
#define TPL_SUBST      tpl_subst
#include "tpl.in.c"
#endif
int tpl_subst(tpl_t **tplp, uint16_t envid, tpl_t **vals, int nb, int flags)
{
    tpl_t *out = *tplp;
    int res = 0;

    if (!out->is_const) {
        out = tpl_new();
        out->is_const = true;
        if ((res = tpl_combine_tpl(out, *tplp, envid, vals, nb, flags))) {
            tpl_delete(&out);
        } else
        if ((flags & TPL_LASTSUBST) && !out->is_const)
            tpl_delete(&out);
        tpl_delete(tplp);
        *tplp = out;
    }
    if (!(flags & TPL_KEEPVAR)) {
        for (int i = 0; i < nb; i++) {
            tpl_delete(&vals[i]);
        }
    }
    return res;
}

int tpl_fold(sb_t *out, tpl_t **tplp, uint16_t envid, tpl_t **vals, int nb,
             int flags)
{
    int pos = out->len;
    int res = 0;

    if ((res = tpl_fold_sb_tpl(out, *tplp, envid, vals, nb, flags))) {
        __sb_fixlen(out, pos);
        return res;
    }
    if (!(flags & TPL_KEEPVAR)) {
        for (int i = 0; i < nb; i++) {
            tpl_delete(&vals[i]);
        }
    }
    tpl_delete(tplp);
    return res;
}

static inline const tpl_str_t *
tpl_str_get_var(uint16_t id, const tpl_str_t *vals, int nb)
{
    const tpl_str_t *res;

    if (!vals || id >= nb)
        return NULL;
    res = &vals[id];
    return res->s ? res : NULL;
}

#ifndef __doxygen_mode__
#define GETVAR(id, vals, nb)  tpl_str_get_var(id, vals, nb)
#define NS(x)          x##_str
#define VAL_TYPE       const tpl_str_t
#define VAL_TYPE_P     const tpl_str_t *
#define DEAL_WITH_VAR(t, v, ...)   (tpl_copy_data((t), (v)->s, tpl_str_len(v)), 0)
#define DEAL_WITH_VAR2(t, v, ...)  (sb_add((t), (v)->s, tpl_str_len(v)), 0)
#define TPL_SUBST      tpl_subst_str
#include "tpl.in.c"
#endif
int tpl_subst_str(tpl_t **tplp, uint16_t envid,
                  const tpl_str_t *vals, int nb, int flags)
{
    tpl_t *out = *tplp;
    int res = 0;

    if (!out->is_const) {
        out = tpl_new();
        out->is_const = true;
        if ((res = tpl_combine_str(out, *tplp, envid, vals, nb, flags))) {
            tpl_delete(&out);
        } else
        if ((flags & TPL_LASTSUBST) && !out->is_const)
            tpl_delete(&out);
        tpl_delete(tplp);
        *tplp = out;
    }
    return res;
}

int tpl_fold_str(sb_t *out, tpl_t **tplp, uint16_t envid,
                 const tpl_str_t *vals, int nb, int flags)
{
    int pos = out->len, res = 0;

    if ((res = tpl_fold_sb_str(out, *tplp, envid, vals, nb, flags))) {
        __sb_fixlen(out, pos);
    }
    tpl_delete(tplp);
    return res;
}

static tpl_t *tpl_to_sb(tpl_t **orig)
{
    tpl_t *res = tpl_new_op(TPL_OP_BLOB);
    assert ((*orig)->op == TPL_OP_DATA || (*orig)->op == TPL_OP_BLOB);
    if ((*orig)->op == TPL_OP_DATA) {
        sb_set(&res->u.blob, (*orig)->u.data.data, (*orig)->u.data.len);
    } else {
        sb_setsb(&res->u.blob, &(*orig)->u.blob);
    }
    tpl_delete(orig);
    return *orig = res;
}

static void tpl_remove_useless_block(tpl_t **block)
{
    tpl_t *tpl = *block;

    if (tpl->op == TPL_OP_BLOCK && tpl->u.blocks.len == 1) {
        tpl_t *child = tpl_dup(tpl->u.blocks.tab[0]);
        tpl_delete(block);
        *block = child;
    }
}

void tpl_optimize(tpl_t *tpl)
{
    if (!tpl || !(tpl->op & TPL_OP_BLOCK) || tpl->u.blocks.len < 1)
        return;

    /* XXX: tpl->u.blocks.len likely to be modified in the loop */
    for (int i = 0; i < tpl->u.blocks.len; ) {
        tpl_t *cur = tpl->u.blocks.tab[i];

        if (cur->op == TPL_OP_BLOCK && tpl->op == TPL_OP_BLOCK) {
            qv_remove(&tpl->u.blocks, i);
            tpl_add_tpls(tpl, cur->u.blocks.tab, cur->u.blocks.len);
            tpl_delete(&cur);
        } else {
            if (cur->op & TPL_OP_BLOCK) {
                tpl_optimize(tpl->u.blocks.tab[i]);
            }
            i++;
        }
    }

    if (tpl->op & TPL_OP_NOT_MERGEABLE)
        goto no_seq_merge;

    for (int i = 0; i < tpl->u.blocks.len - 1; ) {
        tpl_t *cur = tpl->u.blocks.tab[i];
        tpl_t *nxt = tpl->u.blocks.tab[i + 1];

        if (nxt->op != TPL_OP_BLOB) {
            if (nxt->op != TPL_OP_DATA || nxt->u.data.len >= TPL_DATA_LIMIT_KEEP) {
                i += 2;
                continue;
            }
        }

        if (cur->op != TPL_OP_BLOB) {
            if (cur->op != TPL_OP_DATA || cur->u.data.len >= TPL_DATA_LIMIT_KEEP) {
                i++;
                continue;
            }
            if (nxt->op == TPL_OP_BLOB && nxt->refcnt == 1) {
                sb_splice(&nxt->u.blob, 0, 0,
                          cur->u.data.data, cur->u.data.len);
                qv_remove(&tpl->u.blocks, i);
                tpl_delete(&cur);
                continue;
            }
            cur = tpl_to_sb(&tpl->u.blocks.tab[i]);
        }

        assert (cur->op == TPL_OP_BLOB);
        if (nxt->op == TPL_OP_DATA) {
            if (cur->refcnt > 1) {
                cur = tpl_to_sb(&tpl->u.blocks.tab[i]);
            }
            sb_add(&cur->u.blob, nxt->u.data.data, nxt->u.data.len);
            qv_remove(&tpl->u.blocks, i + 1);
            tpl_delete(&nxt);
            continue;
        }

        assert (nxt->op == TPL_OP_BLOB);
        if (cur->refcnt > 1 && nxt->refcnt > 1) {
            cur = tpl_to_sb(&tpl->u.blocks.tab[i]);
        }
        if (cur->refcnt > 1) {
            sb_splice(&nxt->u.blob, 0, 0, cur->u.blob.data, cur->u.blob.len);
            qv_remove(&tpl->u.blocks, i);
            tpl_delete(&cur);
        } else {
            sb_addsb(&cur->u.blob, &nxt->u.blob);
            qv_remove(&tpl->u.blocks, i + 1);
            tpl_delete(&nxt);
        }
    }

  no_seq_merge:

    for (int i = 0; i < tpl->u.blocks.len; i++) {
        tpl_remove_useless_block(&tpl->u.blocks.tab[i]);
    }
}

bool tpl_is_variable(const tpl_t *tpl)
{
    switch (tpl->op) {
      case TPL_OP_DATA:
      case TPL_OP_BLOB:
        return true;

      case TPL_OP_BLOCK:
        for (int i = 0; i < tpl->u.blocks.len; i++) {
            if (!tpl_is_variable(tpl->u.blocks.tab[i]))
                return false;
        }
        return true;

      default:
        return false;
    }
}

int tpl_to_iov(struct iovec *iov, int nr, tpl_t *tpl)
{
    switch (tpl->op) {
        int n;
      case TPL_OP_DATA:
        if (nr > 0) {
            iov->iov_base = (void *)tpl->u.data.data;
            iov->iov_len  = tpl->u.data.len;
        }
        return 1;
      case TPL_OP_BLOB:
        if (nr > 0) {
            iov->iov_base = (void *)tpl->u.blob.data;
            iov->iov_len  = tpl->u.blob.len;
        }
        return 1;
      case TPL_OP_BLOCK:
        for (int i = n = 0; i < tpl->u.blocks.len; i++) {
            n += RETHROW(tpl_to_iov(iov + n, n < nr ? nr - n : 0,
                         tpl->u.blocks.tab[i]));
        }
        return n;
      default:
        return -1;
    }
}

int tpl_to_iovec_vector(qv_t(iovec) *iov, tpl_t *tpl)
{
    int oldlen = iov->len;

    switch (tpl->op) {
      case TPL_OP_DATA:
        qv_append(iov, MAKE_IOVEC(tpl->u.data.data, tpl->u.data.len));
        return 0;

      case TPL_OP_BLOB:
        qv_append(iov, MAKE_IOVEC(tpl->u.blob.data, tpl->u.blob.len));
        return 0;

      case TPL_OP_BLOCK:
        for (int i = 0; i < tpl->u.blocks.len; i++) {
            if (tpl_to_iovec_vector(iov, tpl->u.blocks.tab[i])) {
                iov->len = oldlen;
                return -1;
            }
        }
        return 0;

      default:
        return -1;
    }
}

/**\}*/
