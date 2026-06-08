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

Runs linters (ruff, pyrefly, ast-grep, clang-format) on either the full
codebase or only on files staged for commit / unstaged modified files.
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

C_PATTERNS = (
    '*.c',
    '**/*.c',
    '*.h',
    '**/*.h',
    '*.blk',
    '**/*.blk',
)


def run_cmd(*args: str) -> None:
    """Run a command, printing it before execution. Abort on failure."""
    print('---', *args, '---')
    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        print(
            f'An error happened. Exit code {result.returncode}. '
            f'The command is:\n{" ".join(args)}',
            file=sys.stderr,
        )
        sys.exit(result.returncode)


def get_git_diff_files(
    diff_mode: str,
    file_patterns: tuple[str, ...] = ('*',),
) -> list[str]:
    """
    Return the list of added/copied/modified/renamed files.

    In ``staged-files`` mode, only staged files are returned.
    In ``modified-files`` mode, only unstaged modified files are returned.

    Non-existent files (e.g. deleted between the diff and now) are silently
    skipped.
    """
    if diff_mode == 'staged-files':
        cmd = [
            'git',
            'diff-index',
            '--name-only',
            '--diff-filter=ACMR',
            '--cached',
            'HEAD',
            '--',
        ]
    elif diff_mode == 'modified-files':
        cmd = [
            'git',
            'diff',
            '--name-only',
            '--diff-filter=ACMR',
            '--',
        ]
    else:
        raise ValueError(f'Unknown diff_mode: {diff_mode}')

    cmd.extend(file_patterns)

    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return []

    # Keep only files that actually exist on disk.
    return [
        name
        for name in result.stdout.splitlines()
        if name and Path(name).exists()
    ]


def get_all_files(file_patterns: tuple[str, ...] = ('*',)) -> list[str]:
    """Return all tracked files matching the given patterns."""
    cmd = ['git', 'ls-files', '--', *file_patterns]

    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return []

    # Keep only files that actually exist on disk.
    return [
        name
        for name in result.stdout.splitlines()
        if name and Path(name).exists()
    ]


def get_modified_python_files(diff_mode: str) -> list[str]:
    """Return modified Python files (including wscript files)."""
    return get_git_diff_files(diff_mode, PYTHON_PATTERNS)


def get_modified_c_files(diff_mode: str) -> list[str]:
    """Return modified C files (*.c, *.h, *.blk)."""
    return get_git_diff_files(diff_mode, C_PATTERNS)


def run_ast_grep_checks(modified_files: list[str]) -> None:
    """Run ast-grep rule tests if any ast-grep config file was modified."""
    if any('ast-grep/' in f for f in modified_files):
        run_cmd('ast-grep', 'test')


def run_clang_format_checks(c_files: list[str]) -> None:
    """Fail if any C file is not properly formatted with clang-format."""
    if c_files:
        run_cmd('clang-format', '--dry-run', '--Werror', *c_files)


def main() -> None:
    parser = argparse.ArgumentParser(description='Run static checks.')
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        '--staged-files',
        action='store_true',
        help='Process only the staged files',
    )
    group.add_argument(
        '--modified-files',
        action='store_true',
        help='Process only the modified (unstaged) files',
    )
    args = parser.parse_args()

    # Go to the root directory of the repository.
    os.chdir(SCRIPT_DIR)

    if args.staged_files:
        diff_mode = 'staged-files'
    elif args.modified_files:
        diff_mode = 'modified-files'
    else:
        diff_mode = None

    if diff_mode is not None:
        # This mode is used by our git hook.
        all_modified_files = get_git_diff_files(diff_mode)
        python_modified_files = get_modified_python_files(diff_mode)
        c_modified_files = get_modified_c_files(diff_mode)

        if python_modified_files:
            # Only run linters on modified files, otherwise it will run on
            # files modified locally but not staged.
            run_cmd(
                'ruff',
                'check',
                '--force-exclude',
                *python_modified_files,
            )
            run_cmd(
                'ruff',
                'format',
                '--check',
                '--force-exclude',
                *python_modified_files,
            )
            run_cmd('pyrefly', 'check', *python_modified_files)

        if c_modified_files:
            run_clang_format_checks(c_modified_files)

        if all_modified_files:
            run_cmd('ast-grep', 'scan', *all_modified_files)
            # `test` checks the rules of ast-grep against the tests provided.
            # It does not check the code.
            run_ast_grep_checks(all_modified_files)
    else:
        # The bot executes static-checks without setting any diff_mode.
        # Run the linters on the entire codebase.
        run_cmd('waf', 'ruff')
        run_cmd('ruff', 'format', '--check')
        run_cmd('waf', 'pyrefly')
        run_cmd('ast-grep', 'scan')
        run_cmd('ast-grep', 'test')
        run_clang_format_checks(get_all_files(C_PATTERNS))

    # Check that Cargo.toml's shared profile/lint regions match the
    # canonical fragments in build/rust/.
    run_cmd(
        sys.executable,
        str(SCRIPT_DIR / 'build' / 'rust' / 'sync-cargo-shared.py'),
        '--check',
        str(SCRIPT_DIR / 'Cargo.toml'),
    )

    # Cargo clippy and fmt only work for the entire codebase
    run_cmd('cargo', 'clippy', '--tests')
    run_cmd('cargo', 'fmt', '--check')


if __name__ == '__main__':
    main()
