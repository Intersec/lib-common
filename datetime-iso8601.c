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

#include "datetime.h"

static int time_parse_timezone(pstream_t *ps, int *tz_h, int *tz_m)
{
    *tz_m = 0;
    *tz_h = 0;

    if (ps_strcaseequal(ps, "ut") || ps_strcaseequal(ps, "gmt")
    ||  ps_strcaseequal(ps, "z"))
    {
        return 0;
    } else
    if (ps_strcaseequal(ps, "edt")) {
        *tz_h = -4;
        return 0;
    } else
    if (ps_strcaseequal(ps, "est") || ps_strcaseequal(ps, "cdt")) {
        *tz_h = -5;
        return 0;
    } else
    if (ps_strcaseequal(ps, "cst") || ps_strcaseequal(ps, "mdt")) {
        *tz_h = -6;
        return 0;
    } else
    if (ps_strcaseequal(ps, "mst") || ps_strcaseequal(ps, "pdt")) {
        *tz_h = -7;
        return 0;
    } else
    if (ps_strcaseequal(ps, "pst")) {
        *tz_h = -8;
        return 0;
    } else
    if (ps_strcaseequal(ps, "a")) {
        *tz_h = -1;
        return 0;
    } else
    if (ps_strcaseequal(ps, "m")) {
        *tz_h = -12;
        return 0;
    } else
    if (ps_strcaseequal(ps, "n")) {
        *tz_h = +1;
        return 0;
    } else
    if (ps_strcaseequal(ps, "y")) {
        *tz_h = +12;
        return 0;
    } else {
        int sgn = ps_getc(ps);

        PS_WANT(sgn == '+' || sgn == '-');

        errno = 0;
        PS_WANT(isdigit(ps->b[0]));
        if (ps_len(ps) == 5) {
            *tz_h = ps_geti(ps);
            PS_WANT(errno == 0 && ps_len(ps) == 3);
            PS_CHECK(ps_skipc(ps, ':'));
            PS_WANT(isdigit(ps->b[0]));
            *tz_m = ps_geti(ps);
            PS_WANT(errno == 0 && ps_done(ps));
        } else
        if (ps_len(ps) == 4) {
            uint32_t raw = ps_geti(ps);

            PS_WANT(errno == 0 && ps_done(ps));
            *tz_h = raw / 100;
            *tz_m = raw % 100;
        } else
        if (ps_len(ps) == 2) {
            *tz_h = ps_geti(ps);
            PS_WANT(errno == 0 && ps_done(ps));
        } else {
            return -1;
        }

        if (sgn == '-') {
            *tz_h = -1 * *tz_h;
            *tz_m = -1 * *tz_m;
        }
        return 0;
    }
    return -1;
}

static int time_parse_iso8601_tok(pstream_t *ps, int *nb, int *type)
{
    *nb = ps_geti(ps);
    if (!ps_has(ps, 1)) {
        e_debug("invalid date tok");
        return -1;
    }
    *type = toupper(__ps_getc(ps));
    return 0;
}

