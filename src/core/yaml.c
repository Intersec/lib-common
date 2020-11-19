/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

#include <math.h>

#include <lib-common/yaml.h>
#include <lib-common/parsing-helpers.h>
#include <lib-common/log.h>
#include <lib-common/file.h>
#include <lib-common/unix.h>
#include <lib-common/iop.h>
#include <lib-common/iop-json.h>

static struct yaml_g {
    logger_t logger;
} yaml_g = {
#define _G yaml_g
    .logger = LOGGER_INIT(NULL, "yaml", LOG_INHERITS),
};

/* {{{ Parsing types definitions */
/* {{{ Presentation */

qm_kvec_t(yaml_pres_node, lstr_t, const yaml__presentation_node__t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

/* This is a yaml.DocumentPresentation transformed into a hashmap. */
typedef struct yaml_presentation_t {
    qm_t(yaml_pres_node) nodes;
} yaml_presentation_t;

/* Presentation details currently being constructed */
typedef struct yaml_env_presentation_t {
    /* Presentation node of the last parsed element.
     *
     * This can point to:
     *  * the node of the last parsed yaml_data_t object.
     *  * the node of a sequence element.
     *  * the node of an object key.
     *
     * It can be NULL at the very beginning of the document.
     */
    yaml__presentation_node__t * nonnull * nullable last_node;

    /* Presentation detail for the next element to generate.
     *
     * When parsing presentation data that applies to the next element (for
     * example, with prefix comments), this element is filled, and retrieved
     * when the next element is created.
     */
    yaml__presentation_node__t * nullable next_node;
} yaml_env_presentation_t;

/* }}} */
/* {{{ Variables */

typedef struct yaml_variable_t {
    /* FIXME: keeping a raw pointer on a yaml_data_t is very flimsy, and this
     * design should be reworked.
     * For example, it prevents properly handling variables in overrides.
     */
    /* Data using the variable. The set value for the variable will be set
     * in the data, or replace it. */
    yaml_data_t *data;

    /* Is the variable in a string, or raw?
     *
     * Raw means any AST is valid:
     *
     * foo: $bar
     *
     * In string means it must be a string value, and will be set in the
     * data:
     *
     * addr: "$host:ip"
     */
    bool in_string;

    /* Bitmap indicating which '$' char are variables.
     *
     * If the template has escaped some '$' characters, we will end up with
     * a string with multiple '$', some being variables, some not. This bitmap
     * allows finding out which one are variables.
     *
     * For example:
     * "$a \\$b \\\\$$c" will give the string "$a $b \\$$c", and the bitmap
     * [0x09] (first and fourth '$' are variables).
     *
     * \warning If the bitmap is NOT set (ie len == 0), then it means every
     * '$' is a variable.
     */
    qv_t(u8) var_bitmap;
} yaml_variable_t;
qvector_t(yaml_variable, yaml_variable_t * nonnull);

qm_kvec_t(yaml_vars, lstr_t, qv_t(yaml_variable), qhash_lstr_hash,
          qhash_lstr_equal);

/* }}} */

qvector_t(yaml_parse, yaml_parse_t *);

typedef struct yaml_included_file_t {
    /* Parsing context that included the current file. */
    const yaml_parse_t * nonnull parent;

    /** Data from the including file, that caused the inclusion.
     *
     * This is the "!include <file>" data. It is not stored in the including
     * yaml_parse_t object, as the inclusion is transparent in its AST.
     * However, it is stored here to provide proper error messages.
     */
    yaml_data_t data;
} yaml_included_file_t;

typedef struct yaml_parse_t {
    /* String to parse. */
    pstream_t ps;

    /* Name of the file being parsed.
     *
     * This is the name of the file as it was given to yaml_parse_attach_file.
     * It can be an absolute or a relative path.
     *
     * NULL if a stream is being parsed.
     */
    const char * nullable filepath;

    /* Path to the "root" directory.
     *
     * The "root" directory is the directory of the initial file imported.
     * All includes are allowed inside this directory, so that includes from
     * sibling directories is allowed, for example:
     *
     * toto/ <-- this is the root directory
     *   root.yml <-- first file included
     *   sub1/
     *     a.yml <-- can include ../sub2/b.yml
     *   sub2/
     *     b.yml
     */
    const char * nullable rootdirpath;

    /* Fullpath to the file being parsed.
     *
     * LSTR_NULL_V if a stream is being parsed.
     */
    lstr_t fullpath;

    /* mmap'ed contents of the file. */
    lstr_t file_contents;

    /* Bitfield of yaml_parse_flags_t elements. */
    int flags;

    /* Current line number. */
    uint32_t line_number;

    /* Pointer to the first character of the current line.
     * Used to compute current column number of ps->s */
    const char *pos_newline;

    /* Error buffer. */
    sb_t err;

    /* Presentation details.
     *
     * Can be NULL if the user did not asked for presentation details.
     */
    yaml_env_presentation_t * nullable pres;

    /* Included files.
     *
     * List of parse context of every subfiles included. */
    qv_t(yaml_parse) subfiles;

    /* Included details.
     *
     * This is set if the current file was included from another file.
     */
    yaml_included_file_t * nullable included;

    qm_t(yaml_vars) variables;
} yaml_parse_t;

qvector_t(override_nodes, yaml__presentation_override_node__t);

/* Presentation details of an override.
 *
 * This object is used to build a yaml.PresentationOverride. See the document
 * of this object for more details.
 */
typedef struct yaml_presentation_override_t {
    /* List of nodes of the override. */
    qv_t(override_nodes) nodes;

    /* Current path from the override root point.
     *
     * Describe the path not from the document's root, but from the override's
     * root. Used to build the nodes.
     */
    sb_t path;
} yaml_presentation_override_t;

/* Node to override, when packing. */
typedef struct yaml_pack_override_node_t {
    /* Data related to the override.
     *
     * When beginning to pack an override, this is set to the original data,
     * the one replaced by the override on parsing.
     * When the AST is packed, this data is retrieved and the overridden data
     * is swapped and stored here.
     * Then, the override data is packed using those datas.
     */
    const yaml_data_t * nullable data;

    /* If the data has been found and retrieved. If false, the data has either
     * not yet been found while packing the AST, or the node has disappeared
     * from the AST.
     */
    bool found;
} yaml_pack_override_node_t;

qm_kvec_t(override_nodes, lstr_t, yaml_pack_override_node_t,
          qhash_lstr_hash, qhash_lstr_equal);

/* Description of an override, used when packing.
 *
 * This object is used to properly pack overrides. It is the equivalent of
 * a yaml.PresentationOverride, but with a qm instead of an array, so that
 * nodes that were overridden can easily find the original value to repack.
 */
typedef struct yaml_pack_override_t {
    /* Mappings of absolute paths to override pack nodes.
     *
     * The paths are not the same as the ones in the presentation IOP. They
     * are absolute path, from the root document and not from the override's
     * root.
     *
     * This is needed in order to handle overrides of included data. The
     * path in the included data is relative from the root of its file, which
     * may be different from the path of the override nodes (for example,
     * if the override was done in a file that included a file including the
     * current file.
     * */
    qm_t(override_nodes) nodes;

    /** List of the absolute paths.
     *
     * Used to iterate on the nodes in the right order when repacking the
     * override objet.
     */
    qv_t(lstr) ordered_paths;

    /* Original override presentation object.
     */
    const yaml__presentation_override__t * nonnull presentation;
} yaml_pack_override_t;
qvector_t(pack_override, yaml_pack_override_t);

/* }}} */
/* {{{ IOP helpers */
/* {{{ IOP scalar */

static void
t_yaml_data_to_iop(const yaml_data_t * nonnull data,
                   yaml__data__t * nonnull out)
{
    yaml__scalar_value__t *scalar;

    out->tag = data->tag;

    /* TODO: for the moment, only scalars can be overridden, so only scalars
     * needs to be serialized. Once overrides can replace any data, this
     * function will have to be modified */
    assert (data->type == YAML_DATA_SCALAR);
    scalar = IOP_UNION_SET(yaml__data_value, &out->value, scalar);

    switch (data->scalar.type) {
#define CASE(_name, _prefix)                                                 \
      case YAML_SCALAR_##_name:                                              \
        *IOP_UNION_SET(yaml__scalar_value, scalar, _prefix)                  \
            = data->scalar._prefix;                                          \
        break

      CASE(STRING, s);
      CASE(DOUBLE, d);
      CASE(UINT, u);
      CASE(INT, i);
      CASE(BOOL, b);
      CASE(BYTES, s);

#undef CASE

      case YAML_SCALAR_NULL:
        IOP_UNION_SET_V(yaml__scalar_value, scalar, nil);
        break;
    }
}

static void
t_iop_data_to_yaml(const yaml__data__t * nonnull data,
                   yaml_data_t * nonnull out)
{
    assert (IOP_UNION_IS(yaml__data_value, &data->value, scalar));

    IOP_UNION_SWITCH(&data->value.scalar) {
      IOP_UNION_CASE(yaml__scalar_value, &data->value.scalar, s, s) {
        yaml_data_set_string(out, s);
      }
      IOP_UNION_CASE(yaml__scalar_value, &data->value.scalar, d, d) {
        yaml_data_set_double(out, d);
      }
      IOP_UNION_CASE(yaml__scalar_value, &data->value.scalar, u, u) {
        yaml_data_set_uint(out, u);
      }
      IOP_UNION_CASE(yaml__scalar_value, &data->value.scalar, i, i) {
        yaml_data_set_int(out, i);
      }
      IOP_UNION_CASE(yaml__scalar_value, &data->value.scalar, b, b)  {
        yaml_data_set_bool(out, b);
      }
      IOP_UNION_CASE_V(yaml__scalar_value, &data->value.scalar, nil)  {
        yaml_data_set_null(out);
      }
      IOP_UNION_CASE(yaml__scalar_value, &data->value.scalar, data, bytes) {
        yaml_data_set_bytes(out, bytes);
      }
    }
    out->tag = data->tag;
}

/* }}} */
/* {{{ IOP Presentation Override */

static yaml__presentation_override__t * nonnull
t_presentation_override_to_iop(
    const yaml_presentation_override_t * nonnull pres,
    const yaml_data_t * nonnull override_data
)
{
    yaml__presentation_override__t *out;

    out = t_iop_new(yaml__presentation_override);
    out->nodes = IOP_TYPED_ARRAY_TAB(yaml__presentation_override_node,
                                     &pres->nodes);

    return out;
}

/* }}} */
/* }}} */
/* {{{ Equality */

/* XXX: canonical string version of a scalar. Used for weak comparisons
 * between different types of scalars */
static lstr_t
t_yaml_scalar_to_string(const yaml_scalar_t * nonnull scalar)
{
    switch (scalar->type) {
      case YAML_SCALAR_STRING:
      case YAML_SCALAR_BYTES:
        return scalar->s;

      case YAML_SCALAR_DOUBLE: {
        int inf = isinf(scalar->d);

        if (inf == 1) {
            return LSTR(".Inf");
        } else
        if (inf == -1) {
            return LSTR("-.Inf");
        } else
        if (isnan(scalar->d)) {
            return LSTR(".NaN");
        } else {
            return t_lstr_fmt("%g", scalar->d);
        }
      } break;

      case YAML_SCALAR_UINT:
        return t_lstr_fmt("%ju", scalar->u);

      case YAML_SCALAR_INT:
        return t_lstr_fmt("%jd", scalar->i);

      case YAML_SCALAR_BOOL:
        return scalar->b ? LSTR("true") : LSTR("false");

      case YAML_SCALAR_NULL:
        return LSTR("~");
    }

    assert (false);
    return LSTR("");
}

static bool
yaml_scalar_equals(const yaml_scalar_t * nonnull s1,
                   const yaml_scalar_t * nonnull s2, bool strong)
{
    if (!strong) {
        t_scope;
        lstr_t v1 = t_yaml_scalar_to_string(s1);
        lstr_t v2 = t_yaml_scalar_to_string(s2);

        return lstr_equal(v1, v2);
    }

    if (s1->type != s2->type) {
        return false;
    }

    switch (s1->type) {
      case YAML_SCALAR_STRING:
      case YAML_SCALAR_BYTES:
        return lstr_equal(s1->s, s2->s);
      case YAML_SCALAR_DOUBLE:
        return memcmp(&s1->d, &s2->d, sizeof(double)) == 0;
      case YAML_SCALAR_UINT:
        return s1->u == s2->u;
      case YAML_SCALAR_INT:
        return s1->i == s2->i;
      case YAML_SCALAR_BOOL:
        return s1->b == s2->b;
      case YAML_SCALAR_NULL:
        return true;
    }

    return false;
}

static bool
yaml_data_equals(const yaml_data_t * nonnull d1,
                 const yaml_data_t * nonnull d2, bool strong);

static bool
yaml_key_data_equals(const yaml_key_data_t * nonnull kd1,
                     const yaml_key_data_t * nonnull kd2, bool strong)
{
    return lstr_equal(kd1->key, kd2->key)
        && yaml_data_equals(&kd1->data, &kd2->data, strong);
}

/* Compare two yaml data recursively.
 *
 * Comparison can be weak or strong.
 * If strong, scalars must have the exact same type.
 * If weak, scalars are considered equal if their string representations are
 * the same.
 */
static bool
yaml_data_equals(const yaml_data_t * nonnull d1,
                 const yaml_data_t * nonnull d2, bool strong)
{
    if (d1->type != d2->type) {
        return false;
    }

    switch (d1->type) {
      case YAML_DATA_SCALAR:
        return yaml_scalar_equals(&d1->scalar, &d2->scalar, strong);
      case YAML_DATA_SEQ:
        if (d1->seq->datas.len != d2->seq->datas.len) {
            return false;
        }
        tab_for_each_pos(pos, &d1->seq->datas) {
            if (!yaml_data_equals(&d1->seq->datas.tab[pos],
                                  &d2->seq->datas.tab[pos], strong))
            {
                return false;
            }
        }
        break;
      case YAML_DATA_OBJ:
        if (d1->obj->fields.len != d2->obj->fields.len) {
            return false;
        }
        tab_for_each_pos(pos, &d1->obj->fields) {
            if (!yaml_key_data_equals(&d1->obj->fields.tab[pos],
                                      &d2->obj->fields.tab[pos], strong))
            {
                return false;
            }
        }
        break;
    }

    return true;
}

/* }}} */
/* {{{ Utils */

static const char * nonnull
yaml_scalar_get_type(const yaml_scalar_t * nonnull scalar, bool has_tag)
{
    switch (scalar->type) {
      case YAML_SCALAR_STRING:
        return has_tag ? "a tagged string value" : "a string value";
      case YAML_SCALAR_DOUBLE:
        return has_tag ? "a tagged double value" : "a double value";
      case YAML_SCALAR_UINT:
        return has_tag ? "a tagged unsigned integer value"
                       : "an unsigned integer value";
      case YAML_SCALAR_INT:
        return has_tag ? "a tagged integer value" : "an integer value";
      case YAML_SCALAR_BOOL:
        return has_tag ? "a tagged boolean value" : "a boolean value";
      case YAML_SCALAR_NULL:
        return has_tag ? "a tagged null value" : "a null value";
      case YAML_SCALAR_BYTES:
        return "a binary value";
    }

    assert (false);
    return "";
}

const char *yaml_data_get_type(const yaml_data_t *data, bool ignore_tag)
{
    bool has_tag = data->tag.s && !ignore_tag;

    switch (data->type) {
      case YAML_DATA_OBJ:
        return has_tag ? "a tagged object" : "an object";
      case YAML_DATA_SEQ:
        return has_tag ? "a tagged sequence" : "a sequence";
      case YAML_DATA_SCALAR:
        return yaml_scalar_get_type(&data->scalar, has_tag);
    }

    assert (false);
    return "";
}

static const char *yaml_data_get_data_type(const yaml_data_t *data)
{
    switch (data->type) {
      case YAML_DATA_OBJ:
        return "an object";
      case YAML_DATA_SEQ:
        return "a sequence";
      case YAML_DATA_SCALAR:
        return "a scalar";
    }

    assert (false);
    return "";
}

lstr_t yaml_span_to_lstr(const yaml_span_t * nonnull span)
{
    return LSTR_PTR_V(span->start.s, span->end.s);
}

static uint32_t yaml_env_get_column_nb(const yaml_parse_t *env)
{
    return env->ps.s - env->pos_newline + 1;
}

static yaml_pos_t yaml_env_get_pos(const yaml_parse_t *env)
{
    return (yaml_pos_t){
        .line_nb = env->line_number,
        .col_nb = yaml_env_get_column_nb(env),
        .s = env->ps.s,
    };
}

static inline void yaml_env_skipc(yaml_parse_t *env)
{
    IGNORE(ps_getc(&env->ps));
}

static void yaml_span_init(yaml_span_t * nonnull span,
                           const yaml_parse_t * nonnull env,
                           yaml_pos_t pos_start, yaml_pos_t pos_end)
{
    p_clear(span, 1);
    span->start = pos_start;
    span->end = pos_end;
    span->env = env;
}

static void
yaml_env_start_data_with_pos(yaml_parse_t * nonnull env,
                             yaml_data_type_t type, yaml_pos_t pos_start,
                             yaml_data_t * nonnull out)
{
    p_clear(out, 1);
    out->type = type;
    yaml_span_init(&out->span, env, pos_start, pos_start);

    if (env->pres && env->pres->next_node) {
        /* Get the saved presentation details that were stored for the next
         * data (ie this one).
         */
        out->presentation = env->pres->next_node;
        env->pres->next_node = NULL;

        logger_trace(&_G.logger, 2, "adding prefixed presentation details "
                     "for data starting at "YAML_POS_FMT,
                     YAML_POS_ARG(pos_start));
    }
}

static void
yaml_env_start_data(yaml_parse_t * nonnull env, yaml_data_type_t type,
                    yaml_data_t * nonnull out)
{
    yaml_env_start_data_with_pos(env, type, yaml_env_get_pos(env), out);
}

static void
yaml_env_end_data_with_pos(yaml_parse_t * nonnull env, yaml_pos_t pos_end,
                           yaml_data_t * nonnull out)
{
    out->span.end = pos_end;

    if (env->pres) {
        env->pres->last_node = &out->presentation;
    }
}

static void
yaml_env_end_data(yaml_parse_t * nonnull env, yaml_data_t * nonnull out)
{
    yaml_env_end_data_with_pos(env, yaml_env_get_pos(env), out);
}

/* }}} */
/* {{{ Var bitmap utils */

/* Var bitmap is a bitmap indicating which '$' characters are variables.
 *
 * The rules are:
 *  * if len is 0, then it means all '$' are variables (ie, the bitmap is full
 *    1's).
 *  * otherwise, only set bits are variables. OOB accessing is allowed, it
 *    just means it is evaluated to 0.
 */

static void var_bitmap_set_bit(qv_t(u8) * nonnull bitmap, int pos)
{
    if (bitmap->len * 8 <= pos) {
        qv_growlen0(bitmap, pos / 8 + 1 - bitmap->len);
    }
    SET_BIT(bitmap->tab, pos);
}

static bool var_bitmap_test_bit(const qv_t(u8) * nonnull bitmap, int pos)
{
    if (bitmap->len == 0) {
        return true;
    }
    if (pos < bitmap->len * 8 && TST_BIT(bitmap->tab, pos)) {
        return true;
    }
    return false;
}

/* }}} */
/* {{{ Errors */

typedef enum yaml_error_t {
    YAML_ERR_BAD_KEY,
    YAML_ERR_BAD_STRING,
    YAML_ERR_MISSING_DATA,
    YAML_ERR_WRONG_DATA,
    YAML_ERR_WRONG_INDENT,
    YAML_ERR_WRONG_OBJECT,
    YAML_ERR_TAB_CHARACTER,
    YAML_ERR_INVALID_TAG,
    YAML_ERR_EXTRA_DATA,
    YAML_ERR_INVALID_INCLUDE,
    YAML_ERR_INVALID_OVERRIDE,
    YAML_ERR_INVALID_VAR,
    YAML_ERR_FORBIDDEN_VAR,
} yaml_error_t;

static int yaml_env_set_err_at(yaml_parse_t * nonnull env,
                               const yaml_span_t * nonnull span,
                               yaml_error_t type, const char * nonnull msg)
{
    SB_1k(err);

    switch (type) {
      case YAML_ERR_BAD_KEY:
        sb_addf(&err, "invalid key, %s", msg);
        break;
      case YAML_ERR_BAD_STRING:
        sb_addf(&err, "expected string, %s", msg);
        break;
      case YAML_ERR_MISSING_DATA:
        sb_addf(&err, "missing data, %s", msg);
        break;
      case YAML_ERR_WRONG_DATA:
        sb_addf(&err, "wrong type of data, %s", msg);
        break;
      case YAML_ERR_WRONG_INDENT:
        sb_addf(&err, "wrong indentation, %s", msg);
        break;
      case YAML_ERR_WRONG_OBJECT:
        sb_addf(&err, "wrong object, %s", msg);
        break;
      case YAML_ERR_TAB_CHARACTER:
        sb_addf(&err, "tab character detected, %s", msg);
        break;
      case YAML_ERR_INVALID_TAG:
        sb_addf(&err, "invalid tag, %s", msg);
        break;
      case YAML_ERR_EXTRA_DATA:
        sb_addf(&err, "extra characters after data, %s", msg);
        break;
      case YAML_ERR_INVALID_INCLUDE:
        sb_addf(&err, "invalid include, %s", msg);
        break;
      case YAML_ERR_INVALID_OVERRIDE:
        sb_addf(&err, "cannot change types of data in override, %s", msg);
        break;
      case YAML_ERR_INVALID_VAR:
        sb_addf(&err, "invalid variable, %s", msg);
        break;
      case YAML_ERR_FORBIDDEN_VAR:
        sb_addf(&err, "use of variables is forbidden, %s", msg);
        break;
    }

    yaml_parse_pretty_print_err(span, LSTR_SB_V(&err), &env->err);

    return -1;
}

static int yaml_env_set_err(yaml_parse_t * nonnull env, yaml_error_t type,
                            const char * nonnull msg)
{
    yaml_span_t span;
    yaml_pos_t start;
    yaml_pos_t end;


    /* build a span on the current position, to have a cursor on this
     * character in the pretty printed error message. */
    start = yaml_env_get_pos(env);
    end = start;
    end.col_nb++;
    end.s++;
    yaml_span_init(&span, env, start, end);

    return yaml_env_set_err_at(env, &span, type, msg);
}

/* }}} */
/* {{{ Parser */

static int t_yaml_env_parse_data(yaml_parse_t *env, const uint32_t min_indent,
                                 yaml_data_t *out);

/* {{{ Presentation utils */

static yaml__presentation_node__t * nonnull
t_yaml_env_pres_get_current_node(yaml_env_presentation_t * nonnull pres)
{
    /* last_node should be set, otherwise this means we are at the very
     * beginning of the document, and we should parse presentation data
     * as prefix rather than inline. */
    assert (pres->last_node);
    if (!(*pres->last_node)) {
        *pres->last_node = t_iop_new(yaml__presentation_node);
    }
    return *pres->last_node;
}

static yaml__presentation_node__t * nonnull
t_yaml_env_pres_get_next_node(yaml_env_presentation_t * nonnull pres)
{
    if (!pres->next_node) {
        pres->next_node = t_iop_new(yaml__presentation_node);
    }

    return pres->next_node;
}

static void t_yaml_env_handle_comment_ps(yaml_parse_t * nonnull env,
                                         pstream_t comment_ps, bool prefix,
                                         qv_t(lstr) * nonnull prefix_comments)
{
    lstr_t comment;

    if (!env->pres) {
        return;
    }

    comment_ps.s_end = env->ps.s;
    ps_skipc(&comment_ps, '#');
    comment = lstr_trim(LSTR_PS_V(&comment_ps));

    if (prefix) {
        if (prefix_comments->len == 0) {
            t_qv_init(prefix_comments, 1);
        }
        qv_append(prefix_comments, comment);
        logger_trace(&_G.logger, 2, "adding prefix comment `%pL`", &comment);
    } else {
        yaml__presentation_node__t *pnode;

        pnode = t_yaml_env_pres_get_current_node(env->pres);
        assert (pnode->inline_comment.len == 0);
        pnode->inline_comment = comment;
        if (env->pres->last_node) {
            logger_trace(&_G.logger, 2, "adding inline comment `%pL`",
                         &comment);
        }
    }
}

static void
yaml_env_set_prefix_comments(yaml_parse_t * nonnull env,
                             qv_t(lstr) * nonnull prefix_comments)
{
    yaml__presentation_node__t *pnode;

    if (!env->pres || prefix_comments->len == 0) {
        return;
    }

    pnode = t_yaml_env_pres_get_next_node(env->pres);
    pnode->prefix_comments = IOP_TYPED_ARRAY_TAB(lstr, prefix_comments);
}

static void t_yaml_env_pres_set_flow_mode(yaml_parse_t * nonnull env)
{
    yaml__presentation_node__t *pnode;

    if (!env->pres) {
        return;
    }

    pnode = t_yaml_env_pres_get_current_node(env->pres);
    pnode->flow_mode = true;
    logger_trace(&_G.logger, 2, "set flow mode");
}

static void t_yaml_env_pres_add_empty_line(yaml_parse_t * nonnull env)
{
    yaml__presentation_node__t *pnode;

    if (!env->pres) {
        return;
    }

    pnode = t_yaml_env_pres_get_next_node(env->pres);
    pnode->empty_lines = MIN(pnode->empty_lines + 1, 2);
}

/* }}} */
/* {{{ Utils */

static void log_new_data(const yaml_data_t * nonnull data)
{
    if (logger_is_traced(&_G.logger, 2)) {
        logger_trace_scope(&_G.logger, 2);
        logger_cont("parsed %s from "YAML_POS_FMT" up to "YAML_POS_FMT,
                    yaml_data_get_type(data, false),
                    YAML_POS_ARG(data->span.start),
                    YAML_POS_ARG(data->span.end));
        if (data->type == YAML_DATA_SCALAR) {
            lstr_t span = yaml_span_to_lstr(&data->span);

            logger_cont(": %pL", &span);
        }
    }
}

static int yaml_env_ltrim(yaml_parse_t *env)
{
    pstream_t comment_ps = ps_init(NULL, 0);
    bool in_comment = false;
    bool in_new_line = yaml_env_get_column_nb(env) == 1;
    qv_t(lstr) prefix_comments;

    p_clear(&prefix_comments, 1);

    while (!ps_done(&env->ps)) {
        int c = ps_peekc(env->ps);

        if (c == '#') {
            if (!in_comment) {
                in_comment = true;
                comment_ps = env->ps;
            }
        } else
        if (c == '\n') {
            if (env->pos_newline == env->ps.s) {
                /* Two \n in a row, indicating an empty line. Save this
                 * is the presentation data. */
                t_yaml_env_pres_add_empty_line(env);
            }
            env->line_number++;
            env->pos_newline = env->ps.s + 1;
            in_comment = false;
            if (comment_ps.s != NULL) {
                t_yaml_env_handle_comment_ps(env, comment_ps, in_new_line,
                                             &prefix_comments);
                comment_ps.s = NULL;
            }
            in_new_line = true;
        } else
        if (c == '\t') {
            return yaml_env_set_err(env, YAML_ERR_TAB_CHARACTER,
                                    "cannot use tab characters for "
                                    "indentation");
        } else
        if (!isspace(c) && !in_comment) {
            break;
        }
        yaml_env_skipc(env);
    }

    if (comment_ps.s != NULL) {
        t_yaml_env_handle_comment_ps(env, comment_ps, in_new_line,
                                     &prefix_comments);
    }

    yaml_env_set_prefix_comments(env, &prefix_comments);

    return 0;
}

static bool
ps_startswith_yaml_seq_prefix(const pstream_t *ps)
{
    if (!ps_has(ps, 2)) {
        return false;
    }

    return ps->s[0] == '-' && isspace(ps->s[1]);
}

/* r:48-57 r:65-90 r:97-122 s:'-_~<'
 * ie: 0-9a-zA-Z-_~
 */
static ctype_desc_t const ctype_yaml_key_chars = { {
    0x00000000, 0x13ff2000, 0x87fffffe, 0x47fffffe,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
} };

static bool ps_startswith_yaml_key(pstream_t ps)
{
    pstream_t ps_key;
    lstr_t key;

    ps_key = ps_get_span(&ps, &ctype_yaml_key_chars);
    key = LSTR_PS_V(&ps_key);

    if (key.len == 0 || ps_len(&ps) == 0) {
        return false;
    }

    return ps.s[0] == ':' && (ps_len(&ps) == 1 || isspace(ps.s[1]));
}

static int
yaml_parse_quoted_string(yaml_parse_t * nonnull env, sb_t * nonnull buf,
                         qv_t(u8) * nonnull var_bitmap,
                         bool * nonnull has_escaped_dollars)
{
    int line_nb = 0;
    int col_nb = 0;
    pstream_t start = env->ps;
    int var_pos = 0;

    while (!ps_done(&env->ps)) {
        switch (ps_peekc(env->ps)) {
          case '\n':
            return yaml_env_set_err(env, YAML_ERR_BAD_STRING,
                                    "missing closing '\"'");

          case '"':
            sb_add(buf, start.p, env->ps.s - start.s);
            ps_skip(&env->ps, 1);
            return 0;

          case '\\':
            sb_add(buf, start.p, env->ps.s - start.s);
            if (ps_has(&env->ps, 3) && env->ps.b[1] == '$'
            &&  env->ps.b[2] == '(')
            {
                /* escaped '$', this is not a variable */
                sb_adds(buf, "$(");
                ps_skip(&env->ps, 3);
                var_pos += 1;
                *has_escaped_dollars = true;
            } else
            if (parse_backslash(&env->ps, buf, &line_nb, &col_nb) < 0) {
                return yaml_env_set_err(env, YAML_ERR_BAD_STRING,
                                        "invalid backslash");
            }
            start = env->ps;
            break;

          case '$':
            if (ps_has(&env->ps, 2) && env->ps.b[1] == '(') {
                /* variable */
                var_bitmap_set_bit(var_bitmap, var_pos);
                var_pos += 1;
                ps_skip(&env->ps, 2);
                break;
            }

            /* FALLTHROUGH */

          default:
            ps_skip(&env->ps, 1);
            break;
        }
    }

    return yaml_env_set_err(env, YAML_ERR_BAD_STRING, "missing closing '\"'");
}

/* }}} */
/* {{{ Variables */

static void
t_yaml_env_add_var(yaml_parse_t * nonnull env, const lstr_t name,
                   yaml_variable_t * nonnull var)
{
    int pos;
    qv_t(yaml_variable) *vec;

    pos = qm_reserve(yaml_vars, &env->variables, &name, 0);
    if (pos & QHASH_COLLISION) {
        logger_trace(&_G.logger, 2, "add new occurrence of variable `%pL`",
                     &name);
        vec = &env->variables.values[pos & ~QHASH_COLLISION];
    } else {
        logger_trace(&_G.logger, 2, "add new variable `%pL`", &name);
        env->variables.keys[pos] = t_lstr_dup(name);
        vec = &env->variables.values[pos];
        t_qv_init(vec, 1);
    }
    qv_append(vec, var);
}

static void
yaml_env_merge_variables(yaml_parse_t * nonnull env,
                           const qm_t(yaml_vars) * nonnull vars)
{
    qm_for_each_key_value_p(yaml_vars, name, vec, vars) {
        int pos;

        logger_trace(&_G.logger, 2, "add occurrences of variable `%pL` in "
                     "including document", &name);
        pos = qm_reserve(yaml_vars, &env->variables, &name, 0);
        if (pos & QHASH_COLLISION) {
            qv_extend(&env->variables.values[pos & ~QHASH_COLLISION], vec);
        } else {
            env->variables.values[pos] = *vec;
        }
    }
}

/* Parse a variable name, following a '$(' pattern.
 *
 * Must be [a-zA-Z][a-ZA-Z0-9-_~]+ up to the ')'.
 */
static lstr_t
ps_parse_variable_name(pstream_t * nonnull ps)
{
    pstream_t name;

    name = ps_get_span(ps, &ctype_yaml_key_chars);
    if (ps_len(&name) <= 0 || !isalpha(name.s[0])) {
        return LSTR_NULL_V;
    }
    if (ps_skipc(ps, ')') < 0) {
        return LSTR_NULL_V;
    }

    return LSTR_PS_V(&name);
}

/* Detect use of $foo in a quoted string, and add those variables in the
 * env */
static int
t_yaml_env_add_variables(yaml_parse_t * nonnull env,
                         yaml_data_t * nonnull data, bool in_string,
                         qv_t(u8) * nullable var_bitmap)
{
    pstream_t ps;
    qh_t(lstr) variables_found;
    bool whole = false;
    bool starts_with_dollar;
    int var_pos = 0;

    assert (data->type == YAML_DATA_SCALAR
         && data->scalar.type == YAML_SCALAR_STRING);

    t_qh_init(lstr, &variables_found, 0);

    ps = ps_initlstr(&data->scalar.s);
    starts_with_dollar = ps_peekc(ps) == '$';
    for (;;) {
        lstr_t name;

        if (ps_skip_afterchr(&ps, '$') < 0) {
            break;
        }
        if (ps_peekc(ps) != '(') {
            continue;
        }
        ps_skip(&ps, 1);

        var_pos += 1;
        if (var_bitmap && !var_bitmap_test_bit(var_bitmap, var_pos - 1)) {
            continue;
        }

        name = ps_parse_variable_name(&ps);
        if (!name.s) {
            /* TODO: Ideally, the span should point to the '$' starting the
             * variable. This isn't easy to do so however, in the case of a
             * quoted string. */
            return yaml_env_set_err_at(env, &data->span, YAML_ERR_INVALID_VAR,
                "the string contains a variable with an invalid name");
        }

        if (name.s) {
            if (ps_done(&ps) && starts_with_dollar
            &&  qh_len(lstr, &variables_found) == 0)
            {
                /* The whole string is this variable */
                whole = true;
            }
            qh_add(lstr, &variables_found, &name);
        }
    }

    if (qh_len(lstr, &variables_found) > 0) {
        yaml_variable_t *var;

        if (env->flags & YAML_PARSE_FORBID_VARIABLES) {
            return yaml_env_set_err_at(
                env, &data->span, YAML_ERR_FORBIDDEN_VAR,
                "cannot use variables in this context"
            );
        }

        var = t_new(yaml_variable_t, 1);
        var->data = data;
        var->in_string = in_string || !whole;
        if (var_bitmap) {
            var->var_bitmap = *var_bitmap;
        }

        qh_for_each_key(lstr, name, &variables_found) {
            t_yaml_env_add_var(env, name, var);
        }

        if (env->flags & YAML_PARSE_GEN_PRES_DATA) {
            yaml__presentation_node__t *node;

            node = t_yaml_env_pres_get_current_node(env->pres);
            node->tpl = t_iop_new(yaml__presentation_template);
            node->tpl->original_value = data->scalar.s;
            if (var_bitmap) {
                node->tpl->variables_bitmap
                    = IOP_TYPED_ARRAY_TAB(u8, var_bitmap);
            }
        }
    }

    return 0;
}

static int count_escaped_vars(const lstr_t value)
{
    pstream_t ps = ps_initlstr(&value);
    int cnt = 0;

    while (ps_skip_afterchr(&ps, '$') >= 0) {
        if (ps_peekc(ps) == '(') {
            cnt++;
        }
    }

    return cnt;
}

/* Replace occurrences of $name with `value` in `data`.
 *
 * If var_bitmap is non-NULL, it is used to detect which '$' characters
 * are used for variables.
 * After the substitution, the var_bitmap is replaced by an updated one
 * (without the variables that were replaced).
 */
static lstr_t
t_tpl_set_variable(const lstr_t tpl_string, const lstr_t name,
                   const lstr_t value, qv_t(u8) * nonnull var_bitmap)
{
    t_SB_1k(buf);
    pstream_t ps;
    pstream_t sub;
    qv_t(u8) *new_bitmap = NULL;
    int bitmap_pos = 0;
    int new_bitmap_pos = 0;
    int nb_raw_dollars;

    /* If the string to insert contains '$(' patterns, we need to properly
     * consider them to generate the right bitmap. */
    nb_raw_dollars = count_escaped_vars(value);

    ps = ps_initlstr(&tpl_string);
    if (var_bitmap->len > 0 || nb_raw_dollars > 0) {
        new_bitmap = t_qv_new(u8, 0);
    }

    for (;;) {
        pstream_t cpy;
        lstr_t var_name;

        /* copy up to next '$' */
        if (ps_get_ps_chr(&ps, '$', &sub) < 0) {
            /* no next '$', copy everything and stop */
            sb_add_ps(&buf, ps);
            break;
        }
        sb_add_ps(&buf, sub);
        if (!ps_has(&ps, 2) || ps.s[1] != '(') {
            ps_skip(&ps, 1);
            sb_addc(&buf, '$');
            continue;
        }

        if (!var_bitmap_test_bit(var_bitmap, bitmap_pos)) {
            bitmap_pos += 1;
            new_bitmap_pos += 1;
            /* add '$' and continue to get to next '$' char */
            sb_addc(&buf, ps_getc(&ps));
            continue;
        }

        bitmap_pos += 1;
        cpy = ps;
        ps_skip(&cpy, 2);
        var_name = ps_parse_variable_name(&cpy);
        if (lstr_equal(var_name, name)) {
            sb_add_lstr(&buf, value);
            new_bitmap_pos += nb_raw_dollars;
        } else {
            pstream_t var_string;

            if (expect(ps_get_ps_upto(&ps, cpy.b, &var_string) >= 0)) {
                sb_add_ps(&buf, var_string);
            }
            if (new_bitmap) {
                var_bitmap_set_bit(new_bitmap, new_bitmap_pos);
                new_bitmap_pos += 1;
            }
        }
        ps = cpy;
    }

    logger_trace(&_G.logger, 2, "apply replacement $(%pL)=%pL, data value "
                 "changed from `%pL` to `%pL`", &name, &value,
                 &tpl_string, &buf);
    if (new_bitmap) {
        *var_bitmap = *new_bitmap;
    }

    return LSTR_SB_V(&buf);
}

qvector_t(var_binding, yaml__presentation_variable_binding__t);

static void
add_var_binding(lstr_t var_name, const lstr_t value,
                qv_t(var_binding) * nonnull bindings)
{
    yaml__presentation_variable_binding__t *binding = qv_growlen(bindings, 1);

    iop_init(yaml__presentation_variable_binding, binding);
    binding->var_name = var_name;
    binding->value = value;
}

static int
t_yaml_env_replace_variables(yaml_parse_t * nonnull env,
                             const yaml_obj_t * nonnull override,
                             qm_t(yaml_vars) * nonnull variables,
                             qv_t(var_binding) * nullable bindings)
{
    tab_for_each_ptr(pair, &override->fields) {
        lstr_t string_value = LSTR_NULL_V;
        int pos;

        pos = qm_find(yaml_vars, variables, &pair->key);
        if (pos < 0) {
            yaml_env_set_err_at(env, &pair->key_span, YAML_ERR_BAD_KEY,
                                "unknown variable");
            return -1;
        }

        /* Replace every occurrence of the variable with the provided data. */
        tab_for_each_entry(var, &variables->values[pos]) {
            if (var->in_string) {
                if (!string_value.s) {
                    if (pair->data.type != YAML_DATA_SCALAR) {
                        yaml_env_set_err_at(
                            env, &pair->data.span, YAML_ERR_WRONG_DATA,
                            "this variable can only be set with a scalar"
                        );
                        return -1;
                    }

                    if (pair->data.scalar.type == YAML_SCALAR_STRING) {
                        string_value = pair->data.scalar.s;
                    } else {
                        string_value = yaml_span_to_lstr(&pair->data.span);
                    }
                }

                assert (var->data->type == YAML_DATA_SCALAR
                     && var->data->scalar.type == YAML_SCALAR_STRING);
                var->data->scalar.s = t_tpl_set_variable(
                    var->data->scalar.s,
                    pair->key,
                    string_value,
                    &var->var_bitmap
                );
            } else {
                *var->data = pair->data;

            }
        }

        if (bindings) {
            add_var_binding(pair->key, string_value, bindings);
        }

        /* remove the variable from variables, to prevent matching twice,
         * and to be able to keep in the qm the variables that are still
         * active. */
        qm_del_at(yaml_vars, variables, pos);
    }

    return 0;
}

/* }}} */
/* {{{ Tag */

typedef enum yaml_tag_type_t {
    YAML_TAG_TYPE_NONE,
    YAML_TAG_TYPE_INCLUDE,
    YAML_TAG_TYPE_INCLUDERAW,
} yaml_tag_type_t;

static yaml_tag_type_t get_tag_type(const lstr_t tag)
{
    if (lstr_startswith(tag, LSTR("include:"))) {
        return YAML_TAG_TYPE_INCLUDE;
    } else
    if (lstr_startswith(tag, LSTR("includeraw:"))) {
        return YAML_TAG_TYPE_INCLUDERAW;
    } else {
        return YAML_TAG_TYPE_NONE;
    }
}

static int
t_handle_binary_tag(yaml_parse_t * nonnull env, yaml_data_t * nonnull data)
{
    if (data->type != YAML_DATA_SCALAR
    ||  data->scalar.type != YAML_SCALAR_STRING)
    {
        return yaml_env_set_err_at(env, &data->span, YAML_ERR_WRONG_DATA,
                                   "binary tag can only be used on strings");
    } else {
        sb_t  sb;
        /* TODO: factorize this with iop-json, iop-xml, etc */
        int   blen = DIV_ROUND_UP(data->scalar.s.len * 3, 4);
        char *buf  = t_new_raw(char, blen + 1);

        sb_init_full(&sb, buf, 0, blen + 1, &mem_pool_static);
        if (sb_add_lstr_unb64(&sb, data->scalar.s) < 0) {
            return yaml_env_set_err_at(env, &data->span, YAML_ERR_WRONG_DATA,
                                       "binary data must be base64 encoded");
        }
        data->scalar.s = LSTR_DATA_V(buf, sb.len);
        data->scalar.type = YAML_SCALAR_BYTES;
        data->tag = LSTR_NULL_V;
    }

    return 0;
}

static int
t_yaml_env_parse_tag(yaml_parse_t * nonnull env, const uint32_t min_indent,
                     yaml_data_t * nonnull out,
                     yaml_tag_type_t * nonnull type)
{
    /* r:32-127 -s:'[]{}, ' */
    static const ctype_desc_t ctype_tag = { {
        0x00000000, 0xffffeffe, 0xd7ffffff, 0xd7ffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    yaml_pos_t tag_pos_start = yaml_env_get_pos(env);
    yaml_pos_t tag_pos_end;
    pstream_t tag;
    unsigned flags = env->flags;
    int res;

    assert (ps_peekc(env->ps) == '!');
    yaml_env_skipc(env);

    if (!isalpha(ps_peekc(env->ps))) {
        return yaml_env_set_err(env, YAML_ERR_INVALID_TAG,
                                "must start with a letter");
    }

    tag = ps_get_span(&env->ps, &ctype_tag);
    if (!isspace(ps_peekc(env->ps)) && !ps_done(&env->ps)) {
        return yaml_env_set_err(env, YAML_ERR_INVALID_TAG,
                                "wrong character in tag");
    }
    tag_pos_end = yaml_env_get_pos(env);

    *type = get_tag_type(LSTR_PS_V(&tag));
    switch (*type) {
      case YAML_TAG_TYPE_INCLUDE:
      case YAML_TAG_TYPE_INCLUDERAW:
        flags = flags | YAML_PARSE_FORBID_VARIABLES;
        break;
      default:
        break;
    }

    SWAP(unsigned, env->flags, flags);
    res = t_yaml_env_parse_data(env, min_indent, out);
    SWAP(unsigned, env->flags, flags);
    RETHROW(res);

    if (out->tag.s) {
        return yaml_env_set_err_at(env, out->tag_span, YAML_ERR_WRONG_OBJECT,
                                   "two tags have been declared");
    }

    out->tag = LSTR_PS_V(&tag);
    out->span.start = tag_pos_start;
    out->tag_span = t_new(yaml_span_t, 1);
    yaml_span_init(out->tag_span, env, tag_pos_start, tag_pos_end);

    if (lstr_equal(out->tag, LSTR("bin"))) {
        RETHROW(t_handle_binary_tag(env, out));
    }

    return 0;
}

static bool has_inclusion_loop(const yaml_parse_t * nonnull env,
                               const lstr_t newfile)
{
    do {
        if (lstr_equal(env->fullpath, newfile)) {
            return true;
        }
        env = env->included ? env->included->parent : NULL;
    } while (env);

    return false;
}

static int t_yaml_env_do_include(yaml_parse_t * nonnull env, bool raw,
                                 lstr_t path, yaml_data_t * nonnull data,
                                 qm_t(yaml_vars) * nonnull variables)
{
    yaml_parse_t *subfile = NULL;
    char dirpath[PATH_MAX];
    SB_1k(err);

    RETHROW(yaml_env_ltrim(env));

    path_dirname(dirpath, PATH_MAX, env->fullpath.s ?: "");

    if (raw) {
        logger_trace(&_G.logger, 2, "copying raw subfile %pL", &path);
    } else {
        logger_trace(&_G.logger, 2, "parsing subfile %pL", &path);
    }

    subfile = t_yaml_parse_new(
        YAML_PARSE_GEN_PRES_DATA
      | YAML_PARSE_ALLOW_UNBOUND_VARIABLES
    );
    subfile->rootdirpath = env->rootdirpath;
    if (t_yaml_parse_attach_file(subfile, t_fmt("%pL", &path), dirpath,
                                 &err) < 0)
    {
        goto err;

    }
    if (has_inclusion_loop(env, subfile->fullpath)) {
        sb_sets(&err, "inclusion loop detected");
        goto err;
    }

    subfile->included = t_new(yaml_included_file_t, 1);
    subfile->included->parent = env;
    subfile->included->data = *data;
    qv_append(&env->subfiles, subfile);

    if (raw) {
        yaml_data_set_bytes(data, subfile->file_contents);
        /* As the include is raw, we do not want the span to point to the
         * content of the include, as it may be binary. So use the span
         * of the "!include" node.
         */
        data->span = subfile->included->data.span;
    } else {
        if (t_yaml_parse(subfile, data, &err) < 0) {
            /* no call to yaml_env_set_err, because the generated error message
             * will already have all the including details. */
            env->err = subfile->err;
            return -1;
        }
    }

    *variables = subfile->variables;

    if (env->pres) {
        yaml__presentation_include__t *inc;

        inc = t_iop_new(yaml__presentation_include);

        if (subfile->included->data.type != YAML_DATA_SCALAR
        ||  subfile->included->data.scalar.type != YAML_SCALAR_NULL
        ||  subfile->included->data.presentation != NULL)
        {
            /* Only do this if the included data is not a raw null scalar, to
             * avoid filling the presentation with clutter. */
            t_yaml_data_get_presentation(&subfile->included->data,
                                         &inc->include_presentation);
        }

        inc->path = path;
        inc->raw = raw;
        t_yaml_data_get_presentation(data, &inc->document_presentation);

        /* XXX: create a new presentation node for data, that indicates it
         * is included. We should not modify the existing presentation node
         * (if it exists), as it indicates the presentation of the data
         * in the subfile, and was saved in "inc->presentation". */
        data->presentation = t_iop_new(yaml__presentation_node);
        data->presentation->included = inc;
    }

    return 0;

  err:
    yaml_env_set_err_at(env, &data->span, YAML_ERR_INVALID_INCLUDE,
                        t_fmt("%pL", &err));
    return -1;
}

static int
t_yaml_env_handle_override(yaml_parse_t * nonnull env,
                           yaml_data_t * nonnull override,
                           qm_t(yaml_vars) * nonnull variables,
                           yaml__presentation_include__t * nullable pres,
                           yaml_data_t * nonnull out);

static int
t_yaml_env_handle_include(yaml_parse_t * nonnull env,
                          const uint32_t min_indent, bool raw,
                          yaml_data_t * nonnull data)
{
    yaml__presentation_include__t *pres;
    qm_t(yaml_vars) vars;
    pstream_t path = ps_initlstr(&data->tag);
    yaml_data_t override = *data;

    if (raw) {
        ps_skip(&path, strlen("includeraw:"));
    } else {
        ps_skip(&path, strlen("include:"));
    }

    /* Parse and retrieve the included AST, and get the associated variables.
     */
    RETHROW(t_yaml_env_do_include(env, raw, LSTR_PS_V(&path), data, &vars));
    pres = data->presentation ? data->presentation->included : NULL;

    /* Parse and apply override, including variable settings */
    RETHROW(t_yaml_env_handle_override(env, &override, &vars, pres, data));

    /* Save remaining variables into current variables for the document.
     */
    yaml_env_merge_variables(env, &vars);

    return 0;
}

/* }}} */
/* {{{ Seq */

/** Get the presentation stored for the next node, and save in "last_node"
 * to ensure inline presentation data uses this node. */
static void
yaml_env_pop_next_node(yaml_parse_t * nonnull env,
                       yaml__presentation_node__t * nullable * nonnull node)
{
    *node = env->pres->next_node;
    env->pres->next_node = NULL;
    env->pres->last_node = node;
}

static int t_yaml_env_parse_seq(yaml_parse_t *env, const uint32_t min_indent,
                                yaml_data_t *out)
{
    qv_t(yaml_data) datas;
    qv_t(yaml_pres_node) pres;
    yaml_pos_t pos_end;

    p_clear(&pos_end, 1);
    t_qv_init(&datas, 0);
    t_qv_init(&pres, 0);

    assert (ps_startswith_yaml_seq_prefix(&env->ps));
    yaml_env_start_data(env, YAML_DATA_SEQ, out);

    for (;;) {
        yaml__presentation_node__t *node = NULL;
        yaml_data_t *elem;
        uint32_t last_indent;

        RETHROW(yaml_env_ltrim(env));
        if (env->pres) {
            yaml_env_pop_next_node(env, &node);
        }

        /* skip '-' */
        yaml_env_skipc(env);

        elem = qv_growlen(&datas, 1);
        RETHROW(t_yaml_env_parse_data(env, min_indent + 1, elem));

        pos_end = yaml_env_get_pos(env);
        RETHROW(yaml_env_ltrim(env));

        qv_append(&pres, node);

        if (ps_done(&env->ps)) {
            break;
        }

        last_indent = yaml_env_get_column_nb(env);
        if (last_indent < min_indent) {
            /* we go down on indent, so the seq is over */
            break;
        }
        if (last_indent > min_indent) {
            return yaml_env_set_err(env, YAML_ERR_WRONG_INDENT,
                                    "line not aligned with current sequence");
        } else
        if (!ps_startswith_yaml_seq_prefix(&env->ps)) {
            return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
                                    "expected another element of sequence");
        }
    }

    yaml_env_end_data(env, out);
    out->seq = t_new(yaml_seq_t, 1);
    out->seq->datas = datas;
    out->seq->pres_nodes = pres;

    return 0;
}

/* }}} */
/* {{{ Obj */

static int
yaml_env_parse_key(yaml_parse_t * nonnull env, lstr_t * nonnull key,
                   yaml_span_t * nonnull key_span,
                   yaml__presentation_node__t * nonnull * nullable node)
{
    pstream_t ps_key;
    yaml_pos_t key_pos_start = yaml_env_get_pos(env);

    RETHROW(yaml_env_ltrim(env));
    if (env->pres && node) {
        yaml_env_pop_next_node(env, node);
    }

    ps_key = ps_get_span(&env->ps, &ctype_yaml_key_chars);
    yaml_span_init(key_span, env, key_pos_start, yaml_env_get_pos(env));

    *key = LSTR_PS_V(&ps_key);

    if (ps_len(&ps_key) == 0) {
        return yaml_env_set_err(env, YAML_ERR_BAD_KEY,
                                "invalid character used");
    } else
    if (unlikely(!isalpha(ps_key.s[0]) && !lstr_equal(*key, LSTR("<<")))) {
        return yaml_env_set_err_at(env, key_span, YAML_ERR_BAD_KEY,
                                   "name must start with an alphabetic "
                                   "character");
    } else
    if (ps_getc(&env->ps) != ':') {
        return yaml_env_set_err(env, YAML_ERR_BAD_KEY, "missing colon");
    }

    return 0;
}

static void
t_add_merge_kd(yaml_key_data_t * nonnull kd,
               qv_t(yaml_key_data) * nonnull out_fields,
               qh_t(lstr) * nonnull keys_hash,
               yaml__presentation_merge_key_elem_key__t * nullable pres)
{
    if (qh_add(lstr, keys_hash, &kd->key) < 0) {
        /* The field already exists, go through the existing ones to
         * find it, and copy over it */
        /* XXX: that's a deviation from the spec, which specifies the
         * key should be ignored here. However, this does not match up
         * with how our overrides works. */
        tab_for_each_ptr(existing_kd, out_fields) {
            if (lstr_equal(existing_kd->key, kd->key)) {
                if (pres) {
                    pres->original_data = t_iop_new(yaml__data);
                    t_yaml_data_to_iop(&existing_kd->data,
                                       pres->original_data);
                }
                *existing_kd = *kd;
                break;
            }
        }
    } else {
        qv_append(out_fields, *kd);
    }
}

qvector_t(elem_keys, yaml__presentation_merge_key_elem_key__t);
qvector_t(mk_elem, yaml__presentation_merge_key_elem__t);

static void
t_add_merge_elem(const qv_t(yaml_key_data) * nonnull fields,
                 qv_t(yaml_key_data) * nonnull out_fields,
                 qh_t(lstr) * nonnull keys_hash,
                 qv_t(mk_elem) * nullable pres)
{
    yaml__presentation_merge_key_elem__t *elem_pres = NULL;
    qv_t(elem_keys) pres_keys;

    if (pres) {
        elem_pres = qv_growlen(pres, 1);
        iop_init(yaml__presentation_merge_key_elem, elem_pres);
        t_qv_init(&pres_keys, fields->len);
    }

    tab_for_each_ptr(kd, fields) {
        yaml__presentation_merge_key_elem_key__t *pres_key = NULL;

        if (pres) {
            pres_key = qv_growlen(&pres_keys, 1);
            iop_init(yaml__presentation_merge_key_elem_key, pres_key);
            pres_key->key = kd->key;
        }
        t_add_merge_kd(kd, out_fields, keys_hash, pres_key);
    }

    if (pres) {
        elem_pres->keys
            = IOP_TYPED_ARRAY_TAB(yaml__presentation_merge_key_elem_key,
                                  &pres_keys);
    }
}

static int
t_add_merge_data(yaml_parse_t * nonnull env, const yaml_data_t * nonnull data,
                 qv_t(yaml_key_data) * nonnull out_fields,
                 qh_t(lstr) * nonnull keys_hash,
                 qv_t(mk_elem) * nullable pres)
{
    if (data->tag.s) {
        return yaml_env_set_err_at(env, data->tag_span, YAML_ERR_INVALID_TAG,
                                   "cannot use tags in a merge key");
    }

    switch (data->type) {
      case YAML_DATA_SCALAR:
      case YAML_DATA_SEQ:
        return yaml_env_set_err_at(env, &data->span, YAML_ERR_WRONG_DATA,
                                   "value of merge key must be an object, or "
                                   "a list of objects");
      case YAML_DATA_OBJ:
        t_add_merge_elem(&data->obj->fields, out_fields, keys_hash, pres);
        break;
    }

    return 0;
}

static int
t_handle_merge_key(yaml_parse_t * nonnull env, yaml_data_t * nonnull out)
{
    qv_t(yaml_key_data) fields;
    qh_t(lstr) keys_hash;
    qv_t(mk_elem) pres_elems;
    qv_t(mk_elem) *pres_elems_p = NULL;
    yaml__document_presentation__t *data_pres = NULL;
    const yaml_data_t *mk_data;

    t_qv_init(&fields, 0);
    t_qh_init(lstr, &keys_hash, 0);
    if (env->pres) {
        t_qv_init(&pres_elems, 0);
        pres_elems_p = &pres_elems;
        data_pres = t_iop_new(yaml__document_presentation);
        t_yaml_data_get_presentation(out, data_pres);
        if (out->presentation) {
            iop_init(yaml__presentation_node, out->presentation);
        }
    }

    assert (out->type == YAML_DATA_OBJ
        &&  out->obj->fields.len >= 1
        &&  lstr_equal(out->obj->fields.tab[0].key, LSTR("<<")));
    mk_data = &out->obj->fields.tab[0].data;

    /* First, handle every element in merge key. */
    switch (mk_data->type) {
      case YAML_DATA_SEQ:
        tab_for_each_ptr(subdata, &mk_data->seq->datas) {
            RETHROW(t_add_merge_data(env, subdata, &fields, &keys_hash,
                                     pres_elems_p));
        }
        break;

      case YAML_DATA_SCALAR:
      case YAML_DATA_OBJ:
        RETHROW(t_add_merge_data(env, mk_data, &fields, &keys_hash,
                                 pres_elems_p));
        break;
    }

    /* Then, consider all the remaining fields as one big override. */
    out->obj->fields.tab++;
    out->obj->fields.len--;
    if (out->obj->fields.len > 0) {
        t_add_merge_elem(&out->obj->fields, &fields, &keys_hash,
                         pres_elems_p);
    }

    if (env->pres) {
        yaml__presentation_node__t *pnode;
        yaml__presentation_merge_key__t *mk;

        mk = t_iop_new(yaml__presentation_merge_key);
        mk->elements = IOP_TYPED_ARRAY_TAB(yaml__presentation_merge_key_elem,
                                           &pres_elems);
        mk->has_only_merge_key = out->obj->fields.len == 0;
        mk->presentation = data_pres;

        pnode = t_yaml_env_pres_get_current_node(env->pres);
        pnode->merge_key = mk;
    }

    out->obj->fields = fields;

    return 0;
}

static int
t_yaml_env_parse_obj(yaml_parse_t * nonnull env, const uint32_t min_indent,
                     yaml_data_t * nonnull out)
{
    qv_t(yaml_key_data) fields;
    yaml_pos_t pos_end;
    qh_t(lstr) keys_hash;

    p_clear(&pos_end, 1);
    t_qv_init(&fields, 0);
    t_qh_init(lstr, &keys_hash, 0);

    yaml_env_start_data(env, YAML_DATA_OBJ, out);

    for (;;) {
        lstr_t key;
        yaml_key_data_t *kd;
        uint32_t last_indent;
        yaml_span_t key_span;
        yaml__presentation_node__t *node;

        RETHROW(yaml_env_parse_key(env, &key, &key_span, &node));

        kd = qv_growlen0(&fields, 1);
        kd->key = key;
        kd->key_span = key_span;
        if (qh_add(lstr, &keys_hash, &kd->key) < 0) {
            return yaml_env_set_err_at(env, &key_span, YAML_ERR_BAD_KEY,
                                       "key is already declared in the "
                                       "object");
        }
        if (unlikely(fields.len > 1 && lstr_equal(key, LSTR("<<")))) {
            return yaml_env_set_err_at(env, &key_span, YAML_ERR_BAD_KEY,
                                       "merge key must be the first key in "
                                       "the object");
        }

        /* XXX: This is a hack to handle the tricky case where a sequence
         * has the same indentation as the key:
         *  a:
         *  - 1
         *  - 2
         * This syntax is valid YAML, but breaks the otherwise valid contract
         * that a subdata always has a strictly greater indentation level than
         * its containing data.
         */
        RETHROW(yaml_env_ltrim(env));

        if (ps_startswith_yaml_seq_prefix(&env->ps)) {
            RETHROW(t_yaml_env_parse_data(env, min_indent, &kd->data));
        } else {
            RETHROW(t_yaml_env_parse_data(env, min_indent + 1, &kd->data));
        }

        pos_end = yaml_env_get_pos(env);
        kd->key_presentation = node;
        RETHROW(yaml_env_ltrim(env));

        if (ps_done(&env->ps)) {
            break;
        }

        last_indent = yaml_env_get_column_nb(env);
        if (last_indent < min_indent) {
            /* we go down on indent, so the obj is over */
            break;
        }
        if (last_indent > min_indent) {
            return yaml_env_set_err(env, YAML_ERR_WRONG_INDENT,
                                    "line not aligned with current object");
        }
    }

    yaml_env_end_data_with_pos(env, pos_end, out);
    out->obj = t_new(yaml_obj_t, 1);
    out->obj->fields = fields;

    if (unlikely(fields.len > 0 && lstr_equal(fields.tab[0].key, LSTR("<<"))))
    {
        RETHROW(t_handle_merge_key(env, out));
    }

    return 0;
}

/* }}} */
/* {{{ Scalar */

static pstream_t yaml_env_get_scalar_ps(yaml_parse_t * nonnull env,
                                        bool in_flow)
{
    /* '\n' and '#' */
    static const ctype_desc_t ctype_scalarend = { {
        0x00000400, 0x00000008, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    /* '\n', '#', '{, '[', '}', ']' or ',' */
    static const ctype_desc_t ctype_scalarflowend = { {
        0x00000400, 0x00001008, 0x28000000, 0x28000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    pstream_t scalar;

    if (in_flow) {
        scalar = ps_get_cspan(&env->ps, &ctype_scalarflowend);
    } else {
        scalar = ps_get_cspan(&env->ps, &ctype_scalarend);
    }

    /* need to rtrim to remove extra spaces */
    ps_rtrim(&scalar);

    /* Position the env ps to the end of the trimmed scalar ps, so that
     * the span can be correctly computed. */
    env->ps.s = scalar.s_end;

    return scalar;
}

static int
t_yaml_env_parse_quoted_string(yaml_parse_t *env, yaml_data_t *out,
                               qv_t(u8) * nonnull var_bitmap,
                               bool * nonnull has_escaped_dollars)
{
    sb_t buf;

    assert (ps_peekc(env->ps) == '"');
    yaml_env_skipc(env);

    t_sb_init(&buf, 128);
    RETHROW(yaml_parse_quoted_string(env, &buf, var_bitmap,
                                     has_escaped_dollars));

    yaml_env_end_data(env, out);
    out->scalar.type = YAML_SCALAR_STRING;
    out->scalar.s = LSTR_SB_V(&buf);

    return 0;
}

static int
yaml_parse_special_scalar(lstr_t line, yaml_scalar_t *out)
{
    if (lstr_equal(line, LSTR("~"))
    ||  lstr_ascii_iequal(line, LSTR("null")))
    {
        out->type = YAML_SCALAR_NULL;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR("true"))) {
        out->type = YAML_SCALAR_BOOL;
        out->b = true;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR("false"))) {
        out->type = YAML_SCALAR_BOOL;
        out->b = false;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR("-.inf"))) {
        out->type = YAML_SCALAR_DOUBLE;
        out->d = -INFINITY;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR(".inf"))) {
        out->type = YAML_SCALAR_DOUBLE;
        out->d = INFINITY;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR(".nan"))) {
        out->type = YAML_SCALAR_DOUBLE;
        out->d = NAN;
        return 0;
    }

    return -1;
}

static int
yaml_parse_numeric_scalar(lstr_t line, yaml_scalar_t *out)
{
    double d;

    if (line.s[0] == '-') {
        int64_t i;

        if (lstr_to_int64(line, &i) == 0) {
            if (i >= 0) {
                /* This can happen for -0 for example. Force to use UINT
                 * in that case, to make sure INT is only used for < 0. */
                out->type = YAML_SCALAR_UINT;
                out->u = i;
            } else {
                out->type = YAML_SCALAR_INT;
                out->i = i;
            }
            return 0;
        }
    } else {
        uint64_t u;

        if (lstr_to_uint64(line, &u) == 0) {
            out->type = YAML_SCALAR_UINT;
            out->u = u;
            return 0;
        }
    }

    if (lstr_to_double(line, &d) == 0) {
        out->type = YAML_SCALAR_DOUBLE;
        out->d = d;
        return 0;
    }

    return -1;
}

static int t_yaml_env_parse_scalar(yaml_parse_t *env, bool in_flow,
                                   yaml_data_t *out)
{
    lstr_t line;
    pstream_t ps_line;

    yaml_env_start_data(env, YAML_DATA_SCALAR, out);
    if (ps_peekc(env->ps) == '"') {
        qv_t(u8) var_bitmap;
        bool has_escaped_dollars = false;

        t_qv_init(&var_bitmap, 0);
        RETHROW(t_yaml_env_parse_quoted_string(env, out, &var_bitmap,
                                               &has_escaped_dollars));

        if (env->flags & YAML_PARSE_GEN_PRES_DATA) {
            yaml__presentation_node__t *node;

            node = t_yaml_env_pres_get_current_node(env->pres);
            node->quoted = true;
        }

        if (var_bitmap.len > 0) {
            if (has_escaped_dollars) {
                RETHROW(t_yaml_env_add_variables(env, out, true,
                                                 &var_bitmap));
            } else {
                /* fast case: has variables but no escaping: do not bother
                 * with the bitmap */
                RETHROW(t_yaml_env_add_variables(env, out, true, NULL));
            }
        }

        return 0;
    }

    /* get scalar string, ie up to newline or comment, or ']' or ',' for flow
     * context */
    ps_line = yaml_env_get_scalar_ps(env, in_flow);
    if (ps_len(&ps_line) == 0) {
        return yaml_env_set_err(env, YAML_ERR_MISSING_DATA,
                                "unexpected character");
    }

    line = LSTR_PS_V(&ps_line);
    yaml_env_end_data(env, out);

    /* special strings */
    if (yaml_parse_special_scalar(line, &out->scalar) >= 0) {
        return 0;
    }

    /* try to parse it as a int/uint or float */
    if (yaml_parse_numeric_scalar(line, &out->scalar) >= 0) {
        return 0;
    }

    /* If all else fail, it is a string. */
    out->scalar.type = YAML_SCALAR_STRING;
    out->scalar.s = line;

    RETHROW(t_yaml_env_add_variables(env, out, false, NULL));

    return 0;
}

/* }}} */
/* {{{ Flow seq */

static int t_yaml_env_parse_flow_key_data(yaml_parse_t * nonnull env,
                                          yaml_key_data_t * nonnull out);

static void
t_yaml_env_build_implicit_obj(yaml_parse_t * nonnull env,
                              yaml_key_data_t * nonnull kd,
                              yaml_data_t * nonnull out)
{
    qv_t(yaml_key_data) fields;

    t_qv_init(&fields, 1);
    qv_append(&fields, *kd);

    yaml_env_start_data_with_pos(env, YAML_DATA_OBJ, kd->key_span.start, out);
    yaml_env_end_data_with_pos(env, kd->data.span.end, out);
    out->obj = t_new(yaml_obj_t, 1);
    out->obj->fields = fields;
}

/* A flow sequence begins with '[', ends with ']' and elements are separated
 * by ','.
 * Inside a flow sequence, block types (ie using indentation) are forbidden,
 * and values can only be:
 *  - a scalar
 *  - a value pair: `a: b`
 *  - a flow object: `{ ... }`
 *  - a flow seq: `[ ... ]`
 */
static int
t_yaml_env_parse_flow_seq(yaml_parse_t *env, yaml_data_t *out)
{
    qv_t(yaml_data) datas;

    t_qv_init(&datas, 0);

    /* skip '[' */
    assert (ps_peekc(env->ps) == '[');
    yaml_env_start_data(env, YAML_DATA_SEQ, out);
    yaml_env_skipc(env);

    for (;;) {
        yaml_key_data_t kd;

        RETHROW(yaml_env_ltrim(env));
        if (ps_peekc(env->ps) == ']') {
            yaml_env_skipc(env);
            goto end;
        }

        RETHROW(t_yaml_env_parse_flow_key_data(env, &kd));
        if (kd.key.s) {
            yaml_data_t obj;

            t_yaml_env_build_implicit_obj(env, &kd, &obj);
            qv_append(&datas, obj);
        } else {
            qv_append(&datas, kd.data);
        }

        RETHROW(yaml_env_ltrim(env));
        switch (ps_peekc(env->ps)) {
          case ']':
            yaml_env_skipc(env);
            goto end;
          case ',':
            yaml_env_skipc(env);
            break;
          default:
            return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
                                    "expected another element of sequence");
        }
    }

  end:
    yaml_env_end_data(env, out);
    out->seq = t_new(yaml_seq_t, 1);
    out->seq->datas = datas;
    t_qv_init(&out->seq->pres_nodes, 0);

    return 0;
}

