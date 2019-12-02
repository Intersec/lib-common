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

#include <lib-common/asn1.h>
#include <lib-common/z.h>

enum test_tf {
    ASN1_TEST_TRUE = 666,
    ASN1_TEST_FALSE = 667
};

typedef struct {
    int8_t x;
    uint32_t y;
    opt_i32_t z;
    opt_u32_t t;
    uint64_t u64_1;
    uint64_t u64_2;
    uint64_t u64_3;
    bool     b;
    enum test_tf tf;
} test_0_t;

typedef struct {
    lstr_t             opt;
    lstr_t             string;
    asn1_bit_string_t  bs;
} test_1_t;

typedef struct {
    const test_0_t *opt_t0;
    test_1_t t1;
} test_2_t;

typedef struct {
    asn1_ext_t ph;
    asn1_ext_t ph_opt;
} test_3_t;

typedef struct {
    bool b;
    uint32_t u32;
} test_rdr_rec1_t;

ASN1_DEF_VECTOR(test_rdr_rec1, const test_rdr_rec1_t);
ASN1_DEF_ARRAY(test_rdr_rec1, const test_rdr_rec1_t);

typedef struct {
    asn1_test_rdr_rec1_array_t array;
} simple_array_t;

typedef struct {
    asn1_int32_vector_t vec;
} test_rdr_rec2_t;

typedef struct {
    int32_t i1;
    int32_t i2;
    lstr_t str;
    opt_i32_t oi3;
    asn1_bit_string_t bstr;
    test_rdr_rec2_t vec;
    opt_i32_t oi4;
    test_rdr_rec1_t rec1;
} test_reader_t;

enum choice_type {
    CHOICE_TYPE_1 = 1,
    CHOICE_TYPE_2,
    CHOICE_TYPE_3,
    CHOICE_TYPE_REC1
};

typedef struct {
    enum choice_type type;
    union {
        int32_t choice1;
        int32_t choice2;
        int32_t choice3;
        test_rdr_rec1_t rec1;
    };
} test_choice_t;

typedef struct {
    uint16_t iop_tag;
    union {
        uint8_t  u8;
        int16_t  i16;
        uint16_t u16;
    };
} test_iop_choice_t;

ASN1_DEF_VECTOR(test_choice, const test_choice_t);
ASN1_DEF_ARRAY(test_choice, const test_choice_t);

typedef struct {
    int32_t i;
    const test_choice_t *choice;
} test_u_choice_t;

typedef struct {
    asn1_test_choice_vector_t choice;
} test_vector_t;

typedef struct {
    asn1_test_choice_array_t choice;
} test_array_t;

typedef struct il_test_t {
    int32_t i1;
    int32_t i2;
} il_test_t;

typedef struct il_test_base_t {
    il_test_t t;
} il_test_base_t;

typedef struct il_rec_t {
    asn1_uint32_vector_t v32;
} il_rec_t;

ASN1_DEF_VECTOR(il_rec, const il_rec_t);
ASN1_DEF_ARRAY(il_rec, const il_rec_t);

typedef struct il_rec_vec_t {
    asn1_il_rec_vector_t rec;
} il_rec_vec_t;

typedef struct il_rec_base_t {
    il_rec_vec_t vec;
} il_rec_base_t;

typedef struct il_trailing_t {
    il_test_t t;
    int32_t i;
} il_trailing_t;

ASN1_DESC(test_0);
ASN1_DESC(test_1);
ASN1_DESC(test_2);
ASN1_DESC(test_3);

ASN1_DESC_BEGIN(desc, test_0);
    asn1_reg_scalar(desc, test_0, x, 0xab);
    asn1_reg_scalar(desc, test_0, y, 0xcd);
    asn1_reg_scalar(desc, test_0, z, 0xef);
    asn1_reg_scalar(desc, test_0, t, 0xef);
    asn1_reg_scalar(desc, test_0, u64_1, 0x64);
    asn1_reg_scalar(desc, test_0, u64_2, 0x64);
    asn1_reg_scalar(desc, test_0, u64_3, 0x64);
    asn1_reg_scalar(desc, test_0, b, 0xbb);
    asn1_reg_enum(desc, test_0, test_tf, tf, 0x0f);
ASN1_DESC_END(desc);

ASN1_DESC_BEGIN(desc, test_1);
    asn1_reg_opt_string(desc, test_1, opt, 0x00);
    asn1_reg_string(desc, test_1, string, 0xab);
    asn1_reg_string(desc, test_1, bs, 0xb5);
ASN1_DESC_END(desc);

ASN1_DESC_BEGIN(desc, test_2);
    asn1_reg_opt_sequence(desc, test_2, test_0, opt_t0, 0x32);
    asn1_reg_sequence(desc, test_2, test_1, t1, 0x34);
ASN1_DESC_END(desc);

