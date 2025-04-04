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

'''
Contains the code needed for backend compilation.
'''

import datetime
import os
import re
import copy
import shlex
import os.path as osp
import time
from itertools import chain

# pylint: disable = import-error
from waflib import TaskGen, Utils, Context, Errors, Options, Logs

from waflib.Build import BuildContext
from waflib.Configure import ConfigurationContext
from waflib.Options import OptionsContext
from waflib.Task import Task
from waflib.Tools import c as c_tool
from waflib.Tools import c_preproc
from waflib.Tools import cxx
from waflib.Tools import ccroot
from waflib.Utils import check_exe
from waflib.Node import Node
# pylint: enable = import-error

from typing import (
    Callable, Optional, TypeVar, TYPE_CHECKING,
    # We still need to use them here because this file is imported in
    # Python 3.6 by waf before switching to Python 3.9+.
    List, Set, Dict, Tuple, Type,
)


# Add type hinting for TaskGen decorators
if TYPE_CHECKING:
    T = TypeVar('T')
    def task_gen_decorator(*args: str) -> Callable[[T], T]:
        ...
    TaskGen.feature = task_gen_decorator
    TaskGen.before_method = task_gen_decorator
    TaskGen.after_method = task_gen_decorator
    TaskGen.extension = task_gen_decorator


# {{{ use_whole

# These functions implement the use_whole attribute, allowing to link a
# library with -whole-archive

@TaskGen.feature('c', 'cprogram', 'cstlib')
@TaskGen.before_method('process_rule')
def prepare_whole(self: TaskGen) -> None:
    use_whole = self.to_list(getattr(self, 'use_whole', []))
    if not use_whole:
        return

    # Add the 'use_whole' elements in the 'use' list, so that waf considers it
    # for paths, includes, ...
    self.use = list(self.to_list(getattr(self, 'use', [])))
    for uw in use_whole:
        if  not getattr(self, 'no_use_whole_error', False) \
        and uw in self.use:
            self.bld.fatal(('`{0}` from `use_whole` of target `{1}` is '
                            'already in attribute `use`, you may remove it')
                           .format(uw, self.target))
        self.use.append(uw)

@TaskGen.feature('c', 'cprogram', 'cstlib')
@TaskGen.after_method('process_use')
def process_whole(self: TaskGen) -> None:
    use_whole = self.to_list(getattr(self, 'use_whole', []))
    if not use_whole:
        return

    # Patch the LINKFLAGS to enter in whole archive mode...
    self.env.append_value('LINKFLAGS',
                          '-Wl,--as-needed,--whole-archive')

    cwd = self.get_cwd()

    for name in use_whole:
        # ...add the whole archive libraries...
        lib_task = self.bld.get_tgen_by_name(name)
        self.env.STLIB.remove(name)

        # TODO waf: filter STLIB_PATH by removing unused ones.
        self.env.append_value(
            'LINKFLAGS',
            list(chain.from_iterable(('-Xlinker', p.path_from(cwd))
                                     for p in lib_task.link_task.outputs)))

    # ...and close the whole archive mode
    self.env.append_value('LINKFLAGS', '-Wl,--no-whole-archive')

# }}}
# {{{ Filter-out zchk binaries in release mode

"""
Tests are not compiled in release mode, so compiling zchk programs is useless
then. This code filters them out.

It assumes all C task generators whose name begins with `zchk` are dedicated
to tests (and only them).
"""

def filter_out_zchk(ctx: BuildContext) -> None:
    for g in ctx.groups:
        for i in range(len(g) - 1, -1, -1):
            tgen = g[i]
            features = tgen.to_list(getattr(tgen, 'features', []))
            if  tgen.name.startswith('zchk') and 'c' in features:
                del g[i]


# }}}
# {{{ Deep add TaskGen compile flags


# The list of keys to skip when copying a TaskGen stlib on
# duplicate_lib_tgen()
SKIPPED_STLIB_TGEN_COPY_KEYS = set((
    '_name', 'bld', 'env', 'features', 'idx', 'path', 'target',
    'tg_idx_count',
))


def duplicate_lib_tgen(ctx: BuildContext, new_name: str,
                       orig_lib: TaskGen) -> TaskGen:
    """Duplicate a TaskGen with a new name"""
    ctx_path_bak = ctx.path
    ctx.path = orig_lib.path

    # Create a new TaskGen stlib by copying the attributes of the original
    # lib TaskGen.
    # XXX: TaskGen.clone() does not work in our case because it does not
    # create a stlib TaskGen, but a generic TaskGen. Moreover, it copies some
    # attributes that should not be copied.
    orig_lib_attrs = dict(
        (key, copy.copy(value)) for key, value in orig_lib.__dict__.items()
        if key not in SKIPPED_STLIB_TGEN_COPY_KEYS
    )
    lib = ctx.stlib(target=new_name, features=orig_lib.features,
                    env=orig_lib.env.derive(), **orig_lib_attrs)
    ctx.path = ctx_path_bak

    return lib


def deep_add_tgen_compile_flags(ctx: BuildContext, tgen: TaskGen,
                                dep_suffix: str, dep_libs: Set[str],
                                cflags: Optional[List[str]] = None,
                                cxxflags: Optional[List[str]] = None,
                                ldflags: Optional[List[str]] = None) -> None:
    """Add the compile flags to a TaskGen and duplicate all its dependencies
    to add the compile flags"""

    def add_tgen_compile_flags(tgen: TaskGen) -> None:
        if cflags:
            tgen.env.append_value('CFLAGS', cflags)
        if cxxflags:
            tgen.env.append_value('CXXFLAGS', cxxflags)
        if ldflags:
            tgen.env.append_value('LDFLAGS', ldflags)

    # Parent TaskGen must be compiled with the compilation flags...
    add_tgen_compile_flags(tgen)

    # ...such as all the libraries they use
    def deep_use_flags(tgen: TaskGen, use_attr: str) -> None:
        # for all the libraries used by tgen...
        use = tgen.to_list(getattr(tgen, use_attr, []))
        for i, use_name in enumerate(use):
            if use_name.endswith(dep_suffix):
                # already a correct dependency
                continue

            try:
                use_tgen = ctx.get_tgen_by_name(use_name)
            except Errors.WafError:
                # the 'use' element does not have an associated task
                # generator; probably an external library
                continue

            features = use_tgen.to_list(getattr(use_tgen, 'features', []))
            if not 'cstlib' in features:
                # the 'use' element is not a static library
                continue

            # Replace the static library by the dependency version in tgen
            # sources
            dep_name = use_name + dep_suffix
            use[i] = dep_name

            # Declare the dependency static library, if not done yet
            if not use_name in dep_libs:
                dep_lib = duplicate_lib_tgen(ctx, dep_name, use_tgen)
                add_tgen_compile_flags(dep_lib)
                dep_libs.add(use_name)
                # Recurse, to deal with the static libraries that use some
                # other static libraries.
                deep_use_flags(dep_lib, 'use')
                deep_use_flags(dep_lib, 'use_whole')

        # In case 'use' is a string, we need to set the variable back
        setattr(tgen, use_attr, use)

    # Process the use and use_whole lists
    deep_use_flags(tgen, 'use')
    deep_use_flags(tgen, 'use_whole')


# }}}
# {{{ fPIC compilation for shared libraries

"""
Our shared libraries need to be compiled with the -fPIC compilation flag (such
as all the static libraries they use).
But since this compilation flag has a significant impact on performances, we
don't want to compile our programs (and the static libraries they use) with
it.

The purpose of this code is to add the -fPIC compilation flag to all the
shared libraries task generators, and to replace all the static libraries they
use by a '-fPIC' version (by copying the original task generator and adding
the compilation flag).

Since this has a significant impact on the compilation time, it can be
disabled using the NO_DOUBLE_FPIC environment variable at configure
(NO_DOUBLE_FPIC=1 waf configure), but keep in mind this has an impact at
runtime. For this reason, disabling the double compilation is not allowed in
release profile.
"""


def compile_fpic(ctx: BuildContext) -> None:
    pic_libs: Set[str] = set()
    pic_flags = ['-fPIC']
    pic_suffix = '.pic'

    for tgen in ctx.get_all_task_gen():
        features = tgen.to_list(getattr(tgen, 'features', []))

        if not 'cshlib' in features:
            continue

        deep_add_tgen_compile_flags(ctx, tgen, pic_suffix, pic_libs,
                                    cflags=pic_flags, cxxflags=pic_flags)


