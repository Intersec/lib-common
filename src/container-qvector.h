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

#ifndef IS_LIB_COMMON_CONTAINER_QVECTOR_H
#define IS_LIB_COMMON_CONTAINER_QVECTOR_H

#include <lib-common/core.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#define STRUCT_QVECTOR_T(val_t)                                              \
    struct {                                                                 \
        val_t * nullable tab;                                                \
        mem_pool_t * nullable mp;                                            \
        int len, size;                                                       \
    }

#define STRUCT_QVECTOR_INIT()  { NULL, NULL, 0, 0 }

typedef STRUCT_QVECTOR_T(uint8_t) qvector_t;

#ifdef __has_blocks
typedef int (BLOCK_CARET qvector_cmp_b)(const void * nonnull,
                                        const void * nonnull);
typedef void (BLOCK_CARET qvector_del_b)(void * nonnull);
typedef void (BLOCK_CARET qvector_cpy_b)(void * nonnull, const void * nonnull);
#endif

static inline qvector_t * nonnull
__qvector_init(qvector_t * nonnull vec, void * nullable buf, int blen,
               int bsize, mem_pool_t * nullable mp)
{
    *vec = (qvector_t){
        cast(uint8_t *, buf),
        mp,
        blen,
        bsize,
    };
    return vec;
}

void  qvector_reset(qvector_t * nonnull vec, size_t v_size)
    __leaf;
void  qvector_wipe(qvector_t * nonnull vec, size_t v_size)
    __leaf;
void  __qvector_grow(qvector_t * nonnull , size_t v_size, size_t v_align,
                     int extra)
    __leaf;
void  __qvector_optimize(qvector_t * nonnull, size_t v_size, size_t v_align,
                         size_t size)
    __leaf;
void * nonnull __qvector_splice(qvector_t * nonnull, size_t v_size,
                                size_t v_align, int pos, int rm_len,
                                int inserted_len)
    __leaf;
#ifdef __has_blocks
void __qv_sort32(void * nonnull a, size_t n, qvector_cmp_b nonnull cmp);
void __qv_sort64(void * nonnull a, size_t n, qvector_cmp_b nonnull cmp);
void __qv_sort(void * nonnull a, size_t v_size, size_t n,
               qvector_cmp_b nonnull cmp);

static ALWAYS_INLINE void
__qvector_sort(qvector_t * nonnull vec, size_t v_size,
               qvector_cmp_b nonnull cmp)
{
    if (v_size == 8) {
        __qv_sort64(vec->tab, vec->len, cmp);
    } else
    if (v_size == 4) {
        __qv_sort32(vec->tab, vec->len, cmp);
    } else {
        __qv_sort(vec->tab, v_size, vec->len, cmp);
    }
}
void __qvector_shuffle(qvector_t * nonnull vec, size_t v_size);
void __qvector_diff(const qvector_t * nonnull vec1,
                    const qvector_t * nonnull vec2,
                    qvector_t * nullable add, qvector_t * nullable del,
                    qvector_t * nullable inter, size_t v_size, size_t v_align,
                    qvector_cmp_b nonnull cmp);
int  __qvector_bisect(const qvector_t * nonnull vec, size_t v_size,
                      const void * nonnull elt, bool * nullable found,
                      qvector_cmp_b nonnull cmp);
int __qvector_find(const qvector_t * nonnull vec, size_t v_size,
                   const void * nonnull elt, bool sorted,
                   qvector_cmp_b nonnull cmp);
bool __qvector_contains(const qvector_t * nonnull vec, size_t v_size,
                        const void * nonnull elt,
                        bool sorted, qvector_cmp_b nonnull cmp);
void __qvector_uniq(qvector_t * nonnull vec, size_t v_size,
                    qvector_cmp_b nonnull cmp,
                    qvector_del_b nullable del);
void __qvector_deep_extend(qvector_t * nonnull vec_dst,
                           const qvector_t * nonnull vec_src,
                           size_t v_size, size_t v_align,
                           qvector_cpy_b nonnull cpy_f);
#endif

