/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

#include <sys/wait.h>

#include <lib-common/log.h>
#include <lib-common/parseopt.h>
#include <lib-common/farch.h>
#include <lib-common/container-qhash.h>
#include <lib-common/unix.h>

#include <clang-c/Index.h>
#include <clang-c/Platform.h>
#include "pxcc.fc.c"

#define PXCC_MAJOR  1
#define PXCC_MINOR  0
#define PXCC_PATCH  1

#define PXCC_EXPORT_FILE_PREFIX    "pxcc_exported_file_"
#define PXCC_EXPORT_TYPE_PREFIX    "pxcc_exported_type_"
#define PXCC_EXPORT_SYMBOL_PREFIX  "pxcc_exported_symbol_"

static lstr_t cython_keywords_to_escape_g[] = {
    LSTR_IMMED("and"),
    LSTR_IMMED("cimport"),
    LSTR_IMMED("class"),
    LSTR_IMMED("def"),
    LSTR_IMMED("del"),
    LSTR_IMMED("elif"),
    LSTR_IMMED("except"),
    LSTR_IMMED("finally"),
    LSTR_IMMED("from"),
    LSTR_IMMED("global"),
    LSTR_IMMED("include"),
    LSTR_IMMED("import"),
    LSTR_IMMED("in"),
    LSTR_IMMED("is"),
    LSTR_IMMED("lambda"),
    LSTR_IMMED("nonlocal"),
    LSTR_IMMED("not"),
    LSTR_IMMED("or"),
    LSTR_IMMED("pass"),
    LSTR_IMMED("raise"),
    LSTR_IMMED("try"),
    LSTR_IMMED("with"),
    LSTR_IMMED("yield"),

/* XXX: The following words are officially keywords for Python, but do not
 * trigger an error with Cython.
 */
#if 0
    LSTR_IMMED("False"),
    LSTR_IMMED("None"),
    LSTR_IMMED("True"),
    LSTR_IMMED("as"),
    LSTR_IMMED("async"),
    LSTR_IMMED("await"),
#endif
};

typedef enum CXChildVisitResult CXChildVisitResult;
typedef enum CXCursorKind CXCursorKind;
typedef enum CXErrorCode CXErrorCode;

typedef enum pxcc_record_name_status_t {
    /** The record name is new. */
    PXCC_RECORD_NAME_NEW,

    /** The record name has been visited one. */
    PXCC_RECORD_NAME_VISITED,

    /** A forwarded record has been generated. */
    PXCC_RECORD_NAME_FORWARDED,

    /** The definition record has been registered. */
    PXCC_RECORD_NAME_COMPLETED,
} pxcc_record_name_status_t;

typedef struct pxcc_record_name_t {
    lstr_t name;
    pxcc_record_name_status_t status;
} pxcc_record_name_t;

static void pxcc_record_name_wipe(pxcc_record_name_t *record_name)
{
    lstr_wipe(&record_name->name);
}

GENERIC_DELETE(pxcc_record_name_t, pxcc_record_name);
GENERIC_NEW_INIT(pxcc_record_name_t, pxcc_record_name);

qm_kvec_t(pxcc_record_name, lstr_t, pxcc_record_name_t *,
          qhash_lstr_hash, qhash_lstr_equal);

typedef enum pxcc_record_kind_t {
    PXCC_RECORD_CANONICAL_TYPE,
    PXCC_RECORD_TYPEDEF,
    PXCC_RECORD_SYMBOL,
    PXCC_RECORD_FORWARD,
} pxcc_record_kind_t;

/* See http://cython.readthedocs.io/en/latest/src/userguide/external_C_code.html#styles-of-struct-union-and-enum-declaration
 */
typedef enum pxcc_typedef_kind_t {
    PXCC_TYPEDEF_TRANSPARENT,
    PXCC_TYPEDEF_DIFFERENT,
    PXCC_TYPEDEF_UNNAMED,
} pxcc_typedef_kind_t;

typedef struct pxcc_record_t {
    lstr_t name;
    lstr_t file;
    CXCursor cursor;
    pxcc_record_kind_t kind : 2;

     /* only set if record is a typedef */
    pxcc_typedef_kind_t typedef_kind : 2;
} pxcc_record_t;

qvector_t(pxcc_record, pxcc_record_t);

typedef struct pxcc_opts_t {
    int help;
    int version;
    int keep_temporary_files;
    const char *output_file;
} pxcc_opts_t;

static bool cxcursor_equal(const qhash_t * nullable qh, const CXCursor *c1,
                           const CXCursor *c2)
{
    return clang_equalCursors(*c1, *c2);
}

static uint32_t cxcursor_hash(const qhash_t * nullable qh, const CXCursor *c)
{
    return clang_hashCursor(*c);
}

qm_kvec_t(cxcursor_name, CXCursor, lstr_t, cxcursor_hash, cxcursor_equal);

static struct {
    pxcc_opts_t opts;
    lstr_t current_file;
    qv_t(lstr) files;
    qm_t(pxcc_record_name) record_names;
    qv_t(pxcc_record) records;
    qm_t(cxcursor_name) anonymous_types;
    bool close_stdout;
    qh_t(lstr) cython_keywords_to_escape;
} pxcc_g = {
#define _G  pxcc_g
    .record_names = QM_INIT(pxcc_record_name, _G.record_names),
    .anonymous_types = QM_INIT(anonymous_types, _G.anonymous_types),
    .cython_keywords_to_escape = QH_INIT(lstr, _G.cython_keywords_to_escape),
};

/* {{{ Helpers */

