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

#include <locale.h>

#include "datetime.h"
#include "thr.h"

/*
 *  Portable interface to the CPU cycle counter
 *
 *  Copyright (C) 2006-2007  Christophe Devine
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of XySSL nor the names of its contributors may be
 *      used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * {{{
 */
#if (defined(_MSC_VER) && defined(_M_IX86)) || defined(__WATCOMC__)

unsigned long hardclock(void)
{
    unsigned long tsc;
    __asm   rdtsc
    __asm   mov  [tsc], eax
    return tsc;
}

#elif defined(__GNUC__)
# if defined(__i386__)

unsigned long hardclock(void)
{
    unsigned long tsc;
    asm("rdtsc" : "=a" (tsc));
    return tsc;
}

# elif defined(__amd64__) || defined(__x86_64__)

unsigned long hardclock(void)
{
    unsigned long lo, hi;
    asm("rdtsc" : "=a" (lo), "=d" (hi));
    return lo | (hi << 32);
}

# elif defined(__powerpc__) || defined(__ppc__)

unsigned long hardclock(void)
{
    unsigned long tbl, tbu0, tbu1;

    do {
        asm("mftbu %0" : "=r" (tbu0));
        asm("mftb  %0" : "=r" (tbl));
        asm("mftbu %0" : "=r" (tbu1));
    } while (tbu0 != tbu1);

    return tbl;
}

# elif defined(__sparc__)

unsigned long hardclock(void)
{
    unsigned long tick;
    asm(".byte 0x83, 0x41, 0x00, 0x00");
    asm("mov   %%g1, %0" : "=r" (tick));
    return tick;
}

# elif defined(__alpha__)

unsigned long hardclock(void)
{
    unsigned long cc;
    asm("rpcc %0" : "=r" (cc));
    return cc & 0xFFFFFFFF;
}

# elif defined(__ia64__)

unsigned long hardclock(void)
{
    unsigned long itc;
    asm("mov %0 = ar.itc" : "=r" (itc));
    return itc;
}
# else
#  error unimplemented for your arch
# endif
#endif

/* }}} */

/* {{{ timeval operations */

/* Arithmetics on timeval assume both members of timeval are signed.
 * We keep timeval structures in normalized form:
 * - tv_usec is always in the range [0 .. 999999]
 * - tv_sec is negative iff timeval is negative
 * This choice is consistent with sys/time.h macros,
 * but it makes it a little bit harder to print timevals.
 */

/* Return reference to static buf for immediate printing */
const char *timeval_format(struct timeval tv, bool as_duration)
{
    // FIXME: assuming 32 bit timeval resolution, bug in 2034
    static char buf[32];
    int pos, d, h, m, s, sec, usec;

    usec = tv.tv_usec;

    if (as_duration) {
        pos = 0;
        sec = tv.tv_sec;
        if (sec < 0) {
            buf[pos++] = '-';
            sec = -sec;
            if (usec != 0) {
                usec = 1000 * 1000 - usec;
                sec -= 1;
            }
        }
        s = sec % 60;
        sec /= 60;
        m = sec % 60;
        sec /= 60;
        h = sec % 24;
        sec /= 24;
        d = sec;
        snprintf(buf + pos, sizeof(buf) - pos,
                 "%d %2d:%02d:%02d.%06d", d, h, m, s, usec);
    } else {
        struct tm t;

        localtime_r(&tv.tv_sec, &t);
        snprintf(buf, sizeof(buf),
                 "%02d/%02d/%04d %2d:%02d:%02d.%06d",
                 t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
                 t.tm_hour, t.tm_min, t.tm_sec, usec);
    }
    return buf;
}

struct timeval timeval_mul(struct timeval tv, int k)
{
    struct timeval res;
    int64_t usecs;

    /* Casting to int64_t is necessary because of potential overflow */
    /* Grouping 1000 * 1000 may yield better code */
    usecs  = (int64_t)tv.tv_sec * (1000 * 1000) + tv.tv_usec;
    usecs *= k;

    res.tv_sec  = usecs / (1000 * 1000);
    res.tv_usec = usecs - (int64_t)res.tv_sec * (1000 * 1000);
    if (res.tv_usec < 0) {
        res.tv_usec += 1000 * 1000;
        res.tv_sec  -= 1;
    }

    return res;
}

struct timeval timeval_div(struct timeval tv, int k)
{
    struct timeval res;
    int64_t usecs;

