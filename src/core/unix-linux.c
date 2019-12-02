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

/* holds the linux-dependent implementations for unix.h functions */
#ifdef __linux__

#include <linux/limits.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <dirent.h>

#include <lib-common/unix.h>
#include <lib-common/datetime.h>
#include <lib-common/sort.h>

/* {{{ psinfo_get */

static unsigned int my_jiffies_to_msecs(const unsigned long j)
{
#if HZ <= 1000 && !(1000 % HZ)
        return (1000 / HZ) * j;
#elif HZ > 1000 && !(HZ % 1000)
        return DIV_ROUND_UP(j, HZ / 1000);
#else
        return (j * 1000) / HZ;
#endif
}

/*
 * Per process flags  (taken from linux/sched.h)
 */
#define PF_ALIGNWARN	0x00000001	/* Print alignment warning msgs */
					/* Not implemented yet, only for 486*/
#define PF_STARTING	0x00000002	/* being created */
#define PF_EXITING	0x00000004	/* getting shut down */
#define PF_DEAD		0x00000008	/* Dead */
#define PF_FORKNOEXEC	0x00000040	/* forked but didn't exec */
#define PF_SUPERPRIV	0x00000100	/* used super-user privileges */
#define PF_DUMPCORE	0x00000200	/* dumped core */
#define PF_SIGNALED	0x00000400	/* killed by a signal */
#define PF_MEMALLOC	0x00000800	/* Allocating memory */
#define PF_FLUSHER	0x00001000	/* responsible for disk writeback */
#define PF_USED_MATH	0x00002000	/* if unset the fpu must be initialized before use */
#define PF_FREEZE	0x00004000	/* this task is being frozen for suspend now */
#define PF_NOFREEZE	0x00008000	/* this thread should not be frozen */
#define PF_FROZEN	0x00010000	/* frozen for system suspend */
#define PF_FSTRANS	0x00020000	/* inside a filesystem transaction */
#define PF_KSWAPD	0x00040000	/* I am kswapd */
#define PF_SWAPOFF	0x00080000	/* I am in swapoff */
#define PF_LESS_THROTTLE 0x00100000	/* Throttle me less: I clean memory */
#define PF_BORROWED_MM	0x00200000	/* I am a kthread doing use_mm */
#define PF_RANDOMIZE	0x00400000	/* randomize virtual address space */
#define PF_SWAPWRITE	0x00800000	/* Allowed to write to swap */
#define PF_SPREAD_PAGE	0x01000000	/* Spread page cache over cpuset */
#define PF_SPREAD_SLAB	0x02000000	/* Spread some slab caches over cpuset */
#define PF_MEMPOLICY	0x10000000	/* Non-default NUMA mempolicy */
#define PF_MUTEX_TESTER	0x20000000	/* Thread belongs to the rt mutex tester */

