#/usr/bin/env python3
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
z

z is a wrapper around unittest

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

import doctest
import os
import sys
import time
import traceback
import unittest
from functools import wraps
from types import TracebackType
from typing import Any, Callable, ClassVar, NoReturn, TypeVar, Union, cast

from .util import wipe_children_rearm, wipe_children_register

ExecInfo = Union[
    tuple[type[BaseException], BaseException, TracebackType],
    tuple[None, None, None],
]


T = TypeVar('T')


class _LoadTests:
    """
    _LoadTests

    @see ZGroup or ZDocTest
    """

    def __init__(self) -> None:
        self.groups: list[type[unittest.TestCase]] = []
        self.docsuites: list[type[DocTestModule]] = []

    def __call__(self, loader: unittest.TestLoader,
                 tests: list[unittest.TestCase],
                 prefix: str) -> unittest.TestSuite:
        suite = unittest.TestSuite()

        for g in self.groups:
            suite.addTest(loader.loadTestsFromTestCase(g))
        for s in self.docsuites:
            suite.addTest(s())

        return suite

    def add_group(self, group: type[unittest.TestCase]) -> None:
        self.groups.append(group)

    def add_docsuite(self, suite: type['DocTestModule']) -> None:
        self.docsuites.append(suite)


def ZGroup(cls: type[T]) -> type[T]:  # noqa: N802 (invalid-function-name)
    """
    ZGroup

    This decorator generates a 'load_tests' function for the module
    where the class lives, for use with unittest.main()
    """
    __import__(cls.__module__)
    m = sys.modules[cls.__module__]

    if getattr(m, 'load_tests', None) is None:
        m.load_tests = _LoadTests()  # type: ignore[attr-defined]

    if issubclass(cls, DocTestModule):
        m.load_tests.add_docsuite(cls)
    else:
        m.load_tests.add_group(cls)

    return cls


# {{{ ZTestSuite


class ZTestSuite(unittest.TestSuite):

    def _is_new_group(self) -> bool:
        return (isinstance(self, DocTestModule) or
                (bool(self._tests) and isinstance(self._tests, list)
                 and isinstance(self._tests[0], unittest.TestCase)))

    def _handle_group(self, result: '_ZTestResult') -> None:
        if self._is_new_group():
            result.reset()
            result.print_suite_summary(self)

    def run(self, result: '_ZTestResult', # type: ignore[override]
            debug: bool = False) -> '_ZTestResult':
        if os.getenv('Z_HARNESS'):
            self._handle_group(result)
        return cast('_ZTestResult', super().run(result, debug))


# }}}
# {{{ DocTests */

class IopDocTestRunner(doctest.DocTestRunner):
    """
    Custom Doc test runner

    Used to wrap tests in a "print_iop" function
    """

    def run(self, test: doctest.DocTest, *args: Any,
            **kwargs: Any) -> doctest.TestResults:
        for example in test.examples:
            source = example.source.strip()
            # Wrap the code with a print call, unless a for loop is detected
            if not (source.startswith('for ') or source.startswith('with')):
                example.source = f'print({source})\n'

        return super().run(test, *args, **kwargs)


class DocTestModule(ZTestSuite):
    """
    Class used to declare a module containing doc tests

    ``module``: mandatory static field, the module to test
    ``extraglobs``: optional method, can be used to specify globs to
        pass to the tests environnement.
    """

    module: ClassVar[Any]
    optionflags = doctest.NORMALIZE_WHITESPACE | doctest.ELLIPSIS

    @staticmethod
    def extraglobs() -> dict[str, int]:
        return {}

    def __init__(self, *args: Any, **kwargs: Any):
        # upgrade doc test runner to our custom one
        if not isinstance(doctest.DocTestCase, IopDocTestRunner):
            doctest.DocTestRunner = IopDocTestRunner  # type: ignore[misc]

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
        moduled_tests = [ModuledTestCase(t, optionflags=self.optionflags)
                         for t in tests]
        tests_casted = cast(list[TestCase], moduled_tests)

        super().__init__(tests_casted, *args, **kwargs)


# }}}

_Z_MODES = set(os.getenv('Z_MODE', '').split())
_FLAGS = set(os.getenv('Z_TAG_SKIP', '').split())
_TAG_OR = os.getenv('Z_TAG_OR', '')
_ALL_FLAGS: dict[Any, list[Any]] = {}

