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

#ifndef IS_LIB_COMMON_IOP_INTERNALS_H
#define IS_LIB_COMMON_IOP_INTERNALS_H

typedef enum iop_repeat_t {
    IOP_R_REQUIRED,
    IOP_R_DEFVAL,
    IOP_R_OPTIONAL,
    IOP_R_REPEATED,
} iop_repeat_t;

typedef enum iop_type_t {
    IOP_T_I8,
    IOP_T_U8,
    IOP_T_I16,
    IOP_T_U16,
    IOP_T_I32,
    IOP_T_U32,
    IOP_T_I64,
    IOP_T_U64,
    IOP_T_BOOL,
    IOP_T_ENUM,
    IOP_T_DOUBLE,
    IOP_T_STRING,
    IOP_T_DATA,
    IOP_T_UNION,
    IOP_T_STRUCT,
    IOP_T_XML,
    IOP_T_VOID,
} iop_type_t;
#define IOP_T_max  IOP_T_VOID

/*{{{ iop_field_t */

typedef struct iop_struct_t iop_struct_t;
typedef struct iop_enum_t   iop_enum_t;

enum iop_field_flags_t {
    IOP_FIELD_CHECK_CONSTRAINTS,    /**< check_constraints function exists  */
    IOP_FIELD_NO_EMPTY_ARRAY,       /**< indicates presence of @minOccurs   */
    IOP_FIELD_IS_REFERENCE,         /**< field points to the value          */
    IOP_FIELD_HAS_SNMP_INFO,
    IOP_FIELD_IS_SNMP_INDEX,        /**< indicates presence of @snmpIndex   */
};

/* XXX do not change the field structure because of backward
 * compatibility issues */
typedef struct iop_field_t {
    lstr_t       name;
    uint16_t     tag;
    unsigned     tag_len:  2; /**< 0 to 2                                   */
    unsigned     flags  : 14; /**< bitfield of iop_field_flags_t            */
    iop_repeat_t repeat : 16; /**< iop_repeat_t                             */
    iop_type_t   type   : 16; /**< iop_type_t                               */
    uint16_t     size;        /**< sizeof(type);                            */
    uint16_t     data_offs;   /**< offset to the data                       */
    /**
     *   unused for IOP_T_{U,I}{8,16,32,64}, IOP_T_DOUBLE
     *   unused for IOP_T_{UNION,STRUCT}
     *   defval_enum holds the default value for IOP_T_ENUM
     *   defval_len  holds the default value length for IOP_T_{XML,STRING,DATA}
     */
    union {
        int      defval_enum;
        int      defval_len;
    } u0;
    /**
     *   defval_u64  holds the default value for IOP_T_{U,I}{8,16,32,64}
     *   defval_d    holds the default value for IOP_T_DOUBLE
     *   defval_data holds the default value data for IOP_T_{XML,STRING,DATA}
     *   st_desc     holds a pointer to the struct desc for IOP_T_{STRUCT,UNION}
     *   en_desc     holds a pointer to the enum desc for IOP_T_ENUM
     */
    union {
        const void         * nonnull defval_data;
        uint64_t            defval_u64;
        double              defval_d;
        const iop_struct_t * nonnull st_desc;
        const iop_enum_t   * nonnull en_desc;
    } u1;
} iop_field_t;

#define IOP_FIELD(type_t, v, i)  (((type_t *)v)[i])

/*}}}*/
/*{{{ Generic attributes */

typedef struct iop_help_t {
    lstr_t brief;
    lstr_t details;
    lstr_t warning;
    /* the fields below can only be accessed if the associated type is
     * ...ATTR_HELP_V2 */
    lstr_t example;
    uint8_t version;
} iop_help_t;

typedef union iop_value_t {
    int64_t     i;
    int64_t     i64;
    int32_t     i32;
    uint64_t    u;
    uint64_t    u64;
    uint32_t    u32;
    double      d;
    lstr_t      s;
    bool        b;
    const void * nullable p;
    void       * nullable v;
} iop_value_t;

/* For each iop object, an enum *_attr_type_t is declared which contains the
 * supported types for a generic attribute:
 * _GEN_ATTR_S for strings (v.s)
 * _GEN_ATTR_I for integers (v.i64)
 * _GEN_ATTR_D for doubles (v.d)
 * _GEN_ATTR_O for json objects (v.s)
 */
