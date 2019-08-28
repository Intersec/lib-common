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

#include <math.h>

#include "net.h"
#include "unix.h"

const char __sb_slop[1];

char *sb_detach(sb_t *sb, int *len)
{
    char *s;

    if (len) {
        *len = sb->len;
    }
    if (mp_ipool(sb->mp) == &mem_pool_libc && sb->data != __sb_slop) {
        if (sb->skip) {
            memmove(sb->data - sb->skip, sb->data, sb->len + 1);
        }
        s = sb->data - sb->skip;
        sb_init(sb);
    } else {
        s = p_dupz(sb->data, sb->len);
        sb_reset(sb);
    }
    return s;
}

void sb_reset_keep_mem(sb_t *sb)
{
    sb_init_full(sb, sb->data - sb->skip, 0, sb->size + sb->skip, sb->mp);
}

static void __sb_reset(sb_t *sb, int threshold)
{
    mem_pool_t *mp = mp_ipool(sb->mp);
    char *ptr = sb->data - sb->skip;

    if (!(mp->mem_pool & MEM_BY_FRAME) && sb->skip + sb->size > threshold) {
        if (ptr != __sb_slop) {
            mp_delete(mp, &ptr);
        }
        sb_init_full(sb, (char *)__sb_slop, 0, 1, sb->mp);
    } else {
        sb_reset_keep_mem(sb);
    }
}

void sb_reset(sb_t *sb)
{
    __sb_reset(sb, 128 << 10);
}

void sb_wipe(sb_t *sb)
{
    __sb_reset(sb, 0);
}

/*
 * this function is meant to rewind any change on a sb in a function doing
 * repetitive appends that may fail.
 *
 * It cannot rewind a sb where anything has been skipped between the store and
 * the rewind. It assumes only appends have been performed.
 *
 */
int __sb_rewind_adds(sb_t *sb, const sb_t *orig)
{
    if (orig->mp != sb->mp) {
        sb_t tmp = *sb;
        int save_errno = errno;

        if (orig->skip) {
            sb_init_full(sb, orig->data - orig->skip, orig->len,
                         orig->size + orig->skip, orig->mp);
            memcpy(sb->data, tmp.data, orig->len);
        } else {
            *sb = *orig;
            __sb_fixlen(sb, orig->len);
        }
        mp_ifree(tmp.mp, tmp.data - tmp.skip);
        errno = save_errno;
    } else {
        __sb_fixlen(sb, orig->len);
    }
    return -1;
}

static void sb_destroy_skip(sb_t *sb)
{
    if (sb->data == __sb_slop || sb->skip == 0) {
        return;
    }

    memmove(sb->data - sb->skip, sb->data, sb->len + 1);
    sb->data -= sb->skip;
    sb->size += sb->skip;
    sb->skip  = 0;
}

void __sb_optimize(sb_t *sb, size_t len)
{
    mem_pool_t *mp = mp_ipool(sb->mp);
    size_t sz = p_alloc_nr(len + 1);
    char *buf;

    if (len == 0) {
        sb_reset(sb);
        return;
    }
    if ((mp->mem_pool & MEM_BY_FRAME)) {
        return;
    }
    buf = mp_new_raw(mp, char, sz);
    p_copy(buf, sb->data, sb->len + 1);
    mp_ifree(mp, sb->data - sb->skip);
    sb_init_full(sb, buf, sb->len, sz, mp);
}

void __sb_grow(sb_t *sb, int extra)
{
    mem_pool_t *mp = mp_ipool(sb->mp);
    int newlen = sb->len + extra;
    int newsz;

    if (unlikely(newlen < 0)) {
        e_panic("trying to allocate insane amount of memory");
    }

    /* if data fits and skip is worth it, shift it left */
    /* most of our pool have expensive reallocs wrt a typical memcpy,
     * and optimize the last realloc so we don't want to alloc and free
     */
    if (newlen < sb->skip + sb->size
    &&  (sb->skip > sb->size / 4 || !(mp->mem_pool & MEM_EFFICIENT_REALLOC)))
    {
        sb_destroy_skip(sb);
        return;
    }

    newsz = p_alloc_nr(sb->size + sb->skip);
    if (newsz < newlen + 1) {
        newsz = newlen + 1;
    }
    /* TODO: avoid memmove + memcpy in case of a fallback */
    sb_destroy_skip(sb);
    if (sb->data == __sb_slop) {
        sb->data = mp_new_raw(sb->mp, char, newsz);
        sb->data[0] = '\0';
    } else {
        sb->data = mp_irealloc_fallback(&sb->mp, sb->data, sb->len + 1, newsz,
                                        1, MEM_RAW);
    }
    sb->size = newsz;
}

