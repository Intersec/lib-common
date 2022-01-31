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

#include <lib-common/container-qvector.h>
#include <lib-common/arith.h>
#include <lib-common/qpage.h>

/*
 * QPage allocator is inspired by qtlsf (look at qtlsf.[hc] for explanations).
 *
 * The mechanism used to find allocation slots in O(1) is directly inherited
 * from tlsf.  Unlike tlsf, as the page linking cannot be embedded inside the
 * pages (since we care about page alignment a lot), allocation returns an
 * extra cookie, called the segment.
 *
 * Caller should try to remember that segment as it makes deallocation
 * an O(1) operation too, whereas it's not a O(1) if the proper segment
 * is not provided (it's even quite slow).
 *
 * Unlike tlsf though, the page allocator provides mmap/unmap-like APIs: you
 * can allocate many pages at once, and then dealloc some of the pages
 * independently.  This allows for example mapping all the pages that will be
 * required to build a QDB column, and allows those pages to be unmapped
 * individually (think of qdb_bitmaps who are doing that often).
 *
 * Also, unlike tlsf, we try to release memory under some circumstances. We
 * don't release the addressing space, but we do release physical memory to
 * the system.  We also remember page dirtyness to some extent, making
 * allocating clean pages quite efficient.
 *
 * All these features have a cost, and the qpage allocator is likely to be
 * slower than qtlsf, but OTOH you allocate QPAGE_SIZE bytes at a time,
 * which will likely not done a lot of times per second ;) I don't expect it to
 * be a lot more than twice as slow as qtlsf, which makes it in the 100ns
 * range per allocation (for raw pages).
 */

#define QPAGE_DEBUG 0
#if QPAGE_DEBUG
# if __GNUC_PREREQ(4, 4)
#  pragma GCC optimize ("-fno-inline", "-fno-inline-functions")
# endif
#else
# undef  assert
# define assert(...)  do { } while (0)
#endif

#define CLASSES_SHIFT  (4U)
#define CLASS_SMALL    (1U << (CLASSES_SHIFT + 1))
#define CLASSES        (1 << (QPAGE_COUNT_BITS - CLASSES_SHIFT + 1))

#define QDB_MADVISE_THRESHOLD  (1 << (20 - QPAGE_SHIFT))
#define QPAGE_ALLOC_MIN        (16 << (20 - QPAGE_SHIFT))

typedef struct qpage_t {
    uint8_t data[QPAGE_SIZE];
} qpage_t;

typedef struct page_desc_t {
    bool     dirty : 1;    /* only makes sense if the block is free */
    uint32_t flags : 31;
    uint32_t blkno;

    struct page_desc_t **free_prev_next;
    union {
        struct page_desc_t  *free_next;
        uint32_t             blk_prev;
    };
} page_desc_t;

qvector_t(pgd, page_desc_t *);

typedef struct page_run_t {
    qpage_t     *mem_pages;
    uint32_t     npages;
    uint32_t     segment;
    page_desc_t  pages[];
} page_run_t;

static struct {
#define BITS_LEN  BITS_TO_ARRAY_LEN(size_t, CLASSES)
    size_t       *bits; /* array of BITS_LEN elements. */
    page_desc_t **blks; /* array of CLASSES elements. */
    qv_t(pgd)     segs;
    spinlock_t    lock;
} qpages_g;
#define _G  qpages_g

#define BLK_STATE       0x70000000
#define BLK_PGINRUN     0x40000000 /* only makes sense if the block is used */

#define BLK_FREE        0x20000000
#define BLK_USED        0x00000000

#define BLK_PREV_FREE   0x10000000
#define BLK_PREV_USED   0x00000000


static ALWAYS_INLINE uint32_t mapping_class_upper(uint32_t npages)
{
    uint32_t level, mask;

    if (npages <= CLASS_SMALL)
        return npages - 1;

    mask    = (1U << (bsr32(npages) - CLASSES_SHIFT)) - 1;
    npages += mask;
    level   = bsr32(npages) - CLASSES_SHIFT;
    return (npages >> level) + (level << CLASSES_SHIFT) - 1;
}

