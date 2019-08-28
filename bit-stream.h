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

#ifndef IS_LIB_COMMON_BIT_STREAM_H
#define IS_LIB_COMMON_BIT_STREAM_H

#include "core.h"
#include "bit-buf.h"

/*
 * bit_stream_t's are basically the two bit-wise bounds in a memory chunk.
 *
 * They are very similar to pstreams.
 */

/*
 * In order to avoid useless arithmetics, bitstreams use chunks of 64bits and
 * always keep aligned pointers. Properly used this will never trigger crashes
 * since the memory atom for the kernel is a page aligned on page size that is
 * itself a multiple of 64bits.
 */

struct bit_ptroff {
    union {
        const byte     *b;
        const uint64_t *p;
    };
    size_t offset;
};

typedef struct bit_stream_t {
    struct bit_ptroff s;
    struct bit_ptroff e;
} bit_stream_t;

/* Helpers {{{ */

static inline void bit_ptroff_add(struct bit_ptroff *s, size_t offset)
{
    s->offset += offset;
    if (s->offset >= 64) {
        s->p      += s->offset / 64;
        s->offset %= 64;
    }
}

static inline void bit_ptroff_sub(struct bit_ptroff *s, size_t offset)
{
    ssize_t off = s->offset;

    off -= offset;
    if (off < 0) {
        s->p -= DIV_ROUND_UP(-off, 64);
        if (off % 64) {
            s->offset = 64 + (off % 64);
        } else {
            s->offset = 0;
        }
    } else {
        s->offset = off;
    }
}

static inline void bit_ptroff_normalize(struct bit_ptroff *s)
{
    const byte *p = (const byte *)s->p;
    const byte *a = (const byte *)(((uintptr_t)p) & ~7ul);

    if (a != p) {
        s->offset += (p - a) * 8;
    }
    s->p = (const uint64_t *)a;

    bit_ptroff_add(s, 0);
}

#define BIT_PTROFF_INIT(Ptr, Offset)    { { (const byte *)(Ptr) }, (Offset) }
#define BIT_PTROFF_NORMALIZED(Ptr, Offset) ({                                \
        struct bit_ptroff __poff = BIT_PTROFF_INIT(Ptr, Offset);             \
        bit_ptroff_normalize(&__poff);                                       \
        __poff;                                                              \
    })

#define BIT_PTROFF_CMP(P1, P2)  \
    (CMP((P1)->p, (P2)->p) ?: CMP((P1)->offset, (P2)->offset))
#define BIT_PTROFF_LEN(P1, P2)  \
    (((P2)->p - (P1)->p) * 64 - (P1)->offset + (P2)->offset)

/* }}} */
/* Init {{{ */

static inline bit_stream_t bs_init_ptroff(const void *s, size_t s_offset,
                                          const void *e, size_t e_offset)
{
    bit_stream_t bs = {
        BIT_PTROFF_NORMALIZED(s, s_offset),
        BIT_PTROFF_NORMALIZED(e, e_offset),
    };
    return bs;
}

static ALWAYS_INLINE
bit_stream_t bs_init_ptr(const void *s, const void *e)
{
    return bs_init_ptroff(s, 0, e, 0);
}

static ALWAYS_INLINE
bit_stream_t bs_init(const void *data, size_t bstart, size_t blen)
{
    return bs_init_ptroff(data, bstart, data, bstart + blen);
}


static ALWAYS_INLINE
bit_stream_t bs_init_ps(const pstream_t *ps, size_t pad)
{
    return bs_init_ptroff(ps->p, 0, ps->p, ps_len(ps) * 8 - pad);
}

static inline bit_stream_t bs_init_bb(const bb_t *bb)
{
    return bs_init(bb->data, 0, bb->len);
}

/* }}} */
/* Checking constraints {{{ */

#define BS_WANT(c)   PS_WANT(c)
#define BS_CHECK(c)  PS_CHECK(c)

static inline size_t bs_len(const bit_stream_t *bs)
{
    return BIT_PTROFF_LEN(&bs->s, &bs->e);
}

static inline bool bs_has(const bit_stream_t *bs, size_t blen)
{
    return blen <= bs_len(bs);
}

static inline bool bs_has_bytes(const bit_stream_t *bs, size_t olen)
{
    return olen * 8 <= bs_len(bs);
}

static inline bool bs_done(const bit_stream_t *bs)
{
    return BIT_PTROFF_CMP(&bs->s, &bs->e) >= 0;
}

