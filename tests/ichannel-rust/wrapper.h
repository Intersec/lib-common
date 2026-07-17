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
#include "../iop/tstiop_rpc.iop.h"

typedef void (*z_echo_impl_f)(IOP_RPC_IMPL_ARGS(tstiop_rpc__rpc, test, echo));

static inline void z_ic_cbs_init(qm_t(ic_cbs) *impl)
{
    qm_init(ic_cbs, impl);
}
static inline void z_ic_cbs_wipe(qm_t(ic_cbs) *impl)
{
    qm_wipe(ic_cbs, impl);
}
static inline void z_ic_register_echo(qm_t(ic_cbs) *impl, z_echo_impl_f cb)
{
    ic_register_(impl, tstiop_rpc__rpc, test, echo, cb);
}
/* handy constants so Rust doesn't hardcode them */
static inline const iop_rpc_t *z_echo_rpc(void)
{
    return IOP_RPC(tstiop_rpc__rpc, test, echo);
}
static inline int32_t z_echo_cmd(void)
{
    return IOP_RPC_CMD(tstiop_rpc__rpc, test, echo);
}
