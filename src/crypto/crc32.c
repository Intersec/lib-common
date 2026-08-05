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

/*
 *
 * \file       crc32.c
 * \brief      CRC32 calculation
 *
 * Calculate the CRC32 using the slice-by-eight algorithm.
 * It is explained in this document:
 * http://www.intel.com/technology/comms/perfnet/download/CRC_generators.pdf
 * The code in this file is not the same as in Intel's paper, but
 * the basic principle is identical.
 *
 *  Author:     Lasse Collin
 *
 *  This file has been put into the public domain.
 *  You can do whatever you want with this file.
 *
 */

#include "crc.h"

/* Whether the CRC is computed by the Armv8 instructions rather than by the
 * table below, see hw_icrc32(). */
#if defined(__ARM_FEATURE_CRC32) && __BYTE_ORDER == __LITTLE_ENDIAN
#  define ICRC32_USE_HW 1
#endif

#ifndef ICRC32_USE_HW

#  include "crc32-table.in.c"

/* Simplistic crc32 calculator, almost compatible with zlib version,
 * except for crc type as uint32_t instead of unsigned long
 */
static ALWAYS_INLINE uint32_t
naive_icrc32(uint32_t crc, const uint8_t *buf, ssize_t len)
{
    if (len) {
        do {
            crc = crc32table[0][*buf++ ^ A(crc)] ^ S8(crc);
        } while (--len);
    }
    return crc;
}

/* If you make any changes, do some bench marking! Seemingly unrelated
 * changes can very easily ruin the performance (and very probably is
 * very compiler dependent).
 */
static uint32_t fast_icrc32(uint32_t crc, const uint8_t *buf, size_t size)
{
    size_t words;

    if (unlikely((uintptr_t)buf & 7)) {
        size -= 8 - ((uintptr_t)buf & 7);
        do {
            crc = crc32table[0][*buf++ ^ A(crc)] ^ S8(crc);
        } while ((uintptr_t)buf & 7);
    }

    words = size >> 3;
    do {
        uint32_t tmp;

        crc ^= *(uint32_t *)(buf);
        buf += 4;

        crc = crc32table[7][A(crc)] ^ crc32table[6][B(crc)] ^
              crc32table[5][C(crc)] ^ crc32table[4][D(crc)];

        tmp = *(uint32_t *)(buf);
        buf += 4;

        // At least with some compilers, it is critical for
        // performance, that the crc variable is XORed
        // between the two table-lookup pairs.
        crc = crc32table[3][A(tmp)] ^ crc32table[2][B(tmp)] ^ crc ^
              crc32table[1][C(tmp)] ^ crc32table[0][D(tmp)];
    } while (--words);

    return naive_icrc32(crc, buf, size & (size_t)7);
}

#else

#  pragma push_macro("__attr_leaf__")
#  undef __attr_leaf__
#  include <arm_acle.h>
#  pragma pop_macro("__attr_leaf__")

/* The Armv8 CRC32 instructions compute this very CRC: same polynomial, same
 * reflected convention. One instruction replaces the eight table lookups per
 * eight bytes. They come with Armv8.1, and the feature macro follows the
 * -march the build selects, so there is nothing to detect at runtime.
 */
static uint32_t hw_icrc32(uint32_t crc, const uint8_t *buf, size_t len)
{
    while (len && ((uintptr_t)buf & 7)) {
        crc = __crc32b(crc, *buf++);
        len--;
    }
    for (; len >= 8; buf += 8, len -= 8) {
        crc = __crc32d(crc, *(const uint64_t *)buf);
    }
    if (len & 4) {
        crc = __crc32w(crc, *(const uint32_t *)buf);
        buf += 4;
    }
    if (len & 2) {
        crc = __crc32h(crc, *(const uint16_t *)buf);
        buf += 2;
    }
    if (len & 1) {
        crc = __crc32b(crc, *buf);
    }
    return crc;
}

#endif

__attr_flatten__ uint32_t icrc32(uint32_t crc, const void *data, ssize_t len)
{
    crc = ~le_to_cpu32(crc);
#ifdef ICRC32_USE_HW
    return ~le_to_cpu32(hw_icrc32(crc, data, len));
#else
    if (len < 64) {
        return ~le_to_cpu32(naive_icrc32(crc, data, len));
    }
    return ~le_to_cpu32(fast_icrc32(crc, data, len));
#endif
}
