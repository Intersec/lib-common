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

#include <lib-common/arith.h>

/* This bench calculate GCD of every combination of integer of a given
 * interval using Euclid's and Stein's algorithms.
 *
 * 1/ Launch bench:
 *     $ perf record ./gcd-bench 5000 10000
 *
 * 2/ Show bench result:
 *     $ perf report
 */

int main(int argc, char **argv)
{
    int min;
    int max;

    if (argc <= 1) {
        fprintf(stderr, "usage: %s [min] max\n", argv[0]);
        return -1;
    }

    if (argc <= 2) {
        min = 1;
        max = atoi(argv[1]);

        if (max < 1) {
            fprintf(stderr, "error: max < 1 (max = %d)\n", max);
            return -1;
        }
    } else {
        min = atoi(argv[1]);
        max = atoi(argv[2]);
    }

    if (min < 1) {
        fprintf(stderr, "error: min < 1 (min = %d)\n", min);
        return -1;
    }

    if (min > max) {
        fprintf(stderr, "error: min > max (min = %d, max = %d)\n", min, max);
        return -1;
    }

    for (int i = min; i < max; i++) {
        for (int j = i; j < max; j++) {
            if (gcd_euclid(i, j) != gcd_stein(i, j)) {
                return -1;
            }
        }
    }

    return 0;
}