ASN1_DESC_BEGIN(desc, test_3);
    asn1_reg_ext(desc, test_3, ph, 0x77);
    asn1_reg_opt_ext(desc, test_3, ph_opt, 0x99);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, test_rdr_rec1);
    asn1_reg_scalar(desc, test_rdr_rec1, b, 0xbb);
    asn1_reg_skip(desc, "test_skip", 0x55);
    asn1_reg_scalar(desc, test_rdr_rec1, u32, 0x16);
ASN1_DESC_END(desc);

static ASN1_SEQUENCE_DESC_BEGIN(desc, simple_array);
    asn1_reg_seq_of_sequence(desc, simple_array, test_rdr_rec1, array, 0xaa);
ASN1_SEQUENCE_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, test_rdr_rec2);
    asn1_reg_scalar(desc, test_rdr_rec2, vec, 0x85);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, test_reader);
    asn1_reg_scalar(desc, test_reader, i1, 0x12);
    asn1_reg_scalar(desc, test_reader, i2, 0x34);
    asn1_reg_string(desc, test_reader, str, 0x82);
    asn1_reg_scalar(desc, test_reader, oi3, 0x56);
    asn1_reg_string(desc, test_reader, bstr, 0x83);
    asn1_reg_sequence(desc, test_reader, test_rdr_rec2, vec, 0xa4);
    asn1_reg_scalar(desc, test_reader, oi4, 0x78);
    asn1_reg_skip(desc, "test_skip", ASN1_TAG_INVALID);
    asn1_reg_sequence(desc, test_reader, test_rdr_rec1, rec1, 0xec);
ASN1_DESC_END(desc);

static ASN1_CHOICE_DESC_BEGIN(desc, test_choice, choice_type, type);
    asn1_reg_scalar(desc, test_choice, choice1, 0x23);
    asn1_reg_scalar(desc, test_choice, choice2, 0x34);
    asn1_reg_scalar(desc, test_choice, choice3, 0x45);
    asn1_reg_sequence(desc, test_choice, test_rdr_rec1, rec1, 0xec);
ASN1_CHOICE_DESC_END(desc);

static __ASN1_IOP_CHOICE_DESC_BEGIN(desc, test_iop_choice);
    asn1_reg_scalar(desc, test_iop_choice, u8,  0x80);
    asn1_reg_scalar(desc, test_iop_choice, i16, 0x81);
    asn1_reg_scalar(desc, test_iop_choice, u16, 0x82);
ASN1_CHOICE_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, test_u_choice);
    asn1_reg_scalar(desc, test_u_choice, i, ASN1_TAG_INTEGER);
    asn1_reg_untagged_choice(desc, test_u_choice, test_choice, choice);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, test_vector);
    asn1_reg_seq_of_untagged_choice(desc, test_vector, test_choice, choice);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, test_array);
    asn1_reg_seq_of_untagged_choice(desc, test_array, test_choice, choice);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, il_test);
    asn1_reg_scalar(desc, il_test, i1, 0x12);
    asn1_reg_scalar(desc, il_test, i2, 0x34);
    asn1_reg_skip  (desc, "skip", 0x55);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, il_test_base);
    asn1_reg_sequence(desc, il_test_base, il_test, t, 0x76);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, il_trailing);
    asn1_reg_sequence(desc, il_trailing, il_test, t, 0x76);
    asn1_reg_scalar(desc, il_trailing, i, 0x01);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, il_rec);
    asn1_reg_scalar(desc, il_rec, v32, 0x12);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, il_rec_vec);
    asn1_reg_seq_of_sequence(desc, il_rec_vec, il_rec, rec, 0x34);
ASN1_DESC_END(desc);

static ASN1_DESC_BEGIN(desc, il_rec_base);
    asn1_reg_sequence(desc, il_rec_base, il_rec_vec, vec, 0x66);
ASN1_DESC_END(desc);

uint8_t il_test_input[] = {
    0x76, 0x80,
          0x12, 0x02,
                0x10, 0x00,
          0x34, 0x01,
                0x00,
          0x55, 0x80,
                0x04, 0x02,
                      0x12, 0x34,
                0x78, 0x80,
                      0x80, 0x00,
                0x00, 0x00,
          0x00, 0x00,
    0x00, 0x00
};

uint8_t il_rec_input[] = {
    0x66, 0x80,
          0x34, 0x80,
                0x12, 0x01, 0x01,
                0x12, 0x01, 0x02,
                0x00, 0x00,
          0x34, 0x80,
                0x12, 0x01, 0x03,
                0x12, 0x01, 0x04,
                0x12, 0x01, 0x05,
                0x00, 0x00,
          0x00, 0x00,
};

uint8_t il_rec_uchoice_input[] = {
    0x23, 0x01, 0x01,
    0xec, 0x80,
          0xbb, 0x01, 0x01,
          0x16, 0x01, 0x42,
          0x00, 0x00,
    0x34, 0x01, 0x02,
};

