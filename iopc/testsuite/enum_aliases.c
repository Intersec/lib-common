/* This test file is used to check that enum aliases are generated
 * correctly. (feature #52799) */

#include <lib-common/iop.h>
#include "attrs_valid-t.iop.h"


int main(void)
{
    attrs_valid__my_enum_b__t en;

    en = MY_ENUM_B_FOO;
    assert (en == MY_ENUM_B_A);
    en = MY_ENUM_B_BAR;
    assert (en == MY_ENUM_B_A);

    return 0;
}
