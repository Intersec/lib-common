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

#include "yaml.h"
#include "parsing-helpers.h"
#include "log.h"

static struct yaml_g {
    logger_t logger;
} yaml_g = {
#define _G yaml_g
    .logger = LOGGER_INIT(NULL, "yaml", LOG_INHERITS),
};

/* {{{ Parsing types definitions */

typedef struct yaml_env_t {
    /* Memory pool to use for internal allocations. */
    mem_pool_t *mp;

    /* String to parse. */
    pstream_t ps;

    /* Current line number. */
    uint32_t line_number;

    /* Pointer to the newline character that started the current line (ie
     * the newline character separating the previous line from the current
     * one).
     * Used to compute current column number of ps->s */
    const char *pos_newline;

    /* Error buffer. */
    sb_t err;
} yaml_env_t;

/* }}} */
/* {{{ Utils */

static const char *yaml_scalar_get_type(const yaml_scalar_t *scalar)
{
    switch (scalar->type) {
      case YAML_SCALAR_STRING:
        return "a string value";
      case YAML_SCALAR_DOUBLE:
        return "a double value";
      case YAML_SCALAR_UINT:
        return "an unsigned integer value";
      case YAML_SCALAR_INT:
        return "an integer value";
      case YAML_SCALAR_BOOL:
        return "a boolean value";
      case YAML_SCALAR_NULL:
        return "a null value";
    }

    assert (false);
    return "";
}

const char *yaml_data_get_type(const yaml_data_t *data)
{
    switch (data->type) {
      case YAML_DATA_OBJ:
        return (data->obj->tag.s) ? "a tagged object" : "an object";
      case YAML_DATA_SEQ:
        return "a sequence";
      case YAML_DATA_SCALAR:
        return yaml_scalar_get_type(&data->scalar);
    }

    assert (false);
    return "";
}

static lstr_t yaml_data_get_span(const yaml_data_t *data)
{
    return LSTR_INIT_V(data->pos_start.s,
                       data->pos_end.s - data->pos_start.s);
}

static uint32_t yaml_env_get_column_nb(const yaml_env_t *env)
{
    return env->ps.s - env->pos_newline;
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
    out->pos_start = pos_start;
    out->pos_end = pos_end;
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
    }

    return -1;
}

/* }}} */
/* {{{ Parser */

static int yaml_env_parse_data(yaml_env_t *env, const uint32_t min_indent,
                               yaml_data_t *out);

static void yaml_env_ltrim(yaml_env_t *env)
{
    bool in_comment = false;

    while (!ps_done(&env->ps)) {
        int c = ps_peekc(env->ps);

        if (c == '#') {
            in_comment = true;
        } else
        if (c == '\n') {
            env->line_number++;
            env->pos_newline = env->ps.s;
            in_comment = false;
        } else
        if (!isspace(c) && !in_comment) {
            break;
        }
        yaml_env_skipc(env);
    }
}

