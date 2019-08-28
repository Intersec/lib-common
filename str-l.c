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

#include "unix.h"
#include "core.h"

/* {{{ Base helpers */

void (lstr_munmap)(lstr_t *dst)
{
    if (munmap(dst->v, dst->len) < 0) {
        e_panic("bad munmap: %m");
    }
}

void mp_lstr_copy_(mem_pool_t *mp, lstr_t *dst, const void *s, int len)
{
    mp = mp ?: &mem_pool_libc;
    if (dst->mem_pool == (mp->mem_pool & MEM_POOL_MASK)) {
        mp_delete(mp, &dst->v);
    } else
    if (dst->mem_pool == MEM_MMAP) {
        (lstr_munmap)(dst);
    } else {
        ifree(dst->v, dst->mem_pool);
    }
    if (s == NULL) {
        *dst = lstr_init_(NULL, 0, MEM_STATIC);
    } else {
        *dst = lstr_init_(s, len, mp->mem_pool & MEM_POOL_MASK);
    }
}

void mp_lstr_copys(mem_pool_t *mp, lstr_t *dst, const char *s, int len)
{
    if (s) {
        if (len < 0) {
            len = strlen(s);
        }
        mp = mp ?: &mem_pool_libc;
        mp_lstr_copy_(mp, dst, mp_dupz(mp, s, len), len);
    } else {
        mp_lstr_copy_(&mem_pool_static, dst, NULL, 0);
    }
}

void mp_lstr_copy(mem_pool_t *mp, lstr_t *dst, const lstr_t src)
{
    if (src.s) {
        mp = mp ?: &mem_pool_libc;
        mp_lstr_copy_(mp, dst, mp_dupz(mp, src.s, src.len), src.len);
    } else {
        mp_lstr_copy_(&mem_pool_static, dst, NULL, 0);
    }
}

lstr_t mp_lstr_dups(mem_pool_t *mp, const char *s, int len)
{
    if (!s) {
        return LSTR_NULL_V;
    }
    if (len < 0) {
        len = strlen(s);
    }
    return mp_lstr_init(mp, mp_dupz(mp, s, len), len);
}

lstr_t mp_lstr_dup(mem_pool_t *mp, const lstr_t s)
{
    if (!s.s) {
        return LSTR_NULL_V;
    }
    return mp_lstr_init(mp, mp_dupz(mp, s.s, s.len), s.len);
}

void mp_lstr_persists(mem_pool_t *mp, lstr_t *s)
{
    mp = mp ?: &mem_pool_libc;
    if (s->mem_pool != MEM_LIBC
    &&  s->mem_pool != (mp->mem_pool & MEM_POOL_MASK))
    {
        s->s        = (char *)mp_dupz(mp, s->s, s->len);
        s->mem_pool = mp->mem_pool & MEM_POOL_MASK;
    }
}

lstr_t mp_lstr_dup_ascii_reversed(mem_pool_t *mp, const lstr_t v)
{
    char *str;

    if (!v.s) {
        return v;
    }

    str = mp_new_raw(mp, char, v.len + 1);

    for (int i = 0; i < v.len; i++) {
        str[i] = v.s[v.len - i - 1];
    }
    str[v.len] = '\0';

    return mp_lstr_init(mp, str, v.len);
}

lstr_t mp_lstr_dup_utf8_reversed(mem_pool_t *mp, const lstr_t v)
{
    int prev_off = 0;
    char *str;

    if (!v.s) {
        return v;
    }

    str = mp_new_raw(mp, char, v.len + 1);
    while (prev_off < v.len) {
        int off = prev_off;
        int c = utf8_ngetc_at(v.s, v.len, &off);

        if (unlikely(c < 0)) {
            return LSTR_NULL_V;
        }
        memcpy(str + v.len - off, v.s + prev_off, off - prev_off);
        prev_off = off;
    }
    return mp_lstr_init(mp, str, v.len);
}

