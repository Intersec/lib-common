/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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
#endif
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

void net_rctl_init(net_rctl_t * nonnull rctl, int rate,
                   void (*nonnull cb)(net_rctl_t * nonnull));
#ifdef __has_blocks
void net_rctl_init_blk(net_rctl_t * nonnull rctl, int rate,
                       block_t nonnull blk);
#endif

void net_rctl_start(net_rctl_t * nonnull rctl);
void net_rctl_stop(net_rctl_t * nonnull rctl);
void net_rctl_wipe(net_rctl_t * nonnull rctl);

#endif
