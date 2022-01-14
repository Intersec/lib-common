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
/* {{{ Header tables */

static int z_hpack_stbl_search_test(lstr_t key, lstr_t val, int exp_idx) {
    int idx = hpack_stbl_find_hdr(key, val);

    Z_ASSERT_EQ(idx, exp_idx);
    Z_HELPER_END;
}

static int z_hpack_enc_dtbl_size_test(hpack_enc_dtbl_t *dtbl, int len,
                                      uint32_t sz, uint32_t sz_lim)
{
    Z_ASSERT_EQ(dtbl->entries.len, len);
    Z_ASSERT_EQ(dtbl->tbl_size, sz);
    Z_ASSERT_EQ(dtbl->tbl_size_limit, sz_lim);
    Z_HELPER_END;
}

static int
z_hpack_enc_dtbl_search_test(hpack_enc_dtbl_t *dtbl, uint16_t key_id,
                             uint16_t val_id, int exp_idx)
{
    int idx = hpack_enc_dtbl_find_hdr(dtbl, key_id, val_id);

    Z_ASSERT_EQ(idx, exp_idx);
    Z_HELPER_END;
}

static int z_hpack_dec_dtbl_size_test(hpack_dec_dtbl_t *dtbl, int len,
                                      uint32_t sz, uint32_t sz_lim)
{
    Z_ASSERT_EQ(dtbl->entries.len, len);
    Z_ASSERT_EQ(dtbl->tbl_size, sz);
    Z_ASSERT_EQ(dtbl->tbl_size_limit, sz_lim);
    Z_HELPER_END;
}

