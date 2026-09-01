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

#include <lib-common/arith.h>

/* The scans are 128 bits wide SIMD reductions built on a handful of vector
 * primitives. There are two implementations of these primitives: one on the
 * SSE2 intrinsics, and a portable one on the compiler vector extensions,
 * which the compiler lowers to NEON on aarch64, and to scalar code on
 * architectures without SIMD. The scans themselves are written once.
 */

#if defined(__SSE2__)
/* SSE2 primitives {{{ */

/* GCC before 4.4 only supports SSE2 and has no x86intrin.h */
#  if defined(__clang__) || __GNUC_PREREQ(4, 4)
#    pragma push_macro("__attr_leaf__")
#    undef __attr_leaf__
#    include <x86intrin.h>
#    pragma pop_macro("__attr_leaf__")
#  else
#    include <emmintrin.h>
#  endif

#  if defined(__clang__) && !defined(__builtin_ia32_pcmpeqb128)
#    define __builtin_ia32_pcmpeqb128(a, b) ((a) == (b))
#    define __builtin_ia32_pcmpeqw128(a, b) ((a) == (b))
#    define __builtin_ia32_pcmpeqd128(a, b) ((a) == (b))
#  endif

typedef __v16qi vec8_t;
typedef __v8hi vec16_t;
typedef __v4si vec32_t;
typedef __v2di vec64_t;

union xmm {
    __m128i i;
    vec8_t b;
    vec16_t w;
    vec32_t d;
    vec64_t q;

    __v2df df;
    __v4sf sf;

    uint8_t vb[16];
    uint16_t vw[8];
    uint32_t vd[4];
    uint64_t vq[2];
};

static ALWAYS_INLINE uint32_t sum_epi64(vec64_t xmm)
{
    union xmm x = {.q = xmm};

    x.sf = _mm_movehl_ps(x.sf, x.sf); /* x.q[0] <- x.q[1] */
    x.q += xmm;                       /* x.q[0] += xmm[0] */
    return x.vd[0];
}

static ALWAYS_INLINE uint32_t sum_epi32(vec32_t xmm)
{
    union xmm x = {.d = xmm};

    x.sf = _mm_movehl_ps(x.sf, x.sf); /* x.d[0..1] <- x.q[2..3] */
    x.d += xmm;                       /* x.d[0..1] += xmm[0..1] */
    return x.vd[0] + x.vd[1];
}

static ALWAYS_INLINE uint32_t sum_epi16(vec16_t xmm)
{
    union xmm x1 = {.w = xmm}, x2 = {.w = xmm};

    x1.d &= (vec32_t){
        0x0000ffff,
        0x0000ffff,
        0x0000ffff,
        0x0000ffff,
    };
    x2.i = _mm_srli_epi32(x2.i, 16);
    return sum_epi32(x1.d + x2.d);
}

/* Mask of the zero elements: all the bits set where the element is zero,
 * cleared where it is not.
 */

static ALWAYS_INLINE vec8_t vec_zero_mask8(vec8_t v)
{
    return __builtin_ia32_pcmpeqb128(v, (vec8_t){0});
}

static ALWAYS_INLINE vec16_t vec_zero_mask16(vec16_t v)
{
    return __builtin_ia32_pcmpeqw128(v, (vec16_t){0});
}

static ALWAYS_INLINE vec32_t vec_zero_mask32(vec32_t v)
{
    return __builtin_ia32_pcmpeqd128(v, (vec32_t){0});
}

/* Sum all the elements of a vector, as unsigned. */

static ALWAYS_INLINE uint32_t vec_sum8(vec8_t v)
{
    /* psadbw(a0..a15, 0) -> (vec64_t){ a0 + … + a7, a8 + … + a15 } */
    return sum_epi64(__builtin_ia32_psadbw128(v, (vec8_t){0}));
}

static ALWAYS_INLINE uint32_t vec_sum16(vec16_t v)
{
    return sum_epi16(v);
}

static ALWAYS_INLINE uint32_t vec_sum32(vec32_t v)
{
    return sum_epi32(v);
}

static ALWAYS_INLINE bool is_128bits_zero(const void *v)
{
    __m128i t = _mm_cmpeq_epi32(*(const __m128i *)v, (__m128i){0});

    return _mm_movemask_epi8(t) == 0xffff;
}

