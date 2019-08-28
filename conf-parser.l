%{
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

#include "conf.h"
#include "unix.h"

struct lexdata {
    bool  seen_section : 1;
    int   arraylvl;    /* number of openings of arrays seen          */
    int   context;     /* state we just left, used by <STRING> state */
    int   lno, lstart; /* line no 1-based, position of the last \n   */
    int   pos, len;    /* stream length, 0-based, and copy of yyleng */

    cfg_parse_hook *hook;
    void *data;
    int   opts;
    char  path[PATH_MAX];

    FILE *fd;
    sb_t buf, err;
    yyscan_t scanner;
};

/* helpers {{{ */

static int lexdata_relpos(struct lexdata *ld, int offs)
{
    assert (-ld->len <= offs && offs < ld->len);
    return ld->pos + offs + (offs < 0 ? ld->len : 0);
}

static void lexdata_newline(struct lexdata *ld, int offs)
{
    ld->lno++;
    ld->lstart = lexdata_relpos(ld, offs) + 1;
}

static int lexdata_col(struct lexdata *ld, int offs)
{
    return 1 + lexdata_relpos(ld, offs) - ld->lstart;
}

static int parse_oct(const char *o) {
    int res = *o - '0';
    while (*++o) {
        res = (res * 10) + *o - '0';
    }
    return res;
}

static int parse_hex(const char *h) {
    int res = str_digit_value(*h);
    while (*++h) {
        res = (res << 4) + str_digit_value(*h);
    }
    return res;
}

static int first_non_id_char(const char *txt)
{
    for (int pos = 0; ; pos++) {
        if (str_digit_value(txt[pos]) == 255) {
            if (txt[pos] != '_' || pos == 0)
                return pos;
        }
    }
}

/* }}} */
/* flex configuration {{{ */

/* there is no noyy* options for them */
__attribute__((unused)) static int yyget_column(yyscan_t yyscanner);
__attribute__((unused)) static void yyset_column(int column_no, yyscan_t yyscanner);

#define YY_USER_ACTION \
    yyextra->pos += yyextra->len;  \
    yyextra->len  = yyleng;

#define HOOK(evt, val, l) \
    do {                                                                     \
        if ((*yyextra->hook)(yyextra->data, CFG_PARSE##evt, (val), (l),      \
                             yyextra))                                       \
        {                                                                    \
            (*yyextra->hook)(yyextra->data, CFG_PARSE_ERROR,                 \
                             yyextra->err.data, yyextra->err.len,            \
                             yyextra);                                       \
            return -1;                                                       \
        }                                                                    \
    } while (0)
#define HOOKBUF(_evt)  HOOK(_evt, yyextra->buf.data, yyextra->buf.len)

#define ERROR_OFFS(offs, fmt, ...)  \
    do {                                                                     \
        sb_setf(&yyextra->err, "%s%d:%d:"fmt, yyextra->path,                 \
                yyextra->lno, lexdata_col(yyextra, (offs)),                  \
                ##__VA_ARGS__);                                              \
        HOOK(_ERROR, yyextra->err.data, yyextra->err.len);                   \
        return -1;                                                           \
    } while (0)

#define ERROR(fmt, ...)  ERROR_OFFS(0, fmt, ##__VA_ARGS__)
#define HAS_OPT(OPT)     (yyextra->opts & CFG_PARSE_##OPT)

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
%option noyywrap nounput noyy_top_state noyy_push_state noyy_pop_state noinput
%option prefix="confp_"

%x      TOEOL
%x      SECTION EOSECTION
%x      EQUAL VALUE STRING
%x      ARRAY ARRAYSEP

ID      [a-zA-Z][a-zA-Z0-9_]*
HS      [ \t\r\v]
NORMAL  [^[:space:];#"(),]
NONSP   [^[:space:];#]

%%
<INITIAL>{
    \[{HS}*             { BEGIN(SECTION); }
    {ID}(\.{ID})*       {
                          if (!yyextra->seen_section)
                              ERROR("no section opened yet");
                          HOOK(_KEY, yytext, yyleng);
                          BEGIN(EQUAL);
                        }
    {ID}(\.{ID})*\[\]   {
                          if (!yyextra->seen_section)
                              ERROR("no section opened yet");
                          if (HAS_OPT(GROK_ARRAY)) {
                              yytext[yyleng - 2] = '\0';
                              HOOK(_KEY_ARRAY, yytext, yyleng - 2);
                              BEGIN(EQUAL);
                          } else
                          if (HAS_OPT(OLD_KEYS)) {
                              HOOK(_KEY, yytext, yyleng);
                              BEGIN(EQUAL);
                          } else {
                              ERROR_OFFS(-2, "unexpected char `[`");
                          }
                        }
    [a-zA-Z]{NONSP}+    {
                          if (!yyextra->seen_section)
                              ERROR("no section opened yet");
                          if (!HAS_OPT(OLD_KEYS)) {
                              int pos = first_non_id_char(yytext);
                              ERROR_OFFS(pos, "unexpected char `%c`", yytext[pos]);
                          }
                          HOOK(_KEY, yytext, yyleng);
                          BEGIN(EQUAL);
                        }
}
<INITIAL,TOEOL>{
    {HS}+               /* SKIP */
    [;#].*\n            { lexdata_newline(yyextra, -1); BEGIN(INITIAL); }
}

<SECTION>{
    {ID}{HS}*\"?        {
                          int has_quote = yytext[yyleng - 1] == '"';
                          yyleng -= has_quote;
                          yyrtrim();
                          HOOK(_SECTION, yytext, yyleng);
                          yyextra->seen_section = true;
                          if (has_quote) {
                              yyextra->context = SECTION;
                              yyleng--;
                              BEGIN(STRING);
                          } else {
                              HOOK(_SECTION_ID, NULL, 0);
                              BEGIN(EOSECTION);
                          }
                        }
    {ID}:[^\n\]]+       {
                          if (!HAS_OPT(OLD_NAMESPACES)) {
                              ERROR_OFFS((int)(strchr(yytext, ':') - yytext),
                                         "unexpected char `:`");
                          }
                          yyrtrim();
                          HOOK(_SECTION, yytext, yyleng);
                          HOOK(_SECTION_ID, NULL, 0);
                          yyextra->seen_section = true;
                          BEGIN(EOSECTION);
                        }
}
<EOSECTION>{HS}*\]      { BEGIN(TOEOL); }

<EQUAL>{
    {HS}+               /* SKIP */
    ={HS}*              {
                          HOOK(_SET, NULL, 0);
                          BEGIN(VALUE);
                        }
    \+={HS}*\(          {
                          if (!HAS_OPT(GROK_ARRAY))
                              ERROR("unexpected char `+`");
                          HOOK(_APPEND, NULL, 0);
                          yyextra->arraylvl++;
                          BEGIN(ARRAY);
                        }
}

<VALUE>{ /* parse right hand side values */
    \n                  { HOOK(_VALUE, NULL, 0);
                          lexdata_newline(yyextra, -1);
                          BEGIN(INITIAL);
                        }
    {NONSP}([^\n;#]*{NONSP})? {
                          if (*yytext == '"') {
                              yyless(1);
                              yyextra->context = VALUE;
                              BEGIN(STRING);
                          } else
                          if (HAS_OPT(GROK_ARRAY) && *yytext == '(') {
                              yyless(1);
                              HOOK(_ARRAY_OPEN, yytext, yyleng);
                              yyextra->arraylvl++;
                              BEGIN(ARRAY);
                          } else {
                              HOOK(_VALUE, yytext, yyleng);
                              BEGIN(TOEOL);
                          }
                        }
}

<ARRAY>{ /* parse arrays */
    \"                  { yyextra->context = ARRAY; BEGIN(STRING); }
    \(                  {
                          HOOK(_ARRAY_OPEN, yytext, yyleng);
                          yyextra->arraylvl++;
                        }
    {NORMAL}+           { HOOK(_VALUE, yytext, yyleng); BEGIN(ARRAYSEP); }
}
<ARRAY,ARRAYSEP>{
    {HS}+               /* SKIP */
    \n                  { lexdata_newline(yyextra, 0); }
    \)                  {
                          HOOK(_ARRAY_CLOSE, NULL, 0);
                          BEGIN(--yyextra->arraylvl > 0 ? ARRAYSEP : TOEOL);
                        }
}
<ARRAYSEP>,{HS}*        { BEGIN(ARRAY); }

<STRING>{ /* parse string enclosed in double quotes with escapes */
    \"                  {
                          switch (yyextra->context) {
                            case VALUE:
                              HOOKBUF(_VALUE);
                              BEGIN(TOEOL);
                              break;
                            case ARRAY:
                              HOOKBUF(_VALUE);
                              BEGIN(ARRAYSEP);
                              break;
                            case SECTION:
                              HOOKBUF(_SECTION_ID);
                              BEGIN(EOSECTION);
                              break;
                            default: abort();
                          }
                          sb_reset(&yyextra->buf);
                        }
    \\[0-2][0-7]{2}     { sb_addc(&yyextra->buf, parse_oct(yytext + 1)); }
    \\x[0-9a-fA-F]{2}   { sb_addc(&yyextra->buf, parse_hex(yytext + 2)); }
    \\u[0-9a-fA-F]{4}   { sb_adduc(&yyextra->buf, parse_hex(yytext + 2)); }
    \\[abetnvfr\\"']    { sb_add_unquoted(&yyextra->buf, yytext, 2); }
    \\.                 { sb_add(&yyextra->buf, yytext, 2); }
    [^\\\n\"]+          { sb_add(&yyextra->buf, yytext, yyleng); }
}

<*>{ /* default rules to catch end of lines and end of files */
    \n                  { if (YYSTATE != INITIAL && YYSTATE != TOEOL)
                              ERROR("unexpected new line");
                          lexdata_newline(yyextra, 0);
                          BEGIN(INITIAL);
                        }
    .                   { ERROR("unexpected char `%c`", *yytext); }
    <<EOF>>             { if (YYSTATE != INITIAL && YYSTATE != TOEOL)
                              ERROR("unexpected end of file");
                          HOOK(_EOF, NULL, 0);
                          return 0;
                        }
}

%%

static struct lexdata *lexdata_fast_init(struct lexdata *ld)
{
    sb_init(&ld->buf);
    sb_init(&ld->err);
    ld->lno = 1;
    yylex_init(&ld->scanner);
    yyset_extra(ld, ld->scanner);
    return ld;
}

#define NOT_BOTH(x, o1, o2) \
    assert (!((x & CFG_PARSE_##o1) && (x & CFG_PARSE_##o2)))
static int check_opts(int opts)
{
    NOT_BOTH(opts, OLD_KEYS, GROK_ARRAY);
    return opts;
}

static void lexdata_wipe(struct lexdata *ld)
{
    sb_wipe(&ld->buf);
    sb_wipe(&ld->err);
    yylex_destroy(ld->scanner);
    p_fclose(&ld->fd);
}

int cfg_parse(const char *file, cfg_parse_hook *hook, void *data, int opts)
{
    struct lexdata ld = { .hook = hook, .data = data, .opts = check_opts(opts) };
    int res = -1;

    lexdata_fast_init(&ld);
    snprintf(ld.path, sizeof(ld.path), "%s:", file);
    if ((ld.fd = fopen(file, "rb"))) {
        yyset_in(ld.fd, ld.scanner);
        res = yylex(ld.scanner);
    }
    lexdata_wipe(&ld);
    return res;
}

int cfg_parse_buf(const char *buf, ssize_t blen, cfg_parse_hook *hook, void *data, int opts)
{
    struct lexdata ld = { .hook = hook, .data = data, .opts = check_opts(opts) };
    int res;

    lexdata_fast_init(&ld);
    yy_scan_bytes(buf, blen, ld.scanner);
    res = yylex(ld.scanner);
    lexdata_wipe(&ld);
    return res;
}

int cfg_parse_seterr(void *ctx, int offs, const char *fmt, ...)
{
    struct lexdata *ld = ctx;
    va_list ap;

    sb_setf(&ld->err, "%s%d:%d:", ld->path, ld->lno, lexdata_col(ld, offs));
    va_start(ap, fmt);
    sb_addvf(&ld->err, fmt, ap);
    va_end(ap);
    return -1;
}