static int psinfo_read_proc(pid_t pid, sb_t *output, sb_t *buf)
{
    char path[PATH_MAX];
    const char *p;
    int pos;

    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    if (sb_read_file(buf, path) < 0) {
        return -1;
    }

    sb_addf(output, "/proc/%d/stat information:\n", pid);

    p = buf->data;
    pos = 0;
    while (p && *p != '\0') {
        int i;
        unsigned long lu;
        long l;

        switch (pos) {
          case 0: /*pid %d The process id.*/
          case 1: /*comm %s
                    The filename of the executable, in parentheses.  This is visible whether or not the executable is swapped out.*/
            break;
          case 2: /*state %c
                    One  character  from the string "RSDZTW" where R is running, S is sleeping in an interruptible wait, D is waiting in uninterruptible disk
                    sleep, Z is zombie, T is traced or stopped (on a signal), and W is paging.*/
            switch (*p) {
              case 'R':
                sb_adds(output, "State: running\n");
                break;
              case 'S':
                sb_adds(output, "State: sleeping in an interruptible wait\n");
                break;
              case 'D':
                sb_adds(output, "State: waiting in uninterruptible disk\n");
                break;
              case 'Z':
                sb_adds(output, "State: zombie\n");
                break;
              case 'T':
                sb_adds(output, "State: traced or stopped (on a signal)\n");
                break;
              case 'W':
                sb_adds(output, "State: paging\n");
                break;
              default:
                sb_addf(output, "State: unknown state: %c\n", *p);
                break;
            }
            break;
          case 3: /*ppid %d
                    The PID of the parent.*/
            i = atoi(p);
            if (i) {
                sb_addf(output, "Parent process pid: %d\n", i);
            }
            break;
          case 4: /*pgrp %d
                    The process group ID of the process.*/
            i = atoi(p);
            if (i) {
                sb_addf(output, "Process group id: %d\n", i);
            }
            break;
          case 5: /*session %d
                    The session ID of the process.*/
            i = atoi(p);
            if (i) {
                sb_addf(output, "Session id: %d\n", i);
            }
            break;
          case 6: /*tty_nr %d
                    The tty the process uses.*/
            i = atoi(p);
            if (i) {
                sb_addf(output, "tty number: %d\n", i);
            }
            break;
          case 7: /*tpgid %d
                    The process group ID of the process which currently owns the tty that the process is connected to.*/
            break;
          case 8: /*flags %lu
                    The kernel flags word of the process. For bit meanings, see the PF_* defines in <linux/sched.h>.  Details depend on the kernel version.*/
            lu = atol(p);
#define CHECK_KERNEL_FLAG(flag, txt)                                   \
            if (lu & flag) {                                           \
                sb_adds(output, "kernel flag: " #flag "(" txt ")\n");  \
            }
            CHECK_KERNEL_FLAG(PF_ALIGNWARN, "Print alignment warning msgs");
            CHECK_KERNEL_FLAG(PF_STARTING, "being created");
            CHECK_KERNEL_FLAG(PF_EXITING, "getting shut down");
            CHECK_KERNEL_FLAG(PF_DEAD, "Dead");
            CHECK_KERNEL_FLAG(PF_FORKNOEXEC, "forked but did not exec");
            CHECK_KERNEL_FLAG(PF_SUPERPRIV, "used super-user privileges");
            CHECK_KERNEL_FLAG(PF_DUMPCORE, "dumped core");
            CHECK_KERNEL_FLAG(PF_SIGNALED, "killed by a signal");
            CHECK_KERNEL_FLAG(PF_MEMALLOC, "Allocating memory");
            CHECK_KERNEL_FLAG(PF_FLUSHER, "responsible for disk writeback");
            CHECK_KERNEL_FLAG(PF_USED_MATH, "if unset the fpu must be initialized before use");
            CHECK_KERNEL_FLAG(PF_FREEZE, "this task is being frozen for suspend now");
            CHECK_KERNEL_FLAG(PF_NOFREEZE, "this thread should not be frozen");
            CHECK_KERNEL_FLAG(PF_FROZEN, "frozen for system suspend");
            CHECK_KERNEL_FLAG(PF_FSTRANS, "inside a filesystem transaction");
            CHECK_KERNEL_FLAG(PF_KSWAPD, "I am kswapd");
            CHECK_KERNEL_FLAG(PF_SWAPOFF, "I am in swapoff");
            CHECK_KERNEL_FLAG(PF_LESS_THROTTLE, "Throttle me less: I clean memory");
            CHECK_KERNEL_FLAG(PF_BORROWED_MM, "I am a kthread doing use_mm");
            CHECK_KERNEL_FLAG(PF_RANDOMIZE, "randomize virtual address space");
            CHECK_KERNEL_FLAG(PF_SWAPWRITE, "Allowed to write to swap");
            CHECK_KERNEL_FLAG(PF_SPREAD_PAGE, "Spread page cache over cpuset");
            CHECK_KERNEL_FLAG(PF_SPREAD_SLAB, "Spread some slab caches over cpuset");
            CHECK_KERNEL_FLAG(PF_MEMPOLICY, "Non-default NUMA mempolicy");
            CHECK_KERNEL_FLAG(PF_MUTEX_TESTER, "Thread belongs to the rt mutex tester");
#undef CHECK_KERNEL_FLAG
            break;

          case 9: /*minflt %lu
                    The number of minor faults the process has made which have not required loading a memory page from disk.*/
            lu = atol(p);
            if (lu) {
                sb_addf(output, "minor faults the process has made: %lu\n", lu);
            }
            break;
          case 10: /*cminflt %lu
                    The number of minor faults that the process's waited-for children have made.*/
            lu = atol(p);
            if (lu) {
                sb_addf(output, "minor faults that the process's waited-for children have made: %lu\n", lu);
            }
            break;
          case 11: /*majflt %lu
                    The number of major faults the process has made which have required loading a memory page from disk.*/
            lu = atol(p);
            if (lu) {
                sb_addf(output, "major faults the process has made: %lu\n", lu);
            }
            break;
          case 12: /*cmajflt %lu
                    The number of major faults that the process's waited-for children have made.*/
            lu = atol(p);
            if (lu) {
                sb_addf(output, "major faults that the process's waited-for children have made: %lu\n", lu);
            }
            break;
          case 13: /*utime %lu
                    The number of jiffies that this process has been scheduled in user mode.*/
            lu = atol(p);
            if (lu) {
                int ms = my_jiffies_to_msecs(lu);
                sb_addf(output, "user mode: %dms\n", ms);
            }
            break;
          case 14: /*stime %lu
                    The number of jiffies that this process has been scheduled in kernel mode.*/
            lu = atol(p);
            if (lu) {
                int ms = my_jiffies_to_msecs(lu);
                sb_addf(output, "kernel mode: %dms\n", ms);
            }
            break;
          case 15: /*cutime %ld
                    The number of jiffies that this process's waited-for children have been scheduled in user mode. (See also times(2).)*/
            l = atol(p);
            if (l) {
                int ms = my_jiffies_to_msecs(l);
                sb_addf(output, "this process's waited-for children user mode: %dms\n", ms);
            }
            break;
          case 16: /*cstime %ld
                    The number of jiffies that this process's waited-for children have been scheduled in kernel mode.*/
            l = atol(p);
            if (l) {
                int ms = my_jiffies_to_msecs(l);
                sb_addf(output, "this process's waited-for children kernel mode: %dms\n", ms);
            }
            break;
          case 17: /*priority %ld
                    The standard nice value, plus fifteen.  The value is never negative in the kernel.*/
            l = atol(p);
            sb_addf(output, "standard nice: %ld\n", l - 15);
            break;
          case 18: /*nice %ld
                    The nice value ranges from 19 (nicest) to -19 (not nice to others).*/
            l = atol(p);
            sb_addf(output, "nice: %ld (from 19 (nicest) to -19 (not nice to others))\n", l);
            break;

          default:
            /* ... */
            goto end;
        }

        p = strchr(p, ' ');
        p++; /* Skip space */
        pos++;
    }

end:
    return 0;
}

static int psinfo_read_maps(pid_t pid, sb_t *output, sb_t *buf)
{
    char path[PATH_MAX];
    const char *p;
    size_t total = 0;

    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    RETHROW(sb_read_file(buf, path));

    sb_addf(output, "\n/proc/%d/maps information:\n", pid);
    p = buf->data;

    while (p && *p) {
        const char *end, *q;
        size_t start, stop;

        end = strchr(p, '\n');
        if (!end) {
            break;
        }

        q = p;
        start = strtoll(q, &q, 16);
        if (*q != '-') {
            sb_addf(output, "Could not parse start: %*pM\n",
                    (int)(end - p), p);
            break;
        }
        q++;
        stop = strtoll(q, &q, 16);
        if (*q != ' ') {
            sb_addf(output, "Could not parse stop: %*pM\n",
                    (int)(end - p), p);
            break;
        }

        sb_addf(output, "% 12lld %*pM\n", (long long)(stop - start),
                (int)(end - p), p);
        total += stop - start;

        p = end + 1;
    }

    if (p && *p == '\0') {
        sb_addf(output, "total mapped size: %lld bytes\n", (long long)total);
    }

    return 0;
}

int psinfo_get(pid_t pid, sb_t *output)
{
    SB_8k(buf);

    if (pid <= 0) {
        pid = getpid();
    }
    /* Retrieve /proc/<pid>/stat infos */
    RETHROW(psinfo_read_proc(pid, output, &buf));

    sb_reset(&buf);

    /* Retrieve maps information */
    return psinfo_read_maps(pid, output, &buf);
}

/* }}} */
/* {{{ pid_get_starttime */

static int hertz;

static FILE *pidproc_open(pid_t pid, const char *what)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/proc/%ld/%s", (long)pid ?: (long)getpid(), what);
    return fopen(path, "r");
}

