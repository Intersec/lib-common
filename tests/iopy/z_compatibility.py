#!/usr/bin/env python
#vim:set fileencoding=utf-8:
###########################################################################
#                                                                         #
# Copyright 2019 INTERSEC SA                                              #
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

import sys

if sys.version_info[0] < 3:
    def metaclass(cls):
        return cls

    def u(*args):
        return unicode(*args)

    def b(*args):
        return bytes(*args)
else:
    def metaclass(cls):
        dct = dict(cls.__dict__)
        dct.pop('__dict__', None)
        return cls.__metaclass__(cls.__name__, cls.__bases__, dct)

    def u(obj, *args):
        return str(obj)

    def b(obj, *args):
        return bytes(obj, encoding='utf-8', *args)
