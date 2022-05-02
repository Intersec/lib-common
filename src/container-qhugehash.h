/***************************************************************************/
/*                                                                         */
/* Copyright 2022 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_CONTAINER_QHUGEHASH_H
#define IS_LIB_COMMON_CONTAINER_QHUGEHASH_H

#include <lib-common/container-qhash.h>

/*
 * QHugeHashes: Real Time huge hash tables.
 *
 * This implements a wrapper around the qh/qm that let user define tables that
 * exceed 1GB.
 *
 * This works by allocating several buckets, the number being defined as a
 * parameter of the type, each bucket will receive the data that match the
 * hashes modulo its position.
 */

/* Hashing functions {{{ */

typedef struct qhhash_t {
    uint64_t len;
} qhhash_t;

static inline uint32_t qhhash_hash_u32(const qhhash_t *qh, uint32_t u32)
{
    return qhash_hash_u32(NULL, u32);
}
static inline uint32_t qhhash_hash_u64(const qhhash_t *qh, uint64_t u64)
{
    return qhash_hash_u64(NULL, u64);
}
static inline uint32_t qhhash_hash_ptr(const qhhash_t *qh, const void *ptr)
{
    return qhash_hash_ptr(NULL, ptr);
}
static inline uint32_t qhhash_str_hash(const qhhash_t *qh, const char *s)
{
    return qhash_str_hash(NULL, s);
}
static inline bool
qhhash_str_equal(const qhhash_t *qhh, const qhash_t *qh,
                 const char *s1, const char *s2)
{
    return qhash_str_equal(NULL, s1, s2);
}
static inline uint32_t qhhash_lstr_hash(const qhhash_t *qh, const lstr_t *ls)
{
    return qhash_lstr_hash(NULL, ls);
}
static inline bool
qhhash_lstr_equal(const qhhash_t *qhh, const qhash_t *qh,
                  const lstr_t *s1, const lstr_t *s2)
{
    return qhash_lstr_equal(NULL, s1, s2);
}
static inline bool
qhhash_ptr_equal(const qhhash_t *qhh, const qhash_t *qh,
                 const void *ptr1, const void *ptr2)
{
    return qhash_ptr_equal(NULL, ptr1, ptr2);
}

/* }}} */
/* base macros for both QHH and QHM {{{ */

#define __QHH_FOREACH_BUCKET(it, qhh, do_it, ...)  do {                      \
        for (int it = 0; it < countof(qhh->buckets); it++) {                 \
            do_it(&qhh->buckets[it].qm.qh, ##__VA_ARGS__);                   \
        }                                                                    \
    } while (0)

