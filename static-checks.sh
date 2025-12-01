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

source ./utils/error-trapper.sh
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

staged_python_files() {
    # Added, Copied, Modified, Renamed (we don't need to check deleted files
    # for instance).
    local diff_filter="--diff-filter=ACMR"
    # Filter only the existing files and not the removed ones
    # Convert new lines into spaces for nice printing
    # With xargs || true, we ensure that if any file does not really exist we
    # skip outputting just that file.
    git diff-index --name-only $diff_filter --cached HEAD -- \
        '*.py' '**/*.py' '*.pyi' '**/*.pyi' 'wscript*' '**/wscript*' | \
        (xargs --no-run-if-empty stat --printf '%n ' 2>/dev/null || true)
}

main() {
    local params
    local staged_files
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

    if [[ "$diff_mode" = "staged-files" ]]; then
        # This diff_mode is used by our git hook.
        staged_files="$(staged_python_files)"

        if [[ -n "$staged_files" ]]; then
            # Only run linters on staged_files, otherwise it will run on
            # file modified locally but not staged.

            # Intended splitting of staged_files
            # shellcheck disable=SC2086
            run_cmd ruff check --force-exclude ${staged_files}
            # shellcheck disable=SC2086
            run_cmd waf mypy ${staged_files}
        fi
    else
        # The bot executes static-check without setting any diff_mode.

        # Run the linters on the entire codebase
        run_cmd waf ruff
        run_cmd waf mypy
    fi

    # Cargo clippy and fmt only work for the entire codebase
    run_cmd cargo clippy
    run_cmd cargo fmt --check
}

main "$@"
