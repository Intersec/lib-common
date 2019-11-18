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

#include "iopc-iopsq.h"
#include "iopc-priv.h"

#include <lib-common/src/iop/priv.h>

/* {{{ IOP-described package to iopc_pkg_t */
/* {{{ Helpers */

static iopc_path_t *parse_path(lstr_t name, bool is_type, sb_t *err)
{
    pstream_t ps = ps_initlstr(&name);
    iopc_path_t *path = iopc_path_new();

    while (!ps_done(&ps)) {
        pstream_t bit;

        if (ps_get_ps_chr_and_skip(&ps, '.', &bit) < 0) {
            bit = ps;
            __ps_skip_upto(&ps, ps.s_end);
        } else
        if (ps_done(&ps)) {
            sb_sets(err, "trailing dot in package name");
            goto error;
        }

        if (ps_done(&bit)) {
            sb_setf(err, "empty package or sub-package name");
            goto error;
        }
        qv_append(&path->bits, p_dupz(bit.s, ps_len(&bit)));
    }

    return path;

  error:
    iopc_path_delete(&path);
    return NULL;
}

static int iopc_check_type_name(lstr_t name, sb_t *err)
{
    RETHROW(iopc_check_name(name, NULL, err));

    /* XXX Checked by iopc_check_name(). */
    assert (name.len);

    if (!isupper(name.s[0])) {
        sb_setf(err, "`%pL': first character should be uppercase",
                &name);
        return -1;
    }

    return 0;
}

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
        *out = IOP_UNION_VA(iop__type, i,
                            .is_signed = iop_int_type_is_signed(type),
                            .size = iopsq_int_type_to_int_size(type));
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

qm_kvec_t(iopsq_type_id, iop_full_type_t, uint64_t,
          qhash_iop_full_type_hash, qhash_iop_full_type_equal);

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
static int iopsq_fill_type(const iop_full_type_t *ftype, iop__type__t *type)
{
    lstr_t typename;
    const iop_obj_t *obj;

    if (iop_type_to_iop(ftype->type, type) >= 0) {
        return 0;
    }

    if (ftype->type == IOP_T_ENUM) {
        typename = ftype->en->fullname;
    } else {
        assert (!iop_type_is_scalar(ftype->type));
        typename = ftype->st->fullname;
    }

    if ((obj = iop_get_obj(typename))) {
        switch (obj->type) {
          case IOP_OBJ_TYPE_PKG:
            break;

          case IOP_OBJ_TYPE_ENUM:
            if (ftype->type == IOP_T_ENUM && obj->desc.en == ftype->en) {
                /* The enumeration is registered in the environment so it can
                 * be refered to with a type name. */
                *type = IOP_UNION(iop__type, type_name, typename);
                return 0;
            }
            break;

          case IOP_OBJ_TYPE_ST:
            if (ftype->type != IOP_T_ENUM && obj->desc.st == ftype->st) {
                /* The struct/union/class is registered in the environment so
                 * it can be refered to with a type name. */
                *type = IOP_UNION(iop__type, type_name, typename);
                return 0;
            }
            break;
        }
    }

    return -1;
}

