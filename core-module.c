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

#include <pthread.h>

#include "log.h"
#include "el.h"
#include "container-qvector.h"
#include "container-qhash.h"
#include "unix.h"

/* {{{ Type definition */
/* {{{ methods */

qm_khptr_ckey_t(methods, module_method_t, void *);
qvector_t(methods_cb, void *);

typedef struct module_method_impl_t {
    const module_method_t *params;
    qv_t(methods_cb) callbacks;
} module_method_impl_t;

GENERIC_NEW_INIT(module_method_impl_t, module_method);
static module_method_impl_t *module_method_wipe(module_method_impl_t *method)
{
    qv_wipe(&method->callbacks);
    return method;
}
GENERIC_DELETE(module_method_impl_t, module_method);

qm_khptr_ckey_t(methods_impl, module_method_t, module_method_impl_t *);
static void module_method_register_all_cb(void);

/* }}} */
/* {{{ modules */

typedef enum module_state_t {
    REGISTERED   = 0,

    AUTO_REQ     = 1 << 0, /* Initialized automatically */
    MANU_REQ     = 1 << 1, /* Initialized manually */

    INITIALIZING = 1 << 2, /* In initialization */
    SHUTTING     = 1 << 3, /* Shutting down */
    FAIL_SHUT    = 1 << 4, /* Fail to shutdown */
} module_state_t;

qvector_t(module, module_t *);

struct module_t {
    lstr_t name;
    module_state_t state;
    int manu_req_count;

    qv_t(module) dependent_of;
    qv_t(module) required_by;
    qm_t(methods) methods;

    int (*constructor)(void *);
    int (*destructor)(void);
    void *constructor_argument;
};

static module_t *module_init(module_t *module)
{
    p_clear(module, 1);
    qm_init(methods, &module->methods);
    return module;
}
GENERIC_NEW(module_t, module);
static void module_wipe(module_t *module)
{
    lstr_wipe(&(module->name));
    qv_wipe(&module->dependent_of);
    qv_wipe(&module->required_by);
    qm_wipe(methods, &module->methods);
}
GENERIC_DELETE(module_t, module);


qm_kptr_t(module, lstr_t, module_t *, qhash_lstr_hash, qhash_lstr_equal);
qh_khptr_t(module, module_t);

/* }}} */
/* }}} */

static struct module_g {
    logger_t logger;
    qm_t(module)     modules;

    qm_t(methods_impl) methods;

    /* Keep track if we are currently initializing a module */
    int in_initialization;

    bool is_shutdown   : 1;
    bool methods_dirty : 1;
} module_g = {
#define _G module_g
    .logger = LOGGER_INIT(NULL, "module", LOG_INHERITS),
    .modules     = QM_INIT(module, _G.modules),
    .methods = QM_INIT(methods_impl, _G.methods),
};

/* {{{ Module Registry */

static void
set_require_type(module_t *module, module_t *required_by)
{
    if (!required_by) {
        module->state = MANU_REQ;
        module->manu_req_count++;
    } else {
        qv_append(&module->required_by, required_by);
        if (module->state != MANU_REQ)
            module->state = AUTO_REQ;
    }
}

module_t *module_register(lstr_t name)
{
    module_t *module;
    int pos;

    pos = qm_reserve(module, &_G.modules, &name, 0);
    if (!expect(!(pos & QHASH_COLLISION))) {
        logger_error(&_G.logger, "%*pM has already been registered",
                     LSTR_FMT_ARG(name));
        return _G.modules.values[pos & ~QHASH_COLLISION];
    }

    module = module_new();
    module->state = REGISTERED;
    module->name = lstr_dup(name);

    _G.modules.keys[pos] = &module->name;
    _G.modules.values[pos] = module;

    return module;
}

module_t *module_implement(module_t *module,
                           int (*constructor)(void *),
                           int (*destructor)(void),
                           module_t *dependency)
{
    assert (!module->constructor);
    module->constructor = constructor;
    assert (!module->destructor);
    module->destructor = destructor;

    if (dependency) {
        qv_push(&module->dependent_of, dependency);
    }

    return module;
}

