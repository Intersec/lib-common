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

/* Visual sampler for the TERM_COLOR_* APIs of <lib-common/unix.h>.
 *
 * It prints, for every color/modifier the API can express, a rendered swatch
 * followed by the literal escape sequence that produced it, so the actual
 * rendering can be eyed in any terminal (xterm, the Linux console, PuTTY, ...).
 *
 * Build: produces the 'ztst-term-colors' binary; just run it with no argument.
 */

#include <lib-common/core.h>
#include <lib-common/unix.h>

typedef struct color_sample_t {
    const char *label;
    const char *code;
} color_sample_t;

/* Stringify the macro expression alongside its expanded escape code. */
#define ENTRY(macro)  { #macro, macro }

static void print_section(const char *title, const color_sample_t *samples,
                          int len)
{
    printf("\n== %s ==\n", title);
    for (int i = 0; i < len; i++) {
        printf("  \e[%sm The quick brown fox \e[0m  %-44s \\e[%sm\n",
               samples[i].code, samples[i].label, samples[i].code);
    }
}

#define PRINT_SECTION(title, arr)  print_section(title, arr, countof(arr))

/* {{{ Base colors */

static color_sample_t const foreground_g[] = {
    ENTRY(TERM_COLOR_BLACK),
    ENTRY(TERM_COLOR_RED),
    ENTRY(TERM_COLOR_GREEN),
    ENTRY(TERM_COLOR_YELLOW),
    ENTRY(TERM_COLOR_BLUE),
    ENTRY(TERM_COLOR_PURPLE),
    ENTRY(TERM_COLOR_CYAN),
    ENTRY(TERM_COLOR_WHITE),
    ENTRY(TERM_COLOR_DEFAULT),
};

static color_sample_t const background_g[] = {
    ENTRY(TERM_COLOR_BLACK_BG),
    ENTRY(TERM_COLOR_RED_BG),
    ENTRY(TERM_COLOR_GREEN_BG),
    ENTRY(TERM_COLOR_YELLOW_BG),
    ENTRY(TERM_COLOR_BLUE_BG),
    ENTRY(TERM_COLOR_PURPLE_BG),
    ENTRY(TERM_COLOR_CYAN_BG),
    ENTRY(TERM_COLOR_WHITE_BG),
    ENTRY(TERM_COLOR_DEFAULT_BG),
};

/* }}} */
/* {{{ Bright colors (the dedicated 90-97 / 100-107 codes) */

static color_sample_t const bright_foreground_g[] = {
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_BLACK)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_RED)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_GREEN)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_YELLOW)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_BLUE)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_PURPLE)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_CYAN)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_WHITE)),
};

static color_sample_t const bright_background_g[] = {
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_BLACK_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_RED_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_GREEN_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_YELLOW_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_BLUE_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_PURPLE_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_CYAN_BG)),
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_WHITE_BG)),
};

/* }}} */
/* {{{ Modifiers */

static color_sample_t const modifiers_g[] = {
    ENTRY(TERM_COLOR_BRIGHTER(TERM_COLOR_GREEN)),
    ENTRY(TERM_COLOR_DIM(TERM_COLOR_WHITE)),
    ENTRY(TERM_COLOR_ITALIC(TERM_COLOR_DEFAULT)),
    ENTRY(TERM_COLOR_UNDERLINED(TERM_COLOR_CYAN)),
    ENTRY(TERM_COLOR_FLASHING(TERM_COLOR_RED)),
    ENTRY(TERM_COLOR_REVERSE(TERM_COLOR_YELLOW)),
    ENTRY(TERM_COLOR_HIDDEN(TERM_COLOR_WHITE)),
    ENTRY(TERM_COLOR_STRIKETHROUGH(TERM_COLOR_PURPLE)),
};

/* }}} */
/* {{{ Logger severity combinations */

/* These mirror the LOG_COLOR_* definitions of core/log.c, so the sampler shows
 * exactly what the logger currently emits for each level. */
static color_sample_t const log_levels_g[] = {
    { "LOG_COLOR_FUNCTION",    TERM_COLOR_YELLOW },
    { "LOG_COLOR_LOGGER_NAME", TERM_COLOR_BRIGHTER(TERM_COLOR_BLACK) },
    { "LOG_COLOR_DEBUG",       TERM_COLOR_ITALIC(TERM_COLOR_DEFAULT) },
    { "LOG_COLOR_WARNING",     TERM_COLOR_BRIGHTER(TERM_COLOR_YELLOW) },
    { "LOG_COLOR_ERROR",       TERM_COLOR_BRIGHTER(TERM_COLOR_RED) },
    { "LOG_COLOR_CRIT",
      TERM_COLOR_BRIGHTER(TERM_COLOR_COMBINE(TERM_COLOR_RED_BG,
                                             TERM_COLOR_WHITE)) },
};

/* }}} */

int main(void)
{
    if (!is_fancy_fd(STDOUT_FILENO)) {
        printf("note: stdout is not a fancy terminal; escapes shown anyway\n");
    }

    PRINT_SECTION("foreground colors", foreground_g);
    PRINT_SECTION("background colors", background_g);
    PRINT_SECTION("bright foreground colors", bright_foreground_g);
    PRINT_SECTION("bright background colors", bright_background_g);
    PRINT_SECTION("modifiers", modifiers_g);
    PRINT_SECTION("logger severity levels", log_levels_g);

    printf("\n");
    return 0;
}
