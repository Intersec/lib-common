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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_MEM_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_MEM_H

/* Memory Copy/Move {{{ */
/**************************************************************************/
/* Memory pools high level APIs                                           */
/**************************************************************************/

#ifndef __USE_GNU
static inline void * nonnull mempcpy(void * nonnull restrict dst,
                                     const void * nonnull restrict src,
                                     size_t n)
{
    return (void *)((byte *)memcpy(dst, src, n) + n);
}

static inline void * nullable memrchr(const void * nonnull s, int c, size_t n)
{
    const uint8_t *start = (const uint8_t *)s;
    const uint8_t *end   = start + n;

    while (end > start) {
        --end;
        if (*end == (uint8_t)c) {
            return (void *)end;
        }
    }
    return NULL;
}

static inline
char * nonnull strchrnul(const char * nonnull s, int c)
{
    while ((unsigned char)*s != c && (unsigned char)*s != '\0') {
        s++;
    }
    return (char *)s;
}
#endif

static inline void * nonnull memcpyz(void * nonnull restrict dst,
                                     const void * nonnull restrict src,
                                     size_t n)
{
    *(char *)mempcpy(dst, src, n) = '\0';
    return dst;
}
static inline void * nonnull mempcpyz(void * nonnull restrict dst,
                                      const void * nonnull restrict src,
                                      size_t n)
{
    dst = mempcpy(dst, src, n);
    *(char *)dst = '\0';
    return (char *)dst + 1;
}

#define p_clear(p, count)                                                    \
    ({ (typeof(*(p)) *)memset((p), 0, sizeof(*(p)) * (count)); })

#define p_alloc_nr(x) (((x) + 16) * 3 / 2)


#ifdef __cplusplus
#define memcpy_check_types(e1, e2) \
    STATIC_ASSERT(sizeof(*(e1)) == sizeof(*(e2)))
#else
#define memcpy_check_types(e1, e2) \
    STATIC_ASSERT(                                                           \
        __builtin_choose_expr(                                               \
            __builtin_choose_expr(                                           \
                __builtin_types_compatible_p(typeof(e2), void *),            \
                true, sizeof(*(e2)) == 1), true,                             \
            __builtin_types_compatible_p(typeof(*(e1)), typeof(*(e2)))))
#endif

#define p_move(to, src, n) \
    ({ memcpy_check_types(to, src);                                          \
       (typeof(*(to)) *)memmove(to, src, sizeof(*(to)) * (n)); })
#define p_copy(to, src, n) \
    ({ memcpy_check_types(to, src);                                          \
       (typeof(*(to)) *)memcpy(to, src, sizeof(*(to)) * (n)); })
#define p_pcopy(to, src, n) \
    ({ memcpy_check_types(to, src);                                          \
       (typeof(*(to)) *)mempcpy(to, src, sizeof(*(to)) * (n)); })

#define p_move2(p, to, from, n) \
    ({ typeof(*(p)) *__p = (p); p_move(__p + (to), __p + (from), n); })
#define p_copy2(p, to, from, n) \
    ({ typeof(*(p)) __p = (p); p_copy(__p + (to), __p + (from), n); })

/** Copies at <code>n</code> characters from the string pointed to by
 * <code>src</code> to the buffer <code>dest</code> of <code>size</code>
 * bytes.
 * If <code>size</code> is greater than 0, a terminating '\0' character will
 * be put at the end of the copied string.
 *
 * The source and destination strings should not overlap.  No assumption
 * should be made on the values of the characters after the first '\0'
 * character in the destination buffer.
 *
 * @return <code>n</code>
 */
size_t pstrcpymem(char * nonnull restrict dest, ssize_t size,
                  const void * nonnull restrict src, size_t n)
    __leaf;

/** Copies the string pointed to by <code>src</code> to the buffer
 * <code>dest</code> of <code>size</code> bytes.
 * If <code>size</code> is greater than 0, a terminating '\0' character will
 * be put at the end of the copied string.
 *
 * The source and destination strings should not overlap.  No assumption
 * should be made on the values of the characters after the first '\0'
 * character in the destination buffer.
 *
 * @return the length of the source string.
 * @see pstrcpylen
 *
 * RFE/RFC: In a lot of cases, we do not care that much about the length of
 * the source string. What we want is "has the string been truncated
 * and we should stop here, or is everything fine ?". Therefore, may be
 * we need a function like :
 * int pstrcpy_ok(char *dest, ssize_t size, const char *src)
 * {
 *     if (pstrcpy(dest, size, src) < size) {
 *        return 0;
 *     } else {
 *        return 1;
 *     }
 * }
 *
 */
__attr_nonnull__((1, 3))
static ALWAYS_INLINE
size_t pstrcpy(char * nonnull restrict dest, ssize_t size,
               const char * nonnull restrict src)
{
    return pstrcpymem(dest, size, src, strlen(src));
}

/** Copies at most <code>n</code> characters from the string pointed
 * to by <code>src</code> to the buffer <code>dest</code> of
 * <code>size</code> bytes.
 * If <code>size</code> is greater than 0, a terminating '\0' character will
 * be put at the end of the copied string.
 *
 * The source and destination strings should not overlap.  No assumption
 * should be made on the values of the characters after the first '\0'
 * character in the destination buffer.
 *
 * @return the length of the source string or <code>n</code> if smaller.
 */
