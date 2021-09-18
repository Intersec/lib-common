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

#include <lib-common/asn1/per-priv.h>

#include <lib-common/bit-buf.h>
#include <lib-common/bit-stream.h>

/* XXX Tracing policy:
 *     5: Low level writer/reader
 *     4: PER packer/unpacker
 */

static struct {
    int decode_log_level;
} asn1_per_g = {
#define _G  asn1_per_g
    .decode_log_level = -1,
};

/* Big Endian generic helpers {{{ */

static ALWAYS_INLINE void
aper_write_aligned_int(bb_t *bb, int64_t i64, uint8_t olen)
{
    bb_align(bb);
    assert (olen <= 8);
    bb_be_add_bits(bb, i64, olen * 8);
}

static ALWAYS_INLINE void aper_write_aligned_u8(bb_t *bb, uint8_t u8)
{
    bb_align(bb);
    bb_be_add_bits(bb, u8, 8);
}

static ALWAYS_INLINE void aper_write_aligned_u16(bb_t *bb, uint16_t u16)
{
    bb_align(bb);
    bb_be_add_bits(bb, u16, 16);
}

static ALWAYS_INLINE int aper_read_aligned_u8(bit_stream_t *bs, uint8_t *res)
{
    uint64_t r64 = 0;

    RETHROW(bs_align(bs));
    RETHROW(bs_be_get_bits(bs, 8, &r64));
    *res = r64;
    return 0;
}

static ALWAYS_INLINE int aper_read_aligned_u16(bit_stream_t *bs,
                                               uint16_t *res)
{
    uint64_t r64 = 0;

    RETHROW(bs_align(bs));
    RETHROW(bs_be_get_bits(bs, 16, &r64));
    *res = r64;
    return 0;
}

static ALWAYS_INLINE int aper_read_aligned_uint(bit_stream_t *bs, size_t olen,
                                                uint64_t *res)
{
    *res = 0;
    RETHROW(bs_align(bs));
    RETHROW(bs_be_get_bits(bs, olen * 8, res));
    return 0;
}

static ALWAYS_INLINE int aper_read_aligned_int(bit_stream_t *bs, size_t olen,
                                               int64_t *res)
{
    uint64_t u = 0;

    RETHROW(aper_read_aligned_uint(bs, olen, &u));
    *res = sign_extend(u, olen * 8);
    return 0;
}

static int t_aper_get_unaligned_bytes(bit_stream_t *bs, size_t olen,
                                      bool copy, lstr_t *res)
{
    THROW_ERR_IF(!bs_has_bytes(bs, olen));
    if (bs_is_aligned(bs)) {
        pstream_t ps;

        ps = __bs_get_bytes(bs, olen);
        *res = LSTR_PS_V(&ps);
        if (copy) {
            t_lstr_persists(res);
        }
    } else {
        uint8_t *buf;

        buf = t_new_raw(uint8_t, olen);
        for (size_t i = 0; i < olen; i++) {
            buf[i] = __bs_be_get_bits(bs, 8);
        }

        *res = mp_lstr_init(t_pool(), buf, olen);
    }

    return 0;
}

/* }}} */
/* PER generic helpers {{{ */

static bool is_bstring_aligned(const asn1_cnt_info_t *constraints,
                               size_t len)
{
    /* No need to realign for an empty bit string. */
    THROW_FALSE_IF(!len);

    if (constraints->max <= 16 && constraints->min == constraints->max) {
        /* Only fixed-sized bit string with size <= 16 may be not aligned. */
        if (len != constraints->min) {
            /* The length is not within the root. */
            assert(constraints->extended);

            return true;
        }
        return false;
    }

    return true;
}

/* }}} */
/* Write {{{ */

/* Helpers {{{ */

/* Fully constrained integer - d_max < 65536 */
void aper_write_u16_m(bb_t *bb, uint16_t u16, uint16_t blen, uint16_t d_max)
{
    bb_push_mark(bb);

    if (!blen) {
        goto end;
    }

    if (blen == 8 && d_max == 255) {
        /* "The one-octet case". */
        aper_write_aligned_u8(bb, u16);
        goto end;
    }

    if (blen <= 8) {
        /* "The bit-field case". */
        bb_be_add_bits(bb, u16, blen);
        goto end;
    }

    assert (blen <= 16);

    /* "The two-octet case". */
    aper_write_aligned_u16(bb, u16);
    /* FALLTHROUGH */

  end:
    e_trace_be_bb_tail(5, bb, "constrained number (n = %u)", u16);
    bb_pop_mark(bb);
}

#define PER_FRAG_64K  (64 << 10)
#define PER_FRAG_16K  (16 << 10)

/* Unconstrained length */
static ALWAYS_INLINE void
aper_write_ulen(bb_t *bb, size_t l, bool *nullable need_fragmentation)
{
    /* See aper_write_len(). */
    assert (!need_fragmentation || !*need_fragmentation);

    bb_push_mark(bb);

    bb_align(bb);

    e_trace_be_bb_tail(5, bb, "align");
    bb_reset_mark(bb);

    if (l <= 127) {
        aper_write_aligned_u8(bb, l);

        e_trace_be_bb_tail(5, bb, "unconstrained length (l = %zd)", l);
        bb_pop_mark(bb);

        return;
    }

    if (l < PER_FRAG_16K) {
        uint16_t u16  = l | (1 << 15);

        aper_write_aligned_u16(bb, u16);

        e_trace_be_bb_tail(5, bb, "unconstrained length (l = %zd)", l);
        bb_pop_mark(bb);

        return;
    }

    bb_pop_mark(bb);

    /* The length should be check in advance. */
    assert(need_fragmentation);
    *need_fragmentation = true;
}

static ALWAYS_INLINE void aper_write_2c_number(bb_t *bb, int64_t v,
                                               bool is_signed)
{
    uint8_t olen;

    /* XXX Handle the special case of unsigned 64-bits integers
     * in [ INT64_MAX + 1, UINT64_MAX ]. */
    if (unlikely(!is_signed && TST_BIT(&v, 63))) {
        olen = 8;
        aper_write_ulen(bb, 9, NULL);
        bb_align(bb);
        bb_add0s(bb, 8);
    } else {
        olen = i64_olen(v);
        aper_write_ulen(bb, olen, NULL);
    }
    aper_write_aligned_int(bb, v, olen);
}

/* XXX semi-constrained or constrained numbers */
static ALWAYS_INLINE void
aper_write_number(bb_t *bb, uint64_t v, const asn1_int_info_t *info)
{
    uint8_t olen;

    if (info && info->has_min && info->has_max) {
        if (info->max_blen <= 16) {
            aper_write_u16_m(bb, v, info->max_blen, info->d_max);
            return;
        }

        olen = u64_olen(v);
        aper_write_u16_m(bb, olen - 1, info->max_olen_blen, info->d_max);
    } else {
        olen = u64_olen(v);
        aper_write_ulen(bb, olen, NULL);
    }

    aper_write_aligned_int(bb, v, olen);
}

/* Normally small non-negative whole number (SIC) */
/* XXX Used for :
 *         - CHOICE index
 *         - Enumeration extensions
 *         - ???
 */
void aper_write_nsnnwn(bb_t *bb, size_t n)
{
    if (n <= 63) {
        bb_be_add_bits(bb, n, 1 + 6);
        return;
    }

    bb_be_add_bit(bb, true);
    aper_write_number(bb, n, NULL);
}

void aper_write_len(bb_t *bb, size_t l, size_t l_min, size_t l_max,
                    bool *nullable need_fragmentation)
{
    /* If set, it is caller's responsibility to pass a boolean initialized
     * with the value 'false'.
     */
    assert (!need_fragmentation || !*need_fragmentation);

    if (l_max != SIZE_MAX) {
        uint32_t d_max = l_max - l_min;
        uint32_t d     = l - l_min;

        assert (l <= l_max);

        if (d_max < (1 << 16)) {
            /* TODO pre-process u16_blen(d_max) */
            aper_write_u16_m(bb, d, u16_blen(d_max), d_max);
            return;
        }

        /* FIXME It doesn't look like this case is properly encoded
         * ("indefinite length case" cf. [1] ITU-T X.691 - §11.5.7)
         * It looks like we should encode it as a non-negative-binary-integer
         * in a bit-field (cf. [1] §11.5.7.4), for which the encoding is
         * described in [1] §11.3.
         */
    }

    aper_write_ulen(bb, l, need_fragmentation);
}

