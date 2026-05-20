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
Splice shared Rust workspace fragments into a Cargo.toml.

Each fragment owns a named region in the target Cargo.toml delimited by
marker pairs. The script:

  1. Reads each fragment file (in the script's directory) and extracts
     the lines between its `# <fragment>` / `# </fragment>` markers.
  2. Replaces the lines between the matching
     `# <include shared:<name>>` / `# </include shared:<name>>` markers
     in the target Cargo.toml.

Run with --check to fail (non-zero exit, diff on stdout) when the target
is out of sync, without modifying anything. static-checks.py uses this
mode to enforce sync in CI.
"""

import argparse
import difflib
import os
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

# (fragment file name, region name in consumer Cargo.toml).
FRAGMENTS = [
    ('workspace-profiles.toml', 'shared:profiles'),
    ('workspace-lints.toml', 'shared:lints'),
]

FRAGMENT_BEGIN = '# <fragment>'
FRAGMENT_END = '# </fragment>'


def extract_fragment(path: Path) -> str:
    """Return the lines between `# >>> fragment` and `# <<< fragment`."""
    text = path.read_text()
    try:
        _, body = text.split(FRAGMENT_BEGIN + '\n', 1)
        body, _ = body.split(FRAGMENT_END, 1)
    except ValueError:
        sys.exit(
            f'{path}: missing `{FRAGMENT_BEGIN}` / `{FRAGMENT_END}` markers'
        )
    return body.rstrip('\n')


def splice(text: str, region: str, fragment: str, source: Path) -> str:
    """Replace the region content in `text` with `fragment`."""
    begin = f'# <include {region}>'
    end = f'# </include {region}>'
    try:
        before, rest = text.split(begin + '\n', 1)
        _, after = rest.split(end, 1)
    except ValueError:
        sys.exit(
            f'target is missing `{begin}` / `{end}` markers '
            f'(synced from {source})'
        )
    header = (
        f'# Synced from {source}. Edit there, then re-run '
        f'sync-cargo-shared.py.\n'
    )
    return f'{before}{begin}\n{header}{fragment}\n{end}{after}'


def render(target: Path) -> str:
    text = target.read_text()
    for filename, region in FRAGMENTS:
        source = SCRIPT_DIR / filename
        fragment = extract_fragment(source)
        # Show the source path relative to the target's directory so the
        # comment is correct in both lib-common and downstream consumers
        # (where the file is reached via the submodule path).
        rel_source = Path(os.path.relpath(source, target.resolve().parent))
        text = splice(text, region, fragment, rel_source)
    return text


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        'cargo_toml',
        type=Path,
        help='Path to the Cargo.toml to update.',
    )
    parser.add_argument(
        '--check',
        action='store_true',
        help='Exit non-zero on drift; do not modify the file.',
    )
    args = parser.parse_args()

    target = args.cargo_toml.resolve()
    current = target.read_text()
    updated = render(target)

    if current == updated:
        return 0

    if args.check:
        diff = difflib.unified_diff(
            current.splitlines(keepends=True),
            updated.splitlines(keepends=True),
            fromfile=str(args.cargo_toml),
            tofile=f'{args.cargo_toml} (expected)',
        )
        sys.stdout.writelines(diff)
        print(
            f'\n{args.cargo_toml} is out of sync with the shared '
            f'workspace fragments. Re-run without --check to update.',
            file=sys.stderr,
        )
        return 1

    target.write_text(updated)
    print(f'updated {args.cargo_toml}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
