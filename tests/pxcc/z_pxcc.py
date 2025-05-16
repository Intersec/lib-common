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

import difflib
import os.path as osp

import zchk_mod  # type: ignore

import zpycore as z

DIR = osp.dirname(osp.realpath(__file__))


@z.ZGroup
class PxccTests(z.TestCase):
    def test_cubic(self) -> None:
        self.assertEqual(zchk_mod.cubic(4), 4**3)

    def test_compare_output_file(self) -> None:
        expected_file_name = 'zchk_cmod_pxc_expected.pxd'
        output_file_name = 'zchk_cmod_pxc.pxd'
        expected_file_path = osp.join(DIR, expected_file_name)
        output_file_path = osp.join(DIR, output_file_name)
        with open(expected_file_path, 'r') as expected_file:
            with open(output_file_path, 'r') as output_file:
                diff_list = difflib.unified_diff(
                    expected_file.readlines(),
                    output_file.readlines(),
                    fromfile=expected_file_name,
                    tofile=output_file_name,
                    lineterm='')
                diff = '\n'.join(line.rstrip('\n') for line in diff_list)

        self.assertTrue(len(diff) == 0,
                        f'content of `{expected_file_name}` is not equal '
                        f'to content of `{output_file_name}`:\n{diff}',
                        )


if __name__ == "__main__":
    z.main()
