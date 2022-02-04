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

#ifndef IS_CYTHON_LIBCOMMON_IOP_H
#define IS_CYTHON_LIBCOMMON_IOP_H

#include <lib-common/iop.h>

/* Macros for ic hdr */
#define is_ic_hdr_simple_hdr(hdr)  IOP_UNION_IS(ic__hdr, (hdr), simple)
#define t_iop_new_ic_hdr()  t_iop_new(ic__hdr)
#define iop_init_ic_simple_hdr(hdr)  iop_init(ic__simple_hdr, (hdr))
#define iop_ic_hdr_from_simple_hdr(hdr)  IOP_UNION(ic__hdr, simple, (hdr))
#define iop_dup_ic_hdr(hdr)  iop_dup(ic__hdr, (hdr))

/* Macros for ichannels */
#define ichannel_get_cmd(ic)  (ic)->cmd

#endif /* IS_CYTHON_LIBCOMMON_IOP_H */
