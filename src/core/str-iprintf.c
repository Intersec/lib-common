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

#include <endian.h>
#include <lib-common/core.h>

/* This code only works on regular architectures: we assume
 *  - integer types are either 32 bit or 64 bit long.
 *  - bytes (char type) have 8 bits
 *  - integer representation is 2's complement, no padding, no traps
 *
 * imported functions:
 *   stdio.h:    stdout, putc_unlocked, fwrite_unlocked;
 */

#define FLOATING_POINT  1
#define _NO_LONGDBL     1

/*---------------- formatter ----------------*/

#define FLAG_UPPER      0x0001
#define FLAG_MINUS      0x0002
#define FLAG_PLUS       0x0004
#define FLAG_SPACE      0x0008
#define FLAG_ALT        0x0010
#define FLAG_QUOTE      0x0020
#define FLAG_ZERO       0x0040

#define FLAG_WIDTH      0x0080
#define FLAG_PREC       0x0100

#define TYPE_int        0
#define TYPE_char       1
#define TYPE_short      2

#if LONG_MAX == INT_MAX
#define TYPE_long       TYPE_int
#define convert_ulong   convert_uint
#else
#define WANT_long       1
#define TYPE_long       3
#endif

#ifndef LLONG_MAX
#if defined(LONG_LONG_MAX)
#define LLONG_MAX       LONG_LONG_MAX
#elif defined(__LONG_LONG_MAX__)
#define LLONG_MAX       __LONG_LONG_MAX__
#else
#error "unsupported architecture: need LLONG_MAX"
#endif
#endif

#if LLONG_MAX == LONG_MAX
#define TYPE_llong      TYPE_long
#define convert_ullong  convert_ulong
#else
#define WANT_llong      1
#define TYPE_llong      4
#endif

#if INTMAX_MAX == LONG_MAX
#define TYPE_intmax_t   TYPE_long
#elif INTMAX_MAX == LLONG_MAX
#define TYPE_intmax_t   TYPE_llong
#else
#error "unsupported architecture: unsupported INTMAX_MAX"
#endif

#ifndef INT32_MAX
#error "unsupported architecture"
#elif INT32_MAX == INT_MAX
#define TYPE_int32      TYPE_int
#define convert_uint32  convert_uint
#elif INT32_MAX == LONG_MAX
#define TYPE_int32      TYPE_long
#define convert_uint32  convert_ulong
#else
#define WANT_int32      1
#define TYPE_int32      5
#endif

#ifndef INT64_MAX
#error "unsupported architecture: need INT64_MAX"
#elif INT64_MAX == INT_MAX
#define TYPE_int64      TYPE_int
#define convert_uint64  convert_uint
#elif INT64_MAX == LONG_MAX
#define TYPE_int64      TYPE_long
#define convert_uint64  convert_ulong
#elif INT64_MAX == LLONG_MAX
#define TYPE_int64      TYPE_llong
#define convert_uint64  convert_ullong
#else
#define WANT_int64      1
#define TYPE_int64      6
#endif

#if SIZE_MAX == UINT32_MAX
#define TYPE_size_t     TYPE_int32
#elif SIZE_MAX == UINT64_MAX
#define TYPE_size_t     TYPE_int64
#else
#error "unsupported architecture: unsupported SIZE_MAX"
#endif

#if PTRDIFF_MAX == INT32_MAX
#define TYPE_ptrdiff_t   TYPE_int32
#elif PTRDIFF_MAX == INT64_MAX
#define TYPE_ptrdiff_t   TYPE_int64
#else
#error "unsupported architecture: unsupported PTRDIFF_MAX"
#endif

#if UINTPTR_MAX == UINT32_MAX
#define convert_uintptr  convert_uint32
#elif UINTPTR_MAX == UINT64_MAX
#define convert_uintptr  convert_uint64
#else
#error "unsupported architecture: unsupported UINTPTR_MAX"
#endif

/* Consider 'L' and 'll' synonyms for compatibility */
#define TYPE_ldouble    TYPE_llong

#if 0
/* No longer need this obsolete stuff */
#define TYPE_far        8
#define TYPE_near       9
#endif

#ifdef FLOATING_POINT

#include <math.h>

#define strtod    gt_strtod
#define atof      gt_atof
#define ecvt      gt_ecvt
#define dtoa      gt_dtoa
#define _dtoa_r   gt_dtoa_r
#define _ldtoa_r  gt_ldtoa_r
#define _ldcheck  gt_ldcheck
#define freedtoa  gt_freedtoa

#ifdef _NO_LONGDBL
/* 11-bit exponent (VAX G floating point) is 308 decimal digits */
#define MAXEXP          308
#else  /* !_NO_LONGDBL */
/* 15-bit exponent (Intel extended floating point) is 4932 decimal digits */
#define MAXEXP          4932
#endif /* !_NO_LONGDBL */
/* 128 bit fraction takes up 39 decimal digits; max reasonable precision */
#define MAXFRACT        39

#define BUF             (MAXEXP+MAXFRACT+1)     /* + decimal point */
#define DEFPREC         6

#ifdef _NO_LONGDBL
static char *cvt(double, int, int, char *, int *, int, int *);
#else
static char *cvt(_LONG_DOUBLE, int, int, char *, int *, int, int *);
extern int _ldcheck(_LONG_DOUBLE *);
#endif

static int exponent(char *, int, int);

double strtod(const char *s00, char **se);
char *dtoa(double d, int mode, int ndigits, int *decpt, int *sign, char **rve);
#else