/** \brief optimize vector for space.
 *
 * \param[in]   vec          the vector to optimize
 * \param[in]   v_size       sizeof(val_t) for this qvector
 * \param[in]   ratio        the ratio of garbage allowed.
 * \param[in]   extra_ratio  the extra size to add when resizing.
 *
 * If there is more than vec->len * (ratio / 100) empty cells, the array is
 * resize to vec->len + vec->len * (extra_ratio / 100).
 *
 * In particular, qvector_optimize(vec, ..., 0, 0) forces the vector
 * allocation to have no waste.
 */
static inline void
qvector_optimize(qvector_t * nonnull vec, size_t v_size, size_t v_align,
                 size_t ratio, size_t extra_ratio)
{
    size_t cur_waste = vec->size - vec->len;

    if (vec->len * ratio < 100 * cur_waste)
        __qvector_optimize(vec, v_size, v_align,
                           vec->len + vec->len * extra_ratio / 100);
}

static inline
void * nonnull qvector_grow(qvector_t * nonnull vec, size_t v_size,
                            size_t v_align, int extra)
{
    ssize_t size = vec->len + extra;

    if (size > vec->size) {
        __qvector_grow(vec, v_size, v_align, extra);
    } else {
        ssize_t cursz = vec->size;

        if (unlikely(cursz * v_size > BUFSIZ && size * 8 < cursz))
            __qvector_optimize(vec, v_size, v_align, p_alloc_nr(size));
    }
    return vec->tab + vec->len * v_size;
}

static inline
void * nonnull qvector_growlen(qvector_t * nonnull vec, size_t v_size,
                               size_t v_align, int extra)
{
    void *res;

    if (vec->len + extra > vec->size)
        __qvector_grow(vec, v_size, v_align, extra);
    res = vec->tab + vec->len * v_size;
    vec->len += extra;
    return res;
}

uint64_t __qvector_grow_get_new_alloc_size(qvector_t *nonnull vec,
                                           size_t v_size, int extra);

static inline void * nonnull
qvector_splice(qvector_t * nonnull vec, size_t v_size, size_t v_align,
               int pos, int rm_len, const void * nullable inserted_values,
               int inserted_len)
{
    void *res;

    assert (pos >= 0 && rm_len >= 0 && inserted_len >= 0);
    assert ((unsigned)pos <= (unsigned)vec->len);
    assert ((unsigned)pos + (unsigned)rm_len <= (unsigned)vec->len);

    if (__builtin_constant_p(inserted_len)) {
        if (inserted_len == 0
        ||  (__builtin_constant_p(rm_len) && rm_len >= inserted_len))
        {
            memmove(vec->tab + v_size * (pos + inserted_len),
                    vec->tab + v_size * (pos + rm_len),
                    v_size * (vec->len - pos - rm_len));
            vec->len += inserted_len - rm_len;
            return vec->tab + v_size * pos;
        }
    }
    if (__builtin_constant_p(rm_len) && rm_len == 0 && pos == vec->len) {
        res = qvector_growlen(vec, v_size, v_align, inserted_len);
    } else {
        res = __qvector_splice(vec, v_size, v_align, pos, rm_len,
                               inserted_len);
    }

    return inserted_values
         ? memcpy(res, inserted_values, inserted_len * v_size)
         : res;
}

#define __QVECTOR_BASE_TYPE(pfx, cval_t, val_t)                             \
    typedef union pfx##_t {                                                 \
        qvector_t qv;                                                       \
        STRUCT_QVECTOR_T(val_t);                                            \
    } pfx##_t;

#define QV_INIT() { .qv = STRUCT_QVECTOR_INIT() }

