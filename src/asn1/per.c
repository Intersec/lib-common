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

#include <lib-common/asn1-per.h>

#include <lib-common/bit-buf.h>
#include <lib-common/bit-stream.h>
#include <lib-common/z.h>

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
write_i64_o_aligned(bb_t *bb, int64_t i64, uint8_t olen)
{
    bb_align(bb);
    assert (olen <= 8);
    bb_be_add_bits(bb, i64, olen * 8);
}

static ALWAYS_INLINE void write_u8_aligned(bb_t *bb, uint8_t u8)
{
    bb_align(bb);
    bb_be_add_bits(bb, u8, 8);
}

static ALWAYS_INLINE void write_u16_aligned(bb_t *bb, uint16_t u16)
{
    bb_align(bb);
    bb_be_add_bits(bb, u16, 16);
}

static ALWAYS_INLINE int __read_u8_aligned(bit_stream_t *bs, uint8_t *res)
{
    uint64_t r64 = 0;

    RETHROW(bs_align(bs));
    RETHROW(bs_be_get_bits(bs, 8, &r64));
    *res = r64;
    return 0;
}

static ALWAYS_INLINE int __read_u16_aligned(bit_stream_t *bs, uint16_t *res)
{
    uint64_t r64 = 0;

    RETHROW(bs_align(bs));
    RETHROW(bs_be_get_bits(bs, 16, &r64));
    *res = r64;
    return 0;
}

static ALWAYS_INLINE int __read_u64_o_aligned(bit_stream_t *bs, size_t olen,
                                              uint64_t *res)
{
    *res = 0;
    RETHROW(bs_align(bs));
    RETHROW(bs_be_get_bits(bs, olen * 8, res));
    return 0;
}

static ALWAYS_INLINE int __read_i64_o_aligned(bit_stream_t *bs, size_t olen,
                                              int64_t *res)
{
    uint64_t u = 0;

    RETHROW(__read_u64_o_aligned(bs, olen, &u));
    *res = sign_extend(u, olen * 8);
    return 0;
}


/* }}} */
/* Write {{{ */

/* Helpers {{{ */

/* Fully constrained integer - d_max < 65536 */
static ALWAYS_INLINE void
aper_write_u16_m(bb_t *bb, uint16_t u16, uint16_t blen, uint16_t d_max)
{
    bb_push_mark(bb);

    if (!blen) {
        goto end;
    }

    if (blen == 8 && d_max == 255) {
        /* "The one-octet case". */
        write_u8_aligned(bb, u16);
        goto end;
    }

    if (blen <= 8) {
        /* "The bit-field case". */
        bb_be_add_bits(bb, u16, blen);
        goto end;
    }

    assert (blen <= 16);

    /* "The two-octet case". */
    write_u16_aligned(bb, u16);
    /* FALLTHROUGH */

  end:
    e_trace_be_bb_tail(5, bb, "constrained number (n = %u)", u16);
    bb_pop_mark(bb);
}

static ALWAYS_INLINE int
aper_write_ulen(bb_t *bb, size_t l) /* Unconstrained length */
{
    bb_push_mark(bb);

    bb_align(bb);

    e_trace_be_bb_tail(5, bb, "align");
    bb_reset_mark(bb);

    if (l <= 127) {
        write_u8_aligned(bb, l);

        e_trace_be_bb_tail(5, bb, "unconstrained length (l = %zd)", l);
        bb_pop_mark(bb);

        return 0;
    }

    if (l < (1 << 14)) {
        uint16_t u16  = l | (1 << 15);

        write_u16_aligned(bb, u16);

        e_trace_be_bb_tail(5, bb, "unconstrained length (l = %zd)", l);
        bb_pop_mark(bb);

        return 0;
    }

    bb_pop_mark(bb);

    return e_error("ASN.1 PER encoder: fragmentation is not supported");
}

static ALWAYS_INLINE void aper_write_2c_number(bb_t *bb, int64_t v,
                                               bool is_signed)
{
    uint8_t olen;

    /* XXX Handle the special case of unsigned 64-bits integers
     * in [ INT64_MAX + 1, UINT64_MAX ]. */
    if (unlikely(!is_signed && TST_BIT(&v, 63))) {
        olen = 8;
        aper_write_ulen(bb, 9);
        bb_align(bb);
        bb_add0s(bb, 8);
    } else {
        olen = i64_olen(v);
        aper_write_ulen(bb, olen);
    }
    write_i64_o_aligned(bb, v, olen);
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
        aper_write_ulen(bb, olen);
    }

    write_i64_o_aligned(bb, v, olen);
}

/* Normally small non-negative whole number (SIC) */
/* XXX Used for :
 *         - CHOICE index
 *         - Enumeration extensions
 *         - ???
 */
static void aper_write_nsnnwn(bb_t *bb, size_t n)
{
    if (n <= 63) {
        bb_be_add_bits(bb, n, 1 + 6);
        return;
    }

    bb_be_add_bit(bb, true);
    aper_write_number(bb, n, NULL);
}

