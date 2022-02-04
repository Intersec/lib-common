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

#ifndef IS_LIB_COMMON_EL_H
#define IS_LIB_COMMON_EL_H

/** \defgroup lc_el Intersec Event Loop
 *
 * \brief This module provides a very efficient, OS-independant event loop.
 *
 * \{
 */

/** \file lib-inet/el.h
 * \brief Event Loop module header.
 */

#include <lib-common/core.h>
#ifdef HAVE_SYS_POLL_H
#include <poll.h>
#else
# define POLLIN      0x0001    /* There is data to read        */
# define POLLPRI     0x0002    /* There is urgent data to read */
# define POLLOUT     0x0004    /* Writing now will not block   */
# define POLLERR     0x0008    /* Error condition              */
# define POLLHUP     0x0010    /* Hung up                      */
# define POLLNVAL    0x0020    /* Invalid request: fd not open */
#endif
#ifndef POLLRDHUP
# ifdef OS_LINUX
#  define POLLRDHUP 0x2000
# else
#  define POLLRDHUP 0
# endif
#endif
#define POLLINOUT  (POLLIN | POLLOUT)

#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#else
#define IN_ACCESS         0x00001
#define IN_ATTRIB         0x00002
#define IN_CLOSE_WRITE    0x00004
#define IN_CLOSE_NOWRITE  0x00008
#define IN_CREATE         0x00010
#define IN_DELETE         0x00020
#define IN_DELETE_SELF    0x00040
#define IN_MODIFY         0x00080
#define IN_MOVE_SELF      0x00100
#define IN_MOVED_FROM     0x00200
#define IN_MOVED_TO       0x00400
#define IN_OPEN           0x00800

#define IN_IGNORED        0x01000
#define IN_ISDIR          0x02000
#define IN_Q_OVERFLOW     0x04000
#define IN_UNMOUNT        0x08000
#define IN_ONLYDIR        0x10000

#define IN_MOVE   (IN_MOVED_FROM | IN_MOVED_TO)
#define IN_CLOSE  (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#endif

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

typedef struct ev_t *el_t;

/* XXX el_data_t is deprecated and will be removed in a future version
 * of the lib-common.
 */
typedef data_t el_data_t;

typedef void (el_cb_f)(el_t nonnull, data_t);
typedef void (el_signal_f)(el_t nonnull, int, data_t);
typedef void (el_child_f)(el_t nonnull, pid_t, int, data_t);
typedef int  (el_fd_f)(el_t nonnull, int, short, data_t);
typedef void (el_proxy_f)(el_t nonnull, short, data_t);
typedef void (el_fs_watch_f)(el_t nonnull, uint32_t mask, uint32_t cookie,
                             lstr_t name, data_t);
typedef void (el_worker_f)(int timeout);

#ifdef __has_blocks
typedef void (BLOCK_CARET el_cb_b)(el_t nonnull);
typedef void (BLOCK_CARET el_signal_b)(el_t nonnull, int);
typedef void (BLOCK_CARET el_child_b)(el_t nonnull, pid_t, int status);
typedef int  (BLOCK_CARET el_fd_b)(el_t nonnull, int, short);
typedef void (BLOCK_CARET el_proxy_b)(el_t nonnull, short);
typedef void (BLOCK_CARET el_fs_watch_b)(el_t nonnull, uint32_t, uint32_t,
                                         lstr_t);
#endif

el_t nonnull el_blocker_register(void) __leaf;
el_t nonnull el_before_register_d(el_cb_f * nonnull, data_t)
    __leaf;
el_t nonnull el_idle_register_d(el_cb_f * nonnull, data_t)
    __leaf;
el_t nonnull el_signal_register_d(int signo, el_signal_f * nonnull, data_t)
    __leaf;
el_t nonnull el_child_register_d(pid_t pid, el_child_f * nonnull, data_t)
    __leaf;

#ifdef __has_blocks
/* The block based API takes a block version of the callback and a second
 * optional block called when the el_t is unregistered. The purpose of this
 * second block is to wipe() the environment of the callback.
 *
 * You cannot change the blocks attached to an el_t after registration (that
 * is, el_set_priv and el_*_set_hook cannot be used on el_t initialized with
 * blocks.
 */

el_t nonnull el_before_register_blk(el_cb_b nonnull, block_t nullable wipe)
    __leaf;
el_t nonnull el_idle_register_blk(el_cb_b nonnull, block_t nullable wipe)
    __leaf;
