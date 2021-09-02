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
#cython: language_level=3

from libc.stdint cimport UINT32_MAX

from libcommon_core cimport *
from libcommon_container_pxc cimport *


cdef inline cbool qhash_while(qhash_t *qh, uint32_t *pos):
    """Loop through a qhash.

    Parameters
    ----------
    qh
        The qhash to loop through.
    pos
        The position in the loop. It will be updated by the function. It must
        be incremented to get the next value before calling this function.

    Returns
    -------
        True if pos is a valid position, False otherwise.
    """
    if qh.hdr.len == 0:
        return False
    pos[0] = qhash_scan(qh, pos[0])
    return pos[0] < UINT32_MAX
