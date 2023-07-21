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

#include <fnmatch.h>
#include <lib-common/unix.h>
#include <lib-common/thr.h>
#include <lib-common/datetime.h>
#include <lib-common/container-qhash.h>
#include <lib-common/iop.h>

#include <lib-common/log.h>

#ifdef MEM_BENCH
#include <lib-common/core-mem-bench.h>
#endif

uint32_t log_conf_gen_g = 1;
log_handler_f *log_stderr_handler_g;
int log_stderr_handler_teefd_g = -1;

struct level {
    int level;
    unsigned flags;
};
qm_kvec_t(level, lstr_t, struct level, qhash_lstr_hash, qhash_lstr_equal);


typedef struct buffer_instance_t {
    qv_t(log_buffer) vec_buffer;
    bool use_handler : 1;
    int buffer_log_level;
} buffer_instance_t;

qvector_t(buffer_instance, buffer_instance_t);

#ifndef NDEBUG
#define LOG_DEFAULT  LOG_TRACE
#else
#define LOG_DEFAULT  LOG_DEBUG
#endif

static log_handler_f log_stderr_raw_handler;

_MODULE_ADD_DECLS(log);

static struct {
    logger_t root_logger;
    e_handler_f   *e_handler;
    log_handler_f *handler;

    char          *is_debug;
    qm_t(level)    pending_levels;

    bool fancy;
    char fancy_prefix[64];
    int fancy_len;

    qv_t(spec)  specs;
    int maxlen, rows, cols;
    int pid;
    spinlock_t update_lock;

    bool log_timestamp : 1;
} log_g = {
#define _G  log_g
    .root_logger = {
        .level         = LOG_DEFAULT,
        .defined_level = LOG_UNDEFINED,
        .default_level = LOG_DEFAULT,
        .name          = LSTR_EMPTY,
        .full_name     = LSTR_EMPTY,
        .parent        = NULL,
        .children      = DLIST_INIT(_G.root_logger.children),
        .siblings      = DLIST_INIT(_G.root_logger.siblings)
    },
    .pending_levels = QM_INIT(level, _G.pending_levels),
    .handler        = &log_stderr_raw_handler,
};

static __thread struct {
    bool inited;
    sb_t log;
    sb_t buf;

    log_ctx_t ml_ctx;

    bool in_spinlock;

    /* log buffer */
    qv_t(buffer_instance) vec_buff_stack;
    mem_stack_pool_t mp_stack;
    int nb_buffer_started;
} log_thr_g;

__thread log_thr_ml_t log_thr_ml_g;

/* Helpers {{{ */

void log_spin_lock(void)
{
    if (unlikely(log_thr_g.in_spinlock)) {
        /* Deadlock detected. This can happen in tricky situations, for
         * example if malloc fails we calling logger_panic, which calls
         * logger_refresh, which takes a lock and tries to allocate memory.
         * If it fails again, the same stack is called again and the deadlock
         * occurs.
         * Catch it and do the best we can to avoid blocking a thread: commit
         * suicide.
         */
        syslog(LOG_USER | LOG_CRIT, "deadlock detected in log library");
        printf("deadlock detected in log library\n");
        abort();
    }
    log_thr_g.in_spinlock = true;
    spin_lock(&_G.update_lock);
}

void log_spin_unlock(void)
{
    spin_unlock(&_G.update_lock);
    log_thr_g.in_spinlock = false;
}

lstr_t t_logger_sanitize_name(const lstr_t name)
{
    t_SB_1k(normalized);
    uint32_t used_characters;

    sb_normalize_utf8(&normalized, name.s, name.len, false);
    used_characters = 0;

    for (int i = 0; i < normalized.len; i++) {
        int c = normalized.data[i];

        if (isprint(c) && c != '!' && c != '/' && !isspace(c)) {
            normalized.data[used_characters] = c;
            used_characters++;
        }

    }

    sb_shrink(&normalized, normalized.len - used_characters);

    if (!normalized.len || !isalnum(normalized.data[0])) {
        return LSTR_NULL_V;
    }

    return LSTR_SB_V(&normalized);
}

/* }}} */
/* Configuration {{{ */

