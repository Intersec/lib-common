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

#if !defined(IS_LIB_COMMON_IOP_H) || defined(IS_LIB_COMMON_IOP_MACROS_H)
#  error "you must include <lib-common/iop.h> instead"
#else
#define IS_LIB_COMMON_IOP_MACROS_H

/* {{{ IOP union helpers */

/** Union tag  type */
#define IOP_UNION_TAG_T(pfx) pfx##__tag_t

/** Get the tag value of a union field. */
#define IOP_UNION_TAG(pfx, field) pfx##__##field##__ft

/** Get the description of a union field. */
#define IOP_UNION_FDESC(pfx, field) pfx##__##field##__fdesc

/** Get the tag value of a union void field.
 *
 * This will not work with non-void field, so we can prevent the use of
 * IOP_UNION_VOID() and IOP_UNION_SET_V() with non-void fields.
 */
#define IOP_UNION_TAG_VOID(pfx, field) pfx##__##field##__empty_ft

/** Allow to make a switch on a union depending on the field chosen.
 *
 * Example:
 *
 * IOP_UNION_SWITCH(my_union) {
 *  IOP_UNION_CASE(pkg__my_union, my_union, a, a_val) {
 *      printf("a value is: %d", a);
 *  }
 *  IOP_UNION_CASE_P(pkg__my_union, my_union, b, b_val_ptr) {
 *      printf("a value is: %u", *b_val_ptr);
 *  }
 *  IOP_UNION_CASE_V(pkg__my_union, my_union, c) {
 *      printf("c type handled but value not important");
 *  }
 * }
 *
 * \param[in] u The union to switch on.
 */
#define IOP_UNION_SWITCH(u) \
    switch ((u)->iop_tag)

/** Case giving the value by pointer.
 *
 * XXX never use _continue_ inside an IOP_UNION_CASE_P.
 *
 * \param[in] pfx   The union prefix (pkg__name).
 * \param[in] u     The union given to IOP_UNION_SWITCH().
 * \param[in] field The union field to select.
 * \param[out] val  Pointer on the field value.
 */

