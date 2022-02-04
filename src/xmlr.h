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

#ifndef IS_LIB_INET_XMLR_H
#define IS_LIB_INET_XMLR_H

#include <lib-common/core.h>
#include <libxml/xmlreader.h>

typedef xmlTextReaderPtr xml_reader_t;

extern __thread xml_reader_t xmlr_g;

enum xmlr_error {
    XMLR_OK      = 0,
    XMLR_ERROR   = -1,
    XMLR_NOCHILD = -2,
};

#define XMLR_CHECK(expr, on_err) \
    ({ int __xres = (expr); if (unlikely(__xres < 0)) { on_err; } __xres; })

/* \brief Initiates the parser with the content in the buffer.
 *
 * This function wants to position itself (pre-load) the root node of the
 * document.
 */
int xmlr_setup(xml_reader_t *xrp, const void *buf, int len);
void xmlr_close(xml_reader_t *xrp);

static inline void xmlr_delete(xml_reader_t *xrp)
{
    if (*xrp) {
        xmlFreeTextReader(*xrp);
        *xrp = NULL;
    }
}

__cold
int  xmlr_fail(xml_reader_t xr, const char *fmt, ...) __attr_printf__(2, 3);
void xmlr_clear_err(void);
__cold
const char *xmlr_get_err(void);

/* \brief Goes to the next node element (closing or opening)
 */
int xmlr_next_node(xml_reader_t xr);

/* \brief Get the shorthand reference to the namespace associated with
 * the node
 */
lstr_t xmlr_node_get_xmlns(xml_reader_t xr);

/* \brief Get namespace associated with the node
 */
lstr_t xmlr_node_get_xmlns_uri(xml_reader_t xr);

/* \brief Goes to the first child of the current node
 */
int xmlr_next_child(xml_reader_t xr);

/* \brief Skip the current node fully, and goes to the next one.
 */
int xmlr_next_sibling(xml_reader_t xr);

/* \brief Skip to the next node after the end of the one at the current level.
 */
int xmlr_next_uncle(xml_reader_t xr);

static inline int xmlr_node_is_empty(xml_reader_t xr)
{
    return RETHROW(xmlTextReaderIsEmptyElement(xr));
}

static inline int xmlr_node_is_closing(xml_reader_t xr)
{
    return RETHROW(xmlTextReaderNodeType(xr)) == XML_READER_TYPE_END_ELEMENT;
}

int xmlr_node_get_local_name(xml_reader_t xr, lstr_t *out);

static inline int xmlr_node_is(xml_reader_t xr, const char *s, size_t len)
{
    lstr_t name = LSTR_NULL_V;

    return !RETHROW(xmlr_node_is_closing(xr))
        && RETHROW(xmlr_node_get_local_name(xr, &name)) >= 0
        && lstr_equal(name, LSTR_INIT_V(s, len));
}
static inline int xmlr_node_is_s(xml_reader_t xr, const char *s)
{
    return xmlr_node_is(xr, s, strlen(s));
}

static inline int xmlr_node_want(xml_reader_t xr, const char *s, size_t len)
{
    if (!xmlr_node_is(xr, s, len))
        return xmlr_fail(xr, "missing <%s> tag", s);
    return 0;
}
static inline int xmlr_node_want_s(xml_reader_t xr, const char *s)
{
    return xmlr_node_want(xr, s, strlen(s));
}

#define XMLR_ENTER_MISSING_OK    (1U << 0)
#define XMLR_ENTER_EMPTY_OK      (2U << 0)
#define XMLR_ENTER_ALL_OK        (0xffffffff)
int xmlr_node_enter(xml_reader_t xr, const char *s, size_t len, int flags);
static inline int xmlr_node_enter_s(xml_reader_t xr, const char *s, int flags)
{
    return xmlr_node_enter(xr, s, strlen(s), flags);
}

static inline int xmlr_node_try_open_s(xml_reader_t xr, const char *s)
{
    return xmlr_node_enter_s(xr, s, XMLR_ENTER_ALL_OK);
}

static inline int xmlr_node_open_s(xml_reader_t xr, const char *s)
{
    return xmlr_node_enter_s(xr, s, 0);
}

int xmlr_node_close(xml_reader_t xr);

static inline int xmlr_node_close_n(xml_reader_t xr, size_t n)
{
    while (n-- > 0)
        RETHROW(xmlr_node_close(xr));
    return 0;
}

static inline int xmlr_node_skip_s(xml_reader_t xr, const char *s, int flags)
{
    if (RETHROW(xmlr_node_enter_s(xr, s, flags)))
        return xmlr_next_uncle(xr);
    return 0;
}

int xmlr_node_skip_until(xml_reader_t xr, const char *s, int len);
static inline int xmlr_node_skip_until_s(xml_reader_t xr, const char *s)
{
    return xmlr_node_skip_until(xr, s, strlen(s));
}


/* \brief Get the current node value, and go to the next node.
 *
 * This function fails if the node has childen.
 */
int xmlr_get_cstr_start(xml_reader_t xr, bool emptyok, lstr_t *out);
int xmlr_get_cstr_done(xml_reader_t xr);

int xmlr_get_strdup(xml_reader_t xr, bool emptyok, lstr_t *out);
int mp_xmlr_get_strdup(mem_pool_t *mp, xml_reader_t xr,
                       bool emptyok, lstr_t *out);
