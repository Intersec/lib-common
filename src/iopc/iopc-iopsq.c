/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include "iopc-iopsq.h"
#include "iopc-priv.h"

#include <lib-common/iop/priv.h>

/* {{{ IOP-described package to iopc_pkg_t */
/* {{{ Helpers */

int iop_type_to_iop(iop_type_t type, iop__type__t *out)
{
    switch (type) {
    case IOP_T_I8:
    case IOP_T_I16:
    case IOP_T_I32:
    case IOP_T_I64:
    case IOP_T_U8:
    case IOP_T_U16:
    case IOP_T_U32:
    case IOP_T_U64:
        *out = IOP_UNION_VA(
            iop__type, i, .is_signed = iop_int_type_is_signed(type),
            .size = iopsq_int_type_to_int_size(type)
        );
        break;

    case IOP_T_BOOL:
        *out = IOP_UNION_VOID(iop__type, b);
        break;

    case IOP_T_DOUBLE:
        *out = IOP_UNION_VOID(iop__type, d);
        break;

    case IOP_T_STRING:
        *out = IOP_UNION(iop__type, s, STRING_TYPE_STRING);
        break;

    case IOP_T_DATA:
        *out = IOP_UNION(iop__type, s, STRING_TYPE_BYTES);
        break;

    case IOP_T_XML:
        *out = IOP_UNION(iop__type, s, STRING_TYPE_XML);
        break;

    case IOP_T_VOID:
        *out = IOP_UNION_VOID(iop__type, v);
        break;

    case IOP_T_ENUM:
    case IOP_T_UNION:
    case IOP_T_STRUCT:
        return -1;
    }

    return 0;
}

/* }}} */
/* {{{ iopsq_type_table_t */

qm_kvec_t(
    iopsq_type_id, iop_full_type_t, uint64_t, qhash_iop_full_type_hash,
    qhash_iop_full_type_equal
);

qvector_t(iop_full_type, iop_full_type_t);

struct iopsq_type_table_t {
    qm_t(iopsq_type_id) map;
    qv_t(iop_full_type) types;
};

static iopsq_type_table_t *__iopsq_type_table_init(iopsq_type_table_t *table)
{
    p_clear(table, 1);
    qm_init(iopsq_type_id, &table->map);

    return table;
}

DO_NEW(iopsq_type_table_t, __iopsq_type_table);

static void __iopsq_type_table_wipe(iopsq_type_table_t *table)
{
    qm_wipe(iopsq_type_id, &table->map);
    qv_wipe(&table->types);
}

DO_DELETE(iopsq_type_table_t, __iopsq_type_table);

/** Fill an iopsq type from an iop_full_type_t. */
static int iopsq_fill_type(
    const iop_env_ctx_t *iop_env_ctx, const iop_full_type_t *ftype,
    iop__type__t *type
)
{
    lstr_t typename;

    if (iop_type_to_iop(ftype->type, type) >= 0) {
        return 0;
    }

    if (ftype->type == IOP_T_ENUM) {
        typename = ftype->en->fullname;

        if (iop_env_ctx_get_enum(iop_env_ctx, typename) == ftype->en) {
            /* The enumeration is registered in the environment so it can
             * be referred to with a type name. */
            *type = IOP_UNION(iop__type, type_name, typename);
            return 0;
        }
    } else {
        assert(!iop_type_is_scalar(ftype->type));
        typename = ftype->st->fullname;

        if (iop_env_ctx_get_struct(iop_env_ctx, typename) == ftype->st) {
            /* The struct/union/class is registered in the environment so
             * it can be referred to with a type name. */
            *type = IOP_UNION(iop__type, type_name, typename);
            return 0;
        }
    }

    return -1;
}

void iopsq_type_table_fill_type(
    iopsq_type_table_t *table, const iop_env_ctx_t *iop_env_ctx,
    const iop_full_type_t *ftype, iop__type__t *type
)
{
    if (iopsq_fill_type(iop_env_ctx, ftype, type) < 0) {
        uint32_t pos;

        /* The type is unknown and has probably been built by the user.
         * Register it in the table. */
        pos = qm_put(iopsq_type_id, &table->map, ftype, table->types.len, 0);
        if (pos & QHASH_COLLISION) {
            pos &= ~QHASH_COLLISION;
        } else {
            qv_append(&table->types, *ftype);
        }

        *type = IOP_UNION(iop__type, type_id, table->map.values[pos]);
    }
}

static const iop_full_type_t *
iopsq_type_table_get_type(const iopsq_type_table_t *table, uint32_t type_id)
{
    THROW_NULL_IF(type_id >= (uint32_t)table->types.len);
    return &table->types.tab[type_id];
}

/* }}} */
/* {{{ Attributes */

/* Field constraints and generic attributes end up in iopc_attr_t, the very
 * representation the parser builds from the `@`-attributes of an .iop source.
 * The IOP² loader reuses the parser's built-in attribute descriptors
 * (iopc_get_attr_desc) so that the resolver can validate them: applicability
 * is described by desc->types/flags. */

/* Shared descriptor for a string-valued attribute argument: only used by
 * iopc_arg_wipe/dup to know the argument owns an lstr. */
static iopc_arg_desc_t iopsq_str_arg_g = {
    .name = LSTR_IMMED("v"),
    .type = ITOK_STRING,
};

static iopc_attr_t *iopsq_attr_new(iopc_attr_id_t id)
{
    iopc_attr_t *attr = iopc_attr_new();

    attr->desc = iopc_get_attr_desc(id);
    return attr;
}

static void iopsq_attr_add_str_arg(iopc_attr_t *attr, lstr_t s)
{
    iopc_arg_t arg;

    iopc_arg_init(&arg);
    arg.desc = &iopsq_str_arg_g;
    arg.type = ITOK_STRING;
    arg.v.s = lstr_dup(s);
    qv_append(&attr->args, arg);
}

