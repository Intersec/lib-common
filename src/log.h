/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_LOG_H
#define IS_LIB_COMMON_LOG_H

#include <lib-common/container-qvector.h>
#include <lib-common/core.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

/** \defgroup log Logging facility.
 * \ingroup log
 * \brief Logging Facility.
 *
 * \{
 *
 * \section log_intro Loggers
 *
 * This module provides a hierarchical logging facility. This is done through
 * a tree of \ref logger_t structures. Each logger comes with it's own logging
 * level (no log with a level greater to that one will be printed), that can
 * be inherited from its parent.
 *
 * Each \ref logger_t also comes with a name, that name can be printed in the
 * logs, allowing more contextual informations to be provided.
 *
 * When a log is rejected it costs near to 0 since only an integer comparison
 * is performed. This let you put logging even in critical paths with low
 * impact on global performances. Arguments of the logging functions are
 * evaluated only in case the log is emitted. As a consequence, you must take
 * care only passing arguments that have no side effects.
 *
 *
 * Every logger is initialized with a default logging level. That level can be
 * either an inheritance mark, in which case the logger will always inherits
 * its level from its parent (unless explicitely set for that particular
 * logger).
 *
 * You can manage the logging of every single logger independently from each
 * other or choose to update the level of the whole tree of loggers at once.
 * This gives you (and the user) a fine-grained control on what should be
 * logged.
 *
 *
 * \section log_hander Handlers
 *
 * When a log is accepted, it is pushed to a handler that performs the output.
 * The handler can be changed by a call to \ref log_set_handler. A default
 * handler that prints on stderr is provided.
 *
 *
 * \section log_scope Logging scope
 *
 * The logging facility provides two main APIs:
 *  - the classic API that formats a whole line.
 *  - a scoped API that let you build one or more line chunk-by-chunk.
 *
 * The scoped API works by delimiting a logging scope in which you will build
 * the logs using calls to \ref logger_cont ("continue logging"). The scope
 * itself is delimited by a starting call to \ref logger_*_start and an
 * ending call to \ref logger_end. However, a more convenient solution
 * consists in using \ref logger_*_scope that automatically start and stop the
 * scope according to the C syntaxic scope.
 *
 * <pre>
 *  {
 *      logger_trace_scope(&logger, 1);
 *
 *      logger_cont("start ");
 *      logger_cont("writing %d ", 2);
 *      logger_cont("logs");
 *  }
 * </pre>
 */

/* Logger {{{ */

extern uint32_t log_conf_gen_g;

/** Minimum level for tracing.
 *
 * You can use any level equal or greater to that value as tracing.
 */
#define LOG_TRACE        (LOG_DEBUG + 1)

/** Log level is inherited from the parent logger.
 */
#define LOG_INHERITS     (-1)

/** No level defined from the logger.
 *
 * This is a private value, used only internally.
 * Note that -2 is not used because this is the value for DEFAULT in the IOP
 * enum LogLevel.
 */
#define LOG_UNDEFINED    (-3)

enum {
    LOG_RECURSIVE = 1 << 0, /**< Force level to be set recursively. */
    LOG_FORCED    = 1 << 1, /**< Level has been recursively forced. internal */
    LOG_SILENT    = 1 << 2, /**< Log handler is called, but default one does
                                 nothing. */
};
#define LOG_MK_FLAGS(_recursive, _silent)  (_recursive << 0 | _silent << 2)

/** Logger structure.
 *
 * That structure bears the name of the logging module and the level it
 * accepts. It can be either allocated (see \ref logger_init, \ref logger_new)
 * or static (see \ref LOGGER_INIT).
 *
 * You should not touch the fields of that structure. It is provided only for
 * the sake of inlining.
 */
typedef struct logger_t {
    atomic_uint conf_gen;
    bool is_static : 1;

    int level;
    int defined_level;
    int default_level;
    unsigned level_flags;
    unsigned default_level_flags;

    lstr_t name;
    lstr_t full_name;
    struct logger_t * nullable parent;
    dlist_t children;
    dlist_t siblings;
} logger_t;

/** Initialize a logger with a default \p LogLevel.
 *
 * Note that you don't have to wipe a logger initialized with LOGGER_INIT and
 * derivates.
 *
 * \param[in] Parent    The parent logger (NULL if parent is the root logger)
 * \param[in] Name      The name of the logger
 * \param[in] LogLevel  The maximum log level activated by default for that
 *                      logger (can be \ref LOG_INHERITS).
 */
