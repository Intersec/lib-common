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

#include "unix.h"
#include "yaml.h"
#include "iop-yaml.h"
#include "iop-helpers.in.c"
#include "log.h"

static struct iop_yaml_g {
    logger_t logger;
} iop_yaml_g = {
#define _G iop_yaml_g
    .logger = LOGGER_INIT(NULL, "iop-yaml", LOG_INHERITS),
};

/* {{{ yunpack */

typedef struct yunpack_error_t {
    /* the yaml data that caused the error */
    const yaml_data_t * nonnull data;
    /* details of the error */
    sb_t buf;
} yunpack_error_t;

typedef struct yunpack_env_t {
    mem_pool_t *mp;

    yunpack_error_t err;

    /* Only IOP_UNPACK_FORBID_PRIVATE is handled. */
    int flags;
} yunpack_env_t;

/* {{{ Yaml scalar to iop field */

/* FIXME: compare with JSON to have as few type mismatch as possible, for
 * backward-compatibility */

typedef enum yunpack_res_t {
    YUNPACK_INVALID_B64_VAL = -4,
    YUNPACK_INVALID_ENUM_VAL = -3,
    YUNPACK_TYPE_MISMATCH = -2,
    YUNPACK_OOB = -1,
    YUNPACK_OK = 0,
} yunpack_res_t;

static yunpack_res_t
yaml_nil_to_iop_field(const iop_field_t * nonnull fdesc,
                      bool in_array, void * nonnull out)
{
    if (!in_array && fdesc->repeat == IOP_R_REPEATED) {
        lstr_t *arr = out;

        /* null value on an array means empty array */
        p_clear(arr, 1);
        return YUNPACK_OK;
    }

    switch (fdesc->type) {
      case IOP_T_STRING:
      case IOP_T_XML:
      case IOP_T_DATA:
        *(lstr_t *)out = LSTR_NULL_V;
        return YUNPACK_OK;
      case IOP_T_VOID:
        return YUNPACK_OK;
      default:
        return YUNPACK_TYPE_MISMATCH;
    }
}

static yunpack_res_t
yaml_string_to_iop_field(mem_pool_t *mp, const lstr_t str,
                         const iop_field_t * nonnull fdesc,
                         void * nonnull out)
{
    switch (fdesc->type) {
      case IOP_T_STRING:
      case IOP_T_XML:
        *(lstr_t *)out = mp_lstr_dup(mp, str);
        return YUNPACK_OK;

      case IOP_T_DATA: {
        sb_t  sb;
        /* TODO: factorize this with iop-json, iop-xml, etc */
        int   blen = DIV_ROUND_UP(str.len * 3, 4);
        char *buf  = mp_new_raw(mp, char, blen + 1);
        lstr_t *data = out;

        sb_init_full(&sb, buf, 0, blen + 1, &mem_pool_static);
        if (sb_add_lstr_unb64(&sb, str) < 0) {
            mp_delete(mp, &buf);
            return YUNPACK_INVALID_B64_VAL;
        }
        data->data = buf;
        data->len  = sb.len;
        return YUNPACK_OK;
      }

      case IOP_T_ENUM: {
        bool found;
        int i;

        i = iop_enum_from_lstr_desc(fdesc->u1.en_desc, str, &found);
        if (!found) {
            return YUNPACK_INVALID_ENUM_VAL;
        }
        *(int32_t *)out = i;
        return 0;
      }

      default:
        return YUNPACK_TYPE_MISMATCH;
    }
}

static void
set_string_from_stream(mem_pool_t * nonnull mp,
                       const yaml_data_t * nonnull data,
                       lstr_t * nonnull out)
{
    *out = mp_lstr_dups(mp, data->pos_start.s,
                        data->pos_end.s - data->pos_start.s);
}

static yunpack_res_t
yaml_double_to_iop_field(mem_pool_t * nonnull mp,
                         const yaml_data_t * nonnull data,
                         double d, const iop_field_t * nonnull fdesc,
                         void * nonnull out)
{
    switch (fdesc->type) {
      case IOP_T_DOUBLE:
        *(double *)out = d;
        return YUNPACK_OK;
      case IOP_T_STRING:
        set_string_from_stream(mp, data, out);
        return YUNPACK_OK;
      default:
        return YUNPACK_TYPE_MISMATCH;
    }
}

