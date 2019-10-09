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

#include "container-dlist.h"
#include "datetime.h"
#include "el.h"
#include "str-buf-pp.h"
#include "thr.h"

#ifdef MEM_BENCH
#include "core-mem-bench.h"

#define WRITE_PERIOD  256
#endif

#ifndef __BIGGEST_ALIGNMENT__
#define __BIGGEST_ALIGNMENT__  16
#endif

#define DEFAULT_ALIGNMENT  __BIGGEST_ALIGNMENT__
#ifndef NDEBUG
# define MIN_ALIGNMENT  sizeof(void *)
#else
# define MIN_ALIGNMENT  1
#endif

/** Size tuning parameters.
 * These are multiplicative factors over sp_alloc_mean.
 */
#define ALLOC_MIN   64 /*< minimum block allocation */
#define RESET_MIN   56 /*< minimum size in mem_stack_pool_reset */
#define RESET_MAX  256 /*< maximum size in mem_stack_pool_reset */

static struct {
    logger_t logger;

    dlist_t all_pools;
    spinlock_t all_pools_lock;
} core_mem_stack_g = {
#define _G  core_mem_stack_g
    .logger = LOGGER_INIT_INHERITS(NULL, "core-mem-stack"),
    .all_pools = DLIST_INIT(_G.all_pools),
};

static ALWAYS_INLINE size_t sp_alloc_mean(mem_stack_pool_t *sp)
{
    return sp->alloc_sz / sp->alloc_nb;
}

static ALWAYS_INLINE mem_stack_blk_t *blk_entry(dlist_t *l)
{
    return container_of(l, mem_stack_blk_t, blk_list);
}

__cold
static mem_stack_blk_t *blk_create(mem_stack_pool_t *sp,
                                   mem_stack_blk_t *cur, size_t size_hint)
{
    size_t blksize = size_hint + sizeof(mem_stack_blk_t);
    size_t alloc_target = MIN(100U << 20, ALLOC_MIN * sp_alloc_mean(sp));
    mem_stack_blk_t *blk;

    if (blksize < sp->minsize)
        blksize = sp->minsize;
    if (blksize < alloc_target)
        blksize = alloc_target;
    blksize = ROUND_UP(blksize, PAGE_SIZE);
    blk = imalloc(blksize, 0, MEM_RAW | MEM_LIBC);
    blk->size      = blksize - sizeof(*blk);
    dlist_add_after(&cur->blk_list, &blk->blk_list);

    sp->stacksize += blk->size;
    sp->nb_blocks++;

#ifdef MEM_BENCH
    sp->mem_bench->malloc_calls++;
    sp->mem_bench->current_allocated += blk->size;
    sp->mem_bench->total_allocated += blksize;
    mem_bench_update(sp->mem_bench);
    mem_bench_print_csv(sp->mem_bench);
#endif

    return blk;
}

__cold
static void blk_destroy(mem_stack_pool_t *sp, mem_stack_blk_t *blk)
{
#ifdef MEM_BENCH
    /* if called by mem_stack_pool_wipe,
     * the mem_bench might be deleted
     */
    if (sp->mem_bench) {
        sp->mem_bench->current_allocated -= blk->size;
        mem_bench_update(sp->mem_bench);
        mem_bench_print_csv(sp->mem_bench);
    }
#endif

    sp->stacksize -= blk->size;
    sp->nb_blocks--;

    dlist_remove(&blk->blk_list);
    mem_tool_allow_memory(blk, blk->size + sizeof(*blk), false);
    ifree(blk, MEM_LIBC);
}

