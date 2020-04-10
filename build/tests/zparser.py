#!/usr/bin/env python3
# -*- coding: utf-8 -*-
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


import sys
import re
import datetime
import logging

from logging import NullHandler
from collections import OrderedDict
from collections import deque

LOGGER = logging.getLogger(__name__)
LOGGER.addHandler(NullHandler())

FIXED_LIST = lambda: deque(maxlen=1000)

STATUS = ("pass", "fail", "skip",  "todo-pass", "todo-fail")
EXTENDED_STATUS = STATUS + ("missing", "bad-number")

RE_SUITE = re.compile(
    r".*starting suite (|\.\/)(?P<suite>(?P<product>[a-zA-Z0-9_-]*)"
    r"(\/.*)?)\.\.\.") # cannot anchor due to shell colors
RE_DONE_SUITE = re.compile(
    r"(\S*(done )|.*(TEST SUITE (?P<suite>.*) (?P<status>FAILED) ))"
    r"\((?P<time>\d+) seconds\)") # cannot anchor due to shell colors
RE_GROUP = re.compile(r"^1\.\.(?P<total>\d+) (?P<group>.*)$")
RE_TEST = re.compile(
    r"^ *(?P<number>\d+) (?P<status>{0})[ \t]+(?P<name>.+)$".format(
        "|".join(STATUS)))
RE_TEST_OPTIONAL = re.compile(
    r"^(?P<name>.*)[ \t]+#[ |\t]+\((?P<time>\d+\.\d+)s?\)"
    r"([ \t]*(?P<comment>.*))?$")
RE_STEP = re.compile(r"^# +\d+-(?P<number>\d+) +(?P<status>{0}) +"
                     r"<(?P<name>.*)>? +(?P<filename>.+):(?P<line>\d+) +# "
                     r"\((?P<time>\d+\.\d+)s\)$".format("|".join(STATUS)))
RE_END = re.compile("^# TOTAL$")
RE_HEADER = re.compile(r"(.*),\d+:(0|1)")
RE_SCREEN = re.compile(r"[ |:]*INFO:corp.intersec.ipy.pywww.helpers:"
                       r"screenshot available -> "
                       r"(?P<url>https://img.corp/.*)")
RE_BROWSER_LOG = re.compile(r"[ |:]*ERROR:corp.intersec.ipy.console.logs:"
                            r"(?P<log>.*)")

RE_CORE = re.compile(r".*Core was generated.*")
RE_CORE_PROCESS = re.compile(r":Processing .*?core(?:(?:\[New \S+ \d+\])|"
                             r"(?:Traceback))")


# {{{ Error messages

LEN_POS_DIFF = ("{0}tests stoppped at position ({1}) but"
                " we were expecting it to stop at ({2})".format)
POS_GT_LEN = "position greater than group len: "
POS_LT_LEN = "too many missing tests: "

# }}}


class Result:
    name = None
    time = 0.0
    status = "pass"
    z_status_nb = ("skipped_nb", "passed_nb", "failed_nb", "total_nb")

    skipped_nb = 0
    passed_nb = 0
    failed_nb = 0
    total_nb = 0

    core = False

    @property
    def skipped(self):
        if not self.total_nb:
            return 0.
        return self.skipped_nb * 100. / self.total_nb

    @property
    def passed(self):
        if not self.total_nb:
            return 0.
        if self.status == 'passed':
            return 100 - self.skipped - self.failed
        return self.passed_nb * 100. / self.total_nb

    @property
    def failed(self):
        if not self.total_nb:
            return 100.
        if self.status == 'fail':
            return 100 - self.skipped - self.passed
        return self.failed_nb * 100. / self.total_nb

    def compute(self):
        raise NotImplementedError

    def _compute(self, items):
        results = dict((k, []) for k in self.z_status_nb)
        for item in items:
            item.compute()
            for k in results.keys():
                results[k].append(getattr(item, k))
        for k, v in results.items():
            setattr(self, k, sum(v))

    @property
    def unique(self):
        return id(self)

    def time_as_str(self):
        return "{:0>8}".format(datetime.timedelta(seconds=int(self.time)))


class Step:

    def __init__(self, number, name, status, filename, line, time):
        self.number = int(number)
        self.name = name
        self.status = status
        self.filename = filename
        self.line = int(line)
        self.time = float(time)

    def __str__(self):
        return "{0:<2} {1:<5} {2:>6.3f} {3} {4}:{5}".format(
            self.number, self.status, self.time, self.name,
            self.filename, self.line).encode('utf-8')


