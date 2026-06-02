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
# ruff: noqa: RUF012, UP006, UP045

"""
Contains the code that could be useful for both backend and frontend build.
"""

import os
from types import TracebackType
from typing import (  # noqa: UP035 (deprecated-import)
    # We still need to use List, Set and Type here because this file
    # is imported in Python 3.6 by waf before switching to Python 3.9+.
    TYPE_CHECKING,
    Callable,
    List,
    Optional,
    Set,
    Type,
    TypeVar,
    cast,
)

import waflib
from waflib import Build, Context, Errors, Logs, Options, TaskGen, Utils
from waflib.Build import BuildContext, inst
from waflib.Configure import ConfigurationContext
from waflib.Node import Node
from waflib.Task import RUN_ME, SKIP_ME, Task, compile_fun

# Add type hinting for TaskGen decorators
if TYPE_CHECKING:
    T = TypeVar('T')

    def task_gen_decorator(*args: str) -> Callable[[T], T]: ...

    task_gen_feature = task_gen_decorator
    task_gen_after = task_gen_decorator
    task_gen_before_method = task_gen_decorator
    task_gen_after_method = task_gen_decorator
    task_gen_extension = task_gen_decorator
else:
    task_gen_feature = TaskGen.feature
    task_gen_after = TaskGen.after
    task_gen_before_method = TaskGen.before_method
    task_gen_after_method = TaskGen.after_method
    task_gen_extension = TaskGen.extension

# {{{ depends_on


def check_circular_dependencies(
    self: TaskGen, tgen: TaskGen, path: List[str], seen: Set[str]
) -> None:
    """
    Recursively check that there is no cycle in depends_on/use dependencies of
    "self" task generator.

    Waf already checks this for "use" dependencies, but not for "depends_on"
    as this is not native.

    Cycles are forbidden because it can cause undefined behaviors in the build
    system.
    """
    deps = list(tgen.to_list(getattr(tgen, 'depends_on', [])))
    deps += list(tgen.to_list(getattr(tgen, 'use', [])))

    for name in deps:
        if name in seen:
            continue
        seen.add(name)

        if name == self.name:
            raise Errors.WafError(
                'cycle detected in use/depends_on from '
                f'tgen `{self.name}`: {" -> ".join(path)}'
            )
        try:
            other = self.bld.get_tgen_by_name(name)
        except Errors.WafError:
            pass
        else:
            path.append(name)
            check_circular_dependencies(self, other, path, seen)
            path.pop()


@task_gen_feature('*')
@task_gen_before_method('process_rule')
def post_depends_on(self: TaskGen) -> None:
    """
    Post the depends_on dependencies of the "self" task generator.

    "depends_on" can be used in the definition of a task generator to define a
    list or other task generators that must be also posted when it is posted.
    Unlike "use", it does not propagate the variables, so for example a binary
    won't be linked against its "depends_on" libraries.
    """
    check_circular_dependencies(self, self, [], set())

    deps = getattr(self, 'depends_on', [])
    for name in self.to_list(deps):
        other = self.bld.get_tgen_by_name(name)
        other.post()


# }}}
# {{{ use


def check_used(self: TaskGen, name: str) -> None:
    try:
        self.bld.get_tgen_by_name(name)
        return None
    except Errors.WafError:
        pass

    # No task generator matching the name.
    # Look for a variable 'XXX_<name>' in the environment.
    sfx = '_' + name
    for var in self.env:
        if var.endswith(sfx):
            return None

    raise Errors.WafError(
        f'In task generator `{self.name}` (path={self.path}): '
        'cannot find tgen or env variable that matches '
        f'`{name}`'
    )


@task_gen_feature('*')
@task_gen_before_method('process_rule')
def check_libs(self: TaskGen) -> None:
    """
    Check that each element listed in "use" exists either as a task generator
    or as a flag set in the environment.
    """
    used = getattr(self, 'use', [])
    used = self.to_list(used)
    for name in used:
        check_used(self, name)


# }}}
# {{{ Run checks


