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

#include "arith.h"
#include "qlzo.h"

#define D_BITS          14
#define D_MASK          ((1u << D_BITS) - 1)
#define D_HIGH          ((D_MASK >> 1) + 1)

static ALWAYS_INLINE uint32_t HASH3(const uint8_t *p)
{
    const uint32_t s1 = 5;
    const uint32_t s2 = 5;
    const uint32_t s3 = 6;

    uint32_t p0 = p[0], p1 = p[1], p2 = p[2], p3 = p[3];
    uint32_t h;

    h = (p3 << (s1 + s2 + s3))
      ^ (p2 << (s1 + s2))
      ^ (p1 << (s1))
      ^ (p0);

    h = (h << 5) + h;
    return (h >> 5) & D_MASK;
}

static ALWAYS_INLINE uint32_t HASH3_SECONDARY(uint32_t h)
{
    return (h & (D_MASK & 0x7ff)) ^ (D_HIGH | 0x1f);
}

static ALWAYS_INLINE uint8_t *
lzo_put_varlen(uint8_t *out, unsigned sz, uint32_t mask, uint32_t marker)
{
    if (sz <= mask) {
        *out++ = marker | sz;
        return out;
    }
    *out++ = marker;
    sz -= mask;
    while (sz > 255) {
        sz    -= 255;
        *out++ = 0;
    }

    *out++ = sz;
    return out;
}

static ALWAYS_INLINE uint8_t *
lzo_put_m1(uint8_t *out, const uint8_t *in, uint32_t sz)
{
    if (sz <= 3) {
        out[-2] |= sz;
        out[0] = in[0];
        if (sz > 1) {
            out[1] = in[1];
            if (sz > 2)
                out[2] = in[2];
        }
        return out + sz;
    }

    out = lzo_put_varlen(out, sz - 3, 15, 0);
    return mempcpy(out, in, sz);
}

static ALWAYS_INLINE uint8_t *compress(uint8_t *out, pstream_t *in, void *buf)
{
    const uint8_t * const orig_in = in->b;
    const uint8_t * const ip_end  = in->b_end - LZO_M2_MAX_LEN - 5;
    unsigned * const dict = buf;

    const uint8_t *ii = in->b;
    uint32_t dindex;

    __ps_skip(in, 4);
    dindex = HASH3(in->b);
    goto literal;

    while (likely(in->b < ip_end)) {
        uint32_t word;
        uint32_t m_off, m_len, m_len_max;
        const uint8_t *m_pos;

        word   = get_unaligned_cpu32(in->b);
        dindex = HASH3(in->b);
        m_pos  = orig_in + dict[dindex];

        if ((size_t)(m_pos + LZO_M4_MAX_OFFSET - in->b) >= LZO_M4_MAX_OFFSET)
            goto literal;

        if (get_unaligned_cpu32(m_pos) == word)
            goto match;

        dindex = HASH3_SECONDARY(dindex);
        m_pos  = orig_in + dict[dindex];

        if ((size_t)(m_pos + LZO_M4_MAX_OFFSET - in->b) >= LZO_M4_MAX_OFFSET
        ||  get_unaligned_cpu32(m_pos) != word)
        {
literal:
            dict[dindex] = in->b++ - orig_in;
            continue;
        }

match:
        dict[dindex] = in->b - orig_in;
        if (in->b != ii)
            out = lzo_put_m1(out, ii, in->b - ii);

        m_len_max = ps_len(in);
        for (m_len = 4; m_len + 2 <= m_len_max; m_len += 2) {
            if (get_unaligned_cpu16(m_pos + m_len) !=
                get_unaligned_cpu16(in->b + m_len))
            {
                break;
            }
        }
        m_len += m_len < m_len_max && m_pos[m_len] == in->b[m_len];

        m_off  = in->b - m_pos;
        if (m_len <= LZO_M2_MAX_LEN) {
            if (m_off <= LZO_M2_MAX_OFFSET) {
                m_off -= 1;
                *out++ = ((m_len - 1) << 5) | ((m_off & 7) << 2);
                *out++ = (m_off >> 3);
                goto m2_offset_already_done;
            } else if (m_off <= LZO_M3_MAX_OFFSET) {
                m_off -= 1;
                *out++ = LZO_M3_MARKER | (m_len - 2);
            } else {
                m_off -= LZO_M3_MAX_OFFSET;
                *out++ = LZO_M4_MARKER | ((m_off >> 11) & 8) | (m_len - 2);
            }
        } else {
            if (m_off <= LZO_M3_MAX_OFFSET) {
                m_off -= 1;
                out = lzo_put_varlen(out, m_len - 2, 31, LZO_M3_MARKER);
            } else {
                m_off -= LZO_M3_MAX_OFFSET;
                out = lzo_put_varlen(out, m_len - 2, 7,
                                     LZO_M4_MARKER | ((m_off >> 11) & 8));
            }
        }
        out = put_unaligned_le16(out, (m_off << 2));

m2_offset_already_done:
        __ps_skip(in, m_len);
        ii = in->b;
    }

    in->b = ii;
    return out;
}

size_t qlzo1x_compress(void *orig_out, size_t outlen, pstream_t in, void *buf)
{
    uint8_t *out = orig_out;
    size_t t;

    /* XXX: initializing the dictionnary is absolutely not useful,
     *      algorithm continues to work properly with random data in.
     *
     *      but having valgrind complain sucks during debugging.
     */
    if (mem_tool_is_running(MEM_TOOL_VALGRIND))
        memset(buf, 0, LZO_BUF_MEM_SIZE);

    if (likely(ps_has(&in, LZO_M2_MAX_LEN + 5)))
        out = compress(out, &in, buf);
    t  = ps_len(&in);
    if (t > 0) {
        if (out == orig_out && t <= 238) {
            *out++ = (17 + t);
            out = mempcpy(out, in.b, t);
        } else {
            out = lzo_put_m1(out, in.b, t);
        }
    }

    out[0] = LZO_M4_MARKER | 1;
    out[1] = 0;
    out[2] = 0;
    return out + 3 - (uint8_t *)orig_out;
}
