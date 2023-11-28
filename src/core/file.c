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

#include <lib-common/file.h>
#include <lib-common/unix.h>

/****************************************************************************/
/* helpers                                                                  */
/****************************************************************************/

#define FAIL_ERRNO(expr, ret)  \
    do { int __save_errno = errno;                                        \
        (expr); errno = __save_errno;                                     \
        return (ret);                                                     \
    } while (0)

static int file_flush_obuf(file_t *f, int len)
{
    sb_t *obuf = &f->obuf;
    int goal = f->obuf.len - len;
    int fd = f->fd;

    assert (f->flags & FILE_WRONLY);
    assert (len <= f->obuf.len);

    while (obuf->len > goal) {
        int nb = write(fd, obuf->data, obuf->len);

        if (nb < 0) {
            if (ERR_RW_RETRIABLE(errno))
                continue;
            return -1;
        }
        sb_skip(obuf, nb);
        f->wpos += nb;
    }
    return 0;
}

int file_flags_to_open_flags(int flags)
{
    int oflags;

    switch (flags & FILE_OPEN_MODE_MASK) {
      case FILE_RDONLY:
#if 0
        oflags = O_RDONLY;
        break;
#else
        errno = ENOSYS;
        return -1;
#endif
      case FILE_WRONLY:
        oflags = O_WRONLY;
        break;
      case FILE_RDWR:
#if 0
        oflags = O_RDWR;
        break;
#else
        errno = ENOSYS;
        return -1;
#endif
      default:
        errno = EINVAL;
        return -1;
    }

    if (flags & FILE_CREATE) {
        oflags |= O_CREAT;
    }
    if (flags & FILE_EXCL) {
        oflags |= O_EXCL;
    }
    if (flags & FILE_TRUNC) {
        oflags |= O_TRUNC;
    }

    return oflags;
}

/****************************************************************************/
/* open/close                                                               */
/****************************************************************************/

file_t *file_open_at(int dfd, const char *path, unsigned flags, mode_t mode)
{
    file_t *res;
    int oflags = file_flags_to_open_flags(flags);

    if (oflags < 0) {
        return NULL;
    }

    res = p_new(file_t, 1);
    res->flags = flags;
    sb_init(&res->obuf);
    res->fd = openat(dfd, path, oflags, mode);
    if (res->fd < 0)
        FAIL_ERRNO(p_delete(&res), NULL);

    return res;
}

file_t *file_open(const char *path, unsigned flags, mode_t mode)
{
    file_t *res;
    int oflags = file_flags_to_open_flags(flags);

    if (oflags < 0) {
        return NULL;
    }

    res = p_new(file_t, 1);
    res->flags = flags;
    sb_init(&res->obuf);
    res->fd = open(path, oflags, mode);
    if (res->fd < 0)
        FAIL_ERRNO(p_delete(&res), NULL);

    return res;
}

int file_flush(file_t *f)
{
    if (!(f->flags & FILE_WRONLY)) {
        errno = EBADF;
        return -1;
    }
    return file_flush_obuf(f, f->obuf.len);
}

int file_close(file_t **fp)
{
    if (*fp) {
        file_t *f = *fp;
        int res = 0;
        res = file_flush(f) | p_close(&f->fd);
        sb_wipe(&f->obuf);
        p_delete(fp);
        return res;
    }
    return 0;
}

/****************************************************************************/
/* seeking                                                                  */
/****************************************************************************/

off_t file_seek(file_t *f, off_t offset, int whence)
{
    off_t res;

    if (f->flags & FILE_WRONLY)
        RETHROW(file_flush(f));
    res = lseek(f->fd, offset, whence);
    if (res != (off_t)-1) {
        f->wpos = res;
    }
    return res;
}

off_t file_tell(file_t *f)
{
    return f->wpos + f->obuf.len;
}

/****************************************************************************/
/* writing                                                                  */
/****************************************************************************/

int file_putc(file_t *f, int c)
{
    if (!(f->flags & FILE_WRONLY)) {
        errno = EBADF;
        return -1;
    }
    sb_addc(&f->obuf, c);
    if (c == '\n' || f->obuf.len > BUFSIZ) {
        RETHROW(file_flush(f));
    }
    return 0;
}

