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

#ifndef IS_LIB_COMMON_QPAGES_H
#define IS_LIB_COMMON_QPAGES_H

#include <lib-common/core.h>

/* ACHTUNG MINEN: QPAGE_SIZE must be an unsigned literal to force unsigned
 * arithmetics in expressions, and an uintptr_t to allow dirty masking against
 * pointers.
 */
#define QPAGE_SHIFT         12U
#define QPAGE_SIZE          ((uintptr_t)1 << QPAGE_SHIFT)
#define QPAGE_MASK          (QPAGE_SIZE - 1)

#define QPAGE_COUNT_BITS    (15U)  /* up to 32k pages blocks    */
#define QPAGE_COUNT_MAX     (1U << QPAGE_COUNT_BITS)
#define QPAGE_ALLOC_MAX     (1U << (QPAGE_COUNT_BITS + QPAGE_SHIFT))

void *qpage_alloc_align(size_t n, size_t shift, uint32_t *seg);
void *qpage_allocraw_align(size_t n, size_t shift, uint32_t *seg);

void *qpage_remap(void *ptr, size_t old_n, uint32_t old_seg,
                  uint32_t new_n, uint32_t *new_seg, bool may_move);
void *qpage_remap_raw(void *ptr, size_t old_n, uint32_t old_seg,
                      uint32_t new_n, uint32_t *new_seg, bool may_move);

void *qpage_dup_n(const void *ptr, size_t n, uint32_t *seg);
void  qpage_free_n(void *, size_t n, uint32_t seg);


static inline void *qpage_alloc_n(size_t n, uint32_t *seg) {
    return qpage_alloc_align(n, 0, seg);
}
static inline void *qpage_alloc(uint32_t *seg) {
    return qpage_alloc_n(1, seg);
}
static inline void *qpage_allocraw_n(size_t n, uint32_t *seg) {
    return qpage_allocraw_align(n, 0, seg);
}
static inline void *qpage_allocraw(uint32_t *seg) {
    return qpage_allocraw_n(1, seg);
}

static inline void *qpage_dup(const void *ptr, uint32_t *seg) {
    return qpage_dup_n(ptr, 1, seg);
}

static inline void qpage_free(void *ptr, uint32_t seg) {
    qpage_free_n(ptr, 1, seg);
}
static inline void qpage_free_n_noseg(void *ptr, size_t n) {
    qpage_free_n(ptr, n, -1);
}
static inline void qpage_free_noseg(void *ptr) {
    qpage_free_n(ptr, 1, -1);
}

MODULE_DECLARE(qpage);

#endif
