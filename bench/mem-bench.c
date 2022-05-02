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

#include <lib-common/core.h>
#include <lib-common/datetime.h>
#include <lib-common/parseopt.h>

/* Small utility to benchmark allocators stack and fifo allocators
 * run mem-bench -f to test the fifo allocator
 *               -s to test the stack allocator
 */

static struct {
    bool help;
    bool test_stack;
    bool test_fifo;
    bool worst_case;
    int num_allocs;
    int max_allocated;
    int max_alloc_size;
    int max_depth;
    int num_tries;
    bool compare;
} settings = {
    .num_allocs = 1 << 20,
    .max_allocated = 10000,
    .max_alloc_size = 512,
    .max_depth = 1500,
    .num_tries = 100,
};

static popt_t popts[] = {
    OPT_FLAG('h', "help", &settings.help, "show this help"),
    OPT_FLAG('s', "stack", &settings.test_stack, "test stack allocator"),
    OPT_FLAG('f', "fifo", &settings.test_fifo, "test fifo allocator"),
    OPT_FLAG('c', "comp", &settings.compare, "also run the test with malloc"),
    OPT_FLAG('w', "worst-case", &settings.worst_case,
             "worst case test (fifo)"),
    OPT_INT('n', "allocs", &settings.num_allocs,
            "number of allocations made (default: 1 << 25)"),
    OPT_INT('m', "max", &settings.max_allocated, "max number of"
            " simultaneously allocated blocks (fifo only, default 10000)"),
    OPT_INT('z', "size", &settings.max_alloc_size,
            "max size of an allocation"),
    OPT_INT('d', "depth", &settings.max_depth, "max stack height (stack only"
            ", default 1500)"),
    OPT_INT('r', "tries", &settings.num_tries, "number of retries (stack only"
            ", default 100"),
    OPT_END(),
};

/* {{{ FIFO benchmarks */

/** Fifo allocator benchmarking
 *
 * First check is under real fifo behaviour: every block allocated is freed
 * immediately
 *
 * Second check is also a real fifo behaviour, but several blocks are
 * allocated simultaneously
 *
 * The third test is randomized: blocks are deallocated at a random time, and
 * at most MAX_ALLOCATED blocks are simultaneously allocated
 */
static int benchmark_fifo_pool(mem_pool_t *mp)
{
    byte **table = p_new(byte *, settings.max_allocated);

    /* Real fifo behavior, one at a time */
    for (int i = 0; i < settings.num_allocs / 3; i++) {
        byte *a = mp_new(mp, byte,
                         rand() % settings.max_alloc_size);

        mp_ifree(mp, a);
    }
#ifdef MEM_BENCH
    mem_fifo_pools_print_stats();
#endif

    /* Real fifo behaviour, settings.max_allocated at a time */
    for (int i = 0; i < settings.num_allocs / 3; i++) {
        int chosen = i % settings.max_allocated;

        if (table[chosen]) {
            mp_ifree(mp, table[chosen]);
        }
        table[chosen] = mp_new(mp, byte,
                               rand() % settings.max_alloc_size);
    }
#ifdef MEM_BENCH
    mem_fifo_pools_print_stats();
#endif

    /* Almost fifo */
    for (int i = 0; i < settings.num_allocs / 3; i++) {
        int chosen = rand() % settings.max_allocated;

        if (table[chosen]) {
            mp_ifree(mp, table[chosen]);
        }
        table[chosen] = mp_new(mp, byte,
                               rand() % settings.max_alloc_size);
    }
#ifdef MEM_BENCH
    mem_fifo_pools_print_stats();
#endif

    /* Clean leftovers */
    for (int i = 0; i < settings.max_allocated; i++) {
        if (table[i]) {
            mp_ifree(mp, table[i]);
        }
    }
#ifdef MEM_BENCH
    mem_fifo_pools_print_stats();
#endif

    p_delete(&table);
    return 0;
}

static int benchmark_fifo(void)
{
    int res;
    mem_pool_t *mp_fifo = mem_fifo_pool_new("benchmark", 0);

    res = benchmark_fifo_pool(mp_fifo);
    mem_fifo_pool_delete(&mp_fifo);

    return res;
}

static int benchmark_fifo_malloc(void)
{
    return benchmark_fifo_pool(&mem_pool_libc);
}

#if 1
/** This one tries to trigger an allocation each time
 */
static int benchmark_fifo_worst_case_pool(mem_pool_t *mp)
{
    for (int i = 0; i < settings.num_allocs; i++) {
        void *a = mp_new(mp, byte, 32 * 4096 + i);

        mp_ifree(mp, a);
    }
    return 0;
}

#else
/** This one isn't actually a worst case scenario (only 2 mmap calls) because
 *  of the freepage optimisation in the fifo allocator
 *  We would need 3 pointers to have it need an allocation each time
 */
static int benchmark_fifo_worst_case(void)
{
    mem_pool_t * mp = mem_fifo_pool_new(32 * 4096);
    void *a = NULL;
    void *b = NULL;

    for (int i = 0; i < settings.num_allocs / 2; i++) {
        a = mp_new(mp, byte, 33 * 4096);
        if (b) {
            mp_ifree(mp, b);
        }
        b = mp_new(mp, byte, 256);
        mp_ifree(mp, a);
    }
    mp_ifree(mp, b);
    mem_fifo_pool_delete(&mp);
    return 0;
}
#endif

