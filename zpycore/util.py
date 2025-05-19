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
import signal
import sys
import tempfile
import threading
from collections.abc import Iterator
from contextlib import contextmanager
from typing import Any

import psutil

# L4D {{{

EVT   = threading.Event()
REARM = True
THR   = None

def log(msg: str) -> None:
    sys.stderr.write(f'{__file__}: {msg}\n')
    sys.stderr.flush()

def wipe_children(reason: str, wait_thr: bool = True) -> None:
    global THR, REARM

    if wait_thr and THR is not None:
        REARM = False
        EVT.set()
        THR.join(2)
    THR = None

    # First we check if we have some pending children
    try:
        parent = psutil.Process()
    except psutil.NoSuchProcess:
        return

    children = parent.children(recursive=True)
    if not children:
        return

    log(f'`{reason}` wipe pending children...')

    # Then we try to kill all processes
    for process in children:
        try:
            process.terminate()
        except psutil.NoSuchProcess:
            continue

    # Now we wait for our children to terminate
    _, alive = psutil.wait_procs(children, timeout=3)

    # Finish the job...
    for process in alive:
        process.kill()

    # And commit suicide...
    parent.terminate()
    _, alive = psutil.wait_procs([parent], timeout=3)
    for process in alive:
        process.kill()


def wipe_children_sig(sig: int, frame: Any) -> None:
    wipe_children('received signal %d' % sig)


def wipe_background_thread() -> None:
    global REARM

    if os.getenv('Z_MODE', '').find('fast') >= 0:
        timeout = 600
    if os.getenv('Z_MODE', '').find('bench') >= 0:
        timeout = 3600 * 24 * 7
    else:
        timeout = 1200

    while REARM:
        EVT.clear()
        REARM = False
        EVT.wait(timeout)
        if not EVT.is_set():
            reason = 'inactive for %d seconds' % timeout
            wipe_children(reason, wait_thr=False)

def wipe_children_rearm() -> None:
    global REARM

    REARM = True
    EVT.set()

@contextmanager
def wipe_children_register() -> Iterator[None]:
    global THR

    # Don't hang child processes on SIGTTOU when changing
    # the process group
    signal.signal(signal.SIGTTOU, signal.SIG_IGN)

    try:
        os.setpgid(0, 0)
    except OSError:
        pass
    signal.signal(signal.SIGTERM, wipe_children_sig)
    signal.signal(signal.SIGINT,  wipe_children_sig)
    signal.signal(signal.SIGQUIT, wipe_children_sig)
    THR = threading.Thread(target=wipe_background_thread)

    try:
        THR.start()
        yield
    finally:
        wipe_children('atexit')

# }}}
# Sandbox {{{

def mkdtemp(ns: str) -> str:
    def do(path: str) -> str:
        prefix = 'zpy.%s.%d.XXXXXX' % (ns, os.getpid())
        return tempfile.mkdtemp(dir=path, prefix=prefix)

    z_dir = os.getenv('Z_DIR', None)
    if z_dir:
        try:
            return do(z_dir)
        except OSError:
            pass
    try:
        return do(os.getcwd())
    except OSError:
        pass
    return do('/tmp')

# }}}
