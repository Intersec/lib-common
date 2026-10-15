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
#include "iopc-priv.h"

/* {{{ Global definitions for Rust processing */

typedef struct iopc_rust_decl_t {
    iop_type_t basic_iop_types;
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
    qh_t(lstr) rust_primitive_types;
    qv_t(iopc_rust_decl) decls;
    qm_t(iopc_struct_visited) map;
} iopc_rust_g;
#define _G iopc_rust_g

/* The generated Rust names each item of libcommon by its full path, so
 * that the module that includes it needs no import. */
#define RUST_LC_IOP "::libcommon::iop::"
#define RUST_LC_LSTR "::libcommon::lstr::"
#define RUST_LC_C "::libcommon::bindings::"

#define t_string(x) LSTR(t_camelcase_to_c_str(x))
#define t_cstring(x) t_camelcase_to_c_str(x)

/* The strict and reserved keywords of Rust. `self`, `Self`, `super` and
 * `crate` cannot be raw identifiers, so a name that is a keyword gets a
 * `__var` suffix instead of an `r#` prefix. */
static const lstr_t rust_keywords_g[] = {
    LSTR_IMMED("abstract"), LSTR_IMMED("as"),      LSTR_IMMED("async"),
    LSTR_IMMED("await"),    LSTR_IMMED("become"),  LSTR_IMMED("box"),
    LSTR_IMMED("break"),    LSTR_IMMED("const"),   LSTR_IMMED("continue"),
    LSTR_IMMED("crate"),    LSTR_IMMED("do"),      LSTR_IMMED("dyn"),
    LSTR_IMMED("else"),     LSTR_IMMED("enum"),    LSTR_IMMED("extern"),
    LSTR_IMMED("false"),    LSTR_IMMED("final"),   LSTR_IMMED("fn"),
    LSTR_IMMED("for"),      LSTR_IMMED("gen"),     LSTR_IMMED("if"),
    LSTR_IMMED("impl"),     LSTR_IMMED("in"),      LSTR_IMMED("let"),
    LSTR_IMMED("loop"),     LSTR_IMMED("macro"),   LSTR_IMMED("match"),
    LSTR_IMMED("mod"),      LSTR_IMMED("move"),    LSTR_IMMED("mut"),
    LSTR_IMMED("override"), LSTR_IMMED("priv"),    LSTR_IMMED("pub"),
    LSTR_IMMED("ref"),      LSTR_IMMED("return"),  LSTR_IMMED("self"),
    LSTR_IMMED("Self"),     LSTR_IMMED("static"),  LSTR_IMMED("struct"),
    LSTR_IMMED("super"),    LSTR_IMMED("trait"),   LSTR_IMMED("true"),
    LSTR_IMMED("try"),      LSTR_IMMED("type"),    LSTR_IMMED("typeof"),
    LSTR_IMMED("unsafe"),   LSTR_IMMED("unsized"), LSTR_IMMED("use"),
    LSTR_IMMED("virtual"),  LSTR_IMMED("where"),   LSTR_IMMED("while"),
    LSTR_IMMED("yield"),
};

/* A field keeps the `__var` suffix that it always had when it is named like
 * a primitive type. A union variant does not need it. */
static const lstr_t rust_primitive_types_g[] = {
    LSTR_IMMED("i8"),   LSTR_IMMED("u8"),    LSTR_IMMED("i16"),
    LSTR_IMMED("u16"),  LSTR_IMMED("i32"),   LSTR_IMMED("u32"),
    LSTR_IMMED("i64"),  LSTR_IMMED("u64"),   LSTR_IMMED("i128"),
    LSTR_IMMED("u128"), LSTR_IMMED("bool"),  LSTR_IMMED("f32"),
    LSTR_IMMED("f64"),  LSTR_IMMED("isize"), LSTR_IMMED("usize"),
    LSTR_IMMED("char"),
};

/* }}} */
/* {{{ Helpers functions */
/* {{{ Boolean expressions */

static bool iopc_rust_field_is_ref_like(const iopc_field_t *f)
{
    if (f->kind != IOP_T_STRUCT && f->kind != IOP_T_UNION) {
        return false;
    }
    return f->is_ref || iopc_field_type_is_class(f);
}

static bool iopc_rust_field_is_emitted(const iopc_field_t *f)
{
    return f->kind != IOP_T_VOID || f->repeat == IOP_R_OPTIONAL;
}

static bool iopc_rust_enum_is_strict(const iopc_enum_t *en)
{
    return iopc_attr_check(&en->attrs, IOPC_ATTR_STRICT, NULL) >= 0;
}

/* Whether zero is a valid Rust value of the enum `en`. The Rust enum of a
 * strict enum holds only the values that it names. */
static bool iopc_rust_enum_accepts_zero(const iopc_enum_t *en)
{
    if (!iopc_rust_enum_is_strict(en)) {
        return true;
    }
    tab_for_each_entry(v, &en->values) {
        if (v->value == 0) {
            return true;
        }
    }
    return false;
}

static bool iopc_rust_init_is_valid(const iopc_struct_t *st);

/* Whether the field `f` has no valid initial value in Rust, so that its
 * builder requires it. An `IopOption` holds a value even when it is not
 * set, and an array is empty. */
static bool iopc_rust_field_must_be_set(const iopc_field_t *f)
{
    if (f->kind == IOP_T_ENUM && !iopc_rust_enum_accepts_zero(f->enum_def) &&
        (f->repeat == IOP_R_REQUIRED || f->repeat == IOP_R_OPTIONAL))
    {
        return true;
    }
    return f->repeat == IOP_R_REQUIRED &&
           (iopc_rust_field_is_ref_like(f) || f->kind == IOP_T_UNION ||
            (f->kind == IOP_T_STRUCT &&
             !iopc_rust_init_is_valid(f->struct_def)));
}

/* Whether `iop_init_desc()` gives a valid Rust value of `st`. It zeroes the
 * value, then sets the default values and the `vptr` of a class. A zeroed
 * union selects no variant, and a zeroed reference is null. */
