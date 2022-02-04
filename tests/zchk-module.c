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

#include <lib-common/z.h>
#include <lib-common/el.h>
#include <lib-common/core.h>

/* mock module {{{ */

#define NEW_MOCK_MODULE(name, init_ret, shut_ret)                            \
    static int name##_initialize(void *args)                                 \
    {                                                                        \
        return init_ret;                                                     \
    }                                                                        \
    static int name##_shutdown(void)                                         \
    {                                                                        \
        return shut_ret;                                                     \
    }                                                                        \
    static MODULE_BEGIN(name)                                                \
    MODULE_END()

static MODULE_METHOD(PTR, DEPS_BEFORE, before);
static MODULE_METHOD(PTR, DEPS_AFTER, after);

NEW_MOCK_MODULE(mock_ic, 1, 1);
NEW_MOCK_MODULE(mock_log, 1, 1);
NEW_MOCK_MODULE(mock_platform, 1, 1);
NEW_MOCK_MODULE(mock_thr, 1, 1);


NEW_MOCK_MODULE(mod1, 1, 1);
NEW_MOCK_MODULE(mod2, 1, 4);
NEW_MOCK_MODULE(mod3, 1, 0);
NEW_MOCK_MODULE(mod4, 1, 1);
NEW_MOCK_MODULE(mod5, 1, 1);
NEW_MOCK_MODULE(mod6, 1, 0);

NEW_MOCK_MODULE(depmod1, 1, 1);
NEW_MOCK_MODULE(depmod2, 1, 1);
NEW_MOCK_MODULE(depmod3, 1, 1);

static _MODULE_ADD_DECLS(load_shut);

static struct _load_shut_state_g {
    bool loaded;
    bool initializing;
    bool shutting;
} load_shut_state_g;

static int load_shut_initialize(void *args)
{
    load_shut_state_g.loaded       = MODULE_IS_LOADED(load_shut);
    load_shut_state_g.initializing = MODULE_IS_INITIALIZING(load_shut);
    load_shut_state_g.shutting     = MODULE_IS_SHUTTING_DOWN(load_shut);
    return 0;
}

static int load_shut_shutdown(void)
{
    load_shut_state_g.loaded       = MODULE_IS_LOADED(load_shut);
    load_shut_state_g.initializing = MODULE_IS_INITIALIZING(load_shut);
    load_shut_state_g.shutting     = MODULE_IS_SHUTTING_DOWN(load_shut);
    return 0;
}

/* method {{{ */

int modmethod1;
int modmethod2;
int modmethod3;
int modmethod5;
int modmethod6;

static void modmethod1_ztst(data_t arg)
{
    modmethod1 = (*(int *)arg.ptr)++;
}

static void modmethod2_ztst(data_t arg)
{
    modmethod2 = (*(int *)arg.ptr)++;
}

static void modmethod3_ztst(data_t arg)
{
    modmethod3 = (*(int *)arg.ptr)++;
}

static void modmethod5_ztst(data_t arg)
{
    modmethod5 = (*(int *)arg.ptr)++;
}

static void modmethod6_ztst(data_t arg)
{
    modmethod6 = (*(int *)arg.ptr)++;
}

enum {
    DONT_RUN_METHOD_DURING_INITIALIZATION,
    RUN_METHOD_BEFORE_DURING_INITIALIZATION,
    RUN_METHOD_AFTER_DURING_INITIALIZATION,
    RUN_METHOD_BEFORE_DURING_SHUTDOWN,
    RUN_METHOD_AFTER_DURING_SHUTDOWN,
};

int modmethod1_run_method = DONT_RUN_METHOD_DURING_INITIALIZATION;
int val_method = 0;

NEW_MOCK_MODULE(modmethod2, 1, 1);
NEW_MOCK_MODULE(modmethod3, 1, 1);
NEW_MOCK_MODULE(modmethod4, 1, 1);
NEW_MOCK_MODULE(modmethod5, 1, 1);
NEW_MOCK_MODULE(modmethod6, 1, 1);

