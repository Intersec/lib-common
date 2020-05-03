/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#include <lib-common/core.h>
#include <lib-common/container-qvector.h>
#include <lib-common/sort.h>

static const char __b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char __b64url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static unsigned char const __str_url_invalid[256] = {
#define REPEAT16(x)  x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x
    REPEAT16(255), REPEAT16(255),
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 255, 255, 255, 255, 255, 255,
    255, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', 255, 255, 255, 255, '_',
    255, 'a', 'b', 'c', 'd', 'e', 'f', 'g',
    'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w',
    'x', 'y', 'z', 255, 255, 255, 255, 255,
    REPEAT16(255), REPEAT16(255), REPEAT16(255), REPEAT16(255),
    REPEAT16(255), REPEAT16(255), REPEAT16(255), REPEAT16(255),
#undef REPEAT16
};

static unsigned char const __decode_base64[256] = {
#define INV       255
#define REPEAT8   INV, INV, INV, INV, INV, INV, INV, INV
#define REPEAT16  REPEAT8, REPEAT8
    REPEAT16, REPEAT16,
    REPEAT8, INV, INV, INV, 62 /* + */, INV, INV, INV, 63 /* / */,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, INV, INV, INV, INV, INV, INV,
    INV, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 /* O */,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 /* Z */, INV, INV, INV, INV, INV,
    INV, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40 /* o */,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 /* z */, INV, INV, INV, INV, INV,
    REPEAT16, REPEAT16, REPEAT16, REPEAT16,
    REPEAT16, REPEAT16, REPEAT16, REPEAT16,
};

static unsigned char const __decode_base64url[256] = {
    REPEAT16, REPEAT16,
    REPEAT8, INV, INV, INV, INV, INV, 62 /* - */, INV, INV,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, INV, INV, INV, INV, INV, INV,
    INV, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 /* O */,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 /* Z */, INV, INV, INV, INV, 63 /* _ */,
    INV, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40 /* o */,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 /* z */, INV, INV, INV, INV, INV,
    REPEAT16, REPEAT16, REPEAT16, REPEAT16,
    REPEAT16, REPEAT16, REPEAT16, REPEAT16,
#undef REPEAT8
#undef REPEAT16
#undef INV
};

static byte const __str_encode_flags[256] = {
#define REPEAT16(x)  x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x
#define QP  1
#define XP  2       /* Obviously, means "non XML printable char". */
    XP,    XP,    XP,    XP,    XP,    XP,    XP,    XP,
    XP,    0,     0,     XP,    XP,    0,     XP,    XP,     /* \n \t \r */
    REPEAT16(XP),
    0,     QP,    XP|QP, QP,    QP,    QP,    XP|QP, XP|QP,  /* "&' */
    QP,    QP,    QP,    QP,    QP,    QP,    0,     QP,     /* . */
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    XP|QP, 0,     XP|QP, QP,     /* <=> */
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    QP,
    QP,    QP,    QP,    QP,    QP,    QP,    QP,    0,      /* DEL */
    REPEAT16(0), REPEAT16(0), REPEAT16(0), REPEAT16(0),
    REPEAT16(0), REPEAT16(0), REPEAT16(0), REPEAT16(0),
#undef REPEAT16
};

static int test_quoted_printable(int x) {
    return __str_encode_flags[x] & QP;
}

static int test_xml_printable(int x) {
    return !(__str_encode_flags[x] & XP);
}

#undef QP
#undef XP


void sb_add_slashes(sb_t *sb, const void *_data, int len,
                    const char *toesc, const char *esc)
{
    uint32_t buf[BITS_TO_ARRAY_LEN(uint32_t, 256)] = { 0, };
    uint8_t  repl[256];
    const byte *p = _data, *end = p + len;

    while (*toesc) {
        byte c = *toesc++;
        SET_BIT(buf, c);
        repl[c] = *esc++;
    }

    if (!TST_BIT(buf, '\\')) {
        SET_BIT(buf, '\\');
        repl['\\'] = '\\';
    }

    sb_grow(sb, len);
    while (p < end) {
        const byte *q = p;

        while (p < end && !TST_BIT(buf, *p))
            p++;
        sb_add(sb, q, p - q);

        while (p < end && TST_BIT(buf, *p)) {
            byte c = repl[*p++];

            if (c) {
                char *s = sb_growlen(sb, 2);
                s[0] = '\\';
                s[1] = c;
            }
        }
    }
}

void sb_add_unslashes(sb_t *sb, const void *_data, int len,
                      const char *tounesc, const char *unesc)
{
    uint32_t buf[BITS_TO_ARRAY_LEN(uint32_t, 256)] = { 0, };
    uint8_t  repl[256];
    const byte *p = _data, *end = p + len;

    while (*tounesc) {
        byte c = *tounesc++;
        SET_BIT(buf, c);
        repl[c] = *unesc++;
    }

    if (!TST_BIT(buf, '\\')) {
        SET_BIT(buf, '\\');
        repl['\\'] = '\\';
    }

    while (p < end) {
        const byte *q = p;

        /* -1 so that we always have a char after \ */
        p = memchr(p, '\\', end - p - 1);
        if (!p) {
            p = q;
            break;
        }
        sb_add(sb, q, p - q);

        if (TST_BIT(buf, p[1])) {
            sb_addc(sb, repl[*++p]);
        } else {
            sb_addc(sb, '\\');
        }
        p++;
    }
    sb_add(sb, p, end - p);
}

int sb_add_expandenv(sb_t *sb, const void *data, int len)
{
    sb_t orig = *sb;
    pstream_t ps = ps_init(data, len);
    SB_1k(env_buf);

    while (!ps_done(&ps)) {
        pstream_t n;
        pstream_t env_name;
        const char *var;
        bool has_env = false;

        if (ps_get_ps_chr_and_skip(&ps, '$', &n) < 0) {
            n = ps;
            ps = ps_init(NULL, 0);
        } else {
            pstream_t prev = n;
            int slashes = 0;

            while (ps_endswithstr(&prev, "\\")) {
                slashes++;
                prev.s_end--;
            }
            if ((slashes & 1)) {
                n.s_end++;
            } else {
                has_env = true;
            }
        }

        sb_add_unslashes(sb, n.s, ps_len(&n), "$", "$");

        if (!has_env) {
            break;
        }

        if (ps_skipc(&ps, '{') < 0) {
            env_name = ps_get_span(&ps, &ctype_iscvar);
        } else
        if (ps_get_ps_chr_and_skip(&ps, '}', &env_name) < 0) {
            return __sb_rewind_adds(sb, &orig);
        }

        if (ps_done(&env_name)) {
            return __sb_rewind_adds(sb, &orig);
        }

        sb_set(&env_buf, env_name.s, ps_len(&env_name));
        var = getenv(env_buf.data);

        if (!var) {
            return __sb_rewind_adds(sb, &orig);
        }

        sb_adds(sb, var);
    }
    return 0;
}