logger_t *logger_init(logger_t *logger, logger_t *parent, lstr_t name,
                      int default_level, unsigned level_flags)
{
    p_clear(logger, 1);
    logger->level = LOG_UNDEFINED;
    logger->defined_level = LOG_UNDEFINED;
    logger->default_level = default_level;
    logger->level_flags         = level_flags;
    logger->default_level_flags = level_flags;
    dlist_init(&logger->siblings);
    dlist_init(&logger->children);

    logger->parent = parent;
    logger->name   = lstr_dup(name);
    __logger_refresh(logger);

    return logger;
}

logger_t *logger_new(logger_t *parent, lstr_t name, int default_level,
                     unsigned level_flags)
{
    return logger_init(p_new_raw(logger_t, 1), parent, name, default_level,
                       level_flags);
}

/* Suppose the parent is locked */
static void logger_wipe_child(logger_t *logger)
{
    if (!dlist_is_empty(&logger->children) && logger->children.next) {
        dlist_for_each_entry(logger_t, child, &logger->children, siblings) {
            if (child->is_static) {
                logger_wipe_child(child);
            } else {
#ifndef NDEBUG
                logger_panic(&_G.root_logger,
                             "leaked logger `%*pM`, cannot wipe `%*pM`",
                             LSTR_FMT_ARG(child->full_name),
                             LSTR_FMT_ARG(logger->full_name));
#endif
            }
        }
    }

    if (!dlist_is_empty(&logger->siblings) && logger->siblings.next) {
        dlist_remove(&logger->siblings);
    }
    lstr_wipe(&logger->name);
    lstr_wipe(&logger->full_name);
}

void logger_wipe(logger_t *logger)
{
    log_spin_lock();
    logger_wipe_child(logger);
    log_spin_unlock();
}

static void logger_compute_fullname(logger_t *logger)
{
    /* The name of a logger must be a non-empty printable string
     * without any '/' or '!'
     */
    assert (memchr(logger->name.s, '/', logger->name.len) == NULL);
    assert (memchr(logger->name.s, '!', logger->name.len) == NULL);
    assert (logger->name.len);
    for (int i = 0; i < logger->name.len; i++) {
        assert (isprint((unsigned char)logger->name.s[i]));
    }

    if (logger->parent->full_name.len) {
        lstr_t name = logger->name;
        lstr_t full_name;

        full_name = lstr_fmt("%*pM/%*pM",
                             LSTR_FMT_ARG(logger->parent->full_name),
                             LSTR_FMT_ARG(logger->name));

        logger->full_name = full_name;
        logger->name = LSTR_INIT_V(full_name.s + full_name.len - name.len,
                                   name.len);
        lstr_wipe(&name);
    } else
    if (logger->name.len) {
        logger->full_name = lstr_dupc(logger->name);
    }
}

void __logger_do_refresh(logger_t *logger)
{
    if (atomic_load_explicit(&logger->conf_gen, memory_order_acquire)
        == log_conf_gen_g)
    {
        return;
    }

    logger->level_flags &= ~LOG_FORCED;

    if (logger->parent == NULL && logger != &_G.root_logger) {
        logger->parent = &_G.root_logger;
    }
    if (logger->parent) {
        __logger_do_refresh(logger->parent);
    }

    if (!logger->full_name.s) {
        int pos = 0;

        logger_compute_fullname(logger);

        assert (logger->level >= LOG_UNDEFINED);
        assert (logger->default_level >= LOG_INHERITS);
        assert (logger->defined_level >= LOG_UNDEFINED);

        dlist_for_each_entry(logger_t, sibling, &logger->parent->children,
                             siblings)
        {
            assert (!lstr_equal(sibling->name, logger->name));
        }
        dlist_add(&logger->parent->children, &logger->siblings);
        dlist_init(&logger->children);

        pos = qm_del_key(level, &_G.pending_levels, &logger->full_name);
        if (pos >= 0) {
            logger->defined_level = _G.pending_levels.values[pos].level;
            logger->level_flags   = _G.pending_levels.values[pos].flags;
            lstr_wipe(&_G.pending_levels.keys[pos]);
        }
    }

    logger->level = logger->default_level;

    if (logger->defined_level >= 0) {
        logger->level = logger->defined_level;
    } else
    if (logger->parent) {
        if (logger->parent->level_flags & (LOG_FORCED | LOG_RECURSIVE)) {
            logger->level = logger->parent->level;
            logger->level_flags |= LOG_FORCED;
        } else
        if (logger->level == LOG_INHERITS) {
            logger->level = logger->parent->level;
        }
    }

    assert (logger->level >= 0);
    atomic_store_explicit(&logger->conf_gen, log_conf_gen_g,
                          memory_order_release);
}