# }}}
# {{{ Fuzzing executable compilation


def compile_fuzzing_programs(ctx: BuildContext) -> None:
    fuzzing_libs: Set[str] = set()
    fuzzing_suffix = '.fuzzing'
    fuzzing_cflags = ctx.env.FUZZING_CFLAGS
    fuzzing_cxxflags = ctx.env.FUZZING_CXXFLAGS
    fuzzing_ldflags = ctx.env.FUZZING_LDFLAGS

    for tgen in ctx.get_all_task_gen():
        features = tgen.to_list(getattr(tgen, 'features', []))

        if not 'fuzzing' in features:
            continue

        deep_add_tgen_compile_flags(ctx, tgen, fuzzing_suffix, fuzzing_libs,
                                    cflags=fuzzing_cflags,
                                    cxxflags=fuzzing_cxxflags,
                                    ldflags=fuzzing_ldflags)

@TaskGen.feature('fuzzing')
@TaskGen.after_method('process_use')
def fuzzing_feature(ctx: TaskGen) -> None:
    # Avoid warning about fuzzing feature
    pass


# }}}
# {{{ Patch C tasks for compression

def compile_sanitizer(ctx: BuildContext) -> None:
    """Add sanitizers flags to C and CXX task generators.

    The flags are also added to shared library task generators and fPIC task
    generators if SHARED_LIBRARY_SANITIZER is set.
    """
    for tgen in ctx.get_all_task_gen():
        features = tgen.to_list(getattr(tgen, 'features', []))

        if 'c' not in features and 'cxx' not in features:
            continue

        if (not ctx.env.SHARED_LIBRARY_SANITIZER
                and ('cshlib' in features or tgen.name.endswith('.pic'))):
            continue

        tgen.env.append_value('CFLAGS', ctx.env.SANITIZER_CFLAGS)
        tgen.env.append_value('CXXFLAGS', ctx.env.SANITIZER_CXXFLAGS)
        tgen.env.append_value('LDFLAGS', ctx.env.SANITIZER_LDFLAGS)


# }}}
# {{{ Execute commands from project root

def register_get_cwd() -> None:
    '''
    Execute the compiler's commands from the project root instead of the
    project build directory.
    This is important for us because some code (for example the Z tests
    registration) relies on the value of the __FILE__ macro.
    '''
    def get_cwd(self: BuildContext) -> str:
        cwd: str = self.env.PROJECT_ROOT
        return cwd

    c_tool.c.get_cwd = get_cwd
    cxx.cxx.get_cwd = get_cwd
    ccroot.link_task.get_cwd = get_cwd
    TaskGen.task_gen.get_cwd = get_cwd


# }}}
# {{{ Register global includes


def register_global_includes(self: BuildContext, includes: List[str]) -> None:
    ''' Register global includes (that are added to all the targets). '''
    for include in Utils.to_list(includes):
        node = self.path.find_node(include)
        if node is None or not node.isdir():
            msg = 'cannot find include path `{0}` from `{1}`'
            self.fatal(msg.format(include, self.path))
        self.env.append_unique('INCLUDES', node.abspath())


# }}}
# {{{ Patch tasks to build targets in the source directory


@TaskGen.feature('cprogram', 'cxxprogram')
@TaskGen.after_method('apply_link')
def deploy_program(self: TaskGen) -> None:
    # Build programs in the corresponding source directory
    assert (len(self.link_task.outputs) == 1)
    node = self.link_task.outputs[0]
    self.link_task.outputs = [node.get_src()]

    # Ensure the binaries are re-linked after running configure (in case the
    # profile was changed)
    self.link_task.hcode += str(self.env.CONFIGURE_TIME).encode('utf-8')


@TaskGen.feature('cshlib')
@TaskGen.after_method('apply_link')
def deploy_shlib(self: TaskGen) -> None:
    # Build C shared library in the corresponding source directory,
    # stripping the 'lib' prefix
    assert (len(self.link_task.outputs) == 1)
    node = self.link_task.outputs[0]
    assert (node.name.startswith('lib'))
    tgt_name = node.name
    if not getattr(self, 'keep_lib_prefix', False):
        tgt_name = tgt_name[len('lib'):]
    tgt = node.parent.get_src().make_node(tgt_name)
    self.link_task.outputs = [tgt]

    # Ensure the shared libraries are re-linked after running configure (in
    # case the profile was changed)
    self.link_task.hcode += str(self.env.CONFIGURE_TIME).encode('utf-8')


# }}}
# {{{ remove_dynlibs: option to remove all dynamic libraries at link

@TaskGen.feature('cshlib', 'cprogram')
@TaskGen.after_method('apply_link', 'process_use')
def remove_dynamic_libs(self: TaskGen) -> None:
    if getattr(self, 'remove_dynlibs', False):
        self.link_task.env.LIB = []

# }}}
# {{{ .local_vimrc.vim / syntastic configuration generation


def get_linter_flags(ctx: BuildContext, flags_key: str,
                     include_python3: bool = True) -> List[str]:
    include_flags = []
    for key in ctx.env:
        if key == 'INCLUDES' or key.startswith('INCLUDES_'):
            include_flags += ['-I' + value for value in ctx.env[key]]

    flags: List[str] = ctx.env[flags_key][:]
    if include_python3:
        flags += ctx.env.CFLAGS_python3
    flags += include_flags
    return flags


def gen_local_vimrc(ctx: BuildContext) -> None:
    content = ""

    # Generate ALE options.
    # Escape the -D flags with double quotes, which is needed for
    # -D"index(s,c)=index__(s,c)"
    flags = get_linter_flags(ctx, 'CLANG_FLAGS')
    for i, flag in enumerate(flags):
        if flag.startswith('-D'):
            flags[i] = '-D"' + flag[2:] + '"'

    # for older versions of ALE
    content += "let g:ale_c_clang_options = '\n"
    content += "    \\ "
    content += " ".join(flags)
    content += "\n"
    content += "\\'\n"

    # for ALE 3.0+
    # https://github.com/dense-analysis/ale/issues/3299
    content += "let g:ale_c_cc_options = '\n"
    content += "    \\ "
    content += " ".join(flags)
    content += "\n"
    content += "\\'\n"

    # Bind :make to waf
    content += r"set makeprg=LC_ALL=C\ NO_WWW=1\ waf"
    content += "\n"

    # Update errorformat so that vim finds the files when compiling with :make
    content += r"set errorformat^=\%D%*\\a:\ Entering\ directory\ `%f/"
    content += ctx.bldnode.name
    content += "'\n"

    # Set flags for cython
    content += "let g:ale_pyrex_cython_options = '\n"
    content += "    \\ "
    content += " ".join(get_linter_flags(ctx, 'CYTHONFLAGS', False))
    content += "\n"
    content += "\\'\n"

    if hasattr(ctx, 'vimrc_additions'):
        for cb in ctx.vimrc_additions:
            content += cb(ctx)

    # Write file if it changed
    node = ctx.srcnode.make_node('.local_vimrc.vim')
    if not node.exists() or node.read() != content:
        node.write(content)
        ctx.msg('Writing local vimrc configuration file', node)


def gen_syntastic(ctx: BuildContext) -> None:
    """
    Syntastic is a vim syntax checker extension. It is not used by anybody
    anymore, but its configuration file is used by the YouCompleteMe plugin,
    that is used by some people.

    https://github.com/vim-syntastic/syntastic
    """
    def write_file(filename: str, what: str, envs: List[str]) -> None:
        node = ctx.srcnode.make_node(filename)
        content = '\n'.join(envs) + '\n'
        if not node.exists() or node.read() != content:
            node.write(content)
            msg = 'Writing syntastic {0} configuration file'.format(what)
            ctx.msg(msg, node)

    write_file('.syntastic_c_config', 'C',
               get_linter_flags(ctx, 'CLANG_FLAGS'))
    write_file('.syntastic_cpp_config', 'C++',
               get_linter_flags(ctx, 'CLANGXX_FLAGS'))


# }}}
# {{{ Tags