#define LOGGER_INIT(Parent, Name, LogLevel)  {                               \
        .is_static     = true,                                               \
        .parent        = (Parent),                                           \
        .name          = LSTR_IMMED(Name),                                   \
        .level         = LOG_UNDEFINED,                                      \
        .defined_level = LOG_UNDEFINED,                                      \
        .default_level = (LogLevel),                                         \
    }

/** Initialize a silent logger with a default \p LogLevel.
 *
 * Silent loggers are loggers for which the default log handler does nothing.
 * This can be used if you want the log handler to be called even if you don't
 * want to print anything.
 *
 * \see LOGGER_INIT
 */
#define LOGGER_INIT_SILENT(Parent, Name, LogLevel)  {                        \
        .is_static           = true,                                         \
        .parent              = (Parent),                                     \
        .name                = LSTR_IMMED(Name),                             \
        .level               = LOG_UNDEFINED,                                \
        .defined_level       = LOG_UNDEFINED,                                \
        .default_level       = (LogLevel),                                   \
        .level_flags         = LOG_SILENT,                                   \
        .default_level_flags = LOG_SILENT,                                   \
    }

/** Initialize a logger that inherits it's parent level.
 *
 * \see LOGGER_INIT
 */
#define LOGGER_INIT_INHERITS(Parent, Name)                                   \
    LOGGER_INIT(Parent, Name, LOG_INHERITS)

/** Initialize a silent logger that inherits it's parent level.
 *
 * \see LOGGER_INIT_SILENT
 */
#define LOGGER_INIT_SILENT_INHERITS(Parent, Name)                            \
    LOGGER_INIT_SILENT(Parent, Name, LOG_INHERITS)

/** Initialize a logger.
 *
 * \param[in]  parent  the parent logger (NULL if parent is the root logger).
 * \param[in]  name  the name of the logger. Note that it can be allocated the
 *                   way you want, this lstr will be dup'ed.
 * \param[in]  default_level  the maximum log level activated by default for
 *                            that logger (can be \ref LOG_INHERITS).
 * \param[in]  level_flags  the flags to use (ie. \ref LOG_SILENT or 0).
 */
logger_t * nonnull logger_init(logger_t * nonnull logger,
                               logger_t *nullable parent,
                               lstr_t name, int default_level,
                               unsigned level_flags) __leaf;
logger_t * nonnull logger_new(logger_t *nullable parent, lstr_t name,
                              int default_level, unsigned level_flags) __leaf;

void logger_wipe(logger_t * nonnull logger) __leaf;
GENERIC_DELETE(logger_t, logger)

/* }}} */
/* Private stuff {{{ */

void log_spin_lock(void);
void log_spin_unlock(void);

logger_t * nonnull logger_get_root(void);
logger_t * nullable logger_get_by_name(lstr_t name);
void __logger_refresh(logger_t * nonnull logger) __leaf __cold;
void __logger_do_refresh(logger_t * nonnull logger);

static ALWAYS_INLINE
int logger_get_level(logger_t * nonnull logger)
{
    if (atomic_load_explicit(&logger->conf_gen, memory_order_acquire)
        != log_conf_gen_g)
    {
        __logger_refresh(logger);
    }

    /* We always want to catch fatal/panic issues which are associated to the
     * LOG_CRIT level.
     */
    return MAX(logger->level, LOG_CRIT);
}

static ALWAYS_INLINE
bool logger_has_level(logger_t * nonnull logger, int level)
{
    return logger_get_level(logger) >= level;
}

static ALWAYS_INLINE __cold
void __logger_cold(void)
{
    /* This function is just a marker for error cases */
}

typedef struct log_trace_spec_t {
    const char * nullable path;
    const char * nullable func;
    const char * nullable name;
    int level;
} log_trace_spec_t;
qvector_t(spec, log_trace_spec_t);

void log_parse_specs(char * nonnull p, qv_t(spec) * nonnull out);
qv_t(spec) * nonnull log_get_specs(void);

void log_module_register(void);

/* }}} */
/* Simple logging {{{ */

#ifndef NDEBUG

int __logger_is_traced(logger_t * nonnull logger, int level,
                       const char * nonnull file, const char * nonnull func,
                       const char * nullable name);