#define PRINT_ERROR(_cursor, _fmt, ...)                                      \
    do {                                                                     \
        CXSourceLocation _location;                                          \
        CXString _file_name;                                                 \
        unsigned line, column;                                               \
                                                                             \
        _location = clang_getCursorLocation(_cursor);                        \
        clang_getPresumedLocation(_location, &_file_name, &line, &column);   \
        fprintf(stderr, "error while parsing `%s`:%u:%u: " _fmt,             \
                clang_getCString(_file_name), line, column, ##__VA_ARGS__);  \
    } while (0)

#if CINDEX_VERSION >= CINDEX_VERSION_ENCODE(0, 35)
#  define PXCC_HAS_ELABORATED_TYPE
#endif

#if CINDEX_VERSION >= CINDEX_VERSION_ENCODE(0, 63)
#  define PXCC_IS_ELABORATED_TYPE_RESOLVE_UNCONST
#endif

static CXType resolve_unexposed_type(CXType type)
{
    CXType res_type = clang_getResultType(type);

    if (res_type.kind != CXType_Invalid) {
        type.kind = CXType_FunctionProto;
    } else {
        type = clang_getCanonicalType(type);
    }

    return type;
}

static CXType get_underlying_type(CXCursor cursor)
{
    CXType underlying_type = clang_getTypedefDeclUnderlyingType(cursor);

#ifdef PXCC_HAS_ELABORATED_TYPE
    if (underlying_type.kind == CXType_Elaborated) {
        underlying_type = clang_Type_getNamedType(underlying_type);
    }
#endif
    if (underlying_type.kind == CXType_Unexposed) {
        underlying_type = resolve_unexposed_type(underlying_type);
    }

    return underlying_type;
}

static lstr_t get_unconst_type_spelling(CXString spelling)
{
    pstream_t ps = ps_initstr(clang_getCString(spelling));

    ps_skipstr(&ps, "const ");
    return LSTR_PS_V(&ps);
}

static pxcc_typedef_kind_t
get_typedef_kind(CXType type, CXType underlying_type)
{
    CXType canonical_type;
    CXString type_spelling;
    lstr_t type_spelling_str;
    CXString canonical_spelling;
    lstr_t canonical_spelling_str;
    CXString underlying_spelling;
    lstr_t underlying_spelling_str;
    pstream_t ps;
    pxcc_typedef_kind_t kind;

    if (underlying_type.kind != CXType_Record
    &&  underlying_type.kind != CXType_Enum)
    {
        return PXCC_TYPEDEF_DIFFERENT;
    }

    type_spelling = clang_getTypeSpelling(type);
    type_spelling_str = get_unconst_type_spelling(type_spelling);

    canonical_type = clang_getCanonicalType(type);
    canonical_spelling = clang_getTypeSpelling(canonical_type);
    canonical_spelling_str = get_unconst_type_spelling(canonical_spelling);

    /* If type and canonical have the same spelling, it means that the
     * original struct, union or enum is unnamed.
     * Example: typedef struct { ... } foo_t;
     */
    if (lstr_equal(type_spelling_str, canonical_spelling_str)) {
        kind = PXCC_TYPEDEF_UNNAMED;
        goto end;
    }

    underlying_spelling = clang_getTypeSpelling(underlying_type);
    underlying_spelling_str = get_unconst_type_spelling(underlying_spelling);

    /* If type and underlying have the same spelling minus the data type
     * keyword, the typedef is transparent.
     * Example: typedef struct plop_t { ... } plop_t;
     */
    ps = ps_initlstr(&underlying_spelling_str);
    if (expect(ps_skip_afterchr(&ps, ' ') >= 0)
    &&  ps_is_equal(ps, ps_initlstr(&type_spelling_str)))
    {
        kind = PXCC_TYPEDEF_TRANSPARENT;
    } else {
        kind = PXCC_TYPEDEF_DIFFERENT;
    }

    clang_disposeString(underlying_spelling);

  end:
    clang_disposeString(canonical_spelling);
    clang_disposeString(type_spelling);
    return kind;
}

static const char *get_cursor_kind_prefix(CXCursor cursor)
{
    CXCursorKind kind = clang_getCursorKind(cursor);

    switch (kind) {
      case CXCursor_StructDecl:
        return "struct ";

      case CXCursor_UnionDecl:
        return "union ";

      case CXCursor_EnumDecl:
        return "enum ";

      default:
        break;
    }

    PRINT_ERROR(cursor, "unknown cursor type kind: %d", kind);
    assert (false);
    return NULL;
}

static lstr_t get_canonical_record_enum_type_name(lstr_t name)
{
    pstream_t ps = ps_initlstr(&name);

    ps_skip_afterchr(&ps, ' ');
    return LSTR_PS_V(&ps);
}

/* }}} */
/* {{{ Register types and symbols */

static int register_file(CXCursor cursor)
{
    CXString val = clang_getCursorSpelling(cursor);
    lstr_t val_lstr = lstr_dups(clang_getCString(val), -1);

    qv_append(&_G.files, val_lstr);
    _G.current_file = lstr_dupc(val_lstr);

    clang_disposeString(val);
    return 0;
}

static CXChildVisitResult
file_decl_visitor(CXCursor cursor, CXCursor parent, CXClientData data)
{
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_StringLiteral) {
        if (register_file(cursor) < 0) {
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    }
    return CXChildVisit_Recurse;
}

static int check_current_file_is_set(CXCursor cursor)
{
    if (!_G.current_file.s) {
        PRINT_ERROR(cursor, "no export file has been set with "
                    "PXCC_EXPORT_FILE before registering a type or symbol."
                    "\n");
        return -1;
    }
    return 0;
}

static pxcc_record_name_t *get_or_add_new_record_name(lstr_t name)
{
    uint32_t pos;
    pxcc_record_name_t *record_name;

    pos = qm_reserve(pxcc_record_name, &_G.record_names, &name, 0);
    if (pos & QHASH_COLLISION) {
        return _G.record_names.values[pos ^ QHASH_COLLISION];
    }

    record_name = pxcc_record_name_new();
    record_name->name = lstr_dup(name);
    _G.record_names.keys[pos] = record_name->name;
    _G.record_names.values[pos] = record_name;
    return record_name;
}

static pxcc_record_name_t *get_or_add_new_record_name_type(CXType type)
{
    CXString spelling = clang_getTypeSpelling(type);
    lstr_t name = get_unconst_type_spelling(spelling);
    pxcc_record_name_t *record_name;

    record_name = get_or_add_new_record_name(name);
    clang_disposeString(spelling);
    return record_name;
}

static pxcc_record_t *nullable
add_new_record(pxcc_record_name_t *record_name, CXCursor cursor,
               pxcc_record_kind_t kind)
{
    pxcc_record_t *record;

    if (record_name->status == PXCC_RECORD_NAME_COMPLETED) {
        /* No record to add if it is already completed. */
        return NULL;
    }

    record = qv_growlen(&_G.records, 1);
    p_clear(record, 1);
    record->name = lstr_dupc(record_name->name);
    record->file = _G.current_file;
    record->cursor = cursor;
    record->kind = kind;

    record_name->status = PXCC_RECORD_NAME_COMPLETED;

    return record;
}

static lstr_t t_concat_type_stack(const qv_t(cstr) *type_stack,
                                  const char *separator, const char *prefix,
                                  const char *suffix)
{
    t_SB_1k(sb);

    sb_adds(&sb, prefix);
    tab_for_each_pos(pos, type_stack) {
        if (pos > 0) {
            sb_adds(&sb, separator);
        }
        sb_adds(&sb, type_stack->tab[pos]);
    }
    sb_adds(&sb, suffix);
    return LSTR_SB_V(&sb);
}

static int t_register_type(CXType type, qv_t(cstr) *type_stack);

static int t_register_type_cursor(CXCursor cursor, qv_t(cstr) *type_stack)
{
    CXString name = clang_getCursorSpelling(cursor);
    const char *cstr_name = clang_getCString(name);
    int res;

    qv_append(type_stack, cstr_name);
    res = t_register_type(clang_getCursorType(cursor), type_stack);
    qv_remove_last(type_stack);
    clang_disposeString(name);
    return res;
}

static CXChildVisitResult
t_visit_register_type_fields(CXCursor cursor, CXCursor parent,
                             CXClientData data)
{
    qv_t(cstr) *type_stack = data;
    CXCursorKind kind = clang_getCursorKind(cursor);

    switch (kind) {
      case CXCursor_FieldDecl:
        if (t_register_type_cursor(cursor, type_stack) < 0) {
            return CXChildVisit_Break;
        }
        break;

      case CXCursor_UnionDecl:
      case CXCursor_StructDecl:
        return CXChildVisit_Recurse;

      case CXCursor_EnumDecl:
      case CXCursor_EnumConstantDecl:
        break;

      default: {
        lstr_t path = t_concat_type_stack(type_stack, "::", "", "");

        fprintf(stderr, "unsupported field type of kind %d for %pL\n",
                kind, &path);
        assert (false);
        return CXChildVisit_Break;
      }
    }

    return CXChildVisit_Continue;
}

static void add_new_typedef_record(pxcc_record_name_t *record_name,
                                   CXCursor cursor,
                                   pxcc_typedef_kind_t typedef_kind)
{
    pxcc_record_t *record;

    record = add_new_record(record_name, cursor, PXCC_RECORD_TYPEDEF);
    if (record) {
        record->typedef_kind = typedef_kind;
    }
}

static int t_register_typedef_type(CXType type, qv_t(cstr) *type_stack)
{
    pxcc_record_name_t *record_name;
    CXCursor cursor;
    CXType underlying_type;
    pxcc_typedef_kind_t typedef_kind;

    record_name = get_or_add_new_record_name_type(type);
    switch (record_name->status) {
    case PXCC_RECORD_NAME_NEW:
        /* It is new, process it as usual. */
        record_name->status = PXCC_RECORD_NAME_VISITED;
        break;

    case PXCC_RECORD_NAME_VISITED:
        /* Recursive call for this record name.
         * We need to continue until it is resolved by forwarding canonical
         * type.
         */
        break;

    case PXCC_RECORD_NAME_FORWARDED:
        /* Should not happen. */
        assert(false);
        break;

    case PXCC_RECORD_NAME_COMPLETED:
        /* The record was already registered, nothing to do here. */
        return 0;
    }

    cursor = clang_getTypeDeclaration(type);
    underlying_type = get_underlying_type(cursor);
    typedef_kind = get_typedef_kind(type, underlying_type);

    switch (typedef_kind) {
    case PXCC_TYPEDEF_UNNAMED:
        if (clang_visitChildren(cursor, t_visit_register_type_fields,
                                type_stack))
        {
            return -1;
        }
        add_new_typedef_record(record_name, cursor, typedef_kind);
        break;

    default: {
        switch (underlying_type.kind) {
        case CXType_FunctionNoProto:
        case CXType_FunctionProto:
            /* In case of typedef to function, we need to do first add the
             * record and then register the type. */
            add_new_typedef_record(record_name, cursor, typedef_kind);
            RETHROW(t_register_type(underlying_type, type_stack));
            break;

        default:
            RETHROW(t_register_type(underlying_type, type_stack));
            add_new_typedef_record(record_name, cursor, typedef_kind);
            break;
        }
    } break;
    }

    return 0;
}

static int t_register_function_type(CXType type, qv_t(cstr) *type_stack)
{
    CXType res_type = clang_getResultType(type);
    int n = clang_getNumArgTypes(type);
    int res;

    qv_append(type_stack, "res");
    res = t_register_type(res_type, type_stack);
    qv_remove_last(type_stack);
    RETHROW(res);

    for (int i = 0; i < n; i++) {
        qv_append(type_stack, t_fmt("arg%d", i));
        res = t_register_type(clang_getArgType(type, i), type_stack);
        qv_remove_last(type_stack);
        RETHROW(res);
    }
    return 0;
}

static const ctype_desc_t *get_non_anonymous_ctype(void)
{
    static bool is_init;
    static ctype_desc_t res;

    if (!is_init) {
        ctype_desc_build(&res, " ");
        ctype_desc_combine(&res, &res, &ctype_iscvar);
        is_init = true;
    }
    return &res;
}

static int t_register_record_enum_type(CXType type, qv_t(cstr) *type_stack)
{
    const ctype_desc_t *non_anonymous_ctype;
    CXString spelling;
    lstr_t name;
    CXCursor cursor;
    bool is_anonymous;
    pxcc_record_name_t *record_name;
    lstr_t canonical_name;
    qv_t(cstr) local_type_stack;

    spelling = clang_getTypeSpelling(type);
    name = get_unconst_type_spelling(spelling);
    cursor = clang_getTypeDeclaration(type);

    non_anonymous_ctype = get_non_anonymous_ctype();
    is_anonymous = !lstr_match_ctype(name, non_anonymous_ctype);
    if (is_anonymous) {
        /* When the record is anonymous, we need to create a custom type in
         * Cython */
        const char *prefix = get_cursor_kind_prefix(cursor);

        RETHROW_PN(prefix);
        name = t_concat_type_stack(type_stack, "__", prefix, "_t");
    }

    record_name = get_or_add_new_record_name(name);
    clang_disposeString(spelling);

    switch (record_name->status) {
    case PXCC_RECORD_NAME_NEW:
        /* It is new, process it as usual. */
        record_name->status = PXCC_RECORD_NAME_VISITED;
        break;

    case PXCC_RECORD_NAME_VISITED:
        /* Recursive call for this record name.
         * Create a forward record, and wait for complete record later on.
         */
        add_new_record(record_name, cursor, PXCC_RECORD_FORWARD);
        record_name->status = PXCC_RECORD_NAME_FORWARDED;
        return 0;

    case PXCC_RECORD_NAME_FORWARDED:
    case PXCC_RECORD_NAME_COMPLETED:
        /* The record was already forwarded or registered,
         * nothing to do here. */
        return 0;
    }

    canonical_name = get_canonical_record_enum_type_name(record_name->name);
    if (is_anonymous) {
        /* When the record is anonymous, we want to register the name of the
         * custom type corresponding to the cursor */
        qm_add(cxcursor_name, &_G.anonymous_types, &cursor, canonical_name);
    } else {
        t_qv_init(&local_type_stack, 1);
        qv_append(&local_type_stack, canonical_name.s);
        type_stack = &local_type_stack;
    }

    if (clang_visitChildren(cursor, t_visit_register_type_fields, type_stack))
    {
        return -1;
    }

    add_new_record(record_name, cursor, PXCC_RECORD_CANONICAL_TYPE);

    return 0;
}

static int t_register_type(CXType type, qv_t(cstr) *type_stack)
{
    for (;;) {
        switch (type.kind) {
          case CXType_Void ... CXType_Complex:
            return 0;

          case CXType_Pointer:
            type = clang_getPointeeType(type);
            break;

          case CXType_Typedef:
            return t_register_typedef_type(type, type_stack);

          case CXType_FunctionNoProto:
          case CXType_FunctionProto:
            return t_register_function_type(type, type_stack);

         case CXType_Unexposed:
            type = resolve_unexposed_type(type);
            break;

          case CXType_Record:
          case CXType_Enum:
            return t_register_record_enum_type(type, type_stack);

          case CXType_ConstantArray:
          case CXType_IncompleteArray:
          case CXType_VariableArray:
          case CXType_DependentSizedArray:
            type = clang_getArrayElementType(type);
            break;

#ifdef PXCC_HAS_ELABORATED_TYPE
          case CXType_Elaborated:
            type = clang_Type_getNamedType(type);
            break;
#endif

          default: {
            CXString type_spelling = clang_getTypeSpelling(type);
            CXString kind_spelling = clang_getTypeKindSpelling(type.kind);
            lstr_t path = t_concat_type_stack(type_stack, "::", "", "");

            fprintf(stderr, "unsupported type of kind %s (%s - %d) for %pL\n",
                    clang_getCString(type_spelling),
                    clang_getCString(kind_spelling), type.kind, &path);
            clang_disposeString(type_spelling);
            clang_disposeString(kind_spelling);
            assert (false);
            return -1;
          }
       }
    }

    return 0;
}

static CXChildVisitResult
type_decl_visitor(CXCursor cursor, CXCursor parent, CXClientData data)
{
    t_scope;
    CXCursorKind kind = clang_getCursorKind(cursor);
    qv_t(cstr) type_stack;

    if (kind != CXCursor_TypeRef) {
        PRINT_ERROR(cursor, "expected type ref cursor type, got %d\n", kind);
        return CXChildVisit_Break;
    }

    t_qv_init(&type_stack, 1);
    if (t_register_type_cursor(cursor, &type_stack) < 0) {
        return CXChildVisit_Break;
    }

    return CXChildVisit_Continue;
}

static pxcc_record_name_t *get_or_add_new_record_name_symbol(CXCursor cursor)
{
    CXString spelling = clang_getCursorSpelling(cursor);
    lstr_t name = LSTR(clang_getCString(spelling));
    pxcc_record_name_t *record_name;

    record_name = get_or_add_new_record_name(name);
    clang_disposeString(spelling);
    return record_name;
}

static int register_symbol(CXCursor cursor)
{
    t_scope;
    pxcc_record_name_t *record_name;
    qv_t(cstr) type_stack;

    cursor = clang_getCursorReferenced(cursor);
    record_name = get_or_add_new_record_name_symbol(cursor);

    switch (record_name->status) {
    case PXCC_RECORD_NAME_NEW:
        /* It is new, process it as usual. */
        record_name->status = PXCC_RECORD_NAME_VISITED;
        break;

    case PXCC_RECORD_NAME_VISITED:
    case PXCC_RECORD_NAME_FORWARDED:
        /* Should not happen, recursive call should not be present for
         * symbols. */
        assert(false);
        break;

    case PXCC_RECORD_NAME_COMPLETED:
        /* The record was already forwarded or registered,
         * nothing to do here. */
        return 0;
    }

    t_qv_init(&type_stack, 0);
    RETHROW(t_register_type_cursor(cursor, &type_stack));

    add_new_record(record_name, cursor, PXCC_RECORD_SYMBOL);

    return 0;
}

static CXChildVisitResult
symbol_decl_visitor(CXCursor cursor, CXCursor parent, CXClientData data)
{
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_DeclRefExpr) {
        if (register_symbol(cursor) < 0) {
            return CXChildVisit_Break;
        }
        return CXChildVisit_Continue;
    }
    return CXChildVisit_Recurse;
}