def gen_tags(ctx: BuildContext) -> None:
    if ctx.cmd == 'tags':
        tags_options = ''
        tags_output  = '.tags'
    elif ctx.cmd == 'etags':
        tags_options = '-e'
        tags_output  = 'TAGS'
    else:
        return

    # Generate tags using ctags
    cmd = '{0} "{1}" "{2}"'.format(ctx.env.CTAGS_SH[0],
                                   tags_options, tags_output)
    if ctx.exec_command(cmd, stdout=None, stderr=None, cwd=ctx.srcnode):
        ctx.fatal('ctags generation failed')

    # Interrupt the build
    ctx.groups = []


class TagsClass(BuildContext): # type: ignore[misc]
    '''generate tags using ctags'''
    cmd = 'tags'


class EtagsClass(BuildContext): # type: ignore[misc]
    '''generate tags for emacs using ctags'''
    cmd = 'etags'


# }}}
# {{{ Detect / delete old generated files


GEN_FILES_SUFFIXES = [
    '.blk.c',
    '.blkk.cc',
    '.iop.c',
    '.iop.h',
    '.iop.json',
    '.iop.ts',
    '.fc.c',
    '.tokens.c',
    '.tokens.h',
]


def gen_file_keep(parent_node: Node, name: str) -> bool:
    ''' The purpose of this function is to exclude some files from the list of
        generated ones (because we don't want them to be deleted).
        TODO waf: avoid hardcoding this list (which should belong to mmsx).
    '''
    # Exclude event.iop.json files produced in bigdata products by the schema
    # library
    if name == 'event.iop.json' and parent_node.name != 'bigdata':
        return False
    return True


def is_gen_file(ctx: BuildContext, parent_node: Node, name: str) -> bool:
    extra_suffixes = getattr(ctx, 'extra_gen_files_suffixes', [])
    gen_files_suffixes = GEN_FILES_SUFFIXES + extra_suffixes

    for sfx in gen_files_suffixes:
        if name.endswith(sfx):
            return gen_file_keep(parent_node, name)
    return False


def get_git_files(ctx: BuildContext, repo_node: Node) -> List[Node]:
    """ Get the list of committed files in a git repository. """

    # Call git ls-files to get the list of committed files
    git_ls_files = ctx.cmd_and_log(['git', 'ls-files'], quiet=Context.BOTH,
                                   cwd=repo_node)

    # Build nodes from the list
    res = [repo_node.make_node(p) for p in git_ls_files.strip().splitlines()]

    # Exclude symlinks
    res = [node for node in res if not os.path.islink(node.abspath())]

    return res


def get_git_files_recur(ctx: BuildContext) -> List[Node]:
    """ Get the list of committed files in the repository, recursively on all
        submodules.
    """

    # Get files of the current repository
    res = get_git_files(ctx, ctx.srcnode)

    # Get the list of submodules
    cmd = 'git submodule foreach --recursive | cut -d"\'" -f2'
    submodules = ctx.cmd_and_log(cmd, quiet=Context.BOTH, cwd=ctx.srcnode)

    # Add the list of files of each submodules
    for sub_path in submodules.strip().splitlines():
        submodule_node = ctx.srcnode.make_node(sub_path)
        res += get_git_files(ctx, submodule_node)

    return res


def get_old_gen_files(ctx: BuildContext) -> List[Node]:
    """ Get the list of files that are on disk, have an extension that
        correspond to auto-generated files, and that are not committed.
    """

    # Get all the generated files that are on disk (excluded symlinks).
    # Do not use waf ant_glob because it follows symlinks
    gen_files = []
    for dirpath, dirnames, filenames in os.walk(ctx.srcnode.abspath()):
        parent_node = ctx.root.make_node(dirpath)
        for name in filenames:
            if is_gen_file(ctx, parent_node, name):
                path = os.path.join(dirpath, name)
                if not os.path.islink(path):
                    gen_files.append(parent_node.make_node(name))
        # Do not recurse in hidden directories (in particular the .build one),
        # this is useless
        for i in range(len(dirnames) - 1, -1, -1):
            if dirnames[i].startswith('.'):
                del dirnames[i]


    # Get all committed files
    git_files = set(get_git_files_recur(ctx))

    # Filter-out from gen_files the files that are committed
    gen_files_set = {node for node in gen_files if node not in git_files}

    # Filter-out the files that are produced by the build system.
    # This requires to post all task generators.
    for tgen in ctx.get_all_task_gen():
        tgen.post()
        for task in tgen.tasks:
            for output in task.outputs:
                try:
                    gen_files_set.remove(output)
                except KeyError:
                    pass

    # The files that are still in gen_files_set are old ones that should not
    # be on disk anymore.
    return sorted(gen_files_set, key=lambda x: str(x.abspath()))


def old_gen_files_detect(ctx: BuildContext) -> None:
    if ctx.cmd not in ['old-gen-files-detect', 'old-gen-files-delete']:
        return

    # Get the list of old generated files and print it
    old_gen_files = get_old_gen_files(ctx)

    # Interrupt the build
    ctx.groups = []

    if len(old_gen_files) == 0:
        print('All good, you have no old generated file on disk :-)')
        return

    if ctx.cmd == 'old-gen-files-detect':
        # Print old generated file names
        Logs.warn('Following files are old generated ones:')
        for node in old_gen_files:
            Logs.warn('  %s', node.path_from(ctx.srcnode))
        ctx.fatal(('Old generated files detected; '
                   'use old-gen-files-delete to remove them'))
    else:
        # Delete old generated files
        for node in old_gen_files:
            Logs.warn('removing %s', node.path_from(ctx.srcnode))
            node.delete()


class OldGenFilesDetect(BuildContext): # type: ignore[misc]
    '''detect old generated files on disk'''
    cmd = 'old-gen-files-detect'


class OldGenFilesDelete(BuildContext): # type: ignore[misc]
    '''delete old generated files on disk'''
    cmd = 'old-gen-files-delete'


# }}}
# {{{ Coverage


def do_coverage_start(ctx: BuildContext) -> None:
    cmd = '{0} --directory {1} --base-directory {2} --zerocounters'
    if ctx.exec_command(cmd.format(ctx.env.LCOV[0], ctx.bldnode.abspath(),
                                   ctx.srcnode.abspath())):
        ctx.fatal('failed to start coverage session')


def coverage_start_cmd(ctx: BuildContext) -> None:
    if ctx.cmd != 'coverage-start':
        return

    if ctx.env.PROFILE != 'coverage':
        ctx.fatal('coverage-start requires coverage profile, current is %s' %
                  ctx.env.PROFILE)

    do_coverage_start(ctx)

    print('You can now run some code, and use `waf coverage-end` to produce '
          'a coverage report.')

    # Interrupt the build
    ctx.groups = []


class CoverageStartClass(BuildContext): # type: ignore[misc]
    '''start a coverage session (requires coverage profile)'''
    cmd = 'coverage-start'


def coverage_end_cmd(ctx: BuildContext) -> None:
    if ctx.cmd != 'coverage-end':
        return

    if ctx.env.PROFILE != 'coverage':
        ctx.fatal('coverage-end requires coverage profile, current is %s' %
                  ctx.env.PROFILE)

    # The following code is adapted from
    # http://bind10.isc.org/wiki/TestCodeCoverage

    # Create empty gcda files for every gcno file if they do not exist
    gcno_nodes = ctx.bldnode.ant_glob('**/*.gcno', excl='*.pic.*', quiet=True)
    for gcno_node in gcno_nodes:
        gcda_node = gcno_node.change_ext('.gcda')
        if not gcda_node.exists():
            os.mknod(gcda_node.abspath())

    # Generate the lcov trace file.
    lcov_all_file = ctx.bldnode.make_node('lcov-all.info')
    cmd = ('{0} --capture --ignore-errors gcov,source --directory {1} '
           '--base-directory {2} --output-file {3}')
    if ctx.exec_command(cmd.format(ctx.env.LCOV[0], ctx.bldnode.abspath(),
                                   ctx.srcnode.abspath(),
                                   lcov_all_file.abspath())):
        ctx.fatal('failed to generate lcov trace file')

    # Remove files not needed in the report
    lcov_file = ctx.bldnode.make_node('lcov.info')
    cmd = '{0} --remove {1} "/usr/*" --output {2}'
    if ctx.exec_command(cmd.format(ctx.env.LCOV[0], lcov_all_file.abspath(),
                                   lcov_file.abspath())):
        ctx.fatal('failed to purify lcov trace file')
    lcov_all_file.delete()

    # Generate HTML report
    now = datetime.datetime.now()
    report_dir_name = 'coverage-report-{:%Y%m%d-%H%M%S}'.format(now)
    report_dir = ctx.srcnode.make_node(report_dir_name)
    report_dir.delete(evict=False)
    cmd = '{0} -o {1} {2}'
    if ctx.exec_command(cmd.format(ctx.env.GENHTML[0], report_dir.abspath(),
                                   lcov_file.abspath())):
        ctx.fatal('failed to generate HTML report')

    # Produce a symlink to the report directory
    report_link = ctx.srcnode.make_node('coverage-report')
    try:
        os.remove(report_link.abspath())
    except OSError:
        pass
    os.symlink(report_dir.abspath(), report_link.abspath())

    print('')
    print('lcov data available in %s' % lcov_file.abspath())
    print('Coverage report produced in %s' % report_dir.abspath())
    print('')

    # Interrupt the build
    ctx.groups = []


