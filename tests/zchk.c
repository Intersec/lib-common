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

#include <lib-common/z.h>
#include <lib-common/unix.h>
#include <lib-common/bit.h>
#include <lib-common/parseopt.h>

Z_GROUP_EXPORT(endianess)
{
    Z_TEST(unaligned, "put_unaligned/get_unaligned") {
        byte data[BUFSIZ];
        uint16_t us;
        uint32_t u;
        uint64_t ul;

#define DO_TEST(w, e, x)                                                    \
        ({                                                                  \
            void *v1 = data, *v2;                                           \
            v2 = put_unaligned_##e##w(v1, x);                               \
            put_unaligned_##e##w(v2, x);                                    \
            Z_ASSERT_EQ(get_unaligned_##e##w(v1), x, "check 1 " #w #e);     \
            Z_ASSERT_EQ(get_unaligned_##e##w(v2), x, "check 2 " #w #e);     \
        })
        us = 0x0201;
        DO_TEST(16, cpu, us);
        DO_TEST(16,  be, us);
        DO_TEST(16,  le, us);

        u  = 0x030201;
        DO_TEST(24,  be, u);
        DO_TEST(24,  le, u);

        u  = 0x04030201;
        DO_TEST(32, cpu, u);
        DO_TEST(32,  be, u);
        DO_TEST(32,  le, u);

        ul = 0x060504030201;
        DO_TEST(48,  be, ul);
        DO_TEST(48,  le, ul);

        ul = 0x0807060504030201;
        DO_TEST(64, cpu, ul);
        DO_TEST(64,  be, ul);
        DO_TEST(64,  le, ul);

#undef DO_TEST
    } Z_TEST_END;
} Z_GROUP_END;

static int bs_check_length(const bit_stream_t bs, size_t len)
{
    Z_ASSERT_EQ(bs_len(&bs), len);
    Z_ASSERT_EQ(len == 0, bs_done(&bs));

    for (size_t i = len; i-- > 0;) {
        Z_ASSERT(bs_has(&bs, i));
    }
    for (size_t i = len + 1; i < len * 2 + 2; i++) {
        Z_ASSERT(!bs_has(&bs, i));
    }

    Z_HELPER_END;
}

static int bs_check_bounds(const bit_stream_t bs, const byte data[128],
                           int from, int to)
{
    const bit_stream_t bds = bs_init_ptroff(data, from, data, to);

    Z_ASSERT(bds.s.p == bs.s.p);
    Z_ASSERT_EQ(bds.s.offset, bs.s.offset);
    Z_ASSERT(bds.e.p == bs.e.p);
    Z_ASSERT_EQ(bds.e.offset, bs.e.offset);

    Z_HELPER_RUN(bs_check_length(bs, to - from));

    Z_HELPER_END;
}

Z_GROUP_EXPORT(bit_stream)
{
    /* Multiple of 64 in the range
        0 64 128 192 256
        320 384 448 512
        576 640 704 768
        832 896 960 1024
    */

#define Z_CHECK_LENGTH(Stream, Len, ...)  \
        Z_HELPER_RUN(bs_check_length(Stream, Len), ##__VA_ARGS__)

#define Z_CHECK_BOUNDS(Stream, From, To, ...)  \
        Z_HELPER_RUN(bs_check_bounds(Stream, data, From, To), ##__VA_ARGS__)

    /* Init {{{ */

    Z_TEST(len, "bit_stream: check length") {
        byte data[128];

        p_clear(&data, 1);
        Z_CHECK_LENGTH(bs_init_ptr(data, data), 0);
        Z_CHECK_LENGTH(bs_init_ptr(&data[1], &data[1]), 0);
        Z_CHECK_LENGTH(bs_init_ptr(&data[2], &data[2]), 0);
        Z_CHECK_LENGTH(bs_init_ptr(&data[3], &data[3]), 0);
        Z_CHECK_LENGTH(bs_init_ptr(&data[4], &data[4]), 0);
        Z_CHECK_LENGTH(bs_init_ptr(&data[5], &data[5]), 0);

        Z_CHECK_LENGTH(bs_init_ptroff(data, 0, data, 0), 0);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 8, &data[1], 0), 0);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 19, &data[2], 3), 0);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 138, &data[16], 10), 0);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 138, &data[17], 2), 0);

        Z_CHECK_LENGTH(bs_init_ptr(data, &data[1]), 8);
        Z_CHECK_LENGTH(bs_init_ptr(data, &data[2]), 16);
        Z_CHECK_LENGTH(bs_init_ptr(data, &data[3]), 24);
        Z_CHECK_LENGTH(bs_init_ptr(data, &data[4]), 32);
        Z_CHECK_LENGTH(bs_init_ptr(data, &data[8]), 64);
        Z_CHECK_LENGTH(bs_init_ptr(&data[3], &data[7]), 32);
        Z_CHECK_LENGTH(bs_init_ptr(&data[3], &data[19]), 128);
        Z_CHECK_LENGTH(bs_init_ptr(data, &data[128]), 1024);

        Z_CHECK_LENGTH(bs_init_ptroff(data, 0, data, 1), 1);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 3, data, 4), 1);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 7, data, 8), 1);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 63, data, 64), 1);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 0, data, 128), 128);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 19, data, 147), 128);
        Z_CHECK_LENGTH(bs_init_ptroff(data, 63, data, 191), 128);

    } Z_TEST_END;

    /* }}} */
    /* Skips/shrink {{{ */

    Z_TEST(skip, "bit_stream: bs_skip") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_skip(&bs, 1025));
        Z_ASSERT_EQ(bs_skip(&bs, 1024), 1024);
        Z_CHECK_BOUNDS(bs, 1024, 1024);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_skip(&bs, 0), 0);
        Z_CHECK_BOUNDS(bs, 0, 1024);

        Z_ASSERT_EQ(bs_skip(&bs, 13), 13);
        Z_CHECK_BOUNDS(bs, 13, 1024);

        Z_ASSERT_EQ(bs_skip(&bs, 51), 51);
        Z_CHECK_BOUNDS(bs, 64, 1024);

        Z_ASSERT_EQ(bs_skip(&bs, 70), 70);
        Z_CHECK_BOUNDS(bs, 134, 1024);

        Z_ASSERT_EQ(bs_skip(&bs, 2), 2);
        Z_CHECK_BOUNDS(bs, 136, 1024);

        Z_ASSERT_EQ(bs_skip(&bs, 128), 128);
        Z_CHECK_BOUNDS(bs, 264, 1024);
    } Z_TEST_END;

    Z_TEST(shrink, "bit_stream: bs_shrink") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_shrink(&bs, 1025));
        Z_ASSERT_EQ(bs_shrink(&bs, 1024), 1024);
        Z_CHECK_BOUNDS(bs, 0, 0);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_shrink(&bs, 0), 0);
        Z_CHECK_BOUNDS(bs, 0, 1024);

        Z_ASSERT_EQ(bs_shrink(&bs, 13), 13);
        Z_CHECK_BOUNDS(bs, 0, 1011);

        Z_ASSERT_EQ(bs_shrink(&bs, 51), 51);
        Z_CHECK_BOUNDS(bs, 0, 960);

        Z_ASSERT_EQ(bs_shrink(&bs, 70), 70);
        Z_CHECK_BOUNDS(bs, 0, 890);

        Z_ASSERT_EQ(bs_shrink(&bs, 2), 2);
        Z_CHECK_BOUNDS(bs, 0, 888);

        Z_ASSERT_EQ(bs_shrink(&bs, 128), 128);
        Z_CHECK_BOUNDS(bs, 0, 760);
    } Z_TEST_END;

    Z_TEST(skip_upto, "bit_stream: bs_skip_upto") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_skip_upto(&bs, data, 1025));
        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 1024), 1024);
        Z_CHECK_BOUNDS(bs, 1024, 1024);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 0), 0);
        Z_CHECK_BOUNDS(bs, 0, 1024);

        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 13), 13);
        Z_CHECK_BOUNDS(bs, 13, 1024);

        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 64), 51);
        Z_CHECK_BOUNDS(bs, 64, 1024);

        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 134), 70);
        Z_CHECK_BOUNDS(bs, 134, 1024);

        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 136), 2);
        Z_CHECK_BOUNDS(bs, 136, 1024);

        Z_ASSERT_EQ(bs_skip_upto(&bs, data, 264), 128);
        Z_CHECK_BOUNDS(bs, 264, 1024);
    } Z_TEST_END;

    Z_TEST(clip_at, "bit_stream: bs_clip_at") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_clip_at(&bs, data, 1025));
        Z_ASSERT_N(bs_clip_at(&bs, data, 0));
        Z_CHECK_BOUNDS(bs, 0, 0);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_N(bs_clip_at(&bs, data, 1024));
        Z_CHECK_BOUNDS(bs, 0, 1024);

        Z_ASSERT_N(bs_clip_at(&bs, data, 1011));
        Z_CHECK_BOUNDS(bs, 0, 1011);

        Z_ASSERT_N(bs_clip_at(&bs, data, 960));
        Z_CHECK_BOUNDS(bs, 0, 960);

        Z_ASSERT_N(bs_clip_at(&bs, data, 890));
        Z_CHECK_BOUNDS(bs, 0, 890);

        Z_ASSERT_N(bs_clip_at(&bs, data, 888));
        Z_CHECK_BOUNDS(bs, 0, 888);

        Z_ASSERT_N(bs_clip_at(&bs, data, 760));
        Z_CHECK_BOUNDS(bs, 0, 760);
    } Z_TEST_END;

    /* }}} */
    /* Extract {{{ */

    Z_TEST(extract_after, "bit_stream: bs_extract_after") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_extract_after(&bs, data, 1025, &n));
        Z_ASSERT_N(bs_extract_after(&bs, data, 0, &n));
        Z_CHECK_BOUNDS(bs, 0, 1024);
        Z_CHECK_BOUNDS(n, 0, 1024);

        bs = n;
        Z_ASSERT_N(bs_extract_after(&bs, data, 1024, &n));
        Z_CHECK_BOUNDS(bs, 0, 1024);
        Z_CHECK_BOUNDS(n, 1024, 1024);

        Z_ASSERT_N(bs_extract_after(&bs, data, 13, &n));
        Z_CHECK_BOUNDS(bs, 0, 1024);
        Z_CHECK_BOUNDS(n, 13, 1024);

        bs = n;
        Z_ASSERT_N(bs_extract_after(&bs, data, 64, &n));
        Z_CHECK_BOUNDS(bs, 13, 1024);
        Z_CHECK_BOUNDS(n, 64, 1024);

        bs = n;
        Z_ASSERT_N(bs_extract_after(&bs, data, 134, &n));
        Z_CHECK_BOUNDS(bs, 64, 1024);
        Z_CHECK_BOUNDS(n, 134, 1024);

        bs = n;
        Z_ASSERT_N(bs_extract_after(&bs, data, 136, &n));
        Z_CHECK_BOUNDS(bs, 134, 1024);
        Z_CHECK_BOUNDS(n, 136, 1024);

        bs = n;
        Z_ASSERT_N(bs_extract_after(&bs, data, 264, &n));
        Z_CHECK_BOUNDS(bs, 136, 1024);
        Z_CHECK_BOUNDS(n, 264, 1024);
    } Z_TEST_END;


    Z_TEST(get_bs_upto, "bit_stream: bs_get_bs_upto") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_get_bs_upto(&bs, data, 1025, &n));
        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 1024, &n));
        Z_CHECK_BOUNDS(bs, 1024, 1024);
        Z_CHECK_BOUNDS(n, 0, 1024);

        bs = n;
        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 0, &n));
        Z_CHECK_BOUNDS(bs, 0, 1024);
        Z_CHECK_BOUNDS(n, 0, 0);

        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 13, &n));
        Z_CHECK_BOUNDS(bs, 13, 1024);
        Z_CHECK_BOUNDS(n, 0, 13);

        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 64, &n));
        Z_CHECK_BOUNDS(bs, 64, 1024);
        Z_CHECK_BOUNDS(n, 13, 64);

        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 134, &n));
        Z_CHECK_BOUNDS(bs, 134, 1024);
        Z_CHECK_BOUNDS(n, 64, 134);

        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 136, &n));
        Z_CHECK_BOUNDS(bs, 136, 1024);
        Z_CHECK_BOUNDS(n, 134, 136);

        Z_ASSERT_N(bs_get_bs_upto(&bs, data, 264, &n));
        Z_CHECK_BOUNDS(bs, 264, 1024);
        Z_CHECK_BOUNDS(n, 136, 264);
    } Z_TEST_END;

    Z_TEST(get_bs, "bit_stream: bs_get_bs") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];

        p_clear(&data, 1);
        bs = bs_init_ptr(data, &data[128]);

        Z_ASSERT_NEG(bs_get_bs(&bs, 1025, &n));
        Z_ASSERT_N(bs_get_bs(&bs, 1024, &n));
        Z_CHECK_BOUNDS(bs, 1024, 1024);
        Z_CHECK_BOUNDS(n, 0, 1024);

        bs = n;
        Z_ASSERT_N(bs_get_bs(&bs, 0, &n));
        Z_CHECK_BOUNDS(bs, 0, 1024);
        Z_CHECK_BOUNDS(n, 0, 0);

        Z_ASSERT_N(bs_get_bs(&bs, 13, &n));
        Z_CHECK_BOUNDS(bs, 13, 1024);
        Z_CHECK_BOUNDS(n, 0, 13);

        Z_ASSERT_N(bs_get_bs(&bs, 51, &n));
        Z_CHECK_BOUNDS(bs, 64, 1024);
        Z_CHECK_BOUNDS(n, 13, 64);

        Z_ASSERT_N(bs_get_bs(&bs, 70, &n));
        Z_CHECK_BOUNDS(bs, 134, 1024);
        Z_CHECK_BOUNDS(n, 64, 134);

        Z_ASSERT_N(bs_get_bs(&bs, 2, &n));
        Z_CHECK_BOUNDS(bs, 136, 1024);
        Z_CHECK_BOUNDS(n, 134, 136);

        Z_ASSERT_N(bs_get_bs(&bs, 128, &n));
        Z_CHECK_BOUNDS(bs, 264, 1024);
        Z_CHECK_BOUNDS(n, 136, 264);
    } Z_TEST_END;

    /* }}} */
    /* Get bits {{{ */

