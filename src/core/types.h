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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_TYPES_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_TYPES_H

/* Spinlock {{{ */

#define cpu_relax()     asm volatile("rep; nop":::"memory")

typedef int spinlock_t;

#define spin_trylock(ptr)  (!__sync_lock_test_and_set(ptr, 1))
#define spin_lock(ptr)     ({ while (unlikely(!spin_trylock(ptr))) { cpu_relax(); }})
#define spin_unlock(ptr)   __sync_lock_release(ptr)

/* }}} */
/* {{{1 Refcount */

#define REFCNT_NEW(type, pfx)                                                \
    __unused__ static inline __attribute__((malloc))                         \
    type *nonnull pfx##_new(void)                                            \
    {                                                                        \
        type *res = pfx##_init(p_new_raw(type, 1));                          \
        res->refcnt = 1;                                                     \
        return res;                                                          \
    }

#define REFCNT_RETAIN(type, pfx)                                             \
    __unused__ static inline __attr_nonnull__((1))                           \
    type *nonnull pfx##_retain(type *nonnull t)                              \
    {                                                                        \
        if (unlikely(t->refcnt < 1)) {                                       \
            e_panic("memory corruption: dead object revival detected");      \
        }                                                                    \
        t->refcnt++;                                                         \
        return t;                                                            \
    }

#define REFCNT_RELEASE(type, pfx)                                            \
    __unused__ static inline void __attr_nonnull__((1))                      \
    pfx##_release(type *nullable *nonnull tp)                                \
    {                                                                        \
        type * const t = *tp;                                                \
                                                                             \
        if (t) {                                                             \
            if (unlikely(t->refcnt <= 0)) {                                  \
                e_panic("memory corruption: double free detected");          \
            } else                                                           \
            if (!--t->refcnt) {                                              \
                pfx##_wipe(t);                                               \
                assert (likely(t == *tp) && "pointer corruption detected");  \
                p_delete(tp);                                                \
            }                                                                \
        }                                                                    \
    }

#define REFCNT_DELETE(type, pfx)                                             \
    __unused__ static inline void __attr_nonnull__((1))                      \
    pfx##_delete(type *nullable *nonnull tp)                                 \
    {                                                                        \
        if (*tp) {                                                           \
            pfx##_release(tp);                                               \
            *tp = NULL;                                                      \
        }                                                                    \
    }

#define DO_REFCNT(type, pfx)                                                 \
    REFCNT_NEW(type, pfx)                                                    \
    REFCNT_RETAIN(type, pfx)                                                 \
    REFCNT_RELEASE(type, pfx)                                                \
    REFCNT_DELETE(type, pfx)

/* 1}}} */
/* {{{ Optional scalar types */

#define OPT_OF(type_t)     struct { type_t v; bool has_field; }
typedef OPT_OF(int8_t)     opt_i8_t;
typedef OPT_OF(uint8_t)    opt_u8_t;
typedef OPT_OF(int16_t)    opt_i16_t;
typedef OPT_OF(uint16_t)   opt_u16_t;
typedef OPT_OF(int32_t)    opt_i32_t;
typedef OPT_OF(uint32_t)   opt_u32_t;
typedef OPT_OF(int64_t)    opt_i64_t;
typedef OPT_OF(uint64_t)   opt_u64_t;
typedef OPT_OF(int)        opt_enum_t;
typedef OPT_OF(bool)       opt_bool_t;
typedef OPT_OF(double)     opt_double_t;
typedef opt_bool_t         opt__Bool_t;

/** Initialize an optional field. */
#define OPT(val)           { .v = (val), .has_field = true }
/** Initialize an optional field to “absent”. */
#define OPT_NONE           { .has_field = false }
/** Initialize an optional field if `cond` is fulfilled. */
#define OPT_IF(cond, val)  { .has_field = (cond), .v = (cond) ? (val) : 0 }

/** Tell whether the optional field is set or not. */
#define OPT_ISSET(_v)  ((_v).has_field == true)
/** Get the optional field value. */
#define OPT_VAL_P(_v)                                                        \
    ({                                                                       \
        typeof(_v) __p_opt = (_v);                                           \
        assert (OPT_ISSET(*__p_opt));                                        \
        &__p_opt->v;                                                         \
    })
