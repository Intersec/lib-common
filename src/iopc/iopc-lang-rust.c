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
#include <lib-common/unix.h>
#include "iopc.h"
#include "iopc-internal.h"

/* {{{ Global definitions for Rust processing */

typedef struct iopc_rust_decl_t {
    iop_type_t basic_iop_types;
    iopc_struct_type_t iop_st_type;
    union {
        const iopc_struct_t *st;
        const iopc_enum_t *en;
    };
    int lmin;
} iopc_rust_decl_t;

qvector_t(iopc_rust_decl, iopc_rust_decl_t);
qm_khptr_t(iopc_struct_visited, iopc_struct_t, bool);

static struct {
    qh_t(lstr) rust_keywords;
    qv_t(iopc_rust_decl) decls;
    qm_t(iopc_struct_visited) map;
} iopc_rust_g;
#define _G iopc_rust_g

static lstr_t rust_keywords_g[] = {
    LSTR_IMMED("as"),      LSTR_IMMED("fn"),       LSTR_IMMED("do"),
    LSTR_IMMED("box"),     LSTR_IMMED("gen"),      LSTR_IMMED("try"),
    LSTR_IMMED("priv"),    LSTR_IMMED("loop"),     LSTR_IMMED("self"),
    LSTR_IMMED("Self"),    LSTR_IMMED("type"),     LSTR_IMMED("final"),
    LSTR_IMMED("macro"),   LSTR_IMMED("yield"),    LSTR_IMMED("become"),
    LSTR_IMMED("virtual"), LSTR_IMMED("abstract"), LSTR_IMMED("override"),
    LSTR_IMMED("i8"),      LSTR_IMMED("u8"),       LSTR_IMMED("i16"),
    LSTR_IMMED("u16"),     LSTR_IMMED("i32"),      LSTR_IMMED("u32"),
    LSTR_IMMED("i64"),     LSTR_IMMED("u64"),      LSTR_IMMED("i128"),
    LSTR_IMMED("u128"),    LSTR_IMMED("bool"),     LSTR_IMMED("f32"),
    LSTR_IMMED("f64"),     LSTR_IMMED("isize"),    LSTR_IMMED("usize"),
    LSTR_IMMED("char"),
};

/* }}} */
/* {{{ Helpers functions */

static bool iopc_rust_field_is_ref_like(const iopc_field_t *f)
{
    if (f->kind != IOP_T_STRUCT && f->kind != IOP_T_UNION) {
        return false;
    }
    return f->is_ref ||
           (f->struct_def && f->struct_def->type == STRUCT_TYPE_CLASS);
}

static bool iopc_rust_field_has_struct_def(const iopc_field_t *f)
{
    return (f->kind == IOP_T_STRUCT || f->kind == IOP_T_UNION) &&
           f->struct_def;
}

/* FIXME: This function doesn't support lifetime addition in some weird cases
 * (like struct repeated field depending on another struct with repeated
 * field). For example this case doesn't produce the right output :
 *
 * struct A {
 *     B[] b_st;
 *     C[] c_st;
 * }
 *
 * struct B {
 *     A[] a_st;
 * }
 * This correction will be part of a later patch !
 */
static bool iopc_rust_struct_needs_lifetime(const iopc_struct_t *st)
{
    uint32_t pos;

    pos = qm_put(iopc_struct_visited, &_G.map, (iopc_struct_t *)st, false, 0);
    if (pos & QHASH_COLLISION) {
        return _G.map.values[pos ^ QHASH_COLLISION];
    }

    iopc_struct_optimize((iopc_struct_t *)st);

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        bool needs_lt =
            f->kind == IOP_T_STRING || f->kind == IOP_T_DATA ||
            f->kind == IOP_T_XML ||
            (iopc_rust_field_has_struct_def(f) &&
             (f->repeat == IOP_R_OPTIONAL || iopc_rust_field_is_ref_like(f)));

        if (needs_lt) {
            qm_put(
                iopc_struct_visited, &_G.map, (iopc_struct_t *)st, true,
                QHASH_OVERWRITE
            );
            return true;
        }
    }

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (iopc_rust_field_has_struct_def(f) &&
            iopc_rust_struct_needs_lifetime(f->struct_def))
        {
            qm_put(
                iopc_struct_visited, &_G.map, (iopc_struct_t *)st, true,
                QHASH_OVERWRITE
            );
            return true;
        }
    }
    return false;
}

