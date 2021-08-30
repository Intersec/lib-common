/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#ifndef IS_IOP_IOPC_IOPC_H
#define IS_IOP_IOPC_IOPC_H

#define SNMP_OBJ_OID_MIN 1
#define SNMP_OBJ_OID_MAX 0xFFFF
#define SNMP_IFACE_OID_MIN 0
#define SNMP_IFACE_OID_MAX 0xFFFF

#include <lib-common/container.h>
#include <lib-common/iop.h>
#include <lib-common/log.h>

typedef enum iopc_tok_type_t {
    ITOK_EOF       = -1,
    ITOK_DOT       = '.',
    ITOK_SEMI      = ';',
    ITOK_EQUAL     = '=',
    ITOK_MINUS     = '-',
    ITOK_COLON     = ':',
    ITOK_COMMA     = ',',
    ITOK_LPAREN    = '(',
    ITOK_RPAREN    = ')',
    ITOK_LBRACKET  = '[',
    ITOK_RBRACKET  = ']',
    ITOK_LBRACE    = '{',
    ITOK_RBRACE    = '}',
    ITOK_QUESTION  = '?',
    ITOK_STAR      = '*',
    ITOK_UNDER     = '_',
    ITOK_SLASH     = '/',
    ITOK_TILDE     = '~',
    ITOK_CARET     = '^',
    ITOK_PLUS      = '+',
    ITOK_PERCENT   = '%',
    ITOK_AMP       = '&',
    ITOK_VBAR      = '|',
    ITOK_LT        = '<',
    ITOK_GT        = '>',

    ITOK_IDENT     = 128,
    ITOK_LSHIFT,
    ITOK_RSHIFT,
    ITOK_EXP,
    ITOK_INTEGER,
    ITOK_DOUBLE,
    ITOK_BOOL,
    ITOK_STRING,
    ITOK_COMMENT,
    ITOK_DOX_COMMENT,
    ITOK_ATTR,
    ITOK_GEN_ATTR_NAME,
} iopc_tok_type_t;

/*----- locations -----*/

typedef struct iopc_loc_t {
    const char *file;
    int lmin, lmax;
    int cmin, cmax;
    const char *comment;
} iopc_loc_t;
qvector_t(iopc_loc, iopc_loc_t);

extern struct {
    logger_t logger;

    const char *prefix_dir;
    bool        display_prefix;

    qv_t(iopc_loc) loc_stack;

    bool print_info;

    int class_id_min;
    int class_id_max;
} iopc_g;

void iopc_loc_merge(iopc_loc_t *l1, iopc_loc_t l2);
iopc_loc_t iopc_loc_merge2(iopc_loc_t l1, iopc_loc_t l2);