static bool iopc_rust_init_is_valid(const iopc_struct_t *st)
{
    if (st->type == STRUCT_TYPE_UNION) {
        return false;
    }
    if (st->type == STRUCT_TYPE_CLASS && st->extends.len > 0 &&
        !iopc_rust_init_is_valid(st->extends.tab[0]->st))
    {
        return false;
    }
    tab_for_each_entry(f, &st->fields) {
        if (iopc_rust_field_must_be_set(f)) {
            return false;
        }
    }
    return true;
}

static bool iopc_rust_field_is_string(const iopc_field_t *f)
{
    return (
        f->kind == IOP_T_STRING || f->kind == IOP_T_DATA ||
        f->kind == IOP_T_XML
    );
}

static bool iopc_rust_field_is_primitive(const iopc_field_t *f)
{
    return !(
        (f->kind == IOP_T_STRUCT) || (f->kind == IOP_T_UNION) ||
        (f->kind == IOP_T_ENUM)
    );
}

static bool iopc_rust_field_has_struct_def(const iopc_field_t *f)
{
    return (f->kind == IOP_T_STRUCT || f->kind == IOP_T_UNION) &&
           f->struct_def;
}

static bool iopc_rust_struct_needs_lifetime(iopc_struct_t *st)
{
    uint32_t pos;

    pos = qm_put(iopc_struct_visited, &_G.map, st, false, 0);
    if (pos & QHASH_COLLISION) {
        return _G.map.values[pos ^ QHASH_COLLISION];
    }

    iopc_struct_optimize(st);

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        bool needs_lt = false;

        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }
        if (f->repeat == IOP_R_REPEATED || iopc_rust_field_is_string(f)) {
            needs_lt = true;
        } else if (iopc_rust_field_has_struct_def(f)) {
            needs_lt = f->repeat == IOP_R_OPTIONAL ||
                       iopc_rust_field_is_ref_like(f) ||
                       iopc_rust_struct_needs_lifetime(f->struct_def);
        }

        if (needs_lt) {
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

static bool iopc_rust_is_keyword(lstr_t name)
{
    return qh_find(lstr, &_G.rust_keywords, &name) >= 0;
}

/* A class declaring no field of its own is not empty as soon as one of
 * its ancestors declares one: the inherited fields are dumped into it.
 * Only a class walks `extends`: an SNMP object reuses it to name its
 * SNMP root, which resolves to no structure at all. */
static bool iopc_rust_is_struct_empty(const iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (iopc_rust_field_is_emitted(f)) {
            return false;
        }
    }
    if (st->type == STRUCT_TYPE_CLASS && st->extends.len > 0) {
        return iopc_rust_is_struct_empty(st->extends.tab[0]->st);
    }
    return true;
}

static bool iopc_rust_needs_lifetime(iopc_struct_t *st)
{
    return (
        (st->type == STRUCT_TYPE_CLASS)
            ? iopc_rust_class_chain_needs_lifetime(st)
            : iopc_rust_struct_needs_lifetime(st)
    );
}

/* }}} */
/* {{{ String formatting functions */

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
        return LSTR(RUST_LC_LSTR "BorrowedUtf8Lstr<'a>");
    case IOP_T_DATA:
        return LSTR(RUST_LC_LSTR "BorrowedBytesLstr<'a>");
    case IOP_T_STRUCT:
    case IOP_T_UNION:
    case IOP_T_ENUM:
        return f->type_name ? LSTR(f->type_name) : LSTR("/* unknown */");
    case IOP_T_VOID:
    default:
        return LSTR("");
    }
}

static const char *iopc_rust_add_lifetime(iopc_struct_t *st)
{
    return iopc_rust_needs_lifetime(st) ? "<'a>" : "";
}

static int
iopc_rust_decl_cmp(const iopc_rust_decl_t *a, const iopc_rust_decl_t *b)
{
    return CMP(a->lmin, b->lmin);
}

/* The name of the item `name` of `pkg`. Its prefix is the dotted IOP path
 * of `pkg` joined with `__`, like the C backend does, so that a Rust item
 * has the name of its C counterpart. */
static lstr_t t_format_symbol_name(
    const iopc_pkg_t *pkg, const char *name, const char *suffix,
    const char *lt
)
{
    return t_lstr_fmt(
        "%s__%s__%s%s", t_iopc_path_join(pkg->name, "__"), t_cstring(name),
        suffix, lt
    );
}

static lstr_t t_format_type_name(
    const iopc_pkg_t *pkg, const iopc_struct_t *st, const char *type_name,
    const char *lt
)
{
    const char *is_union = (st->type == STRUCT_TYPE_UNION) ? "u" : "t";

    return t_format_symbol_name(pkg, type_name, is_union, lt);
}

/* The `__e`/`__u`/`__t` suffix of the type of the field or typedef `f`. A
 * typedef alias takes the suffix of the type it designates, so that iopc
 * names it like that type. */
static const char *iopc_rust_field_type_suffix(const iopc_field_t *f)
{
    if (f->kind == IOP_T_ENUM) {
        return "e";
    }
    if (f->struct_def && f->struct_def->type == STRUCT_TYPE_UNION) {
        return "u";
    }
    return "t";
}

static lstr_t t_format_type_name_extend(const iopc_field_t *f)
{
    const char *lt =
        f->kind == IOP_T_ENUM ? "" : iopc_rust_add_lifetime(f->struct_def);

    return t_format_symbol_name(
        f->type_pkg, f->type_name, iopc_rust_field_type_suffix(f), lt
    );
}

static lstr_t t_iopc_rust_field_name(const iopc_field_t *f)
{
    lstr_t name = t_string(f->name);

    if (iopc_rust_is_keyword(name) ||
        qh_find(lstr, &_G.rust_primitive_types, &name) >= 0)
    {
        return t_lstr_fmt("%pL__var", &name);
    }
    return name;
}

static lstr_t t_iopc_rust_variant_name(const iopc_field_t *f)
{
    lstr_t name = t_string(f->name);

    if (iopc_rust_is_keyword(name)) {
        return t_lstr_fmt("%pL__var", &name);
    }
    return name;
}

static lstr_t t_iopc_rust_get_type(const iopc_field_t *f)
{
    assert(iopc_rust_field_is_emitted(f));

    return iopc_rust_field_is_primitive(f) ? iopc_rust_get_type(f)
                                           : t_format_type_name_extend(f);
}

