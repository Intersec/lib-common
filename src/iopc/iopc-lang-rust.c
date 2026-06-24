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

/* }}} */

/* {{{ Helpers functions */

/**
 * \brief This function is a helper function to get the Rust type
 * corresponding to an IOPC field type. It is used in the
 * iopc_rust_dump_struct_fields function to get the Rust type of each field
 * of a structure (struct, enum, union, class) in the generated Rust file.
 *
 * \param[in] f The IOPC field for which we want to get the Rust type. The
 * IOPC field is represented as a C structure (iopc_field_t)
 *
 * \return This function returns a string corresponding to the Rust type of
 * the IOPC field passed as argument. For example, if the IOPC field is of
 * type IOP_T_I32, this function will return "i32" which is the Rust type.
 */
static const char *iopc_rust_get_type(const iopc_field_t *f)
{
    switch (f->kind) {
    case IOP_T_I8:
        return "i8";
    case IOP_T_U8:
        return "u8";
    case IOP_T_I16:
        return "i16";
    case IOP_T_U16:
        return "u16";
    case IOP_T_I32:
        return "i32";
    case IOP_T_U32:
        return "u32";
    case IOP_T_I64:
        return "i64";
    case IOP_T_U64:
        return "u64";
    case IOP_T_BOOL:
        return "bool";
    case IOP_T_DOUBLE:
        return "f64";
    case IOP_T_STRING:
        return "String";
    case IOP_T_DATA:
        return "Vec<u8>";
    case IOP_T_STRUCT:
    case IOP_T_UNION:
    case IOP_T_ENUM:
        /* For complex types, we use the name of the type */
        return f->type_path ? f->type_name : f->type_path->dot_path;

    default:
        return "/* unknown */";
    }
}

/**
 * \brief This function is a helper function to get the constraint of an IOPC
 * field.
 *
 * \param[in] repeat The constraint of the IOPC field.
 *
 * \return A string corresponding to the constraint of the IOPC field.
 */
static const char *iopc_rust_dump_constraint(int repeat)
{
    switch (repeat) {
    case IOP_R_REQUIRED:
        return "mandatory";
    case IOP_R_DEFVAL:
        return "default";
    case IOP_R_OPTIONAL:
        return "optional";
    case IOP_R_REPEATED:
        return "repeated";
    default:
        return "unknown";
    }
}

static int
iopc_rust_decl_cmp(const iopc_rust_decl_t *a, const iopc_rust_decl_t *b)
{
    return CMP(a->lmin, b->lmin);
}

/* }}} */
/* {{{ Package/import management */

/**
 * \brief Dump all imports needed for the current *.iop file.
 *
 * \param[out] buf The buffer in which the importation will be dumped.
 * \param[in] pkg The IOPC package for which the importation will be dumped.
 *
 * \return This function doesn't return any value.
 */
static void iopc_rust_dump_import(sb_t *buf, const iopc_pkg_t *pkg)
{
    sb_addf(buf, "// package: %s\n", pkg->name->dot_path);
}

/* }}} */
/* {{{ Structures management */

/**
 * \brief Dump the fields of a structure into the Rust file buffer.
 *
 * \param[out] buf The file buffer where the fields will be dumped.
 * \param[in] st The IOPC structure for which the fields will be dumped.
 *
 * \return This function doesn't return any value.
 */
static void iopc_rust_dump_struct_fields(
    sb_t *buf, const iopc_struct_t *st, bool is_union
)
{
    tab_for_each_entry(f, &st->fields) {
        const char *rust_type = iopc_rust_get_type(f);
        const char *constraint = iopc_rust_dump_constraint(f->repeat);
        const char *str = (const char *)f->defval.ptr;

        SB_1k(escaped);

        if (f->repeat == IOP_R_DEFVAL) {
            switch (f->defval_type) {
            case IOPC_DEFVAL_INTEGER:
                if (f->defval_is_signed) {
                    sb_addf(
                        buf, "    pub %s: %s, // default: %ld\n", f->name,
                        rust_type, (int64_t)f->defval.u64
                    );
                } else {
                    sb_addf(
                        buf, "    pub %s: %s, // default: %lu\n", f->name,
                        rust_type, (uint64_t)f->defval.u64
                    );
                }
                break;
            case IOPC_DEFVAL_DOUBLE:
                sb_addf(
                    buf, "    pub %s: %s, // default: %g\n", f->name,
                    rust_type, f->defval.d
                );
                break;
            case IOPC_DEFVAL_STRING:
                sb_add_csvescape(&escaped, ' ', str, strlen(str));
                sb_addf(
                    buf, "    pub %s: %s, // default: \"%*pS\"\n", f->name,
                    rust_type, SB_FMT_ARG(&escaped)
                );
                break;
            default:
                sb_addf(
                    buf, "    pub %s: %s, // default\n", f->name, rust_type
                );
                break;
            }
        } else {
            sb_addf(
                buf, "    pub %s: %s, // %s\n", f->name, rust_type, constraint
            );
        }
    }
}