def run_checks(ctx: BuildContext) -> None:
    env = dict(os.environ)

    if ctx.cmd == 'fast-check':
        env['Z_MODE'] = 'fast'
        env['Z_TAG_SKIP'] = 'upgrade slow perf'
    elif ctx.cmd == 'www-check':
        env['Z_LIST_SKIP'] = 'C behave'
    elif ctx.cmd == 'selenium':
        env['Z_LIST_SKIP'] = 'C web'
        env['Z_TAG_SKIP'] = 'wip'
        env['BEHAVE_FLAGS'] = '--tags=web'
    elif ctx.cmd in {'fast-selenium', 'fast-selenium-retry'}:
        env['Z_LIST_SKIP'] = 'C web'
        env['Z_TAG_SKIP'] = 'wip upgrade slow'
        env['BEHAVE_FLAGS'] = '--tags=web'
    elif ctx.cmd not in {'check', 'check-retry'}:
        return

    # CARGO/RUST tests related env vars used by the script zcargo.py
    if 'CARGO' not in env:
        env['CARGO'] = ctx.env.CARGO[0]
    env['CARGO_PROFILE'] = ctx.env.CARGO_PROFILE

    if ctx.env.USE_SANITIZER:
        env['USE_SANITIZER'] = '1'
        sanitizer = ctx.env.SANITIZER
        old_rustflags = os.environ.get('RUSTFLAGS', '')
        old_rustdocflags = os.environ.get('RUSTDOCFLAGS', '')
        env['RUSTFLAGS'] = f'-Zsanitizer={sanitizer} {old_rustflags}'
        env['RUSTDOCFLAGS'] = f'-Zsanitizer={sanitizer} {old_rustdocflags}'

    if ctx.cmd in {'check-retry', 'fast-selenium-retry'}:
        arg = ctx.cmd
    else:
        arg = ctx.launch_node().path_from(ctx.env.PROJECT_ROOT)

    cmd = f'{ctx.env.RUN_CHECKS_SH[0]} {arg}'
    if ctx.exec_command(cmd, stdout=None, stderr=None, env=env):
        ctx.fatal('')


class CheckClass(BuildContext):
    """run tests (no web)"""

    cmd = 'check'
    has_jasmine_tests = True


class CheckRetryClass(BuildContext):
    """retry failed run tests (no web)"""

    cmd = 'check-retry'


class FastCheckClass(BuildContext):
    """run tests in fast mode (no web)"""

    cmd = 'fast-check'
    has_jasmine_tests = True


class WwwCheckClass(BuildContext):
    """run jasmine tests"""

    cmd = 'www-check'
    has_jasmine_tests = True


class SeleniumCheckClass(BuildContext):
    """run selenium tests (including slow ones)"""

    cmd = 'selenium'


class FastSeleniumCheckClass(BuildContext):
    """run selenium tests (without slow ones)"""

    cmd = 'fast-selenium'


class FastSeleniumCheckRetryClass(BuildContext):
    """retry selenium tests (without slow ones)"""

    cmd = 'fast-selenium-retry'


# }}}
# {{{ Node::change_ext_src method


""" Declares the method Node.change_ext_src, which is similar to
    Node.change_ext, excepts it makes a node in the source directory (instead
    of the build directory).
"""


def node_change_ext_src(self: Node, ext: str) -> Node:
    name = self.name

    k = name.rfind('.')
    if k >= 0:
        name = name[:k] + ext
    else:
        name += ext

    return self.parent.make_node(name)


Node.change_ext_src = node_change_ext_src


# }}}
# {{{ with UseGroup statement


"""
This context manager allows using a waf group, and then restore the previous
one.

For example, this:

   with UseGroup(ctx, 'www'):
       do_something()

Is equivalent to:

   previous_group = ctx.current_group
   ctx.set_group('www')
   do_something()
   ctx.set_group(previous_group)
"""


class UseGroup:
    previous_group: int

    def __init__(self, ctx: BuildContext, group: str) -> None:
        self.ctx = ctx
        self.group = group

    def __enter__(self) -> None:
        self.previous_group = self.ctx.current_group
        self.ctx.set_group(self.group)

    def __exit__(
        self,
        exctype: Optional[Type[BaseException]],
        excinst: Optional[BaseException],
        exctb: Optional[TracebackType],
    ) -> None:
        self.ctx.set_group(self.previous_group)


