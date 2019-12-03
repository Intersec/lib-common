/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
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

static struct yaml_g {
    logger_t logger;
} yaml_g = {
#define _G yaml_g
    .logger = LOGGER_INIT(NULL, "yaml", LOG_INHERITS),
};

/* Missing features:
 *
 * #1
 * Tab characters are forbidden, because it makes the indentation computation
 * harder than with simple spaces. It could be handled properly however.
 */

/* {{{ Parsing types definitions */
/* {{{ Presentation */

/* Presentation details applied to a specific node. */
typedef struct yaml_presentation_node_t {
    /* Comments prefixed before the node.
     *
     * For example:
     *
     * a:
     *   # Comment
     *   # Second line
     *   b: ~
     *
     * ["Comment", "Second line"] are the prefix comments for "a.b".
     */
    /* TODO: it would be better to use a static array container here, like
     * lstr__array_t, but without the IOP dependencies. */
    qv_t(lstr) prefix_comments;

    /* Comment inlined after the node.
     *
     * The comment is present at the end of the line where the node
     * is declared. For example:
     *
     * a:
     *   b: ~ # Comment
     *
     * "Comment" is an inline comment for "a.b".
     */
    lstr_t inline_comment;
} yaml_presentation_node_t;

qm_kvec_t(yaml_pres_node, lstr_t, yaml_presentation_node_t, qhash_lstr_hash,
          qhash_lstr_equal);

struct yaml_presentation_t {
    /* Map of path -> presentation details.
     *
     * The format for the path is the following:
     *  * for a key: .<key>
     *  * for a seq: [idx]
     * So for example:
     *
     * .a.foo[2].b
     * [0].b[2][0].c
     *
     * If the presentation applies to the root object that is a scalar, the
     * key is LSTR_EMPTY.
     */
    qm_t(yaml_pres_node) nodes;
};

/* Presentation details currently being constructed */
typedef struct yaml_env_presentation_t {
    qm_t(yaml_pres_node) nodes;

    /* Current path being parsed. */
    sb_t current_path;

    /* Presentation detail for the next node to be parsed. */
    yaml_presentation_node_t next_node;
    bool has_next_node;
} yaml_env_presentation_t;

/* }}} */

typedef struct yaml_env_t {
    /* String to parse. */
    pstream_t ps;

    /* Current line number. */
    uint32_t line_number;

    /* Pointer to the first character of the current line.
     * Used to compute current column number of ps->s */
    const char *pos_newline;

    /* Error buffer. */
    sb_t err;

    /* Presentation details */
    yaml_env_presentation_t * nullable pres;
} yaml_env_t;

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

static lstr_t yaml_data_get_span_lstr(const yaml_span_t * nonnull span)
{
    return LSTR_INIT_V(span->start.s, span->end.s - span->start.s);
}

static uint32_t yaml_env_get_column_nb(const yaml_env_t *env)
{
    return env->ps.s - env->pos_newline + 1;
}

static yaml_pos_t yaml_env_get_pos(const yaml_env_t *env)
{
    return (yaml_pos_t){
        .line_nb = env->line_number,
        .col_nb = yaml_env_get_column_nb(env),
        .s = env->ps.s,
    };
}

static inline void yaml_env_skipc(yaml_env_t *env)
{
    IGNORE(ps_getc(&env->ps));
}

static void
yaml_env_init_data_with_end(const yaml_env_t *env, yaml_data_type_t type,
                            yaml_pos_t pos_start, yaml_pos_t pos_end,
                            yaml_data_t *out)
{
    p_clear(out, 1);
    out->type = type;
    out->span.start = pos_start;
    out->span.end = pos_end;
}

static void
yaml_env_init_data(const yaml_env_t *env, yaml_data_type_t type,
                   yaml_pos_t pos_start, yaml_data_t *out)
{
    yaml_env_init_data_with_end(env, type, pos_start, yaml_env_get_pos(env),
                                out);
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
} yaml_error_t;

static int yaml_env_set_err(yaml_env_t *env, yaml_error_t type,
                            const char *msg)
{
    yaml_pos_t pos = yaml_env_get_pos(env);

    sb_setf(&env->err, YAML_POS_FMT ": ", YAML_POS_ARG(pos));

    switch (type) {
      case YAML_ERR_BAD_KEY:
        sb_addf(&env->err, "invalid key, %s", msg);
        break;
      case YAML_ERR_BAD_STRING:
        sb_addf(&env->err, "expected string, %s", msg);
        break;
      case YAML_ERR_MISSING_DATA:
        sb_addf(&env->err, "missing data, %s", msg);
        break;
      case YAML_ERR_WRONG_DATA:
        sb_addf(&env->err, "wrong type of data, %s", msg);
        break;
      case YAML_ERR_WRONG_INDENT:
        sb_addf(&env->err, "wrong indentation, %s", msg);
        break;
      case YAML_ERR_WRONG_OBJECT:
        sb_addf(&env->err, "wrong object, %s", msg);
        break;
      case YAML_ERR_TAB_CHARACTER:
        sb_addf(&env->err, "tab character detected, %s", msg);
        break;
      case YAML_ERR_INVALID_TAG:
        sb_addf(&env->err, "invalid tag, %s", msg);
        break;
      case YAML_ERR_EXTRA_DATA:
        sb_addf(&env->err, "extra characters after data, %s", msg);
        break;
    }

    return -1;
}

/* }}} */
/* {{{ Parser */

static int t_yaml_env_parse_data(yaml_env_t *env, const uint32_t min_indent,
                                 yaml_data_t *out);

/* {{{ Presentation utils */

static yaml_presentation_node_t * nonnull
t_yaml_env_pres_get_current_node(yaml_env_presentation_t * nonnull pres)
{
    int pos;
    yaml_presentation_node_t *node;
    lstr_t path = LSTR_SB_V(&pres->current_path);

    pos = qm_reserve(yaml_pres_node, &pres->nodes, &path, 0);
    if (pos & QHASH_COLLISION) {
        node = &pres->nodes.values[pos & ~QHASH_COLLISION];
    } else {
        pres->nodes.keys[pos] = t_lstr_dup(path);
        node = &pres->nodes.values[pos];
        p_clear(node, 1);
    }

    return node;
}

