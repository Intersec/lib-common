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

#include <lib-common/core.h>
#include <lib-common/el.h>
#include <lib-common/log.h>
#include <lib-common/str-buf-pp.h>

#ifdef MEM_BENCH

#include "mem-bench.h"
#include <lib-common/thr.h>

#define WRITE_PERIOD 256
#endif

static struct {
    logger_t logger;

    dlist_t all_pools;
    spinlock_t all_pools_lock;
} core_mem_fifo_g = {
#define _G  core_mem_fifo_g
    .logger = LOGGER_INIT_INHERITS(NULL, "core-mem-fifo"),
    .all_pools = DLIST_INIT(_G.all_pools),
};

typedef struct mem_page_t {

    uint32_t used_size;
    uint32_t used_blocks;
    size_t   size;
    void    *last;           /* last result of an allocation */

    byte __attribute__((aligned(8))) area[];
} mem_page_t;

typedef struct mem_block_t {
    uint32_t page_offs;
    uint32_t blk_size;
    byte     area[];
} mem_block_t;

typedef struct mem_fifo_pool_t {
    mem_pool_t  funcs;
    union {
        mem_page_t *freepage;
        mem_pool_t **owner;
    };
    mem_page_t  *current;
    size_t      occupied:63;
    bool        alive:1;
    size_t      map_size;
    uint32_t    page_size;
    uint32_t    nb_pages;

    char       *name;
    dlist_t     pool_list;

#ifdef MEM_BENCH
    /* Instrumentation */
    mem_bench_t  mem_bench;
#endif

} mem_fifo_pool_t;

static ALWAYS_INLINE mem_page_t *pageof(mem_block_t *blk)
{
    return (mem_page_t *)((char *)blk - blk->page_offs);
}

static mem_page_t *mem_page_new(mem_fifo_pool_t *mfp, uint32_t minsize)
{
    mem_page_t *page = mfp->freepage;
    uint32_t mapsize;

    if (page && page->size >= minsize) {
        mfp->freepage = NULL;
        return page;
    }

    if (minsize < mfp->page_size - sizeof(mem_page_t)) {
        mapsize = mfp->page_size;
    } else {
        mapsize = ROUND_UP(minsize + sizeof(mem_page_t), PAGE_SIZE);
    }

    page = (mem_page_t *) pa_new(byte, mapsize, 8);

    page->size  = mapsize - sizeof(mem_page_t);
    mem_tool_disallow_memory(page->area, page->size);
    mfp->nb_pages++;
    mfp->map_size   += mapsize;

#ifdef MEM_BENCH
    mfp->mem_bench.malloc_calls++;
    mfp->mem_bench.current_allocated += mapsize;
    mfp->mem_bench.total_allocated += mapsize;
    mem_bench_update(&mfp->mem_bench);
    mem_bench_print_csv(&mfp->mem_bench);
#endif

    return page;
}

static void mem_page_reset(mem_page_t *page)
{
    mem_tool_allow_memory(page->area, page->used_size, false);
    p_clear(page->area, page->used_size);
    mem_tool_disallow_memory(page->area, page->size);

    page->used_blocks = 0;
    page->used_size   = 0;
    page->last        = NULL;
}

static void mem_page_delete(mem_fifo_pool_t *mfp, mem_page_t **pagep)
{
    mem_page_t *page = *pagep;

    if (page) {
#ifdef MEM_BENCH
        mfp->mem_bench.current_allocated -= page->size + sizeof(mem_page_t);
        mem_bench_update(&mfp->mem_bench);
        mem_bench_print_csv(&mfp->mem_bench);
#endif

        mfp->nb_pages--;
        mfp->map_size -= page->size + sizeof(mem_page_t);
        mem_tool_allow_memory(page, page->size + sizeof(mem_page_t), true);
        p_delete(pagep);
    }
}

static uint32_t mem_page_size_left(mem_page_t *page)
{
    return (page->size - page->used_size);
}

