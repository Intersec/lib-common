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
Contains the code needed for rust compilation.
"""

from __future__ import annotations

import json
import os
import os.path as osp
import stat
import subprocess
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    TypeVar,
)

from waflib import Context, Errors, Logs, Node, Task, TaskGen, Utils
from waflib.Build import BuildContext, CleanContext
from waflib.Configure import ConfigurationContext

# Add type hinting for TaskGen decorators
if TYPE_CHECKING:
    T = TypeVar('T')

    def task_gen_decorator(*args: str) -> Callable[[T], T]: ...

    TaskGen.feature = task_gen_decorator
    TaskGen.before_method = task_gen_decorator
    TaskGen.after_method = task_gen_decorator
    TaskGen.extension = task_gen_decorator


# {{{ configure


def configure(ctx: ConfigurationContext) -> None:
    ctx.find_program('cargo', var='CARGO')

    if ctx.exec_command(ctx.env.CARGO + ['tree', '--quiet', '--locked'],
                        stdout=subprocess.DEVNULL, stderr=None):
        ctx.fatal('cargo lock is not up-to-date')

    waf_profile = ctx.env.PROFILE
    ctx.env.CARGO_PROFILE = 'release' if waf_profile == 'release' else 'dev'
    cargo_target_dir = 'release' if waf_profile == 'release' else 'debug'
    ctx.env.CARGO_BUILD_DIR = osp.join('.cargo', 'target', cargo_target_dir)


# }}}
# {{{ build


def rust_set_features(tgen: TaskGen, feats: list[str]) -> None:
    # Required for STLIB dependencies
    if 'use' not in feats:
        feats.append('use')

    # Required for USELIB flags dependencies
    if 'uselib' not in feats:
        feats.append('uselib')

    # Required for INCPATHS variable
    if 'includes' not in feats:
        feats.append('includes')

    tgen.features = feats


def rust_resolve_local_recursive_dependencies(
    ctx: BuildContext, cargo_package: dict[str, Any],
) -> dict[str, str]:
    local_recursive_dependencies: dict[str, str] | None = (
        cargo_package.get('local_recursive_dependencies')
    )
    if local_recursive_dependencies is not None:
        return local_recursive_dependencies

    local_recursive_dependencies = {}
    for dep in cargo_package['dependencies']:
        dep_path = dep.get('path')
        if dep_path is None:
            # Not a local dependency
            continue

        dep_name = dep['name']

        # Add the local dependency
        local_recursive_dependencies[dep_name] = dep_path

        dep_package = ctx.cargo_packages.get(dep_name)
        if dep_package is None:
            # Not a real dependency?
            raise AssertionError
            continue

        # Also add their dependencies recursively
        local_recursive_dependencies.update(
            rust_resolve_local_recursive_dependencies(ctx, dep_package),
        )

    cargo_package['local_recursive_dependencies'] = (
        local_recursive_dependencies
    )
    return local_recursive_dependencies


def rust_resolve_all_local_recursive_dependencies(ctx: BuildContext) -> None:
    for cargo_package in ctx.cargo_packages.values():
        rust_resolve_local_recursive_dependencies(ctx, cargo_package)


def rust_pre_build(ctx: BuildContext) -> None:
    # Read the packages from the cargo metadata for the workspace.
    cargo_metadata_json = ctx.cmd_and_log(
        ctx.env.CARGO + ['metadata', '--format-version', '1'],
        quiet=Context.STDOUT,
    )
    cargo_metadata = json.loads(cargo_metadata_json)
    ctx.cargo_packages = {
        pkg['name']: pkg for pkg in cargo_metadata['packages']
    }
    rust_resolve_all_local_recursive_dependencies(ctx)

    for tgen in ctx.get_all_task_gen():
        feats = Utils.to_list(getattr(tgen, 'features', []))
        if 'rust' in feats:
            rust_set_features(tgen, feats)


def cargo_clean(ctx: CleanContext) -> None:
    """Perform `cargo clean` on `waf clean`"""
    Logs.info('Waf: running `cargo clean`')
    ctx.exec_command(ctx.env.CARGO + ['clean'], stdout=None, stderr=None)


def build(ctx: BuildContext) -> None:
    # CleanContext is a subclass of BuildContext, and build() is called on
    # `waf clean` but without running the compilation tasks, and do an
    # internal clean instead.
    # Run the command now as we cannot actually run it later.
    if isinstance(ctx, CleanContext):
        cargo_clean(ctx)
        return

    ctx.add_pre_fun(rust_pre_build)


# }}}
# {{{ rust feature


class CargoBuild(Task.Task):  # type: ignore[misc]
    always_run = True
    color = 'PINK'

    @classmethod
    def keyword(cls: type[CargoBuild]) -> str:
        return 'Cargo'

    def __str__(self) -> str:
        ctx = self.generator.bld
        pkg_dir_node = ctx.root.make_node(self.env.PKG_DIR)
        res: str = pkg_dir_node.path_from(ctx.srcnode)
        return res

    def run(self) -> int:
        self.make_waf_build_env()
        self.run_cargo()
        self.make_hardlinks()
        return 0

    def make_waf_build_env(self) -> None:
        tg = self.generator
        ctx = tg.bld

        cargo_package = ctx.cargo_packages[self.env.PKG_NAME]

        incpaths = Utils.to_list(self.env.INCPATHS)
        cargo_includes = [
            incdir if osp.isabs(incdir)
            else ctx.srcnode.make_node(incdir).abspath()
            for incdir in incpaths
        ]

        cflags = Utils.to_list(self.env.CFLAGS)
        cargo_cflags = [x for x in cflags if not x.startswith('-D')]

        defines = [x for x in cflags if x.startswith('-D')]
        cargo_defines = sorted({x.removeprefix('-D') for x in defines})

        dep_stlibs = []
        for use_stlib in tg.tmp_use_sorted:  # Result of sorted use libs
            dep_tg = ctx.get_tgen_by_name(use_stlib)
            link_task = getattr(dep_tg, 'link_task', None)
            if link_task is None or not link_task.outputs:
                continue
            dep_stlibs.append(link_task.outputs[0].abspath())

        cargo_libs = (
            Utils.to_list(self.env.STLIB) +
            Utils.to_list(self.env.LIB)
        )
        cargo_libpaths = (
            Utils.to_list(self.env.STLIBPATH) +
            Utils.to_list(self.env.LIBPATH)
        )
        cargo_rerun_libs = sorted(dep_stlibs)
        cargo_link_args = Utils.to_list(self.env.LDFLAGS).copy()

        if self.env.USE_SANITIZER:
            if self.env.PROFILE == 'asan':
                cargo_link_args.append('-static-libasan')
            # Needs to always be the first library to be linked
            cargo_libs.insert(0, self.env.PROFILE)

        waf_env_content = {
            'includes': cargo_includes,
            'defines': cargo_defines,
            'cflags': cargo_cflags,
            'libs': cargo_libs,
            'libpaths': cargo_libpaths,
            'link_args': cargo_link_args,
            'rerun_libs': cargo_rerun_libs,
            'cc': self.env.CC[0],
            'local_recursive_dependencies': (
                cargo_package['local_recursive_dependencies']
            ),
        }

        waf_build_env_file = self.generator.path.make_node(
            '_waf_build_env.json')

        if waf_build_env_file.exists():
            # If the file already exists and the content has not changed, do
            # not rewrite the file to avoid recompiling the cargo package
            old_content = waf_build_env_file.read_json()
            if waf_env_content == old_content:
                return

            waf_build_env_file.chmod(stat.S_IWUSR)
            waf_build_env_file.delete(evict=False)

        waf_build_env_file.write_json(waf_env_content)
        waf_build_env_file.chmod(stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)

    def run_cargo(self) -> None:
        if not self.outputs:
            # The Rust package will not produce any of output handled by waf
            # (not a stlib, shlib or bin), it refers to a Rust only library.
            # In that case, do not run `cargo build` as this package will be
            # used by other cargo packages as a dependency.
            return

        cargo_exec_cmd = [
            self.env.CARGO[0],
            'build',
            '--profile',
            self.env.CARGO_PROFILE,
            '-p',
            self.env.PKG_NAME,
        ]

        # Run cargo in verbose mode if waf is run in verbose mode.
        if Logs.verbose > 0:
            cargo_exec_cmd.append('-' + 'v' * Logs.verbose)

        if self.exec_command(cargo_exec_cmd, stdout=None, stderr=None) != 0:
            raise Errors.WafError('unable to run cargo')

    def make_hardlinks(self) -> None:
        """
        Hard link the files produced by cargo to the expected output files
        """
        for waf_out, cargo_out in self.env.PKG_HARDLINKS:
            try:
                os.remove(waf_out.abspath())
            except FileNotFoundError:
                pass
            os.link(cargo_out.abspath(), waf_out.abspath())


@TaskGen.feature('rust')
@TaskGen.before_method('process_use')
def rust_create_task(self: TaskGen) -> None:
    ctx = self.bld

    cargo_packages = getattr(ctx, 'cargo_packages', None)
    if cargo_packages is None:
        # We are not really in the build stage, do nothing.
        return

    # Get the cargo package name to use
    cargo_pkg_name = getattr(self, 'cargo_package', self.name)

    # Get the cargo package corresponding to the waf target
    pkg_metadata = cargo_packages.get(cargo_pkg_name)
    if pkg_metadata is None:
        ctx.fatal(f'waf target `{cargo_pkg_name}` does not correspond to a '
                  'cargo package')

    # Get manifest path and package dir
    manifest_path = pkg_metadata['manifest_path']
    package_dir = osp.dirname(manifest_path)

    # Build the list of outputs and hardlinks for the task
    cargo_bld_dir = ctx.srcnode.make_node(self.env.CARGO_BUILD_DIR)
    outputs: list[Node] = []
    hardlinks: list[tuple[str, str]] = []
    for cargo_target in pkg_metadata['targets']:
        target_name = cargo_target['name']
        kinds = cargo_target['kind']

        # Add the rust libs compiled as a static lib
        if 'staticlib' in kinds:
            target_output_name = ctx.env.cstlib_PATTERN % target_name
            cargo_output = cargo_bld_dir.make_node(target_output_name)
            outputs.append(cargo_output)

        # Add the rust libs compiled as a shared lib
        if 'cdylib' in kinds:
            target_output_name = ctx.env.cshlib_PATTERN % target_name
            cargo_output = cargo_bld_dir.make_node(target_output_name)

            waf_target_name = target_output_name
            if not getattr(self, 'keep_lib_prefix', False):
                assert waf_target_name.startswith('lib')
                waf_target_name = waf_target_name[len('lib'):]
            waf_output = self.path.make_node(waf_target_name)

            outputs.extend([waf_output, cargo_output])
            hardlinks.append((waf_output, cargo_output))

        # Add the rust bin
        if 'bin' in kinds:
            target_output_name = ctx.env.cprogram_PATTERN % target_name
            cargo_output = cargo_bld_dir.make_node(target_output_name)
            waf_output = self.path.make_node(target_output_name)
            outputs.extend([waf_output, cargo_output])
            hardlinks.append((waf_output, cargo_output))

    # `link_task` is required for use lib links in waf.
    self.link_task = self.rust_task = tsk = self.create_task(
        'CargoBuild', [ctx.root.make_node(manifest_path)], outputs)
    tsk.env.PKG_DIR = package_dir
    tsk.env.PKG_NAME = cargo_pkg_name
    tsk.env.PKG_HARDLINKS = hardlinks


@TaskGen.feature('rust')
@TaskGen.after_method('process_use')
def rust_add_dep_task(self: TaskGen) -> None:
    """
    Add task dependencies between rust task gen even if the waf rust task gen
    does not produce an output.
    """
    ctx = self.bld

    # Get the link task
    link_task = getattr(self, 'link_task', None)
    if link_task is None:
        # We are not really in the build stage, do nothing.
        return

    if not link_task.outputs:
        # If the task do not produce any output (rust lib), delete `link_task`
        # to avoid a waf exception later on with some invalid ouputs.
        del self.link_task

    for use_stlib in self.tmp_use_sorted:  # Result of sorted use libs
        dep_tg = ctx.get_tgen_by_name(use_stlib)
        dep_rust_task = getattr(dep_tg, 'rust_task', None)
        if dep_rust_task is None:
            # Not a rust task generator
            continue

        self.rust_task.set_run_after(dep_rust_task)


# }}}
