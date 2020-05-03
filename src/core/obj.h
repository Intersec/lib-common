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

#if !defined(IS_LIB_COMMON_CORE_H) || defined(IS_LIB_COMMON_CORE_OBJ_H)
#  error "you must include core.h instead"
#else
#define IS_LIB_COMMON_CORE_OBJ_H

#include <lib-common/core.h>

/** \defgroup lc_obj Intersec Object Oriented C
 *
 * \brief Object Oriented Programming with C.
 *
 * \{
 */

/** \file lib-common/core-obj.h
 * \brief Objects and Virtual Tables in C (header)
 *
 * Intersec C objects are defined using two types of constructor macros:
 * - The class definition with the macros OBJ_CLASS*. It is used to create the
 *   class type, define the data fields and the virtual functions.
 * - The implementation of the virtual table with the macros OBJ_VTABLE*.
 *
 * The class should inherit from the root class `object`.
 * The `object` class defines two special virtual functions, `init` and
 * `wipe`.
 * - `init` is the constructor, constructors are called in the order of
 *   declaration.
 * - `wipe` is the destructor, destructors are called in the reverse order
 *   of declaration.
 *
 * The virtual functions table can also be extended in another compile unit
 * than the one using OBJ_VTABLE* with the macros OBJ_EXT_VTABLE*.
 *
 * Example:
 *
 * -- obj.h
 *
 * #define MY_BASE_OBJECT_FIELDS(pfx)                                        \
 *     OBJECT_FIELDS(pfx);                                                   \
 *     int a
 *
 * #define MY_BASE_OBJECT_METHODS(type_t)                                    \
 *     OBJECT_METHODS(type_t);                                               \
 *     void (*print)(type_t *self)
 *
 * OBJ_CLASS(my_base_object, object,
 *           MY_BASE_OBJECT_FIELDS, MY_BASE_OBJECT_METHODS)
 *
 * #define MY_CHILD_OBJECT_FIELDS(pfx, b_type_t)                             \
 *     MY_BASE_OBJECT_FIELDS(pfx);                                           \
 *     b_type_t b
 *
 * #define MY_CHILD_OBJECT_METHODS(type_t, b_type_t)                         \
 *     MY_BASE_OBJECT_METHODS(type_t);                                       \
 *     b_type_t (*get_b)(type_t *self)
 *
 * OBJ_CLASS(my_child_object, my_base_object,
 *           MY_CHILD_OBJECT_FIELDS, MY_CHILD_OBJECT_METHODS, double)
 *
 *
 * -- obj1.c
 *
 * static my_base_object_t *my_base_object_init(my_base_object_t *self)
 * {
 *     self->a = 42;
 *     return self;
 * }
 *
 * static void my_base_object_print(my_base_object_t *self)
 * {
 *     printf("base a: %d\n", self->a);
 * }
 *
 * OBJ_VTABLE(my_base_object)
 *     my_base_object.init = my_base_object_init;
 *     my_base_object.print = my_base_object_print;
 * OBJ_VTABLE_END()
 *
 * static my_child_object_t *my_child_object_init(my_child_object_t *self)
 * {
 *     self->b = 3.14159;
 *     return self;
 * }
 *
 * static void my_child_object_print(my_child_object_t *self)
 * {
 *     super_call(my_child_object, self, print);
 *     printf("child b: %g\n", obj_vcall(self, get_b));
 * }
 *
 * OBJ_VTABLE(my_child_object)
 *     my_child_object.init = my_child_object_init;
 *     my_child_object.print = my_child_object_print;
 * OBJ_VTABLE_END()
 *
 *
 * -- obj2.c
 *
 * static double my_child_object_get_b(my_child_object_t *self)
 * {
 *     return self->b;
 * }
 *
 * OBJ_EXT_VTABLE(my_child_object)
 *     my_child_object.get_b = my_child_object_get_b;
 * OBJ_EXT_VTABLE_END()
 *
 */

