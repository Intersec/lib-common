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

#ifndef IS_LIB_COMMON_CORE_MEM_BENCH_H
#define IS_LIB_COMMON_CORE_MEM_BENCH_H

#include <lib-common/core.h>
#include <lib-common/datetime.h>
#include <lib-common/log.h>

/* for timing individual function */
typedef struct mem_bench_func_t {
    uint32_t nb_calls;
    uint32_t nb_slow_path;

    proctimerstat_t timer_stat;
} mem_bench_func_t;

typedef struct mem_bench_t {
    /* profile data */
    mem_bench_func_t alloc;
    mem_bench_func_t realloc;
    mem_bench_func_t free;

    uint64_t total_allocated;
    uint64_t total_requested;

    uint32_t max_allocated;
    uint32_t max_used;
    uint32_t max_unused;

    uint32_t malloc_calls;
    uint32_t current_used;
    uint32_t current_allocated;

    /* live summary printing */
    logger_t  *logger;

    /* leak destruction */
    dlist_t    bench_list;

    /* CSV dumping */
    FILE      *file;
    uint32_t   out_period;
    uint32_t   out_counter;

    /* allocator type */
    lstr_t   allocator_name;
} mem_bench_t;

/** Initialize mem_bench object.
 *
 * The mem_bench object is initialized to dump into a file
 * each \p period iterations. The filename is
 * "./mem.[\p name].[pid].[address of \p sp]".
 * "[\p type].[address of \p sp] is also used for logger name.
 *
 * \param[in] type   Name of mem_bench object.
 * \param[in] period Logging period. 0 means no logging.
 */
mem_bench_t *mem_bench_init(mem_bench_t *sp, lstr_t type, uint32_t period);

/** Wipes a mem_bench object.
 *
 * Note : the mem_bench object is still usable after call to this method,
 * but any operation on it will be no-op.
 */
void mem_bench_wipe(mem_bench_t *sp);

__unused__
static inline mem_bench_t *mem_bench_new(lstr_t type, uint32_t period)
{
    return mem_bench_init(p_new_raw(mem_bench_t, 1), type, period);
}
GENERIC_DELETE(mem_bench_t, mem_bench)

/** Declare a mem_bench object to be leaked.
 *
 * If you cannot ensure the mem_bench object will be
 * wiped before the log module termination
 * (if it must be destroyed by a thr_hook for example),
 * it must be registered with this function.
 * It will do a partial wipe, allowing to work properly.
 *
 * The mem_bench object must still be mem_bench_wipe'd manually
 * to finish the cleanup.
 *
 * Wipe time is unspecified, and the allocated memory
 * will not be reclaimed.
 */
void mem_bench_leak(mem_bench_t *sp);

/** Update state of the mem-bench object.
 *
 * This function updates the max_* fields of \p sp
 * and is responsible for periodic dumping to file.
 */
void mem_bench_update(mem_bench_t *sp);

/** Manually dump fields. */
void mem_bench_print_csv(mem_bench_t *sp);

/** Print stats in human-readable form.
 *
 * \param[in] flags Flag controlling printed informations.
 */
void mem_bench_print_human(const mem_bench_t *sp, int flags);

/** Flag for print_human : print current allocation status. */
#define MEM_BENCH_PRINT_CURRENT  1

/** Require mem_bench module manually */
void mem_bench_require(void);

#endif /* IS_LIB_COMMON_CORE_MEM_BENCH_H */