/* }}} */
/* {{{ Flow obj */

/* A flow sequence begins with '{', ends with '}' and elements are separated
 * by ','.
 * Inside a flow sequence, block types (ie using indentation) are forbidden,
 * and only value pairs are allowed: `key: <flow_data>`.
 */
static int
t_yaml_env_parse_flow_obj(yaml_parse_t *env, yaml_data_t *out)
{
    qv_t(yaml_key_data) fields;
    qh_t(lstr) keys_hash;

    t_qv_init(&fields, 0);
    t_qh_init(lstr, &keys_hash, 0);

    /* skip '{' */
    assert (ps_peekc(env->ps) == '{');
    yaml_env_start_data(env, YAML_DATA_OBJ, out);
    yaml_env_skipc(env);

    for (;;) {
        yaml_key_data_t kd;

        RETHROW(yaml_env_ltrim(env));
        if (ps_peekc(env->ps) == '}') {
            yaml_env_skipc(env);
            goto end;
        }

        RETHROW(t_yaml_env_parse_flow_key_data(env, &kd));
        if (!kd.key.s) {
            return yaml_env_set_err_at(env, &kd.data.span,
                                       YAML_ERR_WRONG_DATA,
                                       "only key-value mappings are allowed "
                                       "inside an object");
        } else
        if (qh_add(lstr, &keys_hash, &kd.key) < 0) {
            return yaml_env_set_err_at(env, &kd.key_span, YAML_ERR_BAD_KEY,
                                       "key is already declared in the "
                                       "object");
        }
        qv_append(&fields, kd);

        RETHROW(yaml_env_ltrim(env));
        switch (ps_peekc(env->ps)) {
          case '}':
            yaml_env_skipc(env);
            goto end;
          case ',':
            yaml_env_skipc(env);
            break;
          default:
            return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
                                    "expected another element of object");
        }
    }

  end:
    yaml_env_end_data(env, out);
    out->obj = t_new(yaml_obj_t, 1);
    out->obj->fields = fields;

    return 0;
}

/* }}} */
/* {{{ Flow key-data */