#define BUF             40

#endif /* FLOATING_POINT */


/*---------------- helpers ----------------*/

static ALWAYS_INLINE char *convert_int10(char *p, int value)
{
    /* compute absolute value without tests */
    unsigned int bits = value >> (bitsizeof(int) - 1);
    unsigned int num = (value ^ bits) + (bits & 1);

    while (num >= 10) {
        *--p = '0' + (num % 10);
        num = num / 10;
    }
    *--p = '0' + num;
    if (value < 0) {
        *--p = '-';
    }
    return p;
}

static ALWAYS_INLINE
char *convert_uint(char *p, unsigned int value, int base)
{
    if (base == 10) {
        while (value > 0) {
            *--p = '0' + (value % 10);
            value = value / 10;
        }
    } else
    if (base == 16) {
        while (value > 0) {
            *--p = '0' + (value % 16);
            value = value / 16;
        }
    } else {
        while (value > 0) {
            *--p = '0' + (value % 8);
            value = value / 8;
        }
    }
    return p;
}

static ALWAYS_INLINE char *
convert_uint_10_8_0(char *p, unsigned int value)
{
    int i;

    for (i = 7; i-- > 0; ) {
        *--p = '0' + (value % 10);
        value /= 10;
    }
    *--p = '0' + value;
    return p;
}

#ifndef convert_ulong
static char *convert_ulong(char *p, unsigned long value, int base)
{
    if (base == 10) {
        while (value > UINT_MAX) {
            unsigned long quot = value / 100000000;
            unsigned int rem = value - quot * 100000000;
            value = quot;
            p = convert_uint_10_8_0(p, rem);
        }
        return convert_uint(p, value, 10);
    } else
    if (base == 16) {
        while (value > 0) {
            *--p = '0' + (value % 16);
            value = value / 16;
        }
    } else {
        while (value > 0) {
            *--p = '0' + (value % 8);
            value = value / 8;
        }
    }
    return p;
}
#endif

#ifndef convert_ullong
static char *convert_ullong(char *p, unsigned long long value, int base)
{
    if (base == 10) {
        while (value > UINT_MAX) {
            unsigned long long quot = value / 100000000;
            unsigned int rem = value - quot * 100000000;
            value = quot;
            p = convert_uint_10_8_0(p, rem);
        }
        return convert_uint(p, value, 10);
    }
    if (base == 16) {
        while (value > 0) {
            *--p = '0' + (value % 16);
            value = value / 16;
        }
    } else {
        while (value > 0) {
            *--p = '0' + (value % 8);
            value = value / 8;
        }
    }
    return p;
}
#endif

#ifndef convert_uint32
static char *convert_uint32(char *p, uint32_t value, int base)
{
    while (value > 0) {
        *--p = '0' + (value % base);
        value = value / base;
    }
    return p;
}
#endif

#ifndef convert_uint64
static char *convert_uint64(char *p, uint64_t value, int base)
{
    while (value > 0) {
        *--p = '0' + (value % base);
        value = value / base;
    }
    return p;
}
#endif

static ALWAYS_INLINE
int fmt_output_chars(FILE *stream, char *str, size_t size,
                     size_t count, int c, ssize_t n)
{
    size_t n1 = n;

    if (n < 0)
        n1 = n = 0;

    if (stream) {
        while (n1-- > 0) {
            putc_unlocked(c, stream);
        }
    } else {
        if (count + n1 >= size) {
            n1 = count >= size ? 0 : size - count - 1;
        }
        if (n1) {
            memset(str + count, c, n1);
        }
    }
    return count + n;
}

static ssize_t fmt_output_raw(int modifier, const void *val, size_t val_len,
                              FILE *stream, char *buf, size_t buf_len)
{
    const char *lp = val;

    if (stream) {
        for (size_t i = 0; i < val_len; i++) {
            putc_unlocked(lp[i], stream);
        }
    } else {
        ssize_t len1 = MIN(val_len, buf_len);;

        memcpy(buf, lp, len1);
    }
    return val_len;
}

static ssize_t fmt_output_lstr(int modifier, const void *val, FILE *stream,
                               char *buf, size_t buf_len)
{
    const lstr_t *str = val;

    /* This function is used to format sb_t objects too, make sure they
     * both have the buf,len at the same offset */
    STATIC_ASSERT(offsetof(lstr_t, s) == offsetof(sb_t, data));
    STATIC_ASSERT(offsetof(lstr_t, len) == offsetof(sb_t, len));

    if (stream) {
        for (int i = 0; i < str->len; i++) {
            putc_unlocked(str->s[i], stream);
        }
    } else {
        ssize_t len1 = MIN((size_t)str->len, buf_len);

        memcpy(buf, str->s, len1);
    }

    return str->len;
}

static ssize_t fmt_output_hex(int modifier, const void *val, size_t val_len,
                              FILE *stream, char *buf, size_t buf_len)
{
    const char *digits;
    const char *lp = val;

    if (modifier == 'X') {
        digits = __str_digits_upper;
    } else {
        digits = __str_digits_lower;
    }

    if (stream) {
        for (size_t i = 0; i < val_len; i++) {
            putc_unlocked(digits[(lp[i] >> 4) & 0x0f], stream);
            putc_unlocked(digits[(lp[i] >> 0) & 0x0f], stream);
        }
    } else {
        size_t len1 = MIN(val_len * 2, buf_len);

        for (size_t i = 0; i < len1 / 2; i++) {
            buf[i * 2]     = digits[(lp[i] >> 4) & 0x0f];
            buf[(i * 2) + 1] = digits[(lp[i] >> 0) & 0x0f];
        }
        if (len1 & 1) {
            buf[len1 - 1] = digits[(lp[len1 / 2] >> 4) & 0x0f];
        }
    }
    return val_len * 2;
}