/* Whether an unaligned vector is entirely zero.
 *
 * This is asked once per vector by the scans, and it is the only thing
 * they ask on the path that finds nothing, which is nearly all of
 * them. Keeping it apart from the position below is what lets a
 * compiler leave the search for the position out of that path.
 */

static ALWAYS_INLINE bool is_zero16_unaligned(const uint16_t u16[])
{
    __m128i v = _mm_loadu_si128((const __m128i *)u16);

    return _mm_movemask_epi8(_mm_cmpeq_epi16(v, (__m128i){0})) == 0xffff;
}

static ALWAYS_INLINE bool is_zero32_unaligned(const uint32_t u32[])
{
    __m128i v = _mm_loadu_si128((const __m128i *)u32);

    return _mm_movemask_epi8(_mm_cmpeq_epi32(v, (__m128i){0})) == 0xffff;
}

/* Position of the first non zero element of an unaligned vector.
 *
 * Only called once a vector is known to hold one.
 */

static ALWAYS_INLINE int first_non_zero16(const uint16_t u16[])
{
    __m128i v = _mm_loadu_si128((const __m128i *)u16);
    int m = _mm_movemask_epi8(_mm_cmpeq_epi16(v, (__m128i){0}));

    return __builtin_ctz(~m) / 2;
}

static ALWAYS_INLINE int first_non_zero32(const uint32_t u32[])
{
    __m128i v = _mm_loadu_si128((const __m128i *)u32);
    int m = _mm_movemask_epi8(_mm_cmpeq_epi32(v, (__m128i){0}));

    return __builtin_ctz(~m) / 4;
}

/* }}} */
#else
/* Portable primitives {{{ */

/* The compiler vector extensions map the arithmetic operators to the SIMD
 * instruction set of the target, and a comparison sets all the bits of the
 * matching elements, exactly like the SSE2 pcmpeq* builtins.
 */
typedef uint8_t vec8_t __attribute__((vector_size(16)));
typedef uint16_t vec16_t __attribute__((vector_size(16)));
typedef uint32_t vec32_t __attribute__((vector_size(16)));

/* Mask of the zero elements: all the bits set where the element is zero,
 * cleared where it is not.
 */

static ALWAYS_INLINE vec8_t vec_zero_mask8(vec8_t v)
{
    return (vec8_t)(v == (vec8_t){0});
}

static ALWAYS_INLINE vec16_t vec_zero_mask16(vec16_t v)
{
    return (vec16_t)(v == (vec16_t){0});
}

static ALWAYS_INLINE vec32_t vec_zero_mask32(vec32_t v)
{
    return (vec32_t)(v == (vec32_t){0});
}

/* Sum all the elements of a vector, as unsigned. */

static ALWAYS_INLINE uint32_t vec_sum8(vec8_t v)
{
    uint32_t res = 0;

    for (int i = 0; i < 16; i++) {
        res += v[i];
    }
    return res;
}

static ALWAYS_INLINE uint32_t vec_sum16(vec16_t v)
{
    uint32_t res = 0;

    for (int i = 0; i < 8; i++) {
        res += v[i];
    }
    return res;
}

static ALWAYS_INLINE uint32_t vec_sum32(vec32_t v)
{
    uint32_t res = 0;

    for (int i = 0; i < 4; i++) {
        res += v[i];
    }
    return res;
}

static ALWAYS_INLINE bool is_128bits_zero(const void *v)
{
    const uint64_t *u64 = v;

    return (u64[0] | u64[1]) == 0;
}

static ALWAYS_INLINE bool is_128bits_zero_unaligned(const void *v)
{
    uint64_t u64[2];

    memcpy(u64, v, sizeof(u64));
    return (u64[0] | u64[1]) == 0;
}

/* Position of the first non zero element of an unaligned vector, -1 if all
 * the elements are zero. The element is looked up in memory order, so the
 * scan does not depend on the endianness.
 */

static ALWAYS_INLINE bool is_zero16_unaligned(const uint16_t u16[])
{
    return is_128bits_zero_unaligned(u16);
}

static ALWAYS_INLINE bool is_zero32_unaligned(const uint32_t u32[])
{
    return is_128bits_zero_unaligned(u32);
}

static ALWAYS_INLINE int first_non_zero16(const uint16_t u16[])
{
    for (int i = 0; i < 7; i++) {
        if (u16[i]) {
            return i;
        }
    }
    /* the vector is not zero, so the last element is the non zero one */
    return 7;
}