__attr_nonnull__((1, 3))
static ALWAYS_INLINE
size_t pstrcpylen(char * nonnull restrict dest, ssize_t size,
                  const char * nonnull restrict src, size_t n)
{
    return pstrcpymem(dest, size, src, strnlen(src, n));
}

/** Appends the string pointed to by <code>src</code> at the end of
 * the string pointed to by <code>dest</code> not overflowing
 * <code>size</code> bytes.
 *
 * The source and destination strings should not overlap.
 * No assumption should be made on the values of the characters
 * after the first '\0' character in the destination buffer.
 *
 * If the destination buffer doesn't contain a properly '\0' terminated
 * string, dest is unchanged and the value returned is size+strlen(src).
 *
 * @return the length of the source string plus the length of the
 * destination string.
 */
__attr_nonnull__((1, 3))
static inline size_t
pstrcat(char * nonnull restrict dest, ssize_t size,
        const char * nonnull restrict src)
{
    size_t dlen = size > 0 ? strnlen(dest, size) : 0;
    return dlen + pstrcpy(dest + dlen, size - dlen, src);
}

/* }}} */
/* Memory Allocations {{{ */
/* Memory Pools {{{ */

/** Check whether the memory pools are active.
 * If not, all the memory pools should fallback
 * mem_pool_libc.
 */
#ifndef NDEBUG
bool mem_pool_is_enabled(void);
#else
static inline bool mem_pool_is_enabled(void)
{
    return true;
}
#endif

typedef unsigned __bitwise__ mem_flags_t;

/*
 * 1 << 30 is 1 Gig, we check that the user hasn't made any mistake with
 * roundings and so on.
 */
#define MEM_ALLOC_MAX    (1ull << 30)
#define MEM_UNKNOWN      ((size_t)-1)
#define MEM_EMPTY_ALLOC  ((void *)0x1000)

/*
 * mem_flags_t modify the ialloc functions behaviours.
 *
 * %MEM_RAW::
 *     - imalloc does not zero returned memory if set.
 *     - irealloc does not zero [oldsize .. size[ if set.
 *       Note that irealloc implementations should not trust oldsize if
 *       MEM_RAW is set and oldsize is MEM_UNKNOWN.
 *
 * %MEM_ERRORS_OK::
 *     caller can deal with allocation errors from malloc or realloc.
 *
 * pool origins:
 *
 * %MEM_STATIC::
 *     memory comes from alloca() or a static buffer.
 *
 * %MEM_LIBC::
 *     This flag makes no sense for pools.  irealloc and ifree shall assume
 *     that this memory is allocated using the libc allocator (malloc or
 *     calloc).
 *
 * %MEM_STACK::
 *     This flags asks the allocations to be performed on the system default
 *     stack pool.
 *
 */
enum mem_pools_t {
    MEM_STATIC = 0,
    MEM_OTHER  = 1,
    MEM_LIBC   = 2,
    MEM_STACK  = 3,
    MEM_MMAP   = 4,
};
#define MEM_POOL_MASK          force_cast(mem_flags_t, 0x00ff)
#define MEM_FLAGS_MASK         force_cast(mem_flags_t, 0xff00)
#define MEM_RAW                force_cast(mem_flags_t, 1 << 8)
#define MEM_ERRORS_OK          force_cast(mem_flags_t, 1 << 9)
#define MEM_UNALIGN_OK         force_cast(mem_flags_t, 1 << 10)
#define MEM_BY_FRAME           force_cast(mem_flags_t, 1 << 11)
#define MEM_EFFICIENT_REALLOC  force_cast(mem_flags_t, 1 << 12)

#define CACHE_LINE_SIZE   64

typedef struct mem_pool_t {
    mem_flags_t mem_pool;
    uint32_t    min_alignment;
    struct mem_pool_t * nullable realloc_fallback;

    /* DO NOT USE DIRECTLY, use mp_imalloc/mp_irealloc/mp_ifree instead */
    void * nonnull (* nonnull malloc)(struct mem_pool_t * nonnull, size_t,
                                      size_t, mem_flags_t);
    void * nonnull (* nonnull realloc)(struct mem_pool_t * nonnull,
                                       void * nullable, size_t, size_t,
                                       size_t, mem_flags_t);
    void  (* nonnull free)(struct mem_pool_t * nonnull, void * nullable);
} mem_pool_t;



/*
 * mem_pool_cl_aligned ensure that allocation will be aligned on
 * CACHE_LINE_SIZE to prevent false sharing between threads.
 */
extern mem_pool_t mem_pool_libc;
extern mem_pool_t mem_pool_cl_aligned;
extern mem_pool_t mem_pool_static;
static ALWAYS_INLINE mem_pool_t * nonnull t_pool(void);

#if __GNUC_PREREQ(4, 3) || __has_attribute(error)
__attribute__((error("you cannot allocate that much memory")))
#endif
extern void __imalloc_too_large(void);

#ifdef PAGE_SIZE
# undef PAGE_SIZE
#endif
#define PAGE_SIZE_SHIFT     12
#define PAGE_SIZE   ((uintptr_t) 1 << PAGE_SIZE_SHIFT)