lstr_t mp_lstr_cat(mem_pool_t *mp, const lstr_t s1, const lstr_t s2)
{
    int    len;
    lstr_t res;
    void  *s;

    if (unlikely(!s1.s && !s2.s)) {
        return LSTR_NULL_V;
    }

    len = s1.len + s2.len;
    res = mp_lstr_init(mp, mp_new_raw(mp, char, len + 1), len);
    s = (void *)res.v;
    s = mempcpy(s, s1.s, s1.len);
    mempcpyz(s, s2.s, s2.len);
    return res;
}

lstr_t mp_lstr_cat3(mem_pool_t *mp, const lstr_t s1, const lstr_t s2,
                    const lstr_t s3)
{
    int    len;
    lstr_t res;
    void  *s;

    if (unlikely(!s1.s && !s2.s && !s3.s)) {
        return LSTR_NULL_V;
    }

    len = s1.len + s2.len + s3.len;
    res = mp_lstr_init(mp, mp_new_raw(mp, char, len + 1), len);
    s = (void *)res.v;
    s = mempcpy(s, s1.s, s1.len);
    s = mempcpy(s, s2.s, s2.len);
    mempcpyz(s, s3.s, s3.len);
    return res;
}

bool lstr_utf8_iendswith(const lstr_t s1, const lstr_t s2)
{
    SB_1k(sb1);
    SB_1k(sb2);

    RETHROW(sb_normalize_utf8(&sb1, s1.s, s1.len, true));
    RETHROW(sb_normalize_utf8(&sb2, s2.s, s2.len, true));

    return lstr_endswith(LSTR_SB_V(&sb1), LSTR_SB_V(&sb2));
}

bool lstr_utf8_endswith(const lstr_t s1, const lstr_t s2)
{
    SB_1k(sb1);
    SB_1k(sb2);

    RETHROW(sb_normalize_utf8(&sb1, s1.s, s1.len, false));
    RETHROW(sb_normalize_utf8(&sb2, s2.s, s2.len, false));

    return lstr_endswith(LSTR_SB_V(&sb1), LSTR_SB_V(&sb2));
}

int lstr_init_from_fd(lstr_t *dst, int fd, int prot, int flags)
{
    struct stat st;

    if (unlikely(fstat(fd, &st)) < 0) {
        return -2;
    }

    if (st.st_size <= 0) {
        SB_8k(sb);

        if (sb_read_fd(&sb, fd) < 0) {
            return -3;
        }

        *dst = LSTR_EMPTY_V;
        if (sb.len == 0) {
            return 0;
        }
        lstr_transfer_sb(dst, &sb, false);
        return 0;
    }

    if (st.st_size == 0) {
        *dst = LSTR_EMPTY_V;
        return 0;
    }

    if (st.st_size > INT_MAX) {
        errno = ERANGE;
        return -3;
    }

    *dst = lstr_init_(mmap(NULL, st.st_size, prot, flags, fd, 0),
                      st.st_size, MEM_MMAP);

    if (dst->v == MAP_FAILED) {
        *dst = LSTR_NULL_V;
        return -3;
    }
    return 0;
}

int lstr_init_from_file(lstr_t *dst, const char *path, int prot, int flags)
{
    int fd_flags = 0;
    int fd = -1;
    int ret = 0;

    if (flags & MAP_ANONYMOUS) {
        assert (false);
        errno = EINVAL;
        return -1;
    }
    if (prot & PROT_READ) {
        if (prot & PROT_WRITE) {
            fd_flags = O_RDWR;
        } else {
            fd_flags = O_RDONLY;
        }
    } else
    if (prot & PROT_WRITE) {
        fd_flags = O_WRONLY;
    } else {
        assert (false);
        *dst = LSTR_NULL_V;
        errno = EINVAL;
        return -1;
    }

    fd = RETHROW(open(path, fd_flags));
    ret = lstr_init_from_fd(dst, fd, prot, flags);
    PROTECT_ERRNO(p_close(&fd));
    return ret;
}

/* }}} */
/* {{{ Transfer & static pool */