static inline bool __bs_contains(const bit_stream_t *bs,
                                 const struct bit_ptroff *p)
{
    return BIT_PTROFF_CMP(&bs->s, p) <= 0
        && BIT_PTROFF_CMP(p, &bs->e) <= 0;
}

static inline bool bs_contains(const bit_stream_t *bs, const void *p,
                               size_t off)
{
    struct bit_ptroff poff = BIT_PTROFF_NORMALIZED(p, off);

    return __bs_contains(bs, &poff);
}

static inline bool bs_is_aligned(const bit_stream_t *bs)
{
    return (bs->s.offset & 7) == 0;
}

/* }}} */
/* Bulk Skipping {{{ */

static inline ssize_t __bs_skip(bit_stream_t *bs, size_t blen)
{
    bit_ptroff_add(&bs->s, blen);
    return blen;
}

static inline ssize_t bs_skip(bit_stream_t *bs, size_t blen)
{
    return unlikely(!bs_has(bs, blen)) ? -1 : __bs_skip(bs, blen);
}

static inline ssize_t bs_align(bit_stream_t *bs)
{
    if (bs->s.offset & 7) {
        return bs_skip(bs, 8 - (bs->s.offset & 7));
    }
    return 0;
}

static inline ssize_t __bs_skip_upto(bit_stream_t *bs, const struct bit_ptroff *p)
{
    size_t skipped = BIT_PTROFF_LEN(&bs->s, p);

    bs->s = *p;
    return skipped;
}

static inline ssize_t bs_skip_upto(bit_stream_t *bs, const void *p, size_t off)
{
    struct bit_ptroff poff = BIT_PTROFF_NORMALIZED(p, off);

    PS_WANT(__bs_contains(bs, &poff));
    return __bs_skip_upto(bs, &poff);
}

static inline ssize_t __bs_shrink(bit_stream_t *bs, size_t len)
{
    bit_ptroff_sub(&bs->e, len);
    return len;
}

static inline ssize_t bs_shrink(bit_stream_t *bs, size_t len)
{
    return unlikely(!bs_has(bs, len)) ? -1 : __bs_shrink(bs, len);
}


static inline ssize_t __bs_clip(bit_stream_t *bs, size_t blen)
{
    ssize_t skipped = bs_len(bs) - blen;

    bs->e = bs->s;
    bs->e.offset += blen;
    bit_ptroff_normalize(&bs->e);
    return skipped;
}

static inline ssize_t bs_clip(bit_stream_t *bs, size_t blen)
{
    return unlikely(!bs_has(bs, blen)) ? -1 : __bs_clip(bs, blen);
}


static inline ssize_t __bs_clip_at(bit_stream_t *bs, const struct bit_ptroff *p)
{
    ssize_t skipped = BIT_PTROFF_LEN(p, &bs->e);

    bs->e = *p;
    return skipped;
}

static inline ssize_t bs_clip_at(bit_stream_t *bs, const void *p, size_t off)
{
    struct bit_ptroff poff = BIT_PTROFF_NORMALIZED(p, off);

    return unlikely(!__bs_contains(bs, &poff)) ? -1 : __bs_clip_at(bs, &poff);
}

/* }}} */
/* Bulk extraction {{{ */

static inline bit_stream_t __bs_extract_after(const bit_stream_t *bs,
                                              const struct bit_ptroff *p)
{
    return (bit_stream_t){ *p, bs->e };
}

static inline
int bs_extract_after(const bit_stream_t *bs, const void *p, size_t off,
                     bit_stream_t *out)
{
    struct bit_ptroff poff = BIT_PTROFF_NORMALIZED(p, off);

    BS_WANT(__bs_contains(bs, &poff));
    *out = __bs_extract_after(bs, &poff);
    return 0;
}

static inline
bit_stream_t __bs_get_bs_upto(bit_stream_t *bs, const struct bit_ptroff *p)
{
    bit_stream_t n = { bs->s, *p };

    bs->s = *p;
    return n;
}

static inline int bs_get_bs_upto(bit_stream_t *bs, const void *p, size_t off,
                                 bit_stream_t *out)
{
    struct bit_ptroff poff = BIT_PTROFF_NORMALIZED(p, off);

    BS_WANT(__bs_contains(bs, &poff));
    *out = __bs_get_bs_upto(bs, &poff);
    return 0;
}

