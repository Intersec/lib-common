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
# pylint: disable = invalid-name, bad-continuation

import os
import sys

# pylint: disable = import-error
from waflib import Context, Logs, Errors
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
                  args=['--cflags'])

    # Linux UAPI SCTP header
    sctp_h = '/usr/include/linux/sctp.h'
    if os.path.exists(sctp_h):
        sctp_flag = '-DHAVE_LINUX_UAPI_SCTP_H'
        ctx.env.CFLAGS.append(sctp_flag)
        ctx.env.CLANG_FLAGS.append(sctp_flag)
        ctx.env.CLANG_REWRITE_FLAGS.append(sctp_flag)
        ctx.msg('Checking for Linux UAPI SCTP header', sctp_h)
    else:
        Logs.info('missing Linux UAPI SCTP header,'
                  ' it will be replaced by a custom one')

    # {{{ Python 2

    # TODO waf: use waf python tool for that?
    ctx.find_program('python2')

    # Check version is >= 2.6
    py_ver = ctx.cmd_and_log(ctx.env.PYTHON2 + ['--version'],
                             output=Context.STDERR)
    py_ver = py_ver.strip()[len('Python '):]
    py_ver_minor = int(py_ver.split('.')[1])
    if py_ver_minor not in [6, 7]:
        ctx.fatal('unsupported python version {0}'.format(py_ver))

    # Get compilation flags
    if py_ver_minor == 6:
        ctx.find_program('python2.6-config', var='PYTHON2_CONFIG')
    else:
        ctx.find_program('python2.7-config', var='PYTHON2_CONFIG')

    py_cflags = ctx.cmd_and_log(ctx.env.PYTHON2_CONFIG + ['--includes'])
    ctx.env.append_unique('CFLAGS_python2', py_cflags.strip().split(' '))

    py_ldflags = ctx.cmd_and_log(ctx.env.PYTHON2_CONFIG + ['--ldflags'])
    ctx.env.append_unique('LDFLAGS_python2', py_ldflags.strip().split(' '))

    # }}}
    # {{{ Python 3

    try:
        ctx.find_program(['python3-config', 'python3.6-config'],
                         var='PYTHON3_CONFIG')
    except Errors.ConfigurationError as e:
        Logs.debug('cannot configure python3: %s', e.msg)
    else:
        py_cflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--includes'])
        ctx.env.append_unique('CFLAGS_python3', py_cflags.strip().split(' '))

        py_ldflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--ldflags'])
        ctx.env.append_unique('LDFLAGS_python3',
                              py_ldflags.strip().split(' '))

    # }}}
    # {{{ lib clang

    clang_format = ctx.find_program('clang-format')
    clang_real_path = os.path.realpath(clang_format[0])
    clang_root_dir = os.path.realpath(os.path.join(clang_real_path, '../..'))

    ctx.env.append_value('LIB_clang', ['clang'])
    ctx.env.append_value('STLIBPATH_clang', [clang_root_dir + '/lib'])
    ctx.env.append_value('RPATH_clang', [clang_root_dir + '/lib'])
    ctx.env.append_value('INCLUDES_clang', [clang_root_dir + '/include'])

    ctx.msg('Checking for clang lib', clang_root_dir)

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