Z_GROUP_EXPORT(hpack_tables) {
#define HPACK_STBL_SEARCH(exp_idx, k, v)                                               \
    Z_HELPER_RUN(                                                            \
        z_hpack_stbl_search_test(LSTR_IMMED_V(k), LSTR_IMMED_V(v), exp_idx))

    Z_TEST(hpack_stbl_search_exact, "search yields exact match in the STBL") {
        HPACK_STBL_SEARCH(0, "", "");
        HPACK_STBL_SEARCH(1, ":authority", "");
        HPACK_STBL_SEARCH(2, ":method", "GET");
        HPACK_STBL_SEARCH(3, ":method", "POST");
        HPACK_STBL_SEARCH(4, ":path", "/");
        HPACK_STBL_SEARCH(5, ":path", "/index.html");
        HPACK_STBL_SEARCH(6, ":scheme", "http");
        HPACK_STBL_SEARCH(7, ":scheme", "https");
        HPACK_STBL_SEARCH(8, ":status", "200");
        HPACK_STBL_SEARCH(9, ":status", "204");
        HPACK_STBL_SEARCH(10, ":status", "206");
        HPACK_STBL_SEARCH(11, ":status", "304");
        HPACK_STBL_SEARCH(12, ":status", "400");
        HPACK_STBL_SEARCH(13, ":status", "404");
        HPACK_STBL_SEARCH(14, ":status", "500");
        HPACK_STBL_SEARCH(15, "accept-charset", "");
        HPACK_STBL_SEARCH(16, "accept-encoding", "gzip, deflate");
        HPACK_STBL_SEARCH(17, "accept-language", "");
        HPACK_STBL_SEARCH(18, "accept-ranges", "");
        HPACK_STBL_SEARCH(19, "accept", "");
        HPACK_STBL_SEARCH(20, "access-control-allow-origin", "");
        HPACK_STBL_SEARCH(21, "age", "");
        HPACK_STBL_SEARCH(22, "allow", "");
        HPACK_STBL_SEARCH(23, "authorization", "");
        HPACK_STBL_SEARCH(24, "cache-control", "");
        HPACK_STBL_SEARCH(25, "content-disposition", "");
        HPACK_STBL_SEARCH(26, "content-encoding", "");
        HPACK_STBL_SEARCH(27, "content-language", "");
        HPACK_STBL_SEARCH(28, "content-length", "");
        HPACK_STBL_SEARCH(29, "content-location", "");
        HPACK_STBL_SEARCH(30, "content-range", "");
        HPACK_STBL_SEARCH(31, "content-type", "");
        HPACK_STBL_SEARCH(32, "cookie", "");
        HPACK_STBL_SEARCH(33, "date", "");
        HPACK_STBL_SEARCH(34, "etag", "");
        HPACK_STBL_SEARCH(35, "expect", "");
        HPACK_STBL_SEARCH(36, "expires", "");
        HPACK_STBL_SEARCH(37, "from", "");
        HPACK_STBL_SEARCH(38, "host", "");
        HPACK_STBL_SEARCH(39, "if-match", "");
        HPACK_STBL_SEARCH(40, "if-modified-since", "");
        HPACK_STBL_SEARCH(41, "if-none-match", "");
        HPACK_STBL_SEARCH(42, "if-range", "");
        HPACK_STBL_SEARCH(43, "if-unmodified-since", "");
        HPACK_STBL_SEARCH(44, "last-modified", "");
        HPACK_STBL_SEARCH(45, "link", "");
        HPACK_STBL_SEARCH(46, "location", "");
        HPACK_STBL_SEARCH(47, "max-forwards", "");
        HPACK_STBL_SEARCH(48, "proxy-authenticate", "");
        HPACK_STBL_SEARCH(49, "proxy-authorization", "");
        HPACK_STBL_SEARCH(50, "range", "");
        HPACK_STBL_SEARCH(51, "referer", "");
        HPACK_STBL_SEARCH(52, "refresh", "");
        HPACK_STBL_SEARCH(53, "retry-after", "");
        HPACK_STBL_SEARCH(54, "server", "");
        HPACK_STBL_SEARCH(55, "set-cookie", "");
        HPACK_STBL_SEARCH(56, "strict-transport-security", "");
        HPACK_STBL_SEARCH(57, "transfer-encoding", "");
        HPACK_STBL_SEARCH(58, "user-agent", "");
        HPACK_STBL_SEARCH(59, "vary", "");
        HPACK_STBL_SEARCH(60, "via", "");
        HPACK_STBL_SEARCH(61, "www-authenticate", "");
    } Z_TEST_END;

    Z_TEST(hpack_stbl_search_empty,
           "search yields partial match in the STBL for static hdrs whose "
           "values replaced by the emtpy string in the STBL") {

        HPACK_STBL_SEARCH(-2, ":method", "");
        HPACK_STBL_SEARCH(-4, ":path", "");
        HPACK_STBL_SEARCH(-6, ":scheme", "");
        HPACK_STBL_SEARCH(-8, ":status", "");
        HPACK_STBL_SEARCH(-16, "accept-encoding", "");
    } Z_TEST_END;

    Z_TEST(hpack_stbl_search_part,
           "search yields partial matches in the STBL") {

        HPACK_STBL_SEARCH(-1, ":authority", "dum-val");
        HPACK_STBL_SEARCH(-2, ":method", "dum-val");
        HPACK_STBL_SEARCH(-4, ":path", "dum-val");
        HPACK_STBL_SEARCH(-6, ":scheme", "dum-val");
        HPACK_STBL_SEARCH(-8, ":status", "dum-val");
        HPACK_STBL_SEARCH(-15, "accept-charset", "dum-val");
        HPACK_STBL_SEARCH(-16, "accept-encoding", "dum-val");
        HPACK_STBL_SEARCH(-17, "accept-language", "dum-val");
        HPACK_STBL_SEARCH(-18, "accept-ranges", "dum-val");
        HPACK_STBL_SEARCH(-19, "accept", "dum-val");
        HPACK_STBL_SEARCH(-20, "access-control-allow-origin", "dum-val");
        HPACK_STBL_SEARCH(-21, "age", "dum-val");
        HPACK_STBL_SEARCH(-22, "allow", "dum-val");
        HPACK_STBL_SEARCH(-23, "authorization", "dum-val");
        HPACK_STBL_SEARCH(-24, "cache-control", "dum-val");
        HPACK_STBL_SEARCH(-25, "content-disposition", "dum-val");
        HPACK_STBL_SEARCH(-26, "content-encoding", "dum-val");
        HPACK_STBL_SEARCH(-27, "content-language", "dum-val");
        HPACK_STBL_SEARCH(-28, "content-length", "dum-val");
        HPACK_STBL_SEARCH(-29, "content-location", "dum-val");
        HPACK_STBL_SEARCH(-30, "content-range", "dum-val");
        HPACK_STBL_SEARCH(-31, "content-type", "dum-val");
        HPACK_STBL_SEARCH(-32, "cookie", "dum-val");
        HPACK_STBL_SEARCH(-33, "date", "dum-val");
        HPACK_STBL_SEARCH(-34, "etag", "dum-val");
        HPACK_STBL_SEARCH(-35, "expect", "dum-val");
        HPACK_STBL_SEARCH(-36, "expires", "dum-val");
        HPACK_STBL_SEARCH(-37, "from", "dum-val");
        HPACK_STBL_SEARCH(-38, "host", "dum-val");
        HPACK_STBL_SEARCH(-39, "if-match", "dum-val");
        HPACK_STBL_SEARCH(-40, "if-modified-since", "dum-val");
        HPACK_STBL_SEARCH(-41, "if-none-match", "dum-val");
        HPACK_STBL_SEARCH(-42, "if-range", "dum-val");
        HPACK_STBL_SEARCH(-43, "if-unmodified-since", "dum-val");
        HPACK_STBL_SEARCH(-44, "last-modified", "dum-val");
        HPACK_STBL_SEARCH(-45, "link", "dum-val");
        HPACK_STBL_SEARCH(-46, "location", "dum-val");
        HPACK_STBL_SEARCH(-47, "max-forwards", "dum-val");
        HPACK_STBL_SEARCH(-48, "proxy-authenticate", "dum-val");
        HPACK_STBL_SEARCH(-49, "proxy-authorization", "dum-val");
        HPACK_STBL_SEARCH(-50, "range", "dum-val");
        HPACK_STBL_SEARCH(-51, "referer", "dum-val");
        HPACK_STBL_SEARCH(-52, "refresh", "dum-val");
        HPACK_STBL_SEARCH(-53, "retry-after", "dum-val");
        HPACK_STBL_SEARCH(-54, "server", "dum-val");
        HPACK_STBL_SEARCH(-55, "set-cookie", "dum-val");
        HPACK_STBL_SEARCH(-56, "strict-transport-security", "dum-val");
        HPACK_STBL_SEARCH(-57, "transfer-encoding", "dum-val");
        HPACK_STBL_SEARCH(-58, "user-agent", "dum-val");
        HPACK_STBL_SEARCH(-59, "vary", "dum-val");
        HPACK_STBL_SEARCH(-60, "via", "dum-val");
        HPACK_STBL_SEARCH(-61, "www-authenticate", "dum-val");
    } Z_TEST_END;

#undef HPACK_STBL_SEARCH
#define HPACK_STBL_SEARCH(exp_idx, k)                                                  \
    Z_HELPER_RUN(                                                            \
        z_hpack_stbl_search_test(LSTR_IMMED_V(k), LSTR_NULL_V, exp_idx))

    Z_TEST(hpack_stbl_search_key, "search for key matches in the STBL") {

        HPACK_STBL_SEARCH(1, ":authority");
        HPACK_STBL_SEARCH(2, ":method");
        HPACK_STBL_SEARCH(4, ":path");
        HPACK_STBL_SEARCH(6, ":scheme");
        HPACK_STBL_SEARCH(8, ":status");
        HPACK_STBL_SEARCH(15, "accept-charset");
        HPACK_STBL_SEARCH(16, "accept-encoding");
        HPACK_STBL_SEARCH(17, "accept-language");
        HPACK_STBL_SEARCH(18, "accept-ranges");
        HPACK_STBL_SEARCH(19, "accept");
        HPACK_STBL_SEARCH(20, "access-control-allow-origin");
        HPACK_STBL_SEARCH(21, "age");
        HPACK_STBL_SEARCH(22, "allow");
        HPACK_STBL_SEARCH(23, "authorization");
        HPACK_STBL_SEARCH(24, "cache-control");
        HPACK_STBL_SEARCH(25, "content-disposition");
        HPACK_STBL_SEARCH(26, "content-encoding");
        HPACK_STBL_SEARCH(27, "content-language");
        HPACK_STBL_SEARCH(28, "content-length");
        HPACK_STBL_SEARCH(29, "content-location");
        HPACK_STBL_SEARCH(30, "content-range");
        HPACK_STBL_SEARCH(31, "content-type");
        HPACK_STBL_SEARCH(32, "cookie");
        HPACK_STBL_SEARCH(33, "date");
        HPACK_STBL_SEARCH(34, "etag");
        HPACK_STBL_SEARCH(35, "expect");
        HPACK_STBL_SEARCH(36, "expires");
        HPACK_STBL_SEARCH(37, "from");
        HPACK_STBL_SEARCH(38, "host");
        HPACK_STBL_SEARCH(39, "if-match");
        HPACK_STBL_SEARCH(40, "if-modified-since");
        HPACK_STBL_SEARCH(41, "if-none-match");
        HPACK_STBL_SEARCH(42, "if-range");
        HPACK_STBL_SEARCH(43, "if-unmodified-since");
        HPACK_STBL_SEARCH(44, "last-modified");
        HPACK_STBL_SEARCH(45, "link");
        HPACK_STBL_SEARCH(46, "location");
        HPACK_STBL_SEARCH(47, "max-forwards");
        HPACK_STBL_SEARCH(48, "proxy-authenticate");
        HPACK_STBL_SEARCH(49, "proxy-authorization");
        HPACK_STBL_SEARCH(50, "range");
        HPACK_STBL_SEARCH(51, "referer");
        HPACK_STBL_SEARCH(52, "refresh");
        HPACK_STBL_SEARCH(53, "retry-after");
        HPACK_STBL_SEARCH(54, "server");
        HPACK_STBL_SEARCH(55, "set-cookie");
        HPACK_STBL_SEARCH(56, "strict-transport-security");
        HPACK_STBL_SEARCH(57, "transfer-encoding");
        HPACK_STBL_SEARCH(58, "user-agent");
        HPACK_STBL_SEARCH(59, "vary");
        HPACK_STBL_SEARCH(60, "via");
        HPACK_STBL_SEARCH(61, "www-authenticate");
    } Z_TEST_END;

#undef HPACK_STBL_SEARCH
    Z_TEST(hpack_dtbl_search, "search for matches in the DTBL") {
        hpack_enc_dtbl_t dtbl;

#define HPACK_DTBL_SZCHCK(cnt, sz, sz_lim)                                   \
    Z_HELPER_RUN(z_hpack_enc_dtbl_size_test(&dtbl, cnt, sz, sz_lim))

#define HPACK_DTBL_INSERT(kid, vid, k, v)                                    \
    do {                                                                     \
        hpack_enc_dtbl_add_hdr(&dtbl, LSTR_IMMED_V(k), LSTR_IMMED_V(v), kid, \
                               vid);                                         \
    } while (0)

#define HPACK_DTBL_SEARCH(exp_idx, kid, vid)                                 \
    Z_HELPER_RUN(z_hpack_enc_dtbl_search_test(&dtbl, kid, vid, exp_idx));

        hpack_enc_dtbl_init(&dtbl);
        hpack_enc_dtbl_init_settings(&dtbl, 128);

        /* Example: application specific (well-known) header pairs
         * well-known keys: x-custom-keyN is tokenized as N (N > 0)
         * well-known values: x-custom-valN is tokenized as N (N > 0) */

        HPACK_DTBL_SZCHCK(0, 0, 128);
        HPACK_DTBL_SEARCH(0, 1, 1);
        HPACK_DTBL_SEARCH(0, 2, 1);
        HPACK_DTBL_INSERT(2, 1, "x-custom-key2", "x-custom-val1");
        HPACK_DTBL_SZCHCK(1, 13 + 13 + 32, 128);
        HPACK_DTBL_SEARCH(0, 1, 1);
        HPACK_DTBL_SEARCH(1, 2, 1);
        HPACK_DTBL_SEARCH(-1, 2, 2);
        HPACK_DTBL_SEARCH(1, 2, 0);
        HPACK_DTBL_INSERT(1, 1, "x-custom-key1", "x-custom-val1");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32), 128);
        HPACK_DTBL_SEARCH(1, 1, 1);
        HPACK_DTBL_SEARCH(2, 2, 1);
        HPACK_DTBL_SEARCH(2, 2, 0);
        HPACK_DTBL_SEARCH(-2, 2, 2);
        /* a case of repetition */
        /* XXX: not error, but, should be avoided for efficiency */
        HPACK_DTBL_INSERT(1, 1, "x-custom-key1", "x-custom-val1");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32), 128);
        HPACK_DTBL_SEARCH(1, 1, 1);
        HPACK_DTBL_SEARCH(0, 2, 1);
        HPACK_DTBL_SEARCH(0, 2, 0);
        HPACK_DTBL_SEARCH(0, 2, 2);
        /* a case of non-token pair */
        /* XXX: not error, but, should be avoided for efficiency */
        HPACK_DTBL_INSERT(0, 0, "x-custom-key__", "x-custom-val__");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32) + 2, 128);
        HPACK_DTBL_SEARCH(2, 1, 1);
        HPACK_DTBL_SEARCH(0, 2, 1);
        HPACK_DTBL_SEARCH(0, 2, 0);
        HPACK_DTBL_SEARCH(0, 2, 2);
        HPACK_DTBL_INSERT(3, 3, "x-custom-key3", "x-custom-val3");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32) + 2, 128);
        HPACK_DTBL_SEARCH(0, 1, 1);
        HPACK_DTBL_SEARCH(0, 2, 1);
        HPACK_DTBL_SEARCH(0, 2, 0);
        HPACK_DTBL_SEARCH(0, 2, 2);
        HPACK_DTBL_SEARCH(1, 3, 3);
        HPACK_DTBL_INSERT(4, 4, "x-custom-key4", "x-custom-val4");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32), 128);
        HPACK_DTBL_SEARCH(0, 1, 1);
        HPACK_DTBL_SEARCH(0, 2, 1);
        HPACK_DTBL_SEARCH(2, 3, 3);
        HPACK_DTBL_SEARCH(1, 4, 4);
        /* a case of token key but non-token value */
        HPACK_DTBL_INSERT(3, 0, "x-custom-key3", "x-custom-val__");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32) + 1, 128);
        HPACK_DTBL_SEARCH(0, 1, 1);
        HPACK_DTBL_SEARCH(0, 2, 1);
        HPACK_DTBL_SEARCH(-1, 3, 3);
        HPACK_DTBL_SEARCH(1, 3, 0);
        HPACK_DTBL_SEARCH(2, 4, 4);

        hpack_enc_dtbl_wipe(&dtbl);

