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

#include "iop-json.h"
#include "iopc-iopsq.h"
#include "../tests/iop/tstiop.iop.h"

#include <lib-common/z.h>

/* {{{ Helpers */

static const char *t_get_path(const char *filename)
{
    return t_fmt("%pL/iopsq-tests/%s", &z_cmddir_g, filename);
}

static lstr_t t_build_json_pkg(const char *pkg_name)
{
    return t_lstr_fmt("{\"name\":\"%s\",\"elems\":[]}", pkg_name);
}

static iop__package__t *t_load_package_from_file(const char *filename,
                                                 sb_t *err)
{
    const char *path;
    iop__package__t *pkg_desc = NULL;

    path = t_get_path(filename);
    RETHROW_NP(t_iop_junpack_ptr_file(path, &iop__package__s,
                                      (void **)&pkg_desc, 0, NULL, err));

    return pkg_desc;
}

/* }}} */
/* {{{ Z_HELPERs */

static int t_package_load(iop_pkg_t **pkg, const char *file)
{
    SB_1k(err);
    const iop__package__t *pkg_desc;

    pkg_desc = t_load_package_from_file(file, &err);
    Z_ASSERT_P(pkg_desc, "%s: %pL", file, &err);
    *pkg = mp_iopsq_build_pkg(t_pool(), pkg_desc, NULL, &err);
    Z_ASSERT_P(*pkg, "%s: %pL", file, &err);

    Z_HELPER_END;
}

static int z_assert_ranges_eq(const int *ranges, int ranges_len,
                              const int *ref_ranges, int ref_ranges_len)
{
    Z_ASSERT_EQ(ranges_len, ref_ranges_len, "lengths mismatch");
    for (int i = 0; i < ranges_len * 2 + 1; i++) {
        Z_ASSERT_EQ(ranges[i], ref_ranges[i],
                    "ranges differ at index %d", i);
    }

    Z_HELPER_END;
}

static int z_assert_enum_eq(const iop_enum_t *en, const iop_enum_t *ref)
{
    if (en == ref) {
        return 0;
    }

    Z_ASSERT_LSTREQUAL(en->name, ref->name, "names mismatch");
    /* XXX Don't check fullname: the package name can change. */

    Z_ASSERT_EQ(en->enum_len, ref->enum_len, "length mismatch");
    for (int i = 0; i < en->enum_len; i++) {
        Z_ASSERT_LSTREQUAL(en->names[i], ref->names[i],
                           "names mismatch for element #%d", i);
        Z_ASSERT_EQ(en->values[i], ref->values[i],
                    "values mismatch for element #%d", i);
    }

    Z_ASSERT_EQ(en->flags, ref->flags, "flags mismatch");
    Z_HELPER_RUN(z_assert_ranges_eq(en->ranges, en->ranges_len,
                                    ref->ranges, ref->ranges_len),
                 "ranges mismatch");

    /* TODO Attributes. */
    /* TODO Aliases. */

    Z_HELPER_END;
}

static int z_assert_struct_eq(const iop_struct_t *st,
                              const iop_struct_t *ref);

static int z_assert_field_eq(const iop_field_t *f, const iop_field_t *ref)
{
    Z_ASSERT_LSTREQUAL(f->name, ref->name, "names mismatch");
    Z_ASSERT_EQ(f->tag, ref->tag, "tag mismatch");
    Z_ASSERT(f->tag_len == ref->tag_len, "tag_len field mismatch");
    Z_ASSERT(f->flags == ref->flags, "flags mismatch");
    Z_ASSERT_EQ(f->size, ref->size, "sizes mismatch");
    Z_ASSERT(f->type == ref->type, "types mismatch");
    Z_ASSERT(f->repeat == ref->repeat, "repeat field mismatch");
    Z_ASSERT_EQ(f->data_offs, ref->data_offs, "offset mismatch");

    /* TODO Check default value. */

    if (!iop_type_is_scalar(f->type)) {
        /* TODO Protect against loops. */
        Z_HELPER_RUN(z_assert_struct_eq(f->u1.st_desc, ref->u1.st_desc),
                     "struct type mismatch");
    } else
    if (f->type == IOP_T_ENUM) {
        Z_HELPER_RUN(z_assert_enum_eq(f->u1.en_desc, ref->u1.en_desc),
                     "enum type mismatch");
    }

    Z_HELPER_END;
}