# }}}
# {{{ get_env_bool


def get_env_bool(self: Context.Context, name: str) -> bool:
    val = os.environ.get(name)
    return val is not None and val.lower() in {'true', 'yes', '1'}


Context.Context.get_env_bool = get_env_bool


# }}}
# {{{ Ensure tasks are re-run when their scan method changes


def add_scan_in_signature(ctx: BuildContext) -> None:
    """
    By default in waf, tasks are not re-run when their scan method
    changes. This is an issue that caused real bugs in our project when
    switching branches.
    The purpose of this code is to take the scan method of tasks in the
    tasks signatures, like it's done for the run method.

    Note: https://gitlab.com/ita1024/waf/issues/2209 was open to fix this
          bug in waf, but it was rejected.
    """
    for task in waflib.Task.classes.values():
        if task.scan:
            task.hcode += Utils.h_fun(task.scan).encode('utf-8')


# }}}
# {{{ Install

# By default, waf will install everything that it has built on install.
# Since our build tree is a little different than what is expected by waf,
# we need to do our own install system.
#
# So we need to do two things:
# - Remove all the default install tasks.
# - Add our own custom install task that start shell commands.
#
# Usage:
#
# ctx.shlib(target='iopy2', features='c cshlib', source=[
#     'iopy.c',
#     ...
# ], depends_on='iopy-version.c',
#    custom_install=[
#     '${INSTALL} -m 444 -D iopy2.so ${PREFIX}/lib/python/iopy/iopy2.so',
#     ...
# ])
#
# ctx(name='python-scripts',
#     custom_install=[
#     ...
#     '${INSTALL} -t ${PREFIX}/lib/python/ z.py',
#     ...
# ])
#
# The environment variable ${PREFIX} is controlled by the configuration
# argument `--prefix` (default is '/usr/local/')


@task_gen_feature('*')
@task_gen_after('process_subst', 'process_rule', 'process_source')
def remove_default_install_tasks(self: TaskGen) -> None:
    """Remove all default install tasks"""
    for i, t in enumerate(self.tasks):
        if isinstance(t, inst):
            del self.tasks[i]  # noqa: B909 (loop-iterator-mutation)

    install_task = getattr(self, 'install_task', None)
    if install_task:
        del self.install_task


class CustomInstall(Task):
    """Task to start custom shell commands on install."""

    color = 'PINK'
    after = ['cprogram', 'cshlib', 'vnum']

    # pyrefly: ignore[missing-override-decorator]
    def __str__(self) -> str:
        launch_node = self.generator.bld.launch_node()
        path = self.generator.path
        return f'{path.path_from(launch_node)}/{self.generator.name}'

    @classmethod
    def keyword(cls: Type['CustomInstall']) -> str:
        return 'Installing'

    def uid(self) -> int:
        """
        Since this task has no inputs, return unique id from the commands.
        """
        res: int = Utils.h_list([self.__class__.__name__] + self.commands)
        return res

    def runnable_status(self) -> int:
        """Installation tasks are always executed on install."""
        if self.generator.bld.is_install <= 0:
            return cast(int, SKIP_ME)
        return cast(int, RUN_ME)

    def run(self) -> int:
        """Execute every shell commands in self.commands."""
        for cmd in self.commands:
            # compile_fun do the right thing by replacing the environment
            # variables in the command.
            f, _ = compile_fun(cmd, shell=True)
            ret: int = f(self)
            if ret:
                return ret
        return 0


@task_gen_feature('*')
@task_gen_after('process_rule', 'process_source')
def add_custom_install(self: TaskGen) -> None:
    commands = getattr(self, 'custom_install', None)
    if not commands:
        return
    if not isinstance(commands, list):
        commands = [commands]

    tsk = self.create_task('CustomInstall', commands=commands, cwd=self.path)
    # All tasks needs to be done before executing the custom install.
    tsk.run_after = set(self.tasks)
    tsk.run_after.remove(tsk)

    # Tasks specified with `depends_on` needs to be done before executing the
    # custom install.
    deps = getattr(self, 'depends_on', [])
    for name in self.to_list(deps):
        other = self.bld.get_tgen_by_name(name)
        for other_tsk in other.tasks:
            tsk.set_run_after(other_tsk)


