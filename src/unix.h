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

#ifndef IS_LIB_COMMON_UNIX_H
#define IS_LIB_COMMON_UNIX_H

#include <lib-common/container-qvector.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#ifndef O_CLOEXEC
# ifdef OS_LINUX
#  define O_CLOEXEC	02000000	/* set close_on_exec */
# else
#  error "set O_CLOEXEC for your platform here"
# endif
#endif

#define O_ISWRITE(m)      (((m) & (O_RDONLY|O_WRONLY|O_RDWR)) != O_RDONLY)

#define PROTECT_ERRNO(expr) \
    ({ int save_errno__ = errno; expr; errno = save_errno__; })

#define ERR_RW_RETRIABLE(err) \
    ((err) == EINTR || (err) == EAGAIN)
#define ERR_CONNECT_RETRIABLE(err) \
    ((err) == EINTR || (err) == EINPROGRESS)
#define ERR_ACCEPT_RETRIABLE(err) \
    ((err) == EINTR || (err) == EAGAIN || (err) == ECONNABORTED)

/* sys_siglist has been deprecated and strsignal() should be used instead. */
#define sys_siglist  DONT_USE_sys_siglist_USE_strsignal

/* {{{ Process related */

int  pid_get_starttime(pid_t pid, struct timeval * nonnull tv);
void ps_panic_sighandler(int signum, siginfo_t * nullable si,
                         void * nullable addr);
void ps_install_panic_sighandlers(void);

/* }}} */
/* {{{ Filesystem related */

/* XXX man 2 open
 * NOTES: minimum alignment boundaries on linux 2.6 for direct I/O is 512
 * bytes but a lot of devices are aligned on 4K, so we use 4K as our default
 * alignment.
 */
#define DIRECT_BITS            12
#define DIRECT_ALIGN           (1 << DIRECT_BITS)
#define DIRECT_REMAIN(Val)     ((Val) & BITMASK_LT(typeof(Val), DIRECT_BITS))
#define DIRECT_TRUNCATE(Val)   ((Val) & BITMASK_GE(typeof(Val), DIRECT_BITS))
#define DIRECT_IS_ALIGNED(Val) (DIRECT_REMAIN(Val) == 0)

int mkdir_p(const char * nonnull dir, mode_t mode);
int mkdirat_p(int dfd, const char * nonnull dir, mode_t mode);
int rmdir_r(const char * nonnull dir, bool only_content);
int rmdirat_r(int dfd, const char * nonnull dir, bool only_content);

int get_mtime(const char * nonnull filename, time_t * nonnull t);

off_t filecopy(const char * nonnull pathin, const char * nonnull pathout);
off_t filecopyat(int dfd_src, const char * nonnull name_src,
                 int dfd_dst, const char * nonnull name_dst);

int p_lockf(int fd, int mode, int cmd, off_t start, off_t len);
int p_unlockf(int fd, off_t start, off_t len);

/** Directory lock */
typedef struct dir_lock_t {
    int dfd;    /**< Directory file descriptor */
    int lockfd; /**< Lock file descriptor      */
} dir_lock_t;

/** Directory lock initializer
 *
 * Since 0 is a valid file descriptor, dir_lock_t fds must be initialiazed to
 * -1.
 */
#define DIR_LOCK_INIT    { .dfd = -1, .lockfd = -1 }
#define DIR_LOCK_INIT_V  (dir_lock_t)DIR_LOCK_INIT

/** Lock a directory
 * \param  dfd    directory file descriptor
 * \param  dlock  directory lock
 * \retval  0 on success
 * \retval -1 on error
 * \see    unlockdir
 *
 * Try to create a .lock file (with u+rw,g+r,o+r permissions) into the given
 * directory and lock it.
 *
 * An error is returned if the directory is already locked (.lock exists and
 * is locked) or if the directory cannot be written. In this case, errno is
 * set appropriately.
 *
 * After a sucessful call to lockdir(), the directory file descriptor (dfd) is
 * duplicated for internal usage. The file descriptors of the dir_lock_t
 * should not be used by the application.
 *
 * Use unlockdir() to unlock a directory locked with unlock().
 */
int lockdir(int dfd, dir_lock_t * nonnull dlock);

/** Unlock a directory
 * \param  dlock  directory lock
 * \see    lockdir
 *
 * Unlock the .lock file and delete it. unlockdir() be called on a file
 * descriptor returned by lockdir().
 *
 * To be safe, this function resets the file descriptors to -1.
 */
void unlockdir(dir_lock_t * nonnull dlock);

int tmpfd(void);
void devnull_dup(int fd);

#ifndef __cplusplus

