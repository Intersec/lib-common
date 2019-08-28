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

#include "iop-helpers.in.c"

struct iop_hash_ctx {
    size_t   pos;
    uint8_t  buf[1024];
    void   (*hfun)(void *ctx, const void *input, ssize_t len);
    void    *ctx;
};

ATTRS
static ALWAYS_INLINE
void F(iop_hash_update)(struct iop_hash_ctx *ctx, const void *d, size_t len)
{
    size_t pos = ctx->pos;

    if (pos + len > sizeof(ctx->buf)) {
        ctx->pos = 0;
        ctx->hfun(ctx->ctx, ctx->buf, pos);
        ctx->hfun(ctx->ctx, d, len);
    } else {
        memcpy(ctx->buf + pos, d, len);
        if ((pos += len) > sizeof(ctx->buf) / 2) {
            ctx->pos = 0;
            ctx->hfun(ctx->ctx, ctx->buf, pos);
        } else {
            ctx->pos = pos;
        }
    }
}

ATTRS
static ALWAYS_INLINE
void F(iop_hash_update_u16)(struct iop_hash_ctx *ctx, uint16_t i)
{
    size_t pos = ctx->pos;

    assert (pos + 2 < sizeof(ctx->buf));
    put_unaligned_le16(ctx->buf + pos, i);
    if ((pos += 2) > sizeof(ctx->buf) / 2) {
        ctx->pos = 0;
        ctx->hfun(ctx->ctx, ctx->buf, pos);
    } else {
        ctx->pos = pos;
    }
}

ATTRS
static ALWAYS_INLINE
void F(iop_hash_update_u32)(struct iop_hash_ctx *ctx, uint32_t i)
{
    size_t pos = ctx->pos;

    assert (pos + 4 < sizeof(ctx->buf));
    put_unaligned_le32(ctx->buf + pos, i);
    if ((pos += 4) > sizeof(ctx->buf) / 2) {
        ctx->pos = 0;
        ctx->hfun(ctx->ctx, ctx->buf, pos);
    } else {
        ctx->pos = pos;
    }
}

ATTRS
static ALWAYS_INLINE
void F(iop_hash_update_i64)(struct iop_hash_ctx *ctx, int64_t i)
{
    size_t pos = ctx->pos;

    assert (pos + 8 < sizeof(ctx->buf));
    put_unaligned_le64(ctx->buf + pos, i);
    if ((pos += 8) > sizeof(ctx->buf) / 2) {
        ctx->pos = 0;
        ctx->hfun(ctx->ctx, ctx->buf, pos);
    } else {
        ctx->pos = pos;
    }
}
#define iop_hash_update_dbl(ctx, d) \
    F(iop_hash_update_i64)(ctx, double_bits_cpu(d))

ATTRS
static void
F(iop_hash_opt)(struct iop_hash_ctx *ctx, const iop_field_t *f, const void *v)
{
    uint8_t b;

    switch (f->type) {
      case IOP_T_BOOL:
        b = !!((opt_bool_t *)v)->v;
        F(iop_hash_update)(ctx, &b, 1);
        break;
      case IOP_T_I8:
        F(iop_hash_update_i64)(ctx, ((opt_i8_t *)v)->v);
        break;
      case IOP_T_U8:
        F(iop_hash_update_i64)(ctx, ((opt_u8_t *)v)->v);
        break;
      case IOP_T_I16:
        F(iop_hash_update_i64)(ctx, ((opt_i16_t *)v)->v);
        break;
      case IOP_T_U16:
        F(iop_hash_update_i64)(ctx, ((opt_u16_t *)v)->v);
        break;
      case IOP_T_I32: case IOP_T_ENUM:
        F(iop_hash_update_i64)(ctx, ((opt_i32_t *)v)->v);
        break;
      case IOP_T_U32:
        F(iop_hash_update_i64)(ctx, ((opt_u32_t *)v)->v);
        break;
      case IOP_T_I64:
        F(iop_hash_update_i64)(ctx, ((opt_i64_t *)v)->v);
        break;
      case IOP_T_U64:
        F(iop_hash_update_i64)(ctx, ((opt_u64_t *)v)->v);
        break;
      case IOP_T_DOUBLE:
        iop_hash_update_dbl(ctx, ((opt_double_t *)v)->v);
        break;
      case IOP_T_VOID:
        b = true;
        F(iop_hash_update)(ctx, &b, 1);
        break;
      case IOP_T_STRING:
      case IOP_T_DATA:
      case IOP_T_UNION:
      case IOP_T_STRUCT:
      case IOP_T_XML:
        e_panic("iop_type unsupported");
    }
}

ATTRS
static void F(__iop_hash)(struct iop_hash_ctx *ctx, const iop_struct_t *st,
                          const uint8_t *v, unsigned flags);

ATTRS
static inline void
F(__iop_hash_class)(struct iop_hash_ctx *ctx, const iop_struct_t *st,
                    const uint8_t *v, unsigned flags)
{
    st = *(const iop_struct_t **)v;
    if (!(flags & IOP_HASH_DONT_INCLUDE_CLASS_ID)) {
        F(iop_hash_update_u16)(ctx, st->class_attrs->class_id);
    }
    do {
        F(__iop_hash)(ctx, st, v, flags);
    } while ((st = st->class_attrs->parent));
}

