/**************************************************************************/
/*                                                                        */
/*  Copyright (C) 2022 INTERSEC SA                                        */
/*                                                                        */
/*  Should you receive a copy of this source code, you must check you     */
/*  have a proper, written authorization of INTERSEC to hold it. If you   */
/*  don't have such an authorization, you must DELETE all source code     */
/*  files in your possession, and inform INTERSEC of the fact you obtain  */
/*  these files. Should you not comply to these terms, you can be         */
/*  prosecuted in the extent permitted by applicable law.                 */
/*                                                                        */
/**************************************************************************/

#ifndef IS_LIB_COMMON_NET_HPACK_PRIV_H
#define IS_LIB_COMMON_NET_HPACK_PRIV_H

#include <lib-common/core.h>
#include <lib-common/net/hpack.h>

/* {{{ Huffman coding & decoding */

/* Huffman code table entry */
typedef struct hpack_huffcode_t {
    /* bits aligned to LSB */
    uint32_t codeword;
    /* bits in codeword */
    unsigned bitlen;
} hpack_huffcode_t;

/* Huffman code table entries */
extern const hpack_huffcode_t hpack_huffcode_tab_g[256 + 1];

/* Huffman decoder's state-transition table entries */
/* Decoder works by consuming each nibble of input transiting over the Huffman
 * tree. We have a state for every non-leaf node while leaf nodes correspond
 * to direct transitions through the root node each emitting the corresponding
 * decoded byte.
 */
typedef struct hpack_huffdec_trans_t {
    /* new state after consuming the 4-bit chunk */
    uint8_t state;
    /* emitted symbol (byte) if emitter */
    uint8_t sym;
    /* does this transition emit a decoded symbol? */
    uint16_t emitter : 1;
    /* is this a final transition? */
    uint16_t final : 1;
    /* is this an error transition? */
    uint16_t error : 1;
} hpack_huffdec_trans_t;

/* Huffman state-transition table based on 4-bit chunks (i.e., nibbles) */
extern const hpack_huffdec_trans_t hpack_huffdec_trans_tab_g[256][16];

/**
 * Return the length of the huffman-coded version of \p str
 */
static inline size_t hpack_get_huffman_len(lstr_t str)
{
    size_t bits = 0;

    for (int i = 0; i != str.len; i++) {
        uint8_t ch = str.s[i];

        bits += hpack_huffcode_tab_g[ch].bitlen;
    }
    return DIV_ROUND_UP(bits, 8U);
}

/**
 * Return a worst case estimate for the length of the huffman-coded version of
 * \p str
 */
static inline size_t hpack_get_huffman_len_estimate(lstr_t str)
{
    return 4 * str.len;
}

/**
 * Write at most \p len bytes of the huffman-coded version of \p str
 *
 * \return the number of bytes written
 */
int hpack_encode_huffman(lstr_t str, void *out, int len);

/** Decode the huffman-coded \p str into \p out
 *
 * \return the number of bytes written to \p out or -1 in case of error
 *
 * \note caller should ensure that \p out has enough size. Worst case estimate
 * for the decoded length is the same length of \p str.
 */
int hpack_decode_huffman(lstr_t str, void *out);

/* }}} */

#endif /* ! IS_LIB_COMMON_NET_HPACK_PRIV_H */
