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

#include "hpack-priv.h"
#include <lib-common/log.h>

/* {{{ Huffman encoding & decoding */

int hpack_encode_huffman(lstr_t str, void *out_, int len)
{
#define OUTPUT_BYTE(val)                                                     \
    do {                                                                     \
        *out++ = (val);                                                      \
        if (out == out_end) {                                                \
            return len;                                                      \
        }                                                                    \
    } while (0)

    uint8_t *out = (uint8_t *)out_;
    uint8_t *out_end = out + len;

    /* acts a bit buffer aligned on the left (i.e., MSB) */
    struct {
        uint64_t word;
        unsigned bits;
    } bitbuf = {0, 0};

    if (unlikely(len == 0)) {
        return 0;
    }
    for (int i = 0; i != str.len; i++) {
        uint8_t ch = str.s[i];
        uint64_t codeword64 = hpack_huffcode_tab_g[ch].codeword;
        unsigned bitlen = hpack_huffcode_tab_g[ch].bitlen;

        bitbuf.word |= codeword64 << (64 - bitlen - bitbuf.bits);
        bitbuf.bits += bitlen;
        if (bitbuf.bits > 32) {
            /* output full bytes from the left of our 64-bit bitbuf */
            for (; bitbuf.bits >= 8; bitbuf.word <<= 8, bitbuf.bits -= 8) {
                OUTPUT_BYTE(bitbuf.word >> (64 - 8));
            }
        }
    }
    /* pad to a byte boundary with one-valued bits */
    if (likely(bitbuf.bits % 8)) {
        uint64_t codeword64 = 0xFF;
        unsigned bitlen = 8 - (bitbuf.bits % 8);

        bitbuf.word |= codeword64 << (64 - 8 - bitbuf.bits);
        bitbuf.bits += bitlen;
    }
    /* output the remaining full bytes */
    for (; bitbuf.bits >= 8; bitbuf.word <<= 8, bitbuf.bits -= 8) {
        OUTPUT_BYTE(bitbuf.word >> (64 - 8));
    }
    return out - (uint8_t *)out_;
#undef OUTPUT_BYTE
}

int hpack_decode_huffman(lstr_t str, void *out_)
{
    uint8_t *out = (uint8_t *)out_;
    uint8_t state;
    const hpack_huffdec_trans_t *trans = NULL;

    if (unlikely(str.len == 0)) {
        return 0;
    }
    state = 0;
    for (int i = 0; i != str.len; i++) {
        uint8_t b = str.s[i];
        uint8_t nibbles[2];

        nibbles[0] = b >> 4;
        nibbles[1] = b & 0xF;

        for (int n = 0; n < 2; n++) {
            trans = &hpack_huffdec_trans_tab_g[state][nibbles[n]];
            THROW_ERR_IF(trans->error);
            if (trans->emitter) {
                *(out)++ = trans->sym;
            }
            state = trans->state;
        }
    }
    THROW_ERR_UNLESS(trans->final);
    return out - (uint8_t *)out_;
}

/* }}} */
/* {{{ Integer encoding & decoding */

int hpack_encode_int(uint32_t val, uint8_t prefix_bits, byte out[8])
{
    byte *out_ = out;
    uint32_t max_prefix_num;

    /* RFC 7541 §5.1: Integer Representation */
    assert(prefix_bits >= 1 && prefix_bits <= 8);
    max_prefix_num = (1u << prefix_bits) - 1;
    if (val < max_prefix_num) {
        *out++ = val;
    } else {
        *out++ = max_prefix_num;
        val -= max_prefix_num;
        for (; val >> 7; val >>= 7) {
            *out++ = 0x80u | val;
        }
        *out++ = val;
    }
    return out - out_;
}

int hpack_decode_int(pstream_t *in, uint8_t prefix_bits, uint32_t *val)
{
    byte b;
    unsigned m;
    uint64_t res;
    uint32_t max_prefix_num;

    /* RFC 7541 §5.1: Integer Representation */
    /* Implementation limits: value <= UINT32_MAX && coded size <= 8 bytes */
    assert(prefix_bits >= 1 && prefix_bits <= 8);
    max_prefix_num = (1u << prefix_bits) - 1;
    THROW_ERR_IF(ps_done(in));
    b = max_prefix_num & __ps_getc(in);
    if (b < max_prefix_num) {
        *val = b;
        return 0;
    }
    res = 0;
    m = 0;
    for (int i = 1; i < 8; i++) {
        THROW_ERR_IF(ps_done(in));
        b = __ps_getc(in);
        res |= ((uint64_t)(0x7Fu & b)) << m;
        m += 7;
        if (b < 128) {
            res += max_prefix_num;
            /* check for overflow */
            THROW_ERR_IF(res > UINT32_MAX);
            *val = res;
            return 0;
        }
    }
    /* coded integer overruns the 8-byte size limit */
    return -1;
}

/* }}} */
