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

enum iopc_rust_gen_runtime_tests { BPACK, JPACK, YPACK };

qvector_t(iopc_rust_decl, iopc_rust_decl_t);
qm_khptr_t(iopc_struct_visited, iopc_struct_t, bool);

static struct {
    qh_t(lstr) rust_keywords;
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

static const lstr_t rust_keywords_g[] = {
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
/* {{{ Boolean expressions */

static bool iopc_rust_field_is_class(const iopc_field_t *f)
{
    if (f->kind != IOP_T_STRUCT && f->kind != IOP_T_UNION) {
        return false;
    }
    return f->struct_def && f->struct_def->type == STRUCT_TYPE_CLASS;
}

static bool iopc_rust_field_is_ref_like(const iopc_field_t *f)
{
    if (f->kind != IOP_T_STRUCT && f->kind != IOP_T_UNION) {
        return false;
    }
    return f->is_ref || iopc_rust_field_is_class(f);
}

static bool iopc_rust_field_is_emitted(const iopc_field_t *f)
{
    return f->kind != IOP_T_VOID || f->repeat == IOP_R_OPTIONAL;
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

static bool iopc_rust_is_variable_name_valid(const iopc_field_t *f)
{
    t_scope;
    lstr_t field_name = LSTR(t_camelcase_to_c_str(f->name));

    return qh_find(lstr, &_G.rust_keywords, &field_name) < 0;
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

static bool iopc_rust_struct_has_constructible_fields(const iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields) {
        if (f->repeat != IOP_R_REQUIRED) {
            continue;
        }
        if (iopc_rust_field_is_ref_like(f) ||
            (f->kind == IOP_T_UNION && !f->is_ref))
        {
            return false;
        }
        if (f->kind == IOP_T_STRUCT && f->struct_def &&
            !iopc_rust_struct_has_constructible_fields(f->struct_def))
        {
            return false;
        }
    }
    if (st->type == STRUCT_TYPE_CLASS && st->extends.len > 0) {
        return iopc_rust_struct_has_constructible_fields(
            st->extends.tab[0]->st
        );
    }
    return true;
}

static bool iopc_rust_field_has_risky_constraint(const iopc_field_t *f)
{
    /* A field carrying one of these constraints cannot be trusted to
     * satisfy it once default-constructed: `new()` does not reliably
     * reflect declared default values for every field (e.g. a `@nonZero`
     * field with a non-zero `= ...` default has still been observed
     * reading back as 0 for inherited class fields), and a plain
     * required field with no default is zero/empty-initialized by C,
     * which violates most of these outright. `@maxLength` alone is not
     * in this list: an empty/zero value always satisfies an upper
     * bound, default or not. */
    const iopc_attr_id_t risky_constraint_attrs[] = {
        IOPC_ATTR_NON_EMPTY, IOPC_ATTR_NON_ZERO,   IOPC_ATTR_MIN,
        IOPC_ATTR_MAX,       IOPC_ATTR_MIN_LENGTH, IOPC_ATTR_LENGTH,
        IOPC_ATTR_PATTERN,
    };

    if (f->repeat == IOP_R_OPTIONAL) {
        /* iop_check_constraints_desc skips unset optional fields, and a
         * freshly default-constructed object always leaves them unset. */
        return false;
    }
    if (f->repeat == IOP_R_REPEATED) {
        /* An empty array vacuously satisfies per-element constraints;
         * only a minimum element count can break it. */
        tab_for_each_entry(attr, &f->attrs) {
            if (attr->desc->id == IOPC_ATTR_MIN_OCCURS) {
                return true;
            }
        }
        return false;
    }
    tab_for_each_entry(attr, &f->attrs) {
        carray_for_each_ptr(id, risky_constraint_attrs) {
            if (*id == attr->desc->id) {
                return true;
            }
        }
    }
    return false;
}

static bool iopc_rust_struct_has_safe_constraints(const iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields) {
        if (iopc_rust_field_has_risky_constraint(f)) {
            return false;
        }
        if (f->repeat == IOP_R_REQUIRED && f->kind == IOP_T_STRUCT &&
            !f->is_ref && f->struct_def &&
            !iopc_rust_struct_has_safe_constraints(f->struct_def))
        {
            return false;
        }
    }

    if (st->type == STRUCT_TYPE_CLASS && st->extends.len > 0) {
        return iopc_rust_struct_has_safe_constraints(st->extends.tab[0]->st);
    }

    return true;
}

/* A `@private` class is not meant to leave the process. The YAML packer
 * asserts on it unconditionally; the binary and JSON ones only do so
 * when the caller asks to skip private classes, which these tests do
 * not. */
static bool iopc_rust_is_private(const iopc_struct_t *st)
{
    tab_for_each_entry(attr, &st->attrs) {
        if (attr->desc->id == IOPC_ATTR_PRIVATE) {
            return true;
        }
    }
    return false;
}

static bool iopc_rust_should_gen_packs_test(const iopc_struct_t *st)
{
    if (st->type == STRUCT_TYPE_UNION ||
        (st->type == STRUCT_TYPE_CLASS && st->is_abstract))
    {
        return false;
    }
    return !iopc_rust_is_struct_empty(st) &&
           iopc_rust_struct_has_safe_constraints(st) &&
           iopc_rust_struct_has_constructible_fields(st);
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

static const char *iopc_rust_add_static_lifetime(iopc_struct_t *st)
{
    return iopc_rust_needs_lifetime(st) ? "<'static>" : "";
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
        return alignof(void *);
    case STRUCT_TYPE_UNION:
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

/* The prefix of the names of `pkg`: its dotted IOP path joined with `__`,
 * like the C backend does, so that a Rust item has the name of its C
 * counterpart. */
static lstr_t t_iopc_rust_mod_name(const iopc_pkg_t *pkg)
{
    SB_1k(buf);

    iopc_path_join(pkg->name, "__", &buf);
    return t_lstr_dup(LSTR_SB_V(&buf));
}

static lstr_t t_iopc_rust_join(
    const iopc_pkg_t *pkg, const char *type_name, const char *suffix
)
{
    lstr_t mod_name = t_iopc_rust_mod_name(pkg);

    return t_lstr_fmt("%pL__%s__%s", &mod_name, t_cstring(type_name), suffix);
}

static lstr_t t_format_symbol_name(
    const iopc_pkg_t *pkg, const char *name, const char *suffix,
    const char *lt
)
{
    lstr_t symbol_name = t_iopc_rust_join(pkg, name, suffix);

    return t_lstr_fmt("%pL%s", &symbol_name, lt);
}

static lstr_t t_format_enum_name(const iopc_pkg_t *pkg, const iopc_enum_t *en)
{
    return t_format_symbol_name(pkg, en->name, "e", "");
}

static lstr_t t_format_type_name(
    const iopc_pkg_t *pkg, const iopc_struct_t *st, const char *type_name,
    const char *lt
)
{
    const char *is_union = (st->type == STRUCT_TYPE_UNION) ? "u" : "t";

    return t_format_symbol_name(pkg, type_name, is_union, lt);
}

static lstr_t t_format_type_name_extend(const iopc_field_t *f)
{
    if (f->kind == IOP_T_ENUM) {
        return t_format_symbol_name(f->type_pkg, f->type_name, "e", "");
    }

    return t_format_type_name(
        f->type_pkg, f->struct_def, f->type_name,
        iopc_rust_add_lifetime(f->struct_def)
    );
}

static lstr_t t_iopc_rust_field_name(const iopc_field_t *f)
{
    return iopc_rust_is_variable_name_valid(f)
               ? t_string(f->name)
               : t_lstr_fmt("%s__var", f->name);
}

static lstr_t t_iopc_rust_get_type(const iopc_field_t *f)
{
    assert(iopc_rust_field_is_emitted(f));

    return iopc_rust_field_is_primitive(f) ? iopc_rust_get_type(f)
                                           : t_format_type_name_extend(f);
}

/* }}} */
/* {{{ Constraint helpers */

static void
iopc_rust_defval_dump(sb_t *buf, const iopc_field_t *f, lstr_t rust_type)
{
    t_scope;
    lstr_t field_name = t_iopc_rust_field_name(f);

    sb_addf(buf, "    pub %pL: %pL, // Default\n", &field_name, &rust_type);
}

static void iopc_rust_opt_dump(
    sb_t *buf, const iopc_field_t *f, lstr_t rust_type, const char *lt
)
{
    t_scope;
    lstr_t field_name = t_iopc_rust_field_name(f);

    if (f->kind == IOP_T_VOID) {
        sb_addf(buf, "    pub %pL: bool, // Optional\n", &field_name);
    } else if (iopc_rust_field_has_struct_def(f)) {
        sb_addf(
            buf, "    pub %pL: Option<&%smut %pL>, // Optional\n",
            &field_name, lt, &rust_type
        );
    } else if (iopc_rust_field_is_string(f)) {
        sb_addf(
            buf, "    pub %pL: %pL, // Optional\n", &field_name, &rust_type
        );
    } else {
        sb_addf(
            buf, "    pub %pL: " RUST_LC_IOP "IopOption<%pL>, // Optional\n",
            &field_name, &rust_type
        );
    }
}

static void iopc_rust_repeat_dump(
    sb_t *buf, const iopc_field_t *f, lstr_t rust_type, const char *lt
)
{
    t_scope;
    lstr_t field_name = t_iopc_rust_field_name(f);

    if (iopc_rust_field_is_class(f)) {
        sb_addf(
            buf,
            "    pub %pL: " RUST_LC_IOP
            "IopArray<'a, &%smut %pL>, // repeated\n",
            &field_name, lt, &rust_type
        );
    } else if (f->kind == IOP_T_VOID) {
        return;
    } else {
        sb_addf(
            buf,
            "    pub %pL: " RUST_LC_IOP "IopArray<'a, %pL>, // repeated\n",
            &field_name, &rust_type
        );
    }
}

static void iopc_rust_field_per_constraint(
    sb_t *buf, iopc_struct_t *st, const iopc_field_t *f
)
{
    t_scope;
    lstr_t field_name = t_iopc_rust_field_name(f);
    lstr_t rust_type = t_iopc_rust_get_type(f);
    const char *lt = iopc_rust_struct_needs_lifetime(st) ? "'a " : "";
    bool ref_like = iopc_rust_field_is_ref_like(f);

    if (f->repeat == IOP_R_REPEATED) {
        iopc_rust_repeat_dump(buf, f, rust_type, lt);
    } else if (f->repeat == IOP_R_OPTIONAL) {
        iopc_rust_opt_dump(buf, f, rust_type, lt);
    } else if (f->repeat == IOP_R_DEFVAL) {
        iopc_rust_defval_dump(buf, f, rust_type);
    } else if (ref_like) {
        sb_addf(
            buf, "    pub %pL: &%smut %pL, // ref\n", &field_name, lt,
            &rust_type
        );
    } else {
        sb_addf(
            buf, "    pub %pL: %pL, // Mandatory\n", &field_name, &rust_type
        );
    }
}

/* }}} */
/* {{{ Field resolution */

static void iopc_rust_dump_fields(sb_t *buf, iopc_struct_t *st)
{
    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }

        if (st->type == STRUCT_TYPE_UNION) {
            /* A variant lives in its own namespace and cannot shadow a
             * type, so it keeps the natural name. */
            t_scope;
            lstr_t variant_name = t_string(f->name);
            lstr_t rust_type = t_iopc_rust_get_type(f);

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

        iopc_rust_field_per_constraint(buf, st, f);
    }
    sb_adds(buf, "}\n\n");
}

/* }}} */
/* {{{ CDESC support (including traits) */

static void iopc_rust_dump_c_instantiable_trait(sb_t *buf, lstr_t type_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "CInstance for %pL {}\n", &type_name);
}

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

static void iopc_rust_dump_base_trait(sb_t *buf, lstr_t type_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "Base for %pL {}\n", &type_name);
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

static void iopc_rust_dump_union_trait(sb_t *buf, lstr_t union_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "Union for %pL {}\n", &union_name);
}

static void iopc_rust_dump_struct_trait(sb_t *buf, lstr_t struct_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "Struct for %pL {}\n", &struct_name);
}

