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
#include <lib-common/iop.h>

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
} yaml_parse_t;

/* }}} */
/* {{{ Equality */

/* Equality is used to compare data to pack, in particular to ensure that
 * two different yaml_data_t object that are packed in the same subfile are
 * equal. Therefore, this functions tells whether two YAML data objects would
 * pack the same way without presentation data, but is not a strong equality
 * test.
 * This is implemented only on actual values and not on presentation data.
 * This is because the presentation objects cannot be modified outside of this
 * file. If this changes, for example to allow the user to modify presentation
 * data himself, then this function *must* be updated to take presentation
 * data into account.
 */
static bool
yaml_scalar_equals(const yaml_scalar_t * nonnull s1,
                   const yaml_scalar_t * nonnull s2)
{
    if (s1->type != s2->type) {
        return false;
    }

    switch (s1->type) {
      case YAML_SCALAR_STRING:
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
                 const yaml_data_t * nonnull d2)
{
    if (d1->type != d2->type) {
        return false;
    }

    switch (d1->type) {
      case YAML_DATA_SCALAR:
        return yaml_scalar_equals(&d1->scalar, &d2->scalar);
      case YAML_DATA_SEQ:
        if (d1->seq->datas.len != d2->seq->datas.len) {
            return false;
        }
        tab_for_each_pos(pos, &d1->seq->datas) {
            if (!yaml_data_equals(&d1->seq->datas.tab[pos],
                                  &d2->seq->datas.tab[pos]))
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
            if (!lstr_equal(d1->obj->fields.tab[pos].key,
                            d2->obj->fields.tab[pos].key))
            {
                return false;
            }
            if (!yaml_data_equals(&d1->obj->fields.tab[pos].data,
                                  &d2->obj->fields.tab[pos].data))
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
            lstr_t span = yaml_data_get_span_lstr(&data->span);

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
t_yaml_env_parse_tag(yaml_parse_t *env, const uint32_t min_indent,
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
    out->tag_span = t_new(yaml_span_t, 1);
    yaml_span_init(out->tag_span, env, tag_pos_start, tag_pos_end);

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

static int t_yaml_env_handle_include(yaml_parse_t * nonnull env,
                                     yaml_data_t * nonnull data)
{
    yaml_parse_t *subfile = NULL;
    yaml_data_t subdata;
    char dirpath[PATH_MAX];
    SB_1k(err);

    if (!lstr_equal(data->tag, LSTR("include"))) {
        return 0;
    }

    RETHROW(yaml_env_ltrim(env));

    if (data->type != YAML_DATA_SCALAR
    ||  data->scalar.type != YAML_SCALAR_STRING)
    {
        sb_sets(&err, "!include can only be used with strings");
        goto err;
    }

    path_dirname(dirpath, PATH_MAX, env->fullpath.s ?: "");

    logger_trace(&_G.logger, 2, "parsing subfile %pL", &data->scalar.s);
    subfile = t_yaml_parse_new(YAML_PARSE_GEN_PRES_DATA);
    if (t_yaml_parse_attach_file(subfile, t_fmt("%pL", &data->scalar.s),
                                 dirpath, &err) < 0)
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

    if (t_yaml_parse(subfile, &subdata, &err) < 0) {
        /* no call to yaml_env_set_err, because the generated error message
         * will already have all the including details. */
        env->err = subfile->err;
        return -1;
    }

    if (env->pres) {
        yaml__presentation_include__t *inc;

        inc = t_iop_new(yaml__presentation_include);
        inc->include_presentation = data->presentation;
        inc->path = data->scalar.s;
        inc->document_presentation = *t_yaml_data_get_presentation(&subdata);

        /* XXX: create a new presentation node for subdata, that indicates it
         * is included. We should not modify the existing presentation node
         * (if it exists), as it indicates the presentation of the subdata
         * in the subfile, and was saved in "inc->presentation". */
        subdata.presentation = t_iop_new(yaml__presentation_node);
        subdata.presentation->included = inc;
    }

    *data = subdata;
    return 0;

  err:
    yaml_env_set_err_at(env, &data->span, YAML_ERR_INVALID_INCLUDE,
                        t_fmt("%pL", &err));
    return -1;
}

/* }}} */
/* {{{ Seq */

static int t_yaml_env_parse_seq(yaml_parse_t *env, const uint32_t min_indent,
                                yaml_data_t *out)
{
    qv_t(yaml_data) datas;
    qv_t(yaml_pres_node) pres;
    yaml_pos_t pos_end = {0};

    t_qv_init(&datas, 0);
    t_qv_init(&pres, 0);

    assert (ps_startswith_yaml_seq_prefix(&env->ps));
    yaml_env_start_data(env, YAML_DATA_SEQ, out);

    for (;;) {
        yaml__presentation_node__t *node = NULL;
        yaml_data_t elem;
        uint32_t last_indent;

        RETHROW(yaml_env_ltrim(env));
        if (env->pres) {
            node = env->pres->next_node;
            env->pres->next_node = NULL;
            env->pres->last_node = &node;
        }

        /* skip '-' */
        yaml_env_skipc(env);

        RETHROW(t_yaml_env_parse_data(env, min_indent + 1, &elem));
        RETHROW(yaml_env_ltrim(env));

        pos_end = elem.span.end;
        qv_append(&datas, elem);
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

    yaml_env_end_data_with_pos(env, pos_end, out);
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
        *node = env->pres->next_node;
        env->pres->next_node = NULL;
        env->pres->last_node = node;
    }

    ps_key = ps_get_span(&env->ps, &ctype_isalnum);
    yaml_span_init(key_span, env, key_pos_start, yaml_env_get_pos(env));

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
t_yaml_env_parse_obj(yaml_parse_t *env, const uint32_t min_indent,
                     yaml_data_t *out)
{
    qv_t(yaml_key_data) fields;
    yaml_pos_t pos_end = {0};
    qh_t(lstr) keys_hash;

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

        pos_end = kd->data.span.end;
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
t_yaml_env_parse_quoted_string(yaml_parse_t *env, yaml_data_t *out)
{
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
        yaml_env_end_data(env, out);
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

static int t_yaml_env_parse_scalar(yaml_parse_t *env, bool in_flow,
                                   yaml_data_t *out)
{
    lstr_t line;
    pstream_t ps_line;

    yaml_env_start_data(env, YAML_DATA_SCALAR, out);
    if (ps_peekc(env->ps) == '"') {
        return t_yaml_env_parse_quoted_string(env, out);
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
/* {{{ Data */

static int t_yaml_env_parse_data(yaml_parse_t *env, const uint32_t min_indent,
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
        RETHROW(t_yaml_env_handle_include(env, out));
    } else
    if (ps_startswith_yaml_seq_prefix(&env->ps)) {
        RETHROW(t_yaml_env_parse_seq(env, cur_indent, out));
    } else
    if (ps_peekc(env->ps) == '[') {
        RETHROW(t_yaml_env_parse_flow_seq(env, out));
        t_yaml_env_pres_set_flow_mode(env);
    } else
    if (ps_peekc(env->ps) == '{') {
        RETHROW(t_yaml_env_parse_flow_obj(env, out));
        t_yaml_env_pres_set_flow_mode(env);
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

    path_extend(fullpath, dirpath ?: "", "%s", filepath);
    path_simplify(fullpath);

    /* detect includes that are not contained in the same directory */
    if (dirpath) {
        char relative_path[PATH_MAX];

        /* to work with path_relative_to, dirpath must end with a '/' */
        dirpath = t_fmt("%s/", dirpath);

        path_relative_to(relative_path, dirpath, fullpath);
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
    env->fullpath = t_lstr_dup(LSTR(fullpath));
    yaml_parse_attach_ps(env, ps_initlstr(&env->file_contents));

    return 0;
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

  end:
    if (res < 0) {
        sb_setsb(out_err, &env->err);
    }
    /* reset the stream to the input, so that it can be properly returned
     * by yaml_parse_get_stream(). */
    env->ps = saved_ps;
    return res;
}

const yaml__document_presentation__t * nonnull
t_yaml_data_get_presentation(const yaml_data_t * nonnull data)
{
    yaml__document_presentation__t *pres;
    qv_t(pres_mapping) mappings;
    t_SB_1k(path);

    pres = t_iop_new(yaml__document_presentation);
    t_qv_init(&mappings, 0);
    t_yaml_add_pres_mappings(data, &path, &mappings);
    pres->mappings = IOP_TYPED_ARRAY_TAB(yaml__presentation_node_mapping,
                                         &mappings);

    return pres;
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

    one_liner = span->end.line_nb == span->start.line_nb;

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
    for (unsigned i = 1; i < span->start.col_nb; i++) {
        sb_addc(out, ' ');
    }
    if (one_liner) {
        for (unsigned i = span->start.col_nb; i < span->end.col_nb; i++) {
            sb_addc(out, '^');
        }
    } else {
        sb_adds(out, "^ starting here");
    }
}

/* }}} */
/* {{{ Packer */

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

qm_kvec_t(path_to_yaml_data, lstr_t, const yaml_data_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

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

    /* Current path being packed. */
    sb_t current_path;

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
     * Associates paths to created subfiles with the yaml_data_t object packed
     * in each subfile. This is used to handle shared subfiles.
     */
    qm_t(path_to_yaml_data) * nullable subfiles;
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

    prev_len = env->current_path.len;
    va_start(args, fmt);
    sb_addvf(&env->current_path, fmt, args);
    va_end(args);

    return prev_len;
}

static void
yaml_pack_env_pop_path(yaml_pack_env_t * nullable env, int prev_len)
{
    if (!env->pres) {
        return;
    }

    sb_clip(&env->current_path, prev_len);
}

static const yaml__presentation_node__t * nullable
yaml_pack_env_get_pres_node(yaml_pack_env_t * nullable env)
{
    lstr_t path = LSTR_SB_V(&env->current_path);

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

static int yaml_pack_string(yaml_pack_env_t *env, lstr_t val)
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

static int yaml_pack_scalar(yaml_pack_env_t * nonnull env,
                            const yaml_scalar_t * nonnull scalar,
                            const lstr_t tag)
{
    int res = 0;
    char ibuf[IBUF_LEN];

    GOTO_STATE(CLEAN);

    switch (scalar->type) {
      case YAML_SCALAR_STRING:
        res += yaml_pack_string(env, scalar->s);
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
    }

    env->state = PACK_STATE_AFTER_DATA;

    return res;
}

/* }}} */
/* {{{ Pack sequence */

static int t_yaml_pack_seq(yaml_pack_env_t * nonnull env,
                           const yaml_seq_t * nonnull seq)
{
    int res = 0;

    if (seq->datas.len == 0) {
        GOTO_STATE(CLEAN);
        PUTS("[]");
        env->state = PACK_STATE_AFTER_DATA;
        return res;
    }

    tab_for_each_pos(pos, &seq->datas) {
        const yaml__presentation_node__t *node = NULL;
        const yaml_data_t *data = &seq->datas.tab[pos];
        int path_len = 0;

        if (env->pres) {
            path_len = yaml_pack_env_push_path(env, "[%d]", pos);
            node = yaml_pack_env_get_pres_node(env);
        } else
        if (pos < seq->pres_nodes.len && seq->pres_nodes.tab[pos]) {
            node = seq->pres_nodes.tab[pos];
        }
        res += yaml_pack_pres_node_prefix(env, node);

        GOTO_STATE(ON_DASH);
        PUTS("-");

        env->indent_lvl += YAML_STD_INDENT;
        res += yaml_pack_pres_node_inline(env, node);
        res += RETHROW(t_yaml_pack_data(env, data));
        env->indent_lvl -= YAML_STD_INDENT;

        yaml_pack_env_pop_path(env, path_len);
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

    if (env->pres) {
        path_len = yaml_pack_env_push_path(env, ".%pL", &kd->key);
        node = yaml_pack_env_get_pres_node(env);
    } else {
        node = kd->key_presentation;
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

    yaml_pack_env_pop_path(env, path_len);

    return res;
}

static int yaml_pack_obj(yaml_pack_env_t * nonnull env,
                         const yaml_obj_t * nonnull obj)
{
    int res = 0;

    if (obj->fields.len == 0) {
        GOTO_STATE(CLEAN);
        PUTS("{}");
        env->state = PACK_STATE_AFTER_DATA;
    } else {
        tab_for_each_ptr(pair, &obj->fields) {
            res += RETHROW(t_yaml_pack_key_data(env, pair));
        }
    }

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

    if (data->tag.s) {
        /* cannot pack in flow mode with a tag: the tag will be lost. */
        /* TODO: something must be done about this case rather than simply
         * ignoring the tag... At the very least, revert the flow packing
         * and pack it with the normal syntax. */
    }

    switch (data->type) {
      case YAML_DATA_SCALAR:
        res += RETHROW(yaml_pack_scalar(env, &data->scalar, LSTR_NULL_V));
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
/* {{{ Pack include */

enum subfile_status_t {
    SUBFILE_TO_CREATE,
    SUBFILE_TO_REUSE,
    SUBFILE_TO_IGNORE,
};

/* check if data can be packed in the subfile given from its relative path
 * from the env outdir */
static enum subfile_status_t
check_subfile(yaml_pack_env_t * nonnull env, const yaml_data_t * nonnull data,
              const char * nonnull relative_path)
{
    char fullpath[PATH_MAX];
    lstr_t path;
    int pos;

    /* compute new outdir */
    path_extend(fullpath, env->outdirpath.s, "%s", relative_path);
    path = LSTR(fullpath);

    assert (env->subfiles);
    pos = qm_put(path_to_yaml_data, env->subfiles, &path, data, 0);
    if (pos & QHASH_COLLISION) {
        pos &= ~QHASH_COLLISION;
        /* TODO: this may be optimized in some way with some sort of hashing,
         * to avoid calling yaml_data_equals repeatedly. */
        if (!yaml_data_equals(env->subfiles->values[pos], data)) {
            return SUBFILE_TO_IGNORE;
        } else {
            return SUBFILE_TO_REUSE;
        }
    } else {
        env->subfiles->keys[pos] = t_lstr_dup(path);
        return SUBFILE_TO_CREATE;
    }
}

static const char * nullable
t_find_right_path(yaml_pack_env_t * nonnull env,
                  const yaml_data_t * nonnull data, lstr_t initial_path,
                  bool * nonnull reuse)
{
    const char *ext;
    char *path;
    lstr_t base;
    int counter = 1;

    path = t_fmt("%pL", &initial_path);
    path_simplify(path);

    ext = path_ext(path);
    base = ext ? LSTR_PTR_V(path, ext) : LSTR(path);

    /* check base.ext, base~1.ext, etc until either the file does not exist,
     * or the data to pack is identical to the data packed in the subfile. */
    for (;;) {
        switch (check_subfile(env, data, path)) {
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

static int
t_yaml_pack_include_path(yaml_pack_env_t * nonnull env,
                         const yaml__presentation_node__t *pres,
                         lstr_t include_path)
{
    const yaml_presentation_t *saved_pres = env->pres;
    yaml_data_t data;
    int res;

    yaml_data_set_string(&data, include_path);
    data.tag = LSTR("include");
    data.presentation = unconst_cast(yaml__presentation_node__t, pres);

    /* Make sure the presentation data is not used as the paths won't be
     * correct when packing this data. */
    env->pres = NULL;
    res = t_yaml_pack_data(env, &data);
    env->pres = saved_pres;

    return res;
}

static int
t_yaml_pack_inclusion(yaml_pack_env_t * nonnull env,
                      yaml__presentation_include__t * nonnull inc,
                      const yaml_data_t * nonnull subdata)
{
    const char *path;
    bool reuse;

    /* This is checked on parsing. Unless the presentation data has been
     * modified by hand, which is not possible yet, this should not fail. */
    if (!expect(!lstr_startswith(inc->path, LSTR("..")))) {
        sb_setf(&env->err, "subfile `%pL` is not contained in the output "
                "directory `%pL`, this is not allowed", &inc->path,
                &env->outdirpath);
        return -1;
    }

    path = t_find_right_path(env, subdata, inc->path, &reuse);
    if (!reuse) {
        yaml_pack_env_t *subenv = t_yaml_pack_env_new();
        SB_1k(err);

        RETHROW(t_yaml_pack_env_set_outdir(subenv, env->outdirpath.s, &err));
        yaml_pack_env_set_presentation(subenv, &inc->document_presentation);

        /* Make sure the subfiles qm is shared, so that if this subfile also
         * generate other subfiles, it is properly handled. */
        subenv->subfiles = env->subfiles;

        logger_trace(&_G.logger, 2, "packing subfile %s", path);
        if (t_yaml_pack_file(subenv, path, subdata, &err) < 0) {
            sb_setf(&env->err, "error when packing subfile `%s`: %pL", path,
                    &err);
            return -1;
        }

    }

    return t_yaml_pack_include_path(env, inc->include_presentation,
                                    LSTR(path));
}

static int
t_yaml_pack_included_data(yaml_pack_env_t * nonnull env,
                          const yaml_data_t * nonnull data,
                          const yaml__presentation_node__t * nonnull node)
{
    yaml__presentation_include__t *inc;

    inc = node->included;
    if (env->flags & YAML_PACK_NO_SUBFILES) {
        return t_yaml_pack_include_path(env, inc->include_presentation,
                                        inc->path);
    } else
    /* Only create subfiles if an outdir is set, ie if we are packing into
     * files. */
    if (env->outdirpath.s) {
        return t_yaml_pack_inclusion(env, inc, data);
    } else {
        const yaml_presentation_t *saved_pres = env->pres;
        SB_1k(new_path);
        sb_t saved_path;
        int res;

        /* Inline the contents of the included data directly in the current
         * stream. This is as easy as just packing data, but we need to also
         * use the presentation data from the included files. To do so, the
         * current_path must be reset. */

        saved_path = env->current_path;

        env->pres = t_yaml_doc_pres_to_map(&inc->document_presentation);
        env->current_path = new_path;
        res = t_yaml_pack_data(env, data);

        env->current_path = saved_path;
        env->pres = saved_pres;

        return res;
    }
}

/* }}} */
/* {{{ Pack data */

static int t_yaml_pack_data(yaml_pack_env_t * nonnull env,
                            const yaml_data_t * nonnull data)
{
    const yaml__presentation_node__t *node;
    int res = 0;

    if (env->pres) {
        int path_len = yaml_pack_env_push_path(env, "!");

        node = yaml_pack_env_get_pres_node(env);
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
    }

    res += yaml_pack_tag(env, data->tag);

    if (node && node->flow_mode) {
        GOTO_STATE(CLEAN);
        res += yaml_pack_flow_data(env, data, false);
        env->state = PACK_STATE_AFTER_DATA;
    } else {
        switch (data->type) {
          case YAML_DATA_SCALAR: {
            res += RETHROW(yaml_pack_scalar(env, &data->scalar, data->tag));
          } break;
          case YAML_DATA_SEQ:
            res += RETHROW(t_yaml_pack_seq(env, data->seq));
            break;
          case YAML_DATA_OBJ:
            res += RETHROW(yaml_pack_obj(env, data->obj));
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

    t_sb_init(&env->current_path, 1024);
    t_sb_init(&env->err, 1024);

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

void yaml_pack_env_set_presentation(
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

    sb_init(&env->current_path);
    res = t_yaml_pack_data(env, data);
    sb_wipe(&env->current_path);
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

void t_yaml_pack_sb(yaml_pack_env_t * nonnull env,
                    const yaml_data_t * nonnull data, sb_t * nonnull sb)
{
    int res;

    res = t_yaml_pack(env, data, &sb_write, sb, NULL);
    /* Should not fail when packing into a sb */
    assert (res >= 0);
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

    if (env->outdirpath.s) {
        filename = t_fmt("%pL/%s", &env->outdirpath, filename);
    }

    /* Make sure the outdirpath is the full dirpath, even if it was set
     * before. */
    path_dirname(path, PATH_MAX, filename);
    RETHROW(t_yaml_pack_env_set_outdir(env, path, err));

    if (!env->subfiles) {
        env->subfiles = t_qm_new(path_to_yaml_data, 0);
    }

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
    yaml_parse_t *env = t_yaml_parse_new(0);
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
z_pack_yaml_file(const char *filepath, const yaml_data_t *data,
                 const yaml__document_presentation__t *presentation,
                 unsigned flags)
{
    t_scope;
    yaml_pack_env_t *env;
    char *path;
    SB_1k(err);

    env = t_yaml_pack_env_new();
    if (flags) {
        yaml_pack_env_set_flags(env, flags);
    }
    path = t_fmt("%pL/%s", &z_tmpdir_g, filepath);
    if (presentation) {
        yaml_pack_env_set_presentation(env, presentation);
    }
    Z_ASSERT_N(t_yaml_pack_file(env, path, data, &err),
               "cannot pack YAML file %s: %pL", filepath, &err);

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

/* out parameter first to let the yaml string be last, which makes it
 * much easier to write multiple lines without horrible indentation */
static
int z_t_yaml_test_parse_success(yaml_data_t * nullable data,
                                const yaml__document_presentation__t ** nullable pres,
                                yaml_parse_t * nonnull * nullable env,
                                const char * nonnull yaml,
                                const char * nullable expected_repack)
{
    const yaml__document_presentation__t *p;
    yaml_pack_env_t *pack_env;
    yaml_data_t local_data;
    yaml_parse_t *local_env = NULL;
    SB_1k(err);
    SB_1k(repack);

    if (!pres) {
        pres = &p;
    }
    if (!data) {
        data = &local_data;
    }
    if (!env) {
        env = &local_env;
    }

    *env = t_yaml_parse_new(YAML_PARSE_GEN_PRES_DATA);
    /* hack to make relative inclusion work in z_tmpdir_g */
    (*env)->fullpath = t_lstr_fmt("%pL/foo.yml", &z_tmpdir_g);
    yaml_parse_attach_ps(*env, ps_initstr(yaml));
    Z_ASSERT_N(t_yaml_parse(*env, data, &err),
               "yaml parsing failed: %pL", &err);

    if (!expected_repack) {
        expected_repack = yaml;
    }

    /* repack using presentation data from the AST */
    pack_env = t_yaml_pack_env_new();
    t_yaml_pack_sb(pack_env, data, &repack);
    Z_ASSERT_STREQUAL(repack.data, expected_repack,
                      "repacking the parsed data leads to differences");
    sb_reset(&repack);

    /* repack using yaml_presentation_t specification, and not the
     * presentation data inside the AST */
    *pres = t_yaml_data_get_presentation(data);
    pack_env = t_yaml_pack_env_new();
    yaml_pack_env_set_presentation(pack_env, *pres);
    t_yaml_pack_sb(pack_env, data, &repack);
    Z_ASSERT_STREQUAL(repack.data, expected_repack,
                      "repacking the parsed data leads to differences");

    if (local_env) {
        yaml_parse_delete(&local_env);
    }

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
                  const yaml__document_presentation__t * nullable presentation,
                  const char *yaml)
{
    t_scope;
    SB_1k(sb);
    yaml_pack_env_t *env = t_yaml_pack_env_new();

    if (presentation) {
        yaml_pack_env_set_presentation(env, presentation);
    }
    t_yaml_pack_sb(env, data, &sb);
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

/* }}} */

Z_GROUP_EXPORT(yaml)
{
    MODULE_REQUIRE(yaml);

    /* {{{ Parsing errors */

    Z_TEST(parsing_errors, "errors when parsing yaml") {
        /* unexpected EOF */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "",
            "<string>:1:1: missing data, unexpected end of line"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "  # my comment",

            "<string>:1:15: missing data, unexpected end of line\n"
            "  # my comment\n"
            "              ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "key:",

            "<string>:1:5: missing data, unexpected end of line\n"
            "key:\n"
            "    ^"
        ));

        /* wrong object continuation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 5\nb",

            "<string>:2:2: invalid key, missing colon\n"
            "b\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 5\n_:",

            "<string>:2:1: invalid key, "
            "only alpha-numeric characters allowed\n"
            "_:\n"
            "^"
        ));

        /* wrong explicit string */
        /* TODO: weird span? */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "\" unfinished string",

            "<string>:1:2: expected string, missing closing '\"'\n"
            "\" unfinished string\n"
            " ^"
        ));

        /* wrong escaped code */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "\"\\",

            "<string>:1:2: expected string, invalid backslash\n"
            "\"\\\n"
            " ^"
        ));

        /* wrong tag */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!-",

            "<string>:1:2: invalid tag, must start with a letter\n"
            "!-\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!a-\n"
            "a: 5",

            "<string>:1:3: invalid tag, "
            "must only contain alphanumeric characters\n"
            "!a-\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!4a\n"
            "a: 5",

            "<string>:1:2: invalid tag, must start with a letter\n"
            "!4a\n"
            " ^"
        ));
        /* TODO: improve span */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!tag1\n"
            "!tag2\n"
            "a: 2",

            "<string>:3:5: wrong object, two tags have been declared\n"
            "a: 2\n"
            "    ^"
        ));

        /* wrong list continuation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "- 2\n"
            "-3",

            "<string>:2:1: wrong type of data, "
            "expected another element of sequence\n"
            "-3\n"
            "^"
        ));

        /* wrong indent */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 2\n"
            " b: 3",

            "<string>:2:2: wrong indentation, "
            "line not aligned with current object\n"
            " b: 3\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "- 2\n"
            " - 3",

            "<string>:2:2: wrong indentation, "
            "line not aligned with current sequence\n"
            " - 3\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a: 1\n"
            "b:\n"
            "c: 3",

            "<string>:3:1: wrong indentation, missing element\n"
            "c: 3\n"
            "^"
        ));

        /* wrong object */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "foo: 1\n"
            "foo: 2",

            "<string>:2:1: invalid key, "
            "key is already declared in the object\n"
            "foo: 2\n"
            "^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{ a: 1, a: 2}",

            "<string>:1:9: invalid key, "
            "key is already declared in the object\n"
            "{ a: 1, a: 2}\n"
            "        ^"
        ));

        /* cannot use tab characters for indentation */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a:\t1",

            "<string>:1:3: tab character detected, "
            "cannot use tab characters for indentation\n"
            "a:\t1\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "a:\n"
            "\t- 2\n"
            "\t- 3",

            "<string>:2:1: tab character detected, "
            "cannot use tab characters for indentation\n"
            "\t- 2\n"
            "^"
        ));

        /* extra data after the parsing */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "1\n"
            "# comment\n"
            "2",

            "<string>:3:1: extra characters after data, "
            "expected end of document\n"
            "2\n"
            "^"
        ));

        /* flow seq */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "[a[",

            "<string>:1:3: wrong type of data, "
            "expected another element of sequence\n"
            "[a[\n"
            "  ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "[",

            "<string>:1:2: missing data, unexpected end of line\n"
            "[\n"
            " ^"
        ));

        /* flow obj */
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{,",

            "<string>:1:2: missing data, unexpected character\n"
            "{,\n"
            " ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{a:b}",

            "<string>:1:2: wrong type of data, "
            "only key-value mappings are allowed inside an object\n"
            "{a:b}\n"
            " ^^^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{a: b[",

            "<string>:1:6: wrong type of data, "
            "expected another element of object\n"
            "{a: b[\n"
            "     ^"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "{ a: b: c }",

            "<string>:1:7: wrong type of data, unexpected colon\n"
            "{ a: b: c }\n"
            "      ^"
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

        /* unexpected EOF */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "",
            "input.yml:2:1: missing data, unexpected end of line"
        ));

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
        /* !include tag must be applied on a string */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include 3",

            "input.yml:1:1: invalid include, "
            "!include can only be used with strings\n"
            "!include 3\n"
            "^^^^^^^^^^"
        ));

        /* unknown file */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include foo.yml",

            "input.yml:1:1: invalid include, "
            "cannot read file foo.yml: No such file or directory\n"
            "!include foo.yml\n"
            "^^^^^^^^^^^^^^^^"
        ));

        /* error in included file */
        Z_HELPER_RUN(z_write_yaml_file("has_errors.yml",
            "key: 1\n"
            "key: 2"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include has_errors.yml",

            "input.yml:1:1: error in included file\n"
            "!include has_errors.yml\n"
            "^^^^^^^^^^^^^^^^^^^^^^^\n"
            "has_errors.yml:2:1: invalid key, "
            "key is already declared in the object\n"
            "key: 2\n"
            "^^^"
        ));

        /* loop detection: include one-self */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include input.yml",

            "input.yml:1:1: invalid include, inclusion loop detected\n"
            "!include input.yml\n"
            "^^^^^^^^^^^^^^^^^^"
        ));
        /* loop detection: include a parent */
        Z_HELPER_RUN(z_write_yaml_file("loop-1.yml",
            "!include loop-2.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("loop-2.yml",
            "!include loop-3.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("loop-3.yml",
            "!include loop-1.yml"
        ));
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include loop-1.yml",

            "input.yml:1:1: error in included file\n"
            "!include loop-1.yml\n"
            "^^^^^^^^^^^^^^^^^^^\n"
            "loop-1.yml:1:1: error in included file\n"
            "!include loop-2.yml\n"
            "^^^^^^^^^^^^^^^^^^^\n"
            "loop-2.yml:1:1: error in included file\n"
            "!include loop-3.yml\n"
            "^^^^^^^^^^^^^^^^^^^\n"
            "loop-3.yml:1:1: invalid include, inclusion loop detected\n"
            "!include loop-1.yml\n"
            "^^^^^^^^^^^^^^^^^^^"
        ));

        /* includes must be in same directory as including file */
        Z_HELPER_RUN(z_yaml_test_file_parse_fail(
            "!include ../input.yml",

            "input.yml:1:1: invalid include, cannot include subfile "
            "`../input.yml`: only includes contained in the directory of the "
            "including file are allowed\n"
            "!include ../input.yml\n"
            "^^^^^^^^^^^^^^^^^^^^^"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include */

    Z_TEST(include, "") {
        t_scope;

        Z_HELPER_RUN(z_write_yaml_file("inner.yml",
            "- a: 3\n"
            "  b: { c: c }\n"
            "- true"
        ));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(NULL, NULL, NULL,
            "a: ~\n"
            "b: !include inner.yml\n"
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
            "- !include b.yml\n"
            "- !include subsub/d.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subdir/b.yml",
            "- !include subsub/c.yml\n"
            "- b"
        ));
        /* TODO d.yml is included twice, should be factorized instead of
         * parsing the file twice. */
        Z_HELPER_RUN(z_write_yaml_file("subdir/subsub/c.yml",
            "- c\n"
            "- !include d.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subdir/subsub/d.yml",
            "d"
        ));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(NULL, NULL, NULL,
            "!include subdir/a.yml",

            "- a\n"
            "- - - c\n"
            "    - d\n"
            "  - b\n"
            "- d"
        ));
    } Z_TEST_END;

    /* }}} */
    /* {{{ Include shared files */

    Z_TEST(include_shared_files, "") {
        t_scope;
        yaml_data_t data;
        const yaml__document_presentation__t *pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_create_tmp_subdir("sf/sub"));
        Z_HELPER_RUN(z_write_yaml_file("sf/shared_1.yml", "1"));
        Z_HELPER_RUN(z_write_yaml_file("sf/sub/shared_1.yml", "-1"));
        Z_HELPER_RUN(z_write_yaml_file("sf/shared_2",
            "!include sub/shared_1.yml"
        ));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres, &env,
            "- !include sf/shared_1.yml\n"
            "- !include sf/././shared_1.yml\n"
            "- !include sf/shared_1.yml\n"
            "- !include sf/sub/shared_1.yml\n"
            "- !include sf/../sf/sub/shared_1.yml\n"
            "- !include sf/sub/shared_1.yml\n"
            "- !include sf/shared_2\n"
            "- !include ./sf/shared_2",

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
        Z_HELPER_RUN(z_pack_yaml_file("sf-pack-1/root.yml", &data, pres, 0));
        Z_HELPER_RUN(z_check_file("sf-pack-1/root.yml",
            "- !include sf/shared_1.yml\n"
            "- !include sf/shared_1.yml\n"
            "- !include sf/shared_1.yml\n"
            "- !include sf/sub/shared_1.yml\n"
            "- !include sf/sub/shared_1.yml\n"
            "- !include sf/sub/shared_1.yml\n"
            "- !include sf/shared_2\n"
            "- !include sf/shared_2\n"
        ));
        Z_HELPER_RUN(z_check_file("sf-pack-1/sf/shared_1.yml", "1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-1/sf/sub/shared_1.yml", "-1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-1/sf/shared_2",
            "!include sub/shared_1.yml\n"
        ));

        /* modifying some data will force the repacking to create new files */
        data.seq->datas.tab[1].scalar.u = 2;
        data.seq->datas.tab[2].scalar.u = 2;
        data.seq->datas.tab[4].scalar.i = -2;
        data.seq->datas.tab[5].scalar.i = -3;
        data.seq->datas.tab[7].scalar.i = -3;
        Z_HELPER_RUN(z_create_tmp_subdir("sf-pack-2"));
        Z_HELPER_RUN(z_pack_yaml_file("sf-pack-2/root.yml", &data, pres, 0));
        Z_HELPER_RUN(z_check_file("sf-pack-2/root.yml",
            "- !include sf/shared_1.yml\n"
            "- !include sf/shared_1~1.yml\n"
            "- !include sf/shared_1~1.yml\n"
            "- !include sf/sub/shared_1.yml\n"
            "- !include sf/sub/shared_1~1.yml\n"
            "- !include sf/sub/shared_1~2.yml\n"
            "- !include sf/shared_2\n"
            "- !include sf/shared_2~1\n"
        ));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_1.yml", "1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_1~1.yml", "2\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/sub/shared_1.yml", "-1\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/sub/shared_1~1.yml", "-2\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/sub/shared_1~2.yml", "-3\n"));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_2",
            "!include sub/shared_1.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("sf-pack-2/sf/shared_2~1",
            "!include sub/shared_1~2.yml\n"
        ));
        yaml_parse_delete(&env);

    } Z_TEST_END;

    /* }}} */
    /* {{{ Include presentation */

    Z_TEST(include_presentation, "") {
        t_scope;
        yaml_data_t data;
        const yaml__document_presentation__t *pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_create_tmp_subdir("subpres/in"));
        Z_HELPER_RUN(z_write_yaml_file("subpres/1.yml",
            "# Included!\n"
            "!include in/sub.yml"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subpres/in/sub.yml",
            "[ 4, 2 ] # packed"
        ));
        Z_HELPER_RUN(z_write_yaml_file("subpres/weird~name",
            "jo: Jo\n"
            "# o\n"
            "o: ra"
        ));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres, &env,
            "- !include subpres/1.yml\n"
            "- !include subpres/weird~name",

            /* XXX: the presentation associated with the "!include" data is
             * not included, as the data is inlined. */
            "- [ 4, 2 ] # packed\n"
            "- jo: Jo\n"
            "  # o\n"
            "  o: ra"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("newsubdir/in"));
        Z_HELPER_RUN(z_pack_yaml_file("newsubdir/root.yml", &data, pres, 0));
        Z_HELPER_RUN(z_check_file("newsubdir/root.yml",
            "- !include subpres/1.yml\n"
            "- !include subpres/weird~name\n"
        ));
        Z_HELPER_RUN(z_check_file("newsubdir/subpres/1.yml",
            "# Included!\n"
            "!include in/sub.yml\n"
        ));
        Z_HELPER_RUN(z_check_file("newsubdir/subpres/in/sub.yml",
            "[ 4, 2 ] # packed\n"
        ));
        Z_HELPER_RUN(z_check_file("newsubdir/subpres/weird~name",
            "jo: Jo\n"
            "# o\n"
            "o: ra\n"
        ));
        yaml_parse_delete(&env);
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing scalars */

    Z_TEST(parsing_scalar, "test parsing of scalars") {
        t_scope;
        yaml_data_t data;

        /* string */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "unquoted string",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 16));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a string value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "!tag unquoted string",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 21));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged string value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "\" quoted: 5 \"",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 14));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR(" quoted: 5 "));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "  trimmed   ",
            "trimmed"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 3, 1, 10));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("trimmed"));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "a:x:b",
            "\"a:x:b\""
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 6));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("a:x:b"));

        /* null */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "~",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 2));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a null value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "!tag ~",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 7));
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged null value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "null",
            "~"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "NulL",
            "~"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        /* bool */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "true",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a boolean value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "!tag true",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 10));
        Z_ASSERT(data.scalar.b);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged boolean value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "TrUE",
            "true"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "false",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "FALse",
            "false"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        /* uint */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "0",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 2));
        Z_ASSERT_EQ(data.scalar.u, 0UL);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an unsigned integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "!tag 0",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 7));
        Z_ASSERT_EQ(data.scalar.u, 0UL);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged unsigned integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "153",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.u, 153UL);

        /* -0 will still generate UINT */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "-0",
            "0"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 3));

        /* int */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "-1",
            NULL));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 3));
        Z_ASSERT_EQ(data.scalar.i, -1L);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "an integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "!tag -1",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 8));
        Z_ASSERT_EQ(data.scalar.i, -1L);
        Z_ASSERT_LSTREQUAL(data.tag, LSTR("tag"));
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a tagged integer value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "-153",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.i, -153L);

        /* double */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "0.5",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.d, 0.5);
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT_STREQUAL(yaml_data_get_type(&data, false),
                          "a double value");

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "-1e3",
            "-1000"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.d, -1000.0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "-.Inf",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 6));
        Z_ASSERT_EQ(isinf(data.scalar.d), -1);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            ".INf",
            ".Inf"
        ));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(isinf(data.scalar.d), 1);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "[]",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 3));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.seq->datas.len == 0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "[ ~ ]",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 6));
        Z_ASSERT(data.seq->datas.len == 1);
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL,
                                         1, 3, 1, 4));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "[ ~, ]",
            "[ ~ ]"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 7));
        Z_ASSERT(data.seq->datas.len == 1);
        elem = &data.seq->datas.tab[0];
        Z_HELPER_RUN(z_check_yaml_scalar(elem, YAML_SCALAR_NULL,
                                         1, 3, 1, 4));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "{}",
            NULL
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 3));
        Z_ASSERT_NULL(data.tag.s);
        Z_ASSERT(data.obj->fields.len == 0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        const yaml__document_presentation__t *pres;
        yaml_parse_t *env;

        Z_HELPER_RUN(z_write_yaml_file("not_recreated.yml", "1"));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres, &env,
            "key: !include not_recreated.yml",

            "key: 1"
        ));

        Z_HELPER_RUN(z_create_tmp_subdir("flags"));
        Z_HELPER_RUN(z_pack_yaml_file("flags/root.yml", &data, pres,
                                      YAML_PACK_NO_SUBFILES));
        Z_HELPER_RUN(z_check_file("flags/root.yml",
            "key: !include not_recreated.yml\n"
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
        const yaml__document_presentation__t *doc_pres = NULL;
        const yaml_presentation_t *pres;

        /* comment on a scalar => not saved */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &doc_pres, NULL,
            "# my scalar\n"
            "3",
            NULL
        ));
        pres = t_yaml_doc_pres_to_map(doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        CHECK_PREFIX_COMMENTS(pres, LSTR("!"), LSTR("my scalar"));

        /* comment on a key => path is key */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &doc_pres, NULL,
            "a: 3 #ticket is #42  ",

            "a: 3 # ticket is #42\n"
        ));
        pres = t_yaml_doc_pres_to_map(doc_pres);
        Z_ASSERT_P(pres);
        Z_ASSERT_EQ(1, qm_len(yaml_pres_node, &pres->nodes));
        Z_HELPER_RUN(z_check_inline_comment(pres, LSTR(".a!"),
                                            LSTR("ticket is #42")));

        /* comment on a list => path is index */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &doc_pres, NULL,
            "# prefix comment\n"
            "- 1 # first\n"
            "- # item\n"
            "  2 # second\n",

            NULL
        ));
        pres = t_yaml_doc_pres_to_map(doc_pres);
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &doc_pres, NULL,
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
        pres = t_yaml_doc_pres_to_map(doc_pres);
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &doc_pres, NULL,
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
        pres = t_yaml_doc_pres_to_map(doc_pres);
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "# prefix key\n"
            "!toto 3",

            NULL
        ));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "# a\n"
            "a: # b\n"
            "  !foo b",

            NULL
        ));

        /* inline comments with flow data */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
            "- # prefix\n"
            "  1 # inline\n",
            NULL
        ));
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, NULL, NULL,
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
        const yaml__document_presentation__t *pres = NULL;

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres, NULL,
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

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres, NULL,
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, &pres, NULL,
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

    /* {{{ yaml_data_equals */

    Z_TEST(yaml_data_equals, "") {
        t_scope;
        yaml_data_t d1;
        yaml_data_t d2;
        yaml_data_t elem;

        yaml_data_set_string(&d1, LSTR("v"));
        yaml_data_set_bool(&d2, false);

        /* scalars with different types are never equal */
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        /* strings */
        yaml_data_set_string(&d2, LSTR("v"));
        Z_ASSERT(yaml_data_equals(&d1, &d2));
        yaml_data_set_string(&d2, LSTR("a"));
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        /* double */
        yaml_data_set_double(&d1, 1.2);
        yaml_data_set_double(&d2, 1.2);
        Z_ASSERT(yaml_data_equals(&d1, &d2));
        yaml_data_set_double(&d2, 1.20000001);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        /* uint */
        yaml_data_set_uint(&d1, 1);
        yaml_data_set_uint(&d2, 1);
        Z_ASSERT(yaml_data_equals(&d1, &d2));
        yaml_data_set_uint(&d2, 2);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        /* uint */
        yaml_data_set_int(&d1, -1);
        yaml_data_set_int(&d2, -1);
        Z_ASSERT(yaml_data_equals(&d1, &d2));
        yaml_data_set_int(&d2, -2);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        /* bool */
        yaml_data_set_bool(&d1, true);
        yaml_data_set_bool(&d2, true);
        Z_ASSERT(yaml_data_equals(&d1, &d2));
        yaml_data_set_int(&d2, false);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        /* null */
        yaml_data_set_null(&d1);
        yaml_data_set_null(&d2);
        Z_ASSERT(yaml_data_equals(&d1, &d2));

        /* sequences */
        t_yaml_data_new_seq(&d1, 1);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));
        t_yaml_data_new_seq(&d2, 1);
        Z_ASSERT(yaml_data_equals(&d1, &d2));

        yaml_data_set_string(&elem, LSTR("l"));
        yaml_seq_add_data(&d1, elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        yaml_data_set_string(&elem, LSTR("d"));
        yaml_seq_add_data(&d2, elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        d2.seq->datas.tab[0].scalar.s = LSTR("l");
        Z_ASSERT(yaml_data_equals(&d1, &d2));

        /* obj */
        t_yaml_data_new_obj(&d1, 2);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));
        t_yaml_data_new_obj(&d2, 2);
        Z_ASSERT(yaml_data_equals(&d1, &d2));

        yaml_data_set_bool(&elem, true);
        yaml_obj_add_field(&d1, LSTR("v"), elem);
        yaml_obj_add_field(&d1, LSTR("a"), elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        yaml_data_set_bool(&elem, false);
        yaml_obj_add_field(&d2, LSTR("v"), elem);
        yaml_obj_add_field(&d2, LSTR("a"), elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        qv_clear(&d2.obj->fields);
        yaml_data_set_bool(&elem, true);
        yaml_obj_add_field(&d2, LSTR("a"), elem);
        yaml_obj_add_field(&d2, LSTR("v"), elem);
        Z_ASSERT(!yaml_data_equals(&d1, &d2));

        qv_clear(&d2.obj->fields);
        yaml_obj_add_field(&d2, LSTR("v"), elem);
        yaml_obj_add_field(&d2, LSTR("a"), elem);
        Z_ASSERT(yaml_data_equals(&d1, &d2));

    } Z_TEST_END;

    /* }}} */

    MODULE_RELEASE(yaml);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
