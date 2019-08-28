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

#ifndef IS_LIB_COMMON_XMLPP_H
#define IS_LIB_COMMON_XMLPP_H

#include "container-qvector.h"

typedef struct xmlpp_t {
    bool can_do_attr : 1;
    bool was_a_tag   : 1;
    bool nospace     : 1;
    sb_t  *buf;
    qv_t(lstr) stack;
} xmlpp_t;

void xmlpp_open_banner(xmlpp_t *, sb_t *buf);
void xmlpp_open(xmlpp_t *, sb_t *buf);
void xmlpp_close(xmlpp_t *);

void xmlpp_opentag(xmlpp_t *, const char *tag);
void xmlpp_closetag(xmlpp_t *);

static inline xmlpp_t *
__xmlpp_open_tag_and_return(xmlpp_t *xmlpp, const char *tag)
{
    xmlpp_opentag(xmlpp, tag);

    return xmlpp;
}

#define _xmlpp_tag_scope(_tmpname, _xmlpp, _tag)                             \
    for (xmlpp_t *_tmpname = __xmlpp_open_tag_and_return((_xmlpp), (_tag));  \
         _tmpname; xmlpp_closetag(_tmpname), _tmpname = NULL)

#define xmlpp_tag_scope(_xmlpp, _tag)                                        \
    _xmlpp_tag_scope(PFX_LINE(__xmlpp_scope), (_xmlpp), (_tag))

void xmlpp_nl(xmlpp_t *);

void xmlpp_putattr(xmlpp_t *, const char *key, const char *val);
void xmlpp_putattrfmt(xmlpp_t *, const char *key,
                      const char *fmt, ...) __attr_printf__(3, 4);

void xmlpp_put_cdata(xmlpp_t *, const char *s, size_t len);
void xmlpp_put(xmlpp_t *, const void *data, int len);
static inline void xmlpp_puts(xmlpp_t *pp, const char *s) {
    xmlpp_put(pp, s, strlen(s));
}
static inline void xmlpp_put_lstr(xmlpp_t *pp, lstr_t s)
{
    xmlpp_put(pp, s.s, s.len);
}
void xmlpp_putf(xmlpp_t *, const char *fmt, ...) __attr_printf__(2, 3);


static inline void xmlpp_closentag(xmlpp_t *pp, int n) {
    while (n-- > 0)
        xmlpp_closetag(pp);
}

static inline void xmlpp_opensib(xmlpp_t *pp, const char *tag) {
    xmlpp_closetag(pp);
    xmlpp_opentag(pp, tag);
}

#endif /* IS_LIB_COMMON_XMLPP_H */
