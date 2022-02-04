#!/bin/sh
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

dirty=$(git diff-files --quiet && git diff-index --cached --quiet HEAD -- || echo "-dirty")

git_describe() {
    match="$1"
    # the first-parent option is only available since git 1.8
    parent=$(git describe -h 2>&1 | grep 'first-parent' | awk '{ print $1; }')

    if test -n "$match"; then
        echo "$(git describe $parent --match "$match"'*' 2>/dev/null || git rev-parse --short HEAD)${dirty}"
    else
        echo "$(git describe $parent 2>/dev/null || git rev-parse HEAD)${dirty}"
    fi
}

git_rcsid() {
    revision=$(git_describe)
    sha1="$(git rev-parse HEAD)${dirty}"
    cat <<EOF
char const $1_id[] =
    "\$Intersec: $1 $revision \$";
char const $1_git_revision[] = "$revision";
char const $1_git_sha1[] = "$sha1";
EOF
}

git_product_version() {
    product="$1"
    tagversion="${2:-$product}"

    revision=$(git describe --match "$tagversion/*" 2>/dev/null || git rev-parse --short HEAD)${dirty}

    version=$(basename `(git describe --match "$tagversion/*" 2>/dev/null || echo "$tagversion/0.0.0") \
              | grep -E -o "[0-9]+\.[0-9]+\.[0-9]+"`)
    version_major=$(echo $version |cut -d '.' -f 1)
    version_minor=$(echo $version |cut -d '.' -f 2)
    version_patchlevel=$(echo $version |cut -d '.' -f 3)

    cat <<EOF
const char ${product}_git_revision[] = "$revision";
const unsigned ${product}_version_major = $version_major;
const unsigned ${product}_version_minor = $version_minor;
const unsigned ${product}_version_patchlevel = $version_patchlevel;
const char ${product}_version[] = "$version";
EOF
}

case "$1" in
    "describe")        shift; git_describe        "$@";;
    "rcsid")           shift; git_rcsid           "$@";;
    "product-version") shift; git_product_version "$@";;
    *);;
esac