/* Check that two IOP structs are identical. The name can differ. */
static int z_assert_struct_eq(const iop_struct_t *st,
                              const iop_struct_t *ref)
{
    if (st == ref) {
        return 0;
    }

    Z_ASSERT_EQ(st->fields_len, ref->fields_len);

    for (int i = 0; i < st->fields_len; i++) {
        const iop_field_t *fdesc = &st->fields[i];
        const iop_field_t *ref_fdesc = &ref->fields[i];

        Z_HELPER_RUN(z_assert_field_eq(fdesc, ref_fdesc),
                     "got difference(s) on field #%d (`%pL')",
                     i, &ref_fdesc->name);
    }

    Z_ASSERT(st->is_union == ref->is_union);
    Z_ASSERT(st->flags == ref->flags, "flags mismatch: %d vs %d",
             st->flags, ref->flags);
    /* TODO Check attributes. */

    Z_HELPER_RUN(z_assert_ranges_eq(st->ranges, st->ranges_len,
                                    ref->ranges, ref->ranges_len),
                 "ranges mismatch");

    Z_HELPER_END;
}

static int _test_struct(const iop_struct_t *nonnull st_desc,
                        const char **jsons, int nb_jsons,
                        const iop_struct_t *nullable ref_st_desc)
{
    t_scope;
    SB_1k(err);
    SB_1k(jbuf);
    SB_1k(jbuf_ref);

    if (ref_st_desc) {
        Z_HELPER_RUN(z_assert_struct_eq(st_desc, ref_st_desc),
                     "struct description mismatch");
    }

    for (int i = 0; i < nb_jsons; i++) {
        t_scope;
        lstr_t st_json = LSTR(jsons[i]);
        pstream_t ps;
        void *st_ptr = NULL;
        void *st_ptr_bunpacked = NULL;
        void *st_ptr_ref = NULL;
        lstr_t bin;
        lstr_t bin_ref;

        ps = ps_initlstr(&st_json);
        Z_ASSERT_N(t_iop_junpack_ptr_ps(&ps, st_desc, &st_ptr, 0, &err),
                   "cannot junpack `%pL': %pL", &err, &st_json);

        sb_reset(&jbuf);
        Z_ASSERT_N(iop_sb_jpack(&jbuf, st_desc, st_ptr, IOP_JPACK_MINIMAL),
                   "cannot pack to get `%pL'", &st_json);

        Z_ASSERT_LSTREQUAL(LSTR_SB_V(&jbuf), st_json,
                           "the json data changed after unpack/repack");

        bin = t_iop_bpack_struct_flags(st_desc, st_ptr, IOP_BPACK_STRICT);
        Z_ASSERT_P(bin.s, "bpack error: %s", iop_get_err());
        ps = ps_initlstr(&bin);
        Z_ASSERT_N(iop_bunpack_ptr(t_pool(), st_desc, &st_ptr_bunpacked, ps,
                                   true), "bunpack error: %s", iop_get_err());
        Z_ASSERT_IOPEQUAL_DESC(st_desc, st_ptr, st_ptr_bunpacked,
                               "IOP differs after bpack+bunpack");

        if (!ref_st_desc) {
            continue;
        }

        sb_reset(&jbuf_ref);
        Z_ASSERT_N(iop_sb_jpack(&jbuf_ref, ref_st_desc, st_ptr,
                                IOP_JPACK_MINIMAL),
                   "unexpected packing failure");

        Z_ASSERT_STREQUAL(jbuf.data, jbuf_ref.data,
                          "the JSON we obtain differs from the one obtained "
                          "with reference description");

        ps = ps_initlstr(&st_json);
        Z_ASSERT_N(t_iop_junpack_ptr_ps(&ps, ref_st_desc, &st_ptr_ref,
                                        0, &err),
                   "unexpected junpacking failure: %pL", &err);

        Z_ASSERT_IOPEQUAL_DESC(ref_st_desc, st_ptr, st_ptr_ref,
                               "junpacked IOP differs "
                               "(desc = reference desc)");
        Z_ASSERT_IOPEQUAL_DESC(st_desc, st_ptr, st_ptr_ref,
                               "junpacked IOP differs "
                               "(desc = generated desc)");

        bin_ref = t_iop_bpack_struct_flags(st_desc, st_ptr_ref,
                                           IOP_BPACK_STRICT);
        Z_ASSERT_P(bin_ref.s, "unexpected bpack error: %s", iop_get_err());
        Z_ASSERT_LSTREQUAL(bin, bin_ref, "bpacked content differs");
    }

    Z_HELPER_END;
}

