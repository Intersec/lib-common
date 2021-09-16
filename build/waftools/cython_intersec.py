#! /usr/bin/env python3
# encoding: utf-8
# Thomas Nagy, 2010-2015
# Romain Le Godais, Nicolas Pauss, 2018

import re
import os.path as osp

# pylint: disable=import-error
from waflib import Task, Logs
from waflib.TaskGen import extension
from waflib.Tools import c as c_tool
from waflib.Node import Node
# pylint: enable = import-error


# {{{ .pyx extension handler


@extension('.pyx')
def add_cython_file(self, node):
    """
    Process a *.pyx* file given in the list of source files. No additional
    feature is required::

        def build(bld):
            bld(features='c cshlib pyext', source='main.c foo.pyx',
                target='app')
    """
    ext = '.c'
    if 'cxx' in self.features:
        self.env.append_unique('CYTHONFLAGS', '--cplus')
        ext = '.cc'

    for x in getattr(self, 'cython_includes', []):
        # TODO re-use these nodes in "scan" below
        d = self.path.find_dir(x)
        if d:
            self.env.append_unique('CYTHONFLAGS', '-I%s' % d.abspath())

    # Create cython task
    c_node = node.change_ext_src(self.env.CYTHONSUFFIX + ext)

    if c_node not in self.env.GEN_FILES:
        self.env.GEN_FILES.add(c_node)
        self.create_task('cython', node, c_node)

    # Create C task
    self.create_compiled_task('CythonC', c_node)


# }}}
# {{{ CythonC compilation task


class CythonC(c_tool.c):

    def get_cwd(self):
        """
        Execute the compiler's commands from the project root instead of the
        project build directory.
        """
        return self.env.PROJECT_ROOT

    def scan(self):
        """
        XXX: Redefine the scan method of C class for c-cython files because
        the original c preprocessor does not work on them.
        Cf. https://gitlab.com/ita1024/waf/issues/2208 for the details.
        """

        # Save task environment
        self.env.stash()

        # Add needed defines so that preprocessor works
        self.env.append_unique('DEFINES', [
            'Py_PYTHON_H',
            'PY_VERSION_HEX=' + self.env.PYTHON_HEXVERSION,
        ])

        # Call original preprocessor
        res = super(CythonC, self).scan()

        # Restore previous environment (in order to not launch the build with
        # the unwanted defines)
        self.env.revert()

        return res

    def run(self):
        """
        XXX: Redefine the run method of C class for c-cython files to add
        clags to ignore warnings that need to be ignored in the c-cython
        files.
        """

        # Save task environment
        self.env.stash()

        # Add needed cflags to ignore warnings
        self.env.append_unique('CFLAGS', [
            '-Wno-unused-function',
            '-Wno-unused-parameter',
            '-Wno-shadow',
            '-Wno-redundant-decls',
            '-Wno-uninitialized',
            '-Wno-declaration-after-statement',
        ])

        # Call original run method
        res = super(CythonC, self).run()

        # Restore previous environment
        self.env.revert()

        return res


# }}}
# {{{ Cython (pyx -> c) task


CY_API_PAT = re.compile(r'\s*?cdef\s*?(public|api)\w*')
RE_INC_DIRS = re.compile(r'-I(.*)')
RE_IMPORT_CYT = re.compile(r"""
    ^\s*                           # may begin with some whitespace characters
    (?:from\s+([\w.]+)*\s+)?       # optionally match "from foo(.baz)*" and
                                   # capture foo
    c?import\s(\w+|[*])            # require "import bar" and capture bar
    """, re.M | re.VERBOSE)
RE_INCLUDE_CYT = re.compile(r"""
    ^\s*                           # may begin with some whitespace characters
    include\s+[\"'](.+)[\"']       # capture include path
    """, re.M | re.VERBOSE)