static int
yaml_uint_to_iop_field(mem_pool_t * nonnull mp,
                       const yaml_data_t * nonnull data,
                       uint64_t u, const iop_field_t * nonnull fdesc,
                       void * nonnull out)
{
#define CHECK_MAX(v, max)  THROW_IF(v > max, YUNPACK_OOB)

    switch (fdesc->type) {
      case IOP_T_I8:
        CHECK_MAX(u, INT8_MAX);
        *(int8_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_U8:
        CHECK_MAX(u, UINT8_MAX);
        *(uint8_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_I16:
        CHECK_MAX(u, INT16_MAX);
        *(int16_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_U16:
        CHECK_MAX(u, UINT16_MAX);
        *(uint16_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_I32:
        CHECK_MAX(u, INT32_MAX);
        *(int32_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_U32:
        CHECK_MAX(u, UINT32_MAX);
        *(uint32_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_I64:
        CHECK_MAX(u, INT64_MAX);
        *(int64_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_U64:
        *(uint64_t *)out = u;
        return YUNPACK_OK;
      case IOP_T_STRING:
        set_string_from_stream(mp, data, out);
        return YUNPACK_OK;
      default:
        return YUNPACK_TYPE_MISMATCH;
    }

#undef CHECK_MAX
}

static int
yaml_int_to_iop_field(int64_t i, const iop_field_t * nonnull fdesc,
                      void * nonnull out)
{
#define CHECK_RANGE(v, min, max)  THROW_IF(v < min || v > max, YUNPACK_OOB)

    switch (fdesc->type) {
      case IOP_T_I8:
        CHECK_RANGE(i, INT8_MIN, INT8_MAX);
        *(int8_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_U8:
        CHECK_RANGE(i, 0, UINT8_MAX);
        *(uint8_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_I16:
        CHECK_RANGE(i, INT16_MIN, INT16_MAX);
        *(int16_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_U16:
        CHECK_RANGE(i, 0, UINT16_MAX);
        *(uint16_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_I32:
        CHECK_RANGE(i, INT32_MIN, INT32_MAX);
        *(int32_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_U32:
        CHECK_RANGE(i, 0, UINT32_MAX);
        *(uint32_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_I64:
        *(int64_t *)out = i;
        return YUNPACK_OK;
      case IOP_T_U64:
        CHECK_RANGE(i, 0, INT64_MAX);
        *(uint64_t *)out = i;
        return YUNPACK_OK;
      default:
        return YUNPACK_TYPE_MISMATCH;
    }
}

#undef CHECK_RANGE

static int
yaml_bool_to_iop_field(mem_pool_t * nonnull mp,
                       const yaml_data_t * nonnull data,
                       bool b, const iop_field_t * nonnull fdesc,
                       void * nonnull out)
{
    switch (fdesc->type) {
      case IOP_T_BOOL:
        *(bool *)out = b;
        return YUNPACK_OK;
      case IOP_T_STRING:
        set_string_from_stream(mp, data, out);
        return YUNPACK_OK;
      default:
        return YUNPACK_TYPE_MISMATCH;
    }
}

static yunpack_res_t
yaml_scalar_to_iop_field(const yunpack_env_t * nonnull env,
                         const yaml_data_t * nonnull data,
                         const iop_field_t * nonnull fdesc,
                         bool in_array, void * nonnull out)
{
    const yaml_scalar_t *scalar = &data->scalar;

    /* backward compatibility: unpack a scalar into an array as an array
     * of one element. */
    if (!in_array && fdesc->repeat == IOP_R_REPEATED
    &&  scalar->type != YAML_SCALAR_NULL)
    {
        lstr_t *arr = out;

        out = mp_imalloc(env->mp, fdesc->size, 8, 0);
        *arr = mp_lstr_init(env->mp, out, 1);
    }

    switch (scalar->type) {
      case YAML_SCALAR_NULL:
        return yaml_nil_to_iop_field(fdesc, in_array, out);
      case YAML_SCALAR_STRING:
        return yaml_string_to_iop_field(env->mp, scalar->s, fdesc, out);
      case YAML_SCALAR_DOUBLE:
        return yaml_double_to_iop_field(env->mp, data, scalar->d, fdesc, out);
      case YAML_SCALAR_UINT:
        return yaml_uint_to_iop_field(env->mp, data, scalar->u, fdesc, out);
      case YAML_SCALAR_INT:
        return yaml_int_to_iop_field(scalar->i, fdesc, out);
      case YAML_SCALAR_BOOL:
        return yaml_bool_to_iop_field(env->mp, data, scalar->b, fdesc, out);
    }

    assert (false);
    return YUNPACK_TYPE_MISMATCH;
}

/* }}} */
/* {{{ Yaml data to union */

/* XXX: the in_array argument is required for the "scalar -> [scalar]"
 * backward compat. When unpacking a sequence, it is false, but when true,
 * arrays are detected and scalars are automatically put in a single element
 * array. */
static int
yaml_data_to_iop_field(yunpack_env_t * nonnull env,
                       const yaml_data_t * nonnull data,
                       const iop_struct_t * nonnull st_desc,
                       const iop_field_t * nonnull fdesc,
                       bool in_array, void * nonnull out);

static int
check_constraints(const iop_struct_t * nonnull desc,
                  const iop_field_t * nonnull fdesc, void * nonnull value)
{
    if (likely(!iop_field_has_constraints(desc, fdesc))) {
        return 0;
    }

    if (fdesc->repeat == IOP_R_REPEATED) {
        iop_array_i8_t *arr = value;

        return iop_field_check_constraints(desc, fdesc, arr->tab, arr->len,
                                           false);
    } else {
        return iop_field_check_constraints(desc, fdesc, value, 1, false);
    }
}

static int
yaml_data_to_union(yunpack_env_t * nonnull env,
                   const yaml_data_t * nonnull data,
                   const iop_struct_t * nonnull st_desc, void * nonnull out)
{
    if (data->type != YAML_DATA_OBJ) {
        sb_setf(&env->err.buf, "cannot unpack %s into a union",
                yaml_data_get_type(data));
        goto error;
    }

    if (data->tag.s) {
        sb_setf(&env->err.buf, "specifying a tag is not allowed");
        goto error;
    }

    if (qm_len(yaml_data, &data->obj->fields) != 1) {
        sb_setf(&env->err.buf, "a single key must be specified");
        goto error;
    }

    qm_for_each_key_value_p(yaml_data, key, val, &data->obj->fields) {
        const iop_field_t *field_desc = NULL;

        iop_field_find_by_name(st_desc, key, NULL, &field_desc);
        if (!field_desc) {
            sb_setf(&env->err.buf, "unknown field `%pL`", &key);
            goto error;
        }

        iop_union_set_tag(st_desc, field_desc->tag, out);
        out = (char *)out + field_desc->data_offs;
        if (yaml_data_to_iop_field(env, val, st_desc, field_desc, false,
                                   out) < 0)
        {
            /* keep the data causing the issue in the err. */
            data = env->err.data;
            goto error;
        }

        if (check_constraints(st_desc, field_desc, out) < 0) {
            sb_setf(&env->err.buf, "field `%pL` is invalid: %s", &key,
                    iop_get_err());
            goto error;
        }
    }

    return 0;

  error:
    sb_prependf(&env->err.buf, "cannot unpack YAML as a `%pL` IOP union: ",
                &st_desc->fullname);
    env->err.data = data;
    return -1;
}

/* }}} */
/* {{{ Yaml obj to iop field */

static int
check_class(yunpack_env_t * nonnull env,
            const iop_struct_t * nonnull st)
{
    if (st->class_attrs->is_abstract) {
        sb_setf(&env->err.buf, "`%pL` is abstract and cannot be unpacked",
                &st->fullname);
        return -1;
    }

    if (env->flags & IOP_UNPACK_FORBID_PRIVATE
    &&  st->class_attrs->is_private)
    {
        sb_setf(&env->err.buf, "`%pL` is private and cannot be unpacked",
                &st->fullname);
        return -1;
    }

    return 0;
}

static const yaml_data_t * nullable
yaml_data_get_field_value(const yaml_data_t * nonnull data,
                          const lstr_t field_name)
{
    if (data->type == YAML_DATA_OBJ) {
        return qm_get_def_p(yaml_data, &data->obj->fields, &field_name, NULL);
    } else {
        assert (data->type == YAML_DATA_SCALAR
            &&  data->scalar.type == YAML_SCALAR_NULL);
        return NULL;
    }
}

static int
yaml_skip_iop_field(yunpack_env_t * nonnull env,
                    const iop_struct_t * nonnull st,
                    const iop_field_t * nonnull fdesc, void * nonnull out)
{
    if (iop_skip_absent_field_desc(env->mp, out, st, fdesc) < 0) {
        const char *iop_err = iop_get_err();

        if (iop_err) {
            sb_setf(&env->err.buf, "field `%pL` is invalid: %s",
                    &fdesc->name, iop_err);
        } else {
            sb_setf(&env->err.buf, "missing field `%pL`", &fdesc->name);
        }
        return -1;
    }
    return 0;
}

static int
yaml_fill_iop_field(yunpack_env_t * nonnull env,
                    const yaml_data_t * nonnull data,
                    const iop_struct_t * nonnull st,
                    const iop_field_t * nonnull fdesc, void * nonnull out)
{
    if (env->flags & IOP_UNPACK_FORBID_PRIVATE) {
        const iop_field_attrs_t *attrs;

        attrs = iop_field_get_attrs(st, fdesc);
        if (attrs && TST_BIT(&attrs->flags, IOP_FIELD_PRIVATE)) {
            sb_setf(&env->err.buf, "unknown field `%pL`", &fdesc->name);
            return -1;
        }
    }

    out = (char *)out + fdesc->data_offs;
    if (yaml_data_to_iop_field(env, data, st, fdesc, false, out) < 0) {
        return -1;
    }

    if (check_constraints(st, fdesc, out) < 0) {
        sb_setf(&env->err.buf, "field `%pL` is invalid: %s", &fdesc->name,
                iop_get_err());
        return -1;
    }

    return 0;
}

static void
yaml_data_find_extra_key(yunpack_env_t * nonnull env,
                         const yaml_data_t * nonnull data,
                         const iop_struct_t * nonnull st)
{
    qm_for_each_key(yaml_data, key, &data->obj->fields) {
        if (iop_field_find_by_name(st, key, NULL, NULL) < 0) {
            sb_setf(&env->err.buf, "unknown field `%pL`", &key);
            return;
        }
    }

    assert (false);
}

static int
yaml_data_to_typed_struct(yunpack_env_t * nonnull env,
                          const yaml_data_t * nonnull data,
                          const iop_struct_t * nonnull st,
                          void * nonnull out)
{
    const iop_struct_t *real_st = st;
    int nb_fields_matched;

    if (st->is_union) {
        return yaml_data_to_union(env, data, st, out);
    }

    switch (data->type) {
      case YAML_DATA_SCALAR:
        if (data->scalar.type == YAML_SCALAR_NULL) {
            break;
        }
        /* FALLTHROUGH */
      case YAML_DATA_SEQ:
        sb_setf(&env->err.buf, "cannot unpack %s into a struct",
                yaml_data_get_type(data));
        goto error;
      case YAML_DATA_OBJ:
        break;
    }

    if (data->tag.s) {
        if (iop_struct_is_class(st)) {
            real_st = iop_get_class_by_fullname(st, data->tag);
            if (!real_st) {
                sb_setf(&env->err.buf, "unknown type `%pL` provided in tag, "
                        "or not a child of `%pL`", &data->tag,
                        &st->fullname);
                real_st = st;
                goto error;
            }
            if (!iop_class_is_a(real_st, st)) {
                sb_setf(&env->err.buf, "provided tag `%pL` is not a child of "
                        "`%pL`", &real_st->fullname, &st->fullname);
                real_st = st;
                goto error;
            }
        } else {
            if (!lstr_equal(st->fullname, data->tag)) {
                sb_setf(&env->err.buf, "wrong type `%pL` provided in tag, "
                        "expected `%pL`", &data->tag, &st->fullname);
                goto error;
            }
        }
    }

    if (iop_struct_is_class(real_st)) {
        void **out_class = out;

        if (check_class(env, real_st) < 0) {
            goto error;
        }

        *out_class = mp_imalloc(env->mp, real_st->size, 8, MEM_RAW);
        *(const iop_struct_t **)(*out_class) = real_st;
        out = *out_class;
    }

    st = real_st;
    nb_fields_matched = 0;
    iop_struct_for_each_field(field_desc, field_st, real_st) {
        const yaml_data_t *val;

        val = yaml_data_get_field_value(data, field_desc->name);
        if (val) {
            if (yaml_fill_iop_field(env, val, field_st, field_desc, out) < 0)
            {
                goto error;
            }
            nb_fields_matched++;
        } else {
            if (yaml_skip_iop_field(env, field_st, field_desc, out) < 0) {
                goto error;
            }
        }
    }

    if (data->type == YAML_DATA_OBJ
    &&  unlikely(nb_fields_matched != qm_len(yaml_data, &data->obj->fields)))
    {
        /* There are fields in the YAML object that have not been matched.
         * The handling of this error is kept in a cold path, as it is
         * supposed to be rare. */
        assert (nb_fields_matched < qm_len(yaml_data, &data->obj->fields));
        yaml_data_find_extra_key(env, data, real_st);
        goto error;
    }

    return 0;

  error:
    sb_prependf(&env->err.buf, "cannot unpack YAML as a `%pL` IOP struct: ",
                &real_st->fullname);
    if (!env->err.data) {
        env->err.data = data;
    }
    return -1;
}

static void yaml_set_type_mismatch_err(yunpack_env_t * nonnull env,
                                       const yaml_data_t * nonnull data,
                                       const iop_field_t * nonnull fdesc)
{
    sb_setf(&env->err.buf, "cannot set %s in a field of type %s",
            yaml_data_get_type(data),
            iop_type_get_string_desc(fdesc->type));
    env->err.data = data;
}

/* }}} */
/* {{{ Yaml seq to iop field */

static int
yaml_seq_to_iop_field(yunpack_env_t * nonnull env,
                      const yaml_data_t * nonnull data,
                      const iop_struct_t * nonnull st_desc,
                      const iop_field_t * nonnull fdesc, void * nonnull out)
{
    lstr_t *arr = out;
    int size = 0;

    if (fdesc->repeat != IOP_R_REPEATED) {
        sb_sets(&env->err.buf, "cannot set a sequence in a non-array field");
        env->err.data = data;
        return -1;
    }

    p_clear(arr, 1);
    assert (data->type == YAML_DATA_SEQ);
    for (uint32_t i = 0; i < data->seq_len; i++) {
        void *elem_out;

        if (arr->len >= size) {
            size = p_alloc_nr(size);
            arr->data = mp_irealloc(env->mp, arr->data,
                                    arr->len * fdesc->size,
                                    size * fdesc->size, 8, 0);
        }
        elem_out = (void *)(((char *)arr->data) + arr->len * fdesc->size);
        RETHROW(yaml_data_to_iop_field(env, &data->seq[i], st_desc, fdesc,
                                       true, elem_out));
        arr->len++;
    }

    return 0;
}

/* }}} */
/* {{{ Yaml data to iop field */

static int
yaml_data_to_iop_field(yunpack_env_t *env, const yaml_data_t * nonnull data,
                       const iop_struct_t * nonnull st_desc,
                       const iop_field_t * nonnull fdesc,
                       bool in_array, void * nonnull out)
{
    if (fdesc->repeat == IOP_R_OPTIONAL && !iop_field_is_class(fdesc)) {
        out = iop_field_set_present(env->mp, fdesc, out);
    }

    if (fdesc->type == IOP_T_STRUCT || fdesc->type == IOP_T_UNION) {
        if (iop_field_is_reference(fdesc)) {
           /* reference fields must be dereferenced */
            out = iop_field_ptr_alloc(env->mp, fdesc, out);
        }
        if (yaml_data_to_typed_struct(env, data, fdesc->u1.st_desc, out) < 0)
        {
            goto err;
        }
        return 0;
    }

    if (data->tag.s) {
        sb_setf(&env->err.buf, "specifying a tag is not allowed");
        env->err.data = data;
        goto err;
    }

    switch (data->type) {
      case YAML_DATA_SCALAR:
        switch (yaml_scalar_to_iop_field(env, data, fdesc, in_array, out)) {
          case YUNPACK_OK:
            break;
          case YUNPACK_INVALID_B64_VAL:
            sb_setf(&env->err.buf, "the value must be encoded in base64");
            env->err.data = data;
            goto err;
          case YUNPACK_INVALID_ENUM_VAL:
            sb_setf(&env->err.buf,
                    "the value is not valid for the enum `%pL`",
                    &fdesc->u1.en_desc->name);
            env->err.data = data;
            goto err;
          case YUNPACK_TYPE_MISMATCH:
            yaml_set_type_mismatch_err(env, data, fdesc);
            goto err;
          case YUNPACK_OOB:
            sb_setf(&env->err.buf,
                    "the value is out of range for the field of type %s",
                    iop_type_get_string_desc(fdesc->type));
            env->err.data = data;
            goto err;
          default:
            assert (false);
            return -1;
        }
        break;

      case YAML_DATA_OBJ:
        /* should have been covered by test on IOP_T_STRUCT earlier */
        yaml_set_type_mismatch_err(env, data, fdesc);
        goto err;

      case YAML_DATA_SEQ:
        if (yaml_seq_to_iop_field(env, data, st_desc, fdesc, out) < 0) {
            goto err;
        }
        break;

      default:
        assert (false);
        return -1;
    }

    logger_trace(&_G.logger, 2,
                 "unpack %s from "YAML_POS_FMT" up to "YAML_POS_FMT
                 " into field %pL of struct %pL",
                 yaml_data_get_type(data), YAML_POS_ARG(data->pos_start),
                 YAML_POS_ARG(data->pos_end), &fdesc->name,
                 &st_desc->fullname);
    return 0;

  err:
    sb_prependf(&env->err.buf, "cannot set field `%pL`: ", &fdesc->name);
    return -1;
}

/* }}} */

static void yunpack_err_pretty_print(const yunpack_error_t *err,
                                     const iop_struct_t * nonnull st,
                                     const char * nullable filename,
                                     const pstream_t *full_input, sb_t *out)
{
    pstream_t ps;
    bool one_liner;

    if (filename) {
        sb_addf(out, "%s:", filename);
    }
    sb_addf(out, YAML_POS_FMT": %pL", YAML_POS_ARG(err->data->pos_start),
            &err->buf);

    one_liner = err->data->pos_end.line_nb
             == err->data->pos_start.line_nb;

    /* get the full line including pos_start */
    ps.s = err->data->pos_start.s;
    ps.s -= err->data->pos_start.col_nb - 1;

    /* find the end of the line */
    ps.s_end = one_liner ? err->data->pos_end.s - 1 : ps.s;
    while (ps.s_end < full_input->s_end && *ps.s_end != '\n') {
        ps.s_end++;
    }
    /* print the whole line */
    sb_addf(out, "\n%*pM\n", PS_FMT_ARG(&ps));

    /* then display some indications or where the issue is */
    for (unsigned i = 1; i < err->data->pos_start.col_nb; i++) {
        sb_addc(out, ' ');
    }
    if (one_liner) {
        for (unsigned i = err->data->pos_start.col_nb;
             i < err->data->pos_end.col_nb; i++)
        {
            sb_addc(out, '^');
        }
    } else {
        sb_adds(out, "^ starting here");
    }
}

static int
_t_iop_yunpack_ps(pstream_t * nonnull ps, const iop_struct_t * nonnull st,
                  const char * nullable filename, void * nonnull out,
                  sb_t * nonnull out_err)
{
    t_SB_1k(err);
    yunpack_env_t unpack_env;
    yaml_data_t data;

    RETHROW(t_yaml_parse(*ps, &data, out_err));

    p_clear(&unpack_env, 1);
    unpack_env.mp = t_pool();
    unpack_env.err.buf = err;
    /* The YAML packer is made for public interfaces. Use flags that makes
     * sense in this context. In the future, they might be overridden if
     * some internal use-cases are found. */
    unpack_env.flags = IOP_UNPACK_FORBID_PRIVATE;
    if (yaml_data_to_typed_struct(&unpack_env, &data, st, out) < 0) {
        yunpack_err_pretty_print(&unpack_env.err, st, filename, ps, out_err);
        return -1;
    }

    /* XXX: may be removed in the future, but useful while the code is still
     * young to ensure we did not mess up our unpacking. */
#ifndef NDEBUG
    {
        void *val = iop_struct_is_class(st) ? *(void **)out : out;

        if (!expect(iop_check_constraints_desc(st, val) >= 0)) {
            sb_setf(&unpack_env.err.buf, "invalid object: %s", iop_get_err());
            unpack_env.err.data = &data;
            yunpack_err_pretty_print(&unpack_env.err, st, filename, ps,
                                     out_err);
            return -1;
        }
    }
#endif

    return 0;
}

int t_iop_yunpack_ps(pstream_t * nonnull ps, const iop_struct_t * nonnull st,
                     void * nonnull out, sb_t * nonnull out_err)
{
    return _t_iop_yunpack_ps(ps, st, NULL, out, out_err);
}

static void * nonnull t_alloc_st_out(const iop_struct_t * nonnull st,
                                     void * nullable * nonnull out)
{
    if (iop_struct_is_class(st)) {
        /* "out" will be (re)allocated after, when the real packed class type
         * will be known. */
        return out;
    } else {
        *out = mp_irealloc(t_pool(), *out, 0, st->size, 8, MEM_RAW);
        return *out;
    }
}

int
t_iop_yunpack_ptr_ps(pstream_t * nonnull ps, const iop_struct_t * nonnull st,
                     void * nullable * nonnull out, sb_t * nonnull out_err)
{
    return t_iop_yunpack_ps(ps, st, t_alloc_st_out(st, out), out_err);
}

int t_iop_yunpack_file(const char * nonnull filename,
                       const iop_struct_t * nonnull st,
                       void * nullable * nonnull out,
                       sb_t * nonnull out_err)
{
    lstr_t file = LSTR_NULL_V;
    pstream_t ps;
    int res = 0;

    if (lstr_init_from_file(&file, filename, PROT_READ, MAP_SHARED) < 0) {
        sb_setf(out_err, "cannot read file %s: %m", filename);
        return -1;
    }

    ps = ps_initlstr(&file);
    res = _t_iop_yunpack_ps(&ps, st, filename, out, out_err);
    lstr_wipe(&file);

    return res;
}

/** Convert a YAML file into an IOP C structure using the t_pool().
 *
 * See t_iop_junpack_ptr_ps.
 */
__must_check__
int t_iop_yunpack_ptr_file(const char * nonnull filename,
                           const iop_struct_t * nonnull st,
                           void * nullable * nonnull out,
                           sb_t * nonnull out_err)
{
    return t_iop_yunpack_file(filename, st, t_alloc_st_out(st, out), out_err);
}

/* }}} */
/* {{{ ypack */

#define YAML_STD_INDENT  2

/* FIXME: need massive factorisation with iop-json.blk */

typedef struct iop_ypack_env_t {
    iop_jpack_writecb_f *write_cb;
    void *priv;
    unsigned flags;
} iop_ypack_env_t;

static int do_write(const iop_ypack_env_t *env, const void *_buf, int len)
{
    const uint8_t *buf = _buf;
    int pos = 0;

    while (pos < len) {
        int res = (*env->write_cb)(env->priv, buf + pos, len - pos);

        if (res < 0) {
            if (ERR_RW_RETRIABLE(errno))
                continue;
            return -1;
        }
        pos += res;
    }
    return len;
}

static int do_indent(const iop_ypack_env_t *env, int indent)
{
    static lstr_t spaces = LSTR_IMMED("                                    ");
    int todo = indent;

    while (todo > 0) {
        int res = (*env->write_cb)(env->priv, spaces.s,
                                   MIN(spaces.len, todo));

        if (res < 0) {
            if (ERR_RW_RETRIABLE(errno)) {
                continue;
            }
            return -1;
        }
        todo -= res;
    }

    return indent;
}

#define WRITE(data, len)                                                     \
    do {                                                                     \
        res += RETHROW(do_write(env, data, len));                            \
    } while (0)
#define PUTS(s)  WRITE(s, strlen(s))
#define PUTLSTR(s)  WRITE(s.data, s.len)

#define INDENT(lvl)                                                          \
    do {                                                                     \
        res += RETHROW(do_indent(env, lvl));                                 \
    } while (0)

#define PUTU(u)                                                              \
    do {                                                                     \
        uint64_t _u = (u);                                                   \
                                                                             \
        WRITE(ibuf, sprintf(ibuf, "%ju", _u));                               \
    } while (0)
#define PUTD(i)                                                              \
    do {                                                                     \
        int64_t _i = (i);                                                    \
                                                                             \
        WRITE(ibuf, sprintf(ibuf, "%jd", _i));                               \
    } while (0)

/* ints:   sign, 20 digits, and NUL -> 22
 * double: sign, digit, dot, 17 digits, e, sign, up to 3 digits NUL -> 25
 */
#define IBUF_LEN  25

static bool yaml_string_must_be_quoted(const lstr_t s)
{
    /* '!', '&', '*', '-', '"' and '.'. Technically, '-' is only forbidden
     * if followed by a space, but it is simpler that way. */
    static ctype_desc_t const yaml_invalid_raw_string_start = { {
        0x00000000, 0x00006446, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    /* printable ascii characters minus ':' and '#'. Also should be
     * followed by space to be forbidden, but simpler that way. */
    static ctype_desc_t const yaml_raw_string_contains = { {
        0x00000000, 0xfbfffff7, 0xffffffff, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };

    if (s.len == 0) {
        return true;
    }

    /* cannot start with those characters */
    if (ctype_desc_contains(&yaml_invalid_raw_string_start, s.s[0])) {
        return true;
    }
    /* cannot contain those characters */
    if (!lstr_match_ctype(s, &yaml_raw_string_contains)) {
        return true;
    }
    if (lstr_equal(s, LSTR("~")) || lstr_equal(s, LSTR("null"))) {
        return true;
    }

    return false;
}

static int write_string(lstr_t val, iop_type_t type,
                        const iop_ypack_env_t *env)
{
    SB_8k(sb);
    int res = 0;

    if (type == IOP_T_DATA) {
        if (val.len) {
            sb_reset(&sb);
            sb_addlstr_b64(&sb, val, -1);
            WRITE(sb.data, sb.len);
            return res;
        }
    }

    if (yaml_string_must_be_quoted(val)) {
        pstream_t ps = ps_initlstr(&val);

        PUTS("\"");
        while (!ps_done(&ps)) {
            /* r:32-127 -s:'\\"' */
            static ctype_desc_t const safe_chars = { {
                0x00000000, 0xfffffffb, 0xefffffff, 0xffffffff,
                    0x00000000, 0x00000000, 0x00000000, 0x00000000,
            } };
            const uint8_t *p = ps.b;
            size_t nbchars;
            int c;

            nbchars = ps_skip_span(&ps, &safe_chars);
            WRITE(p, nbchars);

            if (ps_done(&ps)) {
                break;
            }

            /* Assume broken utf-8 is mixed latin1 */
            c = ps_getuc(&ps);
            if (unlikely(c < 0)) {
                c = ps_getc(&ps);
            }
            switch (c) {
              case '"':  PUTS("\\\""); break;
              case '\\': PUTS("\\\\"); break;
              case '\a': PUTS("\\a"); break;
              case '\b': PUTS("\\b"); break;
              case '\e': PUTS("\\e"); break;
              case '\f': PUTS("\\f"); break;
              case '\n': PUTS("\\n"); break;
              case '\r': PUTS("\\r"); break;
              case '\t': PUTS("\\t"); break;
              case '\v': PUTS("\\v"); break;
              default: {
                char ibuf[IBUF_LEN];

                WRITE(ibuf, sprintf(ibuf, "\\u%04x", c));
              } break;
            }
        }
        PUTS("\"");
    } else {
        PUTLSTR(val);
    }

    return res;
}

static int
iop_ypack_typed_struct(const iop_struct_t *desc, const void *value,
                       const iop_ypack_env_t *env, unsigned indent,
                       bool to_indent);

/* We expect the caller to have left us in a situation where we are already
 * indented by "indent" spaces.
 */
static int
iop_ypack_raw_struct(const iop_struct_t *desc, const void *value,
                     const iop_ypack_env_t *env, unsigned indent,
                     bool to_indent)
{
    const iop_field_t *fstart;
    const iop_field_t *fend;
    char ibuf[IBUF_LEN];
    int res = 0;
    bool first = !to_indent;

    if (desc->is_union) {
        fstart = get_union_field(desc, value);
        fend = fstart + 1;
    } else {
        fstart = desc->fields;
        fend = desc->fields + desc->fields_len;
    }

    for (const iop_field_t *fdesc = fstart; fdesc < fend; fdesc++) {
        bool repeated = fdesc->repeat == IOP_R_REPEATED;
        const void *ptr;
        int n;
        bool is_skipped = false;

        ptr = iop_json_get_n_and_ptr(desc, env->flags, fdesc, value, &n,
                                     &is_skipped);
        if (is_skipped) {
            continue;
        }

        if (first) {
            first = false;
        } else {
            PUTS("\n");
            INDENT(indent);
        }

        PUTLSTR(fdesc->name);
        PUTS(":");
        if (n == 0) {
            PUTS(" ~");
            continue;
        }

        for (int j = 0; j < n; j++) {
            unsigned field_indent = indent;

            if (repeated) {
                PUTS("\n");
                field_indent += YAML_STD_INDENT;
                INDENT(field_indent);
                PUTS("-");
            }

            switch (fdesc->type) {
                const void *v;

#define CASE(n) \
              case IOP_T_I##n:                                               \
                PUTS(" ");                                                   \
                PUTD(IOP_FIELD(int##n##_t, ptr, j));                         \
                break;                                                       \
              case IOP_T_U##n:                                               \
                PUTS(" ");                                                   \
                PUTD(IOP_FIELD(uint##n##_t, ptr, j));                        \
                break;
              CASE(8); CASE(16); CASE(32); CASE(64);
#undef CASE

              case IOP_T_ENUM:
                v = iop_enum_to_str_desc(fdesc->u1.en_desc,
                                         IOP_FIELD(int, ptr, j)).s;
                PUTS(" ");
                if (likely(v)) {
                    PUTS(v);
                } else {
                    /* if not found, dump the integer */
                    PUTU(IOP_FIELD(int, ptr, j));
                }
                break;

              case IOP_T_BOOL:
                if (IOP_FIELD(bool, ptr, j)) {
                    PUTS(" true");
                } else {
                    PUTS(" false");
                }
                break;

              case IOP_T_DOUBLE: {
                double d = IOP_FIELD(double, ptr, j);
                int inf = isinf(d);

                PUTS(" ");
                if (inf == 1) {
                    PUTS(".Inf");
                } else
                if (inf == -1) {
                    PUTS("-.Inf");
                } else
                if (isnan(d)) {
                    PUTS(".NaN");
                } else {
                    WRITE(ibuf, sprintf(ibuf, "%g", d));
                }
              } break;

              case IOP_T_UNION:
              case IOP_T_STRUCT: {
                v = iop_json_get_struct_field_value(fdesc, ptr, j);
                /* Write the field inline. */
                if (repeated) {
                    PUTS(" ");
                    field_indent += 2;
                } else {
                    field_indent += YAML_STD_INDENT;
                }
                res += RETHROW(iop_ypack_typed_struct(fdesc->u1.st_desc, v,
                                                      env, field_indent,
                                                      true));
              } break;

              case IOP_T_STRING:
              case IOP_T_XML:
              case IOP_T_DATA: {
                const lstr_t *sv = &IOP_FIELD(const lstr_t, ptr, j);

                /* Write the field inline. */
                PUTS(" ");
                res += RETHROW(write_string(*sv, fdesc->type, env));
              } break;

              case IOP_T_VOID:
                PUTS(" ~");
                break;

              default:
                abort();
            }
        }
    }

    return res;
}

static int
iop_ypack_typed_struct(const iop_struct_t *desc, const void *value,
                       const iop_ypack_env_t *env, unsigned indent,
                       bool to_indent)
{
    int res = 0;
    int type_header_len = 0;

    if (iop_struct_is_class(desc)) {
        qv_t(iop_struct) parents;
        const iop_struct_t *real_desc = *(const iop_struct_t **)value;

        e_assert(panic, !real_desc->class_attrs->is_abstract,
                 "packing of abstract class '%*pM' is forbidden",
                 LSTR_FMT_ARG(real_desc->fullname));

        /* If this assert fails, you are exporting private classes through
         * a public interface... this is BAD!
         */
        assert (!real_desc->class_attrs->is_private);

        /* Write type of class */
        if (desc != real_desc
        ||  !(env->flags & IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES))
        {
            if (to_indent) {
                PUTS(" !");
            } else {
                PUTS("!");
            }
            PUTLSTR(real_desc->fullname);
            to_indent = true;
            type_header_len = res;
        }

        /* We want to write the fields in the order "master -> children", and
         * not "children -> master", so first build a qvector of the parents.
         */
        qv_inita(&parents, 8);
        do {
            qv_append(&parents, real_desc);
            real_desc = real_desc->class_attrs->parent;
        } while (real_desc);

        /* Write fields of different levels */
        for (int pos = parents.len; pos-- > 0; ) {
            bool first_field = pos == parents.len - 1;

            res += RETHROW(iop_ypack_raw_struct(parents.tab[pos], value, env,
                                                indent,
                                                to_indent || !first_field));
        }
        qv_wipe(&parents);

    } else {
        res += RETHROW(iop_ypack_raw_struct(desc, value, env, indent,
                                            to_indent));
    }

    if (res == type_header_len) {
        if (to_indent) {
            PUTS(" ~");
        } else {
            PUTS("~");
        }
    }

    return res;
}

#undef WRITE
#undef PUTS
#undef INDENT
#undef PUTU
#undef PUTI

int iop_ypack_with_flags(const iop_struct_t *desc, const void *value,
                         iop_jpack_writecb_f *writecb, void *priv,
                         unsigned flags)
{
    const iop_ypack_env_t env = {
        .write_cb = writecb,
        .priv = priv,
        .flags = flags,
    };

    return iop_ypack_typed_struct(desc, value, &env, 0, false);
}

int iop_ypack(const iop_struct_t *desc, const void *value,
              iop_jpack_writecb_f *writecb, void *priv)
{
    /* Always skip everything that can be skipped */
    return iop_ypack_with_flags(desc, value, writecb, priv,
                                IOP_JPACK_SKIP_PRIVATE
                              | IOP_JPACK_SKIP_DEFAULT
                              | IOP_JPACK_SKIP_EMPTY_ARRAYS
                              | IOP_JPACK_SKIP_EMPTY_STRUCTS
                              | IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES);
}

typedef struct ypack_file_ctx_t {
    file_t *file;
    sb_t *err;
} ypack_file_ctx_t;

static int iop_ypack_write_file(void *priv, const void *data, int len)
{
    ypack_file_ctx_t *ctx = priv;

    if (file_write(ctx->file, data, len) < 0) {
        sb_addf(ctx->err, "cannot write in output file: %m");
        return -1;
    }

    return len;
}

int (iop_ypack_file)(const char *filename, unsigned file_flags,
                     mode_t file_mode, const iop_struct_t *st,
                     const void *value, sb_t *err)
{
    ypack_file_ctx_t ctx;
    int res;

    p_clear(&ctx, 1);
    ctx.file = file_open(filename, file_flags, file_mode);
    if (!ctx.file) {
        sb_setf(err, "cannot open output file `%s`: %m", filename);
        return -1;
    }
    ctx.err = err;

    res = iop_ypack(st, value, &iop_ypack_write_file, &ctx);
    if (res < 0) {
        IGNORE(file_close(&ctx.file));
        return res;
    }

    /* End the file with a newline, as the packing ends immediately after
     * the last value. */
    file_puts(ctx.file, "\n");

    if (file_close(&ctx.file) < 0) {
        sb_setf(err, "cannot close output file `%s`: %m", filename);
        return -1;
    }

    return 0;
}

/* }}} */
/* {{{ Module */

static int iop_yaml_initialize(void *arg)
{
    return 0;
}

static int iop_yaml_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(iop_yaml)
    /* There is an implicit dependency on "log" */
MODULE_END()

/* }}} */
