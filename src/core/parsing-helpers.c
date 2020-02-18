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

int
parse_backslash(pstream_t *ps, sb_t *buf, int *line, int *col)
{
#define SKIP(i)  \
    ({ int tmp = (i); *col += tmp; __ps_skip(ps, tmp); })

    if (!ps_has(ps, 2)) {
        return -1;
    }

    switch (ps->b[1]) {
        int a, b;

      case 'a': case 'b': case 'e': case 't': case 'n': case 'v':
      case 'f': case 'r':
        sb_add_unquoted(buf, ps->p, 2);
        SKIP(2);
        return 0;
      case '\\': case '"': case '\'': case '/':
        sb_addc(buf, ps->b[1]);
        SKIP(2);
        return 0;
      case '0' ... '2':
        if (ps_has(ps, 4)
        &&  ps->b[2] >= '0' && ps->b[2] <= '7'
        &&  ps->b[3] >= '0' && ps->b[3] <= '7')
        {
            sb_addc(buf, ((ps->b[1] - '0') << 6)
                       | ((ps->b[2] - '0') << 3)
                       |  (ps->b[3] - '0'));
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
        if (ps_has(ps, 4) && (a = hexdecode(ps->s + 2)) >= 0) {
            sb_addc(buf, a);
            SKIP(4);
            return 0;
        }
        break;
      case 'u':
        if (ps_has(ps, 6) && (a = hexdecode(ps->s + 2)) >= 0
            && (b = hexdecode(ps->s + 4)) >= 0)
        {
            sb_adduc(buf, (a << 8) | b);
            SKIP(6);
            return 0;
        }
        break;
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