void __logger_refresh(logger_t *logger)
{
    if (atomic_load_explicit(&logger->conf_gen, memory_order_acquire)
        == log_conf_gen_g)
    {
        return;
    }

    log_spin_lock();
    __logger_do_refresh(logger);
    log_spin_unlock();
}

logger_t *logger_get_root(void)
{
    return &_G.root_logger;
}

logger_t *logger_get_by_name(lstr_t name)
{
    pstream_t ps = ps_initlstr(&name);
    logger_t *logger = &log_g.root_logger;

    while (!ps_done(&ps)) {
        logger_t *next  = NULL;
        pstream_t n;

        if (ps_get_ps_chr_and_skip(&ps, '/', &n) < 0) {
            n = ps;
            ps = ps_init(NULL, 0);
        }

        dlist_for_each_entry(logger_t, child, &logger->children, siblings) {
            if (lstr_equal(child->name, LSTR_PS_V(&n))) {
                next = child;
                break;
            }
        }

        RETHROW_P(next);
        logger = next;
    }

    return logger;
}

int logger_set_level(lstr_t name, int level, unsigned flags)
{
    logger_t *logger;

    log_spin_lock();
    logger = logger_get_by_name(name);

    assert (level >= LOG_UNDEFINED);
    assert ((flags & (LOG_RECURSIVE | LOG_SILENT)) == flags);
    assert (!(flags & LOG_RECURSIVE) || level >= 0);

    /* -2 == LOG_LEVEL_DEFAULT, which cannot be used here because it is
     * defined in core.iop, and we are in a source file of libcommon-minimal
     * that cannot use content from iop files. */
    if (level == -2) {
        level = LOG_DEFAULT;
    }

    if (!logger) {
        if (level == LOG_UNDEFINED) {
            int pos = qm_del_key(level, &_G.pending_levels, &name);

            if (pos >= 0) {
                lstr_wipe(&_G.pending_levels.keys[pos]);
            }
        } else {
            struct level l = { .level = level, .flags = flags };
            uint32_t pos;

            pos = qm_put(level, &_G.pending_levels, &name, l, QHASH_OVERWRITE);
            if (!(pos & QHASH_COLLISION)) {
                _G.pending_levels.keys[pos] = lstr_dup(name);
            }
        }
        log_spin_unlock();
        return LOG_UNDEFINED;
    }

    if (level == LOG_UNDEFINED) {
        logger->level_flags = logger->default_level_flags;
    } else {
        logger->level_flags = flags;
    }
    SWAP(int, logger->level, level);
    logger->defined_level = logger->level;
    log_conf_gen_g += 2;

    log_spin_unlock();
    return level;
}

int logger_reset_level(lstr_t name)
{
    return logger_set_level(name, LOG_UNDEFINED, 0);
}

/* }}} */
/* Logging {{{ */

/* syslog_is_critical allows you to know if a LOG_CRIT, LOG_ALERT or LOG_EMERG
 * logger has been called.
 *
 * It is mainly usefull for destructor function in order to skip some code
 * that shouldn't be run when the system is critical
 */
bool syslog_is_critical;

GENERIC_INIT(buffer_instance_t, buffer_instance);

static void buffer_instance_wipe(buffer_instance_t *buffer_instance)
{
    qv_wipe(&buffer_instance->vec_buffer);
}

static void free_last_buffer(void)
{
    if (log_thr_g.vec_buff_stack.len > log_thr_g.nb_buffer_started) {
        assert (log_thr_g.vec_buff_stack.len
            ==  log_thr_g.nb_buffer_started + 1);
        buffer_instance_wipe(tab_last(&log_thr_g.vec_buff_stack));
        qv_remove(&log_thr_g.vec_buff_stack,
                  log_thr_g.vec_buff_stack.len - 1);
        mem_stack_pool_pop(&log_thr_g.mp_stack);
    }
}

void log_start_buffering_filter(bool use_handler, int log_level)
{
    buffer_instance_t *buffer_instance;

    if (log_thr_g.nb_buffer_started) {
        const buffer_instance_t *buff;

        buff = &log_thr_g.vec_buff_stack.tab[log_thr_g.nb_buffer_started - 1];
        if (!buff->use_handler) {
            use_handler = false;
        }
    }
    free_last_buffer();
    buffer_instance = qv_growlen(&log_thr_g.vec_buff_stack, 1);

    buffer_instance_init(buffer_instance);
    buffer_instance->use_handler = use_handler;
    buffer_instance->buffer_log_level = log_level;

    mem_stack_pool_push(&log_thr_g.mp_stack);
    log_thr_g.nb_buffer_started++;
}

