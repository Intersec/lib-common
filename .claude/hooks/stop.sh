#!/bin/bash
###########################################################################
#                                                                         #
# Copyright 2026 INTERSEC SA                                              #
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

# Claude Code Stop hook: format modified files and run static checks.
# Triggered when Claude finishes its turn. If static checks fail, Claude
# is forced to continue and fix the reported errors (exit code 2).
set -uo pipefail

# Only run when explicitly enabled via environment.
if [[ "${IS_CLAUDE_STOP_HOOK:-}" != "1" ]]; then
    exit 0
fi

cd "$CLAUDE_PROJECT_DIR" || exit 0

# -- Collect modified (unstaged) files from git --
PYTHON_PATTERNS=('*.py' '**/*.py' '*.pyi' '**/*.pyi' 'wscript*' '**/wscript*')
RUST_PATTERNS=('*.rs' '**/*.rs')
C_PATTERNS=('*.c' '**/*.c' '*.h' '**/*.h' '*.blk' '**/*.blk')

get_modified_files() {
    local patterns=("$@")
    git diff --name-only --diff-filter=ACMR -- "${patterns[@]}" 2>/dev/null \
        | while read -r f; do [[ -f "$f" ]] && echo "$f"; done
}

# -- Format modified Python files --
mapfile -t py_files < <(get_modified_files "${PYTHON_PATTERNS[@]}")
if [[ ${#py_files[@]} -gt 0 ]]; then
    ruff check --fix --force-exclude "${py_files[@]}" || true
    ruff format --force-exclude "${py_files[@]}" || true
fi

# -- Format modified Rust files --
mapfile -t rs_files < <(get_modified_files "${RUST_PATTERNS[@]}")
for f in "${rs_files[@]}"; do
    rustfmt "$f" || true
done

# -- Format modified C files (*.c, *.h, *.blk) --
mapfile -t c_files < <(get_modified_files "${C_PATTERNS[@]}")
if [[ ${#c_files[@]} -gt 0 ]]; then
    clang-format -i "${c_files[@]}" || true
fi

# -- Run static checks; exit 2 on failure to force Claude to fix errors --
export FROM_AI_AGENT=1
OUTPUT=$(python3 static-checks.py --modified-files 2>&1)
STATUS=$?

if [[ $STATUS -ne 0 ]]; then
    echo "$OUTPUT" >&2
    exit 2
fi
