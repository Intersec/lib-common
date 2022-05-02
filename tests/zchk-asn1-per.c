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

#include <lib-common/asn1/per-priv.h>
#include <lib-common/z.h>
#include <lib-common/iop.h>
#include <lib-common/bit-buf.h>
#include <lib-common/bit-stream.h>

#include "iop/tstiop.iop.h"

/* {{{ Choice */

typedef struct {
    uint16_t iop_tag;
    union {
        int i;
    };
} choice1_t;

static __ASN1_IOP_CHOICE_DESC_BEGIN(choice1);
    asn1_reg_scalar(choice1, i, 0);
    asn1_set_int_min_max(choice1, 2, 15);
ASN1_CHOICE_DESC_END(choice1);

/* }}} */
/* {{{ Extended choice. */

static __ASN1_IOP_CHOICE_DESC_BEGIN(tstiop__asn1_ext_choice_);
    asn1_reg_scalar(tstiop__asn1_ext_choice_, i, 0);
    asn1_set_int_min_max(tstiop__asn1_ext_choice_, 42, 666);
    asn1_reg_extension(tstiop__asn1_ext_choice_);
    asn1_reg_string(tstiop__asn1_ext_choice_, ext_s, 1);
    asn1_reg_scalar(tstiop__asn1_ext_choice_, ext_i, 2);
    asn1_set_int_min_max(tstiop__asn1_ext_choice_, 666, 1234567);
ASN1_CHOICE_DESC_END(tstiop__asn1_ext_choice_);

/* }}} */
/* {{{ Extended sequence. */

typedef struct sequence1_t {
#define SEQ_EXT_ROOT_FIELDS                                                  \
    opt_i8_t root1;                                                          \
    int root2

#define SEQ_EXT_PARTIAL_FIELDS                                               \
    SEQ_EXT_ROOT_FIELDS;                                                     \
    lstr_t ext1;

#define SEQ_EXT_FIELDS                                                       \
    SEQ_EXT_PARTIAL_FIELDS;                                                  \
    opt_i32_t ext2;                                                          \
    opt_u8_t ext3

    SEQ_EXT_FIELDS;
} sequence1_t;

static ASN1_SEQUENCE_DESC_BEGIN(sequence1);
#define SEQ_EXT_ROOT_FIELDS_DESC(pfx)                                        \
    asn1_reg_scalar(pfx, root1, 0);                                          \
    asn1_set_int_min_max(pfx, 1, 16);                                        \
                                                                             \
    asn1_reg_scalar(pfx, root2, 0);                                          \
    asn1_set_int_min(pfx, -42);                                              \
                                                                             \
    asn1_reg_extension(pfx)

#define SEQ_EXT_PARTIAL_FIELDS_DESC(pfx)                                     \
    SEQ_EXT_ROOT_FIELDS_DESC(pfx);                                           \
    asn1_reg_opt_string(pfx, ext1, 0)

#define SEQ_EXT_FIELDS_DESC(pfx)                                             \
    SEQ_EXT_PARTIAL_FIELDS_DESC(pfx);                                        \
    asn1_reg_scalar(pfx, ext2, 0);                                           \
    asn1_set_int_min_max(pfx, -100000, 100000);                              \
                                                                             \
    asn1_reg_scalar(pfx, ext3, 0);                                           \
    asn1_set_int_min_max(pfx, 0, 256)

    SEQ_EXT_FIELDS_DESC(sequence1);
ASN1_SEQUENCE_DESC_END(sequence1);

/* Same without extension. */
typedef struct sequence1_root_t {
    SEQ_EXT_ROOT_FIELDS;
} sequence1_root_t;

static ASN1_SEQUENCE_DESC_BEGIN(sequence1_root);
    SEQ_EXT_ROOT_FIELDS_DESC(sequence1_root);
ASN1_SEQUENCE_DESC_END(sequence1_root);

/* Same with less fields in extension. */
typedef struct sequence1_partial_t {
    SEQ_EXT_PARTIAL_FIELDS;
} sequence1_partial_t;

static ASN1_SEQUENCE_DESC_BEGIN(sequence1_partial);
    SEQ_EXT_PARTIAL_FIELDS_DESC(sequence1_partial);
ASN1_SEQUENCE_DESC_END(sequence1_partial);

static int z_test_seq_ext(const sequence1_t *in, lstr_t exp_encoding)
{
    SB_1k(buf);
    pstream_t ps;
    sequence1_t out;
    sequence1_root_t out_root;
    sequence1_partial_t out_partial;

    Z_ASSERT_N(aper_encode(&buf, sequence1, in), "encoding failure");
    /* TODO switch to bits */
    Z_ASSERT_DATAEQUAL(exp_encoding, LSTR_SB_V(&buf),
                       "unexpected encoding value");

    memset(&out, 0xff, sizeof(out));
    ps = ps_initsb(&buf);
    Z_ASSERT_N(t_aper_decode(&ps, sequence1, false, &out),
               "decoding failure (full sequence)");
    Z_ASSERT_OPT_EQ(out.root1, in->root1);
    Z_ASSERT_EQ(out.root2, in->root2);
    Z_ASSERT_DATAEQUAL(out.ext1, in->ext1);
    Z_ASSERT_OPT_EQ(out.ext2, in->ext2);
    Z_ASSERT_OPT_EQ(out.ext3, in->ext3);

    memset(&out_root, 0xff, sizeof(out_root));
    ps = ps_initsb(&buf);
    Z_ASSERT_N(t_aper_decode(&ps, sequence1_root, false, &out_root),
               "decoding failure (root sequence)");
    Z_ASSERT_OPT_EQ(out_root.root1, in->root1);
    Z_ASSERT_EQ(out_root.root2, in->root2);

    memset(&out_partial, 0xff, sizeof(out_partial));
    ps = ps_initsb(&buf);
    Z_ASSERT_N(t_aper_decode(&ps, sequence1_partial, false, &out_partial),
               "decoding failure (partial sequence)");
    Z_ASSERT_OPT_EQ(out_partial.root1, in->root1);
    Z_ASSERT_EQ(out_partial.root2, in->root2);
    Z_ASSERT_DATAEQUAL(out_partial.ext1, in->ext1);

    Z_HELPER_END;
}

/* }}} */
/* {{{ Enumerated type. */

typedef enum enum1 {
    FOO,
    BAR,
} enum1_t;

static ASN1_ENUM_BEGIN(enum1)
    asn1_enum_reg_val(FOO);
    asn1_enum_reg_val(BAR);
ASN1_ENUM_END();

typedef struct {
    enum1_t e1;
} struct1_t;

static ASN1_SEQUENCE_DESC_BEGIN(struct1);
    asn1_reg_enum(struct1, enum1, e1, 0);
    asn1_set_enum_info(struct1, enum1);