class CoverageReportClass(BuildContext): # type: ignore[misc]
    '''end a coverage session and produce a report'''
    cmd = 'coverage-end'


# }}}

# {{{ BLK


def ensure_clang_rewrite_blocks(ctx: BuildContext) -> None:
    # Ensure clang_rewrite_block tgen is posted
    if not getattr(ctx.clang_rewrite_blocks_tgen, 'posted', False):
        ctx.clang_rewrite_blocks_tgen.post()
    if not hasattr(ctx, 'clang_rewrite_block_task'):
        ctx.clang_rewrite_blocks_task = (
            ctx.clang_rewrite_blocks_tgen.link_task
        )
        ctx.env.CLANG_REWRITE_BLOCKS = (
            ctx.clang_rewrite_blocks_tgen.link_task.outputs[0].abspath()
        )


def compute_clang_extra_cflags(self: BuildContext, clang_flags: List[str],
                               cflags_var: str) -> List[str]:
    ''' Compute clang cflags for a task generator from CFLAGS '''
    def keep_flag(flag: str) -> bool:
        if not flag.startswith('-I') and not flag.startswith('-D'):
            return False
        return flag not in clang_flags

    cflags: List[str] = self.env[cflags_var]
    return [flag for flag in cflags if keep_flag(flag)]


class Blk2c(Task): # type: ignore[misc]
    run_str = ['rm -f ${TGT}',
               ('${CLANG_REWRITE_BLOCKS} -x c ${CLANG_REWRITE_FLAGS} '
                '${CLANG_CFLAGS} -DIS_CLANG_BLOCKS_REWRITER '
                '${CLANG_EXTRA_CFLAGS} ${CPPPATH_ST:INCPATHS} '
                '${SRC} -o ${TGT}')]
    ext_out = [ '.c' ]
    color = 'CYAN'

    @classmethod
    def keyword(cls: Type['Blk2c']) -> str:
        return 'Rewriting'


@TaskGen.feature('c')
@TaskGen.before_method('process_source')
def init_c_ctx(self: TaskGen) -> None:
    self.blk2c_tasks = []
    self.clang_check_tasks = []
    self.env.CLANG_CFLAGS = self.to_list(getattr(self, 'cflags', []))


@TaskGen.feature('c')
@TaskGen.after_method('propagate_uselib_vars')
def update_blk2c_envs(self: TaskGen) -> None:
    if not self.blk2c_tasks:
        return

    # Compute clang extra cflags from gcc flags
    extra_cflags = compute_clang_extra_cflags(self, self.env.CLANG_FLAGS,
                                              'CFLAGS')

    # Update Blk2c tasks environment
    for task in self.blk2c_tasks:
        task.cwd = self.env.PROJECT_ROOT
        task.env.CLANG_EXTRA_CFLAGS = extra_cflags


@TaskGen.extension('.blk')
def process_blk(self: TaskGen, node: Node) -> None:
    if self.env.COMPILER_CC == 'clang':
        # clang is our C compiler -> directly compile the file
        self.create_compiled_task('c', node)
    else:
        # clang is not our C compiler -> it has to be rewritten first
        ensure_clang_rewrite_blocks(self.bld)
        blk_c_node = node.change_ext_src('.blk.c')

        if not blk_c_node in self.env.GEN_FILES:
            self.env.GEN_FILES.add(blk_c_node)

            # Create block rewrite task.
            blk_task = self.create_task('Blk2c', node, blk_c_node)
            self.blk2c_tasks.append(blk_task)

        # Create C compilation task for the generated C source.
        self.create_compiled_task('c', blk_c_node)


# }}}
# {{{ BLKK

class Blkk2cc(Task): # type: ignore[misc]
    run_str = ['rm -f ${TGT}',
               ('${CLANG_REWRITE_BLOCKS} -x c++ ${CLANGXX_REWRITE_FLAGS} '
                '${CLANGXX_EXTRA_CFLAGS} ${CPPPATH_ST:INCPATHS} '
                '${SRC} -o ${TGT}')]
    ext_out = [ '.cc' ]
    color = 'CYAN'

    @classmethod
    def keyword(cls: Type['Blkk2cc']) -> str:
        return 'Rewriting'


@TaskGen.feature('cxx')
@TaskGen.before_method('process_source')
def init_cxx_ctx(self: TaskGen) -> None:
    self.blkk2cc_tasks = []


@TaskGen.feature('cxx')
@TaskGen.after_method('propagate_uselib_vars')
def update_blk2cc_envs(self: TaskGen) -> None:
    if self.blkk2cc_tasks:
        # Compute clang extra cflags from g++ flags
        extra_flags = compute_clang_extra_cflags(
            self, self.env.CLANGXX_REWRITE_FLAGS, 'CXXFLAGS')

        # Update Blk2cc tasks environment
        for task in self.blkk2cc_tasks:
            task.env.CLANGXX_EXTRA_CFLAGS = extra_flags


@TaskGen.extension('.blkk')
def process_blkk(self: TaskGen, node: Node) -> None:
    if self.env.COMPILER_CXX == 'clang++':
        # clang++ is our C++ compiler -> directly compile the file
        self.create_compiled_task('cxx', node)
    else:
        # clang++ is not our C++ compiler -> it has to be rewritten first
        ensure_clang_rewrite_blocks(self.bld)
        blkk_cc_node = node.change_ext_src('.blkk.cc')

        if not blkk_cc_node in self.env.GEN_FILES:
            self.env.GEN_FILES.add(blkk_cc_node)

            # Create block rewrite task.
            blkk_task = self.create_task('Blkk2cc', node, blkk_cc_node)
            blkk_task.cwd = self.env.PROJECT_ROOT
            self.blkk2cc_tasks.append(blkk_task)

        # Create CC compilation task for the generated c++ source.
        self.create_compiled_task('cxx', blkk_cc_node)


# }}}
# {{{ PERF


class Perf2c(Task): # type: ignore[misc]
    run_str = '${GPERF} --language ANSI-C --output-file ${TGT} ${SRC}'
    color   = 'BLUE'

    @classmethod
    def keyword(cls: Type['Perf2c']) -> str:
        return 'Generating'


@TaskGen.extension('.perf')
def process_perf(self: TaskGen, node: Node) -> None:
    c_node = node.change_ext_src('.c')

    if not c_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(c_node)
        self.create_task('Perf2c', node, c_node, cwd=self.bld.srcnode)

    self.source.extend([c_node])


# }}}
# {{{ LEX

class Lex2c(Task): # type: ignore[misc]
    run_str = ['rm -f ${TGT}', '${FLEX_SH} ${SRC} ${TGT}']
    color   = 'BLUE'

    @classmethod
    def keyword(cls: Type['Lex2c']) -> str:
        return 'Generating'


@TaskGen.extension('.l')
def process_lex(self: TaskGen, node: Node) -> None:
    c_node = node.change_ext_src('.c')

    if not c_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(c_node)
        self.create_task('Lex2c', node, c_node, cwd=self.bld.srcnode)

    self.source.extend([c_node])


# }}}
# {{{ FC


class FirstInputStrTask(Task): # type: ignore[misc]

    def __str__(self) -> str:
        node = self.inputs[0]
        node_path: str = node.path_from(node.ctx.launch_node())
        return node_path