/* }}} */
/* Front End Encoders {{{ */

/* Length encoding {{{ */

typedef struct {
    int len;
    int to_encode;
    int remains;

    bool extension_present;
    bool use_fragmentation;
    bool done;

    /* XXX Set only when the length value is within the root. */
    size_t min_root_len;
    size_t max_root_len;
} aper_len_encoding_ctx_t;

GENERIC_INIT(aper_len_encoding_ctx_t, aper_len_encoding_ctx);

static void sb_add_asn1_size(sb_t *sb, size_t size)
{
    if (size == SIZE_MAX) {
        sb_adds(sb, "MAX");
    } else {
        sb_addf(sb, "%zu", size);
    }
}

static void sb_add_asn1_len_min_max(sb_t *sb, size_t min, size_t max)
{
    if (min == max) {
        sb_addf(sb, "%zu", min);
    } else {
        sb_addf(sb, "%zu..", min);
        sb_add_asn1_size(sb, max);
    }
}

void sb_add_asn1_len_constraints(sb_t *sb, const asn1_cnt_info_t *info)
{
    sb_adds(sb, "SIZE(");
    sb_add_asn1_len_min_max(sb, info->min, info->max);
    if (info->extended) {
        sb_adds(sb, ", ...");
        if (info->ext_min != 0 || info->ext_max != SIZE_MAX) {
            sb_adds(sb, ", ");
            sb_add_asn1_len_min_max(sb, info->ext_min, info->ext_max);
        }
    }
    sb_adds(sb, ")");
}

static void aper_trace_constraint_violation(const asn1_cnt_info_t *info,
                                            size_t len)
{
    SB_1k(constraints);

    sb_add_asn1_len_constraints(&constraints, info);
    e_error("length = %zu, constraints = %*pM", len,
            SB_FMT_ARG(&constraints));
}

/* Check constraints, write extension bit (if needed) and prepare encoding
 * context. */
static int
aper_encode_len_extension_bit(bb_t *bb, size_t l, const asn1_cnt_info_t *info,
                              aper_len_encoding_ctx_t *ctx)
{
    aper_len_encoding_ctx_init(ctx);
    ctx->len = l;
    ctx->remains = l;
    ctx->done = false;

    if (info) {
        if (l < info->min || l > info->max) {
            if (info->extended) {
                ctx->extension_present = true;

                if (l < info->ext_min || l > info->ext_max) {
                    aper_trace_constraint_violation(info, l);
                    return e_error("extended constraint not respected");
                }

                /* Extension present */
                bb_be_add_bit(bb, true);
            } else {
                aper_trace_constraint_violation(info, l);
                return e_error("root constraint not respected");
            }
        } else {
            if (info->extended) {
                /* Extension not present */
                bb_be_add_bit(bb, false);
            }

            ctx->min_root_len = info->min;
            ctx->max_root_len = info->max;
        }
    } else {
        ctx->max_root_len = SIZE_MAX;
    }

    return 0;
}

/** Encode the length of a repeated element (octet string, bit string,
 * sequence of, set of, etc...).
 *
 * To call before encoding the data. This function also handles data
 * fragmentation. After the call the number of elements to encode is set in
 * \p ctx->to_encode and the caller can tell if it was the last bit of data to
 * encode by checking \p ctx->done.
 *
 * Details about the fragmentation:
 *
 * The principle and encoding rules for fragmentation is given in the
 * ITU-T specification X.691, especially in §11.9.3.8.1.
 *
 * General case:
 *
 * 1. The items are written per fragment of 64k items max.,
 * 2. Then we write a penultimate fragment of 16k, 32k or 48k items
 *    (if there a less than 16k items left, directly go to next step),
 * 3. Then we write the remainder.

 * ┌───┬───────┬───────────┬───┬───────┬───────────┬──┬────────┬─────────┐
 * │ 11 000100 │ 64K items │ 11 000001 │ 16K items │ 0 0000011 │ 3 items │
 * └───┴───────┴───────────┴───┴───────┴───────────┴──┴────────┴─────────┘
 *  fragment    value       fragment    value      unconstrained  value
 *  length                  length                 length
 *  (16k blocks)            (16k blocks)           (remainder)

 * Special case when the number of elements is a multiple of 16k.
 * We encode an empty remainder:

 * ┌───┬───────┬───────────┬───┬───────┬───────────┬──┬────────┐
 * │ 11 000100 │ 64K items │ 11 000011 │ 48K items │ 0 0000000 │
 * └───┴───────┴───────────┴───┴───────┴───────────┴──┴────────┘
 *  fragment    value       fragment    value      unconstrained
 *  length                  length                 length == 0
 *  (16k blocks)            (16k blocks)           (empty remainder)
 */
static void aper_encode_len(bb_t *bb, aper_len_encoding_ctx_t *ctx)
{
    if (!ctx->use_fragmentation) {
        if (ctx->extension_present) {
            aper_write_ulen(bb, ctx->len, &ctx->use_fragmentation);
        } else {
            aper_write_len(bb, ctx->len, ctx->min_root_len,
                           ctx->max_root_len, &ctx->use_fragmentation);
        }
        if (!ctx->use_fragmentation) {
            ctx->done = true;
            ctx->to_encode = ctx->len;
        }
    }
    if (ctx->use_fragmentation) {
        if (ctx->remains < PER_FRAG_16K) {
            aper_write_ulen(bb, ctx->remains, NULL);
            ctx->to_encode = ctx->remains;
            ctx->done = true;
        } else {
            int nb_16k_blocks;
            int to_encode;

            to_encode = MIN(ctx->remains, PER_FRAG_64K);
            nb_16k_blocks = to_encode / PER_FRAG_16K;
            to_encode = nb_16k_blocks * PER_FRAG_16K;
            ctx->to_encode = to_encode;

            bb_align(bb);
            bb_be_add_byte(bb, 0xc0 | nb_16k_blocks);
        }
    }
    ctx->remains -= ctx->to_encode;
}

/* }}} */
/* Scalar types {{{ */

static ALWAYS_INLINE int check_constraints(int64_t n,
                                           bool has_min, asn1_int_t min,
                                           bool has_max, asn1_int_t max,
                                           bool is_signed)
{
    if (is_signed) {
        THROW_ERR_IF((has_min && n < min.i) || (has_max && n > max.i));
    } else {
        uint64_t u = n;

        THROW_ERR_IF((has_min && u < min.u) || (has_max && u > max.u));
    }

    return 0;
}

static int
aper_check_int_root_constraints(int64_t n, const asn1_int_info_t *info,
                                bool is_signed)
{
    return check_constraints(n, info->has_min, info->min, info->has_max,
                             info->max, is_signed);
}

static int
aper_check_int_ext_constraints(int64_t n, const asn1_int_info_t *info,
                               bool is_signed)
{
    return check_constraints(n, info->has_ext_min, info->ext_min,
                             info->has_ext_max, info->ext_max, is_signed);
}

int aper_encode_number(bb_t *bb, int64_t n, const asn1_int_info_t *info,
                       bool is_signed)
{
    if (aper_check_int_root_constraints(n, info, is_signed) < 0) {
        if (info->extended) {
            if (aper_check_int_ext_constraints(n, info, is_signed) < 0) {
                return e_error("extended constraint not respected");
            }

            /* Extension present */
            bb_be_add_bit(bb, true);

            /* XXX Extension constraints are not PER-visible */
            aper_write_number(bb, n, NULL);

            return 0;
        } else {
            e_error("root constraint not respected");
            return -1;
        }
    } else {
        if (info->extended) {
            /* Extension not present */
            bb_be_add_bit(bb, false);
        }
    }

    if (info->has_min) {
        aper_write_number(bb, n - info->min.i, info);
    } else { /* Only 2's-complement case */
        aper_write_2c_number(bb, n, is_signed);
    }

    return 0;
}