static ALWAYS_INLINE uint32_t mapping_class(uint32_t npages)
{
    uint32_t level;

    if (npages <= CLASS_SMALL)
        return npages - 1;

    level = bsr32(npages) - CLASSES_SHIFT;
    return (npages >> level) + (level << CLASSES_SHIFT) - 1;
}

static ALWAYS_INLINE page_desc_t *find_suitable_block(uint32_t *class)
{
    unsigned vec  = *class / bitsizeof(size_t);
    size_t   mask = BITMASK_GE(size_t, *class);

    do {
        size_t tmp = _G.bits[vec] & mask;
        if (tmp) {
            *class = vec * bitsizeof(size_t) + bsfsz(tmp);
            return _G.blks[*class];
        }
        mask = (size_t)-1;
    } while (++vec < BITS_LEN);

    return NULL;
}


static ALWAYS_INLINE uint32_t blk_no(page_desc_t *desc)
{
    return desc->blkno;
}

static ALWAYS_INLINE page_run_t *run_of(page_desc_t *desc, uint32_t blkno)
{
    return container_of(desc, page_run_t, pages[blkno]);
}

static ALWAYS_INLINE uint32_t blk_size(page_desc_t *desc)
{
    return desc->flags & ~BLK_STATE;
}

static ALWAYS_INLINE page_desc_t *blk_next(page_desc_t *desc, uint32_t size)
{
    return desc + size;
}

static ALWAYS_INLINE page_desc_t *blk_get_prev(page_desc_t *blk)
{
    assert ((blk->flags & BLK_PREV_FREE) != 0);
    return blk - blk->blk_prev;
}

#if QPAGE_DEBUG
static void qpages_check(page_run_t *run)
{
    uint32_t j = 0;

    spin_lock(&_G.lock);
    for (j = 0; j < run->npages + 1; j++) {
        uint32_t flags = run->pages[j].flags;

        if ((flags & BLK_FREE) && (flags & BLK_PREV_FREE))
            e_panic("wrong flags for %p:%d", run, j);
    }

    for (j = 0; j < run->npages; ) {
        page_desc_t *blk  = run->pages + j;
        uint32_t     bsz  = blk_size(blk);
        page_desc_t *next = blk_next(blk, bsz);

        if (blk->flags & BLK_FREE) {
            if (next->flags & BLK_FREE) {
                e_panic("two consecutive free blocks: %d, %d",
                        j, blk_no(next));
            }
            if (!(next->flags & BLK_PREV_FREE)) {
                e_panic("missed that previous block is free [%d from %d]",
                        j, blk_no(next));
            }
            if (blk_get_prev(next) != blk) {
                e_panic("previous free blk offset is wrong");
            }
        } else {
            if (next->flags & BLK_PREV_FREE) {
                e_panic("next block believe we're free %d, %d",
                        j, blk_no(next));
            }
        }
        j += bsz;
    }
    spin_unlock(&_G.lock);
}
#else
#  define qpages_check(run)  do { } while (0)
#endif

static inline void blk_insert(page_desc_t *blk, size_t npages)
{
    uint32_t class = mapping_class(npages);

    blk->flags = npages | BLK_PREV_USED | BLK_FREE;
    if ((blk->free_next = _G.blks[class])) {
        blk->free_next->free_prev_next = &blk->free_next;
    } else {
        SET_BIT(_G.bits, class);
    }
    *(blk->free_prev_next = &_G.blks[class]) = blk;

    blk[npages].flags    |= BLK_PREV_FREE;
    blk[npages].blk_prev  = npages;
}

static inline uint32_t blk_remove(page_desc_t *blk)
{
    uint32_t npages = blk_size(blk);
    uint32_t class  = mapping_class(npages);

    assert (blk->flags & BLK_FREE);
    *blk->free_prev_next = blk->free_next;
    if (blk->free_next) {
        blk->free_next->free_prev_next = blk->free_prev_next;
    } else
    if (blk->free_prev_next == &_G.blks[class]) {
        RST_BIT(_G.bits, class);
    }
    return npages;
}

