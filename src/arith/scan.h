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

#if !defined(IS_LIB_COMMON_ARITH_H) || defined(IS_LIB_COMMON_ARITH_SCAN_H)
#  error "you must include arith.h instead"
#else
#define IS_LIB_COMMON_ARITH_SCAN_H

/* This module implements optimized scans primitives on data. The scans are
 * optimized with SSE instruction sets, as a consequence, we requires the
 * memory to be aligned on 128bits
 */

bool is_memory_zero(const void * nonnull data, size_t len);

ssize_t scan_non_zero16(const uint16_t u16[], size_t pos, size_t len);
ssize_t scan_non_zero32(const uint32_t u32[], size_t pos, size_t len);

size_t count_non_zero8(const uint8_t u8[], size_t len);
size_t count_non_zero16(const uint16_t u16[], size_t len);
size_t count_non_zero32(const uint32_t u32[], size_t len);
extern size_t (* nonnull count_non_zero64)(const uint64_t u64[], size_t len);
size_t count_non_zero128(const void * nonnull u128, size_t len);

#endif
