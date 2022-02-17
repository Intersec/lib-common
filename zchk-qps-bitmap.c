/**************************************************************************/
/*                                                                        */
/*  Copyright (C) INTERSEC SA                                             */
/*                                                                        */
/*  Should you receive a copy of this source code, you must check you     */
/*  have a proper, written authorization of INTERSEC to hold it. If you   */
/*  don't have such an authorization, you must DELETE all source code     */
/*  files in your possession, and inform INTERSEC of the fact you obtain  */
/*  these files. Should you not comply to these terms, you can be         */
/*  prosecuted in the extent permitted by applicable law.                 */
/*                                                                        */
/**************************************************************************/

#include "z.h"
#include "qps-bitmap.h"

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

    Z_TEST(nullable_enumeration, "nullable enumeration") { /* {{{ */
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
        qps_bitmap_for_each(enumeration, &bitmap) {
            Z_ASSERT_EQ(enumeration.key.key, count);
            count++;
        }
        Z_ASSERT_EQ(count, 0x8000U);

        en = qps_bitmap_get_enumerator(&bitmap);
        for (uint32_t i = 0; i < 0x8000; i++) {
            qps_bitmap_key_t key = { .key = 0 };

            qps_bitmap_enumerator_find_word(&en, key);
            Z_ASSERT_EQ(en.key.key, 0U);

            key.key = i;
            qps_bitmap_enumerator_find_word(&en, key);
            Z_ASSERT_EQ(en.key.key, i);
        }

        qps_bitmap_destroy(&bitmap);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(nr_33413, "nr_33413") { /* {{{ */
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

        qps_bitmap_enumerator_next(&en);
        Z_ASSERT_EQ(en.key.key, 270101u);

        qps_bitmap_destroy(&bitmap);
    } Z_TEST_END;

    /* }}} */
    Z_TEST(qps_bitmap_nr, "") { /* {{{ */
        qps_handle_t hbitmap;
        qps_bitmap_t bitmap;
        qps_bitmap_enumerator_t en;

        Z_TEST_FLAGS("redmine_83666");

        hbitmap = qps_bitmap_create(qps, false);
        qps_bitmap_init(&bitmap, qps, hbitmap);

        for (int i = 1; i < 100; i++) {
            qps_bitmap_set(&bitmap, i);
        }
        en = qps_bitmap_get_enumerator_at(&bitmap, 80);
        for (int i = 100; i < 1025; i++) {
            qps_bitmap_set(&bitmap, i);
        }

        for (uint32_t key = 80; key < 1025; key++) {
            /* FIXME QPS bitmap enumerator is "safe" for changes that modify
             * the structure of the bitmap (eg. when the structure generation
             * "struct_gen" is changed), but not for small changes that keep
             * the structure untouched.
             *
             * We should have a "safe" version of
             * 'qps_bitmap_enumerator_next[_nn]() that would cope with those
             * small changes.
             */
            if (key == 100) {
                key = 128;
            }

            Z_ASSERT(!en.end);
            Z_ASSERT_EQ(en.key.key, key);
            qps_bitmap_enumerator_next_nn(&en);
        }
        Z_ASSERT(en.end);
    } Z_TEST_END;

    /* }}} */

    qps_close(&qps);
    MODULE_RELEASE(qps);
} Z_GROUP_END

/* LCOV_EXCL_STOP */