static inline void blk_set_clean(page_desc_t *blk, uint32_t npages)
{
    while (npages-- > 0)
        (blk++)->dirty = false;
}

static inline void blk_set_dirty(page_desc_t *blk, uint32_t npages)
{
    while (npages-- > 0)
        (blk++)->dirty = true;
}

static inline void
blk_cleanse(page_run_t *run, uint32_t blkno, uint32_t npages)
{
    uint32_t end = npages + blkno;

    for (uint32_t i = blkno; i < end; i++) {
        if (run->pages[i].dirty) {
            run->pages[i].dirty = false;
            p_clear(run->mem_pages + i, 1);
        }
    }
}

static inline void
blk_setup_backptrs(page_desc_t *blk, uint32_t flags, uint32_t npages)
{
    blk->flags = flags | BLK_USED | npages;
    for (uint32_t i = 1; i < npages; i++) {
        blk[i].flags = BLK_PREV_USED | BLK_USED | BLK_PGINRUN | i;
    }
}

static inline uint32_t blk_cut(page_desc_t *blk)
{
    uint32_t offs = blk->flags & ~BLK_STATE;
    page_desc_t *tmp;
    uint32_t tsz, bsz;

    tmp = blk - offs;
    tsz = blk_size(tmp);
    assert (tsz >= offs);
    tmp->flags = (tmp->flags & BLK_STATE) | offs;

    blk_setup_backptrs(blk, BLK_PREV_USED, bsz = tsz - offs);
    return bsz;
}

static NEVER_INLINE int create_arena(size_t npages)
{
    size_t pgsize = getpagesize();
    size_t size, offset;
    page_desc_t *blk, *end;
    page_run_t *run;
    qpage_t *pgs;

    if (npages < QPAGE_ALLOC_MIN) {
        npages = QPAGE_ALLOC_MIN;
    } else {
        if (npages & (npages - 1))
            npages = 1U << (bsr32(npages) + 1);
    }
    size = npages * QPAGE_SIZE;
    if (QPAGE_SIZE > pgsize)
        size += QPAGE_SIZE;
    pgs  = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (pgs == MAP_FAILED)
        return -1;
    mem_tool_disallow_memory(pgs, size);

    run = calloc(1, sizeof(page_run_t) + (npages + 1) * sizeof(page_desc_t));
    if (run == NULL) {
        mem_tool_allow_memory(pgs, size, true);
        munmap(pgs, size);
        return -1;
    }
    if (QPAGE_SIZE > pgsize) {
        offset = (uintptr_t)pgs & QPAGE_MASK;
        if (offset) {
            npages--;
            mem_tool_allow_memory(pgs, QPAGE_SIZE - offset, true);
            munmap(pgs, QPAGE_SIZE - offset);
            pgs = (qpage_t *)((uintptr_t)pgs + QPAGE_SIZE - offset);
            mem_tool_allow_memory(pgs + npages, offset, true);
            munmap(pgs + npages, offset);
        } else {
            mem_tool_allow_memory(pgs + npages, QPAGE_SIZE, true);
            munmap(pgs + npages, QPAGE_SIZE);
        }
    }
    run->mem_pages = pgs;
    run->npages    = npages;
    run->segment   = _G.segs.len;
    qv_append(&_G.segs, run->pages);
    for (uint32_t i = 0; i <= npages; i++) {
        run->pages[i].blkno = i;
    }

    blk = run->pages + 0;
    end = run->pages + npages;

    blk->flags = BLK_PREV_USED | BLK_FREE | npages;
    end->flags = BLK_PREV_FREE | BLK_USED;
    blk_insert(blk, npages);
    return 0;
}

/**************************************************************************/
/* Public stuff                                                           */
/**************************************************************************/

