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

#ifndef IS_LIB_COMMON_CORE_MEM_PRIV_H
#define IS_LIB_COMMON_CORE_MEM_PRIV_H

#include <lib-common/core.h>
#include <lib-common/log.h>

/** Clean a mem pool list and log a warning when suspecting a leak.
 *
 * \param[in,out] list List of memory pools to clean. All its elements are
 *                     going to be removed during the call.
 *
 * \param[in] pool_type Pool type to use in the warning message.
 *
 * \param[in] get_mp_name Callback that get the name of a mem pool from its
 *                        list link.
 *
 * \param[in,out] lock Lock associated to the list.
 *
 * \param[in,out] logger Logger to use for the warning.
 *
 * \param[in] supprs List of "suppressions" containing names of memory
 *                   pools that should not be considered as leaked.
 */
void mem_pool_list_clean(dlist_t *nonnull list, const char *nonnull pool_type,
                         const char *nonnull (*nonnull get_mp_name)(
                             const dlist_t *nonnull link),
                         spinlock_t *nonnull lock, logger_t *nonnull logger,
                         const char *nonnull *nullable supprs,
                         int supprs_len);

#endif /* IS_LIB_COMMON_CORE_MEM_PRIV_H */
