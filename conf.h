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

#ifndef IS_LIB_COMMON_CONF_H
#define IS_LIB_COMMON_CONF_H

/* {{{ cfg files are ini-like files with an extended format.

  - leading and trailing spaces aren't significant.
  - quoted strings can embed usual C escapes (\a \b \n ...), octal chars
    (\ooo) and hexadecimal ones (\x??) and unicode ones (\u????).

  Encoding should be utf-8.

----8<----

[simple]
key = value

[section "With a Name"]
key = 1234
other = "some string with embeded spaces"
; comment
# alternate comment form
foo = /some/value/without[spaces|semicolon|dash]

; available with GROK_ARRAY
foo[] = bar
bar   = (1, 2, 3)
baz   = ("asd", 324,
         "toto")
baz  += (foobar)

---->8----
}}} */

#include "property.h"

/****************************************************************************/
/* Low level API                                                            */
/****************************************************************************/

/* warning, some combination aren't compatible:
 *  * OLD_KEYS with GROK_ARRAY
 */
enum cfg_parse_opts {
    CFG_PARSE_OLD_NAMESPACES = 1 << 0,
    CFG_PARSE_OLD_KEYS       = 1 << 1,
    CFG_PARSE_GROK_ARRAY     = 1 << 2,
};

typedef enum cfg_parse_evt {
    CFG_PARSE_SECTION,        /* v isn't NULL and vlen is >= 1 */
    CFG_PARSE_SECTION_ID,     /* v may be NULL                 */
    CFG_PARSE_KEY,            /* v isn't NULL and vlen is >= 1 */
    CFG_PARSE_KEY_ARRAY,      /* v isn't NULL and vlen is >= 1 */

    CFG_PARSE_SET,            /* v is NULL                     */
    CFG_PARSE_APPEND,         /* v is NULL                     */
    CFG_PARSE_VALUE,          /* v may be NULL                 */
    CFG_PARSE_EOF,            /* v is NULL                     */
    CFG_PARSE_ARRAY_OPEN,     /* v is NULL                     */
    CFG_PARSE_ARRAY_CLOSE,    /* v is NULL                     */

    CFG_PARSE_ERROR,          /* v isn't NULL and vlen is >= 1 */
} cfg_parse_evt;

typedef int cfg_parse_hook(void *priv, cfg_parse_evt,
                           const char *, int len, void *ctx);

int cfg_parse(const char *file, cfg_parse_hook *, void *, int opts);
int cfg_parse_buf(const char *, ssize_t, cfg_parse_hook *, void *, int opts);

__attr_printf__(3, 4)
int cfg_parse_seterr(void *ctx, int offs, const char *fmt, ...)
    __leaf;


/****************************************************************************/
/* conf_t's                                                                 */
/****************************************************************************/

typedef struct conf_section_t {
    char *name;
    qv_t(props) vals;
} conf_section_t;
qvector_t(conf_section, conf_section_t *);
typedef qv_t(conf_section) conf_t;

conf_t *conf_load(const char *filename) __leaf;
int conf_merge_dir(conf_t *conf, const char *path) __leaf;
conf_t *conf_load_str(const char *s, int len) __leaf;
void conf_delete(conf_t **) __leaf;

int conf_save(const conf_t *conf, const char *filename) __leaf;

static inline const conf_section_t *conf_get_section(const conf_t *cfg, int i)
{
    return (i < 0 || i >= cfg->len) ? NULL : cfg->tab[i];
}
const conf_section_t *conf_get_section_by_name(const conf_t *, const char *)
    __leaf;
const char *conf_get_raw(const conf_t *, const char *section, const char *v)
    __leaf;
const char *conf_section_get_raw(const conf_section_t *, const char *v)
    __leaf;

static inline const char *
conf_get(const conf_t *conf, const char *section,
         const char *var, const char *defval)
{
    const char *res = conf_get_raw(conf, section, var);
    return res ? res : defval;
}

static inline const char *
conf_section_get(const conf_section_t *section,
                 const char *var, const char *defval)
{
    const char *res = conf_section_get_raw(section, var);
    return res ? res : defval;
}


const char *conf_put(conf_t *conf, const char *section,
                     const char *var, const char *value) __leaf;

int conf_get_int(const conf_t *conf, const char *section,
                 const char *var, int defval) __leaf;
int conf_get_verbosity(const conf_t *conf, const char *section,
                       const char *var, int defval) __leaf;
int conf_get_bool(const conf_t *conf, const char *section,
                  const char *var, int defval) __leaf;
int conf_section_get_int(const conf_section_t *section,
                         const char *var, int defval) __leaf;
int conf_section_get_bool(const conf_section_t *section,
                          const char *var, int defval) __leaf;

/* Lookup next section beginning with prefix.
 * Store section name remaining characters in suffix if not NULL */
int conf_next_section_idx(const conf_t *conf, const char *prefix,
                          int prev_idx, const char **suffixp) __leaf;

#endif /* IS_LIB_COMMON_CONF_H */
