#!/usr/bin/env python3
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
"""
Dump the tests to retry from a dedicated environment variable

This script handles the retry requested by buildbot the same way the
list_checks.py does. This way, we ensure the main script used to launch tests
stays the same.
"""

import os
import sys
from collections import defaultdict


def main() -> int:
    retry_env_variable = 'Z_LIST_RETRY'
    retry_string = os.getenv(retry_env_variable)

    retry_tests = []
    if retry_string and retry_string != '':
        retry_tests = retry_string.split(',')

    if not retry_tests:
        print(f'ERROR: no tests provided in {retry_env_variable} environment '
              'variable or variable is empty',
              file=sys.stderr)
        return 2

    final_tests = defaultdict(list)
    for test in retry_tests:
        parts = test.split(os.path.sep)
        try:
            # Catch the 'ci' folder in XXX.feature path, so we are able to
            # rebuild the main behave test suite
            position = parts.index('ci')
            behave_test = os.path.sep.join(
                ['retry-behave'] + parts[:position])
            final_tests[behave_test].append(test)
        except ValueError:
            final_tests[test] = []

    for test_cat, test_values in final_tests.items():
        print(test_cat + ' ' + ' '.join(test_values))
    return 0


if __name__ == '__main__':
    # Expect no arguments, as all tests to retry are passed through an
    # environment variable.
    assert len(sys.argv) < 2 or not sys.argv[1]
    sys.exit(main())
