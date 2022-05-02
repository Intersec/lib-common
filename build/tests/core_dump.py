#!/usr/bin/env python3
# encoding: utf-8

#vim:set fileencoding=utf-8
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

"""
core_dump.py is an utility to simplify backtrace generation (from autotest
or from command line).

In automatic mode
 - Retrieve list of coredumps
 $ core_dump.py list
 /srv/data/core/core_db,/srv/data/core/core_igloo,[...]
 - Launch test
 $ behave ci/features/ag.feature

 - Display backtrace of new coredumps
 $ core_dump.py --root ~/dev/mmsx --ignore                 \
    /srv/data/core/core_db,/srv/data/core/core_igloo,[...] \
    diff
 ...


In manual mode
 - core_dump.py --core /var/log/iglo.core show
...
"""


import os
from os import path as osp
import sys
import platform
from subprocess import check_output
import re
from tempfile import NamedTemporaryFile
from glob import glob
import argparse
import shutil

CORE_PATTERN = "/proc/sys/kernel/core_pattern"
DEBUG = os.getenv('CORE_DEBUG', None)

# general stuff
GDB_CMD = [
    'print "bt"',
    'bt',
]
# used to find Point of Interest (our code)
GDB_CMD_POI = [
    'bt'
]
# GDB command to display info on intersec frame
GDB_CMD_FRAME = [
    'print "list"',
    'list',
    'print "info local"',
    'info local',
    'print "info args"',
    'info args'
]
BINARY_EXT='-binary'


def find_exe(name, root):
    ret = set()
    for dirpath, _, filenames in os.walk(root):
        if name in filenames:
            ret.add(osp.realpath("{0}/{1}".format(dirpath, name)))
    if len(ret) != 1:
        debug("binary %s not found or more than once" % name)
        return None
    return ret.pop()

def debug(*args):
    if DEBUG:
        print(" ".join(args), file=sys.stderr)

REG = re.compile(r'^#(\d+) .* at (.*)$')
def get_intersec_poi(output, root):
    for line in output.split('\n'):
        reg = REG.match(line)
        if not reg:
            continue
        debug(reg.group(1), '=', reg.group(2))
        fname = reg.group(2).split(':')[0]
        if os.path.isfile('%s/%s' % (root, fname)):
            return reg.group(1)
    return None

