#!/bin/bash -e
###########################################################################
#                                                                         #
# Copyright 2022 INTERSEC SA                                              #
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

SCRIPTDIR=$(dirname "$(readlink -f "$0")")
NODE=$(which node || which nodejs)

test -z $NODE && {
    echo "nodejs is required, apt-get install nodejs or http://nodejs.org" >&2
    exit 1
}

clean_up() {
    errcode=$?
    pkill -f "$(basename $NODE).*phantomjs";
    exit $errcode
}

trap clean_up ERR SIGHUP SIGINT SIGTERM

$NODE $SCRIPTDIR/ztestem.js $@
exit $?