/* A Rust type, and whether it borrows the `'a` lifetime. */
typedef struct iopc_rust_type_t {
    lstr_t name;
    bool borrows;
} iopc_rust_type_t;

/* Wraps `type` in what the repeat of `f` adds around it.
 *
 * A field and the alias of a typedef sharing a repeat must land on the
 * same Rust type, or a field declared with that typedef stops being
 * interchangeable with it. Only the mandatory case differs and stays
 * with each caller: a field holds a class through a pointer, which says
 * nothing about the type a typedef names. */
static iopc_rust_type_t t_iopc_rust_repeat_type(
    const iopc_field_t *f, iopc_rust_type_t type, const char *lt
)
{
    /* `repeat` is an `int`: the cast makes the compiler check that the
     * switch handles each repeat. */
    switch ((iop_repeat_t)f->repeat) {
    case IOP_R_REPEATED:
        type.name =
            iopc_field_type_is_class(f)
                ? t_lstr_fmt(
                      RUST_LC_IOP "IopArray<'a, &%smut %pL>", lt, &type.name
                  )
                : t_lstr_fmt(RUST_LC_IOP "IopArray<'a, %pL>", &type.name);
        type.borrows = true;
        break;

    case IOP_R_OPTIONAL:
        if (iopc_rust_field_has_struct_def(f)) {
            type.name = t_lstr_fmt("Option<&%smut %pL>", lt, &type.name);
            type.borrows = true;
        } else if (!iopc_rust_field_is_string(f)) {
            type.name = t_lstr_fmt(RUST_LC_IOP "IopOption<%pL>", &type.name);
        }
        break;

    case IOP_R_REQUIRED:
    case IOP_R_DEFVAL:
        break;
    }

    return type;
}

/* }}} */
/* {{{ Constraint helpers */

/* The Rust type of the field `f` of `st`, and the comment of its repeat. */
static lstr_t t_iopc_rust_field_type(
    iopc_struct_t *st, const iopc_field_t *f, const char **comment_out
)
{
    iopc_rust_type_t type = {.name = t_iopc_rust_get_type(f)};
    const char *lt = iopc_rust_struct_needs_lifetime(st) ? "'a " : "";
    const char *comment = NULL;

    switch ((iop_repeat_t)f->repeat) {
    case IOP_R_REPEATED:
        comment = "repeated";
        break;
    case IOP_R_OPTIONAL:
        comment = "Optional";
        if (f->kind == IOP_T_VOID) {
            type.name = LSTR("bool");
        }
        break;
    case IOP_R_DEFVAL:
        comment = "Default";
        break;
    case IOP_R_REQUIRED:
        if (iopc_rust_field_is_ref_like(f)) {
            type.name = t_lstr_fmt("&%smut %pL", lt, &type.name);
            comment = "ref";
        } else {
            comment = "Mandatory";
        }
        break;
    }
    if (f->kind != IOP_T_VOID) {
        type = t_iopc_rust_repeat_type(f, type, lt);
    }
    if (comment_out) {
        *comment_out = comment;
    }
    return type.name;
}

/* Dumps the field `f` with its type wrapped by its repeat. */
static void iopc_rust_field_per_constraint(
    sb_t *buf, iopc_struct_t *st, const iopc_field_t *f
)
{
    t_scope;
    lstr_t field_name = t_iopc_rust_field_name(f);
    const char *comment;
    lstr_t type = t_iopc_rust_field_type(st, f, &comment);

    sb_addf(buf, "    pub %pL: %pL, // %s\n", &field_name, &type, comment);
}

/* }}} */
/* {{{ Field resolution */

static void iopc_rust_dump_fields(sb_t *buf, iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (st->type == STRUCT_TYPE_UNION) {
            t_scope;
            lstr_t variant_name = t_iopc_rust_variant_name(f);
            lstr_t rust_type;

            /* A void member carries no value, but it is still a choice of
             * the union. */
            if (f->kind == IOP_T_VOID) {
                sb_addf(
                    buf, "    %pL = %d, // Mandatory\n", &variant_name, f->tag
                );
                continue;
            }

            rust_type = t_iopc_rust_get_type(f);
            if (iopc_rust_field_is_ref_like(f)) {
                const char *lt =
                    iopc_rust_struct_needs_lifetime(st) ? "'a " : "";

                rust_type = t_lstr_fmt("&%smut %pL", lt, &rust_type);
            }
            sb_addf(
                buf, "    %pL(%pL) = %d, // Mandatory\n", &variant_name,
                &rust_type, f->tag
            );
            continue;
        }
        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }

        iopc_rust_field_per_constraint(buf, st, f);
    }
    sb_adds(buf, "}\n\n");
}

/* }}} */
/* {{{ CDESC support (including traits) */

static void
iopc_rust_dump_struct_desc(sb_t *buf, const iopc_pkg_t *pkg, const char *name)
{
    t_scope;
    lstr_t desc_name = t_format_symbol_name(pkg, name, "s", "");

    sb_addf(
        buf,
        "unsafe extern \"C\" {\n"
        "    pub static %pL: " RUST_LC_C "iop_struct_t;\n"
        "}\n\n",
        &desc_name
    );
}

static void
iopc_rust_dump_enum_desc(sb_t *buf, const iopc_pkg_t *pkg, const char *name)
{
    t_scope;
    lstr_t enum_name = t_format_symbol_name(pkg, name, "e", "");
    lstr_t desc_name = t_format_symbol_name(pkg, name, "edesc", "");

    sb_addf(
        buf,
        "unsafe extern \"C\" {\n"
        "    #[link_name = \"%pL\"]\n"
        "    pub static %pL: " RUST_LC_C "iop_enum_t;\n"
        "}\n\n",
        &enum_name, &desc_name
    );
}

/* bindgen names the C types of an IOP type by their C names plus `_raw`,
 * because the Rust types can have other names or take a lifetime. Define
 * these names. bindgen knows no lifetime, so the aliases pin it to
 * `'static`. */
static void iopc_rust_dump_raw_alias(
    sb_t *buf, const iopc_pkg_t *pkg, const char *name, const char *suffix,
    lstr_t target
)
{
    t_scope;
    lstr_t c_name = t_format_symbol_name(pkg, name, suffix, "");

    sb_addf(
        buf,
        "#[allow(nonstandard_style)]\n"
        "pub type %pL_raw = %pL;\n",
        &c_name, &target
    );
}

