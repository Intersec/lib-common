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
#include <lib-common/z.h>

Z_GROUP_EXPORT(bithacks)
{
    Z_TEST(bsr8, "full check of bsr8") {
        for (int i = 1; i < 256; i++) {
            Z_ASSERT_EQ(bsr8(i), bsr16(i), "[i:%d]", i);
        }
    } Z_TEST_END;

    Z_TEST(bsf8, "full check of bsf8") {
        for (int i = 1; i < 256; i++) {
            Z_ASSERT_EQ(bsf8(i), bsf16(i), "[i:%d]", i);
        }
    } Z_TEST_END;

    Z_TEST(bitmasks, "core: BITMASKS") {
        Z_ASSERT_EQ(BITMASK_GE(uint32_t, 0),  0xffffffffU);
        Z_ASSERT_EQ(BITMASK_GE(uint32_t, 31), 0x80000000U);

        Z_ASSERT_EQ(BITMASK_GT(uint32_t, 0),  0xfffffffeU);
        Z_ASSERT_EQ(BITMASK_GT(uint32_t, 31), 0x00000000U);

        Z_ASSERT_EQ(BITMASK_LE(uint32_t, 0),  0x00000001U);
        Z_ASSERT_EQ(BITMASK_LE(uint32_t, 31), 0xffffffffU);

        Z_ASSERT_EQ(BITMASK_LT(uint32_t, 0),  0x00000000U);
        Z_ASSERT_EQ(BITMASK_LT(uint32_t, 31), 0x7fffffffU);
    } Z_TEST_END;
} Z_GROUP_END