static void iopsq_attr_add_int_arg(iopc_attr_t *attr, int64_t i)
{
    iopc_arg_t arg;

    iopc_arg_init(&arg);
    arg.type = ITOK_INTEGER;
    arg.v.i64 = i;
    qv_append(&attr->args, arg);
}

/* Append an argument built from an IOP² Value, tagging it with the token type
 * expected downstream (drives generic-attribute type selection). */
static void
iopsq_attr_add_value_arg(iopc_attr_t *attr, const iop__value__t *val)
{
    iopc_arg_t arg;

    iopc_arg_init(&arg);
    IOP_UNION_SWITCH(val) {
        IOP_UNION_CASE(iop__value, val, i, i)
        {
            arg.type = ITOK_INTEGER;
            arg.v.i64 = i;
        }
        IOP_UNION_CASE(iop__value, val, u, u)
        {
            arg.type = ITOK_INTEGER;
            arg.v.i64 = (int64_t)u;
        }
        IOP_UNION_CASE(iop__value, val, d, d)
        {
            arg.type = ITOK_DOUBLE;
            arg.v.d = d;
        }
        IOP_UNION_CASE(iop__value, val, s, s)
        {
            arg.type = ITOK_STRING;
            arg.desc = &iopsq_str_arg_g;
            arg.v.s = lstr_dup(s);
        }
        IOP_UNION_CASE(iop__value, val, b, b)
        {
            arg.type = ITOK_BOOL;
            arg.v.i64 = b;
        }
    }
    qv_append(&attr->args, arg);
}

/* }}} */
/* {{{ IOP struct/union */

static iop_type_t iop_type_from_iop(const iop__type__t *iop_type)
{
    IOP_UNION_SWITCH(iop_type) {
        IOP_UNION_CASE_P(iop__type, iop_type, i, i)
        {
            switch (i->size) {
#define CASE(_sz)                                                            \
    case INT_SIZE_S##_sz:                                                    \
        return i->is_signed ? IOP_T_I##_sz : IOP_T_U##_sz

                CASE(8);
                CASE(16);
                CASE(32);
                CASE(64);

#undef CASE
            }
        }
        IOP_UNION_CASE_V(iop__type, iop_type, b)
        {
            return IOP_T_BOOL;
        }
        IOP_UNION_CASE_V(iop__type, iop_type, d)
        {
            return IOP_T_DOUBLE;
        }
        IOP_UNION_CASE(iop__type, iop_type, s, s)
        {
            switch (s) {
            case STRING_TYPE_STRING:
                return IOP_T_STRING;

            case STRING_TYPE_BYTES:
                return IOP_T_DATA;

            case STRING_TYPE_XML:
                return IOP_T_XML;
            }
        }
        IOP_UNION_CASE_V(iop__type, iop_type, v)
        {
            return IOP_T_VOID;
        }
        IOP_UNION_CASE_V(iop__type, iop_type, type_name)
        {
            /* This case should be handled at higher level. */
            e_panic("should not happen");
        }
        IOP_UNION_CASE_V(iop__type, iop_type, array)
        {
            /* This case should be handled at higher level. */
            e_panic("should not happen");
        }
        IOP_UNION_CASE_V(iop__type, iop_type, type_id)
        {
            /* This case should be handled at higher level. */
            e_panic("should not happen");
        }
    }

    return 0;
}

static int iopc_field_set_typename(
    iopc_field_t *nonnull f, const iop_env_ctx_t *nonnull iop_env_ctx,
    lstr_t typename, sb_t *nonnull err
)
{
    f->kind = iop_get_type(typename);

    if (f->kind == IOP_T_STRUCT) {
        if (lstr_contains(typename, LSTR("."))) {
            /* TODO Could parse and check that the type name looks like a
             * proper type name. */
            const iop_struct_t *st;
            const iop_enum_t *en;

            if ((st = iop_env_ctx_get_struct(iop_env_ctx, typename))) {
                f->external_st = st;
                f->kind = st->is_union ? IOP_T_UNION : IOP_T_STRUCT;
                f->has_external_type = true;
            } else if ((en = iop_env_ctx_get_enum(iop_env_ctx, typename))) {
                f->external_en = en;
                f->kind = IOP_T_ENUM;
                f->has_external_type = true;
            }
        } else {
            if (iopc_check_type_name(typename, err) < 0) {
                sb_prepends(err, "invalid type name: ");
                return -1;
            }
        }
    }
    f->type_name = p_dupz(typename.s, typename.len);
    return 0;
}

static int iopc_field_set_type(
    iopc_field_t *nonnull f, const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__type__t *nonnull type,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    struct iopsq__type__t *const *array_type;

    if ((array_type = IOP_UNION_GET(iop__type, type, array))) {
        type = *array_type;

        if (IOP_UNION_IS(iop__type, type, array)) {
            sb_setf(err, "multi-dimension arrays are not supported");
            return -1;
        }

        f->repeat = IOP_R_REPEATED;
    }

    IOP_UNION_SWITCH(type) {
        IOP_UNION_CASE(iop__type, type, type_name, typename)
        {
            if (iopc_field_set_typename(f, iop_env_ctx, typename, err) < 0) {
                return -1;
            }
        }

        IOP_UNION_CASE(iop__type, type, type_id, type_id)
        {
            const iop_full_type_t *ftype;

            if (!type_table) {
                sb_sets(err, "got type ID but no type table");
                return -1;
            }

            ftype = iopsq_type_table_get_type(type_table, type_id);
            f->kind = ftype->type;
            if (ftype->type == IOP_T_ENUM) {
                f->external_en = ftype->en;
                f->has_external_type = true;
            } else if (!iop_type_is_scalar(ftype->type)) {
                f->external_st = ftype->st;
                f->has_external_type = true;
            }
        }

        IOP_UNION_DEFAULT()
        {
            f->kind = iop_type_from_iop(type);
        }
    }

    RETHROW(iopc_check_field_type(f, err));

    return 0;
}