static void iopc_rust_dump_st_raw_aliases(
    sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st
)
{
    t_scope;
    const char *lifetime;
    lstr_t target;
    lstr_t elem;

    lifetime = iopc_rust_needs_lifetime(st) ? "<'static>" : "";
    target = t_format_type_name(pkg, st, st->name, lifetime);
    if (st->type == STRUCT_TYPE_CLASS) {
        /* A C array holds a class through a pointer. */
        elem = t_lstr_fmt("&'static mut %pL", &target);
    } else {
        elem = target;
    }

    iopc_rust_dump_raw_alias(buf, pkg, st->name, "t", target);
    iopc_rust_dump_raw_alias(
        buf, pkg, st->name, "array_t",
        t_lstr_fmt(RUST_LC_IOP "IopArray<'static, %pL>", &elem)
    );
}

/* Dumps the implementation of the marker trait `trait`, which has no
 * item. */
static void
iopc_rust_dump_marker_trait(sb_t *buf, const char *trait, lstr_t type_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "%s for %pL {}\n", trait, &type_name);
}

static void
iopc_rust_dump_enum_trait(sb_t *buf, lstr_t enum_name, lstr_t desc_name)
{
    sb_addf(
        buf,
        "impl " RUST_LC_IOP "Enum for %pL {\n"
        "    fn get_cdesc(&self) -> *const " RUST_LC_C "iop_enum_t {\n"
        "        &raw const %pL\n"
        "    }\n"
        "}\n",
        &enum_name, &desc_name
    );
}

static void iopc_rust_dump_struct_union_trait(
    sb_t *buf, iopc_struct_type_t struct_type, lstr_t struct_name,
    lstr_t desc_name
)
{
    t_scope;
    /* An instance of a class can be one of a child class: only its
     * `vptr` gives its actual descriptor. */
    lstr_t cdesc = struct_type == STRUCT_TYPE_CLASS
                       ? LSTR("self.vptr")
                       : t_lstr_fmt("&raw const %pL", &desc_name);

    sb_addf(
        buf,
        "impl " RUST_LC_IOP "StructUnion for %pL {\n"
        "    fn get_cdesc(&self) -> *const " RUST_LC_C "iop_struct_t {\n"
        "        %pL\n"
        "    }\n"
        "}\n",
        &struct_name, &cdesc
    );
}

static void
iopc_rust_dump_c_enum_trait(sb_t *buf, lstr_t enum_name, lstr_t desc_name)
{
    sb_addf(
        buf,
        "impl " RUST_LC_IOP "CEnum for %pL {\n"
        "    const CDESC: *const " RUST_LC_C "iop_enum_t = &raw const %pL;\n"
        "}\n",
        &enum_name, &desc_name
    );
}

static void iopc_rust_dump_c_struct_union_trait(
    sb_t *buf, lstr_t struct_name, lstr_t desc_name
)
{
    sb_addf(
        buf,
        "impl " RUST_LC_IOP "CStructUnion for %pL {\n"
        "    const CDESC: *const " RUST_LC_C
        "iop_struct_t = &raw const %pL;\n"
        "}\n",
        &struct_name, &desc_name
    );
}

static void iopc_rust_dump_needed_traits_for_structs(
    sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st
)
{
    t_scope;
    const char *lt = iopc_rust_needs_lifetime(st) ? "<'_>" : "";
    lstr_t name = t_format_type_name(pkg, st, st->name, lt);
    lstr_t desc_name = t_format_symbol_name(pkg, st->name, "s", "");

    iopc_rust_dump_marker_trait(buf, "Base", name);
    iopc_rust_dump_struct_union_trait(buf, st->type, name, desc_name);
    iopc_rust_dump_c_struct_union_trait(buf, name, desc_name);

    switch (st->type) {
    case STRUCT_TYPE_CLASS:
    case STRUCT_TYPE_STRUCT: {
        iopc_rust_dump_marker_trait(buf, "Struct", name);
        /* Initializing an abstract class is forbidden, so it only gets the
         * dyn-dispatch marker. */
        if (st->type == STRUCT_TYPE_CLASS && st->is_abstract) {
            break;
        }
        iopc_rust_dump_marker_trait(buf, "CStruct", name);
        /* `CInstance` carries `new()`. Its implementation is `unsafe`,
         * because it promises that the initial value is valid. */
        if (iopc_rust_init_is_valid(st)) {
            sb_addf(
                buf, "unsafe impl " RUST_LC_IOP "CInstance for %pL {}\n",
                &name
            );
        }
        break;
    }
    case STRUCT_TYPE_UNION: {
        iopc_rust_dump_marker_trait(buf, "Union", name);
        iopc_rust_dump_marker_trait(buf, "CUnion", name);
        break;
    }
    default:
        break;
    }
}

static void iopc_rust_dump_needed_traits_for_enums(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_enum_t *en
)
{
    t_scope;
    lstr_t enum_name = t_format_symbol_name(pkg, en->name, "e", "");
    lstr_t desc_name = t_format_symbol_name(pkg, en->name, "edesc", "");

    iopc_rust_dump_marker_trait(buf, "Base", enum_name);
    iopc_rust_dump_enum_trait(buf, enum_name, desc_name);
    iopc_rust_dump_c_enum_trait(buf, enum_name, desc_name);
}

/* }}} */
/* {{{ Attribute support */

static void iopc_rust_dump_type_alias(
    sb_t *buf, const lstr_t *alias, const char *lt, const lstr_t *target
)
{
    sb_addf(
        buf,
        "#[allow(nonstandard_style)]\n"
        "pub type %pL%s = %pL;\n\n",
        alias, lt, target
    );
}