static int serialize_test_0(uint8_t *dst, const test_0_t *t0)
{
    int32_t length;
    qv_t(i32) stack;

    qv_init(&stack);
    length = RETHROW(asn1_pack_size_(t0, asn1_test_0_desc(), &stack));
    asn1_pack_(dst, t0, asn1_test_0_desc(), &stack);
    qv_wipe(&stack);
    return length;
}

static int serialize_test_1(uint8_t *dst, const test_1_t *t1)
{
    int32_t length;
    qv_t(i32) stack;

    qv_init(&stack);
    length = RETHROW(asn1_pack_size_(t1, asn1_test_1_desc(), &stack));
    asn1_pack_(dst, t1, asn1_test_1_desc(), &stack);
    qv_wipe(&stack);
    return length;
}

static int serialize_test_2(uint8_t *dst, const test_2_t *t2)
{
    int32_t length;
    qv_t(i32) stack;

    qv_init(&stack);
    length = RETHROW(asn1_pack_size_(t2, asn1_test_2_desc(), &stack));
    asn1_pack_(dst, t2, asn1_test_2_desc(), &stack);
    qv_wipe(&stack);
    return length;
}

static int serialize_test_3(uint8_t *dst, const test_3_t *t3)
{
    int32_t length;
    qv_t(i32) stack;

    qv_init(&stack);
    length = RETHROW(asn1_pack_size(test_3, t3, &stack));
    asn1_pack(test_3, dst, t3, &stack);
    qv_wipe(&stack);
    return length;
}

static bool simple_array_equal(const simple_array_t *a1, const simple_array_t *a2)
{
    if (a1->array.len != a2->array.len) {
        return false;
    }

    for (int i = 0; i < a1->array.len; i++) {
        if (a1->array.data[i]->b != a2->array.data[i]->b
        ||  a1->array.data[i]->u32 != a2->array.data[i]->u32)
        {
            return false;
        }
    }
    return true;
}


static bool test_vector_equal(const test_vector_t *t1, const test_vector_t *t2)
{
    if (t1->choice.len != t2->choice.len) {
        return false;
    }

    for (int i = 0; i < t1->choice.len; i++) {
        if (t1->choice.data[i].type != t2->choice.data[i].type) {
            e_trace(0, "FAIL (type %d != %d)", t1->choice.data[i].type,
                    t2->choice.data[i].type);

            return false;
        }
        if (t1->choice.data[i].choice1 != t2->choice.data[i].choice1) /* XXX */
        {
            e_trace(0, "FAIL (choice %d != %d)", t1->choice.data[i].choice1,
                    t2->choice.data[i].choice1);

            return false;
        }
    }

    return true;
}

static bool test_array_equal(const test_array_t *t1, const test_array_t *t2)
{
    if (t1->choice.len != t2->choice.len) {
        return false;
    }

    for (int i = 0; i < t1->choice.len; i++) {
        if (t1->choice.data[i]->type != t2->choice.data[i]->type) {
            e_trace(0, "FAIL (type %d != %d)", t1->choice.data[i]->type,
                    t2->choice.data[i]->type);

            return false;
        }
        if (t1->choice.data[i]->choice1 != t2->choice.data[i]->choice1) /* XXX */
        {
            e_trace(0, "FAIL (choice %d != %d)", t1->choice.data[i]->choice1,
                    t2->choice.data[i]->choice1);

            return false;
        }
    }

    return true;
}