void log_start_buffering(bool use_handler)
{
    log_start_buffering_filter(use_handler, INT32_MAX);
}

const qv_t(log_buffer) *log_stop_buffering(void)
{
    buffer_instance_t *buffer_instance;

    if (!expect(log_thr_g.nb_buffer_started > 0)) {
        return NULL;
    }
    free_last_buffer();
    buffer_instance = tab_last(&log_thr_g.vec_buff_stack);
    log_thr_g.nb_buffer_started--;

    return &buffer_instance->vec_buffer;
}

static __attr_printf__(2, 0)
void logger_vsyslog(int level, const char *fmt, va_list va)
{
    SB_1k(sb);

    sb_addvf(&sb, fmt, va);
    syslog(LOG_USER | level, "%s", sb.data);
}

static __attr_printf__(3, 0)
void logger_putv(const log_ctx_t *ctx, bool do_log,
                 const char *fmt, va_list va)
{
    buffer_instance_t *buff;

    if (ctx->level <= LOG_CRIT) {
        va_list cpy;

        syslog_is_critical = true;
        va_copy(cpy, va);
        logger_vsyslog(ctx->level, fmt, cpy);
        va_end(cpy);
    }

    if (!do_log) {
        return;
    }

    if (!log_thr_g.nb_buffer_started) {
        (*_G.handler)(ctx, fmt, va);
        return;
    }

    buff = &log_thr_g.vec_buff_stack.tab[log_thr_g.nb_buffer_started - 1];

    if (ctx->level <= buff->buffer_log_level) {
        int size_fmt;
        log_buffer_t *log_save;
        va_list cpy;
        char *buffer;
        qv_t(log_buffer) *vec_buffer = &buff->vec_buffer;

        if (buff->use_handler) {
            va_copy(cpy, va);
            (*_G.handler)(ctx, fmt, cpy);
            va_end(cpy);
        }

        va_copy(cpy, va);
        size_fmt = vsnprintf(NULL, 0, fmt, cpy) + 1;
        va_end(cpy);

        free_last_buffer();
        buffer = mp_new_raw(&log_thr_g.mp_stack.funcs, char, size_fmt);
        vsnprintf(buffer, size_fmt, fmt, va);

        log_save = qv_growlen(vec_buffer, 1);
        log_save->ctx = *ctx;
        log_save->msg = LSTR_INIT_V(buffer, size_fmt - 1);
    }
}

static __attr_printf__(3, 4)
void logger_put(const log_ctx_t *ctx, bool do_log, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    logger_putv(ctx, do_log, fmt, va);
    va_end(va);
}

static __attr_printf__(2, 0)
void logger_put_in_buf(const log_ctx_t *ctx, const char *fmt, va_list ap)
{
    const char *p;
    sb_t *buf = &log_thr_g.buf;

    sb_addvf(buf, fmt, ap);

    while ((p = memchr(buf->data, '\n', buf->len))) {
        logger_put(ctx, true, "%*pM", (int)(p - buf->data), buf->data);
        sb_skip_upto(buf, p + 1);
    }
}

static __attr_noreturn__
void logger_do_fatal(void)
{
    if (psinfo_get_tracer_pid(0) > 0) {
        abort();
    }
    _exit(127);
}

int logger_vlog(logger_t *logger, int level, const char *prog, int pid,
                const char *file, const char *func, int line,
                const char *fmt, va_list va)
{
    log_ctx_t ctx = {
        .logger_name = lstr_dupc(logger->full_name),
        .level       = level,
        .file        = file,
        .func        = func,
        .line        = line,
        .pid         = pid < 0 ? _G.pid : pid,
        .prog_name   = prog ?: program_invocation_short_name,
        .is_silent   = !!(logger->level_flags & LOG_SILENT),
    };

    assert (atomic_load_explicit(&logger->conf_gen, memory_order_acquire)
            == log_conf_gen_g);
    logger_putv(&ctx, logger_has_level(logger, level) || level >= LOG_TRACE,
                fmt, va);
    return level <= LOG_WARNING ? -1 : 0;
}

