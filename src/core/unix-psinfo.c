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

#include <sys/wait.h>

#include <lib-common/thr.h>
#include <lib-common/unix.h>

pid_t psinfo_get_tracer_pid(pid_t pid)
{
    return _psinfo_get_tracer_pid(pid);
}

void ps_panic_sighandler(int signum, siginfo_t *si, void *addr)
{
    static const struct sigaction sa = {
        .sa_flags   = SA_RESTART,
        .sa_handler = SIG_DFL,
    };

    PROTECT_ERRNO(sigaction(signum, &sa, NULL));
    ps_write_backtrace(signum, true);
    raise(signum);
}

void ps_install_panic_sighandlers(void)
{
#ifndef __has_asan
    struct sigaction sa = {
        .sa_flags     = SA_RESTART | SA_SIGINFO,
        .sa_sigaction = &ps_panic_sighandler,
    };

    sigaction(SIGABRT,   &sa, NULL);
    sigaction(SIGILL,    &sa, NULL);
    sigaction(SIGFPE,    &sa, NULL);
    sigaction(SIGSEGV,   &sa, NULL);
    sigaction(SIGBUS,    &sa, NULL);
#if defined(__linux__)
    sigaction(SIGSTKFLT, &sa, NULL);
#endif
#endif
}