Z_GROUP_EXPORT(asn1_ber)
{
    test_0_t const t0 = {
        .x     = -1,
        .y     = 0x87654321,
        .z     = ASN1_OPT_CLEAR(int32),
        .t     = ASN1_OPT_SET(uint32, 0x42),
        .u64_1 = 0x87654321ll,
        .u64_2 = 0x9234567890abcdefll,
        .u64_3 = 0x1234567890abcdefll,
        .b     = true,
        .tf    = ASN1_TEST_TRUE,
    };

    static uint8_t const bs_content[] = { 0xF };
    static test_1_t const t1 = {
        .opt = LSTR_NULL,
        .string = LSTR_IMMED("string"),
        .bs = { bs_content, 4 },
    };

    Z_TEST(dec_len32, "asn1: ber_decode_len32") {
        const byte dec0[] = { 0x80 | 0x3, 0xfa, 0x56, 0x09 };
        const byte dec1[] = { 0x3 };
        const byte dec2[] = { 0x80, 0xb5, 0x45 };
        const byte dec3[] = { 0x85, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6 };
        const byte dec4[] = { 0x84, 0x1, 0x2, 0x3};

        uint32_t len32 = 0;
        pstream_t ps;

#define DEC(buf, len32) \
        ({ ps = ps_init(buf, sizeof(buf)); ber_decode_len32(&ps, len32); })

        Z_ASSERT_EQ(DEC(dec0, &len32), 0);
        Z_ASSERT_EQ(0xfa5609U, len32);

        Z_ASSERT_EQ(DEC(dec1, &len32), 0);
        Z_ASSERT_EQ(3U, len32);

        Z_ASSERT_EQ(DEC(dec2, &len32), 1, "indefinite length");

        Z_ASSERT_EQ(DEC(dec3, &len32), -1, "length too long");
        Z_ASSERT_EQ(DEC(dec4, &len32), -1, "not enough data");
#undef DEC
    } Z_TEST_END;


    Z_TEST(dec_int32, "asn1: ber_decode_int32") {
        const byte dec0[] = { 0x3, 0xfa, 0x56, 0x09 };
        const byte dec1[] = { 0x83, 0xfa, 0x56 };
        const byte dec2[] = { 0xff, 0xfa, 0x56, 0x45, 0xf5 };

        int32_t int32 = 0;
        pstream_t ps;

#define DEC(v, i) \
        ({ ps = ps_init(v, sizeof(v)); ber_decode_int32(&ps, i); })

        Z_ASSERT_N(DEC(dec0, &int32));
        Z_ASSERT_EQ(0x3fa5609, int32);

        Z_ASSERT_N(DEC(dec1, &int32));
        Z_ASSERT_EQ((int32_t)0xff83fa56, int32);

        Z_ASSERT(DEC(dec2, &int32) == -1, "integer too long");
#undef DEC
    } Z_TEST_END;

    Z_TEST(enc0, "asn1: BER encoder/decoder - constructed types") {
        static uint8_t const expected[] = {
            0xab, 0x01, 0xff, 0xcd, 0x05, 0x00, 0x87, 0x65,
            0x43, 0x21, 0xef, 0x01, 0x42, 0x64, 0x05, 0x00,
            0x87, 0x65, 0x43, 0x21, 0x64, 0x09, 0x00, 0x92,
            0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef, 0x64,
            0x08, 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd,
            0xef, 0xbb, 0x01, 0x01, 0x0f, 0x02, 0x02, 0x9a
        };
        uint8_t buf[256];
        size_t len;

        len = serialize_test_0(buf, &t0);
        Z_ASSERT_EQUAL(buf, len, expected, sizeof(expected));
    } Z_TEST_END;

    Z_TEST(enc1, "asn1: BER encoder/decoder - constructed types") {
        static uint8_t const expected[] = {
            0xab, 0x06, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67,
            0xb5, 0x02, 0x04, 0x0f,
        };
        uint8_t buf[256];
        size_t len;

        len = serialize_test_1(buf, &t1);
        Z_ASSERT_EQUAL(buf, len, expected, sizeof(expected));
    } Z_TEST_END;

    Z_TEST(enc2, "asn1: BER encoder/decoder - constructed types") {
        static uint8_t const expected[] = {
            0x32, 0x30, 0xab, 0x01, 0xff, 0xcd, 0x05, 0x00,
            0x87, 0x65, 0x43, 0x21, 0xef, 0x01, 0x42, 0x64,
            0x05, 0x00, 0x87, 0x65, 0x43, 0x21, 0x64, 0x09,
            0x00, 0x92, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd,
            0xef, 0x64, 0x08, 0x12, 0x34, 0x56, 0x78, 0x90,
            0xab, 0xcd, 0xef, 0xbb, 0x01, 0x01, 0x0f, 0x02,
            0x02, 0x9a, 0x34, 0x0c, 0xab, 0x06, 0x73, 0x74,
            0x72, 0x69, 0x6e, 0x67, 0xb5, 0x02, 0x04, 0x0f,
        };
        test_2_t const t2 = { &t0, t1 };
        uint8_t buf[256];
        size_t len;

        len = serialize_test_2(buf, &t2);
        Z_ASSERT_EQUAL(buf, len, expected, sizeof(expected));
    } Z_TEST_END;

    Z_TEST(enc3, "asn1: BER encoder/decoder - constructed types") {
        static uint8_t const expected[] = {
            0x77, 0x0c, 0xab, 0x06, 0x73, 0x74, 0x72, 0x69,
            0x6e, 0x67, 0xb5, 0x02, 0x04, 0x0f,
        };
        test_3_t const t3 = {
            .ph = { .data = &t1, .desc = ASN1_GET_DESC(test_1) },
            .ph_opt = { .data = NULL }
        };
        uint8_t buf[256];
        size_t len;

        len = serialize_test_3(buf, &t3);
        Z_ASSERT_EQUAL(buf, len, expected, sizeof(expected));
    } Z_TEST_END;

    Z_TEST(indef_len_skip_trailing_fields, "asn1: BER decoder - "
           "skip trailing filed in case of indefinite length")
    {
        t_scope;

        /* One trailing field. */
        static uint8_t const in1[] = {
            0x76, 0x80,
                        /* Declared fields. */
                        0x12, 0x01, 0x01,
                        0x34, 0x01, 0x02,
                        /* Trailing field. */
                        0x56, 0x02, 0xab, 0xcd,
                        /* EOC */
                        0x00, 0x00,
            0x01, 0x01, 0x03,
        };
        /* Two trailing fields. */
        static uint8_t const in2[] = {
            0x76, 0x80,
                        /* Declared fields. */
                        0x12, 0x01, 0x01,
                        0x34, 0x01, 0x02,
                        /* Trailing fields. */
                        0x56, 0x02, 0xab, 0xcd,
                        0x78, 0x03, 0xab, 0xcd, 0xef,
                        /* EOC */
                        0x00, 0x00,
            0x01, 0x01, 0x03,
        };

        il_trailing_t t;
        pstream_t ps;

        p_clear(&t, 1);

        ps = ps_init(in1, sizeof(in1));
        Z_ASSERT_N(asn1_unpack(il_trailing, &ps, t_pool(), &t, false));
        Z_ASSERT_EQ(t.t.i1, 1);
        Z_ASSERT_EQ(t.t.i2, 2);
        Z_ASSERT_EQ(t.i, 3);
        ps = ps_init(in2, sizeof(in2));
        Z_ASSERT_N(asn1_unpack(il_trailing, &ps, t_pool(), &t, false));
        Z_ASSERT_EQ(t.t.i1, 1);
        Z_ASSERT_EQ(t.t.i2, 2);
        Z_ASSERT_EQ(t.i, 3);
    } Z_TEST_END;

    Z_TEST(reader, "asn1: BER reader test") {
        t_scope;

        static int32_t const rdr_vec[] = { 0x1234, 0x8555 };
        static uint8_t const rdr_bstring[] = { 0x12, 0x58 };

        test_reader_t exp_rdr_out = {
            .i1 = 1234,
            .i2 = 56,
            .str = LSTR_IMMED("test"),
            .bstr = ASN1_BIT_STRING(rdr_bstring, 13),
            .oi3 = ASN1_OPT_SET(int32, -0xabcd),
            .oi4 = ASN1_OPT_CLEAR(int32),
            .vec = {
                .vec = {
                    .data = rdr_vec,
                    .len = 2,
                }
            },
            .rec1 = {
                .b = true,
                .u32 = 0x87785555
            }
        };
        test_reader_t rdr_out;
        qv_t(i32) stack;
        uint8_t buf[256];
        size_t len;
        pstream_t ps;

        qv_inita(&stack, 1024);

        p_clear(&rdr_out, 1);
        len = asn1_pack_size(test_reader, &exp_rdr_out, &stack);
        asn1_pack(test_reader, buf, &exp_rdr_out, &stack);
        ps = ps_init(buf, len);

        Z_ASSERT_N(asn1_unpack(test_reader, &ps, t_pool(), &rdr_out, false));
        Z_ASSERT_EQ(rdr_out.i1, exp_rdr_out.i1);
        Z_ASSERT_EQ(rdr_out.i2, exp_rdr_out.i2);
        Z_ASSERT_EQUAL(rdr_out.str.s, rdr_out.str.len,
                       exp_rdr_out.str.s, exp_rdr_out.str.len);
        Z_ASSERT_EQ(rdr_out.bstr.bit_len, exp_rdr_out.bstr.bit_len);
        Z_ASSERT_ZERO(memcmp(rdr_out.bstr.data, exp_rdr_out.bstr.data,
                             asn1_bit_string_size(&rdr_out.bstr) - 1));
        Z_ASSERT_EQ(rdr_out.oi3.has_field, exp_rdr_out.oi3.has_field);
        if (rdr_out.oi3.has_field)
            Z_ASSERT_EQ(rdr_out.oi3.v, exp_rdr_out.oi3.v);
        Z_ASSERT_EQ(rdr_out.oi4.has_field, exp_rdr_out.oi4.has_field);
        if (rdr_out.oi4.has_field)
            Z_ASSERT_EQ(rdr_out.oi4.v, exp_rdr_out.oi4.v);
        Z_ASSERT_EQ(rdr_out.rec1.b, exp_rdr_out.rec1.b);
        Z_ASSERT_EQ(rdr_out.rec1.u32, exp_rdr_out.rec1.u32);
        Z_ASSERT_EQUAL(rdr_out.vec.vec.data, rdr_out.vec.vec.len,
                       exp_rdr_out.vec.vec.data, exp_rdr_out.vec.vec.len);
        qv_wipe(&stack);
    } Z_TEST_END;

    Z_TEST(array, "asn1: BER array (un)packing") {
        t_scope;

        static test_rdr_rec1_t const rec1_vector[] = {
            { .b = true,  .u32 = 0x123 },
            { .b = false, .u32 = 0x44444 },
            { .b = false, .u32 = 0x0 },
            { .b = true,  .u32 = 0x96 }
        };

        static test_rdr_rec1_t const *rec1_array[] = {
            &rec1_vector[0],
            &rec1_vector[1],
            &rec1_vector[2],
            &rec1_vector[3],
        };

        static simple_array_t const simple_array = {
            .array = {
                .data = rec1_array,
                .len = 4
            }
        };

        static uint8_t const exp_simple_array[] = {
            0xaa, 0x07, 0xbb, 0x01, 0x01, 0x16, 0x02, 0x01,
            0x23, 0xaa, 0x08, 0xbb, 0x01, 0x00, 0x16, 0x03,
            0x04, 0x44, 0x44, 0xaa, 0x06, 0xbb, 0x01, 0x00,
            0x16, 0x01, 0x00, 0xaa, 0x07, 0xbb, 0x01, 0x01,
            0x16, 0x02, 0x00, 0x96
        };

        simple_array_t simple_array_out;
        qv_t(i32) stack;
        uint8_t buf[256];
        size_t len;
        pstream_t ps;

        qv_inita(&stack, 1024);

        len = asn1_pack_size(simple_array, &simple_array, &stack);
        asn1_pack(simple_array, buf, &simple_array, &stack);
        Z_ASSERT_EQUAL(buf, len, exp_simple_array, sizeof(exp_simple_array));

        ps = ps_init(buf, len);
        Z_ASSERT_N(asn1_unpack(simple_array, &ps, t_pool(),
                               &simple_array_out, false));
        Z_ASSERT(simple_array_equal(&simple_array_out, &simple_array));
        qv_wipe(&stack);
    } Z_TEST_END;

    Z_TEST(choice, "asn1: BER choice (un)packing") {
        t_scope;

        static uint8_t const exp_choice_no_skip[] = {
            0xec, 0x07, 0xbb, 0x01, 0x01, 0x16, 0x02, 0X34, 0x56
        };

        static test_choice_t const in_u_choice = {
            .type = 2,
            {
                .choice2 = 0x25,
            },
        };

        static test_u_choice_t const u_choice = {
            .i = 0x34,
            .choice = &in_u_choice
        };

        static uint8_t const exp_u_choice[] = {
            0x02, 0x01, 0x34, 0x34, 0x01, 0x25
        };

        static uint8_t const choice_input[] = {
            0xec, 0x0d, 0xbb, 0x01, 0x01, 0x55,
            0x04, 0x00, 0x01, 0x02, 0x03, 0x16, 0x2, 0x34, 0x56
        };

        pstream_t choice_ps = ps_init(choice_input, sizeof(choice_input));
        test_choice_t exp_choice;
        test_choice_t choice;
        test_u_choice_t u_choice_out = { .choice = NULL };
        qv_t(i32) stack;
        uint8_t buf[256];
        size_t len;
        pstream_t ps;

        qv_inita(&stack, 1024);

        p_clear(&exp_choice, 1);
        p_clear(&choice, 1);
        exp_choice.type = CHOICE_TYPE_REC1;
        exp_choice.rec1.b = true;
        exp_choice.rec1.u32 = 0x3456;

        len = asn1_pack_size(test_choice, &exp_choice, &stack);
        asn1_pack(test_choice, buf, &exp_choice, &stack);
        Z_ASSERT_EQUAL(buf, len, exp_choice_no_skip, sizeof(exp_choice_no_skip));

        Z_ASSERT_N(asn1_unpack(test_choice, &choice_ps, NULL, &choice, false));
        Z_ASSERT_ZERO(memcmp(&exp_choice, &choice, sizeof(test_choice_t)));

        len = asn1_pack_size(test_u_choice, &u_choice, &stack);
        asn1_pack(test_u_choice, buf, &u_choice, &stack);
        Z_ASSERT_EQUAL(buf, len, exp_u_choice, sizeof(exp_u_choice));

        ps = ps_init(buf, len);
        Z_ASSERT_N(asn1_unpack(test_u_choice, &ps, t_pool(),
                               &u_choice_out, false));
        Z_ASSERT_EQ(u_choice.i, u_choice_out.i);
        Z_ASSERT_EQ(u_choice.choice->type, u_choice_out.choice->type);
        Z_ASSERT_EQ(u_choice.choice->choice2, u_choice_out.choice->choice2);
        qv_wipe(&stack);
    } Z_TEST_END;

    Z_TEST(iop_choice, "asn1: IOP union/ASN.1 choice interoperability") {
        lstr_t ber = LSTR_IMMED("\x81\x01\x45");
        test_iop_choice_t choice;
        int    blen;
        qv_t(i32) stack;
        uint8_t buf[256];
        pstream_t ps;

        qv_inita(&stack, 1024);

        ps = ps_initlstr(&ber);
        Z_ASSERT_N(asn1_unpack(test_iop_choice, &ps, NULL, &choice, false));
        Z_ASSERT_EQ(choice.iop_tag, 2);
        Z_ASSERT_EQ(choice.i16,     0x45);

        blen = asn1_pack_size(test_iop_choice, &choice, &stack);
        Z_ASSERT_EQ(blen, ber.len);
        asn1_pack(test_iop_choice, buf, &choice, &stack);
        Z_ASSERT_LSTREQUAL(ber, LSTR_INIT_V((const char *)buf, blen));
        qv_wipe(&stack);
    } Z_TEST_END;

    Z_TEST(vector_array, "asn1: BER vectors/array") {
        t_scope;

        static const test_choice_t choice_vec[] = {
            ASN1_CHOICE(type, CHOICE_TYPE_2, .choice2 = 0x123),
            ASN1_CHOICE(type, CHOICE_TYPE_1, .choice1 = 0x456),
            ASN1_CHOICE(type, CHOICE_TYPE_3, .choice3 = 0x789),
        };

        static test_choice_t const * choice_arr[] = {
            &choice_vec[0],
            &choice_vec[1],
            &choice_vec[2],
        };

        static const uint8_t exp_test_vector[] = {
            0x34, 0x02, 0x01, 0x23, 0x23, 0x02, 0x04, 0x56,
            0x45, 0x02, 0x07, 0x89
        };

        static const test_vector_t test_vector_in = {
            .choice = {
                .data = choice_vec,
                .len = 3
            }
        };

        static const test_array_t test_array_in = {
            .choice = {
                .data = choice_arr,
                .len = 3
            }
        };

        test_vector_t test_vector;
        test_array_t test_array;
        il_test_base_t il;
        il_rec_base_t il_rec;
        qv_t(i32) stack;
        uint8_t buf[256];
        size_t len;
        pstream_t ps;

        qv_inita(&stack, 1024);

        /* Sequence of untagged choice test (with a vector) */
        len = asn1_pack_size(test_vector, &test_vector_in, &stack);
        asn1_pack(test_vector, buf, &test_vector_in, &stack);
        Z_ASSERT_EQUAL(buf, len, exp_test_vector, sizeof(exp_test_vector));

        ps = ps_init(buf, len);
        Z_ASSERT_N(asn1_unpack(test_vector, &ps, t_pool(), &test_vector, false));
        Z_ASSERT_EQ(test_vector.choice.len, 3);
        Z_ASSERT(test_vector_equal(&test_vector, &test_vector_in));

        /* Sequence of untagged choice test (with an array) */
        len = asn1_pack_size(test_array, &test_array_in, &stack);
        asn1_pack(test_array, buf, &test_array_in, &stack);
        Z_ASSERT_EQUAL(buf, len, exp_test_vector, sizeof(exp_test_vector));

        ps = ps_init(buf, len);
        Z_ASSERT_N(asn1_unpack(test_array, &ps, t_pool(), &test_array, false));
        Z_ASSERT_EQ(test_array.choice.len, 3, "");
        Z_ASSERT(test_array_equal(&test_array, &test_array_in));

        ps = ps_init(il_test_input, sizeof(il_test_input));
        Z_ASSERT_N(asn1_unpack(il_test_base, &ps, t_pool(), &il, false));
        Z_ASSERT_EQ(il.t.i1, 0x1000);
        Z_ASSERT_EQ(il.t.i2, 0x0);

        ps = ps_init(il_rec_input, sizeof(il_rec_input));
        Z_ASSERT_N(asn1_unpack(il_rec_base, &ps, t_pool(), &il_rec, false));
        Z_ASSERT_EQ(il_rec.vec.rec.len, 2);
        Z_ASSERT_EQ(il_rec.vec.rec.data[0].v32.len, 2);
        Z_ASSERT_EQ(il_rec.vec.rec.data[1].v32.len, 3);
        Z_ASSERT_EQ(il_rec.vec.rec.data[0].v32.data[0], 1u);
        Z_ASSERT_EQ(il_rec.vec.rec.data[0].v32.data[1], 2u);
        Z_ASSERT_EQ(il_rec.vec.rec.data[1].v32.data[0], 3u);
        Z_ASSERT_EQ(il_rec.vec.rec.data[1].v32.data[1], 4u);
        Z_ASSERT_EQ(il_rec.vec.rec.data[1].v32.data[2], 5u);

        p_clear(&test_vector, 1);
        ps = ps_init(il_rec_uchoice_input, sizeof(il_rec_uchoice_input));
        Z_ASSERT_N(asn1_unpack(test_vector, &ps, t_pool(), &test_vector,
                               false));
        Z_ASSERT_EQ(test_vector.choice.len, 3);
        Z_ASSERT_EQ((int)test_vector.choice.data[0].type, CHOICE_TYPE_1);
        Z_ASSERT_EQ((int)test_vector.choice.data[0].choice1, 1);
        Z_ASSERT_EQ((int)test_vector.choice.data[1].type, CHOICE_TYPE_REC1);
        Z_ASSERT_EQ(test_vector.choice.data[1].rec1.b, true);
        Z_ASSERT_EQ(test_vector.choice.data[1].rec1.u32, (uint32_t)0x42);
        Z_ASSERT_EQ((int)test_vector.choice.data[2].type, CHOICE_TYPE_2);
        Z_ASSERT_EQ((int)test_vector.choice.data[2].choice2, 2);
        qv_wipe(&stack);
    } Z_TEST_END;

    Z_TEST(asn1_skip_field, "asn1: asn1_skip_field()") {
        t_scope;

        static const uint8_t fields[] = {
            0x01, 0x02, 0xab, 0xcd,
            0xa1, 0x80, 0x01, 0x01, 0x02,
                        0x01, 0x02, 0xfe, 0xdc,
                        0x00, 0x00,
            0x04, 0x81, 0xa2, 0x01,
        };
        byte *long_field;
        int vlen;
        qv_t(i32) stack;
        pstream_t ps;

        qv_inita(&stack, 1024);

        ps = ps_init(fields, sizeof(fields));

        /* Normal field */
        Z_ASSERT_N(asn1_skip_field(&ps));
        Z_ASSERT(ps.b == &fields[4]);

        /* Indefinite length */
        Z_ASSERT_N(asn1_skip_field(&ps));
        Z_ASSERT(ps.b == &fields[15]);

        /* Value length > 127 - Error: stream end */
        Z_ASSERT_NEG(asn1_skip_field(&ps));

        /* Value length > 127 */
        vlen = 0xa2;
        long_field = t_new(byte, 3 + vlen);
        long_field[0] = 0x04;
        long_field[1] = 0x81;
        long_field[2] = vlen;
        ps = ps_init(long_field, 3 + vlen);
        Z_ASSERT_N(asn1_skip_field(&ps));
        Z_ASSERT(ps_done(&ps));
        qv_wipe(&stack);
    } Z_TEST_END;
} Z_GROUP_END