struct formatter_t {
    union {
        formatter_f *raw_formatter;
        pointer_formatter_f *ptr_formatter;
    };
    bool is_raw;
};

static struct formatter_t put_memory_fmt[256] = {
    ['M'] = { { .raw_formatter = &fmt_output_raw }, .is_raw = true },
    ['X'] = { { .raw_formatter = &fmt_output_hex }, .is_raw = true },
    ['x'] = { { .raw_formatter = &fmt_output_hex }, .is_raw = true },
    ['L'] = { { .ptr_formatter = &fmt_output_lstr }, .is_raw = false }
};

static ALWAYS_INLINE
ssize_t fmt_output_chunk(FILE *stream, char *str, size_t size,
                         size_t count, const char *lp, size_t len,
                         int modifier)
{
    const struct formatter_t *format;
    size_t out_len;

    size = count >= size ? 0 : size - count - 1;
    str += count;
    if (likely(modifier == 'M')) {
        out_len = RETHROW(fmt_output_raw(modifier, lp, len, stream,
                                         str, size));
    } else {
        format = &put_memory_fmt[(unsigned char)modifier];

        if (format->is_raw) {
            if (!expect(format->raw_formatter)) {
                return -1;
            }

            out_len = RETHROW((*format->raw_formatter)(modifier, lp, len,
                                                       stream, str, size));
        } else {
            if (!expect(format->ptr_formatter)) {
                return -1;
            }
            out_len = RETHROW((*format->ptr_formatter)(modifier, lp, stream,
                                                       str, size));
        }
    }

    return count + out_len;
}

