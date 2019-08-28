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

#if !defined(IS_LIB_COMMON_NET_H) || defined(IS_LIB_COMMON_NET_SCTP_H)
#  error "you must include net.h instead"
#elif defined(HAVE_NETINET_SCTP_H)
#define IS_LIB_COMMON_NET_SCTP_H

enum sctp_events {
    SCTP_DATA_IO_EV           = 0x01,
    SCTP_ASSOCIATION_EV       = 0x02,
    SCTP_ADDRESS_EV           = 0x04,
    SCTP_SEND_FAILURE_EV      = 0x08,
    SCTP_PEER_ERROR_EV        = 0x10,
    SCTP_SHUTDOWN_EV          = 0x20,
    SCTP_PARTIAL_DELIVERY_EV  = 0x40,
    SCTP_ADAPTATION_LAYER_EV  = 0x80,
};

#ifdef SCTP_SOCKOPT_CONNECTX_OLD
#  undef SCTP_SOCKOPT_CONNECTX
#  define SCTP_SOCKOPT_CONNECTX SCTP_SOCKOPT_CONNECTX_OLD
#endif
#define sctp_connectx  sctp_connectx_old
int sctp_connectx_old(int fd, struct sockaddr * nonnull addrs, int count);

/** Improved version of sctp_connectx().
 *
 * This function uses the CONNECTX3 API, allowing to return the new
 * association ID immediately. If the association ID is not required, it will
 * fallback on the old sctp_connectx().
 */
int sctp_connectx_ng(int fd, struct sockaddr *nonnull addrs, int count,
                     sctp_assoc_t *nullable id);

int sctp_enable_events(int fd, int flags);

ssize_t sctp_sendv(int sd, const struct iovec * nullable iov, int iovlen,
                   const struct sctp_sndrcvinfo * nonnull sinfo, int flags);
int sctp_addr_len(const sockunion_t * nonnull addrs, int count);
int sctp_close_assoc(int fd, int assoc_id);
#ifndef __cplusplus
int sctp_getaddrs(int fd, int optnum, sctp_assoc_t id,
                  struct sockaddr * nonnull addrs, int addr_size);
#endif

void sctp_dump_notif(char * nonnull buf, int len);

#endif