/** Locate a command.
 *
 * Search the directories of environment variable "PATH" for an executable
 * file matching the given argument. The first matching file is returned.
 *
 * \param[in]  cmd      The name of the executable we are looking for.
 * \param[out] cmd_path The output buffer where the absolute path to the
 *                      executable file will be written.
 * \return -1 in case of error (including executable name not found) and 0
 *  otherwise.
 */
int which(const char *nonnull cmd, char cmd_path[static PATH_MAX]);

#endif

/* }}} */
/* {{{ File listing related */

#if defined(__linux__)

typedef struct linux_dirent_t {
    long           d_ino;
    off_t          d_off;
    unsigned short d_reclen;
    char           d_name[];
} linux_dirent_t;

/* XXX: D_TYPE works only on ext2, ext3, ext4, btrfs (man getdents)
 *      using D_TYPE() inside of a list_dir block call back is safe,
 *      list_dir will set the type for you.
 */
#define D_TYPE(ld)            *((byte *)ld + ld->d_reclen - 1)
#define D_SET_TYPE(ld, type)  *((byte *)ld + ld->d_reclen - 1) = type

#else
typedef struct dirent linux_dirent_t;

#define D_TYPE(ld)  ((ld)->d_type)
#define D_SET_TYPE(ld, type) ((ld)->d_type = type)

#endif

#ifdef __has_blocks
typedef int (BLOCK_CARET on_file_b)(const char * nonnull dir,
                                    const linux_dirent_t * nonnull de);

enum list_dir_flags_t {
    /** List subdirectories recursively. */
    LIST_DIR_RECUR = 1 << 0,

    /** Follow symbolic links when inspecting files/directories. */
    LIST_DIR_FOLLOW_SYMLINK = 1 << 1,
};

/** List all the files of a directory and apply the specified treatment
 * on them.
 *
 * This function is designed to limit system calls even on directories with a
 * very large amount of files. The performance will mostly rely on the
 * treatment function given.
 *
 * @param dir Path to the directory to read.
 * @param flags 0 or a combination of \ref list_dir_flags_t flags.
 * @param on_file The function called on each file found.
 *
 * @return The number of files found in the directory (and its sub-directories
 * when recur = true).
 * On error, the function returns -1.
 * The function returns the result of the processing function if it fails.
 *
 */
int list_dir(const char * nonnull path, unsigned flags,
             on_file_b nullable on_file);
#endif

/* }}} */
/* {{{ File descriptor related */

/* `data' is cast to `void *' to remove the const: iovec structs are used
 * for input and output, thus `iov_base' cannot be `const void *' even
 * for write operations.
 */
#define MAKE_IOVEC(data, len)  \
     (struct iovec){ .iov_base = (void *)(data), .iov_len = (len) }
#define MAKE_IOVEC_TAB(Tab) \
     MAKE_IOVEC((Tab)->tab, sizeof((Tab)->tab[0]) * (Tab)->len)

qvector_t(iovec, struct iovec);

static inline size_t iovec_len(const struct iovec * nonnull iov, int iovlen)
{
    size_t res = 0;
    for (int i = 0; i < iovlen; i++) {
        res += iov[i].iov_len;
    }
    return res;
}

int iovec_vector_kill_first(qv_t(iovec) * nonnull iovs, ssize_t len);

__must_check__ ssize_t
xwrite_file_extended(const char * nonnull path, const void * nonnull data,
                     ssize_t dlen, int flags, mode_t mode);

static inline __must_check__ ssize_t
xwrite_file(const char * nonnull path, const void * nonnull data,
            ssize_t dlen)
{
    return xwrite_file_extended(path, data, dlen,
                                O_WRONLY | O_CREAT | O_TRUNC, 0644);
}

static inline __must_check__ ssize_t
xappend_to_file(const char * nonnull path, const void * nonnull data,
                ssize_t dlen)
{
    return xwrite_file_extended(path, data, dlen,
                                O_WRONLY | O_CREAT | O_APPEND, 0644);
}

__must_check__ ssize_t xwrite(int fd, const void * nonnull data,
                              ssize_t dlen);
__must_check__ ssize_t xwritev(int fd, struct iovec * nonnull iov,
                               int iovcnt);
__must_check__ ssize_t xpwrite(int fd, const void * nonnull data,
                               ssize_t dlen, off_t offset);
__must_check__ int xftruncate(int fd, off_t offs);
__must_check__ int xread(int fd, void * nonnull data, ssize_t dlen);
__must_check__ int xpread(int fd, void * nonnull data, ssize_t dlen,
                          off_t offset);

bool is_fd_open(int fd);