static void
iopc_rust_dump_enum_aliases(sb_t *buf, lstr_t en_name, const iopc_enum_t *en)
{
    SB_1k(consts);

    tab_for_each_entry(v, &en->values) {
        tab_for_each_entry(attr, &v->attrs) {
            if (attr->desc->id != IOPC_ATTR_ALIAS) {
                continue;
            }
            tab_for_each_ptr(arg, &attr->args) {
                sb_addf(
                    &consts, "    pub const %pL: Self = Self::%s;\n",
                    &arg->v.s, v->name
                );
            }
        }
    }
    if (consts.len == 0) {
        return;
    }

    sb_addf(
        buf,
        "#[allow(nonstandard_style)]\n"
        "impl %pL {\n"
        "%*pM"
        "}\n\n",
        &en_name, SB_FMT_ARG(&consts)
    );
}

static void iopc_rust_dump_ctype_aliases(
    sb_t *buf, const qv_t(iopc_attr) *attrs, lstr_t target, const char *lt
)
{
    tab_for_each_entry(attr, attrs) {
        if (attr->desc->id != IOPC_ATTR_CTYPE) {
            continue;
        }
        tab_for_each_ptr(arg, &attr->args) {
            t_scope;
            lstr_t raw = t_lstr_fmt("%pL_raw", &arg->v.s);
            lstr_t raw_target =
                t_lstr_fmt("%pL%s", &arg->v.s, *lt ? "<'static>" : "");

            iopc_rust_dump_type_alias(buf, &arg->v.s, lt, &target);
            /* bindgen names a `@ctype` like any other C name of the type,
             * see iopc_rust_dump_raw_alias(). */
            iopc_rust_dump_type_alias(buf, &raw, "", &raw_target);
        }
    }
}

/* }}} */
/* }}} */
/* {{{ Constants */

/* The C header defines the constants below with `#define`. Their C type is
 * `int`, but a class id fits in a `u16`, which is its type in the
 * descriptors. */
static void
iopc_rust_dump_const(sb_t *buf, lstr_t name, const char *type, int value)
{
    sb_addf(
        buf,
        "#[allow(nonstandard_style)]\n"
        "pub const %pL: %s = %d;\n",
        &name, type, value
    );
}

static void iopc_rust_dump_enum_consts(sb_t *buf, const iopc_enum_t *en)
{
    t_scope;
    lstr_t ns = t_iopc_enum_get_prefix(en);
    int min = INT_MAX;
    int max = INT_MIN;

    tab_for_each_entry(v, &en->values) {
        min = MIN(min, v->value);
        max = MAX(max, v->value);
    }
    iopc_rust_dump_const(
        buf, t_lstr_fmt("%pL_count", &ns), "i32", en->values.len
    );
    iopc_rust_dump_const(buf, t_lstr_fmt("%pL_min", &ns), "i32", min);
    iopc_rust_dump_const(buf, t_lstr_fmt("%pL_max", &ns), "i32", max);
}

/* The class id of `st`, under its C name and under each of its
 * `@ctype`. */
static void iopc_rust_dump_class_ids(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    t_scope;

    iopc_rust_dump_const(
        buf, t_format_symbol_name(pkg, st->name, "class_id", ""), "u16",
        st->class_id
    );
    tab_for_each_entry(attr, &st->attrs) {
        if (attr->desc->id != IOPC_ATTR_CTYPE) {
            continue;
        }
        tab_for_each_ptr(arg, &attr->args) {
            lstr_t pfx = iopc_ctype_get_prefix(arg->v.s);

            iopc_rust_dump_const(
                buf, t_lstr_fmt("%pL__class_id", &pfx), "u16", st->class_id
            );
        }
    }
}

/* }}} */
/* {{{ Builders */

/* The builder of a struct. Each field that has no valid initial value is
 * a type parameter of the builder: `Unset<marker>` until its setter runs,
 * and `Set` after. */
typedef struct iopc_rust_builder_t {
    lstr_t name;
    /* The module of the markers of the fields to set. */
    lstr_t fields_mod;
    const char *lt;
    /* The names of the fields to set. */
    qv_t(lstr) params;
    /* The index of the next field to set, while the setters are dumped. */
    int param;
} iopc_rust_builder_t;

typedef enum iopc_rust_generics_t {
    /* `M0, M1`: one parameter per field to set. */
    IOPC_RUST_GENERICS_PARAMS,
    /* `M0: IsSet, M1: IsSet`. */
    IOPC_RUST_GENERICS_BOUNDS,
    /* `Unset<fields::a>, Unset<fields::b>`. */
    IOPC_RUST_GENERICS_UNSET,
} iopc_rust_generics_t;

/* The generics of the builder, between `<>`, or nothing if it has none.
 * The parameter `set` is `Set`. */
static lstr_t t_iopc_rust_builder_generics(
    const iopc_rust_builder_t *b, iopc_rust_generics_t kind, int set
)
{
    SB_1k(sb);

    if (*b->lt) {
        sb_adds(&sb, "'a, ");
    }
    tab_for_each_pos(i, &b->params) {
        if (i == set) {
            sb_adds(&sb, RUST_LC_IOP "Set, ");
        } else if (kind == IOPC_RUST_GENERICS_UNSET) {
            sb_addf(
                &sb, RUST_LC_IOP "Unset<%pL::%pL>, ", &b->fields_mod,
                &b->params.tab[i]
            );
        } else if (kind == IOPC_RUST_GENERICS_BOUNDS) {
            sb_addf(&sb, "M%d: " RUST_LC_IOP "IsSet, ", i);
        } else {
            sb_addf(&sb, "M%d, ", i);
        }
    }
    if (!sb.len) {
        return LSTR("");
    }
    /* Drop the last ", ". */
    return t_lstr_fmt("<%*pM>", sb.len - 2, sb.data);
}

/* Walks the fields of `st` and of its parent classes. It lists the fields
 * to set if `buf` is NULL, and dumps a setter per field otherwise. */
