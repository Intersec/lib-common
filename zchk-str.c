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

/* LCOV_EXCL_START */

#include <math.h>

#include "http.h"
#include "str-buf-pp.h"
#include "z.h"

/* {{{ str */

const void *to_free_g;

static void custom_free(mem_pool_t *m, void *p)
{
    free(p);
    if (p == to_free_g) {
        to_free_g = NULL;
    }
}

static int z_test_padding(lstr_t initial_value, lstr_t padded_exp_value)
{
    SB_1k(sb_padded);

    sb_set_lstr(&sb_padded, initial_value);
    sb_add_pkcs7_8_bytes_padding(&sb_padded);

    Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb_padded), padded_exp_value);
    Z_ASSERT_LSTREQUAL(initial_value,
                       lstr_trim_pkcs7_padding(LSTR_SB_V(&sb_padded)));

    Z_HELPER_END;
}

Z_GROUP_EXPORT(str)
{
    char    buf[BUFSIZ];
    char    buf2[BUFSIZ * 2];
    ssize_t res;

    Z_TEST(lstr_equal, "lstr_equal") {
        Z_ASSERT_LSTREQUAL(LSTR_EMPTY_V, LSTR_EMPTY_V);
        Z_ASSERT_LSTREQUAL(LSTR_NULL_V, LSTR_NULL_V);
        Z_ASSERT_LSTREQUAL(LSTR("toto"), LSTR("toto"));
        Z_ASSERT(!lstr_equal(LSTR_EMPTY_V, LSTR_NULL_V));
        Z_ASSERT(!lstr_equal(LSTR(""), LSTR("toto")));
    } Z_TEST_END;

    Z_TEST(lstr_copyc, "lstr_copyc") {
        lstr_t dst = lstr_dup(LSTR("a string"));
        lstr_t src = LSTR("an other string");
        void (*libc_free)(mem_pool_t *, void *) = mem_pool_libc.free;

        to_free_g = dst.s;

        mem_pool_libc.free = &custom_free;
        lstr_copyc(&dst, src);
        mem_pool_libc.free = libc_free;

        Z_ASSERT_NULL(to_free_g, "destination string has not been freed"
                      " before writing a new value to it");

        Z_ASSERT(dst.mem_pool == MEM_STATIC);
        Z_ASSERT(lstr_equal(dst, src));
    } Z_TEST_END;

    Z_TEST(sb_detach, "sb_detach") {
        sb_t sb;
        char *p;
        int len;

        sb_init(&sb);
        p = sb_detach(&sb, NULL);
        Z_ASSERT(p != __sb_slop);
        Z_ASSERT_EQ(*p, '\0');
        p_delete(&p);

        sb_adds(&sb, "foo");
        p = sb_detach(&sb, &len);
        Z_ASSERT_EQ(len, 3);
        Z_ASSERT_STREQUAL(p, "foo");
        p_delete(&p);
    } Z_TEST_END;

    Z_TEST(sb_add, "sb_add/sb_prepend") {
        SB_1k(sb);

        sb_addf(&sb, "%s", "bar");
        sb_prependf(&sb, "%s", "foo");
        Z_ASSERT_STREQUAL(sb.data, "foobar");

        sb_reset(&sb);
        sb_adds(&sb, "bar");
        sb_prepends(&sb, "foo");
        Z_ASSERT_STREQUAL(sb.data, "foobar");

        memset(buf2, 'a', sizeof(buf2));
        for (unsigned i = 0; i <= sizeof(buf2); i++) {
            sb_reset(&sb);
            sb_adds(&sb, "bar");
            sb_prependf(&sb, "%*pM", i, buf2);
            Z_ASSERT_EQ(sb.len, (int)i + 3);
            Z_ASSERT_EQ(strlen(sb.data), i + 3);
        }

        sb_reset(&sb);
        sb_adds(&sb, "zo meu");
        sb_prepend_lstr(&sb, LSTR("ga bu "));
        Z_ASSERT_STREQUAL(sb.data, "ga bu zo meu");

        sb_reset(&sb);
        sb_adds(&sb, "ol");
        sb_prependc(&sb, 'l');
        Z_ASSERT_STREQUAL(sb.data, "lol");
    } Z_TEST_END;

    Z_TEST(sb_add_urlencode, "sb_add_urlencode") {
        SB_1k(sb);
        lstr_t raw = LSTR("test32@localhost-#!$;*");

        sb_add_urlencode(&sb, raw.s, raw.len);
        Z_ASSERT_LSTREQUAL(LSTR("test32%40localhost-%23%21%24%3B%2A"),
                           LSTR_SB_V(&sb));
    } Z_TEST_END;

    Z_TEST(strconv_hexdecode, "str: strconv_hexdecode") {
        const char *encoded = "30313233";
        const char *decoded = "0123";

        p_clear(&buf, 1);
        res = strconv_hexdecode(buf, sizeof(buf), encoded, -1);
        Z_ASSERT_EQ((size_t)res, strlen(encoded) / 2);
        Z_ASSERT_STREQUAL(buf, decoded);

        encoded = "1234567";
        p_clear(&buf, 1);
        Z_ASSERT_NEG(strconv_hexdecode(buf, sizeof(buf), encoded, -1),
                 "str_hexdecode should not accept odd-length strings");
        encoded = "1234567X";
        p_clear(&buf, 1);
        Z_ASSERT_NEG(strconv_hexdecode(buf, sizeof(buf), encoded, -1),
                 "str_hexdecode accepted non hexadecimal string");
    } Z_TEST_END;

    Z_TEST(lstr_hexencode, "str: t_lstr_hexencode/t_lstr_hexdecode") {
        t_scope;
        lstr_t src = LSTR_IMMED("intersec");
        lstr_t hex = LSTR_IMMED("696e746572736563");
        lstr_t out;

        out = t_lstr_hexencode(src);
        Z_ASSERT_LSTREQUAL(out, hex);
        out = t_lstr_hexdecode(hex);
        Z_ASSERT_LSTREQUAL(out, src);

        out = t_lstr_hexdecode(LSTR_IMMED_V("F"));
        Z_ASSERT_EQ(out.len, 0);
        Z_ASSERT_NULL(out.s);
    } Z_TEST_END;

    Z_TEST(lstr_obfuscate, "str: lstr_obfuscate/lstr_unobfuscate") {
        uint64_t keys[] = { 0, 1, 1234, 2327841961327486523LLU, UINT64_MAX };

        STATIC_ASSERT (sizeof(buf) >= 3 * 16);
        /* Check, for different key values that:
         *   - obfuscation preserves the input (when different than output),
         *   - obfuscation is not identity,
         *   - obfuscation · unobfuscation is identity,
         *   - obfuscating the same string with the same key yields the same
         *     results,
         *   - our functions work both with two different lstr and with the
         *     same lstr given as input and output (inplace).
         */
        for (int i = 0; i < countof(keys); i++) {
            lstr_t orig = LSTR_IMMED("intersec");
            lstr_t obf = LSTR_INIT(buf, orig.len);
            lstr_t unobf = LSTR_INIT(buf + 16, orig.len);
            lstr_t inplace = LSTR_INIT(buf + 16 * 2, orig.len);

            p_clear(&buf, 1);

            lstr_obfuscate(orig, keys[i], obf);
            Z_ASSERT_EQ(orig.len, obf.len);
            Z_ASSERT(!lstr_equal(orig, obf));
            lstr_unobfuscate(obf, keys[i], unobf);
            Z_ASSERT_LSTREQUAL(orig, unobf);

            memcpy(inplace.v, orig.s, orig.len);
            Z_ASSERT_LSTREQUAL(orig, inplace);
            lstr_obfuscate(inplace, keys[i], inplace);
            Z_ASSERT_LSTREQUAL(obf, inplace);
            lstr_unobfuscate(inplace, keys[i], inplace);
            Z_ASSERT_LSTREQUAL(orig, inplace);
        }
    } Z_TEST_END;

    Z_TEST(utf8_stricmp, "str: utf8_stricmp test") {

#define RUN_UTF8_TEST_(Str1, Str2, Strip, Val) \
        ({  int len1 = strlen(Str1);                                         \
            int len2 = strlen(Str2);                                         \
            int cmp  = utf8_stricmp(Str1, len1, Str2, len2, Strip);          \
                                                                             \
            Z_ASSERT_EQ(cmp, Val, "utf8_stricmp(\"%.*s\", \"%.*s\", %d) "    \
                        "returned bad value: %d, expected %d",               \
                        len1, Str1, len2, Str2, Strip, cmp, Val);            \
        })

#define RUN_UTF8_TEST(Str1, Str2, Val) \
        ({  RUN_UTF8_TEST_(Str1, Str2, false, Val);                          \
            RUN_UTF8_TEST_(Str2, Str1, false, -(Val));                       \
            RUN_UTF8_TEST_(Str1, Str2, true, Val);                           \
            RUN_UTF8_TEST_(Str2, Str1, true, -(Val));                        \
            RUN_UTF8_TEST_(Str1"   ", Str2, true, Val);                      \
            RUN_UTF8_TEST_(Str1, Str2"    ", true, Val);                     \
            RUN_UTF8_TEST_(Str1"     ", Str2"  ", true, Val);                \
            if (Val == 0) {                                                  \
                RUN_UTF8_TEST_(Str1"   ", Str2, false, 1);                   \
                RUN_UTF8_TEST_(Str1, Str2"   ", false, -1);                  \
                RUN_UTF8_TEST_(Str1"  ", Str2"    ", false, -1);             \
            }                                                                \
        })

        /* Basic tests and case tests */
        RUN_UTF8_TEST("abcdef", "abcdef", 0);
        RUN_UTF8_TEST("AbCdEf", "abcdef", 0);
        RUN_UTF8_TEST("abcdef", "abbdef", 1);
        RUN_UTF8_TEST("aBCdef", "abbdef", 1);

        /* Accentuation tests */
        RUN_UTF8_TEST("abcdéf", "abcdef", 0);
        RUN_UTF8_TEST("abcdÉf", "abcdef", 0);
        RUN_UTF8_TEST("àbcdèf", "abcdef", 0);

        /* Collation tests */
        RUN_UTF8_TEST("æbcdef", "aebcdef", 0);
        RUN_UTF8_TEST("æbcdef", "aébcdef", 0);
        RUN_UTF8_TEST("abcdœf", "abcdoef", 0);
        RUN_UTF8_TEST("abcdŒf", "abcdoef", 0);

        RUN_UTF8_TEST("æ", "a", 1);
        RUN_UTF8_TEST("æ", "ae", 0);
        RUN_UTF8_TEST("ß", "ss", 0);
        RUN_UTF8_TEST("ßß", "ssss", 0);
        RUN_UTF8_TEST("ßß", "sßs", 0); /* Overlapping collations */

#undef RUN_UTF8_TEST_
#undef RUN_UTF8_TEST
    } Z_TEST_END;

    Z_TEST(utf8_strcmp, "str: utf8_strcmp test") {

#define RUN_UTF8_TEST_(Str1, Str2, Strip, Val) \
        ({  int len1 = strlen(Str1);                                         \
            int len2 = strlen(Str2);                                         \
            int cmp  = utf8_strcmp(Str1, len1, Str2, len2, Strip);           \
                                                                             \
            Z_ASSERT_EQ(cmp, Val, "utf8_strcmp(\"%.*s\", \"%.*s\", %d) "     \
                        "returned bad value: %d, expected %d",               \
                        len1, Str1, len2, Str2, Strip, cmp, Val);            \
        })

#define RUN_UTF8_TEST(Str1, Str2, Val) \
        ({  RUN_UTF8_TEST_(Str1, Str2, false, Val);                          \
            RUN_UTF8_TEST_(Str2, Str1, false, -(Val));                       \
            RUN_UTF8_TEST_(Str1, Str2, true, Val);                           \
            RUN_UTF8_TEST_(Str2, Str1, true, -(Val));                        \
            RUN_UTF8_TEST_(Str1"   ", Str2, true, Val);                      \
            RUN_UTF8_TEST_(Str1, Str2"    ", true, Val);                     \
            RUN_UTF8_TEST_(Str1"     ", Str2"  ", true, Val);                \
            if (Val == 0) {                                                  \
                RUN_UTF8_TEST_(Str1"   ", Str2, false, 1);                   \
                RUN_UTF8_TEST_(Str1, Str2"   ", false, -1);                  \
                RUN_UTF8_TEST_(Str1"  ", Str2"    ", false, -1);             \
            }                                                                \
        })

        /* Basic tests and case tests */
        RUN_UTF8_TEST("abcdef", "abcdef", 0);
        RUN_UTF8_TEST("AbCdEf", "abcdef", -1);
        RUN_UTF8_TEST("abcdef", "abbdef", 1);
        RUN_UTF8_TEST("aBCdef", "abbdef", -1);

        /* Accentuation tests */
        RUN_UTF8_TEST("abcdéf", "abcdef", 0);
        RUN_UTF8_TEST("abcdÉf", "abcdef", -1);
        RUN_UTF8_TEST("àbcdèf", "abcdef", 0);

        /* Collation tests */
        RUN_UTF8_TEST("æbcdef", "aebcdef", 0);
        RUN_UTF8_TEST("æbcdef", "aébcdef", 0);
        RUN_UTF8_TEST("abcdœf", "abcdoef", 0);
        RUN_UTF8_TEST("abcdŒf", "abcdoef", -1);

        RUN_UTF8_TEST("æ", "a", 1);
        RUN_UTF8_TEST("æ", "ae", 0);
        RUN_UTF8_TEST("ß", "ss", 0);
        RUN_UTF8_TEST("ßß", "ssss", 0);
        RUN_UTF8_TEST("ßß", "sßs", 0); /* Overlapping collations */

#undef RUN_UTF8_TEST_
#undef RUN_UTF8_TEST
    } Z_TEST_END;

    Z_TEST(utf8_str_istartswith, "str: utf8_str_istartswith test") {

#define RUN_UTF8_TEST(Str1, Str2, Val)  \
        ({  int len1 = strlen(Str1);                                         \
            int len2 = strlen(Str2);                                         \
            int cmp  = utf8_str_istartswith(Str1, len1, Str2, len2);         \
                                                                             \
            Z_ASSERT_EQ(cmp, Val,                                            \
                        "utf8_str_istartswith(\"%.*s\", \"%.*s\") "          \
                        "returned bad value: %d, expected %d",               \
                        len1, Str1, len2, Str2, cmp, Val);                   \
        })

        /* Basic tests and case tests */
        RUN_UTF8_TEST("abcdef", "abc", true);
        RUN_UTF8_TEST("abcdef", "abcdef", true);
        RUN_UTF8_TEST("abcdef", "abcdefg", false);
        RUN_UTF8_TEST("AbCdEf", "abc", true);
        RUN_UTF8_TEST("abcdef", "AbC", true);
        RUN_UTF8_TEST("aBCdef", "AbC", true);

        /* Accentuation tests */
        RUN_UTF8_TEST("abcdéf", "abcde", true);
        RUN_UTF8_TEST("abcdÉf", "abcdè", true);
        RUN_UTF8_TEST("àbcdèf", "abcdé", true);
        RUN_UTF8_TEST("àbcdè", "abcdé", true);
        RUN_UTF8_TEST("abcde", "àbCdé", true);
        RUN_UTF8_TEST("abcde", "àbcdéf", false);

#undef RUN_UTF8_TEST
    } Z_TEST_END;

    Z_TEST(lstr_utf8_iendswith, "str: lstr_utf8_iendswith test") {

#define RUN_UTF8_TEST(Str1, Str2, Val)  \
        ({  lstr_t lstr1 = LSTR(Str1);                                       \
            lstr_t lstr2 = LSTR(Str2);                                       \
            int cmp  = lstr_utf8_iendswith(lstr1, lstr2);                    \
                                                                             \
            Z_ASSERT_EQ(cmp, Val,                                            \
                        "lstr_utf8_iendswith(\"%s\", \"%s\") "               \
                        "returned bad value: %d, expected %d",               \
                        Str1, Str2, cmp, Val);                               \
        })

        /* Basic tests and case tests */
        RUN_UTF8_TEST("abcdef", "def", true);
        RUN_UTF8_TEST("abcdef", "abcdef", true);
        RUN_UTF8_TEST("abcdef", "0abcdef", false);
        RUN_UTF8_TEST("AbCdEf", "def", true);
        RUN_UTF8_TEST("AbCdEf", "abc", false);
        RUN_UTF8_TEST("abcdef", "DeF", true);
        RUN_UTF8_TEST("abcDEf", "deF", true);

        /* Accentuation tests */
        RUN_UTF8_TEST("abcdéf", "bcdef", true);
        RUN_UTF8_TEST("abcdÉf", "bcdèf", true);
        RUN_UTF8_TEST("àbcdèf", "abcdéF", true);
        RUN_UTF8_TEST("àbcdè", "abcdé", true);
        RUN_UTF8_TEST("abcde", "àbCdé", true);
        RUN_UTF8_TEST("abcde", "0àbcdé", false);

#undef RUN_UTF8_TEST
    } Z_TEST_END;

    Z_TEST(utf8_str_startswith, "str: utf8_str_startswith test") {

#define RUN_UTF8_TEST(Str1, Str2, Val)  \
        ({  int len1 = strlen(Str1);                                         \
            int len2 = strlen(Str2);                                         \
            int cmp  = utf8_str_startswith(Str1, len1, Str2, len2);          \
                                                                             \
            Z_ASSERT_EQ(cmp, Val,                                            \
                        "utf8_str_startswith(\"%.*s\", \"%.*s\") "           \
                        "returned bad value: %d, expected %d",               \
                        len1, Str1, len2, Str2, cmp, Val);                   \
        })

        /* Basic tests and case tests */
        RUN_UTF8_TEST("abcdef", "abc", true);
        RUN_UTF8_TEST("abcdef", "abcdef", true);
        RUN_UTF8_TEST("abcdef", "abcdefg", false);
        RUN_UTF8_TEST("AbCdEf", "abc", false);
        RUN_UTF8_TEST("abcdef", "AbC", false);
        RUN_UTF8_TEST("aBCdef", "AbC", false);
        RUN_UTF8_TEST("aBCdef", "aBC", true);

        /* Accentuation tests */
        RUN_UTF8_TEST("abcdéf", "abcde", true);
        RUN_UTF8_TEST("abcdÉf", "abcdè", false);
        RUN_UTF8_TEST("àbcdèf", "abcdé", true);
        RUN_UTF8_TEST("àbcdèf", "abcdé", true);
        RUN_UTF8_TEST("abcde", "àbcdé", true);
        RUN_UTF8_TEST("abcde", "àbcdéf", false);

#undef RUN_UTF8_TEST
    } Z_TEST_END;

    Z_TEST(lstr_utf8_endswith, "str: lstr_utf8_endswith test") {

#define RUN_UTF8_TEST(Str1, Str2, Val)  \
        ({  lstr_t lstr1 = LSTR(Str1);                                       \
            lstr_t lstr2 = LSTR(Str2);                                       \
            int cmp  = lstr_utf8_endswith(lstr1, lstr2);                     \
                                                                             \
            Z_ASSERT_EQ(cmp, Val,                                            \
                        "lstr_utf8_endswith(\"%s\", \"%s\") "                \
                        "returned bad value: %d, expected %d",               \
                        Str1, Str2, cmp, Val);                               \
        })

        /* Basic tests and case tests */
        RUN_UTF8_TEST("abcdef", "def", true);
        RUN_UTF8_TEST("abcdef", "abcdef", true);
        RUN_UTF8_TEST("abcdef", "0abcdef", false);
        RUN_UTF8_TEST("AbCdEf", "def", false);
        RUN_UTF8_TEST("abcdef", "DeF", false);
        RUN_UTF8_TEST("aBCdef", "deF", false);
        RUN_UTF8_TEST("aBCdEf", "dEf", true);

        /* Accentuation tests */
        RUN_UTF8_TEST("abcdéf", "bcdef", true);
        RUN_UTF8_TEST("abcdÉf", "bcdèf", false);
        RUN_UTF8_TEST("àbcdèf", "bcdéf", true);
        RUN_UTF8_TEST("àbcdèf", "abcdéf", true);
        RUN_UTF8_TEST("abcde", "0àbcdé", false);

#undef RUN_UTF8_TEST
    } Z_TEST_END;

    Z_TEST(lstr_utf8_strlen, "str: lstr_utf8_strlen test") {
        char unterminated[] = { 0xEE, 0x80, 0x80, 0xEE };
        char invalid[]      = { 0xB0, 0x80, 0x80 };

        /* Valid strings. */
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR_NULL_V), 0);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR_EMPTY_V), 0);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR("abcdefgh")), 8);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR("àbçdéfgh")), 8);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR("à")), 1);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR("é")), 1);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR("This is a penguin: ")), 20);

        /* Invalid strings. */
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR_INIT_V(unterminated,
                                                 countof(unterminated))), -1);
        Z_ASSERT_EQ(lstr_utf8_strlen(LSTR_INIT_V(invalid,
                                                 countof(invalid))), -1);
    } Z_TEST_END;

    Z_TEST(lstr_utf8_truncate, "str: lstr_utf8_truncate test") {
        char data[9] = { 'a', 'b', 'c', 0xff, 'e', 0xff, 'g', 'h', '\0' };
        lstr_t lstr_null = LSTR_NULL_V;

#define RUN_TEST(str, count, out) \
        Z_ASSERT_LSTREQUAL(lstr_utf8_truncate(LSTR(str), count), out)

        RUN_TEST("abcdefgh", 9, LSTR("abcdefgh"));
        RUN_TEST("abcdefgh", 8, LSTR("abcdefgh"));
        RUN_TEST("abcdefgh", 7, LSTR("abcdefg"));
        RUN_TEST("abcdefgh", 0, LSTR(""));

        RUN_TEST("àbçdéfgh", 9, LSTR("àbçdéfgh"));
        RUN_TEST("àbçdéfgh", 8, LSTR("àbçdéfgh"));
        RUN_TEST("àbçdéfgh", 7, LSTR("àbçdéfg"));
        RUN_TEST("àbçdéfgh", 5, LSTR("àbçdé"));
        RUN_TEST("àbçdéfgh", 4, LSTR("àbçd"));
        RUN_TEST("àbçdéfgh", 3, LSTR("àbç"));
        RUN_TEST("àbçdéfgh", 2, LSTR("àb"));
        RUN_TEST("àbçdéfgh", 1, LSTR("à"));
        RUN_TEST("àbçdéfgh", 0, LSTR(""));

        RUN_TEST(data, 9, lstr_null);
        RUN_TEST(data, 8, lstr_null);
        RUN_TEST(data, 7, lstr_null);
        RUN_TEST(data, 6, lstr_null);
        RUN_TEST(data, 5, lstr_null);
        RUN_TEST(data, 4, lstr_null);
        RUN_TEST(data, 3, LSTR("abc"));
#undef RUN_TEST
    } Z_TEST_END;

    Z_TEST(path_simplify, "str-path: path_simplify") {
#define T(s0, s1)  \
        ({ pstrcpy(buf, sizeof(buf), s0);    \
            Z_ASSERT_N(path_simplify(buf)); \
            Z_ASSERT_STREQUAL(buf, s1); })

        buf[0] = '\0';
        Z_ASSERT_NEG(path_simplify(buf));
        T("/a/b/../../foo/./", "/foo");
        T("/test/..///foo/./", "/foo");
        T("/../test//foo///",  "/test/foo");
        T("./test/bar",        "test/bar");
        T("./test/../bar",     "bar");
        T("./../test",         "../test");
        T(".//test",           "test");
        T("a/..",              ".");
        T("a/../../..",        "../..");
        T("a/../../b/../c",    "../c");
#undef T
    } Z_TEST_END;

    Z_TEST(path_is_safe, "str-path: path_is_safe test") {
#define T(how, path)  Z_ASSERT(how path_is_safe(path), path)
        T(!, "/foo");
        T(!, "../foo");
        T( , "foo/bar");
        T(!, "foo/bar/foo/../../../../bar");
        T(!, "foo/bar///foo/../../../../bar");
#undef T
    } Z_TEST_END;

    Z_TEST(path_extend, "str-path: path_extend") {
        const char *env_home = getenv("HOME") ?: "/";
        char path_test[PATH_MAX];
        char expected[PATH_MAX];
        char long_prefix[PATH_MAX];
        char very_long_prefix[2 * PATH_MAX];
        char very_long_suffix[2 * PATH_MAX];

#define T(_expected, _prefix, _suffix, ...)  \
        ({ Z_ASSERT_EQ(path_extend(path_test, _prefix, _suffix,              \
                                   ##__VA_ARGS__),                           \
                       (int)strlen(_expected));                              \
            Z_ASSERT_STREQUAL(_expected, path_test);                         \
        })

        T("/foo/bar/1", "/foo/bar/", "%d", 1);
        T("/foo/bar/", "/foo/bar/", "");
        T("/1", "/foo/bar/", "/%d", 1);
        T("/foo/bar/1", "/foo/bar", "%d", 1);
        T("/foo/bar/", "/foo/bar", "");
        T("1", "", "%d", 1);
        T("/1", "", "/%d", 1);

        memset(long_prefix, '1', sizeof(long_prefix));
        long_prefix[PATH_MAX - 3] = '\0';
        T("/foo/bar", long_prefix, "/foo/bar");

        memset(very_long_prefix, '1', sizeof(very_long_prefix));
        very_long_prefix[PATH_MAX + 5] = '\0';
        T("/foo/bar1", very_long_prefix, "/foo/bar%d", 1);

        memset(very_long_prefix, '1', sizeof(very_long_prefix));
        very_long_prefix[PATH_MAX + 5] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, very_long_prefix, "foo/bar%d", 1),
                    -1);

        memset(long_prefix, '1', sizeof(long_prefix));
        long_prefix[PATH_MAX - 1] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, long_prefix, ""), -1);

        memset(long_prefix, '1', sizeof(long_prefix));
        long_prefix[PATH_MAX - 2] = '/';
        long_prefix[PATH_MAX - 1] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, long_prefix, ""), PATH_MAX - 1);

        memset(long_prefix, '1', sizeof(long_prefix));
        long_prefix[PATH_MAX - 2] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, long_prefix, "a"), -1);

        memset(long_prefix, '1', sizeof(long_prefix));
        long_prefix[PATH_MAX - 3] = '/';
        long_prefix[PATH_MAX - 2] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, long_prefix, "a"), PATH_MAX - 1);

        memset(very_long_prefix, '1', sizeof(very_long_prefix));
        very_long_prefix[PATH_MAX-1] = '\0';
        very_long_prefix[PATH_MAX-2] = '/';
        T("/foo/bar1", very_long_prefix, "/foo/bar%d", 1);

        memset(very_long_suffix, '1', sizeof(very_long_suffix));
        memset(long_prefix, '1', sizeof(long_prefix));
        very_long_suffix[0] = '/';
        very_long_suffix[PATH_MAX + 5] = '\0';
        long_prefix[PATH_MAX - 4] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, long_prefix, "%s",
                                very_long_suffix), -1);

        memset(very_long_suffix, '1', sizeof(very_long_suffix));
        memset(very_long_prefix, '1', sizeof(very_long_prefix));
        very_long_suffix[0] = '/';
        very_long_suffix[PATH_MAX + 5] = '\0';
        very_long_prefix[PATH_MAX + 5] = '\0';
        Z_ASSERT_EQ(path_extend(path_test, very_long_prefix, "%s",
                                very_long_suffix), -1);

        memset(very_long_prefix, '1', sizeof(very_long_prefix));
        very_long_prefix[PATH_MAX-2] = '\0';
        very_long_prefix[PATH_MAX-3] = '/';
        T("/foo/bar1", very_long_prefix, "/foo/bar%d", 1);

        snprintf(expected, sizeof(expected), "%s/foo/bar/1", env_home);
        T(expected, "/prefix", "~/foo/bar/%d", 1);

        memset(very_long_prefix, '1', sizeof(very_long_prefix));
        very_long_prefix[PATH_MAX + 5] = '\0';
        T(expected, very_long_prefix, "~/foo/bar/%d", 1);
