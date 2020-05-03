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

#if !defined(IS_LIB_COMMON_ARITH_H) || defined(IS_LIB_COMMON_ARITH_CMP_H)
#  error "you must include arith.h instead"
#else
#define IS_LIB_COMMON_ARITH_CMP_H

static inline int min_int(int a, int b)          { return MIN(a, b); }
static inline int max_int(int a, int b)          { return MAX(a, b); }
static inline int clamp_int(int a, int m, int M) { return CLIP(a, m, M); }

static inline void maximize(int * nonnull pi, int val) {
    if (*pi < val)
        *pi = val;
}
static inline void minimize(int * nonnull pi, int val) {
    if (*pi > val)
        *pi = val;
}

#endif
