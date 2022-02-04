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

from cpython.version cimport PY_VERSION_HEX
from cpython.object cimport PyObject_Str

from libcommon_cython.core_pxc cimport *


# {{{ Definitions


cdef extern from "<lib-common/libcommon_cython/core.h>" nogil:
    ctypedef _Bool cbool

    cbool unlikely(cbool)
    cbool likely(cbool)
    void cassert(...)

    int ROUND_UP(int, int)
    cbool TST_BIT(const void *, int)

    void *p_clear(void *, int)
    void p_delete(void **)

    int MEM_BY_FRAME
    int MEM_RAW
    int PROT_READ
    int MAP_SHARED

    ctypedef const void *t_scope_t
    t_scope_t t_scope_init()
    void t_scope_ignore(t_scope_t)
    uint8_t *t_new_u8(int)
    char *t_new_char(int)

    lstr_t LSTR(const char *)
    lstr_t LSTR_INIT_V(const char *, int)
    extern lstr_t LSTR_NULL_V
    lstr_t LSTR_SB_V(const sb_t *)
    void *LSTR_FMT_ARG(lstr_t)
    char *t_fmt(const char *, ...)
    lstr_t t_lstr_fmt(const char *, ...)

    ctypedef char sb_static_buf_t[0]
    ctypedef sb_static_buf_t sb_buf_1k_t
    ctypedef sb_static_buf_t sb_buf_8k_t

    ctypedef sb_t sb_scope_t
    sb_scope_t sb_scope_init_static(sb_static_buf_t)
    sb_scope_t t_sb_scope_init(int)
    sb_scope_t t_sb_scope_init_1k()
    sb_scope_t t_sb_scope_init_8k()
    void sb_addf(sb_t *, const char *, ...)
    void sb_prependf(sb_t *, const char *, ...)

    const char *PyUnicode_AsUTF8AndSize(str obj,
                                        Py_ssize_t *size) except NULL


# }}}
# {{{ Helpers


cdef inline bytes py_str_to_py_bytes(str val):
    """Encode python str to python bytes with UTF-8 codec.

    Parameters
    ----------
    val
        The str object to encode.

    Returns
    -------
        The encoded python bytes.
    """
    return val.encode('utf8', 'strict')


cdef inline bytes c_str_len_to_py_bytes(const char *val, Py_ssize_t length):
    """Convert C string with length to python bytes.

    Parameters
    ----------
    val
        The C string to convert.
    length
        The length of the C string.

    Returns
    -------
        The converted python bytes.
    """
    return val[:length]


cdef inline bytes c_str_to_py_bytes(const char *val):
    """Convert C string to python bytes.

    Parameters
    ----------
    val
        The C string to convert.

    Returns
    -------
        The converted python bytes.
    """
    return c_str_len_to_py_bytes(val, len(val))


cdef inline str c_str_len_to_py_str(const char *val, Py_ssize_t length):
    """Decode C string with length to python str.

    We use UTF-8 codec with backslash replace error handling.
    Malformed data is replaced by a backslashed escape sequence, (i.e. \\xXX).
    See https://docs.python.org/3/library/codecs.html#error-handlers.

    Parameters
    ----------
    val
        The C string to decode.
    length
        The length of the C string.

    Returns
    -------
        The decoded python str.
    """
    return val[:length].decode('utf8', 'backslashreplace')


cdef inline str c_str_to_py_str(const char *val):
    """Decode C string to python str.

    Parameters
    ----------
    val
        The C string to decode.

    Returns
    -------
        The decoded python str.
    """
    return c_str_len_to_py_str(val, len(val))


cdef inline str py_bytes_to_py_str(bytes val):
    """Decode python bytes to python str.

    Parameters
    ----------
    val
        The bytes object to decode.

    Returns
    -------
        The decoded python str.
    """
    return c_str_len_to_py_str(val, len(val))


cdef inline bytes lstr_to_py_bytes(lstr_t lstr):
    """Convert lstr_t to python bytes.

    Parameters
    ----------
    lstr
        The lstr_t to convert.

    Returns
    -------
        The python bytes.
    """
    return c_str_len_to_py_bytes(lstr.s, lstr.len)


cdef inline str lstr_to_py_str(lstr_t lstr):
    """Convert lstr_t to python str.

    Parameters
    ----------
    lstr
        The lstr_t to convert.

    Returns
    -------
        The python str.
    """
    return c_str_len_to_py_str(lstr.s, lstr.len)


cdef inline lstr_t py_bytes_to_lstr(bytes obj):
    """Convert python bytes to lstr_t.

    Parameters
    ----------
    obj
        The python bytes to convert.

    Returns
    -------
        The lstr string.
    """
    return LSTR_INIT_V(obj, len(obj))


cdef inline lstr_t py_str_to_lstr(str obj) except *:
    """Convert python str to lstr_t.

    Parameters
    ----------
    obj
        The python str to convert.

    Returns
    -------
        The lstr string.
    """
    cdef const char *val
    cdef Py_ssize_t size

    size = 0
    val = PyUnicode_AsUTF8AndSize(obj, &size)
    return LSTR_INIT_V(val, size)


cdef inline lstr_t mp_py_obj_to_lstr(mem_pool_t *mp, object obj) except *:
    """Convert python object to lstr_t.

    Parameters
    ----------
    mp
        The memory pool used in case of allocations.
    obj
        The python object to convert.

    Returns
    -------
        The lstr string.
    """
    cdef lstr_t res

    if isinstance(obj, str):
        return py_str_to_lstr(<str>obj)
    elif isinstance(obj, bytes):
        return py_bytes_to_lstr(<bytes>obj)
    else:
        # PyObject_Str() is assured to return a string, so we should get back
        # to the first case.
        cassert(mp != NULL)
        res = mp_py_obj_to_lstr(NULL, PyObject_Str(obj))
        return mp_lstr_dup(mp, res)


cdef inline lstr_t t_py_obj_to_lstr(object obj) except *:
    """Convert python object to lstr_t using t_pool allocator.

    Parameters
    ----------
    obj
        The python object to convert.

    Returns
    -------
        The lstr string.
    """
    return mp_py_obj_to_lstr(t_pool(), obj)


# }}}
