#!/bin/bash -u
###########################################################################
#                                                                         #
# Copyright 2019 INTERSEC SA                                              #
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

RES=0

run_test() {
    echo
    echo "# $1"
    eval "$1"
    let "RES=$RES + $?"
}

main() {
    local tools_python_dir
    local waf_list

    tools_python_dir=$(dirname "$(readlink -f "$0")")
    cd "$tools_python_dir" || exit -1
    waf_list="$(waf list)"

    if grep -qs "iopy/python2/iopy" <<<"$waf_list" ; then
        run_test "python2 $tools_python_dir/z_iopy.py"
    fi

    if grep -qs "zchk-iopy-dso2" <<<"$waf_list" ; then
        run_test "$tools_python_dir/zchk-iopy-dso2"
    fi

    if grep -qs "iopy/python3/iopy" <<<"$waf_list" ; then
        run_test "python3 $tools_python_dir/z_iopy.py"
    fi

    if grep -qs "zchk-iopy-dso3" <<<"$waf_list" ; then
        run_test "$tools_python_dir/zchk-iopy-dso3"
    fi
}

main
exit $RES
