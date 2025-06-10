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
from __future__ import annotations

import datetime
import logging
import re
import sys
from collections import OrderedDict, deque
from collections.abc import Iterable
from logging import NullHandler
from typing import Any, TypeVar

LOGGER = logging.getLogger(__name__)
LOGGER.addHandler(NullHandler())

STATUS = ('pass', 'fail', 'skip', 'todo-pass', 'todo-fail')
EXTENDED_STATUS = STATUS + ('missing', 'bad-number')
RETRY_STEPS = {'check-retry', 'fast-selenium-retry'}

RE_KIND_STEP = re.compile(r"^\s*argv:\s+\[.?'waf',\s+.?'(?P<kind>[^']+)'\]$")
RE_SUITE = re.compile(
    r'.*starting suite (?:\.\/)?(?P<suite>(?P<product>[a-zA-Z0-9_\-\.]*)'
    r'(?:\/.*)?)\.\.\.')  # cannot anchor due to shell colors
RE_DONE_SUITE = re.compile(
    r'(\S*(done )|.*(TEST SUITE (?P<suite>.*) (?P<status>FAILED) ))'
    r'\((?P<time>\d+) seconds\)')  # cannot anchor due to shell colors
RE_GROUP = re.compile(r'^1\.\.(?P<total>\d+) (?P<group>.*)$')
RE_TEST = re.compile(
    r'^ *(?P<number>\d+) (?P<status>{})[ \t]+(?P<name>.+)$'.format(
        '|'.join(STATUS)))
RE_TEST_OPTIONAL = re.compile(
    r'^(?P<name>.*)[ \t]+#[ |\t]+\((?P<time>\d+\.\d+)s?\)'
    r'([ \t]*(?P<comment>.*))?$')
RE_STEP = re.compile(r'^# +\d+-(?P<number>\d+) +(?P<status>{}) +'
                     r'<(?P<name>.*)>? +(?P<filename>.+):(?P<line>\d+) +# '
                     r'\((?P<time>\d+\.\d+)s\)$'.format('|'.join(STATUS)))
RE_END = re.compile(r'^# TOTAL$')
RE_HEADER = re.compile(r'(.*),\d+:(0|1)')
RE_SCREEN = re.compile(r'.*screenshot available -> '
                       r'(?P<url>https://img.corp/.*)')
RE_BROWSER_LOG = re.compile(r'[ |:]*ERROR:corp.intersec.ipy.console.logs:'
                            r'(?P<log>.*)')

RE_CORE = re.compile(r'.*Core was generated.*')
RE_CORE_PROCESS = re.compile(r':Processing .*?core(?:(?:\[New \S+ \d+\])|'
                             r'(?:Traceback))')


# {{{ Error messages

LEN_POS_DIFF = ('{0}tests stoppped at position ({1}) but'
                ' we were expecting it to stop at ({2})'.format)
POS_GT_LEN = 'position greater than group len: '
POS_LT_LEN = 'too many missing tests: '

# }}}


T = TypeVar('T')


def fixed_list() -> deque[T]:
    return deque(maxlen=1000)


class Result:
    name: str | None = None
    step_kind: str | None = None
    retry: bool = False
    time = 0.0
    status = 'pass'
    z_status_nb = ('skipped_nb', 'passed_nb', 'failed_nb', 'total_nb')

    skipped_nb = 0
    passed_nb = 0
    failed_nb = 0
    total_nb = 0

    core = False

    @property
    def skipped(self) -> float:
        if not self.total_nb:
            return 0.
        return self.skipped_nb * 100. / self.total_nb

    @property
    def passed(self) -> float:
        if not self.total_nb:
            return 0.
        if self.status == 'passed':
            return 100 - self.skipped - self.failed
        return self.passed_nb * 100. / self.total_nb

    @property
    def failed(self) -> float:
        if not self.total_nb:
            return 100.
        if self.status == 'fail':
            return 100 - self.skipped - self.passed
        return self.failed_nb * 100. / self.total_nb

    def compute(self) -> None:
        raise NotImplementedError

    def _compute(self, items: Iterable[Result]) -> None:
        results: dict[str, list[Result]] = {k: [] for k in self.z_status_nb}
        for item in items:
            item.compute()
            for k, v in results.items():
                v.append(getattr(item, k))
        for k, v in results.items():
            setattr(self, k, sum(v))

    @property
    def unique(self) -> int:
        return id(self)

    def time_as_str(self) -> str:
        t = int(self.time)
        return f'{datetime.timedelta(seconds=t)!s:0>8}'