static int
aper_write_len(bb_t *bb, size_t l, size_t l_min, size_t l_max)
{
    if (l_max != SIZE_MAX) {
        uint32_t d_max = l_max - l_min;
        uint32_t d     = l - l_min;

        assert (l <= l_max);

        if (d_max < (1 << 16)) {
            /* TODO pre-process u16_blen(d_max) */
            aper_write_u16_m(bb, d, u16_blen(d_max), d_max);
            return 0;
        }
    }

    return aper_write_ulen(bb, l);
}

/* }}} */
/* Front End Encoders {{{ */

/* Scalar types {{{ */

static int
aper_encode_len(bb_t *bb, size_t l, const asn1_cnt_info_t *info)
{
    if (info) {
        if (l < info->min || l > info->max) {
            if (info->extended) {
                if (l < info->ext_min || l > info->ext_max) {
                    return e_error("extended constraint not respected");
                }

                /* Extension present */
                bb_be_add_bit(bb, true);

                if (aper_write_ulen(bb, l) < 0) {
                    return e_error("failed to write extended length");
                }
            } else {
                return e_error("constraint not respected");
            }
        } else {
            if (info->extended) {
                /* Extension not present */
                bb_be_add_bit(bb, false);
            }

            aper_write_len(bb, l, info->min, info->max);
        }
    } else {
        aper_write_len(bb, l, 0, SIZE_MAX);
    }

    return 0;
}

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

