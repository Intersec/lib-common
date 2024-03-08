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
import os
import shutil
import subprocess

from pathlib import Path
from typing import Optional


# {{{ Add poetry to sys path


# pylint: disable=unsubscriptable-object
def str2bool(value: Optional[str]) -> bool:
    if not value:
        return False
    return value.lower() in ['true', 'yes', '1']


POETRY_DEBUG = str2bool(os.environ.get('POETRY_DEBUG'))


def add_poetry_to_sys_path() -> None:
    """Add Poetry library to sys path

    Adapted from cloud-custodian
    https://github.com/cloud-custodian/cloud-custodian/blob/
        284a71a6f178caac136485b715648540e492d342/tools/dev/poetrypkg.py#L31
    """

    # if we're using poetry from git, have a flag to prevent the user
    # installed one from getting precedence.
    if POETRY_DEBUG:
        return

    # If there is a local installation of poetry in ~/.poetry, prefer that.
    poetry_python_lib = Path(os.path.expanduser('~/.poetry/lib'))
    if poetry_python_lib.exists():
        sys.path.insert(0, os.path.realpath(poetry_python_lib))
        # poetry env vendored deps
        sys.path.insert(
            0,
            os.path.join(
                poetry_python_lib,
                'poetry',
                '_vendor',
                'py{}.{}'.format(sys.version_info.major,
                                 sys.version_info.minor),
            ),
        )
        return

    # If there is a local installation of poetry in ~/.local, prefer that.
    cur_poetry_python_lib = Path(os.path.expanduser(
        '~/.local/share/pypoetry/venv/lib'))
    if cur_poetry_python_lib.exists():
        sys.path.insert(
            0, str(list(cur_poetry_python_lib.glob('*'))[0] / "site-packages")
        )
        return

    # Look for the global installation of poetry
    global_poetry_bin = shutil.which('poetry')
    global_poetry_bin = Path(os.path.realpath(global_poetry_bin))
    global_poetry_root = global_poetry_bin.parents[1]
    global_poetry_glob_sites = list(global_poetry_root.glob(
        'lib*/*/site-packages'))
    if global_poetry_glob_sites:
        for site in global_poetry_glob_sites:
            sys.path.insert(0, str(site))
        return

    # Look for ASDF installation of poetry
    if os.environ['ASDF_DIR']:
        asdf_which = subprocess.run(['asdf', 'which', 'poetry'],
                                    stdout=subprocess.PIPE, check=True)
        asdf_poetry_bin = asdf_which.stdout.decode('utf-8').strip()
        asdf_poetry_bin = Path(os.path.realpath(asdf_poetry_bin))
        asdf_poetry_root = asdf_poetry_bin.parents[1]
        asdf_poetry_glob_sites = list(asdf_poetry_root.glob(
            'lib*/*/site-packages'))
        if asdf_poetry_glob_sites:
            for site in asdf_poetry_glob_sites:
                sys.path.insert(0, str(site))
            return

    raise RuntimeError('unable to find poetry library directory')


# }}}
