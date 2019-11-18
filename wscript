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

waftoolsdir = os.path.join(os.getcwd(), 'waftools')
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
    ctx.register_global_includes(['.', 'compat'])

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
    ctx.recurse('scripts')

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
                          'utils/ic-cert-default.pem',
                          'utils/ic-cert.pem')
    customize_source_file('Ichannel SSL private key',
                          'ic_key_src',
                          'utils/ic-key-default.pem',
                          'utils/ic-key.pem')

    # }}}


# }}}
# {{{ build


def build(ctx):
    # Declare 4 build groups:
    #  - one for compiling farchc
    #  - one for compiling iopc
    #  - one for compiling pxc (used in the tools repository)
    #  - one for generating/compiling code after then.
    #
    # This way we are sure farchc is generated before iopc (needed because it
    # uses a farch file), and iopc is generated before building the IOP files.
    # Refer to section "Building the compiler first" of the waf book.
    ctx.add_group('farchc')
    ctx.add_group('iopc')
    ctx.add_group('pxcc')
    ctx.add_group('code_compiling')

    load_tools(ctx)

    ctx.set_group('farchc')

    # {{{ libcommon-minimal library

    ctx(rule='${VERSION_SH} rcsid libcommon > ${TGT}',
        target='core-version.c', cwd='.', always=True)

    # This minimal version of the lib-common contains only what's needed to
    # build farchc and iopc. As a consequence, it cannot contain .fc or .iop
    # files in its sources.
    ctx.stlib(target='libcommon-minimal',
        depends_on='core-version.c',
        use=['libxml', 'valgrind', 'compat'],
        source=[
            'core-version.c',

            'src/container/qhash.c',
            'src/container/qvector.blk',
            'src/container/rbtree.c',
            'src/container/ring.c',

            'src/core/bithacks.c',
            'src/core/obj.c',
            'src/core/rand.c',
            'src/core/mem-fifo.c',
            'src/core/mem-ring.c',
            'src/core/mem-stack.c',
            'src/core/mem-bench.c',
            'src/core/errors.c',
            'src/core/mem.blk',
            'src/core/module.c',
            'src/core/types.blk',
            'src/core/str.c',
            'src/core/str-buf-gsm.c',
            'src/core/str-buf-quoting.c',
            'src/core/str-buf-pp.c',
            'src/core/str-buf.c',
            'src/core/str-conv.c',
            'src/core/str-ctype.c',
            'src/core/str-dtoa.c',
            'src/core/str-iprintf.c',
            'src/core/str-l.c',
            'src/core/str-l-obfuscate.c',
            'src/core/str-num.c',
            'src/core/str-outbuf.c',
            'src/core/str-path.c',
            'src/core/str-stream.c',

            'compat/data.c',
            'compat/runtime.c',

            'datetime.c',
            'datetime-iso8601.c',

            'el.blk',

            'farch.c',

            'hash-aes.c',
            'hash-crc32.c',
            'hash-crc64.c',
            'hash-des.c',
            'hash-hash.c',
            'hash-md5.c',
            'hash-padlock.c',
            'hash-sha1.c',
            'hash-sha2.c',
            'hash-sha4.c',

            'iop.blk',
            'iop-dso.c',
            'iop-cfolder.c',
            'iop-core-obj.blk',
            'iop-void.c',

            'log.c',

            'parseopt.c',

            'qlzo-c.c',
            'qlzo-d.c',

            'sort.blk',

            'thr.c',
            'thr-evc.c',
            'thr-job.blk',
            'thr-spsc.c',

            'unix.blk',
            'unix-fts.c',
            'unix-psinfo.c',
            'unix-linux.c',

            'xmlpp.c',
            'xmlr.c',
        ]
    )

    # }}}

    ctx.recurse([
        'src/tools',
        'src/iopc',
    ])

    ctx.set_group('code_compiling')

    # {{{ libcommon-iop / libcommon libraries

    # libcommon library containing only IOP symbols
    ctx.IopcOptions(ctx, class_range='1-499',
                    json_path='json',
                    ts_path='iop-core')
    ctx.stlib(target='libcommon-iop', features='c cstlib', source=[
        'core.iop',
        'ic.iop',
    ])

    # Full lib-common library
    ctx.stlib(target='libcommon',
        features='c cstlib',
        use=['libcommon-iop', 'libcommon-minimal', 'openssl', 'zlib'],
        source=[
            'src/arith/int.c',
            'src/arith/float.c',
            'src/arith/scan.c',

            'src/asn1/asn1.c',
            'src/asn1/writer.c',
            'src/asn1/per.c',

            'bit-buf.c',
            'bit-wah.c',

            'file.c',
            'file-bin.c',
            'file-log.blk',

            'http.c',
            'http-hdr.perf',
            'http-srv-static.c',
            'http-def.c',
            'httptokens.c',

            'yaml.c',

            'iop-json.blk',
            'iop-yaml.blk',
            'iop-rpc-channel.fc',
            'iop-rpc-channel.blk',
            'iop-rpc-http-pack.c',
            'iop-rpc-http-unpack.c',
            'iop-xml-pack.c',
            'iop-xml-unpack.c',
            'iop-xml-wsdl.blk',

            'log-iop.c',

            'net-addr.c',
            'net-sctp.c',
            'net-socket.c',
            'net-rate.blk',

            'qpage.c',
            'qps.blk',
            'qps-hat.c',
            'qps-bitmap.c',

            'ssl.blk',

            'tpl.c',
            'tpl-funcs.c',

            'z.blk',
            'zchk-helpers.blk',
            'zlib-wrapper.c',
        ]
    )

    # }}}

    ctx.recurse([
        'iop-tutorial',
        'src/pxcc',
        'src/iopy',
        'bench',
        'tests',
    ])

    # {{{ iop-snmp library

    ctx.stlib(target='iop-snmp', source=[
        'iop-snmp-doc.c',
        'iop-snmp-mib.c',
    ], use='libcommon')

    # }}}
    # {{{ dso-compatibility-check

    ctx.program(target='dso-compatibility-check', features='c cprogram',
                source='dso-compatibility-check.blk',
                use='libcommon')

    # }}}


# }}}