#undef T

    } Z_TEST_END;

    Z_TEST(path_relative_to, "path relative to") {
        char old_cwd[PATH_MAX];

#define T(_from, _to, _expected)                                             \
    do {                                                                     \
        char _path[PATH_MAX];                                                \
        int _len = path_relative_to(_path, (_from), (_to));                  \
                                                                             \
        Z_ASSERT_N(_len);                                                    \
        Z_ASSERT_STREQUAL(_path, (_expected));                               \
        Z_ASSERT_EQ((int)strlen(_expected), _len);                           \
    } while (0)

        T("/a/b/c/d", "/a/b/e/f", "../e/f");
        T("/a/b/c/d/", "/a/b/e/f", "../../e/f");
        T("a/b/c", "d/e/", "../../d/e");
        T("a/b/c/", "a/b/c", "c");
        T("a/b/c/", "a/b/c/", "c");
        T("a/b/c", "a/b/c/", "c");
        T("toto/tata", "toto/titi", "titi");
        T("/aa/bb/..//cc", "/aa/bb/./dd", "bb/dd");
        T("/qq/ss/dd", "/ww/xx/cc", "../../ww/xx/cc");

        Z_ASSERT_P(getcwd(old_cwd, sizeof(old_cwd)));
        Z_ASSERT_N(chdir("/tmp"));

        T("/tmp/a/b/c", "a/d/e", "../d/e");
        T("a/b/c", "/tmp/a/d/e", "../d/e");

        Z_ASSERT_N(chdir(old_cwd));

#undef T
    } Z_TEST_END;

    Z_TEST(strstart, "str: strstart") {
        static const char *week =
            "Monday Tuesday Wednesday Thursday Friday Saturday Sunday";
        const char *p;

        Z_ASSERT(strstart(week, "Monday", &p));
        Z_ASSERT(week + strlen("Monday") == p,
                 "finding Monday at the proper position");
        Z_ASSERT(!strstart(week, "Tuesday", NULL),
                 "week doesn't start with Tuesday");
    } Z_TEST_END;

    Z_TEST(stristart, "str: stristart") {
        static const char *week =
            "Monday Tuesday Wednesday Thursday Friday Saturday Sunday";
        const char *p = NULL;

        Z_ASSERT(stristart(week, "mOnDaY", &p));
        Z_ASSERT(week + strlen("mOnDaY") == p,
                 "finding mOnDaY at the proper position");
        Z_ASSERT(!stristart(week, "tUESDAY", NULL),
                 "week doesn't start with tUESDAY");
    } Z_TEST_END;

    Z_TEST(stristrn, "str: stristrn") {
        static const char *alphabet = "abcdefghijklmnopqrstuvwxyz";

        Z_ASSERT(stristr(alphabet, "aBC") == alphabet,
                 "not found at start of string");
        Z_ASSERT(stristr(alphabet, "Z") == alphabet + 25,
                 "not found at end of string");
        Z_ASSERT(stristr(alphabet, "mn") == alphabet + 12,
                 "not found in the middle of the string");
        Z_ASSERT_NULL(stristr(alphabet, "123"), "inexistant string found");
    } Z_TEST_END;

    Z_TEST(strfind, "str: strfind") {
        Z_ASSERT( strfind("1,2,3,4", "1", ','));
        Z_ASSERT( strfind("1,2,3,4", "2", ','));
        Z_ASSERT( strfind("1,2,3,4", "4", ','));
        Z_ASSERT(!strfind("11,12,13,14", "1", ','));
        Z_ASSERT(!strfind("11,12,13,14", "2", ','));
        Z_ASSERT( strfind("11,12,13,14", "11", ','));
        Z_ASSERT(!strfind("11,12,13,14", "111", ','));
        Z_ASSERT(!strfind("toto,titi,tata,tutu", "to", ','));
        Z_ASSERT(!strfind("1|2|3|4|", "", '|'));
        Z_ASSERT( strfind("1||3|4|", "", '|'));
    } Z_TEST_END;

    Z_TEST(buffer_increment, "str: buffer_increment") {
#define T(initval, expectedval, expectedret)       \
        ({  pstrcpy(buf, sizeof(buf), initval);                      \
            Z_ASSERT_EQ(expectedret, buffer_increment(buf, -1));     \
            Z_ASSERT_STREQUAL(buf, expectedval); })

        T("0", "1", 0);
        T("1", "2", 0);
        T("00", "01", 0);
        T("42", "43", 0);
        T("09", "10", 0);
        T("99", "00", 1);
        T(" 99", " 00", 1);
        T("", "", 1);
        T("foobar-00", "foobar-01", 0);
        T("foobar-0-99", "foobar-0-00", 1);

#undef T
    } Z_TEST_END;

    Z_TEST(buffer_increment_hex, "str: buffer_increment_hex") {
#define T(initval, expectedval, expectedret)   \
        ({  pstrcpy(buf, sizeof(buf), initval);                          \
            Z_ASSERT_EQ(expectedret, buffer_increment_hex(buf, -1));     \
            Z_ASSERT_STREQUAL(buf, expectedval); })

        T("0", "1", 0);
        T("1", "2", 0);
        T("9", "A", 0);
        T("a", "b", 0);
        T("Ab", "Ac", 0);
        T("00", "01", 0);
        T("42", "43", 0);
        T("09", "0A", 0);
        T("0F", "10", 0);
        T("FFF", "000", 1);
        T(" FFF", " 000", 1);
        T("FFFFFFFFFFFFFFF", "000000000000000", 1);
        T("", "", 1);
        T("foobar", "foobar", 1);
        T("foobaff", "foobb00", 0);
        T("foobar-00", "foobar-01", 0);
        T("foobar-0-ff", "foobar-0-00", 1);

#undef T
    } Z_TEST_END;

    Z_TEST(strrand, "str: strrand") {
        char b[32];

        Z_ASSERT_EQ(0, pstrrand(b, sizeof(b), 0, 0));
        Z_ASSERT_EQ(strlen(b), 0U);

        Z_ASSERT_EQ(3, pstrrand(b, sizeof(b), 0, 3));
        Z_ASSERT_EQ(strlen(b), 3U);

        /* Ask for 32 bytes, where buffer can only contain 31. */
        Z_ASSERT_EQ(ssizeof(b) - 1, pstrrand(b, sizeof(b), 0, sizeof(b)));
        Z_ASSERT_EQ(sizeof(b) - 1, strlen(b));
    } Z_TEST_END;

    Z_TEST(strtoip, "str: strtoip") {
#define T(p, err_exp, val_exp, end_i) \
        ({  const char *endp;                                               \
            int end_exp = (end_i >= 0) ? end_i : (int)strlen(p);            \
                                                                            \
            errno = 0;                                                      \
            Z_ASSERT_EQ(val_exp, strtoip(p, &endp));                        \
            Z_ASSERT_EQ(err_exp, errno);                                    \
            Z_ASSERT_EQ(end_exp, endp - p);                                 \
        })

        T("123", 0, 123, -1);
        T(" 123", 0, 123, -1);
        T(" +123", 0, 123, -1);
        T("  -123", 0, -123, -1);
        T(" +-123", EINVAL, 0, 2);
        T("123 ", 0, 123, 3);
        T("123z", 0, 123, 3);
        T("123+", 0, 123, 3);
        T("2147483647", 0, 2147483647, -1);
        T("2147483648", ERANGE, 2147483647, -1);
        T("21474836483047203847094873", ERANGE, 2147483647, -1);
        T("000000000000000000000000000000000001", 0, 1, -1);
        T("-2147483647", 0, -2147483647, -1);
        T("-2147483648", 0, -2147483647 - 1, -1);
        T("-2147483649", ERANGE, -2147483647 - 1, -1);
        T("-21474836483047203847094873", ERANGE, -2147483647 - 1, -1);
        T("-000000000000000000000000000000000001", 0, -1, -1);
        T("", EINVAL, 0, -1);
        T("          ", EINVAL, 0, -1);
        T("0", 0, 0, -1);
        T("0x0", 0, 0, 1);
        T("010", 0, 10, -1);
#undef T
    } Z_TEST_END;

    Z_TEST(memtoip, "str: memtoip") {
#define T(p, err_exp, val_exp, end_i) \
        ({  const byte *endp;                                               \
            int end_exp = (end_i >= 0) ? end_i : (int)strlen(p);            \
                                                                            \
            errno = 0;                                                      \
            Z_ASSERT_EQ(val_exp, memtoip(p, strlen(p), &endp));             \
            Z_ASSERT_EQ(err_exp, errno);                                    \
            Z_ASSERT_EQ(end_exp, endp - (const byte *)p);                   \
        })

        T("123", 0, 123, -1);
        T(" 123", 0, 123, -1);
        T(" +123", 0, 123, -1);
        T("  -123", 0, -123, -1);
        T(" +-123", EINVAL, 0, 2);
        T("123 ", 0, 123, 3);
        T("123z", 0, 123, 3);
        T("123+", 0, 123, 3);
        T("2147483647", 0, 2147483647, -1);
        T("2147483648", ERANGE, 2147483647, -1);
        T("21474836483047203847094873", ERANGE, 2147483647, -1);
        T("000000000000000000000000000000000001", 0, 1, -1);
        T("-2147483647", 0, -2147483647, -1);
        T("-2147483648", 0, -2147483647 - 1, -1);
        T("-2147483649", ERANGE, -2147483647 - 1, -1);
        T("-21474836483047203847094873", ERANGE, -2147483647 - 1, -1);
        T("-000000000000000000000000000000000001", 0, -1, -1);
        T("", EINVAL, 0, -1);
        T("          ", EINVAL, 0, -1);
        T("0", 0, 0, -1);
        T("0x0", 0, 0, 1);
        T("010", 0, 10, -1);
#undef T
    } Z_TEST_END;

    Z_TEST(strtolp, "str: strtolp") {
#define T(p, flags, min, max, val_exp, ret_exp, end_i) \
    ({  const char *endp;                                                    \
        long val;                                                            \
        int end_exp = (end_i >= 0) ? end_i : (int)strlen(p);                 \
                                                                             \
        Z_ASSERT_EQ(ret_exp, strtolp(p, &endp, 0, &val, flags, min, max));   \
        if (ret_exp == 0) {                                                  \
            Z_ASSERT_EQ(val_exp, val);                                       \
            Z_ASSERT_EQ(end_exp, endp - p);                                  \
        }                                                                    \
    })

        T("123", 0, 0, 1000, 123, 0, -1);

        /* Check min/max */
        T("123", STRTOLP_CHECK_RANGE, 0, 100, 123, -ERANGE, 0);
        T("123", STRTOLP_CHECK_RANGE, 1000, 2000, 123, -ERANGE, 0);

        /* check min/max corner cases */
        T("123", STRTOLP_CHECK_RANGE, 0, 123, 123, 0, -1);
        T("123", STRTOLP_CHECK_RANGE, 0, 122, 123, -ERANGE, 0);
        T("123", STRTOLP_CHECK_RANGE, 123, 1000, 123, 0, -1);
        T("123", STRTOLP_CHECK_RANGE, 124, 1000, 123, -ERANGE, 0);

        /* Check skipspaces */
        T(" 123", 0, 0, 1000, 123, -EINVAL, 0);
        T("123 ", STRTOLP_CHECK_END, 0, 100, 123, -EINVAL, 0);
        T(" 123 ", STRTOLP_CHECK_END | STRTOLP_CHECK_RANGE, 0, 100, 123, -EINVAL, 0);
        T(" 123", STRTOLP_IGNORE_SPACES, 0, 100, 123, 0, -1);
        T(" 123 ", STRTOLP_IGNORE_SPACES, 0, 100, 123, 0, -1);
        T(" 123 ", STRTOLP_IGNORE_SPACES | STRTOLP_CHECK_RANGE, 0, 100, 123, -ERANGE, 0);
        T(" 123 ", STRTOLP_IGNORE_SPACES | STRTOLP_CLAMP_RANGE, 0, 100, 100, 0, -1);
        T("123456789012345678901234567890", 0, 0, 100, 123, -ERANGE, 0);
        T("123456789012345678901234567890 ", STRTOLP_CHECK_END, 0, 100, 123, -EINVAL, 0);
        T("123456789012345678901234567890",  STRTOLP_CLAMP_RANGE, 0, 100, 100, 0, -1);
        T("123456789012345678901234567890 ", STRTOLP_CLAMP_RANGE, 0, 100, 100, 0, 30);
#undef T
    } Z_TEST_END;

    Z_TEST(memtoxll_ext, "str: memtoxll_ext") {
#define T(str, sgn, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp) \
    do {                                                                     \
        uint64_t val;                                                        \
        int _len = (len == INT_MAX || str) ? (int)strlen(p) : len;           \
        int _ret_exp = (ret_exp == INT_MAX && _len >= 0) ? _len : ret_exp;   \
        int _end_exp = (_ret_exp >= 0 && !end_exp) ? _ret_exp : end_exp;     \
        int ret;                                                             \
                                                                             \
        if (str) {                                                           \
            ret = (sgn) ? strtoll_ext(p, (int64_t *)&val,                    \
                                      _endp ? (const char **)&endp : NULL,   \
                                      base)                                  \
                        : strtoull_ext(p, &val,                              \
                                       _endp ? (const char **)&endp : NULL,  \
                                       base);                                \
        } else {                                                             \
            ret = (sgn) ? memtoll_ext(p, _len, (int64_t *)&val,              \
                                      _endp ? &endp : NULL, base)            \
                        : memtoull_ext(p, _len, &val, _endp ? &endp : NULL,  \
                                       base);                                \
        }                                                                    \
                                                                             \
        Z_ASSERT_EQ(_ret_exp, ret);                                          \
        if (!errno)                                                          \
            Z_ASSERT_EQ((uint64_t)(val_exp), val);                           \
        Z_ASSERT_EQ(err_exp, errno);                                         \
        if (_endp)                                                           \
            Z_ASSERT_EQ(_end_exp, (const char *)endp - (const char *)p);     \
    } while (0)

#define TT_MEM(p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp)      \
    do {                                                                     \
        T(0, 0, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
        T(0, 1, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
    } while (0)

#define TT_USGN(p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp)     \
    do {                                                                     \
        T(0, 0, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
        T(1, 0, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
    } while (0)

#define TT_SGN(p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp)      \
    do {                                                                     \
        T(0, 1, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
        T(1, 1, p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
    } while (0)

#define TT_ALL(p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp)      \
    do {                                                                     \
        TT_USGN(p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
        TT_SGN( p, len, _endp, base, val_exp, ret_exp, end_exp, err_exp);    \
    } while (0)

        const void *endp;

        TT_ALL("123",           3, true, 0, 123,  3, 0, 0);
        TT_ALL("123.456", INT_MAX, true, 0, 123,  3, 0, 0);

        /* different len */
        TT_MEM("123",  2, true, 0,  12,  2, 0, 0);
        TT_MEM("123;", 4, true, 0, 123,  3, 0, 0);
        TT_MEM("123k", 3, true, 0, 123,  3, 0, 0);
        TT_MEM("123",  0, true, 0,   0,  0, 0, 0);
        TT_MEM("123", -1, true, 0,   0, -1, 0, EINVAL);

        /* argument endp NULL */
        TT_ALL("123", INT_MAX, false, 0, 123, INT_MAX, 0, 0);

        /* spaces and sign char */
        TT_ALL("  123  ", INT_MAX, true, 0,  123, 5,       0, 0);
        TT_ALL("+123",    INT_MAX, true, 0,  123, INT_MAX, 0, 0);
        TT_SGN("-123",    INT_MAX, true, 0, -123, INT_MAX, 0, 0);
        TT_ALL("  +",     INT_MAX, true, 0,  -1, -1, 0, EINVAL);
        TT_ALL("  -",     INT_MAX, true, 0,  -1, -1, 0, EINVAL);

        /* other bases than 10 */
        TT_ALL("0x123", INT_MAX, true,  0, 0x123, INT_MAX, 0, 0);
        TT_ALL("0123",  INT_MAX, true,  0,  0123, INT_MAX, 0, 0);
        TT_ALL("123",   INT_MAX, true, 20,   443, INT_MAX, 0, 0);

        /* extensions */
        TT_ALL("100w",  INT_MAX, true, 0,   60480000, INT_MAX, 0, 0);
        TT_ALL("100d",  INT_MAX, true, 0,    8640000, INT_MAX, 0, 0);
        TT_ALL("100h",  INT_MAX, true, 0,     360000, INT_MAX, 0, 0);
        TT_ALL("100m",  INT_MAX, true, 0,       6000, INT_MAX, 0, 0);
        TT_ALL("100s",  INT_MAX, true, 0,        100, INT_MAX, 0, 0);
        TT_ALL("100T",  INT_MAX, true, 0, 100L << 40, INT_MAX, 0, 0);
        TT_ALL("100G",  INT_MAX, true, 0, 100L << 30, INT_MAX, 0, 0);
        TT_ALL("100M",  INT_MAX, true, 0, 100  << 20, INT_MAX, 0, 0);
        TT_ALL("100K",  INT_MAX, true, 0,     102400, INT_MAX, 0, 0);
        TT_ALL("100K;", INT_MAX, true, 0,     102400,       4, 0, 0);
        TT_MEM("100Ki",       4, true, 0,     102400,       4, 0, 0);

        /* extension with octal number */
        TT_ALL("012K",  INT_MAX, true, 0, 10240, INT_MAX, 0, 0);

        /* negative number with extension */
        TT_SGN("-100K", INT_MAX, true, 0, -102400, INT_MAX, 0, 0);

        /* invalid extensions */
        TT_ALL("100k",  INT_MAX, true, 0, 100, -1, 3, EDOM);
        TT_ALL("100Ki", INT_MAX, true, 0, 100, -1, 4, EDOM);

        /* values at limits for unsigned */
        TT_USGN("18446744073709551615s", INT_MAX, true, 0, UINT64_MAX,
                INT_MAX, 0, 0);
        TT_USGN("18446744073709551616s", INT_MAX, true, 0, UINT64_MAX,
                -1, 20, ERANGE);
        TT_USGN("16777215T", INT_MAX, true, 0, 16777215 * (1UL << 40),
                INT_MAX, 0, 0);
        TT_USGN("16777216T", INT_MAX, true, 0, UINT64_MAX, -1, 9, ERANGE);
        TT_USGN("-123",    INT_MAX, true, 0, 0, -1, 0, ERANGE);
        TT_USGN("   -123", INT_MAX, true, 0, 0, -1, 0, ERANGE);
        TT_USGN("    -0 ", INT_MAX, true, 0,  0, 6, 0, 0);
        TT_USGN("  -az ",  INT_MAX, true, 0,  -1, -1, 0, EINVAL);
        TT_USGN("  - az ", INT_MAX, true, 0,  -1, -1, 0, EINVAL);
        TT_USGN("  az ",   INT_MAX, true, 0,  -1, -1, 0, EINVAL);

        /* positives values at limits for signed */
        TT_SGN("9223372036854775807s", INT_MAX, true, 0, INT64_MAX,
               INT_MAX, 0, 0);
        TT_SGN("9223372036854775808s", INT_MAX, true, 0, INT64_MAX,
               -1, 19, ERANGE);
        TT_SGN("8388607T", INT_MAX, true, 0, 8388607 * (1L << 40),
               INT_MAX, 0, 0);
        TT_SGN("8388608T", INT_MAX, true, 0, INT64_MAX, -1, 8, ERANGE);

        /* negatives values at limits for signed */
        TT_SGN("-9223372036854775808s", INT_MAX, true, 0, INT64_MIN,
               INT_MAX, 0, 0);
        TT_SGN("-9223372036854775809s", INT_MAX, true, 0, INT64_MIN,
               -1, 20, ERANGE);
        TT_SGN("-8388608T", INT_MAX, true, 0, -8388608 * (1L << 40),
               INT_MAX, 0, 0);
        TT_SGN("-8388609T", INT_MAX, true, 0, INT64_MIN, -1, 9, ERANGE);

#undef T
#undef TT_MEM
#undef TT_USGN
#undef TT_SGN
#undef TT_ALL
    } Z_TEST_END;

    Z_TEST(memtod, "str: memtod") {

#define DOUBLE_ABS(_d)   (_d) > 0 ? (_d) : -(_d)

/* Absolute maximum error is bad, but in our case it is perfectly
 * acceptable
 */
#define DOUBLE_CMP(_d1, _d2)  (DOUBLE_ABS(_d1 - _d2) < 0.00001)

#define TD(p, err_exp, val_exp, end_i) \
        ({  const byte *endp;                                               \
            int end_exp = (end_i >= 0) ? end_i : (int)strlen(p);            \
                                                                            \
            errno = 0;                                                      \
            Z_ASSERT(DOUBLE_CMP(val_exp, memtod(p, strlen(p), &endp)));     \
            Z_ASSERT_EQ(err_exp, errno);                                    \
            Z_ASSERT_EQ(end_exp, endp - (const byte *)p);                   \
            Z_ASSERT(DOUBLE_CMP(val_exp, memtod(p, -1, &endp)));            \
            Z_ASSERT_EQ(err_exp, errno);                                    \
            Z_ASSERT_EQ(end_exp, endp - (const byte *)p);                   \
        })

        TD("123", 0, 123.0, -1);
        TD(" 123", 0, 123.0, -1);
        TD("123.18", 0, 123.18, -1);
        TD(" +123.90", 0, 123.90, -1);
        TD("  -123", 0, -123.0, -1);
        TD("123.50 ", 0, 123.50, 6);
        TD("123z.50", 0, 123, 3);
        TD("123+", 0, 123.0, 3);
        TD("000000000000000000000000000000000001", 0, 1, -1);
        TD("-000000000000000000000000000000000001", 0, -1, -1);
        TD("", 0, 0, -1);
        TD("          ", 0, 0, 0);
        TD("0", 0, 0, -1);
        TD("0x0", 0, 0, -1);
        TD("010", 0, 10, -1);
        TD("10e3", 0, 10000, -1);
        TD("0.1e-3", 0, 0.0001, -1);

#undef TD
#undef DOUBLE_CMP
#undef DOUBLE_ABS
    } Z_TEST_END;

    Z_TEST(memtoxllp, "str: memtoxllp") {
        lstr_t s = LSTR("123");
        const byte *end;

        Z_ASSERT_EQ(123, memtollp(s.s, s.len, NULL));
        Z_ASSERT_EQ(123, memtollp(s.s, s.len, &end));
        Z_ASSERT(end == (byte *)s.s + s.len);

        Z_ASSERT_EQ(123U, memtoullp(s.s, s.len, NULL));
        Z_ASSERT_EQ(123U, memtoullp(s.s, s.len, &end));
        Z_ASSERT(end == (byte *)s.s + s.len);
    } Z_TEST_END;

    Z_TEST(str_tables, "str: test conversion tables") {
        for (int i = 0; i < countof(__str_unicode_lower); i++) {
            /* Check idempotence */
            if (__str_unicode_lower[i] < countof(__str_unicode_lower)) {
                Z_ASSERT_EQ(__str_unicode_lower[i],
                            __str_unicode_lower[__str_unicode_lower[i]],
                            "%x", i);
            }
            if (__str_unicode_upper[i] < countof(__str_unicode_upper)) {
                Z_ASSERT_EQ(__str_unicode_upper[i],
                            __str_unicode_upper[__str_unicode_upper[i]],
                            "%x", i);
            }
        }

        for (int i = 0; i < countof(__str_unicode_general_ci); i++) {
            uint32_t ci = __str_unicode_general_ci[i];
            uint32_t cs = __str_unicode_general_cs[i];

            cs = (__str_unicode_upper[cs >> 16] << 16)
               |  __str_unicode_upper[cs & 0xffff];

            Z_ASSERT_EQ(ci, cs);
        }
    } Z_TEST_END;

    Z_TEST(str_normalize, "str: utf8 normalizer") {
        SB_1k(sb);

#define T(from, ci, cs)  do {                                                \
        sb_reset(&sb);                                                       \
        Z_ASSERT_N(sb_normalize_utf8(&sb, from, sizeof(from) - 1, true));    \
        Z_ASSERT_EQUAL(sb.data, sb.len, ci, sizeof(ci) - 1);                 \
        sb_reset(&sb);                                                       \
        Z_ASSERT_N(sb_normalize_utf8(&sb, from, sizeof(from) - 1, false));   \
        Z_ASSERT_EQUAL(sb.data, sb.len, cs, sizeof(cs) - 1);                 \
    } while (0)

        T("toto", "TOTO", "toto");
        T("ToTo", "TOTO", "ToTo");
        T("électron", "ELECTRON", "electron");
        T("Électron", "ELECTRON", "Electron");

        T("Blisßs", "BLISSSS", "Blissss");
        T("Œœ", "OEOE", "OEoe");
#undef T
    } Z_TEST_END;

    Z_TEST(str_lowup, "str: utf8 tolower/toupper") {
        SB_1k(sb);

#define T(from, low, up)  do {                                               \
        sb_reset(&sb);                                                       \
        Z_ASSERT_N(sb_add_utf8_tolower(&sb, from, sizeof(from) - 1));        \
        Z_ASSERT_EQUAL(sb.data, sb.len, low, sizeof(low) - 1);               \
        sb_reset(&sb);                                                       \
        Z_ASSERT_N(sb_add_utf8_toupper(&sb, from, sizeof(from) - 1));        \
        Z_ASSERT_EQUAL(sb.data, sb.len, up, sizeof(up) - 1);                 \
    } while (0)

        T("toto", "toto", "TOTO");
        T("ToTo", "toto", "TOTO");
        T("électron", "électron", "ÉLECTRON");
        T("Électron", "électron", "ÉLECTRON");

        T("Blisßs", "blisßs", "BLISßS");
        T("Œœ", "œœ", "ŒŒ");
#undef T
    } Z_TEST_END;


    Z_TEST(sb_add_double_fmt, "str: sb_add_double_fmt") {
#define T(val, nb_max_decimals, dec_sep, thousand_sep, res) \
    ({  SB_1k(sb);                                                           \
                                                                             \
        sb_add_double_fmt(&sb, val, nb_max_decimals, dec_sep, thousand_sep); \
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(res));                       \
    })

        T(    0,          5, '.', ',',  "0");
        T(   -0,          5, '.', ',',  "0");
        T(    1,          5, '.', ',',  "1");
        T(   12,          5, '.', ',',  "12");
        T(  123,          5, '.', ',',  "123");
        T( 1234,          5, '.', ',',  "1,234");
        T( 1234.123,      0, '.', ',',  "1,234");
        T( 1234.123,      1, '.', ',',  "1,234.1");
        T( 1234.123,      2, '.', ',',  "1,234.12");
        T( 1234.123,      3, '.', ',',  "1,234.123");
        T( 1234.123,      4, '.', ',',  "1,234.1230");
        T(-1234.123,      5, ',', ' ', "-1 234,12300");
        T(-1234.123,      5, '.',  -1, "-1234.12300");
        T( 1234.00000001, 2, '.', ',', "1,234");
        T(NAN,       5, '.',  -1, "NaN");
        T(INFINITY,  5, '.',  -1, "Inf");
#undef T
    } Z_TEST_END;

    Z_TEST(sb_add_punycode, "str: sb_add_punycode") {
        SB_1k(sb);

#define T(_in, _out) \
        do {                                                                 \
            Z_ASSERT_N(sb_add_punycode_str(&sb, _in, strlen(_in)));          \
            Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(_out));                  \
            sb_reset(&sb);                                                   \
        } while (0)

        /* Basic test cases to validate sb_add_punycode_str */
        T("hello-world", "hello-world-");
        T("hellö-world", "hell-world-hcb");
        T("bücher", "bcher-kva");
        T("bücherü", "bcher-kvae");
#undef T

#define T(_name, _out, ...) \
        do {                                                                 \
            const uint32_t _in[] = { __VA_ARGS__ };                          \
                                                                             \
            Z_ASSERT_N(sb_add_punycode_vec(&sb, _in, countof(_in)),          \
                       "punycode encoding failed for " _name);               \
            Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(_out),                   \
                               "punycode comparison failed for " _name);     \
            sb_reset(&sb);                                                   \
        } while (0)

        /* More complex test cases taken in section 7.1 (Sample strings) of
         * RFC 3492. */
        T("(A) Arabic (Egyptian)", "egbpdaj6bu4bxfgehfvwxn",
          0x0644, 0x064A, 0x0647, 0x0645, 0x0627, 0x0628, 0x062A, 0x0643,
          0x0644, 0x0645, 0x0648, 0x0634, 0x0639, 0x0631, 0x0628, 0x064A,
          0x061F);
        T("(B) Chinese (simplified)", "ihqwcrb4cv8a8dqg056pqjye",
          0x4ED6, 0x4EEC, 0x4E3A, 0x4EC0, 0x4E48, 0x4E0D, 0x8BF4, 0x4E2D,
          0x6587);
        T("(C) Chinese (traditional)", "ihqwctvzc91f659drss3x8bo0yb",
          0x4ED6, 0x5011, 0x7232, 0x4EC0, 0x9EBD, 0x4E0D, 0x8AAA, 0x4E2D,
          0x6587);
        T("(D) Czech: Pro<ccaron>prost<ecaron>nemluv<iacute><ccaron>esky",
          "Proprostnemluvesky-uyb24dma41a",
          0x0050, 0x0072, 0x006F, 0x010D, 0x0070, 0x0072, 0x006F, 0x0073,
          0x0074, 0x011B, 0x006E, 0x0065, 0x006D, 0x006C, 0x0075, 0x0076,
          0x00ED, 0x010D, 0x0065, 0x0073, 0x006B, 0x0079,
        );
        T("(E) Hebrew:", "4dbcagdahymbxekheh6e0a7fei0b",
          0x05DC, 0x05DE, 0x05D4, 0x05D4, 0x05DD, 0x05E4, 0x05E9, 0x05D5,
          0x05D8, 0x05DC, 0x05D0, 0x05DE, 0x05D3, 0x05D1, 0x05E8, 0x05D9,
          0x05DD, 0x05E2, 0x05D1, 0x05E8, 0x05D9, 0x05EA);
        T("(F) Hindi (Devanagari):",
          "i1baa7eci9glrd9b2ae1bj0hfcgg6iyaf8o0a1dig0cd",
          0x092F, 0x0939, 0x0932, 0x094B, 0x0917, 0x0939, 0x093F, 0x0928,
          0x094D, 0x0926, 0x0940, 0x0915, 0x094D, 0x092F, 0x094B, 0x0902,
          0x0928, 0x0939, 0x0940, 0x0902, 0x092C, 0x094B, 0x0932, 0x0938,
          0x0915, 0x0924, 0x0947, 0x0939, 0x0948, 0x0902);
        T("(G) Japanese (kanji and hiragana):",
          "n8jok5ay5dzabd5bym9f0cm5685rrjetr6pdxa",
          0x306A, 0x305C, 0x307F, 0x3093, 0x306A, 0x65E5, 0x672C, 0x8A9E,
          0x3092, 0x8A71, 0x3057, 0x3066, 0x304F, 0x308C, 0x306A, 0x3044,
          0x306E, 0x304B);
        T("(H) Korean (Hangul syllables):",
          "989aomsvi5e83db1d2a355cv1e0vak1dwrv93d5xbh15a0dt30a5jpsd879ccm6fea98c",
          0xC138, 0xACC4, 0xC758, 0xBAA8, 0xB4E0, 0xC0AC, 0xB78C, 0xB4E4,
          0xC774, 0xD55C, 0xAD6D, 0xC5B4, 0xB97C, 0xC774, 0xD574, 0xD55C,
          0xB2E4, 0xBA74, 0xC5BC, 0xB9C8, 0xB098, 0xC88B, 0xC744, 0xAE4C);
        T("(I) Russian (Cyrillic):", "b1abfaaepdrnnbgefbadotcwatmq2g4l",
          0x043F, 0x043E, 0x0447, 0x0435, 0x043C, 0x0443, 0x0436, 0x0435,
          0x043E, 0x043D, 0x0438, 0x043D, 0x0435, 0x0433, 0x043E, 0x0432,
          0x043E, 0x0440, 0x044F, 0x0442, 0x043F, 0x043E, 0x0440, 0x0443,
          0x0441, 0x0441, 0x043A, 0x0438);
        T("(J) Spanish: Porqu<eacute>nopuedensimplementehablarenEspa<ntilde>ol",
          "PorqunopuedensimplementehablarenEspaol-fmd56a",
          0x0050, 0x006F, 0x0072, 0x0071, 0x0075, 0x00E9, 0x006E, 0x006F,
          0x0070, 0x0075, 0x0065, 0x0064, 0x0065, 0x006E, 0x0073, 0x0069,
          0x006D, 0x0070, 0x006C, 0x0065, 0x006D, 0x0065, 0x006E, 0x0074,
          0x0065, 0x0068, 0x0061, 0x0062, 0x006C, 0x0061, 0x0072, 0x0065,
          0x006E, 0x0045, 0x0073, 0x0070, 0x0061, 0x00F1, 0x006F, 0x006C);
        T("(K) Vietnamese:", "TisaohkhngthchnitingVit-kjcr8268qyxafd2f1b9g",
          0x0054, 0x1EA1, 0x0069, 0x0073, 0x0061, 0x006F, 0x0068, 0x1ECD,
          0x006B, 0x0068, 0x00F4, 0x006E, 0x0067, 0x0074, 0x0068, 0x1EC3,
          0x0063, 0x0068, 0x1EC9, 0x006E, 0x00F3, 0x0069, 0x0074, 0x0069,
          0x1EBF, 0x006E, 0x0067, 0x0056, 0x0069, 0x1EC7, 0x0074);
        T("(L) 3<nen>B<gumi><kinpachi><sensei>", "3B-ww4c5e180e575a65lsy2b",
          0x0033, 0x5E74, 0x0042, 0x7D44, 0x91D1, 0x516B, 0x5148, 0x751F);
        T("(M) <amuro><namie>-with-SUPER-MONKEYS",
          "-with-SUPER-MONKEYS-pc58ag80a8qai00g7n9n",
          0x5B89, 0x5BA4, 0x5948, 0x7F8E, 0x6075, 0x002D, 0x0077, 0x0069,
          0x0074, 0x0068, 0x002D, 0x0053, 0x0055, 0x0050, 0x0045, 0x0052,
          0x002D, 0x004D, 0x004F, 0x004E, 0x004B, 0x0045, 0x0059, 0x0053);
        T("(N) Hello-Another-Way-<sorezore><no><basho>",
          "Hello-Another-Way--fc4qua05auwb3674vfr0b",
          0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0x002D, 0x0041, 0x006E,
          0x006F, 0x0074, 0x0068, 0x0065, 0x0072, 0x002D, 0x0057, 0x0061,
          0x0079, 0x002D, 0x305D, 0x308C, 0x305E, 0x308C, 0x306E, 0x5834,
          0x6240);
        T("(O) <hitotsu><yane><no><shita>2", "2-u9tlzr9756bt3uc0v",
          0x3072, 0x3068, 0x3064, 0x5C4B, 0x6839, 0x306E, 0x4E0B, 0x0032);
        T("(P) Maji<de>Koi<suru>5<byou><mae>", "MajiKoi5-783gue6qz075azm5e",
          0x004D, 0x0061, 0x006A, 0x0069, 0x3067, 0x004B, 0x006F, 0x0069,
          0x3059, 0x308B, 0x0035, 0x79D2, 0x524D);
        T("(Q) <pafii>de<runba>", "de-jg4avhby1noc0d",
          0x30D1, 0x30D5, 0x30A3, 0x30FC, 0x0064, 0x0065, 0x30EB, 0x30F3,
          0x30D0);
        T("(R) <sono><supiido><de>", "d9juau41awczczp",
          0x305D, 0x306E, 0x30B9, 0x30D4, 0x30FC, 0x30C9, 0x3067);
        T("(S) -> $1.00 <-", "-> $1.00 <--",
          0x002D, 0x003E, 0x0020, 0x0024, 0x0031, 0x002E, 0x0030, 0x0030,
          0x0020, 0x003C, 0x002D);
#undef T
    } Z_TEST_END;

    Z_TEST(sb_add_idna_domain_name, "str: sb_add_idna_domain_name") {
        SB_1k(sb);
        SB_1k(domain);

#define T_OK(_in, _out, _flags, _nb_labels) \
        do {                                                                 \
            int nb_labels = sb_add_idna_domain_name(&sb, _in, strlen(_in),   \
                                                    _flags);                 \
            Z_ASSERT_N(nb_labels);                                           \
            Z_ASSERT_EQ(nb_labels, _nb_labels);                              \
            Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(_out));                  \
            sb_reset(&sb);                                                   \
        } while (0)

#define T_KO(_in, _flags) \
        do {                                                                 \
            Z_ASSERT_NEG(sb_add_idna_domain_name(&sb, _in, strlen(_in),      \
                                                 _flags));                   \
            sb_reset(&sb);                                                   \
        } while (0)

        /* Basic failure cases */
        T_KO("intersec", 0);
        T_KO("intersec..com", 0);
        T_KO("intersec.com.", 0);
        T_KO("intersec-.com", IDNA_USE_STD3_ASCII_RULES);
        T_KO("intersec.-com", IDNA_USE_STD3_ASCII_RULES);
        T_KO("xN--bücher.com", 0);
        T_KO("123456789012345678901234567890123456789012345678901234567890"
             "1234.com", 0);
        T_KO("InSighted!.intersec.com", IDNA_USE_STD3_ASCII_RULES);

        /* Basic success cases */
        T_OK("jObs.InTerseC.coM", "jObs.InTerseC.coM",
             IDNA_USE_STD3_ASCII_RULES, 3);
        T_OK("jObs.InTerseC.coM", "jobs.intersec.com",
             IDNA_USE_STD3_ASCII_RULES | IDNA_ASCII_TOLOWER, 3);
        T_OK("jobs.intersec.com", "jobs.intersec.com",
             IDNA_USE_STD3_ASCII_RULES, 3);
        T_OK("bücher.com", "xn--bcher-kva.com", IDNA_USE_STD3_ASCII_RULES, 2);
        T_OK("xn--bcher-kva.com", "xn--bcher-kva.com",
             IDNA_USE_STD3_ASCII_RULES, 2);
        T_OK("label1.label2。label3．label4｡com",
             "label1.label2.label3.label4.com",
             IDNA_USE_STD3_ASCII_RULES, 5);
        T_OK("intersec-.com", "intersec-.com", 0, 2);
        T_OK("intersec.-com", "intersec.-com", 0, 2);
        T_OK("xn-bücher.com", "xn--xn-bcher-95a.com", 0, 2);
        T_OK("InSighted!.intersec.com", "InSighted!.intersec.com", 0, 3);
#undef T_OK
#undef T_KO

        /* Commonly mapped to nothing */
        sb_reset(&domain);
        sb_adds(&domain, "int");
        sb_adduc(&domain, 0x00ad);
        sb_adds(&domain, "er");
        sb_adduc(&domain, 0xfe01);
        sb_adds(&domain, "sec.com");
        Z_ASSERT_N(sb_add_idna_domain_name(&sb, domain.data, domain.len, 0));
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR("intersec.com"));
        sb_reset(&domain);
        sb_reset(&sb);
        sb_adds(&domain, "büc");
        sb_adduc(&domain, 0x00ad);
        sb_adds(&domain, "he");
        sb_adduc(&domain, 0xfe01);
        sb_adds(&domain, "r.com");
        Z_ASSERT_N(sb_add_idna_domain_name(&sb, domain.data, domain.len, 0));
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR("xn--bcher-kva.com"));

        /* Prohibited output */
        sb_reset(&domain);
        sb_adds(&domain, "inter");
        sb_adduc(&domain, 0x00a0);
        sb_adds(&domain, "sec.com");
        Z_ASSERT_NEG(sb_add_idna_domain_name(&sb, domain.data, domain.len,
                                             0));

        /* Unassigned Code Points */
        sb_reset(&domain);
        sb_adds(&domain, "inter");
        sb_adduc(&domain, 0x0221);
        sb_adds(&domain, "sec.com");
        Z_ASSERT_NEG(sb_add_idna_domain_name(&sb, domain.data, domain.len,
                                             0));
        Z_ASSERT(sb_add_idna_domain_name(&sb, domain.data, domain.len,
                                         IDNA_ALLOW_UNASSIGNED) == 2);
    } Z_TEST_END;

    Z_TEST(sb_add_duration, "str: sb_add_duration") {
        SB_1k(sb);

#define T(d, h, m, s, ms, str)                                               \
    do {                                                                     \
        uint64_t dur;                                                        \
        dur = (d)  * 24 * 60 * 60 * 1000                                     \
            + (h)  *      60 * 60 * 1000                                     \
            + (m)  *           60 * 1000                                     \
            + (s)  *                1000                                     \
            + (ms) *                   1;                                    \
        sb_add_duration_ms(&sb, dur);                                        \
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(str));                       \
        sb_reset(&sb);                                                       \
    } while (0)

        T(2, 3,  5, 6, 900, "2d 3h");
        T(2, 3,  5, 0,   0, "2d 3h");
        T(2, 3, 45, 0,   0, "2d 4h");
        T(2, 4,  0, 0,   0, "2d 4h");

        T(0, 3, 5, 29,   0, "3h 5m");
        T(0, 3, 5, 30,   0, "3h 6m");
        T(0, 3, 5, 31,   0, "3h 6m");
        T(0, 3, 5, 31, 300, "3h 6m");

        T(0, 0, 59, 59, 999, "1h 0m");
        T(0, 1,  0, 29,   0, "1h 0m");

        T(0, 1, 45, 29,  12, "1h 45m");
        T(0, 1, 45, 34,  12, "1h 46m");

        T(0, 0, 45, 34,   0, "45m 34s");
        T(0, 0, 45, 34,  12, "45m 34s");
        T(0, 0, 45, 34, 888, "45m 35s");

        T(0, 0, 0, 8,   0, "8s 0ms");
        T(0, 0, 0, 8, 100, "8s 100ms");
        T(0, 0, 0, 8, 900, "8s 900ms");

        /* corner case */
        T(0, 0, 0, 0, 0, "0s");

        /* test the helper for seconds */
        sb_add_duration_s(&sb, 65);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR("1m 5s"));
        sb_reset(&sb);

        sb_add_duration_s(&sb, 3);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR("3s"));
        sb_reset(&sb);