static void
iopc_field_set_defval(iopc_field_t *f, const iop__value__t *defval)
{
    IOP_UNION_SWITCH(defval) {
        IOP_UNION_CASE(iop__value, defval, i, i)
        {
            f->defval.u64 = i;
            f->defval_is_signed = (i < 0);
            f->defval_type = IOPC_DEFVAL_INTEGER;
        }
        IOP_UNION_CASE(iop__value, defval, u, u)
        {
            f->defval.u64 = u;
            f->defval_type = IOPC_DEFVAL_INTEGER;
        }
        IOP_UNION_CASE(iop__value, defval, d, d)
        {
            f->defval.d = d;
            f->defval_type = IOPC_DEFVAL_DOUBLE;
        }
        IOP_UNION_CASE(iop__value, defval, s, s)
        {
            f->defval.ptr = p_dupz(s.s, s.len);
            f->defval_type = IOPC_DEFVAL_STRING;
        }
        IOP_UNION_CASE(iop__value, defval, b, b)
        {
            f->defval.u64 = b;
            f->defval_type = IOPC_DEFVAL_INTEGER;
        }
    }
}

static void iopc_field_set_opt_info(
    iopc_field_t *nonnull f, const iop__opt_info__t *nullable opt_info
)
{
    if (!opt_info) {
        f->repeat = IOP_R_REQUIRED;
    } else if (opt_info->def_val) {
        f->repeat = IOP_R_DEFVAL;
        iopc_field_set_defval(f, opt_info->def_val);
    } else {
        f->repeat = IOP_R_OPTIONAL;
    }
}

/* Build the iopc_attr_t for a single built-in constraint, mirroring the
 * mapping the parser does from `@`-attributes to attribute descriptors. */
static void
iopc_field_load_constraint(iopc_field_t *f, const iop__constraint__t *c)
{
    iopc_attr_t *attr = NULL;

    IOP_UNION_SWITCH(c) {
        IOP_UNION_CASE_P(iop__constraint, c, min, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_MIN);
            iopsq_attr_add_value_arg(attr, v);
        }
        IOP_UNION_CASE_P(iop__constraint, c, max, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_MAX);
            iopsq_attr_add_value_arg(attr, v);
        }
        IOP_UNION_CASE(iop__constraint, c, min_occurs, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_MIN_OCCURS);
            iopsq_attr_add_int_arg(attr, v);
        }
        IOP_UNION_CASE(iop__constraint, c, max_occurs, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_MAX_OCCURS);
            iopsq_attr_add_int_arg(attr, v);
        }
        IOP_UNION_CASE(iop__constraint, c, min_length, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_MIN_LENGTH);
            iopsq_attr_add_int_arg(attr, v);
        }
        IOP_UNION_CASE(iop__constraint, c, max_length, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_MAX_LENGTH);
            iopsq_attr_add_int_arg(attr, v);
        }
        IOP_UNION_CASE(iop__constraint, c, length, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_LENGTH);
            iopsq_attr_add_int_arg(attr, v);
        }
        IOP_UNION_CASE(iop__constraint, c, pattern, v)
        {
            attr = iopsq_attr_new(IOPC_ATTR_PATTERN);
            iopsq_attr_add_str_arg(attr, v);
        }
        IOP_UNION_CASE_V(iop__constraint, c, non_empty)
        {
            attr = iopsq_attr_new(IOPC_ATTR_NON_EMPTY);
        }
        IOP_UNION_CASE_V(iop__constraint, c, non_zero)
        {
            attr = iopsq_attr_new(IOPC_ATTR_NON_ZERO);
        }
        IOP_UNION_CASE_V(iop__constraint, c, cdata)
        {
            attr = iopsq_attr_new(IOPC_ATTR_CDATA);
        }
        IOP_UNION_CASE_V(iop__constraint, c, is_private)
        {
            attr = iopsq_attr_new(IOPC_ATTR_PRIVATE);
        }
        IOP_UNION_CASE_V(iop__constraint, c, is_deprecated)
        {
            attr = iopsq_attr_new(IOPC_ATTR_DEPRECATED);
        }
    }

    /* 'attr' stays NULL only for an unknown/future constraint tag that
     * matched no case above; skip it rather than append an uninitialised
     * pointer (also silences -Werror=maybe-uninitialized on older GCC). */
    if (attr) {
        qv_append(&f->attrs, attr);
    }
}

static void iopsq_attrs_add_generic(
    qv_t(iopc_attr) *attrs, const iop__generic_attr__t *ga
)
{
    iopc_attr_t *attr = iopsq_attr_new(IOPC_ATTR_GENERIC);

    attr->real_name = lstr_fmt("%pL:%pL", &ga->ns, &ga->id);
    iopsq_attr_add_value_arg(attr, &ga->value);
    qv_append(attrs, attr);
}