static void t_iopc_rust_builder_walk(
    sb_t *nullable buf, iopc_rust_builder_t *b, iopc_struct_t *st
)
{
    if (st->type == STRUCT_TYPE_CLASS && st->extends.len > 0) {
        t_iopc_rust_builder_walk(buf, b, st->extends.tab[0]->st);
    }
    iopc_struct_optimize(st);
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        bool must_be_set;
        lstr_t field_name, type, ret;

        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }
        must_be_set = iopc_rust_field_must_be_set(f);
        field_name = t_iopc_rust_field_name(f);
        if (!buf) {
            if (must_be_set) {
                qv_append(&b->params, field_name);
            }
            continue;
        }
        type = t_iopc_rust_field_type(st, f, NULL);
        ret = LSTR("Self");
        if (must_be_set) {
            lstr_t generics = t_iopc_rust_builder_generics(
                b, IOPC_RUST_GENERICS_PARAMS, b->param
            );

            ret = t_lstr_fmt("%pL%pL", &b->name, &generics);
        }
        sb_addf(
            buf,
            "    #[inline]\n"
            "    pub fn %pL(mut self, value: %pL) -> %pL {\n"
            "        unsafe {\n"
            "            (&raw mut (*self.value.as_mut_ptr()).%pL)"
            ".write(value);\n"
            "        }\n",
            &field_name, &type, &ret, &field_name
        );
        if (must_be_set) {
            sb_addf(
                buf,
                "        %pL {\n"
                "            value: self.value,\n"
                "            set: ::core::marker::PhantomData,\n"
                "        }\n",
                &b->name
            );
            b->param++;
        } else {
            sb_adds(buf, "        self\n");
        }
        sb_adds(buf, "    }\n");
    }
}

/* Dumps the builder of the struct or class `st`. `IopBuild` builds the
 * value only when each field that has no valid initial value is set, so
 * a missing field is a compilation error. */
static void iopc_rust_dump_builder(
    sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st, lstr_t st_name,
    const char *lt
)
{
    t_scope;
    iopc_rust_builder_t b = {
        .name = t_format_symbol_name(pkg, st->name, "t__builder", ""),
        .fields_mod = t_format_symbol_name(pkg, st->name, "t__fields", ""),
        .lt = lt,
    };
    lstr_t params, bounds, unset;
    SB_1k(phantom);

    t_qv_init(&b.params, 8);
    t_iopc_rust_builder_walk(NULL, &b, st);
    params = t_iopc_rust_builder_generics(&b, IOPC_RUST_GENERICS_PARAMS, -1);
    bounds = t_iopc_rust_builder_generics(&b, IOPC_RUST_GENERICS_BOUNDS, -1);
    unset = t_iopc_rust_builder_generics(&b, IOPC_RUST_GENERICS_UNSET, -1);

    tab_for_each_pos(i, &b.params) {
        sb_addf(&phantom, "%sM%d", i ? ", " : "", i);
    }
    if (b.params.len == 1) {
        sb_adds(&phantom, ",");
    }

    if (b.params.len) {
        sb_addf(
            buf,
            "#[allow(nonstandard_style)]\n"
            "pub mod %pL {\n",
            &b.fields_mod
        );
        tab_for_each_entry(param, &b.params) {
            sb_addf(buf, "    pub struct %pL;\n", &param);
        }
        sb_adds(buf, "}\n\n");
    }
    sb_addf(
        buf,
        "#[allow(nonstandard_style)]\n"
        "pub struct %pL%pL {\n"
        "    value: ::core::mem::MaybeUninit<%pL>,\n"
        "    set: ::core::marker::PhantomData<(%*pM)>,\n"
        "}\n\n"
        "impl%s %pL {\n"
        "    #[inline]\n"
        "    pub fn builder() -> %pL%pL {\n"
        "        %pL {\n"
        "            value: " RUST_LC_IOP "__iop_init(),\n"
        "            set: ::core::marker::PhantomData,\n"
        "        }\n"
        "    }\n"
        "}\n\n"
        "impl%pL %pL%pL {\n",
        &b.name, &params, &st_name, SB_FMT_ARG(&phantom), lt, &st_name,
        &b.name, &unset, &b.name, &params, &b.name, &params
    );
    t_iopc_rust_builder_walk(buf, &b, st);
    sb_addf(
        buf,
        "}\n\n"
        "impl%pL " RUST_LC_IOP "IopBuild for %pL%pL {\n"
        "    type Output = %pL;\n\n"
        "    #[inline]\n"
        "    fn __iop_build(self) -> %pL {\n"
        "        unsafe { self.value.assume_init() }\n"
        "    }\n"
        "}\n\n",
        &bounds, &b.name, &params, &st_name, &st_name
    );
}

/* }}} */
/* {{{ Structures management */

/* Dumps the aliases of the struct, union or class `st`: the names that
 * bindgen and its `@ctype` give it. */
static void iopc_rust_dump_st_aliases(
    sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st, lstr_t st_name,
    const char *lt
)
{
    iopc_rust_dump_st_raw_aliases(buf, pkg, st);
    iopc_rust_dump_ctype_aliases(buf, &st->attrs, st_name, lt);
    sb_adds(buf, "\n");
}

