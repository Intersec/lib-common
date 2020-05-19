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

#ifndef IS_LIB_COMMON_PROMETHEUS_CLIENT_H
#define IS_LIB_COMMON_PROMETHEUS_CLIENT_H

#include <netinet/in.h>

#include <lib-common/core.h>
#include <lib-common/core/core.iop.h>
#include <lib-common/container-qvector.h>

/** Prometheus client.
 *
 * This is a simple implementation of a Prometheus (https://prometheus.io/)
 * client, integrated to the lib-common.
 *
 * Read the documentation here:
 * https://intersec.github.io/lib-common/lib-common/base/prometheus-client.html
 */

/* {{{ Base metric class */

struct prom_metric_t;
union qm_prom_metric_t;

#define PROM_METRIC_FIELDS(pfx) \
    OBJECT_FIELDS(pfx);                                                      \
                                                                             \
    /* Parent fields */                                                      \
    lstr_t name;                                                             \
    lstr_t documentation;                                                    \
    qv_t(cstr) label_names;                                                  \
    union qm_prom_metric_t * nullable children_by_labels;                    \
    dlist_t children_list;                                                   \
                                                                             \
    /* Children fields */                                                    \
    qv_t(cstr) label_values;                                                 \
    pfx##_t * nullable parent;                                               \
                                                                             \
    /* Common fields */                                                      \
    dlist_t siblings_list;                                                   \

#define PROM_METRIC_METHODS(type_t) \
    OBJECT_METHODS(type_t);                                                  \
                                                                             \
    /** Register the metric in the (unique) collector.                       \
     *                                                                       \
     * This method attaches the metric to the collector (ie. the metric will \
     * be scrapped). Only parent metrics can be registered.                  \
     *                                                                       \
     * At registration, the name of the metric and the labels are checked,   \
     * so they must be set at this point. They must follow the guidelines    \
     * explained here:                                                       \
     *                                                                       \
     * https://prometheus.io/docs/concepts/data_model/#metric-names-and-labels \
     *                                                                       \
     * A description must be set too.                                        \
     *                                                                       \
     * The program aborts in case of error.                                  \
     *                                                                       \
     * XXX: not simply called "register" because it is a reserved keyword in \
     *      C...                                                             \
     */                                                                      \
    void (*do_register)(type_t *self);                                       \
                                                                             \
    /** Unregister the metric from the (unique) collector.                   \
     *                                                                       \
     * This is usually not needed as all metrics are destroyed when module   \
     * prometheus_client is shutdown.                                        \
     * Unregistered metrics must be manually destroyed.                      \
     */                                                                      \
    void (*unregister)(type_t *self);                                        \
                                                                             \
    /** Get the child metric corresponding to the given label values.        \
     *                                                                       \
     * Should be callled only on parent metrics having label names, and with \
     * the correct number of label values. The program will abort otherwise. \
     *                                                                       \
     * The returned child is created if needed. It is safe to keep a pointer \
     * on the returned value, as long as it (or the parent metric) is not    \
     * explicitly destroyed.                                                 \
     */                                                                      \
    type_t *(*labels)(type_t *self, const qv_t(cstr) *label_values);         \
                                                                             \
    /** Remove the child metric corresponding to the given label values.     \
     *                                                                       \
     * Should be callled only on parent metrics having label names, and with \
     * the correct number of label values. The program will abort otherwise. \
     */                                                                      \
    void (*remove)(type_t *self, const qv_t(cstr) *label_values);            \
                                                                             \
    /** Remove all the child metrics. */                                     \
    void (*clear)(type_t *self);                                             \

/** Base class for all prometheus metric. */
OBJ_CLASS(prom_metric, object, PROM_METRIC_FIELDS, PROM_METRIC_METHODS);

/* Private helper for metrics creation. Do not use, prefer variants of
 * children classes. */
prom_metric_t *prom_metric_new(const object_class_t *cls,
                               const char *name, const char *documentation,
                               const char **labels, int nb_labels);

/* Private helper for getting a metric's child. Do not use, prefer variants
 * of children classes. */
#define prom_metric_labels(metric, ...)  \
    ({                                                                       \
        const char *__labels[] = { __VA_ARGS__ };                            \
        qv_t(cstr) __labels_qv;                                              \
                                                                             \
        qv_init_static(&__labels_qv, __labels, countof(__labels));           \
        obj_vcall(metric, labels, &__labels_qv);                             \
    })

