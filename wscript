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

import os
import os.path as osp
import re
import shlex
import shutil
import sys

from waflib import Errors, Logs, Options
from waflib.Build import BuildContext
from waflib.Configure import ConfigurationContext
from waflib.Context import Context
from waflib.Options import OptionsContext


waftoolsdir = os.path.join(os.getcwd(), 'build', 'waftools')
sys.path.insert(0, waftoolsdir)


out = '.build-waf-%s' % os.environ.get('P', 'default')

# {{{ helpers


def remove_prefix(text: str, prefix: str) -> str:
    if text.startswith(prefix):
        return text[len(prefix):]
    return text


def load_tools(ctx: Context) -> None:
    ctx.load('common', tooldir=waftoolsdir)
    ctx.load('backend', tooldir=waftoolsdir)
    if sys.version_info >= (2, 7):
        # This extension uses the python json library to unpack the existing
        # compilation database. But it uses an argument that is not compatible
        # with older python versions.
        ctx.load('compilation_database', tooldir=waftoolsdir)
    for tool in getattr(ctx, 'extra_waftools', []):
        ctx.load(tool, tooldir=waftoolsdir)

    # Configure waf to re-evaluate hashes only when file timestamp/size
    # change. This is way faster on no-op builds.
    ctx.load('md5_tstamp')


# }}}
# {{{ asdf


def run_asdf_install(ctx: BuildContext) -> None:
    if ctx.get_env_bool('_ASDF_INSTALL_DONE_WAF_CONFIGURE'):
        # We have already installed ASDF
        return

    # Run the ASDF install script
    build_dir = os.path.join(ctx.path.abspath(), 'build')
    cmd = [f'{build_dir}/asdf_install.sh', str(ctx.srcnode)]
    if ctx.exec_command(cmd, stdout=None, stderr=None, cwd=ctx.srcnode):
        ctx.fatal('ASDF installation failed')

    # Set _ASDF_INSTALL_DONE_WAF_CONFIGURE to avoid install ASDF twice.
    os.environ['_ASDF_INSTALL_DONE_WAF_CONFIGURE'] = '1'


def configure_asdf(ctx: BuildContext) -> None:
    if 'ASDF_DIR' not in os.environ:
        # No ASDF
        ctx.msg('Using ASDF', 'no')
        return

    # For ASDF users, we first ensure that all ASDF plugins and tool versions
    # are installed before continuing the configuration.
    ctx.env.USE_ASDF = True
    ctx.msg('Using ASDF', 'yes')

    # https://asdf-vm.com/manage/configuration.html#asdf-data-dir
    ctx.env.ASDF_DATA_DIR = os.environ.get(
        'ASDF_DATA_DIR', os.environ['HOME'] + '/.asdf')
    ctx.env.ASDF_SHIMS = ctx.env.ASDF_DATA_DIR + '/shims'

    # Run the ASDF install script if needed
    run_asdf_install(ctx)


# }}}
# {{{ uv


def run_waf_with_uv(ctx: BuildContext) -> None:
    Logs.info('Waf: Run waf in uv environment')

    exit_code = ctx.exec_command(ctx.env.UV + ['run'] + sys.argv,
                                 stdout=None, stderr=None)
    if exit_code != 0:
        sys.exit(exit_code)


def python_asdf_cleanup_prev_venv(ctx: BuildContext) -> None:
    # If we have a virtual environment, we need to clean it
    virtual_env = os.environ.get('VIRTUAL_ENV')
    if virtual_env is not None:
        # Remove the virtual env from the PATH
        old_path = os.environ['PATH']
        new_path = old_path.replace(virtual_env + '/bin:', '')
        os.environ['PATH'] = new_path

        # Remove VIRTUAL_ENV environment variables
        os.environ.pop('VIRTUAL_ENV', None)

    # Remove the potential ASDF python plugin and install directories
    # '.asdf/*/python/*' from the PATH.
    # Since waf can be started with a previous python version controlled by
    # ASDF, ASDF can put some directory in the PATH when running waf that
    # points to the old python version. We need to clean them to really use
    # the python version that we want from the asdf_install.sh script.
    old_path = os.environ['PATH']
    new_path = re.sub(ctx.env.ASDF_DATA_DIR + r'/?[^/]*/python/[^:]*:',
                      '', old_path)
    os.environ['PATH'] = new_path


def uv_no_srv_tools(ctx: BuildContext) -> None:
    # Get python site packages from uv
    ctx.uv_site_packages = ctx.cmd_and_log(
        ctx.env.UV + ['run', 'python3', '-c',
        (
            "import sysconfig; "
            "print(sysconfig.get_paths()['purelib'])"
        ),
    ]).strip()

    # Write intersec no srv tools path file.
    # We use a `.pth` that is automatically loaded by python.
    # See https://docs.python.org/3/library/site.html
    no_srv_tools_file = osp.join(ctx.uv_site_packages,
                                 '_intersec_no_srv_tools.pth')
    with open(no_srv_tools_file, 'w') as f:
        # Remove /srv/tools from sys.path. We don't want to depend on the
        # outdated packages in /srv/tools.
        f.write(
            "import sys; sys.path = ["
            "    x for x in sys.path if not x.startswith('/srv/tools')"
            "]\n",
        )


