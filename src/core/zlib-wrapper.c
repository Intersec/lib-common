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

#include "zlib-wrapper.h"

ssize_t sb_add_compressed(sb_t *out, const void *data, size_t dlen,
                          int level, bool do_gzip)
{
    int err;
    sb_t orig = *out;
    z_stream stream = {
        .next_in   = (Bytef *)data,
        .avail_in  = dlen,
    };

    RETHROW(deflateInit2(&stream, level, Z_DEFLATED,
                         MAX_WBITS + (do_gzip ? 16 : 0),
                         MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY));

    for (;;) {
        stream.next_out  = (Bytef *)sb_grow(out, MAX(stream.avail_in / 2, 128));
        stream.avail_out = sb_avail(out);

        switch ((err = deflate(&stream, Z_FINISH))) {
          case Z_STREAM_END:
            __sb_fixlen(out, orig.len + stream.total_out);
            IGNORE(deflateEnd(&stream));
            return out->len - orig.len;
          case Z_OK:
            /* Compression OK, but not finished, must allocate more space */
            __sb_fixlen(out, orig.len + stream.total_out);
            break;
          default:
            __sb_rewind_adds(out, &orig);
            deflateEnd(&stream);
            return err;
        }
    }
}