static lstr_t iopc_rust_get_type(const iopc_field_t *f)
{
    switch (f->kind) {
    case IOP_T_I8:
        return LSTR("i8");
    case IOP_T_U8:
        return LSTR("u8");
    case IOP_T_I16:
        return LSTR("i16");
    case IOP_T_U16:
        return LSTR("u16");
    case IOP_T_I32:
        return LSTR("i32");
    case IOP_T_U32:
        return LSTR("u32");
    case IOP_T_I64:
        return LSTR("i64");
    case IOP_T_U64:
        return LSTR("u64");
    case IOP_T_BOOL:
        return LSTR("bool");
    case IOP_T_DOUBLE:
        return LSTR("f64");
    case IOP_T_XML:
        /*FALLTHROUGH*/
    case IOP_T_STRING:
        return LSTR("BorrowedUtf8Lstr<'a>");
    case IOP_T_DATA:
        return LSTR("BorrowedBytesLstr<'a>");
    case IOP_T_STRUCT:
        /*FALLTHROUGH*/
    case IOP_T_UNION:
        /*FALLTHROUGH*/
    case IOP_T_ENUM:
        return f->type_name ? LSTR(f->type_name) : LSTR("/* unknown */");
    case IOP_T_VOID:
        /*FALLTHROUGH*/
    default:
        return LSTR("");
    }
}

static lstr_t iopc_rust_dump_constraint(int repeat)
{
    switch (repeat) {
    case IOP_R_REQUIRED:
        return LSTR("mandatory");
    case IOP_R_DEFVAL:
        return LSTR("default");
    case IOP_R_OPTIONAL:
        return LSTR("optional");
    case IOP_R_REPEATED:
        return LSTR("repeated");
    default:
        return LSTR("unknown");
    }
}

static int
iopc_rust_decl_cmp(const iopc_rust_decl_t *a, const iopc_rust_decl_t *b)
{
    return CMP(a->lmin, b->lmin);
}

static lstr_t t_iopc_rust_join(
    const iopc_pkg_t *pkg, const char *type_name, const char *suffix
)
{
    t_SB_1k(buf);

    iopc_path_join(pkg->name, "_", &buf);
    sb_addf(&buf, "__%s__%s", t_camelcase_to_c_str(type_name), suffix);
    return LSTR(buf.data);
}

static lstr_t t_format_type_name(
    const iopc_pkg_t *pkg, const iopc_struct_t *st, lstr_t type_name
)
{
    t_SB_1k(buf);
    const char *lt = iopc_rust_struct_needs_lifetime(st) ? "<'a>" : "";
    const char *is_union = (st->type == STRUCT_TYPE_UNION) ? "u" : "t";

    iopc_path_join(pkg->name, "_", &buf);
    sb_addf(
        &buf, "__%s__%s%s", t_camelcase_to_c_str(type_name.s), is_union, lt
    );
    return LSTR(buf.data);
}

static bool iopc_rust_is_variable_name_valid(const iopc_field_t *f)
{
    t_scope;
    lstr_t field_name = LSTR(t_camelcase_to_c_str(f->name));

    return qh_find(lstr, &_G.rust_keywords, &field_name) < 0;
}

