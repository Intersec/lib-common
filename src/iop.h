/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#ifndef IS_LIB_COMMON_IOP_H
#define IS_LIB_COMMON_IOP_H

#include <lib-common/container-qhash.h>
#include <lib-common/container-qvector.h>

#if __has_feature(nullability)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wnullability-completeness"
#if __has_warning("-Wnullability-completeness-on-arrays")
#pragma GCC diagnostic ignored "-Wnullability-completeness-on-arrays"
#endif
#endif

#include "iop/cfolder.h"
#include "iop/internals.h"

#define IOP_ABI_VERSION  2

typedef enum iop_wire_type_t {
    IOP_WIRE_BLK1,
    IOP_WIRE_BLK2,
    IOP_WIRE_BLK4,
    IOP_WIRE_QUAD,
    IOP_WIRE_INT1,
    IOP_WIRE_INT2,
    IOP_WIRE_INT4,
    IOP_WIRE_REPEAT,
} iop_wire_type_t;

/* {{{ IOP various useful typedefs and functions */

qvector_t(iop_struct, const iop_struct_t * nonnull);

/** Convert an IOP identifier from CamelCase naming to C underscored naming */
lstr_t t_camelcase_to_c(lstr_t name);

const char *nonnull t_camelcase_to_c_str(const char *nonnull name);

/** Convert an IOP type name (pkg.CamelCase) to C underscored naming */
lstr_t t_iop_type_to_c(lstr_t fullname);

/** Returns the maximum/minimum possible value of an iop_type_t */
iop_value_t iop_type_to_max(iop_type_t type);
iop_value_t iop_type_to_min(iop_type_t type);

/** Convert an identifier from C underscored naming to CamelCase naming.
 *
 * \param[in] name The name to convert.
 * \param[in] is_first_upper Specify wether the output should start with an
 *                           upper case character, otherwise a lower case
 *                           character is emitted.
 * \param[out] out The string receiving the camelCase/CamelCase result.
 * \return 0 in case of success -1 in case of error.
 */
int c_to_camelcase(lstr_t name, bool is_first_upper, sb_t * nonnull out);

/** Same as \ref c_to_camelcase but returns a string.
 *
 * \return LSTR_NULL_V in case of error.
 */
lstr_t t_c_to_camelcase(lstr_t s, bool is_first_upper);

/* }}} */
/* {{{ IOP attributes and constraints */

/** Checks the constraints associated to a given field.
 *
 * \param[in] desc     Struct descriptor.
 * \param[in] fdesc    Field descriptor.
 * \param[in] values   Pointer on the field value to check.
 * \param[in] len      Number of values to check (the field can be repeated).
 * \param[in] recurse  If set, subfields should be tested too (only apply to
 *                     struct/class/union fields).
 *
 * \note The values for parameters \ref values and \ref len can easily be
 *       obtained with function \ref iop_get_field_values.
 *
 * \return -1 in case of constraints violation in the field, in that case, the
 *         error message can be retrieved with \ref iop_get_err.
 */
int iop_field_check_constraints(const iop_struct_t * nonnull desc,
                                const iop_field_t * nonnull fdesc,
                                const void * nonnull values, int len,
                                bool recurse);

static inline
const iop_field_attrs_t * nullable
iop_field_get_attrs(const iop_struct_t * nonnull desc,
                    const iop_field_t * nonnull fdesc)
{
    unsigned desc_flags = desc->flags;

    if (TST_BIT(&desc_flags, IOP_STRUCT_EXTENDED) && desc->fields_attrs) {
        const iop_field_attrs_t *attrs;

        attrs = &desc->fields_attrs[fdesc - desc->fields];
        assert (attrs);

        return attrs;
    }
    return NULL;
}

static inline
const iop_rpc_attrs_t * nullable
iop_rpc_get_attrs(const iop_iface_t * nonnull desc,
                  const iop_rpc_t * nonnull fdesc)
{
    unsigned desc_flags = desc->flags;

    if (TST_BIT(&desc_flags, IOP_IFACE_EXTENDED) && desc->rpc_attrs) {
        const iop_rpc_attrs_t *attrs;

        attrs = &desc->rpc_attrs[fdesc - desc->funs];
        return attrs;
    }
    return NULL;
}

/** Find a generic attribute value for an IOP interface.
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  iface The IOP interface definition (__if).
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_iface_get_gen_attr(const iop_iface_t * nonnull iface, lstr_t key,
                           iop_type_t exp_type,
                           iop_type_t * nullable val_type,
                           iop_value_t * nonnull value);

/** Find a generic attribute value for an IOP rpc.
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  iface The IOP interface definition (__if).
 * \param[in]  rpc   The IOP rpc definition (__rpc).
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_rpc_get_gen_attr(const iop_iface_t * nonnull iface,
                         const iop_rpc_t * nonnull rpc,
                         lstr_t key, iop_type_t exp_type,
                         iop_type_t * nullable val_type,
                         iop_value_t * nonnull value);

static inline check_constraints_f * nullable
iop_field_get_constraints_cb(const iop_struct_t * nonnull desc,
                             const iop_field_t * nonnull fdesc)
{
    unsigned fdesc_flags = fdesc->flags;

    if (TST_BIT(&fdesc_flags, IOP_FIELD_CHECK_CONSTRAINTS)) {
        const iop_field_attrs_t *attrs = iop_field_get_attrs(desc, fdesc);

        return attrs->check_constraints;
    }
    return NULL;
}

static inline
bool iop_field_has_constraints(const iop_struct_t * nonnull desc,
                               const iop_field_t * nonnull fdesc)
{
    if (iop_field_get_constraints_cb(desc, fdesc))
        return true;
    if (fdesc->type == IOP_T_ENUM && fdesc->u1.en_desc->flags)
        return true;
    return false;
}

/*}}}*/
/* {{{ IOP introspection */

const iop_iface_t * nullable
iop_mod_find_iface(const iop_mod_t * nonnull mod, uint32_t tag);
const iop_rpc_t * nullable
iop_iface_find_rpc(const iop_iface_t * nonnull iface, uint32_t tag);
const iop_rpc_t * nullable
iop_mod_find_rpc(const iop_mod_t * nonnull mod, uint32_t cmd);

/** Get the string description of an iop type.
 *
 * \param[in]  type iop type
 *
 * \return the string description.
 */
const char * nonnull iop_type_get_string_desc(iop_type_t type);

/** Return whether the IOP type is scalar or not.
 *
 * A scalar type is a type that holds one and only one value (no structure,
 * class or union).
 *
 * \param[in] type The IOP type
 * \return true if the IOP type is scalar, false otherwise.
 */
bool iop_type_is_scalar(iop_type_t type);

static inline bool iop_field_is_reference(const iop_field_t * nonnull fdesc)
{
    unsigned fdesc_flags = fdesc->flags;

    return TST_BIT(&fdesc_flags, IOP_FIELD_IS_REFERENCE);
}

/** Return whether the C representation of the IOP field is a pointer or not.
 *
 * \param[in] fdesc IOP field description.
 * \return true if the associated C field is a pointer, false otherwise.
 *
 * \note For repeated fields, the function returns true if the elements of the
 *       associated array are pointers.
 */
bool iop_field_is_pointed(const iop_field_t * nonnull fdesc);

/** Get an iop_field from its name.
 *
 * Get an iop_field_t in an iop_struct_t if it exists. If \p st is a class,
 * its parents will be walked through in order to check if they contain a
 * field named \p name.
 *
 * \param[in]  st  the iop_struct_t in which the field \p name is searched.
 * \param[in]  name  the name of the field to look for.
 * \param[out] found_st  set to the class that contains the field if \p st is
 *                       a class - or to \p st otherwise - if the field is
 *                       found.
 * \param[out] found_fdesc  set to the field descriptor if the field is found.
 *
 * \return  index of the field in a structure if the field is found, -1
 *          otherwise.
 */
int iop_field_find_by_name(const iop_struct_t * nonnull st, const lstr_t name,
                           const iop_struct_t * nullable * nullable found_st,
                           const iop_field_t * nullable * nullable found_fdesc);

/** Fill a field in an iop structure.
 *
 * Fill a field of an iop structure if it is possible to set it empty or to
 * set a default value.
 *
 * \param[in]  mp  the memory pool on which the missing data will be
 *                 allocated.
 * \param[in]  value  the pointer to an iop structure of the type \p fdesc
 *                    belongs to.
 * \param[in]  desc   IOP structure description (optional: only used for
 *                    setting struct desc in \p iop_err_g).
 * \param[in]  fdesc  the descriptor of the field to fill.
 *
 * \return  0 if the field was filled, -1 otherwise.
 */
__must_check__
int iop_skip_absent_field_desc(mem_pool_t * nonnull mp, void * nonnull value,
                               const iop_struct_t * nullable sdesc,
                               const iop_field_t * nonnull fdesc);

int iop_ranges_search(int const * nonnull ranges, int ranges_len, int tag);

/* }}} */
/* {{{ IOP Introspection: iop_for_each_field() */

#ifdef __has_blocks

/** Anonymous type for IOP field stack. */
typedef struct iop_field_stack_t iop_field_stack_t;

/** Print an IOP field stack as a path.
 *
 * Field paths printed with this function will look like 'foo.bar[42].param'.
 */
void sb_add_iop_field_stack(sb_t *nonnull buf,
                            const iop_field_stack_t *nonnull fstack);

/** Write the result of 'sb_add_iop_field_stack' in a t-allocated lstring. */
lstr_t t_iop_field_stack_to_lstr(const iop_field_stack_t *nonnull fstack);

#define IOP_FIELD_SKIP  1

/** Callback for function 'iop_for_each_field'.
 *
 * \param[in]     st_desc  Description of the current struct/union/class.
 *
 * \param[in]     fdesc    Description of the current field.
 *
 * \param[in,out] st_ptr   Pointer on the structure.
 *
 * \param[in]     stack    Data structure containing the context of the
 *                         currently explored field starting from the root
 *                         IOP struct/union/class. Its only use for now is
 *                         allowing to print the path to the field (example:
 *                         "a.b[0].c[42]").
 *
 * \return A negative value to stop the exploration,
 *         IOP_FIELD_SKIP to avoid exploring current field (no effect if the
 *         field is not a struct/union/class), 0 otherwise.
 */
typedef int
(BLOCK_CARET iop_for_each_field_cb_b)
    (const iop_struct_t *nonnull st_desc,
     const iop_field_t *nonnull fdesc,
     void *nonnull st_ptr,
     const iop_field_stack_t *nonnull stack);

/** Explore an IOP struct/class/union recursively and call a block for each
 *  field.
 *
 *  \param[in] st_desc  Description of the struct/union/class to explore.
 *                      Can be left NULL for IOP classes.
 *  \param[in] st_ptr   Pointer on the struct/union/class to explore.
 *  \param[in] cb       See documentation for type 'iop_for_each_field_cb_b'.
 *
 *  \return A negative value (the one returned by the callback) if the
 *          exploration was interrupted, 0 otherwise.
 */
int iop_for_each_field(const iop_struct_t *nullable st_desc,
                       void *nonnull st_ptr,
                       iop_for_each_field_cb_b nonnull cb);

/** Const version for 'iop_for_each_field_cb_b'. */
typedef int
(BLOCK_CARET iop_for_each_field_const_cb_b)
    (const iop_struct_t *nonnull st_desc,
     const iop_field_t *nonnull fdesc,
     const void *nonnull st_ptr,
     const iop_field_stack_t *nonnull stack);

/** Const version for 'iop_for_each_field'. */
int iop_for_each_field_const(
    const iop_struct_t *nullable st_desc,
    const void *nonnull st_ptr,
    iop_for_each_field_const_cb_b nonnull cb);

/** Callback for function 'iop_for_each_field_fast'.
 *
 * Same as 'iop_for_each_field_cb_b' without the parameter 'stack'.
 */
typedef int
(BLOCK_CARET iop_for_each_field_fast_cb_b)(
    const iop_struct_t * nonnull st_desc,
    const iop_field_t * nonnull fdesc,
    void * nonnull st_ptr);

/** Fast version of 'iop_for_each_field'.
 *
 * This version doesn't maintain the context of exploration with the data
 * structure carried by 'iop_field_stack_t'. Should be used when this data
 * structure is not needed.
 *
 * Using this version instead of 'iop_for_each_field' brings an estimate gain
 * of 33% in CPU time.
 */
