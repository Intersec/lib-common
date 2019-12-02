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

#include <lib-common/unix.h>
#include <lib-common/str-outbuf.h>

void ob_check_invariants(outbuf_t *ob)
{
    int len = ob->length, sb_len = ob->sb.len;

    htlist_for_each(it, &ob->chunks_list) {
        outbuf_chunk_t *obc = htlist_entry(it, outbuf_chunk_t, chunks_link);

        sb_len -= obc->sb_leading;
        len    -= obc->sb_leading;
        len    -= (obc->length - obc->offset);
    }

    assert (len == ob->sb_trailing);
    assert (sb_len == ob->sb_trailing);
}

void ob_chunk_wipe(outbuf_chunk_t *obc)
{
    switch (obc->on_wipe) {
      case OUTBUF_DO_FREE:
        ifree(obc->u.vp, MEM_LIBC);
        break;
      case OUTBUF_DO_MUNMAP:
        munmap(obc->u.vp, obc->length);
        break;
    }
}

static void ob_merge_(outbuf_t *dst, outbuf_t *src, bool wipe)
{
    outbuf_chunk_t *obc;

    sb_addsb(&dst->sb, &src->sb);

    if (!htlist_is_empty(&src->chunks_list)) {
        obc = htlist_first_entry(&src->chunks_list, outbuf_chunk_t,
                                 chunks_link);
        obc->sb_leading  += dst->sb_trailing;
        dst->sb_trailing  = src->sb_trailing;
        htlist_splice_tail(&dst->chunks_list, &src->chunks_list);
    } else {
        dst->sb_trailing += src->sb_trailing;
    }
    dst->length += src->length;

    if (wipe) {
        sb_wipe(&src->sb);
    } else {
        src->length      = 0;
        src->sb_trailing = 0;
        htlist_init(&src->chunks_list);
        sb_reset(&src->sb);
    }
}

void ob_merge(outbuf_t *dst, outbuf_t *src)
{
    ob_merge_(dst, src, false);
}

void ob_merge_wipe(outbuf_t *dst, outbuf_t *src)
{
    ob_merge_(dst, src, true);
}

void ob_merge_delete(outbuf_t *dst, outbuf_t **srcp)
{
    if (*srcp) {
        ob_merge_(dst, *srcp, true);
        p_delete(srcp);
    }
}

void ob_wipe(outbuf_t *ob)
{
    while (!htlist_is_empty(&ob->chunks_list)) {
        outbuf_chunk_t *obc;

        obc = htlist_pop_entry(&ob->chunks_list, outbuf_chunk_t, chunks_link);
        ob_chunk_delete(&obc);
    }
    sb_wipe(&ob->sb);
}

static int sb_xread(sb_t *sb, int fd, int size)
{
    void *p = sb_grow(sb, size);
    int res = xread(fd, p, size);

    __sb_fixlen(sb, sb->len + (res < 0 ? 0 : size));
    return res;
}
int ob_xread(outbuf_t *ob, int fd, int size)
{
    RETHROW(sb_xread(&ob->sb, fd, size));
    ob->sb_trailing += size;
    ob->length      += size;
    return 0;
}

int ob_add_file(outbuf_t *ob, const char *file, int size)
{
    int fd = RETHROW(open(file, O_RDONLY));

    if (size < 0) {
        struct stat st;

        if (fstat(fd, &st)) {
            PROTECT_ERRNO(close(fd));
            return -1;
        }
        size = st.st_size;
    }

    if (size <= OUTBUF_CHUNK_MIN_SIZE) {
        if (ob_xread(ob, fd, size)) {
            PROTECT_ERRNO(close(fd));
            return -1;
        }
        close(fd);
    } else {
        void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

        PROTECT_ERRNO(close(fd));
        if (map == MAP_FAILED)
            return -1;
        madvise(map, size, MADV_SEQUENTIAL);
        ob_add_memmap(ob, map, size);
    }
    return 0;
}

static int ob_consume(outbuf_t *ob, int len)
{
    ob->length -= len;

    while (!htlist_is_empty(&ob->chunks_list)) {
        outbuf_chunk_t *obc;

        obc = htlist_first_entry(&ob->chunks_list, outbuf_chunk_t,
                                 chunks_link);
        if (len < obc->sb_leading) {
            sb_skip(&ob->sb, len);
            obc->sb_leading -= len;
            return 0;
        }
        if (obc->sb_leading) {
            sb_skip(&ob->sb, obc->sb_leading);
            len -= obc->sb_leading;
            obc->sb_leading = 0;
        }
        if (obc->offset + len < obc->length) {
            obc->offset += len;
            return 0;
        }
        len -= (obc->length - obc->offset);

        htlist_pop(&ob->chunks_list);
        ob_chunk_delete(&obc);
    }

    assert (len <= ob->sb_trailing);
    sb_skip(&ob->sb, len);
    ob->sb_trailing -= len;
    return 0;
}

int ob_write_with(outbuf_t *ob, int fd,
                  ssize_t (*writerv)(int, const struct iovec *, int, void *),
                  void *priv)
{
#define PREPARE_AT_LEAST  (64U << 10)
    struct iovec iov[IOV_MAX];
    size_t iovcnt = 0, sb_pos = 0, iov_size = 0;

    if (!ob->length)
        return 0;

    htlist_for_each(it, &ob->chunks_list) {
        outbuf_chunk_t *obc = htlist_entry(it, outbuf_chunk_t, chunks_link);
        size_t len;

        len = obc->sb_leading;
        if (len) {
            iov[iovcnt++] = MAKE_IOVEC(ob->sb.data + sb_pos, len);
            sb_pos   += len;
            iov_size += len;
        }

        len = obc->length - obc->offset;
        iov[iovcnt++] = MAKE_IOVEC(obc->u.b + obc->offset, len);
        iov_size += len;
        if (iov_size > PREPARE_AT_LEAST || iovcnt + 2 >= sizeof(iov))
            goto doit;
    }

    if (ob->sb_trailing) {
        iov[iovcnt++] = MAKE_IOVEC(ob->sb.data + sb_pos, ob->sb_trailing);
        assert ((size_t)ob->sb.len == sb_pos + ob->sb_trailing);
    }

  doit:
    return ob_consume(ob, RETHROW(writerv ?
                                  (*writerv)(fd, iov, iovcnt, priv) :
                                  writev(fd, iov, iovcnt)));
}