void module_add_dep(module_t *module, module_t *dep)
{
    assert (module->state == REGISTERED);
    qv_append(&module->dependent_of, dep);
}

static int modules_topo_visit(qh_t(module) *temporary_mark,
                              qh_t(module) *permanent_mark,
                              qv_t(module) *ordered_modules,
                              module_t *m, sb_t *err)
{
    if (qh_find(module, permanent_mark, m) >= 0) {
        return 0;
    }
    if (qh_add(module, temporary_mark, m) < 0) {
        sb_addf(err, "-> %*pM", LSTR_FMT_ARG(m->name));
        return -1;
    }
    tab_for_each_entry(dep, &m->dependent_of) {
        if (modules_topo_visit(temporary_mark, permanent_mark,
                               ordered_modules, dep, err) < 0)
        {
            sb_prependf(err, "-> %*pM", LSTR_FMT_ARG(m->name));
            return -1;
        }
    }
    qh_add(module, permanent_mark, m);
    qv_append(ordered_modules, m);
    return 0;
}

static int
modules_topo_sort_rev(qm_t(module) *modules, qv_t(module) *sorted, sb_t *err)
{
    int ret = 0;
    qh_t(module) temporary_mark;
    qh_t(module) permanent_mark;

    qh_init(module, &temporary_mark);
    qh_init(module, &permanent_mark);

    qm_for_each_pos(module, pos, &_G.modules) {
        module_t *m = _G.modules.values[pos];

        if (qh_find(module, &temporary_mark, m) < 0
        &&  qh_find(module, &permanent_mark, m) < 0)
        {
            if (modules_topo_visit(&temporary_mark, &permanent_mark, sorted,
                                   m, err) < 0)
            {
                sb_prepends(err, "module dependency error: ");
                ret = -1;
                goto end;
            }
        }
    }

  end:
    qh_wipe(module, &temporary_mark);
    qh_wipe(module, &permanent_mark);
    return ret;
}

void module_require(module_t *module, module_t *required_by)
{
    if (module->state == INITIALIZING) {
        logger_fatal(&_G.logger,
                     "`%*pM` has been recursively required %s%*pM",
                     LSTR_FMT_ARG(module->name), required_by ? "by " : "",
                     LSTR_FMT_ARG(required_by ? required_by->name
                                              : LSTR_NULL_V));
    }
    if (module->state == SHUTTING) {
        logger_fatal(&_G.logger,
                     "`%*pM` has been required %s%*pM while shutting down",
                     LSTR_FMT_ARG(module->name), required_by ? "by " : "",
                     LSTR_FMT_ARG(required_by ? required_by->name
                                              : LSTR_NULL_V));
    }

    _G.in_initialization++;

    if (!module_is_loaded(module)) {
        logger_trace(&_G.logger, 1, "`%*pM` has been required %s%*pM",
                     LSTR_FMT_ARG(module->name), required_by ? "by " : "",
                     LSTR_FMT_ARG(required_by ? required_by->name
                                              : LSTR_NULL_V));
    }

    if (module->state == AUTO_REQ || module->state == MANU_REQ) {
        set_require_type(module, required_by);
        _G.in_initialization--;
        return;
    }

    module->state = INITIALIZING;
    logger_trace(&_G.logger, 1, "requiring `%*pM` dependencies",
                 LSTR_FMT_ARG(module->name));

    _G.methods_dirty = true;

    tab_for_each_entry(dep, &module->dependent_of) {
        module_require(dep, module);
    }

    logger_trace(&_G.logger, 1, "calling `%*pM` constructor",
                 LSTR_FMT_ARG(module->name));

    if ((*module->constructor)(module->constructor_argument) < 0) {
        logger_fatal(&_G.logger, "unable to initialize %*pM",
                     LSTR_FMT_ARG(module->name));
    }

    set_require_type(module, required_by);
    _G.methods_dirty = true;
    _G.in_initialization--;
}

