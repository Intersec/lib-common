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

'''
Contains the code needed for backend compilation.
'''

import datetime
import os
import re
from itertools import chain

# pylint: disable = import-error
from waflib import TaskGen, Utils, Context, Errors, Options, Logs

from waflib.Build import BuildContext
from waflib.Configure import ConfigurationContext
from waflib.Task import Task
from waflib.TaskGen import extension
from waflib.Tools import c as c_tool
from waflib.Tools import c_preproc
from waflib.Tools import cxx
from waflib.Tools import ccroot
# pylint: enable = import-error


# {{{ use_whole

# These functions implement the use_whole attribute, allowing to link a
# library with -whole-archive

@TaskGen.feature('c', 'cprogram', 'cstlib')
@TaskGen.before_method('process_rule')
def prepare_whole(self):
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
def process_whole(self):
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

def filter_out_zchk(ctx):
    for g in ctx.groups:
        for i in range(len(g) - 1, -1, -1):
            tgen = g[i]
            features = tgen.to_list(getattr(tgen, 'features', []))
            if  tgen.name.startswith('zchk') and 'c' in features:
                del g[i]


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

def declare_fpic_lib(ctx, pic_name, orig_lib):
    orig_source     = orig_lib.to_list(getattr(orig_lib, 'source',     []))
    orig_use        = orig_lib.to_list(getattr(orig_lib, 'use',        []))
    orig_use_whole  = orig_lib.to_list(getattr(orig_lib, 'use_whole',  []))
    orig_depends_on = orig_lib.to_list(getattr(orig_lib, 'depends_on', []))
    orig_cflags     = orig_lib.to_list(getattr(orig_lib, 'cflags',     []))
    orig_includes   = orig_lib.to_list(getattr(orig_lib, 'includes',   []))

    ctx_path_bak = ctx.path
    ctx.path = orig_lib.path
    lib = ctx.stlib(target=pic_name,
                    features=orig_lib.features,
                    source=orig_source[:],
                    cflags=orig_cflags[:],
                    use=orig_use[:],
                    use_whole=orig_use_whole[:],
                    depends_on=orig_depends_on[:],
                    includes=orig_includes[:])
    ctx.path = ctx_path_bak

    lib.env.append_value('CFLAGS', ['-fPIC'])
    return lib


def compile_fpic(ctx):
    pic_libs = set()

    for tgen in ctx.get_all_task_gen():
        features = tgen.to_list(getattr(tgen, 'features', []))

        if not 'cshlib' in features:
            continue

        # Shared libraries must be compiled with the -fPIC compilation flag...
        tgen.env.append_value('CFLAGS', ['-fPIC'])

        # ...such as all the libraries they use
        def process_use_pic(tgen, use_attr):
            # for all the libraries used by tgen...
            use = tgen.to_list(getattr(tgen, use_attr, []))
            for i in range(len(use)):
                use_name = use[i]

                if use_name.endswith('.pic'):
                    # already a pic library
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

                # Replace the static library by the pic version in tgen
                # sources
                pic_name = use_name + '.pic'
                use[i] = pic_name

                # Declare the pic static library, if not done yet
                if not use_name in pic_libs:
                    pic_lib = declare_fpic_lib(ctx, pic_name, use_tgen)
                    pic_libs.add(use_name)
                    # Recurse, to deal with the static libraries that use some
                    # other static libraries.
                    process_use_pic(pic_lib, 'use')
                    process_use_pic(pic_lib, 'use_whole')

        # Process the use and use_whole lists
        process_use_pic(tgen, 'use')
        process_use_pic(tgen, 'use_whole')


def compile_sanitizer(ctx):
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
# {{{ Patch C tasks for compression


def patch_c_tasks_for_compression(ctx):
    '''
    This function recreates the c, cprogram and cshlib task classes in order
    to add the compression of the debug sections using objcopy.
    We can't simply replace the run_str field of each class because it was
    already compiled into a 'run' method at this point.
    '''
    # pylint: disable = invalid-name
    compress_str = '${OBJCOPY} --compress-debug-sections ${TGT}'

    class c(Task):
        run_str = [c_tool.c.orig_run_str, compress_str]
        vars    = c_tool.c.vars
        ext_in  = c_tool.c.ext_in
        scan    = c_preproc.scan
    c_tool.c = c

    class cprogram(ccroot.link_task):
        run_str = [c_tool.cprogram.orig_run_str, compress_str]
        vars    = c_tool.cprogram.vars
        ext_out = c_tool.cprogram.ext_out
        inst_to = c_tool.cprogram.inst_to
    c_tool.cprogram = cprogram

    class cshlib(cprogram):
        inst_to = c_tool.cshlib.inst_to
    c_tool.cshlib = cshlib


