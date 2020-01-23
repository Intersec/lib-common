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

#include <lib-common/unix.h>
#include <lib-common/datetime.h>
#include <lib-common/zbenchmark.h>

typedef struct mcms_event_t {
    int stamp;
    char type;  /* should be int, but it breaks the log_file format */

    int64_t msisdn;

    int camp_lineno;
    int camp_id;
    int user_id;

    uint32_t remote_id;

    int payload_len;
    byte *payload;
    unsigned payload_allocated;
} mcms_event_t;

static long snprintf_event(const mcms_event_t *event)
{
    char buf[BUFSIZ];

    return snprintf(buf, sizeof(buf),
                    "%d|%c|%lld|%d|%d|%u|%d|\n",
                    event->stamp, event->type,
                    (long long)event->msisdn,
                    event->camp_lineno, event->camp_id,
                    event->remote_id, event->payload_len);
}

ZBENCH_GROUP_EXPORT(iprintf_bench) {
    ZBENCH(snprintf) {
        int i = 0;
        mcms_event_t ev, *event = &ev;

        p_clear(event, 1);
        ZBENCH_LOOP() {
            event->stamp = 1178096605;
            event->type = "ABDG"[i & 3];
            event->msisdn = 33612345678LL + i + (i ^ 4321);
            event->camp_lineno = i & 16383;
            event->camp_id = i >> 14;
            event->remote_id = 1;
            event->payload_len = 0;
            i++;

            ZBENCH_MEASURE() {
                for (int j = 0; j < 1000; j++) {
                    snprintf_event(event);
                }
            } ZBENCH_MEASURE_END
        } ZBENCH_LOOP_END
    } ZBENCH_END
} ZBENCH_GROUP_END
