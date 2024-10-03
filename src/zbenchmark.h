/***************************************************************************/
/*                                                                         */
/* Copyright 2024 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_ZBENCHMARK_H
#define IS_LIB_COMMON_ZBENCHMARK_H

#include <lib-common/datetime.h>

/** This file is used to easily create benchmarks of C code.
 *
 * A typical benchmark looks like this:
 *
 * ZBENCH_GROUP_EXPORT(my_bench_group) {
 *      my_bench_group_setup();
 *
 *      ZBENCH(my_bench) {
 *          my_bench_setup();
 *
 *          ZBENCH_LOOP() {
 *              my_bench_loop_setup();
 *
 *              ZBENCH_MEASURE() {
 *                  my_function_to_bench();
 *              } ZBENCH_MEASURE_END
 *
 *              my_bench_loop_teardown();
 *          } ZBENCH_LOOP_END
 *
 *          my_bench_teardown();
 *      } ZBENCH_END
 *
 *      my_bench_group_teardown();
 * } ZBENCH_GROUP_END
 */

/** Define a group of zbenchmarks.
 *
 *\param[in] _name The name of the group of zbenchmarks.
 */
#define ZBENCH_GROUP_EXPORT(_name, ...)  _ZBENCH_GROUP_EXPORT(_name)

/** End of definition of a group of zbenchmarks.
 */
#define ZBENCH_GROUP_END  _ZBENCH_GROUP_END

/** Define a zbenchmark.
 *
 * This must be used inside the definition of a group of zbenchmarks.
 *
 * Only the code inside ZBENCHMARK_START() and ZBENCHMARK_STOP() will be
 * measured.
 *
 *\param[in] _name The name of the benchmark.
 */
#define ZBENCH(_name, ...)  _ZBENCH(_name)

/** End of definition of a zbenchmark.
 */
#define ZBENCH_END  _ZBENCH_END

/** Enter the benchmarking loop in the zbenchmark.
 *
 * This must be used inside the definition of a zbenchmark.
 */
#define ZBENCH_LOOP()  _ZBENCH_LOOP()

/** End of benchmarking in the zbenchmark.
 */
#define ZBENCH_LOOP_END  _ZBENCH_LOOP_END

/** Benchmark the following code in the zbenchmark.
 *
 * This must be used inside the definition of a zbenchmark loop.
 */
#define ZBENCH_MEASURE()  _ZBENCH_MEASURE()

/** End of benchmarking in the zbenchmark.
 */
#define ZBENCH_MEASURE_END  _ZBENCH_MEASURE_END

/** Run the registered benchmarks with main() arguments. */
int zbenchmark_main(int argc, char **argv);

/* {{{ Low-level macros and functions. */

/** Definition of a group of zbenchmarks. */
typedef struct zbenchmark_group_t {
    /** Name of the group of zbenchmarks. */
    const char *name;

    /** Function of the group of zbenchmarks. */
    void (*func)(void);
} zbenchmark_group_t;

/** Definition of a zbenchmark. */
typedef struct zbenchmark_t {
    /** Name of the of zbenchmark. */
    const char *name;
} zbenchmark_t;

/** Define a group of zbenchmarks.
 */
