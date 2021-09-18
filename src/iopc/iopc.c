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

#include <lib-common/parseopt.h>
#include <lib-common/unix.h>

#include "iopc.h"

extern const char libcommon_git_revision[];

static struct {
    bool help, version, git_revision, wall;
    char *incpath;
    const char *lang;
    const char *outpath;
    const char *json_outpath;
    const char *c_outpath;
    const char *typescript_outpath;
    const char *depends;
    const char *class_id_range;
} opts;

typeof(iopc_g) iopc_g = {
    .logger       = LOGGER_INIT_INHERITS(NULL, "iopc"),
    .class_id_min = 0,
    .class_id_max = 0xFFFF,
};
#define _G  iopc_g

static popt_t options[] = {
    OPT_FLAG('h', "help",    &opts.help,    "show this help"),
    OPT_FLAG('V', "version", &opts.version, "show version (git revision)"),

    OPT_GROUP(""),
    OPT_STR('I',  "include-path", &opts.incpath,  "include path"),
    OPT_STR('o',  "output-path",  &opts.outpath,  "base of the compiled hierarchy"),
    OPT_STR('d',  "depends",      &opts.depends,  "dump depends file"),
    OPT_STR('l',  "language",     &opts.lang,     "output language"),
    OPT_STR(0,  "class-id-range", &opts.class_id_range,
            "authorized class id range (min-max, included)"),
    OPT_FLAG(0,   "Wextra",       &_G.print_info,  "add extra warnings"),

    OPT_GROUP("C backend options"),
    OPT_FLAG(0,   "c-resolve-includes", &iopc_do_c_g.resolve_includes,
             "try to generate relative includes"),
    OPT_STR(0,    "c-output-path", &opts.c_outpath,
            "base of the compiled hierarchy for C files"),

    OPT_GROUP("JSON backend options"),
    OPT_STR(0,    "json-output-path", &opts.json_outpath,
            "base of the compiled hierarchy for JSON files"),

    OPT_GROUP("TypeScript backend options"),
    OPT_STR(0,    "typescript-output-path", &opts.typescript_outpath,
            "base of the compiled hierarchy for TypeScript files"),
    OPT_FLAG(0,   "typescript-enable-backbone",
             &iopc_do_typescript_g.enable_iop_backbone,
             "enable the generation of IOP/Backbone models"),
    OPT_END(),
};

__attr_printf__(2, 0) static void
iopc_log_handler(const log_ctx_t *ctx, const char *fmt, va_list va)
{
    vfprintf(stderr, fmt, va);
    fputc('\n', stderr);
}

static void parse_incpath(qv_t(cstr) *ipath, char *spec)
{
    if (!spec)
        return;
    for (;;) {
        if (!*spec) {
            qv_append(ipath, ".");
            return;
        }
        if (*spec == ':') {
            qv_append(ipath, ".");
        } else {
            qv_append(ipath, spec);
            spec = strchr(spec, ':');
            if (!spec)
                break;
        }
        *spec++ = '\0';
    }
    for (int i = ipath->len - 1; i >= 0; --i) {
        struct stat st;

        if (stat(ipath->tab[i], &st) || !S_ISDIR(st.st_mode)) {
            qv_remove(ipath, i);
        }
    }
}

static int parse_class_id_range(const char *class_id_range)
{
    pstream_t ps = ps_initstr(class_id_range);

    if (!ps_len(&ps))
        return 0;

    /* Format: "min-max" */
    iopc_g.class_id_min = ps_geti(&ps);
    THROW_ERR_IF(iopc_g.class_id_min < 0 || iopc_g.class_id_min > 0xFFFF);

    RETHROW(ps_skipc(&ps, '-'));

    iopc_g.class_id_max = ps_geti(&ps);
    THROW_ERR_IF(iopc_g.class_id_max < iopc_g.class_id_min
              || iopc_g.class_id_max > 0xFFFF);

    return 0;
}

static char const * const only_stdin[] = { "-", NULL };

struct doit {
    int (*cb)(iopc_pkg_t *, const char *, sb_t *);
    const char *outpath;
};
qvector_t(doit, struct doit);