static char const __c_unescape[] = {
    ['a'] = '\a', ['b'] = '\b', ['e'] = '\e', ['t'] = '\t', ['n'] = '\n',
    ['v'] = '\v', ['f'] = '\f', ['r'] = '\r', ['\\'] = '\\',
    ['"'] = '"',  ['\''] = '\''
};

void sb_add_unquoted(sb_t *sb, const void *data, int len)
{
    const char *p = data, *end = p + len;

    while (p < end) {
        const char *q = p;
        int c;

        p = memchr(q, '\\', end - q);
        if (!p) {
            sb_add(sb, q, end - q);
            return;
        }
        sb_add(sb, q, p - q);
        p += 1;

        if (p == end) {
            sb_addc(sb, '\\');
            return;
        }

        switch (*p) {
          case 'a': case 'b': case 'e': case 't': case 'n': case 'v':
          case 'f': case 'r': case '\\': case '"': case '\'':
            sb_addc(sb, __c_unescape[(unsigned char)*p++]);
            continue;

          case '0'...'7':
            c = *p++ - '0';
            if (p < end && *p >= '0' && *p <= '7')
                c = (c << 3) + *p++ - '0';
            if (c < '\040' && p < end && *p >= '0' && *p <= '7')
                c = (c << 3) + *p++ - '0';
            sb_addc(sb, c);
            continue;

          case 'x':
            if (end - p < 3)
                break;
            c = hexdecode(p + 1);
            if (c < 0)
                break;
            p += 3;
            sb_addc(sb, c);
            continue;

          case 'u':
            if (end - p < 5)
                break;
            c = (hexdecode(p + 1) << 8) | hexdecode(p + 3);
            if (c < 0)
                break;
            p += 5;
            sb_adduc(sb, c);
            continue;
        }
        sb_addc(sb, '\\');
    }
}

void sb_add_urlencode(sb_t *sb, const void *_data, int len)
{
    const byte *p = _data, *end = p + len;

    sb_grow(sb, len);
    while (p < end) {
        const byte *q = p;

        while (p < end && __str_url_invalid[*p] != 255)
            p++;
        sb_add(sb, q, p - q);

        while (p < end && __str_url_invalid[*p] == 255) {
            char *s = sb_growlen(sb, 3);
            s[0] = '%';
            s[1] = __str_digits_upper[(*p >> 4) & 0xf];
            s[2] = __str_digits_upper[(*p >> 0) & 0xf];
            p++;
        }
    }
}

void sb_add_urldecode(sb_t *sb, const void *data, int len)
{
    const char *p = data, *end = p + len;

    for (;;) {
        const char *q = p;
        int c;

        p = memchr(q, '%', end - q);
        if (!p) {
            sb_add(sb, q, end - q);
            return;
        }
        sb_add(sb, q, p - q);

        if (end - p < 3) {
            sb_addc(sb, *p++);
            continue;
        }
        c = hexdecode(p + 1);
        if (c < 0) {
            sb_addc(sb, *p++);
            continue;
        }
        sb_addc(sb, c);
        p += 3;
    }
}

void sb_urldecode(sb_t *sb)
{
    const char *tmp, *r, *end = sb_end(sb);
    char *w;

    r = w = memchr(sb->data, '%', sb->len);
    if (!r)
        return;

    for (;;) {
        int c;

        if (end - r < 3) {
            *w++ = *r++;
            continue;
        }
        c = hexdecode(r + 1);
        if (c < 0) {
            *w++ = *r++;
            continue;
        }
        *w++ = c;
        r   += 3;

        r = memchr(tmp = r, '%', end - r);
        if (!r) {
            memmove(w, tmp, end - tmp);
            w += end - tmp;
            __sb_fixlen(sb, w - sb->data);
            return;
        }
        memmove(w, tmp, r - tmp);
        w += r - tmp;
    }
}

void sb_add_hex(sb_t *sb, const void *data, int len)
{
    char *s = sb_growlen(sb, len * 2);

    for (const byte *p = data, *end = p + len; p < end; p++) {
        *s++ = __str_digits_upper[(*p >> 4) & 0x0f];
        *s++ = __str_digits_upper[(*p >> 0) & 0x0f];
    }
}

int  sb_add_unhex(sb_t *sb, const void *data, int len)
{
    sb_t orig = *sb;
    char *s;

    if (unlikely(len & 1))
        return -1;

    s = sb_growlen(sb, len / 2);
    for (const char *p = data, *end = p + len; p < end; p += 2) {
        int c = hexdecode(p);

        if (unlikely(c < 0))
            return __sb_rewind_adds(sb, &orig);
        *s++ = c;
    }
    return 0;
}

void sb_add_xmlescape(sb_t *sb, const void *data, int len)
{
    const byte *p = data, *end = p + len;

    sb_grow(sb, len);
    while (p < end) {
        const byte *q = p;

        while (p < end && test_xml_printable(*p))
            p++;
        sb_add(sb, q, p - q);

        while (p < end && !test_xml_printable(*p)) {
            switch (*p) {
              case '&':  sb_adds(sb, "&amp;"); break;
              case '<':  sb_adds(sb, "&lt;");  break;
              case '>':  sb_adds(sb, "&gt;");  break;
              case '\'': sb_adds(sb, "&#39;"); break;
              case '"':  sb_adds(sb, "&#34;"); break;
              default:
                 /* Invalid XML1.0 characters, we skip this one. */
                 break;
            }
            p++;
        }
    }
}

