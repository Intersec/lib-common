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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_STR_L_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_STR_L_H

/** \brief representation of a string with length.
 *
 * This type provides a unified way to talk about immutable strings (unlike
 * #sb_t that are mutable buffers).
 *
 * It remembers wether the string has been allocated through p_new, t_new, a
 * pool or is static.
 */
typedef struct lstr_t {
    union {
        const char * nullable s;
        char       * nullable v;
        void       * nullable data;
    };
    int len;
    unsigned mem_pool;
} lstr_t;

/* Static initializers {{{ */

#define LSTR_INIT(s_, len_)     { { (s_) }, (len_), 0 }
#define LSTR_INIT_V(s, len)     (lstr_t)LSTR_INIT(s, len)
#define LSTR_IMMED(str)         LSTR_INIT(""str, sizeof(str) - 1)
#define LSTR_IMMED_V(str)       LSTR_INIT_V(""str, sizeof(str) - 1)
#define LSTR(str)               ({ const char *__s = (str); \
                                   LSTR_INIT_V(__s, (int)strlen(__s)); })
#define LSTR_NULL               LSTR_INIT(NULL, 0)
#define LSTR_NULL_V             LSTR_INIT_V(NULL, 0)
#define LSTR_EMPTY              LSTR_INIT("", 0)
#define LSTR_EMPTY_V            LSTR_INIT_V("", 0)
#define LSTR_SB(sb)             LSTR_INIT((sb)->data, (sb)->len)
#define LSTR_SB_V(sb)           LSTR_INIT_V((sb)->data, (sb)->len)
#define LSTR_PS(ps)             LSTR_INIT((ps)->s, ps_len(ps))
#define LSTR_PS_V(ps)           LSTR_INIT_V((ps)->s, ps_len(ps))
#define LSTR_PTR(start, end)    LSTR_INIT((start), (end) - (start))
#define LSTR_PTR_V(start, end)  LSTR_INIT_V((start), (end) - (start))

#define LSTR_DATA(data, len)    LSTR_INIT((const char *)(data), (len))
#define LSTR_DATA_V(data, len)  (lstr_t)LSTR_DATA(data, len)

#define LSTR_CARRAY(carray)     LSTR_DATA((carray), sizeof(carray))
#define LSTR_CARRAY_V(carray)   (lstr_t)LSTR_CARRAY(carray)

#define LSTR_FMT_ARG(s_)      (s_).len, (s_).s

#define LSTR_OPT(str)         ({ const char *__s = (str);              \
                                 __s ? LSTR_INIT_V(__s, strlen(__s))   \
                                     : LSTR_NULL_V; })

/* obsolete stuff, please try not to use anymore */
#define LSTR_STR_V      LSTR
#define LSTR_OPT_STR_V  LSTR_OPT

/* }}} */
/* Base helpers {{{ */

static ALWAYS_INLINE lstr_t lstr_init_(const void * nullable s, int len,
                                       unsigned flags)
{
    return (lstr_t){ { (const char *)s }, len, flags };
}

static ALWAYS_INLINE
lstr_t mp_lstr_init(mem_pool_t * nullable mp, const void * nullable s, int len)
{
    mp = mp ?: &mem_pool_libc;
    return lstr_init_(s, len, mp->mem_pool & MEM_POOL_MASK);
}

/** Initialize a lstr_t from the content of a file.
 *
 * The function takes the prot and the flags to be passed to the mmap call.
 */
int lstr_init_from_file(lstr_t * nonnull dst, const char * nonnull path,
                        int prot, int flags);

/** Initialize a lstr_t from the content of a file pointed by a fd.
 */
int lstr_init_from_fd(lstr_t * nonnull dst, int fd, int prot, int flags);

/** lstr_wipe helper.
 */
void lstr_munmap(lstr_t * nonnull dst);
#define lstr_munmap(...)  lstr_munmap_DO_NOT_CALL_DIRECTLY(__VA_ARGS__)


/** lstr_copy_* helper. */
void mp_lstr_copy_(mem_pool_t * nullable mp, lstr_t * nonnull dst,
                   const void * nullable s, int len);

