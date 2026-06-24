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
    union {
        iop_type_t basic_iop_types;
        iopc_struct_type_t iop_st_type;
    };
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
    LSTR_IMMED("fn"),       LSTR_IMMED("do"),       LSTR_IMMED("box"),
    LSTR_IMMED("gen"),      LSTR_IMMED("try"),      LSTR_IMMED("priv"),
    LSTR_IMMED("loop"),     LSTR_IMMED("self"),     LSTR_IMMED("Self"),
    LSTR_IMMED("type"),     LSTR_IMMED("final"),    LSTR_IMMED("macro"),
    LSTR_IMMED("yield"),    LSTR_IMMED("become"),   LSTR_IMMED("virtual"),
    LSTR_IMMED("abstract"), LSTR_IMMED("override"), LSTR_IMMED("i8"),
    LSTR_IMMED("u8"),       LSTR_IMMED("i16"),      LSTR_IMMED("u16"),
    LSTR_IMMED("i32"),      LSTR_IMMED("u32"),      LSTR_IMMED("i64"),
    LSTR_IMMED("u64"),      LSTR_IMMED("i128"),     LSTR_IMMED("u128"),
    LSTR_IMMED("bool"),     LSTR_IMMED("f32"),      LSTR_IMMED("f64"),
    LSTR_IMMED("isize"),    LSTR_IMMED("usize"),    LSTR_IMMED("char"),
};

/* }}} */
/* {{{ Helpers functions */

