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

#ifndef IS_LIB_COMMON_LOG_IOP_H
#define IS_LIB_COMMON_LOG_IOP_H

#include <lib-common/log.h>
#include <lib-common/iop-rpc.h>
#include "core/core.iop.h"

/* {{{ Configuration */

/** Set the configuration of the logging system.
 */
void logger_configure(const core__log_configuration__t * nonnull conf);

/** IOP configuration interface.
 */

void IOP_RPC_IMPL(core__core, log, set_root_level);
void IOP_RPC_IMPL(core__core, log, reset_root_level);
void IOP_RPC_IMPL(core__core, log, set_logger_level);
void IOP_RPC_IMPL(core__core, log, reset_logger_level);

/* }}} */
/* {{{ Accessors */

qvector_t(logger_conf, core__logger_configuration__t);

/** Returns information about a set of loggers, which can be filtered using
 *  a prefixed name.
 *
 * \param[in] prefix A string that the loggers' full names must start with.
 *                   LSTR_NULL_V can be used to avoid filtering.
 * \param[out] confs The vector holding the information about each logger.
 */
void logger_get_all_configurations(lstr_t prefix,
                                   qv_t(logger_conf) * nonnull confs);

void IOP_RPC_IMPL(core__core, log, list_loggers);

/* }}} */

#endif