static ssize_t __file_writev(file_t *f, struct iovec *iov, size_t iovcnt)
{
    size_t oldcnt = iovcnt;
    size_t len = 0;
    ssize_t res = 0;

    for (size_t i = 0; i < iovcnt; i++) {
        len += iov[i].iov_len;
    }
    res = len;

    while (len >= BUFSIZ) {
        ssize_t resv = writev(f->fd, iov, iovcnt);

        if (resv < 0) {
            if (!ERR_RW_RETRIABLE(errno)) {
                res = -1;
                break;
            }
        } else {
            f->wpos += resv;
            len     -= resv;
            while (resv > 0) {
                if (iov[0].iov_len > (size_t)resv) {
                    iov->iov_base = (char *)iov->iov_base + resv;
                    iov->iov_len -= resv;
                    break;
                }
                resv -= iov[0].iov_len;
                iov++;
                iovcnt--;
            }
        }
    }

    if (f->obuf.len) {
        if (iovcnt < oldcnt) {
            sb_reset(&f->obuf);
        } else
        if (iovcnt == oldcnt) {
            sb_skip_upto(&f->obuf, iov->iov_base);
            iov++;
            iovcnt--;
        }
    }
    for (size_t i = 0; i < iovcnt; i++) {
        sb_add(&f->obuf, iov[i].iov_base, iov[i].iov_len);
    }
    return res;
}

ssize_t file_writev(file_t *f, const struct iovec *iov, size_t iovcnt)
{
    struct iovec iov2[IOV_MAX];

    if (unlikely(!(f->flags & FILE_WRONLY))) {
        errno = EBADF;
        return -1;
    }
    if (unlikely(iovcnt + 1 > IOV_MAX)) {
        errno = EINVAL;
        return -1;
    }

    iov2[0] = MAKE_IOVEC(f->obuf.data, f->obuf.len);
    p_copy(iov2 + 1, iov, iovcnt);

    if (f->obuf.len == 0)
        return __file_writev(f, iov2 + 1, iovcnt);
    return __file_writev(f, iov2, iovcnt + 1);
}

ssize_t file_writevf(file_t *f, const char *fmt, va_list ap)
{
    int orig_len, res;

    orig_len = f->obuf.len;
    sb_addvf(&f->obuf, fmt, ap);
    res = f->obuf.len - orig_len;

    if (f->obuf.len > BUFSIZ) {
        RETHROW(file_flush(f));
    }

    return res;
}

ssize_t file_write(file_t *f, const void *data, size_t len)
{
    struct iovec iov[2];

    if (unlikely(!(f->flags & FILE_WRONLY))) {
        errno = EBADF;
        return -1;
    }
    if (f->obuf.len) {
        iov[0] = MAKE_IOVEC(f->obuf.data, f->obuf.len);
        iov[1] = MAKE_IOVEC(data, len);
        return __file_writev(f, iov, 2);
    }
    iov[0] = MAKE_IOVEC(data, len);
    return __file_writev(f, iov, 1);
}

int write_in_tmp_file(char *file_path, const char *data, int len, sb_t *err)
{
    int fd;
    int ret;

    if ((fd = mkstemp(file_path)) < 0) {
        sb_setf(err, "failed to create a temporary path: %m");
        return -1;
    }

    ret = xwrite(fd, data, len);
    if (ret < 0) {
        PROTECT_ERRNO(p_close(&fd));
    } else {
        ret = p_close(&fd);
    }

    if (ret < 0) {
        sb_setf(err, "failed to write data in temporary file `%s`: %m",
                file_path);
        unlink(file_path);
        return -1;
    }

    return 0;
}

int file_truncate(file_t *f, off_t len)
{
    if (len < f->wpos) {
        RETHROW(ftruncate(f->fd, len));
        RETHROW(lseek(f->fd, len, SEEK_SET));
        f->wpos = len;
        sb_reset(&f->obuf);
    } else if (len <= f->wpos + f->obuf.len) {
        sb_clip(&f->obuf, len - f->wpos);
    } else { /* len > f->wpos + f->obuf.len */
        if (len - f->wpos > BUFSIZ) {
            /* Buffer would be too big: flush */
            RETHROW(file_flush(f));
            RETHROW(ftruncate(f->fd, len));
            RETHROW(lseek(f->fd, len, SEEK_SET));
            f->wpos = len;
        } else {
            sb_add0s(&f->obuf, len - (f->wpos + f->obuf.len));
        }
    }

    return 0;
}