static int
register_types_symbols_var_decl(const char *name, CXCursor cursor)
{
    if (strstart(name, PXCC_EXPORT_FILE_PREFIX, NULL)) {
        THROW_ERR_IF(clang_visitChildren(cursor, file_decl_visitor, NULL));
    } else
    if (strstart(name, PXCC_EXPORT_TYPE_PREFIX, NULL)) {
        RETHROW(check_current_file_is_set(cursor));
        THROW_ERR_IF(clang_visitChildren(cursor, type_decl_visitor, NULL));
    } else
    if (strstart(name, PXCC_EXPORT_SYMBOL_PREFIX, NULL)) {
        RETHROW(check_current_file_is_set(cursor));
        THROW_ERR_IF(clang_visitChildren(cursor, symbol_decl_visitor, NULL));
    }
    return 0;
}

static CXChildVisitResult
register_types_symbols_visitor(CXCursor cursor, CXCursor parent,
                               CXClientData data)
{
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_VarDecl) {
        CXString name = clang_getCursorSpelling(cursor);
        const char *cstr_name = clang_getCString(name);
        int res = register_types_symbols_var_decl(cstr_name, cursor);

        clang_disposeString(name);
        if (res < 0) {
            return CXChildVisit_Break;
        }
    }

    return CXChildVisit_Continue;
}

