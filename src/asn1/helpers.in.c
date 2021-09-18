/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_ASN1_TOOLS_H
#define IS_LIB_COMMON_ASN1_TOOLS_H

static ALWAYS_INLINE size_t asn1_int32_size(int32_t i32)
{
    int32_t zzi = (i32 >> 31) ^ (i32 << 1);

    return 1 + bsr32(zzi | 1) / 8;
}

static ALWAYS_INLINE size_t asn1_int64_size(int64_t i64)
{
    int64_t zzi = (i64 >> 63) ^ (i64 << 1);

    return 1 + bsr64(zzi | 1) / 8;
}

static ALWAYS_INLINE size_t asn1_uint32_size(uint32_t u32)
{
    return asn1_int64_size(u32);
}

static ALWAYS_INLINE size_t asn1_uint64_size(uint64_t u64)
{
    if (unlikely((0x1ULL << 63) & u64))
        return 9;

    return asn1_int64_size(u64);
}

static ALWAYS_INLINE size_t asn1_length_size(uint32_t len)
{
    if (len < 0x80)
        return 1;

    return 2 + bsr32(len) / 8;
}

static ALWAYS_INLINE size_t u64_blen(int64_t u64)
{
    if (likely(u64)) {
        return bsr64(u64) + 1;
    }

    return 0;
}

static ALWAYS_INLINE size_t u16_blen(uint16_t u16)
{
    if (likely(u16)) {
        return bsr16(u16) + 1;
    }

    return 0;
}

static ALWAYS_INLINE size_t i64_olen(int64_t i)
{
    return asn1_int64_size(i);
}

static ALWAYS_INLINE size_t u64_olen(uint64_t u)
{
    if (!u) {
        return 1;
    }

    return 1 + (bsr64(u) / 8);
}

#endif