static iopc_field_t *iopc_field_load(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__field__t *nonnull field_desc, const qv_t(iopc_field) *fields,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_field_t *f = NULL;

    if (iopc_check_field_name(field_desc->name, err) < 0) {
        goto error;
    }

    f = iopc_field_new();
    f->name = p_dupz(field_desc->name.s, field_desc->name.len);
    f->field_pos = fields->len;

    if (OPT_ISSET(field_desc->tag)) {
        f->tag = OPT_VAL(field_desc->tag);
    } else {
        f->tag = fields->len ? (*tab_last(fields))->tag + 1 : 1;
    }
    if (iopc_check_tag_value(f->tag, err) < 0) {
        goto error;
    }
    tab_for_each_entry(other_field, fields) {
        if (strequal(other_field->name, f->name)) {
            sb_sets(err, "name already used by another field");
            goto error;
        }
        if (other_field->tag == f->tag) {
            sb_setf(
                err, "tag `%d' is already used by field `%s'", f->tag,
                other_field->name
            );
            goto error;
        }
    }

    if (iopc_field_set_type(
            f, iop_env_ctx, &field_desc->type, type_table, err
        ) < 0)
    {
        goto error;
    }
    if (f->repeat == IOP_R_REPEATED) {
        if (field_desc->optional) {
            sb_setf(
                err, "repeated field cannot be optional "
                     "or have a default value"
            );
            goto error;
        }
    } else {
        iopc_field_set_opt_info(f, field_desc->optional);
    }

    if (field_desc->is_reference) {
        if (f->repeat == IOP_R_OPTIONAL) {
            sb_setf(err, "optional references are not supported");
            goto error;
        }
        if (f->repeat == IOP_R_REPEATED) {
            sb_setf(err, "arrays of references are not supported");
            goto error;
        }
        f->is_ref = true;
    }

    tab_for_each_ptr(constraint, &field_desc->constraints) {
        iopc_field_load_constraint(f, constraint);
    }
    tab_for_each_ptr(gen_attr, &field_desc->generic_attrs) {
        iopsq_attrs_add_generic(&f->attrs, gen_attr);
    }

    return f;

error:
    sb_prependf(err, "field `%pL': ", &field_desc->name);
    iopc_field_delete(&f);
    return NULL;
}

static void iop_structure_get_type_and_fields(
    const iop__structure__t *desc, iopc_struct_type_t *type,
    iop__field__array_t *fields
)
{
    IOP_OBJ_EXACT_SWITCH(desc)
    {
        IOP_OBJ_CASE_CONST(iop__struct, desc, st)
        {
            *fields = st->fields;
            *type = STRUCT_TYPE_STRUCT;
        }

        IOP_OBJ_CASE_CONST(iop__union, desc, un)
        {
            *fields = un->fields;
            *type = STRUCT_TYPE_UNION;
        }

        IOP_OBJ_CASE_CONST(iop__class, desc, cls)
        {
            *fields = cls->fields;
            *type = STRUCT_TYPE_CLASS;
        }

        IOP_OBJ_EXACT_DEFAULT()
        {
            assert(false);
        }
    }
}

/* Load a class static field: a scalar field carrying its constant value as a
 * default value (as the parser does through parse_field_defval). */
static iopc_field_t *iopc_static_field_load(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__static_field__t *nonnull sf,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_field_t *f = NULL;

    if (iopc_check_field_name(sf->name, err) < 0) {
        goto error;
    }

    f = iopc_field_new();
    f->name = p_dupz(sf->name.s, sf->name.len);
    /* Set 'is_static' before the type so iopc_check_field_type() applies the
     * static-field restrictions (no optional/reference/repeated/void). */
    f->is_static = true;

    if (iopc_field_set_type(f, iop_env_ctx, &sf->type, type_table, err) < 0) {
        goto error;
    }

    f->repeat = IOP_R_DEFVAL;
    iopc_field_set_defval(f, &sf->value);

    return f;

error:
    sb_prependf(err, "static field `%pL': ", &sf->name);
    iopc_field_delete(&f);
    return NULL;
}

/* Load the class-specific parts of a Class package element: parent (by name,
 * resolved later by the typer), class id, abstract/private flags, and static
 * fields. */
static int iopc_class_load(
    const iop_env_ctx_t *nonnull iop_env_ctx, iopc_struct_t *nonnull st,
    const iop__class__t *nonnull cls,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    st->class_id = cls->class_id;
    st->is_abstract = cls->is_abstract;

    if (cls->is_private) {
        qv_append(&st->attrs, iopsq_attr_new(IOPC_ATTR_PRIVATE));
    }

    if (cls->parent.s) {
        iopc_extends_t *xt = iopc_extends_new();

        /* Only the name is set: the typer resolves it against the current
         * package (auto-filling pkg/path/st). */
        xt->name = p_dupz(cls->parent.s, cls->parent.len);
        qv_append(&st->extends, xt);
    }

    qv_grow(&st->static_fields, cls->static_fields.len);
    tab_for_each_ptr(sf, &cls->static_fields) {
        iopc_field_t *f = RETHROW_PN(
            iopc_static_field_load(iop_env_ctx, sf, type_table, err)
        );

        f->field_pos = st->static_fields.len;
        qv_append(&st->static_fields, f);
    }

    return 0;
}

static iopc_struct_t *iopc_struct_load(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__structure__t *nonnull st_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_struct_t *st;
    iop__field__array_t fields = IOP_ARRAY_EMPTY;

    st = iopc_struct_new();
    st->name = p_dupz(st_desc->name.s, st_desc->name.len);
    /* Make the struct findable by name during resolution: parent lookups
     * (unlike same-package field types) require the symbol to be visible. */
    st->is_visible = true;
    iop_structure_get_type_and_fields(st_desc, &st->type, &fields);

    qv_grow(&st->fields, fields.len);
    tab_for_each_ptr(field_desc, &fields) {
        iopc_field_t *f;

        if (!(f = iopc_field_load(
                  iop_env_ctx, field_desc, &st->fields, type_table, err
              )))
        {
            iopc_struct_delete(&st);
            return NULL;
        }

        qv_append(&st->fields, f);
    }

    tab_for_each_ptr(gen_attr, &st_desc->generic_attrs) {
        iopsq_attrs_add_generic(&st->attrs, gen_attr);
    }

    IOP_OBJ_EXACT_SWITCH(st_desc)
    {
        IOP_OBJ_CASE_CONST(iop__class, st_desc, cls)
        {
            if (iopc_class_load(iop_env_ctx, st, cls, type_table, err) < 0) {
                iopc_struct_delete(&st);
                return NULL;
            }
        }

        IOP_OBJ_EXACT_DEFAULT()
        {
        }
    }

    return st;
}

/* }}} */
/* {{{ IOP interface */

/* Load one RPC arg/res/exn part into \p fun_st as an anonymous structure
 * named "<rpc><suffix>" (the same convention as the parser). A NULL
 * description leaves the part void. */