class Step:

    def __init__(self, number: int, name: str, status: str, filename: str,
                 line: str, time: float):
        self.number = int(number)
        self.name = name
        self.status = status
        self.filename = filename
        self.line = int(line)
        self.time = float(time)

    def __str__(self) -> str:
        return (f'{self.number:<2} {self.status:<5} {self.time:>6.3f} '
                f'{self.name} {self.filename}:{self.line}')


class Test:

    def __init__(self, number: int, name: str, status: str, time: float = 0.0,
                 comment: str = ''):
        self.number = int(number)
        self.status = status
        self.name = name
        self.time = float(time) if isinstance(time, str) else time
        self.comment = comment
        self.steps: list[Step] = []

    def __str__(self) -> str:
        return (f'{self.number:<5} {self.status:<5} {self.time:>10.6f} '
                f'{self.name} {self.comment.strip()}')


class Group(Result):

    def __init__(self, name: str, total: int):
        self.name: str = name
        self.total_nb = int(total)
        self.tests: OrderedDict[str, Test] = OrderedDict()

    def append_test(self, test: Test) -> None:
        # we already have a test with the same name (behave Scenario Outline?)
        # we sum it.
        if test.name in self.tests:
            outline_nb = (test.number - self.tests[test.name].number) + 1
            test.name = f'{test.name} (outline {outline_nb})'

        self.tests.setdefault(test.name, test)
        self.time += test.time

    def compute(self) -> None:
        results = dict.fromkeys(EXTENDED_STATUS, 0)
        for test in self.tests.values():
            results[test.status] += 1
        self.skipped_nb = results['skip'] + results['todo-fail']
        self.passed_nb = results['pass']
        self.failed_nb = (results['fail'] + results['todo-pass'] +
                          results['missing'] + results['bad-number'])

    def __str__(self) -> str:
        return f'{self.name} ({self.passed}% passed)   {self.time}s'


class Suite(Result):

    def __init__(self, fullname: str, product: str):
        self.name = self.make_short_name(product, fullname)
        self.groups: list[Group] = []
        self.product = product

    @staticmethod
    def make_short_name(product: str, name: str) -> str:
        for useless in ['www/testem/', product + '/', 'testem/',
                        'jasmine/testem/', 'jasmine/']:
            name = name.replace(useless, '', 1)
        return name

    def compute(self) -> None:
        self._compute(self.groups)

        # compute time if needed
        if not self.time:
            for gr in self.groups:
                self.time += gr.time

    def __str__(self) -> str:
        return (f'suite {self.name} passed {self.passed}% '
                f'skipped {self.skipped}% failed {self.failed}%')

    def __repr__(self) -> str:
        return self.__str__()


class Product(Result):
    def __init__(self, name: str):
        self.name: str = name
        self.suites: list[Suite] = []

    def compute(self) -> None:
        self._compute(self.suites)

        # compute time
        for suite in self.suites:
            self.time += suite.time

    def __str__(self) -> str:
        return (f'product {self.name} passed {self.passed}% '
                f'skipped {self.skipped}% failed {self.failed}%')