static int t_yaml_env_parse_flow_key_val(yaml_parse_t *env,
                                         yaml_key_data_t *out)
{
    yaml_key_data_t kd;

    RETHROW(yaml_env_parse_key(env, &out->key, &out->key_span, NULL));

    RETHROW(yaml_env_ltrim(env));
    RETHROW(t_yaml_env_parse_flow_key_data(env, &kd));
    if (kd.key.s) {
        yaml_span_t span;

        /* This means the value was a key val mapping:
         *   a: b: c.
         * Place the ps on the end of the second key, to point to the second
         * colon. */
        span = kd.key_span;
        span.start = span.end;
        span.end.col_nb++;
        span.end.s++;
        return yaml_env_set_err_at(env, &span, YAML_ERR_WRONG_DATA,
                                   "unexpected colon");
    } else {
        out->data = kd.data;
    }

    return 0;
}

/* As inside a flow context, implicit key-value mappings are allowed, It is
 * easier to return a key_data object:
 *  * if a key:value mapping is parsed, a yaml_key_data_t object is returned.
 *  * otherwise, only out->data is filled, and out->key.s is set to NULL.
 */
static int t_yaml_env_parse_flow_key_data(yaml_parse_t *env,
                                          yaml_key_data_t *out)
{
    p_clear(out, 1);

    RETHROW(yaml_env_ltrim(env));
    if (ps_done(&env->ps)) {
        return yaml_env_set_err(env, YAML_ERR_MISSING_DATA,
                                "unexpected end of line");
    }

    if (ps_startswith_yaml_key(env->ps)) {
        RETHROW(t_yaml_env_parse_flow_key_val(env, out));
        goto end;
    }

    out->key = LSTR_NULL_V;
    if (ps_peekc(env->ps) == '[') {
        RETHROW(t_yaml_env_parse_flow_seq(env, &out->data));
    } else
    if (ps_peekc(env->ps) == '{') {
        RETHROW(t_yaml_env_parse_flow_obj(env, &out->data));
    } else {
        RETHROW(t_yaml_env_parse_scalar(env, true, &out->data));
    }

  end:
    log_new_data(&out->data);
    return 0;
}

/* }}} */
/* {{{ Override */

/* Some data can have an "override" that modifies the previously parsed
 * content.
 * This is for example true for !include, and would be true for anchors
 * if implemented.
 * These overrides are specified by defining an object on a greater
 * indentation than the including object:
 *
 * - !include foo.yml
 *   a: 2
 *   b: 5
 *
 * or
 *
 * key: !include bar.yml
 *   c: ~
 *
 * All the fields of the override are then merged in the modified data.
 * The merge strategy is this one (only merge with the same yaml data type is
 * allowed):
 *  * for scalars, the override overwrites the original data.
 *  * for sequences, all data from the override are added.
 *  * for obj, matched keys means recursing the merge in the inner datas, and
 *    unmatched keys are added.
 */

/* {{{ Merging */

static void
yaml_pres_override_add_node(const lstr_t path,
                            const yaml_data_t * nullable data,
                            qv_t(override_nodes) * nonnull nodes)

{
    yaml__presentation_override_node__t *node;

    node = qv_growlen(nodes, 1);
    iop_init(yaml__presentation_override_node, node);
    node->path = path;
    if (data) {
        node->original_data = t_iop_new(yaml__data);
        t_yaml_data_to_iop(data, node->original_data);
    }
}

static int
t_yaml_env_merge_data(yaml_parse_t * nonnull env,
                      const yaml_data_t * nonnull override,
                      yaml_presentation_override_t * nullable pres,
                      yaml_data_t * nonnull data);

static int
t_yaml_env_merge_key_data(yaml_parse_t * nonnull env,
                          const yaml_key_data_t * nonnull override,
                          yaml_presentation_override_t * nullable pres,
                          yaml_obj_t * nonnull obj)
{
    int prev_len = 0;

    tab_for_each_ptr(data_pair, &obj->fields) {
        if (lstr_equal(data_pair->key, override->key)) {
            if (pres) {
                prev_len = pres->path.len;

                sb_addf(&pres->path, ".%pL", &data_pair->key);
            }

            /* key found, recurse the merging of the inner data */
            RETHROW(t_yaml_env_merge_data(env, &override->data,
                                          pres, &data_pair->data));
            if (pres) {
                sb_clip(&pres->path, prev_len);
            }
            return 0;
        }
    }

    /* key not found, add the pair to the object. */
    logger_trace(&_G.logger, 2,
                 "merge new key from "YAML_POS_FMT" up to "YAML_POS_FMT,
                 YAML_POS_ARG(override->key_span.start),
                 YAML_POS_ARG(override->key_span.end));
    qv_append(&obj->fields, *override);

    if (pres) {
        lstr_t path = t_lstr_fmt("%pL.%pL", &pres->path, &override->key);

        yaml_pres_override_add_node(path, NULL, &pres->nodes);
    }

    return 0;
}

static int t_yaml_env_merge_obj(yaml_parse_t * nonnull env,
                                const yaml_obj_t * nonnull override,
                                yaml_presentation_override_t * nullable pres,
                                yaml_obj_t * nonnull obj)
{
    /* XXX: O(n^2), not great but normal usecase would never override
     * every key of a huge object, so the tradeoff is fine.
     */
    tab_for_each_ptr(pair, &override->fields) {
        if (!lstr_startswith(pair->key, LSTR("$"))) {
            RETHROW(t_yaml_env_merge_key_data(env, pair, pres, obj));
        }
    }

    return 0;
}

static int yaml_env_merge_seq(yaml_parse_t * nonnull env,
                              const yaml_seq_t * nonnull override,
                              const yaml_span_t * nonnull span,
                              yaml_presentation_override_t * nullable pres,
                              yaml_seq_t * nonnull seq)
{
    logger_trace(&_G.logger, 2,
                 "merging seq from "YAML_POS_FMT" up to "YAML_POS_FMT
                 " by appending its datas", YAML_POS_ARG(span->start),
                 YAML_POS_ARG(span->end));

    if (pres) {
        int len = seq->datas.len;
        for (int i = 0; i < override->datas.len; i++) {
            lstr_t path = t_lstr_fmt("%pL[%d]", &pres->path, len + i);

            yaml_pres_override_add_node(path, NULL, &pres->nodes);
        }
    }

    /* Until a proper syntax is found, seq merge are only additive */
    qv_extend(&seq->datas, &override->datas);
    qv_extend(&seq->pres_nodes, &override->pres_nodes);

    return 0;
}

static void
t_yaml_merge_scalar(const yaml_data_t * nonnull override,
                    yaml_presentation_override_t * nullable pres,
                    yaml_data_t * nonnull out)
{
    if (pres) {
        lstr_t path = t_lstr_dup(LSTR_SB_V(&pres->path));

        yaml_pres_override_add_node(path, out, &pres->nodes);
    }

    logger_trace(&_G.logger, 2,
                 "merging scalar from "YAML_POS_FMT" up to "YAML_POS_FMT,
                 YAML_POS_ARG(override->span.start),
                 YAML_POS_ARG(override->span.end));
    *out = *override;
}

static int
t_yaml_env_merge_data(yaml_parse_t * nonnull env,
                      const yaml_data_t * nonnull override,
                      yaml_presentation_override_t * nullable pres,
                      yaml_data_t * nonnull data)
{
    if (data->type != override->type) {
        const char *msg;

        /* XXX: This could be allowed, and implemented by completely replacing
         * the overridden data with the overriding one. However, the use-cases
         * are not clear, and it could hide errors, so reject it until a
         * valid use-case is found. */
        msg = t_fmt("overridden data is %s and not %s",
                    yaml_data_get_data_type(data),
                    yaml_data_get_data_type(override));
        return yaml_env_set_err_at(env, &override->span,
                                   YAML_ERR_INVALID_OVERRIDE, msg);
    }

    switch (data->type) {
      case YAML_DATA_SCALAR: {
        int prev_len = 0;

        if (pres) {
            prev_len = pres->path.len;

            sb_addc(&pres->path, '!');
        }

        t_yaml_merge_scalar(override, pres, data);

        if (pres) {
            sb_clip(&pres->path, prev_len);
        }
      } break;
      case YAML_DATA_SEQ:
        RETHROW(yaml_env_merge_seq(env, override->seq, &override->span,
                                   pres, data->seq));
        break;
      case YAML_DATA_OBJ:
        RETHROW(t_yaml_env_merge_obj(env, override->obj, pres, data->obj));
        break;
    }

    return 0;
}

/* }}} */
/* {{{ Override */

/* Set variables in the AST.
 *
 * \param[in]      env   The parsing environment.
 * \param[in]      data  The data containing variables bindings.
 * \param[in,out]  variables  Information about unbound variables in the AST.
 * \param[out]     pres  Presentation details.
 */
static int
yaml_env_set_variables(yaml_parse_t * nonnull env,
                       const yaml_data_t * nonnull data,
                       qm_t(yaml_vars) * nonnull variables,
                       yaml__presentation_include__t * nullable pres)
{
    if (data->type != YAML_DATA_OBJ) {
        return yaml_env_set_err_at(env, &data->span, YAML_ERR_WRONG_DATA,
                                   "variable settings must be an object");
    }

    if (pres) {
        qv_t(var_binding) bindings;

        t_qv_init(&bindings, data->obj->fields.len);

        RETHROW(t_yaml_env_replace_variables(env, data->obj, variables,
                                             &bindings));

        pres->variables = t_iop_new(yaml__presentation_variable_settings);
        pres->variables->bindings
            = IOP_TYPED_ARRAY_TAB(yaml__presentation_variable_binding,
                                  &bindings);
    } else {
        RETHROW(t_yaml_env_replace_variables(env, data->obj, variables,
                                             NULL));
    }

    return 0;
}

static int
t_yaml_env_handle_override(yaml_parse_t * nonnull env,
                           yaml_data_t * nonnull override,
                           qm_t(yaml_vars) * nonnull variables,
                           yaml__presentation_include__t * nullable pres,
                           yaml_data_t * nonnull out)
{
    yaml_presentation_override_t *ov_pres = NULL;

    if (override->type == YAML_DATA_SCALAR
    &&  override->scalar.type == YAML_SCALAR_NULL)
    {
        /* no overrides, standard '!include:foo.yml' case. */
        return 0;
    }

    if (override->type != YAML_DATA_OBJ) {
        return yaml_env_set_err_at(env, &override->span, YAML_ERR_WRONG_DATA,
                                   "override data after include "
                                   "must be an object");
    }

    if (pres) {
        ov_pres = t_new(yaml_presentation_override_t, 1);
        t_qv_init(&ov_pres->nodes, 0);
        t_sb_init(&ov_pres->path, 1024);
    }


    /* If the first key of the obj is "variables", use it to set variables
     * in the AST. */
    /* TODO: this forbids overriding a "variables" field. This could be fixed
     * by adding a more explicit syntax in that case, for example:
     *
     * !include
     *   path: foo.yml
     *   variables: ...
     *   override: ...
     *
     * For the moment, it isn't a big deal however.
     */
    if (override->obj->fields.len > 0
    &&  lstr_equal(override->obj->fields.tab[0].key, LSTR("variables")))
    {
        yaml_obj_t *obj = override->obj;

        RETHROW(yaml_env_set_variables(env, &obj->fields.tab[0].data,
                                       variables, pres));
        obj->fields.tab++;
        obj->fields.len--;
        if (obj->fields.len == 0) {
            return 0;
        }
    }

    RETHROW(t_yaml_env_merge_data(env, override, ov_pres, out));

    if (ov_pres) {
        pres->override = t_presentation_override_to_iop(ov_pres, override);
    }

    return 0;
}

/* }}} */
/* }}} */
/* {{{ Data */

static int t_yaml_env_parse_data(yaml_parse_t *env, const uint32_t min_indent,
                                 yaml_data_t *out)
{
    uint32_t cur_indent;

    RETHROW(yaml_env_ltrim(env));
    cur_indent = yaml_env_get_column_nb(env);
    if (cur_indent < min_indent || ps_done(&env->ps)) {
        yaml_env_start_data(env, YAML_DATA_SCALAR, out);
        yaml_env_end_data(env, out);
        out->scalar.type = YAML_SCALAR_NULL;

        if (env->flags & YAML_PARSE_GEN_PRES_DATA) {
            yaml__presentation_node__t *node;

            node = t_yaml_env_pres_get_current_node(env->pres);
            node->empty_null = true;
        }

        return 0;
    }

    if (ps_peekc(env->ps) == '!') {
        yaml_tag_type_t type = YAML_TAG_TYPE_NONE;

        RETHROW(t_yaml_env_parse_tag(env, min_indent, out, &type));
        switch (type) {
          case YAML_TAG_TYPE_INCLUDE:
            RETHROW(t_yaml_env_handle_include(env, min_indent + 1, false,
                                              out));
            break;
          case YAML_TAG_TYPE_INCLUDERAW:
            RETHROW(t_yaml_env_handle_include(env, min_indent + 1, true,
                                              out));
            break;
          case YAML_TAG_TYPE_NONE:
            break;
        }
    } else
    if (ps_startswith_yaml_seq_prefix(&env->ps)) {
        RETHROW(t_yaml_env_parse_seq(env, cur_indent, out));
    } else
    if (ps_peekc(env->ps) == '[') {
        unsigned flags = env->flags;
        int res;

        env->flags |= YAML_PARSE_FORBID_VARIABLES;
        res = t_yaml_env_parse_flow_seq(env, out);
        env->flags = flags;
        RETHROW(res);
        if (out->seq->datas.len > 0) {
            t_yaml_env_pres_set_flow_mode(env);
        }
    } else
    if (ps_peekc(env->ps) == '{') {
        unsigned flags = env->flags;
        int res;

        env->flags |= YAML_PARSE_FORBID_VARIABLES;
        res = t_yaml_env_parse_flow_obj(env, out);
        env->flags = flags;
        RETHROW(res);
        if (out->obj->fields.len > 0) {
            t_yaml_env_pres_set_flow_mode(env);
        }
    } else
    if (ps_startswith_yaml_key(env->ps)) {
        RETHROW(t_yaml_env_parse_obj(env, cur_indent, out));
    } else {
        RETHROW(t_yaml_env_parse_scalar(env, false, out));
    }

    log_new_data(out);
    return 0;
}

/* }}} */
/* }}} */
/* {{{ Generate presentations */

qvector_t(pres_mapping, yaml__presentation_node_mapping__t);

static void
add_mapping(const sb_t * nonnull sb_path,
            const yaml__presentation_node__t * nonnull node,
            qv_t(pres_mapping) * nonnull out)
{
    yaml__presentation_node_mapping__t *mapping;

    mapping = qv_growlen(out, 1);
    iop_init(yaml__presentation_node_mapping, mapping);

    mapping->path = t_lstr_dup(LSTR_SB_V(sb_path));
    mapping->node = *node;
}

static void
t_yaml_add_pres_mappings(const yaml_data_t * nonnull data, sb_t *path,
                         qv_t(pres_mapping) * nonnull mappings)
{
    if (data->presentation) {
        int prev_len = path->len;

        sb_addc(path, '!');
        add_mapping(path, data->presentation, mappings);
        sb_clip(path, prev_len);

        if (data->presentation->included || data->presentation->merge_key) {
            return;
        }
    }

    switch (data->type) {
      case YAML_DATA_SCALAR:
        break;

      case YAML_DATA_SEQ: {
        int prev_len = path->len;

        tab_enumerate_ptr(pos, val, &data->seq->datas) {
            sb_addf(path, "[%d]", pos);
            if (pos < data->seq->pres_nodes.len) {
                yaml__presentation_node__t *node;

                node = data->seq->pres_nodes.tab[pos];
                if (node) {
                    add_mapping(path, node, mappings);
                }
            }
            t_yaml_add_pres_mappings(val, path, mappings);
            sb_clip(path, prev_len);
        }
      } break;

      case YAML_DATA_OBJ: {
        int prev_len = path->len;

        tab_for_each_ptr(kv, &data->obj->fields) {
            sb_addf(path, ".%pL", &kv->key);
            if (kv->key_presentation) {
                add_mapping(path, kv->key_presentation, mappings);
            }
            t_yaml_add_pres_mappings(&kv->data, path, mappings);
            sb_clip(path, prev_len);
        }
      } break;
    }
}

/* }}} */
/* {{{ Parser public API */

yaml_parse_t *t_yaml_parse_new(int flags)
{
    yaml_parse_t *env;

    env = t_new(yaml_parse_t, 1);
    env->flags = flags;
    t_sb_init(&env->err, 1024);
    t_qv_init(&env->subfiles, 0);
    t_qm_init(yaml_vars, &env->variables, 0);

    return env;
}

void yaml_parse_delete(yaml_parse_t **env)
{
    if (!(*env)) {
        return;
    }
    lstr_wipe(&(*env)->file_contents);
    qv_deep_clear(&(*env)->subfiles, yaml_parse_delete);
}

void yaml_parse_attach_ps(yaml_parse_t *env, pstream_t ps)
{
    env->ps = ps;
    env->pos_newline = ps.s;
    env->line_number = 1;
}

int
t_yaml_parse_attach_file(yaml_parse_t *env, const char *filepath,
                         const char *dirpath, sb_t *err)
{
    char fullpath[PATH_MAX];
    char realpath[PATH_MAX];

    path_extend(fullpath, dirpath ?: "", "%s", filepath);
    path_simplify(fullpath);

    if (!env->rootdirpath && dirpath) {
        lstr_t lstr_dirpath = LSTR(dirpath);

        /* to work with path_relative_to, dirpath must end with a '/' */
        if (lstr_endswith(lstr_dirpath, LSTR("/"))) {
            env->rootdirpath = t_fmt("%pL", &lstr_dirpath);
        } else {
            env->rootdirpath = t_fmt("%pL/", &lstr_dirpath);
        }
    }

    /* detect includes that are not contained in the root directory */
    if (env->rootdirpath) {
        char relative_path[PATH_MAX];

        path_relative_to(relative_path, env->rootdirpath, fullpath);
        if (lstr_startswith(LSTR(relative_path), LSTR(".."))) {
            sb_setf(err, "cannot include subfile `%s`: "
                    "only includes contained in the directory of the "
                    "including file are allowed", filepath);
            return -1;
        }
    }

    if (lstr_init_from_file(&env->file_contents, fullpath, PROT_READ,
                            MAP_SHARED) < 0)
    {
        sb_setf(err, "cannot read file %s: %m", filepath);
        return -1;
    }

    env->filepath = t_strdup(filepath);

    /* Canonify to resolve symbolic links, and to make sure includes
     * are resolved relative to the real path to the file. */
    if (path_canonify(realpath, PATH_MAX, fullpath) < 0) {
        sb_setf(err, "cannot canonify path %s: %m", fullpath);
        return -1;
    }

    if (!strequal(realpath, fullpath)) {
        char realdirpath[PATH_MAX];

        path_dirname(realdirpath, PATH_MAX, realpath);
        env->rootdirpath = t_fmt("%s/", realdirpath);
        logger_trace(&_G.logger, 2,
                     "include done through a symbolic link, root path is "
                     "updated to `%s`", env->rootdirpath);
    }
    env->fullpath = t_lstr_dup(LSTR(realpath));
    yaml_parse_attach_ps(env, ps_initlstr(&env->file_contents));

    return 0;
}

static void
set_unbound_variables_err(yaml_parse_t *env)
{
    SB_1k(buf);

    /* build list of unbound variable names */
    qm_for_each_key(yaml_vars, name, &env->variables) {
        if (buf.len > 0) {
            sb_adds(&buf, ", ");
        }
        sb_add_lstr(&buf, name);
    }

    /* TODO: maybe pretty printing the locations of the unbound variables
     * would be useful */
    sb_setf(&env->err, "the document is invalid: "
            "there are unbound variables: %pL", &buf);
}

int t_yaml_parse(yaml_parse_t *env, yaml_data_t *out, sb_t *out_err)
{
    pstream_t saved_ps = env->ps;
    int res = 0;

    if (env->flags & YAML_PARSE_GEN_PRES_DATA) {
        env->pres = t_new(yaml_env_presentation_t, 1);
    }

    assert (env->ps.s && "yaml_parse_attach_ps/file must be called first");
    if (t_yaml_env_parse_data(env, 0, out) < 0) {
        res = -1;
        goto end;
    }

    RETHROW(yaml_env_ltrim(env));
    if (!ps_done(&env->ps)) {
        yaml_env_set_err(env, YAML_ERR_EXTRA_DATA,
                         "expected end of document");
        res = -1;
        goto end;
    }

    if (qm_len(yaml_vars, &env->variables) > 0
    &&  !(env->flags & YAML_PARSE_ALLOW_UNBOUND_VARIABLES))
    {
        set_unbound_variables_err(env);
        res = -1;
        goto end;
    }

  end:
    if (res < 0) {
        sb_setsb(out_err, &env->err);
    }
    /* reset the stream to the input, so that it can be properly returned
     * by yaml_parse_get_stream(). */
    env->ps = saved_ps;
    return res;
}

void t_yaml_data_get_presentation(
    const yaml_data_t * nonnull data,
    yaml__document_presentation__t * nonnull pres
)
{
    qv_t(pres_mapping) mappings;
    SB_1k(path);

    iop_init(yaml__document_presentation, pres);
    t_qv_init(&mappings, 0);
    t_yaml_add_pres_mappings(data, &path, &mappings);
    pres->mappings = IOP_TYPED_ARRAY_TAB(yaml__presentation_node_mapping,
                                         &mappings);
}

static const yaml_presentation_t * nonnull
t_yaml_doc_pres_to_map(const yaml__document_presentation__t *doc_pres)
{
    yaml_presentation_t *pres = t_new(yaml_presentation_t, 1);

    t_qm_init(yaml_pres_node, &pres->nodes, 0);
    tab_for_each_ptr(mapping, &doc_pres->mappings) {
        int res;

        res = qm_add(yaml_pres_node, &pres->nodes, &mapping->path,
                     &mapping->node);
        assert (res >= 0);
    }

    return pres;
}

void yaml_parse_pretty_print_err(const yaml_span_t * nonnull span,
                                 lstr_t error_msg, sb_t * nonnull out)
{
    pstream_t ps;
    bool one_liner;

    if (span->env->included) {
        yaml_parse_pretty_print_err(&span->env->included->data.span,
                                    LSTR("error in included file"), out);
        sb_addc(out, '\n');
    }

    if (span->env->filepath) {
        sb_addf(out, "%s:", span->env->filepath);
    } else {
        sb_adds(out, "<string>:");
    }
    sb_addf(out, YAML_POS_FMT": %pL", YAML_POS_ARG(span->start), &error_msg);

    one_liner = span->end.line_nb == span->start.line_nb
             && span->end.col_nb != span->start.col_nb;

    /* get the full line including pos_start */
    ps.s = span->start.s;
    ps.s -= span->start.col_nb - 1;

    /* find the end of the line */
    ps.s_end = one_liner ? span->end.s - 1 : ps.s;
    while (ps.s_end < span->env->ps.s_end && *ps.s_end != '\n') {
        ps.s_end++;
    }
    if (ps_len(&ps) == 0) {
        return;
    }

    /* print the whole line */
    sb_addf(out, "\n%*pM\n", PS_FMT_ARG(&ps));

    /* then display some indications or where the issue is */
    if (span->start.col_nb > 1) {
        sb_addnc(out, span->start.col_nb - 1, ' ');
    }
    if (one_liner) {
        assert (span->end.col_nb > span->start.col_nb);
        sb_addnc(out, span->end.col_nb - span->start.col_nb, '^');
    } else {
        sb_adds(out, "^ starting here");
    }
}

/* }}} */
/* {{{ Packer */
/* {{{ Packing types */
/* {{{ Variables */

typedef struct yaml_pack_variable_t yaml_pack_variable_t;

/** Deduced value of a variable. */
struct yaml_pack_variable_t {
    /* Name of the variable */
    lstr_t name;

    /* If NULL, variable's value has not been deduced yet. */
    const yaml_data_t * nullable deduced_value;

    /* Original value used for the variable. Only set if the variable was
     * used in strings, see PresentationVariableBinding::value */
    lstr_t original_value;

    /* Chaining to a new variable, created in result of a conflict.
     *
     * If the variable is used in multiple places, but deduced values do
     * not match, the conflict resolution will add new variables to hold
     * the different values.
     * These new variables must not match any existing names in any active
     * vars map, and must be reusable if multiple deduce values matches.
     *
     * For example:
     *  Resolving [ $(foo), $(foo), <$(foo)>] with [ 1, 2, <2> ] should yield:
     *   [ $(foo), $(foo~1), <$(foo~1)> ]
     *   and
     *     $(foo): 1
     *     $(foo~1): 2
     *
     * Chaining the conflicts directly in the original variable object makes
     * it easier to resolve them rather than adding them directly in the
     * active_vars map.
     */
    yaml_pack_variable_t * nullable conflict;
};

qvector_t(pack_var, yaml_pack_variable_t * nonnull);

/* Mapping from variable name, to deduced value */
qm_kvec_t(active_vars, lstr_t, yaml_pack_variable_t, qhash_lstr_hash,
          qhash_lstr_equal);

qvector_t(active_vars, qm_t(active_vars));

/* }}} */
/* }}} */

#define YAML_STD_INDENT  2

/* State describing the state of the packing "cursor".
 *
 * This is used to properly insert newlines & indentations between every key,
 * sequence, data, comments, etc.
 */
typedef enum yaml_pack_state_t {
    /* Clean state for writing. This state is required before writing any
     * new data. */
    PACK_STATE_CLEAN,

    /* On sequence dash, ie the "-" of a new sequence element. */
    PACK_STATE_ON_DASH,

    /* On object key, ie the ":" of a new object key. */
    PACK_STATE_ON_KEY,

    /* On a newline */
    PACK_STATE_ON_NEWLINE,

    /* After having wrote data. */
    PACK_STATE_AFTER_DATA,
} yaml_pack_state_t;

qm_kvec_t(path_to_checksum, lstr_t, uint64_t, qhash_lstr_hash,
          qhash_lstr_equal);

typedef struct yaml_pack_env_t {
    /* Write callback + priv data. */
    yaml_pack_writecb_f *write_cb;
    void *priv;

    /* Current packing state.
     *
     * Used to prettify the output by correctly transitioning between states.
     */
    yaml_pack_state_t state;

    /* Indent level (in number of spaces). */
    int indent_lvl;

    /* Presentation data, if provided. */
    const yaml_presentation_t * nullable pres;

    /* Path from the root document.
     *
     * When packing the root document, this is equivalent to the current path.
     * However, when packing a subfile, this includes the path from the
     * including document.
     * To get the current path only, see yaml_pack_env_get_curpath.
     *
     * Absolute paths are used for overrides through includes, see
     * yaml_pack_override_t::absolute_path.
     */
    sb_t absolute_path;

    /* Start of current path being packed.
     *
     * This is the index of the start of the current path in the
     * absolute_path buffer.
     */
    unsigned current_path_pos;

    /* Error buffer. */
    sb_t err;

    /* Path to the output directory. */
    lstr_t outdirpath;

    /* Flags to use when creating subfiles. */
    unsigned file_flags;

    /* Mode to use when creating subfiles. */
    mode_t file_mode;

    /* Bitfield of yaml_pack_flags_t elements. */
    unsigned flags;

    /* Packed subfiles.
     *
     * Associates paths to created subfiles with a checksum of the file's
     * content. This is used to handle shared subfiles.
     */
    qm_t(path_to_checksum) * nullable subfiles;

    /** Information about overridden values.
     *
     * This is a *stack* of currently active overrides. The last element
     * is the most recent override, and matching overridden values should thus
     * be done in reverse order.
     */
    qv_t(pack_override) overrides;

    /** Information about substituted variables.
     *
     * This is a *stack* of currently active variables (i.e., variable names
     * that are handled by an override). The last element is the most recent
     * override, and matching variable values should thus be done in reverse
     * order.
     */
    qv_t(active_vars) active_vars;
} yaml_pack_env_t;

static int t_yaml_pack_data(yaml_pack_env_t * nonnull env,
                            const yaml_data_t * nonnull data);

/* {{{ Utils */

static int do_write(yaml_pack_env_t *env, const void *_buf, int len)
{
    const uint8_t *buf = _buf;
    int pos = 0;

    while (pos < len) {
        int res = (*env->write_cb)(env->priv, buf + pos, len - pos,
                                   &env->err);

        if (res < 0) {
            if (ERR_RW_RETRIABLE(errno)) {
                continue;
            }
            return -1;
        }
        pos += res;
    }
    return len;
}

static int do_indent(yaml_pack_env_t *env)
{
    static lstr_t spaces = LSTR_IMMED("                                    ");
    int todo = env->indent_lvl;

    while (todo > 0) {
        int res = (*env->write_cb)(env->priv, spaces.s, MIN(spaces.len, todo),
                                   &env->err);

        if (res < 0) {
            if (ERR_RW_RETRIABLE(errno)) {
                continue;
            }
            return -1;
        }
        todo -= res;
    }

    env->state = PACK_STATE_CLEAN;

    return env->indent_lvl;
}

#define WRITE(data, len)                                                     \
    do {                                                                     \
        res += RETHROW(do_write(env, data, len));                            \
    } while (0)
#define PUTS(s)  WRITE(s, strlen(s))
#define PUTLSTR(s)  WRITE(s.data, s.len)

#define INDENT()                                                             \
    do {                                                                     \
        res += RETHROW(do_indent(env));                                      \
    } while (0)

