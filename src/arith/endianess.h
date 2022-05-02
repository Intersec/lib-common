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

#if !defined(IS_LIB_COMMON_ARITH_H) || defined(IS_LIB_COMMON_ARITH_ENDIANESS_H)
#  error "you must include arith.h instead"
#else
#define IS_LIB_COMMON_ARITH_ENDIANESS_H

/*----- byte swapping stuff -----*/

#ifdef __GLIBC__
#include <byteswap.h>
static inline uint64_t bswap64(uint64_t x) { return bswap_64(x); }
static inline uint32_t bswap32(uint32_t x) { return bswap_32(x); }
static inline uint16_t bswap16(uint16_t x) { return bswap_16(x); }
#elif __GNUC_PREREQ(4, 3) || __CLANG_PREREQ(2, 0)
static inline uint64_t bswap64(uint64_t x) { return __builtin_bswap64(x); }
static inline uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }
static inline uint16_t bswap16(uint16_t x) { return (x >> 8) | (x << 8); }
#else
#  error please implement byte swap ops.
#endif

#define bswap16_const(x) \
    ((((uint16_t)(x)) << 8) | (((uint16_t)(x)) >> 8))
#define bswap32_const(x) \
    ((((uint32_t)(x) >> 24) & 0x000000ff) | \
     (((uint32_t)(x) >>  8) & 0x0000ff00) | \
     (((uint32_t)(x) <<  8) & 0x00ff0000) | \
     (((uint32_t)(x) << 24) & 0xff000000))
#define bswap64_const(x) \
    (((uint64_t)bswap32_const(x) << 32) | \
     (bswap32_const((uint64_t)(x) >> 32)))

#if __BYTE_ORDER == __BIG_ENDIAN
#  define ntohl_const(x)    force_cast(uint32_t, (x))
#  define ntohs_const(x)    force_cast(uint16_t, (x))
#  define htonl_const(x)    force_cast(be32_t, (x))
#  define htons_const(x)    force_cast(be16_t, (x))
#else
#  define ntohl_const(x)    force_cast(uint32_t, bswap32_const(x))
#  define ntohs_const(x)    force_cast(uint16_t, bswap16_const(x))
#  define htonl_const(x)    force_cast(be32_t, bswap32_const(x))
#  define htons_const(x)    force_cast(be16_t, bswap16_const(x))
#endif

#ifdef __SPARSE__
#include <arpa/inet.h>
#undef  htonl
#undef  htons
#undef  ntohl
#undef  ntohs
#define htonl htonl_const
#undef  htons htons_const
#undef  ntohl ntohl_const
#define ntohs ntohs_const
#endif


#if __BYTE_ORDER == __BIG_ENDIAN
#  define CPU_TO_LE(w, x)        force_cast(le##w##_t, bswap##w(x))
#  define CPU_TO_BE(w, x)        force_cast(be##w##_t, x)
#  define LE_TO_CPU(w, x)        force_cast(uint##w##_t, bswap##w(x))
#  define BE_TO_CPU(w, x)        force_cast(uint##w##_t, x)
#  define CPU_TO_LE_CONST(w, x)  force_cast(le##w##_t, bswap##w##_const(x))
#  define CPU_TO_BE_CONST(w, x)  force_cast(be##w##_t, x)
#else
#  define CPU_TO_LE(w, x)        force_cast(le##w##_t, x)
#  define CPU_TO_BE(w, x)        force_cast(be##w##_t, bswap##w(x))
#  define LE_TO_CPU(w, x)        force_cast(uint##w##_t, x)
#  define BE_TO_CPU(w, x)        force_cast(uint##w##_t, bswap##w(x))
#  define CPU_TO_LE_CONST(w, x)  force_cast(le##w##_t, x)
#  define CPU_TO_BE_CONST(w, x)  force_cast(be##w##_t, bswap##w##_const(x))
#endif

#define BE16_T(x)  CPU_TO_BE_CONST(16, x)
#define BE32_T(x)  CPU_TO_BE_CONST(32, x)
#define BE64_T(x)  CPU_TO_BE_CONST(64, x)

#define LE16_T(x)  CPU_TO_LE_CONST(16, x)
#define LE32_T(x)  CPU_TO_LE_CONST(32, x)
#define LE64_T(x)  CPU_TO_LE_CONST(64, x)

