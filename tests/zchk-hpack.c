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

#include <lib-common/net/hpack-priv.h>
#include <lib-common/z.h>

/* {{{ Huffman encoding & decoding */

static int z_hpack_encode_huffman(lstr_t str, lstr_t expected)
{
    char buff[8 * 1024];
    int len;
    int hufflen;

    len = hpack_get_huffman_len_estimate(str);
    Z_ASSERT_GE(len, expected.len);
    /* won't test encoding of very long strings */
    assert(len <= (int)sizeof(buff));
    hufflen = hpack_get_huffman_len(str);
    Z_ASSERT_EQ(hufflen, expected.len);
    hufflen = hpack_encode_huffman(str, buff, len);
    Z_ASSERT_N(hufflen, "unexpected failure in huffman encoding");
    Z_ASSERT_DATAEQUAL(LSTR_DATA_V(buff, hufflen), expected);
    Z_HELPER_END;
}

static int z_hpack_decode_huffman(lstr_t str, lstr_t expected)
{
    char buff[8 * 1024];
    int len;

    /* won't test decoding of very long strings */
    assert(str.len <= (int)sizeof(buff));
    len = hpack_decode_huffman(str, buff);
    Z_ASSERT_N(len);
    Z_ASSERT_DATAEQUAL(LSTR_DATA_V(buff, len), expected);
    Z_HELPER_END;
}

static lstr_t t_z_hex_decode(lstr_t s)
{
    pstream_t ps = ps_initlstr(&s);
    t_SB(sb, 32 + s.len / 2);
    int ch;

    while (!ps_done(&ps)) {
        ps_ltrim(&ps);
        ch = ps_hexdecode(&ps);
        if (ch < 0) {
            return LSTR_NULL_V;
        }
        sb_addc(&sb, ch);
    }
    return LSTR_SB_V(&sb);
}

static int z_huffman_test(lstr_t str, lstr_t coded_str_hex)
{
    t_scope;
    lstr_t coded_str;

    coded_str = t_z_hex_decode(coded_str_hex);
    assert(coded_str.data);
    Z_HELPER_RUN(z_hpack_encode_huffman(str, coded_str));
    Z_HELPER_RUN(z_hpack_decode_huffman(coded_str, str));
    Z_HELPER_END;
}

Z_GROUP_EXPORT(hpack_huffman) {
#define ZT_TEST(str, coded_str)                                              \
    Z_HELPER_RUN(z_huffman_test(LSTR_IMMED_V(str), LSTR_IMMED_V(coded_str)))

    Z_TEST(hpack_huffman_simple, "simple cases") {
        ZT_TEST("", "");
        ZT_TEST("0", "07");
        ZT_TEST("1", "0f");
        ZT_TEST("&", "f8");
        ZT_TEST("\xae", "ff ff d7");
    } Z_TEST_END;
    Z_TEST(hpack_huffman_rfc, "huffman encoding from rfc7541 examples") {
        ZT_TEST("www.example.com",
                "f1 e3 c2 e5 f2 3a 6b a0 ab 90 f4 ff");
        ZT_TEST("no-cache", "a8 eb 10 64 9c bf");
        ZT_TEST("custom-key", "25 a8 49 e9 5b a9 7d 7f");
        ZT_TEST("custom-value", "25 a8 49 e9 5b b8 e8 b4 bf");
        ZT_TEST("private", "ae c3 77 1a 4b");
        ZT_TEST("Mon, 21 Oct 2013 20:13:21 GMT",
                "d0 7a be 94 10 54 d4 44 a8 20 05 95 04 0b 81 66 e0 82 a6 2d"
                "1b ff");
        ZT_TEST("https://www.example.com",
                "9d 29 ad 17 18 63 c7 8f 0b 97 c8 e9 ae 82 ae 43 d3");
        ZT_TEST("Mon, 21 Oct 2013 20:13:22 GMT",
                "d0 7a be 94 10 54 d4 44 a8 20 05 95 04 0b 81 66 e0 84 a6 2d"
                "1b ff");
        ZT_TEST("foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
                "94 e7 82 1d d7 f2 e6 c7 b3 35 df df cd 5b 39 60 d5 af 27 08"
                "7f 36 72 c1 ab 27 0f b5 29 1f 95 87 31 60 65 c0 03 ed 4e e5"
                "b1 06 3d 50 07");
    } Z_TEST_END;

#undef ZT_TEST
} Z_GROUP_END;

