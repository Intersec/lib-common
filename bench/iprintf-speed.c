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
#include <lib-common/datetime.h>

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

int main(int argc, char **argv)
{
    char buf[BUFSIZ];
    proctimer_t pt;
    int elapsed, i, count, len;
    long nbytes;
    mcms_event_t ev, *event = &ev;
    FILE *out1, *out2;

    count = 100000;
    out1 = out2 = NULL;

    if (argc > 1)
        count = parse_number(argv[1]);
    if (argc > 2)
        out1 = fopen(argv[2], "w");
    if (argc > 3)
        out2 = fopen(argv[3], "w");

    p_clear(event, 1);
    event->stamp = 1178096605;

    nbytes = 0;
    proctimer_start(&pt);

    for (i = 0; i < count; i++) {
        event->type = "ABDG"[i & 3];
        event->msisdn = 33612345678LL + i + (i ^ 4321);
        event->camp_lineno = i & 16383;
        event->camp_id = i >> 14;
        event->remote_id = 1;
        event->payload_len = 0;

        len = snprintf(buf, sizeof(buf),
                       "%d|%c|%lld|%d|%d|%u|%d|\n",
                       event->stamp, event->type,
                       (long long)event->msisdn,
                       event->camp_lineno, event->camp_id,
                       event->remote_id, event->payload_len);
        nbytes += len;
        if (out1) {
            IGNORE(fwrite(buf, 1, len, out1));
        }
    }

    elapsed = proctimer_stop(&pt);

    fprintf(stderr, "snprintf: %d tests, %ld bytes, %d.%03d ms.\n",
            count, nbytes, elapsed / 1000, elapsed % 1000);

    nbytes = 0;
    proctimer_start(&pt);

    for (i = 0; i < count; i++) {
        event->type = "ABDG"[i & 3];
        event->msisdn = 33612345678LL + i + (i ^ 4321);
        event->camp_lineno = i & 16383;
        event->camp_id = i >> 14;
        event->remote_id = 1;
        event->payload_len = 0;

        len = isnprintf(buf, sizeof(buf),
                        "%d|%c|%lld|%d|%d|%u|%d|\n",
                        event->stamp, event->type,
                        (long long)event->msisdn,
                        event->camp_lineno, event->camp_id,
                        event->remote_id, event->payload_len);
        nbytes += len;
        if (out2) {
            IGNORE(fwrite(buf, 1, len, out2));
        }
    }

    elapsed = proctimer_stop(&pt);

    fprintf(stderr, "isnprintf: %d tests, %ld bytes, %d.%03d ms.\n",
            count, nbytes, elapsed / 1000, elapsed % 1000);

    p_fclose(&out1);
    p_fclose(&out2);

    return 0;
}