class Fc2c(FirstInputStrTask):
    run_str = ['rm -f ${TGT}', '${FARCHC} -c -o ${TGT} ${SRC[0].abspath()}']
    color   = 'BLUE'
    before  = ['Blk2c', 'Blkk2cc', 'ClangCheck']
    ext_out = ['.h']

    @classmethod
    def keyword(cls: Type['Fc2c']) -> str:
        return 'Generating'

    def scan(self) -> Tuple[Optional[List[str]], Optional[List[str]]]:
        """ Parses the .fc file to get the dependencies. """
        node = self.inputs[0]
        lines = node.read().splitlines()
        variable_name_found = False
        deps = []

        for line in lines:
            line = line.strip()
            if len(line) > 0 and line[0] != '#':
                if variable_name_found:
                    dep_node = node.parent.find_node(line)
                    if dep_node is None:
                        err = 'cannot find `{0}` when scanning `{1}`'
                        node.ctx.fatal(err.format(line, node))
                    deps.append(dep_node)
                else:
                    variable_name_found = True

        return (deps, None)


@TaskGen.extension('.fc')
def process_fc(self: TaskGen, node: Node) -> None:
    ctx = self.bld

    # Ensure farchc tgen is posted
    if not getattr(ctx.farchc_tgen, 'posted', False):
        ctx.farchc_tgen.post()
    if not hasattr(ctx, 'farchc_task'):
        ctx.farchc_task = ctx.farchc_tgen.link_task
        ctx.env.FARCHC = ctx.farchc_tgen.link_task.outputs[0].abspath()

    # Handle file
    h_node = node.change_ext_src('.fc.c')
    if not h_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(h_node)
        inputs = [node, ctx.farchc_tgen.link_task.outputs[0]]
        farch_task = self.create_task('Fc2c', inputs, h_node)
        farch_task.set_run_after(ctx.farchc_task)


# }}}
# {{{ TOKENS


class Tokens2c(Task): # type: ignore[misc]
    run_str = ('${TOKENS_SH} ${SRC[0].abspath()} ${TGT[0]} && ' +
               '${TOKENS_SH} ${SRC[0].abspath()} ${TGT[1]}')
    color   = 'BLUE'
    before  = ['Blk2c', 'Blkk2cc', 'ClangCheck']
    ext_out = ['.h', '.c']

    @classmethod
    def keyword(cls: Type['Tokens2c']) -> str:
        return 'Generating'


@TaskGen.extension('.tokens')
def process_tokens(self: TaskGen, node: Node) -> None:
    c_node = node.change_ext_src('tokens.c')
    h_node = node.change_ext_src('tokens.h')

    if not h_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(h_node)
        self.create_task('Tokens2c', [node], [c_node, h_node])

    self.source.append(c_node)


# }}}
# {{{ IOP

# IOPC options for a given sources path
class IopcOptions:

    def __init__(self, ctx: BuildContext, path: Optional[Node] = None,
                 class_range: Optional[str] = None,
                 includes: Optional[List[str]] = None,
                 json_path: Optional[str] = None,
                 ts_path: Optional[str] = None):
        self.ctx = ctx
        self.path = path or ctx.path
        self.class_range = class_range

        # Evaluate include nodes
        self.includes = set()
        if includes is not None:
            for include in Utils.to_list(includes):
                node = self.path.find_node(include)
                if node is None or not node.isdir():
                    msg = 'cannot find IOP include {0} from {1}'
                    ctx.fatal(msg.format(include, self.path))
                self.includes.add(node)

        self.computed_includes: Optional[str] = None

        # json path
        if json_path:
            self.json_node = self.path.make_node(json_path)
        else:
            self.json_node = None

        # typescript path
        if ts_path:
            self.ts_node = self.path.make_node(ts_path)
        else:
            self.ts_node = None

        # Add options in global cache
        assert not self.path in ctx.iopc_options
        ctx.iopc_options[self.path] = self

    @property
    def languages(self) -> str:
        """ Get the list of languages for iopc """
        res = 'c'
        if self.json_node:
            res += ',json'
        if self.ts_node:
            res += ',typescript'
        return res

    @property
    def class_range_option(self) -> str:
        """ Get the class-id range option for iopc """
        if self.class_range:
            return '--class-id-range={0}'.format(self.class_range)
        else:
            return ''

    @property
    def json_output_option(self) -> str:
        """ Get the json-output-path option for iopc """
        if self.json_node:
            return '--json-output-path={0}'.format(self.json_node)
        else:
            return ''

    @property
    def ts_output_option(self) -> str:
        """ Get the typescript-output-path option for iopc """
        if self.ts_node:
            return '--typescript-output-path={0}'.format(self.ts_node)
        else:
            return ''

    def get_includes_recursive(self, includes: Set[Node],
                               seen_opts: Set['IopcOptions']) -> None:
        """ Recursively get the IOP include paths for the current node """

        # Detect infinite recursions
        if self in seen_opts:
            return
        seen_opts.add(self)

        # Add includes of the current object in the resulting set
        includes.update(self.includes)

        # Recurse on the included nodes
        for node in self.includes:
            if node in self.ctx.iopc_options:
                self.ctx.iopc_options[node].get_includes_recursive(includes,
                                                                   seen_opts)

    @property
    def includes_option(self) -> str:
        """ Get the -I option for iopc """
        if self.computed_includes is None:
            includes: Set[Node] = set()
            seen_opts: Set['IopcOptions'] = set()
            self.get_includes_recursive(includes, seen_opts)
            if includes:
                nodes = [node.abspath() for node in includes]
                nodes.sort()
                self.computed_includes = '-I{0}'.format(':'.join(nodes))
            else:
                self.computed_includes = ''
        return self.computed_includes


class Iop2c(FirstInputStrTask):
    color   = 'BLUE'
    ext_out = ['.h', '.c']
    before  = ['Blk2c', 'Blkk2cc', 'ClangCheck']

    @classmethod
    def keyword(cls: Type['Iop2c']) -> str:
        return 'Generating'

    def get_cwd(self) -> Node:
        return self.inputs[0].parent

    def scan(self) -> Tuple[Optional[List[str]], Optional[List[str]]]:
        """ Gets the dependencies of the current IOP file.
            It uses the --depends command of iopc.
        """
        node = self.inputs[0]
        depfile = node.change_ext('.iop.d')

        # Manually redirect output to /dev/null because we don't want IOP
        # errors to be printed in double (once here, and once in build).
        # exec_command does not seem to allow dropping the output :-(...
        cmd = ('{iopc} {includes} --depends {depfile} -o {outdir} {source} '
               '> /dev/null 2>&1')
        cmd = cmd.format(iopc=self.inputs[1].abspath(),
                         includes=self.env.IOP_INCLUDES,
                         depfile=depfile.abspath(),
                         outdir=self.outputs[0].parent.abspath(),
                         source=node.abspath())
        if self.exec_command(cmd, cwd=self.get_cwd()):
            # iopc falied, run should fail too
            self.scan_failed = True
            return ([], None)

        deps = depfile.read().splitlines()
        deps = [node.ctx.root.make_node(dep) for dep in deps]

        return (deps, None)

    def run(self) -> int:
        cmd = ('{iopc} --Wextra --language {languages} --c-resolve-includes '
               '{includes} {class_range} {json_output} {ts_output}  {source}')
        cmd = cmd.format(iopc=self.inputs[1].abspath(),
                         languages=self.env.IOP_LANGUAGES,
                         includes=self.env.IOP_INCLUDES,
                         class_range=self.env.IOP_CLASS_RANGE,
                         json_output=self.env.IOP_JSON_OUTPUT,
                         ts_output=self.env.IOP_TS_OUTPUT,
                         source=self.inputs[0].abspath())
        self.last_cmd = cmd
        res: int = self.exec_command(cmd, cwd=self.get_cwd())
        if res and not getattr(self, 'scan_failed', False):
            self.bld.fatal("scan should have failed for %s" % self.inputs[0])
        return res


RE_IOP_PACKAGE = re.compile(r'^package (.*);$', re.MULTILINE)

def iop_get_package_path(self: BuildContext, node: Node) -> str:
    """ Get the 'package path' of a IOP file.
        It opens the IOP file, parses the 'package' line, and returns a string
        where dots are replaced by slashes.
    """
    match = RE_IOP_PACKAGE.search(node.read())
    if match is None:
        self.bld.fatal('no package declaration found in %s' % node)
        return '' # Dummy return
    return match.group(1).replace('.', '/')