int aper_encode_enum(bb_t *bb, int32_t val, const asn1_enum_info_t *e)
{
    bool extended_val = false;
    int pos = asn1_enum_find_val(e, val, &extended_val);

    bb_push_mark(bb);

    if (pos < 0) {
        e_error("undeclared enumerated value: %d", val);
        return -1;
    }

    if (extended_val) {
        bb_be_add_bit(bb, true);
        aper_write_nsnnwn(bb, pos);

        return 0;
    }

    if (e->extended) {
        bb_be_add_bit(bb, false);
    }

    aper_write_number(bb, pos, &e->constraints);

    e_trace_be_bb_tail(5, bb, "enum value (value = %d)", val);
    bb_pop_mark(bb);

    return 0;
}

/* }}} */
/* String types {{{ */

int aper_encode_octet_string(bb_t *bb, lstr_t os, const asn1_cnt_info_t *info)
{
    aper_len_encoding_ctx_t ctx;
    pstream_t ps;
    bool align_before_data = true;

    if (aper_encode_len_extension_bit(bb, os.len, info, &ctx) < 0) {
        return -1;
    }

    if (info && info->max <= 2 && info->min == info->max
    &&  os.len == (int)info->max)
    {
        /* Short form: the string isn't realigned. */
        align_before_data = false;
    }

    ps = ps_initlstr(&os);
    do {
        aper_encode_len(bb, &ctx);
        if (align_before_data) {
            bb_align(bb);
        }
        bb_be_add_bytes(bb, ps.b, ctx.to_encode);
        __ps_skip(&ps, ctx.to_encode);
    } while (!ctx.done);


    return 0;
}

int aper_encode_bstring(bb_t *bb, const bit_stream_t *bits,
                        const asn1_cnt_info_t *info)
{
    bit_stream_t bs = *bits;
    size_t len = bs_len(&bs);
    aper_len_encoding_ctx_t ctx;

    if (aper_encode_len_extension_bit(bb, len, info, &ctx) < 0) {
        return -1;
    }
    do {
        bit_stream_t to_write;

        aper_encode_len(bb, &ctx);
        if (is_bstring_aligned(info, len)) {
            bb_align(bb);
        }
        if (!expect(bs_get_bs(&bs, ctx.to_encode, &to_write) >= 0)) {
            return e_error("bit string: unexpected length error");
        }
        bb_be_add_bs(bb, &to_write);
    } while (!ctx.done);

    return 0;
}

static int
aper_encode_bit_string(bb_t *bb, const asn1_bit_string_t *b,
                       const asn1_cnt_info_t *info)
{
    bit_stream_t bs = bs_init(b->data, 0, b->bit_len);

    return aper_encode_bstring(bb, &bs, info);
}

static ALWAYS_INLINE void aper_encode_bool(bb_t *bb, bool b)
{
    bb_be_add_bit(bb, b);
}

/* }}} */
/* Constructed types {{{ */

static int aper_encode_constructed(bb_t *bb, const void *st,
                                   const asn1_desc_t *desc,
                                   const asn1_field_t *field);

static int
aper_encode_value(bb_t *bb, const void *v, const asn1_field_t *field)
{
    switch (field->type) {
      case ASN1_OBJ_TYPE(bool):
        aper_encode_bool(bb, *(const bool *)v);
        break;
#define ASN1_ENCODE_INT_CASE(type_t, is_signed)                              \
      case ASN1_OBJ_TYPE(type_t):                                            \
        return aper_encode_number(bb, *(type_t *)v, &field->int_info,        \
                                  (is_signed));
      ASN1_ENCODE_INT_CASE(int8_t, true);
      ASN1_ENCODE_INT_CASE(uint8_t, false);
      ASN1_ENCODE_INT_CASE(int16_t, true);
      ASN1_ENCODE_INT_CASE(uint16_t, false);
      ASN1_ENCODE_INT_CASE(int32_t, true);
      ASN1_ENCODE_INT_CASE(uint32_t, false);
      ASN1_ENCODE_INT_CASE(int64_t, true);
      ASN1_ENCODE_INT_CASE(uint64_t, false);
#undef ASN1_ENCODE_INT_CASE
      case ASN1_OBJ_TYPE(enum):
        return aper_encode_enum(bb, *(int32_t *)v, field->enum_info);
      case ASN1_OBJ_TYPE(NULL):
      case ASN1_OBJ_TYPE(OPT_NULL):
        break;
      case ASN1_OBJ_TYPE(lstr_t):
        return aper_encode_octet_string(bb, *(const lstr_t *)v,
                                        &field->str_info);
      case ASN1_OBJ_TYPE(asn1_bit_string_t):
        return aper_encode_bit_string(bb, (const asn1_bit_string_t *)v,
                                      &field->str_info);
      case ASN1_OBJ_TYPE(SEQUENCE): case ASN1_OBJ_TYPE(CHOICE):
      case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
        return aper_encode_constructed(bb, v, field->u.comp, field);
      case ASN1_OBJ_TYPE(asn1_ext_t):
        assert (0);
        e_error("ext type not supported");
        break;
      case ASN1_OBJ_TYPE(OPAQUE):
        assert (0);
        e_error("opaque type not supported");
        break;
      case ASN1_OBJ_TYPE(SKIP):
        e_error("skip not supported"); /* We cannot stand squirrels */
        break;
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        e_error("open type not supported");
        break;
    }

    return 0;
}

static int
aper_encode_field(bb_t *bb, const void *v, const asn1_field_t *field)
{
    int res;

    e_trace(5, "encoding value %s:%s", field->oc_t_name, field->name);

    bb_push_mark(bb);

    if (field->is_open_type || field->is_extension) {
        lstr_t  os;
        bb_t    buf;

        bb_inita(&buf, field->open_type_buf_len);
        aper_encode_value(&buf, v, field);

        if (!buf.len) {
            bb_be_add_byte(&buf, 0);
        }

        os = LSTR_INIT_V((const char *)buf.bytes, DIV_ROUND_UP(buf.len, 8));
        res = aper_encode_octet_string(bb, os, NULL);
        bb_wipe(&buf);
    } else {
        res = aper_encode_value(bb, v, field);
    }

    e_trace_be_bb_tail(5, bb, "value encoding for %s:%s",
                    field->oc_t_name, field->name);
    bb_pop_mark(bb);

    return res;
}

static void field_bitmap_add_bit(bb_t *bitmap, const void *st,
                                 const asn1_field_t *field, bool *present)
{
    const void *opt;
    const void *val;
    bool field_present;

    assert (field->mode == ASN1_OBJ_MODE(OPTIONAL));
    opt = GET_DATA_P(st, field, uint8_t);
    val = asn1_opt_field(opt, field->type);
    field_present = !!val;

    /* Add bit '1' if the field is present, '0' otherwise. */
    bb_be_add_bit(bitmap, field_present);

    *present = field_present;
}

static uint16_t
fill_ext_bitmap(const void *st, const asn1_desc_t *desc, bb_t *bb)
{
    uint16_t fields_cnt = 0;

    for (int i = desc->ext_pos; i < desc->fields.len; i++) {
        const asn1_field_t *field = &desc->fields.tab[i];
        bool field_present;

        field_bitmap_add_bit(bb, st, field, &field_present);
        fields_cnt += field_present;
    }

    return fields_cnt;
}

static uint16_t
fill_opt_bitmap(const void *st, const asn1_desc_t *desc, bb_t *bb)
{
    uint16_t fields_cnt = 0;

    tab_for_each_entry(field_pos, &desc->opt_fields) {
        const asn1_field_t *field = &desc->fields.tab[field_pos];
        bool field_present;

        field_bitmap_add_bit(bb, st, field, &field_present);
        fields_cnt += field_present;
    }

    return fields_cnt;
}

