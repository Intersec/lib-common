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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_MODULE_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_MODULE_H

/* {{{ Types */

/** Opaque type that defines a module.
 */
typedef struct module_t module_t;
typedef module_t *module_ptr_t;
typedef module_t * nullable *module_ptr_ptr_t;

/** Describe the evaluation order of the method.
 */
typedef enum module_order_t {
    /** The method should be run in the dependencies before being run in the
     * dependent module.
     */
    MODULE_DEPS_BEFORE,

    /** The method should be run in the dependent module before being run in
     * dependencies.
     */
    MODULE_DEPS_AFTER
} module_order_t;

/** Method protypes.
 */
typedef enum module_method_type_t {
    METHOD_VOID,
    METHOD_INT,
    METHOD_PTR,
    METHOD_GENERIC,
} module_method_type_t;

/** Opaque type that defines a custom module method.
 */
typedef struct module_method_t {
    module_method_type_t type;
    module_order_t order;
} module_method_t;

/* }}} */
/* {{{ Methods */

/** Declare a new method.
 *
 * This macro should be called in headers files to export a method.
 *
 * \param[in] Type prototype of the method. One of \ref module_method_type_t.
 * \param[in] name name of the method.
 */
#define MODULE_METHOD_DECLARE(Type, Order, name)  \
    extern const module_method_t name##_method;                              \
    extern const module_method_t * const nonnull name##_method_ptr

/** Define a new method.
 *
 * This macro should be called in an implementation file to define a method.
 * It can be prefixed by static in order to create a local definition. If the
 * method is public, it must be declared using \ref MODULE_METHOD_DECLARE.
 *
 * \param[in] Type prototype of the method.
 * \param[in] name name of the method.
 */
#define MODULE_METHOD(Type, Order, name)  \
    const module_method_t name##_method = {                                  \
        .type  = METHOD_##Type,                                              \
        .order = MODULE_##Order,                                             \
    };                                                                       \
    const module_method_t * const name##_method_ptr = &name##_method

/** Run a void method.
 *
 * This will run the method whose name has been provided.
 *
 * This macro is only suitable for void methods.
 *
 * \see module_run_method
 */
