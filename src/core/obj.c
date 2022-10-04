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

#include <lib-common/core.h>
#include <lib-common/log.h>
#include <lib-common/container.h>
#include <lib-common/str-buf-pp.h>

/** \addtogroup lc_obj
 * \{
 */

/** \file lib-common/core-obj.c
 * \brief Objects and Virtual Tables in C (module)
 */

static struct {
    logger_t logger;
} core_obj_g = {
#define _G core_obj_g
    .logger = LOGGER_INIT_INHERITS(NULL, "core-obj"),
};

void (object_panic)(const char *nonnull file, const char *nonnull func,
                    int line, const char *nonnull fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    __logger_vpanic(&_G.logger, file, func, line, fmt, va);
}

/* {{{ Tagged references. */

#ifndef NDEBUG

typedef struct obj_tagged_ref_t {
    /* Absent for retain scope refs. */
    const char *nullable tag;

    /* Line of the retain. */
    const char *nonnull func;
    const char *nonnull file;
    int line;

    int refcnt;
} obj_tagged_ref_t;

qvector_t(obj_tagged_ref, obj_tagged_ref_t);

struct obj_tagged_ref_list_t {
    qv_t(obj_tagged_ref) refs;
};

GENERIC_NEW_INIT(obj_tagged_ref_list_t, obj_tagged_ref_list);

static void obj_tagged_ref_list_wipe(obj_tagged_ref_list_t *list)
{
    qv_wipe(&list->refs);
}

GENERIC_DELETE(obj_tagged_ref_list_t, obj_tagged_ref_list);

void (obj_print_references)(const object_t *nonnull obj)
{
    int64_t tagged_refcnt = 0;
    logger_notice_scope(&_G.logger);

    if (obj->obj_tagged_refs_) {
        tab_for_each_ptr(ref, &obj->obj_tagged_refs_->refs) {
            tagged_refcnt += ref->refcnt;
        }
    }
    logger_cont("object @%p, refcnt=%'zd, %'jd tagged reference(s)",
                obj, obj->refcnt, tagged_refcnt);
    if (obj->obj_tagged_refs_) {
        t_scope;
        SB_1k(table_buf);
        qv_t(table_hdr) hdr;
        qv_t(table_data) data;
        table_hdr_t hdr_data[] = { {
            /* For indentation. */
            .title = LSTR_IMMED("  "),
        }, {
            .title = LSTR_IMMED("TAG"),
        }, {
            .title = LSTR_IMMED("FUNCTION"),
        }, {
            .title = LSTR_IMMED("FILE:LINE"),
        }, {
            .title = LSTR_IMMED("REFCNT"),
        } };

        logger_cont(":\n");

        qv_init_static(&hdr, hdr_data, countof(hdr_data));
        t_qv_init(&data, obj->obj_tagged_refs_->refs.len);

        tab_for_each_ptr(ref, &obj->obj_tagged_refs_->refs) {
            qv_t(lstr) *row;

            row = t_qv_init(qv_growlen(&data, 1), 4);
            qv_append(row, LSTR_EMPTY_V);
            if (ref->tag) {
                qv_append(row, LSTR(ref->tag));
            } else {
                qv_append(row, LSTR("<obj_retain_scope>"));
            }
            qv_append(row, LSTR(ref->func));
            qv_append(row, t_lstr_fmt("%s:%d", ref->file, ref->line));
            qv_append(row, t_lstr_fmt("%'d", ref->refcnt));
        }
        sb_add_table(&table_buf, &hdr, &data);
        /* Remove the trailing '\n'. */
        sb_shrink(&table_buf, 1);
        logger_cont("%*pM", SB_FMT_ARG(&table_buf));
    }
}

static obj_tagged_ref_t *obj_find_tagged_ref(object_t *obj, const char *tag)
{
    THROW_NULL_IF(!obj->obj_tagged_refs_);

    tab_for_each_ptr(ref, &obj->obj_tagged_refs_->refs) {
        if (ref->tag && strequal(ref->tag, tag)) {
            return ref;
        }
    }

    return NULL;
}

static obj_tagged_ref_t *
obj_find_scope_ref(object_t *obj, const char *file, int line)
{
    THROW_NULL_IF(!obj->obj_tagged_refs_);

    tab_for_each_ptr(ref, &obj->obj_tagged_refs_->refs) {
        if (strequal(ref->file, file) && ref->line == line && !ref->tag) {
            return ref;
        }
    }

    return NULL;
}

static obj_tagged_ref_t *
obj_add_tagged_ref(object_t *obj, const char *nullable tag,
                   const char *nonnull func,
                   const char *nonnull file, int line)
{
    obj_tagged_ref_t *ref;

    if (!obj->obj_tagged_refs_) {
        obj->obj_tagged_refs_ = obj_tagged_ref_list_new();
    }

    ref = qv_growlen0(&obj->obj_tagged_refs_->refs, 1);
    ref->tag = tag;
    ref->func = func;
    ref->file = file;
    ref->line = line;

    return ref;
}

