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

#include <lib-common/arith.h>
#include <lib-common/qlzo.h>

typedef struct ostream_t {
    uint8_t *b;
    uint8_t *b_end;
} ostream_t;

static ALWAYS_INLINE
int lzo_get_varlen(pstream_t *in, unsigned u, unsigned mask)
{
    unsigned sz = u & mask;
    const uint8_t *b = in->b;

    if (!sz) {
        do {
            if (unlikely(++b >= in->b_end))
                return LZO_ERR_INPUT_OVERRUN;
        } while (!*b);
        sz = *b + mask + 255 * (b - in->b - 1);
    }
    in->b = b + 1;
    return sz;
}

static ALWAYS_INLINE
int lzo_copy_input(pstream_t *in, ostream_t *os, unsigned sz)
{
    if (unlikely(!ps_has(in, sz)))
        return LZO_ERR_INPUT_OVERRUN;
    if (unlikely(os->b + sz > os->b_end))
        return LZO_ERR_OUTPUT_OVERRUN;
    os->b = mempcpy(os->b, in->b, sz);
    return __ps_skip(in, sz);
}

/* XXX: assumes sz <= 4 */
static ALWAYS_INLINE
int lzo_copy_input_small(pstream_t *in, ostream_t *os, unsigned sz)
{
    const uint8_t *src = in->b;
    uint8_t *dst = os->b;

    if (unlikely(!ps_has(in, sz)))
        return LZO_ERR_INPUT_OVERRUN;
    if (likely(dst + 4 <= os->b_end)) {
        put_unaligned_cpu32(dst, get_unaligned_cpu32(src));
    } else {
        if (unlikely(dst + sz > os->b_end))
            return LZO_ERR_OUTPUT_OVERRUN;
        dst[0] = src[0];
        if (sz > 1) {
            dst[1] = src[1];
            if (sz > 2)
                dst[2] = src[2];
        }
    }
    os->b += sz;
    return __ps_skip(in, sz);
}

static ALWAYS_INLINE
int lzo_copy_backptr(ostream_t *os, const uint8_t *out_orig,
                     unsigned back, unsigned sz)
{
    const uint8_t *src = os->b - back;
    uint8_t *dst = os->b;

    if (unlikely(src < out_orig)) {
        return LZO_ERR_BACKPTR_OVERRUN;
    }
    if (unlikely(dst + sz > os->b_end)) {
        return LZO_ERR_OUTPUT_OVERRUN;
    }

    if (back == 1) {
        memset(dst, *src, sz);
        os->b += sz;
        return 0;
    }

    while (sz >= back) {
        memcpy(dst, src, back);
        dst += back;
        src += back;
        sz -= back;
    }
    while (sz-- > 0) {
        *dst++ = *src++;
    }
    os->b = dst;
    return 0;
}

static ALWAYS_INLINE int
lzo_copy_backptr2(ostream_t *os, const uint8_t *out_orig, unsigned back)
{
    const uint8_t *src = os->b - back;
    uint8_t *dst = os->b;

    if (unlikely(src < out_orig))
        return LZO_ERR_BACKPTR_OVERRUN;
    if (unlikely(dst + 2 > os->b_end))
        return LZO_ERR_OUTPUT_OVERRUN;

    dst[0] = src[0];
    dst[1] = src[1];
    os->b += 2;
    return 0;
}

static ALWAYS_INLINE
int lzo_copy_backptr3(ostream_t *os, const uint8_t *out_orig, unsigned back)
{
    const uint8_t *src = os->b - back;
    uint8_t *dst = os->b;

    if (unlikely(src < out_orig))
        return LZO_ERR_BACKPTR_OVERRUN;
    if (unlikely(dst + 3 > os->b_end))
        return LZO_ERR_OUTPUT_OVERRUN;

    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    os->b += 3;
    return 0;
}

static ssize_t decompress(void *_out, size_t outlen, pstream_t in, bool safe)
{
    uint8_t *out_orig = _out;
    ostream_t os = {
        .b      = out_orig,
        .b_end  = out_orig + outlen,
    };
#define SAFE_CHECK_LEN(len) \
    if (safe && unlikely(!ps_has(&in, len))) \
        return LZO_ERR_INPUT_OVERRUN

    SAFE_CHECK_LEN(1);
    if (in.b[0] > 17)
        RETHROW(lzo_copy_input(&in, &os, __ps_getc(&in) - 17));
    for (;;) {
        unsigned u, sz, back;

        SAFE_CHECK_LEN(1);
        if ((u = in.b[0]) >= LZO_M4_MARKER) {
          match_2to4:
            if (u >= LZO_M2_MARKER) {
                SAFE_CHECK_LEN(2);
                sz    = (u >> 5) + 1;
                back  = (in.b[1] << 3) + ((u >> 2) & 7) + 1;
                __ps_skip(&in, 2);
            } else if (u >= LZO_M3_MARKER) {
                sz    = RETHROW(lzo_get_varlen(&in, u, 31)) + 2;
                SAFE_CHECK_LEN(2);
                u     = __ps_get_le16(&in);
                back  = (u >> 2) + 1;
            } else {
                sz    = RETHROW(lzo_get_varlen(&in, u, 7)) + 2;
                back  = ((u & 8) << 11);
                SAFE_CHECK_LEN(2);
                u     = __ps_get_le16(&in);
                back += (u >> 2);
                if (back == 0)
                    break;
                back += LZO_M3_MAX_OFFSET;
            }
            RETHROW(lzo_copy_backptr(&os, out_orig, back, sz));
        } else {
            sz    = RETHROW(lzo_get_varlen(&in, u, 15)) + 3;
            RETHROW(lzo_copy_input(&in, &os, sz));
            SAFE_CHECK_LEN(1);
            if ((u = in.b[0]) >= LZO_M4_MARKER)
                goto match_2to4;
            SAFE_CHECK_LEN(2);
            back  = (1 << 11) + (in.b[1] << 2) + (u >> 2) + 1;
            __ps_skip(&in, 2);
            RETHROW(lzo_copy_backptr3(&os, out_orig, back));
        }

        while ((sz = (u & 3))) {
            RETHROW(lzo_copy_input_small(&in, &os, sz));
            SAFE_CHECK_LEN(1);
            if ((u = in.b[0]) >= LZO_M4_MARKER)
                goto match_2to4;
            SAFE_CHECK_LEN(2);
            back = (in.b[1] << 2) + (u >> 2) + 1;
            __ps_skip(&in, 2);
            RETHROW(lzo_copy_backptr2(&os, out_orig, back));
        }
    }

    if (likely(in.b == in.b_end))
        return os.b - out_orig;
    if (in.b < in.b_end)
        return LZO_ERR_INPUT_NOT_CONSUMED;
    return LZO_ERR_INPUT_OVERRUN;
#undef SAFE_CHECK_LEN
}

__flatten
ssize_t qlzo1x_decompress(void *out, size_t outlen, pstream_t in)
{
    return decompress(out, outlen, in, false);
}

__flatten
ssize_t qlzo1x_decompress_safe(void *out, size_t outlen, pstream_t in)
{
    return decompress(out, outlen, in, true);
}