#define logger_is_traced(Logger, Level)  ({                                  \
        static int8_t __logger_traced;                                       \
        static const logger_t *__last_logger = NULL;                         \
        const logger_t *__i_clogger = (Logger);                              \
        logger_t *__i_logger = (logger_t *)__i_clogger;                      \
        const int __logger_i_level = (Level);                                \
        bool __logger_h_level = logger_has_level(__i_logger,                 \
                                    LOG_TRACE + __logger_i_level);           \
                                                                             \
        if (!__logger_h_level) {                                             \
            if (unlikely(!__builtin_constant_p(Level)                        \
                       || __i_clogger != __last_logger))                     \
            {                                                                \
                __logger_traced = __logger_is_traced(__i_logger,             \
                                      __logger_i_level,__FILE__, __func__,   \
                                      __i_logger->full_name.s);              \
                __last_logger = __i_clogger;                                 \
            }                                                                \
        }                                                                    \
        __logger_h_level || __logger_traced > 0;                             \
    })

#define __LOGGER_HAS_LEVEL(__logger, __level)                                \
    ((__level >= LOG_TRACE) ?                                                \
     logger_is_traced(__logger, __level - LOG_TRACE) :                       \
     logger_has_level(__logger, __level))

#else

#define logger_is_traced(Logger, Level)  ({                                  \
        const logger_t *__i_clogger = (Logger);                              \
        logger_t *__i_logger = (logger_t *)__i_clogger;                      \
                                                                             \
        logger_has_level(__i_logger, (Level) + LOG_TRACE);                   \
    })

#define __LOGGER_HAS_LEVEL(__logger, __level)                              \
    logger_has_level(__logger, __level)

#endif

__attr_printf__(8, 0)
int logger_vlog(logger_t * nonnull logger, int level,
                const char * nullable prog, int pid,
                const char * nullable file, const char * nullable func,
                int line, const char * nonnull fmt, va_list va);

__attr_printf__(8, 9)
int __logger_log(logger_t * nonnull logger, int level,
                 const char * nullable prog, int pid,
                 const char * nonnull file, const char * nonnull func,
                 int line, const char * nonnull fmt, ...);

__attr_printf__(5, 0) __attr_noreturn__ __cold
void __logger_vpanic(logger_t * nonnull logger, const char * nonnull file,
                     const char * nonnull func, int line,
                     const char * nonnull fmt, va_list va);
__attr_printf__(5, 6) __attr_noreturn__ __cold
void __logger_panic(logger_t * nonnull logger, const char * nonnull file,
                    const char * nonnull func, int line,
                    const char * nonnull fmt, ...);

__attr_noreturn__ __cold
static inline void __logger_panics(logger_t * nonnull logger,
                                   const char * nonnull file,
                                   const char * nonnull func, int line,
                                   const char * nullable msg)
{
    __logger_panic(logger, file, func, line, "%s", msg);
}


__attr_printf__(5, 0) __attr_noreturn__ __cold
void __logger_vfatal(logger_t * nonnull logger, const char * nonnull file,
                     const char * nonnull func, int line,
                     const char * nonnull fmt, va_list va);
__attr_printf__(5, 6) __attr_noreturn__ __cold
void __logger_fatal(logger_t * nonnull logger, const char * nonnull file,
                    const char * nonnull func, int line,
                    const char * nonnull fmt, ...);

__attr_noreturn__ __cold
static inline void __logger_fatals(logger_t * nonnull logger,
                                   const char * nonnull file,
                                   const char * nonnull func, int line,
                                   const char * nullable msg)
{
    __logger_fatal(logger, file, func, line, "%s", msg);
}


__attr_printf__(5, 0) __attr_noreturn__ __cold
void __logger_vexit(logger_t * nonnull logger, const char * nonnull file,
                    const char * nonnull func, int line,
                    const char * nonnull fmt, va_list va);
__attr_printf__(5, 6) __attr_noreturn__ __cold
void __logger_exit(logger_t * nonnull logger, const char * nonnull file,
                   const char * nonnull func, int line,
                   const char * nonnull fmt, ...);

__attr_noreturn__ __cold
static inline void __logger_exits(logger_t * nonnull logger,
                                  const char * nonnull file,
                                  const char * nonnull func, int line,
                                  const char * nullable msg)
{
    __logger_exit(logger, file, func, line, "%s", msg);
}