char *__sb_splice(sb_t *sb, int pos, int rm_len, int insert_len)
{
    assert (pos >= 0 && rm_len >= 0 && insert_len >= 0);
    assert (pos <= sb->len && pos + rm_len <= sb->len);

    if (rm_len >= insert_len) {
        /* More data to suppress than to insert, move the tail of the buffer
         * to the left. */
        p_move2(sb->data, pos + insert_len, pos + rm_len,
                sb->len - pos - rm_len);
        __sb_fixlen(sb, sb->len + insert_len - rm_len);
    } else
    if (rm_len + sb->skip >= insert_len) {
        /* The skip area is at least as large as the data to insert
         * (substracted from the data to remove), move the head of the buffer
         * to the left, in the skip area. */
        sb->skip -= insert_len - rm_len;
        sb->data -= insert_len - rm_len;
        sb->size += insert_len - rm_len;
        sb->len  += insert_len - rm_len;
        p_move2(sb->data, 0, insert_len - rm_len, pos);
    } else {
        /* Default case: move the tail of the buffer to the right to leave
         * some room for the data to insert. */
        sb_grow(sb, insert_len - rm_len);
        p_move2(sb->data, pos + insert_len, pos + rm_len,
                sb->len - pos - rm_len);
        __sb_fixlen(sb, sb->len + insert_len - rm_len);
    }
    sb_optimize(sb, 0);
    return sb->data + pos;
}

/**************************************************************************/
/* str/mem-functions wrappers                                             */
/**************************************************************************/

int sb_search(const sb_t *sb, int pos, const void *what, int wlen)
{
    const char *p = memmem(sb->data + pos, sb->len - pos, what, wlen);
    return p ? p - sb->data : -1;
}


/**************************************************************************/
/* printf function                                                        */
/**************************************************************************/

int sb_addvf(sb_t *sb, const char *fmt, va_list ap)
{
    va_list ap2;
    int len;

    len = sb_avail(sb);
    if (len != 0) {
        len = len + 1;
    }

    va_copy(ap2, ap);
    len = ivsnprintf(sb_end(sb), len, fmt, ap2);
    va_end(ap2);

    if (len <= sb_avail(sb)) {
        __sb_fixlen(sb, sb->len + len);
    } else {
        ivsnprintf(sb_growlen(sb, len), len + 1, fmt, ap);
    }
    return len;
}

int sb_addf(sb_t *sb, const char *fmt, ...)
{
    int res;
    va_list args;

    va_start(args, fmt);
    res = sb_addvf(sb, fmt, args);
    va_end(args);

    return res;
}

int sb_prependvf(sb_t *sb, const char *fmt, va_list ap)
{
    char buf[BUFSIZ];
    va_list ap2;
    int len;

    va_copy(ap2, ap);
    len = ivsnprintf(buf, BUFSIZ, fmt, ap2);
    va_end(ap2);

    if (len < BUFSIZ) {
        sb_splice(sb, 0, 0, buf, len);
    } else {
        char *tbuf = p_new(char, len + 1);

        ivsnprintf(tbuf, len + 1, fmt, ap);
        sb_splice(sb, 0, 0, tbuf, len);
        p_delete(&tbuf);
    }
    return len;
}

int sb_prependf(sb_t *sb, const char *fmt, ...)
{
    int res;
    va_list args;

    va_start(args, fmt);
    res = sb_prependvf(sb, fmt, args);
    va_end(args);

    return res;
}

static void sb_add_ps_int_fmt(sb_t *out, pstream_t ps, int thousand_sep)
{
    if (thousand_sep < 0) {
        sb_add(out, ps.p, ps_len(&ps));
        return;
    }

    while (!ps_done(&ps)) {
        int len = ps_len(&ps) % 3 ?: 3;

        sb_add(out, ps.s, len);
        __ps_skip(&ps, len);
        if (!ps_done(&ps)) {
            sb_addc(out, thousand_sep);
        }
    }
}

void sb_add_uint_fmt(sb_t *sb, uint64_t val, int thousand_sep)
{
    char buf[21];
    int len = snprintf(buf, sizeof(buf), "%ju", val);

    sb_add_ps_int_fmt(sb, ps_init(buf, len), thousand_sep);
}

