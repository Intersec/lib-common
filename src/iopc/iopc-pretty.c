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

#include "iopc.h"

const char *t_pretty_token(iopc_tok_type_t token)
{
    if (isprint(token)) {
        return t_fmt("`%c`", token);
    }
    switch (token) {
      case ITOK_EOF:           return "end of file";
      case ITOK_IDENT:         return "identifier";
      case ITOK_LSHIFT:        return "`<<`";
      case ITOK_RSHIFT:        return "`>>`";
      case ITOK_EXP:           return "`**`";
      case ITOK_INTEGER:       return "integer";
      case ITOK_DOUBLE:        return "double";
      case ITOK_BOOL:          return "boolean";
      case ITOK_STRING:        return "string";
      case ITOK_COMMENT:       return "comment";
      case ITOK_DOX_COMMENT:   return "doxygen comment";
      case ITOK_ATTR:          return "attribute";
      case ITOK_GEN_ATTR_NAME:
        return "generic attribute name (namespaces:id)";
      default:                 return "unknown token";
    }
}

const char *pretty_path_dot(iopc_path_t *path)
{
    if (!path->pretty_dot) {
        sb_t b;
        sb_init(&b);

        for (int i = 0; i < path->bits.len; i++)
            sb_addf(&b, "%s.", path->bits.tab[i]);
        sb_shrink(&b, 1);
        path->pretty_dot = sb_detach(&b, NULL);
    }
    return path->pretty_dot;
}

const char *pretty_path(iopc_path_t *path)
{
    if (!path->pretty_slash) {
        sb_t b;
        sb_init(&b);

        for (int i = 0; i < path->bits.len; i++)
            sb_addf(&b, "%s/", path->bits.tab[i]);
        b.data[b.len - 1] = '.';
        sb_adds(&b, "iop");
        path->pretty_slash = sb_detach(&b, NULL);
    }
    return path->pretty_slash;
}
