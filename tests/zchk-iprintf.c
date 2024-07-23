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

/* LCOV_EXCL_START */

#include <endian.h>
#include <math.h>

#include <lib-common/core.h>
#include <lib-common/z.h>

Z_GROUP_EXPORT(iprintf) {
    Z_TEST(double, "") {
        char buffer[128];

        isprintf(buffer, "%g", -INFINITY);
        Z_ASSERT_STREQUAL(buffer, "-Inf");
        isprintf(buffer, "%g", INFINITY);
        Z_ASSERT_STREQUAL(buffer, "Inf");
        isprintf(buffer, "%+g", INFINITY);
        Z_ASSERT_STREQUAL(buffer, "+Inf");
    } Z_TEST_END;

    Z_TEST(pM, "") {
        char buffer[128];

        isprintf(buffer, "%*pM", 3, "1234");
        Z_ASSERT_STREQUAL(buffer, "123", "");
        isprintf(buffer, "%*pM;toto", 3, "123");
        Z_ASSERT_STREQUAL(buffer, "123;toto", "");
        isprintf(buffer, "%*pMtrailing", 3, "123");
        Z_ASSERT_STREQUAL(buffer, "123trailing", "");
    } Z_TEST_END

    Z_TEST(pX, "") {
        char buffer[128];

        isprintf(buffer, "%*pX", 4, "1234");
        Z_ASSERT_STREQUAL(buffer, "31323334");
        isprintf(buffer, "%*pX world!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656C6C6F world!");
        isprintf(buffer, "%*pXworld!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656C6C6Fworld!");
    } Z_TEST_END;

    Z_TEST(px, "") {
        char buffer[128];

        isprintf(buffer, "%*px", 4, "1234");
        Z_ASSERT_STREQUAL(buffer, "31323334");
        isprintf(buffer, "%*px world!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656c6c6f world!");
        isprintf(buffer, "%*pxworld!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656c6c6fworld!");
    } Z_TEST_END;

    Z_TEST(pL, "") {
        char buffer[128];
        const lstr_t str = LSTR_IMMED("1234");
        SB_1k(sb);

        isprintf(buffer, "%pL", &str);
        Z_ASSERT_STREQUAL(buffer, "1234");

        isprintf(buffer, "%pL;toto", &str);
        Z_ASSERT_STREQUAL(buffer, "1234;toto");
        isprintf(buffer, "%pLtrailing", &str);
        Z_ASSERT_STREQUAL(buffer, "1234trailing");

        /* works for sb_t variables too */
        sb_set_lstr(&sb, str);

        isprintf(buffer, "%pL", &sb);
        Z_ASSERT_STREQUAL(buffer, "1234");

        isprintf(buffer, "%pL;toto", &sb);
        Z_ASSERT_STREQUAL(buffer, "1234;toto");
        isprintf(buffer, "%pLtrailing", &sb);
        Z_ASSERT_STREQUAL(buffer, "1234trailing");
    } Z_TEST_END

    Z_TEST(ivasprintf, "") {
        char *formatted = iasprintf("%*pM", 4, "1234");
        int len = 2 * BUFSIZ;
        char big[len + 1];

        Z_ASSERT_STREQUAL(formatted, "1234");
        p_delete(&formatted);

        memset(big, 'a', len);
        big[2*BUFSIZ] = 0;
        formatted = iasprintf("%*pM", len, big);
        Z_ASSERT_STREQUAL(formatted, big);
        p_delete(&formatted);
    } Z_TEST_END;

    Z_TEST(thousand_sep, "") {
        char buffer[128];

#define T(_fmt, _val, _res)                                                   \
    do {                                                                     \
        isprintf(buffer, _fmt, (_val));                                      \
        Z_ASSERT_STREQUAL(buffer, _res, "format: %s", _fmt);                 \
    } while(0)

        T("%'hd", (short)12345, "12,345");
        T("%'d", 123456789, "123,456,789");
        T("%'ld", 123456789l, "123,456,789");
        T("%'lld", 123456789ll, "123,456,789");
        T("%'zd", 123456789l, "123,456,789");
        T("%'jd", 123456789l, "123,456,789");
        T("%'td", 123456789l, "123,456,789");

        T("%'hd", (short)-12345, "-12,345");
        T("%'d", -123456789, "-123,456,789");
        T("%'ld", -123456789l, "-123,456,789");
        T("%'lld", -123456789ll, "-123,456,789");
        T("%'zd", -123456789l, "-123,456,789");
        T("%'jd", -123456789l, "-123,456,789");
        T("%'td", -123456789l, "-123,456,789");

        T("%'hu", (unsigned short)12345u, "12,345");
        T("%'u", 123456789u, "123,456,789");
        T("%'lu", 123456789ul, "123,456,789");
        T("%'llu", 123456789ull, "123,456,789");
        T("%'zu", 123456789ul, "123,456,789");
        T("%'ju", 123456789ul, "123,456,789");
        T("%'tu", 123456789ul, "123,456,789");

        T("%'015hd", (short)12345, "00000000012,345");
        T("%'015d", 123456789, "0000123,456,789");
        T("%'015ld", 123456789l, "0000123,456,789");
        T("%'015lld", 123456789ll, "0000123,456,789");
        T("%'015zd", 123456789l, "0000123,456,789");
        T("%'015jd", 123456789l, "0000123,456,789");
        T("%'015td", 123456789l, "0000123,456,789");

        T("%'015hd", (short)-12345, "-0000000012,345");
        T("%'015d", -123456789, "-000123,456,789");
        T("%'015ld", -123456789l, "-000123,456,789");
        T("%'015lld", -123456789ll, "-000123,456,789");
        T("%'015zd", -123456789l, "-000123,456,789");
        T("%'015jd", -123456789l, "-000123,456,789");
        T("%'015td", -123456789l, "-000123,456,789");

        T("%'015hu", (unsigned short)12345u, "00000000012,345");
        T("%'015u", 123456789u, "0000123,456,789");
        T("%'015lu", 123456789ul, "0000123,456,789");
        T("%'015llu", 123456789ull, "0000123,456,789");
        T("%'015zu", 123456789ul, "0000123,456,789");
        T("%'015ju", 123456789ul, "0000123,456,789");
        T("%'015tu", 123456789ul, "0000123,456,789");

        /* UINT64_MAX */
        T("%'zu", 18446744073709551615ul, "18,446,744,073,709,551,615");

#undef T
    } Z_TEST_END;

    Z_TEST(i128, "printing 128 bits integers") {
        int len;
        char buffer[128];

#define T(_fmt, _val, _res) \
        do {                                                                 \
            p_clear(&buffer, 1);                                             \
            len = isnprintf(buffer, sizeof(buffer), _fmt, _val);             \
            Z_ASSERT_STREQUAL(buffer, _res, "format: `%s'", _fmt);           \
            Z_ASSERT_EQ(len, (int)strlen(_res), "format: `%s'", _fmt);       \
        } while (0)

        /* uint128_t */
        T(PRIu128, PRIu128_FMT_ARG(0), "0");
        T(PRIu128, PRIu128_FMT_ARG(1), "1");
        T(PRIu128, PRIu128_FMT_ARG(UINT32_MAX - 1), "4294967294");
        T(PRIu128, PRIu128_FMT_ARG(UINT32_MAX), "4294967295");
        T(PRIu128, PRIu128_FMT_ARG(UINT32_MAX + 1ULL), "4294967296");
        T(PRIu128, PRIu128_FMT_ARG(UINT64_MAX - 1), "18446744073709551614");
        T(PRIu128, PRIu128_FMT_ARG(UINT64_MAX), "18446744073709551615");
        T(PRIu128, PRIu128_FMT_ARG((uint128_t)UINT64_MAX + 1),
          "18446744073709551616");
        T(PRIu128, PRIu128_FMT_ARG(UINT128_MAX - 1),
          "340282366920938463463374607431768211454");
        T(PRIu128, PRIu128_FMT_ARG(UINT128_MAX),
          "340282366920938463463374607431768211455");
        T(PRIu128, PRIu128_FMT_ARG(MAKE128(0xdeadbeef, UINT64_MAX)),
          "68915718023982259027008552959");

        /* int128_t */
        T(PRId128, PRId128_FMT_ARG(INT128_MIN),
          "-170141183460469231731687303715884105728");
        T(PRId128, PRId128_FMT_ARG(INT128_MIN + 1),
          "-170141183460469231731687303715884105727");
        T(PRId128, PRId128_FMT_ARG(INT64_MIN), "-9223372036854775808");
        T(PRId128, PRId128_FMT_ARG(INT32_MIN), "-2147483648");
        T(PRId128, PRId128_FMT_ARG(-1), "-1");
        T(PRId128, PRId128_FMT_ARG(0), "0");
        T(PRId128, PRId128_FMT_ARG(1), "1");
        T(PRId128, PRId128_FMT_ARG(UINT32_MAX - 1), "4294967294");
        T(PRId128, PRId128_FMT_ARG(UINT32_MAX), "4294967295");
        T(PRId128, PRId128_FMT_ARG(UINT32_MAX + 1ULL), "4294967296");
        T(PRId128, PRId128_FMT_ARG(UINT64_MAX - 1), "18446744073709551614");
        T(PRId128, PRId128_FMT_ARG(UINT64_MAX), "18446744073709551615");
        T(PRId128, PRId128_FMT_ARG((uint128_t)UINT64_MAX + 1),
          "18446744073709551616");
        T(PRId128, PRId128_FMT_ARG(INT128_MAX - 1),
          "170141183460469231731687303715884105726");
        T(PRId128, PRId128_FMT_ARG(INT128_MAX),
          "170141183460469231731687303715884105727");
        T(PRId128, PRId128_FMT_ARG(MAKE128(0xdeadbeef, UINT64_MAX)),
          "68915718023982259027008552959");

        /* uint128_t / hex */
        T(PRIx128, PRIx128_FMT_ARG(0), "0");
        T(PRIx128, PRIx128_FMT_ARG(1), "1");
        T(PRIx128, PRIx128_FMT_ARG(0x1234567890abcdef), "1234567890abcdef");
        T(PRIX128, PRIX128_FMT_ARG(0x1234567890abcdef), "1234567890ABCDEF");
        T(PRIx128, PRIx128_FMT_ARG(UINT64_MAX), "ffffffffffffffff");
        T(PRIx128, PRIx128_FMT_ARG(UINT128_MAX),
          "ffffffffffffffffffffffffffffffff");
        T(PRIx128, PRIx128_FMT_ARG(MAKE128(0xdeadbeef, UINT64_MAX)),
          "deadbeefffffffffffffffff");

#undef T
    } Z_TEST_END;
} Z_GROUP_END

/* LCOV_EXCL_STOP */