# }}}
# {{{ python checkers


def _get_python_files(ctx: BuildContext) -> List[str]:
    """
    Return the file list to check: positional CLI args, or all tracked
    python and ``wscript*`` files.
    """
    # Steal the optional list of files passed as arguments.
    # Waf store the arguments in `Options.commands` and use them to run each
    # individual commands.
    # But here, the remaining arguments are not commands, but files.
    # So we need to steal the remaining arguments in `Options.commands`.
    files_args = cast(List[str], Options.commands[:])
    Options.commands.clear()

    if files_args:
        return files_args

    files_str = cast(
        str,
        ctx.cmd_and_log(
            'git ls-files "*.py" "**/*.py" "*.pyi" "**/*.pyi" '
            '"wscript*" "**/wscript*"',
            cwd=ctx.launch_node(),
            quiet=Context.BOTH,
        ),
    ).strip()
    return files_str.splitlines()


def run_python_checker(ctx: BuildContext, checker_cmd: List[str]) -> None:
    """
    Run ``checker_cmd`` on tracked Python files and ``wscript*`` files.

    ``checker_cmd`` is the argv prefix (executable plus flags); files are
    appended as additional argv entries — no shell interpolation. The
    file list is passed in a single invocation.
    """
    # Reset the build: we don't want waf to perform its normal build.
    groups: List[List[TaskGen]] = []
    ctx.groups = groups

    files_list = _get_python_files(ctx)
    if not files_list:
        return

    argv = [*checker_cmd, *files_list]
    if ctx.exec_command(
        argv, cwd=ctx.launch_node(), stdout=None, stderr=None
    ):
        ctx.fatal(f'`{checker_cmd[0]}` reported errors')


# }}}
# {{{ ruff


def run_ruff(ctx: BuildContext) -> None:
    if ctx.cmd not in {'ruff', 'ruff-fix'}:
        return

    cmd = ['ruff', 'check', '--force-exclude']
    if ctx.cmd == 'ruff-fix':
        cmd += ['--fix', '--unsafe-fixes']
    run_python_checker(ctx, cmd)


class RuffClass(BuildContext):
    """run ruff checks on committed python files"""

    cmd = 'ruff'


class RuffFixClass(BuildContext):
    """run ruff check fixes on committed python files"""

    cmd = 'ruff-fix'


# }}}
# {{{ pyrefly


def _read_pyrefly_project_excludes(pyproject_path: str) -> List[str]:
    """
    Return the `project-excludes` patterns from the `[tool.pyrefly]`
    section of ``pyproject_path``, or an empty list if absent.
    """
    # Deferred to keep `common.py` importable on Python 3.6 (waf
    # bootstrap); `waf pyrefly` only runs in the 3.9+ phase.
    try:
        import tomllib  # noqa: PLC0415
    except ImportError:
        import tomli as tomllib  # type: ignore[no-redef, import-not-found]  # noqa: PLC0415
    try:
        with open(pyproject_path, 'rb') as f:
            data = tomllib.load(f)
    except FileNotFoundError:
        return []
    pyrefly_cfg = data.get('tool', {}).get('pyrefly', {})
    return list(pyrefly_cfg.get('project-excludes', []))