typedef struct iop_generic_attr_arg_t {
    iop_value_t v;
} iop_generic_attr_arg_t;

/*}}}*/
/*{{{ iop_enum_t */

typedef enum iop_enum_value_attr_type_t {
    IOP_ENUM_VALUE_ATTR_HELP,
    IOP_ENUM_VALUE_GEN_ATTR_S,
    IOP_ENUM_VALUE_GEN_ATTR_I,
    IOP_ENUM_VALUE_GEN_ATTR_D,
    IOP_ENUM_VALUE_GEN_ATTR_O,
    IOP_ENUM_VALUE_ATTR_HELP_V2,
} iop_enum_value_attr_type_t;

typedef iop_generic_attr_arg_t iop_enum_value_attr_arg_t;

typedef struct iop_enum_value_attr_t {
    iop_enum_value_attr_type_t       type;
    const iop_enum_value_attr_arg_t * nonnull args;
} iop_enum_value_attr_t;

typedef struct iop_enum_value_attrs_t {
    unsigned                     flags; /**< bitfield of iop_enum_value_attr_type_t */
    uint16_t                     attrs_len;
    uint8_t                      version;   /**< version 0 */
    uint8_t                      padding;
    const iop_enum_value_attr_t * nonnull attrs;
} iop_enum_value_attrs_t;

typedef enum iop_enum_attr_type_t {
    IOP_ENUM_ATTR_HELP,
    IOP_ENUM_GEN_ATTR_S,
    IOP_ENUM_GEN_ATTR_I,
    IOP_ENUM_GEN_ATTR_D,
    IOP_ENUM_GEN_ATTR_O,
    IOP_ENUM_ATTR_HELP_V2,
} iop_enum_attr_type_t;

typedef iop_generic_attr_arg_t iop_enum_attr_arg_t;

typedef struct iop_enum_attr_t {
    iop_enum_attr_type_t       type;
    const iop_enum_attr_arg_t * nonnull args;
} iop_enum_attr_t;

typedef struct iop_enum_attrs_t {
    unsigned               flags; /**< bitfield of iop_enum_attr_type_t */
    uint16_t               attrs_len;
    uint8_t                version;   /**< version 0 */
    uint8_t                padding;
    const iop_enum_attr_t * nonnull attrs;
} iop_enum_attrs_t;

typedef struct iop_enum_alias_t {
    int    pos;
    lstr_t name;
} iop_enum_alias_t;

typedef struct iop_enum_aliases_t {
    uint16_t len;
    iop_enum_alias_t aliases[];
} iop_enum_aliases_t;

/*
 * .ranges helps finding tags into .fields.
 * ----------------------------------------
 *
 * it's a suite of [i_1, tag_1, ... i_k, tag_k, ... t_n, i_{n+1}]
 *
 * it says that at .fields[i_k] starts a contiguous range of tags from
 * value tag_k to tag_k + i_{k+1} - i_k.
 * Of course n == .ranges_len and i_{n+1} == .fields_len.
 *
 *
 * Example:
 * -------
 *
 * If you have the tags [1, 2, 3,  7,  9, 10, 11], the ranges will read:
 *                       --v----   v   ----v----
 *                      [0, 1,   3, 7,   4, 9,   7]
 *
 * Looking for a given tag needs a binary search the odd places of the
 * ranges array. Then, if you're tag T on the kth range, the index of your
 * description in .fields is:  (i_k + T - t_k)
 *
 */
struct iop_enum_t {
    const lstr_t                  name;
    const lstr_t                  fullname;
    const lstr_t                 * nonnull names;
    const int                    * nonnull values;
    const int                    * nonnull ranges;
    uint16_t                      enum_len;
    uint16_t                      flags; /**< bitfield of iop_enum_flags_t */
    int                           ranges_len;
    /* XXX do not dereference the following 2 members without checking
     * TST_BIT(this->flags, IOP_ENUM_EXTENDED) first */
    const iop_enum_attrs_t       * nullable en_attrs;
    const iop_enum_value_attrs_t * nullable values_attrs;
    /* XXX do not dereference the following member without checking
     * TST_BIT(this->flags, IOP_ENUM_ALIASES) first */
    const iop_enum_aliases_t     * nullable aliases;
};

enum iop_enum_flags_t {
    IOP_ENUM_EXTENDED,      /**< to access en_attrs and values_attrs */
    IOP_ENUM_STRICT,        /**< strict packing/unpacking of enum values */
    IOP_ENUM_ALIASES,       /**< aliases of enum values */
};