/** Sets \p dst to a new \p mp allocated lstr from its arguments. */
void mp_lstr_copys(mem_pool_t * nullable mp, lstr_t * nonnull dst,
                   const char * nullable s, int len);

/** Sets \p dst to a new \p mp allocated lstr from its arguments. */
void mp_lstr_copy(mem_pool_t * nullable mp, lstr_t * nonnull dst,
                  const lstr_t src);

/** Returns new \p mp allocated lstr from its arguments. */
lstr_t mp_lstr_dups(mem_pool_t * nullable mp, const char * nonnull s, int len);

/** Returns new \p mp allocated lstr from its arguments. */
lstr_t mp_lstr_dup(mem_pool_t * nullable mp, const lstr_t s);

/** Ensure \p s is \p mp or heap allocated. */
void mp_lstr_persists(mem_pool_t * nullable mp, lstr_t * nonnull s);

/** Duplicates \p v on the t_stack and reverse its content.
 *
 * This function is not unicode-aware.
 */
lstr_t mp_lstr_dup_ascii_reversed(mem_pool_t * nullable mp, const lstr_t v);

/** Duplicates \p v on the mem_pool and reverse its content.
 *
 * This function reverse character by character, which means that the result
 * contains the same characters in the reversed order but each character is
 * preserved in it's origin byte-wise order. This guarantees that both the
 * source and the destination are valid utf8 strings.
 *
 * In case of error, LSTR_NULL_V is returned.
 */
lstr_t mp_lstr_dup_utf8_reversed(mem_pool_t * nullable mp, const lstr_t v);

/** Concatenates its argument to form a new lstr on the mem pool. */
lstr_t mp_lstr_cat(mem_pool_t * nullable mp, const lstr_t s1, const lstr_t s2);

/** Concatenates its argument to form a new lstr on the mem pool. */
lstr_t mp_lstr_cat3(mem_pool_t * nullable mp, const lstr_t s1, const lstr_t s2,
                    const lstr_t s3);

/** Wipe a lstr_t (frees memory if needed).
 *
 * This flavour assumes that the passed memory pool is the one to deallocate
 * from if the lstr_t is known as beeing allocated in a pool.
 */
static inline void mp_lstr_wipe(mem_pool_t * nullable mp, lstr_t * nonnull s)
{
    mp_lstr_copy_(mp, s, NULL, 0);
}

/* }}} */
/* Transfer & static pool {{{ */

/** \brief copies \v src into \dst tranferring memory ownership to \v dst.
 */
static inline void lstr_transfer(lstr_t * nonnull dst, lstr_t * nonnull src)
{
    mp_lstr_copy_(ipool(src->mem_pool), dst, src->s, src->len);
    src->mem_pool = MEM_STATIC;
}

struct sb_t;

/** \brief copies \p src into \p dst transferring memory ownershipt to \p dst
 *
 * The \p src is a string buffer that will get reinitilized by the operation
 * as it loses ownership to the buffer.
 *
 * If \p keep_pool is false, the function ensures the memory will be allocated
 * on the heap (\ref sb_detach). If \p keep_pool is true, the memory will be
 * transfered as-is including the allocation pool.
 */
void lstr_transfer_sb(lstr_t * nonnull dst, struct sb_t * nonnull sb,
                      bool keep_pool);

/** \brief copies a constant of \v s into \v dst.
 */
static inline void lstr_copyc(lstr_t * nonnull dst, const lstr_t s)
{
    mp_lstr_copy_(&mem_pool_static, dst, s.s, s.len);
}

/** \brief returns a constant copy of \v s.
 */
static inline lstr_t lstr_dupc(const lstr_t s)
{
    return lstr_init_(s.s, s.len, MEM_STATIC);
}

/* }}} */
/* Heap allocations {{{ */

/** \brief wipe a lstr_t (frees memory if needed).
 */
static inline void lstr_wipe(lstr_t * nonnull s)
{
    return mp_lstr_wipe(NULL, s);
}

/** \brief returns new libc allocated lstr from its arguments.
 */