static void *mfp_alloc(mem_pool_t *_mfp, size_t size, size_t alignment,
                       mem_flags_t flags)
{
    mem_fifo_pool_t *mfp = container_of(_mfp, mem_fifo_pool_t, funcs);
    mem_block_t *blk;
    mem_page_t *page;
    size_t req_size = size;

#ifdef MEM_BENCH
    proctimer_t ptimer;
    proctimer_start(&ptimer);
#endif

    if (alignment > 8) {
        e_panic("mem_fifo_pool does not support alignments greater than 8");
    }

    if (unlikely(size == 0)) {
        return MEM_EMPTY_ALLOC;
    }

    page = mfp->current;
    /* Must round size up to keep proper alignment */
    size = ROUND_UP((unsigned)size + sizeof(mem_block_t), 8);
    if (mem_page_size_left(page) < size) {
#ifndef NDEBUG
        if (unlikely(!mfp->alive)) {
            e_panic("trying to allocate from a dead pool");
        }
#endif
        if (unlikely(!page->used_blocks)) {
            if (page->size >= size) {
                mem_page_reset(page);
            } else {
                mem_page_delete(mfp, &page);
                page = mfp->current = mem_page_new(mfp, size);
            }
        } else {
            page = mfp->current = mem_page_new(mfp, size);
        }
#ifdef MEM_BENCH
        mfp->mem_bench.alloc.nb_slow_path++;
#endif
    }

    blk = acast(mem_block_t, page->area + page->used_size);
    mem_tool_allow_memory(blk, sizeof(*blk), true);
    mem_tool_malloclike(blk->area, req_size, 0, true);
    blk->page_offs = (uintptr_t)blk - (uintptr_t)page;
    blk->blk_size  = size;
    mem_tool_disallow_memory(blk, sizeof(*blk));

    mfp->occupied   += size;
    page->used_size += size;
    page->used_blocks++;

#ifdef MEM_BENCH
    proctimer_stop(&ptimer);
    proctimerstat_addsample(&mfp->mem_bench.alloc.timer_stat, &ptimer);

    mfp->mem_bench.alloc.nb_calls++;
    mfp->mem_bench.current_used = mfp->occupied;
    mfp->mem_bench.total_requested += req_size;
    mem_bench_update(&mfp->mem_bench);
    mem_bench_print_csv(&mfp->mem_bench);
#endif

    return page->last = blk->area;
}

static void mfp_free(mem_pool_t *_mfp, void *mem)
{
    mem_fifo_pool_t *mfp = container_of(_mfp, mem_fifo_pool_t, funcs);
    mem_block_t *blk;
    mem_page_t *page;

#ifdef MEM_BENCH
    proctimer_t ptimer;
    proctimer_start(&ptimer);
#endif

    if (!mem || unlikely(mem == MEM_EMPTY_ALLOC)) {
        return;
    }

    blk  = container_of(mem, mem_block_t, area);
    mem_tool_allow_memory(blk, sizeof(*blk), true);
    page = pageof(blk);
    mfp->occupied -= blk->blk_size;
    mem_tool_freelike(mem, blk->blk_size - sizeof(*blk), 0);
    mem_tool_disallow_memory(blk, sizeof(*blk));

    if (--page->used_blocks > 0) {
#ifdef MEM_BENCH
        proctimer_stop(&ptimer);
        proctimerstat_addsample(&mfp->mem_bench.free.timer_stat, &ptimer);

        mfp->mem_bench.free.nb_calls++;
        mfp->mem_bench.current_used = mfp->occupied;
        mem_bench_update(&mfp->mem_bench);
        mem_bench_print_csv(&mfp->mem_bench);
#endif
        return;
    }

    /* specific case for a dying pool */
    if (unlikely(!mfp->alive)) {
        mem_page_delete(mfp, &page);
        if (mfp->nb_pages == 0)
            p_delete(mfp->owner);
        return;
    }

    /* this was the last block,
     * reset this page unless it is the current one
     */
    if (page != mfp->current) {
        /* keep the page around if we have none kept around yet */
        if (mfp->freepage) {
            /* if the current page is almost full, better replace the current
             * by this one than deleting it and wasting the freepage soon
             */
            if (page->size >
                8 * (mfp->current->size - mfp->current->used_size))
            {
                if (mfp->current->used_blocks == 0) {
                    mem_page_delete(mfp, &mfp->current);
                }
                mem_page_reset(page);
                mfp->current = page;
            } else
            if (mfp->freepage->size >= page->size) {
                mem_page_delete(mfp, &page);
            } else {
                mem_page_delete(mfp, &mfp->freepage);
                mem_page_reset(page);
                mfp->freepage = page;
            }
        } else {
            mem_page_reset(page);
            mfp->freepage = page;
        }
    }

#ifdef MEM_BENCH
    proctimer_stop(&ptimer);
    proctimerstat_addsample(&mfp->mem_bench.free.timer_stat, &ptimer);

    mfp->mem_bench.free.nb_calls++;
    mfp->mem_bench.free.nb_slow_path++;
    mfp->mem_bench.current_used = mfp->occupied;
    mem_bench_update(&mfp->mem_bench);
    mem_bench_print_csv(&mfp->mem_bench);
#endif
}