#ifdef __has_blocks
#define __QVECTOR_BASE_BLOCKS(pfx, cval_t, val_t)                           \
    CORE_CMP_TYPE(pfx, val_t);                                              \
    typedef void (BLOCK_CARET pfx##_del_b)(val_t * nonnull v);              \
    typedef void (BLOCK_CARET pfx##_cpy_b)(val_t * nonnull a,               \
                                           cval_t * nonnull b);             \
                                                                            \
    __unused__                                                              \
    static inline void pfx##_sort(pfx##_t * nonnull vec,                    \
                                  pfx##_cmp_b nonnull cmp)                  \
    {                                                                       \
        __qvector_sort(&vec->qv, sizeof(val_t), (qvector_cmp_b)cmp);        \
    }                                                                       \
    __unused__                                                              \
    static inline void                                                      \
    pfx##_diff(const pfx##_t * nonnull vec1, const pfx##_t * nonnull vec2,  \
               pfx##_t * nullable add, pfx##_t * nullable del,              \
               pfx##_t * nullable inter, pfx##_cmp_b nonnull cmp)           \
    {                                                                       \
        __qvector_diff(&vec1->qv, &vec2->qv, add ? &add->qv : NULL,         \
                       del ? &del->qv : NULL, inter ? &inter->qv : NULL,    \
                       sizeof(val_t), alignof(val_t), (qvector_cmp_b)cmp);  \
    }                                                                       \
    __unused__                                                              \
    static inline                                                           \
    void pfx##_uniq(pfx##_t * nonnull vec, pfx##_cmp_b nonnull cmp,         \
                    pfx##_del_b nullable del)                               \
    {                                                                       \
        __qvector_uniq(&vec->qv, sizeof(val_t), (qvector_cmp_b)cmp,         \
                       (qvector_del_b)del);                                 \
    }                                                                       \
    __unused__                                                              \
    static inline                                                           \
    int pfx##_bisect(const pfx##_t * nonnull vec, cval_t v,                 \
                     bool * nullable found, pfx##_cmp_b nonnull cmp)        \
    {                                                                       \
        return __qvector_bisect(&vec->qv, sizeof(val_t), &v, found,         \
                                (qvector_cmp_b)cmp);                        \
    }                                                                       \
    __unused__                                                              \
    static inline                                                           \
    int pfx##_find(const pfx##_t * nonnull vec, cval_t v, bool sorted,      \
                   pfx##_cmp_b nonnull cmp)                                 \
    {                                                                       \
        return __qvector_find(&vec->qv, sizeof(val_t), &v, sorted,          \
                              (qvector_cmp_b)cmp);                          \
    }                                                                       \
    __unused__                                                              \
    static inline                                                           \
    bool pfx##_contains(const pfx##_t * nonnull vec, cval_t v, bool sorted, \
                        pfx##_cmp_b nonnull cmp)                            \
    {                                                                       \
        return __qvector_contains(&vec->qv, sizeof(val_t), &v, sorted,      \
                                  (qvector_cmp_b)cmp);                      \
    }                                                                       \
    __unused__                                                              \
    static inline                                                           \
    void pfx##_deep_extend(pfx##_t * nonnull vec_dst,                       \
                           pfx##_t * nonnull vec_src,                       \
                           pfx##_cpy_b nonnull cpy_f)                       \
    {                                                                       \
        __qvector_deep_extend(&vec_dst->qv, &vec_src->qv, sizeof(val_t),    \
                              alignof(val_t), (qvector_cpy_b)cpy_f);        \
    }
#else
#define __QVECTOR_BASE_BLOCKS(pfx, cval_t, val_t)
#endif

#define __QVECTOR_BASE_FUNCTIONS(pfx, cval_t, val_t) \
    __QVECTOR_BASE_BLOCKS(pfx, cval_t, val_t)

/** Declare a new vector type.
 *
 * qvector_type_t and qvector_funcs_t allow recursive declaraion of structure
 * types that contain a qvector of itself:
 *
 * \code
 * qvector_type_t(vec_type, struct mytype_t);
 * struct mytype_t {
 *     qv_t(vec_type) children;
 * };
 * qvector_funcs_t(vec_type, struct mytype_t);
 * \endcode
 *
 * For most use cases you should use qvector_t() that declares both the type
 * and the functions.
 */
#define qvector_type_t(n, val_t) \
    __QVECTOR_BASE_TYPE(qv_##n, val_t const, val_t)
#define qvector_funcs_t(n, val_t) \
    __QVECTOR_BASE_FUNCTIONS(qv_##n, val_t const, val_t)

#define qvector_t(n, val_t)                                                  \
    qvector_type_t(n, val_t);                                                \
    qvector_funcs_t(n, val_t);

#define qv_t(n)                             qv_##n##_t
#define __qv_typeof(vec)                    typeof((vec)->tab[0])
#define __qv_sz(vec)                        sizeof((vec)->tab[0])
#define __qv_align(vec)                     alignof((vec)->tab[0])
#define __qv_init(vec, b, bl, bs, mp)                                        \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        __qvector_init(&__vec->qv, (b), (bl), (bs), ipool(mp));              \
        __vec;                                                               \
    })
#define qv_init_static(vec, _tab, _len)                                      \
    ({  size_t __len = (_len);                                               \
        typeof(*(vec)) *__vec = (vec);                                       \
        __qv_typeof(vec) const * const __tab = (_tab);                       \
        __qvector_init(&__vec->qv, (void *)__tab, __len, __len,              \
                       &mem_pool_static);                                    \
        __vec; })
#define qv_init_static_tab(vec, _tab)                                        \
    ({                                                                       \
        typeof(_tab) ___tab = (_tab);                                        \
        qv_init_static((vec), ___tab->tab, ___tab->len);                     \
    })
#define qv_inita(vec, size)                                                  \
    ({  size_t __size = (size), _sz = __size * __qv_sz(vec);                 \
        typeof(*(vec)) *__vec = (vec);                                       \
        __qvector_init(&__vec->qv, alloca(_sz), 0, __size,                   \
                       &mem_pool_static);                                    \
        __vec; })
#define mp_qv_init(mp, vec, size)                                            \
    ({  size_t __size = (size);                                              \
        mem_pool_t *_mp = (mp);                                              \
        typeof(*(vec)) *__vec = (vec);                                       \
        __qvector_init(&__vec->qv, mp_new_raw(_mp, __qv_typeof(vec), __size),\
                       0, __size, _mp);                                      \
        __vec; })

#define t_qv_init(vec, size)  mp_qv_init(t_pool(), (vec), (size))
#define r_qv_init(vec, size)  mp_qv_init(r_pool(), (vec), (size))

#define qv_init(vec)                                                         \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        p_clear(&__vec->qv, 1);                                              \
        __vec;                                                               \
    })

