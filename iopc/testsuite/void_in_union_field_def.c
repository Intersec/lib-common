/* This test file is used to check that definitions for void fields in
 * unions are present for void fields, and absent for non-void fields.
 * (feature #8536)
 */

#include <lib-common/iop.h>
#include "void_in_union-t.iop.h"

#ifndef void_in_union__a__v__empty_ft
#error "void tag should be defined for void field"
#endif

#ifdef void_in_union__a__i__empty_ft
#error "void tag should be undefined for fields not void"
#endif

#ifndef void_set_safe__v__empty_ft
#error "ctype void tag should be defined for void field"
#endif

#ifdef void_set_safe__i__empty_ft
#error "ctype void tag should not be defined for fields not void"
#endif

int main(void)
{
    return 0;
}
