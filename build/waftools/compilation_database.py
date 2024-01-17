# pylint: disable = line-too-long
# Borrowed from waf sources: https://gitlab.com/ita1024/waf/-/blob/master/waflib/extras/clang_compilation_database.py
# Christoph Koke, 2013
# Alibek Omarov, 2019

"""
Writes the c and cpp compile commands into build/compile_commands.json
see http://clang.llvm.org/docs/JSONCompilationDatabase.html

Usage:

    Load this tool in `options` to be able to generate database
    by request in command-line and before build:

    $ waf compiledb

    def options(opt):
        opt.load('compilation_database')

    Otherwise, load only in `configure` to generate it always before build.

    def configure(conf):
        conf.load('compiler_cxx')
        ...
        conf.load('compilation_database')
"""

# pylint: disable = import-error
from waflib import Logs, Task, Build, Scripting

from backend import compute_clang_extra_cflags


TASK_CLASSES = None


def build_task_classes_map():
    '''Build the task classes'''
    global TASK_CLASSES

    assert TASK_CLASSES is None
    TASK_CLASSES = {
        x: Task.classes.get(x) for x in
        ('Iop2c', 'c', 'cxx', 'Blk2c', 'Blkk2cc')
    }


class CompileDbContext(Build.BuildContext):
    '''generates compile_commands.json by request'''

    cmd = 'compiledb'

    @staticmethod
    def get_task_cmd(task, is_dep):
        '''Get the command used for the task'''
        cmd = task.last_cmd

        if not isinstance(cmd, list):
            # Add it to list command arguments
            return cmd

        # Get the file compilation type from the task class
        additional_args = None
        if isinstance(task, (TASK_CLASSES['c'], TASK_CLASSES['Blk2c'])):
            if is_dep:
                # XXX: Due to a bug with clang with '-fsyntax-only'
                # on headers, clang still complains about unused symbols even
                # when specifying the header type.
                additional_args = ['-xc-header', '-Wno-unused']
            else:
                additional_args = ['-xc']
        elif isinstance(task, (TASK_CLASSES['cxx'], TASK_CLASSES['Blkk2cc'])):
            if is_dep:
                # XXX: Due to a bug with clang with '-fsyntax-only'
                # on headers, clang still complains about unused symbols even
                # when specifying the header type.
                additional_args = ['-xcxx-header', '-Wno-unused']
            else:
                additional_args = ['-xcxx']

        if additional_args is not None:
            # Insert additional arguments.
            cmd = cmd + additional_args

        return cmd

    def add_task_nodes_db(self, clang_db, task, cmd, f_nodes):
        '''Add the nodes to the db'''
        for f_node in f_nodes:
            filename = f_node.path_from(self.srcnode)
            if filename in clang_db:
                # Only record the first compilation
                continue

            entry = {
                "directory": task.get_cwd().abspath(),
                "file": filename,
            }

            # Add the command as 'arguments' if the cmd is a list,
            # otherwise, use 'command', as specified in compilation database
            # specification:
            # https://clang.llvm.org/docs/JSONCompilationDatabase.html
            if isinstance(cmd, list):
                entry['arguments'] = cmd
            else:
                assert isinstance(cmd, str)
                entry['command'] = cmd

            clang_db[filename] = entry

    def write_one_compilation_db(self, db_file, tasks,
                                 dep_additional_args=None):
        database_file = self.srcnode.make_node(db_file)
        Logs.info('Build commands will be stored in %s',
                  database_file.path_from(self.path))

        empty_list = []
        clang_db = {}

        for task in tasks:
            # Add the task node to the db
            task_node = task.inputs[0]
            cmd = self.get_task_cmd(task, False)
            self.add_task_nodes_db(clang_db, task, cmd, [task_node])

            # Add the dependencies of the task
            dep_nodes = self.node_deps.get(task.uid(), empty_list)
            cmd = self.get_task_cmd(task, True)
            self.add_task_nodes_db(clang_db, task, cmd, dep_nodes)

        root = list(clang_db.values())
        database_file.write_json(root)

    def write_compilation_database(self):
        self.write_one_compilation_db('compile_commands.json',
                                      self.clang_compilation_database_tasks)
        self.write_one_compilation_db('iop_compile_commands.json',
                                      self.iop_compilation_database_tasks)

    def execute(self):
        """
        Build dry run
        """
        self.restore()
        self.cur_tasks = []
        self.clang_compilation_database_tasks = []
        self.iop_compilation_database_tasks = []

        if not self.all_envs:
            self.load_envs()

        # Force clang environment
        self.env.DO_CHECK = False

        if self.env.COMPILER_CC != 'clang':
            self.env.COMPILER_CC = 'clang'
            self.env.CC = self.env.CLANG
            extra_cflags = compute_clang_extra_cflags(
                self, self.env.CLANG_FLAGS, 'CFLAGS')
            include_cflags = ['-I' + include for include in
                              self.env.INCLUDES_clang]
            self.env.CFLAGS = (
                self.env.CLANG_FLAGS + extra_cflags + include_cflags
            )

        if self.env.COMPILER_CC != 'clang++':
            self.env.COMPILER_CXX = 'clang++'
            self.env.CXX = self.env.CLANGXX
            extra_cxxflags = compute_clang_extra_cflags(
                self, self.env.CLANGXX_FLAGS, 'CXXFLAGS')
            include_cxxflags = ['-I' + include for include in
                                self.env.INCLUDES_clang_cpp]
            self.env.CXXFLAGS = (
                self.env.CLANGXX_FLAGS + extra_cxxflags + include_cxxflags
            )

        self.recurse([self.run_dir])

        # we need only to generate last_cmd, so override
        # exec_command temporarily
        def exec_command(self, *k, **kw):
            return 0

        # Get the list of classes to filter
        build_task_classes_map()
        classes = tuple(TASK_CLASSES.values())
        iop2c = TASK_CLASSES['Iop2c']

        for g in self.groups:
            for tg in g:
                try:
                    f = tg.post
                except AttributeError:
                    pass
                else:
                    f()

                if isinstance(tg, Task.Task):
                    lst = [tg]
                else:
                    lst = tg.tasks

                for tsk in lst:
                    if not isinstance(tsk, classes):
                        continue

                    if isinstance(tsk, iop2c):
                        self.iop_compilation_database_tasks.append(tsk)
                    else:
                        self.clang_compilation_database_tasks.append(tsk)
                    tsk.nocache = True
                    tsk.keep_last_cmd = True
                    old_exec = tsk.exec_command
                    tsk.exec_command = exec_command
                    tsk.run()
                    tsk.exec_command = old_exec

        self.write_compilation_database()


EXECUTE_PATCHED = False


def patch_execute():
    global EXECUTE_PATCHED

    if EXECUTE_PATCHED:
        return

    def new_execute_build(self):
        """
        Invoke compiledb command before build
        """
        if self.cmd.startswith('build'):
            Scripting.run_command(self.cmd.replace('build', 'compiledb'))

        old_execute_build(self)

    old_execute_build = getattr(Build.BuildContext, 'execute_build', None)
    setattr(Build.BuildContext, 'execute_build', new_execute_build)
    EXECUTE_PATCHED = True


patch_execute()
