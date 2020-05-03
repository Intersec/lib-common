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

# Variant of version.sh which generates fakes (and thus constant) versions.
# This is used by the FAKE_VERSIONS mode of waf.

git_rcsid() {
    cat <<EOF
char const $1_id[] =
    "\$Intersec: $1 fake-revision \$";
char const $1_git_revision[] = "$1-fake-revision";
char const $1_git_sha1[] = "$1-fake-sha1";
EOF
}

git_product_version() {
    product="$1"
    cat <<EOF
const char ${product}_git_revision[] = "${product}-fake-revision";
const unsigned ${product}_version_major = 0;
const unsigned ${product}_version_minor = 0;
const unsigned ${product}_version_patchlevel = 0;
const char ${product}_version[] = "${product}-fake-version";
EOF
}

case "$1" in
    "rcsid")           shift; git_rcsid           "$@";;
    "product-version") shift; git_product_version "$@";;
    *);;
esac