# }}}
# {{{ Execute commands from project root

def register_get_cwd():
    '''
    Execute the compiler's commands from the project root instead of the
    project build directory.
    This is important for us because some code (for example the Z tests
    registration) relies on the value of the __FILE__ macro.
    '''
    def get_cwd(self):
        return self.env.PROJECT_ROOT

    c_tool.c.get_cwd = get_cwd
    cxx.cxx.get_cwd = get_cwd
    ccroot.link_task.get_cwd = get_cwd
    TaskGen.task_gen.get_cwd = get_cwd


# }}}
# {{{ Register global includes


def register_global_includes(self, includes):
    ''' Register global includes (that are added to all the targets). '''
    for include in Utils.to_list(includes):
        node = self.path.find_node(include)
        if node is None or not node.isdir():
            msg = 'cannot find include path `{0}` from `{1}`'
            self.fatal(msg.format(include, self.path))
        self.env.append_unique('INCLUDES', node.abspath())


# }}}
# {{{ Deploy targets / patch tasks to build targets in the source directory

class DeployTarget(Task):
    color = 'CYAN'

    @classmethod
    def keyword(cls):
        return 'Deploying'

    def __str__(self):
        node = self.outputs[0]
        return node.path_from(node.ctx.launch_node())

    def run(self):
        # Create a hardlink from source to target
        out_node = self.outputs[0]
        out_node.delete(evict=False)
        os.link(self.inputs[0].abspath(), out_node.abspath())


@TaskGen.feature('cprogram', 'cxxprogram')
@TaskGen.after_method('apply_link')
def deploy_program(self):
    # Build programs in the corresponding source directory
    assert (len(self.link_task.outputs) == 1)
    node = self.link_task.outputs[0]
    self.link_task.outputs = [node.get_src()]


@TaskGen.feature('cshlib')
@TaskGen.after_method('apply_link')
def deploy_shlib(self):
    # Build C shared library in the corresponding source directory,
    # stripping the 'lib' prefix
    assert (len(self.link_task.outputs) == 1)
    node = self.link_task.outputs[0]
    assert (node.name.startswith('lib'))
    tgt = node.parent.get_src().make_node(node.name[len('lib'):])
    self.link_task.outputs = [tgt]


# }}}
# {{{ remove_dynlibs: option to remove all dynamic libraries at link

@TaskGen.feature('cshlib', 'cprogram')
@TaskGen.after_method('apply_link', 'process_use')
def remove_dynamic_libs(self):
    if getattr(self, 'remove_dynlibs', False):
        self.link_task.env.LIB = []

# }}}
# {{{ .local_vimrc.vim / syntastic configuration generation


def get_linter_flags(ctx, flags_key):
    include_flags = []
    for key in ctx.env:
        if key == 'INCLUDES' or key.startswith('INCLUDES_'):
            include_flags += ['-I' + value for value in ctx.env[key]]

    return ctx.env[flags_key] + ctx.env.CFLAGS_python3 + include_flags


def gen_local_vimrc(ctx):
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

    # Write file if it changed
    node = ctx.srcnode.make_node('.local_vimrc.vim')
    if not node.exists() or node.read() != content:
        node.write(content)
        ctx.msg('Writing local vimrc configuration file', node)


def gen_syntastic(ctx):
    """
    Syntastic is a vim syntax checker extension. It is not used by anybody
    anymore, but its configuration file is used by the YouCompleteMe plugin,
    that is used by some people.

    https://github.com/vim-syntastic/syntastic
    """
    def write_file(filename, what, envs):
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


def gen_tags(ctx):
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


class TagsClass(BuildContext):
    '''generate tags using ctags'''
    cmd = 'tags'


class EtagsClass(BuildContext):
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


def gen_file_keep(parent_node, name):
    ''' The purpose of this function is to exclude some files from the list of
        generated ones (because we don't want them to be deleted).
        TODO waf: avoid hardcoding this list (which should belong to mmsx).
    '''
    # Exclude event.iop.json files produced in bigdata products by the schema
    # library
    if name == 'event.iop.json' and parent_node.name != 'bigdata':
        return False
    return True


