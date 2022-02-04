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

/* LCOV_EXCL_START */

#include <endian.h>
#include <math.h>

#include <lib-common/core.h>
#include <lib-common/z.h>

Z_GROUP_EXPORT(iprintf) {
    Z_TEST(double, "") {
        char buffer[128];

        isprintf(buffer, "%g", -INFINITY);
        Z_ASSERT_STREQUAL(buffer, "-Inf");
        isprintf(buffer, "%g", INFINITY);
        Z_ASSERT_STREQUAL(buffer, "Inf");
        isprintf(buffer, "%+g", INFINITY);
        Z_ASSERT_STREQUAL(buffer, "+Inf");
    } Z_TEST_END;

    Z_TEST(pM, "") {
        char buffer[128];

        isprintf(buffer, "%*pM", 3, "1234");
        Z_ASSERT_STREQUAL(buffer, "123", "");
        isprintf(buffer, "%*pM;toto", 3, "123");
        Z_ASSERT_STREQUAL(buffer, "123;toto", "");
        isprintf(buffer, "%*pMtrailing", 3, "123");
        Z_ASSERT_STREQUAL(buffer, "123trailing", "");
    } Z_TEST_END

    Z_TEST(pX, "") {
        char buffer[128];

        isprintf(buffer, "%*pX", 4, "1234");
        Z_ASSERT_STREQUAL(buffer, "31323334");
        isprintf(buffer, "%*pX world!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656C6C6F world!");
        isprintf(buffer, "%*pXworld!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656C6C6Fworld!");
    } Z_TEST_END;

    Z_TEST(px, "") {
        char buffer[128];

        isprintf(buffer, "%*px", 4, "1234");
        Z_ASSERT_STREQUAL(buffer, "31323334");
        isprintf(buffer, "%*px world!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656c6c6f world!");
        isprintf(buffer, "%*pxworld!", 5, "Hello");
        Z_ASSERT_STREQUAL(buffer, "48656c6c6fworld!");
    } Z_TEST_END;

    Z_TEST(pL, "") {
        char buffer[128];
        const lstr_t str = LSTR_IMMED("1234");
        SB_1k(sb);

        isprintf(buffer, "%pL", &str);
        Z_ASSERT_STREQUAL(buffer, "1234");

        isprintf(buffer, "%pL;toto", &str);
        Z_ASSERT_STREQUAL(buffer, "1234;toto");
        isprintf(buffer, "%pLtrailing", &str);
        Z_ASSERT_STREQUAL(buffer, "1234trailing");

        /* works for sb_t variables too */
        sb_set_lstr(&sb, str);

        isprintf(buffer, "%pL", &sb);
        Z_ASSERT_STREQUAL(buffer, "1234");

        isprintf(buffer, "%pL;toto", &sb);
        Z_ASSERT_STREQUAL(buffer, "1234;toto");
        isprintf(buffer, "%pLtrailing", &sb);
        Z_ASSERT_STREQUAL(buffer, "1234trailing");
    } Z_TEST_END

    Z_TEST(ivasprintf, "") {
        char *formatted = iasprintf("%*pM", 4, "1234");
        int len = 2 * BUFSIZ;
        char big[len + 1];

        Z_ASSERT_STREQUAL(formatted, "1234");
        p_delete(&formatted);

        memset(big, 'a', len);
        big[2*BUFSIZ] = 0;
        formatted = iasprintf("%*pM", len, big);
        Z_ASSERT_STREQUAL(formatted, big);
        p_delete(&formatted);
    } Z_TEST_END;
} Z_GROUP_END

/* LCOV_EXCL_STOP */
