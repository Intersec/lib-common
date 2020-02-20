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
    RES=$(( RES + $? ))
}

main() {
    local script_dir

    script_dir=$(dirname "$(readlink -f "$0")")
    RES=0

    # TODO: run z_pxcc.py directly with ZFile
    run_test "python3 $script_dir/z_pxcc.py"

    return $RES
}

main
