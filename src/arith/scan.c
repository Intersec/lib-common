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

#include <lib-common/arith.h>
#ifndef __SSE2__
#   error "scan requires SSE2"
#endif

/* GCC before 4.4 only supports SSE2 and has no x86intrin.h */
#if defined(__clang__) || __GNUC_PREREQ(4, 4)
#   pragma push_macro("__leaf")
#   undef __leaf
#   include <x86intrin.h>
#   pragma pop_macro("__leaf")
#else
#   include <emmintrin.h>
#endif

#if defined(__clang__) && !defined(__builtin_ia32_pcmpeqb128)
#   define __builtin_ia32_pcmpeqb128(a, b)  ((a) == (b))
#   define __builtin_ia32_pcmpeqw128(a, b)  ((a) == (b))
#   define __builtin_ia32_pcmpeqd128(a, b)  ((a) == (b))
#endif

union xmm {
    __m128i  i;
    __v16qi  b;
    __v8hi   w;
    __v4si   d;
    __v2di   q;

    __v2df   df;
    __v4sf   sf;

    uint8_t  vb[16];
    uint16_t vw[8];
    uint32_t vd[4];
    uint64_t vq[2];
};

/* SSE2 {{{ */

static ALWAYS_INLINE uint32_t sum_epi64(__v2di xmm)
{
    union xmm x = { .q = xmm };

    x.sf = _mm_movehl_ps(x.sf, x.sf); /* x.q[0] <- x.q[1] */
    x.q += xmm;                       /* x.q[0] += xmm[0] */
    return x.vd[0];
}

static ALWAYS_INLINE uint32_t sum_epi32(__v4si xmm)
{
    union xmm x = { .d = xmm };

    x.sf = _mm_movehl_ps(x.sf, x.sf); /* x.d[0..1] <- x.q[2..3] */
    x.d += xmm;                       /* x.d[0..1] += xmm[0..1] */
    return x.vd[0] + x.vd[1];
}

static ALWAYS_INLINE uint32_t sum_epi16(__v8hi xmm)
{
    union xmm x1 = { .w = xmm }, x2 = { .w = xmm };

    x1.d &= (__v4si){ 0x0000ffff, 0x0000ffff, 0x0000ffff, 0x0000ffff, };
    x2.i  = _mm_srli_epi32(x2.i, 16);
    return sum_epi32(x1.d + x2.d);
}

static ALWAYS_INLINE bool is_128bits_zero(const void *v)
{
    __m128i t = _mm_cmpeq_epi32(*(const __m128i *)v, (__m128i){ 0, });
    return _mm_movemask_epi8(t) == 0xffff;
}

bool is_memory_zero(const void *_data, size_t n)
{
    const uint8_t *data = _data;

    assert (n % 64 == 0);
    assert ((uintptr_t)data % 16 == 0);
    for (size_t i = 0; i < n; i += 64) {
        if (!is_128bits_zero(data + i +  0))
            return false;
        if (!is_128bits_zero(data + i + 16))
            return false;
        if (!is_128bits_zero(data + i + 32))
            return false;
        if (!is_128bits_zero(data + i + 48))
            return false;
    }
    return true;
}

