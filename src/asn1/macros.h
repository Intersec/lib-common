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

#if !defined(IS_LIB_COMMON_ASN1_H) || defined(IS_LIB_COMMON_ASN1_MACROS_H)
#  error "you must include asn1.h instead"
#else
#define IS_LIB_COMMON_ASN1_MACROS_H

#include <lib-common/container-qvector.h>

#define ASN1_OBJ_TYPE(type)  ASN1_OBJ_TYPE_##type
#define ASN1_OBJ_MODE(mode)  ASN1_OBJ_MODE_##mode

/*{{{ Macros for memory manipulation */

#define GET_PTR(st, spec, typ)  \
        ((typ *)((char *)(st) + (spec->offset)))
#define GET_CONST_PTR(st, typ, off)  \
        ((const typ *)((const char *)(st) + (off)))

/**
 * Gets a const pointer on the data field
 * without having to know whether the data is pointed.
 */
#define GET_DATA_P(st, field, typ) \
    (field->pointed                                        \
     ? *GET_CONST_PTR(st, typ *, field->offset)            \
     :  GET_CONST_PTR(st, typ, field->offset))

/**
 * Gets the const value of the data field
 * without having to know whether the data is pointed.
 */
#define GET_DATA(st, field, typ) \
    (field->pointed                                        \
     ? **GET_CONST_PTR(st, typ *, field->offset)           \
     :  *GET_CONST_PTR(st, typ, field->offset))

#define GET_VECTOR_DATA(st, field) \
    GET_CONST_PTR(st, ASN1_VECTOR_OF(void), field->offset)->data
#define GET_VECTOR_LEN(st, field) \
    GET_CONST_PTR(st, ASN1_VECTOR_OF(void), field->offset)->len

/*}}} */
/*{{{ Macros for description function getters */

#define ASN1_GET_DESC(st_pfx)  asn1_##st_pfx##_desc()

#define ASN1_DESC(st_pfx) \
    const asn1_desc_t *asn1_##st_pfx##_desc(void)

#define ASN1_ST_DESC_VAR(st_pfx) st_pfx##_DESC

#define ASN1_TYPEDEF(src_pfx, dst_pfx) \
    typedef src_pfx##_t dst_pfx##_t;                                         \
    static inline ASN1_DESC(dst_pfx)                                         \
    {                                                                        \
        return ASN1_GET_DESC(src_pfx);                                       \
    }


/* Registers ASN.1 sequences implicitly. */
#define _ASN1_DESC_BEGIN(desc, st_pfx) \
    ASN1_DESC(st_pfx)                                                        \
    {                                                                        \
        static __thread asn1_desc_t *desc;                                   \
                                                                             \
        if (unlikely(!desc)) {                                               \
            desc = asn1_desc_new();                                          \
            desc->size = sizeof(st_pfx##_t);

#define ASN1_DESC_BEGIN(st_pfx)                                              \
    _ASN1_DESC_BEGIN(ASN1_ST_DESC_VAR(st_pfx), st_pfx)

#define _ASN1_DESC_END(desc) \
            if (desc->is_seq_of) {                                           \
                assert (desc->fields.len == 1);                              \
                assert (desc->fields.tab[0].mode == ASN1_OBJ_MODE(SEQ_OF));  \
            }                                                                \
                                                                             \
            assert (desc->type == ASN1_CSTD_TYPE_SEQUENCE);                  \
            qv_append(&asn1_descs_g.descs, desc);                            \
        }                                                                    \
                                                                             \
        return desc;                                                         \
    }
#define ASN1_DESC_END(st_pfx) _ASN1_DESC_END(ASN1_ST_DESC_VAR(st_pfx))

#define ASN1_SEQUENCE_DESC_BEGIN(st_pfx) \
    ASN1_DESC_BEGIN(st_pfx);                                                 \
    ASN1_ST_DESC_VAR(st_pfx)->type = ASN1_CSTD_TYPE_SEQUENCE;

#define _ASN1_SEQUENCE_DESC_END(desc) \
            assert (desc->type == ASN1_CSTD_TYPE_SEQUENCE);                  \
            qv_append(&asn1_descs_g.descs, desc);                            \
        }                                                                    \
                                                                             \
        return desc;                                                         \
    }