#define IOP_UNION_CASE_P(pfx, u, field, val) \
        break;                                                          \
      case IOP_UNION_TAG(pfx, field):                                   \
        { const pfx##__t __attribute__((unused)) *val = (u); }          \
        for (typeof((u)->field) *val##_2 = &(u)->field, *val = val##_2; \
             val##_2; val##_2 = NULL)

/** Case giving the field value by value.
 *
 * XXX never use _continue_ inside an IOP_UNION_CASE.
 *
 * \param[in] pfx   The union prefix (pkg__name).
 * \param[in] u     The union given to IOP_UNION_SWITCH().
 * \param[in] field The union field to select.
 * \param[out] val  A copy of the field value.
 */
#define IOP_UNION_CASE(pfx, u, field, val) \
        break;                                                          \
      case IOP_UNION_TAG(pfx, field):                                   \
        { const pfx##__t __attribute__((unused)) *val = (u); }          \
        for (typeof((u)->field) *val##_p = &(u)->field, val = *val##_p; \
             val##_p; val##_p = NULL)

/** Case not giving the field value.
 *
 * XXX never use _continue_ inside an IOP_UNION_CASE_V.
 *
 * \param[in] pfx   The union prefix (pkg__name).
 * \param[in] u     The union given to IOP_UNION_SWITCH().
 * \param[in] field The union field to select.
 */
#define IOP_UNION_CASE_V(pfx, u, field)  \
        break;                                                          \
      case IOP_UNION_TAG(pfx, field):                                   \
        { const pfx##__t __attribute__((unused)) *_tmp = (u); }

/** Default case. */
#define IOP_UNION_DEFAULT() \
        break;              \
      default: (void)0;

/** Check if a union field is selected.
 *
 * \param[in] pfx   The union prefix (pkg__name).
 * \param[in] u     The union object.
 * \param[in] field The union field to check.
 */
#define IOP_UNION_IS(pfx, u, field) \
    ({ const pfx##__t *_tmp_union_is = (u);                                  \
       _tmp_union_is->iop_tag == IOP_UNION_TAG(pfx, field); })

/** Extract a value from a union.
 *
 * \param[in] pfx   The union prefix (pkg__name).
 * \param[in] u     The union object.
 * \param[in] field The union field to select.
 *
 * \return
 *   A pointer on the wanted field or NULL if this field isn't selected.
 */
#define IOP_UNION_GET(pfx, u, field) \
    ({ const pfx##__t *_tmp0 = (u);                                      \
       typeof(u) _tmp = (typeof(u))_tmp0;                                \
       IOP_UNION_IS(pfx, _tmp, field) ? &_tmp->field : NULL; })

/** Select an union field.
 *
 * \param[in]    pfx   The union prefix (pkg__name).
 * \param[inout] u     The union object.
 * \param[in]    field The union field to select.
 *
 * \return
 *   A pointer on the selected field.
 */
#define IOP_UNION_SET(pfx, u, field) \
    ({ pfx##__t *_tmp = (u);                                             \
       _tmp->iop_tag = IOP_UNION_TAG(pfx, field);                        \
       &_tmp->field;                                                     \
    })

/** Select a void union field.
 *
 * \param[in]    pfx   The union prefix (pkg__name).
 * \param[inout] u     The union object.
 * \param[in]    field The union field to select.
 */
#define IOP_UNION_SET_V(pfx, u, field)  \
    do {                                                                     \
        pfx##__t *_tmp = (u);                                                \
        _tmp->iop_tag = IOP_UNION_TAG_VOID(pfx, field);                      \
    } while(0)

/** Extract a value from a union by copying it.
 *
 * \param[out] dst   The variable to put the field value into.
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  u     The union object.
 * \param[in]  field The union field to select.
 *
 * \return
 *   True if the wanted field is selected, false otherwise.
 */
#define IOP_UNION_COPY(dst, pfx, u, field) \
    ({ pfx##__t *_tmp = (u);                                         \
       bool _copyok  = IOP_UNION_IS(pfx, _tmp, field);               \
       if (_copyok)                                                  \
           dst = _tmp->field;                                        \
       _copyok;                                                      \
    })

/** Make an immediate IOP union.
 *
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  field The field to select.
 * \param[in]  val   The field value.
 */
#define IOP_UNION_CST(pfx, field, val) \
    { IOP_UNION_TAG(pfx, field), { .field = val } }

/** Make an immediate IOP union with no value.
 *
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  field The field to select.
 */
#define IOP_UNION_VOID_CST(pfx, field) \
    { IOP_UNION_TAG_VOID(pfx, field), {} }

/** Make an immediate IOP union.
 *
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  field The field to select.
 * \param[in]  val   The field values.
 */
#define IOP_UNION_VA_CST(pfx, field, ...) \
    { IOP_UNION_TAG(pfx, field), { .field = { __VA_ARGS__ } } }

/** Make an IOP union.
 *
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  field The field to select.
 * \param[in]  val   The field value.
 */
#define IOP_UNION(pfx, field, val) \
    (pfx##__t){ IOP_UNION_TAG(pfx, field), { .field = val } }

/** Make an IOP union with no value.
 *
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  field The field to select.
 */
#define IOP_UNION_VOID(pfx, field) \
    (pfx##__t){ IOP_UNION_TAG_VOID(pfx, field), {} }

/** Make an IOP union.
 *
 * \param[in]  pfx   The union prefix (pkg__name).
 * \param[in]  field The field to select.
 * \param[in]  val   The field values.
 */
#define IOP_UNION_VA(pfx, field, ...) \
    (pfx##__t){ IOP_UNION_TAG(pfx, field), { .field = { __VA_ARGS__ } } }

/** Get the selected field name of an IOP union.
 *
 * \param[in] _data      The IOP union.
 * \param[in] _type_desc The IOP union description.
 */
#define IOP_UNION_TYPE_TO_LSTR(_data, _type_desc)                       \
    ({                                                                  \
        int _res = iop_ranges_search((_type_desc).ranges,               \
                                     (_type_desc).ranges_len,           \
                                     (_data)->iop_tag);                 \
        _res >= 0 ? (_type_desc).fields[_res].name : LSTR_NULL_V;       \
    })

/** Get the selected field name of an IOP union.
 *
 * \param[in] _data      The IOP union.
 * \param[in] _type_desc The IOP union description.
 */
#define IOP_UNION_TYPE_TO_STR(_data, _type_desc)  \
    IOP_UNION_TYPE_TO_LSTR((_data), (_type_desc)).s

/* }}} */
/* {{{ Data packing helpers */

/** Pack an IOP structure into IOP binary format using the t_pool().
 *
 * This version of `iop_bpack` allows to pack an IOP structure in one
 * operation and uses the t_pool() to allocate the resulting buffer.
 *
 * \param[in] _pfx   Prefix of the IOP structure.
 * \param[in] _flags Packer modifiers (see iop_bpack_flags).
 * \return
 *   The buffer containing the packed structure.
 */
#define t_iop_bpack_flags(_pfx, _val, _flags)                            \
    ({                                                                   \
        const _pfx##__t *_tval = (_val);                                 \
        \
        t_iop_bpack_struct_flags(&_pfx##__s, _tval, _flags);             \
    })


/** Pack an IOP structure into IOP binary format using the t_pool().
 *
 * This version of `iop_bpack` allows to pack an IOP structure in one
 * operation and uses the t_pool() to allocate the resulting buffer.
 *
 * \param[in] _pfx  Prefix of the IOP structure.
 * \return
 *   The buffer containing the packed structure.
 */
#define t_iop_bpack(_pfx, _val)                            \
    ({                                                     \
        const _pfx##__t *_tval = (_val);                   \
        \
        t_iop_bpack_struct(&_pfx##__s, _tval);             \
    })

/** Unpack an IOP structure from IOP binary format using the t_pool().
 *
 * This version of `iop_bunpack` allows to unpack an IOP structure in one
 * operation and uses the t_pool() to allocate the resulting structure.
 *
 * This function calls `iop_bunpack` with the copy parameter set to false.
 *
 * \param[in]  _str  The lstr_t “compatible” variable containing the packed
 *                   object.
 * \param[in]  _pfx  Prefix of the IOP structure.
 * \param[out] _valp Pointer on the structure to use for unpacking.
 */
#define t_iop_bunpack(_str, _pfx, _valp)                                 \
    ({                                                                   \
        _pfx##__t *_tval = (_valp);                                      \
        typeof(_str) _str2 = (_str);                                     \
        pstream_t _ps = ps_init(_str2->s, _str2->len);                   \
        \
        t_iop_bunpack_ps(&_pfx##__s, _tval, _ps, false);                 \
    })

/** Unpack an IOP structure from IOP binary format using the t_pool().
 *
 * This version of `iop_bunpack` allows to unpack an IOP structure in one
 * operation and uses the t_pool() to allocate the resulting structure.
 *
 * This function calls `iop_bunpack` with the copy parameter set to true.
 *
 * \param[in]  _str  The lstr_t “compatible” variable containing the packed
 *                   object.
 * \param[in]  _pfx  Prefix of the IOP structure.
 * \param[out] _valp Pointer on the structure to use for unpacking.
 */
#define t_iop_bunpack_dup(_str, _pfx, _valp)                            \
    ({                                                                  \
        _pfx##__t *_tval = (_valp);                                     \
        typeof(_str) _str2 = (_str);                                    \
        pstream_t _ps = ps_init(_str2->s, _str2->len);                  \
        \
        t_iop_bunpack_ps(&_pfx##__s, _tval, _ps, true);                 \
    })

/* }}} */
/* {{{ RPC helpers */

/** Get an IOP interface structure definition.
 *
 * \param[in] _mod  RPC module name.
 * \param[in] _if   RPC interface name.
 */
#define IOP_IFACE(_mod, _if)              _mod##__##_if(ifp)

/** Get an RPC structure definition.
 *
 * \param[in] _mod  RPC module name.
 * \param[in] _if   RPC interface name.
 * \param[in] _rpc  RPC name.
 */
#define IOP_RPC(_mod, _if, _rpc)          _mod##__##_if(_rpc##__rpc)

/** Get the type of RPC arguments/response/exception.
 *
 * \param[in] _mod  RPC module name.
 * \param[in] _if   RPC interface name.
 * \param[in] _rpc  RPC name.
 * \param[in] what  `args`, `res` or `exn`.
 */
#define IOP_RPC_T(_mod, _if, _rpc, what)  _mod##__##_if(_rpc##_##what##__t)

/** Get the command tag of an RPC.
 *
 * \param[in] _mod  RPC module name.
 * \param[in] _if   RPC interface name.
 * \param[in] _rpc  RPC name.
 */
#define IOP_RPC_CMD(_mod, _if, _rpc) \
    (_mod##__##_if##__TAG << 16) | _mod##__##_if(_rpc##__rpc__tag)

/* }}} */
/* {{{ Helpers generated for enums */

#define IOP_ENUM(pfx)

/* }}} */
/* {{{ Helpers generated for structures and unions */

#define IOP_GENERIC(pfx)

/* }}} */
/* {{{ Helpers generated for classes */

#define IOP_CLASS(pfx)

/* }}} */
/* {{{ Helpers for classes manipulation */

#ifndef NDEBUG
#  define iop_obj_cast_debug(pfx, o)  \
    ({                                                                       \
        typeof(*o) *__o = (o);                                               \
        if (!iop_obj_is_a(__o, pfx)) {                                       \
            e_panic("cannot cast %p to type " TOSTR(pfx), __o);              \
        }                                                                    \
        __o;                                                                 \
    })
#else
#  define iop_obj_cast_debug(pfx, o)  (o)
#endif

#define iop_obj_dyn_cast(pfx, o)                                             \
    ({                                                                       \
        typeof(*o) *__o = (o);                                               \
        iop_obj_is_a(__o, pfx) ? __o : NULL;                                 \
    })

/** Cast an IOP class object to the wanted type.
 *
 * This macro is to be used only in case the parent object is known to be of
 * the target type.
 *
 * In debug mode, this macro checks if the wanted type is compatible with the
 * actual type of the object (ie. if iop_obj_is_a returns true).
 * In release mode, no check will be made.
 *
 * \param[in]  pfx  Prefix of the destination type.
 * \param[in]  o    Pointer on the class object to cast.
 *
 * \return  a pointer equal to \p o, of the \p pfx type.
 */
#define iop_obj_vcast(pfx, o)                                                \
    ({                                                                       \
        void *_arg_o = (o); /* check constness with cast to void * */        \
                                                                             \
        (pfx##__t *)iop_obj_cast_debug(pfx, (typeof(o))_arg_o);              \
    })

/** Cast an IOP class object to the wanted type.
 *
 * Same as iop_obj_vcast, but returns a const pointer.
 */
#define iop_obj_ccast(pfx, o)  ((const pfx##__t *)iop_obj_cast_debug(pfx, o))

/** Dynamically cast an IOP class object to the wanted type.
 *
 * This macro will cast \p o to \p pfx if \p o inherits from \p pfx and will
 * return NULL if this is not the case.
 */
#define iop_obj_dynvcast(pfx, o)                                             \
    ({                                                                       \
        void *_arg_o = (o); /* check constness with cast to void * */        \
                                                                             \
        (pfx##__t *)iop_obj_dyn_cast(pfx, (typeof(o))_arg_o);                \
    })

/** Dynamically cast an IOP class object to the wanted type.
 *
 * This macro will cast \p o to \p pfx if \p o inherits from \p pfx and will
 * return NULL if this is not the case.
 */
#define iop_obj_dynccast(pfx, o)  ((const pfx##__t *)iop_obj_dyn_cast(pfx, o))

/** Get the class id of a class type. */
#define IOP_CLASS_ID(type)   type##__class_id
/** Get the class id of a class instance. */
#define IOP_OBJ_CLASS_ID(o)  (o)->__vptr->class_attrs->class_id

/** Allow to make a switch on a class instance.
 *
 * This switch matches the exact class id of a class instance. This can be
 * used either with the IOP_OBJ_CASE() helper that match the class and
 * provides the casted instance, or directly using case IOP_CLASS_ID(), but
 * not both.
 *
 * IOP_OBJ_EXACT_SWITCH(my_class_instance) {
 *   IOP_OBJ_CASE(a_class, my_class_instance, a_instance) {
 *     do_something_with(a_instance);
 *   }
 *
 *   IOP_OBJ_CASE(b_class, my_class_instance, b_instance) {
 *     do_something_with(b_instance);
 *   }
 *
 *   IOP_OBJ_EXACT_DEFAULT() {
 *     do_something_with(my_class_instance);
 *   }
 * }
 *
 * This also can be written as:
 *
 * IOP_OBJ_EXACT_SWITCH(my_class_instance) {
 *   case IOP_CLASS_ID(a_class):
 *     do_something_with(my_class_instance);
 *     break;
 *
 *   case IOP_CLASS_ID(b_class):
 *     do_something_with(my_class_instance);
 *     break;
 *
 *   default:
 *     do_something_with(my_class_instance);
 *     break;
 * }
 */
#define IOP_OBJ_EXACT_SWITCH(inst) \
    switch ((inst)->__vptr->class_attrs->class_id)

/** Allow to make a switch on a class descriptor.
 *
 * Same as \ref IOP_OBJ_EXACT_SWITCH, but with a class descriptor instead of a
 * class instance.
 * Of course, \ref IOP_OBJ_CASE cannot be used inside such a switch.
 *
 * This should be used like that:
 *
 * IOP_CLASS_EXACT_SWITCH(my_class_descriptor) {
 *   case IOP_CLASS_ID(a_class):
 *     // This is a a_class
 *     break;
 *
 *   case IOP_CLASS_ID(b_class):
 *     // This is a b_class
 *     break;
 *
 *   default:
 *     // This is something else...
 *     break;
 * }
 */
#define IOP_CLASS_EXACT_SWITCH(cls) \
    switch ((cls)->class_attrs->class_id)

/** Case matching a given IOP class and giving the casted value.
 *
 * XXX never use _continue_ inside an IOP_OBJ_CASE of an IOP_OBJ_SWITCH.
 *
 * \param[in] pfx    The class name (pkg__name)
 * \param[in] inst   The class instance given to IOP_OBJ_SWITCH()
 * \param[out] val   Pointer to the instance casted to the given class if the
 *                   case matched.
 */
#define IOP_OBJ_CASE(pfx, inst, val) \
        break;                                                               \
      case IOP_CLASS_ID(pfx):                                                \
        for (pfx##__t *val = iop_obj_vcast(pfx, (inst)); val; val = NULL)

/** Case matching a given IOP class and giving the const casted value.
 *
 * XXX never use _continue_ inside an IOP_OBJ_CASE_CONST of an IOP_OBJ_SWITCH.
 *
 * \param[in] pfx    The class name (pkg__name)
 * \param[in] inst   The class instance given to IOP_OBJ_SWITCH()
 * \param[out] val   Pointer to the instance casted to the given class if the
 *                   case matched. The pointer is const.
 */
#define IOP_OBJ_CASE_CONST(pfx, inst, val) \
        break;                                                               \
      case IOP_CLASS_ID(pfx):                                                \
        for (const pfx##__t *val = iop_obj_ccast(pfx, (inst)); val; val = NULL)

/** Default case.
 */
#define IOP_OBJ_EXACT_DEFAULT()  \
        break;                                                               \
      default: (void)0;

/** Case matching a given IOP class.
 *
 * XXX never use _continue_ inside an IOP_CLASS_CASE of an IOP_CLASS_SWITCH.
 *
 * \param[in] pfx    The class name (pkg__name)
 */
#define IOP_CLASS_CASE(pfx) \
        break;                                                               \
      case IOP_CLASS_ID(pfx):

/** Allow to make a switch on a class descriptor.
 *
 * This switch matches the nearest parent class id of a class descriptor.
 * It must be used with the IOP_CLASS_CASE() macro and must contain a
 * IOP_CLASS_DEFAULT().
 *
 * IOP_CLASS_SWITCH(name, my_class_descriptor) {
 *   IOP_CLASS_CASE(a_class) {
 *   }
 *
 *   IOP_CLASS_CASE(b_class) {
 *   }
 *
 *   IOP_CLASS_DEFAULT(name) {
 *   }
 * }
 *
 * \param[in] name A uniq token to identify the switch. This is used to allow
 *                 imbricated IOP_OBJ_SWITCH
 * \param[in] cls  The class descriptor to be matched.
 */
#define IOP_CLASS_SWITCH(name, cls)  \
    for (const iop_struct_t *__##name##_st = cls,                            \
                            *__##name##_next_st = cls,                       \
                            *__##name##_missing_switch_default = NULL;       \
         __##name##_st == __##name##_next_st;                                \
         __##name##_st = __##name##_st->class_attrs->parent)                 \
        switch (__##name##_st->class_attrs->class_id)

/** Allow to make a switch on an inherited class instance.
 *
 * This switch matches the nearest parent class id of a class instance. It can
 * only be used with the IOP_OBJ_CASE() macros and must contain a
 * IOP_OBJ_DEFAULT().
 *
 * IOP_OBJ_SWITCH(name, my_class_instance) {
 *   IOP_OBJ_CASE(a_class, my_class_instance, a_instance) {
 *   }
 *
 *   IOP_OBJ_CASE(b_class, my_class_instance, b_instance) {
 *   }
 *
 *   IOP_OBJ_DEFAULT(name) {
 *   }
 * }
 *
 * \param[in] name A uniq token to identify the switch. This is used to allow
 *                 imbricated IOP_OBJ_SWITCH
 * \param[in] inst The class instance to be matched.
 */
#define IOP_OBJ_SWITCH(name, inst)  IOP_CLASS_SWITCH(name, (inst)->__vptr)

/** Case to match unsupported classes.
 *
 * XXX never use _continue_ inside an IOP_(OBJ|CLASS)_DEFAULT.
 *
 * \param[in] name The name of the IOP_(OBJ|CLASS)_SWITCH()
 */
#define IOP_CLASS_DEFAULT(name)  \
        break;                                                               \
      default:                                                               \
        IGNORE(__##name##_missing_switch_default);                           \
        if (__##name##_next_st->class_attrs->parent) {                       \
            __##name##_next_st = __##name##_next_st->class_attrs->parent;    \
            continue;                                                        \
        } else

#define IOP_OBJ_DEFAULT  IOP_CLASS_DEFAULT

/* }}} */
/* {{{ Helpers for use of IOPs as key in QH/QM */

#define qhash_iop_hash_fn(name, pfx)    qhash_##name##_##pfx##__hash
#define qhash_iop_equals_fn(name, pfx)  qhash_##name##_##pfx##__equals

#define QHASH_IOP_FUNCS(name, pfx)                                           \
    static inline uint32_t                                                   \
    qhash_iop_hash_fn(name, pfx)(const qhash_t * nullable qhash,             \
                                 const pfx##__t * nonnull key)               \
    {                                                                        \
        uint8_t hash[4];                                                     \
                                                                             \
        iop_hash32(&pfx##__s, key, hash, 0);                                 \
                                                                             \
        return get_unaligned_cpu32(hash);                                    \
    }                                                                        \
    static inline bool                                                       \
    qhash_iop_equals_fn(name, pfx)(const qhash_t * nullable qhash,           \
                                   const pfx##__t * nonnull k1,              \
                                   const pfx##__t * nonnull k2)              \
    {                                                                        \
        return iop_equals(pfx, k1, k2);                                      \
    }

#define QH_K_IOP_T(type, name, pfx)                                          \
    QHASH_IOP_FUNCS(name, pfx)                                               \
    qh_k##type##_t(name, pfx##__t, qhash_iop_hash_fn(name, pfx),             \
                   qhash_iop_equals_fn(name, pfx))

#define qh_iop_kvec_t(name, pfx)       QH_K_IOP_T(vec, name, pfx)
#define qh_iop_kptr_t(name, pfx)       QH_K_IOP_T(ptr, name, pfx)
#define qh_iop_kptr_ckey_t(name, pfx)  QH_K_IOP_T(ptr_ckey, name, pfx)

#define QM_K_IOP_T(type, name, pfx, val_t)                                   \
    QHASH_IOP_FUNCS(name, pfx)                                               \
    qm_k##type##_t(name, pfx##__t, val_t, qhash_iop_hash_fn(name, pfx),      \
                   qhash_iop_equals_fn(name, pfx))

#define qm_iop_kvec_t(name, pfx, val_t)  QM_K_IOP_T(vec, name, pfx, val_t)
#define qm_iop_kptr_t(name, pfx, val_t)  QM_K_IOP_T(ptr, name, pfx, val_t)
#define qm_iop_kptr_ckey_t(name, pfx, val_t)                                 \
    QM_K_IOP_T(ptr_ckey, name, pfx, val_t)


/* }}} */

#endif