static inline lstr_t lstr_dups(const char * nullable s, int len)
{
    return mp_lstr_dups(NULL, s, len);
}

/** \brief returns new libc allocated lstr from its arguments.
 */
static inline lstr_t lstr_dup(const lstr_t s)
{
    return mp_lstr_dup(NULL, s);
}

/** \brief sets \v dst to a new libc allocated lstr from its arguments.
 */
static inline void lstr_copys(lstr_t * nonnull dst, const char * nullable s,
                              int len)
{
    mp_lstr_copys(NULL, dst, s, len);
}

/** \brief sets \v dst to a new libc allocated lstr from its arguments.
 */
static inline void lstr_copy(lstr_t * nonnull dst, const lstr_t src)
{
    mp_lstr_copy(NULL, dst, src);
}

/** \brief force lstr to be heap-allocated.
 *
 * This function ensure the lstr_t is allocated on the heap and thus is
 * guaranteed to be persistent.
 */
static inline void lstr_persists(lstr_t * nonnull s)
{
    mp_lstr_persists(NULL, s);
}

/** \brief duplicates \p v on the heap and reverse its content.
 *
 * This function is not unicode-aware.
 */
static inline lstr_t lstr_dup_ascii_reversed(const lstr_t v)
{
    return mp_lstr_dup_ascii_reversed(NULL, v);
}

/** \brief duplicates \p v on the heap and reverse its content.
 *
 * This function reverse character by character, which means that the result
 * contains the same characters in the reversed order but each character is
 * preserved in it's origin byte-wise order. This guarantees that both the
 * source and the destination are valid utf8 strings.
 *
 * In case of error, LSTR_NULL_V is returned.
 */
static inline lstr_t lstr_dup_utf8_reversed(const lstr_t v)
{
    return mp_lstr_dup_utf8_reversed(NULL, v);
}

/** \brief concatenates its argument to form a new lstr on the heap.
 */
static inline lstr_t lstr_cat(const lstr_t s1, const lstr_t s2)
{
    return mp_lstr_cat(NULL, s1, s2);
}

/** \brief concatenates its argument to form a new lstr on the heap.
 */
static inline lstr_t lstr_cat3(const lstr_t s1, const lstr_t s2,
                               const lstr_t s3)
{
    return mp_lstr_cat3(NULL, s1, s2, s3);
}

/* }}} */
/* t_stack allocation {{{ */

/** \brief returns a duplicated lstr from the mem stack.
 */
static inline lstr_t t_lstr_dup(const lstr_t s)
{
    return mp_lstr_dup(t_pool(), s);
}

/** \brief returns a duplicated lstr from the mem stack.
 */
static inline lstr_t t_lstr_dups(const char * nullable s, int len)
{
    return mp_lstr_dups(t_pool(), s, len);
}

/** \brief sets \v dst to a mem stack allocated copy of its arguments.
 */
static inline void t_lstr_copys(lstr_t * nonnull dst, const char * nullable s,
                                int len)
{
    return mp_lstr_copys(t_pool(), dst, s, len);
}

/** \brief sets \v dst to a mem stack allocated copy of its arguments.
 */
static inline void t_lstr_copy(lstr_t * nonnull dst, const lstr_t s)
{
    return mp_lstr_copy(t_pool(), dst, s);
}

/** \brief force lstr to be t_stack-allocated.
 *
 * This function ensure the lstr_t is allocated on the t_stack and thus is
 * guaranteed to be persistent.
 */
static inline void t_lstr_persists(lstr_t * nonnull s)
{
    return mp_lstr_persists(t_pool(), s);
}

/** \brief duplicates \p v on the t_stack and reverse its content.
 *
 * This function is not unicode-aware.
 */
static inline lstr_t t_lstr_dup_ascii_reversed(const lstr_t v)
{
    return mp_lstr_dup_ascii_reversed(t_pool(), v);
}

/** \brief duplicates \p v on the t_stack and reverse its content.
 *
 * This function reverse character by character, which means that the result
 * contains the same characters in the reversed order but each character is
 * preserved in it's origin byte-wise order. This guarantees that both the
 * source and the destination are valid utf8 strings.
 *
 * In case of error, LSTR_NULL_V is returned.
 */