/*}}}*/
/*{{{ iop_struct_t */

typedef int (check_constraints_f)(const void * nonnull ptr, int n);

typedef iop_generic_attr_arg_t iop_field_attr_arg_t;

typedef enum iop_field_attr_type_t {
    IOP_FIELD_MIN_OCCURS,
    IOP_FIELD_MAX_OCCURS,
    IOP_FIELD_CDATA,
    IOP_FIELD_MIN,
    IOP_FIELD_MAX,
    IOP_FIELD_NON_EMPTY,
    IOP_FIELD_NON_ZERO,
    IOP_FIELD_MIN_LENGTH,
    IOP_FIELD_MAX_LENGTH,
    IOP_FIELD_PATTERN,
    IOP_FIELD_PRIVATE,
    IOP_FIELD_ATTR_HELP,
    IOP_FIELD_GEN_ATTR_S,
    IOP_FIELD_GEN_ATTR_I,
    IOP_FIELD_GEN_ATTR_D,
    IOP_FIELD_GEN_ATTR_O,
    IOP_FIELD_DEPRECATED,
    IOP_FIELD_SNMP_INFO, /**< not a real attribute, used in snmpObj         */
    IOP_FIELD_ATTR_HELP_V2,
} iop_field_attr_type_t;

typedef struct iop_field_attr_t {
    iop_field_attr_type_t        type;
    const iop_field_attr_arg_t  * nonnull args;
} iop_field_attr_t;

typedef struct iop_field_attrs_t {
    check_constraints_f     * nullable check_constraints;
    unsigned                 flags;  /**< bitfield of iop_field_attr_type_t */
    uint16_t                 attrs_len;
    uint8_t                  version;   /**< version 0 */
    uint8_t                  padding;
    const iop_field_attr_t  * nonnull attrs;
} iop_field_attrs_t;

typedef enum iop_struct_attr_type_t {
    IOP_STRUCT_ATTR_HELP,
    IOP_STRUCT_GEN_ATTR_S,
    IOP_STRUCT_GEN_ATTR_I,
    IOP_STRUCT_GEN_ATTR_D,
    IOP_STRUCT_GEN_ATTR_O,
    IOP_STRUCT_DEPRECATED,
    IOP_STRUCT_ATTR_HELP_V2,
} iop_struct_attr_type_t;

typedef iop_generic_attr_arg_t iop_struct_attr_arg_t;

typedef struct iop_struct_attr_t {
    iop_struct_attr_type_t       type;
    const iop_struct_attr_arg_t * nonnull args;
} iop_struct_attr_t;

typedef struct iop_struct_attrs_t {
    unsigned                 flags; /**< bitfield of iop_struct_attr_type_t */
    uint16_t                 attrs_len;
    uint8_t                  version;   /**< version 0 */
    uint8_t                  padding;
    const iop_struct_attr_t * nonnull attrs;
} iop_struct_attrs_t;

typedef struct iop_static_field_t {
    lstr_t                   name;
    iop_value_t              value;
    const iop_field_attrs_t * nullable attrs; /**< NULL if there are none */
    uint16_t                 type;
} iop_static_field_t;

/* Class attributes */
typedef struct iop_class_attrs_t {
    /** NULL for "master" classes       */
    const iop_struct_t        * nullable parent;
    /** NULL if there are none   */
    const iop_static_field_t * nonnull * nullable static_fields;
    uint8_t                    static_fields_len;
    uint8_t                    is_abstract : 1;
    uint8_t                    is_private  : 1;
    uint8_t                    padding     : 6;
    uint16_t                   class_id;
} iop_class_attrs_t;

/* Snmp attributes */
typedef struct iop_snmp_attrs_t {
    const iop_struct_t * nullable parent; /**< NULL if parent is Intersec   */
    uint16_t            oid;
    uint16_t            type;   /**< iop_type_t                             */
} iop_snmp_attrs_t;