void lstr_transfer_sb(lstr_t *dst, sb_t *sb, bool keep_pool)
{
    if (keep_pool) {
        mem_pool_t *mp = mp_ipool(sb->mp);

        if ((mp_ipool(mp)->mem_pool & MEM_BY_FRAME)) {
            if (sb->skip) {
                memmove(sb->data - sb->skip, sb->data, sb->len + 1);
            }
        }
        mp_lstr_copy_(mp, dst, sb->data, sb->len);
        sb_init(sb);
    } else {
        lstr_wipe(dst);
        dst->v = sb_detach(sb, &dst->len);
        dst->mem_pool = MEM_LIBC;
    }
}


/* }}} */
/* {{{ Comparisons */

int lstr_ascii_icmp(const lstr_t s1, const lstr_t s2)
{
    int min = MIN(s1.len, s2.len);

    for (int i = 0; i < min; i++) {
        int a = tolower((unsigned char)s1.s[i]);
        int b = tolower((unsigned char)s2.s[i]);

        if (a != b) {
            return CMP(a, b);
        }
    }

    return CMP(s1.len, s2.len);
}

bool lstr_ascii_iequal(const lstr_t s1, const lstr_t s2)
{
    if (s1.len != s2.len) {
        return false;
    }
    for (int i = 0; i < s1.len; i++) {
        if (tolower((unsigned char)s1.s[i]) != tolower((unsigned char)s2.s[i]))
        {
            return false;
        }
    }
    return true;
}

bool lstr_match_ctype(lstr_t s, const ctype_desc_t *d)
{
    for (int i = 0; i < s.len; i++) {
        if (!ctype_desc_contains(d, (unsigned char)s.s[i])) {
            return false;
        }
    }
    return true;
}

int lstr_dlevenshtein(const lstr_t cs1, const lstr_t cs2, int max_dist)
{
    t_scope;
    lstr_t s1 = cs1;
    lstr_t s2 = cs2;
    int *cur, *prev, *prev2;

    if (s2.len > s1.len) {
        SWAP(lstr_t, s1, s2);
    }
    if (max_dist < 0) {
        max_dist = INT32_MAX;
    } else {
        THROW_ERR_IF(s1.len - s2.len > max_dist);
    }
    if (unlikely(!s2.len)) {
        return (s1.len <= max_dist) ? s1.len : -1;
    }

    cur   = t_new_raw(int, 3 * (s2.len + 1));
    prev  = cur + 1 * (s2.len + 1);
    prev2 = cur + 2 * (s2.len + 1);

    for (int j = 0; j <= s2.len; j++) {
        cur[j] = j;
    }

    for (int i = 0; i < s1.len; i++) {
        int  min_dist;
        int *tmp = prev2;

        prev2 = prev;
        prev  = cur;
        cur   = tmp;

        cur[0] = min_dist = i + 1;

        for (int j = 0; j < s2.len; j++) {
            int cost              = (s1.s[i] == s2.s[j]) ? 0 : 1;
            int deletion_cost     = prev[j + 1] + 1;
            int insertion_cost    =  cur[j    ] + 1;
            int substitution_cost = prev[j    ] + cost;

            cur[j + 1] = MIN3(deletion_cost, insertion_cost,
                              substitution_cost);

            if (i > 0 && j > 0
            &&  (s1.s[i    ] == s2.s[j - 1])
            &&  (s1.s[i - 1] == s2.s[j    ]))
            {
                int transposition_cost = prev2[j - 1] + cost;

                cur[j + 1] = MIN(cur[j + 1], transposition_cost);
            }

            min_dist = MIN(min_dist, cur[j + 1]);
        }

        THROW_ERR_IF(min_dist > max_dist);
    }

    return cur[s2.len];
}

lstr_t lstr_utf8_truncate(lstr_t s, int char_len)
{
    int pos = 0;

    while (char_len > 0 && pos < s.len) {
        if (utf8_ngetc_at(s.s, s.len, &pos) < 0) {
            return LSTR_NULL_V;
        }
        char_len--;
    }
    return LSTR_INIT_V(s.s, pos);
}