static void
iopc_rust_dump_struct(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
{
    t_scope;
    const char *lt = iopc_rust_add_lifetime(st);
    lstr_t st_name = t_format_type_name(pkg, st, st->name, lt);
    bool is_empty;

    iopc_struct_optimize(st);
    is_empty = iopc_rust_is_struct_empty(st);
    sb_adds(
        buf, "#[repr(C)]\n"
             "#[allow(nonstandard_style)]\n"
    );
    if (is_empty) {
        sb_addf(buf, "pub struct %pL;\n\n", &st_name);
    } else {
        sb_addf(buf, "pub struct %pL {\n", &st_name);
        iopc_rust_dump_fields(buf, st);
    }
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
    iopc_rust_dump_needed_traits_for_structs(buf, pkg, st);
    if (!is_empty) {
        iopc_rust_dump_builder(buf, pkg, st, st_name, lt);
    }
    iopc_rust_dump_st_aliases(buf, pkg, st, st_name, lt);
}

/* }}} */
/* {{{ Enums management */

/* An unpacker rejects a value that a strict enum does not name, so a Rust
 * enum can hold the value of a strict enum. */
static void iopc_rust_dump_strict_enum_type(
    sb_t *buf, lstr_t en_name, const iopc_enum_t *en
)
{
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

/* A field of a non-strict enum holds any value, and a Rust enum that holds
 * a value it does not name is undefined behavior. The values of the enum
 * are therefore constants of a newtype. */
static void iopc_rust_dump_open_enum_type(
    sb_t *buf, lstr_t en_name, const iopc_enum_t *en
)
{
    sb_addf(
        buf,
        "#[repr(transparent)]\n"
        "#[allow(nonstandard_style)]\n"
        "#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]\n"
        "pub struct %pL(pub i32);\n\n"
        "#[allow(nonstandard_style)]\n"
        "impl %pL {\n",
        &en_name, &en_name
    );
    tab_for_each_entry(v, &en->values) {
        sb_addf(
            buf, "    pub const %s: Self = Self(%d);\n", v->name, v->value
        );
    }
    /* Like the Rust enum of a strict enum, the default is the first
     * value. */
    sb_addf(
        buf,
        "}\n\n"
        "impl Default for %pL {\n"
        "    fn default() -> Self {\n"
        "        Self(%d)\n"
        "    }\n"
        "}\n\n",
        &en_name, en->values.len > 0 ? en->values.tab[0]->value : 0
    );
}

static void
iopc_rust_dump_enum(sb_t *buf, const iopc_pkg_t *pkg, const iopc_enum_t *en)
{
    t_scope;
    lstr_t en_name = t_format_symbol_name(pkg, en->name, "e", "");

    if (iopc_rust_enum_is_strict(en)) {
        iopc_rust_dump_strict_enum_type(buf, en_name, en);
    } else {
        iopc_rust_dump_open_enum_type(buf, en_name, en);
    }
    iopc_rust_dump_enum_desc(buf, pkg, en->name);
    iopc_rust_dump_needed_traits_for_enums(buf, pkg, en);
    iopc_rust_dump_raw_alias(buf, pkg, en->name, "t", en_name);
    iopc_rust_dump_raw_alias(
        buf, pkg, en->name, "array_t",
        t_lstr_fmt(RUST_LC_IOP "IopArray<'static, %pL>", &en_name)
    );
    iopc_rust_dump_raw_alias(
        buf, pkg, en->name, "opt_t",
        t_lstr_fmt(RUST_LC_IOP "IopOption<%pL>", &en_name)
    );
    iopc_rust_dump_enum_aliases(buf, en_name, en);
    iopc_rust_dump_ctype_aliases(buf, &en->attrs, en_name, "");
    iopc_rust_dump_enum_consts(buf, en);
    sb_adds(buf, "\n");
}

/* }}} */
/* {{{ Unions management */

/* The C enum of the tags of the union `st`, under its C name and under each
 * of its `@ctype`. The Rust enum of a union holds the tag as its
 * discriminant, but the C code names the tags through this enum. */
static void iopc_rust_dump_union_tags(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    t_scope;
    lstr_t tag_name = t_format_symbol_name(pkg, st->name, "tag_t", "");
    const char *base = t_fmt(
        "%s__%s", t_iopc_path_join(pkg->name, "__"), t_cstring(st->name)
    );

    sb_addf(
        buf,
        "#[repr(u32)]\n"
        "#[allow(nonstandard_style)]\n"
        "#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq)]\n"
        "pub enum %pL {\n",
        &tag_name
    );
    tab_for_each_entry(f, &st->fields) {
        sb_addf(
            buf, "    %s__%s__ft = %d,\n", base, t_cstring(f->name), f->tag
        );
    }
    sb_adds(buf, "}\n\n");

    tab_for_each_entry(attr, &st->attrs) {
        if (attr->desc->id != IOPC_ATTR_CTYPE) {
            continue;
        }
        tab_for_each_ptr(arg, &attr->args) {
            lstr_t pfx = iopc_ctype_get_prefix(arg->v.s);
            lstr_t alias = t_lstr_fmt("%pL__tag_t", &pfx);

            iopc_rust_dump_type_alias(buf, &alias, "", &tag_name);
        }
    }
}

static void
iopc_rust_dump_union(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
{
    t_scope;
    const char *lt = iopc_rust_add_lifetime(st);
    lstr_t un_name = t_format_type_name(pkg, st, st->name, lt);
    /* `repr(C)` on an enum with only unit variants conflicts with
     * `repr(u32)`. A union of void members only has the layout of its
     * tag anyway. */
    const char *repr = iopc_rust_is_struct_empty(st) ? "u32" : "C, u32";

    sb_addf(
        buf,
        "#[repr(%s)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub enum %pL {\n",
        repr, &un_name
    );
    iopc_struct_optimize(st);
    iopc_rust_dump_fields(buf, st);
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
    iopc_rust_dump_needed_traits_for_structs(buf, pkg, st);
    iopc_rust_dump_union_tags(buf, pkg, st);
    iopc_rust_dump_st_aliases(buf, pkg, st, un_name, lt);
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
            if (!iopc_rust_field_is_emitted(f)) {
                continue;
            }
            iopc_field_compute_size(f);
            *align = MAX(*align, f->align);
            *offset = ROUND_UP(*offset, f->align) + f->size;
            iopc_rust_field_per_constraint(sb, parent, f);
        }

        padded_offset = ROUND_UP(*offset, *align);
        if (padded_offset > *offset) {
            t_scope;

            /* No code may write the padding, so the field is private. The
             * `_` prefix tells rustc that nothing reads it. */
            sb_addf(
                sb, "    _pad_%s_%u: [u8; %u],\n",
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
    const char *lt = iopc_rust_add_lifetime(st);
    lstr_t st_name = t_format_type_name(pkg, st, st->name, lt);
    /* A class starts with its `vptr`. */
    unsigned int offset = sizeof(void *);
    unsigned int align = alignof(void *);
    bool parent_needs_lt;
    lstr_t parent_type, st_name_impl;
    const char *impl_lt;

    if (st->extends.len > 0) {
        const iopc_extends_t *extends = st->extends.tab[0];
        iopc_struct_t *parent = extends->st;

        parent_needs_lt = iopc_rust_needs_lifetime(parent);
        /* The parent may live in another package: name it from its own,
         * not from the one being dumped. */
        parent_type = t_format_type_name(
            extends->pkg ?: pkg, parent, parent->name,
            iopc_rust_add_lifetime(parent)
        );
    } else {
        parent_needs_lt = false;
        parent_type = LSTR("Self");
    }

    impl_lt = parent_needs_lt ? lt : "";
    st_name_impl = (*lt && !parent_needs_lt)
                       ? t_format_type_name(pkg, st, st->name, "<'_>")
                       : st_name;

    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style, clippy::partial_pub_fields)]\n"
        "pub struct %pL {\n"
        "    pub vptr: *const " RUST_LC_C "iop_struct_t,\n\n",
        &st_name
    );
    iopc_rust_inheritance_dump(buf, st, &offset, &align);
    iopc_rust_dump_fields(buf, st);

    /* Trait implementation giving access to the C descriptor. */
    sb_addf(
        buf,
        "impl%s " RUST_LC_IOP "Class for %pL {\n"
        "    type Parent = %pL;\n"
        "}\n",
        impl_lt, &st_name_impl, &parent_type
    );
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
    iopc_rust_dump_needed_traits_for_structs(buf, pkg, st);
    if (!st->is_abstract) {
        iopc_rust_dump_builder(buf, pkg, st, st_name, lt);
    }
    iopc_rust_dump_class_ids(buf, pkg, st);
    iopc_rust_dump_st_aliases(buf, pkg, st, st_name, lt);
}

/* }}} */
/* {{{ Typedefs */

/* The Rust type a typedef aliases, that is the very type a field
 * declared with that typedef gets, so that the two remain
 * interchangeable. */
static iopc_rust_type_t t_iopc_rust_typedef_target(const iopc_field_t *td)
{
    iopc_rust_type_t type = {
        .name = t_iopc_rust_get_type(td),
        .borrows = iopc_rust_field_has_struct_def(td)
                       ? iopc_rust_needs_lifetime(td->struct_def)
                       : iopc_rust_field_is_string(td),
    };

    /* A typedef carries the repeat of its declaration (`typedef Enum1[]
     * Enum1List;`), so the alias carries the wrapper the field gets. */
    type = t_iopc_rust_repeat_type(td, type, "'a ");

    /* Only an explicit `&` makes the alias a reference. */
    if (td->is_ref) {
        type.name = t_lstr_fmt("&'a mut %pL", &type.name);
        type.borrows = true;
    }

    return type;
}

/* The C names of a typedef of a struct, union, class or enum designate the
 * C types of that type: `__t`, `__array_t`, and `__opt_t` for an enum.
 * Each `_raw` alias of the typedef is therefore the one of that type. */
static void iopc_rust_dump_typedef_raw_aliases(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_field_t *td
)
{
    const char *suffixes[] = {"t", "array_t", "opt_t"};
    int nb_suffixes = td->kind == IOP_T_ENUM ? 3 : 2;

    for (int i = 0; i < nb_suffixes; i++) {
        t_scope;
        lstr_t target = t_format_symbol_name(
            td->type_pkg, td->type_name, suffixes[i], ""
        );

        iopc_rust_dump_raw_alias(
            buf, pkg, td->name, suffixes[i], t_lstr_fmt("%pL_raw", &target)
        );
    }
    sb_adds(buf, "\n");
}

static void iopc_rust_dump_typedefs(sb_t *buf, const iopc_pkg_t *pkg)
{
    tab_for_each_entry(td, &pkg->typedefs) {
        t_scope;
        lstr_t alias;
        iopc_rust_type_t type;

        if (td->kind == IOP_T_VOID) {
            /* A void typedef has no type of its own in Rust: a field
             * declared with it becomes a `bool` presence flag when
             * optional, and is not emitted at all otherwise. */
            continue;
        }
        type = t_iopc_rust_typedef_target(td);
        alias = t_format_symbol_name(
            pkg, td->name, iopc_rust_field_type_suffix(td), ""
        );

        iopc_rust_dump_type_alias(
            buf, &alias, type.borrows ? "<'a>" : "", &type.name
        );
        if (td->kind == IOP_T_ENUM || iopc_rust_field_has_struct_def(td)) {
            iopc_rust_dump_typedef_raw_aliases(buf, pkg, td);
        }
    }
}

/* }}} */
/* {{{ Main functions */

static void iopc_init_rust_dump(const iopc_pkg_t *pkg)
{
    iopc_rust_decl_t decl;

    qm_init(iopc_struct_visited, &_G.map);
    qv_init(&_G.decls);
    qh_init(lstr, &_G.rust_keywords);
    carray_for_each_ptr(word, rust_keywords_g) {
        IGNORE(expect(qh_add(lstr, &_G.rust_keywords, word) >= 0));
    }
    qh_init(lstr, &_G.rust_primitive_types);
    carray_for_each_ptr(word, rust_primitive_types_g) {
        IGNORE(expect(qh_add(lstr, &_G.rust_primitive_types, word) >= 0));
    }

    tab_for_each_entry(st, &pkg->structs) {
        decl = (iopc_rust_decl_t){
            .basic_iop_types = IOP_T_STRUCT,
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
    qh_wipe(lstr, &_G.rust_primitive_types);
}

int iopc_do_rust(iopc_pkg_t *pkg)
{
    SB_8k(buf);
    char path[PATH_MAX];

    /* iopc has no output path for Rust: the bindings generator finds the
     * Rust file of a package next to its C header. */
    if (iopc_set_path(NULL, pkg, ".iop.rs", sizeof(path), path, false) < 0) {
        throw_error("cannot build output path for `%s`: %m", pkg->file);
    }
    iopc_init_rust_dump(pkg);
    sb_adds(&buf, "// Auto-generated by iopc - DO NOT EDIT\n");
    sb_addf(&buf, "// IOP package: %s\n", iopc_path_dot(pkg->name));
    iopc_rust_dump_typedefs(&buf, pkg);

    tab_for_each_entry(d, &_G.decls) {
        if (d.basic_iop_types == IOP_T_ENUM) {
            iopc_rust_dump_enum(&buf, pkg, d.en);
            continue;
        }

        if (d.st->type == STRUCT_TYPE_STRUCT) {
            iopc_rust_dump_struct(&buf, pkg, d.st);
        } else if (d.st->type == STRUCT_TYPE_UNION) {
            iopc_rust_dump_union(&buf, pkg, d.st);
        } else if (d.st->type == STRUCT_TYPE_CLASS) {
            iopc_rust_dump_class(&buf, pkg, d.st);
        }
    }

    iopc_rust_wipe_dump();

    return iopc_write_file(&buf, path);
}

/* }}} */
