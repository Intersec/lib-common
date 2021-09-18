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

#if !defined(IS_LIB_COMMON_NET_H) || defined(IS_LIB_COMMON_NET_SOCKET_H)
#  error "you must include net.h instead"
#else
#define IS_LIB_COMMON_NET_SOCKET_H

/* Create a pair of connected sockets with some socket options.
 *
 * The socket pair is obtain with `socketpair(d, type, protocol)`. See
 * socketpair(2) for more information about socketpair. The flags are ignored
 * if O_NONBLOCK is missing.
 */
int socketpairx(int d, int type, int protocol, int flags, int sv[2]);

int bindx(int sock, const sockunion_t * nonnull, int cnt,
          int type, int proto, int flags);
int listenx(int sock, const sockunion_t * nonnull, int cnt,
            int type, int proto, int flags);
int isconnectx(int sock, const sockunion_t * nonnull, int cnt,
               int type, int proto, int flags);
#define connectx(sock, su, cnt, type, proto, flags)  \
    isconnectx((sock), (su), (cnt), (type), (proto), (flags))

/** Connect using a specified src
 *
 * \param[in]   sock    a file descriptor for the socket, a negative value to
 *                      create a new socket
 * \param[in]   addrs   addresses to connect to
 * \param[in]   cnt     number of addresses to connect to (=1 except for SCTP)
 * \param[in]   src     use specific network interface as source
 * \param[in]   type    see socket(2)
 * \param[in]   proto   see socket(2)
 * \param[in]   flags   see fd_features_flags_t
 * \param[in]   timeout  the maximum time, in seconds, spent to establish the
 *                       connection; only for blocking connects; bound to the
 *                       socket and kept after connection such that further
 *                       blocking read attempts would also have this timeout;
 *                       ignored if equals to 0.
 *
 * \Returns On success, the file descriptor for the socket
 *          On error, -1 and errno is set appropriately.
 *
 */
int connectx_as(int sock, const sockunion_t * nonnull addrs, int cnt,
                const sockunion_t * nullable src, int type, int proto,
                int flags, int timeout);
int acceptx(int server_fd, int flags);
int acceptx_get_addr(int server_fd, int flags, sockunion_t * nullable su);

int getsockport(int sock, sa_family_t family);
int getpeerport(int sock, sa_family_t family);

/* returns -1 if broken, 0 if in progress, 1 if connected */
int socket_connect_status(int sock);

#endif