struct iop_struct_t {
    const lstr_t        fullname;
    const iop_field_t  * nonnull fields;
    const int          * nonnull ranges;
    uint16_t            ranges_len;
    uint16_t            fields_len;
    uint16_t            size;           /**< sizeof(type);                  */
    unsigned            flags    : 15;  /**< bitfield of iop_struct_flags_t */
    unsigned            is_union :  1;  /**< struct or union ?              */
    /* XXX do not dereference the following members without checking
     * TST_BIT(this->flags, IOP_STRUCT_EXTENDED) first */
    const iop_struct_attrs_t * nullable st_attrs;
    const iop_field_attrs_t  * nullable fields_attrs;
    union {
        /* XXX do not dereference the following members without checking
         * iop_struct_is_class(this) first */
        const iop_class_attrs_t * nullable class_attrs;
        /* XXX do not dereference the following members without checking
         * iop_struct_is_snmp_obj(this) first */
        const iop_snmp_attrs_t * nullable snmp_attrs;
    };
};

enum iop_struct_flags_t {
    IOP_STRUCT_EXTENDED,        /**< st_attrs and field_attrs exist */
    IOP_STRUCT_HAS_CONSTRAINTS, /**< will iop_check_constraints do smth? */
    IOP_STRUCT_IS_CLASS,        /**< is it a class? */
    IOP_STRUCT_STATIC_HAS_TYPE, /**< in class mode, does iop_static_field_t
                                 * have a type field? */
    IOP_STRUCT_IS_SNMP_OBJ,     /**< is it a snmpObj? */
    IOP_STRUCT_IS_SNMP_TBL,     /**< is it a snmpTbl? */
    IOP_STRUCT_IS_SNMP_PARAM,   /**< does it have @snmpParam? */
};

/*}}}*/
/*{{{ iop_rpc_t */

enum iop_rpc_flags_t {
    IOP_RPC_IS_ALIAS,
    IOP_RPC_HAS_ALIAS,
};

typedef iop_field_attr_arg_t iop_rpc_attr_arg_t;

typedef enum iop_rpc_attr_type_t {
    IOP_RPC_ALIAS,
    IOP_RPC_ATTR_HELP,
    IOP_RPC_ATTR_ARG_HELP,
    IOP_RPC_ATTR_RES_HELP,
    IOP_RPC_ATTR_EXN_HELP,
    IOP_RPC_GEN_ATTR_S,
    IOP_RPC_GEN_ATTR_I,
    IOP_RPC_GEN_ATTR_D,
    IOP_RPC_GEN_ATTR_O,
    IOP_RPC_ATTR_HELP_V2,
    IOP_RPC_ATTR_ARG_HELP_V2,
    IOP_RPC_ATTR_RES_HELP_V2,
    IOP_RPC_ATTR_EXN_HELP_V2,
} iop_rpc_attr_type_t;

typedef struct iop_rpc_attr_t {
    iop_rpc_attr_type_t type;
    const iop_rpc_attr_arg_t * nonnull args;
} iop_rpc_attr_t;

typedef struct iop_rpc_attrs_t {
    unsigned                 flags;  /**< bitfield of iop_rpc_attr_type_t */
    uint16_t                 attrs_len;
    uint8_t                  version;   /**< version 0 */
    uint8_t                  padding;
    const iop_rpc_attr_t    * nonnull attrs;
} iop_rpc_attrs_t;

typedef struct iop_rpc_t {
    const lstr_t        name;
    const iop_struct_t * nullable args;
    const iop_struct_t * nullable result;
    const iop_struct_t * nullable exn;
    uint32_t            tag;
    unsigned            async : 1;
    unsigned            flags : 31; /**< bitfield of iop_rpc_flags_t */
} iop_rpc_t;

/*}}}*/
/*{{{ iop_iface_t */

typedef enum iop_iface_attr_type_t {
    IOP_IFACE_ATTR_HELP,
    IOP_IFACE_GEN_ATTR_S,
    IOP_IFACE_GEN_ATTR_I,
    IOP_IFACE_GEN_ATTR_D,
    IOP_IFACE_GEN_ATTR_O,
    IOP_IFACE_DEPRECATED,
    IOP_IFACE_ATTR_HELP_V2,
} iop_iface_attr_type_t;

typedef iop_generic_attr_arg_t iop_iface_attr_arg_t;

typedef struct iop_iface_attr_t {
    iop_iface_attr_type_t       type;
    const iop_iface_attr_arg_t * nonnull args;
} iop_iface_attr_t;

typedef struct iop_iface_attrs_t {
    unsigned                flags; /**< bitfield of iop_iface_attr_type_t */
    uint16_t                attrs_len;
    uint8_t                 version;   /**< version 0 */
    uint8_t                 padding;
    const iop_iface_attr_t * nonnull attrs;
} iop_iface_attrs_t;

