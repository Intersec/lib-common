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

#include <math.h>
#include <lib-common/iop.h>
#include "helpers.in.c"

static void xpack_struct(sb_t *, const iop_struct_t *, const void *, unsigned);
static void xpack_class(sb_t *, const iop_struct_t *, const void *, unsigned);
static void xpack_union(sb_t *, const iop_struct_t *, const void *, unsigned);

static void
xpack_value(sb_t *sb, const iop_struct_t *desc, const iop_field_t *f,
            const void *v, unsigned flags)
{
    static lstr_t const types[] = {
        [IOP_T_I8]     = LSTR_IMMED(" xsi:type=\"xsd:byte\">"),
        [IOP_T_U8]     = LSTR_IMMED(" xsi:type=\"xsd:unsignedByte\">"),
        [IOP_T_I16]    = LSTR_IMMED(" xsi:type=\"xsd:short\">"),
        [IOP_T_U16]    = LSTR_IMMED(" xsi:type=\"xsd:unsignedShort\">"),
        [IOP_T_I32]    = LSTR_IMMED(" xsi:type=\"xsd:int\">"),
        [IOP_T_ENUM]   = LSTR_IMMED(" xsi:type=\"xsd:int\">"),
        [IOP_T_U32]    = LSTR_IMMED(" xsi:type=\"xsd:unsignedInt\">"),
        [IOP_T_I64]    = LSTR_IMMED(" xsi:type=\"xsd:long\">"),
        [IOP_T_U64]    = LSTR_IMMED(" xsi:type=\"xsd:unsignedLong\">"),
        [IOP_T_BOOL]   = LSTR_IMMED(" xsi:type=\"xsd:boolean\">"),
        [IOP_T_DOUBLE] = LSTR_IMMED(" xsi:type=\"xsd:double\">"),
        [IOP_T_STRING] = LSTR_IMMED(" xsi:type=\"xsd:string\">"),
        [IOP_T_DATA]   = LSTR_IMMED(" xsi:type=\"xsd:base64Binary\">"),
        [IOP_T_XML]    = LSTR_IMMED(">"),
        [IOP_T_UNION]  = LSTR_IMMED(">"),
        [IOP_T_STRUCT] = LSTR_IMMED(">"),
        [IOP_T_VOID]   = LSTR_IMMED(" xsi:nil=\"true\">"),
    };
    const lstr_t *s;
    const iop_field_attrs_t *attrs;
    bool is_class = iop_field_is_class(f);
    bool is_ref   = iop_field_is_reference(f);

    sb_grow(sb, 64 + f->name.len * 2);
    sb_addc(sb, '<');
    sb_add(sb, f->name.s, f->name.len);

    if ((is_class || is_ref) && f->repeat != IOP_R_OPTIONAL) {
        /* Non-optional reference fields have to be dereferenced
         * (dereferencing of optional fields was already done by
         * caller).
         */
        v = *(void **)v;
    }
    if (is_class) {
        const iop_struct_t *real_desc = *(const iop_struct_t **)v;

        /* If this assert fails, you are exporting private classes through
         * a public interface... this is BAD!
         */
        assert (!real_desc->class_attrs->is_private
                || !(flags & IOP_XPACK_SKIP_PRIVATE));

        /* The "n" namespace is used here because it's the one used in
         * ichttp_serialize_soap. */
        sb_addf(sb, " xsi:type=\"n:%*pM\">",
                LSTR_FMT_ARG(real_desc->fullname));
    } else
    if (((flags & IOP_XPACK_VERBOSE)
        && !((flags & IOP_XPACK_LITERAL_ENUMS) && f->type == IOP_T_ENUM))
    ||  f->type == IOP_T_VOID)
    {
        sb_add(sb, types[f->type].s, types[f->type].len);
    } else {
        sb_addc(sb, '>');
    }

    switch (f->type) {
        double d;

      case IOP_T_I8:     sb_addf(sb, "%i",      *(  int8_t *)v); break;
      case IOP_T_U8:     sb_addf(sb, "%u",      *( uint8_t *)v); break;
      case IOP_T_I16:    sb_addf(sb, "%i",      *( int16_t *)v); break;
      case IOP_T_U16:    sb_addf(sb, "%u",      *(uint16_t *)v); break;
      case IOP_T_I32:    sb_addf(sb, "%i",      *( int32_t *)v); break;
      case IOP_T_ENUM:
        if (!(flags & IOP_XPACK_LITERAL_ENUMS)) {
            sb_addf(sb, "%i",      *( int32_t *)v);
        } else {
            lstr_t str = iop_enum_to_str_desc(f->u1.en_desc, *(int32_t *)v);
            if (!str.s) {
                sb_addf(sb, "%i",      *( int32_t *)v);
            } else {
                sb_add_lstr(sb, str);
            }
        }
        break;
      case IOP_T_U32:    sb_addf(sb, "%u",      *(uint32_t *)v); break;
      case IOP_T_I64:    sb_addf(sb, "%jd",     *( int64_t *)v); break;
      case IOP_T_U64:    sb_addf(sb, "%ju",     *(uint64_t *)v); break;
      case IOP_T_DOUBLE:
        d = *(double *)v;
        if (isinf(d)) {
            sb_adds(sb, d < 0 ? "-INF" : "INF");
        } else {
            sb_addf(sb, "%.17e", d);
        }
        break;
      case IOP_T_BOOL:
        if (*(bool *)v) {
            sb_addc(sb, '1');
        } else {
            sb_addc(sb, '0');
        }
        break;
      case IOP_T_STRING:
        s = v;
        if (s->len) {
            attrs = iop_field_get_attrs(desc, f);
            if (attrs && TST_BIT(&attrs->flags, IOP_FIELD_CDATA)) {
                sb_adds(sb, "<![CDATA[");
                sb_add(sb, s->s, s->len);
                sb_adds(sb, "]]>");
            } else {
                sb_add_xmlescape(sb, s->s, s->len);
            }
        }
        break;
      case IOP_T_DATA:
        s = v;
        if (s->len)
            sb_addlstr_b64(sb, *s, -1);
        break;
      case IOP_T_XML:
        s = v;
        sb_add(sb, s->s, s->len);
        break;
      case IOP_T_UNION:
        xpack_union(sb, f->u1.st_desc, v, flags);
        break;
      case IOP_T_VOID:
        break;
      case IOP_T_STRUCT:
        if (is_class) {
            xpack_class(sb, f->u1.st_desc, v, flags);
        } else {
            xpack_struct(sb, f->u1.st_desc, v, flags);
        }
        break;
    }
    sb_adds(sb, "</");
    sb_add(sb, f->name.s, f->name.len);
    sb_addc(sb, '>');
}