static ALWAYS_INLINE
size_t mem_bit_align(const mem_pool_t * nonnull mp, size_t alignment)
{
    assert (bitcountsz(alignment) <= 1);
    return (alignment ? MAX(mp->min_alignment, alignment)
            : __BIGGEST_ALIGNMENT__);
}

static ALWAYS_INLINE
uintptr_t mem_align_ptr(uintptr_t ptr, size_t align)
{
    return ROUND_UP_2EXP(ptr, align);
}

static ALWAYS_INLINE void icheck_alloc_constant(size_t size)
{
    if (__builtin_constant_p(size) && size > MEM_ALLOC_MAX) {
        __imalloc_too_large();
    }
}

void icheck_alloc(size_t size);

#ifndef alignof
# define alignof(type)  __alignof__(type)
#endif

#define extra_field_size(type, field, size)  ({                              \
        size_t __efz = offsetof(type, field);                                \
                                                                             \
        __efz += fieldsizeof(type, field[0]) * (size);                       \
                                                                             \
        /* Allocate at least sizeof(type) so that p_clear() works properly   \
         * on the result of a new_extra_field allocation.                    \
         */                                                                  \
        MAX(sizeof(type), __efz);                                            \
    })


/*
 * Intersec memory allocation wrappers allow people to write APIs that are not
 * aware that the memory was actually allocated from a pool.
 */

__attribute__((malloc, warn_unused_result))
void * nonnull __mp_imalloc(mem_pool_t * nullable mp, size_t size,
                            size_t alignment, mem_flags_t flags);

__attribute__((malloc, warn_unused_result))
static ALWAYS_INLINE void * nonnull
mp_imalloc(mem_pool_t * null_unspecified mp, size_t size, size_t alignment,
           mem_flags_t flags)
{
    icheck_alloc_constant(size);
    return __mp_imalloc(mp, size, alignment, flags);
}

__attribute__((warn_unused_result))
void * nonnull __mp_irealloc(mem_pool_t * nullable mp, void * nullable mem,
                             size_t oldsize, size_t size, size_t alignment,
                             mem_flags_t flags);

__attribute__((warn_unused_result))
static ALWAYS_INLINE
void * nonnull mp_irealloc(mem_pool_t * nullable mp, void * nullable mem,
                           size_t oldsize, size_t size, size_t alignment,
                           mem_flags_t flags)
{
    icheck_alloc_constant(size);
    return __mp_irealloc(mp, mem, oldsize, size, alignment, flags);
}

void mp_ifree(mem_pool_t * nullable mp, void * nullable mem);

__attribute__((warn_unused_result))
void * nonnull __mp_irealloc_fallback(mem_pool_t * nullable * nonnull pmp,
                                      void * nullable mem, size_t oldsize,
                                      size_t size, size_t alignment,
                                      mem_flags_t flags);

__attribute__((warn_unused_result))
static ALWAYS_INLINE
void * nonnull mp_irealloc_fallback(mem_pool_t * nullable * nonnull pmp,
                                    void * nullable mem, size_t oldsize,
                                    size_t size, size_t alignment,
                                    mem_flags_t flags)
{
    icheck_alloc_constant(size);
    return __mp_irealloc_fallback(pmp, mem, oldsize, size, alignment, flags);
}

mem_pool_t * nonnull ipool(mem_flags_t flags);

static ALWAYS_INLINE
mem_pool_t * nonnull mp_ipool(mem_pool_t * nullable mp)
{
    return mp ?: &mem_pool_libc;
}

__attribute__((warn_unused_result, malloc))
static ALWAYS_INLINE
void * nonnull imalloc(size_t size, size_t alignment, mem_flags_t flags)
{
    return mp_imalloc(ipool(flags), size, alignment, flags);
}

__attribute__((warn_unused_result))
static ALWAYS_INLINE
void * nonnull irealloc(void * nullable mem, size_t oldsize, size_t size,
                        size_t alignment, mem_flags_t flags)
{
    return mp_irealloc(ipool(flags), mem, oldsize, size, alignment, flags);
}

static ALWAYS_INLINE
void ifree(void * nullable mem, mem_flags_t flags)
{
    return mp_ifree(ipool(flags), mem);
}

__attribute__((malloc, warn_unused_result))
static inline void * nonnull
mp_idup(mem_pool_t * nullable mp, const void * nonnull src, size_t size,
        size_t alignment)
{
    return memcpy(mp_imalloc(mp, size, alignment, MEM_RAW), src, size);
}

__attribute__((malloc, warn_unused_result))
static inline void * nonnull
mp_dupz(mem_pool_t * nullable mp, const void * nonnull src, size_t len)
{
    void *res = mp_imalloc(mp, len + 1, 1, MEM_RAW);
    return memcpyz(res, src, len);
}

__attribute__((malloc, warn_unused_result))
static inline char * nonnull
mp_strdup(mem_pool_t * nullable mp, const char * nonnull src)
{
    return (char *)mp_idup(mp, src, strlen(src) + 1, 1);
}

char * nonnull mp_fmt(mem_pool_t * nullable mp, int * nullable lenp,
                      const char * nonnull fmt, ...)
    __leaf __attr_printf__(3, 4);

char * nonnull mp_vfmt(mem_pool_t * nullable mp, int * nullable lenp,
                       const char * nonnull fmt, va_list va)
    __leaf __attr_printf__(3, 0);