void module_provide(module_t *module, void *argument)
{
    if (module->constructor_argument) {
        logger_warning(&_G.logger, "argument for module '%*pM' has already "
                       "been provided", LSTR_FMT_ARG(module->name));
    }
    module->constructor_argument = argument;
}

void * nullable module_get_arg(module_t * nonnull mod)
{
    return mod->constructor_argument;
}

__attr_nonnull__((1))
static int module_shutdown(module_t *module);

static int notify_shutdown(module_t *module, module_t *dependence)
{
    logger_trace(&_G.logger, 2, "module '%*pM' notify shutdown to '%*pM'"
                 ": %d pending dependencies", LSTR_FMT_ARG(dependence->name),
                 LSTR_FMT_ARG(module->name), module->required_by.len);

    tab_for_each_pos(pos, &module->required_by) {
        if (module->required_by.tab[pos] == dependence) {
            qv_remove(&module->required_by, pos);
            break;
        }
    }
    if (module->required_by.len == 0 && module->state != MANU_REQ) {
        return module_shutdown(module);
    }

    return 0;
}

/** \brief Shutdown a module
 *
 *  Two steps   :   - Shutdown the module.
 *                  - Notify dependent modules that it has been shutdown
 *                    if the dependent modules don't have any other parent
 *                    modules and they have been automatically initialize
 *                    they will shutdown
 *
 *  If the module is not able to shutdown (destructor returns a negative
 *  number), module state change to FAIL_SHUT but we considered as shutdown
 *  and notify dependent modules.
 *
 *
 *  @param mod The module to shutdown
 */
__attr_nonnull__((1))
static int module_shutdown(module_t *module)
{
    int shut_dependent = 1;
    int shut_self;

    assert (module->state == MANU_REQ || module->state == AUTO_REQ);

    /* shutdown must be symmetric to require */
    module->manu_req_count = 0;

    module->state = SHUTTING;
    logger_trace(&_G.logger, 1, "shutting down `%*pM`",
                 LSTR_FMT_ARG(module->name));

    if ((shut_self = (*module->destructor)()) < 0) {
        logger_warning(&_G.logger, "unable to shutdown   %*pM",
                       LSTR_FMT_ARG(module->name));
        module->state = FAIL_SHUT;
    }

    _G.methods_dirty = true;

    tab_for_each_entry(dep, &module->dependent_of) {
        int shut;

        shut = notify_shutdown(dep, module);
        if (shut < 0) {
            shut_dependent = shut;
        }
    }

    if (shut_self >= 0) {
        module->state = REGISTERED;
    }

    RETHROW(shut_dependent);
    RETHROW(shut_self);
    return 0;
}

void module_release(module_t *module)
{
    if (module->manu_req_count == 0) {
        /* You are trying to manually release a module that have been spawn
         * automatically (AUTO_REQ)
         */
        logger_panic(&_G.logger, "unauthorized release for module '%*pM'",
                     LSTR_FMT_ARG(module->name));
        return;
    }

    if (module->state == MANU_REQ && module->manu_req_count > 1) {
        module->manu_req_count--;
        return;
    }

    if (module->state == MANU_REQ && module->manu_req_count == 1) {
        if (module->required_by.len > 0) {
            module->manu_req_count = 0;
            module->state = AUTO_REQ;
            return;
        }
    }

    module_shutdown(module);
}


bool module_is_loaded(const module_t *module)
{
    return module->state == AUTO_REQ || module->state == MANU_REQ;
}

bool module_is_initializing(const module_t *module)
{
    return module->state == INITIALIZING;
}

bool module_is_shutting_down(const module_t *module)
{
    return module->state == SHUTTING;
}

const char *module_get_name(const module_t *module)
{
    return module->name.s;
}

