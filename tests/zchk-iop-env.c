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

/* LCOV_EXCL_START */

#include <pthread.h>
#include <stdatomic.h>

#include <lib-common/core.h>
#include <lib-common/z.h>
#include <lib-common/iop/priv.h>

#include "iop/tstiop.iop.h"
#include "iop/tstiop_dox.iop.h"
#include "iop/tstiop_inheritance.iop.h"
#include "iop/tstiop_backward_compat.iop.h"
#include "iop/tstiop_typedef.iop.h"
#include "iop/tstiop_void_type.iop.h"

struct {
    iop_env_t *iop_env;
} zchk_iop_env_g;
#define _G zchk_iop_env_g

/* {{{ IOP env testing helpers */

static int z_dso_open(const char *dso_path, bool in_cmddir,
                      iop_env_t *iop_env, iop_dso_t **dsop)
{
    t_scope;
    SB_1k(err);
    lstr_t path = LSTR(dso_path);
    iop_dso_t *dso;

    if (in_cmddir) {
        path = t_lstr_cat(z_cmddir_g, path);
    }
    dso = iop_dso_open(iop_env, path.s, &err);
    Z_ASSERT_P(dso, "unable to load `%s`: %*pM",
               path.s, SB_FMT_ARG(&err));

    *dsop = dso;
    Z_HELPER_END;
}

static int z_iop_env_is_empty(const iop_env_t *iop_env)
{
    const iop_env_ctx_t *ctx;

    iop_env_ctx_acquire_scoped(iop_env, ctx);
    Z_ASSERT_EQ(qm_len(iop_class_by_id, &ctx->classes_by_id), 0);
    Z_ASSERT_EQ(qm_len(iop_dso_by_pkg, &ctx->dso_by_pkg), 0);
    Z_ASSERT_EQ(qm_len(iop_env_struct, &ctx->struct_by_fullname), 0);
    Z_ASSERT_EQ(qm_len(iop_enum, &ctx->enum_by_fullname), 0);
    Z_ASSERT_EQ(qm_len(iop_typedef, &ctx->typedef_by_fullname), 0);
    Z_ASSERT_EQ(qm_len(iop_iface, &ctx->iface_by_fullname), 0);
    Z_ASSERT_EQ(qm_len(iop_mod, &ctx->mod_by_fullname), 0);
    Z_ASSERT_EQ(qm_len(iop_pkg, &ctx->pkg_by_fullname), 0);

    Z_HELPER_END;
}

/* }}} */

