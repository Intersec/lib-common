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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_BIHACKS_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_BIHACKS_H

#if (SIZE_MAX == UINT32_MAX)
#  define DO_SZ(f)  f##32
#elif (SIZE_MAX == UINT64_MAX)
#  define DO_SZ(f)  f##64
#else
#  error  unable to detect your architecture wordsize
#endif

#if (defined(__x86_64__) || defined(__i386__))                               \
 && (__GNUC_PREREQ(4, 4) ||__CLANG_PREREQ(7, 0))
#   define __HAS_CPUID 1
#endif

/* XXX bit scan reverse, only defined for u != 0
 * bsr32(0x1f) == 4 because first bit set from the "left" is 2^4
 */
extern uint8_t const __firstbit_rev8[256];

static inline size_t bsr8 (uint8_t  u) { return __firstbit_rev8[u]; }
static inline size_t bsr16(uint16_t u) { return __builtin_clz(u) ^ 31; }
static inline size_t bsr32(uint32_t u) { return __builtin_clz(u) ^ 31; }
static inline size_t bsr64(uint64_t u) { return __builtin_clzll(u) ^ 63; }
static inline size_t bsrsz(size_t   u) { return DO_SZ(bsr)(u); }

/** Find the last bit at 1 or 0 in a sequence of bits.
 *
 * Same as bsf but scan from the end of the stream (bitwise).
 *
 * \see bsf.
 */
ssize_t bsr(const void * nonnull data, size_t start_bit, size_t len,
            bool reverse);

/* XXX bit scan forward, only defined for u != 0
 * bsf32(0xf10) == 4 because first bit set from the "right" is 2^4
 */
extern uint8_t const __firstbit_fwd8[256];

static inline size_t bsf8 (uint8_t  u) { return __firstbit_fwd8[u]; }
static inline size_t bsf16(uint16_t u) { return __builtin_ctz(u); }
static inline size_t bsf32(uint32_t u) { return __builtin_ctz(u); }
static inline size_t bsf64(uint64_t u) { return __builtin_ctzll(u); }
static inline size_t bsfsz(size_t   u) { return DO_SZ(bsf)(u); }

/** Find the first bit at 1 or 0 in a sequence of bits.
 *
 * \param[in] data      the memory to scan
 * \param[in] start_bit offset of the first bit after data
 * \param[in] len       maximum number of bits to scan after start_bit
 * \param[in] reverse   if reverse, look for the first 0 instead of the first 1
 * \return -1 if no 1 (or 0 if \p reverse was set) was found, else the offset
 *         of the first bit of the given value relative to start_bit.
 */
ssize_t bsf(const void * nonnull data, size_t start_bit, size_t len,
            bool reverse);

#define __SIGN_EXTEND_HELPER(T, x, bits)  (((T)(x) << (bits)) >> (bits))
#define sign_extend(x, bits) \
    ({ int __bits = (bits);                                                  \
       __builtin_choose_expr(bitsizeof(x) <= bitsizeof(uint32_t),            \
                             __SIGN_EXTEND_HELPER(int32_t, x, 32 - __bits),  \
                             __SIGN_EXTEND_HELPER(int64_t, x, 64 - __bits)); \
     })


/*----- bit_reverse --------*/

/** Reverses the bit order of any unsigned integer:
 *  16 bits example:
 *     0x3445           -> 0xa22c
 *     0011010001000101 -> 1010001000101100
 */

extern uint8_t const __bit_reverse8[1 << 8];

static ALWAYS_INLINE uint8_t bit_reverse8(uint8_t u8) {
    return __bit_reverse8[u8];
}

static ALWAYS_INLINE uint16_t bit_reverse16(uint16_t u16) {
    return ((uint16_t)bit_reverse8(u16) << 8) | bit_reverse8(u16 >> 8);
}

static ALWAYS_INLINE uint32_t bit_reverse32(uint32_t u32) {
    return ((uint32_t)bit_reverse16(u32) << 16) | bit_reverse16(u32 >> 16);
}

static ALWAYS_INLINE uint64_t bit_reverse64(uint64_t u64) {
    return ((uint64_t)bit_reverse32(u64) << 32) | bit_reverse32(u64 >> 32);
}

/*----- bitcount -----*/

#ifdef __POPCNT__
static inline uint8_t bitcount8(uint8_t n) {
    return __builtin_popcount(n);
}
static inline uint8_t bitcount16(uint16_t n) {
    return __builtin_popcount(n);
}
static inline uint8_t bitcount32(uint32_t n) {
    return __builtin_popcount(n);
}
static inline uint8_t bitcount64(uint64_t n) {
    return __builtin_popcountll(n);
}
#else

extern uint8_t const __bitcount11[1 << 11];

static inline uint8_t bitcount8(uint8_t n) {
    return __bitcount11[n];
}
static inline uint8_t bitcount16(uint16_t n) {
    return bitcount8(n) + bitcount8(n >> 8);
}

static inline uint8_t bitcount32(uint32_t n) {
    return __bitcount11[(n >>  0) & 0x7ff]
        +  __bitcount11[(n >> 11) & 0x7ff]
        +  __bitcount11[(n >> 22) & 0x7ff];
}
static inline uint8_t bitcount64(uint64_t n) {
    return __bitcount11[(n >>  0) & 0x7ff]
        +  __bitcount11[(n >> 11) & 0x7ff]
        +  __bitcount11[(n >> 22) & 0x7ff]
        +  __bitcount11[(n >> 33) & 0x7ff]
        +  __bitcount11[(n >> 44) & 0x7ff]
        +  __bitcount11[(n >> 55)];
}
#endif

static inline uint8_t bitcountsz(size_t n) {
    return DO_SZ(bitcount)(n);
}

extern size_t (*nonnull membitcount)(const void * nonnull ptr, size_t n);

size_t membitcount_c(const void *nonnull ptr, size_t n);

#ifdef __HAS_CPUID
size_t membitcount_ssse3(const void * nonnull ptr, size_t n);
size_t membitcount_popcnt(const void * nonnull ptr, size_t n);
#endif

#endif