int sb_add_xmlunescape(sb_t *sb, const void *data, int len)
{
    sb_t orig = *sb;
    const char *p = data, *end = p + len;

    while (p < end) {
        const char *q = p;
        const char *semi;

        while (p < end && *p != '<' && *p != '&')
            p++;
        sb_add(sb, q, p - q);

        if (p == end)
            return 0;

        if (*p++ == '<') {
            /* strip out comments */
            if (p + 3 <= end && !memcmp(p, "!--", 3)) {
                p += 3;
                for (;;) {
                    p = memchr(p, '-', end - p);
                    if (!p || p + 3 > end)
                        goto error;
                    if (!memcmp(p, "-->", 3)) {
                        p += 3;
                        break;
                    }
                    p++;
                }
                continue;
            }

            /* extract CDATA stuff */
            if (p + 8 <= end && !memcmp(p, "![CDATA[", 8)) {
                p += 8;
                for (q = p;;) {
                    p = memchr(p, ']', end - p);
                    if (!p || p + 3 > end)
                        goto error;
                    if (!memcmp(p, "]]>", 3)) {
                        sb_add(sb, q, p - q);
                        p += 3;
                        break;
                    }
                    p++;
                }
                continue;
            }
            goto error;
        }

        /* entities: we have (p[-1] == '&') */
        semi = memchr(p, ';', end - p);
        if (!semi || p + 1 > semi)
            goto error;

        if (*p == '#') {
            int c = 0;

            if (semi - p > 7)
                goto error;

            if (p[1] == 'x') {
                for (p += 2; p < semi; p++) {
                    c = (c << 4) | hexdigit(*p);
                }
                if (c < 0)
                    goto error;
            } else {
                c = memtoip(p + 1, semi - p - 1, (const byte **)&p);
                if (p != semi)
                    goto error;
            }
            sb_adduc(sb, c);
        } else {
            /* &lt; &gt; &apos; &amp; &quot */
            switch (semi - p) {
              case 2:
                if (!memcmp(p, "lt", 2)) {
                    sb_addc(sb, '<');
                    break;
                }
                if (!memcmp(p, "gt", 2)) {
                    sb_addc(sb, '>');
                    break;
                }
                goto error;

              case 3:
                if (!memcmp(p, "amp", 3)) {
                    sb_addc(sb, '&');
                    break;
                }
                goto error;

              case 4:
                if (!memcmp(p, "apos", 4)) {
                    sb_addc(sb, '\'');
                    break;
                }
                if (!memcmp(p, "quot", 4)) {
                    sb_addc(sb, '"');
                    break;
                }
                goto error;

              default:
                goto error;
            }
        }

        p = semi + 1;
    }
    return 0;

  error:
    return __sb_rewind_adds(sb, &orig);
}

/* OG: should take width as a parameter */
void sb_add_qpe(sb_t *sb, const void *data, int len)
{
    int c, i = 0, j = 0, col = 0;
    const byte *src = data;

    sb_grow(sb, len);
    while (i < len) {
        if (col + i - j >= 75) {
            sb_add(sb, src + j, 75 - col);
            sb_adds(sb, "=\r\n");
            j  += 75 - col;
            col = 0;
        }
        if (test_quoted_printable(c = src[i++]))
            continue;
        /* only encode '.' if at the beginning of a line */
        if (c == '.' && col)
            continue;
        /* encode spaces only at end on line */
        if (isblank(c)) {
            if (!(i + 2 <= len && !memcmp(src + i, "\r\n", 2)))
                continue;
        }
        /* \r\n remain the same and reset col to 0 */
        if (c == '\r' && i + 1 <= len && src[i] == '\n') {
            i++;
            sb_add(sb, src + j, i - j);
            col = 0;
        } else {
            char *s;

            sb_add(sb, src + j, i - 1 - j);
            col += i - 1 - j ;
            if (col > 75 - 3) {
                sb_adds(sb, "=\r\n");
                col = 0;
            }
            s = sb_growlen(sb, 3);
            s[0] = '=';
            s[1] = __str_digits_upper[(c >> 4) & 0xf];
            s[2] = __str_digits_upper[(c >> 0) & 0xf];
            col += 3;
        }
        j = i;
    }
    sb_add(sb, src + j, i - j);
    if (sb->data[sb->len - 1] != '\n')
        sb_adds(sb, "=\r\n");
}

void sb_add_unqpe(sb_t *sb, const void *data, int len)
{
    const byte *p = data, *end = p + len;
    sb_grow(sb, len);

    while (p < end) {
        const byte *q = p;

        while (p < end && *p != '=' && *p != '\r' && *p)
            p++;
        sb_add(sb, q, p - q);

        if (p >= end)
            return;
        switch (*p++) {
          case '\0':
            return;

          case '=':
            if (end - p < 2) {
                sb_addc(sb, '=');
            } else
            if (p[0] == '\r' && p[1] == '\n') {
                p += 2;
            } else {
                int c = hexdecode((const char *)p);
                if (c < 0) {
                    sb_addc(sb, '=');
                } else {
                    sb_addc(sb, c);
                    p += 2;
                }
            }
            break;

          case '\r':
            if (p < end && *p == '\n') {
                sb_addc(sb, *p++);
            } else {
                sb_addc(sb, '\r');
            }
            break;
        }
    }
}


/* computes a slightly overestimated size to write srclen bytes with `ppline`
 * packs per line, not knowing if we start at column 0 or not.
 *
 * The over-estimate is of at most 4 bytes, we can live with that.
 */
static int b64_rough_size(int srclen, int ppline)
{
    int nbpacks = ((srclen + 2) / 3);

    if (ppline < 0)
        return 4 * nbpacks;
    /*
     * Worst case is we're at `4 * ppline` column so we have to add a \r\n
     * straight away plus what is need for the rest.
     */
    return 4 * nbpacks + 2 + 2 * DIV_ROUND_UP(nbpacks, ppline);
}

void sb_add_b64_start(sb_t *dst, int len, int width, sb_b64_ctx_t *ctx)
{
    p_clear(ctx, 1);
    if (width == 0) {
        width = 19; /* 76 characters + \r\n */
    } else {
        /* XXX: >>2 keeps the sign, unlike /4 */
        width >>= 2;
    }
    ctx->packs_per_line = width;
    sb_grow(dst, b64_rough_size(len, width));
}

static void _sb_add_b64_update(sb_t *dst, const void *src0, int len,
                               sb_b64_ctx_t *ctx, const char table[64])
{
    short ppline    = ctx->packs_per_line;
    short pack_num  = ctx->pack_num;
    const byte *src = src0;
    const byte *end = src + len;
    unsigned pack;
    char *data;

    if (ctx->trail_len + len < 3) {
        memcpy(ctx->trail + ctx->trail_len, src, len);
        ctx->trail_len += len;
        return;
    }

    data = sb_grow(dst, b64_rough_size(ctx->trail_len + len, ppline));

    if (ctx->trail_len) {
        pack  = ctx->trail[0] << 16;
        pack |= (ctx->trail_len == 2 ? ctx->trail[1] : *src++) << 8;
        pack |= *src++;
        goto initialized;
    }

    do {
        pack  = *src++ << 16;
        pack |= *src++ <<  8;
        pack |= *src++ <<  0;

      initialized:
        *data++ = table[(pack >> (3 * 6)) & 0x3f];
        *data++ = table[(pack >> (2 * 6)) & 0x3f];
        *data++ = table[(pack >> (1 * 6)) & 0x3f];
        *data++ = table[(pack >> (0 * 6)) & 0x3f];

        if (ppline > 0 && ++pack_num >= ppline) {
            pack_num = 0;
            *data++ = '\r';
            *data++ = '\n';
        }
    } while (src + 3 <= end);

    memcpy(ctx->trail, src, end - src);
    ctx->trail_len = end - src;
    ctx->pack_num  = pack_num;
    __sb_fixlen(dst, data - dst->data);
}

