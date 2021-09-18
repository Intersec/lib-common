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

#include "bit-buf.h"
#include "bit-stream.h"

bb_t *bb_init(bb_t *bb)
{
    p_clear(bb, 1);

    bb->alignment = 8;

#ifndef NDEBUG
    qv_init(&bb->marks);
#endif

    return bb;
}

void bb_wipe(bb_t *bb)
{
#ifndef NDEBUG
    qv_wipe(&bb->marks);
#endif

    mp_delete(bb->mp, &bb->data);
}

void bb_reset(bb_t *bb)
{
    mem_pool_t *mp = mp_ipool(bb->mp);
    const size_t alignment = bb->alignment;

    if (!(mp->mem_pool & MEM_BY_FRAME) && bb->size > (16 << 10)) {
        bb_wipe(bb);
        bb_init(bb);
        bb->alignment = alignment;
    } else {
        bzero(bb->data, DIV_ROUND_UP(bb->len, 8));
        bb->len = 0;
    }
}

void bb_init_sb(bb_t *bb, sb_t *sb)
{
    if (sb->data == __sb_slop) {
        bb_init(bb);
    } else {
        /* bb->size is a number of 64 bits words so the sb size must be bigger
         * than the sb length rounded up to 8 bytes */
        sb_grow(sb, ROUND_UP(sb->len, 8) - sb->len);
        bb_init_full(bb, sb->data, sb->len * 8, sb->size / 8, 8, sb->mp);

        /* We took ownership of the memory so ensure clear the sb */
        sb_init(sb);
    }
}


void bb_transfer_to_sb(bb_t *bb, sb_t *sb)
{
    sb_wipe(sb);
    bb_grow(bb, 8);
    sb_init_full(sb, bb->data, DIV_ROUND_UP(bb->len, 8), bb->size * 8,
                 bb->mp);
    bb->data = NULL;
    bb_wipe(bb);
    bb_init(bb);
}

void __bb_grow(bb_t *bb, size_t extra)
{
    size_t newlen = DIV_ROUND_UP(bb->len + extra, 64);
    size_t newsz;

    newsz = p_alloc_nr(bb->size);
    if (newsz < newlen) {
        newsz = newlen;
    }
    assert (bb->alignment && bb->alignment % 8 == 0);
    newsz = ROUND_UP(newsz, bb->alignment / 8);

    bb->data = mp_irealloc_fallback(&bb->mp, bb->data, bb->size * 8,
                                    newsz * 8, bb->alignment, 0);
    bb->size = newsz;
}

void bb_add_bs(bb_t *bb, const bit_stream_t *b)
{
    bit_stream_t bs = *b;
    pstream_t ps;

    while (!bs_done(&bs) && !bs_is_aligned(&bs)) {
        bb_add_bit(bb, __bs_get_bit(&bs));
    }

    ps = __bs_get_bytes(&bs, bs_len(&bs) / 8);
    bb_add_bytes(bb, ps.b, ps_len(&ps));

    while (!bs_done(&bs)) {
        bb_add_bit(bb, __bs_get_bit(&bs));
    }
}

void bb_be_add_bs(bb_t *bb, const bit_stream_t *b)
{
    bit_stream_t bs = *b;
    pstream_t ps;

    while (!bs_done(&bs) && !bs_is_aligned(&bs)) {
        bb_be_add_bit(bb, __bs_be_get_bit(&bs));
    }

    ps = __bs_get_bytes(&bs, bs_len(&bs) / 8);
    bb_be_add_bytes(bb, ps.b, ps_len(&ps));

    while (!bs_done(&bs)) {
        bb_be_add_bit(bb, __bs_be_get_bit(&bs));
    }
}

