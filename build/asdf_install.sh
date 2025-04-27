#!/bin/bash -eu
###########################################################################
#                                                                         #
# Copyright 2025 INTERSEC SA                                              #
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

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"

asdf_log() {
    echo -e "... $*" >&2
}

asdf_setup() {
    local asdf_tools_tpl="$1/.tool-versions-tpl.sh"
    local asdf_tools="$1/.tool-versions"
    local asdf_plugin

    # Resolve the template to generate .tool-versions if the template is
    # present
    if [[ -r "$asdf_tools_tpl" ]]; then
        # Export LIBCOMMON_DIR and REPO_DIR variables to the template.
        LIBCOMMON_DIR="$(dirname -- "$SCRIPT_DIR")"
        export LIBCOMMON_DIR
        REPO_DIR="$1"
        export REPO_DIR

        # Execute the template to generate .tool-versions file
        "$asdf_tools_tpl" > "$asdf_tools"
    fi

    # Avoid installing rust-docs which is huge.
    # See https://github.com/asdf-community/asdf-rust?tab=readme-ov-file#configuration
    export RUST_WITHOUT=rust-docs

    # Installing plugins
    asdf_log "installing ASDF plugins from '$asdf_tools'…"
    awk '/^[^#]/ {print $1}' "$asdf_tools" | \
    while IFS='' read -r asdf_plugin; do
        # Note: `asdf plugin add` returns 1 in case of error, 2 if the plugin
        # is already up to date and 0 in case of successful install/update.
        asdf plugin-add "$asdf_plugin" || [ $? != 1 ]
    done

    # Installing versions
    asdf_log "installing ASDF versions…"
    asdf install
}

asdf_setup "$1"