class Test:

    def __init__(self, number, name, status, time=0.0, comment=""):
        self.number = int(number)
        self.status = status
        self.name = name
        self.time = float(time) if isinstance(time, str) else time
        self.comment = comment
        self.steps = []

    def __str__(self):
        return "{0:<5} {1:<5} {2:>10.6f} {3} {4}".format(
            self.number, self.status, self.time, self.name,
            self.comment.strip()).encode('utf-8')


class Group(Result):

    def __init__(self, name, total):
        self.name = name
        self.total_nb = int(total)
        self.tests = OrderedDict()

    def append_test(self, test):
        # we already have a test with the same name (behave Scenario Outline?)
        # we sum it.
        if test.name in self.tests:
            outline_nb = (test.number - self.tests[test.name].number) + 1
            test.name = "{0} (outline {1})".format(test.name, outline_nb)

        self.tests.setdefault(test.name, test)
        self.time += test.time

    def compute(self):
        results = dict.fromkeys(EXTENDED_STATUS, 0)
        for test in self.tests.values():
            results[test.status] += 1
        self.skipped_nb = results['skip']
        self.passed_nb = results['pass'] + results['todo-pass']
        self.failed_nb = (results['fail'] + results['todo-fail'] +
                          results['missing'] + results['bad-number'])

    def __str__(self):
        return "{0} ({1}% passed)   {2}s".format(
            self.name, self.passed, self.time).encode('utf-8')


class Suite(Result):

    def __init__(self, fullname, product):
        self.name = self.make_short_name(product, fullname)
        self.groups = []
        self.product = product

    @staticmethod
    def make_short_name(product, name):
        for useless in ["www/testem/", product + "/", "testem/",
                        "jasmine/testem/", "jasmine/"]:
            name = name.replace(useless, "", 1)
        return name

    def compute(self):
        self._compute(self.groups)

        # compute time if needed
        if not self.time:
            for gr in self.groups:
                self.time += gr.time


    def __str__(self):
        return "suite {0} passed {1}% skipped {2}% failed {3}%".format(
            self.name, self.passed, self.skipped, self.failed).encode('utf-8')

    def __repr__(self):
        return self.__str__()


class Product(Result):

    def __init__(self, name):
        self.name = name
        self.suites = []

    def compute(self):
        self._compute(self.suites)

        # compute time
        for suite in self.suites:
            self.time += suite.time

    def __str__(self):
        return "product {0} passed {1}% skipped {2}% failed {3}%".format(
            self.name, self.passed, self.skipped, self.failed).encode('utf-8')


class Global(Result):

    def __init__(self):
        self.name = "global suite"
        self.products = OrderedDict()
        self.errors = FIXED_LIST()
        self.timeout = True
        self.additionals = FIXED_LIST()

    def compute(self):
        self._compute(self.products.values())
        self.define_width()

    def define_width(self):
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

    def z_total(self):
        if not self.total_nb:
            return "# NO TESTS FOUND"
        res = ["#", "#"]
        res.append("# TOTAL")
        res.append("# Skipped %d (%.1f%%)" % (self.skipped_nb, self.skipped))
        res.append("# Failed  %d (%.1f%%)" % (self.failed_nb, self.failed))
        res.append("# Success %d (%.1f%%)" % (self.passed_nb, self.passed))
        return "\n".join(res)

    def z_additional(self):
        if not self.additionals:
            return "# NO ADDITIONAL INFOS"
        res = ["#"]
        res.append(": ADDITIONAL INFOS")
        res.extend([":  {0}".format(l) for l in self.additionals])
        return "\n".join(res)

    def z_errors(self):
        if not self.errors:
            return "# NO ERRORS"
        res = ["#"]
        res.append(": ERRORS")
        previous_suite = ''
        for error in self.errors:
            current_suite = ": - ./{0}".format(error.suite_fullname)
            if current_suite != previous_suite:
                if previous_suite:
                    res.append("{0}: {1}".format(previous_suite, "error"))
                res.append("{0}: {1}".format(current_suite, "starting"))
            previous_suite = current_suite
            res.append(error.z_error())
            trace = error.z_trace()
            if trace:
                res.append(trace)
            res.append('')
        res.append("{0}: {1}".format(previous_suite, "error"))
        return "\n".join(res)

    def z_report(self):
        res = [self.z_total()]
        if self.additionals:
            res.append(self.z_additional())
        if self.errors:
            res.append(self.z_errors())
        return "\n".join(res)

    def __str__(self):
        return "global passed {0}% skipped {1}% failed {2}%".format(
            self.passed, self.skipped, self.failed)