static inline lstr_t t_lstr_dup_utf8_reversed(const lstr_t v)
{
    return mp_lstr_dup_utf8_reversed(t_pool(), v);
}

/** \brief concatenates its argument to form a new lstr on the t_stack.
 */
static inline lstr_t t_lstr_cat(const lstr_t s1, const lstr_t s2)
{
    return mp_lstr_cat(t_pool(), s1, s2);
}

/** \brief concatenates its argument to form a new lstr on the t_stack.
 */
static inline lstr_t t_lstr_cat3(const lstr_t s1, const lstr_t s2,
                                 const lstr_t s3)
{
    return mp_lstr_cat3(t_pool(), s1, s2, s3);
}

/** \brief return the left-trimmed lstr.
 */
static __must_check__ inline lstr_t lstr_ltrim(lstr_t s)
{
    while (s.len && isspace((unsigned char)s.s[0])) {
        s.s++;
        s.len--;
    }

    s.mem_pool = MEM_STATIC;
    return s;
}

/** \brief return the right-trimmed lstr.
 */
static __must_check__ inline lstr_t lstr_rtrim(lstr_t s)
{
    while (s.len && isspace((unsigned char)s.s[s.len - 1])) {
        s.len--;
    }

    return s;
}

/** \brief return the trimmed lstr.
 */
static __must_check__ inline lstr_t lstr_trim(lstr_t s)
{
    return lstr_rtrim(lstr_ltrim(s));
}

/* }}} */
/* r_pool allocation {{{ */

/** \brief returns a duplicated lstr from the mem stack.
 */
static inline lstr_t r_lstr_dup(const lstr_t s)
{
    return mp_lstr_dup(r_pool(), s);
}

/** \brief concatenates its argument to form a new lstr on the r_stack.
 */
static inline lstr_t r_lstr_cat(const lstr_t s1, const lstr_t s2)
{
    return mp_lstr_cat(r_pool(), s1, s2);
}

/* }}} */
/* Comparisons {{{ */

/** Returns "memcmp" ordering of \v s1 and \v s2. */
static ALWAYS_INLINE int lstr_cmp(const lstr_t s1, const lstr_t s2)
{
    /* workaround for a warning of -Wstringop-overflow in gcc 8
     * see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89699
     */
    size_t len = MIN((size_t)s1.len, (size_t)s2.len);

    return memcmp(s1.s, s2.s, len) ?: CMP(s1.len, s2.len);
}

/** Alternative version of \ref lstr_cmp that takes pointers as arguments.
 *
 * Can be used with qv_qsort.
 */
static ALWAYS_INLINE int lstr_cmp_p(const lstr_t * nonnull s1,
                                    const lstr_t * nonnull s2)
{
    return lstr_cmp(*s1, *s2);
}

/** Returns "memcmp" ordering of lowercase \v s1 and lowercase \v s2.  */
int lstr_ascii_icmp(const lstr_t s1, const lstr_t s2);

/** Alternative version of \ref lstr_ascii_icmp that takes pointers as
 * arguments.
 *
 * Can be used with qv_qsort.
 */
static inline int lstr_ascii_icmp_p(const lstr_t * nonnull s1,
                                    const lstr_t * nonnull s2)
{
    return lstr_ascii_icmp(*s1, *s2);
}

/** Returns whether \v s1 and \v s2 contents are equal. */
static ALWAYS_INLINE bool lstr_equal(const lstr_t s1, const lstr_t s2)
{
    return !!s1.s == !!s2.s && s1.len == s2.len
        && memcmp(s1.s, s2.s, s1.len) == 0;
}

/** Returns whether \p s1 and \p s2 contents are case-insentively equal.
 *
 * This function should only be used in case you have a small number of
 * comparison to perform. If you need to perform a lot of checks with the
 * exact same string, you should first lowercase (or uppercase) both string
 * and use the case-sensitive equality that will be much more efficient.
 *
 * This function is not unicode-aware.
 */
bool lstr_ascii_iequal(const lstr_t s1, const lstr_t s2);

