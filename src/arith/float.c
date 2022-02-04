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

#include <math.h>
#include <float.h>

#include <lib-common/arith.h>
#include <lib-common/z.h>

double double_round(double val, uint8_t precision)
{
    double val_floor;

    if (isinf(val) || isnan(val)) {
        return val;
    }

    val_floor = floor(val);
    if (!expect(precision < countof(powerof10))) {
        return val;
    }

    val -= val_floor;
    val *= powerof10[precision];
    val  = round(val);
    val /= powerof10[precision];

    return val + val_floor;
}

double double_round_significant(double d, uint8_t precision)
{
    double base = round(d);
    uint64_t ubase = (uint64_t)(base < 0 ? -base : base);
    size_t base_nb_digits = 0;

    if (!expect(precision < countof(powerof10))) {
        return d;
    }
    assert (precision != 0);

    /* powerof10[X-1] is the smallest number with precision X */
    if (ubase >= powerof10[precision - 1]) {
        return base;
    }

    while (ubase >= powerof10[base_nb_digits]) {
        base_nb_digits++;
    }

    return double_round(d, precision - base_nb_digits);
}

/* {{{ Tests */

Z_GROUP_EXPORT(arithfloat)
{
    Z_TEST(double_round, "double_round") {
#define T(val, precision, res)  \
    Z_ASSERT_LT(fabs(double_round(val, precision) - res), DBL_EPSILON)

        T(12.1234567, 0, 12.);
        T(12.1234567, 1, 12.1);
        T(12.1234567, 2, 12.12);
        T(12.1234567, 3, 12.123);
        T(12.1234567, 4, 12.1235);
        T(12.1234567, 5, 12.12346);
        T(12.1234567, 6, 12.123457);
        T(12.1234567, 7, 12.1234567);
        T(12.1234567, 8, 12.1234567);
        T(12.12345,   4, 12.1235);

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
        T(-12.12345,   4, -12.1234);

        T(-12.6, 0, -13.);
#undef T
        Z_ASSERT_NE(isinf(double_round(INFINITY, 3)), 0);
        Z_ASSERT_NE(isinf(double_round(-INFINITY, 3)), 0);
        Z_ASSERT_NE(isnan(double_round(NAN, 3)), 0);
    } Z_TEST_END

    Z_TEST(double_round_significant, "double_round_significant") {
#define T(v, p, res)  \
    Z_ASSERT_LT(fabs(double_round_significant(v, p) - res), DBL_EPSILON)

        T(12.1234567,  1, 12.);
        T(12.1234567,  2, 12.);
        T(12.1234567,  3, 12.1);
        T(12.1234567,  4, 12.12);
        T(12.1234567,  5, 12.123);
        T(12.1234567,  6, 12.1235);
        T(12.1234567,  7, 12.12346);
        T(12.1234567,  8, 12.123457);
        T(12.1234567,  9, 12.1234567);
        T(12.1234567, 10, 12.1234567);
        T(12.12345,    6, 12.1235);

        T(12.6, 2, 13.);

        T(1234.567,  2, 1235.);
        T(12345.67,  5, 12346.);
        T(1234567.8, 6, 1234568.);

        T(-12.1234567,  1, -12.);
        T(-12.1234567,  2, -12.);
        T(-12.1234567,  3, -12.1);
        T(-12.1234567,  4, -12.12);
        T(-12.1234567,  5, -12.123);
        T(-12.1234567,  6, -12.1235);
        T(-12.1234567,  7, -12.12346);
        T(-12.1234567,  8, -12.123457);
        T(-12.1234567,  9, -12.1234567);
        T(-12.1234567, 10, -12.1234567);
        T(-12.12345,    6, -12.1234);

        T(-12.6, 2, -13.);

        T(-1234.567,  2, -1235.);
        T(-12345.67,  5, -12346.);
        T(-1234567.8, 6, -1234568.);

        T(10.23,  2, 10.);
        T(10.23,  3, 10.2);
        T( 9.23,  2, 9.2);
        T( 9.23,  1, 9.);
#undef T
    } Z_TEST_END
} Z_GROUP_END

/* }}} */
