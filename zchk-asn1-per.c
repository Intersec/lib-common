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

#include "asn1-per.h"
#include "z.h"
#include "iop.h"
#include "iop/tstiop.iop.h"

/* {{{ Choice */

typedef struct {
    uint16_t iop_tag;
    union {
        int i;
    };
} choice1_t;

static __ASN1_IOP_CHOICE_DESC_BEGIN(desc, choice1);
    asn1_reg_scalar(desc, choice1, i, 0);
    asn1_set_int_min_max(desc, 2, 15);
ASN1_CHOICE_DESC_END(desc);

/* }}} */
/* {{{ Extended choice. */

static __ASN1_IOP_CHOICE_DESC_BEGIN(desc, tstiop__asn1_ext_choice_);
    asn1_reg_scalar(desc, tstiop__asn1_ext_choice_, i, 0);
    asn1_set_int_min_max(desc, 42, 666);
    asn1_reg_extension(desc);
    asn1_reg_string(desc, tstiop__asn1_ext_choice_, ext_s, 1);
    asn1_reg_scalar(desc, tstiop__asn1_ext_choice_, ext_i, 2);
    asn1_set_int_min_max(desc, 666, 1234567);
ASN1_CHOICE_DESC_END(desc);

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

static ASN1_SEQUENCE_DESC_BEGIN(desc, sequence1);
#define SEQ_EXT_ROOT_FIELDS_DESC(pfx)                                        \
    asn1_reg_scalar(desc, pfx, root1, 0);                                    \
    asn1_set_int_min_max(desc, 1, 16);                                       \
                                                                             \
    asn1_reg_scalar(desc, pfx, root2, 0);                                    \
    asn1_set_int_min(desc, -42);                                             \
                                                                             \
    asn1_reg_extension(desc)

#define SEQ_EXT_PARTIAL_FIELDS_DESC(pfx)                                     \
    SEQ_EXT_ROOT_FIELDS_DESC(pfx);                                           \
    asn1_reg_opt_string(desc, pfx, ext1, 0)

#define SEQ_EXT_FIELDS_DESC(pfx)                                             \
    SEQ_EXT_PARTIAL_FIELDS_DESC(pfx);                                        \
    asn1_reg_scalar(desc, pfx, ext2, 0);                                     \
    asn1_set_int_min_max(desc, -100000, 100000);                             \
                                                                             \
    asn1_reg_scalar(desc, pfx, ext3, 0);                                     \
    asn1_set_int_min_max(desc, 0, 256)

    SEQ_EXT_FIELDS_DESC(sequence1);
ASN1_SEQUENCE_DESC_END(desc);

/* Same without extension. */
typedef struct sequence1_root_t {
    SEQ_EXT_ROOT_FIELDS;
} sequence1_root_t;

static ASN1_SEQUENCE_DESC_BEGIN(desc, sequence1_root);
    SEQ_EXT_ROOT_FIELDS_DESC(sequence1_root);
ASN1_SEQUENCE_DESC_END(desc);

/* Same with less fields in extension. */
typedef struct sequence1_partial_t {
    SEQ_EXT_PARTIAL_FIELDS;
} sequence1_partial_t;

static ASN1_SEQUENCE_DESC_BEGIN(desc, sequence1_partial);
    SEQ_EXT_PARTIAL_FIELDS_DESC(sequence1_partial);
ASN1_SEQUENCE_DESC_END(desc);