static yaml_presentation_node_t * nonnull
t_yaml_env_pres_get_next_node(yaml_env_presentation_t * nonnull pres)
{
    if (!pres->has_next_node) {
        p_clear(&pres->next_node, 1);
        pres->has_next_node = true;
    }

    return &pres->next_node;
}

/* XXX: need a prototype declaration to specify the __attr_printf__ */
static int yaml_env_pres_push_path(yaml_env_presentation_t * nullable pres,
                                   const char *fmt, ...)
    __attr_printf__(2, 3);

static int yaml_env_pres_push_path(yaml_env_presentation_t * nullable pres,
                                   const char *fmt, ...)
{
    int prev_len;
    va_list args;

    if (!pres) {
        return 0;
    }

    prev_len = pres->current_path.len;
    va_start(args, fmt);
    sb_addvf(&pres->current_path, fmt, args);
    va_end(args);

    if (pres->has_next_node) {
        int res;
        lstr_t path = t_lstr_dup(LSTR_SB_V(&pres->current_path));

        res = qm_add(yaml_pres_node, &pres->nodes, &path, pres->next_node);
        assert (res == 0);
        pres->has_next_node = false;

        logger_trace(&_G.logger, 2,
                     "adding prefixed presentation details on path `%pL`",
                     &path);
    }

    return prev_len;
}

static void yaml_env_pres_pop_path(yaml_env_presentation_t * nullable pres,
                                   int prev_len)
{
    if (!pres) {
        return;
    }

    sb_clip(&pres->current_path, prev_len);
}

static void t_yaml_env_handle_comment_ps(yaml_env_t * nonnull env,
                                         pstream_t comment_ps, bool prefix)
{
    yaml_presentation_node_t *pnode;
    lstr_t comment;

    if (!env->pres) {
        return;
    }

    comment_ps.s_end = env->ps.s;
    ps_skipc(&comment_ps, '#');
    comment = lstr_trim(LSTR_PS_V(&comment_ps));

    if (prefix) {
        pnode = t_yaml_env_pres_get_next_node(env->pres);
        if (pnode->prefix_comments.len == 0) {
            t_qv_init(&pnode->prefix_comments, 1);
        }
        qv_append(&pnode->prefix_comments, comment);
        logger_trace(&_G.logger, 2,
                     "adding prefix comment `%pL` for the next node",
                     &comment);
    } else {
        pnode = t_yaml_env_pres_get_current_node(env->pres);
        assert (pnode->inline_comment.len == 0);
        pnode->inline_comment = comment;
        logger_trace(&_G.logger, 2,
                     "adding inline comment `%pL` on path `%pL`",
                     &comment, &env->pres->current_path);
    }
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
            lstr_t span = yaml_data_get_span_lstr(&data->span);

            logger_cont(": %pL", &span);
        }
    }
}

