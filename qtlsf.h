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

#ifndef IS_LIB_COMMON_QTLSF_H
#define IS_LIB_COMMON_QTLSF_H

#include "core.h"

/* XXX tlsf is deprecate. It was known to be broken and is now an alias to
 * malloc */
static inline mem_pool_t *tlsf_pool_new(size_t minpagesize)
{
    return &mem_pool_libc;
}

static inline void tlsf_pool_delete(mem_pool_t **mpp) {
    *mpp = NULL;
}

#endif