static int register_types_symbols(CXTranslationUnit translation_unit)
{
    CXCursor root_cursor = clang_getTranslationUnitCursor(translation_unit);

    THROW_ERR_IF(clang_visitChildren(root_cursor,
                                     register_types_symbols_visitor,
                                     NULL));
    return 0;
}

/* }}} */
/* {{{ Print */

static bool is_cython_keyword_to_escape(lstr_t name)
{
    return qh_find(lstr, &_G.cython_keywords_to_escape, &name) >= 0;
}

static lstr_t t_format_cython_keyword(lstr_t name)
{
    return t_lstr_fmt("c_%pL", &name);
}

static lstr_t t_escape_cython_keyword(lstr_t name)
{
    if (is_cython_keyword_to_escape(name)) {
        lstr_t formatted_name = t_format_cython_keyword(name);

        return t_lstr_fmt("%pL \"%pL\"", &formatted_name, &name);
    }
    return name;
}

static lstr_t t_escape_cython_keyword_record(const pxcc_record_t *record)
{
    pstream_t name_ps;
    pstream_t prefix_ps = ps_init(NULL, 0);
    lstr_t name_lstr;

    name_ps = ps_initlstr(&record->name);
    ps_get_ps_chr_and_skip(&name_ps, ' ', &prefix_ps);
    name_lstr = LSTR_PS_V(&name_ps);
    if (is_cython_keyword_to_escape(name_lstr)) {
        lstr_t prefix_lstr = LSTR_PS_V(&prefix_ps);
        lstr_t formatted_name = t_format_cython_keyword(name_lstr);
        const char *space = prefix_ps.s ? " ": "";

        return t_lstr_fmt("%pL%s%pL \"%pL\"", &prefix_lstr, space,
                          &formatted_name, &name_lstr);
    }

    return lstr_dupc(record->name);
}