def uv_sync(ctx: BuildContext) -> None:
    if ctx.env.USE_ASDF:
        python_asdf_cleanup_prev_venv(ctx)

    before_uv_sync = getattr(ctx, 'before_uv_sync', None)
    if before_uv_sync is not None:
        before_uv_sync(ctx)

    # Sync uv environment
    if ctx.exec_command(ctx.env.UV + ['sync', '--locked'],
                        stdout=None, stderr=None):
        ctx.fatal('uv sync failed')

    # Remove /srv/tools from python path in uv
    uv_no_srv_tools(ctx)

    after_uv_sync = getattr(ctx, 'after_uv_sync', None)
    if after_uv_sync is not None:
        after_uv_sync(ctx)


def uv_environment_is_active(ctx: BuildContext) -> bool:
    # Consider the UV environment is active if the VIRTUAL_ENV variable is set
    # to the venv path of the project (resolving symlinks)
    virtual_env = os.environ.get('VIRTUAL_ENV', None)
    if not virtual_env:
        return False

    venv_path: str = os.path.realpath(virtual_env)
    project_venv_node = ctx.srcnode.make_node('.venv')
    project_venv_path: str = os.path.realpath(project_venv_node.abspath())

    return venv_path == project_venv_path


def rerun_waf_configure_with_uv(ctx: BuildContext) -> None:
    if ctx.get_env_bool('_IN_UV_WAF_CONFIGURE'):
        # We are already in a recursion with uv, do nothing.
        return

    if uv_environment_is_active(ctx) and not ctx.env.USE_ASDF:
        # If uv is already activated and ASDF is not in use we do nothing.
        # However if ASDF is in use, the python version that will be loaded by
        # ASDF may differ from the current active venv, and thus we need to
        # recurse anyway to use the right python version.
        return

    # Set _IN_UV_WAF_CONFIGURE to avoid doing the uv configuration twice.
    os.environ['_IN_UV_WAF_CONFIGURE'] = '1'

    # Run waf with uv
    run_waf_with_uv(ctx)

    # Get lockfile for waf in uv environment.
    uv_waf_lockfile = ctx.cmd_and_log(
        ctx.env.UV + ['run', 'python3', '-c',
        (
            "import sys; import os; "
            "print(os.environ.get('WAFLOCK', "
            "      '.lock-waf_%s_build' % sys.platform))"
        ),
    ]).strip()

    # If uv_waf_lockfile is different from the current lockfile, we need
    # to copy it.
    if uv_waf_lockfile != Options.lockfile:
        shutil.copy(uv_waf_lockfile, Options.lockfile)

    # Do nothing more on configure.
    sys.exit(0)


def configure_with_uv(ctx: BuildContext) -> None:
    if ctx.path != ctx.srcnode and not getattr(ctx, 'use_uv', False):
        # The current project is not lib-common and uv is not used for the
        # current project.
        return

    if ctx.get_env_bool('NO_UV'):
        Logs.warn('Waf: Disabling uv support')
        return

    if ctx.env.USE_ASDF:
        ctx.find_program('uv', path_list=[ctx.env.ASDF_SHIMS])
    else:
        ctx.find_program('uv')

    if not ctx.get_env_bool('_IN_UV_WAF_CONFIGURE'):
        # We are not in waf run by uv, sync uv
        uv_sync(ctx)

    ctx.env.HAVE_UV = True
    rerun_waf_configure_with_uv(ctx)


def rerun_waf_build_with_uv(ctx: BuildContext) -> None:
    if ctx.get_env_bool('_IN_UV_WAF_BUILD'):
        # We are already in a recursion with uv, do nothing.
        return
    if uv_environment_is_active(ctx) and not ctx.env.USE_ASDF:
        # uv environment is activated and ASDF is not in use, do nothing.
        return

    # Set _IN_UV_WAF_BUILD to avoid doing the recursion twice.
    os.environ['_IN_UV_WAF_BUILD'] = '1'

    # Reset current directory to launch directory.
    os.chdir(ctx.launch_dir)

    # Run waf with uv..
    run_waf_with_uv(ctx)

    # Do nothing more on build.
    sys.exit(0)


def build_with_uv(ctx: BuildContext) -> None:
    if not ctx.env.HAVE_UV:
        return

    rerun_waf_build_with_uv(ctx)


# }}}
# {{{ options


def options(ctx: OptionsContext) -> None:
    load_tools(ctx)


# }}}
# {{{ configure


def configure(ctx: ConfigurationContext) -> None:
    # First, configure and install ASDF
    configure_asdf(ctx)

    # Configure and run waf configure in uv if needed
    configure_with_uv(ctx)

    # Load the different tools for configure
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
        ),
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

    py_prefix = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--prefix'])
    py_prefix_lib = py_prefix.strip() + '/lib'
    ctx.env.append_unique('RPATH_python3', py_prefix_lib)
    ctx.env.append_unique('RPATH_python3_embed', py_prefix_lib)

    py_ldflags = ctx.cmd_and_log(ctx.env.PYTHON3_CONFIG + ['--ldflags'])
    py_ldflags = shlex.split(py_ldflags)
    ctx.env.append_unique('LDFLAGS_python3', py_ldflags)

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

    def customize_source_file(name: str, ctx_field: str, default_path: str,
                              out_path: str) -> None:
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


def build(ctx: BuildContext) -> None:
    build_with_uv(ctx)

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