el_t nonnull el_signal_register_blk(int signo, el_signal_b nonnull,
                                    block_t nullable) __leaf;
el_t nonnull el_child_register_blk(pid_t pid, el_child_b nonnull,
                                   block_t nullable) __leaf;

/** Run a command in the background.
 *
 * \param[in]  file    the command to run.
 * \param[in]  argv    the argument list available to the executed program,
 *                     without the name of the program itself as the first
 *                     argument.
 * \param[in]  envp    the environment for the new process (optional).
 * \param[in]  child   optional callback to run in the child before exec.
 * \param[in]  blk     the callback to run in the parent when the child exits.
 * \param[in]  wipe    optional block to wipe the environment of the callback.
 *
 * \return the pid
 */
pid_t el_spawn_child(const char * nonnull file, const char * nullable argv[],
                     const char * nullable envp[], block_t nullable child,
                     el_child_b nonnull blk, block_t nullable wipe);

typedef void (BLOCK_CARET el_child_output_b)(el_t nonnull, pid_t, int status,
                                             lstr_t output);

/** Run a command in the background, capturing its output.
 *
 * \param[in]  file    the command to run.
 * \param[in]  argv    the argument list available to the executed program,
 *                     without the name of the program itself as the first
 *                     argument.
 * \param[in]  envp    the environment for the new process (optional).
 * \param[in]  timeout if positive, maximum execution time of the command
 *                     before it gets killed.
 * \param[in]  child   optional callback to run in the child before exec.
 * \param[in]  blk     the callback to run in the parent when the child exits
 *                     it takes the child stdout/stderr capture as argument.
 * \param[in]  wipe    optional block to wipe the environment of the callback.
 *
 * \return the pid
 */
pid_t el_spawn_child_capture(const char * nonnull file,
                             const char * nullable argv[],
                             const char * nullable envp[],
                             int timeout,
                             block_t nullable child,
                             el_child_output_b nonnull blk,
                             block_t nullable wipe);
#endif

static inline el_t nonnull
el_before_register(el_cb_f * nonnull f, void * nullable ptr)
{
    return el_before_register_d(f, (data_t){ ptr });
}
static inline el_t nonnull
el_idle_register(el_cb_f * nonnull f, void * nullable ptr)
{
    return el_idle_register_d(f, (data_t){ ptr });
}
static inline el_t nonnull
el_signal_register(int signo, el_signal_f * nonnull f, void * nullable ptr)
{
    return el_signal_register_d(signo, f, (data_t){ ptr });
}
static inline el_t nonnull
el_child_register(pid_t pid, el_child_f * nonnull f, void * nullable ptr)
{
    return el_child_register_d(pid, f, (data_t){ ptr });
}

void el_before_set_hook(el_t nonnull, el_cb_f * nonnull) __leaf;
void el_idle_set_hook(el_t nonnull, el_cb_f * nonnull) __leaf;
void el_signal_set_hook(el_t nonnull, el_signal_f * nonnull) __leaf;
void el_child_set_hook(el_t nonnull, el_child_f * nonnull) __leaf;

/** Unregister an event whatever its type. */
data_t el_unregister(el_t nullable * nonnull);

/*----- idle related -----*/
void el_idle_unpark(el_t nonnull) __leaf;

/*----- child related -----*/
pid_t el_child_getpid(el_t nonnull) __leaf __attribute__((pure));
int   el_child_get_status(el_t nonnull) __leaf;
el_t nullable el_child_get_el(pid_t pid);

/*----- proxy related -----*/
el_t nonnull el_proxy_register_d(el_proxy_f * nonnull, data_t) __leaf;
#ifdef __has_blocks
el_t nonnull el_proxy_register_blk(el_proxy_b nonnull, block_t nullable) __leaf;
#endif
static inline el_t nonnull
el_proxy_register(el_proxy_f * nonnull f, void * nullable ptr)
{
    return el_proxy_register_d(f, (data_t){ ptr });
}
void el_proxy_set_hook(el_t nonnull, el_proxy_f * nonnull) __leaf;
short el_proxy_set_event(el_t nonnull, short mask) __leaf;
short el_proxy_clr_event(el_t nonnull, short mask) __leaf;
short el_proxy_set_mask(el_t nonnull, short mask) __leaf;

/*----- fd related -----*/
extern struct rlimit fd_limit_g;

