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

#include <lib-common/iop.h>
#include "priv.h"

/* XXX: Notes on DSO, LMID and cache system:
 *
 * Because the glibc implementation supports a maximum of 16 namespaces
 * per process, we need to be able to re-use the namespaces as much as
 * possible.
 *
 * To mitigate this issue, we have a cache of stored LMID per DSO.
 * When we try to open a DSO with LM_ID_NEWLM to create a new namespace,
 * check if this DSO has already been opened in a namespace, if it does,
 * return this namespace and increment its ref counter.
 * Else, open the DSO by creating a new namespace and store it in the cache.
 *
 * Unfortunately, this does not completely solve the issue as there are
 * still some issues:
 * - We cannot create more than 15 different simultaneous namespaces.
 * - If we load too many DSOs into too many different namespaces, even if
 *   we fully close the DSOs and namespaces, an error is thrown when
 *   trying to call dlmopen() after some time:
 *     libc.so.6: cannot allocate memory in static TLS block
 * - If we try to create two different namespaces by loading the same DSO
 *   simultaneously, we would actually end up using the same namespace.
 *   This can have undesired consequences if we try load additional DSOs
 *   without unloading the previous ones in the same namespace.
 *
 * Ideally, to solve this issue, what we should do instead is:
 * - Open the DSO in dlmopen()
 * - Load the IOP symbols in the IOP environment
 * - Unload the DSO with dlclose()
 * But this is not currently possible for two reasons:
 * - The IOP symbols from the DSO are not copied, but directly inserted in the
 *   IOP environment. Doing a copy would require resolving dynamically the
 *   different pointers on copy.
 * - We rely on the linker to resolve the IOP symbols on dlmopen() for
 *   additional DSOs in the same namespace.
 *
 * The best solution would be to abandon loading DSO as shared objects (.so),
 * and use IOP² instead.
 *
 * See https://www.man7.org/linux/man-pages/man3/dlmopen.3.html#NOTES
 */

/** Reference of LMID when a DSO tries to create a new LMID. */
typedef struct iop_dso_lmid_ref_t {
    int refcnt;
    Lmid_t lmid;
} iop_dso_lmid_ref_t;

static inline uint32_t
iop_dso_file_stat_hash(const qhash_t * nonnull h,
                       const iop_dso_file_stat_t * nonnull stat)
{
    return mem_hash32(stat, sizeof(iop_dso_file_stat_t));
}

static inline bool
iop_dso_file_stat_equal(const qhash_t * nonnull h,
                        const iop_dso_file_stat_t * nonnull stat1,
                        const iop_dso_file_stat_t * nonnull stat2)
{
    return memcmp(stat1, stat2, sizeof(iop_dso_file_stat_t)) == 0;
}

qm_kvec_t(iop_dso_lmid_by_stat, iop_dso_file_stat_t, iop_dso_lmid_ref_t,
          iop_dso_file_stat_hash, iop_dso_file_stat_equal);

static struct {
    /** Cache of stored LMID by DSO stat.
     *
     * See notes above why it is required.
     */
    qm_t(iop_dso_lmid_by_stat) lmid_by_stat;
} iop_dso_g;
#define _G  iop_dso_g

static ALWAYS_INLINE
void iopdso_register_struct(iop_dso_t *dso, iop_struct_t const *st)
{
    qm_add(iop_struct, &dso->struct_h, &st->fullname, st);
}

static ALWAYS_INLINE
void iopdso_register_typedef(iop_dso_t *dso, iop_typedef_t const *td)
{
    qm_add(iop_typedef, &dso->typedef_h, &td->fullname, td);
}

static lstr_t iop_pkgname_from_fullname(lstr_t fullname)
{
    const void *p;
    pstream_t pkgname;
    pstream_t ps = ps_initlstr(&fullname);

    p = memrchr(ps.s, '.', ps_len(&ps));
    if (!p) {
        /* XXX: This case case may happen with the special 'Void' package */
        return LSTR_NULL_V;
    }
    pkgname = __ps_get_ps_upto(&ps, p);
    return LSTR_PS_V(&pkgname);
}

static const iop_struct_t *
iop_pkg_get_struct(const iop_pkg_t *pkg, lstr_t fullname)
{
    for (const iop_struct_t * const *st = pkg->structs; *st; st++) {
        if (lstr_equal(fullname, (*st)->fullname)) {
            return *st;
        }
    }
    for (const iop_iface_t * const *iface = pkg->ifaces; *iface; iface++) {
        for (int i = 0; i < (*iface)->funs_len; i++) {
            const iop_rpc_t *rpc = &(*iface)->funs[i];

            if (lstr_equal(fullname, rpc->args->fullname)) {
                return rpc->args;
            }
            if (lstr_equal(fullname, rpc->result->fullname)) {
                return rpc->result;
            }
            if (lstr_equal(fullname, rpc->exn->fullname)) {
                return rpc->exn;
            }
        }
    }

    return NULL;
}