static int fmt_output(FILE *stream, char *str, size_t size,
                      const char *format, va_list ap)
{
    char buf[(64 + 2) / 3 + 1 + 1];
    int c, count, len, width, prec, base, flags, type_flags;
    int left_pad, prefix_len, zero_pad, right_pad;
    const char *lp;
    int sign;
    int save_errno = errno;

    if (size > INT_MAX) {
        size = 0;
    }

    if (!format) {
        /* set output to empty string out of compatibility with side
         * effect in glibc.  NULL format is really an error
         */
        if (!stream && size > 0) {
            str[0] = '\0';
        }
        errno = EINVAL;
        return -1;
    }

    count = 0;
    right_pad = 0;

#if 0
    /* Lock stream.  */
    _IO_cleanup_region_start ((void (*) (void *)) &_IO_funlockfile, s);
    _IO_flockfile (s);
#endif

    for (;;) {
        /* Modifier 'M' is for 'put memory', this is the modifier to use to
         * put raw data in the output stream/buffer */
        int modifier = 'M';

        for (lp = format; *format && *format != '%'; format++)
            continue;
        len = format - lp;
      haslp:
        count = fmt_output_chunk(stream, str, size, count, lp, len, modifier);
        modifier  = 'M';
        if (right_pad) {
            count = fmt_output_chars(stream, str, size, count, ' ', right_pad);
        }

        right_pad = 0;

        if (*format == '\0')
            goto done;

        if (*format != '%')
            continue;

        format++;

        /* special case naked %d and %s formats */
        if (*format == 'd') {
            format++;
            lp = convert_int10(buf + sizeof(buf), va_arg(ap, int));
            len = buf + sizeof(buf) - lp;
            goto haslp;
        }
        if (*format == 's') {
            format++;
            lp = va_arg(ap, const char *);
            if (lp == NULL)
                lp = "(null)";
            len = strlen(lp);
            goto haslp;
        }
        /* also special case %.*s */
        if (format[0] == '.' && format[1] == '*' && format[2] == 's') {
            format += 3;
            len = va_arg(ap, int);
            lp = va_arg(ap, const char *);
            if (lp == NULL) {
                lp = "(null)";
                len = 6;
            }
            len = strnlen(lp, len);
            goto haslp;
        }

        /* also special case %*p?, where '?' is a registered modifier. We
         * natively support %*pM for "put memory content here"
         * and %*pX for "put hexadecimal content here".
         */
        if (format[0] == '*' && format[1] == 'p'
        &&  put_memory_fmt[(unsigned char)format[2]].is_raw
        &&  put_memory_fmt[(unsigned char)format[2]].raw_formatter)
        {
            modifier = format[2];
            format += 3;
            len = va_arg(ap, int);
            lp  = va_arg(ap, const char *);

            /* XXX No "trailing garbage" consumption: we support only single
             *     character modifiers for now.
             */
            goto haslp;
        } else
        if (format[0] == 'p'
        &&  !put_memory_fmt[(unsigned char)format[1]].is_raw
        &&  put_memory_fmt[(unsigned char)format[1]].ptr_formatter)
        {
            modifier = format[1];
            format += 2;
            len = 0;
            lp  = va_arg(ap, const char *);

            goto haslp;
        }

        /* general case: parse complete format syntax */
        flags = 0;

        /* parse optional flags */
        for (;; format++) {
            switch (*format) {
            case '\0': goto error;
            case '-':  flags |= FLAG_MINUS;  continue;
            case '+':  flags |= FLAG_PLUS;   continue;
            case '#':  flags |= FLAG_ALT;    continue;
            case '\'': flags |= FLAG_QUOTE;  continue;
            case ' ':  flags |= FLAG_SPACE;  continue;
            case '0':  flags |= FLAG_ZERO;   continue;
                       /* locale's alternative output digits */
            case 'I':  /* ignore this shit */;   continue;
            }
            break;
        }

        /* parse optional width */
        width = 0;
        if (*format == '*') {
            format++;
            flags |= FLAG_WIDTH;
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= FLAG_MINUS;
                width = -width;
            }
        } else
        if (*format >= '1' && *format <= '9') {
            flags |= FLAG_WIDTH;
            width = *format++ - '0';
            while (*format >= '0' && *format <= '9') {
                width = width * 10 + *format++ - '0';
            }
        }

        /* parse optional precision */
        prec = 1;
        if (*format == '.') {
            format++;
            prec = 0;
            //flags &= ~FLAG_ZERO;
            flags |= FLAG_PREC;
            if (*format == '*') {
                format++;
                prec = va_arg(ap, int);
                if (prec < 0) {
                    /* OG: should be treated as if precision were
                     * omitted, ie: prec = 1, flags &= ~FLAG_PREC
                     */
                    prec = 0;
                }
            } else
            if (*format >= '0' && *format <= '9') {
                prec = *format++ - '0';
                while (*format >= '0' && *format <= '9') {
                    prec = prec * 10 + *format++ - '0';
                }
            }
        }

        /* parse optional format modifiers */
        type_flags = 0;
        switch (*format) {
        case '\0':
            goto error;
#ifdef TYPE_far
        case 'F':
            type_flags |= TYPE_far;
            format++;
            break;
        case 'N':
            type_flags |= TYPE_near;
            format++;
            break;
#endif
        case 'l':
            if (format[1] == 'l') {
                format++;
                type_flags |= TYPE_llong;
            } else {
                type_flags |= TYPE_long;
            }
            format++;
            break;
        case 'h':
            if (format[1] == 'h') {
                format++;
                type_flags |= TYPE_char;
            } else {
                type_flags |= TYPE_short;
            }
            format++;
            break;
        case 'j':
            type_flags |= TYPE_intmax_t;
            format++;
            break;
        case 'z':
            type_flags |= TYPE_size_t;
            format++;
            break;
        case 't':
            type_flags |= TYPE_ptrdiff_t;
            format++;
            break;
        case 'L':
            type_flags |= TYPE_ldouble;
            format++;
            break;
        }

        /* dispatch on actual format character */
        switch (c = *format++) {
        case '\0':
            format--;
            goto error;

        case 'n':
#if 0
            /* Consume pointer to int from argument list, but ignore it */
            (void)va_arg(ap, int *);
#else
            /* The type of pointer defaults to int* but can be
             * specified with the TYPE_xxx prefixes */
            switch (type_flags) {
              case TYPE_char:
                *va_arg(ap, char *) = count;
                break;

              case TYPE_short:
                *va_arg(ap, short *) = count;
                break;

              case TYPE_int:
              default:
                *va_arg(ap, int *) = count;
                break;
#ifdef WANT_long
              case TYPE_long:
                *va_arg(ap, long *) = count;
                break;
#endif
#ifdef WANT_llong
              case TYPE_llong:
                *va_arg(ap, long long *) = count;
                break;
#endif
            }
#endif
            break;

        case 'c':
            /* ignore 'l' prefix for wide char converted with wcrtomb() */
            c = (unsigned char)va_arg(ap, int);
            goto has_char;

        case '%':
        default:
        has_char:
            lp = buf + sizeof(buf) - 1;
            buf[sizeof(buf) - 1] = c;
            len = 1;
            goto has_string_len;

        case 'm':
            lp = strerror(save_errno);
            goto has_string;

        case 's':
            /* ignore 'l' prefix for wide char string */
            lp = va_arg(ap, char *);
            if (lp == NULL) {
                lp = "(null)";
            }

        has_string:
            if (flags & FLAG_PREC) {
                len = strnlen(lp, prec);
            } else {
                len = strlen(lp);
            }

        has_string_len:
            prefix_len = zero_pad = 0;
            flags &= ~FLAG_ZERO;

            goto apply_final_padding;

        case 'd':
        case 'i':
            switch (type_flags) {
                int int_value;

              case TYPE_char:
                int_value = (char)va_arg(ap, int);
                goto convert_int;

              case TYPE_short:
                int_value = (short)va_arg(ap, int);
                goto convert_int;

              case TYPE_int:
                int_value = va_arg(ap, int);
              convert_int:
                {
                    unsigned int bits = int_value >> (bitsizeof(int_value) - 1);
                    unsigned int num = (int_value ^ bits) + (bits & 1);
                    sign = '-' & bits;
                    lp = convert_uint(buf + sizeof(buf), num, 10);
                    break;
                }
#ifdef WANT_long
              case TYPE_long:
                {
                    long value = va_arg(ap, long);
                    unsigned long bits = value >> (bitsizeof(value) - 1);
                    unsigned long num = (value ^ bits) + (bits & 1);
                    sign = '-' & bits;
                    lp = convert_ulong(buf + sizeof(buf), num, 10);
                    break;
                }
#endif
#ifdef WANT_llong
              case TYPE_llong:
                {
                    long long value = va_arg(ap, long long);
                    unsigned long long bits = value >> (bitsizeof(value) - 1);
                    unsigned long long num = (value ^ bits) + (bits & 1);
                    sign = '-' & bits;
                    lp = convert_ullong(buf + sizeof(buf), num, 10);
                    break;
                }
#endif
#ifdef WANT_int32
              case TYPE_int32:
                {
                    int32_t value = va_arg(ap, int32_t);
                    uint32_t bits = value >> (bitsizeof(value) - 1);
                    uint32_t num = (value ^ bits) + (bits & 1);
                    sign = '-' & bits;
                    lp = convert_uint32(buf + sizeof(buf), num, 10);
                    break;
                }
#endif
#ifdef WANT_int64
              case TYPE_int64:
                {
                    int64_t value = va_arg(ap, int64_t);
                    uint64_t bits = value >> (bitsizeof(value) - 1);
                    uint64_t num = (value ^ bits) + (bits & 1);
                    sign = '-' & bits;
                    lp = convert_uint64(buf + sizeof(buf), num, 10);
                    break;
                }
#endif
              default:
                {
                    /* do not know what to fetch, must ignore remaining
                     * formats specifiers.  This is really an error.
                     */
                    goto error;
                }
            }
            /* We may have the following parts to output:
             * - left padding with spaces    (left_pad)
             * - the optional sign char      (buf, prefix_len)
             * - 0 padding                   (zero_pad)
             * - the converted number        (lp, len)
             * - right padding with spaces   (right_pad)
             */

            prefix_len = zero_pad = 0;

            len = buf + sizeof(buf) - lp;

            if (len < prec) {
                if (prec == 1) {
                    /* special case number 0 */
                    *(char*)--lp = '0';
                    len++;
                } else {
                    zero_pad = prec - len;
                }
            }
            if (!sign) {
                if (flags & FLAG_PLUS) {
                    sign = '+';
                } else
                if (flags & FLAG_SPACE) {
                    sign = ' ';
                }
            }
            if (sign) {
                if (zero_pad == 0 && !(flags & FLAG_ZERO)) {
                    *(char*)--lp = sign;
                    len++;
                } else {
                    buf[0] = (char)sign;
                    prefix_len = 1;
                }
            }
            goto apply_final_padding;

        case 'P':
            flags |= FLAG_UPPER;
            /* fall thru */
        case 'p':
            flags |= FLAG_ALT;
            base = 16;
            if (unlikely(isalnum((unsigned char)*format))) {
                /* XXX Reserve each %[*]p[0-9a-zA-Z]+ format for our own usage
                 */
                e_trace(0, "trailing garbage after %%p format");
                do { format++; } while (isalnum((unsigned char)*format));
            }
            {
                void *vp = va_arg(ap, void *);

                if (vp == NULL) {
                    lp = "(nil)";
                    len = 5;
                    goto has_string_len;
                }
                /* Should share with has_unsigned switch code */
                lp = convert_uintptr(buf + sizeof(buf), (uintptr_t)vp, base);
                goto patch_unsigned_conversion;
            }

        case 'X':
            flags |= FLAG_UPPER;
            /* fall thru */
        case 'x':
            base = 16;
            goto has_unsigned;

        case 'o':
            base = 8;
            goto has_unsigned;

        case 'u':
            base = 10;

        has_unsigned:
            switch (type_flags) {
                int uint_value;

              case TYPE_char:
                uint_value = (unsigned char)va_arg(ap, unsigned int);
                goto convert_uint;

              case TYPE_short:
                uint_value = (unsigned short)va_arg(ap, unsigned int);
                goto convert_uint;

              case TYPE_int:
                uint_value = va_arg(ap, unsigned int);
              convert_uint:
                lp = convert_uint(buf + sizeof(buf), uint_value, base);
                break;
#ifdef WANT_long
              case TYPE_long:
                lp = convert_ulong(buf + sizeof(buf),
                                   va_arg(ap, unsigned long), base);
                break;
#endif
#ifdef WANT_llong
              case TYPE_llong:
                lp = convert_ullong(buf + sizeof(buf),
                                   va_arg(ap, unsigned long long), base);
                break;
#endif
#ifdef WANT_int32
              case TYPE_int32:
                lp = convert_uint32(buf + sizeof(buf),
                                    va_arg(ap, uint32_t), base);
                break;
#endif
#ifdef WANT_int64
              case TYPE_int64:
                lp = convert_uint64(buf + sizeof(buf),
                                    va_arg(ap, uint64_t), base);
                break;
#endif
              default:
                {
                    /* do not know what to fetch, must ignore remaining
                     * formats specifiers.  This is really an error.
                     */
                    goto error;
                }
            }

        patch_unsigned_conversion:
            /* We may have the following parts to output:
             * - left padding with spaces    (left_pad)
             * - the 0x or 0X prefix if any  (buf, prefix_len)
             * - 0 padding                   (zero_pad)
             * - the converted number        (lp, len)
             * - right padding with spaces   (right_pad)
             */

            prefix_len = zero_pad = 0;

            len = buf + sizeof(buf) - lp;

            if (base == 16) {
                char *p;
                int alpha_shift;

                if ((flags & FLAG_ALT) && len > 0) {
                    buf[0] = '0';
                    buf[1] = (flags & FLAG_UPPER) ? 'X' : 'x';
                    prefix_len = 2;
                }
                alpha_shift = (flags & FLAG_UPPER) ?
                              'A' - '9' - 1: 'a' - '9' - 1;

                for (p = (char*)lp; p < buf + sizeof(buf); p++) {
                    if (*p > '9') {
                        *p += alpha_shift;
                    }
                }
            }

            if (len < prec) {
                if (prec == 1) {
                    /* special case number 0 */
                    *(char*)--lp = '0';
                    len++;
                } else {
                    zero_pad = prec - len;
                }
            }

            if (base == 8) {
                if ((flags & FLAG_ALT) && zero_pad == 0 &&
                      (len == 0 || *lp != '0')) {
                    *(char*)--lp = '0';
                    len++;
                }
            }

        apply_final_padding:

            left_pad = 0;

            if (width > prefix_len + zero_pad + len) {
                if (flags & FLAG_MINUS) {
                    right_pad = width - prefix_len - zero_pad - len;
                } else
                if ((flags & (FLAG_ZERO | FLAG_PREC)) == FLAG_ZERO) {
                    zero_pad = width - prefix_len - len;
                } else {
                    left_pad = width - prefix_len - zero_pad - len;
                }
            }

            if (left_pad) {
                count = fmt_output_chars(stream, str, size, count,
                                         ' ', left_pad);
            }
            if (prefix_len) {
                /* prefix_len is 0, 1 or 2 */
                count = fmt_output_chunk(stream, str, size, count,
                                         buf, prefix_len, modifier);
            }
            if (zero_pad) {
                count = fmt_output_chars(stream, str, size, count,
                                         '0', zero_pad);
            }
            goto haslp;

        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            {
#ifdef FLOATING_POINT
                const char *decimal_point = ".";
                char softsign;          /* temporary negative sign for floats */
#ifdef _NO_LONGDBL
                union { int i; double d; unsigned long long ull; } _double_ = { 0 };
#define fpvalue (_double_.d)
#else
                union { int i; _LONG_DOUBLE ld; } _long_double_ = { 0 };
#define fpvalue (_long_double_.ld)
                int tmp;
#endif
                int fsize, realsz, dprec;
                int expt;               /* integer value of exponent */
                int expsize = 0;        /* character count for expstr */
                int ndig;               /* actual number of digits returned by cvt */
                char expstr[7];         /* buffer for exponent string */
                char ox[2];             /* space for 0x hex-prefix */
#endif

                /* fetch double value */
                if (type_flags == TYPE_ldouble) {
                    fpvalue = (double)va_arg(ap, long double);
                } else {
                    fpvalue = va_arg(ap, double);
                }

#ifdef FLOATING_POINT
                dprec = 0;
                sign = 0;

                if ((flags & FLAG_PREC) == 0) {
                    prec = 6;
                } else
                if ((c == 'g' || c == 'G') && prec == 0) {
                    prec = 1;
                }
                /* do this before tricky precision changes */
                if (isnan(fpvalue)) {
                    lp = "NaN";
                    len = 3;
                    goto has_string_len;
                }
                if (!isfinite(fpvalue)) {
                    if (fpvalue < 0) {
                        lp  = "-Inf";
                        len = 4;
                    } else
                    if (flags & FLAG_PLUS) {
                        lp  = "+Inf";
                        len = 4;
                    } else {
                        lp = "Inf";
                        len = 3;
                    }
                    goto has_string_len;
                }

                lp = cvt(fpvalue, prec, flags, &softsign, &expt, c, &ndig);

//fprintf(stream ? stream : stdout, "\ncvt(d=%16llX, prec=%d, flags=%x, c=%c) -> |%.*s|, softsign=%d, expt=%d, ndig=%d\n",
//       _double_.ull, prec, flags, c, ndig, lp, softsign, expt, ndig);

                if (c == 'g' || c == 'G') {
                    if (expt <= -4 || expt > prec)
                        c = (c == 'g') ? 'e' : 'E';
                    else
                        c = 'g';
                }
                if (c <= 'e') { /* 'e' or 'E' fmt */
                    --expt;
                    expsize = exponent(expstr, expt, c);
                    fsize = expsize + ndig;
                    if (ndig > 1 || (flags & FLAG_ALT)) {
                        ++fsize;
                    }
                } else
                if (c == 'f') {         /* f fmt */
                    if (expt > 0) {
                        fsize = expt;
                        if (prec || (flags & FLAG_ALT))
                            fsize += prec + 1;
                    } else {    /* "0.X" */
                        fsize = prec + 2;
                    }
                } else
                if (expt >= ndig) {     /* fixed g fmt */
                    fsize = expt;
                    if (flags & FLAG_ALT)
                        ++fsize;
                } else {
                    fsize = ndig + (expt > 0 ? 1 : 2 - expt);
                }

                if (softsign) {
                    sign = '-';
                } else {
                    if (flags & FLAG_PLUS) {
                        sign = '+';
                    } else
                    if (flags & FLAG_SPACE) {
                        sign = ' ';
                    }
                }

                /*
                 * At this point, `lp'
                 * points to a string which (if not flags&LADJUST) should be
                 * padded out to `width' places.  If flags&ZEROPAD, it should
                 * first be prefixed by any sign or other prefix; otherwise,
                 * it should be blank padded before the prefix is emitted.
                 * After any left-hand padding and prefixing, emit zeroes
                 * required by a decimal [diouxX] precision, then print the
                 * string proper, then emit zeroes required by any leftover
                 * floating precision; finally, if LADJUST, pad with blanks.
                 *
                 * Compute actual size, so we know how much to pad.
                 * size excludes decimal prec; realsz includes it.
                 */
                realsz = dprec > fsize ? dprec : fsize;
                if (sign) {
                    realsz++;
                }

#define PRINT(s,n)  count = fmt_output_chunk(stream, str, size, count, s, n, 'M')
#define PAD(n,c)    count = fmt_output_chars(stream, str, size, count, c, n)
#define zeroes '0'
#define blanks ' '
#define cp lp

                /* right-adjusting blank padding */
                if ((flags & (FLAG_MINUS|FLAG_ZERO)) == 0) {
//fprintf(stream ? stream : stdout, "PAD(%d, %c)\n", width - realsz, blanks);
                    PAD(width - realsz, blanks);
                }

                /* prefix */
                if (sign) {
                    count = fmt_output_chars(stream, str, size, count,
                                             sign, 1);
                }

                /* right-adjusting zero padding */
                if ((flags & (FLAG_MINUS|FLAG_ZERO)) == FLAG_ZERO) {
//fprintf(stream ? stream : stdout, "PAD(%d, %c)\n", width - realsz, zeroes);
                    PAD(width - realsz, zeroes);
                }

                /* leading zeroes from decimal precision */
//fprintf(stream ? stream : stdout, "PAD(%d, %c)\n", dprec - fsize, zeroes);
                PAD(dprec - fsize, zeroes);

                if (c >= 'f') { /* 'f' or 'g' */
                    if (fpvalue == 0) {
                        /* kludge for __dtoa irregularity */
                        PRINT("0", 1);
                        if (expt < ndig || (flags & FLAG_ALT) != 0) {
                            PRINT(decimal_point, 1);
                            PAD(ndig - 1, zeroes);
                        }
                    } else
                    if (expt <= 0) {
                        PRINT("0", 1);
                        if (expt || ndig) {
                            PRINT(decimal_point, 1);
                            PAD(-expt, zeroes);
                            PRINT(cp, ndig);
                        }
                    } else
                    if (expt >= ndig) {
                        PRINT(cp, ndig);
                        PAD(expt - ndig, zeroes);
                        if (flags & FLAG_ALT)
                            PRINT(".", 1);
                    } else {
                        PRINT(cp, expt);
                        cp += expt;
                        PRINT(".", 1);
                        PRINT(cp, ndig - expt);
                    }
                } else {        /* 'e' or 'E' */
                    if (ndig > 1 || (flags & FLAG_ALT)) {
                        ox[0] = *cp++;
                        ox[1] = '.';
                        PRINT(ox, 2);
                        if (fpvalue) {
                            PRINT(cp, ndig - 1);
                        } else { /* 0.[0..] */
                            /* __dtoa irregularity */
                            PAD(ndig - 1, zeroes);
                        }
                    } else {    /* XeYYY */
                        PRINT(cp, 1);
                    }
                    PRINT(expstr, expsize);
                }
                /* left-adjusting padding (always blank) */
                if (flags & FLAG_MINUS) {
                    PAD(width - realsz, blanks);
                }
#endif
            }
            break;
        }
        continue;

      error:
        lp = format;
        len = strlen(format);
        format += len;
        goto haslp;
    }

  done:
    if (!stream) {
        if (count < (int)size) {
            str[count] = '\0';
        } else
        if (size > 0) {
            str[size - 1] = '\0';
        }
    }
