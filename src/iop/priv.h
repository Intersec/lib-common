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

#ifndef IS_LIB_COMMON_IOP_PRIV_H
#define IS_LIB_COMMON_IOP_PRIV_H

#include <lib-common/iop.h>

/* {{{ IOP environment */

/** Definition of an IOP environment. */
struct iop_env_t {
    /** Reference counter. */
    int refcnt;

    /** The current context of the IOP environment.
     *
     * It is swapped on \ref iop_env_transfer().
     */
    /* TODO: In order to support multi-threading, protect it with a rw-lock or
     * an arc-swap. */
    iop_env_ctx_t ctx;
};

int iop_check_registered_classes(const iop_env_t *iop_env, sb_t *err);

iop_dso_t *iop_dso_get_from_pkg(const iop_env_t *iop_env,
                                const iop_pkg_t *pkg);

int iop_register_packages_dso(iop_env_t *iop_env, const iop_pkg_t **pkgs,
                              int len, iop_dso_t * nullable dso, sb_t *err);

/* }}} */
/* {{{ Getters */

const iop_struct_t *
iop_pkg_get_struct_by_name(const iop_pkg_t *pkg, lstr_t name);

/* }}} */
/* {{{ Helpers */

static inline bool iop_int_type_is_signed(iop_type_t type)
{
    assert (type <= IOP_T_U64);
    return !(type & 1);
}

static inline size_t iop_int_type_size(iop_type_t type)
{
    assert (type <= IOP_T_U64);
    return 1 << (type >> 1);
}

/* }}} */

#endif
