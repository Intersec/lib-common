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

#ifndef IS_LIB_COMMON_LOG_FILE_H
#define IS_LIB_COMMON_LOG_FILE_H

#include "file-bin.h"
#include "file.h"
#include "core.iop.h"
#include "container-qhash.h"

/* This module provides auto rotating log files: log files are rotated
 * automatically depending on file size or data, or both.
 */

#define LOG_FILE_DATE_FMT  "%04d%02d%02d_%02d%02d%02d"
#define LOG_FILE_DATE_FMT_ARG(tm)  \
    (tm).tm_year + 1900, (tm).tm_mon + 1, (tm).tm_mday, (tm).tm_hour,        \
    (tm).tm_min, (tm).tm_sec


enum log_file_flags {
    LOG_FILE_USE_LAST  = (1U << 0),
    LOG_FILE_COMPRESS  = (1U << 1), /* Use gzip on results */
    LOG_FILE_UTCSTAMP  = (1U << 2),
    LOG_FILE_NOSYMLINK = (1U << 3),

    /* Force a rotation when opening the log_file_t. */
    LOG_FILE_FORCE_ROTATE = (1U << 4),
};

enum log_file_event {
    LOG_FILE_CREATE, /* just after a new file creation */
    LOG_FILE_CLOSE,  /* called after file_close is called */
    LOG_FILE_DELETE, /* called after a log file is deleted. */
    LOG_FILE_ROTATE, /* at rotation, after the log file is closed,
                        but before the new one is opened. */
};

struct log_file_t;

/* Log file event callback. fpath is built using the prefix passed during
 * the log_file initialization. So, fpath will be absolute if the log_file
 * was constructed with an absolute prefix, or else, fpath is relative
 * to the current working directory at log_file_new() time. */
typedef void (log_file_cb_f)(struct log_file_t *file,
                             enum log_file_event event,
                             const char *fpath, void *priv);

typedef struct log_file_t {
    /* Internal file handler. */
    union {
        file_bin_t *_bin_internal;
        file_t     *_internal;
    };

    uint32_t flags;
    uint32_t mode;
    uint64_t total_size;
    int      max_size;
    int      max_files;
    int      max_total_size; /* in Mo */
    time_t   open_date;
    time_t   rotate_delay;
    char     prefix[PATH_MAX];
    char     ext[8];

    /* Flags. */
    bool disable_rotation : 1;
    bool is_file_bin      : 1;

    /* Event callback */
    log_file_cb_f *on_event;
    void          *priv_cb;

    /* Internal usage. */
    qh_t(u64) files_being_compressed;
    int refcnt;
} log_file_t;

log_file_t *log_file_init(log_file_t *, const char *nametpl, int flags);
log_file_t *log_file_new(const char *nametpl, int flags);

/** Open a log file.
 *
 * \param[in]  log_file      The log file to open
 * \param[in]  use_file_bin  True if the file bin library should be used,
 *                           false otherwise.
 *
 * \return  0 on success, a negative value otherwise.
 */
__must_check__ int log_file_open(log_file_t *log_file, bool use_file_bin);

__must_check__ int log_file_close(log_file_t **log_file);
__must_check__ int log_file_rotate(log_file_t *log_file);

/** Creates a log file from a configuration object, and opens it. */
log_file_t *
log_file_create_from_iop(const char *nametpl,
                         const core__log_file_configuration__t *conf,
                         bool use_file_bin, int flags,
                         log_file_cb_f *on_event, void *priv);

void log_file_set_maxsize(log_file_t *file, int max);
void log_file_set_rotate_delay(log_file_t *file, time_t delay);
void log_file_set_maxfiles(log_file_t *file, int maxfiles);
void log_file_set_maxtotalsize(log_file_t *file, int maxtotalsize);
void
log_file_set_file_cb(log_file_t *file, log_file_cb_f *on_event,
                     void *priv);
void log_file_set_mode(log_file_t *file, uint32_t mode);

int log_fwrite(log_file_t *log_file, const void *data, size_t len);
int log_fwritev(log_file_t *log_file, struct iovec *iov, size_t iovlen);
int log_fprintf(log_file_t *log_file, const char *format, ...)
    __attr_printf__(2, 3)  __attr_nonnull__((1, 2));

int log_file_flush(log_file_t *log_file);

/** Disable file rotations until 'log_file_enable_rotation' is called.
 *
 * This function should be used for logs we don't want to spread on two (or
 * more) files because of rotation mechanism.
 */
void log_file_disable_rotation(log_file_t *file);

/** Re-enable file rotations after 'log_file_disable_rotation' has been
 *  called and trigger a rotation if needed.
 *
 * \return 0 on success, -1 in case of rotation failure.
 */
int log_file_enable_rotation(log_file_t *log_file);

/** Get the creation timestamp of a log file from his full path.
 *
 * \param[in]  file  The log_file_t.
 * \param[in]  path  The full path of the log file we want the date.
 * \param[out] out   Pointer to the creation date timestamp to fill.
 *
 * \return 0 on success, a negative value on failure.
 */
int log_file_get_file_stamp(const log_file_t *file, const char *path,
                            time_t *out);

static inline off_t log_file_tell(log_file_t *log_file)
{
    if (!log_file->_internal) {
        errno = EBADF;
        return -1;
    }
    if (log_file->is_file_bin) {
        return log_file->_bin_internal->cur;
    } else {
        return file_tell(log_file->_internal);
    }
}

#ifdef __has_blocks

/** Write logs in the current file in a transaction-like mode.
 *
 * This function should be used for logs we don't want to split on two (or
 * more) files because of the rotation mechanism.
 *
 * This function executes the block given as an argument with file rotations
 * disabled. In case of failure, the current file is rewinded to the position
 * it had before the call.
 *
 * XXX Warning: we speak about transaction here because of the rollback
 * capability of the feature. For example, this function does *not* unlock the
 * possibility to write logs from different threads using the same log_file_t.
 *
 * \param[in]  file   The log_file_t.
 * \param[in]  log_b  The callback block.
 *
 * \return 0 on success, a negative value on failure.
 */
int log_fwrite_transaction(log_file_t *file, int (BLOCK_CARET log_b)(void));

#endif

#endif /* IS_LIB_COMMON_LOG_FILE_H */