int iop_for_each_field_fast(const iop_struct_t * nullable st_desc,
                            void * nonnull st_ptr,
                            iop_for_each_field_fast_cb_b nonnull cb);

/** Const version for 'iop_for_each_field_fast_cb_b'. */
typedef int
(BLOCK_CARET iop_for_each_field_const_fast_cb_b)(
    const iop_struct_t * nonnull st_desc,
    const iop_field_t * nonnull fdesc,
    const void * nonnull st_ptr);

/** Const version of 'iop_for_each_field_fast'. */
int iop_for_each_field_const_fast(
    const iop_struct_t * nullable st_desc,
    const void * nonnull st_ptr,
    iop_for_each_field_const_fast_cb_b nonnull cb);

/** Callback for function 'iop_for_each_st'.
 *
 * \param[in]     st_desc  Description of the current struct/union/class.
 * \param[in,out] st_ptr   Pointer on the structure.
 * \param[in]     stack    Context for the field containing the current
 *                         struct/union/class.
 *
 * \return A negative value to stop the exploration,
 *         IOP_FIELD_SKIP to avoid exploring the fields of current
 *         struct/union/class.
 */
typedef int
(BLOCK_CARET iop_for_each_st_cb_b)(const iop_struct_t *nonnull st_desc,
                                   void *nonnull st_ptr,
                                   const iop_field_stack_t *nonnull stack);

/** Explore an IOP struct/union/class recursively and call a block for each
 *  struct/union/class.
 *
 *  Same as iop_for_each_field() but the callback will be called for each
 *  struct/union/class contained in the input IOP including itself, not for
 *  scalar fields.
 */
int iop_for_each_st(const iop_struct_t * nullable st_desc,
                    void * nonnull st_ptr,
                    iop_for_each_st_cb_b nonnull cb);

/** Const version for 'iop_for_each_st_cb_b'. */
typedef int
(BLOCK_CARET iop_for_each_st_const_cb_b)(
    const iop_struct_t *nonnull st_desc,
    const void *nonnull st_ptr,
    const iop_field_stack_t *nonnull stack);

/** Const version for 'iop_for_each_st'. */
int iop_for_each_st_const(const iop_struct_t *nullable st_desc,
                          const void *nonnull st_ptr,
                          iop_for_each_st_const_cb_b nonnull cb);

/** Callback for function 'iop_for_each_st_fast'.
 *
 * Same as 'iop_for_each_st_cb_b' without the parameter 'stack'.
 */
typedef int
(BLOCK_CARET iop_for_each_st_fast_cb_b)(const iop_struct_t *nonnull st_desc,
                                        void *nonnull st_ptr);

/** Fast version of 'iop_for_each_st'.
 *
 * See 'iop_for_each_st'.
 */
int iop_for_each_st_fast(const iop_struct_t *nullable st_desc,
                         void *nonnull st_ptr,
                         iop_for_each_st_fast_cb_b nonnull cb);

/** Const version for 'iop_for_each_st_fast_cb_b'. */
typedef int
(BLOCK_CARET iop_for_each_st_const_fast_cb_b)(
    const iop_struct_t * nonnull st_desc,
    const void * nonnull st_ptr);

/** Const version of 'iop_for_each_st_fast'. */
int iop_for_each_st_const_fast(const iop_struct_t *nullable st_desc,
                               const void *nonnull st_ptr,
                               iop_for_each_st_const_fast_cb_b nonnull cb);

#endif

/* }}} */
/* {{{ IOP iop_full_type_t */

/** Description of a complete IOP type. */
typedef struct {
    /** The base type.
     */
    iop_type_t type;

    union {
        /** Union/struct type description.
         *
         * If \ref type is a struct, union or class (\ref IOP_T_STRUCT or
         * \ref IOP_T_UNION), then this field is expected to be set to the
         * right iop structure descriptor.
         */
        const iop_struct_t *nullable st;

        /** Enum type description.
         *
         * If \ref type is \ref IOP_T_ENUM, then this field is set to the
         * right iop enum descriptor.
         */
        const iop_enum_t *nullable en;
    };
    /* TODO Attributes and constraints (we are very likely to want to accept
     * constraints as a part of the type in the future). */
    /* TODO Flag for repeated types (we may want to support ARRAY_OF(X) as a
     * plain type in the future). */
} iop_full_type_t;

GENERIC_INIT(iop_full_type_t, iop_full_type);

#define IOP_FTYPE_VOID    (iop_full_type_t){ .type = IOP_T_VOID }
#define IOP_FTYPE_I8      (iop_full_type_t){ .type = IOP_T_I8 }
#define IOP_FTYPE_U8      (iop_full_type_t){ .type = IOP_T_U8 }
#define IOP_FTYPE_I16     (iop_full_type_t){ .type = IOP_T_I16 }
#define IOP_FTYPE_U16     (iop_full_type_t){ .type = IOP_T_U16 }
#define IOP_FTYPE_I32     (iop_full_type_t){ .type = IOP_T_I32 }
#define IOP_FTYPE_U32     (iop_full_type_t){ .type = IOP_T_U32 }
#define IOP_FTYPE_I64     (iop_full_type_t){ .type = IOP_T_I64 }
#define IOP_FTYPE_U64     (iop_full_type_t){ .type = IOP_T_U64 }
#define IOP_FTYPE_BOOL    (iop_full_type_t){ .type = IOP_T_BOOL }
#define IOP_FTYPE_DOUBLE  (iop_full_type_t){ .type = IOP_T_DOUBLE }
#define IOP_FTYPE_STRING  (iop_full_type_t){ .type = IOP_T_STRING }
#define IOP_FTYPE_DATA    (iop_full_type_t){ .type = IOP_T_DATA }
#define IOP_FTYPE_XML     (iop_full_type_t){ .type = IOP_T_XML }

#define IOP_FTYPE_ST_DESC(_st)                                               \
    ({                                                                       \
        const iop_struct_t *__st = (_st);                                    \
                                                                             \
        (iop_full_type_t){                                                   \
            .type = __st->is_union ? IOP_T_UNION : IOP_T_STRUCT,             \
            { .st = __st }                                                   \
        };                                                                   \
    })

#define IOP_FTYPE_ST(pfx)  IOP_FTYPE_ST_DESC(&pfx##__s)

#define IOP_FTYPE_EN_DESC(_en)                                               \
    (iop_full_type_t){                                                       \
        .type = IOP_T_ENUM,                                                  \
        { .en = (_en) }                                                      \
    };                                                                       \

/* Functions to use iop_full_type_t as a QH/QM key.
 *
 * XXX In these functions, the enum and struct types are considered equal iff
 * there is a pointer equality between the iop_enum_t/iop_struct_t
 * descriptions.
 */
uint32_t qhash_iop_full_type_hash(const qhash_t *nonnull qhash,
                                  const iop_full_type_t *nonnull type);
bool qhash_iop_full_type_equal(const qhash_t *nonnull qhash,
                               const iop_full_type_t *nonnull t1,
                               const iop_full_type_t *nonnull t2);

/** Get the IOP full type associated with a given IOP field. */
void iop_field_get_type(const iop_field_t *nonnull field,
                        iop_full_type_t *nonnull type);

/* }}} */
/* {{{ IOP field path API */

typedef struct iop_field_path_t iop_field_path_t;

/** Build an IOP field path on a specified memory pool.
 *
 * \param[in] mp    The memory pool on which the allocation will be done.
 *                  Can be NULL to use malloc.
 * \param[in] st    The structure type of the values containing the fields.
 * \param[in] path  Full path to the field. Can contain:
 *     - Subfields: 'foo.bar'.
 *     - Array indexes: 'elts[0].v', 'a.array[-1]' (negative indexes means
 *     that the array is indexed backward: the index '-1' is for the last
 *     element).
 *     - Wildcard indexes: 'elts[*].v', 'a.array[*]', 'structs[*].fields[*]':
 *     can be used when wanting to iterate on all elements of an array.
 * \param[out] err  The error description in case of error.
 * \return The IOP field path allocated on the memory pool. NULL in case of
 * error.
 */
const iop_field_path_t *nullable
mp_iop_field_path_compile(mem_pool_t *nullable mp,
                          const iop_struct_t *nonnull st,
                          lstr_t path, sb_t *nullable err);

/** Build an IOP field path on the t_stack.
 *
 * \see mp_iop_field_path_compile.
 */
static inline const iop_field_path_t *nullable
t_iop_field_path_compile(const iop_struct_t *nonnull st,
                         lstr_t path, sb_t *nullable err)
{
    return mp_iop_field_path_compile(t_pool(), st, path, err);
}

/** Build an IOP field path on the standard libc allocator.
 *
 * \see mp_iop_field_path_compile.
 */
static inline const iop_field_path_t *nullable
iop_field_path_compile(const iop_struct_t *nonnull st,
                       lstr_t path, sb_t *nullable err)
{
    return mp_iop_field_path_compile(NULL, st, path, err);
}

/** Delete an IOP field path allocated on the specfied memory pool.
 *
 * \param[in] mp     The memory pool on which the allocation was be done.
 *                   Can be NULL to use malloc.
 * \param[in] fp_ptr A pointer to the IOP field path to delete.
 */
void
mp_iop_field_path_delete(mem_pool_t *nullable mp,
                         const iop_field_path_t *nullable *nonnull fp_ptr);

/** Delete an IOP field path allocated on the standard libc allocator.
 *
 * \see mp_iop_field_path_delete.
 */
static inline void
iop_field_path_delete(const iop_field_path_t *nullable *nonnull fp_ptr)
{
    mp_iop_field_path_delete(NULL, fp_ptr);
}

/** Get the type associated with a given field path.
 *
 * \note The parameter \p is_array may be removed someday if we integrate it
 * into \ref iop_full_type_t.
 */
void iop_field_path_get_type(const iop_field_path_t *nonnull fp,
                             iop_full_type_t *nonnull type,
                             bool *nonnull is_array);

/** Get the type of a field for a given IOP object.
 *
 * This function differs from
 * \ref t_iop_field_path_compile + \ref iop_field_path_get_type in that it
 * applies to a specific IOP object, and is not generic on any object of a
 * given type. This allows the path to use fields from subclasses used by this
 * IOP object.
 * For example, the path "arrayOfParentClass[1].fieldOfSubclass" cannot be
 * used with t_iop_field_path_compile. However, if passed to this function,
 * it will be able to resolve the type, if the given IOP object has the right
 * subclass in this path.
 *
 * \warning. Wildcard indexes cannot be used with this function.
 *
 * \param[in] st         Type of the IOP object.
 * \param[in] value      Pointer to the IOP object.
 * \param[in] path       Path to the IOP field. See
 *                       \ref t_iop_field_path_compile for the syntax.
 * \param[out] type      Type of the IOP field for the given IOP object.
 * \param[out] is_array  True if the field is an array.
 * \param[out] err  The error description in case of error.
 *
 * \return -1 In case of error, 0 otherwise.
 */
int iop_obj_get_field_type(const iop_struct_t *nonnull st,
                           const void *nonnull value, lstr_t path,
                           iop_full_type_t *nonnull type,
                           bool *nonnull is_array, sb_t *nullable err);

#ifdef __has_blocks

/** Block type for \ref iop_field_path_for_each_value.
 *
 * \param[in] field_ptr  Pointer on one of the value of the field targeted by
 *                       the field path.
 *
 * \return a negative value when wanting to stop the scan.
 */
typedef int (BLOCK_CARET iop_ptr_cb_b)(const void *nonnull field_ptr);

/** List each value matching a given field path.
 *
 * \param[in] fp  Field path built with \ref t_iop_field_path_compile.
 *
 * \param[in] st_ptr  Pointer on the structure containing the values to list.
 *                    The struct type should be the same as the one the field
 *                    path has been built with.
 *
 * \param[in] on_value  Callback to call for each value found.
 *
 * \return -1 if the scan was interrupted because the \p on_value returned a
 * negative value (user interruption).
 */
int iop_field_path_for_each_value(const iop_field_path_t *nonnull fp,
                                  const void *nonnull st_ptr,
                                  iop_ptr_cb_b nonnull on_value);