/* Generic Helpers */

#define mpa_new_raw(mp, type, count, alignment)                              \
    ((type *)mp_imalloc((mp), sizeof(type) * (count), (alignment), MEM_RAW))
#define mpa_new(mp, type, count, alignment)                                  \
    ((type *)mp_imalloc((mp), sizeof(type) * (count), (alignment), 0))
#define mpa_new_extra(mp, type, size, alignment)                             \
    ((type *)mp_imalloc((mp), sizeof(type) + (size), (alignment), 0))
#define mpa_new_extra_field(mp, type, field, size, alignment)                \
    ((type *)mp_imalloc((mp), extra_field_size(type, field, (size)),         \
                        (alignment), 0))
#define mpa_new_extra_raw(mp, type, size, alignment)                         \
    ((type *)mp_imalloc((mp), sizeof(type) + (size), (alignment), MEM_RAW))
#define mpa_new_extra_field_raw(mp, type, field, size, alignment)            \
    ((type *)mp_imalloc((mp), extra_field_size(type, field, (size)),         \
                        (alignment), MEM_RAW))

#define __mpa_realloc(mp, pp, old, now, alignment, flags)                    \
    ({                                                                       \
        typeof(**(pp)) **__ptr = (pp);                                       \
        *__ptr = mp_irealloc((mp), *__ptr, sizeof(**__ptr) * (old),          \
                             sizeof(**__ptr) * (now),                        \
                             (alignment), (flags));                          \
    })

#define mpa_realloc(mp, pp, count, alignment)                                \
    __mpa_realloc((mp), (pp), MEM_UNKNOWN, (count), (alignment), MEM_RAW)
#define mpa_realloc0(mp, pp, old, now, alignment)                            \
    __mpa_realloc((mp), (pp), (old), (now), (alignment), 0)
#define mpa_realloc_from(mp, pp, old, now, alignment)                        \
    __mpa_realloc((mp), (pp), (old), (now), (alignment), MEM_RAW)

#define mpa_realloc_extra(mp, pp, extra, alignment)                          \
    ({                                                                       \
        typeof(**(pp)) **__ptr = (pp);                                       \
        *__ptr = mp_irealloc((mp), *__ptr, MEM_UNKNOWN,                      \
                             sizeof(**__ptr) + (extra),                      \
                             (alignment), MEM_RAW);                          \
    })

#define __mpa_realloc_extra_from(mp, pp, old_extra, new_extra, alignment,    \
                                 flags)                                      \
    ({                                                                       \
        typeof(**(pp)) **__ptr = (pp);                                       \
        *__ptr = mp_irealloc((mp), *__ptr, sizeof(**__ptr) + (old_extra),    \
                             sizeof(**__ptr) + (new_extra), (alignment),     \
                             (flags));                                       \
    })

#define mpa_realloc0_extra(mp, pp, old_extra, new_extra, alignment)          \
    __mpa_realloc_extra_from((mp), (pp), (old_extra), (new_extra),           \
                             (alignment), 0)

#define mpa_realloc_extra_from(mp, pp, old_extra, new_extra, alignment)      \
    __mpa_realloc_extra_from((mp), (pp), (old_extra), (new_extra),           \
                             (alignment), MEM_RAW)

#define mpa_realloc_extra_field(mp, pp, field, count, alignment)             \
    ({                                                                       \
        typeof(**(pp)) **__ptr = (pp);                                       \
        *__ptr = mp_irealloc((mp), *__ptr, MEM_UNKNOWN,                      \
                             extra_field_size(typeof(**pp), field, (count)), \
                             (alignment), MEM_RAW);                          \
    })

#define __mpa_realloc_extra_field_from(mp, pp, field, old_count, new_count,  \
                                       alignment, flags)                     \
    ({                                                                       \
        typeof(**(pp)) **__ptr = (pp);                                       \
        *__ptr = mp_irealloc((mp), *__ptr,                                   \
                     extra_field_size(typeof(**pp), field, (old_count)),     \
                     extra_field_size(typeof(**pp), field, (new_count)),     \
                     (alignment), (flags));                                  \
    })
#define mpa_realloc0_extra_field(mp, pp, field, old_count, new_count,        \
                                 alignment)                                  \
    __mpa_realloc_extra_field_from((mp), (pp), field, (old_count),           \
                                   (new_count), (alignment), 0)

#define mpa_realloc_extra_field_from(mp, pp, field, old_count, new_count,    \
                                 alignment)                                  \
    __mpa_realloc_extra_field_from((mp), (pp), field, (old_count),           \
                                   (new_count), (alignment), MEM_RAW)

#define mpa_dup(mp, ptr, count, alignment)                                   \
    mp_idup((mp), (ptr), sizeof(*ptr) * (count), (alignment))


/* Alignement aware helpers */

#define mp_new_raw(mp, type, count)                                          \
    mpa_new_raw((mp), type, (count), alignof(type))
#define mp_new(mp, type, count)                                              \
    mpa_new((mp), type, (count), alignof(type))
#define mp_new_extra(mp, type, size)                                         \
    mpa_new_extra((mp), type, (size), alignof(type))
