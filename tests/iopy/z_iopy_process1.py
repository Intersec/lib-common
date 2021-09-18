#!/usr/bin/env python3
#vim:set fileencoding=utf-8:
###########################################################################
#                                                                         #
# Copyright 2021 INTERSEC SA                                              #
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

# XXX: python code for a subprocess called from z-iopy.py
# this is to test the listen blocking server mode
# this file contains the client part that connect to the server
# and call the rpc unblocking the server
# arguments:
#    full path name of the iop plugin
#    uri to connect to

import sys
import warnings
import time
import iopy

plugin_file = sys.argv[1]
uri         = sys.argv[2]

p = iopy.Plugin(plugin_file)
r = p.register()

connected = False
t0 = time.time()
while not connected and time.time() - t0 < 30:
    try:
        c = r.connect(uri)
        connected = True
    except iopy.Error as e:
        if str(e) != 'unable to connect to {0}'.format(uri):
            raise e

if not connected:
    sys.exit(100)

# XXX: the server will disconnect inside its RPC impl
warnings.filterwarnings("ignore", category=iopy.Warning,
                        message='.*lost connection.*')

res = c.test_ModuleA.interfaceA.funA(a=r.test.ClassB(field1=1),
                                     _login='root', _password='1234')

c.disconnect()

warnings.resetwarnings()

if res.res != 1:
    sys.exit(101)
sys.exit(0)