#define qv_clear(vec)                                                        \
    ({  typeof(*(vec)) *__qc_vec = (vec);                                    \
        qvector_reset(&__qc_vec->qv, __qv_sz(vec)); })
#define qv_deep_clear(vec, wipe)                                             \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        tab_for_each_pos_safe(__i, __vec) {                                  \
            wipe(&__vec->tab[__i]);                                          \
        }                                                                    \
        qv_clear(__vec); })

#define qv_wipe(vec)  qvector_wipe(&(vec)->qv, __qv_sz(vec))
#define qv_deep_wipe(vec, wipe)                                              \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        tab_for_each_pos_safe(__i, __vec) {                                  \
            wipe(&__vec->tab[__i]);                                          \
        }                                                                    \
        qv_wipe(__vec); })

#define qv_delete(vec)  do {                                                 \
        typeof(**(vec)) **__pvec = (vec);                                    \
        if (*__pvec) {                                                       \
            qv_wipe(*__pvec);                                                \
            p_delete(__pvec);                                                \
        }                                                                    \
    } while (0)
#define qv_deep_delete(vecp, wipe)                                           \
    ({  typeof(**(vecp)) **__vecp = (vecp);                                  \
        if (likely(*__vecp)) {                                               \
            tab_for_each_pos_safe(__i, *__vecp) {                            \
                wipe(&(*__vecp)->tab[__i]);                                  \
            }                                                                \
            qv_delete(__vecp);                                               \
        } })

#define qv_new(n)  p_new(qv_t(n), 1)

#define mp_qv_new(n, mp, sz)                                                 \
    ({                                                                       \
        mem_pool_t *__mp = (mp);                                             \
        mp_qv_init(__mp, mp_new_raw(__mp, qv_t(n), 1), (sz));                \
    })
#define t_qv_new(n, sz)  mp_qv_new(n, t_pool(), (sz))
#define r_qv_new(n, sz)  mp_qv_new(n, r_pool(), (sz))