#if 0
    /* Unlock the stream.  */
    _IO_funlockfile (s);
    _IO_cleanup_region_end (0);
#endif

    return count;
}

/*---------------- printf functions ----------------*/

int iprintf(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = fmt_output(stdout, NULL, 0, format, ap);
    va_end(ap);

    return n;
}

int ifprintf(FILE *stream, const char *format, ...)
{
    va_list ap;
    int n;

    if (!stream) {
        errno = EBADF;
        return -1;
    }
    va_start(ap, format);
    n = fmt_output(stream, NULL, 0, format, ap);
    va_end(ap);

    return n;
}

int isnprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = fmt_output(NULL, str, size, format, ap);
    va_end(ap);

    return n;
}

int isprintf(char *str, const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = fmt_output(NULL, str, INT_MAX, format, ap);
    va_end(ap);

    return n;
}

int ivprintf(const char *format, va_list arglist)
{
    return fmt_output(stdout, NULL, 0, format, arglist);
}

int ivfprintf(FILE *stream, const char *format, va_list arglist)
{
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    return fmt_output(stream, NULL, 0, format, arglist);
}

int ivsnprintf(char *str, size_t size, const char *format, va_list arglist)
{
    return fmt_output(NULL, str, size, format, arglist);
}

int ivsprintf(char *str, const char *format, va_list arglist)
{
    return fmt_output(NULL, str, INT_MAX, format, arglist);
}

