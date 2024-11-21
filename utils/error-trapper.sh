#!/bin/bash -e
###########################################################################
#                                                                         #
# Copyright 2024 INTERSEC SA                                              #
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

# When you source this file, any command returning a non zero exit code
# will trigger a function to log useful information. Information such as
# line number, file name, exit code and what command failed.

# Read the file in the following link to see more advanced things you can do
# with bash trap:
# https://github.com/iptoux/bash_error_lib/blob/main/lib/bash_error_lib

echoerr() {
  echo "$@" 1>&2;
}

__error_trapper() {
  local code="$1"
  local commands="$2"

  echoerr "An error happened line $(caller). Exit code $code. The command is:"
  echoerr "$commands"
  exit "$code"
}

# With errtrace trap also works when commands fail inside functions.
set -o errtrace

# Anytime a command returns something else than zero the function
# __error_trapper is called.
trap '__error_trapper "$?" "$BASH_COMMAND"' ERR
