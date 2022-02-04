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

#ifndef IS_LIB_COMMON_ARITH_H
#define IS_LIB_COMMON_ARITH_H

#include <lib-common/core.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#include "arith/endianess.h"
#include "arith/cmp.h"
#include "arith/float.h"
#include "arith/str.h"
#include "arith/scan.h"

unsigned gcd(unsigned a, unsigned b);
unsigned gcd_euclid(unsigned a, unsigned b);
unsigned gcd_stein(unsigned a, unsigned b);

/** Count the number of multiples of a number in a range.
 *
 * Count the number of multiples of a number 'n' in the range 'min' --> 'max'
 * (min and max included).
 *
 * \param[in]  n   The number 'n' whose we are counting the multiples.
 * \param[in]  min The lower inclusive boundary of the range.
 * \param[in]  max The upper inclusive boundary of the range.
 *
 * \return  The number of multiples of 'n' in the range min --> max.
 */
uint32_t get_multiples_nb_in_range(uint32_t n, uint32_t min, uint32_t max);

extern uint64_t const powerof10[16];

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
