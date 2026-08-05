/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <lib-common/hash.h>
#include <lib-common/z.h>

/* A fixed buffer and the CRC32 of some of its prefixes, hardcoded.
 *
 * The CRC32 of data written on disk must not change with the CPU that reads
 * it back, so these values pin what every implementation has to return. They
 * come from the definition of the CRC, one bit at a time. The lengths cross
 * the paths of the implementations: the tails of one, two and four bytes of
 * the Armv8 one, and the 64 bytes threshold of the table one.
 */
static uint8_t const crc32_test_buf[128] = {
    0x87, 0x15, 0x48, 0x81, 0x70, 0x29, 0x89, 0xc5, 0xd3, 0x3a, 0x69, 0x4e,
    0xfc, 0x35, 0x8c, 0xf6, 0xcd, 0xee, 0xe7, 0x40, 0x2d, 0xea, 0x29, 0xcf,
    0xf7, 0xf4, 0xac, 0xc1, 0xea, 0x4c, 0x0f, 0xf8, 0xc0, 0xcb, 0x97, 0x98,
    0x84, 0xcf, 0xe5, 0x7a, 0x68, 0xd7, 0x77, 0x44, 0x2c, 0x1c, 0x34, 0x28,
    0x68, 0x14, 0x96, 0xbe, 0x2d, 0x6f, 0x86, 0xc9, 0x78, 0xbe, 0x23, 0x6a,
    0xfd, 0xaf, 0xbd, 0x0e, 0x90, 0x6a, 0x74, 0x94, 0x21, 0x86, 0xe5, 0x95,
    0xa2, 0x7e, 0xec, 0x91, 0x7a, 0x8e, 0x96, 0x16, 0xce, 0xc2, 0x3d, 0x96,
    0x6b, 0x4d, 0xe6, 0x4e, 0x28, 0xec, 0x1e, 0x09, 0x70, 0x84, 0x4c, 0xfe,
    0xc4, 0xa0, 0x53, 0xc9, 0xc9, 0x0e, 0x32, 0xdd, 0x51, 0x9c, 0x3f, 0x48,
    0x4f, 0x02, 0x47, 0x8c, 0x8b, 0xc7, 0x99, 0x80, 0x47, 0x15, 0xc8, 0xc7,
    0x8d, 0xb0, 0x10, 0xce, 0xdf, 0x66, 0xcc, 0xf6,
};

/* The CRC32 of the whole buffer above. */
#define CRC32_TEST_BUF_CRC 0x13675907u

/** Compute the CRC32 of the reflected polynomial 0xedb88320, one bit at a
 * time.
 *
 * This is the definition of the CRC that the table and the Armv8
 * instructions compute. It depends on nothing but the input, so it is the
 * reference both must match, at every length and every alignment.
 */
static uint32_t z_crc32_bitwise(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc = ~le_to_cpu32(crc);
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & -(uint32_t)(crc & 1));
        }
    }
    return ~le_to_cpu32(crc);
}

Z_GROUP_EXPORT(crc32) {
    Z_TEST(check_value) {
        /* The check value of CRC-32, the CRC of "123456789". */
        Z_ASSERT_EQ(0xcbf43926u, icrc32(0, "123456789", 9));
        Z_ASSERT_EQ(0u, icrc32(0, "", 0));
    } Z_TEST_END;

    Z_TEST(vectors) {
#define T(Len, Crc)                                                          \
    Z_ASSERT_EQ(                                                             \
        (uint32_t)(Crc), icrc32(0, crc32_test_buf, (Len)), "length %d",      \
        (Len)                                                                \
    )
        T(0, 0x00000000);
        T(1, 0xa1def90e);
        T(2, 0x58c6f898);
        T(3, 0x5489fba1);
        T(4, 0xe93846be);
        T(7, 0x3a7396c3);
        T(8, 0x3b5b392e);
        T(9, 0x11353a15);
        T(15, 0x63b0ca88);
        T(16, 0x65dc037c);
        T(31, 0xe898b5d8);
        T(32, 0xe98457f0);
        T(63, 0xbef68fa9);
        T(64, 0x9a0e2f49);
        T(65, 0x2d978bd2);
        T(71, 0x2b298887);
        T(72, 0x2190b74d);
        T(79, 0x9a396b9a);
        T(127, 0x04ab21c8);
        T(128, CRC32_TEST_BUF_CRC);
#undef T
    } Z_TEST_END;

    Z_TEST(bitwise) {
        /* Every length at every alignment. The implementations read the head
         * of the buffer one byte at a time until it is aligned, then eight
         * bytes at a time, then the tail one piece at a time. */
        for (size_t off = 0; off < 16; off++) {
            for (size_t len = 0; off + len <= countof(crc32_test_buf); len++)
            {
                const uint8_t *buf = crc32_test_buf + off;

                Z_ASSERT_EQ(
                    z_crc32_bitwise(0, buf, len), icrc32(0, buf, len),
                    "offset %zu, length %zu", off, len
                );
            }
        }
    } Z_TEST_END;

    Z_TEST(chained) {
        /* Two calls on the two halves give the CRC of the whole buffer. */
        for (size_t cut = 0; cut <= countof(crc32_test_buf); cut++) {
            size_t len = countof(crc32_test_buf);
            uint32_t crc = icrc32(0, crc32_test_buf, cut);

            crc = icrc32(crc, crc32_test_buf + cut, len - cut);
            Z_ASSERT_EQ(CRC32_TEST_BUF_CRC, crc, "cut at %zu", cut);
        }
    } Z_TEST_END;
} Z_GROUP_END;
