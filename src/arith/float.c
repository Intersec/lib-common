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

#include <lib-common/arith.h>

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
    val = round(val);
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
    assert(precision != 0);

    /* powerof10[X-1] is the smallest number with precision X */
    if (ubase >= powerof10[precision - 1]) {
        return base;
    }

    while (ubase >= powerof10[base_nb_digits]) {
        base_nb_digits++;
    }

    return double_round(d, precision - base_nb_digits);
}

bool double_is_close(double d1, double d2, double rel_tol, double abs_tol)
{
    THROW_IF(d1 == d2, true);
    THROW_FALSE_IF(isinf(d1));
    THROW_FALSE_IF(isinf(d2));

    return fabs(d2 - d1) <= MAX(abs_tol, rel_tol * MAX(fabs(d2), fabs(d1)));
}