class Global(Result):
    def __init__(self) -> None:
        self.name = 'global suite'
        self.products: OrderedDict[str, Product] = OrderedDict()
        self.errors: deque[Error] = fixed_list()
        self.timeout = True
        self.additionals: deque[str] = fixed_list()

    def compute(self) -> None:
        self._compute(self.products.values())
        self.define_width()

    def define_width(self) -> None:
        width_min = 9.0
        self.width_passed = self.passed
        self.width_skipped = self.skipped
        self.width_failed = self.failed
        if self.passed == 0 or self.skipped == 0 or self.failed == 0:
            self.width_passed = self.passed
            self.width_skipped = self.skipped
            self.width_failed = self.failed
        elif (self.passed < width_min and self.skipped > width_min
              and self.failed > width_min):
            self.width_passed = width_min
            if (self.skipped - (width_min - self.passed)) > width_min:
                self.width_skipped = self.skipped - (width_min - self.passed)
            else:
                self.width_failed = self.failed - (width_min - self.passed)
        elif (self.passed < width_min and self.skipped < width_min
              and self.failed > width_min):
            self.width_passed = width_min
            self.width_skipped = width_min
            self.width_failed = 100 - 2 * width_min
        elif (self.passed < width_min and self.skipped > width_min
              and self.failed < width_min):
            self.width_passed = width_min
            self.width_skipped = 100 - 2 * width_min
            self.width_failed = width_min
        elif (self.passed > width_min and self.skipped < width_min
              and self.failed < width_min):
            self.width_passed = 100 - 2 * width_min
            self.width_skipped = width_min
            self.width_failed = width_min
        elif (self.passed > width_min and self.skipped < width_min
              and self.failed > width_min):
            self.width_skipped = width_min
            if (self.passed - (width_min - self.skipped)) > width_min:
                self.width_passed = self.passed - (width_min - self.skipped)
            else:
                self.width_failed = self.failed - (width_min - self.skipped)
        elif (self.passed > width_min and self.skipped > width_min
              and self.failed < width_min):
            self.width_failed = width_min
            if (self.passed - (width_min - self.failed)) > width_min:
                self.width_passed = self.passed - (width_min - self.failed)
            else:
                self.width_skipped = self.skipped - (width_min - self.failed)

    def z_total(self) -> str:
        if not self.total_nb:
            return '# NO TESTS FOUND'
        res = [
            '#',
            '#',
            '# TOTAL',
            f'# Skipped {self.skipped_nb:d} ({self.skipped:.1f}%)',
            f'# Failed  {self.failed_nb:d} ({self.failed:.1f}%)',
            f'# Success {self.passed_nb:d} ({self.passed:.1f}%)',
        ]
        return '\n'.join(res)

    def z_additional(self) -> str:
        if not self.additionals:
            return '# NO ADDITIONAL INFOS'
        res = [
            '#',
            ': ADDITIONAL INFOS',
        ]
        res.extend([f':  {elt}' for elt in self.additionals])
        return '\n'.join(res)

    def z_errors(self) -> str:
        if not self.errors:
            return '# NO ERRORS'
        res = ['#', ': ERRORS']
        previous_suite = ''
        for error in self.errors:
            current_suite = f': - ./{error.suite_fullname}'
            if current_suite != previous_suite:
                if previous_suite:
                    res.append('{}: {}'.format(previous_suite, 'error'))
                res.append('{}: {}'.format(current_suite, 'starting'))
            previous_suite = current_suite
            res.append(error.z_error())
            trace = error.z_trace()
            if trace:
                res.append(trace)

        res.append('{}: {}'.format(previous_suite, 'error'))
        return '\n'.join(res)

    def z_report(self) -> str:
        res = [self.z_total()]
        if self.additionals:
            res.append(self.z_additional())
        if self.errors:
            res.append(self.z_errors())
        return '\n'.join(res)

    def __str__(self) -> str:
        return (f'global passed {self.passed}% skipped {self.skipped}% '
                f'failed {self.failed}%')


