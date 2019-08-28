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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_STR_BUF_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_STR_BUF_H

/* sb_t is a wrapper type for a reallocatable byte array.  Its internal
 * representation is accessible to client code but care must be exercised to
 * preserve strbuf invariants and avoid dangling pointers when strbuf data is
 * reallocated by strbuf functions.
 *
 * Here is a description of the strbuf fields:
 *
 * - <data> is a pointer to the active portion of the byte array.  As <data>
 *   may be reallocated by strbuf operations, it is error prone to copy its
 *   value to a local variable for content parsing or other such
 *   manipulations.  As <data> may point to non allocated storage, static or
 *   dynamic or automatic, returning its value is guaranteed to cause
 *   potential problems, current of future.  If some strbuf data was skipped,
 *   <data> may no longer point to the beginning of the original or allocated
 *   array.  <data> cannot be NULL.  It is initialized as the address of a
 *   global 1 byte array, or a buffer with automatic storage.
 *
 * - <len> is the length in bytes of the strbuf contents.  A strbuf invariant
 *   specifies that data[len] == '\0'.  This invariant must be kept at all
 *   times, and implies that data storage extend at least one byte beyond
 *   <len>. <len> is always positive or zero. <len> is initialized to zero for
 *   an empty strbuf.
 *
 * - <size> is the number of bytes available for strbuf contents starting at
 *   <data>.  The strbuf invariant implies that size > len.
 *
 * - <skip> is a count of byte skipped from the beginning of the original data
 *   buffer.  It is used for efficient pruning of initial bytes.
 *
 * - <allocated> is a 1 bit boolean to indicate if strbuf data was allocated
 *   with p_new and must be freed.  Actual allocated buffer starts at .data -
 *   .skip.
 *
 * strbuf invariants:
 * - sb.data != NULL
 * - sb.len >= 0
 * - sb.size > sb.len
 * - sb.data - sb.skip points to an array of at least sb.size + sb.skip bytes.
 * - sb.data[sb.len] == '\0'
 * - sb.data - sb.skip is a pointer handled by mp_new/mp_delete for the pool
 *   sb.mp.
 *
 *                │←─ sb->size ──────────────────────→│
 * │←─ sb->skip ─→│←─ sb->len ─→│    │←─ sb_avail() ─→│
 * ┌──────────────┬─────────────┬────┬────────────────┐
 * │  skip area   │    data     │'\0'│ available room │
 * └──────────────┴─────────────┴────┴────────────────┘
 *                ↑             ↑
 *            sb->data       sb_end()
 */

typedef struct sb_t {
    char * nonnull data;
    int len, size, skip;
    mem_pool_t * nullable mp;
#ifdef __cplusplus
    inline sb_t();
    inline sb_t(void * nonnull buf, int len, int size,
                mem_pool_t * nullable mp);
    inline ~sb_t();

  private:
    DISALLOW_COPY_AND_ASSIGN(sb_t);
#endif
} sb_t;

/* Default byte array for empty strbufs.
 */
extern const char __sb_slop[1];


/**************************************************************************/
/* Initialization                                                         */
/**************************************************************************/

static inline void sb_set_trailing0(sb_t * nonnull sb)
{
    if (sb->data != __sb_slop) {
        sb->data[sb->len] = '\0';
    } else {
        assert (sb->data[0] == '\0');
    }
}

static inline sb_t * nonnull
sb_init_full(sb_t * nonnull sb, void * nonnull buf, int blen, int bsize,
             mem_pool_t * nullable mp)
{
    assert (blen < bsize);
    sb->data = cast(char *, buf);
    sb->len  = blen;
    sb->size = bsize;
    sb->skip = 0;
    sb->mp   = mp;
    sb_set_trailing0(sb);
    return sb;
}

/** SB() macro declare a sb using stack buffer with a constant size.
 *
 * It will be automatically wiped when leaving the current scope.
 */
#ifdef __cplusplus

