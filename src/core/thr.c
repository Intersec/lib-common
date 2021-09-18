/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#include <dlfcn.h>
#include <pthread.h>
#include <lib-common/thr.h>

struct thr_hooks thr_hooks_g = {
    .init_cbs = DLIST_INIT(thr_hooks_g.init_cbs),
    .exit_cbs = DLIST_INIT(thr_hooks_g.exit_cbs),
};

/*
 * This code gets pulled automatically as soon as pthread_create() is used
 * because we make our symbol strong and visible, and it then overrides the
 * libc one.
 *
 * When we build a shared library, we export our own pthread_create and it
 * also overrides the libc one, meaning that the library can use all intersec
 * APIs and be pthread compatible.
 */

static struct {
    pthread_once_t key_once;
    pthread_key_t  key;
} core_thread_g = {
#define _G  core_thread_g
    .key_once = PTHREAD_ONCE_INIT,
};

void thr_detach(void)
{
    pthread_setspecific(_G.key, NULL);
    dlist_for_each(it, &thr_hooks_g.exit_cbs) {
        (container_of(it, struct thr_ctor, link)->cb)();
    }
}

static void thr_hooks_at_exit(void *unused)
{
    thr_detach();
}

static void thr_hooks_atfork_in_child(void)
{
    _G.key_once = (pthread_once_t)PTHREAD_ONCE_INIT;
}

static void thr_hooks_key_setup(void)
{
    pthread_key_create(&_G.key, thr_hooks_at_exit);
}

void thr_attach(void)
{
    pthread_once(&_G.key_once, thr_hooks_key_setup);
    if (pthread_getspecific(_G.key) == NULL) {
        pthread_setspecific(_G.key, MAP_FAILED);

        dlist_for_each(it, &thr_hooks_g.init_cbs) {
            (container_of(it, struct thr_ctor, link)->cb)();
        }
    }
}

static void *thr_hooks_wrapper(void *data)
{
    void *(*fn)(void *) = ((void **)data)[0];
    void   *arg         = ((void **)data)[1];
    void   *ret;

    p_delete(&data);
    thr_attach();
    ret = fn(arg);
    thr_detach();
    return ret;
}

int thr_create(pthread_t *restrict thread,
               const pthread_attr_t *restrict attr,
               void *(*fn)(void *), void *restrict arg)
{
    static typeof(pthread_create) *real_pthread_create;
    void **pair = p_new(void *, 2);
    int res;

#if !defined(__has_asan) && !defined(__has_tsan)
    if (unlikely(!real_pthread_create))
        real_pthread_create = dlsym(RTLD_NEXT, "pthread_create");
#else
    if (unlikely(!real_pthread_create))
        real_pthread_create = dlsym(RTLD_DEFAULT, "pthread_create");
#endif
    pair[0] = fn;
    pair[1] = arg;
    res = (*real_pthread_create)(thread, attr, &thr_hooks_wrapper, pair);
    if (res != 0) {
        errno = res;
        p_delete(&pair);
    }
    return res;
}

#if !defined(__has_asan) && !defined(__has_tsan)
__attribute__((visibility("default")))
int pthread_create(pthread_t *restrict thread,
                   const pthread_attr_t *restrict attr,
                   void *(*fn)(void *), void *restrict arg)
{
    return thr_create(thread, attr, fn, arg);
}
#endif

void pthread_force_use(void)
{
}

static int thr_hooks_initialize(void *arg)
{
    return 0;
}

static int thr_hooks_shutdown(void)
{
    dlist_for_each(it, &thr_hooks_g.exit_cbs) {
        (container_of(it, struct thr_ctor, link)->cb)();
    }
    return 0;
}

module_t *thr_hooks_module_g;

_MODULE_ADD_DECLS(thr_hooks);

void thr_hooks_register(void)
{
    if (!thr_hooks_module_g) {
        thr_hooks_module_g = module_implement(MODULE(thr_hooks),
                                              &thr_hooks_initialize,
                                              &thr_hooks_shutdown, NULL);
        module_implement_method(MODULE(thr_hooks), &at_fork_on_child_method,
                                &thr_hooks_atfork_in_child);
    }
}
