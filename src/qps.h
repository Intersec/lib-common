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

#ifndef IS_LIB_COMMON_QPS_H
#define IS_LIB_COMMON_QPS_H

#include <sysexits.h>
#include <lib-common/unix.h>
#include <lib-common/log.h>
#include <lib-common/thr.h>
#include <lib-common/el.h>

/* define this flag to 1 to allow valgrind/asan to potentially detect incorect
 * QPS API usage (WARNING: the QPS spool storage format is not compatible with
 * the standard one) */
#define QPS_USE_REDZONES 0

#define qps_io_wrap(what, ...) \
    ({  typeof(what(__VA_ARGS__)) _res = what(__VA_ARGS__);  \
        if (unlikely(_res < 0))                              \
            qps_enospc(NULL, #what);                         \
        _res; })

#define x_close(...)      qps_io_wrap(close, __VA_ARGS__)
#define x_fdatasync(...)  qps_io_wrap(fdatasync, __VA_ARGS__)
#define x_ftruncate(...)  qps_io_wrap(xftruncate, __VA_ARGS__)
#define x_linkat(...)     qps_io_wrap(linkat, __VA_ARGS__)
#define x_msync(...)      qps_io_wrap(msync, __VA_ARGS__)
#define x_openat(...)     qps_io_wrap(openat, __VA_ARGS__)
#define x_renameat(...)   qps_io_wrap(renameat, __VA_ARGS__)
#define x_write(...)      qps_io_wrap(xwrite, __VA_ARGS__)
#define x_writev(...)     qps_io_wrap(xwritev, __VA_ARGS__)
#define x_pwrite(...)     qps_io_wrap(xpwrite, __VA_ARGS__)
#define x_munmap(...)     qps_io_wrap(munmap, __VA_ARGS__)
#define x_fchmodat(...)   qps_io_wrap(fchmodat, __VA_ARGS__)
#define x_fchmod(...)     qps_io_wrap(fchmod, __VA_ARGS__)

#define x_mmap(...) \
    ({  void *_ptr = mmap(__VA_ARGS__);                      \
        if (unlikely(_ptr == MAP_FAILED))                    \
            qps_enospc(NULL, "mmap");                        \
        _ptr; })

/** \defgroup qkv__ll__qps  Quick Paged Store.
 * \ingroup qkv__ll
 * \brief Quick Paged Store.
 *
 * \{
 *
 * \section qps_intro QPS Principles
 *
 * QPS is an allocator, mostly paged (but not only) meant to allocate small
 * amounts of memory (less than #QPS_ALLOC_MAX octets). Its goal is to provide
 * a persistent snapshotable storage for allocations (see #qps_snapshot).
 *
 * It is up to the user to maintain and deal with its binlog that is able to
 * reconstruct the full state of the memory allocator at any time.
 *
 * \section qps_allocators QPS Allocators
 *
 * QPS is split in two allocators. The first one is a paged allocator
 * (functions qps_pg_*). This allocator returns contiguous sets of pages.
 * Sizes are in pages (of #QPS_PAGE_SIZE octets). The allocation book-keeping
 * is maintained externally and dumped in the QPS meta data (\p meta.qps)
 * every now and then (TODO be a bit more specific about it).
 *
 * The second allocator is a TLSF (http://rtportal.upv.es/rtmalloc/)
 * allocator. Like in TLSF the allocation book-keeping is stored inside the
 * allocator itself. This allocator allocates objects smaller than
 * #QPS_M_ALLOC_MAX and falls back to the paged allocator for larger objects
 * transparently.
 *
 * QPS has a notion of handles for objects allocated with the TLSF allocator.
 * Those are mandatory. Handles is just a boxed pointer, which allow efficient
 * relocation of data for memory defragmentation purposes.
 *
 * \section qps_replay Opening and Creating a QPS
 *
 * QPS offers the functions #qps_exists, #qps_open and #qps_create.  The idiom
 * to reopen a given QPS is often something like:
 *
 * \code
 * #define QPS_PATH   "/some/directory"
 *
 * if (qps_exists(QPS_PATH)) {
 *     qps = qps_open(QPS_PATH, "test-qps");
 * } else {
 *     qps = qps_create(QPS_PATH, "test-qps", 0755);
 * }
 * \endcode
 *
 * Given that QPS lives in a directory, those APIs don't even try to deal with
 * data creation races (there is nothing similar to open() \a O_EXCL flag). If
 * you need such security provided (because you create the QPS in an unsafe
 * location), then it's up to you to ensure that the enclosing directory has
 * been created by you with the proper permissions.
 *
 * \note Do remember that on NFS directory creation isn't atomic.
 */

/** Type of a qps page handle.
 *
 * a #qps_pg_t is actually made of two parts:
 * - the 16 most significat bits are a map index, into qps->maps
 * - the 16 least significant bits are a page index into the map,
 *   0 is reserved and doesn't point to a valid page.
 */
typedef uint32_t           qps_pg_t;
/** the NULL page handle */
#define QPS_PG_NULL        cast(qps_pg_t, 0)
/** format to use in printf() when pretty printing a #qps_pg_t */
#define QPS_PG_FMT      "%d:%04x"
/** format argument to use in printf(), counterpart to #QPS_PG_FMT */
#define QPS_PG_ARG(pg)  ((pg) >> 16), ((pg) & 0xffff)

/** Type of a qps memory handle.
 *
 * a #qps_handle_t is actually a boxed relocatable pointer (#qps_ptr_t).
 */
typedef uint32_t qps_handle_t;
/** The NULL qps handle */
#define QPS_HANDLE_NULL   0U

/** Type of a qps generic relocatable pointer */
typedef struct qps_ptr_t {
    /** offset in the page, should be in [0 .. #QPS_PAGE_SIZE[ */
    uint32_t addr;
    /** page the pointer points into.
     * When this field is NULL, the pointer is invalid.
     */
    qps_pg_t pgno;
} qps_ptr_t;
/** format to use in printf() when pretty printing a #qps_ptr_t */
#define QPS_PTR_FMT     "%d:%04x:%08x"
/** format argument to use in printf(), counterpart to #QPS_PTR_FMT */
#define QPS_PTR_ARG(p)  QPS_PG_ARG((p).pgno), (p).addr

/** Type for caching the result of the dereferencement of a handle.
 */
typedef struct qps_hptr_t {
    void        *data;
    uint32_t     gc_gen;
    qps_handle_t handle;
} qps_hptr_t;

/* qps types, public for inlining reasons {{{ */

typedef struct qps_pghdr_t  qps_pghdr_t;
typedef struct qps_cowhdr_t qps_cowhdr_t;
typedef struct qps_mhdr_t   qps_mhdr_t;
typedef union  qps_map_t    qps_map_t;
typedef struct qps_gcmap_t  qps_gcmap_t;
qvector_t(qps_handle, qps_handle_t);
qvector_t(qps_pg,     qps_pg_t);
qvector_t(qpsm,       qps_map_t *);

static inline int qps_gen_cmp(uint32_t gen1, uint32_t gen2)
{
    return ((int)(gen1 - gen2));
}

/** Compares two QPS generations together.
 * \param gen1  lhs of the comparison
 * \param gen2  rhs of the comparison
 * \param op    the comparison to perform, one of <, <=, ==, >=, >
 * \return      the result of the comparison (bool)
 */
#define QPS_GEN_CMP(gen1, op, gen2)  (qps_gen_cmp(gen1, gen2) op 0)

union qps_map_t {
#define QPS_PAGE_SHIFT   12UL
#define QPS_PAGE_SIZE    (1UL << QPS_PAGE_SHIFT)
#define QPS_PAGE_MASK    (QPS_PAGE_SIZE - 1)

#define QPS_MAP_PAGES    (1UL << 16)
#define QPS_MAP_SHIFT    (16UL + QPS_PAGE_SHIFT)
#define QPS_MAP_SIZE     (1UL << QPS_MAP_SHIFT)
#define QPS_MAP_MASK     (QPS_MAP_SIZE - 1)

    struct {
#define QPS_META_SIG     "QPS_meta/v01.00"
#define QPS_MAP_PG_SIG   "QPS_page/v01.00"
#define QPS_MAP_MEM_SIG  "QPS_tlsf/v01.00"
        uint8_t         sig[16];
        uint32_t        mapno;
        uint32_t        generation;
        uint32_t        allocated;
        uint8_t         __padding[QPS_PAGE_SIZE / 2 - 16 - 3 * 4];

        /* Past this point, data on disk may be corrupted */
        struct qps_t   *qps;
        uint32_t        remaining;      /* only for memory */
        uint32_t        disk_usage;     /* only for memory */
    } hdr;
    uint8_t data[QPS_PAGE_SIZE];
};

struct qps_gcmap_t {
    qps_map_t *map;
    uint32_t   mark;
    uint32_t   gen;
    uint32_t   allocated;
    uint32_t   disk_usage;
};
qvector_t(qps_gcmap, qps_gcmap_t);

#ifdef __has_blocks
typedef void (BLOCK_CARET qps_notify_b)(uint32_t gen);
#else
typedef void *qps_notify_b;
#endif

typedef struct qps_t {
    logger_t logger;
    logger_t tracing_logger;

    dir_lock_t   lock;
    int          dfd;
    uint16_t     snapshotting;
    uint32_t     generation;
    qv_t(qpsm)   maps;
    qv_t(qpsm)   smaps;
    qv_t(qpsm)   omaps;
    qv_t(u32)    no_free;
    qv_t(u32)    no_blocked;
    dlist_t      qps_link;

#define QPS_HANDLES_PAGES   (QPS_HANDLES_COUNT * sizeof(qps_ptr_t) / QPS_PAGE_SIZE)
#define QPS_HANDLES_COUNT   (1U << 16)
    qps_ptr_t  **handles;
    uint32_t     handles_max;
    uint32_t     handles_freelist;
    uint32_t     handles_gc_gen;

    /* Allocator state, private */
    qps_pghdr_t *hdrs;
    qps_map_t   *gc_map;     /* do not use, filled for the SIGBUS handler */
    thr_syn_t   *snapshot_syn; /* not owned by the qps_t */
    el_t         snap_el;
    el_t         snap_timer_el;
    qps_notify_b snap_notify;
    pid_t        snap_pid;
    struct timeval snap_start;
    uint32_t     snap_gen;
    uint32_t     snap_max_duration; /* in seconds, 3600 by default */

    struct {
#define QPS_PGL2_SHIFT       5U
#define QPS_PGL2_LEVELS      bitsizeof(uint32_t)
#define QPS_PGL1_LEVELS      (16UL - QPS_PGL2_SHIFT + 1)
        uint32_t    l1_bitmap;
        uint32_t    l2_bitmap[QPS_PGL1_LEVELS];
        qps_pg_t    blks[QPS_PGL1_LEVELS][QPS_PGL2_LEVELS];
    } pgs;

    struct {
#define QPS_ML2_OFFSET       3U
#define QPS_ML2_SHIFT        5U
#define QPS_ML2_LEVELS       bitsizeof(uint32_t)
#define QPS_ML1_LEVELS       (QPS_MAP_SHIFT + 1 - QPS_ML2_SHIFT - QPS_ML2_OFFSET)
        uint32_t    l1_bitmap;
        uint32_t    l2_bitmap[QPS_ML1_LEVELS];
        qps_mhdr_t *blks[QPS_ML1_LEVELS][QPS_ML2_LEVELS];
    } m;
} qps_t;

__attribute__((noreturn))
extern void qps_enospc(qps_t *nullable qps, const char *what);

/* }}} */
/* qps: file-system/persistent store handling {{{ */

struct qps_stats {
    size_t n_maps;
    size_t n_pages;
    size_t ro_allocs;
    size_t rw_allocs;
    size_t n_pages_free;
    int pages;
    int pages_free;
};

qps_t    *qps_create(const char *path, const char *name, mode_t mode,
                     const void *data, size_t dlen);

qps_t    *_qps_open(const char *path, const char *name,
                    bool load_whole_spool, sb_t *priv);
#define qps_open(path, name, priv)  _qps_open((path), (name), true, (priv))

int       __qps_check_consistency(const char *path, const char *name);
int       __qps_check_maps(qps_t *qps, bool fatal);
bool      qps_exists(const char *path);
int       qps_unlink(const char *path);
void      qps_close(qps_t **qps);
void      qps_get_usage(const qps_t *qps, struct qps_stats *);

#ifdef __has_blocks
uint32_t  qps_snapshot(qps_t *qps, const void *data, size_t dlen,
                       void (BLOCK_CARET notify)(uint32_t gen));
#endif

/** Set a thr syn to use for thr jobs that should synchronize with the
 * snapshots.
 *
 * When set, this syn is awaited before ending a snapshot.
 *
 * Can be used for example if you want to spare CPU time in the main thread
 * because of a big operation, but the operation has to be completed before
 * the end of a potential snapshot:
 *
 * > qps_set_snapshot_syn(my_qps, my_syn);
 * > thr_syn_queue_b(my_syn, _G.my_queue, ^{
 * >     // big operation
 * > });
 */
void qps_set_snapshot_syn(qps_t *qps, thr_syn_t *syn);

/** Backup a qps.
 * This function shall not be called during a snapshot.
 *
 * /param[in] dfd_dst       a descriptor on the destination directory.
 * /param[in] link_as_copy  if true do a hard link rather than a copy.
 *
 * /return 0   if OK
 *         -1  error with arguments
 *         -2  call while snapshotting
 *         -3  error on source
 *         -4  error on destination
 */
int qps_backup(qps_t *qps, int dfd_dst, bool link_as_copy);

/* TODO Document what the QPS handle garbage collector actually does and how.
 */
/** Run the QPS handle garbage collector.
 *
 * Can only be run from main thread otherwise the call will have no effect.
 *
 * \warning it's invalid to run the GC while a snapshot is going on!
 */
void qps_gc_run(qps_t *qps);

void qps_snapshot_wait(qps_t *qps);

/* }}} */
/* qps: Allocation routines {{{ */

/** Maximum amount of memory allocated inside the tlsf pool */
#define QPS_M_ALLOC_MAX   (64U << 10)
/** Largest amount of memory QPS can allocate */
#define QPS_ALLOC_MAX     (32U << 20)
/** Smallest amount of memory QPS will alloc */
#define QPS_ALLOC_MIN     (24)

/**
 * XXX map/alloc/remap/realloc do *NOT* zero memory
 *
 * @param [n] number of manipulated page (each of size QPS_PAGE_SIZE)
 */
qps_pg_t  qps_pg_map(qps_t *qps, size_t n);
qps_pg_t  qps_pg_remap(qps_t *qps, qps_pg_t blk, size_t size);
void      qps_pg_unmap(qps_t *qps, qps_pg_t blk);
void      qps_pg_unload(qps_t *qps, qps_pg_t blk);
size_t    qps_pg_sizeof(qps_t *qps, qps_pg_t blk);
void      qps_pg_zero(qps_t *qps, qps_pg_t blk, size_t n);

void     *qps_alloc(qps_t *qps, qps_handle_t *id, size_t size);
/** Reallocs the memory behind a handle.
 * \warning unlike realloc(), qps_realloc(qps, id, 0) won't free the handle,
 * #qps_free must still be called.
 * \warning unlike realloc(), qps_realloc(qps, QPS_HANDLE_NULL, size) won't
 * do an allocation, use #qps_alloc() to get the handle.
 */
void     *qps_realloc(qps_t *qps, qps_handle_t id, size_t size);
void      qps_free(qps_t *qps, qps_handle_t id);
size_t    qps_sizeof(qps_t *qps, const void *ptr);
void      qps_zero(qps_t *qps, void *ptr, size_t n);

/* }}} */
/* qps: conversions between various kind of pointers {{{ */

static ALWAYS_INLINE
qps_map_t *qps_map_of(const void *ptr)
{
    return cast(qps_map_t *, cast(uintptr_t, ptr) & ~QPS_MAP_MASK);
}

static ALWAYS_INLINE
bool qps_map_is_pg(const qps_map_t *map)
{
    return map->hdr.sig[4] == 'p';
}

static ALWAYS_INLINE
bool qps_is_ro(const qps_t *qps, const qps_map_t *map)
{
    return !qps_map_is_pg(map) && qps->generation != map->hdr.generation;
}
#ifndef __doxygen_mode__
#define qps_is_ro(qps, map)  unlikely(qps_is_ro(qps, map))
#endif

static ALWAYS_INLINE
qps_pg_t qps_pg_of(const void *ptr_)
{
    uintptr_t  ptr = cast(uintptr_t, ptr_);
    qps_map_t *map = qps_map_of(ptr_);
    return (map->hdr.mapno << 16) | (ptr & QPS_MAP_MASK) >> QPS_PAGE_SHIFT;
}

/* Check for broken page number. */
static ALWAYS_INLINE
bool qps_pg_is_in_range(const qps_t *qps, qps_pg_t pg)
{
    int idx = pg >> 16;
    return idx < qps->maps.len;
}

static ALWAYS_INLINE
void *qps_pg_deref(const qps_t *qps, qps_pg_t pg)
{
    return pg ? qps->maps.tab[pg >> 16][pg & 0xffff].data : NULL;
}

#if !defined(__doxygen_mode__)
void *qps_w_deref_(qps_t *, qps_handle_t, void *);
#endif
static ALWAYS_INLINE
void *qps_w_deref(qps_t *qps, qps_handle_t id, void *ptr)
{
    return qps_is_ro(qps, qps_map_of(ptr)) ? qps_w_deref_(qps, id, ptr) : ptr;
}

static ALWAYS_INLINE
qps_ptr_t *qps_handle_slot(qps_t *qps, qps_handle_t id)
{
    assert (id && id < qps->handles_max);
    return &qps->handles[id / QPS_HANDLES_COUNT][id % QPS_HANDLES_COUNT];
}

void qps_handle_allow_memory(qps_t *qps, qps_handle_t id, qps_ptr_t *ptr);

static ALWAYS_INLINE
void *qps_handle_deref(qps_t *qps, qps_handle_t id)
{
    qps_ptr_t *ptr = qps_handle_slot(qps, id);

    assert ((ptr->addr & ~QPS_PAGE_MASK) == 0);
#if QPS_USE_REDZONES
    qps_handle_allow_memory(qps, id, ptr);
#endif
    return (uint8_t *)qps_pg_deref(qps, ptr->pgno) + ptr->addr;
}

static ALWAYS_INLINE
void *qps_handle_w_deref(qps_t *qps, qps_handle_t id)
{
    return qps_w_deref(qps, id, qps_handle_deref(qps, id));
}

static ALWAYS_INLINE
void *qps_hptr_init(qps_t *qps, qps_handle_t h, qps_hptr_t *cache)
{
    cache->handle = h;
    cache->data   = qps_handle_deref(qps, h);
    cache->gc_gen = qps->handles_gc_gen;
    return cache->data;
}

static ALWAYS_INLINE
const void *qps_hptr_deref(qps_t *qps, qps_hptr_t *cache)
{
    if (unlikely(qps->handles_gc_gen != cache->gc_gen)) {
        cache->data   = qps_handle_deref(qps, cache->handle);
        cache->gc_gen = qps->handles_gc_gen;
    }
    return cache->data;
}

static ALWAYS_INLINE
void *qps_hptr_w_deref(qps_t *qps, qps_hptr_t *cache)
{
    qps_hptr_deref(qps, cache);
    return cache->data = qps_w_deref(qps, cache->handle, cache->data);
}

static ALWAYS_INLINE
void *qps_hptr_alloc(qps_t *qps, size_t n, qps_hptr_t *cache)
{
    cache->gc_gen = qps->handles_gc_gen;
    return cache->data = qps_alloc(qps, &cache->handle, n);
}

static ALWAYS_INLINE
void *qps_hptr_realloc(qps_t *qps, size_t n, qps_hptr_t *cache)
{
    cache->data   = RETHROW_P(qps_realloc(qps, cache->handle, n));
    cache->gc_gen = qps->handles_gc_gen;
    return cache->data;
}

static ALWAYS_INLINE
void qps_hptr_free(qps_t *qps, qps_hptr_t *cache)
{
    qps_free(qps, cache->handle);
    p_clear(cache, 1);
}

/* }}} */

/** \brief Initialize the QPS module.
 *
 * qps_initialize calls sigaction() to hook the SIGSEGV and SIGBUGS signal for
 * internal processing and will forward signals caught that way that aren't
 * expected to the handler (if any). If you hook the SIGSEGV or SIGBUS signal
 * before calling qps_initialize this will be overwritten, if you do it after
 * QKV won't be functionnal.
 *
 * the sighandler callback is meant to be chained with similar processings
 * (to catch pagefaults and recover from them) from other libraries, though
 * it's expected that this callbacks either:
 * - fixes the issue and returns ;
 * - chainloads to a similar callback (with the same expectations);
 * - unhooks the SIGSEGV or SIGBUS signal and raises the condition again
 *   using code like:
 *   \code
 *   signal(SIGSEGV, SIG_DFL);
 *   raise(SIGSEGV);
 *   \endcode
 *
 * \param[in]  sighandler  a segfault/sigbus handler.
 */
MODULE_DECLARE(qps);

/* leak checker {{{ */

typedef struct qps_roots_t {
    qv_t(qps_handle) handles;
    qv_t(qps_pg)     pages;
} qps_roots_t;
GENERIC_NEW_INIT(qps_roots_t, qps_roots);

static inline
void qps_roots_wipe(qps_roots_t *roots)
{
    qv_wipe(&roots->handles);
    qv_wipe(&roots->pages);
}
GENERIC_DELETE(qps_roots_t, qps_roots);

/** Check QPS for pages and handles non referenced in roots.
 *
 * How to use it ? The user layer should list all the QPS pages and handled it
 * uses, then call \p qps_check_leaks().
 *
 * \return A negative value in case of leak detection.
 */
int qps_check_leaks(qps_t *qps, qps_roots_t *roots);

/* }}} */
/** \} */
#endif