#define test_struct(st_desc, ref_st_desc, ...)                               \
    ({                                                                       \
        const char *__files[] = { __VA_ARGS__ };                             \
                                                                             \
        _test_struct(st_desc, __files, countof(__files), (st_desc));         \
    })

static int _test_pkg_struct(const char *pkg_file, int st_index,
                            const char **jsons, int nb_jsons,
                            const iop_struct_t *nullable ref_st_desc)
{
    t_scope;
    iop_pkg_t *pkg;
    const iop_struct_t *st_desc;

    Z_HELPER_RUN(t_package_load(&pkg, pkg_file),
                 "failed to load package");
    st_desc = pkg->structs[st_index];

    Z_HELPER_RUN(_test_struct(st_desc, jsons, nb_jsons, ref_st_desc),
                 "struct tests failed");
    Z_HELPER_END;
}

#define test_pkg_struct(_file, _idx, st_desc, ...)                           \
    ({                                                                       \
        const char *__files[] = { __VA_ARGS__ };                             \
                                                                             \
        _test_pkg_struct(_file, _idx, __files, countof(__files), (st_desc)); \
    })

/* }}} */
/* {{{ Z_GROUP */

Z_GROUP_EXPORT(iopsq) {
    IOP_REGISTER_PACKAGES(&iopsq__pkg);
    IOP_REGISTER_PACKAGES(&tstiop__pkg);

    Z_TEST(struct_, "basic struct") {
        Z_HELPER_RUN(test_pkg_struct("struct.json", 0, NULL,
                                     "{\"i1\":42,\"i2\":2,\"s\":\"foo\"}"));
    } Z_TEST_END;

    Z_TEST(sub_struct, "struct with struct field") {
        t_scope;
        const char *v1 = "{\"i\":51}";
        const char *v2 = "{\"i\":12345678}";
        const char *tst1;
        const char *tst2;

        tst1 = t_fmt("{\"st\":%s,\"stRef\":%s}", v1, v2);
        tst2 = t_fmt("{\"st\":%s,\"stRef\":%s,\"stOpt\":%s}", v1, v2, v1);

        Z_HELPER_RUN(test_pkg_struct("sub-struct.json", 1, tstiop__s2__sp,
                                     tst1, tst2));
    } Z_TEST_END;

    Z_TEST(union_, "basic union") {
        Z_HELPER_RUN(test_pkg_struct("union.json", 0, NULL,
                                     "{\"i\":6}",
                                     "{\"s\":\"toto\"}"));
    } Z_TEST_END;

    Z_TEST(enum_, "basic enum") {
        Z_HELPER_RUN(test_pkg_struct("enum.json", 0,
                                     tstiop__iop_sq_enum_st__sp,
                                     "{\"en\":\"VAL1\"}",
                                     "{\"en\":\"VAL2\"}",
                                     "{\"en\":\"VAL3\"}"));
    } Z_TEST_END;

    Z_TEST(array, "array") {
        Z_HELPER_RUN(test_pkg_struct("array.json", 0, tstiop__array_test__sp,
                                     "{\"i\":[4,5,6]}"));
    } Z_TEST_END;

    Z_TEST(external_types, "external type names") {
        Z_HELPER_RUN(test_pkg_struct("external-types.json", 0,
                                     tstiop__test_external_types__sp,
                                     "{\"st\":{\"i\":42},\"en\":\"B\"}"));
    } Z_TEST_END;

    Z_TEST(error_invalid_pkg_name, "error case: invalid package name") {
        SB_1k(err);
        static struct {
            const char *pkg_name;
            const char *jpack_err;
            const char *lib_err;
        } tests[] = {
            {
                "foo..bar", NULL,
                "invalid package `foo..bar': "
                "invalid name: empty package or sub-package name"
            }, {
                "fOo.bar",
                "1:9: invalid field (ending at `\"fOo.bar\"'): "
                "in type iopsq.Package: violation of constraint pattern "
                "([a-z_\\.]*) on field name: fOo.bar",
                NULL
            }, {
                "foo.", NULL,
                "invalid package `foo.': "
                "invalid name: trailing dot in package name"
            }
        };

        carray_for_each_ptr(t, tests) {
            t_scope;
            lstr_t json = t_build_json_pkg(t->pkg_name);
            pstream_t ps = ps_initlstr(&json);
            iop__package__t pkg_desc;
            int res;

            sb_reset(&err);
            res = t_iop_junpack_ps(&ps, iopsq__package__sp, &pkg_desc, 0,
                                   &err);
            if (t->jpack_err) {
                Z_ASSERT_STREQUAL(err.data, t->jpack_err);
                continue;
            }
            Z_ASSERT_N(res);
            Z_ASSERT_P(t->lib_err);
            Z_ASSERT_NULL(mp_iopsq_build_pkg(t_pool(), &pkg_desc, NULL, &err),
                          "unexpected success");
            Z_ASSERT_STREQUAL(err.data, t->lib_err);
        }
    } Z_TEST_END;

    Z_TEST(full_struct, "test with a struct as complete as possible") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_struct_t *st;
        lstr_t st_name = LSTR("FullStruct");

        /* FIXME: some types cannot be implemented with IOP² yet (classes and
         * fields with default values) so we have to use types from tstiop to
         * avoid dissimilarities between structs. */
        Z_HELPER_RUN(t_package_load(&pkg, "full-struct.json"));
        st = iop_pkg_get_struct_by_name(pkg, st_name);
        Z_ASSERT_P(st, "cannot find struct `%pL'", &st_name);
        Z_HELPER_RUN(z_assert_struct_eq(st, &tstiop__full_struct__s),
                     "structs mismatch");
    } Z_TEST_END;

    Z_TEST(mp_iopsq_build_struct, "test mp_iopsq_build_struct and "
           "iop_struct_mp_build")
    {
        t_scope;
        SB_1k(err);
        iop__package__t *pkg_desc;
        const iop__structure__t *st_desc;
        const iop_struct_t *st;
        iopsq_iop_struct_t st_mp;

        pkg_desc = t_load_package_from_file("single-struct.json", &err);
        Z_ASSERT_P(pkg_desc, "%pL", &err);
        Z_ASSERT_EQ(pkg_desc->elems.len, 1);
        st_desc = iop_obj_ccast(iop__structure, pkg_desc->elems.tab[0]);
        st = mp_iopsq_build_struct(t_pool(), st_desc, NULL, &err);
        Z_ASSERT_P(st, "%pL", &err);
        Z_HELPER_RUN(z_assert_struct_eq(st, &tstiop__tst_build_struct__s),
                     "struct mismatch");

        iopsq_iop_struct_init(&st_mp);
        Z_ASSERT_N(iopsq_iop_struct_build(&st_mp, st_desc, NULL, &err));
        Z_ASSERT_P(st_mp.st, "%pL", &err);
        Z_HELPER_RUN(z_assert_struct_eq(st_mp.st,
                                        &tstiop__tst_build_struct__s),
                     "struct mismatch");

        iopsq_iop_struct_wipe(&st_mp);
        Z_ASSERT_NULL(st_mp.st);
        Z_ASSERT_NULL(st_mp.mp);
        Z_ASSERT_NULL(st_mp.release_cookie);
    } Z_TEST_END;

    Z_TEST(error_misc, "struct error cases miscellaneous") {
        t_scope;
        SB_1k(err);
        const iop__package__t *pkg_desc;
        const char *errors[] = {
            /* TODO Detect the bad type name instead. */
            "failed to resolve the package: error: "
                "unable to find any pkg providing type `foo..Bar`",
            "invalid package `user_package': invalid struct name: "
                "`invalidStructTypeName': "
                "first character should be uppercase",
            "invalid package `user_package': invalid union name: "
                "`invalidUnionTypeName': "
                "first character should be uppercase",
            "invalid package `user_package': invalid enum name: "
                "`invalidEnumTypeName': "
                "first character should be uppercase",
            "invalid package `user_package': "
                "cannot load `MultiDimensionArray': field `multiArray': "
                "multi-dimension arrays are not supported",
            "invalid package `user_package': "
                "cannot load `OptionalArray': field `optionalArray': "
                "repeated field cannot be optional or have a default value",
            "invalid package `user_package': "
                "cannot load `OptionalReference': field `optionalReference': "
                "optional references are not supported",
            "invalid package `user_package': "
                "cannot load `ArrayOfReference': field `arrayOfReference': "
                "arrays of references are not supported",
            "invalid package `user_package': "
                "cannot load `TagConflict': field `f2': "
                "tag `42' is already used by field `f1'",
            "invalid package `user_package': "
                "cannot load `NameConflict': field `field': "
                "name already used by another field",
            "invalid package `user_package': "
                "cannot load enum `ValueConflict': "
                "key `B': the value `42' is already used",
            "invalid package `user_package': "
                "cannot load enum `KeyConflict': "
                "the key `A' is duplicated",
            "failed to generate package `user_package': "
                "struct UnsupportedDefVal: field `field': "
                "default values are not supported yet",
            "failed to resolve the package: "
                "error: unable to find any pkg providing type `Unknown`",
            "invalid package `user_package': "
                "cannot load `LowercaseTypeName': "
                "field `lowercaseTypeName': "
                "invalid type name: `lowercase': "
                "first character should be uppercase",
            "invalid package `user_package': cannot load `UppercaseField': "
                "field `UppercaseField': "
                "first field name character should be lowercase",
            "invalid package `user_package': cannot load `TagTooSmall': "
                "field `tagTooSmall': tag is too small (must be >= 1, got 0)",
            "invalid package `user_package': cannot load `TagTooBig': "
                "field `tagTooBig': "
                "tag is too large (must be < 0x8000, got 0x8000)"
        };
        const char **exp_error = errors;

        pkg_desc = t_load_package_from_file("error-misc.json", &err);
        Z_ASSERT_P(pkg_desc, "%pL", &err);
        Z_ASSERT_EQ(pkg_desc->elems.len, countof(errors));

        tab_for_each_entry(elem, &pkg_desc->elems) {
            t_scope;

            Z_ASSERT_NULL(mp_iopsq_build_mono_element_pkg(t_pool(), elem,
                                                          NULL, &err),
                          "unexpected success for struct %*pS "
                          "(expected error: %s)",
                          IOP_OBJ_FMT_ARG(elem), *exp_error);
            Z_ASSERT_STREQUAL(err.data, *exp_error,
                              "unexpected error message");
            exp_error++;
        }
    } Z_TEST_END;

    Z_TEST(error_duplicated_name, "duplicated type names") {
        t_scope;
        SB_1k(err);
        const iop__package__t *pkg_desc;

        pkg_desc = t_load_package_from_file("error-duplicated-name.json",
                                            &err);
        Z_ASSERT_P(pkg_desc, "%pL", &err);
        Z_ASSERT_NULL(mp_iopsq_build_pkg(t_pool(), pkg_desc, NULL, &err),
                      "unexpected success");
        Z_ASSERT_STREQUAL(err.data, "invalid package `foo': "
                          "already got a thing named `DuplicatedName'");
    } Z_TEST_END;

    Z_TEST(iop_type_to_iop, "test function 'iop_type_to_iop'") {
        iop__type__t res;
        struct {
            iop_type_t type;
            iop__int_size__t sz;
            bool is_signed;
        } int_szs_and_signs[] = {
            { IOP_T_I8, INT_SIZE_S8, true },
            { IOP_T_U8, INT_SIZE_S8, false },
            { IOP_T_I16, INT_SIZE_S16, true },
            { IOP_T_U16, INT_SIZE_S16, false },
            { IOP_T_I32, INT_SIZE_S32, true },
            { IOP_T_U32, INT_SIZE_S32, false },
            { IOP_T_I64, INT_SIZE_S64, true },
            { IOP_T_U64, INT_SIZE_S64, false },
        };

        carray_for_each_ptr(t, int_szs_and_signs) {
            Z_ASSERT_N(iop_type_to_iop(t->type, &res));
            Z_ASSERT_IOPEQUAL(iop__type, &res,
                              &IOP_UNION_VA(iop__type, i,
                                            .is_signed = t->is_signed,
                                            .size = t->sz));
        }

        Z_ASSERT_N(iop_type_to_iop(IOP_T_BOOL, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res, &IOP_UNION_VOID(iop__type, b));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_DOUBLE, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res, &IOP_UNION_VOID(iop__type, d));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_STRING, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res,
                          &IOP_UNION(iop__type, s, STRING_TYPE_STRING));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_DATA, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res,
                          &IOP_UNION(iop__type, s, STRING_TYPE_BYTES));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_XML, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res,
                          &IOP_UNION(iop__type, s, STRING_TYPE_XML));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_VOID, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res, &IOP_UNION_VOID(iop__type, v));

        Z_ASSERT_NEG(iop_type_to_iop(IOP_T_ENUM, &res));
        Z_ASSERT_NEG(iop_type_to_iop(IOP_T_UNION, &res));
        Z_ASSERT_NEG(iop_type_to_iop(IOP_T_STRUCT, &res));
    } Z_TEST_END;

    Z_TEST(type_table, "create types using already generated ones") {
        t_scope;
        SB_1k(err);

        /* TTBasicStruct */
        qv_t(iopsq_field) fields;
        iop__field__t *field;
        iop__struct__t st;
        const iop_struct_t *basic_st_desc;
        iop_full_type_t basic_st_ftype;
        iop__struct__t *expected_st = NULL;

        /* TTBasicEnum */
        qv_t(iopsq_enum_val) enum_vals;
        iop__enum__t en;
        const iop_enum_t *basic_en_desc;
        iop_pkg_t *en_pkg;
        iop_full_type_t basic_en_ftype;

        /* Complete IOP types from tstiop.iop */
        iop_full_type_t tstiop_basic_st_ftype;
        iop_full_type_t tstiop_basic_en_ftype;

        struct {
            const char *name;
            const iop_full_type_t *type;
        } complex_struct_fields[] = {
            { "s", &IOP_FTYPE_STRING, },
            { "stId1", &basic_st_ftype },
            { "enId", &basic_en_ftype },
            { "stTypeName", &tstiop_basic_st_ftype },
            { "stId2", &basic_st_ftype },
            { "enTypeName", &tstiop_basic_en_ftype },
        };

        const iop_struct_t *st_desc;
        IOPSQ_TYPE_TABLE(type_table);

        /* Build a simple structure manually. */
        t_qv_init(&fields, 16);
        field = iop_init(iop__field, qv_growlen(&fields, 1));
        field->name = LSTR("i");
        Z_ASSERT_N(iop_type_to_iop(IOP_T_I32, &field->type));

        iop_init(iop__struct, &st);
        st.name = LSTR("TTBasicStruct");
        st.fields = IOP_TYPED_ARRAY_TAB(iop__field, &fields);
        basic_st_desc = mp_iopsq_build_struct(t_pool(), &st.super, NULL,
                                              &err);
        Z_ASSERT_P(basic_st_desc, "%pL", &err);

        /* Build a simple enumeration manually. */
        t_qv_init(&enum_vals, 16);
        for (int i = 'A'; i <= 'D'; i++) {
            iop__enum_val__t *enum_val;

            enum_val = iop_init(iop__enum_val, qv_growlen(&enum_vals, 1));
            enum_val->name = t_lstr_fmt("%c", i);
        }
        iop_init(iop__enum, &en);
        en.name = LSTR("TTBasicEnum");
        en.values = IOP_TYPED_ARRAY_TAB(iop__enum_val, &enum_vals);
        en_pkg = mp_iopsq_build_mono_element_pkg(t_pool(), &en.super, NULL,
                                                 &err);
        basic_en_desc = en_pkg->enums[0];
        Z_ASSERT_P(basic_en_desc, "the expected enumeration is missing");

        /* Create a structure with two fields of the newly created structure
         * type and one of the new enumeration type.
         */
        basic_st_ftype = IOP_FTYPE_ST_DESC(basic_st_desc);
        basic_en_ftype = IOP_FTYPE_EN_DESC(basic_en_desc);
        tstiop_basic_st_ftype = IOP_FTYPE_ST(tstiop__t_t_basic_struct);
        tstiop_basic_en_ftype = IOP_FTYPE_EN_DESC(&tstiop__t_t_basic_enum__e);
        t_qv_init(&fields, 16);
        carray_for_each_ptr(cfield, complex_struct_fields) {
            field = iop_init(iop__field, qv_growlen(&fields, 1));
            field->name = LSTR(cfield->name);
            iopsq_type_table_fill_type(type_table, cfield->type,
                                       &field->type);
        }

        iop_init(iop__struct, &st);
        st.name = LSTR("TTComplexStruct");
        st.fields = IOP_TYPED_ARRAY_TAB(iop__field, &fields);

        Z_ASSERT_N(t_iop_junpack_ptr_file(t_get_path("type-table.json"),
                                          &iop__struct__s,
                                          (void **)&expected_st, 0, NULL,
                                          &err),
                   "invalid JSON content: %pL", &err);
        Z_ASSERT_IOPEQUAL(iop__struct, &st, expected_st);

        Z_ASSERT_NULL(mp_iopsq_build_struct(t_pool(), &st.super, NULL, &err),
                      "unexpected success (missing type table)");
        st_desc = mp_iopsq_build_struct(t_pool(), &st.super, type_table, &err);
        Z_ASSERT_P(st_desc, "%pL", &err);

        /* Check that the generated desc matches the one declared in
         * tstiop.iop.
         */
        test_struct(st_desc, &tstiop__t_t_complex_struct__s,
                    "{"
                    "\"s\":\"C'est curieux chez les marins "
                    "ce besoin de faire des phrases\","
                    "\"stId1\":{\"i\":24},"
                    "\"enId\":\"B\","
                    "\"stTypeName\":{\"i\":42},"
                    "\"stId2\":{\"i\":7},"
                    "\"enTypeName\":\"D\""
                    "}");
    } Z_TEST_END;

    Z_TEST(iopsq_int_type_to_int_size, "") { /* {{{ */
        struct {
            iop_type_t type;
            iopsq__int_size__t size;
        } int_types[] = {
            { IOP_T_I8, INT_SIZE_S8, },
            { IOP_T_U8, INT_SIZE_S8, },
            { IOP_T_I16, INT_SIZE_S16, },
            { IOP_T_U16, INT_SIZE_S16, },
            { IOP_T_I32, INT_SIZE_S32, },
            { IOP_T_U32, INT_SIZE_S32, },
            { IOP_T_I64, INT_SIZE_S64, },
            { IOP_T_U64, INT_SIZE_S64, },
        };

        carray_for_each_ptr(type, int_types) {
            Z_ASSERT_EQ(iopsq_int_type_to_int_size(type->type), type->size,
                        "wrong size for type %s",
                        iop_type_get_string_desc(type->type));
        }
    } Z_TEST_END;
    /* }}} */
} Z_GROUP_END;

/* }}} */

int main(int argc, char **argv)
{
    z_setup(argc, argv);
    z_register_exports(PLATFORM_PATH LIBCOMMON_PATH "iopc/");
    return z_run();
}