int ifputs_hex(FILE *stream, const void *_buf, int len)
{
    const byte *buf = _buf;
    int line_len, i, ret = 0;
    static const char hexchar[16] = "0123456789ABCDEF";

    if (!stream) {
        errno = EBADF;
        return -1;
    }

    while (len) {
        line_len = MIN(len, 16);
        for (i = 0; i < line_len; i++) {
            putc_unlocked(hexchar[(buf[i] >> 4) & 0x0F], stream);
            putc_unlocked(hexchar[ buf[i]       & 0x0F], stream);
            putc_unlocked(' ', stream);
        }
        while (i < 16) {
            putc_unlocked(' ', stream);
            putc_unlocked(' ', stream);
            putc_unlocked(' ', stream);
            i++;
        }
        ret += 16 * 3;
        for (i = 0; i < line_len; i++) {
            if (isprint((unsigned char)buf[i])) {
                putc_unlocked(buf[i], stream);
            } else {
                putc_unlocked('.', stream);
            }
        }
        ret += line_len;
        buf += line_len;
        len -= line_len;
        putc_unlocked('\n', stream);
        ret++;
    }
    return ret;
}

#ifdef FLOATING_POINT

#ifdef _NO_LONGDBL
extern char *_dtoa_r(double, int, int, int *, int *, char **);
union double_union {
    double d;
    struct {
#if __BYTE_ORDER == __BIG_ENDIAN
        unsigned int high;
        unsigned int low;
#else
        /* Should deal with __FLOAT_WORD_ORDER special case */
        unsigned int low;
        unsigned int high;
#endif
    } u;
};