#define mp_new_extra_raw(mp, type, size)                                     \
    mpa_new_extra_raw((mp), type, (size), alignof(type))
#define mp_new_extra_field(mp, type, field, size)                            \
    mpa_new_extra_field((mp), type, field, (size), alignof(type))
#define mp_new_extra_field_raw(mp, type, field, size)                        \
    mpa_new_extra_field_raw((mp), type, field, (size), alignof(type))

#define mp_realloc(mp, pp, count)                                            \
    mpa_realloc((mp), (pp), (count), alignof(**(pp)))
#define mp_realloc_from(mp, pp, old, now)                                    \
    mpa_realloc_from((mp), (pp), (old), (now), alignof(**(pp)))
#define mp_realloc0(mp, pp, old, now)                                        \
    mpa_realloc0((mp), (pp), (old), (now), alignof(**(pp)))
#define mp_realloc_extra(mp, pp, extra)                                      \
    mpa_realloc0_extra((mp), (pp), (extra), alignof(**(pp)))
#define mp_realloc0_extra(mp, pp, old_extra, new_extra)                      \
    mpa_realloc0_extra((mp), (pp), (old_extra), (new_extra), alignof(**(pp)))

#define mp_realloc_extra_field(mp, pp, field, count)                         \
    mpa_realloc_extra_field((mp), (pp), field, (count), alignof(**(pp)))
#define mp_realloc0_extra_field(mp, pp, field, old_count, new_count)         \
    mpa_realloc0_extra_field((mp), (pp), field, (old_count), (new_count),    \
                             alignof(**(pp)))

#define mp_delete(mp, pp)  ({                                                \
        typeof(**(pp))  **__ptr = (pp);                                      \
        mp_ifree((mp), *__ptr);                                              \
        *__ptr = NULL;                                                       \
    })

#define mp_dup(mp, src, size)  mpa_dup((mp), (src), (size), alignof(src))


/* }}} */
/* LibC {{{ */

/* Generic Helpers */

#define pa_new_raw(type, count, alignment)                                   \
    mpa_new_raw(&mem_pool_libc, type, (count), (alignment))
#define pa_new(type, count, alignment)                                       \
    mpa_new(&mem_pool_libc, type, (count), (alignment))
#define pa_new_extra(type, size, alignment)                                  \
    mpa_new_extra(&mem_pool_libc, type, (size), (alignment))
#define pa_new_extra_raw(type, size, alignment)                              \
    mpa_new_extra_raw(&mem_pool_libc, type, (size), (alignment))
#define pa_new_extra_field(type, field, size, alignment)                     \
    mpa_new_extra_field(&mem_pool_libc, type, field, (size), (alignment))
#define pa_new_extra_field_raw(type, field, size, alignment)                 \
    mpa_new_extra_field_raw(&mem_pool_libc, type, field, (size), (alignment))

#define pa_realloc(pp, count, alignment)                                     \
    mpa_realloc(&mem_pool_libc, (pp), (count), (alignment))
#define pa_realloc0(pp, old, now, alignment)                                 \
    mpa_realloc0(&mem_pool_libc, (pp), (old), (now), (alignment))
#define pa_realloc_extra(pp, extra, alignment)                               \
    mpa_realloc_extra(&mem_pool_libc, (pp), (extra), (alignment))
#define pa_realloc0_extra(pp, old_extra, new_extra, alignment)               \
    mpa_realloc0_extra(&mem_pool_libc, (pp), (old_extra), (new_extra),       \
                       (alignment))

#define pa_realloc_extra_field(pp, field, count, alignment)                  \
    mpa_realloc_extra_field(&mem_pool_libc, (pp), field, (count),            \
                            (alignment))
#define pa_realloc0_extra_field(pp, field, old_count, new_count, alignment)  \
    mpa_realloc0_extra_field(&mem_pool_libc, (pp), field, (old_count),       \
                             (new_count), (alignment))

#define pa_dup(src, size, alignment)                                         \
    mpa_dup(&mem_pool_libc, (src), (size), (alignment))

/* Alignement aware helpers */

#define p_new_raw(type, count)       pa_new_raw(type, (count), alignof(type))
#define p_new(type, count)           pa_new(type, (count), alignof(type))
#define p_new_extra(type, size)      pa_new_extra(type, (size), alignof(type))
#define p_new_extra_raw(type, size)                                          \
    pa_new_extra_raw(type, (size), alignof(type))
#define p_new_extra_field(type, field, size)                                 \
    pa_new_extra_field(type, field, (size), alignof(type))
#define p_new_extra_field_raw(type, field, size)                             \
    pa_new_extra_field_raw(type, field, (size), alignof(type))

#define p_realloc(pp, count)        pa_realloc((pp), (count), alignof(**(pp)))
#define p_realloc0(pp, old, now)                                             \
    pa_realloc0((pp), (old), (now), alignof(**(pp)))
#define p_realloc_extra(pp, extra)                                           \
    pa_realloc_extra((pp), (extra), alignof(**(pp)))
#define p_realloc0_extra(pp, old_extra, new_extra)                           \
    pa_realloc0_extra((pp), (old_extra), (new_extra), alignof(**(pp)))

#define p_realloc_extra_field(pp, field, count)                              \
    pa_realloc_extra_field((pp), field, (count), alignof(**(pp)))
