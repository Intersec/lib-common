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
        iopc_struct_t *st;
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

#define t_string(x) LSTR(t_camelcase_to_c_str(x))
#define t_cstring(x) t_camelcase_to_c_str(x)

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
static bool iopc_rust_struct_needs_lifetime(iopc_struct_t *st)
{
    uint32_t pos;

    pos = qm_put(iopc_struct_visited, &_G.map, st, false, 0);
    if (pos & QHASH_COLLISION) {
        return _G.map.values[pos ^ QHASH_COLLISION];
    }

    iopc_struct_optimize(st);

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        bool needs_lt =
            f->kind == IOP_T_STRING || f->kind == IOP_T_DATA ||
            f->kind == IOP_T_XML ||
            (iopc_rust_field_has_struct_def(f) &&
             (f->repeat == IOP_R_OPTIONAL || iopc_rust_field_is_ref_like(f)));

        if (needs_lt) {
            qm_put(iopc_struct_visited, &_G.map, st, true, QHASH_OVERWRITE);
            return true;
        }
    }

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (iopc_rust_field_has_struct_def(f) &&
            iopc_rust_struct_needs_lifetime(f->struct_def))
        {
            qm_put(iopc_struct_visited, &_G.map, st, true, QHASH_OVERWRITE);
            return true;
        }
    }
    return false;
}

static bool iopc_rust_class_chain_needs_lifetime(iopc_struct_t *st)
{
    if (iopc_rust_struct_needs_lifetime(st)) {
        return true;
    }
    if (st->extends.len > 0) {
        return iopc_rust_class_chain_needs_lifetime(st->extends.tab[0]->st);
    }
    return false;
}

static bool iopc_rust_is_variable_name_valid(const iopc_field_t *f)
{
    t_scope;
    lstr_t field_name = LSTR(t_camelcase_to_c_str(f->name));

    return qh_find(lstr, &_G.rust_keywords, &field_name) < 0;
}

static bool iopc_rust_field_is_emitted(const iopc_field_t *f)
{
    return f->kind != IOP_T_VOID || f->repeat == IOP_R_OPTIONAL;
}

static bool iopc_rust_is_struct_empty(const iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (iopc_rust_field_is_emitted(f)) {
            return false;
        }
    }
    return true;
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
    case IOP_T_STRING:
        return LSTR("BorrowedUtf8Lstr<'a>");
    case IOP_T_DATA:
        return LSTR("BorrowedBytesLstr<'a>");
    case IOP_T_STRUCT:
    case IOP_T_UNION:
    case IOP_T_ENUM:
        return f->type_name ? LSTR(f->type_name) : LSTR("/* unknown */");
    case IOP_T_VOID:
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

static unsigned int iopc_rust_base_alignment(const iopc_struct_t *st)
{
    switch (st->type) {
    case STRUCT_TYPE_CLASS:
        /* A class starts with its vptr. */
        return alignof(void *);
    case STRUCT_TYPE_UNION:
        /* A union starts with its tag, generated in C as an enum and
         * declared in Rust through `#[repr(C, u32)]`. */
        return alignof(uint32_t);
    default:
        return 1;
    }
}

static unsigned int iopc_rust_base_offset(const iopc_struct_t *st)
{
    switch (st->type) {
    case STRUCT_TYPE_CLASS:
        return sizeof(void *);
    case STRUCT_TYPE_UNION:
        return 0;
    default:
        return 0;
    }
}

static lstr_t t_iopc_rust_join(
    const iopc_pkg_t *pkg, const char *type_name, const char *suffix
)
{
    t_SB_1k(buf);

    iopc_path_join(pkg->name, "_", &buf);
    sb_addf(&buf, "__%s__%s", t_cstring(type_name), suffix);
    return LSTR(buf.data);
}

static lstr_t
t_format_type_name(const iopc_pkg_t *pkg, iopc_struct_t *st, lstr_t type_name)
{
    bool needs_lt = (st->type == STRUCT_TYPE_CLASS)
                        ? iopc_rust_class_chain_needs_lifetime(st)
                        : iopc_rust_struct_needs_lifetime(st);
    const char *lt = needs_lt ? "<'a>" : "";
    const char *is_union = (st->type == STRUCT_TYPE_UNION) ? "u" : "t";
    lstr_t mod_name = t_string(pkg->name->dot_path);

    return t_lstr_fmt(
        "%pL__%s__%s%s", &mod_name, t_camelcase_to_c_str(type_name.s),
        is_union, lt
    );
}

