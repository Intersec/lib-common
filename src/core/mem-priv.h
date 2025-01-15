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
 * \param[in,out] lock Lock associated to the list.
 *
 * \param[in,out] logger Logger to use for the warning.
 */
void mem_pool_list_clean(dlist_t *list, const char *pool_type,
                         spinlock_t *lock, logger_t *logger);

static inline void mem_pool_set(mem_pool_t *mp, const char *name,
                                dlist_t *all_pools_list, spinlock_t *lock,
                                const mem_pool_t *base, unsigned flags)
{
    *mp = *base;

    /* Check for non-user flags. */
    assert((flags & ~MEM_USER_FLAGS) == 0);
    mp->mem_pool |= flags;

    if (flags & MEM_DISABLE_POOL_TRACKING) {
        dlist_init(&mp->pool_link);
    } else {
        spin_lock(lock);
        dlist_add_tail(all_pools_list, &mp->pool_link);
        spin_unlock(lock);
    }

    mp->name_v = p_strdup(name);
}

static inline void mem_pool_wipe(mem_pool_t *mp, spinlock_t *lock)
{
    if (mp->mem_pool & MEM_DISABLE_POOL_TRACKING) {
        /* Check that the pool was not inserted in a list despite its flag. */
        assert(dlist_is_empty(&mp->pool_link));
    } else {
        spin_lock(lock);
        dlist_remove(&mp->pool_link);
        spin_unlock(lock);
    }
    p_delete(&mp->name_v);
}

#endif /* IS_LIB_COMMON_CORE_MEM_PRIV_H */
