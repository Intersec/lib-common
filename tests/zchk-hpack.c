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