void sb_add_int_fmt(sb_t *sb, int64_t val, int thousand_sep)
{
    if (val < 0) {
        sb_addc(sb, '-');
        val *= -1;
    }
    sb_add_uint_fmt(sb, val, thousand_sep);
}

void sb_add_double_fmt(sb_t *sb, double val, uint8_t nb_max_decimals,
                       int dec_sep, int thousand_sep)
{
    char buf[BUFSIZ];
    pstream_t ps, integer_part = ps_init(NULL, 0);

    if (isnan(val) || isinf(val)) {
        sb_addf(sb, "%f", val);
        return;
    }

    ps = ps_init(buf, snprintf(buf, sizeof(buf), "%.*f",
                               MAX(1, nb_max_decimals), val));

    /* Sign */
    if (ps_skipc(&ps, '-') == 0) {
        sb_addc(sb, '-');
    }

    /* Integer part  */
    ps_get_ps_chr_and_skip(&ps, '.', &integer_part);
    sb_add_ps_int_fmt(sb, integer_part, thousand_sep);

    /* Decimal part */
    if (nb_max_decimals > 0) {
        pstream_t decimal_part = ps;

        while (ps_shrinkc(&decimal_part, '0') == 0);

        if (ps_len(&decimal_part)) {
            sb_addc(sb, dec_sep);
            sb_add(sb, ps.s, ps_len(&ps));
        }
    }
}

void sb_add_filtered(sb_t *sb, lstr_t s, const ctype_desc_t *d)
{
    return sb_add_sanitized(sb, s, d, -1);
}

void sb_add_filtered_out(sb_t *sb, lstr_t s, const ctype_desc_t *d)
{
    return sb_add_sanitized_out(sb, s, d, -1);
}

void sb_add_sanitized(sb_t *sb, lstr_t s, const ctype_desc_t *d, int c)
{
    pstream_t w, r = ps_initlstr(&s);

    while (!ps_done(&r)) {
        w = ps_get_span(&r, d);
        sb_add(sb, w.s, ps_len(&w));
        if (ps_skip_cspan(&r, d) > 0 && c > -1) {
            sb_addc(sb, c);
        }
    }
}

void sb_add_sanitized_out(sb_t *sb, lstr_t s, const ctype_desc_t *d, int c)
{
    pstream_t w, r = ps_initlstr(&s);

    while (!ps_done(&r)) {
        w = ps_get_cspan(&r, d);
        sb_add(sb, w.s, ps_len(&w));
        if (ps_skip_span(&r, d) > 0 && c > -1) {
            sb_addc(sb, c);
        }
    }
}

void _sb_add_duration_ms(sb_t *sb, uint64_t ms, bool print_ms)
{
    uint8_t nb_prints = 0;
    static const uint32_t units[] = {
        24 * 60 * 60 * 1000, /* day */
             60 * 60 * 1000, /* hour */
                  60 * 1000, /* minute */
                       1000, /* second */
                          1, /* millisecond */
    };

    if (!ms) {
        sb_adds(sb, "0s");
        return;
    }

    for (int j = 0; j < countof(units) - 1; j++) {
        if (ms >= units[j]) {
            /* only units j and j + 1 will be printed, round the remainder to
             * the nearest value in unit j + 1 */
            ms = ROUND(ms + units[j + 1] / 2, units[j + 1]);
            break;
        }
    }

#define SB_ADD_DUR(_unit, i)                                                 \
    do {                                                                     \
        uint32_t nb_ms_unit = units[i];                                      \
                                                                             \
        if ((ms) >= nb_ms_unit || nb_prints == 1) {                          \
            if (nb_prints) {                                                 \
                sb_addc(sb, ' ');                                            \
            }                                                                \
            sb_addf(sb, "%lu" _unit, ms / (nb_ms_unit));                     \
            ms %= (nb_ms_unit);                                              \
            nb_prints++;                                                     \
        }                                                                    \
    } while (0)

    SB_ADD_DUR(     "d", 0);
    SB_ADD_DUR(     "h", 1);
    SB_ADD_DUR(     "m", 2);
    SB_ADD_DUR(     "s", 3);
    if (print_ms) {
        SB_ADD_DUR("ms", 4);
    }

#undef SB_ADD_DUR
}

