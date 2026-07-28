###########################################################################
#                                                                         #
# Copyright 2026 INTERSEC SA                                              #
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
# ruff: noqa: UP006, UP045

import json
import os
import os.path as osp
import re
import shlex
import shutil
import sys
from typing import (  # noqa: UP035 (deprecated-import)
    # We still need the typing aliases here, and Optional rather than
    # `X | None`, because this file is read by waf with the system Python,
    # which is 3.6 on the oldest supported OS, before switching to the
    # version the project targets. That is also too old for
    # `from __future__ import annotations`, which needs 3.7.
    List,
    Optional,
    Tuple,
)

from waflib import Errors, Logs, Options
from waflib.Build import BuildContext
from waflib.Configure import ConfigurationContext
from waflib.Context import BOTH, Context
from waflib.Options import OptionsContext

waftoolsdir = os.path.join(os.getcwd(), 'build', 'waftools')
sys.path.insert(0, waftoolsdir)


out = f'.build-waf-{os.environ.get("P", "default")}'

# {{{ helpers


def remove_prefix(text: str, prefix: str) -> str:
    if text.startswith(prefix):
        return text[len(prefix) :]
    return text


def load_tools(ctx: Context) -> None:
    ctx.load('common', tooldir=waftoolsdir)
    ctx.load('backend', tooldir=waftoolsdir)
    ctx.load('compilation_database', tooldir=waftoolsdir)
    for tool in getattr(ctx, 'extra_waftools', []):
        ctx.load(tool, tooldir=waftoolsdir)

    # Configure waf to re-evaluate hashes only when file timestamp/size
    # change. This is way faster on no-op builds.
    ctx.load('md5_tstamp')


# }}}
# {{{ Tool Managers


REQUIRES_PYTHON_RE = re.compile(
    r'^\s*requires-python\s*=\s*["\']([^"\']+)["\']', re.MULTILINE
)
VERSION_BOUND_RE = re.compile(r'(>=|<)\s*(\d+)\.(\d+)(?:\.\d+)?')


def requires_python_bounds(
    ctx: BuildContext,
) -> Tuple[Optional[Tuple[int, int]], Optional[Tuple[int, int]]]:
    # Return the (minimum, maximum) Python versions supported by the project,
    # as (major, minor) tuples, the maximum being excluded. Either is None
    # when the corresponding bound is absent.
    #
    # pyproject.toml is parsed with a regexp on purpose: this runs before the
    # uv environment exists, so no TOML parser is available yet.
    pyproject = ctx.srcnode.find_node('pyproject.toml')
    if pyproject is None:
        return None, None

    match = REQUIRES_PYTHON_RE.search(pyproject.read())
    if match is None:
        return None, None

    minimum = maximum = None
    for part in match.group(1).split(','):
        spec = part.strip()
        bound = VERSION_BOUND_RE.fullmatch(spec)
        if bound is None:
            # Warn rather than fail: an unknown bound only makes the choice of
            # the interpreter less accurate, and uv checks requires-python by
            # itself anyway.
            Logs.warn(
                f'Waf: ignoring requires-python bound {spec} of '
                'pyproject.toml: only `>=X.Y` and `<X.Y` are understood'
            )
            continue

        operator, major, minor = bound.groups()
        if operator == '>=':
            minimum = (int(major), int(minor))
        else:
            maximum = (int(major), int(minor))

    return minimum, maximum


