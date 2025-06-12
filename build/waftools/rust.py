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


import os
import os.path as osp
import stat
from typing import (  # noqa: UP035 (deprecated-import)
    TYPE_CHECKING,
    Callable,
    Type,
    TypeVar,
)

from waflib import Errors, Logs, Task, TaskGen, Utils
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

    waf_profile = ctx.env.PROFILE
    ctx.env.CARGO_PROFILE = 'release' if waf_profile == 'release' else 'dev'
    cargo_target_dir = 'release' if waf_profile == 'release' else 'debug'
    ctx.env.CARGO_BUILD_DIR = osp.join('.cargo', 'target', cargo_target_dir)


# }}}
# {{{ build


def rust_check_tgen(tgen: TaskGen) -> None:
    source = Utils.to_list(getattr(tgen, 'source', []))
    if source:
        raise Errors.WafError(f'rust task gen {tgen.name}: '
                              'source must be empty')

    cargo_pkg = getattr(tgen, 'cargo_pkg', None)
    if not cargo_pkg:
        raise Errors.WafError(f'rust task gen {tgen.name}: '
                              '`cargo_pkg` must be set to a corresponding '
                              'cargo package')


def rust_set_features(tgen: TaskGen, feats: list[str]) -> None:
    if tgen.typ == 'stlib' and 'ruststlib' not in feats:
        feats.append('ruststlib')
    elif tgen.typ == 'shlib' and 'rustshlib' not in feats:
        feats.append('rustshlib')
    elif tgen.typ == 'program' and 'rustprogram' not in feats:
        feats.append('rustprogram')
    else:
        raise Errors.WafError('unsupported kind of rust task gen '
                              f'{tgen.name}')

    if 'use' not in feats:
        feats.append('use')

    tgen.features = feats


def rust_pre_build(ctx: BuildContext) -> None:
    for tgen in ctx.get_all_task_gen():
        feats = Utils.to_list(getattr(tgen, 'features', []))
        if 'rust' in feats:
            rust_check_tgen(tgen)
            rust_set_features(tgen, feats)


@TaskGen.feature('rust')
@TaskGen.before_method('process_rule')
def rust_dummy_feature(tg: TaskGen) -> None:
    """
    Dummy 'rust' feature method so that waf does not complain it does not
    exist.
    """


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

    def run(self) -> int:
        self.make_waf_build_env()
        self.run_cargo()
        self.make_hard_link()
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
            os.chmod(waf_build_env_file.abspath(), stat.S_IWUSR)
            os.remove(waf_build_env_file.abspath())

        waf_build_env_file.write_json({
            'includes': cargo_includes,
            'defines': cargo_defines,
            'cflags': cargo_cflags,
            'libs': cargo_libs,
            'libpaths': cargo_libpaths,
        })
        os.chmod(waf_build_env_file.abspath(),
                 stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)

    def run_cargo(self) -> None:
        cargo_exec_cmd = [
            self.env.CARGO[0],
            'build',
            '--profile',
            self.env.CARGO_PROFILE,
            *self.env.PKG_SPEC,
            *Utils.to_list(self.env.TGT_SPEC),
        ]
        if self.exec_command(cargo_exec_cmd) != 0:
            raise Errors.WafError('unable to run cargo')

    def make_hard_link(self) -> None:
        """Hard link the file produced by cargo to the expected output file"""
        waf_out, cargo_out = self.outputs
        try:
            os.remove(waf_out.abspath())
        except FileNotFoundError:
            pass
        os.link(cargo_out.abspath(), waf_out.abspath())


@TaskGen.feature('rustprogram')
@TaskGen.after_method('process_source')
def apply_cargo_build_program(self: TaskGen) -> None:
    waf_out = self.path.make_node(self.name)
    cargo_bld_dir = self.bld.srcnode.make_node(self.env.CARGO_BUILD_DIR)
    cargo_out = cargo_bld_dir.make_node(self.name)
    self.link_task = tsk = self.create_task(
        'CargoBuild', [], [waf_out, cargo_out])
    tsk.env.PKG_SPEC = ['-p', self.cargo_pkg]
    tsk.env.TGT_SPEC = ['--bin', self.name]


@TaskGen.feature('rustlib')
@TaskGen.after_method('process_source')
def apply_cargo_build_lib(self: TaskGen) -> None:
    # FIXME
    bld_dir = self.bld.srcnode.make_node(self.env.CARGO_BUILD_DIR)
    src = self.main_src

    outputs = []
    for typ in self.crate_type:
        if typ == 'rlib':
            p = 'lib%s.rlib'
        elif typ in {'dylib', 'cdylib'}:
            p = self.bld.env.cshlib_PATTERN
        elif typ == 'staticlib':
            p = self.bld.env.cstlib_PATTERN
        else:
            raise AssertionError

        outputs.append(bld_dir.make_node(p % self.target_lib))

    self.cargo_task = tsk = self.create_task('CargoBuild', [src], outputs)
    tsk.env.PKG_SPEC = ['-p', self.pkg]
    tsk.env.TGT_SPEC = '--lib'


# }}}