static int iopc_rpc_struct_load(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__rpc_struct__t *nullable desc, const char *nonnull rpc_name,
    const char *nonnull suffix, iopc_fun_struct_t *nonnull fun_st,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_struct_t *st;

    if (!desc) {
        /* Void part. */
        return 0;
    }

    st = iopc_struct_new();
    st->name = asprintf("%s%s", rpc_name, suffix);
    st->type = STRUCT_TYPE_STRUCT;
    /* Hand the struct to \p fun_st now: on error the caller deletes the
     * function, and the struct with it. */
    fun_st->is_anonymous = true;
    fun_st->anonymous_struct = st;

    qv_grow(&st->fields, desc->fields.len);
    tab_for_each_ptr(field_desc, &desc->fields) {
        iopc_field_t *f;

        if (!(f = iopc_field_load(
                  iop_env_ctx, field_desc, &st->fields, type_table, err
              )))
        {
            return -1;
        }

        qv_append(&st->fields, f);
    }

    return 0;
}

/* Fill \p fun from the RPC description. The caller owns \p fun and adds the
 * error context. */
static int iopc_fun_fill(
    const iop_env_ctx_t *nonnull iop_env_ctx, const iop__rpc__t *nonnull rpc,
    const iopc_iface_t *nonnull iface, iopc_fun_t *nonnull fun,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    RETHROW(iopc_check_field_name(rpc->name, err));

    fun->name = p_dupz(rpc->name.s, rpc->name.len);
    fun->pos = iface->funs.len;
    fun->fun_is_async = rpc->is_async;

    if (OPT_ISSET(rpc->tag)) {
        fun->tag = OPT_VAL(rpc->tag);
    } else {
        fun->tag = iface->funs.len ? (*tab_last(&iface->funs))->tag + 1 : 1;
    }
    RETHROW(iopc_check_tag_value(fun->tag, err));

    tab_for_each_entry(other, &iface->funs) {
        if (strequal(other->name, fun->name)) {
            sb_sets(err, "name already used by another RPC");
            return -1;
        }
        if (other->tag == fun->tag) {
            sb_setf(
                err, "tag `%d' is already used by RPC `%s'", fun->tag,
                other->name
            );
            return -1;
        }
    }

    if (rpc->is_async && rpc->res) {
        sb_sets(err, "an asynchronous RPC cannot have a result");
        return -1;
    }
    if (rpc->is_async && rpc->exn) {
        sb_sets(err, "an asynchronous RPC cannot throw");
        return -1;
    }

    RETHROW(iopc_rpc_struct_load(
        iop_env_ctx, rpc->arg, fun->name, "Args", &fun->arg, type_table, err
    ));
    RETHROW(iopc_rpc_struct_load(
        iop_env_ctx, rpc->res, fun->name, "Res", &fun->res, type_table, err
    ));
    RETHROW(iopc_rpc_struct_load(
        iop_env_ctx, rpc->exn, fun->name, "Exn", &fun->exn, type_table, err
    ));

    return 0;
}

/* Load an RPC (function) into \p iface. Its arguments, result and exceptions
 * are anonymous structures; an absent part is void. */
static int iopc_fun_load(
    const iop_env_ctx_t *nonnull iop_env_ctx, const iop__rpc__t *nonnull rpc,
    iopc_iface_t *nonnull iface,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_fun_t *fun = iopc_fun_new();

    if (iopc_fun_fill(iop_env_ctx, rpc, iface, fun, type_table, err) < 0) {
        iopc_fun_delete(&fun);
        sb_prependf(err, "RPC `%pL': ", &rpc->name);
        return -1;
    }

    qv_append(&iface->funs, fun);
    return 0;
}

static iopc_iface_t *iopc_iface_load(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__iface__t *nonnull iface_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_iface_t *iface;

    iface = iopc_iface_new();
    iface->name = p_dupz(iface_desc->name.s, iface_desc->name.len);
    iface->type = IFACE_TYPE_IFACE;
    /* Make the interface findable by name when resolving module refs. */
    iface->is_visible = true;

    qv_grow(&iface->funs, iface_desc->rpcs.len);
    tab_for_each_ptr(rpc, &iface_desc->rpcs) {
        if (iopc_fun_load(iop_env_ctx, rpc, iface, type_table, err) < 0) {
            iopc_iface_delete(&iface);
            return NULL;
        }
    }

    return iface;
}

/* }}} */
/* {{{ IOP module */

/* Fill \p f from a module interface reference. The caller owns \p f and adds
 * the error context. */
static int iopc_module_iface_fill(
    const iop__module_iface__t *nonnull miface,
    const iopc_struct_t *nonnull mod, iopc_field_t *nonnull f,
    sb_t *nonnull err
)
{
    RETHROW(iopc_check_field_name(miface->name, err));

    f->name = p_dupz(miface->name.s, miface->name.len);
    f->type_name = p_dupz(miface->iface.s, miface->iface.len);

    if (OPT_ISSET(miface->tag)) {
        /* User-defined tag. */
        f->tag = OPT_VAL(miface->tag);
    } else if (mod->fields.len == 0) {
        f->tag = 1;
    } else {
        /* Incremental tagging: use last tag + 1. */
        f->tag = (*tab_last(&mod->fields))->tag + 1;
    }
    RETHROW(iopc_check_tag_value(f->tag, err));

    tab_for_each_entry(other, &mod->fields) {
        if (strequal(other->name, f->name)) {
            sb_setf(err, "interface alias `%s' is already used", f->name);
            return -1;
        }
        if (other->tag == f->tag) {
            sb_setf(
                err, "tag `%d' is already used by interface `%s'", f->tag,
                other->name
            );
            return -1;
        }
    }

    return 0;
}

/* Load a module as an 'iopc_struct_t' whose fields are interface references
 * (the same representation as the parser). Each field carries the alias name,
 * the referenced interface name (resolved later by the typer against the
 * current package) and a tag. */