static ALWAYS_INLINE mem_stack_blk_t *
frame_get_next_blk(mem_stack_pool_t *sp, mem_stack_blk_t *cur, size_t alignment,
                   size_t size)
{
    size_t deleted_size = 0;
    mem_stack_blk_t *blk;

#ifdef MEM_BENCH
    sp->mem_bench->alloc.nb_slow_path++;
#endif

    dlist_for_each_entry_continue(cur, blk, &sp->blk_list, blk_list) {
        size_t needed_size = size;
        uint8_t *aligned_area;

        aligned_area = (uint8_t *)mem_align_ptr((uintptr_t)blk->area, alignment);
        needed_size += aligned_area - blk->area;

        if (blk->size >= needed_size) {
            return blk;
        }

        /* bound deleted size by created size */
        if (deleted_size >= needed_size) {
            break;
        }

        deleted_size += blk->size;
        blk_destroy(sp, blk);
    }

    if ((offsetof(mem_stack_blk_t, area) & (alignment - 1)) != 0) {
        /* require enough free space so we're sure we can allocate the size
         * bytes properly aligned.
         */
        size += alignment;
    }
    return blk_create(sp, cur, size);
}

static ALWAYS_INLINE uint8_t *blk_end(mem_stack_blk_t *blk)
{
    return blk->area + blk->size;
}

static ALWAYS_INLINE void frame_set_blk(mem_stack_frame_t *frame,
                                        mem_stack_blk_t *blk)
{
    frame->blk  = blk;
    frame->pos  = blk->area;
    frame->last = blk->area;
    frame->end  = blk_end(blk);
}

static ALWAYS_INLINE uint8_t *frame_end(mem_stack_frame_t *frame)
{
    assert (frame->end == blk_end(frame->blk));
    return frame->end;
}

static void *sp_reserve(mem_stack_pool_t *sp, size_t asked, size_t alignment,
                        uint8_t **end)
{
    uint8_t           *res;
    mem_stack_frame_t *frame = sp->stack;

    res = (uint8_t *)mem_align_ptr((uintptr_t)frame->pos, alignment);

    if (unlikely(res + asked > frame_end(frame))) {
        mem_stack_blk_t *blk = frame_get_next_blk(sp, frame->blk, alignment,
                                                  asked);
        frame_set_blk(frame, blk);

        res   = blk->area;
        res   = (uint8_t *)mem_align_ptr((uintptr_t)res, alignment);
    }

    mem_tool_disallow_memory(frame->pos, res - frame->pos);
    mem_tool_allow_memory(res, asked, false);

    *end = res + asked;

    /* compute a progressively forgotten mean of the allocation size.
     *
     * Every 64k allocations, we divide the sum of allocations by four so that
     * the distant past has less and less consequences on the mean in the hope
     * that it will converge.
     *
     * There is no risk of overflow on alloc_sz,
     * since mp_imalloc checks that asked < MEM_ALLOC_MAX = (1 << 30),
     * and MEM_ALLOC_MAX * UINT16_MAX < SIZE_MAX.
     */
    if (unlikely(sp->alloc_nb >= UINT16_MAX)) {
        STATIC_ASSERT (MEM_ALLOC_MAX * UINT16_MAX < SIZE_MAX);

        sp->alloc_sz /= 4;
        sp->alloc_nb /= 4;
    }
    sp->alloc_sz += asked;
    sp->alloc_nb += 1;

    return res;
}

__flatten
static void *sp_alloc(mem_pool_t *_sp, size_t size, size_t alignment,
                      mem_flags_t flags)
{
    mem_stack_pool_t *sp = mem_stack_get_pool(_sp);
    mem_stack_frame_t *frame = sp->stack;
    uint8_t *res;
#ifdef MEM_BENCH
    proctimer_t ptimer;
    proctimer_start(&ptimer);
#endif

    if (unlikely((size == 0))) {
        return MEM_EMPTY_ALLOC;
    }

#ifndef NDEBUG
    if (unlikely(frame == &sp->base))
        e_panic("allocation performed without a t_scope");
    if (frame->prev & 1)
        e_panic("allocation performed on a sealed stack");
    size += alignment;
#endif
    res = sp_reserve(sp, size, alignment, &frame->pos);
    if (!(flags & MEM_RAW)) {
#ifdef MEM_BENCH
        /* since sp_free is no-op, we use the fields to measure memset */
        proctimer_t free_timer;
        proctimer_start(&free_timer);
#endif
        memset(res, 0, size);

#ifdef MEM_BENCH
        proctimer_stop(&free_timer);
        proctimerstat_addsample(&sp->mem_bench->free.timer_stat, &free_timer);

        sp->mem_bench->free.nb_calls++;
        mem_bench_update(sp->mem_bench);
#endif
    }

#ifndef NDEBUG
    res += alignment;
    ((void **)res)[-1] = sp->stack;
    mem_tool_disallow_memory(res - alignment, alignment);
#endif

#ifdef MEM_BENCH
    proctimer_stop(&ptimer);
    proctimerstat_addsample(&sp->mem_bench->alloc.timer_stat, &ptimer);

    sp->mem_bench->alloc.nb_calls++;
    sp->mem_bench->current_used += size;
    sp->mem_bench->total_requested += size;
    mem_bench_update(sp->mem_bench);
#endif
    return frame->last = res;
}