#define MODULE_METHOD_RUN_VOID(method)  do {                                 \
        assert (method##_method.type == METHOD_VOID);                        \
        module_run_method(&method##_method, (data_t)NULL);                   \
    } while (0)

/** Run a pointer method.
 *
 * This will run the method whose name has been provided and passing the given
 * pointer as argument.
 *
 * This macro is only suitable for methods that take a pointer as argument.
 *
 * \see module_run_method
 */
#define MODULE_METHOD_RUN_PTR(method, arg)  do {                             \
        assert (method##_method.type == METHOD_PTR);                         \
        module_run_method(&method##_method, (data_t){ .ptr = arg });         \
    } while (0)

/** Run an integer method.
 *
 * This will run the method whose name has been provided and passing the given
 * integer as argument.
 *
 * This macro is only suitable for methods that take an integer as argument.
 *
 * \see module_run_method
 */
#define MODULE_METHOD_RUN_INT(method, arg)  do {                             \
        assert (method##_method.type == METHOD_INT);                         \
        module_run_method(&method##_method, (data_t){ .u32 = arg });         \
    } while (0)

/** Run a method.
 *
 * This will run the method whose name has been provided with the given \ref
 * data_t object.
 *
 * This macro is only suitable for generic methods.
 *
 * \see module_run_method
 */
#define MODULE_METHOD_RUN(method, data)  do {                                \
        assert (method##_method.type == METHOD_GENERIC);                     \
        module_run_method(&method##_method, data);                           \
    } while (0)

/** Run a method.
 *
 * Traverse the list of started modules and run the given method in all
 * modules that implement it according to the given
 * \ref module_method_t::order. If the method is non-void, it will receive
 * the provided \p arg as argument.
 *
 * \param[in] method The method to call.
 * \param[in] arg Argument passed to the methods.
 */
__attr_nonnull__((1))
void module_run_method(const module_method_t * nonnull method, data_t arg);

/* }}} */
/* {{{ Module creation */

/** Pointer to the module of the given name.
 */
#define MODULE(name)  name##_get_module()

/** Declare a module.
 *
 * This macro declares a module variable.
 */
#define MODULE_DECLARE(name)                                                 \
    module_t * nonnull name##_get_module(void)

/** Add declarations of a module.
 *
 * Do not use this, use \ref MODULE_BEGIN.
 */
#define _MODULE_ADD_DECLS(name)                                              \
    module_t *name##_get_module(void)                                        \
    {                                                                        \
        __attr_section("intersec", "module")                                 \
        static module_t *mod;                                                \
                                                                             \
        if (!mod) {                                                          \
            mod = module_register(LSTR(#name));                              \
        }                                                                    \
        return mod;                                                          \
    }

/** Begin the definition of a module.
 *
 * This begin a section of code that can contain the description of a module.
 * The section can currently contain the following descriptions:
 * - \ref MODULE_DEPENDS_ON to add a dependency.
 * - \ref MODULE_IMPLEMENTS to add a method.
 *
 * The macro can also take a variadic list of dependencies as strings, but
 * this usage is discouraged since it does not add compile-time dependencies
 * between compilation units. Using \ref MODULE_DEPENDS_ON should be the
 * prefered solution to add a new dependence.
 *
 * The section must be closed by calling \ref MODULE_END().
 */
#define MODULE_BEGIN(name)                                                   \
    _MODULE_ADD_DECLS(name);                                                 \
                                                                             \
    static __attribute__((constructor))                                      \
    void __##name##_module_register(void) {                                  \
        __unused__                                                           \
        module_t *__mod = module_implement(MODULE(name),                     \
                                           &name##_initialize,               \
                                           &name##_shutdown,                 \
                                           MODULE(log));                     \

/** Macro to end the definition of a module.
 *
 * \see MODULE_BEGIN
 */
#define MODULE_END()  }

/** Add a dependence on another module.
 *
 * This macro can only be used in a MODULE_BEGIN/MODULE_END block of code. It
 * declares a dependence from the current module on \p dep.
 */
#define MODULE_DEPENDS_ON(dep)  \
    module_add_dep(__mod, MODULE(dep))

/** Add a dependence to another module.
 *
 * As \ref MODULE_DEPENDS_ON this macro can only be used in a
 * MODULE_BEGIN/MODULE_END block.
 * It declares a dependence from the current module to \p need.
 */
#define MODULE_NEEDED_BY(need)  \
    module_add_dep(MODULE(need), __mod)

/* {{{ Method */

/** Declare the implementation of the method \p hook.
 *
 * This macro can only be used in a MODULE_BEGIN/MODULE_END block of code. It
 * declares that the current module implements the method \p hook with the
 * implementation being \p cb.
 *
 * This macro can only be used if the method has a void prototype.
 */
#define MODULE_IMPLEMENTS_VOID(hook, cb)                                     \
    module_implement_method_void(__mod, &hook##_method, (cb))

/** Declare the implementation of the method \p hook.
 *
 * \see MODULE_IMPLEMENTS_VOID
 *
 * This macro can only be used if the method takes a pointer as argument.
 */
#define MODULE_IMPLEMENTS_PTR(Type, hook, cb)  do {                          \
        void (*__hook_cb)(Type *ptr) = (cb);                                 \
        module_implement_method_ptr(__mod, &hook##_method,                   \
                                    (void (*)(void *))__hook_cb);            \
    } while (0)

/** Declare the implementation of the method \p hook.
 *
 * \see MODULE_IMPLEMENTS_VOID
 *
 * This macro can only be used if the method takes an integer as argument.
 */
#define MODULE_IMPLEMENTS_INT(hook, cb)                                      \
    module_implement_method_int(__mod, &hook##_method, (cb))

/** Declare the implementation of the method \p hook.
 *
 * \see MODULE_IMPLEMENTS_VOID
 *
 * This macro can only be used if the method takes a \ref data_t as argument.
 */
#define MODULE_IMPLEMENTS(hook, cb)                                          \
    module_implement_method_generic(__mod, &hook##_method, (cb))

/* }}} */
/* {{{ Low-level API */

/** Register a new module. */
__leaf module_t * nonnull module_register(lstr_t name);

/** Implement a module.
 *
 *  \param[in] module       the module to implement
 *  \param[in] initialize   pointer to the function that initialize the module
 *  \param[in] shutdown     pointer to the function that shutdown the module
 *  \param[in] dependency   module to depend on
 *
 *  \return the newly registered module in case of success.
 */
__leaf
module_t * nullable
module_implement(module_t * nonnull module,
                 int (*nonnull constructor)(void * nullable),
                 int (*nonnull destructor)(void),
                 module_t *nullable dependency);

void module_add_dep(module_t * nonnull module, module_t * nonnull dep);

__attr_nonnull__((1, 2, 3))
void module_implement_method(module_t * nonnull mod,
                             const module_method_t * nonnull method,
                             void * nonnull cb);

__attr_nonnull__((1, 2, 3))
static inline
void module_implement_method_void(module_t * nonnull mod,
                                  const module_method_t * nonnull method,
                                  void (*nonnull cb)(void))
{
    assert (method->type == METHOD_VOID);
    module_implement_method(mod, method, (void *)cb);
}

__attr_nonnull__((1, 2, 3))
static inline
void module_implement_method_int(module_t * nonnull mod,
                                 const module_method_t * nonnull method,
                                 void (*nonnull cb)(int))
{
    assert (method->type == METHOD_INT);
    module_implement_method(mod, method, (void *)cb);
}

__attr_nonnull__((1, 2, 3))
static inline
void module_implement_method_generic(module_t * nonnull mod,
                                     const module_method_t * nonnull method,
                                     void (*nonnull cb)(data_t))
{
    assert (method->type == METHOD_GENERIC);
    module_implement_method(mod, method, (void *)cb);
}

__attr_nonnull__((1, 2, 3))
static inline
void module_implement_method_ptr(module_t * nonnull mod,
                                 const module_method_t * nonnull method,
                                 void (*nonnull cb)(void * nullable))
{
    assert (method->type == METHOD_PTR);
    module_implement_method(mod, method, (void *)cb);
}

/* }}} */
/* }}} */
/* {{{ Module management */

/** Provide an argument for module constructor.
 *
 *  Use:
 *      MODULE_PROVIDE(module, (void *)arg)
 *
 *  Your initialization function (alias constructor function) should
 *  return a negative number if it needs an argument and this argument
 *  is not provided (by default NULL)
 *  Exe:
 *     int module_initialize(void *arg) {
 *         if (arg == NULL)
 *            return -1; (or assert)
 *       ...
 *     }
 */
#define MODULE_PROVIDE(name, argument)                                       \
    module_provide(MODULE(name), argument)

/** Macro for requiring a module.
 *
 *   Use:
 *     MODULE_REQUIRE(module1);
 *
 *              module1
 *             /       \
 *         module2   module3
 *
 *   Behavior:
 *      + If every thing works
 *             - module1, module2, module3 initialized
 *             - module1 will have state MANU_REQ
 *             - module2, module3 will have state AUTO_REQ
 *             - MODULE_REQUIRE will return
 *       + If module3 fail to initialize
 *             - module_require will throw a logger_fatal
 */
#define MODULE_REQUIRE(name)  module_require(MODULE(name), NULL)

/** Macro for releasing a module.
 *
 *  Use:
 *    MODULE_RELEASE(module);
 *
 *  Behavior:
 *     + If you try to RELEASE a module that has not been manually initialized
 *       the program will assert
 *       In other words only RELEASE modules that has been require with REQUIRE
 *     + For returns value see module_release and module_shutdown
 */
#define MODULE_RELEASE(name)  module_release(MODULE(name))

#define MODULE_IS_LOADED(name)  module_is_loaded(MODULE(name))
#define MODULE_IS_INITIALIZING(name)  module_is_initializing(MODULE(name))
#define MODULE_IS_SHUTTING_DOWN(name)  module_is_shutting_down(MODULE(name))

/** Retreives the name of a module.
 */
__attr_nonnull__((1))
const char * nonnull module_get_name(const module_t * nonnull mod);

/* {{{ Low-level API */

/** Require a module. (initialization)
 *
 *  Two steps  : - Require dependent modules
 *                - Initialize the module
 *
 *  If one of the required modules does not initialize, the function
 *  will throw a logger_fatal.
 *
 *  \param[in] mod          pointer to the module to initialize
 *  \param[in] required_by  module that requires \p mod to be initialized
 *                          it can be NULL if \p mod has no parent module
 */
__attr_nonnull__((1))
void module_require(module_t * nonnull mod, module_t * nullable required_by);

__attr_nonnull__((1))
void module_release(module_t * nonnull mod);

__attr_nonnull__((1))
void module_provide(module_t * nonnull mod,
                    void * nullable argument);

/** Get the argument of a module.
 *
 * Only for advanced uses.
 */
__attr_nonnull__((1))
void * nullable module_get_arg(module_t * nonnull mod);

/** true if module is loaded. (AUTO_REQ || MANU_REQ) */
__attr_nonnull__((1))
bool module_is_loaded(const module_t * nonnull mod);

/** true if module is currently loading. */
__attr_nonnull__((1))
bool module_is_initializing(const module_t * nonnull mod);

/** true if module is currently shutting down. */
__attr_nonnull__((1))
bool module_is_shutting_down(const module_t * nonnull mod);

/** Fetch the module hierarchy.
 *
 * \param[out] modules      This buffer will contains all the registered
 *                          modules formated like a csv file.
 *                               * nodes;loaded
 *                          nodes represents the name of a module
 *                          loaded is a boolean (1 for loaded, 0 for unloaded)
 * \param[out] dependencies This buffer will contains all the dependencies
 *                          between the registered modules formated like a csv
 *                          file.
 *                               * nodes;dest
 *                          nodes represents the name of a module
 *                          dest  represents the name of the one of the
 *                          dependencies of nodes.
 */
void module_debug_dump_hierarchy(sb_t * nonnull modules,
                                 sb_t * nonnull dependencies);

/** Destroy all modules.
 *
 * XXX This is needed when calling C code from Java code using JNI. If not,
 * the Java code is crashing at shutdown when the C module destructor is
 * called. This function should not be called otherwise.
 */
void module_destroy_all(void);

/* }}} */
/* }}} */
/* {{{ on_term method */

MODULE_METHOD_DECLARE(INT, DEPS_BEFORE, on_term);

/** On term all modules.
 *
 *  Call the module_on_term if it exists of all modules
 */
void module_on_term(int signo);

/* }}} */
/* {{{ atfork methods */

/* XXX: Do not use t_scope with these methods because the t_pool_g is not
 *      necessarily initialized when the methods are called. */

/** Method called in the parent, just before the fork. */
MODULE_METHOD_DECLARE(VOID, DEPS_AFTER, at_fork_prepare);

/** Method called in the parent, just after the fork.
 *
 * It takes the PID of the child as argument, or -1 if it is not known.
 */
MODULE_METHOD_DECLARE(INT, DEPS_BEFORE, at_fork_on_parent);

/** Method called in the child, just after the fork. */
MODULE_METHOD_DECLARE(VOID, DEPS_BEFORE, at_fork_on_child);

/** Method called in the parent, just after the child terminates.
 *
 * It takes the PID of the child as argument.
 * It is called only for the child processes that are tracked by the el
 * library, either by using ifork() or el_child_register().
 */
MODULE_METHOD_DECLARE(INT, DEPS_BEFORE, at_fork_on_child_terminated);

MODULE_METHOD_DECLARE(VOID, DEPS_BEFORE, consume_child_events);

/** Register at fork methods.
 *
 * This is needed so that fork methods defined above are called also when not
 * using ifork(), which happens when we do not control the fork (like in
 * python modules).
 *
 * Automatically called for non shared libraries.
 * When compiling for non shared libraries this function has constructor
 * attribute so is automatically called before main() function of the program.
 *
 * For shared libraries, at fork methods are not enabled by default because
 * when the dso is unloaded, at fork functions point to invalid addresses that
 * trigger a segfault when forking after unload.
 *
 * So this function should be called only by shared libraries that need to
 * have at fork methods and that are not unloaded during execution of the
 * program (python modules for example) or the program shouldn't fork after
 * unloading the library.
 */
void module_register_at_fork(void);

/* }}} */
/* {{{ Dependency collision */

/** Checks if the given modules are independent from each other.
 *
 * \param[in]   tab        input modules list.
 * \param[in]   len        length of the modules list.
 * \param[out]  collision  name of the first module in the list on which
 *                         another module from the list depends (if any).
 *
 * \return -1 in case of collision.
 */
__must_check__
int module_check_no_dependencies(module_t * nonnull tab[], int len,
                                 lstr_t * nonnull collision);

/* }}} */

/* {{{ Module declarations */

/* Here are the declaration of the core modules that can't be declared in
 * their own header because of a circular dependency. */

/** Log module.
 *
 * All modules declared with \p MODULE_BEGIN automatically depend on it.
 */
MODULE_DECLARE(log);

/** Core memory module.
 *
 * Require it to have the memory pools status when receiving a PWR signal.
 */
MODULE_DECLARE(core_mem);

/* {{{ private */

MODULE_DECLARE(core_mem_fifo);
MODULE_DECLARE(core_mem_ring);
MODULE_DECLARE(core_mem_stack);

/* }}} */
/* }}} */

#endif
