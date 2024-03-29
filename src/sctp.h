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

/** SCTP minimal kernel Implementation: User API extensions.
 *
 * This file is part of the user library that offers support for the
 * Linux Kernel SCTP Implementation. The main purpose of this
 * code is to provide the SCTP Socket API mappings for user
 * application to interface with SCTP in kernel.
 *
 * This header represents the constants and the functions prototypes needed
 * to support the SCTP Extension to the Sockets API.
 *
 * The libsctp-dev package contains the same header file (netinet/sctp.h).
 * This header file contains all structures and constants needed to support
 * the SCTP Extension to the Sockets API.
 * But it is a duplication of structures and constants of the kernel sctp
 * header (and has some incoherence).
 */

#ifndef  IS_LIB_COMMON_SCTP_H
#define  IS_LIB_COMMON_SCTP_H

#include <stdint.h>
#include <linux/types.h>
#include <sys/socket.h>

#include <linux/sctp.h>

/** Socket option layer for SCTP */
#ifndef SOL_SCTP
#define SOL_SCTP  132
#endif

#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP  132
#endif

/** Preprocessor constants */
#define HAVE_SCTP
#define HAVE_KERNEL_SCTP
#define HAVE_SCTP_MULTIBUF
#define HAVE_SCTP_NOCONNECT
#define HAVE_SCTP_PRSCTP
#define HAVE_SCTP_ADDIP
#define HAVE_SCTP_CANSET_PRIMARY

int sctp_peeloff(int sd, sctp_assoc_t assoc_id);

/** This library function assists the user with the advanced features
 * of SCTP.  This is a new SCTP API described in the section 8.7 of the
 * Sockets API Extensions for SCTP. This is implemented using the
 * sendmsg() interface.
 */
int sctp_sendmsg(int s, const void *msg, size_t len, struct sockaddr *to,
                 socklen_t tolen, uint32_t ppid, uint32_t flags,
                 uint16_t stream_no, uint32_t timetolive, uint32_t context);

/** This library function assist the user with sending a message without
 * dealing directly with the CMSG header.
 */
int sctp_send(int s, const void *msg, size_t len,
              const struct sctp_sndrcvinfo *sinfo, int flags);

/** This library function assists the user with the advanced features
 * of SCTP.  This is a new SCTP API described in the section 8.8 of the
 * Sockets API Extensions for SCTP. This is implemented using the
 * recvmsg() interface.
 */
int sctp_recvmsg(int s, void *msg, size_t len, struct sockaddr *from,
                 socklen_t *fromlen, struct sctp_sndrcvinfo *sinfo,
                 int *msg_flags);

/** Return the address length for an address family. */
int sctp_getaddrlen(sa_family_t family);

#endif /* IS_LIB_COMMON_SCTP_H */