class Error:

    def __init__(self, product: str, suite: str, group: str, test: str,
                 context: deque[tuple[str, str]], test_filename: str = '',
                 status: str = 'fail'):
        self.productName = product
        self.suite_fullname = suite
        self.suiteName = Suite.make_short_name(product, suite)
        self.groupName = group
        self.testName = test
        self.context_l = context
        self.traces: deque[str] = fixed_list()
        self.step_fail = ''
        self.screen_url = ''
        self.browser_log_l: deque[str] = fixed_list()
        self.status = status
        self.test_filename = test_filename

    @property
    def context(self) -> str:
        return '\n'.join([line[1] for line in self.context_l])

    @property
    def full_name(self) -> str:
        fullname = f'{self.productName} → {self.suiteName}'
        if self.groupName:
            fullname += f' → {self.groupName}'
        if self.testName:
            fullname += f' → {self.testName}'
        return fullname

    @property
    def browser_log(self) -> str:
        return '\n'.join(self.browser_log_l)

    @property
    def trace(self) -> str:
        return '\n'.join(self.traces)

    def z_trace(self) -> str:
        return '\n'.join([f':  {t}' for t in self.traces])

    def z_error(self) -> str:
        return f': - {self!s:s}: {self.status}'

    def z_screenshot(self) -> list[str]:
        return ['Failed screenshot:',
                f'screenshot available -> {self.screen_url}',
                '']

    def z_step_fail(self) -> list[str]:
        return ['Step failed:',
                f'<{self.step_fail}',
                '']

    def __str__(self) -> str:
        return f'{self.groupName}.{self.testName.strip()}'


