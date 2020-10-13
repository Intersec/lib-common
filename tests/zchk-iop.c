/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

/* LCOV_EXCL_START */

#include <math.h>

#include <lib-common/core.h>
#include <lib-common/datetime.h>
#include <lib-common/thr.h>
#include <lib-common/unix.h>
#include <lib-common/z.h>
#include <lib-common/iop-json.h>
#include <lib-common/iop/priv.h>
#include <lib-common/iop/ic.iop.h>
#include <lib-common/xmlr.h>

#include "zchk-iop.h"
#include "iop/tstiop.iop.h"
#include "iop/tstiop2.iop.h"
#include "iop/tstiop_inheritance.iop.h"
#include "iop/tstiop_backward_compat.iop.h"
#include "iop/tstiop_backward_compat_deleted_struct_1.iop.h"
#include "iop/tstiop_backward_compat_deleted_struct_2.iop.h"
#include "iop/tstiop_backward_compat_incompatible_struct_1.iop.h"
#include "iop/tstiop_backward_compat_incompatible_struct_2.iop.h"
#include "iop/tstiop_backward_compat_iface.iop.h"
#include "iop/tstiop_backward_compat_iface_deleted.iop.h"
#include "iop/tstiop_backward_compat_iface_deleted_rpc.iop.h"
#include "iop/tstiop_backward_compat_iface_deleted_rpc_ignored.iop.h"
#include "iop/tstiop_backward_compat_iface_deleted_rpc_ignored_bin.iop.h"
#include "iop/tstiop_backward_compat_iface_deleted_rpc_ignored_json.iop.h"
#include "iop/tstiop_backward_compat_iface_incompatible_rpc.iop.h"
#include "iop/tstiop_backward_compat_iface_incompatible_rpc_ignored.iop.h"
#include "iop/tstiop_backward_compat_iface_incompatible_rpc_ignored_binjson.iop.h"
#include "iop/tstiop_backward_compat_mod.iop.h"
#include "iop/tstiop_backward_compat_mod_deleted.iop.h"
#include "iop/tstiop_backward_compat_mod_deleted_if.iop.h"
#include "iop/tstiop_bpack_unregistered_class.iop.h"
#include "iop/tstiop_void_type.iop.h"
#include "iop/tstiop_wsdl.iop.h"
#include "zchk-iop-ressources.h"

qvector_t(my_struct_a, tstiop__my_struct_a__t);
qvector_t(my_struct_a_opt, tstiop__my_struct_a_opt__t);
qvector_t(my_struct_g, tstiop__my_struct_g__t);
qvector_t(my_struct_m, tstiop__my_struct_m__t);
qvector_t(my_class2, tstiop__my_class2__t *);
qvector_t(filtered_struct, tstiop__filtered_struct__t);
qvector_t(my_struct_f, tstiop__my_struct_f__t);

/* {{{ IOP testing helpers */

/* {{{ iop_get_field_values() */

static int
z_iop_get_field_values_check(const iop_struct_t *st_desc, const void *st_ptr,
                             const char *fpath, const void *exp_values,
                             int exp_len, bool exp_is_array_of_pointers)
{
    const iop_field_t *fdesc;
    const void *values;
    int len;
    bool is_array_of_pointers;

    fdesc = iop_get_field_const(st_ptr, st_desc, LSTR(fpath), NULL, NULL);
    Z_ASSERT_P(fdesc, "call to 'iop_get_field_const()' failed");
    iop_get_field_values_const(fdesc, st_ptr, &values, &len,
                               &is_array_of_pointers);
    Z_ASSERT(values == exp_values, "pointers differ, got %p, expected %p",
             values, exp_values);
    Z_ASSERT_EQ(len, exp_len, "lengths differ");
    Z_ASSERT_EQ(is_array_of_pointers, exp_is_array_of_pointers,
                "values differ for `is_array_of_pointers'");
    Z_HELPER_END;
}

/* }}} */
/* {{{ iop_value_get_bpack_size() */

static int
_z_check_iop_value_get_bpack_size(const tstiop__get_bpack_sz_u__t *u,
                                  const char *fname)
{
    size_t st_bpack_sz;
    size_t field_bpack_sz;
    qv_t(i32) szs;
    const iop_field_t *f;
    iop_value_t v;

    qv_inita(&szs, 1024);
    st_bpack_sz = iop_bpack_size(&tstiop__get_bpack_sz_u__s, u, &szs);
    qv_wipe(&szs);

    Z_ASSERT_N(iop_field_find_by_name(&tstiop__get_bpack_sz_u__s,
                                      LSTR(fname), NULL, &f),
               "field `%s' does not exist", fname);
    /* XXX The real tag binary packing length is 'tag_len' + 1. */
    field_bpack_sz = st_bpack_sz - f->tag_len - 1;

    Z_ASSERT_N(iop_value_from_field(u, f, &v), "cannot get value");
    Z_ASSERT_EQ(iop_value_get_bpack_size(&v, f->type, f->u1.st_desc),
                field_bpack_sz, "unexpected bpack size");
    Z_HELPER_END;
}

static int
z_check_iop_value_get_bpack_size(const tstiop__get_bpack_sz_u__t *u,
                                 const char *fname)
{
    Z_HELPER_RUN(_z_check_iop_value_get_bpack_size(u, fname),
                 "check failed for %*pS",
                 IOP_ST_FMT_ARG(tstiop__get_bpack_sz_u, u));
    Z_HELPER_END;
}

/* }}} */
/* {{{ zchk iop.dup_and_copy */

typedef enum z_test_dup_and_copy_flags_t {
    Z_TEST_DUP_AND_COPY_TEST_DUP = 1 << 0,
    Z_TEST_DUP_AND_COPY_USE_POOL = 1 << 1,
    Z_TEST_DUP_AND_COPY_GET_SIZE = 1 << 2,
    Z_TEST_DUP_AND_COPY_MULTIPLE_ALLOC = 1 << 3,
    Z_TEST_DUP_AND_COPY_SHALLOW = 1 << 4,
    Z_TEST_DUP_AND_COPY_NO_REALLOC = 1 << 5,

    Z_TEST_DUP_AND_COPY_END = 1 << 6,
} z_test_dup_and_copy_flags_t;

#define _F(_fl)  ((z_flags) & Z_TEST_DUP_AND_COPY_##_fl)

static int z_test_dup_or_copy(const iop_struct_t *st, const void *v,
                              size_t exp_size, unsigned z_flags)
{
    t_scope;
    void *res;
    size_t sz;
    mem_pool_t *mp = _F(USE_POOL) ? t_pool() : NULL;
    size_t *psz = _F(GET_SIZE) ? &sz : NULL;
    unsigned flags = 0;

    if (_F(MULTIPLE_ALLOC)) {
        if (!mp || psz) {
            /* Skip invalid case */
            return 0;
        }
        flags |= IOP_COPY_MULTIPLE_ALLOC;
    }

    if (_F(SHALLOW)) {
        flags |= IOP_COPY_SHALLOW;
    }

    if (_F(NO_REALLOC)) {
        if (psz || _F(TEST_DUP) || (!mp && !_F(SHALLOW))) {
            /* Skip invalid case */
            return 0;
        }
        flags |= IOP_COPY_NO_REALLOC;
    }

    if (_F(TEST_DUP)) {
        res = mp_iop_dup_desc_flags_sz(mp, st, v, flags, psz);
    } else {
        res = mp_iop_new_desc(mp, st);
        mp_iop_copy_desc_flags_sz(mp, st, &res, v, flags, psz);
    }
    Z_ASSERT_IOPEQUAL_DESC(st, res, v, "result differs from source");

    if (_F(SHALLOW)) {
        Z_ASSERT_EQ(memcmp(res, v, st->size), 0);
    } else {
        Z_ASSERT_NE(memcmp(res, v, st->size), 0);
        if (psz) {
            Z_ASSERT_EQ(*psz, exp_size, "size differs from expected");
        }
    }
    mp_delete(mp, &res);

    Z_HELPER_END;
}

static int
z_test_dup_and_copy(const iop_struct_t *st, const void *v)
{
    t_scope;
    size_t exp_size;

    Z_ASSERT_P(mp_iop_dup_desc_sz(t_pool(), st, v, &exp_size));

    for (unsigned z_flags = 0; z_flags < Z_TEST_DUP_AND_COPY_END; z_flags++) {
        Z_HELPER_RUN(z_test_dup_or_copy(st, v, exp_size, z_flags),
                     "%s test failed (use_pool=%s, get_size=%s, shallow=%s, "
                     "multiple_alloc=%s)",
                     _F(TEST_DUP) ? "duplication" : "copy",
                     _F(USE_POOL) ? "true" : "false",
                     _F(GET_SIZE) ? "true" : "false",
                     _F(MULTIPLE_ALLOC) ? "true" : "false",
                     _F(SHALLOW)  ? "true" : "false");
    }

    Z_HELPER_END;
}

static int z_test_macros_dup_copy_eq(const tstiop__full_struct__t *v,
                                     const tstiop__full_struct__t *out,
                                     bool memcmp_eq)
{
    Z_ASSERT_IOPEQUAL(tstiop__full_struct, out, v);
    Z_ASSERT_EQ(memcmp(v, out, sizeof(tstiop__full_struct__t)) == 0,
                memcmp_eq);
    Z_HELPER_END;
}

static int z_test_macros_dup_copy(const tstiop__full_struct__t *v)
{
    t_scope;
    const void *frame = r_newframe();
    const unsigned flags = IOP_COPY_SHALLOW;
    size_t sz;
    tstiop__full_struct__t *out;

    /* dup */

    sz = 0;
    out = mp_iop_dup_desc_sz(t_pool(), &tstiop__full_struct__s, v, &sz);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));
    Z_ASSERT_NE(sz, (size_t)0);

    /* iop_dup_flags */
    sz = 0;
    out = mp_iop_dup_flags_sz(t_pool(), tstiop__full_struct, v, flags, &sz);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));
    Z_ASSERT_NE(sz, (size_t)0);

    out = mp_iop_dup_flags(t_pool(), tstiop__full_struct, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    out = iop_dup_flags(tstiop__full_struct, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));
    p_delete(&out);

    out = t_iop_dup_flags(tstiop__full_struct, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    out = r_iop_dup_flags(tstiop__full_struct, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    /* iop_dup */
    sz = 0;
    out = mp_iop_dup_sz(t_pool(), tstiop__full_struct, v, &sz);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));
    Z_ASSERT_NE(sz, (size_t)0);

    out = mp_iop_dup(t_pool(), tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    out = iop_dup(tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));
    p_delete(&out);

    out = t_iop_dup(tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    out = r_iop_dup(tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    /* iop_shallow_dup */
    out = mp_iop_shallow_dup(t_pool(), tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    out = iop_shallow_dup(tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));
    p_delete(&out);

    out = t_iop_shallow_dup(tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    out = r_iop_shallow_dup(tstiop__full_struct, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));


    /* copy */

    out = NULL;
    sz = 0;
    mp_iop_copy_desc_sz(t_pool(), &tstiop__full_struct__s, (void **)&out, v,
                        &sz);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));
    Z_ASSERT_NE(sz, (size_t)0);

    /* iop_copy_flags */
    out = NULL;
    sz = 0;
    mp_iop_copy_flags_sz(t_pool(), tstiop__full_struct, &out, v, flags, &sz);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));
    Z_ASSERT_NE(sz, (size_t)0);

    out = NULL;
    mp_iop_copy_flags(t_pool(), tstiop__full_struct, &out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    out = NULL;
    iop_copy_flags(tstiop__full_struct, &out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));
    p_delete(&out);

    out = NULL;
    t_iop_copy_flags(tstiop__full_struct, &out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    out = NULL;
    r_iop_copy_flags(tstiop__full_struct, &out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    /* iop_copy */
    out = NULL;
    sz = 0;
    mp_iop_copy_sz(t_pool(), tstiop__full_struct, &out, v, &sz);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));
    Z_ASSERT_NE(sz, (size_t)0);

    out = NULL;
    mp_iop_copy(t_pool(), tstiop__full_struct, &out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    out = NULL;
    iop_copy(tstiop__full_struct, &out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));
    p_delete(&out);

    out = NULL;
    t_iop_copy(tstiop__full_struct, &out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    out = NULL;
    r_iop_copy(tstiop__full_struct, &out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    /* iop_copy_v_flags */
    out = t_iop_new(tstiop__full_struct);
    mp_iop_copy_v_flags(t_pool(), tstiop__full_struct, out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    iop_init(tstiop__full_struct, out);
    t_iop_copy_v_flags(tstiop__full_struct, out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    iop_init(tstiop__full_struct, out);
    r_iop_copy_v_flags(tstiop__full_struct, out, v, flags);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));

    /* iop_copy_v */
    mp_iop_copy_v(t_pool(), tstiop__full_struct, out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    iop_init(tstiop__full_struct, out);
    t_iop_copy_v(tstiop__full_struct, out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));

    iop_init(tstiop__full_struct, out);
    r_iop_copy_v(tstiop__full_struct, out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, false));


    /* iop_shallow_copy_v */
    iop_init(tstiop__full_struct, out);
    iop_shallow_copy_v(tstiop__full_struct, out, v);
    Z_HELPER_RUN(z_test_macros_dup_copy_eq(v, out, true));


    r_release(frame);
    Z_HELPER_END;
}

#undef _F

/* }}} */
/* {{{ zchk iop.equals_and_cmp */

static int z_assert_iop_gt_desc(const struct iop_struct_t *st,
                                const void *s1, const void *s2)
{
    Z_ASSERT(!iop_equals_desc(st, s1, s2));
    Z_ASSERT_GT(iop_cmp_desc(st, s1, s2), 0);
    Z_HELPER_END;
}

static int z_assert_iop_lt_desc(const struct iop_struct_t *st,
                                const void *s1, const void *s2)
{
    Z_ASSERT(!iop_equals_desc(st, s1, s2));
    Z_ASSERT_LT(iop_cmp_desc(st, s1, s2), 0);
    Z_HELPER_END;
}

static int z_assert_iop_eq_desc(const struct iop_struct_t *st,
                                const void *s1, const void *s2)
{
    Z_ASSERT_IOPEQUAL_DESC(st, s1, s2);
    Z_ASSERT_ZERO(iop_cmp_desc(st, s1, s2));
    Z_HELPER_END;
}

/* }}} */
/* {{{ zchk iop.iop_field_path_compile */

static int _z_check_field_path_compile(
    const iop_struct_t *st, lstr_t path, const void *nullable value,
    iop_type_t exp_type, bool exp_is_array,
    const iop_struct_t *nullable exp_st, const iop_enum_t *nullable exp_en,
    lstr_t exp_error)
{
    t_scope;
    SB_1k(err);
    iop_full_type_t type;
    bool is_array;
    int res;

    if (value) {
        res = iop_obj_get_field_type(st, value, path, &type, &is_array, &err);
    } else {
        const iop_field_path_t *fp = NULL;

        fp = t_iop_field_path_compile(st, path, &err);
        if (exp_error.s) {
            res = fp ? 0 : -1;
        } else {
            Z_ASSERT_P(fp, "%pL", &err);
            iop_field_path_get_type(fp, &type, &is_array);
        }
    }

    if (exp_error.s) {
        Z_ASSERT_NEG(res, "unexpected success");
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&err), exp_error);
    } else {
        Z_ASSERT_EQ(type.type, exp_type);
        Z_ASSERT_EQ(is_array, exp_is_array);
        if (exp_st) {
            Z_ASSERT(!iop_type_is_scalar(exp_type), "broken test");
            Z_ASSERT(type.st == exp_st, "unexpected struct type: %pL != %pL",
                     &type.st->fullname, &exp_st->fullname);
        }
        if (exp_en) {
            Z_ASSERT(exp_type == IOP_T_ENUM, "broken test");
            Z_ASSERT(type.en == exp_en, "unexpected enum type: %pL != %pL",
                     &type.en->name, &exp_en->name);
        }
    }

    Z_HELPER_END;
}

static int z_check_field_path_compile(
    const iop_struct_t *st, lstr_t path, const void *nullable value,
    iop_type_t exp_type, bool exp_is_array,
    const iop_struct_t *nullable exp_st, const iop_enum_t *nullable exp_en,
    lstr_t exp_error)
{
    Z_HELPER_RUN(_z_check_field_path_compile(st, path, value, exp_type,
                                             exp_is_array, exp_st, exp_en,
                                             exp_error),
                 "%pL:%pL", &st->fullname, &path);
    Z_HELPER_END;
}

/* }}} */
/* {{{ zchk iop.iop_filter* */

static void **t_z_create_values_ptr_from_values(void *values, int values_len,
                                                size_t value_size)
{
    void **values_ptrs = t_new_raw(void *, values_len);

    for (int i = 0; i < values_len; i++) {
        values_ptrs[i] = (uint8_t *)values + (i * value_size);
    }

    return values_ptrs;
}

static int z_iop_filter_check_results(const iop_struct_t *obj_st,
                                      void *tst_objs, int tst_objs_len,
                                      void *exp_objs, int exp_objs_len)
{
    bool is_pointer = iop_struct_is_class(obj_st);
    size_t obj_size = is_pointer ? sizeof(void *) : obj_st->size;

    Z_ASSERT_EQ(exp_objs_len, tst_objs_len);
    for (int i = 0; i < exp_objs_len; i++) {
        void *exp_obj = exp_objs;
        void *tst_obj = tst_objs;

        if (is_pointer) {
            exp_obj = *(void **)exp_obj;
            tst_obj = *(void **)tst_obj;
        }

        Z_ASSERT_IOPEQUAL_DESC(obj_st, exp_obj, tst_obj);

        exp_objs = (uint8_t *)exp_objs + obj_size;
        tst_objs = (uint8_t *)tst_objs + obj_size;
    }

    Z_HELPER_END;
}

static int z_iop_filter_check_filter(const char *field, unsigned flags,
                                     void *values, int values_len,
                                     size_t value_size,
                                     const iop_struct_t *obj_st,
                                     void *tst_objs, int tst_objs_len,
                                     void *exp_objs, int exp_objs_len)
{
    t_scope;
    SB_1k(err);
    void *values_ptrs;

    values_ptrs = t_z_create_values_ptr_from_values(values, values_len,
                                                    value_size);

    Z_ASSERT_N(iop_filter(obj_st, tst_objs, &tst_objs_len, LSTR(field),
                          values_ptrs, values_len, flags, &err),
               "%*pM", SB_FMT_ARG(&err));

    Z_HELPER_RUN(z_iop_filter_check_results(obj_st, tst_objs, tst_objs_len,
                                            exp_objs, exp_objs_len));
    Z_HELPER_END;
}

/* This macro is used to convert macro arguments in the form (a, b, ...) to an
 * array initializer list {a, b, ...}.
 * The array arguments passed to the Z_IOP_FILTER_* macros, e.g. _values_args,
 * are in the form (a, b, ...). So by doing `ARGS_TO_ARRAY _array_args`, this
 * will be expanded as `ARGS_TO_ARRAY (a, b, ...)`, and thus will finally be
 * expanded as `{a, b, ...}`.
 */
#define ARGS_TO_ARRAY(...)  { __VA_ARGS__ }

#define Z_IOP_FILTER_CHECK_FILTER(_value_type, _obj_type, _obj_st,           \
                                  _tst_objs_args, _flags, _field,            \
                                  _values_args, _exp_objs_args)              \
    do {                                                                     \
        _value_type _values[] = ARGS_TO_ARRAY _values_args;                  \
        _obj_type _tst_objs[] = ARGS_TO_ARRAY _tst_objs_args;                \
        _obj_type _exp_objs[] = ARGS_TO_ARRAY _exp_objs_args;                \
                                                                             \
        Z_HELPER_RUN(z_iop_filter_check_filter(                              \
            _field, _flags, _values, countof(_values), sizeof(_value_type),  \
            _obj_st, _tst_objs, countof(_tst_objs), _exp_objs,               \
            countof(_exp_objs)));                                            \
    } while (0)

static int t_z_iop_filter_add_bitmap(const char *field, unsigned flags,
                                     iop_filter_bitmap_op_t op,
                                     void *values, int values_len,
                                     size_t value_size,
                                     const iop_struct_t *obj_st,
                                     void *tst_objs, int tst_objs_len,
                                     byte **bitmap)
{
    SB_1k(err);
    void *values_ptrs;

    values_ptrs = t_z_create_values_ptr_from_values(values, values_len,
                                                    value_size);

    Z_ASSERT_N(t_iop_filter_bitmap(obj_st, tst_objs, tst_objs_len,
                                   LSTR(field), values_ptrs, values_len,
                                   flags, op, bitmap, &err),
               "%*pM", SB_FMT_ARG(&err));

    Z_HELPER_END;
}

#define T_Z_IOP_FILTER_ADD_BITMAP(_value_type, _obj_type, _obj_st,           \
                                  _tst_objs_args, _flags, _field, _op,       \
                                  _values_args, _bitmap)                     \
    do {                                                                     \
        _value_type _values[] = ARGS_TO_ARRAY _values_args;                  \
        _obj_type _tst_objs[] = ARGS_TO_ARRAY _tst_objs_args;                \
                                                                             \
        Z_HELPER_RUN(t_z_iop_filter_add_bitmap(                              \
            _field, _flags, _op, _values, countof(_values),                  \
            sizeof(_value_type), _obj_st, _tst_objs, countof(_tst_objs),     \
            _bitmap));                                                       \
    } while (0)

static int z_iop_filter_apply_bitmap(byte *bitmap,
                                     const iop_struct_t *obj_st,
                                     void *tst_objs, int tst_objs_len,
                                     void *exp_objs, int exp_objs_len)
{
    iop_filter_bitmap_apply(obj_st, tst_objs, &tst_objs_len, bitmap);
    Z_HELPER_RUN(z_iop_filter_check_results(obj_st, tst_objs, tst_objs_len,
                                            exp_objs, exp_objs_len));
    Z_HELPER_END;
}

#define Z_IOP_FILTER_APPLY_BITMAP(_obj_type, _obj_st, _tst_objs_args,        \
                                  _exp_objs_args, _bitmap)                   \
    do {                                                                     \
        _obj_type _tst_objs[] = ARGS_TO_ARRAY _tst_objs_args;                \
        _obj_type _exp_objs[] = ARGS_TO_ARRAY _exp_objs_args;                \
                                                                             \
        Z_HELPER_RUN(z_iop_filter_apply_bitmap(_bitmap, _obj_st, _tst_objs,  \
                                               countof(_tst_objs), _exp_objs,\
                                               countof(_exp_objs)));         \
    } while (0)

static int z_iop_filter_check_opt(const char *field, bool must_be_set,
                                  const iop_struct_t *obj_st, void *tst_objs,
                                  int tst_objs_len, void *exp_objs,
                                  int exp_objs_len)
{
    SB_1k(err);

    Z_ASSERT_N(iop_filter_opt(obj_st, tst_objs, &tst_objs_len, LSTR(field),
                              must_be_set, &err), "%*pM", SB_FMT_ARG(&err));
    Z_HELPER_RUN(z_iop_filter_check_results(obj_st, tst_objs, tst_objs_len,
                                            exp_objs, exp_objs_len));
    Z_HELPER_END;
}

#define Z_IOP_FILTER_CHECK_OPT(_obj_type, _obj_st, _tst_objs_args, _field,   \
                               _must_be_set, _exp_objs_args)                 \
    do {                                                                     \
        _obj_type _tst_objs[] = ARGS_TO_ARRAY _tst_objs_args;                \
        _obj_type _exp_objs[] = ARGS_TO_ARRAY _exp_objs_args;                \
                                                                             \
        Z_HELPER_RUN(z_iop_filter_check_opt(                                 \
            _field, _must_be_set, _obj_st, _tst_objs, countof(_tst_objs),    \
            _exp_objs, countof(_exp_objs)));                                 \
    } while (0)

/* }}} */
/* {{{ Other helpers (waiting proper folds). */

static int iop_xml_test_struct(const iop_struct_t *st, void *v,
                               const char *info)
{
    t_scope;
    int len;
    lstr_t s;
    uint8_t buf1[20], buf2[20];
    void *res = NULL;
    int ret;
    sb_t sb;

    /* XXX: Use a small t_sb here to force a realloc during (un)packing and
     *      detect possible illegal usage of the t_pool in the (un)packing
     *      functions. */
    t_sb_init(&sb, 100);

    sb_adds(&sb, "<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"");
    if (iop_struct_is_class(st)) {
        const iop_struct_t *real_st = *(const iop_struct_t **)v;

        sb_addf(&sb, " xsi:type=\"tns:%*pM\"",
                LSTR_FMT_ARG(real_st->fullname));
    }
    sb_addc(&sb, '>');
    len = sb.len;
    iop_xpack(&sb, st, v, false, true);
    sb_adds(&sb, "</root>");

    s = t_lstr_dups(sb.data + len, sb.len - len - 7);

    /* unpacking */
    Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
    ret = iop_xunpack_ptr(xmlr_g, t_pool(), st, &res);
    Z_ASSERT_N(ret, "XML unpacking failure (%s, %s): %s", st->fullname.s,
               info, xmlr_get_err());

    /* pack again ! */
    t_sb_init(&sb, 10);
    iop_xpack(&sb, st, res, false, true);

    /* check packing equality */
    Z_ASSERT_LSTREQUAL(s, LSTR_SB_V(&sb),
                       "XML packing/unpacking doesn't match! (%s, %s)",
                       st->fullname.s, info);

    /* In case of, check hashes equality */
    iop_hash_sha1(st, v,   buf1, 0);
    iop_hash_sha1(st, res, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "XML packing/unpacking hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    xmlr_close(&xmlr_g);
    Z_HELPER_END;
}

static int iop_xml_test_struct_invalid(const iop_struct_t *st, void *v,
                                       const char *info)
{
    t_scope;
    void *res = NULL;
    sb_t sb;

    /* XXX: Use a small t_sb here to force a realloc during (un)packing and
     *      detect possible illegal usage of the t_pool in the (un)packing
     *      functions. */
    t_sb_init(&sb, 100);

    sb_adds(&sb, "<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"");
    if (iop_struct_is_class(st)) {
        const iop_struct_t *real_st = *(const iop_struct_t **)v;

        sb_addf(&sb, " xsi:type=\"tns:%*pM\"",
                LSTR_FMT_ARG(real_st->fullname));
    }
    sb_addc(&sb, '>');
    iop_xpack(&sb, st, v, false, true);
    sb_adds(&sb, "</root>");

    /* unpacking */
    Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
    Z_ASSERT_NEG(iop_xunpack_ptr(xmlr_g, t_pool(), st, &res),
                 "XML unpacking unexpected success (%s, %s)", st->fullname.s,
                 info);

    xmlr_close(&xmlr_g);
    Z_HELPER_END;
}

static int iop_json_test_struct(const iop_struct_t *st, void *v,
                                const char *info)
{
    iop_json_lex_t jll;
    pstream_t ps;
    int strict = 0;
    uint8_t buf1[20], buf2[20];

    iop_jlex_init(t_pool(), &jll);
    jll.flags = IOP_UNPACK_IGNORE_UNKNOWN;

    while (strict < 3) {
        t_scope;
        sb_t sb;
        void *res = NULL;
        int ret;

        /* XXX: Use a small t_sb here to force a realloc during (un)packing
         *      and detect possible illegal usage of the t_pool in the
         *      (un)packing functions. */
        t_sb_init(&sb, 10);

        /* packing */
        Z_ASSERT_N(iop_jpack(st, v, iop_sb_write, &sb, strict),
                   "JSon packing failure! (%s, %s)", st->fullname.s, info);

        /* unpacking */
        ps = ps_initsb(&sb);
        iop_jlex_attach(&jll, &ps);
        if ((ret = iop_junpack_ptr(&jll, st, &res, true)) < 0) {
            t_sb_init(&sb, 10);
            iop_jlex_write_error(&jll, &sb);
        }
        Z_ASSERT_N(ret, "JSon unpacking error (%s, %s): %s", st->fullname.s,
                   info, sb.data);
        iop_jlex_detach(&jll);

        /* check hashes equality */
        iop_hash_sha1(st, v,   buf1, 0);
        iop_hash_sha1(st, res, buf2, 0);
        Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                       "JSON %spacking/unpacking hashes don't match! (%s, %s)",
                       (strict ? "strict " : ""),
                       st->fullname.s, info);

        strict++;
    }

    iop_jlex_wipe(&jll);

    Z_HELPER_END;
}

static int iop_json_test_struct_invalid(const iop_struct_t *st, void *v,
                                        const char *info)
{
    iop_json_lex_t jll;
    pstream_t ps;
    int strict = 0;

    iop_jlex_init(t_pool(), &jll);
    jll.flags = IOP_UNPACK_IGNORE_UNKNOWN;

    while (strict < 3) {
        t_scope;
        sb_t sb;
        void *res = NULL;
        int ret;

        /* XXX: Use a small t_sb here to force a realloc during (un)packing
         *      and detect possible illegal usage of the t_pool in the
         *      (un)packing functions. */
        t_sb_init(&sb, 10);

        /* packing */
        Z_ASSERT_N(iop_jpack(st, v, iop_sb_write, &sb, strict),
                   "JSon packing failure! (%s, %s)", st->fullname.s, info);

        /* unpacking */
        ps = ps_initsb(&sb);
        iop_jlex_attach(&jll, &ps);
        ret = iop_junpack_ptr(&jll, st, &res, true);
        Z_ASSERT_NEG(ret, "JSon unpacking unexpected success (%s, %s)",
                     st->fullname.s, info);
        iop_jlex_detach(&jll);

        strict++;
    }

    iop_jlex_wipe(&jll);

    Z_HELPER_END;
}


static int iop_json_test_json(const iop_struct_t *st, const char *json,
                              const void *expected, const char *info)
{
    t_scope;
    const char *path;
    iop_json_lex_t jll;
    pstream_t ps;
    void *res = NULL;
    int ret;
    uint8_t buf1[20], buf2[20];
    sb_t sb;

    /* XXX: Use a small t_sb here to force a realloc during (un)packing and
     *      detect possible illegal usage of the t_pool in the (un)packing
     *      functions. */
    t_sb_init(&sb, 10);

    iop_jlex_init(t_pool(), &jll);
    jll.flags = IOP_UNPACK_IGNORE_UNKNOWN;

    ps = ps_initstr(json);
    iop_jlex_attach(&jll, &ps);
    if ((ret = iop_junpack_ptr(&jll, st, &res, true)) < 0)
        iop_jlex_write_error(&jll, &sb);
    Z_ASSERT_N(ret, "JSon unpacking error (%s, %s): %s", st->fullname.s, info,
               sb.data);
    iop_jlex_detach(&jll);

    /* visualize result */
    if (e_is_traced(1))
        iop_jtrace_(1, __FILE__, __LINE__, __func__, NULL, st, res);

    /* check hashes equality */
    iop_hash_sha1(st, res,      buf1, 0);
    iop_hash_sha1(st, expected, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "JSON unpacking hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    iop_jlex_wipe(&jll);

    /* Test iop_jpack_file / t_iop_junpack_file */
    path = t_fmt("%*pM/tstjson.json", LSTR_FMT_ARG(z_tmpdir_g));
    sb_reset(&sb);
    Z_ASSERT_N(iop_jpack_file(path, st, res, 0, &sb),
               "%*pM", SB_FMT_ARG(&sb));
    Z_ASSERT_N(t_iop_junpack_ptr_file(path, st, &res, 0, NULL, &sb),
               "%*pM", SB_FMT_ARG(&sb));
    Z_ASSERT_IOPEQUAL_DESC(st, res, expected);

    Z_HELPER_END;
}

static int iop_json_test_unpack(const iop_struct_t *st, const char *json,
                                int flags, bool valid, const char *info)
{
    t_scope;
    iop_json_lex_t jll;
    pstream_t ps;
    void *res = NULL;
    int ret;
    sb_t sb;

    /* XXX: Use a small t_sb here to force a realloc during (un)packing and
     *      detect possible illegal usage of the t_pool in the (un)packing
     *      functions. */
    t_sb_init(&sb, 10);

    iop_jlex_init(t_pool(), &jll);
    jll.flags = flags;

    ps = ps_initstr(json);
    iop_jlex_attach(&jll, &ps);

    if ((ret = iop_junpack_ptr(&jll, st, &res, true)) < 0)
        iop_jlex_write_error(&jll, &sb);
    if (valid) {
        Z_ASSERT_N(ret, "JSon unpacking error (%s, %s): %s", st->fullname.s,
                   info, sb.data);
    } else {
        Z_ASSERT_NEG(ret, "JSon unpacking unexpected success (%s, %s)",
                     st->fullname.s, info);
    }
    iop_jlex_detach(&jll);

    iop_jlex_wipe(&jll);

    Z_HELPER_END;
}

static int iop_json_test_pack(const iop_struct_t *st, const void *value,
                              unsigned flags, bool test_unpack,
                              bool must_be_equal, const char *expected)
{
    t_scope;
    t_SB_1k(sb);
    void *unpacked = NULL;

    Z_ASSERT_N(iop_sb_jpack(&sb, st, value, flags));
    Z_ASSERT_STREQUAL(sb.data, expected);

    if (test_unpack) {
        pstream_t ps = ps_initsb(&sb);

        Z_ASSERT_N(t_iop_junpack_ptr_ps(&ps, st, &unpacked, 0, NULL));
        if (must_be_equal) {
            Z_ASSERT(iop_equals_desc(st, value, unpacked));
        }
    }

    Z_HELPER_END;
}

static void iop_std_test_speed(const iop_struct_t *st, void *v, int iter,
                               const unsigned flags, const char *info)
{
    proctimer_t pt;
    int elapsed, elapsed2;
    qv_t(i32) szs;
    int len;
    byte *dst;

    proctimer_start(&pt);
    for (int i = 0; i < iter; i++) {
        t_scope;

        t_qv_init(&szs, 2);
        len = iop_bpack_size_flags(st, v, flags, &szs);
        dst = t_new(byte, len);
        iop_bpack(dst, st, v, szs.tab);
    }
    elapsed = proctimer_stop(&pt);
    e_named_trace(1, "iop_speed", "pack monothread: %i", elapsed);

    module_require(MODULE(thr), NULL);
    iop_bpack_set_threaded_threshold(2);
    proctimer_start(&pt);
    for (int i = 0; i < iter; i++) {
        t_scope;

        t_qv_init(&szs, 2);
        len = iop_bpack_size_flags(st, v, flags, &szs);
        dst = t_new(byte, len);
        iop_bpack(dst, st, v, szs.tab);
    }
    elapsed2 = proctimer_stop(&pt);
    module_release(MODULE(thr));
    e_named_trace(1, "iop_speed", "pack multithread: %i", elapsed2);
    e_named_trace(1, "iop_speed", "multithread improvement: x%f",
                  (float)elapsed / elapsed2);
}

static int iop_std_test_struct_flags(const iop_struct_t *st, void *v,
                                     const unsigned flags, const char *info)
{
    t_scope;
    int ret;
    void *res = NULL;
    uint8_t buf1[20], buf2[20];
    qv_t(i32) szs, szs2;
    int len, len2;
    byte *dst, *dst2;

    /* XXX: Use a small t_qv here to force a realloc during (un)packing and
     *      detect possible illegal usage of the t_pool in the (un)packing
     *      functions. */
    t_qv_init(&szs, 2);

    /* packing */
    Z_ASSERT_N((len = iop_bpack_size_flags(st, v, flags, &szs)),
               "invalid structure size (%s, %s)", st->fullname.s, info);
    dst = t_new(byte, len);
    iop_bpack(dst, st, v, szs.tab);

    /* packing with strict flag should give the same result */
    Z_ASSERT_LSTREQUAL(t_iop_bpack_struct_flags(st, v, flags |
                                                IOP_BPACK_STRICT),
                       LSTR_INIT_V((const char *)dst, len));

    /* packing in threaded mode should work */
    module_require(MODULE(thr), NULL);
    iop_bpack_set_threaded_threshold(2);
    t_qv_init(&szs2, 2);
    len2 = iop_bpack_size_flags(st, v, flags, &szs2);
    Z_ASSERT_EQ(len, len2);
    Z_ASSERT_LE(szs.len, szs2.len);
    dst2 = t_new(byte, len2);
    iop_bpack(dst2, st, v, szs2.tab);
    Z_ASSERT_LSTREQUAL(LSTR_INIT_V((const char *)dst, len),
                       LSTR_INIT_V((const char *)dst2, len2));

    /* test flag to force monothread */
    t_qv_init(&szs2, 2);
    len2 =  iop_bpack_size_flags(st, v, flags | IOP_BPACK_MONOTHREAD, &szs2);
    Z_ASSERT_EQ(len, len2);
    Z_ASSERT_EQ(szs.len, szs2.len);
    dst2 = t_new(byte, len2);
    iop_bpack(dst2, st, v, szs2.tab);
    Z_ASSERT_LSTREQUAL(LSTR_INIT_V((const char *)dst, len),
                       LSTR_INIT_V((const char *)dst2, len2));
    module_release(MODULE(thr));

    /* unpacking */
    ret = iop_bunpack_ptr(t_pool(), st, &res, ps_init(dst, len), false);
    Z_ASSERT_N(ret, "IOP unpacking error (%s, %s, %s)",
               st->fullname.s, info, iop_get_err());

    /* check hashes equality */
    iop_hash_sha1(st, v,   buf1, 0);
    iop_hash_sha1(st, res, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "IOP packing/unpacking hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    /* check equality */
    Z_ASSERT_IOPEQUAL_DESC(st, v, res);

    /* test duplication */
    Z_ASSERT_NULL(mp_iop_dup_desc_sz(NULL, st, NULL, NULL));
    Z_ASSERT_P(res = mp_iop_dup_desc_sz(t_pool(), st, v, NULL),
               "IOP duplication error! (%s, %s)", st->fullname.s, info);

    /* check equality */
    Z_ASSERT_IOPEQUAL_DESC(st, v, res);

    /* check hashes equality */
    iop_hash_sha1(st, res, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "IOP duplication hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    /* test copy */
    mp_iop_copy_desc_sz(t_pool(), st, (void **)&res, NULL, NULL);
    Z_ASSERT_NULL(res);
    mp_iop_copy_desc_sz(t_pool(), st, (void **)&res, v, NULL);

    /* check equality */
    Z_ASSERT_IOPEQUAL_DESC(st, v, res);

    /* check hashes equality */
    iop_hash_sha1(st, res, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "IOP copy hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    Z_HELPER_END;
}
#define iop_std_test_struct(st, v, info) \
    iop_std_test_struct_flags(st, v, 0, info)

static int iop_std_test_struct_invalid(const iop_struct_t *st, void *v,
                                       const char *info, const char *err)
{
    t_scope;
    void *res = NULL;
    qv_t(i32) szs;
    int len, ret;
    byte *dst;

    /* packing with strict flag should fail */
    Z_ASSERT_LSTREQUAL(t_iop_bpack_struct_flags(st, v, IOP_BPACK_STRICT),
                       LSTR_NULL_V);
    Z_ASSERT_STREQUAL(iop_get_err(), err);

    /* XXX: Use a small t_qv here to force a realloc during (un)packing and
     *      detect possible illegal usage of the t_pool in the (un)packing
     *      functions. */
    t_qv_init(&szs, 2);

    /* here packing will work... */
    Z_ASSERT_N((len = iop_bpack_size(st, v, &szs)),
               "invalid structure size (%s, %s)", st->fullname.s, info);
    dst = t_new(byte, len);
    iop_bpack(dst, st, v, szs.tab);

    /* and unpacking should fail */
    ret = iop_bunpack_ptr(t_pool(), st, &res, ps_init(dst, len), false);
    Z_ASSERT_NEG(ret, "IOP unpacking unexpected success (%s, %s)",
                 st->fullname.s, info);
    Z_ASSERT_STREQUAL(iop_get_err(), err);

    Z_HELPER_END;
}

static int iop_check_retro_compat_roptimized(lstr_t path)
{
    t_scope;
    SB_1k(err);
    tstiop__repeated__t sr;
    const iop_struct_t *st;

    int8_t   *i8;
    uint8_t  *u8;
    bool     *b;
    int16_t  *i16;
    uint16_t *u16;
    int32_t  *i32;

    lstr_t s[] = {
        LSTR_IMMED("foo"),
        LSTR_IMMED("bar"),
        LSTR_IMMED("foobar"),
    };

    iop_dso_t *dso;
    unsigned seed = (unsigned)time(NULL);

    dso = iop_dso_open(path.s, LM_ID_BASE, &err);
    Z_ASSERT_P(dso, "unable to load zchk-tstiop-plugin: %*pM",
               SB_FMT_ARG(&err));

    Z_ASSERT_P(st = iop_dso_find_type(dso, LSTR("tstiop.Repeated")));

    /* initialize my arrays */
    {
        const int sz = 256;

        i8  = t_new_raw(int8_t, sz);
        u8  = t_new_raw(uint8_t, sz);
        i16 = t_new_raw(int16_t, sz);
        u16 = t_new_raw(uint16_t, sz);
        b   = t_new_raw(bool, sz);
        i32 = t_new_raw(int32_t, sz);

        for (int i = 0; i < sz; i++) {
            i8[i]  = (int8_t)i;
            u8[i]  = (uint8_t)i;
            i16[i] = (int16_t)i;
            u16[i] = (uint16_t)i;
            b[i]   = (bool)i;
            i32[i] = i;
        }
    }

    /* do some tests… */
#define SET(dst, f, _len)  ({ dst.f.tab = f; dst.f.len = (_len); })
#define SET_RAND(dst, f)   ({ dst.f.tab = f; dst.f.len = (rand() % 256); })
    iop_init_desc(st, &sr);
    SET(sr, i8, 13);
    Z_HELPER_RUN(iop_std_test_struct(st, &sr,  "sr1"));

    iop_init_desc(st, &sr);
    SET(sr, i8, 13);
    SET(sr, i32, 4);
    Z_HELPER_RUN(iop_std_test_struct(st, &sr,  "sr2"));

    srand(seed);
    e_trace(1, "rand seed: %u", seed);
    for (int i = 0; i < 256; i++ ) {
        iop_init_desc(st, &sr);
        SET_RAND(sr, i8);
        SET_RAND(sr, u8);
        SET_RAND(sr, i16);
        SET_RAND(sr, u16);
        SET_RAND(sr, b);
        SET_RAND(sr, i32);
        SET(sr, s, rand() % (countof(s) + 1));
        Z_HELPER_RUN(iop_std_test_struct(st, &sr,  "sr_rand"));
    }
    /* Check the retro-compatibility */
    {
        lstr_t file_map;
        pstream_t ps;

        /* map the file */
        path = t_lstr_cat(z_cmddir_g,
                          LSTR("samples/repeated.ibp"));
        Z_ASSERT_N(lstr_init_from_file(&file_map, path.s,
                                       PROT_READ, MAP_SHARED));

        /* check the data */
        ps = ps_initlstr(&file_map);
        while (ps_len(&ps) > 0) {
            t_scope;
            uint32_t dlen = 0;
            tstiop__repeated__t sr_res;

            Z_ASSERT_N(ps_get_cpu32(&ps, &dlen));
            Z_ASSERT(ps_has(&ps, dlen));

            iop_init_desc(st, &sr);
            Z_ASSERT_N(iop_bunpack(t_pool(), st, &sr_res,
                                   __ps_get_ps(&ps, dlen), false),
                       "IOP unpacking error (%s) at offset %zu",
                       st->fullname.s, ps.b - (byte *)file_map.data);
        }

        lstr_wipe(&file_map);
    }

    iop_dso_close(&dso);
#undef SET
#undef SET_RAND
    Z_HELPER_END;
}

static int iop_check_retro_compat_copy_inv_tab(lstr_t path)
{
    SB_1k(err);
    tstiop__my_struct_b__t sb, *sb_dup;
    iop_dso_t *dso;
    const iop_struct_t *st_sb;

    dso = iop_dso_open(path.s, LM_ID_BASE, &err);
    Z_ASSERT_P(dso, "unable to load zchk-tstiop-plugin: %*pM",
               SB_FMT_ARG(&err));

    Z_ASSERT_P(st_sb = iop_dso_find_type(dso, LSTR("tstiop.MyStructB")));

    iop_init_desc(st_sb, &sb);
    sb.b.tab = (void *)0x42;
    sb.b.len = 0;

    sb_dup = mp_iop_dup_desc_sz(NULL, st_sb, &sb, NULL);
    Z_ASSERT_NULL(sb_dup->b.tab);
    Z_ASSERT_ZERO(sb_dup->b.len);

    p_delete(&sb_dup);

    iop_dso_close(&dso);
    Z_HELPER_END;
}

typedef struct z_json_sub_file_t {
    const iop_struct_t *st; /* NULL for string fields. */
    const void         *val;
    const char         *path;
} z_json_sub_file_t;
qvector_t(z_json_sub_file, z_json_sub_file_t);

static int
iop_check_json_include_packing(const iop_struct_t *st, const void *val,
                               const qv_t(iop_json_subfile) *sub_files,
                               const qv_t(z_json_sub_file) *z_sub_files,
                               const char *exp_err)
{
    t_scope;
    static int packing_cnt;
    const char *dir;
    const char *path;
    SB_1k(err);
    int res;

    dir = t_fmt("%*pM/packing-%d", LSTR_FMT_ARG(z_tmpdir_g), packing_cnt++);
    mkdir_p(dir, 0755);

    /* Pack val in a file, using the sub_files. */
    path = t_fmt("%s/main.json", dir);

    res = __iop_jpack_file(path, FILE_WRONLY | FILE_CREATE | FILE_TRUNC,
                           0444, st, val, 0, sub_files, &err);

    if (exp_err) {
        Z_ASSERT_NEG(res);
        Z_ASSERT(strstr(err.data, exp_err), "unexpected error: %s", err.data);
        return 0;
    }

    Z_ASSERT_N(res, "%*pM", SB_FMT_ARG(&err));

#define CHECK_FILE(_st, _file, _exp)  \
    do {                                                                     \
        t_scope;                                                             \
        void *_val = NULL;                                                   \
                                                                             \
        path = t_fmt("%s/%s", dir, _file);                                   \
        Z_ASSERT_N(t_iop_junpack_ptr_file(path, _st, &_val, 0, NULL, &err),  \
                   "cannot unpack `%s`: %*pM", path, SB_FMT_ARG(&err));      \
        Z_ASSERT_IOPEQUAL_DESC(_st, _val, _exp);                             \
    } while (0)

    /* Check that main file can be unpacked, and that the result is equal to
     * the expected value. */
    CHECK_FILE(st, "main.json", val);

    /* Check sub-files. */
    tab_for_each_ptr(sub_file, z_sub_files) {
        if (sub_file->st) {
            CHECK_FILE(sub_file->st, sub_file->path, sub_file->val);
        } else {
            t_scope;
            const lstr_t *content = sub_file->val;
            lstr_t file_map;

            path = t_fmt("%s/%s", dir, sub_file->path);
            Z_ASSERT_N(lstr_init_from_file(&file_map, path, PROT_READ,
                                           MAP_SHARED));
            Z_ASSERT_LSTREQUAL(file_map, *content);
            lstr_wipe(&file_map);
        }
    }
#undef CHECK_FILE

    Z_HELPER_END;
}

static int
iop_check_struct_backward_compat(const iop_struct_t *st1,
                                 const iop_struct_t *st2,
                                 unsigned flags, const char *exp_err,
                                 const void *obj1)
{
    t_scope;
    SB_1k(err);
    const char *ctx;

    ctx = t_fmt("check_backward_compat from %*pM to %*pM",
                LSTR_FMT_ARG(st1->fullname), LSTR_FMT_ARG(st2->fullname));

    if (exp_err) {
        Z_ASSERT_NEG(iop_struct_check_backward_compat(st1, st2, flags, &err),
                     "%s should fail", ctx);
        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&err), LSTR(exp_err));
    } else {
        Z_ASSERT_N(iop_struct_check_backward_compat(st1, st2, flags, &err),
                   "unexpected failure of %s: %*pM", ctx, SB_FMT_ARG(&err));
    }

    if (!obj1) {
        return 0;
    }

    if (flags & IOP_COMPAT_BIN) {
        void *obj2 = NULL;
        lstr_t data = t_iop_bpack_struct(st1, obj1);

        if (exp_err) {
            Z_ASSERT_NEG(iop_bunpack_ptr(t_pool(), st2, &obj2,
                                         ps_initlstr(&data), false),
                         "bunpack should fail when testing %s", ctx);
        } else {
            Z_ASSERT_N(iop_bunpack_ptr(t_pool(), st2, (void **)&obj2,
                                       ps_initlstr(&data), false),
                       "unexpected bunpack failure when testing %s", ctx);
        }
    }

    if (flags & IOP_COMPAT_JSON) {
        SB_1k(data);
        void *obj2 = NULL;
        pstream_t ps;

        iop_sb_jpack(&data, st1, obj1, 0);
        ps = ps_initsb(&data);
        if (exp_err) {
            Z_ASSERT_NEG(t_iop_junpack_ptr_ps(&ps, st2, &obj2, 0, &err),
                         "junpack should fail when testing %s", ctx);
        } else {
            Z_ASSERT_N(t_iop_junpack_ptr_ps(&ps, st2, &obj2, 0, &err),
                       "unexpected junpack failure when testing %s: %*pM",
                       ctx, SB_FMT_ARG(&err));
        }
    }

    Z_HELPER_END;
}

#define _Z_DSO_OPEN(_dso_path, in_cmddir)                                    \
    ({                                                                       \
        t_scope;                                                             \
        SB_1k(_err);                                                         \
        lstr_t _path = LSTR(_dso_path);                                      \
        iop_dso_t *_dso;                                                     \
                                                                             \
        if (in_cmddir) {                                                     \
            _path = t_lstr_cat(z_cmddir_g, _path);                           \
        }                                                                    \
        _dso = iop_dso_open(_path.s, LM_ID_BASE, &_err);                     \
        if (_dso == NULL) {                                                  \
            Z_SKIP("unable to load `%s`, TOOLS repo? (%*pM)",                \
                   _path.s, SB_FMT_ARG(&_err));                              \
        }                                                                    \
        _dso;                                                                \
    })

#define Z_DSO_OPEN()  _Z_DSO_OPEN("iop/zchk-tstiop-plugin" SO_FILEEXT, true)

static int z_check_static_field_type(const iop_struct_t *st,
                                     lstr_t name, iop_type_t type,
                                     const char *type_name)
{
    const iop_static_field_t *static_field = NULL;

    Z_ASSERT(iop_struct_is_class(st));

    for (int i = 0; i < st->class_attrs->static_fields_len; i++) {
        const iop_static_field_t *sf = st->class_attrs->static_fields[i];

        if (lstr_equal(name, sf->name)) {
            static_field = sf;
            break;
        }
    }

    Z_ASSERT_P(static_field, "static field `%*pM` not found in class `%*pM`",
               LSTR_FMT_ARG(name), LSTR_FMT_ARG(st->fullname));
    Z_ASSERT_EQ((int)type, iop_class_static_field_type(st, static_field),
                "expected type `%s`", type_name);

    Z_HELPER_END;
}

/* }}} */

/* }}} */

Z_GROUP_EXPORT(iop)
{
    IOP_REGISTER_PACKAGES(&tstiop__pkg,
                          &tstiop_inheritance__pkg,
                          &tstiop_backward_compat__pkg);

    Z_TEST(dso_open, "test whether iop_dso_open works and loads stuff") { /* {{{ */
        t_scope;

        SB_1k(err);
        iop_dso_t *dso;
        const iop_struct_t *st;
        qv_t(cstr) ressources_str;
        qv_t(i32) ressources_int;
        lstr_t path = t_lstr_cat(z_cmddir_g,
                                 LSTR("zchk-iop-plugin"SO_FILEEXT));

        Z_ASSERT(dso = iop_dso_open(path.s, LM_ID_BASE, &err), "%*pM",
                 SB_FMT_ARG(&err));
        Z_ASSERT_N(qm_find(iop_struct, &dso->struct_h, &LSTR_IMMED_V("ic.Hdr")));

        Z_ASSERT_P(st = iop_dso_find_type(dso, LSTR("ic.SimpleHdr")));
        Z_ASSERT(st != &ic__simple_hdr__s);

        t_qv_init(&ressources_str, 0);
        iop_dso_for_each_ressource(dso, str, ressource) {
            qv_append(&ressources_str, *ressource);
        }
        Z_ASSERT_EQ(ressources_str.len, 2, "loading ressources failed");
        Z_ASSERT_ZERO(strcmp(ressources_str.tab[0], z_ressource_str_a));
        Z_ASSERT_ZERO(strcmp(ressources_str.tab[1], z_ressource_str_b));

        t_qv_init(&ressources_int, 0);
        iop_dso_for_each_ressource(dso, int, ressource) {
            qv_append(&ressources_int, *ressource);
        }
        Z_ASSERT_EQ(ressources_int.len, 2, "loading ressources failed");
        Z_ASSERT_EQ(ressources_int.tab[0], z_ressources_int_1);
        Z_ASSERT_EQ(ressources_int.tab[1], z_ressources_int_2);

        /* Test iop_dso_get_from_pkg */
        qm_for_each_pos(iop_pkg, pos, &dso->pkg_h) {
            const iop_pkg_t *pkg = dso->pkg_h.values[pos];

            Z_ASSERT(iop_dso_get_from_pkg(pkg) == dso);
        }

        /* Play with register/unregister */
        iop_dso_unregister(dso);
        iop_dso_unregister(dso);
        qm_for_each_pos(iop_pkg, pos, &dso->pkg_h) {
            Z_ASSERT_NULL(iop_dso_get_from_pkg(dso->pkg_h.values[pos]));
        }
        iop_dso_register(dso);
        iop_dso_register(dso);

        iop_dso_close(&dso);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(hash_sha1, "test whether iop_hash_sha1 is stable wrt ABI change") { /* {{{ */
        t_scope;

        int  i_10 = 10, i_11 = 11;
        long j_10 = 10;

        struct tstiop__hash_v1__t v1 = {
            .b  = OPT(true),
            .i  = IOP_ARRAY(&i_10, 1),
            .s  = LSTR_IMMED("foo bar baz"),
        };

        struct tstiop__hash_v2__t v2 = {
            .b  = OPT(true),
            .i  = IOP_ARRAY(&j_10, 1),
            .s  = LSTR_IMMED("foo bar baz"),
        };

        struct tstiop__hash_v1__t v1_not_same = {
            .b  = OPT(true),
            .i  = IOP_ARRAY(&i_11, 1),
            .s  = LSTR_IMMED("foo bar baz"),
        };

        const iop_struct_t *stv1;
        const iop_struct_t *stv2;

        iop_dso_t *dso;
        uint8_t buf1[20], buf2[20];

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(stv1 = iop_dso_find_type(dso, LSTR("tstiop.HashV1")));
        Z_ASSERT_P(stv2 = iop_dso_find_type(dso, LSTR("tstiop.HashV2")));

        iop_hash_sha1(stv1, &v1, buf1, 0);
        iop_hash_sha1(stv2, &v2, buf2, 0);
        Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2));

        iop_hash_sha1(stv1, &v1_not_same, buf2, 0);
        Z_ASSERT(memcmp(buf1, buf2, sizeof(buf1)) != 0);
        iop_dso_close(&dso);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(hash_sha1_class, "test whether iop_hash_sha1 takes the IOP_HASH_DONT_INCLUDE_CLASS_ID param into account") { /* {{{ */
        tstiop__my_class2__t cl2;
        tstiop__my_class2_bis__t cl2bis;
        tstiop__my_class2_after__t cl2after;
        uint8_t buf1[20], buf2[20];

        iop_init(tstiop__my_class2, &cl2);
        cl2.int1 = 1;
        cl2.int2 = 2;
        iop_init(tstiop__my_class2_bis, &cl2bis);
        cl2bis.int1 = 1;
        cl2bis.int2 = 2;

        Z_ASSERT(!iop_equals_desc(&tstiop__my_class1__s, &cl2, &cl2bis));

        /* test both classes hash are equal with
         * IOP_HASH_DONT_INCLUDE_CLASS_ID param */
        iop_hash_sha1(&tstiop__my_class1__s, &cl2, buf1,
                      IOP_HASH_DONT_INCLUDE_CLASS_ID);
        iop_hash_sha1(&tstiop__my_class1__s, &cl2bis, buf2,
                      IOP_HASH_DONT_INCLUDE_CLASS_ID);
        Z_ASSERT(memcmp(buf1, buf2, sizeof(buf1)) == 0);

        /* test both classes hash are different without
         * IOP_HASH_DONT_INCLUDE_CLASS_ID param */
        iop_hash_sha1(&tstiop__my_class1__s, &cl2, buf1, 0);
        iop_hash_sha1(&tstiop__my_class1__s, &cl2bis, buf2, 0);
        Z_ASSERT(memcmp(buf1, buf2, sizeof(buf1)) != 0);

        /* ensure that adding an empty class in the hierarchy (which is
         * backward compatible) does not change the hash, only the class_id
         * of the instance is considered. */
        iop_init(tstiop__my_class2_after, &cl2after);
        cl2after.int1 = 1;
        cl2after.int2 = 2;
        iop_hash_sha1(&tstiop__my_class1_after__s, &cl2after, buf2, 0);
        Z_ASSERT(memcmp(buf1, buf2, sizeof(buf1)) == 0);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(constant_folder, "test the IOP constant folder") { /* {{{ */
#define feed_num(_num)                                                  \
        Z_ASSERT_N(iop_cfolder_feed_number(&cfolder, _num, true),       \
                   "error when feeding %jd", (int64_t)_num)
#define feed_op(_op)                                                    \
        Z_ASSERT_N(iop_cfolder_feed_operator(&cfolder, _op),            \
                   "error when feeding with %d", _op)

#define result(_res, _signed) \
        do {                                                            \
            uint64_t cres;                                              \
            bool is_signed;                                             \
            \
            Z_ASSERT_N(iop_cfolder_get_result(&cfolder, &cres, &is_signed),\
                       "constant folder error");                        \
            Z_ASSERT_EQ((int64_t)cres, (int64_t)_res);                  \
            Z_ASSERT_EQ(is_signed, _signed);                            \
            iop_cfolder_wipe(&cfolder);                                 \
            iop_cfolder_init(&cfolder);                                 \
        } while (false)

#define error()                                                         \
        do {                                                            \
            uint64_t cres;                                              \
                                                                        \
            Z_ASSERT_NEG(iop_cfolder_get_result(&cfolder, &cres, NULL));\
            iop_cfolder_wipe(&cfolder);                                 \
            iop_cfolder_init(&cfolder);                                 \
        } while (false)

        iop_cfolder_t cfolder;

        iop_cfolder_init(&cfolder);

        feed_num(10);
        feed_op('+');
        feed_num(2);
        feed_op('*');
        feed_num(3);
        feed_op('*');
        feed_num(4);
        feed_op('-');
        feed_num(10);
        result(24, false);

        feed_num(10);
        feed_op('*');
        feed_num(2);
        feed_op('+');
        feed_num(3);
        feed_op('+');
        feed_num(4);
        feed_op('*');
        feed_num(10);
        result(63, false);

        feed_num(8);
        feed_op('+');
        feed_num(4);
        feed_op('+');
        feed_op('-');
        feed_num(2);
        feed_op('+');
        feed_num(2);
        feed_op('*');
        feed_op('-');
        feed_num(5);
        feed_op('/');
        feed_num(2);
        feed_op('+');
        feed_num(1);
        result(6, false);

        feed_num(32);
        feed_op('/');
        feed_num(4);
        feed_op(CF_OP_EXP);
        feed_num(2);
        feed_op('/');
        feed_num(2);
        result(1, false);

        feed_num(8);
        feed_op('/');
        feed_num(4);
        feed_op('/');
        feed_num(2);
        result(1, false);

        feed_num(8);
        feed_op('/');
        feed_op('(');
        feed_num(4);
        feed_op('/');
        feed_num(2);
        feed_op(')');
        result(4, false);

        feed_num(4);
        feed_op(CF_OP_EXP);
        feed_num(3);
        feed_op(CF_OP_EXP);
        feed_num(2);
        result(262144, false);

        feed_num(4);
        feed_op('+');
        feed_op('-');
        feed_num(2);
        feed_op(CF_OP_EXP);
        feed_num(2);
        result(0, false);

        feed_num(1);
        feed_op('+');
        feed_num(4);
        feed_op(CF_OP_EXP);
        feed_num(3);
        feed_op(CF_OP_EXP);
        feed_num(1);
        feed_op('+');
        feed_num(1);
        feed_op('-');
        feed_num(1);
        result(65, false);

        feed_num(0xfffff);
        feed_op('&');
        feed_num(32);
        feed_op(CF_OP_LSHIFT);
        feed_num(2);
        feed_op('|');
        feed_num(3);
        result(131, false);

        feed_num(63);
        feed_op('-');
        feed_num(64);
        result(-1, true);

        feed_num(1);
        feed_op('/');
        feed_num(0);
        error();

        feed_num(1);
        feed_op('%');
        feed_num(0);
        error();

        feed_num(INT64_MIN);
        feed_op('/');
        feed_num(-1);
        error();

        feed_num(2);
        feed_op(CF_OP_EXP);
        feed_num(63);
        feed_op('-');
        feed_num(1);
        result(INT64_MAX, false);

        feed_num(-2);
        feed_op(CF_OP_EXP);
        feed_num(63);
        result(INT64_MIN, true);

        feed_num(1);
        feed_op(CF_OP_EXP);
        feed_num(INT64_MAX);
        result(1, false);

        feed_num(-1);
        feed_op(CF_OP_EXP);
        feed_num(INT64_MAX);
        result(-1, true);

        feed_num(-1);
        feed_op(CF_OP_EXP);
        feed_num(0);
        result(1, false);

        feed_num(-1);
        feed_op(CF_OP_EXP);
        feed_num(INT64_MAX - 1);
        result(1, false);

        feed_num(2);
        feed_op(CF_OP_EXP);
        feed_num(INT64_MAX);
        error();

        feed_num(-2);
        feed_op(CF_OP_EXP);
        feed_num(INT64_MAX);
        error();

        iop_cfolder_wipe(&cfolder);
#undef feed_num
#undef feed_op
#undef result
#undef error
    } Z_TEST_END;
    /* }}} */
    Z_TEST(camelcase_to_c, "test IOP camelcase name to C") { /* {{{ */
        t_scope;

        Z_ASSERT_LSTREQUAL(LSTR("foo"), t_camelcase_to_c(LSTR("foo")));
        Z_ASSERT_LSTREQUAL(LSTR("foo_bar123_long_name456"),
                           t_camelcase_to_c(LSTR("FooBar123LongName456")));

        Z_ASSERT_LSTREQUAL(LSTR("foo"), t_iop_type_to_c(LSTR("foo")));
        Z_ASSERT_LSTREQUAL(LSTR("pa__cka__ge__foo_bar123_long_name456"),
            t_iop_type_to_c(LSTR("pa.cka.ge.FooBar123LongName456")));
        Z_ASSERT_LSTREQUAL(LSTR("foo__bar__baz_baz__qux"),
                           t_iop_type_to_c(LSTR("foo.bar.baz_baz.qux")));
    } Z_TEST_END;
    /* }}} */
    Z_TEST(c_to_camelcase, "test C name to IOP camelcase") { /* {{{ */
        t_scope;
        SB_1k(out);

#define CHECK_C_TO_CAMELCASE(_lhs, _rhs, _caps)                              \
        Z_ASSERT_N(c_to_camelcase(_lhs, _caps, &out));                       \
        Z_ASSERT_LSTREQUAL(_rhs,                                             \
                           lstr_init_(out.data, out.len, MEM_STACK));        \

        CHECK_C_TO_CAMELCASE(LSTR("foo"), LSTR("foo"), false);
        CHECK_C_TO_CAMELCASE(LSTR("foo_bar_123_long_name456"),
                             LSTR("FooBar123LongName456"), true);
        CHECK_C_TO_CAMELCASE(t_camelcase_to_c(LSTR("fBa42")),
                             LSTR("fBa42"), false);

        Z_ASSERT_N(c_to_camelcase(LSTR("a_b_c"), false, &out));
        Z_ASSERT_LSTREQUAL(LSTR("a_b_c"),
                           t_camelcase_to_c(lstr_init_(out.data, out.len,
                                                       MEM_STACK)));

        Z_ASSERT_NEG(c_to_camelcase(LSTR("_foo"), false, &out));
        Z_ASSERT_NEG(c_to_camelcase(LSTR("bar_"), true, &out));
        Z_ASSERT_NEG(c_to_camelcase(LSTR("foo__bar"), false, &out));
        Z_ASSERT_NEG(c_to_camelcase(LSTR("foo-bar"), false, &out));
        Z_ASSERT_NEG(c_to_camelcase(LSTR("foo_Bar"), false, &out));

#undef CHECK_C_TO_CAMELCASE

        Z_ASSERT_LSTREQUAL(t_c_to_camelcase(LSTR("foo_bar"), true),
                           LSTR("FooBar"));
        Z_ASSERT_LSTREQUAL(t_c_to_camelcase(LSTR("foo_bar"), false),
                           LSTR("fooBar"));
    } Z_TEST_END;
    /* }}} */
    Z_TEST(unions, "test IOP union helpers") { /* {{{ */
        t_scope;

        iop_dso_t *dso;

        dso = Z_DSO_OPEN();

        {
            tstiop__my_union_a__t ua = IOP_UNION(tstiop__my_union_a, ua, 42);
            int *uavp, uav = 0;

            IOP_UNION_SWITCH(&ua) {
              IOP_UNION_CASE(tstiop__my_union_a, &ua, ua, v) {
                Z_ASSERT_EQ(v, 42);
              }
              IOP_UNION_CASE_V(tstiop__my_union_a, &ua, ub) {
                Z_ASSERT(false, "shouldn't be reached");
              }
              IOP_UNION_CASE_V(tstiop__my_union_a, &ua, us) {
                Z_ASSERT(false, "shouldn't be reached");
              }
              IOP_UNION_DEFAULT() {
                Z_ASSERT(false, "default case shouldn't be reached");
              }
            }

            Z_ASSERT_P(uavp = tstiop__my_union_a__get(&ua, ua));
            Z_ASSERT_EQ(*uavp, 42);
            Z_ASSERT(IOP_UNION_COPY(uav, tstiop__my_union_a, &ua, ua));
            Z_ASSERT_EQ(uav, 42);

            Z_ASSERT_NULL(tstiop__my_union_a__get(&ua, ub));
            Z_ASSERT_NULL(tstiop__my_union_a__get(&ua, us));
        }

        {
            tstiop__my_union_a__t ub = IOP_UNION(tstiop__my_union_a, ub, 42);
            int8_t *ubvp;

            IOP_UNION_SWITCH(&ub) {
              IOP_UNION_CASE_V(tstiop__my_union_a, &ub, ua) {
                Z_ASSERT(false, "shouldn't be reached");
              }
              IOP_UNION_CASE_P(tstiop__my_union_a, &ub, ub, v) {
                Z_ASSERT_EQ(*v, 42);
              }
              IOP_UNION_CASE_V(tstiop__my_union_a, &ub, us) {
                Z_ASSERT(false, "shouldn't be reached");
              }
              IOP_UNION_DEFAULT() {
                Z_ASSERT(false, "default case shouldn't be reached");
              }
            }

            Z_ASSERT_P(ubvp = tstiop__my_union_a__get(&ub, ub));
            Z_ASSERT_EQ(*ubvp, 42);

            Z_ASSERT_NULL(tstiop__my_union_a__get(&ub, ua));
            Z_ASSERT_NULL(tstiop__my_union_a__get(&ub, us));
        }

        {
            tstiop__my_union_a__t us = IOP_UNION(tstiop__my_union_a, us,
                                                 LSTR_IMMED("foo"));
            lstr_t *usvp;

            IOP_UNION_SWITCH(&us) {
              IOP_UNION_CASE_V(tstiop__my_union_a, &us, ua) {
                Z_ASSERT(false, "shouldn't be reached");
              }
              IOP_UNION_CASE_V(tstiop__my_union_a, &us, ub) {
                Z_ASSERT(false, "shouldn't be reached");
              }
              IOP_UNION_CASE(tstiop__my_union_a, &us, us, v) {
                Z_ASSERT_LSTREQUAL(v, LSTR("foo"));
              }
              IOP_UNION_DEFAULT() {
                Z_ASSERT(false, "default case shouldn't be reached");
              }
            }

            Z_ASSERT_P(usvp = tstiop__my_union_a__get(&us, us));
            Z_ASSERT_LSTREQUAL(*usvp, LSTR("foo"));

            Z_ASSERT_NULL(tstiop__my_union_a__get(&us, ua));
            Z_ASSERT_NULL(tstiop__my_union_a__get(&us, ub));
        }

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(soap, "test IOP SOAP (un)packer") { /* {{{ */
        t_scope;

        iop_dso_t *dso;

        int32_t val[] = {15, 30, 45};

        tstiop__my_struct_e__t se = {
            .a = 10,
            .b = IOP_UNION(tstiop__my_union_a, ua, 42),
            .c = { .b = IOP_ARRAY(val, countof(val)), },
        };

        uint64_t uval[] = {UINT64_MAX, INT64_MAX, 0};

        tstiop__my_class2__t cls2;

        tstiop__my_union_a__t un = IOP_UNION(tstiop__my_union_a, ua, 1);

        tstiop__my_struct_a__t sa = {
            .a = 42,
            .b = 5,
            .c_of_my_struct_a = 120,
            .d = 230,
            .e = 540,
            .f = 2000,
            .g = 10000,
            .h = UINT64_MAX,
            .htab = IOP_ARRAY(uval, countof(uval)),
            .i = LSTR_IMMED("foo"),
            .j = LSTR_EMPTY,
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, ub, 42),
            .lr = &un,
            .cls2 = &cls2,
            .m = 3.14159265,
            .n = true,
            .xml_field = LSTR_IMMED("<foo><bar/><foobar "
                                    "attr=\"value\">toto</foobar></foo>"),
        };

        lstr_t svals[] = {
            LSTR_IMMED("foo"), LSTR_IMMED("bar"), LSTR_IMMED("foobar"),
        };

        lstr_t dvals[] = {
            LSTR_IMMED("Test"), LSTR_IMMED("Foo"), LSTR_IMMED("BAR"),
        };

        tstiop__my_struct_b__t bvals[] = {
            { .b = IOP_ARRAY(NULL, 0), },
            { .a = OPT(55), .b = IOP_ARRAY(NULL, 0), }
        };

        tstiop__my_struct_f__t sf = {
            .a = IOP_ARRAY(svals, countof(svals)),
            .b = IOP_ARRAY(dvals, countof(dvals)),
            .c = IOP_ARRAY(bvals, countof(bvals)),
        };

        const iop_struct_t *st_se, *st_sa, *st_sf, *st_cs, *st_sa_opt;
        const iop_struct_t *st_cls2;

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_se = iop_dso_find_type(dso, LSTR("tstiop.MyStructE")));
        Z_ASSERT_P(st_sa = iop_dso_find_type(dso, LSTR("tstiop.MyStructA")));
        Z_ASSERT_P(st_sf = iop_dso_find_type(dso, LSTR("tstiop.MyStructF")));
        Z_ASSERT_P(st_cs = iop_dso_find_type(dso, LSTR("tstiop.ConstraintS")));
        Z_ASSERT_P(st_sa_opt = iop_dso_find_type(dso, LSTR("tstiop.MyStructAOpt")));
        Z_ASSERT_P(st_cls2 = iop_dso_find_type(dso, LSTR("tstiop.MyClass2")));

        iop_init_desc(st_cls2, &cls2);

        /* We test that packing and unpacking of XML structures is stable */
        Z_HELPER_RUN(iop_xml_test_struct(st_se, &se, "se"));
        Z_HELPER_RUN(iop_xml_test_struct(st_sa, &sa, "sa"));
        Z_HELPER_RUN(iop_xml_test_struct(st_sf, &sf, "sf"));

        { /* IOP_XUNPACK_IGNORE_UNKNOWN */
            t_scope;
            tstiop__my_struct_f__t sf_ret;
            SB_1k(sb);

            sb_adds(&sb, "<root "
                    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    ">\n");
            sb_adds(&sb,
                    "<unk1></unk1>"
                    "<a>foo</a><a>bar</a><a>foobar</a>"
                    "<b>VGVzdA==</b><b>Rm9v</b><b>QkFS</b>"
                    "<c><unk2>foo</unk2></c><c><a>55</a><unk3 /></c><c />"
                    "<c><a>55</a><b>2</b><unk3 /></c>"
                    "<unk4>foo</unk4>");
            sb_adds(&sb, "</root>\n");

            iop_init_desc(st_sf, &sf_ret);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_NEG(iop_xunpack(xmlr_g, t_pool(), st_sf, &sf_ret),
                         "unexpected successful unpacking");
            xmlr_close(&xmlr_g);

            iop_init_desc(st_sf, &sf_ret);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_N(iop_xunpack_flags(xmlr_g, t_pool(), st_sf, &sf_ret,
                                         IOP_UNPACK_IGNORE_UNKNOWN),
                       "unexpected unpacking failure using IGNORE_UNKNOWN");
            xmlr_close(&xmlr_g);
        }

        {
            t_scope;
            tstiop__my_struct_f__t sf_ret;
            SB_1k(sb);
            qm_t(part) parts;

            qm_init_cached(part, &parts);
            qm_add(part, &parts, &LSTR_IMMED_V("foo"), LSTR("part cid foo"));
            qm_add(part, &parts, &LSTR_IMMED_V("bar"), LSTR("part cid bar"));

            sb_adds(&sb, "<root "
                    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    ">\n");
            sb_adds(&sb,
                    "<a></a><a/><a>foo</a>"
                    "<a href=\'cid:foo\'/>"
                    "<a><inc:Include href=\'cid:bar\' xmlns:inc=\"url\" /></a>"
                    "<b>VGVzdA==</b>"
                    "<b href=\'cid:foo\'/>");
            sb_adds(&sb, "</root>\n");

            iop_init_desc(st_sf, &sf_ret);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_NEG(iop_xunpack(xmlr_g, t_pool(), st_sf, &sf_ret),
                         "unexpected successful unpacking");
            xmlr_close(&xmlr_g);

            iop_init_desc(st_sf, &sf_ret);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_N(iop_xunpack_parts(xmlr_g, t_pool(), st_sf, &sf_ret,
                                         0, &parts),
                       "unexpected unpacking failure with parts");
            xmlr_close(&xmlr_g);

            qm_wipe(part, &parts);
        }

        { /* Test numeric values */
            t_scope;
            tstiop__my_struct_a_opt__t sa_opt;
            SB_1k(sb);

            sb_adds(&sb, "<root "
                    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    ">\n");
            sb_adds(&sb,
                    "<a>42</a>"
                    "<b>0x10</b>"
                    "<e>-42</e>"
                    "<f>0x42</f>");
            sb_adds(&sb, "</root>\n");

            iop_init_desc(st_sa_opt, &sa_opt);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_N(iop_xunpack(xmlr_g, t_pool(), st_sa_opt, &sa_opt));
            xmlr_close(&xmlr_g);

            Z_ASSERT(OPT_ISSET(sa_opt.a));
            Z_ASSERT_EQ(OPT_VAL(sa_opt.a), 42);

            Z_ASSERT(OPT_ISSET(sa_opt.b));
            Z_ASSERT_EQ(OPT_VAL(sa_opt.b), 0x10U);

            Z_ASSERT(OPT_ISSET(sa_opt.e));
            Z_ASSERT_EQ(OPT_VAL(sa_opt.e), -42);

            Z_ASSERT(OPT_ISSET(sa_opt.f));
            Z_ASSERT_EQ(OPT_VAL(sa_opt.f), 0x42);
        }

        { /* Test PRIVATE */
            t_scope;
            tstiop__constraint_s__t cs;
            SB_1k(sb);
            byte *res;
            int ret;
            lstr_t strings[] = {
                LSTR("foo5"),
                LSTR("foo6"),
            };

            iop_init_desc(st_cs, &cs);
            cs.s.tab = strings;
            cs.s.len = 2;
            Z_HELPER_RUN(iop_xml_test_struct(st_cs, &cs, "cs"));

            OPT_SET(cs.priv, true);
            cs.priv2 = false;
            Z_HELPER_RUN(iop_xml_test_struct(st_cs, &cs, "cs"));

            /* packing (private values should be skipped) */
            sb_adds(&sb, "<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">");
            iop_xpack_flags(&sb, st_cs, &cs, IOP_XPACK_SKIP_PRIVATE);
            sb_adds(&sb, "</root>");

            Z_ASSERT_NULL(strstr(sb.data, "<priv>"));
            Z_ASSERT_NULL(strstr(sb.data, "<priv2>"));

            /* unpacking should work (private values are gone) */
            res = t_new(byte, ROUND_UP(st_cs->size, 8));
            iop_init_desc(st_cs, res);

            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            ret = iop_xunpack_flags(xmlr_g, t_pool(), st_cs, &cs,
                                    IOP_UNPACK_FORBID_PRIVATE);
            Z_ASSERT_N(ret, "XML unpacking failure (%s, %s): %s",
                       st_cs->fullname.s, "st_cs", xmlr_get_err());
            Z_ASSERT(!OPT_ISSET(cs.priv));
            Z_ASSERT(cs.priv2);

            /* now test that unpacking only works when private values are not
             * specified */
            sb_reset(&sb);
            sb_adds(&sb, "<root "
                    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    ">\n");
            sb_adds(&sb,
                    "<s>abcd</s>"
                    "<s>abcd</s>");
            sb_adds(&sb, "</root>\n");

            iop_init_desc(st_cs, &cs);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_N(iop_xunpack_flags(xmlr_g, t_pool(), st_cs, &cs,
                                         IOP_UNPACK_FORBID_PRIVATE));
            xmlr_close(&xmlr_g);

            sb_reset(&sb);
            sb_adds(&sb, "<root "
                    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    ">\n");
            sb_adds(&sb,
                    "<s>abcd</s>"
                    "<s>abcd</s>"
                    "<priv>true</priv>");
            sb_adds(&sb, "</root>\n");

            iop_init_desc(st_cs, &cs);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_NEG(iop_xunpack_flags(xmlr_g, t_pool(), st_cs, &cs,
                                           IOP_UNPACK_FORBID_PRIVATE));
            xmlr_close(&xmlr_g);

            sb_reset(&sb);
            sb_adds(&sb, "<root "
                    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                    ">\n");
            sb_adds(&sb,
                    "<s>abcd</s>"
                    "<s>abcd</s>"
                    "<priv2>true</priv2>");
            sb_adds(&sb, "</root>\n");

            iop_init_desc(st_cs, &cs);
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
            Z_ASSERT_NEG(iop_xunpack_flags(xmlr_g, t_pool(), st_cs, &cs,
                                           IOP_UNPACK_FORBID_PRIVATE));
            xmlr_close(&xmlr_g);
        }

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(json, "test IOP JSon (un)packer") { /* {{{ */
        t_scope;
        /* {{{ Variable declarations */

        SB_1k(err);

        tstiop__my_class2__t cls2;

        tstiop__my_union_a__t un = IOP_UNION(tstiop__my_union_a, ua, 1);

        tstiop__my_struct_a__t sa = {
            .a = 42,
            .b = 5,
            .c_of_my_struct_a = 120,
            .d = 230,
            .e = 540,
            .f = 2000,
            .g = 10000,
            .h = 20000,
            .i = LSTR_IMMED("foo"),
            .j = LSTR_IMMED("baré© \" foo ."),
            .xml_field = LSTR_IMMED("<foo />"),
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, ub, 42),
            .lr = &un,
            .cls2 = &cls2,
            .m = 3.14159265,
            .n = true,
            .p = '.',
            .q = '!',
            .r = '*',
            .s = '+',
            .t = '\t',
        };

        tstiop__my_struct_a__t sa2 = {
            .a = 42,
            .b = 5,
            .c_of_my_struct_a = 120,
            .d = 230,
            .e = 540,
            .f = 2000,
            .g = 10000,
            .h = 20000,
            .i = LSTR_EMPTY,
            .j = LSTR_EMPTY,
            .xml_field = LSTR_EMPTY,
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, ub, 42),
            .lr = &un,
            .cls2 = &cls2,
            .m = 3.14159265,
            .n = true,
            .p = '.',
            .q = '!',
            .r = '*',
            .s = '+',
            .t = '\t',
        };

        const char json_sa[] =
            "/* Json example */\n"
            "@j \"bar\" {\n"
            "    \"a\": 42,\n"
            "    \"b\": 50,\n"
            "    cOfMyStructA: 30,\n"
            "    \"d\": 40,\n"
            "    \"e\": 50, //comment\n"
            "    \"f\": 60,\n"
            "    \"g\": 10d,\n"
            "    \"h\": 1T,\n"
            "    \"i\": \"Zm9v\",\n"
            "    \"xmlField\": \"\",\n"
            "    \"k\": \"B\",\n"
            "    l.us: \"union value\",\n"
            "    lr.ua: 1,\n"
            "    cls2: {\n"
            "        \"_class\": \"tstiop.MyClass2\",\n"
            "        \"int1\": 1,\n"
            "        \"int2\": 2\n"
            "    },\n"
            "    foo: {us: \"union value to skip\"},\n"
            "    bar.us: \"union value to skip\",\n"
            "    arraytoSkip: [ .blah: \"skip\", .foo: 42, 32; \"skipme\";\n"
            "                   { foo: 42 } ];"
            "    \"m\": .42,\n"
            "    \"n\": true,\n"
            "    \"p\": c\'.\',\n"
            "    \"q\": c\'\\041\',\n"
            "    \"r\": c\'\\x2A\',\n"
            "    \"s\": c\'\\u002B\',\n"
            "    \"t\": c\'\\t\'\n"
            "};\n"
            ;

        const char json_sa2[] =
            "/* Json example */\n"
            "@j \"bar\" {\n"
            "    \"a\": 42,\n"
            "    \"b\": 50,\n"
            "    cOfMyStructA: 30,\n"
            "    \"d\": 40,\n"
            "    \"e\": 50, //comment\n"
            "    \"f\": 60,\n"
            "    \"g\": 10d,\n"
            "    \"h\": 1T,\n"
            "    \"i\": \"Zm9v\",\n"
            "    \"skipMe\": 42,\n"
            "    \"skipMe2\": null,\n"
            "    \"skipMe3\": { foo: [1, 2, 3, {bar: \"plop\"}] },\n"
            "    \"xmlField\": \"\",\n"
            "    \"k\": \"B\",\n"
            "    l: {us: \"union value\"},\n"
            "    lr: {ua: 1},\n"
            "    cls2: {\n"
            "        \"_class\": \"tstiop.MyClass2\",\n"
            "        \"int1\": 1,\n"
            "        \"int2\": 2\n"
            "    },\n"
            "    foo: {us: \"union value to skip\"},\n"
            "    bar.us: \"union value to skip\",\n"
            "    \"m\": 0.42\n,"
            "    \"n\": true,\n"
            "    \"p\": c\'.\',\n"
            "    \"q\": c\'\\041\',\n"
            "    \"r\": c\'\\x2A\',\n"
            "    \"s\": c\'\\u002B\',\n"
            "    \"t\": c\'\\t\'\n"
            "};\n"
            "// last line contains a comment and no \\n"
            ;

        tstiop__my_struct_a__t json_sa_res = {
            .a = 42,
            .b = 50,
            .c_of_my_struct_a = 30,
            .d = 40,
            .e = 50,
            .f = 60,
            .g = 10 * 24 * 3600,
            .h = 1ULL << 40,
            .i = LSTR_IMMED("foo"),
            .j = LSTR_IMMED("bar"),
            .xml_field = LSTR_EMPTY,
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, us, LSTR_IMMED("union value")),
            .lr = &un,
            .cls2 = &cls2,
            .m = 0.42,
            .n = true,
            .p = '.',
            .q = '!',
            .r = '*',
            .s = '+',
            .t = '\t',
        };

        const char json_sf[] =
            "/* Json example */\n"
            "{\n"
            "    a = [ \"foo\", \"bar\", ];\n"
            "    b = [ \"Zm9vYmFy\", \"YmFyZm9v\", ];\n"
            "    c = [ @a 10 {\n"
            "       b = [ 1w, 1d, 1h, 1m, 1s, 1G, 1M, 1K, ];\n"
            "    }];\n"
            "    d = [ .us: \"foo\", .ub: true ];\n"
            "};;;\n"
            ;

        const char json_sf2[] =
            "/* Json example */\n"
            "{\n"
            "    a = [ \"foo\", \"bar\", ];\n"
            "    b = [ \"Zm9vYmFy\", \"YmFyZm9v\", ];\n"
            "    c = [ @a 10 {\n"
            "       b = [ 1w, 1d, 1h, 1m, 1s, 1G, 1M, 1K, ];\n"
            "    }];\n"
            "    d = [ {us: \"foo\"}, {ub: true} ];\n"
            "};;;\n"
            ;

        lstr_t avals[] = {
            LSTR_IMMED("foo"),
            LSTR_IMMED("bar"),
        };

        lstr_t bvals[] = {
            LSTR_IMMED("foobar"),
            LSTR_IMMED("barfoo"),
        };

        int b2vals[] = { 86400*7, 86400, 3600, 60, 1, 1<<30, 1<<20, 1<<10 };

        tstiop__my_struct_b__t cvals[] = {
            {
                .a = OPT(10),
                .b = IOP_ARRAY(b2vals, countof(b2vals)),
            }
        };

        tstiop__my_union_a__t dvals[] = {
            IOP_UNION(tstiop__my_union_a, us, LSTR_IMMED("foo")),
            IOP_UNION(tstiop__my_union_a, ub, true),
        };

        const tstiop__my_struct_f__t json_sf_res = {
            .a = IOP_ARRAY(avals, countof(avals)),
            .b = IOP_ARRAY(bvals, countof(bvals)),
            .c = IOP_ARRAY(cvals, countof(cvals)),
            .d = IOP_ARRAY(dvals, countof(dvals)),
        };

#define xstr(...) str(__VA_ARGS__)
#define str(...)  #__VA_ARGS__

#define IVALS -1*10-(-10-1), 0x10|0x11, ((0x1f + 010)- 0X1E -5-8) *(2+2),   \
    0-1, ((0x1f + 010) - 0X1E - 5 - 8) * (2 +2),                            \
    ~0xffffffffffffff00 + POW(3, 4) - (1 << 2), (2 * 3 + 1) << 2,           \
    POW(2, (5+(-2))), 1, -1, +1, 1+1, -1+1, +1+1
#define DVALS .5, +.5, -.5, 0.5, +0.5, -0.5, 5.5, 0.2e2, 0x1P10
#define EVALS  EC(A), EC(A) | EC(B) | EC(C) | EC(D) | EC(E), (1 << 5) - 1

#define EC(s)       #s
#define POW(a,b)    a ** b
        const char json_si[] =
            "/* Json example */\n"
            "{\n"
            "    i = [ " xstr(IVALS) " ];\n"
            "    d = [ " xstr(DVALS) " ];\n"
            "    e = [ " xstr(EVALS) " ];\n"
            "};;;\n"
            ;

        const char json_si_p1[] = "{l = [ -0x7fffffffffffffff + (-1) ]; };" ;
        const char json_si_p2[] = "{u =    0xffffffffffffffff +   0   ; };" ;
        const char json_si_p3[] = "{u = [ \"9223372036854775808\" ]; };" ;

        const char json_si_n1[] = "{l = [ -0x7fffffffffffffff + (-2) ]; };" ;
        const char json_si_n2[] = "{u = [  0xffffffffffffffff +   1  ]; };" ;
#undef EC
#undef POW

#define EC(s)       MY_ENUM_C_ ##s
#define POW(a,b)    (int)pow(a,b)
        int                     i_ivals[] = { IVALS };
        double                  i_dvals[] = { DVALS };
        tstiop__my_enum_c__t    i_evals[] = { EVALS };
#undef EC
#undef POW
        const tstiop__my_struct_i__t json_si_res = {
            .i = IOP_ARRAY(i_ivals, countof(i_ivals)),
            .d = IOP_ARRAY(i_dvals, countof(i_dvals)),
            .e = IOP_ARRAY(i_evals, countof(i_evals)),
        };

        const char json_sk[] =
            "/* Json example */\n"
            "{\n"
            "    j = @cval 2 { \n"
            "                  b.a.us = \"foo\";\n"
            "                  btab = [ .bval: 0xf + 1, .a.ua: 2*8 ];\n"
            "                };\n"
            "};;;\n"
            ;

        tstiop__my_union_b__t j_bvals[] = {
            IOP_UNION(tstiop__my_union_b, bval, 16),
            IOP_UNION(tstiop__my_union_b, a,
                      (IOP_UNION(tstiop__my_union_a, ua, 16))),
        };

        const tstiop__my_struct_k__t json_sk_res = {
            .j = {
                .cval   = 2,
                .b      = IOP_UNION(tstiop__my_union_b, a,
                                    IOP_UNION(tstiop__my_union_a, us,
                                              LSTR("foo"))),
                .btab = IOP_ARRAY(j_bvals, countof(j_bvals)),
            },
        };

        tstiop__my_struct_a_opt__t json_sa_opt_res = {
            .a = OPT(42),
        };

        tstiop__void__t iop_void = {};

        iop_dso_t *dso;

        const iop_struct_t *st_sa, *st_sf, *st_si, *st_sk, *st_sn, *st_sa_opt;
        const iop_struct_t *st_cls2, *st_sg, *st_uc;

        const char json_sg_p1[] = "{ \"c_of_g\": 42 }";
        const char json_uc_p1[] = "{ d_of_c: 3.141592653589793238462643383 }";

        /* }}} */

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_sa = iop_dso_find_type(dso, LSTR("tstiop.MyStructA")));
        Z_ASSERT_P(st_sf = iop_dso_find_type(dso, LSTR("tstiop.MyStructF")));
        Z_ASSERT_P(st_si = iop_dso_find_type(dso, LSTR("tstiop.MyStructI")));
        Z_ASSERT_P(st_sk = iop_dso_find_type(dso, LSTR("tstiop.MyStructK")));
        Z_ASSERT_P(st_sn = iop_dso_find_type(dso, LSTR("tstiop.MyStructN")));
        Z_ASSERT_P(st_sa_opt = iop_dso_find_type(dso, LSTR("tstiop.MyStructAOpt")));
        Z_ASSERT_P(st_cls2 = iop_dso_find_type(dso, LSTR("tstiop.MyClass2")));
        Z_ASSERT_P(st_sg = iop_dso_find_type(dso, LSTR("tstiop.MyStructG")));
        Z_ASSERT_P(st_uc = iop_dso_find_type(dso, LSTR("tstiop.MyUnionC")));

        iop_init_desc(st_cls2, &cls2);
        cls2.int1 = 1;
        cls2.int2 = 2;

        /* test packing/unpacking */
        Z_HELPER_RUN(iop_json_test_struct(st_sa, &sa,  "sa"));
        Z_HELPER_RUN(iop_json_test_struct(st_sa, &sa2, "sa2"));

        /* test unpacking */
        Z_HELPER_RUN(iop_json_test_json(st_sa, json_sa,  &json_sa_res,
                                        "json_sa"));
        Z_HELPER_RUN(iop_json_test_json(st_sa, json_sa2, &json_sa_res,
                                        "json_sa2"));
        Z_HELPER_RUN(iop_json_test_json(st_sf, json_sf,  &json_sf_res,
                                        "json_sf"));
        Z_HELPER_RUN(iop_json_test_json(st_sf, json_sf2, &json_sf_res,
                                        "json_sf2"));
        Z_HELPER_RUN(iop_json_test_json(st_si, json_si,  &json_si_res,
                                        "json_si"));
        Z_HELPER_RUN(iop_json_test_json(st_sk, json_sk,  &json_sk_res,
                                        "json_sk"));

        Z_HELPER_RUN(iop_json_test_json(st_sa_opt, "{ a:42, o: null }",
                                        &json_sa_opt_res, "json_sa_opt"));

        /* test iop void */
        json_sa_opt_res.v = &iop_void;
        /* test escaping of characters according to http://www.json.org/ */
        json_sa_opt_res.j = LSTR("\" \\ / \b \f \n \r \t ♡");
        Z_HELPER_RUN(iop_json_test_json(st_sa_opt, "{ a:42, o: null, v: {}, "
                                        "j: \"\\\" \\\\ \\/ \\b \\f \\n \\r "
                                        "\\t \\u2661\" }",
                                        &json_sa_opt_res, "json_sa_opt"));

        Z_HELPER_RUN(iop_json_test_unpack(st_si, json_si_p1,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          true, "json_si_p1"));
        Z_HELPER_RUN(iop_json_test_unpack(st_si, json_si_p2,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          true, "json_si_p2"));
        Z_HELPER_RUN(iop_json_test_unpack(st_si, json_si_p3,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          true, "json_si_p3"));

        Z_HELPER_RUN(iop_json_test_unpack(st_si, json_si_n1,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          false, "json_si_n1"));
        Z_HELPER_RUN(iop_json_test_unpack(st_si, json_si_n2,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          false, "json_si_n2"));

        Z_HELPER_RUN(iop_json_test_unpack(st_sg, json_sg_p1, 0, false,
                                          "json_sg_p1"));
        Z_HELPER_RUN(iop_json_test_unpack(st_sg, json_sg_p1,
                                          IOP_UNPACK_USE_C_CASE, true,
                                          "json_sg_p1"));
        Z_HELPER_RUN(iop_json_test_unpack(st_uc, json_uc_p1, 0, false,
                                          "json_uc_p1"));
        Z_HELPER_RUN(iop_json_test_unpack(st_uc, json_uc_p1,
                                          IOP_UNPACK_USE_C_CASE, true,
                                          "json_uc_p1"));

        /* Test iop_jpack_file failure */
        Z_ASSERT_NEG(iop_jpack_file("/proc/path/to/unknown/dir.json", st_sk,
                                    &json_sk_res, 0, &err));
        Z_ASSERT_STREQUAL(err.data, "cannot open output file "
                          "`/proc/path/to/unknown/dir.json`: "
                          "No such file or directory");

        /* Test packer flags. */
        {
            tstiop__struct_jpack_flags__t st_jpack;
            tstiop__my_class1__t my_class_1;
            tstiop__my_class2__t my_class_2;
            unsigned flags = IOP_JPACK_NO_WHITESPACES
                           | IOP_JPACK_NO_TRAILING_EOL;

            iop_init(tstiop__struct_jpack_flags, &st_jpack);
            iop_init(tstiop__my_class1, &my_class_1);
            iop_init(tstiop__my_class2, &my_class_2);

#define TST_FLAGS(_flags, _test_unpack, _must_be_equal, _exp)  \
            Z_HELPER_RUN(iop_json_test_pack(&tstiop__struct_jpack_flags__s,  \
                                            &st_jpack, _flags, _test_unpack, \
                                            _must_be_equal, _exp))

            /* NO_WHITESPACES, NO_TRAILING_EOL */
            TST_FLAGS(0, true, true,
                      "{\n"
                      "    \"def\": 1,\n"
                      "    \"rep\": [  ]\n"
                      "}\n");
            TST_FLAGS(IOP_JPACK_NO_WHITESPACES, true, true,
                      "{\"def\":1,\"rep\":[]}\n");
            TST_FLAGS(flags, true, true,
                      "{\"def\":1,\"rep\":[]}");

            /* SKIP_DEFAULT */
            TST_FLAGS(flags | IOP_JPACK_SKIP_DEFAULT, true, true,
                      "{\"rep\":[]}");
            st_jpack.def = 2;
            TST_FLAGS(flags | IOP_JPACK_SKIP_DEFAULT, true, true,
                      "{\"def\":2,\"rep\":[]}");
            st_jpack.def = 1;

            /* SKIP_EMPTY_ARRAYS */
            TST_FLAGS(flags | IOP_JPACK_SKIP_EMPTY_ARRAYS, true, true,
                      "{\"def\":1}");
            st_jpack.rep.tab = &st_jpack.def;
            st_jpack.rep.len = 1;
            TST_FLAGS(flags | IOP_JPACK_SKIP_EMPTY_ARRAYS, true, true,
                      "{\"def\":1,\"rep\":[1]}");
            st_jpack.rep.len = 0;
            flags |= IOP_JPACK_SKIP_EMPTY_ARRAYS;

            /* SKIP_OPTIONAL_CLASS_NAME */
            st_jpack.my_class = &my_class_1;
            TST_FLAGS(flags, true, true,
                      "{\"def\":1,\"myClass\":{\"_class\":\"tstiop.MyClass1\""
                      ",\"int1\":0}}");
            TST_FLAGS(flags | IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES, true, true,
                      "{\"def\":1,\"myClass\":{\"int1\":0}}");
            st_jpack.my_class = &my_class_2.super;
            TST_FLAGS(flags | IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES, true, true,
                      "{\"def\":1,\"myClass\":{\"_class\":\"tstiop.MyClass2\""
                      ",\"int1\":0,\"int2\":0}}");

            /* IOP_JPACK_SKIP_CLASS_NAMES */
            TST_FLAGS(flags | IOP_JPACK_SKIP_CLASS_NAMES, false, false,
                      "{\"def\":1,\"myClass\":{\"int1\":0,\"int2\":0}}");
            st_jpack.my_class = NULL;

            /* SKIP_PRIVATE */
            OPT_SET(st_jpack.priv, 12);
            TST_FLAGS(flags, true, true, "{\"priv\":12,\"def\":1}");
            TST_FLAGS(flags | IOP_JPACK_SKIP_PRIVATE, true, false,
                      "{\"def\":1}");

#undef TST_FLAGS
        }

        /* Test empty struct packer flag. */
        {
            tstiop__jpack_empty_struct__t empty_jpack;
            tstiop__struct_jpack_flags__t sub_st;
            tstiop__jpack_empty_cls_b__t clsb;
            tstiop__jpack_empty_cls_c__t clsc;
            unsigned flags = IOP_JPACK_MINIMAL;

            iop_init(tstiop__jpack_empty_struct, &empty_jpack);
            iop_init(tstiop__jpack_empty_cls_b, &clsb);
            empty_jpack.sub.cls = &clsb;

#define TST(_flags, _must_be_equal, _exp)                                    \
            Z_HELPER_RUN(iop_json_test_pack(&tstiop__jpack_empty_struct__s,  \
                                            &empty_jpack, _flags, true,      \
                                            _must_be_equal, _exp))

            TST(flags, true, "{}");

            OPT_SET(empty_jpack.sub.priv, 8);
            TST(flags, true, "{\"sub\":{\"priv\":8}}");
            TST(flags | IOP_JPACK_SKIP_PRIVATE, false, "{}");
            OPT_CLR(empty_jpack.sub.priv);

            OPT_SET(empty_jpack.sub.opt, 12);
            TST(flags, true, "{\"sub\":{\"opt\":12}}");
            OPT_CLR(empty_jpack.sub.opt);

            empty_jpack.sub.def = 99;
            TST(flags, true, "{\"sub\":{\"def\":99}}");
            empty_jpack.sub.def = 42;

            empty_jpack.sub.rep.tab = &empty_jpack.sub.def;
            empty_jpack.sub.rep.len = 1;
            TST(flags, true, "{\"sub\":{\"rep\":[42]}}");
            empty_jpack.sub.rep.len = 0;

            OPT_SET(empty_jpack.sub.req_st.opt, 65);
            TST(flags, true, "{\"sub\":{\"reqSt\":{\"opt\":65}}""}");
            OPT_CLR(empty_jpack.sub.req_st.opt);

            iop_init(tstiop__struct_jpack_flags, &sub_st);
            empty_jpack.sub.opt_st = &sub_st;
            TST(flags, true, "{\"sub\":{\"optSt\":{}}""}");
            empty_jpack.sub.opt_st = NULL;

            clsb.a = 10;
            TST(flags, true, "{\"sub\":{\"cls\":{\"a\":10}}""}");
            clsb.a = 1;

            iop_init(tstiop__jpack_empty_cls_c, &clsc);
            empty_jpack.sub.cls = &clsc.super;
            TST(flags, true, "{\"sub\":{\"cls\":{"
                "\"_class\":\"tstiop.JpackEmptyClsC\"}}""}");
            empty_jpack.sub.cls = &clsb;

#undef TST
        }

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(json_big_integer, "test JSON packing with big integers") { /* {{{ */
        SB_1k(sb);
        tstiop__my_struct_n__t sn = {
            .u = 9223372036854775808ull,
            .i = -4611686018427387904ll
        };

        const char json_sn_bigint[] =
            "{\n"
            "    \"u\": 9223372036854775808,\n"
            "    \"i\": -4611686018427387904\n"
            "}\n";

        const char json_sn_strint[] =
            "{\n"
            "    \"u\": \"9223372036854775808\",\n"
            "    \"i\": \"-4611686018427387904\"\n"
            "}\n";

        Z_ASSERT_N(iop_jpack(&tstiop__my_struct_n__s, &sn, iop_sb_write,
                             &sb, IOP_JPACK_UNSAFE_INTEGERS));
        Z_ASSERT_STREQUAL(sb.data, json_sn_bigint);

        sb_reset(&sb);
        Z_ASSERT_N(iop_jpack(&tstiop__my_struct_n__s, &sn, iop_sb_write,
                             &sb, 0));
        Z_ASSERT_STREQUAL(sb.data, json_sn_strint);
    } Z_TEST_END
    /* }}} */
    Z_TEST(json_big_bytes, "test JSON packing big bytes fields") { /* {{{ */
        SB_1k(sb);
        tstiop__my_struct_a_opt__t sn;

#define B64_RES_START  "QUJDREVGR0h"
#define B64_RES_MIDDLE  "JSktMTU5PUFFSU1RVVldYWVpBQkNERUZHSElKS0xNTk9QUVJTV"
#define B64_RES_END  "FVWV1hZWg=="

        const char json[] =
            "{\n"
            "    \"i\": \"" B64_RES_START B64_RES_MIDDLE B64_RES_END "\"\n"
            "}\n";

        const char json_cut[] =
            "{\n"
            "    \"i\": \"" B64_RES_START " …(skip 50 bytes)… " B64_RES_END
                "\"\n"
            "}\n";

        Z_ASSERT_EQ(strlen(B64_RES_MIDDLE), 50ul);
        Z_ASSERT_EQ(strlen(B64_RES_START), 11ul);
        Z_ASSERT_EQ(strlen(B64_RES_END), 11ul);

#undef B64_RES_START
#undef B64_RES_MIDDLE
#undef B64_RES_END

        iop_init(tstiop__my_struct_a_opt, &sn);
        sn.i = LSTR("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ");

        Z_ASSERT_N(iop_jpack(&tstiop__my_struct_a_opt__s, &sn, iop_sb_write,
                             &sb, IOP_JPACK_UNSAFE_INTEGERS
                                      | IOP_JPACK_SKIP_EMPTY_ARRAYS));
        Z_ASSERT_STREQUAL(sb.data, json, "`%*pM`", SB_FMT_ARG(&sb));

        sb_reset(&sb);

        Z_ASSERT_N(iop_jpack(&tstiop__my_struct_a_opt__s, &sn, iop_sb_write,
                             &sb, IOP_JPACK_UNSAFE_INTEGERS
                                      | IOP_JPACK_SKIP_EMPTY_ARRAYS
                                      | IOP_JPACK_SHORTEN_DATA));
        Z_ASSERT_STREQUAL(sb.data, json_cut, "`%*pM`", SB_FMT_ARG(&sb));
    } Z_TEST_END
    /* }}} */
    Z_TEST(json_file_include, "test file inclusion in IOP JSon (un)packer") { /* {{{ */
        t_scope;
        SB_1k(err);
        const char *exp_err;
        qv_t(iop_json_subfile) sub_files;
        qv_t(z_json_sub_file)  z_sub_files;
        tstiop__my_struct_a_opt__t obj_basic_string;
        tstiop__my_struct_f__t     obj_string_array;
        tstiop__my_struct_c__t     obj_struct;
        tstiop__my_struct_e__t     obj_union;
        tstiop__my_struct_f__t     obj_class;
        tstiop__my_ref_struct__t   obj_ref;
        tstiop__my_struct_c__t     obj_recursion;
        tstiop__my_struct_m__t     obj_first_field;

        /* {{{ Unpacker tests */

#define T_KO(_type, _file, _exp)  \
        do {                                                                 \
            t_scope;                                                         \
            _type##__t _obj;                                                 \
            const char *_path;                                               \
                                                                             \
            _path = t_fmt("%*pM/iop/tstiop_file_inclusion_invalid-" _file    \
                          ".json", LSTR_FMT_ARG(z_cmddir_g));                \
            Z_ASSERT_NEG(t_iop_junpack_file(_path, &_type##__s, &_obj, 0,    \
                                            NULL, &err));                    \
            Z_ASSERT(strstr(err.data, _exp), "unexpected error: %s",         \
                     err.data);                                              \
            sb_reset(&err);                                                  \
        } while (0)

        T_KO(tstiop__my_struct_a_opt, "include-alone",
             "3:10: expected a string value, got `@'");
        T_KO(tstiop__my_struct_a_opt, "include-empty",
             "3:19: unexpected token `)'");
        T_KO(tstiop__my_struct_a_opt, "include-eof",
             "3:19: something was expected after `\"'");
        T_KO(tstiop__my_struct_a_opt, "missing-quotes",
             "3:19: unexpected token `t'");
        T_KO(tstiop__my_struct_a_opt, "unclosed-quotes",
             "3:20: unclosed string");
        T_KO(tstiop__my_struct_a_opt, "unclosed-parenthesis",
             "3:39: expected ), got `g'");
        T_KO(tstiop__my_struct_a_opt, "misplaced-include",
             "3:5: expected a valid member name, got `@'");
        T_KO(tstiop__my_struct_a_opt, "unknown-file",
             "3:19: cannot read file `/proc/path/to/unknown/file`: "
             "No such file or directory");
        T_KO(tstiop__my_struct_a_opt, "int",
             "3:19: file inclusion not supported for int fields");
        T_KO(tstiop__my_struct_a_opt, "json",
             "3:22: cannot unpack file");
        T_KO(tstiop__my_struct_c, "infinite-recursion",
             "infinite recursion detected in includes");
#undef T_KO

#define T_OK(_type, _res, _file, ...)                                        \
        do {                                                                 \
            _type##__t _exp;                                                 \
            const char *_path;                                               \
            qv_t(iop_json_subfile) _subfiles;                                \
            iop_json_subfile__t _subfiles_exp[] = { __VA_ARGS__ };           \
            int _subfiles_nb = countof(_subfiles_exp);                       \
                                                                             \
            t_qv_init(&_subfiles, _subfiles_nb);           \
            _path = t_fmt("%*pM/iop/tstiop_file_inclusion_" _file ".json",   \
                          LSTR_FMT_ARG(z_cmddir_g));                         \
            Z_ASSERT_N(t_iop_junpack_file(_path, &_type##__s, _res, 0,       \
                                          &_subfiles, &err),                 \
                       "cannot unpack `%s`: %*pM", _path, SB_FMT_ARG(&err)); \
                                                                             \
            _path = t_fmt("%*pM/iop/tstiop_file_inclusion_" _file            \
                          "-exp.json", LSTR_FMT_ARG(z_cmddir_g));            \
            Z_ASSERT_N(t_iop_junpack_file(_path, &_type##__s, &_exp, 0,      \
                                          NULL, &err),                       \
                       "cannot unpack `%s`: %*pM", _path, SB_FMT_ARG(&err)); \
            Z_ASSERT_IOPEQUAL(_type, _res, &_exp);                           \
            Z_ASSERT_EQ(_subfiles_nb, _subfiles.len);                        \
            for (int i = 0; i < _subfiles_nb; i++) {                         \
                Z_ASSERT_LSTREQUAL(_subfiles_exp[i].file_path,               \
                                   _subfiles.tab[i].file_path);              \
                Z_ASSERT_LSTREQUAL(_subfiles_exp[i].iop_path,                \
                                   _subfiles.tab[i].iop_path);               \
            }                                                                \
        } while (0)

        T_OK(tstiop__my_struct_a_opt, &obj_basic_string, "basic-string", {
            .file_path = LSTR("json-includes/string.txt"),
            .iop_path = LSTR("j"),
        });

        T_OK(tstiop__my_struct_f, &obj_string_array, "string-array", {
            .file_path = LSTR("json-includes/string.txt"),
            .iop_path = LSTR("a[0]"),
        }, {
            .file_path = LSTR("json-includes/string2.txt"),
            .iop_path = LSTR("a[2]"),
        }, {
            .file_path = LSTR("json-includes/string.txt"),
            .iop_path = LSTR("b[1]"),
        });

        T_OK(tstiop__my_struct_c, &obj_struct, "struct", {
            .file_path = LSTR("json-includes/MyStructC-1.json"),
            .iop_path = LSTR("b"),
        }, {
            .file_path = LSTR("json-includes/MyStructC-2.json"),
            .iop_path = LSTR("b.b"),
        }, {
            .file_path = LSTR("json-includes/MyStructC-2.json"),
            .iop_path = LSTR("c[1]"),
        });

        T_OK(tstiop__my_struct_e, &obj_union, "union", {
            .file_path = LSTR("json-includes/MyUnionA.json"),
            .iop_path = LSTR("b"),
        });

        T_OK(tstiop__my_struct_f, &obj_class, "class", {
            .file_path = LSTR("json-includes/MyClass1.json"),
            .iop_path = LSTR("e[0]"),
        }, {
            .file_path = LSTR("json-includes/string.txt"),
            .iop_path = LSTR("e[0].string1"),
        }, {
            .file_path = LSTR("json-includes/MyClass1.json"),
            .iop_path = LSTR("f"),
        }, {
            .file_path = LSTR("json-includes/string.txt"),
            .iop_path = LSTR("f.string1"),
        });

        T_OK(tstiop__my_ref_struct, &obj_ref, "ref", {
            .file_path = LSTR("json-includes/MyReferencedStruct.json"),
            .iop_path = LSTR("s"),
        }, {
            .file_path = LSTR("json-includes/MyReferencedUnion.json"),
            .iop_path = LSTR("u"),
        });

        T_OK(tstiop__my_struct_c, &obj_recursion, "recursion", {
            .file_path = LSTR("json-includes/MyStructC-recur-3.json"),
            .iop_path = LSTR("b"),
        }, {
            .file_path = LSTR("json-includes/MyStructC-recur-4.json"),
            .iop_path = LSTR("b.b"),
        });

        T_OK(tstiop__my_struct_c, &obj_recursion, "recursion_symlinks", {
            .file_path = LSTR("json-includes-symlinks/MyStructC-recur-3.json"),
            .iop_path = LSTR("b"),
        }, {
            .file_path = LSTR("json-includes-symlinks/MyStructC-recur-4.json"),
            .iop_path = LSTR("b.b"),
        });

        T_OK(tstiop__my_struct_m, &obj_first_field, "first_field", {
            .file_path = LSTR("json-includes/MyStructK.json"),
            .iop_path = LSTR("k"),
        }, {
            .file_path = LSTR("json-includes/MyStructJ.json"),
            .iop_path = LSTR("k.j"),
        });

#undef T_OK

        /* }}} */
        /* {{{ Packer tests */

        t_qv_init(&sub_files,   16);
        t_qv_init(&z_sub_files, 16);

#define CLEAR_SUB_FILES()  \
        do {                                                                 \
            qv_clear(&sub_files);                          \
            qv_clear(&z_sub_files);                        \
        } while (0)

#define ADD_SUB_FILE(_st, _val, _iop_path, _file_path)  \
        do {                                                                 \
            qv_append(&sub_files, ((iop_json_subfile__t) {                   \
                .iop_path = LSTR(_iop_path),                                 \
                .file_path = LSTR(_file_path),                               \
            }));                                                             \
            qv_append(&z_sub_files, ((z_json_sub_file_t) {  \
                .st   = (_st),                                               \
                .val  = (_val),                                              \
                .path = (_file_path),                                        \
            }));                                                             \
        } while (0)

#define T(_type, _val, _exp_err)  \
        Z_HELPER_RUN(iop_check_json_include_packing(&_type##__s, _val,       \
                                                    &sub_files,              \
                                                    &z_sub_files, _exp_err))

#define T_OK(_type, _val)  T(_type, _val, NULL)
#define T_KO  T

        /* Basic failure cases */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(NULL, &obj_basic_string.j, "j",
                     "/proc/path/to/unknown/file.txt");
        exp_err = "cannot create directory `/proc/path/to/unknown`";
        T_KO(tstiop__my_struct_a_opt, &obj_basic_string, exp_err);

        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_struct.b, "b",
                     "/proc/path/to/unknown/file.json");
        T_KO(tstiop__my_struct_c, &obj_struct, exp_err);

        /* Basic string */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(NULL, &obj_basic_string.j, "j", "j\"quote.txt");
        T_OK(tstiop__my_struct_a_opt, &obj_basic_string);

        /* String array */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(NULL, &obj_string_array.a.tab[0], "a[0]", "a0.txt");
        ADD_SUB_FILE(NULL, &obj_string_array.a.tab[2], "a[2]", "a2.txt");
        ADD_SUB_FILE(NULL, &obj_string_array.b.tab[1], "b[1]", "b1.txt");
        T_OK(tstiop__my_struct_f, &obj_string_array);

        /* Struct */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_struct.b, "b", "b.json");
        ADD_SUB_FILE(&tstiop__my_struct_c__s, &obj_struct.c.tab[1], "c[1]",
                     "c1.json");
        T_OK(tstiop__my_struct_c, &obj_struct);

        /* Union */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_union_a__s, &obj_union.b, "b", "b.json");
        T_OK(tstiop__my_struct_e, &obj_union);

        /* Class */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_class1__s, obj_class.e.tab[0], "e[0]",
                     "e0.json");
        ADD_SUB_FILE(&tstiop__my_class1__s, obj_class.f, "f",
                     "f.json");
        T_OK(tstiop__my_struct_f, &obj_class);

        /* Reference */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_referenced_struct__s, obj_ref.s, "s",
                     "s.json");
        ADD_SUB_FILE(&tstiop__my_referenced_union__s,  obj_ref.u, "u",
                     "u.json");
        T_OK(tstiop__my_ref_struct, &obj_ref);

        /* Recursive */
        CLEAR_SUB_FILES();
        Z_ASSERT_N(mkdir_p(t_fmt("%*pM/b1", LSTR_FMT_ARG(z_tmpdir_g)), 0755));
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_recursion.b, "b",
                     "b1/b.json");

        Z_ASSERT_N(mkdir_p(t_fmt("%*pM/b2", LSTR_FMT_ARG(z_tmpdir_g)), 0755));
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_recursion.b->b, "b.b",
                     "b2/b.json");
        T_OK(tstiop__my_struct_c, &obj_recursion);

        /* First field */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_struct_k__s, &obj_first_field.k, "k",
                     "k.json");
        ADD_SUB_FILE(&tstiop__my_struct_j__s, &obj_first_field.k.j, "k.j",
                     "j.json");
        T_OK(tstiop__my_struct_m, &obj_first_field);

        /* Dumping the exact same values in the same file twice is fine */
        /* For structs */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_struct.b, "b", "b.json");
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_struct.b->b, "b.b",
                     "c.json");
        ADD_SUB_FILE(&tstiop__my_struct_c__s, &obj_struct.c.tab[1], "c[1]",
                     "c.json");
        T_OK(tstiop__my_struct_c, &obj_struct);

        /* And for strings */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(NULL, &obj_string_array.a.tab[0], "a[0]", "s1.txt");
        ADD_SUB_FILE(NULL, &obj_string_array.a.tab[2], "a[2]", "s2.txt");
        ADD_SUB_FILE(NULL, &obj_string_array.b.tab[1], "b[1]", "s1.txt");
        T_OK(tstiop__my_struct_f, &obj_string_array);

        /* Dumping different types in the same file twice is not ok */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_referenced_struct__s, obj_ref.s, "s",
                     "s.json");
        ADD_SUB_FILE(&tstiop__my_referenced_union__s,  obj_ref.u, "u",
                     "s.json");
        exp_err = "subfile `s.json` is written twice with different iop "
                  "types `struct` vs `union`";
        T_KO(tstiop__my_ref_struct, &obj_ref, exp_err);

        /* Dumping different values in the same file twice is not ok */
        /* For structs */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_struct.b, "b", "c.json");
        ADD_SUB_FILE(&tstiop__my_struct_c__s, obj_struct.b->b, "b.b",
                     "b.json");
        ADD_SUB_FILE(&tstiop__my_struct_c__s, &obj_struct.c.tab[1], "c[1]",
                     "c.json");
        exp_err = "subfile `c.json` is written twice with different values";
        T_KO(tstiop__my_struct_c, &obj_struct, exp_err);

        /* And for strings */
        CLEAR_SUB_FILES();
        ADD_SUB_FILE(NULL, &obj_string_array.a.tab[0], "a[0]", "s1.txt");
        ADD_SUB_FILE(NULL, &obj_string_array.a.tab[2], "a[2]", "s1.txt");
        ADD_SUB_FILE(NULL, &obj_string_array.b.tab[1], "b[1]", "s2.txt");
        exp_err = "subfile `s1.txt` is written twice with different values";
        T_KO(tstiop__my_struct_f, &obj_string_array, exp_err);

#undef T
#undef T_OK
#undef T_KO
#undef ADD_SUB_FILE
#undef CLEAR_SUB_FILES
        /* }}} */

    } Z_TEST_END
    /* }}} */
    Z_TEST(std, "test IOP std (un)packer") { /* {{{ */
        t_scope;

        tstiop__my_class2__t cls2;

        tstiop__my_union_a__t un = IOP_UNION(tstiop__my_union_a, ua, 1);

        tstiop__my_struct_a__t sa = {
            .a = 42,
            .b = 5,
            .c_of_my_struct_a = 120,
            .d = 230,
            .e = 540,
            .f = 2000,
            .g = 10000,
            .h = 20000,
            .i = LSTR_IMMED("foo"),
            .j = LSTR_IMMED("baré© \" foo ."),
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, ub, 42),
            .lr = &un,
            .cls2 = &cls2,
            .m = 3.14159265,
            .n = true,
        };

        tstiop__my_struct_a__t sa2 = {
            .a = 42,
            .b = 5,
            .c_of_my_struct_a = 120,
            .d = 230,
            .e = 540,
            .f = 2000,
            .g = 10000,
            .h = 20000,
            .i = LSTR_EMPTY,
            .j = LSTR_EMPTY,
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, ub, 42),
            .lr = &un,
            .cls2 = &cls2,
            .m = 3.14159265,
            .n = true,
        };
        tstiop__my_struct_a_opt__t sa_opt;

        int32_t val[] = {15, 30, 45};
        tstiop__my_struct_e__t se = {
            .a = 10,
            .b = IOP_UNION(tstiop__my_union_a, ua, 42),
            .c = { .b = IOP_ARRAY(val, countof(val)), },
        };

        iop_dso_t *dso;


        const iop_struct_t *st_sa, *st_sa_opt, *st_se, *st_cls2;


        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_sa = iop_dso_find_type(dso, LSTR("tstiop.MyStructA")));
        Z_ASSERT_P(st_sa_opt = iop_dso_find_type(dso, LSTR("tstiop.MyStructAOpt")));
        Z_ASSERT_P(st_se = iop_dso_find_type(dso, LSTR("tstiop.MyStructE")));
        Z_ASSERT_P(st_cls2 = iop_dso_find_type(dso, LSTR("tstiop.MyClass2")));

        iop_init_desc(st_cls2, &cls2);

        Z_ASSERT_N(iop_check_constraints_desc(st_sa, &sa));
        Z_ASSERT_N(iop_check_constraints_desc(st_sa, &sa2));

        Z_HELPER_RUN(iop_std_test_struct(st_sa, &sa,  "sa"));
        Z_HELPER_RUN(iop_std_test_struct(st_sa, &sa2, "sa2"));
        Z_HELPER_RUN(iop_std_test_struct(st_se, &se, "se"));

        iop_init_desc(st_sa_opt, &sa_opt);
        OPT_SET(sa_opt.a, 32);
        sa_opt.j = LSTR("foo");
        Z_HELPER_RUN(iop_std_test_struct(st_sa_opt, &sa_opt, "sa_opt"));

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(big_array_parallel, "test big array packing") { /* {{{ */
        tstiop__my_struct_f__t sf;
        tstiop__my_struct_b__t arr[100000];
        tstiop__my_class1__t *arr2[100000];
        tstiop__my_class2__t cl1;

        iop_init(tstiop__my_class2, &cl1);
        cl1.int1 = 123;
        cl1.int2 = 4567;

        iop_init(tstiop__my_struct_f, &sf);
        for (int i = 0; i < 100000; i++) {
            iop_init(tstiop__my_struct_b, &arr[i]);
            OPT_SET(arr[i].a, 123);
            arr2[i] = iop_obj_vcast(tstiop__my_class1, &cl1);
        }
        sf.c = IOP_TYPED_ARRAY(tstiop__my_struct_b, arr, 100000);
        sf.e = IOP_TYPED_ARRAY(tstiop__my_class1, arr2, 100000);

        Z_HELPER_RUN(iop_std_test_struct(&tstiop__my_struct_f__s,
                                         &sf, "big_arr"));

        iop_std_test_speed(&tstiop__my_struct_f__s, &sf, 100, 0, "big arr");
    } Z_TEST_END;
    /* }}} */
    Z_TEST(roptimized, "test IOP std: optimized repeated fields") { /* {{{ */
        t_scope;
        lstr_t path_curr_v;
        lstr_t path_v3;

        path_curr_v = t_lstr_fmt("%*pM/iop/zchk-tstiop-plugin" SO_FILEEXT,
                                 LSTR_FMT_ARG(z_cmddir_g));

        path_v3 = t_lstr_fmt("%*pM/test-data/test_v3_centos-5u4/"
                             "zchk-tstiop-plugin" SO_FILEEXT,
                             LSTR_FMT_ARG(z_cmddir_g));

        Z_HELPER_RUN(iop_check_retro_compat_roptimized(path_curr_v));
        Z_HELPER_RUN(iop_check_retro_compat_roptimized(path_v3));
    } Z_TEST_END
    /* }}} */
    Z_TEST(defval, "test IOP std: do not pack default values") { /* {{{ */
        t_scope;

        iop_dso_t *dso;
        tstiop__my_struct_g__t sg;
        const iop_struct_t *st_sg;
        qv_t(i32) szs;
        int len;
        lstr_t s;
        const unsigned flags = IOP_BPACK_SKIP_DEFVAL;

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_sg = iop_dso_find_type(dso, LSTR("tstiop.MyStructG")));

        t_qv_init(&szs, 1024);

        /* test with all the default values */
        iop_init_desc(st_sg, &sg);
        Z_ASSERT_EQ((len = iop_bpack_size_flags(st_sg, &sg, flags, &szs)), 0,
                    "sg-empty");
        Z_HELPER_RUN(iop_std_test_struct_flags(st_sg, &sg, flags,
                                               "sg-empty"));

        /* check that t_iop_bpack returns LSTR_EMPTY_V and not LSTR_NULL_V */
        s = t_iop_bpack_struct_flags(st_sg, &sg, flags);
        Z_ASSERT_P(s.s);
        Z_ASSERT_ZERO(s.len);

        /* test with a different string length */
        sg.j.len = sg.j.len - 1;
        Z_ASSERT_EQ((len = iop_bpack_size_flags(st_sg, &sg, flags, &szs)), 15,
                    "sg-string-len-diff");
        Z_HELPER_RUN(iop_std_test_struct_flags(st_sg, &sg, flags,
                                               "sg-string-len-diff"));

        /* test with a NULL string */
        sg.j = LSTR_NULL_V;
        Z_ASSERT_EQ((len = iop_bpack_size_flags(st_sg, &sg, flags, &szs)), 0,
                    "sg-string-null");

        /* test with a different string */
        sg.j = LSTR("plop");
        Z_ASSERT_EQ((len = iop_bpack_size_flags(st_sg, &sg, flags, &szs)), 7,
                    "sg-string-diff");
        Z_HELPER_RUN(iop_std_test_struct_flags(st_sg, &sg, flags,
                                               "sg-string-diff"));

        /* test with different values at different places */
        sg.a = 42;
        sg.f = 12;
        sg.l = 10.6;
        Z_ASSERT_EQ((len = iop_bpack_size_flags(st_sg, &sg, flags, &szs)), 20,
                    "sg-diff");
        Z_HELPER_RUN(iop_std_test_struct_flags(st_sg, &sg, flags, "sg-diff"));

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(private, "test private attribute with binary packing") { /* {{{ */
        t_scope;
        void *out = NULL;
        tstiop_inheritance__c5__t c5;
        lstr_t bpacked;
        qv_t(i32) szs;

        iop_init(tstiop_inheritance__c5, &c5);
        bpacked = t_iop_bpack_struct(&tstiop_inheritance__c5__s, &c5);
        Z_ASSERT(bpacked.s);

        t_qv_init(&szs, 16);
        Z_ASSERT_NEG(iop_bunpack_ptr_flags(t_pool(), &tstiop_inheritance__c5__s,
                                           &out, ps_initlstr(&bpacked),
                                           IOP_UNPACK_FORBID_PRIVATE));
        Z_ASSERT(strstr(iop_get_err(),
                        "class `tstiop_inheritance.C5` is private"),
                 "%s", iop_get_err());
        Z_ASSERT_N(iop_bunpack_ptr_flags(t_pool(), &tstiop_inheritance__c5__s,
                                         &out, ps_initlstr(&bpacked), 0));
    } Z_TEST_END;
    /* }}} */
    Z_TEST(equals_and_cmp, "test iop_equals()/iop_cmp()") { /* {{{ */
#define CHECK_IOP_GT(st, lhs, rhs, ...)                                      \
    Z_HELPER_RUN(z_assert_iop_gt_desc((st), (lhs), (rhs)), ##__VA_ARGS__)

#define CHECK_IOP_LT(st, lhs, rhs, ...)                                      \
    Z_HELPER_RUN(z_assert_iop_lt_desc((st), (lhs), (rhs)), ##__VA_ARGS__)

#define CHECK_IOP_EQ(st, lhs, rhs, ...)                                      \
    Z_HELPER_RUN(z_assert_iop_eq_desc((st), (lhs), (rhs)), ##__VA_ARGS__)

        t_scope;

        tstiop__my_struct_g__t sg_a, sg_b;
        tstiop__my_struct_a_opt__t sa_opt_a, sa_opt_b;
        tstiop__my_union_a__t ua_a, ua_b;
        tstiop__repeated__t sr_a, sr_b;
        tstiop__void__t v_a, v_b;

        iop_dso_t *dso;
        const iop_struct_t *st_sg, *st_sa_opt, *st_ua, *st_sr;

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_sg = iop_dso_find_type(dso, LSTR("tstiop.MyStructG")));
        Z_ASSERT_P(st_sr = iop_dso_find_type(dso, LSTR("tstiop.Repeated")));
        Z_ASSERT_P(st_sa_opt = iop_dso_find_type(dso, LSTR("tstiop.MyStructAOpt")));
        Z_ASSERT_P(st_ua = iop_dso_find_type(dso, LSTR("tstiop.MyUnionA")));

        /* Test with all the default values */
        iop_init_desc(st_sg, &sg_a);
        iop_init_desc(st_sg, &sg_b);
        CHECK_IOP_EQ(st_sg, &sg_a, &sg_b);

        /* Change some fields and test */
        sg_a.b++;
        CHECK_IOP_GT(st_sg, &sg_a, &sg_b);

        sg_a.b--;
        sg_b.j = LSTR("not equal");
        CHECK_IOP_LT(st_sg, &sg_a, &sg_b);

        /* test with bytes */
        sg_b = sg_a;
        sg_a.i = LSTR("aa");
        sg_b.i = LSTR("Az");
        CHECK_IOP_GT(st_sg, &sg_a, &sg_b, "expected binary comparison");

        /* Use a more complex structure */
        iop_init_desc(st_sa_opt, &sa_opt_a);
        iop_init_desc(st_sa_opt, &sa_opt_b);
        CHECK_IOP_EQ(st_sa_opt, &sa_opt_a, &sa_opt_b);

        /* Change optional void field. */
        sa_opt_a.w = true;
        CHECK_IOP_GT(st_sa_opt, &sa_opt_a, &sa_opt_b);
        sa_opt_b.w = true;

        OPT_SET(sa_opt_a.a, 42);
        OPT_SET(sa_opt_b.a, 42);
        sa_opt_a.j = LSTR("plop");
        sa_opt_b.j = LSTR("plop");
        CHECK_IOP_EQ(st_sa_opt, &sa_opt_a, &sa_opt_b);

        OPT_CLR(sa_opt_b.a);
        CHECK_IOP_GT(st_sa_opt, &sa_opt_a, &sa_opt_b);

        OPT_SET(sa_opt_b.a, 42);
        sa_opt_b.j = LSTR_NULL_V;
        CHECK_IOP_GT(st_sa_opt, &sa_opt_a, &sa_opt_b);

        sa_opt_b.j = LSTR("plop2");
        CHECK_IOP_LT(st_sa_opt, &sa_opt_a, &sa_opt_b);

        sa_opt_b.j = LSTR("plop");
        ua_a = IOP_UNION(tstiop__my_union_a, ua, 1);
        ua_b = IOP_UNION(tstiop__my_union_a, ua, 1);
        sa_opt_a.l = &ua_a;
        sa_opt_b.l = &ua_b;
        CHECK_IOP_EQ(st_sa_opt, &sa_opt_a, &sa_opt_b);

        sa_opt_b.l = NULL;
        CHECK_IOP_GT(st_sa_opt, &sa_opt_a, &sa_opt_b);

        ua_b = IOP_UNION(tstiop__my_union_a, ub, 1);
        sa_opt_b.l = &ua_b;
        CHECK_IOP_LT(st_sa_opt, &sa_opt_a, &sa_opt_b);

        /* test with non initialized optional fields values */
        iop_init_desc(st_sa_opt, &sa_opt_a);
        iop_init_desc(st_sa_opt, &sa_opt_b);
        sa_opt_a.a.v = 42;
        CHECK_IOP_EQ(st_sa_opt, &sa_opt_a, &sa_opt_b);

        /* Now test with some arrays */
        {
            lstr_t strs[] = { LSTR_IMMED("a"), LSTR_IMMED("b") };
            uint8_t uints[] = { 1, 2, 3, 4 };
            uint8_t uints2[] = { 1, 2, 4, 4 };
            tstiop__full_repeated__t st1;
            tstiop__full_repeated__t st2;

            iop_init_desc(st_sr, &sr_a);
            iop_init_desc(st_sr, &sr_b);
            CHECK_IOP_EQ(st_sr, &sr_a, &sr_b);

            sr_a.s.tab = strs;
            sr_a.s.len = countof(strs);
            sr_b.s.tab = strs;
            sr_b.s.len = countof(strs);
            sr_a.u8.tab = uints;
            sr_a.u8.len = countof(uints);
            sr_b.u8.tab = uints;
            sr_b.u8.len = countof(uints);
            CHECK_IOP_EQ(st_sr, &sr_a, &sr_b);

            sr_b.s.len--;
            CHECK_IOP_GT(st_sr, &sr_a, &sr_b);
            sr_b.s.len++;

            sr_b.u8.len--;
            CHECK_IOP_GT(st_sr, &sr_a, &sr_b);
            sr_b.u8.len++;

            sr_b.u8.tab = uints2;
            CHECK_IOP_LT(st_sr, &sr_a, &sr_b);

            iop_init(tstiop__full_repeated, &st1);
            iop_init(tstiop__full_repeated, &st2);
            st1.s = T_IOP_ARRAY(lstr, LSTR("abc"), LSTR("dez"));
            st2.s = T_IOP_ARRAY(lstr, LSTR("abc"), LSTR("def"), LSTR("ghij"));
            CHECK_IOP_GT(tstiop__full_repeated__sp, &st1, &st2);
            st1.s.tab[1] = LSTR("dea");
            CHECK_IOP_LT(tstiop__full_repeated__sp, &st1, &st2);
            st1.s.tab[1] = st2.s.tab[1];
            CHECK_IOP_LT(tstiop__full_repeated__sp, &st1, &st2);
        }

        /* An empty struct has only one representation, so iop_equals should
         * always return true. */
        iop_init(tstiop__void, &v_a);
        iop_init(tstiop__void, &v_b);
        CHECK_IOP_EQ(&tstiop__void__s, NULL, NULL);
        CHECK_IOP_EQ(&tstiop__void__s, NULL, &v_a);
        CHECK_IOP_EQ(&tstiop__void__s, &v_a, NULL);
        CHECK_IOP_EQ(&tstiop__void__s, &v_a, &v_b);

        iop_dso_close(&dso);

#undef CHECK_IOP_EQ
#undef CHECK_IOP_GT
#undef CHECK_IOP_LT
    } Z_TEST_END
    /* }}} */
    Z_TEST(nr_61968, "non-regression test for bug with object comparison") { /* {{{ */
        tstiop__bob__t bob1;
        tstiop__bob__t bob2;

        iop_init(tstiop__bob, &bob1);
        bob1.i = 1;
        iop_init(tstiop__bob, &bob2);
        bob2.i = 2;

        Z_ASSERT_LT(iop_cmp(tstiop__alice, &bob1.super, &bob2.super), 0);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(xsort_and_xpsort, "test iop_xsort()/iop_xpsort()") { /* {{{ */
        t_scope;
        tstiop__xsort_struct__array_t array;
        const tstiop__xsort_struct__t **parray;

#define XSORT_ST(_a, _s)  (tstiop__xsort_struct__t){ .a = _a, .s = LSTR(_s) }

        array = T_IOP_ARRAY(tstiop__xsort_struct, XSORT_ST(42, "abc"),
                            XSORT_ST(42, "aaaa"), XSORT_ST(1, "toto"));

        iop_xsort(tstiop__xsort_struct, array.tab, array.len);
        for (int i = 0; i < array.len - 1; i++) {
            Z_ASSERT_LT(iop_cmp(tstiop__xsort_struct, &array.tab[i],
                                &array.tab[i + 1]), 0);
        }

        array = T_IOP_ARRAY(tstiop__xsort_struct, XSORT_ST(51, "abc"),
                            XSORT_ST(42, "tutu"), XSORT_ST(51, "zzz"),
                            XSORT_ST(21, "lala"));
        parray = t_new_raw(const tstiop__xsort_struct__t *, array.len);
        tab_enumerate_ptr(pos, xs, &array) {
            parray[pos] = xs;
        }
        iop_xpsort(tstiop__xsort_struct, parray, array.len);
        for (int i = 0; i < array.len - 1; i++) {
            Z_ASSERT_LT(iop_cmp(tstiop__xsort_struct, parray[i],
                                parray[i + 1]), 0);
        }

#undef XSORT_ST
    } Z_TEST_END;
    /* }}} */
    Z_TEST(strict_enum, "test IOP strict enum (un)packing") { /* {{{ */
        t_scope;

        tstiop__my_enum_b__t bvals[] = {
            MY_ENUM_B_A, MY_ENUM_B_B, MY_ENUM_B_C
        };

        tstiop__my_struct_l__t sl1 = {
            .a      = MY_ENUM_A_A,
            .b      = MY_ENUM_B_B,
            .btab   = IOP_ARRAY(bvals, countof(bvals)),
            .c      = MY_ENUM_C_A | MY_ENUM_C_B,
        };

        tstiop__my_struct_l__t sl2 = {
            .a      = 10,
            .b      = MY_ENUM_B_B,
            .btab   = IOP_ARRAY(bvals, countof(bvals)),
            .c      = MY_ENUM_C_A | MY_ENUM_C_B,
        };

        tstiop__my_struct_l__t sl3 = {
            .a      = MY_ENUM_A_A,
            .b      = 10,
            .btab   = IOP_ARRAY(bvals, countof(bvals)),
            .c      = MY_ENUM_C_A | MY_ENUM_C_B,
        };

        const char json_sl_p1[] =
            "{\n"
            "     a     = 1 << \"C\";               \n"
            "     b     = \"C\";                    \n"
            "     btab  = [ \"A\", \"B\", \"C\" ];  \n"
            "     c     = 1 << \"C\";               \n"
            "};\n";

        const char json_sl_n1[] =
            "{\n"
            "     a     = 1 << \"C\";               \n"
            "     b     = 1 << \"C\";               \n"
            "     btab  = [ \"A\", \"B\", \"C\" ];  \n"
            "     c     = 1 << \"C\";               \n"
            "};\n";

        iop_dso_t *dso;
        const iop_struct_t *st_sl;

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_sl = iop_dso_find_type(dso, LSTR("tstiop.MyStructL")));

        Z_ASSERT_N(iop_check_constraints_desc(st_sl, &sl1));
        Z_ASSERT_N(iop_check_constraints(tstiop__my_struct_l, &sl2));
        Z_ASSERT_NEG(iop_check_constraints_desc(st_sl, &sl3));

        Z_HELPER_RUN(iop_std_test_struct(st_sl, &sl1, "sl1"));
        Z_HELPER_RUN(iop_std_test_struct(st_sl, &sl2, "sl2"));
        Z_HELPER_RUN(iop_std_test_struct_invalid(st_sl, &sl3, "sl3",
                     "in type tstiop.MyStructL: 10 is not a valid value for "
                     "enum tstiop.MyEnumB (field b)"));

        Z_HELPER_RUN(iop_xml_test_struct(st_sl, &sl1, "sl1"));
        Z_HELPER_RUN(iop_xml_test_struct(st_sl, &sl2, "sl2"));
        Z_HELPER_RUN(iop_xml_test_struct_invalid(st_sl, &sl3, "sl3"));

        Z_HELPER_RUN(iop_json_test_unpack(st_sl, json_sl_p1,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          true, "json_sl_p1"));
        Z_HELPER_RUN(iop_json_test_unpack(st_sl, json_sl_n1,
                                          IOP_UNPACK_IGNORE_UNKNOWN,
                                          false, "json_sl_n1"));

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(constraints, "test IOP constraints") { /* {{{ */
        t_scope;

        tstiop__constraint_u__t u;
        tstiop__constraint_s__t s, s1, s2;
        tstiop_inheritance__c1__t c;

        lstr_t strings[] = {
            LSTR("fooBAR_1"),
            LSTR("foobar_2"),
            LSTR("foo3"),
            LSTR("foo4"),
            LSTR("foo5"),
            LSTR("foo6"),
        };

        lstr_t bad_strings[] = {
            LSTR("abcd[]"),
            LSTR("a b c"),
        };

        int8_t   i8tab[] = { INT8_MIN,  INT8_MAX,  3, 4, 5, 6 };
        int16_t i16tab[] = { INT16_MIN, INT16_MAX, 3, 4, 5, 6 };
        int32_t i32tab[] = { INT32_MIN, INT32_MAX, 3, 4, 5, 6 };
        int64_t i64tab[] = { INT64_MIN, INT64_MAX, 3, 4, 5, 6 };

        iop_dso_t *dso;
        const iop_struct_t *st_s, *st_u, *st_c;

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_s = iop_dso_find_type(dso, LSTR("tstiop.ConstraintS")));
        Z_ASSERT_P(st_u = iop_dso_find_type(dso, LSTR("tstiop.ConstraintU")));
        Z_ASSERT_P(st_c = iop_dso_find_type(dso,
                                            LSTR("tstiop_inheritance.C1")));

#define CHECK_VALID(st, v, info) \
        Z_ASSERT_N(iop_check_constraints_desc((st), (v)));              \
        Z_HELPER_RUN(iop_std_test_struct((st), (v), (info)));           \
        Z_HELPER_RUN(iop_xml_test_struct((st), (v), (info)));           \
        Z_HELPER_RUN(iop_json_test_struct((st), (v), (info)));

#define CHECK_INVALID(st, v, info, err) \
        Z_ASSERT_NEG(iop_check_constraints_desc((st), (v)));                 \
        Z_HELPER_RUN(iop_std_test_struct_invalid((st), (v), (info), (err))); \
        Z_HELPER_RUN(iop_xml_test_struct_invalid((st), (v), (info)));        \
        Z_HELPER_RUN(iop_json_test_struct_invalid((st), (v), (info)));

#define CHECK_UNION(f, size) \
        u = IOP_UNION(tstiop__constraint_u, f, 1L << (size - 1));           \
        CHECK_VALID(st_u, &u, #f);                                          \
        u = IOP_UNION(tstiop__constraint_u, f, 1 + (1L << (size - 1)));     \
        CHECK_INVALID(st_u, &u, #f "_max",                                  \
                      t_fmt("in type tstiop.ConstraintU: violation of "     \
                            "constraint max (%ju) on field %s: val=%ju",    \
                            1L << (size - 1), #f, 1 + (1L << (size - 1)))); \
        u = IOP_UNION(tstiop__constraint_u, f, 0);                          \
        CHECK_INVALID(st_u, &u, #f "_zero",                                 \
                      t_fmt("in type tstiop.ConstraintU: violation of "     \
                            "constraint nonZero on field %s", #f));         \

        iop_init_desc(st_u, &u);
        CHECK_UNION(u8,   8);
        CHECK_UNION(u16, 16);
        CHECK_UNION(u32, 32);
        CHECK_UNION(u64, 64);

        u = IOP_UNION(tstiop__constraint_u, s, LSTR_EMPTY_V);
        CHECK_INVALID(st_u, &u, "s_empty",
                      "in type tstiop.ConstraintU: violation of constraint "
                      "nonEmpty on field s");
        u = IOP_UNION(tstiop__constraint_u, s, LSTR_NULL_V);
        CHECK_INVALID(st_u, &u, "s_null",
                      "in type tstiop.ConstraintU: violation of constraint "
                      "nonEmpty on field s");
        u = IOP_UNION(tstiop__constraint_u, s, LSTR("way_too_long"));
        CHECK_INVALID(st_u, &u, "s_maxlength",
                      "in type tstiop.ConstraintU: violation of constraint "
                      "maxLength (10) on field s: length=12");
        u = IOP_UNION(tstiop__constraint_u, s, LSTR("ab.{}[]"));
        CHECK_INVALID(st_u, &u, "s_pattern",
                      "in type tstiop.ConstraintU: violation of constraint "
                      "pattern ([^\\[\\]]*) on field s: ab.{}[]");
        u = IOP_UNION(tstiop__constraint_u, s, LSTR("ab.{}()"));
        CHECK_VALID(st_u, &u, "s");

        iop_init_desc(st_s, &s);
        CHECK_INVALID(st_s, &s, "s_minoccurs",
                      "in type tstiop.ConstraintS: empty array not allowed "
                      "for field `s`");

        s.s.tab = bad_strings;
        s.s.len = countof(bad_strings);
        CHECK_INVALID(st_s, &s, "s_pattern",
                      "in type tstiop.ConstraintS: violation of constraint "
                      "pattern ([a-zA-Z0-9_\\-]*) on field s: abcd[]");

        s.s.tab = strings;
        s.s.len = 1;
        CHECK_INVALID(st_s, &s, "s_minoccurs",
                      "in type tstiop.ConstraintS: violation of constraint "
                      "minOccurs (2) on field s: length=1");
        s.s.len = countof(strings);
        CHECK_INVALID(st_s, &s, "s_maxoccurs",
                      "in type tstiop.ConstraintS: violation of constraint "
                      "maxOccurs (5) on field s: length=6");
        s.s.len = 2;
        CHECK_VALID(st_s, &s, "s");
        s.s.len = 5;
        CHECK_VALID(st_s, &s, "s");

        iop_init_desc(st_s, &s);
        iop_init_desc(st_s, &s1);
        iop_init_desc(st_s, &s2);
        s.s.tab = strings;
        s.s.len = 5;
        s.tab.tab = &s1;
        s.tab.len = 1;
        s1.s.tab = strings;
        s1.s.len = 5;
        s1.tab.tab = &s2;
        s1.tab.len = 1;
        s2.s.tab = strings;
        s2.s.len = 6;
        CHECK_INVALID(st_s, &s, "s_maxoccurs",
                      "in tab[0].tab[0] of type tstiop.ConstraintS: violation"
                      " of constraint maxOccurs (5) on field s: length=6");

        u = IOP_UNION(tstiop__constraint_u, cs, s);
        CHECK_INVALID(st_u, &u, "s_maxoccurs",
                "in cs.tab[0].tab[0] of type tstiop.ConstraintS: violation"
                " of constraint maxOccurs (5) on field s: length=6");

#define CHECK_TAB(_f, _tab)                                                  \
        s._f.tab = _tab;                                                     \
        s._f.len = 6;                                                        \
        CHECK_INVALID(st_s, &s, "s",                                         \
                      t_fmt("in type tstiop.ConstraintS: violation of "      \
                            "constraint maxOccurs (5) on field %s: length=6",\
                            #_f));                                           \
        s._f.len = 5;                                                        \
        CHECK_INVALID(st_s, &s, "s",                                         \
                      t_fmt("in type tstiop.ConstraintS: violation of "      \
                            "constraint min (%jd) on field %s[0]: val=%jd",  \
                            (int64_t)_tab[0] + 1, #_f, (int64_t)_tab[0]));   \
        s._f.tab[0]++;                                                       \
        CHECK_VALID(st_s, &s, "s");

        s2.s.len = 5;
        CHECK_TAB(i8,   i8tab);
        CHECK_TAB(i16,  i16tab);
        CHECK_TAB(i32,  i32tab);
        CHECK_TAB(i64,  i64tab);

        /* With inheritance */
        iop_init_desc(st_c, &c);
        CHECK_VALID(st_c, &c, "c");
        c.a = 0;
        CHECK_INVALID(st_c, &c, "c",
                      "in type tstiop_inheritance.A1: violation of constraint"
                      " nonZero on field a");
        c.a = 2;
        c.c = 0;
        CHECK_INVALID(st_c, &c, "c",
                      "in type tstiop_inheritance.C1: violation of constraint"
                      " nonZero on field c");
        c.c = 3;
        CHECK_VALID(st_c, &c, "c");

#undef CHECK_TAB
#undef CHECK_UNION
#undef CHECK_VALID
#undef CHECK_INVALID

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_sort, "test IOP structures/unions sorting") { /* {{{ */
        t_scope;
        qv_t(my_struct_a) vec;
        tstiop__my_union_a__t un[5];
        tstiop__my_struct_a__t a;
        qv_t(my_struct_a_opt) vec2;
        tstiop__my_struct_a_opt__t a2;
        tstiop__my_struct_b__t b1, b2;
        tstiop__my_struct_m__t m;
        tstiop__my_class2__t cls2;
        tstiop__my_class3__t cls3;
        qv_t(my_struct_m) mvec;
        qv_t(my_class2) cls2_vec;
        qv_t(my_struct_f) fvec;
        tstiop__my_struct_f__t *fst;

        qv_init(&vec);
        iop_init(tstiop__my_struct_a, &a);
        iop_init(tstiop__my_class2, &cls2);
        iop_init(tstiop__my_class3, &cls3);

        un[0] = IOP_UNION(tstiop__my_union_a, ub, 42);
        a.e = 1;
        a.j = LSTR("xyz");
        a.l = IOP_UNION(tstiop__my_union_a, ua, 111);
        a.lr = &un[0];
        a.htab = T_IOP_ARRAY(u64, 3, 2, 1);
        cls3.int1 = 10;
        cls3.int2 = 100;
        cls3.int3 = 1000;
        a.cls2 = t_iop_dup(tstiop__my_class2, &cls3.super);
        qv_append(&vec, a);

        un[1] = IOP_UNION(tstiop__my_union_a, ub, 23);
        a.e = 2;
        a.j = LSTR("abc");
        a.l = IOP_UNION(tstiop__my_union_a, ua, 666);
        a.lr = &un[1];
        a.htab = T_IOP_ARRAY(u64, 3, 2, 2);
        cls2.int1 = 15;
        cls2.int2 = 95;
        a.cls2 = t_iop_dup(tstiop__my_class2, &cls2);
        qv_append(&vec, a);

        un[2] = IOP_UNION(tstiop__my_union_a, ua, 222);
        a.e = 3;
        a.j = LSTR("Jkl");
        a.l = IOP_UNION(tstiop__my_union_a, ua, 222);
        a.lr = &un[2];
        a.htab = T_IOP_ARRAY(u64, 1, 2);
        cls3.int1 = 13;
        cls3.int2 = 98;
        cls3.int3 = 1000;
        a.cls2 = t_iop_dup(tstiop__my_class2, &cls3.super);
        qv_append(&vec, a);

        un[3] = IOP_UNION(tstiop__my_union_a, ua, 666);
        a.e = 3;
        a.j = LSTR("jKl");
        a.l = IOP_UNION(tstiop__my_union_a, ub, 23);
        a.lr = &un[3];
        a.htab = T_IOP_ARRAY(u64, 1, 2, 3, 4);
        cls2.int1 = 14;
        cls2.int2 = 96;
        a.cls2 = t_iop_dup(tstiop__my_class2, &cls2);
        qv_append(&vec, a);

        un[4] = IOP_UNION(tstiop__my_union_a, ua, 111);
        a.e = 3;
        a.j = LSTR("jkL");
        a.l = IOP_UNION(tstiop__my_union_a, ub, 42);
        a.lr = &un[4];
        a.htab = T_IOP_ARRAY(u64, 4);
        cls2.int1 = 16;
        cls2.int2 = 97;
        a.cls2 = t_iop_dup(tstiop__my_class2, &cls2);
        qv_append(&vec, a);

#define TST_SORT_VEC(p, f)  \
        iop_sort(tstiop__my_struct_a, vec.tab, vec.len, p, f, NULL)

        /* reverse sort on short e */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("e"), IOP_SORT_REVERSE));
        Z_ASSERT_EQ(vec.tab[0].e, 3);
        Z_ASSERT_EQ(vec.tab[4].e, 1);

        /* sort on string j */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("j"), 0));
        Z_ASSERT_LSTREQUAL(vec.tab[0].j, LSTR("abc"));
        Z_ASSERT_LSTREQUAL(vec.tab[4].j, LSTR("xyz"));

        /* sort on union l */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l"), 0));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[0].l, ua));
        Z_ASSERT_EQ(vec.tab[0].l.ua, 111);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[1].l, ua));
        Z_ASSERT_EQ(vec.tab[1].l.ua, 222);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[2].l, ua));
        Z_ASSERT_EQ(vec.tab[2].l.ua, 666);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[3].l, ub));
        Z_ASSERT_EQ(vec.tab[3].l.ub, 23);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[4].l, ub));
        Z_ASSERT_EQ(vec.tab[4].l.ub, 42);

        /* sort on int ua, member of union l */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l.ua"), 0));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[0].l, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[1].l, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[2].l, ua));
        Z_ASSERT_EQ(vec.tab[0].l.ua, 111);
        Z_ASSERT_EQ(vec.tab[1].l.ua, 222);
        Z_ASSERT_EQ(vec.tab[2].l.ua, 666);

        /* reverse sort on int ua, member of union l */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l.ua"), IOP_SORT_REVERSE));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[0].l, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[1].l, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[2].l, ua));
        Z_ASSERT_EQ(vec.tab[0].l.ua, 666);
        Z_ASSERT_EQ(vec.tab[1].l.ua, 222);
        Z_ASSERT_EQ(vec.tab[2].l.ua, 111);

        /* sort on int ua, member of union l, put other union members first */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l.ua"), IOP_SORT_NULL_FIRST));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[0].l, ub));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[1].l, ub));
        Z_ASSERT_EQ(vec.tab[2].l.ua, 111);
        Z_ASSERT_EQ(vec.tab[3].l.ua, 222);
        Z_ASSERT_EQ(vec.tab[4].l.ua, 666);

        /* reverse sort on int ua, member of union l, put other union members
         * first */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l.ua"),
                                IOP_SORT_NULL_FIRST | IOP_SORT_REVERSE));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[0].l, ub));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[1].l, ub));
        Z_ASSERT_EQ(vec.tab[2].l.ua, 666);
        Z_ASSERT_EQ(vec.tab[3].l.ua, 222);
        Z_ASSERT_EQ(vec.tab[4].l.ua, 111);

        /* sort on byte ub, member of union l, put other union members first */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l.ub"), IOP_SORT_NULL_FIRST));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[0].l, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[1].l, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, &vec.tab[2].l, ua));
        Z_ASSERT_EQ(vec.tab[3].l.ua, 23);
        Z_ASSERT_EQ(vec.tab[4].l.ua, 42);

        /* sort on union lr */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("lr"), 0));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[0].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[4].lr, ub));

        /* sort on int ua, member of union lr */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("lr.ua"), 0));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[0].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[1].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[2].lr, ua));
        Z_ASSERT_EQ(vec.tab[0].lr->ua, 111);
        Z_ASSERT_EQ(vec.tab[1].lr->ua, 222);
        Z_ASSERT_EQ(vec.tab[2].lr->ua, 666);

        /* reverse sort on int ua, member of union lr */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("lr.ua"), IOP_SORT_REVERSE));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[0].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[1].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[2].lr, ua));
        Z_ASSERT_EQ(vec.tab[0].lr->ua, 666);
        Z_ASSERT_EQ(vec.tab[1].lr->ua, 222);
        Z_ASSERT_EQ(vec.tab[2].lr->ua, 111);

        /* sort on int ua, member of union lr, put other union members first */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("lr.ua"), IOP_SORT_NULL_FIRST));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[0].lr, ub));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[1].lr, ub));
        Z_ASSERT_EQ(vec.tab[2].lr->ua, 111);
        Z_ASSERT_EQ(vec.tab[3].lr->ua, 222);
        Z_ASSERT_EQ(vec.tab[4].lr->ua, 666);

        /* reverse sort on int ua, member of union lr, put other union members
         * first */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("lr.ua"),
                                IOP_SORT_NULL_FIRST | IOP_SORT_REVERSE));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[0].lr, ub));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[1].lr, ub));
        Z_ASSERT_EQ(vec.tab[2].lr->ua, 666);
        Z_ASSERT_EQ(vec.tab[3].lr->ua, 222);
        Z_ASSERT_EQ(vec.tab[4].lr->ua, 111);

        /* sort on byte ub, member of union lr, put other union members first */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("lr.ub"), IOP_SORT_NULL_FIRST));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[0].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[1].lr, ua));
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_a, vec.tab[2].lr, ua));
        Z_ASSERT_EQ(vec.tab[3].lr->ua, 23);
        Z_ASSERT_EQ(vec.tab[4].lr->ua, 42);

        /* sort on class members */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("cls2.int1"), 0));
        Z_ASSERT_EQ(vec.tab[0].cls2->int1, 10);
        Z_ASSERT_EQ(vec.tab[1].cls2->int1, 13);
        Z_ASSERT_EQ(vec.tab[2].cls2->int1, 14);
        Z_ASSERT_EQ(vec.tab[3].cls2->int1, 15);
        Z_ASSERT_EQ(vec.tab[4].cls2->int1, 16);
        Z_ASSERT_N(TST_SORT_VEC(LSTR("cls2.int2"), 0));
        Z_ASSERT_EQ(vec.tab[0].cls2->int2, 95);
        Z_ASSERT_EQ(vec.tab[1].cls2->int2, 96);
        Z_ASSERT_EQ(vec.tab[2].cls2->int2, 97);
        Z_ASSERT_EQ(vec.tab[3].cls2->int2, 98);
        Z_ASSERT_EQ(vec.tab[4].cls2->int2, 100);

        /* sort on class name */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("cls2._class"), 0));
        Z_ASSERT_LSTREQUAL(vec.tab[0].cls2->__vptr->fullname,
                           LSTR("tstiop.MyClass2"));
        Z_ASSERT_LSTREQUAL(vec.tab[1].cls2->__vptr->fullname,
                           LSTR("tstiop.MyClass2"));
        Z_ASSERT_LSTREQUAL(vec.tab[2].cls2->__vptr->fullname,
                           LSTR("tstiop.MyClass2"));
        Z_ASSERT_LSTREQUAL(vec.tab[3].cls2->__vptr->fullname,
                           LSTR("tstiop.MyClass3"));
        Z_ASSERT_LSTREQUAL(vec.tab[4].cls2->__vptr->fullname,
                           LSTR("tstiop.MyClass3"));

        /* sort on repeated field */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("htab"), 0));
        Z_ASSERT_EQ(vec.tab[0].htab.tab[0], 1u);
        Z_ASSERT_EQ(vec.tab[0].htab.len, 2);
        Z_ASSERT_EQ(vec.tab[1].htab.tab[0], 1u);
        Z_ASSERT_EQ(vec.tab[2].htab.tab[0], 3u);
        Z_ASSERT_EQ(vec.tab[2].htab.tab[2], 1u);
        Z_ASSERT_EQ(vec.tab[3].htab.tab[0], 3u);
        Z_ASSERT_EQ(vec.tab[3].htab.tab[2], 2u);
        Z_ASSERT_EQ(vec.tab[4].htab.tab[0], 4u);

        /* sort on the length of a repeated field */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("htab.len"), 0));
        Z_ASSERT_EQ(vec.tab[0].htab.len, 1);
        Z_ASSERT_EQ(vec.tab[1].htab.len, 2);
        Z_ASSERT_EQ(vec.tab[2].htab.len, 3);
        Z_ASSERT_EQ(vec.tab[3].htab.len, 3);
        Z_ASSERT_EQ(vec.tab[4].htab.len, 4);

        /* sort on an element of a repeated field */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("htab[2]"), 0));
        Z_ASSERT_GE(vec.tab[0].htab.len, 3);
        Z_ASSERT_EQ(vec.tab[0].htab.tab[2], 1u);
        Z_ASSERT_GE(vec.tab[1].htab.len, 3);
        Z_ASSERT_EQ(vec.tab[1].htab.tab[2], 2u);
        Z_ASSERT_GE(vec.tab[2].htab.len, 3);
        Z_ASSERT_EQ(vec.tab[2].htab.tab[2], 3u);
        Z_ASSERT_LT(vec.tab[3].htab.len, 3);
        Z_ASSERT_LT(vec.tab[4].htab.len, 3);

        /* sort on the last element of a repeated field */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("htab[-1]"), 0));
        Z_ASSERT_EQ(vec.tab[0].htab.len, 3);
        Z_ASSERT_EQ(vec.tab[0].htab.tab[2], 1u);
        Z_ASSERT_EQ(*tab_last(&vec.tab[1].htab), 2u);
        Z_ASSERT_EQ(*tab_last(&vec.tab[2].htab), 2u);
        Z_ASSERT_EQ(*tab_last(&vec.tab[3].htab), 4u);
        Z_ASSERT_EQ(*tab_last(&vec.tab[4].htab), 4u);

        /* error: empty field path */
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR(""), 0));
        /* error: invalid field path */
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR("."), 0));
        /* error: bar field does not exist */
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR("bar"), 0));
        /* error: get class of non-class */
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR("_class"), 0));
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR("lr._class"), 0));
        /* error: get subfield of class */
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR("cls2._class.int2"), 0));
        /* error: cannot sort on required void field */
        Z_ASSERT_NEG(TST_SORT_VEC(LSTR("u"), 0));

        qv_wipe(&vec);
#undef TST_SORT_VEC

        qv_init(&vec2);
        iop_init(tstiop__my_struct_a_opt, &a2);

        qv_append(&vec2, a2);
        OPT_SET(a2.a, 42);
        qv_append(&vec2, a2);
        OPT_SET(a2.a, 43);
        a2.w = true;
        qv_append(&vec2, a2);
        OPT_CLR(a2.a);
        a2.w = false;
        a2.j = LSTR("abc");
        a2.l = &IOP_UNION(tstiop__my_union_a, ua, 222);
        qv_append(&vec2, a2);
        a2.j = LSTR("def");
        a2.l = &IOP_UNION(tstiop__my_union_a, ub, 222);
        qv_append(&vec2, a2);
        a2.l = &IOP_UNION(tstiop__my_union_a, us, LSTR("xyz"));
        qv_append(&vec2, a2);

        iop_init(tstiop__my_struct_b, &b1);
        OPT_SET(b1.a, 42);
        a2.o = &b1;
        qv_append(&vec2, a2);

        iop_init(tstiop__my_struct_b, &b2);
        OPT_SET(b2.a, 72);
        a2.o = &b2;
        qv_append(&vec2, a2);

#define TST_SORT_VEC(p, f)  \
        iop_sort(tstiop__my_struct_a_opt, vec2.tab, vec2.len, p, f, NULL)

        /* sort on optional int a */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("a"), 0));
        Z_ASSERT_EQ(OPT_VAL(vec2.tab[0].a), 42);
        Z_ASSERT_EQ(OPT_VAL(vec2.tab[1].a), 43);
        Z_ASSERT(!OPT_ISSET(vec2.tab[2].a));
        Z_ASSERT(!OPT_ISSET(vec2.tab[3].a));
        Z_ASSERT(!OPT_ISSET(vec2.tab[4].a));
        Z_ASSERT(!OPT_ISSET(vec2.tab[5].a));

        /* sort on optional string j */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("j"), 0));
        Z_ASSERT_LSTREQUAL(vec2.tab[0].j, LSTR("abc"));
        Z_ASSERT_LSTREQUAL(vec2.tab[1].j, LSTR("def"));

        /* sort on optional union l */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("l"), 0));
        Z_ASSERT_P(vec2.tab[0].l);
        Z_ASSERT_EQ(vec2.tab[0].l->ua, 222);

        /* sort on optional int a, member of optional struct MyStructB o */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("o.a"), 0));
        Z_ASSERT_EQ(OPT_VAL(vec2.tab[0].o->a), 42);
        Z_ASSERT_EQ(OPT_VAL(vec2.tab[1].o->a), 72);

        /* sort on optional void w */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("w"), 0));
        Z_ASSERT_EQ(OPT_VAL(vec2.tab[0].a), 43);
        Z_ASSERT(vec2.tab[0].w);
        Z_ASSERT(!vec2.tab[1].w);
        Z_ASSERT(!vec2.tab[2].w);
        Z_ASSERT(!vec2.tab[3].w);
        Z_ASSERT(!vec2.tab[4].w);
        Z_ASSERT(!vec2.tab[5].w);

        /* sort on struct */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("o"), 0));
        Z_ASSERT(vec2.tab[0].o == &b1);
        Z_ASSERT(vec2.tab[1].o == &b2);
        for (int i = 2; i < vec2.len; i++) {
            Z_ASSERT_NULL(vec2.tab[i].o);
        }

        qv_wipe(&vec2);
#undef TST_SORT_VEC

        qv_init(&mvec);
        iop_init(tstiop__my_struct_m, &m);

        m.k.j.cval = 5;
        m.k.j.b = IOP_UNION(tstiop__my_union_b, bval, 55);
        qv_append(&mvec, m);
        m.k.j.cval = 4;
        m.k.j.b = IOP_UNION(tstiop__my_union_b, bval, 44);
        qv_append(&mvec, m);
        m.k.j.cval = 3;
        m.k.j.b = IOP_UNION(tstiop__my_union_b, bval, 33);
        qv_append(&mvec, m);

#define TST_SORT_VEC(p, f)  \
        iop_sort(tstiop__my_struct_m, mvec.tab, mvec.len, p, f, NULL)

        /* sort on int cval from MyStructJ j from MyStructK k */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("k.j.cval"), 0));
        Z_ASSERT_EQ(mvec.tab[0].k.j.cval, 3);
        Z_ASSERT_EQ(mvec.tab[1].k.j.cval, 4);
        Z_ASSERT_EQ(mvec.tab[2].k.j.cval, 5);

        /* sort on int bval from MyUnionB b from MyStructJ j from MyStructK k
         */
        Z_ASSERT_N(TST_SORT_VEC(LSTR("k.j.b.bval"), 0));
        Z_ASSERT_EQ(mvec.tab[0].k.j.b.bval, 33);
        Z_ASSERT_EQ(mvec.tab[1].k.j.b.bval, 44);
        Z_ASSERT_EQ(mvec.tab[2].k.j.b.bval, 55);

        qv_wipe(&mvec);
#undef TST_SORT_VEC

        t_qv_init(&cls2_vec, 3);

        cls2.int1 = 3;
        cls2.int2 = 4;
        qv_append(&cls2_vec,
                  t_iop_dup(tstiop__my_class2, &cls2));
        cls2.int1 = 2;
        cls2.int2 = 5;
        qv_append(&cls2_vec,
                  t_iop_dup(tstiop__my_class2, &cls2));
        cls2.int1 = 1;
        cls2.int2 = 6;
        qv_append(&cls2_vec,
                  t_iop_dup(tstiop__my_class2, &cls2));

#define TST_SORT_VEC(p, f)  \
        iop_obj_sort(tstiop__my_class2, cls2_vec.tab, cls2_vec.len, p, f, NULL)

        Z_ASSERT_N(TST_SORT_VEC(LSTR("int1"), 0));
        Z_ASSERT_EQ(cls2_vec.tab[0]->int1, 1);
        Z_ASSERT_EQ(cls2_vec.tab[1]->int1, 2);
        Z_ASSERT_EQ(cls2_vec.tab[2]->int1, 3);

        Z_ASSERT_N(TST_SORT_VEC(LSTR("int2"), 0));
        Z_ASSERT_EQ(cls2_vec.tab[0]->int2, 4);
        Z_ASSERT_EQ(cls2_vec.tab[1]->int2, 5);
        Z_ASSERT_EQ(cls2_vec.tab[2]->int2, 6);
#undef TST_SORT_VEC

        t_qv_init(&fvec, 3);
        fst = iop_init(tstiop__my_struct_f, qv_growlen(&fvec, 1));
        fst->d = T_IOP_ARRAY(tstiop__my_union_a,
                             IOP_UNION(tstiop__my_union_a, ua, 2),
                             IOP_UNION(tstiop__my_union_a, ua, 3));
        fst->e = T_IOP_ARRAY(tstiop__my_class1,
                             t_iop_new(tstiop__my_class1),
                             t_iop_new(tstiop__my_class1));
        fst->e.tab[0]->int1 = 7;
        fst->e.tab[1]->int1 = 8;

        fst = iop_init(tstiop__my_struct_f, qv_growlen(&fvec, 1));
        fst->d = T_IOP_ARRAY(tstiop__my_union_a,
                             IOP_UNION(tstiop__my_union_a, ua, 1),
                             IOP_UNION(tstiop__my_union_a, ua, 4));
        fst->e = T_IOP_ARRAY(tstiop__my_class1,
                             t_iop_new(tstiop__my_class1));
        fst->e.tab[0]->int1 = 4;

        fst = iop_init(tstiop__my_struct_f, qv_growlen(&fvec, 1));
        fst->d = T_IOP_ARRAY(tstiop__my_union_a,
                             IOP_UNION(tstiop__my_union_a, ua, 3));
        fst->e = T_IOP_ARRAY(tstiop__my_class1,
                             t_iop_new(tstiop__my_class1),
                             t_iop_new(tstiop__my_class1),
                             t_iop_new(tstiop__my_class1));
        fst->e.tab[0]->int1 = 5;
        fst->e.tab[1]->int1 = 10;
        fst->e.tab[2]->int1 = 42;

#define TST_SORT_VEC(p, f)  \
        iop_sort(tstiop__my_struct_f, fvec.tab, fvec.len, LSTR(p), (f), NULL)

        Z_ASSERT_N(TST_SORT_VEC("d[0].ua", 0));
        Z_ASSERT_EQ(fvec.tab[0].d.tab[0].ua, 1);
        Z_ASSERT_EQ(fvec.tab[1].d.tab[0].ua, 2);
        Z_ASSERT_EQ(fvec.tab[2].d.tab[0].ua, 3);

        Z_ASSERT_N(TST_SORT_VEC("d[1].ua", 0));
        Z_ASSERT_EQ(fvec.tab[0].d.tab[1].ua, 3);
        Z_ASSERT_EQ(fvec.tab[1].d.tab[1].ua, 4);
        Z_ASSERT_EQ(fvec.tab[2].d.len, 1);

        Z_ASSERT_N(TST_SORT_VEC("d[-1].ua", 0));
        Z_ASSERT_EQ(tab_last(&fvec.tab[0].d)->ua, 3);
        Z_ASSERT_EQ(tab_last(&fvec.tab[1].d)->ua, 3);
        Z_ASSERT_EQ(tab_last(&fvec.tab[2].d)->ua, 4);

        Z_ASSERT_N(TST_SORT_VEC("d[-2].ua", 0));
        Z_ASSERT_EQ(fvec.tab[0].d.len, 2);
        Z_ASSERT_EQ(fvec.tab[0].d.tab[0].ua, 1);
        Z_ASSERT_EQ(fvec.tab[1].d.len, 2);
        Z_ASSERT_EQ(fvec.tab[1].d.tab[0].ua, 2);
        Z_ASSERT_EQ(fvec.tab[2].d.len, 1);

        Z_ASSERT_N(TST_SORT_VEC("d[0]", 0));
        Z_ASSERT_EQ(fvec.tab[0].d.tab[0].ua, 1);
        Z_ASSERT_EQ(fvec.tab[1].d.tab[0].ua, 2);
        Z_ASSERT_EQ(fvec.tab[2].d.tab[0].ua, 3);

        Z_ASSERT_N(TST_SORT_VEC("e[0].int1", 0));
        Z_ASSERT_EQ(fvec.tab[0].e.tab[0]->int1, 4);
        Z_ASSERT_EQ(fvec.tab[1].e.tab[0]->int1, 5);
        Z_ASSERT_EQ(fvec.tab[2].e.tab[0]->int1, 7);

        Z_ASSERT_N(TST_SORT_VEC("e[1].int1", 0));
        Z_ASSERT_EQ(fvec.tab[0].e.tab[1]->int1, 8);
        Z_ASSERT_EQ(fvec.tab[1].e.tab[1]->int1, 10);
        Z_ASSERT_EQ(fvec.tab[2].e.len, 1);

        Z_ASSERT_N(TST_SORT_VEC("e[2].int1", 0));
        Z_ASSERT_EQ(fvec.tab[0].e.tab[2]->int1, 42);
        Z_ASSERT_LT(fvec.tab[1].e.len, 3);
        Z_ASSERT_LT(fvec.tab[2].e.len, 3);

        Z_ASSERT_N(TST_SORT_VEC("e[-1].int1", 0));
        Z_ASSERT_EQ((*tab_last(&fvec.tab[0].e))->int1, 4);
        Z_ASSERT_EQ((*tab_last(&fvec.tab[1].e))->int1, 8);
        Z_ASSERT_EQ((*tab_last(&fvec.tab[2].e))->int1, 42);

#undef TST_SORT_VEC


    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_msort, "test IOP structures/unions multi sorting") { /* {{{ */
        t_scope;
        qv_t(my_struct_a) original;
        qv_t(my_struct_a) sorted;
        qv_t(iop_sort) params;

        t_qv_init(&original, 3);
        t_qv_init(&sorted, 3);
        t_qv_init(&params, 2);

        qv_growlen(&original, 3);
        iop_init(tstiop__my_struct_a, &original.tab[0]);
        iop_init(tstiop__my_struct_a, &original.tab[1]);
        iop_init(tstiop__my_struct_a, &original.tab[2]);

        original.tab[0].a = 1;
        original.tab[1].a = 2;
        original.tab[2].a = 3;

        original.tab[0].b = 1;
        original.tab[1].b = 1;
        original.tab[2].b = 2;

        original.tab[0].d = 3;
        original.tab[1].d = 2;
        original.tab[2].d = 1;


#define ADD_PARAM(_field, _flags)  do {                                      \
        qv_append(&params, ((iop_sort_t){                          \
            .field_path = LSTR(_field),                                      \
            .flags = _flags,                                                 \
        }));                                                                 \
    } while (0)

#define SORT_AND_CHECK(p1, p2, p3)  do {                                     \
        Z_ASSERT_ZERO(iop_msort(tstiop__my_struct_a, sorted.tab, sorted.len, \
                                &params, NULL));                             \
        Z_ASSERT_EQ(sorted.tab[0].a, original.tab[p1].a);                    \
        Z_ASSERT_EQ(sorted.tab[1].a, original.tab[p2].a);                    \
        Z_ASSERT_EQ(sorted.tab[2].a, original.tab[p3].a);                    \
        Z_ASSERT_EQ(sorted.tab[0].b, original.tab[p1].b);                    \
        Z_ASSERT_EQ(sorted.tab[1].b, original.tab[p2].b);                    \
        Z_ASSERT_EQ(sorted.tab[2].b, original.tab[p3].b);                    \
        Z_ASSERT_EQ(sorted.tab[0].d, original.tab[p1].d);                    \
        Z_ASSERT_EQ(sorted.tab[1].d, original.tab[p2].d);                    \
        Z_ASSERT_EQ(sorted.tab[2].d, original.tab[p3].d);                    \
    } while (0)

        /* Simple sort */
        qv_copy(&sorted, &original);
        qv_clear(&params);
        ADD_PARAM("a", IOP_SORT_REVERSE);
        SORT_AND_CHECK(2, 1, 0);

        /* Double sort */
        qv_clear(&params);
        ADD_PARAM("b", 0);
        ADD_PARAM("d", 0);
        SORT_AND_CHECK(1, 0, 2);

        /* Double sort reverse on first */
        qv_clear(&params);
        ADD_PARAM("b", IOP_SORT_REVERSE);
        ADD_PARAM("d", 0);
        SORT_AND_CHECK(2, 1, 0);

        /* Double sort reverse on last */
        qv_clear(&params);
        ADD_PARAM("b", 0);
        ADD_PARAM("d", IOP_SORT_REVERSE);
        SORT_AND_CHECK(0, 1, 2);

#undef ADD_PARAM
#undef SORT_AND_CHECK

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter, "test IOP structures filtering") { /* {{{ */
        t_scope;
        tstiop__filtered_struct__t first;
        tstiop__filtered_struct__t second;
        tstiop__filtered_struct__t third;
        byte *bitmap;

        iop_init(tstiop__filtered_struct, &first);
        first.a = 1;
        first.b = 1;
        first.d = 42;
        first.c = T_IOP_ARRAY(i32, 2, 3, 5, 7, 11);

        iop_init(tstiop__filtered_struct, &second);
        second.a = 2;
        second.b = 1;
        second.d = 43;
        second.c = T_IOP_ARRAY(i32, 2, 3, 7, 11);

        iop_init(tstiop__filtered_struct, &third);
        third.a = 1;
        third.b = 1;
        third.d = 44;

#define CHECK_FILTER(_field, _values_args, _exp_objs_args)                   \
    Z_IOP_FILTER_CHECK_FILTER(int, tstiop__filtered_struct__t,               \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third), 0, _field,             \
                              _values_args, _exp_objs_args)

        /* Simple filter */
        CHECK_FILTER("a", (1), (first, third));

        /* Filter on several values */
        CHECK_FILTER("a", (1, 2), (first, second, third));

        /* Filter with no match */
        CHECK_FILTER("a", (3773), ());

        /* Filter excluding tip */
        CHECK_FILTER("d", (43), (second));

        /* Filter on repeated field */
        CHECK_FILTER("c", (5), (first));
        CHECK_FILTER("c", (5, 11), (first, second));
        CHECK_FILTER("c[0]", (5), ());
        CHECK_FILTER("c[2]", (5), (first));
        CHECK_FILTER("c[2]", (5, 7), (first, second));
        CHECK_FILTER("c[-1]", (11), (first, second));
        CHECK_FILTER("c[-2]", (7), (first, second));
        CHECK_FILTER("c[-3]", (5), (first));

        /* Filter on the length of a repeated field */
        CHECK_FILTER("c.len", (4), (second));

#undef CHECK_FILTER

        /* iop_filter_bitmap. */
#define T_ADD_BITMAP(_field, _values_args, _op)                              \
    T_Z_IOP_FILTER_ADD_BITMAP(int, tstiop__filtered_struct__t,               \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third), 0, _field, _op,        \
                              _values_args, &bitmap)

#define APPLY_BITMAP(...)                                                    \
    Z_IOP_FILTER_APPLY_BITMAP(tstiop__filtered_struct__t,                    \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third), (__VA_ARGS__), bitmap)

        bitmap = NULL;
        T_ADD_BITMAP("a", (42), BITMAP_OP_OR);
        T_ADD_BITMAP("a", (42, 1), BITMAP_OP_OR);
        APPLY_BITMAP(first, third);

        bitmap = NULL;
        T_ADD_BITMAP("a", (1), BITMAP_OP_OR);
        T_ADD_BITMAP("a", (2), BITMAP_OP_OR);
        APPLY_BITMAP(first, second, third);

        bitmap = NULL;
        T_ADD_BITMAP("a", (1), BITMAP_OP_AND);
        T_ADD_BITMAP("a", (1, 2), BITMAP_OP_AND);
        APPLY_BITMAP(first, third);

#undef APPLY_BITMAP
#undef T_ADD_BITMAP

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter_class, "test IOP classes filtering") { /* {{{ */
        t_scope;
        tstiop__my_class2__t *first;
        tstiop__my_class2__t *second;
        tstiop__my_class2__t *third;

        first = t_iop_new(tstiop__my_class2);
        first->int1 = 1;
        first->int2 = 1;

        second = t_iop_new(tstiop__my_class2);
        second->int1 = 2;
        second->int2 = 1;

        third = &t_iop_new(tstiop__my_class3)->super;
        third->int1 = 1;
        third->int2 = 1;

#define CHECK_FILTER(_field, _value_type, _values_args, _exp_objs_args)      \
    Z_IOP_FILTER_CHECK_FILTER(_value_type, tstiop__my_class2__t *,           \
                              &tstiop__my_class2__s,                         \
                              (first, second, third), 0, _field,             \
                              _values_args, _exp_objs_args)

        /* Simple filter */
        CHECK_FILTER("int1", int, (1), (first, third));

        /* Filter on several values */
        CHECK_FILTER("int1", int, (1, 2), (first, second, third));

        /* Filter on class name */
        CHECK_FILTER("_class", lstr_t, (LSTR("tstiop.MyClass3")), (third));

#undef CHECK_FILTER

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter_strings, "test IOP filtering on string values") { /* {{{ */
        t_scope;
        tstiop__filtered_struct__t first;
        tstiop__filtered_struct__t second;
        tstiop__filtered_struct__t third;
        lstr_t filter;

        iop_init(tstiop__filtered_struct, &first);
        first.s = LSTR("toto");

        iop_init(tstiop__filtered_struct, &second);
        second.s = LSTR("titi");

        iop_init(tstiop__filtered_struct, &third);
        third.s = LSTR("tutu");

#define CHECK_FILTER(_flags, _exp_objs_args)                                 \
    Z_IOP_FILTER_CHECK_FILTER(lstr_t, tstiop__filtered_struct__t,            \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third), _flags, "s",           \
                              (filter), _exp_objs_args)

        /* Simple filters */
        filter = LSTR("none");
        CHECK_FILTER(0, ());
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, ());

        filter = LSTR("titi");
        CHECK_FILTER(0, (second));
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, (second));

        /* SQL patterns. */
        filter = LSTR("to%");
        CHECK_FILTER(0, ());
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, (first));

        filter = LSTR("%t%");
        CHECK_FILTER(0, ());
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, (first, second, third));

#undef CHECK_FILTER

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter_opt, "test IOP filtering on optional fields") { /* {{{ */
        t_scope;
        tstiop__my_struct_a_opt__t first;
        tstiop__my_struct_a_opt__t second;
        tstiop__my_struct_a_opt__t third;

        iop_init(tstiop__my_struct_a_opt, &first);
        iop_init(tstiop__my_struct_a_opt, &second);
        iop_init(tstiop__my_struct_a_opt, &third);

#define CHECK_FILTER(_field, _must_be_set, _exp_objs_args)                   \
    Z_IOP_FILTER_CHECK_OPT(tstiop__my_struct_a_opt__t,                       \
                           &tstiop__my_struct_a_opt__s,                      \
                           (first, second, third), _field, _must_be_set,     \
                           _exp_objs_args)

        /* Test filter on optional string. */
        second.j = LSTR("present");
        CHECK_FILTER("j", true,  (second));
        CHECK_FILTER("j", false, (first, third));

        /* Test filter on optional integer. */
        OPT_SET(first.a, 1);
        OPT_SET(third.a, 2);
        CHECK_FILTER("a", true,  (first, third));
        CHECK_FILTER("a", false, (second));

        /* Test filter on optional union. */
        third.l  = t_iop_new(tstiop__my_union_a);
        *third.l = IOP_UNION(tstiop__my_union_a, ua, 1);
        CHECK_FILTER("l", true,  (third));
        CHECK_FILTER("l", false, (first, second));

        /* Test filter on optional struct. */
        first.o  = t_iop_new(tstiop__my_struct_b);
        second.o = first.o;
        CHECK_FILTER("o", true,  (first, second));
        CHECK_FILTER("o", false, (third));

        /* Test filter on optional class. */
        third.cls2 = t_iop_new(tstiop__my_class2);
        CHECK_FILTER("cls2", true,  (third));
        CHECK_FILTER("cls2", false, (first, second));

        /* Test filter on a repeated field. */
        second.u.tab = t_new(int, 1);
        second.u.len = 1;
        CHECK_FILTER("u", true,  (second));
        CHECK_FILTER("u", false, (first, third));
        CHECK_FILTER("u[0]", true,  (second));
        CHECK_FILTER("u[0]", false, (first, third));
        CHECK_FILTER("u[1]", true,  ());
        CHECK_FILTER("u[1]", false, (first, second, third));
        CHECK_FILTER("u[-1]", true,  (second));
        CHECK_FILTER("u[-1]", false, (first, third));

        /* Test filter on optional void. */
        first.w  = true;
        second.w = true;
        CHECK_FILTER("w", true,  (first, second));
        CHECK_FILTER("w", false, (third));

#undef CHECK_FILTER

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter_invert_match, "test IOP filtering by fields with invert match") { /* {{{ */
        t_scope;
        tstiop__filtered_struct__t first;
        tstiop__filtered_struct__t second;
        tstiop__filtered_struct__t third;
        byte *bitmap;

        iop_init(tstiop__filtered_struct, &first);
        first.a = 1;
        first.b = 1;
        first.d = 42;
        first.c = T_IOP_ARRAY(i32, 2, 3, 5, 7, 11);

        iop_init(tstiop__filtered_struct, &second);
        second.a = 2;
        second.b = 1;
        second.d = 43;
        second.c = T_IOP_ARRAY(i32, 2, 3, 7, 11);

        iop_init(tstiop__filtered_struct, &third);
        third.a = 1;
        third.b = 1;
        third.d = 44;

#define CHECK_FILTER(_field, _values_args, _exp_objs_args)                   \
    Z_IOP_FILTER_CHECK_FILTER(int, tstiop__filtered_struct__t,               \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third),                        \
                              IOP_FILTER_INVERT_MATCH, _field, _values_args, \
                              _exp_objs_args)

        /* Simple filter */
        CHECK_FILTER("a", (1), (second));

        /* Filter on several values */
        CHECK_FILTER("a", (1, 2), ());

        /* Filter with no match */
        CHECK_FILTER("a", (3773), (first, second, third));

        /* Filter excluding tip */
        CHECK_FILTER("d", (43), (first, third));

        /* Filter on repeated field */
        CHECK_FILTER("c", (5), (second, third));
        CHECK_FILTER("c", (5, 11), (third));
        CHECK_FILTER("c[0]", (5), (first, second, third));
        CHECK_FILTER("c[2]", (5), (second, third));
        CHECK_FILTER("c[2]", (5, 7), (third));
        CHECK_FILTER("c[-1]", (11), (third));
        CHECK_FILTER("c[-2]", (7), (third));
        CHECK_FILTER("c[-3]", (5), (second, third));

        /* Filter on the length of a repeated field */
        CHECK_FILTER("c.len", (4), (first, third));

#undef CHECK_FILTER

        /* iop_filter_bitmap. */
#define T_ADD_BITMAP(_field, _values_args, _op)                              \
    T_Z_IOP_FILTER_ADD_BITMAP(int, tstiop__filtered_struct__t,               \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third),                        \
                              IOP_FILTER_INVERT_MATCH, _field, _op,          \
                              _values_args, &bitmap)

#define APPLY_BITMAP(...)                                                    \
    Z_IOP_FILTER_APPLY_BITMAP(tstiop__filtered_struct__t,                    \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third), (__VA_ARGS__), bitmap)

        bitmap = NULL;
        T_ADD_BITMAP("a", (42), BITMAP_OP_OR);
        T_ADD_BITMAP("a", (42, 1), BITMAP_OP_OR);
        APPLY_BITMAP(first, second, third);

        bitmap = NULL;
        T_ADD_BITMAP("a", (1), BITMAP_OP_OR);
        T_ADD_BITMAP("a", (2), BITMAP_OP_OR);
        APPLY_BITMAP(first, second, third);

        bitmap = NULL;
        T_ADD_BITMAP("a", (1), BITMAP_OP_AND);
        T_ADD_BITMAP("a", (1, 2), BITMAP_OP_AND);
        APPLY_BITMAP();

#undef APPLY_BITMAP
#undef T_ADD_BITMAP

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter_class_invert_match, "test IOP classes filtering with invert match") { /* {{{ */
        t_scope;
        tstiop__my_class2__t *first;
        tstiop__my_class2__t *second;
        tstiop__my_class2__t *third;

        first = t_iop_new(tstiop__my_class2);
        first->int1 = 1;
        first->int2 = 1;

        second = t_iop_new(tstiop__my_class2);
        second->int1 = 2;
        second->int2 = 1;

        third = &t_iop_new(tstiop__my_class3)->super;
        third->int1 = 1;
        third->int2 = 1;

#define CHECK_FILTER(_field, _value_type, _values_args, _exp_objs_args)      \
    Z_IOP_FILTER_CHECK_FILTER(_value_type, tstiop__my_class2__t *,           \
                              &tstiop__my_class2__s,                         \
                              (first, second, third),                        \
                              IOP_FILTER_INVERT_MATCH, _field, _values_args, \
                              _exp_objs_args)

        /* Simple filter */
        CHECK_FILTER("int1", int, (1), (second));

        /* Filter on several values */
        CHECK_FILTER("int1", int, (1, 2), ());

        /* Filter on class name */
        CHECK_FILTER("_class", lstr_t, (LSTR("tstiop.MyClass3")),
                     (first, second));

#undef CHECK_FILTER

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_filter_strings_invert_match, "test IOP string filtering with invert match") { /* {{{ */
        t_scope;
        tstiop__filtered_struct__t first;
        tstiop__filtered_struct__t second;
        tstiop__filtered_struct__t third;
        lstr_t filter;

        iop_init(tstiop__filtered_struct, &first);
        first.s = LSTR("toto");

        iop_init(tstiop__filtered_struct, &second);
        second.s = LSTR("titi");

        iop_init(tstiop__filtered_struct, &third);
        third.s = LSTR("tutu");

#define CHECK_FILTER(_flags, _exp_objs_args)                                 \
    Z_IOP_FILTER_CHECK_FILTER(lstr_t, tstiop__filtered_struct__t,            \
                              &tstiop__filtered_struct__s,                   \
                              (first, second, third),                        \
                              _flags | IOP_FILTER_INVERT_MATCH, "s",         \
                              (filter), _exp_objs_args)

        /* Simple filters */
        filter = LSTR("none");
        CHECK_FILTER(0, (first, second, third));
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, (first, second, third));

        filter = LSTR("titi");
        CHECK_FILTER(0, (first, third));
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, (first, third));

        /* SQL patterns. */
        filter = LSTR("to%");
        CHECK_FILTER(0, (first, second, third));
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, (second, third));

        filter = LSTR("%t%");
        CHECK_FILTER(0, (first, second, third));
        CHECK_FILTER(IOP_FILTER_SQL_LIKE, ());

#undef CHECK_FILTER

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_prune, "check gen attr filtering") { /* {{{ */
        tstiop__filtered_struct__t obj;
        int arr[] = { 1, 2, 3 };

        iop_init(tstiop__filtered_struct, &obj);
        obj.long_string = LSTR("struct");
        obj.c = IOP_TYPED_ARRAY(i32, arr, countof(arr));

        /* Filter fields tagged with "test:mayBeSkipped". */
        iop_prune(&tstiop__filtered_struct__s, &obj,
                  LSTR("test:mayBeSkipped"));
        Z_ASSERT_NULL(obj.c.tab);
        Z_ASSERT_EQ(obj.c.len, 0);
        Z_ASSERT_LSTREQUAL(obj.long_string, LSTR_NULL_V);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_field_path_compile, "test iop_field_path compilation") { /* {{{ */
        t_scope;
        tstiop__my_struct_f__t msf;
        tstiop__my_class3__t mc;
        tstiop__my_class3__t mc2;

#define TEST(pfx, _path, _value, _exp_type, _exp_is_array, _exp_st, _exp_en, \
             _exp_error)                                                     \
        Z_HELPER_RUN(z_check_field_path_compile(&pfx##__s, LSTR(_path),      \
                                                (_value),                    \
                                                (_exp_type),                 \
                                                (_exp_is_array),             \
                                                (_exp_st), (_exp_en),        \
                                                (_exp_error)))

#define TEST_SCALAR(pfx, _path, _value, _exp_type, _exp_is_array)            \
        TEST(pfx, (_path), (_value), (_exp_type), (_exp_is_array), NULL,     \
             NULL, LSTR_NULL_V)
#define TEST_ST(pfx, _path, _value, _exp_type, _exp_is_array, st_pfx)        \
        TEST(pfx, (_path), (_value), (_exp_type), (_exp_is_array),           \
             &st_pfx##__s, NULL, LSTR_NULL_V)
#define TEST_ENUM(pfx, _path, _value, _exp_is_array, en_pfx)                 \
        TEST(pfx, (_path), (_value), IOP_T_ENUM, (_exp_is_array), NULL,      \
             &en_pfx##__e, LSTR_NULL_V)
#define TEST_ERROR(pfx, _path, _value, _error)                               \
        TEST(pfx, (_path), (_value), IOP_T_VOID, false, NULL, NULL,          \
             LSTR(_error))

        TEST_ERROR(tstiop__my_struct_a, "", NULL,
                   "cannot process empty field path");
        TEST_SCALAR(tstiop__my_struct_a, "htab", NULL, IOP_T_U64, true);
        TEST_SCALAR(tstiop__my_struct_a, "htab[5]", NULL, IOP_T_U64, false);
        TEST_SCALAR(tstiop__my_struct_a, "htab[*]", NULL, IOP_T_U64, false);
        TEST_ERROR(tstiop__my_struct_a, "htab[5*]", NULL,
                   "cannot read index for field `htab': syntax error");
        TEST_ST(tstiop__my_struct_a, "cls2", NULL, IOP_T_STRUCT, false,
                tstiop__my_class2);
        TEST_ST(tstiop__my_struct_f, "d", NULL, IOP_T_UNION, true,
                tstiop__my_union_a);
        TEST_ERROR(tstiop__my_struct_f, "d.ub", NULL,
                   "cannot process field path `d.ub', field `d' "
                   "is repeated in structure `tstiop.MyStructF'");
        TEST_ERROR(tstiop__my_struct_f, "d[*].ub[0]", NULL,
                   "got index but field `tstiop.MyUnionA:ub' "
                   "is not repeated");
        TEST_SCALAR(tstiop__my_struct_a, "cls2._class", NULL,
                    IOP_T_STRING, false);
        TEST_ERROR(tstiop__my_struct_a, "cls2._class.sub", NULL,
                   "cannot fetch subfield of a typename");
        TEST_ERROR(tstiop__my_struct_a, "lr._class", NULL,
                   "cannot fetch typename of a non-class field");
        TEST_ERROR(tstiop__my_struct_a, "lr._class.sub", NULL,
                   "cannot fetch typename of a non-class field");
        TEST_ENUM(tstiop__my_struct_a, "k", NULL, false, tstiop__my_enum_a);
        TEST_ERROR(tstiop__my_struct_a_opt, "o.c", NULL,
                   "cannot process field path `o.c', field `c' is unknown "
                   "in structure `tstiop.MyStructB'");

        iop_init(tstiop__my_struct_f, &msf);
        iop_init(tstiop__my_class3, &mc);
        msf.f = &mc.super.super;

        TEST_SCALAR(tstiop__my_struct_f, "f.int1", NULL, IOP_T_I32, false);
        TEST_ERROR(tstiop__my_struct_f, "f.int2", NULL,
                   "cannot process field path `f.int2', field `int2' is unknown "
                   "in structure `tstiop.MyClass1'");
        TEST_SCALAR(tstiop__my_struct_f, "f.int2", &msf, IOP_T_I32, false);
        TEST_ERROR(tstiop__my_struct_f, "f.int4", &msf,
                   "cannot process field path `f.int4', field `int4' is unknown "
                   "in structure `tstiop.MyClass3'");

        msf.e = T_IOP_ARRAY_NEW(tstiop__my_class1, 1);
        msf.e.tab[0] = &mc.super.super;
        TEST_ERROR(tstiop__my_struct_f, "e[0].int2", NULL,
                   "cannot process field path `e[0].int2', field `int2' is unknown "
                   "in structure `tstiop.MyClass1'");
        TEST_SCALAR(tstiop__my_struct_f, "e[0].int2", &msf, IOP_T_I32, false);
        TEST_ERROR(tstiop__my_struct_f, "e[*].int2", &msf,
                   "unexpected wildcard");
        TEST_ERROR(tstiop__my_struct_f, "e[8].int2", &msf,
                   "the path up to the field `int2` is not valid for the "
                   "provided value");

        TEST_SCALAR(tstiop__my_class3, "int2", &mc, IOP_T_I32, false);
        TEST_SCALAR(tstiop__my_class1, "int2", &mc, IOP_T_I32, false);
        TEST_ERROR(tstiop__my_class1, "int2", NULL,
                   "cannot process field path `int2', field `int2' is unknown "
                   "in structure `tstiop.MyClass1'");

        iop_init(tstiop__my_class3, &mc2);
        mc.next_class = &mc2.super.super;
        TEST_SCALAR(tstiop__my_struct_f, "e[0].nextClass.bool1", &msf, IOP_T_BOOL,
                    false);

#undef TEST_SCALAR
#undef TEST_ST
#undef TEST_ENUM
#undef TEST_ERROR
#undef TEST
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_copy_inv_tab, "mp_iop_copy_desc_sz(): invalid tab pointer when len == 0") { /* {{{ */
        t_scope;
        lstr_t path_curr_v;
        lstr_t path_v3;

        path_curr_v = t_lstr_fmt("%*pM/iop/zchk-tstiop-plugin" SO_FILEEXT,
                                 LSTR_FMT_ARG(z_cmddir_g));

        path_v3 = t_lstr_fmt("%*pM/test-data/test_v3_centos-5u4/"
                             "zchk-tstiop-plugin" SO_FILEEXT,
                             LSTR_FMT_ARG(z_cmddir_g));

        Z_HELPER_RUN(iop_check_retro_compat_copy_inv_tab(path_curr_v));
        Z_HELPER_RUN(iop_check_retro_compat_copy_inv_tab(path_v3));

    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_basics, "test inheritance basic properties") { /* {{{ */
#define CHECK_PARENT(_type, _class_id)  \
        do {                                                                 \
            const iop_class_attrs_t *attrs;                                  \
                                                                             \
            attrs = tstiop_inheritance__##_type##__s.class_attrs;            \
            Z_ASSERT_P(attrs);                                               \
            Z_ASSERT_EQ(attrs->class_id, _class_id);                         \
            Z_ASSERT_NULL(attrs->parent);                                    \
        } while (0)

#define CHECK_CHILD(_type, _class_id, _parent)  \
        do {                                                                 \
            const iop_class_attrs_t *attrs;                                  \
                                                                             \
            attrs = tstiop_inheritance__##_type##__s.class_attrs;            \
            Z_ASSERT_EQ(attrs->class_id, _class_id);                         \
            Z_ASSERT(attrs->parent ==  &tstiop_inheritance__##_parent##__s); \
        } while (0)

        CHECK_PARENT(a1, 0);
        CHECK_CHILD(b1,  1, a1);
        CHECK_CHILD(b2,  65535, a1);
        CHECK_CHILD(c1,  3, b2);
        CHECK_CHILD(c2,  4, b2);

        CHECK_PARENT(a2, 0);
        CHECK_CHILD(b3,  1, a2);
        CHECK_CHILD(c3,  2, b3);
        CHECK_CHILD(c4,  3, b3);

        CHECK_PARENT(a3, 0);
        CHECK_CHILD(b4,  1, a3);
#undef CHECK_PARENT
#undef CHECK_CHILD
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_switch, "test IOP_(OBJ|CLASS)_SWITCH helpers") { /* {{{ */
        tstiop_inheritance__c1__t c1;
        bool matched = false;

        iop_init(tstiop_inheritance__c1, &c1);
        Z_ASSERT_EQ(IOP_OBJ_CLASS_ID(&c1), 3);
        IOP_OBJ_EXACT_SWITCH(&c1) {
          IOP_OBJ_CASE_CONST(tstiop_inheritance__a1, &c1, a1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b1, &c1, b1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b2, &c1, b2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c1, &c1, ok) {
            Z_ASSERT_P(ok);
            Z_ASSERT(!matched);
            matched = true;
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c2, &c1, c2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_EXACT_DEFAULT() {
            Z_ASSERT(false);
          }
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_OBJ_EXACT_SWITCH(&c1) {
          IOP_OBJ_CASE_CONST(tstiop_inheritance__a1, &c1, a1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b1, &c1, b1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b2, &c1, b2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c2, &c1, c2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_EXACT_DEFAULT() {
            Z_ASSERT(!matched);
            matched = true;
          }
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_CLASS_EXACT_SWITCH(&tstiop_inheritance__c1__s) {
          case IOP_CLASS_ID(tstiop_inheritance__a1):
            Z_ASSERT(false);
            break;

          case IOP_CLASS_ID(tstiop_inheritance__b1):
            Z_ASSERT(false);
            break;

          case IOP_CLASS_ID(tstiop_inheritance__b2):
            Z_ASSERT(false);
            break;

          case IOP_CLASS_ID(tstiop_inheritance__c1):
            matched = true;
            break;

          case IOP_CLASS_ID(tstiop_inheritance__c2):
            Z_ASSERT(false);
            break;

          default:
            Z_ASSERT(false);
            break;
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_OBJ_SWITCH(c1, &c1) {
          IOP_OBJ_CASE_CONST(tstiop_inheritance__a1, &c1, a1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b1, &c1, b1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b2, &c1, b2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c1, &c1, ok) {
            Z_ASSERT_P(ok);
            Z_ASSERT(!matched);
            matched = true;
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c2, &c1, c2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_DEFAULT(c1) {
            Z_ASSERT(false);
          }
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_CLASS_SWITCH(c1, c1.__vptr) {
          IOP_CLASS_CASE(tstiop_inheritance__a1) {
            Z_ASSERT(false);
          }
          IOP_CLASS_CASE(tstiop_inheritance__b1) {
            Z_ASSERT(false);
          }
          IOP_CLASS_CASE(tstiop_inheritance__b2) {
            Z_ASSERT(false);
          }
          IOP_CLASS_CASE(tstiop_inheritance__c1) {
            Z_ASSERT(!matched);
            matched = true;
          }
          IOP_CLASS_CASE(tstiop_inheritance__c2) {
            Z_ASSERT(false);
          }
          IOP_CLASS_DEFAULT(c1) {
            Z_ASSERT(false);
          }
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_OBJ_SWITCH(c1, &c1) {
          IOP_OBJ_CASE_CONST(tstiop_inheritance__a1, &c1, a1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b1, &c1, b1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b2, &c1, b2) {
            Z_ASSERT(b2);
            Z_ASSERT(!matched);
            matched = true;
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c2, &c1, c2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_DEFAULT(c1) {
            Z_ASSERT(false);
          }
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_OBJ_SWITCH(c1, &c1) {
          IOP_OBJ_CASE_CONST(tstiop_inheritance__a1, &c1, a1) {
            Z_ASSERT(a1);
            Z_ASSERT(!matched);
            matched = true;
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b1, &c1, b1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c2, &c1, c2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_DEFAULT(c1) {
            Z_ASSERT(false);
          }
        }
        Z_ASSERT(matched);

        matched = false;
        IOP_OBJ_SWITCH(c1, &c1) {
          IOP_OBJ_CASE_CONST(tstiop_inheritance__b1, &c1, b1) {
            Z_ASSERT(false);
          }
          IOP_OBJ_CASE_CONST(tstiop_inheritance__c2, &c1, c2) {
            Z_ASSERT(false);
          }
          IOP_OBJ_DEFAULT(c1) {
            Z_ASSERT(!matched);
            matched = true;
          }
        }
        Z_ASSERT(matched);
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_fields_init, "test fields initialization") { /* {{{ */
        {
            tstiop_inheritance__b1__t b1;

            iop_init(tstiop_inheritance__b1, &b1);
            Z_ASSERT_EQ(b1.a, 1);
            Z_ASSERT_LSTREQUAL(b1.b, LSTR("b"));
        }
        {
            tstiop_inheritance__c1__t c1;

            iop_init(tstiop_inheritance__c1, &c1);
            Z_ASSERT_EQ(c1.a, 1);
            Z_ASSERT_EQ(c1.b, true);
            Z_ASSERT_EQ(c1.c, (uint32_t)3);
        }
        {
            tstiop_inheritance__c2__t c2;

            iop_init(tstiop_inheritance__c2, &c2);
            Z_ASSERT_EQ(c2.a, 1);
            Z_ASSERT_EQ(c2.b, true);
            Z_ASSERT_EQ(c2.c, 4);
        }
        {
            tstiop_inheritance__c3__t c3;

            iop_init(tstiop_inheritance__c3, &c3);
            Z_ASSERT_LSTREQUAL(c3.a, LSTR("A2"));
            Z_ASSERT_EQ(c3.b, 5);
            Z_ASSERT_EQ(c3.c, 6);
        }
        {
            tstiop_inheritance__c4__t c4;

            iop_init(tstiop_inheritance__c4, &c4);
            Z_ASSERT_LSTREQUAL(c4.a, LSTR("A2"));
            Z_ASSERT_EQ(c4.b, 5);
            Z_ASSERT_EQ(c4.c, false);
        }
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_casts, "test inheritance casts") { /* {{{ */
        tstiop_inheritance__c2__t  c2;
        tstiop_inheritance__c2__t *c2p;
        tstiop_inheritance__b2__t *b2p;
        uint8_t buf_b2p[20], buf_c2p[20];

#define CHECK_IS_A(_type1, _type2, _res)  \
        do {                                                                 \
            tstiop_inheritance__##_type1##__t obj;                           \
                                                                             \
            iop_init(tstiop_inheritance__##_type1, &obj);                    \
            Z_ASSERT(iop_obj_is_a(&obj,                                      \
                                  tstiop_inheritance__##_type2) == _res);    \
            Z_ASSERT(iop_obj_dynvcast(tstiop_inheritance__##_type2, &obj)    \
                     == (_res ? (void *)&obj : NULL));                       \
            Z_ASSERT(iop_obj_dynccast(tstiop_inheritance__##_type2, &obj)    \
                     == (_res ? (const void *)&obj : NULL));                 \
            if (_res) {                                                      \
                Z_ASSERT(iop_obj_vcast(tstiop_inheritance__##_type2, &obj)   \
                         == (void *)&obj);                                   \
                Z_ASSERT(iop_obj_ccast(tstiop_inheritance__##_type2, &obj)   \
                         == (const void *)&obj);                             \
            }                                                                \
        } while (0)

        CHECK_IS_A(a1, a1, true);
        CHECK_IS_A(b1, a1, true);
        CHECK_IS_A(b1, b1, true);
        CHECK_IS_A(b2, a1, true);
        CHECK_IS_A(c1, b2, true);
        CHECK_IS_A(c1, a1, true);
        CHECK_IS_A(c2, b2, true);
        CHECK_IS_A(c2, a1, true);
        CHECK_IS_A(c3, b3, true);
        CHECK_IS_A(c3, a2, true);
        CHECK_IS_A(c4, b3, true);
        CHECK_IS_A(c4, a2, true);

        CHECK_IS_A(a1, b1, false);
        CHECK_IS_A(a1, a2, false);
        CHECK_IS_A(c1, c2, false);
#undef CHECK_IS_A

        /* Initialize a C2 class */
        iop_init(tstiop_inheritance__c2, &c2);
        c2.a = 11111;
        c2.c = 500;

        /* Cast it in B2, and change some values */
        b2p = iop_obj_vcast(tstiop_inheritance__b2, &c2);
        Z_ASSERT_IOPEQUAL(tstiop_inheritance__b2, b2p, &c2.super);
        Z_HELPER_RUN(iop_std_test_struct(&tstiop_inheritance__b2__s, b2p,
                                         "b2p"));
        Z_ASSERT_EQ(b2p->a, 11111);
        Z_ASSERT_EQ(b2p->b, true);
        b2p->a = 22222;
        b2p->b = false;

        /* Re-cast it in C2, and check fields equality */
        c2p = iop_obj_vcast(tstiop_inheritance__c2, b2p);
        Z_ASSERT_IOPEQUAL(tstiop_inheritance__b2, b2p, &c2.super);
        Z_HELPER_RUN(iop_std_test_struct(&tstiop_inheritance__c2__s, c2p,
                                         "c2p"));
        Z_ASSERT_EQ(c2p->a, 22222);
        Z_ASSERT_EQ(c2p->b, false);
        Z_ASSERT_EQ(c2p->c, 500);

        /* Test that hashes of b2p and c2p are the sames */
        iop_hash_sha1(&tstiop_inheritance__b2__s, b2p, buf_b2p, 0);
        iop_hash_sha1(&tstiop_inheritance__c2__s, c2p, buf_c2p, 0);
        Z_ASSERT_EQUAL(buf_b2p, sizeof(buf_b2p), buf_c2p, sizeof(buf_c2p));
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_static, "test static class members") { /* {{{ */
        const iop_value_t *cvar;

#define CHECK_STATIC_STR(_type, _varname, _value)  \
        do {                                                                 \
            tstiop_inheritance__##_type##__t obj;                            \
                                                                             \
            iop_init(tstiop_inheritance__##_type, &obj);                     \
            cvar = iop_get_cvar_cst(&obj, _varname);                         \
            Z_ASSERT_P(cvar);                                                \
            Z_ASSERT_LSTREQUAL(cvar->s, LSTR(_value));                       \
        } while (0)

        CHECK_STATIC_STR(a1, "staticStr",  "a1");
        CHECK_STATIC_STR(b1, "staticStr",  "a1");
        CHECK_STATIC_STR(b2, "staticStr",  "a1");
        CHECK_STATIC_STR(c1, "staticStr",  "a1");
        CHECK_STATIC_STR(c2, "staticStr",  "c2");
        CHECK_STATIC_STR(c2, "staticStr1", "staticStr1");
        CHECK_STATIC_STR(c2, "staticStr2", "staticStr2");
        CHECK_STATIC_STR(c2, "staticStr3", "staticStr3");
        CHECK_STATIC_STR(c2, "staticStr4", "staticStr4");
        CHECK_STATIC_STR(c2, "staticStr5", "staticStr5");
        CHECK_STATIC_STR(c2, "staticStr6", "staticStr6");
        CHECK_STATIC_STR(c3, "staticStr",  "c3");
#undef CHECK_STATIC_STR

#define CHECK_STATIC(_type, _varname, _field, _value)  \
        do {                                                                 \
            tstiop_inheritance__##_type##__t obj;                            \
                                                                             \
            iop_init(tstiop_inheritance__##_type, &obj);                     \
            cvar = iop_get_cvar_cst(&obj, _varname);                         \
            Z_ASSERT_P(cvar);                                                \
            Z_ASSERT_EQ(cvar->_field, _value);                               \
        } while (0)

        CHECK_STATIC(a1, "staticEnum", i, MY_ENUM_A_B);
        CHECK_STATIC(b1, "staticInt", i, 12);
        CHECK_STATIC(c4, "staticInt", u, (uint64_t)44);
        CHECK_STATIC(b4, "staticInt", u, (uint64_t)4);

        CHECK_STATIC(b2, "staticBool", b, true);
        CHECK_STATIC(c1, "staticBool", b, false);
        CHECK_STATIC(c2, "staticBool", b, true);

        CHECK_STATIC(b3, "staticDouble", d, 23.0);
        CHECK_STATIC(c3, "staticDouble", d, 33.0);
        CHECK_STATIC(c4, "staticDouble", d, 23.0);
#undef CHECK_STATIC

#define CHECK_STATIC_UNDEFINED(_type, _varname)  \
        do {                                                                 \
            tstiop_inheritance__##_type##__t obj;                            \
                                                                             \
            iop_init(tstiop_inheritance__##_type, &obj);                     \
            Z_ASSERT_NULL(iop_get_cvar_cst(&obj, _varname));                 \
        } while (0)

        CHECK_STATIC_UNDEFINED(a1, "undefined");
        CHECK_STATIC_UNDEFINED(a1, "staticInt");
        CHECK_STATIC_UNDEFINED(a1, "staticBool");
        CHECK_STATIC_UNDEFINED(a1, "staticDouble");
        CHECK_STATIC_UNDEFINED(b1, "staticBool");
        CHECK_STATIC_UNDEFINED(b3, "staticBool");
#undef CHECK_STATIC_UNDEFINED

        {
            tstiop_inheritance__a3__t a3;

            a3.__vptr = &tstiop_inheritance__a3__s;
            Z_ASSERT_NULL(iop_get_cvar_cst(&a3, "staticInt"));
        }

        {
            tstiop_inheritance__a1__t a1;
            tstiop_inheritance__b1__t b1;

            a1.__vptr = &tstiop_inheritance__a1__s;
            b1.__vptr = &tstiop_inheritance__b1__s;
            Z_ASSERT(iop_get_cvar_cst(&a1, "staticStr"));
            Z_ASSERT(iop_get_cvar_cst(&b1, "staticStr"));
            cvar = iop_get_class_cvar_cst(&a1, "staticStr");
            Z_ASSERT_P(cvar);
            Z_ASSERT_LSTREQUAL(cvar->s, LSTR("a1"));
            Z_ASSERT_NULL(iop_get_class_cvar_cst(&b1, "staticStr"));
        }
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_static_types, "test static class members types") { /* {{{ */
#define CHECK_STATIC_TYPE(_cls_type, _field_name, _field_type)               \
    Z_HELPER_RUN(z_check_static_field_type(&_cls_type##__s,                  \
                                           LSTR(_field_name), (_field_type), \
                                           #_field_type))

    CHECK_STATIC_TYPE(tstiop_inheritance__a1, "staticStr",    IOP_T_STRING);
    CHECK_STATIC_TYPE(tstiop_inheritance__a1, "staticEnum",   IOP_T_I64);
    CHECK_STATIC_TYPE(tstiop_inheritance__b1, "staticInt",    IOP_T_I64);
    CHECK_STATIC_TYPE(tstiop_inheritance__b2, "staticBool",   IOP_T_BOOL);
    CHECK_STATIC_TYPE(tstiop_inheritance__c2, "staticStr",    IOP_T_STRING);
    CHECK_STATIC_TYPE(tstiop_inheritance__b3, "staticDouble", IOP_T_DOUBLE);
    CHECK_STATIC_TYPE(tstiop_inheritance__c4, "staticInt",    IOP_T_U64);

#undef CHECK_STATIC_TYPE
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_equals, "test iop_equals/hash with inheritance") { /* {{{ */
        t_scope;
        tstiop_inheritance__c2__t c2_1_1, c2_1_2, c2_1_3;
        tstiop_inheritance__c2__t c2_2_1, c2_2_2, c2_2_3;
        tstiop_inheritance__b2__t b2_1, b2_2;
        tstiop_inheritance__class_container__t cc_1, cc_2;

        iop_init(tstiop_inheritance__c2, &c2_1_1);
        iop_init(tstiop_inheritance__c2, &c2_1_2);
        iop_init(tstiop_inheritance__c2, &c2_1_3);
        iop_init(tstiop_inheritance__c2, &c2_2_1);
        iop_init(tstiop_inheritance__c2, &c2_2_2);
        iop_init(tstiop_inheritance__c2, &c2_2_3);

        iop_init(tstiop_inheritance__b2, &b2_1);
        iop_init(tstiop_inheritance__b2, &b2_2);

        iop_init(tstiop_inheritance__class_container, &cc_1);
        iop_init(tstiop_inheritance__class_container, &cc_2);

        /* These tests rely on the fact that there are no hash collisions in
         * the test samples, which is the case.
         *
         * They are actually doing much more than just testing
         * iop_equals/hash: packing/unpacking in binary/json/xml is also
         * tested.
         */
#define CHECK_EQUALS(_type, _v1, _v2, _res)  \
        do {                                                                 \
            uint8_t buf1[20], buf2[20];                                      \
                                                                             \
            Z_ASSERT(iop_equals_desc(&tstiop_inheritance__##_type##__s,      \
                                     _v1, _v2) == _res);                     \
            Z_ASSERT_EQ(!iop_cmp_desc(&tstiop_inheritance__##_type##__s,     \
                                  _v1, _v2), _res);                          \
            iop_hash_sha1(&tstiop_inheritance__##_type##__s, _v1, buf1, 0);  \
            iop_hash_sha1(&tstiop_inheritance__##_type##__s, _v2, buf2, 0);  \
            Z_ASSERT(lstr_equal(                                             \
                LSTR_INIT_V((const char *)buf1, sizeof(buf1)),               \
                LSTR_INIT_V((const char *)buf2, sizeof(buf2))) == _res);     \
            Z_HELPER_RUN(iop_std_test_struct(                                \
                &tstiop_inheritance__##_type##__s, _v1, TOSTR(_v1)));        \
            Z_HELPER_RUN(iop_std_test_struct(                                \
                &tstiop_inheritance__##_type##__s, _v2, TOSTR(_v2)));        \
            Z_HELPER_RUN(iop_json_test_struct(                               \
                &tstiop_inheritance__##_type##__s, _v1, TOSTR(_v1)));        \
            Z_HELPER_RUN(iop_json_test_struct(                               \
                &tstiop_inheritance__##_type##__s, _v2, TOSTR(_v2)));        \
            Z_HELPER_RUN(iop_xml_test_struct(                                \
                &tstiop_inheritance__##_type##__s, _v1, TOSTR(_v1)));        \
            Z_HELPER_RUN(iop_xml_test_struct(                                \
                &tstiop_inheritance__##_type##__s, _v2, TOSTR(_v2)));        \
        } while (0)

        /* ---- Tests with "simple" classes --- */
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, true);

        /* Modify a field of "level A1" */
        c2_1_1.a = 2;
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, false);
        c2_2_1.a = 2;
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, true);

        /* Modify a field of "level B2" */
        c2_1_1.b = false;
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, false);
        c2_2_1.b = false;
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, true);

        /* Modify a field of "level C2" */
        c2_1_1.c = 8;
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, false);
        c2_2_1.c = 8;
        CHECK_EQUALS(c2, &c2_1_1, &c2_2_1, true);

        /* ---- Test when modifying a non-scalar field ---- */
        {
            t_scope;
            tstiop_inheritance__c2__t *c2_1_4;

            /* With mp_iop_dup_desc_sz */
            c2_1_1.a3 = t_lstr_fmt("a");
            c2_1_4 = t_iop_dup(tstiop_inheritance__c2, &c2_1_1);
            CHECK_EQUALS(c2, &c2_1_1, c2_1_4, true);
            c2_1_1.a3.v[0] = 'b';
            CHECK_EQUALS(c2, &c2_1_1, c2_1_4, false);
            c2_1_4->a3.v[0] = 'b';
            CHECK_EQUALS(c2, &c2_1_1, c2_1_4, true);

            /* And with mp_iop_copy_desc_sz */
            c2_1_1.a3 = t_lstr_fmt("c");
            mp_iop_copy(t_pool(), tstiop_inheritance__c2, &c2_1_4, &c2_1_1);
            CHECK_EQUALS(c2, &c2_1_1, c2_1_4, true);
            c2_1_1.a3.v[0] = 'd';
            CHECK_EQUALS(c2, &c2_1_1, c2_1_4, false);
            c2_1_4->a3.v[0] = 'd';
            CHECK_EQUALS(c2, &c2_1_1, c2_1_4, true);

            c2_1_1.a3 = LSTR_NULL_V;
        }

        /* ---- Tests with a class container --- */
        cc_1.a1 = iop_obj_vcast(tstiop_inheritance__a1, &c2_1_1);
        cc_2.a1 = iop_obj_vcast(tstiop_inheritance__a1, &c2_2_1);
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);

        /* Test mandatory field a1 */
        cc_1.a1->a = 3;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        cc_2.a1->a = 3;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);

        /* Test optional field b2 */
        cc_1.b2 = &b2_1;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        cc_2.b2 = &b2_2;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);
        cc_1.b2->a = 4;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        cc_2.b2->a = 4;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);
        cc_2.b2 = iop_obj_vcast(tstiop_inheritance__b2, &c2_2_1);
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        cc_2.b2 = &b2_2;

        /* Test repeated field c2 */
        cc_1.c2.tab = t_new(tstiop_inheritance__c2__t *, 2);
        cc_1.c2.tab[0] = &c2_1_2;
        cc_1.c2.len = 1;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        cc_2.c2.tab = t_new(tstiop_inheritance__c2__t *, 2);
        cc_2.c2.tab[0] = &c2_2_2;
        cc_2.c2.len = 1;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);
        c2_1_2.b = false;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        c2_2_2.b = false;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);
        cc_1.c2.tab[1] = &c2_1_3;
        cc_1.c2.len = 2;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        cc_2.c2.tab[1] = &c2_2_3;
        cc_2.c2.len = 2;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);
        c2_1_3.a = 5;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, false);
        c2_2_3.a = 5;
        CHECK_EQUALS(class_container, &cc_1, &cc_2, true);
#undef CHECK_EQUALS

    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_json, "test json unpacking inheritance") { /* {{{ */
        /* These tests are meant to check json unpacking in some unusual
         * conditions.
         * Packing and unpacking in usual conditions (ie. valid json packed by
         * our packer) is already stressed by the other tests.
         */
        t_scope;
        tstiop_inheritance__c1__t *c1 = NULL;
        tstiop_inheritance__d1__t *d1 = NULL;
        tstiop_inheritance__b2__t *b2 = NULL;
        tstiop_inheritance__a3__t *a3 = NULL;
        tstiop_inheritance__b4__t *b4 = NULL;
        tstiop_inheritance__c5__t *c5 = NULL;
        tstiop_inheritance__class_container__t  *class_container  = NULL;
        tstiop_inheritance__class_container2__t *class_container2 = NULL;
        SB_1k(err);

#define CHECK_OK(_type, _filename)  \
        do {                                                                 \
            Z_ASSERT_N(t_iop_junpack_ptr_file(t_fmt("%*pM/iop/" _filename,   \
                                                    LSTR_FMT_ARG(z_cmddir_g)),\
                                              &tstiop_inheritance__##_type##__s,\
                                              (void **)&_type, 0, NULL,      \
                                              &err),                         \
                       "junpack failed: %s", err.data);                      \
        } while (0)

        /* Test that fields can be in any order */
        CHECK_OK(c1, "tstiop_inheritance_valid1.json");
        Z_ASSERT(c1->__vptr == &tstiop_inheritance__c1__s);
        Z_ASSERT_EQ(c1->a,   2);
        Z_ASSERT_EQ(c1->a2, 12);
        Z_ASSERT_EQ(c1->b,  false);
        Z_ASSERT_EQ(c1->c,  (uint32_t)5);

        /* Test with missing optional fields */
        CHECK_OK(c1, "tstiop_inheritance_valid2.json");
        Z_ASSERT(c1->__vptr == &tstiop_inheritance__c1__s);
        Z_ASSERT_EQ(c1->a,   1);
        Z_ASSERT_EQ(c1->a2, 12);
        Z_ASSERT_EQ(c1->b,  true);
        Z_ASSERT_EQ(c1->c,  (uint32_t)3);

        /* Test that "_class" field can be missing */
        CHECK_OK(d1, "tstiop_inheritance_valid3.json");
        Z_ASSERT(d1->__vptr == &tstiop_inheritance__d1__s);
        Z_ASSERT_EQ(d1->a,  -12);
        Z_ASSERT_EQ(d1->a2, -15);
        Z_ASSERT_EQ(d1->b,  true);
        Z_ASSERT_EQ(d1->c,  (uint32_t)153);

        /* Test that missing mandatory class fields are OK if this class have
         * only optional fields.
         * Also check prefixed syntax on a class field. */
        CHECK_OK(class_container2, "tstiop_inheritance_valid4.json");
        Z_ASSERT_P(class_container2->a1);
        Z_ASSERT(class_container2->a1->__vptr == &tstiop_inheritance__a1__s);
        Z_ASSERT_EQ(class_container2->a1->a2, 10);
        Z_ASSERT_P(class_container2->b3);
        Z_ASSERT(class_container2->b3->__vptr == &tstiop_inheritance__b3__s);
        Z_ASSERT_LSTREQUAL(class_container2->b3->a, LSTR("A2"));
        Z_ASSERT_EQ(class_container2->b3->b, 5);
        Z_ASSERT(class_container2->a3->__vptr == &tstiop_inheritance__b4__s);
        b4 = iop_obj_vcast(tstiop_inheritance__b4, class_container2->a3);
        Z_ASSERT_EQ(b4->a3, 6);
        Z_ASSERT_EQ(b4->b4, 7);

        /* Test that "_class" field can be given using prefixed syntax */
        CHECK_OK(c1, "tstiop_inheritance_valid5.json");
        Z_ASSERT(c1->__vptr == &tstiop_inheritance__c1__s);
        Z_ASSERT_EQ(c1->a,  -480);
        Z_ASSERT_EQ(c1->a2, -479);
        Z_ASSERT_EQ(c1->b,  false);
        Z_ASSERT_EQ(c1->c,  (uint32_t)478);

#define CHECK_FAIL(_type, _filename, _flags, _err)  \
        do {                                                                 \
            sb_reset(&err);                                                  \
            Z_ASSERT_NEG(t_iop_junpack_ptr_file(t_fmt("%*pM/iop/" _filename, \
                LSTR_FMT_ARG(z_cmddir_g)), &tstiop_inheritance__##_type##__s,\
                (void **)&_type, _flags, NULL, &err));                       \
            Z_ASSERT(strstr(err.data, _err), "%s", err.data);                \
        } while (0)

        /* Test that when the "_class" is missing, the expected type is the
         * wanted one */
        CHECK_FAIL(b2, "tstiop_inheritance_invalid1.json", 0,
                   "expected field of struct tstiop_inheritance.B2, got "
                   "`\"c\"'");

        /* Test that the "_class" field is mandatory for abstract classes */
        CHECK_FAIL(a3, "tstiop_inheritance_invalid1.json", 0,
                   "expected `_class' field, got `}'");

        /* Test with an unknown "_class" */
        CHECK_FAIL(c1, "tstiop_inheritance_invalid2.json", 0,
                   "expected a child of `tstiop_inheritance.C1'");

        /* Test with an incompatible "_class" */
        CHECK_FAIL(c1, "tstiop_inheritance_invalid3.json", 0,
                   "expected a child of `tstiop_inheritance.C1'");

        /* Test with a missing mandatory field */
        CHECK_FAIL(c1, "tstiop_inheritance_invalid4.json", 0,
                   "member `tstiop_inheritance.A1:a2' is missing");
        CHECK_FAIL(class_container, "tstiop_inheritance_invalid5.json", 0,
                   "member `tstiop_inheritance.ClassContainer:a1' is missing");
        CHECK_FAIL(class_container, "tstiop_inheritance_invalid6.json", 0,
                   "member `tstiop_inheritance.ClassContainer:a1' is missing");

        /* Unpacking of abstract classes is forbidden */
        CHECK_FAIL(a3, "tstiop_inheritance_invalid7.json", 0,
                   "expected a non-abstract class");

        /* Check that missing mandatory class fields, for classes having only
         * optional fields, is KO if this class is abstract (while it's ok if
         * it's not abstract, cf. test above).
         */
        CHECK_FAIL(class_container2, "tstiop_inheritance_invalid8.json", 0,
                   "member `tstiop_inheritance.ClassContainer2:a3' is "
                   "missing");

        /* Check that private classes cannot be unpacked if ask so.
         */
        CHECK_OK(c5, "tstiop_inheritance_invalid9.json");
        Z_ASSERT(c5->__vptr == &tstiop_inheritance__c5__s);
        CHECK_FAIL(c5, "tstiop_inheritance_invalid9.json",
                   IOP_UNPACK_FORBID_PRIVATE,
                   "a non-private child of `tstiop_inheritance.C5`");

#undef CHECK_OK
#undef CHECK_FAIL
    } Z_TEST_END
    /* }}} */
    Z_TEST(inheritance_xml, "test inheritance and xml") { /* {{{ */
        /* These tests are meant to check XML unpacking in some unusual
         * conditions.
         * Packing and unpacking in usual conditions (ie. valid XML packed by
         * our packer) is already stressed by the other tests.
         */
        t_scope;
        lstr_t file;
        tstiop_inheritance__c2__t *c2 = NULL;
        tstiop_inheritance__c3__t *c3 = NULL;
        tstiop_inheritance__a3__t *a3 = NULL;
        tstiop_inheritance__c5__t *c5 = NULL;

#define MAP(_filename)  \
        do {                                                                 \
            Z_ASSERT_N(lstr_init_from_file(&file,                            \
                           t_fmt("%*pM/iop/" _filename,                      \
                                 LSTR_FMT_ARG(z_cmddir_g)),                  \
                           PROT_READ, MAP_SHARED));                          \
        } while (0)

#define UNPACK_OK(_filename, _type)  \
        do {                                                                 \
            MAP(_filename);                                                  \
            Z_ASSERT_N(xmlr_setup(&xmlr_g, file.s, file.len));               \
            Z_ASSERT_N(t_iop_xunpack_ptr(xmlr_g,                             \
                                         &tstiop_inheritance__##_type##__s,  \
                                         (void **)&_type),                   \
                       "XML unpacking failure: %s", xmlr_get_err());         \
            lstr_wipe(&file);                                                \
        } while (0)

#define UNPACK_FAIL(_filename, _type, _flags, _err)  \
        do {                                                                 \
            MAP(_filename);                                                  \
            Z_ASSERT_N(xmlr_setup(&xmlr_g, file.s, file.len));               \
            Z_ASSERT_NEG(t_iop_xunpack_ptr_flags(xmlr_g,                     \
                                &tstiop_inheritance__##_type##__s,           \
                                (void **)&_type, _flags));                   \
            Z_ASSERT(strstr(xmlr_get_err(), _err), "%s", xmlr_get_err());    \
            lstr_wipe(&file);                                                \
        } while (0)

        /* Test that 'xsi:type' can be missing, if the packed object is of
         * the expected type. */
        UNPACK_OK("tstiop_inheritance_valid1.xml", c2);
        Z_ASSERT(c2->__vptr == &tstiop_inheritance__c2__s);
        Z_ASSERT_EQ(c2->a,  15);
        Z_ASSERT_EQ(c2->a2, 16);
        Z_ASSERT_EQ(c2->b,  false);
        Z_ASSERT_EQ(c2->c,  18);

        /* Test with missing optional fields */
        UNPACK_OK("tstiop_inheritance_valid2.xml", c3);
        Z_ASSERT(c3->__vptr == &tstiop_inheritance__c3__s);
        Z_ASSERT_LSTREQUAL(c3->a, LSTR("I am the only field"));
        Z_ASSERT_EQ(c3->b, 5);
        Z_ASSERT_EQ(c3->c, 6);

        /* Test with no field at all (all are optional) */
        UNPACK_OK("tstiop_inheritance_valid3.xml", c3);
        Z_ASSERT(c3->__vptr == &tstiop_inheritance__c3__s);
        Z_ASSERT_LSTREQUAL(c3->a, LSTR("A2"));
        Z_ASSERT_EQ(c3->b, 5);
        Z_ASSERT_EQ(c3->c, 6);

        /* Test with fields in bad order */
        UNPACK_FAIL("tstiop_inheritance_invalid1.xml", c2, 0,
                    "near /root/a: unknown tag <a>");
        UNPACK_FAIL("tstiop_inheritance_invalid2.xml", c2, 0,
                    "near /root/b: missing mandatory tag <a2>");

        /* Test with an unknown field */
        UNPACK_FAIL("tstiop_inheritance_invalid3.xml", c2, 0,
                    "near /root/toto: unknown tag <toto>");

        /* Test with a missing mandatory field */
        UNPACK_FAIL("tstiop_inheritance_invalid4.xml", c2, 0,
                    "near /root: missing mandatory tag <a2>");

        /* Test with an unknown/incompatible class */
        UNPACK_FAIL("tstiop_inheritance_invalid5.xml", c2, 0,
                    "near /root: class `tstiop_inheritance.Toto' not found");
        UNPACK_FAIL("tstiop_inheritance_invalid6.xml", c2, 0,
                    "near /root: class `tstiop_inheritance.C1' is not a "
                    "child of `tstiop_inheritance.C2'");
        UNPACK_FAIL("tstiop_inheritance_invalid7.xml", a3, 0,
                    "near /root: class `tstiop_inheritance.A3' is an "
                    "abstract class");

        /* 'xsi:type' is mandatory for abstract classes */
        UNPACK_FAIL("tstiop_inheritance_invalid8.xml", a3, 0,
                    "near /root: type attribute not found (mandatory for "
                    "abstract classes)");

        /* Check that private classes cannot be unpacked if ask so.
         */
        UNPACK_OK("tstiop_inheritance_invalid9.xml", c5);
        Z_ASSERT(c5->__vptr == &tstiop_inheritance__c5__s);
        UNPACK_FAIL("tstiop_inheritance_invalid9.xml", c5,
                   IOP_UNPACK_FORBID_PRIVATE,
                   "class `tstiop_inheritance.C5` is private");

#undef UNPACK_OK
#undef UNPACK_FAIL
#undef MAP
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_references, "test iop references") { /* {{{ */
        t_scope;
        SB_1k(err);
        tstiop__my_referenced_struct__t rs = { .a = 666 };
        tstiop__my_referenced_union__t ru;
        tstiop__my_ref_union__t uu;
        tstiop__my_ref_union__t us;
        tstiop__my_ref_struct__t s = {
            .s = &rs,
            .u = &ru
        };

        uu = IOP_UNION(tstiop__my_ref_union, u, &ru);
        us = IOP_UNION(tstiop__my_ref_union, s, &rs);
        ru = IOP_UNION(tstiop__my_referenced_union, b, 42);

#define XUNPACK_OK(_type, _str)                                              \
        do {                                                                 \
            void *_type = NULL;                                              \
                                                                             \
            Z_ASSERT_N(xmlr_setup(&xmlr_g, _str, strlen(_str)));             \
            Z_ASSERT_N(t_iop_xunpack_ptr(xmlr_g, &tstiop__##_type##__s,      \
                                         &_type),                            \
                       "XML unpacking failure: %s", xmlr_get_err());         \
        } while (0)

#define XUNPACK_FAIL(_type, _str, _err)                                      \
        do {                                                                 \
            void *_type = NULL;                                              \
                                                                             \
            Z_ASSERT_N(xmlr_setup(&xmlr_g, _str, strlen(_str)));             \
            Z_ASSERT_NEG(t_iop_xunpack_ptr(xmlr_g, &tstiop__##_type##__s,    \
                                           &_type));                         \
            Z_ASSERT(strstr(xmlr_get_err(), _err), "%s", xmlr_get_err());    \
        } while (0)

#define JUNPACK_FAIL(_type, _str, _err)                                      \
        do {                                                                 \
            void *_type = NULL;                                              \
            pstream_t ps = ps_initstr(_str);                                 \
                                                                             \
            sb_reset(&err);                                                  \
            Z_ASSERT_NEG(t_iop_junpack_ptr_ps(&ps, &tstiop__##_type##__s,    \
                                              &_type, 0, &err));             \
            Z_ASSERT(strstr(err.data, _err), "%s", err.data);                \
        } while (0)

        Z_HELPER_RUN(iop_std_test_struct(&tstiop__my_ref_struct__s, &s, "s"));
        Z_HELPER_RUN(iop_json_test_struct(&tstiop__my_ref_struct__s, &s, "s"));
        Z_HELPER_RUN(iop_xml_test_struct(&tstiop__my_ref_struct__s, &s, "s"));
        XUNPACK_OK(my_ref_struct,
                   "<MyRefStruct><s><a>2</a></s><u><b>1</b></u></MyRefStruct>");
        XUNPACK_FAIL(my_ref_struct,
                     "<MyRefStruct><u><b>1</b></u></MyRefStruct>",
                     "missing mandatory tag <s>");
        XUNPACK_FAIL(my_ref_struct,
                     "<MyRefStruct><u><b>1</b></u></MyRefStruct>",
                     "missing mandatory tag <s>");
        XUNPACK_FAIL(my_ref_struct,
                     "<MyRefStruct><s></s></MyRefStruct>",
                     "missing mandatory tag <a>");
        Z_ASSERT_IOPJSONEQUAL(tstiop__my_ref_struct, &s,
                              LSTR("{ u: { b: 42 }, s: { a: 666 } }"));
        Z_ASSERT_IOPJSONEQUAL(tstiop__my_ref_struct, &s,
                              LSTR("{ u.b: 42, s: { a: 666 } }"));
        JUNPACK_FAIL(my_ref_struct, "{ u: { b: 1 } }",
                     "member `tstiop.MyRefStruct:s' is missing");
        JUNPACK_FAIL(my_ref_struct, "{ s: { a: 1 } }",
                     "member `tstiop.MyRefStruct:u' is missing");

        Z_HELPER_RUN(iop_std_test_struct(&tstiop__my_ref_union__s, &uu, "uu"));
        Z_HELPER_RUN(iop_json_test_struct(&tstiop__my_ref_union__s, &uu, "uu"));
        Z_HELPER_RUN(iop_xml_test_struct(&tstiop__my_ref_union__s, &uu, "uu"));
        Z_HELPER_RUN(iop_std_test_struct(&tstiop__my_ref_union__s, &us, "us"));
        Z_HELPER_RUN(iop_json_test_struct(&tstiop__my_ref_union__s, &us, "us"));
        Z_HELPER_RUN(iop_xml_test_struct(&tstiop__my_ref_union__s, &us, "us"));
        XUNPACK_OK(my_ref_union, "<MyRefUnion><s><a>2</a></s></MyRefUnion>");
        XUNPACK_OK(my_ref_union, "<MyRefUnion><u><b>2</b></u></MyRefUnion>");
        XUNPACK_FAIL(my_ref_union, "<MyRefUnion></MyRefUnion>",
                     "node has no children");
        XUNPACK_FAIL(my_ref_union, "<MyRefUnion><u></u></MyRefUnion>",
                     "node has no children");
        XUNPACK_FAIL(my_ref_union,
                     "<MyRefUnion><s><a>2</a></s><u><b>1</b></u></MyRefUnion>",
                     "closing tag expected");
        Z_ASSERT_IOPJSONEQUAL(tstiop__my_ref_union, &uu,
                              LSTR("{ u: { b: 42 } }"));
        Z_ASSERT_IOPJSONEQUAL(tstiop__my_ref_union, &uu,
                              LSTR("{ u.b: 42 }"));
        Z_ASSERT_IOPJSONEQUAL(tstiop__my_ref_union, &us,
                              LSTR("{ s: { a: 666 } }"));

#undef JUNPACK_FAIL
#undef XUNPACK_OK
#undef XUNPACK_FAIL
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_get_field_len, "test iop_get_field_len") { /* {{{ */
        t_scope;

        tstiop__my_class2__t cls2;
        tstiop__my_union_a__t ua = IOP_UNION(tstiop__my_union_a, ua, 1);
        tstiop__my_struct_a__t sa = {
            .a = 42,
            .b = 5,
            .c_of_my_struct_a = 120,
            .d = 230,
            .e = 540,
            .f = 2000,
            .g = 10000,
            .h = 20000,
            .i = LSTR_IMMED("foo"),
            .j = LSTR_IMMED("baré© \" foo ."),
            .k = MY_ENUM_A_B,
            .l = IOP_UNION(tstiop__my_union_a, ub, 42),
            .lr = &ua,
            .cls2 = &cls2,
            .m = 3.14159265,
            .n = true,
        };

        iop_dso_t *dso;
        const iop_struct_t *st_sa, *st_cls2;
        qv_t(i32) szs;
        int len;
        byte *dst;
        pstream_t ps;

        dso = Z_DSO_OPEN();

        Z_ASSERT_P(st_sa = iop_dso_find_type(dso, LSTR("tstiop.MyStructA")));
        Z_ASSERT_P(st_cls2 = iop_dso_find_type(dso, LSTR("tstiop.MyClass2")));

        t_qv_init(&szs, 1024);
        iop_init_desc(st_cls2, &cls2);

        /* packing */
        Z_ASSERT_N((len = iop_bpack_size(st_sa, &sa, &szs)),
                   "invalid structure size (%s)", st_sa->fullname.s);
        dst = t_new(byte, len);
        iop_bpack(dst, st_sa, &sa, szs.tab);

        ps = ps_init(dst, len);
        while (!ps_done(&ps)) {
            Z_ASSERT_GT(len = iop_get_field_len(ps), 0);
            Z_ASSERT_N(ps_skip(&ps, len));
        }

        iop_dso_close(&dso);
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_struct_for_each_field, "test iop_struct_for_each_field") { /* {{{ */
        tstiop__my_class1__t cls1;
        tstiop__my_class2__t cls2;
        tstiop__my_class3__t cls3;
        int i = 0;

        iop_init(tstiop__my_class1, &cls1);
        iop_init(tstiop__my_class2, &cls2);
        iop_init(tstiop__my_class3, &cls3);

#define TEST_FIELD(_f, _type, _name, _st, _class)                       \
        do {                                                            \
            Z_ASSERT_EQ((int)_f->type, IOP_T_##_type);                  \
            Z_ASSERT_LSTREQUAL(_f->name, LSTR(_name));                  \
            Z_ASSERT(_st == _class.__vptr);                             \
        } while (0)

        iop_obj_for_each_field(f, st, &cls3) {
            switch (i) {
              case 0:
                TEST_FIELD(f, I32, "int3", st, cls3);
                break;
              case 1:
                TEST_FIELD(f, BOOL, "bool1", st, cls3);
                break;
              case 2:
                TEST_FIELD(f, STRING, "string1", st, cls3);
                break;
              case 3:
                TEST_FIELD(f, STRUCT, "nextClass", st, cls3);
                break;
              case 4:
                TEST_FIELD(f, I32, "int2", st, cls2);
                break;
              case 5:
                TEST_FIELD(f, I32, "int1", st, cls1);
                break;
              default:
                Z_ASSERT(false);
            }
            i++;
        }
        Z_ASSERT_EQ(i, 6);

        i = 0;
        iop_obj_for_each_field(f, st, &cls2) {
            switch (i) {
              case 0:
                TEST_FIELD(f, I32, "int2", st, cls2);
                break;
              case 1:
                TEST_FIELD(f, I32, "int1", st, cls1);
                break;
              default:
                Z_ASSERT(false);
            }
            i++;
        }
        Z_ASSERT_EQ(i, 2);

        i = 0;
        iop_obj_for_each_field(f, st, &cls1) {
            TEST_FIELD(f, I32, "int1", st, cls1);
            Z_ASSERT_EQ(i, 0);
            i++;
        }

        /* Imbrication */
        i = 0;
        iop_obj_for_each_field(f, st, &cls3) {
            int j = 0;

            iop_obj_for_each_field(f2, st2, &cls1) {
                TEST_FIELD(f2, I32, "int1", st2, cls1);
                Z_ASSERT_EQ(j, 0);
                j++;
            }

            switch (i) {
              case 0:
                TEST_FIELD(f, I32, "int3", st, cls3);
                break;
              case 1:
                TEST_FIELD(f, BOOL, "bool1", st, cls3);
                break;
              case 2:
                TEST_FIELD(f, STRING, "string1", st, cls3);
                break;
              case 3:
                TEST_FIELD(f, STRUCT, "nextClass", st, cls3);
                break;
              case 4:
                TEST_FIELD(f, I32, "int2", st, cls2);
                break;
              case 5:
                TEST_FIELD(f, I32, "int1", st, cls1);
                break;
              default:
                Z_ASSERT(false);
            }
            i++;
        }

#undef TEST_FIELD

    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_get_field, "test iop_get_field function") { /* {{{ */
        tstiop__my_struct_a__t struct_a;
        tstiop__my_struct_b__t struct_b;
        tstiop__my_struct_c__t struct_c;
        tstiop__my_struct_e__t struct_e;
        tstiop__my_struct_f__t struct_f;
        tstiop__my_class3__t cls3;
        tstiop__my_struct_a_opt__t struct_a_opt;
        tstiop__my_ref_struct__t struct_ref;
        tstiop__my_referenced_struct__t referenced_struct;
        const iop_field_t *iop_field;
        const iop_struct_t *out_st = NULL;
        const void *out = NULL;
        uint64_t htab_vals[] = {42, 22};
        lstr_t f_a_vals[] = {LSTR("test1"), LSTR("test2")};
        lstr_t f_b_vals[] = {LSTR("foo"), LSTR("bar")};
        int f_c_0_b_vals[] = {42, 16};
        int f_c_1_b_vals[] = {20, 56};
        tstiop__my_struct_b__t f_c_vals[] = {
            {.a = OPT(12), .b = IOP_ARRAY(f_c_0_b_vals,
                                          countof(f_c_0_b_vals))},
            {.a = OPT_NONE, .b = IOP_ARRAY(f_c_1_b_vals,
                                           countof(f_c_1_b_vals))},
        };
        tstiop__my_union_a__t f_d_vals[] = {
            IOP_UNION(tstiop__my_union_a, ua, 25),
            IOP_UNION(tstiop__my_union_a, ub, 0xAA),
            IOP_UNION(tstiop__my_union_a, us, LSTR("toto")),
        };
        tstiop__my_class1__t f_e_cls1;
        tstiop__my_class2__t f_e_cls2;
        tstiop__my_class3__t f_e_cls3;
        tstiop__my_class1__t *f_e_vals[3];

        iop_init(tstiop__my_class1, &f_e_cls1);
        iop_init(tstiop__my_class2, &f_e_cls2);
        iop_init(tstiop__my_class3, &f_e_cls3);

        f_e_cls1.int1 = 1;
        f_e_cls2.int1 = 2;
        f_e_cls2.int2 = 3;
        f_e_cls3.int1 = 5;
        f_e_cls3.int2 = 8;
        f_e_cls3.int3 = 13;

        f_e_vals[0] = &f_e_cls1;
        f_e_vals[1] = iop_obj_vcast(tstiop__my_class1, &f_e_cls2);
        f_e_vals[2] = iop_obj_vcast(tstiop__my_class1, &f_e_cls3);

        iop_init(tstiop__my_struct_a, &struct_a);
        iop_init(tstiop__my_struct_b, &struct_b);
        iop_init(tstiop__my_struct_c, &struct_c);
        iop_init(tstiop__my_struct_e, &struct_e);
        iop_init(tstiop__my_struct_f, &struct_f);
        iop_init(tstiop__my_class3, &cls3);
        iop_init(tstiop__my_struct_a_opt, &struct_a_opt);
        iop_init(tstiop__my_ref_struct, &struct_ref);
        iop_init(tstiop__my_referenced_struct, &referenced_struct);
        cls3.int3 = 10;
        cls3.int2 = 5;
        cls3.int1 = 2;
        cls3.bool1 = true;
        struct_a.a = 15;
        struct_a.j = LSTR("toto");
        struct_a.l = IOP_UNION(tstiop__my_union_a, ua, 25);
        struct_a.cls2 = iop_obj_vcast(tstiop__my_class2, &cls3);
        OPT_SET(struct_b.a, 5);
        struct_c.b = &struct_c;
        struct_a_opt.l = &IOP_UNION(tstiop__my_union_a, ua, 10);
        referenced_struct.a = 21;
        struct_ref.s = &referenced_struct;
        OPT_SET(struct_e.c.a, 42);
        struct_a.htab = (iop_array_u64_t)IOP_ARRAY(htab_vals,
                                                   countof(htab_vals));
        struct_f.a = (iop_array_lstr_t)IOP_ARRAY(f_a_vals, countof(f_a_vals));
        struct_f.b = (iop_array_lstr_t)IOP_ARRAY(f_b_vals, countof(f_b_vals));
        struct_f.c = (IOP_ARRAY_T(tstiop__my_struct_b))
                      IOP_ARRAY(f_c_vals, countof(f_c_vals));
        struct_f.d = (IOP_ARRAY_T(tstiop__my_union_a))
                      IOP_ARRAY(f_d_vals, countof(f_d_vals));
        struct_f.e = (IOP_ARRAY_T(tstiop__my_class1))
                      IOP_ARRAY(f_e_vals, countof(f_e_vals));

        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("unknown_field"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR(""), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("."), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR(".a"), NULL, NULL));
        Z_ASSERT_P(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                       LSTR("l."), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("l.."), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("z[5]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[42]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[]]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[a]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[0a]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[0]a"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("htab[-42]"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                          LSTR("c.a"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                          LSTR("e[0].int2"), NULL, NULL));
        Z_ASSERT_NULL(iop_get_field_const(&f_d_vals[0],
                                          &tstiop__my_union_a__s,
                                          LSTR("ub"), NULL, NULL));

        Z_ASSERT_P(iop_get_field_const(&f_e_cls3, &tstiop__my_class3__s,
                                       LSTR("int3"), NULL, NULL));
        Z_ASSERT_P(iop_get_field_const(&f_e_cls3.super, &tstiop__my_class2__s,
                                       LSTR("int3"), NULL, NULL));

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_EQ(*(int *)out, struct_a.a);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("l"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_union_a, out, &struct_a.l);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("l.ua"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_union_a__s);
        Z_ASSERT_EQ(*(int *)out, struct_a.l.ua);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("cls2"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_class2, *(tstiop__my_class2__t **)out,
                          struct_a.cls2);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("cls2.int2"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class2__s);
        Z_ASSERT_EQ(*(int *)out, struct_a.cls2->int2);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("cls2.int1"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class1__s);
        Z_ASSERT_EQ(*(int *)out, struct_a.cls2->int1);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("cls2.bool1"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class3__s);
        Z_ASSERT_EQ(*(bool *)out, cls3.bool1);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("j"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_LSTREQUAL(*(lstr_t *)out, struct_a.j);

        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("cls2.bool10"), NULL, NULL));

        iop_field = iop_get_field_const(&struct_e, &tstiop__my_struct_e__s,
                                        LSTR("c"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_e__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_struct_b, out, &struct_e.c);

        iop_field = iop_get_field_const(&struct_e, &tstiop__my_struct_e__s,
                                        LSTR("c.a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_b__s);
        Z_ASSERT_EQ(*(int *)out, OPT_VAL(struct_e.c.a));

        iop_field = iop_get_field_const(&struct_b, &tstiop__my_struct_b__s,
                                        LSTR("a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_b__s);
        Z_ASSERT(OPT_ISSET(*(opt_i32_t *)out));
        Z_ASSERT_OPT_EQ(*(opt_i32_t *)out, struct_b.a);

        Z_ASSERT_NULL(iop_get_field_const(&struct_a,
                                          &tstiop__my_struct_a__s,
                                          LSTR("a.b"), NULL, NULL));

        iop_field = iop_get_field_const(&struct_a_opt,
                                        &tstiop__my_struct_a_opt__s,
                                        LSTR("l"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a_opt__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_union_a, *(tstiop__my_union_a__t **)out,
                          struct_a_opt.l);

        iop_field = iop_get_field_const(&struct_a_opt,
                                        &tstiop__my_struct_a_opt__s,
                                        LSTR("l.ua"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_union_a__s);
        Z_ASSERT_EQ(*((int *)out), struct_a_opt.l->ua);

        iop_field = iop_get_field_const(&struct_c, &tstiop__my_struct_c__s,
                                        LSTR("b.a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_c__s);
        Z_ASSERT_EQ(*(int *)out, struct_c.b->a);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("lr"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_NULL(*((tstiop__my_union_a__t **)out));

        Z_ASSERT_NULL(iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                          LSTR("lr.ua"), &out, &out_st));

        iop_field = iop_get_field_const(&struct_ref,
                                        &tstiop__my_ref_struct__s,
                                        LSTR("s"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_ref_struct__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_referenced_struct,
                          *(tstiop__my_referenced_struct__t **)out,
                          struct_ref.s);

        iop_field = iop_get_field_const(&struct_ref,
                                        &tstiop__my_ref_struct__s,
                                        LSTR("s.a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_referenced_struct__s);
        Z_ASSERT_EQ(*(int *)out, struct_ref.s->a);

        Z_ASSERT_NULL(iop_get_field_const(&struct_ref,
                                          &tstiop__my_ref_struct__s,
                                          LSTR("u.b"), &out, &out_st));

        iop_field = iop_get_field_const(&struct_c, &tstiop__my_struct_c__s,
                                        LSTR("b.b.a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_c__s);
        Z_ASSERT_EQ(*(int *)out, struct_c.b->b->a);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("htab[0]"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_EQ(*(uint64_t *)out, struct_a.htab.tab[0]);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("htab[1]"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_EQ(*(uint64_t *)out, struct_a.htab.tab[1]);

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("htab[-1]"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT(out == tab_last(&struct_a.htab));

        iop_field = iop_get_field_const(&struct_a, &tstiop__my_struct_a__s,
                                        LSTR("htab"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_a__s);
        Z_ASSERT_EQ(((iop_array_u64_t *)out)->len, countof(htab_vals));

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("a[1]"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT_LSTREQUAL(*(lstr_t *)out, struct_f.a.tab[1]);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("b[1]"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_f__s);
        Z_ASSERT_LSTREQUAL(*(lstr_t *)out, struct_f.b.tab[1]);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("c[1].a"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_b__s);
        Z_ASSERT_EQ(OPT_ISSET(*(opt_i32_t *)out),
                    OPT_ISSET(struct_f.c.tab[1].a));

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("c[0].b[1]"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_struct_b__s);
        Z_ASSERT_EQ(*(int *)out, struct_f.c.tab[0].b.tab[1]);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("d[0].ua"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_union_a__s);
        Z_ASSERT_EQ(*(int *)out, *IOP_UNION_GET(tstiop__my_union_a,
                                                &struct_f.d.tab[0], ua));

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("d[1].ub"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_union_a__s);
        Z_ASSERT_EQ(*(int8_t *)out, *IOP_UNION_GET(tstiop__my_union_a,
                                                   &struct_f.d.tab[1], ub));

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("d[2].us"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_union_a__s);
        Z_ASSERT_LSTREQUAL(*(lstr_t *)out, *IOP_UNION_GET(tstiop__my_union_a,
                                                          &struct_f.d.tab[2],
                                                          us));

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("e[0].int1"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class1__s);
        Z_ASSERT_EQ(*(int *)out, f_e_cls1.int1);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("e[1].int1"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class1__s);
        Z_ASSERT_EQ(*(int *)out, f_e_cls2.int1);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("e[1].int2"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class2__s);
        Z_ASSERT_EQ(*(int *)out, f_e_cls2.int2);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("e[2].int1"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class1__s);
        Z_ASSERT_EQ(*(int *)out, f_e_cls3.int1);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("e[2].int2"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class2__s);
        Z_ASSERT_EQ(*(int *)out, f_e_cls3.int2);

        iop_field = iop_get_field_const(&struct_f, &tstiop__my_struct_f__s,
                                        LSTR("e[2].bool1"), &out, &out_st);
        Z_ASSERT_P(iop_field);
        Z_ASSERT_P(out);
        Z_ASSERT(out_st == &tstiop__my_class3__s);
        Z_ASSERT(out == &f_e_cls3.bool1);
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_get_field_values, "test iop_get_field_values function") { /* {{{ */
        t_scope;
        tstiop__z_iop_get_field_values__t z_struct;

        iop_init(tstiop__z_iop_get_field_values, &z_struct);
#define TEST(field_path, exp_ptr, exp_len, exp_is_array_of_pointers)         \
        Z_HELPER_RUN(z_iop_get_field_values_check(                           \
                &tstiop__z_iop_get_field_values__s, &z_struct, (field_path), \
                (exp_ptr), (exp_len), (exp_is_array_of_pointers)));

        TEST("integer", &z_struct.integer, 1, false);
        TEST("integerTab", z_struct.integer_tab.tab, 0, false);
        TEST("optInteger", NULL, 0, false);
        OPT_SET(z_struct.opt_integer, 666);
        TEST("optInteger", &z_struct.opt_integer.v, 1, false);

        TEST("st", &z_struct.st, 1, false);
        TEST("optSt", z_struct.opt_st, 0, false);
        z_struct.opt_st = t_iop_new(tstiop__simple_struct);
        TEST("optSt", z_struct.opt_st, 1, false);
        z_struct.st_ref = t_iop_new(tstiop__simple_struct);
        TEST("stRef", z_struct.st_ref, 1, false);
        TEST("stTab", z_struct.st_tab.tab, 0, false);
        z_struct.st_tab = T_IOP_ARRAY_NEW(tstiop__simple_struct, 42);
        TEST("stTab", z_struct.st_tab.tab, 42, false);

        z_struct.obj = t_iop_new(tstiop__simple_class);
        TEST("obj", z_struct.obj, 1, false);
        TEST("optObj", z_struct.opt_obj, 0, false);
        z_struct.opt_obj = t_iop_new(tstiop__simple_class);
        TEST("optObj", z_struct.opt_obj, 1, false);
        TEST("objTab", z_struct.obj_tab.tab, 0, true);
        z_struct.obj_tab = T_IOP_ARRAY_NEW(tstiop__simple_class, 1);
        TEST("objTab", z_struct.obj_tab.tab, 1, true);

        TEST("v", NULL, 0, false);
        TEST("optVoid", NULL, 0, false);
        z_struct.opt_void = true;
        TEST("optVoid", NULL, 1, false);

#undef TEST
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_value_from_field, "test iop_value_from_field") { /* {{{ */
        tstiop__my_struct_g__t sg;
        const iop_struct_t *st;
        const iop_field_t *field;
        iop_value_t value;

        iop_init(tstiop__my_struct_g, &sg);

        st = &tstiop__my_struct_g__s;

#define TEST_FIELD(_n, _type, _u, _res)                                    \
        field = &st->fields[_n];                                           \
        Z_ASSERT_N(iop_value_from_field((void *) &sg, field, &value));     \
        Z_ASSERT_EQ(value._u, (_type) _res)

        TEST_FIELD(0, int64_t, i, -1);
        TEST_FIELD(1, uint64_t, u, 2);
        TEST_FIELD(11, double, d, 10.5);

#undef TEST_FIELD

        field = &st->fields[9];
        Z_ASSERT_N(iop_value_from_field((void *) &sg, field, &value));
        Z_ASSERT_LSTREQUAL(value.s, LSTR("fo\"o?cbaré©"));

        /* test to get struct */
        {
            tstiop__my_struct_k__t sk;
            tstiop__my_struct_j__t *sj;

            iop_init(tstiop__my_struct_k, &sk);

            sk.j.cval = 2314;
            st = &tstiop__my_struct_k__s;
            field = &st->fields[0];
            Z_ASSERT_N(iop_value_from_field((void *) &sk, field, &value));
            sj = value.s.data;
            Z_ASSERT_EQ(sj->cval, 2314);
        }

        /* test to get reference */
        {
            tstiop__my_ref_struct__t ref_st;
            tstiop__my_referenced_struct__t referenced_st;
            tstiop__my_referenced_struct__t *p;

            iop_init(tstiop__my_ref_struct, &ref_st);
            iop_init(tstiop__my_referenced_struct, &referenced_st);

            referenced_st.a = 23;
            ref_st.s = &referenced_st;

            st = &tstiop__my_ref_struct__s;
            field = &st->fields[0];
            Z_ASSERT_N(iop_value_from_field((void *) &ref_st, field, &value));
            p = value.s.data;
            Z_ASSERT_EQ(p->a, 23);
        }

        /* test to get optional */
        {
            t_scope;
            tstiop__my_struct_a_opt__t s;
            tstiop__my_struct_b__t sb;

            st = &tstiop__my_struct_a_opt__s;

            /* simple field */
            iop_init(tstiop__my_struct_a_opt, &s);
            OPT_SET(s.a, 42);

            field = &st->fields[0];
            Z_ASSERT_N(iop_value_from_field((void *)&s, field, &value));
            Z_ASSERT_EQ(value.i, 42);

            iop_init(tstiop__my_struct_a_opt, &s);
            Z_ASSERT_EQ(iop_value_from_field((void *)&s, field, &value),
                        IOP_FIELD_NOT_SET);

            /* string field */
            iop_init(tstiop__my_struct_a_opt, &s);
            s.j = LSTR("abc");
            field = &st->fields[9];
            Z_ASSERT_N(iop_value_from_field((void *)&s, field, &value));
            Z_ASSERT_LSTREQUAL(value.s, LSTR("abc"));

            iop_init(tstiop__my_struct_a_opt, &s);
            Z_ASSERT_EQ(iop_value_from_field((void *)&s, field, &value),
                        IOP_FIELD_NOT_SET);

            /* struct field */
            iop_init(tstiop__my_struct_a_opt, &s);
            s.o = t_iop_new(tstiop__my_struct_b);
            OPT_SET(s.o->a, 42);

            field = &st->fields[15];
            Z_ASSERT_N(iop_value_from_field((void *)&s, field, &value));
            Z_ASSERT_P(value.v);
            Z_ASSERT_EQ(OPT_VAL(((tstiop__my_struct_b__t *)value.v)->a), 42);

            iop_init(tstiop__my_struct_a_opt, &s);
            Z_ASSERT_EQ(iop_value_from_field((void *)&s, field, &value),
                        IOP_FIELD_NOT_SET);

            /* class field */
            iop_init(tstiop__my_struct_a_opt, &s);
            s.cls2 = t_iop_new(tstiop__my_class2);
            s.cls2->int2 = 42;

            field = &st->fields[16];
            Z_ASSERT_N(iop_value_from_field((void *)&s, field, &value));
            Z_ASSERT_P(value.v);
            Z_ASSERT_EQ(((tstiop__my_class2__t *)value.v)->int2, 42);

            iop_init(tstiop__my_struct_a_opt, &s);
            Z_ASSERT_EQ(iop_value_from_field((void *)&s, field, &value),
                        IOP_FIELD_NOT_SET);

            /* not handled array field */
            iop_init(tstiop__my_struct_b, &sb);
            sb.b.tab = t_new_raw(int, 1);
            sb.b.tab[0] = 42;
            sb.b.len = 1;

            st = &tstiop__my_struct_b__s;
            field = &st->fields[1];
            Z_ASSERT_EQ(iop_value_from_field((void *)&s, field, &value),
                        IOP_FIELD_ERROR);

            iop_init(tstiop__my_struct_b, &sb);
            Z_ASSERT_EQ(iop_value_from_field((void *)&s, field, &value),
                        IOP_FIELD_ERROR);
        }

        /* test with iop_get_field */
        {
            tstiop__my_struct_a__t struct_a;
            tstiop__my_class2__t cls2;
            const void *ptr;

            iop_init(tstiop__my_struct_a, &struct_a);
            iop_init(tstiop__my_class2, &cls2);
            struct_a.a = 42;
            struct_a.l = IOP_UNION(tstiop__my_union_a, ua, 21);
            struct_a.lr = &struct_a.l;
            cls2.int1 = 12;
            struct_a.cls2 = &cls2;
            st = &tstiop__my_struct_a__s;

            field = iop_get_field_const(&struct_a, st, LSTR("a"), &ptr, NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            Z_ASSERT_N(iop_value_from_field(ptr, field, &value));
            Z_ASSERT_EQ(value.i, struct_a.a);

            field = iop_get_field_const(&struct_a, st, LSTR("l.ua"), &ptr,
                                        NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            Z_ASSERT_N(iop_value_from_field(ptr, field, &value));
            Z_ASSERT_EQ(value.i, struct_a.l.ua);

            field = iop_get_field_const(&struct_a, st, LSTR("lr"), &ptr,
                                        NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            Z_ASSERT_N(iop_value_from_field(ptr, field, &value));
            Z_ASSERT_EQ(((tstiop__my_union_a__t *)value.p)->ua,
                        struct_a.lr->ua);

            field = iop_get_field_const(&struct_a, st, LSTR("cls2"), &ptr,
                                        NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            Z_ASSERT_N(iop_value_from_field(ptr, field, &value));
            Z_ASSERT_EQ(((tstiop__my_class2__t *)value.p)->int1,
                        struct_a.cls2->int1);
        }
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_value_to_field, "test iop_value_to_field") { /* {{{ */
        tstiop__my_struct_g__t sg;
        tstiop__my_struct_k__t sk;
        tstiop__my_struct_j__t sj;
        tstiop__my_struct_a_opt__t saopt;
        const iop_struct_t *st;
        const iop_field_t *field;
        iop_value_t value;

        iop_init(tstiop__my_struct_g, &sg);
        iop_init(tstiop__my_struct_k, &sk);
        iop_init(tstiop__my_struct_j, &sj);
        iop_init(tstiop__my_struct_a_opt, &saopt);

        /* test with int */
        st = &tstiop__my_struct_g__s;
        field = &st->fields[0];
        value.i = 2314;
        iop_value_to_field((void *) &sg, field, &value);
        Z_ASSERT_EQ(sg.a, 2314);

        /* test with optional int */
        st = &tstiop__my_struct_a_opt__s;
        field = &st->fields[0];
        iop_value_to_field((void *) &saopt, field, &value);
        Z_ASSERT(OPT_ISSET(saopt.a));
        Z_ASSERT_EQ(OPT_VAL(saopt.a), 2314);

        /* test with string */
        st = &tstiop__my_struct_g__s;
        field = &st->fields[9];
        value.s = LSTR("fo\"o?cbaré©");
        iop_value_to_field((void *) &sg, field, &value);
        Z_ASSERT_LSTREQUAL(sg.j, LSTR("fo\"o?cbaré©"));

        /* test with optional string */
        st = &tstiop__my_struct_a_opt__s;
        field = &st->fields[9];
        iop_value_to_field((void *) &saopt, field, &value);
        Z_ASSERT_LSTREQUAL(saopt.j, LSTR("fo\"o?cbaré©"));

        /* test struct */
        sj.cval = 42;
        value.p = &sj;
        st = &tstiop__my_struct_k__s;
        field = &st->fields[0];
        iop_value_to_field((void *) &sk, field, &value);
        Z_ASSERT_EQ(sk.j.cval, 42);

        /* test to get reference */
        {
            t_scope;
            tstiop__my_ref_struct__t ref_st;
            tstiop__my_referenced_struct__t referenced_st;

            iop_init(tstiop__my_ref_struct, &ref_st);
            iop_init(tstiop__my_referenced_struct, &referenced_st);

            referenced_st.a = 23;
            ref_st.s = t_new(tstiop__my_referenced_struct__t, 1);
            iop_init(tstiop__my_referenced_struct, ref_st.s);

            value.p = &referenced_st;

            st = &tstiop__my_ref_struct__s;
            field = &st->fields[0];
            iop_value_to_field((void *) &ref_st, field, &value);
            Z_ASSERT_EQ(ref_st.s->a, 23);
        }

        /* test to get optional */
        {
            tstiop__my_struct_b__t sb;

            iop_init(tstiop__my_struct_b, &sb);

            value.i = 42;
            st = &tstiop__my_struct_b__s;
            field = &st->fields[0];
            iop_value_to_field((void *) &sb, field, &value);
            Z_ASSERT_EQ(*OPT_GET(&sb.a), 42);
        }

        /* test with an array */
        {
            t_scope;
            tstiop__my_struct_b__t sb;
            void *out = NULL;

            field = &tstiop__my_struct_b__s.fields[1];
            iop_init(tstiop__my_struct_b, &sb);
            sb.b.len = 3;
            sb.b.tab = t_new(int, sb.b.len);

            value.i = 42;
            out = ((byte *)&sb.b.tab[1]) - field->data_offs;
            iop_value_to_field(out, field, &value);
            Z_ASSERT_EQ(sb.b.tab[1], 42);
        }

        /* test with iop_get_field */
        {
            tstiop__my_struct_a__t struct_a;
            tstiop__my_class2__t cls2;
            const void *ptr;

            iop_init(tstiop__my_struct_a, &struct_a);
            iop_init(tstiop__my_class2, &cls2);
            cls2.int1 = 12;
            struct_a.cls2 = &cls2;
            struct_a.l = IOP_UNION(tstiop__my_union_a, ua, 69);
            st = &tstiop__my_struct_a__s;

            value.i = 42;
            field = iop_get_field_const(&struct_a, st, LSTR("a"), &ptr, NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            iop_value_to_field((void *)ptr, field, &value);
            Z_ASSERT_EQ(value.i, struct_a.a);

            value.i = 21;
            field = iop_get_field_const(&struct_a, st, LSTR("l.ua"), &ptr,
                                        NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            iop_value_to_field((void *)ptr, field, &value);
            Z_ASSERT_EQ(value.i, struct_a.l.ua);

            value.p = &struct_a.l;
            field = iop_get_field_const(&struct_a, st, LSTR("lr"), &ptr,
                                        NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            iop_value_to_field((void *)ptr, field, &value);
            Z_ASSERT_EQ(struct_a.l.ua, struct_a.lr->ua);

            value.p = &cls2;
            field = iop_get_field_const(&struct_a, st, LSTR("cls2"), &ptr,
                                        NULL);
            Z_ASSERT_P(field);
            ptr = (const byte *)ptr - field->data_offs;
            iop_value_to_field((void *)ptr, field, &value);
            Z_ASSERT_EQ(cls2.int1, struct_a.cls2->int1);
        }
    } Z_TEST_END
    /* }}} */
    Z_TEST(nr_47521, "test bug while unpacking json with bunpack") { /* {{{ */
        /* test that bunpack does not crash when trying to unpack json */
        t_scope;
        SB_1k(sb);
        tstiop__my_struct_b__t b;
        tstiop__my_class1__t c;
        tstiop__my_class1__t *c_ptr = NULL;

        iop_init(tstiop__my_struct_b, &b);
        Z_ASSERT_N(iop_sb_jpack(&sb, &tstiop__my_struct_b__s, &b, 0));
        Z_ASSERT_NEG(t_iop_bunpack(&LSTR_SB_V(&sb), tstiop__my_struct_b, &b));

        iop_init(tstiop__my_class1, &c);
        Z_ASSERT_N(iop_sb_jpack(&sb, &tstiop__my_class1__s, &c, 0));
        Z_ASSERT_NEG(iop_bunpack_ptr(t_pool(), &tstiop__my_class1__s,
                                     (void **)&c_ptr, ps_initsb(&sb), false));
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_enum, "test iop enums") { /* {{{ */
        bool found = false;
        const iop_enum_t *en;

        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "A", -1, -1),
                    MY_ENUM_A_A);
        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "b", -1, -1),
                    MY_ENUM_A_B);
        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "c", -1, -1),
                    MY_ENUM_A_C);

        Z_ASSERT_EQ(iop_enum_from_str2(tstiop__my_enum_a, "A", -1, &found),
                    MY_ENUM_A_A);
        Z_ASSERT_EQ(iop_enum_from_str2(tstiop__my_enum_a, "b", -1, &found),
                    MY_ENUM_A_B);
        Z_ASSERT_EQ(iop_enum_from_str2(tstiop__my_enum_a, "c", -1, &found),
                    MY_ENUM_A_C);

        Z_ASSERT_EQ(iop_enum_from_lstr(tstiop__my_enum_a, LSTR("A"), &found),
                    MY_ENUM_A_A);
        Z_ASSERT_EQ(iop_enum_from_lstr(tstiop__my_enum_a, LSTR("b"), &found),
                    MY_ENUM_A_B);
        Z_ASSERT_EQ(iop_enum_from_lstr(tstiop__my_enum_a, LSTR("c"), &found),
                    MY_ENUM_A_C);

        Z_ASSERT_P(en = iop_get_enum(LSTR("tstiop.MyEnumA")));
        Z_ASSERT_LSTREQUAL(en->fullname, LSTR("tstiop.MyEnumA"));
        Z_ASSERT_LSTREQUAL(en->name, LSTR("MyEnumA"));
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_enum_alias, "test iop enums aliases") { /* {{{ */
        Z_TEST_FLAGS("redmine_52799");
        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "A_ALIAS", -1, -1),
                    iop_enum_from_str(tstiop__my_enum_a, "A", -1, -1));
        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "C_ALIAS_1", -1, -1),
                    iop_enum_from_str(tstiop__my_enum_a, "C", -1, -1));
        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "C_ALIAS_2", -1, -1),
                    iop_enum_from_str(tstiop__my_enum_a, "C", -1, -1));
        Z_ASSERT_EQ(iop_enum_from_str(tstiop__my_enum_a, "D_ALIAS", -1, -1),
                    iop_enum_from_str(tstiop__my_enum_a, "D", -1, -1));
        Z_ASSERT_EQ(MY_ENUM_A_A_ALIAS, MY_ENUM_A_A);
        Z_ASSERT_EQ(MY_ENUM_A_C_ALIAS_1, MY_ENUM_A_C);
        Z_ASSERT_EQ(MY_ENUM_A_C_ALIAS_2, MY_ENUM_A_C);
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_gen_attrs, "test iop generic attributes") { /* {{{ */
        iop_value_t value;
        iop_type_t type;

        /* enum */
        Z_ASSERT_N(iop_enum_get_gen_attr(&tstiop__my_enum_a__e,
                                         LSTR("test:gen1"), IOP_T_I8, NULL,
                                         &value));
        Z_ASSERT_EQ(value.i, 1);
        /* wrong type */
        Z_ASSERT_NEG(iop_enum_get_gen_attr(&tstiop__my_enum_a__e,
                                           LSTR("test:gen1"), IOP_T_STRING,
                                           &type, &value));
        Z_ASSERT_EQ(type, (iop_type_t)IOP_T_I64);
        Z_ASSERT_NEG(iop_enum_get_gen_attr(&tstiop__my_enum_a__e,
                                           LSTR("test:gen2"), IOP_T_I8, NULL,
                                           &value));

        /* enum values */
        Z_ASSERT_N(iop_enum_get_gen_attr_from_str(
            &tstiop__my_enum_a__e, LSTR("A"), LSTR("test:gen2"), IOP_T_DOUBLE,
            NULL, &value));
        Z_ASSERT_EQ(value.d, 2.2);
        Z_ASSERT_N(iop_enum_get_gen_attr_from_str(
            &tstiop__my_enum_a__e, LSTR("a"), LSTR("test:gen2"), IOP_T_DOUBLE,
            NULL, &value));
        Z_ASSERT_EQ(value.d, 2.2);
        Z_ASSERT_N(iop_enum_get_gen_attr_from_val(
            &tstiop__my_enum_a__e, 0, LSTR("test:gen2"), IOP_T_DOUBLE, NULL,
            &value));
        Z_ASSERT_EQ(value.d, 2.2);
        /* wrong type */
        Z_ASSERT_NEG(iop_enum_get_gen_attr_from_val(&tstiop__my_enum_a__e, 0,
                                                    LSTR("test:gen2"),
                                                    IOP_T_I64, &type, &value));
        Z_ASSERT_EQ(type, (iop_type_t)IOP_T_DOUBLE);

        Z_ASSERT_NEG(iop_enum_get_gen_attr_from_str(
            &tstiop__my_enum_a__e, LSTR("b"), LSTR("test:gen2"), IOP_T_I8,
            NULL, &value));
        Z_ASSERT_NEG(iop_enum_get_gen_attr_from_val(&tstiop__my_enum_a__e, 1,
                                                    LSTR("test:gen2"),
                                                    IOP_T_I8, NULL, &value));

        /* struct */
        Z_ASSERT_N(iop_struct_get_gen_attr(&tstiop__my_struct_a__s,
                                           LSTR("test:gen3"), IOP_T_STRING,
                                           NULL, &value));
        Z_ASSERT_LSTREQUAL(value.s, LSTR("3"));
        /* wrong type */
        Z_ASSERT_NEG(iop_struct_get_gen_attr(&tstiop__my_struct_a__s,
                                             LSTR("test:gen3"), IOP_T_I8,
                                             &type, &value));
        Z_ASSERT_EQ(type, (iop_type_t)IOP_T_STRING);
        Z_ASSERT_NEG(iop_struct_get_gen_attr(&tstiop__my_struct_a__s,
                                             LSTR("test:gen1"), IOP_T_I8,
                                             NULL, &value));

        /* struct field */
        Z_ASSERT_N(iop_field_by_name_get_gen_attr(
            &tstiop__my_struct_a__s, LSTR("a"), LSTR("test:gen4"), IOP_T_I16,
            NULL, &value));
        Z_ASSERT_EQ(value.i, 4);
        Z_ASSERT_NEG(iop_field_by_name_get_gen_attr(
            &tstiop__my_struct_a__s, LSTR("a"), LSTR("test:gen1"), IOP_T_I32,
            NULL, &value));

        /* iface */
        Z_ASSERT_N(iop_iface_get_gen_attr(&tstiop__my_iface_a__if,
                                          LSTR("test:gen5"), IOP_T_U8, NULL,
                                          &value));
        Z_ASSERT_EQ(value.i, 5);
        Z_ASSERT_NEG(iop_iface_get_gen_attr(&tstiop__my_iface_a__if,
                                            LSTR("test:gen1"), IOP_T_U16,
                                            NULL, &value));

        /* rpc */
        Z_ASSERT_N(iop_rpc_get_gen_attr(
            &tstiop__my_iface_a__if, tstiop__my_iface_a__fun_a__rpc,
            LSTR("test:gen6"), IOP_T_U32, NULL, &value));
        Z_ASSERT_EQ(value.i, 6);
        Z_ASSERT_NEG(iop_rpc_get_gen_attr(
            &tstiop__my_iface_a__if, tstiop__my_iface_a__fun_a__rpc,
            LSTR("test:gen1"), IOP_T_U64, NULL, &value));

        /* json object */
        Z_ASSERT_N(iop_struct_get_gen_attr(&tstiop__my_struct_a__s,
                                           LSTR("test:json"), IOP_T_STRING,
                                           NULL, &value));
        Z_ASSERT_STREQUAL(value.s.s,
            "{\"field\":{\"f1\":\"val1\",\"f2\":-1.00000000000000000e+02}}");
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_new, "test iop_new and sisters") { /* {{{ */
        t_scope;
        tstiop__my_struct_g__t  g;
        tstiop__my_struct_g__t *gp;

        iop_init(tstiop__my_struct_g, &g);

        gp = mp_iop_new_desc(NULL, &tstiop__my_struct_g__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_struct_g, &g, gp);
        p_delete(&gp);

        gp = t_iop_new_desc(&tstiop__my_struct_g__s);
        Z_ASSERT_IOPEQUAL(tstiop__my_struct_g, &g, gp);

        gp = mp_iop_new(NULL, tstiop__my_struct_g);
        Z_ASSERT_IOPEQUAL(tstiop__my_struct_g, &g, gp);
        p_delete(&gp);

        gp = iop_new(tstiop__my_struct_g);
        Z_ASSERT_IOPEQUAL(tstiop__my_struct_g, &g, gp);
        p_delete(&gp);

        gp = t_iop_new(tstiop__my_struct_g);
        Z_ASSERT_IOPEQUAL(tstiop__my_struct_g, &g, gp);
    } Z_TEST_END
    /* }}} */
    Z_TEST(class_printf, "test %*pS in format string for IOP class") { /* {{{ */
        t_scope;
        SB_1k(ref);
        SB_1k(tst_sb);
        tstiop__my_class3__t obj;
        char buf[10];
        char *path;
        lstr_t file;
        FILE *out;

        iop_init(tstiop__my_class3, &obj);
        obj.int1 = 12345;
        obj.int2 = 67890;
        obj.int2 = -2;
        obj.bool1 = true;

        iop_sb_jpack(&ref, &tstiop__my_class3__s, &obj,
                     IOP_JPACK_NO_WHITESPACES | IOP_JPACK_NO_TRAILING_EOL);

        sb_addf(&tst_sb, "%*pS", IOP_OBJ_FMT_ARG(&obj));
        Z_ASSERT_EQ(tst_sb.len, ref.len);
        Z_ASSERT_STREQUAL(tst_sb.data, ref.data);

        Z_ASSERT_EQ(snprintf(buf, countof(buf), "%*pS", IOP_OBJ_FMT_ARG(&obj)),
                    ref.len);
        Z_ASSERT_LSTREQUAL(LSTR_INIT_V(buf, countof(buf) - 1),
                           LSTR_INIT_V(ref.data, countof(buf) - 1));

        path = t_fmt("%*pM/tst", LSTR_FMT_ARG(z_tmpdir_g));
        out = fopen(path, "w");
        Z_ASSERT_EQ(fprintf(out, "%*pS", IOP_OBJ_FMT_ARG(&obj)), ref.len);
        fclose(out);

        Z_ASSERT_N(lstr_init_from_file(&file, path, PROT_READ, MAP_SHARED),
                   "%m");
        Z_ASSERT_LSTREQUAL(file, LSTR_SB_V(&ref));
        lstr_wipe(&file);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(struct_printf, "test %*pS in format string for IOP struct") { /* {{{ */
        t_scope;
        SB_1k(ref);
        SB_1k(tst_sb);
        tstiop__my_struct_a__t st;
        tstiop__my_class2__t cls2;
        char buf[10];
        char *path;
        lstr_t file;
        FILE *out;
        int compact_flags;

        compact_flags = IOP_JPACK_NO_WHITESPACES | IOP_JPACK_NO_TRAILING_EOL;
        iop_init(tstiop__my_class2, &cls2);

        iop_init(tstiop__my_struct_a, &st);
        st.a = 12345;
        st.b = 67890;
        st.p = -2;
        st.n = true;
        st.j = LSTR("toto");
        st.l = IOP_UNION(tstiop__my_union_a, ua, 1);
        st.lr = &st.l;
        st.cls2 = &cls2;

        iop_sb_jpack(&ref, &tstiop__my_struct_a__s, &st, compact_flags);
        sb_setf(&tst_sb, "%*pS", IOP_ST_FMT_ARG(tstiop__my_struct_a, &st));
        Z_ASSERT_EQ(tst_sb.len, ref.len);
        sb_setf(&tst_sb, "%*pS",
                IOP_ST_DESC_FMT_ARG_FLAGS(&tstiop__my_struct_a__s, &st,
                                          compact_flags));
        Z_ASSERT_EQ(tst_sb.len, ref.len);
        Z_ASSERT_STREQUAL(tst_sb.data, ref.data);

        Z_ASSERT_EQ(snprintf(buf, countof(buf), "%*pS",
                             IOP_ST_FMT_ARG(tstiop__my_struct_a, &st)),
                    ref.len);
        Z_ASSERT_LSTREQUAL(LSTR_INIT_V(buf, countof(buf) - 1),
                           LSTR_INIT_V(ref.data, countof(buf) - 1));

        path = t_fmt("%*pM/tst", LSTR_FMT_ARG(z_tmpdir_g));
        out = fopen(path, "w");
        Z_ASSERT_EQ(fprintf(out, "%*pS", IOP_ST_FMT_ARG(tstiop__my_struct_a,
                                                        &st)), ref.len);
        fclose(out);

        Z_ASSERT_N(lstr_init_from_file(&file, path, PROT_READ, MAP_SHARED),
                   "%m");
        Z_ASSERT_LSTREQUAL(file, LSTR_SB_V(&ref));
        lstr_wipe(&file);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(enum_printf, "test %*pE in format string") { /* {{{ */
        struct {
            int v;
            int flags;
            lstr_t res;
        } t[] = {
#define T(_v, _base, _full)                                                  \
            { _v, 0,                 LSTR(_base) },                          \
            { _v, IOP_ENUM_FMT_FULL, LSTR(_full) }

            T(MY_ENUM_D_FOO,     "FOO",     "FOO(0)"),
            T(1,                 "1",       "<unknown>(1)"),
            T(MY_ENUM_D_BAR,     "BAR",     "BAR(2)"),
            T(3,                 "3",       "<unknown>(3)"),
            T(MY_ENUM_D_FOO_BAR, "FOO_BAR", "FOO_BAR(4)"),
            T(5,                 "5",       "<unknown>(5)"),
#undef T
        };

        carray_for_each_ptr(test, t) {
            t_scope;
            lstr_t file;
            FILE *out;
            SB_1k(tst_sb);
            char *path;

            sb_addf(&tst_sb, "%*pE", IOP_ENUM_FMT_ARG_FLAGS(tstiop__my_enum_d,
                                                            t->v, t->flags));
            Z_ASSERT_LSTREQUAL(LSTR_SB_V(&tst_sb), t->res);

            path = t_fmt("%*pM/tst%d", LSTR_FMT_ARG(z_tmpdir_g), t->v);
            out = fopen(path, "w");
            Z_ASSERT_EQ(fprintf(out, "%*pE",
                                IOP_ENUM_FMT_ARG_FLAGS(tstiop__my_enum_d,
                                                       t->v, t->flags)),
                        t->res.len);
            fclose(out);

            Z_ASSERT_N(lstr_init_from_file(&file, path, PROT_READ,
                                           MAP_SHARED), "%m");
            Z_ASSERT_LSTREQUAL(file, t->res);
            lstr_wipe(&file);
        }
    } Z_TEST_END;
    /* }}} */
    Z_TEST(union_printf, "test %*pU in format string for IOP union types") { /* {{{ */
        t_scope;
        tstiop__my_union_c__t uc;

        uc = IOP_UNION(tstiop__my_union_c, i_of_c, 42);
        Z_ASSERT_STREQUAL(t_fmt("%*pU",
                                IOP_UNION_FMT_ARG(tstiop__my_union_c, &uc)),
                          "iOfC");
        uc = IOP_UNION(tstiop__my_union_c, d_of_c, 0.1);
        Z_ASSERT_STREQUAL(t_fmt("%*pU",
                                IOP_UNION_FMT_ARG(tstiop__my_union_c, &uc)),
                          "dOfC");

        p_clear(&uc, 1);
        Z_ASSERT_STREQUAL(t_fmt("%*pU",
                                IOP_UNION_FMT_ARG(tstiop__my_union_c, &uc)),
                          "<unknown>(0)");
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_set_opt_field, "test iop_set_opt_field function") { /* {{{ */
        tstiop__my_struct_a_opt__t obj;
        const iop_field_t *f;

        iop_init(tstiop__my_struct_a_opt, &obj);

        /* Field a (int) */
        Z_ASSERT_N(iop_field_find_by_name(&tstiop__my_struct_a_opt__s,
                                          LSTR("a"), NULL, &f));
        obj.a.v = 10;
        Z_ASSERT(!OPT_ISSET(obj.a));
        iop_set_opt_field(&obj, f);
        Z_ASSERT(OPT_ISSET(obj.a));
        Z_ASSERT_EQ(obj.a.v, 10);

        /* Field b (uint) */
        Z_ASSERT_N(iop_field_find_by_name(&tstiop__my_struct_a_opt__s,
                                          LSTR("b"), NULL, &f));
        obj.b.v = 11;
        Z_ASSERT(!OPT_ISSET(obj.b));
        iop_set_opt_field(&obj, f);
        Z_ASSERT(OPT_ISSET(obj.b));
        Z_ASSERT_EQ(obj.b.v, 11u);

        /* Field n (bool) */
        Z_ASSERT_N(iop_field_find_by_name(&tstiop__my_struct_a_opt__s,
                                          LSTR("n"), NULL, &f));
        obj.n.v = true;
        Z_ASSERT(!OPT_ISSET(obj.n));
        iop_set_opt_field(&obj, f);
        Z_ASSERT(OPT_ISSET(obj.n));
        Z_ASSERT_EQ(obj.n.v, true);

        /* Field j (string) */
        Z_ASSERT_N(iop_field_find_by_name(&tstiop__my_struct_a_opt__s,
                                          LSTR("j"), NULL, &f));
        Z_ASSERT(!obj.j.s);
        iop_set_opt_field(&obj, f);
        Z_ASSERT_LSTREQUAL(obj.j, LSTR_EMPTY_V);
        obj.j = LSTR("toto");
        iop_set_opt_field(&obj, f);
        Z_ASSERT_LSTREQUAL(obj.j, LSTR("toto"));
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_array_dup, "test the IOP_ARRAY_DUP macro") { /* {{{ */
        t_scope;
        int32_t a[] = { 1, 2, 3 };
        iop_array_i32_t m = IOP_ARRAY(a, 3);
        iop_array_i32_t n;

        n = T_IOP_ARRAY_DUP(m);
        Z_ASSERT_EQ(m.len, n.len);
        /* both arrays have the same elements */
        carray_for_each_pos(i, a) {
            Z_ASSERT_EQ(m.tab[i], a[i]);
            Z_ASSERT_EQ(m.tab[i], n.tab[i]);
        }

        /* modify a */
        n = IOP_ARRAY_DUP(NULL, m);
        carray_for_each_ptr(p, a) {
            (*p)++;
        }

        /* m has the new values, n has the old ones */
        carray_for_each_pos(i, a) {
            Z_ASSERT_EQ(m.tab[i], a[i]);
            Z_ASSERT_EQ(n.tab[i], a[i] - 1);
        }

        p_delete(&n.tab);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_array_new, "test the IOP_ARRAY_NEW* macros") { /* {{{ */
        t_scope;
        tstiop__my_struct_a__array_t array;

#define TEST(macro, wipe)                                                    \
    do {                                                                     \
        p_clear(&array, 1);                                                  \
        array = macro(tstiop__my_struct_a, 3);                               \
        Z_ASSERT_P(array.tab);                                               \
        Z_ASSERT_EQ(array.len, 3);                                           \
                                                                             \
        wipe(&array.tab);                                                    \
    } while (0)

        TEST(T_IOP_ARRAY_NEW, IGNORE);
        TEST(T_IOP_ARRAY_NEW_RAW, IGNORE);
        TEST(IOP_ARRAY_NEW, p_delete);
        TEST(IOP_ARRAY_NEW_RAW, p_delete);

#undef TEST
    } Z_TEST_END;
    /* }}} */
    Z_TEST(mp_iop_array, "test the *_IOP_ARRAY macros") { /* {{{ */
        t_scope;
        tstiop__basic_struct__t st1 = { .i = 1 };
        tstiop__basic_struct__t st2 = { .i = 2 };
        tstiop__basic_struct__array_t st_array;

        tstiop__basic_class__t cl1;
        tstiop__basic_class__t cl2;
        tstiop__basic_class__array_t cl_array;

        iop_array_u32_t u32_array;

        st_array = T_IOP_ARRAY(tstiop__basic_struct, st1, st2);
        Z_ASSERT_EQ(st_array.len, 2);
        Z_ASSERT_IOPEQUAL(tstiop__basic_struct, &st1, &st_array.tab[0]);
        Z_ASSERT_IOPEQUAL(tstiop__basic_struct, &st2, &st_array.tab[1]);

        st_array = T_IOP_ARRAY(tstiop__basic_struct, st2, st1);
        Z_ASSERT_EQ(st_array.len, 2);
        Z_ASSERT_IOPEQUAL(tstiop__basic_struct, &st1, &st_array.tab[1]);
        Z_ASSERT_IOPEQUAL(tstiop__basic_struct, &st2, &st_array.tab[0]);

        iop_init(tstiop__basic_class, &cl1);
        cl1.i = 3;
        iop_init(tstiop__basic_class, &cl2);
        cl2.i = 4;
        cl_array = T_IOP_ARRAY(tstiop__basic_class, &cl1, &cl2);
        Z_ASSERT_EQ(cl_array.len, 2);
        Z_ASSERT_IOPEQUAL(tstiop__basic_class, &cl1, cl_array.tab[0]);
        Z_ASSERT_IOPEQUAL(tstiop__basic_class, &cl2, cl_array.tab[1]);

        u32_array = T_IOP_ARRAY(u32, 10, 11, 12, 13, 14);
        Z_ASSERT_EQ(u32_array.len, 5);
        tab_enumerate(pos, u, &u32_array) {
            Z_ASSERT_EQ(u, 10u + pos);
        }
    } Z_TEST_END;
    /* }}} */
    Z_TEST(dup_and_copy, "test duplication/copy functions") { /* {{{ */
        t_scope;
        SB_1k(err);
        const char *path;
        tstiop__full_struct__t fs;
        const iop_struct_t *st = tstiop__full_struct__sp;

        path = t_fmt("%*pM/samples/z-full-struct.json",
                     LSTR_FMT_ARG(z_cmddir_g));
        Z_ASSERT_N(t_iop_junpack_file(path, st, &fs, 0, NULL, &err),
                   "%pL", &err);
        Z_HELPER_RUN(z_test_dup_and_copy(st, &fs),
                     "test failed for sample %s (type `%pL')", path,
                     &st->fullname);
        Z_HELPER_RUN(z_test_dup_and_copy(fs.required.o->__vptr,
                                         fs.required.o),
                     "test failed for class");
        Z_HELPER_RUN(z_test_macros_dup_copy(&fs));
    } Z_TEST_END;
    /* }}} */
    Z_TEST(nr_58558, "avoid leak when copying an IOP with no value") { /* {{{ */
        tstiop__my_struct_c__t st;
        tstiop__my_struct_c__t *p;

        iop_init(tstiop__my_struct_c, &st);
        p = iop_dup(tstiop__my_struct_c, &st);
        iop_copy(tstiop__my_struct_c, &p, NULL);
        Z_ASSERT_NULL(p);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_field_is_pointed, "test the iop_field_is_pointed function") { /* {{{ */
        struct {
            const iop_struct_t *st;
            lstr_t field_name;
            bool is_pointed;
        } t[] = {
#define TEST(pfx, field, res)                                                \
            { &pfx##__s, LSTR(#field), res }

            TEST(tstiop__my_struct_a, a, false),
            TEST(tstiop__my_struct_a, k, false),
            TEST(tstiop__my_struct_a, l, false),
            TEST(tstiop__my_struct_a, lr, true),
            TEST(tstiop__my_struct_a, cls2, true),

            TEST(tstiop__my_struct_a_opt, a, false),
            TEST(tstiop__my_struct_a_opt, j, false),
            TEST(tstiop__my_struct_a_opt, l, true),
            TEST(tstiop__my_struct_a_opt, lr, true),
            TEST(tstiop__my_struct_a_opt, o, true),
            TEST(tstiop__my_struct_a_opt, cls2, true),

            TEST(tstiop__my_struct_f, a, false),
            TEST(tstiop__my_struct_f, b, false),
            TEST(tstiop__my_struct_f, c, false),
            TEST(tstiop__my_struct_f, d, false),
            TEST(tstiop__my_struct_f, e, true),

#undef TEST
        };

        carray_for_each_ptr(test, t) {
            const iop_field_t *field;

            Z_ASSERT_N(iop_field_find_by_name(test->st, test->field_name,
                                              NULL, &field));
            Z_ASSERT_EQ(test->is_pointed, iop_field_is_pointed(field));
        }
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_struct_check_backward_compat, "test iop_struct_check_backward_compat") { /* {{{ */
        t_scope;
        const char *err;
        tstiop_backward_compat__basic_union__t  basic_union;
        tstiop_backward_compat__basic_struct__t basic_struct;
        tstiop_backward_compat__basic_class__t  basic_class;
        tstiop_backward_compat__struct_container1__t struct_container1;
        tstiop_backward_compat__parent_class_a__t *parent_class;
        tstiop_backward_compat__empty_struct__t empty_struct;

        basic_union = IOP_UNION(tstiop_backward_compat__basic_union, a, 12);

        iop_init(tstiop_backward_compat__basic_struct, &basic_struct);
        basic_struct.a = 12;
        basic_struct.b = LSTR("string");

        iop_init(tstiop_backward_compat__struct_container1,
                 &struct_container1);
        struct_container1.s = basic_struct;

        iop_init(tstiop_backward_compat__basic_class, &basic_class);
        basic_class.a = 12;
        basic_class.b = LSTR("string");

        iop_init(tstiop_backward_compat__empty_struct, &empty_struct);

#define T_OK(_type1, _obj1, _type2, _flags)  \
        do {                                                                 \
            const iop_struct_t *st1 = &tstiop_backward_compat__##_type1##__s;\
            const iop_struct_t *st2 = &tstiop_backward_compat__##_type2##__s;\
            tstiop_backward_compat__##_type1##__t *__obj1 = (_obj1);         \
                                                                             \
            Z_HELPER_RUN(iop_check_struct_backward_compat(st1, st2, _flags,  \
                                                          NULL, __obj1));    \
        } while (0)

#define T_OK_ALL(_type1, _obj1, _type2)  \
        do {                                                                 \
            T_OK(_type1, _obj1, _type2, IOP_COMPAT_BIN);                     \
            T_OK(_type1, _obj1, _type2, IOP_COMPAT_JSON);                    \
            T_OK(_type1, NULL,  _type2, IOP_COMPAT_ALL);                     \
        } while (0)

#define T_KO(_type1, _obj1, _type2, _flags, _err)  \
        do {                                                                 \
            const iop_struct_t *st1 = &tstiop_backward_compat__##_type1##__s;\
            const iop_struct_t *st2 = &tstiop_backward_compat__##_type2##__s;\
            tstiop_backward_compat__##_type1##__t *__obj1 = (_obj1);         \
                                                                             \
            Z_HELPER_RUN(iop_check_struct_backward_compat(st1, st2, _flags,  \
                                                          _err, __obj1));    \
        } while (0)

#define T_KO_ALL(_type1, _obj1, _type2, _err)  \
        do {                                                                 \
            T_KO(_type1, _obj1, _type2, IOP_COMPAT_BIN,  _err);              \
            T_KO(_type1, _obj1, _type2, IOP_COMPAT_JSON, _err);              \
            T_KO(_type1,  NULL, _type2, IOP_COMPAT_ALL,  _err);              \
        } while (0)

#define INDENT_LVL1  "\n  | "
#define INDENT_LVL2  "\n  |   | "
#define INDENT_LVL3  "\n  |   |   | "
#define INDENT_LVL4  "\n  |   |   |   | "

        /* Struct to root when no fields are set is OK */
        T_OK_ALL(empty_struct, &empty_struct, empty_class);

        /* Basic struct to class transitions. */
        T_KO_ALL(basic_struct, &basic_struct, basic_union,
                 "was a struct and is now a union");
        T_KO_ALL(basic_union, &basic_union, basic_struct,
                 "was a union and is now a struct");

        /* struct to abstract class is KO */
        T_KO_ALL(basic_struct, &basic_struct, basic_abstract_class,
                 "was a struct and is now an abstract class");

        /* Struct to root class is OK */
        T_OK_ALL(basic_struct, &basic_struct, basic_class);

        /* Struct to child class is OK for JSON only */
        T_KO(basic_struct, &basic_struct, basic_class_child, IOP_COMPAT_BIN,
             "was a struct and is now a child class");

        /* TODO: add checks for the JSON case
         *
         * T_OK(basic_struct, &basic_struct, basic_class_child,
         *      IOP_COMPAT_JSON);
         */

        /* Struct to root class with missing fields is KO */
        T_KO(basic_struct, &basic_struct, basic_class_parent, IOP_COMPAT_BIN,
             "field `a` -> `b`:\n  | incompatible types");
        T_KO(basic_struct, &basic_struct, basic_class_parent, IOP_COMPAT_JSON,
             "field `a` does not exist anymore");

        T_KO(basic_class, &basic_class, basic_abstract_class, IOP_COMPAT_BIN,
             "is an abstract class but was not abstract");
        T_KO(basic_class, &basic_class, basic_abstract_class, IOP_COMPAT_JSON,
             "is an abstract class but was not abstract\n"
             "class fullname changed (`tstiop_backward_compat.BasicClass`"
             " != `tstiop_backward_compat.BasicAbstractClass`)");
        T_OK(basic_abstract_class, NULL, basic_class, IOP_COMPAT_BIN);
        T_KO(basic_abstract_class, NULL, basic_class,
             IOP_COMPAT_JSON, "class fullname changed "
             "(`tstiop_backward_compat.BasicAbstractClass` != "
             "`tstiop_backward_compat.BasicClass`)");

        T_OK_ALL(basic_union,          &basic_union,  basic_union);
        T_OK_ALL(basic_struct,         &basic_struct, basic_struct);
        T_OK_ALL(basic_class,          &basic_class,  basic_class);
        T_OK_ALL(basic_abstract_class, NULL,          basic_abstract_class);

        /* A field disappears. */
        T_OK(basic_struct, &basic_struct, disappeared_field, IOP_COMPAT_BIN);
        T_KO(basic_struct, &basic_struct, disappeared_field, IOP_COMPAT_JSON,
             "field `b` does not exist anymore");

        /* A required field was added. */
        T_KO_ALL(basic_struct, &basic_struct, new_required_field,
                 "new field `c` must not be required");

        /* Optional/repeated/default/required void value fields added. */
        T_OK_ALL(basic_struct, &basic_struct, new_opt_field);
        T_OK_ALL(basic_struct, &basic_struct, new_repeated_field);
        T_OK_ALL(basic_struct, &basic_struct, new_defval_field);
        T_OK_ALL(basic_struct, &basic_struct, new_required_void_field);
        T_OK_ALL(struct_container1, &struct_container1, struct_container3);

        /* Renamed field. */
        T_OK(basic_struct, &basic_struct, renamed_field, IOP_COMPAT_BIN);
        T_KO(basic_struct, &basic_struct, renamed_field, IOP_COMPAT_JSON,
             "new field `b2` must not be required\n"
             "field `b` does not exist anymore");

        /* Field tag changed. */
        T_OK(basic_struct, &basic_struct, tag_changed_field, IOP_COMPAT_JSON);
        T_KO(basic_struct, &basic_struct, tag_changed_field, IOP_COMPAT_BIN,
             "new field `b` must not be required");

        T_KO(basic_struct, &basic_struct, renamed_and_tag_changed_field,
             IOP_COMPAT_ALL,
             "field `b` (1): name and tag lookups mismatch: "
             "`b` (2) != `a` (1)\n"
             "field `a` (2): name and tag lookups mismatch: "
             "`a` (1) != `b` (2)"
             );

        /* Field changed of type in a binary-compatible way. */
        T_OK(basic_struct, &basic_struct, field_compatible_type_bin,
             IOP_COMPAT_BIN);
        T_KO(basic_struct, &basic_struct, field_compatible_type_bin,
             IOP_COMPAT_JSON, "field `b`:" INDENT_LVL1 "incompatible types");

        /* A field was added in a union. */
        T_OK_ALL(basic_union, &basic_union, union1);
        T_KO(basic_union, &basic_union, union2, IOP_COMPAT_BIN,
             "field with tag 1 (`a`) does not exist anymore");
        T_KO(basic_union, &basic_union, union2, IOP_COMPAT_JSON,
             "field `a` does not exist anymore");

        /* Number types changes. */
        {
            tstiop_backward_compat__number_struct__t  number_struct;
            tstiop_backward_compat__number_struct2__t number_struct2;

            iop_init(tstiop_backward_compat__number_struct, &number_struct);
            number_struct.b   = true;
            number_struct.i8  = INT8_MAX;
            number_struct.u8  = UINT8_MAX;
            number_struct.i16 = INT16_MAX;
            number_struct.u16 = UINT16_MAX;
            number_struct.i32 = INT32_MAX;
            number_struct.u32 = UINT32_MAX;
            T_OK_ALL(number_struct, &number_struct, number_struct2);

            iop_init(tstiop_backward_compat__number_struct2, &number_struct2);
            number_struct2.b   = INT8_MAX;
            number_struct2.i8  = INT16_MAX;
            number_struct2.u8  = INT16_MAX;
            number_struct2.i16 = INT32_MAX;
            number_struct2.u16 = INT32_MAX;
            number_struct2.i32 = INT64_MAX;
            number_struct2.u32 = INT64_MAX;
            T_KO_ALL(number_struct2, &number_struct2, number_struct,
                     "field `b`:"   INDENT_LVL1 "incompatible types\n"
                     "field `i8`:"  INDENT_LVL1 "incompatible types\n"
                     "field `u8`:"  INDENT_LVL1 "incompatible types\n"
                     "field `i16`:" INDENT_LVL1 "incompatible types\n"
                     "field `u16`:" INDENT_LVL1 "incompatible types\n"
                     "field `i32`:" INDENT_LVL1 "incompatible types\n"
                     "field `u32`:" INDENT_LVL1 "incompatible types");
        }

        /* Class id change. */
        T_KO(basic_class, &basic_class, class_id_changed, IOP_COMPAT_BIN,
             "class id changed (0 != 1)");
        /* XXX: This is authorized in json, but the test would fail because
         *      the fullname changes :-(. */

        /* Field repeated <-> not repeated. */
        {
            tstiop_backward_compat__field_repeated__t field_repeated;
            bool a_arr[7] = { true, true, true, true, true, true, true };

            iop_init(tstiop_backward_compat__field_repeated, &field_repeated);
            field_repeated.a.tab = a_arr;
            field_repeated.a.len = countof(a_arr);

            /* Not repeated -> repeated. */
            T_OK(basic_struct, &basic_struct, field_repeated, IOP_COMPAT_BIN);
            T_OK(basic_struct, &basic_struct, field_repeated, IOP_COMPAT_JSON);

            /* Repeated -> not repeated. */
            T_KO_ALL(field_repeated, &field_repeated, basic_struct,
                     "field `a`:"
                     INDENT_LVL1 "was repeated and is not anymore");

            /* Repeated -> not repeated void. */
            T_OK_ALL(field_repeated, &field_repeated, field_void);
        }

        /* Fields repeated, different types */
        {
#define T_REP_INIT(_type) \
            do {                                                             \
                iop_init(tstiop_backward_compat__##_type##_repeated,         \
                         &(_type##_rep));                                    \
                _type##_rep.el.tab = _type##_arr;                            \
                _type##_rep.el.len = countof(_type##_arr);                   \
            } while(0)

#define T_REP_BIN_KO(_type, _type2) \
            do {                                                             \
                T_OK(_type##_repeated, &(_type##_rep), _type2##_repeated,    \
                     IOP_COMPAT_JSON);                                       \
                T_KO(_type##_repeated, &(_type##_rep), _type2##_repeated,    \
                     IOP_COMPAT_BIN,                                         \
                     "field `el`:" INDENT_LVL1 "incompatible types");        \
            } while(0)

#define T_REP_OK_ALL(_type, _type2) \
            do {                                                             \
                T_OK_ALL(_type##_repeated, &(_type##_rep),                   \
                         _type2##_repeated);                                 \
            } while(0)

            tstiop_backward_compat__bool_repeated__t bool_rep;
            tstiop_backward_compat__byte_repeated__t byte_rep;
            tstiop_backward_compat__ubyte_repeated__t ubyte_rep;
            tstiop_backward_compat__short_repeated__t short_rep;
            tstiop_backward_compat__ushort_repeated__t ushort_rep;
            tstiop_backward_compat__int_repeated__t int_rep;
            tstiop_backward_compat__uint_repeated__t uint_rep;

            bool bool_arr[7] = {true, true, true, true, true, true, true};
            int8_t byte_arr[7]     = {1, 2, 3, 4, 5, 6, 7};
            uint8_t ubyte_arr[7]   = {1, 2, 3, 4, 5, 6, 7};
            int16_t short_arr[7]   = {1, 2, 3, 4, 5, 6, 7};
            uint16_t ushort_arr[7] = {1, 2, 3, 4, 5, 6, 7};
            int32_t int_arr[7]     = {1, 2, 3, 4, 5, 6, 7};
            uint32_t uint_arr[7]   = {1, 2, 3, 4, 5, 6, 7};

            T_REP_INIT(bool);
            T_REP_OK_ALL(bool, byte);
            T_REP_OK_ALL(bool, ubyte);
            T_REP_BIN_KO(bool, short);
            T_REP_BIN_KO(bool, ushort);
            T_REP_BIN_KO(bool, int);
            T_REP_BIN_KO(bool, uint);
            T_REP_BIN_KO(bool, long);
            T_REP_BIN_KO(bool, ulong);

            T_REP_INIT(byte);
            T_REP_BIN_KO(byte, short);
            T_REP_BIN_KO(byte, ushort);
            T_REP_BIN_KO(byte, int);
            T_REP_BIN_KO(byte, uint);
            T_REP_BIN_KO(byte, long);
            T_REP_BIN_KO(byte, ulong);

            T_REP_INIT(ubyte);
            T_REP_BIN_KO(ubyte, short);
            T_REP_BIN_KO(ubyte, ushort);
            T_REP_BIN_KO(ubyte, int);
            T_REP_BIN_KO(ubyte, uint);
            T_REP_BIN_KO(ubyte, long);
            T_REP_BIN_KO(ubyte, ulong);

            T_REP_INIT(short);
            T_REP_BIN_KO(short, int);
            T_REP_BIN_KO(short, uint);
            T_REP_BIN_KO(short, long);
            T_REP_BIN_KO(short, ulong);

            T_REP_INIT(ushort);
            T_REP_BIN_KO(ushort, int);
            T_REP_BIN_KO(ushort, uint);
            T_REP_BIN_KO(ushort, long);
            T_REP_BIN_KO(ushort, ulong);

            T_REP_INIT(int);
            T_REP_OK_ALL(int, long);
            T_REP_OK_ALL(int, ulong);

            T_REP_INIT(uint);
            T_REP_OK_ALL(uint, long);
            T_REP_OK_ALL(uint, ulong);

#undef T_REP_INIT
#undef T_REP_OK_ALL
#undef T_REP_BIN_KO
        }

        /* Field required <-> optional. */
        {
            tstiop_backward_compat__field_optional__t field_optional;

            iop_init(tstiop_backward_compat__field_optional, &field_optional);
            field_optional.b = LSTR("plop");

            /* Required -> optional. */
            T_OK_ALL(basic_struct, &basic_struct, field_optional);

            /* Optional -> required. */
            T_KO_ALL(field_optional, &field_optional, basic_struct,
                     "field `a`:"
                     INDENT_LVL1 "is required and was not before");

            /* Optional -> required void. */
            T_OK_ALL(field_optional, &field_optional, field_void);

            /* Optional -> required, optional structure */
            {
                tstiop_backward_compat__opt_field_opt_struct__t opt_field;

                iop_init(tstiop_backward_compat__opt_field_opt_struct,
                         &opt_field);

                T_OK_ALL(opt_field_opt_struct, &opt_field,
                         mandatory_field_opt_struct);
            }
        }

        /* Field of type struct changed for an incompatible struct. */
        T_KO_ALL(struct_container1, &struct_container1, struct_container2,
                 "field `s`:"
                 INDENT_LVL1 "new field `c` must not be required");

        /* Infinite recursion in structure inclusion. */
        {
            tstiop_backward_compat__infinite_recur1__t recur1_1;
            tstiop_backward_compat__infinite_recur1__t recur1_2;

            iop_init(tstiop_backward_compat__infinite_recur1, &recur1_1);
            recur1_1.s = &recur1_2;

            iop_init(tstiop_backward_compat__infinite_recur1, &recur1_2);

            T_OK_ALL(infinite_recur1, &recur1_1, infinite_recur2);
        }

        /* Enums. */
        {
            tstiop_backward_compat__struct_enum1__t enum_1;
            tstiop_backward_compat__struct_enum2__t enum_2;
            tstiop_backward_compat__struct_strict_enum1__t strict_enum_1;
            tstiop_backward_compat__struct_inverted_enum1__t inverted_enum_1;

            iop_init(tstiop_backward_compat__struct_enum1, &enum_1);
            enum_1.en = 12;

            iop_init(tstiop_backward_compat__struct_enum2, &enum_2);
            enum_2.en = ENUM2_VAL1;

            iop_init(tstiop_backward_compat__struct_strict_enum1,
                     &strict_enum_1);
            strict_enum_1.en = STRICT_ENUM1_VAL1;

            iop_init(tstiop_backward_compat__struct_inverted_enum1,
                     &inverted_enum_1);
            inverted_enum_1.en = INVERTED_ENUM1_VAL1;

            /* Test enums are compatible with themselves. */
            T_OK_ALL(struct_enum1, &enum_1, struct_enum1);
            T_OK_ALL(struct_enum2, &enum_2, struct_enum2);
            T_OK_ALL(struct_strict_enum1, &strict_enum_1,
                     struct_strict_enum1);

            /* Not strict -> strict is always forbidden. */
            T_KO_ALL(struct_enum1, &enum_1, struct_strict_enum1,
                     "field `en`:"
                     INDENT_LVL1 "enum is strict and was not before");

            /* A value disappears from an enum, this is always forbidden.
             * Note that this actually "works" in binary if the new enum is
             * not strict, but forbid this dangerous usage. */
            T_KO(struct_enum1, NULL, struct_enum2, IOP_COMPAT_BIN,
                 "field `en`:"
                 INDENT_LVL1 "numeric value 2 does not exist anymore");
            enum_1.en = 2;
            T_KO(struct_enum1, &enum_1, struct_enum2, IOP_COMPAT_JSON,
                 "field `en`:"
                 INDENT_LVL1 "value `VAL2` does not exist anymore");

            /* Inverting two enumeration values should be allowed in binary
             * and in json, but not when both binary and json compatibility
             * modes are required. */
            T_OK(struct_enum1, &enum_1, struct_inverted_enum1,
                 IOP_COMPAT_BIN);
            T_OK(struct_enum1, &enum_1, struct_inverted_enum1,
                 IOP_COMPAT_JSON);
            T_KO(struct_enum1, NULL, struct_inverted_enum1,
                 IOP_COMPAT_JSON | IOP_COMPAT_BIN,
                 "field `en`:"
                 INDENT_LVL1 "value `VAL1` (1): name and value lookups "
                 "mismatch: `VAL1` (2) != `VAL2` (1)"
                 INDENT_LVL1 "value `VAL2` (2): name and value lookups "
                 "mismatch: `VAL2` (1) != `VAL1` (2)");

            /* Field conversion from enum to int. */
            T_OK(struct_enum1, &enum_1, struct_enum3, IOP_COMPAT_BIN);
            T_KO(struct_enum1, &enum_1, struct_enum3, IOP_COMPAT_JSON,
                 "field `en`:" INDENT_LVL1 "incompatible types");
        }

        /* Classes (these tests can only be done in binary and not in json
         * because class names change). */
        {
            tstiop_backward_compat__parent_class1__t parent_class1;
            tstiop_backward_compat__child_class1__t child_class1;

            iop_init(tstiop_backward_compat__parent_class1, &parent_class1);
            parent_class1.a = 10;

            iop_init(tstiop_backward_compat__child_class1, &child_class1);
            child_class1.a = 10;
            child_class1.b = 20;

            T_KO(child_class1, &child_class1, child_class2, IOP_COMPAT_BIN,
                 "cannot find class with id 1 in the parents of "
                 "`tstiop_backward_compat.ChildClass2`");

            T_KO(child_class1, &child_class1, child_class32, IOP_COMPAT_BIN,
                 "class `tstiop_backward_compat.ChildClass31` was added in "
                 "the parents with a required field `c`");

            T_OK(child_class1, &child_class1, child_class42, IOP_COMPAT_BIN);

            T_KO(child_class1, &child_class1, child_class52, IOP_COMPAT_BIN,
                 "parent `tstiop_backward_compat.ParentClass5`:"
                 INDENT_LVL1 "field `a`:"
                 INDENT_LVL2 "incompatible types");

            T_KO(parent_class1, &parent_class1, child_class6, IOP_COMPAT_BIN,
                 "class `tstiop_backward_compat.ParentClass6` was added in "
                 "the parents with a required field `b`");

            T_OK(parent_class1, &parent_class1, child_class7, IOP_COMPAT_BIN);
        }

        /* Ignore backward incompatibilities */
        {
            /* Json backward incompatibilities ignored */
            T_OK(basic_struct, NULL,
                 new_required_field_json_ignored,
                 IOP_COMPAT_JSON);

            T_KO(basic_struct, &basic_struct,
                 new_required_field_json_ignored,
                 IOP_COMPAT_BIN, "new field `c` must not be required");

            T_KO(basic_struct, &basic_struct,
                 new_required_field_json_ignored,
                 IOP_COMPAT_ALL, "new field `c` must not be required");

            /* Bin backward incompatibilities ignored */
            T_OK(basic_struct, NULL,
                 new_required_field_bin_ignored,
                 IOP_COMPAT_BIN);

            T_KO(basic_struct, &basic_struct,
                 new_required_field_bin_ignored,
                 IOP_COMPAT_JSON, "new field `c` must not be required");

            T_KO(basic_struct, &basic_struct,
                 new_required_field_bin_ignored,
                 IOP_COMPAT_ALL, "new field `c` must not be required");

            /* Json/Bin backward incompatibilities ignored */
            T_OK_ALL(basic_struct, NULL, new_required_field_ignored);

            /* Nested ignored struct: must throw errors unless the root
             * struct is flagged as ignored. */
            T_OK(struct_container1, NULL,
                 root_struct_json_ignored, IOP_COMPAT_JSON);

            T_OK(struct_container1, NULL,
                 root_struct_bin_ignored, IOP_COMPAT_BIN);

            T_OK_ALL(struct_container1, NULL, root_struct_ignored);

            T_KO_ALL(struct_container1, &struct_container1,
                     root_struct,
                     "field `s`:"
                     INDENT_LVL1 "new field `c` must not be required");
        }

        /* Last optional field disappears. */
        parent_class =
            iop_obj_vcast(tstiop_backward_compat__parent_class_a,
                          t_iop_new(tstiop_backward_compat__child_class_a));
        T_OK(parent_class_a, parent_class, parent_class_b, IOP_COMPAT_BIN);

        /* Adding a non-optional field whose type is an "optional" struct
         * is backward compatible. */
        T_OK_ALL(basic_struct, &basic_struct,
                 new_mandatory_field_optional);

        /* Adding a non-optional field whose type is a "non-optional" struct
         * is not backward compatible. */
        err = "new field `c` must not be required";
        T_KO_ALL(basic_struct, &basic_struct,
                 new_mandatory_field_non_optional, err);
        T_KO_ALL(basic_struct, &basic_struct,
                 new_mandatory_field_non_optional2, err);
        T_KO_ALL(basic_struct, &basic_struct,
                 new_mandatory_field_non_optional3, err);

        /* A required struct but with all optional fields is optional. If
         * added in a new parent class, it is backward compatible for the
         * child. */
        T_OK(child_opt_a, NULL, child_opt_b, IOP_COMPAT_BIN);

#undef T_OK
#undef T_OK_ALL
#undef T_KO
#undef T_KO_ALL

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_pkg_check_backward_compat, "test iop_pkg_check_backward_compat") { /* {{{ */
        SB_1k(err);

#define T_OK(_pkg1, _pkg2, _flags)  \
        do {                                                                 \
            Z_ASSERT_N(iop_pkg_check_backward_compat(&_pkg1##__pkg,          \
                                                     &_pkg2##__pkg,          \
                                                     _flags, &err));         \
        } while (0)

#define T_OK_ALL(_pkg1, _pkg2)  \
        do {                                                                 \
            T_OK(_pkg1, _pkg2, IOP_COMPAT_BIN);                              \
            T_OK(_pkg1, _pkg2, IOP_COMPAT_JSON);                             \
            T_OK(_pkg1, _pkg2, IOP_COMPAT_ALL);                              \
        } while (0)

#define T_KO(_pkg1, _pkg2, _flags, _err)  \
        do {                                                                 \
            sb_reset(&err);                                                  \
            Z_ASSERT_NEG(iop_pkg_check_backward_compat(&_pkg1##__pkg,        \
                                                       &_pkg2##__pkg,        \
                                                       _flags, &err));       \
            Z_ASSERT_LSTREQUAL(LSTR_SB_V(&err), LSTR(_err));                 \
        } while (0)

#define T_KO_ALL(_pkg1, _pkg2, _err)  \
        do {                                                                 \
            T_KO(_pkg1, _pkg2, IOP_COMPAT_BIN,  _err);                       \
            T_KO(_pkg1, _pkg2, IOP_COMPAT_JSON, _err);                       \
            T_KO(_pkg1, _pkg2, IOP_COMPAT_ALL,  _err);                       \
        } while (0)

        /* Test packages with themselves. */
        T_OK_ALL(tstiop, tstiop);
        T_OK_ALL(tstiop_inheritance, tstiop_inheritance);
        T_OK_ALL(tstiop_backward_compat, tstiop_backward_compat);
        T_OK_ALL(tstiop_backward_compat_iface, tstiop_backward_compat_iface);
        T_OK_ALL(tstiop_backward_compat_mod, tstiop_backward_compat_mod);

        /* Deleted structure. */
        T_KO_ALL(tstiop_backward_compat_deleted_struct_1,
                 tstiop_backward_compat_deleted_struct_2,
                 "pkg `tstiop_backward_compat_deleted_struct_2`:"
                 INDENT_LVL1
                 "struct `tstiop_backward_compat_deleted_struct_1.Struct2` "
                 "does not exist anymore");

        /* Incompatible structures. */
        T_KO(tstiop_backward_compat_incompatible_struct_1,
             tstiop_backward_compat_incompatible_struct_2, IOP_COMPAT_BIN,
             "pkg `tstiop_backward_compat_incompatible_struct_2`:"
             INDENT_LVL1
             "struct `tstiop_backward_compat_incompatible_struct_1.Struct1`:"
             INDENT_LVL2
             "new field `b` must not be required");
        T_KO(tstiop_backward_compat_incompatible_struct_1,
             tstiop_backward_compat_incompatible_struct_2, IOP_COMPAT_JSON,
             "pkg `tstiop_backward_compat_incompatible_struct_2`:"
             INDENT_LVL1
             "struct `tstiop_backward_compat_incompatible_struct_1.Struct1`:"
             INDENT_LVL2
             "new field `b` must not be required"
             INDENT_LVL1
             "struct `tstiop_backward_compat_incompatible_struct_1.Struct2`:"
             INDENT_LVL2
             "new field `d` must not be required"
             INDENT_LVL2
             "field `c` does not exist anymore");

        /* Deleted interface. */
        T_KO_ALL(tstiop_backward_compat_iface,
                 tstiop_backward_compat_iface_deleted,
                 "pkg `tstiop_backward_compat_iface_deleted`:"
                 INDENT_LVL1
                 "interface `tstiop_backward_compat_iface.Iface` does not "
                 "exist anymore");

       /* Deleted RPC. */
 #define PREFIX  "pkg `tstiop_backward_compat_iface_deleted_rpc`:"           \
                 INDENT_LVL1                                                 \
                 "interface `tstiop_backward_compat_iface.Iface`:"           \
                 INDENT_LVL2
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_deleted_rpc, IOP_COMPAT_BIN,
             PREFIX "RPC with tag 2 (`rpc2`) does not exist anymore");
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_deleted_rpc, IOP_COMPAT_JSON,
             PREFIX "RPC `rpc2` does not exist anymore");
#undef PREFIX

        /* test @(compat:ignore) on Interface */
        T_OK_ALL(tstiop_backward_compat_iface,
                 tstiop_backward_compat_iface_deleted_rpc_ignored);
        /* test @(compat:ignoreJson) on Interface */
        T_OK(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_deleted_rpc_ignored_json,
             IOP_COMPAT_JSON);
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_deleted_rpc_ignored_json,
             IOP_COMPAT_BIN,
             "pkg `tstiop_backward_compat_iface_deleted_rpc_ignored_json`:"
             INDENT_LVL1 "interface `tstiop_backward_compat_iface.Iface`:"
             INDENT_LVL2 "RPC with tag 2 (`rpc2`) does not exist anymore");
        /* test @(compat:ignoreBin) on Interface */
        T_OK(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_deleted_rpc_ignored_bin,
             IOP_COMPAT_BIN);
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_deleted_rpc_ignored_bin,
             IOP_COMPAT_JSON,
             "pkg `tstiop_backward_compat_iface_deleted_rpc_ignored_bin`:"
             INDENT_LVL1 "interface `tstiop_backward_compat_iface.Iface`:"
             INDENT_LVL2 "RPC `rpc2` does not exist anymore");

        /* Incompatible RPC changes. */
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_incompatible_rpc, IOP_COMPAT_JSON,
             "pkg `tstiop_backward_compat_iface_incompatible_rpc`:"
             INDENT_LVL1 "interface `tstiop_backward_compat_iface.Iface`:"
             INDENT_LVL2 "RPC `rpc1` args:"
             INDENT_LVL3 "new field `c` must not be required"
             INDENT_LVL3 "field `b` does not exist anymore"
             INDENT_LVL2 "RPC `rpc1` result:"
             INDENT_LVL3 "field `res`:"
             INDENT_LVL4 "incompatible types"
             INDENT_LVL2 "RPC `rpc1` exn:"
             INDENT_LVL3 "field `desc` does not exist anymore"
             INDENT_LVL2 "RPC `rpc2` was async and is not anymore");
        /* test @(compat:ignore) on RPC */
        T_OK_ALL(tstiop_backward_compat_iface,
                 tstiop_backward_compat_iface_incompatible_rpc_ignored);
        /* test @(compat:ignoreJson) on RPC */
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_incompatible_rpc_ignored_binjson,
             IOP_COMPAT_JSON,
             "pkg `tstiop_backward_compat_iface_"
             "incompatible_rpc_ignored_binjson`:"
             INDENT_LVL1 "interface `tstiop_backward_compat_iface.Iface`:"
             INDENT_LVL2 "RPC `rpc1` args:"
             INDENT_LVL3 "new field `c` must not be required"
             INDENT_LVL3 "field `b` does not exist anymore"
             INDENT_LVL2 "RPC `rpc1` result:"
             INDENT_LVL3 "field `res`:"
             INDENT_LVL4 "incompatible types"
             INDENT_LVL2 "RPC `rpc1` exn:"
             INDENT_LVL3 "field `desc` does not exist anymore");
        /* test @(compat:ignoreBin) on RPC */
        T_KO(tstiop_backward_compat_iface,
             tstiop_backward_compat_iface_incompatible_rpc_ignored_binjson,
             IOP_COMPAT_BIN,
             "pkg `tstiop_backward_compat_iface_"
             "incompatible_rpc_ignored_binjson`:"
             INDENT_LVL1 "interface `tstiop_backward_compat_iface.Iface`:"
             INDENT_LVL2 "RPC `rpc2` was async and is not anymore");

        /* Deleted module. */
        T_KO_ALL(tstiop_backward_compat_mod,
                 tstiop_backward_compat_mod_deleted,
                 "pkg `tstiop_backward_compat_mod_deleted`:"
                 INDENT_LVL1
                 "module `tstiop_backward_compat_mod.Module` does not exist "
                 "anymore");

        /* Deleted interface in a module. */
 #define PREFIX  "pkg `tstiop_backward_compat_mod_deleted_if`:"              \
                 INDENT_LVL1                                                 \
                 "module `tstiop_backward_compat_mod.Module`:"               \
                 INDENT_LVL2
        T_KO(tstiop_backward_compat_mod,
             tstiop_backward_compat_mod_deleted_if, IOP_COMPAT_JSON,
             PREFIX "interface `iface2` does not exist anymore");
        T_KO(tstiop_backward_compat_mod,
             tstiop_backward_compat_mod_deleted_if, IOP_COMPAT_BIN,
             PREFIX "interface with tag 2 (`iface2`) does not exist anymore");
#undef PREFIX

#undef T_OK
#undef T_OK_ALL
#undef T_KO
#undef T_KO_ALL

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_struct_is_optional, "test iop_struct_is_optional") { /* {{{ */

        Z_ASSERT(iop_struct_is_optional(
                    &tstiop_backward_compat__abstract_class1__s, false));
        Z_ASSERT(iop_struct_is_optional(
                    &tstiop_backward_compat__abstract_class1__s, true));
        Z_ASSERT(!iop_struct_is_optional(
                    &tstiop_backward_compat__child_class41__s, true));
        Z_ASSERT(iop_struct_is_optional(
                    &tstiop_backward_compat__child_class41__s, false));
        Z_ASSERT(!iop_struct_is_optional(
                    &tstiop_backward_compat__child_class42__s, false));

    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_dso_fixup, "test fixup for external DSOs") { /* {{{ */
        iop_dso_t *dso;
        const iop_struct_t *my_struct;
        const iop_field_t *field;

        dso = _Z_DSO_OPEN("iop/zchk-tstiop2-plugin" SO_FILEEXT, true);

        my_struct = iop_dso_find_type(dso, LSTR("tstiop2.MyStruct"));
        Z_ASSERT_N(iop_field_find_by_name(my_struct, LSTR("a"), NULL,
                                          &field));

        /* the two pointers to "tstiop.MyStructA" must be the same */
        Z_ASSERT_LSTREQUAL(tstiop__my_struct_a__s.fullname,
                           field->u1.st_desc->fullname);
        Z_ASSERT(&tstiop__my_struct_a__s == field->u1.st_desc);

        iop_dso_close(&dso);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_dso_fixup_bad_dep, "test bug in fixup") { /* {{{ */
        /* test that loading the same dso twice will not induce dependencies
         * between the two dsos */
        t_scope;
        iop_dso_t *dso1;
        iop_dso_t *dso2;
        const char *newpath;
        const char *sofile = "zchk-tstiop2-plugin" SO_FILEEXT;
        const char *sopath = t_fmt("%s/iop/%s", z_cmddir_g.s, sofile);

        /* build one dso, remove file */
        newpath = t_fmt("%*pM/1_%s", LSTR_FMT_ARG(z_tmpdir_g), sofile);
        Z_ASSERT_N(filecopy(sopath, newpath), "%s -> %s: %m", sopath, newpath);
        dso1 = _Z_DSO_OPEN(newpath, false);
        Z_ASSERT_N(unlink(newpath));

        /* build the second one, remove file */
        newpath = t_fmt("%*pM/2_%s", LSTR_FMT_ARG(z_tmpdir_g), sofile);
        Z_ASSERT_N(filecopy(sopath, newpath));
        dso2 = _Z_DSO_OPEN(newpath, false);
        Z_ASSERT_N(unlink(newpath));

        /* the two files must be independent. If they are not, closing the
         * first one will cause the second one to be reloaded, which will fail
         * as the file no longer exists */
        iop_dso_close(&dso1);
        iop_dso_close(&dso2);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_first_diff_desc, "test iop_first_diff_desc()") { /* {{{ */
        SB_1k(diff_desc);
        z_first_diff_st__t d1;
        z_first_diff_st__t d2;
        z_first_diff_c1__t c1;
        z_first_diff_c2__t c2;
        int tab1[] = { 1, 2, 3 };
        int tab2[] = { 1 };
        int tab3[] = { 1, 3, 3 };

        iop_init(z_first_diff_st, &d1);
        d1.i = 42;
        d1.s = LSTR("toto");

        d2 = d1;

        Z_ASSERT_NEG(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                         &diff_desc), "diff_desc: %*pM",
                     SB_FMT_ARG(&diff_desc));
        d2.i = 41;
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data,
                          "field `i`: value differs (`42` vs `41`)");

        d2 = d1;
        d2.b = true;
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data,
                          "field `b`: value differs (`false` vs `true`)");

        d2 = d1;
        OPT_SET(d1.opt_i, 666);
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data,
                          "field `optI`: field presence differs "
                          "(field absent on second value)");
        d2 = d1;
        d1.tab = (iop_array_i32_t)IOP_ARRAY(tab1, countof(tab1));
        d2 = d1;
        Z_ASSERT_NEG(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                         &diff_desc), "diff_desc: %*pM",
                     SB_FMT_ARG(&diff_desc));

        d2.tab = (iop_array_i32_t)IOP_ARRAY(tab2, countof(tab2));
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data,
                          "field `tab[0]`: array length differs (3 vs 1)");

        d2.tab = (iop_array_i32_t)IOP_ARRAY(tab3, countof(tab3));
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data,
                          "field `tab[1]`: value differs (`2` vs `3`)");

        iop_init(z_first_diff_c1, &c1);
        iop_init(z_first_diff_c2, &c2);
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_c0__s, &c1, &c2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data, "class type differs "
                          "(tstiop.FirstDiffC1 vs tstiop.FirstDiffC2)");

        d2 = d1;
        d1.o = iop_obj_vcast(z_first_diff_c0, &c1);
        d2.o = iop_obj_vcast(z_first_diff_c0, &c2);
        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data, "field `o`: class type differs "
                          "(tstiop.FirstDiffC1 vs tstiop.FirstDiffC2)");

        d2 = d1;
        OPT_SET(d1.e, FIRST_DIFF_ENUM_A);
        OPT_SET(d2.e, FIRST_DIFF_ENUM_C);

        Z_ASSERT_N(iop_first_diff_desc(&z_first_diff_st__s, &d1, &d2,
                                       &diff_desc));
        Z_ASSERT_STREQUAL(diff_desc.data, "field `e`: "
                          "value differs (`A(0)` vs `C(2)`)");
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_nonreg_ioptag_union_unpack, "test iop_tag all bytes set (i32 vs u16)") { /* {{{ */
        tstiop__my_union_b__t dst;
        tstiop__my_union_b__t src;
        int32_t *i;
        int ret;
        lstr_t data;
        pstream_t json1 = ps_initstr("{ bval: 1234 }");
        pstream_t json2 = ps_initstr("{ a.ua: 1234 }");
        SB_1k(sb);
        t_scope;

        iop_init(tstiop__my_union_b, &src);
        i = IOP_UNION_SET(tstiop__my_union_b, &src, bval);
        *i = 1234;
        data = t_iop_bpack_struct_flags(&tstiop__my_union_b__s, &src, 0);

        /* bunpack to struct (set to 0xFF) */
        memset(&dst, 0xFF, sizeof(tstiop__my_union_b__t));
        ret = iop_bunpack(t_pool(), &tstiop__my_union_b__s,
                          &dst, ps_initlstr(&data), false);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT_EQ(src.iop_tag, dst.iop_tag);

        /* unpack json union with format ":" */
        memset(&dst, 0xFF, sizeof(tstiop__my_union_b__t));
        ret = t_iop_junpack_ps(&json1, &tstiop__my_union_b__s, &dst, 0, NULL);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT_EQ(dst.iop_tag, (IOP_UNION_TAG_T(tstiop__my_union_b))
                    IOP_UNION_TAG(tstiop__my_union_b, bval));

        /* unpack json union with format "." */
        memset(&dst, 0xFF, sizeof(tstiop__my_union_b__t));
        ret = t_iop_junpack_ps(&json2, &tstiop__my_union_b__s, &dst, 0, NULL);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT_EQ(dst.iop_tag, (IOP_UNION_TAG_T(tstiop__my_union_b))
                    IOP_UNION_TAG(tstiop__my_union_b, a));
        Z_ASSERT_EQ(dst.a.iop_tag, (IOP_UNION_TAG_T(tstiop__my_union_a))
                    IOP_UNION_TAG(tstiop__my_union_a, ua));

        /* pack/unpack xml */
        sb_adds(&sb, "<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
                "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">");
        iop_xpack(&sb, &tstiop__my_union_b__s, &src, false, false);
        sb_adds(&sb, "</root>");
        memset(&dst, 0xFF, sizeof(tstiop__my_union_b__t));
        Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));
        ret = iop_xunpack(xmlr_g, t_pool(), &tstiop__my_union_b__s, &dst);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT_EQ(src.iop_tag, dst.iop_tag);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_void_union, "test iop void in union") { /* {{{ */
        t_scope;
        tstiop_void_type__void_alone__t s;
        lstr_t data;
        int ret;
        tstiop_void_type__void_alone__t dest;
        SB(buff, 100);

        iop_init(tstiop_void_type__void_alone, &s);

        /* pack with field "other" selected */
        s = IOP_UNION(tstiop_void_type__void_alone, other, 0x55);
        data = t_iop_bpack_struct(&tstiop_void_type__void_alone__s, &s);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V("\x82\x55"), data);

        /* check iop_copy for other field */
        iop_std_test_struct(&tstiop_void_type__void_alone__s, &s,
                                  "Union void (unselected)");

        /* pack with void field */
        IOP_UNION_SET_V(tstiop_void_type__void_alone, &s, field);
        data = t_iop_bpack_struct(&tstiop_void_type__void_alone__s, &s);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V("\x01\x00"), data);

        /* check unpacking void field */
        ret = iop_bunpack(t_pool(), &tstiop_void_type__void_alone__s,
                          &dest, ps_initlstr(&data), false);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT(IOP_UNION_IS(tstiop_void_type__void_alone, &s, field));

        /* check iop_copy for void field */
        iop_std_test_struct(&tstiop_void_type__void_alone__s, &s,
                                  "Union void (selected)");

        /* test JSON */
        iop_json_test_json(&tstiop_void_type__void_alone__s,
                           "{ \"field\": null }\n", &s, "");

        s = IOP_UNION(tstiop_void_type__void_alone, other, 0x55);
        iop_json_test_json(&tstiop_void_type__void_alone__s,
                           "{ \"other\": 85 }\n", &s, "");

        /* test XML */
        IOP_UNION_SET_V(tstiop_void_type__void_alone, &s, field);
        iop_xpack(&buff, &tstiop_void_type__void_alone__s, &s, false,
                  false);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V("<field xsi:nil=\"true\"></field>"),
                           LSTR_SB_V(&buff));

        sb_reset(&buff);
        s = IOP_UNION(tstiop_void_type__void_alone, other, 0x55);
        iop_xpack(&buff, &tstiop_void_type__void_alone__s, &s, false,
                  false);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V("<other>85</other>"),
                           LSTR_SB_V(&buff));

        iop_xml_test_struct(&tstiop_void_type__void_alone__s, &s, "va");

        /* test WSDL */
        sb_reset(&buff);
        iop_xwsdl(&buff, tstiop_void_type__void_alone_mod__modp, NULL,
                  "http://example.com/tstiop",
                  "http://localhost:1080/iop/", false, true);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_void_optional, "test iop void, optional") {/* {{{ */
        t_scope;
        tstiop_void_type__void_optional__t s;
        tstiop_void_type__void_optional__t dest;
        lstr_t data;
        int ret;
        byte buf1[20], buf2[20];
        SB(buff, 100);

        iop_init(tstiop_void_type__void_optional, &s);

        /* pack with optional void enabled */
        s.field = true;
        data = t_iop_bpack_struct(&tstiop_void_type__void_optional__s, &s);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V("\x01\x00"), data);

        /* unpack enabled optional void */
        ret = iop_bunpack(t_pool(), &tstiop_void_type__void_optional__s,
                          &dest, ps_initlstr(&data), false);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT_EQ(dest.field, true);

        /* check iop_copy */
        iop_std_test_struct(&tstiop_void_type__void_optional__s, &s,
                                  "Optional void (enabled)");

        /* pack with optional void disabled */
        s.field = false;
        data = t_iop_bpack_struct(&tstiop_void_type__void_optional__s, &s);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V(""), data);

        /* unpack disabled optional void */
        ret = iop_bunpack(t_pool(), &tstiop_void_type__void_optional__s,
                          &dest, ps_initlstr(&data), false);
        Z_ASSERT_EQ(ret, 0);
        Z_ASSERT_EQ(dest.field, false);

        /* check iop_copy */
        iop_std_test_struct(&tstiop_void_type__void_optional__s, &s,
                                  "Optional void (disabled)");

        /* check hash different for set/unset optional void */
        s.field = false;
        iop_hash_sha1(&tstiop_void_type__void_optional__s, &s, buf1, 0);
        s.field = true;
        iop_hash_sha1(&tstiop_void_type__void_optional__s, &s, buf2, 0);
        Z_ASSERT(memcmp(buf1, buf2, sizeof(buf1)),
                 "Hashes should be different");

        /* test JSON */
        s.field = true;
        iop_json_test_json(&tstiop_void_type__void_optional__s,
                           "{ \"field\": null }\n", &s, "");
        s.field = false;
        iop_json_test_json(&tstiop_void_type__void_optional__s,
                           "{ }\n", &s, "");

        /* test XML */
        s.field = true;
        iop_xpack(&buff, &tstiop_void_type__void_optional__s, &s, false,
                  false);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V("<field xsi:nil=\"true\"></field>"),
                           LSTR_SB_V(&buff));
        iop_xml_test_struct(&tstiop_void_type__void_optional__s, &s, "va");

        sb_reset(&buff);
        s.field = false;
        iop_xpack(&buff, &tstiop_void_type__void_optional__s, &s, false,
                  false);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V(""), LSTR_SB_V(&buff));
        iop_xml_test_struct(&tstiop_void_type__void_optional__s, &s, "va");

        /* test WSDL */
        sb_reset(&buff);
        iop_xwsdl(&buff, tstiop_void_type__void_optional_mod__modp, NULL,
                  "http://example.com/tstiop",
                  "http://localhost:1080/iop/", false, true);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_void_required, "test iop void, required") { /* {{{ */
        t_scope;
        int8_t data1[5] = {0, 1, 2, 3, 4};
        int32_t data2[5] = {0, 1, 2, 3, 4};
        tstiop_void_type__void_required__t s;
        tstiop_void_type__int_to_void__t s_int;
        tstiop_void_type__array_to_void__t s_array;
        tstiop_void_type__struct_to_void__t s_struct;
        tstiop_void_type__small_array_to_void__t s_small_array;
        tstiop_void_type__double_to_void__t s_double;
        lstr_t packed;
        SB(buff, 10);

        /* pack required void (skipped) */
        iop_init(tstiop_void_type__void_required, &s);
        packed = t_iop_bpack_struct(&tstiop_void_type__void_required__s, &s);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V(""), packed);

        /* check iop_copy */
        iop_std_test_struct(&tstiop_void_type__void_required__s, &s,
                                  "Required void");

#define T_UNPACK_TO_VOID(type) \
        do {                                                                 \
            lstr_t data;                                                     \
            data = t_iop_bpack_struct(                                       \
                &tstiop_void_type__##type##_to_void__s, &s_##type);          \
            Z_ASSERT_EQ(iop_bunpack(t_pool(),                                \
                                    &tstiop_void_type__void_required__s,     \
                                    &s, ps_initlstr(&data), false), 0);      \
        } while(0)

        /* unpack integer wire type into void */
        iop_init(tstiop_void_type__int_to_void, &s_int);
        s_int.field = 0x42;
        T_UNPACK_TO_VOID(int);

        /* unpack repeated wire type into void */
        iop_init(tstiop_void_type__array_to_void, &s_array);
        s_array.field.tab = data2;
        s_array.field.len = 5;
        T_UNPACK_TO_VOID(array);

        /* unpack blk wire type (struct) to void */
        iop_init(tstiop_void_type__struct_to_void, &s_struct);
        s_struct.field.field = 0x55;
        T_UNPACK_TO_VOID(struct);

        /* unpack blk wire type (byte array) to void */
        iop_init(tstiop_void_type__small_array_to_void, &s_small_array);
        s_small_array.field.tab = data1;
        s_small_array.field.len = 5;
        T_UNPACK_TO_VOID(small_array);

        /* unpack quad wire type (double) to void */
        iop_init(tstiop_void_type__double_to_void, &s_double);
        s_double.field = 1.0;
        T_UNPACK_TO_VOID(double);
#undef T_UNPACK_TO_VOID

        /* test JSON */
        iop_json_test_unpack(&tstiop_void_type__void_required__s,
                             "{ field: 1 }", 0, true, "int to void");
        iop_json_test_unpack(&tstiop_void_type__void_required__s,
                             "{ field: [0, 1, 2] }", 0, true,
                             "array to void");
        iop_json_test_unpack(&tstiop_void_type__void_required__s,
                             "{ field: { a: 1, b: 2 } }", 0, true,
                             "struct to void");
        iop_json_test_pack(&tstiop_void_type__void_required__s, &s,
                           0, true, true, "{\n}\n");

        /* test XML pack required void */
        iop_xpack(&buff, &tstiop_void_type__void_required__s, &s, false,
                  false);
        Z_ASSERT_LSTREQUAL(LSTR_IMMED_V(""), LSTR_SB_V(&buff));

        /* test XML unpack to void */
#define T_XUNPACK_TO_VOID(type) \
        do {                                                                 \
            SB(sb, 10);                                                      \
            int ret;                                                         \
            void *res = NULL;                                                \
            sb_adds(&sb,                                                     \
                    "<root xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "  \
                    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""\
                    ">");                                                    \
            iop_xpack(&sb, &tstiop_void_type__##type##_to_void__s, &s_##type,\
                      false, false);                                         \
            sb_adds(&sb, "</root>");                                         \
            Z_ASSERT_N(xmlr_setup(&xmlr_g, sb.data, sb.len));                \
            ret = iop_xunpack_ptr(xmlr_g, t_pool(),                          \
                                  &tstiop_void_type__void_required__s, &res);\
            Z_ASSERT_EQ(ret, 0);                                             \
        } while(0)

        T_XUNPACK_TO_VOID(int);
        T_XUNPACK_TO_VOID(struct);
        T_XUNPACK_TO_VOID(double);
        T_XUNPACK_TO_VOID(array);
#undef T_XUNPACK_TO_VOID

        /* test WSDL */
        sb_reset(&buff);
        iop_xwsdl(&buff, tstiop_void_type__void_required_mod__modp, NULL,
                  "http://example.com/tstiop",
                  "http://localhost:1080/iop/", false, true);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(json_empty_string, "parsing '' as JSON always returns an error") { /* {{{ */
        tstiop__my_union_a__t union_a;
        tstiop__my_class1__t class1;
        tstiop__my_struct_a_opt__t struct_opt;
        pstream_t json = ps_initstr("");
        const char *error = "1:1: there is nothing to read";
        SB_1k(err);

        /* test for an union */
        Z_ASSERT_NEG(t_iop_junpack_ps(&json, &tstiop__my_union_a__s, &union_a,
                                      0, &err));
        Z_ASSERT_STREQUAL(err.data, error);
        sb_reset(&err);

        /* test for a class */
        Z_ASSERT_NEG(t_iop_junpack_ps(&json, &tstiop__my_class1__s, &class1,
                                      0, &err));
        Z_ASSERT_STREQUAL(err.data, error);
        sb_reset(&err);

        /* test for a struct with all optional fields */
        Z_ASSERT_NEG(t_iop_junpack_ps(&json, &tstiop__my_struct_a_opt__s,
                                      &struct_opt, 0, &err));
        Z_ASSERT_STREQUAL(err.data, error);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(repeated_field_removal, "repeated field removal") { /* {{{ */
        t_scope;
        lstr_t data;
        pstream_t data_ps;
        struct_with_repeated_field__t st;
        struct_without_repeated_field__t *out = NULL;
        lstr_t tab[] = { LSTR_IMMED("toto"), LSTR_IMMED("foo") };

        Z_TEST_FLAGS("redmine_54728");

        iop_init(struct_with_repeated_field, &st);
        st.a = 42;
        st.b.tab = tab;
        st.b.len = countof(tab);
        st.c = 999;

        data = t_iop_bpack_struct(&struct_with_repeated_field__s, &st);
        Z_ASSERT_P(data.s);
        data_ps = ps_initlstr(&data);
        Z_ASSERT_N(iop_bunpack_ptr(t_pool(),
                                   &struct_without_repeated_field__s,
                                   (void **)&out, data_ps, false),
                   "unexpected backward incompatibility for repeated field "
                   "removal: %s", iop_get_err());
        Z_ASSERT_EQ(st.a, out->a);
        Z_ASSERT_EQ(st.c, out->c);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_value_get_bpack_size, "iop_value_get_bpack_size") { /* {{{ */
        tstiop__get_bpack_sz_u__t u;
        tstiop__get_bpack_sz_st__t st;

        iop_init(tstiop__get_bpack_sz_st, &st);
        st.a = 123456;
        st.b = LSTR("test");

#define T(field, _v)                                                         \
        u = IOP_UNION(tstiop__get_bpack_sz_u, field, _v);                    \
        Z_HELPER_RUN(z_check_iop_value_get_bpack_size(&u, #field),           \
                     #field "=" #_v);

        T(i8, 45);
        T(u8, 240);
        T(i16, -42);
        T(u16, UINT16_MAX);
        T(i32, 4000);
        T(u32, UINT32_MAX);
        T(i64, INT64_MIN);
        T(i64, INT64_MAX);
        T(i64, 0);
        T(u64, UINT64_MAX);
        T(b, true);
        T(b, false);
        T(s, LSTR("I am Joe's complete lack of surprise."));
        T(en, GET_BPACK_SZ_EN_B);
        T(st, st);

#undef T
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_env, "environment object getters") { /* {{{ */
        lstr_t name;
        const iop_obj_t *obj;
        const iop_struct_t *cls;
        const iop_enum_t *en;

        name = tstiop__my_struct_a__s.fullname;
        Z_ASSERT_P((obj = iop_get_obj(name)), "cannot find obj `%pL'", &name);
        Z_ASSERT(obj->type == IOP_OBJ_TYPE_ST);
        Z_ASSERT(obj->desc.st == &tstiop__my_struct_a__s,
                 "wrong iop_struct_t (got `%pL')", &obj->desc.st->fullname);

        Z_ASSERT_NULL(iop_get_enum(name), "`%pL' is not an enum", &name);
        Z_ASSERT_NULL(iop_get_class_by_fullname(&tstiop__my_class1__s, name),
                      "`%pL' is not a class", &name);

        name = tstiop__my_enum_c__e.fullname;
        Z_ASSERT_P((obj = iop_get_obj(name)), "cannot find obj `%pL'", &name);
        Z_ASSERT(obj->type == IOP_OBJ_TYPE_ENUM);
        Z_ASSERT(obj->desc.en == &tstiop__my_enum_c__e,
                 "wrong iop_enum_t (got `%pL')", &obj->desc.en->fullname);

        en = iop_get_enum(name);
        Z_ASSERT_P(en, "cannot find enum `%pL'", &name);
        Z_ASSERT(en == &tstiop__my_enum_c__e, "wrong enum (got `%pL')",
                 &en->fullname);

        name = tstiop__my_class3__s.fullname;
        Z_ASSERT_P((obj = iop_get_obj(name)), "cannot find obj `%pL'", &name);
        Z_ASSERT(obj->type == IOP_OBJ_TYPE_ST);
        Z_ASSERT(obj->desc.st == &tstiop__my_class3__s,
                 "wrong iop_struct_t (got `%pL')", &obj->desc.st->fullname);

        cls = iop_get_class_by_fullname(&tstiop__my_class1__s, name);
        Z_ASSERT_P(cls, "cannot find class `%pL'", &name);
        Z_ASSERT(cls == &tstiop__my_class3__s,
                 "wrong IOP class (got `%pL')", &obj->desc.st->fullname);

        cls = iop_get_class_by_id(&tstiop__my_class1__s,
                                  tstiop__my_class3__s.class_attrs->class_id);
        Z_ASSERT_P(cls, "cannot find class `%pL' from ID", &name);
        Z_ASSERT(cls == &tstiop__my_class3__s, "wrong IOP class (got `%pL')",
                 &cls->fullname);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(struct_packing, "check struct packing behavior") { /* {{{ */
        /* Check that a struct is properly packed. */
        STATIC_ASSERT(sizeof(tstiop__struct_with_optional_object__t) ==
                      2 * sizeof(int32_t) + sizeof(void *));

        /* Check consistency of struct packing between similar structs. */
        STATIC_ASSERT(sizeof(tstiop__struct_with_mandatory_object__t) ==
                      sizeof(tstiop__struct_with_optional_object__t));
        STATIC_ASSERT(sizeof(tstiop__struct_with_mandatory_object__t) ==
                      sizeof(tstiop__struct_with_typedef__t));

        Z_ASSERT(true);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(int_types_helpers, "integer types sign/size helpers") { /* {{{ */
        struct {
            iop_type_t type;
            bool is_signed;
            size_t size;
        } int_types[] = {
            { IOP_T_I8, true, 1 },
            { IOP_T_U8, false, 1 },
            { IOP_T_I16, true, 2 },
            { IOP_T_U16, false, 2 },
            { IOP_T_I32, true, 4 },
            { IOP_T_U32, false, 4 },
            { IOP_T_I64, true, 8 },
            { IOP_T_U64, false, 8 },
        };

        carray_for_each_ptr(type, int_types) {
            Z_ASSERT_EQ(iop_int_type_is_signed(type->type), type->is_signed,
                        "wrong sign for type %s",
                        iop_type_get_string_desc(type->type));
            Z_ASSERT_EQ(iop_int_type_size(type->type), type->size,
                        "wrong size for type %s",
                        iop_type_get_string_desc(type->type));
        }
    } Z_TEST_END;
    /* }}} */
    Z_TEST(wsdl, "test generation of WSDL") { /* {{{ */
        t_scope;
        SB_1k(buf);
        lstr_t expected;

        Z_ASSERT_N(lstr_init_from_file(&expected,
                                       t_fmt("%*pM/test-data/iop.wsdl",
                                             LSTR_FMT_ARG(z_cmddir_g)),
                                       PROT_READ, MAP_SHARED));

        iop_xwsdl(&buf, &tstiop_wsdl__m__mod, NULL,
                  "http://example.com/tstiop",
                  "http://localhost:1080/iop/", false, true);

        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&buf), expected);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_core_obj, "IOP core obj") { /* {{{ */
        Z_HELPER_RUN(test_iop_core_obj());
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_init_union, "test IOP union init") { /* {{{ */
        tstiop__my_union_d__t u;

        iop_init_union(tstiop__my_union_d, &u, ua);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_d, &u, ua));
        Z_ASSERT_EQ(u.ua, 0);

        iop_init_union(tstiop__my_union_d, &u, ub);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_d, &u, ub));
        Z_ASSERT_EQ(u.ub, 0);

        iop_init_union(tstiop__my_union_d, &u, ug);
        Z_ASSERT_P(IOP_UNION_GET(tstiop__my_union_d, &u, ug));
        Z_ASSERT_EQ(u.ug.a, -1);
    } Z_TEST_END
    /* }}} */
    Z_TEST(iop_st_array_for_each, "test iop_st_array_for_each") { /* {{{ */
        t_scope;
        tstiop__my_class3__array_t obj_array;
        tstiop__my_class3__t **obj_ptr;
        tstiop__my_union_d__array_t u_array;
        tstiop__my_union_d__t *u_ptr;

        obj_array = T_IOP_ARRAY(tstiop__my_class3,
                                (tstiop__my_class3__t *)0x1,
                                (tstiop__my_class3__t *)0x2,
                                (tstiop__my_class3__t *)0x3);
        obj_ptr = obj_array.tab;
        iop_tab_for_each(&tstiop__my_class3__s, ptr, &obj_array) {
            Z_ASSERT(ptr == *obj_ptr++);
        }
        Z_ASSERT(obj_ptr == tab_last(&obj_array) + 1);

        u_array = T_IOP_ARRAY_NEW(tstiop__my_union_d, 2);
        u_ptr = u_array.tab;
        iop_tab_for_each_const(&tstiop__my_union_d__s, ptr, &u_array) {
            Z_ASSERT(ptr == u_ptr++);
        }
        Z_ASSERT(u_ptr == tab_last(&u_array) + 1);
    } Z_TEST_END
    /* }}} */
    Z_TEST(bpack_error_unregistered_class, "unpacking an instance of an unregistered class") { /* {{{ */
        t_scope;
        lstr_t bin;
        void *instance = NULL;

        bin = t_iop_bpack_struct(&tstiop_not_registered_class__s,
                                 t_iop_new(tstiop_not_registered_class));
        Z_ASSERT_NEG(iop_bunpack_ptr(t_pool(), &tstiop_registered_class__s,
                                     &instance, ps_initlstr(&bin), false));
        Z_ASSERT_STREQUAL(iop_get_err(),
                          "cannot find child 2 of class "
                          "'tstiop.RegisteredClass'");
    } Z_TEST_END;
    /* }}} */
    Z_TEST(bpack_error_unexpected_class_type, "unpacking an instance of an unexpected class type") { /* {{{ */
        t_scope;
        lstr_t bin;
        void *instance = NULL;

        bin = t_iop_bpack_struct(&tstiop__child_class_a__s,
                                 t_iop_new(tstiop__child_class_a));
        Z_ASSERT_NEG(iop_bunpack_ptr(t_pool(), &tstiop__child_class_b__s,
                                     &instance, ps_initlstr(&bin), false));
        Z_ASSERT_STREQUAL(iop_get_err(),
                          "class 'tstiop.ChildClassA' (id 2) "
                          "is not a child of 'tstiop.ChildClassB' (id 3) "
                          "as expected");
    } Z_TEST_END;
    /* }}} */

} Z_GROUP_END

/* LCOV_EXCL_STOP */
