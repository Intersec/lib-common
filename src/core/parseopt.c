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

#include <sysexits.h>

#include <lib-common/parseopt.h>

#define FLAG_SHORT 1
#define FLAG_UNSET 2

typedef struct popt_state_t {
    int flags;
    char * const * pending_argv;
    int pending_argc;
    char **left_argv;
    int left_argc;
    const char *p;
} popt_state_t;

static inline popt_state_t *
opt_state_init(popt_state_t *st, int argc, char **argv, int flags)
{
    p_clear(st, 1);
    st->flags = flags;
    st->pending_argv = argv;
    st->pending_argc = argc;
    st->left_argv = argv;
    st->left_argc = 0;
    return st;
}

static inline int opt_state_end(popt_state_t *st)
{
    memmove(st->left_argv + st->left_argc, st->pending_argv,
            st->pending_argc * sizeof(*st->pending_argv));
    return st->left_argc + st->pending_argc;
}

static inline void opt_add_left_arg(popt_state_t *st, char *arg)
{
    st->left_argv[st->left_argc++] = arg;
}

static const char *opt_arg(popt_state_t *st)
{
    if (st->p) {
        const char *res = st->p;
        st->p = NULL;
        return res;
    }
    st->pending_argc--;
    return *++st->pending_argv;
}

static int opterror(popt_t *opt, const char *reason, int flags)
{
    if (flags & FLAG_SHORT) {
        e_error("option `%c' %s", opt->shrt, reason);
    } else
    if (flags & FLAG_UNSET) {
        e_error("option `no-%s' %s", opt->lng, reason);
    } else {
        e_error("option `%s' %s", opt->lng, reason);
    }
    return -1;
}

