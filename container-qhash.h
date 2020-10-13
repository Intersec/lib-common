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

#ifndef IS_LIB_COMMON_CONTAINER_QHASH_H
#define IS_LIB_COMMON_CONTAINER_QHASH_H

#include "hash.h"

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

/*
 * QHashes: Real Time hash tables
 *
 *   The trick is during resize. When we resize, we keep the old qhash_hdr_t
 *   around. the keys/values arrays are shared during a resize between the old
 *   and new values, in place.
 *
 *   When we look for a given value, there are two functions:
 *
 *   qhash_get_safe::
 *       which doesn't modify the hash table, but has to look up in both the
 *       old and new view of the values to be sure the value doesn't exist.
 *
 *   qhash_get::
 *       this function preemptively moves all the collision chain that
 *       correspond to the searched key to the new hashtable, plus makes the
 *       move progress if one is in progress.
 *
 *       This function must not be used in a qhash enumeration.
 *
 *
 *   To reserve a new slot, one must use __qhash_put or a wrapper. __qhash_put
 *   returns the position where the key lives in the 31 least significant bits
 *   of the result. The most significant bit is used to notify that there is a
 *   value in that slot already.
 *
 *
 *   When we insert an element, it always goes in the "new" view of the
 *   hashtable. But the slot it has to be put may be occupied by the "old"
 *   view of the hashtable. Should that happen, we reinsert the value that
 *   lives at the offending place (note that the value may actually never
 *   move, but it doesn't really matter). Such a move may trigger more moves,
 *   but we assume that collision chains are usually short due to our double
 *   hashing.
 *
 */

#define QHASH_COLLISION     (1U << 31)
#define QHASH_OVERWRITE     (1U <<  0)

/*
 * len holds:
 *   - the number of elements in the hash when accessed through qh->hdr.len
 *   - the maximum position at which the old view still has elements through
 *     qh->old->len.
 */
typedef struct qhash_hdr_t {
    size_t     * nonnull bits;
    uint32_t    len;
    uint32_t    size;
    mem_pool_t * nullable mp;
} qhash_hdr_t;

#define STRUCT_QHASH_T(key_t, val_t)                                         \
    struct {                                                                 \
        qhash_hdr_t  hdr;                                                    \
        qhash_hdr_t * nullable old;                                          \
        key_t       * nullable keys;                                         \
        val_t       * nullable values;                                       \
        uint32_t    * nullable hashes;                                       \
        uint32_t     ghosts;                                                 \
        uint8_t      h_size;                                                 \
        uint8_t      k_size;                                                 \
        uint16_t     v_size;                                                 \
        uint32_t     minsize;                                                \
    }

/* uint8_t allow us to use pointer arith on ->{values,vec} */
typedef STRUCT_QHASH_T(uint8_t, uint8_t) qhash_t;

typedef uint32_t (qhash_khash_f)(const qhash_t * nullable,
                                 const void * nullable);
typedef bool     (qhash_kequ_f)(const qhash_t * nullable,
                                const void * nullable, const void * nullable);


/****************************************************************************/
/* templatization and module helpers                                        */
/****************************************************************************/

/* helper functions, module functions {{{ */

uint32_t qhash_scan(const qhash_t * nonnull qh, uint32_t pos)
    __leaf;
void qhash_init(qhash_t * nonnull qh, uint16_t k_size, uint16_t v_size, bool doh,
                mem_pool_t * nullable mp)
    __leaf;
void qhash_clear(qhash_t * nonnull qh)
    __leaf;
void qhash_set_minsize(qhash_t * nonnull qh, uint32_t minsize)
    __leaf;
void qhash_unseal(qhash_t * nonnull qh)
    __leaf;
void qhash_wipe(qhash_t * nonnull qh)
    __leaf;

static inline void qhash_slot_inv_flags(size_t * nonnull bits, uint32_t pos)
{
    size_t off = (2 * pos) % bitsizeof(size_t);

    bits[2 * pos / bitsizeof(size_t)] ^= (size_t)3 << off;
}
static inline size_t qhash_slot_get_flags(const size_t * nonnull bits,
                                          uint32_t pos)
{
    size_t off = (2 * pos) % bitsizeof(size_t);
    return (bits[2 * pos / bitsizeof(size_t)] >> off) & (size_t)3;
}
static inline size_t qhash_slot_is_set(const qhash_hdr_t * nonnull hdr,
                                       uint32_t pos)
{
    if (unlikely(pos >= hdr->size)) {
        return 0;
    }
    return TST_BIT(hdr->bits, 2 * pos);
}

static inline void qhash_del_at(qhash_t * nonnull qh, uint32_t pos)
{
    qhash_hdr_t *hdr = &qh->hdr;
    qhash_hdr_t *old = qh->old;

#ifndef NDEBUG
    e_assert(panic, qh->ghosts != UINT32_MAX,
             "delete operation performed on a sealed hash table");
#endif

    if (likely(qhash_slot_is_set(hdr, pos))) {
        qhash_slot_inv_flags(hdr->bits, pos);
        hdr->len--;
        qh->ghosts++;
    } else
    if (unlikely(old != NULL) && qhash_slot_is_set(old, pos))
    {
        qhash_slot_inv_flags(old->bits, pos);
        hdr->len--;
    }
}

static inline uint32_t qhash_hash_u32(const qhash_t * nullable qh, uint32_t u32)
{
    return u32;
}
static inline uint32_t qhash_hash_u64(const qhash_t * nullable qh, uint64_t u64)
{
    return u64_hash32(u64);
}

static inline uint32_t qhash_hash_ptr(const qhash_t * nullable qh,
                                      const void * nullable ptr)
{
    if (sizeof(void *) == 4)
        return (uintptr_t)ptr;
    return u64_hash32((uintptr_t)ptr);
}

#ifdef __cplusplus
# define __qhash_check_type(_htype, _h)  (void)0
#else
# define __qhash_check_type(_htype, _h)                                      \
    (void)({                                                                 \
        STATIC_ASSERT(__builtin_types_compatible_p(typeof(_h), _htype *)     \
                  ||  __builtin_types_compatible_p(typeof(_h),               \
                                                   const _htype *));         \
        0;                                                                   \
    })
#endif

#define __qhash_for_each(_htype, _pos, _h, _doit)                            \
    for (uint32_t __##_pos##_priv = (                                        \
            __qhash_check_type(_htype, (_h)),                                \
            (_h)->qh.hdr.len ? qhash_scan(&(_h)->qh, 0) : UINT32_MAX         \
         ),                                                                  \
         _pos = __##_pos##_priv;                                             \
         __##_pos##_priv != UINT32_MAX && (_doit, true);                     \
         __##_pos##_priv = qhash_scan(&(_h)->qh, __##_pos##_priv + 1),       \
         _pos = __##_pos##_priv)

#define __qhash_for_each_pos(_htype, _pos, _h)                               \
    __qhash_for_each(_htype, _pos, (_h), (void)0)