static void sp_free(mem_pool_t *_sp, void *mem)
{
}

static void *sp_realloc(mem_pool_t *_sp, void *mem, size_t oldsize,
                        size_t asked, size_t alignment, mem_flags_t flags)
{
    mem_stack_pool_t *sp = mem_stack_get_pool(_sp);
    mem_stack_frame_t *frame = sp->stack;
    ssize_t sizediff = asked - oldsize;
    uint8_t *res = mem;

#ifdef MEM_BENCH
    proctimer_t ptimer;
    proctimer_start(&ptimer);
#endif

    if (unlikely(mem == MEM_EMPTY_ALLOC)) {
        mem = NULL;
    }

#ifndef NDEBUG
    if (frame->prev & 1)
        e_panic("allocation performed on a sealed stack");
    if (mem != NULL) {
        mem_tool_allow_memory((byte *)mem - sizeof(void *), sizeof(void *), true);
        if (unlikely(((void **)mem)[-1] != sp->stack))
            e_panic("%p wasn't allocated in that frame, realloc is forbidden", mem);
        mem_tool_disallow_memory((byte *)mem - sizeof(void *), sizeof(void *));
    }
    if (unlikely(oldsize == MEM_UNKNOWN))
        e_panic("stack pools do not support reallocs with unknown old size");
#endif

    if (likely(res == frame->last)
    &&  likely(res + asked <= frame_end(frame)))
    {
        assert (res);

        frame->pos = res + asked;
        sp->alloc_sz += sizediff;

        if (likely(sizediff >= 0)) {
            mem_tool_allow_memory(res + oldsize, sizediff, false);

            if (!(flags & MEM_RAW)) {
                p_clear(res + oldsize, sizediff);
            }
        } else {
            mem_tool_disallow_memory(res + asked, -sizediff);
            if (!asked) {
                res = MEM_EMPTY_ALLOC;
            }
        }

#ifdef MEM_BENCH
        proctimer_stop(&ptimer);
        proctimerstat_addsample(&sp->mem_bench->realloc.timer_stat, &ptimer);

        sp->mem_bench->realloc.nb_calls++;
        sp->mem_bench->total_requested += sizediff;
        sp->mem_bench->current_used += sizediff;
        mem_bench_update(sp->mem_bench);
#endif

        return res;
    }

    if (likely(sizediff <= 0)) {
        mem_tool_disallow_memory(res + asked, -sizediff);

#ifdef MEM_BENCH
        proctimer_stop(&ptimer);
        proctimerstat_addsample(&sp->mem_bench->realloc.timer_stat, &ptimer);

        sp->mem_bench->realloc.nb_calls++;
        sp->mem_bench->current_used -= sizediff;
        mem_bench_update(sp->mem_bench);
#endif

        return asked ? res : MEM_EMPTY_ALLOC;
    }

    res = sp_alloc(_sp, asked, alignment, flags | MEM_RAW);
    if (mem) {
        memcpy(res, mem, oldsize);
        mem_tool_disallow_memory(mem, oldsize);
    }
    if (!(flags & MEM_RAW)) {
        p_clear(res + oldsize, sizediff);
    }

    return res;
}

static mem_pool_t const pool_funcs = {
    .malloc   = &sp_alloc,
    .realloc  = &sp_realloc,
    .free     = &sp_free,
    .mem_pool = MEM_STACK | MEM_BY_FRAME,
    .min_alignment = sizeof(void *)
};