ssize_t scan_non_zero16(const uint16_t u16[], size_t pos, size_t len)
{
    if (len - pos >= 8) {
#define T(x, offs) \
        ({  __m128i c = _mm_cmpeq_epi16(x, (__m128i){ 0, });   \
            int     m = _mm_movemask_epi8(c);                  \
            if (m != 0xffff)                                   \
                return offs + __builtin_ctz(~m) / 2;           \
        })
        for (; pos + 32 <= len; pos += 32) {
            T(_mm_loadu_si128((__m128i *)(u16 + pos +  0)), pos +  0);
            T(_mm_loadu_si128((__m128i *)(u16 + pos +  8)), pos +  8);
            T(_mm_loadu_si128((__m128i *)(u16 + pos + 16)), pos + 16);
            T(_mm_loadu_si128((__m128i *)(u16 + pos + 24)), pos + 24);
        }
        for (; pos + 8 <= len; pos += 8) {
            T(_mm_loadu_si128((__m128i *)(u16 + pos)), pos);
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
#define T(x, offs) \
        ({  __m128i c = _mm_cmpeq_epi32(x, (__m128i){ 0, });   \
            int     m = _mm_movemask_epi8(c);                  \
            if (m != 0xffff)                                   \
                return offs + __builtin_ctz(~m) / 4;           \
        })
        for (; pos + 16 <= len; pos += 16) {
            T(_mm_loadu_si128((__m128i *)(u32 + pos +  0)), pos +  0);
            T(_mm_loadu_si128((__m128i *)(u32 + pos +  4)), pos +  4);
            T(_mm_loadu_si128((__m128i *)(u32 + pos +  8)), pos +  8);
            T(_mm_loadu_si128((__m128i *)(u32 + pos + 12)), pos + 12);
        }
        for (; pos + 4 <= len; pos += 4) {
            T(_mm_loadu_si128((__m128i *)(u32 + pos)), pos);
        }
#undef T
    }

    if (pos + 0 == len) return -1;
    if (u32[pos + 0])   return pos + 0;
    if (pos + 1 == len) return -1;
    if (u32[pos + 1])   return pos + 1;
    if (pos + 2 == len) return -1;
    if (u32[pos + 2])   return pos + 2;
    return -1;
}

size_t count_non_zero8(const uint8_t u8[], size_t n)
{
    const __v16qi zero = { 0, };
    __v2di  acc  = { 0, };

    assert (n % 64 == 0);
    assert ((uintptr_t)u8 % 16 == 0);
    for (uint32_t i = 0; i < n; ) {
        __v16qi acc0 = zero, acc1 = zero, acc2 = zero, acc3 = zero;

        /* avoid overflows in acc0 + acc1 + acc2 + acc3, 63 * 4 < 256 */
        for (uint32_t j = 0; j < 63 && i < n; j++, i += 64) {
            acc0 -= __builtin_ia32_pcmpeqb128(*(__v16qi *)(u8 + i +  0), zero);
            acc1 -= __builtin_ia32_pcmpeqb128(*(__v16qi *)(u8 + i + 16), zero);
            acc2 -= __builtin_ia32_pcmpeqb128(*(__v16qi *)(u8 + i + 32), zero);
            acc3 -= __builtin_ia32_pcmpeqb128(*(__v16qi *)(u8 + i + 48), zero);
        }
        /* psadbw(a0..a15, 0) -> (__v2di){ a0 + … + a7, a8 + … + a15 } */
        acc += __builtin_ia32_psadbw128(acc0 + acc1 + acc2 + acc3, zero);
    }
    return n - sum_epi64(acc);
}

size_t count_non_zero16(const uint16_t u16[], size_t n)
{
    const __v8hi zero = { 0, };
    __v8hi acc0 = zero;
    __v8hi acc1 = zero;
    __v8hi acc2 = zero;
    __v8hi acc3 = zero;

    assert (n < INT16_MAX * 4);
    assert (n % 32 == 0);
    assert ((uintptr_t)u16 % 16 == 0);
    for (uint32_t i = 0; i < n; i += 32) {
        acc0 -= __builtin_ia32_pcmpeqw128(*(__v8hi *)(u16 + i +  0), zero);
        acc1 -= __builtin_ia32_pcmpeqw128(*(__v8hi *)(u16 + i +  8), zero);
        acc2 -= __builtin_ia32_pcmpeqw128(*(__v8hi *)(u16 + i + 16), zero);
        acc3 -= __builtin_ia32_pcmpeqw128(*(__v8hi *)(u16 + i + 24), zero);
    }
    return n - sum_epi16(acc0 + acc1 + acc2 + acc3);
}

size_t count_non_zero32(const uint32_t u32[], size_t n)
{
    const __v4si zero = { 0, };
    __v4si acc0 = zero;
    __v4si acc1 = zero;
    __v4si acc2 = zero;
    __v4si acc3 = zero;

    assert (n < (uint32_t)INT32_MAX * 2);
    assert (n % 16 == 0);
    assert ((uintptr_t)u32 % 16 == 0);
    for (uint32_t i = 0; i < n; i += 16) {
        acc0 -= __builtin_ia32_pcmpeqd128(*(__v4si *)(u32 + i +  0), zero);
        acc1 -= __builtin_ia32_pcmpeqd128(*(__v4si *)(u32 + i +  4), zero);
        acc2 -= __builtin_ia32_pcmpeqd128(*(__v4si *)(u32 + i +  8), zero);
        acc3 -= __builtin_ia32_pcmpeqd128(*(__v4si *)(u32 + i + 12), zero);
    }
    return n - sum_epi32(acc0 + acc1 + acc2 + acc3);
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

#ifdef __HAS_CPUID
#pragma push_macro("__leaf")
#undef __leaf
#include <cpuid.h>
#pragma pop_macro("__leaf")

#if defined(__clang__) && !defined(__builtin_ia32_pcmpeqq)
#   define __builtin_ia32_pcmpeqq(a, b)  _mm_cmpeq_epi64(a, b)
#endif

__attribute__((target("sse4.1")))
static size_t count_non_zero64_sse41(const uint64_t u64[], size_t n)
{
    const __v2di zero = { 0, };
    __v2di acc0 = zero;
    __v2di acc1 = zero;
    __v2di acc2 = zero;
    __v2di acc3 = zero;

    assert (n % 8 == 0);
    assert ((uintptr_t)u64 % 16 == 0);
    for (uint32_t i = 0; i < n; i += 8) {
        acc0 -= __builtin_ia32_pcmpeqq(*(__v2di *)(u64 + i + 0), zero);
        acc1 -= __builtin_ia32_pcmpeqq(*(__v2di *)(u64 + i + 2), zero);
        acc2 -= __builtin_ia32_pcmpeqq(*(__v2di *)(u64 + i + 4), zero);
        acc3 -= __builtin_ia32_pcmpeqq(*(__v2di *)(u64 + i + 6), zero);
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

size_t (*count_non_zero64)(const uint64_t[], size_t) = &count_non_zero64_resolve;

#else

size_t (*count_non_zero64)(const uint64_t[], size_t) = &count_non_zero64_naive;

#endif

size_t count_non_zero128(const void *_data, size_t n)
{
    const struct {
        uint64_t h;
        uint64_t l;
    } *data = _data;
    register uint32_t acc0 = 0, acc1 = 0, acc2 = 0, acc3 = 0;

    assert (n % 4 == 0);
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

typedef struct { uint64_t h; uint64_t l; } uint128_t;

#define IS_ZERO(Size, Val)  IS_ZERO##Size(Val)
#define IS_ZERO128(Val)     is_128bits_zero(&Val)
#define IS_ZERO8(Val)       (Val == 0)
#define IS_ZERO16(Val)      (Val == 0)
#define IS_ZERO32(Val)      (Val == 0)
#define IS_ZERO64(Val)      (Val == 0)


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
                Z_ASSERT_EQ(i < j ? -1 : i, scan_non_zero16(buf, j, n),
                            "scan_non_zero16 failed for size=%d at index=%d "
                            "(starting at %d)", n, i, j);
            }
            buf[i] = -1;
            for (int j = 0; j < n; j++) {
                Z_ASSERT_EQ(i < j ? -1 : i, scan_non_zero16(buf, j, n),
                            "scan_non_zero16 failed for size=%d at index=%d "
                            "(starting at %d)", n, i, j);
            }
        }
        p_clear(buf, n);
        for (int j = 0; j < n; j++) {
            Z_ASSERT_EQ(-1, scan_non_zero16(buf, 0, n), "scan_non_zero16 "
                        "failed for zeros buf of size=%d (starting at %d)",
                        n, j);
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
                Z_ASSERT_EQ(i < j ? -1 : i, scan_non_zero32(buf, j, n),
                            "scan_non_zero32 failed for size=%d at index=%d "
                            "(starting at %d)", n, i, j);
            }
            buf[i] = -1;
            for (int j = 0; j < n; j++) {
                Z_ASSERT_EQ(i < j ? -1 : i, scan_non_zero32(buf, j, n),
                            "scan_non_zero32 failed for size=%d at index=%d "
                            "(starting at %d)", n, i, j);
            }
        }
        p_clear(buf, n);
        for (int j = 0; j < n; j++) {
            Z_ASSERT_EQ(-1, scan_non_zero32(buf, 0, n), "scan_non_zero32 "
                        "failed for zeros buf of size=%d (starting at %d)",
                        n, j);
        }
    }
    Z_HELPER_END;
}


