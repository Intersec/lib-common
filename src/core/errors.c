/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <dlfcn.h>    /* dladdr */
#include <execinfo.h> /* backtrace_symbols_fd */
#include <link.h>     /* dl_iterate_phdr */
#include <signal.h>   /* signal */
#include <sys/wait.h> /* waitpid */

#include <lib-common/core.h>
#include <lib-common/thr.h>
#include <lib-common/unix.h>

#define XWRITE(s) IGNORE(xwrite(fd, s, strlen(s)))

#ifndef NDEBUG
/* Runtime address range and load bias of the main executable.
 *
 * dladdr() reports as base the address of the ELF header, which for a non-PIE
 * (ET_EXEC) executable is not the load bias (e.g. 0x400000 vs 0), so the
 * offset it implies is wrong for addr2line. We instead use the load bias
 * reported by dl_iterate_phdr() (0 for non-PIE, the mapping base for PIE) and
 * identify the main executable's frames by their address range.
 * See ps_dump_symbolized_backtrace(). */
static uintptr_t main_exe_lo_g;
static uintptr_t main_exe_hi_g;
static uintptr_t main_exe_bias_g;

static int
find_main_exe_base(struct dl_phdr_info *info, size_t size, void *data)
{
    uintptr_t lo = UINTPTR_MAX;
    uintptr_t hi = 0;

    /* The first object reported by dl_iterate_phdr() is the main program;
     * span its loadable segments to get its runtime address range. */
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        uintptr_t seg = info->dlpi_addr + ph->p_vaddr;

        if (ph->p_type != PT_LOAD) {
            continue;
        }
        lo = MIN(lo, seg);
        hi = MAX(hi, seg + ph->p_memsz);
    }
    main_exe_bias_g = info->dlpi_addr;
    main_exe_lo_g = lo;
    main_exe_hi_g = hi;
    return 1;
}
#endif

/** XXX The backtrace() function calls an init() function which uses malloc()
 * and leads to deadlock in the signals handler. So we always call backtrace()
 * once outside of the signals hander. It's an horrible hack but it works.
 */
__attribute__((constructor)) static void fix_backtrace_init(void)
{
    void *arr[256];

    backtrace(arr, countof(arr));

#ifndef NDEBUG
    dl_iterate_phdr(&find_main_exe_base, NULL);
#endif
}

static bool should_dump_maps(void)
{
    /* By default, memory maps are not dumped in .debug files, because it
     * makes the files big and this is rarely used.
     * It can be enabled with the IS_DEBUG_FILES_DUMP_MAPS environment
     * variable. */
    static int dump_maps = -1;

    if (unlikely(dump_maps < 0)) {
        const char *env = getenv("IS_DEBUG_FILES_DUMP_MAPS");

        dump_maps = env && *env && atoi(env) > 0;
    }

    return dump_maps;
}

static bool debug_stack_has_frames(void);

#ifndef NDEBUG

typedef struct ps_frame_t {
    const char *mod;   /* module path to display, or NULL if unresolved */
    const char *a2l;   /* module path passed to addr2line, or NULL       */
    uintptr_t addr;    /* return address                                 */
    uintptr_t off;     /* addr - module base, i.e. the offset to display */
    uintptr_t a2l_off; /* offset passed to addr2line (see the -1 below)  */
} ps_frame_t;

/* Write one backtrace line: "#NN module(+0xoff)[0xaddr]  name", where `name`
 * (the addr2line result) may be NULL when the frame could not be resolved. */
static void
ps_emit_frame(const ps_frame_t *frame, int idx, const char *name, int fd)
{
    char buf[1024];
    int len;

    if (frame->mod) {
        len = snprintf(
            buf, sizeof(buf), "#%02d %s(+0x%jx)[0x%jx]", idx, frame->mod,
            (uintmax_t)frame->off, (uintmax_t)frame->addr
        );
    } else {
        len = snprintf(
            buf, sizeof(buf), "#%02d [0x%jx]", idx, (uintmax_t)frame->addr
        );
    }
    IGNORE(xwrite(fd, buf, MIN(len, (int)sizeof(buf) - 1)));
    if (name && name[0]) {
        XWRITE("  ");
        XWRITE(name);
    }
    XWRITE("\n");
}

/* Symbolize the frames [lo, hi), which all belong to the same object, and
 * write their backtrace lines to `fd`.
 *
 * We run "addr2line -p -f -C -e <obj>", feed it the file offsets on its
 * standard input and read the resolved names back on a pipe (addr2line prints
 * exactly one line per input address, in order). Each name is merged with the
 * raw location computed by the caller. Frames left unresolved (addr2line
 * missing, or fewer lines than expected) still get their raw location.
 *
 * We deliberately use raw fork() and execvp(): this runs from a signal
 * handler after a crash, so we must run neither the module fork hooks that
 * ifork() would trigger nor a shell as system() would. */