#ifndef NDEBUG
/** Special code to bypass the allocator.
 * The frames keep their usual behaviour.
 * The blk objects are now the prefixes of all the allocations performed.
 */
static void *sp_alloc_libc(mem_pool_t *_sp, size_t asked, size_t alignment,
                           mem_flags_t flags)
{
    mem_stack_pool_t *sp = mem_stack_get_pool(_sp);
    size_t oversize = mem_align_ptr(sizeof(mem_stack_blk_t), alignment);
    mem_stack_blk_t *blk;
    uint8_t *ptr;

    assert (alignment >= 8);

    ptr  = mp_imalloc(&mem_pool_libc, asked + oversize, alignment, flags);
    ptr += oversize;

    blk  = (mem_stack_blk_t *)ptr - 1;
    blk->size = oversize;
    dlist_add_tail(&sp->blk_list, &blk->blk_list);

    return ptr;
}

static void *sp_realloc_libc(mem_pool_t *_sp, void *mem, size_t oldsize,
                             size_t asked, size_t alignment,
                             mem_flags_t flags)
{
    mem_stack_blk_t *blk = (mem_stack_blk_t *)mem - 1;
    size_t oversize = blk->size;
    uint8_t *ptr = (uint8_t *)mem;

    assert (oversize >= sizeof(mem_stack_blk_t));

    ptr -= oversize;
    ptr  = mp_irealloc(&mem_pool_libc, ptr, oldsize + oversize,
                       asked + oversize, alignment, flags);
    ptr += oversize;

    blk  = (mem_stack_blk_t *)ptr - 1;
    __dlist_repair(&blk->blk_list);

    return ptr;
}

static void sp_free_libc(mem_pool_t *_sp, void *mem)
{
}

static void *sp_push_libc(mem_stack_pool_t *sp)
{
    mem_stack_frame_t *frame = p_new(mem_stack_frame_t, 1);

    frame->prev = (uintptr_t)sp->stack;
    frame->blk  = blk_entry(sp->blk_list.prev);

    return sp->stack = frame;
}

const void *mem_stack_pool_pop_libc(mem_stack_pool_t *sp)
{
    mem_stack_frame_t *frame = sp->stack;
    mem_stack_blk_t *blk;

    sp->stack = mem_stack_pool_prev(frame);

    dlist_for_each_entry_continue(frame->blk, blk, &sp->blk_list,
                                       blk_list) {
        uint8_t *ptr = (uint8_t *)(blk + 1) - blk->size;

        dlist_remove(&blk->blk_list);
        mp_ifree(&mem_pool_libc, ptr);
    }

    {
        mem_stack_frame_t *f = frame;

        p_delete(&f);
    }

    return frame;
}

static mem_pool_t const pool_funcs_libc = {
    .malloc  = &sp_alloc_libc,
    .realloc = &sp_realloc_libc,
    .free    = &sp_free_libc,
    .mem_pool= MEM_STACK | MEM_BY_FRAME,
    .min_alignment = sizeof(void *)
};
#endif

mem_stack_pool_t *mem_stack_pool_init(mem_stack_pool_t *sp, const char *name,
                                      int initialsize)
{
    /* no p_clear is made for two reasons :
     * - there is few objects that shall be zero-initialized
     * - it is very poorly optimized by gcc
     * Therefore, is is needed to explicitly initialize
     * all the added fields. Thus, it is advised to keep initializations
     * of the fields in the order of declaration.
     */
    sp->stack     = &sp->base;

    sp->alloc_sz   = 0;
    sp->alloc_nb   = 1; /* avoid the division by 0 */
    sp->last_reset = lp_getsec();

    sp->funcs     = pool_funcs;

    /* root block */
    sp->size      = 0;
    dlist_init(&sp->blk_list);

    /* root frame */
    frame_set_blk(&sp->base, blk_entry(&sp->blk_list));
    sp->base.prev = 0;

    /* 640k should be enough for everybody =) */
    if (initialsize <= 0)
        initialsize = 640 << 10;
    sp->minsize   = ROUND_UP(initialsize, PAGE_SIZE);

    sp->stacksize = 0;
    sp->nb_blocks = 0;

#ifndef NDEBUG
    /* bypass mem_pool if demanded
     * XXX this code is intentionnally
     * placed at the end of the init function,
     * to avoid problems with seal/unseal macros */
    if (!mem_pool_is_enabled()) {
        sp->funcs = pool_funcs_libc;
        return sp;
    }
#endif

#ifdef MEM_BENCH
    sp->mem_bench = mem_bench_new(LSTR("stack"), WRITE_PERIOD);
    mem_bench_leak(sp->mem_bench);
#endif

    sp->name = p_strdup(name);

    spin_lock(&_G.all_pools_lock);
    dlist_add_tail(&_G.all_pools, &sp->pool_list);
    spin_unlock(&_G.all_pools_lock);

    return sp;
}