/* Private remove a child metric. Do not use, prefer variants of children
 * classes. */
#define prom_metric_remove(metric, ...)  \
    do {                                                                     \
        const char *__labels[] = { __VA_ARGS__ };                            \
        qv_t(cstr) __labels_qv;                                              \
                                                                             \
        qv_init_static(&__labels_qv, __labels, countof(__labels));           \
        obj_vcall(metric, remove, &__labels_qv);                             \
    } while (0)


/* {{{ Simple value metric */

#define PROM_SIMPLE_VALUE_METRIC_FIELDS(pfx) \
    PROM_METRIC_FIELDS(pfx);                                                 \
                                                                             \
    /** Metric value.                                                        \
     *                                                                       \
     * You can safely directy modify it in non multi-threaded environments.  \
     * If you need thread safety, use the provided helpers.                  \
     */                                                                      \
    double value;                                                            \
                                                                             \
    /** Spinlock used for threaded access.                                   \
     *                                                                       \
     * We are using spinlocks instead of atomic doubles because:             \
     *  - using atomic doubles require gcc >= 4.9, and we have to support    \
     *    RedHat 7 that has gcc 4.8.                                         \
     *  - spinlocks have better performances in case there is no concurrency \
     *    which is the case in most of our code (but atomic doubles are      \
     *    slightly better in case of high concurrency). The bench tool       \
     *    bench/threaded-operations-bench demonstrates that.                 \
     */                                                                      \
    spinlock_t value_lock;                                                   \


#define PROM_SIMPLE_VALUE_METRIC_METHODS(type_t) \
    PROM_METRIC_METHODS(type_t);                                             \
                                                                             \
    /** Get the current value of the metric. */                              \
    double (*get_value)(type_t *self);                                       \

/** Base class for prometheus metrics have a simple double value. */
OBJ_CLASS(prom_simple_value_metric, prom_metric,
          PROM_SIMPLE_VALUE_METRIC_FIELDS, PROM_SIMPLE_VALUE_METRIC_METHODS);

/* }}} */
/* }}} */
/* {{{ Counter metric */

/* A counter is a cumulative metric that represents a single monotonically
 * increasing counter whose value can only increase or be reset to zero on
 * restart.
 *
 * More details here:
 * https://prometheus.io/docs/concepts/metric_types/#counter
 */

#define PROM_COUNTER_METHODS(type_t) \
    PROM_SIMPLE_VALUE_METRIC_METHODS(type_t);                                \
                                                                             \
    /** Add the given value to the counter.                                  \
     *                                                                       \
     * The value must be positive.                                           \
     */                                                                      \
    void (*add)(type_t *self, double value);                                 \
                                                                             \
    /** Increment (add 1) the counter. */                                    \
    void (*inc)(type_t *self);                                               \


OBJ_CLASS(prom_counter, prom_simple_value_metric,
          PROM_SIMPLE_VALUE_METRIC_FIELDS, PROM_COUNTER_METHODS);

/** All-in-one helper to declare and register a counter metric.
 *
 * \param[in]  name           name of the counter
 * \param[in]  documentation  description of the counter
 * \param[in]  ...            the label names of the counter (empty if the
 *                            counter has no label)
 *
 * \return  the newly created metric, that was registered in the collector
 */
#define prom_counter_new(name, documentation, ...)  \
    ({                                                                       \
        const char *__labels[] = { __VA_ARGS__ };                            \
        prom_metric_t *__new_metric;                                         \
                                                                             \
        __new_metric = prom_metric_new(obj_class(prom_counter),              \
                                       name, documentation,                  \
                                       __labels, countof(__labels));         \
        (prom_counter_t *)__new_metric;                                      \
    })

/** Get the child counter corresponding to the given label values.
 *
 * This a convenience wrapper around the labels() method of the prom_metric_t
 * class.
 *
 * \param[in]  counter  the parent counter
 * \param[in]  ...      the label values (must be in the same number as the
 *                      label names in the parent).
 *
 * \return  the child counter
 */
#define prom_counter_labels(counter, ...)  \
    (prom_counter_t *)prom_metric_labels(obj_vcast(prom_metric, counter),    \
                                         __VA_ARGS__)

/** Remove the child counter corresponding to the given label values.
 *
 * This a convenience wrapper around the remove() method of the prom_metric_t
 * class.
 *
 * \param[in]  counter  the parent counter
 * \param[in]  ...      the label values (must be in the same number as the
 *                      label names in the parent).
 */