class StreamParser:
    def __init__(self, stats: Global | None = None):
        self.suite: Suite | None = None
        self.group: Group | None = None
        self.product: Product | None = None
        self.steps: list[Step] = []
        self.error: Error | None = None
        self.first_step_fail: str | None = None
        self.test_filename: str = ''
        self.screenshot: str | None = None
        self.do_break = False
        self.core_logs = False
        self.context: deque[tuple[str, str]] = fixed_list()
        self.res = stats or Global()
        self.last_stream = '2'  # this is the code for 'environment' stream.

        self.group_name = ''
        self.suite_fullname = ''
        self.group_len = 0
        self.group_pos = 0

        self.missing_test_name = 'missing: {0}.({1:d}->{2:d})(unknown)'.format
        self.bad_number_test_name = 'bad-number: {0}.{1}'.format
        self.line_counter = 0

    def parse_line(self, stream_line: bytes | str) -> None:
        self.line_counter += 1
        if self.do_break:
            return

        if isinstance(stream_line, bytes):
            stream_line = stream_line.decode('utf-8', 'replace')
        stream_line = stream_line.strip()

        # The buildbot adds a token within the lines to announce stderr/stdout
        # outputs. This rule checks if this prefix is in the line. It then
        # removes the token if the stream type has not changed otherwise it
        # splits the line in two.
        r = RE_HEADER.match(stream_line)
        if r is not None:
            _, cur_stream = r.groups()
            sub_token = r'\g<1>'
            if cur_stream != self.last_stream:
                sub_token += r'\n'
            self.last_stream = cur_stream
            stream_line = RE_HEADER.sub(sub_token, stream_line)

        lines = stream_line.split('\n')
        for line in lines:
            if not line:
                continue

            # Identify the type of step by parsing the waf command argument.
            # This approach relies on the fact that argv is printed to
            # the console/log.
            # Also detect whether the step is a retry attempt.
            r = RE_KIND_STEP.match(line)
            if r is not None:
                self.res.step_kind = r.group('kind')
                if r.group('kind') in RETRY_STEPS:
                    self.res.retry = True

            r = RE_END.match(line)
            if r is not None:
                self.res.timeout = False
                self.do_break = True
                return

            if RE_CORE.match(line):
                self.res.core = True

            r = RE_SUITE.match(line)
            if r is not None:
                if self.group_len > self.group_pos:
                    test_name = self.missing_test_name(
                        self.group_name, self.group_pos + 1, self.group_len)
                    assert self.product is not None
                    assert self.group is not None
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context,
                        test_filename=self.test_filename, status='missing')
                    self.res.errors.append(self.error)
                    assert self.group_pos <= self.group_len, (
                        LEN_POS_DIFF(POS_GT_LEN, self.group_pos,
                                     self.group_len)
                    )
                    assert self.group_len - self.group_pos < 1000, (
                        LEN_POS_DIFF(POS_LT_LEN, self.group_pos,
                                     self.group_len)
                    )
                    for i in range(self.group_pos, self.group_len):
                        test_name = self.missing_test_name(
                            self.group_name, i + 1, self.group_len)
                        test = Test(i, test_name, 'missing')
                        self.group.append_test(test)
                    self.group_len = 0
                self.context = fixed_list()
                self.suite_fullname, name = r.groups()
                self.product = self.res.products.setdefault(
                    name, Product(name))
                self.suite = Suite(self.suite_fullname, self.product.name)
                self.product.suites.append(self.suite)

            r = RE_GROUP.match(line)
            if r is not None:
                assert self.product is not None
                if self.group:
                    self.group_pos = len(self.group.tests)
                if self.group_len > self.group_pos:
                    test_name = self.missing_test_name(
                        self.group_name, self.group_pos + 1, self.group_len)
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context,
                        test_filename=self.test_filename, status='missing')
                    self.res.errors.append(self.error)
                    assert self.group is not None
                    for i in range(self.group_pos, self.group_len):
                        test_name = self.missing_test_name(
                            self.group_name, i + 1, self.group_len)
                        test = Test(i, test_name, 'missing')
                        self.group.append_test(test)
                group_len, group_name = r.groups()
                self.group_name = str(group_name)
                self.group_len = int(group_len)
                self.group = Group(name=self.group_name, total=self.group_len)
                if not self.suite:
                    self.suite_fullname = (
                        f'{self.product.name}/unknown_suite')
                    self.suite = Suite(self.suite_fullname, self.product.name)
                    self.product.suites.append(self.suite)
                self.suite.groups.append(self.group)
                self.group_pos = 0

            r = RE_DONE_SUITE.match(line)
            if r is not None:
                if self.suite is None:
                    LOGGER.error('wrong suite end, any suites initializes '
                                 'line %s %s', self.line_counter, line[:-1])
                    continue

                assert self.product is not None

                self.suite.time = float(r.group('time'))
                self.suite.status = 'fail' if r.group('status') else 'pass'

                if self.suite.status == 'fail':
                    if len(self.suite.groups) == 0:
                        self.error = Error(
                            self.product.name, self.suite_fullname,
                            'No specific group', 'Suite initialize',
                            self.context)
                        self.res.errors.append(self.error)
                        continue

                    do_err = True
                    for grp in self.suite.groups:
                        if any(t.status in {'fail', 'todo-pass'}
                                for t in grp.tests.values()):
                            do_err = False
                            break
                    if do_err:
                        self.error = Error(
                            self.product.name, self.suite_fullname,
                            'No specific group', 'Outside of any test',
                            self.context)
                        self.res.errors.append(self.error)
                self.suite = None

            r = RE_TEST.match(line)
            if r is not None:
                assert self.product is not None
                assert self.group is not None

                self.error = None
                test_args: dict[str, Any] = dict(r.groupdict())
                r = RE_TEST_OPTIONAL.match(test_args['name'])
                if r is not None:
                    test_args.update(r.groupdict())

                n = int(test_args['number'])
                self.group_pos = len(self.group.tests) + 1

                if n < self.group_pos:
                    test_name = self.bad_number_test_name(
                        self.group_name, test_args['name'])
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context,
                        test_filename=self.test_filename, status='bad-number')
                    self.res.errors.append(self.error)
                elif n > self.group_pos:
                    test_name = self.missing_test_name(
                        self.group_name, self.group_pos, n)
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context,
                        test_filename=self.test_filename, status='missing')
                    self.res.errors.append(self.error)
                    assert self.group_pos <= n, (
                        LEN_POS_DIFF(POS_GT_LEN, self.group_pos,
                                     self.group_len)
                    )
                    assert n - self.group_pos < 1000, (
                        LEN_POS_DIFF(POS_LT_LEN, self.group_pos,
                                     self.group_len)
                    )
                    for i in range(self.group_pos, n):
                        test_name = self.missing_test_name(
                            self.group_name, i, n)
                        test = Test(i, test_name, 'missing')
                        self.group.append_test(test)

                test = Test(**test_args)
                self.group.append_test(test)
                if self.steps:
                    test.steps = self.steps
                    self.steps = []
                self.context.append((self.last_stream, line))

                if test.status in {'fail', 'todo-pass'}:
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group.name, test.name, self.context,
                        test_filename=self.test_filename, status=test.status)
                    self.error.screen_url = self.screenshot or ''
                    self.screenshot = None
                    self.error.step_fail = self.first_step_fail or ''
                    self.first_step_fail = None
                    self.res.errors.append(self.error)
                    self.context = fixed_list()
                    self.context.append((self.last_stream, line))

                    # Define error.trace start content
                    if self.error.step_fail:
                        self.error.traces.extend(
                            self.error.z_step_fail())
                    if self.error.screen_url:
                        self.error.traces.extend(
                            self.error.z_screenshot())
                    self.error.traces.append('Traceback:')
                continue

            r = RE_SCREEN.match(line)
            if r is not None:
                self.screenshot = r.group('url')

            r = RE_STEP.match(line)
            if r is not None:
                step_args: dict[str, Any] = dict(r.groupdict())
                step = Step(**step_args)
                self.steps.append(step)
                if step.status == 'fail':
                    # save the first KO step
                    self.test_filename = step.filename
                    self.first_step_fail = step.name

            if line.startswith(':'):
                if self.error:
                    self.error.traces.append(line[1:])

                    elt = RE_BROWSER_LOG.match(line)
                    if elt is not None:
                        self.error.browser_log_l.append(elt.group('log'))

                    p = RE_CORE_PROCESS.match(line)
                    if p is not None or self.core_logs:
                        self.core_logs = True
                        self.res.additionals.append(line[1:])

                else:
                    self.res.additionals.append(line[1:])
                continue
            self.core_logs = False

            self.context.append((self.last_stream, line))

    def gen_report(self) -> Global:
        if self.group_len > self.group_pos:
            assert self.product is not None
            assert self.group is not None
            test_name = self.missing_test_name(
                self.group_name, self.group_pos + 1, self.group_len)
            self.error = Error(
                self.product.name, self.suite_fullname, self.group_name,
                test_name, self.context, test_filename=self.test_filename,
                status='missing')
            self.res.errors.append(self.error)
            assert self.group_pos <= self.group_len, (
                LEN_POS_DIFF(POS_GT_LEN, self.group_pos, self.group_len))
            assert self.group_len - self.group_pos < 1000, (
                LEN_POS_DIFF(POS_LT_LEN, self.group_pos, self.group_len))
            for i in range(self.group_pos, self.group_len):
                test_name = self.missing_test_name(
                    self.group_name, i + 1, self.group_len)
                test = Test(i, test_name, 'missing')
                self.group.append_test(test)
        self.res.compute()
        return self.res


def main() -> int:
    if len(sys.argv) != 2:
        print('expected one and only one file argument')
        return -1

    stream_parser = StreamParser()

    with open(sys.argv[1], 'r') as f:
        for line in f:
            stream_parser.parse_line(line)

    rept = stream_parser.gen_report()
    print(rept.z_report())
    if rept.errors:
        return -1
    return 0


if __name__ == '__main__':
    sys.exit(main())