int time_parse_iso8601_flags(pstream_t *ps, time_t *res, unsigned flags)
{
    struct tm t;
    bool local = false, syslog_format = false;
    int tz_h, tz_m;
    char c;

    if (!ps_has(ps, 1)) {
        e_debug("invalid date: empty");
        return -1;
    }

    if (*ps->s == 'P') {
        /* Relative date */
        int nb, tok;
        time_t now = lp_getsec();

        localtime_r(&now, &t);

        __ps_skip(ps, 1);
        if (ps_done(ps))
            goto end_rel;
        RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        if (tok == 'Y') {
            t.tm_year += nb;
            if (ps_done(ps))
                goto end_rel;
            RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        }
        if (tok == 'M') {
            t.tm_mon += nb;
            if (ps_done(ps))
                goto end_rel;
            RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        }
        if (tok == 'D') {
            t.tm_mday += nb;
            if (ps_done(ps))
                goto end_rel;
            RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        }
        if (tok != 'T') {
            e_debug("missing 'T' time mark");
            return -1;
        }
        RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        if (tok == 'H') {
            t.tm_hour += nb;
            if (ps_done(ps))
                goto end_rel;
            RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        }
        if (tok == 'M') {
            t.tm_min += nb;
            if (ps_done(ps))
                goto end_rel;
            RETHROW(time_parse_iso8601_tok(ps, &nb, &tok));
        }
        if (tok == 'S') {
            t.tm_sec += nb;
        }

      end_rel:
        t.tm_isdst = -1;
        *res = mktime(&t);
        return 0;
    }

    p_clear(&t, 1);

    t.tm_year = ps_geti(ps) - 1900;
    if (t.tm_year <= 0 || t.tm_year > 200) {
        e_debug("invalid year in date");
        return -1;
    }
    if (ps_getc(ps) != '-') {
        e_debug("invalid date format, missing '-' after year");
        return -1;
    }

    t.tm_mon = ps_geti(ps) - 1;
    if (t.tm_mon < 0 || t.tm_mon > 11) {
        e_debug("invalid month in date");
        return -1;
    }
    if (ps_getc(ps) != '-') {
        e_debug("invalid date format, missing '-' after month");
        return -1;
    }

    t.tm_mday = ps_geti(ps);
    if (!is_mday_valid(t.tm_mday, t.tm_mon, t.tm_year + 1900)) {
        e_debug("invalid day in date");
        return -1;
    }

    if (ps_done(ps)) {
        if ((flags & ISO8601_RESTRICT_DAY_DATE_FORMAT)
        ||  (flags & ISO8601_ALLOW_DAY_DATE_FORMAT))
        {
            t.tm_isdst = -1;
            *res = mktime(&t);
            return 0;
        } else {
            e_debug("day date format `YYYY-MM-DD` is not allowed");
            return -1;
        }
    }

    if (flags & ISO8601_RESTRICT_DAY_DATE_FORMAT) {
        e_debug("input is not a day");
        return -1;
    }

    c = ps_getc(ps);
    if (toupper(c) != 'T') {
        if ((flags & ISO8601_ALLOW_SYSLOG_FORMAT) && c == ' ') {
            syslog_format = true;
        } else {
            e_debug("invalid date format, missing 'T' after day");
            return -1;
        }
    }

    t.tm_hour = ps_geti(ps);
    switch (ps_getc(ps)) {
      case 'L':
      case 'l':
        local = true;
        break;

      case ':':
        break;

      default:
        e_debug("invalid date format, invalid char after hour");
        return -1;
    }

    t.tm_min = ps_geti(ps);
    if (ps_getc(ps) != ':') {
        e_debug("invalid date format, missing ':' after minutes");
        return -1;
    }

    t.tm_sec = ps_geti(ps);
    if (ps_startswith(ps, ".", 1)) { /* Ignore it */
        ps_getc(ps);
        ps_geti(ps);
    }

    if (local) {
        t.tm_isdst = -1;
        *res = mktime(&t);
        return 0;
    }

    if (ps_done(ps)) {
        t.tm_isdst = -1;
        *res = mktime(&t);
        return 0;
    }

    if (syslog_format) {
        RETHROW(ps_skipc(ps, ' '));
    }
    RETHROW(time_parse_timezone(ps, &tz_h, &tz_m));

    /* subtract the offset from the local time to get UTC time */
    t.tm_hour -= tz_h;
    t.tm_min  -= tz_m;
    t.tm_isdst = 0;
    *res = timegm(&t);
    return 0;
}

int time_parse(pstream_t *ps, time_t *d)
{
    struct tm date;
    int len = ps_len(ps);

    p_clear(&date, 1);
    date.tm_isdst = -1;

#define PARSE_FORMAT(format, skip_space)                                     \
    do {                                                                     \
        t_scope;                                                             \
        char *end = strptime(t_dupz(ps->s, len), format, &date);             \
                                                                             \
        if (end == NULL) {                                                   \
            return -1;                                                       \
        } else                                                               \
        if (*end != '\0') {                                                  \
            pstream_t ts = ps_initstr(end);                                  \
            int tz_h, tz_m;                                                  \
                                                                             \
            if (skip_space) {                                                \
                ps_ltrim(&ts);                                               \
            }                                                                \
            RETHROW(time_parse_timezone(&ts, &tz_h, &tz_m));                 \
            date.tm_hour -= tz_h;                                            \
            date.tm_min  -= tz_m;                                            \
            *d = timegm(&date);                                              \
            return 0;                                                        \
        }                                                                    \
        *d = mktime(&date);                                                  \
        return 0;                                                            \
    } while (0);

    if (len > 4 && (ps->s[0] == 'P' || ps->s[4] == '-')) {
        unsigned flags;

        /* ISO8601: YYYY-MM-DD[Thh:mm:ss] */
        flags = ISO8601_ALLOW_SYSLOG_FORMAT | ISO8601_ALLOW_DAY_DATE_FORMAT;
        return time_parse_iso8601_flags(ps, d, flags);
    } else
    if (len > 3 && (ps->s[1] == ' ' || ps->s[2] == ' ')) {
        /* RFC822: D month YYYY hh:mm:ss tz */
        PARSE_FORMAT("%d%n%h%n%Y%n%T", true);
    } else
    if (len > 4 && ps->s[3] == ',') {
        /* RFC822: Day, D month YYYY hh:mm:ss tz */
        PARSE_FORMAT("%a,%n%d%n%h%n%Y%n%T", true);
    } else {
        /* Unix timestamp */
        errno = 0;
        *d = ps_getlli(ps);
        if (errno || !ps_done(ps)) {
            return -1;
        }
        return 0;
    }
#undef PARSE_FORMAT
}