#define ASN1_SEQUENCE_DESC_END(st_pfx) \
    _ASN1_SEQUENCE_DESC_END(ASN1_ST_DESC_VAR(st_pfx))

#define __ASN1_CHOICE_DESC_BEGIN(_desc, st_pfx) \
    ASN1_DESC(st_pfx)                                                        \
    {                                                                        \
        static __thread asn1_desc_t *_desc;                                  \
                                                                             \
        if (unlikely(!_desc)) {                                              \
            asn1_choice_desc_t *__choice_desc;                               \
                                                                             \
            __choice_desc = asn1_choice_desc_new();                          \
            _desc = &__choice_desc->desc;                                    \
            _desc->type = ASN1_CSTD_TYPE_CHOICE;                             \
            _desc->size = sizeof(st_pfx##_t);

#define ASN1_CHOICE_DESC_BEGIN(st_pfx, enum_pfx, enum_field) \
    __ASN1_CHOICE_DESC_BEGIN(ASN1_ST_DESC_VAR(st_pfx), st_pfx);              \
            asn1_reg_enum(st_pfx, enum_pfx, enum_field, ASN1_TAG_INVALID)

/* XXX Choices declared using this macro must have incremental tagging
 *     starting with value 1
 */
#define __ASN1_IOP_CHOICE_DESC_BEGIN(st_pfx) \
    __ASN1_CHOICE_DESC_BEGIN(ASN1_ST_DESC_VAR(st_pfx), st_pfx);              \
            asn1_reg_scalar(st_pfx, iop_tag, ASN1_TAG_INVALID)

#define __ASN1_CHOICE_DESC_END(_desc) \
            assert (_desc->type == ASN1_CSTD_TYPE_CHOICE);                   \
            asn1_int_info_set_min(&_desc->choice_info, 0);                   \
            /* - 2 -> index + first choice */                                \
            assert (_desc->fields.len >= 2);                                 \
            asn1_int_info_set_max(&_desc->choice_info,                       \
                                  (_desc->is_extended ?                      \
                                   _desc->ext_pos :                          \
                                   _desc->fields.len) - 2);                  \
            asn1_int_info_update(&_desc->choice_info, false);                \
            asn1_build_choice_table((asn1_choice_desc_t *)_desc);            \
            qv_append(&asn1_descs_g.choice_descs, __choice_desc);            \
        }                                                                    \
                                                                             \
        return _desc;                                                        \
    }
#define ASN1_CHOICE_DESC_END(st_pfx) \
    __ASN1_CHOICE_DESC_END(ASN1_ST_DESC_VAR(st_pfx))

#define asn1_pack_size(pfx, v, stack) \
    ({                                                                       \
        if (!__builtin_types_compatible_p(typeof(v), const pfx##_t *)        \
        &&  !__builtin_types_compatible_p(typeof(v), pfx##_t *))             \
        {                                                                    \
            __error__("incompatible input type for size packing");           \
        }                                                                    \
        asn1_pack_size_(v, ASN1_GET_DESC(pfx), stack);                       \
    })

#define asn1_pack(pfx, dst, v, stack) \
    ({                                                                       \
        if (!__builtin_types_compatible_p(typeof(v), const pfx##_t *)        \
        &&  !__builtin_types_compatible_p(typeof(v), pfx##_t *))             \
        {                                                                    \
            __error__("incompatible input type for packing");                \
        }                                                                    \
        asn1_pack_(dst, v, ASN1_GET_DESC(pfx), stack);                       \
    })

/* TODO stop messing with param order */
#define asn1_unpack(pfx, ps, mem_pool, st, cpy) \
    ({                                                                       \
        if (!__builtin_types_compatible_p(typeof(st), pfx##_t *)) {          \
            __error__("incompatible output type for unpacking");             \
        }                                                                    \
        asn1_unpack_(ps, ASN1_GET_DESC(pfx), mem_pool, st, cpy);             \
    })

#define ASN1_EXPLICIT(fld, val) \
{                                                                            \
    .fld = val,                                                              \
}

#define ASN1_UNION_CHOICE(typ_fld, type, ch_fld, choice) \
{                                                                            \
    .typ_fld = type,                                                         \
    {                                                                        \
        .chc_fld = choice,                                                   \
    }                                                                        \
}

/*}}}*/
/*{{{ Common registering macros */
/*****************/
/* COMMON MACROS */
/*****************/

#define ASN1_IS_FIELD_TYPE(ctype_t, field, st_pfx) \
    __builtin_types_compatible_p(fieldtypeof(st_pfx##_t, field), ctype_t)

#define ASN1_COMMON_FIELDS(ctype_t, st_pfx, field, tg, typ, mod, ptd) \
    .name      = #field,                                                     \
    .oc_t_name = #ctype_t,                                                   \
    .offset    = offsetof(st_pfx##_t, field),                                \
    .type      = ASN1_OBJ_TYPE(typ),                                         \
    .tag       = tg,                                                         \
    .tag_len   = 1,                                                          \
    .mode      = ASN1_OBJ_MODE(mod),                                         \
    .size      = sizeof(ctype_t),                                            \
    .pointed   = ptd

/*}}}*/
/*{{{ Scalar types registering macros */
/***************************/
/* MACROS FOR SCALAR TYPES */
/***************************/

#define ASN1_REG_SCALAR_WITH_MODE(st_pfx, ctype_t, field, tag, mode) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(ctype_t, st_pfx, field, tag, ctype_t, mode,   \
                               false),                                       \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

#define ASN1_REG_SCALAR(st_pfx, ctype_t, field, tag) \
    ASN1_REG_SCALAR_WITH_MODE(st_pfx, ctype_t, field, tag, MANDATORY)
#define ASN1_REG_OPT_SCALAR(st_pfx, ctype_t, field, tag) \
    ASN1_REG_SCALAR_WITH_MODE(st_pfx, ctype_t, field, tag, OPTIONAL)
#define ASN1_REG_SEQ_OF_SCALAR(st_pfx, ctype_t, field, tag) \
    ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                              \
    ASN1_REG_SCALAR_WITH_MODE(st_pfx, ctype_t, field, tag, SEQ_OF)

#define asn1_reg_scalar(st_pfx, field, tag) \
    do {                                                                     \
        if (__builtin_types_compatible_p(fieldtypeof(st_pfx##_t, field),     \
                                         bool)) {                            \
            ASN1_REG_SCALAR(st_pfx, bool, field, tag);                       \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(int8_t, field, st_pfx)) {                     \
            ASN1_REG_SCALAR(st_pfx, int8_t, field, tag);                     \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(uint8_t, field, st_pfx)) {                    \
            ASN1_REG_SCALAR(st_pfx, uint8_t, field, tag);                    \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(int16_t, field, st_pfx)) {                    \
            ASN1_REG_SCALAR(st_pfx, int16_t, field, tag);                    \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(uint16_t, field, st_pfx)) {                   \
            ASN1_REG_SCALAR(st_pfx, uint16_t, field, tag);                   \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(int32_t, field, st_pfx)) {                    \
            ASN1_REG_SCALAR(st_pfx, int32_t, field, tag);                    \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(uint32_t, field, st_pfx)) {                   \
            ASN1_REG_SCALAR(st_pfx, uint32_t, field, tag);                   \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(int64_t, field, st_pfx)) {                    \
            ASN1_REG_SCALAR(st_pfx, int64_t, field, tag);                    \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(uint64_t, field, st_pfx)) {                   \
            ASN1_REG_SCALAR(st_pfx, uint64_t, field, tag);                   \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_bool_t, field, st_pfx)) {                 \
            ASN1_REG_OPT_SCALAR(st_pfx, bool, field, tag);                   \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_i8_t, field, st_pfx)) {                   \
            ASN1_REG_OPT_SCALAR(st_pfx, int8_t, field, tag);                 \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_u8_t, field, st_pfx)) {                   \
            ASN1_REG_OPT_SCALAR(st_pfx, uint8_t, field, tag);                \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_i16_t, field, st_pfx)) {                  \
            ASN1_REG_OPT_SCALAR(st_pfx, int16_t, field, tag);                \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_u16_t, field, st_pfx)) {                  \
            ASN1_REG_OPT_SCALAR(st_pfx, uint16_t, field, tag);               \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_i32_t, field, st_pfx)) {                  \
            ASN1_REG_OPT_SCALAR(st_pfx, int32_t, field, tag);                \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_u32_t, field, st_pfx)) {                  \
            ASN1_REG_OPT_SCALAR(st_pfx, uint32_t, field, tag);               \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_i64_t, field, st_pfx)) {                  \
            ASN1_REG_OPT_SCALAR(st_pfx, int64_t, field, tag);                \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(opt_u64_t, field, st_pfx)) {                  \
            ASN1_REG_OPT_SCALAR(st_pfx, uint64_t, field, tag);               \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(bool), field, st_pfx)) {     \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, bool, field, tag);                \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(int8), field, st_pfx)) {     \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, int8_t, field, tag);              \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(uint8), field, st_pfx)) {    \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, uint8_t, field, tag);             \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(int16), field, st_pfx)) {    \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, int16_t, field, tag);             \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(uint16), field, st_pfx)) {   \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, uint16_t, field, tag);            \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(int32), field, st_pfx)) {    \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, int32_t, field, tag);             \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(uint32), field, st_pfx)) {   \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, uint32_t, field, tag);            \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(int64), field, st_pfx)) {    \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, int64_t, field, tag);             \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(uint64), field, st_pfx)) {   \
            ASN1_REG_SEQ_OF_SCALAR(st_pfx, uint64_t, field, tag);            \
            break;                                                           \
        }                                                                    \
        __error__(#st_pfx"_t->"#field                                        \
                  " type is not a supported scalar type");                   \
    } while (0)