int __logger_log(logger_t *logger, int level, const char *prog, int pid,
                 const char *file, const char *func, int line,
                 const char *fmt, ...)
{
    int res;
    va_list va;

    va_start(va, fmt);
    res = logger_vlog(logger, level, prog, pid, file, func, line, fmt, va);
    va_end(va);

    if (unlikely(level <= LOG_CRIT)) {
        logger_do_fatal();
    }

    return res;
}

void __logger_vpanic(logger_t *logger, const char *file, const char *func,
                     int line, const char *fmt, va_list va)
{
    __logger_refresh(logger);

    logger_vlog(logger, LOG_CRIT, NULL, -1, file, func, line, fmt, va);
    abort();
}

void __logger_panic(logger_t *logger, const char *file, const char *func,
                    int line, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    __logger_vpanic(logger, file, func, line, fmt, va);
}

void __logger_vfatal(logger_t *logger, const char *file, const char *func,
                    int line, const char *fmt, va_list va)
{
    __logger_refresh(logger);

    logger_vlog(logger, LOG_CRIT, NULL, -1, file, func, line, fmt, va);

    logger_do_fatal();
}

void __logger_fatal(logger_t *logger, const char *file, const char *func,
                    int line, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    __logger_vfatal(logger, file, func, line, fmt, va);
}

void __logger_vexit(logger_t *logger, const char *file, const char *func,
                    int line, const char *fmt, va_list va)
{
    va_list vc;

    __logger_refresh(logger);

    va_copy(vc, va);
    logger_vlog(logger, LOG_ERR, NULL, -1, file, func, line, fmt, va);
    logger_vsyslog(LOG_ERR, fmt, vc);

    _exit(0);
}

void __logger_exit(logger_t *logger, const char *file, const char *func,
                   int line, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    __logger_vexit(logger, file, func, line, fmt, va);
}

#ifndef NDEBUG

int __logger_is_traced(logger_t *logger, int lvl, const char *modname,
                       const char *func, const char *name)
{
    int level;

    lvl += LOG_TRACE;
    level = logger->level;

    for (int i = 0; i < _G.specs.len; i++) {
        log_trace_spec_t *spec = &_G.specs.tab[i];

        if (spec->path && fnmatch(spec->path, modname, FNM_PATHNAME) != 0)
            continue;
        if (spec->func && fnmatch(spec->func, func, 0) != 0)
            continue;
        if (spec->name
        &&  (name == NULL
         ||  fnmatch(spec->name, name, FNM_PATHNAME | FNM_LEADING_DIR) != 0))
        {
            continue;
        }
        level = spec->level;
    }
    return lvl > level ? -1 : 1;
}

#endif

void __logger_start(logger_t *logger, int level, const char *prog, int pid,
                    const char *file, const char *func, int line)
{
    assert (atomic_load_explicit(&logger->conf_gen, memory_order_acquire)
            == log_conf_gen_g);

    log_thr_g.ml_ctx = (log_ctx_t){
        .logger_name = lstr_dupc(logger->full_name),
        .level       = level,
        .file        = file,
        .func        = func,
        .line        = line,
        .pid         = pid < 0 ? _G.pid : pid,
        .prog_name   = prog ?: program_invocation_short_name,
        .is_silent   = !!(logger->level_flags & LOG_SILENT),
    };
}

void __logger_vcont(const char *fmt, va_list ap)
{
    if (log_thr_ml_g.activated) {
        logger_put_in_buf(&log_thr_g.ml_ctx, fmt, ap);
    }
}

void __logger_cont(const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    __logger_vcont(fmt, va);
    va_end(va);
}

void __logger_end(void)
{
    if (log_thr_ml_g.activated) {
        __logger_cont("\n");
    }
}

void __logger_end_panic(void)
{
    __logger_end();
    abort();
}

void __logger_end_fatal(void)
{
    __logger_end();
    logger_do_fatal();
}

/* }}} */
/* Handlers {{{ */

int log_make_fancy_prefix(const char * nonnull progname, int pid,
                          char fancy[static 64])
{
    static int colors[] = { 1, 2, 4, 5, 6, 9, 12, 14 };
    uint32_t color;
    int len;

    color = mem_hash32(progname, strlen(progname));

    color = colors[color % countof(colors)];
    len = snprintf(fancy, 64, "\e[%d;%dm%10s[%d]\e[0m: ", color >= 10 ? 1 : 0,
                   (color >= 10 ? 20 : 30) + color, progname, pid);
    return MIN(len, 63);
}