void bb_shift_left(bb_t *bb, size_t shift)
{
    if (shift >= bb->len) {
        bb_reset(bb);
    } else
    if (shift % 8 == 0) {
        /* Shift is nicely aligned */
        const size_t bshift = shift / 8;
        const size_t blen = DIV_ROUND_UP(bb->len, 8);

        p_move(bb->bytes, (bb->bytes + bshift), blen - bshift);
        p_clear(bb->bytes + blen - bshift, bshift);

        bb->len -= shift;
    } else {
        const size_t nwords = shift / 64, wshift = shift % 64;
        const size_t rwshift = 64 - wshift;
        size_t src = nwords, dst = 0;

        /* We have to shift the bit-buffer word per word… Here we are allowed
         * to overflow a little because we know that we are always aligned on
         * 8 bytes. */
        while (src < bb->word) {
            /* Copy the bits from src which have to be shifted in dst */
            bb->data[dst++] = ((bb->data[src] >> wshift)
                               | (bb->data[src + 1] << rwshift));
            src++;
        }

        /* Handle last word */
        bb->data[dst] = (bb->data[src] >> wshift);

        /* Now we have to cleanup the old bits */
        if (dst < bb->word) {
            p_clear(&bb->data[dst + 1], bb->word - dst);
        }

        bb->len -= shift;
    }
}

char *t_print_bits(uint8_t bits, uint8_t bstart, uint8_t blen)
{
    char *str = t_new(char, blen + 1);
    char *w   = str;

    for (int i = bstart; i < blen; i++) {
        *w++ = (bits & (1 << i)) ? '1' : '0';
    }

    *w = '\0';

    return str;
}

char *t_print_be_bb(const bb_t *bb, size_t *len)
{
    bit_stream_t bs = bs_init_bb(bb);

    return t_print_be_bs(bs, len);
}

int z_set_be_bb(bb_t *bb, const char *bits, sb_t *err)
{
    int c;
    uint8_t u = 0;
    int blen = 0;
    const char *r = bits;

    bb_reset(bb);
    for (;;) {
        c = *r++;

        if (c == '0' || c == '1') {
            u <<= 1;
            blen++;

            if (blen > 8) {
                sb_sets(err, "invalid input");
                return -1;
            }

            if (c == '1') {
                u |= 1;
            }
        } else
        if (c == '.' || !c) {
            bb_add_bits(bb, u, blen);

            if (!c) {
                break;
            }

            u = 0;
            blen = 0;
        } else {
            sb_setf(err, "unexpected character '%c'", c);
            return -1;
        }
    }

    {
        t_scope;
        const char *s = t_print_be_bb(bb, NULL);

        if (!strequal(s, bits)) {
            sb_setf(err, "input different when re-generated: got `%s`", s);
            return -1;
        }
    }

    return 0;
}

char *t_print_bb(const bb_t *bb, size_t *len)
{
    bit_stream_t bs = bs_init_bb(bb);

    return t_print_bs(bs, len);
}

/* Tests {{{ */

#include "z.h"

#define T_TEST_BB(bb, bits) \
    do {                                                                    \
        bit_stream_t _bs = bs_init_bb(bb);                                  \
                                                                            \
        Z_ASSERT_STREQUAL(bits, t_print_bs(_bs, NULL));                     \
    } while (0)

#define T_TEST_PREV_BB(bb, oldlen, bits) \
    do {                                                                    \
        bb_t _tmp_bb = *(bb);                                               \
                                                                            \
        _tmp_bb.len = (oldlen);                                             \
        assert (_tmp_bb.len <= _tmp_bb.size * 64);                          \
        T_TEST_BB(&_tmp_bb, bits);                                          \
    } while (0)