/** Close all open file descriptors strictly higher than \p fd_min and
 *  not in \p to_keep.
 *
 * Set \p fd_min to a negative value to have to lower limit.
 * If provided, \p to_keep will be sorted and uniq'ed.
 */
void close_fds(int fd_min, qv_t(u32) * nullable to_keep);

/** Unix (non-linux) implementation of close_fds. */
void close_fds_unix(int fd_min, qv_t(u32) * nullable to_keep);

/** Close all open file descriptors strictly higher than \p fd_min. */
static inline void close_fds_higher_than(int fd_min)
{
    return close_fds(fd_min, NULL);
}

bool is_fancy_fd(int fd);
void term_get_size(int * nonnull cols, int * nonnull rows);

typedef enum {
    FD_FEAT_TCP_NODELAY = 1 << 0,

    FD_FEAT_NONBLOCK = O_NONBLOCK,
    FD_FEAT_DIRECT   = O_DIRECT,
    FD_FEAT_CLOEXEC  = O_CLOEXEC,
} fd_features_flags_t;
int fd_set_features(int fd, int flags);
int fd_unset_features(int fd, int flags);

/** Build an eventfd
 */
int eventfd(int initialvalue, int flags);

/** Get the path of the file opened by that file descriptor.
 *
 * In case of success this function guarantees the buffer is terminated by a
 * nul byte. It only works if the fd points to a regular file or a directory
 * that hasn't been moved or renamed since it was opened.
 *
 * @param fd  The filedescriptor to inspect.
 * @param buf A buffer that will receive the path.
 * @param buf_len The size of buf.
 *
 * @return A negative value if the filename cannot be retrieved or if the
 *         buffer is too short. Or the size of the string stored in the buffer
 *         in case of success (not counting the trailing nul byte).
 *
 * @warning this function is expensive. It performs up to 3 system calls.
 */
ssize_t fd_get_path(int fd, char buf[], size_t buf_len);

__attr_nonnull__((1))
static inline int p_fclose(FILE * nullable * nonnull fpp) {
    FILE *fp = *fpp;

    *fpp = NULL;
    return fp ? fclose(fp) : 0;
}

__attr_nonnull__((1))
static inline int p_closedir(DIR * nullable * nonnull dirp) {
    DIR *dir = *dirp;

    *dirp = NULL;
    return dir ? closedir(dir) : 0;
}

__attr_nonnull__((1))
static inline int p_close(int * nonnull hdp) {
    int hd = *hdp;
    *hdp = -1;
    if (hd < 0)
        return 0;
    while (close(hd) < 0) {
        if (!ERR_RW_RETRIABLE(errno))
            return -1;
    }
    return 0;
}

/* }}} */
/* {{{ Misc */

static inline void getopt_init(void) {
    /* XXX this is not portable, BSD want it to be set to -1 *g* */
    optind = 0;
}

/* if pid <= 0, retrieve infos for the current process */
int psinfo_get(pid_t pid, sb_t * nonnull output);

/** \brief Get PID of a traced process
 *
 * \param pid  PID of the process (0 = current process)
 *
 * \return  the PID of the tracer
 * \return  0 if the process is not traced
 * \return -1 on error
 */
pid_t psinfo_get_tracer_pid(pid_t pid);

/* XXX: This function MUST be inlined in check_strace() to avoid appearing
 * in the stack.
 */
static ALWAYS_INLINE int _psinfo_get_tracer_pid(pid_t pid)
{
    t_scope;
    sb_t      buf;
    pstream_t ps;
    pid_t     tpid;

    t_sb_init(&buf, (2 << 10));

    if (pid <= 0) {
        RETHROW(sb_read_file(&buf, "/proc/self/status"));
    } else {
        char path[PATH_MAX];

        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        RETHROW(sb_read_file(&buf, path));
    }

    ps = ps_initsb(&buf);

    while (!ps_done(&ps)) {
        if (ps_skipstr(&ps, "TracerPid:") >= 0) {
            tpid = ps_geti(&ps);
            return tpid > 0 ? tpid : 0;
        }
        RETHROW(ps_skip_afterchr(&ps, '\n'));
    }

    return -1;
}

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

/* }}} */
/* {{{ atfork methods / ifork */

/** Wrapper of fork() that must be used instead of it.
 *
 * Using this wrapper guarantees fork handlers are properly called.
 * In particular, the at_fork_on_parent method is called with the child pid
 * when using ifork.
 */
__must_check__ pid_t ifork(void);

/** Are we inside a ifork() call? */
bool ifork_in_progress(void);

/* }}} */

#endif /* IS_LIB_COMMON_UNIX_H */
