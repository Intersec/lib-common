/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_STR_STREAM_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_STR_STREAM_H

/*
 * pstream_t's are basically a pointer and a length
 * and are meant to simplify parsing a lot
 *
 * Those structures are not meant to be allocated ever.
 */

/*
 * When you write a pstream function to parse a type_t, here is how you should
 * proceed:
 *
 * Case 1:
 * ~~~~~~
 *   When the size of the data is _easy_ to know before hand (we're reading a
 *   fixed amount of bytes for example) :
 *
 *   - depending on the fact that `type_t` is scalar or complex, provide:
 *     type_t __ps_get_type(pstream_t *ps, ...);
 *     void __ps_get_type(pstream_t *ps, type_t *res, ...);
 *
 *     This function shall assume that there is space to read the `type_t`.
 *
 *   - provide then a safe ps_get_type function looking like this:
 *     int ps_get_type(pstream_t *ps, type_t *res, ...) {
 *         if (unlikely(!ps_has(ps, EASY_LEN_TO_COMPUTE)))
 *             return -1;
 *          res = __ps_get_type(ps, ...);
 *          return 0;
 *     }
 *
 * Case 2:
 * ~~~~~~
 *   When the size of the data is hard to know, or too complex, you may only
 *   provide the safe interface. For example, ps_gets, we have to do a memchr
 *   to know the size, and it almost gives the result, you don't want to have
 *   both a __ps_gets and a ps_gets it's absurd.
 *
 *
 * In any case, if the task to perform is complex, do not inline the function,
 * and providing the unsafe version becomes useless too. The goal of the
 * unsafe function is to be able to inline the sizes checks.
 *
 */

typedef struct pstream_t {
    union {
        const void * nullable p;
        const char * nullable s;
        const byte * nullable b;
    };
    union {
        const void * nullable p_end;
        const char * nullable s_end;
        const byte * nullable b_end;
    };
} pstream_t;


/****************************************************************************/
/* init, checking constraints, skipping                                     */
/****************************************************************************/