static void jiffies_to_tv(unsigned long long jiff, struct timeval *tv)
{
    if (!hertz)
        hertz = sysconf(_SC_CLK_TCK);

    tv->tv_sec  = jiff / hertz;
    tv->tv_usec = (jiff % hertz) * (1000000UL / hertz);
}

int pid_get_starttime(pid_t pid, struct timeval *tv)
{
    unsigned long long starttime;
    FILE *pidstat = pidproc_open(pid ? pid : getpid(), "stat");
    static int boot_time;
    int res;

    if (!pidstat)
        return -1;

    if (!hertz)
        hertz = sysconf(_SC_CLK_TCK);

    /* see proc(3), here we want starttime */
    res = fscanf(pidstat,
                 "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u "
                 "%*u %*d %*d %*d %*d %*d 0 %llu ", &starttime);
    p_fclose(&pidstat);

    if (res < 1) {
        errno = EINVAL;
        return -1;
    }

    if (!boot_time) {
        char buf[BUFSIZ];
        const char *p;
        FILE *f = fopen("/proc/stat", "r");

        if (!f)
            e_panic("cannot open /proc/stat");

        while (fgets(buf, sizeof(buf), f)) {
            if (strstart(buf, "btime", &p)) {
                boot_time = strtoip(p, NULL);
                break;
            }
        }
        p_fclose(&f);
        if (boot_time == 0)
            e_panic("could not parse boot time");
    }

    jiffies_to_tv(starttime, tv);
    tv->tv_sec += boot_time;

    return 0;
}