#define __qhash_for_each_it_guard(_kinds, _it, _h, _opt_value_of_op)         \
    for (typeof((_h)->_kinds[0]) _opt_value_of_op _it,                       \
         *__##_it##_guard = (void *)-1;                                      \
         __##_it##_guard != NULL; __##_it##_guard = NULL)

#define __qhash_for_each_single_it(_htype, _kinds, _it, _h, _opt_addr_of_op, \
                                  _opt_value_of_op)                          \
    __qhash_for_each_it_guard(_kinds, _it, (_h), _opt_value_of_op)           \
        __qhash_for_each(_htype, __##_it##_pos, (_h),                        \
                         _it = _opt_addr_of_op (_h)->_kinds[__##_it##_pos])

#define __qhash_for_each_key(_htype, _key, _h, _opt_addr_of_op,              \
                             _opt_value_of_op)                               \
    __qhash_for_each_single_it(_htype, keys, _key, (_h), _opt_addr_of_op,    \
                               _opt_value_of_op)

#define __qhash_for_each_value(_htype, _value, _h, _opt_addr_of_op,          \
                               _opt_value_of_op)                             \
    __qhash_for_each_single_it(_htype, values, _value, (_h), _opt_addr_of_op,\
                               _opt_value_of_op)

#define __qhash_for_each_key_value(_htype, _key, _value, _h,                 \
                                   _key_opt_addr_of_op,                      \
                                   _key_opt_value_of_op,                     \
                                   _value_opt_addr_of_op,                    \
                                   _value_opt_value_of_op)                   \
    __qhash_for_each_it_guard(keys, _key, (_h), _key_opt_value_of_op)        \
        __qhash_for_each_it_guard(values, _value, (_h),                      \
                                  _value_opt_value_of_op)                    \
            __qhash_for_each(_htype, __##_key##_##_value##_pos, (_h),        \
                             (                                               \
                                 _key = _key_opt_addr_of_op                  \
                                    (_h)->keys[__##_key##_##_value##_pos],   \
                                 _value = _value_opt_addr_of_op              \
                                    (_h)->values[__##_key##_##_value##_pos]  \
                             ))

int32_t  qhash_safe_get32(const qhash_t * nonnull qh, uint32_t h, uint32_t k)
    __leaf;
int32_t  qhash_get32(qhash_t * nonnull qh, uint32_t h, uint32_t k)
    __leaf;
uint32_t __qhash_put32(qhash_t * nonnull qh, uint32_t h, uint32_t k,
                       uint32_t flags)
    __leaf;
void qhash_seal32(qhash_t * nonnull qh);

int32_t  qhash_safe_get64(const qhash_t * nonnull qh, uint32_t h, uint64_t k)
    __leaf;
int32_t  qhash_get64(qhash_t * nonnull qh, uint32_t h, uint64_t k)
    __leaf;
uint32_t __qhash_put64(qhash_t * nonnull qh, uint32_t h, uint64_t k,
                       uint32_t flags)
    __leaf;
void qhash_seal64(qhash_t * nonnull qh);

int32_t  qhash_safe_get_ptr(const qhash_t * nonnull qh, uint32_t h,
                            const void * nullable k,
                            qhash_khash_f * nonnull hf,
                            qhash_kequ_f * nonnull equ);
int32_t  qhash_get_ptr(qhash_t * nonnull qh, uint32_t h,
                       const void * nullable k,
                       qhash_khash_f * nonnull hf,
                       qhash_kequ_f * nonnull equ);
uint32_t __qhash_put_ptr(qhash_t * nonnull qh, uint32_t h,
                         const void * nullable k,
                         uint32_t flags, qhash_khash_f * nonnull hf,
                         qhash_kequ_f * nonnull equ);
void qhash_seal_ptr(qhash_t * nonnull qh, qhash_khash_f * nonnull hf,
                    qhash_kequ_f * nonnull equ);

int32_t  qhash_safe_get_vec(const qhash_t * nonnull qh, uint32_t h,
                            const void * nullable k,
                            qhash_khash_f * nonnull hf,
                            qhash_kequ_f * nonnull equ);
int32_t  qhash_get_vec(qhash_t * nonnull qh, uint32_t h,
                       const void * nullable k,
                       qhash_khash_f * nonnull hf,
                       qhash_kequ_f * nonnull equ);
uint32_t __qhash_put_vec(qhash_t * nonnull qh, uint32_t h,
                         const void * nullable k, uint32_t flags,
                         qhash_khash_f * nonnull hf,
                         qhash_kequ_f * nonnull equ);
void qhash_seal_vec(qhash_t * nonnull qh, qhash_khash_f * nonnull hf,
                    qhash_kequ_f * nonnull equ);
size_t qhash_memory_footprint(const qhash_t * nonnull qh);

/* }}} */
/*----- base macros to define QH's and QM's -{{{-*/

#define CASTK_ID(key)  (key)
#define CASTK_UPTR(key)  ((uintptr_t)(key))

#define __QH_BASE(sfx, pfx, name, ckey_t, key_t, val_t, _v_size, hashK, castK)\
    typedef union pfx##_t {                                                  \
        qhash_t qh;                                                          \
        STRUCT_QHASH_T(key_t, val_t);                                        \
    } pfx##_t;                                                               \
                                                                             \
    __unused__                                                               \
    static inline void pfx##_init(pfx##_t * nonnull qh, bool chahes,         \
                                  mem_pool_t * nullable mp)                  \
    {                                                                        \
        STATIC_ASSERT(sizeof(key_t) < 256);                                  \
        qhash_init(&qh->qh, sizeof(key_t), _v_size, chahes, mp);             \
    }                                                                        \
    __unused__                                                               \
    static inline uint32_t pfx##_hash(const pfx##_t * nonnull qh, ckey_t key)\
    {                                                                        \
        return hashK(&qh->qh, castK(key));                                   \
    }

#define __QH_FIND(sfx, pfx, name, ckey_t, key_t, hashK, castK)               \
    __unused__                                                               \
    static inline int32_t                                                    \
    pfx##_find_int(pfx##_t * nonnull qh, const uint32_t * nullable ph,       \
                   ckey_t key)                                               \
    {                                                                        \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        return qhash_get##sfx(&qh->qh, h, castK(key));                       \
    }                                                                        \
    __unused__                                                               \
    static inline int32_t                                                    \
    pfx##_find_safe_int(const pfx##_t * nonnull qh,                          \
                        const uint32_t * nullable ph, ckey_t key)            \
    {                                                                        \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        return qhash_safe_get##sfx(&qh->qh, h, castK(key));                  \
    }                                                                        \
    __unused__                                                               \
    static inline void pfx##_seal(pfx##_t * nonnull qh)                      \
    {                                                                        \
        return qhash_seal##sfx(&qh->qh);                                     \
    }

