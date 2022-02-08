/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

void iopc_path_join(const iopc_path_t *path, const char *sep, sb_t *buf)
{
    tab_enumerate(i, bit, &path->bits) {
        sb_adds(buf, bit);

        if (i < path->bits.len - 1) {
            sb_adds(buf, sep);
        }
    }
}

static char *iopc_path_join_cached(iopc_path_t *path, const char *sep,
                                   const char *sfx, char **cache)
{
    if (!*cache) {
        SB_1k(buf);

        iopc_path_join(path, sep, &buf);
        sb_adds(&buf, sfx);
        *cache = p_dupz(buf.data, buf.len);
    }

    return *cache;
}

const char *iopc_path_dot(iopc_path_t *path)
{
    return iopc_path_join_cached(path, ".", "", &path->dot_path);
}

const char *iopc_path_slash(iopc_path_t *path)
{
    return iopc_path_join_cached(path, "/", ".iop", &path->slash_path);
}

const char *t_iopc_path_join(const iopc_path_t *path, const char *sep)
{
    SB_1k(buf);

    iopc_path_join(path, sep, &buf);

    return t_dupz(buf.data, buf.len);
}