#define Z_ASSERT_BIT(Expr, Bit) do {                                         \
        int __bit = (Expr);                                                  \
        Z_ASSERT_N(__bit);                                                   \
        Z_ASSERT_EQ(!!__bit, !!(Bit));                                       \
    } while (0)

#define Z_CHECK_BIT(bs, pos, Variant, Tst, Be)  do {                         \
        Z_ASSERT_BIT(Variant##peek_bit(&bs), Tst(data, pos));                \
        for (int j = 0; j < MIN(65, 1024 - pos); j++) {                      \
            n = bs;                                                          \
            Z_ASSERT_N(Variant##get_bits(&n,j, &res));                       \
            if (j != 64) {                                                   \
                Z_ASSERT_EQ(res & BITMASK_GE(uint64_t, j), 0ul, "%d %d",     \
                            pos, j);             \
            }                                                                \
            for (int k = 0; k < j; k++) {                                    \
                if (Be) {                                                    \
                    Z_ASSERT_EQ(!!TST_BIT(&res, j - k - 1),                  \
                                !!Tst(data, pos + k),                        \
                                "%d %d %d %jx", pos, j, k, res);             \
                } else {                                                     \
                    Z_ASSERT_EQ(!!TST_BIT(&res, k),                          \
                                !!Tst(data, pos + k));                       \
                }                                                            \
            }                                                                \
        }                                                                    \
        if (1024 - pos < 64) {                                               \
            Z_ASSERT_NEG(Variant##get_bits(&bs, 1024 - pos + 1, &res));      \
        }                                                                    \
        Z_ASSERT_NEG(Variant##get_bits(&bs, 65, &res));                      \
        Z_ASSERT_BIT(Variant##get_bit(&bs), Tst(data, pos));                 \
    } while (0)

    Z_TEST(get_bits, "bit_stream: bs_get_bits") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];
        uint64_t res = 0;

        for (int i = 0; i < countof(data); i++) {
            data[i] = i;
        }

        bs = bs_init_ptr(data, &data[128]);
        for (int i = 0; i < 1024; i++) {
            Z_CHECK_BIT(bs, i, bs_, TST_BIT, false);
            Z_CHECK_BOUNDS(bs, i + 1, 1024);
        }
        Z_ASSERT_NEG(bs_peek_bit(&bs));
        Z_ASSERT_NEG(bs_get_bit(&bs));
        Z_ASSERT_NEG(bs_get_bits(&bs, 1, &res));
    } Z_TEST_END;

#define TST_BE_BIT(d, pos)  ({                                               \
        size_t __offset = (pos);                                             \
        __offset = (__offset & ~7ul) + 7 - (__offset % 8);                   \
        TST_BIT(d, __offset);                                                \
    })

    Z_TEST(be_get_bits, "bit_stream: bs_be_get_bits") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];
        uint64_t res = 0;

        for (int i = 0; i < countof(data); i++) {
            data[i] = i;
        }

        bs = bs_init_ptr(data, &data[128]);
        for (int i = 0; i < 1024; i++) {
            Z_CHECK_BIT(bs, i, bs_be_, TST_BE_BIT, true);
            Z_CHECK_BOUNDS(bs, i + 1, 1024);
        }
        Z_ASSERT_NEG(bs_be_peek_bit(&bs));
        Z_ASSERT_NEG(bs_be_get_bit(&bs));
        Z_ASSERT_NEG(bs_be_get_bits(&bs, 1, &res));
    } Z_TEST_END;

    /* }}} */
    /* Scans {{{ */

    Z_TEST(skip_upto_bit, "bit_stream: bs_skip_upto_bit") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        SET_BIT(data, 3);
        SET_BIT(data, 165);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_skip_upto_bit(&bs, true, -1), 3);
        Z_ASSERT_BIT(bs_peek_bit(&bs), true);
        Z_CHECK_BOUNDS(bs, 3, 1024);

        Z_ASSERT_EQ(bs_skip_upto_bit(&bs, true, -1), 0);
        Z_CHECK_BOUNDS(bs, 3, 1024);

        Z_ASSERT_EQ(bs_skip_upto_bit(&bs, false, -1), 1);
        Z_ASSERT_BIT(bs_peek_bit(&bs), false);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_EQ(bs_skip_upto_bit(&bs, false, -1), 0);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_EQ(bs_skip_upto_bit(&bs, true, -1), 161);
        Z_ASSERT_BIT(bs_peek_bit(&bs), true);
        Z_CHECK_BOUNDS(bs, 165, 1024);

        Z_ASSERT_EQ(bs_skip_upto_bit(&bs, false, -1), 1);
        Z_ASSERT_BIT(bs_peek_bit(&bs), false);
        Z_CHECK_BOUNDS(bs, 166, 1024);

        Z_ASSERT_NEG(bs_skip_upto_bit(&bs, true, -1));
        Z_CHECK_BOUNDS(bs, 166, 1024);
    } Z_TEST_END;

    Z_TEST(skip_after_bit, "bit_stream: bs_skip_after_bit") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        SET_BIT(data, 3);
        SET_BIT(data, 165);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_skip_after_bit(&bs, true, -1), 4);
        Z_ASSERT_BIT(bs_peek_bit(&bs), false);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_EQ(bs_skip_after_bit(&bs, true, -1), 162);
        Z_ASSERT_BIT(bs_peek_bit(&bs), false);
        Z_CHECK_BOUNDS(bs, 166, 1024);

        Z_ASSERT_NEG(bs_skip_after_bit(&bs, true, -1));
        Z_CHECK_BOUNDS(bs, 166, 1024);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_skip_after_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 1, 1024);

        Z_ASSERT_EQ(bs_skip_after_bit(&bs, true, -1), 3);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_EQ(bs_skip_after_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 5, 1024);

        Z_ASSERT_EQ(bs_skip_after_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 6, 1024);
    } Z_TEST_END;

