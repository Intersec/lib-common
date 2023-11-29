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

    if test -n "$match"; then
        echo "$(git describe --first-parent --match "$match"'*' 2>/dev/null || git rev-parse --short HEAD)${dirty}"
    else
        echo "$(git describe --first-parent 2>/dev/null || git rev-parse HEAD)${dirty}"
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

git_cut_version_column() {
    local version="$1"
    local column="$2"

    # We need to handle the case where the version can have a leading 0. In
    # this case, we would output 0X. In some case, it can be interpreted by
    # the compiler as an octal number instead of a decimal one (ex: 08).
    # So we remove any leading 0 in the verion number but still keep one if
    # the version would be empty otherwise.
    echo "$version" | cut -d '.' -f "$column" | sed -e 's/^0*//' -e 's/^$/0/'
}

git_product_version() {
    product="$1"
    tagversion="${2:-$product}"

    revision=$(git describe --match "$tagversion/*" 2>/dev/null || git rev-parse --short HEAD)${dirty}

    version=$(basename `(git describe --match "$tagversion/*" 2>/dev/null || echo "$tagversion/0.0.0") \
              | grep -E -o "[0-9]+\.[0-9]+\.[0-9]+"`)
    version_major=$(git_cut_version_column "$version" 1)
    version_minor=$(git_cut_version_column "$version" 2)
    version_patchlevel=$(git_cut_version_column "$version" 3)

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