typedef struct iop_iface_t {
    const lstr_t             fullname;
    const iop_rpc_t         * nonnull funs;
    uint16_t                 funs_len;
    uint16_t                 flags; /**< bitfield of iop_iface_flags_t */
    const iop_rpc_attrs_t   * nullable rpc_attrs;
    /** check TST_BIT(flags, IOP_IFACE_HAS_ATTRS)
     *  before accessing iface_attrs */
    const iop_iface_attrs_t * nullable iface_attrs;
    /** check TST_BIT(flags, IOP_IFACE_IS_SNMP_IFACE)
     *  before accessing iface_attrs */
    const iop_snmp_attrs_t  * nullable snmp_iface_attrs;
} iop_iface_t;

enum iop_iface_flags_t {
    IOP_IFACE_EXTENDED,
    IOP_IFACE_HAS_ATTRS,
    IOP_IFACE_IS_SNMP_IFACE,
};

/*}}}*/
/*{{{ iop_mod_t */

typedef struct iop_iface_alias_t {
    const iop_iface_t  * nonnull iface;
    const lstr_t        name;
    uint32_t            tag;
} iop_iface_alias_t;

typedef enum iop_mod_iface_attr_type_t {
    IOP_MOD_IFACE_ATTR_HELP,
    IOP_MOD_IFACE_ATTR_HELP_V2,
} iop_mod_iface_attr_type_t;

typedef iop_generic_attr_arg_t iop_mod_iface_attr_arg_t;

typedef struct iop_mod_iface_attr_t {
    iop_mod_iface_attr_type_t       type;
    const iop_mod_iface_attr_arg_t * nonnull args;
} iop_mod_iface_attr_t;

typedef struct iop_mod_iface_attrs_t {
    unsigned                flags; /**< bitfield of iop_mod_iface_attr_type_t */
    uint16_t                attrs_len;
    uint8_t                 version;   /**< version 0 */
    uint8_t                 padding;
    const iop_mod_iface_attr_t * nonnull attrs;
} iop_mod_iface_attrs_t;

typedef enum iop_mod_attr_type_t {
    IOP_MOD_ATTR_HELP,
    IOP_MOD_ATTR_HELP_V2,
} iop_mod_attr_type_t;

typedef iop_generic_attr_arg_t iop_mod_attr_arg_t;

typedef struct iop_mod_attr_t {
    iop_mod_attr_type_t       type;
    const iop_mod_attr_arg_t * nonnull args;
} iop_mod_attr_t;

typedef struct iop_mod_attrs_t {
    unsigned              flags;     /**< bitfield of iop_mod_attr_type_t */
    uint16_t              attrs_len;
    uint8_t               version;   /**< version 0 */
    uint8_t               padding;
    const iop_mod_attr_t * nonnull attrs;
} iop_mod_attrs_t;

enum iop_mod_flags_t {
    IOP_MOD_EXTENDED,
};

typedef struct iop_mod_t {
    const lstr_t fullname;
    const iop_iface_alias_t * nullable ifaces;
    uint16_t ifaces_len;
    uint16_t flags; /**< bitfield of iop_mod_flags_t */
    /** check TST_BIT(flags, IOP_MOD_EXTENDED)
     *  before accessing mod_attrs and ifaces_attrs */
    const iop_mod_attrs_t       * nullable mod_attrs;
    const iop_mod_iface_attrs_t * nullable ifaces_attrs;
} iop_mod_t;

/*}}}*/
/*{{{ iop_pkg_t */

typedef struct iop_pkg_t iop_pkg_t;
struct iop_pkg_t {
    const lstr_t               name;
    iop_enum_t   const *const nullable *nonnull enums;
    iop_struct_t const *const nullable *nonnull structs;
    iop_iface_t  const *const nullable *nonnull ifaces;
    iop_mod_t    const *const nullable *nonnull mods;
    iop_pkg_t    const *const nullable *nonnull deps;
};

/*}}}*/
/*{{{ iop_array */

#define IOP_ARRAY_OF(type_t)                     \
    struct {                                     \
        type_t * nullable tab;                   \
        int32_t len;                             \
        unsigned flags;                          \
    }