static int iopdso_register_struct_ref(iop_dso_t *dso, const iop_struct_t *st,
                                      const iop_pkg_t *own_pkg, sb_t *err)
{
    const iop_struct_t *pkg_st;
    lstr_t pkgname = iop_pkgname_from_fullname(st->fullname);
    const iop_pkg_t *pkg;
    iop_dso_t *dep;

    pkg = iop_env_get_pkg(dso->iop_env, pkgname);
    if (!pkg) {
        lstr_t pkgname2 = iop_pkgname_from_fullname(pkgname);

        pkg = iop_env_get_pkg(dso->iop_env, pkgname2);
        if (!pkg) {
            e_trace(4, "cannot find package `%*pM` in current environment",
                    LSTR_FMT_ARG(pkgname));
            return 0;
        }
    }

    if (lstr_equal(pkg->name, own_pkg->name)) {
        return 0;
    }

    pkg_st = iop_pkg_get_struct(pkg, st->fullname);
    if (!pkg_st) {
        e_error("IOP DSO: did not find struct `%*pM` in memory",
                LSTR_FMT_ARG(st->fullname));
        return 0;
    }

    dep = iop_dso_get_from_pkg(dso->iop_env, pkg);
    if (dep && dep != dso) {
        qh_add(ptr, &dso->depends_on, dep);
        qh_add(ptr, &dep->needed_by,  dso);
    }

    return 0;
}

static int iopdso_register_class_parent_ref(
    iop_dso_t *dso, const iop_struct_t *desc,
    const iop_pkg_t *own_pkg, sb_t *err)
{
    iop_class_attrs_t *class_attrs;

    if (!iop_struct_is_class(desc)) {
        return 0;
    }

    class_attrs = (iop_class_attrs_t *)desc->class_attrs;
    if (class_attrs->parent) {
        RETHROW(iopdso_register_struct_ref(dso, class_attrs->parent, own_pkg,
                                           err));
    }
    return 0;
}

static int iopdso_register_pkg_ref(iop_dso_t *dso, const iop_pkg_t *pkg,
                                   sb_t *err)
{
    for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
        const iop_struct_t *desc = *it;

        RETHROW(iopdso_register_struct_ref(dso, desc, pkg, err));
        RETHROW(iopdso_register_class_parent_ref(dso, desc, pkg, err));

        for (int i = 0; i < desc->fields_len; i++) {
            iop_field_t *f = (iop_field_t *)&desc->fields[i];

            if (f->type == IOP_T_STRUCT || f->type == IOP_T_UNION) {
                RETHROW(iopdso_register_struct_ref(dso, f->u1.st_desc, pkg,
                                                   err));
            }
        }
    }
    for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
        for (int i = 0; i < (*it)->funs_len; i++) {
            iop_rpc_t *rpc = (iop_rpc_t *)&(*it)->funs[i];

            RETHROW(iopdso_register_struct_ref(dso, rpc->args, pkg, err));
            RETHROW(iopdso_register_struct_ref(dso, rpc->result, pkg, err));
            RETHROW(iopdso_register_struct_ref(dso, rpc->exn, pkg, err));
        }
    }
    return 0;
}

static int iopdso_register_pkg(iop_dso_t *dso, iop_pkg_t const *pkg,
                               iop_env_t *iop_env, sb_t *err)
{
    if (qm_add(iop_pkg, &dso->pkg_h, &pkg->name, pkg) < 0) {
        return 0;
    }
    if (dso->use_external_packages) {
        e_trace(1, "register package refs `%*pM` (%p)",
                LSTR_FMT_ARG(pkg->name), pkg);
        RETHROW(iopdso_register_pkg_ref(dso, pkg, err));
    }
    RETHROW(iop_register_packages_dso(iop_env, &pkg, 1, dso, err));
    for (const iop_enum_t *const *it = pkg->enums; *it; it++) {
        qm_add(iop_enum, &dso->enum_h, &(*it)->fullname, *it);
    }
    for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
        iopdso_register_struct(dso, *it);
    }
    if (dso->version >= IOP_DSO_VERSION_TYPEDEF) {
        for (const iop_typedef_t *const *it = pkg->typedefs; *it; it++) {
            iopdso_register_typedef(dso, *it);
        }
    }
    for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
        qm_add(iop_iface, &dso->iface_h, &(*it)->fullname, *it);
        for (int i = 0; i < (*it)->funs_len; i++) {
            const iop_rpc_t *rpc = &(*it)->funs[i];

            iopdso_register_struct(dso, rpc->args);
            iopdso_register_struct(dso, rpc->result);
            iopdso_register_struct(dso, rpc->exn);
        }
    }
    for (const iop_mod_t *const *it = pkg->mods; *it; it++) {
        qm_add(iop_mod, &dso->mod_h, &(*it)->fullname, *it);
    }
    for (const iop_pkg_t *const *it = pkg->deps; *it; it++) {
        if (dso->use_external_packages &&
            iop_env_get_pkg(iop_env, (*it)->name))
        {
            continue;
        }
        RETHROW(iopdso_register_pkg(dso, *it, iop_env, err));
    }
    return 0;
}