static ALWAYS_INLINE int first_non_zero32(const uint32_t u32[])
{
    for (int i = 0; i < 3; i++) {
        if (u32[i]) {
            return i;
        }
    }
    return 3;
}

/* }}} */
#endif
/* Scans {{{ */

bool is_memory_zero(const void *_data, size_t n)
{
    const uint8_t *data = _data;

    assert(n % 64 == 0);
    assert((uintptr_t)data % 16 == 0);
    for (size_t i = 0; i < n; i += 64) {
        if (!is_128bits_zero(data + i + 0)) {
            return false;
        }
        if (!is_128bits_zero(data + i + 16)) {
            return false;
        }
        if (!is_128bits_zero(data + i + 32)) {
            return false;
        }
        if (!is_128bits_zero(data + i + 48)) {
            return false;
        }
    }
    return true;
}

ssize_t scan_non_zero16(const uint16_t u16[], size_t pos, size_t len)
{
    if (len - pos >= 8) {
#define T(offs)                                                              \
    do {                                                                     \
        const uint16_t *_v = u16 + (offs);                                   \
                                                                             \
        if (!is_zero16_unaligned(_v)) {                                      \
            return (ssize_t)(offs) + first_non_zero16(_v);                   \
        }                                                                    \
    } while (0)
        for (; pos + 32 <= len; pos += 32) {
            T(pos + 0);
            T(pos + 8);
            T(pos + 16);
            T(pos + 24);
        }
        for (; pos + 8 <= len; pos += 8) {
            T(pos);
        }
#undef T
    }

    for (; pos < len; pos++) {
        if (u16[pos]) {
            return pos;
        }
    }
    return -1;
}

ssize_t scan_non_zero32(const uint32_t u32[], size_t pos, size_t len)
{
    if (len - pos >= 4) {
#define T(offs)                                                              \
    do {                                                                     \
        const uint32_t *_v = u32 + (offs);                                   \
                                                                             \
        if (!is_zero32_unaligned(_v)) {                                      \
            return (ssize_t)(offs) + first_non_zero32(_v);                   \
        }                                                                    \
    } while (0)
        for (; pos + 16 <= len; pos += 16) {
            T(pos + 0);
            T(pos + 4);
            T(pos + 8);
            T(pos + 12);
        }
        for (; pos + 4 <= len; pos += 4) {
            T(pos);
        }
#undef T
    }

    if (pos + 0 == len) {
        return -1;
    }
    if (u32[pos + 0]) {
        return pos + 0;
    }
    if (pos + 1 == len) {
        return -1;
    }
    if (u32[pos + 1]) {
        return pos + 1;
    }
    if (pos + 2 == len) {
        return -1;
    }
    if (u32[pos + 2]) {
        return pos + 2;
    }
    return -1;
}

size_t count_non_zero8(const uint8_t u8[], size_t n)
{
    const vec8_t zero = {0};
    size_t nb_zero = 0;

    assert(n % 64 == 0);
    assert((uintptr_t)u8 % 16 == 0);
    for (size_t i = 0; i < n;) {
        vec8_t acc0 = zero, acc1 = zero, acc2 = zero, acc3 = zero;

        /* avoid overflows in acc0 + acc1 + acc2 + acc3, 63 * 4 < 256 */
        for (uint32_t j = 0; j < 63 && i < n; j++, i += 64) {
            acc0 -= vec_zero_mask8(*(const vec8_t *)(u8 + i + 0));
            acc1 -= vec_zero_mask8(*(const vec8_t *)(u8 + i + 16));
            acc2 -= vec_zero_mask8(*(const vec8_t *)(u8 + i + 32));
            acc3 -= vec_zero_mask8(*(const vec8_t *)(u8 + i + 48));
        }
        nb_zero += vec_sum8(acc0 + acc1 + acc2 + acc3);
    }
    return n - nb_zero;
}

size_t count_non_zero16(const uint16_t u16[], size_t n)
{
    const vec16_t zero = {0};
    vec16_t acc0 = zero;
    vec16_t acc1 = zero;
    vec16_t acc2 = zero;
    vec16_t acc3 = zero;

    assert(n < INT16_MAX * 4);
    assert(n % 32 == 0);
    assert((uintptr_t)u16 % 16 == 0);
    for (uint32_t i = 0; i < n; i += 32) {
        acc0 -= vec_zero_mask16(*(const vec16_t *)(u16 + i + 0));
        acc1 -= vec_zero_mask16(*(const vec16_t *)(u16 + i + 8));
        acc2 -= vec_zero_mask16(*(const vec16_t *)(u16 + i + 16));
        acc3 -= vec_zero_mask16(*(const vec16_t *)(u16 + i + 24));
    }
    return n - vec_sum16(acc0 + acc1 + acc2 + acc3);
}

