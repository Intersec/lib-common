%{
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

#include <lib-common/unix.h>
#include "iopc.h"

struct lexdata {
    int   context;     /* state we just left, used by <STRING> state */
    int   lno, lstart; /* line no 1-based, position of the last \n   */
    int   pos, len;    /* stream length, 0-based, and copy of yyleng */
    int   prev_pos, prev_len;
    int   done;

    const char *path;
    FILE *fd;
    YY_BUFFER_STATE buffer;
    sb_t buf;
    yyscan_t scanner;

    iopc_token_t *tk;

    sb_t  *dox_cur_paragraph;
    int    dox_sp_count;
    int    dox_scan_params_args_counter;
    bool   dox_inline_comment : 1;
};

/* helpers {{{ */

static int lexdata_relpos(struct lexdata *ld, int offs)
{
    if (!ld->len) {
        /* empty file */
        return ld->pos;
    }
    if (offs >= 0) {
        assert (offs <= ld->len);
        return ld->pos + offs;
    }
    assert (ld->len + offs >= 0);
    return ld->pos + offs + ld->len;
}

static void lexdata_newline(struct lexdata *ld, int offs)
{
    ld->lno++;
    ld->lstart = lexdata_relpos(ld, offs);
}

static int lexdata_col(struct lexdata *ld, int offs)
{
    return 1 + lexdata_relpos(ld, offs) - ld->lstart;
}

static const char *get_integer_error(int err)
{
    switch (err) {
      case EDOM:
        return "invalid integer extension";
      case ERANGE:
        return "integer overflow";
    }
    return "unknown integer error";
}

static void lexdata_read_loc(struct lexdata *ld, iopc_loc_t *loc)
{
    *loc = (iopc_loc_t){
        .file = ld->path,
        .lmin = ld->lno,
        .lmax = ld->lno,
        .cmin = lexdata_col(ld, 0),
        .cmax = lexdata_col(ld, -1),
    };
}

static iopc_token_t *token_new(iopc_tok_type_t type, struct lexdata *ld)
{
    iopc_token_t *tk = iopc_token_new();

    lexdata_read_loc(ld, &tk->loc);
    tk->token = type;
    return tk;
}
#define token_new(type)    ((token_new)(type, yyextra))

static
void lexdox_comment_begin(struct lexdata *ld, const char *text, int len)
{
    assert (len >= 3);

    ld->tk = (token_new)(ITOK_DOX_COMMENT, ld);
    ld->tk->dox = dox_tok_new();

    /* strip trailing spaces */
    while (isspace(text[len - 1])) {
        len--;
    }
    ld->tk->dox->is_back = (text[len - 1] == '<');

    ld->dox_cur_paragraph = NULL;
    ld->dox_sp_count = 0;
    ld->dox_scan_params_args_counter = 0;
    ld->dox_inline_comment = (text[1] == '/');
}

static void lexdox_update_last_chunk_loc(const struct lexdata *ld)
{
    if (ld->tk->dox->chunks.len) {
        tab_last(&ld->tk->dox->chunks)->loc.lmax = ld->lno;
    }
}

static void debug_dump_dox(const struct lexdata *ld)
{
#define DEBUG_LVL  4

    e_trace(DEBUG_LVL, "DOX COMMENT, is_back: %d, file: %s",
            ld->tk->dox->is_back, ld->path);

    tab_for_each_ptr(chunk, &ld->tk->dox->chunks) {
        e_trace(DEBUG_LVL, "chunk, lmin: %d, lmax: %d, "
                "keyword: %*pM, paragraph0_args_len: %d "
                "first_sentence_len: %d", chunk->loc.lmin, chunk->loc.lmax,
                LSTR_FMT_ARG(chunk->keyword), chunk->paragraph0_args_len,
                chunk->first_sentence_len);
        tab_for_each_entry(param, &chunk->params) {
            e_trace(DEBUG_LVL, "param: %*pM", LSTR_FMT_ARG(param));
        }
        tab_for_each_entry(arg, &chunk->params_args) {
            e_trace(DEBUG_LVL, "params_arg: %*pM", LSTR_FMT_ARG(arg));
        }
        tab_for_each_ptr(paragraph, &chunk->paragraphs) {
            e_trace(DEBUG_LVL, "paragraph: %*pM", SB_FMT_ARG(paragraph));
        }
    }

#undef DEBUG_LVL
}

static void lexdox_comment_end(const struct lexdata *ld)
{
    tab_for_each_ptr(chunk, &ld->tk->dox->chunks) {
        /* Strip spaces at the beginning and end of each paragraph to have
         * clean brief and details paragraphs. Since details is put in its own
         * paragraph, this won't affect the inner formatting.
         */
        tab_for_each_ptr(paragraph, &chunk->paragraphs) {
            sb_ltrim(paragraph);
            sb_rtrim(paragraph);
        }
        /* Track the first sentence of the first paragraph (== brief). */
        if (chunk->paragraphs.len) {
            sb_t *paragraph = &chunk->paragraphs.tab[0];
            const char *p = memchr(paragraph->data, '.', paragraph->len);

            if (p) {
                chunk->first_sentence_len = p - paragraph->data + 1;
            }
        }
    }
    debug_dump_dox(ld);
}

static dox_chunk_t *lexdox_add_chunk(struct lexdata *ld)
{
    qv_t(dox_chunk) *chunks = &ld->tk->dox->chunks;
    dox_chunk_t *res = dox_chunk_init(qv_growlen(chunks, 1));

    lexdata_read_loc(ld, &res->loc);
    ld->dox_cur_paragraph = NULL;
    ld->dox_sp_count = 0;
    ld->dox_scan_params_args_counter = 2;
    return res;
}

static void lexdox_add_paragraph(struct lexdata *ld)
{
    qv_t(sb) *paragraphs;
    qv_t(dox_chunk) *chunks = &ld->tk->dox->chunks;

    if (!chunks->len)
        lexdox_add_chunk(ld);

    paragraphs = &tab_last(chunks)->paragraphs;
    if (paragraphs->len) {
        ld->dox_scan_params_args_counter = 0;
    }
    ld->dox_cur_paragraph = sb_init(qv_growlen(paragraphs, 1));
    ld->dox_sp_count = 0;
}

static
void lexdox_check_params_arg(struct lexdata *ld, const char *text, int len)
{
    dox_chunk_t *chunk = tab_last(&ld->tk->dox->chunks);

    if (chunk->paragraphs.len != 1)
        return;

    if (isalpha(text[0]) && --ld->dox_scan_params_args_counter > 0) {
        qv_append(&chunk->params_args, lstr_dups(text, len));
        chunk->paragraph0_args_len = chunk->paragraphs.tab[0].len + len;
    } else
    if (text[0] == ',' && ld->dox_scan_params_args_counter >= 1) {
        /* we allow scanning another arg id */
        ld->dox_scan_params_args_counter = 2;
    } else {
        ld->dox_scan_params_args_counter = 0;
    }
}

static void lexdox_add_text(struct lexdata *ld, const char *text, int len)
{
    dox_chunk_t *chunk;

    if (!ld->dox_cur_paragraph) {
        lexdox_add_paragraph(ld);
    } else
    if (ld->dox_sp_count == 1) {
        ld->dox_sp_count = 0;
        sb_addc(ld->dox_cur_paragraph, ' ');
    }
    lexdox_check_params_arg(ld, text, len);

    chunk = tab_last(&ld->tk->dox->chunks);
    /* check if we are beginning the second paragraph (== details) */
    if (!chunk->keyword.s && chunk->paragraphs.len == 1 && text[0] == '\n'
    &&  ld->dox_cur_paragraph
    &&  ld->dox_cur_paragraph->len > 0
    &&  ld->dox_cur_paragraph->data[ld->dox_cur_paragraph->len - 1] == '\n')
    {
        ld->dox_cur_paragraph = NULL;
        return;
    }
    sb_add(ld->dox_cur_paragraph, text, len);
    lexdox_update_last_chunk_loc(ld);
}

/* }}} */
/* flex configuration {{{ */

/* there is no noyy* options for them */
__attribute__((unused)) static int yyget_column(yyscan_t yyscanner);
__attribute__((unused)) static void yyset_column(int column_no, yyscan_t yyscanner);

#define YY_USER_ACTION \
    yyextra->prev_pos = yyextra->pos; \
    yyextra->prev_len = yyextra->len; \
    yyextra->pos += yyextra->len;     \
    yyextra->len  = yyleng;

#define PRINT_MSG_OFFS(prn_fct, offs, fmt, ...)  \
    prn_fct("%s:%d:%d:"fmt, yyextra->path, yyextra->lno, \
            lexdata_col(yyextra, (offs)), ##__VA_ARGS__)

#define WARN_OFFS(offs, fmt, ...)  \
    PRINT_MSG_OFFS(print_warning, offs, " warning: "fmt, ##__VA_ARGS__)

#define ERROR_OFFS(offs, fmt, ...)  do {                   \
    PRINT_MSG_OFFS(print_error, offs, fmt, ##__VA_ARGS__); \
    return NULL;                                           \
} while (0)

#define WARN(fmt, ...)   WARN_OFFS(0, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)  ERROR_OFFS(0, fmt, ##__VA_ARGS__)

#define INTEGER_ERROR(err)  ERROR("%s", get_integer_error(err))

#define YY_DECL        static iopc_token_t *yylex(yyscan_t yyscanner)
#define YY_EXTRA_TYPE  struct lexdata *
#undef ECHO
#define ECHO

#define yyrtrim() \
    do {                                                                     \
        while (isspace((unsigned char)yytext[yyleng - 1]))                   \
            yyleng--;                                                        \
        yytext[yyleng] = '\0';                                               \
    } while (0)

/* }}} */

%}

%option 8bit batch nointeractive reentrant stack
%option noyywrap nounput noyy_top_state noinput
%option prefix="iopc_"

%x      DOX_COMMENT DOX_LINE_COMMENT DOX_PARAM COMMENT STRING CSTRING
%s      ATTR

ID      [a-zA-Z][a-zA-Z0-9_]*
ATTR_ID [a-zA-Z_][a-zA-Z0-9_.]*
HS      [ \t\r\v]
NONSP   [^[:space:];#]
EOL     (\r\n|\r|\n)
SCHARS  [.=:,;()\[\]{}?*\-_~\^/+%&|]

%%

<INITIAL,ATTR>{
    "\xef\xbb\xbf"      /* skip UTF8 BOM */
    {HS}+               /* skip */
    {EOL}               { lexdata_newline(yyextra, yyleng); }
    {SCHARS}            { return token_new(yytext[0]); }
    "<<"                { return token_new(ITOK_LSHIFT); }
    ">>"                { return token_new(ITOK_RSHIFT); }
    "**"                { return token_new(ITOK_EXP); }
    <<EOF>>             { return token_new(ITOK_EOF); }

    "true"              {
                          iopc_token_t *tk = token_new(ITOK_BOOL);
                          tk->i = 1;
                          return tk;
                        }
    "false"             {
                          iopc_token_t *tk = token_new(ITOK_BOOL);
                          tk->i = 0;
                          return tk;
                        }
    {ID}                {
                          iopc_token_t *tk = token_new(ITOK_IDENT);
                          sb_set(&tk->b, yytext, yyleng);
                          return tk;
                        }
    "@"{ID}             {
                          iopc_token_t *tk = token_new(ITOK_ATTR);
                          sb_set(&tk->b, yytext + 1, yyleng - 1);
                          return tk;
                        }
    "@("                {
                          iopc_token_t *tk = token_new(ITOK_ATTR);
                          sb_sets(&tk->b, "generic");
                          return tk;
                        }
    ({ID}":")+{ID}      {
                          iopc_token_t *tk = token_new(ITOK_GEN_ATTR_NAME);
                          sb_set(&tk->b, yytext, yyleng);
                          return tk;
                        }
    "/*"("*<"|"!""<"?){HS}* {
                          lexdox_comment_begin(yyextra, yytext, yyleng);
                          BEGIN(DOX_COMMENT);
                        }
    "/**"{HS}+|"/**"/[^"*"] {
                          lexdox_comment_begin(yyextra, yytext, yyleng);
                          BEGIN(DOX_COMMENT);
                        }
    "//"("/<"|"!""<"?)  {
                          lexdox_comment_begin(yyextra, yytext, yyleng);
                          BEGIN(DOX_LINE_COMMENT);
                        }
    "///"/[^"/"]        {
                          lexdox_comment_begin(yyextra, yytext, yyleng);
                          BEGIN(DOX_LINE_COMMENT);
                        }
    "//"([^/!]|"//")[^\n\r]*{EOL} {
                          iopc_token_t *tk = token_new(ITOK_COMMENT);
                          sb_set(&tk->b, yytext, yyleng);
                          lexdata_newline(yyextra, yyleng);
                          return tk;
                        }
    "//"[^\n\r]*{EOL}   {
                          /* Skip this rule or it will mask the doxygen comments */
                          yyextra->pos = yyextra->prev_pos;
                          yyextra->len = yyextra->prev_len;
                          REJECT;
                        }
    "/*"                {
                          yyextra->tk = token_new(ITOK_COMMENT);
                          sb_set(&yyextra->tk->b, yytext, yyleng);
                          BEGIN(COMMENT);
                        }
    \"                  {
                          yyextra->tk = token_new(ITOK_STRING);
                          BEGIN(STRING);
                        }
    c\"                 {
                          yyextra->tk = token_new(ITOK_STRING);
                          BEGIN(CSTRING);
                        }
    "-"?[0-9]*"."[0-9]+([eE][+-]?[0-9]+)? {
                          iopc_token_t *tk = token_new(ITOK_DOUBLE);
                          errno = 0;
                          tk->d = strtod(yytext, NULL);
                          if (errno) {
                              iopc_token_delete(&tk);
                              ERROR("double overflow");
                          }
                          return tk;
                        }
    "-"?("0x"[0-9a-fA-F]+|[1-9][0-9]*)[a-zA-Z]* {
                          iopc_token_t *tk = token_new(ITOK_INTEGER);
                          int i_res;
                          if (yytext[0] == '-') {
                              i_res = strtoll_ext(yytext, (int64_t *)&tk->i,
                                                  NULL, 0);
                              tk->i_is_signed = true;
                          } else {
                              i_res = strtoull_ext(yytext, &tk->i, NULL, 0);
                              tk->i_is_signed = false;
                          }
                          if (i_res < 0) {
                              iopc_token_delete(&tk);
                              INTEGER_ERROR(errno);
                          }
                          return tk;
                        }
    "0"[0-7]*[a-zA-Z]*  {
                          iopc_token_t *tk = token_new(ITOK_INTEGER);
                          if (strtoll_ext(yytext, (int64_t *)&tk->i, NULL, 8)
                              < 0)
                          {
                              iopc_token_delete(&tk);
                              INTEGER_ERROR(errno);
                          }
                          tk->i_is_signed = true;
                          return tk;
                        }
}

<ATTR>{
    {ATTR_ID} {
                  iopc_token_t *tk = token_new(ITOK_IDENT);
                  sb_set(&tk->b, yytext, yyleng);
                  return tk;
              }
}

<DOX_COMMENT>{
    "*/"                {
                          lexdox_comment_end(yyextra);
                          BEGIN(INITIAL);
                          return yyextra->tk;
                        }
    {HS}*{EOL}{HS}*"*"*"*/" {
                          lexdata_newline(yyextra, yyleng);
                          /* reinject the comment end */
                          yyless(yyleng - 2);
                        }
    ^{HS}*"*"+{HS}?     { /* skip */ }
    {EOL}               {
                          lexdata_newline(yyextra, yyleng);
                          lexdox_add_text(yyextra, yytext, yyleng);
                        }
    {HS}+               { lexdox_add_text(yyextra, yytext, yyleng); }
}

<DOX_LINE_COMMENT>{
    {EOL}               {
                          lexdata_newline(yyextra, yyleng);
                          lexdox_comment_end(yyextra);
                          BEGIN(INITIAL);
                          return yyextra->tk;
                        }
}

<DOX_COMMENT,DOX_LINE_COMMENT>{
    <<EOF>>             { ERROR("unterminated doxygen comment"); }
    ("\\"|"@")[a-zA-Z]+{HS}*"["?  {
                          if (yytext[yyleng - 1] == '[') {
                              yytext[--yyleng] = '\0';
                              BEGIN(DOX_PARAM);
                          }
                          yyrtrim();
                          lexdox_add_chunk(yyextra)->keyword =
                              lstr_dups(yytext + 1, yyleng - 1);
                          lexdox_add_paragraph(yyextra);
                        }
    {HS}+               { yyextra->dox_sp_count = 1; }
    {ID}|.              { lexdox_add_text(yyextra, yytext, yyleng); }
}

<DOX_PARAM>{
    ","|{HS}+           /* skip */
    [a-zA-Z]+           {
                          qv_t(dox_chunk) *chunks = &yyextra->tk->dox->chunks;
                          qv_append(&tab_last(chunks)->params,
                                    lstr_dups(yytext, yyleng));
                        }
    "]"{HS}*            {
                          BEGIN(yyextra->dox_inline_comment ? DOX_LINE_COMMENT
                                                            : DOX_COMMENT);
                        }
    {EOL}               { ERROR("unterminated doxygen param"); }
    <<EOF>>             { ERROR("unterminated doxygen param at EOF"); }
    .|^","|","{HS}*","  {
                          qv_t(dox_chunk) *chunks = &yyextra->tk->dox->chunks;
                          dox_chunk_t *chunk = tab_last(chunks);

                          lexdox_add_text(yyextra, "[", 1);
                          tab_for_each_ptr(s, &chunk->params) {
                              lexdox_add_text(yyextra, s->s, s->len);
                              if (s != tab_last(&chunk->params))
                                  lexdox_add_text(yyextra, ", ", 2);
                          }
                          if (yytext[0] == ',') {
                              lexdox_add_text(yyextra, ", ,", 1);
                          } else {
                              lexdox_add_text(yyextra, yytext, yyleng);
                          }
                          qv_deep_wipe(&chunk->params, lstr_wipe);
                          qv_deep_wipe(&chunk->params_args, lstr_wipe);
                          chunk->paragraph0_args_len = 0;

                          WARN("doxygen param syntax error");
                          BEGIN(yyextra->dox_inline_comment ? DOX_LINE_COMMENT
                                                            : DOX_COMMENT);
                        }
}

<COMMENT>{
    {EOL}               {
                          sb_add(&yyextra->tk->b, yytext, yyleng);
                          lexdata_newline(yyextra, yyleng);
                        }
    <<EOF>>             {
                            iopc_token_delete(&yyextra->tk);
                            ERROR("unterminated comment");
                        }
    "*/"                {
                          sb_add(&yyextra->tk->b, yytext, yyleng);
                          BEGIN(INITIAL);
                          return yyextra->tk;
                        }
    .                   {
                          sb_add(&yyextra->tk->b, yytext, yyleng);
                        }
}

<STRING>{ /* parse string enclosed in double quotes with escapes */
    \"                  {
                          BEGIN(INITIAL);
                          return yyextra->tk;
                        }
    \\[0-2][0-7]{2}     { sb_addc(&yyextra->tk->b, strtol(yytext + 1, NULL, 8)); }
    \\x[0-9a-fA-F]{2}   { sb_addc(&yyextra->tk->b, strtol(yytext + 2, NULL, 16)); }
    \\u[0-9a-fA-F]{4}   { sb_adduc(&yyextra->tk->b, strtol(yytext + 2, NULL, 16)); }
    \\[abetnvfr\\"']    { sb_add_unquoted(&yyextra->tk->b, yytext, 2); }
    \\.                 { sb_add(&yyextra->tk->b, yytext, 2); }
    [^\\\n\"]+          { sb_add(&yyextra->tk->b, yytext, yyleng); }
    <<EOF>>             {
                            iopc_token_delete(&yyextra->tk);
                            ERROR("unterminated string");
                        }
}

<CSTRING>{
    \"                  {
                          BEGIN(INITIAL);
                          yyextra->tk->b_is_char = true;
                          return yyextra->tk;
                        }
    \\[abetnvfr\\"']    { sb_add_unquoted(&yyextra->tk->b, yytext, 2); }
    [^\\\n\"]+          { sb_add(&yyextra->tk->b, yytext, yyleng); }
    <<EOF>>             {
                            iopc_token_delete(&yyextra->tk);
                            ERROR("unterminated cstring");
                        }
}

<*>{ /* default rules to catch end of lines and end of files */
    .                   { ERROR("unexpected char `%c`", *yytext); }
}

%%

struct lexdata *iopc_lexer_new(const char *file, const char *data,
                               iopc_file_t type)
{
    struct lexdata *ld = p_new(struct lexdata, 1);

    sb_init(&ld->buf);
    ld->lno = 1;
    ld->path = file;

    if (type == IOPC_FILE_FD) {
        ld->fd = fopen(get_full_path(file), "rb");
        if (!ld->fd) {
            print_error("unable to open file `%s` for reading", file);
            p_delete(&ld);
            return NULL;
        }
    }

    yylex_init(&ld->scanner);
    yyset_extra(ld, ld->scanner);
    if (type == IOPC_FILE_FD) {
        yyset_in(ld->fd, ld->scanner);
    } else
    if (type == IOPC_FILE_BUFFER) {
        ld->buffer = yy_scan_string(data, ld->scanner);
    }

    return ld;
}

int iopc_lexer_fd(struct lexdata *ld)
{
    return ld->fd ? fileno(ld->fd) : -1;
}

void iopc_lexer_push_state_attr(struct lexdata *ld)
{
    yy_push_state(ATTR, ld->scanner);
}

void iopc_lexer_pop_state(struct lexdata *ld)
{
    yy_pop_state(ld->scanner);
}

static void iopc_lexer_wipe(struct lexdata *ld)
{
    sb_wipe(&ld->buf);
    yy_delete_buffer(ld->buffer, ld->scanner);
    yylex_destroy(ld->scanner);
    p_fclose(&ld->fd);
}
DO_DELETE(struct lexdata, iopc_lexer);

int iopc_next_token(struct lexdata *ld, bool want_comments,
                    iopc_token_t **out_tk)
{
    if (ld->done) {
        *out_tk = NULL;
        return 0;
    }

    for (;;) {
        iopc_token_t *tk = RETHROW_PN(yylex(ld->scanner));

        if (tk->token == ITOK_EOF) {
            ld->done = true;
            *out_tk = tk;
            return 0;
        }

        if (want_comments || tk->token != ITOK_COMMENT) {
            *out_tk = tk;
            return 0;
        }
        iopc_token_delete(&tk);
    }
}
