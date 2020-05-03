###########################################################################
#                                                                         #
# Copyright 2020 INTERSEC SA                                              #
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

'''
Contains the code that could be useful for both backend and frontend build.
'''

import os

# pylint: disable = import-error
import waflib
from waflib import Build, Context, TaskGen, Logs, Utils

from waflib.Build import BuildContext, inst
from waflib.Node import Node
from waflib.Task import Task, compile_fun, SKIP_ME, RUN_ME
# pylint: enable = import-error

# {{{ depends_on


@TaskGen.feature('*')
@TaskGen.before_method('process_rule')
def post_deps(self):
    deps = getattr(self, 'depends_on', [])
    for name in self.to_list(deps):
        other = self.bld.get_tgen_by_name(name)
        other.post()


# }}}
# {{{ Run checks


def run_checks(ctx):
    env = dict(os.environ)

    if ctx.cmd == 'fast-check':
        env['Z_MODE']     = 'fast'
        env['Z_TAG_SKIP'] = 'upgrade slow perf'
    elif ctx.cmd == 'www-check':
        env['Z_LIST_SKIP'] = 'C behave'
    elif ctx.cmd == 'selenium':
        env['Z_LIST_SKIP']  = 'C web'
        env['Z_TAG_SKIP']   = 'wip'
        env['BEHAVE_FLAGS'] = '--tags=web'
    elif ctx.cmd == 'fast-selenium':
        env['Z_LIST_SKIP']  = 'C web'
        env['Z_TAG_SKIP']   = 'wip upgrade slow'
        env['BEHAVE_FLAGS'] = '--tags=web'
    elif ctx.cmd != 'check':
        return

    path = ctx.launch_node().path_from(ctx.env.PROJECT_ROOT)
    cmd = '{0} {1}'.format(ctx.env.RUN_CHECKS_SH[0], path)
    if ctx.exec_command(cmd, stdout=None, stderr=None, env=env):
        ctx.fatal('')

class CheckClass(BuildContext):
    '''run tests (no web)'''
    cmd = 'check'
    has_jasmine_tests = True

class FastCheckClass(BuildContext):
    '''run tests in fast mode (no web)'''
    cmd = 'fast-check'
    has_jasmine_tests = True

class WwwCheckClass(BuildContext):
    '''run jasmine tests'''
    cmd = 'www-check'
    has_jasmine_tests = True

class SeleniumCheckClass(BuildContext):
    '''run selenium tests (including slow ones)'''
    cmd = 'selenium'

class FastSeleniumCheckClass(BuildContext):
    '''run selenium tests (without slow ones)'''
    cmd = 'fast-selenium'


# }}}
# {{{ Node::change_ext_src method


''' Declares the method Node.change_ext_src, which is similar to
    Node.change_ext, excepts it makes a node in the source directory (instead
    of the build directory).
'''

def node_change_ext_src(self, ext):
    name = self.name

    k = name.rfind('.')
    if k >= 0:
        name = name[:k] + ext
    else:
        name = name + ext

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

    def __init__(self, ctx, group):
        self.ctx = ctx
        self.group = group

    def __enter__(self):
        self.previous_group = self.ctx.current_group
        self.ctx.set_group(self.group)

    def __exit__(self, exc_type, exc_value, traceback):
        self.ctx.set_group(self.previous_group)


# }}}
# {{{ get_env_bool


def get_env_bool(self, name):
    val = os.environ.get(name, 0)
    if isinstance(val, str):
        return val.lower() in ['true', 'yes', '1']
    else:
        return int(val) == 1


Context.Context.get_env_bool = get_env_bool


# }}}
# {{{ Ensure tasks are re-run when their scan method changes


def add_scan_in_signature(ctx):
    ''' By default in waf, tasks are not re-run when their scan method
        changes. This is an issue that caused real bugs in our project when
        switching branches.
        The purpose of this code is to take the scan method of tasks in the
        tasks signatures, like it's done for the run method.

        Note: https://gitlab.com/ita1024/waf/issues/2209 was open to fix this
              bug in waf, but it was rejected.
    '''
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

@TaskGen.feature('*')
@TaskGen.after('process_subst', 'process_rule', 'process_source')
def remove_default_install_tasks(self):
    """ Remove all default install tasks """
    for i, t in enumerate(self.tasks):
        if isinstance(t, inst):
            del self.tasks[i]

    install_task = getattr(self, 'install_task', None)
    if install_task:
        del self.install_task


class CustomInstall(Task):
    """ Task to start custom shell commands on install. """
    color = 'PINK'
    after = ['DeployTarget', 'cprogram', 'cshlib', 'vnum']

    def __str__(self):
        launch_node = self.generator.bld.launch_node()
        path = self.generator.path
        return '%s/%s' % (path.path_from(launch_node), self.generator.name)

    @classmethod
    def keyword(cls):
        return 'Installing'

    def uid(self):
        """ Since this task has no inputs, return unique id from the commands.
        """
        return Utils.h_list([self.__class__.__name__] + self.commands)

    def runnable_status(self):
        """ Installation tasks are always executed on install. """
        if self.generator.bld.is_install <= 0:
            return SKIP_ME
        return RUN_ME

    def run(self):
        """ Execute every shell commands in self.commands. """
        for cmd in self.commands:
            # compile_fun do the right thing by replacing the environment
            # variables in the command.
            f, _ = compile_fun(cmd, shell=True)
            ret = f(self)
            if ret:
                return ret
        return 0