@TaskGen.extension('.iop')
def process_iop(self: TaskGen, node: Node) -> None:
    ctx = self.bld

    # Ensure iopc tgen is posted
    if not getattr(ctx.iopc_tgen, 'posted', False):
        ctx.iopc_tgen.post()
    if not hasattr(ctx, 'iopc_task'):
        ctx.iopc_task = ctx.iopc_tgen.link_task

    # Handle file
    c_node = node.change_ext_src('.iop.c')
    if not c_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(c_node)

        # Get options
        if self.path in ctx.iopc_options:
            opts = ctx.iopc_options[self.path]
        else:
            opts = IopcOptions(ctx, path=self.path)

        # Build list of outputs
        outputs = [c_node,
                   node.change_ext_src('.iop.h'),
                   node.change_ext_src('-tdef.iop.h'),
                   node.change_ext_src('-t.iop.h')]
        if opts.json_node or opts.ts_node:
            package_path = iop_get_package_path(self, node)
            if opts.json_node:
                json_path = package_path + '.iop.json'
                outputs.append(opts.json_node.make_node(json_path))
            if opts.ts_node:
                ts_path = package_path + '.iop.ts'
                outputs.append(opts.ts_node.make_node(ts_path))

        # Create iopc task (add iopc itself in the inputs so that IOP files
        # are rebuilt if iopc changes)
        inputs = [node, ctx.iopc_tgen.link_task.outputs[0]]
        task = self.create_task('Iop2c', inputs, outputs)
        task.set_run_after(ctx.iopc_task)

        # Set options in environment
        task.env.IOP_LANGUAGES   = opts.languages
        task.env.IOP_INCLUDES    = opts.includes_option
        task.env.IOP_CLASS_RANGE = opts.class_range_option
        task.env.IOP_JSON_OUTPUT = opts.json_output_option
        task.env.IOP_TS_OUTPUT   = opts.ts_output_option

    self.source.append(c_node)


# }}}
# {{{ LD


@TaskGen.extension('.ld')
def process_ld(self: TaskGen, node: Node) -> None:
    self.env.append_value('LDFLAGS',
                          ['-Xlinker', '--version-script',
                           '-Xlinker', node.abspath()])


# }}}
# {{{ PXC


class Pxc2Pxd(FirstInputStrTask):
    run_str = ('${PXCC} ${CLANG_FLAGS} ${CLANG_CFLAGS} ${CLANG_EXTRA_CFLAGS} '
               '-fno-blocks ${CPPPATH_ST:INCPATHS} ${SRC[0].abspath()} '
               '-o ${TGT}')
    color   = 'BLUE'
    before  = 'cython'
    after   = 'Iop2c'
    scan    = c_preproc.scan # pxc files are C-like files

    @classmethod
    def keyword(cls: Type['Pxc2Pxd']) -> str:
        return 'Pxcc'


@TaskGen.extension('.pxc')
def process_pxcc(self: TaskGen, node: Node) -> None:
    ctx = self.bld

    # Ensure pxcc tgen is posted
    if not getattr(ctx.pxcc_tgen, 'posted', False):
        ctx.pxcc_tgen.post()
    if not hasattr(ctx, 'pxcc_task'):
        ctx.pxcc_task = ctx.pxcc_tgen.link_task
        ctx.env.PXCC = ctx.pxcc_tgen.link_task.outputs[0].abspath()

    # Handle file
    pxd_node = node.change_ext_src('_pxc.pxd')

    if pxd_node not in self.env.GEN_FILES:
        self.env.GEN_FILES.add(pxd_node)
        inputs = [node, ctx.pxcc_tgen.link_task.outputs[0]]
        pxc_task = self.create_task('Pxc2Pxd', inputs, [pxd_node],
                                    cwd=self.env.PROJECT_ROOT)
        pxc_task.set_run_after(ctx.pxcc_task)


# }}}
# {{{ .c checks using clang


class ClangCheck(Task): # type: ignore[misc]
    run_str = ('${CLANG} -x c -O0 -fsyntax-only ${CLANG_FLAGS} '
               '${CLANG_CFLAGS} ${CLANG_EXTRA_CFLAGS} ${CPPPATH_ST:INCPATHS} '
               '${SRC} -o /dev/null')
    color = 'BLUE'

    @classmethod
    def keyword(cls: Type['ClangCheck']) -> str:
        return 'Checking'


@TaskGen.feature('c')
@TaskGen.after_method('propagate_uselib_vars')
def update_clang_check_envs(self: TaskGen) -> None:
    if self.clang_check_tasks:
        # Compute clang extra cflags from gcc flags
        extra_flags = compute_clang_extra_cflags(self, self.env.CLANG_FLAGS,
                                                 'CFLAGS')
        for task in self.clang_check_tasks:
            task.env.CLANG_EXTRA_CFLAGS = extra_flags


@TaskGen.extension('.c')
def process_c_for_check(self: TaskGen, node: Node) -> None:
    # Call standard C hook
    c_task = c_tool.c_hook(self, node)

    # Do not check files in .pic task generators (they are already checked in
    # the original task generator)
    if self.name.endswith('.pic'):
        return

    # Test if checks are globally disabled...
    if not self.env.DO_CHECK:
        return

    # ...or locally disabled
    if hasattr(self, 'nocheck'):
        if isinstance(self.nocheck, bool):
            if self.nocheck:
                # Checks are disabled for this task generator
                return
        else:
            if node.path_from(self.path) in self.nocheck:
                # Checks are disabled for this node
                return

    # Do not check generated files, or files for which a check task was
    # already created
    if node in self.env.GEN_FILES or node in self.env.CHECKED_FILES:
        return

    # Create a clang check task
    self.env.CHECKED_FILES.add(node)
    clang_check_task = self.create_task('ClangCheck', node)
    clang_check_task.cwd = self.env.PROJECT_ROOT
    self.clang_check_tasks.append(clang_check_task)
    c_task.set_run_after(clang_check_task)


# }}}

# {{{ options

def options(ctx: OptionsContext) -> None:
    # Load C/C++ compilers
    ctx.load('compiler_c')
    ctx.load('compiler_cxx')

    # Python/cython
    ctx.load('python')
    ctx.load('cython_intersec')

# }}}
# {{{ configure
# {{{ llvm/clang

