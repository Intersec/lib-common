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

#ifndef IS_LIB_COMMON_CONTAINER_RING_H
#define IS_LIB_COMMON_CONTAINER_RING_H

#include <lib-common/core.h>

#define RING_TYPE(type_t, pfx)  \
    typedef struct pfx##_ring { \
        type_t *tab;            \
        int first, len, size;   \
    } pfx##_ring
RING_TYPE(void, generic);

void generic_ring_ensure(generic_ring *r, int newlen, int el_siz)
    __leaf;

#define RING_MAP(r, f, ...)                                            \
    do {                                                               \
        int __pos = (r)->first;                                        \
        for (int __i = (r)->len; __i > 0; __i--) {                     \
            f((r)->tab + __pos, ##__VA_ARGS__);                        \
            if (++__pos == (r)->size)                                  \
                __pos = 0;                                             \
        }                                                              \
    } while (0)

#define RING_FILTER(r, f, ...)                                         \
    do {                                                               \
        int __r = (r)->first, __w = __r;                               \
        for (int __i = (r)->len; __i > 0; __i--) {                     \
            if (f((r)->tab + __r, ##__VA_ARGS__)) {                    \
                (r)->tab[__w] = (r)->tab[__r];                         \
                if (++__w == (r)->size)                                \
                    __w = 0;                                           \
            }                                                          \
            if (++__r == (r)->size)                                    \
                __r = 0;                                               \
        }                                                              \
        (r)->len -= __r >= __w ? __r - __w : (r)->size + __r - __w;    \
    } while (0)

#define RING_FUNCTIONS(type_t, pfx, wipe)                              \
    GENERIC_INIT(pfx##_ring, pfx##_ring);                              \
    __unused__                                                         \
    static inline void pfx##_ring_wipe(pfx##_ring *r) {                \
        RING_MAP(r, wipe);                                             \
        p_delete(&r->tab);                                             \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline int pfx##_ring_pos(pfx##_ring *r, int idx) {         \
        int pos = r->first + idx;                                      \
        return pos >= r->size ? pos - r->size : pos;                   \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline void pfx##_ring_unshift(pfx##_ring *r, type_t e) {   \
        generic_ring_ensure((void *)r, ++r->len, sizeof(type_t));      \
        r->first = r->first ? r->first - 1 : r->size - 1;              \
        r->tab[r->first] = e;                                          \
    }                                                                  \
    __unused__                                                         \
    static inline void pfx##_ring_push(pfx##_ring *r, type_t e) {      \
        generic_ring_ensure((void *)r, r->len + 1, sizeof(type_t));    \
        r->tab[pfx##_ring_pos(r, r->len++)] = e;                       \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline bool pfx##_ring_shift(pfx##_ring *r, type_t *e) {    \
        if (r->len <= 0)                                               \
            return false;                                              \
        *e = r->tab[r->first];                                         \
        if (++r->first == r->size)                                     \
            r->first = 0;                                              \
        r->len--;                                                      \
        return true;                                                   \
    }                                                                  \
    __unused__                                                         \
    static inline bool pfx##_ring_pop(pfx##_ring *r, type_t *e) {      \
        if (r->len <= 0)                                               \
            return false;                                              \
        *e = r->tab[pfx##_ring_pos(r, --r->len)];                      \
        return true;                                                   \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline bool pfx##_ring_skip(pfx##_ring *r, int n) {         \
        if (r->len < n || n < 0)                                       \
            return false;                                              \
        r->first += n - (r->first + n >= r->size ? r->size : 0);       \
        r->len -= n;                                                   \
        return true;                                                   \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline type_t pfx##_ring_get(pfx##_ring *r, int n) {        \
        return r->tab[pfx##_ring_pos(r, n)];                           \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline type_t *pfx##_ring_get_first_ptr(pfx##_ring *r) {    \
        return r->len > 0 ? r->tab + r->first : NULL;                  \
    }                                                                  \
                                                                       \
    __unused__                                                         \
    static inline type_t *pfx##_ring_get_last_ptr(pfx##_ring *r) {     \
        return r->tab + pfx##_ring_pos(r, r->len - 1);                 \
    }                                                                  \

#define DO_RING(type_t, pfx, wipe) \
    RING_TYPE(type_t, pfx); RING_FUNCTIONS(type_t, pfx, wipe)

#define ring_for_each_ptr(pfx, ptr, r)                             \
    for (typeof((r)->tab) ptr = pfx##_ring_get_first_ptr((r)),     \
         __ptr_last = pfx##_ring_get_last_ptr((r)),                \
         __ptr_end  = (r)->tab + (r)->size;                        \
         ptr != NULL;                                              \
         ({                                                        \
             if (ptr == __ptr_last) {                              \
                 ptr = NULL;                                       \
             } else {                                              \
                 ptr++;                                            \
                 if (ptr == __ptr_end) {                           \
                     ptr = (r)->tab;                               \
                 }                                                 \
             }                                                     \
         }))

#define ring_for_each_entry(pfx, e, r)                             \
    for (typeof(*(r)->tab) *__ptr = pfx##_ring_get_first_ptr((r)), \
         *__ptr_last = pfx##_ring_get_last_ptr((r)),               \
         *__ptr_end  = (r)->tab + (r)->size, e;                    \
         ({                                                        \
             bool e##__res = __ptr != NULL;                        \
             if (e##__res) {                                       \
                 e = *__ptr;                                       \
             }                                                     \
             e##__res;                                             \
         });                                                       \
         ({                                                        \
             if (__ptr == __ptr_last) {                            \
                 __ptr = NULL;                                     \
             } else {                                              \
                 if (++__ptr == __ptr_end) {                       \
                     __ptr = (r)->tab;                             \
                 }                                                 \
             }                                                     \
         }))

#endif
