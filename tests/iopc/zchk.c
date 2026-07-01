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

#include <lib-common/iop-json.h>
#include <lib-common/iop-yaml.h>
#include <lib-common/iopc/iopc-iopsq.h>
#include <lib-common/z.h>

#include "../iop/tstiop.iop.h"

/* {{{ Helpers */

static const char *t_get_path(const char *filename)
{
    return t_fmt("%pL/iopsq-tests/%s", &z_cmddir_g, filename);
}

static lstr_t t_build_json_pkg(const char *pkg_name)
{
    return t_lstr_fmt("{\"name\":\"%s\",\"elems\":[]}", pkg_name);
}

static iop__package__t *t_load_package_from_file(
    const char *filename, const iop_env_t *iop_env, sb_t *err
)
{
    iop_env_ctx_scope(iop_env, iop_env_ctx);
    const char *path;
    iop__package__t *pkg_desc = NULL;

    path = t_get_path(filename);
    RETHROW_NP(t_iop_yunpack_ptr_file(
        iop_env_ctx, path, &iop__package__s, (void **)&pkg_desc, 0, NULL, err
    ));

    return pkg_desc;
}

/* }}} */
/* {{{ Z_HELPERs */

static int
t_package_load(iop_pkg_t **pkg, const iop_env_t *iop_env, const char *file)
{
    iop_env_ctx_scope(iop_env, iop_env_ctx);
    SB_1k(err);
    const iop__package__t *pkg_desc;

    pkg_desc = t_load_package_from_file(file, iop_env, &err);
    Z_ASSERT_P(pkg_desc, "%s: %pL", file, &err);
    *pkg = mp_iopsq_build_pkg(t_pool(), iop_env_ctx, pkg_desc, NULL, &err);
    Z_ASSERT_P(*pkg, "%s: %pL", file, &err);

    Z_HELPER_END;
}

static int z_assert_ranges_eq(
    const int *ranges, int ranges_len, const int *ref_ranges,
    int ref_ranges_len
)
{
    Z_ASSERT_EQ(ranges_len, ref_ranges_len, "lengths mismatch");
    for (int i = 0; i < ranges_len * 2 + 1; i++) {
        Z_ASSERT_EQ(ranges[i], ref_ranges[i], "ranges differ at index %d", i);
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
        Z_ASSERT_LSTREQUAL(
            en->names[i], ref->names[i], "names mismatch for element #%d", i
        );
        Z_ASSERT_EQ(
            en->values[i], ref->values[i], "values mismatch for element #%d",
            i
        );
    }

    Z_ASSERT_EQ(en->flags, ref->flags, "flags mismatch");
    Z_HELPER_RUN(
        z_assert_ranges_eq(
            en->ranges, en->ranges_len, ref->ranges, ref->ranges_len
        ),
        "ranges mismatch"
    );

    if (TST_BIT(&en->flags, IOP_ENUM_ALIASES)) {
        Z_ASSERT_P(en->aliases);
        Z_ASSERT_P(ref->aliases);
        Z_ASSERT_EQ(
            en->aliases->len, ref->aliases->len, "aliases count mismatch"
        );
        for (int i = 0; i < en->aliases->len; i++) {
            Z_ASSERT_EQ(
                en->aliases->aliases[i].pos, ref->aliases->aliases[i].pos,
                "alias position mismatch for alias #%d", i
            );
            Z_ASSERT_LSTREQUAL(
                en->aliases->aliases[i].name, ref->aliases->aliases[i].name,
                "alias name mismatch for alias #%d", i
            );
        }
    }

    /* TODO Attributes. */

    Z_HELPER_END;
}

static int
z_assert_struct_eq(const iop_struct_t *st, const iop_struct_t *ref);

static int z_assert_static_field_eq(
    const iop_static_field_t *sf, const iop_static_field_t *ref
)
{
    Z_ASSERT_LSTREQUAL(sf->name, ref->name, "static field name mismatch");
    Z_ASSERT_EQ(sf->type, ref->type, "static field type mismatch");

    switch (sf->type) {
    case IOP_T_DOUBLE:
        Z_ASSERT_EQ(sf->value.d, ref->value.d, "static field value mismatch");
        break;
    case IOP_T_STRING:
        Z_ASSERT_LSTREQUAL(
            sf->value.s, ref->value.s, "static field value mismatch"
        );
        break;
    default:
        Z_ASSERT_EQ(sf->value.u, ref->value.u, "static field value mismatch");
        break;
    }

    Z_HELPER_END;
}

/* Compare the class-specific attributes of two class descriptors: parent,
 * class id, abstract/private flags and static fields. */
static int z_assert_class_attrs_eq(
    const iop_class_attrs_t *attrs, const iop_class_attrs_t *ref
)
{
    Z_ASSERT_EQ(attrs->class_id, ref->class_id, "class id mismatch");
    Z_ASSERT(
        attrs->is_abstract == ref->is_abstract, "abstract flag mismatch"
    );
    Z_ASSERT(attrs->is_private == ref->is_private, "private flag mismatch");

    Z_ASSERT(
        (attrs->parent == NULL) == (ref->parent == NULL),
        "parent presence mismatch"
    );
    if (attrs->parent) {
        Z_HELPER_RUN(
            z_assert_struct_eq(attrs->parent, ref->parent), "parent mismatch"
        );
    }

    Z_ASSERT_EQ(
        attrs->static_fields_len, ref->static_fields_len,
        "static fields count mismatch"
    );
    for (int i = 0; i < attrs->static_fields_len; i++) {
        Z_HELPER_RUN(
            z_assert_static_field_eq(
                attrs->static_fields[i], ref->static_fields[i]
            ),
            "static field #%d mismatch", i
        );
    }

    Z_HELPER_END;
}

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

    if (f->repeat == IOP_R_DEFVAL) {
        switch (f->type) {
        case IOP_T_I8 ... IOP_T_U64:
        case IOP_T_BOOL:
            Z_ASSERT_EQ(
                f->u1.defval_u64, ref->u1.defval_u64, "defval mismatch"
            );
            break;
        case IOP_T_DOUBLE:
            Z_ASSERT_EQ(f->u1.defval_d, ref->u1.defval_d, "defval mismatch");
            break;
        case IOP_T_ENUM:
            Z_ASSERT_EQ(
                f->u0.defval_enum, ref->u0.defval_enum, "defval mismatch"
            );
            break;
        case IOP_T_STRING:
        case IOP_T_DATA:
        case IOP_T_XML:
            Z_ASSERT_EQ(
                f->u0.defval_len, ref->u0.defval_len, "defval length mismatch"
            );
            Z_ASSERT_EQUAL(
                (const char *)f->u1.defval_data, f->u0.defval_len,
                (const char *)ref->u1.defval_data, ref->u0.defval_len,
                "defval data mismatch"
            );
            break;
        default:
            break;
        }
    }

    if (!iop_type_is_scalar(f->type)) {
        /* TODO Protect against loops. */
        Z_HELPER_RUN(
            z_assert_struct_eq(f->u1.st_desc, ref->u1.st_desc),
            "struct type mismatch"
        );
    } else if (f->type == IOP_T_ENUM) {
        Z_HELPER_RUN(
            z_assert_enum_eq(f->u1.en_desc, ref->u1.en_desc),
            "enum type mismatch"
        );
    }

    Z_HELPER_END;
}