#ifdef __has_blocks
/* You must be in a .blk to use qv_sort/qv_deep_extend, because it
 * expects blocks !
 */
#define qv_sort(n)                          qv_##n##_sort
#define qv_cmp_b(n)                         qv_##n##_cmp_b
#define qv_del_b(n)                         qv_##n##_del_b

#define qv_deep_extend(n)                   qv_##n##_deep_extend
#define qv_cpy_b(n)                         qv_##n##_cpy_b
#endif /* #ifdef __has_blocks */
#define qv_qsort(vec, cmp)                                                   \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        int (*__cb)(__qv_typeof(vec) const *, __qv_typeof(vec) const *)      \
            = (cmp);                                                         \
        qsort(__vec->qv.tab, __vec->qv.len, __qv_sz(vec),                    \
              (int (*)(const void *, const void *))__cb);                    \
    })

/** Shuffle a vector using the Fisher-Yates shuffle algorithm (O(n)). */
#define qv_shuffle(vec)                                                      \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        __qvector_shuffle(&__vec->qv, __qv_sz(vec));                         \
    })

#define __qv_splice(vec, pos, rm_len, inserted_len)                          \
    ({ (__qv_typeof(vec) *)__qvector_splice(&(vec)->qv, __qv_sz(vec),        \
                                            __qv_align(vec), (pos),          \
                                            (rm_len), (inserted_len)); })

/** At a given position, remove N elements then insert M extra elements.
 *
 *  \param[in] pos              Position of removal or/and insertion.
 *  \param[in] rm_len           Number of elements to remove (N).
 *  \param[in] inserted_values  Values to set for the inserted elements
 *                              (optional: if null, the inserted elements are
 *                              left uninitialized, the returned pointer can
 *                              be used to initialize them manually).
 *  \param[in] inserted_len     Number of elements to insert (M).
 *
 *  \return Pointer to vec->tab[pos].
 */
#define qv_splice(vec, pos, rm_len, inserted_values, inserted_len)           \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        __qv_typeof(vec) const *__vals = (inserted_values);                  \
        (__qv_typeof(vec) *)qvector_splice(&__vec->qv, __qv_sz(vec),         \
                                           __qv_align(vec), (pos), (rm_len), \
                                           (__vals), (inserted_len)); })
#define qv_optimize(vec, r1, r2)                                             \
    qvector_optimize(&(vec)->qv, __qv_sz(vec), __qv_align(vec), (r1), (r2))

#define qv_grow(vec, extra)                                                  \
    ({ (__qv_typeof(vec) *)qvector_grow(&(vec)->qv, __qv_sz(vec),            \
                                        __qv_align(vec), (extra)); })
#define qv_growlen(vec, extra)                                               \
    ({ (__qv_typeof(vec) *)qvector_growlen(&(vec)->qv, __qv_sz(vec),         \
                                           __qv_align(vec), (extra)); })

/** Get the amount of memory needed to perform such grow call.
 *
 * Parameters are identical to \ref qv_grow ones.
 *
 * \return  The amount of memory that will need to be allocated in order to
 * perform successfully such a call to \ref qv_grow function.
 */
#define qv_grow_get_alloc_size(vec, extra)                                   \
    ({                                                                       \
        __qvector_grow_get_new_alloc_size(&(vec)->qv, __qv_sz(vec),          \
                                          (extra));                          \
    })

#define qv_grow0(vec, extra)                                                 \
    ({                                                                       \
        int __extra = extra;                                                 \
        typeof((vec)->tab) __res = qv_grow(vec, __extra);                    \
                                                                             \
        p_clear(__res, __extra);                                             \
        __res;                                                               \
    })

#define qv_growlen0(vec, extra)                                              \
    ({                                                                       \
        int __extra = extra;                                                 \
        typeof((vec)->tab) __res = qv_growlen(vec, __extra);                 \
                                                                             \
        p_clear(__res, __extra);                                             \
        __res;                                                               \
    })

