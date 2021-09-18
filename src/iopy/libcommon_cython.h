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

#ifndef IS_PYTHON_LIBCOMMON_CYTHON_H
#define IS_PYTHON_LIBCOMMON_CYTHON_H

#include <lib-common/iop.h>
#include <lib-common/iop-yaml.h>
#include <lib-common/xmlr.h>
#include <lib-common/xmlpp.h>
#include <lib-common/iop-rpc.h>
#include <lib-common/farch.h>
#include <lib-common/thr.h>

/* Macro for c99 bool. */
typedef _Bool cbool;

/* Macros to use t_scope in cython */
#define t_scope_t                                                            \
    __attribute__((unused,cleanup(t_scope_cleanup))) const void *
#define t_scope_init()  mem_stack_pool_push(&t_pool_g)
#define t_scope_ignore(x)  (void)(x)
#define t_new_u8(count)  t_new(uint8_t, count)
#define t_new_char(count)  t_new(char, count)

/* Macros to use sb in cython */
#define sb_scope_t  __attribute__((cleanup(sb_wipe))) sb_t
#define sb_scope_init(sz)  (sb_t)SB_INIT(alloca(sz), sz, &mem_pool_static)
#define sb_scope_init_1k()  sb_scope_init(1 << 10)
#define sb_scope_init_8k()  sb_scope_init(8 << 10)
#define t_sb_scope_init(sz)  (sb_t)SB_INIT(t_new_raw(char, sz), sz, t_pool())
#define t_sb_scope_init_1k()  t_sb_scope_init(1 << 10)
#define t_sb_scope_init_8k()  t_sb_scope_init(8 << 10)

/* Macro to do a C assert in cython */
#define cassert(...)  assert(__VA_ARGS__)

/* Macros for ic hdr */
#define is_ic_hdr_simple_hdr(hdr)  IOP_UNION_IS(ic__hdr, (hdr), simple)
#define t_iop_new_ic_hdr()  t_iop_new(ic__hdr)
#define iop_init_ic_simple_hdr(hdr)  iop_init(ic__simple_hdr, (hdr))
#define iop_ic_hdr_from_simple_hdr(hdr)  IOP_UNION(ic__hdr, simple, (hdr))
#define iop_dup_ic_hdr(hdr)  iop_dup(ic__hdr, (hdr))

/* Macros for ichannels */
#define ichannel_get_cmd(ic)  (ic)->cmd

/* iop dso resources */
#define iopy_dso_get_scripts(dso)                                            \
    IOP_DSO_GET_RESSOURCES(dso, iopy_on_register)
typedef char farch_name_t[FARCH_MAX_FILENAME];

#endif
