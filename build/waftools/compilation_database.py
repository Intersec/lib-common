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

Task.Task.keep_last_cmd = True


class CompileDbContext(Build.BuildContext):
    '''generates compile_commands.json by request'''

    cmd='compiledb'

    def write_one_compilation_db(self, db_file, tasks):
        database_file=self.srcnode.make_node(db_file)
        Logs.info('Build commands will be stored in %s',
                  database_file.path_from(self.path))

        try:
            root = database_file.read_json()
        except IOError:
            root = []
        clang_db = dict((x['file'], x) for x in root)

        # Those arguments are not recognized by clangd. They will be removed.
        clang_args_to_filter = ['-cc1', '-internal-isystem',
                                '-internal-externc-isystem']
        for task in tasks:
            try:
                if isinstance(task.last_cmd, list):
                    cmd = []
                    for arg in task.last_cmd:
                        # Blocks handling seems to require a different option
                        # for clangd.
                        if arg == '-rewrite-blocks':
                            cmd.append('-fblocks')
                        elif arg in clang_args_to_filter:
                            continue
                        else:
                            cmd.append(arg)
                else:
                    cmd = task.last_cmd
            except AttributeError:
                continue

            f_node = task.inputs[0]
            filename = f_node.path_from(self.srcnode)
            entry = {
                "directory": task.get_cwd().abspath(),
                "arguments": cmd,
                "file": filename,
            }
            clang_db[filename] = entry

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

        self.recurse([self.run_dir])
        self.pre_build()

        # we need only to generate last_cmd, so override
        # exec_command temporarily
        def exec_command(self, *k, **kw):
            return 0

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

                classes = [Task.classes.get(x) for x in ('c', 'cxx', 'Blk2c',
                                                         'Blkk2cc')]
                iop2c = Task.classes.get('Iop2c')
                classes.append(iop2c)
                tup = tuple(y for y in classes if y)
                for tsk in lst:
                    if (not isinstance(tsk, tup) or
                            tsk.inputs[0] in self.env.GEN_FILES):
                        continue

                    if isinstance(tsk, iop2c):
                        self.iop_compilation_database_tasks.append(tsk)
                    else:
                        self.clang_compilation_database_tasks.append(tsk)
                    tsk.nocache = True
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