static lstr_t t_escape_cython_keyword_type(CXType type, bool use_canonical)
{
    CXString spelling = clang_getTypeSpelling(type);
    lstr_t name = LSTR(clang_getCString(spelling));

    if (use_canonical) {
        name = get_canonical_record_enum_type_name(name);
    }

    if (is_cython_keyword_to_escape(name)) {
        name = t_format_cython_keyword(name);
    } else {
        name = t_lstr_dup(name);
    }

    clang_disposeString(spelling);
    return name;
}

typedef struct pxcc_print_field_t {
    sb_t sb_before;
    sb_t sb_after;
} pxcc_print_field_t;

static pxcc_print_field_t *t_pxcc_print_field_init(pxcc_print_field_t *ctx)
{
    p_clear(ctx, 1);
    t_sb_init(&ctx->sb_before, 1 << 10);
    t_sb_init(&ctx->sb_after, 1 << 10);
    return ctx;
}

__attr_printf__(2, 3)
static void t_print_field_add_before(pxcc_print_field_t *ctx,
                                     const char *fmt, ...)
{
    int old_len = ctx->sb_before.len;
    va_list va;
    lstr_t reversed;

    va_start(va, fmt);
    sb_addvf(&ctx->sb_before, fmt, va);
    va_end(va);

    reversed = LSTR_INIT_V(ctx->sb_before.data + old_len,
                           ctx->sb_before.len - old_len);
    lstr_ascii_reverse(&reversed);
}

__attr_printf__(2, 3)
static void t_print_field_add_after(pxcc_print_field_t *ctx,
                                    const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    sb_addvf(&ctx->sb_after, fmt, va);
    va_end(va);
}

static void t_print_parentheses_prev_is_ptr(pxcc_print_field_t *ctx,
                                            bool prev_is_ptr)
{
    if (prev_is_ptr) {
        t_print_field_add_before(ctx, "(");
        t_print_field_add_after(ctx, ")");
    }
}

static lstr_t t_concat_print_field(pxcc_print_field_t *ctx)
{
    lstr_t before = LSTR_SB_V(&ctx->sb_before);

    lstr_ascii_reverse(&before);
    sb_addsb(&ctx->sb_before, &ctx->sb_after);
    return LSTR_SB_V(&ctx->sb_before);
}

static void t_print_canonical_type(CXType type, pxcc_print_field_t *ctx)
{
    lstr_t name = t_escape_cython_keyword_type(type, false);

    t_print_field_add_before(ctx, "%pL ", &name);
}

static void t_print_bool_type(CXType type, pxcc_print_field_t *ctx)
{
    t_print_field_add_before(ctx, "_Bool ");

    if (clang_isConstQualifiedType(type)) {
        t_print_field_add_before(ctx, "const ");
    }
}

static CXType t_print_pointer_type(CXType type, pxcc_print_field_t *ctx)
{
    if (clang_isConstQualifiedType(type)) {
        t_print_field_add_before(ctx, " const ");
    }
    t_print_field_add_before(ctx, "*");
    return clang_getPointeeType(type);
}

static CXType t_print_unexposed_type(CXType type, pxcc_print_field_t *ctx)
{
    return resolve_unexposed_type(type);
}

static CXType t_print_array_type(CXType type, pxcc_print_field_t *ctx)
{
    long long num = MAX(clang_getArraySize(type), 0);

    t_print_field_add_after(ctx, "[%lld]", num);
    return clang_getArrayElementType(type);
}

static void t_print_field_type(CXType type, pxcc_print_field_t *ctx);

static CXType t_print_function_type(CXType type, pxcc_print_field_t *ctx)
{
    int n = clang_getNumArgTypes(type);

    t_print_field_add_after(ctx, "(");
    for (int i = 0; i < n; i++) {
        CXType arg_type = clang_getArgType(type, i);
        pxcc_print_field_t arg_ctx;
        lstr_t arg_res;

        if (i > 0) {
            t_print_field_add_after(ctx, ", ");
        }

        t_pxcc_print_field_init(&arg_ctx);
        t_print_field_type(arg_type, &arg_ctx);
        arg_res = t_concat_print_field(&arg_ctx);
        t_print_field_add_after(ctx, "%pL", &arg_res);
    }
    t_print_field_add_after(ctx, ")");

    return clang_getResultType(type);
}

static void t_print_record_enum_field(CXType type, pxcc_print_field_t *ctx)
{
    CXCursor cursor = clang_getTypeDeclaration(type);
    int pos = qm_find(cxcursor_name, &_G.anonymous_types, &cursor);
    lstr_t name;

    if (pos < 0) {
        name = t_escape_cython_keyword_type(type, true);
    } else {
        name = _G.anonymous_types.values[pos];
    }

    t_print_field_add_before(ctx, "%pL ", &name);

    if (clang_isConstQualifiedType(type)) {
        t_print_field_add_before(ctx, "const ");
    }
}