static int modmethod1_initialize(void *args)
{
    switch (modmethod1_run_method) {
      case RUN_METHOD_BEFORE_DURING_INITIALIZATION:
        MODULE_METHOD_RUN_PTR(before, &val_method);
        break;
      case RUN_METHOD_AFTER_DURING_INITIALIZATION:
        MODULE_METHOD_RUN_PTR(after, &val_method);
        break;
      default:
        break;
    }

    return 1;
}
static int modmethod1_shutdown(void)
{
    switch (modmethod1_run_method) {
      case RUN_METHOD_BEFORE_DURING_SHUTDOWN:
        MODULE_METHOD_RUN_PTR(before, &val_method);
        break;
      case RUN_METHOD_AFTER_DURING_SHUTDOWN:
        MODULE_METHOD_RUN_PTR(after, &val_method);
        break;
      default:
        break;
    }

    return 1;
}
static _MODULE_ADD_DECLS(modmethod1);

#undef NEW_MOCK_MODULE


static int module_arg_initialize(void * args)
{
    if (args == NULL)
        return -1;
    return *((int *)args);
}

static int module_arg_shutdown(void)
{
    return 1;
}
static _MODULE_ADD_DECLS(module_arg);

#define Z_MODULE_REGISTER(name)                                              \
    module_implement(MODULE(name), &name##_initialize,  &name##_shutdown,    \
                     NULL)

#define Z_MODULE_DEPENDS_ON(name, dep)                                       \
    module_add_dep(MODULE(name), MODULE(dep))

#define Z_MODULE_NEEDED_BY(name, need)                                       \
    module_add_dep(MODULE(need), MODULE(name))

/** Provide arguments in constructor. */
lstr_t *word_global;
lstr_t  provide_arg = LSTR_IMMED("HELLO");

MODULE_DECLARE(modprovide);

static int modprovide2_initialize(void *arg)
{
    return 0;
}
static int modprovide2_shutdown(void)
{
    return 0;
}
static MODULE_BEGIN(modprovide2)
    MODULE_PROVIDE(modprovide, &provide_arg);
    MODULE_DEPENDS_ON(modprovide);
MODULE_END()

static int modprovide_initialize(void *arg)
{
    word_global = arg;
    return 0;
}
static int modprovide_shutdown(void)
{
    return 0;
}
MODULE_BEGIN(modprovide)
MODULE_END()

/** Dependency checks
 * ex. module_a depends on module_b and module_c
 *
 *          module_a
 *         /        \
 *     module_b    module_c
 *                    \
 *                  module_d
 *
 *
 *          module_g    module_e
 *              |           |
 *          module_h    module_f
 *                  \  /
 *                module_i
 *
 */

#define MODULE_INIT_SHUTDOWN_FUNCTIONS(mod) \
    static int mod##_initialize(void *arg)                                   \
    {                                                                        \
        return 0;                                                            \
    }                                                                        \
    static int mod##_shutdown(void)                                          \
    {                                                                        \
        return 0;                                                            \
    }

MODULE_INIT_SHUTDOWN_FUNCTIONS(module_a)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_b)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_c)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_d)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_e)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_f)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_g)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_h)
MODULE_INIT_SHUTDOWN_FUNCTIONS(module_i)

#undef MODULE_INIT_SHUTDOWN_FUNCTIONS

static MODULE_BEGIN(module_i)
MODULE_END()

static MODULE_BEGIN(module_h)
    MODULE_DEPENDS_ON(module_i);
MODULE_END()

static MODULE_BEGIN(module_g)
    MODULE_DEPENDS_ON(module_h);
MODULE_END()

static MODULE_BEGIN(module_f)
    MODULE_DEPENDS_ON(module_i);
MODULE_END()

static MODULE_BEGIN(module_e)
    MODULE_DEPENDS_ON(module_f);
MODULE_END()

static MODULE_BEGIN(module_d)
MODULE_END()

static MODULE_BEGIN(module_c)
    MODULE_DEPENDS_ON(module_d);
MODULE_END()

static MODULE_BEGIN(module_b)
MODULE_END()

static MODULE_BEGIN(module_a)
    MODULE_DEPENDS_ON(module_b);
    MODULE_DEPENDS_ON(module_c);
MODULE_END()

/* }}} */