def ZFlags(*flags: str) -> Callable[[T], T]:  # noqa: N802 (invalid-function-name)
    """
    ZFlags

    This decorator is a wrapper around unittest.skip() to implement the
    Z_TAG_SKIP required interface.
    """
    def wrap(func: T) -> T:
        func_flags = _ALL_FLAGS.setdefault(func, [])
        func_flags.extend(flags)
        fl = set(func_flags) & _FLAGS
        if any(f in _TAG_OR for f in func_flags) and 'wip' not in fl:
            func.__unittest_skip__ = False # type: ignore[attr-defined]
            return func
        if fl:
            skip_msg = 'skipping tests flagged with %s' % (' '.join(fl))
            skip_wrapper = unittest.skip(skip_msg)
            return skip_wrapper(func) # type: ignore[type-var]
        return func
    return wrap

class _ZTodo(Exception):  # noqa: N818 (error-suffix-on-exception-name)
    """
    _ZTodo

    Hack to store the reason in the exception for ZTodo.
    """

    def __init__(self, reason: str, exc_info: ExecInfo):
        self.reason = reason
        self.exc_info = exc_info
        super().__init__(reason, exc_info)

def ZTodo(reason: str) -> Any:  # noqa: N802 (invalid-function-name)
    """
    ZTodo

    Decorator to use instead of unittest.expectedFailure
    """
    def decorator(func: Any) -> Any:
        @wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> None:
            try:
                func(*args, **kwargs)
            except Exception as exc:
                raise _ZTodo(reason, sys.exc_info()) from exc
        return unittest.case.expectedFailure(wrapper)
    return decorator

class _ZTextTestResult(unittest.TextTestResult):
    """
    _ZTextTestResult

    Replacement of TextTestResult to add ability to debug our tests with pdb.
    Also, addExpectedFailure is overriden to fixup the _ZTodo hack
    """

    def __init__(self, *args: Any, **kwargs: Any):
        self.debug_on = os.getenv('Z_DEBUG_ON_ERROR') == '1'
        super().__init__(*args, **kwargs)

    def debug(self, err: ExecInfo) -> None:
        if self.debug_on:
            import pdb
            _, _, exc_traceback = err
            pdb.post_mortem(exc_traceback)

    def addError(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            err: ExecInfo,
    ) -> None:
        """
        Called when an error has occurred. 'err' is a tuple of values as
        returned by sys.exc_info().
        """
        self.debug(err)
        super().addError(test, err)

    def addFailure(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            err: ExecInfo,
    ) -> None:
        """
        Called when an error has occurred. 'err' is a tuple of values as
        returned by sys.exc_info().
        """
        self.debug(err)
        super().addFailure(test, err)

    def addExpectedFailure(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            err: ExecInfo,
    ) -> None:
        """Replacement of addExpectedFailure to fixup the _ZTodo hack"""
        assert err[0] == _ZTodo
        exn = err[1]
        assert isinstance(exn, _ZTodo)
        super().addExpectedFailure(test, exn.exc_info)