class Cores:
    def __init__(self, rootpath='.'):
        self.cores = []
        self.rootpath = rootpath
        self.core_filter = None
        self.core_path = None

        with open(CORE_PATTERN, "r") as fpr:
            pattern = fpr.read().strip('\n')

        # needed for buildbot because coredump are stored in shared directory
        self.core_filter = platform.node() + '.' if '%h' in pattern else None
        self.core_path = os.path.dirname(pattern)
        self.core_regex = None

        self.init_regex(pattern)

        debug("CORE_PATH = ", self.core_path)
        debug("CORE_FILTER = ", self.core_filter)

    def init_regex(self, pattern):
        # we replace core template with proper regex pattern
        r = {'%h': platform.node(),
             '%t': '[0-9]{8,16}',
             '%p': r'\d+',
             '%e': '(?P<exe>[A-Za-z-_]*)',
            }
        for k, v in r.items():
            pattern = pattern.replace(k, v)

        if '%' in pattern:
            debug('Update %s or this script to manage template %s' % (
                CORE_PATTERN, pattern))
            return

        debug("CORE_REGEX = ", pattern)
        self.core_regex = re.compile(pattern)

    def refresh(self):
        self.cores = self._glob()

    def set(self, cores):
        if cores is None:
            self.cores = []
            return
        if cores.startswith('@'):
            with open(cores[1:], 'r') as f_:
                cores = f_.read().strip()
        self.cores = [c for c in cores.split(',') if c]

    def _glob(self):
        cores = glob(self.core_path + "/*")
        cores = [c for c in cores if os.path.isfile(c) and
                 c.endswith(BINARY_EXT) is False]
        if self.core_filter is not None:
            cores = [c for c in cores if self.core_filter in c]
        debug("cores found : ", cores)
        return cores

    @staticmethod
    def _gdb_cmd(cmd, fullpath, core):
        # prepare CMD
        gdb_cmd = NamedTemporaryFile(delete=False)
        gdb_cmd.write('\n'.join(cmd).encode('utf-8'))
        gdb_cmd.close()

        # launch gdb
        cmd = ['gdb',
               '-batch',
               '-x', gdb_cmd.name,
               '--core', core]
        if fullpath:
            cmd += ['-cd=' + osp.dirname(fullpath), fullpath]

        debug('running ', ' '.join(cmd))
        with open("/dev/null", "w") as f_:
            stdout = check_output(cmd, stderr=f_)
        os.unlink(gdb_cmd.name)
        return stdout.decode('utf-8')

    def find_binary_fullpath(self, core):
        fullpath = None
        exe = None

        # first check, we check output of gdb --core /my/path/core
        # to retrieve
        # "Core was generated by `c/statd -n platform.statd.Statd'."
        tmp = self._gdb_cmd([""], None, core)
        ptn = re.compile(r"^Core.*by `([^\s]+).*'.")
        for line in tmp.split('\n'):
            reg = ptn.match(line)
            if not reg:
                continue
            fullpath = reg.group(1)
            exe = fullpath.split('/')[-1]
            break

        # fallback to recursive search of binary
        if not exe or not osp.isfile(fullpath):
            if not self.core_regex:
                debug('Cores are not handled')
                return None

            res = self.core_regex.match(core)
            if not res:
                print('Failed to execute core-pattern', file=sys.stderr)
                return None
            exe = res.group('exe')
            # find executable from ROOT_PATH
            fullpath = find_exe(exe, self.rootpath)

        if fullpath is None:
            debug('Failed to found ', exe)
            return None

        debug('executable : ', exe)
        debug('fullpath : ', fullpath)

        return fullpath

    def backtrace(self, core, exe=None):
        debug('Run stuff on ', core)
        fullpath = exe if exe else self.find_binary_fullpath(core)

        # found intersec function involved in crash
        tmp = self._gdb_cmd(GDB_CMD_POI, fullpath, core)
        frame = get_intersec_poi(tmp, self.rootpath)

        cmd = list(GDB_CMD)
        if frame is None:
            debug("Interesting frame not found")
        else:
            debug("Found frame ", frame)
            cmd += ["frame %s" % frame] + GDB_CMD_FRAME

        stdout = "Processing %s" % core
        stdout += self._gdb_cmd(cmd, fullpath, core)

        # FIXME: split GDB output to be more human friendly
        return stdout

    def parse(self, frmt="text"):
        new_list = list(set(self.cores)^set(self._glob()))
        if len(new_list) == 0:
            return

        print("Cores detected during run : ", new_list)
        for core in new_list:
            if not os.path.isfile(core):
                print("core", core, "has been deleted.", file=sys.stderr)
                continue

            exe = self.find_binary_fullpath(core)
            if exe is None:
                continue
            self.show_backtrace(core, exe, frmt)
            shutil.copyfile(exe, "{0}{1}".format(core, BINARY_EXT))

    def show_backtrace(self, core, exe=None, frmt="text"):
        core = osp.realpath(core)
        out = self.backtrace(core, exe)
        if out is None:
            return

        if frmt == "z":
            # Z format (check lib-common/z.txt)
            out = ":" + out
            print("\n:".join(out.split('\n')))
        else:
            print(out)

    def __str__(self):
        return ','.join(self.cores)

    def __repr__(self):
        return 'Cores(rootpath="%s", cores="%s")' % (self.rootpath,
                                                     ','.join(self.cores))

def options(args):
    op = argparse.ArgumentParser()
    op.add_argument('-r', '--root', action='store', default='.',
                    dest='rootpath', help='Root path to find executable')
    op.add_argument('-f', '--format', action='store', choices=['text', 'z'],
                    default='text', help='Output format')
    op.add_argument('-i', '--ignore', action='store', default=None,
                    help='Coredumps to ignore, prefix it with @ to load from'
                    ' file')
    op.add_argument('-c', '--core', help='Full path of core to inspect')

    subparsers = op.add_subparsers(dest='action')
    subparsers.add_parser('list', help='Show list')
    subparsers.add_parser('diff', help=
                          'Show backtrace of all new detected coredump')
    subparsers.add_parser('show', help=
                          'Show backtrace of specifieds coredump')
    return op.parse_args(args)

def main(args):
    opts = options(args)

    cores = Cores(rootpath=opts.rootpath)
    if opts.action == 'list':
        cores.refresh()
        print(cores)

    elif opts.action == 'diff':
        cores.set(opts.ignore)
        cores.parse(frmt=opts.format)

    elif opts.action == 'show':
        cores.show_backtrace(opts.core, frmt=opts.format)

if __name__ == "__main__":
    if platform.system() == 'Linux':
        main(sys.argv[1:])
