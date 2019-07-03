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

"""
z

z is a wrapper around unnitest (python 2.7+) or unittest2

To write a "group" of test, write a class deriving from unittest.TestCase
and decorate it with z.ZGroup this way:

>>> import z
>>>
>>> @z.ZGroup
>>> class MyTest(z.TestCase):
>>>     pass

Any function in MyTest whose name starts with 'test' will be treated as a
test.

Tests functions can be decorated with any of the unittest usual decorator but
for expectedFailure where @z.ZTodo(reason) must be used instead.

As a conveniency, the z.ZFlags() decorator exists to put flags on a test, it's
a conveniency wrapper around unittest.skip()

Doctests can also be run:

>>> import my_python_module
>>>
>>> @z.ZGroup
>>> class MyTest(z.DocTestModule):
>>>     module = my_python_module

At the end of your test, just call z.main() (and not unittest.main()).
if Z_HARNESS is set, then z.main() will run in "Z" mode, else it fallbacks to
unittest.main() allowing all the python unittest module features (such as
running tests as specified on the command line).

Note that 'z' extends the unittest module, so you just need to import z and
use z.* as you would use unittest instead
"""

import sys
import os
import traceback
import time

# For some reason, using doctest in Python 2.6 with some values for TERM will
# escape characters, which will make the output parser of the test (when run
# as make check) fail, and report a incorrect failure on this file.
# Fix this by hotfixing the TERM env in this specific case.
if not sys.stdout.isatty() and sys.version_info < (2, 7):
    os.environ['TERM'] = 'linux'

# pylint: disable=wrong-import-position
import doctest

from functools import wraps
import zpycore.util

try:
    import unittest2 as unittest
except ImportError:
    import unittest

_U = __import__(unittest.__name__, globals(), locals())
globals().update(_U.__dict__)
__all__ = list(_U.__all__)


def public(sym):
    __all__.append(sym.__name__)
    return sym

class _LoadTests(object):
    """
    _LoadTests

    @see ZGroup or ZDocTest
    """
    def __init__(self):
        self.groups = []
        self.docsuites = []

    def __call__(self, loader, tests, prefix):
        suite = unittest.TestSuite()

        for g in self.groups:
            suite.addTest(loader.loadTestsFromTestCase(g))
        for s in self.docsuites:
            suite.addTest(s())

        return suite

    def add_group(self, group):
        self.groups.append(group)

    def add_docsuite(self, suite):
        self.docsuites.append(suite)

@public
def ZGroup(cls): # pylint: disable=invalid-name
    """
    ZGroup

    This decorator generates a 'load_tests' function for the module
    where the class lives, for use with unittest.main()
    """
    __import__(cls.__module__)
    m = sys.modules[cls.__module__]
    if getattr(m, 'load_tests', None) is None:
        m.load_tests = _LoadTests()

    if issubclass(cls, DocTestModule):
        m.load_tests.add_docsuite(cls)
    else:
        m.load_tests.add_group(cls)

    return cls

# {{{ ZTestSuite


class ZTestSuite(unittest.TestSuite):

    def _is_new_group(self):
        return (isinstance(self, DocTestModule) or
                (self._tests and isinstance(self._tests, list)
                 and isinstance(self._tests[0], unittest.TestCase)))

    def _handle_group(self, result):
        if self._is_new_group():
            result.reset()
            result.print_suite_summary(self)

    def run(self, result):
        if os.getenv('Z_HARNESS'):
            self._handle_group(result)
        return super(ZTestSuite, self).run(result)

    # XXX: _wrapped_run for old version of unittest2
    def _wrapped_run(self, result, debug=False):
        if os.getenv('Z_HARNESS'):
            self._handle_group(result)
        return super(ZTestSuite, self)._wrapped_run(result, debug=debug)


# }}}
# {{{ DocTests */

OLD_RUNNER = doctest.DocTestRunner
class IopDocTestRunner(OLD_RUNNER):
    """ Custom Doc test runner

    Used to wrap tests in a "print_iop" function
    """
    def __init__(self, *args, **kwargs):
        # XXX: parent is old style class, so super cannot be used: save the
        # parent in in field instead
        # pylint: disable=non-parent-init-called
        OLD_RUNNER.__init__(self, *args, **kwargs)

    def run(self, test, *args, **kwargs):
        for example in test.examples:
            source = example.source.strip()
            # Wrap the code with a print call, unless a for loop is detected
            if not (source.startswith('for ') or source.startswith('with')):
                example.source = 'print({0})\n'.format(source)

        return OLD_RUNNER.run(self, test, *args, **kwargs)


