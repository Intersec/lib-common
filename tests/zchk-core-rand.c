/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#include <lib-common/z.h>

Z_GROUP_EXPORT(core_havege) {
    Z_TEST(havege_range, "havege_range") {
        int number1;
        int numbers[10000];
        bool is_different_than_int_min = false;

        /* Test bug that existed in rand_range when INT_MIN was given, the
         * results were always INT_MIN.
         */
        for (int i = 0; i < 10000; i++) {
            numbers[i] = rand_range(INT_MIN, INT_MAX);
        }
        for (int i = 0; i < 10000; i++) {
            if (numbers[i] != INT_MIN) {
                is_different_than_int_min = true;
            }
        }
        Z_ASSERT(is_different_than_int_min);

        number1 = rand_range(INT_MIN + 1, INT_MAX - 1);
        Z_ASSERT(number1 > INT_MIN && number1 < INT_MAX);
        number1 = rand_range(-10, 10);
        Z_ASSERT(number1 >= -10 && number1 <= 10);
    } Z_TEST_END;
} Z_GROUP_END;

/* LCOV_EXCL_STOP */
