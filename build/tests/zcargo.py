#!/usr/bin/env python3
###########################################################################
#                                                                         #
# Copyright 2026 INTERSEC SA                                              #
#                                                                         #
# Licensed under the Apache License, Version 2.0 (the 'License');         #
# you may not use this file except in compliance with the License.        #
# You may obtain a copy of the License at                                 #
#                                                                         #
#     http://www.apache.org/licenses/LICENSE-2.0                          #
#                                                                         #
# Unless required by applicable law or agreed to in writing, software     #
# distributed under the License is distributed on an 'AS IS' BASIS,       #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.#
# See the License for the specific language governing permissions and     #
# limitations under the License.                                          #
#                                                                         #
###########################################################################

"""
A wrapper to launch cargo test and convert its human-readable output to our
Z format.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

RE_ANSI = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

# Examples of a line we want to match:
# Running unittests zcargo.rs (/.cargo/target/debug/deps/zcargo-7838fa10588bc85e)  # noqa: E501
RE_TESTS_INTRO = re.compile(
    r'^\s*Running unittests (?P<file>[^ ]+)\s*\((?P<path>[^)]+)\)\s*$')

# Examples of a line we want to match:
# running 0 tests
# running 1 test
# running 5 tests
RE_SUITE_INTRO = re.compile(r'^\s*running\s+(?P<planned>\d+)\s+tests?\s*$')

# Examples of a line we want to match:
# test tests::should_skip ... ignored, example of ignored test
# test should_pass ... ok
# test should_fail - should panic ... ok
# test tests::should_pass ... ok
# test tests::should_fail - should panic ... ok
# test should_fail - should panic ... FAILED
# test tests::should_skip ... ignored, example of ignored test
# test should_pass ... FAILED
# test tests::should_pass ... FAILED
# test tests::should_fail - should panic ... FAILED
RE_TEST_STATUS = re.compile(
    r'^\s*test\s+(?P<name>[^ -]+)\s(?P<notice>-.*)?\s*\.\.\.\s*'
    r'(?P<status>ok|FAILED|ignored),?\s*(?P<comment>.*)?\s*$')


# Examples of a line we want to match:
# ---- should_fail stdout ----
# ---- should_pass stdout ----
RE_TEST_STDOUT = re.compile(
    r'^\s*----\s*(?P<name>[^ ]+)\s+stdout\s*----\s*$')


# Examples of a line we want to match:
# test result: ok. 4 passed; 0 failed; 1 ignored; 0 measured; 0 filtered out; finished in 0.00s  # noqa: E501
# test result: FAILED. 0 passed; 4 failed; 1 ignored; 0 measured; 0 filtered out; finished in 0.00s  # noqa: E501
RE_SUITE_RESULT = re.compile(
    r'^test result: (?P<status>ok|FAILED)\. (?P<passed>\d+) passed; '
    r'(?P<failed>\d+) failed; (?P<ignored>\d+) ignored; '
    r'(?P<measured>\d+) measured; (?P<filtered>\d+) '
    r'filtered out; finished in (?P<duration>[^ ]+)s$')

# {{{ helpers


def strip_ansi(line: str) -> str:
    return RE_ANSI.sub('', line)


class TestStatus:
    __slots__ = ('index', 'name', 'status', 'stdout')

    def __init__(self, index: int, name: str, status: str):
        self.name = name
        self.status = status.lower()
        self.stdout: list[str] = []
        self.index = index

    def get_z_status(self) -> str:
        if self.status == 'ok':
            return 'pass'
        if self.status == 'failed':
            return 'fail'
        if self.status == 'ignored':
            return 'skip'

        raise ValueError(f'unknown status: {self.status}')

    def add_stdout(self, line: str) -> None:
        self.stdout.append(line.rstrip('\n'))

    def get_z_output(self) -> str:
        return '\n:'.join(
            (
                f'{self.index} {self.get_z_status()} {self.name}',
                *self.stdout,
            ),
        )


class TestSuite:
    __slots__ = (
        'duration',
        'failed',
        'filtered',
        'ignored',
        'measured',
        'name',
        'parse_state',
        'passed',
        'planned',
        'status',
        'test_with_open_stdout',
        'tests',
    )

    def __init__(self, name: str, planned: int):
        self.name = name
        self.planned = planned
        self.tests: dict[str, TestStatus] = {}
        self.parse_state = 'STARTED'
        self.test_with_open_stdout: TestStatus | None = None

    def add_test(self, test: TestStatus) -> None:
        assert test.name not in self.tests
        self.tests[test.name] = test

    def get_test(self, name: str) -> TestStatus:
        return self.tests[name]

    def set_result(self, status: str, passed: int, failed: int, ignored: int,
                   measured: int, filtered: int, duration: float) -> None:
        self.status = status
        self.passed = passed
        self.failed = failed
        self.ignored = ignored
        self.measured = measured
        self.filtered = filtered
        self.duration = duration

    def try_parse_result_line(self, line: str) -> None:
        result = RE_SUITE_RESULT.match(line)
        if result:
            self.status = result.group('status')
            self.passed = int(result.group('passed'))
            self.failed = int(result.group('failed'))
            self.ignored = int(result.group('ignored'))
            self.measured = int(result.group('measured'))
            self.filtered = int(result.group('filtered'))
            self.duration = float(result.group('duration'))
            self.parse_state = 'RESULT_PARSED'

    def parse_line(self, line: str, raw_line: str) -> None:
        assert self.parse_state != 'RESULT_PARSED'

        self.try_parse_result_line(line)
        if self.parse_state == 'RESULT_PARSED':
            return

        test_status = RE_TEST_STATUS.match(line)
        if test_status:
            name = test_status.group('name')
            status = test_status.group('status')
            self.add_test(TestStatus(index=len(self.tests) + 1, name=name,
                                     status=status))
            return

        """
        failures:

        ---- should_pass stdout ----

        thread 'should_pass' panicked at rust/zcargo/zcargo.rs:174:5:
        assertion `left == right` failed
        left: 42
        right: 420
        note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace


        failures:
            should_pass
        """  # noqa: E501

        if line.startswith('failures:'):
            if self.parse_state == 'DIAGNOSTICS':
                self.parse_state = 'SUMMARY_FAILURES'
            else:
                self.parse_state = 'DIAGNOSTICS'
            return

        if self.parse_state == 'SUMMARY_FAILURES':
            return

        if self.parse_state == 'DIAGNOSTICS':
            test_stdout = RE_TEST_STDOUT.match(line)
            if test_stdout:
                name = test_stdout.group('name')
                self.test_with_open_stdout = self.get_test(name)
                return

        if self.test_with_open_stdout:
            self.test_with_open_stdout.add_stdout(raw_line)
            return

        if line.strip():
            raise SyntaxError(f'unhandled line: {raw_line}')

    def get_z_output(self) -> str:
        assert self.parse_state == 'RESULT_PARSED'

        return '\n'.join(
            (
                '',
                f'1..{len(self.tests)} {self.name}',
                *(test.get_z_output() for test in self.tests.values()),
                f'# {self.get_summary()}',
            ),
        )

    def get_summary(self) -> str:
        assert self.parse_state == 'RESULT_PARSED'

        if len(self.tests) == 0:
            return 'no tests'

        total = 0.01 * len(self.tests)
        s = self.ignored / total
        p = self.passed / total
        f = self.failed / total
        return f'{s:.0f}% skipped {p:.0f}% passed {f:.0f}% failed'


def get_cargo_test_cmd(pkg: str, argv: list[str]) -> list[str]:
    # Build cargo command: forward arguments after --
    # Ask for no colored output, however we will strip ansi control sequences
    cmd = [os.environ.get('CARGO', 'cargo')]

    if 'USE_SANITIZER' in os.environ:
        # See
        # https://github.com/rust-lang/wg-cargo-std-aware/issues/56#issuecomment-2750778380
        # and
        # https://github.com/rust-lang/wg-cargo-std-aware/issues/29#issuecomment-549950466
        # for panic-abort strategy.
        cmd.extend(['+nightly', '-Zbuild-std=panic_abort,std',
                    '-Zpanic-abort-tests'])

    cmd.append('test')

    if 'USE_SANITIZER' in os.environ:
        cmd.extend(['--target', 'x86_64-unknown-linux-gnu'])

    if 'CARGO_PROFILE' in os.environ:
        cmd.extend(['--profile', os.environ['CARGO_PROFILE']])

    cmd.extend(['--color', 'never', '--package', pkg, *argv])

    return cmd


# }}}


def run_cargo_test_for_pkg(pkg: str, argv: list[str]) -> None:
    cmd = get_cargo_test_cmd(pkg, argv)

    env = os.environ.copy()
    env['CARGO_TERM_COLOR'] = 'never'

    cargo = subprocess.Popen(
        cmd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    if cargo.stdout is None:
        raise OSError('cannot read cargo stdout')

    suites: list[TestSuite] = []
    curr_suite: TestSuite | None = None
    suite_filename: str | None = ''

    def test_suite_name_of_file(filename: str) -> str:
        return f'unit-tests of `{filename}` from pkg `{pkg}` (cargo test)'

    for raw_line in cargo.stdout:
        line = strip_ansi(raw_line).strip()

        if not suite_filename:
            # Line comes from the stderr of `cargo test`
            grp = RE_TESTS_INTRO.match(line)
            if grp:
                curr_suite = None
                suite_filename = grp.group('file')
                continue

            # Ignore compiler messages like
            # Finished `test` profile [unoptimized + debuginfo] target(s) in 0.11s  # noqa: E501
            # Redirect to stderr to catch them in case of cargo test exit
            # with errors (ex. compile error, ASAN error, etc.)
            sys.stderr.write(raw_line)
            continue

        if not curr_suite:
            suite = RE_SUITE_INTRO.match(line)
            if suite:
                assert suite_filename is not None
                suite_name = test_suite_name_of_file(suite_filename)
                planned = int(suite.group('planned'))
                curr_suite = TestSuite(suite_name, planned)
                suites.append(curr_suite)
                continue

            if not line:
                # Ignore empty lines before announcing the test suite
                continue

            raise SyntaxError(f'unhandled line: {raw_line}')

        curr_suite.parse_line(line, raw_line)
        if curr_suite.parse_state == 'RESULT_PARSED':
            curr_suite = None
            suite_filename = None

    rc = cargo.wait()

    if suites:
        curr_suite = suites[-1]
        if curr_suite.parse_state != 'RESULT_PARSED':
            raise SyntaxError('unfinished cargo test suite')

        for s in suites:
            print(s.get_z_output())
    else:
        print(f'# No test suites found for {pkg}')

    # Normal exit status codes:
    # https://doc.rust-lang.org/cargo/commands/cargo-test.html#exit-status
    if rc not in {0, 101}:
        raise OSError(f'`cargo test` for `{pkg}` failed with exit code {rc}')

    if rc:
        if suites:
            raise OSError(f'`cargo test` for `{pkg}` failed (test crash?)')
        raise OSError(f'`cargo test` for `{pkg}` failed (compiler error?)')

    sys.exit(rc)  # should be 0


def print_usage_and_exit(code: int) -> None:
    print(f'usage: {sys.argv[0]} <pkg> [-- cargo-args...]')
    sys.exit(code)


def main() -> None:
    if len(sys.argv) < 2:
        print_usage_and_exit(0)

    argv = sys.argv[2:]
    if argv and argv[0] != '--':
        print_usage_and_exit(1)

    pkg = sys.argv[1]
    run_cargo_test_for_pkg(pkg=pkg, argv=argv[1:])


if __name__ == '__main__':
    main()
