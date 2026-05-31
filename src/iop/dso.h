/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

#include <lib-common/farch.h>

qm_kvec_t(iop_struct, lstr_t, const iop_struct_t * nonnull,
          qhash_lstr_hash, qhash_lstr_equal);

/** Stat of a DSO file used for LMID cache. */
typedef struct iop_dso_file_stat_t {
    dev_t dev;
    ino_t ino;
    struct timespec mtim;
} iop_dso_file_stat_t;

typedef struct iop_dso_t {
    int               refcnt;
    void             * nonnull handle;
    lstr_t            path;
    uint32_t          version;

    /* The IChannel version loaded from this DSO. */
    ic_user_version_t ic_user_version;

    /* The IOP environment loaded by this DSO. */
    iop_env_t * nonnull iop_env;

    /* The stat of the DSO file if the DSO is used to create a new LMID */
    iop_dso_file_stat_t file_stat;

    qm_t(iop_pkg)     pkg_h;
    qm_t(iop_enum)    enum_h;
    qm_t(iop_struct)  struct_h;
    qm_t(iop_typedef) typedef_h;
    qm_t(iop_iface)   iface_h;
    qm_t(iop_mod)     mod_h;

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
 *  - RTLD_LAZY | RTLD_GLOBAL | RTLD_DEEPBIND when the lmid of the IOP
 *    environment is LM_ID_BASE. This is equivalent of calling dlopen(3) with
 *    the same flags.
 *  - RTLD_LAZY | RTLD_DEEPBIND when the lmid of the IOP environment is
 *    LM_ID_NEWLM or an already existing namespace.
 *
 * Because we need to cache the different namespaces for the different DSOs
 * when creating new namespaces, with LM_ID_NEWLM, it is possible that the
 * namespace is reused instead of creating a new one depending on the DSO.
 * See notes in dso.c for more explanation why it is required.
 *
 * The returned DSO owns a single reference. Once you are done with it, call
 * \ref iop_dso_close (NOT \ref iop_dso_delete): closing both unregisters its
 * packages from the IOP environment and releases that reference.
 *
 * \param[in]  iop_env the current IOP environment.
 * \param[in]  path    path to the DSO.
 * \param[out] err     error description in case of error.
 */
iop_dso_t * nullable iop_dso_open(iop_env_t * nonnull iop_env,
                                  const char * nonnull path,
                                  sb_t * nonnull err);

/** Take an additional reference on a DSO.
 *
 * Each reference taken this way must be released with \ref iop_dso_release.
 */
REFCNT_RETAIN(iop_dso_t, iop_dso);

/** Release one reference to a DSO (counterpart of \ref iop_dso_retain).
 *
 * This is memory-only: it does NOT unregister the DSO's packages from the
 * IOP environment (that is done eagerly by \ref iop_dso_close /
 * \ref iop_dso_unregister). The shared object is unmapped (dlclose'd) only
 * once the DSO's last reference is dropped, which lets an \ref iop_env_ctx_t
 * snapshot keep a DSO's descriptors mapped for as long as the snapshot is
 * held.
 *
 * To dispose of a DSO obtained from \ref iop_dso_open, use \ref iop_dso_close
 * instead: that one also unregisters the packages. Use this function only to
 * release extra references acquired with \ref iop_dso_retain.
 */
void iop_dso_release(iop_dso_t * nullable * nonnull dsop);

/** Close a DSO (counterpart of \ref iop_dso_open).
 *
 * Unregisters the DSO's packages from the IOP environment (reloading the DSOs
 * that depend on it) and releases the reference held by the caller. This is
 * what owners of a DSO opened with \ref iop_dso_open must call.
 */
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

/* Called by iop module. */
void iop_dso_initialize(void);
void iop_dso_shutdown(void);

#endif