#undef HPACK_DTBL_SEARCH
#undef HPACK_DTBL_INSERT
#undef HPACK_DTBL_SZCHCK
    } Z_TEST_END;

    Z_TEST(hpack_dtbl_insert, "insertions into the decoder's DTBL") {
        hpack_dec_dtbl_t dtbl;

#define HPACK_DTBL_SZCHCK(cnt, sz, sz_lim)                                   \
    Z_HELPER_RUN(z_hpack_dec_dtbl_size_test(&dtbl, cnt, sz, sz_lim))

#define HPACK_DTBL_INSERT(k, v)                                              \
    do {                                                                     \
        hpack_dec_dtbl_add_hdr(&dtbl, LSTR_IMMED_V(k), LSTR_IMMED_V(v));     \
    } while (0)

        hpack_dec_dtbl_init(&dtbl);
        hpack_dec_dtbl_init_settings(&dtbl, 128);

        HPACK_DTBL_SZCHCK(0, 0, 128);
        HPACK_DTBL_INSERT("x-custom-key2", "x-custom-val1");
        HPACK_DTBL_SZCHCK(1, 13 + 13 + 32, 128);
        HPACK_DTBL_INSERT("x-custom-key1", "x-custom-val1");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32), 128);
        HPACK_DTBL_INSERT("x-custom-key1", "x-custom-val1");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32), 128);
        HPACK_DTBL_INSERT("x-custom-key__", "x-custom-val__");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32) + 2, 128);
        HPACK_DTBL_INSERT("x-custom-key3", "x-custom-val3");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32) + 2, 128);
        HPACK_DTBL_INSERT("x-custom-key4", "x-custom-val4");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32), 128);
        HPACK_DTBL_INSERT("x-custom-key3", "x-custom-val__");
        HPACK_DTBL_SZCHCK(2, 2 * (13 + 13 + 32) + 1, 128);

        hpack_dec_dtbl_wipe(&dtbl);