#endif /* __has_blocks */

/* }}} */
/* {{{ IOP structures manipulation */

/** Initialize an IOP structure with the correct default values.
 *
 * You always need to initialize your IOP structure before packing it, however
 * it is useless when you unpack a structure it will be done for you.
 * If the struct contains a field with an union, that field will remain
 * uninitialized.
 *
 * Prefer the macro version iop_init() instead of this low-level API.
 *
 * \param[in] st    The IOP structure definition (__s).
 * \param[in] value Pointer on the IOP structure to initialize.
 */
void iop_init_desc(const iop_struct_t * nonnull st, void * nonnull value);

#define iop_init(pfx, value)  ({                                             \
        pfx##__t *__v = (value);                                             \
                                                                             \
        iop_init_desc(&pfx##__s, (void *)__v);                               \
        __v;                                                                 \
    })

/** Initialize an IOP union with the specified tag.
 *
 * Prefer the macro version iop_init_union() instead of this low-level API.
 *
 * \param[in] st    The IOP union definition (__s).
 * \param[in] value Pointer on the IOP structure to initialize.
 * \param[in] tag   The union tag.
 */
void iop_init_union_desc(const iop_struct_t * nonnull st,
                         void * nonnull value,
                         const iop_field_t * nonnull fdesc);

#define iop_init_union(pfx, value, field)  ({                                \
        pfx##__t *__v = (value);                                             \
                                                                             \
        iop_init_union_desc(&pfx##__s, (void *)__v,                          \
                            &IOP_UNION_FDESC(pfx, field));                   \
        __v;                                                                 \
    })

/** Allocate an IOP structure and initialize it with the correct
 *  default values.
 *
 * \param[in] mp  The memory pool on which the allocation will be done.
 *                Can be NULL to use malloc.
 * \param[in] st  The IOP structure definition (__s).
 */
__attribute__((malloc, warn_unused_result))
void * nonnull mp_iop_new_desc(mem_pool_t *nullable mp,
                               const iop_struct_t * nonnull st);

__attribute__((malloc, warn_unused_result))
static inline void * nonnull iop_new_desc(const iop_struct_t * nonnull st)
{
    return mp_iop_new_desc(NULL, st);
}
__attribute__((malloc, warn_unused_result))
static inline void * nonnull t_iop_new_desc(const iop_struct_t * nonnull st)
{
    return mp_iop_new_desc(t_pool(), st);
}
__attribute__((malloc, warn_unused_result))
static inline void * nonnull r_iop_new_desc(const iop_struct_t * nonnull st)
{
    return mp_iop_new_desc(r_pool(), st);
}

#define mp_iop_new(mp, pfx)  ((pfx##__t *)mp_iop_new_desc(mp, &pfx##__s))
#define iop_new(pfx)         ((pfx##__t *)iop_new_desc(&pfx##__s))
#define t_iop_new(pfx)       ((pfx##__t *)t_iop_new_desc(&pfx##__s))
#define r_iop_new(pfx)       ((pfx##__t *)r_iop_new_desc(&pfx##__s))

/** Return whether two IOP structures are equals or not.
 *
 * Prefer the macro version iop_equals instead of this low-level API.
 *
 * v1 and v2 can be NULL. If both v1 and v2 are NULL they are considered as
 * equals.
 *
 * \param[in] st  The IOP structures definition (__s).
 * \param[in] v1  Pointer on the IOP structure to be compared.
 * \param[in] v2  Pointer on the IOP structure to be compared with.
 */
bool  iop_equals_desc(const iop_struct_t * nonnull st,
                      const void * nullable v1,
                      const void * nullable v2);

#define iop_equals(pfx, v1, v2)  ({                                          \
        const pfx##__t *__v1 = (v1);                                         \
        const pfx##__t *__v2 = (v2);                                         \
                                                                             \
        iop_equals_desc(&pfx##__s, (const void *)__v1, (const void *)__v2);  \
    })

/** Print a description of the first difference between two IOP structures.
 *
 * Mainly designed for testing: give additional information when two IOP
 * structures differ when they are not supposed to.
 *
 * \return -1 if the IOP structs are equal.
 */
int iop_first_diff_desc(const iop_struct_t * nonnull st,
                        const void * nonnull v1, const void * nonnull v2,
                        sb_t * nonnull diff_desc);

/** Flags for IOP sorter. */
enum iop_sort_flags {
    /* Perform a reversed sort */
    IOP_SORT_REVERSE = (1U << 0),
    /* Let the IOP objects that do not contain the sorting field at the
     * beginning of the vector (otherwise they are left at the end)
     */
    IOP_SORT_NULL_FIRST = (1U << 1),
};

/** Sort a vector of IOP structures or unions based on a given field or
 *  subfield of reference. The comparison function is the canonical comparison
 *  associated to the type of field.
 *
 * Prefer the macro versions iop_sort() and iop_obj_sort() instead of this
 * low-level API.
 *
 *  \param[in] st          The IOP structure definition (__s).
 *  \param[in] vec         Array of objects to sort. If st is a class, this
 *                         must be an array of pointers on the elements, and
 *                         not an array of elements.
 *  \param[in] len         Length of the array
 *  \param[in] field_path  Path of the field of reference for sorting,
 *                         containing the names of the fields and subfield,
 *                         separated by dots
 *                         Example: "field.subfield1.subfield2"
 *  \param[in] flags       Binary combination of sorting flags (see enum
 *                         iop_sort_flags)
 *  \param[out] err        In case of error, the error description.
 */
int iop_sort_desc(const iop_struct_t * nonnull st, void * nonnull vec,
                  int len, lstr_t field_path, int flags, sb_t * nullable err);

#define iop_sort(pfx, vec, len, field_path, flags, err)  ({                  \
        pfx##__t *__vec = (vec);                                             \
                                                                             \
        iop_sort_desc(&pfx##__s, (void *)__vec, (len), (field_path),         \
                      (flags), (err));                                       \
    })

#define iop_obj_sort(pfx, vec, len, field_path, flags, err)  ({              \
        pfx##__t **__vec = (vec);                                            \
                                                                             \
        iop_sort_desc(&pfx##__s, (void *)__vec, (len), (field_path),         \
                      (flags), (err));                                       \
    })

typedef struct iop_sort_t {
    lstr_t field_path;
    int flags;
} iop_sort_t;
GENERIC_FUNCTIONS(iop_sort_t, iop_sort)

qvector_t(iop_sort, iop_sort_t);

/** Sort a vector of IOP as iop_sort, but on multiple fields.
 *
 *
 *  \param[in] st          The IOP structure definition (__s).
 *  \param[in] vec         The array to sort \see iop_sort.
 *  \param[in] len         Length of the array
 *  \param[in] params      All the path to sort on, the array will be sorted
 *                         on the first element, and in case of equality,
 *                         on the seconde one, and so on.
 *                         \see iop_sort for field path syntax and flags desc.
 *  \param[out] err        In case of error, the error description.
 */
int iop_msort_desc(const iop_struct_t * nonnull st, void * nonnull vec,
                   int len, const qv_t(iop_sort) * nonnull params,
                   sb_t * nullable err);

#define iop_msort(pfx, vec, len, params, err)  ({                            \
        pfx##__t *__vec = (vec);                                             \
                                                                             \
        iop_msort_desc(&pfx##__s, (void *)__vec, (len), (params), (err));    \
    })

#define iop_obj_msort(pfx, vec, len, params, err)  ({                        \
        pfx##__t **__vec = (vec);                                            \
                                                                             \
        iop_msort_desc(&pfx##__s, (void *)__vec, (len), (params), (err));    \
    })

/** Compare two IOPs in an arbitrary way. */
int iop_cmp_desc(const iop_struct_t *nonnull st,
                 const void *nullable v1, const void *nullable v2);

#define iop_cmp(pfx, st1, st2)  ({                                           \
        const pfx##__t *__st1 = (st1);                                       \
        const pfx##__t *__st2 = (st2);                                       \
                                                                             \
        iop_cmp_desc(&pfx##__s, __st1, __st2);                               \
    })

/** Single-field comparison between two IOP structs, unions or classes.
 *
 * \param[in] st1  Pointer on the first struct containing the field to
 *                 compare.
 *
 * \param[in] st2  Pointer on the second struct.
 *
 * \warning If the fields are union fields then the field given in \p fdesc
 *          should be selected in both structs.
 */
int iop_cmp_field(const iop_field_t *nonnull fdesc,
                  const void *nonnull st1, const void *nonnull st2);

/** Sort an IOP vector following an arbitrary order.
 *
 * \warning The array will be considered as an array of pointers iff the
 *          struct is a class. If the input is an user-built array of struct
 *          or unions, please use \ref iop_xpsort_desc.
 */
void iop_xsort_desc(const iop_struct_t *nonnull st,
                    void *nonnull vec, int len);

/** Sort an IOP vector of pointers following an arbitrary order. */
void iop_xpsort_desc(const iop_struct_t *nonnull st,
                     const void *nonnull *nonnull vec, int len);

#define iop_xsort(pfx, vec, len)  ({                                         \
        pfx##__t *__vec = (vec);                                             \
        iop_xsort_desc(&pfx##__s, (void *)__vec, (len));                     \
    })

#define iop_xpsort(pfx, vec, len)  ({                                        \
        const pfx##__t **__vec = (vec);                                      \
        iop_xpsort_desc(&pfx##__s, (const void **)__vec, (len));             \
    })

/** Flags for IOP filter. */
enum iop_filter_flags {
    /** Perform a SQL-like pattern matching for strings. */
    IOP_FILTER_SQL_LIKE = (1U << 0),

    /** Instead of filtering out the objects which field value is not in the
     *  values array by default, filtering out the objects which field value
     *  is in the values array. */
    IOP_FILTER_INVERT_MATCH = (1U << 1),
};

/** Filter in-place a vector of IOP based on a given field or subfield of
 *  reference.
 *
 *  It takes an array of IOP objets and an array of values, and filters out
 *  the objects which field value is not in the values array if
 *  IOP_FILTER_INVERT_MATCH is not set. Otherwise, it filters out
 *  the objects which field value is in the values array.
 *
 *  When the field is repeated, the function looks for the first occurrence of
 *  values in the field.
 *  Example: [ 1, 2, 3 ] and values = [ 3 ] => true.
 *
 *  \param[in] st             The IOP structure definition (__s).
 *  \param[in/out] vec        Array of objects to filter. If st is a class,
 *                            this must be an array of pointers on the
 *                            elements, and not an array of elements.
 *  \param[in/out] len        Length of the array. It is adjusted with the new
 *                            value once the filter is done.
 *  \param[in] field_path     Path of the field of reference for filtering,
 *                            containing the names of the fields and subfield,
 *                            separated by dots
 *                            Example: "field.subfield1.subfield2"
 *  \param[in] values         Array of pointer on values to be matched inside
 *                            vec.
 *                            \warning the type of values must be the right
 *                            one because no check is done inside the
 *                            function.
 *  \param[in] values_len     Length of the array of values.
 *  \param[in] flags          A combination of enum iop_filter_flags.
 *  \param[out] err           In case of error, the error description.
 */
int iop_filter(const iop_struct_t * nonnull st, void * nonnull vec,
               int * nonnull len, lstr_t field_path,
               void * const nonnull * nonnull values, int values_len,
               unsigned flags, sb_t * nullable err);

/** Filter in-place a vector of IOP based on the presence of a given optional
 *  or repeated field or subfield.
 *
 * Same as \ref iop_filter but for optional or repeated fields only. It does
 * not take an array of value, but a parameter \p is_set telling if the fields
 * must be set (for optional fields) or non-empty (for repeated fields) to be
 * kept.
 */
int iop_filter_opt(const iop_struct_t * nonnull st, void * nonnull vec,
                   int * nonnull len, lstr_t field_path, bool is_set,
                   sb_t * nullable err);

typedef enum iop_filter_bitmap_op_t {
    /** And operation.
     *
     * The elements that are not in the allowed values are removed from the
     * bitmap.
     */
    BITMAP_OP_AND,

    /** Or operation.
     *
     * The elements that are in the allowed values are added in the bitmap.
     */
    BITMAP_OP_OR,
} iop_filter_bitmap_op_t;

