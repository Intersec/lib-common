/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <math.h>
#include <float.h>

#include <lib-common/arith.h>
#include <lib-common/z.h>

/* {{{ arithfloat */

Z_GROUP_EXPORT(arithfloat)
{
    Z_TEST(double_round) {
#define T(val, precision, res)                                               \
    do {                                                                     \
        Z_ASSERT_LT(fabs(double_round(val, precision) - res), DBL_EPSILON);  \
        Z_ASSERT_DBL_EQ(val, res, precision);                                \
    } while (0)

        T(12.1234567, 0, 12.);
        T(12.1234567, 1, 12.1);
        T(12.1234567, 2, 12.12);
        T(12.1234567, 3, 12.123);
        T(12.1234567, 4, 12.1235);
        T(12.1234567, 5, 12.12346);
        T(12.1234567, 6, 12.123457);
        T(12.1234567, 7, 12.1234567);
        T(12.1234567, 8, 12.1234567);
        T(12.12345, 4, 12.1235);

        T(12.6, 0, 13.);

        T(-12.1234567, 0, -12.);
        T(-12.1234567, 1, -12.1);
        T(-12.1234567, 2, -12.12);
        T(-12.1234567, 3, -12.123);
        T(-12.1234567, 4, -12.1235);
        T(-12.1234567, 5, -12.12346);
        T(-12.1234567, 6, -12.123457);
        T(-12.1234567, 7, -12.1234567);
        T(-12.1234567, 8, -12.1234567);
        T(-12.12345, 4, -12.1234);

        T(-12.6, 0, -13.);
#undef T
        Z_ASSERT_NE(isinf(double_round(INFINITY, 3)), 0);
        Z_ASSERT_NE(isinf(double_round(-INFINITY, 3)), 0);
        Z_ASSERT_NE(isnan(double_round(NAN, 3)), 0);
    }
    Z_TEST_END

    Z_TEST(double_round_significant) {
#define T(v, p, res)                                                         \
    Z_ASSERT_LT(fabs(double_round_significant(v, p) - res), DBL_EPSILON)

        T(12.1234567, 1, 12.);
        T(12.1234567, 2, 12.);
        T(12.1234567, 3, 12.1);
        T(12.1234567, 4, 12.12);
        T(12.1234567, 5, 12.123);
        T(12.1234567, 6, 12.1235);
        T(12.1234567, 7, 12.12346);
        T(12.1234567, 8, 12.123457);
        T(12.1234567, 9, 12.1234567);
        T(12.1234567, 10, 12.1234567);
        T(12.12345, 6, 12.1235);

        T(12.6, 2, 13.);

        T(1234.567, 2, 1235.);
        T(12345.67, 5, 12346.);
        T(1234567.8, 6, 1234568.);

        T(-12.1234567, 1, -12.);
        T(-12.1234567, 2, -12.);
        T(-12.1234567, 3, -12.1);
        T(-12.1234567, 4, -12.12);
        T(-12.1234567, 5, -12.123);
        T(-12.1234567, 6, -12.1235);
        T(-12.1234567, 7, -12.12346);
        T(-12.1234567, 8, -12.123457);
        T(-12.1234567, 9, -12.1234567);
        T(-12.1234567, 10, -12.1234567);
        T(-12.12345, 6, -12.1234);

        T(-12.6, 2, -13.);

        T(-1234.567, 2, -1235.);
        T(-12345.67, 5, -12346.);
        T(-1234567.8, 6, -1234568.);

        T(10.23, 2, 10.);
        T(10.23, 3, 10.2);
        T(9.23, 2, 9.2);
        T(9.23, 1, 9.);
#undef T
    }
    Z_TEST_END

    Z_TEST(double_is_close, "double_is_close") {
        /* Exactly equal values are close, whatever the tolerances. */
        Z_ASSERT(double_is_close(1.0, 1.0, 0, 0));
        Z_ASSERT(double_is_close(1.0, 1.0, 1e-9, 1e-12));
        /* +0 and -0 are equal for ==, hence close. */
        Z_ASSERT(double_is_close(0.0, -0.0, 0, 0));

        /* Relative tolerance: applied to the largest magnitude. */
        Z_ASSERT(double_is_close(1.0, 1.0 + 5e-10, 1e-9, 0));
        Z_ASSERT(!double_is_close(1.0, 1.0 + 2e-9, 1e-9, 0));
        /* It scales with the magnitude of the values. */
        Z_ASSERT(double_is_close(1e6, 1e6 + 5e-4, 1e-9, 0));
        Z_ASSERT(!double_is_close(1e6, 1e6 + 2e-3, 1e-9, 0));

        /* Absolute tolerance: a fixed floor, useful around zero where the
         * relative tolerance vanishes. */
        Z_ASSERT(double_is_close(0.0, 1e-13, 0, 1e-12));
        Z_ASSERT(!double_is_close(0.0, 1e-11, 0, 1e-12));
        Z_ASSERT(double_is_close(0.0, 1e-13, 1e-9, 1e-12));

        /* The two tolerances combine with a max(): here the absolute one
         * dominates the (tiny) relative one. */
        Z_ASSERT(double_is_close(1.0, 1.0 + 5e-10, 1e-12, 1e-9));
        Z_ASSERT(!double_is_close(1.0, 1.0 + 5e-10, 1e-12, 1e-12));

        /* Matching infinities are equal (==); any other case with an
         * infinity is not close. */
        Z_ASSERT(double_is_close(INFINITY, INFINITY, 1e-9, 1e-12));
        Z_ASSERT(double_is_close(-INFINITY, -INFINITY, 1e-9, 1e-12));
        Z_ASSERT(!double_is_close(INFINITY, -INFINITY, 1e-9, 1e-12));
        Z_ASSERT(!double_is_close(INFINITY, 1.0, 1e-9, 1e-12));

        /* NaN is never close, not even to an identical NaN: detecting that
         * is up to the caller (e.g. via double_is_identical()). */
        Z_ASSERT(!double_is_close(NAN, NAN, 1e-9, 1e-12));
        Z_ASSERT(!double_is_close(NAN, 1.0, 1e-9, 1e-12));
    }
    Z_TEST_END
}
Z_GROUP_END