class Error(object):

    def __init__(self, product, suite, group, test, context, status="fail"):
        self.productName = product  # pylint: disable=invalid-name
        self.suite_fullname = suite
        self.suiteName = Suite.make_short_name(product, suite) # pylint: disable=invalid-name
        self.groupName = group  # pylint: disable=invalid-name
        self.testName = test  # pylint: disable=invalid-name
        self.context_l = context
        self.traces = FIXED_LIST()
        self.screen_url = ""
        self.browser_log_l = FIXED_LIST()
        self.status = status

    @property
    def context(self):
        return '\n'.join([line[1] for line in self.context_l])

    @property
    def full_name(self):
        fullname = "{0} → {1}".format(self.productName, self.suiteName)
        if self.groupName:
            fullname += " → {0}".format(self.groupName)
        if self.testName:
            fullname += " → {0}".format(self.testName)
        return fullname

    @property
    def browser_log(self):
        return "\n".join(self.browser_log_l)

    @property
    def trace(self):
        return "\n".join(self.traces)

    def z_trace(self):
        return "\n".join([":  {0}".format(t) for t in self.traces])

    def z_error(self):
        return ": - {0:s}: {1}".format(str(self), self.status)

    def __str__(self):
        return "{0}.{1}".format(self.groupName, self.testName.strip())


