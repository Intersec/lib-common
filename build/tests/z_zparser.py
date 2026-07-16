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
Focused zparser unit tests: current parser behaviour.

Each test pins one observable of the stream parser on short synthetic
z-format fragments; no captured logs. These lock the behaviour the
rd-infra consumers depend on today (camelCase Error names, `timeout`
end-of-log flag, full_name identity, CLI contract, "# TOTAL" stop) so
the upcoming parser reworks can be reviewed against a green baseline.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile

SELF_PATH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(SELF_PATH, '..', '..'))
sys.path.insert(0, SELF_PATH)

import zparser  # noqa: E402

import zpycore as z  # noqa: E402

ARGV_RETRY = "argv: [b'waf', b'check-retry']"
ARGV_CHECK = "argv: [b'waf', b'check']"
SUITE = 'starting suite prod/behave...'
GROUP = '1..2 G'
PASS1 = '1 pass t1   # (0.10s)'
PASS2 = '2 pass t2   # (0.10s)'
FAIL2 = '2 fail t2   # (0.20s)'
STEP_FAIL = '#  2-1  fail <given x> f.feature:3    # (0.010s)'
SUITE_FAILED = 'TEST SUITE prod/behave FAILED (3 seconds)'
DONE = 'done (3 seconds)'
TOTAL = '# TOTAL'


def parse(lines: list[str]) -> zparser.Global:
    sp = zparser.StreamParser()
    for line in lines:
        sp.parse_line(line)
    return sp.gen_report()


@z.ZGroup
class ZParserCompatTest(z.TestCase):
    # CT1
    def test_timeout_true_without_total(self) -> None:
        rep = parse([SUITE, GROUP, PASS1, PASS2, DONE])
        self.assertIs(rep.timeout, True)

    # CT2
    def test_timeout_cleared_by_total(self) -> None:
        rep = parse([SUITE, GROUP, PASS1, PASS2, DONE, TOTAL])
        self.assertIs(rep.timeout, False)

    # CT3
    def test_error_name_values(self) -> None:
        rep = parse([SUITE, GROUP, PASS1, FAIL2, SUITE_FAILED, TOTAL])
        self.assertEqual(len(rep.errors), 1)
        err = rep.errors[0]
        self.assertEqual(err.productName, 'prod')
        self.assertEqual(err.suiteName, 'behave')
        self.assertEqual(err.groupName, 'G')
        # trailing spaces are part of the production value; consumers
        # rstrip when they need a key (e.g. RFA on full_name)
        self.assertEqual(err.testName, 't2  ')

    # CT4
    def test_test_filename(self) -> None:
        rep = parse(
            [SUITE, GROUP, PASS1, STEP_FAIL, FAIL2, SUITE_FAILED, TOTAL]
        )
        err = rep.errors[0]
        self.assertEqual(err.test_filename, 'f.feature')

    # CT5
    def test_step_kind_and_retry(self) -> None:
        rep = parse([ARGV_RETRY, SUITE, GROUP, PASS1, PASS2, DONE, TOTAL])
        self.assertEqual(rep.step_kind, 'check-retry')
        self.assertIs(rep.retry, True)

        rep = parse([ARGV_CHECK, SUITE, GROUP, PASS1, PASS2, DONE, TOTAL])
        self.assertEqual(rep.step_kind, 'check')
        self.assertIs(rep.retry, False)

    # CT6
    def test_cli_contract(self) -> None:
        zparser_py = os.path.join(SELF_PATH, 'zparser.py')

        def run_cli(lines: list[str]) -> subprocess.CompletedProcess[bytes]:
            with tempfile.NamedTemporaryFile(
                'w', suffix='.log', delete=False
            ) as f:
                f.write('\n'.join(lines) + '\n')
                path = f.name
            try:
                return subprocess.run(
                    [sys.executable, zparser_py, path],
                    check=False,
                    capture_output=True,
                )
            finally:
                os.unlink(path)

        clean = [SUITE, GROUP, PASS1, PASS2, DONE, TOTAL]
        res1 = run_cli(clean)
        res2 = run_cli(clean)
        self.assertEqual(res1.returncode, 0)
        self.assertIn(b'# TOTAL', res1.stdout)
        self.assertEqual(res1.stdout, res2.stdout)

        failed = run_cli([SUITE, GROUP, PASS1, FAIL2, SUITE_FAILED, TOTAL])
        self.assertEqual(failed.returncode, 255)
        self.assertIn(b': ERRORS', failed.stdout)

    # CT7
    def test_full_name_format(self) -> None:
        rep = parse([SUITE, GROUP, PASS1, FAIL2, SUITE_FAILED, TOTAL])
        # exact production value, including the trailing whitespace
        # inherited from testName: this string is the BugsDb/RFA identity
        self.assertEqual(rep.errors[0].full_name, 'prod → behave → G → t2  ')

    # CT8
    def test_total_stops_parsing(self) -> None:
        rep = parse(
            [
                SUITE,
                GROUP,
                PASS1,
                PASS2,
                DONE,
                TOTAL,
                'starting suite prod/other...',
                '1..1 X',
                'Core was generated by ignored',
                '1 fail late   # (0.10s)',
            ]
        )
        self.assertEqual(len(rep.products), 1)
        self.assertEqual(len(rep.products['prod'].suites), 1)
        self.assertIs(rep.core, False)
        self.assertEqual(len(rep.errors), 0)


if __name__ == '__main__':
    z.main()