static inline uint16_t get_unaligned_cpu16(const void * nonnull p) {
    return get_unaligned_type(uint16_t, p);
}
static inline uint32_t get_unaligned_cpu32(const void * nonnull p) {
    return get_unaligned_type(uint32_t, p);
}
static inline uint64_t get_unaligned_cpu64(const void * nonnull p) {
    return get_unaligned_type(uint64_t, p);
}

static inline void * nonnull put_unaligned_cpu16(void * nonnull p, uint16_t x)
{
    return put_unaligned(p, x);
}
static inline void * nonnull put_unaligned_cpu32(void * nonnull p, uint32_t x)
{
    return put_unaligned(p, x);
}
static inline void * nonnull put_unaligned_cpu64(void * nonnull p, uint64_t x)
{
    return put_unaligned(p, x);
}

static inline le16_t cpu_to_le16(uint16_t x) { return CPU_TO_LE(16, x); }
static inline le32_t cpu_to_le32(uint32_t x) { return CPU_TO_LE(32, x); }
static inline le64_t cpu_to_le64(uint64_t x) { return CPU_TO_LE(64, x); }
static inline uint16_t le_to_cpu16(le16_t x) { return LE_TO_CPU(16, x); }
static inline uint32_t le_to_cpu32(le32_t x) { return LE_TO_CPU(32, x); }
static inline uint64_t le_to_cpu64(le64_t x) { return LE_TO_CPU(64, x); }

static inline le16_t cpu_to_le16p(const uint16_t * nonnull x)
{
    return CPU_TO_LE(16, *x);
}
static inline le32_t cpu_to_le32p(const uint32_t * nonnull x)
{
    return CPU_TO_LE(32, *x);
}
static inline le64_t cpu_to_le64p(const uint64_t * nonnull x)
{
    return CPU_TO_LE(64, *x);
}
static inline uint16_t le_to_cpu16p(const le16_t * nonnull x)
{
    return LE_TO_CPU(16, *x);
}
static inline uint32_t le_to_cpu32p(const le32_t * nonnull x)
{
    return LE_TO_CPU(32, *x);
}
static inline uint64_t le_to_cpu64p(const le64_t * nonnull x)
{
    return LE_TO_CPU(64, *x);
}

static inline le16_t cpu_to_le16pu(const void * nonnull x) {
    return CPU_TO_LE(16, get_unaligned_cpu16(x));
}
static inline le32_t cpu_to_le32pu(const void * nonnull x) {
    return CPU_TO_LE(32, get_unaligned_cpu32(x));
}
static inline le64_t cpu_to_le64pu(const void * nonnull x) {
    return CPU_TO_LE(64, get_unaligned_cpu64(x));
}
static inline uint16_t le_to_cpu16pu(const void * nonnull x) {
    return LE_TO_CPU(16, get_unaligned_cpu16(x));
}
static inline uint32_t le_to_cpu32pu(const void * nonnull x) {
    return LE_TO_CPU(32, get_unaligned_cpu32(x));
}
static inline uint64_t le_to_cpu64pu(const void * nonnull x) {
    return LE_TO_CPU(64, get_unaligned_cpu64(x));
}

#define get_unaligned_le16  cpu_to_le16pu
#define get_unaligned_le32  cpu_to_le32pu
#define get_unaligned_le64  cpu_to_le64pu

static inline void * nonnull put_unaligned_le16(void * nonnull p, uint16_t x)
{
    return put_unaligned_cpu16(p, CPU_TO_LE(16, x));
}
static inline void * nonnull put_unaligned_le32(void * nonnull p, uint32_t x)
{
    return put_unaligned_cpu32(p, CPU_TO_LE(32, x));
}
static inline void * nonnull put_unaligned_le64(void * nonnull p, uint64_t x)
{
    return put_unaligned_cpu64(p, CPU_TO_LE(64, x));
}

static inline be16_t cpu_to_be16(uint16_t x) { return CPU_TO_BE(16, x); }
static inline be32_t cpu_to_be32(uint32_t x) { return CPU_TO_BE(32, x); }
static inline be64_t cpu_to_be64(uint64_t x) { return CPU_TO_BE(64, x); }
static inline uint16_t be_to_cpu16(be16_t x) { return BE_TO_CPU(16, x); }
static inline uint32_t be_to_cpu32(be32_t x) { return BE_TO_CPU(32, x); }
static inline uint64_t be_to_cpu64(be64_t x) { return BE_TO_CPU(64, x); }