/* }}} */
/* {{{ Conversions */

void lstr_ascii_tolower(lstr_t *s)
{
    for (int i = 0; i < s->len; i++) {
        s->v[i] = tolower((unsigned char)s->v[i]);
    }
}

void lstr_ascii_toupper(lstr_t *s)
{
    for (int i = 0; i < s->len; i++) {
        s->v[i] = toupper((unsigned char)s->v[i]);
    }
}

void lstr_ascii_reverse(lstr_t *s)
{
    for (int i = 0; i < s->len / 2; i++) {
        SWAP(char, s->v[i], s->v[s->len - i - 1]);
    }
}

int lstr_to_int(lstr_t lstr, int *out)
{
    int         tmp = errno;
    const byte *endp;

    lstr = lstr_rtrim(lstr);

    errno = 0;
    *out = memtoip(lstr.s, lstr.len, &endp);

    THROW_ERR_IF(errno);
    if (endp != (const byte *)lstr.s + lstr.len) {
        errno = EINVAL;
        return -1;
    }

    errno = tmp;

    return 0;
}

int lstr_to_int64(lstr_t lstr, int64_t *out)
{
    int         tmp = errno;
    const byte *endp;

    lstr = lstr_rtrim(lstr);

    errno = 0;
    *out = memtollp(lstr.s, lstr.len, &endp);

    THROW_ERR_IF(errno);
    if (endp != (const byte *)lstr.s + lstr.len) {
        errno = EINVAL;
        return -1;
    }

    errno = tmp;

    return 0;
}

int lstr_to_uint64(lstr_t lstr, uint64_t *out)
{
    int         tmp = errno;
    const byte *endp;

    lstr = lstr_trim(lstr);

    errno = 0;
    *out = memtoullp(lstr.s, lstr.len, &endp);

    THROW_ERR_IF(errno);
    if (endp != (const byte *)lstr.s + lstr.len) {
        errno = EINVAL;
        return -1;
    }

    errno = tmp;

    return 0;
}

int lstr_to_uint(lstr_t lstr, uint32_t *out)
{
    uint64_t u64;

    RETHROW(lstr_to_uint64(lstr, &u64));

    if (u64 > UINT32_MAX) {
        errno = ERANGE;
        return -1;
    }

    *out = u64;
    return 0;
}

int lstr_to_double(lstr_t lstr, double *out)
{
    int         tmp = errno;
    const byte *endp;

    lstr = lstr_rtrim(lstr);

    errno = 0;
    *out = memtod(lstr.s, lstr.len, &endp);

    THROW_ERR_IF(errno);
    if (endp != (const byte *)lstr.s + lstr.len) {
        errno = EINVAL;
        return -1;
    }

    errno = tmp;

    return 0;
}

lstr_t t_lstr_hexdecode(lstr_t lstr)
{
    char *s;
    int len;

    len = lstr.len / 2;
    s   = t_new_raw(char, len + 1);

    if (strconv_hexdecode(s, len, lstr.s, lstr.len) < 0) {
        return LSTR_NULL_V;
    }

    s[len] = '\0';
    return LSTR_INIT_V(s, len);
}

lstr_t t_lstr_hexencode(lstr_t lstr)
{
    char *s;
    int len;

    len = lstr.len * 2;
    s   = t_new_raw(char, len + 1);

    if (strconv_hexencode(s, len + 1, lstr.s, lstr.len) < 0) {
        return LSTR_NULL_V;
    }

    return LSTR_INIT_V(s, len);
}

lstr_t lstr_trim_pkcs7_padding(lstr_t padded)
{
    int nb_padding_bytes;

    if (padded.len <= 0
    ||  padded.len % 8 != 0)
    {
        return LSTR_NULL_V;
    }

    nb_padding_bytes = padded.s[padded.len - 1];
    if (nb_padding_bytes < 1 || nb_padding_bytes > 8) {
        return LSTR_NULL_V;
    }

    padded.len -= nb_padding_bytes;
    if (!expect(padded.len >= 0)) {
        return LSTR_NULL_V;
    }
    return padded;
}