static int benchmark_fifo_worst_case(void)
{
    int res;
    mem_pool_t *mp = mem_fifo_pool_new("worst-case", 32 * 4096);

    res = benchmark_fifo_worst_case_pool(mp);
    mem_fifo_pool_delete(&mp);

    return res;
}

static int benchmark_fifo_worst_case_malloc(void)
{
    return benchmark_fifo_worst_case_pool(&mem_pool_libc);
}

/* }}} */
/* {{{ Stack benchmarks */

/** Stack allocator bench
 *
 * Runs NUM_TRIES times the function recursive_memory_user, ith a random depth
 * between 0 and MAX_DEPTH
 *
 * recursive_memory_user performs a random number of allocations between 0 and
 * MAX_ALLOCS using the stack allocator, calls itself recursively, performs
 * some allocations again and returns.
 */
static int recursive_memory_user(int depth)
{
    t_scope;
    int size = rand() % MAX(2 * settings.num_allocs /
                        (settings.num_tries * settings.max_depth), 1);
    byte **mem = t_new_raw(byte *, size);

    for (int i = 0; i < size; i++) {
        mem[i] = t_new_raw(byte, rand() % settings.max_alloc_size);
    }

    if (likely(depth > 0)) {
        recursive_memory_user(depth - 1);
    }
    for (int i = 0; i < size; i++) {
        mem[i] = t_new_raw(byte, rand() % settings.max_alloc_size);
    }
    return 0;
}

static int benchmark_stack(void)
{
    for (int i = 0; i < settings.num_tries; i++) {
        int depth = rand() % settings.max_depth;

        recursive_memory_user(depth);
    }
    return 0;
}

/** Same bench with malloc
 */
static int recursive_memory_user_malloc(int depth)
{
    int size = rand() % MAX(2 * settings.num_allocs /
                        (settings.num_tries * settings.max_depth), 1);
    byte **mem = p_new_raw(byte *, size * 2);

    for (int i = 0; i < size; i ++) {
        mem[i] = p_new_raw(byte, rand() % settings.max_alloc_size);
    }

    if (likely(depth > 0)) {
        recursive_memory_user(depth - 1);
    }
    for (int i = size; i < 2 * size; i ++) {
        mem[i] = p_new_raw(byte, rand() % settings.max_alloc_size);
    }
    for (int i = 0; i < size; i ++) {
        p_delete(&mem[i]);
    }
    p_delete(&mem);
    return 0;
}

static int benchmark_stack_malloc(void)
{
    for (int i = 0; i < settings.num_tries; i ++) {
        int depth = rand() % settings.max_depth;

        recursive_memory_user_malloc(depth);
    }
    return 0;
}

/** This function's branching behaviour is random, and allows a more realistic
 * check.
 * It is launched with -sw
 * XXX: it always terminates, but it can take a long time...
 */
static void random_recursive_func(int depth)
{
    t_scope;
    int size = rand() % (2 * settings.num_allocs /
                        (settings.num_tries * settings.max_depth));
    byte **mem = t_new_raw(byte *, size);
    int threshold = 4100;

    for (int i = 0; i < size; i++) {
        mem[i] = t_new_raw(byte, rand() % settings.max_alloc_size);
    }

  retry:
    if (depth >= settings.max_depth || rand() % 10000 < threshold) {
        return;
    } else {
        random_recursive_func(depth + 1);
        threshold -= 50;
        goto retry;
    }
}

static int benchmark_stack_random(void)
{
    printf("Random stack bench started\n");
    random_recursive_func(0);
    return 0;
}

/* }}} */

/** Times the execution of a function
 */
static void benchmark_func(int func(void), const char* message) {
    proctimer_t pt;
    int elapsed;

    proctimer_start(&pt);
    func();
    elapsed = proctimer_stop(&pt);
    printf("%s done. Elapsed time: %d.%06d s\n", message, elapsed / 1000000,
           elapsed % 1000000);
}

int main(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);

    srand(time(NULL));

    argc = parseopt(argc, argv, popts, 0);
    if (argc != 0 || settings.help
    || (!settings.test_stack && !settings.test_fifo))
    {
        makeusage(0, arg0, "", NULL, popts);
    }

    if (settings.test_stack) {
        printf("Starting stack allocator test...\n");
        if (settings.worst_case) {
            benchmark_func(benchmark_stack_random,
                           "Worst-case stack allocator test.");
        } else {
            benchmark_func(benchmark_stack, "Stack allocator test");
            if (settings.compare) {
                benchmark_func(benchmark_stack_malloc, "With malloc:");
            }
        }

    }
    if (settings.test_fifo) {
        printf("Starting fifo allocator test...\n");
        if (settings.worst_case) {
            benchmark_func(benchmark_fifo_worst_case,
                           "Worst-case fifo allocator test.");
            if (settings.compare) {
                benchmark_func(benchmark_fifo_worst_case_malloc, "With malloc:");
            }
        } else {
            benchmark_func(benchmark_fifo, "Fifo allocator test");
            if (settings.compare) {
                benchmark_func(benchmark_fifo_malloc, "With malloc:");
            }
        }
    }

    return 0;
}