void sb_add_b64_update(sb_t *dst, const void *src0, int len,
                       sb_b64_ctx_t *ctx)
{
    _sb_add_b64_update(dst, src0, len, ctx, __b64);
}

void sb_add_b64url_update(sb_t *dst, const void *src0, int len,
                          sb_b64_ctx_t *ctx)
{
    _sb_add_b64_update(dst, src0, len, ctx, __b64url);
}

static void
_sb_add_b64_finish(sb_t *dst, sb_b64_ctx_t *ctx, const char table[64])
{
    if (ctx->trail_len) {
        unsigned c1 = ctx->trail[0];
        unsigned c2 = ctx->trail_len == 2 ? ctx->trail[1] : 0;
        char *data  = sb_growlen(dst, 4);

        data[0] = table[c1 >> 2];
        data[1] = table[((c1 << 4) | (c2 >> 4)) & 0x3f];
        data[2] = ctx->trail_len == 2 ? table[(c2 << 2) & 0x3f] : '=';
        data[3] = '=';
    }
    if (ctx->packs_per_line > 0 && ctx->pack_num != 0) {
        ctx->pack_num = 0;
        sb_adds(dst, "\r\n");
    }
    ctx->trail_len = 0;
}

void sb_add_b64_finish(sb_t *dst, sb_b64_ctx_t *ctx)
{
    _sb_add_b64_finish(dst, ctx, __b64);
}

void sb_add_b64url_finish(sb_t *dst, sb_b64_ctx_t *ctx)
{
    _sb_add_b64_finish(dst, ctx, __b64url);
}

void sb_add_b64(sb_t *dst, const void *_src, int len, int width)
{
    sb_b64_ctx_t ctx;

    sb_add_b64_start(dst, len, width, &ctx);
    sb_add_b64_update(dst, _src, len, &ctx);
    sb_add_b64_finish(dst, &ctx);
}

void sb_add_lstr_b64(sb_t * nonnull sb, lstr_t data, int width)
{
    sb_add_b64(sb, data.s, data.len, width);
}

void sb_add_b64url(sb_t *dst, const void *_src, int len, int width)
{
    sb_b64_ctx_t ctx;

    sb_add_b64url_start(dst, len, width, &ctx);
    sb_add_b64url_update(dst, _src, len, &ctx);
    sb_add_b64url_finish(dst, &ctx);
}

void sb_add_lstr_b64url(sb_t * nonnull sb, lstr_t data, int width)
{
    sb_add_b64url(sb, data.s, data.len, width);
}

static int _sb_add_unb64(sb_t *sb, const void *data, int len,
                         const unsigned char table[256])
{
    const byte *src = data, *end = src + len;
    sb_t orig = *sb;

    while (src < end) {
        byte in[4];
        int ilen = 0;
        char *s;

        while (ilen < 4 && src < end) {
            int c = *src++;

            if (isspace(c))
                continue;

            /*
             * '=' must be at the end, they can only be 1 or 2 of them
             * IOW we must have 2 or 3 fill `in` bytes already.
             *
             * And after them we can only have spaces. No data.
             *
             */
            if (c == '=') {
                if (ilen < 2)
                    goto error;
                if (ilen == 2) {
                    while (src < end && isspace(*src))
                        src++;
                    if (src >= end || *src++ != '=')
                        goto error;
                }
                while (src < end) {
                    if (!isspace(*src++))
                        goto error;
                }

                s = sb_growlen(sb, ilen - 1);
                if (ilen == 3) {
                    s[1] = (in[1] << 4) | (in[2] >> 2);
                }
                s[0] = (in[0] << 2) | (in[1] >> 4);
                return 0;
            }

            in[ilen++] = c = table[c];
            if (unlikely(c == 255))
                goto error;
        }

        if (ilen == 0)
            return 0;

        if (ilen != 4)
            goto error;

        s    = sb_growlen(sb, 3);
        s[0] = (in[0] << 2) | (in[1] >> 4);
        s[1] = (in[1] << 4) | (in[2] >> 2);
        s[2] = (in[2] << 6) | (in[3] >> 0);
    }
    return 0;

error:
    return __sb_rewind_adds(sb, &orig);
}

int sb_add_unb64(sb_t *sb, const void *data, int len)
{
    return _sb_add_unb64(sb, data, len, __decode_base64);
}

int sb_add_unb64url(sb_t *sb, const void *data, int len)
{
    return _sb_add_unb64(sb, data, len, __decode_base64url);
}

void sb_add_csvescape(sb_t *sb, int sep, const void *data, int len)
{
    static ctype_desc_t ctype_needs_escape;
    pstream_t ps = ps_init(data, len);
    pstream_t cspan;

    ctype_desc_build(&ctype_needs_escape, "\"\n\r");
    SET_BIT(ctype_needs_escape.tab, sep);

    cspan = ps_get_cspan(&ps, &ctype_needs_escape);
    if (ps_done(&ps)) {
        /* No caracter needing escaping was found, just copy the input
         * string. */
        sb_add(sb, data, len);
        return;
    }

    /* There is at least one special character found in the input string, so
     * the whole string has to be double-quoted, and the double-quotes have
     * to be escaped by double-quotes.
     */
    sb_grow(sb, len + 2);
    sb_addc(sb, '"');
    sb_add_ps(sb, cspan);

    while (!ps_done(&ps)) {
        if (ps_get_ps_chr_and_skip(&ps, '"', &cspan) < 0) {
            sb_add_ps(sb, ps);
            break;
        }
        cspan.s_end++;
        sb_add_ps(sb, cspan);
        sb_addc(sb, '"');
    }

    sb_addc(sb, '"');
}

/*{{{ Punycode (RFC 3492) */

#define PUNYCODE_DELIMITER     '-'
#define PUNYCODE_BASE          36
#define PUNYCODE_TMIN          1
#define PUNYCODE_TMAX          26
#define PUNYCODE_SKEW          38
#define PUNYCODE_DAMP          700
#define PUNYCODE_INITIAL_BIAS  72
#define PUNYCODE_INITIAL_N     0x80

static int punycode_adapt_bias(int delta, int numpoints, bool firsttime)
{
    int k = 0;

    if (firsttime) {
        delta /= PUNYCODE_DAMP;
    } else {
        delta /= 2;
    }
    delta += delta / numpoints;

    while (delta > ((PUNYCODE_BASE - PUNYCODE_TMIN) * PUNYCODE_TMAX) / 2) {
        delta /= PUNYCODE_BASE - PUNYCODE_TMIN;
        k += PUNYCODE_BASE;
    }

    return k + (((PUNYCODE_BASE - PUNYCODE_TMIN + 1) * delta)
        /  (delta + PUNYCODE_SKEW));
}