ASN1_SEQUENCE_DESC_END(struct1);

/* }}} */
/* {{{ Integers overflows checks. */

typedef struct ints_seq_t {
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    int64_t i64_bis;
    uint64_t u64_bis;
} ints_seq_t;

typedef struct ints_seq_base_t {
    int64_t i8;
    int64_t u8;
    int64_t i16;
    int64_t u16;
    int64_t i32;
    int64_t u32;
    int64_t i64;
    uint64_t u64;
    uint64_t i64_bis;
    int64_t u64_bis;
} ints_seq_base_t;

#define INTS_SEQ_FIELDS_DESC(pfx)                                            \
    asn1_reg_scalar(pfx, i8, 0);                                             \
    asn1_reg_scalar(pfx, u8, 1);                                             \
    asn1_reg_scalar(pfx, i16, 2);                                            \
    asn1_reg_scalar(pfx, u16, 3);                                            \
    asn1_reg_scalar(pfx, i32, 4);                                            \
    asn1_reg_scalar(pfx, u32, 5);                                            \
    asn1_reg_scalar(pfx, i64, 6);                                            \
    asn1_reg_scalar(pfx, u64, 7);                                            \
    asn1_reg_scalar(pfx, i64_bis, 8);                                        \
    asn1_reg_scalar(pfx, u64_bis, 9)

static ASN1_SEQUENCE_DESC_BEGIN(ints_seq);
    INTS_SEQ_FIELDS_DESC(ints_seq);
ASN1_SEQUENCE_DESC_END(ints_seq);

static ASN1_SEQUENCE_DESC_BEGIN(ints_seq_base);
    INTS_SEQ_FIELDS_DESC(ints_seq_base);
ASN1_SEQUENCE_DESC_END(ints_seq_base);

static int z_assert_ints_seq_equals_base(const ints_seq_t *seq,
                                         const ints_seq_base_t *base)
{
#define ASSERT_FIELD_EQUALS(field)                                           \
    Z_ASSERT_EQ(seq->field, base->field);

    ASSERT_FIELD_EQUALS(i8);
    ASSERT_FIELD_EQUALS(u8);
    ASSERT_FIELD_EQUALS(i16);
    ASSERT_FIELD_EQUALS(u16);
    ASSERT_FIELD_EQUALS(i32);
    ASSERT_FIELD_EQUALS(u32);

#undef ASSERT_FIELD_EQUALS

    Z_HELPER_END;
}

#undef INTS_SEQ_FIELDS_DESC

static int z_translate_ints_seq(const ints_seq_base_t *base,
                                bool expect_error)
{
    t_scope;
    SB_1k(sb);
    ints_seq_t ints;
    pstream_t ps;

    p_clear(&ints, 1);
    Z_ASSERT_N(aper_encode(&sb, ints_seq_base, base));
    ps = ps_initsb(&sb);

    if (expect_error) {
        Z_ASSERT_NEG(t_aper_decode(&ps, ints_seq, false, &ints));
    } else {
        Z_ASSERT_N(t_aper_decode(&ps, ints_seq, false, &ints));
        Z_HELPER_RUN(z_assert_ints_seq_equals_base(&ints, base));
    }

    Z_HELPER_END;
}

/* }}} */
/* {{{ Octet string. */

typedef struct {
    lstr_t str;
} z_octet_string_t;

static ASN1_SEQUENCE_DESC_BEGIN(z_octet_string);
    asn1_reg_string(z_octet_string, str, 0);
ASN1_SEQUENCE_DESC_END(z_octet_string);

/* }}} */
/* {{{ Bit string. */

typedef struct {
    asn1_bit_string_t bs;
} z_bit_string_t;

static ASN1_SEQUENCE_DESC_BEGIN(z_bit_string);
    asn1_reg_string(z_bit_string, bs, 0);
ASN1_SEQUENCE_DESC_END(z_bit_string);

/* }}} */
/* {{{ Open type. */

typedef struct {
    z_octet_string_t os;
} z_open_type_t;

static ASN1_SEQUENCE_DESC_BEGIN(z_open_type);
    asn1_reg_sequence(z_open_type, z_octet_string, os,
                      ASN1_TAG_SEQUENCE_C);
    asn1_set_open_type(z_open_type, (64 << 10));
ASN1_SEQUENCE_DESC_END(z_open_type);

/* }}} */
/* {{{ Sequence of. */

typedef struct {
    ASN1_VECTOR_TYPE(int8) seqof;
} z_seqof_i8_t;

static ASN1_SEQ_OF_DESC_BEGIN(z_seqof_i8);
    asn1_reg_scalar(z_seqof_i8, seqof, 0);
    asn1_set_int_min_max(z_seqof_i8, -3, 3);
ASN1_SEQ_OF_DESC_END(z_seqof_i8);

typedef struct {
    uint8_t a;
    z_seqof_i8_t s;
} z_seqof_t;

GENERIC_INIT(z_seqof_t, z_seqof);

static ASN1_SEQUENCE_DESC_BEGIN(z_seqof);
    asn1_reg_scalar(z_seqof, a, 0);
    asn1_set_int_min_max(z_seqof, 0, 2);

    asn1_reg_seq_of(z_seqof, z_seqof_i8, s, ASN1_TAG_SEQUENCE_C);
    asn1_set_seq_of_min_max(z_seqof, 0, 1024);
    asn1_set_seq_of_extended_min_max(z_seqof, 0, (256 << 10));
ASN1_SEQUENCE_DESC_END(z_seqof);

typedef struct {
    int a;
} z_basic_struct_t;

static ASN1_SEQUENCE_DESC_BEGIN(z_basic_struct);
    asn1_reg_scalar(z_basic_struct, a, 0);
    asn1_set_int_min_max(z_basic_struct, INT32_MIN, INT32_MAX);
ASN1_SEQUENCE_DESC_END(z_basic_struct);

ASN1_DEF_VECTOR(z_basic_struct, z_basic_struct_t);
ASN1_DEF_ARRAY(z_basic_struct, z_basic_struct_t);

typedef struct {
    ASN1_ARRAY_TYPE(z_basic_struct) seqof;
} z_seqof_ptr_t;

static ASN1_SEQ_OF_DESC_BEGIN(z_seqof_ptr);
    asn1_reg_seq_of_sequence(z_seqof_ptr, z_basic_struct, seqof,
                             ASN1_TAG_SEQUENCE_C);
ASN1_SEQ_OF_DESC_END(z_seqof_ptr);

typedef struct {
    z_seqof_ptr_t s;
} z_seqof_wrapper_t;

static ASN1_SEQUENCE_DESC_BEGIN(z_seqof_wrapper);
    asn1_reg_seq_of(z_seqof_wrapper, z_seqof_ptr, s, ASN1_TAG_SEQUENCE_C);