def system_python_version(ctx: BuildContext) -> Optional[Tuple[int, int]]:
    # Return the (major, minor) version of the system Python, or None when
    # there is none.
    #
    # The interpreters of a virtual environment or of a tool manager are
    # removed from the PATH: they are precisely what we are about to decide to
    # use or not, so taking one of them for the system Python would make the
    # decision depend on the result of the previous configure.
    #
    # Both tool managers are cleaned up, whichever one we use: a machine
    # migrating from ASDF to Mise has both, and the shims of the unused one
    # shadow the system interpreter just as well.
    mise_dir = ctx.env.MISE_DATA_DIR
    asdf_dir = ctx.env.ASDF_DATA_DIR
    virtual_env = os.environ.get('VIRTUAL_ENV')

    path = []
    for entry in os.environ['PATH'].split(os.pathsep):
        if entry.endswith('/.venv/bin') or entry == f'{mise_dir}/shims':
            continue
        if entry == f'{asdf_dir}/shims':
            continue
        if virtual_env is not None and entry == f'{virtual_env}/bin':
            continue
        if entry.startswith(f'{mise_dir}/installs/python/'):
            continue
        if entry.startswith(asdf_dir) and '/python/' in entry:
            continue
        path.append(entry)

    env = dict(os.environ, PATH=os.pathsep.join(path))
    env.pop('VIRTUAL_ENV', None)
    # Make any ASDF shim left in the PATH dispatch to the system Python, like
    # build/asdf_python_version.sh does.
    env['ASDF_PYTHON_VERSION'] = 'system'

    try:
        version = ctx.cmd_and_log(
            [
                'python3',
                '-c',
                'import sys; print("%d.%d" % sys.version_info[:2])',
            ],
            env=env,
        )
    except Errors.WafError:
        return None

    major, _, minor = version.strip().partition('.')
    return int(major), int(minor)


def mise_python_version(ctx: BuildContext) -> Optional[str]:
    # Return the Python version mise must provide, or None when the system one
    # must be used.
    #
    # mise cannot express "the system Python, but only if its version is
    # supported", so the decision is taken here, from the requires-python of
    # pyproject.toml. That range is the set of Python versions shipped by the
    # OSes supported by the branch, hence the system Python of any of them is
    # suitable. Production platforms depend on that, since they must use their
    # system interpreter for performance reasons.
    minimum, maximum = requires_python_bounds(ctx)
    if minimum is None:
        return None

    system = system_python_version(ctx)
    if (
        system is not None
        and system >= minimum
        and (maximum is None or system < maximum)
    ):
        return None

    # Fall back on the oldest supported version, the one common to every
    # supported OS. Only the major and minor versions are requested: mise
    # downloads prebuilt archives, which only exist for recent patch versions.
    return '{}.{}'.format(*minimum)


def configure_tool_manager(ctx: BuildContext) -> None:
    # The data directories of both tool managers, resolved here once and for
    # all: they are needed whichever manager is in use, since a machine can
    # have both installed.
    # https://mise.jdx.dev/directories.html
    # https://asdf-vm.com/manage/configuration.html#asdf-data-dir
    xdg_data_home = os.environ.get(
        'XDG_DATA_HOME', osp.expanduser('~/.local/share')
    )
    ctx.env.MISE_DATA_DIR = os.environ.get(
        'MISE_DATA_DIR', osp.join(xdg_data_home, 'mise')
    )
    ctx.env.ASDF_DATA_DIR = os.environ.get(
        'ASDF_DATA_DIR', osp.expanduser('~/.asdf')
    )

    # For ASDF/Mise users, we first ensure that all plugins and tool versions
    # are installed before continuing the configuration.
    if (
        'MISE_SHELL' in os.environ
        or 'MISE_DATA_DIR' in os.environ
        or 'mise/shims' in os.environ['PATH']
    ):
        ctx.env.TOOL_MANAGER = 'mise'
    elif 'ASDF_DIR' in os.environ:
        ctx.env.TOOL_MANAGER = 'asdf'
        ctx.env.ASDF_SHIMS = ctx.env.ASDF_DATA_DIR + '/shims'
    else:
        ctx.msg('Using tool manager', 'no')
        return

    if ctx.get_env_bool('_TOOL_MANAGER_INSTALL_DONE_WAF_CONFIGURE'):
        # We have already installed the tool manager
        return

    ctx.msg('Using tool manager', ctx.env.TOOL_MANAGER)
    if ctx.env.TOOL_MANAGER == 'mise':
        # Let MISE_PYTHON_VERSION win when it is already set, so that another
        # Python version can be tried without touching the repository.
        python_version = os.environ.get('MISE_PYTHON_VERSION')
        if python_version is None:
            python_version = mise_python_version(ctx)
            if python_version is None:
                # Prevent mise from providing any Python, so that uv falls
                # back on the system one (cf. python-preference in
                # pyproject.toml). This also neutralizes a `python system`
                # entry coming from a legacy ASDF ~/.tool-versions file,
                # which mise cannot resolve.
                disabled = os.environ.get('MISE_DISABLE_TOOLS', '')
                tools = [tool for tool in disabled.split(',') if tool]
                if 'python' not in tools:
                    tools.append('python')
                os.environ['MISE_DISABLE_TOOLS'] = ','.join(tools)
            else:
                os.environ['MISE_PYTHON_VERSION'] = python_version
        ctx.msg('Python version', python_version or 'system')
        ctx.env.UV_PYTHON_VERSION = python_version

        cmd = ['mise', 'install']
        if ctx.exec_command(cmd, stdout=None, stderr=None, cwd=ctx.srcnode):
            ctx.fatal('Mise installation failed')

        # After the tools installation we need to update our environment to
        # take in account the changes performed by Mise
        cmd = ['mise', 'env', '--json']
        mise_env = json.loads(ctx.cmd_and_log(cmd))
        os.environ.update(mise_env)
        ctx.environ.update(mise_env)

    elif ctx.env.TOOL_MANAGER == 'asdf':
        build_dir = os.path.join(ctx.path.abspath(), 'build')
        cmd = [f'{build_dir}/asdf_install.sh', str(ctx.srcnode)]
        if ctx.exec_command(cmd, stdout=None, stderr=None, cwd=ctx.srcnode):
            ctx.fatal('ASDF installation failed')

    # Set _TOOL_MANAGER_INSTALL_DONE_WAF_CONFIGURE to avoid install twice.
    os.environ['_TOOL_MANAGER_INSTALL_DONE_WAF_CONFIGURE'] = '1'