#define p_realloc0_extra_field(pp, field, old_count, new_count)              \
    pa_realloc0_extra_field((pp), field, (old_count), (new_count),           \
                            alignof(**(pp)))

#define p_dup(p, count)         pa_dup((p), (count), alignof(p))
#define p_dupz(p, count)        mp_dupz(&mem_pool_libc, (p), (count))
#define p_strdup(p)             mp_strdup(&mem_pool_libc, (p))

/* Deallocation */

#define p_delete(pp)  mp_delete(&mem_pool_libc, (pp))

#ifndef __cplusplus
static inline void (p_delete)(void * nullable * nonnull p)
{
    p_delete(p);
}
#endif

/** Set the period for the cron that calls malloc_trim().
 *
 * \param[in] period  Malloc trim period (in millisecond).
 */
void core_mem_set_malloc_trim_period(int64_t period);

/** Set the value to use when calling malloc_trim(). */
void core_mem_set_malloc_trim_pad(size_t pad);

/** Manually call malloc trim. */
void core_mem_malloc_trim(void);

/* }}} */
/* Mem-fifo Pool {{{ */

mem_pool_t * nonnull mem_fifo_pool_new(const char * nonnull name,
                                       int page_size_hint)
    __leaf __attribute__((malloc));
void mem_fifo_pool_delete(mem_pool_t * nullable * nonnull poolp)
    __leaf;
void mem_fifo_pool_stats(mem_pool_t * nonnull mp, ssize_t * nonnull allocated,
                         ssize_t * nonnull used)
    __leaf;

void mem_fifo_pool_print_stats(mem_pool_t * nonnull mp);
void mem_fifo_pools_print_stats(void);

/* }}} */
/* Mem-ring Pool {{{ */

/** Create a new memory ring-pool.
 *
 * The memory ring-pool is an allocator working with frames of memory which
 * rotates on a extending ring.
 *
 * This is quite a fifo-pool with the oldest frames always re-appended at the
 * head of the pool.
 *
 * \param[name]         Name of the ring pool, used for debug.
 * \param[initialsize]  First memory block size.
 */
mem_pool_t * nonnull mem_ring_new(const char * nonnull name, int initialsize);

/** Delete the given memory ring-pool */
void mem_ring_delete(mem_pool_t * nullable * nonnull) __leaf;

/** Force clean the ring pool from empty memory blocks.
 *
 * Adapted from mem_stack_pool_reset().
 *
 * Only keeps the current frame so as the one that fits the best the current
 * needs regarding the mean allocation size (see code for details).
 *
 * Should be called when the pool is idle: won't do anything if called while
 * there are still some frames not released.
 *
 * XXX The clean-up is supposed to be automatically triggered every 64K frame
 * releases, using this function to force it means that you have a clear
 * understanding of what you are doing.
 */
void mem_ring_reset(mem_pool_t * nonnull) __leaf;

/** Create a new frame of memory in the ring.
 *
 * This function push a new extensible page of memory in the ring. You need to
 * call it before allocating memory. But be careful, you cannot push a new
 * frame if you haven't sealed the previous one.
 *
 * \return Frame cookie (needed to release the frame later).
 */
const void * nonnull mem_ring_newframe(mem_pool_t * nonnull) __leaf;

/** Get the active frame cookie. */
const void * nonnull mem_ring_getframe(mem_pool_t * nonnull) __leaf;

/** Seal the active frame.
 *
 * When you seal the active frame, you cannot perform new allocations in it
 * but the allocated data is still accessible. Do not forget to release it
 * later!
 *
 * \return Frame cookie (needed to release the frame later).
 */
const void * nonnull mem_ring_seal(mem_pool_t * nonnull) __leaf;

/** Release the frame identified by the given cookie.
 *
 * This function will free an existing frame using the given cookie. Be
 * careful that the cookie will be invalidated after the frame release.
 */
void mem_ring_release(const void * nonnull cookie) __leaf;

/** Seal the ring-pool and return a checkpoint.
 *
 * The returned check point will allow you to restore later the ring-pool at
 * this current state.
 */
const void * nonnull mem_ring_checkpoint(mem_pool_t * nonnull) __leaf;


/** Rewind the ring-pool at a given checkpoint.
 *
 * The rewind procedure will release all the framed created between the
 * current active frame and the checkpoint. Then it will unseal the frame
 * which was sealed when mem_ring_checkpoint() was called.
 *
 * Be careful that it will not restore the frames released after the
 * checkpoint.
 *
 * The checkpoint cannot be reused after this call.
 */
void mem_ring_rewind(mem_pool_t * nonnull, const void * nonnull ckpoint) __leaf;

/** Dump the ring structure on stdout (debug) */
void mem_ring_dump(const mem_pool_t * nonnull) __leaf;

/** Get the malloc'd size of the memory pool. */
size_t mem_ring_memory_footprint(const mem_pool_t * nonnull) __leaf;

/** Just like the t_pool() we have the corresponding r_pool() */
mem_pool_t * nonnull r_pool(void) __leaf;

/** Destroy the r_pool() */
void r_pool_destroy(void) __leaf;