static iopc_struct_t *
iopc_module_load(const iop__module__t *nonnull mod_desc, sb_t *nonnull err)
{
    iopc_struct_t *mod;

    mod = iopc_struct_new();
    mod->name = p_dupz(mod_desc->name.s, mod_desc->name.len);
    mod->is_visible = true;

    qv_grow(&mod->fields, mod_desc->ifaces.len);
    tab_for_each_ptr(miface, &mod_desc->ifaces) {
        iopc_field_t *f = iopc_field_new();

        if (iopc_module_iface_fill(miface, mod, f, err) < 0) {
            iopc_field_delete(&f);
            sb_prependf(err, "module interface `%pL': ", &miface->name);
            goto error;
        }

        qv_append(&mod->fields, f);
    }

    return mod;

error:
    iopc_struct_delete(&mod);
    return NULL;
}

/* }}} */
/* {{{ IOP enum */

/* Attach an @alias attribute holding the value aliases, mirroring the parser
 * which stores one alias name per attribute argument. */
static void iopc_enum_field_load_aliases(
    iopc_enum_field_t *field, const lstr_t *aliases, int nb_aliases
)
{
    iopc_attr_t *attr;

    if (!nb_aliases) {
        return;
    }
    attr = iopsq_attr_new(IOPC_ATTR_ALIAS);
    for (int i = 0; i < nb_aliases; i++) {
        iopsq_attr_add_str_arg(attr, aliases[i]);
    }
    qv_append(&field->attrs, attr);
}

static iopc_enum_t *iopc_enum_load(const iop__enum__t *en_desc, sb_t *err)
{
    t_scope;
    iopc_enum_t *en;
    int next_val = 0;
    qh_t(lstr) keys;
    qh_t(u32) values;

    t_qh_init(lstr, &keys, en_desc->values.len);
    t_qh_init(u32, &values, en_desc->values.len);
    tab_for_each_ptr(enum_val, &en_desc->values) {
        int32_t val = OPT_DEFVAL(enum_val->val, next_val);

        if (qh_add(u32, &values, val) < 0) {
            sb_setf(
                err, "key `%pL': the value `%d' is already used",
                &enum_val->name, val
            );
            return NULL;
        }
        if (qh_add(lstr, &keys, &enum_val->name) < 0) {
            sb_setf(err, "the key `%pL' is duplicated", &enum_val->name);
            return NULL;
        }
        tab_for_each_ptr(alias, &enum_val->aliases) {
            if (qh_add(lstr, &keys, alias) < 0) {
                sb_setf(err, "the alias `%pL' is duplicated", alias);
                return NULL;
            }
        }
        next_val = val + 1;
    }

    en = iopc_enum_new();
    en->name = p_dupz(en_desc->name.s, en_desc->name.len);
    if (en_desc->strict) {
        qv_append(&en->attrs, iopsq_attr_new(IOPC_ATTR_STRICT));
    }
    next_val = 0;
    tab_for_each_ptr(enum_val, &en_desc->values) {
        iopc_enum_field_t *field = iopc_enum_field_new();

        field->name = p_dupz(enum_val->name.s, enum_val->name.len);
        field->value = OPT_DEFVAL(enum_val->val, next_val);
        next_val = field->value + 1;

        iopc_enum_field_load_aliases(
            field, enum_val->aliases.tab, enum_val->aliases.len
        );
        qv_append(&en->values, field);
    }

    return en;
}

/* }}} */
/* {{{ IOP typedef */

static iopc_field_t *iopc_typedef_load(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__typedef__t *nonnull td_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iopc_field_t *tdef = iopc_field_new();

    tdef->is_visible = true;
    tdef->name = p_dupz(td_desc->name.s, td_desc->name.len);
    if (iopc_field_set_type(
            tdef, iop_env_ctx, &td_desc->type, type_table, err
        ) < 0)
    {
        iopc_field_delete(&tdef);
        return NULL;
    }

    return tdef;
}

/* }}} */
/* {{{ IOP package */

static const char *pkg_elem_type_to_str(const iop__package_elem__t *elem)
{
    IOP_OBJ_EXACT_SWITCH(elem)
    {
    case IOP_CLASS_ID(iop__struct):
        return "struct";

    case IOP_CLASS_ID(iop__union):
        return "union";

    case IOP_CLASS_ID(iop__class):
        return "class";

    case IOP_CLASS_ID(iop__enum):
        return "enum";

    case IOP_CLASS_ID(iop__typedef):
        return "typedef";

    case IOP_CLASS_ID(iop__iface):
        return "interface";

    case IOP_CLASS_ID(iop__module):
        return "module";
    }

    assert(false);
    return "<unknown>";
}