def is_gen_file(parent_node, name):
    for sfx in GEN_FILES_SUFFIXES:
        if name.endswith(sfx):
            return gen_file_keep(parent_node, name)
    return False


def get_git_files(ctx, repo_node):
    """ Get the list of committed files in a git repository. """

    # Call git ls-files to get the list of committed files
    git_ls_files = ctx.cmd_and_log(['git', 'ls-files'], quiet=Context.BOTH,
                                   cwd=repo_node)

    # Build nodes from the list
    res = [repo_node.make_node(p) for p in git_ls_files.strip().splitlines()]

    # Exclude symlinks
    res = [node for node in res if not os.path.islink(node.abspath())]

    return res


def get_git_files_recur(ctx):
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


def get_old_gen_files(ctx):
    """ Get the list of files that are on disk, have an extension that
        correspond to auto-generated files, and that are not committed.
    """

    # Get all the generated files that are on disk (excluded symlinks).
    # Do not use waf ant_glob because it follows symlinks
    gen_files = []
    for dirpath, dirnames, filenames in os.walk(ctx.srcnode.abspath()):
        parent_node = ctx.root.make_node(dirpath)
        for name in filenames:
            if is_gen_file(parent_node, name):
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
    gen_files = {node for node in gen_files if node not in git_files}

    # Filter-out the files that are produced by the build system.
    # This requires to post all task generators.
    for tgen in ctx.get_all_task_gen():
        tgen.post()
        for task in tgen.tasks:
            for output in task.outputs:
                try:
                    gen_files.remove(output)
                except KeyError:
                    pass

    # The files that are still in gen_files are old ones that should not be on
    # disk anymore.
    return sorted(gen_files, key=lambda x: x.abspath())


def old_gen_files_detect(ctx):
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


class OldGenFilesDetect(BuildContext):
    '''detect old generated files on disk'''
    cmd = 'old-gen-files-detect'


class OldGenFilesDelete(BuildContext):
    '''delete old generated files on disk'''
    cmd = 'old-gen-files-delete'


# }}}
# {{{ Coverage


def do_coverage_start(ctx):
    cmd = '{0} --directory {1} --base-directory {2} --zerocounters'
    if ctx.exec_command(cmd.format(ctx.env.LCOV[0], ctx.bldnode.abspath(),
                                   ctx.srcnode.abspath())):
        ctx.fatal('failed to start coverage session')


def coverage_start_cmd(ctx):
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


class CoverageStartClass(BuildContext):
    '''start a coverage session (requires coverage profile)'''
    cmd = 'coverage-start'


def coverage_end_cmd(ctx):
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
    report_dir = 'coverage-report-{:%Y%m%d-%H%M%S}'.format(now)
    report_dir = ctx.srcnode.make_node(report_dir)
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


class CoverageReportClass(BuildContext):
    '''end a coverage session and produce a report'''
    cmd = 'coverage-end'


# }}}

# {{{ BLK


def compute_clang_extra_cflags(self, clang_flags, cflags):
    ''' Compute clang cflags for a task generator from CFLAGS '''
    def keep_flag(flag):
        if not flag.startswith('-I') and not flag.startswith('-D'):
            return False
        return flag not in clang_flags

    cflags = self.env[cflags]
    return [flag for flag in cflags if keep_flag(flag)]


class Blk2c(Task):
    run_str = ['rm -f ${TGT}',
               ('${CLANG} -cc1 -x c ${CLANG_REWRITE_FLAGS} ${CLANG_CFLAGS} '
                '-rewrite-blocks -DIS_CLANG_BLOCKS_REWRITER '
                '${CLANG_EXTRA_CFLAGS} ${CPPPATH_ST:INCPATHS} '
                '${SRC} -o ${TGT}')]
    ext_out = [ '.c' ]
    color = 'CYAN'

    @classmethod
    def keyword(cls):
        return 'Rewriting'


@TaskGen.feature('c')
@TaskGen.before_method('process_source')
def init_c_ctx(self):
    self.blk2c_tasks = []
    self.clang_check_tasks = []
    self.env.CLANG_CFLAGS = self.to_list(getattr(self, 'cflags', []))


