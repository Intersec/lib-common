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

#if !defined(IS_LIB_COMMON_HASH_H) || defined(IS_LIB_COMMON_HASH_IOP_H)
#  error "you must include <lib-common/hash.h> instead"
#else
#define IS_LIB_COMMON_HASH_IOP_H

struct iop_struct_t;

typedef void (iop_hash_f)(void * nonnull ctx, const void * nonnull input,
                          ssize_t ilen);

enum {
    IOP_HASH_SKIP_MISSING = 1 << 0, /* Skip missing optional fields         */
    IOP_HASH_SKIP_DEFAULT = 1 << 1, /* Skip fields having the default value */
    IOP_HASH_SHALLOW_DEFAULT = 1 << 2, /* Compare pointers, not content of
                                          string to detect default values */
    IOP_HASH_DONT_INCLUDE_CLASS_ID = 1 << 3, /* Do not take the class id into
                                                account when hashing a class
                                              */
};

#define ATTRS
#define F(x)  x
#include "iop.in.h"
#undef F
#undef ATTRS

#endif
