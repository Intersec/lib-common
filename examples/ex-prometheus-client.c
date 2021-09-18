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

#include <lib-common/el.h>
#include <lib-common/iop.h>
#include <lib-common/parseopt.h>
#include <lib-common/prometheus-client.h>

static struct {
    /* Event loop blocker */
    el_t blocker;

    /* Prometheus metrics */
    prom_counter_t   *counter_no_label;
    prom_counter_t   *counter_labels;
    prom_gauge_t     *gauge_labels;
    prom_histogram_t *histo_no_label;
    prom_histogram_t *histo_timing;
    el_t metrics_cron;

    /* Command-line options */
    bool opt_help;
    int  opt_port;
} ex_prometheus_client_g = {
#define _G  ex_prometheus_client_g
    .opt_port = 8080,
};

/* {{{ Metrics cron */

static void prom_gauge_random(int min, int max)
{
    t_scope;
    prom_gauge_t *child;

    child = prom_gauge_labels(_G.gauge_labels,
                              t_fmt("%d", min), t_fmt("%d", max));
    obj_vcall(child, set, rand_ranged(min, max));
}

static void metrics_cron(el_t el, data_t data)
{
    prom_counter_t *child;

    /* Increment the simple counter with no label; use the thread-safe API for
     * that, even if this is not needed in this mono-thread application. */
    obj_vcall(_G.counter_no_label, inc);

    /* Have a first child for the counter with labels; also use the
     * thread-safe API to update its value. */
    child = prom_counter_labels(_G.counter_labels, "2");
    obj_vcall(child, add, 2);

    /* Have a second child with factor 4; this time we directly increment the
     * value which is authorized because we don't need thread-safety in our
     * mono-thread application. */
    child = prom_counter_labels(_G.counter_labels, "4");
    child->value += 4;

    /* Have 3 gauge children, with random numbers */
    prom_gauge_random(-100,   0);
    prom_gauge_random(   0, 100);
    prom_gauge_random(-100, 100);

    /* Observe the histogram with a random number */
    obj_vcall(_G.histo_no_label, observe, rand_ranged(0, 120));

    /* Example of usage of prom_histogram_timer_scope */
    {
        prom_histogram_timer_scope(_G.histo_timing);
        int useconds = rand_range(0, 1000);

        usleep(useconds);
    }
}

/* }}} */
/* {{{ main() / start client */

static popt_t popts_g[] = {
    OPT_GROUP("Options:"),
    OPT_FLAG('h', "help",  &_G.opt_help, "show this help"),
    OPT_INT( 'p', "port",  &_G.opt_port, "listening port (default 8080)"),
    OPT_END(),
};

static void prom_client_on_term(el_t idx, int signum, data_t priv)
{
    /* Unregister blocker to exit the event loop */
    el_unregister(&_G.blocker);

    /* Unregister the metrics cron */
    el_unregister(&_G.metrics_cron);
}

int main(int argc, char **argv)
{
    t_scope;
    SB_1k(err);
    const char *arg0 = NEXTARG(argc, argv);
    core__httpd_cfg__t httpd_cfg;

    /* Read command line */
    argc = parseopt(argc, argv, popts_g, 0);
    if (_G.opt_help || argc != 0) {
        makeusage(EXIT_FAILURE, arg0, "", NULL, popts_g);
    }

    /* Register signals & blocker */
    _G.blocker = el_blocker_register();
    el_signal_register(SIGTERM, &prom_client_on_term, NULL);
    el_signal_register(SIGINT,  &prom_client_on_term, NULL);
    el_signal_register(SIGQUIT, &prom_client_on_term, NULL);

    /* Initialize the prometheus client library */
    MODULE_REQUIRE(prometheus_client);

    /* Run prometheus HTTP server for scraping */
    iop_init(core__httpd_cfg, &httpd_cfg);
    httpd_cfg.bind_addr = t_lstr_fmt("0.0.0.0:%d", _G.opt_port);
    if (prom_http_start_server(&httpd_cfg, &err) < 0) {
        e_fatal("cannot start HTTP server: %*pM", SB_FMT_ARG(&err));
    }

    /* Create some metrics */
    _G.counter_no_label = prom_counter_new(
        "ex:counter_no_label",
        "A simple auto-incremented counter with no label",
    );
    _G.counter_labels = prom_counter_new(
        "ex:counter_labels",
        "A counter with one label; each time series is incremented by the "
        "factor every second",
        "factor",
    );
    _G.gauge_labels = prom_gauge_new(
        "ex:gauge_labels",
        "A gauge with two labels; each time series contains random numbers "
        "between min and max",
        "min", "max",
    );
    _G.histo_no_label = prom_histogram_new(
        "ex:histogram_no_label",
        "An histogram with linear buckets from 10 to 100 (step 10)",
    );
    prom_histogram_set_linear_buckets(_G.histo_no_label, 10, 10, 10);
    _G.histo_timing = prom_histogram_new(
        "ex:histogram_timer_seconds",
        "An histogram to observe he duration of a block of code",
    );
    prom_histogram_set_linear_buckets(_G.histo_timing, 0.0001, 0.0001, 10);

    /* Register the cron that will be called every second to update the
     * metrics values */
    _G.metrics_cron = el_timer_register(1000, 1000, EL_TIMER_LOWRES,
                                        &metrics_cron, NULL);

    /* Run event loop */
    el_loop();

    /* Release the prometheus client library */
    MODULE_RELEASE(prometheus_client);

    return 0;
}

/* }}} */