@TaskGen.feature('*')
@TaskGen.after('process_rule', 'process_source')
def add_custom_install(self):
    commands = getattr(self, 'custom_install', None)
    if not commands:
        return
    if not isinstance(commands, list):
        commands = [commands]

    tsk = self.create_task('CustomInstall', commands=commands, cwd=self.path)
    # All tasks needs to be done before executing the custom install.
    tsk.run_after = set(self.tasks)
    tsk.run_after.remove(tsk)


# }}}
# {{{ pylint


def run_pylint(ctx):
    if ctx.cmd != 'pylint':
        return

    # Reset the build
    ctx.groups = []

    # Get list of committed python files under the launch directory
    path = ctx.launch_node()
    files = ctx.cmd_and_log('git ls-files "*.py" "**/*.py"', cwd=path,
                            quiet=Context.BOTH).strip()

    # Create tasks to check them using pylint
    for f in files.splitlines():
        node = path.make_node(f)
        ctx(rule='pylint ${SRC}', source=node, path=path, always=True)


class PylintClass(BuildContext):
    '''run pylint checks on committed python files'''
    cmd = 'pylint'


# }}}
# {{{ APP delivery


def app_delivery(ctx):
    if ctx.cmd != 'app-delivery':
        return

    # Create temporary working directory
    tmp_dir = ctx.bldnode.make_node('app-delivery')
    tmp_dir.delete(evict=False)
    tmp_dir.mkdir()

    # Build archive name and node
    cur_branch = ctx.cmd_and_log('git rev-parse --abbrev-ref HEAD',
                                 quiet=Context.BOTH, cwd=ctx.srcnode).strip()
    cur_rev = ctx.cmd_and_log('git rev-parse HEAD',
                              quiet=Context.BOTH, cwd=ctx.srcnode).strip()
    archive_name = '{0}-{1}'.format(cur_branch, cur_rev)
    archive_node = tmp_dir.make_node(archive_name + '.tar')

    # Create main archive
    print('Creating main archive...')
    cmd = 'git archive --prefix=intersec-{0}/ -o {1} HEAD'
    ctx.cmd_and_log(cmd.format(archive_name, archive_node.abspath()),
                    quiet=Context.BOTH, cwd=ctx.srcnode)

    # Concatenate it with archives of each submodule
    cmd = 'git submodule foreach --recursive | cut -d"\'" -f2'
    submodules = ctx.cmd_and_log(cmd, quiet=Context.BOTH, cwd=ctx.srcnode)
    for sub_path in submodules.strip().splitlines():
        print('Appending {0} submodule archive...'.format(sub_path))

        sub_name = sub_path.replace('/', '.')
        sub_archive_name = 'submodule-tmp-{0}.tar'.format(sub_name)
        sub_archive_node = tmp_dir.make_node(sub_archive_name)
        submodule_node = ctx.srcnode.make_node(sub_path)

        # Build temporary submodule archive
        cmd = 'git archive --prefix=intersec-{0}/{1}/ -o {2} HEAD'
        cmd = cmd.format(archive_name, sub_path, sub_archive_node.abspath())
        ctx.cmd_and_log(cmd, quiet=Context.BOTH, cwd=submodule_node)

        # Concatenate it with the main archive
        cmd = 'tar --concatenate --file={0} {1}'
        cmd = cmd.format(archive_node.abspath(), sub_archive_node.abspath())
        ctx.cmd_and_log(cmd, quiet=Context.BOTH)

        # Remove the temporary submodule archive
        sub_archive_node.delete()

    # Compress it
    print('Compressing main archive...')
    cmd = 'bzip2 {0}'.format(archive_node.abspath())
    ctx.cmd_and_log(cmd, quiet=Context.BOTH)
    archive_node = archive_node.change_ext('.tar.bz2')

    # Move it in the source directory
    final_node = ctx.srcnode.make_node(archive_node.name)
    os.rename(archive_node.abspath(), final_node.abspath())

    print('APP archive ready: {0}'.format(final_node.abspath()))
    print('Now, move it to papyrus and proceed to the APP delivery')

    # Clean temporary directory
    tmp_dir.delete()

    # Interrupt the build
    ctx.groups = []


class AppDelivery(BuildContext):
    '''prepare an APP delivery (archive of the source code)'''
    cmd = 'app-delivery'


# }}}

# {{{ configure


def configure(ctx):
    ctx.msg('Install prefix', ctx.env.PREFIX)

    # Find install for custom install
    ctx.find_program('install')


# }}}
# {{{ build

def build(ctx):
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
    ctx.add_pre_fun(run_pylint)
    ctx.add_pre_fun(app_delivery)
    ctx.add_post_fun(run_checks)


# }}}