/* }}} */
/* {{{ SQL LIKE pattern matching */

/* XXX: This is copied from QDB, so that other daemons can use the
 * functionality without depending on QDB.
 *
 * XXX The behavior *should not be modified*.
 */

/* This function need only be called if c is in [0xC2...0xF4].
 * It completes the decoding of the current UTF-8 encoded code point
 * from a pstream.
 * - embedded '\0' are handled as normal characters
 * - on correctly encoded UTF-8 stream, it returns the code point and
 *   moves the pstream to the next position
 * - on invalid UTF-8 sequences, it leaves the pstream unchanged and
 * returns the initial byte value.
 */
static inline int ps_utf8_complete(int c, pstream_t *ps)
{
    int c1, c2, c3;

    switch (c) {
        /* 00...7F: US-ASCII */
        /* 80...BF: Non UTF-8 leading byte */
        /* C0...C1: Non canonical 2 byte UTF-8 encoding */
      case 0xC2 ... 0xDF:
        /* 2 byte UTF-8 sequence */
        if (ps_has(ps, 1)
        &&  (unsigned)(c1 = ps->b[0] - 0x80) < 0x40)
        {
            c = ((c & 0x3F) << 6) + c1;
            ps->b += 1;
        }
        break;

      case 0xE0 ... 0xEF:
        /* 3 byte UTF-8 sequence */
        if (ps_has(ps, 2)
        &&  (unsigned)(c1 = ps->b[0] - 0x80) < 0x40
        &&  (unsigned)(c2 = ps->b[1] - 0x80) < 0x40)
        {
            c = ((c & 0x3F) << 12) + (c1 << 6) + c2;
            ps->b += 2;
        }
        break;

      case 0xF0 ... 0xF4:
        /* 3 byte UTF-8 sequence */
        if (ps_has(ps, 3)
        &&  (unsigned)(c1 = ps->b[0] - 0x80) < 0x40
        &&  (unsigned)(c2 = ps->b[1] - 0x80) < 0x40
        &&  (unsigned)(c3 = ps->b[2] - 0x80) < 0x40)
        {
            c = ((c & 0x3F) << 18) + (c1 << 12) + (c2 << 6) + c3;
            ps->b += 3;
        }
        break;
        /* F5..F7: Start of a 4-byte sequence, Restricted by RFC 3629 */
        /* F8..FB: Start of a 5-byte sequence, Restricted by RFC 3629 */
        /* FC..FD: Start of a 6-byte sequence, Restricted by RFC 3629 */
        /* FE..FF: Invalid, non UTF-8 */
    }

    return c;
}

#define COLLATE_MASK      0xffff
#define COLLATE_SHIFT(c)  ((unsigned)(c) >> 16)