static inline void punycode_output(sb_t *sb, int code_point)
{
    int c;

    assert (code_point >= 0);
    assert (code_point < PUNYCODE_BASE);

    if (code_point < PUNYCODE_TMAX) {
        /* 0..25 -> a..z */
        c = 'a' + code_point;
    } else {
        /* 26..35 -> 0..9 */
        c = '0' - PUNYCODE_TMAX + code_point;
    }
    sb_addc(sb, c);
}

static inline void
punycode_output_variable_length_integer(sb_t *sb, int bias, uint32_t q)
{
    for (int k = PUNYCODE_BASE; ; k += PUNYCODE_BASE) {
        uint32_t t;

        if (k <= bias) {
            t = PUNYCODE_TMIN;
        } else
            if (k >= bias + PUNYCODE_TMAX) {
                t = PUNYCODE_TMAX;
            } else {
                t = k - bias;
            }
        if (q < t) {
            break;
        }
        punycode_output(sb, t + ((q - t) % (PUNYCODE_BASE - t)));
        q = (q - t) / (PUNYCODE_BASE - t);
    }

    punycode_output(sb, q);
}

int sb_add_punycode_vec(sb_t *sb, const uint32_t *code_points,
                        int nb_code_points)
{
    /* This function implementation is a slightly enhanced version of the
     * algorithm described in section 6.3 (Encoding procedure) of the
     * RFC 3492.
     * The variables m, n, delta, bias and h have the same meaning as in the
     * RFC.
     */
    uint64_t *point_pos_pairs = p_alloca(uint64_t, nb_code_points);
    int point_pos_pairs_len = 0, nb_basic_code_points = 0;
    uint32_t n = PUNYCODE_INITIAL_N;
    uint64_t delta = 0;
    int bias = PUNYCODE_INITIAL_BIAS;
    int i, h;

    /* Basic code point segregation */
    for (i = 0; i < nb_code_points; i++) {
        if (isascii(code_points[i])) {
            nb_basic_code_points++;
            sb_addc(sb, code_points[i]);
        } else {
            point_pos_pairs[point_pos_pairs_len++]
                = ((uint64_t)code_points[i] << 32) + i;
        }
    }
    if (nb_basic_code_points > 0) {
        sb_addc(sb, PUNYCODE_DELIMITER);
    }

    /* Sort point_pos_pairs (which is the array of the non-basic code points)
     * in first by code point value, then by position in the input sequence */
    dsort64(point_pos_pairs, point_pos_pairs_len);

    /* Insertion unsort coding */
    i = 0;
    h = nb_basic_code_points;
    while (i < point_pos_pairs_len) {
        uint32_t m = point_pos_pairs[i] >> 32;
        uint32_t last_pos = 0;

        delta += (m - n) * (h + 1);
        n = m;

        while (i < point_pos_pairs_len && point_pos_pairs[i] >> 32 == m) {
            uint32_t point_pos = point_pos_pairs[i] & 0xffffffff;

            for (uint32_t j = last_pos; j < point_pos; j++) {
                if (code_points[j] < n) {
                    delta++;
                }
            }

            THROW_ERR_IF(delta > UINT32_MAX);
            punycode_output_variable_length_integer(sb, bias, delta);

            bias = punycode_adapt_bias(delta, h + 1,
                                       h == nb_basic_code_points);
            last_pos = point_pos;
            delta = 0;
            i++;
            h++;
        }

        for (int j = last_pos; j < nb_code_points; j++) {
            if (code_points[j] < n) {
                delta++;
            }
        }

        delta++;
        n++;
    }

    return 0;
}

int sb_add_punycode_str(sb_t *sb, const char *src, int src_len)
{
    uint32_t *code_points = p_alloca(uint32_t, src_len);
    int pos = 0, code_points_len = 0;
    bool is_ascii = true;

    while (pos < src_len) {
        int c = RETHROW(utf8_ngetc_at(src, src_len, &pos));

        is_ascii = is_ascii && isascii(c);
        code_points[code_points_len++] = c;
    }

    if (is_ascii) {
        sb_add(sb, src, src_len);
        sb_addc(sb, PUNYCODE_DELIMITER);
        return 0;
    } else {
        return sb_add_punycode_vec(sb, code_points, code_points_len);
    }
}

/*}}} */
/*{{{ IDNA (RFC 3490) */

#define IDNA_ACE_PFX      "xn--"
#define IDNA_ACE_PFX_LEN  (ssizeof(IDNA_ACE_PFX) - 1)

/* Non-LDH ASCII code points are: 0..2C, 2E..2F, 3A..40, 5B..60, and 7B..7F.
 */
ctype_desc_t const ctype_is_non_ldh = {
    {
        0xffffffff, 0xfc00dfff, 0xf8000001, 0xf8000001,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    }
};

static int idna_label_to_ascii(sb_t *sb, const char *label, int label_size,
                               qv_t(u32) *code_points,
                               bool is_ascii, unsigned flags)
{
    int initial_len = sb->len;

    THROW_ERR_IF(label_size == 0 || code_points->len == 0);

    if (flags & IDNA_USE_STD3_ASCII_RULES) {
        /* Verify the absence of leading and trailing hyphen-minus */
        THROW_ERR_IF(code_points->tab[0] == '-');
        THROW_ERR_IF(code_points->tab[code_points->len - 1] == '-');
    }

    if (is_ascii) {
        /* All characters of this label are ASCII ones, just output the input
         * string. */
        if (flags & IDNA_ASCII_TOLOWER) {
            const char *end = label + label_size;

            sb_grow(sb, label_size);
            while (label < end) {
                sb_addc(sb, tolower(*label++));
            }
        } else {
            sb_add(sb, label, label_size);
        }
    } else {
        is_ascii = true;

        /* Perform the last NAMEPREP operations on ASCII characters (which
         * were not performed in idna_nameprep). */
        tab_for_each_ptr(c, code_points) {
            if (isascii(*c)) {
                *c = unicode_tolower(*c);
            } else {
                is_ascii = false;
            }
        }

        if (unlikely(is_ascii)) {
            /* We were called in this function with is_ascii to false, but all
             * code points are actually ASCII ones. This can happen if
             * NAMEPREP filtered all the non-ASCII characters. In that case,
             * just output the code points without encoding them. */
            tab_for_each_entry(c, code_points) {
                sb_addc(sb, c);
            }
        } else {
            /* Verify that the sequence does NOT begin with the ACE prefix */
            if (code_points->len >= IDNA_ACE_PFX_LEN) {
                uint32_t ace_pfx[] = { 'x', 'n', '-', '-' };

                THROW_ERR_IF(!memcmp(code_points->tab, ace_pfx,
                                     IDNA_ACE_PFX_LEN * sizeof(uint32_t)));
            }

            /* Encode the sequence using the punycode encoding */
            sb_add(sb, IDNA_ACE_PFX, IDNA_ACE_PFX_LEN);
            RETHROW(sb_add_punycode_vec(sb, code_points->tab,
                                        code_points->len));
        }
    }

    /* Verify that the number of code points is in the range 1 to 63
     * inclusive. */
    THROW_ERR_IF(sb->len - initial_len > 63);

    return 0;
}