class cython(Task.Task): # pylint: disable=invalid-name
    class ScannerState:
        """Class to hold the state on scan method."""
        __slots__ = ('inc_dirs', 'nodes', 'found', 'missing', 'has_api',
                     'has_public')

        def __init__(self):
            self.inc_dirs = []
            self.nodes = set()
            self.found = []
            self.missing = []
            self.has_api = False
            self.has_public = False


    run_str = '${CYTHON} ${CYTHONFLAGS} -o ${TGT[0].abspath()} ${SRC}'
    color   = 'GREEN'

    vars    = ['INCLUDES']
    """
    Rebuild whenever the INCLUDES change. The variables such as CYTHONFLAGS
    will be appended by the metaclass.
    """

    ext_out = ['.h']
    """
    The creation of a .h file is known only after the build has begun, so it
    is not possible to compute a build order just by looking at the task
    inputs/outputs.
    """

    def runnable_status(self):
        """
        Perform a double-check to add the headers created by Cython
        to the output nodes. The scanner is executed only when the Cython task
        must be executed (optimization).
        """
        ret = super(cython, self).runnable_status()
        if ret == Task.ASK_LATER:
            return ret
        for x in self.generator.bld.raw_deps[self.uid()]:
            if x.startswith('header:'):
                self.outputs.append(self.inputs[0].parent.find_or_declare(
                    x.replace('header:', '')))
        return super(cython, self).runnable_status()

    def post_run(self):
        for x in self.outputs:
            if x.name.endswith('.h'):
                if not x.exists():
                    if Logs.verbose:
                        Logs.warn('Expected %r', x.abspath())
                    x.write('')
        return Task.Task.post_run(self)

    def scan(self):
        """
        Return the dependent files (.pxd, .pxi) by looking in the include
        folders.
        Put the headers to generate in the custom list "bld.raw_deps".
        To inspect the scanner results use::

            $ waf clean build --zones=deps
        """
        node = self.inputs[0]
        state = self.ScannerState()

        # build list of inc_dirs
        self.scan_parse_inc_dirs(state)

        # scan pyx node
        self.scan_node(state, node)

        # the cython file implicitly depends on a pxd file that might be
        # present
        implicit = node.parent.find_resource(node.name[:-3] + 'pxd')
        if implicit:
            state.found.append(implicit)

        # Now the .h created - store them in bld.raw_deps for later use
        name = node.name.replace('.pyx', '')
        cython_suffix = self.env.CYTHONSUFFIX
        if state.has_api:
            state.missing.append('header:%s_api%s.h' % (name, cython_suffix))
        if state.has_public:
            state.missing.append('header:%s%s.h' % (name, cython_suffix))

        Logs.debug('cython: found %r, missing %r', state.found, state.missing)

        return (state.found, state.missing)

    def scan_parse_inc_dirs(self, state):
        """
        Populate the include directories in the scan state
        """
        path_find = self.generator.path.find_node
        root_find = self.generator.bld.root.find_node

        cython_flags = getattr(self.env, 'CYTHONFLAGS', [])
        it = iter(cython_flags)
        for flag in it:
            m = RE_INC_DIRS.match(flag)
            if not m:
                continue

            inc_dir = m.group(1)
            if not inc_dir:
                # If the include directory is not attached to -I, look for the
                # next flag
                inc_dir = next(it, None)
                if inc_dir is None:
                    # End of iterator
                    break

            if isinstance(inc_dir, Node):
                node = inc_dir
            elif osp.isabs(inc_dir):
                node = root_find(inc_dir)
            else:
                node = path_find(inc_dir)

            if node:
                state.inc_dirs.append(node)

    def scan_node(self, state, node):
        """
        Scan the dependencies of the file located at the given node
        recursively.
        """

        state.nodes.add(node)
        txt = node.read()

        inc_dirs = state.inc_dirs[:]
        inc_dirs.append(node.parent)

        # first with the inc_dirs
        includes = set()
        for m in RE_INCLUDE_CYT.finditer(txt):
            includes.add(m.group(1))
        Logs.debug('cython: includes %r', includes)

        for include in sorted(includes):
            self.scan_dependency(state, inc_dirs, include, include, True)

        # second with the import
        mods = set()
        for m in RE_IMPORT_CYT.finditer(txt):
            if m.group(1):  # matches "from foo import bar"
                mods.add(m.group(1))
            else:
                mods.add(m.group(2))
        Logs.debug('cython: mods %r', mods)

        for mod in sorted(mods):
            mod_file_name = mod.replace('.', '/') + '.pxd'
            self.scan_dependency(state, inc_dirs, mod, mod_file_name, False)

        # Check if some functions are exported
        for l in txt.splitlines():
            if CY_API_PAT.match(l):
                if ' api ' in l:
                    state.has_api = True
                if ' public ' in l:
                    state.has_public = True

    def scan_dependency(self, state, inc_dirs, dep_orig_name,
                        dep_file_name, can_be_abs):
        """
        Look for a dependency in all includes directories and scan it
        recursively if found.
        """
        if can_be_abs and dep_file_name.startswith('/'):
            # When dependency is a file with absolute path, create dep_node
            # from the root directory only
            inc_dirs = [self.generator.bld.root]

        for inc_dir in inc_dirs:
            dep_node = inc_dir.find_resource(dep_file_name)
            if dep_node:
                if dep_node in state.nodes:
                    # Skip already processed node
                    break
                state.found.append(dep_node)
                self.scan_node(state, dep_node)
                break
        else:
            state.missing.append(dep_orig_name)

# }}}


def options(ctx):
    ctx.add_option('--cython-flags', action='store', default='',
                   help='space separated list of flags to pass to cython')
    ctx.add_option('--cython-suffix', action='store', default='',
                   help='add a suffix to cython generated files')


def configure(ctx):
    if not ctx.env.CC and not ctx.env.CXX:
        ctx.fatal('Load a C/C++ compiler first')
    if not ctx.env.PYTHON:
        ctx.fatal('Load the python tool first!')
    ctx.find_program('cython', var='CYTHON')
    if hasattr(ctx.options, 'cython_flags'):
        ctx.env.CYTHONFLAGS = ctx.options.cython_flags
    ctx.env.CYTHONSUFFIX = ctx.options.cython_suffix
    ctx.env.PYTHON_HEXVERSION = ctx.cmd_and_log(ctx.env.PYTHON + [
        '-c', 'import sys; print(hex(sys.hexversion))'
    ]).strip()
