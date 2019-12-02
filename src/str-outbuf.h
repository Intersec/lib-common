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

#ifndef IS_LIB_COMMON_STR_OUTBUF_H
#define IS_LIB_COMMON_STR_OUTBUF_H

#include <lib-common/core.h>
#include <lib-common/container-htlist.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

/****************************************************************************/
/* Outbuf                                                                   */
/****************************************************************************/

typedef struct outbuf_t {
    int      length;
    int      sb_trailing;
    sb_t     sb;
    htlist_t chunks_list;
} outbuf_t;

void ob_check_invariants(outbuf_t * nonnull ob) __leaf;

static inline outbuf_t * nonnull ob_init(outbuf_t * nonnull ob)
{
    ob->length      = 0;
    ob->sb_trailing = 0;
    htlist_init(&ob->chunks_list);
    sb_init(&ob->sb);
    return ob;
}

void ob_wipe(outbuf_t * nonnull ob) __leaf;
GENERIC_NEW(outbuf_t, ob);
GENERIC_DELETE(outbuf_t, ob);
void ob_merge(outbuf_t * nonnull dst, outbuf_t * nonnull src) __leaf;
void ob_merge_wipe(outbuf_t * nonnull dst, outbuf_t * nonnull src) __leaf;
void ob_merge_delete(outbuf_t * nonnull dst,
                     outbuf_t * nullable * nonnull src) __leaf;
static inline bool ob_is_empty(const outbuf_t * nonnull ob)
{
    return ob->length == 0;
}

int ob_write_with(outbuf_t * nonnull ob, int fd,
                  ssize_t (* nullable writerv)(int,
                                               const struct iovec * nonnull,
                                               int, void * nullable),
                  void * nullable priv) __leaf;
static inline int ob_write(outbuf_t * nonnull ob, int fd) {
    return ob_write_with(ob, fd, NULL, NULL);
}
int ob_xread(outbuf_t * nonnull ob, int fd, int size) __leaf;


/****************************************************************************/
/* Chunks                                                                   */
/****************************************************************************/

#define OUTBUF_CHUNK_MIN_SIZE    (16 << 10)

enum outbuf_on_wipe {
    OUTBUF_DO_NOTHING,
    OUTBUF_DO_FREE,
    OUTBUF_DO_MUNMAP,
};

typedef struct outbuf_chunk_t {
    htnode_t  chunks_link;
    int       length;
    int       offset;
    int       sb_leading;
    int       on_wipe;
    union {
        const void    * nonnull p;
        const uint8_t * nonnull b;
        void          * nonnull vp;
    } u;
} outbuf_chunk_t;
void ob_chunk_wipe(outbuf_chunk_t * nonnull obc) __leaf;
GENERIC_DELETE(outbuf_chunk_t, ob_chunk);

static inline void ob_add_chunk(outbuf_t * nonnull ob,
                                outbuf_chunk_t * nonnull obc)
{
    htlist_add_tail(&ob->chunks_list, &obc->chunks_link);
    ob->length += (obc->length - obc->offset);
    obc->sb_leading = ob->sb_trailing;
    ob->sb_trailing = 0;
}

static inline sb_t * nonnull outbuf_sb_start(outbuf_t * nonnull ob,
                                             int * nonnull oldlen)
{
    *oldlen = ob->sb.len;
    return &ob->sb;
}

static inline void outbuf_sb_end(outbuf_t * nonnull ob, int oldlen)
{
    ob->sb_trailing += ob->sb.len - oldlen;
    ob->length      += ob->sb.len - oldlen;
}

#define OB_WRAP(sb_fun, ob, ...) \
    do {                                             \
        outbuf_t *__ob = (ob);                       \
        int curlen = __ob->sb.len;                   \
                                                     \
        sb_fun(&__ob->sb, ##__VA_ARGS__);            \
        __ob->sb_trailing += __ob->sb.len - curlen;  \
        __ob->length      += __ob->sb.len - curlen;  \
    } while (0)

#define ob_add(ob, data, len)       OB_WRAP(sb_add,   ob, data, len)
#define ob_adds(ob, data)           OB_WRAP(sb_adds,  ob, data)
#define ob_addf(ob, fmt, ...)       OB_WRAP(sb_addf,  ob, fmt, ##__VA_ARGS__)
#define ob_addvf(ob, fmt, ap)       OB_WRAP(sb_addvf, ob, fmt, ap)
#define ob_addsb(ob, sb)            OB_WRAP(sb_addsb, ob, sb)
#define ob_add_urlencode(ob, s, l)  OB_WRAP(sb_add_urlencode, ob, s, l)
#define ob_adds_urlencode(ob, s)    OB_WRAP(sb_adds_urlencode, ob, s)

/* XXX: invalidated as sonn as the outbuf is consumed ! */
static inline int ob_reserve(outbuf_t * nonnull ob, unsigned len)
{
    int res = ob->sb.len;

    sb_growlen(&ob->sb, len);
    ob->sb_trailing += len;
    ob->length      += len;
    return res;
}

static inline
void ob_add_memchunk(outbuf_t * nonnull ob, const void * nonnull ptr,
                     int len, bool is_const)
{
    if (len <= OUTBUF_CHUNK_MIN_SIZE) {
        ob_add(ob, ptr, len);
        if (!is_const)
            ifree((void *)ptr, MEM_LIBC);
    } else {
        outbuf_chunk_t *obc = p_new(outbuf_chunk_t, 1);

        obc->u.p    = ptr;
        obc->length = len;
        if (!is_const)
            obc->on_wipe = OUTBUF_DO_FREE;
        ob_add_chunk(ob, obc);
    }
}

static inline void ob_add_memmap(outbuf_t * nonnull ob, void * nonnull map,
                                 int len)
{
    if (len <= OUTBUF_CHUNK_MIN_SIZE) {
        ob_add(ob, map, len);
        munmap(map, len);
    } else {
        outbuf_chunk_t *obc = p_new(outbuf_chunk_t, 1);

        obc->u.p     = map;
        obc->length  = len;
        obc->on_wipe = OUTBUF_DO_MUNMAP;
        ob_add_chunk(ob, obc);
    }
}

int ob_add_file(outbuf_t * nonnull ob, const char * nonnull file, int size)
    __leaf;

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
