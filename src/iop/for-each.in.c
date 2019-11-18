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

/* XXX Make this file syntastic-compatible. */
#ifndef F_NAME
#  include <lib-common/iop.h>
#  include "helpers.in.c"
#  define F(x)  x
#  define F_NAME  __unused__ static default_func
#  define ON_FIELD  default_on_field
#  define ON_FIELD_DONE()
#  define ON_STRUCT  default_on_struct
#  define SET_INDEX(index)

static int default_on_field(const iop_struct_t *st, void *st_ptr,
                            const iop_field_t *field)
{
    return 0;
}

static int default_on_struct(const iop_struct_t *st, void *st_ptr)
{
    return 0;
}

#endif

#ifdef F_PROTO
#  define  __F_PROTO  , F_PROTO
#  define  __F_ARGS   , F_ARGS
#else
#  define  __F_PROTO
#  define  __F_ARGS
#endif

#ifndef MODIFIER
#  define MODIFIER
#endif

#ifdef ON_STRUCT
#  define __ON_STRUCT(st_desc, st_ptr)                                       \
    do {                                                                     \
        int res = RETHROW(ON_STRUCT(st_desc, st_ptr __F_ARGS));              \
                                                                             \
        if (res == IOP_FIELD_SKIP) {                                         \
            return 0;                                                        \
        }                                                                    \
    } while (0)
#else
#  define __ON_STRUCT(...)
#endif

static int F(on_field)(const iop_struct_t *st_desc, const iop_field_t *fdesc,
                       void *st_ptr __F_PROTO);
static int F(for_each_field)(const iop_struct_t *st_desc, void *st_ptr
                             __F_PROTO);

static int F(for_each_st_field)(const iop_struct_t *st_desc, void *st_ptr
                                __F_PROTO)
{
    const iop_field_t *field = st_desc->fields;

    for (int i = 0; i < st_desc->fields_len; i++) {
        RETHROW(F(on_field)(st_desc, field++, st_ptr __F_ARGS));
    }

    return 0;
}


static int
F(for_each_repeated_field)(const iop_field_t *fdesc, void *fptr __F_PROTO)
{
    bool field_is_pointed = iop_field_is_pointed(fdesc);
    size_t fsize;
    iop_array_i8_t array = *(iop_array_i8_t *)fptr;

    if (field_is_pointed) {
        fsize = sizeof(void *);
    } else {
        fsize = fdesc->size;
    }

    tab_for_each_pos(index, &array) {
#ifdef SET_INDEX
        SET_INDEX(index);
#endif

        fptr = field_is_pointed ? *(void **)array.tab : array.tab;
        RETHROW(F(for_each_field)(fdesc->u1.st_desc, fptr __F_ARGS));

        array.tab += fsize;
    }

    return 0;
}

static int F(on_field)(const iop_struct_t *st_desc, const iop_field_t *fdesc,
                       void *st_ptr __F_PROTO)
{
    void *fptr;
    int res = 0;

#ifdef F_LOCAL
    F_LOCAL;
#endif

#ifdef ON_FIELD
    res = ON_FIELD(st_desc, st_ptr, fdesc __F_ARGS);
    if (res < 0 || res == IOP_FIELD_SKIP) {
        goto end;
    }
#endif

    if (iop_type_is_scalar(fdesc->type)) {
        goto end;
    }

    fptr = (byte *)st_ptr + fdesc->data_offs;

    if (fdesc->repeat == IOP_R_REPEATED) {
        res = F(for_each_repeated_field)(fdesc, fptr __F_ARGS);
        goto end;
    }

    if (fdesc->repeat == IOP_R_OPTIONAL
    ||  iop_field_is_reference(fdesc)
    ||  iop_field_is_class(fdesc))
    {
        fptr = *(void **)fptr;
        if (!fptr) {
            assert (fdesc->repeat == IOP_R_OPTIONAL);
            goto end;
        }
    }

    res = F(for_each_field)(fdesc->u1.st_desc, fptr __F_ARGS);

  end:
#ifdef ON_FIELD_DONE
    ON_FIELD_DONE();
#endif

    return res;
}

static int F(for_each_class_field)(const iop_struct_t *st_desc, void *v
                                   __F_PROTO)
{
    if (st_desc->class_attrs->parent) {
        RETHROW(F(for_each_class_field)(st_desc->class_attrs->parent, v
                                        __F_ARGS));
    }

    return F(for_each_st_field)(st_desc, v __F_ARGS);
}

static int F(for_each_field)(const iop_struct_t *st_desc, void *st_ptr
                             __F_PROTO)
{
    if (iop_struct_is_class(st_desc)) {
        st_desc = *(const iop_struct_t **)st_ptr;

        __ON_STRUCT(st_desc, st_ptr);

        return F(for_each_class_field)(st_desc, st_ptr __F_ARGS);
    }

    __ON_STRUCT(st_desc, st_ptr);

    if (st_desc->is_union) {
        const iop_field_t *field = get_union_field(st_desc, st_ptr);

        return F(on_field)(st_desc, field, st_ptr __F_ARGS);
    }

    return F(for_each_st_field)(st_desc, st_ptr __F_ARGS);
}

#ifdef F_STATIC
static
#endif
int F_NAME(const iop_struct_t * nullable st_desc,
           MODIFIER void *nonnull st_ptr __F_PROTO)
{
    if (!st_desc) {
        st_desc = *(const iop_struct_t **)st_ptr;
        assert (iop_struct_is_class(st_desc));
    }

    return F(for_each_field)(st_desc, (void *)st_ptr __F_ARGS);
}

#undef __ON_STRUCT
#undef ON_STRUCT
#undef ON_FIELD
#undef ON_FIELD_DONE
#undef SET_INDEX
#undef F
#undef F_ARGS
#undef F_PROTO
#undef __F_ARGS
#undef __F_PROTO
#undef F_NAME
#undef F_STATIC
#undef F_LOCAL
#undef MODIFIER