size_t count_non_zero32(const uint32_t u32[], size_t n)
{
    const vec32_t zero = {0};
    vec32_t acc0 = zero;
    vec32_t acc1 = zero;
    vec32_t acc2 = zero;
    vec32_t acc3 = zero;

    assert(n < (uint32_t)INT32_MAX * 2);
    assert(n % 16 == 0);
    assert((uintptr_t)u32 % 16 == 0);
    for (uint32_t i = 0; i < n; i += 16) {
        acc0 -= vec_zero_mask32(*(const vec32_t *)(u32 + i + 0));
        acc1 -= vec_zero_mask32(*(const vec32_t *)(u32 + i + 4));
        acc2 -= vec_zero_mask32(*(const vec32_t *)(u32 + i + 8));
        acc3 -= vec_zero_mask32(*(const vec32_t *)(u32 + i + 12));
    }
    return n - vec_sum32(acc0 + acc1 + acc2 + acc3);
}

static size_t count_non_zero64_naive(const uint64_t u64[], size_t n)
{
    register size_t acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;

    for (size_t i = 0; i < n; i += 4) {
        acc0 += !u64[i + 0];
        acc1 += !u64[i + 1];
        acc2 += !u64[i + 2];
        acc3 += !u64[i + 3];
    }
    return n - (acc0 + acc1 + acc2 + acc3);
}

#if defined(__HAS_CPUID) && defined(__SSE2__)
#  pragma push_macro("__attr_leaf__")
#  undef __attr_leaf__
#  include <cpuid.h>
#  pragma pop_macro("__attr_leaf__")

#  if defined(__clang__) && !defined(__builtin_ia32_pcmpeqq)
#    define __builtin_ia32_pcmpeqq(a, b) _mm_cmpeq_epi64(a, b)
#  endif

__attribute__((target("sse4.1"))) static size_t
count_non_zero64_sse41(const uint64_t u64[], size_t n)
{
    const vec64_t zero = {0};
    vec64_t acc0 = zero;
    vec64_t acc1 = zero;
    vec64_t acc2 = zero;
    vec64_t acc3 = zero;

    assert(n % 8 == 0);
    assert((uintptr_t)u64 % 16 == 0);
    for (uint32_t i = 0; i < n; i += 8) {
        acc0 -= __builtin_ia32_pcmpeqq(*(vec64_t *)(u64 + i + 0), zero);
        acc1 -= __builtin_ia32_pcmpeqq(*(vec64_t *)(u64 + i + 2), zero);
        acc2 -= __builtin_ia32_pcmpeqq(*(vec64_t *)(u64 + i + 4), zero);
        acc3 -= __builtin_ia32_pcmpeqq(*(vec64_t *)(u64 + i + 6), zero);
    }
    return n - sum_epi64(acc0 + acc1 + acc2 + acc3);
}

static size_t count_non_zero64_resolve(const uint64_t u64[], size_t n)
{
    int eax, ebx, ecx, edx;

    __cpuid(1, eax, ebx, ecx, edx);
    count_non_zero64 = &count_non_zero64_naive;
    if (ecx & bit_SSE4_1) {
        count_non_zero64 = &count_non_zero64_sse41;
    }

    return (*count_non_zero64)(u64, n);
}

size_t (*count_non_zero64)(const uint64_t[], size_t) =
    &count_non_zero64_resolve;

#else

size_t (*count_non_zero64)(const uint64_t[], size_t) =
    &count_non_zero64_naive;

#endif

size_t count_non_zero128(const void *_data, size_t n)
{
    const struct {
        uint64_t h;
        uint64_t l;
    } *data = _data;
    register uint32_t acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;

    assert(n % 4 == 0);
    for (uint32_t i = 0; i < n; i += 4) {
        acc0 += is_128bits_zero(data + i + 0);
        acc1 += is_128bits_zero(data + i + 1);
        acc2 += is_128bits_zero(data + i + 2);
        acc3 += is_128bits_zero(data + i + 3);
    }
    return n - (acc0 + acc1 + acc2 + acc3);
}

/* }}} */
/* Tests {{{ */