void mem_stack_pool_reset(mem_stack_pool_t *sp)
{
    mem_stack_blk_t *saved_blk;
    size_t saved_size;
    size_t max_size;

    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    /* we do not want to wipe everything :
     * we will keep one block, iff
     * its size is more than 56*alloc_mean
     * (blk_create has minimum 64*alloc_mean)
     * and less than 256*alloc_mean.
     * we keep the biggest in this range.
     */
    sp->last_reset = lp_getsec();
    saved_blk  = NULL;
    saved_size = RESET_MIN * sp_alloc_mean(sp);
    max_size   = RESET_MAX * sp_alloc_mean(sp);

    dlist_for_each(e, &sp->blk_list) {
        mem_stack_blk_t *blk = blk_entry(e);

        if (blk->size > saved_size && blk->size < max_size) {
            if (saved_blk) {
                blk_destroy(sp, saved_blk);
            }
            saved_blk  = blk;
            saved_size = blk->size;
        } else {
            blk_destroy(sp, blk);
        }
    }

    if (saved_blk) {
        frame_set_blk(&sp->base, saved_blk);
    } else {
        frame_set_blk(&sp->base, blk_entry(&sp->blk_list));
    }
}

void mem_stack_pool_try_reset(mem_stack_pool_t *sp)
{
    size_t size_limit = 1 << 20; /* 1 MiB */

    /* Reset only at top stacks. */
    if (!mem_stack_pool_is_at_top(sp)) {
        return;
    }

    /* Do not reset small stacks (10 MiB on the main thread, 1 MiB on the
     * others). */
    if (thr_is_on_queue(thr_queue_main_g)) {
        size_limit *= 10;
    }
    if (sp->stacksize < size_limit) {
        return;
    }

    /* Do not reset more than once per minute. */
    if (sp->last_reset + 60 > lp_getsec()) {
        return;
    }

    mem_stack_pool_reset(sp);
}

void mem_stack_pool_wipe(mem_stack_pool_t *sp)
{
    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    spin_lock(&_G.all_pools_lock);
    dlist_remove(&sp->pool_list);
    spin_unlock(&_G.all_pools_lock);

    p_delete(&sp->name);

#ifdef MEM_BENCH
    mem_bench_delete(&sp->mem_bench);
#endif

    frame_set_blk(&sp->base, blk_entry(&sp->blk_list));

    dlist_for_each(e, &sp->blk_list) {
        blk_destroy(sp, blk_entry(e));
    }
    assert (sp->stacksize == 0);
}

const void *mem_stack_pool_push(mem_stack_pool_t *sp)
{
    uint8_t *end;
    uint8_t *res;
    mem_stack_frame_t *frame;
    mem_stack_frame_t *oldframe = sp->stack;

#ifndef NDEBUG
    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return sp_push_libc(sp);
    }
#endif

    res = sp_reserve(sp, sizeof(mem_stack_frame_t),
                     __BIGGEST_ALIGNMENT__, &end);

#ifdef MEM_BENCH
    /* if the assert fires,
     * it means the stack pool has been wiped by mem_stack_pool_wipe.
     * t_push'ing again is then an incorrect behaviour.
     */
    assert (sp->mem_bench);

    mem_bench_print_csv(sp->mem_bench);
