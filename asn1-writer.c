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

#include "asn1.h"
#include "z.h"
#include "thr.h"

static void asn1_wipe(void)
{
    qv_deep_wipe(&asn1_descs_g.descs, asn1_desc_delete);
    qv_deep_wipe(&asn1_descs_g.choice_descs, asn1_choice_desc_delete);
    qv_deep_wipe(&asn1_descs_g.enums, asn1_enum_info_delete);
}
thr_hooks(NULL, asn1_wipe);

__thread struct asn1_descs_t asn1_descs_g;

/*----- COMMON -{{{- */

#ifndef NDEBUG
static const char *asn1_type_name(enum obj_type type)
{
    switch (type) {
#define CASE(t)  case ASN1_OBJ_TYPE(t): return #t
        CASE(bool);
        CASE(int8_t);
        CASE(uint8_t);
        CASE(int16_t);
        CASE(uint16_t);
        CASE(int32_t);
        CASE(uint32_t);
        CASE(int64_t);
        CASE(uint64_t);
        CASE(enum);
        case ASN1_OBJ_TYPE(NULL): return "NULL";
        CASE(OPT_NULL);
        CASE(lstr_t);
        CASE(OPEN_TYPE);
        CASE(asn1_bit_string_t);
        CASE(OPAQUE);
        CASE(asn1_ext_t);
        CASE(SEQUENCE);
        CASE(CHOICE);
        CASE(UNTAGGED_CHOICE);
        CASE(SKIP);
#undef CASE
    }
    return NULL;
}

static const char *asn1_mode_name(enum obj_mode mode)
{
    switch (mode) {
#define CASE(t)  case ASN1_OBJ_MODE(t): return #t
        CASE(MANDATORY);
        CASE(SEQ_OF);
        CASE(OPTIONAL);
#undef CASE
    }
    return NULL;
}

static void e_trace_desc(int level, const char *txt,
                        const asn1_desc_t *desc, int pos, int depth)
{
    const asn1_field_t *spec = &desc->vec.tab[pos];
    bool disp_type_name = spec->type == ASN1_OBJ_TYPE(OPAQUE)
                       || spec->type == ASN1_OBJ_TYPE(SEQUENCE)
                       || spec->type == ASN1_OBJ_TYPE(CHOICE)
                       || spec->type == ASN1_OBJ_TYPE(UNTAGGED_CHOICE);

    e_trace(3, "%s %*s(%d/%d) %s:%s%s%s:%s", txt,
        (depth % 16) * 2, "",
        pos + 1, desc->vec.len,
        asn1_mode_name(spec->mode), asn1_type_name(spec->type),
        disp_type_name ? ":" : "", disp_type_name ? spec->oc_t_name : "",
        spec->name);
}
#else
#   define e_trace_desc(...)
#endif

const char *t_asn1_oid_print(lstr_t oid)
{
    static const char digits[10] = "0123456789";

    char *str = t_new_raw(char, oid.len * 4);
    char *w = str;

    for (int i = 0;; i++) {
        uint8_t u = (uint8_t)oid.s[i];
        uint8_t d = u / 10;
        uint8_t c = d / 10;

        u -= 10 * d;
        d -= 10 * c;

        if (c)
            *w++ = digits[c];

        if (c || d)
            *w++ = digits[d];

        *w++ = digits[u];

        if (i >= oid.len)
            break;

        *w++ = '.';
    }

    *w = '\0';

    return str;
}

static ALWAYS_INLINE bool asn1_field_is_tagged(const asn1_field_t *field)
{
    switch (field->type) {
      case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        return false;
      default:
        return true;
    }
}

asn1_bit_string_t
t_asn1_bstring_from_bf64(uint64_t bit_field, int min_bit_len)
{
    int len, bit_len;
    be64_t be;

    assert (min_bit_len <= 64);
    bit_len = bit_field ? bsr64(bit_field) + 1 : 0;
    bit_len = MAX(bit_len, min_bit_len);
    len = DIV_ROUND_UP(bit_len, 8);

    be = cpu_to_be64(bit_reverse64(bit_field));

    return (asn1_bit_string_t){
        .bit_len = bit_len,
        .data = t_dup((const byte *)&be, len),
    };
}

void asn1_enum_info_reg_ext_defval(asn1_enum_info_t *info, int32_t defval)
{
    if (!info->extended) {
        e_panic("the enumeration is not extended");
    }

    if (OPT_ISSET(info->ext_defval)) {
        e_panic("default value already registered");
    }

    /* TODO use contains_i32 */
    tab_for_each_entry(v, &info->values) {
        if (defval == v) {
            e_panic("cannot use %d as default value: "
                    "already registered (root value)", defval);
        }
    }
    /* TODO use contains_i32 */
    tab_for_each_entry(v, &info->ext_values) {
        if (defval == v) {
            e_panic("cannot use %d as default value: "
                    "already registered (extended value)", defval);
        }
    }

    OPT_SET(info->ext_defval, defval);
}

/* }}} */
/*----- PACKER -{{{- */
#define ASN1_BOOL_TRUE_VALUE 0x01

const void *asn1_opt_field(const void *field, enum obj_type type)
{
     switch (type) {
       case ASN1_OBJ_TYPE(bool):
         return ((const opt_bool_t *)field)->has_field ? field : NULL;
       case ASN1_OBJ_TYPE(int8_t): case ASN1_OBJ_TYPE(uint8_t):
         return ((const opt_i8_t *)field)->has_field ? field : NULL;
       case ASN1_OBJ_TYPE(int16_t): case ASN1_OBJ_TYPE(uint16_t):
         return ((const opt_i16_t *)field)->has_field ? field : NULL;
       case ASN1_OBJ_TYPE(int32_t): case ASN1_OBJ_TYPE(uint32_t):
       case ASN1_OBJ_TYPE(enum):
         return ((const opt_i32_t *)field)->has_field ? field : NULL;
       case ASN1_OBJ_TYPE(int64_t): case ASN1_OBJ_TYPE(uint64_t):
         return ((const opt_i64_t *)field)->has_field ? field : NULL;
       case ASN1_OBJ_TYPE(NULL): /* Should not happen. */
         return NULL;
       case ASN1_OBJ_TYPE(OPT_NULL):
         return *(const bool *)field ? field : NULL;
       case ASN1_OBJ_TYPE(lstr_t):
       case ASN1_OBJ_TYPE(OPEN_TYPE):   case ASN1_OBJ_TYPE(asn1_bit_string_t):
         return ((const lstr_t *)field)->data ? field : NULL;
       case ASN1_OBJ_TYPE(asn1_ext_t):
         return ((const asn1_ext_t *)field)->data ? field : NULL;
       case ASN1_OBJ_TYPE(OPAQUE): case ASN1_OBJ_TYPE(SEQUENCE):
       case ASN1_OBJ_TYPE(CHOICE): case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
         return field;
       case ASN1_OBJ_TYPE(SKIP):
         return NULL;
    }
    return NULL;
}

