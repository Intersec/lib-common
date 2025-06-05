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
import shlex
from typing import (  # noqa: UP035 (deprecated-import)
    TYPE_CHECKING,
    Callable,
    Type,
    TypeVar,
)

from waflib import Errors, Task, TaskGen, Utils
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
# {{{ rust feature


class CargoBuild(Task.Task):  # type: ignore[misc]
    always_run = True
    color = 'PINK'

    @classmethod
    def keyword(cls: Type['CargoBuild']) -> str:
        return 'Cargo'

    def run(self) -> int:
        bld = self.generator.bld
        bld_env = bld.env

        cargo_includes = list({
            x for key in bld_env
            if key == 'INCLUDES' or key.startswith('INCLUDES_')
            for x in bld_env[key]
        })

        cflags = list(bld_env.CFLAGS)
        cargo_cflags = list(filter(lambda x: not x.startswith('-D'), cflags))

        defines = list(filter(lambda x: x.startswith('-D'), cflags))
        cargo_defines = list({x.removeprefix('-D') for x in defines})

        env = dict(self.env.env or os.environ)
        env['WAFCARGO'] = '1'
        env['WAFCARGO_WAF_PROFILE'] = bld_env.PROFILE
        env['WAFCARGO_CARGO_PROFILE'] = bld_env.CARGO_PROFILE
        env['WAFCARGO_INCLUDES'] = shlex.join(cargo_includes)
        env['WAFCARGO_DEFINES'] = shlex.join(cargo_defines)
        env['WAFCARGO_CFLAGS'] = shlex.join(cargo_cflags)

        cargo_exec_cmd = [
            bld_env.CARGO[0],
            'build',
            '--profile',
            bld_env.CARGO_PROFILE,
            *self.env.PKG_SPEC,
            *Utils.to_list(self.env.TGT_SPEC),
        ]
        res: int = self.exec_command(cargo_exec_cmd, env=env)
        if res != 0:
            return res

        # Hard link the file produced by cargo to the expected output file
        waf_out, cargo_out = self.outputs
        try:
            os.remove(waf_out.abspath())
        except FileNotFoundError:
            pass
        os.link(cargo_out.abspath(), waf_out.abspath())

        return 0


@TaskGen.feature('rust')
@TaskGen.before_method('process_rule')
def check_rust(tg: TaskGen) -> None:
    # FIXME
    feats = Utils.to_list(tg.features)
    if 'ruststlib' not in feats and tg.typ == 'stlib':
        feats.append('ruststlib')
    elif 'rustshlib' not in feats and tg.typ == 'shlib':
        feats.append('rustshlib')
    elif 'rustprogram' not in feats and tg.typ == 'program':
        feats.append('rustprogram')
    else:
        return
    tg.features = feats


@TaskGen.feature('ruststlib', 'rustshlib', 'rustprogram')
@TaskGen.before_method('process_source')
def check_rust_source(tg: TaskGen) -> None:
    source = Utils.to_list(getattr(tg, 'source', []))
    if source:
        raise Errors.WafError(f'rust task gen {tg.name}: '
                              'source must be empty')

    cargo_pkg = getattr(tg, 'cargo_pkg', None)
    if not cargo_pkg:
        raise Errors.WafError(f'rust task gen {tg.name}: '
                              '`cargo_pkg` must be set to a corresponding '
                              'cargo package')


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
@TaskGen.after_method('cargo_pkg_add_lib')
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