static int z_test_seq_ext(const sequence1_t *in, lstr_t exp_encoding)
{
    SB_1k(buf);
    pstream_t ps;
    sequence1_t out;
    sequence1_root_t out_root;
    sequence1_partial_t out_partial;

    Z_ASSERT_N(aper_encode(&buf, sequence1, in), "encoding failure");
    /* TODO switch to bits */
    Z_ASSERT_LSTREQUAL(exp_encoding, LSTR_SB_V(&buf),
                       "unexpected encoding value");

    memset(&out, 0xff, sizeof(out));
    ps = ps_initsb(&buf);
    Z_ASSERT_N(t_aper_decode(&ps, sequence1, false, &out),
               "decoding failure (full sequence)");
    Z_ASSERT_OPT_EQ(out.root1, in->root1);
    Z_ASSERT_EQ(out.root2, in->root2);
    Z_ASSERT_LSTREQUAL(out.ext1, in->ext1);
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
    Z_ASSERT_LSTREQUAL(out_partial.ext1, in->ext1);

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

static ASN1_SEQUENCE_DESC_BEGIN(desc, struct1);
    asn1_reg_enum(desc, struct1, enum1, e1, 0);
    asn1_set_enum_info(desc, enum1);
ASN1_SEQUENCE_DESC_END(desc);

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

#define INTS_SEQ_FIELDS_DESC(desc, pfx)                                      \
    asn1_reg_scalar(desc, pfx, i8, 0);                                       \
    asn1_reg_scalar(desc, pfx, u8, 1);                                       \
    asn1_reg_scalar(desc, pfx, i16, 2);                                      \
    asn1_reg_scalar(desc, pfx, u16, 3);                                      \
    asn1_reg_scalar(desc, pfx, i32, 4);                                      \
    asn1_reg_scalar(desc, pfx, u32, 5);                                      \
    asn1_reg_scalar(desc, pfx, i64, 6);                                      \
    asn1_reg_scalar(desc, pfx, u64, 7);                                      \
    asn1_reg_scalar(desc, pfx, i64_bis, 8);                                  \
    asn1_reg_scalar(desc, pfx, u64_bis, 9)

static ASN1_SEQUENCE_DESC_BEGIN(desc, ints_seq);
    INTS_SEQ_FIELDS_DESC(desc, ints_seq);
ASN1_SEQUENCE_DESC_END(desc);

static ASN1_SEQUENCE_DESC_BEGIN(desc, ints_seq_base);
    INTS_SEQ_FIELDS_DESC(desc, ints_seq_base);
ASN1_SEQUENCE_DESC_END(desc);

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

Z_GROUP_EXPORT(asn1_aper) {
    /* {{{ Choice. */

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
    /* {{{ Extended choice. */

    Z_TEST(extended_choice, "extended choice") {
        t_scope;
        struct {
            tstiop__asn1_ext_choice__t in;
            lstr_t aper_bytes;
        } tests[3];

        tstiop__asn1_ext_choice__t out;
        SB_1k(buf);
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
            Z_ASSERT_LSTREQUAL(t->aper_bytes, LSTR_SB_V(&buf));
            ps = ps_initsb(&buf);
            Z_ASSERT_N(t_aper_decode(&ps, tstiop__asn1_ext_choice_, false,
                                     &out));
            Z_ASSERT_IOPEQUAL(tstiop__asn1_ext_choice, &t->in, &out);
        }
    } Z_TEST_END;

    /* }}} */
    /* {{{ Extended sequence. */

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
    /* {{{ Integers overflow. */

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
    /* {{{ Enumerated. */

    Z_TEST(enumerated, "enumerated type check (mostly for auto-wipe)") {
        t_scope;
        SB_1k(buf);
        pstream_t ps;
        struct1_t s1[2];
        lstr_t expected_encoding = LSTR_IMMED("\x80");

        p_clear(&s1[0], 1);
        s1[0].e1 = BAR;

        Z_ASSERT_N(aper_encode(&buf, struct1, &s1[0]), "encoding failure");
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&buf), expected_encoding, "%*pX",
                           SB_FMT_ARG(&buf));
        ps = ps_initsb(&buf);
        Z_ASSERT_N(t_aper_decode(&ps, struct1, false, &s1[1]),
                   "decoding failure");
        Z_ASSERT_EQ(s1[1].e1, s1[0].e1);
    } Z_TEST_END;

    /* }}} */
} Z_GROUP_END