@TaskGen.feature('c')
@TaskGen.after_method('propagate_uselib_vars')
def update_blk2c_envs(self):
    if not self.blk2c_tasks:
        return

    # Compute clang extra cflags from gcc flags
    extra_cflags = compute_clang_extra_cflags(self, self.env.CLANG_FLAGS,
                                              'CFLAGS')

    # Update Blk2c tasks environment
    for task in self.blk2c_tasks:
        task.cwd = self.env.PROJECT_ROOT
        task.env.CLANG_EXTRA_CFLAGS = extra_cflags


@extension('.blk')
def process_blk(self, node):
    if self.env.COMPILER_CC == 'clang':
        # clang is our C compiler -> directly compile the file
        self.create_compiled_task('c', node)
    else:
        # clang is not our C compiler -> it has to be rewritten first
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

class Blkk2cc(Task):
    run_str = ['rm -f ${TGT}',
               ('${CLANGXX} -cc1 -x c++ ${CLANGXX_REWRITE_FLAGS} '
                '${CLANGXX_EXTRA_CFLAGS} -rewrite-blocks '
                '${CPPPATH_ST:INCPATHS} ${SRC} -o ${TGT}')]
    ext_out = [ '.cc' ]
    color = 'CYAN'

    @classmethod
    def keyword(cls):
        return 'Rewriting'


@TaskGen.feature('cxx')
@TaskGen.before_method('process_source')
def init_cxx_ctx(self):
    self.blkk2cc_tasks = []


@TaskGen.feature('cxx')
@TaskGen.after_method('propagate_uselib_vars')
def update_blk2cc_envs(self):
    if self.blkk2cc_tasks:
        # Compute clang extra cflags from g++ flags
        extra_flags = compute_clang_extra_cflags(
            self, self.env.CLANGXX_REWRITE_FLAGS, 'CXXFLAGS')

        # Update Blk2cc tasks environment
        for task in self.blkk2cc_tasks:
            task.env.CLANGXX_EXTRA_CFLAGS = extra_flags


@extension('.blkk')
def process_blkk(self, node):
    if self.env.COMPILER_CXX == 'clang++':
        # clang++ is our C++ compiler -> directly compile the file
        self.create_compiled_task('cxx', node)
    else:
        # clang++ is not our C++ compiler -> it has to be rewritten first
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


class Perf2c(Task):
    run_str = '${GPERF} --language ANSI-C --output-file ${TGT} ${SRC}'
    color   = 'BLUE'

    @classmethod
    def keyword(cls):
        return 'Generating'


@extension('.perf')
def process_perf(self, node):
    c_node = node.change_ext_src('.c')

    if not c_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(c_node)
        self.create_task('Perf2c', node, c_node, cwd=self.bld.srcnode)

    self.source.extend([c_node])


# }}}
# {{{ LEX

class Lex2c(Task):
    run_str = ['rm -f ${TGT}', '${FLEX_SH} ${SRC} ${TGT}']
    color   = 'BLUE'

    @classmethod
    def keyword(cls):
        return 'Generating'


@extension('.l')
def process_lex(self, node):
    c_node = node.change_ext_src('.c')

    if not c_node in self.env.GEN_FILES:
        self.env.GEN_FILES.add(c_node)
        self.create_task('Lex2c', node, c_node, cwd=self.bld.srcnode)

    self.source.extend([c_node])


# }}}
# {{{ FC


class FirstInputStrTask(Task):

    def __str__(self):
        node = self.inputs[0]
        return node.path_from(node.ctx.launch_node())


class Fc2c(FirstInputStrTask):
    run_str = ['rm -f ${TGT}', '${FARCHC} -c -o ${TGT} ${SRC[0].abspath()}']
    color   = 'BLUE'
    before  = ['Blk2c', 'Blkk2cc', 'ClangCheck']
    ext_out = ['.h']

    @classmethod
    def keyword(cls):
        return 'Generating'

    def scan(self):
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


@extension('.fc')
def process_fc(self, node):
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


class Tokens2c(Task):
    run_str = ('${TOKENS_SH} ${SRC[0].abspath()} ${TGT[0]} && ' +
               '${TOKENS_SH} ${SRC[0].abspath()} ${TGT[1]}')
    color   = 'BLUE'
    before  = ['Blk2c', 'Blkk2cc', 'ClangCheck']
    ext_out = ['.h', '.c']

    @classmethod
    def keyword(cls):
        return 'Generating'