static void t_print_field_type(CXType type, pxcc_print_field_t *ctx)
{
    bool loop_prev_is_ptr = false;
    bool is_elaborated_type_const = false;

    for (bool end_loop = false; !end_loop;) {
        bool prev_is_ptr = loop_prev_is_ptr;

        loop_prev_is_ptr = false;
        switch (type.kind) {
          case CXType_Void:
          case CXType_Char_U ... CXType_Complex:
          case CXType_Typedef:
            t_print_canonical_type(type, ctx);
            end_loop = true;
            break;

          case CXType_Bool:
            t_print_bool_type(type, ctx);
            end_loop = true;
            break;

          case CXType_Pointer:
            type = t_print_pointer_type(type, ctx);
            loop_prev_is_ptr = true;
            break;

         case CXType_Unexposed:
            t_print_parentheses_prev_is_ptr(ctx, prev_is_ptr);
            type = t_print_unexposed_type(type, ctx);
            break;

          case CXType_ConstantArray:
          case CXType_IncompleteArray:
          case CXType_VariableArray:
          case CXType_DependentSizedArray:
            type = t_print_array_type(type, ctx);
            break;

          case CXType_FunctionNoProto:
          case CXType_FunctionProto:
            t_print_parentheses_prev_is_ptr(ctx, prev_is_ptr);
            type = t_print_function_type(type, ctx);
            break;

          case CXType_Record:
          case CXType_Enum:
            t_print_record_enum_field(type, ctx);
            end_loop = true;
            break;

#ifdef PXCC_HAS_ELABORATED_TYPE
          case CXType_Elaborated:
#ifdef PXCC_IS_ELABORATED_TYPE_RESOLVE_UNCONST
            if (clang_isConstQualifiedType(type)) {
                is_elaborated_type_const = true;
            }
#endif /* PXCC_IS_ELABORATED_TYPE_RESOLVE_UNCONST */
            type = clang_Type_getNamedType(type);
            break;
#endif /* PXCC_HAS_ELABORATED_TYPE */

          default:
            fprintf(stderr, "unsupported type of kind %d\n", type.kind);
            assert (false);
            end_loop = true;
            break;
        }
    }

    if (is_elaborated_type_const) {
        t_print_field_add_before(ctx, "const ");
    }
}

static void print_field_definition(CXCursor cursor, CXType type)
{
    t_scope;
    pxcc_print_field_t ctx;
    CXString cursor_spelling;
    const char *cursor_spelling_str;
    lstr_t name;
    lstr_t res;

    t_pxcc_print_field_init(&ctx);
    cursor_spelling = clang_getCursorSpelling(cursor);
    cursor_spelling_str = clang_getCString(cursor_spelling);
    name = t_escape_cython_keyword(LSTR(cursor_spelling_str));
    t_print_field_add_after(&ctx, "%pL", &name);
    clang_disposeString(cursor_spelling);

    t_print_field_type(type, &ctx);

    res = t_concat_print_field(&ctx);
    printf("%pL", &res);
}

static void print_field_cursor(CXCursor cursor)
{
    CXType type = clang_getCursorType(cursor);

    print_field_definition(cursor, type);
}

static int print_type_field(CXCursor cursor)
{
    printf("        ");
    print_field_cursor(cursor);
    printf("\n");
    return 0;
}

static CXChildVisitResult
visit_print_type_fields(CXCursor cursor, CXCursor parent, CXClientData data)
{
    int *nb_fields = data;
    CXCursorKind kind = clang_getCursorKind(cursor);

    switch (kind) {
      case CXCursor_FieldDecl:
        if (print_type_field(cursor) < 0) {
            return CXChildVisit_Break;
        }
        (*nb_fields)++;
        break;

      case CXCursor_UnionDecl:
      case CXCursor_StructDecl:
        if (qm_find(cxcursor_name, &_G.anonymous_types, &cursor) < 0) {
            return CXChildVisit_Recurse;
        }
        break;

      case CXCursor_EnumDecl:
      case CXCursor_EnumConstantDecl:
        break;

      default: {
        fprintf(stderr, "unsupported field type of kind %d\n", kind);
        assert (false);
        return CXChildVisit_Break;
      }
    }

    return CXChildVisit_Continue;
}

static int print_type_fields(CXCursor cursor)
{
    int nb_fields = 0;

    if (clang_visitChildren(cursor, visit_print_type_fields, &nb_fields)) {
        return -1;
    }

    if (nb_fields == 0) {
        printf("        pass\n");
    }

    printf("\n");

    return 0;
}

static int print_enum_field(CXCursor cursor, CXType decl_type)
{
    t_scope;
    CXString cursor_spelling;
    lstr_t name;

    cursor_spelling = clang_getCursorSpelling(cursor);
    name = LSTR(clang_getCString(cursor_spelling));
    name = t_escape_cython_keyword(name);
    printf("        %pL = ", &name);
    clang_disposeString(cursor_spelling);

    switch (decl_type.kind) {
      case CXType_Bool ... CXType_ULongLong:
        printf("%llu", clang_getEnumConstantDeclUnsignedValue(cursor));
        break;

      case CXType_Char_S ... CXType_LongLong:
        printf("%lld", clang_getEnumConstantDeclValue(cursor));
        break;

      default: {
        CXString type_spelling = clang_getTypeSpelling(decl_type);

        fprintf(stderr, "unsupported enum type of kind %s (%d)\n",
                clang_getCString(type_spelling), decl_type.kind);
        clang_disposeString(type_spelling);
        assert (false);
        return -1;
      }
    }
    printf(",\n");
    return 0;
}

typedef struct pxcc_print_enum_t {
    int nb_fields;
    CXType decl_type;
} pxcc_print_enum_t;

static CXChildVisitResult
visit_print_enum_fields(CXCursor cursor, CXCursor parent, CXClientData data)
{
    pxcc_print_enum_t *ctx = data;
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_EnumConstantDecl) {
        if (print_enum_field(cursor, ctx->decl_type) < 0) {
            return CXChildVisit_Break;
        }
        ctx->nb_fields++;
    }

    return CXChildVisit_Continue;
}

static int print_enum_fields(CXCursor cursor)
{
    pxcc_print_enum_t ctx;

    p_clear(&ctx, 1);
    ctx.decl_type = clang_getEnumDeclIntegerType(cursor);

    if (clang_visitChildren(cursor, visit_print_enum_fields, &ctx)) {
        return -1;
    }

    if (ctx.nb_fields == 0) {
        printf("        pass\n");
    }

    printf("\n");

    return 0;
}