static inline bit_stream_t __bs_get_bs(bit_stream_t *bs, size_t blen)
{
    struct bit_ptroff poff = BIT_PTROFF_NORMALIZED(bs->s.p,
                                                   bs->s.offset + blen);
    bit_stream_t sub = { bs->s, poff };

    bs->s = poff;
    return sub;
}

static inline int bs_get_bs(bit_stream_t *bs, size_t len, bit_stream_t *out)
{
    BS_WANT(bs_has(bs, len));
    *out = __bs_get_bs(bs, len);
    return 0;
}

static inline pstream_t __bs_get_bytes(bit_stream_t *bs, size_t len)
{
    pstream_t ps;

    ps = ps_init(((const byte *)bs->s.p) + bs->s.offset / 8, len);
    __bs_skip(bs, len * 8);
    return ps;
}

static inline int bs_get_bytes(bit_stream_t *bs, size_t len, pstream_t *ps)
{
    BS_WANT(bs_is_aligned(bs));
    BS_WANT(bs_has(bs, len * 8));
    *ps = __bs_get_bytes(bs, len);
    return 0;
}

/* }}} */
/* Read bit, little endian {{{ */

static inline int __bs_peek_bit(const bit_stream_t *bs)
{
    return (*bs->s.p >> bs->s.offset) & 1;
}

static inline int bs_peek_bit(const bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_peek_bit(bs);
}

static inline int __bs_get_bit(bit_stream_t *bs)
{
    bool bit = __bs_peek_bit(bs);
    __bs_skip(bs, 1);
    return bit;
}

static inline int bs_get_bit(bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_get_bit(bs);
}

static inline uint64_t __bs_peek_bits(const bit_stream_t *bs, size_t blen)
{
    uint64_t res;

    assert (blen <= 64);
    if (unlikely(!blen))
        return 0;

    assert (bs_has(bs, blen));

    if (bs->e.p == bs->s.p) {
        mem_tool_allow_memory(bs->s.p, 8, true);
    }
    res = *bs->s.p >> bs->s.offset;
    if (bs->s.offset + blen > 64) {
        if (bs->e.p == &bs->s.p[1]) {
            mem_tool_allow_memory(&bs->s.p[1], 8, true);
        }
        res |= bs->s.p[1] << (64 - bs->s.offset);
    }
    if (blen != 64) {
        res &= BITMASK_LT(uint64_t, blen);
    }
    return res;
}

static inline uint64_t __bs_get_bits(bit_stream_t *bs, size_t blen)
{
    uint64_t res = __bs_peek_bits(bs, blen);
    __bs_skip(bs, blen);
    return res;
}

static inline int bs_get_bits(bit_stream_t *bs, size_t blen, uint64_t *out)
{
    BS_WANT(blen <= 64);
    BS_WANT(bs_has(bs, blen));
    *out = __bs_get_bits(bs, blen);
    return blen;
}

static inline int __bs_peek_last_bit(const bit_stream_t *bs)
{
    if (bs->e.offset) {
        return (*bs->e.p >> (bs->e.offset - 1)) & 1;
    } else {
        return (*(bs->e.p - 1) >> 63) & 1;
    }
}

static inline int bs_peek_last_bit(const bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_peek_last_bit(bs);
}

static inline int __bs_get_last_bit(bit_stream_t *bs)
{
    __bs_shrink(bs, 1);
    return (*bs->e.p >> bs->e.offset) & 1;
}

static inline int bs_get_last_bit(bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_get_last_bit(bs);
}

static inline int
bs_get_last_bits(bit_stream_t *bs, size_t blen, uint64_t *out)
{
    bit_stream_t tmp = *bs;
    const size_t orig_len = bs_len(bs);

    BS_CHECK(bs_shrink(bs, blen));
    __bs_skip(&tmp, orig_len - blen);
    BS_CHECK(bs_get_bits(&tmp, blen, out));

    return blen;
}

/* }}} */
/* Read bit, big endian {{{ */

static inline int __bs_be_peek_bit(const bit_stream_t *bs)
{
    int offset = (bs->s.offset & ~7ul) + 7 - (bs->s.offset % 8);

    return (bs->s.b[offset / 8] >> (offset % 8)) & 1;
}

static inline int bs_be_peek_bit(const bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_be_peek_bit(bs);
}

static inline int __bs_be_get_bit(bit_stream_t *bs)
{
    bool bit = __bs_be_peek_bit(bs);
    __bs_skip(bs, 1);
    return bit;
}