#define r_newframe()                mem_ring_newframe(r_pool())
#define r_seal()                    mem_ring_seal(r_pool())
#define r_getframe()                mem_ring_getframe(r_pool())
#define r_release(ptr)              mem_ring_release(ptr)
#define r_checkpoint()              mem_ring_checkpoint(r_pool())
#define r_rewind(ptr)               mem_ring_rewind(r_pool(), ptr)


/* Aligned pointers allocation helpers */

#define ra_new_raw(type, count, alignment)                                   \
    mpa_new_raw(r_pool(), type, (count), (alignment))
#define ra_new(type, count, alignment)                                       \
    mpa_new(r_pool(), type, (count), (alignment))
#define ra_new_extra(type, size, alignment)                                  \
    mpa_new_extra(r_pool(), type, (size), (alignment))
#define ra_new_extra_raw(type, size, alignment)                              \
    mpa_new_extra_raw(r_pool(), type, (size), (alignment))
#define ra_new_extra_field(type, field, size, alignment)                     \
    mpa_new_extra_field(r_pool(), type, field, (size), (alignment))
#define ra_new_extra_field_raw(type, field, size, alignment)                 \
    mpa_new_extra_field_raw(r_pool(), type, field, (size), (alignment))

#define ra_realloc_from(pp, old, now, alignment)                             \
    mpa_realloc_from(r_pool(), (pp), (old), (now), (alignment))
#define ra_realloc0(pp, old, now, alignment)                                 \
    mpa_realloc0(r_pool(), (pp), (old), (now), (alignment))
#define ra_realloc_extra_from(pp, old_extra, new_extra, alignment)           \
    mpa_realloc_extra_from(r_pool(), (pp), (old_extra), (new_extra),         \
                           (alignment))
#define ra_realloc0_extra(pp, old_extra, new_extra, alignment)               \
    mpa_realloc0_extra(r_pool(), (pp), (old_extra), (new_extra), (alignment))

#define ra_realloc_extra_field_from(pp, field, old_count, new_count,         \
                                    alignment)                               \
    mpa_realloc_extra_field_from(r_pool(), (pp), field, (old_count),         \
                                 (new_count), (alignment))
#define ra_realloc0_extra_field(pp, field, old_count, new_count, alignment)  \
    mpa_realloc0_extra_field(r_pool(), (pp), field, (old_count),             \
                             (new_count), (alignment))

#define ra_dup(p, count, alignment)                                          \
    mpa_dup(r_pool(), (p), (count), (alignment))

/* Pointer allocations helpers */

#define r_new_raw(type, count)       ra_new_raw(type, (count), alignof(type))
#define r_new(type, count)           ra_new(type, (count), alignof(type))
#define r_new_extra(type, size)      ra_new_extra(type, (size), alignof(type))
#define r_new_extra_raw(type, size)                                          \
    ra_new_extra_raw(type, (size), alignof(type))
#define r_new_extra_field(type, field, size)                                 \
    ra_new_extra_field(type, field, (size), alignof(type))
#define r_new_extra_field_raw(type, field, size)                             \
    ra_new_extra_field_raw(type, field, (size), alignof(type))

#define r_realloc_from(tp, old, now)                                         \
    ra_realloc_from(tp, (old), (now), alignof(**(tp)))
#define r_realloc0(tp, old, now)                                             \
    ra_realloc0(tp, (old), (now), alignof(**(tp)))
#define r_realloc_extra_from(tp, old_extra, new_extra)                       \
    ra_realloc_extra_from((tp), (old_extra), (new_extra), alignof(**(tp)))
#define r_realloc0_extra(tp, old_extra, new_extra)                           \
    ra_realloc0_extra(tp, (old_extra), (new_extra), alignof(**(tp)))

#define r_realloc_extra_field_from(tp, field, old_count, new_count)          \
    ra_realloc_extra_field_from((tp), field, (old_count), (new_count),       \
                                alignof(**(tp)))
#define r_realloc0_extra_field(tp, field, old_count, new_count)              \
    ra_realloc0_extra_field((tp), field, (old_count), (new_count),           \
                            alignof(**(tp)))

#define r_dup(p, count)    ra_dup((p), (count), alignof(p))
#define r_dupz(p, count)   mp_dupz(r_pool(), (p), (count))
#define r_strdup(p)        mp_strdup(r_pool(), (p))

/* }}} */
/* Alloca {{{ */

#define p_alloca_raw(type_t, count)                                          \
    ((type_t *)alloca(sizeof(type_t) * (count)))

#define p_alloca(type_t, count)                                              \
    ({  size_t __count = (count);                                            \
        p_clear(p_alloca_raw(type_t, __count), __count);                     \
    })

/* }}} */
/* }}} */
/* Structure allocation helpers {{{ */

#define DO_INIT(type, prefix)                                                \
    __attr_nonnull__((1))                                                    \
    type * nonnull prefix##_init(type * nonnull var) {                       \
        return p_clear(var, 1);                                              \
    }
#define DO_NEW(type, prefix) \
    __attribute__((malloc)) type * nonnull prefix##_new(void) {              \
        return prefix##_init(p_new_raw(type, 1));                            \
    }
#define DO_NEW0(type, prefix) \
    __attribute__((malloc)) type * nonnull prefix##_new(void) {              \
        return p_new(type, 1);                                               \
    }

#define DO_WIPE(type, prefix)                                                \
    __attr_nonnull__((1)) void prefix##_wipe(type * nonnull var) { }