# }}}
# {{{ uv


def run_waf_with_uv(ctx: BuildContext) -> None:
    Logs.info('Waf: Run waf in uv environment')

    exit_code = ctx.exec_command(
        ctx.env.UV + ['run'] + sys.argv, stdout=None, stderr=None
    )
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
    new_path = re.sub(
        ctx.env.ASDF_DATA_DIR + r'/?[^/]*/python/[^:]*:', '', old_path
    )
    os.environ['PATH'] = new_path


def uv_no_srv_tools(ctx: BuildContext) -> None:
    # Get python site packages from uv
    ctx.uv_site_packages = ctx.cmd_and_log(
        ctx.env.UV
        + [
            'run',
            'python3',
            '-c',
            ("import sysconfig; print(sysconfig.get_paths()['purelib'])"),
        ]
    ).strip()

    # Write intersec no srv tools path file.
    # We use a `.pth` that is automatically loaded by python.
    # See https://docs.python.org/3/library/site.html
    no_srv_tools_file = osp.join(
        ctx.uv_site_packages, '_intersec_no_srv_tools.pth'
    )
    with open(no_srv_tools_file, 'w') as f:
        # Remove /srv/tools from sys.path. We don't want to depend on the
        # outdated packages in /srv/tools.
        f.write(
            'import sys; sys.path = ['
            "    x for x in sys.path if not x.startswith('/srv/tools')"
            ']\n',
        )


def uv_sync_args(ctx: BuildContext) -> List[str]:
    # The arguments describing the environment uv must produce. Shared with
    # uv_environment_is_synced(): the check has to request the very same
    # environment, or it reports a difference on every build.
    uv_args = ['sync', '--locked']

    if ctx.env.UV_EXTRA:
        extras = set(re.split(r'[ ,]+', ctx.env.UV_EXTRA))

        for extra in extras:
            uv_args += ['--extra', extra]

    return uv_args


def uv_sync(ctx: BuildContext) -> None:
    if ctx.env.TOOL_MANAGER == 'asdf':
        python_asdf_cleanup_prev_venv(ctx)

    before_uv_sync = getattr(ctx, 'before_uv_sync', None)
    if before_uv_sync is not None:
        before_uv_sync(ctx)

    uv_args = uv_sync_args(ctx)

    if ctx.env.UV_PYTHON_VERSION:
        # Force the interpreter selected by the tool manager: without it, uv
        # keeps the venv of a previous configure as long as its version
        # satisfies requires-python, so a change of version would be ignored.
        uv_args += ['--python', ctx.env.UV_PYTHON_VERSION]

    # Sync uv environment
    if ctx.exec_command(ctx.env.UV + uv_args, stdout=None, stderr=None):
        ctx.fatal('uv sync failed')

    # Remove /srv/tools from python path in uv
    uv_no_srv_tools(ctx)

    after_uv_sync = getattr(ctx, 'after_uv_sync', None)
    if after_uv_sync is not None:
        after_uv_sync(ctx)