static void
ps_symbolize_group(const ps_frame_t *frames, int lo, int hi, int fd)
{
    int in_fd[2];
    int out_fd[2];
    pid_t pid;
    int frame = lo;
    char rbuf[512];
    char line[1024];
    int line_len = 0;
    bool overflow = false;
    ssize_t nr;

    if (pipe(in_fd) < 0) {
        goto unresolved;
    }
    if (pipe(out_fd) < 0) {
        p_close(&in_fd[0]);
        p_close(&in_fd[1]);
        goto unresolved;
    }
    pid = (fork)();
    if (pid < 0) {
        p_close(&in_fd[0]);
        p_close(&in_fd[1]);
        p_close(&out_fd[0]);
        p_close(&out_fd[1]);
        goto unresolved;
    }
    if (pid == 0) {
        const char *argv[] = {
            "addr2line", "-p", "-f", "-C", "-e", frames[lo].a2l, NULL,
        };

        if (dup2(in_fd[0], STDIN_FILENO) >= 0 &&
            dup2(out_fd[1], STDOUT_FILENO) >= 0)
        {
            close(in_fd[0]);
            close(in_fd[1]);
            close(out_fd[0]);
            close(out_fd[1]);
            execvp("addr2line", (char *const *)argv);
        }
        _exit(127);
    }

    p_close(&in_fd[0]);
    p_close(&out_fd[1]);

    /* Feed addr2line the file offsets, one per line. */
    for (int k = lo; k < hi; k++) {
        char buf[24];
        int len = snprintf(
            buf, sizeof(buf), "0x%jx\n", (uintmax_t)frames[k].a2l_off
        );

        IGNORE(xwrite(in_fd[1], buf, len));
    }
    p_close(&in_fd[1]);

    /* Merge each resolved name back with its raw location. */
    for (;;) {
        nr = read(out_fd[0], rbuf, sizeof(rbuf));
        if (nr < 0) {
            if (ERR_RW_RETRIABLE(errno)) {
                continue;
            }
            break;
        }
        if (nr == 0) {
            break;
        }
        for (int p = 0; p < nr; p++) {
            if (rbuf[p] == '\n') {
                line[line_len] = '\0';
                if (frame < hi) {
                    ps_emit_frame(&frames[frame], frame, line, fd);
                    frame++;
                }
                line_len = 0;
                overflow = false;
            } else if (!overflow) {
                if (line_len < (int)sizeof(line) - 1) {
                    line[line_len++] = rbuf[p];
                } else {
                    /* Drop the rest of an over-long line to stay aligned. */
                    overflow = true;
                }
            }
        }
    }
    p_close(&out_fd[0]);
    IGNORE(waitpid(pid, NULL, 0));

unresolved:
    for (; frame < hi; frame++) {
        ps_emit_frame(&frames[frame], frame, NULL, fd);
    }
}

/* Write a symbolized backtrace to `fd`.
 *
 * backtrace_symbols_fd() can only name symbols listed in the dynamic symbol
 * table. Static functions are never listed there, and since we build with
 * -fvisibility=hidden almost nothing else is either, so it can only print
 * bare addresses. We instead resolve every frame with addr2line, which reads
 * the DWARF debug info embedded by -ggdb3, and print on each line both the
 * raw location (to symbolize offline should the debug info be missing here)
 * and the resolved "function at file:line".
 *
 * This is only done on debug and default builds (NDEBUG undefined). */