#define ASN1_REG_ENUM(st_pfx, enum_sfx, field, tag, mode) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(int, st_pfx, field, tag, enum, mode, false)   \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

#define asn1_reg_enum(st_pfx, enum_sfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(enum enum_sfx, field, st_pfx)) {              \
            ASN1_REG_ENUM(st_pfx, enum_sfx, field, tag, MANDATORY);          \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_opt_enum(st_pfx, enum_sfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(ASN1_OPT_TYPE(enum_sfx), field, st_pfx)) {    \
            ASN1_REG_ENUM(st_pfx, enum_sfx, field, tag, OPTIONAL);           \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_seq_of_enum(st_pfx, enum_sfx, field, tag) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(enum_sfx), field, st_pfx)) { \
            ASN1_REG_ENUM(st_pfx, enum_sfx, field, tag, SEQ_OF);             \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#ifndef NDEBUG
#define asn1_reg_null(st_pfx, field_name, tg) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            .name = field_name,                                              \
            .tag = tg,                                                       \
            .tag_len = 1,                                                    \
            .mode = ASN1_OBJ_MODE(MANDATORY),                                \
            .type = ASN1_OBJ_TYPE(NULL),                                     \
            .offset = 0                                                      \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)
#else
#define asn1_reg_null(st_pfx, field_name, tg) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            .tag = tg,                                                       \
            .tag_len = 1,                                                    \
            .mode = ASN1_OBJ_MODE(MANDATORY),                                \
            .type = ASN1_OBJ_TYPE(NULL),                                     \
            .offset = 0                                                      \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)