/** Returns whether \v s1 contains substring \v s2.
 */
static ALWAYS_INLINE bool lstr_contains(const lstr_t s1, const lstr_t s2)
{
    return memmem(s1.data, s1.len, s2.data, s2.len) != NULL;
}

/** Returns whether \v s starts with \v p
 */
static ALWAYS_INLINE bool lstr_startswith(const lstr_t s, const lstr_t p)
{
    return s.len >= p.len && memcmp(s.s, p.s, p.len) == 0;
}

/** Returns whether \c s starts with \c c.
 */
static ALWAYS_INLINE bool lstr_startswithc(const lstr_t s, int c)
{
    return s.len >= 1 && s.s[0] == c;
}

/** Returns whether \c s ends with \c p.
 */
static ALWAYS_INLINE bool lstr_endswith(const lstr_t s, const lstr_t p)
{
    return s.len >= p.len && memcmp(s.s + s.len - p.len, p.s, p.len) == 0;
}

/** Returns whether \c s ends with \c c.
 */
static ALWAYS_INLINE bool lstr_endswithc(const lstr_t s, int c)
{
    return s.len >= 1 && s.s[s.len - 1] == c;
}

/** Returns whether \p s starts with \p p case-insenstively.
 *
 * \sa lstr_iequal, lstr_startswith
 *
 * This function is not unicode-aware.
 */
static inline bool lstr_ascii_istartswith(const lstr_t s, const lstr_t p)
{
    if (s.len < p.len) {
        return false;
    }
    return lstr_ascii_iequal(LSTR_INIT_V(s.s, p.len), p);
}

/** Returns whether \p s ends with \p p case-insenstively.
 *
 * \sa lstr_iequal, lstr_endswith
 * This function is not unicode-aware.
 */
static inline bool lstr_ascii_iendswith(const lstr_t s, const lstr_t p)
{
    if (s.len < p.len) {
        return false;
    }
    return lstr_ascii_iequal(LSTR_INIT_V(s.s + s.len - p.len, p.len), p);
}

/** Performs utf8-aware, case-insensitive comparison.
 */
static ALWAYS_INLINE int lstr_utf8_icmp(const lstr_t s1, const lstr_t s2)
{
    return utf8_stricmp(s1.s, s1.len, s2.s, s2.len, false);
}

/** Performs utf8-aware, case-sensitive comparison.
 */
static ALWAYS_INLINE int lstr_utf8_cmp(const lstr_t s1, const lstr_t s2)
{
    return utf8_strcmp(s1.s, s1.len, s2.s, s2.len, false);
}

/** Performs utf8-aware, case-insensitive equality check.
 */
static ALWAYS_INLINE bool lstr_utf8_iequal(const lstr_t s1, const lstr_t s2)
{
    return utf8_striequal(s1.s, s1.len, s2.s, s2.len, false);
}

/** Performs utf8-aware, case-sensitive equality check.
 */
static ALWAYS_INLINE bool lstr_utf8_equal(const lstr_t s1, const lstr_t s2)
{
    return utf8_strequal(s1.s, s1.len, s2.s, s2.len, false);
}

/** Returns whether \v s starts with \v p, in a case-insensitive utf8-aware
 * way.
 */
static ALWAYS_INLINE
bool lstr_utf8_istartswith(const lstr_t s1, const lstr_t s2)
{
    return utf8_str_istartswith(s1.s, s1.len, s2.s, s2.len);
}

/** Returns whether \v s starts with \v p, in a case-sensitive utf8-aware way.
 */
static ALWAYS_INLINE
bool lstr_utf8_startswith(const lstr_t s1, const lstr_t s2)
{
    return utf8_str_startswith(s1.s, s1.len, s2.s, s2.len);
}

/** Returns whether \v s ends with \v p, in a case-insensitive utf8-aware way.
 */
bool lstr_utf8_iendswith(const lstr_t s1, const lstr_t s2);

/** Returns whether \v s ends with \v p, in a case-sensitive utf8-aware way.
 */
bool lstr_utf8_endswith(const lstr_t s1, const lstr_t s2);

