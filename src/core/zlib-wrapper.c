/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <lib-common/zlib-wrapper.h>

ssize_t sb_add_compressed(
    sb_t *out, const void *data, size_t dlen, int level, bool do_gzip
)
{
    int err;
    sb_t orig = *out;
    z_stream stream = {
        .next_in = (Bytef *)data,
        .avail_in = dlen,
    };

    RETHROW(deflateInit2(
        &stream, level, Z_DEFLATED, MAX_WBITS + (do_gzip ? 16 : 0),
        MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY
    ));

    for (;;) {
        stream.next_out =
            (Bytef *)sb_grow(out, MAX(stream.avail_in / 2, 128));
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

ssize_t sb_add_uncompressed(
    sb_t *out, const void *data, size_t dlen, size_t max_len, bool do_gzip
)
{
    int err;
    sb_t orig = *out;
    size_t in_left = dlen;
    z_stream stream = {
        .next_in = (Bytef *)data,
    };

    /* An allocation larger than MEM_ALLOC_MAX aborts. Lower max_len so that
     * the buffer can hold max_len + 1 bytes and the trailing zero. */
    if (out->len > (int)MEM_ALLOC_MAX - 2) {
        return Z_BUF_ERROR;
    }
    max_len = MIN(max_len, (size_t)(MEM_ALLOC_MAX - 2 - out->len));

    RETHROW(inflateInit2(&stream, MAX_WBITS + (do_gzip ? 16 : 0)));

    for (;;) {
        /* Give room for one byte more than max_len: inflate() writes it
         * only if the uncompressed data is too large. */
        size_t room = max_len - stream.total_out + 1;
        int chunk;

        /* avail_in is 32 bits wide: give a larger input in several parts. */
        if (!stream.avail_in) {
            stream.avail_in = MIN(in_left, UINT_MAX);
            in_left -= stream.avail_in;
        }
        chunk = MIN(room, MAX(2 * (size_t)stream.avail_in, 1024));
        stream.next_out = (Bytef *)sb_grow(out, chunk);
        stream.avail_out = MIN((size_t)sb_avail(out), room);

        err = inflate(&stream, Z_NO_FLUSH);
        __sb_fixlen(out, orig.len + stream.total_out);
        if (stream.total_out > max_len) {
            err = Z_BUF_ERROR;
            break;
        }
        if (err == Z_STREAM_END) {
            if (stream.avail_in || in_left) {
                err = Z_DATA_ERROR;
                break;
            }
            IGNORE(inflateEnd(&stream));
            return out->len - orig.len;
        }
        if (err == Z_BUF_ERROR || err == Z_NEED_DICT) {
            /* Z_BUF_ERROR with room in the output means that the chunk ends
             * before the end of the stream. A stream that needs a preset
             * dictionary cannot be uncompressed here. */
            err = Z_DATA_ERROR;
            break;
        }
        if (err != Z_OK) {
            break;
        }
    }

    __sb_rewind_adds(out, &orig);
    IGNORE(inflateEnd(&stream));
    return err;
}