static iopc_pkg_t *iopc_pkg_load_from_iop(
    const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__package__t *nonnull pkg_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    t_scope;
    iopc_pkg_t *pkg = iopc_pkg_new();
    qh_t(lstr) things;

    pkg->file = p_strdup("<none>");
    pkg->name = iopc_path_parse(pkg_desc->name, err);
    if (!pkg->name) {
        sb_prepends(err, "invalid name: ");
        goto error;
    }
    /* XXX Nothing to do for attribute "base" (related to package file path).
     */

    t_qh_init(lstr, &things, pkg_desc->elems.len);
    tab_for_each_entry(elem, &pkg_desc->elems) {
        if (iopc_check_type_name(elem->name, err) < 0) {
            sb_prependf(err, "invalid %s name: ", pkg_elem_type_to_str(elem));
            goto error;
        }

        if (qh_add(lstr, &things, &elem->name) < 0) {
            sb_setf(err, "already got a thing named `%pL'", &elem->name);
            goto error;
        }

        IOP_OBJ_SWITCH(iop__package_elem, elem)
        {
            IOP_OBJ_CASE(iop__structure, elem, st_desc)
            {
                iopc_struct_t *st;

                if (!(st = iopc_struct_load(
                          iop_env_ctx, st_desc, type_table, err
                      )))
                {
                    sb_prependf(err, "cannot load `%pL': ", &elem->name);
                    goto error;
                }

                qv_append(&pkg->structs, st);
            }

            IOP_OBJ_CASE(iop__enum, elem, en_desc)
            {
                iopc_enum_t *en;

                if (!(en = iopc_enum_load(en_desc, err))) {
                    sb_prependf(err, "cannot load enum `%pL': ", &elem->name);
                    goto error;
                }

                qv_append(&pkg->enums, en);
            }

            IOP_OBJ_CASE(iop__typedef, elem, td_desc)
            {
                iopc_field_t *tdef;

                if (!(tdef = iopc_typedef_load(
                          iop_env_ctx, td_desc, type_table, err
                      )))
                {
                    sb_prependf(
                        err, "cannot load typedef `%pL': ", &elem->name
                    );
                    goto error;
                }

                qv_append(&pkg->typedefs, tdef);
            }

            IOP_OBJ_CASE(iop__iface, elem, iface_desc)
            {
                iopc_iface_t *iface;

                if (!(iface = iopc_iface_load(
                          iop_env_ctx, iface_desc, type_table, err
                      )))
                {
                    sb_prependf(
                        err, "cannot load interface `%pL': ", &elem->name
                    );
                    goto error;
                }

                qv_append(&pkg->ifaces, iface);
            }

            IOP_OBJ_CASE(iop__module, elem, mod_desc)
            {
                iopc_struct_t *mod;

                if (!(mod = iopc_module_load(mod_desc, err))) {
                    sb_prependf(
                        err, "cannot load module `%pL': ", &elem->name
                    );
                    goto error;
                }

                qv_append(&pkg->modules, mod);
            }

            /* Classes are 'iop__structure__t' subclasses and are handled by
             * the 'iop__structure' case above. */
            /* TODO SNMP stuff */

            IOP_OBJ_DEFAULT(iop__package_elem)
            {
                sb_setf(
                    err,
                    "package elements of type `%pL' are not supported yet",
                    &elem->__vptr->fullname
                );
                goto error;
            }
        }
    }

    return pkg;

error:
    iopc_pkg_delete(&pkg);
    return NULL;
}

/* }}} */
/* }}} */
/* {{{ iop_pkg_t to IOP-described package */

/* Reverse of the forward converter above: extract an 'iopsq' description from
 * a compiled 'iop_pkg_t'. Types are referenced by fullname (resolved against
 * the environment when the package is rebuilt). Everything is allocated on
 * the provided memory pool. */

/* Short (unqualified) name of a type, i.e. the part after the last dot of its
 * fullname: `pkg.Foo' gives `Foo', `Foo' gives `Foo'. IOP² package elements
 * carry the short name; the package name supplies the prefix. */
static lstr_t iopsq_short_name(lstr_t fullname)
{
    pstream_t ps = ps_initlstr(&fullname);
    pstream_t pkg_path;

    if (ps_get_ps_lastchr_and_skip(&ps, '.', &pkg_path) < 0) {
        return fullname;
    }
    return LSTR_PS_V(&ps);
}

/* Fill an IOP² Type from a compiled field. Structs/unions/classes and enums
 * are referenced by fullname; a repeated field wraps its element type in a
 * Type.array. */
static void mp_iopsq_type_from_field(
    mem_pool_t *mp, const iop_field_t *f, iop__type__t *out
)
{
    iop__type__t base;

    switch (f->type) {
    case IOP_T_STRUCT:
    case IOP_T_UNION:
        base = IOP_UNION(
            iop__type, type_name, mp_lstr_dup(mp, f->u1.st_desc->fullname)
        );
        break;

    case IOP_T_ENUM:
        base = IOP_UNION(
            iop__type, type_name, mp_lstr_dup(mp, f->u1.en_desc->fullname)
        );
        break;

    default:
        /* Scalar (including void): cannot fail for a non-aggregate type. */
        (void)iop_type_to_iop(f->type, &base);
        break;
    }

    if (f->repeat == IOP_R_REPEATED) {
        iop__type__t *elem = mp_new(mp, iop__type__t, 1);

        *elem = base;
        *out = IOP_UNION(iop__type, array, elem);
    } else {
        *out = base;
    }
}

/* Fill an IOP² Value from a compiled field's default value, mirroring the
 * encoding read back by iopc_field_set_defval. */
static void mp_iopsq_value_from_defval(
    mem_pool_t *mp, const iop_field_t *f, iop__value__t *out
)
{
    switch (f->type) {
    case IOP_T_I8:
    case IOP_T_I16:
    case IOP_T_I32:
    case IOP_T_I64:
        *out = IOP_UNION(iop__value, i, (int64_t)f->u1.defval_u64);
        break;

    case IOP_T_U8:
    case IOP_T_U16:
    case IOP_T_U32:
    case IOP_T_U64:
        *out = IOP_UNION(iop__value, u, f->u1.defval_u64);
        break;

    case IOP_T_BOOL:
        *out = IOP_UNION(iop__value, b, f->u1.defval_u64 != 0);
        break;

    case IOP_T_DOUBLE:
        *out = IOP_UNION(iop__value, d, f->u1.defval_d);
        break;

    case IOP_T_ENUM:
        *out = IOP_UNION(iop__value, i, (int64_t)f->u0.defval_enum);
        break;

    case IOP_T_STRING:
    case IOP_T_DATA:
    case IOP_T_XML:
        *out = IOP_UNION(
            iop__value, s,
            mp_lstr_dups(mp, f->u1.defval_data, f->u0.defval_len)
        );
        break;

    /* A struct, union or void field carries no default value. */
    case IOP_T_STRUCT:
    case IOP_T_UNION:
    case IOP_T_VOID:
        e_panic("unexpected default value type");
    }
}