#undef T
    } Z_TEST_END;

    Z_TEST(sb_add_pkcs7_8_bytes_padding, "") {
#define T(lstr_init, lstr_expected_padded)  \
        Z_HELPER_RUN(z_test_padding(lstr_init, lstr_expected_padded))

        T(LSTR_EMPTY_V,     LSTR(          "\x8\x8\x8\x8\x8\x8\x8\x8"));
        T(LSTR("1"),        LSTR("1"       "\x7\x7\x7\x7\x7\x7\x7"));
        T(LSTR("2"),        LSTR("2"       "\x7\x7\x7\x7\x7\x7\x7"));
        T(LSTR("12"),       LSTR("12"      "\x6\x6\x6\x6\x6\x6"));
        T(LSTR("123"),      LSTR("123"     "\x5\x5\x5\x5\x5"));
        T(LSTR("1234"),     LSTR("1234"    "\x4\x4\x4\x4"));
        T(LSTR("12345"),    LSTR("12345"   "\x3\x3\x3"));
        T(LSTR("123456"),   LSTR("123456"  "\x2\x2"));
        T(LSTR("1234567"),  LSTR("1234567" "\x1"));
        T(LSTR("12345678"), LSTR("12345678""\x8\x8\x8\x8\x8\x8\x8\x8"));

        T(LSTR("12345678123"),
          LSTR("12345678123\x5\x5\x5\x5\x5"));
        T(LSTR("12345678123456781234"),
          LSTR("12345678123456781234\x4\x4\x4\x4"));
        T(LSTR("123456781234567812345678"),
          LSTR("123456781234567812345678\x8\x8\x8\x8\x8\x8\x8\x8"));
        T(LSTR("1234567812345678123456781"),
          LSTR("1234567812345678123456781\x7\x7\x7\x7\x7\x7\x7"));

#undef T

        /* failing lstr_trim_pkcs7_padding cases */
#define TEST_FAIL(_l)  \
        Z_ASSERT_LSTREQUAL(LSTR_NULL_V, lstr_trim_pkcs7_padding(_l))

        TEST_FAIL(LSTR_NULL_V);
        TEST_FAIL(LSTR_EMPTY_V);
        TEST_FAIL(LSTR_INIT_V("a", -1));
        TEST_FAIL(LSTR("1"));
        TEST_FAIL(LSTR("12345678"));
        TEST_FAIL(LSTR("1234567890"));

#undef TEST_FAIL
    } Z_TEST_END;

    Z_TEST(str_span, "str: filtering") {
        SB_1k(sb);

#define T(f, d, c, from, to) do {                                            \
        f(&sb, LSTR(from), d, c);                                            \
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(to));                        \
        sb_reset(&sb);                                                       \
    } while (0)

        T(sb_add_sanitized, &ctype_isdigit, -1, "1a2b3C4D5e6f7", "1234567");
        T(sb_add_sanitized, &ctype_isdigit, '_', "1a2b3C4D5e6f7",
          "1_2_3_4_5_6_7");
        T(sb_add_sanitized, &ctype_islower, -1, "1a2b3C4D5e6f7", "abef");
        T(sb_add_sanitized, &ctype_islower, '_', "1a2b3C4D5e6f7",
          "_a_b_e_f_");
        T(sb_add_sanitized, &ctype_isupper, -1, "1a2b3C4D5e6f7", "CD");
        T(sb_add_sanitized, &ctype_isupper, '_', "1a2b3C4D5e6f7", "_C_D_");

        T(sb_add_sanitized_out, &ctype_isdigit, -1, "1a2b3C4D5e6f7",
          "abCDef");
        T(sb_add_sanitized_out, &ctype_isdigit, '_', "1a2b3C4D5e6f7",
          "_a_b_C_D_e_f_");
        T(sb_add_sanitized_out, &ctype_islower, -1, "1a2b3C4D5e6f7",
          "123C4D567");
        T(sb_add_sanitized_out, &ctype_islower, '_', "1a2b3C4D5e6f7",
          "1_2_3C4D5_6_7");
        T(sb_add_sanitized_out, &ctype_isupper, -1, "1a2b3C4D5e6f7",
          "1a2b345e6f7");
        T(sb_add_sanitized_out, &ctype_isupper, '_', "1a2b3C4D5e6f7",
          "1a2b3_4_5e6f7");

