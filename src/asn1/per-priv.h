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

#ifndef IS_LIB_INET_ASN1_PER_PRIV_H
#define IS_LIB_INET_ASN1_PER_PRIV_H

#include <lib-common/asn1-per.h>
#include <lib-common/bit-buf.h>
#include <lib-common/bit-stream.h>

/* This header regroups functions that are exposed only for unit testing. */

int aper_encode_enum(bb_t *bb, int32_t val, const asn1_enum_info_t *e);
int aper_decode_enum(bit_stream_t *bs, const asn1_enum_info_t *e,
                     int32_t *val);

int aper_encode_number(bb_t *bb, int64_t n, const asn1_int_info_t *info,
                       bool is_signed);
int aper_decode_number(bit_stream_t *nonnull bs,
                       const asn1_int_info_t *nonnull info, bool is_signed,
                       int64_t *nonnull n);

void aper_write_len(bb_t *bb, size_t l, size_t l_min, size_t l_max,
                    bool *nullable need_fragmentation);
int aper_read_len(bit_stream_t *bs, size_t l_min, size_t l_max, size_t *l,
                  bool *nullable is_fragmented);

void aper_write_nsnnwn(bb_t *bb, size_t n);
int aper_read_nsnnwn(bit_stream_t *bs, size_t *n);

int aper_encode_bstring(bb_t *bb, const bit_stream_t *bs,
                        const asn1_cnt_info_t *info);
int aper_decode_bstring(bit_stream_t *bs, const asn1_cnt_info_t *info,
                        bb_t *bit_string);

void aper_write_u16_m(bb_t *bb, uint16_t u16, uint16_t blen, uint16_t d_max);
int aper_read_u16_m(bit_stream_t *bs, size_t blen, uint16_t d_max,
                    uint16_t *u16);

int aper_encode_octet_string(bb_t *bb, lstr_t os,
                             const asn1_cnt_info_t *info);
int t_aper_decode_octet_string(bit_stream_t *bs, const asn1_cnt_info_t *info,
                               bool copy, lstr_t *os);

void sb_add_asn1_len_constraints(sb_t *sb, const asn1_cnt_info_t *info);

#endif /* IS_LIB_INET_ASN1_PER_PRIV_H */