static int
yaml_env_parse_tagged_obj(yaml_env_t *env, const uint32_t min_indent,
                          yaml_data_t *out)
{
    /* TODO: reject tags starting with anything but a letter */
    /* a-zA-Z0-9. */
    static const ctype_desc_t ctype_tag = { {
        0x00000000, 0x03ff4000, 0x07fffffe, 0x07fffffe,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    pstream_t tag;

    assert (ps_peekc(env->ps) == '!');
    yaml_env_skipc(env);

    tag = ps_get_span(&env->ps, &ctype_tag);
    if (ps_len(&tag) <= 0) {
        return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
                                "expected a string after '!'");
    }

    RETHROW(yaml_env_parse_data(env, min_indent, out));
    switch (out->type) {
      case YAML_DATA_SEQ:
      case YAML_DATA_SCALAR:
        return yaml_env_set_err(env, YAML_ERR_WRONG_DATA,
                                "can only use a tag on an object");
      case YAML_DATA_OBJ:
        if (out->obj->tag.s) {
            return yaml_env_set_err(env, YAML_ERR_WRONG_OBJECT,
                                    "two tags have been declared");
        }
        break;
    }

    out->obj->tag = LSTR_PS_V(&tag);
    out->pos_start = pos_start;
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

static int yaml_env_parse_seq(yaml_env_t *env, const uint32_t min_indent,
                              yaml_data_t *out)
{
    qv_t(yaml_data) datas;
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    yaml_pos_t pos_end = {0};

    mp_qv_init(env->mp, &datas, 0);

    assert (ps_startswith_yaml_seq_prefix(&env->ps));

    for (;;) {
        yaml_data_t elem;
        uint32_t last_indent;

        /* skip '-' */
        yaml_env_skipc(env);

        RETHROW(yaml_env_parse_data(env, min_indent + 1, &elem));
        pos_end = elem.pos_end;
        qv_append(&datas, elem);

        yaml_env_ltrim(env);
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
    out->seq = datas.tab;
    out->seq_len = datas.len;
    return 0;
}

static int yaml_env_parse_raw_obj(yaml_env_t *env, const uint32_t min_indent,
                                  yaml_data_t *out)
{
    qm_t(yaml_data) fields;
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    yaml_pos_t pos_end = {0};

    mp_qm_init(yaml_data, env->mp, &fields, 0);

    for (;;) {
        pstream_t ps_key;
        yaml_data_t val;
        lstr_t key;
        uint32_t last_indent;
        int32_t pos;

        ps_key = ps_get_span(&env->ps, &ctype_isalnum);
        if (ps_len(&ps_key) == 0) {
            return yaml_env_set_err(env, YAML_ERR_BAD_KEY,
                                    "only alpha-numeric characters allowed");
        } else
        if (ps_getc(&env->ps) != ':') {
            return yaml_env_set_err(env, YAML_ERR_BAD_KEY, "missing colon");
        }

        key = LSTR_PS_V(&ps_key);
        pos = qm_reserve(yaml_data, &fields, &key, 0);
        if (pos & QHASH_COLLISION) {
            return yaml_env_set_err(env, YAML_ERR_BAD_KEY,
                                    "key is already declared in the object");
        }

        RETHROW(yaml_env_parse_data(env, min_indent + 1, &val));

        fields.values[pos] = val;
        pos_end = val.pos_end;

        yaml_env_ltrim(env);
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
    out->obj = mp_new(env->mp, yaml_obj_t, 1);
    out->obj->fields = fields;
    return 0;
}

static pstream_t yaml_get_scalar_ps(pstream_t *ps)
{
    /* '\n' and '#' */
    static const ctype_desc_t ctype_scalarend = { {
        0x00000400, 0x00000008, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    } };
    pstream_t scalar;

    scalar = ps_get_cspan(ps, &ctype_scalarend);
    /* need to rtrim to remove extra spaces */
    ps_rtrim(&scalar);

    return scalar;
}

static int yaml_env_parse_scalar(yaml_env_t *env, yaml_data_t *out)
{
    lstr_t line;
    yaml_pos_t pos_start = yaml_env_get_pos(env);
    pstream_t span;

    if (ps_peekc(env->ps) == '"') {
        int line_nb = 0;
        int col_nb = 0;
        sb_t buf;
        parse_str_res_t res;

        yaml_env_skipc(env);
        mp_sb_init(env->mp, &buf, 128);
        /* XXX use a json util for escaping handling. To be factorized
         * outside of the json file however. */
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

    /* get scalar string, ie up to newline or comment */
    span = yaml_get_scalar_ps(&env->ps);
    /* this is caught by the ps_done check in the beginning of parse_data */
    assert (ps_len(&span) > 0);

    line = LSTR_PS_V(&span);
    yaml_env_init_data(env, YAML_DATA_SCALAR, pos_start, out);

    /* special strings */
    if (lstr_equal(line, LSTR("~"))
    ||  lstr_ascii_iequal(line, LSTR("null")))
    {
        out->scalar.type = YAML_SCALAR_NULL;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR("true"))) {
        out->scalar.type = YAML_SCALAR_BOOL;
        out->scalar.b = true;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR("false"))) {
        out->scalar.type = YAML_SCALAR_BOOL;
        out->scalar.b = false;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR("-.inf"))) {
        out->scalar.type = YAML_SCALAR_DOUBLE;
        out->scalar.d = -INFINITY;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR(".inf"))) {
        out->scalar.type = YAML_SCALAR_DOUBLE;
        out->scalar.d = INFINITY;
        return 0;
    } else
    if (lstr_ascii_iequal(line, LSTR(".nan"))) {
        out->scalar.type = YAML_SCALAR_DOUBLE;
        out->scalar.d = NAN;
        return 0;
    }

    /* try to parse it as a integer, then as a float. Otherwise, it is
     * a string */
    if (line.s[0] == '-') {
        int64_t i;

        if (lstr_to_int64(line, &i) == 0) {
            out->scalar.type = YAML_SCALAR_INT;
            out->scalar.i = i;
            return 0;
        }
    } else {
        uint64_t u;

        if (lstr_to_uint64(line, &u) == 0) {
            out->scalar.type = YAML_SCALAR_UINT;
            out->scalar.u = u;
            return 0;
        }
    }

    {
        double d;

        if (lstr_to_double(line, &d) == 0) {
            out->scalar.type = YAML_SCALAR_DOUBLE;
            out->scalar.d = d;
            return 0;
        }
    }

    out->scalar.type = YAML_SCALAR_STRING;
    out->scalar.s = line;
    return 0;
}

static int yaml_env_parse_data(yaml_env_t *env, const uint32_t min_indent,
                               yaml_data_t *out)
{
    pstream_t saved_ps;
    pstream_t key;
    uint32_t cur_indent;

    yaml_env_ltrim(env);
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
        RETHROW(yaml_env_parse_tagged_obj(env, min_indent, out));
        goto end;
    }

    if (ps_startswith_yaml_seq_prefix(&env->ps)) {
        RETHROW(yaml_env_parse_seq(env, cur_indent, out));
        goto end;
    }

    /* try to parse a key */
    saved_ps = env->ps;
    key = ps_get_span(&env->ps, &ctype_isalnum);
    if (ps_len(&key) > 0 && ps_peekc(env->ps) == ':') {
        env->ps = saved_ps;
        RETHROW(yaml_env_parse_raw_obj(env, cur_indent, out));
        goto end;
    }
    env->ps = saved_ps;

    /* otherwise, parse the line as a scalar */
    RETHROW(yaml_env_parse_scalar(env, out));

  end:
    if (logger_is_traced(&_G.logger, 2)) {
        logger_trace_scope(&_G.logger, 2);
        logger_cont("parsed %s from "YAML_POS_FMT" up to "YAML_POS_FMT,
                    yaml_data_get_type(out), YAML_POS_ARG(out->pos_start),
                    YAML_POS_ARG(out->pos_end));
        if (out->type == YAML_DATA_SCALAR) {
            logger_cont(": %*pM", LSTR_FMT_ARG(yaml_data_get_span(out)));
        }
    }
    return 0;
}

