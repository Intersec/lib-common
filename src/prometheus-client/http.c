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

#include <lib-common/http.h>

#include "priv.h"

static struct {
    logger_t logger;

    lstr_t listen_host;
    in_port_t listen_port;

    el_t httpd;
    httpd_cfg_t *httpd_cfg;
} prom_http_g = {
#define _G  prom_http_g
    .logger = LOGGER_INIT_INHERITS(&prom_logger_g, "http"),
};

/* {{{ "metrics/" query */

static void metrics_query_on_done(httpd_query_t *q)
{
    SB_8k(buf);
    outbuf_t *ob;

    /* Send request headers */
    ob = httpd_reply_hdrs_start(q, HTTP_CODE_OK, true);
    ob_adds(ob, "Content-Type: text/plain; version=0.0.4\n");
    httpd_reply_hdrs_done(q, -1, false);

    /* Reply with metrics data */
    prom_collector_bridge(&prom_collector_g, &buf);
    ob_addsb(ob, &buf);

    httpd_reply_done(q);
}

static void metrics_query_hook(httpd_trigger_t *tcb, struct httpd_query_t *q,
                               const httpd_qinfo_t *qi)
{
    q->on_done = metrics_query_on_done;
    q->qinfo   = httpd_qinfo_dup(qi);
}

/* }}} */
/* {{{ API */

int prom_http_start_server(const core__httpd_cfg__t *cfg,
                           sb_t * nullable err)
{
    t_scope;
    pstream_t host;
    sockunion_t su;
    lstr_t addr = cfg->bind_addr;
    httpd_trigger_t *trigger;

    if (_G.httpd) {
        if (err) {
            sb_addf(err, "server is already running");
            return -1;
        }
    }

    /* Resolve address */
    RETHROW(addr_resolve2("prometheus HTTP server", addr, 0, 0, &su,
                          &host, &_G.listen_port, err));

    /* Start HTTP server */
    _G.httpd_cfg = httpd_cfg_new();
    httpd_cfg_from_iop(_G.httpd_cfg, cfg);
    if (!(_G.httpd = httpd_listen(&su, _G.httpd_cfg))) {
        httpd_cfg_delete(&_G.httpd_cfg);
        if (err) {
            sb_addf(err, "cannot bind HTTP server on %*pM",
                    LSTR_FMT_ARG(addr));
            return -1;
        }
    }

    if (_G.listen_port == 0) {
        _G.listen_port = getsockport(el_fd_get_fd(_G.httpd), su.family);
        addr = t_lstr_fmt("%*pM:%d", PS_FMT_ARG(&host), _G.listen_port);
    }
    _G.listen_host = lstr_dup(LSTR_PS_V(&host));

    /* Register "metrics/" trigger */
    trigger     = httpd_trigger_new();
    trigger->cb = metrics_query_hook;
    httpd_trigger_register(_G.httpd_cfg, GET, "/metrics", trigger);

    logger_notice(&_G.logger, "listening for prometheus scraping on %*pM",
                  LSTR_FMT_ARG(addr));
    return 0;
}

void prom_http_get_infos(lstr_t * nullable host, in_port_t * nullable port,
                         int * nullable fd)
{
    if (host) {
        *host = _G.listen_host;
    }
    if (port) {
        *port = _G.listen_port;
    }
    if (fd) {
        *fd = el_fd_get_fd(_G.httpd);
    }
}

/* }}} */
/* {{{ Module */

static int prometheus_client_http_initialize(void *arg)
{
    return 0;
}

static void prometheus_client_http_on_term(int signo)
{
    httpd_unlisten(&_G.httpd);
}

static int prometheus_client_http_shutdown(void)
{
    lstr_wipe(&_G.listen_host);
    httpd_unlisten(&_G.httpd);
    httpd_cfg_delete(&_G.httpd_cfg);
    return 0;
}

MODULE_BEGIN(prometheus_client_http)
    MODULE_DEPENDS_ON(http);
    MODULE_IMPLEMENTS_INT(on_term, &prometheus_client_http_on_term);
MODULE_END()

/* }}} */