static int build_doit_table(qv_t(doit) *doits)
{
    qv_t(lstr) langs;
    ctype_desc_t sep;

    if (!opts.lang) {
        /* No language specified; this is authorized in case of --depends. */
        if (!opts.depends) {
            print_error("no language specified");
            return -1;
        }
        return 0;
    }

    qv_inita(&langs, 2);
    ctype_desc_build(&sep, ",");
    ps_split(ps_initstr(opts.lang), &sep, 0, &langs);

    tab_for_each_entry(lang, &langs) {
        struct doit doit;

        if (lstr_ascii_iequal(lang, LSTR("c"))) {
            doit = (struct doit){
                .cb = &iopc_do_c,
                .outpath = opts.c_outpath
            };
        } else
        if (lstr_ascii_iequal(lang, LSTR("json"))) {
            doit = (struct doit){
                .cb = &iopc_do_json,
                .outpath = opts.json_outpath
            };
        } else
        if (lstr_ascii_iequal(lang, LSTR("typescript"))) {
            doit = (struct doit){
                .cb = &iopc_do_typescript,
                .outpath = opts.typescript_outpath
            };
        } else {
            print_error("unsupported language `%*pM`", LSTR_FMT_ARG(lang));
            goto error;
        }

        if (doit.outpath) {
            if (mkdir_p(doit.outpath, 0777) < 0) {
                print_error("cannot create output directory `%s`: %m",
                            doit.outpath);
                goto error;
            }
        }
        qv_append(doits, doit);
    }

    qv_wipe(&langs);
    return 0;

  error:
    qv_wipe(&langs);
    return -1;
}

static void sb_add_depends(iopc_pkg_t *pkg, sb_t *depbuf)
{
    t_scope;
    qv_t(iopc_pkg) t_deps;
    qv_t(iopc_pkg) t_weak_deps;
    qv_t(iopc_pkg) i_deps;

    t_qv_init(&t_deps, 16);
    t_qv_init(&t_weak_deps, 16);
    t_qv_init(&i_deps, 16);

    iopc_get_depends(pkg, &t_deps, &t_weak_deps, &i_deps,
                     IOPC_GET_DEPENDS_INCLUDE_ALL);

    tab_for_each_entry(dep, &t_deps) {
        sb_addf(depbuf, "%s\n", dep->file);
    }
    tab_for_each_entry(dep, &t_weak_deps) {
        sb_addf(depbuf, "%s\n", dep->file);
    }
    tab_for_each_entry(dep, &i_deps) {
        sb_addf(depbuf, "%s\n", dep->file);
    }
}

int main(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);
    qv_t(cstr) incpath;
    qv_t(doit) doits;
    iopc_pkg_t *pkg;
    SB_8k(deps);

    setlinebuf(stderr);
    argc = parseopt(argc, argv, options, 0);
    if (argc < 0 || opts.help) {
        makeusage(!opts.help, arg0, "<iop file>", NULL, options);
    }
    if (opts.version) {
        printf("%s\n", libcommon_git_revision);
        return 0;
    }

    MODULE_REQUIRE(iopc);

    qv_init(&incpath);
    qv_init(&doits);

    opts.c_outpath    = opts.c_outpath ?: opts.outpath;
    opts.json_outpath = opts.json_outpath ?: opts.outpath;

    _G.prefix_dir     = getcwd(NULL, 0);
    _G.display_prefix = true;

    log_set_handler(&iopc_log_handler);

    if (opts.c_outpath && iopc_do_c_g.resolve_includes) {
        print_error("outdir and --c-resolve-includes are incompatible");
        goto error;
    }

    if (build_doit_table(&doits) < 0) {
        goto error;
    }

    parse_incpath(&incpath, opts.incpath);

    if (opts.class_id_range && parse_class_id_range(opts.class_id_range) < 0)
    {
        print_error("invalid class-id-range `%s`", opts.class_id_range);
        goto error;
    }

    if (argc == 0) {
        argc = 1;
        argv = (char **)only_stdin;
    }
    for (int i = 0; i < argc; i++) {
        iopc_parser_typer_initialize();

        if (!(pkg = iopc_parse_file(&incpath, NULL, argv[i], NULL, true))) {
            iopc_parser_typer_shutdown();
            goto error;
        }
        if (iopc_resolve(pkg) < 0) {
            iopc_parser_typer_shutdown();
            goto error;
        }
        if (iopc_resolve_second_pass(pkg) < 0) {
            iopc_parser_typer_shutdown();
            goto error;
        }

        iopc_types_fold(pkg);

        tab_for_each_ptr(doit, &doits) {
            if ((*doit->cb)(pkg, doit->outpath, &deps) < 0) {
                iopc_parser_typer_shutdown();
                goto error;
            }
        }

        if (opts.depends) {
            sb_add_depends(pkg, &deps);
        }

        iopc_parser_typer_shutdown();
    }

    if (opts.depends) {
        char dir[PATH_MAX];

        path_dirname(dir, sizeof(dir), opts.depends);
        if (mkdir_p(dir, 0755) < 0) {
            print_error("cannot create directory `%s`: %m", dir);
            goto error;
        }
        if (sb_write_file(&deps, opts.depends) < 0) {
            print_error("cannot write file `%s`: %m", opts.depends);
            goto error;
        }
    }

    MODULE_RELEASE(iopc);
    qv_wipe(&incpath);
    qv_wipe(&doits);
    p_delete((char **)&_G.prefix_dir);
    return 0;

  error:
    MODULE_RELEASE(iopc);
    qv_wipe(&incpath);
    qv_wipe(&doits);
    p_delete((char **)&_G.prefix_dir);
    return -1;
}