static int put_int_value(popt_t *opt, uint64_t v)
{
    switch (opt->int_vsize) {
#define CASE(_sc) case _sc / 8:                                              \
        if (opt->kind == OPTION_UINT) {                                      \
            if (v <= UINT##_sc##_MAX) {                                      \
                *(uint##_sc##_t *)opt->value = v;                            \
            } else {                                                         \
                return -1;                                                   \
            }                                                                \
        } else {                                                             \
            int64_t w = (int64_t)v;                                          \
            if (w >= INT##_sc##_MIN && w <= INT##_sc##_MAX) {                \
                *(int##_sc##_t *)opt->value = v;                             \
            } else {                                                         \
                return -1;                                                   \
            }                                                                \
        }                                                                    \
        break;

        CASE(8);
        CASE(16);
        CASE(32);
        CASE(64);

#undef CASE

      default: e_panic("should not happen");
    }

    return 0;
}

static int get_value(popt_state_t *st, popt_t *opt, int flags)
{
    if (st->p && (flags & FLAG_UNSET)) {
        return opterror(opt, "takes no value", flags);
    }

    switch (opt->kind) {
        const char *s;

      case OPTION_FLAG:
        if (!(flags & FLAG_SHORT) && st->p) {
            return opterror(opt, "takes no value", flags);
        }
        put_int_value(opt, !(flags & FLAG_UNSET));
        return 0;

      case OPTION_STR:
        if (flags & FLAG_UNSET) {
            *(const char **)opt->value = (const char *)opt->init;
        } else {
            if (!st->p && st->pending_argc < 2) {
                return opterror(opt, "requires a value", flags);
            }
            *(const char **)opt->value = opt_arg(st);
        }
        return 0;

      case OPTION_CHAR:
        if (flags & FLAG_UNSET) {
            *(char *)opt->value = (char)opt->init;
        } else {
            const char *value;
            if (!st->p && st->pending_argc < 2) {
                return opterror(opt, "requires a value", flags);
            }
            value = opt_arg(st);
            if (strlen(value) != 1) {
                return opterror(opt, "expects a single character", flags);
            }
            *(char *)opt->value = value[0];
        }
        return 0;

      case OPTION_INT:
      case OPTION_UINT:
      {
          uint64_t v;

          if (flags & FLAG_UNSET) {
              v = opt->init;
          } else {
              if (!st->p && st->pending_argc < 2) {
                  return opterror(opt, "requires a value", flags);
              }

              errno = 0;
              if (opt->kind == OPTION_UINT) {
                  lstr_t value = lstr_ltrim(LSTR(opt_arg(st)));

                  if (lstr_startswithc(value, '-')) {
                      /* -0 will return an error. */
                      return opterror(opt, "expects a positive value", flags);
                  }
                  v = strtoull(value.s, &s, 10);
              } else {
                  v = strtoll(opt_arg(st), &s, 10);
              }
              if (*s || (errno && errno != ERANGE)) {
                  return opterror(opt, "expects a numerical value", flags);
              }
          }

          if (errno == ERANGE || put_int_value(opt, v) < 0) {
              return opterror(opt, "integer overflow", flags);
          }
      }
      return 0;

      case OPTION_VERSION:
        if (flags & FLAG_UNSET) {
            return opterror(opt, "takes no value", flags);
        }
        makeversion(EX_OK, opt->value, (void *)opt->init);

      default:
        e_panic("should not happen, programmer is a moron");
    }
}

static popt_t *find_opts_short_opt(popt_state_t *st, popt_t *opts)
{
    for (; opts->kind; opts++) {
        if (opts->shrt == *st->p) {
            return opts;
        }
    }
    return NULL;
}

static int parse_short_opt(popt_state_t *st, char *arg, popt_t *opts)
{
    bool ignore_unknown = st->flags & POPT_IGNORE_UNKNOWN_OPTS;

    st->p = arg + 1;
    do {
        popt_t *p_opts = find_opts_short_opt(st, opts);

        if (!p_opts) {
            if (ignore_unknown) {
                st->p = NULL;
                opt_add_left_arg(st, arg);
                return 0;
            } else {
                return e_error("unknown option `%c'", *st->p);
            }
        }
        ignore_unknown = false;

        st->p = st->p[1] ? st->p + 1 : NULL;
        RETHROW(get_value(st, p_opts, FLAG_SHORT));
    } while (st->p);

    return 0;
}

static int parse_long_opt(popt_state_t *st, char *arg, popt_t *opts)
{
    const char *arg_opt = arg + 2;

    for (; opts->kind; opts++) {
        const char *p;
        int flags = 0;

        if (!opts->lng) {
            continue;
        }

        if (!strstart(arg_opt, opts->lng, &p)) {
            if (!strstart(arg_opt, "no-", &p) || !strstart(p, opts->lng, &p))
            {
                continue;
            }
            flags = FLAG_UNSET;
        }
        if (*p) {
            if (*p != '=') {
                continue;
            }
            st->p = p + 1;
        }
        return get_value(st, opts, flags);
    }
    if (st->flags & POPT_IGNORE_UNKNOWN_OPTS) {
        opt_add_left_arg(st, arg);
        return 0;
    } else {
        return e_error("unknown option `%s'", arg_opt);
    }
}

static void copyinits(popt_t *opts)
{
    for (;;) {
        switch (opts->kind) {
          case OPTION_FLAG:
          case OPTION_INT:
            opts->init = *(int *)opts->value;
            break;
          case OPTION_STR:
            opts->init = (intptr_t)*(const char **)opts->value;
            break;
          case OPTION_CHAR:
            opts->init = *(char *)opts->value;
            break;
          default:
            break;
          case OPTION_END:
            return;
        }
        opts++;
    }
}

int parseopt(int argc, char **argv, popt_t *opts, int flags)
{
    popt_state_t st;

    opt_state_init(&st, argc, argv, flags);
    copyinits(opts);

    for (; st.pending_argc > 0; st.pending_argc--, st.pending_argv++) {
        char *arg = st.pending_argv[0];

        if (*arg != '-' || !arg[1]) {
            if (flags & POPT_STOP_AT_NONARG) {
                break;
            }
            opt_add_left_arg(&st, arg);
            continue;
        }

        if (arg[1] != '-') {
            RETHROW(parse_short_opt(&st, arg, opts));
            continue;
        }

        if (!arg[2]) { /* "--" */
            st.pending_argc--;
            st.pending_argv++;
            break;
        }

        RETHROW(parse_long_opt(&st, arg, opts));
    }

    return opt_state_end(&st);
}

#define OPTS_WIDTH 20
#define OPTS_GAP    2

void makeusage(int ret, const char *arg0, const char *usage,
               const char * const text[], popt_t *opts)
{
    const char *p = strrchr(arg0, '/');

    printf("Usage: %s [options] %s\n", p ? p + 1 : arg0, usage);
    if (text) {
        putchar('\n');
        while (*text) {
            printf("    %s\n", *text++);
        }
    }
    if (opts->kind != OPTION_GROUP)
        putchar('\n');
    for (; opts->kind; opts++) {
        int pos = 4;
        pstream_t help;

        if (opts->kind == OPTION_GROUP) {
            putchar('\n');
            if (*opts->help)
                printf("%s\n", opts->help);
            continue;
        }
        printf("    ");
        if (opts->shrt) {
            pos += printf("-%c", opts->shrt);
        }
        if (opts->lng) {
            pos += printf(opts->shrt ? ", --%s" : "--%s", opts->lng);
        }
        if (opts->kind != OPTION_FLAG) {
            pos += printf(" ...");
        }

        help = ps_initstr(opts->help);
        while (!ps_done(&help)) {
            pstream_t line;

            if (ps_get_ps_chr_and_skip(&help, '\n', &line) < 0) {
                line = help;
                help = ps_init(NULL, 0);
            }
            if (pos <= OPTS_WIDTH) {
                printf("%*s%*pM", OPTS_WIDTH + OPTS_GAP - pos, "",
                       PS_FMT_ARG(&line));
                pos = OPTS_WIDTH + 1;
            } else {
                printf("\n%*s%*pM", OPTS_WIDTH + OPTS_GAP, "",
                       PS_FMT_ARG(&line));
            }
        }
        printf("\n");
    }
    exit(ret);
}

void makeversion(int ret, const char *name, const char *(*get_version)(void))
{
    if (name && get_version) {
        printf("Intersec %s\n"
               "Revision: %s\n",
               name, (*get_version)());
    } else {
        int main_versions_printed = 0;

        for (int i = 0; i < core_versions_nb_g; i++) {
            const core_version_t *version = &core_versions_g[i];

            if (version->is_main_version) {
                printf("Intersec %s %s\n"
                       "Revision: %s\n",
                       version->name, version->version,
                       version->git_revision);
                main_versions_printed++;
            }
        }
        if (main_versions_printed > 0) {
            printf("\n");
        }
        for (int i = 0; i < core_versions_nb_g; i++) {
            const core_version_t *version = &core_versions_g[i];

            if (!version->is_main_version) {
                printf("%s %s (%s)\n",
                       version->name, version->version,
                       version->git_revision);
            }
        }
    }

    printf("\n"
           "See http://www.intersec.com/ for more details about our\n"
           "line of products for telecommunications operators\n");
    exit(ret);
}