void sb_add_pkcs7_8_bytes_padding(sb_t *sb)
{
    int nb_additional_bytes = 8 - (sb->len % 8);

    assert (1 <= nb_additional_bytes && nb_additional_bytes <= 8);
    sb_addnc(sb, nb_additional_bytes, nb_additional_bytes);
}

/**************************************************************************/
/* FILE *                                                                 */
/**************************************************************************/

int sb_getline(sb_t *sb, FILE *f)
{
    int64_t start = ftell(f);
    sb_t orig = *sb;

    do {
        int64_t end;
        char *buf = sb_grow(sb, BUFSIZ);

        if (!fgets(buf, sb_avail(sb) + 1, f)) {
            if (ferror(f))
                return __sb_rewind_adds(sb, &orig);
            break;
        }

        end = ftell(f);
        if (start != -1 && end != -1) {
            sb->len += (end - start);
        } else {
            sb->len += strlen(buf);
        }
        start = end;
    } while (sb->data[sb->len - 1] != '\n');

    if (likely(sb->len - orig.len > 0)
    &&  unlikely(sb->data[sb->len - 1] != '\n'))
    {
        sb_addc(sb, '\n');
    }

    return sb->len - orig.len;
}

/* OG: returns the number of elements actually appended to the sb,
 * -1 if error
 */
int sb_fread(sb_t *sb, int size, int nmemb, FILE *f)
{
    sb_t orig = *sb;
    int   res = size * nmemb;
    char *buf = sb_grow(sb, size * nmemb);

    if (unlikely(((long long)size * (long long)nmemb) != res))
        e_panic("trying to allocate insane amount of memory");

    res = fread(buf, size, nmemb, f);
    if (res < 0)
        return __sb_rewind_adds(sb, &orig);

    __sb_fixlen(sb, sb->len + res * size);
    return res;
}

/* Return the number of bytes appended to the sb, negative value
 * indicates an error.
 * OG: this function insists on reading a complete file.  If the file
 * cannot be read completely, no data is kept in the sb and an error
 * is returned.
 */
int sb_read_fd(sb_t *sb, int fd)
{
    sb_t orig = *sb;
    struct stat st;
    char *buf;
    int res;

    if (fstat(fd, &st) < 0 || st.st_size <= 0) {
        for (;;) {
            res = sb_read(sb, fd, 0);

            if (res < 0) {
                return __sb_rewind_adds(sb, &orig);
            }
            if (res == 0) {
                return sb->len - orig.len;
            }
        }
    }

    if (st.st_size > INT_MAX) {
        errno = ENOMEM;
        return -1;
    }

    res = st.st_size;
    buf = sb_growlen(sb, res);
    if (xread(fd, buf, res) < 0) {
        return __sb_rewind_adds(sb, &orig);
    }

    return res;
}

int sb_read_file(sb_t *sb, const char *filename)
{
    int fd, res;

    fd = RETHROW(open(filename, O_RDONLY));
    res = sb_read_fd(sb, fd);
    PROTECT_ERRNO(p_close(&fd));
    return res;
}

int sb_write_file(const sb_t *sb, const char *filename)
{
    return xwrite_file(filename, sb->data, sb->len);
}

int sb_append_to_file(const sb_t *sb, const char *filename)
{
    return xappend_to_file(filename, sb->data, sb->len);
}

/**************************************************************************/
/* fd and sockets                                                         */
/**************************************************************************/

int sb_read(sb_t *sb, int fd, int hint)
{
    sb_t orig = *sb;
    char *buf;
    int   res;

    buf = sb_grow(sb, hint <= 0 ? BUFSIZ : hint);
    res = read(fd, buf, sb_avail(sb));
    if (res < 0)
        return __sb_rewind_adds(sb, &orig);
    __sb_fixlen(sb, sb->len + res);
    return res;
}

int sb_recvfrom(sb_t *sb, int fd, int hint, int flags,
                struct sockaddr *addr, socklen_t *alen)
{
    sb_t orig = *sb;
    char *buf;
    int   res;

    buf = sb_grow(sb, hint <= 0 ? BUFSIZ : hint);
    res = recvfrom(fd, buf, sb_avail(sb), flags, addr, alen);
    if (res < 0)
        return __sb_rewind_adds(sb, &orig);
    __sb_fixlen(sb, sb->len + res);
    return res;
}

int sb_recv(sb_t *sb, int fd, int hint, int flags)
{
    return sb_recvfrom(sb, fd, hint, flags, NULL, NULL);
}
