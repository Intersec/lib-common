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

#ifndef IS_IOP_IOPC_IOP_H
#define IS_IOP_IOPC_IOP_H

#include "iopc.h"
#include "iopsq.iop.h"

#include <lib-common/src/iop/priv.h>

/* IOP² - An IOP-based library for IOP generation.
 *
 * Tools for dynamic generation of usable IOP content.
 *
 *  iopsq.Package ── 1 ──→ iopc_pkg_t ── 2,3 ──→ iop_pkg_t
 *
 * 1. Building of an iopc package from an IOP² package. The code is in
 *    iopc-iopsq.c and it works in a similar way as iopc-parser.
 *    The package can refer to types from IOP environment: builtin IOP types
 *    can be used and have the expected pointer value (&foo__bar__s).
 *
 * 2. Resolution of IOP types with iopc-typer.
 *
 * 3. Generation of an IOP C package description. The code is in iopc-lang-c.c
 *    because it does quite the same as the code generator except that it
 *    directly generates the structures. This is the part that uses the
 *    provided memory pool.
 *
 * Limitations:
 *
 *    - Default values: default values exist in iopsq.iop and are correctly
 *    transformed at step 1., but not at step 3 (yet).
 *
 *    - Not supported yet: classes, attributes, modules, interfaces, typedefs,
 *    RPCs, SNMP objects.
 *
 *    - Typedefs from IOPs loaded to the environment cannot be used by
 *    refering to them with "typeName" like for the other types because
 *    typedefs cease to exist in packge descriptions iop_pkg_t, so they are
 *    missing in the IOP environment too.
 *
 *    - Dynamic transformation of IOP syntax (mypackage.iop) into IOP
 *    description without having to create a DSO: already possible but no
 *    helper is provided to do that. The step 3. should be protected against
 *    unsupported features first, because for now, most of them would be
 *    silently ignored.
 *
 *   - No tool to "extract" an IOP² description of an IOP type for now. This
 *   feature could be useful for versioning and migration of IOP objects.
 *
 *   - Sub-packages and multi-package loading.
 */

/* {{{ IOP² building helpers */
/* {{{ Basic builders */

qvector_t(iopsq_field, iop__field__t);
qvector_t(iopsq_enum_val, iop__enum_val__t);

/** Fill an IOP² type from an \p iop_type_t.
 *
 * \return -1 if the base type is not enough (for \ref IOP_T_ENUM,
 *         \ref IOP_T_STRUCT or \ref IOP_T_UNION).
 */
int iop_type_to_iop(iop_type_t type, iop__type__t *out);

/* }}} */
/* {{{ Type table */

/** Type table: allows to use custom IOP structs/enums in IOP² descriptions.
 */
typedef struct iopsq_type_table_t iopsq_type_table_t;

/* Low-level constructor/destructor. Should not be used directly. */
iopsq_type_table_t *__iopsq_type_table_new(void);
void __iopsq_type_table_delete(iopsq_type_table_t **table);

/** Create a scope-bound IOP² type table. */
#define IOPSQ_TYPE_TABLE(name)                                               \
    iopsq_type_table_t *name                                                 \
    __attribute__((cleanup(__iopsq_type_table_delete))) =                    \
        __iopsq_type_table_new()

/** Build an iopsq.Type instance from an 'iop_full_type_t'.
 *
 * If the \p iop_full_type_t is an enum or struct/union/class that is not
 * present in the IOP environment, the table will register it and pair it with
 * an ID, otherwise, it will set the IOP² type accordingly to the input type.
 *
 * \param[in, out] table  The type table.
 *
 * \param[in]      ftype  The complete field type (with \p en or \p st filled
 *                        appropriately).
 *
 * \param[out]     type   The IOP² type.
 */
void iopsq_type_table_fill_type(iopsq_type_table_t *table,
                                const iop_full_type_t *ftype,
                                iop__type__t *type);

/* }}} */
/* }}} */

/** Generates an IOP package description from its IOP version.
 *
 * \warning This function can use elements from current IOP environment
 * (referenced by full type name), so the environment should *not* be updated
 * during the lifetime of an IOP description obtained with this function.
 *
 * \param[in,out] mp          Memory pool to use for any needed allocation
 *                            (should be a frame-based pool).
 *
 * \param[in]     pkg_desc    IOP description of the package.
 *
 * \param[in]     type_table  Table for custom IOP types.
 *
 * \param[out]    err         Error buffer.
 */
iop_pkg_t *mp_iopsq_build_pkg(mem_pool_t *nonnull mp,
                              const iop__package__t *nonnull pkg_desc,
                              const iopsq_type_table_t *nullable type_table,
                              sb_t *nonnull err);

/** Generates an IOP struct or union description from its IOP version.
 *
 * \warning Same as for \ref mp_iopsq_build_pkg.
 *
 * \param[in,out] mp          Memory pool to use for any needed allocation
 *                            (should be a frame-based pool).
 *
 * \param[in]     st_desc     IOP description of the struct/union.
 *
 * \param[in]     type_table  Table for custom IOP types.
 *
 * \param[out]    err         Error buffer.
 */
const iop_struct_t *
mp_iopsq_build_struct(mem_pool_t *nonnull mp,
                      const iopsq__structure__t *nonnull st_desc,
                      const iopsq_type_table_t *nullable type_table,
                      sb_t *nonnull err);

/** Generates an dumb IOP package from a single package elem description.
 *
 * \note Mainly meant to be used for testing.
 */
iop_pkg_t *
mp_iopsq_build_mono_element_pkg(mem_pool_t *nonnull mp,
                                const iop__package_elem__t *nonnull elem,
                                const iopsq_type_table_t *nullable type_table,
                                sb_t *nonnull err);

/* {{{ Helper: iopsq_iop_struct_t */

typedef struct iopsq_iop_struct_t {
    const iop_struct_t *st;
/* {{{ Internal: used to allocate and wipe the above iop_struct_t object. */
    mem_pool_t *mp;
    const void *release_cookie;
/* }}} */
} iopsq_iop_struct_t;

GENERIC_INIT(iopsq_iop_struct_t, iopsq_iop_struct);


/** Build iopsq_iop_struct_t object.
 *
 * Same as calling \ref mp_iopsq_build_struct except that the mempool is
 * handled in iopsq_iop_struct_t.
 * \warning Same as for \ref mp_iopsq_build_pkg.
 *
 * \param[out] st          The iopsq_iop_struct_t object to build. Must be
 *                         first initialized with iopsq_iop_struct_init.
 * \param[in]  st_desc     IOP description of the struct/union.
 * \param[in]  type_table  Table for custom IOP types.
 * \param[out] err         Error buffer.
 *
 * \return -1 in case of error, 0 otherwise.
 */
int iopsq_iop_struct_build(iopsq_iop_struct_t *nonnull st,
                           const iopsq__structure__t *nonnull st_desc,
                           const iopsq_type_table_t *nullable type_table,
                           sb_t *nonnull err);

/** Wipe an iopsq_iop_struct_t object.
 *
 * \param[in,out] st The iopsq_iop_struct_t object to wipe.
 */
void iopsq_iop_struct_wipe(iopsq_iop_struct_t *nonnull st);

/* }}} */
/* {{{ Private helpers */

static inline iopsq__int_size__t iopsq_int_type_to_int_size(iop_type_t type)
{
    assert (type <= IOP_T_U64);
    return (iopsq__int_size__t)(type >> 1);
}

/* }}} */

#endif /* IS_IOP_IOPC_IOP_H */