#define word0(tmp) (tmp).u.high
#define Sign_bit 0x80000000
#else
extern char *_ldtoa_r(_LONG_DOUBLE, int, int, int *, int *, char **);
#undef word0
#define word0(x) ldword0(x)
#endif

static char *cvt(
#ifdef _NO_LONGDBL
        double value,
#else
        _LONG_DOUBLE value,
#endif
        int ndigits, int flags, char *sign, int *decpt, int ch, int *length)
{
    int mode, dsgn;
    char *digits, *bp, *rve;
#ifdef _NO_LONGDBL
    union double_union tmp;
#else
    struct ldieee *ldptr;
#endif

    if (ch == 'f') {
        mode = 3;               /* ndigits after the decimal point */
    } else {
        /* To obtain ndigits after the decimal point for the 'e'
         * and 'E' formats, round to ndigits + 1 significant
         * figures.
         */
        if (ch == 'e' || ch == 'E') {
            ndigits++;
        }
        mode = 2;               /* ndigits significant digits */
    }

#ifdef _NO_LONGDBL
    tmp.d = value;

    if (word0(tmp) & Sign_bit) { /* this will check for < 0 and -0.0 */
        value = -value;
        *sign = '-';
    } else {
        *sign = '\000';
    }
    digits = dtoa(value, mode, ndigits, decpt, &dsgn, &rve);
#else /* !_NO_LONGDBL */
    ldptr = (struct ldieee *)&value;
    if (ldptr->sign) { /* this will check for < 0 and -0.0 */
        value = -value;
        *sign = '-';
    } else {
        *sign = '\000';
    }
    digits = ldtoa(value, mode, ndigits, decpt, &dsgn, &rve);
#endif /* !_NO_LONGDBL */

    if ((ch != 'g' && ch != 'G') || (flags & FLAG_ALT)) {       /* Print trailing zeros */
        bp = digits + ndigits;
        if (ch == 'f') {
            /* FBE: it seems that the newlib is incorrect. I
            changed '0' to '\0' */
            if (*digits == '\0' && value)
                *decpt = -ndigits + 1;
            bp += *decpt;
        }
        if (value == 0) { /* kludge for __dtoa irregularity */
            rve = bp;
        }
        while (rve < bp) {
            *rve++ = '0';
        }
    }
    *length = rve - digits;
    return digits;
}