#undef T
    } Z_TEST_END;

    Z_TEST(lstr_startswithc, "str: starts with character") {
        Z_ASSERT(lstr_startswithc(LSTR("1234"), '1'));
        Z_ASSERT(!lstr_startswithc(LSTR("1234"), '2'));
        Z_ASSERT(lstr_startswithc(LSTR("a"), 'a'));
        Z_ASSERT(!lstr_startswithc(LSTR_NULL_V, '2'));
        Z_ASSERT(!lstr_startswithc(LSTR_EMPTY_V, '2'));
    } Z_TEST_END;

    Z_TEST(lstr_endswithc, "str: ends with character") {
        Z_ASSERT(!lstr_endswithc(LSTR("1234"), '1'));
        Z_ASSERT(lstr_endswithc(LSTR("a"), 'a'));
        Z_ASSERT(lstr_endswithc(LSTR("1234"), '4'));
        Z_ASSERT(!lstr_endswithc(LSTR_NULL_V, '2'));
        Z_ASSERT(!lstr_endswithc(LSTR_EMPTY_V, '2'));
    } Z_TEST_END;

    Z_TEST(lstr_ascii_reverse, "str: reverse a lstr") {
        t_scope;
#define T(f, t) do {                                                         \
        lstr_t a = t_lstr_dup(f);                                            \
        lstr_t b = t_lstr_dup_ascii_reversed(a);                             \
        lstr_ascii_reverse(&a);                                              \
        Z_ASSERT_LSTREQUAL(a, (t));                                          \
        Z_ASSERT_LSTREQUAL(b, (t));                                          \
    } while (0)
        T(LSTR_NULL_V, LSTR_NULL_V);
        T(LSTR_EMPTY_V, LSTR_EMPTY_V);
        T(LSTR("a"), LSTR("a"));
        T(LSTR("ab"), LSTR("ba"));
        T(LSTR("abc"), LSTR("cba"));
        T(LSTR("abcd"), LSTR("dcba"));
