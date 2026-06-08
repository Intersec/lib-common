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

#include <lib-common/arith.h>
#include <lib-common/z.h>

uint64_t const powerof10[16] = {
    1LL,
    10LL,
    100LL,
    1000LL,
    10000LL,
    100000LL,
    1000000LL,
    10000000LL,
    100000000LL,
    1000000000LL,
    10000000000LL,
    100000000000LL,
    1000000000000LL,
    10000000000000LL,
    100000000000000LL,
    1000000000000000LL,
};

/* {{{ GCD */

/* Note about GCD algorithms:
 *
 * Stein's algorithm is significantly better than Euclid's one for lower
 * values (the switch is located around 1M on a 2009 quad core). For greater
 * values, Euclid's algorithm seems to take advantage of the low-level
 * optimization of modulo operator.
 *
 * We suppose that most basic usages of GCD will be alright with Stein's
 * algorithm.
 *
 */

uint32_t gcd_euclid(uint32_t a, uint32_t b)
{
    if (a < b) {
        SWAP(uint32_t, a, b);
    }

    while (b) {
        a = a % b;
        SWAP(uint32_t, a, b);
    }

    return a;
}

uint32_t gcd_stein(uint32_t a, uint32_t b)
{
    uint8_t za;
    uint8_t zb;

    if (!a) {
        return b;
    }
    if (!b) {
        return a;
    }

    za = bsf32(a);
    a >>= za;
    zb = bsf32(b);
    b >>= zb;

    while (a != b) {
        if (a > b) {
            a -= b;
            a >>= bsf32(a);
        } else {
            b -= a;
            b >>= bsf32(b);
        }
    }

    return a << MIN(za, zb);
}

uint32_t gcd(uint32_t a, uint32_t b)
{
    return gcd_stein(a, b);
}

/* }}} */

uint32_t get_multiples_nb_in_range(uint32_t n, uint32_t min, uint32_t max)
{
    if (!expect(max >= min && n != 0)) {
        return 0;
    }
    return 1 + (max / n) - DIV_ROUND_UP(min, n);
}