typedef enum ev_priority_t {
    EV_PRIORITY_LOW    = 0,
    EV_PRIORITY_NORMAL = 1,
    EV_PRIORITY_HIGH   = 2
} ev_priority_t;

el_t nonnull el_fd_register_d(int fd, bool own_fd, short events,
                              el_fd_f * nonnull, data_t) __leaf;
#ifdef __has_blocks
el_t nonnull el_fd_register_blk(int fd, bool own_fd, short events,
                                el_fd_b nonnull, block_t nullable)
    __leaf;
#endif
static inline el_t nonnull
el_fd_register(int fd, bool own_fd, short events, el_fd_f * nonnull f,
               void * nullable ptr)
{
    return el_fd_register_d(fd, own_fd, events, f, (data_t){ ptr });
}
void el_fd_set_hook(el_t nonnull, el_fd_f * nonnull) __leaf;

typedef enum ev_fd_loop_flags_t {
    EV_FDLOOP_HANDLE_SIGNALS = 1 << 0,
    EV_FDLOOP_HANDLE_TIMERS  = 1 << 1,
} ev_fd_loop_flags_t;

int   el_fd_loop(el_t nonnull, int timeout, ev_fd_loop_flags_t flags);
int   el_fds_loop(el_t nonnull * nonnull els, int el_count, int timeout,
                  ev_fd_loop_flags_t flags);

short el_fd_get_mask(el_t nonnull) __leaf __attribute__((pure));
short el_fd_set_mask(el_t nonnull, short events) __leaf;
int   el_fd_get_fd(el_t nonnull) __leaf __attribute__((pure));
void  el_fd_mark_fired(el_t nonnull) __leaf;