static int iop_dso_reopen(iop_dso_t *dso, sb_t *err);

static iop_dso_t *iop_dso_init(iop_dso_t *dso)
{
    p_clear(dso, 1);
    qm_init_cached(iop_pkg,     &dso->pkg_h);
    qm_init_cached(iop_enum,    &dso->enum_h);
    qm_init_cached(iop_struct,  &dso->struct_h);
    qm_init_cached(iop_typedef, &dso->typedef_h);
    qm_init_cached(iop_iface,   &dso->iface_h);
    qm_init_cached(iop_mod,     &dso->mod_h);
    qh_init(ptr,                &dso->depends_on);
    qh_init(ptr,                &dso->needed_by);

    return dso;
}

static void iop_dso_unload(iop_dso_t *dso)
{
    SB_1k(err);

    e_trace(1, "close dso %p (%*pM)", dso, LSTR_FMT_ARG(dso->path));

    /* Delete references of this DSO in depends_on. */
    qh_for_each_pos(ptr, pos, &dso->depends_on) {
        iop_dso_t *depends_on = dso->depends_on.keys[pos];

        qh_del_key(ptr, &depends_on->needed_by, dso);
    }

    /* Unregister needed_by (otherwise they'll have orphan classes when
     * unregistering this one). */
    qh_for_each_pos(ptr, pos, &dso->needed_by) {
        iop_dso_t *needed_by = dso->needed_by.keys[pos];

        iop_dso_unregister(needed_by);
    }

    /* Unregister DSO. */
    iop_dso_unregister(dso);

    /* Reload needed_by. */
    qh_for_each_pos(ptr, pos, &dso->needed_by) {
        iop_dso_t *needed_by = dso->needed_by.keys[pos];

        if (iop_dso_reopen(needed_by, &err) < 0) {
            e_panic("IOP DSO: unable to reload plugin `%*pM` when unloading "
                    "plugin `%*pM`: %*pM",
                    LSTR_FMT_ARG(needed_by->path), LSTR_FMT_ARG(dso->path),
                    SB_FMT_ARG(&err));
        }
    }
}

static void iop_dso_unregister_ref(const iop_dso_file_stat_t *dso_stat)
{
    int pos;
    iop_dso_lmid_ref_t *lmid_ref;

    /* Look for the DSO in the cache. */
    pos = qm_find(iop_dso_lmid_by_stat, &_G.lmid_by_stat, dso_stat);
    if (pos < 0) {
        /* If not found, this is not a DSO using a separate namespace. */
        return;
    }

    /* Decrement the ref counter. */
    lmid_ref = &_G.lmid_by_stat.values[pos];
    lmid_ref->refcnt--;
    assert(lmid_ref->refcnt >= 0);

    /* If it reaches 0, delete the ref from the cache. */
    if (lmid_ref->refcnt <= 0) {
        qm_del_at(iop_dso_lmid_by_stat, &_G.lmid_by_stat, pos);
    }
}

static void iop_dso_wipe(iop_dso_t *dso)
{
    iop_dso_unload(dso);

    qm_wipe(iop_pkg,     &dso->pkg_h);
    qm_wipe(iop_enum,    &dso->enum_h);
    qm_wipe(iop_struct,  &dso->struct_h);
    qm_wipe(iop_typedef, &dso->typedef_h);
    qm_wipe(iop_iface,   &dso->iface_h);
    qm_wipe(iop_mod,     &dso->mod_h);
    qh_wipe(ptr,         &dso->depends_on);
    qh_wipe(ptr,         &dso->needed_by);
    lstr_wipe(&dso->path);
    iop_dso_unregister_ref(&dso->file_stat);
    if (dso->handle) {
        dlclose(dso->handle);
    }
}
REFCNT_NEW(iop_dso_t, iop_dso);
REFCNT_RELEASE(iop_dso_t, iop_dso);
REFCNT_DELETE(iop_dso_t, iop_dso);