/* XXX: extracted from QDB (was named qdb_strlike), Do not change behavior! */
static bool ps_is_like(pstream_t ps, pstream_t pattern)
{
    pstream_t pattern0;
    int c1, c10, c11, c2, c20, c21;

    for (;;) {
        if (ps_done(&pattern)) {
            return ps_done(&ps);
        }
        c1 = __ps_getc(&pattern);
        if (c1 == '_') {
            if (ps_done(&ps)) {
                return false;
            }
            c2 = __ps_getc(&ps);
            if (c2 >= 0xC2) {
                ps_utf8_complete(c2, &ps);
            }
            continue;
        }
        if (c1 == '%') {
            for (;;) {
                if (ps_done(&pattern)) {
                    return true;
                }

                pattern0 = pattern;

                /* Check for non canonical pattern */
                c1 = __ps_getc(&pattern);
                if (c1 == '_') {
                    if (ps_done(&ps)) {
                        return false;
                    }
                    ps_utf8_complete(__ps_getc(&ps), &ps);
                    continue;
                }
                if (c1 == '%') {
                    continue;
                }
                if (c1 == '\\' && !ps_done(&pattern)) {
                    c1 = __ps_getc(&pattern);
                }

                c1 = ps_utf8_complete(c1, &pattern);
                c10 = c1;
                c11 = 0;
                if (c1 < countof(__str_unicode_general_ci)) {
                    int cc1 = __str_unicode_general_ci[c1];
                    c10 = cc1 & COLLATE_MASK;
                    c11 = COLLATE_SHIFT(cc1);
                }

                /* Simplistic recursive matcher */
                for (;;) {
                    pstream_t ps0 = ps;

                    if (ps_done(&ps)) {
                        return false;
                    }
                    c2 = __ps_getc(&ps);
                    if (c2 >= 0xC2) {
                        c2 = ps_utf8_complete(c2, &ps);
                    }
                    c20 = c2;
                    c21 = 0;
                    if (c2 < countof(__str_unicode_general_ci)) {
                        int cc2 = __str_unicode_general_ci[c2];
                        c20 = cc2 & COLLATE_MASK;
                        c21 = COLLATE_SHIFT(cc2);
                    }
                    if (c10 != c20) {
                        continue;
                    }
                    /* Handle dual collation */
                    if (c11 != c21) {
                        /* identical leading collation chars, but
                         * different dual collation: recurse
                         * without skipping */
                        if (ps_is_like(ps0, pattern0)) {
                            return true;
                        }
                        continue;
                    }
                    /* both large, single or dual and identical */
                    if (ps_is_like(ps, pattern)) {
                        return true;
                    }
                }
            }
        }
        if (c1 == '\\' && !ps_done(&pattern)) {
            c1 = __ps_getc(&pattern);
        }
        c1 = ps_utf8_complete(c1, &pattern);
        if (ps_done(&ps)) {
            return false;
        }
        c2 = __ps_getc(&ps);
        if (c2 >= 0xC2) {
            c2 = ps_utf8_complete(c2, &ps);
        }
        if (c1 == c2) {
            continue;
        }
        if ((c1 | c2) >= countof(__str_unicode_general_ci)) {
            /* large characters require exact match */
            break;
        }
        c1 = __str_unicode_general_ci[c1];
        c2 = __str_unicode_general_ci[c2];

      again:
        if ((c1 & COLLATE_MASK) != (c2 & COLLATE_MASK)) {
            break;
        }
        /* Handle dual collation */
        c1 = COLLATE_SHIFT(c1);
        c2 = COLLATE_SHIFT(c2);
        if ((c1 | c2) == 0) {
            /* both collation chars are single and identical */
            continue;
        }
        if (c1 == 0) {
            /* c2 is non zero */
            if (ps_done(&pattern)) {
                break;
            }
            c1 = __ps_getc(&pattern);
            if (c1 == '_' || c1 == '%' || c1 == '\\') {
                /* wildcards must fall on character boundaries */
                break;
            }
            c1 = ps_utf8_complete(c1, &pattern);
            if (c1 >= countof(__str_unicode_general_ci)) {
                /* large character cannot match c2 */
                break;
            }
            c1 = __str_unicode_general_ci[c1];
            goto again;
        } else
        if (c2 == 0) {
            /* c1 is non zero */
            if (ps_done(&ps)) {
                break;
            }
            c2 = ps_utf8_complete(__ps_getc(&ps), &ps);
            if (c2 >= countof(__str_unicode_general_ci)) {
                /* large character cannot match c1 */
                break;
            }
            c2 = __str_unicode_general_ci[c2];
            goto again;
        } else
        if (c1 == c2) {
            /* both collation chars are dual and identical */
            continue;
        } else {
            /* both are dual and different */
            break;
        }
    }
    return false;
}

#undef COLLATE_MASK
#undef COLLATE_SHIFT

bool lstr_utf8_is_ilike(const lstr_t s, const lstr_t pattern)
{
    return ps_is_like(ps_initlstr(&s), ps_initlstr(&pattern));
}

/* }}} */