#define print_warning(fmt, ...)  \
    logger_warning(&iopc_g.logger, fmt, ##__VA_ARGS__)

#define print_error(fmt, ...)  \
    logger_error(&iopc_g.logger, fmt, ##__VA_ARGS__)

#define throw_error(fmt, ...)  \
    do {                                    \
        print_error(fmt, ##__VA_ARGS__);    \
        return -1;                          \
    } while (0)

const char *__get_path(const char *file, bool display_prefix);

static inline const char *get_print_path(const char *file)
{
    return __get_path(file, iopc_g.display_prefix);
}

static inline const char *get_full_path(const char *file)
{
    return __get_path(file, true);
}

#define do_loc_(fmt, level, t, loc, ...)                                     \
    do {                                                                     \
        const typeof(loc) *__loc = &(loc);                                   \
                                                                             \
        if (__loc->file) {                                                   \
            logger_log(&iopc_g.logger, level, "%s:%d:%d: %s: "fmt,           \
                       get_print_path(__loc->file),                          \
                       __loc->lmin, __loc->cmin, (t), ##__VA_ARGS__);        \
        } else {                                                             \
            logger_log(&iopc_g.logger, level, "%s: "fmt, (t),                \
                       ##__VA_ARGS__);                                       \
        }                                                                    \
    } while (0)

#define do_loc(fmt, level, t, loc, ...)  \
    do {                                                                     \
        do_loc_(fmt, level, t, loc, ##__VA_ARGS__);                          \
        for (int i_ = 0; i_ < iopc_g.loc_stack.len; i_++) {                  \
            iopc_loc_t loc_ = iopc_g.loc_stack.tab[i_];                      \
            do_loc_("%s", level, " from", loc_, loc_.comment);               \
        }                                                                    \
    } while (0)

#define t_push_loc(loc, ...)  \
    do {                                                                     \
        qv_append(&iopc_g.loc_stack, loc);                                   \
        tab_last(&iopc_g.loc_stack)->comment = t_fmt(__VA_ARGS__);           \
    } while (0)

#define pop_loc()          qv_shrink(&iopc_g.loc_stack, 1)
#define clear_loc()        qv_clear(&iopc_g.loc_stack)

#define info_loc(fmt, loc, ...)   \
    do {                                                                     \
        if (iopc_g.print_info) {                                             \
            do_loc(fmt, LOG_INFO, "info", loc, ##__VA_ARGS__);               \
        }                                                                    \
    } while (0)

#define warn_loc(fmt, loc, ...)   \
    do_loc(fmt, LOG_WARNING, "warning", loc, ##__VA_ARGS__)

#define error_loc(fmt, loc, ...)  \
    do_loc(fmt, LOG_ERR, "error", loc, ##__VA_ARGS__)

#define throw_loc(fmt, loc, ...)  \
    do {                                                                     \
        error_loc(fmt, loc, ##__VA_ARGS__);                                  \
        return -1;                                                           \
    } while (0)

#define throw_loc_p(fmt, loc, ...)  \
    do {                                                                     \
        error_loc(fmt, loc, ##__VA_ARGS__);                                  \
        return NULL;                                                         \
    } while (0)

/*----- doxygen lexer part -----*/

qvector_t(sb, sb_t);

typedef struct dox_chunk_t {
    lstr_t     keyword;
    qv_t(lstr) params;      /* params inside [] following the keyword */
    qv_t(lstr) params_args; /* IDs starting the first paragraph */
    qv_t(sb)   paragraphs;
    int        paragraph0_args_len;
    int        first_sentence_len; /* used for auto-brief */
    iopc_loc_t loc;
} dox_chunk_t;
GENERIC_INIT(dox_chunk_t, dox_chunk);
static inline void dox_chunk_wipe(dox_chunk_t *p)
{
    lstr_wipe(&p->keyword);
    qv_deep_wipe(&p->params, lstr_wipe);
    qv_deep_wipe(&p->params_args, lstr_wipe);
    qv_deep_wipe(&p->paragraphs, sb_wipe);
}

qvector_t(dox_chunk, dox_chunk_t);

typedef struct dox_tok_t {
    qv_t(dox_chunk) chunks;
    bool is_back : 1;
} dox_tok_t;
static inline void dox_tok_wipe(dox_tok_t *p) {
    /* XXX: don't deep_wipe chunks when wiping dox_tok
     *      elements of chunks will be used after deletion of the token
     */
    qv_wipe(&p->chunks);
}
GENERIC_NEW_INIT(dox_tok_t, dox_tok);
GENERIC_DELETE(dox_tok_t, dox_tok);

/*----- doxygen data -----*/

typedef enum iopc_dox_type_t {
    IOPC_DOX_TYPE_BRIEF,
    IOPC_DOX_TYPE_DETAILS,
    IOPC_DOX_TYPE_WARNING,
    IOPC_DOX_TYPE_EXAMPLE,
    IOPC_DOX_TYPE_SIMPLE = IOPC_DOX_TYPE_EXAMPLE,

    IOPC_DOX_TYPE_PARAM,
    IOPC_DOX_TYPE_count
} iopc_dox_type_t;

lstr_t iopc_dox_type_to_lstr(iopc_dox_type_t);

typedef struct iopc_dox_t {
    iopc_dox_type_t type;
    lstr_t          desc;
} iopc_dox_t;
GENERIC_INIT(iopc_dox_t, iopc_dox);
static inline void iopc_dox_wipe(iopc_dox_t *dox)
{
    lstr_wipe(&dox->desc);
}

qvector_t(iopc_dox, iopc_dox_t);

iopc_dox_t *iopc_dox_find_type(const qv_t(iopc_dox) *, iopc_dox_type_t);

/*----- lexer and tokens -----*/

typedef struct iopc_token_t {
    iopc_loc_t      loc;
    iopc_tok_type_t token;

    int refcnt;
    union {
        uint64_t i;
        double   d;
    };
    bool i_is_signed;
    bool b_is_char;
    sb_t b;
    dox_tok_t *dox;
} iopc_token_t;
static inline iopc_token_t *iopc_token_init(iopc_token_t *tk) {
    p_clear(tk, 1);
    sb_init(&tk->b);
    return tk;
}
static inline void iopc_token_wipe(iopc_token_t *tk) {
    sb_wipe(&tk->b);
    dox_tok_delete(&tk->dox);
}
DO_REFCNT(iopc_token_t, iopc_token);
qvector_t(iopc_token, iopc_token_t *);

typedef enum iopc_file_t {
    IOPC_FILE_FD,
    IOPC_FILE_STDIN,
    IOPC_FILE_BUFFER,
} iopc_file_t;

struct lexdata *iopc_lexer_new(const char *file, const char *data,
                               iopc_file_t type);
int iopc_lexer_fd(struct lexdata *);
void iopc_lexer_push_state_attr(struct lexdata *ld);
void iopc_lexer_pop_state(struct lexdata *ld);
void iopc_lexer_delete(struct lexdata **);
int iopc_next_token(struct lexdata *, bool want_comments,
                    iopc_token_t **out_tk);

/*----- ast -----*/

typedef struct iopc_path_t {
    int refcnt;
    iopc_loc_t loc;
    qv_t(str) bits;
    char *pretty_dot;
    char *pretty_slash;
} iopc_path_t;
static inline iopc_path_t *iopc_path_init(iopc_path_t *path) {
    p_clear(path, 1);
    qv_init(&path->bits);
    return path;
}
static inline void iopc_path_wipe(iopc_path_t *path) {
    p_delete(&path->pretty_dot);
    p_delete(&path->pretty_slash);
    qv_deep_wipe(&path->bits, p_delete);
}
DO_REFCNT(iopc_path_t, iopc_path);
qvector_t(iopc_path, iopc_path_t *);

typedef struct iopc_pkg_t iopc_pkg_t;
static inline void iopc_pkg_delete(iopc_pkg_t **);

/*----- attributes -----*/

typedef enum iopc_attr_id_t {
    IOPC_ATTR_CTYPE,
    IOPC_ATTR_NOWARN,
    IOPC_ATTR_PREFIX,
    IOPC_ATTR_STRICT,
    IOPC_ATTR_MIN,
    IOPC_ATTR_MAX,
    IOPC_ATTR_MIN_LENGTH,
    IOPC_ATTR_MAX_LENGTH,
    IOPC_ATTR_LENGTH,
    IOPC_ATTR_MIN_OCCURS,
    IOPC_ATTR_MAX_OCCURS,
    IOPC_ATTR_CDATA,
    IOPC_ATTR_NON_EMPTY,
    IOPC_ATTR_NON_ZERO,
    IOPC_ATTR_PATTERN,
    IOPC_ATTR_PRIVATE,
    IOPC_ATTR_ALIAS,
    IOPC_ATTR_NO_REORDER,
    IOPC_ATTR_ALLOW,
    IOPC_ATTR_DISALLOW,
    IOPC_ATTR_GENERIC,
    IOPC_ATTR_DEPRECATED,
    IOPC_ATTR_SNMP_PARAMS_FROM,
    IOPC_ATTR_SNMP_PARAM,
    IOPC_ATTR_SNMP_INDEX,
    IOPC_ATTR_TS_NO_COLL,
    IOPC_ATTR_FORCE_FIELD_NAME,
} iopc_attr_id_t;

/* types on which an attribute can apply */
typedef enum iopc_attr_type_t {
    /* fields */
    IOPC_ATTR_T_INT = 1 << 0,
    IOPC_ATTR_T_BOOL = 1 << 1,
    IOPC_ATTR_T_DOUBLE = 1 << 2,
    IOPC_ATTR_T_STRING = 1 << 3,
    IOPC_ATTR_T_DATA = 1 << 4,
    IOPC_ATTR_T_XML = 1 << 5,
    /* fields or declarations */
    IOPC_ATTR_T_ENUM = 1 << 6,
    IOPC_ATTR_T_UNION = 1 << 7,
    IOPC_ATTR_T_STRUCT = 1 << 8,
    IOPC_ATTR_T_CLASS = 1 << 9,
#define IOPC_ATTR_T_ALL_FIELDS  BITMASK_LE(int64_t, 9)
    /* declarations */
    IOPC_ATTR_T_RPC = 1 << 10,
    IOPC_ATTR_T_IFACE = 1 << 11,
    IOPC_ATTR_T_MOD = 1 << 12,
#define IOPC_ATTR_T_ALL         BITMASK_LE(int64_t, 12)
    /* snmp */
    IOPC_ATTR_T_SNMP_OBJ = 1 << 13,
    IOPC_ATTR_T_SNMP_IFACE = 1 << 14,
    IOPC_ATTR_T_SNMP_TBL = 1 << 15,
} iopc_attr_type_t;


typedef enum iopc_attr_flag_t {
    /* attribute can apply to required fields */
    IOPC_ATTR_F_FIELD_REQUIRED = 1 << 0,
    /* attribute can apply to field with default value */
    IOPC_ATTR_F_FIELD_DEFVAL = 1 << 1,
    /* attribute can apply to optional fields */
    IOPC_ATTR_F_FIELD_OPTIONAL = 1 << 2,
    /* attribute can apply to repeated fields */
    IOPC_ATTR_F_FIELD_REPEATED = 1 << 3,
#define IOPC_ATTR_F_FIELD_ALL  BITMASK_LE(int64_t, 3)
#define IOPC_ATTR_F_FIELD_ALL_BUT_REQUIRED                \
    (IOPC_ATTR_F_FIELD_ALL & ~IOPC_ATTR_F_FIELD_REQUIRED)

    /* attribute can apply to declarations */
    IOPC_ATTR_F_DECL = 1 << 4,
    /* attribute can be used more than once */
    IOPC_ATTR_F_MULTI = 1 << 5,
    /* attribute will generate a check in iop_check_constraints */
    IOPC_ATTR_F_CONSTRAINT = 1 << 6,
} iopc_attr_flag_t;


typedef struct iopc_arg_desc_t {
    lstr_t          name;
    /* ITOK_STRING / ITOK_IDENT / ITOK_DOUBLE / ITOK_INTEGER
     * when set to ITOK_DOUBLE, ITOK_INTEGER is accepted as well */
    iopc_tok_type_t type;
} iopc_arg_desc_t;
static inline iopc_arg_desc_t *iopc_arg_desc_init(iopc_arg_desc_t *arg) {
    p_clear(arg, 1);
    return arg;
}
static inline void iopc_arg_desc_wipe(iopc_arg_desc_t *arg) {
    lstr_wipe(&arg->name);
}
GENERIC_NEW(iopc_arg_desc_t, iopc_arg_desc);
GENERIC_DELETE(iopc_arg_desc_t, iopc_arg_desc);
qvector_t(iopc_arg_desc, iopc_arg_desc_t);

typedef struct iopc_attr_desc_t {
    iopc_attr_id_t      id;
    lstr_t              name;
    qv_t(iopc_arg_desc) args;
    /* bitfield of iopc_attr_type_t */
    int64_t             types;
    /* bitfield of iopc_attr_flag_t */
    int64_t             flags;
} iopc_attr_desc_t;
#define IOPC_ATTR_REPEATED_MONO_ARG(desc)  \
    ({                                                                       \
        const iopc_attr_desc_t *__desc = (desc);                             \
        __desc->args.len == 1 && (__desc->flags & IOPC_ATTR_F_MULTI);        \
    })

static inline iopc_attr_desc_t *iopc_attr_desc_init(iopc_attr_desc_t *d) {
    p_clear(d, 1);
    qv_init(&d->args);
    return d;
}
static inline void iopc_attr_desc_wipe(iopc_attr_desc_t *d) {
    lstr_wipe(&d->name);
    qv_deep_wipe(&d->args, iopc_arg_desc_wipe);
}
GENERIC_NEW(iopc_attr_desc_t, iopc_attr_desc);
GENERIC_DELETE(iopc_attr_desc_t, iopc_attr_desc);
qm_kvec_t(attr_desc, lstr_t, iopc_attr_desc_t,
          qhash_lstr_hash, qhash_lstr_equal);


typedef struct iopc_arg_t {
    iopc_loc_t           loc;
    iopc_arg_desc_t     *desc;
    /* ITOK_STRING / ITOK_IDENT / ITOK_DOUBLE / ITOK_INTEGER */
    iopc_tok_type_t      type;
    union {
        int64_t i64;
        double  d;
        lstr_t  s;
    } v;
} iopc_arg_t;

static inline iopc_arg_t iopc_arg_dup(const iopc_arg_t *arg) {
    iopc_arg_t a = *arg;

    if (a.desc) {
        switch (a.desc->type) {
          case ITOK_STRING:
          case ITOK_IDENT:
            a.v.s = lstr_dup(arg->v.s);
            break;
          default:
            break;
        }
    }
    return a;
}
static inline iopc_arg_t *iopc_arg_init(iopc_arg_t *a) {
    p_clear(a, 1);
    return a;
}
static inline void iopc_arg_wipe(iopc_arg_t *a) {
    if (a->desc) {
        switch (a->desc->type) {
          case ITOK_STRING:
          case ITOK_IDENT:
            lstr_wipe(&a->v.s);
            break;
          default:
            break;
        }
    }
}
GENERIC_NEW(iopc_arg_t, iopc_arg);
GENERIC_DELETE(iopc_arg_t, iopc_arg);
qvector_t(iopc_arg, iopc_arg_t);

typedef struct iopc_extends_t {
    bool       is_snmp_root : 1;
    iopc_loc_t loc;
    iopc_path_t *path;
    iopc_pkg_t  *pkg;
    char *name;
    struct iopc_struct_t *st;
} iopc_extends_t;
GENERIC_NEW_INIT(iopc_extends_t, iopc_extends);
static inline void iopc_extends_wipe(iopc_extends_t *extends);
GENERIC_DELETE(iopc_extends_t, iopc_extends);
qvector_t(iopc_extends, iopc_extends_t *);

typedef struct iopc_attr_t {
    int                  refcnt;
    iopc_loc_t           loc;
    iopc_attr_desc_t    *desc;
    qv_t(iopc_arg)       args;

    /* Used only for generic attributes */
    lstr_t               real_name;

    /* Used only for snmp_params_from attributes */
    qv_t(iopc_extends)   snmp_params_from;
} iopc_attr_t;

static inline iopc_attr_t *iopc_attr_init(iopc_attr_t *a) {
    p_clear(a, 1);
    qv_init(&a->args);
    qv_init(&a->snmp_params_from);
    return a;
}
static inline void iopc_attr_wipe(iopc_attr_t *a) {
    lstr_wipe(&a->real_name);
    qv_deep_wipe(&a->args, iopc_arg_wipe);
    qv_deep_wipe(&a->snmp_params_from, iopc_extends_delete);
}
DO_REFCNT(iopc_attr_t, iopc_attr);
qvector_t(iopc_attr, iopc_attr_t *);

int
iopc_attr_check(const qv_t(iopc_attr) *, iopc_attr_id_t,
                const qv_t(iopc_arg) **out);

int t_iopc_attr_check_prefix(const qv_t(iopc_attr) *, lstr_t *out);

typedef enum iopc_struct_type_t {
    STRUCT_TYPE_STRUCT   = 0,
    STRUCT_TYPE_CLASS    = 1,
    STRUCT_TYPE_UNION    = 2,
    STRUCT_TYPE_TYPEDEF  = 3,
    STRUCT_TYPE_SNMP_OBJ = 4,
    STRUCT_TYPE_SNMP_TBL = 5,
} iopc_struct_type_t;

static inline bool iopc_is_class(iopc_struct_type_t type)
{
    return type == STRUCT_TYPE_CLASS;
}
static inline bool iopc_is_snmp_obj(iopc_struct_type_t type)
{
    return type == STRUCT_TYPE_SNMP_OBJ;
}
static inline bool iopc_is_snmp_tbl(iopc_struct_type_t type)
{
    return type == STRUCT_TYPE_SNMP_TBL;
}
static inline bool iopc_is_snmp_st(iopc_struct_type_t type)
{
    return type == STRUCT_TYPE_SNMP_TBL || type == STRUCT_TYPE_SNMP_OBJ;
}
static inline const char *iopc_struct_type_to_str(iopc_struct_type_t type)
{
    switch (type) {
      case STRUCT_TYPE_CLASS:
        return "class";
      case STRUCT_TYPE_SNMP_OBJ:
        return "snmpObj";
      case STRUCT_TYPE_UNION:
        return "union";
      case STRUCT_TYPE_TYPEDEF:
        return "typedef";
      case STRUCT_TYPE_STRUCT:
        return "struct";
      case STRUCT_TYPE_SNMP_TBL:
        return "snmpTbl";
      default:
        e_panic("type not handled");
    }
}

typedef enum iopc_defval_t {
    IOPC_DEFVAL_NONE,
    IOPC_DEFVAL_STRING,
    IOPC_DEFVAL_INTEGER,
    IOPC_DEFVAL_DOUBLE,
} iopc_defval_t;

typedef enum iopc_iface_type_t {
    IFACE_TYPE_IFACE        = 0,
    IFACE_TYPE_SNMP_IFACE   = 1,
} iopc_iface_type_t;

static inline bool iopc_is_snmp_iface(iopc_iface_type_t type)
{
    return type == IFACE_TYPE_SNMP_IFACE;
}

typedef struct iopc_field_t {
    int        refcnt;
    uint16_t   size;
    uint8_t    align;
    iopc_struct_type_t type;

    iopc_loc_t loc;

    int tag;

    /* Position in the order of appearance of the struct fields. Starts at
     * zero. Static fields and regular fields positions are computed
     * separately. */
    int field_pos;
    int repeat;
    char *name;

    union {
        uint64_t u64;
        double d;
        void *ptr;
    } defval;
    iopc_defval_t defval_type;
    bool defval_is_signed : 1;
    bool is_visible : 1;
    bool resolving  : 1;
    bool is_static  : 1;
    bool is_ref     : 1;
    /* In case the field is contained by a snmpIface rpc struct', it
     * references another snmpObj field */
    bool snmp_is_from_param : 1;
    /* In case the field is contained by a snmpTbl */
    bool snmp_is_in_tbl : 1;

    /* For IOP²: true for fields of IOP types taken from IOP environment. */
    bool has_external_type : 1;

    /** kind of the resolved type */
    iop_type_t kind;
    /** path of the resolved complex type */
    iopc_path_t *type_path;
    /** package of the resolved complex type */
    iopc_pkg_t *type_pkg;
    /** in case of a typedef, package of the typedef, otherwise same as
     * type_pkg */
    iopc_pkg_t *found_pkg;
    /** definition of the resolved complex type */
    union {
        struct iopc_struct_t *struct_def;
        struct iopc_enum_t   *enum_def;

        /* For IOP², when the field has a type taken from IOP environment. */
        const iop_struct_t *external_st;
        const iop_enum_t   *external_en;
    };
    char *type_name;
    char *pp_type;

    qv_t(iopc_attr) attrs;
    qv_t(iopc_dox)  comments;

    /* In case the field is contained by a snmpIface rpc struct' */
    struct iopc_field_t *field_origin; /* the reference field */
    qv_t(iopc_extends) parents;
} iopc_field_t;
static inline bool iopc_struct_is_field_ignored(const iopc_field_t *field) {
    return field->kind == IOP_T_VOID && field->repeat == IOP_R_REQUIRED;
}
static inline iopc_field_t *iopc_field_init(iopc_field_t *field) {
    p_clear(field, 1);
    field->type = STRUCT_TYPE_TYPEDEF;
    qv_init(&field->attrs);
    qv_init(&field->comments);
    qv_init(&field->parents);
    return field;
}
static inline void iopc_field_wipe(iopc_field_t *field) {
    p_delete(&field->name);
    p_delete(&field->type_name);
    p_delete(&field->pp_type);
    iopc_path_delete(&field->type_path);
    qv_deep_wipe(&field->attrs, iopc_attr_delete);
    qv_deep_wipe(&field->comments, iopc_dox_wipe);
    if (field->defval_type == IOPC_DEFVAL_STRING) {
        p_delete((char **)&field->defval.ptr);
    }
    qv_deep_wipe(&field->parents, iopc_extends_delete);
}
DO_REFCNT(iopc_field_t, iopc_field);
qvector_t(iopc_field, iopc_field_t *);
qm_kptr_t(iopc_field, char, iopc_field_t *, qhash_str_hash, qhash_str_equal);

int iopc_check_field_attributes(iopc_field_t *f, bool tdef);
int iopc_field_add_attr(iopc_field_t *f, iopc_attr_t **attrp, bool tdef);

/* used for the code generation of field attributes */
typedef struct iopc_attrs_t {
    unsigned                 flags;  /**< bitfield of iop_field_attr_type_t */
    uint16_t                 attrs_len;
    lstr_t                   attrs_name;
    lstr_t                   checkf_name;
} iopc_attrs_t;
qvector_t(iopc_attrs, iopc_attrs_t);
GENERIC_INIT(iopc_attrs_t, iopc_attrs);
static inline void iopc_attrs_wipe(iopc_attrs_t *attrs)
{
    lstr_wipe(&attrs->attrs_name);
    lstr_wipe(&attrs->checkf_name);
}


/* Used to detect duplicated ids in an inheritance tree */
qm_k32_t(id_class, struct iopc_struct_t *);

typedef struct iopc_struct_t {
    uint16_t   size;
    uint8_t    align;
    iopc_struct_type_t type;
    iopc_loc_t loc;
    bool       is_visible : 1;
    bool       optimized : 1;
    bool       resolving : 1;
    bool       resolved  : 1;
    bool       resolving_inheritance : 1;
    bool       resolved_inheritance  : 1;
    bool       checked_constraints  : 1;
    bool       has_constraints      : 1;
    bool       has_fields_attrs     : 1;    /**< st.fields_attrs existence  */
    bool       is_abstract          : 1;
    bool       is_local             : 1;
    /* struct has snmpParams attribute */
    bool       is_snmp_params       : 1;
    /* struct is a snmpIface rpc' struct */
    bool       contains_snmp_info : 1;
    /* C writer */
    bool       c_hdr_written : 1;
    unsigned   flags;                       /**< st.flags                   */

    char      *name;
    lstr_t     sig;
    union {
        int    class_id;
        int    oid;
    };
    struct iopc_struct_t *same_as;
    struct iopc_iface_t  *iface;
    qv_t(iopc_field)   fields;

    qv_t(iopc_field)   fields_by_tag;
    qv_t(iopc_field)   fields_in_c_struct_order;

    qv_t(iopc_field)   static_fields;
    int                nb_real_static_fields; /**< those with a defval */
    qv_t(iopc_extends) extends;
    qv_t(iopc_attr)    attrs;
    qv_t(iopc_dox)     comments;

    /* Used for master classes (ie. not having a parent); indexes all the
     * children classes by their id. */
    qm_t(id_class) children_by_id;

    /* IOP description of the struct (used by IOP² when generating IOP
     * descriptions from iopc structures). */
    const iop_struct_t *desc;
} iopc_struct_t;
static inline iopc_struct_t *iopc_struct_init(iopc_struct_t *st) {
    p_clear(st, 1);
    qv_init(&st->fields);
    qv_init(&st->fields_by_tag);
    qv_init(&st->fields_in_c_struct_order);
    qv_init(&st->static_fields);
    qv_init(&st->extends);
    qv_init(&st->attrs);
    qv_init(&st->comments);
    qm_init(id_class, &st->children_by_id);
    return st;
}
static inline void iopc_struct_delete(iopc_struct_t **);
static inline void iopc_iface_delete(struct iopc_iface_t **);
static inline void iopc_struct_wipe(iopc_struct_t *st) {
    qv_deep_wipe(&st->extends, iopc_extends_delete);
    qv_wipe(&st->fields_by_tag);
    qv_wipe(&st->fields_in_c_struct_order);
    qv_deep_wipe(&st->fields, iopc_field_delete);
    qv_deep_wipe(&st->static_fields, iopc_field_delete);
    qv_deep_wipe(&st->attrs, iopc_attr_delete);
    qv_deep_wipe(&st->comments, iopc_dox_wipe);
    p_delete(&st->name);
    lstr_wipe(&st->sig);
    qm_wipe(id_class, &st->children_by_id);
}
GENERIC_NEW(iopc_struct_t, iopc_struct);
GENERIC_DELETE(iopc_struct_t, iopc_struct);
qvector_t(iopc_struct, iopc_struct_t *);
qm_kptr_t(iopc_struct, char, iopc_struct_t *,
          qhash_str_hash, qhash_str_equal);

static inline void iopc_extends_wipe(iopc_extends_t *extends) {
    iopc_path_delete(&extends->path);
    p_delete(&extends->name);
}

typedef struct iopc_enum_field_t {
    iopc_loc_t loc;
    char *name;
    int value;
    qv_t(iopc_attr) attrs;
    qv_t(iopc_dox) comments;
} iopc_enum_field_t;
static inline iopc_enum_field_t *iopc_enum_field_init(iopc_enum_field_t *e) {
    p_clear(e, 1);
    qv_init(&e->attrs);
    qv_init(&e->comments);
    return e;
}
static inline void iopc_enum_field_wipe(iopc_enum_field_t *e) {
    p_delete(&e->name);
    qv_deep_wipe(&e->attrs, iopc_attr_delete);
    qv_deep_wipe(&e->comments, iopc_dox_wipe);
}
GENERIC_NEW(iopc_enum_field_t, iopc_enum_field);
GENERIC_DELETE(iopc_enum_field_t, iopc_enum_field);
qvector_t(iopc_enum_field, iopc_enum_field_t *);

typedef struct iopc_enum_t {
    bool       is_visible : 1;
    iopc_loc_t loc;
    char *name;
    qv_t(iopc_enum_field) values;
    qv_t(iopc_attr)       attrs;
    qv_t(iopc_dox)        comments;

    /* IOP description of the enum (used by IOP² when generating IOP
     * descriptions from iopc enumerations). */
    const iop_enum_t *desc;
} iopc_enum_t;
static inline iopc_enum_t *iopc_enum_init(iopc_enum_t *e) {
    p_clear(e, 1);
    qv_init(&e->values);
    qv_init(&e->attrs);
    qv_init(&e->comments);
    return e;
}
static inline void iopc_enum_wipe(iopc_enum_t *e) {
    qv_deep_wipe(&e->values, iopc_enum_field_delete);
    qv_deep_wipe(&e->attrs, iopc_attr_delete);
    qv_deep_wipe(&e->comments, iopc_dox_wipe);
    p_delete(&e->name);
}
GENERIC_NEW(iopc_enum_t, iopc_enum);
GENERIC_DELETE(iopc_enum_t, iopc_enum);
qvector_t(iopc_enum, iopc_enum_t *);

typedef enum iopc_fun_struct_type_t {
    IOPC_FUN_ARGS,
    IOPC_FUN_RES,
    IOPC_FUN_EXN,
} iopc_fun_struct_type_t;

/* Argument, result or exception type for a given RPC. */
typedef struct iopc_fun_struct_t {
    union {
        iopc_struct_t *anonymous_struct;
        iopc_field_t *existing_struct;
    };
    bool is_anonymous;
} iopc_fun_struct_t;

static inline bool iopc_fun_struct_is_void(const iopc_fun_struct_t *fun_st)
{
    return !fun_st->anonymous_struct;
}

typedef struct iopc_fun_t {
    iopc_loc_t loc;
    int        tag;
    int        pos; /* To sort funs by order of appearance in iface. */
    char      *name;

    bool fun_is_async;

    iopc_fun_struct_t arg;
    iopc_fun_struct_t res;
    iopc_fun_struct_t exn;

    qv_t(iopc_attr) attrs;
    qv_t(iopc_dox)  comments;
} iopc_fun_t;
static inline iopc_fun_t *iopc_fun_init(iopc_fun_t *fun) {
    p_clear(fun, 1);
    qv_init(&fun->attrs);
    qv_init(&fun->comments);
    return fun;
}
static inline void iopc_fun_struct_wipe(iopc_fun_struct_t *fun_st) {
    if (fun_st->is_anonymous) {
        iopc_struct_delete(&fun_st->anonymous_struct);
    } else {
        iopc_field_delete(&fun_st->existing_struct);
    }
}
static inline void iopc_fun_wipe(iopc_fun_t *fun) {
    iopc_fun_struct_wipe(&fun->arg);
    iopc_fun_struct_wipe(&fun->res);
    iopc_fun_struct_wipe(&fun->exn);
    p_delete(&fun->name);
    qv_deep_wipe(&fun->attrs, iopc_attr_delete);
    qv_deep_wipe(&fun->comments, iopc_dox_wipe);
}
GENERIC_NEW(iopc_fun_t, iopc_fun);
GENERIC_DELETE(iopc_fun_t, iopc_fun);
qvector_t(iopc_fun, iopc_fun_t *);
qm_kptr_t(iopc_fun, char, iopc_fun_t *, qhash_str_hash, qhash_str_equal);

typedef struct iopc_iface_t {
    bool       is_visible : 1;
    iopc_loc_t loc;
    unsigned   flags;

    char *name;
    qv_t(iopc_fun)  funs;
    qv_t(iopc_attr) attrs;
    qv_t(iopc_dox)  comments;

    /* Used only for snmpIface*/
    iopc_iface_type_t type;
    int oid;
    qv_t(iopc_extends) extends;
} iopc_iface_t;
static inline iopc_iface_t *iopc_iface_init(iopc_iface_t *iface) {
    p_clear(iface, 1);
    qv_init(&iface->funs);
    qv_init(&iface->attrs);
    qv_init(&iface->comments);
    qv_init(&iface->extends);
    return iface;
}
static inline void iopc_iface_wipe(iopc_iface_t *iface) {
    qv_deep_wipe(&iface->funs, iopc_fun_delete);
    qv_deep_wipe(&iface->attrs, iopc_attr_delete);
    qv_deep_wipe(&iface->comments, iopc_dox_wipe);
    qv_deep_wipe(&iface->extends, iopc_extends_delete);
    p_delete(&iface->name);
}
GENERIC_NEW(iopc_iface_t, iopc_iface);
GENERIC_DELETE(iopc_iface_t, iopc_iface);
qvector_t(iopc_iface, iopc_iface_t *);
qh_khptr_t(iopc_pkg, iopc_pkg_t);

struct iopc_pkg_t {
    bool t_resolving : 1;
    bool i_resolving : 1;
    bool t_resolved  : 1;
    bool i_resolved  : 1;

    char        *file;
    char        *base;
    iopc_path_t *name;

    qv_t(iopc_enum)   enums;
    qv_t(iopc_struct) structs;
    qv_t(iopc_iface)  ifaces;
    qv_t(iopc_struct) modules;
    qv_t(iopc_field)  typedefs;
    qv_t(iopc_dox)    comments;
    qh_t(iopc_pkg)    deps;
};
static inline iopc_pkg_t *iopc_pkg_init(iopc_pkg_t *pkg) {
    p_clear(pkg, 1);
    qv_init(&pkg->enums);
    qv_init(&pkg->structs);
    qv_init(&pkg->modules);
    qv_init(&pkg->ifaces);
    qv_init(&pkg->typedefs);
    qv_init(&pkg->comments);
    qh_init(iopc_pkg, &pkg->deps);
    return pkg;
}
static inline void iopc_pkg_wipe(iopc_pkg_t *pkg) {
    qh_wipe(iopc_pkg, &pkg->deps);
    qv_deep_wipe(&pkg->ifaces, iopc_iface_delete);
    qv_deep_wipe(&pkg->structs, iopc_struct_delete);
    qv_deep_wipe(&pkg->modules, iopc_struct_delete);
    qv_deep_wipe(&pkg->enums, iopc_enum_delete);
    qv_deep_wipe(&pkg->typedefs, iopc_field_delete);
    qv_deep_wipe(&pkg->comments, iopc_dox_wipe);
    iopc_path_delete(&pkg->name);
    p_delete(&pkg->file);
    p_delete(&pkg->base);
}
GENERIC_NEW(iopc_pkg_t, iopc_pkg);
GENERIC_DELETE(iopc_pkg_t, iopc_pkg);
qvector_t(iopc_pkg, iopc_pkg_t *);
qm_kptr_ckey_t(iopc_pkg, char, iopc_pkg_t *, qhash_str_hash, qhash_str_equal);

/*----- pretty printing  -----*/

const char *t_pretty_token(iopc_tok_type_t token);
const char *pretty_path(iopc_path_t *path);
const char *pretty_path_dot(iopc_path_t *path);
static inline const char *pretty_path_base(iopc_path_t *path) {
    return path->bits.tab[path->bits.len - 1];
}


/*----- parser & typer -----*/

qm_kptr_ckey_t(iopc_env, char, char *, qhash_str_hash, qhash_str_equal);

void iopc_parser_initialize(void);
void iopc_parser_shutdown(void);
iopc_pkg_t *iopc_parse_file(qv_t(cstr) *includes,
                            const qm_t(iopc_env) *env, const char *file,
                            const char *data, bool is_main_pkg);

void iopc_typer_initialize(void);
void iopc_typer_shutdown(void);
int iopc_resolve(iopc_pkg_t *pkg);
int iopc_resolve_second_pass(iopc_pkg_t *pkg);
void iopc_types_fold(iopc_pkg_t *pkg);
void iopc_depends_uniquify(qv_t(iopc_pkg) *deps);

/** Flags to be used by iopc_pkg_get_deps(). */
typedef enum iopc_pkg_get_deps_flags_t {
    /** Include the iface dependencies. */
    IOPC_PKG_GET_DEPS_INCLUDE_IFACES          = 1 << 0,

    /** Include the SNMP structures dependencies. */
    IOPC_PKG_GET_DEPS_INCLUDE_SNMP            = 1 << 1,

    /** Include the dependencies of all ancestors of the classes. */
    IOPC_PKG_GET_DEPS_INCLUDE_CLASS_ANCESTORS = 1 << 2,

    /** Include all possible dependencies. */
    IOPC_PKG_GET_DEPS_INCLUDE_ALL = IOPC_PKG_GET_DEPS_INCLUDE_IFACES
                                  | IOPC_PKG_GET_DEPS_INCLUDE_SNMP
                                  | IOPC_PKG_GET_DEPS_INCLUDE_CLASS_ANCESTORS,
} iopc_pkg_get_deps_flags_t;

/** For a given IOPC package, list the other IOPC packages it depends on.
 *
 * \param[in] flags  Flags from \p iopc_pkg_get_deps_flags_t.

 * \param[out] t_deps  Dependencies for which complete types definitions
 *                     will be needed. These complete definitions are meant to
 *                     be found in header files "<package>-t.iop.h".
 *
 * \param[out] t_weak_deps  Dependencies for which only forward types
 *                          declarations are needed (for example when a struct
 *                          attribute is a pointer).
 *                          These definitions are meant to be found in header
 *                          files "<package>-t.iop.h".
 *
 * \param[out] i_deps  Dependencies induced by interfaces definitions.
 *                     Interfaces and modules stuff are not dumped in the same
 *                     header as structs/union/classes so their dependencies
 *                     are listed separately.
 */
void iopc_pkg_get_deps(iopc_pkg_t *pkg, unsigned flags,
                       qv_t(iopc_pkg) *t_deps, qv_t(iopc_pkg) *t_weak_deps,
                       qv_t(iopc_pkg) *i_deps);

/** Check that the name is valid to use as an IOP type */
int iopc_check_type_name(lstr_t name, sb_t * nullable err);

/** Check that the name is valid to use as an IOP field name */
int iopc_check_field_name(lstr_t name, sb_t * nullable err);

static inline void iopc_parser_typer_initialize(void)
{
    iopc_parser_initialize();
    iopc_typer_initialize();
}

static inline void iopc_parser_typer_shutdown(void)
{
    iopc_parser_shutdown();
    iopc_typer_shutdown();
}

/*----- utilities -----*/

int iopc_field_get_signed(const iopc_field_t *f, bool *is_signed);

/*----- writing output files -----*/

#define DOUBLE_FMT  "%.17e"

#define IOPC_ATTR_GET_ARG_V(_type, _a)  \
    ((_a)->type == ITOK_INTEGER || (_a)->type == ITOK_BOOL ? \
     (_type)(_a)->v.i64 : (_type)(_a)->v.d)

typedef struct iopc_write_buf_t {
    sb_t *buf;
    sb_t *tab; /* for tabulations in start of lines */
} iopc_write_buf_t;

#define IOPC_WRITE_START_LINE(_wbuf, _fct, _fmt, ...)  \
    do {                                               \
        const iopc_write_buf_t *__b = (_wbuf);         \
        sb_addsb(__b->buf, __b->tab);                  \
        _fct(__b->buf, _fmt, ##__VA_ARGS__);           \
    } while (0)

#define IOPC_WRITE_START_LINE_CSTR(_wbuf, _str)  \
    IOPC_WRITE_START_LINE((_wbuf), sb_add_lstr, LSTR(_str))

void iopc_write_buf_init(iopc_write_buf_t *wbuf, sb_t *buf, sb_t *tab);

void iopc_write_buf_tab_inc(const iopc_write_buf_t *);

void iopc_write_buf_tab_dec(const iopc_write_buf_t *);

int
iopc_set_path(const char *outdir, const iopc_pkg_t *pkg,
              const char *ext, int max_len, char *path, bool only_pkg);

int iopc_write_file(const sb_t *buf, const char *path);

/*----- language backends -----*/

extern struct iopc_do_c_globs {
    bool resolve_includes;

    /** remove const on all objects that may contain a pointer to an
     * iop_struct_t */
    bool no_const;

    /** Use iop compat header in memory instead of lib-common/core.h +
     *  lib-common/iop-internals.h
     */
    const char *iop_compat_header;
} iopc_do_c_g;

extern struct iopc_do_typescript_globs {
    /** Enable IOP-Backbone model generation.
     */
    bool enable_iop_backbone;
} iopc_do_typescript_g;

int iopc_do_c(iopc_pkg_t *pkg, const char *outdir);
int iopc_do_json(iopc_pkg_t *pkg, const char *outdir);
int iopc_do_typescript(iopc_pkg_t *pkg, const char *outdir);

/*----- IOPC DSO -----*/

/** Specify the class id range used when building IOP DSO with iopc_dso_load.
 */
void iopc_dso_set_class_id_range(uint16_t class_id_min,
                                 uint16_t class_id_max);

/** Build an IOP DSO.
 *
 * \param[in] pfxdir       prefix directory of the IOP file to compile
 * \param[in] display_pfx  set to false if only the relative part of the files
 *                         should be printed in case of error
 * \param[in] iopfile      the IOP file to compile; this path must be relative
 *                         to \p pfxdir
 * \param[in] env          a map of buffered IOP files (dependencies); the paths
 *                         of these dependencies must be relative to \p pfxdir
 * \param[in] outdir       the absolute path of the directory to store the IOP DSO
 *                         file (outdir/pkgname.so)
 * \param[out] err         buffer filled in case of error
 *
 * \return             0 if ok, -1 if the build failed
 */
int iopc_dso_build(const char *pfxdir, bool display_pfx,
                   const char *iopfile, const qm_t(iopc_env) *env,
                   const char *outdir, sb_t *err);

/** Get iop type from a type name.
 *
 * Note that it's not possible to detect UNION and ENUM from a type name, so
 * this function returns STRUCT for both.
 */
iop_type_t iop_get_type(lstr_t name);

MODULE_DECLARE(iopc_dso);
MODULE_DECLARE(iopc_lang_c);
MODULE_DECLARE(iopc);

#endif
