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