Z_GROUP_EXPORT(module)
{
/* basic behavior {{{ */

    Z_TEST(basic,  "basic registering require shutdown") {
        /*         platform
         *        /   |    \
         *       /    |     \
         *      ic   thr    log
         */

        Z_MODULE_DEPENDS_ON(mock_platform, mock_thr);
        Z_MODULE_DEPENDS_ON(mock_platform, mock_log);
        Z_MODULE_DEPENDS_ON(mock_platform, mock_ic);

        MODULE_REQUIRE(mock_log);
        MODULE_REQUIRE(mock_thr);
        MODULE_REQUIRE(mock_ic);
        MODULE_REQUIRE(mock_platform);
        Z_ASSERT(MODULE_IS_LOADED(mock_log));
        Z_ASSERT(MODULE_IS_LOADED(mock_thr));
        Z_ASSERT(MODULE_IS_LOADED(mock_ic));
        Z_ASSERT(MODULE_IS_LOADED(mock_platform));

        MODULE_RELEASE(mock_platform);
        Z_ASSERT(MODULE_IS_LOADED(mock_log));
        Z_ASSERT(MODULE_IS_LOADED(mock_thr));
        Z_ASSERT(MODULE_IS_LOADED(mock_ic));
        Z_ASSERT(!MODULE_IS_LOADED(mock_platform),
                 "mock_platform should be shutdown");

        MODULE_RELEASE(mock_log);
        Z_ASSERT(!MODULE_IS_LOADED(mock_log));
        Z_ASSERT(MODULE_IS_LOADED(mock_thr));
        Z_ASSERT(MODULE_IS_LOADED(mock_ic));
        Z_ASSERT(!MODULE_IS_LOADED(mock_platform),
                 "mock_platform should be shutdown");
        MODULE_RELEASE(mock_thr);
        Z_ASSERT(!MODULE_IS_LOADED(mock_log));
        Z_ASSERT(!MODULE_IS_LOADED(mock_thr));
        Z_ASSERT(MODULE_IS_LOADED(mock_ic));
        Z_ASSERT(!MODULE_IS_LOADED(mock_platform),
                 "mock_platform should be shutdown");
        MODULE_RELEASE(mock_ic);


        Z_ASSERT(!MODULE_IS_LOADED(mock_log), "mock_log should be shutdown");
        Z_ASSERT(!MODULE_IS_LOADED(mock_ic), "mock_ic should be shutdown");
        Z_ASSERT(!MODULE_IS_LOADED(mock_thr), "mock_thr should be shutdown");
        Z_ASSERT(!MODULE_IS_LOADED(mock_platform),
                 "mock_platform should be shutdown");
    } Z_TEST_END;

    Z_TEST(basic2,  "Require submodule") {
       MODULE_REQUIRE(mock_platform);
       MODULE_REQUIRE(mock_ic);
       Z_ASSERT(MODULE_IS_LOADED(mock_ic));
       MODULE_RELEASE(mock_platform);
       Z_ASSERT(!MODULE_IS_LOADED(mock_thr));
       Z_ASSERT(!MODULE_IS_LOADED(mock_log));
       Z_ASSERT(MODULE_IS_LOADED(mock_ic));
       MODULE_RELEASE(mock_ic);
       Z_ASSERT(!MODULE_IS_LOADED(mock_ic));
    } Z_TEST_END;

    Z_TEST(load_shut, "Initialize and shutting down states") {
        Z_MODULE_REGISTER(load_shut);
        Z_ASSERT(!MODULE_IS_LOADED(load_shut));
        Z_ASSERT(!MODULE_IS_INITIALIZING(load_shut));
        Z_ASSERT(!MODULE_IS_SHUTTING_DOWN(load_shut));
        MODULE_REQUIRE(load_shut);
        Z_ASSERT(load_shut_state_g.loaded       == false);
        Z_ASSERT(load_shut_state_g.initializing == true);
        Z_ASSERT(load_shut_state_g.shutting     == false);
        Z_ASSERT( MODULE_IS_LOADED(load_shut));
        Z_ASSERT(!MODULE_IS_INITIALIZING(load_shut));
        Z_ASSERT(!MODULE_IS_SHUTTING_DOWN(load_shut));
        MODULE_RELEASE(load_shut);
        Z_ASSERT(load_shut_state_g.loaded       == false);
        Z_ASSERT(load_shut_state_g.initializing == false);
        Z_ASSERT(load_shut_state_g.shutting     == true);
        Z_ASSERT(!MODULE_IS_LOADED(load_shut));
        Z_ASSERT(!MODULE_IS_INITIALIZING(load_shut));
        Z_ASSERT(!MODULE_IS_SHUTTING_DOWN(load_shut));
    } Z_TEST_END;

    Z_TEST(use_case1,  "Use case1") {
      /*           mod1           mod6
       *         /   |   \         |
       *        /    |    \        |
       *      mod2  mod3  mod4    mod2
       *             |
       *             |
       *            mod5
       */
      Z_MODULE_DEPENDS_ON(mod1, mod2);
      Z_MODULE_DEPENDS_ON(mod1, mod3);
      Z_MODULE_DEPENDS_ON(mod1, mod4);
      Z_MODULE_DEPENDS_ON(mod3, mod5);
      Z_MODULE_DEPENDS_ON(mod6, mod2);

      /* Test 1 All init work and shutdown work */
      MODULE_REQUIRE(mod1);
      Z_ASSERT(MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(MODULE_IS_LOADED(mod3));
      Z_ASSERT(MODULE_IS_LOADED(mod4));
      Z_ASSERT(MODULE_IS_LOADED(mod5));
      Z_ASSERT(!MODULE_IS_LOADED(mod6));
      MODULE_REQUIRE(mod1);
      Z_ASSERT(MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(MODULE_IS_LOADED(mod3));
      Z_ASSERT(MODULE_IS_LOADED(mod4));
      Z_ASSERT(MODULE_IS_LOADED(mod5));
      Z_ASSERT(!MODULE_IS_LOADED(mod6));
      MODULE_REQUIRE(mod6);
      Z_ASSERT(MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(MODULE_IS_LOADED(mod3));
      Z_ASSERT(MODULE_IS_LOADED(mod4));
      Z_ASSERT(MODULE_IS_LOADED(mod5));
      Z_ASSERT(MODULE_IS_LOADED(mod6));
      MODULE_REQUIRE(mod3);
      Z_ASSERT(MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(MODULE_IS_LOADED(mod3));
      Z_ASSERT(MODULE_IS_LOADED(mod4));
      Z_ASSERT(MODULE_IS_LOADED(mod5));
      Z_ASSERT(MODULE_IS_LOADED(mod6));

      MODULE_RELEASE(mod3);
      Z_ASSERT(MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(MODULE_IS_LOADED(mod3));
      Z_ASSERT(MODULE_IS_LOADED(mod4));
      Z_ASSERT(MODULE_IS_LOADED(mod5));
      Z_ASSERT(MODULE_IS_LOADED(mod6));
      MODULE_RELEASE(mod1);
      Z_ASSERT(MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(MODULE_IS_LOADED(mod3));
      Z_ASSERT(MODULE_IS_LOADED(mod4));
      Z_ASSERT(MODULE_IS_LOADED(mod5));
      Z_ASSERT(MODULE_IS_LOADED(mod6));
      MODULE_RELEASE(mod1);
      Z_ASSERT(!MODULE_IS_LOADED(mod1));
      Z_ASSERT(MODULE_IS_LOADED(mod2));
      Z_ASSERT(!MODULE_IS_LOADED(mod3));
      Z_ASSERT(!MODULE_IS_LOADED(mod4));
      Z_ASSERT(!MODULE_IS_LOADED(mod5));
      Z_ASSERT(MODULE_IS_LOADED(mod6));
      MODULE_RELEASE(mod6);
      Z_ASSERT(!MODULE_IS_LOADED(mod1));
      Z_ASSERT(!MODULE_IS_LOADED(mod2));
      Z_ASSERT(!MODULE_IS_LOADED(mod3));
      Z_ASSERT(!MODULE_IS_LOADED(mod4));
      Z_ASSERT(!MODULE_IS_LOADED(mod5));
      Z_ASSERT(!MODULE_IS_LOADED(mod6));

    } Z_TEST_END;

/* }}} */
/* provide {{{ */

    Z_TEST(provide,  "Provide") {
        int a = 4;

        Z_MODULE_REGISTER(module_arg);
        MODULE_PROVIDE(module_arg, &a);
        MODULE_PROVIDE(module_arg, &a);
        MODULE_REQUIRE(module_arg);
        Z_ASSERT(MODULE_IS_LOADED(module_arg));
        MODULE_RELEASE(module_arg);
    } Z_TEST_END;

    Z_TEST(provide_constructor, "provide constructor") {
        MODULE_REQUIRE(modprovide2);
        Z_ASSERT_LSTREQUAL(*word_global, provide_arg);
        MODULE_RELEASE(modprovide2);
    } Z_TEST_END;

/* }}} */
/* methods {{{ */

    Z_TEST(method, "Methods") {
        Z_MODULE_REGISTER(modmethod1);
        Z_MODULE_DEPENDS_ON(modmethod1, modmethod2);
        module_implement_method(MODULE(modmethod1), &after_method,
                                &modmethod1_ztst);
        module_implement_method(MODULE(modmethod1), &before_method,
                                &modmethod1_ztst);

        Z_MODULE_DEPENDS_ON(modmethod2, modmethod3);
        module_implement_method(MODULE(modmethod2), &after_method,
                                &modmethod2_ztst);
        module_implement_method(MODULE(modmethod2), &before_method,
                                &modmethod2_ztst);

        Z_MODULE_DEPENDS_ON(modmethod3, modmethod4);
        module_implement_method(MODULE(modmethod3), &after_method,
                                &modmethod3_ztst);
        module_implement_method(MODULE(modmethod3), &before_method,
                                &modmethod3_ztst);

        Z_MODULE_DEPENDS_ON(modmethod4, modmethod5);
        module_implement_method(MODULE(modmethod5), &after_method,
                                &modmethod5_ztst);
        module_implement_method(MODULE(modmethod5), &before_method,
                                &modmethod5_ztst);

        Z_MODULE_DEPENDS_ON(modmethod6, modmethod5);
        module_implement_method(MODULE(modmethod6), &after_method,
                                &modmethod6_ztst);
        module_implement_method(MODULE(modmethod6), &before_method,
                                &modmethod6_ztst);

        val_method = 1;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_METHOD_RUN_PTR(after, &val_method);
        Z_ASSERT_ZERO(modmethod1);
        Z_ASSERT_ZERO(modmethod2);
        Z_ASSERT_ZERO(modmethod3);
        Z_ASSERT_ZERO(modmethod5);
        Z_ASSERT_ZERO(modmethod6);
        Z_ASSERT_EQ(val_method, 1);

        MODULE_REQUIRE(modmethod1);

        val_method = 1;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_METHOD_RUN_PTR(after, &val_method);
        Z_ASSERT_EQ(modmethod1, 1);
        Z_ASSERT_EQ(modmethod2, 2);
        Z_ASSERT_EQ(modmethod3, 3);
        Z_ASSERT_EQ(modmethod5, 4);
        Z_ASSERT_ZERO(modmethod6);
        Z_ASSERT_EQ(val_method, 5);

        val_method = 1;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_METHOD_RUN_PTR(before, &val_method);
        Z_ASSERT_EQ(modmethod1, 4);
        Z_ASSERT_EQ(modmethod2, 3);
        Z_ASSERT_EQ(modmethod3, 2);
        Z_ASSERT_EQ(modmethod5, 1);
        Z_ASSERT_ZERO(modmethod6);
        Z_ASSERT_EQ(val_method, 5);

        MODULE_REQUIRE(modmethod6);

        val_method = 1;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_METHOD_RUN_PTR(after, &val_method);
        Z_ASSERT_LT(modmethod1, modmethod2);
        Z_ASSERT_LT(modmethod2, modmethod3);
        Z_ASSERT_LT(modmethod3, modmethod5);
        Z_ASSERT_LT(modmethod6, modmethod5);
        Z_ASSERT(modmethod1);
        Z_ASSERT(modmethod6);
        Z_ASSERT_EQ(val_method, 6);

        val_method = 1;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_METHOD_RUN_PTR(before, &val_method);
        Z_ASSERT_GT(modmethod1, modmethod2);
        Z_ASSERT_GT(modmethod2, modmethod3);
        Z_ASSERT_GT(modmethod3, modmethod5);
        Z_ASSERT_GT(modmethod6, modmethod5);
        Z_ASSERT(modmethod5);
        Z_ASSERT_EQ(val_method, 6);


        MODULE_RELEASE(modmethod6);
        MODULE_RELEASE(modmethod1);

        val_method = 1;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_METHOD_RUN_PTR(after, &val_method);
        Z_ASSERT_ZERO(modmethod1);
        Z_ASSERT_ZERO(modmethod2);
        Z_ASSERT_ZERO(modmethod3);
        Z_ASSERT_ZERO(modmethod5);
        Z_ASSERT_ZERO(modmethod6);
        Z_ASSERT_EQ(val_method, 1);

        val_method = 1;
        modmethod1_run_method = RUN_METHOD_BEFORE_DURING_INITIALIZATION;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_REQUIRE(modmethod1);
        Z_ASSERT_GT(modmethod2, modmethod3);
        Z_ASSERT_GT(modmethod3, modmethod5);
        Z_ASSERT_EQ(val_method, 4);
        MODULE_RELEASE(modmethod1);

        val_method = 1;
        modmethod1_run_method = RUN_METHOD_AFTER_DURING_INITIALIZATION;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_REQUIRE(modmethod1);
        Z_ASSERT_GT(modmethod5, modmethod3);
        Z_ASSERT_GT(modmethod3, modmethod2);
        Z_ASSERT_EQ(val_method, 4);
        MODULE_RELEASE(modmethod1);

        val_method = 1;
        modmethod1_run_method = RUN_METHOD_BEFORE_DURING_INITIALIZATION;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_REQUIRE(modmethod6);
        MODULE_REQUIRE(modmethod1);
        Z_ASSERT_GT(modmethod6, modmethod5);
        Z_ASSERT_GT(modmethod3, modmethod5);
        Z_ASSERT_GT(modmethod2, modmethod3);
        Z_ASSERT_EQ(val_method, 5);
        MODULE_RELEASE(modmethod1);
        MODULE_RELEASE(modmethod6);

        val_method = 1;
        modmethod1_run_method = RUN_METHOD_AFTER_DURING_INITIALIZATION;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_REQUIRE(modmethod6);
        MODULE_REQUIRE(modmethod1);
        Z_ASSERT_GT(modmethod5, modmethod6);
        Z_ASSERT_GT(modmethod5, modmethod3);
        Z_ASSERT_GT(modmethod3, modmethod2);
        Z_ASSERT_EQ(val_method, 5);
        MODULE_RELEASE(modmethod1);
        MODULE_RELEASE(modmethod6);

        /* call method on shutdown -- deps before */
        val_method = 1;
        modmethod1_run_method = RUN_METHOD_BEFORE_DURING_SHUTDOWN;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_REQUIRE(modmethod1);
        MODULE_RELEASE(modmethod1);
        /* modmethod1 is shutting down, not called */
        Z_ASSERT_EQ(modmethod1, 0);
        /* modmethod1 dependencies are still loaded */
        Z_ASSERT_GT(modmethod2, modmethod3);
        Z_ASSERT_GT(modmethod3, modmethod5);
        Z_ASSERT_EQ(val_method, 4);

        /* call method on shutdown -- deps after */
        val_method = 1;
        modmethod1_run_method = RUN_METHOD_AFTER_DURING_SHUTDOWN;
        modmethod1 = modmethod2 = modmethod3 = modmethod5 = modmethod6 = 0;
        MODULE_REQUIRE(modmethod1);
        MODULE_RELEASE(modmethod1);
        /* modmethod1 is shutting down, not called */
        Z_ASSERT_EQ(modmethod1, 0);
        /* modmethod1 dependencies are still loaded */
        Z_ASSERT_GT(modmethod3, modmethod2);
        Z_ASSERT_GT(modmethod5, modmethod3);
        Z_ASSERT_EQ(val_method, 4);

    } Z_TEST_END;

/* }}} */
/* invert dependency {{{ */

    Z_TEST(invert_dependency, "invert dependency") {
        Z_MODULE_DEPENDS_ON(depmod1, depmod2);
        Z_MODULE_NEEDED_BY(depmod3, depmod1);

        MODULE_REQUIRE(depmod1);

        Z_ASSERT(MODULE_IS_LOADED(depmod1));
        Z_ASSERT(MODULE_IS_LOADED(depmod2));
        Z_ASSERT(MODULE_IS_LOADED(depmod3));

        MODULE_RELEASE(depmod1);

        Z_ASSERT(!MODULE_IS_LOADED(depmod1));
        Z_ASSERT(!MODULE_IS_LOADED(depmod2));
        Z_ASSERT(!MODULE_IS_LOADED(depmod3));

    } Z_TEST_END;

/* }}} */
/* dependency check {{{ */

    Z_TEST(dependency, "Modules dependency check") {
        module_t *liste1[] = { MODULE(module_a), MODULE(module_e) };
        module_t *liste2[] = { MODULE(module_a), MODULE(module_e),
                               MODULE(module_g) };
        module_t *liste3[] = { MODULE(module_a), MODULE(module_e),
                               MODULE(module_i) };
        lstr_t collision;

        Z_ASSERT_N(module_check_no_dependencies(liste1, countof(liste1),
                                                &collision));
        Z_ASSERT_N(module_check_no_dependencies(liste2, countof(liste2),
                                                &collision));
        Z_ASSERT_NEG(module_check_no_dependencies(liste3, countof(liste3),
                                                  &collision));
        Z_ASSERT_LSTREQUAL(collision,
                           LSTR(module_get_name(MODULE(module_i))));
    } Z_TEST_END;

/* }}} */

} Z_GROUP_END;
