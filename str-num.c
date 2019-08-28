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

#include "core.h"

static ALWAYS_INLINE int64_t
memtoip_impl(const byte *s, int _len, const byte **endp,
             const int64_t min, const int64_t max, bool ll, bool use_len)
{
    int64_t value = 0;

#define len ((use_len) ? _len : INT_MAX)
#define declen ((use_len) ? --_len : INT_MAX)

    if (!s || len < 0) {
        errno = EINVAL;
        goto done;
    }
    while (len && isspace((unsigned char)*s)) {
        s++;
        (void)declen;
    }
    if (!len) {
        errno = EINVAL;
        goto done;
    }
    if (*s == '-') {
        s++;
        (void)declen;
        if (!len || !isdigit((unsigned char)*s)) {
            errno = EINVAL;
            goto done;
        }
        value = '0' - *s++;
        while (declen && isdigit((unsigned char)*s)) {
            int digit = '0' - *s++;
            if ((value <= min / 10)
                &&  (value < min / 10 || digit < min % 10)) {
                errno = ERANGE;
                value = min;
                /* keep looping inefficiently in case of overflow */
            } else {
                value = value * 10 + digit;
            }
        }
    } else {
        if (*s == '+') {
            s++;
            (void)declen;
        }
        if (!len || !isdigit((unsigned char)*s)) {
            errno = EINVAL;
            goto done;
        }
        value = *s++ - '0';
        while (declen && isdigit((unsigned char)*s)) {
            int digit = *s++ - '0';
            if ((value >= max / 10)
                &&  (value > max / 10 || digit > max % 10)) {
                errno = ERANGE;
                value = max;
                /* keep looping inefficiently in case of overflow */
            } else {
                value = value * 10 + digit;
            }
        }
    }
done:
    if (endp) {
        *endp = s;
    }
    return value;
#undef len
#undef declen
}

int strtoip(const char *s, const char **endp)
{
    return memtoip_impl((const byte *)s, 0, (const byte **)(void *)endp,
                        INT_MIN, INT_MAX, false, false);
}

int memtoip(const void *s, int len, const byte **endp)
{
    return memtoip_impl(s, len, endp, INT_MIN, INT_MAX, false, true);
}

int64_t memtollp(const void *s, int len, const byte **endp)
{
    return memtoip_impl(s, len, endp, INT64_MIN, INT64_MAX, true, true);
}

#define INVALID_NUMBER  INT64_MIN
int64_t parse_number(const char *str)
{
    int64_t value;
    int64_t mult = 1;
    int frac = 0;
    int denom = 1;
    int exponent;

    value = strtoll(str, &str, 0);
    if (*str == '.') {
        for (str++; isdigit((unsigned char)*str); str++) {
            if (denom <= (INT_MAX / 10)) {
                frac = frac * 10 + *str - '0';
                denom *= 10;
            }
        }
    }
    switch (*str) {
      case 'P':
        mult <<= 10;
        /* FALL THRU */
      case 'T':
        mult <<= 10;
        /* FALL THRU */
      case 'G':
        mult <<= 10;
        /* FALL THRU */
      case 'M':
        mult <<= 10;
        /* FALL THRU */
      case 'K':
        mult <<= 10;
        str++;
        break;
      case 'p':
        mult *= 1000;
        /* FALL THRU */
      case 't':
        mult *= 1000;
        /* FALL THRU */
      case 'g':
        mult *= 1000;
        /* FALL THRU */
      case 'm':
        mult *= 1000;
        /* FALL THRU */
      case 'k':
        mult *= 1000;
        str++;
        break;
      case 'E':
      case 'e':
        exponent = strtol(str + 1, &str, 10);
        for (; exponent > 0; exponent--) {
            if (mult > (INT64_MAX / 10))
                return INVALID_NUMBER;
            mult *= 10;
        }
        break;
    }
    if (*str != '\0') {
        return INVALID_NUMBER;
    }
    /* Catch most overflow cases */
    if ((value | mult) > INT32_MAX && INT64_MAX / mult > value) {
        return INVALID_NUMBER;
    }
    return value * mult + frac * mult / denom;
}

static bool mem_startswith_minus(const void *p, int len)
{
    pstream_t ps = ps_init(p, len);

    ps_ltrim(&ps);
    return ps_peekc(ps) == '-';
}

uint64_t memtoullp(const void *s, int len, const byte **endp)
{
    t_scope;
    const char *str = t_dupz(s, len);
    const char *tail;
    uint64_t res;

    if (!len) {
        errno = EINVAL;
        return 0;
    }

    res = strtoull(str, &tail, 10);
    if ((int64_t)res < 0 && mem_startswith_minus(str, tail - str)) {
        errno = ERANGE;
        return 0;
    }
    if (endp) {
        *endp = (const byte *)s + (tail - str);
    }

    return res;
}

/** Parses a string to extract a long, checking some constraints.
 * <code>res</code> points to the destination of the long value
 * <code>p</code> points to the string to parse
 * <code>endp</code> points to the end of the parse (the next char to
 *   parse, after the value. spaces after the value are skipped if
 *   STRTOLP_IGNORE_SPACES is set)
 * <code>min</code> and <code>max</code> are extrema values (only checked
 *   if STRTOLP_CHECK_RANGE is set.
 * <code>dest</code> of <code>size</code> bytes.
 *
 * If STRTOLP_IGNORE_SPACES is set, leading and trailing spaces are
 * considered normal and skipped. If not set, then even leading spaces
 * lead to a failure.
 *
 * If STRTOLP_CHECK_END is set, the end of the value is supposed to
 * be the end of the string.
 *
 * @returns 0 if all constraints are met. Otherwise returns a negative
 * value corresponding to the error
 */
