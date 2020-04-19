#!/bin/sh
###########################################################################
#                                                                         #
# Copyright 2020 INTERSEC SA                                              #
#                                                                         #
# Licensed under the Apache License, Version 2.0 (the "License");         #
# you may not use this file except in compliance with the License.        #
# You may obtain a copy of the License at                                 #
#                                                                         #
#     http://www.apache.org/licenses/LICENSE-2.0                          #
#                                                                         #
# Unless required by applicable law or agreed to in writing, software     #
# distributed under the License is distributed on an "AS IS" BASIS,       #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.#
# See the License for the specific language governing permissions and     #
# limitations under the License.                                          #
#                                                                         #
###########################################################################

set -e

flex -R -o $2 $1

sed -i -e 's/^extern int isatty.*;//' \
       -e '1s/^/#if ((__GNUC__ << 16) >= (8 << 16))\n#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"\n#endif\n/' \
       -e '1s/^/#if defined __clang__ || ((__GNUC__ << 16) + __GNUC_MINOR__ >= (4 << 16) +2)\n#pragma GCC diagnostic ignored "-Wsign-compare"\n#endif\n/' \
       -e '1s/^/#if defined __clang__ \&\& __clang_major__ >= 10\n#pragma GCC diagnostic ignored "-Wmisleading-indentation"\n#endif\n/' \
       -e 's/^\t\tint n; \\/            size_t n; \\/' \
       -e 's/^int .*get_column.*;//' \
       -e 's/^void .*set_column.*;//' \
       -e 's!$~$3.c+"!$(3:l=c)"!g' \
       $2
