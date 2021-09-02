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

from libcommon_core cimport *
from libcommon_iop_pxc cimport *


cdef extern from "<lib-common/cython/libcommon_iop.h>" nogil:
    cbool is_ic_hdr_simple_hdr(const ic__hdr__t *)
    ic__hdr__t *t_iop_new_ic_hdr()
    void iop_init_ic_simple_hdr(ic__simple_hdr__t *)
    ic__hdr__t iop_ic_hdr_from_simple_hdr(ic__simple_hdr__t)
    ic__hdr__t *iop_dup_ic_hdr(const ic__hdr__t *)

    ctypedef struct ichannel_t:
        pass
    int32_t ichannel_get_cmd(const ichannel_t *ic)

    Lmid_t LM_ID_BASE
    int IOP_XPACK_LITERAL_ENUMS
    int IOP_XPACK_SKIP_PRIVATE