static void log_add_timestamp(sb_t *sb)
{
    if (_G.log_timestamp) {
        struct timeval tv;

        lp_gettv(&tv);
        sb_addf(sb, "%ld.%02ld ", tv.tv_sec, (long)tv.tv_usec / 10000);
    }
}

__attr_printf__(2, 0)
static void log_stderr_fancy_handler(const log_ctx_t *ctx, const char *fmt,
                                     va_list va)
{
    sb_t *sb  = &log_thr_g.log;
    int max_len = _G.cols - 2;

    if (ctx->is_silent) {
        return;
    }

    if (ctx->level >= LOG_TRACE) {
        int len;
        char escapes[BUFSIZ];

        sb_setf(sb, "%s:%d:%s[%d]", ctx->file, ctx->line,
                ctx->prog_name, ctx->pid);
        if (sb->len > max_len) {
            sb_clip(sb, max_len);
        }
        len = snprintf(escapes, sizeof(escapes), "\r\e[%dC\e[7m ",
                       max_len - sb->len);
        sb_splice(sb, 0, 0, escapes, len);
        sb_adds(sb, " \e[0m\r");

        log_add_timestamp(sb);

        sb_adds(sb, "\e[33m");
        if (strlen(ctx->func) < 17) {
            sb_addf(sb, "%*s: ", 17, ctx->func);
        } else {
            sb_addf(sb, "%*pM...: ", 14, ctx->func);
        }
    } else {
        log_add_timestamp(sb);
        if (ctx->prog_name == program_invocation_short_name) {
            sb_add(sb, _G.fancy_prefix, _G.fancy_len);
        } else {
            char fancy[64];
            int len;

            len = log_make_fancy_prefix(ctx->prog_name, ctx->pid, fancy);
            sb_add(sb, fancy, len);
        }
    }
    if (ctx->logger_name.len) {
        sb_addf(sb, "\e[1;30m{%*pM} ", LSTR_FMT_ARG(ctx->logger_name));
    }
    switch (ctx->level) {
      case LOG_DEBUG:
      case LOG_INFO:
        sb_adds(sb, "\e[39;3m");
        break;
      case LOG_WARNING:
        sb_adds(sb, "\e[33;1m");
        break;
      case LOG_ERR:
        sb_adds(sb, "\e[31;1m");
        break;
      case LOG_CRIT:
      case LOG_ALERT:
      case LOG_EMERG:
        sb_adds(sb, "\e[41;37;1m");
        break;
      default:
        sb_adds(sb, "\e[0m");
        break;
    }
    sb_addvf(sb, fmt, va);
    sb_adds(sb, "\e[0m\n");

    fputs(sb->data, stderr);
    if (log_stderr_handler_teefd_g >= 0) {
        IGNORE(xwrite(log_stderr_handler_teefd_g, sb->data, sb->len));
    }
    sb_reset(sb);
}

static void log_initialize_thread(void);

__attr_printf__(2, 0)
static void log_stderr_raw_handler(const log_ctx_t *ctx, const char *fmt,
                                   va_list va)
{
    static char const *prefixes[] = {
        [LOG_CRIT]     = "fatal: ",
        [LOG_ERR]      = "error: ",
        [LOG_WARNING]  = "warn:  ",
        [LOG_NOTICE]   = "note:  ",
        [LOG_INFO]     = "info:  ",
        [LOG_DEBUG]    = "debug: ",
        [LOG_TRACE]    = "trace: ",
    };

    sb_t *sb = &log_thr_g.log;

    if (!log_thr_g.inited) {
        log_initialize_thread();
    }

    if (ctx->is_silent) {
        return;
    }

    log_add_timestamp(sb);
    sb_addf(sb, "%s[%d]: ", ctx->prog_name, ctx->pid);
    if (ctx->level >= LOG_TRACE && ctx->func) {
        sb_addf(sb, "%s:%d:%s: ", ctx->file, ctx->line, ctx->func);
    } else {
        sb_adds(sb, prefixes[MIN(LOG_TRACE, ctx->level)]);
    }
    if (ctx->logger_name.len) {
        sb_addf(sb, "{%*pM} ", LSTR_FMT_ARG(ctx->logger_name));
    }
    sb_addvf(sb, fmt, va);
    sb_addc(sb, '\n');

    fputs(sb->data, stderr);
    if (log_stderr_handler_teefd_g >= 0) {
        IGNORE(xwrite(log_stderr_handler_teefd_g, sb->data, sb->len));
    }
    sb_reset(sb);
}

