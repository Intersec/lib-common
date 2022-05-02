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

#if !defined(IS_LIB_COMMON_ARITH_H) || defined(IS_LIB_COMMON_ARITH_STR_H)
#  error "you must include arith.h instead"
#else
#define IS_LIB_COMMON_ARITH_STR_H

#define __PS_GET(ps, w, endianess)                                   \
    do {                                                             \
        endianess##w##_t res = get_unaligned_##endianess##w(ps->p);  \
        __ps_skip(ps, w / 8);                                        \
        return res;                                                  \
    } while (0)

#define PS_GET(ps, res, w, endianess)               \
    do {                                            \
        PS_WANT(ps_has(ps, w / 8));                 \
        *res = __ps_get_##endianess##w(ps);         \
        return 0;                                   \
    } while (0)

static inline uint16_t __ps_get_be16(pstream_t * nonnull ps)
{
    __PS_GET(ps, 16, be);
}
static inline uint32_t __ps_get_be24(pstream_t * nonnull ps)
{
    __PS_GET(ps, 24, be);
}
static inline uint32_t __ps_get_be32(pstream_t * nonnull ps)
{
    __PS_GET(ps, 32, be);
}
static inline uint64_t __ps_get_be48(pstream_t * nonnull ps)
{
    __PS_GET(ps, 48, be);
}
static inline uint64_t __ps_get_be64(pstream_t * nonnull ps)
{
    __PS_GET(ps, 64, be);
}

static inline uint16_t __ps_get_le16(pstream_t * nonnull ps)
{
    __PS_GET(ps, 16, le);
}
static inline uint32_t __ps_get_le24(pstream_t * nonnull ps)
{
    __PS_GET(ps, 24, le);
}
static inline uint32_t __ps_get_le32(pstream_t * nonnull ps)
{
    __PS_GET(ps, 32, le);
}
static inline uint64_t __ps_get_le48(pstream_t * nonnull ps)
{
    __PS_GET(ps, 48, le);
}
static inline uint64_t __ps_get_le64(pstream_t * nonnull ps)
{
    __PS_GET(ps, 64, le);
}

static inline uint16_t __ps_get_cpu16(pstream_t * nonnull ps)
{
    uint16_t res = get_unaligned_cpu16(ps->p);
    __ps_skip(ps, 16 / 8);
    return res;
}
static inline uint32_t __ps_get_cpu32(pstream_t * nonnull ps)
{
    uint32_t res = get_unaligned_cpu32(ps->p);
    __ps_skip(ps, 32 / 8);
    return res;
}
static inline uint64_t __ps_get_cpu64(pstream_t * nonnull ps)
{
    uint64_t res = get_unaligned_cpu64(ps->p);
    __ps_skip(ps, 64 / 8);
    return res;
}

static inline float __ps_get_float_le(pstream_t * nonnull ps)
{
    float res = get_unaligned_float_le(ps->p);
    __ps_skip(ps, sizeof(float));
    return res;
}
static inline int ps_get_float_le(pstream_t * nonnull ps, float * nonnull out)
{
    PS_WANT(ps_has(ps, sizeof(float)));
    *out = __ps_get_float_le(ps);
    return 0;
}
static inline double __ps_get_double_le(pstream_t * nonnull ps)
{
    double res = get_unaligned_double_le(ps->p);
    __ps_skip(ps, sizeof(double));
    return res;
}
static inline int ps_get_double_le(pstream_t * nonnull ps, double * nonnull out)
{
    PS_WANT(ps_has(ps, sizeof(double)));
    *out = __ps_get_double_le(ps);
    return 0;
}

static inline float __ps_get_float_be(pstream_t * nonnull ps)
{
    float res = get_unaligned_float_be(ps->p);
    __ps_skip(ps, sizeof(float));
    return res;
}
static inline int ps_get_float_be(pstream_t * nonnull ps, float * nonnull out)
{
    PS_WANT(ps_has(ps, sizeof(float)));
    *out = __ps_get_float_be(ps);
    return 0;
}
static inline double __ps_get_double_be(pstream_t * nonnull ps)
{
    double res = get_unaligned_double_be(ps->p);
    __ps_skip(ps, sizeof(double));
    return res;
}
static inline int ps_get_double_be(pstream_t * nonnull ps, double * nonnull out)
{
    PS_WANT(ps_has(ps, sizeof(double)));
    *out = __ps_get_double_be(ps);
    return 0;
}