/* Check that two IOP structs are identical. The name can differ. */
static int z_assert_struct_eq(const iop_struct_t *st, const iop_struct_t *ref)
{
    if (st == ref) {
        return 0;
    }

    Z_ASSERT_EQ(st->fields_len, ref->fields_len);

    for (int i = 0; i < st->fields_len; i++) {
        const iop_field_t *fdesc = &st->fields[i];
        const iop_field_t *ref_fdesc = &ref->fields[i];

        Z_HELPER_RUN(
            z_assert_field_eq(fdesc, ref_fdesc),
            "got difference(s) on field #%d (`%pL')", i, &ref_fdesc->name
        );
    }

    Z_ASSERT(st->is_union == ref->is_union);
    Z_ASSERT(
        st->flags == ref->flags, "flags mismatch: %d vs %d", st->flags,
        ref->flags
    );
    /* TODO Check attributes. */

    if (iop_struct_is_class(st)) {
        Z_HELPER_RUN(
            z_assert_class_attrs_eq(st->class_attrs, ref->class_attrs),
            "class attributes mismatch"
        );
    }

    Z_HELPER_RUN(
        z_assert_ranges_eq(
            st->ranges, st->ranges_len, ref->ranges, ref->ranges_len
        ),
        "ranges mismatch"
    );

    Z_HELPER_END;
}

/* Check that two IOP typedefs are identical: same aliased type and, for a
 * struct/enum alias, the same referenced descriptor. */
static int
z_assert_typedef_eq(const iop_typedef_t *td, const iop_typedef_t *ref)
{
    if (td == ref) {
        return 0;
    }

    Z_ASSERT_LSTREQUAL(td->fullname, ref->fullname, "typedef name mismatch");
    Z_ASSERT_EQ(td->type, ref->type, "typedef type mismatch");

    if (td->type == IOP_T_ENUM) {
        Z_HELPER_RUN(
            z_assert_enum_eq(td->ref_enum, ref->ref_enum),
            "typedef enum mismatch"
        );
    } else if (!iop_type_is_scalar(td->type)) {
        Z_HELPER_RUN(
            z_assert_struct_eq(td->ref_struct, ref->ref_struct),
            "typedef struct mismatch"
        );
    }

    Z_HELPER_END;
}

/* Compare an RPC arg/res/exn part: a void part is the shared &iop__void__s
 * descriptor in both (z_assert_struct_eq short-circuits on it). */
static int
z_assert_rpc_part_eq(const iop_struct_t *st, const iop_struct_t *ref)
{
    Z_ASSERT((st == NULL) == (ref == NULL), "rpc part presence mismatch");
    if (st) {
        Z_HELPER_RUN(z_assert_struct_eq(st, ref), "rpc part mismatch");
    }
    Z_HELPER_END;
}

static int z_assert_rpc_eq(const iop_rpc_t *rpc, const iop_rpc_t *ref)
{
    Z_ASSERT_LSTREQUAL(rpc->name, ref->name, "rpc name mismatch");
    Z_ASSERT_EQ(rpc->tag, ref->tag, "rpc tag mismatch");
    Z_ASSERT(rpc->async == ref->async, "rpc async flag mismatch");
    Z_HELPER_RUN(
        z_assert_rpc_part_eq(rpc->args, ref->args), "rpc arg mismatch"
    );
    Z_HELPER_RUN(
        z_assert_rpc_part_eq(rpc->result, ref->result), "rpc res mismatch"
    );
    Z_HELPER_RUN(
        z_assert_rpc_part_eq(rpc->exn, ref->exn), "rpc exn mismatch"
    );
    Z_HELPER_END;
}

static int z_assert_iface_eq(const iop_iface_t *iface, const iop_iface_t *ref)
{
    Z_ASSERT_LSTREQUAL(iface->fullname, ref->fullname, "iface name mismatch");
    Z_ASSERT_EQ(iface->funs_len, ref->funs_len, "rpc count mismatch");
    Z_ASSERT(iface->flags == ref->flags, "iface flags mismatch");

    for (int i = 0; i < iface->funs_len; i++) {
        Z_HELPER_RUN(
            z_assert_rpc_eq(&iface->funs[i], &ref->funs[i]),
            "rpc #%d mismatch", i
        );
    }
    Z_HELPER_END;
}

static int z_assert_mod_eq(const iop_mod_t *mod, const iop_mod_t *ref)
{
    Z_ASSERT_LSTREQUAL(mod->fullname, ref->fullname, "module name mismatch");
    Z_ASSERT_EQ(mod->ifaces_len, ref->ifaces_len, "iface count mismatch");
    Z_ASSERT(mod->flags == ref->flags, "module flags mismatch");

    for (int i = 0; i < mod->ifaces_len; i++) {
        const iop_iface_alias_t *a = &mod->ifaces[i];
        const iop_iface_alias_t *ra = &ref->ifaces[i];

        Z_ASSERT_LSTREQUAL(a->name, ra->name, "iface alias name mismatch");
        Z_ASSERT_EQ(a->tag, ra->tag, "iface alias tag mismatch");
        Z_HELPER_RUN(
            z_assert_iface_eq(a->iface, ra->iface), "aliased iface mismatch"
        );
    }
    Z_HELPER_END;
}