#define __QH_FIND2(sfx, pfx, name, ckey_t, key_t, hashK, castK, iseqK)       \
    __unused__                                                               \
    static inline int32_t                                                    \
    pfx##_find_int(pfx##_t * nonnull qh, const uint32_t * nullable ph,       \
                   ckey_t key)                                               \
    {                                                                        \
        uint32_t (*hf)(const qhash_t *, ckey_t) = &hashK;                    \
        bool     (*ef)(const qhash_t *, ckey_t, ckey_t) = &iseqK;            \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        return qhash_get##sfx(&qh->qh, h, castK(key),                        \
                              (qhash_khash_f *)hf, (qhash_kequ_f *)ef);      \
    }                                                                        \
    __unused__                                                               \
    static inline int32_t                                                    \
    pfx##_find_safe_int(const pfx##_t * nonnull qh,                          \
                        const uint32_t * nullable ph, ckey_t key)            \
    {                                                                        \
        uint32_t (*hf)(const qhash_t *, ckey_t) = &hashK;                    \
        bool     (*ef)(const qhash_t *, ckey_t, ckey_t) = &iseqK;            \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        return qhash_safe_get##sfx(&qh->qh, h, castK(key),                   \
                                   (qhash_khash_f *)hf, (qhash_kequ_f *)ef); \
    }                                                                        \
    __unused__                                                               \
    static inline void pfx##_seal(pfx##_t * nonnull qh)                      \
    {                                                                        \
        uint32_t (*hf)(const qhash_t *, ckey_t) = &hashK;                    \
        bool     (*ef)(const qhash_t *, ckey_t, ckey_t) = &iseqK;            \
        return qhash_seal##sfx(&qh->qh,                                      \
                               (qhash_khash_f *)hf, (qhash_kequ_f *)ef);     \
    }

#define __QH_IKEY(sfx, pfx, name, key_t, val_t, v_size)                      \
    __QH_BASE(sfx, pfx, name, key_t const, key_t, val_t, v_size,             \
              qhash_hash_u##sfx, CASTK_ID);                                  \
    __QH_FIND(sfx, pfx, name, key_t const, key_t, qhash_hash_u##sfx,         \
              CASTK_ID);                                                     \
                                                                             \
    __unused__                                                               \
    static inline uint32_t                                                   \
    pfx##_reserve_int(pfx##_t * nonnull qh, const uint32_t * nullable ph,    \
                      key_t key, uint32_t fl)                                \
    {                                                                        \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        uint32_t pos = __qhash_put##sfx(&qh->qh, h, key, fl);                \
                                                                             \
        if ((fl & QHASH_OVERWRITE) || !(pos & QHASH_COLLISION)) {            \
            qh->keys[pos & ~QHASH_COLLISION] = key;                          \
        }                                                                    \
        return pos;                                                          \
    }

#define __QH_HPKEY(pfx, name, ckey_t, key_t, val_t, v_size)                  \
    __QH_BASE(64, pfx, name, ckey_t * nullable, key_t * nullable, val_t,     \
              v_size, qhash_hash_u64, CASTK_UPTR);                           \
    __QH_FIND(64, pfx, name, ckey_t * nullable, key_t * nullable,            \
              qhash_hash_u64, CASTK_UPTR);                                   \
                                                                             \
    __unused__                                                               \
    static inline uint32_t                                                   \
    pfx##_reserve_int(pfx##_t * nonnull qh, const uint32_t * nullable ph,    \
                      key_t * nullable key, uint32_t fl)                     \
    {                                                                        \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        uint32_t pos = __qhash_put64(&qh->qh, h, CASTK_UPTR(key), fl);       \
                                                                             \
        if ((fl & QHASH_OVERWRITE) || !(pos & QHASH_COLLISION)) {            \
            qh->keys[pos & ~QHASH_COLLISION] = key;                          \
        }                                                                    \
        return pos;                                                          \
    }

#define __QH_PKEY(pfx, name, ckey_t, key_t, val_t, v_size, hashK, iseqK)     \
    __QH_BASE(_ptr, pfx, name, ckey_t * nullable, key_t * nullable, val_t,   \
              v_size, hashK, CASTK_ID);                                      \
    __QH_FIND2(_ptr, pfx, name, ckey_t * nullable, key_t * nullable, hashK,  \
               CASTK_ID, iseqK);                                             \
                                                                             \
    __unused__                                                               \
    static inline uint32_t                                                   \
    pfx##_reserve_int(pfx##_t * nonnull qh, const uint32_t * nullable ph,    \
                      key_t * nullable key, uint32_t fl)                     \
    {                                                                        \
        uint32_t (*hf)(const qhash_t * nullable, ckey_t * nullable) = &hashK;\
        bool     (*ef)(const qhash_t * nullable, ckey_t * nullable,          \
                       ckey_t * nullable) = &iseqK;                          \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        uint32_t pos = __qhash_put_ptr(&qh->qh, h, key, fl,                  \
                              (qhash_khash_f *)hf, (qhash_kequ_f *)ef);      \
                                                                             \
        if ((fl & QHASH_OVERWRITE) || !(pos & QHASH_COLLISION)) {            \
            qh->keys[pos & ~QHASH_COLLISION] = key;                          \
        }                                                                    \
        return pos;                                                          \
    }

#define __QH_VKEY(pfx, name, ckey_t, key_t, val_t, v_size, hashK, iseqK)     \
    __QH_BASE(_vec, pfx, name, ckey_t * nonnull, key_t, val_t, v_size,       \
              hashK, CASTK_ID);                                              \
    __QH_FIND2(_vec, pfx, name, ckey_t * nonnull, key_t * nonnull, hashK,    \
               CASTK_ID,  iseqK);                                            \
                                                                             \
    __unused__                                                               \
    static inline uint32_t                                                   \
    pfx##_reserve_int(pfx##_t * nonnull qh, const uint32_t * nullable ph,    \
                      ckey_t * nonnull key, uint32_t fl)                     \
    {                                                                        \
        uint32_t (*hf)(const qhash_t * nullable, ckey_t * nonnull) = &hashK; \
        bool     (*ef)(const qhash_t * nullable, ckey_t * nonnull,           \
                       ckey_t * nonnull) = &iseqK;                           \
        uint32_t h = ph ? *ph : pfx##_hash(qh, key);                         \
        uint32_t pos = __qhash_put_vec(&qh->qh, h, key, fl,                  \
                                       (qhash_khash_f *)hf,                  \
                                       (qhash_kequ_f *)ef);                  \
                                                                             \
        if ((fl & QHASH_OVERWRITE) || !(pos & QHASH_COLLISION)) {            \
            qh->keys[pos & ~QHASH_COLLISION] = *key;                         \
        }                                                                    \
        return pos;                                                          \
    }

/* }}} */

/****************************************************************************/
/* Macros to abstract the templating mess                                   */
/****************************************************************************/

/**
 * \fn qm_wipe(name, qh)
 *     Wipes the hash table structure, but not the indexed elements.
 *     You'll have to use qhash_for_each_pos to do that.
 */

