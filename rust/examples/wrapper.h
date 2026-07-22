/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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
#include <lib-common/iop-rpc.h>
#include "../../examples/iop-tutorial/exiop.iop.h"

/* The IChannel RPC helpers below are macros in C, so they can't be reached
 * through bindgen directly. We wrap them in small `static inline` shims that
 * Rust can call. This mirrors the pattern used by `tests/ichannel-rust`. */

/* {{{ RPC implementation table (qm_t(ic_cbs)) */

typedef void (*exiop_send_impl_f)(
    IOP_RPC_IMPL_ARGS(exiop__hello_mod, hello_interface, send)
);
typedef void (*exiop_send_async_impl_f)(
    IOP_RPC_IMPL_ARGS(exiop__hello_mod, hello_interface, send_async)
);

static inline void exiop_ic_cbs_init(qm_t(ic_cbs) *impl)
{
    qm_init(ic_cbs, impl);
}
static inline void exiop_ic_cbs_wipe(qm_t(ic_cbs) *impl)
{
    qm_wipe(ic_cbs, impl);
}
static inline void
exiop_register_send(qm_t(ic_cbs) *impl, exiop_send_impl_f cb)
{
    ic_register_(impl, exiop__hello_mod, hello_interface, send, cb);
}
static inline void
exiop_register_send_async(qm_t(ic_cbs) *impl, exiop_send_async_impl_f cb)
{
    ic_register_(impl, exiop__hello_mod, hello_interface, send_async, cb);
}

/* }}} */
/* {{{ `send` RPC descriptor / command id (for the Rust ic_query path) */

static inline const iop_rpc_t *exiop_send_rpc(void)
{
    return IOP_RPC(exiop__hello_mod, hello_interface, send);
}
static inline int32_t exiop_send_cmd(void)
{
    return IOP_RPC_CMD(exiop__hello_mod, hello_interface, send);
}

/* }}} */
/* {{{ Server-side helpers */

/* Reply to a `send` query. */
static inline void exiop_reply_send(ichannel_t *ic, uint64_t slot, int res)
{
    ic_reply(ic, slot, exiop__hello_mod, hello_interface, send, .res = res);
}

/* Push a one-way `sendAsync` message. This RPC has `out null`, so it is
 * fire-and-forget: no reply comes back and no completion callback runs, which
 * is why it cannot go through the reply-based Rust `ic_query`. */
static inline void
exiop_send_async_query(ichannel_t *ic, int seqnum, lstr_t msg)
{
    ic_query2(
        ic, ic_msg_new(0), exiop__hello_mod, hello_interface, send_async,
        .seqnum = seqnum, .msg = msg
    );
}

/* }}} */
/* {{{ Socket constants as plain ints for `ic_listento` */

static inline int exiop_sock_stream(void)
{
    return SOCK_STREAM;
}
static inline int exiop_proto_tcp(void)
{
    return IPPROTO_TCP;
}

/* }}} */