#endif

#define asn1_reg_opt_null(st_pfx, bool_field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(bool, bool_field, st_pfx)) {                  \
            asn1_field_t tmp = (asn1_field_t){                               \
                ASN1_COMMON_FIELDS(bool, st_pfx, bool_field, tag, OPT_NULL,  \
                                   OPTIONAL, false)                          \
            };                                                               \
            asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                  \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while(0)

/*}}}*/
/*{{{ String types registering macros */
/***************************/
/* MACROS FOR STRING TYPES */
/***************************/

#define asn1_reg_string(st_pfx, field, tag) \
    ASN1_REG_MAND_OPT_STRING(st_pfx, field, tag, MANDATORY)

#define asn1_reg_opt_string(st_pfx, field, tag) \
    ASN1_REG_MAND_OPT_STRING(st_pfx, field, tag, OPTIONAL)

#define asn1_reg_seq_of_string(st_pfx, field, tag) \
    ASN1_REG_SEQ_OF_STRING(st_pfx, field, tag)

#define ASN1_REG_MAND_OPT_STRING(st_pfx, field, tag, mode) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(lstr_t, field, st_pfx)) {                     \
            ASN1_REG_STRING(st_pfx, lstr_t, field, tag, mode);               \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(asn1_bit_string_t, field, st_pfx)) {          \
            ASN1_REG_STRING(st_pfx, asn1_bit_string_t, field, tag, mode);    \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define ASN1_REG_SEQ_OF_STRING(st_pfx, field, tag) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(lstr), field, st_pfx)) {     \
            ASN1_REG_STRING(st_pfx, lstr_t, field, tag, SEQ_OF);             \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(bit_string), field, st_pfx)) \
        {                                                                    \
            ASN1_REG_STRING(st_pfx, asn1_bit_string_t, field, tag, SEQ_OF);  \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define ASN1_REG_STRING(st_pfx, ctype_t, field, tag, mode) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(ctype_t, st_pfx, field, tag, ctype_t, mode,   \
                               false),                                       \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}}*/
