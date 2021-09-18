/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#include <lib-common/net.h>
#include <lib-common/container-qvector.h>

int ps_copyv(pstream_t *ps, struct iovec *iov, size_t *iov_len, int *flags)
{
    int orig_len = ps_len(ps);
    size_t i;

    for (i = 0; !ps_done(ps) && i < *iov_len; i++) {
        if (iov[i].iov_len > ps_len(ps))
            iov[i].iov_len = ps_len(ps);
        memcpy(iov[i].iov_base, ps->b, iov[i].iov_len);
        ps_skip(ps, iov[i].iov_len);
    }
    *iov_len = i;

    if (flags) {
        if (ps_done(ps)) {
            *flags &= ~MSG_TRUNC;
            return orig_len;
        }
        if (*flags & MSG_TRUNC)
            return orig_len;
        *flags |= MSG_TRUNC;
    }
    return orig_len - ps_len(ps);
}

static int ps_get_csv_quoted_field(mem_pool_t *mp, pstream_t *ps, int quote,
                                   qv_t(lstr) *fields)
{
    SB_8k(sb);

    __ps_skip(ps, 1);

    for (;;) {
        pstream_t part;

        PS_CHECK(ps_get_ps_chr_and_skip(ps, quote, &part));
        if (!ps_done(ps) && *ps->s == quote) {
            __ps_skip(ps, 1);
            sb_add(&sb, part.s, ps_len(&part) + 1);
        } else
        if (sb.len == 0) {
            qv_append(fields, LSTR_PS_V(&part));
            return 0;
        } else {
            sb_add(&sb, part.s, ps_len(&part));

            if (mp) {
                qv_append(fields, mp_lstr_dups(mp, sb.data, sb.len));
            } else {
                lstr_t dst = LSTR_NULL;

                lstr_transfer_sb(&dst, &sb, false);
                qv_append(fields, dst);
            }
            return 0;
        }
    }

    return 0;
}

int ps_get_csv_line(mem_pool_t *mp, pstream_t *ps, int sep, int quote,
                    qv_t(lstr) *fields, pstream_t *out_line)
{
    ctype_desc_t cdesc;
    char cdesc_tok[] = { '\r', '\n', sep, '\0' };
    pstream_t out = *ps;

    ctype_desc_build(&cdesc, cdesc_tok);

    if (!out_line) {
        out_line = &out;
    }

    if (ps_done(ps)) {
        *out_line = ps_init(NULL, 0);
        return 0;
    }

    for (;;) {
        if (ps_done(ps)) {
            qv_append(fields, LSTR_NULL_V);
            *out_line = ps_initptr(out.s, ps->s);
            return 0;
        } else
        if (*ps->s == quote) {
            PS_CHECK(ps_get_csv_quoted_field(mp, ps, quote, fields));
        } else {
            pstream_t field = ps_get_cspan(ps, &cdesc);

            if (!ps_len(&field)) {
                qv_append(fields, LSTR_NULL_V);
            } else {
                qv_append(fields, LSTR_PS_V(&field));
            }
        }

        switch (ps_getc(ps)) {
          case '\r':
            *out_line = ps_initptr(out.s, ps->s - 1);
            return ps_skipc(ps, '\n');

          case '\n':
            *out_line = ps_initptr(out.s, ps->s - 1);
            return 0;

          case EOF:
            *out_line = ps_initptr(out.s, ps->s);
            return 0;

          default:
            PS_WANT(ps->s[-1] == sep);
            break;
        }
    }

    return 0;
}

void ps_split(pstream_t ps, const ctype_desc_t *sep, unsigned flags,
              qv_t(lstr) *res)
{
    if (flags & PS_SPLIT_SKIP_EMPTY) {
        ps_skip_span(&ps, sep);
    }
    while (!ps_done(&ps)) {
        pstream_t n = ps_get_cspan(&ps, sep);

        qv_append(res, LSTR_PS_V(&n));
        if (flags & PS_SPLIT_SKIP_EMPTY) {
            ps_skip_span(&ps, sep);
        } else {
            ps_skip(&ps, 1);
        }
    }
}

void ps_split_escaped(mem_pool_t *nullable mp, pstream_t ps,
                      const ctype_desc_t *nonnull sep, const char escape,
                      unsigned flags, qv_t(lstr) *res)
{
    SB_1k(sb);
    pstream_t tmp;
    ctype_desc_t sep_esc;
    ctype_desc_t esc;

    if (escape) {
        ctype_desc_build2(&esc, &escape, 1);
        ctype_desc_combine(&sep_esc, sep, &esc);
    } else {
        sep_esc = *sep;
    }

    if (flags & PS_SPLIT_SKIP_EMPTY) {
        ps_skip_span(&ps, sep);
    }

    while (!ps_done(&ps)) {
        tmp = ps_get_cspan(&ps, &sep_esc);
        sb_add(&sb, tmp.s, tmp.s_end - tmp.s);

        if (ctype_desc_contains(sep, *ps.s)) {
            if (mp) {
                qv_append(res, mp_lstr_dup(mp, LSTR_SB_V(&sb)));
            } else {
                lstr_t dst = LSTR_NULL;

                lstr_transfer_sb(&dst, &sb, false);
                qv_append(res, dst);
            }
            sb_reset(&sb);

            if (flags & PS_SPLIT_SKIP_EMPTY) {
                ps_skip_span(&ps, sep);
            } else {
                ps_skip(&ps, 1);
            }
        } else
        if (escape && escape == *ps.s) {
            /* Check if next character is a separator
             * An escape character is removed if followed by an other escape
             * character or a separator.
             */
            if (ps_has(&ps, 2)
            &&  ctype_desc_contains(&sep_esc, ps.s[1]))
            {
                /* add next character, skip escape character */
                sb_add(&sb, ps.s + 1, 1);
                ps_skip(&ps, 2);
            } else {
                /* add escape character only */
                sb_add(&sb, ps.s, 1);
                ps_skip(&ps, 1);
            }
        }
    }

    if (!(flags & PS_SPLIT_SKIP_EMPTY)  /* last character is a separator */
    ||  (sb.len != 0))                  /* last character is an escape */
    {
        qv_append(res, t_lstr_dup(LSTR_SB_V(&sb)));
    }
}
