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

#ifndef IS_LIB_COMMON_NET_HPACK_H
#define IS_LIB_COMMON_NET_HPACK_H

#include <lib-common/container-qhash.h>
#include <lib-common/container-ring.h>
#include <lib-common/core.h>

/* worst case estimate for hpack-coded integers (uint32_t) */
#define HPACK_BUFLEN_INT 8

/* worst case estimate for hpack-coded string (inefficiently zipped) */
#define HPACK_BUFLEN_STR(len) (HPACK_BUFLEN_INT + (4 * (len)))

/* {{{ Dynamic table (DTBL) */

/* encoder's dtbl entry  */
typedef struct hpack_enc_dtbl_entry_t {
    /* size of this entry including the 32 bytes overhead */
    unsigned sz;
    uint16_t key_id;
    uint16_t val_id;

    /* whether this hdr is the most recent entry with KEY_ID */
    bool mre_key;

    /* whether this hdr is the most recent entry with (KEY_ID, VAL_ID) */
    bool mre_val;
} hpack_enc_dtbl_entry_t;

RING_TYPE(hpack_enc_dtbl_entry_t, hpack_enc_dtbl);
typedef hpack_enc_dtbl_ring hpack_enc_dtbl_ring_t;
qm_k32_t(hpack_insertion_idx, uint32_t);

/* encoder's version of the dynamic table used for compression headers */
typedef struct hpack_enc_dtbl_t {
    /* current total size in bytes of the dtbl entries */
    uint32_t tbl_size;

    /* current dtbl size limit: updated during encoding dts updates */
    uint32_t tbl_size_limit;

    /* ack'ed/negotiated dtbl size: set by user upon settings ack */
    uint32_t tbl_size_max;

    /* ring buffer to hold the dtbl's entries */
    hpack_enc_dtbl_ring_t entries;

    /* counter for dtbl insertions */
    unsigned ins_cnt;

    /* map to the find the most recent hdrs with (KEY_ID, 0) & (KEY_ID,
     * VAL_ID) in the dtbl. The mapped value is the insertion idx of the hdr
     * which is the value of ins_cnt after inserting the respective hdr into
     * the dtbl. The current position of the hdr in the dtbl is calculated as
     * follows: POS_OR_IDX(hdr) = dtbl.ins_cnt - INSERTION_IDX(hdr) */
    qm_t(hpack_insertion_idx) ins_idx;
} hpack_enc_dtbl_t;

hpack_enc_dtbl_t *hpack_enc_dtbl_init(hpack_enc_dtbl_t *dtbl);
void hpack_enc_dtbl_wipe(hpack_enc_dtbl_t *dtbl);

GENERIC_NEW(hpack_enc_dtbl_t, hpack_enc_dtbl)
GENERIC_DELETE(hpack_enc_dtbl_t, hpack_enc_dtbl)

static inline void
hpack_enc_dtbl_init_settings(hpack_enc_dtbl_t *dtbl, uint32_t init_tbl_size)
{
    assert(!dtbl->tbl_size);
    dtbl->tbl_size_limit = dtbl->tbl_size_max = init_tbl_size;
}

static inline int hpack_enc_dtbl_get_count(hpack_enc_dtbl_t *dtbl)
{
    return dtbl->entries.len;
}

typedef struct hpack_dec_dtbl_entry_t {
    lstr_t key;
    lstr_t val;
} hpack_dec_dtbl_entry_t;

RING_TYPE(hpack_dec_dtbl_entry_t, hpack_dec_dtbl);
typedef hpack_dec_dtbl_ring hpack_dec_dtbl_ring_t;

/* decoder's version of the dynamic table used for compression headers */
typedef struct hpack_dec_dtbl_t {
    /* current total size in bytes of the dtbl entries */
    uint32_t tbl_size;

    /* current dtbl size limit: updated during encoding dts updates */
    uint32_t tbl_size_limit;

    /* ack'ed/negotiated dtbl size: set by user upon settings ack */
    uint32_t tbl_size_max;

    /* ring buffer to hold the dtbl's entries */
    hpack_dec_dtbl_ring_t entries;
} hpack_dec_dtbl_t;

hpack_dec_dtbl_t *hpack_dec_dtbl_init(hpack_dec_dtbl_t *dtbl);
void hpack_dec_dtbl_wipe(hpack_dec_dtbl_t *dtbl);

GENERIC_NEW(hpack_dec_dtbl_t, hpack_dec_dtbl)
GENERIC_DELETE(hpack_dec_dtbl_t, hpack_dec_dtbl)

static inline void
hpack_dec_dtbl_init_settings(hpack_dec_dtbl_t *dtbl, uint32_t init_tbl_size)
{
    assert(!dtbl->tbl_size);
    dtbl->tbl_size_limit = dtbl->tbl_size_max = init_tbl_size;
}

static inline int hpack_dec_dtbl_get_count(hpack_dec_dtbl_t *dtbl)
{
    return dtbl->entries.len;
}

/* }}} */
#endif /* IS_LIB_COMMON_NET_HPACK_H */
