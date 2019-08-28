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

#include <lib-common/core.h>

#include "asn1.h"
#include "asn1-helpers.in.c"

/** \brief Decode a ber-encoded length to a 32 bits unsigned int.
  * \param[inout]ps Input byte stream.
  * \param[out]_len Output length.
  */
int ber_decode_len32(pstream_t *ps, uint32_t *_len)
{
    int c = ps_getc(ps);
    uint32_t len;

    if (unlikely(c == EOF))
        return -1;

    if (!(c & 0x80)) {
        /* Case 1 . Local variable first contains length. */
        *_len = c;
        return 0;
    }

    /* Case 2 . */
    /* Local variable first contains byte length of the computed value */
    c &= 0x7f;
    if (!ps_has(ps, c) || c <= 0 || c > 4) {
        if (c == 0) {
            e_trace(3, "(Indefinite length)");
            return 1;
        }

        e_trace(1, "invalid length encoding");

        return -1;
    }

    len = 0;
    while (c--) {
        len = (len << 8) | __ps_getc(ps);
    }
    *_len = len;
    return 0;
}

/** \brief Decode a ber-encoded integer.
  * \param[inout]ps Input byte stream.
  * \param[out]_val Output value.
  */
#define BER_DECODE_INT_IMPL(pfx) \
int ber_decode_##pfx(pstream_t *ps, pfx##_t *_val)                           \
{                                                                            \
    pfx##_t c;                                                               \
                                                                             \
    if (unlikely(ps_len(ps) > sizeof(pfx##_t))) {                            \
        e_trace(1, "invalid pfx encoding");                                  \
        return -1;                                                           \
    }                                                                        \
                                                                             \
    /* XXX: sign extension */                                                \
    c = (int8_t)ps_getc(ps);                                                 \
    while (!ps_done(ps))                                                     \
        c = (c << 8) | ps_getc(ps);                                          \
    *_val = c;                                                               \
    return 0;                                                                \
}

BER_DECODE_INT_IMPL(int16);
BER_DECODE_INT_IMPL(int32);
BER_DECODE_INT_IMPL(int64);

#define BER_DECODE_UINT_IMPL(pfx) \
int ber_decode_##pfx(pstream_t *ps, pfx##_t *_val)                           \
{                                                                            \
    pfx##_t c;                                                               \
                                                                             \
    if (unlikely(ps_len(ps) > sizeof(pfx##_t))) {                            \
        if (ps_len(ps) == sizeof(pfx##_t) + 1 && *ps->b == 0x00) {           \
           __ps_skip(ps, 1);                                                 \
        } else {                                                             \
            e_trace(1, "invalid pfx encoding");                              \
            return -1;                                                       \
        }                                                                    \
    }                                                                        \
                                                                             \
    /* XXX: sign extension */                                                \
    c = (int8_t)ps_getc(ps);                                                 \
    while (!ps_done(ps))                                                     \
        c = (c << 8) | ps_getc(ps);                                          \
    *_val = c;                                                               \
    return 0;                                                                \
}

BER_DECODE_UINT_IMPL(uint16);
BER_DECODE_UINT_IMPL(uint32);
BER_DECODE_UINT_IMPL(uint64);