#undef T
    } Z_TEST_END;

    Z_TEST(lstr_utf8_reverse, "str: reverse a lstr") {
        t_scope;
#define T(f, t) do {                                                         \
        lstr_t a = t_lstr_dup_utf8_reversed(f);                              \
        Z_ASSERT_LSTREQUAL(a, (t));                                          \
    } while (0)
        T(LSTR_NULL_V, LSTR_NULL_V);
        T(LSTR_EMPTY_V, LSTR_EMPTY_V);
        T(LSTR("a"), LSTR("a"));
        T(LSTR("ab"), LSTR("ba"));
        T(LSTR("abc"), LSTR("cba"));
        T(LSTR("abcd"), LSTR("dcba"));
        T(LSTR("aé"), LSTR("éa"));
        T(LSTR("é"), LSTR("é"));
        T(LSTR("éa"), LSTR("aé"));
        T(LSTR("béa"), LSTR("aéb"));
#undef T
    } Z_TEST_END;

    Z_TEST(lstr_dl_distance, "str: Damerau–Levenshtein distance") {
#define T(s1, s2, exp) do {                                                  \
        Z_ASSERT_EQ(lstr_dlevenshtein(LSTR(s1), LSTR(s2), exp), exp);        \
        Z_ASSERT_EQ(lstr_dlevenshtein(LSTR(s2), LSTR(s1), exp), exp);        \
        Z_ASSERT_EQ(lstr_dlevenshtein(LSTR(s1), LSTR(s2), -1), exp);         \
        if (exp > 0) {                                                       \
            Z_ASSERT_NEG(lstr_dlevenshtein(LSTR(s1), LSTR(s2), exp - 1));    \
        }                                                                    \
    } while (0)

        T("",         "",         0);
        T("abcd",     "abcd",     0);
        T("",         "abcd",     4);
        T("toto",     "totototo", 4);
        T("ba",       "abc",      2);
        T("fee",      "deed",     2);
        T("hurqbohp", "qkhoz",    6);
#undef T
    } Z_TEST_END;

    Z_TEST(ps_split, "str-stream: ps_split") {
        qv_t(lstr) arr;

        qv_init(&arr);

#define T(str1, str2, str3, sep, seps) \
        TST_MAIN(str1, str1, str2, str3, sep, seps, 0)

#define T_SKIP(str_main, str1, str2, str3, seps) \
        TST_MAIN(str_main, str1, str2, str3, "\0", seps, PS_SPLIT_SKIP_EMPTY)

#define TST_MAIN(str_main, str1, str2, str3, sep, seps, flags)               \
        ({  pstream_t ps;                                                    \
            ctype_desc_t desc;                                               \
                                                                             \
            if (flags & PS_SPLIT_SKIP_EMPTY) {                               \
                ps = ps_initstr(str_main);                                   \
            } else {                                                         \
                ps = ps_initstr(str1 sep str2 sep str3);                     \
            }                                                                \
            ctype_desc_build(&desc, seps);                                   \
            qv_deep_clear(&arr, lstr_wipe);                            \
            ps_split(ps, &desc, flags, &arr);                                \
            Z_ASSERT_EQ(arr.len, 3);                                         \
            Z_ASSERT_LSTREQUAL(arr.tab[0], LSTR(str1));                      \
            Z_ASSERT_LSTREQUAL(arr.tab[1], LSTR(str2));                      \
            Z_ASSERT_LSTREQUAL(arr.tab[2], LSTR(str3)); })

        T("123", "abc", "!%*", "/", "/");
        T("123", "abc", "!%*", " ", " ");
        T("123", "abc", "!%*", "$", "$");
        T("   ", ":::", "!!!", ",", ",");

        T("secret1", "secret2", "secret3", " ", " ,;");
        T("secret1", "secret2", "secret3", ",", " ,;");
        T("secret1", "secret2", "secret3", ";", " ,;");

        qv_deep_wipe(&arr, lstr_wipe);

        T_SKIP("//123//abc/!%*", "123", "abc", "!%*", "/");
        T_SKIP("$123$$$abc$!%*", "123", "abc", "!%*", "$");
        T_SKIP(",   ,:::,!!!,,", "   ", ":::", "!!!", ",");

        T_SKIP(" secret1 secret2   secret3", "secret1",
               "secret2" , "secret3", " ,;");
        T_SKIP(",secret1;secret2,,secret3,;,,", "secret1", "secret2",
               "secret3", " ,;");
        T_SKIP("secret1;;,,secret2; ;secret3;;", "secret1", "secret2",
               "secret3", " ,;");

        qv_deep_wipe(&arr, lstr_wipe);
#undef T
#undef TST_MAIN
#undef T_SKIP
    } Z_TEST_END;

    Z_TEST(t_ps_split_escaped, "str-stream: t_ps_split_escaped") {
        t_scope;
        qv_t(lstr) arr;

        qv_init(&arr);

#define T(str_main, str1, str2, str3, seps, esc)                             \
        TST_MAIN(str_main, str1, str2, str3, seps, esc, 0)

#define T_SKIP(str_main, str1, str2, str3, seps, esc)                        \
        TST_MAIN(str_main, str1, str2, str3, seps, esc, PS_SPLIT_SKIP_EMPTY)

#define TST_EMPTY(str_main, str, seps, esc, flags)                           \
        ({  pstream_t ps;                                                    \
            ctype_desc_t sep_desc;                                           \
            const char esc_char = esc;                                       \
                                                                             \
            ps = ps_initstr(str_main);                                       \
            ctype_desc_build(&sep_desc, seps);                               \
            qv_deep_clear(&arr, lstr_wipe);                                  \
            t_ps_split_escaped(ps, &sep_desc, esc_char, flags, &arr);        \
            if (flags & PS_SPLIT_SKIP_EMPTY) {                               \
                Z_ASSERT_EQ(arr.len, 0);                                     \
            } else {                                                         \
                Z_ASSERT_EQ(arr.len, 1);                                     \
                Z_ASSERT_LSTREQUAL(arr.tab[0], LSTR(str));                   \
            }                                                                \
         })

#define TST_MAIN(str_main, str1, str2, str3, seps, esc, flags)               \
        ({  pstream_t ps;                                                    \
            ctype_desc_t sep_desc;                                           \
            const char esc_char = esc;                                       \
                                                                             \
            ps = ps_initstr(str_main);                                       \
            ctype_desc_build(&sep_desc, seps);                               \
            qv_deep_clear(&arr, lstr_wipe);                                  \
            t_ps_split_escaped(ps, &sep_desc, esc_char, flags, &arr);        \
            Z_ASSERT_EQ(arr.len, 3);                                         \
            Z_ASSERT_LSTREQUAL(arr.tab[0], LSTR(str1));                      \
            Z_ASSERT_LSTREQUAL(arr.tab[1], LSTR(str2));                      \
            Z_ASSERT_LSTREQUAL(arr.tab[2], LSTR(str3));                      \
        })

        TST_EMPTY("", "", "123 ", '\\', 0);
        T("123/abc !%*", "123", "abc", "!%*", " /", '\0');
        T("/123;abc", "", "123", "abc", "/;", '\0');
        T("abc/123;", "abc", "123", "", "/;", '\0');

        T_SKIP("//123//abc/!%*", "123", "abc", "!%*", "/", '\0');
        T_SKIP("$123$$$abc$!%*", "123", "abc", "!%*", "$", '\0');
        T_SKIP(",   ,:::,!!!,,", "   ", ":::", "!!!", ",", '\0');
        T_SKIP(",secret1;secret2, ,secret3,;,,", "secret1", "secret2",
               "secret3", " ,;", '\0');

        /* with escape characters */
        TST_EMPTY("", "", "123 ", '\\', PS_SPLIT_SKIP_EMPTY);
        TST_EMPTY("///", "", "123/", '\\', PS_SPLIT_SKIP_EMPTY);
        T("12\\3\\%abc%%abc", "12\\3%abc", "", "abc", "%", '\\');
        T("123&%abc&!def!ghi;ab", "123%abc!def", "ghi", "ab", "%;!", '&');
        T("&123&%&abc&!def!ghi;ab", "&123%&abc!def", "ghi", "ab",
          "%;!", '&');
        T("1\\%\\%\\%\\\\a%b%c", "1%%%\\a", "b", "c", "%", '\\');
        T("%\\%%", "", "%", "", "%", '\\');
        T("\\%%%\\%", "%", "", "%", "%", '\\');

        T_SKIP("//123\\/abc/abc/!%*", "123/abc", "abc", "!%*", "/", '\\');
        T_SKIP("//1\\/\\;abc/;;;;;a/!%*", "1/;abc", "a", "!%*", "/;", '\\');
        T_SKIP("\\1\\/\\;a/;;;;;a/!%*", "\\1/;a", "a", "!%*", "/;", '\\');
        T_SKIP("%%\\%%%%%a%\\%%%%%", "%", "a", "%", "%", '\\');
        T_SKIP("\\%%%%%a%%%%%\\%", "%", "a", "%", "%", '\\');
        T_SKIP("\\%%%a%%%%%%%a\\", "%", "a", "a\\", "%", '\\');
        qv_deep_wipe(&arr, lstr_wipe);

#undef T
#undef TST_EMPTY
#undef TST_MAIN
#undef T_SKIP
    } Z_TEST_END;

    Z_TEST(t_ps_get_http_var, "str: t_ps_get_http_var") {
        t_scope;
        pstream_t ps;
        lstr_t    key, value;

#define TST_INVALID(_text)  \
        do {                                                                 \
            ps = ps_initstr(_text);                                          \
            Z_ASSERT_NEG(t_ps_get_http_var(&ps, &key, &value));              \
        } while (0)

        TST_INVALID("");
        TST_INVALID("key");
        TST_INVALID("=value");
        TST_INVALID("=&");
#undef TST_INVALID

        ps = ps_initstr("cid1%3d1%26cid2=2&cid3=3&cid4=");
        Z_ASSERT_N(t_ps_get_http_var(&ps, &key, &value));
        Z_ASSERT_LSTREQUAL(key,   LSTR("cid1=1&cid2"));
        Z_ASSERT_LSTREQUAL(value, LSTR("2"));
        Z_ASSERT_N(t_ps_get_http_var(&ps, &key, &value));
        Z_ASSERT_LSTREQUAL(key,   LSTR("cid3"));
        Z_ASSERT_LSTREQUAL(value, LSTR("3"));
        Z_ASSERT_N(t_ps_get_http_var(&ps, &key, &value));
        Z_ASSERT_LSTREQUAL(key,   LSTR("cid4"));
        Z_ASSERT_LSTREQUAL(value, LSTR(""));
        Z_ASSERT(ps_done(&ps));
        Z_ASSERT_NEG(t_ps_get_http_var(&ps, &key, &value));
    } Z_TEST_END;

    Z_TEST(sb_add_int_fmt, "str: sb_add_int_fmt") {
#define T(val, thousand_sep, res) \
    ({  SB_1k(sb);                                                           \
                                                                             \
        sb_add_int_fmt(&sb, val, thousand_sep);                              \
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(res));                       \
    })

        T(        0, ',', "0");
        T(        1, ',', "1");
        T(       -1, ',', "-1");
        T(       12, ',', "12");
        T(      123, ',', "123");
        T(     1234, ',', "1,234");
        T(INT64_MIN, ',', "-9,223,372,036,854,775,808");
        T(INT64_MAX, ',', "9,223,372,036,854,775,807");
        T(     1234, ' ', "1 234");
        T(     1234,  -1, "1234");