#if 0
#define PS_WANT(c)  \
    do {                                                                    \
        if (unlikely(!(c))) {                                               \
            e_trace(0, "str-stream error on: %s != true", #c);              \
            return -1;                                                      \
        }                                                                   \
    } while (0)
#define PS_CHECK(c) \
    ({ typeof(c) __res = (c);                                               \
       if (unlikely(__res < 0)) {                                           \
           e_trace(0, "str-stream error on: %s < 0", #c);                   \
           return __res;                                                    \
       }                                                                    \
       __res;                                                               \
    })
#else
#define PS_WANT(c)   do { if (unlikely(!(c)))    return -1; } while (0)
#define PS_CHECK(c)  RETHROW(c)
#endif

static inline pstream_t ps_initptr(const void * nullable s,
                                   const void * nullable p)
{
    return (pstream_t){ { s }, { p } };
}
static inline pstream_t ps_init(const void * nullable s, size_t len)
{
    return ps_initptr(s, (const byte *)s + len);
}
static inline pstream_t ps_initstr(const char * nonnull s)
{
    return ps_initptr(s, s + strlen(s));
}
static inline pstream_t ps_initlstr(const lstr_t * nonnull s)
{
    return ps_init(s->s, s->len);
}
static inline pstream_t ps_initsb(const sb_t * nonnull sb)
{
    return ps_init(sb->data, sb->len);
}

static inline size_t ps_len(const pstream_t * nonnull ps)
{
    return ps->s_end - ps->s;
}
static inline const void * nullable ps_end(const pstream_t * nonnull ps)
{
    return ps->p_end;
}

static inline bool ps_done(const pstream_t * nonnull ps)
{
    return ps->p >= ps->p_end;
}
static inline bool ps_has(const pstream_t * nonnull ps, size_t len)
{
    return (ps_done(ps) && len == 0) || (size_t)(ps->s_end - ps->s) >= len;
}
static inline bool ps_contains(const pstream_t * nonnull ps,
                               const void * nonnull p)
{
    return p >= ps->p && p <= ps->p_end;
}

static inline bool ps_is_equal(pstream_t ps1, pstream_t ps2)
{
    return ps_len(&ps1) == ps_len(&ps2) && !memcmp(ps1.b, ps2.b, ps_len(&ps1));
}
static inline int ps_cmp(pstream_t ps1, pstream_t ps2)
{
    size_t len = MIN(ps_len(&ps1), ps_len(&ps2));
    return memcmp(ps1.b, ps2.b, len) ?: CMP(ps_len(&ps1), ps_len(&ps2));
}

static inline bool ps_startswith(const pstream_t * nonnull ps,
                                 const void * nonnull data, size_t len)
{
    return ps_len(ps) >= len && !memcmp(ps->p, data, len);
}
static inline bool ps_startswithstr(const pstream_t * nonnull ps,
                                    const char * nonnull s)
{
    return ps_startswith(ps, s, strlen(s));
}
static inline bool ps_startswithlstr(const pstream_t * nonnull ps, lstr_t s)
{
    return ps_startswith(ps, s.s, s.len);
}

static inline bool ps_endswith(const pstream_t * nonnull ps,
                               const void * nonnull data, size_t len)
{
    return ps_len(ps) >= len && !memcmp(ps->b_end - len, data, len);
}
static inline bool ps_endswithstr(const pstream_t * nonnull ps,
                                  const char * nonnull s)
{
    return ps_endswith(ps, s, strlen(s));
}
static inline bool ps_endswithlstr(const pstream_t * nonnull ps, lstr_t s)
{
    return ps_endswith(ps, s.s, s.len);
}

static inline bool ps_memequal(const pstream_t * nonnull ps,
                               const void * nonnull data, size_t len)
{
    return ps_len(ps) == len && !memcmp(ps->p, data, len);
}
static inline bool ps_strequal(const pstream_t * nonnull ps,
                               const char * nonnull s)
{
    return ps_memequal(ps, s, strlen(s));
}

static inline bool ps_memcaseequal(const pstream_t * nonnull ps,
                                   const char * nonnull s, size_t len)
{
    if (ps_len(ps) != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (tolower(ps->b[i]) != tolower((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

static inline bool ps_strcaseequal(const pstream_t * nonnull ps,
                                   const char * nonnull s)
{
    return ps_memcaseequal(ps, s, strlen(s));
}


/****************************************************************************/
/* skipping/trimming helpers                                                */
/****************************************************************************/

static inline int __ps_skip(pstream_t * nonnull ps, size_t len)
{
    assert (ps_has(ps, len));
    ps->s += len;
    return 0;
}
static inline int ps_skip(pstream_t * nonnull ps, size_t len)
{
    return unlikely(!ps_has(ps, len)) ? -1 : __ps_skip(ps, len);
}
static inline int __ps_skip_upto(pstream_t * nonnull ps,
                                 const void * nonnull p)
{
    assert (ps_contains(ps, p));
    ps->p = p;
    return 0;
}
static inline int ps_skip_upto(pstream_t * nonnull ps,
                               const void * nonnull p)
{
    PS_WANT(ps_contains(ps, p));
    return __ps_skip_upto(ps, p);
}

static inline int __ps_shrink(pstream_t * nonnull ps, size_t len)
{
    assert (ps_has(ps, len));
    ps->s_end -= len;
    return 0;
}
static inline int ps_shrink(pstream_t * nonnull ps, size_t len)
{
    return unlikely(!ps_has(ps, len)) ? -1 : __ps_shrink(ps, len);
}

static inline int __ps_clip(pstream_t * nonnull ps, size_t len)
{
    assert (ps_has(ps, len));
    ps->s_end = ps->s + len;
    return 0;
}
static inline int ps_clip(pstream_t * nonnull ps, size_t len)
{
    return unlikely(!ps_has(ps, len)) ? -1 : __ps_clip(ps, len);
}
static inline int __ps_clip_at(pstream_t * nonnull ps,
                               const void * nullable p)
{
    assert (ps_contains(ps, p));
    ps->p_end = p;
    return 0;
}
static inline int ps_clip_at(pstream_t * nonnull ps, const void * nonnull p)
{
    return unlikely(!ps_contains(ps, p)) ? -1 : __ps_clip_at(ps, p);
}
static inline int ps_clip_atchr(pstream_t * nonnull ps, int c)
{
    const char *p = (const char *)memchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_clip_at(ps, p) : -1;
}
static inline int ps_clip_afterchr(pstream_t * nonnull ps, int c)
{
    const char *p = (const char *)memchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_clip_at(ps, p + 1) : -1;
}
static inline int ps_clip_atlastchr(pstream_t * nonnull ps, int c)
{
    const char *p = (const char *)memrchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_clip_at(ps, p) : -1;
}
static inline int ps_clip_afterlastchr(pstream_t * nonnull ps, int c)
{
    const char *p = (const char *)memrchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_clip_at(ps, p + 1) : -1;
}

static inline int ps_skipdata(pstream_t * nonnull ps,
                              const void * nonnull data, size_t len)
{
    PS_WANT(ps_startswith(ps, data, len));
    return __ps_skip(ps, len);
}
static inline int ps_skipstr(pstream_t * nonnull ps, const char * nonnull s)
{
    return ps_skipdata(ps, s, strlen(s));
}
static inline int ps_skiplstr(pstream_t * nonnull ps, lstr_t s)
{
    return ps_skipdata(ps, s.data, s.len);
}
static inline int ps_skip_uptochr(pstream_t * nonnull ps, int c)
{
    const void *p = memchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_skip_upto(ps, p) : -1;
}
static inline int ps_skip_afterchr(pstream_t * nonnull ps, int c)
{
    const char *p = (const char *)memchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_skip_upto(ps, p + 1) : -1;
}

static inline int ps_skip_afterlastchr(pstream_t * nonnull ps, int c)
{
    const char *p = (const char *)memrchr(ps->p, c, ps_len(ps));
    return likely(p) ? __ps_skip_upto(ps, p + 1) : -1;
}

/** \brief  Skips up to the (\a data, \a len) word
 * \return -1 if the word cannot be found
 */
static inline
int ps_skip_upto_data(pstream_t * nonnull ps, const void * nonnull data,
                      size_t len)
{
    void *mem = RETHROW_PN(memmem(ps->p, ps_len(ps), data, len));
    return __ps_skip_upto(ps, mem);
}
static inline int ps_skip_upto_str(pstream_t * nonnull ps,
                                   const char * nonnull s)
{
    return ps_skip_upto_data(ps, s, strlen(s));
}

/** \brief  Skips after the (\a data, \a len) word
 * \return -1 if the word cannot be found
 */
static inline
int ps_skip_after_data(pstream_t * nonnull ps, const void * nonnull data,
                       size_t len)
{
    void *mem = RETHROW_PN(memmem(ps->p, ps_len(ps), data, len));
    return __ps_skip_upto(ps, (char *)mem + len);
}
static inline int ps_skip_after_str(pstream_t * nonnull ps,
                                    const char * nonnull s)
{
    return ps_skip_after_data(ps, s, strlen(s));
}

/****************************************************************************/
/* extracting sub pstreams                                                  */
/****************************************************************************/
/*
 * extract means it doesn't modifies the "parent" ps
 * get means it reduces the size of the "parent" (skip or shrink)
 *
 */
static inline pstream_t __ps_extract_after(const pstream_t * nonnull ps,
                                           const void * nonnull p)
{
    assert (ps_contains(ps, p));
    return ps_initptr(p, ps->p_end);
}
static inline int ps_extract_after(pstream_t * nonnull ps,
                                   const void * nonnull p,
                                   pstream_t * nonnull out)
{
    PS_WANT(ps_contains(ps, p));
    *out = __ps_extract_after(ps, p);
    return 0;
}

static inline pstream_t __ps_get_ps_upto(pstream_t * nonnull ps,
                                         const void * nonnull p)
{
    const void *old = ps->p;
    assert (ps_contains(ps, p));
    return ps_initptr(old, ps->p = p);
}
static inline int ps_get_ps_upto(pstream_t * nonnull ps,
                                 const void * nonnull p,
                                 pstream_t * nonnull out)
{
    PS_WANT(ps_contains(ps, p));
    *out = __ps_get_ps_upto(ps, p);
    return 0;
}

static inline pstream_t __ps_get_ps(pstream_t * nonnull ps, size_t len)
{
    const void *old = ps->b;
    assert (ps_has(ps, len));
    return ps_initptr(old, ps->b += len);
}
static inline int ps_get_ps(pstream_t * nonnull ps, size_t len,
                            pstream_t * nonnull out)
{
    PS_WANT(ps_has(ps, len));
    *out = __ps_get_ps(ps, len);
    return 0;
}

static inline int ps_get_ps_chr(pstream_t * nonnull ps, int c,
                                pstream_t * nonnull out)
{
    const void *p = memchr(ps->s, c, ps_len(ps));

    PS_WANT(p);
    *out = __ps_get_ps_upto(ps, p);
    return 0;
}

static inline int ps_get_ps_chr_and_skip(pstream_t * nonnull ps, int c,
                                         pstream_t * nonnull out)
{
    const void *p = memchr(ps->s, c, ps_len(ps));

    PS_WANT(p);
    *out = __ps_get_ps_upto(ps, p);
    __ps_skip(ps, 1);
    return 0;
}

static inline int ps_get_ps_lastchr(pstream_t * nonnull ps, int c,
                                    pstream_t * nonnull out)
{
    const void *p = memrchr(ps->p, c, ps_len(ps));

    PS_WANT(p);
    *out = __ps_get_ps_upto(ps, p);
    return 0;
}

static inline int
ps_get_ps_lastchr_and_skip(pstream_t * nonnull ps, int c,
                           pstream_t * nonnull out)
{
    const void *p = memrchr(ps->p, c, ps_len(ps));

    PS_WANT(p);
    *out = __ps_get_ps_upto(ps, p);
    __ps_skip(ps, 1);
    return 0;
}


/** \brief  Returns the bytes up to the (\a d, \a len) word
 * \return -1 if the word cannot be found
 */
static inline int
ps_get_ps_upto_data(pstream_t * nonnull ps, const void * nonnull d,
                    size_t len, pstream_t * nonnull out)
{
    void *mem = RETHROW_PN(memmem(ps->p, ps_len(ps), d, len));
    *out = __ps_get_ps_upto(ps, mem);
    return 0;
}
static inline int
ps_get_ps_upto_str(pstream_t * nonnull ps, const char * nonnull s,
                   pstream_t * nonnull out)
{
    return ps_get_ps_upto_data(ps, s, strlen(s), out);
}

/** \brief  Returns the bytes up to the (\a data, \a len) word, and skip it.
 * \return -1 if the word cannot be found
 */
static inline int
ps_get_ps_upto_data_and_skip(pstream_t * nonnull ps,
                             const void * nonnull data,
                             size_t len, pstream_t * nonnull out)
{
    void *mem = RETHROW_PN(memmem(ps->p, ps_len(ps), data, len));
    *out = __ps_get_ps_upto(ps, mem);
    __ps_skip_upto(ps, (char *)mem + len);
    return 0;
}
static inline int
ps_get_ps_upto_str_and_skip(pstream_t * nonnull ps, const char * nonnull s,
                            pstream_t * nonnull out)
{
    return ps_get_ps_upto_data_and_skip(ps, s, strlen(s), out);
}

/****************************************************************************/
/* copying helpers                                                          */
/****************************************************************************/

int ps_copyv(pstream_t * nonnull ps, struct iovec * nonnull iov,
             size_t * nonnull iov_len, int * nullable flags)
    __leaf __attr_nonnull__((1, 2, 3));


/****************************************************************************/
/* string parsing helpers                                                   */
/****************************************************************************/

static inline int ps_geti(pstream_t * nonnull ps)
{
    return memtoip(ps->b, ps_len(ps), &ps->b);
}

static inline int64_t ps_getlli(pstream_t * nonnull ps)
{
    return memtollp(ps->b, ps_len(ps), &ps->b);
}

static inline int64_t ps_get_ll_ext(pstream_t * nonnull ps, int base)
{
    int64_t res;

    memtoll_ext(ps->p, ps_len(ps), &res, &ps->p, base);
    return res;
}

/** Get a unsigned integer.
 *
 * To check if the function fails, errno must be checked.
 *
 * If the pstream begins with a minus sign (white spaces are skipped), the
 * function fails and errno is set to ERANGE.
 */
static inline uint64_t ps_get_ull_ext(pstream_t * nonnull ps, int base)
{
    uint64_t res;

    memtoull_ext(ps->p, ps_len(ps), &res, &ps->p, base);
    return res;
}

static inline double ps_getd(pstream_t * nonnull ps) {
    return memtod(ps->b, ps_len(ps), &ps->b);
}

static inline int __ps_skipc(pstream_t * nonnull ps, int c)
{
    assert (ps_has(ps, 1));
    if (*ps->b == c) {
        ps->b++;
        return 0;
    }
    return -1;
}

static inline int ps_skipc(pstream_t * nonnull ps, int c)
{
    PS_WANT(ps_has(ps, 1));
    return __ps_skipc(ps, c);
}

static inline int __ps_shrinkc(pstream_t * nonnull ps, int c)
{
    assert (ps_has(ps, 1));
    if (*(ps->b_end - 1) == c) {
        ps->b_end--;
        return 0;
    }
    return -1;
}

static inline int ps_shrinkc(pstream_t * nonnull ps, int c)
{
    PS_WANT(ps_has(ps, 1));
    return __ps_shrinkc(ps, c);
}

static inline int __ps_getc(pstream_t * nonnull ps)
{
    int c = *ps->b;
    __ps_skip(ps, 1);
    return c;
}

static inline int ps_getc(pstream_t * nonnull ps)
{
    if (unlikely(!ps_has(ps, 1)))
        return EOF;
    return __ps_getc(ps);
}

static inline int ps_peekc(pstream_t ps)
{
    return ps_getc(&ps);
}

static inline int ps_getuc(pstream_t * nonnull ps)
{
    return utf8_ngetc(ps->s, ps_len(ps), &ps->s);
}

static inline int ps_peekuc(pstream_t ps)
{
    return ps_getuc(&ps);
}

static inline int __ps_hexdigit(pstream_t * nonnull ps)
{
    return hexdigit(__ps_getc(ps));
}
static inline int ps_hexdigit(pstream_t * nonnull ps)
{
    PS_WANT(ps_has(ps, 1));
    return __ps_hexdigit(ps);
}

static inline int ps_hex16(pstream_t * nonnull ps, int len,
                           uint16_t * nonnull res)
{
    const byte *b = ps->b;

    PS_WANT(len <= 4);
    PS_WANT(ps_has(ps, len));

    *res = 0;
    while (b < ps->b + len) {
        PS_WANT(ctype_desc_contains(&ctype_ishexdigit, *b));
        *res <<= 4;
        *res |= hexdigit(*b);
        b++;
    }
    return __ps_skip(ps, len);
}

static inline int ps_hexdecode(pstream_t * nonnull ps)
{
    int res;
    PS_WANT(ps_has(ps, 2));
    res = hexdecode(ps->s);
    __ps_skip(ps, 2);
    return res;
}

static inline const char * nullable ps_gets(pstream_t * nonnull ps,
                                            int * nullable len)
{
    const char *end = (const char *)memchr(ps->s, '\0', ps_len(ps));
    const char *res = ps->s;

    if (unlikely(!end))
        return NULL;
    if (len)
        *len = end - ps->s;
    __ps_skip(ps, end + 1 - ps->s);
    return res;
}

/* this function returns a lstr containing the characters of the ps until the
 * next '\0' and returns LSTR_NULL_V iff the pstream isn't null-terminated
 * XXX if you want to get the whole pstream, you should use LSTR_PS_V instead
 */
static inline lstr_t ps_get_lstr(pstream_t * nonnull ps)
{
    int len = 0;
    const char *s = ps_gets(ps, &len);
    return LSTR_INIT_V(s, len);
}

static inline int ps_skipcasedata(pstream_t * nonnull ps,
                                  const char * nonnull s, int len)
{
    PS_WANT(ps_has(ps, len));
    for (int i = 0; i < len; i++)
        PS_WANT(tolower((unsigned char)ps->s[i]) == tolower((unsigned char)s[i]));
    return __ps_skip(ps, len);
}

static inline int ps_skipcasestr(pstream_t * nonnull ps,
                                 const char * nonnull s)
{
    return ps_skipcasedata(ps, s, strlen(s));
}

static inline size_t ps_skip_span(pstream_t * nonnull ps,
                                  const ctype_desc_t * nonnull d)
{
    size_t l = 0;

    while (ps->b + l < ps->b_end && ctype_desc_contains(d, ps->b[l]))
        l++;
    ps->b += l;
    return l;
}

static inline size_t ps_skip_cspan(pstream_t * nonnull ps,
                                   const ctype_desc_t * nonnull d)
{
    size_t l = 0;

    while (ps->b + l < ps->b_end && !ctype_desc_contains(d, ps->b[l]))
        l++;
    ps->b += l;
    return l;
}

/* @func ps_get_span
 * @param[in] ps
 * @param[in] d
 * @return a sub pstream spanning on the first characters
 *         contained by d
 */
static inline pstream_t ps_get_span(pstream_t * nonnull ps,
                                    const ctype_desc_t * nonnull d)
{
    const byte *b = ps->b;

    while (b < ps->b_end && ctype_desc_contains(d, *b))
        b++;
    return __ps_get_ps_upto(ps, b);
}

/* @func ps_get_cspan
 * @param[in] ps
 * @param[in] d
 * @return a sub pstream spanning on the first characters
 *         not contained by d
 */
static inline pstream_t ps_get_cspan(pstream_t * nonnull ps,
                                     const ctype_desc_t * nonnull d)
{
    const byte *b = ps->b;

    while (b < ps->b_end && !ctype_desc_contains(d, *b))
        b++;
    return __ps_get_ps_upto(ps, b);
}

/** Check if a pstream_t contains at least one character from a ctype_desc_t.
 *
 * \param ps The pstream on which do the search.
 * \param d  The ctype_desc_t containing the characters we are looking for.
 * \return true if ps contains at least one character from d, false otherwise.
 */
static inline bool
ps_has_char_in_ctype(const pstream_t * nonnull ps,
                     const ctype_desc_t * nonnull d)
{
    for (const byte *b = ps->b; b < ps->b_end; b++) {
        if (ctype_desc_contains(d, *b)) {
            return true;
        }
    }
    return false;
}

static inline pstream_t ps_get_tok(pstream_t * nonnull ps,
                                   const ctype_desc_t * nonnull d)
{
    pstream_t out = ps_get_cspan(ps, d);
    ps_skip_span(ps, d);
    return out;
}

static inline size_t ps_ltrim(pstream_t * nonnull ps)
{
    return ps_skip_span(ps, &ctype_isspace);
}
#define ps_skipspaces ps_ltrim
static inline size_t ps_rtrim(pstream_t * nonnull ps)
{
    const uint8_t *end = ps->b_end;
    size_t res;

    while (ps->b < end && ctype_desc_contains(&ctype_isspace, end[-1]))
        end--;
    res = ps->b_end - end;
    ps->b_end = end;
    return res;
}
static inline size_t ps_trim(pstream_t * nonnull ps)
{
    return ps_ltrim(ps) + ps_rtrim(ps);
}

union qv_lstr_t;

/** Read a CSV line from the pstream.
 *
 * The CSV is parsed according to RFC 4180 with the following relaxed rules:
 *  - TEXTDATA is set to all that is neither a separator nor \r or \n. Which
 *    mean a field can contain a quotation mark.
 *  - \n alone is allowed as a line break.
 *
 * The result of the parsing is put in a vector of strings. Copies of data are
 * avoided as much as possible, which mean that unquoted field will be direct
 * references to the content of the pstream and thus will have the same
 * lifetime. Fields that must be copied (those containing escaped quotation
 * marks) are allocated in the provided memory pool (which may be NULL if you
 * want allocations on the heap).
 *
 * In case of error, the pstream is placed where it encountered the error.
 * In case of success it is placed at the beginning of the next line.
 *
 * \param[in] mp The pool on which data that must be copied is allocated (NULL
 *                to allocate data on the heap).
 * \param[in,out] ps The stream from which data is read.
 * \param[in] sep The separator (usually comma or semi-colon)
 * \param[in] quote The quoting character (usually double quote), can be -1 if
 *                   quoting is disallowed.
 * \param[out] fields A vector that will be filled with the fields.
 * \param[out] out_line A pstream which will contain the content of the read
 *                      CSV line (without the final line break).
 *                      Can be NULL if you are not interrested in this peace
 *                      of information.
 * \return -1 if the content of the pstream does not starts with a valid CSV
 *            record.
 */
int ps_get_csv_line(mem_pool_t * nullable mp, pstream_t * nonnull ps, int sep,
                    int quote, union qv_lstr_t * nonnull fields,
                    pstream_t * nullable out_line);

enum {
    PS_SPLIT_SKIP_EMPTY = 1 << 0
};

/** Split a stream based on a set of separator.
 *
 * The line is parsed and each time one of the separators is encountered,
 * a new chunk is added in the result vector. The results do not contain the
 * separator and may contain empty strings.
 *
 * Strings appended are not copied, they point to the content of the origin
 * pstream.
 *
 * \param ps The input stream.
 * \param sep The separating characters.
 * \param flags Some flags (see the enum declaration above)
 * \param res A vector that get filled with the content of the ps.
 */
void ps_split(pstream_t ps, const ctype_desc_t * nonnull sep, unsigned flags,
              union qv_lstr_t * nonnull res);

/** Split a stream based on a set of separator and an escape character.
 *
 * The line is parsed and
 * - Each time one of the separators is encountered, a new chunk is added
 *   in the result vector.
 * - Each time the escape character is encountered:
 *   - If the next character is a separator or an escape character. This
 *     character is added in the result vector.
 *   - If not, the escape character and the next character are added in the
 *     result vector.
 *
 * Strings appended are copied. The results may contain empty strings if
 * flags == PS_SPLIT_SKIP_EMPTY.
 *
 * \param[in] mp The pool on which copied data is allocated (NULL to allocate
 *                data on the heap).
 * \param[in] ps The input stream.
 * \param[in] sep The separating characters.
 * \param[in] esc An escape character.
 * \param[in] flags Some flags (see the enum declaration above)
 * \param[out] res A vector that get filled with the content of the ps.
 */
void ps_split_escaped(mem_pool_t * nullable mp, pstream_t ps,
                      const ctype_desc_t * nonnull sep, const char escape,
                      unsigned flags, union qv_lstr_t * nonnull res);

#define t_ps_split_escaped(ps, sep, escape, flags, res)  ({                  \
            ps_split_escaped(t_pool(), ps, sep, escape, flags, res);         \
        })

/****************************************************************************/
/* binary parsing helpers                                                   */
/****************************************************************************/

/*
 * XXX: those are dangerous, be sure you won't trigger unaligned access !
 *
 * Also the code supposes that `align` is a power of 2. If it's not, then
 * results will be completely absurd.
 */

static inline bool ps_aligned(const pstream_t * nonnull ps, size_t align)
{
    return ((uintptr_t)ps->p & (align - 1)) == 0;
}
#define ps_aligned2(ps)   ps_aligned(ps, 2)
#define ps_aligned4(ps)   ps_aligned(ps, 4)
#define ps_aligned8(ps)   ps_aligned(ps, 8)

static inline int __ps_align(pstream_t * nonnull ps, uintptr_t align)
{
    return __ps_skip_upto(ps, (const void *)ROUND_UP((uintptr_t)ps->b, align));
}
static inline const void * nonnull
__ps_get_block(pstream_t * nonnull ps, size_t len, size_t align)
{
    const void *p = ps->p;
    __ps_skip(ps, (len + align - 1) & ~(align - 1));
    return p;
}
#define __ps_get_type(ps,  type_t)  ((type_t *)__ps_get_block(ps, sizeof(type_t), 1))
#define __ps_get_type2(ps, type_t)  ((type_t *)__ps_get_block(ps, sizeof(type_t), 2))
#define __ps_get_type4(ps, type_t)  ((type_t *)__ps_get_block(ps, sizeof(type_t), 4))
#define __ps_get_type8(ps, type_t)  ((type_t *)__ps_get_block(ps, sizeof(type_t), 8))

static inline int ps_align(pstream_t * nonnull ps, uintptr_t align)
{
    const void *p = (const void *)ROUND_UP((uintptr_t)ps->b, align);
    PS_WANT(p <= ps->p_end);
    return __ps_skip_upto(ps, p);
}
static inline const void * nullable
ps_get_block(pstream_t * nonnull ps, size_t len, size_t align)
{
    return unlikely(!ps_has(ps, len)) ? NULL : __ps_get_block(ps, len, align);
}
#define ps_get_type(ps,  type_t)    ((type_t *)ps_get_block(ps, sizeof(type_t), 1))
#define ps_get_type2(ps, type_t)    ((type_t *)ps_get_block(ps, sizeof(type_t), 2))
#define ps_get_type4(ps, type_t)    ((type_t *)ps_get_block(ps, sizeof(type_t), 4))
#define ps_get_type8(ps, type_t)    ((type_t *)ps_get_block(ps, sizeof(type_t), 8))


/****************************************************************************/
/* misc helpers                                                             */
/****************************************************************************/

#define PS_FMT_ARG(ps)  (int)ps_len(ps), (ps)->s

static inline void sb_add_ps(sb_t * nonnull sb, pstream_t ps)
{
    sb_add(sb, ps.s, ps_len(&ps));
}

#endif