static void *mfp_realloc(mem_pool_t *_mfp, void *mem, size_t oldsize,
                         size_t size, size_t alignment, mem_flags_t flags)
{
    mem_fifo_pool_t *mfp = container_of(_mfp, mem_fifo_pool_t, funcs);
    mem_block_t *blk;
    mem_page_t *page;
    bool guessed_size = false;
    size_t alloced_size;
    size_t req_size = size;

#ifdef MEM_BENCH
    proctimer_t ptimer;
    proctimer_start(&ptimer);
#endif

    if (alignment > 8) {
        e_panic("mem_fifo_pool does not support alignments greater than 8");
    }

    if (unlikely(!mfp->alive)) {
        e_panic("trying to reallocate from a dead pool");
    }

    if (unlikely(size == 0)) {
        mfp_free(_mfp, mem);
        return MEM_EMPTY_ALLOC;
    }

    if (!mem || unlikely(mem == MEM_EMPTY_ALLOC)) {
        return mfp_alloc(_mfp, size, alignment, flags);
    }

    blk  = container_of(mem, mem_block_t, area);
    mem_tool_allow_memory(blk, sizeof(*blk), true);
    page = pageof(blk);

    alloced_size = blk->blk_size - sizeof(*blk);
    if ((flags & MEM_RAW) && oldsize == MEM_UNKNOWN) {
        oldsize = alloced_size;
        guessed_size = true;
    }
    assert (oldsize <= alloced_size);
    if (req_size <= alloced_size) {
        mem_tool_freelike(mem, oldsize, 0);
        mem_tool_malloclike(mem, req_size, 0, false);
        mem_tool_allow_memory(mem, MIN(req_size, oldsize), true);
        if (!(flags & MEM_RAW) && oldsize < req_size)
            memset(blk->area + oldsize, 0, req_size - oldsize);
    } else
    /* optimization if it's the last block allocated */
    if (mem == page->last
    && req_size - alloced_size <= mem_page_size_left(page))
    {
        size_t diff;

        size = ROUND_UP((size_t)req_size + sizeof(*blk), 8);
        diff = size - blk->blk_size;
        blk->blk_size    = size;

        mfp->occupied   += diff;
        page->used_size += diff;
        mem_tool_freelike(mem, oldsize, 0);
        mem_tool_malloclike(mem, req_size, 0, false);
        mem_tool_allow_memory(mem, MIN(req_size, oldsize), true);
    } else {
        void *old = mem;

        mem = mfp_alloc(_mfp, size, alignment, flags);

        if (guessed_size) {
            /* XXX we guessed the size from the size of the block, as a
             * consequence, we don't know the exact amount of used memory, and
             * only have an upper bound, this means that some trailing bytes
             * may be part of oldsize but not actually part of the allocation,
             * so we must ensure we are allowed to copy those bytes.
             */
            mem_tool_allow_memory(old, oldsize, true);
        }
        memcpy(mem, old, oldsize);
        mfp_free(_mfp, old);
        return mem;
    }

#ifdef MEM_BENCH
    proctimer_stop(&ptimer);
    proctimerstat_addsample(&mfp->mem_bench.realloc.timer_stat, &ptimer);

    mfp->mem_bench.realloc.nb_calls++;
    mfp->mem_bench.current_used = mfp->occupied;
    mem_bench_update(&mfp->mem_bench);
#endif

    mem_tool_disallow_memory(blk, sizeof(*blk));
    return mem;
}