/* }}} */
/* {{{ close_fds */

void close_fds(int fd_min, qv_t(u32) * nullable to_keep)
{
    DIR *proc_fds = opendir("/proc/self/fd");
    struct dirent *entry;
    const char *p;
    int opendir_fd;

    if (!proc_fds) {
        /* Fall back to unix.c implementation */
        close_fds_unix(fd_min, to_keep);
        return;
    }

    if (to_keep) {
        dsort32(to_keep->tab, to_keep->len);
        to_keep->len = uniq32(to_keep->tab, to_keep->len);
    }

    /* XXX: opendir opens a fd. Do not close it while using it */
    opendir_fd = dirfd(proc_fds);
    while ((entry = readdir(proc_fds)) != NULL) {
        int fd = strtol(entry->d_name, &p, 10);

        if (*p || fd == opendir_fd) {
            continue;
        }
        if (fd > fd_min
        &&  (!to_keep
          || !contains32(fd, to_keep->tab, to_keep->len)))
        {
            p_close(&fd);
        }
    }
    closedir(proc_fds);
}

/* }}} */
/* {{{ eventfd */

#ifdef SYS_eventfd

int eventfd(int initialvalue, int flags)
{
    int fd = RETHROW(syscall(SYS_eventfd, initialvalue));

    fd_set_features(fd, flags);
    return fd;
}

#else

int eventfd(int initialvalue, int flags)
{
    return -1;
}

#endif

/* }}} */

#endif