/** \brief keep only the first len elements */
#define qv_clip(vec, _len)  ((vec)->qv.len = (_len))
/** \brief shrink the vector length by len */
#define qv_shrink(vec, _len)                                                 \
    ({  typeof(*(vec)) *__vec = (vec);                                       \
        int __len = (_len);                                                  \
        assert (0 <= __len && __len <= __vec->len);                          \
        __vec->len -= __len; })
/** \brief skip the first len elements */
#define qv_skip(vec, len)    (void)__qv_splice(vec, 0, len, 0)

/** \brief remove the element at position i */
#define qv_remove(vec, i)    (void)__qv_splice(vec, i, 1, 0)
/** \brief remove the last element */
#define qv_remove_last(vec)  (void)__qv_splice(vec, (vec)->len - 1, 1, 0)
/** \brief remove the first element */
#define qv_pop(vec)          (void)__qv_splice(vec, 0, 1, 0)

#define qv_insert(vec, i, v)                                                 \
    ({                                                                       \
        typeof(*(vec)->tab) __v = (v);                                       \
        *__qv_splice(vec, i, 0, 1) = __v;                                    \
    })
#define qv_append(vec, v)                                                    \
    ({                                                                       \
        typeof(*(vec)->tab) __v = (v);                                       \
        *qv_growlen(vec, 1) = (__v);                                         \
    })
#define qv_push(vec, v)                  qv_insert(vec, 0, (v))
#define qv_insertp(vec, i, v)            qv_insert(vec, i, *(v))
#define qv_appendp(vec, v)               qv_append(vec, *(v))
#define qv_pushp(vec, v)                 qv_push(vec, *(v))

/** Append the elements from an array characterized by a pointer and a length.

 *
 * \param[in] _ptr  Pointer to the first element of the array.
 * \param[in] _len  The length of array to append.
 */
#define qv_extend(vec, _ptr, _len)                                           \
    ({                                                                       \
        int _qv_extend_len = (_len);                                         \
        __qv_typeof(vec) *_qv_extend_w = qv_growlen((vec), _qv_extend_len);  \
                                                                             \
        p_copy(_qv_extend_w, (_ptr), _qv_extend_len);                        \
    })

/** Append several elements (variadic macro).
 *
 * \example qv_extend_va(&my_u32_vec, 42, 43, 44);
 */
#define qv_extend_va(vec, ...)                                               \
    ({                                                                       \
        __qv_typeof(vec) _qv_extend_va_array[] = { __VA_ARGS__ };            \
                                                                             \
        qv_extend((vec), _qv_extend_va_array, countof(_qv_extend_va_array)); \
    })

/** Append the elements from an array-like structure.
 *
 * \param[in] _tab  Array-like structure with attributes \p tab and \p len.
 */
#define qv_extend_tab(vec, _tab)                                             \
    ({                                                                       \
        typeof(_tab) _qv_extend_tab_tab = (_tab);                            \
                                                                             \
        qv_extend((vec), _qv_extend_tab_tab->tab, _qv_extend_tab_tab->len);  \
    })

#define qv_copy(vec_out, vec_in)                                             \
    ({  typeof(*(vec_out)) *__vec_out = (vec_out);                           \
        typeof(*(vec_out)) const *__vec_in = (vec_in);                       \
        __vec_out->len = 0;                                                  \
        p_copy(qv_growlen(__vec_out, __vec_in->len), __vec_in->tab,          \
               __vec_in->len);                                               \
        __vec_out;                                                           \
    })

#ifdef __has_blocks
/** \brief build the difference and intersection vectors by comparing elements
 *         of vec1 and vec2
 *
 * This generates a qv_diff function which can be used like that, for example:
 *
 *   qv_diff(u32)(&vec1, &vec2, &add, &del, &inter,
 *                ^int (const uint32_t *v1, const uint32_t *v2) {
 *       return CMP(*v1, *v2);
 *   });
 *
 * \param[in]   vec1  the vector to filter (not modified)
 * \param[in]   vec2  the vector containing the elements to filter
 *                    from vec1 (not modified)
 * \param[out]  add   the vector of v2 values not in v1 (may be NULL if not
 *                    interested in added values)
 * \param[out]  del   the vector of v1 values not in v2 (may be NULL if not
 *                    interested in deleted values)
 * \param[out]  inter the vector of v1 values also in v2 (may be NULL if not
 *                    interested in intersection values)
 * \param[in]   cmp   comparison function for the elements of the vectors
 *
 * You must be in a .blk to use qv_diff, because it expects blocks.
 *
 * WARNING: vec1 and vec2 must be sorted and uniq'ed.
 */