static mem_pool_t const mem_fifo_pool_funcs = {
    .malloc   = &mfp_alloc,
    .realloc  = &mfp_realloc,
    .free     = &mfp_free,
    .mem_pool = MEM_OTHER,
    .min_alignment = 8
};

mem_pool_t *mem_fifo_pool_new(const char *name, int page_size_hint)
{
    mem_fifo_pool_t *mfp = p_new(mem_fifo_pool_t, 1);

    mfp->name = p_strdup(name);

    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        mfp->funcs = mem_pool_libc;
        return &mfp->funcs;
    }

    STATIC_ASSERT((offsetof(mem_page_t, area) % 8) == 0);
    mfp->funcs     = mem_fifo_pool_funcs;
    mfp->page_size = MAX(16 * PAGE_SIZE,
                         ROUND_UP(page_size_hint, PAGE_SIZE));
    mfp->alive     = true;
    mfp->current   = mem_page_new(mfp, 0);

#ifdef MEM_BENCH
    mem_bench_init(&mfp->mem_bench, LSTR("fifo"), WRITE_PERIOD);
#endif

    spin_lock(&_G.all_pools_lock);
    dlist_add_tail(&_G.all_pools, &mfp->pool_list);
    spin_unlock(&_G.all_pools_lock);

    return &mfp->funcs;
}

void mem_fifo_pool_delete(mem_pool_t **poolp)
{
    mem_fifo_pool_t *mfp;

    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        p_delete(poolp);
        return;
    }

    if (!*poolp) {
        return;
    }

    mfp = container_of(*poolp, mem_fifo_pool_t, funcs);

    spin_lock(&_G.all_pools_lock);
    dlist_remove(&mfp->pool_list);
    spin_unlock(&_G.all_pools_lock);

#ifdef MEM_BENCH
    mem_bench_wipe(&mfp->mem_bench);
#endif

    p_delete(&mfp->name);
    mfp->alive = false;
    mem_page_delete(mfp, &mfp->freepage);
    if (mfp->current && mfp->current->used_blocks == 0) {
        mem_page_delete(mfp, &mfp->current);
    } else {
        mfp->current = NULL;
    }

    if (mfp->nb_pages) {
        e_trace(0, "keep fifo-pool alive: %d pages in use (mem: %lubytes)",
                mfp->nb_pages, (unsigned long) mfp->occupied);
        mfp->owner   = poolp;
        return;
    }
    p_delete(poolp);
}

void mem_fifo_pool_stats(mem_pool_t *mp, ssize_t *allocated, ssize_t *used)
{
    mem_fifo_pool_t *mfp = (mem_fifo_pool_t *)(mp);

    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    /* we don't want to account the 'spare' page as allocated, it's an
       optimization that should not leak. */
    *allocated = mfp->map_size - (mfp->freepage ? mfp->freepage->size : 0);
    *used      = mfp->occupied;
}

