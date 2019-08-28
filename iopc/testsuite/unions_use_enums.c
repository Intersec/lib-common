/* This test file is used to check that a C file will not compile if its
 * IOP_UNION_SWITCH misses at least one case. (feature #50352) */

#include <lib-common/iop.h>
#include "typedef1-t.iop.h"


int main(void)
{
    typedef1__foo_u__t u;

    u = IOP_UNION(typedef1__foo_u, bar, 1);

    /* This should not compile because all the cases are not covered */
    IOP_UNION_SWITCH(&u) {
    }

    return 0;
}