static int print_canonical_type(pxcc_record_t *record)
{
    t_scope;
    lstr_t name;

    name = t_escape_cython_keyword_record(record);
    printf("    cdef %pL:\n", &name);
    if (clang_getCursorKind(record->cursor) == CXCursor_EnumDecl) {
        RETHROW(print_enum_fields(record->cursor));
    } else {
        RETHROW(print_type_fields(record->cursor));
    }

    return 0;
}

static void print_different_typedef(pxcc_record_t *record)
{
    CXType underlying_type = get_underlying_type(record->cursor);

    printf("    ctypedef ");
    print_field_definition(record->cursor, underlying_type);
    printf("\n\n");
}

static const char *get_unnamed_typedef_kind_prefix(CXCursor cursor)
{
    CXType type = clang_getCursorType(cursor);
    CXType canonical_type = clang_getCanonicalType(type);
    CXCursor canonical_cursor = clang_getTypeDeclaration(canonical_type);

    return get_cursor_kind_prefix(canonical_cursor);
}

static int print_unnamed_typedef(pxcc_record_t *record)
{
    t_scope;
    const char *prefix;
    lstr_t name;

    prefix = RETHROW_PN(get_unnamed_typedef_kind_prefix(record->cursor));
    name = t_escape_cython_keyword_record(record);
    printf("    ctypedef %s%pL:\n", prefix, &name);
    print_type_fields(record->cursor);
    return 0;
}

static int print_typedef(pxcc_record_t *record)
{
    switch (record->typedef_kind) {
    case PXCC_TYPEDEF_TRANSPARENT:
        /* Do not print transparent typedef. */
        break;

    case PXCC_TYPEDEF_DIFFERENT:
        print_different_typedef(record);
        break;

    case PXCC_TYPEDEF_UNNAMED:
        RETHROW(print_unnamed_typedef(record));
        break;
    }

    return 0;
}

static int print_symbol(pxcc_record_t *record)
{
    printf("    ");
    print_field_cursor(record->cursor);
    printf("\n\n");
    return 0;
}

static int print_forward(pxcc_record_t *record)
{
    t_scope;
    lstr_t name;

    name = t_escape_cython_keyword_record(record);
    printf("    cdef %pL\n\n", &name);
    return 0;
}

static void print_header(void)
{
    printf(
        "#**** THIS FILE IS AUTOGENERATED DO NOT MODIFY DIRECTLY ! ****\n\n"
        "from libcpp cimport bool as _Bool\n\n"
    );
}

static void print_file(lstr_t file)
{
    printf("cdef extern from %pL nogil:\n\n", &file);
}

static int _print_registered_types_and_symbols(void)
{
    lstr_t prev_file = LSTR_NULL_V;

    print_header();

    tab_for_each_ptr(record, &_G.records) {
        if (!lstr_equal(prev_file, record->file)) {
            prev_file = record->file;
            print_file(record->file);
        }

        switch (record->kind) {
          case PXCC_RECORD_CANONICAL_TYPE:
            RETHROW(print_canonical_type(record));
            break;

          case PXCC_RECORD_TYPEDEF:
            RETHROW(print_typedef(record));
            break;

          case PXCC_RECORD_SYMBOL:
            RETHROW(print_symbol(record));
            break;

          case PXCC_RECORD_FORWARD:
            RETHROW(print_forward(record));
            break;
        }
    }

    return 0;
}

static int open_output_file(void)
{
    if (!_G.opts.output_file) {
        return 0;
    }

    if (!freopen(_G.opts.output_file, "w", stdout)) {
        fprintf(stderr, "unable to open output file `%s`: %m",
                _G.opts.output_file);
        return -1;
    }

    return 0;
}

static void close_output_file(void)
{
    if (_G.close_stdout) {
        fclose(stdout);
    }
}

static int print_registered_types_and_symbols(void)
{
    int res;

    RETHROW(open_output_file());
    res = _print_registered_types_and_symbols();
    close_output_file();
    return res;
}

/* }}} */
/* {{{ Parsing */

static int read_call_fd(const char *prg_name, int fd, sb_t *sb)
{
    int res;

    do {
        res = sb_read(sb, fd, 0);
    } while (res > 0 || (res < 0 && ERR_RW_RETRIABLE(errno)));

    if (res < 0 && !ERR_RW_RETRIABLE(errno)) {
        fprintf(stderr, "unable to read error of %s: %m", prg_name);
        return -1;
    }
    return 0;
}

static int call_cmd(const char *args[], sb_t *err)
{
    const char *prg_name = args[0];
    int fde[2];
    pid_t pid;
    int status;

    if (pipe(fde) < 0) {
        fprintf(stderr, "unable to pipe(): %m");
        return -1;
    }

    pid = ifork();
    if (pid < 0) {
        fprintf(stderr, "unable to fork(): %m");
        return -1;
    }

    if (pid == 0) {
        close(fde[0]);
        dup2(fde[1], STDERR_FILENO);
        close(fde[1]);

        setpgid(0, 0);
        execvp(args[0], (char * const *)args);
        e_fatal("unable to run %s: %m", prg_name);
    }

    close(fde[1]);
    read_call_fd(prg_name, fde[0], err);
    close(fde[0]);

    for (;;) {
        if (waitpid(pid, &status, 0) < 0) {
            e_fatal("waitpid: %m");
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status) ? -1 : 0;
        }
        if (WIFSIGNALED(status)) {
            fprintf(stderr, "%s killed with signal %s",
                    prg_name, strsignal(WTERMSIG(status)));
            return -1;
        }
    }

    return 0;
}

static int t_get_clang_isystem(qv_t(lstr) *clang_isystem_dirs)
{
    SB_1k(err);
    pstream_t ps;
    const char *args[] = {
        "clang",
        "-xc",
        "-###",
        "/dev/null",
        NULL,
    };

    if (call_cmd(args, &err) < 0) {
        fprintf(stderr, "unable to get clang isystem directories: %*pM",
                SB_FMT_ARG(&err));
        return -1;
    }

    /* Retrieve every internal arguments that are returned by clang as
     * -internal-* */
    ps = ps_initsb(&err);
    while (!ps_done(&ps)) {
        pstream_t val = ps_get_tok(&ps, &ctype_isspace);

        if (ps_startswithstr(&val, "\"-internal")) {
            val = ps_get_tok(&ps, &ctype_isspace);

            /* clang returns arguments protected with '"'. We need to remove
             * them in order to use them later on. */
            ps_skipc(&val, '"');
            ps_shrinkc(&val, '"');
            qv_append(clang_isystem_dirs, t_lstr_dup(LSTR_PS_V(&val)));
        }
    }

    return 0;
}