def llvm_clang_configure(ctx: ConfigurationContext) -> None:
    # Minimum supported version
    llvm_min_version = 9

    # Find llvm-config
    llvm_version_major = None
    llvm_version_minor = None

    # Try default version first
    if ctx.find_program('llvm-config', var='LLVM_CONFIG', mandatory=False):
        # Get llvm version
        llvm_version = ctx.cmd_and_log(ctx.env.LLVM_CONFIG + ['--version'])
        llvm_version = tuple(map(int, llvm_version.strip().split('.')))
        llvm_version_major = llvm_version[0]
        llvm_version_minor = llvm_version[1]

        if llvm_version_major < llvm_min_version:
            Logs.warn('llvm-config found with version {0}, '
                      'but is not supported by lib-common, '
                      'lib-common only supports llvm versions >= {1}'
                      .format(llvm_version_major, llvm_min_version))
            llvm_version_major = None
            del ctx.env.LLVM_CONFIG

    # If the default version is not found or supported, try explicit versions
    if llvm_version_major is None:
        # Try up to version 99
        for version in range(llvm_min_version, 100):
            if ctx.find_program('llvm-config-{0}'.format(version),
                                var='LLVM_CONFIG', mandatory=False):
                llvm_version_major = version
                break
        else:
            ctx.fatal('supported version of llvm-config not found, '
                      'lib-common only supports llvm versions >= {0}, '
                      'please install supported version of llvm-dev or '
                      'llvm-devel'.format(llvm_min_version))

    # Get llvm flags
    llvm_flags_env_args = {
        'CXXFLAGS_llvm': ['--cxxflags'],
        'LDFLAGS_llvm': ['--ldflags'],
        'RPATH_llvm': ['--libdir'],
        'INCLUDES_llvm': ['--includedir'],
    }

    for env_name, cmd_args in llvm_flags_env_args.items():
        llvm_flags = ctx.cmd_and_log(ctx.env.LLVM_CONFIG + cmd_args)
        llvm_flags = shlex.split(llvm_flags)
        ctx.env.append_unique(env_name, llvm_flags)

    llvm_libs = ctx.cmd_and_log(ctx.env.LLVM_CONFIG + ['--libs'])
    llvm_libs = shlex.split(llvm_libs)
    llvm_libs = [x[len('-l'):] for x in llvm_libs]
    ctx.env.append_value('LIB_llvm', llvm_libs)

    # Get clang flags
    ctx.env.append_value('LIB_clang', ['clang'])
    ctx.env.LDFLAGS_clang = ctx.env.LDFLAGS_llvm
    ctx.env.RPATH_clang = ctx.env.RPATH_llvm
    ctx.env.INCLUDES_clang = ctx.env.INCLUDES_llvm

    # Get clang cpp flags
    # On some installations of clang, the symlinks
    # libclang-cpp.so.x -> libclang-cpp-x.so
    # libclang-cpp-x.so -> libclang-cpp.so are not done.
    # Use filename instead.
    clang_cpp_lib_major = ':libclang-cpp.so.{0}'.format(llvm_version_major)
    clang_cpp_lib_major_minor = \
        ':libclang-cpp.so.{0}.{1}'.format(llvm_version_major,
                                          llvm_version_minor)
    clang_cpp_lib = ''
    ctx.env.RPATH_clang_cpp = ctx.env.RPATH_clang
    for path in ctx.env.RPATH_clang_cpp:
        major_path = os.path.join(path, clang_cpp_lib_major[1:])
        if os.path.isfile(major_path):
            clang_cpp_lib = clang_cpp_lib_major
            break
        maj_minor_path = os.path.join(path, clang_cpp_lib_major_minor[1:])
        if os.path.isfile(maj_minor_path):
            clang_cpp_lib = clang_cpp_lib_major_minor
            break
    if not clang_cpp_lib:
        ctx.fatal('cannot find libclang-cpp.so.{0} or libclang-cpp.so.{0}.{1}'
                  'in any clang path'.format(llvm_version_major,
                                             llvm_version_minor))
    ctx.env.append_value('LIB_clang_cpp', [clang_cpp_lib])
    ctx.env.CXXFLAGS_clang_cpp =  ctx.env.CXXFLAGS_llvm
    ctx.env.LDFLAGS_clang_cpp =  ctx.env.LDFLAGS_clang
    ctx.env.INCLUDES_clang_cpp = ctx.env.INCLUDES_clang

    # Check installation of libclang
    ctx.msg('Checking for libclang', ctx.env.INCLUDES_clang)
    ctx.check_cc(header_name='clang-c/Index.h', use='clang',
                 errmsg='clang-c not available in libclang, libclang-dev '
                        'or clang-devel may be missing', nocheck=True)

    # Get clang binaries from llvm
    llvm_bindir = ctx.cmd_and_log(ctx.env.LLVM_CONFIG + ['--bindir'])
    llvm_bindir = llvm_bindir.strip()

    # Set clang if used as C compiler, otherwise, use llvm version
    if ctx.env.COMPILER_CC == 'clang':
        ctx.env.CLANG = ctx.env.CC
    else:
        ctx.env.CLANG = [osp.join(llvm_bindir, 'clang')]
    ctx.msg("Checking for program 'clang'", ctx.env.CLANG[0])
    if not check_exe(ctx.env.CLANG[0]):
        ctx.fatal('`{0}` is not a valid executable, clang may be missing'
                  .format(ctx.env.CLANG[0]))

    # Set clang++ if used as C++ compiler, otherwise, use llvm version
    if ctx.env.COMPILER_CXX == 'clang++':
        ctx.env.CLANGXX = ctx.env.CXX
    else:
        ctx.env.CLANGXX = [osp.join(llvm_bindir, 'clang++')]
    ctx.msg("Checking for program 'clang++'", ctx.env.CLANGXX[0])
    if not check_exe(ctx.env.CLANGXX[0]):
        ctx.fatal('`{0}` is not a valid executable, clang++ may be missing'
                  .format(ctx.env.CLANGXX[0]))


# }}}
# {{{ compilation profiles


def get_cflags(ctx: ConfigurationContext, args: List[str]) -> List[str]:
    # TODO waf: maybe rewrite it in full-python after getting rid of make
    flags: str = ctx.cmd_and_log(ctx.env.CFLAGS_SH + args)
    return flags.strip().replace('"', '').split(' ')


def profile_default(
        ctx: ConfigurationContext,
        no_assert: bool = False,
        allow_no_double_fpic: bool = True,
        allow_fake_versions: bool = True,
        use_sanitizer: bool = False,
        optim_level: int = 2,
        fortify_source: Optional[str] = '-D_FORTIFY_SOURCE=2') -> None:
    # Load C/C++ compilers
    ctx.load('compiler_c')
    ctx.load('compiler_cxx')

    # Load llvm/clang
    llvm_clang_configure(ctx)

    # Get compilation flags with cflags.sh
    ctx.find_program('cflags.sh', var='CFLAGS_SH',
                     path_list=[os.path.join(ctx.path.abspath(), 'build')])

    ctx.env.CFLAGS = get_cflags(ctx, ctx.env.CC)

    oflags = ['-O' + str(optim_level)]
    ctx.env.OFLAGS = oflags
    ctx.env.CFLAGS += oflags
    ctx.env.CXXFLAGS += oflags

    ctx.env.CFLAGS += [
        '-ggdb3',
        '-fno-omit-frame-pointer',
        '-fvisibility=hidden',
    ]
    ctx.env.LINKFLAGS = [
        '-Wl,--export-dynamic',
        # Debian stretch uses --enable-new-dtags by default, which breaks
        # indirect library dependencies loading when using -rpath.
        # See https://sourceware.org/ml/binutils/2014-02/msg00031.html
        #  or https://reviews.llvm.org/D8836
        '-Xlinker', '--disable-new-dtags',
        '-Wl,--disable-new-dtags',
    ]
    ctx.env.LIB = [
        'pthread',
        'dl',
        'm',
        'rt',
    ]

    ctx.env.CXXFLAGS = get_cflags(ctx, ctx.env.CXX)
    ctx.env.CXXFLAGS += [
        '-ggdb3',
        '-D__STDC_LIMIT_MACROS',
        '-D__STDC_CONSTANT_MACROS',
        '-D__STDC_FORMAT_MACROS',
        '-fno-omit-frame-pointer',
        '-fvisibility=hidden',
    ]

    if ctx.env.COMPILER_CC == 'clang':
        # C compilation directly done using clang
        ctx.env.CFLAGS += ['-x', 'c']
        ctx.env.CLANG_FLAGS = ctx.env.CFLAGS
    else:
        # Probably compiling with gcc; we'll need the .blk -> .c rewriting
        # pass with our modified clang
        ctx.env.CLANG_FLAGS = get_cflags(ctx, ctx.env.CLANG)
        ctx.env.CLANG_FLAGS += oflags
        ctx.env.CLANG_REWRITE_FLAGS = get_cflags(
            ctx, ctx.env.CLANG + ['rewrite'])
        ctx.env.CLANG_REWRITE_FLAGS += oflags

    if ctx.env.COMPILER_CXX == 'clang++':
        # C++ compilation directly done using clang
        ctx.env.CXXFLAGS += ['-x', 'c++']
        ctx.env.CLANGXX_FLAGS = ctx.env.CXXFLAGS
    else:
        # Probably compiling with g++; we'll need the .blkk -> .cc rewriting
        # pass with our modified clang
        ctx.env.CLANGXX_FLAGS = get_cflags(ctx, ctx.env.CLANGXX)
        ctx.env.CLANGXX_FLAGS += oflags
        ctx.env.CLANGXX_REWRITE_FLAGS = get_cflags(
            ctx, ctx.env.CLANGXX + ['rewrite'])
        ctx.env.CLANGXX_REWRITE_FLAGS += oflags

    # Asserts
    if no_assert or ctx.get_env_bool('NOASSERT'):
        ctx.env.NDEBUG = True
        ctx.env.CFLAGS += ['-DNDEBUG']
        ctx.env.CXXFLAGS += ['-DNDEBUG']
        log = 'no'
    else:
        log = 'yes'
    ctx.msg('Enable asserts', log)

    if fortify_source is not None:
        ctx.env.CFLAGS += [fortify_source]

    # Checks
    if ctx.env.COMPILER_CC == 'clang' or ctx.get_env_bool('NOCHECK'):
        ctx.env.DO_CHECK = False
        log = 'no'
    else:
        ctx.env.DO_CHECK = True
        log = 'yes'
    ctx.msg('Do checks', log)

    # Disable double fPIC compilation for shared libraries?
    if allow_no_double_fpic and ctx.get_env_bool('NO_DOUBLE_FPIC'):
        ctx.env.DO_DOUBLE_FPIC = False
        ctx.env.CFLAGS += ['-fPIC']
        log = 'no'
    else:
        ctx.env.DO_DOUBLE_FPIC = True
        log = 'yes'
    ctx.msg('Double fPIC compilation for shared libraries', log)

    # Generate fake versions?
    if allow_fake_versions and ctx.get_env_bool('FAKE_VERSIONS'):
        ctx.env.FAKE_VERSIONS = True
        ctx.env.CFLAGS += ['-DFAKE_VERSIONS']
        ctx.env.CXXFLAGS += ['-DFAKE_VERSIONS']
        log = 'yes'
    else:
        ctx.env.FAKE_VERSIONS = False
        log = 'no'
    ctx.msg('Generate fake versions in binaries', log)

    # Use sanitizer for shared libraries?
    ctx.env.USE_SANITIZER = use_sanitizer
    if use_sanitizer and ctx.get_env_bool('SHARED_LIBRARY_SANITIZER'):
        ctx.env.SHARED_LIBRARY_SANITIZER = True
        log = 'yes'
    else:
        ctx.env.SHARED_LIBRARY_SANITIZER = False
        log = 'no'
    ctx.msg('Use sanitizer for shared libraries', log)


