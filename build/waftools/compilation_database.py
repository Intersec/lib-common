# pylint: disable = line-too-long
# Borrowed from waf sources: https://gitlab.com/ita1024/waf/-/blob/master/waflib/extras/clang_compilation_database.py
# Christoph Koke, 2013
# Alibek Omarov, 2019
# Nicolas Pauss, 2025

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
from waflib.Node import Node
# pylint: enable = import-error

from typing import (
    Any, Optional, Union, TYPE_CHECKING,
    # We still need to use them here because this file is imported in
    # Python 3.6 by waf before switching to Python 3.9+.
    List, Dict, Type,
)

if TYPE_CHECKING:
    # mypy wants the import to be relative ¯\_(ツ)_/¯
    from .backend import compute_clang_extra_cflags
else:
    from backend import compute_clang_extra_cflags


TASK_CLASSES: Optional[Dict[str, Type[Task.Task]]] = None


def build_task_classes_map() -> None:
    '''Build the task classes'''
    global TASK_CLASSES

    assert TASK_CLASSES is None
    TASK_CLASSES = {
        x: Task.classes.get(x) for x in
        ('Iop2c', 'c', 'cxx', 'Blk2c', 'Blkk2cc')
    }


ClangDbEntry = Dict[str, Union[str, List[str]]]
ClangDb = Dict[str, ClangDbEntry]


class CompileDbContext(Build.BuildContext): # type: ignore[misc]
    '''generates compile_commands.json by request'''

    cmd = 'compiledb'

    def filter_task_node_cmd_input(self, cmd: Union[str, List[str]],
                                   task: Task,
                                   task_node: Node) -> Union[str, List[str]]:
        if not isinstance(cmd, list):
            # This is not a list of arguments, do nothing
            assert isinstance(cmd, str)
            return cmd

        node_abs_path = task_node.abspath()
        node_rel_path = task_node.path_from(self.srcnode)
        node_paths = {node_abs_path, node_rel_path}

        # Filter out the input argument corresponding to the file path
        return [arg for arg in cmd if arg not in node_paths]

    @staticmethod
    def replace_task_node_cmd_output(
            cmd: Union[str, List[str]]
    ) -> Union[str, List[str]]:
        if not isinstance(cmd, list):
            # This is not a list of arguments, do nothing
            assert isinstance(cmd, str)
            return cmd

        new_cmd = []

        next_arg_skip = False
        for arg in cmd:
            arg_to_be_replaced = False

            if next_arg_skip:
                # The previous argument was '-o', so we should skip this
                # argument
                next_arg_skip = False
                continue

            if arg == '-o':
                # We replace skip this argument and skip the next one
                next_arg_skip = True
                arg_to_be_replaced = True
            elif arg.startswith('-o'):
                # We should replace the '-o*' argument
                arg_to_be_replaced = True

            # Replace it with '-o/dev/null'
            if arg_to_be_replaced:
                arg = '-o/dev/null'

            # Else, add the argument to the new ones
            new_cmd.append(arg)

        return new_cmd

    @staticmethod
    def get_task_node_cmd(task: Task, cmd: Union[str, List[str]],
                          f_node: Node,
                          is_dep: bool) -> Union[str, List[str]]:
        '''Get the command used for the task'''
        if not isinstance(cmd, list):
            # This is not a list of arguments, do nothing
            assert isinstance(cmd, str)
            return cmd

        assert TASK_CLASSES is not None

        # Get the file compilation type from the task class
        additional_args = []
        if isinstance(task, (TASK_CLASSES['c'], TASK_CLASSES['Blk2c'])):
            if is_dep:
                # XXX: Due to a bug with clang with '-fsyntax-only'
                # on headers, clang still complains about unused symbols even
                # when specifying the header type.
                additional_args.append('-xc-header')
                additional_args.append('-Wno-unused')
            else:
                additional_args.append('-xc')
        elif isinstance(task, (TASK_CLASSES['cxx'], TASK_CLASSES['Blkk2cc'])):
            if is_dep:
                # XXX: Due to a bug with clang with '-fsyntax-only'
                # on headers, clang still complains about unused symbols even
                # when specifying the header type.
                additional_args.append('-xcxx-header')
                additional_args.append('-Wno-unused')
            else:
                additional_args.append('-xcxx')

        # Add absolute path of the node to compile
        additional_args.append(f_node.abspath())

        return cmd + additional_args

    def add_task_nodes_db(self, clang_db: ClangDb, task: Task,
                          cmd: Union[str, List[str]],
                          f_nodes: List[Node], is_dep: bool) -> None:
        '''Add the nodes to the db'''
        for f_node in f_nodes:
            filename = f_node.path_from(self.srcnode)
            if filename in clang_db:
                # Only record the first compilation
                continue

            node_cmd = self.get_task_node_cmd(task, cmd, f_node, is_dep)
            entry: ClangDbEntry = {
                "directory": task.get_cwd().abspath(),
                "file": filename,
            }

            # Add the command as 'arguments' if the cmd is a list,
            # otherwise, use 'command', as specified in compilation database
            # specification:
            # https://clang.llvm.org/docs/JSONCompilationDatabase.html
            if isinstance(node_cmd, list):
                entry['arguments'] = node_cmd
            else:
                assert isinstance(node_cmd, str)
                entry['command'] = node_cmd

            clang_db[filename] = entry

    def write_one_compilation_db(self, db_file: str, tasks: Task) -> None:
        database_file = self.srcnode.make_node(db_file)
        Logs.info('Build commands will be stored in %s',
                  database_file.path_from(self.path))

        empty_list: List[Node] = []
        clang_db: ClangDb = {}

        for task in tasks:
            task_node = task.inputs[0]

            # First filter out the task node path from the arguments list as
            # doxygen requires absolute paths.
            # The paths will be added later on.
            cmd = self.filter_task_node_cmd_input(task.last_cmd, task,
                                                  task_node)

            # Add the task node to the db
            self.add_task_nodes_db(clang_db, task, cmd, [task_node], False)

            # Replace the output commands with '-o/dev/null' for dependencies
            cmd = self.replace_task_node_cmd_output(cmd)

            # Add the dependencies of the task
            dep_nodes = self.node_deps.get(task.uid(), empty_list)
            self.add_task_nodes_db(clang_db, task, cmd, dep_nodes, True)

        root = list(clang_db.values())
        database_file.write_json(root)

    def write_compilation_database(self) -> None:
        self.write_one_compilation_db('compile_commands.json',
                                      self.clang_compilation_database_tasks)
        self.write_one_compilation_db('iop_compile_commands.json',
                                      self.iop_compilation_database_tasks)

    def execute(self) -> None:
        """
        Build dry run
        """
        self.restore()
        self.cur_tasks: List[Task] = []
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
        def exec_command(self: Task, *k: Any, **kw: Any) -> int:
            return 0

        # Get the list of classes to filter
        build_task_classes_map()
        assert TASK_CLASSES is not None
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


def patch_execute() -> None:
    global EXECUTE_PATCHED

    if EXECUTE_PATCHED:
        return

    def new_execute_build(self: Build.BuildContext) -> None:
        """
        Invoke compiledb command before build
        """
        if self.cmd.startswith('build'):
            Scripting.run_command(self.cmd.replace('build', 'compiledb'))

        assert old_execute_build is not None
        old_execute_build(self)

    old_execute_build = getattr(Build.BuildContext, 'execute_build', None)
    setattr(Build.BuildContext, 'execute_build', new_execute_build)
    EXECUTE_PATCHED = True


patch_execute()
