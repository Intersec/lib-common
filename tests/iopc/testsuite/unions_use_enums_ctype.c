/* This test file is used to check that even with --c-unions-use-enums,
 * @ctype attributes are not broken. (attrs_valid is broken with v6) */

#include <lib-common/iop.h>
#include "attrs_valid-t.iop.h"


int main(void)
{
    my_union_a__t u;

    u = IOP_UNION(my_union_a, us1, LSTR_IMMED("plop"));

    IOP_UNION_SWITCH(&u) {
        IOP_UNION_CASE_V(my_union_a, &u, ub) {
        }
        IOP_UNION_CASE_V(my_union_a, &u, us1) {
        }
        IOP_UNION_CASE_V(my_union_a, &u, us2) {
        }
        IOP_UNION_CASE_V(my_union_a, &u, fls) {
        }
        IOP_UNION_CASE_V(my_union_a, &u, class) {
        }
    }

    return 0;
}