/* ----- BUILTIN PACKING FUNCTIONS - {{{ - */
/************************************/
/* Built-in serialization functions */
/************************************/

static uint8_t *asn1_pack_bool(uint8_t *dst, bool b)
{
    *dst++ = (b ? ASN1_BOOL_TRUE_VALUE : 0);

    return dst;
}

static uint8_t *asn1_pack_int32(uint8_t *dst, int32_t i)
{
    be32_t be32 = cpu_to_be32(i);
    size_t len  = asn1_int32_size(i);

    return mempcpy(dst, (char *)&be32 + 4 - len, len);
}

static uint8_t *asn1_pack_int64(uint8_t *dst, int64_t i)
{
    be64_t be64 = cpu_to_be64(i);
    size_t len = asn1_int64_size(i);

    return mempcpy(dst, (char *)&be64 + 8 - len, len);
}

static uint8_t *asn1_pack_uint32(uint8_t *dst, uint32_t i)
{
    return asn1_pack_int64(dst, i);
}

static uint8_t *asn1_pack_uint64(uint8_t *dst, uint64_t u)
{
    if (unlikely((0x1ULL << 63) & u)) {
        be64_t be64 = cpu_to_be64(u);
        *dst++ = (uint8_t)0;
        return mempcpy(dst, (char *)&be64, 8);
    }

    return asn1_pack_int64(dst, (int64_t)u);
}

static uint8_t *asn1_pack_len(uint8_t *dst, uint32_t i)
{
    if (i >= 0x80) {
        size_t len = 1 + bsr32(i) / 8;
        be32_t be32 = cpu_to_be32(i);

        *dst++ = 0x80 | len;
        return mempcpy(dst, (char *)&be32 + 4 - len, len);
    }
    *dst++ = i;
    return dst;
}

static uint8_t *asn1_pack_tag(uint8_t *dst, uint32_t tag, uint8_t len)
{
    be32_t be32 = cpu_to_be32(tag);

    return mempcpy(dst, (char *)&be32 + 4 - len, len);
}

static uint8_t *asn1_pack_data(uint8_t *dst, const lstr_t *data)
{
    return mempcpy(dst, data->data, data->len);
}

static uint8_t *asn1_pack_bit_string(uint8_t *dst, const asn1_bit_string_t *bs)
{
    /* TODO find an existing function that does the same thing */
    int32_t size = asn1_bit_string_size(bs) - 1;
    *dst++ = (8 - (bs->bit_len) % 8) % 8;

    return mempcpy(dst, bs->data, size);
}
/* - }}} */

/********************************/
/* API serialization function   */
/********************************/

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

/* ----- SIZE PACKING - {{{ - */
static int asn1_pack_size_rec(const void *st, const asn1_desc_t *desc,
                              qv_t(i32) *stack);