typedef IOP_ARRAY_OF(int8_t) iop_array_i8_t;
typedef IOP_ARRAY_OF(uint8_t) iop_array_u8_t;
typedef IOP_ARRAY_OF(int16_t) iop_array_i16_t;
typedef IOP_ARRAY_OF(uint16_t) iop_array_u16_t;
typedef IOP_ARRAY_OF(int32_t) iop_array_i32_t;
typedef IOP_ARRAY_OF(uint32_t) iop_array_u32_t;
typedef IOP_ARRAY_OF(int64_t) iop_array_i64_t;
typedef IOP_ARRAY_OF(uint64_t) iop_array_u64_t;
typedef IOP_ARRAY_OF(bool) iop_array_bool_t;
typedef iop_array_bool_t iop_array__Bool_t;
typedef IOP_ARRAY_OF(double) iop_array_double_t;
typedef IOP_ARRAY_OF(lstr_t) iop_array_lstr_t;

typedef iop_array_i8_t i8__array_t;
typedef iop_array_u8_t u8__array_t;
typedef iop_array_i16_t i16__array_t;
typedef iop_array_u16_t u16__array_t;
typedef iop_array_i32_t i32__array_t;
typedef iop_array_u32_t u32__array_t;
typedef iop_array_i64_t i64__array_t;
typedef iop_array_u64_t u64__array_t;
typedef iop_array_bool_t bool__array_t;
typedef iop_array_double_t double__array_t;
typedef iop_array_lstr_t lstr__array_t;

/*}}}*/
/*{{{ iop__void__t */

typedef struct iop__void__t { } iop__void__t;
EXPORT iop_struct_t const iop__void__s;
EXPORT iop_struct_t const * const nonnull iop__void__sp;

/*}}}*/
/*{{{ IOP constraints */

__attr_printf__(1, 2)
int         iop_set_err(const char * nonnull fmt, ...) __cold;
__attr_printf__(1, 0)
void        iop_set_verr(const char * nonnull fmt, va_list ap) __cold ;
int         iop_set_err2(const lstr_t * nonnull s) __cold;
void        iop_clear_err(void);

/*}}}*/
/*{{{ IOP DSO */

typedef struct iop_dso_vt_t {
    size_t  vt_size;
    __attr_printf__(1, 0)
    void  (*nullable iop_set_verr)(const char * nonnull fmt, va_list ap);
} iop_dso_vt_t;

#define IOP_EXPORT_PACKAGES(...) \
    EXPORT iop_pkg_t const * nullable const iop_packages[];   \
    iop_pkg_t const * const iop_packages[] = { __VA_ARGS__, NULL }

#define IOP_USE_EXTERNAL_PACKAGES \
    EXPORT bool iop_use_external_packages;                                   \
    bool iop_use_external_packages = true;                                   \
    EXPORT bool iop_dont_replace_fix_pkg;                                    \
    bool iop_dont_replace_fix_pkg = true;

#define IOP_EXPORT_PACKAGES_VTABLE \
    EXPORT iop_dso_vt_t iop_vtable;                                     \
    iop_dso_vt_t iop_vtable = {                                         \
        .vt_size = sizeof(iop_dso_vt_t),                                \
        .iop_set_verr = NULL,                                           \
    };

#define IOP_EXPORT_PACKAGES_COMMON \
    IOP_EXPORT_PACKAGES_VTABLE                                          \
    iop_struct_t const iop__void__s = {                                 \
        .fullname   = LSTR_IMMED("Void"),                               \
        .fields_len = 0,                                                \
        .size       = 0,                                                \
    };                                                                  \
    iop_struct_t const * const iop__void__sp = &iop__void__s;           \
                                                                        \
    __attr_printf__(1, 2)                                               \
    int iop_set_err(const char * nonnull fmt, ...) {                    \
        va_list ap;                                                     \
                                                                        \
        va_start(ap, fmt);                                              \
        if (NULL == iop_vtable.iop_set_verr) {                          \
            fputs("iop_vtable.iop_set_verr not defined", stderr);       \
            exit(1);                                                    \
        }                                                               \
        (iop_vtable.iop_set_verr)(fmt, ap);                             \
        va_end(ap);                                                     \
        return -1;                                                      \
    }                                                                   \

#define iop_dso_ressource_t(category)  iop_dso_ressource_##category##_t

#define IOP_DSO_DECLARE_RESSOURCE_CATEGORY(category, type)  \
    typedef type iop_dso_ressource_t(category)