/* }}} */
/* {{{ arithint */

Z_GROUP_EXPORT(arithint)
{
    Z_TEST(gcd, "gcd: Euclid's algorithm") {
        struct {
            uint32_t i;
            uint32_t j;
            uint32_t gcd;
        } t[] = {
            {5, 0, 5},    {0, 7, 7},   {4, 1, 1},   {1, 15, 1},
            {17, 999, 1}, {15, 18, 3}, {18, 15, 3}, {60, 84, 12},
        };

        for (int i = 0; i < countof(t); i++) {
            Z_ASSERT_EQ(
                t[i].gcd, gcd_euclid(t[i].i, t[i].j), "EUCLID: GCD(%u, %u)",
                t[i].i, t[i].j
            );
            Z_ASSERT_EQ(
                t[i].gcd, gcd_stein(t[i].i, t[i].j), "STEIN: GCD(%u, %u)",
                t[i].i, t[i].j
            );
        }
    }
    Z_TEST_END

    Z_TEST(multiples, "Multiples count in a range") {
        /* Multiples of 5 between 0 and 100 */
        Z_ASSERT_EQ(get_multiples_nb_in_range(5, 0, 100), 21U);

        /* Multiples of 5 between 1 and 100 */
        Z_ASSERT_EQ(get_multiples_nb_in_range(5, 1, 100), 20U);

        /* Multiples of 12 between 22 and 25 */
        Z_ASSERT_EQ(get_multiples_nb_in_range(12, 22, 25), 1U);

        /* Multiples of 12 between 25 and 28 */
        Z_ASSERT_EQ(get_multiples_nb_in_range(12, 25, 28), 0U);

        /* Multiples of 1000 between 1 and 2 */
        Z_ASSERT_EQ(get_multiples_nb_in_range(1000, 1, 2), 0U);

        /* Multiples of 1000 between 7598 and 125829 */
        Z_ASSERT_EQ(get_multiples_nb_in_range(1000, 7598, 125829), 118U);
    }
    Z_TEST_END
}
Z_GROUP_END

/* }}} */
