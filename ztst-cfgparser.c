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

#include "conf.h"

struct parse_state {
    bool seen_section : 1;
    int arraylvl;
};

static int parse_hook(void *_ps, cfg_parse_evt evt,
                      const char *v, int vlen, void *ctx)
{
    struct parse_state *ps = _ps;

    switch (evt) {
      case CFG_PARSE_SECTION:
        if (ps->seen_section)
            putchar('\n');
        printf("[%s", v);
        ps->seen_section = true;
        return 0;

      case CFG_PARSE_SECTION_ID:
        if (v) {
            printf(" \"%s\"]\n", v);
        } else {
            printf("]\n");
        }
        return 0;

      case CFG_PARSE_KEY:
      case CFG_PARSE_KEY_ARRAY:
        printf("%s%s", v, evt == CFG_PARSE_KEY_ARRAY ? "[]" : "");
        return 0;

      case CFG_PARSE_SET:
        printf(" =");
        return 0;

      case CFG_PARSE_APPEND:
        ps->arraylvl++;
        printf(" += {");
        return 0;

      case CFG_PARSE_VALUE:
        printf(ps->arraylvl ? " %s," : " %s\n", v ?: "");
        return 0;

      case CFG_PARSE_EOF:
        return 0;

      case CFG_PARSE_ERROR:
        fprintf(stderr, "%s\n", v);
        return 0;

      case CFG_PARSE_ARRAY_OPEN:
        ps->arraylvl++;
        printf(" {");
        return 0;

      case CFG_PARSE_ARRAY_CLOSE:
        ps->arraylvl--;
        printf(ps->arraylvl ? " }, " : " }\n");
        return 0;
    }
    return -1;
}

int main(int argc, const char **argv)
{
    for (argc--, argv++; argc > 0; argc--, argv++) {
        struct parse_state ps = {
            .seen_section = false,
        };
        cfg_parse(*argv, &parse_hook, &ps, CFG_PARSE_OLD_NAMESPACES | CFG_PARSE_GROK_ARRAY);
    }
    return 0;
}