static void ps_dump_symbolized_backtrace(void *const *arr, int count, int fd)
{
    ps_frame_t frames[256];
    char self_exe[32];
    void (*old_sigpipe)(int);

    if (count <= 0) {
        return;
    }
    count = MIN(count, countof(frames));

    /* Path to the main executable. We use /proc/<pid>/exe rather than its
     * on-disk path: it is always absolute and still points to the running
     * binary even if it was replaced on disk (e.g. after an upgrade). It must
     * be resolved through our own pid, not /proc/self/exe, since addr2line
     * opens it from its own process. */
    snprintf(self_exe, sizeof(self_exe), "/proc/%d/exe", (int)getpid());

    for (int i = 0; i < count; i++) {
        uintptr_t addr = (uintptr_t)arr[i];
        /* The frames are return addresses: step back one byte so the lookup
         * lands on the call instruction, and not on whatever follows it (the
         * next line, or even the next function for a noreturn call). */
        uintptr_t pc = addr ? addr - 1 : addr;
        Dl_info info;

        frames[i].addr = addr;
        if (addr >= main_exe_lo_g && addr < main_exe_hi_g) {
            /* Main executable: offset from the load bias (right for both PIE
             * and non-PIE), and /proc/<pid>/exe as the addr2line target. */
            frames[i].mod = dladdr((void *)pc, &info) && info.dli_fname &&
                                    info.dli_fname[0]
                                ? info.dli_fname
                                : self_exe;
            frames[i].a2l = self_exe;
            frames[i].off = addr - main_exe_bias_g;
            frames[i].a2l_off = pc - main_exe_bias_g;
        } else if (
            dladdr((void *)pc, &info) && info.dli_fname && info.dli_fname[0]
        )
        {
            /* Shared library: it is always ET_DYN, so dladdr()'s base is the
             * load bias and the offset is correct. */
            uintptr_t base = (uintptr_t)info.dli_fbase;

            frames[i].mod = info.dli_fname;
            frames[i].a2l = info.dli_fname;
            frames[i].off = addr - base;
            frames[i].a2l_off = pc - base;
        } else {
            frames[i].mod = NULL;
            frames[i].a2l = NULL;
            frames[i].off = 0;
            frames[i].a2l_off = 0;
        }
    }

    XWRITE("--- Backtrace:\n\n");

    /* Feeding offsets to an addr2line that is absent or has died raises
     * SIGPIPE; ignore it so we keep dumping (and fall back to raw addresses)
     * instead of being killed mid-crash. */
    old_sigpipe = signal(SIGPIPE, SIG_IGN);

    /* Symbolize consecutive frames from the same object in one addr2line run,
     * keeping the frames in stack order. */
    for (int i = 0; i < count;) {
        int j = i;

        if (!frames[i].a2l) {
            ps_emit_frame(&frames[i], i, NULL, fd);
            i++;
            continue;
        }
        while (j < count && frames[j].a2l == frames[i].a2l) {
            j++;
        }
        ps_symbolize_group(frames, i, j, fd);
        i = j;
    }

    signal(SIGPIPE, old_sigpipe);
}

#endif

void ps_dump_backtrace(int signum, const char *prog, int fd, bool full)
{
    char buf[256];
    void *arr[256];
    int bt, n;

    if (signum >= 0) {
        n = snprintf(
            buf, sizeof(buf), "---> %s[%d] %s at %jd\n\n", prog, getpid(),
            strsignal(signum), time(NULL)
        );
    } else {
        n = snprintf(
            buf, sizeof(buf), "---> %s[%d] expect violation at %jd\n\n", prog,
            getpid(), time(NULL)
        );
    }
    if (xwrite(fd, buf, n) < 0) {
        return;
    }

    if (debug_stack_has_frames()) {
        XWRITE(
            "WARNING: additional user context available at the end of the "
            "file\n\n"
        );
    }

    bt = backtrace(arr, countof(arr));
#ifndef NDEBUG
    /* On debug and default builds, resolve each frame to a symbol name and
     * source location; the raw address stays on the line as a fallback. */
    ps_dump_symbolized_backtrace(arr, bt, fd);
#else
    backtrace_symbols_fd(arr, bt, fd);
#endif
    fsync(fd);

    if (full && should_dump_maps()) {
        int maps_fd = open("/proc/self/smaps", O_RDONLY);

        if (maps_fd != -1) {
            XWRITE("\n--- Memory maps:\n\n");
            for (;;) {
                n = read(maps_fd, buf, sizeof(buf));
                if (n < 0) {
                    if (ERR_RW_RETRIABLE(errno)) {
                        continue;
                    }
                    break;
                }
                if (n == 0) {
                    break;
                }
                if (xwrite(fd, buf, n) < 0) {
                    break;
                }
            }
            close(maps_fd);
        }
    } else {
        XWRITE("\n");
    }
}

static void
ps_panic_sighandler_print_version(int fd, const core_version_t *version)
{
    XWRITE(version->name);
    XWRITE(" version: ");
    XWRITE(version->version);
    XWRITE(" (");
    XWRITE(version->git_revision);
    XWRITE(")");
    XWRITE("\n");
}

__attr_printf__(2, 3) static void ps_print_file(
    const char *path, const char *fmt, ...
)
{
    va_list va;
    char cmd[BUFSIZ];
    int len;

    va_start(va, fmt);
    len = vsnprintf(cmd, sizeof(cmd), fmt, va);
    va_end(va);

    snprintf(cmd + len, sizeof(cmd) - len, " >> %s", path);

    IGNORE(system(cmd));
}