int strtolp(const char *p, const char **endp, int base, long *res,
            int flags, long min, long max)
{
    const char *end;
    long value;
    bool clamped;

    if (!endp) {
        endp = &end;
    }
    if (!res) {
        res = &value;
    }
    if (flags & STRTOLP_IGNORE_SPACES) {
        p = skipspaces(p);
    } else {
        if (isspace((unsigned char)*p))
            return -EINVAL;
    }
    errno = 0;
    *res = strtol(p, endp, base);
    if (flags & STRTOLP_IGNORE_SPACES) {
        *endp = skipspaces(*endp);
    }
    if ((flags & STRTOLP_CHECK_END) && **endp != '\0') {
        return -EINVAL;
    }
    if (*endp == p && !(flags & STRTOLP_EMPTY_OK)) {
        return -EINVAL;
    }
    clamped = false;
    if (flags & STRTOLP_CLAMP_RANGE) {
        if (*res < min) {
            *res = min;
            clamped = true;
        } else
        if (*res > max) {
            *res = max;
            clamped = true;
        }
        if (errno == ERANGE)
            errno = 0;
    }
    if (errno) {
        return -errno;  /* -ERANGE or maybe EINVAL as checked by strtol() */
    }
    if (flags & STRTOLP_CHECK_RANGE) {
        if (clamped || *res < min || *res > max) {
            return -ERANGE;
        }
    }
    return 0;
}

/*{{{ integer extraction with iop extensions */

static
int str_read_number_extension(const void **p, int len, uint64_t *out)
{
    uint64_t mult = 1;
    const char *s = *(const char **)p;
    int res = 0;

    if (!len)
        goto end;

    switch (*s) {
      /* times */
      case 'w':
        mult *= 7;
        /* FALLTHROUGH */
      case 'd':
        mult *= 24;
        /* FALLTHROUGH */
      case 'h':
        mult *= 60;
        /* FALLTHROUGH */
      case 'm':
        mult *= 60;
        /* FALLTHROUGH */
      case 's':
        mult *= 1;
        res = 1;
        break;

      /* sizes */
      case 'T':
        mult *= 1024;
        /* FALLTHROUGH */
      case 'G':
        mult *= 1024;
        /* FALLTHROUGH */
      case 'M':
        mult *= 1024;
        /* FALLTHROUGH */
      case 'K':
        mult *= 1024;
        res = 1;
        break;

      default:
        if (isalpha(*s))
            return -1;
        goto end;
    }

    *p = ++s;

    if (--len && isalnum(*s))
        return -1;

  end:
    *out = mult;
    return res;
}

static int
str_apply_number_extension(uint64_t mult, bool is_signed, uint64_t *number)
{
    if (is_signed) {
        int64_t signed_number = *(int64_t *)number;

        /* XXX: here mult <= INT64_MAX by implementation */

        if (signed_number > INT64_MAX / (int64_t)mult) {
            *number = INT64_MAX;
            return -1;
        }

        if (signed_number < INT64_MIN / (int64_t)mult) {
            *number = INT64_MIN;
            return -1;
        }
    } else
    if (*number > UINT64_MAX / mult) {
        *number = UINT64_MAX;
        return -1;
    }

    *number *= mult;

    return 0;
}

static int
memtoxll_ext(const void *p, int len, bool is_signed, uint64_t *out,
             const void **endp, int base, bool use_len)
{
    t_scope;
    uint64_t mult = 1;
    int ext_count;
    const char *s = p;
    const char *tail;
    const void *local_endp;
    int res;

    if (!endp)
        endp = &local_endp;

    *endp = p;
    errno = 0;

    if (use_len) {
        char last_char;

        if (len < 0) {
            errno = EINVAL;
            return -1;
        }
        if (!len) {
            *out = 0;
            return 0;
        }

        last_char = s[len - 1];
        if (isalnum(last_char) || last_char == '+' || last_char == '-') {
            s = t_dupz(p, len);
        }
    } else {
        len = INT_MAX;
    }

    if (is_signed) {
        *out = strtoll(s, &tail, base);
    } else {
        uint64_t val = strtoull(s, &tail, base);

        if ((int64_t)val < 0 && mem_startswith_minus(s, tail - s)) {
            errno = ERANGE;
            return -1;
        }
        *out = val;
    }

    res = tail - s;
    *endp = (const char *)p + res;

    if (res == 0) {
        errno = EINVAL;
    }
    if (errno)
        return -1;

    len -= res;

    if ((ext_count = str_read_number_extension(endp, len, &mult)) < 0) {
        errno = EDOM;
        return -1;
    }
    res += ext_count;

    if (str_apply_number_extension(mult, is_signed, out) < 0) {
        errno = ERANGE;
        return -1;
    }

    return res;
}

int
memtoll_ext(const void *p, int len, int64_t *out, const void **endp, int base)
{
    return memtoxll_ext(p, len, true, (uint64_t *)out, endp, base, true);
}

int
memtoull_ext(const void *p, int len, uint64_t *out, const void **endp,
             int base)
{
    return memtoxll_ext(p, len, false, out, endp, base, true);
}

int strtoll_ext(const char *s, int64_t *out, const char **tail, int base)
{
    return memtoxll_ext(s, 0, true, (uint64_t *)out, (const void **)tail,
                        base, false);
}

int strtoull_ext(const char *s, uint64_t *out, const char **tail, int base)
{
    return memtoxll_ext(s, 0, false, out, (const void **)tail, base, false);
}

/*}}} */
