/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_ASN1_H
#define IS_LIB_COMMON_ASN1_H

#include <lib-common/arith.h>

#include "asn1/macros.h"
#include "asn1/writer.h"

#define ASN1_TAG_INVALID   0xFFFFFFFF

enum asn1_tag_class {
    ASN1_TAG_CLASS_UNIVERSAL,
    ASN1_TAG_CLASS_APPLICATION,
    ASN1_TAG_CLASS_CONTEXT_SPECIFIC,
    ASN1_TAG_CLASS_PRIVATE
};

#define ASN1_MK_TAG(class, value)  ((ASN1_TAG_CLASS_##class << 6) | (value))

#define ASN1_TAG_CONSTRUCTED(tag) ((1 << 5) | (tag))

/* Tag for constructed values. */
#define ASN1_MK_TAG_C(class, value) \
    ASN1_TAG_CONSTRUCTED(ASN1_MK_TAG(class, value))

/* Tag list on :
 * http://www.obj-sys.com/asn1tutorial/node124.html */
#define ASN1_TAG_BOOLEAN           ASN1_MK_TAG(UNIVERSAL, 1)
#define ASN1_TAG_INTEGER           ASN1_MK_TAG(UNIVERSAL, 2)
#define ASN1_TAG_BIT_STRING        ASN1_MK_TAG(UNIVERSAL, 3)
#define ASN1_TAG_OCTET_STRING      ASN1_MK_TAG(UNIVERSAL, 4)
#define ASN1_TAG_NULL              ASN1_MK_TAG(UNIVERSAL, 5)
#define ASN1_TAG_OBJECT_ID         ASN1_MK_TAG(UNIVERSAL, 6)
#define ASN1_TAG_OBJECT_DECRIPTOR  ASN1_MK_TAG(UNIVERSAL, 7)
#define ASN1_TAG_EXTERNAL          ASN1_MK_TAG(UNIVERSAL, 8)
#define ASN1_TAG_REAL              ASN1_MK_TAG(UNIVERSAL, 9)
#define ASN1_TAG_ENUMERATED        ASN1_MK_TAG(UNIVERSAL, 10)
#define ASN1_TAG_EMBEDDED_PDV      ASN1_MK_TAG(UNIVERSAL, 11)
#define ASN1_TAG_UTF8_STRING       ASN1_MK_TAG(UNIVERSAL, 12)
#define ASN1_TAG_RELATIVE_OID      ASN1_MK_TAG(UNIVERSAL, 13)

#define ASN1_TAG_SEQUENCE          ASN1_MK_TAG(UNIVERSAL, 16)
#define ASN1_TAG_SET               ASN1_MK_TAG(UNIVERSAL, 17)
#define ASN1_TAG_NUMERIC_STRING    ASN1_MK_TAG(UNIVERSAL, 18)
#define ASN1_TAG_PRINTABLE_STRING  ASN1_MK_TAG(UNIVERSAL, 19)
#define ASN1_TAG_TELETEX_STRING    ASN1_MK_TAG(UNIVERSAL, 20)
#define ASN1_TAG_VIDEOTEX_STRING   ASN1_MK_TAG(UNIVERSAL, 21)
#define ASN1_TAG_IA5STRING         ASN1_MK_TAG(UNIVERSAL, 22)
#define ASN1_TAG_UTC_TIME          ASN1_MK_TAG(UNIVERSAL, 23)
#define ASN1_TAG_GENERALIZED_TIME  ASN1_MK_TAG(UNIVERSAL, 24)
#define ASN1_TAG_GRAPHIC_STRING    ASN1_MK_TAG(UNIVERSAL, 25)
#define ASN1_TAG_VISIBLE_STRING    ASN1_MK_TAG(UNIVERSAL, 26)
#define ASN1_TAG_GENERAL_STRING    ASN1_MK_TAG(UNIVERSAL, 27)
#define ASN1_TAG_UNIVERSAL_STRING  ASN1_MK_TAG(UNIVERSAL, 28)
#define ASN1_TAG_CHARACTER_STRING  ASN1_MK_TAG(UNIVERSAL, 29)
#define ASN1_TAG_BMP_STRING        ASN1_MK_TAG(UNIVERSAL, 30)

#define ASN1_TAG_EXTERNAL_C        ASN1_MK_TAG_C(UNIVERSAL, 8)
#define ASN1_TAG_SEQUENCE_C        ASN1_MK_TAG_C(UNIVERSAL, 16)
#define ASN1_TAG_SET_C             ASN1_MK_TAG_C(UNIVERSAL, 17)

int ber_decode_len32(pstream_t *ps, uint32_t *_len);

int ber_decode_int16(pstream_t *ps, int16_t *_val);
int ber_decode_int32(pstream_t *ps, int32_t *_val);
int ber_decode_int64(pstream_t *ps, int64_t *_val);
int ber_decode_uint16(pstream_t *ps, uint16_t *_val);
int ber_decode_uint32(pstream_t *ps, uint32_t *_val);
int ber_decode_uint64(pstream_t *ps, uint64_t *_val);

/* Return number of bytes read, or -1 on error */
int ber_decode_oid(const byte *p, int size, int *oid, int size_oid);

int ber_decode_bit_string_len(pstream_t *ps);

/* {{{ Private functions */

void asn1_alloc_seq_of(void *st, int count, const asn1_field_t *field,
                       mem_pool_t *mp);

/* }}} */

#endif
