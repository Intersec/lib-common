###########################################################################
#                                                                         #
# Copyright 2022 INTERSEC SA                                              #
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
import os.path as osp
import sys
import shlex
import shutil

# pylint: disable = import-error
from waflib import Logs, Errors, Options
# pylint: enable = import-error

waftoolsdir = os.path.join(os.getcwd(), 'build', 'waftools')
sys.path.insert(0, waftoolsdir)


out = ".build-waf-%s" % os.environ.get('P', 'default')

# {{{ helpers


def remove_prefix(text, prefix):
    if text.startswith(prefix):
        return text[len(prefix):]
    return text



def load_tools(ctx):
    ctx.load('common', tooldir=waftoolsdir)
    ctx.load('backend', tooldir=waftoolsdir)
    for tool in getattr(ctx, 'extra_waftools', []):
        ctx.load(tool, tooldir=waftoolsdir)

    # Configure waf to re-evaluate hashes only when file timestamp/size
    # change. This is way faster on no-op builds.
    ctx.load('md5_tstamp')


# }}}
# {{{ poetry


def run_waf_with_poetry(ctx):
    Logs.info('Waf: Run waf in poetry environment')

    # Force POETRY_ACTIVE as older versions of poetry don't set it on
    # `poetry run`, but set it on `poetry shell`.
    os.environ['POETRY_ACTIVE'] = '1'

    exit_code = ctx.exec_command(ctx.env.POETRY + ['run'] + sys.argv,
                                 stdout=None, stderr=None)
    if exit_code != 0:
        sys.exit(exit_code)

    del os.environ['POETRY_ACTIVE']


def poetry_fix_no_env_use(ctx):
    py_short_version_lines = ctx.cmd_and_log(
        ctx.env.POETRY + ["run", "python3", "-c",
        (
            "import sys;"
            "print(f'{sys.version_info[0]}.{sys.version_info[1]}')"
        )
    ]).strip().split('\n')

    if len(py_short_version_lines) == 1:
        # Poetry environment is well defined or python default version is
        # compatible with the poetry configuration, do nothing.
        return

    # Force using the python version in poetry environment already used for
    # install.
    # The last line of the output is the short python version we want to use.
    py_short_version = py_short_version_lines[-1]
    ctx.cmd_and_log(ctx.env.POETRY + ["env", "use", py_short_version])


def poetry_no_srv_tools(ctx):
    # Get python site packages from poetry
    ctx.poetry_site_packages = ctx.cmd_and_log(
        ctx.env.POETRY + ["run", "python3", "-c",
        (
            "import distutils.sysconfig; "
            "print(distutils.sysconfig.get_python_lib())"
        )
    ]).strip()

    # Write intersec no srv tools path file.
    # We use a `.pth` that is automatically loaded by python.
    # See https://docs.python.org/3/library/site.html
    no_srv_tools_file = osp.join(ctx.poetry_site_packages,
                                 '_intersec_no_srv_tools.pth')
    with open(no_srv_tools_file, 'w') as f:
        # Remove /srv/tools from sys.path. We don't want to depend on the
        # outdated packages in /srv/tools.
        f.write(
            "import sys; sys.path = ["
            "    x for x in sys.path if not x.startswith('/srv/tools')"
            "]\n"
        )


def poetry_install(ctx):
    before_poetry_install = getattr(ctx, 'before_poetry_install', None)
    if before_poetry_install is not None:
        before_poetry_install(ctx)

    # Install poetry packages
    if ctx.exec_command(ctx.env.POETRY + ['install'], stdout=None,
                        stderr=None):
        ctx.fatal('poetry install failed')

    # Force poetry environment to the compatible version
    poetry_fix_no_env_use(ctx)

    # Remove /srv/tools from python path in poetry
    poetry_no_srv_tools(ctx)

    after_poetry_install = getattr(ctx, 'after_poetry_install', None)
    if after_poetry_install is not None:
        after_poetry_install(ctx)


def rerun_waf_configure_with_poetry(ctx):
    if ctx.get_env_bool('POETRY_ACTIVE'):
        # Poetry is already activated, do nothing.
        return

    # Set _IN_WAF_POETRY to avoid doing the poetry configuration twice.
    os.environ['_IN_POETRY_WAF_CONFIGURE'] = '1'

    # Run waf with poetry.
    run_waf_with_poetry(ctx)

    # Get lockfile for waf in poetry environment.
    poetry_waf_lockfile = ctx.cmd_and_log(
        ctx.env.POETRY + ['run', 'python3', '-c',
        (
            "import sys; import os; "
            "print(os.environ.get('WAFLOCK', "
            "      '.lock-waf_%s_build' % sys.platform))"
        )
    ]).strip()

    # If poetry_waf_lock_file is different from the current lockfile, we need
    # to copy it.
    if poetry_waf_lockfile != Options.lockfile:
        shutil.copy(poetry_waf_lockfile, Options.lockfile)

    # Do nothing more on configure.
    sys.exit(0)