/* }}} */
/* {{{ Integer coding */

static int
z_hpack_encode_int(uint32_t in, uint8_t prefix_bits, lstr_t expected)
{
    byte out[8];
    int len;

    len = hpack_encode_int(in, prefix_bits, out);
    Z_ASSERT_N(len);
    Z_ASSERT_DATAEQUAL(LSTR_INIT_V((char *)out, len), expected);
    Z_HELPER_END;
}

static int
z_hpack_decode_int(lstr_t in, uint8_t prefix_bits, uint32_t expected)
{
    int rc;
    uint32_t val;
    pstream_t ps_in = ps_initlstr(&in);

    rc = hpack_decode_int(&ps_in, prefix_bits, &val);
    Z_ASSERT_N(rc);
    Z_ASSERT(ps_done(&ps_in));
    Z_ASSERT_EQ(val, expected);
    Z_HELPER_END;
}

static int
z_hpack_int_test(uint32_t val, uint8_t prefix_bits, lstr_t coded_int_hex)
{
    t_scope;
    lstr_t coded_int;

    coded_int = t_z_hex_decode(coded_int_hex);
    assert(coded_int.data);
    Z_HELPER_RUN(z_hpack_encode_int(val, prefix_bits, coded_int));
    Z_HELPER_RUN(z_hpack_decode_int(coded_int, prefix_bits, val));
    Z_HELPER_END;
}

Z_GROUP_EXPORT(hpack_enc_int) {
#define ZT_TEST(val, prefix_bits, coded_int)                                 \
    Z_HELPER_RUN(z_hpack_int_test(val, prefix_bits, LSTR_IMMED_V(coded_int)))

    Z_TEST(hpack_enc_int_corner, "integer encoding of corner cases") {
        ZT_TEST(0, 1, "00");
        ZT_TEST(0, 7, "00");
        ZT_TEST(0, 8, "00");
        ZT_TEST(1, 1, "01  00");
        ZT_TEST(1, 7, "01");
        ZT_TEST(1, 8, "01");
        ZT_TEST(127, 1, "01 7E");
        ZT_TEST(127, 7, "7F 00");
        ZT_TEST(127, 8, "7F");
        ZT_TEST(128, 1, "01 7F");
        ZT_TEST(128, 7, "7F 01");
        ZT_TEST(128, 8, "80");
        ZT_TEST(255, 1, "01 FE 01");
        ZT_TEST(255, 7, "7F 80 01");
        ZT_TEST(255, 8, "FF  00");
        ZT_TEST(0x7FFFFFFFu, 1, "01 FE FF FF FF 07");
        ZT_TEST(0x7FFFFFFFu, 7, "7F 80 FF FF FF 07");
        ZT_TEST(0x7FFFFFFFu, 8, "FF 80 FE FF FF 07");
        ZT_TEST(0xFFFFFFFFu, 1, "01 FE FF FF FF 0F");
        ZT_TEST(0xFFFFFFFFu, 7, "7F 80 FF FF FF 0F");
        ZT_TEST(0xFFFFFFFFu, 8, "FF 80 FE FF FF 0F");
    } Z_TEST_END;

    Z_TEST(hpack_enc_int_simple, "integer encoding of simple cases") {
        ZT_TEST(0, 8, "00");
        ZT_TEST(4, 4, "04");
        ZT_TEST(30, 5, "1E");
        ZT_TEST(31, 5, "1F 00");
    } Z_TEST_END;

    Z_TEST(hpack_enc_int_rfc, "integer decoding of rfc7541 examples") {
        ZT_TEST(10, 5, "0A");
        ZT_TEST(1337, 5, "1F 9A 0A");
        ZT_TEST(42, 8, "2A");
    } Z_TEST_END;

#undef ZT_TEST
} Z_GROUP_END;

/* }}} */
