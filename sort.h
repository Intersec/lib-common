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

#ifndef IS_LIB_COMMON_SORT_H
#define IS_LIB_COMMON_SORT_H

#include "core.h"

/* {{{ Numeric optimized versions */

static inline
size_t bisect8(uint8_t what, const uint8_t data[], size_t len, bool *found);
static inline
size_t bisect_i8(int8_t what, const int8_t data[], size_t len, bool *found);
static inline
bool contains8(uint8_t what, const uint8_t data[], size_t len);
static inline
bool contains_i8(int8_t what, const int8_t data[], size_t len);

void   dsort8(uint8_t base[], size_t n);
void   dsort_i8(int8_t base[], size_t n);
size_t uniq8(uint8_t base[], size_t n);
static inline size_t uniq_i8(int8_t base[], size_t n) {
    return uniq8((uint8_t *)base, n);
}

static inline size_t
bisect16(uint16_t what, const uint16_t data[], size_t len, bool *found);
static inline size_t
bisect_i16(int16_t what, const int16_t data[], size_t len, bool *found);
static inline
bool contains16(uint16_t what, const uint16_t data[], size_t len);
static inline
bool contains_i16(int16_t what, const int16_t data[], size_t len);

void   dsort16(uint16_t base[], size_t n);
void   dsort_i16(int16_t base[], size_t n);
size_t uniq16(uint16_t base[], size_t n);
static inline size_t uniq_i16(int16_t base[], size_t n) {
    return uniq16((uint16_t *)base, n);
}

static inline size_t
bisect32(uint32_t what, const uint32_t data[], size_t len, bool *found);
static inline size_t
bisect_i32(int32_t what, const int32_t data[], size_t len, bool *found);
static inline
bool contains32(uint32_t what, const uint32_t data[], size_t len);
static inline
bool contains_i32(int32_t what, const int32_t data[], size_t len);

void   dsort32(uint32_t base[], size_t n);
void   dsort_i32(int32_t base[], size_t n);
size_t uniq32(uint32_t base[], size_t n);
static inline size_t uniq_i32(int32_t base[], size_t n) {
    return uniq32((uint32_t *)base, n);
}

static inline size_t
bisect64(uint64_t what, const uint64_t data[], size_t len, bool *found);
static inline size_t
bisect_i64(int64_t what, const int64_t data[], size_t len, bool *found);
static inline
bool contains64(uint64_t what, const uint64_t data[], size_t len);
static inline
bool contains_i64(int64_t what, const int64_t data[], size_t len);

void   dsort64(uint64_t base[], size_t n);
void   dsort_i64(int64_t base[], size_t n);
size_t uniq64(uint64_t base[], size_t n);
static inline size_t uniq_i64(int64_t base[], size_t n) {
    return uniq64((uint64_t *)base, n);
}

#define type_t   uint8_t
#define bisect   bisect8
#define contains contains8
#include "sort-numeric.in.h"

#define type_t   int8_t
#define bisect   bisect_i8
#define contains contains_i8
#include "sort-numeric.in.h"

#define type_t   uint16_t
#define bisect   bisect16
#define contains contains16
#include "sort-numeric.in.h"

#define type_t   int16_t
#define bisect   bisect_i16
#define contains contains_i16
#include "sort-numeric.in.h"

#define type_t   uint32_t
#define bisect   bisect32
#define contains contains32
#include "sort-numeric.in.h"

#define type_t   int32_t
#define bisect   bisect_i32
#define contains contains_i32
#include "sort-numeric.in.h"

#define type_t   uint64_t
#define bisect   bisect64
#define contains contains64
#include "sort-numeric.in.h"

#define type_t   int64_t
#define bisect   bisect_i64
#define contains contains_i64
#include "sort-numeric.in.h"

/* }}} */

/* Generic implementations */
typedef int (cmp_r_t)(const void *a, const void *b, void *arg);
typedef void (del_r_t)(void *v, void *arg);

size_t  uniq(void *data, size_t size, size_t nmemb, cmp_r_t *cmp,
             void *cmp_arg, del_r_t * nullable del, void *del_arg);
size_t  bisect(const void *what, const void *data, size_t size,
               size_t nmemb, bool *found, cmp_r_t *cmp, void *arg);
bool    contains(const void *what, const void *data, size_t size,
                 size_t nmemb, cmp_r_t *cmp, void *arg);

#ifdef __has_blocks
typedef int (BLOCK_CARET cmp_b)(const void *a, const void *b);
typedef void (BLOCK_CARET del_b)(void *v);

size_t  uniq_blk(void *data, size_t size, size_t nmemb, cmp_b cmp,
                 del_b nullable del);
size_t  bisect_blk(const void *what, const void *data, size_t size,
                   size_t nmemb, bool *found, cmp_b cmp);
bool    contains_blk(const void *what, const void *data, size_t size,
                     size_t nmemb, cmp_b cmp);
#endif

typedef int (*cmp_f)(const void *a, const void *b);

#define SORT_DEF(sfx, type_t, cmp)                                          \
static inline int cmp_##sfx(const void *p1, const void *p2) {               \
    return cmp(*(const type_t *)p1, *(const type_t *)p2);                   \
}

SORT_DEF(i8,     int8_t,   CMP);
SORT_DEF(u8,     uint8_t,  CMP);
SORT_DEF(i16,    int16_t,  CMP);
SORT_DEF(u16,    uint16_t, CMP);
SORT_DEF(i32,    int32_t,  CMP);
SORT_DEF(u32,    uint32_t, CMP);
SORT_DEF(i64,    int64_t,  CMP);
SORT_DEF(u64,    uint64_t, CMP);
SORT_DEF(bool,   bool,     CMP);
SORT_DEF(double, double,   CMP);

#undef SORT_DEF

static inline int cmp_lstr_bin(const void *s1, const void *s2) {
    return lstr_cmp(*(const lstr_t *)s1, *(const lstr_t *)s2);
}
static inline int cmp_lstr_iutf8(const void *s1, const void *s2) {
    return lstr_utf8_icmp(*(const lstr_t *)s1, *(const lstr_t *)s2);
}

#endif
