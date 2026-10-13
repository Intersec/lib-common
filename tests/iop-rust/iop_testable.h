/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

/* Which IOP types the generated tests can check with a default-constructed
 * object. See `iop_testable.rs`.
 */

#ifndef IS_TESTS_IOP_RUST_IOP_TESTABLE_H
#define IS_TESTS_IOP_RUST_IOP_TESTABLE_H

#include <lib-common/iop.h>

/* Whether a default value can fail a constraint of the field.
 *
 * A default value leaves an optional field unset and an array empty, which
 * the constraints accept, except a minimum number of elements. A default
 * declared in the IOP can still break a constraint: reading the value to
 * check it would duplicate the code under test, so any constraint that a
 * value can break excludes the field.
 */
static inline bool iop_testable_field_can_break_constraint(
    const iop_struct_t *nonnull st, const iop_field_t *nonnull f
)
{
    const iop_field_attrs_t *attrs = iop_field_get_attrs(st, f);
    unsigned flags = attrs ? attrs->flags : 0;
    unsigned risky = (1U << IOP_FIELD_NON_EMPTY) |
                     (1U << IOP_FIELD_NON_ZERO) | (1U << IOP_FIELD_MIN) |
                     (1U << IOP_FIELD_MAX) | (1U << IOP_FIELD_MIN_LENGTH) |
                     (1U << IOP_FIELD_PATTERN);

    if (f->repeat == IOP_R_OPTIONAL) {
        return false;
    }
    if (f->repeat == IOP_R_REPEATED) {
        return TST_BIT(&flags, IOP_FIELD_MIN_OCCURS);
    }
    if (f->repeat == IOP_R_REQUIRED && f->type == IOP_T_ENUM) {
        /* The zero of a strict enum may not be a valid value. */
        unsigned en_flags = f->u1.en_desc->flags;

        if (TST_BIT(&en_flags, IOP_ENUM_STRICT)) {
            return true;
        }
    }
    return flags & risky;
}

static inline bool
iop_testable_has_safe_constraints(const iop_struct_t *nonnull st)
{
    iop_struct_for_each_field(f, field_st, st) {
        if (iop_testable_field_can_break_constraint(field_st, f)) {
            return false;
        }
        if (f->repeat == IOP_R_REQUIRED && f->type == IOP_T_STRUCT &&
            !iop_field_is_reference(f) &&
            !iop_testable_has_safe_constraints(f->u1.st_desc))
        {
            return false;
        }
    }
    return true;
}

/* Whether a default value fills each required field.
 *
 * C leaves a required reference, class or union field NULL, and the
 * packers dereference it.
 */
static inline bool
iop_testable_can_fill_fields(const iop_struct_t *nonnull st)
{
    iop_struct_for_each_field(f, field_st, st) {
        if (f->repeat != IOP_R_REQUIRED) {
            continue;
        }
        if (iop_field_is_reference(f) || iop_field_is_class(f) ||
            f->type == IOP_T_UNION)
        {
            return false;
        }
        if (f->type == IOP_T_STRUCT &&
            !iop_testable_can_fill_fields(f->u1.st_desc))
        {
            return false;
        }
    }
    return true;
}

/* Whether a default-constructed object of `st` packs, and satisfies its
 * constraints.
 */
static inline bool iop_testable_is_testable(const iop_struct_t *nonnull st)
{
    if (st->is_union) {
        /* A default value selects no member. */
        return false;
    }
    if (iop_struct_is_class(st) && st->class_attrs->is_abstract) {
        return false;
    }
    return iop_testable_has_safe_constraints(st) &&
           iop_testable_can_fill_fields(st);
}

/* A private class must not leave the process: the YAML packer asserts on
 * it.
 */
static inline bool iop_testable_is_private(const iop_struct_t *nonnull st)
{
    return iop_struct_is_class(st) && st->class_attrs->is_private;
}

#endif
