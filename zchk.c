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

#include "z.h"
#include "bit.h"

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
    bit_stream_t bs;
    bit_stream_t n;
    byte data[128];

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

/* {{{ core-macros.h */

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
    /* {{{ if_assign */

    Z_TEST(if_assign, "if_assign") {
        int i = 1;

        if_assign (a, &i) {
            Z_ASSERT_EQ(*a, 1);
        } else {
            Z_ASSERT(false);
        }

        /* resure a to ensure it is defined only in the scope of the
         * if_assign
         */
        if_assign (a, NULL) {
            Z_ASSERT(false);
        } else {
            Z_ASSERT(true);
        }

        /* Same with a if cascade */
        if_assign (a, NULL) {
            Z_ASSERT(false);
        } else
        if_assign (b, &i) {
            Z_ASSERT_EQ(*b, i);
        } else {
            Z_ASSERT(false);
        }
    } Z_TEST_END;

    /* }}} */
    /* {{{ while_assign */

    Z_TEST(while_assign, "while_assign") {
        int v[] = { 1, 2 };
        int *tab[] = { &v[0], &v[1], NULL };
        int pos = 0;
        int it = 0;

        /* pos++ in the value to ensure we don't evaluate the provided
         * expression more than necessary.
         */
        while_assign (a, tab[pos++]) {
            it++;
            Z_ASSERT_LT(it, 3);
            Z_ASSERT_EQ(pos, it);
            switch (pos) {
              case 1 ... 2:
                Z_ASSERT_EQ(*a, v[pos - 1]);
                break;

              default:
                Z_ASSERT(false);
                break;
            }
        }
        Z_ASSERT_EQ(pos, 3);
        Z_ASSERT_EQ(it, 2);
    } Z_TEST_END;

    /* }}} */
} Z_GROUP_END;

/* }}} */

int main(int argc, char **argv)
{
    z_setup(argc, argv);
    z_register_exports(PLATFORM_PATH LIBCOMMON_PATH);
    return z_run();
}