Z_GROUP_EXPORT(arith_sse)
{
    srand(0);

#define DO_TEST(Size, Count, Get)                                            \
    __attribute__((aligned(4096)))                                           \
    uint##Size##_t v[Count];                                                 \
    int set  = 0;                                                            \
                                                                             \
    p_clear(&v, 1);                                                          \
    Z_ASSERT_EQ(0u, count_non_zero##Size(v, Count));                         \
    Z_ASSERT(is_memory_zero(v, Count * (Size / 8)));                         \
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
            Get(v[pos])  += 1;                                               \
        }                                                                    \
        Z_ASSERT_EQ((unsigned)set, count_non_zero##Size(v, Count));          \
        Z_ASSERT(!is_memory_zero(v, Count * (Size / 8)));                    \
    }

#define GET(V)     V
#define GET128(V)  (V).l

    Z_TEST(8, "") {
        DO_TEST(8, 4096, GET);
    } Z_TEST_END;

    Z_TEST(16, "") {
        DO_TEST(16, 2048, GET);
    } Z_TEST_END;

    Z_TEST(32, "") {
        DO_TEST(32, 1024, GET);
    } Z_TEST_END;

    Z_TEST(64, "") {
        DO_TEST(64, 1024, GET);
    } Z_TEST_END;

    Z_TEST(128, "") {
        DO_TEST(128, 1024, GET128);
    } Z_TEST_END;

    Z_TEST(scan_non_zero16, "scan_non_zero16") {
        Z_HELPER_RUN(test_scan_non_zero16());
    } Z_TEST_END;

    Z_TEST(scan_non_zero32, "scan_non_zero32") {
        Z_HELPER_RUN(test_scan_non_zero32());
    } Z_TEST_END;
#undef GET
#undef GET128
#undef DO_TEST
} Z_GROUP_END

/* }}} */
