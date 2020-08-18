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

#include <dlfcn.h>
#include <sys/wait.h>

#include <lib-common/unix.h>
#include <lib-common/core.h>

#include "iopc.h"
#include "iopc.fc.c"

typeof(iopc_g) iopc_g = {
    .logger       = LOGGER_INIT_INHERITS(NULL, "iopc"),
    .class_id_min = 0,
    .class_id_max = 0xFFFF,
};

static struct {
    logger_t logger;
} iopc_so_g = {
#define _G  iopc_so_g
    .logger = LOGGER_INIT_INHERITS(&iopc_g.logger, "dso"),
};

static int do_call(char * const argv[], sb_t *err)
{
    pid_t pid;

    pid = ifork();
    if (pid < 0) {
        sb_setf(err, "unable to fork(): %m");
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        execvp(argv[0], argv);
        logger_fatal(&_G.logger, "unable to run %s: %m", argv[0]);
    }

    for (;;) {
        int status;

        if (waitpid(pid, &status, 0) < 0) {
            sb_setf(err, "waitpid: %m");
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status) ? -1 : 0;
        }
        if (WIFSIGNALED(status)) {
            sb_setf(err, "%s killed with signal %s", argv[0],
                    sys_siglist[WTERMSIG(status)]);
            return -1;
        }
    }

    return 0;
}

static int do_compile(const qv_t(str) *in, const char *out, sb_t *err)
{
    t_scope;
    qv_t(cstr) args;

    t_qv_init(&args, 20);

    qv_append(&args, "cc");

    qv_append(&args, "-std=gnu99");
    qv_append(&args, "-shared");
    qv_append(&args, "-fPIC");

    qv_append(&args, "-Wall");
    qv_append(&args, "-Werror");
    qv_append(&args, "-Wextra");
    qv_append(&args, "-Wno-unused-parameter");

#ifdef NDEBUG
    qv_append(&args, "-s");                       /* strip DSO        */
    qv_append(&args, "-O3");
#else
    qv_append(&args, "-O0");
    /* XXX valgrind does not support loading dso built with -g3, it fails with
     * "Warning: DWARF2 reader: Badly formed extended line op encountered"
     */
    if (mem_tool_is_running(MEM_TOOL_VALGRIND)) {
        qv_append(&args, "-g");
    } else {
        qv_append(&args, "-g3");
    }
#endif
    qv_append(&args, "-fno-strict-aliasing");

    qv_append(&args, "-o");
    qv_append(&args, out);                        /* DSO output       */
    tab_for_each_entry(s, in) {
        qv_append(&args, s);
    }
    qv_append(&args, NULL);

    return do_call((char * const *)args.tab, err);
}

static int
iopc_build(const char *pfxdir, bool display_pfx, const qm_t(iopc_env) *env,
           const char *iopfile, const char *iopdata, const char *outdir,
           bool is_main_pkg, lstr_t * nullable pkgname,
           lstr_t * nullable pkgpath)
{
    t_scope;
    SB_1k(sb);
    iopc_pkg_t *pkg;
    lstr_t farch;

    farch = t_farch_get_data(iopc_farch, "../iop-compat.h");
    sb_add_lstr(&sb, farch);
    farch = t_farch_get_data(iopc_farch, "../iop-internals.h");
    sb_add_lstr(&sb, farch);

    iopc_g.prefix_dir     = pfxdir;
    iopc_g.display_prefix = display_pfx;

    iopc_do_c_g.resolve_includes = false;
    iopc_do_c_g.no_const = true;
    iopc_do_c_g.iop_compat_header = sb.data;

    iopc_parser_typer_initialize();

    pkg = iopc_parse_file(NULL, env, iopfile, iopdata, is_main_pkg);
    if (!pkg) {
        goto error;
    }

    if (iopc_resolve(pkg) < 0) {
        goto error;
    }

    if (iopc_resolve_second_pass(pkg) < 0) {
        goto error;
    }

    iopc_types_fold(pkg);

    if (iopc_do_c(pkg, outdir, NULL) < 0) {
        goto error;
    }

    if (is_main_pkg && iopc_do_json(pkg, outdir, NULL) < 0) {
        goto error;
    }

    if (pkgname) {
        *pkgname = lstr_dups(pretty_path_dot(pkg->name), -1);
    }
    if (pkgpath) {
        *pkgpath = lstr_dups(pretty_path(pkg->name), -1);
    }

    iopc_parser_typer_shutdown();
    return 0;

  error:
    iopc_parser_typer_shutdown();
    return -1;
}