Z_GROUP_EXPORT(bit_buf)
{
    Z_TEST(le_full, "bit-buf/bit-stream: full check") {
        t_scope;
        BB_1k(bb);
        bit_stream_t bs;

        bb_add_bit(&bb, true);
        bb_add_bit(&bb, false);
        bb_add_bit(&bb, true);
        bb_add_bit(&bb, true);
        bb_add_bit(&bb, false);
        bb_add_bit(&bb, false);
        bb_add_bit(&bb, false);
        bb_add_bit(&bb, true);
        bb_add_bit(&bb, false);
        bb_add_bit(&bb, true);
        Z_ASSERT_EQ(bb.len, 10U);

        bs = bs_init_bb(&bb);
        Z_ASSERT_EQ(bs_len(&bs), 10U, "Check length #1");

        bb_add_bits(&bb, 0x1a, 7); /* 0011010 */
        Z_ASSERT_STREQUAL("0101100", t_print_bits(0x1a, 0, 7));

        Z_ASSERT_EQ(bb.len, 17U);
        bs = bs_init_bb(&bb);
        Z_ASSERT_STREQUAL(".10110001.01010110.0", t_print_bs(bs, NULL));

        Z_ASSERT_EQ(bs_len(&bs), 17U, "Check length #2");
        Z_ASSERT_EQ(__bs_get_bit(&bs), true,  "Check bit #1");
        Z_ASSERT_EQ(__bs_get_bit(&bs), false, "Check bit #2");
        Z_ASSERT_EQ(__bs_get_bit(&bs), true,  "Check bit #3");
        Z_ASSERT_EQ(__bs_get_bit(&bs), true,  "Check bit #4");
        Z_ASSERT_EQ(__bs_get_bit(&bs), false, "Check bit #5");
        Z_ASSERT_EQ(__bs_get_bit(&bs), false, "Check bit #6");
        Z_ASSERT_EQ(__bs_get_bit(&bs), false, "Check bit #7");
        Z_ASSERT_EQ(__bs_get_bit(&bs), true,  "Check bit #8");
        Z_ASSERT_EQ(__bs_get_bit(&bs), false, "Check bit #9");
        Z_ASSERT_EQ(__bs_get_bit(&bs), true,  "Check bit #10");

        /* Reverse check */
        bs = bs_init_bb(&bb);
        Z_ASSERT_N(bs_shrink(&bs, 7), "Shrink #1");
        Z_ASSERT_EQ(bs_len(&bs), 10U, "Check length #2");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), true,  "Check bit #10");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), true,  "Check bit #10");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), false, "Check bit #9");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), false, "Check bit #9");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), true,  "Check bit #8");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), true,  "Check bit #8");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), false, "Check bit #7");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), false, "Check bit #7");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), false, "Check bit #6");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), false, "Check bit #6");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), false, "Check bit #5");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), false, "Check bit #5");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), true,  "Check bit #4");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), true,  "Check bit #4");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), true,  "Check bit #3");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), true,  "Check bit #3");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), false, "Check bit #2");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), false, "Check bit #2");
        Z_ASSERT_EQ(__bs_peek_last_bit(&bs), true,  "Check bit #1");
        Z_ASSERT_EQ(__bs_get_last_bit(&bs), true,  "Check bit #1");

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(le_add_0_1, "") {
        BB_1k(bb);

        for (int i = 1; i < 256; i++) {
            for (int j = 0; j < i; j++) {
                bit_stream_t bs;

                bb_reset(&bb);
                Z_ASSERT_EQ(bb.len, (size_t)0);

                bb_add0s(&bb, j);
                Z_ASSERT_EQ(bb.len, (size_t)j, "bad size 0s %d-%d", i, j);
                bb_add1s(&bb, i - j);
                Z_ASSERT_EQ(bb.len, (size_t)i, "bad size 1s %d-%d", i, j);

                bs = bs_init_bb(&bb);
                for (int k = 0; k < i; k++) {
                    int bit = bs_get_bit(&bs);

                    Z_ASSERT_N(bit, "buffer too short %d-%d-%d", i, j, k);
                    if (k < j) {
                        Z_ASSERT(!bit, "bad bit %d-%d-%d", i, j, k);
                    } else {
                        Z_ASSERT(bit, "bad bit %d-%d-%d", i, j, k);
                    }
                }
                Z_ASSERT_NEG(bs_get_bit(&bs));
            }
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(le_add_bytes, "") {
        BB_1k(bb);
        byte b[32];

        for (int i = 0; i < countof(b); i++) {
            b[i] = i;
        }

        for (int i = 0; i < countof(b) / 2; i++) {
            for (int j = 0; j < countof(b) / 2; j++) {
                for (int k = 0; k < 64; k++) {
                    bit_stream_t bs;
                    bit_stream_t bs2;

                    bb_reset(&bb);

                    bb_add1s(&bb, k);
                    bb_add_bytes(&bb, b + j, i);

                    bs = bs_init_bb(&bb);
                    for (int l = 0; l < k; l++) {
                        int bit = bs_get_bit(&bs);

                        Z_ASSERT_N(bit, "buffer too short %d-%d-%d", i, k, l);
                        Z_ASSERT(bit, "bad bit %d-%d-%d", i, k, l);
                    }

                    bs2 = bs_init(b + j, 0, i * 8);
                    while (!bs_done(&bs2)) {
                        Z_ASSERT_EQ(bs_get_bit(&bs), bs_get_bit(&bs2));
                    }
                    Z_ASSERT(bs_done(&bs));
                }
            }
        }

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(be_full, "bit-buf/bit-stream: full check") {
        t_scope;
        BB_1k(bb);
        bit_stream_t bs;

        bb_be_add_bit(&bb, true);
        bb_be_add_bit(&bb, false);
        bb_be_add_bit(&bb, true);
        bb_be_add_bit(&bb, true);
        bb_be_add_bit(&bb, false);
        bb_be_add_bit(&bb, false);
        bb_be_add_bit(&bb, false);
        bb_be_add_bit(&bb, true);
        bb_be_add_bit(&bb, false);
        bb_be_add_bit(&bb, true);
        Z_ASSERT_EQ(bb.len, 10U);

        bs = bs_init_bb(&bb);
        Z_ASSERT_EQ(bs_len(&bs), 10U, "Check length #1");

        bb_be_add_bits(&bb, 0x1a, 7); /* 0011010 */
        Z_ASSERT_STREQUAL("0101100", t_print_bits(0x1a, 0, 7));

        Z_ASSERT_EQ(bb.len, 17U);
        bs = bs_init_bb(&bb);
        Z_ASSERT_STREQUAL(".10110001.01001101.0", t_print_be_bs(bs, NULL));

        Z_ASSERT_EQ(bs_len(&bs), 17U, "Check length #2");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), true,  "Check bit #1");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), false, "Check bit #2");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), true,  "Check bit #3");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), true,  "Check bit #4");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), false, "Check bit #5");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), false, "Check bit #6");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), false, "Check bit #7");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), true,  "Check bit #8");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), false, "Check bit #9");
        Z_ASSERT_EQ(__bs_be_get_bit(&bs), true,  "Check bit #10");

        /* Reverse check */
        bs = bs_init_bb(&bb);
        Z_ASSERT_N(bs_shrink(&bs, 7), "Shrink #1");
        Z_ASSERT_EQ(bs_len(&bs), 10U, "Check length #2");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), true,  "Check bit #10");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), true,  "Check bit #10");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), false, "Check bit #9");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), false, "Check bit #9");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), true,  "Check bit #8");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), true,  "Check bit #8");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), false, "Check bit #7");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), false, "Check bit #7");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), false, "Check bit #6");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), false, "Check bit #6");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), false, "Check bit #5");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), false, "Check bit #5");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), true,  "Check bit #4");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), true,  "Check bit #4");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), true,  "Check bit #3");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), true,  "Check bit #3");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), false, "Check bit #2");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), false, "Check bit #2");
        Z_ASSERT_EQ(__bs_be_peek_last_bit(&bs), true,  "Check bit #1");
        Z_ASSERT_EQ(__bs_be_get_last_bit(&bs), true,  "Check bit #1");

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(le_bug, "bit-buf: add 64nth bit") {
        t_scope;
        t_BB_1k(bb);

        bb_add0s(&bb, 63);
        bb_add_bit(&bb, true);
        bb_add0s(&bb, 8);
        Z_ASSERT_STREQUAL(".00000000.00000000.00000000.00000000.00000000.00000000.00000000.00000001.00000000", t_print_bb(&bb, NULL));
    } Z_TEST_END;

    Z_TEST(align, "bit-buf: alignment on 512 bytes") {
        bb_t bb;

        bb_init(&bb);
        bb.alignment = 512;

        bb_add0s(&bb, 42);
        Z_ASSERT((bb.size * 8) % 512 == 0);
        Z_ASSERT(((intptr_t)bb.bytes) % 512 == 0);

        bb_add0s(&bb, 8 * 520);
        Z_ASSERT((bb.size * 8) % 512 == 0);
        Z_ASSERT(((intptr_t)bb.bytes) % 512 == 0);

        bb_wipe(&bb);
    } Z_TEST_END;

    Z_TEST(sb, "bit-buf: init/transfer sb") {
        t_scope;
        sb_t sb;
        t_SB(sb2, 42);
        bb_t bb;

        sb_init(&sb);
        bb_init_sb(&bb, &sb);

        bb_add_bits(&bb, 0x2aa, 10); /* 1010101010 */
        Z_ASSERT_EQ(bb.len, 10U);
        Z_ASSERT_EQ(sb.len, 0);

        bb_transfer_to_sb(&bb, &sb);
        Z_ASSERT_EQ(bb.len, 0U);
        Z_ASSERT_EQ(sb.len, 2);
        Z_ASSERT_EQ(sb.data[0], 0xaa);
        Z_ASSERT_EQ(sb.data[1], 0x2);

        bb_init_sb(&bb, &sb2);

        bb_add_bits(&bb, 0x2aa, 10); /* 1010101010 */
        Z_ASSERT_EQ(bb.len, 10U);
        Z_ASSERT_EQ(sb2.len, 0);

        bb_wipe(&bb);
        sb_wipe(&sb);
    } Z_TEST_END;

    Z_TEST(left_shit, "bit-buf: left shift") {
        t_scope;
        t_BB_1k(bb);

        bb_add_bits(&bb, 0x2aa, 10); /* 1010101010 */
        Z_ASSERT_EQ(bb.len, 10U);
        T_TEST_BB(&bb, ".01010101.01");

        bb_shift_left(&bb, 32);
        Z_ASSERT_EQ(bb.len, 0U);
        T_TEST_PREV_BB(&bb, 10, ".00000000.00");

        bb_add_bits(&bb, 0x2aa, 10); /* 1010101010 */
        bb_shift_left(&bb, 8);
        Z_ASSERT_EQ(bb.len, 2U);
        T_TEST_BB(&bb, ".01");
        T_TEST_PREV_BB(&bb, 10, ".01000000.00");

        bb_reset(&bb);
        bb_add_bits(&bb, 0x2aa, 10); /* 1010101010 */
        bb_shift_left(&bb, 3);
        Z_ASSERT_EQ(bb.len, 7U);
        T_TEST_BB(&bb, ".1010101");
        T_TEST_PREV_BB(&bb, 10, ".10101010.00");

        /* Tests with a big buffer now */
        bb_reset(&bb);
        for (int i = 0; i < 100; i++) {
            /* add a lot of 100 */
            bb_add_bit(&bb, true);
            bb_add_bit(&bb, false);
            bb_add_bit(&bb, false);
        }
        T_TEST_BB(&bb, ".10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.0100");

        bb_shift_left(&bb, 16);
        T_TEST_BB(&bb, ".00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.0100");
        T_TEST_PREV_BB(&bb, 300, ".00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01000000.00000000.0000");

        bb_shift_left(&bb, 79);
        T_TEST_BB(&bb, ".01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100");
        T_TEST_PREV_BB(&bb, 300, ".01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100100.10010010.01001001.00100000.00000000.00000000.00000000.00000000.00000000.00000000.00000000.00000000.00000000.00000000.00000000.0000");
    } Z_TEST_END;
} Z_GROUP_END;

/* }}} */