ASN1_SEQUENCE_DESC_END(z_seqof_wrapper);

/* }}} */
/* {{{ Indefinite length case. */

typedef struct {
    /* OCTET STRING (SIZE(2..70000, ...)) */
    lstr_t os;
} z_indef_len_t;

static ASN1_SEQUENCE_DESC_BEGIN(z_indef_len);
    asn1_reg_string(z_indef_len, os, 0);
    asn1_set_str_min_max(z_indef_len, 2, 70000);
    asn1_str_set_extended(z_indef_len);
    asn1_reg_extension(z_indef_len);
ASN1_SEQUENCE_DESC_END(z_indef_len);

/* }}} */
/* {{{ Helpers. */

/* Skip N characters on two pstreams and check that they are the same. */
static int z_ps_skip_and_check_eq(pstream_t *ps1, pstream_t *ps2, int len)
{
    if (len < 0) {
        len = ps_len(ps2);
    }
    Z_ASSERT(ps_has(ps1, len));
    Z_ASSERT(ps_has(ps2, len));

    for (int i = 0; i < len; i++) {
        int c1 = __ps_getc(ps1);
        int c2 = __ps_getc(ps2);

        Z_ASSERT_EQ(c1, c2, "[%d] %x != %x", i, c1, c2);
    }

    Z_HELPER_END;
}

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

    Z_ASSERT_N(aper_encode_number(&bb, val, info, is_signed));
    bs = bs_init_bb(&bb);
    Z_ASSERT_STREQUAL(t_print_be_bb(&bb, NULL), exp_encoding,
                      "unexpected encoding");
    Z_ASSERT_N(aper_decode_number(&bs, info, is_signed, &i64),
               "cannot decode `%s`", exp_encoding);
    Z_ASSERT_EQ(i64, val, "decoded value differs");

    bb_wipe(&bb);
    Z_HELPER_END;
}

static int z_test_aper_len(size_t l, size_t l_min, size_t l_max, int skip,
                           const char *exp_encoding)
{
    t_scope;
    BB_1k(bb);
    bit_stream_t bs;
    size_t len;

    bb_add0s(&bb, skip);

    aper_write_len(&bb, l, l_min, l_max, NULL);
    bs = bs_init_bb(&bb);
    Z_ASSERT_N(bs_skip(&bs, skip));
    Z_ASSERT_STREQUAL(exp_encoding, t_print_be_bs(bs, NULL));
    Z_ASSERT_N(aper_read_len(&bs, l_min, l_max, &len, NULL));
    Z_ASSERT_EQ(len, l);
    bb_wipe(&bb);

    Z_HELPER_END;
}

