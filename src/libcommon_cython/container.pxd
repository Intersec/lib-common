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
#cython: language_level=3

from libc.stdint cimport UINT32_MAX

from libcommon_cython.core cimport *
from libcommon_cython.container_pxc cimport *


cdef struct QHashIterator:
    # Iterator on a qhash.
    #
    # It is typically used like this:
    #
    #   cdef QHashIterator it
    #
    #   it = qhash_iter_make(&some_qm.qh)
    #   while qhash_iter_next(&it):
    #       do_something(some_qm.values[it.pos])
    #
    # Fields
    # ------
    # qh
    #     The qhash to iterate on.
    # pos
    #     The current item position.
    # next_pos
    #     The next item position to look for.
    #
    # Functions
    # ---------
    # qhash_iter_make()
    #     Make an iterator from a qhash.
    # qhash_iter_next()
    #     Advance the iterator to next item.
    const qhash_t *qh
    uint32_t pos
    uint32_t next_pos


cdef inline QHashIterator qhash_iter_make(const qhash_t *qh):
    """Make a new qhash iterator

    Parameters
    ----------
    qh
        The qhash to iterate on.

    Returns
    -------
        The qhash iterator.
    """
    cdef QHashIterator it

    it.qh = qh
    it.pos = 0 if qh.hdr.len > 0 else UINT32_MAX
    it.next_pos = 0
    return it


cdef inline cbool qhash_iter_next(QHashIterator *it):
    """Advance the iterator to next item.

    Parameters
    ----------
    it
        The qhash iterator.

    Returns
    -------
        True if the iterator has a next item, False otherwise.
    """
    if it.pos == UINT32_MAX:
        return False
    it.pos = qhash_scan(it.qh, it.next_pos)
    it.next_pos = it.pos + 1
    return it.pos < UINT32_MAX
