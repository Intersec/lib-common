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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_STR_CONV_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_STR_CONV_H

extern uint8_t const __str_digit_value[128 + 256];
extern char const __str_digits_upper[36];
extern char const __str_digits_lower[36];

extern uint32_t const __utf8_offs[6];
extern uint8_t  const __utf8_clz_to_charlen[32];
extern uint8_t  const __utf8_mark[7];
extern uint8_t  const __utf8_char_len[32];

extern uint16_t const __str_unicode_upper[512];
extern uint16_t const __str_unicode_lower[512];
extern uint32_t const __str_unicode_general_ci[512];
extern uint32_t const __str_unicode_general_cs[512];

#define STR_COLLATE_MASK      0xffff
#define STR_COLLATE_SHIFT(c)  ((unsigned)(c) >> 16)

/****************************************************************************/
/* Base 36 stuff                                                            */
/****************************************************************************/

static inline int str_digit_value(int x)
{
    if (__builtin_constant_p(x)) {
        switch (x) {
          case '0' ... '9': return x - '0';
          case 'a' ... 'z': return 10 + x - 'a';
          case 'A' ... 'Z': return 10 + x - 'A';
          default:          return -1;
        }
    } else {
        return __str_digit_value[x + 128];
    }
}
static inline int hexdigit(int x)
{
    if (__builtin_constant_p(x)) {
        switch (x) {
          case '0' ... '9': return x - '0';
          case 'a' ... 'f': return 10 + x - 'a';
          case 'A' ... 'F': return 10 + x - 'A';
          default:          return -1;
        }
    } else {
        int i = __str_digit_value[x + 128];
        return i < 16 ? i : -1;
    }
}
static inline int hexdecode(const char * nonnull str)
{
    int h = hexdigit(str[0]);

    return (h < 0) ? h : ((h << 4) | hexdigit(str[1]));
}

/* XXX: dest will not be NUL terminated in strconv_hexdecode*/
int strconv_hexdecode(void * nonnull dest, int size,
                      const char * nonnull src, int len)
    __leaf;
int strconv_hexencode(char * nonnull dest, int size,
                      const void * nonnull src, int len)
    __leaf;

/****************************************************************************/
/* utf-8 and charset conversions                                            */
/****************************************************************************/

static inline int unicode_toupper(int c)
{
    return (unsigned)c < countof(__str_unicode_upper) ?
            __str_unicode_upper[c] : c;
}

static inline int unicode_tolower(int c)
{
    return (unsigned)c < countof(__str_unicode_lower) ?
            __str_unicode_lower[c] : c;
}

static inline uint8_t __pstrputuc(char * nonnull dst, int32_t c)
{
    uint8_t len;

    if (c < 0x80) {
        *dst = c;
        return 1;
    }
    if (__builtin_constant_p(c)) {
        if (c >= 0 && c < 0x200000) {
            len = 1 + (c >= 0x80) + (c >= 0x800) + (c >= 0x10000);
        } else {
            len = 0;
        }
    } else {
        /* XXX: 31 ^ clz(c) is actually bsr in x86 assembly */
        len = __utf8_clz_to_charlen[31 ^ __builtin_clz(c | 1)];
    }
    switch (__builtin_expect(len, 2)) {
      default: dst[3] = (c | 0x80) & 0xbf; c >>= 6;
               /* FALLTHROUGH */
      case 3:  dst[2] = (c | 0x80) & 0xbf; c >>= 6;
               /* FALLTHROUGH */
      case 2:  dst[1] = (c | 0x80) & 0xbf; c >>= 6;
               dst[0] = (c | __utf8_mark[len]);
               /* FALLTHROUGH */
      case 0:  break;
    }
    return len;
}

/* XXX 0 means invalid utf8 char */
static inline uint8_t utf8_charlen(const char * nonnull s, int len)
{
    uint8_t charlen = __utf8_char_len[(unsigned char)*s >> 3];

    if (len >= 0 && unlikely(len < charlen)) {
        return 0;
    }

    switch (charlen) {
      default: if (unlikely((*++s & 0xc0) != 0x80)) return 0;
               /* FALLTHROUGH */
      case 3:  if (unlikely((*++s & 0xc0) != 0x80)) return 0;
               /* FALLTHROUGH */
      case 2:  if (unlikely((*++s & 0xc0) != 0x80)) return 0;
               /* FALLTHROUGH */
      case 1:  return charlen;
      case 0:  return charlen;
    }
}