#undef T
    } Z_TEST_END;

    Z_TEST(sb_add_uint_fmt, "str: sb_add_uint_fmt") {
#define T(val, thousand_sep, res) \
    ({  SB_1k(sb);                                                           \
                                                                             \
        sb_add_uint_fmt(&sb, val, thousand_sep);                             \
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR(res));                       \
    })

        T(         0, ',', "0");
        T(         1, ',', "1");
        T(        12, ',', "12");
        T(       123, ',', "123");
        T(      1234, ',', "1,234");
        T(UINT64_MAX, ',', "18,446,744,073,709,551,615");
        T(      1234, ' ', "1 234");
        T(      1234,  -1, "1234");
#undef T
    } Z_TEST_END;

    Z_TEST(sb_add_csvescape, "") {
        SB_1k(sb);

#define CHECK(_str, _sep, _expected)  \
        do {                                                                 \
            sb_adds_csvescape(&sb, _sep, _str);                              \
            Z_ASSERT_STREQUAL(_expected, sb.data);                           \
            sb_reset(&sb);                                                   \
        } while (0)

        CHECK("toto", ';', "toto");
        CHECK("toto;tata", ';', "\"toto;tata\"");
        CHECK("toto,tata", ',', "\"toto,tata\"");
        CHECK("toto|tata", '|', "\"toto|tata\"");
        CHECK("toto\"tata", ';', "\"toto\"\"tata\"");
        CHECK("toto\n", ';', "\"toto\n\"");
        CHECK("toto\"", ';', "\"toto\"\"\"");
        CHECK("toto\ntata", ';', "\"toto\ntata\"");
        CHECK("toto\n\"tata", ';', "\"toto\n\"\"tata\"");
        CHECK("toto\"\ntata", ';', "\"toto\"\"\ntata\"");
        CHECK("", ';', "");
        CHECK("\"", ';', "\"\"\"\"");
    } Z_TEST_END;

    Z_TEST(sb_splice_lstr, "") {
        SB_1k(sb);

        sb_sets(&sb, "123");
        sb_splice_lstr(&sb, 1, 1, LSTR("two"));
        Z_ASSERT_LSTREQUAL(LSTR("1two3"), LSTR_SB_V(&sb));
    } Z_TEST_END;

    Z_TEST(sb_loop_safe,
           "Test using SB() in a loop does not trigger a stack overflow")
    {
        for (int i = 0; i < 1000000; i++) {
            SB(sb, 32 << 10);

            sb_sets(&sb, "pouet");
            Z_ASSERT_LSTREQUAL(LSTR("pouet"), LSTR_SB_V(&sb));
        }
    } Z_TEST_END;

    Z_TEST(ps_skip_afterlastchr, "") {
        pstream_t ps = ps_initstr("test_1_2");
        pstream_t ps2 = ps_initstr("test1.02");
        pstream_t ps3 = ps_initstr("test_2");

        Z_ASSERT_N(ps_skip_afterlastchr(&ps, '_'));
        Z_ASSERT(ps_len(&ps) == 1);
        Z_ASSERT(ps_strequal(&ps, "2"));

        Z_ASSERT_NEG(ps_skip_afterlastchr(&ps2, '_'));
        Z_ASSERT(ps_len(&ps2) == strlen("test1.02"));
        Z_ASSERT(ps_strequal(&ps2, "test1.02"));
        Z_ASSERT_N(ps_skip_afterlastchr(&ps2, '.'));
        Z_ASSERT(ps_len(&ps2) == 2);
        Z_ASSERT(ps_strequal(&ps2, "02"));

        Z_ASSERT_N(ps_skip_afterlastchr(&ps3, '_'));
        Z_ASSERT(ps_len(&ps3) == 1);
        Z_ASSERT(ps_strequal(&ps3, "2"));
    } Z_TEST_END;

    Z_TEST(ps_clip_atlastchr, "") {
        pstream_t ps = ps_initstr("test_1_2");
        pstream_t ps2 = ps_initstr("test1.02");
        pstream_t ps3 = ps_initstr("test_2");

        Z_ASSERT_N(ps_clip_atlastchr(&ps, '_'));
        Z_ASSERT(ps_len(&ps) == 6);
        Z_ASSERT(ps_strequal(&ps, "test_1"));

        Z_ASSERT_NEG(ps_clip_atlastchr(&ps2, '_'));
        Z_ASSERT(ps_len(&ps2) == strlen("test1.02"));
        Z_ASSERT(ps_strequal(&ps2, "test1.02"));
        Z_ASSERT_N(ps_clip_atlastchr(&ps2, '.'));
        Z_ASSERT(ps_len(&ps2) == 5);
        Z_ASSERT(ps_strequal(&ps2, "test1"));

        Z_ASSERT_N(ps_clip_atlastchr(&ps3, '_'));
        Z_ASSERT(ps_len(&ps3) == 4);
        Z_ASSERT(ps_strequal(&ps3, "test"));
    } Z_TEST_END;

    Z_TEST(ps_clip_afterlastchr, "") {
        pstream_t ps = ps_initstr("test_1_2");
        pstream_t ps2 = ps_initstr("test1.02");
        pstream_t ps3 = ps_initstr("test_2");

        Z_ASSERT_N(ps_clip_afterlastchr(&ps, '_'));
        Z_ASSERT(ps_len(&ps) == 7);
        Z_ASSERT(ps_strequal(&ps, "test_1_"));

        Z_ASSERT_NEG(ps_clip_afterlastchr(&ps2, '_'));
        Z_ASSERT(ps_len(&ps2) == strlen("test1.02"));
        Z_ASSERT(ps_strequal(&ps2, "test1.02"));
        Z_ASSERT_N(ps_clip_afterlastchr(&ps2, '.'));
        Z_ASSERT(ps_len(&ps2) == 6);
        Z_ASSERT(ps_strequal(&ps2, "test1."));

        Z_ASSERT_N(ps_clip_afterlastchr(&ps3, '_'));
        Z_ASSERT(ps_len(&ps3) == 5);
        Z_ASSERT(ps_strequal(&ps3, "test_"));
    } Z_TEST_END;

    Z_TEST(ps_skip_upto_str, "") {
        const char *str = "foo bar baz";
        pstream_t ps = ps_initstr(str);

        Z_ASSERT_NEG(ps_skip_upto_str(&ps, "toto"));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));

        Z_ASSERT_N(ps_skip_upto_str(&ps, ""));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));

        Z_ASSERT_N(ps_skip_upto_str(&ps, "bar"));
        Z_ASSERT(ps_len(&ps) == 7);
        Z_ASSERT(ps_strequal(&ps, "bar baz"));
    } Z_TEST_END;

    Z_TEST(ps_skip_after_str, "") {
        const char *str = "foo bar baz";
        pstream_t ps = ps_initstr(str);

        Z_ASSERT_NEG(ps_skip_after_str(&ps, "toto"));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));

        Z_ASSERT_N(ps_skip_after_str(&ps, ""));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));

        Z_ASSERT_N(ps_skip_after_str(&ps, "bar"));
        Z_ASSERT(ps_len(&ps) == 4);
        Z_ASSERT(ps_strequal(&ps, " baz"));
    } Z_TEST_END;

    Z_TEST(ps_get_ps_upto_str, "") {
        const char *str = "foo bar baz";
        pstream_t ps = ps_initstr(str);
        pstream_t extract;

        p_clear(&extract, 1);
        Z_ASSERT_NEG(ps_get_ps_upto_str(&ps, "toto", &extract));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));
        Z_ASSERT(ps_len(&extract) == 0);

        Z_ASSERT_N(ps_get_ps_upto_str(&ps, "", &extract));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));
        Z_ASSERT(ps_len(&extract) == 0);

        Z_ASSERT_N(ps_get_ps_upto_str(&ps, "bar", &extract));
        Z_ASSERT(ps_len(&ps) ==  7);
        Z_ASSERT(ps_strequal(&ps, "bar baz"));
        Z_ASSERT(ps_len(&extract) == 4);
        Z_ASSERT(ps_strequal(&extract, "foo "));
    } Z_TEST_END;

    Z_TEST(ps_get_ps_upto_str_and_skip, "") {
        const char *str = "foo bar baz";
        pstream_t ps = ps_initstr(str);
        pstream_t extract;

        p_clear(&extract, 1);
        Z_ASSERT_NEG(ps_get_ps_upto_str_and_skip(&ps, "toto", &extract));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));
        Z_ASSERT(ps_len(&extract) == 0);

        Z_ASSERT_N(ps_get_ps_upto_str_and_skip(&ps, "", &extract));
        Z_ASSERT(ps_len(&ps) ==  strlen(str));
        Z_ASSERT(ps_strequal(&ps, str));
        Z_ASSERT(ps_len(&extract) == 0);

        Z_ASSERT_N(ps_get_ps_upto_str_and_skip(&ps, "bar", &extract));
        Z_ASSERT(ps_len(&ps) ==  4);
        Z_ASSERT(ps_strequal(&ps, " baz"));
        Z_ASSERT(ps_len(&extract) == 4);
        Z_ASSERT(ps_strequal(&extract, "foo "));
    } Z_TEST_END;

    Z_TEST(ps_endswith, "") {
        pstream_t ps1 = ps_initstr("toto");
        pstream_t ps2 = ps_initstr("42toto");
        pstream_t ps3 = ps_initstr("toto42");
        pstream_t ps4 = ps_initstr("");

        Z_ASSERT(ps_endswithstr(&ps1, "toto"));
        Z_ASSERT(ps_endswithstr(&ps2, "toto"));
        Z_ASSERT(!ps_endswithstr(&ps3, "toto"));
        Z_ASSERT(!ps_endswithstr(&ps4, "toto"));
    } Z_TEST_END;

    Z_TEST(lstr_ascii_icmp, "str: lstr_ascii_icmp") {
#define T(_str1, _str2, _expected)                                           \
        Z_ASSERT(lstr_ascii_icmp(LSTR_IMMED_V(_str1),  LSTR_IMMED_V(_str2))  \
                 _expected)

        T("a",    "b",     <  0);
        T("b",    "a",     >  0);
        T("a",    "a",     == 0);
        T("A",    "a",     == 0);
        T("aaa",  "b",     <  0);
        T("bbb",  "a",     >  0);
        T("aaa",  "aa",    >  0);
        T("aaa",  "AA",    >  0);
        T("AbCd", "aBcD",  == 0);
        T("AbCd", "aBcDe", <  0);
        T("faaa", "FAAB",  <  0);
        T("FAAA", "faab",  <  0);
        T("faaa", "FAAA",  == 0);
        T("faab", "faaba", <  0);
        T("faab", "faaab", >  0);
#undef T
    } Z_TEST_END;

    Z_TEST(lstr_to_int, "str: lstr_to_int and friends") {
        t_scope;
        int      i;
        uint32_t u32;
        int64_t  i64;
        uint64_t u64;

#define T_OK(_str, _exp)  \
        do {                                                                 \
            Z_ASSERT_N(lstr_to_int(LSTR(_str), &i));                         \
            Z_ASSERT_EQ(i, _exp);                                            \
            Z_ASSERT_N(lstr_to_uint(LSTR(_str), &u32));                      \
            Z_ASSERT_EQ(u32, (uint32_t)_exp);                                \
            Z_ASSERT_N(lstr_to_int64(LSTR(_str), &i64));                     \
            Z_ASSERT_EQ(i64, _exp);                                          \
            Z_ASSERT_N(lstr_to_uint64(LSTR(_str), &u64));                    \
            Z_ASSERT_EQ(u64, (uint64_t)_exp);                                \
        } while (0)

        T_OK("0",        0);
        T_OK("1234",     1234);
        T_OK("  1234  ", 1234);
#undef T_OK

        Z_ASSERT_N(lstr_to_uint(t_lstr_fmt("%u", UINT32_MAX), &u32));
        Z_ASSERT_EQ(u32, UINT32_MAX);

#define T_KO(_str)  \
        do {                                                                 \
            Z_ASSERT_NEG(lstr_to_int(LSTR(_str), &i));                       \
            Z_ASSERT_NEG(lstr_to_uint(LSTR(_str), &u32));                    \
            Z_ASSERT_NEG(lstr_to_int64(LSTR(_str), &i64));                   \
            Z_ASSERT_NEG(lstr_to_uint64(LSTR(_str), &u64));                  \
        } while (0)

        T_KO("");
        T_KO("   ");
        T_KO("abcd");
        T_KO("  12 12 ");
        T_KO("  12abcd");
        T_KO("12.12");
#undef T_KO

        errno = 0;
        Z_ASSERT_NEG(lstr_to_uint(LSTR(" -123"), &u32));
        Z_ASSERT_EQ(errno, ERANGE);
        Z_ASSERT_NEG(lstr_to_uint(t_lstr_fmt("%jd", (uint64_t)UINT32_MAX + 1),
                                  &u32));
        Z_ASSERT_EQ(errno, ERANGE);

        errno = 0;
        Z_ASSERT_NEG(lstr_to_uint64(LSTR(" -123"), &u64));
        Z_ASSERT_EQ(errno, ERANGE);
    } Z_TEST_END;

    Z_TEST(lstr_to_double, "str: lstr_to_double") {
        double d;

#define T_OK(_str, _exp)  \
        do {                                                                 \
            Z_ASSERT_N(lstr_to_double(LSTR(_str), &d));                      \
            Z_ASSERT_EQ(d, _exp);                                            \
        } while (0)

        T_OK("0",        0);
        T_OK("1234",     1234);
        T_OK("  1234  ", 1234);
        T_OK("-1.33e12", -1.33e12);
        T_OK("INF", INFINITY);
        T_OK("INFINITY", INFINITY);
#undef T_OK

#define T_KO(_str)  \
        do {                                                                 \
            Z_ASSERT_NEG(lstr_to_double(LSTR(_str), &d));                    \
        } while (0)

        T_KO("abcd");
        T_KO("  12 12 ");
        T_KO("  12abcd");
#undef T_KO
    } Z_TEST_END;

    Z_TEST(str_match_ctype, "str: strings match the ctype description") {
        struct {
            lstr_t              s;
            const ctype_desc_t *d;
            bool                expected;
        } t[] = {
#define T(_str, _ctype, _expected)                                           \
            {.s = LSTR_IMMED(_str), .d = _ctype, .expected = _expected}

            T("0123456789",       &ctype_isdigit,    true),
            T("abcde",            &ctype_islower,    true),
            T("ABCDE",            &ctype_isupper,    true),
            T(" \n",              &ctype_isspace,    true),
            T("0123456789ABCDEF", &ctype_ishexdigit, true),
            T("0123456789abcdef", &ctype_ishexdigit, true),

            T("abcdEF",           &ctype_isdigit,    false),
            T("ABC",              &ctype_islower,    false),
            T("abcABC",           &ctype_islower,    false),
            T("abc132",           &ctype_islower,    false),
            T("abc",              &ctype_isupper,    false),
            T("aBCDE",            &ctype_isupper,    false),

#undef T
        };

        for (int i = 0; i < countof(t); i++) {
            Z_ASSERT_EQ(lstr_match_ctype(t[i].s, t[i].d), t[i].expected);
        }
    } Z_TEST_END;

    Z_TEST(lstr_macros, "lstr: macros") {
        uint16_t data[] = { 11, 22, 33 };
        lstr_t data_ref, data_s, data_c;

        data_ref = LSTR_INIT_V((const char *)data, sizeof(data));
        data_s = LSTR_DATA_V(data, sizeof(data));
        data_c = LSTR_CARRAY_V(data);

        Z_ASSERT_LSTREQUAL(data_s, data_ref);
        Z_ASSERT_LSTREQUAL(data_c, data_ref);
    } Z_TEST_END;

    Z_TEST(ps_has_char, "ps: ps_has_char_in_ctype") {
        pstream_t p;

        p = ps_initstr("aBcdEfGhij");
        Z_ASSERT(!ps_has_char_in_ctype(&p, &ctype_isdigit));
        Z_ASSERT( ps_has_char_in_ctype(&p, &ctype_isalpha));

        p = ps_initstr("abcdef1hij");
        Z_ASSERT(ps_has_char_in_ctype(&p, &ctype_isdigit));
        Z_ASSERT(ps_has_char_in_ctype(&p, &ctype_isalpha));

        p = ps_initstr("ABCDEFJHIJ8");
        Z_ASSERT(ps_has_char_in_ctype(&p, &ctype_isdigit));
        Z_ASSERT(ps_has_char_in_ctype(&p, &ctype_isalpha));

        p = ps_initstr("9191959485889");
        Z_ASSERT( ps_has_char_in_ctype(&p, &ctype_isdigit));
        Z_ASSERT(!ps_has_char_in_ctype(&p, &ctype_isalpha));
    } Z_TEST_END;

    Z_TEST(sb_add_expandenv, "sb: sb_add_expandenv") {
        const char *var = getenv("USER");
        SB_1k(data);
        SB_1k(expected);

#define T(str, res, ...)  do {                                               \
        sb_reset(&data);                                                     \
        sb_adds_expandenv(&data, str);                                       \
                                                                             \
        sb_setf(&expected, res, ##__VA_ARGS__);                              \
        Z_ASSERT_STREQUAL(data.data, expected.data);                         \
    } while (0)

        T("toto", "toto");
        T("", "");
        T("$USER", "%s", var);
        T("${USER}", "%s", var);
        T("$USER ", "%s ", var);
        T("$USER$USER", "%s%s", var, var);
        T("/$USER/", "/%s/", var);
        T("Hello ${USER}!", "Hello %s!", var);
        T("\\$", "$");
        T("\\\\$USER", "\\%s", var);

#undef T
#define T_ERR(str)  Z_ASSERT_NEG(sb_adds_expandenv(&data, str))

        T_ERR("${USER");
        T_ERR("$$");

#undef T_ERR

    } Z_TEST_END;

    Z_TEST(lstr_is_like, "Test lstr_is_like") {
#define MATCH(str, pattern)                                                  \
        Z_ASSERT(lstr_utf8_is_ilike(LSTR(str), LSTR(pattern)))
#define NOMATCH(str, pattern)                                                \
        Z_ASSERT(!lstr_utf8_is_ilike(LSTR(str), LSTR(pattern)))

        /* cases with no special characters */
        MATCH("", "");
        MATCH("a", "a");
        NOMATCH("", "a");
        NOMATCH("a", "");
        NOMATCH("a", "b");

        /* matching is case insensitive */
        MATCH("a", "A");
        MATCH("AaAa", "aaAA");

        /* '_' pattern */
        MATCH("a", "_");
        MATCH("aa", "__");

        NOMATCH("_", "a");
        NOMATCH("aa", "_");
        NOMATCH("", "_");
        NOMATCH("a", "__");

        /* '%' pattern */
        MATCH("a", "%");
        MATCH("a", "%%%");
        MATCH("aaa", "%");

        NOMATCH("%", "a");
        NOMATCH("aa", "_");
        NOMATCH("a", "__");

        /* mix and escape */
        MATCH("a", "%_%");
        MATCH("%_%", "%_%");
        MATCH("jose_mourinho", "%e\\_m%");
        MATCH("%a", "\\%_");
        MATCH("a_", "a%\\_");

        NOMATCH("abc", "\\_bc");
        NOMATCH("abc", "a\\_c");
        NOMATCH("abc", "ab\\_");
        NOMATCH("abc", "\\%c");
        NOMATCH("abc", "a\\%c");
        NOMATCH("abc", "a\\%");

        /* collation stuff */
        MATCH("œ", "_");
        MATCH("œ", "oe");
        MATCH("oe", "œ");
        NOMATCH("œ", "o_");
        NOMATCH("œ", "_e");
        NOMATCH("œ", "o%");
        NOMATCH("œ", "%e");

        MATCH("é", "e");
        MATCH("e", "é");
        MATCH("éœ", "%oe");
        MATCH("éœ", "e%oe");
        MATCH("eœ", "é%oé");
        NOMATCH("éœ", "%e");

#undef NOMATCH
#undef MATCH
    } Z_TEST_END;

    Z_TEST(ps_get_str, "ps: ps_gets") {
        lstr_t lstr_zero_terminated = LSTR_IMMED("foo\0baar\0");
        pstream_t ps_zero_terminated = ps_initlstr(&lstr_zero_terminated);
        lstr_t lstr_not_zero_term = LSTR_IMMED("foobar");
        pstream_t ps_not_zero_term = ps_initlstr(&lstr_not_zero_term);
        int len;

        Z_ASSERT_STREQUAL(ps_gets(&ps_zero_terminated, &len), "foo");
        Z_ASSERT_EQ(len, 3);
        Z_ASSERT_STREQUAL(ps_gets(&ps_zero_terminated, &len), "baar");
        Z_ASSERT_EQ(len, 4);
        Z_ASSERT(ps_done(&ps_zero_terminated));
        Z_ASSERT_NULL(ps_gets(&ps_zero_terminated, NULL));

        Z_ASSERT_NULL(ps_gets(&ps_not_zero_term, NULL));
    } Z_TEST_END;

    Z_TEST(ps_get_lstr, "ps: ps_get_lstr") {
        lstr_t lstr_zero_terminated = LSTR_IMMED("foo\0baar\0");
        pstream_t ps_zero_terminated = ps_initlstr(&lstr_zero_terminated);
        lstr_t lstr_not_zero_term = LSTR_IMMED("foobar");
        pstream_t ps_not_zero_term = ps_initlstr(&lstr_not_zero_term);

        Z_ASSERT_LSTREQUAL(ps_get_lstr(&ps_zero_terminated),
                           LSTR_IMMED_V("foo"));
        Z_ASSERT_LSTREQUAL(ps_get_lstr(&ps_zero_terminated),
                           LSTR_IMMED_V("baar"));
        Z_ASSERT(ps_done(&ps_zero_terminated));
        Z_ASSERT_LSTREQUAL(ps_get_lstr(&ps_zero_terminated), LSTR_NULL_V);

        Z_ASSERT_LSTREQUAL(ps_get_lstr(&ps_not_zero_term), LSTR_NULL_V);
    } Z_TEST_END;

    Z_TEST(base64, "base64/base64url encoding decoding") {
        lstr_t data = LSTR_IMMED("\xD9\x87\xE3\xFE\x48\x7E\x25\x81\xFB");
        SB_1k(data_buf);
        SB_1k(data_decoded);

        sb_add_lstr_b64(&data_buf, data, -1);
        Z_ASSERT_STREQUAL(data_buf.data, "2Yfj/kh+JYH7");
        Z_ASSERT_N(sb_add_lstr_unb64(&data_decoded, LSTR_SB_V(&data_buf)));
        Z_ASSERT_LSTREQUAL(data, LSTR_SB_V(&data_decoded));

        sb_reset(&data_buf);
        sb_reset(&data_decoded);
        sb_add_lstr_b64url(&data_buf, data, -1);
        Z_ASSERT_STREQUAL(data_buf.data, "2Yfj_kh-JYH7");
        Z_ASSERT_N(sb_add_lstr_unb64url(&data_decoded, LSTR_SB_V(&data_buf)));
        Z_ASSERT_LSTREQUAL(data, LSTR_SB_V(&data_decoded));

        /* Data encoded with base64url should not be decoded with base64.
         * The opposite is also true.
         */
        Z_ASSERT_N(sb_add_lstr_unb64(&data_decoded, LSTR("wQA/03e=")));
        Z_ASSERT_NEG(sb_add_lstr_unb64(&data_decoded, LSTR("wQA-03e=")));
        Z_ASSERT_NEG(sb_add_lstr_unb64(&data_decoded, LSTR("wQA_03e=")));
        Z_ASSERT_NEG(sb_add_lstr_unb64(&data_decoded, LSTR("wQA&03e=")));

        Z_ASSERT_N(sb_add_lstr_unb64url(&data_decoded, LSTR("wQA_03e=")));
        Z_ASSERT_NEG(sb_add_lstr_unb64url(&data_decoded, LSTR("wQA/03e=")));
        Z_ASSERT_NEG(sb_add_lstr_unb64url(&data_decoded, LSTR("wQA+03e=")));
        Z_ASSERT_NEG(sb_add_lstr_unb64url(&data_decoded, LSTR("wQA&03e=")));
    } Z_TEST_END

} Z_GROUP_END;

