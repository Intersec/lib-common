#!/bin/bash
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

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"

USAGE="$(cat <<EOM
Usage: $0 [-h | --help] [--staged-files]
    -h | --help:    Print this help
    --staged-files: Process only the staged files
EOM
)"


run_cmd() {
    echo "---" "$@" "---"
    "$@"
}

modified_python_files() {
    local diff_mode="$1"
    local git_diff_files
    # Added, Copied, Modified, Renamed (we don't need to check deleted files
    # for instance).
    local diff_filter="--diff-filter=ACMR"
    if [ "$diff_mode" = "staged-files" ]; then
        # Get only the staged files
        git_diff_files="git diff-index --name-only $diff_filter --cached HEAD"
    else
        # By default, we check the last commit
        git_diff_files="git diff $diff_filter --name-only HEAD^"
    fi
    # Filter only the existing files and not the removed ones
    # Convert new lines into spaces for nice printing
    # With xargs || true, we ensure that if any file does not really exist we
    # skip outputting just that file.
    eval "$git_diff_files -- \
            '*.py' '**/*.py' '*.pyi' '**/*.pyi' 'wscript*' '**/wscript*' | \
            (xargs --no-run-if-empty stat --printf '%n ' 2>/dev/null || true)"
}

run_on_modified_python_files() {
    local cmd="$1"
    local modified_files="$2"

    # Run each command in parallel with xargs
    echo "$modified_files" | \
        xargs --no-run-if-empty -n 1 -P "$(nproc --all)" "$cmd"
}

main() {
    local params
    local modified_files
    local diff_mode="last-commit"
    local supported_commands="help,staged-files"

    # Go to the root directory of the repository
    cd "$SCRIPT_DIR"

    # Parse options
    params="$(getopt -o h -l "$supported_commands" --name "$0" -- "$@")"
    eval set -- "$params"

    while true; do
        case "$1" in
        -h|--help)
            echo "$USAGE"
            exit 0
            ;;
        --staged-files)
            diff_mode="staged-files"
            shift
            ;;
        --)
            # End of arguments
            shift
            break
            ;;
        *)
            echo "Not implemented: $1" >&2
            echo "$USAGE" >&2
            exit 1
            ;;
        esac
    done

    modified_files="$(modified_python_files "$diff_mode")"

    # If no python files are staged, do not run any linting tool
    if [[ -z "$modified_files" ]] && [[ "$diff_mode" = "staged-files" ]]; then
        return 0
    fi

    # Run the different commands
    run_cmd waf ruff
    run_cmd waf mypy
    run_cmd cargo clippy
    run_cmd cargo fmt --check
}

main "$@"
