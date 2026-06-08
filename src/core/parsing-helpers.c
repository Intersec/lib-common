/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <lib-common/parsing-helpers.h>

/** Handle Unicode surrogate pairs for characters beyond BMP.
 *
 * This function checks if we have a valid surrogate pair and converts it
 * back to the original Unicode codepoint.
 *
 * \param[in]     ps        The parsing stream (must have at least 6 bytes)
 * \param[in]     codepoint The first Unicode codepoint (potential high
 *                          surrogate)
 * \param[in,out] buf       The string buffer to append the result to
 * \return                  Number of characters to skip (6 for BMP,
 *                          12 for surrogate pair), or -1 on error
 */
static int
handle_unicode_surrogate_pair(pstream_t *ps, int codepoint, sb_t *buf)
{
    /* Check if this is a high surrogate (0xD800-0xDBFF) */
    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && ps_has(ps, 12) &&
        ps->s[6] == '\\' && ps->s[7] == 'u')
    {
        int low_a, low_b, low_surrogate;

        /* Try to parse the low surrogate */
        low_a = PS_CHECK(hexdecode(ps->s + 8));
        low_b = PS_CHECK(hexdecode(ps->s + 10));
        low_surrogate = (low_a << 8) | low_b;

        /* Check if this is a valid low surrogate (0xDC00-0xDFFF) */
        if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
            /* Convert surrogate pair back to original codepoint */
            int actual_cp;

            actual_cp = 0x10000 + ((codepoint - 0xD800) << 10) +
                        (low_surrogate - 0xDC00);

            sb_adduc(buf, actual_cp);
            return 12;
        }
    }

    /* Regular BMP character or unpaired surrogate (pass through as-is) */
    sb_adduc(buf, codepoint);
    return 6;
}

int parse_backslash(pstream_t *ps, sb_t *buf, int *line, int *col)
{
#define SKIP(i)                                                              \
    ({                                                                       \
        int tmp = (i);                                                       \
        *col += tmp;                                                         \
        __ps_skip(ps, tmp);                                                  \
    })

    if (!ps_has(ps, 2)) {
        return -1;
    }

    switch (ps->b[1]) {
        int a, b;

    case 'a':
    case 'b':
    case 'e':
    case 't':
    case 'n':
    case 'v':
    case 'f':
    case 'r':
        sb_add_unquoted(buf, ps->p, 2);
        SKIP(2);
        return 0;
    case '\\':
    case '"':
    case '\'':
    case '/':
        sb_addc(buf, ps->b[1]);
        SKIP(2);
        return 0;
    case '0' ... '2':
        if (ps_has(ps, 4) && ps->b[2] >= '0' && ps->b[2] <= '7' &&
            ps->b[3] >= '0' && ps->b[3] <= '7')
        {
            sb_addc(
                buf, ((ps->b[1] - '0') << 6) | ((ps->b[2] - '0') << 3) |
                         (ps->b[3] - '0')
            );
            SKIP(4);
            return 0;
        }
        if (ps->b[1] == '0') {
            sb_addc(buf, '\0');
            SKIP(2);
            return 0;
        }
        break;
    case 'x':
        if (ps_has(ps, 4)) {
            sb_addc(buf, PS_CHECK(hexdecode(ps->s + 2)));
            SKIP(4);
            return 0;
        }
        break;
    case 'u': {
        if (ps_has(ps, 6)) {
            int codepoint, skip_len;

            a = PS_CHECK(hexdecode(ps->s + 2));
            b = PS_CHECK(hexdecode(ps->s + 4));
            codepoint = (a << 8) | b;

            /* Handle Unicode character (BMP or surrogate pair) */
            skip_len = handle_unicode_surrogate_pair(ps, codepoint, buf);
            if (skip_len < 0) {
                return -1;
            }
            SKIP(skip_len);
            return 0;
        }
        break;
    }
    case '\n':
        sb_add(buf, ps->p, 2);
        SKIP(2);
        (*line)++;
        *col = 1;
        return 0;
    }

    sb_add(buf, ps->p, 2);
    SKIP(2);

#undef SKIP

    return 0;
}