@extension('.tokens')
def process_tokens(self, node):
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

    def __init__(self, ctx, path=None, class_range=None, includes=None,
                 json_path=None, ts_path=None):
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
        self.computed_includes = None

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
    def languages(self):
        """ Get the list of languages for iopc """
        res = 'c'
        if self.json_node:
            res += ',json'
        if self.ts_node:
            res += ',typescript'
        return res

    @property
    def class_range_option(self):
        """ Get the class-id range option for iopc """
        if self.class_range:
            return '--class-id-range={0}'.format(self.class_range)
        else:
            return ''

    @property
    def json_output_option(self):
        """ Get the json-output-path option for iopc """
        if self.json_node:
            return '--json-output-path={0}'.format(self.json_node)
        else:
            return ''

    @property
    def ts_output_option(self):
        """ Get the typescript-output-path option for iopc """
        if self.ts_node:
            return '--typescript-output-path={0}'.format(self.ts_node)
        else:
            return ''

    def get_includes_recursive(self, includes, seen_opts):
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
    def includes_option(self):
        """ Get the -I option for iopc """
        if self.computed_includes is None:
            includes = set()
            seen_opts = set()
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
    def keyword(cls):
        return 'Generating'

    def get_cwd(self):
        return self.inputs[0].parent

    def scan(self):
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

    def run(self):
        cmd = ('{iopc} --Wextra --language {languages} '
               '--c-resolve-includes --typescript-enable-backbone '
               '{includes} {class_range} {json_output} {ts_output} {source}')
        cmd = cmd.format(iopc=self.inputs[1].abspath(),
                         languages=self.env.IOP_LANGUAGES,
                         includes=self.env.IOP_INCLUDES,
                         class_range=self.env.IOP_CLASS_RANGE,
                         json_output=self.env.IOP_JSON_OUTPUT,
                         ts_output=self.env.IOP_TS_OUTPUT,
                         source=self.inputs[0].abspath())
        res = self.exec_command(cmd, cwd=self.get_cwd())
        if res and not getattr(self, 'scan_failed', False):
            self.bld.fatal("scan should have failed for %s" % self.inputs[0])
        return res


RE_IOP_PACKAGE = re.compile(r'^package (.*);$', re.MULTILINE)

def iop_get_package_path(self, node):
    """ Get the 'package path' of a IOP file.
        It opens the IOP file, parses the 'package' line, and returns a string
        where dots are replaced by slashes.
    """
    match = RE_IOP_PACKAGE.search(node.read())
    if match is None:
        self.bld.fatal('no package declaration found in %s' % node)
    return match.group(1).replace('.', '/')


@extension('.iop')
def process_iop(self, node):
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


@extension('.ld')
def process_ld(self, node):
    self.env.append_value('LDFLAGS',
                          ['-Xlinker', '--version-script',
                           '-Xlinker', node.abspath()])


# }}}
# {{{ PXC


class Pxc2Pxd(FirstInputStrTask):
    run_str = '${PXCC} ${CPPPATH_ST:INCPATHS} ${SRC[0].abspath()} -o ${TGT}'
    color   = 'BLUE'
    before  = 'cython'
    after   = 'Iop2c'
    scan    = c_preproc.scan # pxc files are C-like files

    @classmethod
    def keyword(cls):
        return 'Pxcc'


@extension('.pxc')
def process_pxcc(self, node):
    ctx = self.bld

    # Ensure pxcc tgen is posted
    if not getattr(ctx.pxcc_tgen, 'posted', False):
        ctx.pxcc_tgen.post()
    if not hasattr(ctx, 'pxcc_task'):
        ctx.pxcc_task = ctx.pxcc_tgen.link_task
        ctx.env.PXCC = ctx.pxcc_tgen.link_task.outputs[0].abspath()

    # Handle file
    pxd_node = node.change_ext_src('.pxd')

    if pxd_node not in self.env.GEN_FILES:
        self.env.GEN_FILES.add(pxd_node)
        inputs = [node, ctx.pxcc_tgen.link_task.outputs[0]]
        pxc_task = self.create_task('Pxc2Pxd', inputs, [pxd_node],
                                    cwd=self.env.PROJECT_ROOT)
        pxc_task.set_run_after(ctx.pxcc_task)


# }}}
# {{{ .c checks using clang