static inline be16_t cpu_to_be16p(const uint16_t * nonnull x)
{
    return CPU_TO_BE(16, *x);
}
static inline be32_t cpu_to_be32p(const uint32_t * nonnull x)
{
    return CPU_TO_BE(32, *x);
}
static inline be64_t cpu_to_be64p(const uint64_t * nonnull x)
{
    return CPU_TO_BE(64, *x);
}
static inline uint16_t be_to_cpu16p(const be16_t * nonnull x)
{
    return BE_TO_CPU(16, *x);
}
static inline uint32_t be_to_cpu32p(const be32_t * nonnull x)
{
    return BE_TO_CPU(32, *x);
}
static inline uint64_t be_to_cpu64p(const be64_t * nonnull x)
{
    return BE_TO_CPU(64, *x);
}

static inline be16_t cpu_to_be16pu(const void * nonnull x) {
    return CPU_TO_BE(16, get_unaligned_cpu16(x));
}
static inline be32_t cpu_to_be32pu(const void * nonnull x) {
    return CPU_TO_BE(32, get_unaligned_cpu32(x));
}
static inline be64_t cpu_to_be64pu(const void * nonnull x) {
    return CPU_TO_BE(64, get_unaligned_cpu64(x));
}
static inline uint16_t be_to_cpu16pu(const void * nonnull x) {
    return BE_TO_CPU(16, get_unaligned_cpu16(x));
}
static inline uint32_t be_to_cpu32pu(const void * nonnull x) {
    return BE_TO_CPU(32, get_unaligned_cpu32(x));
}
static inline uint64_t be_to_cpu64pu(const void * nonnull x) {
    return BE_TO_CPU(64, get_unaligned_cpu64(x));
}

#define get_unaligned_be16  cpu_to_be16pu
#define get_unaligned_be32  cpu_to_be32pu
#define get_unaligned_be64  cpu_to_be64pu

static inline void * nonnull put_unaligned_be16(void * nonnull p, uint16_t x)
{
    return put_unaligned_cpu16(p, CPU_TO_BE(16, x));
}
static inline void * nonnull put_unaligned_be32(void * nonnull p, uint32_t x)
{
    return put_unaligned_cpu32(p, CPU_TO_BE(32, x));
}
static inline void * nonnull put_unaligned_be64(void * nonnull p, uint64_t x)
{
    return put_unaligned_cpu64(p, CPU_TO_BE(64, x));
}

/* 24 and 48 bits helpers */
static inline void * nonnull put_unaligned_le24(void * nonnull p, uint32_t x)
{
    x = CPU_TO_LE(32, x);
    return mempcpy(p, &x, 3);
}
static inline void * nonnull put_unaligned_be24(void * nonnull p, uint32_t x)
{
    union {
        uint32_t x;
        uint8_t  b[4];
    } be;
    be.x = CPU_TO_BE(32, x);
    return mempcpy(p, &be.b[1], 3);
}

static inline void * nonnull put_unaligned_le48(void * nonnull p, uint64_t x)
{
    x = CPU_TO_LE(64, x);
    return mempcpy(p, &x, 6);
}
static inline void * nonnull put_unaligned_be48(void * nonnull p, uint64_t x)
{
    union {
        uint64_t x;
        uint8_t  b[8];
    } be;
    be.x = CPU_TO_BE(64, x);
    return mempcpy(p, &be.b[2], 6);
}

static inline uint32_t get_unaligned_le24(const void * nonnull p) {
    const uint8_t *p8 = cast(const uint8_t *, p);
    return get_unaligned_le16(p8) | (p8[2] << 16);
}

static inline uint32_t get_unaligned_be24(const void * nonnull p) {
    const uint8_t *p8 = cast(const uint8_t *, p);
    return get_unaligned_be16(p8 + 1) | (p8[0] << 16);
}

static inline uint64_t get_unaligned_le48(const void * nonnull p) {
    const uint8_t *p8 = cast(const uint8_t *, p);
    return get_unaligned_le32(p8) | ((uint64_t)get_unaligned_le16(p8 + 4) << 32);
}

static inline uint64_t get_unaligned_be48(const void * nonnull p) {
    const uint8_t *p8 = cast(const uint8_t *, p);
    return get_unaligned_be32(p8 + 2) | ((uint64_t)get_unaligned_be16(p) << 32);
}

#endif
