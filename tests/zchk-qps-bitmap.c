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
#include <lib-common/qps-bitmap.h>

/* LCOV_EXCL_START */

Z_GROUP_EXPORT(qps_bitmap) {
    qps_t *qps;

    MODULE_REQUIRE(qps);

    if (qps_exists(z_grpdir_g.s)) {
        qps = qps_open(z_grpdir_g.s, "bitmap", NULL);
    } else {
        qps = qps_create(z_grpdir_g.s, "bitmap", 0755, NULL, 0);
    }
    assert (qps);

    Z_TEST(nullable_enumeration) { /* {{{ */
        qps_handle_t handle = qps_bitmap_create(qps, true);
        qps_bitmap_t bitmap;
        uint32_t count;
        qps_bitmap_enumerator_t en;

        qps_bitmap_init(&bitmap, qps, handle);

        /* Store 0s */
        for (uint32_t i = 0; i < 0x8000; i++) {
            Z_ASSERT_EQ(qps_bitmap_set(&bitmap, i), (uint32_t)QPS_BITMAP_NULL);
        }

        for (uint32_t i = 0; i < 0x8000; i++) {
            Z_ASSERT_EQ(qps_bitmap_get(&bitmap, i), (uint32_t)QPS_BITMAP_1);
        }

        count = 0;
        qps_bitmap_for_each_unsafe(enumeration, &bitmap) {
            Z_ASSERT_EQ(enumeration.key.key, count);
            count++;
        }
        Z_ASSERT_EQ(count, 0x8000U);

        en = qps_bitmap_get_enumerator(&bitmap);
        for (uint32_t i = 0; i < 0x8000; i++) {
            qps_bitmap_key_t key = { .key = 0 };

            qps_bitmap_enumerator_find_word_nu(&en, key);
            Z_ASSERT_EQ(en.key.key, 0U);

            key.key = i;
            qps_bitmap_enumerator_find_word_nu(&en, key);
            Z_ASSERT_EQ(en.key.key, i);
        }

        qps_bitmap_destroy(&bitmap);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(nr_33413) { /* {{{ */
        /* Non-regression test for ticket #33413. */
        qps_handle_t handle = qps_bitmap_create(qps, true);
        qps_bitmap_t bitmap;
        qps_bitmap_enumerator_t en;

        qps_bitmap_init(&bitmap, qps, handle);

        Z_ASSERT_EQ(qps_bitmap_set(&bitmap, 270100),
                    (uint32_t)QPS_BITMAP_NULL);
        Z_ASSERT_EQ(qps_bitmap_set(&bitmap, 270101),
                    (uint32_t)QPS_BITMAP_NULL);

        en = qps_bitmap_get_enumerator(&bitmap);
        Z_ASSERT_EQ(en.key.key, 270100u);

        for (uint32_t i = 0; i < 270100; i++) {
            Z_ASSERT_EQ(qps_bitmap_set(&bitmap, i), (uint32_t)QPS_BITMAP_NULL);
        }

        qps_bitmap_enumerator_next(&en, true);
        Z_ASSERT_EQ(en.key.key, 270101u);

        qps_bitmap_destroy(&bitmap);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(qps_bitmap_nr) { /* {{{ */
        qps_handle_t hbitmap;
        qps_bitmap_t bitmap;
        qps_bitmap_enumerator_t en;

        Z_TEST_FLAGS("redmine_83666");

        hbitmap = qps_bitmap_create(qps, false);
        qps_bitmap_init(&bitmap, qps, hbitmap);

        for (int i = 1; i < 100; i++) {
            qps_bitmap_set(&bitmap, i);
        }
        /* Start the enumeration. */
        en = qps_bitmap_get_enumerator_at(&bitmap, 80);

        /* Modify the bitmap. */
        for (int i = 100; i < 1025; i++) {
            qps_bitmap_set(&bitmap, i);
        }

        /* Complete the enumeration. */
        for (uint32_t key = 80; key < 1025; key++) {
            Z_ASSERT(!en.end);
            Z_ASSERT_EQ(en.key.key, key);
            qps_bitmap_enumerator_next_nn(&en, true);
        }
        Z_ASSERT(en.end);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(remove_current_row) { /* {{{ */
        bool is_nullable_v[] = { false, true };

        Z_TEST_FLAGS("redmine_83666");

        carray_for_each_entry(is_nullable, is_nullable_v) {
            qps_handle_t hbitmap;
            qps_bitmap_t bitmap;
            qps_bitmap_enumerator_t en;

            hbitmap = qps_bitmap_create(qps, is_nullable);
            qps_bitmap_init(&bitmap, qps, hbitmap);

            for (int i = 1; i < 100; i++) {
                qps_bitmap_set(&bitmap, i);
            }
            en = qps_bitmap_get_enumerator_at(&bitmap, 50);
            Z_ASSERT_EQ(en.key.key, 50u);
            if (is_nullable) {
                qps_bitmap_remove(&bitmap, 50);
            } else {
                qps_bitmap_reset(&bitmap, 50);
            }
            qps_bitmap_enumerator_next(&en, true);
            Z_ASSERT_EQ(en.key.key, 51u);
        }
    } Z_TEST_END;

    Z_TEST(nr_100747) { /* {{{ */
        qps_handle_t hbitmap;
        qps_bitmap_t bitmap;
        uint32_t start_key, gap_key, k_end;

        Z_TEST_FLAGS("redmine_100747");

        hbitmap = qps_bitmap_create(qps, true);
        Z_ASSERT_NE(hbitmap, QPS_HANDLE_NULL, "creation of bitmap failed");
        qps_bitmap_init(&bitmap, qps, hbitmap);

        start_key = 261889;
        gap_key = 42;
        k_end = start_key + gap_key * 5122;
        for (uint32_t k = start_key; k < k_end; k += gap_key) {
            qps_bitmap_set(&bitmap, k);
        }

        start_key = 4294901759;
        gap_key = 1;
        /* This is close to an overflow but not yet. */
        k_end = start_key + gap_key * 65535;
        for (uint32_t k = start_key; k < k_end; k += gap_key) {
            qps_bitmap_set(&bitmap, k);
        }

        qps_bitmap_clear(&bitmap);

        /* Check all nodes have been cleared (so we don't reproduce issue
         * for invalid node accesses in this QPS bitmap, if we use it again).
         */
        for (uint32_t i = 0; i < countof(bitmap.root->roots); i++) {
            Z_ASSERT_EQ(bitmap.root->roots[i], QPS_PG_NULL, "page not null");
        }

        /* Commenting out the previous loop should now trigger a panic while
         * freeing memory if issue is still there on QPS bitmap (double free
         * performed on QPS allocator). */
        qps_bitmap_destroy(&bitmap);
    } Z_TEST_END;

    /* }}} */

    qps_close(&qps);
    MODULE_RELEASE(qps);
} Z_GROUP_END

/* LCOV_EXCL_STOP */
