/***************************************************************************/
/*                                                                         */
/* Copyright 2020 INTERSEC SA                                              */
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

/* {{{ class_id/class_name qm declarations */

typedef struct class_id_key_t {
    const iop_struct_t *master;
    uint16_t            child_id;
} class_id_key_t;

static inline uint32_t
class_id_key_hash(const qhash_t *h, const class_id_key_t *key)
{
    return mem_hash32(key, offsetof(class_id_key_t, child_id) + 2);
}
static inline bool
class_id_key_equal(const qhash_t *h, const class_id_key_t *k1,
                   const class_id_key_t *k2)
{
    return (k1->master == k2->master) && (k1->child_id == k2->child_id);
}

qm_kvec_t(iop_class_by_id, class_id_key_t, const iop_struct_t *,
          class_id_key_hash, class_id_key_equal);

/* }}} */
/* {{{ IOP context */

qm_khptr_ckey_t(iop_dsos, iop_pkg_t, iop_dso_t *);

qvector_t(iop_obj, iop_obj_t);
qm_kvec_t(iop_objs, lstr_t, qv_t(iop_obj), qhash_lstr_hash, qhash_lstr_equal);

typedef struct iop_env_t {
    qm_t(iop_class_by_id)   classes_by_id;
    qm_t(iop_dsos)   dsos_by_pkg;

    /* Contains classes/structs/unions/enums/packages. */
    qm_t(iop_objs)   iop_obj_by_fullname;
} iop_env_t;

iop_env_t *iop_env_init(iop_env_t *env);
void iop_env_wipe(iop_env_t *env);
void iop_env_set(iop_env_t *env);
void iop_env_get(iop_env_t *env);

const iop_pkg_t *iop_get_pkg_env(lstr_t pkgname, const iop_env_t *env);
int iop_check_registered_classes(const iop_env_t *env, sb_t *err);

iop_dso_t *iop_dso_get_from_pkg(const iop_pkg_t *pkg);

int iop_register_packages_env(const iop_pkg_t **pkgs, int len,
                              iop_dso_t * nullable dso,
                              iop_env_t *env, unsigned flags, sb_t *err);

/* }}} */
/* {{{ Getters */

const iop_struct_t *iop_pkg_get_struct_by_name(const iop_pkg_t *pkg, lstr_t name);

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
