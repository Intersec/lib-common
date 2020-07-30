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
# pylint: disable = invalid-name, bad-continuation

import os
import sys
import shlex

# pylint: disable = import-error
from waflib import Logs, Errors
# pylint: enable = import-error

waftoolsdir = os.path.join(os.getcwd(), 'build', 'waftools')
sys.path.insert(0, waftoolsdir)


out = ".build-waf-%s" % os.environ.get('P', 'default')


# {{{ options


def load_tools(ctx):
    ctx.load('common',  tooldir=waftoolsdir)
    ctx.load('backend', tooldir=waftoolsdir)
    for tool in getattr(ctx, 'extra_waftools', []):
        ctx.load(tool, tooldir=waftoolsdir)

    # Configure waf to re-evaluate hashes only when file timestamp/size
    # change. This is way faster on no-op builds.
    ctx.load('md5_tstamp')


def options(ctx):
    load_tools(ctx)


# }}}
# {{{ configure


def configure(ctx):
    load_tools(ctx)

    # Export includes
    ctx.register_global_includes(['.', 'src/compat'])

    # {{{ Compilation flags

    flags = ['-DHAS_LIBCOMMON_REPOSITORY=0']

    ctx.env.CFLAGS += flags
    ctx.env.CXXFLAGS += flags
    ctx.env.CLANG_FLAGS += flags
    ctx.env.CLANG_REWRITE_FLAGS += flags
    ctx.env.CLANGXX_FLAGS += flags
    ctx.env.CLANGXX_REWRITE_FLAGS += flags

    # }}}
    # {{{ Dependencies

    # Scripts
    ctx.recurse('build')

    # External programs
    ctx.find_program('gperf')

    # External libraries
    ctx.check_cfg(package='libxml-2.0', uselib_store='libxml',
                  args=['--cflags', '--libs'])
    ctx.check_cfg(package='openssl', uselib_store='openssl',
                  args=['--cflags', '--libs'])
    ctx.check_cfg(package='zlib', uselib_store='zlib',
                  args=['--cflags', '--libs'])
    ctx.check_cfg(package='valgrind', uselib_store='valgrind',
                  args=['--cflags'], mandatory=False)

    ctx.find_program('smilint', mandatory=False)
    if ctx.env.SMILINT:
        ctx.define('HAVE_SMILINT', 1)

    # {{{ Python 3

    ctx.find_program('python3')
    ctx.find_program('python3-config', var='PYTHON3_CONFIG')

    py_cflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--includes'])
    py_cflags = shlex.split(py_cflags)
    ctx.env.append_unique('CFLAGS_python3', py_cflags)

    py_ldflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--ldflags'])
    py_ldflags = shlex.split(py_ldflags)
    ctx.env.append_unique('LDFLAGS_python3', py_ldflags)

    # pylint: disable=line-too-long
    # We need to '--embed' for python 3.8+ for standalone executables.
    # See https://docs.python.org/3/whatsnew/3.8.html#debug-build-uses-the-same-abi-as-release-build
    # For python < 3.8, the cflags and ldflags are the same for both
    # shared libraries and standalone executables.
    try:
        py_embed_cflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG +
                                          ['--includes', '--embed'])
    except Errors.WafError:
        py_embed_cflags = py_cflags
    else:
        py_embed_cflags = shlex.split(py_embed_cflags)

    ctx.env.append_unique('CFLAGS_python3_embed', py_embed_cflags)

    try:
        py_embed_ldflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG +
                                           ['--ldflags', '--embed'])
    except Errors.WafError:
        py_embed_ldflags = py_ldflags
    else:
        py_embed_ldflags = shlex.split(py_embed_ldflags)

    ctx.env.append_unique('LDFLAGS_python3_embed', py_embed_ldflags)

    # }}}
    # {{{ lib clang

    # If clang is used as the C compiler, use it instead of default clang.
    if ctx.env.COMPILER_CC == 'clang':
        clang_cmd = ctx.env.CC[0]
    else:
        clang_cmd = 'clang'

    # Use -print-prog-name to get the true path of clang as it can be hidden
    # behind ccache.
    clang_bin_exe = ctx.cmd_and_log([clang_cmd, '-print-prog-name=clang'])
    clang_bin_exe = os.path.realpath(clang_bin_exe.strip())
    clang_bin_dir = os.path.dirname(clang_bin_exe)
    clang_root_dir = os.path.dirname(clang_bin_dir)

    ctx.env.append_value('LIB_clang', ['clang'])
    ctx.env.append_value('STLIBPATH_clang',
                         [os.path.join(clang_root_dir, 'lib')])
    ctx.env.append_value('RPATH_clang',
                         [os.path.join(clang_root_dir, 'lib')])
    ctx.env.append_value('INCLUDES_clang',
                         [os.path.join(clang_root_dir, 'include')])

    ctx.msg('Checking for clang lib', clang_root_dir)

    ctx.check_cc(header_name='clang-c/Index.h', use='clang',
                 errmsg='clang-c not available in clang lib, libclang-dev '
                        'may be missing', nocheck=True)

    # }}}
    # {{{ cython

    ctx.env.append_unique('CYTHONFLAGS', [
        '--warning-errors',
        '--warning-extra'
    ])
    ctx.env.CYTHONSUFFIX = '.pyx'

    # }}}

    # }}}
    # {{{ Source files customization

    # The purpose of this section is to let projects using the lib-common to
    # redefine some files.

    def customize_source_file(name, ctx_field, default_path, out_path):
        in_path = getattr(ctx, ctx_field, None)
        if in_path:
            in_node = ctx.srcnode.make_node(in_path)
        else:
            in_node = ctx.path.make_node(default_path)
        out_node = ctx.path.make_node(out_path)
        out_node.delete(evict=False)
        os.symlink(in_node.path_from(out_node.parent), out_node.abspath())
        ctx.msg(name, in_node)

    # str-l-obfuscate.c
    customize_source_file('lstr_obfuscate source file',
                          'lstr_obfuscate_src',
                          'src/core/str-l-obfuscate-default.c',
                          'src/core/str-l-obfuscate.c')

    # Ichannels SSL certificate/key
    customize_source_file('Ichannel SSL certificate',
                          'ic_cert_src',
                          'src/iop/ic-cert-default.pem',
                          'src/iop/ic-cert.pem')
    customize_source_file('Ichannel SSL private key',
                          'ic_key_src',
                          'src/iop/ic-key-default.pem',
                          'src/iop/ic-key.pem')

    # }}}


# }}}
# {{{ build


def build(ctx):
    # Declare 4 build groups:
    #  - one for generating the "version" source files
    #  - one for compiling farchc
    #  - one for compiling iopc
    #  - one for compiling pxc (used in the tools repository)
    #  - one for generating/compiling code after then.
    #
    # This way we are sure farchc is generated before iopc (needed because it
    # uses a farch file), and iopc is generated before building the IOP files.
    # Refer to section "Building the compiler first" of the waf book.
    ctx.add_group('gen_version')
    ctx.add_group('farchc')
    ctx.add_group('iopc')
    ctx.add_group('pxcc')
    ctx.add_group('code_compiling')

    load_tools(ctx)

    ctx.recurse([
        'src',
        'bench',
        'examples',
        'tests',
    ])


# }}}
