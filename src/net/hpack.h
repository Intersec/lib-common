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
/* Encoder API */

/* flag values to control the header encoding function */
enum {
    HPACK_FLG_NOZIP_KEY   = 1u << 0,  /* Encode key as-is (raw)             */
    HPACK_FLG_ZIP_KEY     = 1u << 1,  /* Encode key as Huffman string       */
    HPACK_FLG_NOZIP_VAL   = 1u << 2,  /* Encode value as-is                 */
    HPACK_FLG_ZIP_VAL     = 1u << 3,  /* Encode value as Huffman string     */
    HPACK_FLG_SKIP_STBL   = 1u << 4,  /* Don't look up in the static table  */
    HPACK_FLG_SKIP_DTBL   = 1u << 5,  /* Don't loop up in the dynamic table */
    HPACK_FLG_LWR_KEY     = 1u << 6,  /* Normalize key (lowercase)          */
    HPACK_FLG_SKIP_VAL    = 1u << 7,  /* Don't try to match the value       */
    HPACK_FLG_NOADD_DTBL  = 1u << 8,  /* Don't add hdr to the dynamic table */
    HPACK_FLG_NVRADD_DTBL = 1u << 9,  /* Don't add hdr to the dynamic table
                                       * and instruct proxies to reuse the
                                       * same encoding for this hdr if
                                       * they are to re-encode headers
                                       * (used for sensitive headers)       */
    HPACK_FLG_ADD_DTBL    = 1u << 10, /* Force adding hdr to the dynamic
                                       * table                              */

    HPACK_FLG_NOZIP_STR   = HPACK_FLG_NOZIP_KEY | HPACK_FLG_NOZIP_VAL,
    HPACK_FLG_ZIP_STR     = HPACK_FLG_ZIP_KEY | HPACK_FLG_ZIP_VAL,
    HPACK_FLG_SKIP_TBLS   = HPACK_FLG_SKIP_STBL | HPACK_FLG_SKIP_DTBL,
};

static inline int
hpack_buflen_to_write_hdr(lstr_t key, lstr_t val, unsigned flags)
{
    /* TODO: better estimate taking into account the flags */
    return HPACK_BUFLEN_STR(key.len) + HPACK_BUFLEN_STR(val.len);
}

/* XXX: Not clear from RFC7541, what should happen at the encoder side, or
 * what we should expect at the decoder side in the following cases:
 *   1. encoder receives & acknowledges settings with a size equal to the
 * current setting value, so there is no real change in the maximum dtbl size.
 * Shall we send a dts update in this case? 
 *   2. decoder receives "un-solicited" dynamic table size updates, i.e,
 * the decoder has not sent a setting frame that changes the maximum dynamic
 * table size.
 */

/** Encode a dtbl max size update to the decoder
 *
 * \return number of bytes written to \p out
 * \note as a side effect, this changes the max size of the dtbl with implied
 * entry evictions if any.
 */
int hpack_encoder_write_dts_update(hpack_enc_dtbl_t *nonnull dtbl,
                                   uint32_t new_sz,
                                   byte out[HPACK_BUFLEN_INT]);

/** Write a single hdr to \p out using the encoding options in \p flags
 *
 * \param key: the key field of the header
 * \param val: the value field of the header
 * \param flags: options to control the encoding. See HPACK_FLG_* constants
 *
 * \note caller must ensure that \p out has enough capacity, e.g, using \ref
 * hpack_buflen_to_write_hdr.
 */
int hpack_encoder_write_hdr(hpack_enc_dtbl_t *nonnull dtbl, lstr_t key,
                            lstr_t val, uint16_t key_id, uint16_t val_id,
                            unsigned flags, byte *out);


int hpack_decoder_read_dts_update_(hpack_dec_dtbl_t *dtbl, pstream_t *in);

/** Read a dtbl size update, if any, from \p in
 *
 * \return 1 if an update is read, 0 if there is not, or -1 on error
 */
static inline int
hpack_decoder_read_dts_update(hpack_dec_dtbl_t *dtbl, pstream_t *in)
{
    if (likely(ps_done(in) || (0xE0u & in->b[0]) != 0x20u)) {
        return 0;
    }
    return hpack_decoder_read_dts_update_(dtbl,in);
}

/* a type to reference an extracted hdr to be decoded */
typedef struct hpack_xhdr_t {
    union {
        pstream_t key;
        int idx;
    };
    pstream_t val;
    unsigned flags;
} hpack_xhdr_t;

/** Extract the next hdr to decode from \p in into \p xhdr
 *
 * \return an estimate of bufflen to decode the header, or -1 on error
 */
int hpack_decoder_extract_hdr(hpack_dec_dtbl_t *dtbl, pstream_t *in,
                              hpack_xhdr_t *xhdr);

/** Decode and write the header line referenced by \p xhdr into \p out
 *
 * \return the number of bytes written, or -1 on error.
 *
 * \note the hdr line written has the format 'KEY(-OF-KEYLEN): VALUE\r\n'. On
 * success, \p keylen is the length of key part, i.e, the offset of ':' in the
 * written hdr line. So, it is up to the caller to ensure that \p out has at
 * least 4 bytes more than the value return by \ref hpack_decoder_extract_hdr.
 */
int hpack_decoder_write_hdr(hpack_dec_dtbl_t *dtbl, hpack_xhdr_t *xhdr,
                            byte *out, int *keylen);

#endif /* IS_LIB_COMMON_NET_HPACK_H */
