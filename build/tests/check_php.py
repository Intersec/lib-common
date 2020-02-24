#!/usr/bin/env python3
# encoding: utf-8
###########################################################################
#                                                                         #
# Copyright 2019 INTERSEC SA                                              #
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


from subprocess import check_output, STDOUT, CalledProcessError
import os
import sys
import fnmatch


def find_php_files(path):
    php_files = []
    for dirname, _, filenames in os.walk(path):
        for filename in fnmatch.filter(filenames, '*.php'):
            php_files.append(os.path.join(dirname, filename))
    return php_files


def main():
    if len(sys.argv) > 1:
        root_dir = os.path.abspath(sys.argv[1])
    else:
        root_dir = os.getcwd()
    # convert to realpath (mix of symbolic link with abspath will fail later)
    root_dir = os.path.realpath(root_dir)
    os.chdir(root_dir)

    files = find_php_files(root_dir)

    print("1..{0} Check PHP syntax".format(len(files)))
    fail = 0
    for i, filename in enumerate(files, 1):
        try:
            check_output(['php', '-l', filename], stderr=STDOUT)
            print("{0} pass {1}".format(i, filename))
        except CalledProcessError as err:
            fail += 1
            print("{0} fail {1}{2}".format(i, filename, err.output))

    fail = 100.0 * fail / len(files)
    print("# 0% skipped  {0}% passed {1}% failed".format(
        int(100 - fail), int(fail)))

if __name__ == "__main__":
    main()