log_handler_f *log_set_handler(log_handler_f *handler)
{
    SWAP(log_handler_f *, _G.handler, handler);
    return handler;
}

/* }}} */
/* Backward compatibility e_*() {{{ */

int e_log(int priority, const char *fmt, ...)
{
    int ret;
    va_list va;

    va_start(va, fmt);
    ret = logger_vlog(&_G.root_logger, priority, NULL, -1, NULL, NULL, -1,
                      fmt, va);
    va_end(va);
    return ret;
}

int e_panic(const char *fmt, ...)
{
    va_list va;

    __logger_refresh(&_G.root_logger);

    va_start(va, fmt);
    logger_vlog(&_G.root_logger, LOG_CRIT, NULL, -1, NULL, NULL, -1, fmt, va);
    abort();
}

int e_fatal(const char *fmt, ...)
{
    va_list va;

    __logger_refresh(&_G.root_logger);

    va_start(va, fmt);
    logger_vlog(&_G.root_logger, LOG_CRIT, NULL, -1, NULL, NULL, -1, fmt, va);

    if (psinfo_get_tracer_pid(0) > 0) {
        abort();
    }
    _exit(127);
}

#define E_FUNCTION(Name, Level)                                              \
    int Name(const char *fmt, ...)                                           \
    {                                                                        \
        if (logger_has_level(&_G.root_logger, (Level))) {                    \
            int ret;                                                         \
            va_list va;                                                      \
                                                                             \
            va_start(va, fmt);                                               \
            ret = logger_vlog(&_G.root_logger, (Level), NULL, -1, NULL,      \
                              NULL, -1, fmt, va);                            \
            va_end(va);                                                      \
            return ret;                                                      \
        }                                                                    \
        return (Level) <= LOG_WARNING ? -1 : 0;                              \
    }
E_FUNCTION(e_error,   LOG_ERR)
E_FUNCTION(e_warning, LOG_WARNING)
E_FUNCTION(e_notice,  LOG_NOTICE)
E_FUNCTION(e_info,    LOG_INFO)
E_FUNCTION(e_debug,   LOG_DEBUG)

#undef E_FUNCTION

__attr_printf__(2, 0)
static void e_handler(const log_ctx_t *ctx, const char *fmt, va_list va)
{
    if (ctx->level >= LOG_TRACE) {
        (*log_stderr_handler_g)(ctx, fmt, va);
    } else {
        (*_G.e_handler)(ctx->level, fmt, va);
    }
}

void e_set_handler(e_handler_f *handler)
{
    _G.e_handler = handler;
    log_set_handler(&e_handler);
}

void e_init_stderr(void)
{
    _G.handler = log_stderr_handler_g;
}

#ifndef NDEBUG

void e_set_verbosity(int max_debug_level)
{
    logger_set_level(LSTR_EMPTY_V, LOG_TRACE + max_debug_level, 0);
}

void e_incr_verbosity(void)
{
    logger_set_level(LSTR_EMPTY_V, log_g.root_logger.level + 1, 0);
}

int e_is_traced_(int lvl, const char *modname, const char *func,
                 const char *name)
{
    logger_t *logger;

    log_spin_lock();
    logger = logger_get_by_name(LSTR_OPT(name)) ?: &log_g.root_logger;
    log_spin_unlock();
    return __logger_is_traced(logger, lvl, modname, func, name);
}

void e_trace_put_(int level, const char *module, int lno,
                  const char *func, const char *name, const char *fmt, ...)
{
    va_list ap;
    log_ctx_t ctx = {
        .logger_name = LSTR_OPT(name),
        .level       = LOG_TRACE + level,
        .file        = module,
        .func        = func,
        .line        = lno,
        .pid         = _G.pid,
        .prog_name   = program_invocation_short_name,
    };

    va_start(ap, fmt);
    logger_put_in_buf(&ctx, fmt, ap);
    va_end(ap);
}

#endif

/* }}} */
/* Module {{{ */

static void on_sigwinch(int signo)
{
    term_get_size(&_G.cols, &_G.rows);
}

static void log_initialize_thread(void)
{
    if (!log_thr_g.inited) {
        sb_init(&log_thr_g.log);
        sb_init(&log_thr_g.buf);

        mem_stack_pool_init(&log_thr_g.mp_stack, "log", 16 << 10);
        qv_init(&log_thr_g.vec_buff_stack);

        log_thr_g.inited = true;
    }
}