static iop_dso_file_stat_t iop_dso_file_get_stat(const char *path)
{
    iop_dso_file_stat_t dso_stat;
    struct stat file_stat;

    p_clear(&dso_stat, 1);
    p_clear(&file_stat, 1);

    if (stat(path, &file_stat) < 0) {
        e_trace(1, "unable to get stat of DSO at path `%s`: %m", path);
        return dso_stat;
    }

    dso_stat.dev = file_stat.st_dev;
    dso_stat.ino = file_stat.st_ino;
    dso_stat.mtim = file_stat.st_mtim;

    return dso_stat;
}

static int iop_dso_register_(iop_dso_t *dso, sb_t *err);

#ifndef RTLD_DEEPBIND
# define RTLD_DEEPBIND  0
#endif

static iop_dso_t *iop_dso_load_handle(iop_env_t *iop_env, void *handle,
                                      const char *path, sb_t *err);

iop_dso_t *iop_dso_open(iop_env_t *iop_env, const char *path, sb_t *err)
{
    iop_env_ctx_t *iop_env_ctx = &iop_env->ctx;
    int flags = RTLD_LAZY | RTLD_DEEPBIND;
    iop_dso_file_stat_t dso_stat;
    void *handle;
    iop_dso_t *dso;
    iop_dso_lmid_ref_t *lmid_ref = NULL;

    p_clear(&dso_stat, 1);

    /* For LM_ID_BASE, use RTLD_LAZY | RTLD_DEEPBIND | RTLD_GLOBAL */
    if (iop_env_ctx->dso_lmid == LM_ID_BASE) {
        flags |= RTLD_GLOBAL;
    }

    if (iop_env_ctx->dso_lmid == LM_ID_NEWLM) {
        uint32_t pos;

        /* Try to look for the namespace in the cache for the given DSO. */
        dso_stat = iop_dso_file_get_stat(path);
        pos = qm_reserve(iop_dso_lmid_by_stat, &_G.lmid_by_stat, &dso_stat, 0);

        if (pos & QHASH_COLLISION) {
            /* If we already have a namespace if this DSO, reuse it and
             * increment the ref counter. */
            lmid_ref = &_G.lmid_by_stat.values[pos ^ QHASH_COLLISION];
            iop_env_ctx->dso_lmid = lmid_ref->lmid;
        } else {
            /* Else, create a new ref. */
            lmid_ref = &_G.lmid_by_stat.values[pos];
            p_clear(lmid_ref, 1);
        }
        lmid_ref->refcnt++;
    }

    /* Opening the DSO with the correct flags and LMID */
    handle = dlmopen(iop_env_ctx->dso_lmid, path, flags);
    if (handle == NULL) {
        sb_setf(err, "unable to dlopen `%s`: %s", path, dlerror());
        return NULL;
    }

    if (iop_env_ctx->dso_lmid == LM_ID_NEWLM) {
        /* If we have created a new namespace, get its LMID and store it in
         * the IOP environment and in the cache reference. */
        if (dlinfo(handle, RTLD_DI_LMID, &iop_env_ctx->dso_lmid) < 0) {
            sb_setf(err, "unable to get lmid of plugin `%s`: %s", path,
                    dlerror());
            return NULL;
        }
        lmid_ref->lmid = iop_env_ctx->dso_lmid;
    }

    /* Load the DSO handle */
    dso = iop_dso_load_handle(iop_env, handle, path, err);
    if (!dso) {
        dlclose(handle);
        iop_dso_unregister_ref(&dso_stat);
        return NULL;
    }

    /* Store the DSO file stat to unregister the lmid ref on DSO unload.
     * If the DSO did not create a new namespace, the is not an issue as the
     * ref will simple not been found in the cache on DSO unload. */
    dso->file_stat = dso_stat;

    return dso;
}