/** Filter a vector of IOP based on a given field or subfield of reference,
 *  and fills a bitmap accordingly.
 *
 * Same as \ref iop_filter, but it does not modify the input vector: it just
 * fills a bitmap depending on the presence of the elements in the allowed
 * values and the requested bitmap operation.
 *
 * If the bitmap is NULL, it is automatically created. Callers must NOT create
 * it themselves.
 */
int t_iop_filter_bitmap(const iop_struct_t * nonnull st,
                        const void * nonnull vec, int len, lstr_t field_path,
                        void * const nonnull * nonnull values,
                        int values_len, unsigned flags,
                        iop_filter_bitmap_op_t bitmap_op,
                        byte * nonnull * nullable bitmap,
                        sb_t * nullable err);

/** Filter a vector of IOP based on the presence of a given optional or
 *  repeated field or subfield.
 *
 * Same as \ref iop_filter_bitmap, but based on \ref iop_filter_opt.
 */
int t_iop_filter_opt_bitmap(const iop_struct_t * nonnull st,
                            const void * nonnull vec, int len,
                            lstr_t field_path, bool is_set,
                            iop_filter_bitmap_op_t bitmap_op,
                            byte * nonnull * nullable bitmap,
                            sb_t * nullable err);

/** Filter in-place a vector according to a bitmap.
 *
 * It applies a bitmap resulting of \ref iop_filter_bitmap or
 * \ref iop_filter_opt_bitmap. Only entries marked as present in the bitmap
 * are kept in the vector.
 */
void iop_filter_bitmap_apply(const iop_struct_t * nonnull st,
                             void * nonnull vec, int * nonnull len,
                             const byte * nonnull bitmap);

/* Remove fields tagged with the `gen_attr` generic attribute.
 *
 * Walk recursively through the IOP object.
 *
 * \param[in]  st  The IOP structure definition (__s).
 * \param[in/out]  obj  The IOP object to lighten.
 * \param[in]  gen_attr  The name of the generic attribute to search. This
 *                       attribute must be a boolean and it must tag optional
 *                       or repeated field. It must not tag default or
 *                       required fields.
 */
void iop_prune(const iop_struct_t * nonnull st, void * nonnull obj,
               lstr_t gen_attr);

/** Flags used by iop_dup and iop_copy functions. */
typedef enum iop_copy_flags_t {
    /** Use multiple allocations instead of using a single block.
     *
     * The memory pool must be a by-frame memory pool like mem_stack or
     * ring_pool because we don't expect the user to free all the pointers
     * manually.
     */
    IOP_COPY_MULTIPLE_ALLOC = 1 << 0,

    /** Perform a shallow copy instead of a default deep copy.
     *
     * Only the root structure fields are copied when using this flag. By
     * default, all fields of the structure are copied recursively.
     */
    IOP_COPY_SHALLOW = 1 << 1,

    /** Do not perform reallocation of the output value on copy.
     *
     * For class instances, the output object must be initialized and the
     * original __vptr of the output object is not modified.
     *
     * Like \ref IOP_COPY_MULTIPLE_ALLOC, the memory pool must be a by-frame
     * memory pool if \ref IOP_COPY_SHALLOW is not used.
     *
     * This flag is not available for iop_dup functions.
     */
    IOP_COPY_NO_REALLOC = 1 << 2,
} iop_copy_flags_t;

/** Duplicate an IOP structure.
 *
 * The resulting IOP structure will fully contained in one block of memory.
 *
 * Prefer the macro versions instead of this low-level API.
 *
 * \param[in] mp The memory pool to use for the new allocation. If mp is NULL
 *               the libc malloc() will be used.
 * \param[in] st The IOP structure definition (__s).
 * \param[in] v  The IOP structure to duplicate.
 * \param[in] flags The bitmap of \ref iop_copy_flags_t.
 * \param[out] sz If set, filled with the size of the allocated buffer.
 */
void * nullable
mp_iop_dup_desc_flags_sz(mem_pool_t * nullable mp,
                         const iop_struct_t * nonnull st,
                         const void * nullable v, unsigned flags,
                         size_t * nullable sz);

static inline void * nullable
mp_iop_dup_desc_sz(mem_pool_t * nullable mp, const iop_struct_t * nonnull st,
                   const void * nullable v, size_t * nullable sz)
{
    return mp_iop_dup_desc_flags_sz(mp, st, v, 0, sz);
}

#define mp_iop_dup_flags_sz(mp, pfx, v, flags, sz)  ({                       \
        const pfx##__t *_id_v = (v);                                         \
                                                                             \
        (pfx##__t *)mp_iop_dup_desc_flags_sz((mp), &pfx##__s,                \
                                             (const void *)_id_v, (flags),   \
                                             (sz));                          \
    })

#define mp_iop_dup_flags(mp, pfx, v, flags)                                  \
    mp_iop_dup_flags_sz((mp), pfx, (v), (flags), NULL)
#define iop_dup_flags(pfx, v, flags)                                         \
    mp_iop_dup_flags(NULL, pfx, (v), (flags))
#define t_iop_dup_flags(pfx, v, flags)                                       \
    mp_iop_dup_flags(t_pool(), pfx, (v), (flags))
#define r_iop_dup_flags(pfx, v, flags)                                       \
    mp_iop_dup_flags(r_pool(), pfx, (v), (flags))

#define mp_iop_dup_sz(mp, pfx, v, sz)                                        \
    mp_iop_dup_flags_sz((mp), pfx, (v), 0, (sz))
#define mp_iop_dup(mp, pfx, v)  mp_iop_dup_flags((mp), pfx, (v), 0)
#define iop_dup(pfx, v)         iop_dup_flags(pfx, (v), 0)
#define t_iop_dup(pfx, v)       t_iop_dup_flags(pfx, (v), 0)
#define r_iop_dup(pfx, v)       r_iop_dup_flags(pfx, (v), 0)

#define mp_iop_shallow_dup(mp, pfx, v)                                       \
    mp_iop_dup_flags((mp), pfx, (v), IOP_COPY_SHALLOW)
#define iop_shallow_dup(pfx, v)    mp_iop_shallow_dup(NULL, pfx, (v))
#define t_iop_shallow_dup(pfx, v)  mp_iop_shallow_dup(t_pool(), pfx, (v))
#define r_iop_shallow_dup(pfx, v)  mp_iop_shallow_dup(r_pool(), pfx, (v))

/** Copy an IOP structure into another one.
 *
 * The destination IOP structure will reallocated to handle the source
 * structure.
 *
 * Prefer the macro versions instead of this low-level API.
 *
 * \param[in] mp    The memory pool to use for the reallocation. If mp is NULL
 *                  the libc realloc() will be used.
 * \param[in] st    The IOP structure definition (__s).
 * \param[in] outp  Pointer on the destination structure that will be
 *                  reallocated to retrieve the v IOP structure.
 * \param[in] v     The IOP structure to copy.
 * \param[in] flags The bitmap of \ref iop_copy_flags_t.
 * \param[out] sz   If set, filled with the size of the allocated buffer.
 */
void mp_iop_copy_desc_flags_sz(mem_pool_t * nullable mp,
                               const iop_struct_t * nonnull st,
                               void * nullable * nonnull outp,
                               const void * nullable v, unsigned flags,
                               size_t * nullable sz);

static inline void
mp_iop_copy_desc_sz(mem_pool_t * nullable mp, const iop_struct_t * nonnull st,
                    void * nullable * nonnull outp, const void * nullable v,
                    size_t * nullable sz)
{
    mp_iop_copy_desc_flags_sz(mp, st, outp, v, 0, sz);
}

#define mp_iop_copy_flags_sz(mp, pfx, outp, v, flags, sz)  do {              \
        pfx##__t **__outp = (outp);                                          \
        const pfx##__t *__v = (v);                                           \
                                                                             \
        mp_iop_copy_desc_flags_sz(mp, &pfx##__s, (void **)__outp,            \
                                  (const void *)__v, (flags), (sz));         \
    } while (0)

#define mp_iop_copy_flags(mp, pfx, outp, v, flags)                           \
    mp_iop_copy_flags_sz((mp), pfx, (outp), (v), (flags), NULL)
#define iop_copy_flags(pfx, outp, v, flags)                                  \
    mp_iop_copy_flags(NULL, pfx, (outp), (v), (flags))
#define t_iop_copy_flags(pfx, outp, v, flags)                                \
    mp_iop_copy_flags(t_pool(), pfx, (outp), (v), (flags))
#define r_iop_copy_flags(pfx, outp, v, flags)                                \
    mp_iop_copy_flags(r_pool(), pfx, (outp), (v), (flags))

#define mp_iop_copy_sz(mp, pfx, outp, v, sz)                                 \
    mp_iop_copy_flags_sz((mp), pfx, (outp), (v), 0, (sz))
#define mp_iop_copy(mp, pfx, outp, v)                                        \
    mp_iop_copy_flags((mp), pfx, (outp), (v), 0)
#define iop_copy(pfx, outp, v)    iop_copy_flags(pfx, (outp), (v), 0)
#define t_iop_copy(pfx, outp, v)  t_iop_copy_flags(pfx, (outp), (v), 0)
#define r_iop_copy(pfx, outp, v)  r_iop_copy_flags(pfx, (outp), (v), 0)

/** Macros to copy an IOP structure value to an already allocated one.
 *
 * It uses \ref IOP_COPY_NO_REALLOC.
 *
 * Example:
 *     value__t dst;
 *
 *     t_iop_copy_v(value, src, &dst);
 */

#define mp_iop_copy_v_flags(mp, pfx, out, v, flags)  do {                    \
        pfx##__t *_out = (out);                                              \
        unsigned _flags = (flags) | IOP_COPY_NO_REALLOC;                     \
                                                                             \
        mp_iop_copy_flags_sz((mp), pfx, &_out, (v), _flags, NULL);           \
    } while (0)

#define t_iop_copy_v_flags(pfx, out, v, flags)                               \
    mp_iop_copy_v_flags(t_pool(), pfx, (out), (v), (flags))
#define r_iop_copy_v_flags(pfx, out, v, flags)                               \
    mp_iop_copy_v_flags(r_pool(), pfx, (out), (v), (flags))

#define mp_iop_copy_v(mp, pfx, out, v)                                        \
    mp_iop_copy_v_flags((mp), pfx, (out), (v), 0)
#define t_iop_copy_v(pfx, out, v)  t_iop_copy_v_flags(pfx, (out), (v), 0)
#define r_iop_copy_v(pfx, out, v)  r_iop_copy_v_flags(pfx, (out), (v), 0)

/* This macro does not perform any allocations. */
#define iop_shallow_copy_v(pfx, out, v)                                      \
    mp_iop_copy_v_flags(NULL, pfx, (out), (v), IOP_COPY_SHALLOW)

/** Find a generic attribute value for an IOP structure.
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  st    The IOP structure definition (__s).
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_struct_get_gen_attr(const iop_struct_t * nonnull st, lstr_t key,
                            iop_type_t exp_type,
                            iop_type_t * nullable val_type,
                            iop_value_t * nonnull value);

/** Find a generic attribute value for an IOP field.
 *
 * If exp_type is >= 0, the type of the generic attribute value will be
 * checked, and the function will return -1 if the type is not compatible.
 * If val_type is not NULL, the type of the generic attribute value will be
 * set (IOP_T_I64, IOP_T_DOUBLE or IOP_T_STRING).
 *
 * \param[in]  st    The IOP structure definition (__s).
 * \param[in]  field The IOP field definition.
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_field_get_gen_attr(const iop_struct_t * nonnull st,
                           const iop_field_t * nonnull field,
                           lstr_t key, iop_type_t exp_type,
                           iop_type_t * nullable val_type,
                           iop_value_t * nonnull value);

/** Get boolean generic attribute value for an IOP field.
 *
 * \param[in]  st    The IOP structure definition.
 * \param[in]  field The IOP field definition.
 * \param[in]  key   The generic attribute key.
 * \param[in]  def   Default value returned if the attribute \p key did not
 *                   match.
 *
 * \return \p def if the generic attribute is not found and the attribute
 *            value otherwise.
 */
bool iop_field_get_bool_gen_attr(const iop_struct_t * nonnull st,
                                 const iop_field_t * nonnull field, lstr_t key,
                                 bool def);