@public
class DocTestModule(ZTestSuite):
    """Class used to declare a module containing doc tests

    ``module``: mandatory static field, the module to test
    ``extraglobs``: optional method, can be used to specify globs to
        pass to the tests environnement.
    """
    optionflags = doctest.NORMALIZE_WHITESPACE | doctest.ELLIPSIS

    @staticmethod
    def extraglobs():
        return {}

    def __init__(self, *args, **kwargs):
        # upgrade doc test runner to our custom one
        if not isinstance(doctest.DocTestCase, IopDocTestRunner):
            doctest.DocTestRunner = IopDocTestRunner

        test_finder = doctest.DocTestFinder()
        tests = test_finder.find(self.module, extraglobs=self.extraglobs())
        tests.sort()

        # XXX: Very hacky. unittest inspects the module of the class of a
        # test case in order to find setUpModule/tearDownModule methods. In
        # order to have this method called properly, the test cases generated
        # for a doc test module must belong to the original module: the one
        # that inherited from this class
        class ModuledTestCase(doctest.DocTestCase):
            pass

        ModuledTestCase.__module__ = self.__module__
        tests = [ModuledTestCase(t, optionflags=self.optionflags)
                 for t in tests]

        super(DocTestModule, self).__init__(tests, *args, **kwargs)


# }}}

_Z_MODES = set(os.getenv('Z_MODE', '').split())
_FLAGS   = set(os.getenv('Z_TAG_SKIP', '').split())
_TAG_OR = os.getenv('Z_TAG_OR', '')
_ALL_FLAGS = {}

@public
def ZFlags(*flags): # pylint: disable=invalid-name
    """
    ZFlags

    This decorator is a wrapper around unittest.skip() to implement the
    Z_TAG_SKIP required interface.
    """
    def wrap(func):
        func_flags = _ALL_FLAGS.setdefault(id(func), [])
        func_flags.extend(flags)
        fl = set(func_flags) & _FLAGS
        if any([f in _TAG_OR for f in func_flags]) and "wip" not in fl:
            func.__unittest_skip__ = False
            return func
        if len(fl):
            return unittest.skip("skipping tests flagged with %s" %
                                 (" ".join(fl)))(func)
        return func
    return wrap

class _ZTodo(Exception):
    """
    _ZTodo

    Hack to store the reason in the exception for ZTodo.
    """
    def __init__(self, reason, exc_info):
        self.reason = reason
        self.exc_info = exc_info
        super(_ZTodo, self).__init__(reason, exc_info)