class _ZTestResult(unittest.TestResult):
    """
    _ZTestResult

    TestResult subclass implementing the Z protocol
    Only used when Z_HARNESS is set
    """

    def __init__(self, *args: Any, **kwargs: Any):
        self.start_time = time.time()
        self.global_failures: list[tuple[unittest.TestCase, str]] = []
        self.global_errors: list[tuple[unittest.TestCase, str]] = []
        super().__init__(*args, **kwargs)

    def startTest(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
    ) -> None:
        wipe_children_rearm()
        self.start_time = time.time()
        super().startTest(test)

    def _put_st(self, what: str, test: unittest.TestCase,
                rest: str = '') -> None:
        wipe_children_rearm()
        run_time = time.time() - self.start_time
        if isinstance(test, doctest.DocTestCase):
            tid = test.id()
        else:
            tid = getattr(test, '_testMethodName', '')
        sys.stdout.write('%d %s %s # (%.3fs)' %
                         (self.testsRun, what, tid, run_time))
        if rest:
            sys.stdout.write(rest)
        sys.stdout.write('\n')

    @classmethod
    def _put_err_common(cls, test: unittest.TestCase) -> None:
        wipe_children_rearm()
        tid = test.id()
        tid = tid.removeprefix('__main__.')
        sys.stdout.write(': $ %s %s\n:\n' % (sys.argv[0], tid))

    @classmethod
    def _put_err(cls, test: unittest.TestCase, err: ExecInfo) -> None:
        cls._put_err_common(test)

        exctype, value, tb = err
        lines = traceback.format_exception(exctype, value, tb)
        sys.stdout.write(': ')
        sys.stdout.write('\n: '.join(''.join(lines).split('\n')))
        sys.stdout.write('\n')

    @classmethod
    def _put_err_str(cls, test: unittest.TestCase, err: str) -> None:
        cls._put_err_common(test)

        sys.stdout.write(': %s\n' % (err))

    def addSuccess(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
    ) -> None:
        super().addSuccess(test)
        self._put_st('pass', test)
        sys.stdout.flush()

    def addError(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            err: ExecInfo,
    ) -> None:
        super().addError(test, err)
        self.global_errors.append((
            test,
            self._exc_info_to_string(err, test), # type: ignore[attr-defined]
        ))
        self._put_st('fail', test)
        self._put_err(test, err)
        sys.stdout.flush()

    def addFailure(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            err: ExecInfo,
    ) -> None:
        super().addFailure(test, err)
        self.global_failures.append((
            test,
            self._exc_info_to_string(err, test), # type: ignore[attr-defined]
        ))
        self._put_st('fail', test)
        self._put_err(test, err)
        sys.stdout.flush()

    def addSkip(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            reason: str,
    ) -> None:
        super().addSkip(test, reason)
        self._put_st('skip', test, reason)
        sys.stdout.flush()

    def addExpectedFailure(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
            err: ExecInfo,
    ) -> None:
        assert err[0] == _ZTodo
        exn = err[1]
        assert isinstance(exn, _ZTodo)
        super().addExpectedFailure(test, exn.exc_info)
        self._put_st('todo-fail', test, exn.reason)
        self._put_err(test, exn.exc_info)
        sys.stdout.flush()

    def addUnexpectedSuccess(  # noqa: N802 (invalid-function-name)
            self, test: unittest.TestCase,
    ) -> None:
        super().addUnexpectedSuccess(test)
        self._put_st('todo-pass', test)
        self._put_err_str(test, 'ZTodo should not pass')
        sys.stdout.flush()

    @classmethod
    def print_suite_summary(cls, test: unittest.TestSuite) -> None:
        if isinstance(test, DocTestModule):
            group_name = test.__class__.__name__
        else:
            group_name = test._tests[0].__class__.__name__
        sys.stdout.write('1..%d %s\n' % (test.countTestCases(), group_name))
        sys.stdout.flush()

    def reset(self) -> None:
        self.failures = []
        self.errors = []
        self.testsRun = 0
        self.skipped = []
        self.expectedFailures = []
        self.unexpectedSuccesses = []

    def wasSuccessful(self) -> bool:  # noqa: N802 (invalid-function-name)
        return (len(self.global_failures) + len(self.global_errors) +
                len(self.unexpectedSuccesses) == 0)


class ZTestRunner(unittest.TextTestRunner):
    """
    split test suite output by group

    By default, unittest as the following test tree:
      suite
        suite1
          test1
          test2
        suite2
          test3
    But z format is defined as follows:
      suite
        group1
          test1
          test2
        group2
          test1
    """

    failfast: bool
    buffer: bool

    def run( # type: ignore[override]
            self, test: Union[unittest.TestSuite, unittest.TestCase],
    ) -> unittest.TestResult:
        result = _ZTestResult()
        result.failfast = self.failfast
        result.buffer = self.buffer

        test(result)

        return result


class TestCase(unittest.TestCase):

    # deprecated
    def zHasMode(  # noqa: N802 (invalid-function-name)
            self, mode: str,
    ) -> bool:
        return self.z_has_mode(mode)

    @classmethod
    def z_has_mode(cls, mode: str) -> bool:
        return mode in _Z_MODES

    @classmethod
    def z_has_skipped_tag(cls, tag: str) -> bool:
        return tag in _FLAGS


def expectedFailure(  # noqa: N802 (invalid-function-name)
        *args: Any, **kwargs: Any,
) -> NoReturn:
    """
    Overrides the unittest definition so that people won't use it by mistake
    """
    raise RuntimeError('Do not use expectedFailure but ZTodo instead')


class TestProgram(unittest.TestProgram):
    def __init__(self, *args: Any, **kwargs: Any):
        with wipe_children_register():
            if os.getenv('Z_HARNESS'):
                unittest.TestLoader.suiteClass = ZTestSuite
                kwargs['testRunner'] = ZTestRunner
            else:
                unittest.TextTestRunner.resultclass = _ZTextTestResult

            super().__init__(*args, **kwargs)


main = TestProgram


__all__ = [
    'DocTestModule',
    'TestCase',
    'TestProgram',
    'ZFlags',
    'ZGroup',
    'ZTodo',
    'expectedFailure',
    'main',
]