#define qv_diff(n)                          qv_##n##_diff

/** Remove duplicated entries from a vector.
 *
 * This takes a sorted vector as input and remove duplicated entries.
 *
 * \param[in,out]   vec the vector to filter
 * \param[in]       cmp comparison callback for the elements of the vector.
 * \param[in]       del deletion callback for the removed elements of the
 *                      vector. Can be NULL.
 */
#define qv_uniq(n)                          qv_##n##_uniq

/** Lookup the position of the entry in a sorted vector.
 *
 * This takes an entry and a sorted vector and lookup the entry within the
 * vector using a binary search.
 *
 * \param[in]       vec the vector
 * \param[in]       v   the value to lookup
 * \param[out]      found a pointer to a boolean that is set to true if the
 *                  value is found in the vector, false if not. Can be NULL.
 * \param[in]       cmp comparison callback for the elements of the vector.
 * \return          the position of \p v if found, the position where to
 *                  insert \p v if not already present in the vector.
 */
#define qv_bisect(n)                        qv_##n##_bisect

/** Find the position of the entry in a vector.
 *
 * This takes an entry and a vector. If the vector is sorted, a binary search
 * is performed, otherwise a linear scan is performed.
 *
 * \param[in]       vec    the vector
 * \param[in]       v      the value to lookup
 * \param[in]       sorted if true the vector is considered as being sorted
 *                         with the ordering of the provided comparator.
 * \param[in]       cmp    comparison callback for the elements of the vector
 * \return          index of the element if found in the vector, -1 otherwise.
 */
#define qv_find(n)                      qv_##n##_find

/** Check if a vector contains a specific entry.
 *
 * This takes an entry and a vector. If the vector is sorted, a binary search
 * is performed, otherwise a linear scan is performed.
 *
 * \param[in]       vec    the vector
 * \param[in]       v      the value to lookup
 * \param[in]       sorted if true the vector is considered as being sorted
 *                         with the ordering of the provided comparator.
 * \param[in]       cmp    comparison callback for the elements of the vector
 * \return          true if the element was found in the vector, false
 *                  otherwise.
 */
#define qv_contains(n)                      qv_##n##_contains
#endif


/* Define several common types */
qvector_t(i8,     int8_t);
qvector_t(u8,     uint8_t);
qvector_t(i16,    int16_t);
qvector_t(u16,    uint16_t);
qvector_t(i32,    int32_t);
qvector_t(u32,    uint32_t);
qvector_t(i64,    int64_t);
qvector_t(u64,    uint64_t);
qvector_t(void,   void * nullable);
qvector_t(double, double);
qvector_t(str,    char * nullable);
qvector_t(lstr,   lstr_t);
qvector_t(pstream, pstream_t);

qvector_t(cvoid, const void * nullable);
qvector_t(cstr,  const char * nullable);
qvector_t(sbp,   sb_t * nullable);

/* Built-in comparison blocks for common types */
#ifdef __has_blocks

#define qv_i8_cmp  core_i8_cmp
#define qv_i16_cmp  core_i16_cmp
#define qv_i32_cmp  core_i32_cmp
#define qv_i64_cmp  core_i64_cmp
#define qv_double_cmp  core_double_cmp
#define qv_str_cmp  core_str_cmp
#define qv_cstr_cmp  core_cstr_cmp
#define qv_lstr_cmp  core_lstr_cmp

/* XXX Always use optimized dsort##n/uniq##n variants instead of
 *     qv_sort(u##n)/qv_uniq(u##n).
 */
#define qv_u8_cmp  NEVER_USE_qv_u8_cmp
#define qv_u16_cmp  NEVER_USE_qv_u16_cmp
#define qv_u32_cmp  NEVER_USE_qv_u32_cmp
#define qv_u64_cmp  NEVER_USE_qv_u64_cmp

#endif

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
