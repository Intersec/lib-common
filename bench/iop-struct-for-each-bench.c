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

#include <lib-common/iop.h>
#include <lib-common/tests/iop/tstiop.iop.h>

/* {{{ Now removed implementation of iop_class_for_each_field. */

__attribute__((noinline))
static const iop_field_t *
_iop_class_get_next_field(const iop_struct_t **st, int *it)
{
    while (*st && *it >= (*st)->fields_len) {
        *st = (*st)->class_attrs->parent;
        *it = 0;
    }

    return *st ? (*st)->fields + (*it)++ : NULL;
}

/** Loop on all fields of a class and its parents.
 *
 *  f is an already-declared iop_field_t where the results will be put in.
 *
 *  st is an already-declared iop_struct_t where the class of each field put
 *  in f will be put in.
 */
#define iop_class_for_each_field(f, st, _cl)                                 \
    st = _cl;                                                                \
    for (int _i_##f = 0; (f = _iop_class_get_next_field(&st, &_i_##f));)

/* }}} */

/* This bench helps evaluating the cost of field iteration methods through IOP
 * structs and classes.
 *
 * Launch bench:
 *
 *     ./iop-struct-for-each-bench <struct-name> <nb-loop> (0|1)(*)
 *
 *     (*) 0: old way
 *         1: new way
 */

uint64_t cnt_g;

static void do_things(const iop_struct_t *st, const iop_field_t *fdesc)
{
    cnt_g += st->fullname.len + fdesc->name.len;
}

static void old_loop_cls(const iop_struct_t *st)
{
    const iop_struct_t *cls;
    const iop_field_t *fdesc;

    iop_class_for_each_field(fdesc, cls, st) {
        do_things(cls, fdesc);
    }
}

static void old_loop_st(const iop_struct_t *st)
{
    for (int i = 0; i < st->fields_len; i++) {
        do_things(st, &st->fields[i]);
    }
}

static void new_loop(const iop_struct_t *st)
{
    iop_struct_for_each_field(fdesc, _st, st) {
        do_things(_st, fdesc);
    }
}

typedef void (*loop_f)(const iop_struct_t *st);

static void run_loops(const iop_struct_t *st, int nb_loops, bool new_way)
{
    loop_f loop;

    if (new_way) {
        loop = &new_loop;
    } else
    if (iop_struct_is_class(st)) {
        loop = &old_loop_cls;
    } else {
        loop = &old_loop_st;
    }

    for (int i = 0; i < nb_loops; i++) {
        (*loop)(st);
    }
}

int main(int argc, char **argv)
{
    int nb_loops;
    const iop_struct_t *st;
    const iop_obj_t *obj;
    bool new_way;

    if (argc <= 3) {
        fprintf(stderr, "usage: %s st_name nb_loops (0|1)\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    IOP_REGISTER_PACKAGES(&tstiop__pkg);
    obj = iop_get_obj(LSTR(argv[1]));
    if (!obj) {
        fprintf(stderr, "unknown IOP object: `%s'\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    if (obj->type != IOP_OBJ_TYPE_ST) {
        fprintf(stderr, "IOP object `%s' is not a struct/union/class\n",
                argv[1]);
        exit(EXIT_FAILURE);
    }

    nb_loops = atoi(argv[2]);
    new_way = atoi(argv[3]);
    st = obj->desc.st;
    run_loops(st, nb_loops, new_way);

    return 0;
}