class StreamParser(object):
    def __init__(self, stats=None):
        self.suite = None
        self.group = None
        self.product = None
        self.steps = []
        self.error = None
        self.do_break = False
        self.core_logs = False
        self.context = FIXED_LIST()
        self.res = stats or Global()
        self.last_stream = '2' # this is the code for 'environment' stream.

        self.group_name = ""
        self.suite_fullname = ""
        self.group_len = 0
        self.group_pos = 0

        self.missing_test_name = "missing: {0}.({1:d}->{2:d})(unknown)".format
        self.bad_number_test_name = "bad-number: {0}.{1}".format
        self.line_counter = 0

    def parse_line(self, line):
        self.line_counter += 1
        if self.do_break:
            return

        if isinstance(line, bytes):
            line = line.decode('utf-8', 'replace')
        line = line.strip()

        # The buildbot adds a token within the lines to announce stderr/stdout
        # outputs. This rule checks if this prefix is in the line. It then
        # removes the token if the stream type has not changed otherwise it
        # splits the line in two.
        r = RE_HEADER.match(line)
        if r is not None:
            _, cur_stream  = r.groups()
            sub_token = r'\g<1>'
            if cur_stream != self.last_stream:
                sub_token += r'\n'
            self.last_stream = cur_stream
            line = RE_HEADER.sub(sub_token, line)

        lines = line.split('\n')
        for line in lines:
            if not line:
                continue

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
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context, "missing")
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
                        test = Test(i, test_name, "missing")
                        self.group.append_test(test)
                    self.group_len = 0
                self.context = FIXED_LIST()
                _, self.suite_fullname, name, _ = r.groups()
                self.product = self.res.products.setdefault(
                    name, Product(name))
                self.suite = Suite(self.suite_fullname, self.product.name)
                self.product.suites.append(self.suite)

            r = RE_GROUP.match(line)
            if r is not None:
                if self.group:
                    self.group_pos = len(self.group.tests)
                if self.group_len > self.group_pos:
                    test_name = self.missing_test_name(
                        self.group_name, self.group_pos + 1, self.group_len)
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context, "missing")
                    self.res.errors.append(self.error)
                    for i in range(self.group_pos, self.group_len):
                        test_name = self.missing_test_name(
                            self.group_name, i + 1,self.group_len)
                        test = Test(i, test_name, "missing")
                        self.group.append_test(test)
                self.group_len, self.group_name = r.groups()
                self.group_len = int(self.group_len)
                self.group = Group(name=self.group_name, total=self.group_len)
                if not self.suite:
                    self.suite_fullname = (
                        "{0}/unknown_suite".format(self.product.name))
                    self.suite = Suite(self.suite_fullname, self.product.name)
                    self.product.suites.append(self.suite)
                self.suite.groups.append(self.group)
                self.group_pos = 0

            r = RE_DONE_SUITE.match(line)
            if r is not None:
                if self.suite is None:
                    LOGGER.error("wrong suite end, any suites initializes "
                                 "line %s %s", self.line_counter, line[:-1])
                    continue

                self.suite.time = float(r.group('time'))
                self.suite.status = "fail" if r.group('status') else "pass"

                if self.suite.status == "fail":
                    if len(self.suite.groups) == 0:
                        self.error = Error(
                            self.product.name, self.suite_fullname,
                            "No specific group", "Suite initialize",
                            self.context)
                        self.res.errors.append(self.error)
                        continue

                    do_err = True
                    for grp in self.suite.groups:
                        if any([t.status == 'fail'
                                for t in grp.tests.values()]):
                            do_err = False
                            break
                    if do_err:
                        self.error = Error(
                            self.product.name, self.suite_fullname,
                            "No specific group", "Outside of any test",
                            self.context)
                        self.res.errors.append(self.error)
                self.suite = None

            r = RE_TEST.match(line)
            if r is not None:
                self.error = None
                test_args = dict(r.groupdict())
                r = RE_TEST_OPTIONAL.match(test_args["name"])
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
                        "bad-number")
                    self.res.errors.append(self.error)
                elif n > self.group_pos:
                    test_name = self.missing_test_name(
                        self.group_name, self.group_pos, n)
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group_name, test_name, self.context, "missing")
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
                        test = Test(i, test_name, "missing")
                        self.group.append_test(test)

                test = Test(**test_args)
                self.group.append_test(test)
                if self.steps:
                    test.steps = self.steps
                    self.steps = []
                self.context.append((self.last_stream, line))

                if test.status == "fail" or test.status == "todo-fail":
                    self.error = Error(
                        self.product.name, self.suite_fullname,
                        self.group.name, test.name, self.context, test.status)
                    self.res.errors.append(self.error)
                    self.context = FIXED_LIST()
                    self.context.append((self.last_stream, line))
                continue

            r = RE_STEP.match(line)
            if r is not None:
                step_args = dict(r.groupdict())
                self.steps.append(Step(**step_args))

            if line.startswith(':'):
                if self.error:
                    self.error.traces.append(line[1:])
                    r = RE_SCREEN.match(line)
                    if r is not None:
                        self.error.screen_url = r.group('url')

                    l = RE_BROWSER_LOG.match(line)
                    if l is not None:
                        self.error.browser_log_l.append(l.group('log'))

                    p = RE_CORE_PROCESS.match(line)
                    if p is not None or self.core_logs:
                        self.core_logs = True
                        self.res.additionals.append(line[1:])

                else:
                    self.res.additionals.append(line[1:])
                continue
            else:
                self.core_logs = False

            self.context.append((self.last_stream, line))

    def gen_report(self):
        if self.group_len > self.group_pos:
            test_name = self.missing_test_name(
                self.group_name, self.group_pos + 1, self.group_len)
            self.error = Error(
                self.product.name, self.suite_fullname, self.group_name,
                test_name, self.context, "missing")
            self.res.errors.append(self.error)
            assert self.group_pos <= self.group_len, (
                LEN_POS_DIFF(POS_GT_LEN, self.group_pos, self.group_len))
            assert self.group_len - self.group_pos < 1000, (
                LEN_POS_DIFF(POS_LT_LEN, self.group_pos, self.group_len))
            for i in range(self.group_pos, self.group_len):
                test_name = self.missing_test_name(
                    self.group_name, i + 1, self.group_len)
                test = Test(i, test_name, "missing")
                self.group.append_test(test)
        self.res.compute()
        return self.res


def main():
    if len(sys.argv) != 2:
        print("expected one and only one file argument")
        return -1

    stream_parser = StreamParser()

    with open(sys.argv[1], 'r') as f:
        for line in f:
            stream_parser.parse_line(line)

    rept = stream_parser.gen_report()
    print(rept.z_report())
    if rept.errors:
        if not all([e.status.startswith('todo') for e in rept.errors]):
            return -1
    return 0


if __name__ == '__main__':
    sys.exit(main())