    usecs  = (int64_t)tv.tv_sec * (1000 * 1000) + tv.tv_usec;
    usecs /= k;

    res.tv_sec  = usecs / (1000 * 1000);
    res.tv_usec = usecs - (int64_t)res.tv_sec * (1000 * 1000);
    if (res.tv_usec < 0) {
        res.tv_usec += 1000 * 1000;
        res.tv_sec  -= 1;
    }

    return res;
}

/* Test if timer has expired: if now is provided, test if date is past
 * now, else test if date is past current time of day.  compute
 * available time left and return it if left is not NULL.
 */
bool is_expired(const struct timeval *date,
                const struct timeval *now,
                struct timeval *left)
{
    struct timeval local_now;

    if (!now) {
        now = &local_now;
        gettimeofday(&local_now, NULL);
    }
    if (left) {
        *left = timeval_sub(*date, *now);
        return timeval_is_le0(*left);
    }
    return timeval_is_le0(timeval_sub(*date, *now));
}

/* }}} */
/* {{{ time.h wrappers */

static int localtime_(time_t date, struct tm *t)
{
    if (date == 0) {
        date = time(NULL);
    }

    if (!localtime_r(&date, t)) {
        return -1;
    }
    return 0;
}

time_t localtime_curminute(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec = 0;

    return mktime(&t);
}

time_t localtime_nextminute(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec  = 0;
    t.tm_min  += 1;

    return mktime(&t);
}

time_t localtime_curhour(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec = 0;
    t.tm_min = 0;

    return mktime(&t);
}

time_t localtime_nexthour(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec  = 0;
    t.tm_min  = 0;
    t.tm_hour += 1;

    return mktime(&t);
}

time_t localtime_curday(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_isdst = -1;

    return mktime(&t);
}

time_t localtime_addday(time_t date, int n)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    /* To avoid complex calculations, we rely on mktime() to normalize
     * the tm structure as specified in the man page:
     *
     * "The mktime() function converts a broken-down time structure,
     * expressed as local time, to calendar time representation.  The
     * function ignores the specified contents of the structure members
     * tm_wday and tm_yday and recomputes them from the other
     * information in the broken-down time structure.  If structure
     * members are outside their legal interval, they will be
     * normalized (so that, e.g., 40 October is changed into 9 Novem-
     * ber).  Calling mktime() also sets the external variable tzname
     * with information about the current time zone.  If the specified
     * broken-down time cannot be represented as calendar time (seconds
     * since the epoch), mktime() returns a value of (time_t)(-1) and
     * does not alter the tm_wday and tm_yday members of the
     * broken-down time structure."
     */

    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday += n;
    t.tm_isdst = -1;

    return mktime(&t);
}

time_t localtime_nextday(time_t date)
{
    return localtime_addday(date, 1);
}

time_t localtime_curweek(time_t date, int first_day_of_week)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec   = 0;
    t.tm_min   = 0;
    t.tm_hour  = 0;
    t.tm_mday -= (7 + t.tm_wday - first_day_of_week) % 7;
    t.tm_isdst = -1;

    return mktime(&t);
}

time_t localtime_nextweek(time_t date, int first_day_of_week)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec  = 0;
    t.tm_min  = 0;
    t.tm_hour = 0;
    t.tm_mday -= (7 + t.tm_wday - first_day_of_week) % 7;
    t.tm_mday += 7;
    t.tm_isdst = -1;

    return mktime(&t);
}

time_t localtime_curmonth(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_isdst = -1;

    return mktime(&t);
}

time_t localtime_curyear(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec = 0;
    t.tm_min = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon  = 0;
    t.tm_isdst = -1;

    return mktime(&t);
}

time_t localtime_nextmonth(time_t date)
{
    struct tm t;

    RETHROW(localtime_(date, &t));

    t.tm_sec  = 0;
    t.tm_min  = 0;
    t.tm_hour = 0;
    t.tm_mday = 1;
    t.tm_mon += 1;
    t.tm_isdst = -1;

    return mktime(&t);
}

static const char * const __abbr_months[] = {
    "jan",
    "feb",
    "mar",
    "apr",
    "may",
    "jun",
    "jul",
    "aug",
    "sep",
    "oct",
    "nov",
    "dec",
    NULL
};

static int const __valid_mdays[] = {
    31,
    28,
    31,
    30,
    31,
    30, /* June */
    31,
    31,
    30,
    31, /* October */
    30,
    31
};