def profile_debug(ctx: ConfigurationContext,
                  allow_no_double_fpic: bool = True,
                  use_sanitizer: bool = False) -> None:
    profile_default(ctx, fortify_source=None,
                    allow_no_double_fpic=allow_no_double_fpic,
                    use_sanitizer=use_sanitizer, optim_level=0)

    cflags = [
        '-Wno-uninitialized', '-fno-inline', '-fno-inline-functions'
    ]
    ctx.env.CFLAGS += cflags
    ctx.env.CXXFLAGS += cflags


def profile_fuzzing(ctx: ConfigurationContext,
                    debug: bool = False,
                    asan: bool = False,
                    display_log: bool = False) -> None:
    Options.options.check_c_compiler = 'clang'
    Options.options.check_cxx_compiler = 'clang++'

    if debug:
        profile_debug(ctx)
    else:
        profile_default(ctx, fortify_source=None)

    ctx.env.USE_FUZZING = True

    asan_opt = ',address' if asan else ''
    fuzzing_flags = ['-D__fuzzing_log__'] if display_log else []
    fuzzing_flags += ['-D__fuzzing__', '-fsanitize=fuzzer-no-link' + asan_opt]

    ctx.env.FUZZING_CFLAGS = fuzzing_flags
    ctx.env.FUZZING_CXXFLAGS = fuzzing_flags
    ctx.env.FUZZING_LDFLAGS = ['-lstdc++', '-fsanitize=fuzzer' + asan_opt]


def profile_fuzzingcov(ctx: ConfigurationContext) -> None:
    profile_fuzzing(ctx, debug=True)

    fuzzing_coverage_flag = ['--coverage']

    ctx.env.CFLAGS += fuzzing_coverage_flag
    ctx.env.CXXFLAGS += fuzzing_coverage_flag
    ctx.env.LDFLAGS += fuzzing_coverage_flag


def profile_fuzzingdebug(ctx: ConfigurationContext) -> None:
    profile_fuzzing(ctx, debug=True, display_log=True)


def profile_release(ctx: ConfigurationContext) -> None:
    profile_default(ctx, no_assert=True,
                    allow_no_double_fpic=False,
                    allow_fake_versions=False)
    ctx.env.LINKFLAGS += ['-Wl,-x', '-rdynamic']
    ctx.env.WEBPACK_MODE = 'production'


def profile_asan(ctx: ConfigurationContext) -> None:
    Options.options.check_c_compiler = 'clang'
    Options.options.check_cxx_compiler = 'clang++'

    profile_debug(ctx, allow_no_double_fpic=False, use_sanitizer=True)

    ctx.env.LDFLAGS += ['-lstdc++']

    asan_flags = ['-fsanitize=address']
    ctx.env.SANITIZER_CFLAGS = asan_flags
    ctx.env.SANITIZER_CXXFLAGS = asan_flags
    ctx.env.SANITIZER_LDFLAGS = asan_flags


def profile_tsan(ctx: ConfigurationContext) -> None:
    Options.options.check_c_compiler = 'clang'
    Options.options.check_cxx_compiler = 'clang++'

    profile_debug(ctx, allow_no_double_fpic=False, use_sanitizer=True)

    tsan_flags = ['-fsanitize=thread']
    ctx.env.SANITIZER_CFLAGS = tsan_flags
    ctx.env.SANITIZER_CXXFLAGS = tsan_flags
    ctx.env.SANITIZER_LDFLAGS = tsan_flags


def profile_mem_bench(ctx: ConfigurationContext) -> None:
    profile_default(ctx, no_assert=True, fortify_source=None)

    flags = ['-DMEM_BENCH']
    ctx.env.CFLAGS += flags
    ctx.env.CXXFLAGS += flags


def profile_coverage(ctx: ConfigurationContext) -> None:
    profile_debug(ctx)
    ctx.find_program('lcov')
    ctx.find_program('genhtml')

    flags = ['-pg', '--coverage']
    ctx.env.CFLAGS += flags
    ctx.env.CXXFLAGS += flags
    ctx.env.LDFLAGS += flags

    do_coverage_start(ctx)
    ctx.msg('Starting coverage session', 'ok')


PROFILES: Dict[str, Callable[[ConfigurationContext], None]] = {
    'default':      profile_default,
    'debug':        profile_debug,
    'release':      profile_release,
    'asan':         profile_asan,
    'tsan':         profile_tsan,
    'mem-bench':    profile_mem_bench,
    'coverage':     profile_coverage,
    'fuzzing':      profile_fuzzing,
    'fuzzingcov':   profile_fuzzingcov,
    'fuzzingdebug': profile_fuzzingdebug,
}

# }}}

def configure(ctx: ConfigurationContext) -> None:
    ctx.env.CONFIGURE_TIME = time.time()

    # register_global_includes
    ConfigurationContext.register_global_includes = register_global_includes

    # Load compilation profile
    profile: str = os.environ.get('P', 'default')
    ctx.env.PROFILE = profile
    try:
        ctx.msg('Selecting profile', ctx.env.PROFILE)
        PROFILES[ctx.env.PROFILE](ctx)
    except KeyError:
        ctx.fatal('Profile `{0}` not found'.format(ctx.env.PROFILE))

    # Check dependencies
    ctx.find_program('objcopy', var='OBJCOPY')

    build_dir  = os.path.join(ctx.path.abspath(), 'build')
    ctx.find_program('run_checks.sh', path_list=[build_dir],
                     var='RUN_CHECKS_SH')
    ctx.find_program('tokens.sh', path_list=[build_dir], var='TOKENS_SH')
    if ctx.find_program('ctags', mandatory=False):
        ctx.find_program('ctags.sh', path_list=[build_dir], var='CTAGS_SH')

    # Python/cython
    ctx.load('python')
    ctx.load('cython_intersec')


class IsConfigurationContext(ConfigurationContext): # type: ignore[misc]

    def execute(self) -> None:
        # Run configure
        ConfigurationContext.execute(self)

        # Ensure local vimrc and syntastic configuration files are generated
        # after the end of the configure step
        gen_local_vimrc(self)
        gen_syntastic(self)


# }}}
# {{{ build

def build(ctx: BuildContext) -> None:
    Logs.info('Waf: Selected profile: %s', ctx.env.PROFILE)

    ctx.env.PROJECT_ROOT = ctx.srcnode
    ctx.env.GEN_FILES = set()
    ctx.env.CHECKED_FILES = set()

    register_get_cwd()

    # iopc options
    ctx.IopcOptions = IopcOptions
    ctx.iopc_options = {}

    # Register pre/post functions
    if ctx.env.NDEBUG:
        ctx.add_pre_fun(filter_out_zchk)
    if ctx.env.DO_DOUBLE_FPIC:
        ctx.add_pre_fun(compile_fpic)
    if ctx.env.USE_FUZZING:
        ctx.add_pre_fun(compile_fuzzing_programs)
    if ctx.env.USE_SANITIZER:
        ctx.add_pre_fun(compile_sanitizer)
    ctx.add_pre_fun(gen_tags)
    ctx.add_pre_fun(old_gen_files_detect)
    ctx.add_pre_fun(coverage_start_cmd)
    ctx.add_pre_fun(coverage_end_cmd)

# }}}