def run_pyrefly(ctx: BuildContext) -> None:
    if ctx.cmd != 'pyrefly':
        return

    # `run_python_checker` passes the file list as argv, putting pyrefly
    # in single-file mode. In that mode pyrefly silently:
    #   - drops the config's `project-excludes`, so explicitly-listed
    #     files get checked even when the config excludes them (e.g.
    #     the intentionally-broken `one/ci/data/web-api/invalid.py`);
    #   - does config-finding *per-file*, which for a project that
    #     vendors `lib-common` as a submodule means lib-common's own
    #     `[tool.pyrefly]` section gets applied to files reached via
    #     import resolution. The two configs each pick their own
    #     search-path for `iopy` and the resulting two `iopy.Channel`
    #     classes fail to unify, producing spurious
    #     `Channel is not assignable to Channel` errors.
    # Workarounds:
    #   `-c <pyproject>`            pins the config and disables the
    #                               per-file config-finding;
    #   `--project-excludes <pat>`  re-applies the config's excludes,
    #                               which `-c` alone does not.
    # The excludes need to be absolute: pyrefly resolves
    # `--project-excludes` patterns against its own cwd, so a TOML
    # pattern like `one/ci/data/web-api/invalid.py` would silently fail
    # to match when waf is invoked from `one/` (cwd=one/, file shows up
    # as `ci/data/web-api/invalid.py`).
    cmd = ['pyrefly', 'check']
    pyproject_node = ctx.srcnode.find_node('pyproject.toml')
    if pyproject_node is not None:
        pyproject_path = pyproject_node.abspath()
        cmd += ['-c', pyproject_path]
        config_dir = os.path.dirname(pyproject_path)
        for pattern in _read_pyrefly_project_excludes(pyproject_path):
            cmd += ['--project-excludes', os.path.join(config_dir, pattern)]

    # Pyrefly's `project-includes` only matches `.py`/`.pyi` paths, so
    # `wscript*` files are silently skipped in project-checking mode. The
    # file list passed by `run_python_checker` includes them explicitly.
    run_python_checker(ctx, cmd)


class PyreflyClass(BuildContext):
    """run pyrefly checks on committed python files"""

    cmd = 'pyrefly'


# }}}
# {{{ git hooks

GIT_HOOKS_SRC_DIR = '/srv/tools/share/git/hooks'
GIT_HOOKS = ('pre-commit', 'prepare-commit-msg')


def install_git_hooks(ctx: ConfigurationContext) -> None:
    """
    Install pre-commit and prepare-commit-msg git hooks as symlinks.

    The hooks point at the shared scripts in /srv/tools/share/git/hooks/.
    If a hook path already exists (correct symlink, wrong symlink, or a
    regular file) it is left untouched, so devs can deploy their own hook
    if they explicitly want to.

    Works for worktrees (uses git's common dir) and for lib-common used
    either as a submodule or standalone (uses ctx.srcnode as the project
    root to query git from).
    """
    src_dir = ctx.root.make_node(GIT_HOOKS_SRC_DIR)
    if not src_dir.exists():
        return

    common_dir = ctx.cmd_and_log(
        [
            'git',
            '-C',
            ctx.srcnode.abspath(),
            'rev-parse',
            '--path-format=absolute',
            '--git-common-dir',
        ],
        output=Context.STDOUT,
        quiet=Context.BOTH,
    ).strip()

    hooks_dir = ctx.root.make_node(common_dir).make_node('hooks')
    if not hooks_dir.exists():
        return

    for hook in GIT_HOOKS:
        src = src_dir.make_node(hook)
        dst = hooks_dir.make_node(hook)

        # lexists also matches dangling symlinks; Node.exists follows them
        if os.path.lexists(dst.abspath()):
            ctx.msg(f'Git hook {hook}', 'already present, skipped')
            continue

        os.symlink(src.abspath(), dst.abspath())
        ctx.msg(f'Git hook {hook}', f'installed -> {src.abspath()}')


# }}}
# {{{ configure


def configure(ctx: ConfigurationContext) -> None:
    ctx.msg('Install prefix', ctx.env.PREFIX)

    # Find install for custom install
    ctx.find_program('install')

    # Install git hooks
    install_git_hooks(ctx)


# }}}
# {{{ build


def build(ctx: BuildContext) -> None:
    if ctx.is_install > 0:
        Logs.info('Waf: Install prefix: %s', ctx.env.PREFIX)

    # Set post_mode to POST_AT_ONCE, which allows to properly handle
    # dependencies between web task generators and IOP ones
    # cf. https://gitlab.com/ita1024/waf/issues/2191
    ctx.post_mode = Build.POST_AT_ONCE

    ctx.env.PROJECT_ROOT = ctx.srcnode
    ctx.UseGroup = UseGroup

    # Register pre/post functions
    ctx.add_pre_fun(add_scan_in_signature)
    ctx.add_pre_fun(run_ruff)
    ctx.add_pre_fun(run_pyrefly)
    ctx.add_post_fun(run_checks)


# }}}