static void iopc_rust_dump_struct_union_trait(
    sb_t *buf, iopc_struct_type_t struct_type, lstr_t struct_name,
    lstr_t desc_name
)
{
    if (struct_type == STRUCT_TYPE_CLASS) {
        sb_addf(
            buf,
            "impl " RUST_LC_IOP "StructUnion for %pL {\n"
            "    fn get_cdesc(&self) -> *const " RUST_LC_C "iop_struct_t {\n"
            "        self.vptr\n"
            "    }\n"
            "}\n",
            &struct_name
        );
        return;
    }
    sb_addf(
        buf,
        "impl " RUST_LC_IOP "StructUnion for %pL {\n"
        "    fn get_cdesc(&self) -> *const " RUST_LC_C "iop_struct_t {\n"
        "        &raw const %pL\n"
        "    }\n"
        "}\n",
        &struct_name, &desc_name
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

static void iopc_rust_dump_c_union_trait(sb_t *buf, lstr_t union_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "CUnion for %pL {}\n", &union_name);
}

static void iopc_rust_dump_c_struct_trait(sb_t *buf, lstr_t struct_name)
{
    sb_addf(buf, "impl " RUST_LC_IOP "CStruct for %pL {}\n", &struct_name);
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

    iopc_rust_dump_base_trait(buf, name);
    iopc_rust_dump_struct_union_trait(buf, st->type, name, desc_name);
    iopc_rust_dump_c_struct_union_trait(buf, name, desc_name);

    switch (st->type) {
    case STRUCT_TYPE_CLASS:
    case STRUCT_TYPE_STRUCT: {
        iopc_rust_dump_struct_trait(buf, name);
        /* `CStruct` carries `new()`, and initializing an abstract class
         * is forbidden, so it only gets the dyn-dispatch marker. */
        if (st->type != STRUCT_TYPE_CLASS || !st->is_abstract) {
            iopc_rust_dump_c_struct_trait(buf, name);
            iopc_rust_dump_c_instantiable_trait(buf, name);
        }
        break;
    }
    case STRUCT_TYPE_UNION: {
        iopc_rust_dump_union_trait(buf, name);
        iopc_rust_dump_c_union_trait(buf, name);
        iopc_rust_dump_c_instantiable_trait(buf, name);
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

    iopc_rust_dump_base_trait(buf, enum_name);
    iopc_rust_dump_enum_trait(buf, enum_name, desc_name);
    iopc_rust_dump_c_enum_trait(buf, enum_name, desc_name);
}

/* }}} */
/* }}} */
/* {{{ Structures management */

static void
iopc_rust_dump_struct(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
{
    t_scope;
    const char *lt = iopc_rust_add_lifetime(st);
    lstr_t st_name = t_format_type_name(pkg, st, st->name, lt);

    iopc_struct_optimize(st);
    if (iopc_rust_is_struct_empty(st)) {
        sb_addf(
            buf,
            "#[repr(C)]\n"
            "#[allow(nonstandard_style)]\n"
            "pub struct %pL;\n\n",
            &st_name
        );
        iopc_rust_dump_struct_desc(buf, pkg, st->name);
        iopc_rust_dump_st_raw_aliases(buf, pkg, st);
        sb_adds(buf, "\n");
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
    iopc_rust_dump_needed_traits_for_structs(buf, pkg, st);
    iopc_rust_dump_st_raw_aliases(buf, pkg, st);
    sb_adds(buf, "\n");
}

/* }}} */
/* {{{ Enums management */

static void
iopc_rust_dump_enum(sb_t *buf, const iopc_pkg_t *pkg, const iopc_enum_t *en)
{
    t_scope;
    lstr_t en_name = t_format_enum_name(pkg, en);

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
    sb_adds(buf, "\n");
}

/* }}} */
/* {{{ Unions management */

static void
iopc_rust_dump_union(sb_t *buf, const iopc_pkg_t *pkg, iopc_struct_t *st)
{
    t_scope;
    lstr_t un_name =
        t_format_type_name(pkg, st, st->name, iopc_rust_add_lifetime(st));

    sb_addf(
        buf,
        "#[repr(C, u32)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub enum %pL {\n",
        &un_name
    );
    iopc_struct_optimize(st);
    iopc_rust_dump_fields(buf, st);
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
    iopc_rust_dump_needed_traits_for_structs(buf, pkg, st);
    iopc_rust_dump_st_raw_aliases(buf, pkg, st);
    sb_adds(buf, "\n");
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
    unsigned int offset = iopc_rust_base_offset(st);
    unsigned int align = iopc_rust_base_alignment(st);
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

    /* Trait implementation for CDESC and runtime testing. */
    sb_addf(
        buf,
        "impl%s " RUST_LC_IOP "Class for %pL {\n"
        "    type Parent = %pL;\n"
        "}\n",
        impl_lt, &st_name_impl, &parent_type
    );
    iopc_rust_dump_struct_desc(buf, pkg, st->name);
    iopc_rust_dump_needed_traits_for_structs(buf, pkg, st);
    iopc_rust_dump_st_raw_aliases(buf, pkg, st);
    sb_adds(buf, "\n");
}

/* }}} */
/* {{{ Test generation */

static void iopc_rust_write_inherited_fields_offsets_tests(
    sb_t *buf_tests, const iopc_struct_t *st, lstr_t type_ref,
    unsigned int *offset, unsigned int *align
)
{
    const iopc_struct_t *parent;

    if (st->extends.len == 0) {
        return;
    }
    parent = st->extends.tab[0]->st;
    iopc_rust_write_inherited_fields_offsets_tests(
        buf_tests, parent, type_ref, offset, align
    );

    tab_for_each_entry(f, &parent->fields_in_c_struct_order) {
        t_scope;
        lstr_t field_name;

        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }
        field_name = t_iopc_rust_field_name(f);
        iopc_field_compute_size(f);
        *align = MAX(*align, f->align);
        *offset = ROUND_UP(*offset, f->align);

        sb_addf(
            buf_tests, "        assert_eq!(offset_of!(%pL, %pL), %u);\n",
            &type_ref, &field_name, *offset
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
    lstr_t type_name = t_format_type_name(pkg, st, st->name, "");
    lstr_t type_ref = t_format_type_name(
        pkg, st, st->name, iopc_rust_add_static_lifetime(st)
    );
    unsigned int offset = iopc_rust_base_offset(st);
    unsigned int align = iopc_rust_base_alignment(st);
    unsigned int max_size = 0;
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
            buf_tests, st, type_ref, &offset, &align
        );
    }

    tab_for_each_entry(f, &st->fields_in_c_struct_order) {
        t_scope;
        lstr_t field_name;

        if (!iopc_rust_field_is_emitted(f)) {
            continue;
        }
        field_name = t_iopc_rust_field_name(f);
        iopc_field_compute_size(f);
        align = MAX(align, f->align);

        if (st->type == STRUCT_TYPE_UNION) {
            max_size = MAX(max_size, f->size);
            continue;
        }

        offset = ROUND_UP(offset, f->align);
        sb_addf(
            buf_tests, "        assert_eq!(offset_of!(%pL, %pL), %u);\n",
            &type_ref, &field_name, offset
        );
        offset += f->size;
    }
    size = (st->type == STRUCT_TYPE_UNION) ? align + ROUND_UP(max_size, align)
                                           : ROUND_UP(offset, align);

    sb_addf(
        buf_tests,
        "        assert_eq!(size_of::<%pL>(), %u);\n"
        "        assert_eq!(align_of::<%pL>(), %u);\n",
        &type_ref, size, &type_ref, align
    );
    sb_adds(buf_tests, "    }\n\n");
}

static void iopc_rust_pack(
    sb_t *buf_tests, lstr_t fn_name, lstr_t st_name, lstr_t upper_type,
    int runtime_test_type
)
{
    lstr_t rust_pack_fn, rust_unpack_fn, rust_test_fn;

    switch (runtime_test_type) {
    case BPACK: {
        rust_test_fn = LSTR("bpack");
        rust_pack_fn = LSTR("bin");
        rust_unpack_fn = LSTR("bunpack");
        break;
    }
    case JPACK: {
        rust_test_fn = LSTR("jpack");
        rust_pack_fn = LSTR("json");
        rust_unpack_fn = LSTR("junpack");
        break;
    }
    case YPACK: {
        rust_test_fn = LSTR("ypack");
        rust_pack_fn = LSTR("yaml");
        rust_unpack_fn = LSTR("yunpack");
        break;
    }
    default:
        e_panic("unexpected pack test type %d", runtime_test_type);
    }

    sb_addf(
        buf_tests,
        "\n    #[test]\n"
        "    #[allow(nonstandard_style, dead_code)]\n"
        "    fn %pL__%pL_roundtrip() {\n"
        "        let t_scope = TScope::new_scope();\n"
        "        let obj = %pL::new();\n"
        "        let %pL1 = obj.as_%pL();\n"
        "        let unpacked = %pL__ENV.t_%pL_desc(&t_scope, "
        "&%pL1, %pL::CDESC, 0)\n"
        "                                    .unwrap_or_else(|e| "
        "panic!(\"%pL "
        "roundtrip failed for %pL: {e}\"));\n"
        "        assert_eq!(%pL1, unpacked.as_%pL());\n"
        "    }\n",
        &fn_name, &rust_test_fn, &st_name, &rust_test_fn, &rust_pack_fn,
        &upper_type, &rust_unpack_fn, &rust_test_fn, &st_name, &rust_test_fn,
        &st_name, &rust_test_fn, &rust_pack_fn
    );
}

static void iopc_rust_write_packs_tests(
    sb_t *buf_tests, const iopc_pkg_t *pkg, iopc_struct_t *st
)
{
    t_scope;
    lstr_t fn_name = t_format_symbol_name(pkg, st->name, "test", "");
    lstr_t st_name = t_format_type_name(pkg, st, st->name, "");
    lstr_t upper_pkg_type = t_lstr_ascii_toupper(t_iopc_rust_mod_name(pkg));

    iopc_rust_pack(buf_tests, fn_name, st_name, upper_pkg_type, BPACK);
    iopc_rust_pack(buf_tests, fn_name, st_name, upper_pkg_type, JPACK);
    if (!iopc_rust_is_private(st)) {
        iopc_rust_pack(buf_tests, fn_name, st_name, upper_pkg_type, YPACK);
    }
}

static void iopc_rust_upcast_downcast_test(
    sb_t *buf_tests, const iopc_pkg_t *pkg, iopc_struct_t *child,
    const iopc_extends_t *extends
)
{
    t_scope;
    lstr_t fn_name = t_format_type_name(pkg, child, child->name, "");
    lstr_t child_type = t_format_type_name(
        pkg, child, child->name, iopc_rust_add_static_lifetime(child)
    );
    lstr_t parent_type = t_format_type_name(
        extends->pkg ?: pkg, extends->st, extends->st->name,
        iopc_rust_add_static_lifetime(extends->st)
    );

    sb_addf(
        buf_tests,
        "\n    #[test]\n"
        "    #[allow(nonstandard_style, dead_code)]\n"
        "    fn %pL__cast_test() {\n"
        "        use ::std::ptr;\n"
        "\n"
        "        let child = %pL::new();\n"
        "        let parent_ref = child.upcast();\n"
        "        assert!(ptr::eq(parent_ref, (&raw const "
        "child).cast::<%pL>()));\n"
        "        assert!(ptr::eq(\n"
        "            parent_ref.downcast::<%pL>().expect(\"downcast should "
        "succeed\"),\n"
        "            &raw const child,\n"
        "        ));\n"
        "    }\n\n",
        &fn_name, &fn_name, &parent_type, &child_type
    );
}

static void iopc_rust_write_constraints_test(
    sb_t *buf_tests, const iopc_pkg_t *pkg, iopc_struct_t *st
)
{
    t_scope;
    lstr_t fn_name = t_format_symbol_name(pkg, st->name, "test", "");
    lstr_t st_name = t_format_type_name(pkg, st, st->name, "");

    sb_addf(
        buf_tests,
        "\n    #[test]\n"
        "    #[allow(nonstandard_style, dead_code)]\n"
        "    fn %pL__constraints() {\n"
        "        let obj = %pL::new();\n"
        "        obj.check_constraints().expect(\"constraints should be "
        "satisfied\");\n"
        "    }\n",
        &fn_name, &st_name
    );
}

/* }}} */
/* {{{ Main functions */

static void
iopc_init_rust_dump(const iopc_pkg_t *pkg, sb_t *buf_tests, bool gen_tests)
{
    iopc_rust_decl_t decl;

    qm_init(iopc_struct_visited, &_G.map);
    qv_init(&_G.decls);
    qh_init(lstr, &_G.rust_keywords);
    carray_for_each_ptr(word, rust_keywords_g) {
        IGNORE(expect(qh_add(lstr, &_G.rust_keywords, word) >= 0));
    }

    if (gen_tests) {
        t_scope;
        lstr_t mod_name = t_iopc_rust_mod_name(pkg);
        lstr_t upper_mod = t_lstr_ascii_toupper(mod_name);

        sb_addf(
            buf_tests,
            "#[cfg(test)]\n"
            "#[rustfmt::skip]\n"
            "pub mod test_%pL {\n"
            "\n\n    unsafe extern \"C\" {\n"
            "        pub static %pL__pkg: iop_pkg_t;\n"
            "    }\n\n"
            "    use crate::%pL_iop_module::*;\n"
            "    pub use crate::bindings::iop_pkg_t;\n"
            "    use libcommon::iop::{Env, EnvCtx};\n"
            "    use libcommon::mem_stack::TScope;\n"
            "    use std::sync::LazyLock;\n\n"
            "    #[allow(dead_code, unused_imports)]\n"
            "    static %pL__ENV: LazyLock<EnvCtx> = LazyLock::new(|| "
            "{\n"
            "        let mut env = Env::new();\n"
            "        env.register_packages(&[&raw const %pL__pkg]);\n"
            "        env.acquire()\n"
            "    });\n\n\n",
            &mod_name, &mod_name, &mod_name, &upper_mod, &mod_name
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
    iopc_init_rust_dump(pkg, &buf_tests, gen_tests);

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
        } else {
            /* No Rust type is emitted for this kind of struct (e.g. SNMP
             * objects, typedefs), so there is nothing to test either. */
            continue;
        }
        if (gen_tests) {
            bool gen_packs_test = iopc_rust_should_gen_packs_test(d.st);

            iopc_rust_write_tests(&buf_tests, pkg, d.st);

            if (gen_packs_test) {
                iopc_rust_write_packs_tests(&buf_tests, pkg, d.st);
                iopc_rust_write_constraints_test(&buf_tests, pkg, d.st);
            }

            if (d.st->extends.len > 0 && gen_packs_test) {
                iopc_rust_upcast_downcast_test(
                    &buf_tests, pkg, d.st, d.st->extends.tab[0]
                );
            }
        }
    }

    sb_adds(&buf, "// Auto-generated by iopc - DO NOT EDIT\n");
    sb_addf(&buf, "// IOP package: %s\n", iopc_path_dot(pkg->name));
    sb_addsb(&buf, &buf_types);

    iopc_rust_wipe_dump();
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