#endif
    frame = (mem_stack_frame_t *)res;
    frame->blk  = oldframe->blk;
    frame->pos  = end;
    frame->end  = oldframe->end;
    frame->last = end;
    frame->prev = (uintptr_t)oldframe;
    return sp->stack = frame;
}

#ifdef MEM_BENCH
void mem_stack_bench_pop(mem_stack_pool_t *sp, mem_stack_frame_t * frame)
{
    mem_stack_blk_t *last_block = frame->blk;
    int32_t cused = sp->mem_bench->current_used;

    mem_bench_print_csv(sp->mem_bench);
    if (sp->stack->blk == last_block) {
        cused -= (frame->pos - sp->stack->pos
                  - sizeof(mem_stack_frame_t));
    } else {
        cused -= frame->pos - last_block->area;
        last_block = container_of(last_block->blk_list.prev,
                                  mem_stack_blk_t, blk_list);
        while (sp->stack->blk != last_block) {
            cused -= last_block->size;
            /* Note: this is inaccurate, because we don't know the size of the
               unused space at the end of the block
            */
            last_block = container_of(last_block->blk_list.prev,
                                      mem_stack_blk_t, blk_list);
        }
        cused -= (last_block->area + last_block->size
                  - sp->stack->pos + sizeof(mem_stack_frame_t));
    }
    if (cused <= 0 || mem_stack_is_at_top(sp)) {
        cused = 0;
    }
    sp->mem_bench->current_used = cused;
    mem_bench_update(sp->mem_bench);
}
#endif

void mem_stack_print_stats(const mem_pool_t *mp) {
#ifdef MEM_BENCH
    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    const mem_stack_pool_t *sp = container_of(mp, mem_stack_pool_t, funcs);
    mem_bench_print_human(sp->mem_bench, MEM_BENCH_PRINT_CURRENT);
#endif
}

void mem_stack_print_pools_stats(void) {
#ifdef MEM_BENCH
    /* bypass mem_pool if demanded */
    if (!mem_pool_is_enabled()) {
        return;
    }

    spin_lock(&_G.all_pools_lock);
    dlist_for_each(n, &_G.all_pools) {
        mem_stack_pool_t *mp = mem_stack_get_pool(n);

        mem_bench_print_human(mp->mem_bench, MEM_BENCH_PRINT_CURRENT);
    }
    spin_unlock(&_G.all_pools_lock);
#endif
}

#ifndef NDEBUG
void mem_stack_pool_protect(mem_stack_pool_t *sp, const mem_stack_frame_t *up_to)
{
    if (up_to->blk == sp->stack->blk) {
        mem_tool_disallow_memory(sp->stack->pos, up_to->pos - sp->stack->pos);
    } else {
        const byte *end = up_to->pos;
        mem_stack_blk_t *end_blk = up_to->blk;
        mem_stack_blk_t *blk = sp->stack->blk;
        size_t remainsz = frame_end(sp->stack) - sp->stack->pos;

        mem_tool_disallow_memory(sp->stack->pos, remainsz);
        dlist_for_each_entry_continue(blk, blk, &sp->blk_list, blk_list) {
            if (blk == end_blk) {
                mem_tool_disallow_memory(blk->area, end - blk->area);
                break;
            } else {
                mem_tool_disallow_memory(blk->area, blk->size);
            }
        }
    }
}
#endif

static inline mem_stack_pool_t *mem_stack_pool_new(const char *name,
                                                   int initialsize)
{
    mem_stack_pool_t *sp = p_new_raw(mem_stack_pool_t, 1);

    mem_stack_pool_init(sp, name, initialsize);

    return sp;
}

mem_pool_t *mem_stack_new(const char *name, int initialsize)
{
    mem_stack_pool_t *pool = mem_stack_pool_new(name, initialsize);

    return &pool->funcs;
}

GENERIC_DELETE(mem_stack_pool_t, mem_stack_pool);

void mem_stack_delete(mem_pool_t *nonnull *nullable mp)
{
    if (*mp) {
        mem_stack_pool_t *sp = mem_stack_get_pool(*mp);

        mem_stack_pool_delete(&sp);
        *mp = NULL;
    }
}