#define Z_CHECK_EXTRACTED(Stream, From, To, Bit)  do {                       \
        Z_CHECK_BOUNDS(Stream, From, To);                                    \
        for (int i = From; i < To; i++) {                                    \
            Z_ASSERT_BIT(bs_get_bit(&(Stream)), Bit);                        \
        }                                                                    \
        Z_ASSERT(bs_done(&(Stream)));                                        \
    } while (0)

    Z_TEST(get_bs_bit, "bit_stream: bs_get_bs_bit") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];

        p_clear(&data, 1);
        SET_BIT(data, 3);
        SET_BIT(data, 165);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_N(bs_get_bs_bit(&bs, true, &n));
        Z_ASSERT_BIT(bs_peek_bit(&bs), true);
        Z_CHECK_EXTRACTED(n, 0, 3, false);
        Z_CHECK_BOUNDS(bs, 3, 1024);

        Z_ASSERT_N(bs_get_bs_bit(&bs, true, &n));
        Z_CHECK_EXTRACTED(n, 3, 3, false);
        Z_CHECK_BOUNDS(bs, 3, 1024);

        Z_ASSERT_N(bs_get_bs_bit(&bs, false, &n));
        Z_ASSERT_BIT(bs_peek_bit(&bs), false);
        Z_CHECK_EXTRACTED(n, 3, 4, true);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_N(bs_get_bs_bit(&bs, false, &n));
        Z_CHECK_EXTRACTED(n, 4, 4, true);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_N(bs_get_bs_bit(&bs, true, &n));
        Z_ASSERT_BIT(bs_peek_bit(&bs), true);
        Z_CHECK_EXTRACTED(n, 4, 165, false);
        Z_CHECK_BOUNDS(bs, 165, 1024);

        Z_ASSERT_N(bs_get_bs_bit(&bs, false, &n));
        Z_ASSERT_BIT(bs_peek_bit(&bs), false);
        Z_CHECK_EXTRACTED(n, 165, 166, true);
        Z_CHECK_BOUNDS(bs, 166, 1024);

        Z_ASSERT_NEG(bs_get_bs_bit(&bs, true, &n));
        Z_CHECK_BOUNDS(bs, 166, 1024);
    } Z_TEST_END;

    Z_TEST(get_bs_bit_and_skip, "bit_stream: bs_get_bs_bit_and_skip") {
        bit_stream_t bs;
        bit_stream_t n;
        byte data[128];

        p_clear(&data, 1);
        SET_BIT(data, 3);
        SET_BIT(data, 165);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_N(bs_get_bs_bit_and_skip(&bs, true, &n));
        Z_CHECK_EXTRACTED(n, 0, 3, false);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_N(bs_get_bs_bit_and_skip(&bs, true, &n));
        Z_CHECK_EXTRACTED(n, 4, 165, false);
        Z_CHECK_BOUNDS(bs, 166, 1024);

        Z_ASSERT_NEG(bs_get_bs_bit_and_skip(&bs, true, &n));
        Z_CHECK_BOUNDS(bs, 166, 1024);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_N(bs_get_bs_bit_and_skip(&bs, false, &n));
        Z_CHECK_EXTRACTED(n, 0, 0, true);
        Z_CHECK_BOUNDS(bs, 1, 1024);

        Z_ASSERT_N(bs_get_bs_bit_and_skip(&bs, true, &n));
        Z_CHECK_EXTRACTED(n, 1, 3, false);
        Z_CHECK_BOUNDS(bs, 4, 1024);

        Z_ASSERT_N(bs_get_bs_bit_and_skip(&bs, false, &n));
        Z_CHECK_EXTRACTED(n, 4, 4, true);
        Z_CHECK_BOUNDS(bs, 5, 1024);

        Z_ASSERT_N(bs_get_bs_bit_and_skip(&bs, false, &n));
        Z_CHECK_EXTRACTED(n, 5, 5, true);
        Z_CHECK_BOUNDS(bs, 6, 1024);
    } Z_TEST_END;


    Z_TEST(shrink_downto_bit, "bit_stream: bs_shrink_downto_bit") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        SET_BIT(data, 3);
        SET_BIT(data, 165);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_shrink_downto_bit(&bs, true, -1), 858);
        Z_CHECK_BOUNDS(bs, 0, 166);

        Z_ASSERT_EQ(bs_shrink_downto_bit(&bs, true, -1), 0);
        Z_CHECK_BOUNDS(bs, 0, 166);

        Z_ASSERT_EQ(bs_shrink_downto_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 0, 165);

        Z_ASSERT_EQ(bs_shrink_downto_bit(&bs, false, -1), 0);
        Z_CHECK_BOUNDS(bs, 0, 165);

        Z_ASSERT_EQ(bs_shrink_downto_bit(&bs, true, -1), 161);
        Z_CHECK_BOUNDS(bs, 0, 4);

        Z_ASSERT_EQ(bs_shrink_downto_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 0, 3);

        Z_ASSERT_NEG(bs_shrink_downto_bit(&bs, true, -1));
        Z_CHECK_BOUNDS(bs, 0, 3);
    } Z_TEST_END;

    Z_TEST(shrink_before_bit, "bit_stream: bs_shrink_before_bit") {
        bit_stream_t bs;
        byte data[128];

        p_clear(&data, 1);
        SET_BIT(data, 3);
        SET_BIT(data, 165);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_shrink_before_bit(&bs, true, -1), 859);
        Z_CHECK_BOUNDS(bs, 0, 165);

        Z_ASSERT_EQ(bs_shrink_before_bit(&bs, true, -1), 162);
        Z_CHECK_BOUNDS(bs, 0, 3);

        Z_ASSERT_NEG(bs_shrink_before_bit(&bs, true, -1));
        Z_CHECK_BOUNDS(bs, 0, 3);

        bs = bs_init_ptr(data, &data[128]);
        Z_ASSERT_EQ(bs_shrink_before_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 0, 1023);

        Z_ASSERT_EQ(bs_shrink_before_bit(&bs, true, -1), 858);
        Z_CHECK_BOUNDS(bs, 0, 165);

        Z_ASSERT_EQ(bs_shrink_before_bit(&bs, false, -1), 1);;
        Z_CHECK_BOUNDS(bs, 0, 164);

        Z_ASSERT_EQ(bs_shrink_before_bit(&bs, false, -1), 1);
        Z_CHECK_BOUNDS(bs, 0, 163);
    } Z_TEST_END;

    /* }}} */
} Z_GROUP_END;