def uv_environment_is_active(ctx: BuildContext) -> bool:
    # Consider the UV environment is active if the VIRTUAL_ENV variable is set
    # to the venv path of the project (resolving symlinks), if the PATH
    # resolves python3 to that venv, and if the interpreter running waf comes
    # from it.
    #
    # The PATH matters as much as VIRTUAL_ENV, because that is what `uv run`
    # changes: it prepends the venv bin directory, which decides every program
    # waf resolves. VIRTUAL_ENV alone can be exported to designate an
    # environment to uv, without the PATH.
    virtual_env = os.environ.get('VIRTUAL_ENV', None)
    if not virtual_env:
        return False

    venv_path: str = os.path.realpath(virtual_env)
    project_venv_node = ctx.srcnode.make_node('.venv')
    project_venv_path: str = os.path.realpath(project_venv_node.abspath())

    if venv_path != project_venv_path:
        return False

    python3 = shutil.which('python3')
    if python3 is None:
        return False

    # Resolve the bin directory, not python3 itself: the interpreter of a venv
    # is a symlink to the one it was created from.
    python3_dir: str = os.path.realpath(os.path.dirname(python3))

    if python3_dir != os.path.join(project_venv_path, 'bin'):
        return False

    # The interpreter running waf itself must also come from the venv: the
    # mise shim execs the system python while exporting VIRTUAL_ENV and the
    # venv PATH (python.uv_venv_auto sourcing), so the environment can look
    # active around an interpreter that is not. Re-running through `uv run`
    # is not a no-op then: with the venv at the head of the PATH, the
    # launcher's `#!/usr/bin/env python3` shebang resolves the venv
    # interpreter instead.
    executable_dir: str = os.path.realpath(os.path.dirname(sys.executable))

    return executable_dir == os.path.join(project_venv_path, 'bin')


def uv_environment_is_synced(ctx: BuildContext) -> bool:
    # `uv sync --check` reports whether the venv matches the lockfile without
    # touching it.
    #
    # `--python` is deliberately left out: the recursion this gates syncs
    # through `uv run`, which cannot select an interpreter, so an interpreter
    # mismatch would be reported forever and re-exec on every build. The
    # version belongs to configure, which passes it to uv_sync().
    try:
        ctx.cmd_and_log(
            ctx.env.UV + uv_sync_args(ctx) + ['--check'],
            cwd=ctx.srcnode,
            quiet=BOTH,
        )
    except Errors.WafError:
        return False

    return True


def rerun_waf_configure_with_uv(ctx: BuildContext) -> None:
    if ctx.get_env_bool('_IN_UV_WAF_CONFIGURE'):
        # We are already in a recursion with uv, do nothing.
        return

    if uv_environment_is_active(ctx):
        # The uv environment is already active: re-running waf through `uv run`
        # would only set up the very same environment again. uv_sync() has
        # already synced the venv with the python version selected by the tool
        # manager (`uv sync --python`), so the active venv is the right one.
        return

    # Set _IN_UV_WAF_CONFIGURE to avoid doing the uv configuration twice.
    os.environ['_IN_UV_WAF_CONFIGURE'] = '1'

    # Run waf with uv
    run_waf_with_uv(ctx)

    # Get lockfile for waf in uv environment.
    uv_waf_lockfile = ctx.cmd_and_log(
        ctx.env.UV
        + [
            'run',
            'python3',
            '-c',
            (
                'import sys; import os; '
                "print(os.environ.get('WAFLOCK', "
                "      '.lock-waf_%s_build' % sys.platform))"
            ),
        ]
    ).strip()

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

    if ctx.env.TOOL_MANAGER == 'asdf':
        ctx.find_program('uv', path_list=[ctx.env.ASDF_SHIMS])
    else:
        ctx.find_program('uv')

    if 'UV_EXTRA' in os.environ:
        ctx.env.UV_EXTRA = os.environ['UV_EXTRA']

    if not ctx.get_env_bool('_IN_UV_WAF_CONFIGURE'):
        # We are not in waf run by uv, sync uv
        uv_sync(ctx)

    ctx.env.HAVE_UV = True
    rerun_waf_configure_with_uv(ctx)


