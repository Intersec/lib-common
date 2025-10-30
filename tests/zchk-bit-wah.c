/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
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

#include <lib-common/z.h>
#include <lib-common/bit-wah.h>

/* LCOV_EXCL_START */

#define Z_WAH_BITS_IN_BUCKETS  10000 * WAH_BIT_IN_WORD

Z_GROUP_EXPORT(wah) {
    /* Have a smaller value of bits_in_bucket for tests to stress the buckets
     * code. */
    wah_set_bits_in_bucket(Z_WAH_BITS_IN_BUCKETS);

    Z_TEST(simple) { /* {{{ */
        wah_t map;

        wah_init(&map);
        wah_add0s(&map, 3);
        for (int i = 0; i < 3; i++) {
            Z_ASSERT(!wah_get(&map, i), "bad bit at offset %d", i);
        }
        Z_ASSERT(!wah_get(&map, 3), "bad bit at offset 3");

        wah_not(&map);
        for (int i = 0; i < 3; i++) {
            Z_ASSERT(wah_get(&map, i), "bad bit at offset %d", i);
        }
        Z_ASSERT(!wah_get(&map, 3), "bad bit at offset 3");
        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(fill) { /* {{{ */
        wah_t map;

        wah_init(&map);

        STATIC_ASSERT(sizeof(wah_word_t) == sizeof(uint32_t));
        STATIC_ASSERT(sizeof(wah_header_t) == sizeof(uint32_t));

        wah_add0s(&map, 63);
        for (int i = 0; i < 2 * 63; i++) {
            Z_ASSERT(!wah_get(&map, i), "bad bit at %d", i);
        }

        wah_add0s(&map, 3 * 63);
        for (int i = 0; i < 5 * 63; i++) {
            Z_ASSERT(!wah_get(&map, i), "bad bit at %d", i);
        }

        wah_reset_map(&map);
        wah_add1s(&map, 63);
        for (int i = 0; i < 2 * 63; i++) {
            bool bit = wah_get(&map, i);

            Z_ASSERT(!(i < 63 && !bit), "bad bit at %d", i);
            Z_ASSERT(!(i >= 63 && bit), "bad bit at %d", i);
        }
        wah_add1s(&map, 3 * 63);
        for (int i = 0; i < 5 * 63; i++) {
            bool bit = wah_get(&map, i);

            Z_ASSERT(!(i < 4 * 63 && !bit), "bad bit at %d", i);
            Z_ASSERT(!(i >= 4 * 63 && bit), "bad bit at %d", i);
        }

        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(set_bitmap) { /* {{{ */
        wah_t map;
        wah_t map2;
        const byte data[] = {
            0x1f, 0x00, 0x00, 0x8c, /* 0, 1, 2, 3, 4, 26, 27, 31 (32)  */
            0xff, 0xff, 0xff, 0xff, /* 32 -> 63                  (64)  */
            0xff, 0xff, 0xff, 0xff, /* 64 -> 95                  (96)  */
            0xff, 0xff, 0xff, 0x80, /* 96 -> 119, 127            (128) */
            0x00, 0x10, 0x40, 0x00, /* 140, 150                  (160) */
            0x00, 0x00, 0x00, 0x00, /*                           (192) */
            0x00, 0x00, 0x00, 0x00, /*                           (224) */
            0x00, 0x00, 0x00, 0x00, /*                           (256) */
            0x00, 0x00, 0x00, 0x21  /* 280, 285                  (288) */
        };

        uint64_t bc;
        pstream_t ps;

        wah_init(&map);
        wah_add(&map, data, bitsizeof(data));
        bc = membitcount(data, sizeof(data));

        Z_ASSERT_EQ(map.len, bitsizeof(data));

        ps = ps_init(map._buckets.tab[0].tab,
                     map._buckets.tab[0].len * sizeof(wah_word_t));
        Z_ASSERT_P(wah_init_from_data(&map2, ps));
        Z_ASSERT_EQ(map.len, map2.len);

        Z_ASSERT_EQ(map.active, bc, "invalid bit count");
        Z_ASSERT_EQ(map2.active, bc, "invalid bit count");
        for (int i = 0; i < countof(data); i++) {
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!!(data[i] & (1 << j)), !!wah_get(&map, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
                Z_ASSERT_EQ(!!(data[i] & (1 << j)), !!wah_get(&map2, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }

        wah_wipe(&map2);

        wah_not(&map);
        Z_ASSERT_EQ(map.active, bitsizeof(data) - bc, "invalid bit count");
        for (int i = 0; i < countof(data); i++) {
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!(data[i] & (1 << j)), !!wah_get(&map, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }

        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(for_each) { /* {{{ */
        wah_t map;
        const byte data[] = {
            0x1f, 0x00, 0x00, 0x8c, /* 0, 1, 2, 3, 4, 26, 27, 31 (32) */
            0xff, 0xff, 0xff, 0xff, /* 32 -> 63                  (64) */
            0xff, 0xff, 0xff, 0xff, /* 64 -> 95                  (96) */
            0xff, 0xff, 0xff, 0x80, /* 96 -> 119, 127            (128)*/
            0x00, 0x10, 0x40, 0x00, /* 140, 150                  (160)*/
            0x00, 0x00, 0x00, 0x00, /*                           (192)*/
            0x00, 0x00, 0x00, 0x00, /*                           (224)*/
            0x00, 0x00, 0x00, 0x00, /*                           (256)*/
            0x00, 0x00, 0x00, 0x21, /* 280, 285                  (288)*/
            0x12, 0x00, 0x10,       /* 289, 292, 308 */
        };

        uint64_t bc;
        uint64_t nbc;
        uint64_t c;
        uint64_t previous;

        wah_init(&map);
        wah_add(&map, data, bitsizeof(data));
        bc  = membitcount(data, sizeof(data));
        nbc = bitsizeof(data) - bc;

        Z_ASSERT_EQ(map.active, bc, "invalid bit count");
        c = 0;
        previous = 0;
        wah_for_each_1(en, &map) {
            if (c != 0) {
                Z_ASSERT_CMP(previous, <, en.key, "misordered enumeration");
            }
            previous = en.key;
            c++;
            Z_ASSERT_CMP(en.key, <, bitsizeof(data), "enumerate too far");
            Z_ASSERT(data[en.key >> 3] & (1 << (en.key & 0x7)),
                     "bit %d is not set", (int)en.key);
        }
        Z_ASSERT_EQ(c, bc, "bad number of enumerated entries");

        c = 0;
        previous = 0;
        wah_for_each_0(en, &map) {
            if (c != 0) {
                Z_ASSERT_CMP(previous, <, en.key, "misordered enumeration");
            }
            previous = en.key;
            c++;
            Z_ASSERT_CMP(en.key, <, bitsizeof(data), "enumerate too far");
            Z_ASSERT(!(data[en.key >> 3] & (1 << (en.key & 0x7))),
                     "bit %d is set", (int)en.key);
        }
        Z_ASSERT_EQ(c, nbc, "bad number of enumerated entries");
        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(binop) { /* {{{ */
        wah_t map1;
        wah_t map2;
        wah_t map3;
        const wah_t *vec[] = { &map1, &map2 };

        const byte data1[] = {
            0x1f, 0x00, 0x00, 0x8c, /* 0, 1, 2, 3, 4, 26, 27, 31 (32) */
            0xff, 0xff, 0xff, 0xff, /* 32 -> 63                  (64) */
            0xff, 0xff, 0xff, 0xff, /* 64 -> 95                  (96) */
            0xff, 0xff, 0xff, 0x80, /* 96 -> 119, 127            (128)*/
            0x00, 0x10, 0x40, 0x00, /* 140, 150                  (160)*/
            0x00, 0x00, 0x00, 0x00, /*                           (192)*/
            0x00, 0x00, 0x00, 0x00, /*                           (224)*/
            0x00, 0x00, 0x00, 0x00, /*                           (256)*/
            0x00, 0x00, 0x00, 0x21  /* 280, 285                  (288)*/
        };

        const byte data2[] = {
            0x00, 0x00, 0x00, 0x00, /*                                     (32) */
            0x00, 0x00, 0x00, 0x80, /* 63                                  (64) */
            0x00, 0x10, 0x20, 0x00, /* 76, 85                              (96) */
            0x00, 0x00, 0xc0, 0x20, /* 118, 119, 125                       (128)*/
            0xff, 0xfc, 0xff, 0x12  /* 128 -> 135, 138 -> 151, 153, 156    (160)*/
        };

        /* And result:
         *                                                                 (32)
         * 63                                                              (64)
         * 76, 85                                                          (96)
         * 118, 119                                                        (128)
         * 140, 150                                                        (160)
         */

        /* Or result:
         * 0 -> 4, 26, 27, 31                                              (32)
         * 32 -> 63                                                        (64)
         * 64 -> 95                                                        (96)
         * 96 -> 119, 125, 127                                             (128)
         * 128 -> 135, 138 -> 151, 153, 156                                (160)
         *                                                                 (192)
         *                                                                 (224)
         *                                                                 (256)
         * 280, 285                                                        (288)
         */

        /* And-Not result
         * 0, 1, 2, 3, 4, 26, 27, 31                                       (32)
         * 32 -> 62                                                        (64)
         * 64 -> 75, 77 -> 84, 86 -> 95                                    (96)
         *                                                                 (128)
         *                                                                 (160)
         *                                                                 (192)
         *                                                                 (224)
         *                                                                 (256)
         * 280, 285                                                        (288)
         */

        /* Not-And result
         *                                                                 (32)
         *                                                                 (64)
         *                                                                 (96)
         * 125                                                             (128)
         * 128 -> 135, 138, 139, 141 -> 149, 151, 153, 156                 (160)
         */

        wah_init(&map1);
        wah_init(&map2);
        wah_init(&map3);

        wah_add(&map1, data1, bitsizeof(data1));
        wah_add(&map2, data2, bitsizeof(data2));
        wah_and(&map1, &map2);
        for (int i = 0; i < countof(data1); i++) {
            byte b = data1[i];
            if (i < countof(data2)) {
                b &= data2[i];
            } else {
                b = 0;
            }
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!!(b & (1 << j)), !!wah_get(&map1, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }

        wah_reset_map(&map1);
        wah_add(&map1, data1, bitsizeof(data1));
        wah_multi_or(vec, countof(vec), &map3);
        wah_or(&map1, &map2);
        for (int i = 0; i < countof(data1); i++) {
            byte b = data1[i];
            if (i < countof(data2)) {
                b |= data2[i];
            }
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!!(b & (1 << j)), !!wah_get(&map1, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
                Z_ASSERT_EQ(!!(b & (1 << j)), !!wah_get(&map3, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }

        wah_reset_map(&map1);
        wah_add(&map1, data1, bitsizeof(data1));
        wah_and_not(&map1, &map2);
        for (int i = 0; i < countof(data1); i++) {
            byte b = data1[i];
            if (i < countof(data2)) {
                b &= ~data2[i];
            }
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!!(b & (1 << j)), !!wah_get(&map1, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }

        wah_reset_map(&map1);
        wah_add(&map1, data1, bitsizeof(data1));
        wah_not_and(&map1, &map2);
        for (int i = 0; i < countof(data1); i++) {
            byte b = ~data1[i];
            if (i < countof(data2)) {
                b &= data2[i];
            } else {
                b  = 0;
            }
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!!(b & (1 << j)), !!wah_get(&map1, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }

        wah_wipe(&map1);
        wah_wipe(&map2);
        wah_wipe(&map3);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(redmine_4576) { /* {{{ */
        wah_t map;
        const byte data[] = {
            0x1f, 0x00, 0x1f, 0x1f,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x1f, 0x1f, 0x1f, 0x1f,
            0x00, 0x00, 0x00, 0x00,
            0x1f, 0x1f, 0x1f, 0x1f,
            0x00, 0x00, 0x00, 0x00
        };

        wah_init(&map);
        wah_add(&map, data, bitsizeof(data));

        for (int i = 0; i < countof(data); i++) {
            for (int j = 0; j < 8; j++) {
                Z_ASSERT_EQ(!!(data[i] & (1 << j)), !!wah_get(&map, i * 8 + j),
                            "invalid byte %d, bit %d", i, j);
            }
        }
        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(redmine_9437) { /* {{{ */
        wah_t map;
        const uint32_t data = 0xbfffffff;

        wah_init(&map);

        wah_add0s(&map, 626 * 32);
        wah_add1s(&map, 32);
        wah_add(&map, &data, 32);

        for (int i = 0; i < 626; i++) {
            for (int j = 0; j < 32; j++) {
                Z_ASSERT(!wah_get(&map, i * 32 + j));
            }
        }
        for (int i = 626 * 32; i < 628 * 32; i++) {
            if (i != 628 * 32 - 2) {
                Z_ASSERT(wah_get(&map, i));
            } else {
                Z_ASSERT(!wah_get(&map, i));
            }
        }
        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(redmine_42990) { /* {{{ */
        wah_t map;
        uint32_t literal[] = { 0xff7fff7f, 0xffffffff, 0xf7fffdeb };

        wah_init(&map);

        /* This triggered an assert without the patch for #42990. */
        wah_add(&map, literal, 3 * WAH_BIT_IN_WORD);

        for (uint64_t i = 0; i < 3 * WAH_BIT_IN_WORD; i++) {
            Z_ASSERT_EQ(wah_get(&map, i), !!TST_BIT(literal, i));
        }

        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(non_reg_and) { /* {{{ */
        t_scope;
        uint32_t src_data[]   = { 0x00000519, 0x00000000, 0x80000101, 0x00000000 };
        uint32_t other_data[] = { 0x00000000, 0x00000002, 0x80000010, 0x00000003,
                                  0x0000001d, 0x00000001, 0x00007e00, 0x0000001e,
                                  0x00000000 };
        wah_t src;
        wah_t other;
        wah_t res;

        wah_init_from_data(&src, ps_init(src_data, sizeof(src_data)));
        src._pending = 0x1ffff;
        src.active   = 8241;
        src.len      = 50001;

        wah_init_from_data(&other, ps_init(other_data, sizeof(other_data)));
        other._pending = 0x600000;
        other.active  = 12;
        other.len     = 2007;

        wah_init(&res);
        wah_copy(&res, &src);
        wah_and(&res, &other);

        Z_ASSERT_EQ(res.len, 50001u);
        Z_ASSERT_LE(res.active, 12u);

        wah_wipe(&src);
        wah_wipe(&other);
        wah_wipe(&res);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(skip1s) { /* {{{ */
        wah_t map;
        uint64_t pos = 0;
        uint64_t bc;
        const byte data[] = {
            0x1f, 0x00, 0x00, 0x8c, /* 0, 1, 2, 3, 4, 26, 27, 31 (8  - 32) */
            0xff, 0xff, 0xff, 0xff, /* 32 -> 63                  (32 - 64) */
            0xff, 0xff, 0xff, 0xff, /* 64 -> 95                  (32 - 96) */
            0xff, 0xff, 0xff, 0x80, /* 96 -> 119, 127            (25 - 128)*/
            0x00, 0x10, 0x40, 0x00, /* 140, 150                  (2  - 160)*/
            0x00, 0x00, 0x00, 0x00, /*                           (0  - 192)*/
            0x00, 0x00, 0x00, 0x00, /*                           (0  - 224)*/
            0x00, 0x00, 0x00, 0x00, /*                           (0  - 256)*/
            0x00, 0x00, 0x00, 0x21, /* 280, 285                  (2  - 288)*/
            0x12, 0x00, 0x10,       /* 289, 292, 308             (3) */
        };

        wah_init(&map);
        wah_add(&map, data, bitsizeof(data));
        bc = membitcount(data, sizeof(data));

        wah_for_each_1(en, &map) {
            for (uint64_t i = pos; i < bc; i++) {
                wah_bit_enum_t en_skip = en;
                wah_bit_enum_t en_incr = en;

                for (uint64_t j = pos; j < i; j++) {
                    wah_bit_enum_next(&en_incr);
                }
                wah_bit_enum_skip1s(&en_skip, i - pos);
                Z_ASSERT_EQ(en_skip.word_en.state, en_incr.word_en.state,
                            "%ju %ju %ju", en.key, pos, i);
                if (en_skip.word_en.state != WAH_ENUM_END) {
                    Z_ASSERT_EQ(en_skip.key, en_incr.key);
                }
            }
            pos++;
        }

        wah_wipe(&map);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(nr_20150119) { /* {{{ */
        wah_t map1;
        wah_t map2;

        wah_init(&map1);
        wah_add0s(&map1, 84969209384ull);
        wah_add1s(&map1, 85038314623ull - 84969209384ull + 1ull);
        Z_ASSERT_EQ(85038314623ull + 1ull, map1.len);
        Z_ASSERT_EQ(85038314623ull - 84969209384ull + 1ull, map1.active);

        wah_init(&map2);
        wah_add0s(&map2, 21 * 32);

        wah_and_(&map1, &map2, false, true);
        Z_ASSERT_EQ(85038314623ull + 1ull, map1.len);
        Z_ASSERT_EQ(85038314623ull - 84969209384ull + 1ull, map1.active);

        wah_wipe(&map2);
        wah_wipe(&map1);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(nr_20150219) { /* {{{ */
        wah_t map1;
        wah_t map2;

        wah_init(&map1);
        wah_add1s(&map1, 68719476704ull * 2 + 11395279936ull + 31);
        Z_ASSERT_EQ(68719476704ull * 2 + 11395279936ull + 31, map1.len);
        Z_ASSERT_EQ(68719476704ull * 2 + 11395279936ull + 31, map1.active);

        wah_init(&map2);
        wah_add0s(&map2, 960);

        wah_and_(&map1, &map2, false, true);
        Z_ASSERT_EQ(68719476704ull * 2 + 11395279936ull + 31, map1.len);
        Z_ASSERT_EQ(68719476704ull * 2 + 11395279936ull + 31, map1.active);

        wah_wipe(&map2);
        wah_wipe(&map1);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(buckets) { /* {{{ */
        SB_1k(sb);
        wah_t map1;
        uint32_t literal[] = {
            0x12345678, 0x12345678, 0x12345678, 0x12345678,
            0x12345678, 0x00000001,
        };

        /* Set bits_in_bucket to a low value, and build a with multiple
         * buckets. */
        wah_set_bits_in_bucket(5 * WAH_BIT_IN_WORD);

        wah_init(&map1);
        wah_add0s(&map1, 5 * WAH_BIT_IN_WORD);
        wah_add1s(&map1, 5 * WAH_BIT_IN_WORD);
        wah_add0s(&map1, 5 * WAH_BIT_IN_WORD);

        wah_add(&map1, literal, 5 * WAH_BIT_IN_WORD + 2);

#define CHECK_WAH(_nb_buckets, _len)  \
        do {                                                                 \
            Z_ASSERT_EQ(map1._buckets.len, _nb_buckets);                     \
            Z_ASSERT_EQ(map1.len, _len);                                     \
            Z_ASSERT_EQ(map1.active, 5 * WAH_BIT_IN_WORD +                   \
                        membitcount(literal, countof(literal) * 4));         \
                                                                             \
            for (uint64_t i = 0; i < 3 * 5 * WAH_BIT_IN_WORD; i++) {         \
                if (i >= 5 * WAH_BIT_IN_WORD                                 \
                &&  i < 2 * 5 * WAH_BIT_IN_WORD)                             \
                {                                                            \
                    Z_ASSERT_EQ(wah_get(&map1, i), true);                    \
                } else {                                                     \
                    Z_ASSERT_EQ(wah_get(&map1, i), false);                   \
                }                                                            \
            }                                                                \
            for (uint64_t i = 0; i < 5 * WAH_BIT_IN_WORD + 2; i++) {         \
                Z_ASSERT_EQ(wah_get(&map1, i + 15 * WAH_BIT_IN_WORD),        \
                            !!TST_BIT(literal, i));                          \
            }                                                                \
        } while (0)

        /* There should be 4 buckets with pending data, so 5 after calling
         * wah_pad32. */
        CHECK_WAH(4, 4 * 5 * WAH_BIT_IN_WORD + 2);
        wah_pad32(&map1);
        CHECK_WAH(5, (4 * 5 + 1) * WAH_BIT_IN_WORD);

        /* Save the wah in a sb. */
        tab_for_each_ptr(bucket, &map1._buckets) {
            sb_add(&sb, bucket->tab, bucket->len * sizeof(wah_word_t));
        }
        wah_wipe(&map1);

        /* Reload it with the same value of bits_in_bucket, and check all the
         * buckets are statically loaded. */
        Z_ASSERT_P(wah_init_from_data(&map1, ps_initsb(&sb)));
        CHECK_WAH(5, (4 * 5 + 1) * WAH_BIT_IN_WORD);
        tab_for_each_ptr(bucket, &map1._buckets) {
            Z_ASSERT(bucket->mp == ipool(MEM_STATIC));
        }
        wah_wipe(&map1);

        /* Reload it with a lower value of bits_in_bucket; this will stress
         * the code of wah_init_from_data. */
        wah_set_bits_in_bucket(4 * WAH_BIT_IN_WORD);
        Z_ASSERT_P(wah_init_from_data(&map1, ps_initsb(&sb)));
        CHECK_WAH(6, (4 * 5 + 1) * WAH_BIT_IN_WORD);
        wah_wipe(&map1);

        wah_set_bits_in_bucket(Z_WAH_BITS_IN_BUCKETS);

#undef CHECK_WAH
    } Z_TEST_END;

    /* }}} */
    Z_TEST(t_wah_get_storage_lstr) { /* {{{ */
        t_scope;
        wah_t wah;
        uint64_t bits_pos[] = {
            1, 4, 5, 6, 7, 100000, 100001, 100010,
        };
        lstr_t storage;
        wah_t *wah_from_data;
        int pos;

        wah_init(&wah);
        carray_for_each_entry(bit, bits_pos) {
            wah_add1_at(&wah, bit);
        }
        wah_pad32(&wah);
        storage = t_wah_get_storage_lstr(&wah);
        wah_wipe(&wah);

        wah_from_data = wah_new_from_data(ps_initlstr(&storage));
        pos = 0;
        wah_for_each_1(en, wah_from_data) {
            Z_ASSERT_EQ(en.key, bits_pos[pos],
                        "bad bit position for bit [%d]", pos);
            pos++;
        }
        Z_ASSERT_EQ(pos, countof(bits_pos),
                    "missing bits in the WAH gotten from data");

        wah_delete(&wah_from_data);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(copy) { /* {{{ */
        wah_t wah_src, wah_dst;

        wah_init(&wah_src);
        wah_init(&wah_dst);

        wah_add0s(&wah_src, 3 * Z_WAH_BITS_IN_BUCKETS);
        Z_ASSERT_EQ(wah_src._buckets.len, 3);

        /* Layout of WAH  of equal size should remain identical. */
        wah_add1s(&wah_dst, 3 * Z_WAH_BITS_IN_BUCKETS);
        Z_ASSERT_EQ(wah_dst._buckets.len, 3);

        wah_copy(&wah_dst, &wah_src);
        Z_ASSERT_EQ(wah_dst._buckets.len, 3);
        Z_ASSERT_EQ(wah_dst._buckets.size, wah_src._buckets.size);

        wah_wipe(&wah_dst);
        wah_init(&wah_dst);

        /* Shorter WAH should be extended to match the source WAH. */
        wah_add1s(&wah_dst, 1 * Z_WAH_BITS_IN_BUCKETS);
        Z_ASSERT_EQ(wah_dst._buckets.len, 1);

        wah_copy(&wah_dst, &wah_src);
        Z_ASSERT_EQ(wah_dst._buckets.len, 3);
        Z_ASSERT_EQ(wah_dst._buckets.size, wah_src._buckets.size);

        wah_wipe(&wah_dst);
        wah_init(&wah_dst);

        /* Larger WAH should be shrunk to match the source WAH. */
        wah_add1s(&wah_dst, 5 * Z_WAH_BITS_IN_BUCKETS);
        Z_ASSERT_EQ(wah_dst._buckets.len, 5);

        wah_copy(&wah_dst, &wah_src);
        Z_ASSERT_EQ(wah_dst._buckets.len, 3);
        Z_ASSERT_EQ(wah_dst._buckets.size, wah_src._buckets.size);

        wah_wipe(&wah_dst);
        wah_wipe(&wah_src);
    } Z_TEST_END;

    /* }}} */

    wah_reset_bits_in_bucket();
} Z_GROUP_END;

/* LCOV_EXCL_STOP */