/* }}} */
/* {{{ Public API */

int t_yaml_parse(pstream_t ps, yaml_data_t *out, sb_t *out_err)
{
    yaml_env_t env;
    t_SB_1k(err);

    p_clear(&env, 1);
    env.mp = t_pool();
    env.err = err;

    env.ps = ps;
    env.line_number = 1;
    /* -1 so that the computation of the column number works for the first
     * line */
    env.pos_newline = ps.s - 1;

    if (yaml_env_parse_data(&env, 0, out) < 0) {
        sb_setsb(out_err, &env.err);
        return -1;
    }

    return 0;
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

#include "z.h"

/* {{{ Helpers */

static int z_yaml_test_parse_fail(const char *yaml, const char *expected_err)
{
    t_scope;
    yaml_data_t data;
    SB_1k(err);

    Z_ASSERT_NEG(t_yaml_parse(ps_initstr(yaml), &data, &err));
    Z_ASSERT_STREQUAL(err.data, expected_err,
                      "wrong error message on yaml string `%s`", yaml);

    Z_HELPER_END;
}

/* out parameter first to let the yaml string be last, which makes it
 * much easier to write multiple lines without horrible indentation */
static int z_t_yaml_test_parse_success(yaml_data_t *data, const char *yaml)
{
    SB_1k(err);

    Z_ASSERT_N(t_yaml_parse(ps_initstr(yaml), data, &err),
               "yaml parsing failed: %pL", &err);

    Z_HELPER_END;
}

static int
z_check_yaml_data(const yaml_data_t *data, yaml_data_type_t type,
                  uint32_t start_line, uint32_t start_col,
                  uint32_t end_line, uint32_t end_col)
{
    Z_ASSERT_EQ(data->type, type);
    Z_ASSERT_EQ(data->pos_start.line_nb, start_line);
    Z_ASSERT_EQ(data->pos_start.col_nb, start_col);
    Z_ASSERT_EQ(data->pos_end.line_nb, end_line);
    Z_ASSERT_EQ(data->pos_end.col_nb, end_col);

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

/* }}} */

/* Unit-tests about YAML->AST parsing. */

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
            "1:2: wrong type of data, expected a string after '!'"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!\"my type\"\n"
            "a: 5",
            "1:2: wrong type of data, expected a string after '!'"
        ));
        Z_HELPER_RUN(z_yaml_test_parse_fail(
            "!tag\n"
            "3",
            "2:2: wrong type of data, can only use a tag on an object"
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
    } Z_TEST_END;

    /* }}} */
    /* {{{ Parsing scalars */

    Z_TEST(parsing_scalar, "test parsing of scalars") {
        t_scope;
        yaml_data_t data;

        /* string */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "unquoted string"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 16));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR("unquoted string"));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "\" quoted: 5 \""));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_STRING,
                                         1, 1, 1, 14));
        Z_ASSERT_LSTREQUAL(data.scalar.s, LSTR(" quoted: 5 "));

        /* null */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "~"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 2));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "null"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "NulL"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_NULL,
                                         1, 1, 1, 5));

        /* bool */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "true"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "TrUE"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 5));
        Z_ASSERT(data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "false"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "FALse"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_BOOL,
                                         1, 1, 1, 6));
        Z_ASSERT(!data.scalar.b);

        /* uint */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "0"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 2));
        Z_ASSERT_EQ(data.scalar.u, 0UL);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "153"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_UINT,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.u, 153UL);

        /* int */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "-0"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 3));
        Z_ASSERT_EQ(data.scalar.i, 0L);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "-153"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_INT,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.i, -153L);

        /* double */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "0.5"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 4));
        Z_ASSERT_EQ(data.scalar.d, 0.5);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "-1e3"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(data.scalar.d, -1000.0);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, "-.Inf"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 6));
        Z_ASSERT_EQ(isinf(data.scalar.d), -1);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, ".INf"));
        Z_HELPER_RUN(z_check_yaml_scalar(&data, YAML_SCALAR_DOUBLE,
                                         1, 1, 1, 5));
        Z_ASSERT_EQ(isinf(data.scalar.d), 1);

        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data, ".NAN"));
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
        lstr_t key;

        /* one liner */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data,
            "a: 2"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 5));
        Z_ASSERT_NULL(data.obj->tag.s);
        Z_ASSERT(qm_len(yaml_data, &data.obj->fields) == 1);
        key = LSTR("a");
        field = qm_get(yaml_data, &data.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_UINT,
                                         1, 4, 1, 5));
        Z_ASSERT_EQ(field.scalar.u, 2UL);

        /* with tag */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data,
            "!tag1 a: 2"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 1, 11));
        Z_ASSERT_LSTREQUAL(data.obj->tag, LSTR("tag1"));
        Z_ASSERT(qm_len(yaml_data, &data.obj->fields) == 1);
        key = LSTR("a");
        field = qm_get(yaml_data, &data.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_UINT,
                                         1, 10, 1, 11));
        Z_ASSERT_EQ(field.scalar.u, 2UL);

        /* imbricated objects */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data,
            "a: 2\n"
            "inner: b: 3\n"
            "       c: -4\n"
            "inner2: !tag\n"
            "  d: ~\n"
            "  e: my-label\n"
            "f: 1.2"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_OBJ, 1, 1, 7, 7));
        Z_ASSERT_NULL(data.obj->tag.s);
        Z_ASSERT(qm_len(yaml_data, &data.obj->fields) == 4);

        /* a */
        key = LSTR("a");
        field = qm_get(yaml_data, &data.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field, YAML_SCALAR_UINT,
                                         1, 4, 1, 5));
        Z_ASSERT_EQ(field.scalar.u, 2UL);

        /* inner */
        key = LSTR("inner");
        field = qm_get(yaml_data, &data.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_data(&field, YAML_DATA_OBJ, 2, 8, 3, 13));
        Z_ASSERT_NULL(field.obj->tag.s);
        Z_ASSERT(qm_len(yaml_data, &field.obj->fields) == 2);

        key = LSTR("b");
        field2 = qm_get(yaml_data, &field.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_UINT,
                                         2, 11, 2, 12));
        Z_ASSERT_EQ(field2.scalar.u, 3UL);
        key = LSTR("c");
        field2 = qm_get(yaml_data, &field.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_INT,
                                         3, 11, 3, 13));
        Z_ASSERT_EQ(field2.scalar.i, -4L);

        /* inner2 */
        key = LSTR("inner2");
        field = qm_get(yaml_data, &data.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_data(&field, YAML_DATA_OBJ, 4, 9, 6, 14));
        Z_ASSERT_LSTREQUAL(field.obj->tag, LSTR("tag"));
        Z_ASSERT(qm_len(yaml_data, &field.obj->fields) == 2);

        key = LSTR("d");
        field2 = qm_get(yaml_data, &field.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_NULL,
                                         5, 6, 5, 7));
        key = LSTR("e");
        field2 = qm_get(yaml_data, &field.obj->fields, &key);
        Z_HELPER_RUN(z_check_yaml_scalar(&field2, YAML_SCALAR_STRING,
                                         6, 6, 6, 14));
        Z_ASSERT_LSTREQUAL(field2.scalar.s, LSTR("my-label"));

        /* f */
        key = LSTR("f");
        field = qm_get(yaml_data, &data.obj->fields, &key);
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
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data,
            "- a"
        ));
        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 1, 4));
        Z_ASSERT_EQ(data.seq_len, 1U);
        Z_HELPER_RUN(z_check_yaml_scalar(&data.seq[0], YAML_SCALAR_STRING,
                                         1, 3, 1, 4));
        Z_ASSERT_LSTREQUAL(data.seq[0].scalar.s, LSTR("a"));

        /* imbricated sequences */
        Z_HELPER_RUN(z_t_yaml_test_parse_success(&data,
            "- \"a: 2\"\n"
            "- - 5\n"
            "  - -5\n"
            "- ~\n"
            "-\n"
            "  - TRUE\n"
            "- FALSE\n"
        ));

        Z_HELPER_RUN(z_check_yaml_data(&data, YAML_DATA_SEQ, 1, 1, 7, 8));
        Z_ASSERT_EQ(data.seq_len, 5U);

        /* "a: 2" */
        elem = data.seq[0];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_STRING,
                                         1, 3, 1, 9));
        Z_ASSERT_LSTREQUAL(elem.scalar.s, LSTR("a: 2"));

        /* subseq */
        elem = data.seq[1];
        Z_HELPER_RUN(z_check_yaml_data(&elem, YAML_DATA_SEQ, 2, 3, 3, 7));
        Z_ASSERT_EQ(elem.seq_len, 2U);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq[0], YAML_SCALAR_UINT,
                                         2, 5, 2, 6));
        Z_ASSERT_EQ(elem.seq[0].scalar.u, 5UL);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq[1], YAML_SCALAR_INT,
                                         3, 5, 3, 7));
        Z_ASSERT_EQ(elem.seq[1].scalar.i, -5L);

        /* null */
        elem = data.seq[2];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_NULL,
                                         4, 3, 4, 4));

        /* subseq */
        elem = data.seq[3];
        Z_HELPER_RUN(z_check_yaml_data(&elem, YAML_DATA_SEQ, 6, 3, 6, 9));
        Z_ASSERT_EQ(elem.seq_len, 1U);
        Z_HELPER_RUN(z_check_yaml_scalar(&elem.seq[0], YAML_SCALAR_BOOL,
                                         6, 5, 6, 9));
        Z_ASSERT(elem.seq[0].scalar.b);

        /* false */
        elem = data.seq[4];
        Z_HELPER_RUN(z_check_yaml_scalar(&elem, YAML_SCALAR_BOOL,
                                         7, 3, 7, 8));
        Z_ASSERT(!elem.scalar.b);
    } Z_TEST_END;

    /* }}} */

    MODULE_RELEASE(yaml);
} Z_GROUP_END

/* LCOV_EXCL_STOP */

/* }}} */