void mem_fifo_pool_print_stats(mem_pool_t *mp)
{
#ifdef MEM_BENCH
    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    mem_fifo_pool_t *mfp = container_of(mp, mem_fifo_pool_t, funcs);
    mem_bench_print_human(&mfp->mem_bench, MEM_BENCH_PRINT_CURRENT);
#endif
}

void mem_fifo_pools_print_stats(void)
{
#ifdef MEM_BENCH
    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    spin_lock(&_G.all_pools_lock);
    dlist_for_each(n, &_G.all_pools) {
        mem_fifo_pool_t *mfp = container_of(n, mem_fifo_pool_t, pool_list);
        mem_bench_print_human(&mfp->mem_bench, MEM_BENCH_PRINT_CURRENT);
    }
    spin_unlock(&_G.all_pools_lock);
#endif
}

/* {{{ Module (for print_state method) */

static void core_mem_fifo_print_state(void)
{
    t_scope;
    qv_t(table_hdr) hdr;
    qv_t(table_data) rows;
    table_hdr_t hdr_data[] = { {
            .title = LSTR_IMMED("FIFO POOL NAME"),
        }, {
            .title = LSTR_IMMED("POINTER"),
        }, {
            .title = LSTR_IMMED("SIZE"),
        }, {
            .title = LSTR_IMMED("OCCUPIED"),
        }, {
            .title = LSTR_IMMED("PAGE SIZE"),
        }, {
            .title = LSTR_IMMED("NB PAGES"),
        }
    };
    uint32_t hdr_size = countof(hdr_data);
    size_t   total_size = 0;
    size_t   total_occupied = 0;
    uint32_t total_nb_pages = 0;
    int nb_fifo_pool = 0;
    mem_fifo_pool_t *fp;

    qv_init_static(&hdr, hdr_data, hdr_size);
    t_qv_init(&rows, 200);

#define ADD_NUMBER_FIELD(_what)  \
    do {                                                                     \
        t_SB(_buf, 16);                                                      \
                                                                             \
        sb_add_int_fmt(&_buf, _what, ',');                                   \
        qv_append(tab, LSTR_SB_V(&_buf));                                    \
    } while (0)

    spin_lock(&_G.all_pools_lock);

    dlist_for_each_entry(fp, &_G.all_pools, pool_list) {
        qv_t(lstr) *tab = qv_growlen(&rows, 1);

        t_qv_init(tab, hdr_size);
        qv_append(tab, t_lstr_fmt("%s", fp->name));
        qv_append(tab, t_lstr_fmt("%p", fp));

        ADD_NUMBER_FIELD(fp->map_size);
        ADD_NUMBER_FIELD(fp->occupied);
        ADD_NUMBER_FIELD(fp->page_size);
        ADD_NUMBER_FIELD(fp->nb_pages);

        nb_fifo_pool++;
        total_size     += fp->map_size;
        total_occupied += fp->occupied;
        total_nb_pages += fp->nb_pages;
    }

    spin_unlock(&_G.all_pools_lock);

    if (nb_fifo_pool) {
        SB_1k(buf);
        qv_t(lstr) *tab = qv_growlen(&rows, 1);

        t_qv_init(tab, hdr_size);
        qv_append(tab, LSTR("TOTAL"));
        qv_append(tab, LSTR("-"));

        ADD_NUMBER_FIELD(total_size);
        ADD_NUMBER_FIELD(total_occupied);
        qv_append(tab, LSTR("-"));
        ADD_NUMBER_FIELD(total_nb_pages);

        sb_add_table(&buf, &hdr, &rows);
        sb_shrink(&buf, 1);
        logger_notice(&_G.logger, "fifo pools summary:\n%*pM",
                      SB_FMT_ARG(&buf));
    }
#undef ADD_NUMBER_FIELD
}

static int core_mem_fifo_initialize(void *arg)
{
    return 0;
}

static int core_mem_fifo_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(core_mem_fifo)
    MODULE_IMPLEMENTS_VOID(print_state, &core_mem_fifo_print_state);
MODULE_END()

/* }}} */