Z_GROUP_EXPORT(iop_env)
{
    _G.iop_env = iop_env_new();
    IOP_REGISTER_PACKAGES(_G.iop_env,
                          &tstiop__pkg,
                          &tstiop_dox__pkg,
                          &tstiop_inheritance__pkg,
                          &tstiop_backward_compat__pkg,
                          &tstiop_typedef__pkg);

    Z_TEST(getter, "environment object getters") { /* {{{ */
        lstr_t name;
        const iop_struct_t *st;
        const iop_struct_t *cls;
        const iop_enum_t *en;
        const iop_typedef_t *td;
        const iop_iface_t *iface;
        const iop_mod_t *mod;
        const iop_pkg_t *pkg;

        /* Struct */
        name = tstiop__my_struct_a__s.fullname;
        Z_ASSERT_P((st = iop_env_get_struct(_G.iop_env, name)),
                   "cannot find struct obj `%pL'", &name);
        Z_ASSERT(st == &tstiop__my_struct_a__s,
                 "wrong iop_struct_t (got `%pL')", &st->fullname);

        Z_ASSERT_NULL(iop_env_get_enum(_G.iop_env, name),
                      "`%pL' is not an enum", &name);
        Z_ASSERT_NULL(iop_get_class_by_fullname(_G.iop_env,
                                                &tstiop__my_class1__s, name),
                      "`%pL' is not a class", &name);

        /* Enum */
        name = tstiop__my_enum_c__e.fullname;
        Z_ASSERT_P((en = iop_env_get_enum(_G.iop_env, name)),
                   "cannot find enum obj `%pL'", &name);
        Z_ASSERT(en == &tstiop__my_enum_c__e,
                 "wrong iop_enum_t (got `%pL')", &en->fullname);

        /* Class */
        name = tstiop__my_class3__s.fullname;
        Z_ASSERT_P((cls = iop_env_get_struct(_G.iop_env, name)),
                   "cannot find class obj `%pL'", &name);
        Z_ASSERT(cls == &tstiop__my_class3__s,
                 "wrong iop_struct_t (got `%pL')", &cls->fullname);

        cls = iop_get_class_by_fullname(_G.iop_env, &tstiop__my_class1__s,
                                        name);
        Z_ASSERT_P(cls, "cannot find class `%pL'", &name);
        Z_ASSERT(cls == &tstiop__my_class3__s,
                 "wrong IOP class (got `%pL')", &cls->fullname);

        cls = iop_get_class_by_id(_G.iop_env, &tstiop__my_class1__s,
                                  tstiop__my_class3__s.class_attrs->class_id);
        Z_ASSERT_P(cls, "cannot find class `%pL' from ID", &name);
        Z_ASSERT(cls == &tstiop__my_class3__s, "wrong IOP class (got `%pL')",
                 &cls->fullname);

        /* Typedef */
        /* tstiop_void_type package is registered with tstiop package since
         * tstiop_void_type.VoidRequired is referenced by tstiop.VoidPkgRef.
         */
        name = tstiop_void_type__void_required__s.fullname;
        Z_ASSERT_P((st = iop_env_get_struct(_G.iop_env, name)),
                   "cannot find struct obj `%pL'", &name);
        Z_ASSERT(st == &tstiop_void_type__void_required__s,
                 "wrong iop_struct_t (got `%pL')", &st->fullname);

        name = tstiop__small_class_typedef__td.fullname;
        Z_ASSERT_P((td = iop_env_get_typedef(_G.iop_env, name)),
                   "cannot find typedef obj `%pL'", &name);
        Z_ASSERT(td == &tstiop__small_class_typedef__td,
                 "wrong iop_typedef_t (got `%pL')", &td->fullname);

        /* Interface */
        name = tstiop__my_iface_a__if.fullname;
        Z_ASSERT_P((iface = iop_env_get_iface(_G.iop_env, name)),
                   "cannot find iface obj `%pL'", &name);
        Z_ASSERT(iface == &tstiop__my_iface_a__if,
                 "wrong iop_iface_t (got `%pL')", &iface->fullname);

        /* Module */
        name = tstiop__my_mod_a__mod.fullname;
        Z_ASSERT_P((mod = iop_env_get_mod(_G.iop_env, name)),
                   "cannot find mod obj `%pL'", &name);
        Z_ASSERT(mod == &tstiop__my_mod_a__mod,
                 "wrong iop_mod_t (got `%pL')", &mod->fullname);

        /* Package */
        name = tstiop__pkg.name;
        Z_ASSERT_P((pkg = iop_env_get_pkg(_G.iop_env, name)),
                   "cannot find pkg obj `%pL'", &name);
        Z_ASSERT(pkg == &tstiop__pkg,
                 "wrong iop_pkg_t (got `%pL')", &pkg->name);

        /* Test same name between IOP struct and interface. */
        name = tstiop__obj_same_name__s.fullname;
        Z_ASSERT_P((st = iop_env_get_struct(_G.iop_env, name)),
                   "cannot find struct obj `%pL'", &name);
        Z_ASSERT(st == &tstiop__obj_same_name__s,
                 "wrong iop_struct_t (got `%pL')", &st->fullname);

        Z_ASSERT_P((iface = iop_env_get_iface(_G.iop_env, name)),
                   "cannot find iface obj `%pL'", &name);
        Z_ASSERT(iface == &tstiop__obj_same_name__if,
                 "wrong iop_iface_t (got `%pL')", &iface->fullname);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(isolation, "test IOP environment isolation") { /* {{{ */
        iop_env_t *iop_env_tstiop;
        iop_env_t *iop_env_backward_old;
        iop_env_t *iop_env_backward_new;
        iop_dso_t *dso_tstiop;
        iop_dso_t *dso_backward_old;
        iop_dso_t *dso_backward_new;
        const iop_struct_t *st1;
        const iop_struct_t *st2;

        /* Create the test envionments */
        iop_env_tstiop = iop_env_new();
        iop_env_backward_old = iop_env_new();
        iop_env_backward_new = iop_env_new();

        /* Open the DSOs in their respective envionments */
        Z_HELPER_RUN(z_dso_open(
            "iop/zchk-tstiop-plugin" SO_FILEEXT, true, iop_env_tstiop,
            &dso_tstiop));
        Z_HELPER_RUN(z_dso_open(
            "iop/backward-compat/old/zchk-tstiop-backward-"
            "compat-typedef-old" SO_FILEEXT, true, iop_env_backward_old,
            &dso_backward_old));
        Z_HELPER_RUN(z_dso_open(
            "iop/backward-compat/new/zchk-tstiop-backward-"
            "compat-typedef-new" SO_FILEEXT, true, iop_env_backward_new,
            &dso_backward_new));

        /* Check IOP obj between global zchk IOP env and tstiop DSO IOP env */
        st1 = iop_env_get_struct(_G.iop_env, LSTR("tstiop.MyClass1"));
        st2 = iop_env_get_struct(iop_env_tstiop, LSTR("tstiop.MyClass1"));
        Z_ASSERT_P(st1, "`tstiop.MyClass1` should exist in zchk IOP env");
        Z_ASSERT(st1 == &tstiop__my_class1__s,
                 "`tstiop.MyClass1` should be taken from the compiled zchk "
                 "IOP env");
        Z_ASSERT_P(st2, "`tstiop.MyClass1` should exist in tstiop DSO IOP "
                   "env");
        Z_ASSERT(st1 != st2, "`tstiop.MyClass1` should be different between "
                 "the zchk IOP env and the tstiop DSO IOP env");

        /* Check IOP obj between backward compat IOP DSO */
        st1 = iop_env_get_struct(
            iop_env_backward_old,
            LSTR("tstiop_backward_compat_typedef.MyClass1"));
        st2 = iop_env_get_struct(
            iop_env_backward_new,
            LSTR("tstiop_backward_compat_typedef.MyClass1"));
        Z_ASSERT_P(st1, "`tstiop_backward_compat_typedef.MyClass1` should "
                   "exist in backward old DSO IOP env");
        Z_ASSERT_P(st2, "`tstiop_backward_compat_typedef.MyClass1` should "
                   "exist in backward new DSO IOP env");
        Z_ASSERT(st1 != st2, "`tstiop_backward_compat_typedef.MyClass1` "
                 "should be different between backward old and new DSO IOP "
                 "env");

        /* Check no contaminations of other IOP envs */
        st1 = iop_env_get_struct(
            _G.iop_env, LSTR("tstiop_backward_compat_typedef.MyClass1"));
        Z_ASSERT_NULL(st1, "`tstiop_backward_compat_typedef.MyClass1` should "
                   "not exist in zchk IOP env");

        st1 = iop_env_get_struct(
            iop_env_tstiop,
            LSTR("tstiop_backward_compat_typedef.MyClass1"));
        Z_ASSERT_NULL(st1, "`tstiop_backward_compat_typedef.MyClass1` should "
                   "not exist in tstiop DSO IOP env");

        st1 = iop_env_get_struct(
            iop_env_backward_old, LSTR("tstiop.MyClass1"));
        Z_ASSERT_NULL(st1, "`tstiop.MyClass1` should not exist in backward "
                      "old DSO IOP env");

        st1 = iop_env_get_struct(
            iop_env_backward_new, LSTR("tstiop.MyClass1"));
        Z_ASSERT_NULL(st1, "`tstiop.MyClass1` should not exist in backward "
                      "new DSO IOP env");

        /* Cleanup */
        iop_dso_close(&dso_backward_new);
        iop_dso_close(&dso_backward_old);
        iop_dso_close(&dso_tstiop);
        iop_env_delete(&iop_env_backward_old);
        iop_env_delete(&iop_env_backward_new);
        iop_env_delete(&iop_env_tstiop);
    } Z_TEST_END;
    /* }}} */
    Z_TEST(iop_dso_unregister, "test IOP DSO unregister do not pollute the IOP env") { /* {{{ */
        iop_env_t *iop_env;
        iop_dso_t *dso_backward_old;
        iop_dso_t *dso_backward_new;

        /* Create the test environment */
        iop_env = iop_env_new();

        /* Open the old DSO */
        Z_HELPER_RUN(z_dso_open(
            "iop/backward-compat/old/zchk-tstiop-backward-"
            "compat-typedef-old" SO_FILEEXT, true, iop_env,
            &dso_backward_old));

        /* Close the old DSO */
        iop_dso_close(&dso_backward_old);

        /* The IOP env should be empty */
        Z_HELPER_RUN(z_iop_env_is_empty(iop_env));

        /* Open the new DSO, this should not conflict with the old DSO */
        Z_HELPER_RUN(z_dso_open(
            "iop/backward-compat/new/zchk-tstiop-backward-"
            "compat-typedef-new" SO_FILEEXT, true, iop_env,
            &dso_backward_new));

        /* Close the new DSO */
        iop_dso_close(&dso_backward_new);

        /* The IOP env should be empty */
        Z_HELPER_RUN(z_iop_env_is_empty(iop_env));

        /* Clean up the env */
        iop_env_delete(&iop_env);
    } Z_TEST_END;
    /* }}} */

    iop_env_delete(&_G.iop_env);
} Z_GROUP_END

/* LCOV_EXCL_STOP */