/** Find a generic attribute value for an IOP field.
 *
 * Same as \ref iop_field_get_gen_attr but a name for the field is given
 * instead of field definition.
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  st         The IOP structure definition (__s).
 * \param[in]  field_name The field name.
 * \param[in]  key        The generic attribute key.
 * \param[in]  exp_type   The expected value type.
 * \param[out] val_type   The actual value type.
 * \param[out] value      The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 if the field is unknown or
 *         if the generic attribute is not found.
 */
int iop_field_by_name_get_gen_attr(const iop_struct_t * nonnull st,
                                   lstr_t field_name,
                                   lstr_t key, iop_type_t exp_type,
                                   iop_type_t * nullable val_type,
                                   iop_value_t * nonnull value);

/** Get a pointer to the field value of an optional field (if it exists).
 *
 * The return value is useful to distinguish the case where the option
 * is set, but there is no data for it (optional void field).
 *
 * The parameter \p value is mandatory. If you only want to know whether the
 * optional field is set or not, please use \ref iop_opt_field_isset.
 *
 * \warning For \p IOP_T_VOID type, a "slop" pointer is returned when the
 * field is present but it should not be dereferenced or used to set a field.
 *
 * \param[in] type The type of the field.
 * \param[in] data A pointer to the optional field.
 *
 * \return A pointer on the field value if present, NULL otherwise.
 */
void *nullable iop_opt_field_getv(iop_type_t type, void * nonnull data);

/** Constant version of \ref iop_get_field (below).
 */
const iop_field_t * nullable
iop_get_field_const(const void * nullable ptr,
                    const iop_struct_t * nonnull st,
                    lstr_t path, const void * nullable * nullable out_ptr,
                    const iop_struct_t * nullable * nullable out_st);

/** Find an IOP field description from a iop object.
 *
 * \param[in]  ptr      The IOP object.
 * \param[in]  st       The iop_struct_t describing the object.
 * \param[in]  path     The path to the field (separate members with a '.').
 * \param[out] out_ptr  A pointer to the final IOP object.
 * \param[out] out_st   Descriptor of the structure that contains
 *                      the returned field.
 *
 * \return The iop field description if found, NULL otherwise.
 */
static inline const iop_field_t * nullable
iop_get_field(void * nullable ptr, const iop_struct_t * nonnull st,
              lstr_t path, void * nullable * nullable out_ptr,
              const iop_struct_t * nullable * nullable out_st)
{
    return iop_get_field_const((const void *)ptr, st, path,
                               (const void **)out_ptr, out_st);
}

/** Get a pointer on the C field associated to a given IOP field.
 *
 * \param[in] f       The IOP field description.
 *
 * \param[in] st_ptr  Pointer on the struct/union/class instance containing
 *                    the field.
 *
 * \return The pointer on the C field.
 */
static inline void *nonnull
iop_field_get_ptr(const iop_field_t *nonnull f, void *nonnull st_ptr)
{
    return ((byte *)st_ptr) + f->data_offs;
}

/** Constant version of \ref iop_field_get_ptr. */
static inline const void *nonnull
iop_field_get_cptr(const iop_field_t *nonnull f, const void *nonnull sptr)
{
    return ((const byte *)sptr) + f->data_offs;
}

/** Get the value(s) associated to a given IOP field.
 *
 * Efficient IOP field value getter that allows to abstract the fact that the
 * field is mandatory, optional, repeated, is a scalar, is a class, is a
 * reference, etc.
 *
 * Purpose: simplify tasks based on IOP introspection, for example systematic
 * treatments to apply to all fields of a given type.
 *
 * ┏━━━━━━━━━━━┯━━━━━━━━━━━━━━┳━━━━━━━━┯━━━━━━━━━━━━━━━━━━━━━━┓
 * ┃ repeat    │ type         ┃ len    │ is_array_of_pointers ┃
 * ┣━━━━━━━━━━━┿━━━━━━━━━━━━━━╋━━━━━━━━┿━━━━━━━━━━━━━━━━━━━━━━┫
 * ┃ MANDATORY │ *            ┃ 1      │ false                ┃
 * ┃ DEFAULT   │ *            ┃ 1      │ false                ┃
 * ┃ OPTIONAL  │ *            ┃ 0 or 1 │ false                ┃
 * ┃ REPEATED  │ struct/union ┃ N      │ false                ┃
 * ┃           │ class        ┃ N      │ true                 ┃
 * ┗━━━━━━━━━━━┷━━━━━━━━━━━━━━┻━━━━━━━━┷━━━━━━━━━━━━━━━━━━━━━━┛
 *
 * \param[in]  fdesc    Field description.
 *
 * \param[in]  st_ptr   Pointer on the struct containing the field.
 *
 * \param[out] values   Pointer on the value or the array of values. Can be an
 *                      simple array or an array of pointers depending on the
 *                      value in \p is_array_of_pointers. There is no memory
 *                      allocation, it points directly in the IOP data given
 *                      with \p st_ptr.
 *
 * \param[out] len      Number of values in the array (can be zero).
 *
 * \param[out] is_array_of_pointers  Indicates that the output array \p values
 *                                   is an array of pointers.
 */
void iop_get_field_values(const iop_field_t * nonnull fdesc,
                          void * nonnull st_ptr,
                          void * nullable * nonnull values, int * nonnull len,
                          bool * nullable is_array_of_pointers);

/** Read-only version of iop_get_field_values(). */
void iop_get_field_values_const(const iop_field_t * nonnull fdesc,
                                const void * nonnull st_ptr,
                                const void * nullable * nonnull values,
                                int * nonnull len,
                                bool * nullable is_array_of_pointers);

/** Return code for iop_value_from_field. */
typedef enum iop_value_from_field_res_t {
    IOP_FIELD_NOT_SET = -2,
    IOP_FIELD_ERROR   = -1,
    IOP_FIELD_SUCCESS = 0,
} iop_value_from_field_res_t;

/** Get an IOP value from an IOP field and an IOP object.
 *
 * \param[in]  ptr   The IOP object.
 * \param[in]  field The IOP field definition.
 * \param[out] value The value to put the result in.
 *
 * \return \ref iop_value_from_field_res_t.
 */
iop_value_from_field_res_t
iop_value_from_field(const void * nonnull ptr,
                     const iop_field_t * nonnull field,
                     iop_value_t * nonnull value);

/** Set a field of an IOP object from an IOP value and an IOP field.
 *
 * \param[in] ptr   The IOP object.
 * \param[in] field The IOP field definition.
 * \param[in] value The value to put the field.
 */
void iop_value_to_field(void * nonnull ptr, const iop_field_t * nonnull field,
                        const iop_value_t * nonnull value);

/** Set one of the values of a repeated IOP field of an IOP object.
 *
 * \param[in] ptr   The IOP object.
 * \param[in] field The IOP field definition.
 * \param[in] pos   The index at which the value \ref value should be set in
 *                  the repeated field \ref field.
 * \param[in] value The value to put at the \ref pos'th position in the
 *                  repeated field \ref field.
 */
int iop_value_to_repeated_field(void * nonnull ptr,
                                const iop_field_t * nonnull field,
                                uint32_t pos,
                                const iop_value_t * nonnull value);

/** Get the size of the binary encoding of a given IOP value.
 *
 * \param[in] value  The IOP value.
 *
 * \param[in] type   The type of the IOP value.
 *
 * \param[in] st_desc  The IOP description if the IOP value is a struct, union
 *                     or class.
 *
 * \return The size of the IOP binary encoding of the value.
 */
size_t iop_value_get_bpack_size(const iop_value_t * nonnull value,
                                iop_type_t type,
                                const iop_struct_t * nullable st_desc);

/** Get the size of the binary encoding of a length. */
size_t iop_get_len_bpack_size(uint32_t length);

/** Set an optional field of an IOP object.
 *
 * For optional scalar fields (integers, double, boolean, enum), this function
 * sets the `has_field` flag to true, without modifying the value.
 *
 * For string/data/xml fields, it ensures the `s` field is not NULL (setting
 * it to the empty string if needed).
 *
 * Other types of fields are not supported.
 *
 * \param[in] ptr   The IOP object.
 * \param[in] field The IOP field definition.
 */
void iop_set_opt_field(void * nonnull ptr, const iop_field_t * nonnull field);

/** Provide the appropriate arguments to the %*pU modifier.
 *
 * '%*pU' can be used in format string in order to print the selected field
 * type of the union given as an argument.
 *
 * \param[in]  pfx    IOP union descriptor prefix.
 * \param[in]  _val   The IOP union to print.
 */