static int
aper_encode_sequence(bb_t *bb, const void *st, const asn1_desc_t *desc)
{
    BB(ext_bb, desc->fields.len - desc->ext_pos);
    const void *v;
    bool extended_fields_reached = false;

    if (desc->is_extended) {
        uint16_t ext_fields_cnt;

        ext_fields_cnt = fill_ext_bitmap(st, desc, &ext_bb);

#ifndef NDEBUG
        {
            t_scope;
            const char *bits = t_print_be_bb(&ext_bb, NULL);

            e_trace(5, "extension bitmap = [ %s ]", bits);
        }
#endif

        /* Put extension bit */
        e_trace(5, "sequence is extended (extension bit = %d)",
                !!ext_fields_cnt);
        bb_be_add_bit(bb, !!ext_fields_cnt);
    }

    bb_push_mark(bb);

    /* Encode optional fields bit-map */
    fill_opt_bitmap(st, desc, bb);

    e_trace_be_bb_tail(5, bb, "SEQUENCE OPTIONAL fields bit-map");
    bb_pop_mark(bb);

    for (int i = 0; i < desc->fields.len; i++) {
        const asn1_field_t *field = &desc->fields.tab[i];

        assert (field->mode != ASN1_OBJ_MODE(SEQ_OF));

        if (field->mode == ASN1_OBJ_MODE(OPTIONAL)) {
            const void *opt = GET_DATA_P(st, field, uint8_t);

            v = asn1_opt_field(opt, field->type);

            if (v == NULL) {
                continue; /* XXX field not present */
            }
        } else {
            v = GET_DATA_P(st, field, uint8_t);
        }

        if (!extended_fields_reached && field->is_extension) {
            bit_stream_t ext_bs = bs_init_bb(&ext_bb);

            bb_push_mark(bb);

            /* First extension field reached, write presence bitmap for fields
             * to come. */
            extended_fields_reached = true;
            aper_write_nsnnwn(bb, bs_len(&ext_bs) - 1);
            e_trace_be_bb_tail(5, bb, "extension bitmap length (l=%zd)",
                               bs_len(&ext_bs));
            bb_be_add_bs(bb, &ext_bs);

            e_trace_be_bb_tail(5, bb, "extension bitmap");
            bb_pop_mark(bb);
        }

        if (aper_encode_field(bb, v, field) < 0) {
            e_error("failed to encode value %s:%s", field->oc_t_name,
                    field->name);
            goto error;
        }
    }

    bb_wipe(&ext_bb);
    return 0;

  error:
    bb_wipe(&ext_bb);
    return -1;
}

static int
aper_encode_choice(bb_t *bb, const void *st, const asn1_desc_t *desc)
{
    const asn1_field_t *choice_field;
    const asn1_field_t *enum_field;
    int index;
    const void *v;
    bool extension_present = false;

    assert (desc->fields.len > 1);

    enum_field = &desc->fields.tab[0];

    index = __asn1_get_int(st, enum_field);
    if (index < 1) {
        return e_error("wrong choice initialization");
    }
    e_trace(5, "index = %d", index);
    choice_field = &desc->fields.tab[index];
    assert (choice_field->mode == ASN1_OBJ_MODE(MANDATORY));

    /* Put extension bit */
    if (desc->is_extended) {
        e_trace(5, "choice is extended");

        if ((extension_present = index >= desc->ext_pos)) {
            e_trace(5, "extension is present");
        } else {
            e_trace(5, "extension is not present");
        }

        bb_be_add_bit(bb, extension_present);
    }

    bb_push_mark(bb);

    if (extension_present) {
        aper_write_nsnnwn(bb, index - desc->ext_pos);
    } else {
        /* XXX Indexes start from 0 */
        aper_write_number(bb, index - 1, &desc->choice_info);
    }

    e_trace_be_bb_tail(5, bb, "CHOICE index");
    bb_pop_mark(bb);

    v = GET_DATA_P(st, choice_field, uint8_t);
    assert (v);

    if (aper_encode_field(bb, v, choice_field) < 0) {
        return e_error("failed to encode choice element %s:%s",
                       choice_field->oc_t_name, choice_field->name);
    }

    return 0;
}

static int
aper_encode_seq_of_field(bb_t *bb, const asn1_field_t *field,
                         const uint8_t *tab, int start, int end)
{
    size_t field_sz = field->pointed ? sizeof(const void *) : field->size;

    for (int i = start; i < end; i++) {
        if (aper_encode_field(bb, tab + i * field_sz, field) < 0) {
            e_error("failed to encode array value [%d] %s:%s",
                    i, field->oc_t_name, field->name);

            return -1;
        }
    }

    return 0;
}

static int
aper_encode_seq_of(bb_t *bb, const void *st, const asn1_field_t *field)
{
    const asn1_field_t *repeated_field;
    const asn1_desc_t *desc = field->u.comp;
    const asn1_void_vector_t *tab;
    aper_len_encoding_ctx_t ctx;
    int offset;

    assert (desc->fields.len == 1);
    repeated_field = &desc->fields.tab[0];

    assert (repeated_field->mode == ASN1_OBJ_MODE(SEQ_OF));

    tab = GET_CONST_PTR(st, asn1_void_vector_t, repeated_field->offset);

    if (aper_encode_len_extension_bit(bb, tab->len, &field->seq_of_info,
                                      &ctx) < 0)
    {
        return -1;
    }

    offset = 0;
    do {
        aper_encode_len(bb, &ctx);

        /* Check for overflow. */
        assert(offset + ctx.to_encode <= tab->len);

        RETHROW(aper_encode_seq_of_field(bb, repeated_field, tab->data,
                                         offset, offset + ctx.to_encode));
        offset += ctx.to_encode;
    } while (!ctx.done);

    return 0;
}

/* TODO get it cleaner */
static int aper_encode_constructed(bb_t *bb, const void *st,
                                   const asn1_desc_t *desc,
                                   const asn1_field_t *field)
{
    if (desc->is_seq_of) {
        assert (field);
        assert (desc == field->u.comp);

        if (aper_encode_seq_of(bb, st, field) < 0) {
            return e_error("failed to encode sequence of values");
        }

        return 0;
    }

    switch (desc->type) {
      case ASN1_CSTD_TYPE_SEQUENCE:
        return aper_encode_sequence(bb, st, desc);
      case ASN1_CSTD_TYPE_CHOICE:
        return aper_encode_choice(bb, st, desc);
      case ASN1_CSTD_TYPE_SET:
        e_error("ASN.1 SET not supported yet");
        /* FALLTHROUGH */
      default:
        return -1;
    }
}

int aper_encode_desc(sb_t *sb, const void *st, const asn1_desc_t *desc)
{
    bb_t bb;
    int res;

    bb_init_sb(&bb, sb);

    res = aper_encode_constructed(&bb, st, desc, NULL);
    bb_transfer_to_sb(&bb, sb);

    if (res >= 0) {
        /* Ref : [1] 10.1.3 */
        if (unlikely(!sb->len)) {
            sb_addc(sb, 0);
        }
    }

    bb_wipe(&bb);
    return res;
}

/* }}} */

/* }}} */

/* }}} */
/* Read {{{ */