/* }}} */
/* {{{ csv */

#define CSV_TEST_START(_str, _separator, _qchar)                             \
        __unused__ int quoting_character = (_qchar);                         \
        __unused__ int separator = (_separator);                             \
        qv_t(lstr) fields;                                                   \
        pstream_t str = ps_initstr(_str);                                    \
                                                                             \
        qv_init(&fields)


#define CSV_TEST_END()                                                       \
        qv_deep_wipe(&fields, lstr_wipe)

#define CSV_TEST_GET_ROW(out_line)                                           \
    qv_deep_clear(&fields, lstr_wipe);                                 \
    Z_ASSERT_N(ps_get_csv_line(NULL, &str, separator,                        \
                               quoting_character, &fields, out_line))

#define CSV_TEST_FAIL_ROW() \
    qv_deep_clear(&fields, lstr_wipe);                                 \
    Z_ASSERT_NEG(ps_get_csv_line(NULL, &str, separator, quoting_character,   \
                                 &fields, NULL))

#define CSV_TEST_CHECK_EOF()  Z_ASSERT(ps_done(&str))

#define CSV_TEST_CHECK_NB_FIELDS(_n) \
    Z_ASSERT_EQ(fields.len, _n, "field count mismatch");

#define CSV_TEST_CHECK_FIELD(_n, _str)                                       \
    if (_str == NULL) {                                                      \
        Z_ASSERT_NULL(fields.tab[_n].s);                                     \
    } else {                                                                 \
        Z_ASSERT_P(fields.tab[_n].s);                                        \
        Z_ASSERT_LSTREQUAL(fields.tab[_n], LSTR(_str), "field value");       \
    }