static int yaml_env_ltrim(yaml_env_t *env)
{
    pstream_t comment_ps = ps_init(NULL, 0);
    bool in_comment = false;
    bool in_new_line = yaml_env_get_column_nb(env) == 1;

    while (!ps_done(&env->ps)) {
        int c = ps_peekc(env->ps);

        if (c == '#') {
            in_comment = true;
            comment_ps = env->ps;
        } else
        if (c == '\n') {
            env->line_number++;
            env->pos_newline = env->ps.s + 1;
            in_comment = false;
            if (comment_ps.s != NULL) {
                t_yaml_env_handle_comment_ps(env, comment_ps, in_new_line);
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
        t_yaml_env_handle_comment_ps(env, comment_ps, in_new_line);
    }

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

static bool
ps_startswith_yaml_key(pstream_t ps)
{
    pstream_t key = ps_get_span(&ps, &ctype_isalnum);

    if (ps_len(&key) == 0 || ps_len(&ps) == 0) {
        return false;
    }

    return ps.s[0] == ':'
        && (ps_len(&ps) == 1 || isspace(ps.s[1]));
}

/* }}} */
/* {{{ Tag */

static int
t_yaml_env_parse_tag(yaml_env_t *env, const uint32_t min_indent,
                     yaml_data_t *out)
{
    /* a-zA-Z0-9. */
    static const ctype_desc_t ctype_tag = { {
        0x00000000, 0x03ff4000, 0x07fffffe, 0x07fffffe,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    yaml_pos_t tag_pos_start = yaml_env_get_pos(env);
    yaml_pos_t tag_pos_end;
    pstream_t tag;

    assert (ps_peekc(env->ps) == '!');
    yaml_env_skipc(env);

    if (!isalpha(ps_peekc(env->ps))) {
        return yaml_env_set_err(env, YAML_ERR_INVALID_TAG,
                                "must start with a letter");
    }

    tag = ps_get_span(&env->ps, &ctype_tag);
    if (!isspace(ps_peekc(env->ps))) {
        return yaml_env_set_err(env, YAML_ERR_INVALID_TAG,
                                "must only contain alphanumeric characters");
    }
    tag_pos_end = yaml_env_get_pos(env);

    RETHROW(t_yaml_env_parse_data(env, min_indent, out));
    if (out->tag.s) {
        return yaml_env_set_err(env, YAML_ERR_WRONG_OBJECT,
                                "two tags have been declared");
    }

    out->tag = LSTR_PS_V(&tag);
    out->span.start = tag_pos_start;
    out->tag_span.start = tag_pos_start;
    out->tag_span.end = tag_pos_end;

    return 0;
}

/* }}} */
/* {{{ Seq */

static int t_yaml_env_parse_seq(yaml_env_t *env, const uint32_t min_indent,
                                yaml_data_t *out)
{
    qv_t(yaml_data) datas;
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    yaml_pos_t pos_end = {0};

    t_qv_init(&datas, 0);

    assert (ps_startswith_yaml_seq_prefix(&env->ps));

    for (;;) {
        yaml_data_t elem;
        uint32_t last_indent;
        int path_len;

        /* skip '-' */
        yaml_env_skipc(env);

        path_len = yaml_env_pres_push_path(env->pres, "[%d]", datas.len);
        RETHROW(t_yaml_env_parse_data(env, min_indent + 1, &elem));
        pos_end = elem.span.end;
        qv_append(&datas, elem);

        /* XXX: keep the current path for this ltrim, to handle inline
         * comments */
        RETHROW(yaml_env_ltrim(env));
        yaml_env_pres_pop_path(env->pres, path_len);

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

    yaml_env_init_data_with_end(env, YAML_DATA_SEQ, pos_start, pos_end, out);
    out->seq = t_new(yaml_seq_t, 1);
    out->seq->datas = datas;

    return 0;
}

/* }}} */
/* {{{ Obj */

static int yaml_env_parse_key(yaml_env_t * nonnull env, lstr_t * nonnull key,
                              yaml_span_t * nonnull key_span)
{
    pstream_t ps_key;

    key_span->start = yaml_env_get_pos(env);
    ps_key = ps_get_span(&env->ps, &ctype_isalnum);
    key_span->end = yaml_env_get_pos(env);

    if (ps_len(&ps_key) == 0) {
        return yaml_env_set_err(env, YAML_ERR_BAD_KEY,
                                "only alpha-numeric characters allowed");
    } else
    if (ps_getc(&env->ps) != ':') {
        return yaml_env_set_err(env, YAML_ERR_BAD_KEY, "missing colon");
    }

    *key = LSTR_PS_V(&ps_key);

    return 0;
}

static int
t_yaml_env_parse_obj(yaml_env_t *env, const uint32_t min_indent,
                     yaml_data_t *out)
{
    qv_t(yaml_key_data) fields;
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    yaml_pos_t pos_end = {0};
    qh_t(lstr) keys_hash;

    t_qv_init(&fields, 0);
    t_qh_init(lstr, &keys_hash, 0);

    for (;;) {
        lstr_t key;
        yaml_key_data_t *kd;
        uint32_t last_indent;
        yaml_span_t key_span;
        int path_len;

        RETHROW(yaml_env_parse_key(env, &key, &key_span));

        kd = qv_growlen0(&fields, 1);
        kd->key = key;
        kd->key_span = key_span;
        if (qh_add(lstr, &keys_hash, &kd->key) < 0) {
            return yaml_env_set_err(env, YAML_ERR_BAD_KEY,
                                    "key is already declared in the object");
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
        path_len = yaml_env_pres_push_path(env->pres, ".%pL", &kd->key);
        RETHROW(yaml_env_ltrim(env));

        if (ps_startswith_yaml_seq_prefix(&env->ps)) {
            RETHROW(t_yaml_env_parse_data(env, min_indent, &kd->data));
        } else {
            RETHROW(t_yaml_env_parse_data(env, min_indent + 1, &kd->data));
        }

        pos_end = kd->data.span.end;

        /* XXX: keep the current path for this ltrim, to handle inline
         * comments */
        RETHROW(yaml_env_ltrim(env));
        yaml_env_pres_pop_path(env->pres, path_len);

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

    yaml_env_init_data_with_end(env, YAML_DATA_OBJ, pos_start, pos_end, out);
    out->obj = t_new(yaml_obj_t, 1);
    out->obj->fields = fields;
    return 0;
}

/* }}} */
/* {{{ Scalar */

static pstream_t yaml_env_get_scalar_ps(yaml_env_t * nonnull env,
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
t_yaml_env_parse_quoted_string(yaml_env_t *env, yaml_data_t *out)
{
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    int line_nb = 0;
    int col_nb = 0;
    sb_t buf;
    parse_str_res_t res;

    assert (ps_peekc(env->ps) == '"');
    yaml_env_skipc(env);

    t_sb_init(&buf, 128);
    res = parse_quoted_string(&env->ps, &buf, &line_nb, &col_nb, '"');
    switch (res) {
      case PARSE_STR_ERR_UNCLOSED:
        return yaml_env_set_err(env, YAML_ERR_BAD_STRING,
                                "missing closing '\"'");
      case PARSE_STR_ERR_EXP_SMTH:
        return yaml_env_set_err(env, YAML_ERR_BAD_STRING,
                                "invalid backslash");
      case PARSE_STR_OK:
        yaml_env_init_data(env, YAML_DATA_SCALAR, pos_start, out);
        out->scalar.type = YAML_SCALAR_STRING;
        out->scalar.s = LSTR_SB_V(&buf);
        return 0;
    }

    assert (false);
    return yaml_env_set_err(env, YAML_ERR_BAD_STRING, "invalid string");
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

static int t_yaml_env_parse_scalar(yaml_env_t *env, bool in_flow,
                                   yaml_data_t *out)
{
    lstr_t line;
    yaml_pos_t pos_start;
    pstream_t ps_line;

    if (ps_peekc(env->ps) == '"') {
        return t_yaml_env_parse_quoted_string(env, out);
    }

    /* get scalar string, ie up to newline or comment, or ']' or ',' for flow
     * context */
    pos_start = yaml_env_get_pos(env);
    ps_line = yaml_env_get_scalar_ps(env, in_flow);
    if (ps_len(&ps_line) == 0) {
        return yaml_env_set_err(env, YAML_ERR_MISSING_DATA,
                                "unexpected character");
    }

    line = LSTR_PS_V(&ps_line);
    yaml_env_init_data(env, YAML_DATA_SCALAR, pos_start, out);

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

    return 0;
}

/* }}} */
/* {{{ Flow seq */

static int t_yaml_env_parse_flow_key_data(yaml_env_t * nonnull env,
                                          yaml_key_data_t * nonnull out);

static void
t_yaml_env_build_implicit_obj(yaml_env_t * nonnull env,
                              yaml_key_data_t * nonnull kd,
                              yaml_data_t * nonnull out)
{
    qv_t(yaml_key_data) fields;

    t_qv_init(&fields, 1);
    qv_append(&fields, *kd);

    yaml_env_init_data_with_end(env, YAML_DATA_OBJ, kd->key_span.start,
                                kd->data.span.end, out);
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
t_yaml_env_parse_flow_seq(yaml_env_t *env, yaml_data_t *out)
{
    qv_t(yaml_data) datas;
    yaml_pos_t pos_start = yaml_env_get_pos(env);

    t_qv_init(&datas, 0);

    /* skip '[' */
    assert (ps_peekc(env->ps) == '[');
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
    yaml_env_init_data_with_end(env, YAML_DATA_SEQ, pos_start,
                                yaml_env_get_pos(env), out);
    out->seq = t_new(yaml_seq_t, 1);
    out->seq->datas = datas;

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
t_yaml_env_parse_flow_obj(yaml_env_t *env, yaml_data_t *out)
{
    qv_t(yaml_key_data) fields;
    yaml_pos_t pos_start = yaml_env_get_pos(env);

    t_qv_init(&fields, 0);

    /* skip '{' */
    assert (ps_peekc(env->ps) == '{');
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
            env->ps.s = kd.data.span.start.s;
            return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
                                    "only key-value mappings are allowed "
                                    "inside an object");
        } else {
            qv_append(&fields, kd);
        }

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
    yaml_env_init_data_with_end(env, YAML_DATA_OBJ, pos_start,
                                yaml_env_get_pos(env), out);
    out->obj = t_new(yaml_obj_t, 1);
    out->obj->fields = fields;

    return 0;
}

/* }}} */
/* {{{ Flow key-data */

static int t_yaml_env_parse_flow_key_val(yaml_env_t *env,
                                         yaml_key_data_t *out)
{
    yaml_key_data_t kd;

    RETHROW(yaml_env_parse_key(env, &out->key, &out->key_span));
    RETHROW(yaml_env_ltrim(env));
    RETHROW(t_yaml_env_parse_flow_key_data(env, &kd));
    if (kd.key.s) {
        /* This means the value was a key val mapping:
         *   a: b: c.
         * Place the ps on the end of the second key, to point to the second
         * colon. */
        env->ps.s = kd.key_span.end.s;
        return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
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
static int t_yaml_env_parse_flow_key_data(yaml_env_t *env,
                                          yaml_key_data_t *out)
{
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
/* {{{ Data */

static int t_yaml_env_parse_data(yaml_env_t *env, const uint32_t min_indent,
                                 yaml_data_t *out)
{
    uint32_t cur_indent;

    RETHROW(yaml_env_ltrim(env));
    if (ps_done(&env->ps)) {
        return yaml_env_set_err(env, YAML_ERR_MISSING_DATA,
                                "unexpected end of line");
    }

    cur_indent = yaml_env_get_column_nb(env);
    if (cur_indent < min_indent) {
        return yaml_env_set_err(env, YAML_ERR_WRONG_INDENT,
                                "missing element");
    }

    if (ps_peekc(env->ps) == '!') {
        RETHROW(t_yaml_env_parse_tag(env, min_indent, out));
    } else
    if (ps_startswith_yaml_seq_prefix(&env->ps)) {
        RETHROW(t_yaml_env_parse_seq(env, cur_indent, out));
    } else
    if (ps_peekc(env->ps) == '[') {
        RETHROW(t_yaml_env_parse_flow_seq(env, out));
    } else
    if (ps_peekc(env->ps) == '{') {
        RETHROW(t_yaml_env_parse_flow_obj(env, out));
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
/* {{{ Parser public API */

int t_yaml_parse(pstream_t ps, yaml_data_t *out,
                 const yaml_presentation_t **presentation, sb_t *out_err)
{
    yaml_env_t env;
    t_SB_1k(err);

    p_clear(&env, 1);
    env.err = err;

    env.ps = ps;
    env.pos_newline = ps.s;
    env.line_number = 1;
    if (presentation) {
        env.pres = t_new(yaml_env_presentation_t, 1);
        t_qm_init(yaml_pres_node, &env.pres->nodes, 0);
        t_sb_init(&env.pres->current_path, 1024);
    }

    if (t_yaml_env_parse_data(&env, 0, out) < 0) {
        sb_setsb(out_err, &env.err);
        return -1;
    }

    RETHROW(yaml_env_ltrim(&env));
    if (!ps_done(&env.ps)) {
        yaml_env_set_err(&env, YAML_ERR_EXTRA_DATA,
                         "expected end of document");
        sb_setsb(out_err, &env.err);
        return -1;
    }

    /* handle "next_node" if the document consists only of a scalar */
    if (out->type == YAML_DATA_SCALAR) {
        yaml_env_pres_push_path(env.pres, "");
    }

    if (presentation) {
        yaml_presentation_t *pres;

        pres = t_new(yaml_presentation_t, 1);
        pres->nodes = env.pres->nodes;
        *presentation = pres;
    }

    return 0;
}

/* }}} */
/* {{{ Packer */

#define YAML_STD_INDENT  2

typedef struct yaml_pack_env_presentation_t {
    qm_t(yaml_pres_node) nodes;

    /* Current path being packed. */
    sb_t current_path;
} yaml_pack_env_presentation_t;

typedef struct yaml_pack_env_t {
    yaml_pack_writecb_f *write_cb;
    void *priv;
    yaml_pack_env_presentation_t * nullable pres;
} yaml_pack_env_t;

/* "to_indent" indicates that we have already packed some data in the output,
 * and that if we want to put some more data, we have to separate it from the
 * previously packed data properly.
 * For a scalar, this means adding a space
 * For a key-data or an array elem, this means adding a newline + indent
 */
static int yaml_pack_data(const yaml_pack_env_t * nonnull env,
                          const yaml_data_t * nonnull data, int indent_lvl,
                          bool to_indent);

/* {{{ Utils */

static int do_write(const yaml_pack_env_t *env, const void *_buf, int len)
{
    const uint8_t *buf = _buf;
    int pos = 0;

    while (pos < len) {
        int res = (*env->write_cb)(env->priv, buf + pos, len - pos);

        if (res < 0) {
            if (ERR_RW_RETRIABLE(errno))
                continue;
            return -1;
        }
        pos += res;
    }
    return len;
}

static int do_indent(const yaml_pack_env_t *env, int indent)
{
    static lstr_t spaces = LSTR_IMMED("                                    ");
    int todo = indent;

    while (todo > 0) {
        int res = (*env->write_cb)(env->priv, spaces.s,
                                   MIN(spaces.len, todo));

        if (res < 0) {
            if (ERR_RW_RETRIABLE(errno)) {
                continue;
            }
            return -1;
        }
        todo -= res;
    }

    return indent;
}

#define WRITE(data, len)                                                     \
    do {                                                                     \
        res += RETHROW(do_write(env, data, len));                            \
    } while (0)
#define PUTS(s)  WRITE(s, strlen(s))
#define PUTLSTR(s)  WRITE(s.data, s.len)

#define INDENT(lvl)                                                          \
    do {                                                                     \
        res += RETHROW(do_indent(env, lvl));                                 \
    } while (0)

/* }}} */
/* {{{ Presentation */

/* XXX: need a prototype declaration to specify the __attr_printf__ */
static int
yaml_pack_env_pres_push_path(yaml_pack_env_presentation_t * nullable pres,
                             const char *fmt, ...)
    __attr_printf__(2, 3);

static int
yaml_pack_env_pres_push_path(yaml_pack_env_presentation_t * nullable pres,
                             const char *fmt, ...)
{
    int prev_len;
    va_list args;

    if (!pres) {
        return 0;
    }

    prev_len = pres->current_path.len;
    va_start(args, fmt);
    sb_addvf(&pres->current_path, fmt, args);
    va_end(args);

    return prev_len;
}

static void
yaml_pack_env_pres_pop_path(yaml_pack_env_presentation_t * nullable pres,
                            int prev_len)
{
    if (!pres) {
        return;
    }

    sb_clip(&pres->current_path, prev_len);
}

static const yaml_presentation_node_t * nullable
yaml_pack_env_get_pres_node(yaml_pack_env_presentation_t * nullable pres)
{
    lstr_t path;

    if (!pres) {
        return NULL;
    }

    path = LSTR_SB_V(&pres->current_path);
    return qm_get_def_p_safe(yaml_pres_node, &pres->nodes, &path, NULL);
}

static int
yaml_pack_pres_node_prefix(const yaml_pack_env_t * nonnull env,
                           const yaml_presentation_node_t * nullable node,
                           int indent_lvl, bool to_indent)
{
    int res = 0;
    bool orig_to_indent = to_indent;

    if (!node || node->prefix_comments.len == 0) {
        return res;
    }

    tab_for_each_entry(comment, &node->prefix_comments) {
        if (to_indent) {
            PUTS("\n");
            INDENT(indent_lvl);
        } else {
            to_indent = true;
        }

        PUTS("# ");
        PUTLSTR(comment);
    }

    if (!orig_to_indent) {
        PUTS("\n");
        INDENT(indent_lvl);
    }

    return res;
}

static int
yaml_pack_pres_node_inline(const yaml_pack_env_t * nonnull env,
                           const yaml_presentation_node_t * nullable node,
                           bool * nonnull to_indent)
{
    int res = 0;

    if (node && node->inline_comment.len > 0) {
        if (*to_indent) {
            PUTS(" # ");
        } else {
            PUTS("# ");
            *to_indent = true;
        }
        PUTLSTR(node->inline_comment);
    }

    return res;
}

/* }}} */
/* {{{ Pack scalar */

/* ints:   sign, 20 digits, and NUL -> 22
 * double: sign, digit, dot, 17 digits, e, sign, up to 3 digits NUL -> 25
 */
#define IBUF_LEN  25

static bool yaml_string_must_be_quoted(const lstr_t s)
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

    /* cannot start with those characters */
    if (ctype_desc_contains(&yaml_invalid_raw_string_start, s.s[0])) {
        return true;
    }
    /* cannot contain those characters */
    if (!lstr_match_ctype(s, &yaml_raw_string_contains)) {
        return true;
    }
    if (lstr_equal(s, LSTR("~")) || lstr_equal(s, LSTR("null"))) {
        return true;
    }

    return false;
}

static int yaml_pack_string(const yaml_pack_env_t *env, lstr_t val)
{
    int res = 0;
    pstream_t ps;

    if (!yaml_string_must_be_quoted(val)) {
        PUTLSTR(val);
        return res;
    }

    ps = ps_initlstr(&val);
    PUTS("\"");
    while (!ps_done(&ps)) {
        /* r:32-127 -s:'\\"' */
        static ctype_desc_t const safe_chars = { {
            0x00000000, 0xfffffffb, 0xefffffff, 0xffffffff,
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

static int yaml_pack_scalar(const yaml_pack_env_t * nonnull env,
                            const yaml_scalar_t * nonnull scalar,
                            bool to_indent)
{
    int res = 0;
    char ibuf[IBUF_LEN];

    if (to_indent) {
        PUTS(" ");
    }

    switch (scalar->type) {
      case YAML_SCALAR_STRING:
        return yaml_pack_string(env, scalar->s);

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
    }

    return res;
}

/* }}} */
/* {{{ Pack sequence */

static int yaml_pack_seq(const yaml_pack_env_t * nonnull env,
                         const yaml_seq_t * nonnull seq, int indent_lvl,
                         bool to_indent)
{
    const yaml_presentation_node_t *node;
    int res = 0;

    if (seq->datas.len == 0) {
        if (to_indent) {
            PUTS(" []");
        } else {
            PUTS("[]");
        }
        return res;
    }

    tab_for_each_pos(pos, &seq->datas) {
        const yaml_data_t *data = &seq->datas.tab[pos];
        int path_len;
        bool data_to_indent = false;

        path_len = yaml_pack_env_pres_push_path(env->pres, "[%d]", pos);
        node = yaml_pack_env_get_pres_node(env->pres);
        res += yaml_pack_pres_node_prefix(env, node, indent_lvl, to_indent);

        if (to_indent) {
            PUTS("\n");
            INDENT(indent_lvl);
        } else {
            to_indent = true;
        }
        PUTS("- ");

        if (data->type != YAML_DATA_SCALAR) {
            res += yaml_pack_pres_node_inline(env, node, &data_to_indent);
        }
        res += RETHROW(yaml_pack_data(env, data, indent_lvl + 2,
                                      data_to_indent));

        yaml_pack_env_pres_pop_path(env->pres, path_len);
    }

    return res;
}

/* }}} */
/* {{{ Pack object */

static int yaml_pack_key_data(const yaml_pack_env_t * nonnull env,
                              const lstr_t key, const yaml_data_t * nonnull data,
                              int indent_lvl, bool to_indent)
{
    int res = 0;
    int path_len;
    const yaml_presentation_node_t *node;

    path_len = yaml_pack_env_pres_push_path(env->pres, ".%pL", &key);
    node = yaml_pack_env_get_pres_node(env->pres);
    res += yaml_pack_pres_node_prefix(env, node, indent_lvl, to_indent);

    if (to_indent) {
        PUTS("\n");
        INDENT(indent_lvl);
    }

    PUTLSTR(key);
    PUTS(":");

    /* for scalars, we put the inline comment after the value:
     *  key: val # comment
     */
    to_indent = true;
    if (data->type != YAML_DATA_SCALAR) {
        res += yaml_pack_pres_node_inline(env, node, &to_indent);
    }
    res += RETHROW(yaml_pack_data(env, data, indent_lvl + YAML_STD_INDENT,
                                  to_indent));

    yaml_pack_env_pres_pop_path(env->pres, path_len);

    return res;
}

static int yaml_pack_obj(const yaml_pack_env_t * nonnull env,
                         const yaml_obj_t * nonnull obj, int indent_lvl,
                         bool to_indent)
{
    int res = 0;
    bool first = !to_indent;

    if (obj->fields.len == 0) {
        if (to_indent) {
            PUTS(" {}");
        } else {
            PUTS("{}");
        }
        return res;
    }

    tab_for_each_ptr(pair, &obj->fields) {
        res += RETHROW(yaml_pack_key_data(env, pair->key, &pair->data,
                                          indent_lvl, !first));
        if (first) {
            first = false;
        }
    }

    return res;
}

/* }}} */
/* {{{ Pack data */

static int yaml_pack_data(const yaml_pack_env_t * nonnull env,
                          const yaml_data_t * nonnull data,
                          int indent_lvl, bool to_indent)
{
    int res = 0;

    if (data->tag.s) {
        if (to_indent) {
            PUTS(" !");
        } else {
            PUTS("!");
        }
        PUTLSTR(data->tag);
        to_indent = true;
    }

    switch (data->type) {
      case YAML_DATA_SCALAR: {
        const yaml_presentation_node_t *node;

        res += RETHROW(yaml_pack_scalar(env, &data->scalar, to_indent));
        node = yaml_pack_env_get_pres_node(env->pres);
        to_indent = true;
        res += yaml_pack_pres_node_inline(env, node, &to_indent);
      } break;
      case YAML_DATA_SEQ:
        res += RETHROW(yaml_pack_seq(env, data->seq, indent_lvl, to_indent));
        break;
      case YAML_DATA_OBJ:
        res += RETHROW(yaml_pack_obj(env, data->obj, indent_lvl, to_indent));
        break;
    }

    return res;
}

static int yaml_pack_root_data(const yaml_pack_env_t * nonnull env,
                               const yaml_data_t * nonnull data)
{
    const yaml_presentation_node_t *node;
    int res = 0;

    node = yaml_pack_env_get_pres_node(env->pres);
    res += yaml_pack_pres_node_prefix(env, node, 0, false);
    res += RETHROW(yaml_pack_data(env, data, 0, false));

    return res;
}

#undef WRITE
#undef PUTS
#undef PUTLSTR
#undef INDENT

/* }}} */
/* }}} */
/* {{{ Pack public API */

int yaml_pack(const yaml_data_t * nonnull data,
              const yaml_presentation_t * nullable presentation,
              yaml_pack_writecb_f * nonnull writecb, void * nullable priv)
{
    /* FIXME: must declare a t_scope */
    yaml_pack_env_t env = {
        .write_cb = writecb,
        .priv = priv,
    };

    if (presentation) {
        env.pres = t_new(yaml_pack_env_presentation_t, 1);
        env.pres->nodes = presentation->nodes;
        t_sb_init(&env.pres->current_path, 1024);
    }

    /* Always skip everything that can be skipped */
    return yaml_pack_root_data(&env, data);
}

static inline int sb_write(void * nonnull b, const void * nonnull buf,
                           int len)
{
    sb_add(b, buf, len);
    return len;
}

int yaml_pack_sb(const yaml_data_t * nonnull data,
                 const yaml_presentation_t * nullable presentation,
                 sb_t * nonnull sb)
{
    return yaml_pack(data, presentation, &sb_write, sb);
}

typedef struct yaml_pack_file_ctx_t {
    file_t *file;
    sb_t *err;
} yaml_pack_file_ctx_t;

static int iop_ypack_write_file(void *priv, const void *data, int len)
{
    yaml_pack_file_ctx_t *ctx = priv;

    if (file_write(ctx->file, data, len) < 0) {
        sb_addf(ctx->err, "cannot write in output file: %m");
        return -1;
    }

    return len;
}

int yaml_pack_file(const char * nonnull filename, unsigned file_flags,
                   mode_t file_mode, const yaml_data_t * nonnull data,
                   const yaml_presentation_t * nullable presentation,
                   sb_t * nonnull err)
{
    yaml_pack_file_ctx_t ctx;
    int res;

    p_clear(&ctx, 1);
    ctx.file = file_open(filename, file_flags, file_mode);
    if (!ctx.file) {
        sb_setf(err, "cannot open output file `%s`: %m", filename);
        return -1;
    }
    ctx.err = err;

    res = yaml_pack(data, presentation, &iop_ypack_write_file, &ctx);
    if (res < 0) {
        IGNORE(file_close(&ctx.file));
        return res;
    }

    /* End the file with a newline, as the packing ends immediately after
     * the last value. */
    file_puts(ctx.file, "\n");

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

static int z_yaml_test_parse_fail(const char *yaml, const char *expected_err)
{
    t_scope;
    yaml_data_t data;
    SB_1k(err);

    Z_ASSERT_NEG(t_yaml_parse(ps_initstr(yaml), &data, NULL, &err));
    Z_ASSERT_STREQUAL(err.data, expected_err,
                      "wrong error message on yaml string `%s`", yaml);

    Z_HELPER_END;
}

/* out parameter first to let the yaml string be last, which makes it
 * much easier to write multiple lines without horrible indentation */
static
int z_t_yaml_test_parse_success(yaml_data_t * nonnull data,
                                const yaml_presentation_t ** nullable pres,
                                const char * nonnull yaml,
                                const char * nullable expected_repack)
{
    const yaml_presentation_t *p;
    SB_1k(err);
    SB_1k(repack);

    if (!pres) {
        pres = &p;
    }
    Z_ASSERT_N(t_yaml_parse(ps_initstr(yaml), data, pres, &err),
               "yaml parsing failed: %pL", &err);

    if (!expected_repack) {
        expected_repack = yaml;
    }
    yaml_pack_sb(data, *pres, &repack);
    Z_ASSERT_STREQUAL(repack.data, expected_repack,
                      "repacking the parsed data leads to differences");

    Z_HELPER_END;
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
                  const yaml_presentation_t * nullable presentation,
                  const char *yaml)
{
    SB_1k(sb);

    yaml_pack_sb(data, presentation, &sb);
    Z_ASSERT_STREQUAL(sb.data, yaml);

    Z_HELPER_END;
}

static int z_check_inline_comment(const yaml_presentation_t * nonnull pres,
                                  lstr_t path, lstr_t comment)
{
    const yaml_presentation_node_t *pnode;

    pnode = qm_get_def_p_safe(yaml_pres_node, &pres->nodes, &path, NULL);
    Z_ASSERT_P(pnode);
    Z_ASSERT_LSTREQUAL(pnode->inline_comment, comment);

    Z_HELPER_END;
}

static int z_check_prefix_comments(const yaml_presentation_t * nonnull pres,
                                   lstr_t path, lstr_t *comments,
                                   int len)
{
    const yaml_presentation_node_t *pnode;

    pnode = qm_get_def_p_safe(yaml_pres_node, &pres->nodes, &path, NULL);
    Z_ASSERT_P(pnode);
    Z_ASSERT_EQ(len, pnode->prefix_comments.len);
    tab_for_each_pos(pos, &pnode->prefix_comments) {
        Z_ASSERT_LSTREQUAL(comments[pos], pnode->prefix_comments.tab[pos],
                           "prefix comment number #%d differs", pos);
    }

    Z_HELPER_END;
}

/* }}} */

Z_GROUP_EXPORT(yaml)
{
    MODULE_REQUIRE(yaml);

    /* {{{ Parsing errors */

    Z_TEST(parsing_errors, "errors when parsing yaml") {
        /* unexpected EOF */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "",
            "1:1: missing data, unexpected end of line"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "  # my comment",
            "1:15: missing data, unexpected end of line"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "key:",
            "1:5: missing data, unexpected end of line"
        ));

        /* wrong object continuation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 5\nb",
            "2:2: invalid key, missing colon"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 5\n_:",
            "2:1: invalid key, only alpha-numeric characters allowed"
        ));

        /* wrong explicit string */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "\" unfinished string",
            "1:2: expected string, missing closing '\"'"
        ));

        /* wrong escaped code */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "\"\\",
            "1:2: expected string, invalid backslash"
        ));

        /* wrong tag */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!-",
            "1:2: invalid tag, must start with a letter"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!a-\n"
            "a: 5",
            "1:3: invalid tag, must only contain alphanumeric characters"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!4a\n"
            "a: 5",
            "1:2: invalid tag, must start with a letter"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!tag1\n"
            "!tag2\n"
            "a: 2",
            "3:5: wrong object, two tags have been declared"
        ));

        /* wrong list continuation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "- 2\n"
            "-3",
            "2:1: wrong type of data, expected another element of sequence"
        ));

        /* wrong indent */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 2\n"
            " b: 3",
            "2:2: wrong indentation, line not aligned with current object"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "- 2\n"
            " - 3",
            "2:2: wrong indentation, line not aligned with current sequence"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 1\n"
            "b:\n"
            "c: 3",
            "3:1: wrong indentation, missing element"
        ));

        /* wrong object */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 1\n"
            "a: 2",
            "2:3: invalid key, key is already declared in the object"
        ));

        /* cannot use tab characters for indentation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a:\t1",
            "1:3: tab character detected, "
            "cannot use tab characters for indentation"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a:\n"
            "\t- 2\n"
            "\t- 3",
            "2:1: tab character detected, "
            "cannot use tab characters for indentation"
        ));

        /* extra data after the parsing */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "1\n"
            "# comment\n"
            "2",
            "3:1: extra characters after data, expected end of document"
        ));

        /* flow seq */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "[a[",
            "1:3: wrong type of data, expected another element of sequence"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "[",
            "1:2: missing data, unexpected end of line"
        ));

        /* flow obj */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{,",
            "1:2: missing data, unexpected character"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{a:b}",
            "1:2: wrong type of data, only key-value mappings are allowed "
            "inside an object"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{a: b[",
            "1:6: wrong type of data, expected another element of object"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{ a: b: c }",
            "1:7: wrong type of data, unexpected colon"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing scalars */

    Z_TEST(parsing_scalar, "test parsing of scalars") {
        t_scope;
        yaml_data_t data;

        /* string */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "unquoted string",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 16));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a string value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "!tag unquoted string",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 21));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged string value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "\" quoted: 5 \"",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 14));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR(" quoted: 5 "));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "  trimmed   ",
            "trimmed"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 3, 1, 10));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("trimmed"));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "a:x:b",
            "\"a:x:b\""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 6));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("a:x:b"));

        /* null */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "~",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 2));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a null value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "!tag ~",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 7));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged null value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "null",
            "~"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "NulL",
            "~"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        /* bool */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "true",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a boolean value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "!tag true",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 10));
        Z_ASSERT(data.scalar.b);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged boolean value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "TrUE",
            "true"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "false",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "FALse",
            "false"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        /* uint */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "0",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 2));
        Z_ASSERT_EQ(data.scalar.u, 0UL);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an unsigned integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "!tag 0",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 7));
        Z_ASSERT_EQ(data.scalar.u, 0UL);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged unsigned integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "153",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.u, 153UL);

        /* -0 will still generate UINT */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "-0",
            "0"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 3));

        /* int */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "-1",
            NULL));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 3));
        Z_ASSERT_EQ(data.scalar.i, -1L);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "!tag -1",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 8));
        Z_ASSERT_EQ(data.scalar.i, -1L);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "-153",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.i, -153L);

        /* double */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "0.5",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.d, 0.5);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a double value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "-1e3",
            "-1000"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.d, -1000.0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "-.Inf",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 6));
        Z_ASSERT_EQ(isinf(data.scalar.d), -1);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            ".INf",
            ".Inf"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(isinf(data.scalar.d), 1);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            ".NAN",
            ".NaN"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT(isnan(data.scalar.d));
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "a: 2\n"
            "inner: b: 3\n"
            "       c: -4\n"
            "inner2: !tag\n"
            "  d: ~\n"
            "  e: my-label\n"
            "f: 1.2",

            "a: 2\n"
            "inner:\n"
            "  b: 3\n"
            "  c: -4\n"
            "inner2: !tag\n"
            "  d: ~\n"
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
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_NULL,
                                         5, 6, 5, 7));
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "- \"a: 2\"\n"
            "- - 5\n"
            "  - -5\n"
            "- ~\n"
            "-\n"
            "  !tag - TRUE\n"
            "- FALSE\n",

            "- \"a: 2\"\n"
            "- - 5\n"
            "  - -5\n"
            "- ~\n"
            "- !tag\n"
            "  - true\n"
            "- false"
        ));

        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 7, 8));
        Z_ASSERT_EQ(data.seq->datas.len, 5);

        /* "a: 2" */
        elem = data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_STRING,
                                         1, 3, 1, 9));
        Z_ASSERT_LSTREQUAL(elem.scalar.s, LSTR("a: 2"));

        /* subseq */
        elem = data.seq->datas.tab[1];
        Z_HELPER_RUN(z_check_yaml_data(&elem, YAML_DATA_SEQ, 2, 3, 3, 7));
        Z_ASSERT_EQ(elem.seq->datas.len, 2);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq->datas.tab[0], YAML_SCALAR_UINT,
                                         2, 5, 2, 6));
        Z_ASSERT_EQ(elem.seq->datas.tab[0].scalar.u, 5UL);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq->datas.tab[1], YAML_SCALAR_INT,
                                         3, 5, 3, 7));
        Z_ASSERT_EQ(elem.seq->datas.tab[1].scalar.i, -5L);

        /* null */
        elem = data.seq->datas.tab[2];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_NULL,
                                         4, 3, 4, 4));

        /* subseq */
        elem = data.seq->datas.tab[3];
        Z_HELPER_RUN(z_check_yaml_data(&elem, YAML_DATA_SEQ, 6, 3, 6, 14));
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "[]",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 3));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.seq->datas.len == 0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "[ ~ ]",
            "- ~"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 6));
        Z_ASSERT(data.seq->datas.len == 1);
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL,
                                         1, 3, 1, 4));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "[ ~, ]",
            "- ~"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 7));
        Z_ASSERT(data.seq->datas.len == 1);
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL,
                                         1, 3, 1, 4));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "[1 ,a:\n"
            "2,c d ,]",

            "- 1\n"
            "- a: 2\n"
            "- c d"
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "- [ ~,\n"
            " [[ true, [ - 2 ] ]\n"
            "   ] , a:  [  -2] ,\n"
            "]",

            "- - ~\n"
            "  - - - true\n"
            "      - - \"- 2\"\n"
            "  - a:\n"
            "      - -2"
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "{}",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 3));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.obj->fields.len == 0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "{ a: ~ }",
            "a: ~"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 9));
        Z_ASSERT(data.obj->fields.len == 1);
        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 3, 1, 4));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_NULL,
                                         1, 6, 1, 7));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "{ a: foo, }",
            "a: foo"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 12));
        Z_ASSERT(data.obj->fields.len == 1);
        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 3, 1, 4));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_STRING,
                                         1, 6, 1, 9));
        Z_ASSERT_LSTREQUAL(elem->data.scalar.s, LSTR("foo"));

        /* FIXME: this should be an error */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "{ a: ~ ,a:\n"
            "2,}",

            "a: ~\n"
            "a: 2"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 2, 4));
        Z_ASSERT(data.obj->fields.len == 2);
        elem = &data.obj->fields.tab[0];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 3, 1, 4));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_NULL,
                                         1, 6, 1, 7));
        elem = &data.obj->fields.tab[1];
        Z_ASSERT_LSTREQUAL(elem->key, LSTR("a"));
        Z_HELPER_RUN(z_check_yaml_span(&elem->key_span, 1, 9, 1, 10));
        Z_HELPER_RUN(z_check_yaml_scalar(&elem->data, YAML_SCALAR_UINT,
                                         2, 1, 2, 2));
        Z_ASSERT_EQ(elem->data.scalar.u, 2UL);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL,
            "- { a: [true,\n"
            "   false,]\n"
            "     , b: f   \n"
            "  ,\n"
            "    z: { y: 1  }}\n"
            "- ~",

            "- a:\n"
            "    - true\n"
            "    - false\n"
            "  b: f\n"
            "  z:\n"
            "    y: 1\n"
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
        const yaml_presentation_t *pres = NULL;

        /* comment on a scalar => path is empty */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres,
            "# my scalar\n"
            "3",
            NULL
        ));
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR_EMPTY_V, LSTR("my scalar"));

        /* comment on a key => path is key */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres,
            "a: 3 #the key is a   ",

            "a: 3 # the key is a"
        ));
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".a"),
                                            LSTR("the key is a")));

        /* comment on a list => path is index */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres,
            "# prefix comment\n"
            "- 1 # first\n"
            "- 2 # second",
            NULL
        ));
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(2, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR("[0]"), LSTR("prefix comment"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR("[0]"),
                                            LSTR("first")));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR("[1]"),
                                            LSTR("second")));

        /* prefix comment with multiple lines */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres,
            "key:\n"
            "   # first line\n"
            " # and second\n"
            "     # bad indent is ok\n"
            "  a: ~ # inline\n"
            "       # this is lost",

            "key:\n"
            "  # first line\n"
            "  # and second\n"
            "  # bad indent is ok\n"
            "  a: ~ # inline"
        ));
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key.a"),
                              LSTR("first line"),
                              LSTR("and second"),
                              LSTR("bad indent is ok"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key.a"),
                                            LSTR("inline")));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres,
            "# prefix key\n"
            "key: # inline key\n"
            "# prefix [0]\n"
            "- # inline [0]\n"
            " # prefix key2\n"
            " key2: ~ # inline key2",

            "# prefix key\n"
            "key: # inline key\n"
            "  # prefix [0]\n"
            "  - # inline [0]\n"
            "    # prefix key2\n"
            "    key2: ~ # inline key2"
        ));
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(3, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key"),
                              LSTR("prefix key"));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key[0]"),
                              LSTR("prefix [0]"));
        CHECK_PREFIX_COMMENTS(pres, LSTR(".key[0].key2"),
                              LSTR("prefix key2"));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key"),
                                            LSTR("inline key")));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key[0]"),
                                            LSTR("inline [0]")));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".key[0].key2"),
                                            LSTR("inline key2")));
    } Z_TEST_END;

    /* }}} */

    MODULE_RELEASE(yaml);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
