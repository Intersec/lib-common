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

#ifndef IS_LIB_COMMON_NET_H
#define IS_LIB_COMMON_NET_H

#include <lib-common/core.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <lib-common/sctp.h>
#ifdef SCTP_ADAPTION_LAYER
   /* see http://www1.ietf.org/mail-archive/web/tsvwg/current/msg05971.html */
#  define SCTP_ADAPTATION_LAYER         SCTP_ADAPTION_LAYER
#  define sctp_adaptation_layer_event   sctp_adaption_layer_event
#endif
#include <sys/socket.h>
#include <sys/un.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#include "net/addr.h"
#include "net/socket.h"
#include "net/sctp.h"
#include "net/rate.h"

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