static int print_diagnostics(CXTranslationUnit translation_unit)
{
    unsigned nb_diag = clang_getNumDiagnostics(translation_unit);
    unsigned display_opts = clang_defaultDiagnosticDisplayOptions();

    for (unsigned i = 0; i < nb_diag; ++i) {
        CXDiagnostic diagnotic;
        CXString error_string;

        diagnotic = clang_getDiagnostic(translation_unit, i);
        error_string = clang_formatDiagnostic(diagnotic, display_opts);
        fprintf(stderr, "%s\n", clang_getCString(error_string));
        clang_disposeString(error_string);
    }

    THROW_ERR_IF(nb_diag > 0);
    return 0;
}

static int parse_register(CXTranslationUnit translation_unit)
{
    RETHROW(print_diagnostics(translation_unit));
    RETHROW(register_types_symbols(translation_unit));
    return print_registered_types_and_symbols();
}

static int parse_create_tu(CXIndex index, const char *header_file,
                           const qv_t(lstr) *clang_isystem_dirs,
                           int argc, char *argv[])
{
    t_scope;
    CXTranslationUnit translation_unit;
    CXErrorCode error_code;
    int res;
    qv_t(cstr) args;
    const char *cargs[] = {
        "-xc",
        "-std=gnu11",
        "-D_GNU_SOURCE",
        "-fno-blocks",
        "-include",
        header_file,
    };

    t_qv_init(&args, countof(cargs) + clang_isystem_dirs->len * 2 + argc);

    carray_for_each_entry(val, cargs) {
        qv_append(&args, val);
    }
    tab_for_each_entry(val, clang_isystem_dirs) {
        qv_append(&args, "-isystem");
        qv_append(&args, val.s);
    }
    for (int i = 0; i < argc; i++) {
        qv_append(&args, argv[i]);
    }

    error_code = clang_parseTranslationUnit2(index, NULL,
                                             args.tab, args.len,
                                             NULL, 0, 0,
                                             &translation_unit);

    if (error_code != CXError_Success) {
        fprintf(stderr, "error parsing transaction unit (error code %d), "
                "the arguments might be invalid\n", error_code);
        return -1;
    }

    res = parse_register(translation_unit);
    clang_disposeTranslationUnit(translation_unit);
    return res;
}

static int parse_create_index(const char *header_file,
                              const qv_t(lstr) *clang_isystem_dirs,
                              int argc, char *argv[])
{
    CXIndex index;
    int res;

    index = clang_createIndex(0, 0);
    if (!expect(index)){
        fprintf(stderr, "error while creating index\n");
        return -1;
    }

    res = parse_create_tu(index, header_file, clang_isystem_dirs, argc, argv);
    clang_disposeIndex(index);
    return res;
}

static int write_header_tmp_file(int header_fd, const char *header_file,
                                 int argc, char *argv[])
{
    t_scope;
    lstr_t header;
    qv_t(lstr) clang_isystem_dirs;

    header = t_farch_get_data(pxcc_farch, "pxcc_header.h");

    if (write(header_fd, header.s, header.len) < 0) {
        fprintf(stderr, "unable to write pxcc header: %m");
        return -1;
    }

    t_qv_init(&clang_isystem_dirs, 0);
    RETHROW(t_get_clang_isystem(&clang_isystem_dirs));

    return parse_create_index(header_file, &clang_isystem_dirs, argc, argv);
}

static int do_parse(int argc, char *argv[])
{
    char header_file[] = "/tmp/pxcc_header_XXXXXX";
    int header_fd;
    int res;

    header_fd = mkstemp(header_file);
    if (header_fd < 0) {
        fprintf(stderr, "unable to open temporary file `%s`: %m\n",
                header_file);
        return -1;
    }

    res = write_header_tmp_file(header_fd, header_file, argc, argv);
    close(header_fd);
    if (!_G.opts.keep_temporary_files) {
        unlink(header_file);
    }
    return res;
}

/* }}} */
/* {{{ Main */

static void make_cython_keywords_to_escape_qh(void)
{
    qh_set_minsize(lstr, &_G.cython_keywords_to_escape,
                   countof(cython_keywords_to_escape_g));
    carray_for_each_entry(keyword, cython_keywords_to_escape_g) {
        qh_add(lstr, &_G.cython_keywords_to_escape, &keyword);
    }
}

static popt_t options_g[] = {
    OPT_FLAG('h', "help",    &_G.opts.help,    "show this help"),
    OPT_FLAG('v', "version", &_G.opts.version, "show version"),
    OPT_FLAG('k', "keep-temporary-files", &_G.opts.keep_temporary_files,
             "keep temporary created files"),
    OPT_STR('o', "output", &_G.opts.output_file,
            "place output in specified file"),
    OPT_END(),
};

static char const * const usage_g[] = {
"Pxcc is a tool to export C types and symbols specified in a '.pxc' file to ",
"a Cython definition file '.pxd'.",
"",
"See README.adoc of pxcc for more information.",
"",
"ARGUMENTS",
"    [-h]:        show this help",
"    [-k]:        keep temporary created files",
"    [-o file]:   place output in specified file, default is stdout",
"    [cflags...]: optional list of cflags given to clang. Typically, the",
"                 list of include paths required for the parse",
"    file:        the pxc file to compile",
NULL
};

static char const * const small_usage_g =
    "[-h] [-k] [-o file] [<cflags...>] <file>";

int main(int argc, char *argv[])
{
    t_scope;
    const char *arg0 = NEXTARG(argc, argv);
    int res;

    if (argc < 1) {
        makeusage(!_G.opts.help, arg0, small_usage_g, usage_g, options_g);
    }

    argc = RETHROW(parseopt(argc, argv, options_g, POPT_IGNORE_UNKNOWN_OPTS));

    if (_G.opts.version) {
        printf("%d.%d.%d\n", PXCC_MAJOR, PXCC_MINOR, PXCC_PATCH);
        return 0;
    }

    if (_G.opts.help) {
        makeusage(!_G.opts.help, arg0, small_usage_g, usage_g, options_g);
    }

    make_cython_keywords_to_escape_qh();
    res = do_parse(argc, argv);

    qv_deep_wipe(&_G.files, lstr_wipe);
    qm_deep_wipe(pxcc_record_name, &_G.record_names, IGNORE,
                 pxcc_record_name_delete);
    qv_wipe(&_G.records);
    qh_wipe(lstr, &_G.cython_keywords_to_escape);

    return res;
}

/* }}} */