static int _test_struct(
    const iop_env_t *iop_env, const iop_struct_t *nonnull st_desc,
    const char **jsons, int nb_jsons, const iop_struct_t *nullable ref_st_desc
)
{
    t_scope;
    SB_1k(err);
    SB_1k(jbuf);
    SB_1k(jbuf_ref);
    iop_env_ctx_scope(iop_env, iop_env_ctx);

    if (ref_st_desc) {
        Z_HELPER_RUN(
            z_assert_struct_eq(st_desc, ref_st_desc),
            "struct description mismatch"
        );
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
        Z_ASSERT_N(
            t_iop_junpack_ptr_ps(iop_env_ctx, &ps, st_desc, &st_ptr, 0, &err),
            "cannot junpack `%pL': %pL", &err, &st_json
        );

        sb_reset(&jbuf);
        Z_ASSERT_N(
            iop_sb_jpack(&jbuf, st_desc, st_ptr, IOP_JPACK_MINIMAL),
            "cannot pack to get `%pL'", &st_json
        );

        Z_ASSERT_LSTREQUAL(
            LSTR_SB_V(&jbuf), st_json,
            "the json data changed after unpack/repack"
        );

        bin = t_iop_bpack_struct_flags(st_desc, st_ptr, IOP_BPACK_STRICT);
        Z_ASSERT_P(bin.s, "bpack error: %s", iop_get_err());
        ps = ps_initlstr(&bin);
        Z_ASSERT_N(
            iop_bunpack_ptr(
                t_pool(), iop_env_ctx, st_desc, &st_ptr_bunpacked, ps, true
            ),
            "bunpack error: %s", iop_get_err()
        );
        Z_ASSERT_IOPEQUAL_DESC(
            st_desc, st_ptr, st_ptr_bunpacked,
            "IOP differs after bpack+bunpack"
        );

        if (!ref_st_desc) {
            continue;
        }

        sb_reset(&jbuf_ref);
        Z_ASSERT_N(
            iop_sb_jpack(&jbuf_ref, ref_st_desc, st_ptr, IOP_JPACK_MINIMAL),
            "unexpected packing failure"
        );

        Z_ASSERT_STREQUAL(
            jbuf.data, jbuf_ref.data,
            "the JSON we obtain differs from the one obtained "
            "with reference description"
        );

        ps = ps_initlstr(&st_json);
        Z_ASSERT_N(
            t_iop_junpack_ptr_ps(
                iop_env_ctx, &ps, ref_st_desc, &st_ptr_ref, 0, &err
            ),
            "unexpected junpacking failure: %pL", &err
        );

        Z_ASSERT_IOPEQUAL_DESC(
            ref_st_desc, st_ptr, st_ptr_ref,
            "junpacked IOP differs "
            "(desc = reference desc)"
        );
        Z_ASSERT_IOPEQUAL_DESC(
            st_desc, st_ptr, st_ptr_ref,
            "junpacked IOP differs "
            "(desc = generated desc)"
        );

        bin_ref =
            t_iop_bpack_struct_flags(st_desc, st_ptr_ref, IOP_BPACK_STRICT);
        Z_ASSERT_P(bin_ref.s, "unexpected bpack error: %s", iop_get_err());
        Z_ASSERT_LSTREQUAL(bin, bin_ref, "bpacked content differs");
    }

    Z_HELPER_END;
}

#define test_struct(iop_env, st_desc, ref_st_desc, ...)                      \
    ({                                                                       \
        const char *__files[] = {__VA_ARGS__};                               \
                                                                             \
        _test_struct(                                                        \
            (iop_env), (st_desc), __files, countof(__files), (st_desc)       \
        );                                                                   \
    })

static int _test_pkg_struct(
    const iop_env_t *iop_env, const char *pkg_file, int st_index,
    const char **jsons, int nb_jsons, const iop_struct_t *nullable ref_st_desc
)
{
    t_scope;
    iop_pkg_t *pkg;
    const iop_struct_t *st_desc;

    Z_HELPER_RUN(
        t_package_load(&pkg, iop_env, pkg_file), "failed to load package"
    );
    st_desc = pkg->structs[st_index];

    Z_HELPER_RUN(
        _test_struct(iop_env, st_desc, jsons, nb_jsons, ref_st_desc),
        "struct tests failed"
    );
    Z_HELPER_END;
}

#define test_pkg_struct(_iop_env, _file, _idx, st_desc, ...)                 \
    ({                                                                       \
        const char *__files[] = {__VA_ARGS__};                               \
                                                                             \
        _test_pkg_struct(                                                    \
            (_iop_env), (_file), (_idx), __files, countof(__files),          \
            (st_desc)                                                        \
        );                                                                   \
    })

static int z_field_index(const iop_struct_t *st, const char *name)
{
    for (int i = 0; i < st->fields_len; i++) {
        if (lstr_equal(st->fields[i].name, LSTR(name))) {
            return i;
        }
    }
    return -1;
}

/* }}} */
/* {{{ Z_GROUP */