#define IOP_DSO_EXPORT_RESSOURCES(category, ...)                \
    EXPORT const iop_dso_ressource_t(category) * nullable const \
        iop_dso_ressources_##category[];                        \
    const iop_dso_ressource_t(category) *const                  \
        iop_dso_ressources_##category[] = { __VA_ARGS__, NULL }

/*}}}*/
/* {{{ IOP array initializers (repeated fields) */

/** Type of the IOP array defined for the given IOP type */
#define IOP_ARRAY_T(type)  type##__array_t

/** Initialize a repeated field */
#define IOP_ARRAY(_data, _len)  { .tab = (_data), .len = (_len), .flags = 0 }

/** Initialize a typed repeated field */
#define IOP_TYPED_ARRAY(_iop_type, _data, _len)                              \
    (IOP_ARRAY_T(_iop_type))IOP_ARRAY(_data, _len)

/** Initialize an empty repeated field */
#define IOP_ARRAY_EMPTY         IOP_ARRAY(NULL, 0)

/** Initialize a repeated field from a qvector */
#define IOP_ARRAY_TAB(vec)      IOP_ARRAY((vec)->tab, (vec)->len)

/** Initialize a typed repeated field from a qvector */
#define IOP_TYPED_ARRAY_TAB(_iop_type, vec)                                  \
    (IOP_ARRAY_T(_iop_type))IOP_ARRAY_TAB(vec)

/** Duplicate a repeated field (the array is copied shallowly) */
#define IOP_ARRAY_DUP(mp, array)                                             \
    ({                                                                       \
        typeof(array) _dup_array = (array);                                  \
                                                                             \
        _dup_array.tab = mp_dup(mp, _dup_array.tab, _dup_array.len);         \
        _dup_array;                                                          \
    })

#define T_IOP_ARRAY_DUP(array)  IOP_ARRAY_DUP(t_pool(), (array))

#define IOP_ARRAY_ELEM_TYPE(_iop_type)                                       \
    typeof(*cast(IOP_ARRAY_T(_iop_type) *, 0)->tab)


#define _IOP_ARRAY_NEW_ALLOC_MP(_mp, _new_fun, _iop_type, _len)              \
    ({                                                                       \
        typeof(_len) __len = _len;                                           \
                                                                             \
        IOP_TYPED_ARRAY(_iop_type,                                           \
                        _new_fun(_mp, IOP_ARRAY_ELEM_TYPE(_iop_type),        \
                                 __len,                                      \
                                 alignof(IOP_ARRAY_ELEM_TYPE(_iop_type))),   \
                        __len);                                              \
    })

#define MP_IOP_ARRAY_NEW(_mp, _iop_type, _len)                               \
    _IOP_ARRAY_NEW_ALLOC_MP(_mp, mpa_new, _iop_type, _len)

#define MP_IOP_ARRAY_NEW_RAW(_mp, _iop_type, _len)                           \
    _IOP_ARRAY_NEW_ALLOC_MP(_mp, mpa_new_raw, _iop_type, _len)

#define T_IOP_ARRAY_NEW(_iop_type, _len)                                     \
    MP_IOP_ARRAY_NEW(t_pool(), _iop_type, _len)

#define T_IOP_ARRAY_NEW_RAW(_iop_type, _len)                                 \
    MP_IOP_ARRAY_NEW_RAW(t_pool(), _iop_type, _len)

#define IOP_ARRAY_NEW(_iop_type, _len)                                       \
    MP_IOP_ARRAY_NEW(NULL, _iop_type, _len)

#define IOP_ARRAY_NEW_RAW(_iop_type, _len)                                   \
    MP_IOP_ARRAY_NEW_RAW(NULL, _iop_type, _len)

#define MP_IOP_ARRAY(_mp, _iop_type, ...)                                    \
    ({                                                                       \
        IOP_ARRAY_ELEM_TYPE(_iop_type) __carray[] = { __VA_ARGS__ };         \
        IOP_ARRAY_T(_iop_type) __array;                                      \
                                                                             \
        __array = MP_IOP_ARRAY_NEW_RAW(_mp, _iop_type, countof(__carray));   \
        p_copy(__array.tab, __carray, countof(__carray));                    \
                                                                             \
        __array;                                                             \
    })

#define T_IOP_ARRAY(_iop_type, ...)                                          \
    MP_IOP_ARRAY(t_pool(), _iop_type, __VA_ARGS__)

/* }}} */

#endif