#include <lib-common/z.h>
#include <lib-common/datetime.h>

#define IS_ZERO(Size, Val) IS_ZERO##Size(Val)
#define IS_ZERO128(Val) is_128bits_zero(&Val)
#define IS_ZERO8(Val) (Val == 0)
#define IS_ZERO16(Val) (Val == 0)
#define IS_ZERO32(Val) (Val == 0)
#define IS_ZERO64(Val) (Val == 0)

/* Number of elements of the sweep that sets one element at a time. It is a
 * multiple of the block size of every counter.
 */
#define COUNT_SWEEP 512

/* Check count_non_zero##Size() on every buffer size it accepts, up to
 * MaxCount elements.
 *
 * A buffer of zeros and a buffer without any zero push the vector
 * accumulators to their two extremes. A single non zero element checks every
 * lane of every vector, the last vector included. MaxCount of
 * count_non_zero8() covers more than one block of 4032 bytes, where the byte
 * accumulators are summed and reset.
 */
#define DO_TEST_COUNT(Size, Step, MaxCount)                                  \
    do {                                                                     \
        static __attribute__((aligned(16))) uint##Size##_t v[MaxCount];      \
                                                                             \
        for (size_t n = (Step); n <= (MaxCount); n += (Step)) {              \
            p_clear(v, n);                                                   \
            Z_ASSERT_EQ(                                                     \
                (size_t)0, count_non_zero##Size(v, n),                       \
                "buffer of %zu zeroed elements", n                           \
            );                                                               \
                                                                             \
            memset(v, 0xff, n * ((Size) / 8));                               \
            Z_ASSERT_EQ(                                                     \
                n, count_non_zero##Size(v, n),                               \
                "buffer of %zu non zero elements", n                         \
            );                                                               \
                                                                             \
            p_clear(v, n);                                                   \
            v[n - 1] = 1;                                                    \
            Z_ASSERT_EQ(                                                     \
                (size_t)1, count_non_zero##Size(v, n),                       \
                "last element set in a buffer of %zu elements", n            \
            );                                                               \
        }                                                                    \
                                                                             \
        for (size_t i = 0; i < COUNT_SWEEP; i++) {                           \
            p_clear(v, COUNT_SWEEP);                                         \
            v[i] = 1;                                                        \
            Z_ASSERT_EQ(                                                     \
                (size_t)1, count_non_zero##Size(v, COUNT_SWEEP),             \
                "element %zu set in a buffer of %d elements", i, COUNT_SWEEP \
            );                                                               \
        }                                                                    \
    } while (0)

/* Check is_memory_zero() on every size it accepts, up to the size of the
 * buffer. Set one byte at a time, at every position of the buffer.
 */
static int test_is_memory_zero(void)
{
    static __attribute__((aligned(16))) uint8_t v[512];

    for (size_t n = 64; n <= sizeof(v); n += 64) {
        p_clear(v, countof(v));
        Z_ASSERT(is_memory_zero(v, n), "buffer of %zu zeroed bytes", n);

        for (size_t i = 0; i < n; i++) {
            v[i] = 1;
            Z_ASSERT(
                !is_memory_zero(v, n),
                "byte %zu set in a buffer of %zu bytes", i, n
            );
            v[i] = 0;
        }
    }
    Z_HELPER_END;
}

static int test_scan_non_zero16(void)
{
    for (int n = 1; n < 140; n++) {
        uint16_t *buf;

        t_scope;

        buf = t_new(uint16_t, n);
        Z_ASSERT_P(buf, "cannot allocate");
        for (int i = 0; i < n; i++) {
            p_clear(buf, n);
            buf[i] = 1;
            for (int j = 0; j < n; j++) {
                Z_ASSERT_EQ(
                    i < j ? -1 : i, scan_non_zero16(buf, j, n),
                    "scan_non_zero16 failed for size=%d at index=%d "
                    "(starting at %d)",
                    n, i, j
                );
            }
            buf[i] = -1;
            for (int j = 0; j < n; j++) {
                Z_ASSERT_EQ(
                    i < j ? -1 : i, scan_non_zero16(buf, j, n),
                    "scan_non_zero16 failed for size=%d at index=%d "
                    "(starting at %d)",
                    n, i, j
                );
            }
        }
        p_clear(buf, n);
        for (int j = 0; j < n; j++) {
            Z_ASSERT_EQ(
                -1, scan_non_zero16(buf, 0, n),
                "scan_non_zero16 "
                "failed for zeros buf of size=%d (starting at %d)",
                n, j
            );
        }
    }
    Z_HELPER_END;
}