def configure_with_poetry(ctx):
    if ctx.path != ctx.srcnode and not getattr(ctx, 'use_poetry', False):
        # The current project is not lib-common and Poetry is not used for the
        # current project.
        return

    if ctx.get_env_bool('NO_POETRY'):
        Logs.warn('Waf: Disabling poetry support')
        return

    ctx.find_program('poetry')

    if not ctx.get_env_bool('_IN_POETRY_WAF_CONFIGURE'):
        # We are not in waf run by Poetry, install Poetry
        poetry_install(ctx)

    ctx.env.HAVE_POETRY = True
    rerun_waf_configure_with_poetry(ctx)


def rerun_waf_build_with_poetry(ctx):
    if ctx.get_env_bool('POETRY_ACTIVE'):
        # Poetry is already activated, do nothing.
        return

    # Reset current directory to launch directory.
    os.chdir(ctx.launch_dir)

    # Run waf with poetry.
    run_waf_with_poetry(ctx)

    # Do nothing more on build.
    sys.exit(0)


def build_with_poetry(ctx):
    if not ctx.env.HAVE_POETRY:
        return

    rerun_waf_build_with_poetry(ctx)


# }}}
# {{{ options


def options(ctx):
    load_tools(ctx)


# }}}
# {{{ configure


def configure(ctx):
    configure_with_poetry(ctx)
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

    # XXX: Python virtualenv does not link python3-config inside the bin
    # directory of the virtualenv. This means that the version of
    # python3-config can be different from the version of python3 when we are
    # in a virtualenv.
    # To solve this issue, look for python3.x-config in the real python3
    # installation directory.
    py_config_path = ctx.cmd_and_log(ctx.env.PYTHON3 + [
        '-c', (
            'import sys, os;'
            'print(os.path.realpath(sys.executable) + "-config")'
        )
    ])
    ctx.find_program('python3-config', var='PYTHON3_CONFIG',
                     value=py_config_path)

    # We need to remove -I prefix to use Python include paths in INCLUDES
    # variables.
    py_includes = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--includes'])
    py_includes = shlex.split(py_includes)
    py_includes = [remove_prefix(x, '-I') for x in py_includes]
    ctx.env.append_unique('INCLUDES_python3', py_includes)
    ctx.env.append_unique('INCLUDES_python3_embed', py_includes)

    py_ldflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--ldflags'])
    py_ldflags = shlex.split(py_ldflags)
    ctx.env.append_unique('LDFLAGS_python3', py_ldflags)

    # pylint: disable=line-too-long
    # We need to '--embed' for python 3.8+ for standalone executables.
    # See https://docs.python.org/3/whatsnew/3.8.html#debug-build-uses-the-same-abi-as-release-build
    # For python < 3.8, ldflags are the same for both shared libraries and
    # standalone executables.
    try:
        py_embed_ldflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG +
                                           ['--ldflags', '--embed'])
    except Errors.WafError:
        py_embed_ldflags = py_ldflags
    else:
        py_embed_ldflags = shlex.split(py_embed_ldflags)

    ctx.env.append_unique('LDFLAGS_python3_embed', py_embed_ldflags)

    # }}}
    # {{{ cython

    src_path = ctx.path.make_node('src').abspath()
    ctx.env.append_unique('CYTHONFLAGS', [
        '--warning-errors',
        '--warning-extra',
        '-I' + src_path,
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
    build_with_poetry(ctx)

    # Declare 4 build groups:
    #  - one for generating the "version" source files
    #  - one for compiling clang-rewrite-blocks
    #  - one for compiling farchc
    #  - one for compiling iopc
    #  - one for compiling pxc (used in the tools repository)
    #  - one for generating/compiling code after then.
    #
    # This way we are sure farchc is generated before iopc (needed because it
    # uses a farch file), and iopc is generated before building the IOP files.
    # Refer to section "Building the compiler first" of the waf book.
    ctx.add_group('gen_version')
    ctx.add_group('clang_rewrite_blocks')
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