/* {{{ Constructor macros */
/* {{{ Private macros */

#define OBJ_MAKE_STRUCT_INHERIT(pfx, superclass, fields, ...)                \
    union {                                                                  \
        superclass##_t super;                                                \
        struct {                                                             \
            fields(pfx, ##__VA_ARGS__);                                      \
        };                                                                   \
    }

#define OBJ_MAKE_STRUCT_BASE(pfx, superclass, fields, ...)                   \
    fields(pfx, ##__VA_ARGS__)

#define OBJ_CLASS_NO_TYPEDEF_(pfx, superclass, fields, methods, make_struct, \
                              ...)                                           \
    typedef struct pfx##_class_t pfx##_class_t;                              \
    typedef void (*pfx##_vtable_extension_f)(pfx##_class_t * nonnull);       \
                                                                             \
    struct pfx##_t {                                                         \
        make_struct(pfx, superclass, fields, ##__VA_ARGS__);                 \
    };                                                                       \
    struct pfx##_class_t {                                                   \
        const superclass##_class_t * nonnull super;                          \
        const char * nonnull type_name;                                      \
        size_t      type_size;                                               \
        methods(pfx##_t, ##__VA_ARGS__);                                     \
    };                                                                       \
                                                                             \
    const pfx##_class_t * nonnull pfx##_class(void) __leaf;                  \
                                                                             \
    pfx##_vtable_extension_f nullable                                        \
    pfx##_set_vtable_extension(pfx##_vtable_extension_f nonnull func);       \
                                                                             \
    __unused__                                                               \
    static inline const superclass##_class_t * nonnull pfx##_super(void) {   \
        /* XXX This assert checks for field order: the fields of the super   \
         *     class should always be placed first.                          \
         */                                                                  \
        STATIC_ASSERT(offsetof(pfx##_t, v) == 0);                            \
        return superclass##_class();                                         \
    }

/* }}} */

/** Define an object class with typedef.
 *
 * \param pfx         object class prefix.
 * \param superclass  prefix of the super class.
 * \param fields      object fields.
 * \param methods     class methods.
 * \param __VA_ARGS__ additional arguments for \p fields and \p methods.
 */
#define OBJ_CLASS(pfx, superclass, fields, methods, ...)                     \
    typedef struct pfx##_t pfx##_t;                                          \
    OBJ_CLASS_NO_TYPEDEF(pfx, superclass, fields, methods, ##__VA_ARGS__)

/** Define an object class without typedef.
 *
 * \see OBJ_CLASS
 */
#define OBJ_CLASS_NO_TYPEDEF(pfx, superclass, fields, methods, ...)          \
    OBJ_CLASS_NO_TYPEDEF_(pfx, superclass, fields, methods,                  \
                          OBJ_MAKE_STRUCT_INHERIT, ##__VA_ARGS__)

/** Begin implementation of class virtual table.
 *
 * \param pfx object class prefix.
 */
#define OBJ_VTABLE(pfx)                                                      \
    static bool pfx##_vtable_extension_init_g;                               \
    static pfx##_vtable_extension_f pfx##_vtable_extension_func_g;           \
                                                                             \
    /** Set class vtable extension function.                                 \
     *                                                                       \
     * This function is called by OBJ_EXT_VTABLE* to set an extension        \
     * function for the class vtable.                                        \
     * This function must be called before initializing the class with       \
     * pfx##_class().                                                        \
     *                                                                       \
     * Returns the previous class vtable extension function or NULL if it    \
     * hasn't been set before.                                               \
     */                                                                      \
    pfx##_vtable_extension_f nullable                                        \
    pfx##_set_vtable_extension(pfx##_vtable_extension_f nonnull func)        \
    {                                                                        \
        pfx##_vtable_extension_f old_func = pfx##_vtable_extension_func_g;   \
                                                                             \
        if (unlikely(pfx##_vtable_extension_init_g)) {                       \
            e_panic("class constructor of " #pfx " has already been called " \
                    "while trying to set vtable extension function, check "  \
                    "vtable extension init order");                          \
        }                                                                    \
        pfx##_vtable_extension_func_g = func;                                \
        return old_func;                                                     \
    }                                                                        \
                                                                             \
    typedef _Atomic(pfx##_class_t *) pfx##_atomic_ptrclass_t;                \
                                                                             \
    const pfx##_class_t *pfx##_class(void) {                                 \
        __attr_section("intersec", "class")                                  \
        static pfx##_atomic_ptrclass_t ptr;                                  \
        pfx##_class_t *cls;                                                  \
                                                                             \
        cls = atomic_load_explicit(&ptr, memory_order_acquire);              \
        /* double checked locking for lazy initialization of the             \
         * pfx_class_t structure */                                          \
        if (unlikely(!cls)) {                                                \
            static spinlock_t lock;                                          \
                                                                             \
            spin_lock(&lock);                                                \
            cls = atomic_load_explicit(&ptr, memory_order_relaxed);          \
            if (unlikely(!cls)) {                                            \
                static pfx##_class_t  pfx;                                   \
                typeof(pfx.super)     cls_super;                             \
                const char           *type_name;                             \
                int                   type_size;                             \
                const pfx##_vtable_extension_f vtable_extension =            \
                    pfx##_vtable_extension_func_g;                           \
                                                                             \
                pfx##_vtable_extension_init_g = true;                        \
                                                                             \
                cls_super = pfx##_super();                                   \
                type_name = #pfx;                                            \
                type_size = sizeof(pfx##_t);                                 \
                cls = &pfx;                                                  \
                memcpy(cls, cls_super, sizeof(*cls_super));

/** End implementation of class virtual table. */
#define OBJ_VTABLE_END()                                                     \
                if (vtable_extension) {                                      \
                    (*vtable_extension)(cls);                                \
                }                                                            \
                cls->type_name = type_name;                                  \
                cls->type_size = type_size;                                  \
                cls->super     = cls_super;                                  \
                atomic_store_explicit(&ptr, cls, memory_order_release);      \
            }                                                                \
            spin_unlock(&lock);                                              \
        }                                                                    \
        return cls;                                                          \
    }

/** Default constructor priority for OBJ_EXT_VTABLE. */
#define OBJ_EXT_VTABLE_DEF_PRIO  1000

/** Begin implementation of class virtual table extension.
 *
 * This macro will create a constructor to set the vtable extension function
 * and begin its implementation.
 *
 * The vtable extension function is set in a constructor with the default
 * constructor priority. It must be set before initializing the class with
 * pfx##_class().
 *
 * If you have some constructor priority conflicts, you can use
 * OBJ_EXT_VTABLE_WITH_PRIO with a custom priority.
 *
 * \param pfx object class prefix.
 */
#define OBJ_EXT_VTABLE(pfx)                                                  \
     OBJ_EXT_VTABLE_WITH_PRIO(pfx, OBJ_EXT_VTABLE_DEF_PRIO)

/* Names generators. */
#define _OBJ_VT_SFX(pfx, sfx)  PFX_LINE_SFX(pfx##_, _ext_vtable##sfx)
#define _OBJ_VT(pfx)           _OBJ_VT_SFX(pfx, )

/** Begin implementation class virtual table extension with a custom priority.
 *
 * This macro can be used if pfx##_class() is called in a constructor with a
 * lower priority than OBJ_EXT_VTABLE_DEF_PRIO, or if you have multiple
 * vtable extensions and the order matters.
 *
 * Extensions with a lower priority are called first.
 *
 * \param pfx  object class prefix.
 * \param prio constructor priority.
 */
#define OBJ_EXT_VTABLE_WITH_PRIO(pfx, prio)                                  \
     OBJ_EXT_VTABLE_WITH_PRIO_(pfx, prio, _OBJ_VT(pfx),                      \
                               _OBJ_VT_SFX(pfx, _ctor),                      \
                               _OBJ_VT_SFX(pfx, _old_func_g))

#define OBJ_EXT_VTABLE_WITH_PRIO_(cls_pfx, prio, _ext_vt,                    \
                                  _ext_vt_constructor, _old_func_g)          \
    static cls_pfx##_vtable_extension_f _old_func_g;                         \
    static void _ext_vt(cls_pfx##_class_t * nonnull cls);                    \
                                                                             \
    __attribute__((constructor(prio)))                                       \
    static void _ext_vt_constructor(void)                                    \
    {                                                                        \
        _old_func_g = cls_pfx##_set_vtable_extension(&_ext_vt);              \
    }                                                                        \
                                                                             \
    static void _ext_vt(cls_pfx##_class_t * nonnull _cls)                    \
    {                                                                        \
        cls_pfx##_class_t cls_pfx;                                           \
        cls_pfx##_class_t *cls = &cls_pfx;                                   \
                                                                             \
        if (_old_func_g) {                                                   \
            (*_old_func_g)(_cls);                                            \
        }                                                                    \
        cls_pfx = *_cls;                                                     \

/** End implementation of class vtable extension. */
#define OBJ_EXT_VTABLE_END()                                                 \
        *_cls = *cls;                                                        \
    }

/* }}} */
/* {{{ Base object class */

/** Data fields for the root object class.
 *
 * \param pfx object class prefix.
 */
#define OBJECT_FIELDS(pfx)                                                   \
    union {                                                                  \
        const object_class_t * nonnull as_obj_cls;                           \
        const pfx##_class_t  * nonnull ptr;                                  \
    } v;                                                                     \
    mem_pool_t * nullable mp;                                                \
    ssize_t refcnt

/** Virtual functions for the root object class.
 *
 * \param type_t object instance type.
 */
#define OBJECT_METHODS(type_t)                                               \
    type_t  * nonnull (*nonnull init)(type_t * nonnull);                     \
    void     (*nonnull wipe)(type_t * nonnull);                              \
    type_t  * nonnull (*nonnull retain)(type_t * nonnull);                   \
    void     (*nonnull release)(type_t * nonnull);                           \
    bool     (*nonnull can_wipe)(type_t * nonnull)

typedef struct object_t object_t;
OBJ_CLASS_NO_TYPEDEF_(object, object, OBJECT_FIELDS, OBJECT_METHODS,
                      OBJ_MAKE_STRUCT_BASE)

/* }}} */
/* {{{ Public helpers */
/* {{{ Private macros */

#if !defined(NDEBUG) && defined(__GNUC__)
#  define obj_cast_debug(pfx, o)                                             \
    ({ typeof(o) __##pfx##_o = (o);                                          \
       if (__##pfx##_o && unlikely(!obj_is_a(__##pfx##_o, pfx))) {           \
           e_panic("%s:%d: cannot cast (%p : %s) into a %s",                 \
                   __FILE__, __LINE__, __##pfx##_o,                          \
                   obj_vfield(__##pfx##_o, type_name),                       \
                   pfx##_class()->type_name);                                \
     }                                                                       \
     __##pfx##_o; })
#  define cls_cast_debug(pfx, c)                                             \
    ({ typeof(c) __##pfx##_c = (c);                                          \
       if (unlikely(!cls_inherits(__##pfx##_c, pfx##_class()))) {            \
           e_panic("%s:%d: bad class cast (%s into %s)", __FILE__, __LINE__, \
                   __##pfx##_c->type_name, pfx##_class()->type_name);        \
       }                                                                     \
       __##pfx##_c; })
#else
#  define obj_cast_debug(pfx, o)  (o)
#  define cls_cast_debug(pfx, c)  (c)
#endif

#define obj_dyn_cast(pfx, o)                                                 \
    ({                                                                       \
        typeof(o) __##pfx##_o = (o);                                         \
        obj_is_a(__##pfx##_o, pfx) ? __##pfx##_o : NULL;                     \
    })

void * nonnull obj_init_real(const void * nonnull cls, void * nonnull o,
                             mem_pool_t * nonnull mp);
void obj_wipe_real(object_t * nonnull o);

/* }}} */

/** Test if one class inherits from another.
 *
 * \param[in] cls  Pointer to the child class descriptor.
 * \param[in] vptr Pointer to the parent class descriptor.
 */
bool cls_inherits(const void * nonnull cls, const void * nonnull vptr)
    __leaf __attribute__((pure));

/** Test if one object is instance of a class.
 *
 * \param[in] obj Object instance.
 * \param[in] cls Pointer to the parent class descriptor.
 */
#define obj_is_a_class(obj, cls)  cls_inherits((obj)->v.as_obj_cls, cls)

/** Test if one object is instance of a class.
 *
 * \param[in] obj Object instance.
 * \param[in] pfx Prefix of the parent class.
 */
#define obj_is_a(obj, pfx)  obj_is_a_class(obj, pfx##_class())

/** Get data field of an object. */
#define obj_vfield(o, field)  ((o)->v.ptr->field)

/** Get virtual method of an object. */
#define obj_vmethod(o, method)  ((o)->v.ptr->method)

/** Call virtual method of an object. */
#define obj_vcall(o, method, ...)  obj_vmethod(o, method)(o, ##__VA_ARGS__)

/** Cast object to another class type.
 *
 * This macro is to be used only in case the parent object is known to be of
 * the target type.
 *
 * In debug mode, this macro checks if the wanted type is compatible with the
 * actual type of the object (ie. if obj_is_a returns true), if the object is
 * not NULL.
 * In release mode, no check will be made.
 *
 * \param[in]  pfx  Prefix of the destination type.
 * \param[in]  o    Pointer on the object to cast.
 *
 * \return  a pointer equal to \p o, of the \p pfx type.
 */
/* TODO: This should have the same behavior as iop_obj_vcast, ie to check the
 * cast is valid, and not allow a NULL pointer. */
#define obj_vcast(pfx, o)  ((pfx##_t *)obj_cast_debug(pfx, o))

/** Cast object to another class type.
 *
 * Same as obj_vcast, but returns a const pointer.
 */
#define obj_ccast(pfx, o)  ((const pfx##_t *)obj_cast_debug(pfx, o))

/** Dynamically cast an object to the wanted type.
 *
 * This macro will cast \p o to \p pfx if \p o inherits from \p pfx and will
 * return NULL if this is not the case.
 */
#define obj_dynvcast(pfx, o)                                                 \
    ({                                                                       \
        void *_arg_o = (o); /* check constness with cast to void * */        \
                                                                             \
        (pfx##_t *)obj_dyn_cast(pfx, (typeof(o))_arg_o);                     \
    })

/** Dynamically cast a const object to the wanted type.
 *
 * Same as obj_dynvcast, but returns a const pointer, or NULL.
 */
#define obj_dynccast(pfx, o)  ((const pfx##_t *)obj_dyn_cast(pfx, o))

/** Cast class decriptor to another class descriptor. */
#define cls_cast(pfx, c)  ((const pfx##_class_t *)cls_cast_debug(pfx, c))

/** Call virtual method of given class for an object.
 *
 * \param pfx         Prefix of class.
 * \param o           Object instance.
 * \param f           Method to call.
 * \param __VA_ARGS__ Arguments passed to the method.
 */
#define cls_call(pfx, o, f, ...)                                             \
    (pfx##_class()->f(obj_vcast(pfx, o), ##__VA_ARGS__))

/** Call virtual method of parent class for an object.
 *
 * \param pfx         Prefix of object's class.
 * \param o           Object instance.
 * \param f           Method to call.
 * \param __VA_ARGS__ Arguments passed to the method.
 */
#define super_call(pfx, o, f, ...)                                           \
    (pfx##_class()->super->f(&(o)->super, ##__VA_ARGS__))

/** Call virtual method of parent class for a class.
 *
 * \param pfx         Prefix of object's class.
 * \param f           Method to call.
 * \param __VA_ARGS__ Arguments passed to the method.
 */
#define super_cls_call(pfx, f, ...)                                          \
    (pfx##_class()->super->f(__VA_ARGS__))

/** Call virtual method of parent class without object instance.
 *
 * \param pfx         Prefix of class.
 * \param f           Method to call.
 * \param __VA_ARGS__ Arguments passed to the method.
 */
#define class_vcall(cls, f, ...)  cls->f(__VA_ARGS__)

/** Get class descriptor for given class prefix. */
#define obj_class(pfx)    ((const object_class_t *)pfx##_class())

/** Create object instance with defined memory pool.
 *
 * The object instance will either be deleted with the frame end for frame
 * based memory pools (t_pool, r_pool) or when the object's reference counting
 * reaches 0.
 * The object instance will be initialized with a reference counting set to 1.
 *
 * \param mp  Memory pool of the object instance.
 * \param pfx Class prefix of the object.
 */
#define obj_mp_new(mp, pfx)  ({                                              \
        mem_pool_t *_mp = (mp);                                              \
                                                                             \
        ((pfx##_t *)obj_init_real(pfx##_class(), mp_new(_mp, pfx##_t, 1),    \
                                  _mp));                                     \
    })

/** Create object instance with libc memory pool.
 *
 * \see obj_mp_new
 *
 * \param pfx Class prefix of the object.
 */
#define obj_new(pfx)  obj_mp_new(&mem_pool_libc, pfx)

/** Create object instance of specified class with defined memory pool.
 *
 * \see obj_mp_new
 *
 * \param mp  Memory pool of the object instance.
 * \param pfx Parent class prefix of the object.
 * \param cls Real class descriptor of the object.
 */
#define obj_mp_new_of_class(mp, pfx, cls)  ({                                \
        mem_pool_t *_mp = (mp);                                              \
                                                                             \
        ((pfx##_t *)(assert(cls_inherits(cls, obj_class(pfx))),              \
                     obj_init_real(cls, mp_new(_mp, char, (cls)->type_size), \
                                   _mp)));                                   \
    })

/** Create object instance of specified class with libc memory pool.
 *
 * \see obj_mp_new_of_class
 *
 * \param pfx Parent class prefix of the object.
 * \param cls Real class descriptor of the object.
 */
#define obj_new_of_class(pfx, cls)                                           \
    obj_mp_new_of_class(&mem_pool_libc, pfx, cls)

/** Retain object instance.
 *
 * The default implementation will increment the object's reference counting
 * by 1.
 */
#define obj_retain(o)   obj_vcall(o, retain)

/** Release object instance.
 *
 * The default implementation will decrement the object's reference counting
 * by 1.
 * When the object's reference couting reaches 0, the object will
 * automatically be wiped and deleted depending of the memory pool.
 */
#define obj_release(o)  obj_vcall(o, release)

/** Delete object instance.
 *
 * If the object instance is not NULL, it will be released and set to NULL.
 */
#define obj_delete(op)  ({ if (*(op)) obj_release(*(op)); *(op) = NULL; })

/** Init object instance with static memory pool.
 *
 * The object instance must be wiped manually with \ref obj_wipe.
 *
 * \param pfx Class prefix of the object.
 * \param v   Object instance.
 */
#define obj_init(pfx, v)                                                     \
    ((pfx##_t *)obj_init_real(pfx##_class(), memset(v, 0, sizeof(*v)),       \
                              &mem_pool_static))

/** Wipe object instance with static memory pool.
 *
 * The object instance must have been manually initialized with \ref obj_init.
 *
 * \param o Object instance.
 */
#define obj_wipe(o)  obj_wipe_real(obj_vcast(object, o))

/* }}} */

/**\}*/
#endif
