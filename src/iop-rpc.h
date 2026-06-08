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

#ifndef IS_LIB_COMMON_IOP_RPC_H
#define IS_LIB_COMMON_IOP_RPC_H

#include <lib-common/el.h>
#include <lib-common/net.h>
#include <lib-common/unix.h>
#include <lib-common/iop.h>
#include <lib-common/http.h>
#include "iop/ic.iop.h"

#if __has_feature(nullability)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic error "-Wnullability-completeness"
#  if __has_warning("-Wnullability-completeness-on-arrays")
#    pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#  endif
#endif

#define IC_SLOT_ERROR (~UINT64_C(0))
#define IC_SLOT_FOREIGN_MASK BITMASK_GE(uint64_t, 62)
#define IC_SLOT_FOREIGN_IC (UINT64_C(0) << 62)
#define IC_SLOT_FOREIGN_HTTP (UINT64_C(1) << 62)

static ALWAYS_INLINE bool ic_slot_is_http(uint64_t slot)
{
    return (slot & IC_SLOT_FOREIGN_MASK) == IC_SLOT_FOREIGN_HTTP;
}

static inline const char *nonnull ic_status_to_string(ic_status__t s)
{
    return iop_enum_to_str(ic_status, s) ?: "UNKNOWN";
}

#include "iop/rpc-channel.h"
#include "iop/rpc-http.h"

/** \brief get the description of the currently unpacked RPC.
 *
 * This low-level function allows to retrieve the description of the currently
 * unpacked RPC. You are allowed to call it only inside of the callback of
 * a RPC implementation.
 */
static inline const iop_rpc_t *nonnull
__ic_get_current_rpc_desc(const ichannel_t *nullable ic, uint64_t slot)
{
#ifndef __cplusplus
    if (ic_slot_is_http(slot)) {
        ichttp_query_t *iq = ichttp_slot_to_query(slot);

        return iq->cbe->fun;
    }
#endif
    return ic->desc;
}

/** \brief get the “command” of the currently unpacked RPC.
 *
 * This low-level function allows to retrieve the “command” value of the
 * currently unpacked RPC. You are allowed to call it only inside of the
 * callback of a RPC implementation.
 */
static inline int
__ic_get_current_rpc_cmd(const ichannel_t *nullable ic, uint64_t slot)
{
#ifndef __cplusplus
    if (ic_slot_is_http(slot)) {
        ichttp_query_t *iq = ichttp_slot_to_query(slot);

        return iq->cbe->cmd;
    }
#endif
    return ic->cmd;
}

#if __has_feature(nullability)
#  pragma GCC diagnostic pop
#endif

#endif
