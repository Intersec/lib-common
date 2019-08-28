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

#if !defined(IS_LIB_COMMON_IOP_H) || defined(IS_LIB_COMMON_IOP_DSO_H)
#  error "you must include <lib-common/iop.h> instead"
#else
#define IS_LIB_COMMON_IOP_DSO_H

#include <dlfcn.h>

#include "farch.h"

qm_kvec_t(iop_struct, lstr_t, const iop_struct_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);
qm_kvec_t(iop_iface, lstr_t, const iop_iface_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);
qm_kvec_t(iop_mod, lstr_t, const iop_mod_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

typedef struct iop_dso_t {
    int              refcnt;
    void            * nonnull handle;
    lstr_t           path;
    Lmid_t           lmid;

    qm_t(iop_pkg)    pkg_h;
    qm_t(iop_enum)   enum_h;
    qm_t(iop_struct) struct_h;
    qm_t(iop_iface)  iface_h;
    qm_t(iop_mod)    mod_h;

    /* Hash table of other iop_dso_t used by this one (in case of fixups). */
    qh_t(ptr) depends_on;
    /* Hash table of other iop_dso_t which need this one (in case of
     * fixups). */
    qh_t(ptr) needed_by;

    bool use_external_packages : 1;
    bool is_registered         : 1;
    bool dont_replace_fix_pkg  : 1;
} iop_dso_t;

/** Load a DSO from a file, and register its packages.
 *
 * The DSO is opened with dlmopen(3) with the following flags:
 *  - RTLD_LAZY | RTLD_GLOBAL | RTLD_DEEPBIND when lmid is LM_ID_BASE. This is
 *    equivalent of calling dlopen(3) with the same flags.
 *  - RTLD_LAZY | RTLD_DEEPBIND when lmid is LM_ID_NEWLM or an already
 *    existing namespace.
 *
 * Due to a bug in glibc < 2.24, we are not able to call dlmopen(3) with
 * RTLD_GLOBAL. This means that the DSO creating a new namespace must contain
 * all the symbols needed by the other DSOs that will use that namespace.
 * See http://man7.org/linux/man-pages/man3/dlopen.3.html#BUGS
 *
 * \param[in]  path  path to the DSO.
 * \param[in]  lmid  lmid argument passed to dlmopen(3).
 * \param[out] err   error description in case of error.
 */
iop_dso_t * nullable iop_dso_open(const char * nonnull path,
                                  Lmid_t lmid, sb_t * nonnull err);

/** Load a DSO from an already opened DSO handle, and register its packages.
 *
 * On success, the DSO will own the handle afterwards.
 * On error, the handle is not closed.
 *
 * \param[in]  handle handle to the opened DSO.
 * \param[in]  path   path to the DSO.
 * \param[in]  lmid   lmid argument used to dlmopen(3) the DSO.
 * \param[out] err    error description in case of error.
 */
iop_dso_t * nullable iop_dso_load_handle(void * nonnull handle,
                                         const char * nonnull path,
                                         Lmid_t lmid, sb_t * nonnull err);

static ALWAYS_INLINE iop_dso_t * nonnull iop_dso_dup(iop_dso_t * nonnull dso)
{
    dso->refcnt++;
    return dso;
}

/** Close a DSO and unregister its packages. */
void iop_dso_close(iop_dso_t * nullable * nonnull dsop);

/** Register the packages contained in a DSO.
 *
 * Packages registration is mandatory in order to pack/unpack classes
 * they contain.
 * \ref iop_dso_open already registers the DSO packages, so calling this
 * function only makes sense if you've called \ref iop_dso_unregister before.
 */
void iop_dso_register(iop_dso_t * nonnull dso);

/** Unregister the packages contained in a DSO. */
void iop_dso_unregister(iop_dso_t * nonnull dso);

iop_struct_t const * nullable
iop_dso_find_type(iop_dso_t const * nonnull dso, lstr_t name);
iop_enum_t const * nullable
iop_dso_find_enum(iop_dso_t const * nonnull dso, lstr_t name);

const void * const nullable * nullable
iop_dso_get_ressources(const iop_dso_t * nonnull, lstr_t category);

#define IOP_DSO_GET_RESSOURCES(dso, category)                 \
    ((const iop_dso_ressource_t(category) *const *)           \
        iop_dso_get_ressources(dso, LSTR(#category)))

#define iop_dso_ressources_for_each_entry(category, ressource, ressources) \
    for (const iop_dso_ressource_t(category) *ressource,                   \
         *const *_ressource_ptr = (ressources);                            \
         (ressource = _ressource_ptr ? *_ressource_ptr : NULL);            \
         _ressource_ptr++)

#define iop_dso_for_each_ressource(dso, category, ressource)                 \
    iop_dso_ressources_for_each_entry(category, ressource,                   \
                                      IOP_DSO_GET_RESSOURCES(dso, category))

IOP_DSO_DECLARE_RESSOURCE_CATEGORY(iopy_on_register, struct farch_entry_t);

/* Called by iop module. */
void iop_dso_initialize(void);
void iop_dso_shutdown(void);

#endif
