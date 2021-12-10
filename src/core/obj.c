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

#include <lib-common/core.h>

/** \addtogroup lc_obj
 * \{
 */

/** \file lib-common/core-obj.c
 * \brief Objects and Virtual Tables in C (module)
 */

bool cls_inherits(const void *_cls, const void *vptr)
{
    const object_class_t *cls = _cls;
    while (cls) {
        if (cls == vptr)
            return true;
        cls = cls->super;
    }
    return false;
}

static void obj_init_real_aux(object_t *o, const object_class_t *cls)
{
    object_t *(*init)(object_t *) = cls->init;

    if (!init)
        return;

    while (cls->super && init == cls->super->init) {
        cls = cls->super;
    }
    if (cls->super)
        obj_init_real_aux(o, cls->super);
    (*init)(o);
}

void *obj_init_real(const void *_cls, void *_o, mem_pool_t *mp)
{
    const object_class_t *cls = _cls;
    object_t *o = _o;

    o->mp     = mp;
    o->refcnt = 1;
    o->v.ptr  = cls;
    obj_init_real_aux(o, cls);
    return o;
}

void obj_wipe_real(object_t *o)
{
    const object_class_t *cls = o->v.ptr;
    void (*wipe)(object_t *);

    /* a crash here means obj_wipe was called on a reachable object.
     * It's likely the caller should have used obj_release() instead.
     */
    assert (o->refcnt == 1);

    while ((wipe = cls->wipe)) {
        (*wipe)(o);
        do {
            cls = cls->super;
            if (!cls)
                return;
        } while (wipe == cls->wipe);
    }
    o->refcnt = 0;
}

static object_t *obj_retain_(object_t *obj)
{
    assert (obj->mp != &mem_pool_static);

    if (likely(obj->refcnt > 0)) {
        if (likely(++obj->refcnt > 0))
            return obj;
        e_panic("too many refcounts");
    }

    switch (obj->refcnt) {
      case 0:
        e_panic("probably acting on a deleted object");
      default:
        /* WTF?! probably a memory corruption */
        e_panic("should not happen");
    }
}

static void obj_release_(object_t *obj, bool *nullable destroyed)
{
    assert (obj->mp != &mem_pool_static);

    if (obj->refcnt > 1) {
        --obj->refcnt;
    } else if (obj->refcnt == 1) {
        if (obj_vmethod(obj, can_wipe) && !obj_vcall(obj, can_wipe)) {
            /* FIXME This 'can_wipe' method smells bad. Remove it. */
        } else {
            obj_wipe_real(obj);
            mp_delete(obj->mp, &obj);
        }
    } else {
        switch (obj->refcnt) {
          case 0:
            e_panic("object refcounting issue");
          default:
            /* Probably a memory corruption we should have hit 0 first. */
            e_panic("should not happen");
        }
    }

    if (destroyed) {
        *destroyed = !obj;
    }
}

const object_class_t *object_class(void)
{
    static object_class_t const klass = {
        .type_size = sizeof(object_t),
        .type_name = "object",

        .retain    = obj_retain_,
        .release   = obj_release_,
    };
    return &klass;
}

/**\}*/