#define GOTO_STATE(state)                                                    \
    do {                                                                     \
        res += RETHROW(yaml_pack_goto_state(env, PACK_STATE_##state));       \
    } while (0)

static int yaml_pack_goto_state(yaml_pack_env_t *env,
                                yaml_pack_state_t new_state)
{
    int res = 0;

    switch (env->state) {
      case PACK_STATE_CLEAN:
        switch (new_state) {
          case PACK_STATE_ON_NEWLINE:
            PUTS("\n");
            break;
          case PACK_STATE_AFTER_DATA:
          case PACK_STATE_CLEAN:
          case PACK_STATE_ON_DASH:
          case PACK_STATE_ON_KEY:
            break;
        };
        break;

      case PACK_STATE_ON_DASH:
        switch (new_state) {
          case PACK_STATE_CLEAN:
          case PACK_STATE_ON_KEY:
          case PACK_STATE_ON_DASH:
            /* a key or seq dash is put on the same line as the seq dash */
            PUTS(" ");
            break;
          case PACK_STATE_ON_NEWLINE:
            PUTS("\n");
            break;
          case PACK_STATE_AFTER_DATA:
            break;
        };
        break;

      case PACK_STATE_ON_KEY:
        switch (new_state) {
          case PACK_STATE_CLEAN:
            PUTS(" ");
            break;
          case PACK_STATE_ON_NEWLINE:
            PUTS("\n");
            break;
          case PACK_STATE_ON_DASH:
          case PACK_STATE_ON_KEY:
            /* a seq dash or a new key is put on a newline after the key */
            PUTS("\n");
            INDENT();
            break;
          case PACK_STATE_AFTER_DATA:
            break;
        };
        break;

      case PACK_STATE_ON_NEWLINE:
        switch (new_state) {
          case PACK_STATE_CLEAN:
          case PACK_STATE_ON_DASH:
          case PACK_STATE_ON_KEY:
            INDENT();
            break;
          case PACK_STATE_ON_NEWLINE:
          case PACK_STATE_AFTER_DATA:
            break;
        };
        break;

      case PACK_STATE_AFTER_DATA:
        switch (new_state) {
          case PACK_STATE_ON_NEWLINE:
            PUTS("\n");
            break;
          case PACK_STATE_CLEAN:
            PUTS(" ");
            break;
          case PACK_STATE_ON_DASH:
          case PACK_STATE_ON_KEY:
            PUTS("\n");
            INDENT();
            break;
          case PACK_STATE_AFTER_DATA:
            break;
        };
        break;
    }

    env->state = new_state;

    return res;
}

static int yaml_pack_tag(yaml_pack_env_t * nonnull env, const lstr_t tag)
{
    int res = 0;

    if (tag.s) {
        GOTO_STATE(CLEAN);
        PUTS("!");
        PUTLSTR(tag);
        env->state = PACK_STATE_AFTER_DATA;
    }

    return res;
}

static yaml_pack_override_node_t * nullable
yaml_pack_env_find_override(yaml_pack_env_t * nonnull env)
{
    t_scope;
    lstr_t abspath;

    if (env->overrides.len == 0) {
        return NULL;
    }

    abspath = LSTR_SB_V(&env->absolute_path);
    tab_for_each_pos_rev(ov_pos, &env->overrides) {
        yaml_pack_override_t *override = &env->overrides.tab[ov_pos];
        int qm_pos;

        qm_pos = qm_find(override_nodes, &override->nodes, &abspath);
        if (qm_pos >= 0) {
            return &override->nodes.values[qm_pos];
        }
    }

    return NULL;
}

static int t_yaml_pack_data_with_doc_pres(
    yaml_pack_env_t * nonnull env, const yaml_data_t * nonnull data,
    const yaml__document_presentation__t * nonnull doc_pres)
{
    const yaml_presentation_t *pres;
    unsigned current_path_pos;
    int res;

    pres = t_yaml_doc_pres_to_map(doc_pres);

    current_path_pos = env->absolute_path.len;

    SWAP(const yaml_presentation_t *, pres, env->pres);
    SWAP(unsigned, current_path_pos, env->current_path_pos);

    res = t_yaml_pack_data(env, data);

    SWAP(unsigned, current_path_pos, env->current_path_pos);
    SWAP(const yaml_presentation_t *, pres, env->pres);

    return res;
}

/* }}} */
/* {{{ Presentation */

/* XXX: need a prototype declaration to specify the __attr_printf__ */
static int
yaml_pack_env_push_path(yaml_pack_env_t * nullable env, const char *fmt,
                        ...)
    __attr_printf__(2, 3);

static int
yaml_pack_env_push_path(yaml_pack_env_t * nullable env, const char *fmt,
                        ...)
{
    int prev_len;
    va_list args;

    if (!env->pres) {
        return 0;
    }

    prev_len = env->absolute_path.len;
    va_start(args, fmt);
    sb_addvf(&env->absolute_path, fmt, args);
    va_end(args);

    return prev_len;
}

static void
yaml_pack_env_pop_path(yaml_pack_env_t * nullable env, int prev_len)
{
    if (!env->pres) {
        return;
    }

    sb_clip(&env->absolute_path, prev_len);
}

static lstr_t
yaml_pack_env_get_curpath(const yaml_pack_env_t * nonnull env)
{
    return LSTR_PTR_V(env->absolute_path.data + env->current_path_pos,
                      env->absolute_path.data + env->absolute_path.len);
}

static const yaml__presentation_node__t * nullable
yaml_pack_env_get_pres_node(yaml_pack_env_t * nonnull env)
{
    lstr_t path = yaml_pack_env_get_curpath(env);

    assert (env->pres);
    return qm_get_def_safe(yaml_pres_node, &env->pres->nodes, &path, NULL);
}

static int
yaml_pack_empty_lines(yaml_pack_env_t * nonnull env, uint8_t nb_lines)
{
    int res = 0;

    if (nb_lines == 0) {
        return 0;
    }

    GOTO_STATE(ON_NEWLINE);
    for (uint8_t i = 0; i < nb_lines; i++) {
        PUTS("\n");
    }

    return res;
}

static int
yaml_pack_pres_node_prefix(yaml_pack_env_t * nonnull env,
                           const yaml__presentation_node__t * nullable node)
{
    int res = 0;

    if (!node) {
        return 0;
    }

    res += yaml_pack_empty_lines(env, node->empty_lines);

    if (node->prefix_comments.len == 0) {
        return 0;
    }
    GOTO_STATE(ON_NEWLINE);
    tab_for_each_entry(comment, &node->prefix_comments) {
        GOTO_STATE(CLEAN);

        PUTS("# ");
        PUTLSTR(comment);
        PUTS("\n");
        env->state = PACK_STATE_ON_NEWLINE;
    }

    return res;
}

static int
yaml_pack_pres_node_inline(yaml_pack_env_t * nonnull env,
                           const yaml__presentation_node__t * nullable node)
{
    int res = 0;

    if (node && node->inline_comment.len > 0) {
        GOTO_STATE(CLEAN);
        PUTS("# ");
        PUTLSTR(node->inline_comment);
        PUTS("\n");
        env->state = PACK_STATE_ON_NEWLINE;
    }

    return res;
}

/* }}} */
/* {{{ Pack scalar */

/* ints:   sign, 20 digits, and NUL -> 22
 * double: sign, digit, dot, 17 digits, e, sign, up to 3 digits NUL -> 25
 */
#define IBUF_LEN  25

static bool
yaml_string_must_be_quoted(const lstr_t s,
                           const yaml__presentation_node__t * nullable pres)
{
    /* '!', '&', '*', '-', '"' and '.' have special YAML meaning.
     * Technically, '-' is only forbidden if followed by a space,
     * but it is simpler that way.
     * Also forbid starting with '[' or '{'. In YAML, this indicates inline
     * JSON, which we do not handle in our parser, but would render the YAML
     * invalid for other parsers.
     */
    static ctype_desc_t const yaml_invalid_raw_string_start = { {
        0x00000000, 0x00006446, 0x08000000, 0x08000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    /* printable ascii characters minus ':' and '#'. Also should be
     * followed by space to be forbidden, but simpler that way. */
    static ctype_desc_t const yaml_raw_string_contains = { {
        0x00000000, 0xfbfffff7, 0xffffffff, 0xffffffff,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };

    if (s.len == 0) {
        return true;
    }

    if (pres && pres->quoted) {
        return true;
    }

    /* If string is a template, only quote if the template contains an
     * escaped var */
    if (pres && pres->tpl) {
        if (pres->tpl->variables_bitmap.len > 0) {
            return true;
        }
    } else {
        /* If not a template, use of '$(' must be escaped, and thus quoted. */
        if (lstr_contains(s, LSTR("$("))) {
            return true;
        }
    }

    /* cannot start with those characters */
    if (ctype_desc_contains(&yaml_invalid_raw_string_start, s.s[0])) {
        return true;
    }
    /* cannot contain those characters */
    if (!lstr_match_ctype(s, &yaml_raw_string_contains)) {
        return true;
    }
    /* cannot start or end with a space */
    if (lstr_startswith(s, LSTR(" ")) || lstr_endswith(s, LSTR(" "))) {
        return true;
    }
    if (lstr_equal(s, LSTR("~")) || lstr_equal(s, LSTR("null"))) {
        return true;
    }

    return false;
}

static int yaml_pack_string(yaml_pack_env_t *env, lstr_t val,
                            const yaml__presentation_node__t * nullable pres)
{
    int res = 0;
    pstream_t ps;
    qv_t(u8) bitmap;
    qv_t(u8) *var_bitmap = NULL;
    int var_pos = 0;

    if (!yaml_string_must_be_quoted(val, pres)) {
        PUTLSTR(val);
        return res;
    }

    if (pres && pres->tpl) {
        qv_init_static_tab(&bitmap, &pres->tpl->variables_bitmap);
        var_bitmap = &bitmap;
    }

    ps = ps_initlstr(&val);
    PUTS("\"");
    while (!ps_done(&ps)) {
        /* r:32-127 -s:'\\"$' */
        static ctype_desc_t const safe_chars = { {
            0x00000000, 0xffffffeb, 0xefffffff, 0xffffffff,
                0x00000000, 0x00000000, 0x00000000, 0x00000000,
        } };
        const uint8_t *p = ps.b;
        size_t nbchars;
        int c;

        nbchars = ps_skip_span(&ps, &safe_chars);
        WRITE(p, nbchars);

        if (ps_done(&ps)) {
            break;
        }

        /* Assume broken utf-8 is mixed latin1 */
        c = ps_getuc(&ps);
        if (unlikely(c < 0)) {
            c = ps_getc(&ps);
        }
        switch (c) {
          case '$':
            if (ps_peekc(ps) != '(') {
                PUTS("$");
                break;
            }
            ps_skip(&ps, 1);

            /* If in a template, we need to quote '$'. Otherwise, we must
             * keep it as is if it is a variable */
            if (var_bitmap) {
                var_pos++;

                if (var_bitmap_test_bit(var_bitmap, var_pos - 1)) {
                    PUTS("$(");
                    break;
                }
            }
            PUTS("\\$(");
            break;
          case '"':  PUTS("\\\""); break;
          case '\\': PUTS("\\\\"); break;
          case '\a': PUTS("\\a"); break;
          case '\b': PUTS("\\b"); break;
          case '\e': PUTS("\\e"); break;
          case '\f': PUTS("\\f"); break;
          case '\n': PUTS("\\n"); break;
          case '\r': PUTS("\\r"); break;
          case '\t': PUTS("\\t"); break;
          case '\v': PUTS("\\v"); break;
          default: {
            char ibuf[IBUF_LEN];

            WRITE(ibuf, sprintf(ibuf, "\\u%04x", c));
          } break;
        }
    }
    PUTS("\"");

    return res;
}

/* XXX: If modifying this function, changes must be reflected in
 * t_yaml_scalar_to_string.
 */
static int
yaml_pack_scalar(yaml_pack_env_t * nonnull env,
                 const yaml_scalar_t * nonnull scalar, const lstr_t tag,
                 const yaml__presentation_node__t * nullable pres)
{
    int res = 0;
    char ibuf[IBUF_LEN];

    if (unlikely(scalar->type == YAML_SCALAR_NULL && pres
              && pres->empty_null))
    {
        /* special case for empty null scalar: do nothing. This allows
         * factorizing the GOTO_STATE as the other cases will all write
         * something. */
        goto end;
    }

    GOTO_STATE(CLEAN);

    switch (scalar->type) {
      case YAML_SCALAR_STRING:
        res += yaml_pack_string(env, scalar->s, pres);
        break;

      case YAML_SCALAR_DOUBLE: {
        int inf = isinf(scalar->d);

        if (inf == 1) {
            PUTS(".Inf");
        } else
        if (inf == -1) {
            PUTS("-.Inf");
        } else
        if (isnan(scalar->d)) {
            PUTS(".NaN");
        } else {
            WRITE(ibuf, sprintf(ibuf, "%g", scalar->d));
        }
      } break;

      case YAML_SCALAR_UINT:
        WRITE(ibuf, sprintf(ibuf, "%ju", scalar->u));
        break;

      case YAML_SCALAR_INT:
        WRITE(ibuf, sprintf(ibuf, "%jd", scalar->i));
        break;

      case YAML_SCALAR_BOOL:
        if (scalar->b) {
            PUTS("true");
        } else {
            PUTS("false");
        }
        break;

      case YAML_SCALAR_NULL:
        PUTS("~");
        break;

      case YAML_SCALAR_BYTES: {
        t_scope;
        t_SB_1k(sb);

        sb_addlstr_b64(&sb, scalar->s, -1);
        res += yaml_pack_string(env, LSTR_SB_V(&sb), pres);
      } break;
    }

  end:
    env->state = PACK_STATE_AFTER_DATA;

    return res;
}

/* }}} */
/* {{{ Pack sequence */

static int t_yaml_pack_seq(yaml_pack_env_t * nonnull env,
                           const yaml_seq_t * nonnull seq)
{
    int res = 0;

    tab_for_each_pos(pos, &seq->datas) {
        const yaml__presentation_node__t *node = NULL;
        const yaml_data_t *data = &seq->datas.tab[pos];
        yaml_pack_override_node_t *override = NULL;
        int path_len = 0;

        if (env->pres) {
            path_len = yaml_pack_env_push_path(env, "[%d]", pos);
            node = yaml_pack_env_get_pres_node(env);
        } else
        if (pos < seq->pres_nodes.len && seq->pres_nodes.tab[pos]) {
            node = seq->pres_nodes.tab[pos];
        }

        override = yaml_pack_env_find_override(env);
        if (override && likely(!override->data)) {
            /* The node was added by an override. Save it in the override
             * data, and ignore the node. */
            logger_trace(&_G.logger, 2,
                         "not packing overridden data in path `%*pM`",
                         LSTR_FMT_ARG(yaml_pack_env_get_curpath(env)));
            override->data = data;
            override->found = true;
            goto next;
        }

        res += yaml_pack_pres_node_prefix(env, node);

        GOTO_STATE(ON_DASH);
        PUTS("-");

        env->indent_lvl += YAML_STD_INDENT;
        res += yaml_pack_pres_node_inline(env, node);
        res += RETHROW(t_yaml_pack_data(env, data));
        env->indent_lvl -= YAML_STD_INDENT;

      next:
        yaml_pack_env_pop_path(env, path_len);
    }

    if (res == 0) {
        /* XXX: This can happen if all elements come from an override: in
         * that case, pack an empty array. */
        GOTO_STATE(CLEAN);
        PUTS("[]");
        env->state = PACK_STATE_AFTER_DATA;
        return res;
    }

    return res;
}

/* }}} */
/* {{{ Pack object */

static int t_yaml_pack_key_data(yaml_pack_env_t * nonnull env,
                                const yaml_key_data_t * nonnull kd)
{
    int res = 0;
    int path_len = 0;
    const yaml__presentation_node__t *node;
    yaml_pack_override_node_t *override = NULL;

    if (env->pres) {
        path_len = yaml_pack_env_push_path(env, ".%pL", &kd->key);
        node = yaml_pack_env_get_pres_node(env);
    } else {
        node = kd->key_presentation;
    }

    override = yaml_pack_env_find_override(env);
    if (override && likely(!override->data)) {
        /* The node was added by an override. Save it in the override
         * data, and ignore the node. */
        logger_trace(&_G.logger, 2, "not packing overridden data in path "
                     "`%*pM`", LSTR_FMT_ARG(yaml_pack_env_get_curpath(env)));
        override->data = &kd->data;
        override->found = true;
        goto end;
    }

    res += yaml_pack_pres_node_prefix(env, node);

    GOTO_STATE(ON_KEY);
    PUTLSTR(kd->key);
    PUTS(":");

    /* for scalars, we put the inline comment after the value:
     *  key: val # comment
     */
    env->indent_lvl += YAML_STD_INDENT;
    res += yaml_pack_pres_node_inline(env, node);
    res += RETHROW(t_yaml_pack_data(env, &kd->data));
    env->indent_lvl -= YAML_STD_INDENT;

  end:
    yaml_pack_env_pop_path(env, path_len);

    return res;
}

qm_kvec_t(key_to_data, lstr_t, const yaml_data_t * nullable, qhash_lstr_hash,
          qhash_lstr_equal);
qvector_t(elems, qm_t(key_to_data));
qvector_t(qv_kd, qv_t(yaml_key_data));

static void
t_merge_elems_to_data(qv_t(qv_kd) * nonnull objs, bool has_only_merge_key,
                      yaml_data_t * nonnull out)
{
    yaml_data_t merge_data;
    qv_t(yaml_key_data) *last_elem = NULL;

    assert (objs->len > 0);

    if (!has_only_merge_key && objs->len > 0) {
        last_elem = &objs->tab[objs->len - 1];
        objs->len--;
    }

    if (objs->len == 1) {
        t_yaml_data_new_obj2(&merge_data, &objs->tab[0]);
    } else {
        t_yaml_data_new_seq(&merge_data, objs->len);
        tab_for_each_ptr(kds, objs) {
            yaml_data_t data;

            t_yaml_data_new_obj2(&data, kds);
            yaml_seq_add_data(&merge_data, data);
        }
    }

    t_yaml_data_new_obj(out, last_elem ? last_elem->len + 1 : 1);
    yaml_obj_add_field(out, LSTR("<<"), merge_data);
    if (last_elem) {
        qv_extend(&out->obj->fields, last_elem);
    }
}

static void t_yaml_build_obj_with_merge_keys(
    const yaml_obj_t * nonnull obj,
    const yaml__presentation_merge_key__t * nonnull pres,
    yaml_data_t * nonnull out)
{
    qv_t(yaml_key_data) *fields = NULL;
    qm_t(key_to_data) ast_map;
    qv_t(elems) elems;
    qv_t(qv_kd) objs;
    bool has_only_merge_key = pres->has_only_merge_key;

    /* Build key => data for current AST. */
    t_qm_init(key_to_data, &ast_map, obj->fields.len);
    tab_for_each_ptr(kd, &obj->fields) {
        int pos;

        pos = qm_add(key_to_data, &ast_map, &kd->key, &kd->data);
        assert (pos >= 0);
    }

    /* Build map of key => data for every element. If original data is
     * missing, get it from the AST. */
    t_qv_init(&elems, pres->elements.len);
    tab_for_each_ptr(elem, &pres->elements) {
        qm_t(key_to_data) *map = qv_growlen(&elems, 1);

        t_qm_init(key_to_data, map, elem->keys.len);
        tab_for_each_ptr(elem_key, &elem->keys) {
            const yaml_data_t *elem_data = NULL;
            int ast_pos;
            int pos;

            /* make sure key still exists in AST. Otherwise, it means the ast
             * was modified since the presentation generation, so ignore this
             * key. */
            ast_pos = qm_find_safe(key_to_data, &ast_map, &elem_key->key);
            if (ast_pos < 0) {
                continue;
            }

            if (elem_key->original_data) {
                yaml_data_t *_elem_data = t_new(yaml_data_t, 1);

                t_iop_data_to_yaml(elem_key->original_data, _elem_data);
                elem_data = _elem_data;
            } else {
                SWAP(const yaml_data_t *, elem_data, ast_map.values[ast_pos]);
            }
            pos = qm_add(key_to_data, map, &elem_key->key, elem_data);
            assert (pos >= 0);
        }
    }

    /* Then, iterate on every element but the last one. For every field,
     * get the earliest value of this field by looking in the elements in
     * increasing order. Repeat.
     */
    t_qv_init(&objs, elems.len);
    for (int pos = 0; pos < elems.len; pos++) {
        const yaml__presentation_merge_key_elem__t *elem;
        qm_t(key_to_data) *elem_map = &elems.tab[pos];

        qm_for_each_key_value_p(key_to_data, key, value, elem_map) {
            for (int pos2 = pos + 1; pos2 < elems.len; pos2++) {
                qm_t(key_to_data) *map2 = &elems.tab[pos2];
                int map_pos;

                map_pos = qm_find_safe(key_to_data, map2, &key);
                if (map_pos >= 0) {
                    SWAP(const yaml_data_t * nullable, *value,
                         map2->values[map_pos]);
                    break;
                }
            }
        }

        /* Build an object with those values */
        elem = &pres->elements.tab[pos];
        fields = qv_growlen(&objs, 1);
        t_qv_init(fields, elem->keys.len);
        tab_for_each_ptr(key, &elem->keys) {
            const yaml_data_t * nullable val;

            val = qm_get_def(key_to_data, elem_map, &key->key, NULL);
            if (val) {
                yaml_key_data_t *kd = qv_growlen(fields, 1);

                p_clear(kd, 1);
                kd->key = key->key;
                kd->data = *val;
            }
        }
        if (fields->len == 0) {
            qv_remove_last(&objs);
            if (pos == elems.len - 1 && !has_only_merge_key) {
                has_only_merge_key = true;
            }
        }
    }

    /* If some values are left, it means the AST changed before repack. Add
     * it to the last element if inlined, or create a a new inlined one. */
    tab_for_each_ptr(obj_kd, &obj->fields) {
        const yaml_data_t *value;

        value = qm_get_def(key_to_data, &ast_map, &obj_kd->key, NULL);
        if (value) {
            yaml_key_data_t *kd;

            if (has_only_merge_key) {
                fields = qv_growlen(&objs, 1);
                t_qv_init(fields, 0);
                has_only_merge_key = false;
            } else {
                fields = &objs.tab[objs.len - 1];
            }

            kd = qv_growlen(fields, 1);
            p_clear(kd, 1);
            kd->key = obj_kd->key;
            kd->data = *value;
        }
    }

    t_merge_elems_to_data(&objs, has_only_merge_key, out);
}

static int yaml_pack_obj(yaml_pack_env_t * nonnull env,
                         const yaml_obj_t * nonnull obj,
                         const yaml__presentation_node__t * nullable pres)
{
    int res = 0;

    if (obj->fields.len == 0) {
        goto empty;
    } else
    if (unlikely(pres && pres->merge_key)) {
        yaml_data_t data;

        t_yaml_build_obj_with_merge_keys(obj, pres->merge_key, &data);

        t_yaml_pack_data_with_doc_pres(env, &data,
                                       pres->merge_key->presentation);
    } else {
        tab_for_each_ptr(pair, &obj->fields) {
            res += RETHROW(t_yaml_pack_key_data(env, pair));
        }

        if (res == 0) {
            /* XXX: This can happen if all keys come from an override: in
             * that case, pack an empty object. */
            goto empty;
        }
    }

    return res;

  empty:
    GOTO_STATE(CLEAN);
    PUTS("{}");
    env->state = PACK_STATE_AFTER_DATA;
    return res;
}

/* }}} */
/* {{{ Pack flow */

static int yaml_pack_flow_data(yaml_pack_env_t * nonnull env,
                               const yaml_data_t * nonnull data,
                               bool can_omit_brackets);

static int yaml_pack_flow_seq(yaml_pack_env_t * nonnull env,
                              const yaml_seq_t * nonnull seq)
{
    int res = 0;

    if (seq->datas.len == 0) {
        PUTS("[]");
        return res;
    }

    PUTS("[ ");
    tab_for_each_pos(pos, &seq->datas) {
        const yaml_data_t *data = &seq->datas.tab[pos];

        if (pos > 0) {
            PUTS(", ");
        }
        res += RETHROW(yaml_pack_flow_data(env, data, true));
    }
    PUTS(" ]");

    return res;
}

/* can_omit_brackets is used to prevent the packing of a single key object
 * inside an object:
 *   a: b: v
 * which is not valid.
 */
static int yaml_pack_flow_obj(yaml_pack_env_t * nonnull env,
                              const yaml_obj_t * nonnull obj,
                              bool can_omit_brackets)
{
    int res = 0;
    bool omit_brackets;

    if (obj->fields.len == 0) {
        PUTS("{}");
        return res;
    }

    omit_brackets = can_omit_brackets && obj->fields.len == 1;
    if (!omit_brackets) {
        PUTS("{ ");
    }
    tab_for_each_pos(pos, &obj->fields) {
        const yaml_key_data_t *kd = &obj->fields.tab[pos];

        if (pos > 0) {
            PUTS(", ");
        }
        PUTLSTR(kd->key);
        PUTS(": ");
        res += RETHROW(yaml_pack_flow_data(env, &kd->data, false));
    }
    if (!omit_brackets) {
        PUTS(" }");
    }

    return res;
}

static int yaml_pack_flow_data(yaml_pack_env_t * nonnull env,
                               const yaml_data_t * nonnull data,
                               bool can_omit_brackets)
{
    int res = 0;

    /* This is guaranteed by the yaml_data_can_use_flow_mode check. */
    assert (!data->tag.s);

    switch (data->type) {
      case YAML_DATA_SCALAR:
        res += RETHROW(yaml_pack_scalar(env, &data->scalar, LSTR_NULL_V,
                                        NULL));
        break;
      case YAML_DATA_SEQ:
        res += RETHROW(yaml_pack_flow_seq(env, data->seq));
        break;
      case YAML_DATA_OBJ:
        res += RETHROW(yaml_pack_flow_obj(env, data->obj, can_omit_brackets));
        break;
    }
    env->state = PACK_STATE_CLEAN;

    return res;
}

/* }}} */
/* {{{ Flow packable helpers */

static bool
yaml_env_path_contains_overrides(const yaml_pack_env_t * nonnull env)
{
    t_scope;
    lstr_t abspath;

    abspath = LSTR_SB_V(&env->absolute_path);
    tab_for_each_ptr(override, &env->overrides) {
        qm_for_each_key(override_nodes, key, &override->nodes) {
            if (lstr_startswith(key, abspath)) {
                return true;
            }
        }
    }

    return false;
}

static bool yaml_data_contains_tags(const yaml_data_t * nonnull data)
{
    if (data->tag.s) {
        return true;
    }

    switch (data->type) {
      case YAML_DATA_SCALAR:
        break;
      case YAML_DATA_SEQ:
        tab_for_each_ptr(elem, &data->seq->datas) {
            if (yaml_data_contains_tags(elem)) {
                return true;
            }
        }
        break;
      case YAML_DATA_OBJ:
        tab_for_each_ptr(kd, &data->obj->fields) {
            if (yaml_data_contains_tags(&kd->data)) {
                return true;
            }
        }
        break;
    }

    return false;
}

/** Make sure the data can be packed using flow mode.
 *
 * Flow mode is incompatible with the use of tags. If any data inside the
 * provided data has tags, flow mode cannot be used.
 */
static bool
yaml_env_data_can_use_flow_mode(const yaml_pack_env_t * nonnull env,
                                const yaml_data_t * nonnull data)
{
    /* If the flow data contains overrides, it cannot be packed into flow
     * mode. This isn't a hard limitation, but not implemented for the moment
     * because:
     *  * the use case seems limited
     *  * this would complicate the flow packing a lot, as it does not handle
     *    presentation.
     */
    if (yaml_env_path_contains_overrides(env)) {
        return false;
    }

    /* Recursing through the data to find out if it can be packed in a certain
     * way isn't ideal... This is acceptable here because flow data are
     * usually very small, and in the worst case, the whole data is in flow
     * mode, so we only go through the data twice.
     */
    if (yaml_data_contains_tags(data)) {
        return false;
    }

    return true;
}

/* }}} */
/* {{{ Pack override */

static void t_iop_pres_override_to_pack_override(
    const yaml_pack_env_t * nonnull env,
    const yaml__presentation_override__t * nonnull pres,
    yaml_pack_override_t *out)
{
    p_clear(out, 1);

    out->presentation = pres;
    t_qm_init(override_nodes, &out->nodes, pres->nodes.len);
    t_qv_init(&out->ordered_paths, pres->nodes.len);

    tab_for_each_ptr(node, &pres->nodes) {
        yaml_pack_override_node_t pack_node;
        yaml_data_t *data = NULL;
        lstr_t path;
        int res;

        if (node->original_data) {
            data = t_new(yaml_data_t, 1);
            t_iop_data_to_yaml(node->original_data, data);
        }
        p_clear(&pack_node, 1);
        pack_node.data = data;

        path = t_lstr_fmt("%pL%pL", &env->absolute_path, &node->path);
        res = qm_add(override_nodes, &out->nodes, &path, pack_node);
        assert (res >= 0);

        qv_append(&out->ordered_paths, path);
    }
}

static void
t_set_data_from_path(const yaml_data_t * nonnull data, pstream_t path,
                     bool new, yaml_data_t * nonnull out)
{
    if (ps_peekc(path) == '!' || ps_len(&path) == 0) {
        /* The ps_len == 0 can happen for added datas. The path ends with
         * [%d] or .%s, to mark the seq elem/obj key as the node being added.
         */
        *out = *data;
    } else
    if (ps_peekc(path) == '[') {
        yaml_data_t new_data;

        ps_skipc(&path, '.');
        /* We do not care about the index, because it is relative to the
         * overridden AST, not relevant here. Here, we only want to create
         * a sequence holding all our elements. */
        ps_skip_afterchr(&path, ']');

        if (new) {
            t_yaml_data_new_seq(out, 1);
        } else {
            /* This assert should not fail unless the presentation data was
             * malignly crafted. As it is created from a parsed AST, there
             * cannot be mixed data types through common path.
             * If this assert fails, it means the override contains paths
             * such as:
             *
             * .foo.bar
             * .foo[0]
             *
             * ie .foo is both an object and a sequence.
             */
            if (!expect(out->type == YAML_DATA_SEQ)) {
                return;
            }
        }

        t_set_data_from_path(data, path, true, &new_data);
        yaml_seq_add_data(out, new_data);
    } else
    if (ps_peekc(path) == '.') {
        yaml_data_t new_data;
        pstream_t ps_key;
        lstr_t key;

        ps_skipc(&path, '.');
        ps_key = ps_get_span(&path, &ctype_isalnum);
        key = LSTR_PS_V(&ps_key);

        if (new) {
            t_yaml_data_new_obj(out, 1);
        } else {
            /* see related expect in the seq case */
            if (!expect(out->type == YAML_DATA_OBJ)) {
                return;
            }

            tab_for_each_ptr(kd, &out->obj->fields) {
                if (lstr_equal(kd->key, key)) {
                    t_set_data_from_path(data, path, false, &kd->data);
                    return;
                }
            }
        }

        t_set_data_from_path(data, path, true, &new_data);
        yaml_obj_add_field(out, key, new_data);
    }
}

static void
t_build_override_data(const yaml_pack_override_t * nonnull override,
                      yaml_data_t * nonnull out)
{
    /* Iterate on the ordered paths, to make sure the data is recreated
     * in the right order. */
    assert (override->ordered_paths.len == override->presentation->nodes.len);
    tab_for_each_pos(pos, &override->ordered_paths) {
        const yaml_pack_override_node_t *node;
        pstream_t ps;

        node = qm_get_p_safe(override_nodes, &override->nodes,
                             &override->ordered_paths.tab[pos]);

        if (unlikely(!node->found)) {
            /* This can happen if an overrided node is no longer present
             * in the AST. In that case, ignore it.  */
            continue;
        }
        assert (node->data);

        /* Use the relative path here, to properly reconstruct the data. */
        ps = ps_initlstr(&override->presentation->nodes.tab[pos].path);
        t_set_data_from_path(node->data, ps, false, out);
    }
}

/* }}} */
/* {{{ Pack include */
/* {{{ Subfile sharing handling */

enum subfile_status_t {
    SUBFILE_TO_CREATE,
    SUBFILE_TO_REUSE,
    SUBFILE_TO_IGNORE,
};

/* check if data can be packed in the subfile given from its relative path
 * from the env outdir */
static enum subfile_status_t
check_subfile(yaml_pack_env_t * nonnull env, uint64_t checksum,
              const char * nonnull relative_path)
{
    char fullpath[PATH_MAX];
    lstr_t path;
    int pos;

    /* compute new outdir */
    path_extend(fullpath, env->outdirpath.s, "%s", relative_path);
    path = LSTR(fullpath);

    assert (env->subfiles);
    pos = qm_put(path_to_checksum, env->subfiles, &path, checksum, 0);
    if (pos & QHASH_COLLISION) {
        pos &= ~QHASH_COLLISION;
        if (env->subfiles->values[pos] == checksum) {
            return SUBFILE_TO_REUSE;
        } else {
            return SUBFILE_TO_IGNORE;
        }
    } else {
        env->subfiles->keys[pos] = t_lstr_dup(path);
        return SUBFILE_TO_CREATE;
    }
}

static const char * nullable
t_find_right_path(yaml_pack_env_t * nonnull env, sb_t * nonnull contents,
                  lstr_t initial_path, bool * nonnull reuse)
{
    const char *ext;
    char *path;
    lstr_t base;
    int counter = 1;
    uint64_t checksum;

    /* TODO: it would be more efficient to compute the checksum as the
     * contents buffer is filled. */
    /* TODO: use full 256bits hash to prevent collision? */
    checksum = sha2_hash_64(contents->data, contents->len);

    path = t_fmt("%pL", &initial_path);
    path_simplify(path);

    ext = path_ext(path);
    base = ext ? LSTR_PTR_V(path, ext) : LSTR(path);

    /* check base.ext, base~1.ext, etc until either the file does not exist,
     * or the data to pack is identical to the data packed in the subfile. */
    for (;;) {
        switch (check_subfile(env, checksum, path)) {
          case SUBFILE_TO_CREATE:
            *reuse = false;
            return path;

          case SUBFILE_TO_REUSE:
            logger_trace(&_G.logger, 2, "subfile `%s` reused", path);
            *reuse = true;
            return path;

          case SUBFILE_TO_IGNORE:
            logger_trace(&_G.logger, 2,
                         "should have reused subfile `%s`, but the packed "
                         "data is different", path);
            break;
        }

        if (ext) {
            path = t_fmt("%pL~%d%s", &base, counter++, ext);
        } else {
            path = t_fmt("%pL~%d", &base, counter++);
        }
    }
}

/* }}} */
/* {{{ Include node packing */

/* Generation presentation for include data to hide the '~', and generate:
 *  !include:path
 * instead of:
 *  !include:path ~
 */
static yaml__document_presentation__t * nonnull
t_gen_default_include_presentation(void)
{
    yaml__presentation_node_mapping__t *mapping;
    yaml__document_presentation__t *pres;

    mapping = t_iop_new(yaml__presentation_node_mapping);
    mapping->path = LSTR("!");
    mapping->node.empty_null = true;

    pres = t_iop_new(yaml__document_presentation);
    pres->mappings = IOP_TYPED_ARRAY(yaml__presentation_node_mapping,
                                     mapping, 1);

    return pres;
}

/* Pack the "!include(raw)?:<path>" node, with the right presentation. */
static int
t_yaml_pack_include_path(yaml_pack_env_t * nonnull env,
                         const yaml__document_presentation__t * nonnull dpres,
                         bool raw, lstr_t include_path,
                         yaml_data_t * nonnull data)
{
    if (raw) {
        data->tag = t_lstr_fmt("includeraw:%pL", &include_path);
    } else {
        data->tag = t_lstr_fmt("include:%pL", &include_path);
    }

    if (dpres->mappings.len == 0) {
        dpres = t_gen_default_include_presentation();
    }

    return t_yaml_pack_data_with_doc_pres(env, data, dpres);
}

/* write raw contents directly into the given filepath. */
static int
yaml_pack_write_raw_file(const yaml_pack_env_t * nonnull env,
                         const char * nonnull filepath,
                         const lstr_t contents, sb_t * nonnull err)
{
    t_scope;
    const char *fullpath;
    char fulldirpath[PATH_MAX];
    file_t *file;

    fullpath = t_fmt("%pL/%s", &env->outdirpath, filepath);

    path_dirname(fulldirpath, PATH_MAX, fullpath);
    if (mkdir_p(fulldirpath, 0755) < 0) {
        sb_sets(err, "could not create output directory: %m");
        return -1;
    }

    file = file_open(fullpath, env->file_flags, env->file_mode);
    if (!file) {
        sb_setf(err, "cannot open output file `%s`: %m", fullpath);
        return -1;
    }

    if (file_write(file, contents.s, contents.len) < 0) {
        sb_setf(err, "cannot write in output file: %m");
        return -1;
    }

    IGNORE(file_close(&file));
    return 0;
}

/* }}} */
/* {{{ Pack subfile */

static int write_nothing(void *b, const void *buf, int len, sb_t *err)
{
    return len;
}

static int
t_yaml_pack_subfile_in_sb(yaml_pack_env_t * nonnull env,
                          const yaml__presentation_include__t * nonnull inc,
                          const yaml_data_t * nonnull data, bool no_subfiles,
                          sb_t * nonnull out, sb_t * nonnull err)
{
    yaml_pack_env_t *subenv = t_yaml_pack_env_new();

    if (!no_subfiles) {
        const char *fullpath;
        char dirpath[PATH_MAX];

        fullpath = t_fmt("%pL/%pL", &env->outdirpath, &inc->path);
        path_dirname(dirpath, PATH_MAX, fullpath);

        RETHROW(t_yaml_pack_env_set_outdir(subenv, dirpath, err));
    }

    t_yaml_pack_env_set_presentation(subenv, &inc->document_presentation);

    sb_setsb(&subenv->absolute_path, &env->absolute_path);
    subenv->current_path_pos = subenv->absolute_path.len;
    yaml_pack_env_set_flags(subenv, env->flags);

    subenv->overrides = env->overrides;
    subenv->active_vars = env->active_vars;

    /* Make sure the subfiles qm is shared, so that if this subfile
     * also generate other subfiles, it is properly handled. */
    subenv->subfiles = env->subfiles;

    if (no_subfiles) {
        /* Go through the AST as if the file was packed, but do not actually
         * write anything. This allows properly recreating the overrides. */
        RETHROW(t_yaml_pack(subenv, data, &write_nothing, NULL, err));
    } else {
        RETHROW(t_yaml_pack_sb(subenv, data, out, err));

        /* always ends with a newline when packing for a file */
        if (out->len > 0 && out->data[out->len - 1] != '\n') {
            sb_addc(out, '\n');
        }
    }

    return 0;
}

static bool
yaml_data_can_be_packed_raw(const yaml_data_t * nonnull data)
{
    if (data->type != YAML_DATA_SCALAR) {
        return false;
    }

    switch (data->scalar.type) {
      case YAML_SCALAR_STRING:
      case YAML_SCALAR_BYTES:
        return true;

      default:
        return false;
    }
}

static int
t_yaml_pack_included_subfile(
    yaml_pack_env_t * nonnull env,
    const yaml__presentation_include__t * nonnull inc,
    const yaml_data_t * nonnull subdata,
    bool * nonnull raw, const char * nonnull * nonnull path)
{
    bool reuse;
    bool no_subfiles = env->flags & YAML_PACK_NO_SUBFILES;
    int res = 0;
    SB_1k(contents);
    SB_1k(err);

    if (!env->subfiles) {
        env->subfiles = t_qm_new(path_to_checksum, 0);
    }

    /* if the YAML data to dump is not a string, it changed and can no longer
     * be packed raw. */
    *raw = inc->raw;
    if (*raw) {
        *raw = yaml_data_can_be_packed_raw(subdata);
    }

    if (*raw) {
        sb_set_lstr(&contents, subdata->scalar.s);
    } else {
        /* Pack the subdata, but in a sb, not in the subfile directly. As the
         * subfile can be shared by multiple includes, we need to ensure
         * the contents are the same to share the same filename, or use
         * another one.
         *
         * Additionally, as packing the subfiles might have side-effects on
         * the current env (mainly, overrides packed in this env depends on
         * the packing of the subdata), it is not possible to do some sort
         * of AST comparison to detect shared subfiles.
         */
        if (t_yaml_pack_subfile_in_sb(env, inc, subdata, no_subfiles,
                                      &contents, &err) < 0)
        {
            sb_setf(&env->err, "cannot pack subfile `%pL`: %pL", &inc->path,
                    &err);
            return -1;
        }
    }

    *path = t_find_right_path(env, &contents, inc->path, &reuse);
    if (!reuse) {
        logger_trace(&_G.logger, 2, "writing %ssubfile %s", raw ? "raw " : "",
                     *path);
        if (likely(!no_subfiles)
        &&  yaml_pack_write_raw_file(env, *path, LSTR_SB_V(&contents),
                                     &err) < 0)
        {
            sb_setf(&env->err, "error when writing subfile `%s`: %pL",
                    *path, &err);
            return -1;
        }
    }

    return res;
}

static void
t_build_variable_settings(
    const yaml__presentation_variable_settings__t *var_pres,
    qm_t(active_vars) * nonnull vars, yaml_data_t * nonnull out)
{
    yaml_data_t data;

    assert (var_pres->bindings.len == qm_len(active_vars, vars));
    t_yaml_data_new_obj(&data, var_pres->bindings.len);

    /* Iterate on the array and not the qm, to recreate the variable settings
     * in the right order. */
    tab_for_each_ptr(binding, &var_pres->bindings) {
        const yaml_pack_variable_t *var;

        var = qm_get_p(active_vars, vars, &binding->var_name);
        while (var) {
            if (var->deduced_value) {
                yaml_obj_add_field(&data, var->name, *var->deduced_value);
            }
            var = var->conflict;
        }
    }

    if (data.obj->fields.len > 0) {
        yaml_obj_add_field(out, LSTR("variables"), data);
    }
}

static int
t_yaml_pack_include_with_override(
    yaml_pack_env_t * nonnull env,
    const yaml__presentation_include__t * nonnull inc,
    const yaml_data_t * nonnull subdata)
{
    yaml_pack_override_t *override = NULL;
    qm_t(active_vars) vars;
    bool raw;
    const char *path;
    yaml_data_t settings;

    /* add current override if it exists, so that it is used when the subdata
     * is packed. */
    if (inc->override) {
        override = qv_growlen0(&env->overrides, 1);
        t_iop_pres_override_to_pack_override(env, inc->override,
                                             override);
    }
    if (inc->variables) {
        t_qm_init(active_vars, &vars, inc->variables->bindings.len);

        tab_for_each_ptr(binding, &inc->variables->bindings) {
            yaml_pack_variable_t var = {
                .name = binding->var_name,
                .original_value = binding->value,
            };

            qm_add(active_vars, &vars, &binding->var_name, var);
        }
        qv_append(&env->active_vars, vars);
    }

    /* Pack the contents of the subfile. Retrieve whether it is a raw include
     * or not, and the exact path (after collision resolution).
     */
    RETHROW(t_yaml_pack_included_subfile(env, inc, subdata, &raw, &path));

    /* Create an object from possible variables and overrides */
    if (inc->variables || override) {

        t_yaml_data_new_obj(&settings,
                            override ? override->ordered_paths.len + 1 : 1);

        if (inc->variables) {
            t_build_variable_settings(inc->variables, &vars, &settings);
            qv_remove_last(&env->active_vars);
        }

        if (override) {
            qv_remove_last(&env->overrides);
            t_build_override_data(override, &settings);
        }
    } else {
        yaml_data_set_null(&settings);
    }

    return t_yaml_pack_include_path(env, &inc->include_presentation, raw,
                                    LSTR(path), &settings);
}

/* }}} */

static int
t_yaml_pack_included_data(yaml_pack_env_t * nonnull env,
                          const yaml_data_t * nonnull data,
                          const yaml__presentation_node__t * nonnull node)
{
    const yaml__presentation_include__t *inc;

    inc = node->included;
    /* Write include node & override if:
     *  * an outdir is set
     *  * NO_SUBFILES is set, meaning we are recreating the file as is.
     */
    if (env->outdirpath.len > 0 || env->flags & YAML_PACK_NO_SUBFILES) {
        return t_yaml_pack_include_with_override(env, inc, data);
    } else {
        const yaml_presentation_t *saved_pres = env->pres;
        unsigned current_path_pos = env->absolute_path.len;
        int res;

        /* Inline the contents of the included data directly in the current
         * stream. This is as easy as just packing data, but we need to also
         * use the presentation data from the included files. To do so, the
         * current_path must be reset. */
        SWAP(unsigned, current_path_pos, env->current_path_pos);
        env->pres = t_yaml_doc_pres_to_map(&inc->document_presentation);

        res = t_yaml_pack_data(env, data);

        env->pres = saved_pres;
        SWAP(unsigned, current_path_pos, env->current_path_pos);

        return res;
    }
}

/* }}} */
/* {{{ Variables */

static yaml_pack_variable_t * nullable
t_yaml_env_find_var(yaml_pack_env_t * nonnull env, lstr_t var_name)
{
    tab_for_each_pos_rev(var_pos, &env->active_vars) {
        const qm_t(active_vars) *vars = &env->active_vars.tab[var_pos];
        yaml_pack_variable_t *var;

        var = qm_get_def_p_safe(active_vars, vars, &var_name, NULL);
        if (!var) {
            continue;
        }

        return var;
    }

    return NULL;
}

static yaml_pack_variable_t * nullable
t_find_var(yaml_pack_env_t * nonnull env, lstr_t name)
{
    tab_for_each_pos_rev(var_pos, &env->active_vars) {
        const qm_t(active_vars) *vars = &env->active_vars.tab[var_pos];
        yaml_pack_variable_t *var;

        var = qm_get_def_p_safe(active_vars, vars, &name, NULL);
        if (var) {
            return var;
        }
    }

    return NULL;
}

static void
t_resolve_var_conflict(yaml_pack_env_t * nonnull env,
                       yaml_pack_variable_t * nonnull var,
                       const yaml_data_t * nonnull data,
                       lstr_t * nonnull new_name)
{
    lstr_t orig_var_name = var->name;
    yaml_pack_variable_t *next_var = var->conflict;
    unsigned cnt = 1;

    /* try to match to existant conflict resolution, or get to the end
     * of the chain */
    while (next_var) {
        if (yaml_data_equals(next_var->deduced_value, data, false)) {
            *new_name = next_var->name;
            return;
        }
        var = next_var;
        next_var = next_var->conflict;
        cnt++;
    }

    for (;;) {
        lstr_t var_name = t_lstr_fmt("%pL~%d", &orig_var_name, cnt++);

        if (t_find_var(env, var_name)) {
            /* This name actually exists in a surrounding env, do not use it.
             */
            continue;
        }
        var->conflict = t_new(yaml_pack_variable_t, 1);
        var->conflict->name = var_name;
        var->conflict->deduced_value = data;
        *new_name = var_name;
        break;
    }
}

static int
t_apply_variable_value(yaml_pack_env_t * nonnull env, lstr_t var_name,
                       const yaml_data_t * nonnull data,
                       lstr_t * nonnull new_name)
{
    yaml_pack_variable_t *var;

    var = t_find_var(env, var_name);
    if (!var) {
        return -1;
    }

    if (var->deduced_value) {
        if (!yaml_data_equals(var->deduced_value, data, false)) {
            t_resolve_var_conflict(env, var, data, new_name);
            return 0;
        }
    } else {
        var->deduced_value = data;
    }

    logger_trace(&_G.logger, 2, "deduced value for variable `%pL` "
                 "to %s", &var_name, yaml_data_get_type(data, false));
    *new_name = LSTR_NULL_V;
    return 0;
}

static lstr_t t_yaml_format_variable(lstr_t name)
{
    return t_lstr_fmt("$(%pL)", &name);
}

/* Apply the original values of the variables to the template.
 * If the result is equal to ast_value, the variables did not change (*), and
 * their original values can thus be used.
 *
 * (*) Theorically, the variables can change while the template result stays
 * the same, through some symmetric modifications. This is quite out of the
 * scope we want to handle.
 */
static int
t_apply_original_var_values(yaml_pack_env_t * nonnull env,
                            const lstr_t ast_value,
                            qv_t(u8) * nonnull var_bitmap,
                            lstr_t * nonnull tpl)
{
    t_SB_1k(buf);
    pstream_t ps;
    pstream_t sub;
    qv_t(pack_var) matched_vars;
    int var_pos = 0;

    ps = ps_initlstr(tpl);
    t_qv_init(&matched_vars, 1);

    for (;;) {
        lstr_t name;
        yaml_pack_variable_t *var;
        pstream_t cpy;

        /* copy up to next '$' */
        if (ps_get_ps_chr(&ps, '$', &sub) < 0) {
            /* no next '$', copy everything and stop */
            sb_add_ps(&buf, ps);
            break;
        }
        sb_add_ps(&buf, sub);
        if (!ps_has(&ps, 2) || ps.s[1] != '(') {
            ps_skip(&ps, 1);
            sb_addc(&buf, '$');
            continue;
        }

        var_pos += 1;
        if (!var_bitmap_test_bit(var_bitmap, var_pos - 1)) {
            sb_addc(&buf, ps_getc(&ps));
            continue;
        }

        cpy = ps;
        ps_skip(&ps, 2);
        name = ps_parse_variable_name(&ps);
        if (!name.s) {
            return -1;
        }
        var = t_yaml_env_find_var(env, name);
        if (!var || !var->original_value.s) {
            if (env->flags & YAML_PACK_ALLOW_UNBOUND_VARIABLES) {
                sb_add(&buf, cpy.p, ps.s - cpy.s);
                continue;
            }
            return -1;
        }

        sb_add_lstr(&buf, var->original_value);
        qv_append(&matched_vars, var);
    }

    if (!lstr_equal(ast_value, LSTR_SB_V(&buf))) {
        return -1;
    }

    /* The strings matches. Apply the original value as the deduced value for
     * all used variables */
    tab_for_each_entry(var, &matched_vars) {
        yaml_data_t *data;
        lstr_t new_name;

        assert (var->original_value.s);
        data = t_new(yaml_data_t, 1);
        yaml_data_set_string(data, var->original_value);

        RETHROW(t_apply_variable_value(env, var->name, data, &new_name));
        if (new_name.s) {
            /* Even though the string did not change, the value has
             * conflicted, and a new name must used. Replace in the template
             * the old name with a variable using the new name. */
            *tpl = t_tpl_set_variable(*tpl, var->name,
                                      t_yaml_format_variable(new_name),
                                      var_bitmap);
        }
    }

    logger_trace(&_G.logger, 2, "template `%pL` did not change: re-use same "
                 "values for variables used", tpl);

    return 0;
}

/* Deduce value of a variable inside a string. The template can be:
 *
 * - a raw variable: a: $(var)
 * - a templated string with a single variable: a: "a_$(var)_b"
 * - a templated string with multiple variables: a: "$(var)_$(foo)"
 *
 * This function handles the first two cases. The last one is not attempted.
 * Even if it can be done in some cases, there are cases that can only be
 * resolved through complex regex-style matching, and there are ambiguous
 * cases, such as:
 *  * template: "$(foo)_$(bar)"
 *  * value: "a_b_c_d_e"
 */
static int
deduce_var_in_string(lstr_t tpl, lstr_t value,
                     const qv_t(u8) * nonnull bitmap,
                     lstr_t * nonnull var_name, lstr_t * nonnull var_value)
{
    pstream_t tpl_ps = ps_initlstr(&tpl);
    pstream_t val_ps = ps_initlstr(&value);
    lstr_t name;
    int var_pos = 0;

    /* advance both streams until the variable or a mismatch is found */
    while (!ps_done(&tpl_ps)) {
        int c = ps_getc(&tpl_ps);

        if (c == '$' && ps_peekc(tpl_ps) == '(') {
            if (bitmap->len == 0 || (var_pos < bitmap->len * 8
                                  && TST_BIT(bitmap->tab, var_pos)))
            {
                /* var found */
                ps_skip(&tpl_ps, 1);
                break;
            }
            var_pos++;
        }

        if (ps_done(&val_ps)) {
            return -1;
        } else
        if (c != ps_getc(&val_ps)) {
            return -1;
        }
    }

    /* capture name of variable */
    name = ps_parse_variable_name(&tpl_ps);
    if (!name.s) {
        return -1;
    }

    /* check if the rest of the template after the variable matches
     * the value. If not, it means either the value changed, or there are
     * multiple variables. */
    value = LSTR_PS_V(&val_ps);
    if (!lstr_endswith(value, LSTR_PS_V(&tpl_ps))) {
        return -1;
    }
    value.len -= ps_len(&tpl_ps);

    *var_name = name;
    *var_value = value;
    return 0;
}

/* Deduce values for active variables, by comparing the original string
 * containing variables, with the value of the AST.
 *
 * If all variables cannot be deduced, -1 is returned, and the original string
 * with variables is not used.
 */
static int
t_deduce_variable_values(yaml_pack_env_t * nonnull env,
                         const yaml_data_t * nonnull data,
                         const u8__array_t * nonnull variables_bitmap,
                         lstr_t * nonnull var_string)
{
    lstr_t new_name;
    qv_t(u8) var_bitmap;

    t_qv_init(&var_bitmap, 0);
    qv_extend(&var_bitmap, variables_bitmap);

    if (data->type == YAML_DATA_SCALAR
    &&  data->scalar.type == YAML_SCALAR_STRING)
    {
        lstr_t var_name;
        lstr_t var_value;
        yaml_data_t *var_data;

        /* First, use the original values of variables to see if the value is
         * still the same. This allows repacking in the same way if the value
         * did not change, without trying to deduce changes that are very
         * rare. */
        if (t_apply_original_var_values(env, data->scalar.s, &var_bitmap,
                                        var_string) >= 0)
        {
            return 0;
        }

        /* Otherwise, try to deduce variable values, but the implementation is
         * limited. */
        if (deduce_var_in_string(*var_string, data->scalar.s, &var_bitmap,
                                 &var_name, &var_value) < 0)
        {
            return -1;
        }
        var_data = t_new(yaml_data_t, 1);
        yaml_data_set_string(var_data, var_value);

        RETHROW(t_apply_variable_value(env, var_name, var_data, &new_name));
        if (new_name.s) {
            /* The value has conflicted, and a new name must used. Replace
             * the old name with a variable using the new name */
            new_name = t_yaml_format_variable(new_name);
            *var_string = t_tpl_set_variable(*var_string, var_name, new_name,
                                             &var_bitmap);
        }
    } else {
        /* If data is not a string, it should be matched on a template
         * containing only the variable, ie "$(name)". */
        pstream_t tpl_ps = ps_initlstr(var_string);
        lstr_t name;

        if (ps_skipc(&tpl_ps, '$') < 0 || ps_skipc(&tpl_ps, '(') < 0) {
            return -1;
        }
        name = ps_parse_variable_name(&tpl_ps);
        if (!name.s || !ps_done(&tpl_ps)) {
            return -1;
        }

        RETHROW(t_apply_variable_value(env, name, data, &new_name));
        if (new_name.s) {
            *var_string = t_yaml_format_variable(new_name);
        }
    }

    return 0;
}

/* }}} */
/* {{{ Pack data */

static int t_yaml_pack_data(yaml_pack_env_t * nonnull env,
                            const yaml_data_t * nonnull data)
{
    const yaml__presentation_node__t *node;
    yaml_pack_override_node_t *override = NULL;
    int res = 0;

    if (env->pres) {
        int path_len = yaml_pack_env_push_path(env, "!");

        node = yaml_pack_env_get_pres_node(env);
        override = yaml_pack_env_find_override(env);
        /* This should only be a replace, as additions can only be done
         * on keys or seq indicators. So *override should be not NULL, but
         * as a user can write its own presentation data, we cannot assert
         * it. */
        if (override && likely(override->data)) {
            logger_trace(&_G.logger, 2,
                         "packing non-overriden data in path `%*pM`",
                         LSTR_FMT_ARG(yaml_pack_env_get_curpath(env)));
            SWAP(const yaml_data_t *, data, override->data);
            override->found = true;
        }
        yaml_pack_env_pop_path(env, path_len);
    } else {
        node = data->presentation;
    }

    /* If the node was included from another file, and we are packing files,
     * dump it in a new file. */
    if (unlikely(node && node->included)) {
        return t_yaml_pack_included_data(env, data, node);
    }

    if (node) {
        res += yaml_pack_pres_node_prefix(env, node);

        if (node->tpl) {
            lstr_t tpl = node->tpl->original_value;

            if (t_deduce_variable_values(env, data,
                                         &node->tpl->variables_bitmap,
                                         &tpl) >= 0)
            {
                yaml_data_t *new_data = t_new(yaml_data_t, 1);

                yaml_data_set_string(new_data, tpl);
                data = new_data;
            } else {
                logger_trace(&_G.logger, 2, "change to template `%pL` "
                             "not handled: template is lost",
                             &node->tpl->original_value);
                /* presentation applies to the template. If losing the
                 * template, lose the presentation. */
                node = NULL;
            }
        }
    }

    if (unlikely(data->type == YAML_DATA_SCALAR
              && data->scalar.type == YAML_SCALAR_BYTES))
    {
        /* TODO: a bytes scalar should not have a set tag, to check */
        res += yaml_pack_tag(env, LSTR("bin"));
    } else {
        res += yaml_pack_tag(env, data->tag);
    }

    if (node && node->flow_mode && yaml_env_data_can_use_flow_mode(env, data))
    {
        GOTO_STATE(CLEAN);
        res += yaml_pack_flow_data(env, data, false);
        env->state = PACK_STATE_AFTER_DATA;
    } else {
        switch (data->type) {
          case YAML_DATA_SCALAR: {
            res += RETHROW(yaml_pack_scalar(env, &data->scalar, data->tag,
                                            node));
          } break;
          case YAML_DATA_SEQ:
            res += RETHROW(t_yaml_pack_seq(env, data->seq));
            break;
          case YAML_DATA_OBJ:
            res += RETHROW(yaml_pack_obj(env, data->obj, node));
            break;
        }
    }

    if (node) {
        res += yaml_pack_pres_node_inline(env, node);
    }

    return res;
}

#undef WRITE
#undef PUTS
#undef PUTLSTR
#undef INDENT

/* }}} */
/* }}} */
/* {{{ Pack env public API */

/** Initialize a new YAML packing context. */
yaml_pack_env_t * nonnull t_yaml_pack_env_new(void)
{
    yaml_pack_env_t *env = t_new(yaml_pack_env_t, 1);

    env->state = PACK_STATE_ON_NEWLINE;
    env->file_flags = FILE_WRONLY | FILE_CREATE | FILE_TRUNC;
    env->file_mode = 0644;
    env->outdirpath = LSTR_EMPTY_V;

    t_sb_init(&env->absolute_path, 1024);
    t_sb_init(&env->err, 1024);
    t_qv_init(&env->overrides, 0);
    t_qv_init(&env->active_vars, 0);

    return env;
}

void yaml_pack_env_set_flags(yaml_pack_env_t * nonnull env, unsigned flags)
{
    env->flags = flags;
}

int t_yaml_pack_env_set_outdir(yaml_pack_env_t * nonnull env,
                               const char * nonnull dirpath,
                               sb_t * nonnull err)
{
    char canonical_path[PATH_MAX];

    if (mkdir_p(dirpath, 0755) < 0) {
        sb_sets(err, "could not create output directory: %m");
        return -1;
    }

    /* Should not fail because any errors should have been caught by
     * mkdir_p first. */
    if (!expect(path_canonify(canonical_path, PATH_MAX, dirpath) >= 0)) {
        sb_setf(err, "cannot compute path to output directory `%s`: %m",
                dirpath);
        return -1;
    }

    env->outdirpath = t_lstr_dup(LSTR(canonical_path));

    return 0;
}

void yaml_pack_env_set_file_mode(yaml_pack_env_t * nonnull env, mode_t mode)
{
    env->file_mode = mode;
}

void t_yaml_pack_env_set_presentation(
    yaml_pack_env_t * nonnull env,
    const yaml__document_presentation__t * nonnull pres
)
{
    env->pres = t_yaml_doc_pres_to_map(pres);
}

int t_yaml_pack(yaml_pack_env_t * nonnull env,
                const yaml_data_t * nonnull data,
                yaml_pack_writecb_f * nonnull writecb, void * nullable priv,
                sb_t * nullable err)
{
    int res;

    env->write_cb = writecb;
    env->priv = priv;

    res = t_yaml_pack_data(env, data);
    if (res < 0 && err) {
        sb_setsb(err, &env->err);
    }

    return res;
}

static inline int sb_write(void * nonnull b, const void * nonnull buf,
                           int len, sb_t * nonnull err)
{
    sb_add(b, buf, len);
    return len;
}

int t_yaml_pack_sb(yaml_pack_env_t * nonnull env,
                   const yaml_data_t * nonnull data, sb_t * nonnull sb,
                   sb_t * nullable err)
{
    return t_yaml_pack(env, data, &sb_write, sb, err);
}

typedef struct yaml_pack_file_ctx_t {
    file_t *file;
} yaml_pack_file_ctx_t;

static int iop_ypack_write_file(void *priv, const void *data, int len,
                                sb_t *err)
{
    yaml_pack_file_ctx_t *ctx = priv;

    if (file_write(ctx->file, data, len) < 0) {
        sb_setf(err, "cannot write in output file: %m");
        return -1;
    }

    return len;
}

int
t_yaml_pack_file(yaml_pack_env_t * nonnull env, const char * nonnull filename,
                 const yaml_data_t * nonnull data, sb_t * nonnull err)
{
    char path[PATH_MAX];
    yaml_pack_file_ctx_t ctx;
    int res;

    if (env->outdirpath.len > 0) {
        filename = t_fmt("%pL/%s", &env->outdirpath, filename);
    }

    /* Make sure the outdirpath is the full dirpath, even if it was set
     * before. */
    path_dirname(path, PATH_MAX, filename);
    RETHROW(t_yaml_pack_env_set_outdir(env, path, err));

    p_clear(&ctx, 1);
    ctx.file = file_open(filename, env->file_flags, env->file_mode);
    if (!ctx.file) {
        sb_setf(err, "cannot open output file `%s`: %m", filename);
        return -1;
    }

    res = t_yaml_pack(env, data, &iop_ypack_write_file, &ctx, err);
    if (res < 0) {
        IGNORE(file_close(&ctx.file));
        return res;
    }

    /* End the file with a newline, as the packing ends immediately after
     * the last value. */
    if (env->state != PACK_STATE_ON_NEWLINE) {
        file_puts(ctx.file, "\n");
    }

    if (file_close(&ctx.file) < 0) {
        sb_setf(err, "cannot close output file `%s`: %m", filename);
        return -1;
    }

    return 0;
}

/* }}} */
/* {{{ AST helpers */

#define SET_SCALAR(data, scalar_type)                                        \
    do {                                                                     \
        p_clear(data, 1);                                                    \
        data->type = YAML_DATA_SCALAR;                                       \
        data->scalar.type = YAML_SCALAR_##scalar_type;                       \
    } while (0)

void yaml_data_set_string(yaml_data_t *data, lstr_t str)
{
    SET_SCALAR(data, STRING);
    data->scalar.s = str;
}

void yaml_data_set_double(yaml_data_t *data, double d)
{
    SET_SCALAR(data, DOUBLE);
    data->scalar.d = d;
}

void yaml_data_set_uint(yaml_data_t *data, uint64_t u)
{
    SET_SCALAR(data, UINT);
    data->scalar.u = u;
}

void yaml_data_set_int(yaml_data_t *data, int64_t i)
{
    SET_SCALAR(data, INT);
    data->scalar.i = i;
}

void yaml_data_set_bool(yaml_data_t *data, bool b)
{
    SET_SCALAR(data, BOOL);
    data->scalar.b = b;
}

void yaml_data_set_null(yaml_data_t *data)
{
    SET_SCALAR(data, NULL);
}

void yaml_data_set_bytes(yaml_data_t *data, lstr_t bytes)
{
    SET_SCALAR(data, BYTES);
    data->scalar.s = bytes;
}

void t_yaml_data_new_seq(yaml_data_t *data, int capacity)
{
    p_clear(data, 1);
    data->type = YAML_DATA_SEQ;
    data->seq = t_new(yaml_seq_t, 1);
    t_qv_init(&data->seq->datas, capacity);
}

void yaml_seq_add_data(yaml_data_t *data, yaml_data_t val)
{
    assert (data->type == YAML_DATA_SEQ);
    qv_append(&data->seq->datas, val);
}

void t_yaml_data_new_obj(yaml_data_t *data, int capacity)
{
    p_clear(data, 1);
    data->type = YAML_DATA_OBJ;
    data->obj = t_new(yaml_obj_t, 1);
    t_qv_init(&data->obj->fields, capacity);
}

void t_yaml_data_new_obj2(yaml_data_t *data, qv_t(yaml_key_data) *fields)
{
    p_clear(data, 1);
    data->type = YAML_DATA_OBJ;
    data->obj = t_new(yaml_obj_t, 1);
    data->obj->fields = *fields;
}

void yaml_obj_add_field(yaml_data_t *data, lstr_t key, yaml_data_t val)
{
    yaml_key_data_t *kd;

    assert (data->type == YAML_DATA_OBJ);
    kd = qv_growlen0(&data->obj->fields, 1);
    kd->key = key;
    kd->data = val;
}

/* }}} */
/* {{{ Module */

static int yaml_initialize(void *arg)
{
    return 0;
}

static int yaml_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(yaml)
    /* There is an implicit dependency on "log" */
MODULE_END()

/* }}} */
/* {{{ Tests */

/* LCOV_EXCL_START */

#include <lib-common/z.h>

/* {{{ Helpers */

static int z_yaml_test_parse_fail(unsigned flags,
                                  const char *yaml, const char *expected_err)
{
    t_scope;
    yaml_data_t data;
    yaml_parse_t *env = t_yaml_parse_new(flags);
    SB_1k(err);

    yaml_parse_attach_ps(env, ps_initstr(yaml));
    Z_ASSERT_NEG(t_yaml_parse(env, &data, &err));
    Z_ASSERT_STREQUAL(err.data, expected_err,
                      "wrong error message on yaml string `%s`", yaml);
    yaml_parse_delete(&env);

    Z_HELPER_END;
}

static int
z_create_tmp_subdir(const char *dirpath)
{
    t_scope;
    const char *path;

    path = t_fmt("%pL/%s", &z_tmpdir_g, dirpath);
    Z_ASSERT_N(mkdir_p(path, 0777));

    Z_HELPER_END;
}

static int
z_write_yaml_file(const char *filepath, const char *yaml)
{
    t_scope;
    file_t *file;
    const char *path;

    path = t_fmt("%pL/%s", &z_tmpdir_g, filepath);
    file = file_open(path, FILE_WRONLY | FILE_CREATE | FILE_TRUNC, 0644);
    Z_ASSERT_P(file);

    file_puts(file, yaml);
    file_puts(file, "\n");

    Z_ASSERT_N(file_close(&file));

    Z_HELPER_END;
}

static int
_z_pack_yaml_file(const char *filepath, const yaml_data_t *data,
                  const yaml__document_presentation__t *presentation,
                  unsigned flags, bool check_reparse_equals)
{
    t_scope;
    yaml_pack_env_t *env;
    yaml_parse_t *parse_env;
    yaml_data_t parsed_data;
    char *path;
    SB_1k(err);

    env = t_yaml_pack_env_new();
    if (flags) {
        yaml_pack_env_set_flags(env, flags);
    }
    path = t_fmt("%pL/%s", &z_tmpdir_g, filepath);
    if (presentation) {
        t_yaml_pack_env_set_presentation(env, presentation);
    }
    Z_ASSERT_N(t_yaml_pack_file(env, path, data, &err),
               "cannot pack YAML file %s: %pL", filepath, &err);

    if (flags & YAML_PACK_NO_SUBFILES) {
        /* this does not repack the full ast, so do not try to reparse it */
        return 0;
    }

    /* parse file and ensure the resulting AST is equal to what was packed. */
    if (flags & YAML_PACK_ALLOW_UNBOUND_VARIABLES) {
        parse_env = t_yaml_parse_new(YAML_PARSE_ALLOW_UNBOUND_VARIABLES);
    } else {
        parse_env = t_yaml_parse_new(0);
    }
    Z_ASSERT_N(t_yaml_parse_attach_file(parse_env, filepath, z_tmpdir_g.s,
                                        &err));
    Z_ASSERT_N(t_yaml_parse(parse_env, &parsed_data, &err),
               "could not reparse the packed file: %pL", &err);

    if (check_reparse_equals) {
        Z_ASSERT(yaml_data_equals(data, &parsed_data, true));
    }

    Z_HELPER_END;
}

static int
z_pack_yaml_file(const char *filepath, const yaml_data_t *data,
                 const yaml__document_presentation__t *presentation,
                 unsigned flags)
{
    return _z_pack_yaml_file(filepath, data, presentation, flags, true);
}

static int
z_pack_yaml_in_sb_with_subfiles(
    const char *dirpath, const yaml_data_t *data,
    const yaml__document_presentation__t *presentation,
    const char *expected_res)
{
    t_scope;
    yaml_pack_env_t *env;
    SB_1k(out);
    SB_1k(err);

    env = t_yaml_pack_env_new();
    dirpath = t_fmt("%pL/%s", &z_tmpdir_g, dirpath);
    Z_ASSERT_N(t_yaml_pack_env_set_outdir(env, dirpath, &err));
    if (presentation) {
        t_yaml_pack_env_set_presentation(env, presentation);
    }
    Z_ASSERT_N(t_yaml_pack_sb(env, data, &out, &err),
               "cannot pack YAML buffer: %pL", &err);
    Z_ASSERT_STREQUAL(out.data, expected_res);

    Z_HELPER_END;
}

static int z_check_file(const char *path, const char *expected_contents)
{
    t_scope;
    lstr_t contents;

    path = t_fmt("%pL/%s", &z_tmpdir_g, path);
    Z_ASSERT_N(lstr_init_from_file(&contents, path, PROT_READ, MAP_SHARED));
    Z_ASSERT_LSTREQUAL(contents, LSTR(expected_contents));
    lstr_wipe(&contents);

    Z_HELPER_END;
}

static int z_check_file_do_not_exist(const char *path)
{
    t_scope;

    path = t_fmt("%pL/%s", &z_tmpdir_g, path);
    Z_ASSERT_NEG(access(path, F_OK));

    Z_HELPER_END;
}

static int z_yaml_test_file_parse_fail(const char *yaml,
                                       const char *expected_err)
{
    t_scope;
    yaml_data_t data;
    yaml_parse_t *env = t_yaml_parse_new(0);
    SB_1k(err);

    Z_HELPER_RUN(z_write_yaml_file("input.yml", yaml));
    Z_ASSERT_N(t_yaml_parse_attach_file(env, "input.yml", z_tmpdir_g.s,
                                        &err),
               "%pL", &err);
    Z_ASSERT_NEG(t_yaml_parse(env, &data, &err));
    Z_ASSERT_STREQUAL(err.data, expected_err,
                      "wrong error message on yaml string `%s`", yaml);
    yaml_parse_delete(&env);

    Z_HELPER_END;
}

static int
z_yaml_test_pack(const yaml_data_t * nonnull data,
                 yaml__document_presentation__t * nullable pres,
                 unsigned flags, const char * nonnull expected_pack)
{
    yaml_pack_env_t *pack_env;
    SB_1k(pack);
    SB_1k(err);

    pack_env = t_yaml_pack_env_new();
    if (pres) {
        t_yaml_pack_env_set_presentation(pack_env, pres);
    }
    yaml_pack_env_set_flags(pack_env, flags);
    Z_ASSERT_N(t_yaml_pack_sb(pack_env, data, &pack, &err));
    Z_ASSERT_STREQUAL(pack.data, expected_pack,
                      "repacking the parsed data leads to differences");

    Z_HELPER_END;
}

/* out parameter first to let the yaml string be last, which makes it
 * much easier to write multiple lines without horrible indentation */
static
int t_z_yaml_test_parse_success_from_dir(
    yaml_data_t * nullable data,
    yaml__document_presentation__t *nullable pres,
    yaml_parse_t * nonnull * nullable env, unsigned flags,
    const char * nullable rootdir, const char * nonnull yaml,
    const char * nullable expected_repack)
{
    yaml__document_presentation__t p;
    yaml_data_t local_data;
    yaml_parse_t *local_env = NULL;
    SB_1k(err);

    if (!pres) {
        pres = &p;
    }
    if (!data) {
        data = &local_data;
    }
    if (!env) {
        env = &local_env;
    }
    if (!rootdir) {
        rootdir = z_tmpdir_g.s;
    }

    *env = t_yaml_parse_new(flags | YAML_PARSE_GEN_PRES_DATA);
    /* hack to make relative inclusion work from the rootdir */
    (*env)->fullpath = t_lstr_fmt("%s/foo.yml", rootdir);
    yaml_parse_attach_ps(*env, ps_initstr(yaml));
    Z_ASSERT_N(t_yaml_parse(*env, data, &err),
               "yaml parsing failed: %pL", &err);

    if (!expected_repack) {
        expected_repack = yaml;
    }

    /* repack using presentation data from the AST */
    Z_HELPER_RUN(z_yaml_test_pack(data, NULL, 0, expected_repack));

    /* repack using yaml_presentation_t specification, and not the
     * presentation data inside the AST */
    t_yaml_data_get_presentation(data, pres);
    Z_HELPER_RUN(z_yaml_test_pack(data, pres, 0, expected_repack));

    if (local_env) {
        yaml_parse_delete(&local_env);
    }

    Z_HELPER_END;
}

static
int t_z_yaml_test_parse_success(
    yaml_data_t * nullable data,
    yaml__document_presentation__t *nullable pres,
    yaml_parse_t * nonnull * nullable env, unsigned flags,
    const char * nonnull yaml, const char * nullable expected_repack)
{
    return t_z_yaml_test_parse_success_from_dir(data, pres, env, flags,
                                                NULL, yaml, expected_repack);
}

static int
z_check_yaml_span(const yaml_span_t *span,
                  uint32_t start_line, uint32_t start_col,
                  uint32_t end_line, uint32_t end_col)
{
    Z_ASSERT_EQ(span->start.line_nb, start_line);
    Z_ASSERT_EQ(span->start.col_nb, start_col);
    Z_ASSERT_EQ(span->end.line_nb, end_line);
    Z_ASSERT_EQ(span->end.col_nb, end_col);

    Z_HELPER_END;
}

static int
z_check_yaml_data(const yaml_data_t *data, yaml_data_type_t type,
                  uint32_t start_line, uint32_t start_col,
                  uint32_t end_line, uint32_t end_col)
{
    Z_ASSERT_EQ(data->type, type);
    Z_HELPER_RUN(z_check_yaml_span(&data->span, start_line, start_col,
                                   end_line, end_col));

    Z_HELPER_END;
}

static int
z_check_yaml_scalar(const yaml_data_t *data, yaml_scalar_type_t type,
                    uint32_t start_line, uint32_t start_col,
                    uint32_t end_line, uint32_t end_col)
{
    Z_HELPER_RUN(z_check_yaml_data(data, YAML_DATA_SCALAR, start_line,
                                   start_col, end_line, end_col));
    Z_ASSERT_EQ(data->scalar.type, type);

    Z_HELPER_END;
}

static int
z_check_yaml_pack(const yaml_data_t * nonnull data,
                  const yaml__document_presentation__t * nullable presentation,
                  const char *yaml)
{
    t_scope;
    yaml_pack_env_t *env = t_yaml_pack_env_new();
    SB_1k(sb);
    SB_1k(err);

    if (presentation) {
        t_yaml_pack_env_set_presentation(env, presentation);
    }
    Z_ASSERT_N(t_yaml_pack_sb(env, data, &sb, &err));
    Z_ASSERT_STREQUAL(sb.data, yaml);

    Z_HELPER_END;
}

static int z_check_inline_comment(const yaml_presentation_t * nonnull pres,
                                  lstr_t path, lstr_t comment)
{
    const yaml__presentation_node__t *pnode;

    pnode = qm_get_def_safe(yaml_pres_node, &pres->nodes, &path, NULL);
    Z_ASSERT_P(pnode);
    Z_ASSERT_LSTREQUAL(pnode->inline_comment, comment);

    Z_HELPER_END;
}

static int z_check_prefix_comments(const yaml_presentation_t * nonnull pres,
                                   lstr_t path, lstr_t *comments,
                                   int len)
{
    const yaml__presentation_node__t *pnode;

    pnode = qm_get_def_safe(yaml_pres_node, &pres->nodes, &path, NULL);
    Z_ASSERT_P(pnode);
    Z_ASSERT_EQ(len, pnode->prefix_comments.len);
    tab_for_each_pos(pos, &pnode->prefix_comments) {
        Z_ASSERT_LSTREQUAL(comments[pos], pnode->prefix_comments.tab[pos],
                           "prefix comment number #%d differs", pos);
    }

    Z_HELPER_END;
}

static int
z_test_var_in_str_change(const yaml_data_t * nonnull data,
                         const yaml__document_presentation__t * nonnull pres,
                         const char * nonnull root,
                         const char * nonnull inner)
{
    Z_HELPER_RUN(z_pack_yaml_file("vc_str/root.yml", data, pres, 0));
    Z_HELPER_RUN(z_check_file("vc_str/root.yml", root));
    Z_HELPER_RUN(z_check_file("vc_str/inner.yml", inner));

    Z_HELPER_END;
}

static int
z_test_pretty_print(const yaml_span_t * nonnull span,
                    const char * nonnull expected_err)
{
    SB_1k(buf);

    yaml_parse_pretty_print_err(span, LSTR("err"), &buf);
    Z_ASSERT_STREQUAL(buf.data, expected_err);

    Z_HELPER_END;
}

/* }}} */

Z_GROUP_EXPORT(yaml)
{
    MODULE_REQUIRE(yaml);

    /* {{{ Parsing errors */

    Z_TEST(parsing_errors, "errors when parsing yaml") {
        /* wrong object continuation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: 5\nb",

            "<string>:2:2: invalid key, missing colon\n"
            "b\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: 5\n%:",

            "<string>:2:1: invalid key, invalid character used\n"
            "%:\n"
            "^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "5: ~",

            "<string>:1:1: invalid key, "
            "name must start with an alphabetic character\n"
            "5: ~\n"
            "^"
        ));

        /* wrong explicit string */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "\" unfinished string",

            "<string>:1:20: expected string, missing closing '\"'\n"
            "\" unfinished string\n"
            "                   ^"
        ));

        /* wrong escaped code */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "\"\\",

            "<string>:1:2: expected string, invalid backslash\n"
            "\"\\\n"
            " ^"
        ));

        /* wrong tag */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!-",

            "<string>:1:2: invalid tag, must start with a letter\n"
            "!-\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!a,\n"
            "a: 5",

            "<string>:1:3: invalid tag, wrong character in tag\n"
            "!a,\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!4a\n"
            "a: 5",

            "<string>:1:2: invalid tag, must start with a letter\n"
            "!4a\n"
            " ^"
        ));
        /* TODO: improve span */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!tag1\n"
            "!tag2\n"
            "a: 2",

            "<string>:2:1: wrong object, two tags have been declared\n"
            "!tag2\n"
            "^^^^^"
        ));

        /* wrong list continuation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "- 2\n"
            "-3",

            "<string>:2:1: wrong type of data, "
            "expected another element of sequence\n"
            "-3\n"
            "^"
        ));

        /* wrong indent */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: 2\n"
            " b: 3",

            "<string>:2:2: wrong indentation, "
            "line not aligned with current object\n"
            " b: 3\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "- 2\n"
            " - 3",

            "<string>:2:2: wrong indentation, "
            "line not aligned with current sequence\n"
            " - 3\n"
            " ^"
        ));

        /* wrong object */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "foo: 1\n"
            "foo: 2",

            "<string>:2:1: invalid key, "
            "key is already declared in the object\n"
            "foo: 2\n"
            "^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "{ a: 1, a: 2}",

            "<string>:1:9: invalid key, "
            "key is already declared in the object\n"
            "{ a: 1, a: 2}\n"
            "        ^"
        ));

        /* cannot use tab characters for indentation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a:\t1",

            "<string>:1:3: tab character detected, "
            "cannot use tab characters for indentation\n"
            "a:\t1\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a:\n"
            "\t- 2\n"
            "\t- 3",

            "<string>:2:1: tab character detected, "
            "cannot use tab characters for indentation\n"
            "\t- 2\n"
            "^"
        ));

        /* extra data after the parsing */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "1\n"
            "# comment\n"
            "2",

            "<string>:3:1: extra characters after data, "
            "expected end of document\n"
            "2\n"
            "^"
        ));

        /* flow seq */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "[a[",

            "<string>:1:3: wrong type of data, "
            "expected another element of sequence\n"
            "[a[\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "[",

            "<string>:1:2: missing data, unexpected end of line\n"
            "[\n"
            " ^"
        ));

        /* flow obj */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "{,",

            "<string>:1:2: missing data, unexpected character\n"
            "{,\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "{a:b}",

            "<string>:1:2: wrong type of data, "
            "only key-value mappings are allowed inside an object\n"
            "{a:b}\n"
            " ^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "{a: b[",

            "<string>:1:6: wrong type of data, "
            "expected another element of object\n"
            "{a: b[\n"
            "     ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "{ a: b: c }",

            "<string>:1:7: wrong type of data, unexpected colon\n"
            "{ a: b: c }\n"
            "      ^"
        ));

        /* Unbound variables are rejected by default */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "key: $(var)",

            "the document is invalid: there are unbound variables: var"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "- $(a)\n"
            "- $(boo)",

            "the document is invalid: there are unbound variables: a, boo"
        ));

        /* Use of variables can be forbidden */
        Z_HELPER_RUN(z_yaml_test_parse_fail(YAML_PARSE_FORBID_VARIABLES,
            "key: 1\n"
            "a: <use of $(var)>\n",

            "<string>:2:4: use of variables is forbidden, "
            "cannot use variables in this context\n"
            "a: <use of $(var)>\n"
            "   ^^^^^^^^^^^^^^^"
        ));

        /* Merge key must be first */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "key: 1\n"
            "<<: { a: 2 }",

            "<string>:2:1: invalid key, "
            "merge key must be the first key in the object\n"
            "<<: { a: 2 }\n"
            "^^"
        ));

        /* merge key must contain an object, or a sequence of objects */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "<<: 2",

            "<string>:1:5: wrong type of data, "
            "value of merge key must be an object, or a list of objects\n"
            "<<: 2\n"
            "    ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "<<:\n"
            "  - a: 2\n"
            "  - - 2",

            "<string>:3:5: wrong type of data, "
            "value of merge key must be an object, or a list of objects\n"
            "  - - 2\n"
            "    ^^^"
        ));

        /* merge key elements must not use tags */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "<<: !foo { a: 2 }\n",

            "<string>:1:5: invalid tag, "
            "cannot use tags in a merge key\n"
            "<<: !foo { a: 2 }\n"
            "    ^^^^"
        ));

        /* invalid base64 */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!bin foo",

            "<string>:1:1: wrong type of data, "
            "binary data must be base64 encoded\n"
            "!bin foo\n"
            "^^^^^^^^"
        ));
        /* invalid binary tag use */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!bin a: 2",

            "<string>:1:1: wrong type of data, "
            "binary tag can only be used on strings\n"
            "!bin a: 2\n"
            "^^^^^^^^^"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing file errors */

    Z_TEST(parsing_file_errors, "errors when parsing YAML from files") {
        t_scope;
        yaml_parse_t *env;
        const char *path;
        const char *filename;
        SB_1k(err);

        env = t_yaml_parse_new(0);
        Z_ASSERT_NEG(t_yaml_parse_attach_file(env, "unknown.yml", NULL, &err));
        Z_ASSERT_STREQUAL(err.data, "cannot read file unknown.yml: "
                          "No such file or directory");

        /* create a file but make it unreadable */
        filename = "unreadable.yml";
        Z_HELPER_RUN(z_write_yaml_file(filename, "2"));
        path = t_fmt("%pL/%s", &z_tmpdir_g, filename);
        chmod(path, 220);

        Z_ASSERT_NEG(t_yaml_parse_attach_file(env, filename, z_tmpdir_g.s,
                                              &err));
        Z_ASSERT_STREQUAL(err.data, "cannot read file unreadable.yml: "
                          "Permission denied");
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing file */

    Z_TEST(parsing_file, "test parsing YAML files") {
        t_scope;
        yaml_parse_t *env;
        const char *filename;
        yaml_data_t data;
        SB_1k(err);

        /* make sure including a file relative to "." works */
        filename = "rel_include.yml";
        Z_HELPER_RUN(z_write_yaml_file(filename, "2"));
        Z_ASSERT_N(chdir(z_tmpdir_g.s));

        env = t_yaml_parse_new(0);
        Z_ASSERT_N(t_yaml_parse_attach_file(env, filename, ".", &err));
        Z_ASSERT_N(t_yaml_parse(env, &data, &err));
        Z_ASSERT(data.type == YAML_DATA_SCALAR);
        Z_ASSERT(data.scalar.type == YAML_SCALAR_UINT);
        Z_ASSERT(data.scalar.u == 2);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include errors */

    Z_TEST(include_errors, "errors when including YAML files") {
        /* unknown file */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include:foo.yml",

            "input.yml:1:1: invalid include, "
            "cannot read file foo.yml: No such file or directory\n"
            "!include:foo.yml\n"
            "^ starting here"
        ));

        /* error in included file */
        Z_HELPER_RUN(z_write_yaml_file("has_errors.yml",
            "key: 1\n"
            "key: 2"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include:has_errors.yml",

            "input.yml:1:1: error in included file\n"
            "!include:has_errors.yml\n"
            "^ starting here\n"
            "has_errors.yml:2:1: invalid key, "
            "key is already declared in the object\n"
            "key: 2\n"
            "^^^"
        ));

        /* loop detection: include one-self */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include:input.yml",

            "input.yml:1:1: invalid include, inclusion loop detected\n"
            "!include:input.yml\n"
            "^ starting here"
        ));
        /* loop detection: include a parent */
        Z_HELPER_RUN(z_write_yaml_file("loop-1.yml",
            "!include:loop-2.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("loop-2.yml",
            "!include:loop-3.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("loop-3.yml",
            "!include:loop-1.yml"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include:loop-1.yml",

            "input.yml:1:1: error in included file\n"
            "!include:loop-1.yml\n"
            "^ starting here\n"
            "loop-1.yml:1:1: error in included file\n"
            "!include:loop-2.yml\n"
            "^ starting here\n"
            "loop-2.yml:1:1: error in included file\n"
            "!include:loop-3.yml\n"
            "^ starting here\n"
            "loop-3.yml:1:1: invalid include, inclusion loop detected\n"
            "!include:loop-1.yml\n"
            "^ starting here"
        ));

        /* includes must be in same directory as including file */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include:../input.yml",

            "input.yml:1:1: invalid include, cannot include subfile "
            "`../input.yml`: only includes contained in the directory of the "
            "including file are allowed\n"
            "!include:../input.yml\n"
            "^ starting here"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("a/b"));
        Z_HELPER_RUN(z_write_yaml_file("a/b/gc.yml",
            "gc: !include:../../c.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("c.yml",
            "c: !include:../p.yml"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include:a/b/gc.yml",

            "input.yml:1:1: error in included file\n"
            "!include:a/b/gc.yml\n"
            "^ starting here\n"
            "a/b/gc.yml:1:5: error in included file\n"
            "gc: !include:../../c.yml\n"
            "    ^ starting here\n"
            "../../c.yml:1:4: invalid include, cannot include subfile "
            "`../p.yml`: only includes contained in the directory of the "
            "including file are allowed\n"
            "c: !include:../p.yml\n"
            "   ^ starting here"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include */

    Z_TEST(include, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "- a: 3\n"
            "  b: { c: c }\n"
            "- true"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(NULL, NULL, NULL, 0,
            "a: ~\n"
            "b: !include:inner.yml\n"
            "c: 3",

            "a: ~\n"
            "b:\n"
            "  - a: 3\n"
            "    b: { c: c }\n"
            "  - true\n"
            "c: 3"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("subdir/subsub"));
        Z_HELPER_RUN(z_write_yaml_file("subdir/a.yml",
            "- a\n"
            "- !include:b.yml\n"
            "- !include:subsub/d.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subdir/b.yml",
            "- !include:subsub/c.yml\n"
            "- b"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subdir/subsub/c.yml",
            "- c\n"
            "- !include:d.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subdir/subsub/d.yml",
            "d"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(NULL, NULL, NULL, 0,
            "!include:subdir/a.yml",

            "- a\n"
            "- - - c\n"
            "    - d\n"
            "  - b\n"
            "- d"
        ));

        /* Parent includes that stays in the "root" directory are allowed:
         *
         * root.yml (include x/b.yml)
         * x/
         *   y/
         *     a.yml (include ../../d.yml)
         *   b.yml (include ../c.yml)
         * c.yml (include x/y/a.yml)
         * d.yml
         *
         */
        Z_HELPER_RUN(z_create_tmp_subdir("x/y"));
        Z_HELPER_RUN(z_write_yaml_file("x/y/a.yml",
            "a: !include:../../d.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("x/b.yml",
            "b: !include:../c.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("c.yml",
            "c: !include:x/y/a.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("d.yml",
            "d"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:x/b.yml",

            "b:\n"
            "  c:\n"
            "    a: d"
        ));

        Z_HELPER_RUN(z_pack_yaml_file("inc-rel/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("inc-rel/root.yml",
            "!include:x/b.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("inc-rel/x/y/a.yml",
            "a: !include:../../d.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("inc-rel/x/b.yml",
            "b: !include:../c.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("inc-rel/c.yml",
            "c: !include:x/y/a.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("inc-rel/d.yml",
            "d\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include shared files */

    Z_TEST(include_shared_files, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_create_tmp_subdir("sf/sub"));
        Z_HELPER_RUN(z_write_yaml_file("sf/shared_1.yml", "1"));
        Z_HELPER_RUN(z_write_yaml_file("sf/sub/shared_1.yml", "-1"));
        Z_HELPER_RUN(z_write_yaml_file("sf/shared_2",
            "!include:sub/shared_1.yml"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "- !include:sf/shared_1.yml\n"
            "- !include:sf/././shared_1.yml\n"
            "- !include:sf/shared_1.yml\n"
            "- !include:sf/sub/shared_1.yml\n"
            "- !include:sf/../sf/sub/shared_1.yml\n"
            "- !include:sf/sub/shared_1.yml\n"
            "- !include:sf/shared_2\n"
            "- !include:./sf/shared_2",

            "- 1\n"
            "- 1\n"
            "- 1\n"
            "- -1\n"
            "- -1\n"
            "- -1\n"
            "- -1\n"
            "- -1"
        ));

        /* repacking it will shared the same subfiles */
        Z_HELPER_RUN(z_create_tmp_subdir("sf-pack-1"));
        Z_HELPER_RUN(z_pack_yaml_file("sf-pack-1/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("sf-pack-1/root.yml",
            "- !include:sf/shared_1.yml\n"
            "- !include:sf/shared_1.yml\n"
            "- !include:sf/shared_1.yml\n"
            "- !include:sf/sub/shared_1.yml\n"
            "- !include:sf/sub/shared_1.yml\n"
            "- !include:sf/sub/shared_1.yml\n"
            "- !include:sf/shared_2\n"
            "- !include:sf/shared_2\n"
        ));
        Z_HELPER_RUN(z_check_file("sf-pack-1/sf/shared_1.yml", "1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-1/sf/sub/shared_1.yml", "-1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-1/sf/shared_2",
            "!include:sub/shared_1.yml\n"
        ));

        /* modifying some data will force the repacking to create new files */
        data.seq->datas.tab[1].scalar.u = 2;
        data.seq->datas.tab[2].scalar.u = 2;
        data.seq->datas.tab[4].scalar.i = -2;
        data.seq->datas.tab[5].scalar.i = -3;
        data.seq->datas.tab[7].scalar.i = -3;
        Z_HELPER_RUN(z_create_tmp_subdir("sf-pack-2"));
        Z_HELPER_RUN(z_pack_yaml_file("sf-pack-2/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("sf-pack-2/root.yml",
            "- !include:sf/shared_1.yml\n"
            "- !include:sf/shared_1~1.yml\n"
            "- !include:sf/shared_1~1.yml\n"
            "- !include:sf/sub/shared_1.yml\n"
            "- !include:sf/sub/shared_1~1.yml\n"
            "- !include:sf/sub/shared_1~2.yml\n"
            "- !include:sf/shared_2\n"
            "- !include:sf/shared_2~1\n"
        ));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_1.yml", "1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_1~1.yml", "2\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/sub/shared_1.yml", "-1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/sub/shared_1~1.yml", "-2\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/sub/shared_1~2.yml", "-3\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_2",
            "!include:sub/shared_1.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_2~1",
            "!include:sub/shared_1~2.yml\n"
        ));
        yaml_parse_delete(&env);

    } Z_TEST_END;

    /* }}} */
    /* {{{ Include presentation */

    Z_TEST(include_presentation, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_create_tmp_subdir("subpres/in"));
        Z_HELPER_RUN(z_write_yaml_file("subpres/1.yml",
            "# Included!\n"
            "!include:in/sub.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subpres/in/sub.yml",
            "[ 4, 2 ] # packed"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subpres/weird~name",
            "jo: Jo\n"
            "# o\n"
            "o: ra"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "- !include:subpres/1.yml\n"
            "- !include:subpres/weird~name",

            /* XXX: the presentation associated with the "!include" data is
             * not included, as the data is inlined. */
            "- [ 4, 2 ] # packed\n"
            "- jo: Jo\n"
            "  # o\n"
            "  o: ra"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("newsubdir/in"));
        Z_HELPER_RUN(z_pack_yaml_file("newsubdir/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("newsubdir/root.yml",
            "- !include:subpres/1.yml\n"
            "- !include:subpres/weird~name\n"
        ));
        Z_HELPER_RUN(z_check_file("newsubdir/subpres/1.yml",
            "# Included!\n"
            "!include:in/sub.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("newsubdir/subpres/in/sub.yml",
            "[ 4, 2 ] # packed\n"
        ));
        Z_HELPER_RUN(z_check_file("newsubdir/subpres/weird~name",
            "jo: Jo\n"
            "# o\n"
            "o: ra\n"
        ));

        /* Test erasing the include presentation for a node. The include tags
         * should still be:
         *  !include:path
         * and not:
         *  !include:path ~
         */
        tab_for_each_ptr(mapping, &pres.mappings) {
            if (lstr_equal(mapping->path, LSTR("[0]!"))) {
                Z_ASSERT(mapping->node.included);
                iop_init(yaml__document_presentation,
                         &mapping->node.included->include_presentation);
                break;
            }
        }
        Z_HELPER_RUN(z_pack_yaml_file("newsubdir2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("newsubdir2/root.yml",
            "- !include:subpres/1.yml\n"
            "- !include:subpres/weird~name\n"
        ));

        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include raw */

    Z_TEST(include_raw, "") {
        t_scope;
        yaml_data_t data;
        yaml_data_t new_data;
        yaml_data_t bool_data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Write a JSON file */
        Z_HELPER_RUN(z_create_tmp_subdir("raw"));
        Z_HELPER_RUN(z_write_yaml_file("raw/inner.json",
            "{\n"
            "  \"foo\": 2\n"
            "}"
        ));
        /* include it verbatim as a string in a YAML document */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "- !includeraw:raw/inner.json",

            "- !bin ewogICJmb28iOiAyCn0K"
        ));
        Z_HELPER_RUN(z_test_pretty_print(
            &data.seq->datas.tab[0].span,
            "<string>:1:3: err\n"
            "- !includeraw:raw/inner.json\n"
            "  ^^^^^^^^^^^^^^^^^^^^^^^^^^"
        ));

        /* check repacking with presentation */
        Z_HELPER_RUN(z_pack_yaml_file("packraw/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("packraw/root.yml",
            "- !includeraw:raw/inner.json\n"
        ));
        Z_HELPER_RUN(z_check_file("packraw/raw/inner.json",
            "{\n"
            "  \"foo\": 2\n"
            "}\n"
        ));

        /* check that repacking as a string instead of bytes does not change
         * the result. */
        Z_HELPER_RUN(z_check_yaml_scalar(&data.seq->datas.tab[0],
                                         YAML_SCALAR_BYTES, 1, 3, 1, 29));
        data.seq->datas.tab[0].scalar.type = YAML_SCALAR_STRING;
        Z_HELPER_RUN(_z_pack_yaml_file("packraw/root.yml", &data, &pres, 0,
                                       false));
        data.seq->datas.tab[0].scalar.type = YAML_SCALAR_BYTES;
        Z_HELPER_RUN(z_check_file("packraw/root.yml",
            "- !includeraw:raw/inner.json\n"
        ));
        Z_HELPER_RUN(z_check_file("packraw/raw/inner.json",
            "{\n"
            "  \"foo\": 2\n"
            "}\n"
        ));

        /* if the included data is no longer a string, it will be dumped as
         * a classic include. */
        t_yaml_data_new_obj(&new_data, 2);
        yaml_obj_add_field(&new_data, LSTR("json"), data.seq->datas.tab[0]);
        yaml_data_set_bool(&bool_data, true);
        yaml_obj_add_field(&new_data, LSTR("b"), bool_data);
        data.seq->datas.tab[0] = new_data;
        Z_HELPER_RUN(z_pack_yaml_file("packraw2/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("packraw2/root.yml",
            /* TODO: we still keep the same file extension, which isn't ideal.
             * Maybe adding a .yml on top of it (without removing the old
             * extension) would be better, maybe even adding a prefix comment
             * for the include explaining the file could no longer be packed
             * raw. */
            "- !include:raw/inner.json\n"
        ));
        Z_HELPER_RUN(z_check_file("packraw2/raw/inner.json",
            "json: !bin ewogICJmb28iOiAyCn0K\n"
            "b: true\n"
        ));

        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include with symbolic links */

    Z_TEST(include_with_symlink, "") {
        t_scope;
        const char *path;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_write_yaml_file("a.yml",
            "a from top dir"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("subdir"));
        Z_HELPER_RUN(z_write_yaml_file("subdir/sym.yml",
            "- !include:a.yml\n"
            "- !include:b.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subdir/b.yml",
            "I am b"
        ));

        path = t_fmt("%pL/symlink.yml", &z_tmpdir_g);
        Z_ASSERT_N(symlink("subdir/sym.yml", path), "%m");

        /* This should fail: the include a.yml should not match
         * the top dir one. */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "!include:symlink.yml",

            "<string>:1:1: invalid include, cannot read file symlink.yml: "
            "No such file or directory\n"
            "!include:symlink.yml\n"
            "^^^^^^^^^^^^^^^^^^^^"
        ));

        Z_HELPER_RUN(z_write_yaml_file("subdir/a.yml",
            "a from sub dir"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "- !include:a.yml\n"
            "- !include:symlink.yml",

            "- a from top dir\n"
            "- - a from sub dir\n"
            "  - I am b"
        ));

        /* repack: this will cause conflict on a.yml */
        Z_HELPER_RUN(z_pack_yaml_file("out/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("out/root.yml",
            "- !include:a.yml\n"
            "- !include:symlink.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("out/a.yml",
            "a from top dir\n"
        ));
        Z_HELPER_RUN(z_check_file("out/symlink.yml",
            "- !include:a~1.yml\n"
            "- !include:b.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("out/a~1.yml",
            "a from sub dir\n"
        ));
        Z_HELPER_RUN(z_check_file("out/b.yml",
            "I am b\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include with symbolic links and root dir */

    Z_TEST(include_symlink_rootdir, "") {
        t_scope;
        const char *path;

        /* Including through a symbolic link resets the root dir, to allow
         * relative includes from a symbolic link outside of the original
         * root dir. */
        Z_HELPER_RUN(z_create_tmp_subdir("root_subdir"));
        Z_HELPER_RUN(z_create_tmp_subdir("sym_subdir"));

        Z_HELPER_RUN(z_write_yaml_file("sym_subdir/sym.yml",
            "!include:a.yml\n"
        ));
        Z_HELPER_RUN(z_write_yaml_file("sym_subdir/a.yml",
            "I am a from sym dir"
        ));
        path = t_fmt("%pL/root_subdir/symlink.yml", &z_tmpdir_g);
        Z_ASSERT_N(symlink("../sym_subdir/sym.yml", path), "%m");


        /* This should fail: the include a.yml should not match
         * the top dir one. */
        path = t_fmt("%pL/root_subdir", &z_tmpdir_g);
        Z_HELPER_RUN(t_z_yaml_test_parse_success_from_dir(
            NULL, NULL, NULL, 0, path,
            "!include:symlink.yml",

            "I am a from sym dir"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Override */

    Z_TEST(override, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *grandchild;
        const char *child;
        const char *root;

        /* test override of scalars, object, sequence */
        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: 3\n"
            "b: { c: c }\n"
            "c:\n"
            "  - 3\n"
            "  - 4\n"
            "d: {}\n"
            "e: []"
        ));
        root =
            "- !include:inner.yml\n"
            "  a: 4\n"
            "\n"
            "  b: { new: true, c: ~ }\n"
            "  c: [ 5, 6 ] # array\n"
            "  d:\n"
            "    dd: 7\n"
            "  e:\n"
            "    - []\n"
            "  # prefix f\n"
            "  f: ~";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "- a: 4\n"
            "  b: { c: ~, new: true }\n"
            "  c:\n"
            "    - 3\n"
            "    - 4\n"
            "    - 5\n"
            "    - 6\n"
            "  d:\n"
            "    dd: 7\n"
            "  e:\n"
            "    - []\n"
            "  f: ~"
        ));
        /* test recreation of override when packing into files */
        Z_HELPER_RUN(z_pack_yaml_file("override_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("override_1/root.yml",
                                  t_fmt("%s\n", root)));
        Z_HELPER_RUN(z_check_file("override_1/inner.yml",
            /* XXX: lost flow mode, incompatible with override */
            "a: 3\n"
            "b:\n"
            "  c: c\n"
            "c:\n"
            "  - 3\n"
            "  - 4\n"
            "d: {}\n"
            "e: []\n"
        ));
        Z_HELPER_RUN(z_check_file("override_1/root.yml",
                                  t_fmt("%s\n", root)));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, YAML_PACK_NO_SUBFILES,
                                      root));
        yaml_parse_delete(&env);

        /* test override of override through includes */
        grandchild =
            "# prefix gc a\n"
            "a: 1 # inline gc 1\n"
            "# prefix gc b\n"
            "b: 2 # inline gc 2\n"
            "# prefix gc c\n"
            "c: 3 # inline gc 3\n"
            "# prefix gc d\n"
            "d: 4 # inline gc 4\n";
        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml", grandchild));
        child =
            "# prefix child g\n"
            "g:\n"
            "  # prefix include gc\n"
            "  !include:grandchild.yml\n"
            "  c: 5 # inline child 5\n"
            "  # prefix child d\n"
            "  d: 6 # inline child 6\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));
        root =
            "# prefix seq\n"
            "-\n"
            "  # prefix include c\n"
            "  !include:child.yml\n"
            "  g: # inline g\n"
            "    # prefix b\n"
            "    b: 7 # inline 7\n"
            "    # prefix c\n"
            "    c: 8 # inline 8\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            /* XXX: the presentation of the original file is used. This is
             * a side effect of how the presentation is stored, but it isn't
             * clear what would be the right behavior. Both have sensical
             * meaning. */
            "# prefix seq\n"
            "-\n"
            "  # prefix child g\n"
            "  g:\n"
            "    # prefix gc a\n"
            "    a: 1 # inline gc 1\n"
            "    # prefix gc b\n"
            "    b: 7 # inline gc 2\n"
            "    # prefix gc c\n"
            "    c: 8 # inline gc 3\n"
            "    # prefix gc d\n"
            "    d: 6 # inline gc 4\n"
        ));

        /* test recreation of override when packing into files */
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, YAML_PACK_NO_SUBFILES,
                                      root));
        Z_HELPER_RUN(z_pack_yaml_file("override_2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("override_2/grandchild.yml",
                                  grandchild));
        Z_HELPER_RUN(z_check_file("override_2/child.yml",
                                  child));
        Z_HELPER_RUN(z_check_file("override_2/root.yml",
                                  root));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Override errors */

    Z_TEST(override_errors, "") {
        t_scope;

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: { b: { c: { d: { e: ~ } } } }"
        ));
        Z_HELPER_RUN(z_write_yaml_file("override.yml", "2"));

        /* only objects allowed as overrides */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  - 1\n"
            "  - 2",

            "input.yml:1:6: wrong type of data, "
            "override data after include must be an object\n"
            "key: !include:inner.yml\n"
            "     ^ starting here"
        ));
        /* See ticket #73983. An override from an include should get a proper
         * span. We test this by triggering an error on the override. */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  - !include:override.yml",

            "input.yml:1:6: wrong type of data, "
            "override data after include must be an object\n"
            "key: !include:inner.yml\n"
            "     ^ starting here"
        ));


        /* must have same type as overridden data */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  a:\n"
            "    b:\n"
            "      c:\n"
            "        - 1",

            "input.yml:5:9: cannot change types of data in override, "
            "overridden data is an object and not a sequence\n"
            "        - 1\n"
            "        ^ starting here"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Override conflict handling */

    Z_TEST(override_conflict_handling, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *root;

        /* Test removal of added node */
        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: 1\n"
            "b: 2"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "- !include:inner.yml\n"
            "  b: 3\n"
            "  c: 4",

            "- a: 1\n"
            "  b: 3\n"
            "  c: 4"
        ));

        /* modify value in the AST */
        data.seq->datas.tab[0].obj->fields.tab[1].data.scalar.u = 10;
        data.seq->datas.tab[0].obj->fields.tab[2].data.scalar.u = 20;

        /* This value should get resolved in the override */
        root =
            "- !include:inner.yml\n"
            "  b: 10\n"
            "  c: 20";
        Z_HELPER_RUN(z_pack_yaml_in_sb_with_subfiles("conflicts_1", &data,
                                                     &pres, root));
        Z_HELPER_RUN(z_check_file("conflicts_1/inner.yml",
            "a: 1\n"
            "b: 2\n"
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, YAML_PACK_NO_SUBFILES,
                                      root));

        /* remove added node from AST */
        data.seq->datas.tab[0].obj->fields.len--;

        /* When packing into files, the override is normally recreated, but
         * here a node is removed. */
        Z_HELPER_RUN(z_pack_yaml_in_sb_with_subfiles("conflicts_2", &data,
                                                     &pres,
            "- !include:inner.yml\n"
            "  b: 10"
        ));
        Z_HELPER_RUN(z_check_file("conflicts_2/inner.yml",
            "a: 1\n"
            "b: 2\n"
        ));

        /* Remove node b as well. This will remove the override entirely. */
        data.seq->datas.tab[0].obj->fields.len--;
        Z_HELPER_RUN(z_pack_yaml_in_sb_with_subfiles("conflicts_3", &data,
                                                     &pres,
            /* TODO: we lost the overrides, so we pack an empty obj. It would
             * be cleaner to pach an empty null. */
            "- !include:inner.yml {}"
        ));
        Z_HELPER_RUN(z_check_file("conflicts_3/inner.yml",
            "a: 1\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Override shared subfiles */

    Z_TEST(override_shared_subfiles, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *root;

        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml",
            "a: a\n"
            "b: b"
        ));
        Z_HELPER_RUN(z_write_yaml_file("child.yml",
            "!include:grandchild.yml\n"
            "b: B"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "- !include:child.yml\n"
            "  a: 0\n"
            "- !include:child.yml\n"
            "  a: 1\n"
            "- !include:child.yml\n"
            "  b: 2",

            "- a: 0\n"
            "  b: B\n"
            "- a: 1\n"
            "  b: B\n"
            "- a: a\n"
            "  b: 2"
        ));

        /* repack into a file: the included subfiles should be shared */
        root =
            "- !include:child.yml\n"
            "  a: 0\n"
            "- !include:child.yml\n"
            "  a: 1\n"
            "- !include:child.yml\n"
            "  b: 2";
        Z_HELPER_RUN(z_pack_yaml_in_sb_with_subfiles("override_shared_1",
                                                     &data, &pres, root));
        Z_HELPER_RUN(z_check_file("override_shared_1/child.yml",
            "!include:grandchild.yml\n"
            "b: B\n"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_1/grandchild.yml",
            "a: a\n"
            "b: b\n"
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, YAML_PACK_NO_SUBFILES,
                                      root));

        /* modify [0].b. This will modify its child, but the grandchild is
         * still shared. */
        data.seq->datas.tab[0].obj->fields.tab[1].data.scalar.s = LSTR("B2");
        Z_HELPER_RUN(z_pack_yaml_in_sb_with_subfiles("override_shared_2",
                                                     &data, &pres,
            "- !include:child.yml\n"
            "  a: 0\n"
            "- !include:child~1.yml\n"
            "  a: 1\n"
            "- !include:child~1.yml\n"
            "  b: 2"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/child.yml",
            "!include:grandchild.yml\n"
            "b: B2\n"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/child~1.yml",
            "!include:grandchild.yml\n"
            "b: B\n"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/grandchild.yml",
            "a: a\n"
            "b: b\n"
        ));
        /* When packing with NO_SUBFILES, we do not check for collisions,
         * so the include path are kept. */
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, YAML_PACK_NO_SUBFILES,
                                      root));

        /* reset [0].b, and modify [2].a. The grandchild will differ, but the
         * child is the same */
        data.seq->datas.tab[0].obj->fields.tab[1].data.scalar.s = LSTR("B");
        data.seq->datas.tab[2].obj->fields.tab[0].data.scalar.s = LSTR("A");

        Z_HELPER_RUN(z_pack_yaml_in_sb_with_subfiles("override_shared_2",
                                                     &data, &pres,
            "- !include:child.yml\n"
            "  a: 0\n"
            "- !include:child.yml\n"
            "  a: 1\n"
            "- !include:child~1.yml\n"
            "  b: 2"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/child.yml",
            "!include:grandchild.yml\n"
            "b: B\n"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/child~1.yml",
            "!include:grandchild~1.yml\n"
            "b: B\n"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/grandchild.yml",
            "a: a\n"
            "b: b\n"
        ));
        Z_HELPER_RUN(z_check_file("override_shared_2/grandchild~1.yml",
            "a: A\n"
            "b: b\n"
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, YAML_PACK_NO_SUBFILES,
                                      root));

        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Merge key */

    Z_TEST(merge_key, "") {
        t_scope;
        yaml__document_presentation__t empty_pres;
        yaml_data_t data;
        yaml_data_t *f;

        /* Allow packing with empty presentation, to get raw AST */
        iop_init(yaml__document_presentation, &empty_pres);

        /* test with a single merge key */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!foo\n"
            "<<:\n"
            "  a: 2\n"
            "  d: ~",

            NULL
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &empty_pres, 0,
            "!foo\n"
            "a: 2\n"
            "d: ~"
        ));

        /* merge key + obj */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "<<:\n"
            "  a: 2\n"
            "  d: ~\n"
            "a: 1\n"
            "c: 3",

            NULL
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &empty_pres, 0,
            "a: 1\n"
            "d: ~\n"
            "c: 3"
        ));

        /* multiple merge elements + obj */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "a:\n"
            "  <<:\n"
            "    - x: 1\n"
            "      y: 2\n"
            "      w: 8\n"
            "    - { x: 3, z: -1, p: 0 }\n"
            "  p: 3\n"
            "  w: a\n"
            "  q: ~",

            NULL
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &empty_pres, 0,
            "a:\n"
            "  x: 3\n"
            "  y: 2\n"
            "  w: a\n"
            "  z: -1\n"
            "  p: 3\n"
            "  q: ~"
        ));

        f = &data.obj->fields.tab[0].data;
        /* Test correct spans for all fields */
        Z_HELPER_RUN(z_test_pretty_print(
            &f->obj->fields.tab[0].key_span,
            "<string>:6:9: err\n"
            "    - { x: 3, z: -1, p: 0 }\n"
            "        ^"
        ));
        Z_HELPER_RUN(z_test_pretty_print(
            &f->obj->fields.tab[0].data.span,
            "<string>:6:12: err\n"
            "    - { x: 3, z: -1, p: 0 }\n"
            "           ^"
        ));
        Z_HELPER_RUN(z_test_pretty_print(
            &f->obj->fields.tab[1].key_span,
            "<string>:4:7: err\n"
            "      y: 2\n"
            "      ^"
        ));
        Z_HELPER_RUN(z_test_pretty_print(
            &f->obj->fields.tab[1].data.span,
            "<string>:4:10: err\n"
            "      y: 2\n"
            "         ^"
        ));
        Z_HELPER_RUN(z_test_pretty_print(
            &f->obj->fields.tab[4].key_span,
            "<string>:7:3: err\n"
            "  p: 3\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_test_pretty_print(
            &f->obj->fields.tab[4].data.span,
            "<string>:7:6: err\n"
            "  p: 3\n"
            "     ^"
        ));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "<<:\n"
            "  - x: 1\n"
            "  - x: 2\n"
            "x: 3",

            NULL
        ));
        Z_HELPER_RUN(z_yaml_test_pack(&data, &empty_pres, 0,
            "x: 3"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Merge key with includes */

    Z_TEST(merge_key_with_includes, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t empty_pres;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *child1;
        const char *child2;
        const char *gc2;
        const char *root;

        child1 =
            "# This is child1 values\n"
            "a: se\n"
            "b:\n"
            "  - ki # comment\n"
            "  - ro\n";
        Z_HELPER_RUN(z_write_yaml_file("child1.yml", child1));
        gc2 =
            "c: shu\n";
        Z_HELPER_RUN(z_write_yaml_file("gc2.yml", gc2));
        child2 =
            "!include:gc2.yml\n"
            "d: ar\n";
        Z_HELPER_RUN(z_write_yaml_file("child2.yml", child2));
        root =
            "# Add default values\n"
            "<<:\n"
            "  # goty\n"
            "  - !include:child1.yml\n"
            /* FIXME: '  # bad\n' is not saved, to fix! */
            /* "  # bad\n" */
            "  - !include:child2.yml\n"
            "    d: ra\n"
            "\n"
            "# Then add specific values\n"
            "e: ISS\n";

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "# Add default values\n"
            "<<:\n"
            "  # goty\n"
            "  -\n"
            "    # This is child1 values\n"
            "    a: se\n"
            "    b:\n"
            "      - ki # comment\n"
            "      - ro\n"
            "  - c: shu\n"
            "    d: ra\n"
            "\n"
            "# Then add specific values\n"
            "e: ISS"
        ));

        /* Test packing with empty presentation, to get raw AST */
        iop_init(yaml__document_presentation, &empty_pres);
        Z_HELPER_RUN(z_yaml_test_pack(&data, &empty_pres, 0,
            "a: se\n"
            "b:\n"
            "  - ki\n"
            "  - ro\n"
            "c: shu\n"
            "d: ra\n"
            "e: ISS"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("merge_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("merge_1/root.yml", root));
        Z_HELPER_RUN(z_check_file("merge_1/child1.yml", child1));
        Z_HELPER_RUN(z_check_file("merge_1/child2.yml", child2));
        Z_HELPER_RUN(z_check_file("merge_1/gc2.yml", gc2));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Merge key modification handling */

    Z_TEST(merge_key_modif_handling, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        yaml_data_t new_data;
        yaml_data_t new_scalar;

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "<<:\n"
            "  - { x: 1, y: 2, z: 3 }\n"
            "  - { a: a, y: 3, c: c }\n"
            "z: z\n"
            "c: C",

            NULL
        ));
        /* modify data to:
         *  - remove one override: x, y, z
         *  - change a value used in one element and after: c
         *  - add a new element: k
         */
        t_yaml_data_new_obj(&new_data, 3);

        yaml_data_set_string(&new_scalar, LSTR("O"));
        yaml_obj_add_field(&new_data, LSTR("c"), new_scalar);

        yaml_data_set_string(&new_scalar, LSTR("a"));
        yaml_obj_add_field(&new_data, LSTR("a"), new_scalar);

        yaml_data_set_string(&new_scalar, LSTR("K"));
        yaml_obj_add_field(&new_data, LSTR("k"), new_scalar);

        Z_HELPER_RUN(z_yaml_test_pack(&new_data, &pres, 0,
            "<<:\n"
            "  a: a\n"
            "  c: c\n"
            "c: O\n"
            "k: K"
        ));
        yaml_parse_delete(&env);

        /* test removal of keys not in merge key */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "<<: { x: x }\n"
            "y: y\n"
            "z: z",

            NULL
        ));

        /* Remove y and z */
        data.obj->fields.len -= 2;
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, 0,
            "<<: { x: x }"
        ));

        /* change z */
        data.obj->fields.len += 2;
        data.obj->fields.tab[2].key = LSTR("a");
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, 0,
            "<<: { x: x }\n"
            "y: y\n"
            "a: z"
        ));

        /* change y as well */
        data.obj->fields.tab[1].key = LSTR("b");
        Z_HELPER_RUN(z_yaml_test_pack(&data, &pres, 0,
            "<<: { x: x }\n"
            "b: y\n"
            "a: z"
        ));
        yaml_parse_delete(&env);

        /* test addition of new keys, spilling outside the merge key */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "<<: { x: x }",

            NULL
        ));

        t_yaml_data_new_obj(&new_data, 2);
        yaml_data_set_string(&new_scalar, LSTR("0"));
        yaml_obj_add_field(&new_data, LSTR("x"), new_scalar);
        yaml_data_set_string(&new_scalar, LSTR("1"));
        yaml_obj_add_field(&new_data, LSTR("y"), new_scalar);

        Z_HELPER_RUN(z_yaml_test_pack(&new_data, &pres, 0,
            "<<: { x: 0 }\n"
            "y: 1"
        ));
        yaml_parse_delete(&env);

        /* test suppression of last merge element, and addition of new fields.
         */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "<<:\n"
            "  - { x: x }\n"
            "  - { y: y }",

            NULL
        ));

        t_yaml_data_new_obj(&new_data, 2);
        yaml_data_set_string(&new_scalar, LSTR("x"));
        yaml_obj_add_field(&new_data, LSTR("x"), new_scalar);
        Z_HELPER_RUN(z_yaml_test_pack(&new_data, &pres, 0,
            "<<:\n"
            "  x: x"
        ));

        yaml_data_set_string(&new_scalar, LSTR("z"));
        yaml_obj_add_field(&new_data, LSTR("z"), new_scalar);
        Z_HELPER_RUN(z_yaml_test_pack(&new_data, &pres, 0,
            "<<:\n"
            "  x: x\n"
            "z: z"
        ));

        new_data.obj->fields.tab[0].key = LSTR("y");
        Z_HELPER_RUN(z_yaml_test_pack(&new_data, &pres, 0,
            "<<:\n"
            "  y: x\n"
            "z: z"
        ));

        new_data.obj->fields.len--;
        Z_HELPER_RUN(z_yaml_test_pack(&new_data, &pres, 0,
            "<<:\n"
            "  y: x"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Merge key with override */

    Z_TEST(merge_key_with_override, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *child;
        const char *root;

        /* TODO: overrides do not work well with merge keys. This is a
         * test example showing how it behaves... */

        child =
            "<<:\n"
            "  - { x: x, y: [ 1, 2 ], w: 2 }\n"
            "  - { x: X, z: z, w: 3 }\n"
            "x:\n"
            "  a: A\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));
        root =
            "!include:child.yml\n"
            "w: 4\n"
            "x:\n"
            "  a: ~\n"
            "  b: b\n"
            "y:\n"
            "  - 3\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "<<:\n"
            "  - { x: x, y: [ 1, 2, 3 ], w: 2 }\n"
            "  - { x: X, z: z, w: 4 }\n"
            "x:\n"
            "  a: ~\n"
            "  b: b"
        ));

        Z_HELPER_RUN(z_pack_yaml_file("merge_ov_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("merge_ov_1/root.yml",
            "!include:child.yml\n"
            "x:\n"
            "  a: ~\n"
            "  b: b\n"
        ));
        Z_HELPER_RUN(z_check_file("merge_ov_1/child.yml",
            "<<:\n"
            "  - { x: x, y: [ 1, 2, 3 ], w: 2 }\n"
            "  - { x: X, z: z, w: 4 }\n"
            "x:\n"
            "  a: A\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing scalars */

    Z_TEST(parsing_scalar, "test parsing of scalars") {
        t_scope;
        yaml_data_t data;

        /* string */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "unquoted string",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 16));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a string value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag unquoted string",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 21));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged string value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "\" quoted: 5 \"",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 14));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR(" quoted: 5 "));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "  trimmed   ",
            "trimmed"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 3, 1, 10));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("trimmed"));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "a:x:b",
            "\"a:x:b\""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 6));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("a:x:b"));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "\"true\"",
            "\"true\""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 7));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("true"));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "\"\\$a\"",
            "\"\\\\$a\""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 6));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("\\$a"));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "\"\\$(a\"",
            "\"\\$(a\""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 7));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("$(a"));

        /* null */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "~",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 2));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a null value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag ~",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 7));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged null value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "null",
            "~"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "NulL",
            "~"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "",
            ""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 1));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!v",
            "!v"
        ));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("v"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 3));

        /* bool */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "true",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a boolean value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag true",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 10));
        Z_ASSERT(data.scalar.b);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged boolean value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "TrUE",
            "true"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "false",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "FALse",
            "false"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        /* uint */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "0",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 2));
        Z_ASSERT_EQ(data.scalar.u, 0UL);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an unsigned integer value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag 0",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 7));
        Z_ASSERT_EQ(data.scalar.u, 0UL);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged unsigned integer value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "153",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.u, 153UL);

        /* -0 will still generate UINT */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "-0",
            "0"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 3));

        /* int */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "-1",
            NULL));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 3));
        Z_ASSERT_EQ(data.scalar.i, -1L);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an integer value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag -1",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 8));
        Z_ASSERT_EQ(data.scalar.i, -1L);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged integer value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "-153",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.i, -153L);

        /* double */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "0.5",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.d, 0.5);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a double value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag 0.5",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 9));
        Z_ASSERT_EQ(data.scalar.d, 0.5);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged double value");

        /* TODO: should a dot be added to show its a floating-point number. */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "-1e3",
            "-1000"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.d, -1000.0);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "-.Inf",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 6));
        Z_ASSERT_EQ(isinf(data.scalar.d), -1);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            ".INf",
            ".Inf"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(isinf(data.scalar.d), 1);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            ".NAN",
            ".NaN"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT(isnan(data.scalar.d));

        /* binary */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!bin SGk=",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BYTES,
                                         1, 1, 1, 10));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("Hi"));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false), "a binary value");

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!bin ABCDEFGH",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BYTES,
                                         1, 1, 1, 14));
        Z_ASSERT_LSTREQUAL(data.scalar.s,
                           LSTR_DATA_V("\x00\x10\x83\x10\x51\x87", 6));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!bin \"1234\"",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BYTES,
                                         1, 1, 1, 12));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR_DATA_V("\xd7\x6d\xf8", 3));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing objects */

    Z_TEST(parsing_obj, "test parsing of objects") {
        t_scope;
        yaml_data_t data;
        yaml_data_t field;
        yaml_data_t field2;

        logger_set_level(LSTR("yaml"), LOG_TRACE + 2, 0);

        /* one liner */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "a: 2",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 5));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.obj->fields.len == 1);
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[0].key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&data.obj->fields.tab[0].key_span,
                                       1, 1, 1, 2));
        field = data.obj->fields.tab[0].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_UINT,
                                         1, 4, 1, 5));
        Z_ASSERT_EQ(field.scalar.u, 2UL);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an object");

        /* with tag */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "!tag1 a: 2",

            "!tag1\n"
            "a: 2"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 11));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag1"));
        Z_ASSERT(data.obj->fields.len == 1);
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[0].key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&data.obj->fields.tab[0].key_span,
                                       1, 7, 1, 8));
        field = data.obj->fields.tab[0].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_UINT,
                                         1, 10, 1, 11));
        Z_ASSERT_EQ(field.scalar.u, 2UL);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged object");

        /* imbricated objects */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "a: 2\n"
            "inner: b: 3\n"
            "       c: -4\n"
            "inner2: !tag\n"
            "  d:\n"
            "  e: my-label\n"
            "f: 1.2",

            "a: 2\n"
            "inner:\n"
            "  b: 3\n"
            "  c: -4\n"
            "inner2: !tag\n"
            "  d:\n"
            "  e: my-label\n"
            "f: 1.2"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 7, 7));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.obj->fields.len == 4);

        /* a */
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[0].key, LSTR("a"));
        field = data.obj->fields.tab[0].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_UINT,
                                         1, 4, 1, 5));
        Z_ASSERT_EQ(field.scalar.u, 2UL);

        /* inner */
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[1].key, LSTR("inner"));
        Z_HELPER_RUN(z_check_yaml_span(&data.obj->fields.tab[1].key_span,
                                       2, 1, 2, 6));
        field = data.obj->fields.tab[1].data;
        Z_HELPER_RUN(z_check_yaml_data(&field, YAML_DATA_OBJ, 2, 8, 3, 13));
        Z_ASSERT_NULL(field.tag.s);
        Z_ASSERT(field.obj->fields.len == 2);

        Z_ASSERT_LSTREQUAL(field.obj->fields.tab[0].key, LSTR("b"));
        Z_HELPER_RUN(z_check_yaml_span(&field.obj->fields.tab[0].key_span,
                                       2, 8, 2, 9));
        field2 = field.obj->fields.tab[0].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_UINT,
                                         2, 11, 2, 12));
        Z_ASSERT_EQ(field2.scalar.u, 3UL);
        Z_ASSERT_LSTREQUAL(field.obj->fields.tab[1].key, LSTR("c"));
        Z_HELPER_RUN(z_check_yaml_span(&field.obj->fields.tab[1].key_span,
                                       3, 8, 3, 9));
        field2 = field.obj->fields.tab[1].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_INT,
                                         3, 11, 3, 13));
        Z_ASSERT_EQ(field2.scalar.i, -4L);

        /* inner2 */
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[2].key, LSTR("inner2"));
        Z_HELPER_RUN(z_check_yaml_span(&data.obj->fields.tab[2].key_span,
                                       4, 1, 4, 7));
        field = data.obj->fields.tab[2].data;
        Z_HELPER_RUN(z_check_yaml_data(&field, YAML_DATA_OBJ, 4, 9, 6, 14));
        Z_ASSERT_LSTREQUAL(field.tag, LSTR("tag"));
        Z_ASSERT(field.obj->fields.len == 2);

        Z_ASSERT_LSTREQUAL(field.obj->fields.tab[0].key, LSTR("d"));
        field2 = field.obj->fields.tab[0].data;
        /* TODO: span could be better, on the previous line */
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_NULL,
                                         6, 3, 6, 3));
        Z_ASSERT_LSTREQUAL(field.obj->fields.tab[1].key, LSTR("e"));
        field2 = field.obj->fields.tab[1].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_STRING,
                                         6, 6, 6, 14));
        Z_ASSERT_LSTREQUAL(field2.scalar.s, LSTR("my-label"));

        /* f */
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[3].key, LSTR("f"));
        field = data.obj->fields.tab[3].data;
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_DOUBLE,
                                         7, 4, 7, 7));
        Z_ASSERT_EQ(field.scalar.d, 1.2);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing sequences */

    Z_TEST(parsing_seq, "test parsing of sequences") {
        t_scope;
        yaml_data_t data;
        yaml_data_t elem;

        /* one liner */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "- a",
            NULL
        ));
        Z_ASSERT_NULL(data.tag.s);
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 4));
        Z_ASSERT_EQ(data.seq->datas.len, 1);
        Z_HELPER_RUN(z_check_yaml_scalar(&data.seq->datas.tab[0],
                                         YAML_SCALAR_STRING, 1, 3, 1, 4));
        Z_ASSERT_LSTREQUAL(data.seq->datas.tab[0].scalar.s, LSTR("a"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a sequence");

        /* imbricated sequences */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "- \"a: 2\"\n"
            "- - 5\n"
            "  - -5\n"
            "-\n"
            "-\n"
            "  !tag - TRUE\n"
            "- FALSE\n",

            "- \"a: 2\"\n"
            "- - 5\n"
            "  - -5\n"
            "-\n"
            "- !tag\n"
            "  - true\n"
            "- false"
        ));

        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 8, 1));
        Z_ASSERT_EQ(data.seq->datas.len, 5);

        /* "a: 2" */
        elem = data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_STRING,
                                         1, 3, 1, 9));
        Z_ASSERT_LSTREQUAL(elem.scalar.s, LSTR("a: 2"));

        /* subseq */
        elem = data.seq->datas.tab[1];
        Z_HELPER_RUN(z_check_yaml_data(&elem, YAML_DATA_SEQ, 2, 3, 4, 1));
        Z_ASSERT_EQ(elem.seq->datas.len, 2);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq->datas.tab[0], YAML_SCALAR_UINT,
                                         2, 5, 2, 6));
        Z_ASSERT_EQ(elem.seq->datas.tab[0].scalar.u, 5UL);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq->datas.tab[1], YAML_SCALAR_INT,
                                         3, 5, 3, 7));
        Z_ASSERT_EQ(elem.seq->datas.tab[1].scalar.i, -5L);

        /* null */
        elem = data.seq->datas.tab[2];
        /* TODO: span could be better */
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_NULL,
                                         5, 1, 5, 1));

        /* subseq */
        elem = data.seq->datas.tab[3];
        Z_HELPER_RUN(z_check_yaml_data(&elem, YAML_DATA_SEQ, 6, 3, 7, 1));
        Z_ASSERT_LSTREQUAL(elem.tag, LSTR("tag"));
        Z_ASSERT_EQ(elem.seq->datas.len, 1);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq->datas.tab[0], YAML_SCALAR_BOOL,
                                         6, 10, 6, 14));
        Z_ASSERT(elem.seq->datas.tab[0].scalar.b);

        /* false */
        elem = data.seq->datas.tab[4];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_BOOL,
                                         7, 3, 7, 8));
        Z_ASSERT(!elem.scalar.b);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing complex data */

    Z_TEST(parsing_complex_data, "test parsing of more complex data") {
        t_scope;
        yaml_data_t data;
        yaml_data_t field;

        /* sequence on same level as key */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "a:\n"
            "- 3\n"
            "- ~",

            "a:\n"
            "  - 3\n"
            "  - ~"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 3, 4));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.obj->fields.len == 1);
        Z_ASSERT_LSTREQUAL(data.obj->fields.tab[0].key, LSTR("a"));
        field = data.obj->fields.tab[0].data;

        Z_HELPER_RUN(z_check_yaml_data(&field, YAML_DATA_SEQ, 2, 1, 3, 4));
        Z_ASSERT_EQ(field.seq->datas.len, 2);
        Z_HELPER_RUN(z_check_yaml_scalar(&field.seq->datas.tab[0], YAML_SCALAR_UINT,
                                         2, 3, 2, 4));
        Z_ASSERT_EQ(field.seq->datas.tab[0].scalar.u, 3UL);
        Z_HELPER_RUN(z_check_yaml_scalar(&field.seq->datas.tab[1], YAML_SCALAR_NULL,
                                         3, 3, 3, 4));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing flow sequence */

    Z_TEST(parsing_flow_seq, "test parsing of flow sequences") {
        t_scope;
        yaml_data_t data;
        const yaml_data_t *subdata;
        const yaml_data_t *elem;

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "[]",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 3));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.seq->datas.len == 0);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "[ ~ ]",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 6));
        Z_ASSERT(data.seq->datas.len == 1);
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL,
                                         1, 3, 1, 4));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "[ ~, ]",
            "[ ~ ]"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 7));
        Z_ASSERT(data.seq->datas.len == 1);
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL,
                                         1, 3, 1, 4));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "[1 ,a:\n"
            "2,c d ,]",

            "[ 1, a: 2, c d ]"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 2, 9));
        Z_ASSERT(data.seq->datas.len == 3);

        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_UINT,
                                         1, 2, 1, 3));
        Z_ASSERT_EQ(elem->scalar.u, 1UL);

        elem = &data.seq->datas.tab[1];
        Z_HELPER_RUN(z_check_yaml_data(elem, YAML_DATA_OBJ, 1, 5, 2, 2));
        Z_ASSERT_EQ(elem->obj->fields.len, 1);
        Z_ASSERT_LSTREQUAL(elem->obj->fields.tab[0].key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->obj->fields.tab[0].key_span,
                                       1, 5, 1, 6));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->obj->fields.tab[0].data,
                                         YAML_SCALAR_UINT, 2, 1, 2, 2));
        Z_ASSERT_EQ(elem->obj->fields.tab[0].data.scalar.u, 2UL);

        elem = &data.seq->datas.tab[2];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_STRING,
                                         2, 3, 2, 6));
        Z_ASSERT_LSTREQUAL(elem->scalar.s, LSTR("c d"));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "- [ ~,\n"
            " [[ true, [ - 2 ] ]\n"
            "   ] , a:  [  -2] ,\n"
            "]",

            "- [ ~, [ [ true, [ \"- 2\" ] ] ], a: [ -2 ] ]"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 4, 2));
        Z_ASSERT(data.seq->datas.len == 1);
        data = data.seq->datas.tab[0];

        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 3, 4, 2));
        Z_ASSERT(data.seq->datas.len == 3);
        /* first elem: ~ */
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL, 1, 5, 1, 6));
        /* second elem: [[ true, [-2]] */
        elem = &data.seq->datas.tab[1];
        Z_HELPER_RUN(z_check_yaml_data(elem, YAML_DATA_SEQ, 2, 2, 3, 5));
        Z_ASSERT(elem->seq->datas.len == 1);

        /* [ true, [-2] ] */
        subdata = &elem->seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_data(subdata, YAML_DATA_SEQ, 2, 3, 2, 20));
        Z_ASSERT(subdata->seq->datas.len == 2);
        elem = &subdata->seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_BOOL, 2, 5, 2, 9));
        elem = &subdata->seq->datas.tab[1];
        Z_HELPER_RUN(z_check_yaml_data(elem, YAML_DATA_SEQ, 2, 11, 2, 18));
        Z_ASSERT_EQ(elem->seq->datas.len, 1);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->seq->datas.tab[0],
                                         YAML_SCALAR_STRING, 2, 13, 2, 16));
        Z_ASSERT_LSTREQUAL(elem->seq->datas.tab[0].scalar.s, LSTR("- 2"));

        /* third elem: a: [-2] */
        elem = &data.seq->datas.tab[2];
        Z_HELPER_RUN(z_check_yaml_data(elem, YAML_DATA_OBJ, 3, 8, 3, 18));
        Z_ASSERT_EQ(elem->obj->fields.len, 1);
        /* [-2] */
        Z_ASSERT_LSTREQUAL(elem->obj->fields.tab[0].key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->obj->fields.tab[0].key_span,
                                       3, 8, 3, 9));
        subdata = &elem->obj->fields.tab[0].data;
        Z_HELPER_RUN(z_check_yaml_data(subdata, YAML_DATA_SEQ, 3, 12, 3, 18));
        Z_ASSERT_EQ(subdata->seq->datas.len, 1);
        Z_HELPER_RUN(z_check_yaml_scalar(&subdata->seq->datas.tab[0],
                                         YAML_SCALAR_INT, 3, 15, 3, 17));
        Z_ASSERT_EQ(subdata->seq->datas.tab[0].scalar.i, -2L);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing flow object */

    Z_TEST(parsing_flow_obj, "test parsing of flow objects") {
        t_scope;
        yaml_data_t data;
        const yaml_key_data_t *elem;

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "{}",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 3));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.obj->fields.len == 0);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "{ a: ~ }",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 9));
        Z_ASSERT(data.obj->fields.len == 1);
        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 3, 1, 4));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_NULL,
                                         1, 6, 1, 7));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "{ a: foo, }",
            "{ a: foo }"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 12));
        Z_ASSERT(data.obj->fields.len == 1);
        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 3, 1, 4));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_STRING,
                                         1, 6, 1, 9));
        Z_ASSERT_LSTREQUAL(elem->data.scalar.s, LSTR("foo"));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "{ a: ~ ,b:\n"
            "2,}",

            "{ a: ~, b: 2 }"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 2, 4));
        Z_ASSERT(data.obj->fields.len == 2);
        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 3, 1, 4));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_NULL,
                                         1, 6, 1, 7));
        elem = &data.obj->fields.tab[1];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("b"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 9, 1, 10));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_UINT,
                                         2, 1, 2, 2));
        Z_ASSERT_EQ(elem->data.scalar.u, 2UL);

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "- { a: [true,\n"
            "   false,]\n"
            "     , b: f   \n"
            "  ,\n"
            "    z: { y: 1  }}\n"
            "- ~",

            "- { a: [ true, false ], b: f, z: { y: 1 } }\n"
            "- ~"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 6, 4));
        Z_ASSERT(data.seq->datas.len == 2);
        Z_HELPER_RUN(z_check_yaml_scalar(&data.seq->datas.tab[1],
                                         YAML_SCALAR_NULL, 6, 3, 6, 4));

        data = data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 3, 5, 18));
        Z_ASSERT(data.seq->datas.len == 3);

        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 5, 1, 6));
        Z_HELPER_RUN(z_check_yaml_data(&elem->data, YAML_DATA_SEQ,
                                         1, 8, 2, 11));
        Z_ASSERT_EQ(elem->data.seq->datas.len,2);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data.seq->datas.tab[0],
                                         YAML_SCALAR_BOOL, 1, 9, 1, 13));
        Z_ASSERT(elem->data.seq->datas.tab[0].scalar.b);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data.seq->datas.tab[1],
                                         YAML_SCALAR_BOOL, 2, 4, 2, 9));
        Z_ASSERT(!elem->data.seq->datas.tab[1].scalar.b);

        elem = &data.obj->fields.tab[1];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("b"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 3, 8, 3, 9));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_STRING,
                                         3, 11, 3, 12));
        Z_ASSERT_LSTREQUAL(elem->data.scalar.s, LSTR("f"));

        elem = &data.obj->fields.tab[2];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("z"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 5, 5, 5, 6));
        Z_HELPER_RUN(z_check_yaml_data(&elem->data, YAML_DATA_OBJ,
                                       5, 8, 5, 17));
        Z_ASSERT_EQ(elem->data.obj->fields.len, 1);

        /* { y: 1 } */
        elem = &elem->data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("y"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 5, 10, 5, 11));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_UINT,
                                         5, 13, 5, 14));
        Z_ASSERT_EQ(elem->data.scalar.u, 1UL);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Packing simple data */

    Z_TEST(pack, "test packing of simple data") {
        t_scope;
        yaml_data_t scalar;
        yaml_data_t data;
        yaml_data_t data2;

        /* empty obj */
        t_yaml_data_new_obj(&data, 0);
        Z_HELPER_RUN(z_check_yaml_pack(&data, NULL, "{}"));

        /* empty obj in seq */
        t_yaml_data_new_seq(&data2, 1);
        yaml_seq_add_data(&data2, data);
        Z_HELPER_RUN(z_check_yaml_pack(&data2, NULL, "- {}"));

        /* empty seq */
        t_yaml_data_new_seq(&data, 0);
        Z_HELPER_RUN(z_check_yaml_pack(&data, NULL, "[]"));

        /* empty seq in obj */
        t_yaml_data_new_obj(&data2, 1);
        yaml_obj_add_field(&data2, LSTR("a"), data);
        Z_HELPER_RUN(z_check_yaml_pack(&data2, NULL, "a: []"));

        /* seq in seq */
        t_yaml_data_new_seq(&data, 1);
        yaml_data_set_bool(&scalar, true);
        yaml_seq_add_data(&data, scalar);
        t_yaml_data_new_seq(&data2, 1);
        yaml_seq_add_data(&data2, data);
        Z_HELPER_RUN(z_check_yaml_pack(&data2, NULL, "- - true"));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Packing flags */

    Z_TEST(pack_flags, "test packing flags") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_write_yaml_file("not_recreated.yml", "1"));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "key: !include:not_recreated.yml",

            "key: 1"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("flags"));
        Z_HELPER_RUN(z_pack_yaml_file("flags/root.yml", &data, &pres,
                                      YAML_PACK_NO_SUBFILES));
        Z_HELPER_RUN(z_check_file("flags/root.yml",
            "key: !include:not_recreated.yml\n"
        ));
        Z_HELPER_RUN(z_check_file_do_not_exist("flags/not_recreated.yml"));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Comment presentation */

