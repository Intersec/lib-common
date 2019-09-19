/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
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

#include "sort.h"
#include "z.h"

static int u64_cmp(const void *a, const void *b, void *arg)
{
    const uint64_t *ua = a;
    const uint64_t *ub = b;

    return CMP(*ua, *ub);
}

typedef struct u64_del_t {
    uint64_t *tab;
    int       len;
} u64_del_t;

static void u64_del(void *v, void *arg)
{
    uint64_t *uv = v;
    u64_del_t *del = arg;

    del->tab[del->len++] = *uv;
}

Z_GROUP_EXPORT(sort) {
    static const uint64_t vals64[] =
        { 8, 8, 1, 2, 4, 4, 12, 5, 3, 7, 10, 1, 4, 1, 12, 12 };
    static const uint64_t sorted64[countof(vals64) + 1][countof(vals64)] = {
        {  },
        { 8 },
        { 8, 8 },
        { 1, 8, 8 },
        { 1, 2, 8, 8 },
        { 1, 2, 4, 8, 8 },
        { 1, 2, 4, 4, 8, 8 },
        { 1, 2, 4, 4, 8, 8, 12 },
        { 1, 2, 4, 4, 5, 8, 8, 12 },
        { 1, 2, 3, 4, 4, 5, 8, 8, 12 },
        { 1, 2, 3, 4, 4, 5, 7, 8, 8, 12 },
        { 1, 2, 3, 4, 4, 5, 7, 8, 8, 10, 12 },
        { 1, 1, 2, 3, 4, 4, 5, 7, 8, 8, 10, 12 },
        { 1, 1, 2, 3, 4, 4, 4, 5, 7, 8, 8, 10, 12 },
        { 1, 1, 1, 2, 3, 4, 4, 4, 5, 7, 8, 8, 10, 12 },
        { 1, 1, 1, 2, 3, 4, 4, 4, 5, 7, 8, 8, 10, 12, 12 },
        { 1, 1, 1, 2, 3, 4, 4, 4, 5, 7, 8, 8, 10, 12, 12, 12 }
    };
    static const uint64_t uniqed64[countof(vals64) + 1][countof(vals64) + 1] =
    {
        { 0,  },
        { 1, 8 },
        { 1, 8 },
        { 2, 1, 8 },
        { 3, 1, 2, 8 },
        { 4, 1, 2, 4, 8 },
        { 4, 1, 2, 4, 8 },
        { 5, 1, 2, 4, 8, 12 },
        { 6, 1, 2, 4, 5, 8, 12 },
        { 7, 1, 2, 3, 4, 5, 8, 12 },
        { 8, 1, 2, 3, 4, 5, 7, 8, 12 },
        { 9, 1, 2, 3, 4, 5, 7, 8, 10, 12 },
        { 9, 1, 2, 3, 4, 5, 7, 8, 10, 12 },
        { 9, 1, 2, 3, 4, 5, 7, 8, 10, 12 },
        { 9, 1, 2, 3, 4, 5, 7, 8, 10, 12 },
        { 9, 1, 2, 3, 4, 5, 7, 8, 10, 12 },
        { 9, 1, 2, 3, 4, 5, 7, 8, 10, 12 }
    };
    static const uint64_t deleted64[countof(vals64) + 1][countof(vals64) + 1] =
    {
        { 0 },
        { 0 },
        { 1, 8 },
        { 1, 8 },
        { 1, 8 },
        { 1, 8 },
        { 2, 4, 8 },
        { 2, 4, 8 },
        { 2, 4, 8,},
        { 2, 4, 8 },
        { 2, 4, 8 },
        { 2, 4, 8 },
        { 3, 1, 4, 8 },
        { 4, 1, 4, 4, 8 },
        { 5, 1, 1, 4, 4, 8 },
        { 6, 1, 1, 4, 4, 8, 12 },
        { 7, 1, 1, 4, 4, 8, 12, 12 }
    };

    Z_TEST(64, "optimized 64") {
        for (int i = 0; i < countof(vals64); i++) {
            uint64_t v[countof(vals64)];
            size_t   len = 0;

            p_copy(v, vals64, countof(vals64));
            dsort64(v, i);
            Z_ASSERT_EQUAL(sorted64[i], i, v, i);
            len = uniq64(v, i);
            Z_ASSERT_EQUAL(&uniqed64[i][1], uniqed64[i][0], v, len);

            for (uint64_t j = 0; j < 15; j++) {
                bool   found;
                size_t pos = (bisect64)(j, v, len, &found);
                size_t scan_pos = 0;

                while (scan_pos < len && v[scan_pos] < j) {
                    scan_pos++;
                }
                Z_ASSERT_EQ(pos, scan_pos);

                Z_ASSERT_LE(pos, len);
                if (pos == len) {
                    Z_ASSERT(!contains64(j, v, len));
                    Z_ASSERT(!found);
                    if (len != 0) {
                        Z_ASSERT_LT(v[len -1], j);
                    }
                } else {
                    Z_ASSERT_GE(v[pos], j);
                    Z_ASSERT_EQ(v[pos] == j, contains64(j, v, len));
                    Z_ASSERT_EQ(v[pos] == j, found);
                }
            }
        }
    } Z_TEST_END;

    Z_TEST(32, "optimized 32") {
        uint32_t vals32[countof(vals64)];

        for (int i = 0; i < countof(vals64); i++) {
            vals32[i] = vals64[i];
        }

        for (int i = 0; i < countof(vals32); i++) {
            uint32_t sorted32[countof(vals64)];
            uint32_t uniqed32[countof(vals64)];
            uint32_t v[countof(vals32)];
            size_t   len = 0;

            for (int j = 0; j < countof(vals64); j++) {
                sorted32[j] = sorted64[i][j];
                uniqed32[j] = uniqed64[i][j + 1];
            }

            p_copy(&v, &vals32, 1);
            dsort32(v, i);
            Z_ASSERT_EQUAL(sorted32, i, v, i);
            len = uniq32(v, i);
            Z_ASSERT_EQUAL(uniqed32, uniqed64[i][0], v, len);

            for (uint64_t j = 0; j < 15; j++) {
                bool   found;
                size_t pos = (bisect32)(j, v, len, &found);
                size_t scan_pos = 0;

                while (scan_pos < len && v[scan_pos] < j) {
                    scan_pos++;
                }
                Z_ASSERT_EQ(pos, scan_pos);

                Z_ASSERT_LE(pos, len);
                if (pos == len) {
                    Z_ASSERT(!contains32(j, v, len));
                    Z_ASSERT(!found);
                    if (len != 0) {
                        Z_ASSERT_LT(v[len -1], j);
                    }
                } else {
                    Z_ASSERT_GE(v[pos], j);
                    Z_ASSERT_EQ(v[pos] == j, contains32(j, v, len));
                    Z_ASSERT_EQ(v[pos] == j, found);
                }
            }
        }
    } Z_TEST_END;

    Z_TEST(16, "optimized 16") {
        uint16_t vals16[countof(vals64)];

        for (int i = 0; i < countof(vals64); i++) {
            vals16[i] = vals64[i];
        }

        for (int i = 0; i < countof(vals16); i++) {
            uint16_t sorted16[countof(vals64)];
            uint16_t uniqed16[countof(vals64)];
            uint16_t v[countof(vals16)];
            size_t   len = 0;

            for (int j = 0; j < countof(vals64); j++) {
                sorted16[j] = sorted64[i][j];
                uniqed16[j] = uniqed64[i][j + 1];
            }

            p_copy(&v, &vals16, 1);
            dsort16(v, i);
            Z_ASSERT_EQUAL(sorted16, i, v, i);
            len = uniq16(v, i);
            Z_ASSERT_EQUAL(uniqed16, uniqed64[i][0], v, len);

            for (uint64_t j = 0; j < 15; j++) {
                bool   found;
                size_t pos = (bisect16)(j, v, len, &found);
                size_t scan_pos = 0;

                while (scan_pos < len && v[scan_pos] < j) {
                    scan_pos++;
                }
                Z_ASSERT_EQ(pos, scan_pos);

                Z_ASSERT_LE(pos, len);
                if (pos == len) {
                    Z_ASSERT(!contains16(j, v, len));
                    Z_ASSERT(!found);
                    if (len != 0) {
                        Z_ASSERT_LT(v[len -1], j);
                    }
                } else {
                    Z_ASSERT_GE(v[pos], j);
                    Z_ASSERT_EQ(v[pos] == j, contains16(j, v, len));
                    Z_ASSERT_EQ(v[pos] == j, found);
                }
            }
        }
    } Z_TEST_END;

    Z_TEST(8, "optimized 8") {
        uint8_t vals8[countof(vals64)];

        for (int i = 0; i < countof(vals64); i++) {
            vals8[i] = vals64[i];
        }

        for (int i = 0; i < countof(vals8); i++) {
            uint8_t sorted8[countof(vals64)];
            uint8_t uniqed8[countof(vals64)];
            uint8_t v[countof(vals8)];
            size_t   len = 0;

            for (int j = 0; j < countof(vals64); j++) {
                sorted8[j] = sorted64[i][j];
                uniqed8[j] = uniqed64[i][j + 1];
            }

            p_copy(&v, &vals8, 1);
            dsort8(v, i);
            Z_ASSERT_EQUAL(sorted8, i, v, i);
            len = uniq8(v, i);
            Z_ASSERT_EQUAL(uniqed8, uniqed64[i][0], v, len);

            for (uint64_t j = 0; j < 15; j++) {
                bool   found;
                size_t pos = (bisect8)(j, v, len, &found);
                size_t scan_pos = 0;

                while (scan_pos < len && v[scan_pos] < j) {
                    scan_pos++;
                }
                Z_ASSERT_EQ(pos, scan_pos);

                Z_ASSERT_LE(pos, len);
                if (pos == len) {
                    Z_ASSERT(!contains8(j, v, len));
                    Z_ASSERT(!found);
                    if (len != 0) {
                        Z_ASSERT_LT(v[len -1], j);
                    }
                } else {
                    Z_ASSERT_GE(v[pos], j);
                    Z_ASSERT_EQ(v[pos] == j, contains8(j, v, len));
                    Z_ASSERT_EQ(v[pos] == j, found);
                }
            }
        }
    } Z_TEST_END;

    Z_TEST(generic, "generic implementation") {
        for (int i = 0; i < countof(vals64); i++) {
            uint64_t v[countof(vals64)];
            uint64_t d[countof(vals64)];
            size_t   len = 0;
            u64_del_t del = {
                .tab = d,
                .len = 0,
            };

            p_copy(v, vals64, countof(vals64));
            dsort64(v, i);
            Z_ASSERT_EQUAL(sorted64[i], i, v, i);
            len = uniq(v, 8, i, &u64_cmp, NULL, &u64_del, &del);
            Z_ASSERT_EQUAL(&uniqed64[i][1], uniqed64[i][0], v, len);
            Z_ASSERT_EQUAL(&deleted64[i][1], deleted64[i][0], del.tab,
                           del.len);

            for (uint64_t j = 0; j < 15; j++) {
                bool   found;
                size_t pos = bisect(&j, v, 8, len, &found, &u64_cmp, NULL);
                size_t scan_pos = 0;

                while (scan_pos < len && v[scan_pos] < j) {
                    scan_pos++;
                }
                Z_ASSERT_EQ(pos, scan_pos);

                Z_ASSERT_LE(pos, len);
                if (pos == len) {
                    Z_ASSERT(!contains(&j, v, 8, len, &u64_cmp, NULL));
                    Z_ASSERT(!found);
                    if (len != 0) {
                        Z_ASSERT_LT(v[len -1], j);
                    }
                } else {
                    Z_ASSERT_GE(v[pos], j);
                    Z_ASSERT_EQ(v[pos] == j,
                                contains(&j, v, 8, len, &u64_cmp, NULL));
                    Z_ASSERT_EQ(v[pos] == j, found);
                }
            }
        }
    } Z_TEST_END;

#define Z_TEST_DSORT_IX(x)                                                   \
    Z_TEST(dsort_i##x, "dsort_i" #x) {                                       \
        t_scope;                                                             \
        int len = 1024;                                                      \
        int##x##_t *tab1 = t_new(int##x##_t, len);                           \
        int##x##_t *tab2 = t_new(int##x##_t, len);                           \
                                                                             \
        for (int i = 0; i < len; i++) {                                      \
            tab1[i] = MAKE64(mrand48(), mrand48());                          \
        }                                                                    \
        p_copy(tab2, tab1, len);                                             \
        dsort_i##x(tab1, len);                                               \
                                                                             \
        for (int i = 0; i < len - 1; i++) {                                  \
            Z_ASSERT_LE(tab1[i], tab1[i + 1],                                \
                        "(int" #x ") the array isn't sorted (i=%d)", i);     \
        }                                                                    \
                                                                             \
        /* Check that all the values we put in tab1 are still here. We       \
         * consider that dsortx works properly. */                           \
        dsort##x((uint##x##_t *)tab1, len);                                  \
        dsort##x((uint##x##_t *)tab2, len);                                  \
        for (int i = 0; i < len - 1; i++) {                                  \
            Z_ASSERT_EQ(tab1[i], tab2[i],                                    \
                        "(int" #x ") the sort changed the array content "    \
                        "(i=%d)", i);                                        \
        }                                                                    \
    } Z_TEST_END

    Z_TEST_DSORT_IX(8);
    Z_TEST_DSORT_IX(16);
    Z_TEST_DSORT_IX(32);
    Z_TEST_DSORT_IX(64);
} Z_GROUP_END;

/* LCOV_EXCL_STOP */
