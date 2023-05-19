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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_ERRORS_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_ERRORS_H

#include <syslog.h>

typedef void (e_handler_f)(int, const char * nonnull, va_list)
    __attr_printf__(2, 0);
typedef int (error_f)(const char * nonnull, ...)
    __attr_printf__(1, 2);

#define E_PREFIX(fmt) \
    ("%s:%d:%s: " fmt), __FILE__, __LINE__, __func__

#define E_UNIXERR(funcname)  funcname ": %m"

/* These functions are meant to correspond to the syslog levels.  */
int e_fatal(const char * nonnull, ...)
    __leaf __attr_noreturn__ __cold __attr_printf__(1, 2);
int e_panic(const char * nonnull, ...)
    __leaf __attr_noreturn__ __cold __attr_printf__(1, 2);
int e_error(const char * nonnull, ...)
    __leaf __cold __attr_printf__(1, 2);
int e_warning(const char * nonnull, ...)
    __leaf __cold __attr_printf__(1, 2);
int e_notice(const char * nonnull, ...)
    __leaf __attr_printf__(1, 2);
int e_info(const char * nonnull, ...)
    __leaf __attr_printf__(1, 2);
int e_debug(const char * nonnull, ...)
    __leaf __attr_printf__(1, 2);

int e_log(int priority, const char * nonnull fmt, ...)
    __leaf __attribute__((format(printf, 2, 3)));

void e_init_stderr(void) __leaf;
void e_set_handler(e_handler_f * nonnull handler) __leaf;

/** This macro provides assertions that remain activated in production builds.
 *
 * \param[in] level The syslog level (fatal, panic, error, warning, notice,
 *                  info or debug).
 * \param[in] Cond  The condition to verify
 * \param[in] fmt   The message to log if the condition is not verified.
 */
#define __e_assert(level, Cond, StrCond, fmt, ...)  do {                     \
        if (unlikely(!(Cond))) {                                             \
            e_##level(E_PREFIX("assertion failed: \"%s\": "fmt), StrCond,    \
                      ##__VA_ARGS__);                                        \
        }                                                                    \
    } while (0)