/** Checks if the input string has only characters in the given ctype. */
bool lstr_match_ctype(lstr_t s, const ctype_desc_t * nonnull d);

/** Returns the Damerau–Levenshtein distance between two strings.
 *
 * This is the number of additions, deletions, substitutions, and
 * transpositions needed to transform a string into another.
 * This can be used to detect possible misspellings in texts written by
 * humans.
 *
 * \param[in]  s1        the first string to compare.
 * \param[in]  s2        the second string to compare.
 * \param[in]  max_dist  the distance computation will be aborted if it is
 *                       detected that the result will be strictly greater
 *                       than this limit, allowing to save CPU time;
 *                       can be -1 for no limit.
 *
 * \return  -1 if \p max_dist was reached, the Damerau–Levenshtein distance
 *          between \p s1 and \p s2 otherwise.
 */
int lstr_dlevenshtein(const lstr_t s1, const lstr_t s2, int max_dist);

/** Retrieve the number of characters of an utf-8 encoded string.
 *
 * \return -1 in case of invalid utf-8 characters encountered.
 */
static inline int lstr_utf8_strlen(lstr_t s)
{
    return utf8_strnlen(s.s, s.len);
}

/** Truncate the string to the given number of utf8 characters. */
lstr_t lstr_utf8_truncate(lstr_t s, int char_len);

/** Returns whether a string respects a given SQL pattern.
 *
 * The matching is case insensitive, and unicode characters are handled
 * internally to make the matching intuitive (eg 'é' matches with 'e', 'œ'
 * matches with 'oe', ...).
 *
 * \param[in] ps The stream to parse
 * \param[in] pattern A pattern to match, using the SQL syntax of the "LIKE"
 *                    operator, (only '_' and '%' are handled).
 * \return True if \p s respects the pattern, false otherwise.
 */
bool lstr_utf8_is_ilike(const lstr_t s, const lstr_t pattern);

/* }}} */
/* Conversions {{{ */

/** Lower case the given lstr.
 *
 * Works only with ascii strings.
 */
lstr_t t_lstr_ascii_tolower(lstr_t s);

/** In-place version of \p t_lstr_ascii_tolower. */
void lstr_ascii_tolower(lstr_t * nonnull s);

/** Upper case the given lstr.
 *
 * Works only with ascii strings.
 */
lstr_t t_lstr_ascii_toupper(lstr_t s);

/** In-place version of \p t_lstr_ascii_toupper. */
void lstr_ascii_toupper(lstr_t * nonnull s);

/** In-place reversing of the lstr.
 *
 * This function is not unicode aware.
 */
lstr_t t_lstr_ascii_reverse(lstr_t s);

/** In-place version of \p t_lstr_ascii_reverse. */
void lstr_ascii_reverse(lstr_t * nonnull s);

/** Convert a lstr into an int.
 *
 *  \param  lstr the string to convert
 *  \param  out  pointer to the memory to store the result of the conversion
 *
 *  \result int
 *
 *  \retval  0   success
 *  \retval -1   failure (errno set)
 */
int lstr_to_int(lstr_t lstr, int * nonnull out);

/** Convert a lstr into an int64.
 *
 *  \param  lstr the string to convert
 *  \param  out  pointer to the memory to store the result of the conversion
 *
 *  \result int
 *
 *  \retval  0   success
 *  \retval -1   failure (errno set)
 */
int lstr_to_int64(lstr_t lstr, int64_t * nonnull out);

/** Convert a lstr into an uint64.
 *
 *  If the string begins with a minus sign (white spaces are skipped), the
 *  function returns -1 and errno is set to ERANGE.
 *
 *  \param  lstr the string to convert
 *  \param  out  pointer to the memory to store the result of the conversion
 *
 *  \result int
 *
 *  \retval  0   success
 *  \retval -1   failure (errno set)
 */
int lstr_to_uint64(lstr_t lstr, uint64_t * nonnull out);

/** Convert a lstr into an uint32.
 *
 *  If the string begins with a minus sign (white spaces are skipped), the
 *  function returns -1 and errno is set to ERANGE.
 *
 *  \param  lstr the string to convert
 *  \param  out  pointer to the memory to store the result of the conversion
 *
 *  \result int
 *
 *  \retval  0   success
 *  \retval -1   failure (errno set)
 */