static int
aper_encode_number(bb_t *bb, int64_t n, const asn1_int_info_t *info,
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

static int
aper_encode_enum(bb_t *bb, int32_t val, const asn1_enum_info_t *e)
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

static int aper_encode_data(bb_t *bb, lstr_t os, const asn1_cnt_info_t *info)
{
    if (aper_encode_len(bb, os.len, info) < 0) {
        return e_error("octet string: failed to encode length");
    }

    if (info && info->max <= 2 && info->min == info->max
    &&  os.len == (int)info->max)
    {
        for (int i = 0; i < os.len; i++) {
            bb_be_add_bits(bb, (uint8_t)os.s[i], 8);
        }

        return 0;
    }

    bb_align(bb);
    bb_be_add_bytes(bb, os.data, os.len);

    return 0;
}

static int
aper_encode_bstring(bb_t *bb, const bit_stream_t *bs,
                    const asn1_cnt_info_t *info)
{
    size_t len = bs_len(bs);

    if (aper_encode_len(bb, len, info) < 0) {
        return e_error("octet string: failed to encode length");
    }

    bb_be_add_bs(bb, bs);

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
        return aper_encode_data(bb, *(const lstr_t *)v, &field->str_info);
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
        res = aper_encode_data(bb, os, NULL);
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

    for (int i = desc->ext_pos; i < desc->vec.len; i++) {
        const asn1_field_t *field = &desc->vec.tab[i];
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
        const asn1_field_t *field = &desc->vec.tab[field_pos];
        bool field_present;

        field_bitmap_add_bit(bb, st, field, &field_present);
        fields_cnt += field_present;
    }

    return fields_cnt;
}

static int
aper_encode_sequence(bb_t *bb, const void *st, const asn1_desc_t *desc)
{
    BB(ext_bb, desc->vec.len - desc->ext_pos);
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

    for (int i = 0; i < desc->vec.len; i++) {
        const asn1_field_t *field = &desc->vec.tab[i];

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

    assert (desc->vec.len > 1);

    enum_field = &desc->vec.tab[0];

    index = __asn1_get_int(st, enum_field);
    if (index < 1) {
        return e_error("wrong choice initialization");
    }
    e_trace(5, "index = %d", index);
    choice_field = &desc->vec.tab[index];
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
aper_encode_seq_of(bb_t *bb, const void *st, const asn1_field_t *field)
{
    const uint8_t *tab;
    size_t elem_cnt;
    const asn1_field_t *repeated_field;
    const asn1_desc_t *desc = field->u.comp;

    assert (desc->vec.len == 1);
    repeated_field = &desc->vec.tab[0];

    assert (repeated_field->mode == ASN1_OBJ_MODE(SEQ_OF));

    tab     = (const uint8_t *)GET_VECTOR_DATA(st, repeated_field);
    elem_cnt = GET_VECTOR_LEN(st, repeated_field);

    bb_push_mark(bb);

    if (aper_encode_len(bb, elem_cnt, &field->seq_of_info) < 0) {
        bb_pop_mark(bb);

        return e_error("failed to encode SEQUENCE OF length (n = %zd)",
                       elem_cnt);
    }

    e_trace_be_bb_tail(5, bb, "SEQUENCE OF length");
    bb_pop_mark(bb);

    if (repeated_field->pointed) {
        for (size_t j = 0; j < elem_cnt; j++) {
            if (aper_encode_field(bb, ((const void **)tab)[j],
                                  repeated_field) < 0)
            {
                e_error("failed to encode array value [%zd] %s:%s",
                        j, repeated_field->oc_t_name, repeated_field->name);

                return -1;
            }
        }
    } else {
        for (size_t j = 0; j < elem_cnt; j++) {
            if (aper_encode_field(bb, tab + j * repeated_field->size,
                                  repeated_field) < 0)
            {
                e_error("failed to encode vector value [%zd] %s:%s",
                        j, repeated_field->oc_t_name, repeated_field->name);

                return -1;
            }
        }
    }

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

static ALWAYS_INLINE int
aper_read_u16_m(bit_stream_t *bs, size_t blen, uint16_t *u16, uint16_t d_max)
{
    uint64_t res;
    assert (blen); /* u16 is given by constraints */

    if (blen == 8 && d_max == 255) {
        /* "The one-octet case". */
        *u16 = 0;
        if (__read_u8_aligned(bs, (uint8_t *)u16) < 0) {
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
    if (__read_u16_aligned(bs, u16) < 0) {
        e_info("cannot read constrained integer: end of input "
               "(expected at least two aligned octet left)");
        return -1;
    }
    return 0;
}

static ALWAYS_INLINE int
aper_read_ulen(bit_stream_t *bs, size_t *l)
{
    union {
        uint8_t  b[2];
        uint16_t w;
    } res;
    res.w = 0;

    if (__read_u8_aligned(bs, &res.b[1]) < 0) {
        e_info("cannot read unconstrained length: end of input "
               "(expected at least one aligned octet left)");
        return -1;
    }

    if (!(res.b[1] & (1 << 7))) {
        *l = res.b[1];
        return 0;
    }

    if (res.b[1] & (1 << 6)) {
        e_info("cannot read unconstrained length: "
               "fragmented values are not supported");
        return -1;
    }

    if (__read_u8_aligned(bs, &res.b[0]) < 0) {
        e_info("cannot read unconstrained length: end of input "
               "(expected at least a second octet left)");
        return -1;
    }

    *l = res.w & 0x7fff;
    return 0;
}

static ALWAYS_INLINE int
aper_read_2c_number(bit_stream_t *bs, int64_t *v, bool is_signed)
{
    size_t olen;

    if (aper_read_ulen(bs, &olen) < 0) {
        e_info("cannot read unconstrained whole number length");
        return -1;
    }

    /* XXX Handle the special case of unsigned 64-bits integers
     * in [ INT64_MAX + 1, UINT64_MAX ]. */
    if (olen == 9 && !is_signed) {
        uint8_t o = 0;
        uint64_t u;

        if (__read_u8_aligned(bs, &o) < 0) {
            goto not_enough_bytes;
        }

        if (o) {
            goto overflow;
        }

        if (__read_u64_o_aligned(bs, 8, &u) < 0) {
            goto not_enough_bytes;
        }

        *v = u;
        return 0;
    }

    if (olen > 8) {
        goto overflow;
    }

    if (__read_i64_o_aligned(bs, olen, v) < 0) {
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
            uint16_t u16;

            if (info->max_blen == 0) {
                *v = 0;
                return 0;
            }

            if (aper_read_u16_m(bs, info->max_blen, &u16, info->d_max) < 0) {
                e_info("cannot read constrained whole number");
                return -1;
            }

            *v = u16;

            return 0;
        } else {
            uint16_t u16;

            if (aper_read_u16_m(bs, info->max_olen_blen, &u16,
                                info->d_max) < 0)
            {
                e_info("cannot read constrained whole number length");
                return -1;
            }

            olen = u16 + 1;
        }
    } else {
        if (aper_read_ulen(bs, &olen) < 0) {
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

    if (__read_u64_o_aligned(bs, olen, v) < 0) {
        e_info("not enough bytes to read number (got %zd, need %zd)",
               bs_len(bs) / 8, olen);
        return -1;
    }

    return 0;
}

static int aper_read_nsnnwn(bit_stream_t *bs, size_t *n)
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

static int
aper_read_len(bit_stream_t *bs, size_t l_min, size_t l_max, size_t *l)
{
    size_t d_max = l_max - l_min;

    if (d_max < (1 << 16)) {
        uint16_t d;

        if (!d_max) {
            *l = l_min;

            return 0;
        }

        if (aper_read_u16_m(bs, u16_blen(d_max), &d, d_max) < 0) {
            e_info("cannot read constrained length");
            return -1;
        }

        *l = l_min + d;
    } else {
        if (aper_read_ulen(bs, l) < 0) {
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

static int
aper_decode_len(bit_stream_t *bs, const asn1_cnt_info_t *info, size_t *l)
{
    if (info) {
        bool extension_present;

        if (info->extended) {
            if (bs_done(bs)) {
                e_info("cannot read extension bit: end of input");
                return -1;
            }

            extension_present = __bs_be_get_bit(bs);
        } else {
            extension_present = false;
        }

        if (extension_present) {
            if (aper_read_ulen(bs, l) < 0) {
                e_info("cannot read extended length");
                return -1;
            }

            if (*l < info->ext_min || *l > info->ext_max) {
                e_info("extended length constraint not respected");
                return -1;
            }
        } else {
            if (aper_read_len(bs, info->min, info->max, l) < 0) {
                e_info("cannot read constrained length");
                return -1;
            }

            if (*l < info->min || *l > info->max) {
                e_info("root length constraint not respected");
                return -1;
            }
        }
    } else {
        if (aper_read_len(bs, 0, SIZE_MAX, l) < 0) {
            e_info("cannot read unconstrained length");
            return -1;
        }
    }

    return 0;
}

static int
aper_decode_number(bit_stream_t *nonnull bs,
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

static int
aper_decode_enum(bit_stream_t *bs, const asn1_enum_info_t *e, int32_t *val)
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

static int
t_aper_decode_ostring(bit_stream_t *bs, const asn1_cnt_info_t *info,
                      bool copy, lstr_t *os)
{
    size_t    len;
    pstream_t ps;

    if (aper_decode_len(bs, info, &len) < 0) {
        e_info("cannot decode octet string length");
        return -1;
    }

    *os = LSTR_INIT_V(NULL, len);

    if (info && info->max <= 2 && info->min == info->max
    &&  len == info->max)
    {
        uint8_t *buf;

        if (!bs_has(bs, os->len * 2)) {
            e_info("cannot read octet string: not enough bits");
            return -1;
        }

        buf = t_new(uint8_t, os->len + 1);

        for (int i = 0; i < os->len; i++) {
            buf[i] = __bs_be_get_bits(bs, 8);
        }

        os->data = buf;
        os->mem_pool = MEM_STACK;

        return 0;
    }

    if (bs_align(bs) < 0 || bs_get_bytes(bs, os->len, &ps) < 0) {
        e_info("cannot read octet string: not enough octets "
               "(want %d, got %zd)", os->len, bs_len(bs) / 8);
        return -1;
    }

    os->s = ps.s;
    if (copy) {
        mp_lstr_persists(t_pool(), os);
        os->mem_pool = MEM_STACK;
    }

    e_trace_hex(6, "Decoded OCTET STRING", os->data, (int)os->len);

    return 0;
}

static int
t_aper_decode_data(bit_stream_t *bs, const asn1_cnt_info_t *info,
                   bool copy, lstr_t *data)
{
    lstr_t os;

    RETHROW(t_aper_decode_ostring(bs, info, copy, &os));

    *data = os;

    return 0;
}

static int
t_aper_decode_bstring(bit_stream_t *bs, const asn1_cnt_info_t *info,
                      bool copy, bit_stream_t *str)
{
    size_t len;

    if (aper_decode_len(bs, info, &len) < 0) {
        e_info("cannot decode bit string length");
        return -1;
    }

    if (bs_get_bs(bs, len, str) < 0) {
        e_info("cannot read bit string: not enough bits");
        return -1;
    }

    e_trace_be_bs(6, str, "Decoded bit string");

    if (copy) {
        size_t olen = str->e.p - str->s.p;

        if (str->e.offset) {
            str->s.p = t_dup(str->e.p, olen + 1);
        } else {
            str->s.p = t_dup(str->e.p, olen);
        }
        str->e.p = str->s.p + olen;
    }

    return 0;
}

static int
t_aper_decode_bit_string(bit_stream_t *bs, const asn1_cnt_info_t *info,
                         asn1_bit_string_t *bit_string)
{
    bit_stream_t bstring;
    bb_t bb;
    uint8_t *data;
    size_t size;

    RETHROW(t_aper_decode_bstring(bs, info, false, &bstring));

    size = DIV_ROUND_UP(bs_len(&bstring), 8);
    bb_inita(&bb, size);
    bb_be_add_bs(&bb, &bstring);
    data = t_dup(bb.bytes, size);
    *bit_string = ASN1_BIT_STRING(data, bs_len(&bstring));
    bb_wipe(&bb);

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

        if (t_aper_decode_ostring(bs, NULL, false, &os) < 0) {
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

    for (int i = 0; i < desc->vec.len; i++) {
        const asn1_field_t *field = &desc->vec.tab[i];
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
            if (t_aper_decode_ostring(bs, NULL, false, &os) < 0) {
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

        if (index + desc->ext_pos >= (size_t)desc->vec.len) {
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

    if ((int)index >= desc->vec.len) {
        e_info("the choice index read is not compatible with the "
               "description: either the data is invalid or the description "
               "incomplete");
        return -1;
    }

    enum_field = &desc->vec.tab[0];
    choice_field = &desc->vec.tab[index];   /* XXX Indexes start from 0 */
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
t_aper_decode_seq_of(bit_stream_t *bs, const asn1_field_t *field,
                     bool copy, void *st)
{
    size_t elem_cnt;
    const asn1_field_t *repeated_field;
    const asn1_desc_t *desc = field->u.comp;

    assert (desc->vec.len == 1);
    repeated_field = &desc->vec.tab[0];

    if (aper_decode_len(bs, &field->seq_of_info, &elem_cnt) < 0) {
        e_info("failed to decode SEQUENCE OF length");
        return -1;
    }

    e_trace(5, "decoded element count of SEQUENCE OF %s:%s (n = %zd)",
            repeated_field->oc_t_name, repeated_field->name, elem_cnt);

    if (unlikely(!elem_cnt)) {
        *GET_PTR(st, repeated_field, lstr_t) = LSTR_NULL_V;
        return 0;
    }

    asn1_alloc_seq_of(st, elem_cnt, repeated_field, t_pool());

    GET_PTR(st, repeated_field, asn1_void_vector_t)->len = elem_cnt;

    for (size_t j = 0; j < elem_cnt; j++) {
        void *v;

        if (repeated_field->pointed) {
            v = GET_PTR(st, repeated_field, asn1_void_array_t)->data[j];
        } else {
            v = (char *)(GET_PTR(st, repeated_field, asn1_void_vector_t)->data)
              + j * repeated_field->size;
        }

        e_trace(5, "decoding SEQUENCE OF %s:%s value [%zu/%zu]",
                repeated_field->oc_t_name, repeated_field->name, j, elem_cnt);

        if (t_aper_decode_field(bs, repeated_field, copy, v) < 0) {
            e_info("failed to decode SEQUENCE OF element");
            return -1;
        }
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
/* Check {{{ */

static int z_test_aper_enum(const asn1_enum_info_t *e, int32_t val,
                            const char *exp_encoding)
{
    BB_1k(bb);
    bit_stream_t bs;
    int32_t res;

    Z_ASSERT_N(aper_encode_enum(&bb, val, e), "cannot encode");
    bs = bs_init_bb(&bb);
    Z_ASSERT_N(aper_decode_enum(&bs, e, &res), "cannot decode");
    Z_ASSERT_EQ(res, val, "decoded value differs");
    Z_ASSERT_STREQUAL(exp_encoding, t_print_be_bb(&bb, NULL),
                      "unexpected encoding");

    bb_wipe(&bb);
    Z_HELPER_END;
}

static int z_test_aper_number(const asn1_int_info_t *nonnull info,
                              int64_t val, bool is_signed,
                              const char *nonnull exp_encoding)
{
    t_scope;
    BB_1k(bb);
    bit_stream_t bs;
    int64_t i64;

    aper_encode_number(&bb, val, info, is_signed);
    bs = bs_init_bb(&bb);
    Z_ASSERT_STREQUAL(t_print_be_bb(&bb, NULL), exp_encoding,
                      "unexpected encoding");
    Z_ASSERT_N(aper_decode_number(&bs, info, is_signed, &i64),
               "cannot decode `%s`", exp_encoding);
    Z_ASSERT_EQ(i64, val, "decoded value differs");

    bb_wipe(&bb);
    Z_HELPER_END;
}

static void z_asn1_int_info_set_opt_min(asn1_int_info_t *info, opt_i64_t i)
{
    if (OPT_ISSET(i)) {
        asn1_int_info_set_min(info, OPT_VAL(i));
    }
}
static void z_asn1_int_info_set_opt_max(asn1_int_info_t *info, opt_i64_t i)
{
    if (OPT_ISSET(i)) {
        asn1_int_info_set_max(info, OPT_VAL(i));
    }
}

Z_GROUP_EXPORT(asn1_aper_low_level) {
    Z_TEST(u16, "aligned per: aper_write_u16_m/aper_read_u16_m") {
        t_scope;
        BB_1k(bb);

        struct {
            size_t d, d_max, skip;
            const char *s;
        } t[] = {
            {     0,     0,  0, "" },
            {   0xe,    57,  0, ".001110" },
            {  0x8d,   255,  0, ".10001101" },
            {  0x8d,   254,  1, ".01000110.1" },
            {  0x8d,   255,  1, ".00000000.10001101" },
            { 0xabd, 33000,  0, ".00001010.10111101" },
        };

        for (int i = 0; i < countof(t); i++) {
            bit_stream_t bs;
            size_t len;

            bb_reset(&bb);
            bb_add0s(&bb, t[i].skip);

            len = u64_blen(t[i].d_max);
            aper_write_u16_m(&bb, t[i].d, u64_blen(t[i].d_max), t[i].d_max);
            bs = bs_init_bb(&bb);
            if (len) {
                uint16_t u16 = t[i].d - 1;

                Z_ASSERT_N(bs_skip(&bs, t[i].skip));
                Z_ASSERT_N(aper_read_u16_m(&bs, len, &u16, t[i].d_max),
                           "[i:%d]", i);
                Z_ASSERT_EQ(u16, t[i].d, "[i:%d] len=%zu", i, len);
            }
            Z_ASSERT_STREQUAL(t[i].s, t_print_be_bb(&bb, NULL), "[i:%d]", i);
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(len, "aligned per: aper_write_len/aper_read_len") {
        t_scope;
        BB_1k(bb);

        struct {
            size_t l, l_min, l_max, skip;
            const char *s;
        } t[] = {
            { 15,    15,           15, 0, "" },
            { 7,      3,           18, 0, ".0100" },
            { 15,     0, ASN1_MAX_LEN, 0, ".00001111" },
            { 0x1b34, 0, ASN1_MAX_LEN, 0, ".10011011.00110100" },
            { 32,     1,          160, 1, ".00001111.1" },
        };

        for (int i = 0; i < countof(t); i++) {
            bit_stream_t bs;
            size_t len;

            bb_reset(&bb);
            bb_add0s(&bb, t[i].skip);

            aper_write_len(&bb, t[i].l, t[i].l_min, t[i].l_max);
            bs = bs_init_bb(&bb);
            Z_ASSERT_N(bs_skip(&bs, t[i].skip));
            Z_ASSERT_N(aper_read_len(&bs, t[i].l_min, t[i].l_max, &len),
                       "[i:%d]", i);
            Z_ASSERT_EQ(len, t[i].l, "[i:%d]", i);
            Z_ASSERT_STREQUAL(t[i].s, t_print_be_bb(&bb, NULL), "[i:%d]", i);
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(nsnnwn, "aligned per: aper_write_nsnnwn/aper_read_nsnnwn") {
        t_scope;
        BB_1k(bb);

        struct {
            size_t n;
            const char *s;
        } t[] = {
            {   0,  ".0000000" },
            { 0xe,  ".0001110" },
            { 96,   ".10000000.00000001.01100000" },
            { 128,  ".10000000.00000001.10000000" },
        };

        for (int i = 0; i < countof(t); i++) {
            bit_stream_t bs;
            size_t len;

            bb_reset(&bb);
            aper_write_nsnnwn(&bb, t[i].n);
            bs = bs_init_bb(&bb);
            Z_ASSERT_N(aper_read_nsnnwn(&bs, &len), "[i:%d]", i);
            Z_ASSERT_EQ(len, t[i].n, "[i:%d]", i);
            Z_ASSERT_STREQUAL(t[i].s, t_print_be_bb(&bb, NULL), "[i:%d]", i);
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(number, "aligned per: aper_{encode,decode}_number") {
        struct {
            int64_t i;
            opt_i64_t min;
            opt_i64_t max;
            bool is_signed;
            bool extended;
            const char *s;
        } tests[] = {
            { 1234,  OPT_NONE, OPT_NONE, true, false,
              ".00000010.00000100.11010010" },
            { -1234, OPT_NONE, OPT_NONE, true, false,
              ".00000010.11111011.00101110" },
            { 0,     OPT_NONE, OPT_NONE, true, false, ".00000001.00000000" },
            { 0,     OPT(-5), OPT_NONE, true, false, ".00000001.00000101" },
            { -3,    OPT(-5), OPT(-1), true, false, ".010" },
            { -1,    OPT(-5), OPT(-1), true, false, ".100" },
            { -1,    OPT_NONE, OPT_NONE, true, false, ".00000001.11111111" },
            { 45,    OPT(0), OPT(100000), true, false, ".00000000.00101101" },
            { 128,   OPT(0), OPT(100000), true, false, ".00000000.10000000" },
            { 256,   OPT(0), OPT(100000), true, false,
                ".01000000.00000001.00000000" },
            { 666,   OPT(666), OPT(666), true, false, "" },
            { 1ULL + INT64_MAX, OPT(INT64_MAX), OPT(UINT64_MAX), false, false,
              ".00000000.00000001" },
            { UINT64_MAX, OPT(0), OPT_NONE, false, false,
              ".00001000.11111111.11111111.11111111.11111111.11111111"
              ".11111111.11111111.11111111" },
            { UINT64_MAX, OPT(INT64_MAX), OPT(UINT64_MAX), false, false,
              ".11100000.10000000.00000000.00000000.00000000.00000000"
              ".00000000.00000000.00000000" },
            { INT64_MAX, OPT(INT64_MIN), OPT(INT64_MAX), true, false,
              ".11100000.11111111.11111111.11111111.11111111.11111111"
              ".11111111.11111111.11111111" },
            { 5, OPT(0), OPT(7), true, true, ".0101" },
            { 8, OPT(0), OPT(7), true, true, ".10000000.00000001.00001000" },
            { UINT64_MAX, OPT_NONE, OPT_NONE, false, false,
              ".00001001.00000000.11111111.11111111.11111111.11111111"
              ".11111111.11111111.11111111.11111111" },
        };

        carray_for_each_ptr(t, tests) {
            asn1_int_info_t info;

            asn1_int_info_init(&info);
            z_asn1_int_info_set_opt_min(&info, t->min);
            z_asn1_int_info_set_opt_max(&info, t->max);
            if (t->extended) {
                info.extended = true;
            }
            asn1_int_info_update(&info, t->is_signed);

            Z_HELPER_RUN(z_test_aper_number(&info, t->i, t->is_signed, t->s),
                         "test (%ld/%zd) failed", t - tests + 1,
                         countof(tests));
        }
    } Z_TEST_END;

    Z_TEST(64bits_number_overflows, "aper: 64bits overflows on numbers") {
        BB_1k(bb);
        SB_1k(err);
        struct {
            const char *title;
            bool is_signed;
            opt_i64_t min;
            opt_i64_t max;
            const char *input;
        } tests[] = {
            { "unsigned: -1", false, OPT_NONE, OPT_NONE,
              ".00000001.11111111" },

            { "unsigned: UINT64_MAX + 1", false, OPT_NONE, OPT_NONE,
              ".00001001.00000001.00000000.00000000.00000000.00000000"
              ".00000000.00000000.00000000.00000000" },

            { "signed: INT64_MIN - 1", true, OPT_NONE, OPT_NONE,
              ".00001001.10000000.00000000.00000000.00000000.00000000"
              ".00000000.00000000.00000000.00000000" },

            { "signed: INT64_MAX + 1", true, OPT_NONE, OPT_NONE,
              ".00001001.00000000.10000000.00000000.00000000.00000000"
              ".00000000.00000000.00000000.00000000" },

            { "signed semi-constrained: INT64_MAX + 1", true,
              OPT(INT64_MAX), OPT_NONE,
              ".00000001.00000001" },

            { "signed semi-constrained: INT64_MAX + 1 (delta overflow)", true,
              OPT(INT64_MIN), OPT_NONE,
              ".00001001.00000001.00000000.00000000.00000000.00000000"
              ".00000000.00000000.00000000.00000000" },

            { "unsigned semi-constrained: UINT64_MAX + 1 (delta overflow)",
              false, OPT(UINT64_MAX), OPT_NONE,
              ".00000001.00000001" },

            { "unsigned constrained: UINT64_MAX + 1", false,
              OPT(1), OPT(UINT64_MAX),
              ".11100000.11111111.11111111.11111111.11111111"
              ".11111111.11111111.11111111.11111111" },
        };

        carray_for_each_ptr(t, tests) {
            bit_stream_t bs;
            int64_t v;
            asn1_int_info_t info;

            asn1_int_info_init(&info);
            z_asn1_int_info_set_opt_min(&info, t->min);
            z_asn1_int_info_set_opt_max(&info, t->max);
            asn1_int_info_update(&info, t->is_signed);

            Z_ASSERT_N(z_set_be_bb(&bb, t->input, &err),
                       "invalid input `%s`: %*pM", t->input,
                       SB_FMT_ARG(&err));
            bs = bs_init_bb(&bb);
            Z_ASSERT_NEG(aper_decode_number(&bs, &info, t->is_signed, &v),
                         "test `%s`: decoding was supposed to fail "
                         "(v=%juULL/%jdLL)", t->title, v, v);
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(ostring, "aligned per: aper_{encode,decode}_ostring") {
        t_scope;
        BB_1k(bb);

        asn1_cnt_info_t uc = { /* Unconstrained */
            .max     = SIZE_MAX,
        };

        asn1_cnt_info_t fc1 = { /* Fully constrained */
            .min     = 3,
            .max     = 3,
        };

        asn1_cnt_info_t fc2 = { /* Fully constrained */
            .max     = 23,
        };

        asn1_cnt_info_t ext1 = { /* Extended */
            .min      = 1,
            .max      = 2,
            .extended = true,
            .ext_min  = 3,
            .ext_max  = 3,
        };

        asn1_cnt_info_t ext2 = { /* Extended */
            .min      = 2,
            .max      = 2,
            .extended = true,
            .ext_max  = SIZE_MAX,
        };

        struct {
            const char      *os;
            asn1_cnt_info_t *info;
            bool             copy;
            const char      *s;
        } t[] = {
            { "aaa", &uc,   true,  ".00000011.01100001.01100001.01100001" },
            { "aaa", NULL,  true,  ".00000011.01100001.01100001.01100001" },
            { "aaa", NULL,  false, ".00000011.01100001.01100001.01100001" },
            { "aaa", &fc1,  false, ".01100001.01100001.01100001" },
            { "aaa", &fc2,  false, ".00011000.01100001.01100001.01100001" },
            { "aa",  &ext1, true,  ".01000000.01100001.01100001" },
            { "aaa", &ext1, true,  ".10000000.00000011.01100001.01100001.01100001" },
            { "aa",  &ext2, true,  ".00110000.10110000.1" },
            { "a",   &ext2, true,  ".10000000.00000001.01100001" },
        };

        for (int i = 0; i < countof(t); i++) {
            lstr_t src = LSTR(t[i].os);
            lstr_t dst;
            bit_stream_t bs;

            bb_reset(&bb);
            aper_encode_data(&bb, src, t[i].info);
            if (src.len < 4) {
                Z_ASSERT_STREQUAL(t[i].s, t_print_be_bb(&bb, NULL),"[i:%d]", i);
            }
            bs = bs_init_bb(&bb);
            Z_ASSERT_N(t_aper_decode_ostring(&bs, t[i].info, t[i].copy, &dst),
                       "[i:%d]", i);
            Z_ASSERT_LSTREQUAL(LSTR_INIT_V((void *)dst.data, dst.len),
                               LSTR_INIT_V((void *)src.data, src.len),
                               "[i:%d]", i);
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(bstring, "aligned per: aper_{encode,decode}_bstring") {
        t_scope;
        BB_1k(bb);
        BB_1k(src_bb);

        asn1_cnt_info_t uc = { /* Unconstrained */
            .max     = SIZE_MAX,
        };

        asn1_cnt_info_t fc1 = { /* Fully constrained */
            .min     = 3,
            .max     = 3,
        };

        asn1_cnt_info_t fc2 = { /* Fully constrained */
            .max     = 23,
        };

        asn1_cnt_info_t ext1 = { /* Extended */
            .min      = 1,
            .max      = 2,
            .extended = true,
            .ext_min  = 3,
            .ext_max  = 3,
        };

        asn1_cnt_info_t ext2 = { /* Extended */
            .min      = 2,
            .max      = 2,
            .extended = true,
            .ext_max  = SIZE_MAX,
        };

        asn1_cnt_info_t ext3 = { /* Extended */
            .min      = 1,
            .max      = 160,
            .extended = true,
            .ext_max  = SIZE_MAX,
        };

        struct {
            const char      *bs;
            asn1_cnt_info_t *info;
            bool             copy;
            const char      *s;
        } t[] = {
            { "01010101", &uc,   false, ".00001000.01010101" },
            { "01010101", NULL,  true,  ".00001000.01010101" },
            { "101",      &fc1,  true,  ".101" },
            { "101",      &fc2,  true,  ".00011101" },
            { "10",       &ext1, true,  ".0110" },
            { "011",      &ext1, true,  ".10000000.00000011.011" },
            { "11",       &ext2, true,  ".011" },
            { "011",      &ext2, true,  ".10000000.00000011.011" },
            { "00",       &ext3, true,  ".00000000.100" },
        };

        for (int i = 0; i < countof(t); i++) {
            bit_stream_t src;
            bit_stream_t dst;
            bit_stream_t bs;

            bb_reset(&bb);
            bb_reset(&src_bb);
            for (const char *s = t[i].bs; *s; s++) {
                if (*s == '1' || *s == '0') {
                    bb_be_add_bit(&src_bb, *s == '1');
                }
            }
            src = bs_init_bb(&src_bb);
            aper_encode_bstring(&bb, &src, t[i].info);
            Z_ASSERT_STREQUAL(t[i].s, t_print_be_bb(&bb, NULL), "[i:%d]", i);
            bs = bs_init_bb(&bb);
            Z_ASSERT_N(t_aper_decode_bstring(&bs, t[i].info, t[i].copy, &dst),
                       "[i:%d]", i);
            Z_ASSERT_EQ(bs_len(&dst), bs_len(&src), "[i:%d]", i);
            Z_ASSERT(bs_equals(dst, src), "[i:%d]", i);
        }

        bb_wipe(&bb);
        bb_wipe(&src_bb);
    } Z_TEST_END;

    Z_TEST(enum, "aligned per: aper_{encode,decode}_enum") {
        t_scope;

        asn1_enum_info_t *e;
        asn1_enum_info_t e1;
        asn1_enum_info_t e2;
        asn1_enum_info_t e3;
        asn1_enum_info_t e4;
        asn1_enum_info_t e5;

        struct {
            int32_t          val;
            const asn1_enum_info_t *e;
            const char       *s;
        } tests[] = {
            { 5,   &e1, ".0" },
            { 18,  &e1, ".1" },
            { 48,  &e2, ".00110000" },
            { 104, &e2, ".10000000" },
            { 192, &e2, ".10000001" },
            { 20,  &e3, ".010100" },
            { -42, &e4, ".0" },
            { 42,  &e4, ".1" },
            { 1024, &e5, ".00000011.11100010" },
        };

        e = asn1_enum_info_init(&e1);
        asn1_enum_append(e, 5);
        asn1_enum_append(e, 18);
        asn1_enum_info_done(e);

        e = asn1_enum_info_init(&e2);
        for (int32_t i = 0; i < 100; i++) {
            asn1_enum_append(e, i);
        }
        e->extended = true;
        asn1_enum_append(e, 104);
        asn1_enum_append(e, 192);
        asn1_enum_info_done(e);

        e = asn1_enum_info_init(&e3);
        for (int32_t i = 0; i < 21; i++) {
            asn1_enum_append(e, i);
        }
        e->extended = true;
        asn1_enum_info_done(e);

        e = asn1_enum_info_init(&e4);
        asn1_enum_append(e, -42);
        asn1_enum_append(e, 42);
        asn1_enum_info_done(e);

        e = asn1_enum_info_init(&e5);
        for (int32_t i = 0; i < 1000; i++) {
            asn1_enum_append(e, i + 30);
        }
        asn1_enum_info_done(e);

        carray_for_each_ptr(t, tests) {
            Z_HELPER_RUN(z_test_aper_enum(t->e, t->val, t->s),
                         "(test %ld/%ld) check fail for value `%d` "
                         "(expected encoding `%s`)", t - tests + 1,
                         countof(tests), t->val, t->s);
        }

        asn1_enum_info_wipe(&e1);
        asn1_enum_info_wipe(&e2);
        asn1_enum_info_wipe(&e3);
        asn1_enum_info_wipe(&e4);
        asn1_enum_info_wipe(&e5);
    } Z_TEST_END;

    Z_TEST(enum_ext_defval, "aligned per: extended enum default value") {
        asn1_enum_info_t e;
        BB_1k(bb);
        bit_stream_t bs;
        int32_t res;

        asn1_enum_info_init(&e);
        e.extended = true;
        asn1_enum_append(&e, 666);
        Z_ASSERT_N(aper_encode_enum(&bb, 666, &e));

        asn1_enum_info_wipe(&e);
        asn1_enum_info_init(&e);
        e.extended = true;
        asn1_enum_info_reg_ext_defval(&e, 42);

        bs = bs_init_bb(&bb);
        Z_ASSERT_N(aper_decode_enum(&bs, &e, &res));
        Z_ASSERT_EQ(res, 42);

        asn1_enum_info_wipe(&e);
        bb_wipe(&bb);
    } Z_TEST_END;
} Z_GROUP_END

/* }}} */
