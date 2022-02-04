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

/*
 * Intersec ASN.1 aligned PER encoder/decoder
 *
 * Based on previous ASN.1 library, it uses the same field registration
 * macros. However, some features of the previous ASN.1 library are not
 * supported.
 *
 * New features that were not included into previous library are:
 *     - Constraint support
 *     - Extension support
 *     - Explicit open type support
 *
 *
 *  - Supported native C types:
 *         - int8_t uint8_t int16_t uint16_t int32_t uint32_t int64_t
 *         - enum
 *         - bool
 *         - lstr_t
 *
 *  - Supported ASN.1 types
 *         - INTEGER
 *             - Unconstrained
 *             - Constrained - ex. : (1 .. 42)
 *             - Extended    - ex. : (1 .. 42, ...)
 *                                   (1 .. 42, ... , 0 .. 128)
 *         - BOOLEAN
 *         - OCTET STRING
 *             - Unconstrained
 *             - Constrained - ex. : (SIZE (3))
 *                                   (SIZE (0 .. 10))
 *             - Extended    - ex. : (SIZE (1 .. 4, ...))
 *             - Length >= 16384 NOT supported yet (fragmented form)
 *             - Constraint FROM not supported
 *         - BIT STRING
 *             - Idem
 *         - ENUMERATED
 *             - Full support, extension included
 *             - Needs optimization
 *         - SEQUENCE
 *             - Full support when no extension is included
 *             - Extensions of type "ComponentType" supported (declared
 *               with "...") are supported (not the ones of type
 *               "ExtensionAdditionGroup", declared with "[[ <extensions ]]")
 *         - CHOICE
 *             - Warning : field order not checked yet (fields must use
 *                         canonical order defined for tag values).
 *                         See [3] 8.6 (p.12).
 *         - SET
 *             - PER encoding of a SET is the same as SEQUENCE encoding
 *             - Make sure you respect canonical ordering when registering
 *               a SET (see [3] 8.6 - p.12).
 *         - OPEN TYPE
 *             - When encountered type is predictible before beginning
 *               decoding, user shall only set the OPEN TYPE flag when
 *               registering the field. If the type is not predicable, then
 *               user shall declare an octet string and then decode its
 *               content later.
 *
 * References :
 *
 *     [1] ITU-T X.691 (02/2002)
 *         data/dev/doc/asn1/per/X.691-0207.pdf
 *     [2] Olivier Dubuisson
 *         ASN.1 Communication entre systèmes hétérogènes
 *         Springer 1999
 *         French version  : Paper book on Seb's desk
 *         English version : data/dev/doc/asn1/ASN1dubuisson.pdf
 *     [3] ITU-T X.680 (07/2002)
 *         data/dev/doc/asn1/X-680-0207.pdf
 */

#ifndef IS_LIB_INET_ASN1_PER_H
#define IS_LIB_INET_ASN1_PER_H

#include <lib-common/asn1.h>
#include "asn1/per-macros.h"

#define ASN1_MAX_LEN  SIZE_MAX /* FIXME put real ASN1_MAX_LEN instead */

/* TODO optimize */
static inline int asn1_enum_find_val(const asn1_enum_info_t *nonnull e,
                                     int32_t val, bool *nonnull extended)
{
    struct {
        const qv_t(i32) *values;
        bool extended;
    } vals_tabs[] = {
        { &e->values, false },
        { &e->ext_values, true },
    };

    carray_for_each_ptr(v, vals_tabs) {
        tab_enumerate(pos, enum_val, v->values) {
            if (enum_val == val) {
                assert (e->extended || !v->extended);
                *extended = v->extended;

                return pos;
            }
        }
    }

    return -1;
}

static inline void asn1_enum_append(asn1_enum_info_t *e, int32_t val)
{
    qv_t(i32) *values;
    const char *kind;

    if (e->extended) {
        values = &e->ext_values;
        kind = "root";
    } else {
        values = &e->values;
        kind = "extended";
    }

    if (values->len) {
        int32_t last = *tab_last(values);

        if (val < last) {
            e_panic("enumeration %s value `%d` "
                    "should be registered before value `%d`", kind, val,
                    last);
        }

        if (val == last) {
            e_panic("duplicated enumeration %s value `%d`", kind, val);
        }
    }

    qv_append(values, val);
}

int aper_encode_desc(sb_t *sb, const void *st, const asn1_desc_t *desc);
int t_aper_decode_desc(pstream_t *ps, const asn1_desc_t *desc,
                       bool copy, void *st);

#define aper_encode(sb, pfx, st)  \
    ({                                                                       \
        if (!__builtin_types_compatible_p(typeof(st), pfx##_t *)             \
        &&  !__builtin_types_compatible_p(typeof(st), const pfx##_t *))      \
        {                                                                    \
            __error__("ASN.1 PER encoder: `"#st"' type "                     \
                      "is not <"#pfx"_t *>");                                \
        }                                                                    \
        aper_encode_desc(sb, st, ASN1_GET_DESC(pfx));                        \
    })

#define t_aper_decode(ps, pfx, copy, st)  \
    ({                                                                       \
        if (!__builtin_types_compatible_p(typeof(st), pfx##_t *)) {          \
            __error__("ASN.1 PER decoder: `"#st"' type "                     \
                      "is not <"#pfx"_t *>");                                \
        }                                                                    \
        t_aper_decode_desc(ps, ASN1_GET_DESC(pfx), copy, st);                \
    })

/*
 * The decode_log_level allows to specify the way we want to log (or not)
 * decoding errors:
 *   * < 0 means e_info
 *   * >= 0 means e_trace(level, ...)
 */
void aper_set_decode_log_level(int level);

#endif