static int test_scan_non_zero32(void)
{
    for (int n = 1; n < 140; n++) {
        uint32_t *buf;

        t_scope;

        buf = t_new(uint32_t, n);
        Z_ASSERT_P(buf, "cannot allocate");
        for (int i = 0; i < n; i++) {
            p_clear(buf, n);
            buf[i] = 1;
            for (int j = 0; j < n; j++) {
                Z_ASSERT_EQ(
                    i < j ? -1 : i, scan_non_zero32(buf, j, n),
                    "scan_non_zero32 failed for size=%d at index=%d "
                    "(starting at %d)",
                    n, i, j
                );
            }
            buf[i] = -1;
            for (int j = 0; j < n; j++) {
                Z_ASSERT_EQ(
                    i < j ? -1 : i, scan_non_zero32(buf, j, n),
                    "scan_non_zero32 failed for size=%d at index=%d "
                    "(starting at %d)",
                    n, i, j
                );
            }
        }
        p_clear(buf, n);
        for (int j = 0; j < n; j++) {
            Z_ASSERT_EQ(
                -1, scan_non_zero32(buf, 0, n),
                "scan_non_zero32 "
                "failed for zeros buf of size=%d (starting at %d)",
                n, j
            );
        }
    }
    Z_HELPER_END;
}

Z_GROUP_EXPORT(arith_scan) {
    srand(0);

#define DO_TEST(Size, Count, Get)                                            \
    __attribute__((aligned(4096))) uint##Size##_t v[Count];                  \
    int set = 0;                                                             \
                                                                             \
    p_clear(&v, 1);                                                          \
    Z_ASSERT_EQ(0u, count_non_zero##Size(v, Count));                         \
    Z_ASSERT(is_memory_zero(v, Count *(Size / 8)));                          \
    for (int i = 0; i < 30; i++) {                                           \
        int fill = Count / 10 + rand() % 30;                                 \
                                                                             \
        for (int p = 0; p < fill; p++) {                                     \
            int pos = rand() % Count;                                        \
                                                                             \
            if (IS_ZERO(Size, v[pos])) {                                     \
                set++;                                                       \
            }                                                                \
            Get(v[pos]) <<= 1;                                               \
            Get(v[pos]) += 1;                                                \
        }                                                                    \
        Z_ASSERT_EQ((unsigned)set, count_non_zero##Size(v, Count));          \
        Z_ASSERT(!is_memory_zero(v, Count * (Size / 8)));                    \
    }

#define GET(V) V

    Z_TEST(8) {
        DO_TEST(8, 4096, GET);
    } Z_TEST_END;

    Z_TEST(16) {
        DO_TEST(16, 2048, GET);
    } Z_TEST_END;

    Z_TEST(32) {
        DO_TEST(32, 1024, GET);
    } Z_TEST_END;

    Z_TEST(64) {
        DO_TEST(64, 1024, GET);
    } Z_TEST_END;

    Z_TEST(128) {
        DO_TEST(128, 1024, GET);
    } Z_TEST_END;

    Z_TEST(count_non_zero8) {
        DO_TEST_COUNT(8, 64, 8256);
    } Z_TEST_END;

    Z_TEST(count_non_zero16) {
        DO_TEST_COUNT(16, 32, 2048);
    } Z_TEST_END;

    Z_TEST(count_non_zero32) {
        DO_TEST_COUNT(32, 16, 1024);
    } Z_TEST_END;

    Z_TEST(count_non_zero64) {
        DO_TEST_COUNT(64, 8, 1024);
    } Z_TEST_END;

    Z_TEST(count_non_zero128) {
        DO_TEST_COUNT(128, 4, 1024);
    } Z_TEST_END;

    Z_TEST(is_memory_zero) {
        Z_HELPER_RUN(test_is_memory_zero());
    } Z_TEST_END;

    Z_TEST(scan_non_zero16) {
        Z_HELPER_RUN(test_scan_non_zero16());
    } Z_TEST_END;

    Z_TEST(scan_non_zero32) {
        Z_HELPER_RUN(test_scan_non_zero32());
    } Z_TEST_END;
#undef GET
#undef DO_TEST
#undef DO_TEST_COUNT
#undef COUNT_SWEEP
} Z_GROUP_END;

/* }}} */