#define logger_panic(Logger, Fmt, ...)                                       \
    __logger_panic((Logger), __FILE__, __func__, __LINE__, (Fmt), ##__VA_ARGS__)

#define logger_fatal(Logger, Fmt, ...)                                       \
    __logger_fatal((Logger), __FILE__, __func__, __LINE__, (Fmt), ##__VA_ARGS__)

#define logger_exit(Logger, Fmt, ...)                                        \
    __logger_exit((Logger), __FILE__, __func__, __LINE__, (Fmt), ##__VA_ARGS__)

#define __LOGGER_LOG(Logger, Level, Mark, Fmt, ...)  ({                      \
        const logger_t *__clogger = (Logger);                                \
        logger_t *__logger = (logger_t *)__clogger;                          \
        int __logger_res;                                                    \
                                                                             \
        Mark;                                                                \
        /* XXX We need two different cases: the first one (Level non-const)  \
         * to protect Level against multi-evaluation; the second one to      \
         * preserve the constness of Level until we reach logger_is_traced() \
         * and allow its __builtin_constant_p to work even in O0.            \
         */                                                                  \
        if (unlikely(!__builtin_constant_p(Level))) {                        \
            const int __logger_level = (Level);                              \
                                                                             \
            if (__LOGGER_HAS_LEVEL(__logger, __logger_level)) {              \
                __logger_log(__logger, __logger_level, NULL, -1, __FILE__,   \
                             __func__, __LINE__, Fmt, ##__VA_ARGS__);        \
            }                                                                \
            __logger_res = __logger_level <= LOG_WARNING ? -1 : 0;           \
        } else {                                                             \
            if (__LOGGER_HAS_LEVEL(__logger, (Level))) {                     \
                __logger_log(__logger, (Level), NULL, -1, __FILE__,          \
                             __func__,  __LINE__, Fmt, ##__VA_ARGS__);       \
            }                                                                \
            __logger_res = (Level) <= LOG_WARNING ? -1 : 0;                  \
        }                                                                    \
        __logger_res;                                                        \
    })

#define logger_log(Logger, Level, Fmt, ...)                                  \
    __LOGGER_LOG(Logger, Level,, Fmt, ##__VA_ARGS__)

#define logger_error(Logger, Fmt, ...)                                       \
    __LOGGER_LOG(Logger, LOG_ERR, __logger_cold(), Fmt, ##__VA_ARGS__)

#define logger_warning(Logger, Fmt, ...)                                     \
    __LOGGER_LOG(Logger, LOG_WARNING, __logger_cold(), Fmt, ##__VA_ARGS__)

#define logger_notice(Logger, Fmt, ...)                                      \
    __LOGGER_LOG(Logger, LOG_NOTICE,, Fmt, ##__VA_ARGS__)

#define logger_info(Logger, Fmt, ...)                                        \
    __LOGGER_LOG(Logger, LOG_INFO,, Fmt, ##__VA_ARGS__)

#define logger_debug(Logger, Fmt, ...)                                       \
    __LOGGER_LOG(Logger, LOG_DEBUG,, Fmt, ##__VA_ARGS__)

#define logger_trace(Logger, Level, Fmt, ...)                                \
    __LOGGER_LOG(Logger, LOG_TRACE + (Level),, Fmt, ##__VA_ARGS__)

/* }}} */
/* Multi-line logging {{{ */

typedef struct log_thr_ml_t {
    logger_t * nullable logger;
    bool activated;
} log_thr_ml_t;

extern __thread log_thr_ml_t log_thr_ml_g;

void __logger_start(logger_t * nonnull logger, int level,
                    const char * nullable prog, int pid,
                    const char * nonnull file,
                    const char * nonnull func, int line);

__attr_printf__(1, 2)
void __logger_cont(const char * nonnull fmt, ...);

__attr_printf__(1, 0)
void __logger_vcont(const char * nonnull fmt, va_list va);

void __logger_end(void);

__attr_noreturn__ __cold
void __logger_end_fatal(void);

__attr_noreturn__ __cold
void __logger_end_panic(void);


#define __logger_log_start(Logger, Level, Mark)  ({                          \
        const logger_t *__clogger = (Logger);                                \
        logger_t *__logger = (logger_t *)__clogger;                          \
        const int __level = (Level);                                         \
                                                                             \
        Mark;                                                                \
        assert (!log_thr_ml_g.logger);                                       \
        log_thr_ml_g.logger = __logger;                                      \
        if (logger_has_level(__logger, __level)) {                           \
            __logger_start(__logger, __level, NULL, -1, __FILE__, __func__,  \
                           __LINE__);                                        \
            log_thr_ml_g.activated = true;                                   \
        }                                                                    \
        __logger;                                                            \
    })

#define logger_panic_start(Logger)   __logger_log_start((Logger), LOG_CRIT,)

#define logger_fatal_start(Logger)   __logger_log_start((Logger), LOG_CRIT,)

#define logger_error_start(Logger)                                           \
    __logger_log_start((Logger), LOG_ERR, __logger_cold())

#define logger_warning_start(Logger)                                         \
    __logger_log_start((Logger), LOG_WARNING, __logger_cold())

#define logger_notice_start(Logger)  __logger_log_start((Logger), LOG_NOTICE,)

#define logger_info_start(Logger)    __logger_log_start((Logger), LOG_INFO,)

#define logger_debug_start(Logger)   __logger_log_start((Logger), LOG_DEBUG,)


#define __logger_trace_start(Logger, Level, Mark)  ({                        \
        const logger_t *__clogger = (Logger);                                \
        logger_t *__logger = (logger_t *)__clogger;                          \
        const int __level = (Level);                                         \
                                                                             \
        assert (!log_thr_ml_g.logger);                                       \
        log_thr_ml_g.logger = __logger;                                      \
        if (logger_is_traced(__logger, __level)) {                           \
            __logger_start(__logger, LOG_TRACE + __level, NULL, -1, __FILE__,\
                           __func__, __LINE__);                              \
            log_thr_ml_g.activated = true;                                   \
        }                                                                    \
        __logger;                                                            \
    })

#define logger_trace_start(Logger, Level)                                    \
    __logger_trace_start((Logger), (Level),)

static inline void logger_end(logger_t * nonnull logger)
{
    assert (logger == log_thr_ml_g.logger);

    if (log_thr_ml_g.activated) {
        __logger_end();
    }
    log_thr_ml_g.logger = NULL;
    log_thr_ml_g.activated = false;
}

static inline void _logger_end(logger_t * nonnull * nonnull logger)
{
    logger_end(*logger);
}

static inline void logger_end_fatal(logger_t * nonnull logger)
{
    assert (logger == log_thr_ml_g.logger);

    __logger_end_fatal();
}

static inline void _logger_end_fatal(logger_t * nonnull * nonnull logger)
{
    logger_end_fatal(*logger);
}

static inline void logger_end_panic(logger_t * nonnull logger)
{
    assert (logger == log_thr_ml_g.logger);

    __logger_end_panic();
}

static inline void _logger_end_panic(logger_t * nonnull * nonnull logger)
{
    logger_end_panic(*logger);
}

#define logger_cont(Fmt, ...)  do {                                          \
        assert (log_thr_ml_g.logger);                                        \
        if (log_thr_ml_g.activated) {                                        \
            __logger_cont(Fmt, ##__VA_ARGS__);                               \
        }                                                                    \
    } while (0)

#define logger_vcont(Fmt, va)  do {                                          \
        assert (log_thr_ml_g.logger);                                        \
        if (log_thr_ml_g.activated) {                                        \
            __logger_vcont(Fmt, va);                                         \
        }                                                                    \
    } while (0)

#ifndef __cplusplus

#define ___logger_scope(Logger, Level, Mark, Start, End, n)                  \
    logger_t *l_scope##n __attribute__((unused,cleanup(End)))                \
        = Start(Logger, Level, Mark)

#define __logger_scope(Logger, Level, Mark, Start, End, n)                   \
    ___logger_scope((Logger), (Level), Mark, Start, End, n)

#define _logger_scope(Logger, Level, Mark, Start, End)                       \
    __logger_scope((Logger), (Level), Mark, Start, End, __LINE__)

#define logger_panic_scope(Logger)                                           \
    _logger_scope((Logger), LOG_CRIT,, __logger_log_start, _logger_end_panic)

#define logger_fatal_scope(Logger)                                           \
    _logger_scope((Logger), LOG_CRIT,, __logger_log_start, _logger_end_fatal)

#define logger_error_scope(Logger)                                           \
    _logger_scope((Logger), LOG_ERR, __logger_cold(),                        \
                  __logger_log_start, _logger_end)

#define logger_warning_scope(Logger)                                         \
    _logger_scope((Logger), LOG_WARNING, __logger_cold(),                    \
                  __logger_log_start, _logger_end)

#define logger_notice_scope(Logger)                                          \
    _logger_scope((Logger), LOG_NOTICE,, __logger_log_start, _logger_end)

#define logger_info_scope(Logger)                                            \
    _logger_scope((Logger), LOG_INFO,, __logger_log_start, _logger_end)

#define logger_debug_scope(Logger)                                           \
    _logger_scope((Logger), LOG_DEBUG,, __logger_log_start, _logger_end)


#define logger_trace_scope(Logger, Level)                                    \
    _logger_scope((Logger), (Level),, __logger_trace_start, _logger_end)

#endif

/* }}} */
/* Configuration {{{ */

/** Set the maximum logging level for \p logger.
 *
 * \param[in] name   The full name of the logger that should be updated
 * \param[in] level  The new level (can be LOG_INHERITS)
 * \param[in] flags  The flags to apply. (LOG_RECURSIVE if you want that
 *                   level to all children with no forced level).
 * \return The previous level.
 */
int logger_set_level(lstr_t name, int level, unsigned flags) __leaf;

/** Reset the maximum logging level of \p logger to its default level.
 *
 * \param[in] name  The full name of the logger that should be updated
 * \return The previous level.
 */
int logger_reset_level(lstr_t name) __leaf;

/* }}} */
/* Handlers {{{ */

typedef struct log_ctx_t {
    int    level;
    lstr_t logger_name;

    const char * nonnull file;
    const char * nonnull func;
    int line;

    int pid;
    const char * nonnull prog_name;

    bool is_silent :  1;
    unsigned padding : 31;
} log_ctx_t;

typedef void (log_handler_f)(const log_ctx_t * nonnull ctx,
                             const char * nonnull fmt, va_list va)
    __attr_printf__(2, 0);

/** Default log handler.
 *
 * That log handler prints on stderr.
 */
extern log_handler_f * nonnull log_stderr_handler_g;

/** Default log handler tee fd.
 *
 * If set, the default log handler will also print on this fd.
 */
extern int log_stderr_handler_teefd_g;

/** Change the handler.
 *
 * This also returns the previous handler.
 */
log_handler_f * nonnull log_set_handler(log_handler_f * nonnull handler);

/* }}} */
/* Log buffer {{{ */

/** Logs buffer.
 *
 * This buffer captures all logger messages after it has been started
 * and it stops the capture when it is stopped.
 *
 * Buffer imbrication:
 * However if the buffer contains an other buffer it does not captures the log
 * messages emitted after the second call of \ref log_start_buffering.
 * When the second is stopped the capture restarts.
 *
 * |--->log_start_buffering();
 * |
 * |   log_msg1;
 * |
 * |   |--->log_start_buffering();
 * |   |
 * |   |  log_msg2;
 * |   |  log_msg3;
 * |   |
 * |   |--->log_stop_buffering(); -------> return qv which [msg2, msg3]
 * |
 * |   log_msg3;
 * |
 * |--->log_stop_buffering();------------> return qv which [msg1, msg4]
 *
 */

typedef struct log_buffer_t {
    log_ctx_t ctx;
    lstr_t msg;
} log_buffer_t;

qvector_t(log_buffer, log_buffer_t);

/** Start buffer for logger and filter on log level.
 *
 * \param[in] use_handler    if false, the log handler won't be called for the
 *                           logs emitted until log_stop_buffering is called.
 * \param[in] log_level      Set the maximum log level for captured logs.
 */
void log_start_buffering_filter(bool use_handler, int log_level);

/** Start buffer for logger.
 *
 * \param[in] use_handler    if false, the log handler won't be called for the
 * logs emitted until log_stop_buffering is called.
 */
void log_start_buffering(bool use_handler);

/** Stop buffer previously started for logger.
 *
 * \return the list of the logs that were emitted since the last call to
 * log_start_buffering, in the order or emission.
 */
const qv_t(log_buffer) * nullable log_stop_buffering(void);

/* }}} */
/* Log helpers {{{ */

/** Remove characters not accepted in logger names.
 *
 * This function returns LSTR_NULL_V in case the logger does not match the
 * specification (starting with alphanumeric character, not null).
 *
 * \param[in] name string that should be checked in order to remove not
 *                 accepted characters in logger name
 */
lstr_t t_logger_sanitize_name(const lstr_t name);

/* }}} */
/** \} */

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