static void iopc_rust_defval_dump(
    sb_t *buf, const iopc_field_t *f, lstr_t field_name, lstr_t rust_type,
    lstr_t constraint
)
{
    switch (f->defval_type) {
    case IOPC_DEFVAL_STRING:
        sb_addf(
            buf, "    pub %pL: *mut %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
        break;
    case IOPC_DEFVAL_INTEGER:
        sb_addf(
            buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
        break;
    case IOPC_DEFVAL_DOUBLE:
        sb_addf(
            buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
        break;
    default:
        sb_addf(
            buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
        break;
    }
}

static void iopc_rust_opt_dump(
    sb_t *buf, const iopc_field_t *f, lstr_t field_name, lstr_t rust_type,
    lstr_t constraint, const char *lt
)
{
    if (f->kind == IOP_T_VOID) {
        sb_addf(buf, "    pub %pL: bool, // %pL\n", &field_name, &constraint);
    } else if (iopc_rust_field_has_struct_def(f)) {
        sb_addf(
            buf, "    pub %pL: Option<&%smut %pL>, // %pL\n", &field_name, lt,
            &rust_type, &constraint
        );
    } else if (
        f->kind == IOP_T_STRING || f->kind == IOP_T_DATA ||
        f->kind == IOP_T_XML
    )
    {
        sb_addf(
            buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
    } else {
        sb_addf(
            buf, "    pub %pL: IopOption<%pL>, // %pL\n", &field_name,
            &rust_type, &constraint
        );
    }
}

static void iopc_rust_field_per_constraint(
    sb_t *buf, const iopc_struct_t *st, const iopc_field_t *f,
    lstr_t field_name, lstr_t rust_type, lstr_t constraint
)
{
    t_scope;
    const char *lt = iopc_rust_struct_needs_lifetime(st) ? "'a " : "";
    bool ref_like = iopc_rust_field_is_ref_like(f);

    if (!iopc_rust_is_variable_name_valid(f)) {
        field_name = t_lstr_fmt("%s__var", f->name);
    }
    if (f->repeat == IOP_R_REPEATED) {
        if (iopc_rust_field_has_struct_def(f) &&
            f->struct_def->type == STRUCT_TYPE_CLASS)
        {
            sb_addf(
                buf, "    pub %pL: IopArray<&%smut %pL>, // repeated\n",
                &field_name, lt, &rust_type
            );
        } else {
            sb_addf(
                buf, "    pub %pL: IopArray<%pL>, // repeated\n", &field_name,
                &rust_type
            );
        }
    } else if (f->repeat == IOP_R_OPTIONAL) {
        iopc_rust_opt_dump(buf, f, field_name, rust_type, constraint, lt);
    } else if (f->repeat == IOP_R_DEFVAL) {
        iopc_rust_defval_dump(buf, f, field_name, rust_type, constraint);
    } else if (
        ref_like && (f->struct_def->type == STRUCT_TYPE_CLASS ||
                     f->struct_def->type == STRUCT_TYPE_UNION ||
                     f->struct_def->type == STRUCT_TYPE_STRUCT)
    )
    {
        sb_addf(
            buf, "    pub %pL: &%smut %pL, // ref\n", &field_name, lt,
            &rust_type
        );
    } else {
        sb_addf(
            buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
    }
}

static void iopc_rust_dump_fields(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        t_scope;
        lstr_t rust_type = iopc_rust_get_type(f);
        lstr_t name = LSTR(f->name);
        lstr_t constraint = iopc_rust_dump_constraint(f->repeat);

        if (f->kind == IOP_T_VOID && f->repeat != IOP_R_OPTIONAL) {
            /* Required void fields are never physically present in the
             * generated C struct, so they must not be emitted here either
             * to keep the #[repr(C)] layout ABI-compatible. */
            continue;
        }
        if (f->kind == IOP_T_ENUM) {
            rust_type = LSTR(t_fmt(
                "%s__%s__e", f->type_pkg->name->dot_path,
                t_camelcase_to_c_str(f->type_name)
            ));
        } else if (iopc_rust_field_has_struct_def(f)) {
            lstr_t type = t_format_type_name(
                f->type_pkg, f->struct_def, LSTR(f->type_name)
            );

            if (st->type != STRUCT_TYPE_UNION) {
                rust_type = type;
            } else if (f->struct_def->type == STRUCT_TYPE_CLASS) {
                /* Classes are always handled through a pointer in the
                 * generated C union member, so mirror that with a
                 * reference here instead of embedding the class by
                 * value. */
                const char *lt =
                    iopc_rust_struct_needs_lifetime(st) ? "'a " : "";

                rust_type = t_lstr_fmt("&%smut %pL", lt, &type);
            } else {
                rust_type = t_lstr_fmt("%pL", &type);
            }
        }

        if (st->type == STRUCT_TYPE_UNION) {
            sb_addf(
                buf, "    %pL(%pL) = %d, // %pL\n", &name, &rust_type, f->tag,
                &constraint
            );
        } else {
            iopc_rust_field_per_constraint(
                buf, st, f, name, rust_type, constraint
            );
        }
    }
    sb_adds(buf, "}\n\n");
}

/* }}} */
/* {{{ Structures management */

static bool iopc_rust_struct_is_empty(const iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (f->kind == IOP_T_VOID && f->repeat != IOP_R_OPTIONAL) {
            continue;
        }
        return false;
    }
    return true;
}

static void iopc_rust_dump_struct(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    t_scope;
    lstr_t st_name = t_format_type_name(pkg, st, LSTR(st->name));

    iopc_struct_optimize((iopc_struct_t *)st);
    if (iopc_rust_struct_is_empty(st)) {
        /* clippy::empty_structs_with_brackets forbids `struct Foo {}`;
         * a struct left with no field once required-void fields are
         * dropped must be declared as a unit struct instead. */
        sb_addf(
            buf,
            "#[repr(C)]\n"
            "#[allow(nonstandard_style)]\n"
            "pub struct %pL;\n\n",
            &st_name
        );
        return;
    }
    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub struct %pL {\n",
        &st_name
    );
    iopc_rust_dump_fields(buf, pkg, st);
}

/* }}} */
/* {{{ Enums management */

static void
iopc_rust_dump_enum(sb_t *buf, const iopc_pkg_t *pkg, const iopc_enum_t *en)
{
    t_scope;
    lstr_t en_name = t_iopc_rust_join(pkg, en->name, "e");

    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style)]\n"
        "#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]\n"
        "pub enum %pL {\n"
        "    #[default]\n",
        &en_name
    );
    tab_for_each_entry(v, &en->values) {
        sb_addf(buf, "    %s = %d,\n", v->name, v->value);
    }
    sb_adds(buf, "}\n\n");
}

/* }}} */
/* {{{ Unions management */

static void iopc_rust_dump_union(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    t_scope;
    lstr_t st_name = t_format_type_name(pkg, st, LSTR(st->name));

    sb_addf(
        buf,
        "#[repr(C, u32)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub enum %pL {\n",
        &st_name
    );
    iopc_struct_optimize((iopc_struct_t *)st);
    iopc_rust_dump_fields(buf, pkg, st);
}

/* }}} */
/* {{{ Classes management */

static void iopc_rust_dump_class(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    t_scope;
    lstr_t st_name = t_format_type_name(pkg, st, LSTR(st->name));

    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub struct %pL {\n"
        "    pub vptr: *mut iop_struct_t,\n",
        &st_name
    );
    iopc_rust_dump_fields(buf, pkg, st);
}

/* }}} */
/* {{{ Test generation */

static void iopc_rust_write_tests(
    sb_t *buf_tests, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    t_scope;
    lstr_t type_name = t_lstr_fmt(
        "%s__%s__%s", pkg->name->dot_path, t_camelcase_to_c_str(st->name),
        (st->type == STRUCT_TYPE_UNION) ? "u" : "t"
    );
    const char *has_lt =
        iopc_rust_struct_needs_lifetime(st) ? "<'static>" : "";
    /* A class starts with its vptr, which the generated Rust
     * declares as its first field. */
    unsigned int offset =
        (st->type == STRUCT_TYPE_CLASS) ? sizeof(void *) : 0;

    sb_addf(
        buf_tests,
        "    #[test]\n"
        "    #[allow(nonstandard_style, dead_code)]\n"
        "    fn %pL__layout() {\n"
        "        assert_eq!(size_of::<%pL%s>(), %u);\n"
        "        assert_eq!(align_of::<%pL%s>(), %u);\n",
        &type_name, &type_name, has_lt, st->size, &type_name, has_lt,
        st->align
    );

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        t_scope;
        lstr_t field_name = iopc_rust_is_variable_name_valid(f)
                                ? LSTR(f->name)
                                : t_lstr_fmt("%s__var", f->name);

        if ((f->kind == IOP_T_VOID && f->repeat == IOP_R_OPTIONAL) ||
            st->type == STRUCT_TYPE_UNION)
        {
            continue;
        }
        iopc_field_compute_size(f);

        offset = ROUND_UP(offset, f->align);
        sb_addf(
            buf_tests, "        assert_eq!(offset_of!(%pL%s, %pL), %u);\n",
            &type_name, has_lt, &field_name, offset
        );
        offset += f->size;
    }
    sb_adds(buf_tests, "    }\n\n");
}