ATTRS
static void
F(__iop_hash)(struct iop_hash_ctx *ctx, const iop_struct_t *st,
              const uint8_t *v, unsigned flags)
{
    const iop_field_t *fdesc;
    const iop_field_t *end;

    if (st->is_union) {
        fdesc = get_union_field(st, v);
        end   = fdesc + 1;
    } else {
        fdesc = st->fields;
        end   = fdesc + st->fields_len;
    }

    for (; fdesc < end; fdesc++) {
        const void *r = v + fdesc->data_offs;
        int n = 1;

        if (fdesc->repeat == IOP_R_REPEATED) {
            n  = ((lstr_t *)r)->len;
            r  = ((lstr_t *)r)->data;
            if (!n && (flags & IOP_HASH_SKIP_MISSING)) {
                continue;
            }
        }

        if (fdesc->repeat == IOP_R_OPTIONAL && (flags & IOP_HASH_SKIP_MISSING)
        &&  !iop_opt_field_isset(fdesc->type, r))
        {
            continue;
        }
        if (fdesc->repeat == IOP_R_DEFVAL && (flags & IOP_HASH_SKIP_DEFAULT)
        &&  iop_field_is_defval(fdesc, r, !(flags & IOP_HASH_SHALLOW_DEFAULT)))
        {
            continue;
        }

        F(iop_hash_update_u16)(ctx, fdesc->tag);
        F(iop_hash_update)(ctx, fdesc->name.s, fdesc->name.len);

        if (fdesc->repeat == IOP_R_REPEATED) {
            F(iop_hash_update_u32)(ctx, n);
        }

        if (fdesc->repeat == IOP_R_OPTIONAL) {
            if (!iop_opt_field_isset(fdesc->type, r)) {
                continue;
            }

            if ((1 << fdesc->type) & IOP_BLK_OK) {
                if ((1 << fdesc->type) & IOP_STRUCTS_OK) {
                    r = *(void **)r;
                }
            } else {
                F(iop_hash_opt)(ctx, fdesc, r);
                continue;
            }
        }

        switch (fdesc->type) {
          case IOP_T_I8:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((int8_t *)r)[i]);
            }
            break;
          case IOP_T_BOOL:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, !!((bool *)r)[i]);
            }
            break;
          case IOP_T_U8:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((uint8_t *)r)[i]);
            }
            break;
          case IOP_T_I16:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((int16_t *)r)[i]);
            }
            break;
          case IOP_T_U16:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((uint16_t *)r)[i]);
            }
            break;
          case IOP_T_I32: case IOP_T_ENUM:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((int32_t *)r)[i]);
            }
            break;
          case IOP_T_U32:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((uint32_t *)r)[i]);
            }
            break;
          case IOP_T_I64:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((int64_t *)r)[i]);
            }
            break;
          case IOP_T_U64:
            for (int i = 0; i < n; i++) {
                F(iop_hash_update_i64)(ctx, ((uint64_t *)r)[i]);
            }
            break;
          case IOP_T_DOUBLE:
            for (int i = 0; i < n; i++) {
                iop_hash_update_dbl(ctx, ((double *)r)[i]);
            }
            break;
          case IOP_T_UNION:
          case IOP_T_STRUCT: {
            bool is_class = iop_field_is_class(fdesc);
            bool is_ref   = iop_field_is_reference(fdesc);

            for (int i = 0; i < n; i++) {
                const uint8_t *v2;

                v2 = &IOP_FIELD(const uint8_t, r, i * fdesc->size);

                if ((is_class || is_ref) && fdesc->repeat != IOP_R_OPTIONAL) {
                    /* Non-optional class fields have to be dereferenced
                     * (dereferencing of optional fields was already done just
                     *  above).
                     */
                    v2 = *(void **)v2;
                }
                if (is_class) {
                    F(__iop_hash_class)(ctx, fdesc->u1.st_desc, v2, flags);
                } else {
                    F(__iop_hash)(ctx, fdesc->u1.st_desc, v2, flags);
                }
            }
            break;
          }
          case IOP_T_XML:
          case IOP_T_STRING:
          case IOP_T_DATA:
            for (int i = 0; i < n; i++) {
                const lstr_t *s = &IOP_FIELD(const lstr_t, r, i);

                F(iop_hash_update_u32)(ctx, s->len);
                F(iop_hash_update)(ctx, s->data, s->len);
            }
            break;
          case IOP_T_VOID:
            break;
        }
    }
}

ATTRS
#ifdef ALL_STATIC
static
#endif
void F(iop_hash)(const iop_struct_t *st, const void *v,
                 void (*hfun)(void *ctx, const void *input, ssize_t ilen),
                 void *hctx, unsigned flags)
{
    struct iop_hash_ctx ctx = {
        .hfun = hfun,
        .ctx  = hctx,
    };

    if (iop_struct_is_class(st)) {
        F(__iop_hash_class)(&ctx, st, v, flags);
    } else {
        F(__iop_hash)(&ctx, st, v, flags);
    }
    if (ctx.pos)
        hfun(hctx, ctx.buf, ctx.pos);
}