object_t *nonnull
(obj_tagged_retain)(object_t *nonnull obj, const char *nonnull tag,
                    const char *nonnull func,
                    const char *nonnull file, int line)
{
    obj_tagged_ref_t *ref;

    ref = obj_find_tagged_ref(obj, tag);
    if (ref) {
        if (!strequal(ref->file, file) || ref->line != line) {
            obj_print_references(obj);
            logger_panic(&_G.logger, "reference tagging collision : "
                         "the tag `%s` is used for two different retains, "
                         "in %s (%s:%d) and in %s (%s:%d)", tag,
                         ref->func, ref->file, ref->line,
                         func, file, line);
        }
    } else {
        ref = obj_add_tagged_ref(obj, tag, func, file, line);
    }
    ref->refcnt++;

    return obj_vcall(obj, retain);
}

static void
obj_release_vcall(object_t *nonnull *nonnull obj_p)
{
    bool destroyed;

    obj_vcall(*obj_p, release, &destroyed);
    if (destroyed) {
        *obj_p = NULL;
    }
}

void (obj_tagged_release)(object_t *nonnull *nonnull obj_p,
                          const char *nonnull tag)
{
    obj_tagged_ref_t *reference;
    object_t *obj = *obj_p;

    reference = obj_find_tagged_ref(obj, tag);
    if (unlikely(!reference)) {
        obj_print_references(obj);
        logger_panic(&_G.logger,
                     "broken tagged release: cannot find reference for tag "
                     "`%s`", tag);
    }
    if (unlikely(!reference->refcnt)) {
        obj_print_references(obj);
        logger_panic(&_G.logger,
                     "broken tagged release: the last reference for tag `%s` "
                     "has already been released", tag);
    }
    reference->refcnt--;

    obj_release_vcall(obj_p);
}

object_t *nonnull
(obj_retain_scope)(object_t *nonnull obj,
                   const char *nonnull func,
                   const char *nonnull file, int line)
{
    obj_tagged_ref_t *ref;

    ref = obj_find_scope_ref(obj, func, line);
    if (!ref) {
        ref = obj_add_tagged_ref(obj, NULL, func, file, line);
    }
    ref->refcnt++;

    return obj_vcall(obj, retain);
}

void (obj_release_scope)(object_t *nonnull *nonnull obj_p,
                         const char *nonnull file, int line)
{
    object_t *obj = *obj_p;
    obj_tagged_ref_t *reference;

    reference = obj_find_scope_ref(obj, file, line);
    reference->refcnt--;

    obj_release_vcall(obj_p);
}

static void obj_check_tagged_refs_before_wipe(const object_t *obj)
{
    if (!obj->obj_tagged_refs_) {
        return;
    }
    tab_for_each_ptr(ref, &obj->obj_tagged_refs_->refs) {
        if (!ref->refcnt) {
            continue;
        }
        obj_print_references(obj);
        logger_panic(&_G.logger,
                     "a reference created in %s (%s:%d) with tag "
                     "`%s` wasn't released with obj_tagged_release()",
                     ref->func, ref->file, ref->line, ref->tag);
    }
}

#endif /* NDEBUG */

/* }}} */

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

#ifndef NDEBUG
    if (o->obj_tagged_refs_) {
        obj_check_tagged_refs_before_wipe(o);
        obj_tagged_ref_list_delete(&o->obj_tagged_refs_);
    }
#endif /* NDEBUG */
}

static object_t *obj_retain_(object_t *obj)
{
    assert (obj->mp != &mem_pool_static);

    if (likely(obj->refcnt > 0)) {
        if (likely(++obj->refcnt > 0))
            return obj;
        logger_panic(&_G.logger, "too many refcounts");
    }

    switch (obj->refcnt) {
      case 0:
        logger_panic(&_G.logger, "probably acting on a deleted object");
      default:
        /* WTF?! probably a memory corruption */
        logger_panic(&_G.logger, "should not happen");
    }
}

static void obj_release_(object_t *obj, bool *nullable destroyed)
{
    assert (obj->mp != &mem_pool_static);

    if (obj->refcnt > 1) {
        --obj->refcnt;
    } else if (obj->refcnt == 1) {
        obj_wipe_real(obj);
        mp_delete(obj->mp, &obj);
    } else {
        switch (obj->refcnt) {
          case 0:
            logger_panic(&_G.logger, "object refcounting issue");
          default:
            /* Probably a memory corruption we should have hit 0 first. */
            logger_panic(&_G.logger, "should not happen");
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