Z_GROUP_EXPORT(iopsq)
{
    iop_env_t *iop_env;

    iop_env = iop_env_new();
    IOP_REGISTER_PACKAGES(iop_env, &iopsq__pkg);
    IOP_REGISTER_PACKAGES(iop_env, &tstiop__pkg);

    Z_TEST(struct_, "basic struct") {
        Z_HELPER_RUN(test_pkg_struct(
            iop_env, "struct.yml", 0, NULL,
            "{\"i1\":42,\"i2\":2,\"s\":\"foo\"}"
        ));
    }
    Z_TEST_END;

    Z_TEST(sub_struct, "struct with struct field") {
        t_scope;
        const char *v1 = "{\"i\":51}";
        const char *v2 = "{\"i\":12345678}";
        const char *tst1;
        const char *tst2;

        tst1 = t_fmt("{\"st\":%s,\"stRef\":%s}", v1, v2);
        tst2 = t_fmt("{\"st\":%s,\"stRef\":%s,\"stOpt\":%s}", v1, v2, v1);

        Z_HELPER_RUN(test_pkg_struct(
            iop_env, "sub-struct.yml", 1, &tstiop__s2__s, tst1, tst2
        ));
    }
    Z_TEST_END;

    Z_TEST(union_, "basic union") {
        Z_HELPER_RUN(test_pkg_struct(
            iop_env, "union.yml", 0, NULL, "{\"i\":6}", "{\"s\":\"toto\"}"
        ));
    }
    Z_TEST_END;

    Z_TEST(enum_, "basic enum") {
        Z_HELPER_RUN(test_pkg_struct(
            iop_env, "enum.yml", 0, &tstiop__iop_sq_enum_st__s,
            "{\"en\":\"VAL1\"}", "{\"en\":\"VAL2\"}", "{\"en\":\"VAL3\"}"
        ));
    }
    Z_TEST_END;

    Z_TEST(array, "array") {
        Z_HELPER_RUN(test_pkg_struct(
            iop_env, "array.yml", 0, &tstiop__array_test__s, "{\"i\":[4,5,6]}"
        ));
    }
    Z_TEST_END;

    Z_TEST(typedef_, "package with typedefs") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_typedef_t *td;
        int n = 0;

        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "typedef.yml"));

        Z_ASSERT_P(pkg->typedefs);
        for (const iop_typedef_t *const *it = pkg->typedefs; *it; it++) {
            n++;
        }
        Z_ASSERT_EQ(n, 3, "expected 3 typedefs");

        /* MyInt -> int: a scalar typedef has no referenced object. */
        td = pkg->typedefs[0];
        Z_ASSERT_LSTREQUAL(td->fullname, LSTR("foo.MyInt"));
        Z_ASSERT(td->type == IOP_T_I32);

        /* MyColor -> Color (enum). */
        td = pkg->typedefs[1];
        Z_ASSERT_LSTREQUAL(td->fullname, LSTR("foo.MyColor"));
        Z_ASSERT(td->type == IOP_T_ENUM);
        Z_ASSERT_P(td->ref_enum);
        Z_ASSERT_LSTREQUAL(td->ref_enum->name, LSTR("Color"));

        /* MyPoint -> Point (struct). */
        td = pkg->typedefs[2];
        Z_ASSERT_LSTREQUAL(td->fullname, LSTR("foo.MyPoint"));
        Z_ASSERT(td->type == IOP_T_STRUCT);
        Z_ASSERT_P(td->ref_struct);
        Z_ASSERT_LSTREQUAL(td->ref_struct->fullname, LSTR("foo.Point"));
    }
    Z_TEST_END;

    Z_TEST(field_attrs, "field constraints and generic attributes") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_struct_t *st;
        const iop_field_attrs_t *fa;
        int idx;

        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "field-attrs.yml"));
        st = pkg->structs[0];
        Z_ASSERT_LSTREQUAL(st->fullname, LSTR("foo.S"));
        Z_ASSERT(st->flags & (1U << IOP_STRUCT_EXTENDED));
        Z_ASSERT_P(st->fields_attrs);

        /* n: @min(1) @max(100). */
        idx = z_field_index(st, "n");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_MIN));
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_MAX));
        Z_ASSERT_EQ(fa->attrs_len, 2);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_MIN);
        Z_ASSERT_EQ(fa->attrs[0].args->v.i64, 1);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_MAX);
        Z_ASSERT_EQ(fa->attrs[1].args->v.i64, 100);

        /* u: @min/@max on an unsigned field. */
        idx = z_field_index(st, "u");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 2);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_MIN);
        Z_ASSERT_EQ(fa->attrs[0].args->v.i64, 1);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_MAX);
        Z_ASSERT_EQ(fa->attrs[1].args->v.i64, 100);

        /* d: @min/@max on a double field keep the double value. */
        idx = z_field_index(st, "d");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 2);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_MIN);
        Z_ASSERT_EQ(fa->attrs[0].args->v.d, -1.5);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_MAX);
        Z_ASSERT_EQ(fa->attrs[1].args->v.d, 2.5);

        /* arr: repeated @minOccurs(2) @maxOccurs(5). */
        idx = z_field_index(st, "arr");
        Z_ASSERT_N(idx);
        Z_ASSERT(st->fields[idx].flags & (1U << IOP_FIELD_NO_EMPTY_ARRAY));
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 2);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_MIN_OCCURS);
        Z_ASSERT_EQ(fa->attrs[0].args->v.i64, 2);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_MAX_OCCURS);
        Z_ASSERT_EQ(fa->attrs[1].args->v.i64, 5);

        /* str: @minLength(3) @maxLength(8) @pattern("ab.*"). */
        idx = z_field_index(st, "str");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 3);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_MIN_LENGTH);
        Z_ASSERT_EQ(fa->attrs[0].args->v.i64, 3);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_MAX_LENGTH);
        Z_ASSERT_EQ(fa->attrs[1].args->v.i64, 8);
        Z_ASSERT(fa->attrs[2].type == IOP_FIELD_PATTERN);
        Z_ASSERT_LSTREQUAL(fa->attrs[2].args->v.s, LSTR("ab.*"));

        /* fixed: @length(4) expands to @minLength and @maxLength. */
        idx = z_field_index(st, "fixed");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 2);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_MIN_LENGTH);
        Z_ASSERT_EQ(fa->attrs[0].args->v.i64, 4);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_MAX_LENGTH);
        Z_ASSERT_EQ(fa->attrs[1].args->v.i64, 4);

        /* text: @nonEmpty @cdata are flag-only (no table entry). */
        idx = z_field_index(st, "text");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 0);
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_NON_EMPTY));
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_CDATA));

        /* secret: @private, only allowed on a non-required field. */
        idx = z_field_index(st, "secret");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 0);
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_PRIVATE));

        /* flagged: @nonZero @deprecated are flag-only too. */
        idx = z_field_index(st, "flagged");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 0);
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_NON_ZERO));
        Z_ASSERT(TST_BIT(&fa->flags, IOP_FIELD_DEPRECATED));

        /* g: generic attributes, typed by their value. */
        idx = z_field_index(st, "g");
        Z_ASSERT_N(idx);
        fa = &st->fields_attrs[idx];
        Z_ASSERT_EQ(fa->attrs_len, 5);
        Z_ASSERT(fa->attrs[0].type == IOP_FIELD_GEN_ATTR_I);
        Z_ASSERT_EQ(fa->attrs[0].args->v.i64, 42);
        Z_ASSERT(fa->attrs[1].type == IOP_FIELD_GEN_ATTR_S);
        Z_ASSERT_LSTREQUAL(fa->attrs[1].args->v.s, LSTR("hello"));
        Z_ASSERT(fa->attrs[2].type == IOP_FIELD_GEN_ATTR_I);
        Z_ASSERT_EQ(fa->attrs[2].args->v.i64, 7);
        Z_ASSERT(fa->attrs[3].type == IOP_FIELD_GEN_ATTR_D);
        Z_ASSERT_EQ(fa->attrs[3].args->v.d, 1.5);
        Z_ASSERT(fa->attrs[4].type == IOP_FIELD_GEN_ATTR_I);
        Z_ASSERT_EQ(fa->attrs[4].args->v.i64, 1);
    }
    Z_TEST_END;

    Z_TEST(struct_attrs, "struct-level generic attributes") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_struct_t *st;
        const iop_struct_attrs_t *sa;

        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "struct-attrs.yml"));

        /* S: three generic attributes, typed by their value. The struct attr
         * kind does not mirror the gen-attr type into 'flags', so it stays 0.
         */
        st = pkg->structs[0];
        Z_ASSERT_LSTREQUAL(st->fullname, LSTR("foo.S"));
        Z_ASSERT(st->flags & (1U << IOP_STRUCT_EXTENDED));
        Z_ASSERT_P(st->st_attrs);
        sa = st->st_attrs;
        Z_ASSERT_EQ(sa->flags, 0u);
        Z_ASSERT_EQ(sa->attrs_len, 3);
        Z_ASSERT(sa->attrs[0].type == IOP_STRUCT_GEN_ATTR_I);
        Z_ASSERT_EQ(sa->attrs[0].args->v.i64, 42);
        Z_ASSERT(sa->attrs[1].type == IOP_STRUCT_GEN_ATTR_S);
        Z_ASSERT_LSTREQUAL(sa->attrs[1].args->v.s, LSTR("hello"));
        Z_ASSERT(sa->attrs[2].type == IOP_STRUCT_GEN_ATTR_D);
        Z_ASSERT_EQ(sa->attrs[2].args->v.d, 1.5);

        /* U: unions inherit 'genericAttrs' from the Structure base class. */
        st = pkg->structs[1];
        Z_ASSERT_LSTREQUAL(st->fullname, LSTR("foo.U"));
        Z_ASSERT(st->is_union);
        Z_ASSERT(st->flags & (1U << IOP_STRUCT_EXTENDED));
        Z_ASSERT_P(st->st_attrs);
        Z_ASSERT_EQ(st->st_attrs->attrs_len, 1);
        Z_ASSERT(st->st_attrs->attrs[0].type == IOP_STRUCT_GEN_ATTR_S);
        Z_ASSERT_LSTREQUAL(st->st_attrs->attrs[0].args->v.s, LSTR("u"));

        /* Plain: no attributes -> st_attrs is NULL and the flag is unset. */
        st = pkg->structs[2];
        Z_ASSERT_LSTREQUAL(st->fullname, LSTR("foo.Plain"));
        Z_ASSERT(!(st->flags & (1U << IOP_STRUCT_EXTENDED)));
        Z_ASSERT_NULL(st->st_attrs);
    }
    Z_TEST_END;

    Z_TEST(class_, "classes with inheritance and static fields") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_struct_t *base;
        const iop_struct_t *child;
        const iop_static_field_t *sf;

        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "class.yml"));

        /* Base: abstract master class with one static field. */
        base = pkg->structs[0];
        Z_ASSERT_LSTREQUAL(base->fullname, LSTR("foo.Base"));
        Z_ASSERT(iop_struct_is_class(base));
        Z_ASSERT(base->flags & (1U << IOP_STRUCT_EXTENDED));
        Z_ASSERT(base->flags & (1U << IOP_STRUCT_STATIC_HAS_TYPE));
        Z_ASSERT_EQ(base->fields_len, 1);
        Z_ASSERT_P(base->class_attrs);
        Z_ASSERT_NULL(base->class_attrs->parent);
        Z_ASSERT_EQ(base->class_attrs->class_id, 0);
        Z_ASSERT(base->class_attrs->is_abstract);
        Z_ASSERT(!base->class_attrs->is_private);
        Z_ASSERT_EQ(base->class_attrs->static_fields_len, 1);
        sf = base->class_attrs->static_fields[0];
        Z_ASSERT_LSTREQUAL(sf->name, LSTR("version"));
        Z_ASSERT(sf->type == IOP_T_I64);
        Z_ASSERT_EQ(sf->value.i, 1);

        /* Child: has a class id, points at its parent, and is private. It
         * carries only its own field 's' ('i' comes from the parent). */
        child = pkg->structs[1];
        Z_ASSERT_LSTREQUAL(child->fullname, LSTR("foo.Child"));
        Z_ASSERT(iop_struct_is_class(child));
        Z_ASSERT_EQ(child->fields_len, 1);
        Z_ASSERT_P(child->class_attrs);
        Z_ASSERT(child->class_attrs->parent == base);
        Z_ASSERT_EQ(child->class_attrs->class_id, 42);
        Z_ASSERT(!child->class_attrs->is_abstract);
        Z_ASSERT(child->class_attrs->is_private);
    }
    Z_TEST_END;

    Z_TEST(iface_module, "interfaces, RPCs and modules") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_iface_t *iface;
        const iop_mod_t *mod;
        const iop_rpc_t *rpc;

        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "iface-module.yml"));

        /* A single interface, GetUser, with two RPCs. */
        iface = pkg->ifaces[0];
        Z_ASSERT_P(iface);
        Z_ASSERT_NULL(pkg->ifaces[1]);
        Z_ASSERT_LSTREQUAL(iface->fullname, LSTR("foo.GetUser"));
        Z_ASSERT_EQ(iface->funs_len, 2);

        /* getUser: anonymous 'in'/'out' structures, void exceptions, tag
         * auto-assigned to 1. */
        rpc = &iface->funs[0];
        Z_ASSERT_LSTREQUAL(rpc->name, LSTR("getUser"));
        Z_ASSERT_EQ((int)rpc->tag, 1);
        Z_ASSERT(!rpc->async);
        Z_ASSERT_P(rpc->args);
        Z_ASSERT(rpc->args != &iop__void__s);
        Z_ASSERT_EQ(rpc->args->fields_len, 1);
        Z_ASSERT_LSTREQUAL(rpc->args->fields[0].name, LSTR("id"));
        Z_ASSERT(rpc->result != &iop__void__s);
        Z_ASSERT_EQ(rpc->result->fields_len, 1);
        Z_ASSERT_LSTREQUAL(rpc->result->fields[0].name, LSTR("name"));
        Z_ASSERT(rpc->exn == &iop__void__s);

        /* ping: asynchronous, so all parts are void. */
        rpc = &iface->funs[1];
        Z_ASSERT_LSTREQUAL(rpc->name, LSTR("ping"));
        Z_ASSERT_EQ((int)rpc->tag, 2);
        Z_ASSERT(rpc->async);
        Z_ASSERT(rpc->args == &iop__void__s);
        Z_ASSERT(rpc->result == &iop__void__s);
        Z_ASSERT(rpc->exn == &iop__void__s);

        /* A single module, MyModule: it references GetUser under the alias
         * 'users' (tag 1). */
        mod = pkg->mods[0];
        Z_ASSERT_P(mod);
        Z_ASSERT_NULL(pkg->mods[1]);
        Z_ASSERT_LSTREQUAL(mod->fullname, LSTR("foo.MyModule"));
        Z_ASSERT_EQ(mod->ifaces_len, 1);
        Z_ASSERT_LSTREQUAL(mod->ifaces[0].name, LSTR("users"));
        Z_ASSERT_EQ((int)mod->ifaces[0].tag, 1);
        Z_ASSERT(mod->ifaces[0].iface == iface);
    }
    Z_TEST_END;

    Z_TEST(enum_strict_aliases, "enum strictness and value aliases") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_enum_t *en;

        Z_HELPER_RUN(
            t_package_load(&pkg, iop_env, "enum-strict-aliases.yml")
        );

        /* StrictEnum: @strict maps to the IOP_ENUM_STRICT flag. */
        en = pkg->enums[0];
        Z_ASSERT_LSTREQUAL(en->name, LSTR("StrictEnum"));
        Z_ASSERT(TST_BIT(&en->flags, IOP_ENUM_STRICT));
        Z_ASSERT(!TST_BIT(&en->flags, IOP_ENUM_ALIASES));
        Z_ASSERT_NULL(en->aliases);

        /* AliasEnum: value aliases build an iop_enum_aliases_t table, with
         * one entry per alias pointing at the position of its value. */
        en = pkg->enums[1];
        Z_ASSERT_LSTREQUAL(en->name, LSTR("AliasEnum"));
        Z_ASSERT(TST_BIT(&en->flags, IOP_ENUM_ALIASES));
        Z_ASSERT(!TST_BIT(&en->flags, IOP_ENUM_STRICT));
        Z_ASSERT_P(en->aliases);
        Z_ASSERT_EQ(en->aliases->len, 3);
        Z_ASSERT_EQ(en->aliases->aliases[0].pos, 0);
        Z_ASSERT_LSTREQUAL(en->aliases->aliases[0].name, LSTR("A_ALIAS"));
        Z_ASSERT_EQ(en->aliases->aliases[1].pos, 2);
        Z_ASSERT_LSTREQUAL(en->aliases->aliases[1].name, LSTR("C_ALIAS_1"));
        Z_ASSERT_EQ(en->aliases->aliases[2].pos, 2);
        Z_ASSERT_LSTREQUAL(en->aliases->aliases[2].name, LSTR("C_ALIAS_2"));
    }
    Z_TEST_END;

    Z_TEST(external_types, "external type names") {
        Z_HELPER_RUN(test_pkg_struct(
            iop_env, "external-types.yml", 0, &tstiop__test_external_types__s,
            "{\"st\":{\"i\":42},\"en\":\"B\"}"
        ));
    }
    Z_TEST_END;

    Z_TEST(error_invalid_pkg_name, "error case: invalid package name") {
        SB_1k(err);
        static struct {
            const char *pkg_name;
            const char *jpack_err;
            const char *lib_err;
        } tests[] = {
            {"foo..bar", NULL,
             "invalid package `foo..bar': "
             "invalid name: empty package or sub-package name"},
            {"fOo.bar",
             "1:9: invalid field (ending at `\"fOo.bar\"'): "
             "in type iopsq.Package: violation of constraint pattern "
             "([a-z_\\.]*) on field name: fOo.bar",
             NULL},
            {"foo.", NULL,
             "invalid package `foo.': "
             "invalid name: trailing dot in package name"}
        };

        carray_for_each_ptr(t, tests) {
            t_scope;
            iop_env_ctx_scope(iop_env, iop_env_ctx);
            lstr_t json = t_build_json_pkg(t->pkg_name);
            pstream_t ps = ps_initlstr(&json);
            iop__package__t pkg_desc;
            int res;

            sb_reset(&err);
            res = t_iop_junpack_ps(
                iop_env_ctx, &ps, &iopsq__package__s, &pkg_desc, 0, &err
            );
            if (t->jpack_err) {
                Z_ASSERT_STREQUAL(err.data, t->jpack_err);
                continue;
            }
            Z_ASSERT_N(res);
            Z_ASSERT_P(t->lib_err);
            Z_ASSERT_NULL(
                mp_iopsq_build_pkg(
                    t_pool(), iop_env_ctx, &pkg_desc, NULL, &err
                ),
                "unexpected success"
            );
            Z_ASSERT_STREQUAL(err.data, t->lib_err);
        }
    }
    Z_TEST_END;

    Z_TEST(full_struct, "test with a struct as complete as possible") {
        t_scope;
        iop_pkg_t *pkg;
        const iop_struct_t *st;
        lstr_t st_name = LSTR("FullStruct");

        /* FIXME: classes cannot be implemented with IOP² yet, so the class
         * fields still use types from tstiop to avoid dissimilarities between
         * structs. */
        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "full-struct.yml"));
        st = iop_pkg_get_struct_by_name(pkg, st_name);
        Z_ASSERT_P(st, "cannot find struct `%pL'", &st_name);
        Z_HELPER_RUN(
            z_assert_struct_eq(st, &tstiop__full_struct__s),
            "structs mismatch"
        );
    }
    Z_TEST_END;

    Z_TEST(
        iopsq_from_iop, "reverse conversion: iop_struct_t -> iopsq -> "
                        "iop_struct_t round-trip"
    )
    {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        const iop_struct_t *refs[] = {
            &tstiop__full_required__s, &tstiop__full_def_val__s,
            &tstiop__full_opt__s,      &tstiop__full_repeated__s,
            &tstiop__full_ref__s,      &tstiop__my_union_a__s,
            &tstiop__my_union_b__s,
        };

        /* Extract each compiled struct back into an IOP² description, rebuild
         * a descriptor from it and check it matches the original. Referenced
         * types resolve to the same env descriptors, so z_assert_struct_eq
         * short-circuits on them. */
        carray_for_each_entry(ref, refs) {
            iop__package_elem__t *elem;
            iop_pkg_t *pkg;

            elem = mp_iopsq_elem_from_iop_struct(t_pool(), ref, &err);
            Z_ASSERT_P(elem, "%pL: %pL", &ref->fullname, &err);

            pkg = mp_iopsq_build_mono_element_pkg(
                t_pool(), iop_env_ctx, elem, NULL, &err
            );
            Z_ASSERT_P(pkg, "%pL: %pL", &ref->fullname, &err);

            Z_HELPER_RUN(
                z_assert_struct_eq(pkg->structs[0], ref),
                "round-trip mismatch for `%pL'", &ref->fullname
            );
        }

        /* A class is extracted too now. */
        Z_ASSERT_P(
            mp_iopsq_elem_from_iop_struct(
                t_pool(), &tstiop__my_class1__s, &err
            ),
            "%pL", &err
        );
    }
    Z_TEST_END;

    Z_TEST(
        iopsq_enum_from_iop, "reverse conversion: iop_enum_t -> iopsq -> "
                             "iop_enum_t round-trip"
    )
    {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        const iop_enum_t *enums[] = {
            &tstiop__test_enum__e,
            &tstiop__my_enum_b__e,
        };

        carray_for_each_entry(ref, enums) {
            iop__package_elem__t *elem;
            iop_pkg_t *pkg;

            elem = mp_iopsq_elem_from_iop_enum(t_pool(), ref);
            pkg = mp_iopsq_build_mono_element_pkg(
                t_pool(), iop_env_ctx, elem, NULL, &err
            );
            Z_ASSERT_P(pkg, "%pL: %pL", &ref->fullname, &err);

            Z_HELPER_RUN(
                z_assert_enum_eq(pkg->enums[0], ref),
                "round-trip mismatch for `%pL'", &ref->fullname
            );
        }
    }
    Z_TEST_END;

    Z_TEST(iopsq_pkg_from_iop, "reverse conversion: whole-package round-trip")
    {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        iop_pkg_t *pkg;
        iop_pkg_t *pkg2;
        iop__package__t *desc;
        int i;

        /* Build a package from IOP², extract it back to IOP², rebuild it and
         * check the two descriptors match element by element (intra-package
         * references must resolve through the short-name path). */
        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "reverse-pkg.yml"));

        desc = mp_iopsq_pkg_from_iop(t_pool(), pkg, &err);
        Z_ASSERT_P(desc, "%pL", &err);

        pkg2 = mp_iopsq_build_pkg(t_pool(), iop_env_ctx, desc, NULL, &err);
        Z_ASSERT_P(pkg2, "%pL", &err);

        for (i = 0; pkg->enums[i] && pkg2->enums[i]; i++) {
            Z_HELPER_RUN(
                z_assert_enum_eq(pkg2->enums[i], pkg->enums[i]),
                "enum #%d mismatch", i
            );
        }
        Z_ASSERT_NULL(pkg->enums[i]);
        Z_ASSERT_NULL(pkg2->enums[i]);

        for (i = 0; pkg->structs[i] && pkg2->structs[i]; i++) {
            Z_HELPER_RUN(
                z_assert_struct_eq(pkg2->structs[i], pkg->structs[i]),
                "struct #%d mismatch", i
            );
        }
        Z_ASSERT_NULL(pkg->structs[i]);
        Z_ASSERT_NULL(pkg2->structs[i]);
    }
    Z_TEST_END;

    Z_TEST(
        iopsq_class_from_iop, "reverse conversion: class hierarchy round-trip"
    )
    {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        iop_pkg_t *pkg;
        iop_pkg_t *pkg2;
        iop__package__t *desc;
        int i;

        /* Build a package containing a class hierarchy (an abstract master
         * with a static field and a private child referencing its parent by
         * name), extract it back to IOP², rebuild it and check the class
         * descriptors (fields, parent, class id, flags and static fields)
         * match. */
        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "class.yml"));

        desc = mp_iopsq_pkg_from_iop(t_pool(), pkg, &err);
        Z_ASSERT_P(desc, "%pL", &err);

        pkg2 = mp_iopsq_build_pkg(t_pool(), iop_env_ctx, desc, NULL, &err);
        Z_ASSERT_P(pkg2, "%pL", &err);

        for (i = 0; pkg->structs[i] && pkg2->structs[i]; i++) {
            Z_ASSERT(iop_struct_is_class(pkg->structs[i]));
            Z_HELPER_RUN(
                z_assert_struct_eq(pkg2->structs[i], pkg->structs[i]),
                "class #%d mismatch", i
            );
        }
        Z_ASSERT_NULL(pkg->structs[i]);
        Z_ASSERT_NULL(pkg2->structs[i]);
    }
    Z_TEST_END;

    Z_TEST(iopsq_typedef_from_iop, "reverse conversion: typedef round-trip") {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        iop_pkg_t *pkg;
        iop_pkg_t *pkg2;
        iop__package__t *desc;
        int i;

        /* Build a package with typedefs aliasing a scalar, a same-package
         * enum and a same-package struct, extract it back to IOP², rebuild it
         * and check the typedef descriptors round-trip (same-package aliases
         * must resolve through the short-name path). */
        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "typedef.yml"));

        desc = mp_iopsq_pkg_from_iop(t_pool(), pkg, &err);
        Z_ASSERT_P(desc, "%pL", &err);

        pkg2 = mp_iopsq_build_pkg(t_pool(), iop_env_ctx, desc, NULL, &err);
        Z_ASSERT_P(pkg2, "%pL", &err);

        Z_ASSERT_P(pkg->typedefs[0], "the fixture should define typedefs");
        for (i = 0; pkg->typedefs[i] && pkg2->typedefs[i]; i++) {
            Z_HELPER_RUN(
                z_assert_typedef_eq(pkg2->typedefs[i], pkg->typedefs[i]),
                "typedef #%d mismatch", i
            );
        }
        Z_ASSERT_NULL(pkg->typedefs[i]);
        Z_ASSERT_NULL(pkg2->typedefs[i]);
    }
    Z_TEST_END;

    Z_TEST(
        iopsq_iface_module_from_iop,
        "reverse conversion: interface and module round-trip"
    )
    {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        iop_pkg_t *pkg;
        iop_pkg_t *pkg2;
        iop__package__t *desc;
        int i;

        /* Build a package with an interface (an RPC with arg/res plus an
         * async void RPC) and a module aliasing it, extract it back to IOP²,
         * rebuild it and check the interface and module descriptors
         * round-trip (the module references its interface through the
         * short-name path). */
        Z_HELPER_RUN(t_package_load(&pkg, iop_env, "iface-module.yml"));

        desc = mp_iopsq_pkg_from_iop(t_pool(), pkg, &err);
        Z_ASSERT_P(desc, "%pL", &err);

        pkg2 = mp_iopsq_build_pkg(t_pool(), iop_env_ctx, desc, NULL, &err);
        Z_ASSERT_P(pkg2, "%pL", &err);

        Z_ASSERT_P(pkg->ifaces[0], "the fixture should define an interface");
        for (i = 0; pkg->ifaces[i] && pkg2->ifaces[i]; i++) {
            Z_HELPER_RUN(
                z_assert_iface_eq(pkg2->ifaces[i], pkg->ifaces[i]),
                "iface #%d mismatch", i
            );
        }
        Z_ASSERT_NULL(pkg->ifaces[i]);
        Z_ASSERT_NULL(pkg2->ifaces[i]);

        Z_ASSERT_P(pkg->mods[0], "the fixture should define a module");
        for (i = 0; pkg->mods[i] && pkg2->mods[i]; i++) {
            Z_HELPER_RUN(
                z_assert_mod_eq(pkg2->mods[i], pkg->mods[i]),
                "module #%d mismatch", i
            );
        }
        Z_ASSERT_NULL(pkg->mods[i]);
        Z_ASSERT_NULL(pkg2->mods[i]);
    }
    Z_TEST_END;

    Z_TEST(
        mp_iopsq_build_struct, "test mp_iopsq_build_struct and "
                               "iop_struct_mp_build"
    )
    {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        iop__package__t *pkg_desc;
        const iop__structure__t *st_desc;
        const iop_struct_t *st;
        iopsq_iop_struct_t st_mp;

        pkg_desc =
            t_load_package_from_file("single-struct.yml", iop_env, &err);
        Z_ASSERT_P(pkg_desc, "%pL", &err);
        Z_ASSERT_EQ(pkg_desc->elems.len, 1);
        st_desc = iop_obj_ccast(iop__structure, pkg_desc->elems.tab[0]);
        st =
            mp_iopsq_build_struct(t_pool(), iop_env_ctx, st_desc, NULL, &err);
        Z_ASSERT_P(st, "%pL", &err);
        Z_HELPER_RUN(
            z_assert_struct_eq(st, &tstiop__tst_build_struct__s),
            "struct mismatch"
        );

        iopsq_iop_struct_init(&st_mp);
        Z_ASSERT_N(
            iopsq_iop_struct_build(&st_mp, iop_env_ctx, st_desc, NULL, &err)
        );
        Z_ASSERT_P(st_mp.st, "%pL", &err);
        Z_HELPER_RUN(
            z_assert_struct_eq(st_mp.st, &tstiop__tst_build_struct__s),
            "struct mismatch"
        );

        iopsq_iop_struct_wipe(&st_mp);
        Z_ASSERT_NULL(st_mp.st);
        Z_ASSERT_NULL(st_mp.mp);
        Z_ASSERT_NULL(st_mp.release_cookie);
    }
    Z_TEST_END;

    Z_TEST(error_misc, "struct error cases miscellaneous") {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
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
            "invalid package `user_package': "
            "cannot load enum `AliasConflict': "
            "the alias `A' is duplicated",
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

        pkg_desc = t_load_package_from_file("error-misc.yml", iop_env, &err);
        Z_ASSERT_P(pkg_desc, "%pL", &err);
        Z_ASSERT_EQ(pkg_desc->elems.len, countof(errors));

        tab_for_each_entry(elem, &pkg_desc->elems) {
            t_scope;

            Z_ASSERT_NULL(
                mp_iopsq_build_mono_element_pkg(
                    t_pool(), iop_env_ctx, elem, NULL, &err
                ),
                "unexpected success for struct %*pS "
                "(expected error: %s)",
                IOP_OBJ_FMT_ARG(elem), *exp_error
            );
            Z_ASSERT_STREQUAL(
                err.data, *exp_error, "unexpected error message"
            );
            exp_error++;
        }
    }
    Z_TEST_END;

    Z_TEST(error_duplicated_name, "duplicated type names") {
        t_scope;
        iop_env_ctx_scope(iop_env, iop_env_ctx);
        SB_1k(err);
        const iop__package__t *pkg_desc;

        pkg_desc = t_load_package_from_file(
            "error-duplicated-name.yml", iop_env, &err
        );
        Z_ASSERT_P(pkg_desc, "%pL", &err);
        Z_ASSERT_NULL(
            mp_iopsq_build_pkg(t_pool(), iop_env_ctx, pkg_desc, NULL, &err),
            "unexpected success"
        );
        Z_ASSERT_STREQUAL(
            err.data, "invalid package `foo': "
                      "already got a thing named `DuplicatedName'"
        );
    }
    Z_TEST_END;

    Z_TEST(iop_type_to_iop, "test function 'iop_type_to_iop'") {
        iop__type__t res;
        struct {
            iop_type_t type;
            iop__int_size__t sz;
            bool is_signed;
        } int_szs_and_signs[] = {
            {IOP_T_I8, INT_SIZE_S8, true},   {IOP_T_U8, INT_SIZE_S8, false},
            {IOP_T_I16, INT_SIZE_S16, true}, {IOP_T_U16, INT_SIZE_S16, false},
            {IOP_T_I32, INT_SIZE_S32, true}, {IOP_T_U32, INT_SIZE_S32, false},
            {IOP_T_I64, INT_SIZE_S64, true}, {IOP_T_U64, INT_SIZE_S64, false},
        };

        carray_for_each_ptr(t, int_szs_and_signs) {
            Z_ASSERT_N(iop_type_to_iop(t->type, &res));
            Z_ASSERT_IOPEQUAL(
                iop__type, &res,
                &IOP_UNION_VA(
                    iop__type, i, .is_signed = t->is_signed, .size = t->sz
                )
            );
        }

        Z_ASSERT_N(iop_type_to_iop(IOP_T_BOOL, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res, &IOP_UNION_VOID(iop__type, b));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_DOUBLE, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res, &IOP_UNION_VOID(iop__type, d));

        Z_ASSERT_N(iop_type_to_iop(IOP_T_STRING, &res));
        Z_ASSERT_IOPEQUAL(
            iop__type, &res, &IOP_UNION(iop__type, s, STRING_TYPE_STRING)
        );

        Z_ASSERT_N(iop_type_to_iop(IOP_T_DATA, &res));
        Z_ASSERT_IOPEQUAL(
            iop__type, &res, &IOP_UNION(iop__type, s, STRING_TYPE_BYTES)
        );

        Z_ASSERT_N(iop_type_to_iop(IOP_T_XML, &res));
        Z_ASSERT_IOPEQUAL(
            iop__type, &res, &IOP_UNION(iop__type, s, STRING_TYPE_XML)
        );

        Z_ASSERT_N(iop_type_to_iop(IOP_T_VOID, &res));
        Z_ASSERT_IOPEQUAL(iop__type, &res, &IOP_UNION_VOID(iop__type, v));

        Z_ASSERT_NEG(iop_type_to_iop(IOP_T_ENUM, &res));
        Z_ASSERT_NEG(iop_type_to_iop(IOP_T_UNION, &res));
        Z_ASSERT_NEG(iop_type_to_iop(IOP_T_STRUCT, &res));
    }
    Z_TEST_END;

    Z_TEST(type_table, "create types using already generated ones") {
        t_scope;
        SB_1k(err);
        iop_env_ctx_scope(iop_env, iop_env_ctx);

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
            {
                "s",
                &IOP_FTYPE_STRING,
            },
            {"stId1", &basic_st_ftype},
            {"enId", &basic_en_ftype},
            {"stTypeName", &tstiop_basic_st_ftype},
            {"stId2", &basic_st_ftype},
            {"enTypeName", &tstiop_basic_en_ftype},
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
        basic_st_desc = mp_iopsq_build_struct(
            t_pool(), iop_env_ctx, &st.super, NULL, &err
        );
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
        en_pkg = mp_iopsq_build_mono_element_pkg(
            t_pool(), iop_env_ctx, &en.super, NULL, &err
        );
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
            iopsq_type_table_fill_type(
                type_table, iop_env_ctx, cfield->type, &field->type
            );
        }

        iop_init(iop__struct, &st);
        st.name = LSTR("TTComplexStruct");
        st.fields = IOP_TYPED_ARRAY_TAB(iop__field, &fields);

        Z_ASSERT_N(
            t_iop_yunpack_ptr_file(
                iop_env_ctx, t_get_path("type-table.yml"), &iop__struct__s,
                (void **)&expected_st, 0, NULL, &err
            ),
            "invalid YAML content: %pL", &err
        );
        Z_ASSERT_IOPEQUAL(iop__struct, &st, expected_st);

        Z_ASSERT_NULL(
            mp_iopsq_build_struct(
                t_pool(), iop_env_ctx, &st.super, NULL, &err
            ),
            "unexpected success (missing type table)"
        );
        st_desc = mp_iopsq_build_struct(
            t_pool(), iop_env_ctx, &st.super, type_table, &err
        );
        Z_ASSERT_P(st_desc, "%pL", &err);

        /* Check that the generated desc matches the one declared in
         * tstiop.iop.
         */
        test_struct(
            iop_env, st_desc, &tstiop__t_t_complex_struct__s,
            "{"
            "\"s\":\"C'est curieux chez les marins "
            "ce besoin de faire des phrases\","
            "\"stId1\":{\"i\":24},"
            "\"enId\":\"B\","
            "\"stTypeName\":{\"i\":42},"
            "\"stId2\":{\"i\":7},"
            "\"enTypeName\":\"D\""
            "}"
        );
    }
    Z_TEST_END;

    Z_TEST(iopsq_int_type_to_int_size) { /* {{{ */
        struct {
            iop_type_t type;
            iopsq__int_size__t size;
        } int_types[] = {
            {
                IOP_T_I8,
                INT_SIZE_S8,
            },
            {
                IOP_T_U8,
                INT_SIZE_S8,
            },
            {
                IOP_T_I16,
                INT_SIZE_S16,
            },
            {
                IOP_T_U16,
                INT_SIZE_S16,
            },
            {
                IOP_T_I32,
                INT_SIZE_S32,
            },
            {
                IOP_T_U32,
                INT_SIZE_S32,
            },
            {
                IOP_T_I64,
                INT_SIZE_S64,
            },
            {
                IOP_T_U64,
                INT_SIZE_S64,
            },
        };

        carray_for_each_ptr(type, int_types) {
            Z_ASSERT_EQ(
                iopsq_int_type_to_int_size(type->type), type->size,
                "wrong size for type %s", iop_type_get_string_desc(type->type)
            );
        }
    }
    Z_TEST_END;
    /* }}} */
    Z_TEST(iopc_check_field_name) { /* {{{ */
        SB_1k(err);

        Z_ASSERT_N(iopc_check_field_name(LSTR("validFieldName"), &err));
        Z_ASSERT_NEG(iopc_check_field_name(LSTR("INVALID_FIELD_NAME"), &err));
    }
    Z_TEST_END;
    /* }}} */

    iop_env_delete(&iop_env);
}
Z_GROUP_END;

/* }}} */

int main(int argc, char **argv)
{
    z_setup(argc, argv);
    z_register_exports(PLATFORM_PATH LIBCOMMON_PATH "tests/iopc/");
    return z_run();
}
