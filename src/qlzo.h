/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_QLZO_H
#define IS_LIB_COMMON_QLZO_H

#include <lib-common/core.h>

#define LZO_M1_MAX_OFFSET       0x0400
#define LZO_M2_MAX_OFFSET       0x0800
#define LZO_M3_MAX_OFFSET       0x4000
#define LZO_M4_MAX_OFFSET       0xbfff

#define LZO_M2_MAX_LEN  8

#define LZO_M1_MARKER   0
#define LZO_M2_MARKER   64
#define LZO_M3_MARKER   32
#define LZO_M4_MARKER   16

#define LZO_INPUT_PADDING       (8)
#define LZO_BUF_MEM_SIZE        (1 << (14 + sizeof(unsigned)))
static inline size_t lzo_cbuf_size(size_t insz) {
    /* Fast non overflowing code for insz * 17 / 16 */
    return insz + (insz >> 4) + 64 + 3;
}

#define LZO_ERR_INPUT_OVERRUN      (-1)
#define LZO_ERR_OUTPUT_OVERRUN     (-2)
#define LZO_ERR_BACKPTR_OVERRUN    (-3)
#define LZO_ERR_INPUT_NOT_CONSUMED (-4)

size_t  qlzo1x_compress(void *out, size_t outlen, pstream_t in, void *buf);

/*
 * qlzo1x_decompress is unsafe and assumes that the input stream can be
 * overread of LZO_INPUT_PADDING octets.
 *
 * qlzo1x_decompress_safe makes no such assumption but is significantly
 * slower.
 */
ssize_t qlzo1x_decompress(void *out, size_t outlen, pstream_t in);
ssize_t qlzo1x_decompress_safe(void *out, size_t outlen, pstream_t in);

#endif