#define to_char(n)      ((n) + '0')

static int exponent(char *p0, int expn, int fmtch)
{
    char expbuf[40];
    char *p, *t;

    p = p0;
    *p++ = fmtch;
    if (expn < 0) {
        expn = -expn;
        *p++ = '-';
    } else {
        *p++ = '+';
    }
    t = expbuf + 40;
    if (expn > 9) {
        do {
            *--t = to_char(expn % 10);
        } while ((expn /= 10) > 9);
        *--t = to_char(expn);
        for (; t < expbuf + 40; *p++ = *t++)
            continue;
    } else {
        *p++ = '0';
        *p++ = to_char(expn);
    }
    return (p - p0);
}

#endif /* FLOATING_POINT */

void iprintf_register_formatter(int modifier, formatter_f *formatter)
{
    struct formatter_t *old = &put_memory_fmt[(unsigned char)modifier];

    if ((old->is_raw && old->raw_formatter != formatter)
    ||  (!old->is_raw && old->ptr_formatter)) {
        e_panic("trying to overload already defined memory formatter for "
                "modifier '%c'", modifier);
    }

    old->is_raw = true;
    old->raw_formatter = formatter;
}

void iprintf_register_pointer_formatter(int modifier,
                                        pointer_formatter_f *formatter)
{
    struct formatter_t *old = &put_memory_fmt[(unsigned char)modifier];

    if ((!old->is_raw && old->ptr_formatter != formatter)
    ||  (old->is_raw && old->raw_formatter)) {
        e_panic("trying to overload already defined memory formatter for "
                "modifier '%c'", modifier);
    }

    old->is_raw = false;
    old->ptr_formatter = formatter;
}

ssize_t formatter_writef(FILE *stream, char *buf, size_t buf_len,
                         const char *fmt, ...)
{
    va_list arg_list;
    ssize_t n;

    va_start(arg_list, fmt);

    if (stream) {
        n = vfprintf(stream, fmt, arg_list);
    } else {
        n = vsnprintf(buf, buf_len, fmt, arg_list);
    }
    va_end(arg_list);

    return n;
}

ssize_t formatter_write(FILE *stream, char *buf, size_t buf_len,
                        const char *s, size_t len)
{
    if (stream) {
        if (fwrite(s, 1, len, stream) != len) {
            return -1;
        }
    } else {
        memcpy(buf, s, MIN(len, buf_len));
    }

    return len;
}