#define e_info(fmt, ...)  \
    if (_G.decode_log_level < 0)                                             \
        e_info(fmt, ##__VA_ARGS__);                                          \
    else                                                                     \
        e_trace(_G.decode_log_level, fmt, ##__VA_ARGS__);

void aper_set_decode_log_level(int level)
{
    _G.decode_log_level = level;
}

/* Helpers {{{ */

int aper_read_u16_m(bit_stream_t *bs, size_t blen, uint16_t d_max,
                    uint16_t *u16)
{
    uint64_t res;
    assert (blen); /* u16 is given by constraints */

    if (blen == 8 && d_max == 255) {
        /* "The one-octet case". */
        *u16 = 0;
        if (aper_read_aligned_u8(bs, (uint8_t *)u16) < 0) {
            e_info("cannot read contrained integer: end of input "
                   "(expected at least one aligned octet)");
            return -1;
        }

        return 0;
    }

    if (blen <= 8) {
        /* "The bit-field case". */
        if (bs_be_get_bits(bs, blen, &res) < 0) {
            e_info("not enough bits to read constrained integer "
                   "(got %zd, need %zd)", bs_len(bs), blen);
            return -1;
        }

        *u16 = res;
        return 0;
    }

    /* "The two-octet case". */
    if (aper_read_aligned_u16(bs, u16) < 0) {
        e_info("cannot read constrained integer: end of input "
               "(expected at least two aligned octet left)");
        return -1;
    }
    return 0;
}

static ALWAYS_INLINE int
aper_read_ulen(bit_stream_t *bs, size_t *l, bool *nullable is_fragmented)
{
    uint64_t len = 0;

    /* XXX Same remark as for "need_fragmentation" in aper_write_len(). */
    assert (!is_fragmented || !*is_fragmented);

    if (bs_align(bs) < 0 || !bs_has(bs, 8)) {
        e_info("cannot read unconstrained length: end of input "
               "(expected at least one aligned octet left)");
        return -1;
    }

    len = __bs_be_peek_bits(bs, 8);
    if (!(len & (1 << 7))) {
        __bs_skip(bs, 8);
        *l = len;
        return 0;
    }

    if (len & (1 << 6)) {
        if (is_fragmented) {
            *is_fragmented = true;
            return 0;
        }
        e_info("cannot read unconstrained length: "
               "fragmented values are not supported");
        return -1;
    }

    if (bs_be_get_bits(bs, 16, &len) < 0) {
        e_info("cannot read unconstrained length: end of input "
               "(expected at least a second octet left)");
        return -1;
    }

    *l = len & 0x7fff;
    return 0;
}

static ALWAYS_INLINE int
aper_read_2c_number(bit_stream_t *bs, int64_t *v, bool is_signed)
{
    size_t olen;

    if (aper_read_ulen(bs, &olen, NULL) < 0) {
        e_info("cannot read unconstrained whole number length");
        return -1;
    }

    /* XXX Handle the special case of unsigned 64-bits integers
     * in [ INT64_MAX + 1, UINT64_MAX ]. */
    if (olen == 9 && !is_signed) {
        uint8_t o = 0;
        uint64_t u;

        if (aper_read_aligned_u8(bs, &o) < 0) {
            goto not_enough_bytes;
        }

        if (o) {
            goto overflow;
        }

        if (aper_read_aligned_uint(bs, 8, &u) < 0) {
            goto not_enough_bytes;
        }

        *v = u;
        return 0;
    }

    if (olen > 8) {
        goto overflow;
    }

    if (aper_read_aligned_int(bs, olen, v) < 0) {
        goto not_enough_bytes;
    }

    if (!is_signed && *v < 0) {
        e_info("cannot write negative number to unsigned integer");
        return -1;
    }

    return 0;

  not_enough_bytes:
    e_info("not enough bytes to read unconstrained number "
           "(got %zd, need %zd)", bs_len(bs) / 8, olen);
    return -1;

  overflow:
    e_info("the number is too big not to overflow");
    return -1;
}

static ALWAYS_INLINE int
aper_read_number(bit_stream_t *bs, const asn1_int_info_t *info, uint64_t *v)
{
    size_t olen;

    if (info && info->has_min && info->has_max) {
        if (info->max_blen <= 16) {
            uint16_t u16 = 0;

            if (info->max_blen == 0) {
                *v = 0;
                return 0;
            }

            if (aper_read_u16_m(bs, info->max_blen, info->d_max, &u16) < 0) {
                e_info("cannot read constrained whole number");
                return -1;
            }

            *v = u16;

            return 0;
        } else {
            uint16_t u16 = 0;

            if (aper_read_u16_m(bs, info->max_olen_blen, info->d_max,
                                &u16) < 0)
            {
                e_info("cannot read constrained whole number length");
                return -1;
            }

            olen = u16 + 1;
        }
    } else {
        if (aper_read_ulen(bs, &olen, NULL) < 0) {
            e_info("cannot read semi-constrained whole number length");
            return -1;
        }
    }

    if (!olen) {
        e_info("forbidden number length value : 0");
        return -1;
    }
    if (olen > sizeof(v)) {
        e_info("number encoding is too big not to overflow");
        return -1;
    }

    if (aper_read_aligned_uint(bs, olen, v) < 0) {
        e_info("not enough bytes to read number (got %zd, need %zd)",
               bs_len(bs) / 8, olen);
        return -1;
    }

    return 0;
}

int aper_read_nsnnwn(bit_stream_t *bs, size_t *n)
{
    bool is_short;
    uint64_t u64;

    if (bs_done(bs)) {
        e_info("cannot read NSNNWN: end of input");
        return -1;
    }

    is_short = !__bs_be_get_bit(bs);

    if (is_short) {
        if (!bs_has(bs, 6)) {
            e_info("cannot read short NSNNWN: not enough bits");
            return -1;
        }

        *n = __bs_be_get_bits(bs, 6);

        return 0;
    }

    if (aper_read_number(bs, NULL, &u64) < 0) {
        e_info("cannot read long form NSNNWN");
        return -1;
    }

    *n = u64;

    return 0;
}

int aper_read_len(bit_stream_t *bs, size_t l_min, size_t l_max, size_t *l,
                  bool *nullable is_fragmented)
{
    size_t d_max = l_max - l_min;

    if (d_max < (1 << 16)) {
        uint16_t d;

        if (!d_max) {
            *l = l_min;

            return 0;
        }

        if (aper_read_u16_m(bs, u16_blen(d_max), d_max, &d) < 0) {
            e_info("cannot read constrained length");
            return -1;
        }

        *l = l_min + d;
    } else {
        if (aper_read_ulen(bs, l, is_fragmented) < 0) {
            e_info("cannot read unconstrained length");
            return -1;
        }
    }

    if (*l > l_max) {
        e_info("length is too high");
        return -1;
    }

    return 0;
}

/* }}} */
/* Front End Decoders {{{ */

/* Scalar types {{{ */

/* Contextual information about length decoding. */
typedef struct aper_len_decoding_ctx_t {
    /* The min/max length depends on the presence of the extension bit. */
    size_t min_len;
    size_t max_len;

    /* Cumulated length of all fragments read.
     * Same as 'to_decode' when there is no fragmentation. */
    uint64_t cumulated_len;

    /* Number of items to read next.
     * Can be the full length or a fragment length. */
    uint32_t to_decode;

    bool extension_present;

    /* If set, then the decoding of the fragments is up to the caller, only
     * the extension bit is expected to be consumed at this point. */
    bool more_fragments_to_read;
} aper_len_decoding_ctx_t;

GENERIC_INIT(aper_len_decoding_ctx_t, aper_len_decoding_ctx);

static int
aper_len_check_max(const aper_len_decoding_ctx_t *ctx, uint64_t len)
{
    if (len > ctx->max_len) {
        e_info("%s maximum length constraint exceeded",
               ctx->extension_present ? "extended" : "root");
        return -1;
    }
    return 0;
}

static int
aper_len_check_min(const aper_len_decoding_ctx_t *ctx, uint64_t len)
{
    if (len < ctx->min_len) {
        e_info("%s minimum length constraint unmet",
               ctx->extension_present ? "extended" : "root");
        return -1;
    }
    return 0;
}

static int
aper_len_check_constraints(const aper_len_decoding_ctx_t *ctx, size_t len)
{
    RETHROW(aper_len_check_min(ctx, len));
    RETHROW(aper_len_check_max(ctx, len));

    return 0;
}

static int
aper_decode_fragment_len(bit_stream_t *bs, aper_len_decoding_ctx_t *ctx)
{
    uint64_t len;

    assert(ctx->more_fragments_to_read);

    if (bs_align(bs) < 0 || !bs_has(bs, 8)) {
        e_info("cannot read fragment len: unexpected end of input");
        return -1;
    }

    len = __bs_peek_bits(bs, 8);
    if ((len & 0xc0) == 0xc0) {
        /* Got a 16k, 32k, 48k or 64k block fragment. */
        if (ctx->to_decode != 0 && ctx->to_decode != PER_FRAG_64K) {
            /* Each block fragment except the last one should be a 64k block.
             * This fragment isn't the first (ctx->len != 0) and the previous
             * fragment wasn't a 64k block fragment, so the rule is broken. */
            e_info("unexpected >16k fragment block");
            return -1;
        }

        len &= ~0xc0;
        if (!len) {
            e_info("unexpected empty fragment block");
            return -1;
        }
        if (len > 4) {
            e_info("unexpected >64k fragment block length");
            return -1;
        }

        len *= PER_FRAG_16K;
        __bs_skip(bs, 8);
    } else {
        /* Remainder. */
        if (aper_read_ulen(bs, &len, NULL) < 0) {
            e_info("cannot read remainder length");
            return -1;
        }

        ctx->more_fragments_to_read = false;
    }

    ctx->cumulated_len += len;

    /* Check the max length isn't exceeded before any further decoding. */
    RETHROW(aper_len_check_max(ctx, ctx->cumulated_len));

    if (ctx->to_decode < PER_FRAG_16K) {
        /* Reached last fragment. The minimum length can be checked now. */
        RETHROW(aper_len_check_min(ctx, ctx->cumulated_len));
    }
    ctx->to_decode = len;

    return 0;
}

static void aper_buf_wipe(qv_t(u8) *buf)
{
    qv_wipe(buf);
}

static uint8_t *aper_buf_growlen(qv_t(u8) *buf, int extra)
{
    if (buf->mp == t_pool() && expect(!buf->size)) {
        /* XXX qv_growlen() allocates more than needed and we want to allocate
         * exactly what we need when using the t_pool. We only accept
         * allocation more than needed when using a temporary buffer. */
        t_qv_init(buf, extra);
    }

    return qv_growlen(buf, extra);
}

/* Read extension bit (if any) and resolve min/max length. */
static int
aper_decode_len_extension_bit(bit_stream_t *bs, const asn1_cnt_info_t *info,
                              aper_len_decoding_ctx_t *ctx)
{
    aper_len_decoding_ctx_init(ctx);

    if (info) {
        if (info->extended) {
            if (bs_done(bs)) {
                e_info("cannot read extension bit: end of input");
                return -1;
            }

            ctx->extension_present = __bs_be_get_bit(bs);
        }

        if (ctx->extension_present) {
            ctx->min_len = info->ext_min;
            ctx->max_len = info->ext_max;
        } else {
            ctx->min_len = info->min;
            ctx->max_len = info->max;
        }
    } else {
        ctx->max_len = SIZE_MAX;
    }

    return 0;
}

/* Decode a length. The 'ctx' parameter should be initialized by
 * 'aper_decode_len_extension_bit()' first. */
static int aper_decode_len(bit_stream_t *bs, aper_len_decoding_ctx_t *ctx)
{
    bool is_fragmented = false;
    size_t l = 0;

    if (ctx->more_fragments_to_read) {
        return aper_decode_fragment_len(bs, ctx);
    }
    if (ctx->extension_present) {
        if (aper_read_ulen(bs, &l, &is_fragmented) < 0) {
            e_info("cannot read extended length");
            return -1;
        }
    } else {
        if (aper_read_len(bs, ctx->min_len, ctx->max_len, &l,
                          &is_fragmented) < 0)
        {
            e_info("cannot read constrained length");
            return -1;
        }
    }
    if (is_fragmented) {
        ctx->more_fragments_to_read = true;
        RETHROW(aper_decode_fragment_len(bs, ctx));
    } else {
        ctx->to_decode = l;
        ctx->cumulated_len = ctx->to_decode;
        RETHROW(aper_len_check_constraints(ctx, ctx->cumulated_len));
    }

    return 0;
}

int aper_decode_number(bit_stream_t *nonnull bs,
                       const asn1_int_info_t *nonnull info, bool is_signed,
                       int64_t *nonnull n)
{
    int64_t res = 0;

    if (info->extended) {
        bool extension_present;

        if (bs_done(bs)) {
            e_info("cannot read extension bit: end of input");
            return -1;
        }

        extension_present = __bs_be_get_bit(bs);

        if (extension_present) {
            if (aper_read_2c_number(bs, n, is_signed) < 0) {
                e_info("cannot read extended unconstrained number");
                return -1;
            }

            if (aper_check_int_ext_constraints(*n, info, is_signed) < 0) {
                e_info("extension constraint not respected");
                return -1;
            }

            return 0;
        }
    }

    if (info->has_min) {
        uint64_t d;

        if (aper_read_number(bs, info, &d) < 0) {
            e_info("cannot read constrained or semi-constrained number");
            return -1;
        }

        if (is_signed) {
            if (d > (uint64_t)INT64_MAX - info->min.i) {
                e_info("cannot decode: overflow of signed 64-bits integer");
                return -1;
            }

            res = info->min.i + d;
        } else {
            if (d > (uint64_t)UINT64_MAX - info->min.u) {
                e_info("cannot decode: overflow of unsigned 64-bits integer");
                return -1;
            }

            res = info->min.u + d;
        }
    } else {
        if (aper_read_2c_number(bs, &res, is_signed) < 0) {
            e_info("cannot read unconstrained number");
            return -1;
        }
    }

    if (aper_check_int_root_constraints(res, info, is_signed) < 0) {
        e_info("root constraint not respected");
        return -1;
    }

    *n = res;

    return 0;
}

int aper_decode_enum(bit_stream_t *bs, const asn1_enum_info_t *e,
                     int32_t *val)
{
    int64_t pos;

    if (e->extended) {
        if (bs_done(bs)) {
            e_info("cannot read enumerated type: end of input");
            return -1;
        }

        if (__bs_be_get_bit(bs)) {
            size_t nsnnwn;

            if (aper_read_nsnnwn(bs, &nsnnwn) < 0) {
                e_info("cannot read extended enumeration");
                return -1;
            }

            if (nsnnwn >= (size_t)e->ext_values.len) {
                if (OPT_ISSET(e->ext_defval)) {
                    e_trace(5, "unknown extended enum value, use default");

                    *val = OPT_VAL(e->ext_defval);

                    return 0;
                }

                e_info("cannot read enumerated value (extended): "
                       "unregistered value");

                return -1;
            }

            *val = e->ext_values.tab[nsnnwn];

            return 0;
        }
    }

    RETHROW(aper_decode_number(bs, &e->constraints, true, &pos));

    if (pos >= e->values.len) {
        e_info("cannot read enumerated value (root): unregistered value");
        return -1;
    }

    *val = e->values.tab[pos];

    return 0;
}

static ALWAYS_INLINE int aper_decode_bool(bit_stream_t *bs, bool *b)
{
    if (bs_done(bs)) {
        e_info("cannot decode boolean: end of input");
        return -1;
    }

    *b = __bs_be_get_bit(bs);

    return 0;
}

/* }}} */
/* String types {{{ */

int t_aper_decode_octet_string(bit_stream_t *bs, const asn1_cnt_info_t *info,
                               bool copy, lstr_t *os)
{
    aper_len_decoding_ctx_t len_ctx;
    qv_t(u8) buf __attribute__((cleanup(aper_buf_wipe))) = QV_INIT();

    if (aper_decode_len_extension_bit(bs, info, &len_ctx) < 0) {
        e_info("cannot read extension bit");
        return -1;
    }

    do {
        lstr_t data;

        if (aper_decode_len(bs, &len_ctx) < 0) {
            e_info("cannot decode octet string length");
            return -1;
        }
        if (!buf.len && info && info->max <= 2 &&
            info->min == info->max && len_ctx.to_decode == info->max)
        {
            /* Special case: unaligned fixed-size octet string
             * (size 1 or 2). */
        } else {
            bs_align(bs);
        }

        if (t_aper_get_unaligned_bytes(bs, len_ctx.to_decode, copy,
                                       &data) < 0)
        {
            e_info("cannot read octet string: not enough bits");
            return -1;
        }
        if (buf.len) {
            memcpy(qv_growlen(&buf, len_ctx.to_decode),
                   data.data, len_ctx.to_decode);
        } else {
            qv_init_static(&buf, data.data, data.len);
        }
    } while (len_ctx.more_fragments_to_read);

    if (likely(buf.mp == &mem_pool_libc)) {
        /* XXX There were more than one fragment so the buffer was reallocated
         * on LIBC. The content has to be transferred on the t_stack or it
         * will be lost. */
        copy = true;
    }
    *os = LSTR_DATA_V(buf.tab, buf.len);
    if (copy) {
        *os = t_lstr_dup(*os);
    }

    e_trace_hex(6, "Decoded OCTET STRING", os->data, (int)os->len);

    return 0;
}

static int
t_aper_decode_data(bit_stream_t *bs, const asn1_cnt_info_t *info,
                   bool copy, lstr_t *data)
{
    lstr_t os;

    RETHROW(t_aper_decode_octet_string(bs, info, copy, &os));

    *data = os;

    return 0;
}

int aper_decode_bstring(bit_stream_t *bs, const asn1_cnt_info_t *info,
                        bb_t *bit_string)
{
    aper_len_decoding_ctx_t len_ctx;

    if (aper_decode_len_extension_bit(bs, info, &len_ctx) < 0) {
        e_info("cannot read extension bit");
        return -1;
    }
    do {
        bit_stream_t bit_string_bs;

        if (aper_decode_len(bs, &len_ctx) < 0) {
            e_info("cannot decode bit string length");
            return -1;
        }
        if (is_bstring_aligned(info, len_ctx.to_decode) && bs_align(bs) < 0) {
            e_info("cannot read bit string: not enough bits for padding");
            return -1;
        }
        if (bs_get_bs(bs, len_ctx.to_decode, &bit_string_bs) < 0) {
            e_info("cannot read bit string: not enough bits");
            return -1;
        }
        e_trace_be_bs(6, &bit_string_bs, "Decoded bit string");
        bb_be_add_bs(bit_string, &bit_string_bs);
    } while (len_ctx.more_fragments_to_read);

    return 0;
}

static int
t_aper_decode_bit_string(bit_stream_t *bs, const asn1_cnt_info_t *info,
                         asn1_bit_string_t *bit_string)
{
    BB_1k(bb __attribute__((cleanup(bb_wipe))));
    uint8_t *data;
    size_t size;

    RETHROW(aper_decode_bstring(bs, info, &bb));
    size = DIV_ROUND_UP(bb.len, 8);
    data = t_dup(bb.bytes, size);
    *bit_string = ASN1_BIT_STRING(data, bb.len);

    return 0;
}

/* }}} */
/* Constructed types {{{ */

static int
t_aper_decode_constructed(bit_stream_t *bs, const asn1_desc_t *desc,
                          const asn1_field_t *field, bool copy, void *st);

static int
t_aper_decode_value(bit_stream_t *bs, const asn1_field_t *field,
                    bool copy, void *v)
{
    switch (field->type) {
      case ASN1_OBJ_TYPE(bool):
        return aper_decode_bool(bs, (bool *)v);
        break;

#define ASN1_DECODE_INT_CASE(type_t, type64_t, is_signed)  \
      case ASN1_OBJ_TYPE(type_t):                                            \
        {                                                                    \
            int64_t i64;                                                     \
                                                                             \
            RETHROW(aper_decode_number(bs, &field->int_info, (is_signed),    \
                                       &i64));                               \
            e_trace(5, "decoded number value (n = %jd)", i64);               \
                                                                             \
            if ((type64_t)i64 != (type_t)i64) {                              \
                e_info("overflow detected for field `%s` (" #type_t ")",     \
                       field->name);                                         \
                return -1;                                                   \
            }                                                                \
            *(type_t *)v = i64;                                              \
        }                                                                    \
        return 0;

      ASN1_DECODE_INT_CASE(int8_t, int64_t, true);
      ASN1_DECODE_INT_CASE(uint8_t, uint64_t, false);
      ASN1_DECODE_INT_CASE(int16_t, int64_t, true);
      ASN1_DECODE_INT_CASE(uint16_t, uint64_t, false);
      ASN1_DECODE_INT_CASE(int32_t, int64_t, true);
      ASN1_DECODE_INT_CASE(uint32_t, uint64_t, false);
      ASN1_DECODE_INT_CASE(int64_t, int64_t, true);
      ASN1_DECODE_INT_CASE(uint64_t, uint64_t, false);

#undef ASN1_DECODE_INT_CASE

      case ASN1_OBJ_TYPE(enum):
        RETHROW(aper_decode_enum(bs, field->enum_info, (int32_t *)v));
        e_trace(5, "decoded enum value (n = %u)", *(int32_t *)v);
        return 0;
      case ASN1_OBJ_TYPE(NULL):
      case ASN1_OBJ_TYPE(OPT_NULL):
        break;
      case ASN1_OBJ_TYPE(lstr_t):
        return t_aper_decode_data(bs, &field->str_info, copy,
                                  (lstr_t *)v);
      case ASN1_OBJ_TYPE(asn1_bit_string_t):
        return t_aper_decode_bit_string(bs, &field->str_info,
                                        (asn1_bit_string_t *)v);
      case ASN1_OBJ_TYPE(SEQUENCE): case ASN1_OBJ_TYPE(CHOICE):
      case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
        return t_aper_decode_constructed(bs, field->u.comp, field, copy, v);
      case ASN1_OBJ_TYPE(asn1_ext_t):
        assert (0);
        e_error("ext type not supported");
        break;
      case ASN1_OBJ_TYPE(OPAQUE):
        assert (0);
        e_error("opaque type not supported");
        break;
      case ASN1_OBJ_TYPE(SKIP):
        break;
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        e_error("open type not supported");
        break;
    }

    return 0;
}

static int
t_aper_decode_field(bit_stream_t *bs, const asn1_field_t *field,
                    bool copy, void *v)
{
    if (field->is_open_type || field->is_extension) {
        lstr_t        os;
        bit_stream_t  open_type_bs;

        if (t_aper_decode_octet_string(bs, NULL, false, &os) < 0) {
            e_info("cannot read %s%sfield",
                   field->is_open_type ? "OPEN TYPE " : "",
                   field->is_extension ? "extension " : "");
            return -1;
        }

        open_type_bs = bs_init(os.data, 0, os.len * 8);

        return t_aper_decode_value(&open_type_bs, field, copy, v);
    }

    return t_aper_decode_value(bs, field, copy, v);
}

static void *t_alloc_if_pointed(const asn1_field_t *field, void *st)
{
    if (field->pointed) {
        return (*GET_PTR(st, field, void *) = t_new_raw(char, field->size));
    }

    return GET_PTR(st, field, void);
}

static int read_ext_bitmap(bit_stream_t *bs, bit_stream_t *ext_bitmap)
{
    size_t ext_bitmap_len;

    if (aper_read_nsnnwn(bs, &ext_bitmap_len) < 0) {
        e_info("cannot read extension bitmap length");
        return -1;
    }

    /* XXX The value "-1" is impossible so the encoded value is "n - 1". */
    ext_bitmap_len++;

    if (bs_get_bs(bs, ext_bitmap_len, ext_bitmap) < 0) {
        e_info("cannot read extension bitmap (not enough bits)");
        return -1;
    }

#ifndef NDEBUG
    {
        t_scope;
        const char *bits = t_print_be_bs(*ext_bitmap, NULL);

        e_trace(5, "extension bitmap = [ %s ]", bits);
    }
#endif

    return 0;
}

static int
t_aper_decode_sequence(bit_stream_t *bs, const asn1_desc_t *desc,
                       bool copy, void *st)
{
    bit_stream_t opt_bitmap;
    bit_stream_t ext_bitmap;
    bit_stream_t *fields_bitmap = &opt_bitmap;
    bool extension_present = false;
    bool extended_fields_reached = false;

    if (desc->is_extended) {
        e_trace(5, "the sequence is extended");

        if (bs_done(bs)) {
            e_info("cannot read extension bit: end of input");
            return -1;
        }

        extension_present = __bs_be_get_bit(bs);
        e_trace(5, "extension present");
    }

    if (!bs_has(bs, desc->opt_fields.len)) {
        e_info("cannot read optional fields bit-map: not enough bits");
        return -1;
    }

    opt_bitmap = __bs_get_bs(bs, desc->opt_fields.len);

    for (int i = 0; i < desc->fields.len; i++) {
        const asn1_field_t *field = &desc->fields.tab[i];
        void *v;

        if (!extended_fields_reached && field->is_extension) {
            extended_fields_reached = true;

            if (extension_present) {
                e_trace(5, "extended fields reached, read extension bitmap");

                if (read_ext_bitmap(bs, &ext_bitmap) < 0) {
                    e_info("cannot read extension bitmap");
                    return -1;
                }

                fields_bitmap = &ext_bitmap;
            }
        }

        if (field->mode == ASN1_OBJ_MODE(OPTIONAL)) {
            if (bs_done(fields_bitmap)) {
                if (likely(extended_fields_reached)) {
                    e_trace(5, "extended field `%s:%s` not present "
                            "(out of bitmap range)", field->oc_t_name,
                            field->name);

                    /* Extended field not present (out of extension bitmap
                     * range). */
                    asn1_opt_field_w(GET_PTR(st, field, void), field->type,
                                     false);
                    continue;
                }

                assert (0);
                return e_error("sequence is broken");
            }

            if (!__bs_be_get_bit(fields_bitmap)) {
                e_trace(5, "field `%s:%s` not present", field->oc_t_name,
                        field->name);

                /* Extended field not present (bit=0 in extension bitmap). */
                asn1_opt_field_w(GET_PTR(st, field, void), field->type,
                                 false);
                continue;
            }

            t_alloc_if_pointed(field, st);
            v = asn1_opt_field_w(GET_PTR(st, field, void), field->type, true);
        } else {
            assert (field->mode != ASN1_OBJ_MODE(SEQ_OF));

            /* Should be checked in "asn1_reg_field()". */
            assert (!field->is_extension);

            v = t_alloc_if_pointed(field, st);
        }

        e_trace(5, "decoding SEQUENCE value %s:%s",
                field->oc_t_name, field->name);

        if (t_aper_decode_field(bs, field, copy, v) < 0) {
            e_info("cannot read sequence field %s:%s",
                   field->oc_t_name, field->name);
            return -1;
        }
    }

    if (extension_present) {
        if (!extended_fields_reached) {
            e_trace(5, "skipping extension bitmap");

            /* The sequence is registered as extended but no extended field is
             * registered. Skip the extended field bitmap. */
            if (read_ext_bitmap(bs, &ext_bitmap) < 0) {
                e_info("cannot read extension bitmap (for skipping)");
                return -1;
            }
        }

        /* Skip all the unknown extended fields. */
        while (!bs_done(&ext_bitmap)) {
            lstr_t os;

            if (!__bs_be_get_bit(&ext_bitmap)) {
                e_trace(5, "skipping unknown extension (absent)");
                continue;
            }

            e_trace(5, "skipping unknown extension (present)");
            if (t_aper_decode_octet_string(bs, NULL, false, &os) < 0) {
                e_info("cannot skip unknown extension field");
                return -1;
            }
            e_trace(5, "skipped unknown extension with encoding size %d",
                    os.len);
        }
    }

    return 0;
}

static int
t_aper_decode_choice(bit_stream_t *bs, const asn1_desc_t *desc, bool copy,
                     void *st)
{
    const asn1_field_t  *choice_field;
    const asn1_field_t  *enum_field;
    uint64_t             u64;
    size_t               index;
    void                *v;
    bool extension_present = false;

    if (desc->is_extended) {
        if (bs_done(bs)) {
            e_info("cannot read extension bit: end of input");
            return -1;
        }

        extension_present = __bs_be_get_bit(bs);
        if (extension_present) {
            e_trace(5, "extension present");
        } else {
            e_trace(5, "extension not present");
        }
    } else {
        e_trace(5, "choice is not extended");
    }

    if (extension_present) {
        if (aper_read_nsnnwn(bs, &index)) {
            e_info("cannot read choice extension index");
            return -1;
        }

        if (index + desc->ext_pos >= (size_t)desc->fields.len) {
            e_info("unknown choice extension (index = %zd)", index);
            return -1;
        }

        index += desc->ext_pos;
    } else {
        if (aper_read_number(bs, &desc->choice_info, &u64) < 0) {
            e_info("cannot read choice index");
            return -1;
        }

        index = u64 + 1;
    }

    e_trace(5, "decoded choice index (index = %zd)", index);

    if ((int)index >= desc->fields.len) {
        e_info("the choice index read is not compatible with the "
               "description: either the data is invalid or the description "
               "incomplete");
        return -1;
    }

    enum_field = &desc->fields.tab[0];
    choice_field = &desc->fields.tab[index];   /* XXX Indexes start from 0 */
    __asn1_set_int(st, enum_field, index);  /* Write enum value         */
    v = t_alloc_if_pointed(choice_field, st);

    assert (choice_field->mode == ASN1_OBJ_MODE(MANDATORY));
    assert (enum_field->mode == ASN1_OBJ_MODE(MANDATORY));

    e_trace(5, "decoding CHOICE value %s:%s",
            choice_field->oc_t_name, choice_field->name);

    if (t_aper_decode_field(bs, choice_field, copy, v) < 0) {
        e_info("cannot decode choice value");
        return -1;
    }

    return 0;
}

static int
t_aper_decode_seq_of_fields(bit_stream_t *bs, const asn1_field_t *field,
                            int len, bool copy, qv_t(u8) *data_vec)
{
    uint8_t *field_data;

    if (field->pointed) {
        field_data = t_new_raw(uint8_t, len * field->size);
    } else {
        field_data = aper_buf_growlen(data_vec, len * field->size);
    }

    for (int i = 0; i < len; i++) {
        void *v = &field_data[field->size * i];

        e_trace(5, "decoding SEQUENCE OF %s:%s value [%d/%d]",
                field->oc_t_name, field->name, i, len);

        if (t_aper_decode_field(bs, field, copy, v) < 0) {
            e_info("failed to decode SEQUENCE OF element");
            return -1;
        }
    }

    if (field->pointed) {
        /* Now that the fields are decoded, fill the pointers values. */
        void **pointers;

        pointers = (void **)aper_buf_growlen(data_vec, len * sizeof(void *));
        for (int i = 0; i < len; i++) {
            pointers[i] = &field_data[field->size * i];
        }
    }

    return 0;
}

static int
t_aper_decode_seq_of(bit_stream_t *bs, const asn1_field_t *field,
                     bool copy, void *st)
{
    const asn1_field_t *repeated_field;
    const asn1_desc_t *desc = field->u.comp;
    aper_len_decoding_ctx_t len_ctx;
    qv_t(u8) buf __attribute__((cleanup(aper_buf_wipe))) = QV_INIT();
    asn1_void_vector_t *array;

    assert (desc->fields.len == 1);
    repeated_field = &desc->fields.tab[0];

    if (aper_decode_len_extension_bit(bs, &field->seq_of_info, &len_ctx) < 0)
    {
        e_info("cannot read extension bit");
        return -1;
    }

    do {
        RETHROW(aper_decode_len(bs, &len_ctx));
        e_trace(5, "decoded element count of SEQUENCE OF %s:%s "
                "(n=%u,total=%ju)",
                repeated_field->oc_t_name, repeated_field->name,
                len_ctx.to_decode, len_ctx.cumulated_len);

        if (!buf.len && !len_ctx.more_fragments_to_read) {
            /* The SEQUENCE OF is not fragmented so we know how much memory
             * we're going to need. Otherwise, using the t_pool() would be
             * inefficient because of the very limited realloc mechanism of
             * the mem stack pool. */
            t_qv_init(&buf, 0);
        }
        RETHROW(t_aper_decode_seq_of_fields(bs, repeated_field,
                                            len_ctx.to_decode, copy, &buf));
    } while (len_ctx.more_fragments_to_read);



    array = GET_PTR(st, repeated_field, asn1_void_vector_t);
    array->len = len_ctx.cumulated_len;
    if (buf.mp == t_pool()) {
        array->data = buf.tab;
        /* XXX qv_wipe() won't destroy anything. */
    } else {
        array->data = t_dup(buf.tab, buf.len);

        /* Supposedly only for fragmented SEQUENCE OF. */
        assert(len_ctx.cumulated_len != len_ctx.to_decode);
    }

    return 0;
}

/* TODO get it cleaner */
static int
t_aper_decode_constructed(bit_stream_t *bs, const asn1_desc_t *desc,
                          const asn1_field_t *field, bool copy, void *st)
{
    if (desc->is_seq_of) {
        assert (field);
        assert (field->u.comp == desc);

        return t_aper_decode_seq_of(bs, field, copy, st);
    }

    switch (desc->type) {
      case ASN1_CSTD_TYPE_SEQUENCE:
        RETHROW(t_aper_decode_sequence(bs, desc, copy, st));
        break;
      case ASN1_CSTD_TYPE_CHOICE:
        RETHROW(t_aper_decode_choice(bs, desc, copy, st));
        break;
      case ASN1_CSTD_TYPE_SET:
        e_panic("ASN.1 SET is not supported yet; please use SEQUENCE");
        break;
    }

    return 0;
}

int t_aper_decode_desc(pstream_t *ps, const asn1_desc_t *desc,
                       bool copy, void *st)
{
    bit_stream_t bs = bs_init_ps(ps, 0);

    RETHROW(t_aper_decode_constructed(&bs, desc, NULL, copy, st));

    RETHROW(bs_align(&bs));
    *ps = __bs_get_bytes(&bs, bs_len(&bs) / 8);

    return 0;
}

/* }}} */

/* }}} */

#undef e_info

/* }}} */