bool is_mday_valid(int d, int m, int y)
{
    assert (0 <= m && m <= 11);

    return d > 0
        && (d <= __valid_mdays[m]
         || (m == 1 && d == 29 && year_is_leap_year(y)));
}

/* We currently support only this format: DD-MMM-[YY]YY */
/* WARNING: Only date related tm structure fields are changed, others
 * must be initialized before this call.
 */
/* OG: why not use strptime ? */
int strtotm(const char *date, struct tm *t)
{
    int mday, mon, year;
    const char *p;
    char lower_mon[4];
    size_t len = strlen(date);

    if (len < strlen("DD-MMM-YY"))
        return -1;

    /* Read day */
    mday = cstrtol(date, &p, 10);
    if (mday <= 0)
        return -1;

    /* Read month */
    if (!p || *p != '-' || p - date != 2)
        return -1;
    p++;

    lower_mon[0] = tolower((unsigned char)p[0]);
    lower_mon[1] = tolower((unsigned char)p[1]);
    lower_mon[2] = tolower((unsigned char)p[2]);
    lower_mon[3] = '\0';

    for (mon = 0;; mon++) {
        if (!__abbr_months[mon])
            return -1;
        if (!memcmp(lower_mon, __abbr_months[mon], 4))
            break;
    }

    /* Read year */
    p += 3;
    if (*p++ != '-')
        return -1;

    year = RETHROW(atoi(p));
    if (year < 70) {
        year += 2000;
    } else
    if (year < 100) {
        year += 1900;
    }
    if (year < 1970 || year > 2036)
        return -1;

    /* Check mday validity */
    if (!is_mday_valid(mday, mon, year)) {
        return -1;
    }

    t->tm_mday = mday;
    t->tm_mon  = mon;
    t->tm_year = year - 1900;
    /* OG: should also update t->tm_wday and t->tm_yday */

    return 0;
}

time_t lstrtotime(lstr_t date)
{
    SB_1k(str);
    struct tm tm;

    sb_add_lstr(&str, date); /* Ensures the date is null terminated. */
    p_clear(&tm, 1);
    tm.tm_isdst = -1;
    RETHROW(strtotm(str.data, &tm));
    return mktime(&tm);
}

struct tm *time_get_localtime(const time_t *p_ts, struct tm *p_tm,
                              const char *tz)
{
    const char *old_tz;
    bool tz_changed = false;

    if (tz) {
        old_tz = getenv("TZ");

        if (!old_tz || !strequal(old_tz, tz)) {
            setenv("TZ", tz, true);
            tzset();
            tz_changed = true;
        }
    }

    localtime_r(p_ts, p_tm);

    if (tz_changed) {
        if (old_tz) {
            setenv("TZ", old_tz, true);
        } else {
            unsetenv("TZ");
        }

        tzset();
    }

    return p_tm;
}

int time_diff_days(time_t from, time_t to)
{
    struct tm tm_from;
    struct tm tm_to;

    if (!expect(localtime_(from, &tm_from) >= 0)
    ||  !expect(localtime_(to, &tm_to) >= 0))
    {
        return -1;
    }
    return tm_diff_days(&tm_from, &tm_to);
}

#if __GNUC_PREREQ(4, 2)
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
int format_timestamp(const char *fmt, time_t ts, const char *locale,
                     char out[], int out_size)
{
    struct tm ts_tm;
    const char *lc_time = NULL;
    int len;
    int ret;

    if (strequal(fmt, "%s")) {
        len = snprintf(out, out_size, "%ld", ts);
        return len;
    }
    if (locale) {
        lc_time = setlocale(LC_TIME, NULL);
        RETHROW_PN(setlocale(LC_TIME, locale));
    }

    if (localtime_r(&ts, &ts_tm) == NULL) {
        ret = -1;
        goto end;
    }
    len = strftime(out, out_size, fmt, &ts_tm);
    ret = len ?: -1;

  end:
    if (locale) {
        setlocale(LC_TIME, lc_time);
    }
    return ret;
}
#if __GNUC_PREREQ(4, 2)
#pragma GCC diagnostic warning "-Wformat-nonliteral"
#endif

/* }}} */
/* {{{ Timers for benchmarks */

