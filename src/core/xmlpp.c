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

#include <lib-common/xmlpp.h>

void xmlpp_open(xmlpp_t *pp, sb_t *buf)
{
    p_clear(pp, 1);
    pp->buf = buf;
    qv_init(&pp->stack);
}

void xmlpp_open_banner(xmlpp_t *pp, sb_t *buf)
{
    xmlpp_open(pp, buf);
    sb_adds(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
}

void xmlpp_close(xmlpp_t *pp)
{
    while (pp->stack.len) {
        xmlpp_closetag(pp);
    }
    if (!pp->nospace && pp->buf->data[pp->buf->len - 1] != '\n') {
        sb_addc(pp->buf, '\n');
    }
    qv_deep_wipe(&pp->stack, lstr_wipe);
}

void xmlpp_opentag(xmlpp_t *pp, const char *tag_)
{
    lstr_t tag = lstr_dups(tag_, strlen(tag_));

    if (pp->nospace) {
        sb_addf(pp->buf, "<%*pM>", tag.len, tag.s);
    } else {
        sb_addf(pp->buf, "%-*c<%*pM>", pp->stack.len * 2 + 1, '\n',
                tag.len, tag.s);
    }
    qv_append(&pp->stack, tag);
    pp->can_do_attr = true;
    pp->was_a_tag   = true;
}

void xmlpp_closetag(xmlpp_t *pp)
{
    lstr_t tag;

    if (!pp->stack.len) {
        return;
    }

    tag = *tab_last(&pp->stack);
    qv_shrink(&pp->stack, 1);
    if (pp->can_do_attr) {
        sb_shrink(pp->buf, 1);
        sb_adds(pp->buf, " />");
    } else {
        if (pp->was_a_tag && !pp->nospace) {
            sb_addc(pp->buf, '\n');
            sb_addnc(pp->buf, pp->stack.len * 2, ' ');
        }
        sb_addf(pp->buf, "</%*pM>", tag.len, tag.s);
    }
    pp->can_do_attr = false;
    pp->was_a_tag   = true;
    lstr_wipe(&tag);
}

void xmlpp_nl(xmlpp_t *pp)
{
    if (pp->nospace) {
        return;
    }
    if (pp->can_do_attr) {
        sb_shrink(pp->buf, 1);
        sb_addf(pp->buf, "%-*c>", 2 * pp->stack.len, '\n');
    } else {
        sb_addf(pp->buf, "%-*c", 2 * pp->stack.len, '\n');
    }
}

void xmlpp_putattr(xmlpp_t *pp, const char *key, const char *val)
{
    if (!pp->can_do_attr) {
        return;
    }

    sb_shrink(pp->buf, 1);
    sb_addf(pp->buf, " %s=\"", key);
    sb_adds_xmlescape(pp->buf, val);
    sb_adds(pp->buf, "\">");
}

void xmlpp_putattrfmt(xmlpp_t *pp, const char *key, const char *fmt, ...)
{
    va_list ap;
    SB_8k(tmp);

    if (!pp->can_do_attr) {
        return;
    }

    va_start(ap, fmt);
    sb_addvf(&tmp, fmt, ap);
    va_end(ap);

    sb_shrink(pp->buf, 1);
    sb_addf(pp->buf, " %s=\"", key);
    sb_add_xmlescape(pp->buf, tmp.data, tmp.len);
    sb_adds(pp->buf, "\">");
}

void xmlpp_put_cdata(xmlpp_t *pp, const char *s, size_t len)
{
    const char *p;

    pp->can_do_attr = false;
    pp->was_a_tag   = false;
    sb_adds(pp->buf, "<![CDATA[");
    while (unlikely((p = memmem(s, len, "]]>", 3)) != NULL)) {
        sb_add(pp->buf, s, p - s);
        sb_adds(pp->buf, "]]>]]<![CDATA[>");
        len -= (p - s) + 3;
        s    = p + 3;
    }
    sb_add(pp->buf, s, len);
    sb_adds(pp->buf, "]]>");
}

void xmlpp_put(xmlpp_t *pp, const void *data, int len)
{
    pp->can_do_attr = false;
    pp->was_a_tag   = false;
    sb_add_xmlescape(pp->buf, data, len);
}

void xmlpp_putf(xmlpp_t *pp, const char *fmt, ...)
{
    va_list ap;
    SB_8k(tmp);

    va_start(ap, fmt);
    sb_addvf(&tmp, fmt, ap);
    va_end(ap);
    xmlpp_put(pp, tmp.data, tmp.len);
}