void iopc_dso_set_class_id_range(uint16_t class_id_min, uint16_t class_id_max)
{
    iopc_g.class_id_min = class_id_min;
    iopc_g.class_id_max = class_id_max;
}

int iopc_dso_build(const char *pfxdir, bool display_pfx,
                   const char *iopfile, const qm_t(iopc_env) *env,
                   const char *outdir, sb_t *err)
{
    SB_1k(sb);
    SB_1k(local_err);
    const qv_t(log_buffer) *log_buffer;
    lstr_t pkgname = LSTR_NULL_V, pkgpath = LSTR_NULL_V;
    char so_path[PATH_MAX], path[PATH_MAX], tmppath[PATH_MAX];
    char json_path[PATH_MAX];
    qv_t(str) sources;
    int ret = 0;
    const char *filepart = path_filepart(iopfile);

    qv_init(&sources);

    path_extend(so_path, outdir, "%s.so", filepart);

    path_extend(tmppath, outdir, "%s.%d.XXXXXX", filepart, getpid());
    if (!mkdtemp(tmppath)) {
        sb_setf(err, "failed to create temporary directory %s: %m",
                tmppath);
        return -1;
    }

    /* We'll get the error produced by iopc_build by using the log buffers. */
    log_start_buffering_filter(false, LOG_ERR);

    if (iopc_build(pfxdir, display_pfx, env, iopfile, NULL, tmppath, true,
                   &pkgname, &pkgpath) < 0)
    {
        goto iopc_build_error;
    }

    /* move json to outdir */
    path_extend(json_path, outdir, "%*pM.json", LSTR_FMT_ARG(pkgpath));
    path_extend(path, tmppath, "%*pM.json",  LSTR_FMT_ARG(pkgpath));
    if (rename(path, json_path) < 0) {
        sb_setf(err, "failed to create json file `%s`: %m", json_path);
        return -1;
    }

    qv_append(&sources, asprintf("-I%s", tmppath));

    path_extend(path, tmppath, "%*pM.c",  LSTR_FMT_ARG(pkgpath));
    qv_append(&sources, p_strdup(path));

    sb_addf(&sb, "#include \"%*pM.h\"\n", LSTR_FMT_ARG(pkgpath));
    sb_adds(&sb, "IOP_EXPORT_PACKAGES_COMMON;\n");
    sb_adds(&sb, "IOP_USE_EXTERNAL_PACKAGES;\n");
    sb_addf(&sb, "IOP_EXPORT_PACKAGES(&%*pM__pkg);\n", LSTR_FMT_ARG(pkgname));

    path_extend(path, tmppath, "%*pM-iop-plugin.c", LSTR_FMT_ARG(pkgname));
    sb_write_file(&sb, path);
    qv_append(&sources, p_strdup(path));

    qm_for_each_pos(iopc_env, pos, env) {
        const char *depfile = env->keys[pos];
        const char *depdata = env->values[pos];

        if (iopc_build(pfxdir, display_pfx, env, depfile, depdata, tmppath,
                       false, NULL, NULL) < 0)
        {
            goto iopc_build_error;
        }
    }
    log_stop_buffering();

    if (do_compile(&sources, so_path, &local_err) < 0) {
        sb_setf(err, "failed to build `%s`: %*pM",
                so_path, SB_FMT_ARG(&local_err));
        ret = -1;
        goto end;
    }
    logger_trace(&_G.logger, 1, "iop plugin %s successfully built from %s",
                 so_path, iopfile);

  end:
    qv_deep_wipe(&sources, p_delete);
    lstr_wipe(&pkgname);
    lstr_wipe(&pkgpath);
    rmdir_r(tmppath, false);
    return ret;

  iopc_build_error:
    log_buffer = log_stop_buffering();
    if (expect(log_buffer->len)) {
        sb_reset(err);
        tab_for_each_pos_rev(pos, log_buffer) {
            if (!err->len) {
                sb_add_lstr(err, log_buffer->tab[pos].msg);
            } else {
                sb_addf(err, ": %*pM", LSTR_FMT_ARG(log_buffer->tab[pos].msg));
            }
        }
    } else {
        sb_sets(err, "unknown iopc parser error");
    }
    ret = -1;
    goto end;
}

/* {{{ Module */

static int iopc_dso_initialize(void *arg)
{
    return 0;
}

static int iopc_dso_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(iopc_dso)
    MODULE_DEPENDS_ON(iopc);
MODULE_END()

/* }}} */
