/***************************************************************************/
/*                                                                         */
/* Copyright 2024 INTERSEC SA                                              */
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

#include <lib-common/core.h>
#include <lib-common/zbenchmark.h>

static size_t membitcount_check_small(size_t (*fn)(const void *, size_t))
{
    static uint8_t v[64] = {
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
        1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8,
    };
    size_t res = 0;

    for (int i = 0; i < countof(v); i++) {
        for (int j = i; j < countof(v); j++) {
            res += fn(v + i, j - i);
        }
    }

    return res;
}

static int membitcount_check_big(size_t (*fn)(const void *, size_t))
{
#define N (1U << 12)
    char v[N];
    size_t res = 0;

    for (size_t i = 0; i < N; i++)
        v[i] = i;
    for (size_t i = 0; i < 32; i++) {
        res += fn(v + i, N - i);
    }
    for (size_t i = 0; i < 32; i++) {
        res += fn(v, N - i);
    }

    return res;
#undef N
}

static size_t membitcount_naive(const void *_p, size_t n)
{
    const uint8_t *p = _p;
    size_t res = 0;

    for (size_t i = 0; i < n; i++) {
        res += bitcount8(p[i]);
    }
    return res;
}

ZBENCH_GROUP_EXPORT(bithacks) {
    const size_t small_res = 71008;
    const size_t big_res = 1044608;

    /* {{{ Naive */

    ZBENCH(membitcount_naive_small) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_small(&membitcount_naive);
            } ZBENCH_MEASURE_END

            if (res != small_res) {
                e_fatal("expected: %zu, got: %zu", small_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    ZBENCH(membitcount_naive_big) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_big(&membitcount_naive);
            } ZBENCH_MEASURE_END

            if (res != big_res) {
                e_fatal("expected: %zu, got: %zu", big_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    /* }}} */
    /* {{{ C */

    ZBENCH(membitcount_c_small) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_small(&membitcount_c);
            } ZBENCH_MEASURE_END

            if (res != small_res) {
                e_fatal("expected: %zu, got: %zu", small_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    ZBENCH(membitcount_c_big) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_big(&membitcount_c);
            } ZBENCH_MEASURE_END

            if (res != big_res) {
                e_fatal("expected: %zu, got: %zu", big_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    /* }}} */
    /* {{{ SSSE3 */

    ZBENCH(membitcount_ssse3_small) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_small(&membitcount_ssse3);
            } ZBENCH_MEASURE_END

            if (res != small_res) {
                e_fatal("expected: %zu, got: %zu", small_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    ZBENCH(membitcount_ssse3_big) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_big(&membitcount_ssse3);
            } ZBENCH_MEASURE_END

            if (res != big_res) {
                e_fatal("expected: %zu, got: %zu", big_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    /* }}} */
    /* {{{ Popcnt */

    ZBENCH(membitcount_popcnt_small) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_small(&membitcount_popcnt);
            } ZBENCH_MEASURE_END

            if (res != small_res) {
                e_fatal("expected: %zu, got: %zu", small_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    ZBENCH(membitcount_popcnt_big) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_big(&membitcount_popcnt);
            } ZBENCH_MEASURE_END

            if (res != big_res) {
                e_fatal("expected: %zu, got: %zu", big_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

/* }}} */
    /* {{{ Auto deduction */

    ZBENCH(membitcount_auto_small) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_small(membitcount);
            } ZBENCH_MEASURE_END

            if (res != small_res) {
                e_fatal("expected: %zu, got: %zu", small_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

    ZBENCH(membitcount_auto_big) {
        ZBENCH_LOOP() {
            size_t res = 0;

            ZBENCH_MEASURE() {
                res = membitcount_check_big(membitcount);
            } ZBENCH_MEASURE_END

            if (res != big_res) {
                e_fatal("expected: %zu, got: %zu", big_res, res);
            }
        } ZBENCH_LOOP_END
    } ZBENCH_END

/* }}} */
} ZBENCH_GROUP_END
