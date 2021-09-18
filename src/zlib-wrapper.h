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

#ifndef IS_LIB_COMMON_ZLIB_WRAPPER_H
#define IS_LIB_COMMON_ZLIB_WRAPPER_H

#include <zlib.h>
#include <lib-common/core.h>
#include <lib-common/str-outbuf.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

/** Add compressed data in the string buffer.
 *
 * Takes the chunk of data pointed by @p data and @p dlen and add it
 * compressed using zlib in the sb_t @p out.
 *
 * @param out     output buffer
 * @param data    source data
 * @param dlen    size of the data pointed by @p data.
 * @param level   compression level
 * @param do_gzip if true compresses using 'gzip', else use 'deflate'
 *
 * @return amount of data written in the stream or a negative zlib error in
 * case of error.
 */
ssize_t sb_add_compressed(sb_t * nonnull out, const void * nonnull data,
                          size_t dlen, int level, bool do_gzip);

#define ob_add_compressed(ob, data, dlen, level, do_gzip)  \
    OB_WRAP(sb_add_compressed, ob, data, dlen, level, do_gzip)

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