typedef struct open_type_t {
    lstr_t ot1;
    lstr_t ot2;
    lstr_t ot3;
} open_type_t;

static ASN1_SEQUENCE_DESC_BEGIN(desc, open_type);
    asn1_reg_open_type(desc, open_type, ot1);
    asn1_reg_opt_open_type(desc, open_type, ot2);
    asn1_reg_opt_open_type(desc, open_type, ot3);
ASN1_SEQUENCE_DESC_END(desc);

Z_GROUP_EXPORT(asn1_open_type)
{
    Z_TEST(open_type, "asn1: open type") {
        t_scope;
        uint8_t buf[256];
        int len;
        pstream_t ps;
        qv_t(i32)  stack;
        open_type_t ot;

        uint8_t want_ot[] = {
            0xa1, 0x03, 0x01, 0x02, 0x03, 0xa2, 0x05, 0x31, 0x32, 0x33, 0x34, 0x00
        };

        ps = ps_init(want_ot, countof(want_ot));
        qv_inita(&stack, 1024);

        Z_ASSERT_N(asn1_unpack(open_type, &ps, t_pool(), &ot, false));
        len = asn1_pack_size(open_type, &ot, &stack);
        asn1_pack(open_type, buf, &ot, &stack);
        Z_ASSERT_LSTREQUAL(LSTR_INIT_V((char *)buf, len),
                           LSTR_INIT_V((char *)want_ot, sizeof(want_ot)));
    } Z_TEST_END;
} Z_GROUP_END

Z_GROUP_EXPORT(asn1_bit_string) {
    Z_TEST(make, "asn1: bit_string") {
        t_scope;
        asn1_bit_string_t bs;

        bs = t_asn1_bstring_from_bf64(0xb, 0);
        Z_ASSERT_EQ(bs.bit_len, 4);
        Z_ASSERT_EQ(*bs.data, 0xd0);

        bs = t_asn1_bstring_from_bf64(0xd0, 0);
        Z_ASSERT_EQ(bs.bit_len, 8);
        Z_ASSERT_EQ(*bs.data, 0xb);

        bs = t_asn1_bstring_from_bf64(0x0b01, 0);
        Z_ASSERT_EQ(bs.bit_len, 12);
        Z_ASSERT_EQ(bs.data[0], 0x80);
        Z_ASSERT_EQ(bs.data[1], 0xd0);

        /* TCAP version */
        bs = t_asn1_bstring_from_bf64(0x1, 0);
        Z_ASSERT_EQ(bs.bit_len, 1);
        Z_ASSERT_EQ(*bs.data, 0x80);
    } Z_TEST_END;
} Z_GROUP_END