/* Fill an IOP² Field from a compiled field. The tag is always set explicitly
 * to preserve the exact numbering; optional/default state is carried in the
 * OptInfo, and references through the boolean flag. */
static void mp_iopsq_field_from_iop(
    mem_pool_t *mp, const iop_field_t *f, iop__field__t *out
)
{
    out->name = mp_lstr_dup(mp, f->name);
    mp_iopsq_type_from_field(mp, f, &out->type);
    OPT_SET(out->tag, f->tag);
    out->is_reference = iop_field_is_reference(f);

    if (f->repeat == IOP_R_OPTIONAL || f->repeat == IOP_R_DEFVAL) {
        iop__opt_info__t *opt = mp_iop_new(mp, iop__opt_info);

        if (f->repeat == IOP_R_DEFVAL) {
            iop__value__t *val = mp_iop_new(mp, iop__value);

            mp_iopsq_value_from_defval(mp, f, val);
            opt->def_val = val;
        }
        out->optional = opt;
    }
}

/* Build the Field array shared by structs and unions. */
static iop__field__array_t
mp_iopsq_fields_from_iop(mem_pool_t *mp, const iop_struct_t *st)
{
    iop__field__array_t fields;

    fields = MP_IOP_ARRAY_NEW(mp, iop__field, st->fields_len);
    for (int i = 0; i < st->fields_len; i++) {
        mp_iopsq_field_from_iop(mp, &st->fields[i], &fields.tab[i]);
    }
    return fields;
}

iop__package_elem__t *mp_iopsq_elem_from_iop_struct(
    mem_pool_t *nonnull mp, const iop_struct_t *nonnull st, sb_t *nonnull err
)
{
    iop__structure__t *structure;
    iop__field__array_t fields;

    if (iop_struct_is_class(st)) {
        sb_setf(err, "classes are not supported yet");
        return NULL;
    }

    fields = mp_iopsq_fields_from_iop(mp, st);

    if (st->is_union) {
        iop__union__t *un = mp_iop_new(mp, iop__union);

        un->fields = fields;
        structure = &un->super;
    } else {
        iop__struct__t *desc = mp_iop_new(mp, iop__struct);

        desc->fields = fields;
        structure = &desc->super;
    }

    structure->name = mp_lstr_dup(mp, iopsq_short_name(st->fullname));

    return &structure->super;
}

/* }}} */
/* {{{ IOP² API */

iop_pkg_t *mp_iopsq_build_pkg(
    mem_pool_t *nonnull mp, const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__package__t *nonnull pkg_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iop_pkg_t *pkg = NULL;
    iopc_pkg_t *iopc_pkg;

    if (!expect(mp->mem_pool & MEM_BY_FRAME)) {
        sb_sets(err, "incompatible memory pool type");
        return NULL;
    }

    if (!(iopc_pkg =
              iopc_pkg_load_from_iop(iop_env_ctx, pkg_desc, type_table, err)))
    {
        sb_prependf(err, "invalid package `%pL': ", &pkg_desc->name);
        return NULL;
    }

    log_start_buffering_filter(false, LOG_ERR);
    if (iopc_resolve(iopc_pkg) < 0 || iopc_resolve_second_pass(iopc_pkg) < 0)
    {
        const qv_t(log_buffer) *logs = log_stop_buffering();

        sb_sets(err, "failed to resolve the package");
        tab_for_each_ptr(log, logs) {
            sb_addf(err, ": %pL", &log->msg);
        }
        goto end;
    }
    IGNORE(log_stop_buffering());

    pkg = mp_iopc_pkg_to_desc(mp, iopc_pkg, err);
    if (!pkg) {
        sb_prependf(
            err,
            "failed to generate package `%s': ", iopc_path_dot(iopc_pkg->name)
        );
        goto end;
    }

end:
    iopc_pkg_delete(&iopc_pkg);
    return pkg;
}

iop_pkg_t *mp_iopsq_build_mono_element_pkg(
    mem_pool_t *nonnull mp, const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__package_elem__t *nonnull elem,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iop__package__t pkg_desc;
    iop__package_elem__t *_elem = unconst_cast(iop__package_elem__t, elem);

    iop_init(iop__package, &pkg_desc);
    pkg_desc.name = LSTR("user_package");
    pkg_desc.elems = IOP_TYPED_ARRAY(iop__package_elem, &_elem, 1);

    return mp_iopsq_build_pkg(mp, iop_env_ctx, &pkg_desc, type_table, err);
}

const iop_struct_t *mp_iopsq_build_struct(
    mem_pool_t *nonnull mp, const iop_env_ctx_t *nonnull iop_env_ctx,
    const iop__structure__t *nonnull iop_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    iop_pkg_t *pkg;

    pkg = RETHROW_P(mp_iopsq_build_mono_element_pkg(
        mp, iop_env_ctx, &iop_desc->super, type_table, err
    ));

    return pkg->structs[0];
}

__must_check__ int iopsq_iop_struct_build(
    iopsq_iop_struct_t *nonnull st, const iop_env_ctx_t *nonnull iop_env_ctx,
    const iopsq__structure__t *nonnull iop_desc,
    const iopsq_type_table_t *nullable type_table, sb_t *nonnull err
)
{
    assert(!st->mp && !st->st);

    st->mp = mem_ring_new("iop_struct_mp_build", PAGE_SIZE);
    mem_ring_newframe(st->mp);
    st->st =
        mp_iopsq_build_struct(st->mp, iop_env_ctx, iop_desc, type_table, err);
    st->release_cookie = mem_ring_seal(st->mp);

    if (unlikely(!st->st)) {
        iopsq_iop_struct_wipe(st);
        return -1;
    }
    return 0;
}

void iopsq_iop_struct_wipe(iopsq_iop_struct_t *nonnull st)
{
    mem_ring_release(st->release_cookie);
    mem_ring_delete(&st->mp);
    p_clear(st, 1);
}

/* }}} */
