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
# ruff: noqa: UP006

"""
Contains the code needed for rust compilation.
"""

import json
import os
import os.path as osp
import stat
import subprocess
from typing import (  # noqa: UP035 (deprecated-import)
    TYPE_CHECKING,
    Callable,
    List,
    Tuple,
    Type,
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

    tgen.features = feats


def rust_pre_build(ctx: BuildContext) -> None:
    # Read the packages from the cargo metadata for the workspace.
    cargo_metadata_json = ctx.cmd_and_log(
        ctx.env.CARGO + ['metadata', '--no-deps', '--format-version', '1'],
        quiet=Context.BOTH,
    )
    cargo_metadata = json.loads(cargo_metadata_json)
    ctx.cargo_packages = {
        pkg['name']: pkg for pkg in cargo_metadata['packages']
    }

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
    def keyword(cls: Type['CargoBuild']) -> str:
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
        cargo_includes = list({
            x for key in self.env
            if key == 'INCLUDES' or key.startswith('INCLUDES_')
            for x in self.env[key]
        })

        cflags = list(self.env.CFLAGS)
        cargo_cflags = list(filter(lambda x: not x.startswith('-D'), cflags))

        defines = list(filter(lambda x: x.startswith('-D'), cflags))
        cargo_defines = list({x.removeprefix('-D') for x in defines})

        cargo_libs = (
            Utils.to_list(self.env.STLIB) +
            Utils.to_list(self.env.LIB)
        )
        cargo_libpaths = (
            Utils.to_list(self.env.STLIBPATH) +
            Utils.to_list(self.env.LIBPATH)
        )

        waf_build_env_file = self.generator.path.make_node(
            '_waf_build_env.json')

        if waf_build_env_file.exists():
            waf_build_env_file.chmod(stat.S_IWUSR)
            waf_build_env_file.delete(evict=False)

        waf_build_env_file.write_json({
            'includes': cargo_includes,
            'defines': cargo_defines,
            'cflags': cargo_cflags,
            'libs': cargo_libs,
            'libpaths': cargo_libpaths,
        })

        waf_build_env_file.chmod(stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)

    def run_cargo(self) -> None:
        cargo_exec_cmd = [
            self.env.CARGO[0],
            'build',
            '--profile',
            self.env.CARGO_PROFILE,
            *self.env.PKG_SPEC,
            *Utils.to_list(self.env.TGT_SPEC),
        ]

        # Run cargo in verbose mode if waf is run in verbose mode.
        if Logs.verbose > 0:
            cargo_exec_cmd.append('-' + 'v' * Logs.verbose)

        if self.exec_command(cargo_exec_cmd, stdout=None, stderr=None) != 0:
            raise Errors.WafError('unable to run cargo')

    def make_hardlinks(self) -> None:
        """
        Hard links the file produced by cargo to the expected output files
        """
        for waf_out, cargo_out in self.env.PKG_HARDLINKS:
            try:
                os.remove(waf_out.abspath())
            except FileNotFoundError:
                pass
            os.link(cargo_out.abspath(), waf_out.abspath())


@TaskGen.feature('rust')
@TaskGen.before_method('process_use')
def rust_build_pkg(self: TaskGen) -> None:
    ctx = self.bld

    cargo_packages = getattr(ctx, 'cargo_packages', None)
    if cargo_packages is None:
        # We are not really in the build stage, do nothing.
        return

    # Get the cargo package corresponding to the waf target
    pkg_metadata = cargo_packages.get(self.name)
    if pkg_metadata is None:
        ctx.fatal(f'waf target `{self.name}` does not correspond to a '
                  'cargo package')

    # Get manifest path and package dir
    manifest_path = pkg_metadata['manifest_path']
    package_dir = osp.dirname(manifest_path)

    # Build the list of outputs and hardlinks for the task
    cargo_bld_dir = ctx.srcnode.make_node(self.env.CARGO_BUILD_DIR)
    outputs: List[Node] = []
    hardlinks: List[Tuple[str, str]] = []
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

    self.link_task = tsk = self.create_task(
        'CargoBuild', [ctx.root.make_node(manifest_path)], outputs)
    tsk.env.PKG_DIR = package_dir
    tsk.env.PKG_HARDLINKS = hardlinks


# }}}
