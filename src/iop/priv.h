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

#ifndef IS_LIB_COMMON_IOP_PRIV_H
#define IS_LIB_COMMON_IOP_PRIV_H

#include <lib-common/iop.h>

/* {{{ IOP environment */

/** Opaque ArcSwap handle managing the env's IOP context.
 *
 * The real type is `iop_env_ctx_arcswap_t` defined in Rust (see
 * `rust/libcommon-core/iop_env.rs`). C code never inspects its fields.
 */
typedef struct iop_env_ctx_arcswap_t iop_env_ctx_arcswap_t;

/* `iop_env_ctx_guard_t` is declared in <lib-common/iop.h> — its layout
 * must stay in sync with the `#[repr(C)]` struct in
 * `rust/libcommon-core/iop_env.rs`. */

/** Definition of an IOP environment.
 *
 * The mutable IOP context lives behind \ref ctx_swap, a lock-free
 * `ArcSwap` managed in Rust. Readers acquire a refcounted snapshot via
 * \ref iop_env_ctx_acquire; writers build a new ctx with
 * \ref iop_env_ctx_copy and install it atomically via
 * \ref iop_env_ctx_replace. The env-handle refcount and the
 * startup-config fields (\ref dso_lmid, \ref ic_user_version) stay
 * mutated on the main thread only.
 */
struct iop_env_t {
    /** Reference counter. */
    int refcnt;

    /** ArcSwap holding the current IOP context (owned, freed in
     * `iop_env_wipe`). */
    iop_env_ctx_arcswap_t * nonnull ctx_swap;

    /** The Lmid_t for the DSOs loaded in the IOP environment.
     *
     * By default, this is set to LM_ID_BASE (the application's namespace).
     * Set it to LM_ID_NEWLM before opening the first DSO of this IOP
     * environment to use a separate namespace.
     *
     * \note Lives on the env, not on the ctx, because it is written by
     *       \ref iop_dso_open after \c dlmopen returns. Keeping it on the
     *       ctx would force a copy-modify-swap for every DSO open. Always
     *       accessed from the main thread.
     */
    Lmid_t dso_lmid;

    /** IC user version.
     *
     * Set it to modify the user version of the IChannels using this IOP
     * environment.
     *
     * \note Lives on the env, not on the ctx, because it is written once at
     *       startup (before any reader exists). Keeping it on the ctx would
     *       require copy-modify-swap to update a single field.
     */
    ic_user_version_t ic_user_version;
};

/** Initialize an `iop_env_ctx_t` in-place: all hash maps are created empty.
 */
iop_env_ctx_t *iop_env_ctx_init(iop_env_ctx_t *iop_env_ctx);

/** Wipe an `iop_env_ctx_t` in-place: free all hash map storage.
 */
void iop_env_ctx_wipe(iop_env_ctx_t *iop_env_ctx);

GENERIC_NEW(iop_env_ctx_t, iop_env_ctx);
GENERIC_DELETE(iop_env_ctx_t, iop_env_ctx);

/** Deep-copy the contents of `src` into `dst`.
 *
 * \pre `dst` must be a freshly-initialized empty ctx with no other
 *      reference holders.
 */
void iop_env_ctx_copy_fields(iop_env_ctx_t *dst, const iop_env_ctx_t *src);

/* {{{ Rust FFI prototypes */

/** Drop function pointer used by the Rust ArcSwap to free a ctx. */
typedef void (*iop_env_ctx_drop_f)(void * nonnull ctx);

iop_env_ctx_arcswap_t * nonnull
iop_env_ctx_arcswap_new(void * nonnull initial_ctx,
                        iop_env_ctx_drop_f nonnull drop_fn);

void
iop_env_ctx_arcswap_free(iop_env_ctx_arcswap_t * nullable arcswap);

iop_env_ctx_guard_t
iop_env_ctx_arcswap_acquire(const iop_env_ctx_arcswap_t * nonnull arcswap);

void iop_env_ctx_arcswap_release(iop_env_ctx_guard_t guard);

void iop_env_ctx_arcswap_store(iop_env_ctx_arcswap_t * nonnull arcswap,
                               void * nonnull new_ctx,
                               iop_env_ctx_drop_f nonnull drop_fn);

/* }}} */

/** Allocate a fresh ctx initialized with a deep copy of \p iop_env's
 *  current ctx.
 *
 *  The returned ctx is uniquely owned by the caller. It must either be
 *  installed via \ref iop_env_ctx_replace or freed via
 *  \ref iop_env_ctx_free_wipe.
 */
iop_env_ctx_t * nonnull iop_env_ctx_copy(const iop_env_t * nonnull iop_env);

/** Atomically install \p *new_ctx into \p iop_env, dropping the
 *  previous ctx once its last reader releases it.
 *
 *  Takes ownership of \p *new_ctx; the caller's pointer is set to NULL
 *  on return.
 */
void iop_env_ctx_replace(iop_env_t * nonnull iop_env,
                         iop_env_ctx_t * nullable * nonnull new_ctx);

int iop_check_registered_classes_ctx(const iop_env_ctx_t *iop_env_ctx,
                                     sb_t *err);

iop_dso_t *iop_dso_get_from_pkg(const iop_env_t *iop_env,
                                const iop_pkg_t *pkg);

int iop_register_packages_ctx(iop_env_ctx_t *iop_env_ctx,
                              const iop_pkg_t * const *pkgs,
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


/** Rough equivalent of memcmp() for IOP objects.
 *
 * This API is exposed only for testing purposes and should not be used in
 * production code.
 */
bool z_iop_mem_equals_desc(const iop_struct_t *nonnull st,
                           const void *nonnull v1,
                           const void *nonnull v2);

/* }}} */

#endif