static void module_hard_shutdown(void)
{
    /* Shutdown manually required modules that were not released. */
    qm_for_each_pos(module, pos, &_G.modules) {
        module_t *module = _G.modules.values[pos];

        if (module->state == MANU_REQ) {
            bool warned = false;

            while (module->manu_req_count && !(module->state & FAIL_SHUT)) {
                if (!warned) {
                    logger_trace(&_G.logger, 1,
                                 "%*pM was not released, forcing release",
                                 LSTR_FMT_ARG(module->name));
                    warned = true;
                }
                module_release(module);
            }
        }
    }

    /* All modules should be shutdown now. */
    qm_for_each_pos(module, pos, &_G.modules) {
        module_t *module = _G.modules.values[pos];

        assert (module->state == REGISTERED || module->state & FAIL_SHUT);
    }
}

extern bool syslog_is_critical;

__attribute__((destructor))
static void _module_shutdown(void)
{
    if (_G.is_shutdown) {
        return;
    }

    if (!syslog_is_critical) {
        module_hard_shutdown();
    }
    qm_deep_wipe(methods_impl, &_G.methods, IGNORE, module_method_delete);
    qm_deep_wipe(module, &_G.modules, IGNORE, module_delete);
    logger_wipe(&_G.logger);
    _G.is_shutdown = true;
}

void module_destroy_all(void)
{
    _module_shutdown();
}

/* }}} */
/* {{{ Methods */

void module_implement_method(module_t *module, const module_method_t *method,
                             void *cb)
{
    int pos;

    pos = qm_reserve(methods_impl, &_G.methods, method, 0);
    if (!(pos & QHASH_COLLISION)) {
        module_method_impl_t *m;

        m = module_method_new();

        m->params = method;
        _G.methods.values[pos] = m;
    }
    qm_add(methods, &module->methods, method, cb);
}

void module_run_method(const module_method_t *method, data_t arg)
{
    module_method_impl_t *m;

    if (unlikely(_G.methods_dirty)) {
        module_method_register_all_cb();
    }

    m = qm_get_def(methods_impl, &_G.methods, method, NULL);
    if (!m) {
        /* method not implemented */
        return;
    }

    switch (method->type) {
      case METHOD_VOID:
        tab_for_each_entry(cb, &m->callbacks) {
            ((void (*)(void))cb)();
        }
        break;

      case METHOD_INT:
        tab_for_each_entry(cb, &m->callbacks) {
            ((void (*)(int))cb)(arg.u32);
        }
        break;

      case METHOD_PTR:
      case METHOD_GENERIC:
        tab_for_each_entry(cb, &m->callbacks) {
            ((void (*)(data_t))cb)(arg);
        }
        break;
    }
}

static void module_add_method(module_t *module, module_method_impl_t *method)
{
    void *cb = qm_get_def(methods, &module->methods, method->params, NULL);

    if (cb) {
        qv_append(&method->callbacks, cb);
    }
}

static void module_method_register_cb(module_method_impl_t *method,
                                      qv_t(module) *modules)
{

    if (method->params->order == MODULE_DEPS_BEFORE) {
        tab_for_each_pos(pos, modules) {
            module_t *m = modules->tab[pos];

            if (module_is_loaded(m)) {
                module_add_method(m, method);
            }
        }
    } else
    if (method->params->order == MODULE_DEPS_AFTER) {
        tab_for_each_pos_rev(pos, modules) {
            module_t *m = modules->tab[pos];

            if (module_is_loaded(m)) {
                module_add_method(m, method);
            }
        }
    }
}

static void module_method_register_all_cb(void)
{
    /* XXX: Do not use t_scope here. This function can be called by
     *      pthread_fork hooks and the t_pool_g is not necessarily
     *      initialized here. */
    qv_t(module) sorted_modules;
    SB_1k(err);

    qv_init(&sorted_modules);
    qv_grow(&sorted_modules, qm_len(module, &_G.modules));
    if (modules_topo_sort_rev(&_G.modules, &sorted_modules, &err) < 0) {
        e_fatal("%*pM", SB_FMT_ARG(&err));
    }

    qm_for_each_pos(methods_impl, pos, &_G.methods) {
        module_method_impl_t *m = _G.methods.values[pos];

        qv_clear(&m->callbacks);
        module_method_register_cb(m, &sorted_modules);
    }
    qv_wipe(&sorted_modules);
    _G.methods_dirty = false;
}