int t_xmlr_get_str(xml_reader_t xr, bool emptyok, lstr_t *out);

/** Get an integer value between minv and maxv.
 *
 * This function get the current node value and go to the next node. It
 * accepts values depending on the base parameter (see man strtol).
 */
int xmlr_get_int_range_base(xml_reader_t xr, int minv, int maxv, int base,
                            int *ip);

/** Get a signed integer value.
 *
 * This function get the current node value and go to the next node. It
 * accepts values depending on the base parameter (see man strtol).
 */
int xmlr_get_i64_base(xml_reader_t xr, int base, int64_t *i64p);

/** Get an unsigned integer value.
 *
 * This function get the current node value and go to the next node. It
 * accepts values depending on the base parameter (see man strtol).
 */
int xmlr_get_u64_base(xml_reader_t xr, int base, uint64_t *u64p);

/** Get an integer value between minv and maxv.
 *
 * This function get the current node value and go to the next node. It
 * accepts decimal values only.
 */
static inline int
xmlr_get_int_range(xml_reader_t xr, int minv, int maxv, int *ip)
{
    return xmlr_get_int_range_base(xr, minv, maxv, 10, ip);
}

/** Get a signed integer value.
 *
 * This function get the current node value and go to the next node. It
 * accepts decimal values only.
 */
static inline int
xmlr_get_i64(xml_reader_t xr, int64_t *i64p)
{
    return xmlr_get_i64_base(xr, 10, i64p);
}

/** Get an unsigned integer value.
 *
 * This function get the current node value and go to the next node. It
 * accepts decimal values only.
 */
static inline int
xmlr_get_u64(xml_reader_t xr, uint64_t *u64p)
{
    return xmlr_get_u64_base(xr, 10, u64p);
}

/** Get a boolean value.
 *
 * This function get the current node value and go to the next node. It
 * accepts the following values: 0, 1, true, false.
 */
int xmlr_get_bool(xml_reader_t xr, bool *bp);

/** Get a double value.
 *
 * This function get the current node value and go to the next node.
 */
int xmlr_get_dbl(xml_reader_t xr, double *dp);

/* XXX: out must be lstr_wipe()d */
int xmlr_get_inner_xml(xml_reader_t xr, lstr_t *out);
int mp_xmlr_get_inner_xml(mem_pool_t *mp, xml_reader_t xr, lstr_t *out);

/* attributes stuff */

#define xmlr_for_each_attr(attr, xr) \
    for (xmlAttrPtr attr = xmlTextReaderCurrentNode(xr)->properties; \
         attr; attr = attr->next)

xmlAttrPtr
xmlr_find_attr(xml_reader_t xr, const char *name, size_t len, bool needed);
static ALWAYS_INLINE xmlAttrPtr
xmlr_find_attr_s(xml_reader_t xr, const char *name, bool needed)
{
    return xmlr_find_attr(xr, name, strlen(name), needed);
}

/* \brief Get the current node attribute value.
 */
int t_xmlr_getattr_str(xml_reader_t xr, xmlAttrPtr attr,
                       bool nullok, lstr_t *out);

/** Get the current node attribute integer value between minv and maxv.
 *
 * This function accepts decimal values depending on the base parameter (see
 * man strtol).
 */
int xmlr_getattr_int_range_base(xml_reader_t xr, xmlAttrPtr attr,
                                int minv, int maxv, int base, int *ip);

/** Get the current node attribute signed integer value.
 *
 * This function accepts decimal values depending on the base parameter (see
 * man strtol).
 */
int xmlr_getattr_i64_base(xml_reader_t xr, xmlAttrPtr attr, int base,
                          int64_t *i64p);

/** Get the current node attribute unsigned integer value.
 *
 * This function accepts decimal values depending on the base parameter (see
 * man strtol).
 */
int xmlr_getattr_u64_base(xml_reader_t xr, xmlAttrPtr attr, int base,
                          uint64_t *u64p);

/** Get the current node attribute integer value between minv and maxv.
 *
 * This function accepts decimal values only.
 */
static inline int
xmlr_getattr_int_range(xml_reader_t xr, xmlAttrPtr attr, int minv, int maxv,
                       int *ip)
{
    return xmlr_getattr_int_range_base(xr, attr, minv, maxv, 10, ip);
}

/** Get the current node attribute signed integer value.
 *
 * This function accepts decimal values only.
 */
static inline int
xmlr_getattr_i64(xml_reader_t xr, xmlAttrPtr attr, int64_t *i64p)
{
    return xmlr_getattr_i64_base(xr, attr, 10, i64p);
}

/** Get the current node attribute unsigned integer value.
 *
 * This function accepts decimal values only.
 */
static inline int
xmlr_getattr_u64(xml_reader_t xr, xmlAttrPtr attr, uint64_t *u64p)
{
    return xmlr_getattr_u64_base(xr, attr, 10, u64p);
}

/** Get the current node attribute unsigned integer value.
 *
 * This function accepts the following values: 0, 1, true, false.
 */
int xmlr_getattr_bool(xml_reader_t xr, xmlAttrPtr attr, bool *bp);


/** Get the current node attribute double value. */
int xmlr_getattr_dbl(xml_reader_t xr, xmlAttrPtr attr, double *dp);

#endif
