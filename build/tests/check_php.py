#!/usr/bin/env python3
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

"""
check_php is a small utility that will check syntax php to avoid
for example use of mix of php 5.4+ languages on 5.3:
please note that current php of buildbot is used so for centos 6, it will
check with php 5.3 and with centos 5.1, php 5.1 will be used.

The script will check PHP syntax in all subdirectories.
"""


import fnmatch
import os
import sys
from subprocess import STDOUT, CalledProcessError, check_output


def find_php_files(path: str) -> list[str]:
    php_files = []
    for dirname, _, filenames in os.walk(path):
        for filename in fnmatch.filter(filenames, '*.php'):
            php_files.append(os.path.join(dirname, filename))
    return php_files


def main() -> None:
    if len(sys.argv) > 1:
        root_dir = os.path.abspath(sys.argv[1])
    else:
        root_dir = os.getcwd()
    # convert to realpath (mix of symbolic link with abspath will fail later)
    root_dir = os.path.realpath(root_dir)
    os.chdir(root_dir)

    files = find_php_files(root_dir)

    print(f'1..{len(files)} Check PHP syntax')
    fail = 0
    for i, filename in enumerate(files, 1):
        try:
            check_output(['php', '-l', filename], stderr=STDOUT)
            print(f'{i} pass {filename}')
        except CalledProcessError as err:
            fail += 1
            print(f'{i} fail {filename}{err.output}')

    fail_percent = 100.0 * fail / len(files)
    print('# 0% skipped  '
          f'{int(100 - fail_percent)}% passed '
          f'{int(fail_percent)}% failed')


if __name__ == '__main__':
    main()