#undef HPACK_DTBL_INSERT
#undef HPACK_DTBL_SZCHCK
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */
/* {{{ Headers encoding */

static int
z_hpack_enc_hdr_test(hpack_enc_dtbl_t *dtbl, lstr_t key, lstr_t val,
                     uint16_t key_id, uint16_t val_id, unsigned flags,
                     lstr_t coded_hdr_hex, sb_t *out_)
{
    t_scope;
    lstr_t coded_hdr = t_z_hex_decode(coded_hdr_hex);
    int len = hpack_buflen_to_write_hdr(key, val, flags);
    byte *out = (byte *)sb_grow(out_, len);

    len = hpack_encoder_write_hdr(dtbl, key, val, key_id, val_id, flags, out);
    Z_ASSERT_N(len);
    out_->len += len;
    sb_set_trailing0(out_);
    Z_ASSERT_DATAEQUAL(LSTR_SB_V(out_), coded_hdr);
    Z_HELPER_END;
}

#define HPACK_DTBL_SZCHCK(dtbl, cnt, sz, sz_lim)                             \
    Z_HELPER_RUN(z_hpack_##dtbl##_dtbl_size_test(&(dtbl), cnt, sz, sz_lim))

Z_GROUP_EXPORT(hpack_headers) {

#define HPACK_ENC(dtbl, out, k, v, kid, vid, flags, exp_hex)                 \
    Z_HELPER_RUN(z_hpack_enc_hdr_test(                                       \
        &(dtbl), LSTR_IMMED_V(k), LSTR_IMMED_V(v), (kid), (vid), (flags),    \
        LSTR_IMMED_V(exp_hex), &(out)))

#define HPACK_DEC(dtbl, in, exp)                                             \
    do {                                                                     \
        t_scope;                                                             \
        hpack_xhdr_t xhdr_;                                                  \
        int len_ = hpack_decoder_extract_hdr(&(dtbl), &in, &xhdr_);          \
        int keylen_;                                                         \
        byte *out_;                                                          \
                                                                             \
        Z_ASSERT_N(len_);                                                    \
        out_ = t_new(byte, len_ + 4);                                        \
        len_ = hpack_decoder_write_hdr(&(dtbl), &xhdr_, out_, &keylen_);     \
        Z_ASSERT_N(len_);                                                    \
        Z_ASSERT_LSTREQUAL(LSTR_DATA_V(out_, len_), LSTR_IMMED_V(exp));      \
    } while (0);

    Z_TEST(hpack_hdrs_rfc_C_2,
           "encoding headers: examples from §C.2 of RFC7541") {
        hpack_enc_dtbl_t enc;
        hpack_dec_dtbl_t dec;
        SB_1k(out);
        pstream_t in;

        hpack_enc_dtbl_init(&enc);
        hpack_enc_dtbl_init_settings(&enc, 256);
        hpack_dec_dtbl_init(&dec);
        hpack_dec_dtbl_init_settings(&dec, 256);

        HPACK_DTBL_SZCHCK(enc, 0, 0, 256);

        HPACK_ENC(enc, out, "custom-key", "custom-header", 1, 0,
                  HPACK_FLG_NOZIP_STR,
                  "40  0a  63  75  73  74  6f  6d  2d  6b  65  79  0d  63"
                  "75  73  74  6f  6d  2d  68  65  61  64  65  72");
        HPACK_DTBL_SZCHCK(enc, 1, 55, 256);

        HPACK_DTBL_SZCHCK(dec, 0, 0, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in, "custom-key: custom-header\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 55, 256);

        sb_reset(&out);
        HPACK_ENC(enc, out, ":path", "/sample/path", 0, 0,
                  HPACK_FLG_NOZIP_STR,
                  "04  0c  2f  73  61  6d  70  6c  65  2f  70  61  74  68");
        HPACK_DTBL_SZCHCK(enc, 1, 55, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in, ":path: /sample/path\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 55, 256);

        sb_reset(&out);
        HPACK_ENC(enc, out, "password", "secret", 0, 0,
                  HPACK_FLG_NOZIP_STR | HPACK_FLG_NVRADD_DTBL,
                  "10  08  70  61  73  73  77  6f  72  64  06  73  65  63"
                  "72  65  74");
        HPACK_DTBL_SZCHCK(enc, 1, 55, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in, "password: secret\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 55, 256);

        sb_reset(&out);
        HPACK_ENC(enc, out, ":method", "GET", 0, 0, 0, "82");
        HPACK_DTBL_SZCHCK(enc, 1, 55, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in, ":method: GET\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 55, 256);

        hpack_dec_dtbl_wipe(&dec);
        hpack_enc_dtbl_wipe(&enc);
    } Z_TEST_END;

#undef HPACK_DEC
#undef HPACK_ENC
} Z_GROUP_END;

typedef struct hpack_enc_hdr_t {
    lstr_t key;
    lstr_t val;
    uint16_t key_id;
    uint16_t val_id;
    unsigned flags;
} hpack_enc_hdr_t;

static int z_hpack_enc_dts_test(hpack_enc_dtbl_t *dtbl, size_t sz, sb_t *out_)
{
    byte *out = (byte *)sb_grow(out_, HPACK_BUFLEN_INT);
    int len = hpack_encoder_write_dts_update(dtbl, sz, out);

    Z_ASSERT_N(len);
    out_->len += len;
    sb_set_trailing0(out_);
    Z_HELPER_END;
}

static int
z_hpack_enc_hdrs_test(hpack_enc_dtbl_t *dtbl, hpack_enc_hdr_t *hdrs, int cnt,
                      lstr_t exp_coded_hdrs_hex, sb_t *out_)
{
    t_scope;
    lstr_t exp_coded_hdrs = t_z_hex_decode(exp_coded_hdrs_hex);

    for (int i = 0; i != cnt; i++) {
        hpack_enc_hdr_t e = hdrs[i];
        int len = hpack_buflen_to_write_hdr(e.key, e.val, e.flags);
        byte *out = (byte *)sb_grow(out_, len);

        len = hpack_encoder_write_hdr(dtbl, e.key, e.val, e.key_id, e.val_id,
                                      e.flags, out);
        Z_ASSERT_N(len);
        out_->len += len;
    }
    sb_set_trailing0(out_);
    Z_ASSERT_DATAEQUAL(LSTR_SB_V(out_), exp_coded_hdrs);
    Z_HELPER_END;
}

static int z_hpack_dec_dts_test(hpack_dec_dtbl_t *dtbl, pstream_t *in)
{
    int ret = hpack_decoder_read_dts_update(dtbl, in);

    Z_ASSERT_GT(ret, 0);
    Z_HELPER_END;
}

static int
z_hpack_dec_hdrs_test(hpack_dec_dtbl_t *dtbl, pstream_t *in, lstr_t exp_hdrs)
{
    SB_1k(buf);
    hpack_xhdr_t xhdr;
    int len;
    int keylen;
    byte *out;

    while (!ps_done(in)) {
        len = hpack_decoder_extract_hdr(dtbl, in, &xhdr);
        Z_ASSERT_N(len);
        out = (byte *)sb_grow(&buf, len);
        len = hpack_decoder_write_hdr(dtbl, &xhdr, out, &keylen);
        Z_ASSERT_N(len);
        buf.len += len;
    }
    sb_set_trailing0(&buf);
    Z_ASSERT_LSTREQUAL(LSTR_SB_V(&buf), exp_hdrs);
    Z_HELPER_END;
}

#define HPACK_ENC_HDR(k, v, kid, vid, flags)                                 \
    {                                                                        \
        LSTR_IMMED("" k), LSTR_IMMED("" v), (kid), (vid), (flags)            \
    }

Z_GROUP_EXPORT(hpack_examples) {

#define HPACK_ENC(dtbl, out, hdrs, exp)                                      \
    Z_HELPER_RUN(z_hpack_enc_hdrs_test(&(dtbl), (hdrs), countof(hdrs),       \
                                       LSTR_IMMED_V(exp), &(out)))

#define HPACK_ENC_DTS(dtbl, out, sz)                                         \
    Z_HELPER_RUN(z_hpack_enc_dts_test(&(dtbl), (sz), &(out)))

#define HPACK_DEC(dtbl, in, exp)                                             \
    Z_HELPER_RUN(z_hpack_dec_hdrs_test(&(dtbl), &(in), LSTR_IMMED_V(exp)))

#define HPACK_DEC_DTS(dtbl, in)                                              \
    Z_HELPER_RUN(z_hpack_dec_dts_test(&(dtbl), &(in)))

    Z_TEST(hpack_dts_update_ex1, "Example of use with dtbl size updates") {
        hpack_enc_dtbl_t enc;
        hpack_dec_dtbl_t dec;
        SB_1k(out);
        pstream_t in;

        /* initial agreed-upon max size: 256 */
        hpack_enc_dtbl_init(&enc);
        hpack_enc_dtbl_init_settings(&enc, 256);
        hpack_dec_dtbl_init(&dec);
        hpack_dec_dtbl_init_settings(&dec, 256);

        /* dec instructs enc to use a smaller size: 128 */
        /* enc acknowledges this instruction, and then */
        enc.tbl_size_max = 128;

        /* here comes first hdr block to encode after the ack. */
        /* enc must encode a dts update at the start of this block */
        HPACK_ENC_DTS(enc, out, 128);
        {
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, 0),
                HPACK_ENC_HDR(":path", "/", 0, 0, 0),
                /* key token: 1 :authority */
                /* val token: 1 www.intersec.com */
                HPACK_ENC_HDR(":authority", "www.intersec.com", 1, 1,
                              HPACK_FLG_NOZIP_STR),
            };
            HPACK_ENC(enc, out, inps,
                      "3F  61  82  84  41  10  77  77  77  2e  69  6e  74"
                      "65  72  73  65  63  2e  63  6f  6d");
        }
        HPACK_DTBL_SZCHCK(enc, 1, 58, 128);

        /* dec receives the ack of settings, and then */
        dec.tbl_size_max = 128;

        in = ps_initsb(&out);
        /* dec expects a dts update in the first hdr block after the ack. */
        HPACK_DEC_DTS(dec, in);
        HPACK_DEC(
            dec, in,
            ":method: GET\r\n:path: /\r\n:authority: www.intersec.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 58, 128);

        /* next request */
        sb_reset(&out);
        {
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, 0),
                HPACK_ENC_HDR(":path", "/index.html", 0, 0, 0),
                HPACK_ENC_HDR(":authority", "www.intersec.com", 1, 1,
                              HPACK_FLG_NOZIP_STR),
            };
            HPACK_ENC(enc, out, inps, "82  85  be");
        }
        HPACK_DTBL_SZCHCK(enc, 1, 58, 128);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n:path: /index.html\r\n:authority: "
                  "www.intersec.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 58, 128);

        hpack_dec_dtbl_wipe(&dec);
        hpack_enc_dtbl_wipe(&enc);
    } Z_TEST_END;
    Z_TEST(hpack_example_rfc_C_3,
           "Request Examples without Huffman Coding (C.3. of rfc7541)") {
        hpack_enc_dtbl_t enc;
        hpack_dec_dtbl_t dec;
        SB_1k(out);
        pstream_t in;

        /* initial agreed-upon max size: 256 */
        hpack_enc_dtbl_init(&enc);
        hpack_enc_dtbl_init_settings(&enc, 256);
        hpack_dec_dtbl_init(&dec);
        hpack_dec_dtbl_init_settings(&dec, 256);

        HPACK_DTBL_SZCHCK(enc, 0, 0, 256);

        /* first request */
        {
            unsigned flags = HPACK_FLG_NOZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, flags),
                HPACK_ENC_HDR(":scheme", "http", 0, 0, flags),
                HPACK_ENC_HDR(":path", "/", 0, 0, flags),
                HPACK_ENC_HDR(":authority", "www.example.com", 1, 1, flags),
            };

            HPACK_ENC(enc, out, inps,
                      "82  86  84  41  0f  77  77  77  2e  65  78  61  6d"
                      "70  6c  65  2e  63  6f  6d");
        }
        HPACK_DTBL_SZCHCK(enc, 1, 57, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n"
                  ":scheme: http\r\n"
                  ":path: /\r\n"
                  ":authority: www.example.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 57, 256);

        /* second request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_NOZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, flags),
                HPACK_ENC_HDR(":scheme", "http", 0, 0, flags),
                HPACK_ENC_HDR(":path", "/", 0, 0, flags),
                HPACK_ENC_HDR(":authority", "www.example.com", 1, 1, flags),
                HPACK_ENC_HDR("cache-control", "no-cache", 2, 2, flags),
            };

            HPACK_ENC(
                enc, out, inps,
                "82  86  84  be  58  08  6e  6f  2d  63  61  63  68  65");
        }
        HPACK_DTBL_SZCHCK(enc, 2, 110, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n"
                  ":scheme: http\r\n"
                  ":path: /\r\n"
                  ":authority: www.example.com\r\n"
                  "cache-control: no-cache\r\n");
        HPACK_DTBL_SZCHCK(dec, 2, 110, 256);

        /* third request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_NOZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, flags),
                HPACK_ENC_HDR(":scheme", "https", 0, 0, flags),
                HPACK_ENC_HDR(":path", "/index.html", 0, 0, flags),
                HPACK_ENC_HDR(":authority", "www.example.com", 1, 1, flags),
                HPACK_ENC_HDR("custom-key", "custom-value", 3, 3, flags),
            };

            HPACK_ENC(
                enc, out, inps,
                "82  87  85  bf  40  0a  63  75  73  74  6f  6d  2d  6b  65"
                "79  0c  63  75  73  74  6f  6d  2d  76  61  6c  75  65");
        }
        HPACK_DTBL_SZCHCK(enc, 3, 164, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n"
                  ":scheme: https\r\n"
                  ":path: /index.html\r\n"
                  ":authority: www.example.com\r\n"
                  "custom-key: custom-value\r\n");
        HPACK_DTBL_SZCHCK(dec, 3, 164, 256);

        hpack_dec_dtbl_wipe(&dec);
        hpack_enc_dtbl_wipe(&enc);
    } Z_TEST_END;
    Z_TEST(hpack_example_rfc_C_4,
           "Request Examples with Huffman Coding (C.4. of rfc7541)") {
        hpack_enc_dtbl_t enc;
        hpack_dec_dtbl_t dec;
        SB_1k(out);
        pstream_t in;

        /* initial agreed-upon max size: 256 */
        hpack_enc_dtbl_init(&enc);
        hpack_enc_dtbl_init_settings(&enc, 256);
        hpack_dec_dtbl_init(&dec);
        hpack_dec_dtbl_init_settings(&dec, 256);

        HPACK_DTBL_SZCHCK(enc, 0, 0, 256);

        /* first request */
        {
            unsigned flags = HPACK_FLG_ZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, flags),
                HPACK_ENC_HDR(":scheme", "http", 0, 0, flags),
                HPACK_ENC_HDR(":path", "/", 0, 0, flags),
                HPACK_ENC_HDR(":authority", "www.example.com", 1, 1, flags),
            };

            HPACK_ENC(enc, out, inps,
                      "82  86  84  41  8c  f1  e3  c2  e5  f2  3a  6b  a0"
                      "ab  90  f4  ff");
        }
        HPACK_DTBL_SZCHCK(enc, 1, 57, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n"
                  ":scheme: http\r\n"
                  ":path: /\r\n"
                  ":authority: www.example.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 1, 57, 256);

        /* second request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_ZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, flags),
                HPACK_ENC_HDR(":scheme", "http", 0, 0, flags),
                HPACK_ENC_HDR(":path", "/", 0, 0, flags),
                HPACK_ENC_HDR(":authority", "www.example.com", 1, 1, flags),
                HPACK_ENC_HDR("cache-control", "no-cache", 2, 2, flags),
            };

            HPACK_ENC(enc, out, inps,
                      "82  86  84  be  58  86  a8  eb  10  64  9c  bf");
        }
        HPACK_DTBL_SZCHCK(enc, 2, 110, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n"
                  ":scheme: http\r\n"
                  ":path: /\r\n"
                  ":authority: www.example.com\r\n"
                  "cache-control: no-cache\r\n");
        HPACK_DTBL_SZCHCK(dec, 2, 110, 256);

        /* third request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_ZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":method", "GET", 0, 0, flags),
                HPACK_ENC_HDR(":scheme", "https", 0, 0, flags),
                HPACK_ENC_HDR(":path", "/index.html", 0, 0, flags),
                HPACK_ENC_HDR(":authority", "www.example.com", 1, 1, flags),
                HPACK_ENC_HDR("custom-key", "custom-value", 3, 3, flags),
            };

            HPACK_ENC(enc, out, inps,
                      "82  87  85  bf  40  88  25  a8  49  e9  5b  a9  7d"
                      "7f  89  25  a8  49  e9  5b  b8  e8  b4  bf");
        }
        HPACK_DTBL_SZCHCK(enc, 3, 164, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":method: GET\r\n"
                  ":scheme: https\r\n"
                  ":path: /index.html\r\n"
                  ":authority: www.example.com\r\n"
                  "custom-key: custom-value\r\n");
        HPACK_DTBL_SZCHCK(dec, 3, 164, 256);

        hpack_dec_dtbl_wipe(&dec);
        hpack_enc_dtbl_wipe(&enc);
    } Z_TEST_END;
    Z_TEST(hpack_example_rfc_C_5,
           "Response Examples without Huffman Coding (C.5. of rfc7541)") {
        hpack_enc_dtbl_t enc;
        hpack_dec_dtbl_t dec;
        SB_1k(out);
        pstream_t in;

        /* initial agreed-upon max size: 256 */
        hpack_enc_dtbl_init(&enc);
        hpack_enc_dtbl_init_settings(&enc, 256);
        hpack_dec_dtbl_init(&dec);
        hpack_dec_dtbl_init_settings(&dec, 256);

        HPACK_DTBL_SZCHCK(enc, 0, 0, 256);

        /* first request */
        {
            unsigned flags = HPACK_FLG_NOZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":status", "302", 1, 1, flags),
                HPACK_ENC_HDR("cache-control", "private", 2, 2, flags),
                HPACK_ENC_HDR("date", "Mon, 21 Oct 2013 20:13:21 GMT", 3, 3,
                              flags),
                HPACK_ENC_HDR("location", "https://www.example.com", 4, 4,
                              flags),
            };

            HPACK_ENC(
                enc, out, inps,
                "48  03  33  30  32  58  07  70  72  69  76  61  74  65  61"
                "1d  4d  6f  6e  2c  20  32  31  20  4f  63  74  20  32  30"
                "31  33  20  32  30  3a  31  33  3a  32  31  20  47  4d  54"
                "6e  17  68  74  74  70  73  3a  2f  2f  77  77  77  2e  65"
                "78  61  6d  70  6c  65  2e  63  6f  6d");
        }
        HPACK_DTBL_SZCHCK(enc, 4, 222, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":status: 302\r\n"
                  "cache-control: private\r\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\r\n"
                  "location: https://www.example.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 4, 222, 256);

        /* second request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_NOZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":status", "307", 1, 101, flags),
                HPACK_ENC_HDR("cache-control", "private", 2, 2, flags),
                HPACK_ENC_HDR("date", "Mon, 21 Oct 2013 20:13:21 GMT", 3, 3,
                              flags),
                HPACK_ENC_HDR("location", "https://www.example.com", 4, 4,
                              flags),
            };

            HPACK_ENC(enc, out, inps, "48  03  33  30  37  c1  c0  bf");
        }
        HPACK_DTBL_SZCHCK(enc, 4, 222, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":status: 307\r\n"
                  "cache-control: private\r\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\r\n"
                  "location: https://www.example.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 4, 222, 256);

        /* third request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_NOZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":status", "200", 0, 0, flags),
                HPACK_ENC_HDR("cache-control", "private", 2, 2, flags),
                HPACK_ENC_HDR("date", "Mon, 21 Oct 2013 20:13:22 GMT", 3, 103,
                              flags),
                HPACK_ENC_HDR("location", "https://www.example.com", 4, 4,
                              flags),
                HPACK_ENC_HDR("content-encoding", "gzip", 5, 5, flags),
                HPACK_ENC_HDR("set-cookie",
                              "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; "
                              "max-age=3600; version=1",
                              6, 6, flags),
            };

            HPACK_ENC(
                enc, out, inps,
                "88  c1  61  1d  4d  6f  6e  2c  20  32  31  20  4f  63  74"
                "20  32  30  31  33  20  32  30  3a  31  33  3a  32  32  20"
                "47  4d  54  c0  5a  04  67  7a  69  70  77  38  66  6f  6f"
                "3d  41  53  44  4a  4b  48  51  4b  42  5a  58  4f  51  57"
                "45  4f  50  49  55  41  58  51  57  45  4f  49  55  3b  20"
                "6d  61  78  2d  61  67  65  3d  33  36  30  30  3b  20  76"
                "65  72  73  69  6f  6e  3d  31");
        }
        HPACK_DTBL_SZCHCK(enc, 3, 215, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":status: 200\r\n"
                  "cache-control: private\r\n"
                  "date: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                  "location: https://www.example.com\r\n"
                  "content-encoding: gzip\r\n"
                  "set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; "
                  "max-age=3600; version=1\r\n");
        HPACK_DTBL_SZCHCK(dec, 3, 215, 256);

        hpack_dec_dtbl_wipe(&dec);
        hpack_enc_dtbl_wipe(&enc);
    } Z_TEST_END;
    Z_TEST(hpack_example_rfc_C_6,
           "Response Examples with Huffman Coding (C.6. of rfc7541)") {
        hpack_enc_dtbl_t enc;
        hpack_dec_dtbl_t dec;
        SB_1k(out);
        pstream_t in;

        /* initial agreed-upon max size: 256 */
        hpack_enc_dtbl_init(&enc);
        hpack_enc_dtbl_init_settings(&enc, 256);
        hpack_dec_dtbl_init(&dec);
        hpack_dec_dtbl_init_settings(&dec, 256);

        HPACK_DTBL_SZCHCK(enc, 0, 0, 256);

        /* first request */
        {
            unsigned flags = HPACK_FLG_ZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":status", "302", 1, 1, flags),
                HPACK_ENC_HDR("cache-control", "private", 2, 2, flags),
                HPACK_ENC_HDR("date", "Mon, 21 Oct 2013 20:13:21 GMT", 3, 3,
                              flags),
                HPACK_ENC_HDR("location", "https://www.example.com", 4, 4,
                              flags),
            };

            HPACK_ENC(
                enc, out, inps,
                "48  82  64  02  58  85  ae  c3  77  1a  4b  61  96  d0  7a"
                "be  94  10  54  d4  44  a8  20  05  95  04  0b  81  66  e0"
                "82  a6  2d  1b  ff  6e  91  9d  29  ad  17  18  63  c7  8f"
                "0b  97  c8  e9  ae  82  ae  43  d3");
        }
        HPACK_DTBL_SZCHCK(enc, 4, 222, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":status: 302\r\n"
                  "cache-control: private\r\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\r\n"
                  "location: https://www.example.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 4, 222, 256);

        /* second request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_ZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":status", "307", 1, 101, flags),
                HPACK_ENC_HDR("cache-control", "private", 2, 2, flags),
                HPACK_ENC_HDR("date", "Mon, 21 Oct 2013 20:13:21 GMT", 3, 3,
                              flags),
                HPACK_ENC_HDR("location", "https://www.example.com", 4, 4,
                              flags),
            };

            HPACK_ENC(enc, out, inps, "48  83  64  0e  ff  c1  c0  bf");
        }
        HPACK_DTBL_SZCHCK(enc, 4, 222, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":status: 307\r\n"
                  "cache-control: private\r\n"
                  "date: Mon, 21 Oct 2013 20:13:21 GMT\r\n"
                  "location: https://www.example.com\r\n");
        HPACK_DTBL_SZCHCK(dec, 4, 222, 256);

        /* third request */
        sb_reset(&out);
        {
            unsigned flags = HPACK_FLG_ZIP_STR;
            hpack_enc_hdr_t inps[] = {
                HPACK_ENC_HDR(":status", "200", 0, 0, flags),
                HPACK_ENC_HDR("cache-control", "private", 2, 2, flags),
                HPACK_ENC_HDR("date", "Mon, 21 Oct 2013 20:13:22 GMT", 3, 103,
                              flags),
                HPACK_ENC_HDR("location", "https://www.example.com", 4, 4,
                              flags),
                HPACK_ENC_HDR("content-encoding", "gzip", 5, 5, flags),
                HPACK_ENC_HDR("set-cookie",
                              "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; "
                              "max-age=3600; version=1",
                              6, 6, flags),
            };

            HPACK_ENC(
                enc, out, inps,
                "88  c1  61  96  d0  7a  be  94  10  54  d4  44  a8  20  05"
                "95  04  0b  81  66  e0  84  a6  2d  1b  ff  c0  5a  83  9b"
                "d9  ab  77  ad  94  e7  82  1d  d7  f2  e6  c7  b3  35  df"
                "df  cd  5b  39  60  d5  af  27  08  7f  36  72  c1  ab  27"
                "0f  b5  29  1f  95  87  31  60  65  c0  03  ed  4e  e5  b1"
                "06  3d  50  07");
        }
        HPACK_DTBL_SZCHCK(enc, 3, 215, 256);

        in = ps_initsb(&out);
        HPACK_DEC(dec, in,
                  ":status: 200\r\n"
                  "cache-control: private\r\n"
                  "date: Mon, 21 Oct 2013 20:13:22 GMT\r\n"
                  "location: https://www.example.com\r\n"
                  "content-encoding: gzip\r\n"
                  "set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; "
                  "max-age=3600; version=1\r\n");
        HPACK_DTBL_SZCHCK(dec, 3, 215, 256);

        hpack_dec_dtbl_wipe(&dec);
        hpack_enc_dtbl_wipe(&enc);
    } Z_TEST_END;
} Z_GROUP_END;

#undef HPACK_DTBL_SZCHCK

/* }}} */