static void iopc_rust_defval_dump(
    sb_t *buf, lstr_t field_name, lstr_t rust_type, lstr_t constraint
)
{
    /* In C, a field with a default value (string, integer or double) is
     * still embedded by value (e.g. a plain lstr_t for a string), the
     * default is applied through the iop_struct_t metadata, not through
     * an extra level of indirection. */
    sb_addf(
        buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
        &constraint
    );
}

static void iopc_rust_opt_dump(
    sb_t *buf, const iopc_field_t *f, lstr_t field_name, lstr_t rust_type,
    lstr_t constraint, const char *lt
)
{
    if (f->kind == IOP_T_VOID) {
        sb_addf(
            buf, "    pub void_%pL: bool, // %pL\n", &field_name, &constraint
        );
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
    sb_t *buf, iopc_struct_t *st, const iopc_field_t *f, lstr_t field_name,
    lstr_t rust_type, lstr_t constraint
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
        } else if (f->kind == IOP_T_VOID) {
            /* Unreachable by construction: repeated void fields are
             * rejected by the parser. Kept as a defensive no-op. */
            return;
        } else {
            sb_addf(
                buf, "    pub %pL: IopArray<%pL>, // repeated\n", &field_name,
                &rust_type
            );
        }
    } else if (f->repeat == IOP_R_OPTIONAL) {
        iopc_rust_opt_dump(buf, f, field_name, rust_type, constraint, lt);
    } else if (f->repeat == IOP_R_DEFVAL) {
        iopc_rust_defval_dump(buf, field_name, rust_type, constraint);
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

/* Resolves a field's Rust name, type and constraint comment. Returns
 * false if the field carries no runtime representation (mandatory void)
 * and must be skipped entirely. */
static bool t_iopc_rust_resolve_field(
    const iopc_field_t *f, lstr_t *field_name, lstr_t *rust_type,
    lstr_t *constraint
)
{
    if (!iopc_rust_field_is_emitted(f)) {
        return false;
    }
    *field_name = t_string(f->name);
    *rust_type = iopc_rust_get_type(f);
    *constraint = iopc_rust_dump_constraint(f->repeat);

    if (f->kind == IOP_T_ENUM) {
        *rust_type = t_iopc_rust_join(f->type_pkg, f->type_name, "e");
    } else if (iopc_rust_field_has_struct_def(f)) {
        *rust_type = t_format_type_name(
            f->type_pkg, f->struct_def, LSTR(f->type_name)
        );
    }
    return true;
}

static void iopc_rust_dump_fields(sb_t *buf, iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        t_scope;
        lstr_t field_name, rust_type, constraint;

        if (!t_iopc_rust_resolve_field(
                f, &field_name, &rust_type, &constraint
            ))
        {
            continue;
        }

        if (st->type == STRUCT_TYPE_UNION) {
            /* Classes and explicit `&`-ref fields are always handled
             * through a pointer in the generated C union member, so
             * mirror that with a reference here instead of embedding
             * the value by value. */
            if (iopc_rust_field_is_ref_like(f)) {
                const char *lt =
                    iopc_rust_struct_needs_lifetime(st) ? "'a " : "";

                rust_type = t_lstr_fmt("&%smut %pL", lt, &rust_type);
            }
            sb_addf(
                buf, "    %pL(%pL) = %d, // %pL\n", &field_name, &rust_type,
                f->tag, &constraint
            );
        } else {
            iopc_rust_field_per_constraint(
                buf, st, f, field_name, rust_type, constraint
            );
        }
    }
    sb_adds(buf, "}\n\n");
}

static void
iopc_rust_dump_struct_desc(sb_t *buf, const iopc_pkg_t *pkg, const char *name)
{
    t_scope;
    lstr_t mod_name = t_string(pkg->name->dot_path);

    sb_addf(
        buf,
        "unsafe extern \"C\" {\n"
        "    pub static %pL__%s__s: iop_struct_t;\n"
        "}\n\n",
        &mod_name, t_cstring(name)
    );
}

