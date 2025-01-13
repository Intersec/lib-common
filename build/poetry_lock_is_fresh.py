#!/usr/bin/env python3
###########################################################################
#                                                                         #
# Copyright 2024 INTERSEC SA                                              #
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

import sys
import argparse

from pathlib import Path

from poetry_helpers import add_poetry_to_sys_path

from typing import cast


DESCRIPTION = """
Check if the poetry.lock file is fresh compared to the pyproject.toml.

Returns 0 if the poetry.lock is fresh, 1 is not.
"""


def is_poetry_lock_fresh(repo_dir: Path) -> bool:
    # pylint: disable=import-outside-toplevel, import-error
    from poetry.factory import Factory # type: ignore[import-not-found]

    factory = Factory()
    poetry = factory.create_poetry(str(repo_dir))

    return cast(bool, poetry.locker.is_fresh())


def main() -> None:
    # Poetry to sys path
    add_poetry_to_sys_path()

    # Arguments
    parser = argparse.ArgumentParser(
        description=DESCRIPTION,
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument('repo_dir', type=Path, help=(
        'The repository directory containing the poetry.lock file'
    ))
    args = parser.parse_args()

    # Check if the poetry lock is fresh, exit 0 if it is, 1 if not.
    res = is_poetry_lock_fresh(args.repo_dir)
    sys.exit(0 if res else 1)


if __name__ == '__main__':
    main()