/*{{{ Open Type registering macro */

#define asn1_reg_open_type(st_pfx, field) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(lstr_t, field, st_pfx)) {                     \
            ASN1_REG_OPEN_TYPE(st_pfx, lstr_t, MANDATORY, field);            \
        }                                                                    \
    } while (0)

#define asn1_reg_opt_open_type(st_pfx, field) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(lstr_t, field, st_pfx)) {                     \
            ASN1_REG_OPEN_TYPE(st_pfx, lstr_t, OPTIONAL, field);             \
        }                                                                    \
    } while (0)

#define ASN1_REG_OPEN_TYPE(st_pfx, ctype_t, mode, field) \
    do {                                                                     \
        asn1_field_t tmp = {                                                 \
            ASN1_COMMON_FIELDS(ctype_t, st_pfx, field, 0, OPEN_TYPE, mode,   \
                               false),                                       \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}} */
/*{{{ Opaque type registering macros */
/***************************/
/* MACROS FOR OPAQUE TYPES */
/***************************/

#define asn1_reg_opaque(st_pfx, ctype, pfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(ctype, field, st_pfx)) {                      \
            ASN1_REG_OPAQUE(st_pfx, ctype, pfx, field, tag, MANDATORY,       \
                            false);                                          \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ctype *, field, st_pfx)                       \
         || ASN1_IS_FIELD_TYPE(const ctype *, field, st_pfx)) {              \
            ASN1_REG_OPAQUE(st_pfx, ctype, pfx, field, tag, MANDATORY,       \
                            true);                                           \
            break;                                                           \
        }                                                                    \
        __error__(#st_pfx"_t ->"#field" type does not match "#ctype);        \
    } while (0)


#define asn1_reg_opt_opaque(st_pfx, ctype, pfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(ctype *, field, st_pfx)                       \
         || ASN1_IS_FIELD_TYPE(const ctype *, field, st_pfx)) {              \
            ASN1_REG_OPAQUE(st_pfx, ctype, pfx, field, tag, OPTIONAL, true); \
            break;                                                           \
        }                                                                    \
        __error__(#st_pfx"_t ->"#field" type is not "#ctype" *");            \
    } while (0)

#define asn1_reg_seq_of_opaque(st_pfx, ctype, pfx, field, tag) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(pfx)), field, st_pfx) {      \
            ASN1_REG_OPAQUE(st_pfx, ctype, pfx, field, tag, SEQ_OF, false);  \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_ARRAY_TYPE(pfx)), field, st_pfx) {       \
            ASN1_REG_OPAQUE(st_pfx, ctype, pfx, field, tag, SEQ_OF, true);   \
            break;                                                           \
        }                                                                    \
        __error__(#st_pfx"_t ->"#field" type is not a correct seq_of "       \
                  ctype_t" type");                                           \
    } while (0)

#define ASN1_REG_OPAQUE(st_pfx, ctype_t, pfx, field, tag, mode, pointed) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(ctype_t, st_pfx, field, tag, OPAQUE, mode,    \
                               pointed),                                     \
            .u = {                                                           \
                .opaque = {                                                  \
                    .pack_size = &asn1_##pfx##_size,                         \
                    .pack      = &asn1_pack_##pfx,                           \
                    .unpack    = &asn1_unpack_##pfx,                         \
                },                                                           \
            },                                                               \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}}*/
/*{{{ Sequence registering macros */
/****************************/
/* MACROS FOR SEQUENCE TYPE */
/****************************/