static inline int ps_get_be16(pstream_t * nonnull ps, uint16_t * nonnull res)
{
    PS_GET(ps, res, 16, be);
}
static inline int ps_get_be24(pstream_t * nonnull ps, uint32_t * nonnull res)
{
    PS_GET(ps, res, 24, be);
}
static inline int ps_get_be32(pstream_t * nonnull ps, uint32_t * nonnull res)
{
    PS_GET(ps, res, 32, be);
}
static inline int ps_get_be48(pstream_t * nonnull ps, uint64_t * nonnull res)
{
    PS_GET(ps, res, 48, be);
}
static inline int ps_get_be64(pstream_t * nonnull ps, uint64_t * nonnull res)
{
    PS_GET(ps, res, 64, be);
}

static inline int ps_get_le16(pstream_t * nonnull ps, uint16_t * nonnull res)
{
    PS_GET(ps, res, 16, le);
}
static inline int ps_get_le24(pstream_t * nonnull ps, uint32_t * nonnull res)
{
    PS_GET(ps, res, 24, le);
}
static inline int ps_get_le32(pstream_t * nonnull ps, uint32_t * nonnull res)
{
    PS_GET(ps, res, 32, le);
}
static inline int ps_get_le48(pstream_t * nonnull ps, uint64_t * nonnull res)
{
    PS_GET(ps, res, 48, le);
}
static inline int ps_get_le64(pstream_t * nonnull ps, uint64_t * nonnull res)
{
    PS_GET(ps, res, 64, le);
}

static inline int ps_get_cpu16(pstream_t * nonnull ps, uint16_t * nonnull res)
{
    PS_GET(ps, res, 16, cpu);
}
static inline int ps_get_cpu32(pstream_t * nonnull ps, uint32_t * nonnull res)
{
    PS_GET(ps, res, 32, cpu);
}
static inline int ps_get_cpu64(pstream_t * nonnull ps, uint64_t * nonnull res)
{
    PS_GET(ps, res, 64, cpu);
}

#undef __PS_GET
#undef PS_GET

#define SB_ADD(sb, u, r, endianess) \
    put_unaligned_##endianess##r(sb_growlen(sb, r / 8), u)

static inline void sb_add_be16(sb_t * nonnull sb, uint16_t u)
{
    SB_ADD(sb, u, 16, be);
}
static inline void sb_add_be24(sb_t * nonnull sb, uint32_t u)
{
    SB_ADD(sb, u, 24, be);
}
static inline void sb_add_be32(sb_t * nonnull sb, uint32_t u)
{
    SB_ADD(sb, u, 32, be);
}
static inline void sb_add_be48(sb_t * nonnull sb, uint64_t u)
{
    SB_ADD(sb, u, 48, be);
}
static inline void sb_add_be64(sb_t * nonnull sb, uint64_t u)
{
    SB_ADD(sb, u, 64, be);
}

static inline void sb_add_le16(sb_t * nonnull sb, uint16_t u)
{
    SB_ADD(sb, u, 16, le);
}
static inline void sb_add_le24(sb_t * nonnull sb, uint32_t u)
{
    SB_ADD(sb, u, 24, le);
}
static inline void sb_add_le32(sb_t * nonnull sb, uint32_t u)
{
    SB_ADD(sb, u, 32, le);
}
static inline void sb_add_le48(sb_t * nonnull sb, uint64_t u)
{
    SB_ADD(sb, u, 48, le);
}
static inline void sb_add_le64(sb_t * nonnull sb, uint64_t u)
{
    SB_ADD(sb, u, 64, le);
}

static inline void sb_add_cpu16(sb_t * nonnull sb, uint16_t u)
{
    SB_ADD(sb, u, 16, cpu);
}
static inline void sb_add_cpu32(sb_t * nonnull sb, uint32_t u)
{
    SB_ADD(sb, u, 32, cpu);
}
static inline void sb_add_cpu64(sb_t * nonnull sb, uint64_t u)
{
    SB_ADD(sb, u, 64, cpu);
}

#undef SB_ADD

#endif
