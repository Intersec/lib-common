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

#include <lib-common/parsing-helpers.h>

parse_str_res_t
parse_quoted_string(pstream_t *ps, sb_t *buf, int *line, int *col, int term)
{
    sb_reset(buf);

#define SKIP(i)  \
    ({ int tmp = (i); *col += tmp; __ps_skip(ps, tmp); })

    for (;;) {
        for (unsigned i = 0; i < ps_len(ps); i++) {
            if (ps->b[i] == '\n') {
                return PARSE_STR_ERR_UNCLOSED;
            } else
            if (ps->b[i] == '\\') {
                sb_add(buf, ps->p, i);
                SKIP(i);
                goto parse_bslash;
            } else
            if (ps->b[i] == term) {
                sb_add(buf, ps->p, i);
                SKIP(i + 1);
                return PARSE_STR_OK;
            }
        }
        return PARSE_STR_ERR_UNCLOSED;

      parse_bslash:
        if (!ps_has(ps, 2)) {
            return PARSE_STR_ERR_EXP_SMTH;
        }

        switch (ps->b[1]) {
            int a, b;

          case 'a': case 'b': case 'e': case 't': case 'n': case 'v':
          case 'f': case 'r':
            sb_add_unquoted(buf, ps->p, 2);
            SKIP(2);
            continue;
          case '\\': case '"': case '\'': case '/':
            sb_addc(buf, ps->b[1]);
            SKIP(2);
            continue;
          case '0' ... '2':
            if (ps_has(ps, 4)
                &&  ps->b[2] >= '0' && ps->b[2] <= '7'
                &&  ps->b[3] >= '0' && ps->b[3] <= '7')
            {
                sb_addc(buf, ((ps->b[1] - '0') << 6)
                           | ((ps->b[2] - '0') << 3)
                           |  (ps->b[3] - '0'));
                SKIP(4);
                continue;
            }
            if (ps->b[1] == '0') {
                sb_addc(buf, '\0');
                SKIP(2);
                continue;
            }
            break;
          case 'x':
            if (ps_has(ps, 4) && (a = hexdecode(ps->s + 2)) >= 0) {
                sb_addc(buf, a);
                SKIP(4);
                continue;
            }
            break;
          case 'u':
            if (ps_has(ps, 6) && (a = hexdecode(ps->s + 2)) >= 0
                && (b = hexdecode(ps->s + 4)) >= 0)
            {
                sb_adduc(buf, (a << 8) | b);
                SKIP(6);
                continue;
            }
            break;
          case '\n':
            sb_add(buf, ps->p, 2);
            SKIP(2);
            (*line)++;
            *col = 1;
            continue;
        }
        sb_add(buf, ps->p, 2);
        SKIP(2);
    }
}