__attribute__((constructor))
static void t_pool_init(void)
{
    mem_stack_pool_init(&t_pool_g, "t_pool", 64 << 10);
}
static void t_pool_wipe(void)
{
    mem_stack_pool_wipe(&t_pool_g);
}
thr_hooks(t_pool_init, t_pool_wipe);

__thread mem_stack_pool_t t_pool_g;

static void mem_stack_reset_all_pools_at_fork(void)
{
    dlist_init(&_G.all_pools);
}

__attribute__((constructor))
static void mem_stack_all_pools_init_at_fork(void)
{
    pthread_atfork(NULL, NULL, &mem_stack_reset_all_pools_at_fork);
}

/* {{{ Module (for print_state method) */

static void core_mem_stack_print_state(void)
{
    t_scope;
    qv_t(table_hdr) hdr;
    qv_t(table_data) rows;
    table_hdr_t hdr_data[] = { {
            .title = LSTR_IMMED("STACK POOL NAME"),
        }, {
            .title = LSTR_IMMED("POINTER"),
        }, {
            .title = LSTR_IMMED("SIZE"),
        }, {
            .title = LSTR_IMMED("NB BLOCKS"),
        }, {
            .title = LSTR_IMMED("ALLOC SIZE"),
        }, {
            .title = LSTR_IMMED("ALLOC NB"),
        }, {
            .title = LSTR_IMMED("ALLOC MEAN"),
        }, {
            .title = LSTR_IMMED("LAST RESET"),
        }
    };
    uint32_t hdr_size = countof(hdr_data);
    size_t   total_stacksize = 0;
    uint32_t total_nb_blocks = 0;
    size_t   total_alloc_sz = 0;
    uint32_t total_alloc_nb = 0;
    int nb_stack_pool = 0;
    mem_stack_pool_t *sp;

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

    dlist_for_each_entry(sp, &_G.all_pools, pool_list) {
        qv_t(lstr) *tab = qv_growlen(&rows, 1);

        t_qv_init(tab, hdr_size);
        qv_append(tab, t_lstr_fmt("%s", sp->name));
        qv_append(tab, t_lstr_fmt("%p", sp));

        ADD_NUMBER_FIELD(sp->stacksize);
        ADD_NUMBER_FIELD(sp->nb_blocks);
        ADD_NUMBER_FIELD(sp->alloc_sz);
        ADD_NUMBER_FIELD(sp->alloc_nb);
        ADD_NUMBER_FIELD(sp_alloc_mean(sp));

        qv_append(tab, t_lstr_fmt("%jd", sp->last_reset));

        nb_stack_pool++;
        total_stacksize += sp->stacksize;
        total_nb_blocks += sp->nb_blocks;
        total_alloc_sz  += sp->alloc_sz;
        total_alloc_nb  += sp->alloc_nb;
    }

    spin_unlock(&_G.all_pools_lock);

    if (nb_stack_pool) {
        SB_1k(buf);
        qv_t(lstr) *tab = qv_growlen(&rows, 1);

        t_qv_init(tab, hdr_size);
        qv_append(tab, LSTR("TOTAL"));
        qv_append(tab, LSTR("-"));

        ADD_NUMBER_FIELD(total_stacksize);
        ADD_NUMBER_FIELD(total_nb_blocks);
        ADD_NUMBER_FIELD(total_alloc_sz);
        ADD_NUMBER_FIELD(total_alloc_nb);
        ADD_NUMBER_FIELD(total_alloc_sz / total_alloc_nb);
        qv_append(tab, LSTR("-"));

        sb_add_table(&buf, &hdr, &rows);
        sb_shrink(&buf, 1);
        logger_notice(&_G.logger, "stack pools summary:\n%*pM",
                      SB_FMT_ARG(&buf));
    }
#undef ADD_NUMBER_FIELD
}

static int core_mem_stack_initialize(void *arg)
{
    return 0;
}

static int core_mem_stack_shutdown(void)
{
    return 0;
}

MODULE_BEGIN(core_mem_stack)
    MODULE_IMPLEMENTS_VOID(print_state, &core_mem_stack_print_state);
MODULE_END()

/* }}} */