void iopsq_type_table_fill_type(iopsq_type_table_t *table,
                                const iop_full_type_t *ftype,
                                iop__type__t *type)
{
    if (iopsq_fill_type(ftype, type) < 0) {
        int pos;

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
/* {{{ IOP struct/union */

static iop_type_t iop_type_from_iop(const iop__type__t *iop_type)
{
    IOP_UNION_SWITCH(iop_type) {
      IOP_UNION_CASE_P(iop__type, iop_type, i, i) {
        switch (i->size) {
#define CASE(_sz)                                                            \
          case INT_SIZE_S##_sz:                                              \
            return i->is_signed ? IOP_T_I##_sz : IOP_T_U##_sz

            CASE(8);
            CASE(16);
            CASE(32);
            CASE(64);

#undef CASE
        }
      }
      IOP_UNION_CASE_V(iop__type, iop_type, b) {
        return IOP_T_BOOL;
      }
      IOP_UNION_CASE_V(iop__type, iop_type, d) {
        return IOP_T_DOUBLE;
      }
      IOP_UNION_CASE(iop__type, iop_type, s, s) {
        switch (s) {
          case STRING_TYPE_STRING:
            return IOP_T_STRING;

          case STRING_TYPE_BYTES:
            return IOP_T_DATA;

          case STRING_TYPE_XML:
            return IOP_T_XML;
        }
      }
      IOP_UNION_CASE_V(iop__type, iop_type, v) {
        return IOP_T_VOID;
      }
      IOP_UNION_CASE_V(iop__type, iop_type, type_name) {
        /* This case should be handled at higher level. */
        e_panic("should not happen");
      }
      IOP_UNION_CASE_V(iop__type, iop_type, array) {
        /* This case should be handled at higher level. */
        e_panic("should not happen");
      }
      IOP_UNION_CASE_V(iop__type, iop_type, type_id) {
        /* This case should be handled at higher level. */
        e_panic("should not happen");
      }
    }

    return 0;
}

static int
iopc_field_set_typename(iopc_field_t *nonnull f, lstr_t typename,
                        sb_t *nonnull err)
{
    f->kind = iop_get_type(typename);

    if (f->kind == IOP_T_STRUCT) {
        if (lstr_contains(typename, LSTR("."))) {
            /* TODO Could parse and check that the type name looks like a
             * proper type name. */
            const iop_obj_t *obj;

            obj = iop_get_obj(typename);
            if (obj) {
                switch (obj->type) {
                  case IOP_OBJ_TYPE_PKG:
                    /* Not expected to happen if we properly check the name.
                     */
                    sb_sets(err, "is a package name");
                    return -1;

                  case IOP_OBJ_TYPE_ST:
                    f->external_st = obj->desc.st;
                    f->kind = obj->desc.st->is_union ? IOP_T_UNION
                                                     : IOP_T_STRUCT;
                    break;

                  case IOP_OBJ_TYPE_ENUM:
                    f->external_en = obj->desc.en;
                    f->kind = IOP_T_ENUM;
                    break;
                }

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

static int
iopc_field_set_type(iopc_field_t *nonnull f,
                    const iop__type__t *nonnull type,
                    const iopsq_type_table_t *nullable type_table,
                    sb_t *nonnull err)
{
    struct iopsq__type__t * const * array_type;

    if ((array_type = IOP_UNION_GET(iop__type, type, array))) {
        type = *array_type;

        if (IOP_UNION_IS(iop__type, type, array)) {
            sb_setf(err, "multi-dimension arrays are not supported");
            return -1;
        }

        f->repeat = IOP_R_REPEATED;
    }

    IOP_UNION_SWITCH(type) {
      IOP_UNION_CASE(iop__type, type, type_name, typename) {
        if (iopc_field_set_typename(f, typename, err) < 0) {
            return -1;
        }
      }

      IOP_UNION_CASE(iop__type, type, type_id, type_id) {
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
        } else
        if (!iop_type_is_scalar(ftype->type)) {
            f->external_st = ftype->st;
            f->has_external_type = true;
        }
      }

      IOP_UNION_DEFAULT() {
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
      IOP_UNION_CASE(iop__value, defval, i, i) {
        f->defval.u64 = i;
        f->defval_is_signed = (i < 0);
        f->defval_type = IOPC_DEFVAL_INTEGER;
      }
      IOP_UNION_CASE(iop__value, defval, u, u) {
        f->defval.u64 = u;
        f->defval_type = IOPC_DEFVAL_INTEGER;
      }
      IOP_UNION_CASE(iop__value, defval, d, d) {
        f->defval.d = d;
        f->defval_type = IOPC_DEFVAL_DOUBLE;
      }
      IOP_UNION_CASE(iop__value, defval, s, s) {
        f->defval.ptr = p_dupz(s.s, s.len);
        f->defval_type = IOPC_DEFVAL_STRING;
      }
      IOP_UNION_CASE(iop__value, defval, b, b) {
        f->defval.u64 = b;
        f->defval_type = IOPC_DEFVAL_INTEGER;
      }
    }
}

static void
iopc_field_set_opt_info(iopc_field_t *nonnull f,
                        const iop__opt_info__t *nullable opt_info)
{
    if (!opt_info) {
        f->repeat = IOP_R_REQUIRED;
    } else
    if (opt_info->def_val) {
        f->repeat = IOP_R_DEFVAL;
        iopc_field_set_defval(f, opt_info->def_val);
    } else {
        f->repeat = IOP_R_OPTIONAL;
    }
}

static iopc_field_t *
iopc_field_load(const iop__field__t *nonnull field_desc,
                const qv_t(iopc_field) *fields,
                const iopsq_type_table_t *nullable type_table,
                sb_t *nonnull err)
{
    iopc_field_t *f = NULL;

    if (iopc_check_name(field_desc->name, NULL, err) < 0) {
        goto error;
    }
    if (!islower(field_desc->name.s[0])) {
        sb_sets(err, "first field name character should be lowercase");
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
            sb_setf(err, "tag `%d' is already used by field `%s'", f->tag,
                    other_field->name);
            goto error;
        }
    }

    if (iopc_field_set_type(f, &field_desc->type, type_table, err) < 0) {
        goto error;
    }
    if (f->repeat == IOP_R_REPEATED) {
        if (field_desc->optional) {
            sb_setf(err, "repeated field cannot be optional "
                    "or have a default value");
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

    return f;

  error:
    sb_prependf(err, "field `%pL': ", &field_desc->name);
    iopc_field_delete(&f);
    return NULL;
}

static void
iop_structure_get_type_and_fields(const iop__structure__t *desc,
                                  iopc_struct_type_t *type,
                                  iop__field__array_t *fields)
{
    IOP_OBJ_EXACT_SWITCH(desc) {
      IOP_OBJ_CASE_CONST(iop__struct, desc, st) {
        *fields = st->fields;
        *type = STRUCT_TYPE_STRUCT;
      }

      IOP_OBJ_CASE_CONST(iop__union, desc, un) {
        *fields = un->fields;
        *type = STRUCT_TYPE_UNION;
      }

      IOP_OBJ_EXACT_DEFAULT() {
        assert (false);
      }
    }
}

static iopc_struct_t *
iopc_struct_load(const iop__structure__t *nonnull st_desc,
                 const iopsq_type_table_t *nullable type_table,
                 sb_t *nonnull err)
{
    iopc_struct_t *st;
    iop__field__array_t fields = IOP_ARRAY_EMPTY;

    st = iopc_struct_new();
    st->name = p_dupz(st_desc->name.s, st_desc->name.len);
    iop_structure_get_type_and_fields(st_desc, &st->type, &fields);

    tab_for_each_ptr(field_desc, &fields) {
        iopc_field_t *f;

        if (!(f = iopc_field_load(field_desc, &st->fields, type_table, err)))
        {
            iopc_struct_delete(&st);
            return NULL;
        }

        qv_append(&st->fields, f);
    }

    return st;
}

/* }}} */
/* {{{ IOP enum */

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
            sb_setf(err, "key `%pL': the value `%d' is already used",
                    &enum_val->name, val);
            return NULL;
        }
        if (qh_add(lstr, &keys, &enum_val->name) < 0) {
            sb_setf(err, "the key `%pL' is duplicated", &enum_val->name);
            return NULL;
        }
        next_val = val + 1;
    }

    en = iopc_enum_new();
    en->name = p_dupz(en_desc->name.s, en_desc->name.len);
    next_val = 0;
    tab_for_each_ptr(enum_val, &en_desc->values) {
        iopc_enum_field_t *field = iopc_enum_field_new();

        field->name = p_dupz(enum_val->name.s, enum_val->name.len);
        field->value = OPT_DEFVAL(enum_val->val, next_val);
        next_val = field->value + 1;

        qv_append(&en->values, field);
    }

    return en;
}

/* }}} */
/* {{{ IOP package */

static const char *pkg_elem_type_to_str(const iop__package_elem__t *elem)
{
    IOP_OBJ_EXACT_SWITCH(elem) {
      case IOP_CLASS_ID(iop__struct):
        return "struct";

      case IOP_CLASS_ID(iop__union):
        return "union";

      case IOP_CLASS_ID(iop__enum):
        return "enum";
    }

    assert (false);
    return "<unknown>";
}

static iopc_pkg_t *
iopc_pkg_load_from_iop(const iop__package__t *nonnull pkg_desc,
                       const iopsq_type_table_t *nullable type_table,
                       sb_t *nonnull err)
{
    t_scope;
    iopc_pkg_t *pkg = iopc_pkg_new();
    qh_t(lstr) things;

    pkg->file = p_strdup("<none>");
    pkg->name = parse_path(pkg_desc->name, false, err);
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

        IOP_OBJ_SWITCH(iop__package_elem, elem) {
          IOP_OBJ_CASE(iop__structure, elem, st_desc) {
              iopc_struct_t *st;

              if (!(st = iopc_struct_load(st_desc, type_table, err))) {
                  sb_prependf(err, "cannot load `%pL': ", &elem->name);
                  goto error;
              }

              qv_append(&pkg->structs, st);
          }

          IOP_OBJ_CASE(iop__enum, elem, en_desc) {
              iopc_enum_t *en;

              if (!(en = iopc_enum_load(en_desc, err))) {
                  sb_prependf(err, "cannot load enum `%pL': ", &elem->name);
                  goto error;
              }

              qv_append(&pkg->enums, en);
          }

          /* TODO Classes */
          /* TODO Typedefs */
          /* TODO Interfaces */
          /* TODO Modules */
          /* TODO SNMP stuff */

          IOP_OBJ_DEFAULT(iop__package_elem) {
              sb_setf(err,
                      "package elements of type `%pL' are not supported yet",
                      &elem->__vptr->fullname);
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
/* {{{ IOP² API */

iop_pkg_t *mp_iopsq_build_pkg(mem_pool_t *nonnull mp,
                              const iop__package__t *nonnull pkg_desc,
                              const iopsq_type_table_t *nullable type_table,
                              sb_t *nonnull err)
{
    iop_pkg_t *pkg = NULL;
    iopc_pkg_t *iopc_pkg;

    if (!expect(mp->mem_pool & MEM_BY_FRAME)) {
        sb_sets(err, "incompatible memory pool type");
        return NULL;
    }

    if (!(iopc_pkg = iopc_pkg_load_from_iop(pkg_desc, type_table, err))) {
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
        sb_prependf(err, "failed to generate package `%s': ",
                    pretty_path_dot(iopc_pkg->name));
        goto end;
    }

  end:
    iopc_pkg_delete(&iopc_pkg);
    return pkg;
}

iop_pkg_t *
mp_iopsq_build_mono_element_pkg(mem_pool_t *nonnull mp,
                                const iop__package_elem__t *nonnull elem,
                                const iopsq_type_table_t *nullable type_table,
                                sb_t *nonnull err)
{
    iop__package__t pkg_desc;
    iop__package_elem__t *_elem = unconst_cast(iop__package_elem__t, elem);

    iop_init(iop__package, &pkg_desc);
    pkg_desc.name = LSTR("user_package");
    pkg_desc.elems = IOP_TYPED_ARRAY(iop__package_elem, &_elem, 1);

    return mp_iopsq_build_pkg(mp, &pkg_desc, type_table, err);
}

const iop_struct_t *
mp_iopsq_build_struct(mem_pool_t *nonnull mp,
                      const iop__structure__t *nonnull iop_desc,
                      const iopsq_type_table_t *nullable type_table,
                      sb_t *nonnull err)
{
    iop_pkg_t *pkg;

    pkg = RETHROW_P(mp_iopsq_build_mono_element_pkg(mp, &iop_desc->super,
                                                    type_table, err));

    return pkg->structs[0];
}

__must_check__
int iopsq_iop_struct_build(iopsq_iop_struct_t *nonnull st,
                           const iopsq__structure__t *nonnull iop_desc,
                           const iopsq_type_table_t *nullable type_table,
                           sb_t *nonnull err)
{
    assert (!st->mp && !st->st);

    st->mp = mem_ring_new("iop_struct_mp_build", PAGE_SIZE);
    mem_ring_newframe(st->mp);
    st->st = mp_iopsq_build_struct(st->mp, iop_desc, type_table, err);
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