class ClangCheck(Task):
    run_str = ('${CLANG} -x c -O0 -fsyntax-only ${CLANG_FLAGS} '
               '${CLANG_CFLAGS} ${CLANG_EXTRA_CFLAGS} ${CPPPATH_ST:INCPATHS} '
               '${SRC} -o /dev/null')
    color = 'BLUE'

    @classmethod
    def keyword(cls):
        return 'Checking'


@TaskGen.feature('c')
@TaskGen.after_method('propagate_uselib_vars')
def update_clang_check_envs(self):
    if self.clang_check_tasks:
        # Compute clang extra cflags from gcc flags
        extra_flags = compute_clang_extra_cflags(self, self.env.CLANG_FLAGS,
                                                 'CFLAGS')
        for task in self.clang_check_tasks:
            task.env.CLANG_EXTRA_CFLAGS = extra_flags


@extension('.c')
def process_c_for_check(self, node):
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

def options(ctx):
    # Load C/C++ compilers
    ctx.load('compiler_c')
    ctx.load('compiler_cxx')

    # Python/cython
    ctx.load('python')
    ctx.load('cython_intersec')

# }}}
# {{{ configure
# {{{ compilation profiles


def get_cflags(ctx, args):
    # TODO waf: maybe rewrite it in full-python after getting rid of make
    flags = ctx.cmd_and_log(ctx.env.CFLAGS_SH + args)
    return flags.strip().replace('"', '').split(' ')


