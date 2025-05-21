/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
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

#if !defined(IS_LIB_COMMON_NET_H) || defined(IS_LIB_COMMON_NET_RATE_H)
#  error "you must include net.h instead"
#else
#define IS_LIB_COMMON_NET_RATE_H

typedef struct net_rctl_t {
    unsigned     rates[10];
    unsigned     rate;
    unsigned     slice_max;
    unsigned     remains;
    uint16_t     slot;
    bool         is_blk;
    struct ev_t * nonnull cron;
    union {
        void (*nonnull on_ready)(struct net_rctl_t * nonnull);
#ifdef __has_blocks
        block_t __unsafe_unretained nonnull blk;
#endif /* __has_blocks */
    };
} net_rctl_t;

static ALWAYS_INLINE bool net_rctl_can_fire(net_rctl_t * nonnull rctl)
{
    return rctl->remains != 0;
}

static ALWAYS_INLINE void __net_rctl_fire(net_rctl_t * nonnull rctl)
{
    rctl->remains--;
}

static ALWAYS_INLINE bool net_rctl_fire(net_rctl_t * nonnull rctl)
{
    if (net_rctl_can_fire(rctl)) {
        __net_rctl_fire(rctl);
        return true;
    }
    return false;
}

/* The "[static slots_nr]" does not work in C++ code. */
#ifndef __cplusplus

/** Divide a 1 second rate into multiple slots.
 *
 * Get for each slot the number of requests that can be sent.
 *
 * If the total rate per seconds is not a multiple of the number of slots,
 * this algorithm makes sure that:
 *
 *   - the sum of all slots equals the expected rate per second.
 *   - the last slot does not counterbalance the accumulated difference
 *
 * Example for a target rate of 97 requests, the expected result:
 *
 *    [9, 10, 10, 9, 10, 10, 9, 10, 10, 10]
 *
 * Exemple of what naïve implementations could give:
 *
 *    [9, 9, 9, 9, 9, 9, 9, 9, 9, 9]          -> only 90 requests
 *    [9, 9, 9, 9, 9, 9, 9, 9, 9, 16]         -> unbalanced distribution
 *    [10, 10, 10, 10, 10, 10, 10, 10, 10, 7] -> unbalanced distribution
 *
 * \param[in] rate  the number of requests that can be sent per second
 * \param[in] slots_nr  the size of \p slots
 * \param[out] slots  array to fill with the rate per slot
 */
void net_rctl_init_slots(int rate, int slots_nr,
                         unsigned slots[static slots_nr]);
#else
void net_rctl_init_slots(int rate, int slots_nr, unsigned slots[]);
#endif /* __cplusplus */

void net_rctl_init(net_rctl_t * nonnull rctl, int rate,
                   void (*nonnull cb)(net_rctl_t * nonnull));
#ifdef __has_blocks
void net_rctl_init_blk(net_rctl_t * nonnull rctl, int rate,
                       block_t nonnull blk);
#endif /* __has_blocks */

void net_rctl_start(net_rctl_t * nonnull rctl);
void net_rctl_stop(net_rctl_t * nonnull rctl);
void net_rctl_wipe(net_rctl_t * nonnull rctl);

#endif /* !defined(IS_LIB_COMMON_NET_H) || defined(IS_LIB_COMMON_NET_RATE_H)*/