static inline int bs_be_get_bit(bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_be_get_bit(bs);
}

static inline uint64_t __bs_be_peek_bits(const bit_stream_t *bs, size_t blen)
{
    const byte *b = &((const byte *)bs->s.p)[bs->s.offset / 8];
    size_t offset = bs->s.offset % 8;
    size_t remain = blen;
    uint64_t res = 0;

    if (offset + blen <= 8) {
        res = (*b >> (8 - (offset + blen))) & BITMASK_LT(uint64_t, blen);
        return res;
    }
    if (offset) {
        remain -= 8 - offset;
        res |= ((uint64_t)*b << remain);
        if (blen != 64) {
            res &= BITMASK_LT(uint64_t, blen);
        }
        b++;
    }
    while (remain >= 8) {
        remain -= 8;
        res |= ((uint64_t)*b << remain);
        b++;
    }
    if (remain) {
        res |= (*b >> (8 - remain)) & BITMASK_LT(uint64_t, remain);
    }
    return res;
}

static inline uint64_t __bs_be_get_bits(bit_stream_t *bs, size_t blen)
{
    uint64_t res = __bs_be_peek_bits(bs, blen);
    __bs_skip(bs, blen);
    return res;
}

static inline int bs_be_get_bits(bit_stream_t *bs, size_t blen, uint64_t *out)
{
    BS_WANT(blen <= 64);
    BS_WANT(bs_has(bs, blen));
    *out = __bs_be_get_bits(bs, blen);
    return blen;
}

static inline int __bs_be_peek_last_bit(const bit_stream_t *bs)
{
    if (bs->e.offset) {
        int offset = ((bs->e.offset - 1) & ~7ul) + 7
            - ((bs->e.offset - 1) % 8);

        return (*bs->e.p >> offset) & 1;
    } else {
        return (*(bs->e.p - 1) >> 56) & 1;
    }
}

static inline int bs_be_peek_last_bit(const bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_be_peek_last_bit(bs);
}

static inline int __bs_be_get_last_bit(bit_stream_t *bs)
{
    int offset;

    __bs_shrink(bs, 1);
    offset = (bs->e.offset & ~7ul) + 7 - (bs->e.offset % 8);
    return (*bs->e.p >> offset) & 1;
}

static inline int bs_be_get_last_bit(bit_stream_t *bs)
{
    return unlikely(bs_done(bs)) ? -1 : __bs_be_get_last_bit(bs);
}

static inline int
bs_be_get_last_bits(bit_stream_t *bs, size_t blen, uint64_t *out)
{
    bit_stream_t tmp = *bs;
    const size_t orig_len = bs_len(bs);

    BS_CHECK(bs_shrink(bs, blen));
    __bs_skip(&tmp, orig_len - blen);
    BS_CHECK(bs_be_get_bits(&tmp, blen, out));

    return blen;
}

/* }}} */
/* Scans {{{ */

static inline int __bs_scan_forward(const bit_stream_t *bs, bool b,
                                    struct bit_ptroff *poff,
                                    ssize_t max_len)
{
    size_t pos;

    max_len = (max_len < 0) ? bs_len(bs) : MIN(bs_len(bs), (size_t)max_len);
    pos = RETHROW(bsf(bs->s.p, bs->s.offset, max_len, !b));

    *poff = BIT_PTROFF_NORMALIZED(bs->s.p, bs->s.offset + pos);
    return 0;
}

static inline ssize_t
bs_skip_upto_bit(bit_stream_t *bs, bool b, ssize_t max_len)
{
    struct bit_ptroff poff = { { NULL }, 0 };

    BS_CHECK(__bs_scan_forward(bs, b, &poff, max_len));
    return __bs_skip_upto(bs, &poff);
}

static inline ssize_t
bs_skip_after_bit(bit_stream_t *bs, bool b, ssize_t max_len)
{
    return BS_CHECK(bs_skip_upto_bit(bs, b, max_len)) + __bs_skip(bs, 1);
}

static inline int bs_get_bs_bit(bit_stream_t *bs, bool b, bit_stream_t *out)
{
    struct bit_ptroff poff = { { NULL }, 0 };

    BS_CHECK(__bs_scan_forward(bs, b, &poff, -1));
    *out = __bs_get_bs_upto(bs, &poff);
    return 0;
}