def profile_default(ctx,
                    no_assert=False,
                    allow_no_compress=True,
                    allow_no_double_fpic=True,
                    allow_fake_versions=True,
                    use_sanitizer=False,
                    optim_level=2,
                    fortify_source='-D_FORTIFY_SOURCE=2'):

    # Load C/C++ compilers
    ctx.load('compiler_c')
    ctx.load('compiler_cxx')

    # Get compilation flags with cflags.sh
    ctx.find_program('cflags.sh', var='CFLAGS_SH',
                     path_list=[os.path.join(ctx.path.abspath(), 'build')])

    ctx.env.CFLAGS = get_cflags(ctx, [ctx.env.COMPILER_CC])

    oflags = ['-O' + str(optim_level)]
    ctx.env.CFLAGS += oflags
    ctx.env.CXXFLAGS += oflags

    ctx.env.CFLAGS += [
        '-g',
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

    ctx.env.CXXFLAGS = get_cflags(ctx, [ctx.env.COMPILER_CXX])
    ctx.env.CXXFLAGS += [
        '-g',
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
        ctx.env.CLANG = ctx.find_program('clang')
        ctx.env.CLANG_FLAGS = get_cflags(ctx, ['clang'])
        ctx.env.CLANG_FLAGS += oflags
        ctx.env.CLANG_REWRITE_FLAGS = get_cflags(ctx, ['clang', 'rewrite'])
        ctx.env.CLANG_REWRITE_FLAGS += oflags

    if ctx.env.COMPILER_CXX == 'clang++':
        # C++ compilation directly done using clang
        ctx.env.CXXFLAGS += ['-x', 'c++']
        ctx.env.CLANGXX_FLAGS = ctx.env.CXXFLAGS
    else:
        # Probably compiling with g++; we'll need the .blkk -> .cc rewriting
        # pass with our modified clang
        ctx.env.CLANGXX = ctx.find_program('clang++')
        ctx.env.CLANGXX_FLAGS = get_cflags(ctx, ['clang++'])
        ctx.env.CLANGXX_FLAGS += oflags
        ctx.env.CLANGXX_REWRITE_FLAGS = get_cflags(ctx,
                                                   ['clang++', 'rewrite'])
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

    # Compression
    if allow_no_compress and ctx.get_env_bool('NOCOMPRESS'):
        ctx.env.DO_COMPRESS = False
        log = 'no'
    else:
        ctx.env.DO_COMPRESS = True
        log = 'yes'

        ld_help = ctx.cmd_and_log('ld --help')
        if 'compress-debug-sections' in ld_help:
            ctx.env.LDFLAGS += ['-Xlinker', '--compress-debug-sections=zlib']
        else:
            Logs.warn('Compression requested but ld do not support it')

        objcopy_help = ctx.cmd_and_log('objcopy --help')
        if 'compress-debug-sections' in objcopy_help:
            ctx.env.DO_OBJCOPY_COMPRESS = True
        else:
            Logs.warn('Compression requested but objcopy do not support it')

    ctx.msg('Do compression', log)

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


def profile_debug(ctx, allow_no_double_fpic=True, use_sanitizer=False):
    profile_default(ctx, fortify_source=None,
                    allow_no_double_fpic=allow_no_double_fpic,
                    use_sanitizer=use_sanitizer, optim_level=0)

    cflags = [
        '-Wno-uninitialized', '-fno-inline', '-fno-inline-functions', '-g3',
        '-gdwarf-2',
    ]
    ctx.env.CFLAGS += cflags
    ctx.env.CXXFLAGS += cflags


def profile_release(ctx):
    profile_default(ctx, no_assert=True,
                    allow_no_compress=False,
                    allow_no_double_fpic=False,
                    allow_fake_versions=False)
    ctx.env.LINKFLAGS += ['-Wl,-x', '-rdynamic']
    ctx.env.WEBPACK_MODE = 'production'


def profile_asan(ctx):
    Options.options.check_c_compiler = 'clang'
    Options.options.check_cxx_compiler = 'clang++'

    profile_debug(ctx, allow_no_double_fpic=False, use_sanitizer=True)

    ctx.env.LDFLAGS += ['-lstdc++']

    asan_flags = ['-fsanitize=address']
    ctx.env.SANITIZER_CFLAGS = asan_flags
    ctx.env.SANITIZER_CXXFLAGS = asan_flags
    ctx.env.SANITIZER_LDFLAGS = asan_flags


def profile_tsan(ctx):
    Options.options.check_c_compiler = 'clang'
    Options.options.check_cxx_compiler = 'clang++'

    profile_debug(ctx, allow_no_double_fpic=False, use_sanitizer=True)

    tsan_flags = ['-fsanitize=thread']
    ctx.env.SANITIZER_CFLAGS = tsan_flags
    ctx.env.SANITIZER_CXXFLAGS = tsan_flags
    ctx.env.SANITIZER_LDFLAGS = tsan_flags


def profile_mem_bench(ctx):
    profile_default(ctx, no_assert=True, fortify_source=None)

    flags = ['-DMEM_BENCH']
    ctx.env.CFLAGS += flags
    ctx.env.CXXFLAGS += flags


def profile_coverage(ctx):
    profile_debug(ctx)
    ctx.find_program('lcov')
    ctx.find_program('genhtml')

    flags = ['-pg', '--coverage']
    ctx.env.CFLAGS += flags
    ctx.env.CXXFLAGS += flags
    ctx.env.LDFLAGS += flags

    do_coverage_start(ctx)
    ctx.msg('Starting coverage session', 'ok')


PROFILES = {
    'default':   profile_default,
    'debug':     profile_debug,
    'release':   profile_release,
    'asan':      profile_asan,
    'tsan':      profile_tsan,
    'mem-bench': profile_mem_bench,
    'coverage':  profile_coverage,
}

# }}}

def configure(ctx):
    # register_global_includes
    ConfigurationContext.register_global_includes = register_global_includes

    # Load compilation profile
    ctx.env.PROFILE = os.environ.get('P', 'default')
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


class IsConfigurationContext(ConfigurationContext):

    def execute(self):
        # Run configure
        ConfigurationContext.execute(self)

        # Ensure local vimrc and syntastic configuration files are generated
        # after the end of the configure step
        gen_local_vimrc(self)
        gen_syntastic(self)


# }}}
# {{{ build

def build(ctx):
    Logs.info('Waf: Selected profile: %s', ctx.env.PROFILE)

    ctx.env.PROJECT_ROOT = ctx.srcnode
    ctx.env.GEN_FILES = set()
    ctx.env.CHECKED_FILES = set()

    if ctx.env.DO_OBJCOPY_COMPRESS:
        patch_c_tasks_for_compression(ctx)

    register_get_cwd()

    # iopc options
    ctx.IopcOptions = IopcOptions
    ctx.iopc_options = {}

    # Register pre/post functions
    if ctx.env.NDEBUG:
        ctx.add_pre_fun(filter_out_zchk)
    if ctx.env.DO_DOUBLE_FPIC:
        ctx.add_pre_fun(compile_fpic)
    if ctx.env.USE_SANITIZER:
        ctx.add_pre_fun(compile_sanitizer)
    ctx.add_pre_fun(gen_tags)
    ctx.add_pre_fun(old_gen_files_detect)
    ctx.add_pre_fun(coverage_start_cmd)
    ctx.add_pre_fun(coverage_end_cmd)

# }}}
