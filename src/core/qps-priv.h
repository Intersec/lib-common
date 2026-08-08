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

#ifndef IS_LIB_COMMON_CORE_QPS_PRIV_H
#define IS_LIB_COMMON_CORE_QPS_PRIV_H

#include <lib-common/qps.h>

/* On-spool layout of the QPS block headers.
 *
 * These definitions are private to the QPS implementation. Only the tests
 * also use them: they corrupt blocks on purpose to exercise the QPS
 * dissection.
 */

/*
 * used in both pg and memory hdrs
 */
#define QPS_BLK_FREE 2
#define QPS_BLK_USED 0
#define QPS_BLK_PREV_FREE 1
#define QPS_BLK_PREV_USED 0

struct qps_pghdr_t {
    uint16_t flags;
    uint16_t size;
    union {
        struct {
            uint32_t handle;
            uint32_t blk_prev;
        };
        struct {
            qps_pg_t next;
            qps_pg_t prev;
        } free;
    };
};

#define QPS_MBLK_HDRSZ offsetof(qps_mhdr_t, data)
struct qps_mhdr_t {
#if QPS_USE_REDZONES
    uint64_t rz_after_block;
    uint64_t rz_alloc_size;
#endif
    /* Flags field is set as follow:
     *  - bits 31 to 2 for block size (in words of 8 bytes), see
     *    qps_m_blk_size()
     *  - bit 1: tells if the current block is used or freed
     *  - bit 0: like bit 1 but for the previous block */
    uint32_t flags;
    uint32_t handle;
#if QPS_USE_REDZONES
    uint64_t rz_before_block;
#endif

    union {
        struct {
            qps_mhdr_t **prev_next;
            qps_mhdr_t *next;
        } free;
        void *padding[3];
        uint8_t data[0];
    };
};

#endif /* IS_LIB_COMMON_CORE_QPS_PRIV_H */