#define IOP_UNION_FMT_ARG(pfx, val)                                          \
    ({ const pfx##__t *__val = (val); __val->iop_tag; }), &pfx##__s

/* }}} */
/* {{{ IOP snmp manipulation */

static inline bool iop_struct_is_snmp_obj(const iop_struct_t * nonnull st)
{
    unsigned st_flags = st->flags;

    return TST_BIT(&st_flags, IOP_STRUCT_IS_SNMP_OBJ);
}

static inline bool iop_struct_is_snmp_tbl(const iop_struct_t * nonnull st)
{
    unsigned st_flags = st->flags;

    return TST_BIT(&st_flags, IOP_STRUCT_IS_SNMP_TBL);
}

static inline bool iop_struct_is_snmp_st(const iop_struct_t * nonnull st)
{
    unsigned st_flags = st->flags;

    return TST_BIT(&st_flags, IOP_STRUCT_IS_SNMP_OBJ)
        || TST_BIT(&st_flags, IOP_STRUCT_IS_SNMP_TBL);
}

static inline bool iop_struct_is_snmp_param(const iop_struct_t * nonnull st)
{
    unsigned st_flags = st->flags;

    return TST_BIT(&st_flags, IOP_STRUCT_IS_SNMP_PARAM);
}

static inline bool iop_field_has_snmp_info(const iop_field_t * nonnull f)
{
    unsigned st_flags = f->flags;

    return TST_BIT(&st_flags, IOP_FIELD_HAS_SNMP_INFO);
}

static inline bool iop_iface_is_snmp_iface(const iop_iface_t * nonnull iface)
{
    unsigned st_flags = iface->flags;

    return TST_BIT(&st_flags, IOP_IFACE_IS_SNMP_IFACE);
}

static inline bool iop_field_is_snmp_index(const iop_field_t * nonnull field)
{
    unsigned st_flags = field->flags;

    return TST_BIT(&st_flags, IOP_FIELD_IS_SNMP_INDEX);
}

int iop_struct_get_nb_snmp_indexes(const iop_struct_t * nonnull st);

/** Get the number of SNMP indexes used by the AgentX layer (cf RFC RFC 2578).
 */
int iop_struct_get_nb_snmp_smiv2_indexes(const iop_struct_t * nonnull st);

const iop_snmp_attrs_t * nonnull
iop_get_snmp_attrs(const iop_field_attrs_t * nonnull attrs);
const iop_snmp_attrs_t * nonnull
iop_get_snmp_attr_match_oid(const iop_struct_t * nonnull st, int oid);
const iop_field_attrs_t * nonnull
iop_get_field_attr_match_oid(const iop_struct_t * nonnull st, int tag);

/* }}} */
/* {{{ IOP class manipulation */

static inline bool iop_struct_is_class(const iop_struct_t * nonnull st)
{
    unsigned st_flags = st->flags;

    return TST_BIT(&st_flags, IOP_STRUCT_IS_CLASS);
}

static inline bool iop_field_is_class(const iop_field_t * nonnull f)
{
    if (f->type != IOP_T_STRUCT) {
        return false;
    }
    return iop_struct_is_class(f->u1.st_desc);
}

/** Gets the value of a class variable (static field).
 *
 * This takes a class instance pointer and a class variable name, and returns
 * a pointer of the value of this class variable for the given object type.
 *
 * If the wanted static field does not exist in the given class, this
 * function will return NULL.
 *
 * It also assumes that the given pointer is a valid pointer on a valid class
 * instance. If not, it will probably crash...
 *
 * \param[in]  obj   Pointer on a class instance.
 * \param[in]  name  Name of the wanted class variable.
 */
__attr_nonnull__((1))
const iop_value_t * nullable
iop_get_cvar(const void * nonnull obj, lstr_t name);

#define iop_get_cvar_cst(obj, name)  iop_get_cvar(obj, LSTR(name))

/** Gets the value of a class variable (static field) from a class descriptor.
 *
 * Same as iop_get_cvar, but directly takes a class descriptor.
 */
__attr_nonnull__((1))
const iop_value_t * nullable
iop_get_cvar_desc(const iop_struct_t * nonnull desc, lstr_t name);

#define iop_get_cvar_desc_cst(desc, name)  \
    iop_get_cvar_desc(desc, LSTR(name))

/* The following variants of iop_get_cvar do not recurse on parents */
__attr_nonnull__((1))
const iop_value_t * nullable
iop_get_class_cvar(const void * nonnull obj, lstr_t name);

#define iop_get_class_cvar_cst(obj, name)  \
    iop_get_class_cvar(obj, LSTR(name))

__attr_nonnull__((1))
const iop_value_t * nullable
iop_get_class_cvar_desc(const iop_struct_t * nonnull desc, lstr_t name);

#define iop_get_class_cvar_desc_cst(desc, name)  \
    iop_get_class_cvar_desc(desc, LSTR(name))


/** Check if the static fields types are available for a given class.
 *
 * \param[in]  desc  pointer to the class descriptor
 *
 * \return  true if and only if the type of static fields can be read
 */
__attr_nonnull__((1))
static inline bool
iop_class_static_fields_have_type(const iop_struct_t * nonnull desc)
{
    unsigned flags = desc->flags;
    return TST_BIT(&flags, IOP_STRUCT_STATIC_HAS_TYPE);
}

/** Read the static field type if available.
 *
 * \param[in]  desc  pointer to the class descriptor containing
 *                   the static field
 * \param[in]  f     static field of which we want to read the type
 *
 * \return  the iop_type_t value of the static field type if available
 *          else -1
 */
__attr_nonnull__((1, 2))
static inline int
iop_class_static_field_type(const iop_struct_t * nonnull desc,
                            const iop_static_field_t * nonnull f)
{
    THROW_ERR_UNLESS(iop_class_static_fields_have_type(desc));
    return f->type;
}

/** Checks if a class has another class in its parents.
 *
 * \param[in]  cls1  Pointer on the first class descriptor.
 * \param[in]  cls2  Pointer on the second class descriptor.
 *
 * \return  true if \p cls1 is equal to \p cls2, or has \p cls2 in its parents
 */
__attr_nonnull__((1, 2))
bool iop_class_is_a(const iop_struct_t * nonnull cls1,
                    const iop_struct_t * nonnull cls2);

/** Checks if an object is of a given class or has it in its parents.
 *
 * If the result of this check is true, then this object can be cast to the
 * given type using iop_obj_vcast or iop_obj_ccast.
 *
 * \param[in]  obj   Pointer on a class instance.
 * \param[in]  desc  Pointer on a class descriptor.
 *
 * \return  true if \p obj is an object of class \p desc, or has \p desc in
 *          its parents.
 */
__attr_nonnull__((1, 2)) static inline bool
iop_obj_is_a_desc(const void * nonnull obj,
                  const iop_struct_t * nonnull desc)
{
    return iop_class_is_a(*(const iop_struct_t **)obj, desc);
}

#define iop_obj_is_a(obj, pfx)  ({                                           \
        typeof(*(obj)) *__obj = (obj);                                       \
                                                                             \
        assert (__obj->__vptr);                                              \
        iop_obj_is_a_desc((void *)(__obj), &pfx##__s);                       \
    })

/** Get the descriptor of a class from its fullname.
 *
 * The wanted class must have the same master class than the given class
 * descriptor.
 */
__attr_nonnull__((1)) const iop_struct_t * nullable
iop_get_class_by_fullname(const iop_struct_t * nonnull st, lstr_t fullname);

/** Get the descriptor of a class from its id.
 *
 * Manipulating class ids should be reserved to some very specific use-cases,
 * so before using this function, be SURE that you really need it.
 */
const iop_struct_t * nullable
iop_get_class_by_id(const iop_struct_t * nonnull st, uint16_t class_id);

#ifdef __has_blocks
typedef void (BLOCK_CARET iop_for_each_class_b)(const iop_struct_t * nonnull);

/** Loop on all the classes registered by `iop_register_packages`.
 */
void iop_for_each_registered_classes(iop_for_each_class_b nonnull cb);
#endif

/** Get the struct/class field after the given one.
 *
 * \note If the struct is a class, fields in children classes come before the
 *       ones in parent classes.
 *
 * \param[in] field  Current struct/class field.
 *                   If null, the first field of the struct/class will be
 *                   returned instead.
 *
 * \param[in, out] st  Description of the struct/class containing the field,
 *                     may be modified if the struct is a class and the next
 *                     field is in a parent class.
 *
 * \return NULL if there is no more field after the given one.
 */
const iop_field_t * nullable
iop_struct_get_next_field(const iop_field_t *nullable field,
                          const iop_struct_t *nonnull *nonnull st);

/* {{{ Private functions for iop_struct_for_each_field macro. */

/* Dig into class hierarchy to find the first parent class containing fields
 * in its own description. */
const iop_struct_t * nullable
_iop_class_first_non_empty_parent(const iop_struct_t * nonnull cls);

/* XXX The purpose of this static inline function is to keep the macro
 * 'iop_struct_for_each_field readable.
 *
 * We want the loop to be fully inline as long as we don't have to switch to a
 * parent struct to get next field (in that case, we use
 * '_iop_class_first_non_empty_parent').
 */
static inline const iop_field_t * nullable
_iop_struct_next_field(bool is_class, const iop_field_t *nullable field,
                       const iop_struct_t *nonnull *nonnull st)
{
    assert (is_class == iop_struct_is_class(*st));

    if (likely(field)) {
        field++;
    } else {
        field = (*st)->fields;
    }

    if (field >= (*st)->fields + (*st)->fields_len) {
        if (is_class) {
            (*st) = RETHROW_P(_iop_class_first_non_empty_parent(*st));
            field = (*st)->fields;
        } else {
            field = NULL;
        }
    }

    return field;
}

/* }}} */

/** Loop on all fields of a class or a struct.
 *
 * Less efficient than a simple loop for basic structs, it should be used when
 * the struct can be a class and when optimizing the case in which the struct
 * is not a class is not an important concern (~2.1x CPU overhead).
 *
 * Keywords \p break and \p continue are supported and work as they could be
 * expected to work by the user.

 * \warning The loop is not protected against modification of \p st or \p f
 *          iterating variables.
 *
 * \param[in] f   Name of the variable to create and use to store the field
 *                description pointer.
 *
 * \param[in] field_st  Name of the variable to create and use to store the
 *                      struct description containing the field (can differ
 *                      from \p st if \p st is a class).
 *
 * \param[in] st  Struct description in which we should iterate.
 */
#define iop_struct_for_each_field(f, field_st, st)                           \
    FOR_INSTR2(iop_struct_for_each_field_##f,                                \
               const iop_struct_t *field_st = (st),                          \
               bool __##field_st##_is_class = iop_struct_is_class(field_st)) \
    for (const iop_field_t *f = NULL;                                        \
         (f = _iop_struct_next_field(__##field_st##_is_class, f,             \
                                     &field_st));)

/** Iterate on all fields of the class of a given IOP object.
 *
 * \see \p iop_struct_for_each_field.
 */
#define iop_obj_for_each_field(f, st, _obj)                                  \
    iop_struct_for_each_field(f, st, (_obj)->__vptr)

/* }}} */
/* {{{ IOP array loops */

/* {{{ Internals, should not be used directly. */

#define __iop_array_for_each(st_desc, _obj, vec, len, _const)                \
    FOR_INSTR6(__iop_array_for_each##_obj,                                   \
               const struct iop_struct_t *_obj##__st = (st_desc),            \
               bool _obj##__is_pointer = iop_struct_is_class(_obj##__st),    \
               size_t _obj##__elem_size = _obj##__is_pointer                 \
                                        ? sizeof(void *)                     \
                                        : _obj##__st->size,                  \
               byte * nonnull _obj##__obj = (void *)(vec),                   \
               int _obj##__len = (len),                                      \
               _const void *_obj)                                            \
    for (int _obj##__i = 0;                                                  \
         _obj##__i < _obj##__len &&                                          \
         (_obj = _obj##__is_pointer ? *(void **)_obj##__obj : _obj##__obj);  \
         _obj##__i++,                                                        \
         _obj##__obj += _obj##__elem_size)

/* }}} */

/** Iterate untyped IOP array of structs, unions or classes.
 *
 * Always give a simple pointer on the element, no need to dereference the
 * pointer if the struct is a class.
 *
 * Should only be used when the IOP type is not known at compilation time,
 * otherwise, \ref tab_for_each_ptr and \ref tab_for_each_entry should be
 * preferred.
 *
 *  \param[in]  st_desc  The IOP structure definition (__s).
 *
 *  \param[in]  obj  The name of the current object. Allocated by the macro as
 *                   a `void *`. It points directly to the struct, the union
 *                   or the class.
 *
 *  \param[in]  vec  Array of struct/union/class instances. The data format
 *                   should be the same as the one for a genuine IOP array: we
 *                   expect it to be an array of inlined IOP instances for
 *                   IOP structs/unions and an array of object pointers for
 *                   IOP classes.
 *
 *  \param[in]  len  Length of the array.
 */
#define iop_array_for_each(st_desc, obj, vec, len)                           \
    __iop_array_for_each(st_desc, obj, vec, len,)

/** Same than \ref iop_array_for_each for tabs.
 *
 *  \param[in]  vec  A structured array with `tab` and `len` fields.
 */
#define iop_tab_for_each(st_desc, obj, vec)                                  \
    FOR_INSTR1(iop_tab_for_each##obj, typeof(vec) obj##__vec = (vec))        \
    iop_array_for_each(st_desc, obj, (obj##__vec)->tab, (obj##__vec)->len)

/** Const version of \ref iop_array_for_each.
 *
 *  \param[in]  obj  The object as a `const void *`.
 */
#define iop_array_for_each_const(st_desc, obj, vec, len)                     \
    __iop_array_for_each(st_desc, obj, vec, len, const)

/** Const version of \ref iop_tab_for_each.
 *
 *  \param[in]  obj  The object as a `const void *`.
 */
#define iop_tab_for_each_const(st_desc, obj, vec)                            \
    FOR_INSTR1(iop_tab_for_each##obj, typeof(vec) obj##__vec = (vec))        \
    iop_array_for_each(st_desc, obj, (obj##__vec)->tab, (obj##__vec)->len)


/* }}} */
/* {{{ IOP constraints handling */

/** Get the constraints error buffer.
 *
 * When a structure constraints checking fails, the error description is
 * accessible in a static buffer, accessible with this function.
 */
const char * nullable iop_get_err(void) __cold;

/** Same as iop_get_err() but returns a lstr_t. */
lstr_t iop_get_err_lstr(void) __cold;

/** Check the constraints of an IOP structure.
 *
 * This function will check the constraints on an IOP structure and will
 * return -1 in case of constraint violation. In case of constraint violation,
 * you can use iop_get_err() to get the error message.
 *
 * Prefer the macro version iop_check_constraints() instead of this low-level
 * API.
 *
 * \param[in] desc  IOP structure description.
 * \param[in] val   Pointer on the IOP structure to check constraints.
 */
int iop_check_constraints_desc(const iop_struct_t * nonnull desc,
                               const void * nonnull val);

#define iop_check_constraints(pfx, val)  ({                                  \
        const pfx##__t *__v = (val);                                         \
                                                                             \
        iop_check_constraints_desc(&pfx##__s, (const void *)__v);            \
    })

/* }}} */
/* {{{ IOP enum manipulation */

qm_kvec_t(iop_enum, lstr_t, const iop_enum_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

/** Get an enumeration from its fullname. */
const iop_enum_t * nullable iop_get_enum(lstr_t fullname);

typedef enum iop_obj_type_t {
    /* Struct/union/class. */
    IOP_OBJ_TYPE_ST,

    /* Enum. */
    IOP_OBJ_TYPE_ENUM,

    /* IOP package. */
    IOP_OBJ_TYPE_PKG,
} iop_obj_type_t;

typedef struct iop_obj_t {
    iop_obj_type_t type;

    union {
        const iop_struct_t *nonnull st;
        const iop_enum_t *nonnull en;
        const iop_pkg_t *nonnull pkg;
    } desc;

    /* Cached ancestor for classes (purpose: optimize calls to
     * iop_get_class_by_fullname()). */
    const iop_struct_t *nullable ancestor;
} iop_obj_t;

/** Get an union/struct/class/enum from its fullname. */
const iop_obj_t *nullable iop_get_obj(lstr_t fullname);

/** Convert IOP enum integer value to lstr_t representation.
 *
 * This function will return NULL if the integer value doesn't exist in the
 * enum set.
 *
 * Prefer the macro version iop_enum_to_lstr() instead of this low-level API.
 *
 * \param[in] ed The IOP enum definition (__e).
 * \param[in] v  Integer value to look for.
 */
static inline lstr_t iop_enum_to_str_desc(const iop_enum_t * nonnull ed, int v)
{
    int res = iop_ranges_search(ed->ranges, ed->ranges_len, v);
    return unlikely(res < 0) ? LSTR_NULL_V : ed->names[res];
}

#define iop_enum_to_lstr(pfx, v)  ({                                         \
        const pfx##__t _etl_v = (v);                                         \
                                                                             \
        iop_enum_to_str_desc(&pfx##__e, _etl_v);                             \
    })
#define iop_enum_to_str(pfx, v)  iop_enum_to_lstr(pfx, (v)).s

static inline bool iop_enum_exists_desc(const iop_enum_t * nonnull ed, int v)
{
    return iop_ranges_search(ed->ranges, ed->ranges_len, v) >= 0;
}

#define iop_enum_exists(pfx, v)  ({                                          \
        const pfx##__t _ee_v = (v);                                          \
                                                                             \
        iop_enum_exists_desc(&pfx##__e, _ee_v);                              \
    })

/** Convert a string to its integer value using an IOP enum mapping.
 *
 * This function will return `err` if the string value doesn't exist in the
 * enum set.
 *
 * Prefer the macro version iop_enum_from_str() instead of this low-level API.
 *
 * \param[in] ed  The IOP enum definition (__e).
 * \param[in] s   String value to look for.
 * \param[in] len String length (or -1 if unknown).
 * \param[in] err Value to return in case of conversion error.
 */
int iop_enum_from_str_desc(const iop_enum_t * nonnull ed,
                           const char * nonnull s, int len,
                           int err);

#define iop_enum_from_str(pfx, s, len, err)  \
    iop_enum_from_str_desc(&pfx##__e, (s), (len), (err))

/** Convert a string to its integer value using an IOP enum mapping.
 *
 * This function will return `-1` if the string value doesn't exist in the
 * enum set and set the `found` variable to false.
 *
 * Prefer the macro version iop_enum_from_str2() instead of this low-level
 * API.
 *
 * \param[in]  ed    The IOP enum definition (__e).
 * \param[in]  s     String value to look for.
 * \param[in]  len   String length (or -1 if unknown).
 * \param[out] found Will be set to false upon failure, true otherwise.
 */
int iop_enum_from_str2_desc(const iop_enum_t * nonnull ed,
                            const char * nonnull s, int len,
                            bool * nonnull found);

#define iop_enum_from_str2(pfx, s, len, found)  \
    iop_enum_from_str2_desc(&pfx##__e, (s), (len), (found))

/** Convert a lstr_t to its integer value using an IOP enum mapping.
 *
 * This function will return `-1` if the string value doesn't exist in the
 * enum set and set the `found` variable to false.
 *
 * Prefer the macro version iop_enum_from_lstr() instead of this low-level
 * API.
 *
 * \param[in]  ed    The IOP enum definition (__e).
 * \param[in]  s     String value to look for.
 * \param[in]  len   String length (or -1 if unknown).
 * \param[out] found Will be set to false upon failure, true otherwise.
 */
int iop_enum_from_lstr_desc(const iop_enum_t * nonnull ed,
                            const lstr_t s, bool * nonnull found);

#define iop_enum_from_lstr(pfx, s, found)  \
    iop_enum_from_lstr_desc(&pfx##__e, (s), (found))


/** Find a generic attribute value for an IOP enum.
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  ed    The IOP enum definition (__e).
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_enum_get_gen_attr(const iop_enum_t * nonnull ed, lstr_t key,
                          iop_type_t exp_type, iop_type_t * nullable val_type,
                          iop_value_t * nonnull value);

/** Find a generic attribute value for an IOP enum value (integer).
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  ed    The IOP enum definition (__e).
 * \param[in]  val   The enum value (integer).
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_enum_get_gen_attr_from_val(const iop_enum_t * nonnull ed, int val,
                                   lstr_t key, iop_type_t exp_type,
                                   iop_type_t * nullable val_type,
                                   iop_value_t * nonnull value);

/** Find a generic attribute value for an IOP enum value (string).
 *
 * See \ref iop_field_get_gen_attr for the description of exp_type and
 * val_type.
 *
 * \param[in]  ed    The IOP enum definition (__e).
 * \param[in]  val   The enum value (string).
 * \param[in]  key   The generic attribute key.
 * \param[in]  exp_type  The expected value type.
 * \param[out] val_type  The actual value type.
 * \param[out] value The value to put the result in.
 *
 * \return 0 if the generic attribute is found, -1 otherwise.
 */
int iop_enum_get_gen_attr_from_str(const iop_enum_t * nonnull ed, lstr_t val,
                                   lstr_t key, iop_type_t exp_type,
                                   iop_type_t * nullable val_type,
                                   iop_value_t * nonnull value);

/** Private intermediary structure for IOP enum formatting. */
struct iop_enum_value {
    const iop_enum_t * nonnull desc;
    int v;
};

/** Flag to use in order to display enums the following way:
 *  "<litteral value>(<int value>)".
 *
 *  Examples: "FOO(0)", "BAR(1)".
 */
#define IOP_ENUM_FMT_FULL  (1 << 0)

/** Provide the appropriate arguments to the %*pE modifier.
 *
 * '%*pE' can be used in format string in order to print enum values. An
 * additional flag \ref IOP_ENUM_FMT_FULL can be provided to display both
 * litteral and integer values.
 *
 * \param[in]  pfx    IOP enum descriptor prefix.
 * \param[in]  _val   The IOP enum value to print.
 * \param[in]  _flags The IOP enum formatting flags.
 */
#define IOP_ENUM_FMT_ARG_FLAGS(pfx, _val, _flags)                            \
    _flags,                                                                  \
    &(struct iop_enum_value){                                                \
        .desc = &pfx##__e,                                                   \
        .v = ({ pfx##__t _v = (_val); _v; })                                 \
    }

/** Same as \ref IOP_ENUM_FMT_ARG_FLAGS but with explicit description pointer.
 */
#define IOP_ENUM_DESC_FMT_ARG_FLAGS(_desc, _val, _flags)                     \
    (_flags),                                                                \
    &(struct iop_enum_value){                                                \
        .desc = (_desc),                                                     \
        .v = (_val),                                                         \
    }

/** Provide the appropriate arguments to print the litteral form of an enum
 *  with the %*pE modifier.
 *
 * \note If the value has no litteral form, the numeric value will be printed
 *       instead.
 *
 * \param[in]  pfx    IOP enum descriptor prefix.
 * \param[in]  _val   The IOP enum value to print.
 */
#define IOP_ENUM_FMT_ARG(pfx, _v)  IOP_ENUM_FMT_ARG_FLAGS(pfx, _v, 0)

/* }}} */
/* {{{ IOP binary packing/unpacking */

/** Set the multithreaded packing threshold, for testing purposes.
 *
 * \param[in]  threshold  Arrays smaller than this be packed in threads.
 */
void iop_bpack_set_threaded_threshold(size_t threshold);

/** IOP binary packer modifiers. */
enum iop_bpack_flags {
    /** With this flag on, the values still equal to their default will not be
     * packed. This is good to save bandwidth but dangerous for backward
     * compatibility */
    IOP_BPACK_SKIP_DEFVAL   = (1U << 0),

    /** With this flag on, packing can fail if the constraints are not
     * respected. The error message is available with iop_get_err. */
    IOP_BPACK_STRICT        = (1U << 1),

    /** With this flag on, packing will omit private fields.
     */
    IOP_BPACK_SKIP_PRIVATE  = (1U << 2),

    /** With this flag on, packing will not be multi-threaded.
     */
    IOP_BPACK_MONOTHREAD    = (1U << 3),
};

/** Do some preliminary work to pack an IOP structure into IOP binary format.
 *
 * This function _must_ be used before the `iop_bpack` function. It will
 * compute some necessary informations.
 *
 * \param[in]  st    The IOP structure definition (__s).
 * \param[in]  v     The IOP structure to pack.
 * \param[in]  flags Packer modifiers (see iop_bpack_flags).
 * \param[out] szs   A qvector of int32 that you have to initialize and give
 *                   after to `iop_bpack`.
 * \return
 *   This function returns the needed buffer size to pack the IOP structure,
 *   or -1 if the IOP_BPACK_STRICT flag was used and a constraint was
 *   violated.
 */
__must_check__
int iop_bpack_size_flags(const iop_struct_t * nonnull st,
                         const void * nonnull v,
                         unsigned flags, qv_t(i32) * nonnull szs);

__must_check__
static inline size_t
iop_bpack_size(const iop_struct_t * nonnull st, const void * nonnull v,
               qv_t(i32) * nonnull szs)
{
    return iop_bpack_size_flags(st, v, 0, szs);
}


/** Pack an IOP structure into IOP binary format.
 *
 * This structure pack a given IOP structure in an existing buffer that need
 * to be big enough. The required size will be given by the `iop_bpack_size`.
 *
 * Common usage:
 *
 * <code>
 * qv_t(i32) sizes;
 * int len;
 * byte *data;
 *
 * qv_inita(&sizes, 1024);
 *
 * len  = iop_bpack_size(&foo__bar__s, obj, &sizes);
 * data = p_new_raw(byte, len);
 *
 * iop_bpack(data, &foo__bar__s, obj, sizes.tab);
 * </code>
 *
 * \param[in] st  The IOP structure definition (__s).
 * \param[in] v   The IOP structure to pack.
 * \param[in] szs The data of the qvector given to the `iop_bpack_size`
 *                function.
 */
void iop_bpack(void * nonnull dst, const iop_struct_t * nonnull st,
               const void * nonnull v, const int * nonnull szs);

/** Pack an IOP structure into IOP binary format using a specific mempool.
 *
 * This version of `iop_bpack` allows to pack an IOP structure in one
 * operation and uses the given mempool to allocate the resulting buffer.
 *
 * \param[in] st    The IOP structure definition (__s).
 * \param[in] v     The IOP structure to pack.
 * \param[in] flags Packer modifiers (see iop_bpack_flags).
 * \return
 *   The buffer containing the packed structure, or LSTR_NULL if the
 *   IOP_BPACK_STRICT flag was used and a constraint was violated.
 */
lstr_t mp_iop_bpack_struct_flags(mem_pool_t * nullable mp,
                                 const iop_struct_t * nonnull st,
                                 const void * nonnull v, const unsigned flags);

/** Pack an IOP structure into IOP binary format using the t_pool().
 */
lstr_t t_iop_bpack_struct_flags(const iop_struct_t * nonnull st,
                                const void * nonnull v,
                                const unsigned flags);

static inline lstr_t t_iop_bpack_struct(const iop_struct_t * nonnull st,
                                        const void * nonnull v)
{
    return t_iop_bpack_struct_flags(st, v, 0);
}

/** Flags for IOP (un)packers. */
enum iop_unpack_flags {
    /** Allow the unpacker to skip unknown fields.
     *
     * This flag applies to the json, yaml and xml packers.
     */
    IOP_UNPACK_IGNORE_UNKNOWN = (1U << 0),

    /** Make the unpacker reject private fields.
     *
     * This flag applies to the binary, json, yaml and xml packers.
     */
    IOP_UNPACK_FORBID_PRIVATE = (1U << 1),

    /** With this flag, packing will copy strings instead of making them
     * point to the packed value when possible.
     *
     * This flag applies to the binary unpacker.
     */
    IOP_UNPACK_COPY_STRINGS    = (1U << 2),

    /** With this flag set, the unpacker will expect the fields names to be
     * in C case instead of camelCase.
     *
     * This flag applies to the json unpacker.
     */
    IOP_UNPACK_USE_C_CASE = (1U << 3),
};

/** Unpack a packed IOP structure.
 *
 * This function unpacks a packed IOP structure from a pstream_t. It unpacks
 * one and only one structure, so the pstream_t must only contain the unique
 * structure to unpack.
 *
 * This function cannot be used to unpack a class; use `iop_bunpack_ptr`
 * instead.
 *
 * \warning If needed, iop_bunpack will allocate memory for each field. So if
 * the mem pool is not frame based, you may end up with a memory leak.
 *
 * \param[in] mp    The memory pool to use when memory allocation is needed.
 * \param[in] st    The IOP structure definition (__s).
 * \param[in] value Pointer on the destination structure.
 * \param[in] ps    The pstream_t containing the packed IOP structure.
 * \param[in] flags A combination of \ref iop_unpack_flags to alter the
 *                  behavior of the unpacker.
 */
__must_check__
int iop_bunpack_flags(mem_pool_t * nonnull mp,
                      const iop_struct_t * nonnull st,
                      void * nonnull value,
                      pstream_t ps, unsigned flags);

__must_check__
static inline int iop_bunpack(mem_pool_t * nonnull mp,
                              const iop_struct_t * nonnull st,
                              void * nonnull value, pstream_t ps, bool copy)
{
    return iop_bunpack_flags(mp, st, value, ps,
                             copy ? IOP_UNPACK_COPY_STRINGS : 0);
}

/** Unpack a packed IOP structure using the t_pool().
 */
__must_check__ static inline int
t_iop_bunpack_ps(const iop_struct_t * nonnull st, void * nonnull value,
                 pstream_t ps, bool copy)
{
    return iop_bunpack(t_pool(), st, value, ps, copy);
}

/** Unpack a packed IOP object and (re)allocates the destination structure.
 *
 * This function acts as `iop_bunpack` but allocates (or reallocates) the
 * destination structure.
 *
 * This function MUST be used to unpack a class instead of `iop_bunpack`,
 * because the size of a class is not known before unpacking it (this could be
 * a child).
 *
 * \warning If needed, iop_bunpack will allocate memory for each field. So if
 * the mem pool is not frame based, you may end up with a memory leak.
 *
 * \param[in] mp    The memory pool to use when memory allocation is needed;
 *                  will be used at least to allocate the destination
 *                  structure.
 * \param[in] st    The IOP structure/class definition (__s).
 * \param[in] value Double pointer on the destination structure.
 *                  If *value is not NULL, it is reallocated.
 * \param[in] ps    The pstream_t containing the packed IOP object.
 * \param[in] flags A combination of \ref iop_unpack_flags to alter the
 *                  behavior of the unpacker.
 */
__must_check__
int iop_bunpack_ptr_flags(mem_pool_t * nonnull mp,
                          const iop_struct_t * nonnull st,
                          void * nullable * nonnull value, pstream_t ps,
                          unsigned flags);

__must_check__
static inline int iop_bunpack_ptr(mem_pool_t * nonnull mp,
                                  const iop_struct_t * nonnull st,
                                  void * nullable * nonnull value,
                                  pstream_t ps, bool copy)
{
    return iop_bunpack_ptr_flags(mp, st, value, ps,
                                 copy ? IOP_UNPACK_COPY_STRINGS : 0);
}

/** Unpack a packed IOP union.
 *
 * This function act like `iop_bunpack` but consume the pstream and doesn't
 * check that the pstream has been fully consumed. This allows to unpack
 * a suite of unions.
 *
 * \param[in] mp    The memory pool to use when memory allocation is needed.
 * \param[in] st    The IOP structure definition (__s).
 * \param[in] value Pointer on the destination unpacked IOP union.
 * \param[in] ps    The pstream_t containing the packed IOP union. In case of
 *                  unpacking failure, it is left untouched.
 * \param[in] flags A combination of \ref iop_unpack_flags to alter the
 *                  behavior of the unpacker.
 */
__must_check__
int iop_bunpack_multi_flags(mem_pool_t * nonnull mp,
                            const iop_struct_t * nonnull st,
                            void * nonnull value, pstream_t * nonnull ps,
                            unsigned flags);

__must_check__
static inline int iop_bunpack_multi(mem_pool_t * nonnull mp,
                                    const iop_struct_t * nonnull st,
                                    void * nonnull value,
                                    pstream_t * nonnull ps, bool copy)
{
    return iop_bunpack_multi_flags(mp, st, value, ps,
                                   copy ? IOP_UNPACK_COPY_STRINGS : 0);
}

/** Unpack a packed IOP union using the t_pool().
 */
__must_check__ static inline int
t_iop_bunpack_multi(const iop_struct_t * nonnull st, void * nonnull value,
                    pstream_t * nonnull ps, bool copy)
{
    return iop_bunpack_multi(t_pool(), st, value, ps, copy);
}

/** Skip a packed IOP union without unpacking it.
 *
 * This function skips a packed IOP union in a pstream_t.
 * This function is efficient because it will not fully unpack the union to
 * skip. But it will not fully check its validity either.
 *
 * \param[in] st    The IOP union definition (__s).
 * \param[in] ps    The pstream_t containing the packed IOP union.
 */
__must_check__
int iop_bskip(const iop_struct_t * nonnull st, pstream_t * nonnull ps);

/** returns the length of the field examining the first octets only.
 *
 * \warning the field should be a non repeated one.
 *
 * This function is meant to know if iop_bunpack{,_multi} will work, hence it
 * should be applied to stream of IOP unions or IOP fields only.
 *
 * Returns 0 if there isn't enough octets to determine the length.
 * Returns -1 if there is something really wrong.
 */
ssize_t iop_get_field_len(pstream_t ps);

/** Write a union tag into a struct.
  *
  * Starting with --c-unions-use-enums, iop_tag field in unions struct can now
  * have a different size than uint16_t (8 or 32 bits).
  *
  * Write this iop_tag in a safe and backward compatible way by checking the
  * offsets of union fields.
  *
  * \param[in] desc  The IOP structure definition (__s).
  * \param[in] value The union iop_tag to write.
  * \param[out] st   A pointer to the IOP structure (union only) to fill.
  */
void iop_union_set_tag(const iop_struct_t *nonnull desc, int value,
                       void *nonnull st);

/** Read a union tag from a struct.
 *
 * Read the iop_tag (undetermined size) into an int, based on the offset of
 * the union fields.
 *
 * \param[in] desc The IOP structure definition (__s).
 * \param[in] st   A pointer to the IOP structure (union only) to read.
 *
 * Returns a positive integer (0-uint16_max) on success.
 * Returns -1 if something wrong happened.
 */
int iop_union_get_tag(const iop_struct_t *nonnull desc,
                      const void *nonnull st);

/* }}} */
/* {{{ IOP packages registration / manipulation */

qm_kvec_t(iop_pkg, lstr_t, const iop_pkg_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

const iop_pkg_t * nullable iop_get_pkg(lstr_t pkgname);

enum iop_register_packages_flags {
    IOP_REGPKG_FROM_DSO = (1U << 0),
};

/** Register a list of packages.
 *
 * Registering a package is necessary if it contains classes; this should be
 * done before trying to pack/unpack any class.
 * This will also perform collision checks on class ids, which cannot be made
 * at compile time.
 *
 * You can use IOP_REGISTER_PACKAGES to avoid the array construction.
 */
void iop_register_packages(const iop_pkg_t * nonnull * nonnull pkgs, int len,
                           unsigned flags);

/** Helper to register a list of packages.
 *
 * Just an helper to call iop_register_packages without having to build an
 * array.
 */
#define IOP_REGISTER_PACKAGES(...)  \
    do {                                                                     \
        const iop_pkg_t *__pkgs[] = { __VA_ARGS__ };                         \
        iop_register_packages(__pkgs, countof(__pkgs), 0);                   \
    } while (0)

/** Unregister a list of packages.
 *
 * Note that unregistering a package at shutdown is NOT necessary.
 * This function is used by the DSO module, and there is no reason to use it
 * somewhere else.
 *
 * You can use IOP_UNREGISTER_PACKAGES to avoid the array construction.
 */
void iop_unregister_packages(const iop_pkg_t * nonnull * nonnull pkgs,
                             int len);

/** Helper to unregister a list of packages.
 *
 * Just an helper to call iop_unregister_packages without having to build an
 * array.
 */
#define IOP_UNREGISTER_PACKAGES(...)  \
    do {                                                                     \
        const iop_pkg_t *__pkgs[] = { __VA_ARGS__ };                         \
        iop_unregister_packages(__pkgs, countof(__pkgs));                    \
    } while (0)

#ifdef __has_blocks
typedef void (BLOCK_CARET iop_for_each_pkg_b)(const iop_pkg_t * nonnull);

/** Loop on all the pkg registered by `iop_register_packages`.
 */
void iop_for_each_registered_pkgs(iop_for_each_pkg_b nonnull cb);
#endif

/* }}} */
/* {{{ IOP backward compatibility checks */

enum iop_compat_check_flags {
    IOP_COMPAT_BIN  = (1U << 0),
    IOP_COMPAT_JSON = (1U << 1),
    /* TODO: XML */
    IOP_COMPAT_ALL  = IOP_COMPAT_BIN | IOP_COMPAT_JSON,
};

/** IOP backward compatibility context.
 *
 * A context contains checker metadata. Among other things it allows the
 * checker to skip structs that have been already verified.
 */
typedef struct iop_compat_ctx_t iop_compat_ctx_t;

iop_compat_ctx_t * nonnull iop_compat_ctx_new(void);
void iop_compat_ctx_delete(iop_compat_ctx_t * nullable * nonnull ctx);

/** Checks the backward compatibility of two IOP structures/classes/unions.
 *
 * This function checks if \p st2 is backward-compatible with \p st1 regarding
 * the formats specified in \p flags, that is if any \p st1 packed
 * structure/class/union can be safely unpacked as a \p st2.
 *
 * \p flags are a combination of \ref iop_compat_check_flags.
 *
 * \warning in case \p st1 and \p st2 are classes, it is not checking the
 *          backward compatibility of their children.
 */
int iop_struct_check_backward_compat(const iop_struct_t * nonnull st1,
                                     const iop_struct_t * nonnull st2,
                                     unsigned flags, sb_t * nonnull err);

/** Checks the backward compatibility of two IOP packages.
 *
 * This function checks if \p pkg2 is backward-compatible with \p pkg1
 * regarding the formats specified in \p flags, that is if any
 * packed structure/class/union of \p st1 can be safely unpacked using
 * structure/class/union defined in \p st2.
 *
 * The names of the structures/classes/unions must not change between \p pkg1
 * and \p pkg2.
 *
 * \warning this function does not check the interfaces/RPCs for now.
 */
int iop_pkg_check_backward_compat(const iop_pkg_t * nonnull pkg1,
                                  const iop_pkg_t * nonnull pkg2,
                                  unsigned flags, sb_t * nonnull err);

/** Checks the backward compatibility of two IOP packages with provided
 * context.
 *
 * \see iop_pkg_check_backward_compat
 *
 * This function introduce a way to provide an external compatibility context
 * \p ctx allowing backward compatibility checks between multiple packages.
 */
int iop_pkg_check_backward_compat_ctx(const iop_pkg_t * nonnull pkg1,
                                      const iop_pkg_t * nonnull pkg2,
                                      iop_compat_ctx_t * nonnull ctx,
                                      unsigned flags, sb_t * nonnull err);

/** Get whether a struct is optional or not.
 *
 * Get whether a struct is optional or not. A struct is optional if it
 * contains no mandatory fields (ie. it only contains arrays, optional fields
 * or fields with a default value).
 *
 * If \ref check_parents is false, parent classes are not checked if \ref st
 * is a class.
 */
bool iop_struct_is_optional(const iop_struct_t *nonnull st,
                            bool check_parents);

/* }}} */

/** Module that handles IOP registration data.
 */
MODULE_DECLARE(iop);

void iop_module_register(void);

#include "iop/macros.h"
#include "iop/xml.h"
#include "iop/dso.h"
#include "iop/core-obj.h"

#if __has_feature(nullability)
#pragma GCC diagnostic pop
#endif

#endif