#define OPT_VAL(_v)  ({ typeof(_v) _opt = (_v); *OPT_VAL_P(&(_opt)); })
#define OPT_DEFVAL(_v, _defval)                       \
    ({ typeof(_v) __v = (_v);                         \
       (__v).has_field ? (__v).v : (_defval); })
#define OPT_GET(_v)  \
    ({ typeof(_v) __v = (_v); __v->has_field ? &__v->v : NULL; })

/** Set the optional field value. */
#define OPT_SET(dst, val)  \
    ({ typeof(dst) *_dst = &(dst); _dst->v = (val); _dst->has_field = true; })
/** Clear the optional field value. */
#define OPT_CLR(dst)   (void)((dst).has_field = false)
/** Set the optional field value if `cond` is fulfilled. */
#define OPT_SET_IF(dst, cond, val) \
    ({ if (cond) {                                         \
           OPT_SET(dst, val);                              \
       } else {                                            \
           OPT_CLR(dst);                                   \
       }                                                   \
    })
/** Clear the optional field value if `cond` is fulfilled. */
#define OPT_CLR_IF(dst, cond) \
    do {                                                   \
        if (cond) {                                        \
            OPT_CLR(dst);                                  \
        }                                                  \
    } while (0)

/** Copy `src` in `dst`. */
#define OPT_COPY(dst, src)                                                   \
    do {                                                                     \
        typeof(src) *_src = &(src);                                          \
                                                                             \
        OPT_SET_IF((dst), OPT_ISSET(*_src), OPT_VAL(*_src));                 \
    } while (0)

/** Get whether 2 optional fields are equal are not.
 *
 * Optional fields are equal if:
 * - they are both unset;
 * - or, they are both set and their values are equal.
 * Otherwise optional fields are different.
 */
#define OPT_EQUAL(v, w)                                                      \
    ({                                                                       \
        typeof(v) _v = (v);                                                  \
        typeof(w) _w = (w);                                                  \
                                                                             \
        (OPT_ISSET(_v) == OPT_ISSET(_w)                                      \
         && (!OPT_ISSET(_v) || OPT_VAL(_v) == OPT_VAL(_w)));                 \
    })

/* 1}}} */
/* Data Baton {{{ */

/** Type to pass as a generic context.
 */
typedef union data_t {
    void    * nullable ptr;
    uint32_t u32;
    uint64_t u64;
} data_t;

#define DATA_U32(_u) (data_t){ .u32 = (_u) }
#define DATA_U64(_u) (data_t){ .u64 = (_u) }
#define DATA_PTR(_ptr) (data_t){ .ptr = (_ptr) }

/* }}} */

#ifdef __has_blocks

#define core_cmp_b(pfx)  pfx##_cmp_b

#define CORE_CMP_TYPE(pfx, val_t) __CORE_CMP_TYPE(pfx, val_t const)

#define __CORE_CMP_TYPE(pfx, cval_t)                                         \
    typedef int (BLOCK_CARET core_cmp_b(pfx))(cval_t * nonnull a,            \
                                              cval_t * nonnull b)

#define CORE_CMP_BLOCK(pfx, val_t)                                           \
    CORE_CMP_TYPE(pfx, val_t);                                               \
    extern const core_cmp_b(pfx) nonnull core_##pfx##_cmp

struct lstr_t;

CORE_CMP_BLOCK(i8, int8_t);
CORE_CMP_BLOCK(u8, uint8_t);
CORE_CMP_BLOCK(i16, int16_t);
CORE_CMP_BLOCK(u16, uint16_t);
CORE_CMP_BLOCK(i32, int32_t);
CORE_CMP_BLOCK(u32, uint32_t);
CORE_CMP_BLOCK(i64, int64_t);
CORE_CMP_BLOCK(u64, uint64_t);
CORE_CMP_BLOCK(double, double);
CORE_CMP_BLOCK(lstr, struct lstr_t);
CORE_CMP_BLOCK(str, char * nonnull);
CORE_CMP_BLOCK(cstr, const char * nonnull);

#endif

#endif
