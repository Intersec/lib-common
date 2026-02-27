#!/usr/bin/env python3
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

"""
Static checks script.

Runs linters (ruff, mypy, ast-grep) on either the full codebase or only
on files staged for commit.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

PYTHON_PATTERNS = (
    '*.py',
    '**/*.py',
    '*.pyi',
    '**/*.pyi',
    'wscript*',
    '**/wscript*',
)


def run_cmd(*args: str) -> None:
    """Run a command, printing it before execution. Abort on failure."""
    print('---', *args, '---')
    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        print(
            f"An error happened. Exit code {result.returncode}. "
            f"The command is:\n{' '.join(args)}",
            file=sys.stderr,
        )
        sys.exit(result.returncode)


def get_staged_files(
    file_patterns: tuple[str, ...] = ('*',),
) -> list[str]:
    """
    Return the list of staged added/copied/modified/renamed files.

    Non-existent files (e.g. deleted between the diff and now) are silently
    skipped.
    """
    cmd = [
        'git', 'diff-index', '--name-only',
        '--diff-filter=ACMR', '--cached', 'HEAD',
        '--',
    ]
    cmd.extend(file_patterns)

    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return []

    # Keep only files that actually exist on disk.
    return [
        name for name in result.stdout.splitlines()
        if name and Path(name).exists()
    ]


def get_staged_python_files() -> list[str]:
    """Return staged Python files (including wscript files)."""
    return get_staged_files(PYTHON_PATTERNS)


def run_ast_grep_checks(modified_files: list[str]) -> None:
    """Run ast-grep rule tests if any ast-grep config file was modified."""
    if any('ast-grep/' in f for f in modified_files):
        run_cmd('ast-grep', 'test')


def main() -> None:
    parser = argparse.ArgumentParser(description='Run static checks.')
    parser.add_argument(
        '--staged-files',
        action='store_true',
        help='Process only the staged files',
    )
    args = parser.parse_args()

    # Go to the root directory of the repository.
    os.chdir(SCRIPT_DIR)

    if args.staged_files:
        # This mode is used by our git hook.
        all_staged_files = get_staged_files()
        staged_python_files = get_staged_python_files()

        if staged_python_files:
            # Only run linters on staged files, otherwise it will run on
            # files modified locally but not staged.
            run_cmd(
                'ruff', 'check', '--force-exclude',
                *staged_python_files,
            )
            run_cmd('waf', 'mypy', *staged_python_files)

        if all_staged_files:
            run_cmd('ast-grep', 'scan', *all_staged_files)
            # `test` checks the rules of ast-grep against the tests provided.
            # It does not check the code.
            run_ast_grep_checks(all_staged_files)
    else:
        # The bot executes static-checks without setting any diff_mode.
        # Run the linters on the entire codebase.
        run_cmd('waf', 'ruff')
        run_cmd('waf', 'mypy')
        run_cmd('ast-grep', 'scan')
        run_cmd('ast-grep', 'test')

    # Cargo clippy and fmt only work for the entire codebase
    run_cmd('cargo', 'clippy', '--tests')
    run_cmd('cargo', 'fmt', '--check')


if __name__ == '__main__':
    main()