static bool iopc_rust_struct_needs_lifetime(const iopc_struct_t *st)
{
    int pos;

    THROW_FALSE_IF(!st->resolved);
    pos = qm_find(iopc_struct_visited, &_G.map, (iopc_struct_t *)st);

    if (pos >= 0) {
        return _G.map.values[pos];
    }

    tab_for_each_entry(field, &st->fields) {
        if (field->kind == IOP_T_STRING || field->kind == IOP_T_DATA) {
            qm_add(iopc_struct_visited, &_G.map, (iopc_struct_t *)st, true);
            return true;
        }
        if ((field->kind == IOP_T_STRUCT || field->kind == IOP_T_UNION) &&
            field->struct_def)
        {
            if (iopc_rust_struct_needs_lifetime(field->struct_def)) {
                qm_add(
                    iopc_struct_visited, &_G.map, (iopc_struct_t *)st, true
                );
                return true;
            }
        }
    }
    qm_add(iopc_struct_visited, &_G.map, (iopc_struct_t *)st, false);
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

static bool iopc_rust_type_is_primitive(const iopc_field_t *f)
{
    return !(
        (f->kind == IOP_T_STRUCT) || (f->kind == IOP_T_UNION) ||
        (f->kind == IOP_T_ENUM)
    );
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

static lstr_t t_format_type_name(
    const iopc_pkg_t *pkg, const iopc_struct_t *st, lstr_t type_name
)
{
    t_SB_1k(buf);
    const char *lt = iopc_rust_struct_needs_lifetime(st) ? "<'a>" : "";

    iopc_path_join(pkg->name, "_", &buf);
    sb_addf(
        &buf, "__%s__%s%s", t_camelcase_to_c_str(type_name.s),
        (st->type == STRUCT_TYPE_UNION) ? "u" : "t", lt
    );
    return LSTR(buf.data);
}

static bool iopc_rust_is_typename_valid(const iopc_field_t *f)
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

static void iopc_rust_fields_per_constraint(
    sb_t *buf, const iopc_field_t *f, lstr_t field_name, lstr_t rust_type,
    lstr_t constraint
)
{
    t_scope;

    if (!iopc_rust_is_typename_valid(f)) {
        field_name = t_lstr_fmt("%s__var", f->name);
    }
    if (f->repeat == IOP_R_REPEATED) {
        sb_addf(
            buf,
            "    pub %pL_tab: *mut %pL,\n"
            "    pub %pL_len: i32,\n"
            "    pub %pL_size: i32, // repeated\n",
            &field_name, &rust_type, &field_name, &field_name
        );
    } else if (f->repeat == IOP_R_OPTIONAL) {
        sb_addf(
            buf, "    pub %pL: *mut %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
    } else if (f->repeat == IOP_R_DEFVAL) {
        iopc_rust_defval_dump(buf, f, field_name, rust_type, constraint);
    } else {
        sb_addf(
            buf, "    pub %pL: %pL, // %pL\n", &field_name, &rust_type,
            &constraint
        );
    }
}

/* }}} */
/* {{{ Structures management */

static void iopc_rust_dump_fields(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    tab_for_each_entry(f, &st->fields) {
        t_scope;
        lstr_t rust_type = iopc_rust_get_type(f);
        lstr_t name = LSTR(f->name);
        lstr_t constraint = iopc_rust_dump_constraint(f->repeat);
        lstr_t type_name_lt = LSTR_NULL_V;

        if (!iopc_rust_type_is_primitive(f)) {
            if (f->struct_def) {
                type_name_lt = t_format_type_name(
                    pkg, f->struct_def, LSTR(f->type_name)
                );
            } else {
                type_name_lt = rust_type;
            }

            if (st->type == STRUCT_TYPE_UNION) {
                sb_addf(
                    buf, "    %pL(%pL) = %d, // %pL\n", &name, &type_name_lt,
                    f->tag, &constraint
                );
            }
        } else {
            iopc_rust_fields_per_constraint(
                buf, f, name, rust_type, constraint
            );
        }
    }
    sb_adds(buf, "}\n\n");
}

static void iopc_rust_dump_struct(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    sb_addf(
        buf,
        "#[repr(C)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub struct %s {\n",
        st->name
    );
    iopc_rust_dump_fields(buf, pkg, st);
}

/* }}} */
/* {{{ Enums management */

static void iopc_rust_dump_enum_values(sb_t *buf, const iopc_enum_t *en)
{
    tab_for_each_entry(v, &en->values) {
        sb_addf(buf, "    %s(%s) = %d,\n", v->name, "u32", v->value);
    }
}

static void iopc_rust_dump_enum(sb_t *buf, const iopc_enum_t *en)
{
    sb_addf(buf, "pub enum %s", en->name);
    sb_adds(buf, "{\n");
    iopc_rust_dump_enum_values(buf, en);
    sb_adds(buf, "}");
    sb_adds(buf, "\n\n");
}

/* }}} */
/* {{{ Unions management */

static void iopc_rust_dump_union(
    sb_t *buf, const iopc_pkg_t *pkg, const iopc_struct_t *st
)
{
    sb_addf(
        buf,
        "#[repr(C, u32)]\n"
        "#[allow(nonstandard_style)]\n"
        "pub enum %s {\n",
        st->name
    );
    iopc_rust_dump_fields(buf, pkg, st);
}

/* }}} */
/* {{{ Classes management */

static void iopc_rust_dump_class(sb_t *buf, const iopc_struct_t *st)
{
    sb_addf(buf, "// class test %d\n", 4);
}

/* }}} */
/* {{{ Main function */

int iopc_do_rust(iopc_pkg_t *pkg, const char *outdir)
{
    SB_8k(buf);
    SB_8k(buf_types);
    char path[PATH_MAX];
    iopc_rust_decl_t decl;

    if (iopc_set_path(outdir, pkg, ".iop.rs", sizeof(path), path, true) < 0) {
        throw_error("cannot build output path for `%s`: %m", outdir);
    }

    qm_init(iopc_struct_visited, &_G.map);
    qv_init(&_G.decls);
    qh_init(lstr, &_G.rust_keywords);
    carray_for_each_entry(word, rust_keywords_g) {
        IGNORE(expect(qh_add(lstr, &_G.rust_keywords, &word) >= 0));
    }

    tab_for_each_entry(st, &pkg->structs) {
        decl = (iopc_rust_decl_t){
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
            iopc_rust_dump_enum(&buf_types, d.en);
        } else if (d.iop_st_type == STRUCT_TYPE_STRUCT) {
            iopc_rust_dump_struct(&buf_types, pkg, d.st);
        } else if (d.iop_st_type == STRUCT_TYPE_UNION) {
            iopc_rust_dump_union(&buf_types, pkg, d.st);
        } else if (d.iop_st_type == STRUCT_TYPE_CLASS) {
            iopc_rust_dump_class(&buf, d.st);
        }
    }

    sb_adds(&buf, "// Auto-generated by iopc - DO NOT EDIT\n");
    sb_addsb(&buf, &buf_types);

    qm_wipe(iopc_struct_visited, &_G.map);
    qv_wipe(&_G.decls);
    qh_wipe(lstr, &_G.rust_keywords);
    return iopc_write_file(&buf, path);
}

/* }}} */