#define _ZBENCH_GROUP_EXPORT(_name)                                          \
    static void _zbenchmark_group_##_name##_func(void);                      \
    static zbenchmark_group_t _zbenchmark_group_##_name##_g = {              \
        .name = #_name,                                                      \
        .func = &_zbenchmark_group_##_name##_func,                           \
    };                                                                       \
    __attribute__((constructor))                                             \
    static void _zbenchmark_group_##_name##_register(void)                   \
    {                                                                        \
        _zbenchmark_register_group(&_zbenchmark_group_##_name##_g);          \
    }                                                                        \
    static void _zbenchmark_group_##_name##_func(void)                       \
    {                                                                        \
        zbenchmark_group_t *const _zbenchmark_current_group =                \
            &_zbenchmark_group_##_name##_g;                                  \
                                                                             \
        {

/** End of definition of a group of zbenchmarks.
 */
#define _ZBENCH_GROUP_END                                                    \
        }                                                                    \
    }                                                                        \

/** Define a zbenchmark.
 */
#define _ZBENCH(_name)                                                       \
    {                                                                        \
        zbenchmark_t _zbenchmark_current = {                                 \
            .name = #_name,                                                  \
        };                                                                   \
                                                                             \
        if (_zbenchmark_should_run(_zbenchmark_current_group,                \
                                   &_zbenchmark_current))                    \
        {                                                                    \
            proctimerstat_t _zbenchmark_stats;                               \
                                                                             \
            p_clear(&_zbenchmark_stats, 1);                                  \
            {

/** End of definition of a zbenchmark.
 */
#define _ZBENCH_END                                                          \
            }                                                                \
            _zbenchmark_print_stats(_zbenchmark_current_group,               \
                                    &_zbenchmark_current,                    \
                                    &_zbenchmark_stats);                     \
        }                                                                    \
    }

/** Enter the benchmarking loop in the zbenchmark.
 */
#define _ZBENCH_LOOP()                                                       \
    for (int _zbenchmark_cnt = 0,_zbenchmark_end =                           \
            _zbenchmark_get_nb_runs(_zbenchmark_current_group,               \
                                    &_zbenchmark_current);                   \
         _zbenchmark_cnt < _zbenchmark_end; _zbenchmark_cnt++)               \
    {

/** End of benchmarking in the zbenchmark.
 */
#define _ZBENCH_LOOP_END                                                     \
    }

/** Benchmark the following code in the zbenchmark.
 */
#define _ZBENCH_MEASURE()                                                    \
    {                                                                        \
        proctimer_t _zbenchmark_timer;                                       \
                                                                             \
        p_clear(&_zbenchmark_timer, 1);                                      \
        proctimer_start(&_zbenchmark_timer);                                 \
        {


/** End of benchmarking in the zbenchmark.
 */
#define _ZBENCH_MEASURE_END                                                  \
        }                                                                    \
        proctimer_stop(&_zbenchmark_timer);                                  \
        if (_zbenchmark_is_verbose()) {                                      \
            _zbenchmark_print_measure(_zbenchmark_current_group,             \
                                      &_zbenchmark_current,                  \
                                      &_zbenchmark_timer);                   \
        } else {                                                             \
            proctimerstat_addsample(&_zbenchmark_stats, &_zbenchmark_timer); \
        } \
    }

/** Register a group of benchmarks to be run by zbenchmark_main().
 *
 * \param[in] group  The group of benchmarks.
 */
void _zbenchmark_register_group(zbenchmark_group_t *group);

/** Check if the zbenchmark should be run.
 *
 * \param[in] group  The group of benchmarks.
 * \param[in] bench  The benchmark.
 * \return true if the benchmark should run, false otherwise.
 */
bool _zbenchmark_should_run(const zbenchmark_group_t *group,
                            const zbenchmark_t *bench);

/** Get the number of runs of the zbenchmark.
 *
 * \param[in] group  The group of benchmarks.
 * \param[in] bench  The benchmark.
 * \return The number of runs.
 */
int _zbenchmark_get_nb_runs(const zbenchmark_group_t *group,
                            const zbenchmark_t *bench);

/** Should the run return all measures?
 *
 * \return True to print all measures, false to print min, max, mean.
 */
bool _zbenchmark_is_verbose(void);

/** Print a measure of the zbenchmark.
 *
 * \param[in] group  The group of benchmarks.
 * \param[in] bench  The benchmark.
 * \param[in] stats  The benchmark stats.
 */
void _zbenchmark_print_measure(const zbenchmark_group_t *group,
                               const zbenchmark_t *bench,
                               const proctimer_t *pt);

/** Print the stats of the zbenchmark.
 *
 * \param[in] group  The group of benchmarks.
 * \param[in] bench  The benchmark.
 * \param[in] stats  The benchmark stats.
 */
void _zbenchmark_print_stats(const zbenchmark_group_t *group,
                             const zbenchmark_t *bench,
                             proctimerstat_t *stats);

/* }}} */

#endif /* IS_LIB_COMMON_ZBENCHMARK_H */