#define qh_k32_t(name)                                                       \
    __QH_IKEY(32, qh_##name, name, uint32_t, void, 0)
#define qh_k64_t(name)                                                       \
    __QH_IKEY(64, qh_##name, name, uint64_t, void, 0)
#define qh_kvec_t(name, key_t, hf, ef)                                       \
    __QH_VKEY(qh_##name, name, key_t const, key_t, void, 0, hf, ef)
#define qh_kptr_t(name, key_t, hf, ef)                                       \
    __QH_PKEY(qh_##name, name, key_t const, key_t, void, 0, hf, ef)
#define qh_kptr_ckey_t(name, key_t, hf, ef)                                  \
    __QH_PKEY(qh_##name, name, key_t const, key_t const, void, 0, hf, ef)
#define qh_khptr_t(name, key_t)                                              \
    __QH_HPKEY(qh_##name, name, key_t const, key_t, void, 0)
#define qh_khptr_ckey_t(name, key_t)                                         \
    __QH_HPKEY(qh_##name, name, key_t const, key_t const, void, 0)

#define qm_k32_t(name, val_t)                                                \
    __QH_IKEY(32, qm_##name, name, uint32_t, val_t, sizeof(val_t))
#define qm_k64_t(name, val_t)                                                \
    __QH_IKEY(64, qm_##name, name, uint64_t, val_t, sizeof(val_t))
#define qm_kvec_t(name, key_t, val_t, hf, ef)                                \
    __QH_VKEY(qm_##name, name, key_t const, key_t, val_t, sizeof(val_t), hf, ef)
#define qm_kptr_t(name, key_t, val_t, hf, ef)                                \
    __QH_PKEY(qm_##name, name, key_t const, key_t, val_t, sizeof(val_t), hf, ef)
#define qm_kptr_ckey_t(name, key_t, val_t, hf, ef)                           \
    __QH_PKEY(qm_##name, name, key_t const, key_t const, val_t,              \
              sizeof(val_t), hf, ef)
#define qm_khptr_t(name, key_t, val_t)                                       \
    __QH_HPKEY(qm_##name, name, key_t const, key_t, val_t, sizeof(val_t))
#define qm_khptr_ckey_t(name, key_t, val_t)                                  \
    __QH_HPKEY(qm_##name, name, key_t const, key_t const, val_t, sizeof(val_t))

/** Static QH initializer.
 *
 * \see qh_init
 */
#define QH_INIT(name, var)                      \
    { .qh = {                                   \
        .k_size = sizeof((var).keys[0]),        \
    } }

/** Static QH initializer with hash caching.
 *
 * \see qh_init_cached
 */
#define QH_INIT_CACHED(name, var) \
    { .qh = {                                   \
        .k_size = sizeof((var).keys[0]),        \
        .h_size = true,                         \
    } }

/** Static QM initializer.
 *
 * \see qm_init
 */
#define QM_INIT(name, var)                      \
    { .qh = {                                   \
        .k_size = sizeof((var).keys[0]),        \
        .v_size = sizeof((var).values[0]),      \
    } }

/** Static QM initializer with hash caching.
 *
 * \see qm_init_cached
 */
#define QM_INIT_CACHED(name, var) \
    { .qh = {                                   \
        .k_size = sizeof((var).keys[0]),        \
        .v_size = sizeof((var).values[0]),      \
        .h_size = true,                         \
    } }

#define QH(name, var)  qh_t(name) var = QH_INIT(name, var)
#define QH_CACHED(name, var)  qh_t(name) var = QH_INIT_CACHED(name, var)
#define QM(name, var)  qm_t(name) var = QM_INIT(name, var)
#define QM_CACHED(name, var)  qm_t(name) var = QM_INIT_CACHED(name, var)

/*
 * The difference between the qh_ and qm_ functions is for the `add` and
 * `replace` ones, since maps have to deal with the associated value.
 */

#define qh_t(name)  qh_##name##_t

#define qh_for_each_pos(name, pos, h)                                        \
    __qhash_for_each_pos(qh_t(name), pos, (h))

#define qh_for_each_key(name, key, h)                                        \
    __qhash_for_each_key(qh_t(name), key, (h), , )

/* WARNING: This macro function is a bit dangerous.
 * It will return a pointer on something very volatile, which will
 * be invalidated by the next find/add/delete.
 * So you must never retain the iterator pointer.
 */
#define qh_for_each_key_p(name, key, h)                                      \
    __qhash_for_each_key(qh_t(name), key, (h), &, *)

/** Initialize a Hash-Set.
 *
 * \param[in] name The type of the hash set.
 * \param[in] qh   A pointer to the hash set to initialize.
 *
 */
#define qh_init(name, qh)  qh_##name##_init(qh, false, NULL)

/** Initialize a Hash-Set with hash caching.
 *
 * \param[in] name The type of the hash set.
 * \param[in] qh   A pointer to the hash set to initialize.
 *
 * This variant enables the table cache, in addition to the keys, the hash
 * of those keys. This has two consequences:
 *  - first, in memory usage: 4 bytes is reserved per slot of the of qh (the
 *    number of slot being larger than the number of keys actually inserted in
 *    the qh).
 *  - secondly, in CPU time: the caching of hashes allow both a marginally
 *    faster lookup (the hashes are compared before checking the key equality)
 *    and a much faster resizing procedure (since the hashes does not need to
 *    be recomputed for each key in order to find its new position in the
 *    resized table).
 *
 * As a consequence, the hash caching should be reserved to use cases in which
 * the hash or equality operation is expensive or in case the hash table will
 * get frequently resized. In a general fashion, if you have any doubt, don't
 * use hash caching before identifying a bottleneck in hashing function (you
 * may even try using \ref qh_set_minsize before activating the caching if you
 * trigger too many resizes of you table).
 *
 * Never uses this function with issued from a qh_k32_t or a qh_k64_t.
 */
#define qh_init_cached(name, qh)  qh_##name##_init(qh, true, NULL)

#define mp_qh_init(name, mp, h, sz)                                          \
    ({                                                                       \
        qh_t(name) *_qh = (h);                                               \
        qh_##name##_init(_qh, false, (mp));                                  \
        qhash_set_minsize(&_qh->qh, (sz));                                   \
        _qh;                                                                 \
    })
#define t_qh_init(name, qh, sz)  mp_qh_init(name, t_pool(), (qh), (sz))
#define r_qh_init(name, qh, sz)  mp_qh_init(name, r_pool(), (qh), (sz))

#define mp_qh_new(name, mp, sz)                                              \
    ({                                                                       \
        mem_pool_t *__mp = (mp);                                             \
        mp_qh_init(name, __mp, mp_new_raw(__mp, qh_t(name), 1), (sz));       \
    })
#define qh_new(name, sz)    mp_qh_new(name, NULL, (sz))
#define t_qh_new(name, sz)  mp_qh_new(name, t_pool(), (sz))
#define r_qh_new(name, sz)  mp_qh_new(name, r_pool(), (sz))

#define qh_fn(name, fname)                  qh_##name##_##fname

#define qh_len(name, _qh)                                                    \
    ({  const qh_t(name) *__qh = (_qh);                                      \
        (int32_t)__qh->qh.hdr.len; })
#define qh_memory_footprint(name, _qh)                                       \
    ({  const qh_t(name) *__qh = (_qh);                                      \
        qhash_memory_footprint(&__qh->qh); })
#define qh_hash(name, qh, key)              qh_##name##_hash(qh, key)
#define qh_set_minsize(name, h, sz)         qhash_set_minsize(&(h)->qh, sz)
/** \see qm_seal */
#define qh_seal(name, qh)                   qh_##name##_seal(qh)
#define qh_unseal(name, _qh)                                                 \
    ({  qh_t(name) *__qh = (_qh);                                            \
        qhash_unseal(&__qh->qh); })
#define qh_wipe(name, _qh)                                                   \
    ({  qh_t(name) *__qh = (_qh);                                            \
        qhash_wipe(&__qh->qh); })
#define qh_clear(name, _qh)                                                  \
    ({  qh_t(name) *__qh = (_qh);                                            \
        qhash_clear(&__qh->qh); })
#define qh_find(name, _qh, _key)                                             \
    qh_##name##_find_int((_qh), NULL, (_key))
#define qh_find_h(name, _qh, h, _key)                                        \
    ({  uint32_t __h = (h);                                                  \
        qh_##name##_find_int((_qh), &__h, (_key));                           \
    })
#define qh_find_safe(name, _qh, _key)                                        \
    qh_##name##_find_safe_int((_qh), NULL, (_key))
#define qh_find_safe_h(name, _qh, h, _key)                                   \
    ({  uint32_t __h = (h);                                                  \
        qh_##name##_find_safe_int((_qh), &__h, (_key));                      \
    })
/** \see qm_put */
#define qh_put(name, qh, key, fl)                                            \
    qh_##name##_reserve_int((qh), NULL, (key), (fl))
#define qh_put_h(name, qh, h, key, fl)                                       \
    ({  uint32_t __h = (h);                                                  \
        qh_##name##_reserve_int((qh), &__h, (key), (fl)); })
/** \see qm_add */
#define qh_add(name, qh, key)                                                \
    ({ (int)qh_put(name, (qh), (key), 0) >> 31; })
#define qh_add_h(name, qh, h, key)                                           \
    ({ (int)qh_put_h(name, (qh), (h), (key), 0) >> 31; })
/** \see qm_replace */
#define qh_replace(name, qh, key)                                            \
    ({ (int)qh_put(name, (qh), (key), QHASH_OVERWRITE) >> 31; })
#define qh_replace_h(name, qh, h, key)                                       \
    ({ (int)qh_put_h(name, (qh), (h), (key), QHASH_OVERWRITE) >> 31; })
#define qh_del_at(name, _qh, pos)                                            \
    ({  qh_t(name) *__qh = (_qh);                                            \
        qhash_del_at(&__qh->qh, (pos)); })
#define qh_del_key(name, _qh, key)                                           \
    ({  qh_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qh_find(name, __dk_qh, key);                         \
        if (likely(__pos >= 0)) qh_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qh_del_key_h(name, _qh, h, key)                                      \
    ({  qh_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qh_find_h(name, __dk_qh, h, key);                    \
        if (likely(__pos >= 0)) qh_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qh_del_key_safe(name, _qh, key)                                      \
    ({  qh_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qh_find_safe(name, __dk_qh, key);                    \
        if (likely(__pos >= 0)) qh_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qh_del_key_safe_h(name, _qh, h, key)                                 \
    ({  qh_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qh_find_safe_h(name, __dk_qh, h, key);               \
        if (likely(__pos >= 0)) qh_del_at(name, __dk_qh, __pos);             \
        __pos; })

#define qh_delete(name, h)                                                   \
    do {                                                                     \
        qh_t(name) **__h = (h);                                              \
        if (likely(*__h)) {                                                  \
            qh_wipe(name, *__h);                                             \
            p_delete(__h);                                                   \
        }                                                                    \
    } while (0)

#define qh_deep_clear(name, h, wipe)                                     \
    do {                                                                 \
        qh_t(name) *__h = (h);                                           \
        qh_for_each_pos(name, __pos, __h) {                              \
            wipe(&(__h)->keys[__pos]);                                   \
        }                                                                \
        qh_clear(name, __h);                                             \
    } while (0)

#define qh_deep_wipe(name, h, wipe)                                      \
    do {                                                                 \
        qh_t(name) *__h = (h);                                           \
        qh_for_each_pos(name, __pos, __h) {                              \
            wipe(&(__h)->keys[__pos]);                                   \
        }                                                                \
        qh_wipe(name, __h);                                              \
    } while (0)

#define qh_deep_delete(name, h, wipe)                                        \
    do {                                                                     \
        qh_t(name) **___h = (h);                                             \
        if (likely(*___h)) {                                                 \
            qh_deep_wipe(name, *___h, wipe);                                 \
            p_delete(___h);                                                  \
        }                                                                    \
    } while (0)

#define qh_wipe_at(name, qh, pos, wipe)  \
    do {                                                                 \
        qh_t(name) *__h = (qh);                                          \
                                                                         \
        if (likely((int32_t)pos >= 0)) {                                 \
            wipe(&(__h)->keys[pos]);                                     \
        }                                                                \
    } while (0)
#define qh_deep_del_at(name, qh, pos, wipe)  \
    ({ qh_wipe_at(name, qh, pos, wipe);                                  \
       qh_del_at(name, qh, pos); })
#define qh_deep_del_key(name, qh, key, wipe)  \
    ({ int32_t _pos = qh_del_key(name, qh, key);                         \
       qh_wipe_at(name, qh, _pos, wipe);                                 \
       _pos; })
#define qh_deep_del_key_h(name, qh, h, key, wipe)  \
    ({ int32_t _pos = qh_del_key_h(name, qh, h, key);                    \
       qh_wipe_at(name, qh, _pos, wipe);                                 \
       _pos; })
#define qh_deep_del_key_safe(name, qh, key, wipe)  \
    ({ int32_t _pos = qh_del_key_safe(name, qh, key);                    \
       qh_wipe_at(name, qh, _pos, wipe);                                 \
       _pos; })
#define qh_deep_del_key_safe_h(name, qh, h, key, wipe)  \
    ({ int32_t _pos = qh_del_key_safe_h(name, qh, h, key);               \
       qh_wipe_at(name, qh, _pos, wipe);                                 \
       _pos; })


#define qm_t(name)  qm_##name##_t

#define qm_for_each_pos(name, pos, h)                                        \
    __qhash_for_each_pos(qm_t(name), pos, (h))

/* WARNING: The loop *_p macro functions are a bit dangerous.
 * They will return a pointer on something very volatile, which will
 * be invalidated by the next find/add/delete.
 * So you must never retain the iterator pointers.
 */

#define qm_for_each_key(name, key, h)                                        \
    __qhash_for_each_key(qm_t(name), key, (h), , )

#define qm_for_each_key_p(name, key, h)                                      \
    __qhash_for_each_key(qm_t(name), key, (h), &, *)

#define qm_for_each_value(name, value, h)                                    \
    __qhash_for_each_value(qm_t(name), value, (h), , )

#define qm_for_each_value_p(name, value, h)                                  \
    __qhash_for_each_value(qm_t(name), value, (h), &, *)

#define qm_for_each_key_value(name, key, value, h)                           \
    __qhash_for_each_key_value(qm_t(name), key, value, (h), , , , )

#define qm_for_each_key_p_value(name, key, value, h)                         \
    __qhash_for_each_key_value(qm_t(name), key, value, (h), &, *, , )

#define qm_for_each_key_value_p(name, key, value, h)                         \
    __qhash_for_each_key_value(qm_t(name), key, value, (h), , , &, *)

#define qm_for_each_key_p_value_p(name, key, value, h)                       \
    __qhash_for_each_key_value(qm_t(name), key, value, (h), &, *, &, *)

/** Initialize a hash-map.
 *
 * \param[in] name   The type of the map.
 * \param[in] qh     A pointer to the map to initialize.
 *
 * \note You can also use the static initializer \ref QM_INIT
 */
#define qm_init(name, qh)  qm_##name##_init(qh, false, NULL)

/** Initialize a hash-map with hash caching.
 *
 * \param[in] name   The type of the map.
 * \param[in] qh     A pointer to the map to initialize.
 *
 * A discussion about the hash caching is available in \ref qh_init_cached
 * documentation.
 */
#define qm_init_cached(name, qh)  qm_##name##_init(qh, true, NULL)

#define mp_qm_init(name, mp, h, sz)                                          \
    ({                                                                       \
        qm_t(name) *_qh = (h);                                               \
        qm_##name##_init(_qh, false, (mp));                                  \
        qhash_set_minsize(&_qh->qh, (sz));                                   \
        _qh;                                                                 \
    })
#define t_qm_init(name, qh, sz)  mp_qm_init(name, t_pool(), (qh), (sz))
#define r_qm_init(name, qh, sz)  mp_qm_init(name, r_pool(), (qh), (sz))

#define mp_qm_new(name, mp, sz)                                              \
    ({                                                                       \
        mem_pool_t *__mp = (mp);                                             \
        mp_qm_init(name, __mp, mp_new_raw(__mp, qm_t(name), 1), (sz));       \
    })
#define qm_new(name, sz)    mp_qm_new(name, NULL, (sz))
#define t_qm_new(name, sz)  mp_qm_new(name, t_pool(), (sz))
#define r_qm_new(name, sz)  mp_qm_new(name, r_pool(), (sz))

#define qm_fn(name, fname)                  qm_##name##_##fname

#define qm_len(name, _qh)                                                    \
    ({  const qm_t(name) *__qh = (_qh);                                      \
        (int32_t)__qh->qh.hdr.len; })
#define qm_memory_footprint(name, _qh)                                       \
    ({  const qm_t(name) *__qh = (_qh);                                      \
        qhash_memory_footprint(&__qh->qh); })
#define qm_hash(name, qh, key)              qm_##name##_hash(qh, key)
#define qm_set_minsize(name, h, sz)         qhash_set_minsize(&(h)->qh, sz)

/** Force the compactness of the hash table, complete any unfinished resize
 * operation and forbid further modifications.
 *
 * This is an optional operation which is mainly designed for big hash tables
 * that will stay unmodified for a long time.
 *
 * The main advantage is that it gets rid of the old version of the table if
 * there is a running resize, freeing some memory. Depending on the dataset,
 * this may also reduce the CPU usage.
 */
#define qm_seal(name, qh)                   qm_##name##_seal(qh)
#define qm_unseal(name, _qh)                                                 \
    ({  qm_t(name) *__qh = (_qh);                                            \
        qhash_unseal(&__qh->qh); })

#define qm_wipe(name, _qh)                                                   \
    ({  qm_t(name) *__qh = (_qh);                                            \
        qhash_wipe(&__qh->qh); })
#define qm_clear(name, _qh)                                                  \
    ({  qm_t(name) *__qh = (_qh);                                            \
        qhash_clear(&__qh->qh); })
#define qm_find(name, _qh, _key)                                             \
    qm_##name##_find_int((_qh), NULL, (_key))
#define qm_find_h(name, qh, h, key)                                          \
    ({  uint32_t __h = (h);                                                  \
        qm_##name##_find_int((qh), &__h, (key));                             \
    })
#define qm_find_safe(name, _qh, _key)                                        \
    qm_##name##_find_safe_int((_qh), NULL, (_key))
#define qm_find_safe_h(name, qh, h, key)                                     \
    ({  uint32_t __h = (h);                                                  \
        qm_##name##_find_safe_int((qh), &__h, (key));                        \
    })

#define _qm_get(name, _qh, _qh_modifier, _opt_address_of_operator, _qm_find, \
                ...)                                                         \
    ({  _qh_modifier qm_t(name) *__gqh = (_qh);                              \
        int __ghp_pos = _qm_find(name, __gqh, ##__VA_ARGS__);                \
        assert (__ghp_pos >= 0);                                             \
        _opt_address_of_operator __gqh->values[__ghp_pos];                   \
    })

/** Get the value of the corresponding key in the hash map.
 *
 * It will assert if the key is not found.
 */
#define qm_get(name, _qh, key)                                               \
    _qm_get(name, (_qh), , , qm_find, (key))
#define qm_get_h(name, _qh, h, key)                                          \
    _qm_get(name, (_qh), , , qm_find_h, (h), (key))
#define qm_get_safe(name, _qh, key)                                          \
    _qm_get(name, (_qh), const, , qm_find_safe, (key))
#define qm_get_safe_h(name, _qh, h, key)                                     \
    _qm_get(name, (_qh), const, , qm_find_safe_h, (h), (key))

/** Get a pointer to the value of the corresponding key in the hash map.
 *
 * It will assert if the key is not found.
 *
 * WARNING: unlike the ones above, these macro functions are a bit dangerous.
 * They will return a pointer on something very volatile, which will
 * be invalidated by the next find/add/delete.
 * So you must never retain the returned pointer.
 */
#define qm_get_p(name, _qh, key)                                             \
    _qm_get(name, (_qh), , &, qm_find, (key))
#define qm_get_p_h(name, _qh, h, key)                                        \
    _qm_get(name, (_qh), , &, qm_find_h, (h), (key))
#define qm_get_p_safe(name, _qh, key)                                        \
    _qm_get(name, (_qh), const, &, qm_find_safe, (key))
#define qm_get_p_safe_h(name, _qh, h, key)                                   \
    _qm_get(name, (_qh), const, &, qm_find_safe_h, (h), (key))


#define _qm_get_def(name, _qh, _def, _qh_modifier, _opt_address_of_operator, \
                    _qm_find, ...)                                           \
    ({  _qh_modifier qm_t(name) *__gqh = (_qh);                              \
        typeof(_def) __def_type = (_def);                                    \
        typeof(_opt_address_of_operator __gqh->values[0]) __def = __def_type;\
        int __ghp_pos = _qm_find(name, __gqh, ##__VA_ARGS__);                \
        assert (__def_type == __def && "default value type is incompatible " \
                "with qm value type");                                       \
        __ghp_pos >= 0 ? _opt_address_of_operator __gqh->values[__ghp_pos]   \
                       : __def;                                              \
    })

/** Get the value of the corresponding key in the hash map or the default
 *  value if the key is not found.
 */
#define qm_get_def(name, _qh, key, def)                                      \
    _qm_get_def(name, (_qh), (def), , , qm_find, (key))
#define qm_get_def_h(name, _qh, h, key, def)                                 \
    _qm_get_def(name, (_qh), (def), , , qm_find_h, (h), (key))
#define qm_get_def_safe(name, _qh, key, def)                                 \
    _qm_get_def(name, (_qh), (def), const, , qm_find_safe, (key))
#define qm_get_def_safe_h(name, _qh, h, key, def)                            \
    _qm_get_def(name, (_qh), (def), const, , qm_find_safe_h, (h), (key))


/** Get a pointer to the value of the corresponding key in the hash map or the
 *  default value if the key is not found.
 *
 * These macro functions are useful to do something like this:
 *
 *    int *pval = qm_get_def_p(test, &qm, 42, NULL);
 *
 *    if (!pval) {
 *        return -1;
 *    }
 *    do_something(*pval);
 *
 * WARNING: unlike the ones above, these macro functions are a bit dangerous.
 * They will return a pointer on something very volatile, which will
 * be invalidated by the next find/add/delete.
 * So you must never retain the returned pointer.
 */
#define qm_get_def_p(name, _qh, key, def)                                    \
    _qm_get_def(name, (_qh), (def), , &, qm_find, (key))
#define qm_get_def_p_h(name, _qh, h, key, def)                               \
    _qm_get_def(name, (_qh), (def), , &, qm_find_h, (h), (key))
#define qm_get_def_p_safe(name, _qh, key, def)                               \
    _qm_get_def(name, (_qh), (def), const, &, qm_find_safe, (key))
#define qm_get_def_p_safe_h(name, _qh, h, key, def)                          \
    _qm_get_def(name, (_qh), (def), const, &, qm_find_safe_h, (h), (key))

#define _qm_fetch(name, _qh, key, _v, _defval, _qh_modifier, _qm_find,       \
                  _opt_address_of_operator, _opt_value_of_operator)          \
    ({                                                                       \
        _qh_modifier qm_t(name) *__gqh = (_qh);                              \
        int __ghp_pos = _qm_find(name, __gqh, (key));                        \
        typeof(__gqh->values[0]) _opt_value_of_operator *__v = (_v);         \
        if (__ghp_pos >= 0) {                                                \
            *__v = _opt_address_of_operator __gqh->values[__ghp_pos];        \
        } else {                                                             \
            *__v = (_defval);                                                \
        }                                                                    \
        __ghp_pos;                                                           \
    })

#define qm_fetch(name, _qh, key, _v, _defval)                                \
    _qm_fetch(name, _qh, key, _v, (_defval), , qm_find, , )
#define qm_fetch_safe(name, _qh, key, _v, _defval)                           \
    _qm_fetch(name, _qh, key, _v, (_defval), const, qm_find_safe, , )
#define qm_fetch_p(name, _qh, key, _vp, _defval)                             \
    _qm_fetch(name, _qh, key, _vp, (_defval), , qm_find, &, *)
#define qm_fetch_p_safe(name, _qh, key, _vp, _defval)                        \
    _qm_fetch(name, _qh, key, _vp, (_defval), const, qm_find_safe, &, *)

#define qm_deep_clear(name, h, k_wipe, v_wipe)                           \
    do {                                                                 \
        qm_t(name) *__h = (h);                                           \
        qm_for_each_pos(name, __pos, __h) {                              \
            k_wipe(&(__h)->keys[__pos]);                                 \
            v_wipe(&(__h)->values[__pos]);                               \
        }                                                                \
        qm_clear(name, __h);                                             \
    } while (0)

#define qm_deep_wipe(name, h, k_wipe, v_wipe)                            \
    do {                                                                 \
        qm_t(name) *__h = (h);                                           \
        qm_for_each_pos(name, __pos, __h) {                              \
            k_wipe(&(__h)->keys[__pos]);                                 \
            v_wipe(&(__h)->values[__pos]);                               \
        }                                                                \
        qm_wipe(name, __h);                                              \
    } while (0)

#define qm_delete(name, h)                                                   \
    do {                                                                     \
        qm_t(name) **___h = (h);                                             \
        if (likely(*___h)) {                                                 \
            qm_wipe(name, *___h);                                            \
            p_delete(___h);                                                  \
        }                                                                    \
    } while (0)

#define qm_deep_delete(name, h, k_wipe, v_wipe)                              \
    do {                                                                     \
        qm_t(name) **___h = (h);                                             \
        if (likely(*___h)) {                                                 \
            qm_deep_wipe(name, *___h, k_wipe, v_wipe);                       \
            p_delete(___h);                                                  \
        }                                                                    \
    } while (0)

/** Find-reserve slot to insert {key,v} pair.
 *
 * These functions finds the slot where to insert {\a key, \a v} in the qmap.
 *
 * qm_reserve[_h]():
 *
 * If there is no collision, the slot is reserved and the key slot is filled,
 * the user still have to fill the value slot.
 * If there is a collision, the key slot is overwritten or not, depending on
 * the \a fl argument.
 * In both cases, the value slot is left unchanged and its update is up to the
 * caller.
 *
 * qm_put[_h]():
 *
 * If there is no collision, the slot is reserved and filled with
 * {\a key, \a v}, else the behavior depends upon the \a fl argument.
 *
 * These functions are useful to write efficient code (spare one lookup) in
 * code that could naively be written:
 * <code>
 * if (qm_find(..., qh, key) < 0) {
 *     // prepare 'v'
 *     qm_add(..., qh, key, v);
 * }
 * </code>
 * and can instead be written:
 * <code>
 * pos = qm_put(..., qh, key, v, 0);
 * if (pos & QHASH_COLLISION) {
 *     // fixup qh->{keys,values}[pos ^ QHASH_COLLISION];
 * }
 * </code>
 * or:
 * <code>
 * pos = qm_reserve(..., qh, key, 0);
 * if (pos & QHASH_COLLISION) {
 *     // fixup qh->keys[pos ^ QHASH_COLLISION];
 * }
 * qh->values[pos & ~QHASH_COLLISION] = v;
 * </code>
 *
 * @param name the base name of the qmap
 * @param qh   pointer to the qmap in wich the value shall be inserted
 * @param key  the value of the key
 * @param v    the value associated to the key
 * @param fl
 *   0 or QHASH_OVERWRITE. If QHASH_OVERWRITE is set, \a key and \a v are used
 *   to overwrite the {key,value} stored in the qmap when there is a
 *   collision.
 * @return
 *   the position of the slot is returned in the 31 least significant bits.
 *   The most significant bit is used to tell if there was a collision.
 *   The constant #QHASH_COLLISION is provided to extract and mask this most
 *   significant bit.
 */

#define qm_reserve(name, qh, key, fl)                                        \
    qm_##name##_reserve_int((qh), NULL, (key), (fl))
#define qm_reserve_h(name, qh, h, key, fl)                                   \
    ({  uint32_t __h = (h);                                                  \
        qm_##name##_reserve_int((qh), &__h, (key), (fl)); })

#define qm_put(name, qh, key, v, fl)                                         \
    ({  qm_t(name) *__qmp_qh = (qh);                                         \
        typeof(__qmp_qh->values[0]) __v = (v);                               \
        uint32_t __fl = (fl);                                                \
        uint32_t __qmp_pos = qm_reserve(name, __qmp_qh, (key), __fl);        \
        if ((__fl & QHASH_OVERWRITE) || !(__qmp_pos & QHASH_COLLISION)) {    \
            __qmp_qh->values[__qmp_pos & ~ QHASH_COLLISION] = __v;           \
        }                                                                    \
        __qmp_pos;                                                           \
    })
#define qm_put_h(name, qh, h, key, v, fl)                                    \
    ({  qm_t(name) *__qmp_qh = (qh);                                         \
        typeof(__qmp_qh->values[0]) __v = (v);                               \
        uint32_t __fl = (fl);                                                \
        uint32_t __qmp_pos = qm_reserve_h(name, __qmp_qh, (h), (key), __fl); \
        if ((__fl & QHASH_OVERWRITE) || !(__qmp_pos & QHASH_COLLISION)) {    \
            __qmp_qh->values[__qmp_pos & ~ QHASH_COLLISION] = __v;           \
        }                                                                    \
        __qmp_pos;                                                           \
    })

/** Adds a new key/value pair into the qmap.
 *
 * When key already exists in the qmap, the add function fails.  If you want
 * to insert or overwrite existing values the replace macro shall be used.
 *
 * @param name the base name of the qmap
 * @param qh   pointer to the qmap in wich the value shall be inserted
 * @param key  the value of the key
 * @param v    the value associated to the key
 *
 * @return     0 if inserted in the qmap, -1 if insertion fails.
 */
#define qm_add(name, qh, key, v)                                             \
    ({ (int)qm_put(name, (qh), (key), (v), 0) >> 31; })
#define qm_add_h(name, qh, h, key, v)                                        \
    ({ (int)qm_put_h(name, (qh), (h), (key), (v), 0) >> 31; })

/** Replaces value for a given key the qmap.
 *
 * If the key does not exists in the current qmap, then the pair key/value
 * is created and inserted.
 *
 * @param name the base name of the qmap
 * @param qh   pointer to the qmap in wich the value shall be inserted
 * @param key  the value of the key
 * @param v    the value associated to the key
 *
 * @return     0 if it's an insertion, -1 if it's a replace.
 */
#define qm_replace(name, qh, key, v)                                         \
    ({ (int)qm_put(name, (qh), (key), (v), QHASH_OVERWRITE) >> 31; })
#define qm_replace_h(name, qh, h, key, v)                                    \
    ({ (int)qm_put_h(name, (qh), (h), (key), (v), QHASH_OVERWRITE) >> 31; })
#define qm_del_at(name, _qh, pos)                                            \
    ({  qm_t(name) *__qh = (_qh);                                            \
        qhash_del_at(&__qh->qh, (pos)); })
#define qm_del_key(name, _qh, key)                                           \
    ({  qm_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qm_find(name, __dk_qh, key);                         \
        if (likely(__pos >= 0)) qm_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qm_del_key_h(name, _qh, h, key)                                      \
    ({  qm_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qm_find_h(name, __dk_qh, (h), key);                  \
        if (likely(__pos >= 0)) qm_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qm_del_key_safe(name, _qh, key)                                      \
    ({  qm_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qm_find_safe(name, __dk_qh, key);                    \
        if (likely(__pos >= 0)) qm_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qm_del_key_safe_h(name, _qh, h, key)                                 \
    ({  qm_t(name) *__dk_qh = (_qh);                                         \
        int32_t __pos = qm_find_safe_h(name, __dk_qh, h, key);               \
        if (likely(__pos >= 0)) qm_del_at(name, __dk_qh, __pos);             \
        __pos; })
#define qm_wipe_at(name, qh, pos, k_wipe, v_wipe)  \
    do {                                                                 \
        qm_t(name) *__h = (qh);                                          \
                                                                         \
        if (likely((int32_t)pos >= 0)) {                                 \
            k_wipe(&(__h)->keys[pos]);                                   \
            v_wipe(&(__h)->values[pos]);                                 \
        }                                                                \
    } while (0)
#define qm_deep_del_at(name, qh, pos, k_wipe, v_wipe)  \
    ({ qm_wipe_at(name, qh, pos, k_wipe, v_wipe);                        \
       qm_del_at(name, qh, pos); })
#define qm_deep_del_key(name, qh, key, k_wipe, v_wipe)  \
    ({ int32_t _pos = qm_del_key(name, qh, key);                         \
       qm_wipe_at(name, qh, _pos, k_wipe, v_wipe);                       \
       _pos; })
#define qm_deep_del_key_h(name, qh, h, key, k_wipe, v_wipe)  \
    ({  int32_t _pos = qm_del_key_h(name, qh, h, key);                   \
        qm_wipe_at(name, qh, _pos, k_wipe, v_wipe);                      \
       _pos; })
#define qm_deep_del_key_safe(name, qh, key, k_wipe, v_wipe)  \
    ({ int32_t _pos = qm_del_key_safe(name, qh, key);                    \
       qm_wipe_at(name, qh, _pos, k_wipe, v_wipe);                       \
       _pos; })
#define qm_deep_del_key_safe_h(name, qh, h, key, k_wipe, v_wipe)  \
    ({ int32_t _pos = qm_del_key_safe_h(name, qh, h, key);               \
       qm_wipe_at(name, qh, _pos, k_wipe, v_wipe);                       \
       _pos; })

static inline uint32_t qhash_str_hash(const qhash_t * nullable qh,
                                      const char * nonnull s)
{
    return mem_hash32(s, -1);
}

static inline bool
qhash_str_equal(const qhash_t * nullable qh, const char * nonnull s1,
                const char * nonnull s2)
{
    return strequal(s1, s2);
}

static inline uint32_t qhash_lstr_hash(const qhash_t * nullable qh,
                                       const lstr_t * nonnull ls)
{
    return mem_hash32(ls->s, ls->len);
}

static inline bool
qhash_lstr_equal(const qhash_t * nullable qh, const lstr_t * nonnull s1,
                 const lstr_t * nonnull s2)
{
    return lstr_equal(*s1, *s2);
}

static inline uint32_t
qhash_lstr_ascii_ihash(const qhash_t * nullable qh, const lstr_t * nonnull ls)
{
    return jenkins_hash_ascii_lower(ls->s, ls->len);
}

static inline bool
qhash_lstr_ascii_iequal(const qhash_t * nullable qh,
                        const lstr_t * nonnull s1, const lstr_t * nonnull s2)
{
    return lstr_ascii_iequal(*s1, *s2);
}

static inline bool
qhash_ptr_equal(const qhash_t * nullable qh, const void * nonnull ptr1,
                const void * nonnull ptr2)
{
    return ptr1 == ptr2;
}

qh_k32_t(u32);
qh_k64_t(u64);
qh_kptr_t(str,   char,    qhash_str_hash,  qhash_str_equal);
qh_kvec_t(lstr,  lstr_t,  qhash_lstr_hash, qhash_lstr_equal);
qh_kvec_t(ilstr, lstr_t,  qhash_lstr_ascii_ihash, qhash_lstr_ascii_iequal);
qh_khptr_t(ptr, void);

qh_kptr_ckey_t(cstr, char, qhash_str_hash, qhash_str_equal);
qh_khptr_ckey_t(cptr, void);

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