/** Get the number of UTF8 characters contained in a string.
 *
 * \return -1 in case of invalid UTF8.
 */
static inline ssize_t utf8_strnlen(const char * nonnull s, size_t len)
{
    const char *end = s + len;

    len = 0;
    while (s < end) {
        uint8_t charlen = utf8_charlen(s, end - s);

        if (unlikely(charlen == 0)) {
            return -1;
        }
        len++;
        s += charlen;
    }

    return len;
}

/** Get the number of UTF8 characters contained in a string.
 *
 * \return -1 in case of invalid UTF8.
 */
static inline ssize_t utf8_strlen(const char * nonnull s)
{
    return utf8_strnlen(s, strlen(s));
}

static inline int utf8_getc_slow(const char * nonnull s,
                                 const char * nullable * nullable out)
{
    uint32_t ret = 0;
    uint8_t  len = utf8_charlen(s, -1) - 1;

    switch (len) {
      default: return -1;
      case 3:  ret += (unsigned char)*s++; ret <<= 6;
               /* FALLTHROUGH */
      case 2:  ret += (unsigned char)*s++; ret <<= 6;
               /* FALLTHROUGH */
      case 1:  ret += (unsigned char)*s++; ret <<= 6;
               /* FALLTHROUGH */
      case 0:  ret += (unsigned char)*s++;
               /* FALLTHROUGH */
    }

    if (out) {
        *out = s;
    }

    return ret - __utf8_offs[len];
}

static ALWAYS_INLINE int utf8_getc(const char * nonnull s,
                                   const char * nullable * nullable out)
{
    if ((unsigned char)*s < 0x80) {
        if (out) {
            *out = s + 1;
        }
        return (unsigned char)*s;
    } else {
        return utf8_getc_slow(s, out);
    }
}

static ALWAYS_INLINE int utf8_ngetc(const char * nonnull s, int len,
                                    const char * nullable * nullable out)
{
    if (len && (unsigned char)*s < 0x80) {
        if (out) {
            *out = s + 1;
        }
        return (unsigned char)*s;
    }
    if (unlikely(len < __utf8_char_len[(unsigned char)*s >> 3])) {
        return -1;
    }
    return utf8_getc_slow(s, out);
}

static ALWAYS_INLINE int utf8_ngetc_at(const char * nonnull s, int len,
                                       int * nonnull offp)
{
    int off = *offp;
    const char *out = NULL;
    int res;

    if (off < 0 || off >= len) {
        return -1;
    }
    s   += off;
    len -= off;
    res = RETHROW(utf8_ngetc(s, len, &out));

    *offp += (out - s);
    return res;
}

static inline int utf8_vgetc(char * nonnull s, char * nullable * nullable out)
{
    return utf8_getc(s, (const char **)out);
}
static inline const char * nonnull utf8_skip_valid(const char * nonnull s,
                                                   const char * nonnull end)
{
    while (s < end) {
        if (utf8_ngetc(s, end - s, &s) < 0)
            return s;
    }
    return end;
}

/** Return utf8 case-insensitive collating comparison as -1, 0, 1.
 *
 * \param[in] strip trailing spaces are ignored for comparison.
 */
int utf8_stricmp(const char * nonnull str1, int len1,
                 const char * nonnull str2, int len2, bool strip) __leaf;

/** Return utf8 case-senstive collating comparison as -1, 0 or 1.
 *
 * \param[in] strip trailing spaces are ignored for comparison.
 */
int utf8_strcmp(const char * nonnull str1, int len1,
                const char * nonnull str2, int len2, bool strip) __leaf;

/** Return whether \p str1 starts with \p str2 utf8 case-insensitive way.
 */
int utf8_str_istartswith(const char * nonnull str1, int len1,
                         const char * nonnull str2, int len2);

/** Return whether \p str1 starts with \p str2 utf8 case-sensitive way.
 */
int utf8_str_startswith(const char * nonnull str1, int len1,
                        const char * nonnull str2, int len2);

static inline
bool utf8_striequal(const char * nonnull str1, int len1,
                    const char * nonnull str2, int len2, bool strip)
{
    return utf8_stricmp(str1, len1, str2, len2, strip) == 0;
}

static inline
bool utf8_strequal(const char * nonnull str1, int len1,
                   const char * nonnull str2, int len2, bool strip)
{
    return utf8_strcmp(str1, len1, str2, len2, strip) == 0;
}

#endif /* IS_LIB_COMMON_STR_CONV_H */