const char *proctimer_report(proctimer_t *tp, const char *fmt)
{
    static char buf[256];
    int pos;
    int elapsed;
    const char *p;

    if (!fmt) {
        fmt = "real %rms, proc %pms, user %ums, sys %sms, %h cycles";
    }

    for (p = fmt, pos = 0; *p && pos < ssizeof(buf) - 1; p++) {
        if (*p == '%') {
            switch (*++p) {
            case 'r':   /* real */
                elapsed = tp->elapsed_real;
                goto format_elapsed;
            case 'u':   /* user */
                elapsed = tp->elapsed_user;
                goto format_elapsed;
            case 's':   /* sys */
                elapsed = tp->elapsed_sys;
                goto format_elapsed;
            case 'p':   /* process */
                elapsed = tp->elapsed_proc;
                goto format_elapsed;
            format_elapsed:
                snprintf(buf + pos, sizeof(buf) - pos, "%d.%03d",
                         elapsed / 1000, elapsed % 1000);
                pos += strlen(buf + pos);
                continue;

            case 'h':   /* hardware */
                snprintf(buf + pos, sizeof(buf) - pos, "%lu",
                         tp->elapsed_hard);
                pos += strlen(buf + pos);
                continue;

            case '%':
            default:
                break;
            }
        }
        buf[pos++] = *p;
    }
    buf[pos] = '\0';
    return buf;
}

const char *proctimerstat_report(proctimerstat_t *pts, const char *fmt)
{
    static char buf[1024];
    int pos;
    unsigned long min, max, tot, mean;
    const char *p;
    const char *unit;

    if (!fmt) {
        fmt = "%n samples\nreal: %r\nproc: %p\nuser: %u\nsys : %s\nproc cycles: %h";
    }

    for (p = fmt, pos = 0; *p && pos < ssizeof(buf) - 1; p++) {
        unit = "ms";

        if (*p == '%') {
            switch (*++p) {
            case 'n':   /* nb samples */
                snprintf(buf + pos, sizeof(buf) - pos, "%d", pts->nb);
                pos += strlen(buf + pos);
                continue;
            case 'r':   /* real */
                min = pts->real_min;
                max = pts->real_max;
                tot = pts->real_tot;
                goto format;
            case 'u':   /* user */
                min = pts->user_min;
                max = pts->user_max;
                tot = pts->user_tot;
                goto format;
            case 's':   /* sys */
                min = pts->sys_min;
                max = pts->sys_max;
                tot = pts->sys_tot;
                goto format;
            case 'p':   /* process */
                min = pts->proc_min;
                max = pts->proc_max;
                tot = pts->proc_tot;
                goto format;
            case 'h':   /* hardware */
                unit = "cycles";
                min = pts->hard_min;
                max = pts->hard_max;
                tot = pts->hard_tot;
                goto format;
            format:
                mean = tot / MAX(1, pts->nb);
                snprintf(buf + pos, sizeof(buf) - pos,
                         "min=%ld.%03ld%s "
                         "max=%ld.%03ld%s "
                         "mean=%ld.%03ld%s",
                         min / 1000, min % 1000, unit,
                         max / 1000, max % 1000, unit,
                         mean / 1000, mean % 1000, unit
                         );
                pos += strlen(buf + pos);
                continue;
            case '%':
            default:
                break;
            }
        }
        buf[pos++] = *p;
    }
    buf[pos] = '\0';
    return buf;
}

/* }}} */
/* {{{ Time amount splitting and formatting */

time_split_t split_time_interval(uint64_t seconds)
{
    time_split_t res;

    p_clear(&res, 1);

    res.years = seconds / (3600 * 24 * 365);
    seconds %= (3600 * 24 * 365);

    res.weeks = seconds / (3600 * 24 * 7);
    seconds %= (3600 * 24 * 7);

    res.days = seconds / (3600 * 24);
    seconds %= (3600 * 24);

    res.hours = seconds / 3600;
    seconds %= 3600;

    res.minutes = seconds / 60;
    seconds %= 60;

    res.seconds = seconds;

    return res;
}

#define ADD_FIELD(field, str)                                                \
    do {                                                                     \
        if (split.field) {                                                   \
            if (sb.len) {                                                    \
                sb_adds(&sb, ", ");                                          \
            }                                                                \
            sb_addf(&sb, "%d " str, split.field);                            \
            if (split.field > 1) {                                           \
                sb_addc(&sb, 's');                                           \
            }                                                                \
        }                                                                    \
        if (sb.len && precision-- == 0) {                                    \
            return LSTR_SB_V(&sb);                                           \
        }                                                                    \
    } while (0)