static inline int bs_get_bs_bit_and_skip(bit_stream_t *bs, bool b,
                                         bit_stream_t *out)
{
    return BS_CHECK(bs_get_bs_bit(bs, b, out)) + __bs_skip(bs, 1);
}



static inline int __bs_scan_reverse(const bit_stream_t *bs, bool b,
                                    struct bit_ptroff *poffp,
                                    ssize_t max_len)
{
    size_t pos, len = bs_len(bs);
    struct bit_ptroff poff = bs->s;

    if (max_len > 0) {
        bit_ptroff_add(&poff, MAX(0, (ssize_t)len - max_len));
        len = MIN(len, (size_t)max_len);
    }
    pos = BS_CHECK(bsr(poff.p, poff.offset, len, !b));

    *poffp = BIT_PTROFF_NORMALIZED(poff.p, poff.offset + pos);
    return 0;
}

static inline ssize_t
bs_shrink_downto_bit(bit_stream_t *bs, bool b, ssize_t max_len)
{
    struct bit_ptroff poff = { { NULL }, 0 };

    BS_CHECK(__bs_scan_reverse(bs, b, &poff, max_len));
    bit_ptroff_add(&poff, 1);
    return __bs_clip_at(bs, &poff);
}

static inline ssize_t
bs_shrink_before_bit(bit_stream_t *bs, bool b, ssize_t max_len)
{
    struct bit_ptroff poff = { { NULL }, 0 };

    BS_CHECK(__bs_scan_reverse(bs, b, &poff, max_len));
    return __bs_clip_at(bs, &poff);
}

/* }}} */
/* Misc {{{ */

/* TODO optimize */
static inline bool bs_equals(bit_stream_t bs1, bit_stream_t bs2)
{
    size_t len = bs_len(&bs1);

    if (len != bs_len(&bs2))
        return false;

    while (!bs_done(&bs1)) {
        if (__bs_get_bit(&bs1) != __bs_get_bit(&bs2)) {
            return false;
        }
    }

    return true;
}

/* }}} */
/* Printing helpers {{{ */

static inline char *t_print_be_bs(bit_stream_t bs, size_t *len)
{
    sb_t sb;

    t_sb_init(&sb, 9 * DIV_ROUND_UP(bs_len(&bs), 8) + 1);
    while (!bs_done(&bs)) {
        if (bs_is_aligned(&bs)) {
            sb_addc(&sb, '.');
        }

        sb_addc(&sb, __bs_be_get_bit(&bs) ? '1' : '0');
    }

    if (len) {
        *len = sb.len;
    }
    return sb.data;
}

static inline char *t_print_bs(bit_stream_t bs, size_t *len)
{
    sb_t sb;

    t_sb_init(&sb, 9 * DIV_ROUND_UP(bs_len(&bs), 8) + 1);
    while (!bs_done(&bs)) {
        if (bs_is_aligned(&bs)) {
            sb_addc(&sb, '.');
        }

        sb_addc(&sb, __bs_get_bit(&bs) ? '1' : '0');
    }

    if (len) {
        *len = sb.len;
    }
    return sb.data;
}

#ifndef NDEBUG
#  define e_trace_be_bs(lvl, bs, fmt, ...)  \
    ({                                                                     \
        t_scope;                                                           \
        static const char spaces[] = "         ";                          \
                                                                           \
        uint8_t start_blank = bs_is_aligned(bs) ? 0                        \
                                                : ((bs)->s.offset % 8) + 1;\
                                                                           \
        e_trace(lvl, "[ %s%s%s ] --(%2zu) " fmt, spaces + 9 - start_blank, \
                t_print_be_bs(*(bs), NULL), spaces + 9 - ((bs)->e.offset % 8),\
                bs_len(bs), ##__VA_ARGS__);                                \
    })

#  define e_trace_bs(lvl, bs, fmt, ...)  \
    ({                                                                     \
        t_scope;                                                           \
        static const char spaces[] = "         ";                          \
                                                                           \
        uint8_t start_blank = bs_is_aligned(bs) ? 0                        \
                                                : ((bs)->s.offset % 8) + 1;\
                                                                           \
        e_trace(lvl, "[ %s%s%s ] --(%2zu) " fmt, spaces + 9 - start_blank, \
                t_print_bs(*(bs), NULL), spaces + 9 - ((bs)->e.offset % 8),\
                bs_len(bs), ##__VA_ARGS__);                                \
    })

#else
#  define e_trace_be_bs(...)
#  define e_trace_bs(...)
#endif

/* }}} */
#endif
