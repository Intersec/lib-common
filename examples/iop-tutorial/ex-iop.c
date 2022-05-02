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

#include <lib-common/parseopt.h>
#include <lib-common/iop-rpc.h>
#include "exiop.iop.h"

qvector_t(ichannel, ichannel_t *);

static struct {
    bool is_closing;
    el_t blocker;

    el_t        ic_srv;    /*< ichannel listener */
    ichannel_t  remote_ic; /*< remote ichannel */

    qm_t(ic_cbs) ic_impl;  /*< implementations table */

    bool opt_help;
    bool opt_version;
    bool opt_client;
    bool opt_server;

    qv_t(ichannel) clients;
} exiop_g = {
#define _G  exiop_g
    .ic_impl = QM_INIT(ic_cbs, _G.ic_impl),
};

/* {{{ utils */

static void exiop_addr_resolve(const lstr_t s, sockunion_t *out)
{
    pstream_t host;
    in_port_t port;

    if (addr_parse(ps_initlstr(&s), &host, &port, -1))
        e_fatal("unable to parse address: %*pM", LSTR_FMT_ARG(s));
    if (addr_info(out, AF_UNSPEC, host, port))
        e_fatal("unable to resolve address: %*pM", LSTR_FMT_ARG(s));
}

static el_t exiop_ic_listento(lstr_t addr, int (*on_accept)(el_t ev, int fd))
{
    sockunion_t su;
    el_t ev;

    exiop_addr_resolve(addr, &su);

    if (!(ev = ic_listento(&su, SOCK_STREAM, IPPROTO_TCP, on_accept)))
        e_fatal("cannot bind on %*pM", LSTR_FMT_ARG(addr));

    return ev;
}

/* }}} */
/* {{{ client */


static void IOP_RPC_IMPL(exiop__hello_mod, hello_interface, send_async)
{
    e_trace(0, "received: msg = '%s', from client = %d", arg->msg.s,
            arg->seqnum);
}

static void IOP_RPC_CB(exiop__hello_mod, hello_interface, send)
{
    if (status != IC_MSG_OK) {
        const char *error;

        if (status != IC_MSG_EXN) {
            error = ic_status_to_string(status);
        } else {
            error = exn->desc.s;
        }

        e_error("cannot send %s", error);
        return;
    }

    e_trace(0, "helloworld: res = %d", res->res);
}

static void exiop_client_on_event(ichannel_t *ic, ic_event_t evt)
{
    if (evt == IC_EVT_CONNECTED) {
        ic_msg_t *msg = ic_msg_new(0);

        e_notice("connected to server");

        /* send message to server */
        ic_query2(ic, msg, exiop__hello_mod, hello_interface, send,
                  .seqnum = 1,
                  .msg    = LSTR_IMMED("From client : Hello (1)"));
    } else
    if (evt == IC_EVT_DISCONNECTED) {
        e_warning("disconnected from server");
    }
}

static void exiop_client_initialize(const char *addr)
{
    ic_init(&_G.remote_ic);
    _G.remote_ic.on_event = &exiop_client_on_event;
    _G.remote_ic.impl     = &_G.ic_impl;

    exiop_addr_resolve(LSTR(addr), &_G.remote_ic.su);

    if (ic_connect(&_G.remote_ic) < 0)
        e_fatal("cannot connect to %s", addr);

    /* Register RPCs */
    ic_register(&_G.ic_impl, exiop__hello_mod, hello_interface, send_async);
}

/* }}} */
/* {{{ Server implementation */

static void IOP_RPC_IMPL(exiop__hello_mod, hello_interface, send)
{
    e_trace(0, "helloworld: msg = %s, seqnum = %d", arg->msg.s,
            arg->seqnum);
    ic_reply(ic, slot, exiop__hello_mod, hello_interface, send, .res = 1);

    /* send message to clients */
    tab_for_each_entry(ic_client, &_G.clients) {
        ic_msg_t *msg;

        if (ic == ic_client) {
            continue;
        }

        ic_query2(ic_client, msg = ic_msg_new(0),
                  exiop__hello_mod, hello_interface, send_async,
                 .seqnum = 0,
                 .msg    = arg->msg);

    }
}

static void exiop_server_on_event(ichannel_t *ic, ic_event_t evt)
{
    if (evt == IC_EVT_CONNECTED) {
        e_notice("client %p connected", ic);

        qv_append(&_G.clients, ic);

    } else
    if (evt == IC_EVT_DISCONNECTED) {
        e_warning("client %p disconnected", ic);

        /* delete client from vector */
        tab_for_each_pos(i, &_G.clients) {
            if (_G.clients.tab[i] == ic) {
                qv_remove(&_G.clients, i);
                break;
            }
        }
    }
}

static int exiop_on_accept(el_t ev, int fd)
{
    ichannel_t *ic;

    e_trace(0, "incoming connection");
    ic              = ic_new();
    ic->on_event    = &exiop_server_on_event;
    ic->impl        = &_G.ic_impl;
    ic->do_el_unref = true;

    ic_spawn(ic, fd, NULL);
    return 0;
}

static void exiop_server_initialize(const char *addr)
{
    /* Start listening */
    _G.ic_srv = exiop_ic_listento(LSTR(addr), &exiop_on_accept);

    /* Register RPCs */
    ic_register(&_G.ic_impl, exiop__hello_mod, hello_interface, send);
}

/* }}} */
/* {{{ initialize & shutdown */

static popt_t popts[] = {
    OPT_GROUP("Options:"),
    OPT_FLAG('h', "help",    &_G.opt_help,     "show this help"),
    OPT_FLAG('v', "version", &_G.opt_version,  "show version"),
    OPT_FLAG('C', "client",  &_G.opt_client,   "client mode"),
    OPT_FLAG('S', "server",  &_G.opt_server,   "server mode"),
    OPT_END(),
};

static void exiop_on_term(el_t idx, int signum, data_t priv)
{
    if (_G.is_closing)
        return;

    /* Close the remote connection */
    if (_G.opt_client)
        ic_bye(&_G.remote_ic);

    /* Make event loop to stop */
    el_unregister(&_G.blocker);

    _G.is_closing = true;
}

int main(int argc, char **argv)
{
    const char *arg0 = NEXTARG(argc, argv);

    /* Read command line */
    argc = parseopt(argc, argv, popts, 0);
    if (_G.opt_help || argc != 1) {
        makeusage(EXIT_FAILURE, arg0, "<address>", NULL, popts);
    }

    if (_G.opt_version) {
        e_notice("HELLO - Version 1.0");
        return EXIT_SUCCESS;
    }

    /* initialize the ichannel library */
    MODULE_REQUIRE(ic);

    if (_G.opt_client) {
        e_notice("launching in client mode…");
        exiop_client_initialize(argv[0]);
    } else
    if (_G.opt_server) {
        e_notice("launching in server mode…");
        exiop_server_initialize(argv[0]);
    }

    /* Register signals & blocker */
    _G.blocker = el_blocker_register();
    el_signal_register(SIGTERM, &exiop_on_term, NULL);
    el_signal_register(SIGINT,  &exiop_on_term, NULL);
    el_signal_register(SIGQUIT, &exiop_on_term, NULL);

    /* got into event loop */
    el_loop();

    if (_G.opt_client)
        ic_wipe(&_G.remote_ic);

    qv_wipe(&_G.clients);

    MODULE_RELEASE(ic);
    return 0;
}

/* }}} */