lstr_t t_get_time_split_p_lstr_en(uint64_t seconds, int precision)
{
    t_SB_1k(sb);

    time_split_t split = split_time_interval(seconds);

    ADD_FIELD(years,   "year");
    ADD_FIELD(weeks,   "week");
    ADD_FIELD(days,    "day");
    ADD_FIELD(hours,   "hour");
    ADD_FIELD(minutes, "minute");
    ADD_FIELD(seconds, "second");

    return LSTR_SB_V(&sb);
}

lstr_t t_get_time_split_p_lstr_fr(uint64_t seconds, int precision)
{
    t_SB_1k(sb);

    time_split_t split = split_time_interval(seconds);

    ADD_FIELD(years,   "année");
    ADD_FIELD(weeks,   "semaine");
    ADD_FIELD(days,    "jour");
    ADD_FIELD(hours,   "heure");
    ADD_FIELD(minutes, "minute");
    ADD_FIELD(seconds, "seconde");

    return LSTR_SB_V(&sb);
}

#undef ADD_FIELD

/* }}} */
/* {{{ Low precision time() and gettimeofday() replacements */

static __thread struct {
    char sec_str[24];
    time_t sec;
} lp_time_g;

__attribute__((weak)) const char *lp_getsec_str(void)
{
    return lp_time_g.sec_str;
}

__attribute__((weak)) time_t lp_getsec(void)
{
    if (unlikely(!lp_time_g.sec || !thr_is_on_queue(thr_queue_main_g))) {
        /* XXX: in the main thead, lp_gettv is called at the beginning of the
         *      event loop, so we have the guarantee the cached values are "up
         *      to date". This is not a guarantee we have in the other
         *      threads, so call lp_gettv to refresh the cache.
         */
        struct timeval tv;

        lp_gettv(&tv);
        return tv.tv_sec;
    }
    return lp_time_g.sec;
}

__attribute__((weak)) void lp_gettv(struct timeval *tv)
{
    gettimeofday(tv, NULL);
    if (unlikely(lp_time_g.sec != tv->tv_sec)) {
        lp_time_g.sec = tv->tv_sec;
        (sprintf)(lp_time_g.sec_str, "%lld", (long long)lp_time_g.sec);
    }
}

__attribute__((weak)) uint64_t lp_getmsec(void)
{
    struct timeval tv;

    lp_gettv(&tv);
    return tv.tv_sec * 1000ull + tv.tv_usec / 1000ull;
}

__attribute__((weak)) uint64_t lp_getcsec(void)
{
    return lp_getmsec() / 10ull;
}

/* }}} */
/* {{{ timing_scope */

timing_scope_ctx_t
timing_scope_start(logger_t *logger,
                   const char *file, const char *func, int line,
                   int64_t timeout_ms, const char *fmt, ...)
{
    va_list va;
    timing_scope_ctx_t res;

    p_clear(&res, 1);
    res.logger     = logger;
    res.file       = file;
    res.func       = func;
    res.line       = line;
    res.timeout_ms = timeout_ms;

    va_start(va, fmt);
    res.desc = lstr_vfmt(fmt, va);
    va_end(va);

    lp_gettv(&res.tv_start);

    return res;
}

void timing_scope_finish(timing_scope_ctx_t *ctx)
{
    struct timeval tv_end;
    int level;

    lp_gettv(&tv_end);

    if (timeval_diffmsec(&tv_end, &ctx->tv_start) >= ctx->timeout_ms) {
        level = LOG_WARNING;
    } else {
        level = LOG_TRACE + 1;
    }

    if (logger_has_level(ctx->logger, level)) {
        struct timeval tv_diff;

        tv_diff = timeval_sub(tv_end, ctx->tv_start);

        __logger_log(ctx->logger, level, NULL, -1,
                     ctx->file, ctx->func, ctx->line,
                     "%*pM done in %ld.%06ldsec (expected less than "
                     "%ld.%06ldsec)",
                     LSTR_FMT_ARG(ctx->desc),
                     tv_diff.tv_sec, tv_diff.tv_usec,
                     ctx->timeout_ms / 1000, (ctx->timeout_ms % 1000) * 1000);
    }

    lstr_wipe(&ctx->desc);
}

/* }}} */
/* {{{ t_time_spent_to_str() */

const char *t_time_spent_to_str(struct timeval from_tv)
{
    struct timeval tv;

    lp_gettv(&tv);
    tv = timeval_sub(tv, from_tv);

    return t_fmt("%ld.%06ld sec", tv.tv_sec, tv.tv_usec);
}

/* }}} */