#define prom_counter_remove(counter, ...)  \
    prom_metric_remove(obj_vcast(prom_metric, counter), __VA_ARGS__)

/* }}} */
/* {{{ Gauge metric */

/* A gauge is a metric that represents a single numerical value that can
 * arbitrarily go up and down.
 *
 * More details here:
 * https://prometheus.io/docs/concepts/metric_types/#gauge
 */

#define PROM_GAUGE_METHODS(type_t) \
    PROM_SIMPLE_VALUE_METRIC_METHODS(type_t);                                \
                                                                             \
    /** Add the given value to the gauge. */                                 \
    void (*add)(type_t *self, double value);                                 \
                                                                             \
    /** Increment (add 1) to the gauge. */                                   \
    void (*inc)(type_t *self);                                               \
                                                                             \
    /** Substract the given value from the gauge. */                         \
    void (*sub)(type_t *self, double value);                                 \
                                                                             \
    /** Decrement (substract 1) the gauge. */                                \
    void (*dec)(type_t *self);                                               \
                                                                             \
    /** Set the value of the gauge. */                                       \
    void (*set)(type_t *self, double value);                                 \


OBJ_CLASS(prom_gauge, prom_simple_value_metric,
          PROM_SIMPLE_VALUE_METRIC_FIELDS, PROM_GAUGE_METHODS);

/** All-in-one helper to declare and register a gauge metric.
 *
 * \param[in]  name           name of the gauge
 * \param[in]  documentation  description of the gauge
 * \param[in]  ...            the label names of the gauge (empty if the
 *                            gauge has no label)
 *
 * \return  the newly created metric, that was registered in the collector
 */
#define prom_gauge_new(name, documentation, ...)  \
    ({                                                                       \
        const char *__labels[] = { __VA_ARGS__ };                            \
        prom_metric_t *__new_metric;                                         \
                                                                             \
        __new_metric = prom_metric_new(obj_class(prom_gauge),                \
                                       name, documentation,                  \
                                       __labels, countof(__labels));         \
        (prom_gauge_t *)__new_metric;                                        \
    })

/** Get the child gauge corresponding to the given label values.
 *
 * This a convenience wrapper around the labels() method of the prom_metric_t
 * class.
 *
 * \param[in]  gauge  the parent gauge
 * \param[in]  ...    the label values (must be in the same number as the
 *                    label names in the parent).
 *
 * \return  the child gauge
 */
#define prom_gauge_labels(gauge, ...)  \
    (prom_gauge_t *)prom_metric_labels(obj_vcast(prom_metric, gauge),        \
                                         __VA_ARGS__)

/** Remove the child gauge corresponding to the given label values.
 *
 * This a convenience wrapper around the remove() method of the prom_metric_t
 * class.
 *
 * \param[in]  gauge  the parent gauge
 * \param[in]  ...    the label values (must be in the same number as the
 *                    label names in the parent).
 */
#define prom_gauge_remove(gauge, ...)  \
    prom_metric_remove(obj_vcast(prom_metric, gauge), __VA_ARGS__)

/* }}} */
/* {{{ Histogram metric */

/* A histogram is a metric that samples observations (usually things like
 * request durations or response sizes) and counts them in configurable
 * buckets. It also provides a sum of all observed values.
 *
 * More details here:
 * https://prometheus.io/docs/concepts/metric_types/#histogram
 */

#define PROM_HISTOGRAM_FIELDS(pfx) \
    PROM_METRIC_FIELDS(pfx);                                                 \
                                                                             \
    /** Spinlock used for threaded access.                                   \
     *                                                                       \
     * Cf. PROM_SIMPLE_VALUE_METRIC_FIELDS for explanation.                  \
     */                                                                      \
    spinlock_t lock;                                                         \
                                                                             \
    /* Buckets configuration (only set in parent metric) */                  \
    int nb_buckets;                                                          \
    double *bucket_upper_bounds;                                             \
                                                                             \
    /* Histogram value and buckets.                                          \
     *                                                                       \
     * These fields MUST NOT be modified manually; always use the provided   \
     * observe() method.                                                     \
     * They are only set in observable metrics.                              \
     */                                                                      \
    double count;                                                            \
    double sum;                                                              \
    double *bucket_counts;                                                   \