#define qhhash_for_each_pos(pos, hh)                                         \
    for (uint64_t __b_##pos = 0;                                             \
         __b_##pos < countof((hh)->buckets);                                 \
         __b_##pos++)                                                        \
        for (uint64_t __##pos##_priv                                         \
             = ((hh)->buckets[__b_##pos].qm.hdr.len                          \
             ? qhash_scan(&(hh)->buckets[__b_##pos].qm.qh, 0)                \
             : UINT32_MAX) | (__b_##pos << 32),                              \
             pos = __##pos##_priv;                                           \
             __QHH_POS(hh, pos) != UINT32_MAX;                               \
             __##pos##_priv = qhash_scan(&(hh)->buckets[__b_##pos].qm.qh,    \
                              __QHH_POS(hh, __##pos##_priv) + 1) |           \
                              (__b_##pos << 32), pos = __##pos##_priv)

#define __QHH_BUCKET_ID(qhh, pos)  ((pos) >> 32)
#define __QHH_BUCKET(qhh, pos)     (&(qhh)->buckets[__QHH_BUCKET_ID(qhh, pos)])
#define __QHH_POS(qhh, pos)        ((pos) & 0xffffffff)

#define __QHH_BASE(pfx, name, hpfx, bucket_count, ckey_t, pkey_t, hf)        \
    typedef struct pfx##_bucket_t {                                          \
        int pos;                                                             \
        hpfx##_t qm;                                                         \
    } pfx##_bucket_t;                                                        \
    typedef struct pfx##_t {                                                 \
        qhhash_t hdr;                                                        \
        pfx##_bucket_t buckets[bucket_count];                                \
    } pfx##_t;                                                               \
                                                                             \
    __unused__                                                               \
    static inline pfx##_t *pfx##_init(pfx##_t *qhh, bool chashes)            \
    {                                                                        \
        p_clear(&qhh->hdr, 1);                                               \
        for (int it = 0; it < countof(qhh->buckets); it++) {                 \
            qhh->buckets[it].pos = it;                                       \
            hpfx##_init(&qhh->buckets[it].qm, chashes, NULL);                \
        }                                                                    \
        return qhh;                                                          \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline size_t pfx##_memory_footprint(const pfx##_t *qhh)          \
    {                                                                        \
        size_t size = 0;                                                     \
                                                                             \
        for (int it = 0; it < countof(qhh->buckets); it++) {                 \
            size += qhash_memory_footprint(&qhh->buckets[it].qm.qh);         \
        }                                                                    \
        return size;                                                         \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline void pfx##_wipe(pfx##_t *qhh)                              \
    {                                                                        \
        __QHH_FOREACH_BUCKET(i, qhh, qhash_wipe);                            \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline void pfx##_clear(pfx##_t *qhh)                             \
    {                                                                        \
        __QHH_FOREACH_BUCKET(i, qhh, qhash_clear);                           \
        qhh->hdr.len = 0;                                                    \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline void pfx##_set_minsize(pfx##_t *qhh, uint64_t minsize)     \
    {                                                                        \
        minsize = DIV_ROUND_UP(minsize, countof(qhh->buckets));              \
        if (minsize > INT32_MAX) {                                           \
            e_fatal("requested minsize too large: %ju", minsize);            \
        }                                                                    \
        for (int it = 0; it < countof(qhh->buckets); it++) {                 \
            qhash_set_minsize(&qhh->buckets[it].qm.qh, minsize);             \
        }                                                                    \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline void pfx##_del_at(pfx##_t *qhh, uint64_t pos)              \
    {                                                                        \
        hpfx##_t *bucket = &__QHH_BUCKET(qhh, pos)->qm;                      \
        uint32_t  old_len = bucket->qh.hdr.len;                              \
                                                                             \
        qhash_del_at(&bucket->qh, __QHH_POS(qhh, pos));                      \
        qhh->hdr.len -= old_len - bucket->qh.hdr.len;                        \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline pkey_t pfx##_key_p(const pfx##_t *qhh, uint64_t pos)       \
    {                                                                        \
        pos &= ~(uint64_t)QHASH_COLLISION;                                   \
        return &__QHH_BUCKET(qhh, pos)->qm.keys[__QHH_POS(qhh, pos)];        \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline uint32_t *pfx##_hash_p(const pfx##_t *qhh, uint64_t pos)   \
    {                                                                        \
        pos &= ~(uint64_t)QHASH_COLLISION;                                   \
        return &__QHH_BUCKET(qhh, pos)->qm.hashes[__QHH_POS(qhh, pos)];      \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline uint64_t pfx##_len(const pfx##_t *qhh)                     \
    {                                                                        \
        return qhh->hdr.len;                                                 \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline uint32_t pfx##_hash(const pfx##_t *qhh, ckey_t key)        \
    {                                                                        \
        return hf(&qhh->hdr, key);                                           \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline uint32_t pfx##__hash(const qhash_t *h, ckey_t key)         \
    {                                                                        \
        const pfx##_bucket_t *bucket = container_of(h, const pfx##_bucket_t, \
                                                    qm.qh);                  \
                                                                             \
        bucket -= bucket->pos;                                               \
        return pfx##_hash(container_of(bucket, const pfx##_t, buckets[0]),   \
                          key);                                              \
    }                                                                        \

#define __QHH_EQUAL(pfx, name, hpfx, ckey_t, ef)                             \
    __unused__                                                               \
    static inline bool pfx##_equal(const pfx##_t *qhh, const qhash_t *qh,    \
                                   ckey_t a, ckey_t b)                       \
    {                                                                        \
        return ef(&qhh->hdr, qh, a, b);                                      \
    }                                                                        \
    __unused__                                                               \
    static inline bool pfx##__equal(const qhash_t *h, ckey_t a, ckey_t b)    \
    {                                                                        \
        const pfx##_bucket_t *bucket = container_of(h, const pfx##_bucket_t, \
                                                    qm.qh);                  \
                                                                             \
        bucket -= bucket->pos;                                               \
        return pfx##_equal(container_of(bucket, const pfx##_t, buckets[0]),  \
                           h, a, b);                                         \
    }

#define __QHH_FIND(pfx, name, hpfx, ckey_t)                                  \
    __unused__                                                               \
    static inline int64_t pfx##_find_h(pfx##_t *qhh, uint32_t h, ckey_t key) \
    {                                                                        \
        uint64_t bid  = h % countof(qhh->buckets);                           \
        uint64_t pos;                                                        \
                                                                             \
        pos = RETHROW(hpfx##_find_int(&qhh->buckets[bid].qm, &h, key));      \
        pos |= (bid << 32);                                                  \
        return pos;                                                          \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int64_t pfx##_find(pfx##_t *qhh, ckey_t key)               \
    {                                                                        \
        return pfx##_find_h(qhh, pfx##_hash(qhh, key), key);                 \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int64_t pfx##_find_safe_h(const pfx##_t *qhh, uint32_t h,  \
                                            ckey_t key)                      \
    {                                                                        \
        uint64_t bid  = h % countof(qhh->buckets);                           \
        uint64_t pos;                                                        \
                                                                             \
        pos = RETHROW(hpfx##_find_safe_int(&qhh->buckets[bid].qm, &h, key)); \
        pos |= (bid << 32);                                                  \
        return pos;                                                          \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int64_t pfx##_find_safe(const pfx##_t *qhh, ckey_t key)    \
    {                                                                        \
        return pfx##_find_safe_h(qhh, pfx##_hash(qhh, key), key);            \
    }

/* }}} */
/* macro for QHH {{{ */

#define __QHH_ADD(pfx, name, hpfx, key_t)                                    \
    __unused__                                                               \
    static inline uint64_t pfx##_put_h(pfx##_t *qhh, uint32_t h,             \
                                       key_t key, uint32_t fl)               \
    {                                                                        \
        uint64_t bid = h % countof(qhh->buckets);                            \
        uint64_t pos;                                                        \
                                                                             \
        pos  = hpfx##_reserve_int(&qhh->buckets[bid].qm, &h, key, fl);       \
        if (!(pos & QHASH_COLLISION)) {                                      \
            qhh->hdr.len++;                                                  \
        }                                                                    \
        pos |= (bid << 32);                                                  \
        return pos;                                                          \
    }                                                                        \
    __unused__                                                               \
    static inline uint64_t pfx##_put(pfx##_t *qhh, key_t key,                \
                                         uint32_t fl)                        \
    {                                                                        \
        return pfx##_put_h(qhh, pfx##_hash(qhh, key), key, fl);              \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int pfx##_add_h(pfx##_t *qhh, uint32_t h, key_t key)       \
    {                                                                        \
        uint64_t bid = h % countof(qhh->buckets);                            \
        int ret = hpfx##_reserve_int(&qhh->buckets[bid].qm, &h, key, 0);     \
                                                                             \
        if (!(ret & QHASH_COLLISION)) {                                      \
            qhh->hdr.len++;                                                  \
        }                                                                    \
        return ret >> 31;                                                    \
    }                                                                        \
    __unused__                                                               \
    static inline int pfx##_add(pfx##_t *qhh, key_t key)                     \
    {                                                                        \
        return pfx##_add_h(qhh, pfx##_hash(qhh, key), key);                  \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int pfx##_replace_h(pfx##_t *qhh, uint32_t h, key_t key)   \
    {                                                                        \
        uint64_t bid = h % countof(qhh->buckets);                            \
        int ret;                                                             \
                                                                             \
        ret = hpfx##_reserve_int(&qhh->buckets[bid].qm, &h, key,             \
                                 QHASH_OVERWRITE);                           \
        if (!(ret & QHASH_COLLISION)) {                                      \
            qhh->hdr.len++;                                                  \
        }                                                                    \
        return ret >> 31;                                                    \
    }                                                                        \
    __unused__                                                               \
    static inline int pfx##_replace(pfx##_t *qhh, key_t key)                 \
    {                                                                        \
        return pfx##_replace_h(qhh, pfx##_hash(qhh, key), key);              \
    }

#define __QHH_IKEY(sfx, pfx, name, key_t, bucket_count)                      \
    __QHH_BASE(pfx, name, qh_u##sfx, bucket_count, key_t const, key_t *,     \
               qhhash_hash_u##sfx);                                          \
    __QHH_FIND(pfx, name, qh_u##sfx, key_t const);                           \
    __QHH_ADD(pfx, name, qh_u##sfx, key_t);                                  \
                                                                             \

#define __QHH_PKEY(pfx, name, qhc_t, bkey_t, ckey_t, key_t, hf, ef,          \
                   bucket_count)                                             \
    static inline uint32_t pfx##__hash(const qhash_t *h, ckey_t *);          \
    static inline bool pfx##__equal(const qhash_t *h, ckey_t *, ckey_t *);   \
    qhc_t(qhh_##name, bkey_t, pfx##__hash, pfx##__equal);                    \
                                                                             \
    __QHH_BASE(pfx, name, qh_qhh_##name, bucket_count, ckey_t *,             \
               key_t **, hf);                                                \
    __QHH_EQUAL(pfx, name, qh_qhh_##name, ckey_t *, ef);                     \
    __QHH_FIND(pfx, name, qh_qhh_##name, ckey_t *);                          \
    __QHH_ADD(pfx, name, qh_qhh_##name, key_t *)

#define __QHH_VKEY(pfx, name, ckey_t, key_t, hf, ef, bucket_count)           \
    static inline uint32_t pfx##__hash(const qhash_t *h, ckey_t *);          \
    static inline bool pfx##__equal(const qhash_t *h, ckey_t *, ckey_t *);   \
    qh_kvec_t(qhh_##name, key_t, pfx##__hash, pfx##__equal);                 \
                                                                             \
    __QHH_BASE(pfx, name, qh_qhh_##name, bucket_count, ckey_t *,             \
               key_t *, hf);                                                 \
    __QHH_EQUAL(pfx, name, qh_qhh_##name, ckey_t *, ef);                     \
    __QHH_FIND(pfx, name, qh_qhh_##name, ckey_t *);                          \
    __QHH_ADD(pfx, name, qh_qhh_##name, ckey_t *)

/* }}} */
/* macros for QHM {{{ */

#define __QHM_ADD(pfx, name, hpfx, key_t, val_t)                             \
    __unused__                                                               \
    static inline val_t *pfx##_value_p(const pfx##_t *qhh, uint64_t pos)     \
    {                                                                        \
        pos &= ~(uint64_t)QHASH_COLLISION;                                   \
        return &__QHH_BUCKET(qhh, pos)->qm.values[__QHH_POS(qhh, pos)];      \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline uint64_t pfx##_put_h(pfx##_t *qhh, uint32_t h,             \
                                       key_t key, val_t v, uint32_t fl)      \
    {                                                                        \
        uint64_t bid = h % countof(qhh->buckets);                            \
        uint64_t pos;                                                        \
                                                                             \
        pos  = hpfx##_reserve_int(&qhh->buckets[bid].qm, &h, key, fl);       \
        if ((fl & QHASH_OVERWRITE) || !(pos & QHASH_COLLISION)) {            \
            qhh->buckets[bid].qm.values[pos & ~QHASH_COLLISION] = v;         \
        }                                                                    \
        if (!(pos & QHASH_COLLISION)) {                                      \
            qhh->hdr.len++;                                                  \
        }                                                                    \
        pos |= (bid << 32);                                                  \
        return pos;                                                          \
    }                                                                        \
    __unused__                                                               \
    static inline uint64_t pfx##_put(pfx##_t *qhh, key_t key, val_t v,       \
                                     uint32_t fl)                            \
    {                                                                        \
        return pfx##_put_h(qhh, pfx##_hash(qhh, key), key, v, fl);           \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline uint64_t pfx##_reserve_h(pfx##_t *qhh, uint32_t h,         \
                                           key_t key, uint32_t fl)           \
    {                                                                        \
        uint64_t bid = h % countof(qhh->buckets);                            \
        uint64_t pos;                                                        \
                                                                             \
        pos  = hpfx##_reserve_int(&qhh->buckets[bid].qm, &h, key, fl);       \
        if (!(pos & QHASH_COLLISION)) {                                      \
            qhh->hdr.len++;                                                  \
        }                                                                    \
        pos |= (bid << 32);                                                  \
        return pos;                                                          \
    }                                                                        \
    __unused__                                                               \
    static inline uint64_t pfx##_reserve(pfx##_t *qhh, key_t key,            \
                                         uint32_t fl)                        \
    {                                                                        \
        return pfx##_reserve_h(qhh, pfx##_hash(qhh, key), key, fl);          \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int pfx##_add_h(pfx##_t *qhh, uint32_t h, key_t key,       \
                                  val_t v)                                   \
    {                                                                        \
        return (int)pfx##_put_h(qhh, h, key, v, 0) >> 31;                    \
    }                                                                        \
    __unused__                                                               \
    static inline int pfx##_add(pfx##_t *qhh, key_t key, val_t v)            \
    {                                                                        \
        return pfx##_add_h(qhh, pfx##_hash(qhh, key), key, v);               \
    }                                                                        \
                                                                             \
    __unused__                                                               \
    static inline int pfx##_replace_h(pfx##_t *qhh, uint32_t h, key_t key,   \
                                      val_t v)                               \
    {                                                                        \
        return (int)pfx##_put_h(qhh, h, key, v, QHASH_OVERWRITE) >> 31;      \
    }                                                                        \
    __unused__                                                               \
    static inline int pfx##_replace(pfx##_t *qhh, key_t key, val_t v)        \
    {                                                                        \
        return pfx##_replace_h(qhh, pfx##_hash(qhh, key), key, v);           \
    }

#define __QHM_IKEY(sfx, pfx, name, key_t, val_t, bucket_count)               \
    qm_k##sfx##_t(qhm_##name, val_t);                                        \
                                                                             \
    __QHH_BASE(pfx, name, qm_qhm_##name, bucket_count, key_t const,          \
               key_t *, qhhash_hash_u##sfx);                                 \
    __QHH_FIND(pfx, name, qm_qhm_##name, key_t const);                       \
    __QHM_ADD(pfx, name, qm_qhm_##name, key_t, val_t)

#define __QHM_PKEY(pfx, name, qmc_t, bkey_t, ckey_t, key_t, val_t, hf, ef,   \
                   bucket_count)                                             \
    static inline uint32_t pfx##__hash(const qhash_t *h, ckey_t *);          \
    static inline bool pfx##__equal(const qhash_t *h, ckey_t *, ckey_t *);   \
    qmc_t(qhm_##name, bkey_t, val_t, pfx##__hash, pfx##__equal);             \
                                                                             \
    __QHH_BASE(pfx, name, qm_qhm_##name, bucket_count, ckey_t *,             \
               key_t **, hf);                                                \
    __QHH_EQUAL(pfx, name, qm_qhm_##name, ckey_t *, ef);                     \
    __QHH_FIND(pfx, name, qm_qhm_##name, ckey_t *);                          \
    __QHM_ADD(pfx, name, qm_qhm_##name, key_t *, val_t)

#define __QHM_VKEY(pfx, name, ckey_t, key_t, val_t, hf, ef, bucket_count)    \
    static inline uint32_t pfx##__hash(const qhash_t *h, ckey_t *);          \
    static inline bool pfx##__equal(const qhash_t *h, ckey_t *, ckey_t *);   \
    qm_kvec_t(qhm_##name, key_t, val_t, pfx##__hash, pfx##__equal);          \
                                                                             \
    __QHH_BASE(pfx, name, qm_qhm_##name, bucket_count, ckey_t *,             \
               key_t *, hf);                                                 \
    __QHH_EQUAL(pfx, name, qm_qhm_##name, ckey_t *, ef);                     \
    __QHH_FIND(pfx, name, qm_qhm_##name, ckey_t *);                          \
    __QHM_ADD(pfx, name, qm_qhm_##name, ckey_t *, val_t)


/* }}} */
/* QHH API {{{ */

#define qhh_k32_t(name, bucket_count)  \
    __QHH_IKEY(32, qhh_##name, name, uint32_t, bucket_count)
#define qhh_k64_t(name, bucket_count)  \
    __QHH_IKEY(64, qhh_##name, name, uint64_t, bucket_count)
#define qhh_kvec_t(name, bucket_count, key_t, hf, ef)  \
    __QHH_VKEY(qhh_##name, name, key_t const, key_t, hf, ef, bucket_count)
#define qhh_kptr_t(name, bucket_count, key_t, hf, ef)  \
    __QHH_PKEY(qhh_##name, name, qh_kptr_t, key_t, key_t const, key_t, hf,   \
               ef, bucket_count)
#define qhh_kptr_ckey_t(name, bucket_count, key_t, hf, ef)  \
    __QHH_PKEY(qhh_##name, name, qh_kptr_ckey_t, key_t, key_t const,         \
               key_t const, hf, ef, bucket_count)

#define qhh_t(name)     qhh_##name##_t
#define qhh_qh_t(name)  qh_t(qhh_##name)
#define qhh_fn(name, fname)                 qhh_##name##_##fname

#define qhh_key_p(name, qhh, pos)                                            \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhh_##name##_key_p(qhh, pos);                                        \
    })
#define qhh_hash_p(name, qhh, pos)                                           \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhh_##name##_hash_p(qhh, pos);                                       \
    })

#define qhh_init(name, qhh, chashes)        qhh_##name##_init(qhh, chashes)
#define qhh_len(name, qhh)                  qhh_##name##_len(qhh)
#define qhh_memory_footprint(name, qhh)     qhh_##name##_memory_footprint(qhh)
#define qhh_hash(name, qhh, key)            qhh_##name##_hash(qhh, key)
#define qhh_set_minsize(name, qhh, sz)      qhh_##name##_set_minsize(qhh, sz)
#define qhh_clear(name, qhh)                qhh_##name##_clear(qhh)
#define qhh_wipe(name, qhh)                 qhh_##name##_wipe(qhh)
#define qhh_find(name, qhh, key)            qhh_##name##_find(qhh, key)
#define qhh_find_h(name, qhh, h, key)       qhh_##name##_find_h(qhh, h, key)
#define qhh_find_safe(name, qhh, key)       qhh_##name##_find_safe(qhh, key)
#define qhh_find_safe_h(name, qhh, h, key)  qhh_##name##_find_safe_h(qhh, h, key)
#define qhh_put(name, qhh, key, fl)         qhh_##name##_put(qhh, key, fl)
#define qhh_put_h(name, qhh, h, key, fl)    qhh_##name##_put_h(qhh, h, key, fl)
#define __qhh_put(name, qhh, key, fl)       qhh_put(name, (qhh), (key), (fl))
#define __qhh_put_h(name, qhh, h, key, fl)  qhh_put_h(name, (qhh), (h), (key), (fl))
#define qhh_add(name, qhh, key)             qhh_##name##_add(qhh, key)
#define qhh_add_h(name, qhh, h, key)        qhh_##name##_add_h(qhh, h, key)
#define qhh_replace(name, qhh, key)         qhh_##name##_replace(qhh, key)
#define qhh_replace_h(name, qhh, h, key)    qhh_##name##_replace_h(qhh, h, key)

#define qhh_del_at(name, qhh, pos)                                           \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhh_##name##_del_at(qhh, pos);                                       \
    })
#define qhh_del_key(name, qhh, key)  \
    ({ int64_t __pos = qhh_find(name, qhh, key);                             \
       if (likely(__pos >= 0)) qhh_del_at(name, qhh, __pos);                 \
       __pos; })
#define qhh_del_key_h(name, qhh, h, key)  \
    ({ int64_t __pos = qhh_find_h(name, qhh, h, key);                        \
       if (likely(__pos >= 0)) qhh_del_at(name, qhh, __pos);                 \
       __pos; })
#define qhh_del_key_safe(name, qhh, key)  \
    ({ int64_t __pos = qhh_find_safe(name, qhh, key);                        \
       if (likely(__pos >= 0)) qhh_del_at(name, qhh, __pos);                 \
       __pos; })
#define qhh_del_key_safe_h(name, qhh, h, key)  \
    ({ int64_t __pos = qhh_find_safe_h(name, qhh, h, key);                   \
       if (likely(__pos >= 0)) qhh_del_at(name, qhh, __pos);                 \
       __pos; })

#ifdef __cplusplus
# define qhh_for_each_pos(name, pos, hh)  qhhash_for_each_pos(pos, hh)
#else
# define qhh_for_each_pos(name, pos, hh)                                     \
    STATIC_ASSERT(__builtin_types_compatible_p(typeof(hh), qhh_t(name) *)    \
                ||  __builtin_types_compatible_p(typeof(hh),                 \
                                                 const qhh_t(name) *));      \
    qhhash_for_each_pos(pos, hh)
#endif

#define qhh_deep_clear(name, h, wipe)                                        \
    do {                                                                     \
        qhh_t(name) *__h = (h);                                              \
        qhh_for_each_pos(name, __pos, __h) {                                 \
            wipe(qhh_key_p(name, __h, __pos));                               \
        }                                                                    \
        qhh_clear(name, __h);                                                \
    } while (0)

#define qhh_deep_wipe(name, h, wipe)                                         \
    do {                                                                     \
        qhh_t(name) *__h = (h);                                              \
        qhh_for_each_pos(name, __pos, __h) {                                 \
            wipe(qhh_key_p(name, __h, __pos));                               \
        }                                                                    \
        qhh_wipe(name, __h);                                                 \
    } while (0)

/* }}} */
/* QHM API {{{ */

#define qhm_k32_t(name, bucket_count, val_t)  \
    __QHM_IKEY(32, qhm_##name, name, uint32_t, val_t, bucket_count)
#define qhm_k64_t(name, bucket_count, val_t)  \
    __QHM_IKEY(64, qhm_##name, name, uint64_t, val_t, bucket_count)
#define qhm_kvec_t(name, bucket_count, key_t, val_t, hf, ef)  \
    __QHM_VKEY(qhm_##name, name, key_t const, key_t, val_t, hf, ef, bucket_count)
#define qhm_kptr_t(name, bucket_count, key_t, val_t, hf, ef)  \
    __QHM_PKEY(qhm_##name, name, qm_kptr_t, key_t, key_t const, key_t, val_t,\
               hf, ef, bucket_count)
#define qhm_kptr_ckey_t(name, bucket_count, key_t, val_t, hf, ef)            \
    __QHM_PKEY(qhm_##name, name, qm_kptr_ckey_t, key_t, key_t const,         \
               key_t const, val_t, hf, ef, bucket_count)

#define qhm_t(name)  qhm_##name##_t
#define qhm_qm_t(name)  qm_t(qhm_##name)
#define qhm_fn(name, fname)                 qhm_##name##_##fname

#define qhm_key_p(name, qhm, pos)                                            \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhm_##name##_key_p(qhm, pos);                                        \
    })
#define qhm_hash_p(name, qhm, pos)                                           \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhm_##name##_hash_p(qhm, pos);                                       \
    })
#define qhm_value_p(name, qhm, pos)                                          \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhm_##name##_value_p(qhm, pos);                                      \
    })

#define qhm_init(name, qhm, chashes)        qhm_##name##_init(qhm, chashes)
#define qhm_len(name, qhm)                  qhm_##name##_len(qhm)
#define qhm_memory_footprint(name, qhm)     qhm_##name##_memory_footprint(qhm)
#define qhm_hash(name, qhm, key)            qhm_##name##_hash(qhm, key)
#define qhm_set_minsize(name, qhm, sz)      qhm_##name##_set_minsize(qhm, sz)
#define qhm_clear(name, qhm)                qhm_##name##_clear(qhm)
#define qhm_wipe(name, qhm)                 qhm_##name##_wipe(qhm)
#define qhm_find(name, qhm, key)            qhm_##name##_find(qhm, key)
#define qhm_find_h(name, qhm, h, key)       qhm_##name##_find_h(qhm, h, key)
#define qhm_find_safe(name, qhm, key)       qhm_##name##_find_safe(qhm, key)
#define qhm_find_safe_h(name, qhm, h, key)  qhm_##name##_find_safe_h(qhm, h, key)
#define qhm_put(name, qhm, key, v, fl)       qhm_##name##_put(qhm, key, v, fl)
#define qhm_put_h(name, qhm, h, key, v, fl)  qhm_##name##_put_h(qhm, h, key, v, fl)
#define __qhm_put(name, qhm, key, v, fl)       qhm_put(name, (qhm), (key), (v), (fl))
#define __qhm_put_h(name, qhm, h, key, v, fl)  qhm_put_h(name, (qhm), (h), (key), (v), (fl))
#define qhm_add(name, qhm, key, v)          qhm_##name##_add(qhm, key, v)
#define qhm_add_h(name, qhm, h, key, v)     qhm_##name##_add_h(qhm, h, key, v)
#define qhm_replace(name, qhm, key, v)      qhm_##name##_replace(qhm, key, v)
#define qhm_replace_h(name, qhm, h, key, v) qhm_##name##_replace_h(qhm, h, key, v)

#define qhm_del_at(name, qhm, pos)                                           \
    ({                                                                       \
        STATIC_ASSERT(sizeof(pos) == sizeof(uint64_t));                      \
        qhm_##name##_del_at(qhm, pos);                                       \
    })
#define qhm_del_key(name, qhm, key)  \
    ({ int64_t __pos = qhm_find(name, qhm, key);                             \
       if (likely(__pos >= 0)) qhm_del_at(name, qhm, __pos);                 \
       __pos; })
#define qhm_del_key_h(name, qhm, h, key)  \
    ({ int64_t __pos = qhm_find_h(name, qhm, h, key);                        \
       if (likely(__pos >= 0)) qhm_del_at(name, qhm, __pos);                 \
       __pos; })
#define qhm_del_key_safe(name, qhm, key)  \
    ({ int64_t __pos = qhm_find_safe(name, qhm, key);                        \
       if (likely(__pos >= 0)) qhm_del_at(name, qhm, __pos);                 \
       __pos; })
#define qhm_del_key_safe_h(name, qhm, h, key)  \
    ({ int64_t __pos = qhm_find_safe_h(name, qhm, h, key);                   \
       if (likely(__pos >= 0)) qhm_del_at(name, qhm, __pos);                 \
       __pos; })

#ifdef __cplusplus
# define qhm_for_each_pos(name, pos, hh)  qhhash_for_each_pos(pos, hh)
#else
# define qhm_for_each_pos(name, pos, hh)                                     \
    STATIC_ASSERT(__builtin_types_compatible_p(typeof(hh), qhm_t(name) *)    \
                ||  __builtin_types_compatible_p(typeof(hh),                 \
                                                 const qhm_t(name) *));      \
    qhhash_for_each_pos(pos, hh)
#endif

#define qhm_deep_clear(name, h, kwipe, vwipe)                                \
    do {                                                                     \
        qhm_t(name) *__h = (h);                                              \
        qhm_for_each_pos(name, __pos, __h) {                                 \
            kwipe(qhm_key_p(name, __h, __pos));                              \
            vwipe(qhm_value_p(name, __h, __pos));                            \
        }                                                                    \
        qhm_clear(name, __h);                                                \
    } while (0)

#define qhm_deep_wipe(name, h, kwipe, vwipe)                                 \
    do {                                                                     \
        qhm_t(name) *__h = (h);                                              \
        qhm_for_each_pos(name, __pos, __h) {                                 \
            kwipe(qhm_key_p(name, __h, __pos));                              \
            vwipe(qhm_value_p(name, __h, __pos));                            \
        }                                                                    \
        qhm_wipe(name, __h);                                                 \
    } while (0)

#define qhm_reserve(name, qhm, key, fl)         \
    qhm_##name##_reserve(qhm, key, fl)
#define qhm_reserve_h(name, qhm, h, key, fl)    \
    qhm_##name##_reserve_h(qhm, h, key, fl)

/* }}} */
#endif