#define DO_DELETE(type, prefix) \
    __attr_nonnull__((1))                                                    \
    void prefix##_delete(type * nullable * nonnull varp) {                   \
        type * const var = *varp;                                            \
                                                                             \
        if (var) {                                                           \
            prefix##_wipe(var);                                              \
            assert (likely(var == *varp) && "pointer corruption detected");  \
            p_delete(varp);                                                  \
        }                                                                    \
    }


#define GENERIC_INIT(type, prefix)                          \
    static __unused__ inline DO_INIT(type, prefix)
#define GENERIC_NEW(type, prefix)                           \
    static __unused__ inline DO_NEW(type, prefix)
#define GENERIC_NEW_INIT(type, prefix)                      \
    GENERIC_INIT(type, prefix)                              \
    static __unused__ inline DO_NEW0(type, prefix)

#define GENERIC_WIPE(type, prefix)                          \
    static __unused__ inline DO_WIPE(type, prefix)
#define GENERIC_DELETE(type, prefix)                        \
    static __unused__ inline DO_DELETE(type, prefix)
#define GENERIC_WIPE_DELETE(type, prefix)                   \
    GENERIC_WIPE(type, prefix)                              \
    GENERIC_DELETE(type, prefix)

#define GENERIC_FUNCTIONS(type, prefix) \
    GENERIC_NEW_INIT(type, prefix)      \
    GENERIC_WIPE_DELETE(type, prefix)


#define DO_MP_NEW(mp, type, prefix)                                          \
    __attribute__((malloc)) type * nonnull prefix##_new(void) {              \
        return prefix##_init(mp_new_raw(mp, type, 1));                       \
    }
#define DO_MP_NEW0(mp, type, prefix)                                         \
    __attribute__((malloc)) type * nonnull prefix##_new(void) {              \
        return mp_new(mp, type, 1);                                          \
    }

#define DO_MP_DELETE(mp, type, prefix)                                       \
    void prefix##_delete(type * nullable * nonnull var) {                    \
        if (*(var)) {                                                        \
            prefix##_wipe(*var);                                             \
            mp_delete(mp, var);                                              \
        }                                                                    \
    }

#define GENERIC_MP_NEW(mp, type, prefix)      \
    static __unused__ inline DO_MP_NEW(mp, type, prefix)
#define GENERIC_MP_NEW_INIT(mp, type, prefix) \
    GENERIC_INIT(type, prefix)                \
    static __unused__ inline DO_MP_NEW0(mp, type, prefix)

#define GENERIC_MP_DELETE(mp, type, prefix)   \
    static __unused__ inline DO_MP_DELETE(mp, type, prefix)
#define GENERIC_MP_WIPE_DELETE(mp, type, prefix) \
    GENERIC_WIPE(type, prefix)                   \
    GENERIC_MP_DELETE(mp, type, prefix)

#define GENERIC_MP_FUNCTIONS(mp, type, prefix) \
    GENERIC_MP_NEW_INIT(mp, type, prefix)      \
    GENERIC_MP_WIPE_DELETE(mp, type, prefix)

/* }}} */
/* Instrumentation {{{ */

enum mem_tool {
    MEM_TOOL_VALGRIND = 1 << 0,
    MEM_TOOL_ASAN     = 1 << 1,

    MEM_TOOL_ANY      = 0xffffffff,
};

#ifndef NDEBUG
#  if defined(__VALGRIND_MAJOR__) && defined(__VALGRIND_MINOR__)
#    define __HAS_VALGRIND_VERSION  1
#  else
#    define __HAS_VALGRIND_VERSION  0
#  endif
#  define __VALGRIND_PREREQ(x, y)  \
    (__HAS_VALGRIND_VERSION && \
     (__VALGRIND_MAJOR__ << 16) + __VALGRIND_MINOR__ >= (((x) << 16) + (y)))
#else
#  define __VALGRIND_PREREQ(x, y)   0
#  define RUNNING_ON_VALGRIND       false
#endif

#ifndef NDEBUG

__leaf __attribute__((const))
bool mem_tool_is_running(unsigned tools);

__leaf
void mem_tool_allow_memory(const void * nonnull mem, size_t len, bool defined);

__leaf
void mem_tool_allow_memory_if_addressable(const void * nonnull mem,
                                          size_t len, bool defined);

__leaf
void mem_tool_disallow_memory(const void * nonnull mem, size_t len);

__leaf
void mem_tool_malloclike(const void * nonnull mem, size_t len, size_t rz,
                         bool zeroed);

__leaf
void mem_tool_freelike(const void * nullable mem, size_t len, size_t rz);

#else

#define mem_tool_is_running(...)                   (false)
#define mem_tool_allow_memory(...)                 e_trace_ignore(0, ##__VA_ARGS__)
#define mem_tool_allow_memory_if_addressable(...)  e_trace_ignore(0, ##__VA_ARGS__)
#define mem_tool_disallow_memory(...)              e_trace_ignore(0, ##__VA_ARGS__)
#define mem_tool_malloclike(...)                   e_trace_ignore(0, ##__VA_ARGS__)
#define mem_tool_freelike(...)                     e_trace_ignore(0, ##__VA_ARGS__)

#endif

/* }}} */

#endif
