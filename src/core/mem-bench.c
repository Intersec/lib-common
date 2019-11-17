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

#include <lib-common/unix.h>
#include <lib-common/thr.h>

#include "mem-bench.h"

static logger_t mem_bench_logger_g = LOGGER_INIT_INHERITS(NULL, "mem-bench");

static spinlock_t  mem_bench_leak_lock_g;
static       DLIST(mem_bench_leak_list_g);

mem_bench_t *mem_bench_init(mem_bench_t *sp, lstr_t type, uint32_t period)
{
    p_clear(sp, 1);
    dlist_init(&sp->bench_list);

    {
        lstr_t logname = lstr_fmt("%*pM.%p", LSTR_FMT_ARG(type), sp);
        sp->logger = logger_new(&mem_bench_logger_g, logname, LOG_INHERITS, 0);
        lstr_wipe(&logname);
    }

    if (period) {
        char filename[PATH_MAX];

        path_extend(filename, ".", "mem.%*pM.data.%u.%p",
                    LSTR_FMT_ARG(type), getpid(), sp);
        sp->file = fopen(filename, "w");
        /* not fatal if sp->file is NULL : we won't log anything. */
    }
    sp->out_period = period;
    sp->out_counter = period;

    lstr_copy(&sp->allocator_name, type);

    return sp;
}

void mem_bench_leak(mem_bench_t *sp)
{
    spin_lock(&mem_bench_leak_lock_g);
    dlist_add_tail(&mem_bench_leak_list_g, &sp->bench_list);
    spin_unlock(&mem_bench_leak_lock_g);
}

static void mem_bench_partial_wipe(mem_bench_t *sp)
{
    mem_bench_print_human(sp, 0);

    spin_lock(&mem_bench_leak_lock_g);
    dlist_remove(&sp->bench_list);
    spin_unlock(&mem_bench_leak_lock_g);

    logger_delete(&sp->logger);
}

void mem_bench_wipe(mem_bench_t *sp)
{
    mem_bench_print_csv(sp);
    mem_bench_partial_wipe(sp);
    p_fclose(&sp->file);
    lstr_wipe(&sp->allocator_name);
}

static void mem_bench_print_func_csv(mem_bench_func_t *spf, FILE *file)
{
    assert (file);
    fprintf(file, "%u,%u,",
            spf->nb_calls, spf->nb_slow_path);
    fprintf(file, "%u,%lu,%lu,%lu,",
            spf->timer_stat.nb,
            spf->timer_stat.hard_min,
            spf->timer_stat.hard_max,
            spf->timer_stat.hard_tot);
}

void mem_bench_print_csv(mem_bench_t *sp)
{
    if (!sp->file) {
        return;
    }

    if (sp->logger) {
        logger_trace(sp->logger, 1, "CSV trace");
    }

    mem_bench_print_func_csv(&sp->alloc, sp->file);
    mem_bench_print_func_csv(&sp->realloc, sp->file);
    mem_bench_print_func_csv(&sp->free, sp->file);
    fprintf(sp->file, "%lu,%lu,%u,%u,%u,%u,%u,%u\n",
            sp->total_allocated, sp->total_requested,
            sp->max_allocated, sp->max_unused, sp->max_used,
            sp->malloc_calls, sp->current_used, sp->current_allocated);
}

void mem_bench_update(mem_bench_t *sp)
{
    sp->max_used      = MAX(sp->current_used, sp->max_used);
    sp->max_allocated = MAX(sp->current_allocated, sp->max_allocated);
    sp->max_unused    = MAX(sp->current_allocated - sp->current_used,
                            sp->max_unused);

    sp->out_counter--;
    if (sp->file && sp->out_counter <= 0) {
        mem_bench_print_csv(sp);
        sp->out_counter = sp->out_period;
    }

    if (sp->logger) {
        logger_trace(sp->logger, 2, "Update");
    }
}

static void mem_bench_print_func_human(const mem_bench_t *sp,
                                       const mem_bench_func_t *spf,
                                       const char* prefix)
{
    assert (sp->logger);
    logger_debug(sp->logger, "%s/requests          : %10u",
                 prefix, spf->nb_calls);
    logger_debug(sp->logger, "%s/slow path calls   : %10u \t%u.%u %%",
                 prefix, spf->nb_slow_path,
                 100 * spf->nb_slow_path / MAX(1, spf->nb_calls),
                 (10000 * spf->nb_slow_path / MAX(1, spf->nb_calls)) % 100);
    logger_debug(sp->logger, "%s/timer             : %s",
                 prefix,
                 proctimerstat_report((proctimerstat_t *)&spf->timer_stat, "%h"));
}

void mem_bench_print_human(const mem_bench_t *sp, int flags)
{
    if (!sp->logger) {
        return;
    }

    logger_debug(sp->logger, "%*pM allocator @%p stats  :",
                 LSTR_FMT_ARG(sp->allocator_name), sp);
    mem_bench_print_func_human(sp, &sp->alloc,   "alloc  ");
    mem_bench_print_func_human(sp, &sp->realloc, "realloc");
    mem_bench_print_func_human(sp, &sp->free,    "free   ");
    logger_debug(sp->logger, "average request size      : %10lu bytes",
                 sp->total_requested / MAX(1,sp->alloc.nb_calls));
    logger_debug(sp->logger, "average block size        : %10lu bytes",
                 sp->total_allocated / MAX(1,sp->malloc_calls));
    logger_debug(sp->logger, "total memory allocated    : %10lu K",
                 sp->total_allocated / 1024);
    logger_debug(sp->logger, "total memory requested    : %10lu K",
                 sp->total_requested / 1024);
    logger_debug(sp->logger, "max used memory           : %10d K",
                 sp->max_used / 1024);
    logger_debug(sp->logger, "max unused memory         : %10u K",
                 sp->max_unused / 1024);
    logger_debug(sp->logger, "max memory allocated      : %10u K",
                 sp->max_allocated / 1024);
    logger_debug(sp->logger, "malloc calls              : %10u",
                 sp->malloc_calls);
    if (flags & MEM_BENCH_PRINT_CURRENT) {
        logger_debug(sp->logger, "current used memory       : %10u K",
                     sp->current_used / 1024);
        logger_debug(sp->logger, "current allocated memory  : %10u K",
                     sp->current_allocated / 1024);
    }
}

static int mem_bench_initialize(void *arg)
{
    (void)arg;
    return 0;
}

static int mem_bench_shutdown(void)
{
    mem_bench_t *sp;

    spin_lock(&mem_bench_leak_lock_g);
    dlist_for_each_entry(sp, &mem_bench_leak_list_g, bench_list) {
        spin_unlock(&mem_bench_leak_lock_g);

        mem_bench_partial_wipe(sp);

        spin_lock(&mem_bench_leak_lock_g);
    }
    spin_unlock(&mem_bench_leak_lock_g);

    return 0;
}

void mem_bench_require(void)
{
    static module_t *mb_module;

    assert (!mb_module);
    mb_module = module_implement(module_register(LSTR("mem-bench")),
                                 &mem_bench_initialize, &mem_bench_shutdown,
                                 MODULE(log));
    module_require(mb_module, NULL);
}
