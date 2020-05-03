/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#include <lib-common/arith.h>
#include <lib-common/z.h>

/* {{{ bsr_bsf */

Z_GROUP_EXPORT(bsr_bsf)
{
    Z_TEST(bsf_1, "forward bit scan") {
        uint8_t data[128];

        p_clear(&data, 1);
        Z_ASSERT_NEG(bsf(data, 0, 0, false));
        Z_ASSERT_NEG(bsf(data, 0, 1024, false));

        SET_BIT(data, 3);
        SET_BIT(data, 165);
        Z_ASSERT_EQ(bsf(data, 0, 1024, false), 3);
        Z_ASSERT_EQ(bsf(data, 1, 1023, false), 2);
        Z_ASSERT_EQ(bsf(data, 3, 1021, false), 0);
        Z_ASSERT_EQ(bsf(data, 5, 1019, false), 160);
        Z_ASSERT_EQ(bsf(data, 5, 161, false), 160);
        Z_ASSERT_EQ(bsf(data, 0, 4, false), 3);
        Z_ASSERT_NEG(bsf(data, 5, 150, false));
        Z_ASSERT_NEG(bsf(data, 5, 33, false));
        Z_ASSERT_NEG(bsf(data, 5, 160, false));
        Z_ASSERT_NEG(bsf(data, 0, 3, false));

        Z_ASSERT_EQ(bsf(&data[1], 3, 1013, false), 154);
    } Z_TEST_END;

    Z_TEST(bsf_0, "forward bit scan, scan of 0") {
        uint8_t data[128];

        p_clear(&data, 1);
        Z_ASSERT_NEG(bsf(data, 0, 0, true));
        Z_ASSERT_ZERO(bsf(data, 0, 1024, true));

        memset(data, 0xff, 128);
        RST_BIT(data, 3);
        RST_BIT(data, 165);
        Z_ASSERT_EQ(bsf(data, 0, 1024, true), 3);
        Z_ASSERT_EQ(bsf(data, 1, 1023, true), 2);
        Z_ASSERT_EQ(bsf(data, 3, 1021, true), 0);
        Z_ASSERT_EQ(bsf(data, 5, 1019, true), 160);
        Z_ASSERT_EQ(bsf(data, 5, 161, true), 160);
        Z_ASSERT_EQ(bsf(data, 0, 4, true), 3);
        Z_ASSERT_NEG(bsf(data, 5, 150, true));
        Z_ASSERT_NEG(bsf(data, 5, 33, true));
        Z_ASSERT_NEG(bsf(data, 5, 160, true));
        Z_ASSERT_NEG(bsf(data, 0, 3, true));

        Z_ASSERT_EQ(bsf(&data[1], 3, 1013, true), 154);
    } Z_TEST_END;


    Z_TEST(bsr_1, "reverse bit scan") {
        uint8_t data[128];

        p_clear(&data, 1);
        Z_ASSERT_NEG(bsr(data, 0, 0, false));
        Z_ASSERT_NEG(bsr(data, 0, 1024, false));

        SET_BIT(data, 3);
        SET_BIT(data, 165);
        Z_ASSERT_EQ(bsr(data, 0, 1024, false), 165);
        Z_ASSERT_EQ(bsr(data, 1, 1023, false), 164);
        Z_ASSERT_EQ(bsr(data, 3, 1021, false), 162);
        Z_ASSERT_EQ(bsr(data, 1, 100, false), 2);
        Z_ASSERT_EQ(bsr(data, 3, 100, false), 0);
        Z_ASSERT_EQ(bsr(data, 5, 161, false), 160);
        Z_ASSERT_EQ(bsr(data, 0, 4, false), 3);
        Z_ASSERT_NEG(bsr(data, 5, 150, false));
        Z_ASSERT_NEG(bsr(data, 5, 33, false));
        Z_ASSERT_NEG(bsr(data, 5, 160, false));
        Z_ASSERT_NEG(bsr(data, 0, 3, false));

        Z_ASSERT_EQ(bsr(&data[1], 3, 1013, false), 154);

        /* Check that we read inside boundaries */
        memset(data, 0xff, 8);
        memset(data + 8, 0, 16);
        memset(data + 24, 0xff, 8);
        for (int i = 64; i < 114; i++) {
            SET_BIT(data, i);
        }
        /* --- blank on 40 bits --- */
        for (int i = 154; i <= 191; i++) {
            SET_BIT(data, i);
        }
        Z_ASSERT_NEG(bsr(data + 8, 50, 40, false));
    } Z_TEST_END;

    Z_TEST(bsr_0, "reverse bit scan, scan of 0") {
        uint8_t data[128];

        p_clear(&data, 1);
        Z_ASSERT_NEG(bsr(data, 0, 0, true));
        Z_ASSERT_EQ(bsr(data, 0, 1024, true), 1023);

        memset(data, 0xff, 128);
        RST_BIT(data, 3);
        RST_BIT(data, 165);
        Z_ASSERT_EQ(bsr(data, 0, 1024, true), 165);
        Z_ASSERT_EQ(bsr(data, 1, 1023, true), 164);
        Z_ASSERT_EQ(bsr(data, 3, 1021, true), 162);
        Z_ASSERT_EQ(bsr(data, 1, 100, true), 2);
        Z_ASSERT_EQ(bsr(data, 3, 100, true), 0);
        Z_ASSERT_EQ(bsr(data, 5, 161, true), 160);
        Z_ASSERT_EQ(bsr(data, 0, 4, true), 3);
        Z_ASSERT_NEG(bsr(data, 5, 150, true));
        Z_ASSERT_NEG(bsr(data, 5, 33, true));
        Z_ASSERT_NEG(bsr(data, 5, 160, true));
        Z_ASSERT_NEG(bsr(data, 0, 3, true));

        Z_ASSERT_EQ(bsr(&data[1], 3, 1013, true), 154);
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */
/* {{{ bit_reverse */

Z_GROUP_EXPORT(bit_reverse) {
    Z_TEST(bit_reverse, "bit reverse") {
        Z_ASSERT_EQ(bit_reverse16(0x3445), 0xa22c);
        Z_ASSERT_EQ(bit_reverse64(0xabc), 0x3d50000000000000ull);
        Z_ASSERT_EQ(bit_reverse64(0x101010101010101), 0x8080808080808080ull);

        for (unsigned i = 0; i < countof(__bit_reverse8); i++) {
            unsigned word = __bit_reverse8[i];
            for (unsigned j = 0; j < 8; j++) {
                Z_ASSERT_EQ((word >> j) & 1, (i >> (7 - j)) & 1);
            }
            Z_ASSERT_EQ(__bit_reverse8[__bit_reverse8[i]], i);
        }
    } Z_TEST_END;
} Z_GROUP_END

/* }}} */
/* {{{ membitcount */

#ifdef __HAS_CPUID
#pragma push_macro("__leaf")
#undef __leaf
#include <cpuid.h>
#pragma pop_macro("__leaf")
#endif

static size_t membitcount_naive(const void *_p, size_t n)
{
    const uint8_t *p = _p;
    size_t res = 0;

    for (size_t i = 0; i < n; i++) {
        res += bitcount8(p[i]);
    }
    return res;
}

static int membitcount_check_small(size_t (*fn)(const void *, size_t))
{
    static uint8_t v[64] = {
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
    };
    for (int i = 0; i < countof(v); i++) {
        for (int j = i; j < countof(v); j++) {
            Z_ASSERT_EQ(membitcount_naive(v + i, j - i), fn(v + i, j - i),
                        "i:%d j:%d", i, j);
        }
    }
    Z_HELPER_END;
}

static int membitcount_check_rand(size_t (*fn)(const void *, size_t))
{
#define N (1U << 12)
    t_scope;
    char *v = t_new_raw(char, N);

    for (size_t i = 0; i < N; i++)
        v[i] = i;
    for (size_t i = 0; i < 32; i++) {
        Z_ASSERT_EQ(membitcount_naive(v + i, N - i), fn(v + i, N - i));
    }
    for (size_t i = 0; i < 32; i++) {
        Z_ASSERT_EQ(membitcount_naive(v, N - i), fn(v, N - i));
    }
    Z_HELPER_END;
#undef N
}

Z_GROUP_EXPORT(membitcount)
{
    Z_TEST(fast_c, "") {
        Z_HELPER_RUN(membitcount_check_rand(membitcount_c));
        Z_HELPER_RUN(membitcount_check_small(membitcount_c));
    } Z_TEST_END;

    Z_TEST(ssse3, "") {
#ifdef __HAS_CPUID
        int eax, ebx, ecx, edx;

        __cpuid(1, eax, ebx, ecx, edx);
        if (ecx & bit_SSSE3) {
            Z_HELPER_RUN(membitcount_check_rand(membitcount_ssse3));
            Z_HELPER_RUN(membitcount_check_small(membitcount_ssse3));
        } else {
            Z_SKIP("your CPU doesn't support ssse3");
        }
#else
        Z_SKIP("neither amd64 nor i386 or unsupported compiler");
#endif
    } Z_TEST_END;

    Z_TEST(popcnt, "") {
#ifdef __HAS_CPUID
        int eax, ebx, ecx, edx;

        __cpuid(1, eax, ebx, ecx, edx);
        if (ecx & bit_POPCNT) {
            Z_HELPER_RUN(membitcount_check_rand(membitcount_popcnt));
            Z_HELPER_RUN(membitcount_check_small(membitcount_popcnt));
        } else {
            Z_SKIP("your CPU doesn't support popcnt");
        }
#else
        Z_SKIP("neither amd64 nor i386 or unsupported compiler");
#endif
    } Z_TEST_END;
} Z_GROUP_END

/* }}} */

/* LCOV_EXCL_STOP */