static void
free_n(page_run_t *run, page_desc_t *blk, size_t npages, uint32_t seg)
{
    uint32_t blkno = blk_no(blk);
    page_desc_t *tmp;
    size_t bsz;

    spin_lock(&_G.lock);
    if (blk->flags & BLK_PGINRUN) {
        bsz = blk_cut(blk);
    } else {
        bsz = blk_size(blk);
    }

    assert (bsz >= npages);
    if (bsz > npages) {
        blk_cut(blk + npages);
        bsz = npages;
    } else {
        tmp = blk_next(blk, bsz);
        if (tmp->flags & BLK_FREE) {
            bsz += blk_remove(tmp);
        }
    }
    if (blk->flags & BLK_PREV_FREE) {
        blk  = blk_get_prev(blk);
        bsz += blk_remove(blk);
    }
    blk_insert(blk, bsz);

    if (bsz == run->npages) {
#ifdef __linux__
        madvise(run->mem_pages, bsz * QPAGE_SIZE, MADV_DONTNEED);
#else
        mmap(run->mem_pages, bsz * QPAGE_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
#endif
        blk_set_clean(run->pages, bsz);
        mem_tool_disallow_memory(run->mem_pages, bsz * QPAGE_SIZE);
    } else {
        /** Divide virtually the array of run->pages into chunk of size of
         * QDB_MADVISE_THRESHOLD.
         *
         *          (¹)   blocks to free
         *           ↓↓     ↓      ↓
         *     ┌───────┬───────┬───────┬───────┬───────┐
         *     │ .   dd│fffffff│fffff  │  .    │       │
         *     └───────┴───────┴───────┴───────┴───────┘
         *       ↑                        ↑
         *    previous                  next
         *   free block               free block
         *
         * When freeing last used blocks in a chunk, previous free block
         * (blk) and next free block (blk + bsz) will point outside these
         * chunk and we can call madvise.
         * If it is not the last used blocks in the chunk, we still need to
         * call blk_set_dirty for these blocks (see (¹)).
         */
        size_t chunk_begin;
        size_t chunk_end;

        /* find the first free chunk */
        chunk_begin = ROUND(blkno, QDB_MADVISE_THRESHOLD);
        if (chunk_begin <= blk_no(blk)) {
            chunk_begin += QDB_MADVISE_THRESHOLD;
        }
        /* find the next non-free chunk */
        chunk_end = ROUND_UP(blkno + npages, QDB_MADVISE_THRESHOLD);
        if (chunk_end > blk_no(blk) + bsz) {
            chunk_end -= QDB_MADVISE_THRESHOLD;
        }

        if (chunk_begin < chunk_end) {
            const size_t chunk_sz = chunk_end - chunk_begin;

#ifdef __linux__
            madvise(run->mem_pages + chunk_begin, chunk_sz * QPAGE_SIZE,
                    MADV_DONTNEED);
#else
            mmap(run->mem_pages + chunk_begin, chunk_sz * QPAGE_SIZE,
                 PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED,
                 -1, 0);
#endif
            blk_set_clean(run->pages + chunk_begin, chunk_sz);
            mem_tool_disallow_memory(run->mem_pages + chunk_begin,
                                     chunk_sz * QPAGE_SIZE);
            if (blkno < chunk_begin) {
                blk_set_dirty(run->pages + blkno, (chunk_begin - blkno));
                mem_tool_disallow_memory(run->mem_pages + blkno,
                                         (chunk_begin - blkno) * QPAGE_SIZE);
            }
            if (blkno + npages > chunk_end) {
                blk_set_dirty(run->pages + chunk_end,
                              (blkno + npages - chunk_end));
                mem_tool_disallow_memory(run->mem_pages + chunk_end,
                    (blkno + npages - chunk_end) * QPAGE_SIZE);
            }
        } else {
            blk_set_dirty(run->pages + blkno, npages);
            mem_tool_disallow_memory(run->mem_pages + blkno,
                                     npages * QPAGE_SIZE);
        }
    }

    spin_unlock(&_G.lock);
    qpages_check(run);
}

static page_desc_t *
qpage_alloc_align_impl(size_t npages, size_t shift, bool zero, page_run_t **runp)
{
    page_desc_t *blk, *next, *split;
    uint32_t class, blkno, size, offs;
    uint32_t smask = (1U << shift) - 1;
    page_run_t *run;

    if (unlikely(npages == 0 || npages + smask > QPAGE_COUNT_MAX))
        return NULL;

    spin_lock(&_G.lock);
    class = mapping_class_upper(npages + smask);
    blk   = find_suitable_block(&class);
    if (unlikely(!blk)) {
        if (create_arena(npages + smask) < 0) {
            spin_unlock(&_G.lock);
            return NULL;
        }
        class = mapping_class_upper(npages + smask);
        blk   = find_suitable_block(&class);
    }

    if ((_G.blks[class] = blk->free_next)) {
        blk->free_next->free_prev_next = &_G.blks[class];
    } else {
        RST_BIT(_G.bits, class);
    }

    size  = blk_size(blk);
    next  = blk_next(blk, size);
    blkno = blk_no(blk);
    run   = run_of(blk, blkno);
    offs  = (((uintptr_t)run->mem_pages >> QPAGE_SHIFT) + blkno) & smask;

    if (offs) {
        offs   = smask + 1 - offs;
        blk_insert(blk, offs);
        blk   += offs;
        blkno += offs;
        size  -= offs;
        blk_setup_backptrs(blk, BLK_PREV_FREE, npages);
    } else {
        blk_setup_backptrs(blk, BLK_PREV_USED, npages);
    }
    if (size > npages) {
        split = blk_next(blk, npages);
        blk_insert(split, size - npages);
    } else {
        assert (size == npages);
        next->flags &= ~BLK_PREV_FREE;
    }

    mem_tool_allow_memory(run->mem_pages + blk_no(blk), npages * QPAGE_SIZE,
                          zero);
    if (zero) {
        blk_cleanse(run, blkno, npages);
    }
    spin_unlock(&_G.lock);
    *runp = run;
    qpages_check(run);
    return blk;
}

void *qpage_allocraw_align(size_t npages, size_t shift, uint32_t *seg)
{
    page_run_t  *run;
    page_desc_t *blk;

    blk = RETHROW_P(qpage_alloc_align_impl(npages, shift, false, &run));
    if (seg)
        *seg = run->segment;
    return run->mem_pages + blk_no(blk);
}

void *qpage_alloc_align(size_t npages, size_t shift, uint32_t *seg)
{
    page_run_t  *run;
    page_desc_t *blk;

    blk = RETHROW_P(qpage_alloc_align_impl(npages, shift, true, &run));
    if (seg)
        *seg = run->segment;
    return run->mem_pages + blk_no(blk);
}

static inline int32_t qpage_find_seg(qpage_t *qp)
{
    spin_lock(&_G.lock);
    for (int i = 0; i < _G.segs.len; i++) {
        page_desc_t *blk = _G.segs.tab[i];
        page_run_t  *run = run_of(blk, blk_no(blk));

        if ((size_t)(qp - run->mem_pages) < run->npages) {
            spin_unlock(&_G.lock);
            return i;
        }
    }
    e_panic("invalid pointer or segment");
}

static void *remap(void *ptr, size_t old_n, uint32_t old_seg,
                   uint32_t new_n, uint32_t *new_seg,
                   bool may_move, bool zero)
{
    qpage_t *qp = ptr;
    page_desc_t *blk, *tmp, *next;
    page_run_t *run;
    uint32_t bsz;

    if (unlikely(old_seg > (uint32_t)_G.segs.len)) {
        old_seg = qpage_find_seg(qp);
    }
    tmp   = _G.segs.tab[old_seg];
    run   = run_of(tmp, blk_no(tmp));
    blk   = run->pages + (qp - run->mem_pages);
    assert (blk + old_n <= run->pages + run->npages);
    assert (!(blk->flags & BLK_FREE));
    bsz   = blk_size(blk);
    assert (old_n <= bsz);

    if (new_n <= bsz) {
        if (new_n < bsz)
            free_n(run, blk + new_n, bsz - new_n, old_seg);
        if (new_seg)
            *new_seg = old_seg;
        return ptr;
    }

    spin_lock(&_G.lock);
    next = blk_next(blk, bsz);
    if ((next->flags & BLK_FREE) && new_n <= bsz + blk_size(next)) {
        bsz += blk_remove(next);
        if (bsz > new_n) {
            tmp = blk_next(blk, new_n);
            blk_insert(tmp, bsz - new_n);
        } else {
            next->flags &= ~BLK_PREV_FREE;
        }
        blk_setup_backptrs(blk, (blk->flags & BLK_PREV_FREE), new_n);
        spin_unlock(&_G.lock);
        mem_tool_allow_memory(qp + old_n, (new_n - old_n) * QPAGE_SIZE, zero);

        if (zero) {
            p_clear(qp + old_n, bsz - old_n);
            blk_cleanse(run, blk_no(blk) + bsz, new_n - bsz);
        }
        if (new_seg)
            *new_seg = old_seg;
        qpages_check(run);
        return ptr;
    }
    spin_unlock(&_G.lock);

    if (may_move) {
        page_run_t  *new_run;
        page_desc_t *new_blk;
        uint32_t     blkno;
        qpage_t     *res;

        new_blk = qpage_alloc_align_impl(new_n, 0, false, &new_run);
        if (!new_blk)
            return NULL;

        blkno = blk_no(new_blk);
        res   = new_run->mem_pages + blkno;
        memcpy(res, ptr, old_n * QPAGE_SIZE);
        free_n(run, blk, bsz, old_seg);

        if (zero) {
            p_clear(res + old_n, bsz - old_n);
            blk_cleanse(new_run, blkno + bsz, new_n - bsz);
        }
        if (new_seg)
            *new_seg = new_run->segment;
        return res;
    }

    return NULL;
}

void *qpage_remap_raw(void *ptr, size_t old_n, uint32_t old_seg,
                      uint32_t new_n, uint32_t *new_seg, bool may_move)
{
    return remap(ptr, old_n, old_seg, new_n, new_seg, may_move, false);
}

void *qpage_remap(void *ptr, size_t old_n, uint32_t old_seg,
                  uint32_t new_n, uint32_t *new_seg, bool may_move)
{
    return remap(ptr, old_n, old_seg, new_n, new_seg, may_move, true);
}

void qpage_free_n(void *ptr, size_t npages, uint32_t seg)
{
    qpage_t *qp = ptr;
    page_desc_t *blk, *tmp;
    page_run_t *run;

    if (!ptr)
        return;

    if (unlikely(seg > (uint32_t)_G.segs.len)) {
        seg = qpage_find_seg(qp);
    }
    tmp   = _G.segs.tab[seg];
    run   = run_of(tmp, blk_no(tmp));
    blk   = run->pages + (qp - run->mem_pages);
    assert (blk + npages <= run->pages + run->npages);
    assert (!(blk->flags & BLK_FREE));
    free_n(run, blk, npages, seg);
}

void *qpage_dup_n(const void *ptr, size_t n, uint32_t *seg)
{
    qpage_t *res = qpage_allocraw_n(n, seg);

    if (likely(res))
        memcpy(res, ptr, n * QPAGE_SIZE);
    return res;
}

static int qpage_initialize(void *arg)
{
    p_clear(&_G, 1);
    _G.bits = p_new(size_t, BITS_LEN);
    _G.blks = p_new(page_desc_t *, CLASSES);
    return 0;
}

static int qpage_shutdown(void)
{
    for (int i = 0; i < _G.segs.len; i++) {
        free(run_of(_G.segs.tab[i], 0));
    }
    qv_wipe(&_G.segs);
    p_delete(&_G.bits);
    p_delete(&_G.blks);
    return 0;
}

MODULE_BEGIN(qpage)
MODULE_END()
