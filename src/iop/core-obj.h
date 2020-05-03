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

#if !defined(IS_LIB_COMMON_IOP_H) || defined(IS_LIB_COMMON_IOP_CORE_OBJ_H)
#  error "you must include <lib-common/iop.h> instead"
#else
#define IS_LIB_COMMON_IOP_CORE_OBJ_H

#include <lib-common/core.h>
#include <lib-common/container-qhash.h>

/** IOP Core Obj.
 *
 * The purpose of IOP Core Objects is to facilitate the association of core
 * classes (C classes from core-obj.h) to a given family of IOP classes.
 *
 * The user declares the ancestor C class as a child of class 'iop_core_obj',
 * then the IOP core objet functions with:
 *
 * \p IOP_CORE_OBJ_DECLARE: declaration of the functions in the header.
 * \p IOP_CORE_OBJ_IMPL: implementation of the functions.
 * \p IOP_CORE_OBJ_IMPL_STATIC: static version of the implementation.
 *
 * The mapping between core object and IOP classes is written into a map of
 * type \p iop_core_obj_map_t, that should be created and deleted by the user.
 *
 * When declaring a class with \p OBJ_CLASS, the prefix of the associated IOP
 * class should be given after the regular arguments of \p OBJ_CLASS.
 */

#define _IOP_CORE_OBJ_FIELDS(pfx, desc_type_t)                               \
    OBJECT_FIELDS(pfx);                                                      \
    desc_type_t *nullable desc

#define IOP_CORE_OBJ_FIELDS(pfx, desc_type)                                  \
    _IOP_CORE_OBJ_FIELDS(pfx, desc_type##__t)

#define IOP_CORE_OBJ_METHODS(type_t, ...)                                    \
    OBJECT_METHODS(type_t)

OBJ_CLASS(iop_core_obj, object, _IOP_CORE_OBJ_FIELDS, IOP_CORE_OBJ_METHODS,
          void);

typedef struct iop_core_obj_map_t iop_core_obj_map_t;

iop_core_obj_map_t *nonnull iop_core_obj_map_new(void);
void iop_core_obj_map_delete(iop_core_obj_map_t *nullable *nonnull map);

void *nullable
_iop_core_obj_map_new_obj(const iop_core_obj_map_t *nonnull map,
                          const void *nonnull iop_obj);
const object_class_t *nullable
_iop_core_obj_map_get_cls(const iop_core_obj_map_t *nonnull map,
                          const void *nonnull iop_obj);
void _iop_core_obj_map_register_cls(iop_core_obj_map_t *nonnull map,
                                    const iop_struct_t *nonnull iop_cls,
                                    const object_class_t *nonnull cls);

#ifdef __has_blocks

/** \return A negative value to stop the scan now. */
typedef int (BLOCK_CARET on_core_obj_cls_b)
(const object_class_t *nonnull cls);

/** Iterate on all object classes of a given \p iop_core_obj_map_t. */
void iop_core_obj_map_for_each_cls(const iop_core_obj_map_t *nonnull map,
                                   on_core_obj_cls_b nonnull on_cls);

#endif /* __has_blocks */

#define IOP_CORE_OBJ_DECLARE(cls_pfx, iop_cls_pfx)                           \
    void cls_pfx##_register(const iop_struct_t *iop_cls,                     \
                            const object_class_t *cls);                      \
    cls_pfx##_t *cls_pfx##_new_obj(const iop_cls_pfx##__t *desc);            \
    const cls_pfx##_class_t *cls_pfx##_get_cls(const iop_cls_pfx##__t *desc) \

#define IOP_CORE_OBJ_IMPL(map, cls_pfx, iop_cls_pfx, ...)                    \
    __VA_ARGS__ void cls_pfx##_register(const iop_struct_t *iop_cls,         \
                                        const object_class_t *cls)           \
    {                                                                        \
        e_assert(panic, iop_class_is_a(iop_cls, &iop_cls_pfx##__s),          \
                 "`%pL' is not a `%pL'", &iop_cls->fullname,                 \
                 &iop_cls_pfx##__s.fullname);                                \
        e_assert(panic, cls_inherits(cls, obj_class(cls_pfx)),               \
                 "the class registered for `%pL' "                           \
                 "does not inherit from `" #cls_pfx "_t",                    \
                 &iop_cls->fullname);                                        \
        _iop_core_obj_map_register_cls((map), iop_cls, cls);                 \
    }                                                                        \
                                                                             \
    __VA_ARGS__ cls_pfx##_t *                                                \
    cls_pfx##_new_obj(const iop_cls_pfx##__t *desc)                          \
    {                                                                        \
        return _iop_core_obj_map_new_obj((map), desc);                       \
    }                                                                        \
                                                                             \
    __VA_ARGS__ __unused__ const cls_pfx##_class_t *                         \
    cls_pfx##_get_cls(const iop_cls_pfx##__t *desc)                          \
    {                                                                        \
        return cls_cast(cls_pfx, _iop_core_obj_map_get_cls((map), desc));    \
    }

#define IOP_CORE_OBJ_IMPL_STATIC(map, cls_pfx, iop_cls_pfx)                  \
    IOP_CORE_OBJ_IMPL(map, cls_pfx, iop_cls_pfx, static)

/** Register the mapping between a core object class and an IOP class.
 *
 * \param[in] ancestor_cls_pfx  Prefix of the ancestor class, as given to
 *                              macros \p IOP_CORE_OBJ_DECLARE and
 *                              \p IOP_CORE_OBJ_IMPL.
 *
 * \param[in] iop_cls_pfx  Prefix of the IOP class to register.
 *
 * \param[in] cls_pfx      Prefix of the core obj class to register.
 */
#define iop_core_obj_register(ancestor_cls_pfx, iop_cls_pfx, cls_pfx)        \
    ancestor_cls_pfx##_register(&iop_cls_pfx##__s, obj_class(cls_pfx))

/** Create a new IOP core object from its IOP description.
 *
 * \param[in] ancestor_cls_pfx  Prefix of the ancestor class.
 *
 * \param[in] desc  IOP description of the object to create. This description
 *                  is going to be duplicated in the newly created object
 *                  (attribute \p desc).
 *
 * \return The newly created object.
 */
#define iop_core_obj_new(ancestor_cls_pfx, desc)                             \
    ancestor_cls_pfx##_new_obj((desc))

#define iop_core_obj_get_cls(ancestor_cls_pfx, desc)                         \
    ancestor_cls_pfx##_get_cls((desc))

#endif /* IS_LIB_COMMON_IOP_CORE_OBJ_H */