static inline int idna_nameprep(qv_t(u32) *code_points, int c, unsigned flags)
{
    /* This function is an approximation of NAMEPREP (RFC 3491), which is a
     * profile of STRINGPREP (RFC 3454) dedicated to internationalized domain
     * names.
     * The tables referenced here are defined in RFC 3454.
     *
     * NAMEPREP is made of 5 steps:
     *  - Mapping (tables B.1 and B.2); we are doing the exaxt checks for
     *    table B.1, but table B.2 (which is a tolower table) is approximated
     *    with unicode_tolower.
     *  - Normalization; not implemented here.
     *  - Prohibited Output; only prohibited characters of section 5.8 are
     *    checked, because they are included in the NAMEPREP prohibited
     *    output.
     *  - Bidirectional characters; not implemented here.
     *  - Unassigned Code Points: exact checks are done in this function.
     */

    /* Commonly mapped to nothing (table B.1) */
    switch (c) {
      case 0x00ad: case 0x034f: case 0x1806: case 0x180b: case 0x180c:
      case 0x180d: case 0x200b: case 0x200c: case 0x200d: case 0x2060:
      case 0xfe00 ... 0xfe0f: case 0xfeff:
        return 0;

      default:
        break;
    }

    /* Prohibited Output
     * Tables C.1.2, C.2.2, C.3, C.4, C.5, C.6, C.7, C.8 and C.9.
     *
     * Bidirectional characters (prohibit characters of section 5.8).
     */
    switch (c) {
      case 0x00a0: case 0x0340: case 0x0341: case 0x06dd: case 0x070f:
      case 0x1680: case 0x180e: case 0x2028: case 0x2029: case 0x205f:
      case 0x3000: case 0xfeff: case 0xe0001:
      case 0x0080 ... 0x009f: case 0x2000 ... 0x200f: case 0x202a ... 0x202f:
      case 0x2060 ... 0x2063: case 0x206a ... 0x206f: case 0x2ff0 ... 0x2ffb:
      case 0xd800 ... 0xdfff: case 0xe000 ... 0xf8ff: case 0xfdd0 ... 0xfdef:
      case 0xfff9 ... 0xfffd: case 0xfffe ... 0xffff:
      case 0x1d173 ... 0x1d17a: case 0x1fffe ... 0x1ffff:
      case 0x2fffe ... 0x2ffff: case 0x3fffe ... 0x3ffff:
      case 0x4fffe ... 0x4ffff: case 0x5fffe ... 0x5ffff:
      case 0x6fffe ... 0x6ffff: case 0x7fffe ... 0x7ffff:
      case 0x8fffe ... 0x8ffff: case 0x9fffe ... 0x9ffff:
      case 0xafffe ... 0xaffff: case 0xbfffe ... 0xbffff:
      case 0xcfffe ... 0xcffff: case 0xdfffe ... 0xdffff:
      case 0xe0020 ... 0xe007f: case 0xefffe ... 0xeffff:
      case 0xf0000 ... 0xffffd: case 0xffffe ... 0xfffff:
      case 0x100000 ... 0x10fffd: case 0x10fffe ... 0x10ffff:
        return -1;

      default:
        break;
    }

    if (!(flags & IDNA_ALLOW_UNASSIGNED)) {
        /* Unassigned Code Points (table A.1) */
        switch (c) {
          case 0x0221: case 0x038b: case 0x038d: case 0x03a2: case 0x03cf:
          case 0x0487: case 0x04cf: case 0x0560: case 0x0588: case 0x05a2:
          case 0x05ba: case 0x0620: case 0x06ff: case 0x070e: case 0x0904:
          case 0x0984: case 0x09a9: case 0x09b1: case 0x09bd: case 0x09de:
          case 0x0a29: case 0x0a31: case 0x0a34: case 0x0a37: case 0x0a3d:
          case 0x0a5d: case 0x0a84: case 0x0a8c: case 0x0a8e: case 0x0a92:
          case 0x0aa9: case 0x0ab1: case 0x0ab4: case 0x0ac6: case 0x0aca:
          case 0x0b04: case 0x0b29: case 0x0b31: case 0x0b5e: case 0x0b84:
          case 0x0b91: case 0x0b9b: case 0x0b9d: case 0x0bb6: case 0x0bc9:
          case 0x0c04: case 0x0c0d: case 0x0c11: case 0x0c29: case 0x0c34:
          case 0x0c45: case 0x0c49: case 0x0c84: case 0x0c8d: case 0x0c91:
          case 0x0ca9: case 0x0cb4: case 0x0cc5: case 0x0cc9: case 0x0cdf:
          case 0x0d04: case 0x0d0d: case 0x0d11: case 0x0d29: case 0x0d49:
          case 0x0d84: case 0x0db2: case 0x0dbc: case 0x0dd5: case 0x0dd7:
          case 0x0e83: case 0x0e89: case 0x0e98: case 0x0ea0: case 0x0ea4:
          case 0x0ea6: case 0x0eac: case 0x0eba: case 0x0ec5: case 0x0ec7:
          case 0x0f48: case 0x0f98: case 0x0fbd: case 0x1022: case 0x1028:
          case 0x102b: case 0x1207: case 0x1247: case 0x1249: case 0x1257:
          case 0x1259: case 0x1287: case 0x1289: case 0x12af: case 0x12b1:
          case 0x12bf: case 0x12c1: case 0x12cf: case 0x12d7: case 0x12ef:
          case 0x130f: case 0x1311: case 0x131f: case 0x1347: case 0x170d:
          case 0x176d: case 0x1771: case 0x180f: case 0x1f58: case 0x1f5a:
          case 0x1f5c: case 0x1f5e: case 0x1fb5: case 0x1fc5: case 0x1fdc:
          case 0x1ff5: case 0x1fff: case 0x24ff: case 0x2618: case 0x2705:
          case 0x2728: case 0x274c: case 0x274e: case 0x2757: case 0x27b0:
          case 0x2e9a: case 0x3040: case 0x318f: case 0x32ff: case 0x33ff:
          case 0xfb37: case 0xfb3d: case 0xfb3f: case 0xfb42: case 0xfb45:
          case 0xfe53: case 0xfe67: case 0xfe75: case 0xff00: case 0xffe7:
          case 0x1031f: case 0x1d455: case 0x1d49d: case 0x1d4ad:
          case 0x1d4ba: case 0x1d4bc: case 0x1d4c1: case 0x1d4c4:
          case 0x1d506: case 0x1d515: case 0x1d51d: case 0x1d53a:
          case 0x1d53f: case 0x1d545: case 0x1d551: case 0xe0000:
          case 0x0234 ... 0x024f: case 0x02ae ... 0x02af:
          case 0x02ef ... 0x02ff: case 0x0350 ... 0x035f:
          case 0x0370 ... 0x0373: case 0x0376 ... 0x0379:
          case 0x037b ... 0x037d: case 0x037f ... 0x0383:
          case 0x03f7 ... 0x03ff: case 0x04f6 ... 0x04f7:
          case 0x04fa ... 0x04ff: case 0x0510 ... 0x0530:
          case 0x0557 ... 0x0558: case 0x058b ... 0x0590:
          case 0x05c5 ... 0x05cf: case 0x05eb ... 0x05ef:
          case 0x05f5 ... 0x060b: case 0x060d ... 0x061a:
          case 0x061c ... 0x061e: case 0x063b ... 0x063f:
          case 0x0656 ... 0x065f: case 0x06ee ... 0x06ef:
          case 0x072d ... 0x072f: case 0x074b ... 0x077f:
          case 0x07b2 ... 0x0900: case 0x093a ... 0x093b:
          case 0x094e ... 0x094f: case 0x0955 ... 0x0957:
          case 0x0971 ... 0x0980: case 0x098d ... 0x098e:
          case 0x0991 ... 0x0992: case 0x09b3 ... 0x09b5:
          case 0x09ba ... 0x09bb: case 0x09c5 ... 0x09c6:
          case 0x09c9 ... 0x09ca: case 0x09ce ... 0x09d6:
          case 0x09d8 ... 0x09db: case 0x09e4 ... 0x09e5:
          case 0x09fb ... 0x0a01: case 0x0a03 ... 0x0a04:
          case 0x0a0b ... 0x0a0e: case 0x0a11 ... 0x0a12:
          case 0x0a3a ... 0x0a3b: case 0x0a43 ... 0x0a46:
          case 0x0a49 ... 0x0a4a: case 0x0a4e ... 0x0a58:
          case 0x0a5f ... 0x0a65: case 0x0a75 ... 0x0a80:
          case 0x0aba ... 0x0abb: case 0x0ace ... 0x0acf:
          case 0x0ad1 ... 0x0adf: case 0x0ae1 ... 0x0ae5:
          case 0x0af0 ... 0x0b00: case 0x0b0d ... 0x0b0e:
          case 0x0b11 ... 0x0b12: case 0x0b34 ... 0x0b35:
          case 0x0b3a ... 0x0b3b: case 0x0b44 ... 0x0b46:
          case 0x0b49 ... 0x0b4a: case 0x0b4e ... 0x0b55:
          case 0x0b58 ... 0x0b5b: case 0x0b62 ... 0x0b65:
          case 0x0b71 ... 0x0b81: case 0x0b8b ... 0x0b8d:
          case 0x0b96 ... 0x0b98: case 0x0ba0 ... 0x0ba2:
          case 0x0ba5 ... 0x0ba7: case 0x0bab ... 0x0bad:
          case 0x0bba ... 0x0bbd: case 0x0bc3 ... 0x0bc5:
          case 0x0bce ... 0x0bd6: case 0x0bd8 ... 0x0be6:
          case 0x0bf3 ... 0x0c00: case 0x0c3a ... 0x0c3d:
          case 0x0c4e ... 0x0c54: case 0x0c57 ... 0x0c5f:
          case 0x0c62 ... 0x0c65: case 0x0c70 ... 0x0c81:
          case 0x0cba ... 0x0cbd: case 0x0cce ... 0x0cd4:
          case 0x0cd7 ... 0x0cdd: case 0x0ce2 ... 0x0ce5:
          case 0x0cf0 ... 0x0d01: case 0x0d3a ... 0x0d3d:
          case 0x0d44 ... 0x0d45: case 0x0d4e ... 0x0d56:
          case 0x0d58 ... 0x0d5f: case 0x0d62 ... 0x0d65:
          case 0x0d70 ... 0x0d81: case 0x0d97 ... 0x0d99:
          case 0x0dbe ... 0x0dbf: case 0x0dc7 ... 0x0dc9:
          case 0x0dcb ... 0x0dce: case 0x0de0 ... 0x0df1:
          case 0x0df5 ... 0x0e00: case 0x0e3b ... 0x0e3e:
          case 0x0e5c ... 0x0e80: case 0x0e85 ... 0x0e86:
          case 0x0e8b ... 0x0e8c: case 0x0e8e ... 0x0e93:
          case 0x0ea8 ... 0x0ea9: case 0x0ebe ... 0x0ebf:
          case 0x0ece ... 0x0ecf: case 0x0eda ... 0x0edb:
          case 0x0ede ... 0x0eff: case 0x0f6b ... 0x0f70:
          case 0x0f8c ... 0x0f8f: case 0x0fcd ... 0x0fce:
          case 0x0fd0 ... 0x0fff: case 0x1033 ... 0x1035:
          case 0x103a ... 0x103f: case 0x105a ... 0x109f:
          case 0x10c6 ... 0x10cf: case 0x10f9 ... 0x10fa:
          case 0x10fc ... 0x10ff: case 0x115a ... 0x115e:
          case 0x11a3 ... 0x11a7: case 0x11fa ... 0x11ff:
          case 0x124e ... 0x124f: case 0x125e ... 0x125f:
          case 0x128e ... 0x128f: case 0x12b6 ... 0x12b7:
          case 0x12c6 ... 0x12c7: case 0x1316 ... 0x1317:
          case 0x135b ... 0x1360: case 0x137d ... 0x139f:
          case 0x13f5 ... 0x1400: case 0x1677 ... 0x167f:
          case 0x169d ... 0x169f: case 0x16f1 ... 0x16ff:
          case 0x1715 ... 0x171f: case 0x1737 ... 0x173f:
          case 0x1754 ... 0x175f: case 0x1774 ... 0x177f:
          case 0x17dd ... 0x17df: case 0x17ea ... 0x17ff:
          case 0x181a ... 0x181f: case 0x1878 ... 0x187f:
          case 0x18aa ... 0x1dff: case 0x1e9c ... 0x1e9f:
          case 0x1efa ... 0x1eff: case 0x1f16 ... 0x1f17:
          case 0x1f1e ... 0x1f1f: case 0x1f46 ... 0x1f47:
          case 0x1f4e ... 0x1f4f: case 0x1f7e ... 0x1f7f:
          case 0x1fd4 ... 0x1fd5: case 0x1ff0 ... 0x1ff1:
          case 0x2053 ... 0x2056: case 0x2058 ... 0x205e:
          case 0x2064 ... 0x2069: case 0x2072 ... 0x2073:
          case 0x208f ... 0x209f: case 0x20b2 ... 0x20cf:
          case 0x20eb ... 0x20ff: case 0x213b ... 0x213c:
          case 0x214c ... 0x2152: case 0x2184 ... 0x218f:
          case 0x23cf ... 0x23ff: case 0x2427 ... 0x243f:
          case 0x244b ... 0x245f: case 0x2614 ... 0x2615:
          case 0x267e ... 0x267f: case 0x268a ... 0x2700:
          case 0x270a ... 0x270b: case 0x2753 ... 0x2755:
          case 0x275f ... 0x2760: case 0x2795 ... 0x2797:
          case 0x27bf ... 0x27cf: case 0x27ec ... 0x27ef:
          case 0x2b00 ... 0x2e7f: case 0x2ef4 ... 0x2eff:
          case 0x2fd6 ... 0x2fef: case 0x2ffc ... 0x2fff:
          case 0x3097 ... 0x3098: case 0x3100 ... 0x3104:
          case 0x312d ... 0x3130: case 0x31b8 ... 0x31ef:
          case 0x321d ... 0x321f: case 0x3244 ... 0x3250:
          case 0x327c ... 0x327e: case 0x32cc ... 0x32cf:
          case 0x3377 ... 0x337a: case 0x33de ... 0x33df:
          case 0x4db6 ... 0x4dff: case 0x9fa6 ... 0x9fff:
          case 0xa48d ... 0xa48f: case 0xa4c7 ... 0xabff:
          case 0xd7a4 ... 0xd7ff: case 0xfa2e ... 0xfa2f:
          case 0xfa6b ... 0xfaff: case 0xfb07 ... 0xfb12:
          case 0xfb18 ... 0xfb1c: case 0xfbb2 ... 0xfbd2:
          case 0xfd40 ... 0xfd4f: case 0xfd90 ... 0xfd91:
          case 0xfdc8 ... 0xfdcf: case 0xfdfd ... 0xfdff:
          case 0xfe10 ... 0xfe1f: case 0xfe24 ... 0xfe2f:
          case 0xfe47 ... 0xfe48: case 0xfe6c ... 0xfe6f:
          case 0xfefd ... 0xfefe: case 0xffbf ... 0xffc1:
          case 0xffc8 ... 0xffc9: case 0xffd0 ... 0xffd1:
          case 0xffd8 ... 0xffd9: case 0xffdd ... 0xffdf:
          case 0xffef ... 0xfff8: case 0x10000 ... 0x102ff:
          case 0x10324 ... 0x1032f: case 0x1034b ... 0x103ff:
          case 0x10426 ... 0x10427: case 0x1044e ... 0x1cfff:
          case 0x1d0f6 ... 0x1d0ff: case 0x1d127 ... 0x1d129:
          case 0x1d1de ... 0x1d3ff: case 0x1d4a0 ... 0x1d4a1:
          case 0x1d4a3 ... 0x1d4a4: case 0x1d4a7 ... 0x1d4a8:
          case 0x1d50b ... 0x1d50c: case 0x1d547 ... 0x1d549:
          case 0x1d6a4 ... 0x1d6a7: case 0x1d7ca ... 0x1d7cd:
          case 0x1d800 ... 0x1fffd: case 0x2a6d7 ... 0x2f7ff:
          case 0x2fa1e ... 0x2fffd: case 0x30000 ... 0x3fffd:
          case 0x40000 ... 0x4fffd: case 0x50000 ... 0x5fffd:
          case 0x60000 ... 0x6fffd: case 0x70000 ... 0x7fffd:
          case 0x80000 ... 0x8fffd: case 0x90000 ... 0x9fffd:
          case 0xa0000 ... 0xafffd: case 0xb0000 ... 0xbfffd:
          case 0xc0000 ... 0xcfffd: case 0xd0000 ... 0xdfffd:
          case 0xe0002 ... 0xe001f: case 0xe0080 ... 0xefffd:
            return -1;

          default:
            break;
        }
    }

    /* To lower (approximately corresponds to mapping table B.2) */
    c = unicode_tolower(c);

    qv_append(code_points, c);
    return 0;
}

