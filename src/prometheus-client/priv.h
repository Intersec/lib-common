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

#ifndef PROMETHEUS_CLIENT_PRIV_H
#define PROMETHEUS_CLIENT_PRIV_H

#include <lib-common/prometheus-client.h>
#include <lib-common/log.h>

/** Base logger for all prometheus client code modules. */
extern logger_t prom_logger_g;

/** Default (and unique) prometheus collector.
 *
 * In our simplified implementation of the prometheus client, we have a unique
 * collector, which is this list. For this reason, we do not have a notion of
 * collector registry.
 *
 * It chains the registered metrics in their order of registration.
 */
extern dlist_t prom_collector_g;

/** Validate the name of a metric.
 *
 * Exposed for tests.
 */
int prom_metric_check_name(lstr_t name);

/** Validate the name of a metric label.
 *
 * Exposed for tests.
 */
int prom_metric_check_label_name(const char *label_name);

/** Bridge function for default metric exposition format.
 *
 * It fills the output buffer with the metrics of the collector exposed in the
 * default metric exposition format, described here:
 *
 * https://prometheus.io/docs/instrumenting/exposition_formats
 */
void prom_collector_bridge(const dlist_t *collector, sb_t *out);

/** Module for HTTP server for scraping. */
MODULE_DECLARE(prometheus_client_http);

#endif /* PROMETHEUS_CLIENT_PRIV_H */