static int z_test_aper_nsnnwn(size_t n, const char *exp_encoding)
{
    t_scope;
    BB_1k(bb);
    bit_stream_t bs;
    size_t nsnnwn;

    bb_reset(&bb);
    aper_write_nsnnwn(&bb, n);
    bs = bs_init_bb(&bb);
    Z_ASSERT_STREQUAL(exp_encoding, t_print_be_bs(bs, NULL));
    Z_ASSERT_N(aper_read_nsnnwn(&bs, &nsnnwn));
    Z_ASSERT_EQ(nsnnwn, n);
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

static int z_assert_bs_be_equal(bit_stream_t bs1, bit_stream_t bs2)
{
    int bit = 0;

    Z_ASSERT_EQ(bs_len(&bs1), bs_len(&bs2));
    while (!bs_done(&bs1)) {
        Z_ASSERT_EQ(__bs_be_get_bit(&bs1), __bs_be_get_bit(&bs2),
                    "bit strings differ at bit [%d]", bit);
        bit++;
    }

    Z_HELPER_END;
}

static int
z_test_aper_octet_string(const asn1_cnt_info_t *info,
                         const char *octet_string,
                         bool copy, const char *exp_bits)
{
    t_scope;
    BB_1k(bb __attribute__((cleanup(bb_wipe))));
    bit_stream_t bs;
    lstr_t decoded_octet_string;

    Z_ASSERT_N(aper_encode_octet_string(&bb, LSTR(octet_string), info),
               "encoding error");
    Z_ASSERT_STREQUAL(t_print_be_bb(&bb, NULL), exp_bits,
                      "unexpected encoding");
    bs = bs_init_bb(&bb);
    Z_ASSERT_N(t_aper_decode_octet_string(&bs, info, copy,
                                          &decoded_octet_string),
               "decoding error");
    Z_ASSERT_DATAEQUAL(LSTR(octet_string), decoded_octet_string,
                       "the decoded octet string is not the same "
                       "as the one initially encoded");

    Z_HELPER_END;
}

static int
z_test_aper_bstring(const asn1_cnt_info_t *info, const char *bit_string,
                    int skip, const char *exp_bits)
{
    t_scope;
    BB_1k(bb __attribute__((cleanup(bb_wipe))));
    BB_1k(src_bb __attribute__((cleanup(bb_wipe))));
    BB_1k(dst_bb __attribute__((cleanup(bb_wipe))));
    bit_stream_t src;
    bit_stream_t dst;
    bit_stream_t bs;

    bb_reset(&src_bb);
    for (const char *s = bit_string; *s; s++) {
        if (*s == '1' || *s == '0') {
            bb_be_add_bit(&src_bb, *s == '1');
        } else {
            Z_ASSERT_EQ(*s, '.', "unauthorized character");
        }
    }
    src = bs_init_bb(&src_bb);
    bb_add0s(&bb, skip);
    Z_ASSERT_N(aper_encode_bstring(&bb, &src, info));
    bs = bs_init_bb(&bb);
    Z_ASSERT_N(bs_skip(&bs, skip));
    Z_ASSERT_STREQUAL(exp_bits, t_print_be_bs(bs, NULL),
                      "unexpected encoding");
    Z_ASSERT_N(aper_decode_bstring(&bs, info, &dst_bb), "decoding error");
    dst = bs_init_bb(&dst_bb);
    Z_ASSERT_EQ(bs_len(&dst), bs_len(&src),
                "encoding length differs from expectations");
    Z_HELPER_RUN(z_assert_bs_be_equal(dst, src),
                 "bit string changed after encoding+decoding ('%s' -> '%s')",
                 t_print_be_bs(src, NULL), t_print_be_bs(dst, NULL));

    Z_HELPER_END;
}

static int
z_check_seq_of_number(bit_stream_t *bs, const asn1_int_info_t *info,
                      int64_t exp)
{
    int64_t v;

    Z_ASSERT_N(aper_decode_number(bs, info, true, &v));
    Z_ASSERT_EQ(v, exp);
    Z_HELPER_END;
}

static int z_check_seq_of_fragment(bit_stream_t *bs, int min, int max,
                                   int start, int end, qv_t(i8) *values)
{
    asn1_int_info_t info;

    asn1_int_info_init(&info);
    asn1_int_info_set_min(&info, min);
    asn1_int_info_set_max(&info, max);
    asn1_int_info_update(&info, true);

    for (int i = start; i < end; i++) {
        Z_ASSERT_LT(i, values->len);
        Z_HELPER_RUN(z_check_seq_of_number(bs, &info, values->tab[i]),
                     "value [%d(%x)]", i, i);
    }

    Z_HELPER_END;
}

/* }}} */

Z_GROUP_EXPORT(asn1_aper) {
    /* {{{ u16 */
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
                Z_ASSERT_N(aper_read_u16_m(&bs, len, t[i].d_max, &u16),
                           "[i:%d]", i);
                Z_ASSERT_EQ(u16, t[i].d, "[i:%d] len=%zu", i, len);
            }
            Z_ASSERT_STREQUAL(t[i].s, t_print_be_bb(&bb, NULL), "[i:%d]", i);
        }

        bb_wipe(&bb);
    } Z_TEST_END;
    /* }}} */
    /* {{{ len */
    Z_TEST(len, "aligned per: aper_write_len/aper_read_len") {
        Z_HELPER_RUN(z_test_aper_len(15, 15, 15, 0, ""));
        Z_HELPER_RUN(z_test_aper_len(7, 3, 18, 0, ".0100"));
        Z_HELPER_RUN(z_test_aper_len(15, 0, ASN1_MAX_LEN, 0, ".00001111"));
        Z_HELPER_RUN(z_test_aper_len(0x1b34, 0, ASN1_MAX_LEN, 0,
                                     ".10011011.00110100"));
        Z_HELPER_RUN(z_test_aper_len(32, 1, 160, 1, "0001111.1"));
        Z_HELPER_RUN(z_test_aper_len(15, 0, ASN1_MAX_LEN, 3,
                                     "00000.00001111"));
        Z_HELPER_RUN(z_test_aper_len(0x1b34, 0, ASN1_MAX_LEN, 5,
                                     "000.10011011.00110100"));

        /* ITU-T X.691 - §11.5.7 - The indefinite length case. */
        /* FIXME We should probably apply the encoding specified in clause
         * 13.2.6 a) as suggested by clause 11.5.7.4. */
        Z_HELPER_RUN(z_test_aper_len(10, 5, 100000, 3,
                                     "00000.00001010"));
    } Z_TEST_END;
    /* }}} */
    /* {{{ nsnnwn */
    Z_TEST(nsnnwn, "aligned per: aper_write_nsnnwn/aper_read_nsnnwn") {
        Z_HELPER_RUN(z_test_aper_nsnnwn(0, ".0000000"));
        Z_HELPER_RUN(z_test_aper_nsnnwn(0xe, ".0001110"));
        Z_HELPER_RUN(z_test_aper_nsnnwn(96, ".10000000.00000001.01100000"));
        Z_HELPER_RUN(z_test_aper_nsnnwn(128, ".10000000.00000001.10000000"));
    } Z_TEST_END;
    /* }}} */
    /* {{{ number */
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

            /* Example from clause 13.2.6 a). */
            { 256, OPT(256), OPT(1234567), false, false,
              ".00000000.00000000" },
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
    /* }}} */
    /* {{{ 64bits_number_overflows */
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
    /* }}} */
    /* {{{ ostring */
    Z_TEST(ostring, "aligned per: aper_{encode,decode}_ostring") {
        asn1_cnt_info_t cnt_info;

        /* {{{ Unconstrained octet string. */

        Z_HELPER_RUN(z_test_aper_octet_string(
                NULL, "aaa", true,
                ".00000011.01100001.01100001.01100001"));
        Z_HELPER_RUN(z_test_aper_octet_string(
                NULL, "aaa", false,
                ".00000011.01100001.01100001.01100001"));

        cnt_info = (asn1_cnt_info_t){
            .max     = SIZE_MAX,
        };
        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "aaa", true,
                ".00000011.01100001.01100001.01100001"));

        /* }}} */
        /* {{{ Fully constrained octet string. */

        cnt_info = (asn1_cnt_info_t){
            .min     = 3,
            .max     = 3,
        };
        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "aaa", false,
                ".01100001.01100001.01100001"));

        cnt_info = (asn1_cnt_info_t){
            .max     = 23,
        };
        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "aaa", false,
                ".00011000.01100001.01100001.01100001"));

        /* }}} */
        /* {{{ Extended octet string. */

        cnt_info = (asn1_cnt_info_t){
            .min      = 1,
            .max      = 2,
            .extended = true,
            .ext_min  = 3,
            .ext_max  = 3,
        };

        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "aa", true,
                ".01000000.01100001.01100001"));
        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "aaa", true,
                ".10000000.00000011.01100001.01100001.01100001"));

        cnt_info = (asn1_cnt_info_t){
            .min      = 2,
            .max      = 2,
            .extended = true,
            .ext_max  = SIZE_MAX,
        };

        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "aa", true,
                ".00110000.10110000.1"));
        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "a", true,
                ".10000000.00000001.01100001"));

        /* }}} */
        /* {{{ ITU-T X.691 - §11.5.7 - The indefinite length case. */

        cnt_info = (asn1_cnt_info_t){
            .min = 4,
            .max = 70000,
            .extended = true,
            .ext_max = SIZE_MAX,
        };

        /* FIXME We should probably apply the encoding specified in clause
         * 13.2.6 a) as suggested by clause 11.5.7.4. */
        Z_HELPER_RUN(z_test_aper_octet_string(
                &cnt_info, "abcd", true,
                ".00000000.00000100.01100001.01100010.01100011.01100100"));

        /* }}} */
    } Z_TEST_END;
    /* }}} */
    /* {{{ bstring */
    Z_TEST(bstring, "aligned per: aper_{encode,decode}_bstring") {
        asn1_cnt_info_t unconstrained;
        asn1_cnt_info_t fully_constrained3;
        asn1_cnt_info_t fully_constrained15;
        asn1_cnt_info_t fully_constrained16;
        asn1_cnt_info_t fully_constrained17;
        asn1_cnt_info_t partially_constrained1;
        asn1_cnt_info_t partially_constrained2;
        asn1_cnt_info_t extended1;
        asn1_cnt_info_t extended2;
        asn1_cnt_info_t fully_constrained_extended;

        /* {{{ BIT STRING */

        asn1_cnt_info_init(&unconstrained);

        Z_HELPER_RUN(z_test_aper_bstring(&unconstrained, "01010101",
                                         0, ".00001000.01010101"));

        asn1_cnt_info_init(&fully_constrained3);
        fully_constrained3.min = 3;
        fully_constrained3.max = 3;

        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained3, "101", 0,
                                         ".101"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(15)) */

        asn1_cnt_info_init(&fully_constrained15);
        fully_constrained15.min = 15;
        fully_constrained15.max = 15;

        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained15,
                                         ".10110011.1000111", 0,
                                         ".10110011.1000111"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained15,
                                         ".10110011.1000111", 1,
                                         "1011001.11000111"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained15,
                                         ".10110011.1000111", 2,
                                         "101100.11100011.1"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(16)) */

        asn1_cnt_info_init(&fully_constrained16);
        fully_constrained16.min = 16;
        fully_constrained16.max = 16;

        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained16,
                                         ".10110011.10001110", 0,
                                         ".10110011.10001110"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained16,
                                         ".10110011.10001110", 1,
                                         "1011001.11000111.0"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained16,
                                         ".10110011.10001110", 2,
                                         "101100.11100011.10"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(17)) */

        asn1_cnt_info_init(&fully_constrained17);
        fully_constrained17.min = 17;
        fully_constrained17.max = 17;

        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained17,
                                         ".10110011.10001110.0", 0,
                                         ".10110011.10001110.0"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained17,
                                         ".10110011.10001110.0", 1,
                                         "0000000.10110011.10001110.0"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained17,
                                         ".10110011.10001110.0", 2,
                                         "000000.10110011.10001110.0"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(0..23)) */

        asn1_cnt_info_init(&partially_constrained1);
        partially_constrained1.max = 23;

        Z_HELPER_RUN(z_test_aper_bstring(&partially_constrained1, "101",
                                         0, ".00011000.101"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(0..1)) */

        asn1_cnt_info_init(&partially_constrained2);
        partially_constrained2.max = 1;

        Z_HELPER_RUN(z_test_aper_bstring(&partially_constrained2, "1",
                                         0, ".10000000.1"));
        Z_HELPER_RUN(z_test_aper_bstring(&partially_constrained2, "1",
                                         1, "1000000.1"));
        Z_HELPER_RUN(z_test_aper_bstring(&partially_constrained2, "",
                                         0, ".0"));
        Z_HELPER_RUN(z_test_aper_bstring(&partially_constrained2, "",
                                         1, "0"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(1..2, ..., 3)) */

        asn1_cnt_info_init(&extended1);
        extended1.min = 1;
        extended1.max = 2;
        extended1.extended = true;
        extended1.ext_min = 3;
        extended1.ext_max = 3;

        Z_HELPER_RUN(z_test_aper_bstring(&extended1, "10", 0,
                                         ".01000000.10"));
        Z_HELPER_RUN(z_test_aper_bstring(&extended1, "011", 0,
                                         ".10000000.00000011.011"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(1..160, ..., 1..65536)) */

        asn1_cnt_info_init(&extended2);
        extended2.min = 1;
        extended2.max = 160;
        extended2.extended = true;
        extended2.ext_min = 1;
        extended2.ext_max = 65536;

        Z_HELPER_RUN(z_test_aper_bstring(&extended2, "00", 0,
                                         ".00000000.10000000.00"));
        Z_HELPER_RUN(z_test_aper_bstring(&extended2,
                                         "010101010101010101", 0,
                                         ".00001000.10000000"
                                         ".01010101.01010101.01"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(2, ...)) */

        asn1_cnt_info_init(&fully_constrained_extended);
        fully_constrained_extended.min = 2;
        fully_constrained_extended.max = 2;
        fully_constrained_extended.extended = true;

        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained_extended,
                                         "11", 0, ".011"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained_extended,
                                         "011", 0, ".10000000.00000011.011"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained_extended,
                                         "", 0, ".10000000.00000000"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained_extended,
                                         "", 5, "100.00000000"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained_extended,
                                         "1", 0, ".10000000.00000001.1"));
        Z_HELPER_RUN(z_test_aper_bstring(&fully_constrained_extended,
                                         "1", 5, "100.00000001.1"));

        /* }}} */
        /* {{{ BIT STRING (SIZE(4..70000, ...)) */

        /* ITU-T X.691 - §11.5.7 - The indefinite length case. */
        /* XXX This encoding is similar to the one we get with an obvious
         * online ASN.1 playground we often compare our implementations to.
         *
         * Schema DEFINITIONS AUTOMATIC TAGS ::=
         * BEGIN
         *   Bs ::= BIT STRING (SIZE(4..70000, ...))
         * END
         *
         * value Bs ::= '010011000111'B
         *
         * But there is no certainty that the BIT STRING length is correctly
         * encoded there, as the specification mentions:
         *
         * > ... the value ("n" – "lb") shall be encoded ...
         *
         * So we should probably encode 12 - 4 == 8, but we simply encode the
         * BIT STRING length directly (12).
         */
        /* FIXME We should probably apply the encoding specified in clause
         * 13.2.6 a) as suggested by clause 11.5.7.4. */
        Z_HELPER_RUN(z_test_aper_bstring(
                &(asn1_cnt_info_t){
                    .min = 4,
                    .max = 70000,
                    .extended = true,
                    .ext_max = ASN1_MAX_LEN,
                }, "010011000111", 0,
                ".00000000.00001100.01001100.0111"));

        /* }}} */
    } Z_TEST_END;
    /* }}} */
    /* {{{ length constraints printing */
    Z_TEST(sb_add_asn1_len_constraints, "") {
        SB_1k(buf);
        asn1_cnt_info_t constraints;

        asn1_cnt_info_init(&constraints);
        constraints.min = constraints.max = 42;
        sb_add_asn1_len_constraints(&buf, &constraints);
        Z_ASSERT_STREQUAL(buf.data, "SIZE(42)");

        sb_reset(&buf);
        constraints.max = 100;
        sb_add_asn1_len_constraints(&buf, &constraints);
        Z_ASSERT_STREQUAL(buf.data, "SIZE(42..100)");

        sb_reset(&buf);
        constraints.extended = true;
        sb_add_asn1_len_constraints(&buf, &constraints);
        Z_ASSERT_STREQUAL(buf.data, "SIZE(42..100, ...)");

        sb_reset(&buf);
        constraints.ext_min = constraints.ext_max = 200;
        sb_add_asn1_len_constraints(&buf, &constraints);
        Z_ASSERT_STREQUAL(buf.data, "SIZE(42..100, ..., 200)");

        sb_reset(&buf);
        constraints.ext_min = 10;
        constraints.ext_max = 400;
        sb_add_asn1_len_constraints(&buf, &constraints);
        Z_ASSERT_STREQUAL(buf.data, "SIZE(42..100, ..., 10..400)");

        sb_reset(&buf);
        constraints.ext_max = SIZE_MAX;
        sb_add_asn1_len_constraints(&buf, &constraints);
        Z_ASSERT_STREQUAL(buf.data, "SIZE(42..100, ..., 10..MAX)");
    } Z_TEST_END;
    /* }}} */
    /* {{{ enum */
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
    /* }}} */
    /* {{{ enum_ext_defval */
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
    /* }}} */
    /* {{{ choice */
    Z_TEST(choice, "choice") {
        t_scope;
        SB_1k(buf);

        for (int i = 2; i <= 15; i++) {
            pstream_t ps;
            choice1_t in;
            choice1_t out;

            sb_reset(&buf);
            p_clear(&in, 1);
            in.iop_tag = 1;
            in.i = i;

            Z_ASSERT_N(aper_encode(&buf, choice1, &in));
            ps = ps_initsb(&buf);
            Z_ASSERT_EQ(ps_len(&ps), 1u);
            Z_ASSERT_EQ(*ps.b, (i - 2) << 4);
            Z_ASSERT_N(t_aper_decode(&ps, choice1, false, &out));

            Z_ASSERT_EQ(in.iop_tag, out.iop_tag);
            Z_ASSERT_EQ(in.i, out.i);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ extended_choice */
    Z_TEST(extended_choice, "extended choice") {
        t_scope;
        struct {
            tstiop__asn1_ext_choice__t in;
            lstr_t aper_bytes;
        } tests[3];

        tstiop__asn1_ext_choice__t out;
        SB(buf, 42);
        pstream_t ps;

        tests[0].in = IOP_UNION(tstiop__asn1_ext_choice, i, 192);
        tests[0].aper_bytes = LSTR_IMMED_V("\x00\x00\x96");
        tests[1].in = IOP_UNION(tstiop__asn1_ext_choice, ext_s, LSTR("test"));
        tests[1].aper_bytes = LSTR_IMMED_V("\x80\x05\x04\x74\x65\x73\x74");
        tests[2].in = IOP_UNION(tstiop__asn1_ext_choice, ext_i, 667);
        tests[2].aper_bytes = LSTR_IMMED_V("\x81\x02\x00\x01");

        carray_for_each_ptr(t, tests) {
            sb_reset(&buf);
            Z_ASSERT_N(aper_encode(&buf, tstiop__asn1_ext_choice_, &t->in));
            Z_ASSERT_DATAEQUAL(t->aper_bytes, LSTR_SB_V(&buf));
            ps = ps_initsb(&buf);
            Z_ASSERT_N(t_aper_decode(&ps, tstiop__asn1_ext_choice_, false,
                                     &out));
            Z_ASSERT_IOPEQUAL(tstiop__asn1_ext_choice, &t->in, &out);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ extended_sequence */
    Z_TEST(extended_sequence, "extended sequence") {
        struct {
            const char *title;
            sequence1_t in;
            lstr_t encoding;
        } tests[] = { {
            "no extension",
            { OPT(10), -20, LSTR_NULL, OPT_NONE, OPT_NONE },
            LSTR_IMMED("\x64\x01\x16"),
        }, {
            "one extension",
            { OPT(10), -20, LSTR_IMMED("toto"), OPT_NONE, OPT_NONE },
            LSTR_IMMED("\xE4\x01\x16\x05\x00\x05\x04toto"),
        }, {
            "more extensions",
            { OPT(10), -20, LSTR_NULL, OPT(-90000), OPT(42) },
            LSTR_IMMED("\xE4\x01\x16\x04\xC0\x03\x40\x27\x10\x02\x00\x2A"),
        } };

        carray_for_each_ptr(t, tests) {
            Z_HELPER_RUN(z_test_seq_ext(&t->in, t->encoding),
                         "test failure for `%s`", t->title);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ ints_overflows */
    Z_TEST(ints_overflows, "integers overflows") {
        ints_seq_base_t base_min = {
            INT8_MIN, 0, INT16_MIN, 0, INT32_MIN, 0, INT64_MIN, 0, 0, 0,
        };
        ints_seq_base_t base_max = {
            INT8_MAX, UINT8_MAX, INT16_MAX, UINT16_MAX,
            INT32_MAX, UINT32_MAX, INT64_MAX, UINT64_MAX,
            0, 0,
        };
        ints_seq_base_t base;

        struct {
            const char *title;
            int64_t v;
            int64_t *base_field;
        } err_cases[] = {
#define TEST(x)                                                              \
    { "i" #x ", min - 1", (int64_t)INT##x##_MIN - 1, &base.i##x },           \
    { "i" #x ", max + 1", (int64_t)INT##x##_MAX + 1, &base.i##x },           \
    { "u" #x ", min - 1", (int64_t)-1, &base.u##x },                         \
    { "u" #x ", max + 1", (int64_t)UINT##x##_MAX + 1, &base.u##x }

            TEST(8),
            TEST(16),
            TEST(32),

#undef TEST

            /* XXX INT64_MIN - 1 is untestable this way */
            { "i64, max + 1", (uint64_t)INT64_MAX + 1,
                (int64_t *)&base.i64_bis },
            { "u64, min + 1", -1, &base.u64_bis },
            /* XXX UINT64_MAX + 1 is untestable this way */
        };

        Z_HELPER_RUN(z_translate_ints_seq(&base_min, false),
                     "unexected error on minimum values");
        Z_HELPER_RUN(z_translate_ints_seq(&base_max, false),
                     "unexected error on maximum values");

        p_clear(&base, 1);
        Z_HELPER_RUN(z_translate_ints_seq(&base, false),
                     "unexected error on zeros");

        carray_for_each_ptr(t, err_cases) {
            p_clear(&base, 1);
            *t->base_field = t->v;
            Z_HELPER_RUN(z_translate_ints_seq(&base, true),
                         "test `%s`: no overflow detection", t->title);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ enumerated */
    Z_TEST(enumerated, "enumerated type check (mostly for auto-wipe)") {
        t_scope;
        SB_1k(buf);
        pstream_t ps;
        struct1_t s1[2];
        lstr_t expected_encoding = LSTR_IMMED("\x80");

        p_clear(&s1[0], 1);
        s1[0].e1 = BAR;

        Z_ASSERT_N(aper_encode(&buf, struct1, &s1[0]), "encoding failure");
        Z_ASSERT_DATAEQUAL(LSTR_SB_V(&buf), expected_encoding);
        ps = ps_initsb(&buf);
        Z_ASSERT_N(t_aper_decode(&ps, struct1, false, &s1[1]),
                   "decoding failure");
        Z_ASSERT_EQ(s1[1].e1, s1[0].e1);
    } Z_TEST_END;
    /* }}} */
    /* {{{ fragmented_octet_string */
    Z_TEST(fragmented_octet_string, "") {
        t_scope;
        sb_t buf;
        sb_t str;
        pstream_t str_ps;
        z_octet_string_t os_before;
        z_octet_string_t os_after;
        pstream_t ps;

        sb_init(&buf);
        sb_init(&str);
        for (int i = 0; i < 123456; i++) {
            sb_addc(&str, (char)i);
        }

        p_clear(&os_before, 1);
        os_before.str = LSTR_SB_V(&str);
        Z_ASSERT_N(aper_encode(&buf, z_octet_string, &os_before),
                   "unexpected failure");

        /* Check the fragmented encoding. */
        str_ps = ps_initsb(&str);
        ps = ps_initsb(&buf);
        /* First fragment: 4 x 16k. */
        Z_ASSERT_EQ(ps_getc(&ps), 0xc4);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &str_ps, (64 << 10)));
        /* Second fragment: 3 x 16k. */
        Z_ASSERT_EQ(ps_getc(&ps), 0xc3);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &str_ps, (48 << 10)));
        /* Remainder. */
        Z_ASSERT(ps_has(&ps, 2));
        Z_ASSERT_EQ(ps_len(&ps), ps_len(&str_ps) + 2);
        Z_ASSERT_EQ(get_unaligned_be16(ps.b), (0x8000 | ps_len(&str_ps)));
        __ps_skip(&ps, 2);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &str_ps, ps_len(&str_ps)));
        ps = ps_initsb(&buf);
        Z_ASSERT_N(t_aper_decode(&ps, z_octet_string, false, &os_after));
        Z_ASSERT_DATAEQUAL(os_before.str, os_after.str);

        /* Special case: all the data is in the fragments
         * (a single 16k fragment in this case). */
        os_before.str.len = (16 << 10);
        sb_reset(&buf);
        Z_ASSERT_N(aper_encode(&buf, z_octet_string, &os_before),
                   "unexpected failure");
        str_ps = ps_initlstr(&os_before.str);
        ps = ps_initsb(&buf);
        /* Fragment: 1 x 16k. */
        Z_ASSERT_EQ(ps_getc(&ps), 0xc1);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &str_ps, (16 << 10)));
        /* No remainder: just a octet with value == zero. */
        Z_ASSERT_EQ(ps_getc(&ps), 0x00);

        sb_wipe(&str);
        sb_wipe(&buf);
    } Z_TEST_END;
    /* }}} */
    /* {{{ fragmented_bit_string */
    Z_TEST(fragmented_bit_string, "") {
        t_scope;
        z_bit_string_t bs_before;
        z_bit_string_t bs_after;
        bb_t bb __attribute__((cleanup(bb_wipe))) = *bb_init(&bb);
        SB_8k(buf);
        pstream_t ps;
        bit_stream_t bs;
        uint64_t uv;

        for (int i = 0; i < 20000; i++) {
            bb_be_add_bit(&bb, i & 1);
        }
        p_clear(&bs_before, 1);
        bs_before.bs.data = bb.bytes;
        bs_before.bs.bit_len = bb.len;
        Z_ASSERT_N(aper_encode(&buf, z_bit_string, &bs_before),
                   "unexpected error");

        /* Check the fragmented encoding. */
        ps = ps_initsb(&buf);
        bs = bs_init_ps(&ps, 0);

        /* First fragment: 16k. */
        Z_ASSERT_N(bs_align(&bs));
        Z_ASSERT_N(bs_be_get_bits(&bs, 8, &uv));
        Z_ASSERT_EQ(uv, (uint64_t)0xc1);
        for (int i = 0; i < (16 << 10); i++) {
            Z_ASSERT_EQ(bs_be_get_bit(&bs), i & 1);
        }

        /* Last fragment: 20000 - 16k = 3616. */
        Z_ASSERT(bs_is_aligned(&bs));
        Z_ASSERT_N(bs_be_get_bits(&bs, 16, &uv));
        Z_ASSERT_EQ(uv, (uint64_t)(3616 | 0x8000));
        for (int i = 0; i < 3616; i++) {
            Z_ASSERT_EQ(bs_be_get_bit(&bs), i & 1);
        }
        Z_ASSERT(bs_done(&bs));

        /* Decode the resulting BIT STRING. */
        Z_ASSERT_N(t_aper_decode(&ps, z_bit_string, false, &bs_after),
                   "unexpected failure");

        Z_ASSERT_EQ(bs_after.bs.bit_len, bs_before.bs.bit_len);
        for (int i = 0; i < bs_before.bs.bit_len; i++) {
            Z_ASSERT_EQ(TST_BIT(bs_after.bs.data, i),
                        TST_BIT(bs_before.bs.data, i),
                        "bit [%d] differs", i);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ fragmented_open_type */
    Z_TEST(fragmented_open_type, "") {
        t_scope;
        sb_t str;
        sb_t buf;
        sb_t os_buf;
        lstr_t motif = LSTR("OPEN TYPE-");
        z_open_type_t ot_before;
        z_open_type_t ot_after;
        pstream_t ps;
        pstream_t exp_ps;

        sb_init(&str);
        for (int i = 0; i < 20000; i++) {
            sb_addc(&str, motif.s[i % motif.len]);
        }

        p_clear(&ot_before, 1);
        ot_before.os.str = LSTR_SB_V(&str);

        sb_init(&buf);
        Z_ASSERT_N(aper_encode(&buf, z_open_type, &ot_before),
                   "unexpected failure");
        ps = ps_initsb(&buf);

        /* Encode the octet string twice: it should be the same as having the
         * octet string in an open type. */
        sb_init(&os_buf);
        sb_addsb(&os_buf, &str);
        for (int i = 0; i < 2; i++) {
            t_scope;
            z_octet_string_t os;

            p_clear(&os, 1);
            os.str = t_lstr_dup(LSTR_SB_V(&os_buf));
            sb_reset(&os_buf);
            Z_ASSERT_N(aper_encode(&os_buf, z_octet_string, &os),
                       "unexpected failure (supposedly already tested with "
                       "fragmented_octet_string)");
        }
        Z_ASSERT_DATAEQUAL(LSTR_SB_V(&buf), LSTR_SB_V(&os_buf));
        exp_ps = ps_initsb(&os_buf);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &exp_ps, ps_len(&exp_ps)));
        Z_ASSERT(ps_done(&ps));

        ps = ps_initsb(&buf);
        Z_ASSERT_N(t_aper_decode(&ps, z_open_type, false, &ot_after));
        Z_ASSERT_LSTREQUAL(ot_before.os.str, ot_after.os.str);
        sb_wipe(&str);
        sb_wipe(&buf);
        sb_wipe(&os_buf);
    } Z_TEST_END;
    /* }}} */
    /* {{{ fragmented_seq_of */
    Z_TEST(fragmented_seq_of, "") {
        t_scope;
        z_seqof_t seq_of_before;
        z_seqof_t seq_of_after;
        qv_t(i8) vec;
        int seqof_len = 100000;
        int min = -3;
        int max = 3;
        SB_1k(buf);
        pstream_t ps;
        bit_stream_t bs;
        uint64_t uv;

        t_qv_init(&vec, seqof_len);
        for (int i = 0; i < seqof_len; i++) {
            qv_append(&vec, min + (i % (max - min + 1)));
        }

        z_seqof_init(&seq_of_before);
        seq_of_before.a = 1;
        seq_of_before.s.seqof = ASN1_VECTOR(ASN1_VECTOR_TYPE(int8),
                                            vec.tab, vec.len);
        Z_ASSERT_N(aper_encode(&buf, z_seqof, &seq_of_before));

        /* Check the fragmented encoding. */
        ps = ps_initsb(&buf);
        bs = bs_init_ps(&ps, 0);
        Z_ASSERT_N(bs_be_get_bits(&bs, 2, &uv));
        Z_ASSERT_EQ(uv, seq_of_before.a);

        /* First fragment: 4 x 16k. */
        Z_ASSERT_N(bs_align(&bs));
        Z_ASSERT_N(bs_be_get_bits(&bs, 8, &uv));
        Z_ASSERT_EQ(uv, (uint64_t)0xc4);
        Z_HELPER_RUN(z_check_seq_of_fragment(&bs, min, max, 0, (64 << 10),
                                             &vec));
        /* Second fragment: 2 * 16k. */
        Z_ASSERT_N(bs_align(&bs));
        Z_ASSERT_N(bs_be_get_bits(&bs, 8, &uv));
        Z_ASSERT_EQ(uv, (uint64_t)0xc2);
        Z_HELPER_RUN(z_check_seq_of_fragment(&bs, min, max, (64 << 10),
                                             (96 << 10), &vec));

        /* Last fragment: 100000 - 96k = 1696. */
        Z_ASSERT_N(bs_align(&bs));
        Z_ASSERT_N(bs_be_get_bits(&bs, 16, &uv));
        Z_ASSERT_EQ(uv, (uint64_t)(1696 | 0x8000));
        Z_HELPER_RUN(z_check_seq_of_fragment(&bs, min, max, (96 << 10),
                                             100000, &vec));
        Z_ASSERT(bs_done(&bs));

        /* Test decoding. */
        Z_ASSERT_N(t_aper_decode(&ps, z_seqof, false, &seq_of_after));

        Z_ASSERT_EQ(seq_of_before.a, seq_of_after.a);
        Z_ASSERT_EQ(seq_of_before.s.seqof.len, seq_of_after.s.seqof.len);
        for (int i = 0; i < seq_of_before.s.seqof.len; i++) {
            Z_ASSERT_EQ(seq_of_before.s.seqof.data[i],
                        seq_of_after.s.seqof.data[i], "[%d]", i);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ seq_of_ptr */
    Z_TEST(seq_of_ptr, "check arrays of pointers encoding/decoding") {
        t_scope;
        z_seqof_wrapper_t seqof_ptr_before;
        z_seqof_wrapper_t seqof_ptr_after;
        int len = 86;
        SB_1k(buf);
        pstream_t ps;

        p_clear(&seqof_ptr_before, 1);
        seqof_ptr_before.s.seqof.data = t_new_raw(z_basic_struct_t *, len);
        for (int i = 0; i < len; i++) {
            z_basic_struct_t *item;

            item = t_new(z_basic_struct_t, 1);
            item->a = i;
            seqof_ptr_before.s.seqof.data[i] = item;
        }

        Z_ASSERT_N(aper_encode(&buf, z_seqof_wrapper, &seqof_ptr_before),
                   "encoding failure");
        ps = ps_initsb(&buf);
        Z_ASSERT_N(t_aper_decode(&ps, z_seqof_wrapper, false,
                                 &seqof_ptr_after), "decoding failure");
        Z_ASSERT_EQ(seqof_ptr_after.s.seqof.len,
                    seqof_ptr_before.s.seqof.len);
        for (int i = 0; i < seqof_ptr_after.s.seqof.len; i++) {
            const z_basic_struct_t *s_before;
            const z_basic_struct_t *s_after;

            s_before = seqof_ptr_before.s.seqof.data[i];
            s_after = seqof_ptr_after.s.seqof.data[i];
            Z_ASSERT_EQ(s_after->a, s_before->a, "item [%d] differs", i);
        }
    } Z_TEST_END;
    /* }}} */
    /* {{{ indefinite_length_case_octet_string_over_64k */

    Z_TEST(indefinite_length_case_octet_string_over_64k, "") {
        t_scope;
        /* XXX Not to be confused with BER indefinite length, that allows to
         * encode SEQUENCE OF or SET OF with an end marker (00 00). */
        z_indef_len_t before;
        z_indef_len_t after;
#define OCTET_STRING_LEN  68000
        t_SB(os, OCTET_STRING_LEN + 1);
        t_SB(buf, OCTET_STRING_LEN + 1);
        pstream_t ps;
        pstream_t os_ps;
        lstr_t pattern = LSTR("9UFH8904YUhjdlqeijf");

        for (int i = 0; i < OCTET_STRING_LEN; i++) {
            sb_addc(&os, pattern.s[i % pattern.len]);
        }

        p_clear(&before, 1);
        before.os = LSTR_SB_V(&os);

        Z_ASSERT_N(aper_encode(&buf, z_indef_len, &before),
                   "encoding failure");

        /* FIXME We use fragmented encoding here but the specification makes
         * think that we should encode the whole length in a single bitfield
         * instead (ITU-T X.691 - §11.5.7 - The indefinite length case).
         * We should probably apply the encoding specified in clause 13.2.6 a)
         * as suggested by clause 11.5.7.4. */
        /* Check encoding. */
        ps = ps_initsb(&buf);
        Z_ASSERT_EQ(ps_getc(&ps), 0x00);
        Z_ASSERT_EQ(ps_getc(&ps), 0xc4);
        os_ps = ps_initlstr(&before.os);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &os_ps, (64 << 10)));
        Z_ASSERT(ps_has(&ps, 2));
        Z_ASSERT_EQ(get_unaligned_be16(ps.b), (0x8000 | ps_len(&os_ps)));
        __ps_skip(&ps, 2);
        Z_HELPER_RUN(z_ps_skip_and_check_eq(&ps, &os_ps, -1));
        Z_ASSERT(ps_done(&ps));

        /* Check decoding. */
        ps = ps_initsb(&buf);
        Z_ASSERT_N(t_aper_decode(&ps, z_indef_len, false, &after),
                   "decoding failure");
        Z_ASSERT(lstr_equal(after.os, before.os), "unexpected failure");

#undef OCTET_STRING_LEN
    } Z_TEST_END;

    /* }}} */
} Z_GROUP_END