static void iopc_rust_dump_union(sb_t *buf, const iopc_struct_t *st);

/**
 * \brief Dump a single struct declaration into the Rust file buffer.
 *
 * \param[out] buf The file buffer where the struct will be dumped.
 * \param[in] st The IOPC struct to dump.
 *
 * \return This function doesn't return any value.
 */
static void iopc_rust_dump_struct(sb_t *buf, const iopc_struct_t *st)
{
    if (st->type == STRUCT_TYPE_CLASS) {
        sb_addf(buf, "// class %s;\n", st->name);
    } else if (st->type == STRUCT_TYPE_UNION) {
        iopc_rust_dump_union(buf, st);
    } else {
        sb_addf(buf, "pub struct %s", st->name);
        sb_adds(buf, "{\n");
        iopc_rust_dump_struct_fields(buf, st, true);
        sb_adds(buf, "}");
        sb_adds(buf, "\n\n");
    }
}

/* }}} */
/* {{{ Enums management */

static void iopc_rust_dump_enum_values(sb_t *buf, const iopc_enum_t *en)
{
    tab_for_each_entry(v, &en->values) {
        sb_addf(buf, "    %s(%s) = %d,\n", v->name, "u32", v->value);
    }
}

/**
 * \brief Dump a single enum declaration into the Rust file buffer.
 *
 * \param[out] buf The file buffer where the enum will be dumped.
 * \param[in] en The IOPC enum to dump.
 *
 * \return This function doesn't return any value.
 */
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

static void iopc_rust_dump_union(sb_t *buf, const iopc_struct_t *st)
{
    /* 1. Union declaration for implement it into structure */
    sb_addf(buf, "pub union %s", st->name);
    sb_adds(buf, "{\n");
    iopc_rust_dump_struct_fields(buf, st, true);
    sb_adds(buf, "}");
    sb_adds(buf, "\n\n");

    /* 2. Wrapping structure of union */
    sb_addf(
        buf,
        "pub struct %s {\n"
        "    pub tag: u16,\n    pub data: %s,\n"
        "}",
        st->name, st->name
    );
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
    t_scope;
    SB_8k(buf);
    SB_8k(buf_types);
    char path[PATH_MAX];
    qv_t(iopc_rust_decl) decls;

    t_qv_init(&decls, pkg->structs.len + pkg->enums.len);
    if (iopc_set_path(outdir, pkg, ".iop.rs", sizeof(path), path, true) < 0) {
        throw_error("cannot build output path for `%s`: %m", outdir);
    }

    tab_for_each_entry(st, &pkg->structs) {
        iopc_rust_decl_t d = {
            .iop_st_type = STRUCT_TYPE_STRUCT,
            .st = st,
            .lmin = st->loc.lmin,
        };
        qv_append(&decls, d);
    }
    tab_for_each_entry(en, &pkg->enums) {
        iopc_rust_decl_t d = {
            .basic_iop_types = IOP_T_ENUM,
            .en = en,
            .lmin = en->loc.lmin,
        };
        qv_append(&decls, d);
    }

    qv_qsort(&decls, &iopc_rust_decl_cmp);
    tab_for_each_entry(d, &decls) {
        if (d.iop_st_type == STRUCT_TYPE_STRUCT) {
            iopc_rust_dump_struct(&buf_types, d.st);
        } else if (d.basic_iop_types == IOP_T_ENUM) {
            iopc_rust_dump_enum(&buf_types, d.en);
        } else {
            iopc_rust_dump_class(&buf, d.st);
        }
    }

    sb_adds(&buf, "// Auto-generated by iopc - DO NOT EDIT\n\n");
    iopc_rust_dump_import(&buf, pkg);
    sb_addsb(&buf, &buf_types);

    return iopc_write_file(&buf, path);
}
/* }}} */