static void
iopc_rust_dump_enum_desc(sb_t *buf, const iopc_pkg_t *pkg, const char *name)
{
    t_scope;
    lstr_t mod_name = t_string(pkg->name->dot_path);

    sb_addf(
        buf,
        "unsafe extern \"C\" {\n"
        "    #[link_name = \"%pL__%s__e\"]\n"
        "    pub static %pL__%s__edesc: iop_enum_t;\n"
        "}\n\n",
        &mod_name, t_cstring(name), &mod_name, t_cstring(name)
    );
}

/* }}} */
/* {{{ Structures management */

static void
iopc_rust_dump_struct(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
{
    t_scope;
    lstr_t st_name = t_format_type_name(pkg, st, LSTR(st->name));

    iopc_struct_optimize(st);
    if (iopc_rust_is_struct_empty(st)) {
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
        iopc_rust_dump_struct_desc(buf, pkg, st->name);
        return;
    }

    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub struct %pL {\n",
        &st_name
    );
    iopc_rust_dump_fields(buf, st);
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
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
    iopc_rust_dump_enum_desc(buf, pkg, en->name);
}

/* }}} */
/* {{{ Unions management */

static void
iopc_rust_dump_union(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
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
    iopc_struct_optimize(st);
    iopc_rust_dump_fields(buf, st);
}

/* }}} */
/* {{{ Classes management */

static void iopc_rust_inheritance_dump(
    sb_t *sb, iopc_struct_t *st, unsigned int *offset, unsigned int *align
)
{
    if (st->extends.len > 0) {
        iopc_extends_t *val = st->extends.tab[0];
        iopc_struct_t *parent = val->st;
        unsigned int padded_offset;

        iopc_struct_optimize(parent);
        iopc_rust_inheritance_dump(sb, parent, offset, align);
        tab_for_each_entry(f, &parent->fields_in_c_struct_order) {
            t_scope;
            lstr_t field_name, rust_type, constraint;

            if (!t_iopc_rust_resolve_field(
                    f, &field_name, &rust_type, &constraint
                ))
            {
                continue;
            }
            iopc_field_compute_size(f);
            *align = MAX(*align, f->align);
            *offset = ROUND_UP(*offset, f->align) + f->size;
            iopc_rust_field_per_constraint(
                sb, parent, f, field_name, rust_type, constraint
            );
        }

        padded_offset = ROUND_UP(*offset, *align);
        if (padded_offset > *offset) {
            t_scope;
            sb_addf(
                sb, "    pub pad_%s_%u: [u8; %u],\n",
                t_camelcase_to_c_str(parent->name), *offset,
                padded_offset - *offset
            );
        }
        *offset = padded_offset;
    }
    sb_adds(sb, "\n");
}

static void
iopc_rust_dump_class(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
{
    t_scope;
    lstr_t st_name = t_format_type_name(pkg, st, LSTR(st->name));
    unsigned int offset = iopc_rust_base_offset(st);
    unsigned int align = iopc_rust_base_alignment(st);

    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub struct %pL {\n"
        "    pub vptr: *const iop_struct_t,\n\n",
        &st_name
    );
    iopc_rust_inheritance_dump(buf, st, &offset, &align);
    iopc_rust_dump_fields(buf, st);
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
}

/* }}} */
/* {{{ Test generation */

static void iopc_rust_write_inherited_fields_offsets_tests(
    sb_t *buf_tests, const iopc_struct_t *st, lstr_t type_name,
    const char *has_lt, unsigned int *offset, unsigned int *align
)
{
    const iopc_struct_t *parent;

    if (st->extends.len == 0) {
        return;
    }
    parent = st->extends.tab[0]->st;
    iopc_rust_write_inherited_fields_offsets_tests(
        buf_tests, parent, type_name, has_lt, offset, align
    );

    tab_for_each_entry(f, &parent->fields_in_c_struct_order) {
        t_scope;
        lstr_t field_name = t_string(f->name);

        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }
        if (f->kind == IOP_T_VOID) {
            field_name = t_lstr_fmt("void_%pL", &field_name);
        }

        iopc_field_compute_size(f);
        *align = MAX(*align, f->align);
        *offset = ROUND_UP(*offset, f->align);

        sb_addf(
            buf_tests,
            "        assert_eq!(offset_of!(%pL%s,\n"
            "                               %pL), %u);\n",
            &type_name, has_lt, &field_name, *offset
        );
        *offset += f->size;
    }
    *offset = ROUND_UP(*offset, *align);
}

static void iopc_rust_write_tests(
    sb_t *buf_tests, const iopc_pkg_t *pkg, iopc_struct_t *st
)
{
    t_scope;
    const char *suffix = (st->type == STRUCT_TYPE_UNION) ? "u" : "t";
    lstr_t type_name = t_iopc_rust_join(pkg, st->name, suffix);
    bool needs_lt = (st->type == STRUCT_TYPE_CLASS)
                        ? iopc_rust_class_chain_needs_lifetime(st)
                        : iopc_rust_struct_needs_lifetime(st);
    const char *has_lt = needs_lt ? "<'static>" : "";
    /* A class starts with its vptr, which the generated Rust
     * declares as its first field. */
    unsigned int offset = iopc_rust_base_offset(st);
    unsigned int max_size = 0;
    unsigned int align = iopc_rust_base_alignment(st);
    unsigned int size;

    sb_addf(
        buf_tests,
        "    #[test]\n"
        "    #[allow(nonstandard_style, dead_code)]\n"
        "    fn %pL__layout() {\n",
        &type_name
    );

    if (st->type == STRUCT_TYPE_CLASS) {
        iopc_rust_write_inherited_fields_offsets_tests(
            buf_tests, st, type_name, has_lt, &offset, &align
        );
    }

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        t_scope;
        lstr_t field_name = iopc_rust_is_variable_name_valid(f)
                                ? t_string(f->name)
                                : t_lstr_fmt("%s__var", f->name);

        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }
        iopc_field_compute_size(f);
        align = MAX(align, f->align);

        if (st->type == STRUCT_TYPE_UNION) {
            max_size = MAX(max_size, f->size);
            continue;
        }

        offset = ROUND_UP(offset, f->align);
        if (f->kind == IOP_T_VOID) {
            field_name = t_lstr_fmt("void_%pL", &field_name);
        }

        sb_addf(
            buf_tests, "        assert_eq!(offset_of!(%pL%s, %pL), %u);\n",
            &type_name, has_lt, &field_name, offset
        );
        offset += f->size;
    }
    size = (st->type == STRUCT_TYPE_UNION) ? align + ROUND_UP(max_size, align)
                                           : ROUND_UP(offset, align);

    sb_addf(
        buf_tests,
        "        assert_eq!(size_of::<%pL%s>(), %u);\n"
        "        assert_eq!(align_of::<%pL%s>(), %u);\n",
        &type_name, has_lt, size, &type_name, has_lt, align
    );
    sb_adds(buf_tests, "    }\n\n");
}

