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

#include "priv.h"

logger_t prom_logger_g = LOGGER_INIT_INHERITS(NULL, "prometheus");

dlist_t prom_collector_g;


static int prometheus_client_initialize(void *arg)
{
    dlist_init(&prom_collector_g);
    return 0;
}

static int prometheus_client_shutdown(void)
{
    prom_metric_t *metric;

    dlist_for_each_entry(metric, &prom_collector_g, siblings_list) {
        obj_delete(&metric);
    }

    return 0;
}

MODULE_BEGIN(prometheus_client)
    MODULE_DEPENDS_ON(prometheus_client_http);
MODULE_END()