MODULE_METHOD(INT, DEPS_BEFORE, on_term);

void module_on_term(int signo)
{
    module_run_method(&on_term_method, (el_data_t){ .u32 = signo });
}

MODULE_METHOD(VOID, DEPS_AFTER, at_fork_prepare);
MODULE_METHOD(INT, DEPS_BEFORE, at_fork_on_parent);
MODULE_METHOD(VOID, DEPS_BEFORE, at_fork_on_child);
MODULE_METHOD(INT, DEPS_BEFORE, at_fork_on_child_terminated);
MODULE_METHOD(VOID, DEPS_BEFORE, consume_child_events);

static void module_at_fork_prepare(void)
{
    /* XXX: don't call method when coming from ifork() because it already
     *      calls it. */
    if (!ifork_in_progress()) {
        MODULE_METHOD_RUN_VOID(at_fork_prepare);
    }
}

static void module_at_fork_on_parent(void)
{
    /* XXX: don't call method when coming from ifork() because it already
     *      calls it (with a good child pid). */
    if (!ifork_in_progress()) {
        MODULE_METHOD_RUN_INT(at_fork_on_parent, -1);
    }
}

static void module_at_fork_on_child(void)
{
    /* XXX: don't call method when coming from ifork() because it already
     *      calls it. */
    if (!ifork_in_progress()) {
        MODULE_METHOD_RUN_VOID(at_fork_on_child);
    }
}

#ifndef SHARED
__attribute__((constructor))
#endif
void module_register_at_fork(void)
{
    static bool at_fork_registered = false;

    if (!at_fork_registered) {
        void *data = NULL;

        /* XXX: ensures internal ressources of the libc used to implement
         * posix_memalign() are ready here. It looks like the glibc of some
         * CentOs use an atfork() handler that get registered the first time
         * you perform an aligned allocation.
         */
        IGNORE(posix_memalign(&data, 64, 1024));
        free(data);

        pthread_atfork(module_at_fork_prepare,
                       module_at_fork_on_parent,
                       module_at_fork_on_child);
        at_fork_registered = true;
    }
}

/* }}} */
/* {{{ Dependency collision */

/** Adds to qh all the modules that are dependent of the module m.
 */
static void add_dependencies_to_qh(module_t *m, qh_t(module) *qh)
{
    tab_for_each_entry(dep, &m->dependent_of) {
        if (qh_add(module, qh, dep) >= 0) {
            add_dependencies_to_qh(dep, qh);
        }
    }
}

int module_check_no_dependencies(module_t *tab[], int len,
                                 lstr_t *collision)
{
    t_scope;
    qh_t(module) dependencies;

    t_qh_init(module, &dependencies, len);
    for (int pos = 0; pos < len; pos++) {
        add_dependencies_to_qh(tab[pos], &dependencies);
    }
    for (int pos = 0; pos < len; pos++) {
        if (qh_find(module, &dependencies, tab[pos]) >= 0) {
            *collision = lstr_dupc(tab[pos]->name);
            return -1;
        }
    }
    return 0;
}

/* }}} */

/* {{{ Debug */

void module_debug_dump_hierarchy(sb_t *modules, sb_t *dependencies)
{
    sb_sets(modules, "nodes;loaded\n");
    sb_sets(dependencies, "nodes;dest\n");
    qm_for_each_pos(module, pos, &_G.modules) {
        module_t *module = _G.modules.values[pos];

        sb_addf(modules, "%*pM;%d\n", LSTR_FMT_ARG(module->name),
                module_is_loaded(module) ? 1 : 0);
        tab_for_each_entry(dep, &module->dependent_of) {
            sb_addf(dependencies, "%*pM;%*pM\n", LSTR_FMT_ARG(module->name),
                    LSTR_FMT_ARG(dep->name));
        }
    }
}

/* }}} */