/* {{{ parseopt */

Z_GROUP_EXPORT(parseopt) {
    Z_TEST(parseopt_geti, "") {
        int i = 0;

        Z_ASSERT_N(parseopt_geti("42", "ARG", &i));
        Z_ASSERT_EQ(i, 42);
        Z_ASSERT_N(parseopt_geti("-4368", "ARG", &i));
        Z_ASSERT_EQ(i, -4368);

        Z_ASSERT_NEG(parseopt_geti("x", "ARG", &i));
        Z_ASSERT_NEG(parseopt_geti("12t", "ARG", &i));
    } Z_TEST_END;

    Z_TEST(parseopt_getu, "") {
        unsigned u = 0;

        Z_ASSERT_N(parseopt_getu("42", "ARG", &u));
        Z_ASSERT_EQ(u, 42u);
        Z_ASSERT_NEG(parseopt_getu("-4368", "ARG", &u));
        Z_ASSERT_NEG(parseopt_getu("x", "ARG", &u));
        Z_ASSERT_NEG(parseopt_getu("12t", "ARG", &u));
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */
/* {{{ core-macros.h */

typedef struct extra_str_tab_t {
    int len;
    const char *tab[];
} extra_str_tab_t;

typedef struct extra_lstr_tab_t {
    int len;
    lstr_t tab[];
} extra_lstr_tab_t;

Z_GROUP_EXPORT(core_macros) {
    /* {{{ OPT */

    Z_TEST(opt, "opt") {
        opt_u32_t src, dst;

        OPT_SET(src, 8008);
        OPT_COPY(dst, src);

        Z_ASSERT(OPT_ISSET(dst));
        Z_ASSERT_EQ(OPT_VAL(dst), 8008U);

        Z_ASSERT_OPT_EQ(src, dst);

        OPT_CLR(src);
        OPT_COPY(dst, src);

        Z_ASSERT(!OPT_ISSET(dst));

        Z_ASSERT_OPT_EQ(src, dst);

        OPT_CLR(src);
        OPT_SET(src, OPT_DEFVAL(src, 1U));
        Z_ASSERT_EQ(OPT_VAL(src), 1U);
    } Z_TEST_END;

    /* }}} */
    /* {{{ carray_loops */

    Z_TEST(carray_loops, "C array loop helpers") {
        int i = 0;
        lstr_t strs[] = {
            LSTR_IMMED("toto"),
            LSTR_IMMED("1234567890"),
            LSTR_IMMED("yop")
        };

        carray_for_each_pos(pos, strs) {
            Z_ASSERT_LT(pos, countof(strs));
            Z_ASSERT_EQ(pos, i++);
        }

        i = 0;
        carray_for_each_entry(s, strs) {
            Z_ASSERT_LSTREQUAL(s, strs[i++]);
        }

        i = 0;
        carray_for_each_ptr(s, strs) {
            Z_ASSERT(s == &strs[i++]);
        }

        i = 0;
        carray_for_each_ptr(s, strs) {
            Z_ASSERT(s == &strs[i++]);
            s = NULL;
        }
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_last */

    Z_TEST(tab_last, "tab_last") {
        int ints[] = { 1, 2, 3, 4 };
        struct {
            int *tab;
            int len;
        } tab = {
            .tab = ints,
            .len = countof(ints),
        };

        Z_ASSERT_EQ(*tab_last(&tab), 4);
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_for_each_pos */

    Z_TEST(tab_for_each_pos, "") {
        int ints[] = { 1, 2, 3, 4 };
        struct {
            int *tab;
            int len;
        } tab = {
            .tab = ints,
            .len = countof(ints),
        };
        int out[4];

        p_clear(out, countof(out));
        tab_for_each_pos(i, &tab) {
            out[i] = ints[i];
        }
        Z_ASSERT_EQ(out[0], ints[0]);
        Z_ASSERT_EQ(out[1], ints[1]);
        Z_ASSERT_EQ(out[2], ints[2]);
        Z_ASSERT_EQ(out[3], ints[3]);
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_for_each_ptr */

    Z_TEST(tab_for_each_ptr, "") {
        t_scope;
        const char *strs[] = { "toto", "abcdef", "42" };
        struct {
            const char **tab;
            int len;
        } tab = {
            .tab = strs,
            .len = countof(strs),
        };
        const char **out[3];
        const char ***w;
        extra_str_tab_t *extra_tab;

        p_clear(out, countof(out));
        w = out;
        tab_for_each_ptr(ptr, &tab) {
            *w++ = ptr;
        }
        Z_ASSERT(out[0] == &strs[0]);
        Z_ASSERT(out[1] == &strs[1]);
        Z_ASSERT(out[2] == &strs[2]);

        extra_tab = t_new_extra(extra_str_tab_t,
                                countof(strs) * sizeof(strs[0]));
        extra_tab->len = countof(strs);
        p_copy(extra_tab->tab, strs, countof(strs));

        p_clear(out, countof(out));
        w = out;
        tab_for_each_ptr(ptr, extra_tab) {
            *w++ = ptr;
        }
        Z_ASSERT(out[0] == &extra_tab->tab[0]);
        Z_ASSERT(out[1] == &extra_tab->tab[1]);
        Z_ASSERT(out[2] == &extra_tab->tab[2]);
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_for_each_entry */

    Z_TEST(tab_for_each_entry, "") {
        t_scope;
        lstr_t lstrs[] = { LSTR("string"), LSTR("int"), LSTR("double") };
        struct {
            lstr_t *tab;
            int len;
        } tab = {
            .tab = lstrs,
            .len = countof(lstrs),
        };
        lstr_t out[3];
        lstr_t *w = out;
        extra_lstr_tab_t *extra_tab;

        p_clear(out, countof(out));
        tab_for_each_entry(s, &tab) {
            *w++ = s;
        }
        Z_ASSERT_LSTREQUAL(out[0], lstrs[0]);
        Z_ASSERT_LSTREQUAL(out[1], lstrs[1]);
        Z_ASSERT_LSTREQUAL(out[2], lstrs[2]);

        p_clear(out, countof(out));

        extra_tab = t_new_extra(extra_lstr_tab_t,
                                countof(lstrs) * sizeof(lstrs[0]));
        extra_tab->len = countof(lstrs);
        p_copy(extra_tab->tab, lstrs, countof(lstrs));

        p_clear(out, countof(out));
        w = out;
        tab_for_each_entry(s, extra_tab) {
            *w++ = s;
        }
        Z_ASSERT_LSTREQUAL(out[0], lstrs[0]);
        Z_ASSERT_LSTREQUAL(out[1], lstrs[1]);
        Z_ASSERT_LSTREQUAL(out[2], lstrs[2]);
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_enumerate_ptr */

    Z_TEST(tab_enumerate_ptr, "") {
        const char *strs[] = { "toto", "abcdef", "42" };
        struct {
            const char **tab;
            int len;
        } tab = {
            .tab = strs,
            .len = countof(strs),
        };
        const char **out[3];

        p_clear(out, countof(out));
        tab_enumerate_ptr(pos, ptr, &tab) {
            out[pos] = ptr;
        }
        Z_ASSERT(out[0] == &strs[0]);
        Z_ASSERT(out[1] == &strs[1]);
        Z_ASSERT(out[2] == &strs[2]);
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_enumerate */

    Z_TEST(tab_enumerate, "") {
        lstr_t lstrs[] = { LSTR("string"), LSTR("int"), LSTR("double") };
        struct {
            lstr_t *tab;
            int len;
        } tab = {
            .tab = lstrs,
            .len = countof(lstrs),
        };
        lstr_t out[3];

        p_clear(out, countof(out));
        tab_enumerate(pos, s, &tab) {
            out[pos] = s;
        }
        Z_ASSERT_LSTREQUAL(out[0], lstrs[0]);
        Z_ASSERT_LSTREQUAL(out[1], lstrs[1]);
        Z_ASSERT_LSTREQUAL(out[2], lstrs[2]);
    } Z_TEST_END;

    /* }}} */
    /* {{{ tab_swap */

    Z_TEST(tab_swap, "tab_swap") {
        int ints[] = { 1, 2, 3, 4 };
        struct {
            int *tab;
            int len;
        } tab = {
            .tab = ints,
            .len = countof(ints),
        };

        tab_swap(&tab, 1, 2);
        Z_ASSERT_EQ(ints[0], 1);
        Z_ASSERT_EQ(ints[1], 3);
        Z_ASSERT_EQ(ints[2], 2);
        Z_ASSERT_EQ(ints[3], 4);
    } Z_TEST_END;

    /* }}} */
    /* {{{ unconst */

    Z_TEST(unconst_cast, "unconst_cast") {
        const int i = 5;
        int *p;

        p = unconst_cast(int, &i);
        Z_ASSERT(p == &i);
    } Z_TEST_END;

    /* }}} */
} Z_GROUP_END;

/* }}} */
/* {{{ core-errors.h */

static void print_int(int fd, data_t data)
{
    int i = *(const int *)data.ptr;

    dprintf(fd, "i = %d\n", i);
}

static int z_check_debug_file(const char *path, const char *func,
                              const char *file, int line, int i)
{
    SB_1k(fbuf);
    SB_1k(exp);

    Z_ASSERT_N(sb_read_file(&fbuf, path), "cannot read file `%s`", path);

    sb_addf(&exp, "\nAdditional user context:\n");
    sb_addf(&exp, "\n[0] in %s() from %s:%d\n", func, file, line);
    sb_addf(&exp, "i = %d\n", i);
    Z_ASSERT_STREQUAL(fbuf.data, exp.data);

    Z_HELPER_END;
}

Z_GROUP_EXPORT(core_errors) {
    Z_TEST(debug_stack, "") {
        t_scope;
        int i = 42;
        const char *path;
        int fd;

        int line = __LINE__ + 1;
        debug_stack_scope(DATA_PTR(&i), &print_int);

        path = t_fmt("%pL.debug", &z_tmpdir_g);

        /* Create the file. */
        fd = open(path, O_EXCL | O_CREAT | O_WRONLY, 0600);
        Z_ASSERT_N(fd, "cannot create .debug file `%s`", path);
        p_close(&fd);

        Z_ASSERT_N(_debug_stack_print(path));
        Z_HELPER_RUN(z_check_debug_file(path, __func__, __FILE__, line, i));

        /* Clean the file. */
        fd = open(path, O_TRUNC | O_CREAT | O_WRONLY, 0600);
        Z_ASSERT_N(fd, "cannot create .debug file `%s`", path);
        p_close(&fd);

        /* Change the value of "i" and check that we can see the new value if
         * we generate the .debug file again. */
        i = 51;
        Z_ASSERT_N(_debug_stack_print(path));
        Z_HELPER_RUN(z_check_debug_file(path, __func__, __FILE__, line, i));
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */

int main(int argc, char **argv)
{
    z_setup(argc, argv);
    z_register_exports(PLATFORM_PATH LIBCOMMON_PATH);
    return z_run();
}