Z_GROUP_EXPORT(csv) {
    Z_TEST(row1, "no row") {
        /* No row */
        CSV_TEST_START("", ',', '"');
        CSV_TEST_CHECK_EOF();
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(row2, "Single row") {
        pstream_t row;

        CSV_TEST_START("foo,bar,baz\r\n", ',', '"');
        CSV_TEST_GET_ROW(&row);
        Z_ASSERT_LSTREQUAL(LSTR("foo,bar,baz"), LSTR_PS_V(&row));
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(row3, "Several rows") {
        pstream_t row;

        CSV_TEST_START("foo,bar,baz\r\n"
                       "truc,machin,bidule\r\n",
                       ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_GET_ROW(&row);
        Z_ASSERT_LSTREQUAL(LSTR("truc,machin,bidule"), LSTR_PS_V(&row));
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(row4, "Mixed line terminators") {
        pstream_t row;

        CSV_TEST_START("foo,bar,baz\n"
                       "truc,machin,bidule\r\n",
                       ',', '"');
        CSV_TEST_GET_ROW(&row);
        Z_ASSERT_LSTREQUAL(LSTR("foo,bar,baz"), LSTR_PS_V(&row));
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(row5, "No line terminator") {
        pstream_t row;

        CSV_TEST_START("foo,bar,baz", ',', '"');
        CSV_TEST_GET_ROW(&row);
        Z_ASSERT_LSTREQUAL(LSTR("foo,bar,baz"), LSTR_PS_V(&row));
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(base1, "Base") {
        CSV_TEST_START("foo", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(1);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(base2, "Base 2") {
        CSV_TEST_START("foo,bar", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(2);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(base3, "Base 3") {
        CSV_TEST_START("foo,bar,baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_CHECK_FIELD(2, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(allowed1, "Invalid but allowed fields 1") {
        CSV_TEST_START("foo,bar\"baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(2);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar\"baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(invalid1, "Invalid fields 2") {
        CSV_TEST_START("foo,\"ba\"z", ',', '"');
        CSV_TEST_FAIL_ROW();
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(empty1, "Empty fields 1") {
        CSV_TEST_START("foo,,baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, NULL);
        CSV_TEST_CHECK_FIELD(2, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(empty2, "Empty fields 2") {
        CSV_TEST_START("foo,bar,", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_CHECK_FIELD(2, NULL);
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(empty3, "Empty fields 3") {
        CSV_TEST_START(",bar,baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, NULL);
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_CHECK_FIELD(2, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(empty4, "Empty fields 4") {
        CSV_TEST_START(",,", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, NULL);
        CSV_TEST_CHECK_FIELD(1, NULL);
        CSV_TEST_CHECK_FIELD(2, NULL);
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(empty4, "Empty fields 4") {
        CSV_TEST_START(",,", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, NULL);
        CSV_TEST_CHECK_FIELD(1, NULL);
        CSV_TEST_CHECK_FIELD(2, NULL);
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted1, "Quoted fields 1") {
        CSV_TEST_START("foo,\"bar\",baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted2, "Quoted fields 2") {
        CSV_TEST_START("foo,bar,\"baz\"", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_CHECK_FIELD(2, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted3, "Quoted fields 3") {
        CSV_TEST_START("\"foo\",bar,baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(3);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_CHECK_FIELD(2, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted4, "Quoted fields 4") {
        CSV_TEST_START("\"foo,bar\",baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(2);
        CSV_TEST_CHECK_FIELD(0, "foo,bar");
        CSV_TEST_CHECK_FIELD(1, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted5, "Quoted fields 5") {
        CSV_TEST_START("\"foo,\"\"\"", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(1);
        CSV_TEST_CHECK_FIELD(0, "foo,\"");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted6, "Quoted fields 6") {
        CSV_TEST_START("\"foo\n"
                       "bar\",baz", ',', '"');
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(2);
        CSV_TEST_CHECK_FIELD(0, "foo\nbar");
        CSV_TEST_CHECK_FIELD(1, "baz");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted7, "Quoted fields 7") {
        CSV_TEST_START("\"foo,\"\"", ',', '"');
        CSV_TEST_FAIL_ROW();
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(quoted8, "Quoted fields 8") {
        CSV_TEST_START("\"foo,\"bar\"", ',', '"');
        CSV_TEST_FAIL_ROW();
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(noquoting1, "No quoting character 1") {
        CSV_TEST_START("foo,bar", ',', -1);
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(2);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "bar");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(noquoting2, "No quoting character 2") {
        CSV_TEST_START("foo,\"bar\"", ',', -1);
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(2);
        CSV_TEST_CHECK_FIELD(0, "foo");
        CSV_TEST_CHECK_FIELD(1, "\"bar\"");
        CSV_TEST_END();
    } Z_TEST_END;

    Z_TEST(noquoting3, "No quoting character 3") {
        CSV_TEST_START("fo\"o", ',', -1);
        CSV_TEST_GET_ROW(NULL);
        CSV_TEST_CHECK_NB_FIELDS(1);
        CSV_TEST_CHECK_FIELD(0, "fo\"o");
        CSV_TEST_END();
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */
/* {{{ str_buf_pp */

Z_GROUP_EXPORT(str_buf_pp) {
    Z_TEST(add_table, "sb_add_table") {
        t_scope;
        t_SB_1k(sb);
        qv_t(table_hdr) hdr;
        qv_t(lstr) *row;
        qv_t(table_data) data;
        table_hdr_t hdr_data[] = { {
                .title = LSTR_IMMED("COL A"),
            }, {
                .title = LSTR_IMMED("COL B"),
            }, {
                .title = LSTR_IMMED("COL C"),
            }
        };

        qv_init_static(&hdr, hdr_data, countof(hdr_data));
        t_qv_init(&data, 2);
        row = qv_growlen(&data, 1);
        t_qv_init(row, countof(hdr_data));
        qv_append(row, LSTR("col A - rôw 1"));
        qv_append(row, LSTR("col B - row 1"));
        row = qv_growlen(&data, 1);
        t_qv_init(row, countof(hdr_data));
        qv_append(row, LSTR("col A - row 2"));
        qv_append(row, LSTR("çôl B - row 2"));

        sb_reset(&sb);
        sb_add_table(&sb, &hdr, &data);
        Z_ASSERT_STREQUAL(sb.data, "COL A          COL B          COL C\n"
                                   "col A - rôw 1  col B - row 1  \n"
                                   "col A - row 2  çôl B - row 2  \n");

        sb_reset(&sb);
        sb_add_csv_table(&sb, &hdr, &data, ';');
        Z_ASSERT_STREQUAL(sb.data, "COL A;COL B;COL C\n"
                                   "col A - rôw 1;col B - row 1;\n"
                                   "col A - row 2;çôl B - row 2;\n");

        hdr_data[0].max_width = 7;
        hdr_data[1].min_width = 20;
        hdr_data[2].omit_if_empty = true;

        sb_reset(&sb);
        sb_add_table(&sb, &hdr, &data);
        Z_ASSERT_STREQUAL(sb.data, "COL A    COL B               \n"
                                   "col A -  col B - row 1       \n"
                                   "col A -  çôl B - row 2       \n");

        sb_reset(&sb);
        sb_add_csv_table(&sb, &hdr, &data, ';');
        Z_ASSERT_STREQUAL(sb.data, "COL A;COL B\n"
                                   "col A - rôw 1;col B - row 1\n"
                                   "col A - row 2;çôl B - row 2\n");

        hdr_data[0].max_width = 7;
        hdr_data[0].add_ellipsis = true;
        hdr_data[1].min_width = 0;
        hdr_data[2].omit_if_empty = false;
        hdr_data[2].empty_value = LSTR("-");

        sb_reset(&sb);
        sb_add_table(&sb, &hdr, &data);
        Z_ASSERT_STREQUAL(sb.data, "COL A    COL B          COL C\n"
                                   "col A …  col B - row 1  -\n"
                                   "col A …  çôl B - row 2  -\n");

        sb_reset(&sb);
        sb_add_csv_table(&sb, &hdr, &data, ';');
        Z_ASSERT_STREQUAL(sb.data, "COL A;COL B;COL C\n"
                                   "col A - rôw 1;col B - row 1;-\n"
                                   "col A - row 2;çôl B - row 2;-\n");

        hdr_data[2].align = ALIGN_RIGHT;

        sb_reset(&sb);
        sb_add_table(&sb, &hdr, &data);
        Z_ASSERT_STREQUAL(sb.data, "COL A    COL B          COL C\n"
                                   "col A …  col B - row 1      -\n"
                                   "col A …  çôl B - row 2      -\n");

        hdr_data[2].align = ALIGN_CENTER;

        sb_reset(&sb);
        sb_add_table(&sb, &hdr, &data);
        Z_ASSERT_STREQUAL(sb.data, "COL A    COL B          COL C\n"
                                   "col A …  col B - row 1    -\n"
                                   "col A …  çôl B - row 2    -\n");

        /* Add a row with characters that will be escaped. */
        row = qv_growlen(&data, 1);
        t_qv_init(row, countof(hdr_data));
        qv_append(row, LSTR("col A -\n \"row\" 3"));
        qv_append(row, LSTR("çôl B -\r row 3"));

        sb_reset(&sb);
        sb_add_csv_table(&sb, &hdr, &data, ';');
        Z_ASSERT_STREQUAL(sb.data,
            "COL A;COL B;COL C\n"
            "col A - rôw 1;col B - row 1;-\n"
            "col A - row 2;çôl B - row 2;-\n"
            "\"col A -\n \"\"row\"\" 3\";\"çôl B -\r row 3\";-\n");

        qv_clear(&data);
        row = qv_growlen(&data, 1);
        t_qv_init(row, countof(hdr_data));
        qv_append(row, LSTR_NULL_V);
        qv_append(row, LSTR("col B - row 1"));
        hdr_data[0].omit_if_empty = true;

        sb_reset(&sb);
        sb_add_table(&sb, &hdr, &data);
        Z_ASSERT_STREQUAL(sb.data, "COL B          COL C\n"
                                   "col B - row 1    -\n");

    } Z_TEST_END;
} Z_GROUP_END

/* }}} */
/* {{{ conv */

Z_GROUP_EXPORT(conv)
{
    sb_t tmp, out;
    lstr_t default_tab = LSTR_IMMED(
        "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
        "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1c\x1d\x1e\x1f"
        "\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f"
        "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\x3e\x3f"
        "\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f"
        "\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5a\x5b\x5c\x5d\x5e\x5f"
        "\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6a\x6b\x6c\x6d\x6e\x6f"
        "\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7a\x7b\x7c\x7d\x7e\x7f");
    lstr_t extended_tab = LSTR_IMMED(
        "\x1b\x14\x1b\x28\x1b\x29\x1b\x2f\x1b\x3c\x1b\x3d\x1b\x3e\x1b\x40\x1b\x65");

    Z_TEST(sb_conv_gsm, "sb conv from/to gsm") {
#define TL(input, expected, desc)       \
        ({  lstr_t in    = input;                                            \
            lstr_t exp_s = expected;                                         \
            sb_reset(&tmp);                                                  \
            sb_reset(&out);                                                  \
            sb_conv_from_gsm(&tmp, in.s, in.len);                            \
            sb_conv_to_gsm(&out, tmp.data, tmp.len);                         \
            Z_ASSERT_LSTREQUAL(exp_s, LSTR_SB_V(&out), desc);                \
        })
#define TLHEX(input, expected, desc)    \
        ({  lstr_t in    = input;                                            \
            lstr_t exp_s = expected;                                         \
            SB_1k(in_hex); SB_1k(exp_hex);                                   \
            sb_add_hex(&in_hex, in.s, in.len);                               \
            sb_add_hex(&exp_hex, exp_s.s, exp_s.len);                        \
            sb_reset(&tmp);                                                  \
            sb_reset(&out);                                                  \
            sb_conv_from_gsm_hex(&tmp, in_hex.data, in_hex.len);             \
            sb_conv_to_gsm_hex(&out, tmp.data, tmp.len);                     \
            Z_ASSERT_LSTREQUAL(LSTR_SB_V(&exp_hex), LSTR_SB_V(&out), desc);  \
        })

        sb_init(&tmp);
        sb_init(&out);

        /* behavior in 2012.4 */
        TL(LSTR_IMMED("\x80\x20\x81\x20\x82"),
           LSTR_IMMED("\x2e\x20\x2e\x20\x2e"),
           "conversion with invalid characters");

        for (int i = 0; i < default_tab.len; i++) {
            TL(LSTR_INIT_V(default_tab.s, i),
               LSTR_INIT_V(default_tab.s, i),
               "test default table with various lengths");
            TLHEX(LSTR_INIT_V(default_tab.s, i),
                  LSTR_INIT_V(default_tab.s, i),
                  "test default table with various lengths (hex)");
        }
        for (int i = 0; i < extended_tab.len; i += 2) {
            TL(LSTR_INIT_V(extended_tab.s, i),
               LSTR_INIT_V(extended_tab.s, i),
               "test extension table with various lengths");
            TLHEX(LSTR_INIT_V(extended_tab.s, i),
                  LSTR_INIT_V(extended_tab.s, i),
                  "test extension table with various lengths (hex)");
        }

        {
            lstr_t str = LSTR_IMMED(
                "coucou random:\"jk6q?#hU*1/m.VVteU[i4S|\\\"@>'wrTFuV[Csrvi<^|%/1>|"
                "'9kpfG76aY5)gWN!+1D8aj-j|)'3'\"ZO:F#XL7n2=DpIEtU5%H8UICK.F\"&2HBOi6ZLZ[|ptN-z");
            sb_wipe(&tmp);
            sb_reset(&out);
            sb_conv_to_gsm_hex(&tmp, str.s, str.len);
            sb_conv_from_gsm_hex(&out, tmp.data, tmp.len);
            Z_ASSERT_LSTREQUAL(str, LSTR_SB_V(&out), "emi teaser crash");
        }

        sb_wipe(&tmp);
        sb_wipe(&out);

#undef TL
#undef TLHEX
    } Z_TEST_END

    Z_TEST(sb_conv_cimd, "sb conv from/to cimd") {
        SB_1k(sb);

#define T(input, _expected, description)      \
        ({  lstr_t expected = LSTR_IMMED(_expected);                         \
            lstr_t in  = LSTR_IMMED(input);                                  \
                                                                             \
            sb_reset(&out);                                                  \
            sb_conv_from_gsm_plan(&out, in.s, in.len, GSM_CIMD_PLAN);        \
            Z_ASSERT_LSTREQUAL(expected, LSTR_SB_V(&out), description);      \
        })

        sb_init(&out);

        /* Example 22 from CIMD spec 8.0 (@£$¥èéùìòç) */
        T("_Oa_L-$_Y-_e`_e'_u`_i`_o`_C,",
          "\x40\xc2\xa3\x24\xc2\xa5\xc3\xa8\xc3\xa9\xc3\xb9\xc3\xac\xc3\xb2"
          "\xc3\x87",
          "Default character conversion over 7-bit link");

        /* A few characters can be encoded either using one latin1 char or a
         * special combination of ascii char */
        T("_Oa_A*_a*_qq_A\"_O\"_U\"_a\"_o\"_u\"",
          "\x40\xC3\x85\xC3\xA5\x22\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3"
          "\xB6\xC3\xBC",
          "special combination");
        T("@]}\"[\\^{|~",
          "\x40\xC3\x85\xC3\xA5\x22\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3"
          "\xB6\xC3\xBC",
          "iso-latin");

        for (int c = 1; c < 256; c++) {
            sb_addc(&sb, c);
        }
        sb_reset(&out);
        Z_ASSERT_N(sb_conv_from_gsm_plan(&out, sb.data, sb.len,
                                         GSM_DEFAULT_PLAN));
        sb_reset(&out);
        Z_ASSERT_N(sb_conv_from_gsm_plan(&out, sb.data, sb.len,
                                         GSM_CIMD_PLAN));

        sb_reset(&sb);
        for (int c = 0; c < 128; c++) {
            sb_adduc(&sb, gsm7_to_unicode(c, '.'));
        }

        sb_reset(&tmp);
        sb_conv_to_cimd(&tmp, sb.data, sb.len);

        sb_reset(&out);
        Z_ASSERT_N(sb_conv_from_gsm_plan(&out, tmp.data, tmp.len,
                                         GSM_CIMD_PLAN));
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&sb), LSTR_SB_V(&out));

        sb_wipe(&out);
        sb_wipe(&tmp);

#undef T
    } Z_TEST_END

    Z_TEST(sb_conv_to_gsm_isok, "sb_conv_to_gsm_isok") {
#define T(input, res, plan, description)      \
        ({  lstr_t in  = LSTR_IMMED(input);                                 \
            Z_ASSERT(res == sb_conv_to_gsm_isok(in.s, in.len, plan),        \
                     description);                                          \
        })

        T("\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
          false, GSM_DEFAULT_PLAN,
          "utf8 which cannot be mapped to gsm7");

        T("\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f"
          /* éèêàâç */
          "\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xa0\xc3\xa2\xc3\xa7",
          true, GSM_DEFAULT_PLAN,
          "utf8 which can be mapped to gsm7");

        T("\xe2\x82\xac", false, GSM_DEFAULT_PLAN,
          "euro cannot be mapped with default table");
        T("\xe2\x82\xac", true, GSM_EXTENSION_PLAN,
          "euro can be mapped with extension table");

#undef T
    } Z_TEST_END
    Z_TEST(sb_conv_to_gsm7, "sb conv to gsm7") {
        SB_1k(sb);
        const char *long_str = "abcdefghijklmnopqrstuvwxyz";
        struct {
            const char *in;
            int         size;
            lstr_t      exp;
        } t[] = {

#define T(_in, _size, _exp) { .in = _in, .size = _size, .exp = _exp }

            T("abcd", 4, LSTR_IMMED("\x61\xF1\x98\x0C")),
            /* euro symbole */
            T("\xE2\x82\xAC\x00", 2, LSTR_IMMED("\x9B\x32")),
            /* start with euro */
            T("\xE2\x82\xAC""abcd", 6, LSTR_IMMED("\x9B\x72\x58\x3C\x26\x03")),
            /* euro in the middle */
            T("ab\xE2\x82\xAC""cd", 6, LSTR_IMMED("\x61\xF1\xA6\x3C\x26\x03")),
            /* stop with euro */
            T("abcd\xE2\x82\xAC", 6, LSTR_IMMED("\x61\xF1\x98\xBC\x29\x03")),
            /* [ and ] are extended */
            T("[*]", 5, LSTR_IMMED("\x1B\x9E\x6A\xE3\x03")),
            /* long string */
            T(long_str, 23,
              LSTR_IMMED("\x61\xF1\x98\x5C\x36\x9F\xD1\x69\xF5\x9A\xDD\x76"
                         "\xBF\xE1\x71\xF9\x9C\x5E\xB7\xDF\xF1\x79\x3D")),

#undef T

        };

        for (int i = 0; i < countof(t); i++) {
            sb_reset(&sb);
            Z_ASSERT_N(sb_conv_to_gsm7(&sb, 0, t[i].in, ' ',
                                       GSM_EXTENSION_PLAN, -1));
            sb_reset(&sb);
            Z_ASSERT_N(sb_conv_to_gsm7(&sb, 0, t[i].in, ' ',
                                               GSM_EXTENSION_PLAN,
                                               t[i].size));
            Z_ASSERT_LSTREQUAL(t[i].exp, LSTR_SB_V(&sb));
            sb_reset(&sb);
            Z_ASSERT_N(sb_conv_to_gsm7(&sb, 0, t[i].in, ' ',
                                               GSM_EXTENSION_PLAN,
                                               t[i].size + 1));
            Z_ASSERT_LSTREQUAL(t[i].exp, LSTR_SB_V(&sb));
            sb_reset(&sb);
            Z_ASSERT_NEG(sb_conv_to_gsm7(&sb, 0, t[i].in, ' ',
                                                 GSM_EXTENSION_PLAN,
                                                 t[i].size - 1));
        }

        /* long string without check */
        Z_ASSERT_N(sb_conv_to_gsm7(&sb, 0, long_str, ' ',
                                           GSM_EXTENSION_PLAN, -1));

    } Z_TEST_END

} Z_GROUP_END

/* }}} */

/* LCOV_EXCL_STOP */