def rerun_waf_build_with_uv(ctx: BuildContext) -> None:
    if ctx.get_env_bool('_IN_UV_WAF_BUILD'):
        # We are already in a recursion with uv, do nothing.
        return
    if uv_environment_is_active(ctx) and uv_environment_is_synced(ctx):
        # The uv environment is already active and up to date, do nothing.
        #
        # Being active is not enough here: unlike configure, build never calls
        # uv_sync(), so the only sync of this path is the implicit one of
        # `uv run`. And relying on that one would not be safe anyway: when it
        # recreates the venv, the `.pth` file of uv_no_srv_tools() goes with
        # it, and only uv_sync() writes it back.
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
    # First, configure tool managers if any
    configure_tool_manager(ctx)

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
    ctx.check_cfg(
        package='libxml-2.0',
        uselib_store='libxml',
        args=['--cflags', '--libs'],
    )
    ctx.check_cfg(
        package='openssl', uselib_store='openssl', args=['--cflags', '--libs']
    )
    ctx.check_cfg(
        package='zlib', uselib_store='zlib', args=['--cflags', '--libs']
    )
    ctx.check_cfg(
        package='valgrind',
        uselib_store='valgrind',
        args=['--cflags'],
        mandatory=False,
    )

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
    py_config_path = ctx.cmd_and_log(
        ctx.env.PYTHON3
        + [
            '-c',
            (
                'import sys, os;'
                'print(os.path.realpath(sys.executable) + "-config")'
            ),
        ]
    )
    ctx.find_program(
        'python3-config', var='PYTHON3_CONFIG', value=py_config_path
    )

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
        py_embed_ldflags = ctx.cmd_and_log(
            ctx.env.PYTHON3_CONFIG + ['--ldflags', '--embed']
        )
    except Errors.WafError:
        py_embed_ldflags = py_ldflags
    else:
        py_embed_ldflags = shlex.split(py_embed_ldflags)

    ctx.env.append_unique('LDFLAGS_python3_embed', py_embed_ldflags)

    # }}}
    # {{{ cython

    src_path = ctx.path.make_node('src').abspath()
    ctx.env.append_unique(
        'CYTHONFLAGS',
        [
            '--warning-errors',
            '--warning-extra',
            '-I' + src_path,
        ],
    )
    ctx.env.CYTHONSUFFIX = '.pyx'

    # }}}

    # }}}
    # {{{ Source files customization

    # The purpose of this section is to let projects using the lib-common to
    # redefine some files.

    def customize_source_file(
        name: str, ctx_field: str, default_path: str, out_path: str
    ) -> None:
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
    customize_source_file(
        'lstr_obfuscate source file',
        'lstr_obfuscate_src',
        'src/core/str-l-obfuscate-default.c',
        'src/core/str-l-obfuscate.c',
    )

    # Ichannels SSL certificate/key
    customize_source_file(
        'Ichannel SSL certificate',
        'ic_cert_src',
        'src/iop/ic-cert-default.pem',
        'src/iop/ic-cert.pem',
    )
    customize_source_file(
        'Ichannel SSL private key',
        'ic_key_src',
        'src/iop/ic-key-default.pem',
        'src/iop/ic-key.pem',
    )

    # }}}


# }}}
# {{{ build


def build(ctx: BuildContext) -> None:
    build_with_uv(ctx)

    # Declare the build groups:
    #  - one for generating the "version" source files
    #  - one for compiling clang-rewrite-blocks
    #  - one for compiling libcommon-minimal
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
    ctx.add_group('libcommon-minimal')
    ctx.add_group('farchc')
    ctx.add_group('iopc')
    ctx.add_group('pxcc')
    ctx.add_group('code_compiling')

    load_tools(ctx)

    ctx.recurse(
        [
            'src',
            'rust',
            'bench',
            'examples',
            'tests',
        ]
    )


# }}}