#define PROM_HISTOGRAM_METHODS(type_t) \
    PROM_METRIC_METHODS(type_t);                                             \
                                                                             \
    /** Set the buckets.                                                     \
     *                                                                       \
     * This MUST be called on the parent metric before observing values.     \
     * The provided upper bounds MUST be sorted, and MUST NOT contain the    \
     * infinite value.                                                       \
     */                                                                      \
    void (*set_buckets)(type_t *self, const qv_t(double) *upper_bounds);     \
                                                                             \
    /** Observe the given value. */                                          \
    void (*observe)(type_t *self, double value);                             \


OBJ_CLASS(prom_histogram, prom_metric,
          PROM_HISTOGRAM_FIELDS, PROM_HISTOGRAM_METHODS);

/** All-in-one helper to declare and register a histogram metric.
 *
 * \warning the buckets of the histogram have to be set after that and before
 *          observing the histogram, by using the helpers provided below.
 *
 * \param[in]  name           name of the histogram
 * \param[in]  documentation  description of the histogram
 * \param[in]  ...            the label names of the histogram (empty if the
 *                            histogram has no label)
 *
 * \return  the newly created metric, that was registered in the collector
 */
#define prom_histogram_new(name, documentation, ...)  \
    ({                                                                       \
        const char *__labels[] = { __VA_ARGS__ };                            \
        prom_metric_t *__new_metric;                                         \
                                                                             \
        __new_metric = prom_metric_new(obj_class(prom_histogram),            \
                                       name, documentation,                  \
                                       __labels, countof(__labels));         \
        (prom_histogram_t *)__new_metric;                                    \
    })

/** Set the buckets used for an histogram metric, by giving the ordered list
 *  of upper bounds.
 *
 * \param[in]  histogram  name histogram (parent) metric
 * \param[in]  ...        the list of ordered upper bounds (double type)
 */
#define prom_histogram_set_buckets(histogram, ...)  \
    do {                                                                     \
        const double __upper_bounds[] = { __VA_ARGS__ };                     \
        qv_t(double) __upper_bounds_qv;                                      \
                                                                             \
        qv_init_static(&__upper_bounds_qv, __upper_bounds,                   \
                       countof(__upper_bounds));                             \
        obj_vcall(histogram, set_buckets, &__upper_bounds_qv);               \
    } while (0)

/** Histogram default buckets.
 *
 * The default buckets are tailored to broadly measure the response time
 * (in seconds) of a network service. Most likely, however, you will be
 * required to define buckets customized to your use case.
 *
 * (inspired by official golang client default buckets)
 */
#define PROM_DEFAULT_BUCKETS  .005, .01, .025, .05, .1, .25, .5, 1, 2.5, 5, 10

/** Set the default buckets for an histogram metric. */
#define prom_histogram_set_default_buckets(histogram)  \
    prom_histogram_set_buckets(histogram, PROM_DEFAULT_BUCKETS)

/** Get the child histogram corresponding to the given label values.
 *
 * This a convenience wrapper around the labels() method of the prom_metric_t
 * class.
 *
 * \param[in]  histogram  the parent histogram
 * \param[in]  ...        the label values (must be in the same number as the
 *                        label names in the parent).
 *
 * \return  the child histogram
 */
#define prom_histogram_labels(histogram, ...)  \
    (prom_histogram_t *)prom_metric_labels(obj_vcast(prom_metric, histogram),\
                                           __VA_ARGS__)

/* }}} */
/* {{{ HTTP server for scraping */

/** Start the HTTP server for scraping.
 *
 * \param[in]  cfg  HTTP configuration of the server.
 * \param[out] err  error buffer, filled in case of error.
 *
 * \return  0 on success, a negative value on error.
 */
int prom_http_start_server(const core__httpd_cfg__t *cfg,
                           sb_t * nullable err);

/** Get the information of the running HTTP server.
 *
 * Useful if the port was automatically attributed.
 *
 * \param[out] host  the listening host.
 * \param[out] port  the listening port.
 * \param[out] fd    the file descriptor of the server.
 */
void prom_http_get_infos(lstr_t * nullable host, in_port_t * nullable port,
                         int * nullable fd);

/* }}} */
/* {{{ Module */

/** Prometheus client code module.
 *
 * This module must be initialized to use the prometheus client library.
 * When released, all the registered metrics are destroyed, and the HTTP
 * server is closed if it was started.
 */
MODULE_DECLARE(prometheus_client);

/* }}} */

#endif /* IS_LIB_COMMON_PROMETHEUS_CLIENT_H */
