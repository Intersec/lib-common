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

#include <lib-common/hash.h>

/*
 *
 *  \file       crc64.c
 *  \brief      CRC64 calculation
 *
 *  Calculate the CRC64 using the slice-by-four algorithm. This is the same
 *  idea that is used in crc32_fast.c, but for CRC64 we use only four tables
 *  instead of eight to avoid increasing CPU cache usage.
 *
 *  Author:     Lasse Collin
 *
 *  This file has been put into the public domain.
 *  You can do whatever you want with this file.
 *
 */

#include "crc.h"
#include "crc64-table.in.c"

static ALWAYS_INLINE
uint64_t naive_icrc64(uint64_t crc, const uint8_t *buf, ssize_t len)
{
    if (len) {
        do {
            crc = crc64table[0][*buf++ ^ A1(crc)] ^ S8(crc);
        } while (--len);
    }
    return crc;
}

static uint64_t fast_icrc64(uint64_t crc, const uint8_t *buf, size_t size)
{
    size_t words;

    if (unlikely((uintptr_t)buf & 3)) {
        size -= 4 - ((uintptr_t)buf & 3);
        do {
            crc = crc64table[0][*buf++ ^ A1(crc)] ^ S8(crc);
        } while ((uintptr_t)buf & 3);
    }

    words = size >> 2;
    do {
#if __BYTE_ORDER == __BIG_ENDIAN
        const uint32_t tmp = (crc >> 32) ^ *(uint32_t *)(buf);
#else
        const uint32_t tmp = crc ^ *(uint32_t *)(buf);
#endif
        buf += 4;

        crc = crc64table[3][A(tmp)]
            ^ crc64table[2][B(tmp)]
            ^ S32(crc)
            ^ crc64table[1][C(tmp)]
            ^ crc64table[0][D(tmp)];
    } while (--words);

    return naive_icrc64(crc, buf, size & (size_t)3);
}

__flatten
uint64_t icrc64(uint64_t crc, const void *data, ssize_t len)
{
    crc = ~le_to_cpu64(crc);
    if (len < 64)
        return ~le_to_cpu64(naive_icrc64(crc, data, len));
    return ~le_to_cpu64(fast_icrc64(crc, data, len));
}
