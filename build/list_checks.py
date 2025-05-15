#!/usr/bin/env python3
# -*- coding: utf-8 -*-
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
""" Dump the tests from ZFile

This script handles the ZFile content. The output of this script is the
directory based the path argument and the test specified in the Zfile
(ex. "./platform/qkv/ zchk-store").

Some filters are applied based on Z_SKIP_PATH and Z_LIST_SKIP:
 * Z_SKIP_PATH removes the research of ZFile in these paths
 * Z_LIST_SKIP removes a kind of test.
"""

import sys
import os
import re

from collections.abc import Iterator
from typing import Optional


def is_file(f: str) -> bool:
    return os.path.isfile(f)


def is_exec(f: str) -> bool:
    return is_file(f) and os.access(f, os.X_OK)


GROUPS = [
    ("behave", re.compile(r".*/behave"),     None),
    ("web",    re.compile(r".*testem.json"), is_file),
    ("web",    re.compile(r".*/check_php"),  None),
    ("C",      re.compile(r".+"),            is_exec)  # default case
]
RE_TAGS = re.compile(r"@([A-Za-z0-9_]+)")
Z_TAG_SKIP = set(os.getenv("Z_TAG_SKIP", "").split())


def dump_zfile(
        zfile: str,
        skipped_groups: list[str]
) -> Iterator[tuple[str, str, Optional[str]]]:
    folder = os.path.dirname(zfile)

    with open(zfile, 'r') as zfile_fd:
        for num, line in enumerate(zfile_fd):
            stripped_line = line.strip()
            test = stripped_line.split()[0]
            test_path = os.path.join(folder, test)

            if stripped_line.startswith('#'):
                continue

            if set(RE_TAGS.findall(stripped_line)) & Z_TAG_SKIP:
                continue

            for group, regex, check in GROUPS:
                if group not in skipped_groups and regex.match(test_path):
                    err = None

                    if check and not check(test_path):
                        err = "%s:%d: no match for %s" % (zfile, num + 1,
                                                          stripped_line)

                    yield (folder, test, err)
                    break


def fetch_zfiles(root: str) -> Iterator[str]:
    paths = os.getenv('Z_SKIP_PATH', None)
    skip = None

    if paths:
        skip = re.compile(paths)

    for path, _, files in os.walk(root):
        for f in files:
            if skip and skip.match(path):
                continue
            if f == 'ZFile':
                yield os.path.join(path, f)


def main(root: str) -> None:
    exit_code = 0
    skipped_groups = os.getenv('Z_LIST_SKIP', "").split()

    for zfile in fetch_zfiles(root):
        for folder, line, err in dump_zfile(zfile, skipped_groups):
            if err:
                sys.stderr.write("ERROR: %s\n" % err)
                sys.stderr.flush()
                exit_code = 2
            else:
                print("%s/ %s" % (folder, line))
                sys.stdout.flush()

    sys.exit(exit_code)


if __name__ == "__main__":
    assert len(sys.argv) == 2
    main(sys.argv[1])