# define SB(name, sz)                                                        \
    CONST_SIZE_ARRAY(__##name##_buf, char, (sz));                            \
    sb_t name(__##name##_buf, 0, sz, &mem_pool_static)

# define t_SB(name, sz)  sb_t name(t_new_raw(char, sz), 0, sz, t_pool())

#else

# define SB_INIT(buf, sz, pool)                                              \
    {   .data = memset(buf, 0, 1),                                           \
        .size = sz, .mp = pool }

# define SB(name, sz)                                                        \
    CONST_SIZE_ARRAY(__##name##_buf, char, (sz));                            \
    sb_t name __attribute__((cleanup(sb_wipe))) =                            \
        SB_INIT(__##name##_buf, sz, &mem_pool_static)

# define t_SB(name, sz)                                                      \
    sb_t name = SB_INIT(t_new_raw(char, sz), sz, t_pool())

#endif

#define t_SB_1k(name)  t_SB(name, 1 << 10)
#define t_SB_8k(name)  t_SB(name, 8 << 10)
#define SB_1k(name)    SB(name, 1 << 10)
#define SB_8k(name)    SB(name, 8 << 10)

static inline sb_t * nonnull sb_init(sb_t * nonnull sb)
{
    return sb_init_full(sb, (char *)__sb_slop, 0, 1, &mem_pool_libc);
}

static inline sb_t * nonnull mp_sb_init(mem_pool_t * nullable mp,
                                        sb_t * nonnull sb, int size)
{
    return sb_init_full(sb, mp_new_raw(mp, char, size), 0, size, mp);
}

static inline sb_t * nonnull t_sb_init(sb_t * nonnull sb, int size)
{
    return mp_sb_init(t_pool(), sb, size);
}

static inline sb_t * nonnull r_sb_init(sb_t * nonnull sb, int size)
{
    return mp_sb_init(r_pool(), sb, size);
}

void sb_reset(sb_t * nonnull sb) __leaf;

/** Resets the buffer but keeps the allocated memory, however big it is. */
void sb_reset_keep_mem(sb_t * nonnull sb) __leaf;

void sb_wipe(sb_t * nonnull sb) __leaf;

GENERIC_NEW(sb_t, sb);
GENERIC_DELETE(sb_t, sb);
#ifdef __cplusplus
sb_t::sb_t() :
    data((char *)__sb_slop),
    len(0),
    size(1),
    skip(0),
    mp(NULL)
{
}
sb_t::sb_t(void * nonnull buf, int len_, int size_, mem_pool_t * nullable mp_)
    : data(static_cast<char *>(memset(buf, 0, 1))),
    len(len_),
    size(size_),
    skip(0),
    mp(mp_)
{
    assert (len < size);
}
sb_t::~sb_t() { sb_wipe(this); }
#endif

/**************************************************************************/
/* str/mem-functions wrappers                                             */
/**************************************************************************/

static inline int sb_cmp(const sb_t * nonnull sb1, const sb_t * nonnull sb2)
{
    int len = MIN(sb1->len, sb2->len);
    int res = memcmp(sb1->data, sb2->data, len);
    return res ? res : sb1->len - sb2->len;
}

int sb_search(const sb_t * nonnull sb, int pos,
              const void * nonnull what, int wlen)
    __leaf;


/**************************************************************************/
/* buffer raw manipulations                                               */
/**************************************************************************/

static inline char * nonnull sb_end(const sb_t * nonnull sb)
{
    return sb->data + sb->len;
}

static inline int sb_avail(const sb_t * nonnull sb)
{
    return sb->size - sb->len - 1;
}

char * nonnull sb_detach(sb_t * nonnull sb, int * nullable len) __leaf;

int  __sb_rewind_adds(sb_t * nonnull sb, const sb_t * nonnull orig) __leaf;
void __sb_grow(sb_t * nonnull sb, int extra) __leaf;
void __sb_optimize(sb_t * nonnull sb, size_t len) __leaf;
static inline void __sb_fixlen(sb_t * nonnull sb, int len)
{
    sb->len = len;
    sb_set_trailing0(sb);
}

static inline void sb_optimize(sb_t * nonnull sb, size_t extra)
{
    size_t size = sb->size + sb->skip;
    size_t len  = sb->len + 1;

    if (unlikely(size > BUFSIZ && (len + extra) * 8 < size))
        __sb_optimize(sb, len + extra);
}

static inline char * nonnull sb_grow(sb_t * nonnull sb, int extra)
{
    if (sb->len + extra >= sb->size) {
        __sb_grow(sb, extra);
    } else {
        sb_optimize(sb, extra);
    }
    return sb_end(sb);
}

static inline char * nonnull sb_growlen(sb_t * nonnull sb, int extra)
{
    if (sb->len + extra >= sb->size)
        __sb_grow(sb, extra);
    __sb_fixlen(sb, sb->len + extra);
    return sb_end(sb) - extra;
}

/**************************************************************************/
/* splicing                                                               */
/**************************************************************************/

static inline void sb_add(sb_t * nonnull sb, const void * nonnull data,
                          int dlen)
{
    char *buf = sb_growlen(sb, dlen);

    /* workaround for a false positive of -Warray-bounds in gcc 8
     * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89689
     */
    if (dlen && sb->data == __sb_slop) {
        __builtin_unreachable();
    }
    memcpy(buf, data, dlen);
}

static inline void sb_addsb(sb_t * nonnull sb, const sb_t * nonnull sb2)
{
    sb_add(sb, sb2->data, sb2->len);
}

static inline void sb_addc(sb_t * nonnull sb, unsigned char c)
{
    sb_add(sb, &c, 1);
}

static inline void sb_adduc(sb_t * nonnull sb, int c)
{
    int len = __pstrputuc(sb_grow(sb, 4), c);
    __sb_fixlen(sb, sb->len + len);
}

static inline void sb_addnc(sb_t * nonnull sb, int extralen, unsigned char c)
{
    memset(sb_growlen(sb, extralen), c, extralen);
}
static inline void sb_add0s(sb_t * nonnull sb, int extralen)
{
    sb_addnc(sb, extralen, 0);
}
static inline void sb_adds(sb_t * nonnull sb, const char * nonnull s)
{
    sb_add(sb, s, strlen(s));
}
static inline void sb_add_lstr(sb_t * nonnull sb, lstr_t s)
{
    sb_add(sb, s.s, s.len);
}

/* Prepare the string buffer for deletion of length "rm_len" followed by an
 * insertion of length "insert_len" at position "pos". */
char * nonnull
__sb_splice(sb_t * nonnull sb, int pos, int rm_len, int insert_len);

/** Deletes and inserts data at a given position in a string buffer.
 *
 * \param[in, out] sb      The string buffer to modify.
 * \param[in]      pos     Position used for deletion and insertion.
 * \param[in]      rm_len  Number of bytes to remove.
 * \param[in]      data    Data to insert.
 * \param[in]      dlen    Length of the data to insert.
 */
static inline char * nonnull
sb_splice(sb_t * nonnull sb, int pos, int rm_len, const void * nullable data,
          int dlen)
{
    char *res;

    assert (pos >= 0 && rm_len >= 0 && dlen >= 0);
    assert ((unsigned)pos <= (unsigned)sb->len);
    assert ((unsigned)pos + (unsigned)rm_len <= (unsigned)sb->len);

#ifndef __cplusplus
    if (__builtin_constant_p(dlen)) {
        if (dlen == 0 || (__builtin_constant_p(rm_len) && rm_len >= dlen)) {
            p_move2(sb->data, pos + dlen, pos + rm_len,
                    sb->len - pos - rm_len);
            __sb_fixlen(sb, sb->len + dlen - rm_len);
            return sb->data + pos;
        }
    }
    if (__builtin_constant_p(rm_len) && rm_len == 0 && pos == sb->len) {
        res = sb_growlen(sb, dlen);
    } else {
        res = __sb_splice(sb, pos, rm_len, dlen);
    }
#else
    res = __sb_splice(sb, pos, rm_len, dlen);
#endif
    return data ? (char *)memcpy(res, data, dlen) : res;
}

static inline char * nonnull
sb_splice_lstr(sb_t * nonnull sb, int pos, int rm_len, lstr_t insert)
{
    return sb_splice(sb, pos, rm_len, insert.s, insert.len);
}

static inline void
sb_splicenc(sb_t * nonnull sb, int pos, int len, int extralen, unsigned char c)
{
    memset(sb_splice(sb, pos, len, NULL, extralen), c, extralen);
}
static inline void sb_splice0s(sb_t * nonnull sb, int pos, int len, int extralen)
{
    sb_splicenc(sb, pos, len, extralen, 0);
}
static inline void sb_prepends(sb_t * nonnull sb, const char * nonnull s)
{
    sb_splice(sb, 0, 0, s, strlen(s));
}
static inline void sb_prepend_lstr(sb_t * nonnull sb, lstr_t s)
{
    sb_splice_lstr(sb, 0, 0, s);
}
static inline void sb_prependc(sb_t * nonnull sb, unsigned char c)
{
    sb_splice(sb, 0, 0, &c, 1);
}
static inline void sb_skip(sb_t * nonnull sb, int len)
{
    assert (len >= 0 && len <= sb->len);
    if ((sb->len -= len)) {
        sb->data += len;
        sb->skip += len;
        sb->size -= len;
    } else {
        sb_reset(sb);
    }
}
static inline void sb_skip_upto(sb_t * nonnull sb, const void * nonnull where)
{
    sb_skip(sb, (const char *)where - sb->data);
}
static inline void sb_clip(sb_t * nonnull sb, int len)
{
    assert (len >= 0 && len <= sb->len);
    __sb_fixlen(sb, len);
}
static inline void sb_shrink(sb_t * nonnull sb, int len)
{
    assert (len >= 0 && len <= sb->len);
    __sb_fixlen(sb, sb->len - len);
}
static inline void sb_shrink_upto(sb_t * nonnull sb, const void * nonnull where)
{
    sb_clip(sb, (const char *)where - sb->data);
}

static inline void sb_ltrim_ctype(sb_t * nonnull sb,
                                  const ctype_desc_t * nonnull desc)
{
    const char *p = sb->data, *end = p + sb->len;

    while (p < end && ctype_desc_contains(desc, *p))
        p++;
    sb_skip_upto(sb, p);
}
static inline void sb_ltrim(sb_t * nonnull sb)
{
    sb_ltrim_ctype(sb, &ctype_isspace);
}

static inline void sb_rtrim_ctype(sb_t * nonnull sb,
                                  const ctype_desc_t * nonnull desc)
{
    const char *p = sb->data, *end = p + sb->len;

    while (p < end && ctype_desc_contains(desc, end[-1]))
        --end;
    sb_shrink_upto(sb, end);
}
static inline void sb_rtrim(sb_t * nonnull sb)
{
    sb_rtrim_ctype(sb, &ctype_isspace);
}

static inline void sb_trim_ctype(sb_t * nonnull sb,
                                 const ctype_desc_t * nonnull desc)
{
    sb_ltrim_ctype(sb, desc);
    sb_rtrim_ctype(sb, desc);
}
static inline void sb_trim(sb_t * nonnull sb)
{
    sb_trim_ctype(sb, &ctype_isspace);
}

int sb_addvf(sb_t * nonnull sb, const char * nonnull fmt, va_list ap)
    __leaf __attr_printf__(2, 0);
int sb_addf(sb_t * nonnull sb, const char * nonnull fmt, ...)
    __leaf __attr_printf__(2, 3);

/** Reset and optimize a string buffer for sb_prepend().
 *
 * Purpose: put the string buffer in a state in which a "sb_prepend" of length
 * "len" triggers only a memmove of size "len" instead of moving the whole
 * buffer first.
 *
 * \note This optimization won't last after first "sb_add" or first realloc of
 * the sb.
 */
static inline void sb_reset_reverse(sb_t * nonnull sb)
{
    sb_reset(sb);
    sb->data += sb->size - 1;
    sb->skip = sb->size - 1;
    /* XXX Keep one byte for '\0'. */
    sb->size = 1;
}

int sb_prependvf(sb_t * nonnull sb, const char * nonnull fmt, va_list ap)
    __leaf __attr_printf__(2, 0);
int sb_prependf(sb_t * nonnull sb, const char * nonnull fmt, ...)
    __leaf __attr_printf__(2, 3);

/** Appends content to a string buffer, filtering out characters that are not
 *  part of a given character set.
 *
 * \deprecated Use sb_add_sanitized instead.
 *
 * \param[inout] sb Buffer to be updated
 * \param[in]    s  String to be filtered and added
 * \param[in]    d  Character set
 */
void sb_add_filtered(sb_t * nonnull sb, lstr_t s,
                     const ctype_desc_t * nonnull d);

/** Appends content to a string buffer, filtering out characters that are part
 *  of a given character set.
 *
 * \deprecated Use sb_add_sanitized_out instead.
 *
 * \param[inout] sb Buffer to be updated
 * \param[in]    s  String to be filtered and added
 * \param[in]    d  Character set
 */
void sb_add_filtered_out(sb_t * nonnull sb, lstr_t s,
                         const ctype_desc_t * nonnull d);

/** Appends content to a string buffer, replacing characters that are not
 *  part of a given character set with another character.
 *  eg "!aa!!b!c" => "_aa_b_c"
 *
 * \param[inout] sb Buffer to be updated
 * \param[in]    s  String to be filtered and added
 * \param[in]    d  Character set
 * \param[in]    c  Character to add to the buffer in place of substrings
 *                  that are replaced. -1 to simply ignore those substrings.
 */
void sb_add_sanitized(sb_t * nonnull sb, lstr_t s,
                      const ctype_desc_t * nonnull d, int c);

/** Appends content to a string buffer, replacing characters that are part of
 *  a given character set with another character.
 *  eg "!aa!!b!c" => "_aa_b_c"
 *
 * \param[inout] sb Buffer to be updated
 * \param[in]    s  String to be filtered and added
 * \param[in]    d  Character set
 * \param[in]    c  Character to add to the buffer in place of substrings
 *                  that are replaced. -1 to simply ignore those substrings.
 */
void sb_add_sanitized_out(sb_t * nonnull sb, lstr_t s,
                          const ctype_desc_t * nonnull d, int c);

#define sb_setvf(sb, fmt, ap) \
    ({ sb_t *__b = (sb); sb_reset(__b); sb_addvf(__b, fmt, ap); })
#define sb_setf(sb, fmt, ...) \
    ({ sb_t *__b = (sb); sb_reset(__b); sb_addf(__b, fmt, ##__VA_ARGS__); })

static inline void sb_set(sb_t * nonnull sb, const void * nonnull data,
                          int dlen)
{
    sb->len = 0;
    sb_add(sb, data, dlen);
}
static inline void sb_setsb(sb_t * nonnull sb, const sb_t * nonnull sb2)
{
    sb_set(sb, sb2->data, sb2->len);
}
static inline void sb_sets(sb_t * nonnull sb, const char * nonnull s)
{
    sb_set(sb, s, strlen(s));
}
static inline void sb_set_lstr(sb_t * nonnull sb, lstr_t s)
{
    sb_set(sb, s.s, s.len);
}

/** Appends a pretty-formated unsigned integer to a string buffer with a
 *  thousand separator.
 *
 * \param[inout] sb           Buffer to be updated.
 * \param[in]    val          Unsigned integer 64 to be added in the buffer.
 * \param[in]    thousand_sep Character used as thousand separator. Use -1 for
 *                            none.
 */
void sb_add_uint_fmt(sb_t * nonnull sb, uint64_t val, int thousand_sep);

/** Appends a pretty-formated integer to a string buffer with a thousand
 *  separator.
 *
 * \param[inout] sb           Buffer to be updated.
 * \param[in]    val          Integer 64 to be added in the buffer.
 * \param[in]    thousand_sep Character used as thousand separator. Use -1 for
 *                            none.
 */
void sb_add_int_fmt(sb_t * nonnull sb, int64_t val, int thousand_sep);

/** Appends a pretty-formated number to a string buffer.
 *
 * Here are some examples with dec_sep = '.' and thousand_sep = ',':
 *
 *   1234.1234 nb_max_decimals 0 ->  '1,234'
 *   1234.1234 nb_max_decimals 1 ->  '1,234.1'
 *   1234.1234 nb_max_decimals 5 ->  '1,234.12340'
 *  -1234.1234 nb_max_decimals 5 -> '-1,234.12340'
 *   1234      nb_max_decimals 3 ->  '1,234'
 *
 * And with thousand_sep = -1:
 *
 *   1234.1234 nb_max_decimals 0 ->  '1234'
 *
 * \param[inout] sb              Buffer to be updated.
 * \param[in]    val             Double value to be added in the buffer.
 * \param[in]    nb_max_decimals Max number of decimals to be printed.
 *                               If all the decimals of the number are 0s,
 *                               none are pinted (and the decimal separator
 *                               is not printed neither). Otherwise, they are
 *                               right-padded with 0s.
 * \param[in]    dec_sep         Character used as decimal separator.
 * \param[in]    thousand_sep    Character used as thousand separator for
 *                               integer part. Use -1 for none.
 */
void sb_add_double_fmt(sb_t * nonnull sb, double val, uint8_t nb_max_decimals,
                       int dec_sep, int thousand_sep);

/** Appends a pretty-formatted duration to a string buffer.
 *
 * Only the two main units are used, e.g. if the duration is at least 1 day,
 * only days and hours will be kept. Plus, the duration is rounded:
 * e.g. 61001 => 1m 1s   1ms => 1m 1s
 *      61999 => 1m 1s 999ms => 1m 2s
 *
 * \param[inout] sb       Buffer to be updated.
 * \param[in]    ms       The duration, in milliseconds.
 * \param[in]    print_ms Whether to print the milliseconds or not.
 */
void _sb_add_duration_ms(sb_t * nonnull sb, uint64_t ms, bool print_ms);

static inline void sb_add_duration_s(sb_t * nonnull sb, uint64_t s)
{
    _sb_add_duration_ms(sb, s * 1000ULL, false);
}

static inline void sb_add_duration_ms(sb_t *nonnull sb, uint64_t ms)
{
    _sb_add_duration_ms(sb, ms, true);
}

/** Append 1 to 8 padding bytes (PKCS#7).
 *
 * Padding bytes are appended to the sb in order to ensure 8 bytes blocks.
 * \note lstr_trim_pkcs7_padding() should be used to trim padding.
 *
 * \param[in,out] sb The buffer to pad.
 */
void sb_add_pkcs7_8_bytes_padding(sb_t *nonnull sb);

/**************************************************************************/
/* syscall/system wrappers                                                */
/**************************************************************************/

struct sockaddr;

/** reads a line from file f.
 *
 * Typical use is (boilerplate removed for clarity)
 *
 * <code>
 * int res;
 * SB_1K(sb);
 * FILE *f = RETHROW_PN(fopen(...));
 *
 * while ((res = sb_getline(&sb, f)) > 0) {
 *     // use sb. WARNING: the last character is always '\n'.
 *     sb_reset(&sb);
 * }
 * if (res == 0) {
 *     // EOF
 * } else {
 *     assert (res < 0);
 *     // ERROR
 * }
 *
 * </code>
 *
 *
 * \returns
 *   -1 if an error was met, check ferror(f) and/or errno
 *   0 if at EOF
 *   >0 the number of octets read
 */
int sb_getline(sb_t * nonnull sb, FILE * nonnull f) __leaf;
int sb_fread(sb_t * nonnull sb, int size, int nmemb,
             FILE * nonnull f) __leaf;
int sb_read_fd(sb_t * nonnull sb, int fd) __leaf;
int sb_read_file(sb_t * nonnull sb, const char * nonnull filename) __leaf;
int sb_write_file(const sb_t * nonnull sb,
                  const char * nonnull filename) __leaf;
int sb_append_to_file(const sb_t * nonnull sb,
                      const char * nonnull filename) __leaf;

int sb_read(sb_t * nonnull sb, int fd, int hint) __leaf;
int sb_recv(sb_t * nonnull sb, int fd, int hint, int flags) __leaf;
int sb_recvfrom(sb_t * nonnull sb, int fd, int hint, int flags,
                struct sockaddr * nullable addr, socklen_t * nullable alen)
    __leaf;


/**************************************************************************/
/* usual quoting mechanisms (base64, addslashes, ...)                     */
/**************************************************************************/

#define __SB_DEFINE_ADDS(sfx, name)                                          \
    static inline void sb_adds_##sfx(sb_t * nonnull sb,                      \
                                     const char * nonnull s)                 \
    {                                                                        \
        sb_add_##sfx(sb, s, strlen(s));                                      \
    }                                                                        \
    static inline void sb_add_lstr_##sfx(sb_t * nonnull sb, lstr_t s) {      \
        sb_add_##sfx(sb, s.s, s.len);                                        \
    }
#define __SB_DEFINE_ADDS_ERR(sfx, name)                                      \
    static inline int sb_adds_##sfx(sb_t * nonnull sb,                       \
                                    const char * nonnull s)                  \
    {                                                                        \
        return sb_add_##sfx(sb, s, strlen(s));                               \
    }                                                                        \
    static inline int sb_add_lstr_##sfx(sb_t * nonnull sb, lstr_t s) {       \
        return sb_add_##sfx(sb, s.s, s.len);                                 \
    }


void sb_add_slashes(sb_t * nonnull sb, const void * nonnull data, int len,
                    const char * nonnull toesc,
                    const char * nonnull esc) __leaf;
static inline void
sb_adds_slashes(sb_t * nonnull sb, const char * nonnull s,
                const char * nonnull toesc, const char * nonnull esc)
{
    sb_add_slashes(sb, s, strlen(s), toesc, esc);
}
void sb_add_unslashes(sb_t * nonnull sb, const void * nonnull data, int len,
                      const char * nonnull tounesc,
                      const char * nonnull unesc) __leaf;
static inline void
sb_adds_unslashes(sb_t * nonnull sb, const char * nonnull s,
                  const char * nonnull tounesc, const char * nonnull unesc)
{
    sb_add_unslashes(sb, s, strlen(s), tounesc, unesc);
}

/** Add a string by expanding the environment variables.
 *
 * This adds the string pointed by \p data with length \p len in the buffer
 * \p sb after expanding environment variables marked with syntax
 * <code>${VAR_NAME}</code> or <code>$VAR_NAME</code>. Literal <code>$</code>
 * or <code>\</code> must be escaped using backslashes.
 */
int sb_add_expandenv(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
__SB_DEFINE_ADDS_ERR(expandenv, "ExpandingEnv");

void sb_add_unquoted(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
__SB_DEFINE_ADDS(unquoted, "Unquoted");

void sb_add_urlencode(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
void sb_add_urldecode(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
void sb_urldecode(sb_t * nonnull sb) __leaf;
__SB_DEFINE_ADDS(urlencode, "EncodingURL");
__SB_DEFINE_ADDS(urldecode, "DecodingURL");

void sb_add_hex(sb_t * nonnull sb, const void * nonnull data, int len) __leaf;
int  sb_add_unhex(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
__SB_DEFINE_ADDS(hex, "Hex");
__SB_DEFINE_ADDS_ERR(unhex, "Unhex");

/* this all assumes utf8 data ! */
void sb_add_xmlescape(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
int  sb_add_xmlunescape(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
__SB_DEFINE_ADDS(xmlescape, "EscapingXML");
__SB_DEFINE_ADDS_ERR(xmlunescape, "UnescapingXML");

void sb_add_qpe(sb_t * nonnull sb, const void * nonnull data, int len) __leaf;
void sb_add_unqpe(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
__SB_DEFINE_ADDS(qpe, "EscapingAsQuotedPrintable");
__SB_DEFINE_ADDS(unqpe, "UnescapingQuotedPrintable");

typedef struct sb_b64_ctx_t {
    short packs_per_line;
    short pack_num;
    byte  trail[2];
    byte  trail_len;
} sb_b64_ctx_t;

/** Start a multiple step base 64 encoding.
 *
 * \param[out]  sb  the output buffer, receiving encoded data.
 * \param[in]  len  estimation of the number of bytes that will be encoded.
 * \param[in]  width   maximum length for output lines, not counting end of
 *                     line markers.  0 for standard 76 character lines, -1
 *                     for unlimited line length.
 * \param[out]  ctx  encoding context (may be uninitialized).
 */
void sb_add_b64_start(sb_t * nonnull sb, int len, int width,
                      sb_b64_ctx_t * nonnull ctx) __leaf;

/** Next base 64 encoding step.
 *
 * \param[out]  sb  the output buffer, receiving encoded data.
 * \param[in]  src0  next data to encode.
 * \param[in]  len  number of bytes to encode from src0.
 * \param[in|out]  ctx  encoding context.
 */
void sb_add_b64_update(sb_t * nonnull sb, const void * nonnull src0, int len,
                       sb_b64_ctx_t * nonnull ctx) __leaf;

/** Terminates a multiple step base 64 encoding.
 *
 * \param[out]  sb  the output buffer, receiving encoded data.
 * \param[in]  ctx  encoding context.
 */
void sb_add_b64_finish(sb_t * nonnull sb, sb_b64_ctx_t * nonnull ctx)
    __leaf;

/** Encode data to base64.
 *
 * \param[out]  sb  the output buffer, receiving encoded data.
 * \param[in]  data  data to encode.
 * \param[in]  len  number of bytes to encode.
 * \param[in]  width   maximum length for output lines, not counting end of
 *                     line markers.  0 for standard 76 character lines, -1
 *                     for unlimited line length.
 */
void sb_add_b64(sb_t * nonnull sb, const void * nonnull data,
                int len, int width) __leaf;

void sb_add_lstr_b64(sb_t * nonnull sb, lstr_t data, int width);

/** Decode data from base64.
 *
 * \param[out]  sb  the output buffer, receiving decoded data.
 * \param[in]  data  data to decode.
 * \param[in]  len  number of bytes to decode.
 * \return 0 on success, -1 on error. The sb is unchanged on error.
 */
int  sb_add_unb64(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
static inline void sb_adds_b64(sb_t * nonnull sb, const char * nonnull s,
                               int width)
{
    sb_add_b64(sb, s, strlen(s), width);
}
static inline void sb_addlstr_b64(sb_t * nonnull sb, lstr_t s, int width)
{
    sb_add_b64(sb, s.s, s.len, width);
}
__SB_DEFINE_ADDS_ERR(unb64, "DecodingBase64");

/** base64url encoder/decoder.
 *
 * base64url is a variant of base64 where '+' and '/' are respectively
 * replaced by '-' and '_', making it suitable for a use in URLs and file
 * names.
 */

#define sb_add_b64url_start  sb_add_b64_start

void sb_add_b64url_update(sb_t * nonnull sb, const void * nonnull src0,
                          int len, sb_b64_ctx_t * nonnull ctx) __leaf;
void sb_add_b64url_finish(sb_t * nonnull sb, sb_b64_ctx_t * nonnull ctx)
    __leaf;

void sb_add_b64url(sb_t * nonnull sb, const void * nonnull data,
                   int len, int width) __leaf;

void sb_add_lstr_b64url(sb_t * nonnull sb, lstr_t data, int width);

int  sb_add_unb64url(sb_t * nonnull sb, const void * nonnull data, int len)
    __leaf;
static inline void sb_adds_b64url(sb_t * nonnull sb, const char * nonnull s,
                                  int width)
{
    sb_add_b64url(sb, s, strlen(s), width);
}
__SB_DEFINE_ADDS_ERR(unb64url, "DecodingBase64Url");

/** Append the CSV-escaped version of the string in the given sb.
 *
 * If the input string does not contain any '\n', '\t', '"' or \p sep, it is
 * appended as-is.
 *
 * Otherwise, it is double-quoted, and the double-quotes of the content of the
 * string (and only them) are escaped with double-quotes.
 */
void sb_add_csvescape(sb_t * nonnull sb, int sep, const void * nonnull data,
                      int len) __leaf;
static inline void sb_adds_csvescape(sb_t * nonnull sb, int sep,
                                     const char * nonnull s)
{
    sb_add_csvescape(sb, sep, s, strlen(s));
}
static inline void sb_add_lstr_csvescape(sb_t * nonnull sb, int sep, lstr_t s)
{
    sb_add_csvescape(sb, sep, s.s, s.len);
}

/** Append the Punycode-encoded string corresponding to the input code points
 *  in the given sb.
 *
 * This function computes the Punycode encoding of the input code points as
 * specified in RFC 3492, and appends it to the given string buffer.
 *
 * \param[out] sb  the string buffer in which the Punycode is appended.
 * \param[in]  code_points  array of the input code points.
 * \param[in]  nbcode_points  number of input code points.
 */
int sb_add_punycode_vec(sb_t * nonnull sb,
                        const uint32_t * nonnull code_points,
                        int nb_code_points) __leaf;

/** Append the Punycode-encoded string corresponding to the input UFT8 string.
 *
 * This function computes the Punycode encoding of the input UTF8 string as
 * specified in RFC 3492, and appends it to the given string buffer.
 *
 * \param[out] sb  the string buffer in which the Punycode is appended.
 * \param[in]  src  input UTF8 string to encode.
 * \param[in]  src_len  length (in bytes) of the input UTF8 string.
 */
int sb_add_punycode_str(sb_t * nonnull sb, const char * nonnull src,
                        int src_len) __leaf;


enum idna_flags_t {
    IDNA_USE_STD3_ASCII_RULES = 1 << 0, /* UseSTD3ASCIIRules                */
    IDNA_ALLOW_UNASSIGNED     = 1 << 1, /* AllowUnassigned                  */
    IDNA_ASCII_TOLOWER        = 1 << 2, /* Lower characters of ASCII labels */
};

/** Append the IDNA-encoded string corresponding to the input UTF8 domain
 * name.
 *
 * This function computes the IDNA-encoded version of the input UTF8 domain
 * name as specified in RFC 3490, and appends it to the given string buffer.
 *
 * \param[out] sb  the output string buffer.
 * \param[in]  src  input UTF8 domain name string to encode.
 * \param[in]  src_len  length (in bytes) of the input UTF8 string.
 * \param[in]  flags    encoding flags (bitfield of idna_flags_t.
 *
 * \return  the (positive) number of encoded labels on success, -1 on failure.
 */
int sb_add_idna_domain_name(sb_t * nonnull sb, const char * nonnull src,
                            int src_len, unsigned flags);

/**************************************************************************/
/* charset conversions (when implicit, charset is utf8)                   */
/**************************************************************************/

void sb_conv_from_latin1(sb_t * nonnull sb, const void * nonnull s, int len)
    __leaf;
void sb_conv_from_latin9(sb_t * nonnull sb, const void * nonnull s, int len)
    __leaf;
int  sb_conv_to_latin1(sb_t * nonnull sb, const void * nonnull s,
                       int len, int rep) __leaf;

/* ucs2 */
int  sb_conv_to_ucs2le(sb_t * nonnull sb, const void * nonnull s, int len)
    __leaf;
int  sb_conv_to_ucs2be(sb_t * nonnull sb, const void * nonnull s, int len)
    __leaf;
int  sb_conv_to_ucs2be_hex(sb_t * nonnull sb, const void * nonnull s, int len)
    __leaf;
int  sb_conv_from_ucs2be_hex(sb_t * nonnull sb, const void * nonnull s,
                             int slen) __leaf;
int  sb_conv_from_ucs2le_hex(sb_t * nonnull sb, const void * nonnull s,
                             int slen) __leaf;

typedef enum gsm_conv_plan_t {
    /* use only default gsm7 alphabet */
    GSM_DEFAULT_PLAN    = 0,
    /* use default gsm7 alphabet + extension table (escape mechanism) */
    GSM_EXTENSION_PLAN  = 1,

    GSM_CIMD_PLAN       = 2,
} gsm_conv_plan_t;
#define GSM_LATIN1_PLAN GSM_EXTENSION_PLAN

int  sb_conv_from_gsm_plan(sb_t * nullable sb, const void * nullable src,
                           int len, int plan) __leaf;
static inline int sb_conv_from_gsm(sb_t * nullable sb,
                                   const void * nullable src, int len)
{
    return sb_conv_from_gsm_plan(sb, src, len, GSM_EXTENSION_PLAN);
}

int  sb_conv_from_gsm_hex(sb_t * nullable sb, const void * nullable src,
                          int len) __leaf;
bool sb_conv_to_gsm_isok(const void * nonnull data, int len,
                         gsm_conv_plan_t plan) __leaf;
void sb_conv_to_gsm(sb_t * nonnull sb, const void * nonnull src, int len)
    __leaf;
void sb_conv_to_gsm_hex(sb_t * nonnull sb, const void * nonnull src, int len)
    __leaf;
void sb_conv_to_cimd(sb_t * nonnull sb, const void * nonnull src, int len)
    __leaf;

/* packed gsm */
int  gsm7_charlen(int c)
    __leaf;
int unicode_to_gsm7(int c, int unknown, gsm_conv_plan_t plan)
    __leaf;
int  sb_conv_to_gsm7(sb_t * nonnull sb, int gsm_start,
                     const char * nonnull utf8, int unknown,
                     gsm_conv_plan_t plan, int max_len) __leaf;
int  sb_conv_from_gsm7(sb_t * nonnull sb, const void * nonnull src,
                       int gsmlen, int udhlen) __leaf;
int gsm7_to_unicode(uint8_t u8, int unknown)
    __leaf;

/* normalisation */
int sb_normalize_utf8(sb_t * nonnull sb, const char * nonnull s, int len,
                      bool ci) __leaf;

/** append to \p sb the string describe by \p s with a lower case */
int sb_add_utf8_tolower(sb_t * nonnull sb, const char * nonnull s, int len);

/** append to \p sb the string describe by \p s with an upper case */
int sb_add_utf8_toupper(sb_t * nonnull sb, const char * nonnull s, int len);

/**************************************************************************/
/* misc helpers                                                           */
/**************************************************************************/

#define SB_FMT_ARG(sb)  (sb)->len, (sb)->data

#endif /* IS_LIB_COMMON_STR_BUF_H */
