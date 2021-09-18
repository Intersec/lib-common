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

#include <lib-common/core.h>
#include <lib-common/container.h>
#include <lib-common/log.h>
#include <lib-common/parseopt.h>
#include <lib-common/datetime.h>

/** Small utility to check the behavior of containers.
 */

static struct {
    logger_t logger;

    /* Command-line options. */
    bool opt_help;
    bool opt_ascii_iqhash;
} ztst_container_g = {
#define _G  ztst_container_g
    .logger = LOGGER_INIT_INHERITS(NULL, "ztst-container"),
};

/* {{{ ASCII insensitive qhash */


/* Old hashing function kept for benchmark comparison.
 */
static inline uint32_t
qhash_lstr_ascii_ihash_old(const qhash_t * nullable qh,
                           const lstr_t * nonnull ls)
{
    t_scope;
    lstr_t tmp = t_lstr_dup(*ls);

    lstr_ascii_tolower(&tmp);
    return qhash_lstr_hash(qh, &tmp);
}

qh_kvec_t(ilstr_old, lstr_t, qhash_lstr_ascii_ihash_old,
          qhash_lstr_ascii_iequal);

static void ztst_run_ascii_iqhash(void)
{
#define NB_WORDS 100000
#define WORD_MAXLEN 100
    t_scope;
    qh_t(lstr)      h_lstr;
    qh_t(ilstr)     h_ilstr;
    qh_t(ilstr_old) h_ilstr_old;
    qv_t(lstr) strs;

    qv_init(&strs);
    qh_init(lstr, &h_lstr);
    qh_init(ilstr, &h_ilstr);
    qh_init(ilstr_old, &h_ilstr_old);

    /* Make some random words */
    for (int i = 0; i < NB_WORDS; i++) {
        lstr_t s = LSTR_INIT(t_new_raw(char, WORD_MAXLEN + 1),
                             rand_range(1, WORD_MAXLEN));

        for (int j = 0; j < s.len; j++) {
            s.v[j] = rand_range(32, 176);
        }
        s.v[s.len] = '\0';

        qv_append(&strs, s);
    }

#define RUN_TEST(pfx) \
    do {                                                                     \
        proctimerstat_t st;                                                  \
                                                                             \
        p_clear(&st, 1);                                                     \
        for (int i = 0; i < 1000; i++) {                                     \
            proctimer_t pt;                                                  \
                                                                             \
            qh_clear(pfx, &h_ ## pfx);                                       \
            proctimer_start(&pt);                                            \
            tab_for_each_ptr(s, &strs) {                                     \
                qh_add(pfx, &h_ ## pfx, s);                                  \
            }                                                                \
            proctimer_stop(&pt);                                             \
            proctimerstat_addsample(&st, &pt);                               \
        }                                                                    \
        logger_notice(&_G.logger, "%d (%d) words inserted in qh " #pfx       \
                      " in %s", strs.len, qh_len(pfx, &h_ ## pfx),           \
                      proctimerstat_report(&st, NULL));                      \
    } while (0)

    RUN_TEST(lstr);
    RUN_TEST(ilstr);
    RUN_TEST(ilstr_old);

    qh_wipe(lstr, &h_lstr);
    qh_wipe(ilstr, &h_ilstr);
    qh_wipe(ilstr_old, &h_ilstr_old);
    qv_wipe(&strs);
#undef RUN_TEST
#undef NB_WORDS
#undef WORD_MAXLEN
}

/* }}} */

static popt_t popts_g[] = {
    OPT_FLAG('h', "help", &_G.opt_help, "show this help"),
    OPT_FLAG('a', "ascii-iqhash", &_G.opt_ascii_iqhash,
             "run ASCII insensitive qhash tests"),
    OPT_END(),
};

int main(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);

    argc = parseopt(argc, argv, popts_g, 0);
    if (argc != 0 || _G.opt_help) {
        makeusage(0, arg0, "", NULL, popts_g);
    }

    if (_G.opt_ascii_iqhash) {
        ztst_run_ascii_iqhash();
    }

    return 0;
}