static int asn1_pack_value_size(const void *dt, const asn1_field_t *spec,
                                qv_t(i32) *stack, int32_t *len)
{
    int32_t data_size;

    switch (spec->type) {
      case ASN1_OBJ_TYPE(bool): case ASN1_OBJ_TYPE(int8_t):
        qv_append(stack, data_size = 1);
        break;
      case ASN1_OBJ_TYPE(uint8_t):
        data_size = asn1_int32_size(*(const uint8_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(int16_t):
        data_size = asn1_int32_size(*(const int16_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(uint16_t):
        data_size = asn1_int32_size(*(const uint16_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(int32_t):
        data_size = asn1_int32_size(*(const int32_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(enum):
        data_size = asn1_int32_size(*(const int *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(uint32_t):
        data_size = asn1_uint32_size(*(const uint32_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(int64_t):
        data_size = asn1_int64_size(*(const int64_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(uint64_t):
        data_size = asn1_uint64_size(*(const uint64_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(NULL):
      case ASN1_OBJ_TYPE(OPT_NULL):
        data_size = 0;
        qv_append(stack, 0);
        break;
      case ASN1_OBJ_TYPE(lstr_t):
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        /* IF ASSERT: user maybe forgot to declare field as optional */
        if (!((lstr_t *)dt)->data) {
           e_trace(0, "%s", spec->name);
        }
        assert (((lstr_t *)dt)->data);
        data_size = ((lstr_t *)dt)->len;
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(asn1_bit_string_t):
        /* IF ASSERT: user maybe forgot to declare field as optional */
        assert (((asn1_bit_string_t *)dt)->data);
        data_size = asn1_bit_string_size((asn1_bit_string_t *)dt);
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(SEQUENCE): case ASN1_OBJ_TYPE(CHOICE):
      case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
        {   /* In this case, length is known after but must be written before
             * any contained field length, so we must keep a space in the
             * stack. */
            size_t len_pos = stack->len;
            qv_append(stack, 0);
            RETHROW(data_size = asn1_pack_size_rec(dt, spec->u.comp, stack));
            stack->tab[len_pos] = data_size;
        }
        break;
      case ASN1_OBJ_TYPE(asn1_ext_t):
        {
            size_t len_pos = stack->len;
            qv_append(stack, 0);
            RETHROW(data_size = asn1_pack_size_rec(
                                    ((const asn1_ext_t *)dt)->data,
                                    ((const asn1_ext_t *)dt)->desc,
                                    stack));
            stack->tab[len_pos] = data_size;
        }
        break;
      case ASN1_OBJ_TYPE(OPAQUE):
        RETHROW(data_size = (*spec->u.opaque.pack_size)(dt));
        qv_append(stack, data_size);
        break;
      case ASN1_OBJ_TYPE(SKIP):
      default:
        e_panic("should not happen");
        return -1;
    }


    if (unlikely(!asn1_field_is_tagged(spec))) {
        *len += data_size;
    } else {
        *len += data_size + asn1_length_size(data_size) + spec->tag_len;
    }

    return *len;
}

static int asn1_pack_field_size(const void *st, const asn1_field_t *spec,
                                qv_t(i32) *stack, int32_t *len)
{
    if (unlikely(spec->type == ASN1_OBJ_TYPE(SKIP))) {
        return 0;
    }

    switch (spec->mode) {
      case ASN1_OBJ_MODE(MANDATORY):
        /* IF ASSERT: user maybe forgot to declare field as optional */
        assert (GET_DATA_P(st, spec, void));
        RETHROW(asn1_pack_value_size(GET_DATA_P(st, spec, void),
                                     spec, stack, len));
        break;
      case ASN1_OBJ_MODE(OPTIONAL):
        {
            const void *field = asn1_opt_field(GET_DATA_P(st, spec, uint8_t),
                                             spec->type);

            if (field) {
                RETHROW(asn1_pack_value_size(field, spec, stack, len));
            }
            break;
        }
      case ASN1_OBJ_MODE(SEQ_OF):
        {
            const uint8_t *tab = (const uint8_t *)GET_VECTOR_DATA(st, spec);
            int vec_len = GET_VECTOR_LEN(st, spec);

            if (spec->pointed) {
                for (int j = 0; j < vec_len; j++) {
                    RETHROW(asn1_pack_value_size(((const void **)tab)[j],
                                                 spec, stack, len));
                }
            } else {
                for (int j = 0; j < vec_len; j++) {
                    RETHROW(asn1_pack_value_size(tab + j * spec->size,
                                                 spec, stack, len));
                }
            }
        }
        break;
    }

    return 0;
}

static int asn1_pack_sequence_size(const void *st,
                                   const asn1_desc_t *desc,
                                   qv_t(i32) *stack)
{
    int len = 0;

    for (int i = 0; i < desc->vec.len; i++) {
        asn1_field_t *spec = &desc->vec.tab[i];

        RETHROW(asn1_pack_field_size(st, spec, stack, &len));
    }

    return len;
}

int __asn1_get_int(const void *st, const asn1_field_t *desc)
{
    switch (desc->type) {
#define CASE(type)                             \
      case ASN1_OBJ_TYPE(type):                \
        return *GET_DATA_P(st, desc, type)

      CASE(int8_t);
      CASE(uint8_t);
      CASE(int16_t);
      CASE(uint16_t);
      case ASN1_OBJ_TYPE(enum):
        /* FALLTHROUGH */
      CASE(int32_t);
      CASE(uint32_t);
      CASE(int64_t);
      CASE(uint64_t);

#undef CASE

      default:
        e_assert(panic, false, "get_int: unexpected field type: %d",
                 desc->type);
    }

    return 0;
}

static int asn1_pack_choice_size(const void *st, const asn1_desc_t *desc,
                                 qv_t(i32) *stack)
{   /* Could be way shorter but far more reader friendly this way */
    int len = 0;
    const asn1_field_t *choice_spec;
    const asn1_field_t *selector_spec;
    int choice;

    assert (desc->vec.len > 1);

    selector_spec = &desc->vec.tab[0];

    choice = __asn1_get_int(st, selector_spec);
    assert (choice > 0 && choice < desc->vec.len);
    choice_spec = &desc->vec.tab[choice];

    RETHROW(asn1_pack_field_size(st, choice_spec, stack, &len));

    return len;
}

/**
 * \brief Compute all field lengths to prepare serialization.
 * \param[in]st      Message desctern.
 * \param[inout]desc Message specification.
 * \param[out]len    Message length.
 * \return           Obtained length or error code.
 */
static int asn1_pack_size_rec(const void *st, const asn1_desc_t *desc,
                              qv_t(i32) *stack)
{
    switch (desc->type) {
      case ASN1_CSTD_TYPE_SEQUENCE:
        return asn1_pack_sequence_size(st, desc, stack);
      case ASN1_CSTD_TYPE_CHOICE:
        return asn1_pack_choice_size(st, desc, stack);
      case ASN1_CSTD_TYPE_SET:
        e_panic("not supported yet");
        return -1;
      default:
        e_panic("should not happen");
        return -1;
    }
}
/* - }}} */
/* ----- PROPER PACKING -{{{- */
static uint8_t *asn1_pack_rec(uint8_t *dst, const void *st, const
                              asn1_desc_t *desc, int32_t depth,
                              qv_t(i32) *stack);

/**
 * \brief Serialize a single given field following specs.
 */
static uint8_t *asn1_pack_value(uint8_t *dst, const void *dt,
                                const asn1_field_t *spec, int32_t depth,
                                qv_t(i32) *stack)
{
    int32_t data_size = stack->tab[stack->len++];

    if (likely(asn1_field_is_tagged(spec))) {
       dst = asn1_pack_tag(dst, spec->tag, spec->tag_len);
       dst = asn1_pack_len(dst, data_size);
    }

    switch (spec->type) {
      case ASN1_OBJ_TYPE(bool):
        dst = asn1_pack_bool(dst, *(bool *)dt);
        e_trace(4, "value: %s", *(bool *)dt ? "true" : "false");
        break;
      case ASN1_OBJ_TYPE(int8_t):
        *dst++ = *(int8_t *)dt;
        e_trace(4, "value: %d", *(int8_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint8_t):
        dst = asn1_pack_int32(dst, *(uint8_t *)dt);
        e_trace(4, "value: %u", *(uint8_t *)dt);
        break;
      case ASN1_OBJ_TYPE(int16_t):
        dst = asn1_pack_int32(dst, *(int16_t *)dt);
        e_trace(4, "value: %d", *(int16_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint16_t):
        dst = asn1_pack_int32(dst, *(uint16_t *)dt);
        e_trace(4, "value: %u", *(uint16_t *)dt);
        break;
      case ASN1_OBJ_TYPE(int32_t):
        dst = asn1_pack_int32(dst, *(int32_t *)dt);
        e_trace(4, "value: %d", *(int32_t *)dt);
        break;
      case ASN1_OBJ_TYPE(enum):
        dst = asn1_pack_int32(dst, *(int32_t *)dt);
        e_trace(4, "value: %d", *(int32_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint32_t):
        dst = asn1_pack_uint32(dst, *(uint32_t *)dt);
        e_trace(4, "value: %u", *(uint32_t *)dt);
        break;
      case ASN1_OBJ_TYPE(int64_t):
        dst = asn1_pack_int64(dst, *(int64_t *)dt);
        e_trace(4, "value: %jd", *(int64_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint64_t):
        dst = asn1_pack_uint64(dst, *(uint64_t *)dt);
        e_trace(4, "value: %ju", *(uint64_t *)dt);
        break;
      case ASN1_OBJ_TYPE(NULL):
      case ASN1_OBJ_TYPE(OPT_NULL):
        break;
      case ASN1_OBJ_TYPE(lstr_t):
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        dst = asn1_pack_data(dst, (const lstr_t *)dt);
        e_trace_hex(4, "value:", ((const lstr_t *)dt)->data,
                    ((const lstr_t *)dt)->len);
        break;
      case ASN1_OBJ_TYPE(asn1_bit_string_t):
        dst = asn1_pack_bit_string(dst, (const asn1_bit_string_t *)dt);
        /* TODO print */
        break;
      case ASN1_OBJ_TYPE(SEQUENCE): case ASN1_OBJ_TYPE(CHOICE):
      case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
        dst = asn1_pack_rec(dst, dt, spec->u.comp, depth + 1, stack);
        break;
      case ASN1_OBJ_TYPE(asn1_ext_t):
        dst = asn1_pack_rec(dst, ((const asn1_ext_t *)dt)->data,
                            ((const asn1_ext_t *)dt)->desc, depth + 1,
                            stack);
        break;
      case ASN1_OBJ_TYPE(OPAQUE):
        dst = (*spec->u.opaque.pack)(dst, dt);
        break;
      case ASN1_OBJ_TYPE(SKIP):
        break;
    }

    return dst;
}

static uint8_t *asn1_pack_field(uint8_t *dst, const void *st,
                                const asn1_field_t *spec, int32_t depth,
                                qv_t(i32) *stack)
{
    if (unlikely(spec->type == ASN1_OBJ_TYPE(SKIP))) {
        return dst;
    }

    switch (spec->mode) {
      case ASN1_OBJ_MODE(MANDATORY):
        dst = asn1_pack_value(dst, GET_DATA_P(st, spec, void),
                              spec, depth, stack);
        break;
      case ASN1_OBJ_MODE(OPTIONAL):
        {
            const void *field = asn1_opt_field(GET_DATA_P(st, spec, void),
                                             spec->type);

            if (field) {
                dst = asn1_pack_value(dst, field, spec, depth, stack);
            }
        }
        break;
      case ASN1_OBJ_MODE(SEQ_OF):
        {
            const uint8_t *tab = (const uint8_t *)GET_VECTOR_DATA(st,
                                                                  spec);
            int vec_len = GET_VECTOR_LEN(st, spec);

            if (spec->pointed) {
                for (int j = 0; j < vec_len; j++) {
                    dst = asn1_pack_value(dst, ((const void **)tab)[j],
                                          spec, depth, stack);
                }
            } else {
                for (int j = 0; j < vec_len; j++) {
                    dst = asn1_pack_value(dst, tab + j * spec->size, spec,
                                          depth, stack);
                }
            }
        }
        break;
    }

    return dst;
}

static uint8_t *asn1_pack_sequence(uint8_t *dst, const void *st,
                                   const asn1_desc_t *desc, int32_t depth,
                                   qv_t(i32) *stack)
{
    for (int i = 0; i < desc->vec.len; i++) {
        const asn1_field_t *spec = &desc->vec.tab[i];

        e_trace_desc(1, "serializing", desc, i, depth);
        dst = asn1_pack_field(dst, st, spec, depth, stack);
    }

    return dst;
}

static uint8_t *asn1_pack_choice(uint8_t *dst, const void *st,
                                 const asn1_desc_t *desc, int32_t depth,
                                 qv_t(i32) *stack)
{   /* Could be way shorter but far more reader friendly this way */
    const asn1_field_t *choice_spec;
    const asn1_field_t *selector_spec;
    int choice;

    assert (desc->vec.len > 1);

    selector_spec = &desc->vec.tab[0];

    choice = __asn1_get_int(st, selector_spec);
    assert (choice > 0 && choice < desc->vec.len);
    choice_spec = &desc->vec.tab[choice];

    e_trace_desc(1, "serializing", desc, choice, depth);
    return asn1_pack_field(dst, st, choice_spec, depth, stack);
}

/**
 * \brief Serialize a given amount of data following specs.
 * \note We suppose asn1_pack_size have been called before for this data.
 * \param[in]st     Message content.
 * \param[inout]desc Message specification.
 * \param[inout]dst Output dstfer on which data is written.
 */
static uint8_t *asn1_pack_rec(uint8_t *dst, const void *st,
                              const asn1_desc_t *desc, int32_t depth,
                              qv_t(i32) *stack)
{
    switch (desc->type) {
      case ASN1_CSTD_TYPE_SEQUENCE:
        return asn1_pack_sequence(dst, st, desc, depth, stack);
      case ASN1_CSTD_TYPE_CHOICE:
        return asn1_pack_choice(dst, st, desc, depth, stack);
      case ASN1_CSTD_TYPE_SET:
        e_panic("not supported yet");
      default:
        return dst;
    }
}
/* - }}} */

void asn1_reg_field(asn1_desc_t *desc, asn1_field_t *field)
{
    /* TODO check what can be checked */
    if (desc->vec.len > 0
    &&  (field->mode == ASN1_OBJ_MODE(SEQ_OF)
     ||  desc->vec.tab[desc->vec.len - 1].mode == ASN1_OBJ_MODE(SEQ_OF)))
    {
        e_fatal("ASN.1 field %s should be explicitly "
                "tagged as a sequence", field->name);
    }

    if (desc->is_extended) {
        if (desc->type == ASN1_CSTD_TYPE_SEQUENCE
        &&  field->mode != ASN1_OBJ_MODE(OPTIONAL))
        {
            e_fatal("ASN.1 extension field `%s` should be optional",
                    field->name);
        }

        field->is_extension = true;
    } else {
        if (field->mode == ASN1_OBJ_MODE(OPTIONAL)) {
            qv_append(&desc->opt_fields, desc->vec.len);
        }
    }

    asn1_field_init_info(field);
    qv_append(&desc->vec, *field);
}

static void asn1_choice_desc_set_field(asn1_choice_desc_t *desc,
                                       const asn1_field_t *field, uint8_t idx)
{
    if (field->type == ASN1_OBJ_TYPE(UNTAGGED_CHOICE)) {
        const asn1_desc_t *sub_choice_desc = field->u.comp;

        /* XXX i = 0 is for enum field */
        for (int i = 1; i < sub_choice_desc->vec.len; i++) {
            const asn1_field_t *sub_choice_field =
                &sub_choice_desc->vec.tab[i];

            asn1_choice_desc_set_field(desc, sub_choice_field, idx);
        }

        return;
    }

    if (desc->choice_table[field->tag]) {
        e_error("[ASN.1] Field %s has the same tag (0x%2X) as another "
                "field in a choice", field->name, field->tag);
        assert (0);
    }

    desc->choice_table[field->tag] = idx;
}

void asn1_build_choice_table(asn1_choice_desc_t *desc)
{
    /* TODO add static assert when tags will be uint8_t */
    /* Choice table size is 256 because tag size is one octet */
    /* STATIC_ASSERT (sizeof(((asn1_field_t *)0)->tag) == 1); */

    p_clear(desc->choice_table, sizeof(desc->choice_table));

    for (int i = 1; i < desc->desc.vec.len; i++) {
        asn1_field_t *spec = &desc->desc.vec.tab[i];

        asn1_choice_desc_set_field(desc, spec, i);
    }
}

static int asn1_find_choice(const asn1_choice_desc_t *desc, uint8_t tag)
{
    return (int)desc->choice_table[tag];
}

uint8_t *asn1_pack_(uint8_t *dst, const void *st, const asn1_desc_t *desc,
                    qv_t(i32) *stack)
{
    stack->len = 0;
    return asn1_pack_rec(dst, st, desc, 0, stack);
}

int asn1_pack_size_(const void *st, const asn1_desc_t *desc,
                    qv_t(i32) *stack)
{
    stack->len = 0;
    return asn1_pack_size_rec(st, desc, stack);
}
/* }}} */
/*----- UNPACKER -- */
/*********************/
/* ASN.1 Reader part */
/*********************/

void *asn1_opt_field_w(void *field, enum obj_type type, bool has_field)
{
    switch (type) {
      case ASN1_OBJ_TYPE(bool):
        if (!(((opt_bool_t *)field)->has_field = has_field)) {
           ((opt_bool_t *)field)->v = false;
        }
        return (uint8_t *)field + offsetof(opt_bool_t, v);
      case ASN1_OBJ_TYPE(int8_t): case ASN1_OBJ_TYPE(uint8_t):
        if (!(((opt_i8_t *)field)->has_field = has_field)) {
            ((opt_i8_t *)field)->v = 0;
        }
        return (uint8_t *)field + offsetof(opt_i8_t, v);
      case ASN1_OBJ_TYPE(int16_t): case ASN1_OBJ_TYPE(uint16_t):
        if (!(((opt_i16_t *)field)->has_field = has_field)) {
            ((opt_i16_t *)field)->v = 0;
        }
        return (uint16_t *)field + offsetof(opt_i16_t, v);
      case ASN1_OBJ_TYPE(int32_t): case ASN1_OBJ_TYPE(uint32_t):
      case ASN1_OBJ_TYPE(enum):
        if (!(((opt_i32_t *)field)->has_field = has_field)) {
            ((opt_i32_t *)field)->v = 0;
        }
        return (uint32_t *)field + offsetof(opt_i32_t, v);
      case ASN1_OBJ_TYPE(int64_t): case ASN1_OBJ_TYPE(uint64_t):
        if (!(((opt_i64_t *)field)->has_field = has_field)) {
            ((opt_i64_t *)field)->v = 0;
        }
        return (uint64_t *)field + offsetof(opt_i64_t, v);
      case ASN1_OBJ_TYPE(NULL): /* Should not happen. */
        return NULL;
      case ASN1_OBJ_TYPE(OPT_NULL):
        *(bool *)field = has_field;
        return field;
      case ASN1_OBJ_TYPE(lstr_t):
      case ASN1_OBJ_TYPE(OPEN_TYPE):   case ASN1_OBJ_TYPE(asn1_bit_string_t):
        if (!has_field) {
            *(lstr_t *)field = LSTR_NULL_V;
        }
        return field;
      case ASN1_OBJ_TYPE(asn1_ext_t):
        if (!has_field) {
            ((asn1_ext_t *)field)->has_value = false;
            ((asn1_ext_t *)field)->value = ps_initptr(NULL, NULL);
        }
        return field;
      case ASN1_OBJ_TYPE(OPAQUE): case ASN1_OBJ_TYPE(SEQUENCE):
      case ASN1_OBJ_TYPE(CHOICE): case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
        if (!has_field) {
            *((void **)field) = NULL;
        }
        return *(void **)field;
      case ASN1_OBJ_TYPE(SKIP):
        return NULL;
      default:
        e_panic("unexpected type");
    }
}

void __asn1_set_int(void *st, const asn1_field_t *desc, int v)
{
    switch (desc->type) {
#define CASE(type)                             \
      case ASN1_OBJ_TYPE(type):                \
        *GET_PTR(st, desc, type) = v;          \
        break;

      CASE(int8_t);
      CASE(uint8_t);
      CASE(int16_t);
      CASE(uint16_t);
      case ASN1_OBJ_TYPE(enum):
        /* FALLTHROUGH */
      CASE(int32_t);
      CASE(uint32_t);
      CASE(int64_t);
      CASE(uint64_t);

#undef CASE

      default:
        e_assert(panic, false, "set_int: unexpected field type: %d",
                 desc->type);
    }
}

int asn1_get_ber_field(pstream_t *ps, bool indef_father, pstream_t *sub_ps)
{
    uint32_t data_size;
    int n_eoc = indef_father ? 1: 0;

    if (sub_ps) {
        sub_ps->b = ps->b;
    }

    do {
        if (!ps_has(ps, 2)) {
            e_trace(1, "error: stream end");
            return -1;
        }

        if (ps->b[0]) {
            __ps_skip(ps, 1);

            if (RETHROW(ber_decode_len32(ps, &data_size))) {
                n_eoc++;
            } else {
                if (unlikely(ps_skip(ps, data_size) < 0)) {
                    e_trace(1, "error: not enough bytes");
                    return -1;
                }
            }
        } else {
            if (unlikely(ps->b[1])) {
                /* See: ITU Fascicle VIII.4 - Rec. X.209 - 23.5.4 */
                e_trace(1, "invalid EOC");
                return -1;
            }

            if (likely(n_eoc > 1 || !indef_father)) {
                __ps_skip(ps, 2);
            }

            n_eoc--;
        }
    } while (n_eoc);

    if (sub_ps) {
        sub_ps->b_end = ps->b;
    }

    return 0;
}

int asn1_skip_field(pstream_t *ps)
{
    return asn1_get_ber_field(ps, false, NULL);
}

static int asn1_unpack_rec(pstream_t *ps, const asn1_desc_t *desc,
                           mem_pool_t *mem_pool, int depth, void *st,
                           bool copy, bool indef_len);

static int asn1_unpack_value(pstream_t *ps, const asn1_field_t *spec,
                             mem_pool_t *mem_pool, int depth, void *dt,
                             bool copy)
{
    uint32_t data_size;
    pstream_t field_ps;
    bool indef_len;

    switch (spec->type) {
      case ASN1_OBJ_TYPE(SKIP):
        return asn1_get_ber_field(ps, false, NULL);
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        RETHROW(asn1_get_ber_field(ps, false, &field_ps));
        data_size = ps_len(&field_ps);
        indef_len = false;
        break;
      default:
        RETHROW(ps_skip(ps, 1));

        if (!RETHROW(ber_decode_len32(ps, &data_size))) {
            if (ps_get_ps(ps, data_size, &field_ps) < 0) {
                e_trace(1, "p-stream does not have enough bytes "
                        "(got %zd needed %u)", ps_len(ps), data_size);
                return -1;
            }

            indef_len = false;
        } else {
            field_ps = *ps;

            if (spec->type < ASN1_OBJ_TYPE(asn1_ext_t)) {
                e_trace(1, "error: unexpected indefinite length");
                return -1;
            }

            indef_len = true;
        }
        break;
    }

    switch (spec->type) {
      case ASN1_OBJ_TYPE(bool):
        *(bool *)dt = (ps_getc(&field_ps) ? true : false);
        e_trace(4, "value: %s", *(bool *)dt ? "true" : "false");
        break;
      case ASN1_OBJ_TYPE(int8_t):
        *(int8_t *)dt = ps_getc(&field_ps);
        e_trace(4, "value: %d", *(int8_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint8_t):
        if (unlikely(ps_len(&field_ps) == 2)) {
            if (*field_ps.b == 0x00) {
                __ps_skip(&field_ps, 1);
            } else {
                e_trace(1, "wrong uint8 size");
                return -1;
            }
        }
        *(uint8_t *)dt = ps_getc(&field_ps);
        e_trace(4, "value: %u", *(uint8_t *)dt);
        break;
      case ASN1_OBJ_TYPE(int16_t):
        RETHROW(ber_decode_int16(&field_ps, (int16_t *)dt));
        e_trace(4, "value: %d", *(int16_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint16_t):
        RETHROW(ber_decode_uint16(&field_ps, (uint16_t *)dt));
        e_trace(4, "value: %u", *(uint16_t *)dt);
        break;
      case ASN1_OBJ_TYPE(int32_t): case ASN1_OBJ_TYPE(enum):
        RETHROW(ber_decode_int32(&field_ps, (int32_t *)dt));
        e_trace(4, "value: %d", *(int32_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint32_t):
        RETHROW(ber_decode_uint32(&field_ps, (uint32_t *)dt));
        e_trace(4, "value: %u", *(uint32_t *)dt);
        break;
      case ASN1_OBJ_TYPE(int64_t):
        RETHROW(ber_decode_int64(&field_ps, (int64_t *)dt));
        e_trace(4, "value: %jd", *(int64_t *)dt);
        break;
      case ASN1_OBJ_TYPE(uint64_t):
        RETHROW(ber_decode_uint64(&field_ps, (uint64_t *)dt));
        e_trace(4, "value: %ju", *(uint64_t *)dt);
        break;
      case ASN1_OBJ_TYPE(NULL):
      case ASN1_OBJ_TYPE(OPT_NULL):
        break;
      case ASN1_OBJ_TYPE(lstr_t):
      case ASN1_OBJ_TYPE(OPEN_TYPE):
        *(lstr_t *)dt = LSTR_PS_V(&field_ps);
        if (copy) {
            mp_lstr_persists(mem_pool, (lstr_t *)dt);
        }
        e_trace_hex(4, "value:", field_ps.s, (int)ps_len(&field_ps));
        break;
      case ASN1_OBJ_TYPE(asn1_bit_string_t):
        if (copy) {
            ((asn1_bit_string_t *)dt)->data = mp_dup(mem_pool, field_ps.b + 1,
                                                     ps_len(&field_ps) - 1);
        } else {
            ((asn1_bit_string_t *)dt)->data = field_ps.b + 1;
        }
        ((asn1_bit_string_t *)dt)->bit_len = 8 * (ps_len(&field_ps) - 1)
                                           - field_ps.b[0];
        /* TODO print */
        break;
      case ASN1_OBJ_TYPE(SEQUENCE): case ASN1_OBJ_TYPE(CHOICE):
      case ASN1_OBJ_TYPE(UNTAGGED_CHOICE):
        RETHROW(asn1_unpack_rec(&field_ps, spec->u.comp, mem_pool,
                                depth + 1, dt, copy, indef_len));
        break;
      case ASN1_OBJ_TYPE(asn1_ext_t):
        ((asn1_ext_t *)dt)->data      = NULL;
        ((asn1_ext_t *)dt)->desc      = NULL;
        ((asn1_ext_t *)dt)->has_value = true;

        if (indef_len) {
            RETHROW(asn1_get_ber_field(&field_ps, true,
                                    &((asn1_ext_t *)dt)->value));
        } else {
            ((asn1_ext_t *)dt)->value = field_ps;
        }
        break;
      case ASN1_OBJ_TYPE(OPAQUE):
        RETHROW((*spec->u.opaque.unpack)(&field_ps, mem_pool, dt));
        break;
      case ASN1_OBJ_TYPE(SKIP):
        e_panic("should not happen");
        break;
    }

    if (indef_len) {
        *ps = field_ps;

        /* Skip every trailing fields up to EOC. */
        RETHROW(asn1_get_ber_field(ps, true, NULL));

        /* Skip EOC. */
        __ps_skip(ps, 2);
    }

    return 0;
}

static int asn1_sequenceof_len(pstream_t ps, uint8_t tag)
{
    int len;

    for (len = 0; ps_peekc(ps) == tag; len++) {
        if (asn1_get_ber_field(&ps, false, NULL) < 0) {
            e_trace(1, "invalid BER content in SEQUENCE OF");
            return -1;
        }
    }

    return len;
}

void asn1_alloc_seq_of(void *st, int count, const asn1_field_t *field,
                       mem_pool_t *mp)
{
    if (field->pointed) {
        asn1_void_array_t *array = GET_PTR(st, field, asn1_void_array_t);

        array->data = mp_new_raw(mp, void *, count);
        array->len  = count;

        for (int i = 0; i < count; i++) {
            array->data[i] = mp_new_raw(mp, char, field->size);
        }
    } else {
        asn1_void_vector_t *vector = GET_PTR(st, field, asn1_void_vector_t);

        vector->data = mp_new_raw(mp, char, count * field->size);
        vector->len  = count;
    }
}

static void *asn1_alloc_if_pointed(const asn1_field_t *spec,
                                   mem_pool_t *mem_pool, void *st)
{
    if (spec->pointed) {
        return (*GET_PTR(st, spec, void *) =
                mp_new_raw(mem_pool, char, spec->size));
    }

    return GET_PTR(st, spec, void);
}

static int asn1_unpack_field(pstream_t *ps, const asn1_field_t *spec,
                             mem_pool_t *mem_pool, int depth, void *st,
                             bool copy, bool indef_len)
{
    switch (spec->mode) {
      case ASN1_OBJ_MODE(MANDATORY):
        if (!asn1_field_is_tagged(spec)
        ||  (ps_has(ps, 1) && *ps->b == spec->tag))
        {
            void *value = asn1_alloc_if_pointed(spec, mem_pool, st);

            RETHROW(asn1_unpack_value(ps, spec, mem_pool, depth, value,
                                      copy));
        } else {
            if (ps_has(ps, 1)) {
                e_trace(0, "mandatory value -- %s -- not found (got tag %x)",
                        spec->name, *ps->b);
            } else {
                e_trace(0, "mandatory value -- %s -- not found (input end)",
                        spec->name);
            }
            return -1;
        }
        break;

      case ASN1_OBJ_MODE(OPTIONAL):
        if (!ps_done(ps) && *ps->b
        &&  (!asn1_field_is_tagged(spec) || *ps->b == spec->tag))
        {
            void *value;

            asn1_alloc_if_pointed(spec, mem_pool, st);
            value = asn1_opt_field_w(GET_PTR(st, spec, void), spec->type,
                                     true);
            RETHROW(asn1_unpack_value(ps, spec, mem_pool, depth, value,
                                      copy));
        } else {
            asn1_opt_field_w(GET_PTR(st, spec, void), spec->type, false);
        }
        break;
      case ASN1_OBJ_MODE(SEQ_OF):
        {
            int count;

            RETHROW(count = asn1_sequenceof_len(*ps, spec->tag));

            if (unlikely(!count)) {
                p_clear(GET_PTR(st, spec, asn1_void_vector_t), 1);
                break;
            }

            asn1_alloc_seq_of(st, count, spec, mem_pool);

            for (int j = 0; j < count; j++) {
                void *st_ptr;
                if (spec->pointed) {
                    st_ptr = GET_PTR(st, spec, asn1_void_array_t)->data[j];
                } else {
                    st_ptr = (char *)(GET_PTR(st, spec, asn1_void_vector_t)->data)
                           + j * spec->size;
                }

                RETHROW(asn1_unpack_value(ps, spec, mem_pool, depth,
                        st_ptr, copy));
            }
        }
        break;
    }
    return 0;
}

static int asn1_unpack_choice(pstream_t *ps, const asn1_desc_t *_desc,
                              mem_pool_t *mem_pool, int depth, void *st,
                              bool copy, bool indef_len)
{
    const asn1_choice_desc_t *desc =
        container_of(_desc, asn1_choice_desc_t, desc);
    const asn1_field_t *spec;
    const asn1_field_t *selector_spec = &desc->desc.vec.tab[0];
    int choice;
    uint8_t tag;

    if (ps_done(ps)) {
        e_trace(1, "no choice element: input stream end");
        return -1;
    }

    tag = *ps->b;

    if (!(choice = asn1_find_choice(desc, tag))) {
        e_trace(1, "no choice element: tag mismatch");
        return -1;
    }

    spec = &desc->desc.vec.tab[choice];
    __asn1_set_int(st, selector_spec, choice);
    e_trace_desc(1, "unpacking", &desc->desc, choice, depth);
    RETHROW(asn1_unpack_field(ps, spec, mem_pool, depth, st, copy,
                              indef_len));

    return 0;
}

/* ----- UNTAGGED CHOICE UNPACKER - {{{ - */
static int
asn1_unpack_u_choice_val(pstream_t *ps, const asn1_field_t *choice_spec,
                         mem_pool_t *mem_pool, int depth, void *st,
                         bool copy)
{
    int choice;
    const asn1_choice_desc_t *choice_desc =
        container_of(choice_spec->u.comp, asn1_choice_desc_t, desc);
    const qv_t(asn1_field) *vec = &choice_desc->desc.vec;
    const asn1_field_t *selector_spec = &vec->tab[0];

    if (ps_done(ps)
    ||  !(choice = asn1_find_choice(choice_desc, *ps->b)))
    {
        if (choice_spec->mode == ASN1_OBJ_MODE(MANDATORY)) {
            e_trace(1, "missing mandatory choice %s", choice_spec->name);
            return -1;
        } else {
            if (ps_done(ps)) {
                e_trace(2, "end of stream");
            }  else {
                e_trace(2, "nothing found for tag %2x", *ps->b);
            }
            return 0;
        }
    }

    {
        const asn1_field_t *field = &vec->tab[choice];
        void  *choice_st;

        choice_st = asn1_alloc_if_pointed(choice_spec, mem_pool, st);
        __asn1_set_int(choice_st, selector_spec, choice);

        e_trace_desc(1, "unpacking", &choice_desc->desc, choice, depth + 1);

        if (field->type == ASN1_OBJ_TYPE(UNTAGGED_CHOICE)) {
            RETHROW(asn1_unpack_u_choice_val(ps, field, mem_pool, depth + 1,
                                             choice_st, copy));
        } else {
            void *value =
                asn1_alloc_if_pointed(field, mem_pool, choice_st);

            RETHROW(asn1_unpack_value(ps, field, mem_pool, depth + 1,
                                      value, copy));
        }
    }

    return 1;
}


static int
asn1_unpack_seq_of_u_choice(pstream_t *ps, const asn1_field_t *choice_spec,
                            mem_pool_t *mem_pool, int depth, void *st,
                            bool copy)
{
    int len;
    pstream_t temp_ps =  *ps;
    const asn1_choice_desc_t *choice_desc =
        container_of(choice_spec->u.comp, asn1_choice_desc_t, desc);

    for (len = 0;
         !ps_done(&temp_ps)
      && asn1_find_choice(choice_desc, ps_peekc(temp_ps));
         len++)
    {
        if (asn1_get_ber_field(&temp_ps, false, NULL) < 0) {
            e_trace(1, "invalid BER content in SEQUENCE OF untagged choice");
            return -1;
        }
    }

    if (unlikely(!len)) {
        p_clear(GET_PTR(st, choice_spec, asn1_void_vector_t), 1);
        return 0;
    }

    asn1_alloc_seq_of(st, len, choice_spec, mem_pool);

    for (int i = 0; i < len; i++) {
        int choice = asn1_find_choice(choice_desc, *ps->b);
        const asn1_field_t *spec = &choice_desc->desc.vec.tab[choice];
        void *choice_st;
        if (choice_spec->pointed) {
            choice_st = GET_PTR(st, choice_spec, asn1_void_array_t)->data[i];
        } else {
            choice_st =
                (char *)(GET_PTR(st, choice_spec, asn1_void_vector_t)->data)
              + i * choice_spec->size;
        }

        *(int *)choice_st = choice;
        e_trace_desc(1, "unpacking", &choice_desc->desc, choice, depth + 1);
        RETHROW(asn1_unpack_value(ps, spec, mem_pool, depth,
                                  GET_PTR(choice_st, spec, void *),
                                  copy));
    }

    return 0;
}

static int
asn1_unpack_untagged_choice(pstream_t *ps, const asn1_field_t *choice_spec,
                            mem_pool_t *mem_pool, int depth, void *st,
                            bool copy)
{
    switch (choice_spec->mode) {
      case ASN1_OBJ_MODE(MANDATORY):
        if (!RETHROW(asn1_unpack_u_choice_val(ps, choice_spec, mem_pool,
                                              depth, st, copy)))
        {
            e_trace(1, "mandatory untagged choice absent");
            return -1;
        }
        break;
      case ASN1_OBJ_MODE(OPTIONAL):
        if (!RETHROW(asn1_unpack_u_choice_val(ps, choice_spec, mem_pool,
                                              depth, st, copy)))
        {
            *GET_PTR(st, choice_spec, void *) = NULL;
        }
        break;
      case ASN1_OBJ_MODE(SEQ_OF):
        RETHROW(asn1_unpack_seq_of_u_choice(ps, choice_spec, mem_pool,
                                            depth, st, copy));
        break;
    }

    return 0;
}
/* }}} */

static int asn1_unpack_sequence(pstream_t *ps, const asn1_desc_t *desc,
                                mem_pool_t *mem_pool, int depth, void *st,
                                bool copy, bool indef_len)
{
    for (int i = 0; i < desc->vec.len; i++) {
        const asn1_field_t *spec = &desc->vec.tab[i];

        e_trace_desc(1, "unpacking", desc, i, depth);

        assert (spec->tag_len == 1);

        if (unlikely(spec->type == ASN1_OBJ_TYPE(UNTAGGED_CHOICE))) {
            RETHROW(asn1_unpack_untagged_choice(ps, spec, mem_pool, depth,
                                                st, copy));
        } else {
            RETHROW(asn1_unpack_field(ps, spec, mem_pool, depth, st, copy,
                                      indef_len));
        }
    }

    return 0;
}

static int asn1_unpack_rec(pstream_t *ps, const asn1_desc_t *desc,
                           mem_pool_t *mem_pool, int depth, void *st,
                           bool copy, bool indef_len)
{
    switch (desc->type) {
      case ASN1_CSTD_TYPE_SEQUENCE:
        RETHROW(asn1_unpack_sequence(ps, desc, mem_pool, depth, st, copy,
                                     indef_len));
        break;
      case ASN1_CSTD_TYPE_CHOICE:
        RETHROW(asn1_unpack_choice(ps, desc, mem_pool, depth, st, copy,
                                   indef_len));
        break;
      case ASN1_CSTD_TYPE_SET:
        e_panic("not supported yet");
        break;
    }

    return 0;
}

/** \brief Unpack a given payload following an asn1 description.
 *  \param[inout] Input pstream.
 *  \param[in]    ASN.1 description of output structure.
 *  \param[in]    Mem pool for unallocated content.
 *  \param[out]   Pointer on output struct.
 */
int asn1_unpack_(pstream_t *ps, const asn1_desc_t *desc,
                 mem_pool_t *mem_pool, void *st, bool copy)
{
    return asn1_unpack_rec(ps, desc, mem_pool, 0, st, copy, false);
}
/*  */

Z_GROUP_EXPORT(asn1_packer)
{
#define T(pfx, v, exp, txt) \
    ({  Z_ASSERT_EQ(asn1_pack_##pfx(buf, v) - buf, ssizeof(exp), txt);    \
        Z_ASSERT_EQUAL(buf, asn1_##pfx##_size(v), exp, sizeof(exp), txt); })

    Z_TEST(i64, "asn1: int64 packer") {
        uint8_t buf[BUFSIZ];

        int64_t i1     = 0xffffffffffffffffLL;
        uint8_t exp1[] = { 0xff };
        int64_t i2     = 0xffffffffffffffLL;
        uint8_t exp2[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        int64_t i3     = 0x8000000000000000LL;
        uint8_t exp3[] = { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

        T(int64, i1, exp1, "-1");
        T(int64, i2, exp2, "2^56 - 1");
        T(int64, i3, exp3, "-0");
    } Z_TEST_END;

    Z_TEST(i32, "asn1: int32 packer") {
        uint8_t buf[BUFSIZ];

        int32_t i1     = 255;
        uint8_t exp1[] = { 0x00, 0xff };
        int32_t i2     = -255;
        uint8_t exp2[] = { 0xff, 0x01 };
        int32_t i3     = (1 << 16) - 1;
        uint8_t exp3[] = { 0x00, 0xff, 0xff };
        int32_t i4     = 0xffffffff;
        uint8_t exp4[] = { 0xff };

        T(int32, i1, exp1, "255");
        T(int32, i2, exp2, "-255");
        T(int32, i3, exp3, "2^16 - 1");
        T(int32, i4, exp4, "-1");
    } Z_TEST_END;

    Z_TEST(u32, "asn1: uint32 packer") {
        uint8_t buf[BUFSIZ];

        uint32_t u1     = 255;
        uint8_t  exp1[] = { 0x00, 0xff };
        uint32_t u2     = 256;
        uint8_t  exp2[] = { 0x01, 0x00 };
        uint32_t u3     = (1 << 16) - 1;
        uint8_t  exp3[] = { 0x00, 0xff, 0xff };
        uint32_t u4     = 0xffffffff;
        uint8_t  exp4[] = { 0x00, 0xff, 0xff, 0xff, 0xff };

        T(uint32, u1, exp1, "255");
        T(uint32, u2, exp2, "256");
        T(uint32, u3, exp3, "2^16 - 1");
        T(uint32, u4, exp4, "MAX UINT32");
    } Z_TEST_END;

    Z_TEST(u64, "asn1: uint64 packer") {
        uint8_t buf[BUFSIZ];

        uint64_t u1     = 0xffffffffffffffffULL;
        uint8_t  exp1[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        uint64_t u2     = 0xffffffffffffffULL;
        uint8_t  exp2[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
        uint64_t u3     = 0x8000000000000000ULL;
        uint8_t  exp3[] = { 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

        T(uint64, u1, exp1, "MAX UINT64");
        T(uint64, u2, exp2, "2^56 - 1");
        T(uint64, u3, exp3, "2^63");
    } Z_TEST_END;

#undef T

    Z_TEST(len, "asn1: len packer") {
        uint8_t buf[BUFSIZ];

        int32_t const l1     = 127;
        uint8_t exp1[] = { 0x7f };
        int32_t const l2     = 128;
        uint8_t exp2[] = { 0x81, 0x80 };

        Z_ASSERT_EQUAL(buf, asn1_pack_len(buf, l1) - buf, exp1, ssizeof(exp1));
        Z_ASSERT_EQUAL(buf, asn1_pack_len(buf, l2) - buf, exp2, ssizeof(exp2));
    } Z_TEST_END;
} Z_GROUP_END;