static iop_dso_t *iop_dso_load_handle(iop_env_t *iop_env, void *handle,
                                      const char *path, sb_t *err)
{
    iop_dso_t *dso;
    iop_dso_vt_t *dso_vt;
    iop_pkg_t **pkgp;
    uint32_t *versionp = dlsym(handle, "iop_dso_version");
    uint32_t *user_version_p;
    iop_dso_user_version_cb_f **user_version_cb_p;

    dso_vt = dlsym(handle, "iop_vtable");
    if (dso_vt == NULL || dso_vt->vt_size == 0) {
        e_warning("IOP DSO: unable to find valid IOP vtable in plugin "
                  "`%s`, no error management allowed: %s", path, dlerror());
    } else {
        dso_vt->iop_set_verr = &iop_set_verr;
    }

    pkgp = dlsym(handle, "iop_packages");
    if (pkgp == NULL) {
        sb_setf(err, "unable to find IOP packages in plugin `%s`: %s",
                path, dlerror());
        return NULL;
    }

    user_version_p = dlsym(handle, "iop_dso_user_version");
    user_version_cb_p = dlsym(handle, "iop_dso_user_version_cb");

    dso = iop_dso_new();
    dso->path = lstr_dups(path, -1);
    dso->iop_env = iop_env;
    dso->handle = handle;
    dso->version = versionp ? *versionp : 0;
    dso->use_external_packages = !!dlsym(handle, "iop_use_external_packages");
    dso->dont_replace_fix_pkg = !!dlsym(handle, "iop_dont_replace_fix_pkg");

    dso->ic_user_version = (ic_user_version_t) {
        .current_version = user_version_p ? *user_version_p : 0,
        .check_cb = user_version_cb_p ? *user_version_cb_p : NULL,
    };

    e_trace(1, "open new dso %p (%*pM)", dso, LSTR_FMT_ARG(dso->path));

    if (iop_dso_register_(dso, err) < 0) {
        dso->handle = NULL;
        iop_dso_delete(&dso);
        return NULL;
    }

    return dso;
}

static int iop_dso_reopen(iop_dso_t *dso, sb_t *err)
{
    e_trace(1, "reopen dso %p (%*pM)", dso, LSTR_FMT_ARG(dso->path));

    iop_dso_unload(dso);

    qm_clear(iop_pkg,     &dso->pkg_h);
    qm_clear(iop_enum,    &dso->enum_h);
    qm_clear(iop_struct,  &dso->struct_h);
    qm_clear(iop_typedef, &dso->typedef_h);
    qm_clear(iop_iface,   &dso->iface_h);
    qm_clear(iop_mod,     &dso->mod_h);
    qh_clear(ptr,         &dso->depends_on);
    qh_clear(ptr,         &dso->needed_by);

    dso->is_registered = false;
    return iop_dso_register_(dso, err);
}

void iop_dso_close(iop_dso_t **dsop)
{
    iop_dso_delete(dsop);
}

static int iop_dso_register_(iop_dso_t *dso, sb_t *err)
{
    if (!dso->is_registered) {
        iop_env_t *iop_env;
        iop_pkg_t **pkgp = dlsym(dso->handle, "iop_packages");

        if (!pkgp) {
            /* This should not happen because this was checked before. */
            e_panic("IOP DSO: iop_packages not found when registering DSO");
        }
        qm_clear(iop_pkg, &dso->pkg_h);
        iop_env = iop_env_new();
        iop_env_copy(iop_env, dso->iop_env);
        while (*pkgp) {
            if (iopdso_register_pkg(dso, *pkgp++, iop_env, err) < 0) {
                iop_env_delete(&iop_env);
                return -1;
            }
        }
        RETHROW(iop_check_registered_classes(iop_env, err));
        iop_env_transfer(dso->iop_env, iop_env);
        iop_env_delete(&iop_env);
        dso->is_registered = true;
    }
    return 0;
}

void iop_dso_register(iop_dso_t *dso)
{
    SB_1k(err);

    if (iop_dso_register_(dso, &err) < 0) {
        e_fatal("IOP DSO: %*pM", SB_FMT_ARG(&err));
    }
}

void iop_dso_unregister(iop_dso_t *dso)
{
    if (dso->is_registered) {
        t_scope;
        qv_t(cvoid) vec;

        t_qv_init(&vec, qm_len(iop_pkg, &dso->pkg_h));
        qm_for_each_pos(iop_pkg, pos, &dso->pkg_h) {
            qv_append(&vec, dso->pkg_h.values[pos]);
        }
        iop_unregister_packages(dso->iop_env, (const iop_pkg_t **)vec.tab,
                                vec.len);
        dso->is_registered = false;
    }
}

void iop_dso_initialize(void)
{
    qm_init(iop_dso_lmid_by_stat, &_G.lmid_by_stat);
}

void iop_dso_shutdown(void)
{
    qm_deep_wipe(iop_dso_lmid_by_stat, &_G.lmid_by_stat, IGNORE, IGNORE);
}
