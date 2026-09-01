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

#include <sys/wait.h>

#include <lib-common/unix.h>

pid_t psinfo_get_tracer_pid(pid_t pid)
{
    return _psinfo_get_tracer_pid(pid);
}

void ps_panic_sighandler(int signum, siginfo_t *si, void *addr)
{
    static const struct sigaction sa = {
        .sa_flags = SA_RESTART,
        .sa_handler = SIG_DFL,
    };
    static __thread bool in_panic;

    if (!in_panic) {
        /* A crash while reporting (typically an abort() called from
         * ps_write_backtrace() itself) re-enters this handler: we must
         * die immediately, the core is what matters now. */
        in_panic = true;
        ps_write_backtrace(signum, true);
    }

    /* XXX: Restore the default operating system handling of this signal as
     * late as possible, just before raising it again: while it is restored,
     * a legitimate fault taken by another thread (such as a QPS
     * copy-on-write page fault, which relies on SIGSEGV being handled)
     * kills the process instead of being handled, and reports the wrong
     * stack in the core. */
    PROTECT_ERRNO(sigaction(signum, &sa, NULL));
    raise(signum);
}

void ps_install_panic_sighandlers(void)
{
#ifndef __has_asan
    struct sigaction sa = {
        .sa_flags = SA_RESTART | SA_SIGINFO,
        .sa_sigaction = &ps_panic_sighandler,
    };

    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
#  if defined(__linux__)
    sigaction(SIGSTKFLT, &sa, NULL);
#  endif
#endif
}