static void
xpack_struct(sb_t *sb, const iop_struct_t *desc, const void *v,
             unsigned flags)
{
    for (int i = 0; i < desc->fields_len; i++) {
        const iop_field_t *f = desc->fields + i;
        const void *ptr = (char *)v + f->data_offs;
        int len = 1;

        if (flags & IOP_XPACK_SKIP_PRIVATE) {
            const iop_field_attrs_t *attrs = iop_field_get_attrs(desc, f);
            if (attrs && TST_BIT(&attrs->flags, IOP_FIELD_PRIVATE))
                continue;
        }

        if (f->repeat == IOP_R_OPTIONAL) {
            if (!iop_opt_field_isset(f->type, ptr)) {
                continue;
            }
            if ((1 << f->type) & IOP_STRUCTS_OK)
                ptr = *(void **)ptr;
        }
        if (f->repeat == IOP_R_REPEATED) {
            const lstr_t *data = ptr;
            len = data->len;
            ptr = data->data;
        }

        while (len-- > 0) {
            if (!(f->type == IOP_T_VOID && f->repeat == IOP_R_REQUIRED)) {
                xpack_value(sb, desc, f, ptr, flags);
            }
            ptr = (const char *)ptr + f->size;
        }
    }
}

static void xpack_class(sb_t *sb, const iop_struct_t *desc, const void *v,
                        unsigned flags)
{
    qv_t(iop_struct) parents;
    const iop_struct_t *real_desc = *(const iop_struct_t **)v;

    e_assert(panic, !real_desc->class_attrs->is_abstract,
             "packing of abstract class '%*pM' is forbidden",
             LSTR_FMT_ARG(real_desc->fullname));

    /* We want to write the fields in the order "master -> children", and
     * not "children -> master", so first build a qvector of the parents.
     */
    qv_inita(&parents, 8);
    do {
        qv_append(&parents, real_desc);
    } while ((real_desc = real_desc->class_attrs->parent));

    /* Write fields of different levels */
    for (int pos = parents.len; pos-- > 0; ) {
        xpack_struct(sb, parents.tab[pos], v, flags);
    }
    qv_wipe(&parents);
}

static void
xpack_union(sb_t *sb, const iop_struct_t *desc, const void *v,
            unsigned flags)
{
    const iop_field_t *f = get_union_field(desc, v);

    xpack_value(sb, desc, f, (char *)v + f->data_offs, flags);
}

void iop_xpack_flags(sb_t *sb, const iop_struct_t *desc, const void *v,
                     unsigned flags)
{
    if (desc->is_union) {
        xpack_union(sb, desc, v, flags);
    } else
    if (iop_struct_is_class(desc)) {
        xpack_class(sb, desc, v, flags);
    } else {
        xpack_struct(sb, desc, v, flags);
    }
}

void iop_xpack(sb_t *sb, const iop_struct_t *desc, const void *v,
               bool verbose, bool with_enums)
{
    unsigned flags = 0;
    if (verbose)
        flags |= IOP_XPACK_VERBOSE;
    if (with_enums)
        flags |= IOP_XPACK_LITERAL_ENUMS;
    iop_xpack_flags(sb, desc, v, flags);
}
