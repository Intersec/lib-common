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

#include "net.h"
#include "unix.h"

static int sock_reuseaddr(int sock)
{
    int v = 1;

    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&v, sizeof(v));
}

int socketpairx(int d, int type, int protocol, int flags, int sv[2])
{
    RETHROW(socketpair(d, type, protocol, sv));
    if (!(flags & O_NONBLOCK))
        return 0;
    if (fd_set_features(sv[0], flags) || fd_set_features(sv[1], flags)) {
        PROTECT_ERRNO(({ p_close(&sv[0]); p_close(&sv[1]); }));
        return -1;
    }
    return 0;
}

int bindx(int sock, const sockunion_t *addrs, int cnt,
          int type, int proto, int flags)
{
    int to_close = -1;

    if (proto != IPPROTO_SCTP && cnt != 1) {
        errno = EINVAL;
        return -1;
    }

    if (sock < 0) {
        to_close = sock = RETHROW(socket(addrs->family, type, proto));
    }

    if (type != SOCK_DGRAM && sock_reuseaddr(sock) < 0) {
        goto error;
    }

    if (fd_set_features(sock, flags))
        goto error;

    if (proto != IPPROTO_SCTP || cnt == 1) {
        if (addrs->family == AF_UNIX) {
            unlink(addrs->sunix.sun_path);
        }

        if (bind(sock, &addrs->sa, sockunion_len(addrs)) < 0)
            goto error;
    } else {
        socklen_t sz = 0;
        while (cnt-- > 0) {
            sockunion_t *su = (sockunion_t *)((char *)addrs + sz);
            socklen_t len = sockunion_len(su);
            if (len == (socklen_t)-1) {
                errno = EINVAL;
                goto error;
            }
            sz += len;
        }
        if (setsockopt(sock, SOL_SCTP, SCTP_SOCKOPT_BINDX_ADD, addrs, sz))
            goto error;
    }

    return sock;

  error:
    p_close(&to_close);
    return -1;
}

int listenx(int sock, const sockunion_t *addrs, int cnt,
            int type, int proto, int flags)
{
    int to_close = -1;

    if (sock < 0) {
        to_close = sock = RETHROW(bindx(-1, addrs, cnt, type, proto, flags));
    }

    if (listen(sock, SOMAXCONN) < 0) {
        if (to_close >= 0)
            PROTECT_ERRNO(p_close(&to_close));
        return -1;
    }
    return sock;
}

int isconnectx(int sock, const sockunion_t *addrs, int cnt, int type,
               int proto, int flags)
{
    return connectx_as(sock, addrs, cnt, NULL, type, proto, flags, 0);
}

int connectx_as(int sock, const sockunion_t *addrs, int cnt,
                const sockunion_t * nullable src, int type, int proto,
                int flags, int timeout)
{
    int to_close = -1;

    if (proto != IPPROTO_SCTP && cnt != 1) {
        errno = EINVAL;
        return -1;
    }

    if (sock < 0) {
        to_close = sock = RETHROW(socket(addrs->family, type, proto));
    }
    if (src && bind(sock, &src->sa, sockunion_len(src)) < 0) {
        goto error;
    }

    if (fd_set_features(sock, flags)) {
        goto error;
    }

    if (timeout) {
        struct timeval tv = { .tv_sec = timeout };

        assert (!(flags & O_NONBLOCK));
        /* Set a socket timeout to avoid blocking indefinitely. */
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    if (proto != IPPROTO_SCTP || cnt == 1) {
        if (connect(sock, &addrs->sa, sockunion_len(addrs)) < 0
        &&  !ERR_CONNECT_RETRIABLE(errno))
        {
            goto error;
        }
    } else {
        socklen_t sz = 0;
        while (cnt-- > 0) {
            sockunion_t *su = (sockunion_t *)((char *)addrs + sz);
            socklen_t len = sockunion_len(su);
            if (len == (socklen_t)-1) {
                errno = EINVAL;
                goto error;
            }
            sz += len;
        }
        if (setsockopt(sock, SOL_SCTP, SCTP_SOCKOPT_CONNECTX, addrs, sz))
            goto error;
    }

    return sock;

  error:
    PROTECT_ERRNO(p_close(&to_close));
    return -1;
}


int acceptx_get_addr(int server_fd, int flags, sockunion_t *su)
{
    int sock;
    socklen_t len = sizeof(su->ss);

    sock = RETHROW(accept(server_fd, su ? &su->sa : NULL, su ? &len : NULL));

    if (fd_set_features(sock, flags)) {
        PROTECT_ERRNO(p_close(&sock));
        return -1;
    }
    return sock;
}

int acceptx(int server_fd, int flags)
{
    return acceptx_get_addr(server_fd, flags, NULL);
}

int getsockport(int sock, sa_family_t family)
{
    sockunion_t local = { .family = family };
    socklen_t   size  = sockunion_len(&local);

    if (getsockname(sock, &local.sa, &size)) {
        e_trace(1, "getsockname failed: %m");
        return 0;
    }
    return sockunion_getport(&local);
}

int getpeerport(int sock, sa_family_t family)
{
    sockunion_t local = { .family = family };
    socklen_t   size  = sockunion_len(&local);

    if (getpeername(sock, &local.sa, &size)) {
        e_trace(1, "getsockname failed: %m");
        return 0;
    }
    return sockunion_getport(&local);
}

int socket_connect_status(int sock)
{
    int err;
    socklen_t size = sizeof(err);

    RETHROW(getsockopt(sock, SOL_SOCKET, SO_ERROR, (void *)&err, &size));
    if (!err)
        return 1;
    errno = err;
    return ERR_CONNECT_RETRIABLE(err) ? 0 : -1;
}