#define e_assert(level, Cond, fmt, ...)  \
    __e_assert(level, (Cond), #Cond, fmt, ##__VA_ARGS__)

#define e_assert_n(level, Expr, fmt, ...)  \
    e_assert(level, (Expr) >= 0, fmt, ##__VA_ARGS__)

#define e_assert_neg(level, Expr, fmt, ...)  \
    e_assert(level, (Expr) < 0, fmt, ##__VA_ARGS__)

#define e_assert_p(level, Expr, fmt, ...)  \
    e_assert(level, (Expr) != NULL, fmt, ##__VA_ARGS__)

#define e_assert_null(level, Expr, fmt, ...)  \
    e_assert(level, (Expr) == NULL, fmt, ##__VA_ARGS__)

/**************************************************************************/
/* Debug part                                                             */
/**************************************************************************/

#ifdef NDEBUG

static ALWAYS_INLINE void e_ignore(int level, ...) { }
#define e_trace_ignore(level, ...) if (false) e_ignore(level, ##__VA_ARGS__)

static ALWAYS_INLINE void assert_ignore(bool cond) { }
#undef  assert
#define assert(cond)  ({ if (false) assert_ignore((bool)(cond)); (void)0; })

#  define e_trace(level, ...)              e_trace_ignore(level, ##__VA_ARGS__)
#  define e_trace_hex(level, ...)          e_trace_ignore(level, ##__VA_ARGS__)
#  define e_trace_start(level, ...)        e_trace_ignore(level, ##__VA_ARGS__)
#  define e_trace_cont(level, ...)         e_trace_ignore(level, ##__VA_ARGS__)
#  define e_trace_end(level, ...)          e_trace_ignore(level, ##__VA_ARGS__)

#  define e_named_trace(level, ...)        e_trace_ignore(level, ##__VA_ARGS__)
#  define e_named_trace_hex(level, ...)    e_trace_ignore(level, ##__VA_ARGS__)
#  define e_named_trace_start(level, ...)  e_trace_ignore(level, ##__VA_ARGS__)
#  define e_named_trace_cont(level, ...)   e_trace_ignore(level, ##__VA_ARGS__)
#  define e_named_trace_end(level, ...)    e_trace_ignore(level, ##__VA_ARGS__)

#  define e_set_verbosity(...)      (void)0
#  define e_incr_verbosity(...)     (void)0
#  define e_is_traced(...)          false
#  define e_name_is_traced(...)     false

#else

void e_set_verbosity(int max_debug_level) __leaf;
void e_incr_verbosity(void) __leaf;

int  e_is_traced_(int level, const char * nonnull fname,
                  const char * nonnull func, const char * nullable name)
    __leaf;

#define e_name_is_traced(lvl, name) \
    ({                                                                       \
       int8_t e_res;                                                         \
                                                                             \
       if (__builtin_constant_p(lvl) && __builtin_constant_p(name)) {        \
           static int8_t e_traced;                                           \
                                                                             \
           if (unlikely(e_traced == 0)) {                                    \
               e_traced = e_is_traced_(lvl, __FILE__, __func__, name);       \
           }                                                                 \
           e_res = e_traced;                                                 \
       } else {                                                              \
           e_res = e_is_traced_(lvl, __FILE__, __func__, name);              \
       }                                                                     \
       e_res > 0;                                                            \
    })
#define e_is_traced(lvl)  e_name_is_traced(lvl, NULL)

void e_trace_put_(int lvl, const char * nonnull fname, int lno,
                  const char * nonnull func, const char * nullable name,
                  const char * nonnull fmt, ...)
                  __leaf __attr_printf__(6, 7) __cold;

#define e_named_trace_start(lvl, name, fmt, ...) \
    do {                                                                     \
        if (e_name_is_traced(lvl, name))                                     \
            e_trace_put_(lvl, __FILE__, __LINE__, __func__,                  \
                         name, fmt, ##__VA_ARGS__);                          \
    } while (0)
#define e_named_trace_cont(lvl, name, fmt, ...) \
    e_named_trace_start(lvl, name, fmt, ##__VA_ARGS__)
#define e_named_trace_end(lvl, name, fmt, ...) \
    e_named_trace_start(lvl, name, fmt "\n", ##__VA_ARGS__)
#define e_named_trace(lvl, name, fmt, ...) \
    e_named_trace_start(lvl, name, fmt "\n", ##__VA_ARGS__)
#define e_named_trace_hex(lvl, name, str, buf, len)                          \
    do {                                                                     \
        if (e_name_is_traced(lvl, name)) {                                   \
            e_trace_put_(lvl, __FILE__, __LINE__, __func__,                  \
                         name, "--%s (%d)--\n", str, len);                   \
            ifputs_hex(stderr, buf, len);                                    \
        }                                                                    \
    } while (0)

#define e_trace_start(lvl, fmt, ...)  e_named_trace_start(lvl, NULL, fmt, ##__VA_ARGS__)
#define e_trace_cont(lvl, fmt, ...)   e_named_trace_cont(lvl, NULL, fmt, ##__VA_ARGS__)
#define e_trace_end(lvl, fmt, ...)    e_named_trace_end(lvl, NULL, fmt, ##__VA_ARGS__)
#define e_trace(lvl, fmt, ...)        e_named_trace(lvl, NULL, fmt, ##__VA_ARGS__)
#define e_trace_hex(lvl, str, buf, len) \
    e_named_trace_hex(lvl, NULL, str, buf, len)

#endif

void ps_dump_backtrace(int signum, const char * nonnull prog, int fd,
                       bool full);
void ps_write_backtrace(int signum, bool allow_fork);

static ALWAYS_INLINE __must_check__
bool e_expect(bool cond, const char * nonnull expr, const char * nonnull file,
              int line, const char * nonnull func)
{
    if (unlikely(!cond)) {
#ifdef NDEBUG
        ps_write_backtrace(-1, false);
        e_error("assertion (%s) failure: %s:%d:%s", expr, file, line, func);
        return false;
#else
        __assert_fail(expr, file, line, func);
#endif
    }
    return true;
}

#undef  expect
#define expect(Cond)  \
    e_expect((Cond), TOSTR(Cond), __FILE__, __LINE__, __func__)

/* {{{ debug_stack_* */

/** Function executed when writing the .debug file.
 *
 * This callback is called when a crash happens in the debug stack scope where
 * the callback has been registered.
 *
 * \warning It should write the user data using \p dprintf. Please notice that
 *          our custom formats (%*pM, %pL, %*pS, ...) wont work in this
 *          context.
 *
 * \warning This code is called in the context of a crash, some contextual
 *          data may be invalid or corrupted.
 *
 * \warning Do not use malloc or functions that could use malloc in the
 *          callback. It could result in deadlocks.
 */
typedef void (debug_stack_cb_f)(int fd, data_t data);

/** Register some data in the debug stack.
 *
 * \param[in] _data  User data that should be dumped into .debug file in case
 *                   of crash during the scope execution.
 *
 * \param[in] _cb  Function that writes the data into .debug file in case of
 *                 crash. See \p debug_stack_cb_f.
 */
#define debug_stack_scope(_data, _cb)                                        \
    data_t PFX_LINE(debug_stack_)                                            \
    __attribute__((unused, cleanup(debug_stack_pop))) =                      \
    debug_stack_push(__func__, __FILE__, __LINE__, (_data), (_cb))

/* Private functions. */
data_t debug_stack_push(const char *nonnull func,
                        const char *nonnull file, int line,
                        data_t data, debug_stack_cb_f *nonnull cb);
void debug_stack_pop(data_t *nonnull priv);

/** Append user debug info into .debug file. */
int _debug_stack_print(const char *nonnull path);

/** Write the content of the debug stack to a given file descriptor. */
void debug_stack_dprint(int fd);

/* }}} */

#endif /* IS_LIB_COMMON_CORE_ERRORS_H */