@public
def ZTodo(reason): # pylint: disable=invalid-name
    """
    ZTodo

    Decorator to use instead of unittest.expectedFailure
    """
    def decorator(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            try:
                func(*args, **kwargs)
            except Exception:
                raise _ZTodo(reason, sys.exc_info())
        return unittest.case.expectedFailure(wrapper)
    return decorator

class _ZTextTestResult(unittest.TextTestResult):
    """
    _ZTextTestResult

    Replacement of TextTestResult to add ability to debug our tests with ipdb.
    Also, addExpectedFailure is overriden to fixup the _ZTodo hack
    """

    def __init__(self, *args, **kwargs):
        self.debug_on = os.getenv("Z_DEBUG_ON_ERROR") == "1"
        super(_ZTextTestResult, self).__init__(*args, **kwargs)

    def debug(self, err):
        if self.debug_on:
            import ipdb
            _, _, exc_traceback = err
            ipdb.post_mortem(exc_traceback)

    def addError(self, test, err):
        """Called when an error has occurred. 'err' is a tuple of values as
        returned by sys.exc_info().
        """
        self.debug(err)
        super(_ZTextTestResult, self).addError(test, err)

    def addFailure(self, test, err):
        """Called when an error has occurred. 'err' is a tuple of values as
        returned by sys.exc_info()."""
        self.debug(err)
        super(_ZTextTestResult, self).addFailure(test, err)

    def addExpectedFailure(self, test, err):
        """Replacement of addExpectedFailure to fixup the _ZTodo hack"""
        assert err[0] == _ZTodo
        exn = err[1]
        assert isinstance(exn, _ZTodo)
        super(_ZTextTestResult, self).addExpectedFailure(test, exn.exc_info)


class _ZTestResult(unittest.TestResult):
    """
    _ZTestResult

    TestResult subclass implementing the Z protocol
    Only used when Z_HARNESS is set
    """
    def __init__(self, *args, **kwargs):
        self.start_time = time.time()
        self.global_failures = []
        self.global_errors = []
        super(_ZTestResult, self).__init__(*args, **kwargs)

    def startTest(self, test):
        zpycore.util.wipe_children_rearm()
        self.start_time = time.time()
        super(_ZTestResult, self).startTest(test)

    def _put_st(self, what, test, rest = ""):
        zpycore.util.wipe_children_rearm()
        run_time = time.time() - self.start_time
        if isinstance(test, doctest.DocTestCase):
            tid = test.id()
        else:
            tid = getattr(test, '_testMethodName', None)
        sys.stdout.write("%d %s %s # (%.3fs)" %
                         (self.testsRun, what, tid, run_time))
        if len(rest):
            sys.stdout.write(rest)
        sys.stdout.write("\n")

    @classmethod
    def _put_err(cls, test, err):
        zpycore.util.wipe_children_rearm()
        tid = test.id()
        if tid.startswith("__main__."):
            tid = tid[len("__main__."):]
        sys.stdout.write(': $ %s %s\n:\n' % (sys.argv[0], tid))

        exctype, value, tb = err
        lines = traceback.format_exception(exctype, value, tb)
        sys.stdout.write(': ')
        sys.stdout.write('\n: '.join(''.join(lines).split("\n")))
        sys.stdout.write("\n")

    def addSuccess(self, test):
        super(_ZTestResult, self).addSuccess(test)
        self._put_st("pass", test)
        sys.stdout.flush()

    def addError(self, test, err):
        super(_ZTestResult, self).addError(test, err)
        self.global_errors.append((test, self._exc_info_to_string(err, test)))
        self._put_st("fail", test)
        self._put_err(test, err)
        sys.stdout.flush()

    def addFailure(self, test, err):
        super(_ZTestResult, self).addFailure(test, err)
        self.global_failures.append((test,
                                     self._exc_info_to_string(err, test)))
        self._put_st("fail", test)
        self._put_err(test, err)
        sys.stdout.flush()

    def addSkip(self, test, reason):
        super(_ZTestResult, self).addSkip(test, reason)
        self._put_st("skip", test, reason)
        sys.stdout.flush()

    def addExpectedFailure(self, test, err):
        assert err[0] == _ZTodo
        exn = err[1]
        assert isinstance(exn, _ZTodo)
        super(_ZTestResult, self).addExpectedFailure(test, exn.exc_info)
        self._put_st("todo-fail", test, exn.reason)
        self._put_err(test, exn.exc_info)
        sys.stdout.flush()

    def addUnexpectedSuccess(self, test):
        super(_ZTestResult, self).addUnexpectedSuccess(test)
        self._put_st("todo-pass", test)
        sys.stdout.flush()

    @classmethod
    def print_suite_summary(cls, test):
        # pylint: disable=protected-access
        if isinstance(test, DocTestModule):
            group_name = test.__class__.__name__
        else:
            group_name = test._tests[0].__class__.__name__
        sys.stdout.write("1..%d %s\n" % (test.countTestCases(), group_name))
        sys.stdout.flush()

    def reset(self):
        self.failures = []
        self.errors = []
        self.testsRun = 0
        self.skipped = []
        self.expectedFailures = []
        self.unexpectedSuccesses = []

    def wasSuccessful(self):
        return (len(self.global_failures) + len(self.global_errors) == 0)


class ZTestRunner(unittest.TextTestRunner):
    """ split test suite output by group

    By default unittest as the following test tree:
      suite
        suite1
          test1
          test2
        suite2
          test3
    But z format is defined as follow:
      suite
        groupe1
          test1
          test2
        groupe2
          test1
    """

    def run(self, test):
        result = _ZTestResult()
        result.failfast = self.failfast
        result.buffer = self.buffer

        test(result)

        return result


@public
class TestCase(unittest.TestCase):

    # deprecated
    def zHasMode(self, mode): # pylint: disable=invalid-name
        return self.z_has_mode(mode)

    @classmethod
    def z_has_mode(cls, mode):
        return mode in _Z_MODES

    @classmethod
    def z_has_skipped_tag(cls, tag):
        return tag in _FLAGS

@public
def expectedFailure(*args, **kwargs): # pylint: disable=invalid-name
    """
    overrides the unittest definition so that people won't use it by mistake
    """
    raise Exception("Do not use expectedFailure but ZTodo instead")


@public
def main():
    with zpycore.util.wipe_children_register():
        if os.getenv('Z_HARNESS'):
            unittest.TestLoader.suiteClass = ZTestSuite
            unittest.main(testRunner=ZTestRunner)
        else:
            unittest.TextTestRunner.resultclass = _ZTextTestResult
            unittest.main()