/* }}} */
/* {{{ Main functions */

static void iopc_init_rust_dump(const iopc_pkg_t *pkg)
{
    iopc_rust_decl_t decl;

    qm_init(iopc_struct_visited, &_G.map);
    qv_init(&_G.decls);
    qh_init(lstr, &_G.rust_keywords);
    carray_for_each_entry(word, rust_keywords_g) {
        IGNORE(expect(qh_add(lstr, &_G.rust_keywords, &word) >= 0));
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
}

static void iopc_rust_wipe_dump(void)
{
    qm_wipe(iopc_struct_visited, &_G.map);
    qv_wipe(&_G.decls);
    qh_wipe(lstr, &_G.rust_keywords);
}

int iopc_do_rust(iopc_pkg_t *pkg, const char *outdir)
{
    SB_8k(buf);
    SB_8k(buf_types);
    SB_1k(buf_tests);
    char path[PATH_MAX];
    bool gen_tests = getenv("IOPC_RUST_GEN_TESTS") != NULL;

    if (iopc_set_path(outdir, pkg, ".iop.rs", sizeof(path), path, true) < 0) {
        throw_error("cannot build output path for `%s`: %m", outdir);
    }
    iopc_init_rust_dump(pkg);

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

    iopc_rust_wipe_dump();
    if (gen_tests) {
        t_scope;
        char tests_path[PATH_MAX];
        lstr_t mod_name = t_string(pkg->name->dot_path);

        sb_prependf(
            &buf_tests,
            "#[cfg(test)]\n"
            "#[rustfmt::skip]\n"
            "pub mod test_%pL {\n"
            "    use crate::%pL_iop_module::*;\n",
            &mod_name, &mod_name
        );
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
