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

#ifndef IS_LIB_COMMON_TPL_H
#define IS_LIB_COMMON_TPL_H

#include "unix.h"

/** \defgroup templates Intersec generic templating API.
 *
 * \brief This module defines the #tpl_t type, and its manipulation APIs.
 *
 * \TODO PUT SOME EXPLANATION ON THE PHILOSOPHY OF tpl_t'S HERE.
 *
 *\{*/

/** \file tpl.h
 * \brief Templating module header.
 */

#define TPL_COPY_LIMIT_HARD    32
#define TPL_COPY_LIMIT_SOFT   256
#define TPL_DATA_LIMIT_KEEP  4096
#define TPL_OP_NOT_MERGEABLE 0x20

typedef enum tpl_op {
    TPL_OP_DATA = 0x00,
    TPL_OP_BLOB,
    TPL_OP_VAR,

    TPL_OP_BLOCK = 0x10,
    TPL_OP_SEQ,               /* should only be used under APPLYs */
    TPL_OP_APPLY,             /* f(x) only depends upon x */
    TPL_OP_APPLY_ASSOC,       /* also f(a + b) == f(a) + f(b) */
    TPL_OP_APPLY_SEQ = TPL_OP_BLOCK | TPL_OP_NOT_MERGEABLE,  /* f(a,b,...) */
    TPL_OP_IFDEF,
} tpl_op;

struct tpl_data {
    const byte *data;
    int len;
};

struct tpl_t;
typedef int (tpl_apply_f)(struct tpl_t *, sb_t *, struct tpl_t **, int nb);

qvector_t(tpl, struct tpl_t *);
typedef struct tpl_t {
    bool is_const   :  1; /* if the subtree has TPL_OP_VARs in it */
    tpl_op op       :  7;
    unsigned refcnt : 24;
    union {
        struct tpl_data data;
        sb_t   blob;
        uint32_t varidx; /* 16 bits of env, 16 bits of index */
        struct {
            tpl_apply_f *f;
            qv_t(tpl)    blocks;
        };
    } u;
} tpl_t;

#define tpl_new()  tpl_new_op(TPL_OP_BLOCK)
tpl_t *tpl_new_op(tpl_op op);
tpl_t *tpl_new_var(uint16_t envid, uint16_t index);
tpl_t *tpl_dup(const tpl_t *);
void tpl_delete(tpl_t **);

/* XXX: This function does not copy str content */
static inline tpl_t *tpl_new_cstr(const void *str, int len)
{
    tpl_t *tpl = tpl_new_op(TPL_OP_DATA);
    tpl->u.data.data = str;
    if (len < 0)
        len = strlen(str);
    tpl->u.data.len = len;
    return tpl;
}

/****************************************************************************/
/* Build the AST                                                            */
/****************************************************************************/

sb_t *tpl_get_blob(tpl_t *tpl);

void tpl_add_data(tpl_t *tpl, const void *data, int len);
void tpl_add_byte(tpl_t *tpl, byte b);
static inline void tpl_add_cstr(tpl_t *tpl, const char *s) {
    tpl_add_data(tpl, s, strlen(s));
}
void tpl_add_fmt(tpl_t *tpl, const char *fmt, ...) __attr_printf__(2, 3);

void tpl_copy_data(tpl_t *tpl, const void *data, int len);
static inline void tpl_copy_cstr(tpl_t *tpl, const char *s) {
    tpl_copy_data(tpl, s, strlen(s));
}
void tpl_add_var(tpl_t *tpl, uint16_t envid, uint16_t index);
void tpl_embed_tpl(tpl_t *out, tpl_t **tpl);

/* XXX: tpl_add_tpl uses tpl_dup: be sure to free 'tpl' afterwards */
void tpl_add_tpl(tpl_t *out, const tpl_t *tpl);

void tpl_add_tpls(tpl_t *out, tpl_t **tpl, int nb);
tpl_t *tpl_add_ifdef(tpl_t *tpl, uint16_t envid, uint16_t index);
tpl_t *tpl_add_apply(tpl_t *tpl, tpl_op op, tpl_apply_f *f);
void tpl_dump(int dbg, const tpl_t *tpl, const char *s);

/****************************************************************************/
/* Substitution and optimization                                            */
/****************************************************************************/

enum {
    TPL_KEEPVAR    = 1 << 0,
    TPL_LASTSUBST  = 1 << 1,
};

typedef struct tpl_str_t {
    const char *s;
    int len;
} tpl_str_t;
#define TPL_STR_NULL        (tpl_str_t){ .s = NULL, .len = 0 }
#define TPL_STR_EMPTY       (tpl_str_t){ .s = "", .len = 0 }
#define TPL_STR2(str, _len) (tpl_str_t){ .s = (str), .len = (_len) }
#define TPL_STR(str)        TPL_STR2(str, strlen(str))
#define TPL_CSTR(str)       (tpl_str_t){ .s = (str), .len = sizeof(str) - 1 }
#define TPL_SBSTR(sb)       (tpl_str_t){ .s = (sb)->data, .len = (sb)->len }

static inline int tpl_str_len(const tpl_str_t *ts) {
    return ts->len >= 0 ? ts->len : (int)strlen(ts->s);
}

int tpl_get_short_data(tpl_t **tpls, int nb, const byte **data, int *len);

int tpl_fold(sb_t *, tpl_t **, uint16_t envid, tpl_t **, int nb, int flags);
int tpl_fold_str(sb_t *, tpl_t **, uint16_t envid, const tpl_str_t *, int nb, int flags);

int tpl_subst(tpl_t **, uint16_t envid, tpl_t **, int nb, int flags);
int tpl_subst_str(tpl_t **, uint16_t envid, const tpl_str_t *, int nb, int flags);
void tpl_optimize(tpl_t *tpl);

bool tpl_is_variable(const tpl_t *tpl);
#define tpl_is_seq(t)  \
    (((t)->op == TPL_OP_SEQ) || ((t)->op == TPL_OP_APPLY_SEQ))

int tpl_to_iov(struct iovec *, int nr, tpl_t *);
int tpl_to_iovec_vector(qv_t(iovec) *iov, tpl_t *tpl);

static inline void tpl_blob_append(tpl_t *tpl, sb_t *out)
{
    if (tpl->op == TPL_OP_DATA) {
        sb_add(out, tpl->u.data.data, tpl->u.data.len);
    } else {
        assert (tpl->op == TPL_OP_BLOB);
        sb_add(out, tpl->u.blob.data, tpl->u.blob.len);
    }
}

/****************************************************************************/
/* Checksums                                                                */
/****************************************************************************/

int tpl_compute_len_copy(sb_t *b, tpl_t **args, int nb, int len);

/****************************************************************************/
/* Escapings                                                                */
/****************************************************************************/

int tpl_encode_xml(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_url(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_ira(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_ira_bin(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_ucs2be(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_ucs2be_hex(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_base64(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_qp(tpl_t *out, sb_t *sb, tpl_t **args, int nb);
int tpl_encode_latin1(tpl_t *out, sb_t *sb, tpl_t **args, int nb);

/**\}*/
#endif