#define asn1_reg_sequence(st_pfx, pfx, field, tag) \
    do {                                                                     \
        if (!(tag & ASN1_TAG_CONSTRUCTED(0))) {                              \
            __error__("sequence tags must be constructed");                  \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(pfx##_t, field, st_pfx)) {                    \
            ASN1_REG_SEQUENCE(st_pfx, pfx, field, tag, MANDATORY, false);    \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(pfx##_t *, field, st_pfx)                     \
        ||  ASN1_IS_FIELD_TYPE(const pfx##_t *, field, st_pfx))              \
        {                                                                    \
            ASN1_REG_SEQUENCE(st_pfx, pfx, field, tag, MANDATORY, true);     \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_opt_sequence(st_pfx, pfx, field, tag) \
    do {                                                                     \
        if (!(tag & ASN1_TAG_CONSTRUCTED(0))) {                              \
            __error__("sequence tags must be constructed");                  \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(pfx##_t *, field, st_pfx)                     \
        ||  ASN1_IS_FIELD_TYPE(const pfx##_t *, field, st_pfx)) {            \
            ASN1_REG_SEQUENCE(st_pfx, pfx, field, tag, OPTIONAL, true);      \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_seq_of_sequence(st_pfx, pfx, field, tag) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(pfx), field, st_pfx)) {      \
            ASN1_REG_SEQUENCE(st_pfx, pfx, field, tag, SEQ_OF, false);       \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_ARRAY_TYPE(pfx), field, st_pfx)) {       \
            ASN1_REG_SEQUENCE(st_pfx, pfx, field, tag, SEQ_OF, true);        \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define ASN1_REG_SEQUENCE(st_pfx, pfx, field, tag, mode, pointed) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(pfx##_t, st_pfx, field, tag, SEQUENCE, mode,  \
                               pointed),                                     \
            .u = { .comp = ASN1_GET_DESC(pfx), },                            \
        };                                                                   \
        if (tmp.u.comp->type != ASN1_CSTD_TYPE_SEQUENCE) {                   \
            e_panic("incorrect sub-type for "#st_pfx"_t:"#pfx":"#field       \
                    ", expected SEQUENCE");                                  \
        }                                                                    \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}}*/
/*{{{ Choice registering macros */
/**************************/
/* MACROS FOR CHOICE TYPE */
/**************************/

#define asn1_reg_choice(st_pfx, pfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(pfx##_t, field, st_pfx)) {                    \
            ASN1_REG_CHOICE(st_pfx, pfx, field, tag, MANDATORY, false);      \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(pfx##_t *, field, st_pfx)                     \
        ||  ASN1_IS_FIELD_TYPE(const pfx##_t *, field, st_pfx)) {            \
            ASN1_REG_CHOICE(st_pfx, pfx, field, tag, MANDATORY, true);       \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_opt_choice(st_pfx, pfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(pfx##_t *, field, st_pfx)                     \
        ||  ASN1_IS_FIELD_TYPE(const pfx##_t *, field, st_pfx)) {            \
            ASN1_REG_CHOICE(st_pfx, pfx, field, tag, OPTIONAL, true);        \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_seq_of_choice(st_pfx, pfx, field, tag) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(pfx), field, st_pfx)) {      \
            ASN1_REG_CHOICE(st_pfx, pfx, field, tag, SEQ_OF, false);         \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_ARRAY_TYPE(pfx), field, st_pfx)) {       \
            ASN1_REG_CHOICE(st_pfx, pfx, field, tag, SEQ_OF, true);          \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define ASN1_REG_CHOICE(st_pfx, pfx, field, tag, mode, pointed) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(pfx##_t, st_pfx, field, tag, CHOICE, mode,    \
                               pointed),                                     \
            .u = { .comp = ASN1_GET_DESC(pfx), },                            \
        };                                                                   \
        if (tmp.u.comp->type != ASN1_CSTD_TYPE_CHOICE) {                     \
            e_panic("incorrect sub-type for "#st_pfx"_t:"#pfx":"#field       \
                    ", expected CHOICE");                                    \
        }                                                                    \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}}*/
/*{{{ Untagged choice registering macros */
/***********************************/
/* MACROS FOR UNTAGGED CHOICE TYPE */
/***********************************/

#define asn1_reg_untagged_choice(st_pfx, pfx, field) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(pfx##_t, field, st_pfx)) {                    \
            ASN1_REG_UNTAGGED_CHOICE(st_pfx, pfx, field, MANDATORY, false);  \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(pfx##_t *, field, st_pfx)                     \
        ||  ASN1_IS_FIELD_TYPE(const pfx##_t *, field, st_pfx)) {            \
            ASN1_REG_UNTAGGED_CHOICE(st_pfx, pfx, field, MANDATORY, true);   \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_opt_untagged_choice(st_pfx, pfx, field) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(pfx##_t *, field, st_pfx)                     \
        ||  ASN1_IS_FIELD_TYPE(const pfx##_t *, field, st_pfx)) {            \
            ASN1_REG_UNTAGGED_CHOICE(st_pfx, pfx, field, OPTIONAL, true);    \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_seq_of_untagged_choice(st_pfx, pfx, field) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(ASN1_VECTOR_TYPE(pfx), field, st_pfx)) {      \
            ASN1_REG_UNTAGGED_CHOICE(st_pfx, pfx, field, SEQ_OF, false);     \
            break;                                                           \
        }                                                                    \
        if (ASN1_IS_FIELD_TYPE(ASN1_ARRAY_TYPE(pfx), field, st_pfx)) {       \
            ASN1_REG_UNTAGGED_CHOICE(st_pfx, pfx, field, SEQ_OF, true);      \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define ASN1_REG_UNTAGGED_CHOICE(st_pfx, pfx, field, mode, pointed) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(pfx##_t, st_pfx, field, ASN1_TAG_INVALID,     \
                               UNTAGGED_CHOICE, mode, pointed),              \
            .u = { .comp = ASN1_GET_DESC(pfx), },                            \
        };                                                                   \
        if (tmp.u.comp->type != ASN1_CSTD_TYPE_CHOICE) {                     \
            e_panic("incorrect sub-type for "#st_pfx"_t:"#pfx":"#field       \
                    ", expected CHOICE");                                    \
        }                                                                    \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}}*/
/*{{{ External fields registering macros */
/*******************************/
/* MACROS FOR EXT TYPE */
/*******************************/

#define asn1_reg_ext(st_pfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(asn1_ext_t, field, st_pfx)) {                 \
            ASN1_REG_EXT(st_pfx, field, tag, MANDATORY);                     \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_opt_ext(st_pfx, field, tag) \
    do {                                                                     \
        if (ASN1_IS_FIELD_TYPE(asn1_ext_t, field, st_pfx)) {                 \
            ASN1_REG_EXT(st_pfx, field, tag, OPTIONAL);                      \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define asn1_reg_seq_of_ext(st_pfx, field, tag) \
    do {                                                                     \
        ASN1_ST_DESC_VAR(st_pfx)->is_seq_of = true;                          \
                                                                             \
        if (ASN1_IS_FIELD_TYPE(asn1_ext_vector_t, field, st_pfx)) {          \
            ASN1_REG_EXT(st_pfx, field, tag, SEQ_OF);                        \
            break;                                                           \
        }                                                                    \
        __error__("incorrect field type regarding desc");                    \
    } while (0)

#define ASN1_REG_EXT(st_pfx, field, tag, mode) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            ASN1_COMMON_FIELDS(asn1_ext_t, st_pfx, field, tag,               \
                               asn1_ext_t, mode, false),                     \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)

/*}}}*/
/*{{{ Field skipping macro */
/**********************/
/* MACRO FOR TLV SKIP */
/**********************/

#ifndef NDEBUG
#define asn1_reg_skip(st_pfx, field_name, tg) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            .name = field_name,                                              \
            .tag = tg,                                                       \
            .tag_len = 1,                                                    \
            .mode = ASN1_OBJ_MODE(OPTIONAL),                                 \
            .type = ASN1_OBJ_TYPE(SKIP),                                     \
            .offset = 0                                                      \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)
#else
#define asn1_reg_skip(st_pfx, field_name, tg) \
    do {                                                                     \
        asn1_field_t tmp = (asn1_field_t){                                   \
            .tag = tg,                                                       \
            .tag_len = 1,                                                    \
            .mode = ASN1_OBJ_MODE(OPTIONAL),                                 \
            .type = ASN1_OBJ_TYPE(SKIP),                                     \
            .offset = 0                                                      \
        };                                                                   \
        asn1_reg_field(ASN1_ST_DESC_VAR(st_pfx), &tmp);                      \
    } while (0)
#endif

/*}}}*/
#endif /* IS_LIB_COMMON_ASN1_WRITER_MACROS_H */