int sb_add_idna_domain_name(sb_t *sb, const char *src, int src_len,
                            unsigned flags)
{
    qv_t(u32) code_points;
    int pos = 0, label_size = 0;
    int nb_labels = 0;
    const char *label_begin_s = src;
    bool is_ascii = true;

#define IDNA_RETHROW(e)  \
    ({ typeof(e) __res = (e);                                                \
       if (unlikely(__res < 0))                                              \
           goto error;                                                       \
       __res;                                                                \
    })

    qv_inita(&code_points, src_len);

    while (pos < src_len) {
        int c = IDNA_RETHROW(utf8_ngetc_at(src, src_len, &pos));

        /* U+002E (full stop), U+3002 (ideographic full stop), U+FF0E
         * (fullwidth full stop) and U+FF61 (halfwidth ideographic full stop)
         * MUST be recognized as dots. */
        if (c != 0x002E && c != 0x3002 && c != 0XFF0E && c != 0xFF61) {
            if (isascii(c)) {
                if (flags & IDNA_USE_STD3_ASCII_RULES) {
                    /* Verify the absence of non-LDH ASCII code points */
                    if (unlikely(ctype_desc_contains(&ctype_is_non_ldh, c))) {
                        goto error;
                    }
                }
                qv_append(&code_points, c);
            } else {
                is_ascii = false;
                IDNA_RETHROW(idna_nameprep(&code_points, c, flags));
            }

            label_size = src + pos - label_begin_s;
            continue;
        }

        /* This is the end of a label */
        IDNA_RETHROW(idna_label_to_ascii(sb, label_begin_s, label_size,
                                         &code_points, is_ascii, flags));

        sb_addc(sb, '.');
        label_begin_s = src + pos;
        label_size    = 0;
        is_ascii = true;
        nb_labels++;
        qv_clear(&code_points);
    }

    /* Encode last label */
    IDNA_RETHROW(idna_label_to_ascii(sb, label_begin_s, label_size,
                                     &code_points, is_ascii, flags));

    /* XXX: this wipe is useless for now; it could become useful if our
     *      implementation of NAMEPREP becomes fully complient one day (it
     *      could increase the number of code points), so keep it to be safe.
     */
    qv_wipe(&code_points);

    return ++nb_labels >= 2 ? nb_labels : -1;

  error:
    qv_wipe(&code_points);
    return -1;
#undef IDNA_RETHROW
}

/*}}} */