#define CHECK_PREFIX_COMMENTS(pres, path, ...)                               \
    do {                                                                     \
        lstr_t comments[] = { __VA_ARGS__ };                                 \
                                                                             \
        Z_HELPER_RUN(z_check_prefix_comments((pres), (path), comments,       \
                                             countof(comments)));            \
    } while (0)

    Z_TEST(comment_presentation, "test saving of comments in presentation") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t doc_pres;
        const yaml_presentation_t *pres;

        /* comment on a scalar => not saved */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &doc_pres, NULL, 0,
            "# my scalar\n"
            "3",
            NULL
        ));
        pres = t_yaml_doc_pres_to_map(&doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR("!"), LSTR("my scalar"));

        /* comment on a key => path is key */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &doc_pres, NULL, 0,
            "a: 3 #ticket is #42  ",

            "a: 3 # ticket is #42\n"
        ));
        pres = t_yaml_doc_pres_to_map(&doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".a!"),
                                            LSTR("ticket is #42")));

        /* comment on a list => path is index */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &doc_pres, NULL, 0,
            "# prefix comment\n"
            "- 1 # first\n"
            "- # item\n"
            "  2 # second\n",

            NULL
        ));
        pres = t_yaml_doc_pres_to_map(&doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(4, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR("!"), LSTR("prefix comment"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR("[0]!"),
                                            LSTR("first")));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR("[1]"),
                                            LSTR("item")));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR("[1]!"),
                                            LSTR("second")));

        /* prefix comment with multiple lines */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &doc_pres, NULL, 0,
            "key:\n"
            "   # first line\n"
            " # and second\n"
            "     # bad indent is ok\n"
            "  a: # inline a\n"
            " # prefix scalar\n"
            "     ~ # inline scalar\n"
            "    # this is lost",

            "key:\n"
            "  # first line\n"
            "  # and second\n"
            "  # bad indent is ok\n"
            "  a: # inline a\n"
            "    # prefix scalar\n"
            "    ~ # inline scalar\n"
        ));
        pres = t_yaml_doc_pres_to_map(&doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(3, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key!"),
                              LSTR("first line"),
                              LSTR("and second"),
                              LSTR("bad indent is ok"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key.a"),
                                            LSTR("inline a")));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key.a!"), LSTR("prefix scalar"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key.a!"),
                                            LSTR("inline scalar")));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &doc_pres, NULL, 0,
            "# prefix key\n"
            "key: # inline key\n"
            "# prefix [0]\n"
            "- # inline [0]\n"
            " # prefix key2\n"
            " key2: ~ # inline key2\n",

            "# prefix key\n"
            "key: # inline key\n"
            "  # prefix [0]\n"
            "  - # inline [0]\n"
            "    # prefix key2\n"
            "    key2: ~ # inline key2\n"
        ));
        pres = t_yaml_doc_pres_to_map(&doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(6, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR("!"),
                              LSTR("prefix key"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key"),
                                            LSTR("inline key")));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key!"),
                              LSTR("prefix [0]"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key[0]"),
                                            LSTR("inline [0]")));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key[0]!"),
                              LSTR("prefix key2"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key[0].key2!"),
                                            LSTR("inline key2")));

        /* prefix comment must be written before tag */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "# prefix key\n"
            "!toto 3",

            NULL
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "# a\n"
            "a: # b\n"
            "  !foo b",

            NULL
        ));

        /* inline comments with flow data */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "- # prefix\n"
            "  1 # inline\n",
            NULL
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, NULL, NULL, 0,
            "- # prefix\n"
            "  [ 1 ] # inline\n"
            "- # prefix2\n"
            "  { a: b } # inline2\n",

            NULL
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Empty lines presentation */

    Z_TEST(empty_lines_presentation, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, NULL, 0,
            "\n"
            "  # comment\n"
            "\n"
            "a: ~",

            /* First empty lines, then prefix comment. */
            "\n"
            "\n"
            "# comment\n"
            "a: ~"
        ));

        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, NULL, 0,
            "# 1\n"
            "a: # 2\n"
            "\n"
            "  - b: 3\n"
            "\n"
            "    c: 4\n"
            "\n"
            "  -\n"
            "\n"
            "    # foo\n"
            "    2\n"
            "  - 3",

            NULL
        ));

        /* max two new lines */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, NULL, 0,
            "\n\n\n\n"
            "a: 4\n"
            "\n\n\n"
            "b: 3\n"
            "\n"
            "# comment\n"
            "\n"
            "c: 2\n"
            "\n"
            "d: 1\n"
            "e: 0",

            "\n\n"
            "a: 4\n"
            "\n\n"
            "b: 3\n"
            "\n\n"
            "# comment\n"
            "c: 2\n"
            "\n"
            "d: 1\n"
            "e: 0"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Flow presentation */

    Z_TEST(flow_presentation, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        const char *expected;

        /* Make sure that flow syntax is reverted if a tag is added in the
         * data. */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, NULL, 0,
            "a: { k: d }\n"
            "b: [ 1, 2 ]",

            NULL
        ));
        data.obj->fields.tab[0].data.obj->fields.tab[0].data.tag
            = LSTR("tag1");
        data.obj->fields.tab[1].data.seq->datas.tab[1].tag = LSTR("tag2");

        expected =
            "a:\n"
            "  k: !tag1 d\n"
            "b:\n"
            "  - 1\n"
            "  - !tag2 2";
        Z_HELPER_RUN(z_check_yaml_pack(&data, NULL, expected));
        Z_HELPER_RUN(z_check_yaml_pack(&data, &pres, expected));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Variable */

    Z_TEST(variable, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *inner;
        const char *root;
        const char *child;
        const char *grandchild;

        /* test replacement of variables */
        inner =
            "- a:\n"
            "    - 1\n"
            "    - $(a)\n"
            "- b:\n"
            "    a: $(a)\n"
            "    b: $(a-b)\n";
        Z_HELPER_RUN(z_write_yaml_file("inner.yml", inner));
        root =
            "!include:inner.yml\n"
            "variables:\n"
            "  a: 3\n"
            "  a-b:\n"
            "    - 1\n"
            "    - 2\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "- a:\n"
            "    - 1\n"
            "    - 3\n"
            "- b:\n"
            "    a: 3\n"
            "    b:\n"
            "      - 1\n"
            "      - 2"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("variables_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("variables_1/root.yml", root));
        Z_HELPER_RUN(z_check_file("variables_1/inner.yml", inner));
        yaml_parse_delete(&env);

        /* test combination of variables settings + override */
        grandchild =
            "var: $(var)\n"
            "var2: $(var_2)\n"
            "a: 0\n"
            "b: 1\n";
        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml", grandchild));
        child =
            "key: !include:grandchild.yml\n"
            "  variables:\n"
            "    var: 3\n"
            /* TODO: we should be able to say "b: $var2" here, but it does
             * not work currently. See FIXME in yaml_variable_t. */
            "  b: 5\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));
        root =
            "!include:child.yml\n"
            "variables:\n"
            "  var_2: 4\n"
            "key:\n"
            "  a: a\n"
            "  var: 1\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "key:\n"
            "  var: 1\n"
            "  var2: 4\n"
            "  a: a\n"
            "  b: 5"
        ));

        Z_HELPER_RUN(z_pack_yaml_file("variables_2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("variables_2/root.yml", root));
        Z_HELPER_RUN(z_check_file("variables_2/child.yml", child));
        Z_HELPER_RUN(z_check_file("variables_2/grandchild.yml", grandchild));

        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Variable in scalar */

    Z_TEST(variable_in_scalar, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *inner;
        const char *root;

        /* modify a template through a direct include of the scalar */
        inner = "$(a) $(b)\n";
        Z_HELPER_RUN(z_write_yaml_file("inner.yml", inner));
        root =
            "!include:inner.yml\n"
            "variables:\n"
            "  a: pi\n"
            "  b: ka\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "pi ka"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_scalar_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("var_scalar_1/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_scalar_1/inner.yml", inner));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Variable used multiple times */

    Z_TEST(variable_multiple, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *root;
        const char *child;
        const char *grandchild;

        /* test replacement of variables */
        grandchild =
            "key: $(var)\n"
            "key2: var2 is <$(var2)>\n";
        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml", grandchild));
        child =
            "inc: !include:grandchild.yml\n"
            "  variables:\n"
            "    var: 1\n"
            "other: $(var)\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));
        root =
            "all: !include:child.yml\n"
            "  variables:\n"
            "    var: 2\n"
            "    var2: 3\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "all:\n"
            "  inc:\n"
            "    key: 1\n"
            "    key2: var2 is <3>\n"
            "  other: 2"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_mul/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("var_mul/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_mul/child.yml", child));
        Z_HELPER_RUN(z_check_file("var_mul/grandchild.yml", grandchild));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Variable scalars used multiple times */

    Z_TEST(variable_multiple_scalar, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *root;
        const char *inner;

        /* test use of non-string scalars in multiple contexts */
        inner =
            "ur: $(u)\n"
            "us: <$(u)>\n"
            "ir: $(i)\n"
            "is: <$(i)>\n"
            "nr: $(n)\n"
            "ns: <$(n)>\n"
            "br: $(b)\n"
            "bs: <$(b)>\n"
            "sr: $(s)\n"
            "ss: <$(s)>\n"
            "dr: $(d)\n"
            "ds: <$(d)>\n"
            "d2r: $(d2)\n"
            "d2s: <$(d2)>\n";
        Z_HELPER_RUN(z_write_yaml_file("inner.yml", inner));
        root =
            "inc: !include:inner.yml\n"
            "  variables:\n"
            "    u: 42\n"
            "    i: -23\n"
            "    n: ~\n"
            "    b: false\n"
            "    s: \"2\"\n"
            "    d: 12.73\n"
            "    d2: .NaN\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "inc:\n"
            "  ur: 42\n"
            "  us: <42>\n"
            "  ir: -23\n"
            "  is: <-23>\n"
            "  nr: ~\n"
            "  ns: <~>\n"
            "  br: false\n"
            "  bs: <false>\n"
            "  sr: 2\n"
            "  ss: <2>\n"
            "  dr: 12.73\n"
            "  ds: <12.73>\n"
            "  d2r: .NaN\n"
            "  d2s: <.NaN>"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_mul_s/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("var_mul_s/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_mul_s/inner.yml", inner));
        yaml_parse_delete(&env);

        /* using non canonical representations won't properly match the
         * variables. */
        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "nr: $(n)\n"
            "ns: <$(n)>\n"
            "br: $(b)\n"
            "bs: <$(b)>\n"
            "dr: $(d)\n"
            "ds: <$(d)>\n"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "inc: !include:inner.yml\n"
            "  variables:\n"
            "    n: null\n"
            "    b: tRuE\n"
            "    d: 10.2e-3\n",

            "inc:\n"
            "  nr: ~\n"
            "  ns: <null>\n"
            "  br: true\n"
            "  bs: <tRuE>\n"
            "  dr: 0.0102\n"
            "  ds: <10.2e-3>"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_mul_s2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("var_mul_s2/root.yml",
            "inc: !include:inner.yml\n"
            "  variables:\n"
            "    n: ~\n"
            "    n~1: \"null\"\n"
            "    b: true\n"
            "    b~1: tRuE\n"
            "    d: 0.0102\n"
            "    d~1: 10.2e-3\n"
        ));
        Z_HELPER_RUN(z_check_file("var_mul_s2/inner.yml",
            "nr: $(n)\n"
            "ns: <$(n~1)>\n"
            "br: $(b)\n"
            "bs: <$(b~1)>\n"
            "dr: $(d)\n"
            "ds: <$(d~1)>\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Variable in string */

    Z_TEST(variable_in_string, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *root;
        const char *inner;
        const char *child;
        const char *grandchild;

        /* test replacement of variables */
        /* TODO: handle variables in flow context? */
        inner =
            "- \"foo var is: `$(foo)`\"\n"
            "- <$(foo)> unquoted also works </$(foo)>\n"
            "- a: $(foo)\n"
            "  b: $(foo)$(foo)a$(qux)-$(qux)\n";
        Z_HELPER_RUN(z_write_yaml_file("inner.yml", inner));

        root =
            "!include:inner.yml\n"
            "variables:\n"
            "  foo: bar\n"
            "  qux: c\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "- \"foo var is: `bar`\"\n"
            "- <bar> unquoted also works </bar>\n"
            "- a: bar\n"
            "  b: barbarac-c"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_str/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("var_str/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_str/inner.yml", inner));
        yaml_parse_delete(&env);

        /* test partial modification of templated string */
        grandchild = "addr: \"$(host):$(port)\"\n";
        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml", grandchild));

        child =
            "!include:grandchild.yml\n"
            "variables:\n"
            "  port: 80\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));

        root =
            "!include:child.yml\n"
            "variables:\n"
            "  host: website.org\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "addr: \"website.org:80\""
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_str2/root.yml", &data, &pres, 0));
        Z_HELPER_RUN(z_check_file("var_str2/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_str2/child.yml", child));
        Z_HELPER_RUN(z_check_file("var_str2/grandchild.yml", grandchild));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Variable errors */

    Z_TEST(variable_errors, "") {
        t_scope;

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: $(a)\n"
            "s: \"<$(s)>\"\n"
            "t: <$(t)>"
        ));

        /* wrong variable settings */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  variables:",

            "input.yml:3:1: wrong type of data, "
            "variable settings must be an object"
        ));
        /* TODO: improve span for empty null scalar */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  variables:\n"
            "key2: 3",

            "input.yml:3:1: wrong type of data, "
            "variable settings must be an object\n"
            "key2: 3\n"
            "^ starting here"
        ));

        /* unknown variable being set */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  variables:\n"
            "    b: foo",

            "input.yml:3:5: invalid key, unknown variable\n"
            "    b: foo\n"
            "    ^"
        ));

        /* string-variable being set with wrong type */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  variables:\n"
            "    s: [ 1, 2 ]",

            "input.yml:3:8: wrong type of data, "
            "this variable can only be set with a scalar\n"
            "    s: [ 1, 2 ]\n"
            "       ^^^^^^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  variables:\n"
            "    t: [ 1, 2 ]",

            "input.yml:3:8: wrong type of data, "
            "this variable can only be set with a scalar\n"
            "    t: [ 1, 2 ]\n"
            "       ^^^^^^^^"
        ));

        /* cannot use variables in variable settings or overrides. */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  variables:\n"
            "    a: $(t)",

            "input.yml:3:8: use of variables is forbidden, "
            "cannot use variables in this context\n"
            "    a: $(t)\n"
            "       ^^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "key: !include:inner.yml\n"
            "  t: <$(a)>",

            "input.yml:2:6: use of variables is forbidden, "
            "cannot use variables in this context\n"
            "  t: <$(a)>\n"
            "     ^^^^^^"
        ));

        /* invalid variable name */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: $()",

            "<string>:1:4: invalid variable, "
            "the string contains a variable with an invalid name\n"
            "a: $()\n"
            "   ^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: $(5a)",

            "<string>:1:4: invalid variable, "
            "the string contains a variable with an invalid name\n"
            "a: $(5a)\n"
            "   ^^^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: \"a \\$(b) $(b) $(-)\"",

            "<string>:1:4: invalid variable, "
            "the string contains a variable with an invalid name\n"
            "a: \"a \\$(b) $(b) $(-)\"\n"
            "   ^^^^^^^^^^^^^^^^^^^"
        ));

        /* cannot use variables in flow context */
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: [ 1, 2, $(b) ]",

            "<string>:1:12: use of variables is forbidden, "
            "cannot use variables in this context\n"
            "a: [ 1, 2, $(b) ]\n"
            "           ^^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(0,
            "a: { a: 1, b: $(b) }",

            "<string>:1:15: use of variables is forbidden, "
            "cannot use variables in this context\n"
            "a: { a: 1, b: $(b) }\n"
            "              ^^^^"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Raw variable modification handling */

    Z_TEST(raw_variable_modif, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: $(var)"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:inner.yml\n"
            "variables:\n"
            "  var:\n"
            "    b: 1\n"
            "    c: 2",

            "a:\n"
            "  b: 1\n"
            "  c: 2"
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("vm_raw_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vm_raw_1/root.yml",
            "!include:inner.yml\n"
            "variables:\n"
            "  var:\n"
            "    b: 1\n"
            "    c: 2\n"
        ));
        Z_HELPER_RUN(z_check_file("vm_raw_1/inner.yml",
            "a: $(var)\n"
        ));

        /* any change to the AST is properly handled */
        yaml_data_set_null(&data.obj->fields.tab[0].data);
        Z_HELPER_RUN(z_pack_yaml_file("vm_raw_2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vm_raw_2/root.yml",
            "!include:inner.yml\n"
            "variables:\n"
            "  var: ~\n"
        ));
        Z_HELPER_RUN(z_check_file("vm_raw_2/inner.yml",
            "a: $(var)\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ variable in string modification handling */

    Z_TEST(variable_in_string_modif, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: <$(var)>\n"
            "b: \"<\\$(a) $b $(var) \\$(c>\""
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:inner.yml\n"
            "variables:\n"
            "  var: yare",

            "a: <yare>\n"
            "b: \"<\\$(a) $b yare \\$(c>\""
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_test_var_in_str_change(&data, &pres,
            "!include:inner.yml\n"
            "variables:\n"
            "  var: yare\n",

            "a: <$(var)>\n"
            "b: \"<\\$(a) $b $(var) \\$(c>\"\n"
        ));

        /* changing the value while still matching the template will work */
        data.obj->fields.tab[0].data.scalar.s = LSTR("<daze>");
        data.obj->fields.tab[1].data.scalar.s = LSTR("<$(a) $b daze $(c>");
        Z_HELPER_RUN(z_test_var_in_str_change(&data, &pres,
            "!include:inner.yml\n"
            "variables:\n"
            "  var: daze\n",

            "a: <$(var)>\n"
            "b: \"<\\$(a) $b $(var) \\$(c>\"\n"
        ));

        /* changing the value and not matching the template will lose the
         * var */
        data.obj->fields.tab[0].data.scalar.s = LSTR("<daze");
        data.obj->fields.tab[1].data.scalar.s = LSTR("<$(a) b daze $(c>");
        Z_HELPER_RUN(z_test_var_in_str_change(&data, &pres,
            "!include:inner.yml {}\n",

            "a: <daze\n"
            "b: \"<\\$(a) b daze \\$(c>\"\n"
        ));

        data.obj->fields.tab[0].data.scalar.s = LSTR("");
        data.obj->fields.tab[1].data.scalar.s = LSTR("<a b d c>");
        Z_HELPER_RUN(z_test_var_in_str_change(&data, &pres,
            "!include:inner.yml {}\n",

            "a: \"\"\n"
            "b: <a b d c>\n"
        ));

        data.obj->fields.tab[0].data.scalar.s = LSTR("d");
        data.obj->fields.tab[1].data.scalar.s = LSTR("d");
        Z_HELPER_RUN(z_test_var_in_str_change(&data, &pres,
            "!include:inner.yml {}\n",

            "a: d\n"
            "b: d\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ multiple variables modification handling */

    Z_TEST(variable_multiple_modif, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "a: $(par) $(ker)"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:inner.yml\n"
            "variables:\n"
            "  par: \" he \"\n"
            "  ker: roes",

            "a: \" he  roes\""
        ));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("vm_mul_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vm_mul_1/root.yml",
            "!include:inner.yml\n"
            "variables:\n"
            "  par: \" he \"\n"
            "  ker: roes\n"
        ));
        Z_HELPER_RUN(z_check_file("vm_mul_1/inner.yml",
            "a: $(par) $(ker)\n"
        ));

        /* changing the value will always lose the variables */
        data.obj->fields.tab[0].data.scalar.s = LSTR("her oes");

        Z_HELPER_RUN(z_pack_yaml_file("vm_mul_2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vm_mul_2/root.yml",
            "!include:inner.yml {}\n"
        ));
        Z_HELPER_RUN(z_check_file("vm_mul_2/inner.yml",
            "a: her oes\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Raw variable conflict handling */

    Z_TEST(raw_variable_conflict, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "- $(var)\n"
            "- $(var)\n"
            "- $(var)\n"
            "- $(var)\n"
            "- $(var)\n"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:inner.yml\n"
            "variables:\n"
            "  var: 1\n",

            "- 1\n"
            "- 1\n"
            "- 1\n"
            "- 1\n"
            "- 1"
        ));

        /* modify AST to get: [ 1, 2, 2, 1, ~ ] */
        yaml_data_set_uint(&data.seq->datas.tab[1], 2);
        yaml_data_set_uint(&data.seq->datas.tab[2], 2);
        yaml_data_set_null(&data.seq->datas.tab[4]);

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("vc_raw_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vc_raw_1/root.yml",
            "!include:inner.yml\n"
            "variables:\n"
            "  var: 1\n"
            "  var~1: 2\n"
            "  var~2: ~\n"
        ));
        Z_HELPER_RUN(z_check_file("vc_raw_1/inner.yml",
            "- $(var)\n"
            "- $(var~1)\n"
            "- $(var~1)\n"
            "- $(var)\n"
            "- $(var~2)\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ variable in string conflict handling */

    Z_TEST(variable_in_string_conflict, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "- $(var)\n"
            "- \" $(var) \"\n"
            "- <$(var)>\n"
            "- $(var) $(var) $(var)\n"
            "- <$(var)>\n"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:inner.yml\n"
            "variables:\n"
            "  var: ga\n",

            "- ga\n"
            "- \" ga \"\n"
            "- <ga>\n"
            "- ga ga ga\n"
            "- <ga>"
        ));

        /* modify AST to get: [ bu, " ga ", <zo>, ga meu bu, ga zo, <bu> ] */
        yaml_data_set_string(&data.seq->datas.tab[0], LSTR("bu"));
        yaml_data_set_string(&data.seq->datas.tab[2], LSTR("<zo>"));
        yaml_data_set_string(&data.seq->datas.tab[3], LSTR("ga meu bu"));
        yaml_data_set_string(&data.seq->datas.tab[4], LSTR("<bu>"));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("vc_str_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vc_str_1/root.yml",
            "!include:inner.yml\n"
            "variables:\n"
            /* First value deduced is taken as the true value. */
            "  var: bu\n"
            /* Other deduced values are added as conflicts */
            "  var~1: ga\n"
            "  var~2: zo\n"
        ));
        Z_HELPER_RUN(z_check_file("vc_str_1/inner.yml",
            "- $(var)\n"

            /* String did not change, but the value was already deduced to
             * a new string. */
            "- \" $(var~1) \"\n"

            /* Value changed, but template is simple, so new value is deduced.
             */
            "- <$(var~2)>\n"

            /* Loss of template due to use of multiple variables. */
            "- ga meu bu\n"

            /* Value changed, but match the deduced value, so template do not
             * change. */
            "- <$(var)>\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ multiple variables conflict handling */

    Z_TEST(variable_multiple_conflict, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "- $(foo)\n"
            "- $(bar)\n"
            "- $(foo) $(foo)\n"
            "- $(foo) $(bar)\n"
            "- $(foo) $(bar) $(foo)\n"
            "- $(bar) $(bar) $(foo)\n"
            "- $(foo) $(bar)\n"
        ));
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            "!include:inner.yml\n"
            "variables:\n"
            "  foo: ga\n"
            "  bar: bu\n",

            "- ga\n"
            "- bu\n"
            "- ga ga\n"
            "- ga bu\n"
            "- ga bu ga\n"
            "- bu bu ga\n"
            "- ga bu"
        ));

        /* force $foo and $bar to be deduced to a new string */
        yaml_data_set_string(&data.seq->datas.tab[0], LSTR("zo"));
        yaml_data_set_string(&data.seq->datas.tab[1], LSTR("meu"));
        yaml_data_set_string(&data.seq->datas.tab[6], LSTR("zo meu"));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("vc_mul_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("vc_mul_1/root.yml",
            "!include:inner.yml\n"
            "variables:\n"
            "  foo: zo\n"
            "  foo~1: ga\n"
            "  bar: meu\n"
            "  bar~1: bu\n"
        ));
        Z_HELPER_RUN(z_check_file("vc_mul_1/inner.yml",
            "- $(foo)\n"
            "- $(bar)\n"
            /* Value did not change, but names conflicted: this is properly
             * handled */
            "- $(foo~1) $(foo~1)\n"
            "- $(foo~1) $(bar~1)\n"
            "- $(foo~1) $(bar~1) $(foo~1)\n"
            "- $(bar~1) $(bar~1) $(foo~1)\n"
            /* Even though the value matches the new variables, the old
             * variable values do not apply, and the template uses multiple
             * variables, so it is lost */
            "- zo meu\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ escaped variables */

    Z_TEST(variable_escaped, "") {
        t_scope;
        yaml_data_t data;
        yaml__document_presentation__t pres;
        yaml_parse_t *env;
        const char *root;
        const char *inner;
        const char *child;
        const char *grandchild;

        /* Test how changing in the parsed AST the value introduced by a
         * variable is reflected when repacking */

        inner =
            /* Not quoting the string won't handle escaping */
            "- $(foo)\n"
            "- \\$(foo)\n"
            "- \\\\$(foo)\n"
            "- <\\$(foo)>\n"
            "- <$(foo) \\$(foo) \\$(bar)$(foo)>\n"
            "- $(foo)\\$(bar)$(bar)\\\\$(foo)\n"

            /* Quoting the string will handle escaping */
            "- \"$(foo)\"\n"
            "- \"\\$(foo)\"\n"
            "- \"\\\\$(foo)\"\n"
            "- \"<\\$(foo)>\"\n"
            "- \"<$(foo) \\$(foo) \\$(bar)$(foo)>\"\n"
            "- \"$(foo)\\$(bar)$(bar)\\\\$(foo)\"\n";

        Z_HELPER_RUN(z_write_yaml_file("inner.yml", inner));
        root =
            "!include:inner.yml\n"
            "variables:\n"
            "  foo: ga\n"
            "  bar: bu\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "- ga\n"
            "- \\ga\n"
            "- \\\\ga\n"
            "- <\\ga>\n"
            "- <ga \\ga \\buga>\n"
            "- ga\\bubu\\\\ga\n"
            "- ga\n"
            "- \"\\$(foo)\"\n"
            "- \\ga\n"
            "- \"<\\$(foo)>\"\n"
            "- \"<ga \\$(foo) \\$(bar)ga>\"\n"
            "- \"ga\\$(bar)bu\\\\ga\""
        ));

        Z_ASSERT_LSTREQUAL(data.seq->datas.tab[7].scalar.s,
                           LSTR("$(foo)"));
        Z_ASSERT_LSTREQUAL(data.seq->datas.tab[9].scalar.s,
                           LSTR("<$(foo)>"));
        Z_ASSERT_LSTREQUAL(data.seq->datas.tab[10].scalar.s,
                           LSTR("<ga $(foo) $(bar)ga>"));
        Z_ASSERT_LSTREQUAL(data.seq->datas.tab[11].scalar.s,
                           LSTR("ga$(bar)bu\\ga"));

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_esc_1/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("var_esc_1/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_esc_1/inner.yml", inner));
        yaml_parse_delete(&env);

        /* Test partial substitutions with escaped characters, + go above
         * 8 '$' characters to test bitmap alloc */

        grandchild =
            "- \"$(a) \\$(a) $(b) \\$(b) \\$(c) $(c) $(d) \\$(d) "
                "$(e) $(e) \\$(e) $(f) "
                "\\$(f1) \\$(f2) \\$(f3) \\$(f4) \\$(f5)\"\n"
            "- $(g)\n";
        /* bitmap: 1, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0.
         * ie: 0x65, 0x0B.
         * the last '0' is not part of the bitmap, this allows testing proper
         * OOB tests on the bitmap.
         */
        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml", grandchild));

        child =
            "!include:grandchild.yml\n"
            "variables:\n"
            "  f: y\n"
            "  a: \"a\\$(\\$(\\$(a))\"\n"
            "  d: \"D:\"\n"
            "  b: b\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));

        root =
            "!include:child.yml\n"
            "variables:\n"
            "  c: \"c\\$(e)\\$(e)\"\n"
            "  e: e k s\n"
            "  g:\n"
            "    - ~\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "- \"a\\$(\\$(\\$(a)) \\$(a) b \\$(b) \\$(c) c\\$(e)\\$(e) "
                "D: \\$(d) e k s e k s \\$(e) y "
                "\\$(f1) \\$(f2) \\$(f3) \\$(f4) \\$(f5)\"\n"
            "- - ~"
        ));

        Z_ASSERT_LSTREQUAL(
            data.seq->datas.tab[0].scalar.s,
            LSTR("a$($($(a)) $(a) b $(b) $(c) c$(e)$(e) "
                 "D: $(d) e k s e k s $(e) y "
                 "$(f1) $(f2) $(f3) $(f4) $(f5)")
        );

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_esc_2/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("var_esc_2/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_esc_2/child.yml", child));
        Z_HELPER_RUN(z_check_file("var_esc_2/grandchild.yml", grandchild));
        yaml_parse_delete(&env);

        /* parse only child, look at ast, and repack */
        Z_HELPER_RUN(t_z_yaml_test_parse_success(
            &data, &pres, &env, YAML_PARSE_ALLOW_UNBOUND_VARIABLES,
            child,

            /* repacking raw, without includes, will lose the variables,
             * and thus the string is packed as is, losing variable data */
            "- \"a\\$(\\$(\\$(a)) \\$(a) b \\$(b) \\$(c) \\$(c) "
                "D: \\$(d) \\$(e) \\$(e) \\$(e) y "
                "\\$(f1) \\$(f2) \\$(f3) \\$(f4) \\$(f5)\"\n"
            "- \"\\$(g)\""
        ));

        /* But repacking with files (and with the right flag) will work fine.
         */
        Z_HELPER_RUN(z_pack_yaml_file("var_esc_3/child.yml", &data, &pres,
                                      YAML_PACK_ALLOW_UNBOUND_VARIABLES));
        Z_HELPER_RUN(z_check_file("var_esc_3/child.yml", child));
        Z_HELPER_RUN(z_check_file("var_esc_3/grandchild.yml", grandchild));
        yaml_parse_delete(&env);

        /* Test partial substitutions, in a string with no escaped characters,
         * but some are introduced by substitutions */

        grandchild =
            "- $(a) $(b) $(c) $(a) $(b) $(c)\n";
        Z_HELPER_RUN(z_write_yaml_file("grandchild.yml", grandchild));

        child =
            "!include:grandchild.yml\n"
            "variables:\n"
            "  b: \"<\\$(b)>\"\n"
            "  a: \"<\\$(a)>\"\n";
        Z_HELPER_RUN(z_write_yaml_file("child.yml", child));

        root =
            "!include:child.yml\n"
            "variables:\n"
            "  c: \"<\\$(c)>\"\n";
        Z_HELPER_RUN(t_z_yaml_test_parse_success(&data, &pres, &env, 0,
            root,

            "- \"<\\$(a)> <\\$(b)> <\\$(c)> <\\$(a)> <\\$(b)> <\\$(c)>\""
        ));

        Z_ASSERT_LSTREQUAL(
            data.seq->datas.tab[0].scalar.s,
            LSTR("<$(a)> <$(b)> <$(c)> <$(a)> <$(b)> <$(c)>")
        );

        /* pack into files, to test repacking of variables */
        Z_HELPER_RUN(z_pack_yaml_file("var_esc_4/root.yml", &data, &pres,
                                      0));
        Z_HELPER_RUN(z_check_file("var_esc_4/root.yml", root));
        Z_HELPER_RUN(z_check_file("var_esc_4/child.yml", child));
        Z_HELPER_RUN(z_check_file("var_esc_4/grandchild.yml", grandchild));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ deduce_var_in_string */

    Z_TEST(deduce_var_in_string, "") {
        t_scope;
        qv_t(u8) bitmap;

        t_qv_init(&bitmap, 1);

#define TST(tpl, value, exp_var_name, exp_var_value)                         \
        do {                                                                 \
            lstr_t var_name;                                                 \
            lstr_t var_value;                                                \
                                                                             \
            Z_ASSERT_N(deduce_var_in_string(LSTR(tpl), LSTR(value), &bitmap, \
                                            &var_name, &var_value),          \
                       "tpl: %s, var: %s failed", (tpl), (value));           \
            Z_ASSERT_LSTREQUAL(var_name, LSTR(exp_var_name));                \
            Z_ASSERT_LSTREQUAL(var_value, LSTR(exp_var_value));              \
        } while(0)

#define TST_ERR(tpl, value)                                                  \
        do {                                                                 \
            lstr_t var_name;                                                 \
            lstr_t var_value;                                                \
                                                                             \
            Z_ASSERT_NEG(deduce_var_in_string(LSTR(tpl), LSTR(value),        \
                                              &bitmap, &var_name,            \
                                              &var_value));                  \
        } while(0)

        TST_ERR("name", "foo");
        TST_ERR("$", "foo");
        TST_ERR("$()", "foo");
        TST_ERR("$(_", "_");
        TST("$(name)", "foo", "name", "foo");
        TST("$(name)", "", "name", "");
        TST("_$(name)_", "_foo_", "name", "foo");
        TST("_$(name)_", "__", "name", "");
        TST_ERR("_$(name)_", "_");
        TST_ERR("_$(name)_", "_foo_a");

        TST("_$(name)", "_foo_", "name", "foo_");
        TST("$(name)_", "_foo_", "name", "_foo");

        qv_append(&bitmap, 0x1);
        TST("$(a) $(b) $(c)", "ga $(b) $(c)", "a", "ga");
        TST_ERR("$(a) $(b) $(c)", "$(a) ga b");
        TST_ERR("$(a) $(b) $(c)", "a ga $(b)");
        TST_ERR("$(a) $(b) $(c)", "$(a) $(b)");
        TST_ERR("$(a) $(b) $(c)", "$(a) ga $(c)");
        TST_ERR("$(a) $(b) $(c)", "$(a) $(b) ga");

        bitmap.tab[0] = 0x2;
        TST("$(a) $(b) $(c)", "$(a) ga $(c)", "b", "ga");
        TST_ERR("$(a) $(b) $(c)", "$(a) ga b");
        TST_ERR("$(a) $(b) $(c)", "a ga $(b)");
        TST_ERR("$(a) $(b) $(c)", "$(a) $(b)");
        TST_ERR("$(a) $(b) $(c)", "ga $(b) $(c)");
        TST_ERR("$(a) $(b) $(c)", "$(a) $(b) ga");

        bitmap.tab[0] = 0x4;
        TST("$(a) $(b) $(c)", "$(a) $(b) ga", "c", "ga");
        TST_ERR("$(a) $(b) $(c)", "$(a) ga b");
        TST_ERR("$(a) $(b) $(c)", "a ga $(b)");
        TST_ERR("$(a) $(b) $(c)", "$(a) $(b)");
        TST_ERR("$(a) $(b) $(c)", "ga $(b) $(c)");
        TST_ERR("$(a) $(b) $(c)", "$(a) ga $(c)");

#undef TST_ERR
#undef TST
    } Z_TEST_END;

    /* }}} */
    /* {{{ yaml_data_equals strong */

    Z_TEST(yaml_data_equals_strong, "") {
        t_scope;
        yaml_data_t d1;
        yaml_data_t d2;
        yaml_data_t elem;

        yaml_data_set_string(&d1, LSTR("v"));
        yaml_data_set_bool(&d2, false);

        /* scalars with different types are never equal */
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        /* strings */
        yaml_data_set_string(&d2, LSTR("v"));
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));
        yaml_data_set_string(&d2, LSTR("a"));
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        /* double */
        yaml_data_set_double(&d1, 1.2);
        yaml_data_set_double(&d2, 1.2);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));
        yaml_data_set_double(&d2, 1.20000001);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        /* uint */
        yaml_data_set_uint(&d1, 1);
        yaml_data_set_uint(&d2, 1);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));
        yaml_data_set_uint(&d2, 2);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        /* uint */
        yaml_data_set_int(&d1, -1);
        yaml_data_set_int(&d2, -1);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));
        yaml_data_set_int(&d2, -2);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        /* bool */
        yaml_data_set_bool(&d1, true);
        yaml_data_set_bool(&d2, true);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));
        yaml_data_set_int(&d2, false);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        /* null */
        yaml_data_set_null(&d1);
        yaml_data_set_null(&d2);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));

        /* sequences */
        t_yaml_data_new_seq(&d1, 1);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));
        t_yaml_data_new_seq(&d2, 1);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));

        yaml_data_set_string(&elem, LSTR("l"));
        yaml_seq_add_data(&d1, elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        yaml_data_set_string(&elem, LSTR("d"));
        yaml_seq_add_data(&d2, elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        d2.seq->datas.tab[0].scalar.s = LSTR("l");
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));

        /* obj */
        t_yaml_data_new_obj(&d1, 2);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));
        t_yaml_data_new_obj(&d2, 2);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));

        yaml_data_set_bool(&elem, true);
        yaml_obj_add_field(&d1, LSTR("v"), elem);
        yaml_obj_add_field(&d1, LSTR("a"), elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        yaml_data_set_bool(&elem, false);
        yaml_obj_add_field(&d2, LSTR("v"), elem);
        yaml_obj_add_field(&d2, LSTR("a"), elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        qv_clear(&d2.obj->fields);
        yaml_data_set_bool(&elem, true);
        yaml_obj_add_field(&d2, LSTR("a"), elem);
        yaml_obj_add_field(&d2, LSTR("v"), elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2, true));

        qv_clear(&d2.obj->fields);
        yaml_obj_add_field(&d2, LSTR("v"), elem);
        yaml_obj_add_field(&d2, LSTR("a"), elem);
        Z_ASSERT(yaml_data_equals(&d1, &d2, true));

    } Z_TEST_END;

    /* }}} */
    /* {{{ yaml_data_equals_weak */

    Z_TEST(yaml_data_equals_weak, "") {
        yaml_data_t d1;
        yaml_data_t d2;

        /* test weak equality between different types of scalars */
#define TST(v1, v2, strong_res, weak_res)                                    \
        do {                                                                 \
            Z_ASSERT_EQ(yaml_data_equals((v1), (v2), true), (strong_res),    \
                        "invalid strong equality result");                   \
            Z_ASSERT_EQ(yaml_data_equals((v1), (v2), false), (weak_res),     \
                        "invalid weak equality result");                     \
        } while (0)

        /* bool */
        yaml_data_set_bool(&d1, true);
        yaml_data_set_bool(&d2, true);
        TST(&d1, &d2, true, true);

        yaml_data_set_string(&d2, LSTR("true"));
        TST(&d1, &d2, false, true);

        yaml_data_set_string(&d2, LSTR("false"));
        TST(&d1, &d2, false, false);

        yaml_data_set_bool(&d1, false);
        TST(&d1, &d2, false, true);

        yaml_data_set_null(&d2);
        TST(&d1, &d2, false, false);

        /* uint */
        yaml_data_set_uint(&d1, 5);
        yaml_data_set_uint(&d2, 5);
        TST(&d1, &d2, true, true);

        yaml_data_set_string(&d2, LSTR("5"));
        TST(&d1, &d2, false, true);

        yaml_data_set_string(&d2, LSTR("05"));
        TST(&d1, &d2, false, false);

        yaml_data_set_int(&d2, 5);
        TST(&d1, &d2, false, true);

        /* int */
        yaml_data_set_int(&d1, -5);
        yaml_data_set_int(&d2, -5);
        TST(&d1, &d2, true, true);

        yaml_data_set_string(&d2, LSTR("-5"));
        TST(&d1, &d2, false, true);

        yaml_data_set_string(&d2, LSTR("5"));
        TST(&d1, &d2, false, false);

        yaml_data_set_int(&d1, 5);
        TST(&d1, &d2, false, true);

        /* double */
        yaml_data_set_double(&d1, 1);
        yaml_data_set_double(&d2, 1);
        TST(&d1, &d2, true, true);

        yaml_data_set_uint(&d2, 1);
        TST(&d1, &d2, false, true);

        yaml_data_set_int(&d2, 1);
        TST(&d1, &d2, false, true);

        yaml_data_set_string(&d2, LSTR("1"));
        TST(&d1, &d2, false, true);

        yaml_data_set_double(&d1, -1.4);
        yaml_data_set_string(&d2, LSTR("-1.4"));
        TST(&d1, &d2, false, true);

        yaml_data_set_int(&d2, -1);
        TST(&d1, &d2, false, false);

        yaml_data_set_double(&d1, INFINITY);
        yaml_data_set_string(&d2, LSTR(".Inf"));
        TST(&d1, &d2, false, true);

        yaml_data_set_double(&d1, -INFINITY);
        TST(&d1, &d2, false, false);

        yaml_data_set_string(&d2, LSTR("-.Inf"));
        TST(&d1, &d2, false, true);

        yaml_data_set_double(&d1, NAN);
        yaml_data_set_string(&d2, LSTR(".NaN"));
        TST(&d1, &d2, false, true);

        yaml_data_set_string(&d2, LSTR(".NAN"));
        TST(&d1, &d2, false, false);

        /* null */
        yaml_data_set_null(&d1);
        yaml_data_set_null(&d2);
        TST(&d1, &d2, true, true);

        yaml_data_set_string(&d2, LSTR(""));
        TST(&d1, &d2, false, false);

        yaml_data_set_string(&d2, LSTR("~"));
        TST(&d1, &d2, false, true);
    } Z_TEST_END;

    /* }}} */

    MODULE_RELEASE(yaml);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