int lstr_to_uint(lstr_t lstr, uint32_t * nonnull out);

/** Convert a lstr into a double.
 *
 *  \param  lstr the string to convert
 *  \param  out  pointer to the memory to store the result of the conversion
 *
 *  \result int
 *
 *  \retval  0   success
 *  \retval -1   failure (errno set)
 */
int lstr_to_double(lstr_t lstr, double * nonnull out);

/** Decode a hexadecimal lstr
 *
 *  \param  lstr      the hexadecimal string to convert
 *
 *  \result lstr_t    the result of the conversion (allocated on t_stack)
 *
 *  \retval LSTR_NULL failure
 */
lstr_t t_lstr_hexdecode(lstr_t lstr);

/** Enccode a lstr into hexadecimal
 *
 *  \param  lstr        the string to convert
 *
 *  \result lstr_t      the result of the conversion (allocated on t_stack)
 *
 *  \retval  LSTR_NULL  failure
 */
lstr_t t_lstr_hexencode(lstr_t lstr);

/** Xor a lstr with another.
 *
 * \param[in]  in  the string to xor
 * \param[in]  key  the string used to perform the xor
 * \param[out]  out the result (may be the same than in)
 */
static inline void lstr_xor(lstr_t in, lstr_t key, lstr_t out)
{
    assert (in.len == out.len);
    for (int i = 0; i < in.len; i++) {
        out.v[i] = in.s[i] ^ key.s[i % key.len];
    }
}

/** Obfuscate or unobfuscate a lstr.
 *
 * This function is meant to be more secure than lstr_xor.
 * The default (naive) implementation is in str-l-obfuscate-default.c, but can
 * be overridden using 'ctx.lstr_obfuscate_src' at waf configure.
 *
 * \param[in]  in  the string to obfuscate
 * \param[in]  key  a key used to perform obfuscation; the same key must be
 *                  given when for obfuscation and unobfuscation of the same
 *                  string.
 * \param[out]  out  the result; must be allocated, may be the same than
 *                   `in`; its length is unchanged.
 */
void lstr_obfuscate(lstr_t in, uint64_t key, lstr_t out);
#define lstr_unobfuscate(in, key, out)  lstr_obfuscate(in, key, out)

/** Trim 1 to 8 padding bytes (PKCS#7).
 *
 * This implementation is not fully PKCS#7 compliant: only the last padding
 * byte is read, the other padding bytes are not checked.
 * \note sb_add_pkcs7_8_bytes_padding() should be used for padding.
 *
 * \param[in] padded The padded lstr.
 * \returns The lstr with padding trimmed in case of success.
 *          LSTR_NULL_V in case of error.
 */
lstr_t lstr_trim_pkcs7_padding(lstr_t padded);

/* }}} */
/* Format {{{ */

#define lstr_fmt(fmt, ...)                                                   \
    ({ const char *__s = asprintf(fmt, ##__VA_ARGS__);                       \
       lstr_init_(__s, strlen(__s), MEM_LIBC); })

#define mp_lstr_fmt(mp, fmt, ...)                                            \
    ({ int __len; const char *__s = mp_fmt(mp, &__len, fmt, ##__VA_ARGS__);  \
       mp_lstr_init((mp), __s, __len); })

#define t_lstr_fmt(fmt, ...)  mp_lstr_fmt(t_pool(), fmt, ##__VA_ARGS__)


#define lstr_vfmt(fmt, va)                                                   \
    ({ const char *__s = vasprintf(fmt, va);                                 \
       lstr_init_(__s, strlen(__s), MEM_LIBC); })

#define mp_lstr_vfmt(mp, fmt, va) \
    ({ int __len; const char *__s = mp_vfmt(mp, &__len, fmt, va); \
       mp_lstr_init((mp), __s, __len); })

#define t_lstr_vfmt(fmt, va)  mp_lstr_vfmt(t_pool(), fmt, va)

/* }}} */
#endif