static void log_shutdown_thread(void)
{
    if (log_thr_g.inited) {
        sb_wipe(&log_thr_g.buf);
        sb_wipe(&log_thr_g.log);

        qv_deep_wipe(&log_thr_g.vec_buff_stack, buffer_instance_wipe);
        mem_stack_pool_wipe(&log_thr_g.mp_stack);

        log_thr_g.inited = false;
    }
}
thr_hooks(log_initialize_thread, log_shutdown_thread);

static void log_atfork(void)
{
    _G.pid = getpid();
}

/** Parse the content of the IS_DEBUG environment variable.
 *
 * It is composed of a series of blank separated <specs>:
 *
 *  <specs> ::= [<path-pattern>][@<funcname>][+<featurename>][:<level>]
 */
void log_parse_specs(char *p, qv_t(spec) *out)
{
    pstream_t ps = ps_initstr(p);
    ctype_desc_t ctype;

    ps_trim(&ps);

    ctype_desc_build(&ctype, "@+:");

    while (!ps_done(&ps)) {
        log_trace_spec_t spec = {
            .path = NULL,
            .func = NULL,
            .name = NULL,
            .level = INT_MAX,
        };
        pstream_t spec_ps;
        int c = '\0';

        spec_ps = ps_get_cspan(&ps, &ctype_isspace);
        ps_skip_span(&ps, &ctype_isspace);

#define GET_ELEM(_dst)  \
        do {                                                                 \
            pstream_t elem = ps_get_cspan(&spec_ps, &ctype);                 \
                                                                             \
            if (ps_len(&elem)) {                                             \
                _dst = elem.s;                                               \
            }                                                                \
            c = *spec_ps.s;                                                  \
            *(char *)spec_ps.s = '\0';                                       \
            ps_skip(&spec_ps, 1);                                            \
        } while (0)

        GET_ELEM(spec.path);
        if (c == '@') {
            GET_ELEM(spec.func);
        }
        if (c == '+') {
            GET_ELEM(spec.name);
        }
        if (c == ':') {
            const char *level = NULL;

            GET_ELEM(level);
            if (level) {
                spec.level = LOG_TRACE + atoi(level);
            }
        }
#undef GET_ELEM

        qv_append(out, spec);
    }
}

qv_t(spec) *log_get_specs(void)
{
    return &_G.specs;
}

static int log_initialize(void* args)
{
    char *env;

    qv_init(&_G.specs);
    _G.fancy = is_fancy_fd(STDERR_FILENO);
    _G.pid   = getpid();
    log_stderr_handler_g = &log_stderr_raw_handler;
    if (_G.fancy) {
        term_get_size(&_G.cols, &_G.rows);
        signal(SIGWINCH, &on_sigwinch);
        log_stderr_handler_g = &log_stderr_fancy_handler;
        _G.fancy_len = log_make_fancy_prefix(program_invocation_short_name,
                                             _G.pid, _G.fancy_prefix);
    }

    _G.handler = log_stderr_handler_g;

    log_initialize_thread();

    if ((env = getenv("IS_DEBUG"))) {
        _G.is_debug = p_strdup(env);
        log_parse_specs(_G.is_debug, &_G.specs);

        tab_for_each_ptr(spec, &_G.specs) {
            if (!spec->func && !spec->path) {
                logger_set_level(LSTR_OPT(spec->name), spec->level, 0);
            }
        }
    }

    if ((env = getenv("IS_LOG_TIMESTAMP"))) {
        _G.log_timestamp = *env && atoi(env) > 0;
    }

    return 0;
}

static int log_shutdown(void)
{
    logger_wipe(&_G.root_logger);
    qm_deep_wipe(level, &_G.pending_levels, lstr_wipe, IGNORE);
    qv_wipe(&_G.specs);
    p_delete(&_G.is_debug);
    return 0;
}

module_t *log_module_g;

__attribute__((constructor))
void log_module_register(void)
{
    if (log_module_g) {
        return;
    }

    thr_hooks_register();
    iop_module_register();
    log_module_g = module_implement(MODULE(log), &log_initialize,
                                    &log_shutdown, MODULE(iop));
    module_add_dep(log_module_g, MODULE(thr_hooks));
    module_implement_method(log_module_g, &at_fork_on_child_method,
                            &log_atfork);

#ifdef MEM_BENCH
    mem_bench_require();
#endif
}

/* }}} */
