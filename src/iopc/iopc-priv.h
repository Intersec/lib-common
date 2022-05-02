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

#ifndef IS_IOP_IOPC_PRIV_H
#define IS_IOP_IOPC_PRIV_H

#include "iopc.h"

/* {{{ IOP Parser */

/** Checks that an IOP tag has an authorized value. */
int iopc_check_tag_value(int tag, sb_t *err);

/** Check for type incompatibilities in an IOPC field. */
int iopc_check_field_type(const iopc_field_t *f, sb_t *err);

/* }}} */
/* {{{ Typer. */

bool iopc_field_type_is_class(const iopc_field_t *f);

/* }}} */
/* {{{ C Language */

/** Create an 'iop_pkg_t' from an 'iopc_pkg_t'.
 *
 * \warning The types must be resolved by the typer first.
 *
 * \param[in,out] mp  The memory pool for all needed allocations. Must be a
 *                    by-frame memory pool (flag MEM_BY_FRAME set).
 */
iop_pkg_t *mp_iopc_pkg_to_desc(mem_pool_t *mp, iopc_pkg_t *pkg, sb_t *err);

/* }}} */

#endif /* IS_IOP_IOPC_PRIV_H */