ev_priority_t el_fd_set_priority(el_t nonnull, ev_priority_t priority);
#define el_fd_set_priority(el, prio)  \
    (el_fd_set_priority)((el), EV_PRIORITY_##prio)

/*
 * \param[in]  ev       a file descriptor el_t.
 * \param[in]  mask     the POLL* mask of events that resets activity to 0
 * \param[in]  timeout
 *    what to do with the activity watch timer:
 *    - 0 means unregister the activity timer ;
 *    - >0 means register (or reset) the activity timer with this timeout in
 *      miliseconds;
 *    - < 0 means reset the activity timer using the timeout it was registered
 *      with. In particular if no activity timer is set up for this given file
 *      descriptor el_t, then this is a no-op.
 */
#define EL_EVENTS_NOACT  ((short)-1)
int   el_fd_watch_activity(el_t nonnull, short mask, int timeout) __leaf;


/**
 * \defgroup el_wake Waking up event loop from another thread.
 * \{
 */

/** Register a waker.
 *
 * A waker is an event loop object that get manually triggered. One can call
 * el_wake_fire() from another thread in order to wake up an event loop
 * waiting on the waker.
 *
 * This is a low level primitive provided in order to build higher-level
 * infrastructure. Look at \ref thr_queue and \ref thr_queue_main_g before
 * using a waker by hand.
 */
el_t nullable el_wake_register_d(el_cb_f * nonnull, data_t)
    __leaf;
#ifdef __has_blocks
el_t nullable el_wake_register_blk(el_cb_b nonnull, block_t nullable);
#endif
static inline el_t nullable
el_wake_register(el_cb_f * nonnull cb, void * nullable ptr)
{
    return el_wake_register_d(cb, (data_t){ ptr });
}

void el_wake_fire(el_t nonnull);

/** \} */

/**
 * \defgroup el_fs_watch FS activity notifications
 * \{
 */

/** Register a new watch for a list of events on a given path.
 *
 *  \warning you must not add more that one watch for a given path.
 */
el_t nullable el_fs_watch_register_d(const char * nonnull, uint32_t,
                                     el_fs_watch_f * nonnull, data_t)
    __leaf;
#ifdef __has_blocks
el_t nullable el_fs_watch_register_blk(const char * nonnull, uint32_t,
                                       el_fs_watch_b nonnull,
                                       block_t nullable);
#endif
static inline el_t nullable
el_fs_watch_register(const char * nonnull path, uint32_t flags,
                     el_fs_watch_f * nonnull f, void * nullable ptr)
{
    return el_fs_watch_register_d(path, flags, f, (data_t){ ptr });
}

int el_fs_watch_change(el_t nonnull el, uint32_t flags);

const char * nonnull el_fs_watch_get_path(el_t nonnull el);

/** \} */

/**
 * \defgroup el_timers Event Loop timers
 * \{
 */

typedef enum ev_timer_flags_t {
    EL_TIMER_NOMISS = (1 << 0),
    EL_TIMER_LOWRES = (1 << 1),
} ev_timer_flags_t;


/** \brief registers a timer
 *
 * There are two kinds of timers: one shot and repeating timers:
 * - One shot timers fire their callback once, when they time out.
 * - Repeating timers automatically rearm after being fired.
 *
 * One shot timers are automatically destroyed at the end of the callback if
 * they have not be rearmed in it. As a consequence, you must be careful to
 * cleanup all references to one-shot timers you don't rearm within the
 * callback.
 *
 * \param[in]  next    relative time in ms at which the timers fires.
 * \param[in]  repeat  repeat interval in ms, 0 means single shot.
 * \param[in]  flags   timer related flags (nomiss, lowres, ...)
 * \param[in]  cb      callback to call upon timer expiry.
 * \param[in]  priv    private data.
 * \return the timer handler descriptor.
 */
el_t nonnull el_timer_register_d(int64_t next, int64_t repeat,
                                 ev_timer_flags_t flags,
                                 el_cb_f * nonnull, data_t)
    __leaf;
#ifdef __has_blocks
el_t nonnull el_timer_register_blk(int64_t next, int64_t repeat,
                                   ev_timer_flags_t flags,
                                   el_cb_b nonnull, block_t nullable)
    __leaf;
#endif
static inline el_t nonnull
el_timer_register(int64_t next, int64_t repeat, ev_timer_flags_t flags,
                  el_cb_f * nonnull f, void * nullable ptr)
{
    return el_timer_register_d(next, repeat, flags, f, (data_t){ ptr });
}
bool el_timer_is_repeated(el_t nonnull ev) __leaf __attribute__((pure));

/** \brief restart a single shot timer.
 *
 * Note that if the timer hasn't expired yet, it just sets it to a later time.
 *
 * \param[in]  el      timer handler descriptor.
 * \param[in]  next    relative time in ms at wich the timers will fire. If
 *                     it's negative, the previous relative value is reused.
 */
void el_timer_restart(el_t nonnull, int64_t next) __leaf;
void el_timer_set_hook(el_t nonnull, el_cb_f * nonnull) __leaf;

/**\}*/

/** Un-reference an event.
 *
 * An unref'ed event does not block the event loop.
 *
 * \warning this is forbidden for FS_WATCH events.
 */
el_t nonnull el_unref(el_t nonnull) __leaf;

/** Reference an event. */
el_t nonnull el_ref(el_t nonnull) __leaf;

#ifndef NDEBUG
bool el_set_trace(el_t nonnull, bool trace) __leaf;
#else
#define el_set_trace(ev, trace)
#endif
data_t el_set_priv(el_t nonnull, data_t) __leaf;

void el_bl_use(void) __leaf;
void el_bl_lock(void);
void el_bl_unlock(void);

/** Define the worker function.
 *
 * The worker is a unique function that get called whenever the el_loop would
 * get block waiting for activity. The worker can handle any workload it
 * wants, but it must ensure that:
 *  - it returns after the given timeout.
 *  - it returns when the event loop has new activity (pollable using
 *    \ref el_has_pending_events.
 *
 * \param[in] worker The new worker (NULL to unset the current worker)
 * \return The previous worker (NULL if there were no worker)
 */
el_worker_f * nullable el_set_worker(el_worker_f * nullable worker) __leaf;

/** Get the current worker function. */
el_worker_f * nullable el_get_worker(void) __leaf;

void el_cond_wait(pthread_cond_t * nonnull);
void el_cond_signal(pthread_cond_t * nonnull) __leaf;

void el_loop(void);
void el_unloop(void) __leaf;
void el_loop_timeout(int msecs);
bool el_has_pending_events(void);

/** Have we received a termination signal? */
bool el_is_terminating(void);

/**\}*/
/* Module {{{ */

MODULE_DECLARE(el);

/** Print state method.
 *
 * This method is called when receiving a SIGPWR signal, so that any module
 * can print relevant information about its internal state (using loggers).
 */
MODULE_METHOD_DECLARE(VOID, DEPS_BEFORE, print_state);

/* }}} */

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

/* {{{ Private (exposed for tests) */

data_t el_fd_unregister(struct ev_t **evp);
data_t el_fs_watch_unregister(struct ev_t **evp);

/* }}} */

#endif
