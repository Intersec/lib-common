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

/* The following define ensures the DSO compatibility with the implementation
 * of typedef for enums, unions and structures (including classes) in IOP
 * files */
#define IOP_DSO_VERSION_TYPEDEF 20231114

qm_khptr_ckey_t(iop_dso_by_handle, void, iop_dso_t *);

static struct {
    qm_t(iop_dso_by_handle) dsos_by_handle;
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

static ALWAYS_INLINE
int iop_typedef_is_struct(iop_typedef_t const *td)
{
    return (td->type == IOP_T_UNION) || (td->type == IOP_T_STRUCT);
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
iop_get_struct(const iop_pkg_t *pkg, lstr_t fullname)
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

static int iopdso_fix_struct_ref(iop_dso_t *dso, const iop_struct_t **st,
                                 const iop_pkg_t *own_pkg, sb_t *err)
{
    const iop_struct_t *fix;
    lstr_t pkgname = iop_pkgname_from_fullname((*st)->fullname);
    const iop_pkg_t *pkg;
    iop_dso_t *dep;

    pkg = iop_get_pkg(pkgname);
    if (!pkg) {
        lstr_t pkgname2 = iop_pkgname_from_fullname(pkgname);

        pkg = iop_get_pkg(pkgname2);
        if (!pkg) {
            e_trace(4, "cannot find package `%*pM` in current environment",
                    LSTR_FMT_ARG(pkgname));
            return 0;
        }
    }

    if (lstr_equal(pkg->name, own_pkg->name)) {
        return 0;
    }

    fix = iop_get_struct(pkg, (*st)->fullname);
    if (!fix) {
        e_error("IOP DSO: did not find struct %s in memory",
                (*st)->fullname.s);
        return 0;
    }
    if (fix != *st) {
        if (dso->dont_replace_fix_pkg) {
            sb_setf(err, "package `%*pM` is already defined when loading IOP "
                    "DSO `%*pM`", LSTR_FMT_ARG((*st)->fullname),
                    LSTR_FMT_ARG(dso->path));
            return -1;
        }

        /* XXX: Kept for backward compatibility.
         * To be deleted once all clients will have a product >= 2017.1. */
        e_trace(3, "fixup `%*pM`, %p => %p", LSTR_FMT_ARG((*st)->fullname),
                *st, fix);
        mprotect((void *)(((uintptr_t)st >> PAGE_SIZE_SHIFT) << PAGE_SIZE_SHIFT),
                 PAGE_SIZE, PROT_READ | PROT_WRITE);
        *st = fix;
    }

    dep = iop_dso_get_from_pkg(pkg);
    if (dep && dep != dso) {
        qh_add(ptr, &dso->depends_on, dep);
        qh_add(ptr, &dep->needed_by,  dso);
    }

    return 0;
}

static int iopdso_fix_class_parent(iop_dso_t *dso, const iop_struct_t *desc,
                                   const iop_pkg_t *own_pkg, sb_t *err)
{
    iop_class_attrs_t *class_attrs;

    if (!iop_struct_is_class(desc)) {
        return 0;
    }

    class_attrs = (iop_class_attrs_t *)desc->class_attrs;
    if (class_attrs->parent) {
        RETHROW(iopdso_fix_struct_ref(dso, &class_attrs->parent, own_pkg,
                                      err));
    }
    return 0;
}

static int iopdso_fix_pkg(iop_dso_t *dso, const iop_pkg_t *pkg, sb_t *err)
{
    for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
        const iop_struct_t *desc = *it;

        RETHROW(iopdso_fix_struct_ref(dso, &desc, pkg, err));
        RETHROW(iopdso_fix_class_parent(dso, desc, pkg, err));

        for (int i = 0; i < desc->fields_len; i++) {
            iop_field_t *f = (iop_field_t *)&desc->fields[i];

            if (f->type == IOP_T_STRUCT || f->type == IOP_T_UNION) {
                RETHROW(iopdso_fix_struct_ref(dso, &f->u1.st_desc, pkg, err));
            }
        }
    }
    for (const iop_iface_t *const *it = pkg->ifaces; *it; it++) {
        for (int i = 0; i < (*it)->funs_len; i++) {
            iop_rpc_t *rpc = (iop_rpc_t *)&(*it)->funs[i];

            RETHROW(iopdso_fix_struct_ref(dso, &rpc->args, pkg, err));
            RETHROW(iopdso_fix_struct_ref(dso, &rpc->result, pkg, err));
            RETHROW(iopdso_fix_struct_ref(dso, &rpc->exn, pkg, err));
        }
    }
    return 0;
}

static const iop_typedef_t *empty_typedefs_g[] = {NULL};

static iop_pkg_t *iop_pkg_dup_old_version(const iop_pkg_t *old_version_pkg)
{
    iop_pkg_t *new_version_pkg = p_new(iop_pkg_t, 1);
    lstr_t *new_name = unconst_cast(lstr_t, &new_version_pkg->name);

    *new_name = old_version_pkg->name;
    new_version_pkg->enums = old_version_pkg->enums;
    new_version_pkg->structs = old_version_pkg->structs;
    new_version_pkg->ifaces = old_version_pkg->ifaces;
    new_version_pkg->mods = old_version_pkg->mods;
    new_version_pkg->deps = old_version_pkg->deps;
    new_version_pkg->typedefs = empty_typedefs_g;

    return new_version_pkg;
}

static int iopdso_register_pkg(iop_dso_t *dso, iop_pkg_t const *pkg,
                               iop_env_t *env, sb_t *err)
{
    uint32_t pos;

    pos = qm_put(iop_pkg, &dso->pkg_h, &pkg->name, pkg, 0);
    if (pos & QHASH_COLLISION) {
        return 0;
    }
    if (dso->version < IOP_DSO_VERSION_TYPEDEF) {
        pkg = iop_pkg_dup_old_version(pkg);
    }
    dso->pkg_h.keys[pos] = pkg->name;
    dso->pkg_h.values[pos] = pkg;
    if (dso->use_external_packages) {
        e_trace(1, "fixup package `%*pM` (%p)", LSTR_FMT_ARG(pkg->name), pkg);
        RETHROW(iopdso_fix_pkg(dso, pkg, err));
    }
    RETHROW(iop_register_packages_env(&pkg, 1, dso, env, IOP_REGPKG_FROM_DSO,
                                      err));
    for (const iop_enum_t *const *it = pkg->enums; *it; it++) {
        qm_add(iop_enum, &dso->enum_h, &(*it)->fullname, *it);
    }
    for (const iop_struct_t *const *it = pkg->structs; *it; it++) {
        iopdso_register_struct(dso, *it);
    }
    for (const iop_typedef_t *const *it = pkg->typedefs; *it; it++) {
        iopdso_register_typedef(dso, *it);
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
        if (dso->use_external_packages && iop_get_pkg_env((*it)->name, env)) {
            continue;
        }
        RETHROW(iopdso_register_pkg(dso, *it, env, err));
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

static void iop_pkg_deep_delete_prev_version(iop_dso_t *dso)
{
    if (dso->version < IOP_DSO_VERSION_TYPEDEF) {
        qm_for_each_value(iop_pkg, value, &dso->pkg_h) {
            iop_pkg_t *pkg = unconst_cast(iop_pkg_t, value);

            p_delete(&pkg);
        }
    }
}

static void iop_pkg_wipe(iop_dso_t *dso)
{
    iop_pkg_deep_delete_prev_version(dso);
    qm_wipe(iop_pkg, &dso->pkg_h);
}

static void iop_pkg_clear(iop_dso_t *dso)
{
    iop_pkg_deep_delete_prev_version(dso);
    qm_clear(iop_pkg, &dso->pkg_h);
}

static void iop_dso_wipe(iop_dso_t *dso)
{
    iop_dso_unload(dso);

    iop_pkg_wipe(dso);
    qm_wipe(iop_enum,    &dso->enum_h);
    qm_wipe(iop_struct,  &dso->struct_h);
    qm_wipe(iop_typedef, &dso->typedef_h);
    qm_wipe(iop_iface,   &dso->iface_h);
    qm_wipe(iop_mod,     &dso->mod_h);
    qh_wipe(ptr,         &dso->depends_on);
    qh_wipe(ptr,         &dso->needed_by);
    lstr_wipe(&dso->path);
    if (dso->handle) {
        qm_del_key(iop_dso_by_handle, &_G.dsos_by_handle, dso->handle);
        dlclose(dso->handle);
    }
}
REFCNT_NEW(iop_dso_t, iop_dso);
REFCNT_RELEASE(iop_dso_t, iop_dso);
REFCNT_DELETE(iop_dso_t, iop_dso);

static int iop_dso_register_(iop_dso_t *dso, sb_t *err);

#ifndef RTLD_DEEPBIND
# define RTLD_DEEPBIND  0
#endif

iop_dso_t *iop_dso_open(const char *path, Lmid_t lmid, sb_t *err)
{
    int flags = RTLD_LAZY | RTLD_DEEPBIND;
    void *handle;
    iop_dso_t *dso;

    if (lmid == LM_ID_BASE) {
        flags |= RTLD_GLOBAL;
    }

    handle = dlmopen(lmid, path, flags);
    if (handle == NULL) {
        sb_setf(err, "unable to dlopen `%s`: %s", path, dlerror());
        return NULL;
    }

    dso = iop_dso_load_handle(handle, path, lmid, err);
    if (!dso) {
        dlclose(handle);
        return NULL;
    }

    return dso;
}

iop_dso_t *iop_dso_load_handle(void *handle, const char *path,
                               Lmid_t lmid, sb_t *err)
{
    iop_dso_t *dso;
    iop_dso_vt_t *dso_vt;
    iop_pkg_t **pkgp;
    uint32_t *versionp = dlsym(handle, "iop_dso_version");

    dso = qm_get_def(iop_dso_by_handle, &_G.dsos_by_handle, handle, NULL);
    if (dso) {
        e_trace(1, "reuse dso %p (%*pM)", dso, LSTR_FMT_ARG(dso->path));
        dlclose(handle);
        return iop_dso_dup(dso);
    }

    if (lmid == LM_ID_NEWLM && dlinfo(handle, RTLD_DI_LMID, &lmid) < 0) {
        sb_setf(err, "unable to get lmid of plugin `%s`: %s", path,
                dlerror());
        return NULL;
    }

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

    dso = iop_dso_new();
    dso->path = lstr_dups(path, -1);
    dso->handle = handle;
    dso->lmid = lmid;
    dso->version = versionp ? *versionp : 0;
    dso->use_external_packages = !!dlsym(handle, "iop_use_external_packages");
    dso->dont_replace_fix_pkg = !!dlsym(handle, "iop_dont_replace_fix_pkg");

    e_trace(1, "open new dso %p (%*pM)", dso, LSTR_FMT_ARG(dso->path));

    if (iop_dso_register_(dso, err) < 0) {
        dso->handle = NULL;
        iop_dso_delete(&dso);
        return NULL;
    }

    qm_add(iop_dso_by_handle, &_G.dsos_by_handle, handle, dso);

    return dso;
}

static int iop_dso_reopen(iop_dso_t *dso, sb_t *err)
{
    e_trace(1, "reopen dso %p (%*pM)", dso, LSTR_FMT_ARG(dso->path));

    iop_dso_unload(dso);

    iop_pkg_clear(dso);
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
        iop_env_t env;
        iop_pkg_t **pkgp = dlsym(dso->handle, "iop_packages");

        if (!pkgp) {
            /* This should not happen because this was checked before. */
            e_panic("IOP DSO: iop_packages not found when registering DSO");
        }
        iop_pkg_clear(dso);
        iop_env_get(&env);
        while (*pkgp) {
            if (iopdso_register_pkg(dso, *pkgp++, &env, err) < 0) {
                iop_env_wipe(&env);
                return -1;
            }
        }
        RETHROW(iop_check_registered_classes(&env, err));
        iop_env_set(&env);
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
        iop_unregister_packages((const iop_pkg_t **)vec.tab, vec.len);
        dso->is_registered = false;
    }
}

static iop_rpc_t const *
find_rpc_in_iface(iop_iface_t const *iface, lstr_t fname)
{
    for (int i = 0; i < iface->funs_len; i++) {
        iop_rpc_t const *rpc = &iface->funs[i];

        if (lstr_equal(rpc->name, fname)) {
            return rpc;
        }
    }
    return NULL;
}

static iop_rpc_t const *
find_rpc_in_mod(iop_mod_t const *mod, lstr_t iface, lstr_t fname)
{
    for (int i = 0; i < mod->ifaces_len; i++) {
        const iop_iface_alias_t *alias = &mod->ifaces[i];

        if (lstr_equal(alias->name, iface)) {
            return find_rpc_in_iface(alias->iface, fname);
        }
    }
    return NULL;
}

iop_struct_t const *iop_dso_find_type(iop_dso_t const *dso, lstr_t name)
{
    const char *s;
    int what, pos;
    lstr_t fname;
    iop_rpc_t const *rpc;

    pos = qm_find_safe(iop_struct, &dso->struct_h, &name);
    if (pos >= 0) {
        return dso->struct_h.values[pos];
    }
    pos = qm_find_safe(iop_typedef, &dso->typedef_h, &name);
    if (pos >= 0 && iop_typedef_is_struct(dso->typedef_h.values[pos])) {
        return dso->typedef_h.values[pos]->ref_struct;
    }

    if (lstr_endswith(name, LSTR("Args"))) {
        name.len -= strlen("Args");
        what = 0;
    } else
    if (lstr_endswith(name, LSTR("Res"))) {
        name.len -= strlen("Res");
        what = 1;
    } else
    if (lstr_endswith(name, LSTR("Exn"))) {
        name.len -= strlen("Exn");
        what = 2;
    } else {
        return NULL;
    }

    /* name is some.path.to.pkg.{Module.iface.rpc|Iface.rpc} */
    s        = RETHROW_P(memrchr(name.s, '.', name.len));
    fname    = LSTR_INIT_V(s + 1, name.s + name.len - s - 1);
    name.len = s - name.s;

    s = RETHROW_P(memrchr(name.s, '.', name.len));
    if ('A' <= s[1] && s[1] <= 'Z') {
        /* name is an Iface */
        pos = RETHROW_NP(qm_find_safe(iop_iface, &dso->iface_h, &name));
        rpc = RETHROW_P(find_rpc_in_iface(dso->iface_h.values[pos], fname));
    } else {
        /* name is a Module.iface */
        lstr_t mod = LSTR_INIT(name.s, s - name.s);
        lstr_t ifn = LSTR_INIT(s + 1, name.s + name.len - (s + 1));

        pos = RETHROW_NP(qm_find_safe(iop_mod, &dso->mod_h, &mod));
        rpc = RETHROW_P(find_rpc_in_mod(dso->mod_h.values[pos], ifn, fname));
    }
    switch (what) {
      case 0:  return rpc->args;
      case 1:  return rpc->result;
      default: return rpc->exn;
    }
}

iop_enum_t const *iop_dso_find_enum(iop_dso_t const *dso, lstr_t name)
{
    int pos = qm_find_safe(iop_enum, &dso->enum_h, &name);
    if (pos >= 0) {
        return dso->enum_h.values[pos];
    }

    pos = qm_find_safe(iop_typedef, &dso->typedef_h, &name);
    return ((pos >= 0) && (dso->typedef_h.values[pos]->type == IOP_T_ENUM)) ?
           dso->typedef_h.values[pos]->ref_enum : NULL;
}

const void *const *iop_dso_get_ressources(const iop_dso_t *dso, lstr_t category)
{
    t_scope;
    lstr_t name = t_lstr_cat(LSTR("iop_dso_ressources_"), category);

    return dlsym(dso->handle, name.s);
}

void iop_dso_initialize(void)
{
    qm_init(iop_dso_by_handle, &_G.dsos_by_handle);
}

void iop_dso_shutdown(void)
{
    qm_wipe(iop_dso_by_handle, &_G.dsos_by_handle);
}