extern const char *syslog_critical_log_g;

void ps_write_backtrace(int signum, bool allow_fork)
{
    const char *debug_dir = getenv("IS_DEBUG_FILES_DIR");
    char path[PATH_MAX];
    int fd;
    int saved_errno = errno;

    if (!debug_dir || !*debug_dir) {
        debug_dir = "/tmp";
    }

    snprintf(
        path, sizeof(path), "%s/%s.%d.%ld.debug", debug_dir,
        program_invocation_short_name, (uint32_t)time(NULL), (long)getpid()
    );
    fd = open(path, O_EXCL | O_CREAT | O_WRONLY | O_TRUNC, 0600);

    if (fd >= 0) {
        int main_versions_printed = 0;
        char buf[256];

        for (int i = 0; i < core_versions_nb_g; i++) {
            const core_version_t *version = &core_versions_g[i];

            if (version->is_main_version) {
                ps_panic_sighandler_print_version(fd, version);
                main_versions_printed++;
            }
        }
        if (main_versions_printed > 0) {
            XWRITE("\n");
        }
        for (int i = 0; i < core_versions_nb_g; i++) {
            const core_version_t *version = &core_versions_g[i];

            if (!version->is_main_version) {
                ps_panic_sighandler_print_version(fd, version);
            }
        }
        XWRITE("\n");

        snprintf(
            buf, sizeof(buf), "--- errno: %s (%d)\n", strerror(saved_errno),
            saved_errno
        );
        XWRITE(buf);

        if (syslog_critical_log_g) {
            snprintf(
                buf, sizeof(buf), "--- critical log: %s\n",
                syslog_critical_log_g
            );
            XWRITE(buf);
        }

        errno = saved_errno;
        ps_dump_backtrace(signum, program_invocation_short_name, fd, true);
        p_close(&fd);

        if (allow_fork) {
            ps_print_file(path, "echo '\n--- OS release:\n'");
            ps_print_file(path, "cat /etc/os-release");

            ps_print_file(
                path, "echo '\n--- File descriptors (using ls):\n'"
            );
            ps_print_file(path, "ls -al /proc/self/fd");

            ps_print_file(
                path, "echo '\n--- File descriptors (using lsof):\n'"
            );
            ps_print_file(path, "lsof -p %d", getpid());
        }
    }
#ifndef NDEBUG
    errno = saved_errno;
    ps_dump_backtrace(
        signum, program_invocation_short_name, STDERR_FILENO, false
    );
#endif
    errno = saved_errno;

    _debug_stack_print(path);
    errno = saved_errno;
}

#undef XWRITE

typedef struct debug_info_t {
    lstr_t func;
    lstr_t file;
    debug_stack_cb_f *cb;
    data_t data;
    int line;
} debug_info_t;

qvector_t(debug_stack, debug_info_t)

    static __thread qv_t(debug_stack) debug_stack_g;

static void debug_stack_init(void)
{
    qv_init(&debug_stack_g);
}

static void debug_stack_wipe(void)
{
    qv_wipe(&debug_stack_g);
}

thr_hooks(debug_stack_init, debug_stack_wipe);

data_t debug_stack_push(
    lstr_t func, lstr_t file, int line, data_t data,
    debug_stack_cb_f *nonnull cb
)
{
    debug_info_t *info = qv_growlen0(&debug_stack_g, 1);

    info->func = func;
    info->file = file;
    info->line = line;
    info->cb = cb;
    info->data = data;

    return data;
}

void debug_stack_pop(data_t *nonnull data)
{
    qv_shrink(&debug_stack_g, 1);
}

static bool debug_stack_has_frames(void)
{
    return debug_stack_g.len > 0;
}

void debug_stack_dprint(int fd)
{
    tab_for_each_pos_rev(i, &debug_stack_g) {
        const debug_info_t *info = &debug_stack_g.tab[i];

        dprintf(
            fd, "\n[%d] in %.*s() from %.*s:%d\n", i,
            LSTR_FMT_ARG(info->func), LSTR_FMT_ARG(info->file), info->line
        );
        (info->cb)(fd, info->data);
    }
}

int _debug_stack_print(const char *nonnull path)
{
    int fd;

    if (!debug_stack_g.len) {
        return 0;
    }

    /* XXX The file is supposed to exist already. */
    fd = RETHROW(open(path, O_WRONLY | O_APPEND, 0600));

    dprintf(fd, "\nAdditional user context:\n");
    debug_stack_dprint(fd);

    return p_close(&fd);
}