/* }}} */
/* {{{ Main function */

int iopc_do_rust(iopc_pkg_t *pkg, const char *outdir)
{
    SB_8k(buf);
    SB_8k(buf_types);
    SB_1k(buf_tests);
    char path[PATH_MAX];
    iopc_rust_decl_t decl;
    bool gen_tests = getenv("IOPC_RUST_GEN_TESTS") != NULL;

    if (iopc_set_path(outdir, pkg, ".iop.rs", sizeof(path), path, true) < 0) {
        throw_error("cannot build output path for `%s`: %m", outdir);
    }

    qm_init(iopc_struct_visited, &_G.map);
    qv_init(&_G.decls);
    qh_init(lstr, &_G.rust_keywords);
    carray_for_each_entry(word, rust_keywords_g) {
        IGNORE(expect(qh_add(lstr, &_G.rust_keywords, &word) >= 0));
    }

    if (gen_tests) {
        sb_addf(
            &buf_tests,
            "#[cfg(test)]\n"
            "#[rustfmt::skip]\n"
            "pub mod test_%s {\n"
            "    use crate::%s_iop_module::*;\n",
            pkg->name->dot_path, pkg->name->dot_path
        );
    }

    tab_for_each_entry(st, &pkg->structs) {
        decl = (iopc_rust_decl_t){
            .basic_iop_types = IOP_T_STRUCT,
            .iop_st_type = st->type,
            .st = st,
            .lmin = st->loc.lmin,
        };
        qv_append(&_G.decls, decl);
    }
    tab_for_each_entry(en, &pkg->enums) {
        decl = (iopc_rust_decl_t){
            .basic_iop_types = IOP_T_ENUM,
            .en = en,
            .lmin = en->loc.lmin,
        };
        qv_append(&_G.decls, decl);
    }
    qv_qsort(&_G.decls, &iopc_rust_decl_cmp);

    tab_for_each_entry(d, &_G.decls) {
        if (d.basic_iop_types == IOP_T_ENUM) {
            iopc_rust_dump_enum(&buf_types, pkg, d.en);
            continue;
        }

        if (d.iop_st_type == STRUCT_TYPE_STRUCT) {
            iopc_rust_dump_struct(&buf_types, pkg, d.st);
        } else if (d.iop_st_type == STRUCT_TYPE_UNION) {
            iopc_rust_dump_union(&buf_types, pkg, d.st);
        } else if (d.iop_st_type == STRUCT_TYPE_CLASS) {
            iopc_rust_dump_class(&buf_types, pkg, d.st);
        }
        if (gen_tests) {
            iopc_rust_write_tests(&buf_tests, pkg, d.st);
        }
    }

    sb_adds(&buf, "// Auto-generated by iopc - DO NOT EDIT\n");
    sb_addsb(&buf, &buf_types);

    qm_wipe(iopc_struct_visited, &_G.map);
    qv_wipe(&_G.decls);
    qh_wipe(lstr, &_G.rust_keywords);

    if (gen_tests) {
        char tests_path[PATH_MAX];

        sb_adds(&buf_tests, "}\n");
        if (iopc_set_path(
                outdir, pkg, ".iop.tests.rs", sizeof(tests_path), tests_path,
                true
            ) < 0 ||
            iopc_write_file(&buf_tests, tests_path) < 0)
        {
            throw_error(
                "cannot write rust layout tests for `%s`: %m",
                pkg->name->dot_path
            );
        }
    }
    return iopc_write_file(&buf, path);
}

/* }}} */
