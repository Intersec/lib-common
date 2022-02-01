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

#ifndef IS_CYTHON_LIBCOMMON_CORE_H
#define IS_CYTHON_LIBCOMMON_CORE_H

#include <Python.h>
#include <lib-common/core.h>

/* Macro for c99 bool. */
typedef _Bool cbool;

/* Macro to do a C assert in cython */
#define cassert(...)  assert(__VA_ARGS__)

/* Macros to use t_scope in cython */
#define t_scope_t                                                            \
    __attribute__((unused,cleanup(t_scope_cleanup))) const void *
#define t_scope_init()  mem_stack_pool_push(&t_pool_g)
#define t_scope_ignore(x)  (void)(x)
#define t_new_u8(count)  t_new(uint8_t, count)
#define t_new_char(count)  t_new(char, count)

/* Typedefs and macros to use sb in cython */
typedef char sb_buf_1k_t[1 << 10];
typedef char sb_buf_8k_t[8 << 10];

#define sb_scope_t  __attribute__((cleanup(sb_wipe))) sb_t
#define sb_scope_init_static(_buf)                                           \
    (sb_t)SB_INIT((_buf), countof(_buf), &mem_pool_static)
#define t_sb_scope_init(sz)  (sb_t)SB_INIT(t_new_raw(char, sz), sz, t_pool())
#define t_sb_scope_init_1k()  t_sb_scope_init(1 << 10)
#define t_sb_scope_init_8k()  t_sb_scope_init(8 << 10)

#endif /* IS_CYTHON_LIBCOMMON_CORE_H */
