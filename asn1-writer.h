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

#if !defined(IS_LIB_COMMON_ASN1_H) || defined(IS_LIB_COMMON_ASN1_WRITER_H)
#  error "you must include asn1.h instead"
#else
#define IS_LIB_COMMON_ASN1_WRITER_H

#include "container-qvector.h"
#include "asn1-helpers.in.c"

/* ASN1 writing API
 * Need an example ? Please read tst-asn1-writer.[hc] .
 */

struct asn1_desc_t;

typedef struct asn1_ext_t {
    /* Packing */
    const void                *data;      /* Data.      */
    const struct asn1_desc_t  *desc;      /* Meta-data. */

    /* Unpacking */
    bool                       has_value;  /* For OPTIONAL ext fields only */
    pstream_t                  value;      /* ASN.1 frame */
} asn1_ext_t;

/* Optional scalar types. */
/* XXX Deprecated. Please use opt_XXX_t types and OPT_XXX macros */
#define ASN1_OPT_OF(...)  OPT_OF(__VA_ARGS__)
typedef opt_bool_t asn1_opt_bool_t;
typedef opt_i8_t   asn1_opt_int8_t;
typedef opt_u8_t   asn1_opt_uint8_t;
typedef opt_i16_t  asn1_opt_int16_t;
typedef opt_u16_t  asn1_opt_uint16_t;
typedef opt_i32_t  asn1_opt_int32_t;
typedef opt_u32_t  asn1_opt_uint32_t;
typedef opt_i64_t  asn1_opt_int64_t;
typedef opt_u64_t  asn1_opt_uint64_t;
#define ASN1_OPT_SET(pfx, val)  \
    (asn1_opt_##pfx##_t){ .v = (val), .has_field = true }
#define ASN1_OPT_TYPE(pfx)      asn1_opt_##pfx##_t
#define ASN1_OPT_CLEAR(pfx)     (ASN1_OPT_TYPE(pfx)){ .has_field = false }

typedef struct {
    const uint8_t *data;
    int bit_len;
} asn1_bit_string_t;

static ALWAYS_INLINE size_t asn1_bit_string_size(const asn1_bit_string_t *bs)
{
    return bs->bit_len / 8 + (bs->bit_len % 8 ? 1 : 0) + 1;
}

/**
 * \brief Converts a C bit field to an ASN.1 bit string
 *
 * In ASN.1:
 *
 * BoolVec ::= BIT STRING { b1(0), b2(1), b3(2), b4(3), b5(4), b6(5) }
 *             (SIZE(2..6));
 *
 * In C:
 *
 * enum bool_vec_masks {
 *     B1 = (1 << 0),
 *     B2 = (1 << 1),
 *     B3 = (1 << 2),
 *     B4 = (1 << 3),
 *     B5 = (1 << 4),
 *     B6 = (1 << 5),
 * };
 * #define BOOL_VEC_MASKS_MIN_LEN  2
 *
 * void foo(void) {
 *     asn1_bit_string_t bs;
 *
 *     bs = t_asn1_bstring_from_bf64(B2 | B4, BOOL_VEC_MASKS_MIN_LEN);
 *     // Do things
 * }
 *
 * \param[in] bit_field    Bit field value
 * \param[in] min_bit_len  Minimum bit string length, given in ASN.1
 *                         specification.
 *                         Example: (SIZE(2..16)) -> min_bit_len = 2
 * \return ASN.1 bit string
 */
asn1_bit_string_t
t_asn1_bstring_from_bf64(uint64_t bit_field, int min_bit_len);

#define ASN1_VECTOR_OF(ctype_t)       struct { ctype_t *data; int len; }

#define ASN1_VECTOR_TYPE(sfx)         asn1_##sfx##_vector_t
#define ASN1_DEF_VECTOR(sfx, type_t) \
    typedef ASN1_VECTOR_OF(type_t) ASN1_VECTOR_TYPE(sfx)
typedef ASN1_VECTOR_OF(bool) asn1_bool_vector_t;
ASN1_DEF_VECTOR(int8, const int8_t);
ASN1_DEF_VECTOR(uint8, const uint8_t);
ASN1_DEF_VECTOR(int16, const int16_t);
ASN1_DEF_VECTOR(uint16, const uint16_t);
ASN1_DEF_VECTOR(int32, const int32_t);
ASN1_DEF_VECTOR(uint32, const uint32_t);
ASN1_DEF_VECTOR(int64, const int64_t);
ASN1_DEF_VECTOR(uint64, const uint64_t);
ASN1_DEF_VECTOR(lstr, const lstr_t);
ASN1_DEF_VECTOR(bit_string, const asn1_bit_string_t);
ASN1_DEF_VECTOR(ext, const asn1_ext_t);
ASN1_DEF_VECTOR(void, void);
#define ASN1_VECTOR(ctype_t, dt, ln)  (ctype_t){ .data = dt, .len = ln }
#define ASN1_SVECTOR(ctype_t, dt) \
    ASN1_VECTOR(ctype_t, dt, sizeof(dt) / sizeof(ctype_t))

#define ASN1_BIT_STRING(dt, bln) \
    (asn1_bit_string_t){ .data = dt, .bit_len = bln }
#define ASN1_BIT_STRING_NULL        ASN1_BIT_STRING(NULL, 0)

#define ASN1_ARRAY_OF(ctype_t)        ASN1_VECTOR_OF(ctype_t *)
#define ASN1_ARRAY_TYPE(sfx)          asn1_##sfx##_array_t
#define ASN1_DEF_ARRAY(sfx, type_t) \
    typedef ASN1_ARRAY_OF(type_t) ASN1_ARRAY_TYPE(sfx)
ASN1_DEF_ARRAY(void, void);

#define ASN1_EXT(pfx, ptr) \
    ({                                                                           \
        __unused__                                                               \
        const pfx##_t *_tmp = ptr;                                               \
        (asn1_ext_t){                                                            \
            .data = ptr,                                                         \
            .desc = ASN1_GET_DESC(pfx)                                           \
        };                                                                       \
    })

#define ASN1_EXT_CLEAR()  (asn1_ext_t){ .data = NULL }

#define ASN1_EXPLICIT(fld, val) \
{                                             \
    .fld = val,                               \
}

#define ASN1_CHOICE(typ_fld, type, ...) \
{                                             \
    .typ_fld = type,                          \
    {                                         \
        __VA_ARGS__                           \
    }                                         \
}

/** \enum enum obj_type
  * \brief Built-in types.
  */
enum obj_type {
    /* Scalar types */
    ASN1_OBJ_TYPE(bool),
    ASN1_OBJ_TYPE(_Bool) = ASN1_OBJ_TYPE(bool), /* bool is defined by macro */
    ASN1_OBJ_TYPE(int8_t),
    ASN1_OBJ_TYPE(uint8_t),
    ASN1_OBJ_TYPE(int16_t),
    ASN1_OBJ_TYPE(uint16_t),
    ASN1_OBJ_TYPE(int32_t),
    ASN1_OBJ_TYPE(uint32_t),
    ASN1_OBJ_TYPE(int64_t),
    ASN1_OBJ_TYPE(uint64_t),
    ASN1_OBJ_TYPE(enum),
    ASN1_OBJ_TYPE(NULL),
    ASN1_OBJ_TYPE(OPT_NULL),

    /* String types */
    ASN1_OBJ_TYPE(lstr_t),
    ASN1_OBJ_TYPE(OPEN_TYPE),
    ASN1_OBJ_TYPE(asn1_bit_string_t),

    /* Opaque -- External */
    ASN1_OBJ_TYPE(OPAQUE),
    ASN1_OBJ_TYPE(asn1_ext_t),

    /* Sub-struct types */
    ASN1_OBJ_TYPE(SEQUENCE),
    ASN1_OBJ_TYPE(CHOICE),
    ASN1_OBJ_TYPE(UNTAGGED_CHOICE),

    /* Skip */
    ASN1_OBJ_TYPE(SKIP),
};

/** \enum asn1_object_mode
  */
enum obj_mode {
    ASN1_OBJ_MODE(MANDATORY),
    ASN1_OBJ_MODE(SEQ_OF),
    ASN1_OBJ_MODE(OPTIONAL),
};

/** \typedef asn1_pack_size_t
 *  \brief Size calculation function type.
 *  \return Size if OK, negative error code if something is wrong.
 */
typedef int32_t (asn1_pack_size_f)(const void *data);

/** \typedef asn1_pack_t
 *  \brief Serialization function type.
 *  \note Use it when calculating the length is costless or useless
 *        during serialization.
 */
typedef uint8_t *(asn1_pack_f)(uint8_t *dst, const void *data);

/** \typedef asn1_unpack_t
 *  \brief Unpacking function type.
 *  \note Take a properly delimited input stream to unserialize
 *        ASN.1 frame.
 */
typedef int (asn1_unpack_f)(pstream_t *value, mem_pool_t *mem_pool,
                            void *out);

/** \brief User side structure for opaque (user defined) mode callbacks.
 */
typedef struct asn1_void_t {
    asn1_pack_size_f *pack_size;
    asn1_pack_f      *pack;
    asn1_unpack_f    *unpack;
} asn1_void_t;

/****************************/
/* Serialization core stuff */
/****************************/

/** \enum asn1_cstd_type
  * \brief Constructed field type enumeration.
  */
enum asn1_cstd_type {
    ASN1_CSTD_TYPE_SEQUENCE,
    ASN1_CSTD_TYPE_CHOICE,
    ASN1_CSTD_TYPE_SET,
};

/* Special field information {{{ */

typedef union asn1_int_t {
    int64_t i;
    uint64_t u;
} asn1_int_t;

/* XXX we use special values (INT64_MIN, INT64_MAX) because PER support far
 *     more than 64 bits integers
 */
typedef struct asn1_int_info_t {
    asn1_int_t    min;
    asn1_int_t    max;

    /* Pre-processed information */
    uint16_t      max_blen;      /* XXX needed only if fully constrained */
    uint8_t       max_olen_blen; /* XXX needed only for max_blen > 16    */
    uint64_t      d_max;         /* XXX needed only if fully constrained */

    /* Extensions */
    asn1_int_t    ext_min;
    asn1_int_t    ext_max;

    bool          has_min : 1;
    bool          has_max : 1;
    bool          extended : 1;
    bool          has_ext_min : 1;
    bool          has_ext_max : 1;
} asn1_int_info_t;

static inline void asn1_int_info_set_min(asn1_int_info_t *info, int64_t min)
{
    info->has_min = true;
    info->min.i = min;
}

static inline void asn1_int_info_set_max(asn1_int_info_t *info, int64_t max)
{
    info->has_max = true;
    info->max.i = max;
}

static inline void asn1_int_info_update(asn1_int_info_t *info, bool is_signed)
{
    if (!info)
        return;

    if (!info->has_min || !info->has_max) {
        return;
    }

    if (is_signed) {
        assert (info->min.i <= info->max.i);
        info->d_max = info->max.i - info->min.i;
    } else {
        assert (info->min.u <= info->max.u);
        info->d_max = info->max.u - info->min.u;
    }

    info->max_blen = u64_blen(info->d_max);

    if (info->max_blen > 16) {
        info->max_olen_blen = u64_blen(u64_olen(info->d_max) - 1);
    }
}


GENERIC_INIT(asn1_int_info_t, asn1_int_info);

static inline bool asn1_field_type_is_signed_int(enum obj_type type)
{
    return type == ASN1_OBJ_TYPE(int8_t) ||
           type == ASN1_OBJ_TYPE(int16_t) ||
           type == ASN1_OBJ_TYPE(int32_t) ||
           type == ASN1_OBJ_TYPE(int64_t);
}

static inline bool asn1_field_type_is_uint(enum obj_type type)
{
    return type == ASN1_OBJ_TYPE(uint8_t) ||
           type == ASN1_OBJ_TYPE(uint16_t) ||
           type == ASN1_OBJ_TYPE(uint32_t) ||
           type == ASN1_OBJ_TYPE(uint64_t);
}

typedef struct asn1_cnt_info_t {
    size_t        min;
    size_t        max; /* XXX SIZE_MAX if infinity */

    bool          extended;
    size_t        ext_min;
    size_t        ext_max; /* XXX SIZE_MAX if infinity */
} asn1_cnt_info_t;

static inline asn1_cnt_info_t *asn1_cnt_info_init(asn1_cnt_info_t *info)
{
    p_clear(info, 1);
    info->max     = SIZE_MAX;
    info->ext_max = SIZE_MAX;

    return info;
}

typedef struct asn1_enum_info_t {
    /* XXX Enumeration values in canonical order (for both root values and
     * extended values). */
    qv_t(i32)     values;
    qv_t(i32)     ext_values;

    /* Value to set when decoding an unknown extended value. */
    opt_i32_t ext_defval;

    asn1_int_info_t constraints;

    bool extended;
} asn1_enum_info_t;

static inline asn1_enum_info_t *asn1_enum_info_init(asn1_enum_info_t *e)
{
    p_clear(e, 1);
    asn1_int_info_init(&e->constraints);

    return e;
}

GENERIC_NEW(asn1_enum_info_t, asn1_enum_info);

static inline void asn1_enum_info_wipe(asn1_enum_info_t *info)
{
    qv_wipe(&info->values);
    qv_wipe(&info->ext_values);
}

GENERIC_DELETE(asn1_enum_info_t, asn1_enum_info);

void asn1_enum_info_reg_ext_defval(asn1_enum_info_t *info, int32_t defval);

/* }}} */

/** \brief Define specification of an asn1 field.
  * \note This structure is designed to be used only
  *       with dedicated functions and macros.
  */
typedef struct {
    const char     *name;      /**< API field type. */
    const char     *oc_t_name; /**< C field type. */

    uint32_t        tag;       /* TODO use uint8_t */
    uint8_t         tag_len;   /* TODO remove      */
    enum obj_mode   mode      : 7;
    bool            pointed   : 1;

    uint16_t        offset;
    enum obj_type   type;
    uint16_t        size;       /**< Message content structure size. */

    union {
        const struct asn1_desc_t   *comp;
        asn1_void_t                 opaque;
    } u;

    asn1_int_info_t             int_info;
    asn1_cnt_info_t             str_info;
    const asn1_enum_info_t     *enum_info;

    /* XXX SEQUENCE OF only */
    asn1_cnt_info_t       seq_of_info;

    /* Only for open type fields */
    /* XXX eg. type is <...>.&<...> */
    bool                        is_open_type : 1;
    bool                        is_extension : 1;
    size_t                      open_type_buf_len;
} asn1_field_t;

static inline void asn1_field_init_info(asn1_field_t *field)
{
    asn1_int_info_init(&field->int_info);
    asn1_cnt_info_init(&field->str_info);
    asn1_cnt_info_init(&field->seq_of_info);
}

qvector_t(asn1_field, asn1_field_t);

/** \brief Message descriptor.
  */
typedef struct asn1_desc_t {
    qv_t(asn1_field)      vec;
    size_t                size;
    enum asn1_cstd_type   type;

    /* XXX CHOICE only */
    asn1_int_info_t       choice_info;

    /* PER information */
    qv_t(u16)             opt_fields;
    uint16_t              ext_pos;
    bool                  is_extended : 1;

    /* TODO add SEQUENCE OF into constructed type enum */
    bool                  is_seq_of : 1;
} asn1_desc_t;

static inline asn1_desc_t *asn1_desc_init(asn1_desc_t *desc)
{
    p_clear(desc, 1);
    asn1_int_info_init(&desc->choice_info);

    return desc;
}

GENERIC_NEW(asn1_desc_t, asn1_desc);

static inline void asn1_desc_wipe(asn1_desc_t *desc)
{
    qv_wipe(&desc->vec);
    qv_wipe(&desc->opt_fields);
}
GENERIC_DELETE(asn1_desc_t, asn1_desc);

typedef struct asn1_choice_desc_t {
    asn1_desc_t desc;
    uint8_t     choice_table[256];
} asn1_choice_desc_t;

GENERIC_NEW_INIT(asn1_choice_desc_t, asn1_choice_desc);

static inline void asn1_choice_desc_wipe(asn1_choice_desc_t *desc)
{
    asn1_desc_wipe(&desc->desc);
}
GENERIC_DELETE(asn1_choice_desc_t, asn1_choice_desc);

qvector_t(asn1_desc, asn1_desc_t *);
qvector_t(asn1_choice_desc, asn1_choice_desc_t *);
qvector_t(asn1_enum_info, asn1_enum_info_t *);
struct asn1_descs_t {
    qv_t(asn1_desc) descs;
    qv_t(asn1_choice_desc) choice_descs;
    qv_t(asn1_enum_info) enums;
};
extern __thread struct asn1_descs_t asn1_descs_g;

int asn1_pack_size_(const void *st, const asn1_desc_t *desc,
                    qv_t(i32) *stack);
uint8_t *asn1_pack_(uint8_t *dst, const void *st, const asn1_desc_t *desc,
                    qv_t(i32) *stack);

int asn1_unpack_(pstream_t *ps, const asn1_desc_t *desc,
                 mem_pool_t *mem_pool, void *st, bool copy);

static inline int t_asn1_unpack(pstream_t *ps, const asn1_desc_t *desc,
                                void **out)
{
    void *v;

    v = t_new(byte, desc->size);
    RETHROW(asn1_unpack_(ps, desc, t_pool(), v, false));
    *out = v;

    return 0;
}

void asn1_reg_field(asn1_desc_t *desc, asn1_field_t *field);
void asn1_build_choice_table(asn1_choice_desc_t *desc);

const char *t_asn1_oid_print(lstr_t oid);

/** \brief Skips an ASN.1 BER field */
int asn1_skip_field(pstream_t *ps);

/** \brief Get an ASN.1 field recursively supporting indefinite lengths.
 *  \note This function is designed for ASN.1 fields without description.
 *  It can only by used for BER encoded streams.
 */
int asn1_get_ber_field(pstream_t *ps, bool indef_father, pstream_t *sub_ps);

/* Private */
const void *asn1_opt_field(const void *field, enum obj_type type);
void *asn1_opt_field_w(void *field, enum obj_type type, bool has_field);
int __asn1_get_int(const void *st, const asn1_field_t *desc);
void __asn1_set_int(void *st, const asn1_field_t *desc, int v);

#endif /* IS_LIB_SIGTRAN_ASN1_WRITER_H */
