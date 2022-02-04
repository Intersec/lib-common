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
# XXX: Cython is complaining about its own code with warn.undeclared.
#      Activate manually to see the warning.
#-cython: warn.undeclared=True

cimport cython

from cpython.object cimport (
    Py_EQ, Py_NE, Py_LT, Py_LE, Py_GT, Py_GE
)
from cpython.ref cimport PyObject, Py_INCREF, Py_DECREF
from cpython.mem cimport PyMem_Malloc, PyMem_Free
from cpython.pylifecycle cimport Py_IsInitialized, Py_AtExit
from cpython.ceval cimport PyEval_InitThreads
from cpython cimport bool
from libc.errno cimport errno
from libc.string cimport strerror


cdef extern from "Python.h":
    # Get raw builtin objects from Python.h
    ctypedef extern class builtins.Exception[object PyBaseExceptionObject]:
        pass
    ctypedef struct PyThreadState:
        pass
    int PyObject_GenericSetAttr(object o, object attr_name,
                                object v) except -1
    int Py_AddPendingCall(int (*func)(void *), void *arg)
    PyThreadState* PyGILState_GetThisThreadState() nogil
    PyThreadState *PyThreadState_GET() nogil
    PyThreadState* PyEval_SaveThread() nogil
    void PyEval_RestoreThread(PyThreadState *) nogil


from libcommon_cython.core cimport *
from libcommon_cython.container cimport *
from libcommon_cython.iop cimport *
from libcommon_cython.xml cimport *
from libcommon_cython.farch cimport *
from libcommon_cython.thr cimport *


cdef extern from "<lib-common/farch.h>":
    """
    #define iopy_dso_get_scripts(dso)                                        \
        IOP_DSO_GET_RESSOURCES(dso, iopy_on_register)
    """
    const farch_entry_t * const *iopy_dso_get_scripts(const iop_dso_t *)

cdef extern from "version.h" nogil:
    """
    extern const char iopy_git_revision[];
    """
    int IOPY_MAJOR
    int IOPY_MINOR
    int IOPY_PATCH
    extern const char *iopy_git_revision

cdef extern from "iopy_cython_export.h":
    pass

from iopy_rpc_pxc cimport *

# Must be added after all includes and imports.
from libcommon_cython.cython_fixes cimport *


# Import python modules that are used when using IOPy module.
# Python has a weird and rare deadlock with the GIL when doing import in a
# function in a thread. So it is best to import the modules on IOPy module
# init.
cdef object time
import time

cdef object sys
import sys

cdef object warnings
import warnings

cdef object inspect
import inspect

cdef object traceback
import traceback


# {{{ Globals


# Global type for the module.
cdef struct IopyGlobal:
    unsigned jpack_flags # Default json pack flags


# Global variable for the module.
cdef IopyGlobal iopy_g


# Dictionary that is used to create metaclasses
cdef dict empty_init_metaclass_dict_g = {}


# Dictionary to be used as class attributes
cdef dict class_attrs_dict_g = {
    '__module__': None
}


# }}}
# {{{ Helpers


cdef inline str make_py_pkg_name(str pkg_name):
    """Create python package name from C iop package name.

    Parameters
    ----------
    pkg_name
        The C iop package name

    Returns
    -------
        The python package name.
    """
    return pkg_name.replace('.', '_')


@cython.internal
@cython.final
cdef class IopPath:
    """Internal class to describe path of an iop symbol."""
    cdef str iop_fullname
    cdef str py_name
    cdef str pkg_name
    cdef str local_name


cdef IopPath make_iop_path(str iop_fullname):
    """Create iop symbol path description from iop symbol fullname.

    Parameters
    ----------
    iop_fullname
        The iop symbol fullname.

    Returns
    -------
        The iop symbol path description.
    """
    cdef IopPath iop_path = IopPath.__new__(IopPath)
    cdef Py_ssize_t pos

    pos = iop_fullname.rfind('.')
    iop_path.iop_fullname = iop_fullname
    iop_path.local_name = iop_fullname[pos + 1:]
    iop_path.pkg_name = make_py_pkg_name(iop_fullname[:pos])
    iop_path.py_name = '.'.join((iop_path.pkg_name, iop_path.local_name))

    return iop_path


cdef inline const iop_struct_t *get_iop_struct_parent(
    const iop_struct_t *st) nogil:
    """Helper to get the parent of an iop struct desc.

    Parameters
    ----------
    st
        The iop struct description.

    Returns
    -------
        NULL if the iop struct description is not a class or has no parents.
        The parent iop struct description otherwise.
    """
    if not iop_struct_is_class(st):
        return NULL
    return st.class_attrs.parent


cdef int py_object_generic_delattr(object o, object name) except -1:
    """Generic way to delete an attribute of an object with
    PyObject_GenericSetAttr.

    Parameters
    ----------
    o
        The object which the attribute should be deleted.
    name
        The name of attribute to delete.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    return PyObject_GenericSetAttr(o, name, <object>NULL)


cdef int32_t get_iface_rpc_cmd(const iop_iface_alias_t *iface_alias,
                               const iop_rpc_t *rpc):
    """Return the command index for the interface alias and the RPC.

    Parameters
    ----------
    iface_alias
        The IOP C interface alias.
    rpc
        The IOP C rpc.

    Returns
    -------
        The command index.
    """
    return (iface_alias.tag << 16) | rpc.tag


cdef int t_parse_uri_arg(object uri, object host, int port,
                        lstr_t *res) except -1:
    """Parse and validate uri from arguments.

    Parameters
    ----------
    uri
        The uri argument.
    host
        The host argument.
    port
        The port argument.
    res
        The result uri after parse.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef lstr_t host_lstr

    if uri is not None:
        if host is not None or port >= 0:
            raise Error("host or port argument shouldn't be provided with "
                        "uri argument")
        res[0] = t_py_obj_to_lstr(uri)
    else:
        if host is None or port < 0:
            raise Error("when uri is not provided, host and port arguments"
                        " should be provided")
        host_lstr = t_py_obj_to_lstr(host)
        res[0] = t_lstr_fmt("%*pM:%d", LSTR_FMT_ARG(host_lstr), port)
    return 0


cdef object get_warning_time_str():
    """Get the current time description as string that is used on warnings.

    Returns
    -------
    str
        The current time description.
    """
    return '%d: %s' % (int(time.time()), time.ctime())


cdef inline cbool iop_struct_is_same_or_child_of(const iop_struct_t *child,
                                                 const iop_struct_t *parent):
    """Check if iop struct desc `child` is the same of a child of `parent`.

    Parameters
    ----------
    child
        The IOP struct desc child.
    parent
        The IOP struct desc parent.

    Returns
    -------
        True of iop struct desc `child` is the same of a child of `parent`,
        False otherwise.
    """
    if iop_struct_is_class(child):
        return iop_struct_is_class(parent) and iop_class_is_a(child, parent)
    else:
        return child == parent


cdef inline int check_iopy_ic_res(iopy_ic_res_t res,
                                  const sb_t *err) except -1:
    """Check if the result of an IOPy IC operation is valid and raise an
    appropriate exception otherwise.

    Parameters
    ----------
    res
        The IOPy IC operation result.
    err
        The error description in case of error.
    """
    if res == IOPY_IC_ERR:
        raise Error(lstr_to_py_str(LSTR_SB_V(err)))
    elif res == IOPY_IC_SIGINT:
        raise KeyboardInterrupt()
    else:
        cassert(res == IOPY_IC_OK)
    return 0


# }}}
# {{{ Errors


cdef class Error(Exception):
    """Iopy generic error."""
    pass


cdef class RpcError(Error):
    """Iopy RPC error."""
    pass


@cython.warn.undeclared(False)
class Warning(Warning):
    """Iopy Warning."""
    pass


@cython.warn.undeclared(False)
class ServerWarning(Warning):
    """Base class for all server warnings.

    Derived from iopy.Warning.
    Deactivate warnings of this class to hide connect/disconnect messages.
    """
    pass


@cython.warn.undeclared(False)
class ServerConnectWarning(ServerWarning):
    """Class for incoming server connections messages.

    Derived from iopy.ServerWarning.
    Deactivate warnings of this class to hide connect messages.
    """
    pass


@cython.warn.undeclared(False)
class ServerDisconnectWarning(ServerWarning):
    """Class for server remote disconnections messages.

    Derived from iopy.ServerWarning.
    Deactivate warnings of this class to hide disconnect messages.
    """
    pass


@cython.warn.undeclared(False)
class ClientWarning(Warning):
    """Class for channel client warning messages.

    Derived from iopy.Warning.
    Deactivate warnings of this class to hide client warning messages.
    """
    pass


cdef int send_exception_to_main_thread_cb(void *arg) except -1:
    """Callback used by Py_AddPendingCall and send_exception_to_main_thread()
    to rethrow the exception in the main thread.

    Parameters
    ----------
    arg
        The exception to rethrow.

    Returns
    -------
        -1 to rethrow the exception.
    """
    cdef tuple exc = <tuple>arg

    try:
        raise exc[0], exc[1], exc[2]
    finally:
        Py_DECREF(exc)


cdef void send_exception_to_main_thread():
    """Send the current exception to the main thread"""
    cdef tuple exc

    exc = sys.exc_info()
    Py_INCREF(exc)
    Py_AddPendingCall(<int (*)(void *)>&send_exception_to_main_thread_cb,
                      <void *>exc)


cdef struct SendWarningCtx:
    # Struct to hold the warning cls and message
    PyObject *cls
    PyObject *message


cdef int send_warning_to_main_thread_cb(void *arg):
    """Callback used by Py_AddPendingCall and send_warning_to_main_thread()
    to send warning to the main thread.

    Parameters
    ----------
    arg
        The SendWarningCtx containing the warning cls and message.
    """
    cdef SendWarningCtx *ctx = <SendWarningCtx *>arg
    cdef object cls = <object>ctx.cls
    cdef object message = <object>ctx.message

    try:
        if Py_IsInitialized():
            warnings.warn_explicit(message, cls, 'sys', 1)
    finally:
        Py_DECREF(cls)
        Py_DECREF(message)
        PyMem_Free(ctx)


cdef void send_warning_to_main_thread(object cls, object message):
    """Print python warning with message.

    Parameters
    ----------
    cls
        The warning class to be used.
    message
        The message to print.
    """
    cdef SendWarningCtx *ctx

    ctx = <SendWarningCtx *>PyMem_Malloc(sizeof(SendWarningCtx))

    Py_INCREF(cls)
    ctx.cls = <PyObject *>cls

    Py_INCREF(message)
    ctx.message = <PyObject *>message
    Py_AddPendingCall(&send_warning_to_main_thread_cb, <void *>ctx)


# }}}
# {{{ Base metaclass holder


@cython.internal
cdef class _InternalBaseHolder(type):
    """Internal metaclass to hold plugin and flag if the class has been
    upgraded through plugin metaclasses.

    See comment in plugin metaclass fold.
    """
    cdef cbool is_metaclass_upgraded
    cdef Plugin plugin


# }}}
# {{{ Types

# Class inheritance
#
# Each generated iopy class is composed of two parts, a class proxy and a
# public class interface.
#
# When the user wants to upgrade an iopy class, the new custom class is
# inserted between the proxy and the public interface. This way, the
# child classes can benefit from the custom class.
#
# The proxy class contains a class variable _iop_type that points to the
# instance of the Cython extension type.
#
# Because Cython extension types does not support C class variables, IOPy
# types do not directly hold the C IOP type descriptor (iop_enum_t or
# iop_struct_t).
#
# To solve this issue, we will need to store this information on the type of
# the IOPy types. A.k.a, we need to use metaclasses. However, we also need to
# create these metaclasses dynamically. So we need a metaclass that create
# metaclass that will hold the C IOP type descriptor and will create the IOPy
# types.
#
# This complex types hierarchy permits us to be able to quickly get the C IOP
# type descriptor `type(type(obj)).desc', and avoid using a standard python
# class variable with a dictionary lookup.
#
# Below is an example of the generated classes from the IOP classes A and B
# where B inherits from A. The structure of the classes inheritance is the
# same for enum, union and struct except that the generated proxy class can
# only inherits from EnumBase, UnionBase and StructBase respectively.
#
#  +---------------+
#  |iopy.StructBase|                    +------------------------+
#  +-------+-------+                    |_InternalStructUnionType|
#          |                            +------------------------+
#    +-----v-----+-----+                            ^
#    |  A_proxy  |     |                       type | instance
#    +-----+-----+     |                            |
#          |           | type                       |
#  +=======v========+------------>+----------+------+
#  | custom A class |  | instance |  A_meta  |      |
#  +=======+========+  |          +----------+      |
#          |           |          |IOP desc A|      |
# +--------v---------+-+          +----+-----+      |
# |        A         |                 |            |
# |(public interface)|                 |            |
# +--------+---------+                 |            |
#          |                           |            |
#    +-----v-----+-----+               |            |
#    |  B_proxy  |     |               |            |
#    +-----+-----+     |               |            |
#          |           | type          |            |
#  +=======v========+------------>+----v-----+------+
#  | custom B class |  | instance |  B_meta  |
#  +=======+========+  |          +----------+
#          |           |          |IOP desc B|
# +--------v---------+-+          +----------+
# |        B         |
# |(public interface)|
# +------------------+

# {{{ Base


cdef class Basic:
    """Base class for all IOPy types."""
    cdef dict __dict__

    def __hash__(Basic self):
        """IOPy types are not hashable"""
        raise TypeError('unhashable type')


@cython.final
cdef class IopHelpDescription:
    """Documentation of an IOP element.

    Attributes
    ----------
    brief : str or None
        The brief documentation of the IOP element.
    details : str or None
        The detailed documentation of the IOP element.
    warning : str or None
        The warning documentation of the IOP element.
    example : str or None
        The example documentation of the IOP element.
    """
    cdef readonly object brief
    cdef readonly object details
    cdef readonly object warning
    cdef readonly object example


cdef IopHelpDescription make_iop_help_description(const iop_help_t *iop_help,
                                                  cbool is_v2):
    """Make IopHelpDescription from iop_help_t.

    Parameters
    ----------
    iop_help
        The C iop_help_t. It can also be NULL to create an empty
        IopHelpDescription.
    is_v2
        Is the C iop_help_t ATTR_HELP_V2.

    Returns
    -------
        The python IopHelpDescription.
    """
    cdef IopHelpDescription res

    res = IopHelpDescription.__new__(IopHelpDescription)

    if iop_help:
        if iop_help.brief.s:
            res.brief = lstr_to_py_str(iop_help.brief)

        if iop_help.details.s:
            res.details = lstr_to_py_str(iop_help.details)

        if iop_help.warning.s:
            res.warning = lstr_to_py_str(iop_help.warning)

        if is_v2 and iop_help.example.s:
            res.example = lstr_to_py_str(iop_help.example)

    return res


# }}}
# {{{ Enum


@cython.internal
@cython.final
cdef class _InternalEnumType(_InternalBaseHolder):
    """Internal metaclass to hold C enum iop desc."""
    cdef const iop_enum_t *desc


@cython.internal
cdef class _InternalEnumMetaclass(type):
    """Base metaclass of enum class types"""
    @property
    def __dict__(_InternalEnumMetaclass cls):
        """Return the values of the enum type.

        Parameters
        ----------
        cls : object
            The enum class type.

        Returns
        -------
        dict(str, int)
            The dict of values with the name as key and the integer value as
            value.
        """
        return enum_get_values(cls)


cdef class EnumBase(Basic):
    """Iopy Enum object

    Only one value allowed, in given values() list, set via set() method.
    set() can take as input int or string.
    Objects are callable to create new instances.
    """
    cdef int val

    def __init__(EnumBase self, object val=None):
        """Constuctor.

        Parameters
        ----------
        val : int or str
            The value of the enum. See set() method for a complete description
            of allowed values.
        """
        cdef const iop_enum_t *en

        if val is None:
            en = enum_get_desc(self)
            assert (en.enum_len >= 1)
            self.val = en.values[0]
        else:
            enum_set(self, val)

    @classmethod
    def name(object cls):
        """Return the name of the enum.

        Returns
        -------
        str
            The enum IOP name.
        """
        cdef const iop_enum_t *en = enum_get_desc_cls(cls)

        return lstr_to_py_str(en.name)

    @classmethod
    def fullname(object cls):
        """Return the fullname of the enum.

        Returns
        -------
        str
            The enum IOP fullname.
        """
        return enum_get_fullname(cls)

    @classmethod
    def __fullname__(object cls):
        """Return the fullname of the enum.

        Returns
        -------
        str
            The enum IOP fullname.
        """
        return enum_get_fullname(cls)

    @classmethod
    def values(object cls):
        """Return the dict of allowed values.

        Returns
        -------
        dict(str, int)
            The dict of values with the name as key and the integer value as
            value.
        """
        return enum_get_values(cls)

    @classmethod
    def __values__(object cls):
        """Deprecated, use values() instead."""
        return enum_get_values(cls)

    @classmethod
    def ranges(object cls):
        """Return the ranges of the enum.

        Returns
        -------
        dict(int, int)
            The ranges of the IOP enum.
        """
        return enum_get_ranges(cls)

    @classmethod
    def __ranges__(object cls):
        """Deprecated, use ranges() instead."""
        return enum_get_ranges(cls)

    @classmethod
    def get_iop_description(object cls):
        """Get the IOP description of the enum.

        Returns
        -------
        IopEnumDescription
            The IOP description of the enum type.
        """
        cdef const iop_enum_t *en = enum_get_desc_cls(cls)

        return enum_make_iop_description(en)

    def set(EnumBase self, object val):
        """Set the value.

        See values() for allowed argument (can be passed as int or string).
        When underlying enum is not @strict, usage as bitfield is allowed,
        like:
        enum Toto {
            A = 1 <<  0,
            B = 1 <<  1,
            C = 1 <<  2,
        };
        print(pkg.Toto('A'))
            Enum pkg.Toto:A (1)
        print(pkg.Toto('A|B'))
            Enum pkg.Toto:A|B (3)
        print(pkg.Toto(3))
            Enum pkg.Toto:A|B (3)
        print pkg.Toto(8)
            Enum pkg.Toto:undefined (8)

        When an enum is required as an argument of a given upper layer struct
        or union, one can pass only the string as the type is already defined
        (the value is of course checked and an error is thrown if the enum can
        not be created).

        Parameters
        ----------
        val : int or str
            The new value of the enum.
        """
        enum_set(self, val)

    def get_as_int(EnumBase self):
        """Return the stored value as an integer.

        Returns
        -------
        int
            The integer value of the enum.
        """
        return self.val

    def __int__(EnumBase self):
        """Cast from enum to int.

        Returns
        -------
        int
            The integer value of the enum.
        """
        return self.val

    def __index__(EnumBase self):
        """Cast from enum to int.

        Returns
        -------
        int
            The integer value of the enum.
        """
        return self.val

    def get_as_str(EnumBase self):
        """Return the stored value as a string.

        Returns
        -------
        str
            The string value of the enum.
        """
        return enum_get_as_name(self)

    def __str__(EnumBase self):
        """Cast from enum to str.

        Returns
        -------
        str
            The string value of the enum.
        """
        return enum_get_as_name(self)

    def __repr__(EnumBase self):
        """Return the representation of the enum.

        Returns
        -------
        str
            The representation of the enum as a string.
        """
        cdef const iop_enum_t *en = enum_get_desc(self)

        return 'Enum %s:%s (%s)' % (lstr_to_py_str(en.fullname),
                                    enum_get_as_name(self), self.val)

    def __richcmp__(EnumBase self, object other, int op):
        """Compare enum with another value.

        Parameters
        ----------
        other : int or EnumBase
            The value to compare the enum against.
        op : int
            The python compare operation to perform.
        """
        cdef int self_val = self.val
        cdef int other_val

        if isinstance(other, int):
            other_val = other
        elif isinstance(other, str):
            other_val = 0
            enum_parse_str_value(enum_get_desc(self), other, &other_val)
        elif (isinstance(other, EnumBase)
          and enum_get_desc(self) == enum_get_desc(<EnumBase>other)):
            other_val = (<EnumBase>(other)).val
        else:
            if op == Py_EQ:
                return False
            elif op == Py_NE:
                return True
            else:
                raise TypeError('comparison between uncompatible objects')

        if op == Py_EQ:
            return self_val == other_val
        elif op == Py_NE:
            return self_val != other_val
        elif op == Py_LT:
            return self_val < other_val
        elif op == Py_LE:
            return self_val <= other_val
        elif op == Py_GT:
            return self_val > other_val
        elif op == Py_GE:
            return self_val >= other_val
        else:
            raise NotImplementedError()


cdef class Enum(EnumBase):
    """Enum class for backward compatibility."""


@cython.final
cdef class IopEnumDescription:
    """Description of an IOP enum type.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP enum.
    strict : bool
        True if the IOP enum is strict. False otherwise.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP enum.
    values : dict(str, IopEnumValueDescription)
        The dictionary of IOP description of each values.
    """
    cdef readonly IopHelpDescription help
    cdef readonly bool strict
    cdef readonly dict generic_attributes
    cdef readonly dict values


@cython.final
cdef class IopEnumValueDescription:
    """Description of an IOP enum value.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP enum value.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP enum value.
    aliases : tuple(str)
        Aliases of the value.
    """
    cdef readonly IopHelpDescription help
    cdef readonly dict generic_attributes
    cdef readonly tuple aliases


cdef inline const iop_enum_t *enum_get_desc_cls(object cls) except NULL:
    """Get C enum desc from enum class.

    Parameters
    ----------
    cls
        The IOPy enum type.

    Returns
    -------
        The C enum iop type.
    """
    cdef _InternalEnumType iop_type = type(cls)

    return iop_type.desc


cdef inline const iop_enum_t *enum_get_desc(EnumBase py_en) except NULL:
    """Get C enum desc from enum object.

    Returns
    -------
        The C enum iop type.
    """
    return enum_get_desc_cls(type(py_en))


cdef int enum_set(EnumBase py_en, object val) except -1:
    """Set enum value.

    Parameters
    ----------
    py_en
        The enum python object.
    val : int or str
        The new value of the enum.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef const iop_enum_t *en
    cdef str val_str

    if isinstance(val, int):
        py_en.val = val
        return 0
    elif isinstance(val, str):
        val_str = val
    else:
        raise Error('%s is not a str or int' % val)

    en = enum_get_desc(py_en)
    enum_parse_str_value(en, val_str, &py_en.val)
    return 0


cdef int enum_parse_str_value(const iop_enum_t *en, str val_str,
                              int *res) except -1:
    """Parse enum string value and set it to res.

    Parameters
    ----------
    en
        The C enum iop type.
    val_str
        The string value name to search.
    res
        The integer value to set.

    Returns
    -------
        -1 in case of invalid value with an exception, 0 otherwise.
    """
    cdef list split_str
    cdef int loop_val

    val_str = val_str.upper()
    if enum_find_val_by_name(en, val_str, res):
        return 0

    if TST_BIT(&en.flags, IOP_ENUM_STRICT):
        raise Error('%s is not a valid value for enum %s' %
                    (val_str, lstr_to_py_str(en.fullname)))

    split_str = val_str.split('|')
    res[0] = 0
    loop_val = 0
    for val_str in split_str:
        val_str = val_str.strip()
        if not enum_find_val_by_name(en, val_str, &loop_val):
            raise Error('%s is not a valid value for enum %s' %
                        (val_str, lstr_to_py_str(en.fullname)))
        res[0] |= loop_val
    return 0


cdef cbool enum_find_val_by_name(const iop_enum_t *en, str val,
                                 int *res):
    """Find enum integer value from string value.

    Parameters
    ----------
    en
        The C enum iop type.
    val
        The string value name to search.
    res
        If found, it will hold the integer value.

    Returns
    -------
        True if the value was found, False otherwise.
    """
    cdef int i
    cdef str name_str
    cdef const iop_enum_alias_t *alias

    for i in range(en.enum_len):
        name_str = lstr_to_py_str(en.names[i])
        if name_str == val:
            res[0] = en.values[i]
            return True

    if TST_BIT(&en.flags, IOP_ENUM_ALIASES) and en.aliases:
        for i in range(en.aliases.len):
            alias = &en.aliases.aliases[i]
            name_str = lstr_to_py_str(alias.name)
            if name_str == val:
                res[0] = en.values[alias.pos]
                return True

    return False


cdef str enum_get_as_name(EnumBase py_en):
    """Get enum value as string.

    Parameters
    ----------
    py_en
        The enum python object.

    Returns
    -------
    str :
        The string value of the enum.
    """
    cdef sb_buf_1k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef const iop_enum_t *en = enum_get_desc(py_en)
    cdef int val = py_en.val
    cdef lstr_t val_lstr
    cdef int i
    cdef int en_val

    val_lstr = iop_enum_to_str_desc(en, val)
    if val_lstr.s:
        return lstr_to_py_str(val_lstr)

    for i in range(en.enum_len):
        en_val = en.values[i]
        if (val & en_val) == en_val:
            if sb.len:
                sb_adds(&sb, '|')

            sb_add_lstr(&sb, en.names[i])
            val ^= en_val

    if val == 0 and sb.len:
        return lstr_to_py_str(LSTR_SB_V(&sb))
    else:
        return 'undefined'


cdef str enum_get_fullname(object cls):
    """Return the fullname of the enum.

    Parameters
    ----------
    cls
        The enum python class type.

    Returns
    -------
        The enum IOP fullname.
    """
    cdef const iop_enum_t *en = enum_get_desc_cls(cls)

    return lstr_to_py_str(en.fullname)


cdef dict enum_get_values(object cls):
    """Return the dict of allowed values.

    Parameters
    ----------
    cls
        The enum python class type.

    Returns
    -------
        The dict of values with the name as key and the integer value as
        value.
    """
    cdef const iop_enum_t *en = enum_get_desc_cls(cls)
    cdef dict res = {}
    cdef int i

    for i in range(en.enum_len):
        res[lstr_to_py_str(en.names[i])] = en.values[i]
    return res


cdef dict enum_get_ranges(object cls):
    """Return the ranges of the enum.

    Parameters
    ----------
    cls
        The enum class type.

    Returns
    -------
    dict(int, int)
        The ranges of the IOP enum.
    """
    cdef const iop_enum_t *en = enum_get_desc_cls(cls)
    cdef dict res = {}
    cdef int i

    for i in range(en.ranges_len):
        res[i] = en.ranges[i]
    return res


cdef IopEnumDescription enum_make_iop_description(const iop_enum_t *en):
    """Make IOP enum description from a C enum iop type.

    Parameters
    ----------
    en
        The C enum iop type.

    Returns
    -------
        The IOP enum description.
    """
    cdef const iop_help_t *iop_help = NULL
    cdef cbool is_help_v2 = False
    cdef IopEnumDescription res

    res = IopEnumDescription.__new__(IopEnumDescription)
    res.generic_attributes = enum_make_iop_generic_attributes(en, &iop_help,
                                                              &is_help_v2)
    res.help = make_iop_help_description(iop_help, is_help_v2)
    res.strict = TST_BIT(&en.flags, IOP_ENUM_STRICT)
    res.values = enum_make_iop_values_descriptions(en)

    return res


cdef dict enum_make_iop_generic_attributes(const iop_enum_t *en,
                                           const iop_help_t **iop_help,
                                           cbool *is_help_v2):
    """Make generic attributes of a enum iop type.

    Parameters
    ----------
    en
        The C enum iop type.
    iop_help
        Set to the iop help of the enum iop type if found.
    is_help_v2
        Set to True if the iop help is ATTR_HELP_V2.

    Returns
    -------
        The generic attributes of the enum iop type.
    """
    cdef int i
    cdef const iop_enum_attr_t *attr
    cdef str attr_key
    cdef object attr_val
    cdef dict res = {}

    if not TST_BIT(&en.flags, IOP_ENUM_EXTENDED) or not en.en_attrs:
        return res

    for i in range(en.en_attrs.attrs_len):
        attr = &en.en_attrs.attrs[i]

        if attr.type == IOP_ENUM_ATTR_HELP:
            iop_help[0] = <const iop_help_t *>(attr.args[0].v.p)
            is_help_v2[0] = False
            continue
        elif attr.type == IOP_ENUM_ATTR_HELP_V2:
            iop_help[0] = <const iop_help_t *>(attr.args[0].v.p)
            is_help_v2[0] = True
            continue
        elif (attr.type == IOP_ENUM_GEN_ATTR_S
           or attr.type == IOP_ENUM_GEN_ATTR_O):
            attr_val = lstr_to_py_str(attr.args[1].v.s)
        elif attr.type == IOP_ENUM_GEN_ATTR_I:
            attr_val = attr.args[1].v.i64
        elif attr.type == IOP_ENUM_GEN_ATTR_D:
            attr_val = attr.args[1].v.d
        else:
            cassert (False)
            continue

        attr_key = lstr_to_py_str(attr.args[0].v.s)
        res[attr_key] = attr_val

    return res


cdef dict enum_make_iop_values_descriptions(const iop_enum_t *en):
    """Make IOP enum values descriptions of a C enum iop type.

    Parameters
    ----------
    en
        The C enum iop type.

    Returns
    -------
        The IOP enum values descriptions as a dict.
    """
    cdef int i
    cdef str name_str
    cdef dict res = {}

    for i in range(en.enum_len):
        name_str = lstr_to_py_str(en.names[i])
        res[name_str] = enum_make_iop_value_description(en, i)

    return res


cdef IopEnumValueDescription enum_make_iop_value_description(
    const iop_enum_t *en, int pos):
    """Make IOP enum value description of a C enum value iop attributes.

    Parameters
    ----------
    en
        The C enum iop type.
    pos
        The position of the value in the enum iop type arrays.

    Returns
    -------
        The IOP enum value description.
    """
    cdef const iop_help_t *iop_help = NULL
    cdef cbool is_help_v2 = False
    cdef IopEnumValueDescription res

    res = IopEnumValueDescription.__new__(IopEnumValueDescription)
    res.generic_attributes = enum_make_iop_value_generic_attributes(
        en, pos, &iop_help, &is_help_v2)
    res.help = make_iop_help_description(iop_help, is_help_v2)
    res.aliases = enum_make_iop_value_aliases(en, pos)

    return res


cdef dict enum_make_iop_value_generic_attributes(
    const iop_enum_t *en, int pos, const iop_help_t **iop_help,
    cbool *is_help_v2):
    """Make IOP enum value generic attributes of a C enum iop value.

    Parameters
    ----------
    en
        The C enum iop type.
    pos
        The position of the value in the enum iop type arrays.
    iop_help
        Set to the iop help of the enum iop value if found.
    is_help_v2
        Set to True if the iop help is ATTR_HELP_V2.

    Returns
    -------
        The generic attributes of the enum iop value.
    """
    cdef int i
    cdef const iop_enum_value_attrs_t *attrs
    cdef const iop_enum_value_attr_t *attr
    cdef str attr_key
    cdef object attr_val
    cdef dict res = {}

    if not TST_BIT(&en.flags, IOP_ENUM_EXTENDED) or not en.values_attrs:
        return res

    attrs = &en.values_attrs[pos]
    for i in range(attrs.attrs_len):
        attr = &attrs.attrs[i]

        if attr.type == IOP_ENUM_VALUE_ATTR_HELP:
            iop_help[0] = <const iop_help_t *>(attr.args[0].v.p)
            is_help_v2[0] = False
            continue
        elif attr.type == IOP_ENUM_VALUE_ATTR_HELP_V2:
            iop_help[0] = <const iop_help_t *>(attr.args[0].v.p)
            is_help_v2[0] = True
            continue
        elif (attr.type == IOP_ENUM_VALUE_GEN_ATTR_S
           or attr.type == IOP_ENUM_VALUE_GEN_ATTR_O):
            attr_val = lstr_to_py_str(attr.args[1].v.s)
        elif attr.type == IOP_ENUM_VALUE_GEN_ATTR_I:
            attr_val = attr.args[1].v.i64
        elif attr.type == IOP_ENUM_VALUE_GEN_ATTR_D:
            attr_val = attr.args[1].v.d
        else:
            cassert (False)
            continue

        attr_key = lstr_to_py_str(attr.args[0].v.s)
        res[attr_key] = attr_val

    return res


cdef tuple enum_make_iop_value_aliases(const iop_enum_t *en, int pos):
    """Make IOP enum value aliases of a C enum iop value.

    Parameters
    ----------
    en
        The C enum iop type.
    pos
        The position of the value in the enum iop type arrays.

    Returns
    -------
        The generic attributes of the enum iop value.
    """
    cdef int i
    cdef list res
    cdef const iop_enum_alias_t *alias

    if not TST_BIT(&en.flags, IOP_ENUM_ALIASES) or not en.aliases:
        return ()

    res = []
    for i in range(en.aliases.len):
        alias = &en.aliases.aliases[i]
        if alias.pos == pos:
            res.append(lstr_to_py_str(alias.name))

    return tuple(res)

# }}}
# {{{ StructUnionBase


@cython.internal
@cython.final
cdef class _InternalStructUnionType(_InternalBaseHolder):
    """Internal metaclass to hold C iop struct and union type"""
    cdef const iop_struct_t *desc


@cython.internal
cdef class _InternalStructUnionMetaclass(type):
    """Base metaclass of struct and union class types"""
    def __call__(_InternalStructUnionMetaclass cls, *args, **kwargs):
        cdef StructUnionBase obj
        cdef dict new_kwargs

        obj = parse_special_kwargs(cls, kwargs)
        if obj is not None:
            return obj

        new_kwargs = struct_union_parse_dict_args(args, kwargs)
        if new_kwargs is not None:
            args = ()
            kwargs = new_kwargs

        return struct_union_get_cls_and_create_obj(cls, args, kwargs)

    @property
    def __dict__(_InternalStructUnionType cls):
        """Return the values of the struct or union type.

        In order to keep IOPyV1 compatibility, optional fields are skipped.

        Parameters
        ----------
        cls : object
            The struct or union class type.

        Returns
        -------
        dict(str, type)
            A dictionary containing the different available values of the
            struct or union.
        """
        return struct_union_get_values_of_cls(cls, True)


cdef class StructUnionBase(Basic):
    """Base class for IOPy struct and union types."""

    @classmethod
    def from_file(object cls, **kwargs):
        """Unpack an IOPy struct or union from a file.

        One and only one of the named arguments _xml, _json, _yaml, _hex or
        _bin must be set depending on the expected file format.

        Parameters
        ----------
        _json : str
            Name of the file to unpack as json.
        _yaml : str
            Name of the file to unpack as yaml.
        _xml : str
            Name of the file to unpack as xml.
        _hex : str
            Name of the file to unpack as hex.
        _bin : str
            Name of the file to unpack as binary.
        single : bool
            When set to False, allows to read multiple instances of the same
            union when unpacking binary file. It is not used for other unpack
            formats and for struct. Default is True.

        Returns
        -------
        struct, union or list
            The unpacked struct or union IOPy object, or a list of union IOPy
            objects for multiple binary union unpack.

        Examples
        --------
            cf = iops.lms_cfg.Lms.from_file(_json="etc/lms/lms.cf")

            logs = iops.lms.BonusLogRecord.from_file(
                _bin="/srv/data/lms/var/log/lms/bonus/bonus.bin")
        """
        return unpack_file_from_args_to_py_obj(cls, kwargs)

    @classmethod
    def __from_file__(object cls, **kwargs):
        """Deprecated, use from_file() instead."""
        return unpack_file_from_args_to_py_obj(cls, kwargs)

    def __richcmp__(StructUnionBase self, object other, int op):
        """Compare struct or union with another value.

        Parameters
        ----------
        other : struct or union
            The value to compare the struct or union against.
        op : int
            The python compare operation to perform.
        """
        cdef t_scope_t t_scope_guard = t_scope_init()
        cdef const iop_struct_t *self_st = struct_union_get_desc(self)
        cdef StructUnionBase other_cast
        cdef const iop_struct_t *other_st
        cdef cbool equal
        cdef void *self_val
        cdef void *other_val

        t_scope_ignore(t_scope_guard)

        if op != Py_EQ and op != Py_NE:
            raise TypeError('unsupported comparison operation')

        if not isinstance(other, StructUnionBase):
            return op != Py_EQ

        other_cast = <StructUnionBase>other
        other_st = struct_union_get_desc(other_cast)

        if self_st != other_st:
            return op != Py_EQ

        self_val = NULL
        other_val = NULL
        mp_iop_py_obj_to_c_val(t_pool(), False, self, &self_val)
        mp_iop_py_obj_to_c_val(t_pool(), False, other_cast, &other_val)
        equal = iop_equals_desc(self_st, self_val, other_val)
        return equal == (op == Py_EQ)

    def to_json(StructUnionBase self, **kwargs):
        """Format the struct or union object as JSON.

        Parameters
        ----------
        no_whitespaces : bool, optional
            Generate JSON without identation, spaces, ...
        no_trailing_eol : bool, optional
            Do not append '\\n' when done.
        skip_private : bool, optional
            Skip the private fields when dumping the JSON (lossy).
        skip_default : bool, optional
            Skip fields having their default value.
            This is good to make the JSON more compact, but is dangerous if a
            default value changes.
        skip_empty_arrays : bool, optional
            Skip empty repeated fields.
        skip_empty_structs : bool, optional
            Skip empty sub-structures.
        shorten_data : bool, optional
            Shorten long data strings when not writing a file (lossy).
            Data longer than 25 characters will be replaced by
            "XXXXXXXXXXX …(skip x bytes)… YYYYYYYYYYY" where only the first
            and last 11 characters are kept.
        skip_class_names : bool, optional
            Skip class names (lossy).
        skip_optional_class_names : bool, optional
            Skip class names when not needed.
            If set, the class names won't be written if they are equal to the
            actual type of the field (missing class names are supported by the
            unpacker in that case).
        minimal : bool, optional
            Produce the smallest non-lossy possible JSON.
            This is:
                no_whitespaces +
                no_trailing_eol +
                skip_default +
                skip_empty_arrays +
                skip_empty_structs +
                skip_optional_class_names

        Returns
        -------
        str
            The formatted string.
        """
        return format_py_obj_to_json(self, kwargs)

    def __json__(StructUnionBase self, **kwargs):
        """Deprecated, use to_json() instead."""
        return format_py_obj_to_json(self, kwargs)

    def to_yaml(StructUnionBase self):
        """Format the struct or union object as YAML.

        Returns
        -------
        str
            The formatted string.
        """
        return format_py_obj_to_yaml(self)

    def __yaml__(StructUnionBase self):
        """Deprecated, use to_yaml() instead."""
        return format_py_obj_to_yaml(self)

    def to_bin(StructUnionBase self):
        """Format the struct or union object as binary.

        Returns
        -------
        str
            The formatted string.
        """
        return format_py_obj_to_bin(self)

    def __bin__(StructUnionBase self):
        """Deprecated, use to_bin() instead."""
        return format_py_obj_to_bin(self)

    def to_hex(StructUnionBase self):
        """Format the struct or union object as hex.

        Returns
        -------
        str
            The formatted string.
        """
        return format_py_obj_to_hex(self)

    # XXX: Ugly hack to avoid that cython uses __hex__ as the function to
    # convert a number to hexadecimal with the hex() function.
    # See https://cython.readthedocs.io/en/latest/src/userguide/special_methods.html?highlight=__hex__#numeric-conversions
    @property
    def __hex__(StructUnionBase self):
        """Deprecated, use to_hex() instead."""
        cdef object wrap

        def wrap():
            return format_py_obj_to_hex(self)
        return wrap

    def to_xml(StructUnionBase self, **kwargs):
        """Format the struct or union object as XML.

        Parameters
        ----------
        name : str, optional
            Name of the xml structure. Default is the IOP type fullname.
        ns : str, optional
            Name of the xml structure namespace. Required when soap is set.
        soap : bool, optional
            Add SOAP XML banner.
        banner : bool, optional
            Add XML banner.

        Returns
        -------
        str
            The formatted string.
        """
        return format_py_obj_to_xml(self, kwargs)

    def __xml__(StructUnionBase self, **kwargs):
        """Deprecated, use to_xml() instead."""
        return format_py_obj_to_xml(self, kwargs)

    def __str__(StructUnionBase self):
        """Return the string representation as JSON of the struct or union
        object.

        Returns
        -------
        str
            The JSON representation of the object.
        """
        return format_py_obj_to_json(self, None)

    @classmethod
    def __fullname__(object cls):
        """Return IOP fullname of struct or union.

        Returns
        -------
        str
            The IOP fullname of the struct or union.
        """
        cdef const iop_struct_t *st

        st = struct_union_get_iop_type_cls(cls).desc
        return lstr_to_py_str(st.fullname)

    @classmethod
    def get_fields_name(object cls):
        """Get the list of name of fields of struct or union.

        Returns
        -------
        list
            The list of name of fields.
        """
        return struct_union_get_fields_name(cls)

    @classmethod
    def __get_fields_name__(object cls):
        """Deprecated, use get_fields_name() instead."""
        return struct_union_get_fields_name(cls)

    @classmethod
    def get_desc(object cls):
        """Return the description of the struct or union.

        Returns
        -------
            The string description of the struct or union.
        """
        return get_struct_union_desc(cls)

    @classmethod
    def __desc__(object cls):
        """Deprecated, use get_desc() instead."""
        return get_struct_union_desc(cls)

    @classmethod
    def get_values(object cls):
        """Return the values of the struct or union type as dict.

        Returns
        -------
        dict(str, type)
            A dictionary containing the different available values of the
            struct or union.
        """
        return struct_union_get_values_of_cls(cls, False)

    @classmethod
    def __values__(object cls):
        """Deprecated, use get_values() instead."""
        return struct_union_get_values_of_cls(cls, False)


cdef class IopStructUnionDescription:
    """Description of an IOP struct or union type.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP struct or union.
    deprecated : bool
        True if the IOP struct or union is deprecated.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP struct or union.
    fields : dict(str, IopStructUnionFieldDescription)
        The dictionary of IOP description of each fields.
    """
    cdef readonly IopHelpDescription help
    cdef readonly bool deprecated
    cdef readonly dict generic_attributes
    cdef readonly dict fields


cdef class IopStructUnionFieldDescription:
    """Description of an IOP struct or union field.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP field.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP field.
    iop_type : str
        The iop type as a string of the IOP field.
    py_type : type
        The python type of the IOP field.
    default_value : object or None
        The default value of the field if any.
    optional : bool
        True if the field is optional.
    repeated : bool
        True if the field is an array.
    private : bool
        True if the field is private.
    deprecated : bool
        True if the field is deprecated.
    min : number or None
        Minimal value allowed for the field.
        Applies only to numeric fields.
    max : number or None
        Maximal value allowed for the field.
        Applies only to numeric fields.
    min_occurs : int or None
        For repeated types, minimum number of objects allowed in the field.
        Applies only to repeated (array) types.
    max_occurs : int or None
        For repeated types, maximum number of objects allowed in the field.
        Applies only to repeated (array) types.
    min_length : int or None
        For strings and data, minimal length allowed for the string.
        Applies only to string and data types.
    max_length : int or None
        For strings and data, maximal length allowed for the string.
        Applies only to string and data types.
    length : int or None
        For strings and data, restrict the allowed lengths to that exact
        value.  Applies only to string and data types.
    cdata : bool
        For strings, when packing to XML, specifies if the the packer should
        use <!CDATA[ or XML quoting.
        Applies only to string.
    non_empty : bool
        For strings, forbids empty values.
        Applies only to string and data types.
    non_zero : bool
        Disallow the value 0 for the field.
        Applies only to numeric fields.
    pattern : str or None
        For strings, specifies a pattern the string must match.
        The pattern allows restricting the characters the string can contain.
        It uses a common regular-expression syntax.
    """
    cdef readonly IopHelpDescription help
    cdef readonly dict generic_attributes
    cdef readonly str iop_type
    cdef readonly object py_type
    cdef readonly object default_value
    cdef readonly bool optional
    cdef readonly bool repeated
    cdef readonly bool private
    cdef readonly bool deprecated
    cdef readonly object min
    cdef readonly object max
    cdef readonly object min_occurs
    cdef readonly object max_occurs
    cdef readonly object min_length
    cdef readonly object max_length
    cdef readonly object length
    cdef readonly bool cdata
    cdef readonly bool non_zero
    cdef readonly bool non_empty
    cdef readonly str pattern


# {{{ Helpers


cdef inline _InternalStructUnionType struct_union_get_iop_type_cls(
    object cls):
    """Get internal class that holds C iop struct and union desc from
    class type.

    Parameters
    ----------
    cls
        The IOPy type.

    Returns
    -------
        The internal object that holds the C iop struct and union desc.
    """
    return type(cls)


cdef inline _InternalStructUnionType struct_union_get_iop_type(
    StructUnionBase py_st):
    """Get internal class that holds C iop struct and union desc from
    object.

    Parameters
    ----------
    py_st
        The struct or union python object.

    Returns
    -------
        The internal object that holds the C iop struct and union desc.
    """
    return struct_union_get_iop_type_cls(type(py_st))


cdef inline const iop_struct_t *struct_union_get_desc(
    StructUnionBase py_st) except NULL:
    """Get C iop struct and union desc.

    Parameters
    ----------
    py_st
        The struct or union python object.

    Returns
    -------
        The C iop struct and union desc.
    """
    return struct_union_get_iop_type(py_st).desc


cdef inline Plugin struct_union_get_plugin(StructUnionBase py_st):
    """Get the IOPy plugin of the object.

    Parameters
    ----------
    py_st
        The struct or union python object.

    Returns
    -------
        The IOPy plugin of the object.
    """
    return struct_union_get_iop_type(py_st).plugin


cdef void *get_iop_field_ptr(const iop_field_t *f, void *iop_val):
    """Get ptr to the field to iop value.

    Parameters
    ----------
    f
        The field description in the IOP value.
    iop_val
        Pointer to the IOP value.

    Returns
    -------
        Pointer to the field in the IOP value.
    """
    return <char *>iop_val + f.data_offs


cdef const void *get_iop_field_const_ptr(const iop_field_t *f,
                                         const void *iop_val):
    """Get ptr to the field to const iop value.

    Parameters
    ----------
    f
        The field description in the IOP value.
    iop_val
        Pointer to the IOP value.

    Returns
    -------
        Pointer to the field in the IOP value.
    """
    return get_iop_field_ptr(f, <void *>iop_val)


cdef void add_error_field_type(const iop_field_t *field, sb_t *err):
    """Add field type description to error.

    Parameters
    ----------
    field
        The iop field.
    err
        The error description.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef iop_type_t ftype = field.type
    cdef object py_field_type
    cdef object py_field_type_name

    t_scope_ignore(t_scope_guard)

    if (ftype == IOP_T_I8
     or ftype == IOP_T_U8
     or ftype == IOP_T_I16
     or ftype == IOP_T_U16
     or ftype == IOP_T_I32
     or ftype == IOP_T_U32
     or ftype == IOP_T_I64
     or ftype == IOP_T_U64):
        py_field_type = int
    elif ftype == IOP_T_BOOL:
        py_field_type = bool
    elif ftype == IOP_T_DOUBLE:
        py_field_type = float
    elif ftype == IOP_T_XML or ftype == IOP_T_STRING:
        py_field_type = str
    elif ftype == IOP_T_DATA:
        py_field_type = bytes
    elif ftype == IOP_T_VOID:
        py_field_type = None
    elif ftype == IOP_T_ENUM:
        py_field_type = EnumBase
    elif ftype == IOP_T_UNION:
        py_field_type = UnionBase
    elif ftype == IOP_T_STRUCT:
        py_field_type = StructBase
    else:
        cassert(False)
        py_field_type = None

    if py_field_type is not None:
        py_field_type_name = py_field_type.__name__
        sb_add_lstr(err, t_py_obj_to_lstr(py_field_type_name))
    else:
        sb_adds(err, 'NoneType')

    if field.type == IOP_T_ENUM:
        sb_addf(err, " (`%*pM`)",
                LSTR_FMT_ARG(field.u1.en_desc.fullname))
    elif field.type == IOP_T_UNION or field.type == IOP_T_STRUCT:
        sb_addf(err, " (`%*pM`)",
                LSTR_FMT_ARG(field.u1.st_desc.fullname))


cdef int add_error_convert_field(const iop_field_t *field, object py_obj,
                                 sb_t *err) except -1:
    """Add error type description when trying to convert object for field.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to convert.
    err
        The error description.

    Returns
    -------
        -1 in case of unexpected python exception. 0 otherwise.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef str py_obj_type_name
    cdef lstr_t py_obj_type_name_lstr
    cdef object py_obj_repr
    cdef lstr_t py_obj_repr_lstr

    t_scope_ignore(t_scope_guard)
    py_obj_type_name = type(py_obj).__name__
    py_obj_type_name_lstr = t_py_obj_to_lstr(py_obj_type_name)
    py_obj_repr = repr(py_obj)
    py_obj_repr_lstr = t_py_obj_to_lstr(py_obj_repr)

    sb_addf(err, "invalid type: got %*pM (%*pM), expected ",
            LSTR_FMT_ARG(py_obj_type_name_lstr),
            LSTR_FMT_ARG(py_obj_repr_lstr))
    add_error_field_type(field, err)

    return 0

cdef const iop_field_t *find_field_in_st_by_name_lstr(const iop_struct_t *st,
                                                      lstr_t name,
                                                      int *field_index) nogil:
    """Find the field identified by its name as lstr in struct or union
    description.

    Parameters
    ----------
    st
        The struct or union description.
    name
        The name of the field.
    field_index : optional
        If present, it will be set with the index of the found field.
        Does not properly work for classes.

    Returns
    -------
        The field if found, NULL otherwise.
    """
    cdef const iop_struct_t *parent = get_iop_struct_parent(st)
    cdef const iop_field_t *res
    cdef int i

    if parent:
        cassert(field_index == NULL)
        res = find_field_in_st_by_name_lstr(parent, name, NULL)
        if res:
            return res

    for i in range(st.fields_len):
        if lstr_equal(st.fields[i].name, name):
            if field_index:
                field_index[0] = i
            return &st.fields[i]
    return NULL


cdef const iop_field_t *find_field_in_st_by_name(const iop_struct_t *st,
                                                 object name,
                                                 int *field_index):
    """Find the field identified by its name in struct or union description.

    Parameters
    ----------
    st
        The struct or union description.
    name
        The name of the field.
    field_index : optional
        If present, it will be set with the index of the found field.
        Does not properly work for classes.

    Returns
    -------
        The field if found, NULL otherwise.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef lstr_t name_lstr

    t_scope_ignore(t_scope_guard)
    name_lstr = t_py_obj_to_lstr(name)
    return find_field_in_st_by_name_lstr(st, name_lstr, field_index)


# }}}
# {{{ IOP C val to Python objects


cdef object iop_ptr_to_py_obj(const iop_field_t *f, const void *ptr,
                              Plugin plugin):
    """Create python object from non-repeated C iop field value.

    Parameters
    ----------
    f
        The field description.
    ptr
        Pointer to the field value to convert.
    plugin
        The IOPy plugin.

    Returns
    -------
        The python object created from the value.
    """
    cdef iop_type_t ftype = f.type
    cdef int enum_val
    cdef object py_type
    cdef lstr_t lstr_val

    if ftype == IOP_T_I8:
        return (<int8_t *>ptr)[0]
    elif ftype == IOP_T_U8:
        return (<uint8_t *>ptr)[0]
    elif ftype == IOP_T_I16:
        return (<int16_t *>ptr)[0]
    elif ftype == IOP_T_U16:
        return (<uint16_t *>ptr)[0]
    elif ftype == IOP_T_I32:
        return (<int32_t *>ptr)[0]
    elif ftype == IOP_T_U32:
        return (<uint32_t *>ptr)[0]
    elif ftype == IOP_T_I64:
        return (<int64_t *>ptr)[0]
    elif ftype == IOP_T_U64:
        return (<uint64_t *>ptr)[0]
    elif ftype == IOP_T_BOOL:
        return (<cbool *>ptr)[0]
    elif ftype == IOP_T_ENUM:
        enum_val = (<int *>ptr)[0]
        py_type = plugin_get_class_type_en(plugin, f.u1.en_desc)
        return py_type(enum_val)
    elif ftype == IOP_T_DOUBLE:
        return (<double *>ptr)[0]
    elif ftype == IOP_T_XML or ftype == IOP_T_STRING:
        lstr_val = (<lstr_t *>ptr)[0]
        return lstr_to_py_str(lstr_val)
    elif ftype == IOP_T_DATA:
        lstr_val = (<lstr_t *>ptr)[0]
        return lstr_to_py_bytes(lstr_val)
    elif ftype == IOP_T_VOID:
        return None
    elif ftype == IOP_T_UNION or ftype == IOP_T_STRUCT:
        if ((iop_field_is_reference(f) or iop_field_is_class(f))
            and f.repeat != IOP_R_OPTIONAL):
            # Non-optional class fields have to be dereferenced
            # (dereferencing of optional fields was already done by the
            #  caller).
            ptr = (<const void **>ptr)[0]

        py_type = plugin_get_class_type_st(plugin, f.u1.st_desc)
        return iop_c_val_to_py_obj(py_type, f.u1.st_desc, ptr, plugin)

    cassert (False)
    raise Error('unknown type %s for field %s'
                % (f.type, lstr_to_py_str(f.name)))


cdef object iop_field_to_py_obj(const iop_field_t *f, const void *iop_val,
                                Plugin plugin, cbool *is_set):
    """Create python object from C iop value.

    Parameters
    ----------
    f
        The field description.
    iop_val
        Pointer to the iop value to convert.
    plugin
        The IOPy plugin.
    is_set:
        Set to True when the iop value was not optional or not set. False
        otherwise.

    Returns
    -------
        The python object created from the value.
    """
    cdef const void *ptr = get_iop_field_const_ptr(f, iop_val)
    cdef const iop_array_u8_t *arr
    cdef int n
    cdef int i
    cdef const void *ptr_i
    cdef object res_i
    cdef void *mut_ptr
    cdef object res

    is_set[0] = True

    if f.repeat == IOP_R_REPEATED:
        arr = <iop_array_u8_t *>ptr
        n = arr.len

        cassert(n <= 0 or arr.tab != NULL)
        cassert(n <= 0 or f.size > 0)
        res = [None] * n
        for i in range(n):
            ptr_i = arr.tab + i * f.size
            res_i = iop_ptr_to_py_obj(f, ptr_i, plugin)
            res[i] = res_i
        return res

    if f.repeat == IOP_R_OPTIONAL:
        mut_ptr = <void *>ptr
        ptr = iop_opt_field_getv(<iop_type_t>f.type, mut_ptr)
        if not ptr:
            is_set[0] = False
            return None

    return iop_ptr_to_py_obj(f, ptr, plugin)


cdef UnionBase iop_c_union_to_py_obj(object cls, const iop_struct_t *st,
                                     const void *iop_val, Plugin plugin):
    """Create python object from C iop union value.

    Parameters
    ----------
    cls
        The IOPy enum class.
    st
        The C iop union description.
    iop_val
        Pointer to the iop union value.
    plugin
        The IOPy plugin.

    Returns
    -------
        The python object created from the union value.
    """
    cdef int val_tag
    cdef int field_index
    cdef const iop_field_t *f
    cdef cbool field_found
    cdef int i
    cdef const void *ptr
    cdef dict kwargs
    cdef UnionBase res

    val_tag = iop_union_get_tag(st, iop_val)
    if val_tag < 0:
        raise Error('unable to get tag of %s' % lstr_to_py_str(st.fullname))

    # XXX: try to get the field from the tag. When the union fields are not
    # tagged, we have field_index == field.tag - 1. This is the most common
    # case.
    field_index = val_tag - 1
    f = &(st.fields[field_index])
    field_found = f.tag == val_tag

    if unlikely(not field_found):
        for i in range(st.fields_len):
            f = &(st.fields[i])
            if val_tag == f.tag:
                field_index = i
                field_found = True
                break
        if unlikely(not field_found):
            raise Error('invalid union data type %s' %
                        lstr_to_py_str(st.fullname))

    ptr = get_iop_field_const_ptr(f, iop_val)
    kwargs = {
        lstr_to_py_str(f.name): iop_ptr_to_py_obj(f, ptr, plugin)
    }

    res = cls.__new__(cls, **kwargs)
    union_safe_init(res, field_index, kwargs)
    return res


cdef int iop_c_struct_fill_fields(const iop_struct_t *st,
                                  const void *iop_val, Plugin plugin,
                                  dict kwargs) except -1:
    """Fill kwargs dict with values of C iop struct.

    Parameters
    ----------
    st
        The C iop struct description.
    iop_val
        Pointer to the iop struct value.
    plugin
        The IOPy plugin.
    dict
        The dictionary that will be filled with converted values of the iop
        struct value.

    Returns
    -------
        -1 in case of error, 0 otherwise.
    """
    cdef int i
    cdef const iop_field_t *f
    cdef cbool is_set = False
    cdef object py_obj

    for i in range(st.fields_len):
        f = &st.fields[i]
        py_obj = iop_field_to_py_obj(f, iop_val, plugin, &is_set)
        if is_set:
            kwargs[lstr_to_py_str(f.name)] = py_obj

    return 0


cdef StructBase iop_c_struct_to_py_obj(object cls, const iop_struct_t *st,
                                       const void *iop_val, Plugin plugin):
    """Create python object from C iop struct value.

    Parameters
    ----------
    cls
        The IOPy struct class.
    st
        The C iop struct description.
    iop_val
        Pointer to the iop struct value.
    plugin
        The IOPy plugin.

    Returns
    -------
        The python object created from the struct value.
    """
    cdef dict kwargs = {}
    cdef StructBase res

    iop_c_struct_fill_fields(st, iop_val, plugin, kwargs)
    res = cls.__new__(cls, **kwargs)
    struct_safe_init(res, kwargs)
    return res

cdef StructBase iop_c_class_to_py_obj(object cls, const iop_struct_t *st,
                                      const void *iop_val, Plugin plugin):
    """Create python object from C iop class value.

    Parameters
    ----------
    cls
        The IOPy struct class.
    st
        The C iop struct description.
    iop_val
        Pointer to the iop class value.
    plugin
        The IOPy plugin.

    Returns
    -------
        The python object created from the class value.
    """
    cdef const iop_struct_t *real_st
    cdef dict kwargs
    cdef StructBase res

    real_st = (<iop_struct_t **>iop_val)[0]
    if real_st != st:
        cls = plugin_get_class_type_st(plugin, real_st)
        st = real_st

    kwargs = {}
    while True:
        iop_c_struct_fill_fields(st, iop_val, plugin, kwargs)
        st = st.class_attrs.parent
        if not st:
            break

    res = cls.__new__(cls, **kwargs)
    struct_safe_init(res, kwargs)
    return res


cdef StructUnionBase iop_c_val_to_py_obj(object cls, const iop_struct_t *st,
                                         const void *iop_val, Plugin plugin):
    """Create python object from C iop union/struct/class value.

    Parameters
    ----------
    cls
        The IOPy class.
    st
        The C iop struct description.
    iop_val
        Pointer to the iop value.
    plugin
        The IOPy plugin.

    Returns
    -------
        The python object created from the iop value.
    """
    if st.is_union:
        return iop_c_union_to_py_obj(cls, st, iop_val, plugin)
    elif iop_struct_is_class(st):
        return iop_c_class_to_py_obj(cls, st, iop_val, plugin)
    else:
        return iop_c_struct_to_py_obj(cls, st, iop_val, plugin)


# }}}
# {{{ IOP Python object to C val


cdef int raise_invalid_field_type(const iop_field_t *field,
                                  object py_obj) except -1:
    """Raise type error when field is not of appropriate type.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to process.
    """
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)

    add_error_convert_field(field, py_obj, &err)
    raise Error(lstr_to_py_str(LSTR_SB_V(&err)))


cdef int iop_value_set_optional(const iop_field_t *f, void *v,
                                cbool flag) except -1:
    """Set optional field as having a value depending on flag.

    Parameters
    ----------
    f
        The iop field.
    v
        The pointer to the iop value.
    flag
        If True, the field is considered has having a value.
    """
    cdef iop_type_t ftype = f.type

    if ftype == IOP_T_I8:
        (<opt_i8_t *>v).has_field = flag
    elif ftype == IOP_T_U8:
        (<opt_u8_t *>v).has_field = flag
    elif ftype == IOP_T_I16:
        (<opt_i16_t *>v).has_field = flag
    elif ftype == IOP_T_U16:
        (<opt_u16_t *>v).has_field = flag
    elif ftype == IOP_T_I32:
        (<opt_i32_t *>v).has_field = flag
    elif ftype == IOP_T_U32:
        (<opt_u32_t *>v).has_field = flag
    elif ftype == IOP_T_I64:
        (<opt_i64_t *>v).has_field = flag
    elif ftype == IOP_T_U64:
        (<opt_u64_t *>v).has_field = flag
    elif ftype == IOP_T_BOOL:
        (<opt_bool_t *>v).has_field = flag
    elif ftype == IOP_T_ENUM:
        (<opt_enum_t *>v).has_field = flag
    elif ftype == IOP_T_DOUBLE:
        (<opt_double_t *>v).has_field = flag
    elif ftype == IOP_T_VOID:
        (<cbool *>v)[0] = flag
    elif ftype == IOP_T_STRING or ftype == IOP_T_XML or ftype == IOP_T_DATA:
        p_clear(<lstr_t *>v, 1)
    elif ftype == IOP_T_UNION or ftype == IOP_T_STRUCT:
        (<void **>v)[0] = NULL
    else:
        cassert(False)
        raise NotImplementedError()
    return 0


cdef int64_t py_obj_to_int64_without_exc(object py_obj):
    """Convert python object to int64_t without checking for exception.

    Parameters
    ----------
    py_obj
        The python object to convert.

    Returns
    -------
        The converted value or -1 if cannot be converted without an exception.
    """
    cdef int64_t res = -1

    try:
        res = py_obj
    except:
        pass
    return res


cdef uint64_t py_obj_to_uint64_without_exc(object py_obj):
    """Convert python object to uint64_t without checking for exception.

    Parameters
    ----------
    py_obj
        The python object to convert.

    Returns
    -------
        The converted value or -1 if cannot be converted without an exception.
    """
    cdef uint64_t res = -1

    try:
        res = py_obj
    except:
        pass
    return res


cdef int mp_iop_py_obj_field_to_c_val(mem_pool_t *mp, cbool force_str_dup,
                                      const iop_field_t *field, object py_obj,
                                      Plugin plugin, void *res) except -1:
    """Create C iop value from a python field object.

    Parameters
    ----------
    mp
        The memory pool used to create objects.
    force_str_dup
        Force duplication of string instead of using object's internal buffer.
    field
        The IOP field to set.
    py_obj
        The python field object.
    plugin
        The IOPy plugin.
    res
        The allocated C field value.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    cdef iop_type_t ftype = field.type
    cdef str py_str
    cdef lstr_t lstr
    cdef bytes py_bytes
    cdef EnumBase py_enum
    cdef object py_enum_cls
    cdef UnionBase py_union
    cdef StructBase py_struct
    cdef const iop_enum_t *en
    cdef const iop_struct_t *st
    cdef const iop_struct_t *real_st

    if iop_field_is_reference(field):
        (<void **>res)[0] = mp_imalloc(mp, field.size, 8, MEM_RAW)
        res = (<void **>res)[0]

    if ftype == IOP_T_I8:
        (<int8_t *>res)[0] = py_obj_to_int64_without_exc(py_obj)

    elif ftype == IOP_T_U8:
        (<uint8_t *>res)[0] = py_obj_to_uint64_without_exc(py_obj)

    elif ftype == IOP_T_I16:
        (<int16_t *>res)[0] = py_obj_to_int64_without_exc(py_obj)

    elif ftype == IOP_T_U16:
        (<uint16_t *>res)[0] = py_obj_to_uint64_without_exc(py_obj)

    elif ftype == IOP_T_I32:
        (<int32_t *>res)[0] = py_obj_to_int64_without_exc(py_obj)

    elif ftype == IOP_T_U32:
        (<uint32_t *>res)[0] = py_obj_to_uint64_without_exc(py_obj)

    elif ftype == IOP_T_I64:
        (<int64_t *>res)[0] = py_obj_to_int64_without_exc(py_obj)

    elif ftype == IOP_T_U64:
        (<uint64_t *>res)[0] = py_obj_to_uint64_without_exc(py_obj)

    elif ftype == IOP_T_BOOL:
        (<cbool *>res)[0] = py_obj

    elif ftype == IOP_T_DOUBLE:
        (<double *>res)[0] = py_obj

    elif ftype == IOP_T_XML or ftype == IOP_T_STRING:
        py_str = py_obj
        lstr = py_str_to_lstr(py_str)
        if force_str_dup:
            lstr = mp_lstr_dup(mp, lstr)
        (<lstr_t *>res)[0] = lstr

    elif ftype == IOP_T_DATA:
        py_bytes = py_obj
        lstr = py_bytes_to_lstr(py_bytes)
        if force_str_dup:
            lstr = mp_lstr_dup(mp, lstr)
        (<lstr_t *>res)[0] = lstr

    elif ftype == IOP_T_ENUM:
        en = field.u1.en_desc

        if likely(isinstance(py_obj, EnumBase)):
            py_enum = <EnumBase>py_obj
            if unlikely(enum_get_desc(py_enum) != en):
                raise_invalid_field_type(field, py_obj)
        else:
            py_enum_cls = plugin_get_class_type_en(plugin, en)
            py_enum = py_enum_cls(py_obj)

        (<int *>res)[0] = py_enum.val

    elif ftype == IOP_T_UNION:
        py_union = py_obj
        st = field.u1.st_desc
        if unlikely(struct_union_get_desc(py_union) != st):
            raise_invalid_field_type(field, py_union)
        mp_iop_py_union_to_c_val(mp, force_str_dup, py_union, st, plugin,
                                 res)

    elif ftype == IOP_T_STRUCT:
        py_struct = py_obj
        st = field.u1.st_desc
        real_st = struct_union_get_desc(py_struct)
        if iop_struct_is_class(st):
            if unlikely(not iop_struct_is_class(real_st)
                     or not iop_class_is_a(real_st, st)):
                raise_invalid_field_type(field, py_obj)
            (<void **>res)[0] = mp_imalloc(mp, real_st.size, 8, MEM_RAW)
            res = (<void **>res)[0]
            mp_iop_py_class_to_c_val(mp, force_str_dup, py_struct, real_st,
                                     plugin, res)
        else:
            if unlikely(real_st != st):
                raise_invalid_field_type(field, py_struct)
            mp_iop_py_struct_to_c_val(mp, force_str_dup, py_struct, st,
                                      plugin, res)

    elif ftype == IOP_T_VOID:
        pass

    else:
        cassert(False)
        raise NotImplementedError()

    return 0


cdef int mp_iop_py_union_to_c_val(mem_pool_t *mp, cbool force_str_dup,
                                  UnionBase py_obj, const iop_struct_t *st,
                                  Plugin plugin, void *res) except -1:
    """Create C iop value from a python union object.

    Parameters
    ----------
    mp
        The memory pool used to create objects.
    force_str_dup
        Force duplication of string instead of using object's internal buffer.
    py_obj
        The python union object.
    st
        The C iop union description of the python object.
    plugin
        The IOPy plugin.
    res
        The allocated C union value.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    cdef const iop_field_t *field
    cdef void *field_res
    cdef object field_obj

    cassert(st.is_union)

    field = &st.fields[py_obj.field_index]
    cassert(field.repeat == IOP_R_REQUIRED)

    field_res = get_iop_field_ptr(field, res)
    field_obj = getattr(py_obj, lstr_to_py_str(field.name))

    iop_union_set_tag(st, field.tag, res)
    mp_iop_py_obj_field_to_c_val(mp, force_str_dup, field, field_obj,
                                 plugin, field_res)
    return 0


cdef int mp_iop_repeat_py_field_to_cval(mem_pool_t *mp,
                                        const iop_struct_t *st, Plugin plugin,
                                        const iop_field_t *field,
                                        object field_obj,
                                        void *field_res) except -1:
    """Fill C array of repeated field from python field list object.

    Parameters
    ----------
    mp
        The memory pool used to create objects.
    st
        The C iop struct description of the python object.
    plugin
        The IOPy plugin.
    field
        The field to set.
    field_obj
        The python list field object.
    field_res
        The C iop array to fill.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef list field_list
    cdef iop_array_u8_t *array
    cdef int list_len
    cdef int i
    cdef cbool is_valid
    cdef cbool is_explicit = False
    cdef object array_obj
    cdef void *array_res

    field_list = field_obj
    list_len = len(field_list)
    array = <iop_array_u8_t *>field_res
    p_clear(array, 1)
    if list_len == 0:
        return 0

    array.len = list_len
    array.tab = <uint8_t *>mp_imalloc(mp, field.size * list_len, 8, MEM_RAW)
    for i in range(list_len):
        # Since a Python list can contain anything, and we cannot actually
        # control the types of element that are being added to the list,
        # we need to convert the list item to field type before convert it to
        # C value.
        is_valid = False
        array_obj = convert_field_object(field, field_list[i], plugin,
                                         &is_valid, &is_explicit, &err)
        if unlikely(not is_valid):
            raise Error("error when parsing %s: invalid selected struct "
                        "field `%s` for element %d/%d: %s" %
                         (lstr_to_py_str(st.fullname),
                          lstr_to_py_str(field.name), i + 1, list_len,
                          lstr_to_py_str(LSTR_SB_V(&err))))

        array_res = &array.tab[i * field.size]
        mp_iop_py_obj_field_to_c_val(mp, True, field, array_obj, plugin,
                                     array_res)

    return 0


cdef int mp_iop_py_struct_to_c_val(mem_pool_t *mp, cbool force_str_dup,
                                   StructBase py_obj, const iop_struct_t *st,
                                   Plugin plugin, void *res) except -1:
    """Create C iop value from a python struct object.

    Parameters
    ----------
    mp
        The memory pool used to create objects.
    force_str_dup
        Force duplication of string instead of using object's internal buffer.
    py_obj
        The python struct object.
    st
        The C iop struct description of the python object.
    plugin
        The IOPy plugin.
    res
        The allocated C struct value.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    cdef const iop_field_t *field
    cdef str field_name
    cdef object field_obj
    cdef void *field_res
    cdef int i

    for i in range(st.fields_len):
        field = &st.fields[i]
        field_name = lstr_to_py_str(field.name)
        field_res = get_iop_field_ptr(field, res)

        if field.type == IOP_T_VOID:
            if field.repeat == IOP_R_OPTIONAL:
                iop_value_set_optional(field, field_res,
                                       hasattr(py_obj, field_name))
            continue

        field_obj = getattr(py_obj, field_name, None)
        if field_obj is None:
            if field.repeat != IOP_R_OPTIONAL:
                raise Error('field %s is not present but required' %
                            field_name)
            iop_value_set_optional(field, field_res, False)
            continue

        if field.repeat == IOP_R_REPEATED:
            mp_iop_repeat_py_field_to_cval(mp, st, plugin, field, field_obj,
                                           field_res)
            continue

        if field.repeat == IOP_R_OPTIONAL:
            iop_value_set_optional(field, field_res, True)
            if (field.type == IOP_T_UNION
             or (field.type == IOP_T_STRUCT
                 and not iop_field_is_class(field))):
                (<void **>field_res)[0] = mp_imalloc(mp, field.size, 8,
                                                     MEM_RAW)
                field_res = (<void **>field_res)[0]

        mp_iop_py_obj_field_to_c_val(mp, force_str_dup, field, field_obj,
                                     plugin, field_res)
    return 0


cdef int mp_iop_py_class_to_c_val(mem_pool_t *mp, cbool force_str_dup,
                                  StructBase py_obj, const iop_struct_t *st,
                                  Plugin plugin, void *res) except -1:
    """Create C iop value from a python class object.

    Parameters
    ----------
    mp
        The memory pool used to create objects.
    force_str_dup
        Force duplication of string instead of using object's internal buffer.
    py_obj
        The python class object.
    st
        The C iop class description of the python object.
    plugin
        The IOPy plugin.
    res
        The allocated C class value.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    # Set the __vptr pointer
    (<const iop_struct_t **>res)[0] = st

    while st:
        mp_iop_py_struct_to_c_val(mp, force_str_dup, py_obj, st, plugin, res)
        st = st.class_attrs.parent
    return 0


cdef int mp_iop_py_obj_to_c_val(mem_pool_t *mp, cbool force_str_dup,
                                StructUnionBase py_obj, void **res) except -1:
    """Create C iop value from a python union/struct/class object.

    Parameters
    ----------
    mp
        The memory pool used to create objects. It must be a frame based
        memory pool.
    force_str_dup
        Force duplication of string instead of using object's internal buffer.
    py_obj
        The python object.
    res
        The pointer where to reallocate C value.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    cdef _InternalStructUnionType iop_type = struct_union_get_iop_type(py_obj)
    cdef const iop_struct_t *st = iop_type.desc
    cdef Plugin plugin = iop_type.plugin

    cassert(mp.mem_pool & MEM_BY_FRAME)

    res[0] = mp_irealloc(mp, res[0], 0, st.size, 8, MEM_RAW)

    if st.is_union:
        mp_iop_py_union_to_c_val(mp, force_str_dup, py_obj, st, plugin,
                                 res[0])
    elif iop_struct_is_class(st):
        mp_iop_py_class_to_c_val(mp, force_str_dup, py_obj, st, plugin,
                                 res[0])
    else:
        mp_iop_py_struct_to_c_val(mp, force_str_dup, py_obj, st, plugin,
                                  res[0])

    return 0


# }}}
# {{{ Parse special kwargs _json, _yaml, _bin, _xml, _hex


cdef enum IopySpecialKwargsType:
    IOPY_SPECIAL_KWARGS_JSON
    IOPY_SPECIAL_KWARGS_YAML
    IOPY_SPECIAL_KWARGS_BIN
    IOPY_SPECIAL_KWARGS_XML
    IOPY_SPECIAL_KWARGS_HEX


cdef object get_special_kwargs(dict kwargs, IopySpecialKwargsType *val_type,
                               cbool *single):
    """Extract special constructor argument from kwargs.

    Parameters
    ----------
    kwargs
        The dict of arguments from the constructor.
    val_type
        If a special constructor is found, this will be set to the
        corresponding type.
    single : optional
        If not NULL, we will parse the kwargs to find the `single` argument
        and set it to the found value. If not found, it is set to True.

    Returns
    -------
        None if no special arguments have been found. Otherwise it will return
        the value of the argument.
    """
    cdef int nb_exp_kwargs = 1
    cdef object key = None
    cdef object res = None

    if single:
        single[0] = True

    # Get first argument unless it is single
    for key, res in kwargs.iteritems():
        if single != NULL and key == 'single':
            single[0] = res
            nb_exp_kwargs += 1
            continue
        break

    if key is None or len(kwargs) != nb_exp_kwargs:
        # Fast path for zero or multiple args
        return None

    if key == '_json':
        val_type[0] = IOPY_SPECIAL_KWARGS_JSON
    elif key == '_yaml':
        val_type[0] = IOPY_SPECIAL_KWARGS_YAML
    elif key == '_bin':
        val_type[0] = IOPY_SPECIAL_KWARGS_BIN
    elif key == '_xml':
        val_type[0] = IOPY_SPECIAL_KWARGS_XML
    elif key == '_hex':
        val_type[0] = IOPY_SPECIAL_KWARGS_HEX
    else:
        return None

    if not isinstance(res, (bytes, str)):
        raise Error('invalid type for parameter %s: expected string' % key)

    return res


cdef void *t_parse_lstr_json(const iop_struct_t *st, lstr_t val) except NULL:
    """Unpack the string value as json for the given structure.

    Parameters
    ----------
    st
        The C iop struct description.
    val
        The json value.

    Returns
    -------
        The unpacked iop struct value or NULL in case of exception.
    """
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef pstream_t ps
    cdef int ret_code
    cdef void *res = NULL

    with nogil:
        ps = ps_initlstr(&val)
        ret_code = t_iop_junpack_ptr_ps(&ps, st, &res, 0, &err)

    if ret_code < 0:
        raise Error('cannot parse string: %s (when trying to unpack an %s)' %
                    (lstr_to_py_str(LSTR_SB_V(&err)),
                     lstr_to_py_str(st.fullname)))
    return res


cdef void *t_parse_lstr_yaml(const iop_struct_t *st, lstr_t val) except NULL:
    """Unpack the string value as json for the given structure.

    Parameters
    ----------
    st
        The C iop struct description.
    val
        The json value.

    Returns
    -------
        The unpacked iop struct value or NULL in case of exception.
    """
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef pstream_t ps
    cdef int ret_code
    cdef void *res = NULL

    with nogil:
        ps = ps_initlstr(&val)
        ret_code = t_iop_yunpack_ptr_ps(&ps, st, &res, 0, NULL, &err)

    if ret_code < 0:
        raise Error('%s' % (lstr_to_py_str(LSTR_SB_V(&err))))
    return res


cdef void *t_parse_lstr_bin(const iop_struct_t *st, lstr_t val) except NULL:
    """Unpack the string value as binary for the given structure.

    Parameters
    ----------
    st
        The C iop struct description.
    val
        The binary value.

    Returns
    -------
        The unpacked iop struct value or NULL in case of exception.
    """
    cdef pstream_t ps
    cdef int ret_code
    cdef void *res = NULL

    with nogil:
        ps = ps_initlstr(&val)
        ret_code = iop_bunpack_ptr(t_pool(), st, &res, ps, False)

    if ret_code < 0:
        raise Error('cannot decode string as an %s bin packed stream' %
                    lstr_to_py_str(st.fullname))
    return res


cdef inline int xmlr_check(const iop_struct_t *st, int val) except -1:
    """Check that the return value of xmlr and raise exception if needed.

    Parameters
    ----------
    st
        The C iop struct description, used for error.
    val
        The xmlr function return value.

    Returns
    -------
        -1 in case of error, val otherwise.
    """
    cdef const char *xml_err

    if val < 0:
        xml_err = xmlr_get_err()
        if not xml_err:
            xml_err = 'parse error'
        raise Error('cannot parse xml string: %s (when trying to unpack an '
                    '%s)' % (xml_err, lstr_to_py_str(st.fullname)))
    return val


cdef void *t_parse_lstr_xml(const iop_struct_t *st, lstr_t val) except NULL:
    """Unpack the string value as xml for the given structure.

    Parameters
    ----------
    st
        The C iop struct description.
    val
        The xml value.

    Returns
    -------
        The unpacked iop struct value or NULL in case of exception.
    """
    cdef cbool soap = False
    cdef cbool exn = False
    cdef void* res = NULL

    xmlr_setup(&xmlr_g, val.s, val.len)
    if xmlr_check(st, xmlr_node_try_open_s(xmlr_g, 'Envelope')):
        if xmlr_check(st, xmlr_node_is_s(xmlr_g, "Header")):
            xmlr_check(st, xmlr_next_sibling(xmlr_g))

        xmlr_check(st, xmlr_node_open_s(xmlr_g, "Body"))

        if xmlr_check(st, xmlr_node_try_open_s(xmlr_g, "Fault")):
            if xmlr_check(st, xmlr_node_is_s(xmlr_g, "faultcode")):
                xmlr_check(st, xmlr_next_sibling(xmlr_g))

            if xmlr_check(st, xmlr_node_is_s(xmlr_g, "faultstring")):
                xmlr_check(st, xmlr_next_sibling(xmlr_g))

            xmlr_check(st, xmlr_node_open_s(xmlr_g, "detail"))
            exn = True

        soap = True

    xmlr_check(st, t_iop_xunpack_ptr(xmlr_g, st, &res))

    if soap:
        if exn:
            xmlr_check(st, xmlr_node_close(xmlr_g))
            xmlr_check(st, xmlr_node_close(xmlr_g))
        xmlr_check(st, xmlr_node_close(xmlr_g))
        xmlr_check(st, xmlr_node_close(xmlr_g))

    xmlr_close(&xmlr_g)

    return res


cdef void *t_parse_lstr_hex(const iop_struct_t *st, lstr_t val) except NULL:
    """Unpack the string value as hex for the given structure.

    Parameters
    ----------
    st
        The C iop struct description.
    val
        The hex value.

    Returns
    -------
        The unpacked iop struct value or NULL in case of exception.
    """
    cdef int l
    cdef uint8_t *raw
    cdef pstream_t ps
    cdef int ret_code = -1
    cdef void *res = NULL

    with nogil:
        l = val.len // 2 + 1
        raw = t_new_u8(l)
        l = strconv_hexdecode(raw, l, val.s, val.len)
        if l >= 0:
            ps = ps_init(raw, l)
            ret_code = iop_bunpack_ptr(t_pool(), st, &res, ps, False)

    if ret_code < 0:
        raise Error('cannot decode string as an %s hex packed stream' %
                    lstr_to_py_str(st.fullname))
    return res


cdef StructUnionBase parse_special_val_lstr(object cls,
                                            const iop_struct_t *st,
                                            Plugin plugin,
                                            IopySpecialKwargsType val_type,
                                            lstr_t val_lstr):
    """Unpack the python value as lstr from special kwargs constructor.

    Parameters
    ----------
    cls
        The IOPy class.
    st
        The IOP struct or union description.
    plugin
        The IOPy plugin.
    val_type
        The type of the special kwargs constructor.
    val_lstr
        The value of the special kwargs constructor as lstr_t.

    Returns
    -------
        The unpacked python object.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef void *iop_val = NULL

    t_scope_ignore(t_scope_guard)

    if val_type == IOPY_SPECIAL_KWARGS_JSON:
        iop_val = t_parse_lstr_json(st, val_lstr)
    elif val_type == IOPY_SPECIAL_KWARGS_YAML:
        iop_val = t_parse_lstr_yaml(st, val_lstr)
    elif val_type == IOPY_SPECIAL_KWARGS_BIN:
        iop_val = t_parse_lstr_bin(st, val_lstr)
    elif val_type == IOPY_SPECIAL_KWARGS_XML:
        iop_val = t_parse_lstr_xml(st, val_lstr)
    elif val_type == IOPY_SPECIAL_KWARGS_HEX:
        iop_val = t_parse_lstr_hex(st, val_lstr)
    else:
        cassert(False)
        raise Error('invalid value type %d' % val_type)

    return iop_c_val_to_py_obj(cls, st, iop_val, plugin)


cdef StructUnionBase parse_special_val(object cls, const iop_struct_t *st,
                                       Plugin plugin,
                                       IopySpecialKwargsType val_type,
                                       object val):
    """Unpack the python value from special kwargs constructor.

    Parameters
    ----------
    cls
        The IOPy class.
    st
        The IOP struct or union description.
    plugin
        The IOPy plugin.
    val_type
        The type of the special kwargs constructor.
    val
        The value of the special kwargs constructor.

    Returns
    -------
        The unpacked python object.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef lstr_t val_lstr

    # Required to ignore cython error about unused entry t_scope_guard
    t_scope_ignore(t_scope_guard)

    val_lstr = t_py_obj_to_lstr(val)
    return parse_special_val_lstr(cls, st, plugin, val_type, val_lstr)


cdef StructUnionBase parse_special_kwargs(object cls, dict kwargs):
    """Try to unpack the python value from a potential special ctor kwargs.

    Parameters
    ----------
    cls
        The IOPy class.
    kwargs
        The kwargs passed to the constructor.

    Returns
    -------
        None if no special arguments have been found. Otherwise the unpacked
        python object otherwise.
    """
    cdef IopySpecialKwargsType val_type = IOPY_SPECIAL_KWARGS_JSON
    cdef object val
    cdef _InternalStructUnionType iop_type

    val = get_special_kwargs(kwargs, &val_type, NULL)
    if val is None:
        return None

    iop_type = struct_union_get_iop_type_cls(cls)
    return parse_special_val(cls, iop_type.desc, iop_type.plugin, val_type,
                             val)


cdef list parse_lstr_list_bin_to_py_obj(object cls, const iop_struct_t *st,
                                        Plugin plugin, lstr_t val_lstr):
    """Unpack the list of union as binary.

    Parameters
    ----------
    cls
        The IOPy class.
    st
        The IOP struct or union description.
    plugin
        The IOPy plugin.
    val
        The value of the special kwargs constructor.

    Returns
    -------
        The unpacked iop struct value or NULL in case of exception.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef pstream_t ps
    cdef void *data
    cdef int ret_code
    cdef object py_item
    cdef list res

    t_scope_ignore(t_scope_guard)

    data = t_new_u8(ROUND_UP(st.size, 8))
    res = []
    ps = ps_initlstr(&val_lstr)
    while not ps_done(&ps):
        with nogil:
            ret_code = iop_bunpack_multi(t_pool(), st, data, &ps, False)

        if ret_code < 0:
            raise Error('cannot decode string as an %s bin packed stream' %
                        lstr_to_py_str(st.fullname))

        py_item = iop_c_val_to_py_obj(cls, st, data, plugin)
        res.append(py_item)
    return res


cdef StructUnionBase unpack_file_to_py_obj(object cls, const iop_struct_t *st,
                                           Plugin plugin, object filename,
                                           IopySpecialKwargsType file_type):
    """Unpack json or yaml file to python object.

    Parameters
    ----------
    cls
        The IOPy class.
    st
        The IOP struct or union description.
    plugin
        The IOPy plugin.
    filename
        The name of the file to unpack.

    Returns
    -------
        The unpacked object.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef lstr_t filename_lstr
    cdef void *data
    cdef int ret_code

    t_scope_ignore(t_scope_guard)

    filename_lstr = t_py_obj_to_lstr(filename)
    with nogil:
        data = NULL
        if file_type == IOPY_SPECIAL_KWARGS_JSON:
            ret_code = t_iop_junpack_ptr_file(filename_lstr.s, st, &data, 0,
                                              NULL, &err)
        else:
            cassert(file_type == IOPY_SPECIAL_KWARGS_YAML)
            ret_code = t_iop_yunpack_ptr_file(filename_lstr.s, st, &data,
                                              0, NULL, &err)
    if ret_code < 0:
        raise Error('cannot unpack input file %s: %s' %
                    (filename, lstr_to_py_str(LSTR_SB_V(&err))))
    return iop_c_val_to_py_obj(cls, st, data, plugin)


cdef object unpack_file_from_args_to_py_obj(object cls, dict kwargs):
    """Retrieve the type and name of file from kwargs and unpack the file.

    Parameters
    ----------
    cls
        The IOPy class type of expected objects.
    kwargs
        The named arguments where to retrieve the type and name of file.
        See StructUnionBase::from_file for more explanation.

    Returns
    -------
        The unpacked python object or list of python objects for multiple
        binary unions.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef IopySpecialKwargsType file_type = IOPY_SPECIAL_KWARGS_JSON
    cdef cbool single = False
    cdef object filename
    cdef lstr_t filename_lstr
    cdef _InternalStructUnionType iop_type
    cdef const iop_struct_t *st
    cdef Plugin plugin
    cdef lstr_t file_content
    cdef const char *err_desc

    t_scope_ignore(t_scope_guard)

    filename = get_special_kwargs(kwargs, &file_type, &single)
    if filename is None:
        raise Error('missing keyword argument; '
                    'one of _xml, _json, _yaml, _hex, _bin')

    iop_type = struct_union_get_iop_type_cls(cls)
    st = iop_type.desc
    plugin = iop_type.plugin

    if (file_type == IOPY_SPECIAL_KWARGS_JSON or
        file_type == IOPY_SPECIAL_KWARGS_YAML):
        return unpack_file_to_py_obj(cls, st, plugin, filename, file_type)

    filename_lstr = t_py_obj_to_lstr(filename)
    if lstr_init_from_file(&file_content, filename_lstr.s, PROT_READ,
                           MAP_SHARED) < 0:
        err_desc = strerror(errno)
        raise Error('could not read input file %s: %s' % (filename, err_desc))

    try:
        if file_type == IOPY_SPECIAL_KWARGS_BIN and single and st.is_union:
            return parse_lstr_list_bin_to_py_obj(cls, st, plugin,
                                                 file_content)
        else:
            return parse_special_val_lstr(cls, st, plugin, file_type,
                                          file_content)
    finally:
        lstr_wipe(&file_content)


# }}}
# {{{ Prepare arguments


cdef dict struct_union_parse_dict_args(tuple args, dict kwargs):
    """Parse dict argument from args and put it in kwargs.

    Parameters
    ----------
    args
        The args containing the dict argument.
    kwargs
        The original kwargs.

    Returns
    -------
        The new kwargs to use, or None if args does not contains a single
        dict argument or kwargs is not empty.
    """
    cdef object first_arg

    if len(args) != 1 or len(kwargs) > 0:
        return None

    first_arg = args[0]

    if not isinstance(first_arg, dict):
        return None

    return <dict>first_arg


cdef StructUnionBase struct_union_create_obj(object cls, tuple args,
                                             dict kwargs):
    cdef object obj

    obj = cls.__new__(cls, *args, **kwargs)
    obj.__init__(*args, **kwargs)
    return obj


cdef StructUnionBase struct_union_get_cls_and_create_obj(
    object cls, tuple args, dict kwargs):
    """Get the real class of the object and create it from the args and
    kwargs.

    Parameters
    ----------
    cls
        The parent IOPy class.
    args
        The positional arguments.
    kwargs
        The keyword arguments.

    Returns
    -------
    object
        The real IOPy class to use to instantiate the object.
    """
    cdef object real_cls_name
    cdef _InternalStructUnionType iop_type
    cdef const iop_struct_t *st
    cdef Plugin plugin
    cdef _InternalTypeClasses type_classes
    cdef object real_cls

    real_cls_name = kwargs.get('_class', None)
    if real_cls_name is None:
        return struct_union_create_obj(cls, args, kwargs)

    kwargs = kwargs.copy()
    del kwargs['_class']

    iop_type = struct_union_get_iop_type_cls(cls)
    st = iop_type.desc

    if st.is_union or not iop_struct_is_class(st):
        raise TypeError('IOPy type `%s` is not a class' % cls.__name__)

    plugin = iop_type.plugin
    type_classes = plugin_get_type_classes(plugin, real_cls_name)
    if type_classes is None:
        raise TypeError('unknown IOPy type `%s`' % real_cls_name)

    real_cls = type_classes.public_cls
    if not issubclass(real_cls, cls):
        raise TypeError('IOPy type `%s` is not a child type of IOPy type `%s`'
                       % (real_cls.__name__, cls.__name__))
    return struct_union_create_obj(real_cls, args, kwargs)


# }}}
# {{{ Validate and convert arguments

cdef cbool check_exact_object_field_type(const iop_field_t *field,
                                         object py_obj):
    """Check that python object has the exact type for the iop field.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to test.

    Returns
    -------
        True if the python object has the type expected for the iop type.
        False otherwise.
    """
    cdef iop_type_t iop_type = field.type
    cdef EnumBase py_en
    cdef StructUnionBase py_st
    cdef const iop_struct_t *iop_st

    if (iop_type == IOP_T_I8
     or iop_type == IOP_T_U8
     or iop_type == IOP_T_I16
     or iop_type == IOP_T_U16
     or iop_type == IOP_T_I32
     or iop_type == IOP_T_U32
     or iop_type == IOP_T_I64
     or iop_type == IOP_T_U64):
        return isinstance(py_obj, int)
    elif iop_type == IOP_T_BOOL:
        return isinstance(py_obj, bool)
    elif iop_type == IOP_T_DOUBLE:
        return isinstance(py_obj, float)
    elif iop_type == IOP_T_XML or iop_type == IOP_T_STRING:
        return isinstance(py_obj, str)
    elif iop_type == IOP_T_DATA:
        return isinstance(py_obj, bytes)
    elif iop_type == IOP_T_VOID:
        return py_obj is None
    elif iop_type == IOP_T_ENUM:
        if not isinstance(py_obj, EnumBase):
            return False
        py_en = <EnumBase>py_obj
        return enum_get_desc(py_en) == field.u1.en_desc
    elif iop_type == IOP_T_UNION:
        if not isinstance(py_obj, UnionBase):
            return False
        py_st = <StructUnionBase>py_obj
        iop_st = struct_union_get_desc(py_st)
        return iop_st == field.u1.st_desc
    elif iop_type == IOP_T_STRUCT:
        if not isinstance(py_obj, StructBase):
            return False
        py_st = <StructUnionBase>py_obj
        iop_st = struct_union_get_desc(py_st)
        return iop_struct_is_same_or_child_of(iop_st, field.u1.st_desc)

    cassert(False)
    return False


cdef object implicit_convert_field(const iop_field_t *field, object py_obj,
                                   Plugin plugin, cbool *is_converted):
    """Try to implicitly convert python object for the iop field.

    The conversion is implicit if the python object can be used as a
    positional argument to instantiate a union by guessing its type.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to convert
    plugin
        The IOPy plugin to use to create new objects when needed.
    is_converted
        Set to true if the python object has been converted successfully.
        False otherwise.

    Returns
    -------
        The converted python object or None if the object cannot be converted.
    """
    cdef iop_type_t iop_type = field.type
    cdef object py_type
    cdef const iop_enum_t *field_en
    cdef const iop_enum_t *py_obj_en
    cdef const iop_struct_t *field_st
    cdef const iop_struct_t *py_obj_st

    is_converted[0] = False

    if iop_type == IOP_T_XML or iop_type == IOP_T_STRING:
        if isinstance(py_obj, bytes):
            py_obj = py_bytes_to_py_str(<bytes>py_obj)
            is_converted[0] = True
            return py_obj

    elif iop_type == IOP_T_DATA:
        # Convert str to bytes.
        if isinstance(py_obj, str):
            py_obj = py_str_to_py_bytes(<str>py_obj)
            is_converted[0] = True
            return py_obj

    elif iop_type == IOP_T_ENUM:
        field_en = field.u1.en_desc

        # XXX: The cast from int or string to enum is done only when
        # converting to iop because some uses cases (igloo ag tests) need for
        # struct fields of enum type to be strings.
        py_type = plugin_get_class_type_en(plugin, field_en)
        try:
            py_type(py_obj)
        except:
            pass
        else:
            is_converted[0] = True
            return py_obj

        # In order to keep the compatibility with IOPyV1 we need to handle the
        # case where giving just the type is equivalent as giving an empty
        # object.
        if isinstance(py_obj, type) and issubclass(py_obj, EnumBase):
            py_obj_en = enum_get_desc_cls(py_obj)
            if py_obj_en == field_en:
                is_converted[0] = True
                return py_obj()

    elif iop_type == IOP_T_STRUCT or iop_type == IOP_T_UNION:
        # In order to keep the compatibility with IOPyV1 we need to handle the
        # case where giving just the type is equivalent as giving an empty
        # object.
        if isinstance(py_obj, type) and issubclass(py_obj, StructUnionBase):
            field_st = field.u1.st_desc
            py_obj_st = struct_union_get_iop_type_cls(py_obj).desc

            if iop_struct_is_same_or_child_of(py_obj_st, field_st):
                is_converted[0] = True
                return py_obj()

    return None


cdef object exact_or_implicit_convert_field(const iop_field_t *field,
                                            object py_obj, Plugin plugin,
                                            cbool *is_valid):
    """Check type is exact or implicitly convert python object for the iop
    field.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to convert
    plugin
        The IOPy plugin to use to create new objects when needed.
    is_valid
        Set to true if the python object is of exact type or has been
        converted successfully. False otherwise.

    Returns
    -------
        The given python object if the type is exactly the one expected,
        the converted python object in case of implicit conversion,
        or None is the object is not valid.
    """
    if check_exact_object_field_type(field, py_obj):
        is_valid[0] = True
        return py_obj

    return implicit_convert_field(field, py_obj, plugin, is_valid)


cdef object find_union_unambiguous_field(const iop_struct_t *st,
                                         object py_obj, Plugin plugin,
                                         int *field_index):
    """Find unambiguous field of union from python object.

    Parameters
    ----------
    st
        The union IOP description.
    py_obj
        The python object to set to the union.
    plugin
        The IOPy plugin to use to create new objects when needed.
    field_index
        The result field index. It will be set to -1 if the field cannot be
        found or is ambiguous.

    Returns
    -------
        The converted object to set to the union the found field, or None if
        no fields have been found.
    """
    cdef const iop_field_t *field
    cdef cbool is_valid
    cdef int i
    cdef object res = None
    cdef object field_res = None

    field_index[0] = -1

    for i in range(st.fields_len):
        field = &st.fields[i]
        is_valid = False
        field_res = exact_or_implicit_convert_field(field, py_obj, plugin,
                                                    &is_valid)
        if is_valid:
            if field_index[0] >= 0:
                # Ambiguous type
                field_index[0] = -1
                return None
            field_index[0] = i
            res = field_res
    return res


cdef object explicit_convert_field(const iop_field_t *field, object py_obj,
                                   Plugin plugin, cbool *is_valid, sb_t *err):
    """Try to explicitly convert python object for the iop field.

    The conversion is explicit if the python object can be only used as a
    named argument to instantiate a union or a struct.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to convert.
    plugin
        The IOPy plugin to use to create new objects when needed.
    is_valid
        Set to true if the python object has been converted successfully.
        False otherwise.
    err
        The error description if the python object is not valid.

    Returns
    -------
        The converted python object in case of explicit conversion,
        or None is the object is not valid.
    """
    cdef iop_type_t iop_type = field.type
    cdef object py_type
    cdef object py_cast_method
    cdef object py_cast_obj
    cdef object py_res_obj
    cdef int union_field_index
    cdef const iop_field_t *union_field
    cdef dict union_kwargs
    cdef UnionBase union_obj
    cdef EnumBase enum_obj

    is_valid[0] = False

    if iop_type == IOP_T_UNION or iop_type == IOP_T_STRUCT:
        py_type = plugin_get_class_type_st(plugin, field.u1.st_desc)

        # Check cast is handled by the class method __from_python__
        py_cast_method = getattr(py_type, "__from_python__", None)
        if py_cast_method is not None:
            py_cast_obj = py_cast_method(py_obj)
            py_res_obj = exact_or_implicit_convert_field(field, py_cast_obj,
                                                         plugin, is_valid)
            if not is_valid[0]:
                add_error_convert_field(field, py_cast_obj, err)
                return None
            return py_res_obj

        # Object creation can be done with a dict argument
        if isinstance(py_obj, dict):
            py_res_obj = py_type(py_obj)
            is_valid[0] = True
            return py_res_obj


    # Check cast to union with unambiguous type
    if iop_type == IOP_T_UNION:
        union_field_index = -1
        py_cast_obj = find_union_unambiguous_field(field.u1.st_desc, py_obj,
                                                   plugin, &union_field_index)
        if union_field_index >= 0:
            py_type = plugin_get_class_type_st(plugin, field.u1.st_desc)
            union_field = &field.u1.st_desc.fields[union_field_index]
            union_kwargs = { lstr_to_py_str(union_field.name): py_cast_obj }
            union_obj = py_type.__new__(py_type, **union_kwargs)
            union_safe_init(union_obj, union_field_index, union_kwargs)
            is_valid[0] = True
            return union_obj

    # Check value from union can be used as object for field
    if isinstance(py_obj, UnionBase):
        union_obj = <UnionBase>py_obj
        py_cast_obj = union_get_object(union_obj)
        py_res_obj = exact_or_implicit_convert_field(field, py_cast_obj,
                                                     plugin, is_valid)
        if is_valid[0]:
            return py_res_obj

    # Check cast from enum to int
    if (isinstance(py_obj, EnumBase)
     and (iop_type == IOP_T_I8
       or iop_type == IOP_T_U8
       or iop_type == IOP_T_I16
       or iop_type == IOP_T_U16
       or iop_type == IOP_T_I32
       or iop_type == IOP_T_U32
       or iop_type == IOP_T_I64
       or iop_type == IOP_T_U64)):
        enum_obj = <EnumBase>py_obj
        py_res_obj = <int>enum_obj.val
        is_valid[0] = True
        return py_res_obj

    add_error_convert_field(field, py_obj, err)
    return None


cdef object convert_field_object(const iop_field_t *field, object py_obj,
                                 Plugin plugin, cbool *is_valid,
                                 cbool *is_explicit, sb_t *err):
    """Try to convert the python object for the iop field.

    We will try to convert the object implicitly and explicitly.

    Parameters
    ----------
    field
        The iop field.
    py_obj
        The python object to convert.
    plugin
        The IOPy plugin to use to create new objects when needed.
    is_valid
        Set to true if the python object has been converted successfully.
        False otherwise.
    is_explicit
        Set to true if the python object has been explicitly converted.
        False otherwise.
    err
        The error description if the python object is not valid.

    Returns
    -------
        The converted python object in case of conversion,
        or None is the object is not valid.
    """
    cdef object py_res_obj

    is_valid[0] = False
    is_explicit[0] = False

    py_res_obj = exact_or_implicit_convert_field(field, py_obj, plugin,
                                                 is_valid)
    if is_valid[0]:
        return py_res_obj

    py_res_obj = explicit_convert_field(field, py_obj, plugin, is_valid, err)
    if is_valid[0]:
        is_explicit[0] = True
        return py_res_obj

    return None


cdef int check_field_constraints(const iop_struct_t *st,
                                 const iop_field_t *field,
                                 object py_obj, cbool recursive,
                                 Plugin plugin, sb_t *err) except -2:
    """Check the constraints of the field for the python object.

    Parameters
    ----------
    st
        The union or struct description.
    field
        The iop field modified.
    py_obj
        The python object to set to the field.
    recursive
        If True, the constraints are checked recursively in the object.
    plugin
        The IOPy plugin.
    err
        The error description if the object is not valid.

    Returns
    -------
        -2 in case of unexpected python exception, it will be directly handled
        by cython.
        -1 if the object is not valid.
        0 otherwise.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef unsigned int st_flags
    cdef list py_list = None
    cdef void *iop_val
    cdef int iop_len = 1
    cdef cbool is_list = False
    cdef int i
    cdef object py_item
    cdef void *iop_item

    t_scope_ignore(t_scope_guard)

    # Get real struct description of field in case of class.
    if not st.is_union and iop_struct_is_class(st):
        while (st.class_attrs.parent
           and field.data_offs < st.class_attrs.parent.size):
            st = st.class_attrs.parent

    st_flags = st.flags
    if (not TST_BIT(&st_flags, IOP_STRUCT_HAS_CONSTRAINTS)
     or not (recursive or iop_field_has_constraints(st, field))):
        # Nothing to do
        return 0

    if field.repeat == IOP_R_REPEATED:
        py_list = py_obj
        iop_len = len(py_list)
        is_list = True
        if iop_len == 0:
            return 0

    iop_val = mp_imalloc(t_pool(), iop_len * field.size, 8, MEM_RAW)
    for i in range(iop_len):
        py_item = py_list[i] if is_list else py_obj
        iop_item = &(<char *>iop_val)[i * field.size]
        mp_iop_py_obj_field_to_c_val(t_pool(), False, field, py_item,
                                     plugin, iop_item)

    if (field.repeat == IOP_R_OPTIONAL
    and (iop_field_is_class(field) or iop_field_is_reference(field))):
        iop_val = (<void **>iop_val)[0]

    if iop_field_check_constraints(st, field, iop_val, iop_len,
                                   recursive) < 0:
        sb_add_lstr(err, iop_get_err_lstr())
        return -1
    return 0


# }}}
# {{{ Format python object to str

cdef unsigned iopy_kwargs_to_jpack_flags(dict kwargs, cbool reset):
    """Get the json pack flags according to the json pack arguments.

    Parameters
    ----------
    kwargs
        The json pack arguments.
    reset
        When set to False, do not perform a binary 'or' with global json
        pack flags.

    Returns
    -------
        The json pack flags.
    """
    # XXX: use the UNSAFE_INTEGERS so that the products not having lib-common
    #      d64486277c70ed2 can unpack the big numbers serialized.
    cdef unsigned flags = IOP_JPACK_UNSAFE_INTEGERS

    if not reset:
        flags |= iopy_g.jpack_flags

    if kwargs is not None:
        if kwargs.get('no_whitespaces'):
            flags |= IOP_JPACK_NO_WHITESPACES
        if kwargs.get('no_trailing_eol'):
            flags |= IOP_JPACK_NO_TRAILING_EOL
        if kwargs.get('skip_private'):
            flags |= IOP_JPACK_SKIP_PRIVATE
        if kwargs.get('skip_default'):
            flags |= IOP_JPACK_SKIP_DEFAULT
        if kwargs.get('skip_empty_arrays'):
            flags |= IOP_JPACK_SKIP_EMPTY_ARRAYS
        if kwargs.get('skip_empty_structs'):
            flags |= IOP_JPACK_SKIP_EMPTY_STRUCTS
        if kwargs.get('shorten_data'):
            flags |= IOP_JPACK_SHORTEN_DATA
        if kwargs.get('skip_class_names'):
            flags |= IOP_JPACK_SKIP_CLASS_NAMES
        if kwargs.get('skip_optional_class_names'):
            flags |= IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES
        if kwargs.get('minimal'):
            flags |= IOP_JPACK_MINIMAL

        # Obsolete, kept for backward compatibility.
        if kwargs.get('compact'):
            flags |= IOP_JPACK_NO_WHITESPACES

    return flags


cdef str format_py_obj_to_json(StructUnionBase py_obj, dict kwargs):
    """Format struct or union object to json str.

    Parameters
    ----------
    py_obj
        The struct or union python object to format.
    kwargs
        The format arguments.

    Returns
    -------
        The formatted string.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef sb_buf_8k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef void *val = NULL
    cdef unsigned flags

    t_scope_ignore(t_scope_guard)
    mp_iop_py_obj_to_c_val(t_pool(), False, py_obj, &val)

    flags = iopy_kwargs_to_jpack_flags(kwargs, False)
    iop_jpack(struct_union_get_desc(py_obj), val, iop_sb_write, &sb, flags)
    return lstr_to_py_str(LSTR_SB_V(&sb))


cdef str format_py_obj_to_yaml(StructUnionBase py_obj):
    """Format struct or union object to yaml str.

    Parameters
    ----------
    py_obj
        The struct or union python object to format.

    Returns
    -------
        The formatted string.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef sb_buf_8k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef void *val = NULL

    t_scope_ignore(t_scope_guard)
    mp_iop_py_obj_to_c_val(t_pool(), False, py_obj, &val)
    t_iop_sb_ypack(&sb, struct_union_get_desc(py_obj), val, NULL)
    return lstr_to_py_str(LSTR_SB_V(&sb))


cdef bytes format_py_obj_to_bin(StructUnionBase py_obj):
    """Format struct or union object to bin str.

    Parameters
    ----------
    py_obj
        The struct or union python object to format.

    Returns
    -------
        The formatted string.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef void *val = NULL
    cdef lstr_t bin_lstr

    t_scope_ignore(t_scope_guard)
    mp_iop_py_obj_to_c_val(t_pool(), False, py_obj, &val)
    bin_lstr = t_iop_bpack_struct(struct_union_get_desc(py_obj), val)
    return lstr_to_py_bytes(bin_lstr)


cdef str format_py_obj_to_hex(StructUnionBase py_obj):
    """Format struct or union object to hex str.

    Parameters
    ----------
    py_obj
        The struct or union python object to format.

    Returns
    -------
        The formatted string.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef void *val = NULL
    cdef lstr_t bin_lstr
    cdef int hsize
    cdef char *hex_str
    cdef int res_size

    t_scope_ignore(t_scope_guard)
    mp_iop_py_obj_to_c_val(t_pool(), False, py_obj, &val)
    bin_lstr = t_iop_bpack_struct(struct_union_get_desc(py_obj), val)

    hsize = bin_lstr.len * 2 + 1
    hex_str = t_new_char(hsize)
    res_size = strconv_hexencode(hex_str, hsize, bin_lstr.s, bin_lstr.len)
    cassert (res_size == (hsize - 1))

    return lstr_to_py_str(LSTR_INIT_V(hex_str, res_size))


cdef str format_py_obj_to_xml(StructUnionBase py_obj, dict kwargs):
    """Format struct or union object to sml str.

    Parameters
    ----------
    py_obj
        The struct or union python object to format.
    kwargs
        The format arguments.

    Returns
    -------
        The formatted string.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef sb_buf_8k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef const iop_struct_t *st = struct_union_get_desc(py_obj)
    cdef void *val = NULL
    cdef lstr_t name = st.fullname
    cdef lstr_t ns = LSTR_NULL_V
    cdef cbool soap = False
    cdef cbool banner = False
    cdef object py_name
    cdef object py_ns
    cdef object py_soap
    cdef object py_banner
    cdef xmlpp_t pp
    cdef int flags = IOP_XPACK_LITERAL_ENUMS

    t_scope_ignore(t_scope_guard)
    mp_iop_py_obj_to_c_val(t_pool(), False, py_obj, &val)

    py_name = kwargs.get('name')
    if py_name is not None:
        name = t_py_obj_to_lstr(py_name)

    py_ns = kwargs.get('ns')
    if py_ns is not None:
        ns = t_py_obj_to_lstr(py_ns)

    py_soap = kwargs.get('soap')
    if py_soap is not None:
        soap = py_soap
        banner = soap

    py_banner = kwargs.get('banner')
    if py_banner is not None:
        banner = py_banner

    if banner:
        xmlpp_open_banner(&pp, &sb)
    else:
        xmlpp_open(&pp, &sb)

    pp.nospace = True

    if soap:
        if not ns.s:
            xmlpp_close(&pp)
            raise Error('missing namespace locator for soap export')

        flags |= IOP_XPACK_SKIP_PRIVATE

        xmlpp_opentag(&pp, "s:Envelope")
        xmlpp_putattr(&pp, "xmlns:s",
                      "http://schemas.xmlsoap.org/soap/envelope/")
        xmlpp_putattr(&pp, "xmlns:n", ns.s)
        xmlpp_putattr(&pp, "xmlns:xsi",
                      "http://www.w3.org/2001/XMLSchema-instance")
        xmlpp_putattr(&pp, "xmlns:xsd",
                      "http://www.w3.org/2001/XMLSchema")
        xmlpp_putattr(&pp, "s:encodingStyle",
                      "http://schemas.xmlsoap.org/soap/encoding/")

        xmlpp_opentag(&pp, "s:Body")
        xmlpp_opentag(&pp, t_fmt("n:%*pM", LSTR_FMT_ARG(name)))
    else:
        xmlpp_opentag(&pp, name.s)
        if ns.s:
            xmlpp_putattr(&pp, "xmlns", ns.s)

    with nogil:
        iop_xpack_flags(&sb, st, val, flags)
        pp.can_do_attr = False
        xmlpp_close(&pp)

    return lstr_to_py_str(LSTR_SB_V(&sb))


cdef object struct_union_export_dict_field(Plugin plugin,
                                           cbool is_union,
                                           object py_field_obj,
                                           const iop_field_t *field,
                                           unsigned flags,
                                           cbool *is_skipped):
    """Export field of struct or union to a primitive python object.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The struct or union description.
    py_field_obj
        The python field object to export.
    field
        The field description.
    flags
        The jpack flags used to dump the dict.
    is_skipped
        Set to True if the field should be skipped and not put in the dict.

    Returns
    -------
        The primitive python object of the field.
    """
    cdef iop_type_t ftype = field.type
    cdef const iop_enum_t *en
    cdef EnumBase py_enum
    cdef object py_enum_cls
    cdef lstr_t enum_val_lstr
    cdef object def_obj

    if ftype == IOP_T_ENUM:
        en = field.u1.en_desc

        if likely(isinstance(py_field_obj, EnumBase)):
            py_enum = <EnumBase>py_field_obj
        else:
            py_enum_cls = plugin_get_class_type_en(plugin, en)
            py_enum = py_enum_cls(py_field_obj)

        enum_val_lstr = iop_enum_to_str_desc(enum_get_desc(py_enum),
                                             py_enum.val)
        if enum_val_lstr.s:
            py_field_obj = lstr_to_py_str(enum_val_lstr)
        else:
            py_field_obj = py_enum.val

        if ((flags & IOP_JPACK_SKIP_DEFAULT) and
                field.repeat == IOP_R_DEFVAL and
                field.u0.defval_enum == py_enum.val):
            is_skipped[0] = True
            return None

    elif ftype == IOP_T_UNION:
        py_field_obj = union_export_to_dict(<UnionBase>py_field_obj, flags)

    elif ftype == IOP_T_STRUCT:
        py_field_obj = struct_export_to_dict(<StructBase>py_field_obj,
                                             field.u1.st_desc, flags)

        if ((flags & IOP_JPACK_SKIP_EMPTY_STRUCTS) and
                field.repeat == IOP_R_REQUIRED and not is_union and
                not py_field_obj):
            is_skipped[0] = True
            return None

    elif (ftype == IOP_T_VOID and field.repeat == IOP_R_REQUIRED and
            not is_union):
        is_skipped[0] = True
        return None

    elif (flags & IOP_JPACK_SKIP_DEFAULT) and field.repeat == IOP_R_DEFVAL:
        def_obj = struct_union_field_make_default_obj(field)
        if py_field_obj == def_obj:
            is_skipped[0] = True
            return None

    return py_field_obj


# }}}
# {{{ Get fields name


cdef int fill_fields_name_list(const iop_struct_t *st, list l) except -1:
    """Fill list with name of fields of struct or union.

    Parameters
    ----------
    st
        The iop struct or union description.
    l
        The list to fill

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef int i
    cdef const iop_field_t *field

    if not st.is_union and iop_struct_is_class(st) and st.class_attrs.parent:
        fill_fields_name_list(st.class_attrs.parent, l)

    for i in range(st.fields_len):
        field = &st.fields[i]
        l.append(lstr_to_py_str(field.name))

    return 0


# }}}
# {{{ Description


cdef list struct_union_get_fields_name(object cls):
    """Get the list of name of fields of struct or union.

    Parameters
    ----------
    cls
        The struct or union iop class type.

    Returns
    -------
    list
        The list of name of fields.
    """
    cdef const iop_struct_t *st
    cdef list l

    st = struct_union_get_iop_type_cls(cls).desc
    l = []
    fill_fields_name_list(st, l)
    return l


cdef void get_struct_union_desc_fields(const iop_struct_t *st, sb_t *sb):
    """Get the description of the fields of the struct or union.

    Parameters
    ----------
    st
        The struct or union iop description.
    sb
        The string buffer filled with the description.
    """
    cdef int i
    cdef const iop_field_t *field
    cdef iop_type_t ftype
    cdef iop_repeat_t frepeat
    cdef lstr_t def_str

    for i in range(st.fields_len):
        field = &st.fields[i]
        ftype = field.type
        frepeat = field.repeat

        sb_adds(sb, "\n - ")
        sb_add_lstr(sb, field.name)
        sb_adds(sb, " [")
        if frepeat == IOP_R_REPEATED:
            sb_adds(sb, "ARRAY ")
        elif frepeat == IOP_R_OPTIONAL:
            sb_adds(sb, "OPTIONAL ")
        elif frepeat == IOP_R_REQUIRED:
            sb_adds(sb, "REQUIRED ")
        elif frepeat == IOP_R_DEFVAL:
            sb_adds(sb, "DEFVAL ")
            if (ftype == IOP_T_I8
             or ftype == IOP_T_U8
             or ftype == IOP_T_I16
             or ftype == IOP_T_U16
             or ftype == IOP_T_I32
             or ftype == IOP_T_U32
             or ftype == IOP_T_I64
             or ftype == IOP_T_U64
             or ftype == IOP_T_BOOL):
                sb_addf(sb, "%ld", field.u1.defval_u64)
            elif ftype == IOP_T_DOUBLE:
                sb_addf(sb, "%g", field.u1.defval_d)
            elif ftype == IOP_T_ENUM:
                def_str = iop_enum_to_str_desc(field.u1.en_desc,
                                               field.u0.defval_enum)
                sb_add_lstr(sb, def_str)
                sb_addf(sb, " (%d)", field.u0.defval_enum)
            elif (ftype == IOP_T_STRING
               or ftype == IOP_T_XML
               or ftype == IOP_T_DATA):
                def_str = LSTR_INIT_V(<const char *>field.u1.defval_data,
                                      field.u0.defval_len)
                sb_add_lstr(sb, def_str)
            else:
                cassert(False)
        else:
            cassert(False)

        sb_adds(sb, " ] ")

        if ftype == IOP_T_I8:
            sb_adds(sb, "IOP_T_I8")
        elif ftype == IOP_T_U8:
            sb_adds(sb, "IOP_T_U8")
        elif ftype == IOP_T_I16:
            sb_adds(sb, "IOP_T_I16")
        elif ftype == IOP_T_U16:
            sb_adds(sb, "IOP_T_U16")
        elif ftype == IOP_T_I32:
            sb_adds(sb, "IOP_T_I32")
        elif ftype == IOP_T_U32:
            sb_adds(sb, "IOP_T_U32")
        elif ftype == IOP_T_I64:
            sb_adds(sb, "IOP_T_I64")
        elif ftype == IOP_T_U64:
            sb_adds(sb, "IOP_T_U64")
        elif ftype == IOP_T_BOOL:
            sb_adds(sb, "IOP_T_BOOL")
        elif ftype == IOP_T_DOUBLE:
            sb_adds(sb, "IOP_T_DOUBLE")
        elif ftype == IOP_T_XML:
            sb_adds(sb, "IOP_T_XML")
        elif ftype == IOP_T_STRING:
            sb_adds(sb, "IOP_T_STRING")
        elif ftype == IOP_T_DATA:
            sb_adds(sb, "IOP_T_DATA")
        elif ftype == IOP_T_VOID:
            sb_adds(sb, "IOP_T_VOID")
        elif ftype == IOP_T_ENUM:
            sb_adds(sb, "IOP_T_ENUM ")
            sb_add_lstr(sb, field.u1.en_desc.fullname)
        elif ftype == IOP_T_UNION:
            sb_adds(sb, "IOP_T_UNION ")
            sb_add_lstr(sb, field.u1.st_desc.fullname)
        elif ftype == IOP_T_STRUCT:
            sb_adds(sb, "IOP_T_STRUCT ")
            sb_add_lstr(sb, field.u1.st_desc.fullname)
        else:
            cassert(False)


cdef void get_struct_union_desc_class(const iop_struct_t *st, sb_t *sb):
    """Get the description of the fields of the class.

    Parameters
    ----------
    st
        The class iop description that we will iterate upon.
    sb
        The string buffer filled with the description.
    """
    if st.class_attrs.parent:
        get_struct_union_desc_class(st.class_attrs.parent, sb)
    get_struct_union_desc_fields(st, sb)


cdef str get_struct_union_desc(object cls):
    """Return the description of the struct or union.

    Parameters
    ----------
    cls
        The struct or union iop class type.

    Returns
    -------
        The string description of the struct or union.
    """
    cdef sb_buf_1k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef const iop_struct_t *st
    cdef cbool is_class

    st = struct_union_get_iop_type_cls(cls).desc
    is_class = iop_struct_is_class(st)

    if st.is_union:
        sb_adds(&sb, 'union ')
    elif is_class:
        if st.class_attrs.is_abstract:
            sb_adds(&sb, 'abstract ')
        sb_adds(&sb, 'class ')
    else:
        sb_adds(&sb, 'structure ')
    sb_add_lstr(&sb, st.fullname)
    sb_adds(&sb, ':')

    if is_class:
        get_struct_union_desc_class(st, &sb)
    else:
        get_struct_union_desc_fields(st, &sb)

    return lstr_to_py_str(LSTR_SB_V(&sb))


cdef dict struct_union_get_values_of_cls(object cls, cbool skip_optionals):
    """Return the values of the struct or union type as dict.

    Parameters
    ----------
    cls
        The struct or union iop class type.
    skip_optionals
        When True, the optional fields of a struct are skipped to respect
        IOPyV1 compatibility.

    Returns
    -------
        A dictionary containing the different available values of the
        union.
    """
    cdef _InternalStructUnionType iop_type
    cdef const iop_struct_t *st
    cdef Plugin plugin
    cdef dict res

    iop_type = struct_union_get_iop_type_cls(cls)
    st = iop_type.desc
    plugin = iop_type.plugin

    res = {}
    while st:
        fill_struct_union_values_dict(plugin, st, skip_optionals, res)
        st = get_iop_struct_parent(st)
    return res


cdef int fill_struct_union_values_dict(Plugin plugin, const iop_struct_t *st,
                                       cbool skip_optionals,
                                       dict res) except -1:
    """Fill the dict of values of the struct or union types.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The struct or union description.
    skip_optionals
        When True, the optional fields of a struct are skipped to respect
        IOPyV1 compatibility.
    res
        The dictionary of values to fill.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef int i
    cdef const iop_field_t *field
    cdef object py_type

    for i in range(st.fields_len):
        field = &st.fields[i]

        if skip_optionals and field.repeat == IOP_R_OPTIONAL:
            continue

        py_type = struct_union_get_py_type_of_field(plugin, field, field.type)
        res[lstr_to_py_str(field.name)] = py_type


cdef object struct_union_get_py_type_of_field(Plugin plugin,
                                              const iop_field_t *field,
                                              iop_type_t ftype):
    """Get the python type of a field.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    field
        The C iop field.
    ftype
        The C iop type of the iop field.

    Returns
    -------
        The python type of the field.
    """
    cdef object py_type

    if (ftype == IOP_T_I8
     or ftype == IOP_T_U8
     or ftype == IOP_T_I16
     or ftype == IOP_T_U16
     or ftype == IOP_T_I32
     or ftype == IOP_T_U32
     or ftype == IOP_T_I64
     or ftype == IOP_T_U64):
        py_type = int
    elif ftype == IOP_T_BOOL:
        py_type = bool
    elif ftype == IOP_T_DOUBLE:
        py_type = float
    elif ftype == IOP_T_XML or ftype == IOP_T_STRING:
        py_type = str
    elif ftype == IOP_T_DATA:
        py_type = bytes
    elif ftype == IOP_T_VOID:
        py_type = None
    elif ftype == IOP_T_ENUM:
        py_type = plugin_get_class_type_en(plugin, field.u1.en_desc)
    elif ftype == IOP_T_UNION or ftype == IOP_T_STRUCT:
        py_type = plugin_get_class_type_st(plugin, field.u1.st_desc)
    else:
        cassert(False)
        py_type = None

    return py_type


cdef str struct_union_get_iop_type_of_field(Plugin plugin,
                                            const iop_field_t *field,
                                            iop_type_t ftype):
    """Get the iop type of a field as a string.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    field
        The C iop field.
    ftype
        The C iop type of the iop field.

    Returns
    -------
        The iop type of the field.
    """
    cdef str res

    if (ftype == IOP_T_I8
     or ftype == IOP_T_U8
     or ftype == IOP_T_I16
     or ftype == IOP_T_U16
     or ftype == IOP_T_I32
     or ftype == IOP_T_U32
     or ftype == IOP_T_I64
     or ftype == IOP_T_U64
     or ftype == IOP_T_BOOL
     or ftype == IOP_T_DOUBLE
     or ftype == IOP_T_XML
     or ftype == IOP_T_STRING
     or ftype == IOP_T_DATA
     or ftype == IOP_T_VOID):
        res = c_str_to_py_str(iop_type_get_string_desc(ftype))
    elif ftype == IOP_T_ENUM:
        res = lstr_to_py_str(field.u1.en_desc.fullname)
    elif ftype == IOP_T_UNION or ftype == IOP_T_STRUCT:
        res = lstr_to_py_str(field.u1.st_desc.fullname)
    else:
        cassert (False)
        res = "<invalid>"

    return res


cdef dict struct_union_make_iop_generic_attributes(
    const iop_struct_t *st, const iop_help_t **iop_help,
    cbool *is_help_v2, cbool *deprecated):
    """Make IOP generic attributes of a struct or union.

    Parameters
    ----------
    st
        The C iop struct or union.
    iop_help
        Set to the iop help of the struct or union type if found.
    is_help_v2
        Set to True if the iop help is ATTR_HELP_V2.
    deprecated
        Set to True if the struct or union is deprecated.

    Returns
    -------
        The generic attributes of the struct or union iop type.
    """
    cdef unsigned flags = st.flags
    cdef int i
    cdef const iop_struct_attr_t *attr
    cdef str attr_key
    cdef object attr_val
    cdef dict res = {}

    if not TST_BIT(&flags, IOP_STRUCT_EXTENDED) or not st.st_attrs:
        return res

    for i in range(st.st_attrs.attrs_len):
        attr = &st.st_attrs.attrs[i]

        if attr.type == IOP_STRUCT_ATTR_HELP:
            iop_help[0] = <const iop_help_t *>(attr.args[0].v.p)
            is_help_v2[0] = False
            continue
        elif attr.type == IOP_STRUCT_ATTR_HELP_V2:
            iop_help[0] = <const iop_help_t *>(attr.args[0].v.p)
            is_help_v2[0] = True
            continue
        elif (attr.type == IOP_STRUCT_GEN_ATTR_S
           or attr.type == IOP_STRUCT_GEN_ATTR_O):
            attr_val = lstr_to_py_str(attr.args[1].v.s)
        elif attr.type == IOP_STRUCT_GEN_ATTR_I:
            attr_val = attr.args[1].v.i64
        elif attr.type == IOP_STRUCT_GEN_ATTR_D:
            attr_val = attr.args[1].v.d
        else:
            cassert (False)
            continue

        attr_key = lstr_to_py_str(attr.args[0].v.s)
        res[attr_key] = attr_val

    deprecated[0] = TST_BIT(&st.st_attrs.flags, IOP_STRUCT_DEPRECATED)

    return res


cdef int struct_union_make_iop_fields_descriptions(Plugin plugin,
                                                   const iop_struct_t *st,
                                                   dict res) except -1:
    """Make IOP fields descriptions of a struct or union.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The C iop struct or union.
    res
        The IOP fields descriptions as a dict.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef int i
    cdef unsigned flags = st.flags
    cdef const iop_field_t *field
    cdef const iop_field_attrs_t *field_attrs
    cdef IopStructUnionFieldDescription field_desc

    for i in range(st.fields_len):
        field = &st.fields[i]
        if TST_BIT(&flags, IOP_STRUCT_EXTENDED) and st.fields_attrs:
            field_attrs = &st.fields_attrs[i]
        else:
            field_attrs = NULL

        field_desc = IopStructUnionFieldDescription.__new__(
            IopStructUnionFieldDescription)
        struct_union_make_iop_field_description(plugin, field, field.repeat,
                                                field.type, field_attrs,
                                                field_desc)
        res[lstr_to_py_str(field.name)] = field_desc

    return 0

cdef int struct_union_make_iop_field_description(
    Plugin plugin, const iop_field_t *field, iop_repeat_t frepeat,
    iop_type_t ftype, const iop_field_attrs_t *field_attrs,
    IopStructUnionFieldDescription res) except -1:
    """Make IOP field description of a struct or union field.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    field
        The C iop field. Can be NULL if we are making a class static field
        description.
    frepeat
        The C iop repeat flag of the iop field.
    ftype
        The C iop type of the iop field.
    field_attrs
        The C iop field attributes.
    res
        The IOP field description.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef object default_value = None
    cdef bool optional = False
    cdef bool repeated = False
    cdef int i
    cdef const iop_field_attr_t *attr
    cdef dict generic_attributes = {}
    cdef cbool is_gen_attr
    cdef str gen_attr_key
    cdef object gen_attr_val = None
    cdef const iop_help_t *iop_help = NULL
    cdef cbool is_help_v2 = False
    cdef bool private = False
    cdef bool deprecated = False
    cdef object py_min = None
    cdef object py_max = None
    cdef object py_min_occurs = None
    cdef object py_max_occurs = None
    cdef int64_t min_length = 0
    cdef int64_t max_length = 0
    cdef object py_min_length = None
    cdef object py_max_length = None
    cdef object py_length = None
    cdef bool cdata = False
    cdef bool non_zero = False
    cdef bool non_empty = False
    cdef str pattern = None

    # Get default value.
    if frepeat == IOP_R_DEFVAL:
        default_value = struct_union_field_make_default_obj(field)
    elif frepeat == IOP_R_OPTIONAL:
        optional = True
    elif frepeat == IOP_R_REPEATED:
        repeated = True
    else:
        cassert (frepeat == IOP_R_REQUIRED)

    # Get other attributes from field_attrs.
    if field_attrs:
        for i in range(field_attrs.attrs_len):
            attr = &field_attrs.attrs[i]
            is_gen_attr = False

            if attr.type == IOP_FIELD_MIN_OCCURS:
                py_min_occurs = attr.args[0].v.i64
            elif attr.type == IOP_FIELD_MAX_OCCURS:
                py_max_occurs = attr.args[0].v.i64
            elif attr.type == IOP_FIELD_MIN_LENGTH:
                min_length = attr.args[0].v.i64
                py_min_length = min_length
            elif attr.type == IOP_FIELD_MAX_LENGTH:
                max_length = attr.args[0].v.i64
                py_max_length = max_length
            elif attr.type == IOP_FIELD_MIN:
                py_min = iop_field_min_max_attr_value(ftype, attr)
            elif attr.type == IOP_FIELD_MAX:
                py_max = iop_field_min_max_attr_value(ftype, attr)
            elif attr.type == IOP_FIELD_PATTERN:
                pattern = lstr_to_py_str(attr.args[0].v.s)
            elif attr.type == IOP_FIELD_ATTR_HELP:
                iop_help = <const iop_help_t *>(attr.args[0].v.p)
                is_help_v2 = False
            elif attr.type == IOP_FIELD_ATTR_HELP_V2:
                iop_help = <const iop_help_t *>(attr.args[0].v.p)
                is_help_v2 = True
            elif (attr.type == IOP_FIELD_GEN_ATTR_S
               or attr.type == IOP_FIELD_GEN_ATTR_O):
                gen_attr_val = lstr_to_py_str(attr.args[1].v.s)
                is_gen_attr = True
            elif attr.type == IOP_FIELD_GEN_ATTR_I:
                gen_attr_val = attr.args[1].v.i64
                is_gen_attr = True
            elif attr.type == IOP_FIELD_GEN_ATTR_D:
                gen_attr_val = attr.args[1].v.d
                is_gen_attr = True
            else:
                cassert (False)
                continue

            if is_gen_attr:
                gen_attr_key = lstr_to_py_str(attr.args[0].v.s)
                generic_attributes[gen_attr_key] = gen_attr_val

        private = TST_BIT(&field_attrs.flags, IOP_FIELD_PRIVATE)
        deprecated = TST_BIT(&field_attrs.flags, IOP_FIELD_DEPRECATED)
        cdata = TST_BIT(&field_attrs.flags, IOP_FIELD_CDATA)
        non_empty = TST_BIT(&field_attrs.flags, IOP_FIELD_NON_EMPTY)
        non_zero = TST_BIT(&field_attrs.flags, IOP_FIELD_NON_ZERO)

        # When min_length == max_length, it means that we have a @length
        # attribute.
        if (py_min_length is not None
        and py_max_length is not None
        and min_length == max_length):
            py_length = py_min_length

    res.help = make_iop_help_description(iop_help, is_help_v2)
    res.generic_attributes = generic_attributes
    res.iop_type = struct_union_get_iop_type_of_field(plugin, field, ftype)
    res.py_type = struct_union_get_py_type_of_field(plugin, field, ftype)
    res.default_value = default_value
    res.optional = optional
    res.repeated = repeated
    res.private = private
    res.deprecated = deprecated
    res.min = py_min
    res.max = py_max
    res.min_occurs = py_min_occurs
    res.max_occurs = py_max_occurs
    res.min_length = py_min_length
    res.max_length = py_max_length
    res.length = py_length
    res.cdata = cdata
    res.non_zero = non_zero
    res.non_empty = non_empty
    res.pattern = pattern

    return 0


cdef object struct_union_field_make_default_obj(const iop_field_t *field):
    """Make a Python object corresponding to default value of the field.

    Parameters
    ----------
    field
        The C iop field.

    Returns
    -------
        A Python object corresponding to the default value of the fields.
    """
    cdef iop_type_t ftype = field.type
    cdef lstr_t def_str
    cdef object default_value = None

    cassert(field.repeat == IOP_R_DEFVAL)

    if (ftype == IOP_T_I8
     or ftype == IOP_T_U8
     or ftype == IOP_T_I16
     or ftype == IOP_T_U16
     or ftype == IOP_T_I32
     or ftype == IOP_T_U32
     or ftype == IOP_T_I64
     or ftype == IOP_T_U64):
        default_value = field.u1.defval_u64
    elif ftype == IOP_T_BOOL:
        default_value = field.u1.defval_u64 != 0
    elif ftype == IOP_T_DOUBLE:
        default_value = field.u1.defval_d
    elif ftype == IOP_T_ENUM:
        def_str = iop_enum_to_str_desc(field.u1.en_desc,
                                       field.u0.defval_enum)
        default_value = lstr_to_py_str(def_str)
    elif (ftype == IOP_T_STRING
       or ftype == IOP_T_XML
       or ftype == IOP_T_DATA):
        def_str = LSTR_INIT_V(<const char *>field.u1.defval_data,
                              field.u0.defval_len)
        default_value = lstr_to_py_str(def_str)
    else:
        cassert(False)

    return default_value


cdef object iop_field_min_max_attr_value(iop_type_t ftype,
                                         const iop_field_attr_t *attr):
    """Get the value of a min/max constraint for an iop field.

    Parameters
    ----------
    ftype
        The C iop type of the iop field.
    attr
        The C iop field attribute.

    Returns
    -------
        The min/max constraint value as python object.
    """
    if ftype == IOP_T_DOUBLE:
        return attr.args[0].v.d
    elif (ftype == IOP_T_I8
       or ftype == IOP_T_I16
       or ftype == IOP_T_I32
       or ftype == IOP_T_I64):
        return attr.args[0].v.i64
    elif (ftype == IOP_T_U8
       or ftype == IOP_T_U16
       or ftype == IOP_T_U32
       or ftype == IOP_T_U64):
        return <uint64_t>(attr.args[0].v.i64)
    else:
        cassert (False)
        return None


cdef int struct_union_init_iop_description(
    _InternalStructUnionType iop_type,
    IopStructUnionDescription desc) except -1:
    """Init IOP struct or union description from a C struct or union iop type.

    Parameters
    ----------
    iop_type
        The struct or union IOP type.
    desc
        The IOP struct or union description to init.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef Plugin plugin = iop_type.plugin
    cdef const iop_struct_t *st = iop_type.desc
    cdef const iop_help_t *iop_help = NULL
    cdef cbool is_help_v2 = False
    cdef cbool deprecated = False
    cdef dict fields = {}

    desc.generic_attributes = struct_union_make_iop_generic_attributes(
        st, &iop_help, &is_help_v2, &deprecated)
    desc.help = make_iop_help_description(iop_help, is_help_v2)
    desc.deprecated = deprecated

    while st:
        struct_union_make_iop_fields_descriptions(plugin, st, fields)
        st = get_iop_struct_parent(st)

    desc.fields = fields

    return 0


# }}}
# }}}
# {{{ Union


cdef class UnionBase(StructUnionBase):
    """Iopy Union object

    Setting a valid members of this object erase the previous ones.
    get_values() return a list of all allowed members.
    Objects are callable to create new instances.

    Almost all methods from Iopy struct class are available for union class
    (like special constructors using _[json|yaml|xml|hex|bin]
    or methods to_[json|yaml|xml|hex|bin]() ])

    Demo:
    #import iopy
    #q = iopy.Plugin("~/dev/mmsx/qrrd/iop/qrrd-iop-plugin.so")
    #q.qrrdquery.Key
    Union qrrdquery.Key
    #q.qrrdquery.Key.get_values()
    {'s': <type 'str'>, 't': <type 'int'>}
    #print(q.qrrdquery.Key.get_desc())
    union qrrdquery.Key:
     - s [REQUIRED ] IOP_T_STRING
     - t [REQUIRED ] IOP_T_U32
    #a = q.qrrdquery.Key(s='toto')
    #a
    Union qrrdquery.Key: s = 'toto'
    #a.t = 12L
    #a
    Union qrrdquery.Key: t = 12L
    #a.get_key()
    't'
    """
    cdef int field_index

    def __init__(UnionBase self, *args, **kwargs):
        """Contructor of the Union.

        See get_values() for allowed keys and argument.
        When field can be guess unambiguously from argument type, named
        keyword is not required, only the argument.

        Example:
        union Key {
            string s;
            int    i;
        };
        #pkg.Key(s='a')
            Union pkg.Key: s = 'a'
        #pkg.Key('a')
            Union pkg.Key: s = 'a'

        This cast is allowed for constructor too, for a member of a struct.
        Example:
        struct NamedKey {
            Key    k;
            string name;
        };
        #pkg.NamedKey(k=pkg.Key(s='a'), name='b')
            Struct pkg.NamedKey:{'k': Union pkg.Key: s = 'a', 'name': 'b'}
        is equivalent to
        #pkg.NamedKey(k=pkg.Key('a'), name='b')
        or
        #pkg.NamedKey(k='a', name='b')
        """
        union_set(self, args, kwargs)

    def get_object(UnionBase self):
        """Get the currently set object.

        Returns
        -------
        object
            The object for the field currently set.
        """
        return union_get_object(self)

    def __object__(UnionBase self):
        """Deprecated, use get_object() instead."""
        return union_get_object(self)

    def get_key(UnionBase self):
        """Get the currently set field name.

        Returns
        -------
        str
            The field name of the currently set field.
        """
        return union_get_key(self)

    def __key__(UnionBase self):
        """Deprecated, use get_key() instead."""
        return union_get_key(self)

    def __setattr__(UnionBase self, object name, object value):
        """Set attribute of union.

        If name corresponds to a field of the union, the previous union field
        value will be removed before setting this new one.

        Parameters
        ----------
        name : str
            The name of the field to set.
        value : object
            The value of the field to set.
        """
        cdef sb_buf_1k_t err_buf
        cdef sb_scope_t err = sb_scope_init_static(err_buf)
        cdef _InternalStructUnionType iop_type
        cdef const iop_struct_t *st
        cdef Plugin plugin
        cdef int field_index
        cdef const iop_field_t *field
        cdef cbool is_valid
        cdef cbool is_explicit
        cdef const iop_field_t *old_field
        cdef str old_field_name
        cdef object py_res

        iop_type = struct_union_get_iop_type(self)
        st = iop_type.desc
        plugin = iop_type.plugin

        field_index = 0
        field = find_field_in_st_by_name(st, name, &field_index)
        if field:
            is_valid = False
            is_explicit = False
            py_res = convert_field_object(field, value, plugin, &is_valid,
                                          &is_explicit, &err)
            if (not is_valid
             or check_field_constraints(st, field, py_res, is_explicit,
                                        plugin, &err) < 0):
                raise Error('error when parsing %s: '
                            'invalid selected union field `%s`: %s' %
                            (lstr_to_py_str(st.fullname), name,
                             lstr_to_py_str(LSTR_SB_V(&err))))

            if self.field_index != field_index:
                old_field = &st.fields[self.field_index]
                old_field_name = lstr_to_py_str(old_field.name)
                py_object_generic_delattr(self, old_field_name)
                self.field_index = field_index
        else:
            py_res = value
        PyObject_GenericSetAttr(self, name, py_res)

    def __delattr__(UnionBase self, object name):
        """Delete attribute of union.

        It is not possible to delete a field of a union object.

        Parameters
        ----------
        name : str
            The name of the attribute to remove.
        """
        cdef const iop_field_t *field

        field = find_field_in_st_by_name(struct_union_get_desc(self), name,
                                         NULL)
        if field:
            raise Error('deleting attribute of union object is forbidden')
        py_object_generic_delattr(self, name)

    def __repr__(UnionBase self):
        """Return the represention of the union."""
        cdef const iop_struct_t *st = struct_union_get_desc(self)
        cdef const iop_field_t *field
        cdef object field_name
        cdef object obj

        field = &st.fields[self.field_index]
        field_name = lstr_to_py_str(field.name)
        obj = getattr(self, field_name)

        return 'Union %s: %s = %r' % (lstr_to_py_str(st.fullname), field_name,
                                      obj)

    @classmethod
    def get_iop_description(object cls):
        """Get the IOP description of the union.

        Returns
        -------
        IopUnionDescription
            The IOP description of the union type.
        """
        cdef _InternalStructUnionType iop_type
        cdef IopUnionDescription res

        iop_type = struct_union_get_iop_type_cls(cls)
        res = IopUnionDescription.__new__(IopUnionDescription)
        struct_union_init_iop_description(iop_type, res)
        return res

    def to_dict(UnionBase self, **kwargs):
        """Deeply convert the union object to a dict.

        It is a faster version of doing `json.loads(obj.to_json())`.

        Parameters
        ----------
        skip_private : bool, optional
            Skip the private fields (lossy).
        skip_default : bool, optional
            Skip fields having their default value.
        skip_empty_arrays : bool, optional
            Skip empty repeated fields.
        skip_empty_structs : bool, optional
            Skip empty sub-structures.
        skip_class_names : bool, optional
            Skip class names (lossy).
        skip_optional_class_names : bool, optional
            Skip class names when not needed.
            If set, the class names won't be written if they are equal to the
            actual type of the field (missing class names are supported by the
            unpacker in that case).
        minimal : bool, optional
            Produce the smallest non-lossy possible dict.
            This is:
                skip_default +
                skip_empty_arrays +
                skip_empty_structs +
                skip_optional_class_names

        Returns
        -------
        dict
            The dict containing the values of the union object.
        """
        cdef unsigned flags

        flags = iopy_kwargs_to_jpack_flags(kwargs, True)
        return union_export_to_dict(self, flags)


cdef class Union(UnionBase):
    """Union class for backward compatibility"""


@cython.final
cdef class IopUnionDescription(IopStructUnionDescription):
    """Description of an IOP union type.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP union.
    deprecated : bool
        True if the IOP union is deprecated.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP union.
    fields : dict(str, IopStructUnionFieldDescription)
        The dictionary of IOP description of each fields.
    """
    pass


cdef int union_safe_init(UnionBase py_obj, int field_index,
                         dict kwargs) except -1:
    """Initialize the union iopy python object with safe dictionary.

    The given kwargs dict has valid values and we can directly set it to
    the object.

    If the object has redefined a custom constructor, we still need to use
    it though.

    Parameters
    ----------
    py_obj
        The union python object.
    field_index
        The index of the field set for the union.
    kwargs
        The valid dictionary of values for the union.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef object cls_init = type(py_obj).__init__

    if cls_init is UnionBase.__init__:
        py_obj.field_index = field_index
        py_obj.__dict__ = kwargs
    else:
        cls_init(py_obj, **kwargs)
    return 0


cdef object union_get_object(UnionBase py_obj):
    """Get the currently set object.

    Parameters
    ----------
    py_obj
        The union python object.

    Returns
    -------
        The object for the field currently set.
    """
    cdef const iop_struct_t *st
    cdef const iop_field_t *field

    st = struct_union_get_desc(py_obj)
    field = &st.fields[py_obj.field_index]
    return getattr(py_obj, lstr_to_py_str(field.name))


cdef object union_get_key(UnionBase py_obj):
    """Get the currently set field name.

    Parameters
    ----------
    py_obj
        The union python object.

    Returns
    -------
    str
        The field name of the currently set field.
    """
    cdef const iop_struct_t *st = struct_union_get_desc(py_obj)
    cdef const iop_field_t *field = &st.fields[py_obj.field_index]

    return lstr_to_py_str(field.name)


cdef int union_set(UnionBase py_obj, tuple args, dict kwargs) except -1:
    """Set the union value from arguments.

    Parameters
    ----------
    py_obj
        The union python object.
    args
        Positional arguments.
    kwargs
        Named arguments.

    Returns
    -------
        -1 in case of python exception. 0 otherwise.
    """
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef _InternalStructUnionType iop_type
    cdef const iop_struct_t *st
    cdef Plugin plugin
    cdef object py_arg
    cdef object py_res
    cdef int field_index
    cdef const iop_field_t *field
    cdef object field_name
    cdef cbool is_valid
    cdef cbool is_explicit
    cdef cbool recursive_constraints

    iop_type = struct_union_get_iop_type(py_obj)
    st = iop_type.desc
    plugin = iop_type.plugin

    if (len(args) + len(kwargs)) !=  1:
        raise Error('error when parsing %s: '
                    'exactly one argument is expected, allowed: %s' %
                    (lstr_to_py_str(st.fullname), union_get_values(py_obj)))

    if args:
        py_arg = args[0]
        field_index = -1
        py_res = find_union_unambiguous_field(st, py_arg, plugin,
                                              &field_index)
        if field_index < 0:
            raise Error('error when parsing %s: '
                        'unable to find unambiguous field for `%s`, '
                        'allowed: %s' %
                        (lstr_to_py_str(st.fullname), py_arg,
                         union_get_values(py_obj)))
        field = &st.fields[field_index]
        field_name = lstr_to_py_str(field.name)
        recursive_constraints = False
    else:
        field_name = None
        py_arg = None
        for field_name, py_arg in kwargs.iteritems():
            break

        field_index = 0
        field = find_field_in_st_by_name(st, field_name, &field_index)
        if not field:
            raise Error('error when parsing %s: '
                        'invalid argument `%s`, allowed: %s' %
                        (lstr_to_py_str(st.fullname), field_name,
                         union_get_values(py_obj)))

        is_valid = False
        is_explicit = False
        py_res = convert_field_object(field, py_arg, plugin, &is_valid,
                                      &is_explicit, &err)
        if not is_valid:
            raise Error('error when parsing %s: '
                        'invalid selected union field `%s`: %s' %
                        (lstr_to_py_str(st.fullname), field_name,
                         lstr_to_py_str(LSTR_SB_V(&err))))
        recursive_constraints = is_explicit

    if check_field_constraints(st, field, py_res, recursive_constraints,
                               plugin, &err) < 0:
        raise Error('error when parsing %s: '
                    'invalid selected union field `%s`: %s' %
                    (lstr_to_py_str(st.fullname), field_name,
                     lstr_to_py_str(LSTR_SB_V(&err))))

    py_obj.field_index = field_index
    PyObject_GenericSetAttr(py_obj, field_name, py_res)
    return 0


cdef inline dict union_get_values(UnionBase py_obj):
    """Return the values of the union object as dict.

    Parameters
    ----------
    py_obj
        The union python object.

    Returns
    -------
        A dictionary containing the different available values of the
        union.
    """
    return struct_union_get_values_of_cls(type(py_obj), False)


cdef dict union_export_to_dict(UnionBase py_obj, unsigned flags):
    """Deeply convert the union object to a dict.

    It is a faster version of doing `json.loads(obj.to_json())`.

    Parameters
    ----------
    py_obj
        The union python object to export.
    flags
        The jpack flags used to dump the dict.

    Returns
    -------
        The dict containing the values of the union object.
    """

    cdef const iop_struct_t *st
    cdef Plugin plugin
    cdef const iop_field_t *field
    cdef str py_field_name
    cdef object py_field_obj

    iop_type = struct_union_get_iop_type(py_obj)
    st = iop_type.desc
    plugin = iop_type.plugin

    field = &st.fields[py_obj.field_index]
    py_field_name = lstr_to_py_str(field.name)

    py_field_obj = getattr(py_obj, py_field_name)
    py_field_obj = struct_union_export_dict_field(
        plugin, True, py_field_obj, field, flags, NULL)

    return {
        py_field_name: py_field_obj
    }


# }}}
# {{{ Struct


cdef class StructBase(StructUnionBase):
    """Iopy Struct object

    This object is used to represent an IOP structure or class.
    All members of an object are considered as IOP fields.
    Attributes name and type are check when set.
    They are initialized to their default value if defined.
    Objects are callable to create new instances. Fields of the new instance
    are passed as named arguments and must conform to the IOP structure.
    Arguments with value None are ignored.
    get_desc() can be called to get a description of the internal IOP
    structure.
    You can also create new instances from json, yaml, bin, xml or
    hexadecimal iop packed strings by creating the object with a single
    argument named _json, _yaml, _xml, _bin or _hex.

    You can dump thoses struct into json, yaml, bin, xml or hexadecimal
    iop-packed string using methods to_json(), to_yaml(), to_bin(), to_xml()
    or to_hex().

    Demo:
    #import iopy
    #q = iopy.Plugin("~/dev/mmsx/qrrd/iop/qrrd-iop-plugin.so")
    #q.qrrdquery.KeyFull
    Struct qrrdquery.KeyFull:{'key': '', 'aggrs': 0L}
    #print(q.qrrdquery.KeyFull.get_desc())
    structure qrrdquery.KeyFull:
     - key [REQUIRED ] IOP_T_STRING
     - aggrs [REQUIRED ] IOP_T_U32
    #a = q.qrrdquery.KeyFull(key='toto', aggrs=23)
    #print(a)
    {
            "key": "toto",
            "aggrs": 23
    }
    #a.key = "lili"
    #print(a.to_xml())
    <qrrdquery.KeyFull><key>lili</key><aggrs>23</aggrs></qrrdquery.KeyFull>
    #b = q.qrrdquery.KeyFull(_json = "{"key": "juju","aggrs":46}")
    #b
    Struct qrrdquery.KeyFull:{'key': 'juju', 'aggrs': 46L}
    """

    def __init__(StructBase self, **kwargs):
        """Constructor.

        Parameters
        ----------
        kwargs
            The list of values for the fields of the structure.
        """
        cdef t_scope_t t_scope_guard = t_scope_init()
        cdef sb_buf_1k_t err_buf
        cdef sb_scope_t err = sb_scope_init_static(err_buf)
        cdef object key
        cdef const iop_struct_t *st
        cdef void *empty_val = NULL

        t_scope_ignore(t_scope_guard)
        st = struct_union_get_desc(self)

        if iop_struct_is_class(st) and st.class_attrs.is_abstract:
            raise Error('cannot instantiate an abstract class: %s' %
                        lstr_to_py_str(st.fullname))

        for key in kwargs:
            if not find_field_in_st_by_name(st, key, NULL):
                raise Error('invalid key %s, allowed: %s' %
                            (key, self.get_iopslots()))

        if t_struct_init_fields(self, st, st, kwargs, &empty_val, &err) < 0:
            raise Error("error when parsing %s: %s" %
                        (lstr_to_py_str(st.fullname),
                         lstr_to_py_str(LSTR_SB_V(&err))))

    @classmethod
    def get_iopslots(object cls):
        """ Return a list of all available IOP slots.

        Returns
        -------
        str
            The list of IOP slots.
        """
        return struct_get_iopslots(cls)

    @classmethod
    def __iopslots__(object cls):
        """Deprecated, use get_iopslots() instead."""
        return struct_get_iopslots(cls)

    def __setattr__(StructBase self, object name, object value):
        """Set attribute of struct.

        Parameters
        ----------
        name : str
            The name of the field to set.
        value : object
            The value of the field to set.
        """
        cdef sb_buf_1k_t err_buf
        cdef sb_scope_t err = sb_scope_init_static(err_buf)
        cdef const iop_struct_t *st = struct_union_get_desc(self)
        cdef const iop_field_t *field
        cdef cbool is_valid
        cdef object py_res

        field = find_field_in_st_by_name(st, name, NULL)
        if field:
            if field.type == IOP_T_VOID:
                # We try to set a void field, we should ignore it if the field
                # is not optional, or set it to None if it is optional.
                struct_set_present_void_field(self, field, name)
                return

            is_valid = False
            py_res = struct_convert_provided_field(self, st, field, value,
                                                   &is_valid, &err)
            if not is_valid:
                raise Error('error when parsing %s: '
                            'invalid struct field `%s`: %s' %
                            (lstr_to_py_str(st.fullname), name,
                             lstr_to_py_str(LSTR_SB_V(&err))))
        else:
            py_res = value
        PyObject_GenericSetAttr(self, name, py_res)

    def __delattr__(StructBase self, object name):
        """Delete attribute of struct.

        It is not possible to delete a mandatory field of a struct object.

        Parameters
        ----------
        name : str
            The name of the attribute to remove.
        """
        cdef const iop_struct_t *st = struct_union_get_desc(self)
        cdef const iop_field_t *field

        field = find_field_in_st_by_name(st, name, NULL)
        if field and field.repeat != IOP_R_OPTIONAL:
            raise Error('cannot delete mandatory field %s of %s type' %
                        (name, lstr_to_py_str(st.fullname)))
        py_object_generic_delattr(self, name)

    @classmethod
    def get_class_attrs(object cls):
        """Return the class attributes.

        Returns
        -------
        dict
            If the iop structure is not a class then return None.
            Else, return a dictionary with the following entries:
            base : str
                The fullname of the base class. It is an empty string when
                there is no base class.
            statics : dict
                Dictionary whose keys are the static fields names and values
                are statics fields values. It is an empty dictionary when
                there is no static fields.
            cls_statics : dict
                Dictionary which contains the class's own static fields.
            is_abstract : bool
                Notices if the class is abstract.
        """
        return struct_get_class_attrs(cls)

    @classmethod
    def __get_class_attrs__(object cls):
        """Deprecated, use get_class_attrs() instead."""
        return struct_get_class_attrs(cls)

    def __repr__(StructBase self):
        """Return the represention of the structure."""
        cdef const iop_struct_t *st = struct_union_get_desc(self)

        return 'Struct %s:%r' % (lstr_to_py_str(st.fullname), self.__dict__)

    @classmethod
    def get_iop_description(object cls):
        """Get the IOP description of the struct or the class.

        Returns
        -------
        IopStructDescription or IopClassDescription
            The IOP description of the struct or class type.
        """
        cdef _InternalStructUnionType iop_type
        cdef IopClassDescription class_res
        cdef IopStructDescription struct_res

        iop_type = struct_union_get_iop_type_cls(cls)
        if iop_struct_is_class(iop_type.desc):
            class_res = IopClassDescription.__new__(IopClassDescription)
            class_init_iop_description(iop_type, class_res)
            return class_res
        else:
            struct_res = IopStructDescription.__new__(IopStructDescription)
            struct_union_init_iop_description(iop_type, struct_res)
            return struct_res

    def to_dict(StructBase self, **kwargs):
        """Deeply convert the struct object to a dict.

        It is a faster version of doing `json.loads(obj.to_json())`.

        Parameters
        ----------
        skip_private : bool, optional
            Skip the private fields (lossy).
        skip_default : bool, optional
            Skip fields having their default value.
        skip_empty_arrays : bool, optional
            Skip empty repeated fields.
        skip_empty_structs : bool, optional
            Skip empty sub-structures.
        skip_class_names : bool, optional
            Skip class names (lossy).
        skip_optional_class_names : bool, optional
            Skip class names when not needed.
            If set, the class names won't be written if they are equal to the
            actual type of the field (missing class names are supported by the
            unpacker in that case).
        minimal : bool, optional
            Produce the smallest non-lossy possible dict.
            This is:
                skip_default +
                skip_empty_arrays +
                skip_empty_structs +
                skip_optional_class_names

        Returns
        -------
        dict
            The dict containing the values of the struct object.
        """
        cdef unsigned flags

        flags = iopy_kwargs_to_jpack_flags(kwargs, True)
        return struct_export_to_dict(self, NULL, flags)


cdef class Struct(StructBase):
    """Struct class for backward compatibility"""


cdef class IopStructDescription(IopStructUnionDescription):
    """Description of an IOP struct type.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP struct.
    deprecated : bool
        True if the IOP struct is deprecated.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP struct.
    fields : dict(str, IopStructUnionFieldDescription)
        The dictionary of IOP description of each fields.
    """
    pass


@cython.final
cdef class IopClassDescription(IopStructDescription):
    """Description of an IOP class type.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP class.
    deprecated : bool
        True if the IOP class is deprecated.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP class.
    fields : dict(str, IopStructUnionFieldDescription)
        The dictionary of IOP description of each fields.
    parent : StructBase or None
        The IOPy parent class of the IOP class.
    is_abstract : bool
        True if the class is abstract.
    is_private : bool
        True if the class is private.
    class_id : int
        The class id of the IOP class.
    statics : dict(str, IopClassStaticFieldDescription)
        The dictionary of IOP description of each static class fields of this
        class and all its parents.
    cls_statics : dict(str, IopClassStaticFieldDescription)
        The dictionary of IOP description of only this class static class
        fields.
    """
    cdef readonly object parent
    cdef readonly bool is_abstract
    cdef readonly bool is_private
    cdef readonly int class_id
    cdef readonly dict statics
    cdef readonly dict cls_statics


cdef class IopClassStaticFieldDescription(IopStructUnionFieldDescription):
    """Description of an IOP class static field.

    Attributes
    ----------
    help : IopHelpDescription
        The documentation of the IOP field.
    generic_attributes : dict(str, object)
        The dictionary of generic IOP attributes of the IOP field.
    iop_type : str
        The iop type as a string of the IOP field.
    py_type : type
        The python type of the IOP field.
    default_value : object or None
        The default value of the field if any.
    optional : bool
        True if the field is optional.
    repeated : bool
        True if the field is an array.
    private : bool
        True if the field is private.
    deprecated : bool
        True if the field is deprecated.
    min : number or None
        Minimal value allowed for the field.
        Applies only to numeric fields.
    max : number or None
        Maximal value allowed for the field.
        Applies only to numeric fields.
    min_occurs : int or None
        For repeated types, minimum number of objects allowed in the field.
        Applies only to repeated (array) types.
    max_occurs : int or None
        For repeated types, maximum number of objects allowed in the field.
        Applies only to repeated (array) types.
    min_length : int or None
        For strings and data, minimal length allowed for the string.
        Applies only to string and data types.
    max_length : int or None
        For strings and data, maximal length allowed for the string.
        Applies only to string and data types.
    length : int or None
        For strings and data, restrict the allowed lengths to that exact
        value.  Applies only to string and data types.
    cdata : bool
        For strings, when packing to XML, specifies if the the packer should
        use <!CDATA[ or XML quoting.
        Applies only to string.
    non_empty : bool
        For strings, forbids empty values.
        Applies only to string and data types.
    non_zero : bool
        Disallow the value 0 for the field.
        Applies only to numeric fields.
    pattern : str or None
        For strings, specifies a pattern the string must match.
        The pattern allows restricting the characters the string can contain.
        It uses a common regular-expression syntax.
    value : object
        The value of the static class field.
    """
    cdef readonly object value


cdef int struct_safe_init(StructBase py_st, dict kwargs) except -1:
    """Initialize the struct iopy python object with safe dictionary.

    The given kwargs dict has valid values and we can directly set it to
    the object.

    If the object has redefined a custom constructor, we still need to use
    it though.

    Parameters
    ----------
    py_st
        The struct python object.
    kwargs
        The valid dictionary of values for the struct.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef object cls_init = type(py_st).__init__

    if cls_init is StructBase.__init__:
        py_st.__dict__ = kwargs
    else:
        cls_init(py_st, **kwargs)
    return 0


cdef int t_struct_init_fields(StructBase py_st, const iop_struct_t *obj_st,
                              const iop_struct_t *current_st, dict kwargs,
                              void **empty_val, sb_t *err) except -2:
    """Init fields of structure from kwargs.

    Parameters
    ----------
    py_st
        The struct python object.
    obj_st
        The real structure description.
    current_st
        The current structure description. We will iterate on the parent
        classes with this variable.
    kwargs
        The dictionary of arguments.
    empty_val
        An empty value used when generating missing fields.
    err
        The string buffer containing the errors description for invalid
        fields.

    Returns
    -------
        -2 if an unexpected python error occurs, it will be handled by
        cython.
        -1 in case of invalid fields.
        0 otherwise.
    """
    cdef sb_buf_1k_t field_err_buf
    cdef sb_scope_t field_err = sb_scope_init_static(field_err_buf)
    cdef int res = 0
    cdef const iop_struct_t *parent_st
    cdef const iop_field_t *field
    cdef int i

    parent_st = get_iop_struct_parent(current_st)
    if parent_st:
        if t_struct_init_fields(py_st, obj_st, parent_st, kwargs, empty_val,
                                err) < 0:
            res = -1

    for i in range(current_st.fields_len):
        field = &current_st.fields[i]
        if t_struct_init_field(py_st, obj_st, field, kwargs, empty_val,
                               &field_err) < 0:
            if err.len > 0:
                sb_adds(err, "; ")
            sb_addsb(err, &field_err)
            sb_reset(&field_err)
            res = -1

    return res


cdef int t_struct_init_field(StructBase py_st, const iop_struct_t *st,
                             const iop_field_t *field, dict kwargs,
                             void **empty_val, sb_t *err) except -2:
    """Init field of structure from kwargs.

    Parameters
    ----------
    py_st
        The struct python object.
    st
        The structure description.
    field
        The field to init.
    kwargs
        The dictionary of arguments.
    empty_val
        An empty value used when generating missing fields.
    err
        The string buffer containing the errors description for invalid
        fields.

    Returns
    -------
        -2 if an unexpected python error occurs, it will be handled by
        cython.
        -1 in case of invalid fields.
        0 otherwise.
    """
    cdef str field_name_str
    cdef object py_obj

    field_name_str = lstr_to_py_str(field.name)
    if field.type == IOP_T_VOID:
        # When the field is of type void, we should ignore it if it is not
        # provided, or ignore it if it is provided but not optional,
        # or set it to None if it is provided and optional.
        if field_name_str in kwargs:
            struct_set_present_void_field(py_st, field, field_name_str)
        return 0

    py_obj = kwargs.get(field_name_str)
    if py_obj is None:
        return t_struct_init_missing_fields(py_st, st, field, field_name_str,
                                            empty_val, err)

    return struct_init_provided_field(py_st, st, field, field_name_str,
                                      py_obj, err)


cdef int struct_set_present_void_field(StructBase py_st,
                                       const iop_field_t *field,
                                       str field_name_str) except -1:
    """Set void field of structure.

    If the field is not optional, it is ignored.
    If the field is optional, it is set to None.

    Parameters
    ----------
    py_st
        The struct python object.
    field
        The field to init.
    field_name_str
        The python str name of the field.

    Returns
    -------
        -1 if an unexpected python error occurs. 0 otherwise.
    """
    if field.repeat == IOP_R_OPTIONAL:
        PyObject_GenericSetAttr(py_st, field_name_str, None)

    return 0


cdef int t_struct_init_missing_fields(StructBase py_st,
                                      const iop_struct_t *st,
                                      const iop_field_t *field,
                                      str field_name_str,
                                      void **empty_val,
                                      sb_t *err) except -2:
    """Init missing field of structure.

    Parameters
    ----------
    py_st
        The struct python object.
    st
        The structure description.
    field
        The field to init.
    field_name_str
        The python str name of the field
    empty_val
        An empty value used when generating missing fields.
    err
        The string buffer containing the errors description.

    Returns
    -------
        -2 if an unexpected python error occurs, it will be handled by
        cython.
        -1 in case of invalid fields.
        0 otherwise.
    """
    cdef cbool is_set = False
    cdef object py_obj

    if field.repeat == IOP_R_OPTIONAL:
        return 0

    if not empty_val[0]:
        empty_val[0] = mp_imalloc(t_pool(), st.size, 8, MEM_RAW)

    if iop_skip_absent_field_desc(t_pool(), empty_val[0], st, field) < 0:
        sb_addf(err, "field %*pM (type: ", LSTR_FMT_ARG(field.name))
        add_error_field_type(field, err)
        if field.repeat == IOP_R_REQUIRED:
            sb_adds(err, ") is required but absent")
        else:
            sb_adds(err, "[]) is not allowed: empty array")
        return -1

    py_obj = iop_field_to_py_obj(field, empty_val[0],
                                 struct_union_get_plugin(py_st), &is_set)
    PyObject_GenericSetAttr(py_st, field_name_str, py_obj)
    return 0


cdef int struct_init_provided_field(StructBase py_st, const iop_struct_t *st,
                                    const iop_field_t *field,
                                    str field_name_str, object py_obj,
                                    sb_t *err) except -2:
    """Init field with provided python object of structure.

    Parameters
    ----------
    py_st
        The struct python object.
    st
        The structure description.
    field
        The field to init.
    field_name_str
        The python str name of the field
    py_obj
        The provided python object for the field.
    err
        The string buffer containing the errors description.

    Returns
    -------
        -2 if an unexpected python error occurs, it will be handled by
        cython.
        -1 in case of invalid fields.
        0 otherwise.
    """
    cdef cbool is_valid = False
    cdef object py_res

    py_res = struct_convert_provided_field(py_st, st, field, py_obj,
                                           &is_valid, err)
    if not is_valid:
        sb_prependf(err, 'invalid argument `%*pM`: ',
                    LSTR_FMT_ARG(field.name))
        return -1

    PyObject_GenericSetAttr(py_st, field_name_str, py_res)
    return 0


cdef object struct_convert_provided_field(StructBase py_st,
                                          const iop_struct_t *st,
                                          const iop_field_t *field,
                                          object py_obj, cbool *is_valid,
                                          sb_t *err):
    """Convert provided python object of to be set as field of structure.

    Parameters
    ----------
    py_st
        The struct python object.
    st
        The structure description.
    field
        The field to init.
    py_obj
        The provided python object for the field.
    is_valid
        Set to True if the provided python object is valid and the result
        object can be used as field object.
    err
        The string buffer containing the errors description.

    Returns
    -------
        The converted object to be used as object for the field. None is
        it is not valid.
    """
    cdef Plugin plugin = struct_union_get_plugin(py_st)
    cdef cbool is_explicit = False
    cdef cbool recursive_constraints = False
    cdef object py_res
    cdef list py_list_arg
    cdef list py_list_res
    cdef Py_ssize_t py_size
    cdef object py_child
    cdef Py_ssize_t i

    if field.repeat != IOP_R_REPEATED:
        py_res = convert_field_object(field, py_obj, plugin, is_valid,
                                      &is_explicit, err)
        if not is_valid[0]:
            return None
        recursive_constraints = is_explicit
    elif not isinstance(py_obj, list):
        py_res = convert_field_object(field, py_obj, plugin, is_valid,
                                      &is_explicit, err)
        if not is_valid[0]:
            return None
        py_res = [py_res]
        recursive_constraints = True
    else:
        py_list_arg = <list>py_obj
        py_size = len(py_list_arg)
        py_list_res = py_list_arg[:]
        for i in range(py_size):
            py_child = py_list_arg[i]
            py_res = convert_field_object(field, py_child, plugin,
                                          is_valid, &is_explicit, err)
            if not is_valid[0]:
                sb_prependf(err, "invalid element list at index %ld: ", i)
                return None
            py_list_res[i] = py_res
        py_res = py_list_res
        recursive_constraints = True

    if check_field_constraints(st, field, py_res, recursive_constraints,
                               plugin, err) < 0:
        is_valid[0] = False
        return None

    is_valid[0] = True
    return py_res


cdef void iopslots_from_st(const iop_struct_t *st, sb_t *sb) nogil:
    """Create iopslots of a struct description.

    Parameters
    ----------
    st
        The structure description.
    sb
        The string buffer to populate with the iopslots.
    """
    cdef const iop_struct_t *parent = get_iop_struct_parent(st)
    cdef int i

    if parent:
        iopslots_from_st(parent, sb)

    for i in range(st.fields_len):
        if sb.len > 0:
            sb_adds(sb, ", ")
        sb_add_lstr(sb, st.fields[i].name)


cdef object static_field_value_to_python(iop_value_t value, iop_type_t t):
    """Convert static field value to python object.

    Parameters
    ----------
    value
        The iop static field value.
    t
        The iop static field type.
    """
    if (t == IOP_T_I8
     or t == IOP_T_I16
     or t == IOP_T_I32
     or t == IOP_T_I64
     or t == IOP_T_ENUM):
        return value.i
    elif (t == IOP_T_U8
       or t == IOP_T_U16
       or t == IOP_T_U32
       or t == IOP_T_U64):
        return value.u
    elif t == IOP_T_DOUBLE:
        return value.d
    elif t == IOP_T_BOOL:
        return value.b
    elif t == IOP_T_STRING or t == IOP_T_XML:
        return lstr_to_py_str(value.s)
    elif t == IOP_T_DATA:
        return lstr_to_py_bytes(value.s)
    else:
        raise Error('invalid iop static field type %d' % t)


cdef int populate_static_fields_cls(const iop_struct_t *st,
                                    dict statics) except -1:
    """Populate static fields dict with static fields of class.

    Parameters
    ----------
    st
        The iop class description.
    statics
        The dict to populate.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef const iop_class_attrs_t *attrs = st.class_attrs
    cdef int i
    cdef const iop_static_field_t *f
    cdef int t
    cdef object v

    if not attrs.static_fields or not iop_class_static_fields_have_type(st):
        return 0

    for i in range(attrs.static_fields_len):
        f = attrs.static_fields[i]
        t = iop_class_static_field_type(st, f)
        v = static_field_value_to_python(f.value, <iop_type_t>t)
        statics[lstr_to_py_str(f.name)] = v
    return 0


cdef int populate_static_fields_parent(const iop_struct_t *st,
                                       dict statics) except -1:
    """Populate static fields dict with static fields of parent classes.

    Parameters
    ----------
    st
        The iop class description.
    statics
        The dict to populate.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef const iop_struct_t *parent = st.class_attrs.parent

    if parent:
        populate_static_fields_parent(parent, statics)
        populate_static_fields_cls(parent, statics)
    return 0


cdef int class_init_iop_description(_InternalStructUnionType iop_type,
                                    IopClassDescription desc) except -1:
    """Init IOP class description from a C class iop type.

    Parameters
    ----------
    iop_type
        The class IOP type.
    desc
        The IOP class to init.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef Plugin plugin = iop_type.plugin
    cdef const iop_struct_t *st = iop_type.desc
    cdef const iop_class_attrs_t *attrs = st.class_attrs
    cdef object parent
    cdef dict cls_statics = {}
    cdef dict statics = {}

    if attrs.parent:
        parent = plugin_get_class_type_st(plugin, attrs.parent)
    else:
        parent = None

    class_init_static_iop_descriptions_cls(plugin, st, cls_statics)
    class_init_static_iop_descriptions_parent(plugin, st, statics)
    statics.update(cls_statics)

    struct_union_init_iop_description(iop_type, desc)
    desc.parent = parent
    desc.is_abstract = attrs.is_abstract != 0
    desc.is_private = attrs.is_private != 0
    desc.class_id = attrs.class_id
    desc.statics = statics
    desc.cls_statics = cls_statics

    return 0


cdef int class_init_static_iop_descriptions_cls(Plugin plugin,
                                                const iop_struct_t *st,
                                                dict statics) except -1:
    """Init own IOP class static fields descriptions of class.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The iop class description.
    statics
        The dict to init.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef const iop_class_attrs_t *attrs = st.class_attrs
    cdef int i
    cdef const iop_static_field_t *field
    cdef iop_type_t ftype
    cdef IopClassStaticFieldDescription desc

    if not attrs.static_fields or not iop_class_static_fields_have_type(st):
        return 0

    for i in range(attrs.static_fields_len):
        field = attrs.static_fields[i]
        ftype = <iop_type_t>field.type

        desc = IopClassStaticFieldDescription.__new__(
            IopClassStaticFieldDescription)
        struct_union_make_iop_field_description(plugin, NULL, IOP_R_REQUIRED,
                                                ftype, field.attrs, desc)
        desc.value = static_field_value_to_python(field.value, ftype)

        statics[lstr_to_py_str(field.name)] = desc
    return 0


cdef int class_init_static_iop_descriptions_parent(Plugin plugin,
                                                   const iop_struct_t *st,
                                                   dict statics) except -1:
    """Init IOP class static fields descriptions of parent classes of class.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The iop class description.
    statics
        The dict to init.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef const iop_struct_t *parent = st.class_attrs.parent

    if parent:
        class_init_static_iop_descriptions_parent(plugin, parent, statics)
        class_init_static_iop_descriptions_cls(plugin, parent, statics)
    return 0


cdef str struct_get_iopslots(object cls):
    """ Return a list of all available IOP slots.

    Parameters
    ----------
    cls
        The struct iop class type.

    Returns
    -------
    str
        The list of IOP slots.
    """
    cdef sb_buf_1k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef const iop_struct_t *st

    st = struct_union_get_iop_type_cls(cls).desc
    iopslots_from_st(st, &sb)
    return lstr_to_py_str(LSTR_SB_V(&sb))


cdef dict struct_get_class_attrs(object cls):
    """Return the class attributes.

    Parameters
    ----------
    cls
        The struct iop class type.

    Returns
    -------
    dict
        If the iop structure is not a class then return None.
        Else, return a dictionary with the following entries:
        base : str
            The fullname of the base class. It is an empty string when
            there is no base class.
        statics : dict
            Dictionary whose keys are the static fields names and values
            are statics fields values. It is an empty dictionary when
            there is no static fields.
        cls_statics : dict
            Dictionary which contains the class's own static fields.
        is_abstract : bool
            Notices if the class is abstract.
    """
    cdef const iop_struct_t *st
    cdef const iop_class_attrs_t *attrs
    cdef dict cls_statics
    cdef dict statics
    cdef object base

    st = struct_union_get_iop_type_cls(cls).desc
    if not iop_struct_is_class(st):
        return None

    attrs = st.class_attrs

    cls_statics = {}
    populate_static_fields_cls(st, cls_statics)

    statics = {}
    populate_static_fields_parent(st, statics)
    statics.update(cls_statics)

    if attrs.parent:
        base = lstr_to_py_str(attrs.parent.fullname)
    else:
        base = ''

    return {
        'base': base,
        'statics': statics,
        'cls_statics': cls_statics,
        'is_abstract': attrs.is_abstract,
    }


cdef dict struct_export_to_dict(StructBase py_obj,
                                const iop_struct_t *base_st,
                                unsigned flags):
    """Deeply convert the struct object to a dict.

    It is a faster version of doing `json.loads(obj.to_json())`.

    Parameters
    ----------
    py_obj
        The struct python object to export.
    base_st
        The base structure description from the structure field.
        Can be NULL if py_obj is from a field.
    flags
        The jpack flags used to dump the dict.

    Returns
    -------
        The dict containing the values of the struct object.
    """
    cdef _InternalStructUnionType iop_type
    cdef const iop_struct_t *st
    cdef Plugin plugin
    cdef dict res

    iop_type = struct_union_get_iop_type(py_obj)
    st = iop_type.desc
    plugin = iop_type.plugin

    res = {}

    if (iop_struct_is_class(st) and
            not (flags & IOP_JPACK_SKIP_CLASS_NAMES) and
            not ((flags & IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES) and
                 (not base_st or base_st == st))):
        res['_class'] = lstr_to_py_str(st.fullname)

    while st:
        struct_fill_fields_dict(plugin, py_obj, st, flags, res)
        st = get_iop_struct_parent(st)
    return res


cdef int struct_fill_fields_dict(Plugin plugin, StructBase py_obj,
                                 const iop_struct_t *st, unsigned flags,
                                 dict res) except -1:
    """Fill the dict of fields of the struct.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    py_obj
        The struct python object to export.
    st
        The struct description.
    flags
        The jpack flags used to dump the dict.
    res
        The dictionary of fields to fill.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef int i
    cdef str py_field_name
    cdef const iop_field_t *field
    cdef cbool is_skipped
    cdef object py_dict_field

    for i in range(st.fields_len):
        field = &st.fields[i]
        py_field_name = lstr_to_py_str(field.name)

        is_skipped = False
        py_dict_field = struct_fill_fields_dict_field(plugin, py_obj, st,
                                                      field, py_field_name,
                                                      flags, &is_skipped)
        if not is_skipped:
            res[py_field_name] = py_dict_field


cdef object struct_fill_fields_dict_field(Plugin plugin, StructBase py_obj,
                                          const iop_struct_t *st,
                                          const iop_field_t *field,
                                          str py_field_name,
                                          unsigned flags, cbool *is_skipped):
    """Get field of the struct to be exported to a dict.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    py_obj
        The struct python object to export.
    st
        The struct description.
    field
        The field to export.
    py_field_name
        The name of the field as a Python string.
    flags
        The jpack flags used to dump the dict.
    is_skipped
        Set to True if the field should be skipped and not put in the dict.

    Returns
    -------
        The object of the field or None with is_skipped set to True if the
        field should be skipped.
    """
    cdef const iop_field_attrs_t *attrs
    cdef object py_field_obj
    cdef list py_field_list
    cdef object py_dict_field

    if flags & IOP_JPACK_SKIP_PRIVATE:
        attrs = iop_field_get_attrs(st, field)
        if attrs and TST_BIT(&attrs.flags, IOP_FIELD_PRIVATE):
            is_skipped[0] = True
            return None

    try:
        py_field_obj = getattr(py_obj, py_field_name)
    except AttributeError:
        # Optional
        is_skipped[0] = True
        return None

    if field.repeat == IOP_R_REPEATED:
        py_field_list = py_field_obj

        if (flags & IOP_JPACK_SKIP_EMPTY_ARRAYS) and not py_field_list:
            is_skipped[0] = True
            return None

        py_dict_field = [
            struct_union_export_dict_field(plugin, False, x, field, flags,
                                           NULL)
            for x in py_field_list
        ]
    else:
        py_dict_field = struct_union_export_dict_field(
            plugin, False, py_field_obj, field, flags, is_skipped)

    return py_dict_field

# }}}
# }}}
# {{{ RPCs
# {{{ Base classes


cdef class ChannelBase:
    """Base class for IC channel"""

    def __init__(Module self):
        """Channel initialization is not supported"""
        raise TypeError('Channel initialization is not supported, use '
                        'connect() instead')


@cython.internal
cdef class _InternalModuleHolder(type):
    """Internal class to hold C IOP module description"""
    cdef int refcnt
    cdef const iop_mod_t *module

    def __repr__(_InternalModuleHolder cls):
        """Return the representation of the IOP module.

        Returns
        -------
        str
            The representation of the IOP module.
        """
        cdef str module_fullname = lstr_to_py_str(cls.module.fullname)
        cdef list ifaces_name = []
        cdef uint16_t i
        cdef const iop_iface_alias_t *iface_alias

        for i in range(cls.module.ifaces_len):
            iface_alias = &cls.module.ifaces[i]
            ifaces_name.append(lstr_to_py_str(iface_alias.name))
        ifaces_name.sort()

        return 'Module %s (Interfaces: %s)' % (module_fullname, ifaces_name)


cdef class Module:
    """Base class for IOP module"""

    def __init__(Module self):
        """Module initialization is not supported"""
        raise TypeError('Module initialization is not supported')

    def __repr__(Module self):
        """Return the representation of the IOP module.

        Returns
        -------
        str
            The representation of the IOP module.
        """
        return repr(type(self))

    @classmethod
    def __fullname__(object cls):
        """Return the fullname of the IOP module.

        Returns
        -------
        str
            The fullname of the IOP module.
        """
        cdef _InternalModuleHolder holder = cls

        return lstr_to_py_str(holder.module.fullname)


@cython.internal
@cython.final
cdef class IfaceRpcsContainer:
    """RPCs container for IfaceBase"""
    cdef dict __dict__


@cython.internal
@cython.final
cdef class _InternalIfaceHolder(_InternalBaseHolder):
    """Internal class to hold C IOP interface description"""
    cdef const iop_iface_t *iface
    cdef int refcnt


cdef object _InternalIfaceNameWrapper
class _InternalIfaceNameWrapper(str):
    """Internal class to make __name__ of iface class to act like both as a
    property and as a method.
    """
    def __call__(self):
        """Used when __name__ is used as a method."""
        return self


@cython.internal
cdef class _InternalIfaceBaseMetaclass(type):
    """Internal base metaclass for interface types"""
    cdef object name_wrapper

    def __cinit__(_InternalIfaceBaseMetaclass cls, name, bases, attrs):
        """Cython constructor for _InternalIface class"""
        cls.name_wrapper = _InternalIfaceNameWrapper(name)

    def __repr__(_InternalIfaceBaseMetaclass cls):
        """Return the representation of the IOP interface.

        Returns
        -------
        str
            The representation of the IOP interface.
        """
        cdef _InternalIfaceHolder holder = type(cls)
        cdef str iface_fullname = lstr_to_py_str(holder.iface.fullname)
        cdef list rpcs_name = []
        cdef uint16_t i
        cdef const iop_rpc_t *rpc

        for i in range(holder.iface.funs_len):
            rpc = &holder.iface.funs[i]
            rpcs_name.append(lstr_to_py_str(rpc.name))
        rpcs_name.sort()

        return 'Interface %s (RPCs: %s)' % (iface_fullname, rpcs_name)

    @property
    def __name__(_InternalIfaceBaseMetaclass cls):
        """Name property that can also act like a method"""
        return cls.name_wrapper


@cython.internal
cdef class _InternalIface:
    """Internal base class for IOP interface.

    Cython extension type does not support setting __pre_hook__ and
    __post_hook__ as class attributes. So we need another plain python class
    to do it.
    """
    cdef readonly IfaceRpcsContainer _rpcs
    cdef readonly ChannelBase channel
    cdef const iop_iface_alias_t *iface_alias

    def __cinit__(_InternalIface self):
        """Cython contructor of _InternalIface"""
        self._rpcs = IfaceRpcsContainer.__new__(IfaceRpcsContainer)

    def __repr__(_InternalIface self):
        """Return the representation of the IOP interface.

        Returns
        -------
        str
            The representation of the IOP interface.
        """
        return repr(type(self))

    @classmethod
    def __fullname__(object cls):
        """Return the fullname of the IOP interface.

        Returns
        -------
        str
            The fullname of the IOP interface.
        """
        cdef _InternalIfaceHolder holder = type(cls)

        return lstr_to_py_str(holder.iface.fullname)

    def __name__(_InternalIface self):
        """Return the name of the IOP interface.

        Returns
        -------
        str
            The name of the IOP interface.
        """
        return lstr_to_py_str(self.iface_alias.name)


@cython.warn.undeclared(False)
class IfaceBase(_InternalIface):
    """Base class for IOP interface.

    It is possible to add a pre hook and a post hook to the RPCs of the
    interface by setting the __pre_hook__ and __post_hook__ methods.

    __pre_hook__

    Parameters
    ----------
    rpc : iopy.RPC
        The IOPy RPC python object.
    *args
        The positional arguments passed to the call of the RPC.
    **kwargs
        The named arguments passed to the call of the RPC.

    Returns
    -------
    None or (args, kwargs)
        If None is returned by the pre hook, the original arguments are used.
        Otherwise, the pre hook must return a tuple containing the new
        named arguments and positional arguments used for the RPC.


    __post_hook__

    Parameters
    ----------
    rpc : iopy.RPC
        The IOPy RPC python object.
    res : object
        The query result of the RPC.

    Returns
    -------
    None or object
        If None is returned by the post hook, the original result is used.
        Otherwise, the object returned by the post hook is used as the query
        result of the RPC.
    """
    pass


# For backward compatibility
Iface = IfaceBase


cdef class RPCBase:
    """Base class for IOP RPC"""
    cdef const iop_rpc_t *rpc
    cdef _InternalIfaceHolder iface_holder

    def __init__(RPCBase self):
        """RPC initialization is not supported"""
        raise TypeError('RPC initialization is not supported')

    def name(RPCBase self):
        """Get the name of the RPC.

        Returns
        -------
        str
            The name of the RPC.
        """
        return lstr_to_py_str(self.rpc.name)

    def async(RPCBase self):
        """Return the RPC asyncness.

        Returns
        -------
        bool
            True if the RPC is asynchronous, False otherwise.
        """
        return self.rpc.async

    def arg(RPCBase self):
        """Return the arguments IOPy type.

        Returns
        -------
        type
            The IOPy type for the RPC arguments. None if the RPC has no
            arguments.
        """
        if not self.rpc.args:
            return None
        return plugin_get_class_type_st(self.iface_holder.plugin,
                                        self.rpc.args)

    def res(RPCBase self):
        """Return the result IOPy type.

        Returns
        -------
        type
            The IOPy type for the RPC results. None if the RPC has no
            results.
        """
        if not self.rpc.result:
            return None
        return plugin_get_class_type_st(self.iface_holder.plugin,
                                        self.rpc.result)

    def exn(RPCBase self):
        """Return the exception IOPy type.

        Returns
        -------
        type
            The IOPy type for the RPC exception. None if the RPC has no
            exception.
        """
        if not self.rpc.exn:
            return None
        return plugin_get_class_type_st(self.iface_holder.plugin,
                                        self.rpc.exn)

    def desc(RPCBase self):
        """Return a description of the RPC.

        Returns
        -------
        str
            The description of the RPC.
        """
        cdef str rpc_name
        cdef object arg_type

        rpc_name = 'RPC ' + lstr_to_py_str(self.rpc.name)
        if not self.rpc.args:
            return rpc_name
        arg_type = plugin_get_class_type_st(self.iface_holder.plugin,
                                            self.rpc.args)
        return '%s, argument: %s' % (rpc_name, arg_type.get_desc())

    def __repr__(RPCBase self):
        """Return the representation of the RPC.

        Returns
        -------
        str
            The representation of the RPC.
        """
        cdef const iop_iface_t *iface = self.iface_holder.iface
        cdef str iface_fullname = lstr_to_py_str(iface.fullname)

        return 'RPC %s::%s' % (iface_fullname, lstr_to_py_str(self.rpc.name))


@cython.internal
cdef class RPCChannel(RPCBase):
    """RPC created from a channel.

    It will hold a reference to the instance of the interface created for the
    channel.
    """
    cdef _InternalIface py_iface

    @property
    def channel(RPCChannel self):
        """Get the channel object for this RPC.

        Returns
        -------
        ChannelBase
            The channel object of the RPC.
        """
        return self.py_iface.channel


ctypedef int (*rpc_create_b)(const iop_rpc_t *rpc,
                             _InternalIfaceHolder iface_holder,
                             _InternalIface py_iface) except -1


cdef int create_modules_of_channel(Plugin plugin, ChannelBase channel,
                                   rpc_create_b rpc_create_cb) except -1:
    """Create the modules of a channel instance.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    channel
        The IOPy channel instance.
    rpc_type
        The RPC class type for the connection.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef dict short_modules = {}
    cdef object module_name
    cdef object module_py_name
    cdef _InternalModuleHolder module_cls
    cdef Module module
    cdef Py_ssize_t pos
    cdef object module_short_name
    cdef object short_py_obj

    for module_name, module_cls in plugin.modules.iteritems():
        module_py_name = make_py_pkg_name(module_name)
        module = create_module(module_cls, channel, rpc_create_cb)
        setattr(channel, module_py_name, module)

        pos = module_py_name.rfind('_')
        module_short_name = module_py_name[pos + 1:]
        if module_short_name in short_modules:
            short_modules[module_short_name] = False
        else:
            short_modules[module_short_name] = module

    # When there is no collision with other module, add a link with the short
    # name (aka only module name) in the same dictionary, to get quick access
    # for users.
    for module_short_name, short_py_obj in short_modules.iteritems():
        if short_py_obj is not False:
            setattr(channel, module_short_name, short_py_obj)


cdef Module create_module(_InternalModuleHolder cls, ChannelBase channel,
                          rpc_create_b rpc_create_cb):
    """Create instance of module from class type and channel.

    Parameters
    ----------
    cls
        The class type of the module.
    channel
        The IC connection channel.
    rpc_type
        The RPC class type for the connection.

    Returns
    -------
        An instance of the module for the given connection.
    """
    cdef Module res = <Module>cls.__new__(cls)
    cdef uint16_t i
    cdef const iop_iface_alias_t *iface_alias
    cdef str iface_name
    cdef object iface_cls
    cdef _InternalIface py_iface

    for i in range(cls.module.ifaces_len):
        iface_alias = &cls.module.ifaces[i]
        iface_name = lstr_to_py_str(iface_alias.name)
        iface_cls = getattr(cls, iface_name)
        py_iface = create_interface(iface_cls, channel, iface_alias,
                                    rpc_create_cb)
        setattr(res, iface_name, py_iface)

    return res


cdef _InternalIface create_interface(object cls, ChannelBase channel,
                                     const iop_iface_alias_t *iface_alias,
                                     rpc_create_b rpc_create_cb):
    """Create instance of module from class type and channel.

    Parameters
    ----------
    cls
        The class type of the interface.
    channel
        The IC connection channel.
    rpc_type
        The RPC class type for the connection.
    iface_alias
        The alias of the interface in the module.

    Returns
    -------
        An instance of the interface for the given connection.
    """
    cdef _InternalIfaceHolder holder = type(cls)
    cdef const iop_iface_t *iface = holder.iface
    cdef _InternalIface res = <_InternalIface>cls()
    cdef uint16_t i
    cdef const iop_rpc_t *rpc

    res.iface_alias = iface_alias
    res.channel = channel

    for i in range(iface.funs_len):
        rpc = &iface.funs[i]
        rpc_create_cb(rpc, holder, res)

    return res


# }}}
# {{{ Client RPC


cdef class RPC(RPCChannel):
    """RPC class for client IC channel"""

    def call(RPC self, *args, **kwargs):
        """Call the RPC for the associated channel.

        All keyword arguments are considered as input structure argument,
        except the one below.
        It is also possible to directly give the RPC argument as a positional
        argument of the correct type.

        Parameters
        ----------
        _timeout : int
            The timeout for the query. If set, it will superseed the default
            timeout. -1 means forever.
        _login : str
            The login to be put in the query IC header.
        _group : str
            The group to be put in the query IC header.
        _password : str
            The password to be put in the query IC header.
        _kind : str
            The kind to be put in the query IC header.
        _workspace_id : int
            The id of workspace to be put in the query IC header.
        _dealias : bool
            The dealias flag to be put in the query IC header.
        _hdr : ic.SimpleHdr
            The IC header to be used for this query. If set, the
            above arguments must not be set.

        Returns
        -------
        object
            The result of the RPC.
        """
        return client_channel_call_rpc(self, args, kwargs)

    def __call__(RPC self, *args, **kwargs):
        """Call the RPC for the associated channel.

        See call method.
        """
        return client_channel_call_rpc(self, args, kwargs)


@cython.internal
cdef class RPCImplWrapper:
    """RPC wrapper when the interface has a custom implementation of the RPC.
    """
    cdef RPC py_rpc
    cdef object cls_impl

    def call(RPCImplWrapper self, *args, **kwargs):
        """Call the RPC for the associated channel.

        See RPC::call.
        """
        return self.cls_impl(self.py_rpc.py_iface, *args, **kwargs)

    def __call__(RPCImplWrapper self, *args, **kwargs):
        """Call the RPC for the associated channel.

        See RPC::call.
        """
        return self.cls_impl(self.py_rpc.py_iface, *args, **kwargs)

    def __getattr__(RPCImplWrapper self, object name):
        """Get the attribute of the RPC"""
        return getattr(self.py_rpc, name)


cdef class Channel(ChannelBase):
    """Class for client IC channel"""
    cdef dict __dict__
    cdef Plugin plugin
    cdef ic__hdr__t *def_hdr
    cdef iopy_ic_client_t *ic_client
    cdef readonly str uri
    cdef public int default_timeout

    def __init__(Channel self, Plugin plugin, object uri=None, *,
                 object host=None, int port=-1, int default_timeout=60,
                 **kwargs):
        """Constructor of client IC channel.

        Parameters
        ----------
        plugin :iopy.Plugin
            The IOPy plugin.
        uri : str
            The URI to connect to. This is the only allowed positional
            argument.
        host : str
            The host to connect to. If set, port must also be set and uri must
            not be set.
        port : int
            The port to connect to. If set, host must also be set and uri must
            not be set.
        default_timeout : int
            The default timeout for the IC channel in seconds.
            -1 means forever, default is 60.
        _login : str
            The login to be put in the default IC header.
        _group : str
            The group to be put in the default IC header.
        _password : str
            The password to be put in the default IC header.
        _kind : str
            The kind to be put in the default IC header.
        _workspace_id : int
            The id of workspace to be put in the default IC header.
        _dealias : bool
            The dealias flag to be put in the default IC header.
        _hdr : ic.SimpleHdr
            The default IC header to be used for this channel. If set, the
            above arguments must not be set.
        """
        client_channel_init(self, plugin, uri, host, port, default_timeout,
                            kwargs)

    def __dealloc__(Channel self):
        """Destructor of client IC channel"""
        p_delete(<void **>&self.def_hdr)
        if self.ic_client:
            iopy_ic_client_set_py_obj(self.ic_client, NULL)
            with nogil:
                iopy_ic_client_destroy(&self.ic_client)

    def __repr__(Channel self):
        """Return the representation of the client IC channel.

        Returns
        -------
        str
            The representation of the client IC channel.
        """
        cdef list modules = list(self.__dict__.keys())

        modules.sort()
        return 'Channel to %s (Modules: %s)' % (self.uri, modules)

    def connect(Channel self, object timeout=None):
        """Connect the client IC channel.

        Parameters
        ----------
        timeout : int
            The timeout of the connection of the IC channel in seconds.
            -1 means forever. If not set, use the default timeout of the IC
            channel.
        """
        cdef int timeout_connect

        if timeout is not None:
            timeout_connect = int(timeout)
        else:
            timeout_connect = self.default_timeout
        client_channel_connect(self, timeout_connect)

    def is_connected(Channel self):
        """Returns whether the associated IC is connected or not.

        Returns
        -------
        bool
            True if the associated IC channel is connected, False otherwise.
        """
        cdef cbool res

        with nogil:
            res = iopy_ic_client_is_connected(self.ic_client)
        return res

    def disconnect(Channel self):
        """Disconnect from the associated IC"""
        with nogil:
            iopy_ic_client_disconnect(self.ic_client)

    def change_default_hdr(Channel self, **kwargs):
        """Change the default header used when performing queries with this
        channel.

        Parameters
        ----------
        _login : str
            The login to be put in the query IC header.
        _group : str
            The group to be put in the query IC header.
        _password : str
            The password to be put in the query IC header.
        _kind : str
            The kind to be put in the query IC header.
        _workspace_id : int
            The id of workspace to be put in the query IC header.
        _dealias : bool
            The dealias flag to be put in the query IC header.
        _hdr : ic.SimpleHdr
            The IC header to be used for this query. If set, the
            above arguments must not be set.
        """
        cdef t_scope_t t_scope_guard = t_scope_init()
        cdef ic__hdr__t *hdr = NULL

        t_scope_ignore(t_scope_guard)
        t_set_ic_hdr_from_kwargs(self.plugin, kwargs, &hdr)
        if not hdr:
            raise Error('no header provided')
        p_delete(<void **>&self.def_hdr)
        self.def_hdr = iop_dup_ic_hdr(hdr)

    def get_default_hdr(Channel self):
        """Get the default header used when performing queries with this
        channel.

        Returns
        -------
        iopy.ic.Hdr
            The default header or None if not set.
        """
        cdef object py_hdr_cls

        if not self.def_hdr:
            return None

        py_hdr_cls = plugin_get_class_type_st(self.plugin, &ic__hdr__s)
        return iop_c_val_to_py_obj(py_hdr_cls, &ic__hdr__s, self.def_hdr,
                                   self.plugin)


cdef int client_channel_init(Channel channel, Plugin plugin, object uri,
                             object host, int port, int default_timeout,
                             dict kwargs) except -1:
    """Initialize client IC channel.

    Parameters
    ----------
    channel
        The client IC channel to initialize.
    plugin
        The IOPy plugin.
    uri
        The URI to connect to.
    host
        The host to connect to. If set, port must also be set and uri must
        not be set.
    port
        The port to connect to. If set, host must also be set and uri must
        not be set.
    default_timeout
        The default timeout for the IC channel in seconds.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef ic__hdr__t *def_hdr = NULL
    cdef lstr_t uri_lstr
    cdef iopy_ic_client_t *ic_client

    t_scope_ignore(t_scope_guard)

    create_modules_of_channel(plugin, channel, &create_client_rpc)

    t_set_ic_hdr_from_kwargs(plugin, kwargs, &def_hdr)

    t_parse_uri_arg(uri, host, port, &uri_lstr)
    with nogil:
        ic_client = iopy_ic_client_create(uri_lstr, &err)

    if not ic_client:
        raise Error(lstr_to_py_str(LSTR_SB_V(&err)))

    channel.plugin = plugin
    channel.ic_client = ic_client
    channel.uri = lstr_to_py_str(uri_lstr)
    channel.default_timeout = default_timeout
    if def_hdr:
        channel.def_hdr = iop_dup_ic_hdr(def_hdr)
    iopy_ic_client_set_py_obj(ic_client, <void*>channel)

    return 0


cdef int create_client_rpc(const iop_rpc_t *rpc,
                           _InternalIfaceHolder iface_holder,
                           _InternalIface py_iface) except -1:
    """Callback called to create the client RPC of interface.

    Parameters
    ----------
    rpc
        The IOP RPC description.
    iface_holder
        The _InternalIfaceHolder for the interface.
    py_iface
        The interface object instance where to set the RPC.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef RPC py_rpc = RPC.__new__(RPC)
    cdef str rpc_name
    cdef object py_cls_base
    cdef object py_cls_rpc
    cdef RPCImplWrapper wrapper

    py_rpc.rpc = rpc
    py_rpc.iface_holder = iface_holder
    py_rpc.py_iface = py_iface

    rpc_name = lstr_to_py_str(rpc.name)
    setattr(py_iface._rpcs, rpc_name, py_rpc)

    # Set the py_rpc directly to the interface if it has not been overridden
    # in the class. Otherwise use a RPCImplWrapper
    py_cls_base = type(py_iface).__base__
    py_cls_rpc = getattr(py_cls_base, rpc_name)
    if isinstance(py_cls_rpc, RPCBase):
        setattr(py_iface, rpc_name, py_rpc)
    else:
        wrapper = RPCImplWrapper.__new__(RPCImplWrapper)
        wrapper.py_rpc = py_rpc
        wrapper.cls_impl = py_cls_rpc
        setattr(py_iface, rpc_name, wrapper)


cdef int client_channel_connect(Channel channel, int timeout) except -1:
    """Initialize client IC channel.

    Parameters
    ----------
    channel
        The client IC channel to connect.
    timeout
        The connection timeout in seconds.
    """
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef iopy_ic_res_t res

    with nogil:
        res = iopy_ic_client_connect(channel.ic_client, timeout, &err)
    check_iopy_ic_res(res, &err)
    return 0


cdef object client_channel_call_rpc(RPC rpc, tuple args, dict kwargs):
    """Call the RPC for the associated channel.

    See RPC::call for parameters documentation.

    Returns
    -------
        The result of the RPC or None in case of asynchronous RPC.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef sb_buf_1k_t err_buf
    cdef sb_scope_t err = sb_scope_init_static(err_buf)
    cdef _InternalIfaceHolder iface_holder = rpc.iface_holder
    cdef Plugin plugin = iface_holder.plugin
    cdef _InternalIface py_iface = rpc.py_iface
    cdef Channel channel = py_iface.channel
    cdef int timeout = channel.default_timeout
    cdef tuple pre_hook_res
    cdef object py_timeout
    cdef ic__hdr__t *hdr = NULL
    cdef object py_arg_cls
    cdef object py_arg
    cdef StructUnionBase py_input
    cdef void *ic_input = NULL
    cdef int32_t cmd
    cdef iopy_ic_res_t call_res
    cdef ic_status_t ic_status = IC_MSG_OK
    cdef void *ic_res = NULL
    cdef object py_res_cls
    cdef object py_res
    cdef object py_exn_cls
    cdef object py_exn

    t_scope_ignore(t_scope_guard)

    pre_hook_res = client_channel_do_pre_hook(rpc, args, kwargs)
    args = <tuple>(pre_hook_res[0])
    kwargs = <dict>(pre_hook_res[1])

    py_timeout = kwargs.pop('_timeout', None)
    if py_timeout is not None:
        timeout = py_timeout

    t_set_ic_hdr_from_kwargs(plugin, kwargs, &hdr)
    if not hdr:
        hdr = channel.def_hdr

    py_arg_cls = plugin_get_class_type_st(plugin, rpc.rpc.args)

    if args:
        if len(args) != 1:
            raise Error('Only one argument allowed: struct, union or dict')

        py_arg = args[0]

        if not isinstance(py_arg, py_arg_cls):
            if issubclass(py_arg_cls, UnionBase) or isinstance(py_arg, dict):
                py_arg = py_arg_cls(py_arg)
            else:
                raise Error('Incompatible type for argument')
    else:
        py_arg = py_arg_cls(**kwargs)

    py_input = py_arg
    mp_iop_py_obj_to_c_val(t_pool(), False, py_input, &ic_input)
    cmd = get_iface_rpc_cmd(py_iface.iface_alias, rpc.rpc)

    with nogil:
        call_res = iopy_ic_client_call(channel.ic_client, rpc.rpc, cmd, hdr,
                                       timeout, ic_input, &ic_status, &ic_res,
                                       &err)

    check_iopy_ic_res(call_res, &err)

    if rpc.rpc.async:
        return None

    try:
        if ic_status == IC_MSG_OK:
            py_res_cls = plugin_get_class_type_st(plugin, rpc.rpc.result)
            py_res = iop_c_val_to_py_obj(py_res_cls, rpc.rpc.result, ic_res,
                                         plugin)
            py_res = client_channel_do_post_hook(rpc, py_res)
            return py_res
        elif ic_status == IC_MSG_EXN:
            py_exn_cls = plugin_get_class_type_st(plugin, rpc.rpc.exn)
            py_exn = iop_c_val_to_py_obj(py_exn_cls, rpc.rpc.exn, ic_res,
                                         plugin)
            raise RpcError(py_exn)
        else:
            raise Error('Query `%s` on object %s failed with status: '
                        '%d (%s)' % (lstr_to_py_str(rpc.rpc.name),
                                     py_input, ic_status,
                                     ic_status_to_string(ic_status)))
    finally:
        p_delete(&ic_res)


cdef tuple client_channel_do_pre_hook(RPC rpc, tuple args, dict kwargs):
    """Execute the pre hook for the RPC interface if needed.

    Parameters
    ----------
    rpc
        The RPC python object.
    args
        The tuple of positional arguments for the RPC.
    kwargs
        The dictionary of named arguments for the RPC.

    Returns
    -------
    (tuple args, dict kwargs)
        A tuple containing the new positional and name arguments to be used.
    """
    cdef _InternalIface py_iface = rpc.py_iface
    cdef object pre_hook
    cdef object res

    pre_hook = getattr(py_iface, '__pre_hook__', None)
    if pre_hook is None:
        return args, kwargs

    res = pre_hook(rpc, *args, **kwargs)
    if res is None:
        return args, kwargs

    if not validate_pre_hook_res(res):
        raise Error('bad returned value %s; of __pre_hook__ in rpc %s; '
                    'when not None the returned value should be: '
                    '(args, kwargs)' % (res, rpc))

    return <tuple>res


cdef cbool validate_pre_hook_res(object res):
    """Validate that the result of pre hook is of expected type.

    Parameters
    ----------
    res
        The result of the pre hook

    Returns
    -------
        True if the result is valid, False otherwise.
    """
    cdef tuple tres
    cdef object res_args
    cdef object res_kwargs

    if not isinstance(res, tuple):
        return False

    tres = <tuple>res
    if len(tres) != 2:
        return False

    res_args = tres[0]
    if not isinstance(res_args, tuple):
        return False

    res_kwargs = tres[1]
    if not isinstance(res_kwargs, dict):
        return False
    return True


cdef object client_channel_do_post_hook(RPC rpc, object res):
    """Execute the post hook for the RPC interface if needed.

    Parameters
    ----------
    rpc
        The RPC python object.
    res
       The result of the RPC.

    Returns
    -------
    object
        The new result to be used.
    """
    cdef _InternalIface py_iface = rpc.py_iface
    cdef object post_hook
    cdef object pres

    post_hook = getattr(py_iface, '__post_hook__', None)
    if post_hook is None:
        return res

    pres = post_hook(rpc, res)
    if pres is None:
        return res
    return pres


cdef int t_set_ic_hdr_from_kwargs(Plugin plugin, dict kwargs,
                                  ic__hdr__t **res) except -1:
    """Set ic hdr from kwargs dict.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    kwargs
        The dictionary of kwargs. The arguments corresponding to the ic hdr
        will be removed from the dictionary.
    res
        Set to the created ic__hdr__t or NULL if empty.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef object py_hdr
    cdef object py_login
    cdef object py_password
    cdef object py_kind
    cdef object py_group
    cdef object py_workspace_id
    cdef object py_dealias
    cdef cbool kwargs_has_hdr_fields
    cdef object py_hdr_cls
    cdef ic__simple_hdr__t simple_hdr

    py_hdr = kwargs.pop('_hdr', None)
    py_login = kwargs.pop('_login', None)
    py_password = kwargs.pop('_password', None)
    py_kind = kwargs.pop('_kind', None)
    py_group = kwargs.pop('_group', None)
    py_workspace_id = kwargs.pop('_workspace_id', None)
    py_dealias = kwargs.pop('_dealias', None)

    kwargs_has_hdr_fields = (py_login is not None or py_password is not None
                          or py_kind is not None or py_group is not None
                          or py_workspace_id is not None
                          or py_dealias is not None)

    if py_hdr is not None:
        if kwargs_has_hdr_fields:
            raise Error('unable to mix _login, _password, _kind, _group, '
                        '_workspace_id and _dealias arguments with _hdr')

        py_hdr_cls = plugin_get_class_type_st(plugin, &ic__hdr__s)

        if not isinstance(py_hdr, py_hdr_cls):
            raise Error('_hdr is not a valid header, expected instance of '
                        'type `%s`, got `%s`' % (py_hdr_cls, type(py_hdr)))

        res[0] = NULL
        mp_iop_py_obj_to_c_val(t_pool(), False, py_hdr, <void **>res)

        if not is_ic_hdr_simple_hdr(res[0]):
            raise Error('only simple header are supported for _hdr argument')

        res[0].simple.source = LSTR('python')
        return 0

    if not kwargs_has_hdr_fields:
        return 0

    iop_init_ic_simple_hdr(&simple_hdr)
    simple_hdr.source = LSTR('python')

    if py_login is not None:
        simple_hdr.login = t_py_obj_to_lstr(py_login)

    if py_password is not None:
        simple_hdr.password = t_py_obj_to_lstr(py_password)

    if py_kind is not None:
        simple_hdr.kind = t_py_obj_to_lstr(py_kind)

    if py_group is not None:
        simple_hdr.group = t_py_obj_to_lstr(py_group)

    if py_workspace_id is not None:
        simple_hdr.workspace_id.has_field = True
        simple_hdr.workspace_id.v = py_workspace_id

    if py_dealias is not None:
        simple_hdr.dealias.has_field = True
        simple_hdr.dealias.v = py_dealias

    res[0] = t_iop_new_ic_hdr()
    res[0][0] = iop_ic_hdr_from_simple_hdr(simple_hdr)
    return 0


cdef public void iopy_ic_py_client_on_disconnect(iopy_ic_client_t *client,
                                                 cbool connected) nogil:
    """Called when the client is disconnecting.

    Parameters
    ----------
    client
        The IOPy IC client.
    connected
        True if the client has been connected before, False otherwise.
    """
    with gil:
        iopy_ic_py_client_on_disconnect_gil(client, connected)


cdef public void iopy_ic_py_client_on_disconnect_gil(iopy_ic_client_t *client,
                                                     cbool connected):
    """Called when the client is disconnecting with the GIL.

    Parameters
    ----------
    client
        The IOPy IC client.
    connected
        True if the client has been connected before, False otherwise.
    """
    cdef void *ctx = iopy_ic_client_get_py_obj(client)
    cdef Channel channel
    cdef object status
    cdef object message

    if not ctx:
        return

    channel = <Channel>ctx
    status = 'lost connection' if connected else 'cannot connect'
    message = ('IChannel %s to %s (%s)' %
               (status, channel.uri, get_warning_time_str()))
    send_warning_to_main_thread(ClientWarning, message)


# }}}
# {{{ Server RPC


cdef class RPCServer(RPCChannel):
    """RPC class for client IC channel"""
    cdef object rpc_impl
    cdef int count

    def __cinit__(RPCServer self):
        """Cython constructor of RPC server"""
        self.rpc_impl = None
        self.count = 0

    @property
    def impl(RPCServer self):
        """Get the current callback for the RPC.

        Returns
        -------
            The current callback for the RPC.
        """
        return self.rpc_impl

    @impl.setter
    def impl(RPCServer self, object value):
        """Set the current callback for the RPC.

        Parameters
        ----------
        value
            The function callback for the RPC.
            Set to None (default value) to disallow the RPC.
            Set to a function having 'rpc_args' as sole argument to enable the
            RPC.
            In this function 'rpc_args' is an object having the following
            fields:
                - rpc: the RPC server object.
                - arg: the received object RPC argument.
                - res: iopy type for result, or None if async.
                - exn: iopy type for exception, or None if async.
                - hdr: the received header.
            If not async this function should return an object either of
            rpc_args.res or rpc_args.exn type, this returned value will be
            transmitted to the remote caller.
            If a python error occurs during RPC execution or if the returned
            value is not of an expected type then the remote caller will
            receive a 'SERVER_ERROR' exception.
        """
        cdef int32_t cmd
        cdef ChannelServer channel

        if value is None:
            del self.impl
            return

        cmd = get_iface_rpc_cmd(self.py_iface.iface_alias, self.rpc)
        channel = self.channel

        channel.rpc_impls[cmd] = self
        with nogil:
            iopy_ic_server_register_rpc(channel.ic_server, self.rpc, cmd)
        self.rpc_impl = value

    @impl.deleter
    def impl(RPCServer self):
        """Remove the current callback for the RPC"""
        cdef const iop_iface_alias_t *iface_alias = self.py_iface.iface_alias
        cdef int32_t cmd = get_iface_rpc_cmd(iface_alias, self.rpc)
        cdef ChannelServer channel = self.channel

        del channel.rpc_impls[cmd]
        with nogil:
            iopy_ic_server_unregister_rpc(channel.ic_server, cmd)
        self.rpc_impl = None

    def wait(RPCServer self, int timeout, object uri=None, object host=None,
             int port=-1, int count=1):
        """Wait for an RPC to be called on a given socket address.

        Listen on the given socket and keep listening until time elapsed or
        RPC called.

        Either uri or (host and port) should be provided but not uri and (host
        and port).

        Parameters
        ----------
        timeout : int
            Number of seconds to listen.
        uri : str
            Socket address to listen to.
        host : str
            Address of the host.
        port : int
            Port number of the socket.
        count : int, optional
             Number of times the RPC should be called before stopping, should
             be greater or equal to 1. Default is 1.

        Returns
        -------
        int
            0 if the RPC has been called 'count' times or an integer
            containing remaining calls number ('count' - number of times the
            RPC has been called), on timeout.
        """
        if count < 1:
            raise Error('argument count should be greater or equal to 1')

        if not self.rpc_impl:
            raise Error('RPC is not implemented')

        self.count = count

        self.channel.listen_block(timeout, uri, host, port)

        return self.count


cdef class ChannelServer(ChannelBase):
    """Class for server IC channel"""
    cdef dict __dict__
    cdef dict rpc_impls
    cdef iopy_ic_server_t *ic_server
    cdef Plugin plugin
    cdef object on_connect_cb
    cdef object on_disconnect_cb

    def __init__(ChannelServer self, Plugin register):
        """Contructor of server IC channel.

        Parameters
        ----------
        register
            The IOPy plugin.
        """
        self.rpc_impls = {}
        self.ic_server = iopy_ic_server_create()
        iopy_ic_server_set_py_obj(self.ic_server, <void *>self)
        create_modules_of_channel(register, self, &create_server_rpc)

        self.plugin = register
        self.on_connect_cb = None
        self.on_disconnect_cb = None

    def __dealloc__(ChannelServer self):
        """Destructor of server IC channel"""
        if self.ic_server:
            iopy_ic_server_set_py_obj(self.ic_server, NULL)
            with nogil:
                iopy_ic_server_destroy(&self.ic_server)

    def __repr__(ChannelServer self):
        """Return the representation of the server IC channel.

        Returns
        -------
        str
            The representation of the server IC channel.
        """
        cdef list modules = list(self.__dict__.keys())

        modules.sort()
        return 'Channel Server (Modules: %s)' % modules

    def listen(ChannelServer self, object uri=None, object host=None,
               int port=-1):
        """Start the IC server listening.

        Either uri or (host and port) must be provided, but not uri and (host
        and port).

        Parameters
        ----------
        uri : str
            Socket address to listen to.
        host : str
            Address of the host.
        port : int
            Port number of the socket.
        """
        cdef t_scope_t t_scope_guard = t_scope_init()
        cdef sb_buf_1k_t err_buf
        cdef sb_scope_t err = sb_scope_init_static(err_buf)
        cdef lstr_t uri_lstr
        cdef int res

        t_scope_ignore(t_scope_guard)
        t_parse_uri_arg(uri, host, port, &uri_lstr)

        with nogil:
            res = iopy_ic_server_listen(self.ic_server, uri_lstr, &err)

        if res < 0:
            raise Error(lstr_to_py_str(LSTR_SB_V(&err)))

    def listen_block(ChannelServer self, int timeout, object uri=None,
                     object host=None, int port=-1):
        """Start the IC server listening and keep listening until time elapsed
        or server stopped.

        Either uri or (host and port) must be provided, but not uri and (host
        and port).

        Parameters
        ----------
        timeout : int
            Number of seconds to listen.
        uri : str
            Socket address to listen to.
        host : str
            Address of the host.
        port : int
            Port number of the socket.
        """
        cdef t_scope_t t_scope_guard = t_scope_init()
        cdef sb_buf_1k_t err_buf
        cdef sb_scope_t err = sb_scope_init_static(err_buf)
        cdef lstr_t uri_lstr
        cdef iopy_ic_res_t res

        t_scope_ignore(t_scope_guard)
        t_parse_uri_arg(uri, host, port, &uri_lstr)

        with nogil:
            res = iopy_ic_server_listen_block(self.ic_server, uri_lstr,
                                              timeout, &err)
        check_iopy_ic_res(res, &err)

    def stop(ChannelServer self):
        """Stop the IC server from listening"""
        cdef iopy_ic_res_t res

        with nogil:
            res = iopy_ic_server_stop(self.ic_server)

        check_iopy_ic_res(res, NULL)

    @property
    def on_connect(ChannelServer self):
        """Get the callback called upon connection.

        Returns
        -------
            The current callback called upon connection.
        """
        return self.on_connect_cb

    @on_connect.setter
    def on_connect(ChannelServer self, object value):
        """Set the callback called upon connection.

        Parameters
        ----------
        value
            Set to None (default value) to disallow connection call-back.
            Set to a function having 'server' and 'remote_addr' as sole
            arguments to enable connection call-back.
            In this function 'server' is the ChannelServer object for which
            connection is received, and 'remote_addr' is a string made of the
            uri of the remote client.
        """
        self.on_connect_cb = value

    @on_connect.deleter
    def on_connect(ChannelServer self):
        """Remove the callback called upon connection"""
        self.on_connect_cb = None

    @property
    def on_disconnect(ChannelServer self):
        """Get the callback called upon disconnection.

        Returns
        -------
            The current callback called upon disconnection.
        """
        return self.on_disconnect_cb

    @on_disconnect.setter
    def on_disconnect(ChannelServer self, object value):
        """Set the callback called upon disconnection.

        Parameters
        ----------
        value
            Set to None (default value) to disallow disconnection call-back.
            Set to a function having 'server' and 'remote_addr' as sole
            arguments to enable disconnection call-back.
            In this function 'server' is the ChannelServer object for which
            disconnection is received, and 'remote_addr' is a string made of
            the uri of the remote client.
        """
        self.on_disconnect_cb = value

    @on_disconnect.deleter
    def on_disconnect(ChannelServer self):
        """Remove the callback called upon disconnection"""
        self.on_disconnect_cb = None

    @property
    def is_listening(ChannelServer self):
        """Is the IC server listening."""
        cdef cbool res

        with nogil:
            res = iopy_ic_server_is_listening(self.ic_server)
        return res


cdef int create_server_rpc(const iop_rpc_t *rpc,
                           _InternalIfaceHolder iface_holder,
                           _InternalIface py_iface) except -1:
    """Callback called to create the server RPC of interface.

    Parameters
    ----------
    rpc
        The IOP RPC description.
    iface_holder
        The _InternalIfaceHolder for the interface.
    py_iface
        The interface object instance where to set the RPC.

    Returns
    -------
        -1 in case of exception, 0 otherwise.
    """
    cdef RPCServer py_rpc = RPCServer.__new__(RPCServer)
    cdef str rpc_name

    py_rpc.rpc = rpc
    py_rpc.iface_holder = iface_holder
    py_rpc.py_iface = py_iface

    rpc_name = lstr_to_py_str(rpc.name)
    setattr(py_iface._rpcs, rpc_name, py_rpc)
    setattr(py_iface, rpc_name, py_rpc)


@cython.internal
@cython.final
cdef class RPCArgs:
    """Arguments passed to callbacks of RPC server implementation"""
    cdef readonly object rpc
    cdef readonly object arg
    cdef readonly object res
    cdef readonly object exn
    cdef readonly object hdr


cdef public void iopy_ic_py_server_on_connect(iopy_ic_server_t *server,
                                              lstr_t server_uri,
                                              lstr_t remote_addr) nogil:
    """Called when a peer is connecting to the server.

    Parameters
    ----------
    ctx
        The IOPy IC server context.
    remote_addr
        The address of the peer.
    """
    with gil:
        iopy_ic_py_server_on_connect_gil(server, server_uri, remote_addr)


cdef void iopy_ic_py_server_on_connect_gil(iopy_ic_server_t *server,
                                           lstr_t server_uri,
                                           lstr_t remote_addr):
    """Called when a peer is connecting to the server with the GIL.

    Parameters
    ----------
    ctx
        The IOPy IC server context.
    remote_addr
        The address of the peer.
    """
    cdef void *ctx = iopy_ic_server_get_py_obj(server)
    cdef ChannelServer channel
    cdef object message

    if not ctx:
        return

    channel = <ChannelServer>ctx
    message = ('Channel Server listening on %s, connected to: %s (%s)' %
               (lstr_to_py_str(server_uri), lstr_to_py_str(remote_addr),
                get_warning_time_str()))
    send_warning_to_main_thread(ServerConnectWarning, message)

    if channel.on_connect_cb is not None:
        try:
            channel.on_connect_cb(channel, lstr_to_py_str(remote_addr))
        except:
            send_exception_to_main_thread()


cdef public void iopy_ic_py_server_on_disconnect(iopy_ic_server_t *server,
                                                 lstr_t server_uri,
                                                 lstr_t remote_addr) nogil:
    """Called when a peer is disconnecting from the server.

    Parameters
    ----------
    ctx
        The IOPy IC server context.
    remote_addr
        The address of the peer.
    """
    with gil:
        iopy_ic_py_server_on_disconnect_gil(server, server_uri, remote_addr)


cdef void iopy_ic_py_server_on_disconnect_gil(iopy_ic_server_t *server,
                                              lstr_t server_uri,
                                              lstr_t remote_addr):
    """Called when a peer is disconnecting from the server with the GIL.

    Parameters
    ----------
    ctx
        The IOPy IC server context.
    remote_addr
        The address of the peer.
    """
    cdef void *ctx = iopy_ic_server_get_py_obj(server)
    cdef ChannelServer channel
    cdef object message

    if not ctx:
        return

    channel = <ChannelServer>ctx
    message = ('Channel Server listening on %s, disconnected from: %s (%s)' %
               (lstr_to_py_str(server_uri), lstr_to_py_str(remote_addr),
                get_warning_time_str()))
    send_warning_to_main_thread(ServerDisconnectWarning, message)

    if channel.on_disconnect_cb is not None:
        try:
            channel.on_disconnect_cb(channel, lstr_to_py_str(remote_addr))
        except:
            send_exception_to_main_thread()


cdef public ic_status_t t_iopy_ic_py_server_on_rpc(
    iopy_ic_server_t *server, ichannel_t *ic, uint64_t slot, void *arg,
    const ic__hdr__t *hdr, void **res, const iop_struct_t **res_st) nogil:
    """Called when a request is made to an RPC.

    Parameters
    ----------
    ctx
        The IOPy IC server context.
    ic
        The ichannel of the request.
    slot
        The IC slot of the request.
    arg
        The RPC argument of the rpc_get_typerequest.
    res
        The RPC result of the reply.
    res_st
        The IOP type description of the reply.

    Returns
    -------
        The status of the reply. If the status is not IC_MSG_OK or IC_MSG_EXN,
        res and res_desc are ignored.
    """
    cdef int status

    with gil:
        try:
            status = t_iopy_ic_py_server_on_rpc_gil(server, ic, slot, arg,
                                                    hdr, res, res_st)
        except:
            send_exception_to_main_thread()
            status = IC_MSG_SERVER_ERROR

    return <ic_status_t>status


cdef int t_iopy_ic_py_server_on_rpc_gil(
    iopy_ic_server_t *server, ichannel_t *ic, uint64_t slot, void *arg,
    const ic__hdr__t *hdr, void **res, const iop_struct_t **res_st) except -1:
    """Called when a request is made to an RPC with the GIL.

    Parameters
    ----------
    ctx
        The IOPy IC server context.
    ic
        The ichannel of the request.
    slot
        The IC slot of the request.
    arg
        The RPC argument of the request.
    res
        The RPC result of the reply.
    res_st
        The IOP type description of the reply.

    Returns
    -------
        The status of the reply. If the status is not IC_MSG_OK or IC_MSG_EXN,
        res and res_desc are ignored.
        -1 in case of python exception.
    """
    cdef void *ctx = iopy_ic_server_get_py_obj(server)
    cdef ChannelServer channel
    cdef Plugin plugin
    cdef RPCServer rpc
    cdef RPCArgs args
    cdef object py_arg_cls
    cdef object py_hdr_cls
    cdef object py_res

    if not ctx:
        raise Error('server has been stopped')

    channel = <ChannelServer>ctx
    plugin = channel.plugin
    rpc = channel.rpc_impls.get(ichannel_get_cmd(ic))
    if rpc is None or rpc.rpc_impl is None:
        return IC_MSG_UNIMPLEMENTED

    args = RPCArgs.__new__(RPCArgs)

    args.rpc = rpc

    py_arg_cls = plugin_get_class_type_st(plugin, rpc.rpc.args)
    args.arg = iop_c_val_to_py_obj(py_arg_cls, rpc.rpc.args, arg, plugin)

    if rpc.rpc.result:
        args.res = plugin_get_class_type_st(plugin, rpc.rpc.result)
    else:
        args.res = None

    if rpc.rpc.exn:
        args.exn = plugin_get_class_type_st(plugin, rpc.rpc.exn)
    else:
        args.exn = None

    if hdr:
        py_hdr_cls = plugin_get_class_type_st(plugin, &ic__hdr__s)
        args.hdr = iop_c_val_to_py_obj(py_hdr_cls, &ic__hdr__s, hdr, plugin)
    else:
        args.hdr = None

    if rpc.count > 0:
        rpc.count -= 1
        if rpc.count == 0:
            channel.stop()

    py_res = rpc.rpc_impl(args)

    if py_res is None:
        if not lstr_equal(rpc.rpc.result.fullname, LSTR('Void')):
            raise Error('RPC impl %s of <%s> returned None while '
                        'expected type is: %s' %
                        (rpc.rpc_impl, rpc, args.arg))
        return IC_MSG_OK
    elif isinstance(py_res, args.res):
        res_st[0] = struct_union_get_desc(<StructUnionBase>py_res)
        mp_iop_py_obj_to_c_val(t_pool(), True, py_res, res)
        return IC_MSG_OK
    elif isinstance(py_res, args.exn):
        res_st[0] = struct_union_get_desc(<StructUnionBase>py_res)
        mp_iop_py_obj_to_c_val(t_pool(), True, py_res, res)
        return IC_MSG_EXN

    raise Error('RPC impl %s of <%s> returned a %s which is not a related '
                'res nor exn type' % (rpc.rpc_impl, rpc, type(py_res)))


# }}}
# }}}
# {{{ Plugin
# {{{ Metaclass


# In order to keep the backward compatibility with IOPyV2, we want to support
# IOPy types and interfaces upgrade through Plugin metaclasses.
# Unfortunately, we have some constraints:
#  - We already use metaclasses to store the IOP C type description and other
#    information about the IOPy types and interfaces.
#  - We want the IOPy type and interface metaclasses to act like the Plugin
#    metaclasses in order to do multiple levels of upgrade.
#  - We cannot create a new metaclass on upgrade since we cannot copy the IOPy
#    type and interface metaclasses.
#  - For classes, we want to support multiple levels of upgrade, but only for
#    the current classes. Child classes should not behave like Plugin
#    metaclasses if they are not upgraded.
#
# One solution, is on upgrade:
#  - Patch the IOPy type or interface metaclass with a __new__ method in order
#    to act like the Plugin metaclasses.
#  - Flag it with is_metaclass_upgraded so only this IOPy class should act
#    like the Plugin metaclass, and not its children.
#
# This is ugly, but due to all the listed constraints, I don't know if there
# is a better solution.


cdef object metaclass_cls_new
def metaclass_cls_new(_InternalBaseHolder mcs, object name, tuple bases,
                      dict dct):
    """Function that will be be used as method __new__ for the metaclass"""
    cdef object base_metaclass
    cdef Plugin plugin
    cdef object fullname
    cdef _InternalTypeClasses type_classes
    cdef object iopy_proxy
    cdef object iopy_public
    cdef _InternalBaseHolder iopy_metaclass
    cdef list field_names
    cdef cbool all_kwargs
    cdef dict init_kwargs
    cdef dict fields_kwargs
    cdef object custom_init
    cdef object spec
    cdef object args
    cdef object keywords
    cdef object defaults
    cdef object covars
    cdef object cls

    if not mcs.is_metaclass_upgraded:
        # We cannot use mcs.__new__ because of recursion.
        # We need to use the appriopriate base iopy type metaclass.
        if isinstance(mcs, _InternalEnumType):
            base_metaclass = _InternalEnumMetaclass
        else:
            base_metaclass = _InternalStructUnionMetaclass

        return base_metaclass.__new__(mcs, name, bases, dct)

    plugin = mcs.plugin
    fullname = name.replace('_', '.')

    type_classes = plugin_get_type_classes(plugin, fullname)
    if type_classes is None:
        raise KeyError('unknown IOPy type `%s`' % fullname)

    iopy_proxy = type_classes.proxy_cls
    # in case it is "Void"
    if not iopy_proxy:
        raise TypeError("%s is not a valid IOP type name" % fullname)

    iopy_metaclass = type_classes.metaclass
    iopy_public = type_classes.public_cls

    # Since, in some cases, metaclasses can inherits from the same base
    # classes in the inheritance tree, we should ignore them to have a
    # consistent MRO
    bases = tuple([x for x in bases if x not in iopy_proxy.__mro__])
    bases += (iopy_proxy,)

    try:
        field_names = iopy_proxy.get_fields_name()
    except AttributeError:
        field_names = []

    all_kwargs = False
    init_kwargs = {}
    fields_kwargs = {}

    custom_init = dct.get('__custom_init__', None)
    if custom_init:
        spec = inspect.getfullargspec(custom_init)
        args = spec[0]
        keywords = spec[2]
        defaults = spec[3]
        all_kwargs = keywords is not None
        if defaults:
            covars = args[-len(defaults):]
            init_kwargs.update(zip(covars, defaults))
            fields_kwargs.update({
                k: v for k, v
                in init_kwargs.iteritems()
                if k in field_names
            })

    def init(object o, *args, **kwargs):
        metaclass_cls_init(cls, o, args, kwargs)

    if mcs is not iopy_metaclass:
        # We patch the iopy type metaclass to use this function so it can be
        # used to do multiple levels of upgrade.
        iopy_metaclass.__new__ = metaclass_cls_new
        iopy_metaclass.is_metaclass_upgraded = True

    # We cannot use iopy_metaclass.__new__ because of recursion.
    # We need to use the appriopriate base iopy type metaclass.
    if isinstance(iopy_metaclass, _InternalEnumType):
        base_metaclass = _InternalEnumMetaclass
    else:
        base_metaclass = _InternalStructUnionMetaclass

    dct.update({
        "_all_kwargs": all_kwargs,
        "_init_kwargs": init_kwargs,
        "_fields_kwargs": fields_kwargs,
        "_fields_list": field_names,
        "__init__": init,
    })
    cls = base_metaclass.__new__(iopy_metaclass, name, bases, dct)
    iopy_public.__bases__ = (cls,)
    return cls


cdef int metaclass_cls_init(object cls, object obj, tuple args,
                            dict kwargs) except -1:
    """Method init for upgraded class through metaclass"""
    cdef object iop_kwargs
    cdef object k
    cdef object v
    cdef object custom_init
    cdef object cust_init_kwargs

    iop_kwargs = cls._fields_kwargs.copy()
    # set kwargs entry to iop_kwargs if:
    #  - entry key is in fields list
    # or
    # - if custom init does not take '**kwargs', entry key is not in
    #   custom init kwargs
    for k, v in kwargs.iteritems():
        if (k in cls._fields_list
         or (not cls._all_kwargs and k not in cls._init_kwargs)):
            iop_kwargs[k] = v

    super(cls, obj).__init__(*args, **iop_kwargs)

    custom_init = getattr(cls, '__custom_init__', None)
    if custom_init is not None:
        cust_init_kwargs = cls._init_kwargs.copy()
        if cls._all_kwargs:
            cust_init_kwargs.update(kwargs)
        else:
            cust_init_kwargs.update({
                k: v for k, v in kwargs.iteritems()
                if k in cls._init_kwargs
            })
        custom_init(obj, *args, **cust_init_kwargs)
    return 0


cdef object metaclass_iface_cls_new
def metaclass_iface_cls_new(_InternalBaseHolder mcs, object name, tuple bases,
                            dict dct):
    """Method that will be be used as __new__ for the metaclass"""
    cdef Plugin plugin = mcs.plugin
    cdef object fullname
    cdef _InternalTypeClasses type_classes
    cdef object iopy_proxy
    cdef object iopy_public
    cdef _InternalBaseHolder iopy_metaclass
    cdef object cls

    if not mcs.is_metaclass_upgraded:
        return _InternalIfaceBaseMetaclass.__new__(mcs, name, bases, dct)

    plugin = mcs.plugin
    fullname = name.replace('_', '.')

    type_classes = plugin_get_interface_classes(plugin, fullname)
    if type_classes is None:
        raise KeyError('unknown IOPy interface `%s`' % fullname)

    iopy_proxy = type_classes.proxy_cls
    iopy_public = type_classes.public_cls
    iopy_metaclass = type_classes.metaclass

    # Since, in some cases, metaclasses can inherits from the same base
    # classes in the inheritance tree, we should ignore them to have a
    # consistent MRO
    bases = tuple([x for x in bases if x not in iopy_proxy.__mro__])
    bases += (iopy_proxy,)

    def init(object o, *args, **kwargs):
        metaclass_iface_cls_init(cls, o, args, kwargs)

    if mcs is not iopy_metaclass:
        # We patch the iopy iface metaclass to use this function so it can be
        # used to do multiple levels of upgrade.
        iopy_metaclass.__new__ = metaclass_iface_cls_new
        iopy_metaclass.is_metaclass_upgraded = True

    dct.update({
        "__init__": init,
    })

    cls = _InternalIfaceBaseMetaclass.__new__(iopy_metaclass, name, bases,
                                              dct)
    iopy_public.__bases__ = (cls,)
    return cls


cdef int metaclass_iface_cls_init(object cls, object obj, tuple args,
                                  dict kwargs) except -1:
    """Method init for upgraded inteface through metaclass"""
    cdef object custom_init

    super(cls, obj).__init__(*args, **kwargs)

    custom_init = getattr(cls, '__custom_init__', None)
    if custom_init is not None:
        custom_init(obj, *args, **kwargs)
    return 0


# }}}


cdef class Interfaces:
    """Class to contain the interfaces of a packages"""
    cdef dict __dict__


cdef class Package:
    """Class to contain the packages of a plugin"""
    cdef dict __dict__
    cdef readonly Interfaces interfaces
    cdef const iop_pkg_t *pkg
    cdef int refcnt

    def __cinit__(self):
        """Cython constructor of package"""
        self.interfaces = Interfaces.__new__(Interfaces)

    def __name__(self):
        """Get package name

        Returns
        -------
        str
            The package name.
        """
        if self.pkg:
            return lstr_to_py_str(self.pkg.name)
        else:
            return '<unknown>'


@cython.internal
@cython.final
cdef class _InternalTypeClasses:
    """Internal class to the meta, proxy and public classes of IOPy types"""
    cdef object metaclass
    cdef object proxy_cls
    cdef object public_cls


@cython.internal
@cython.final
cdef class _InternalAdditionalDso:
    """Internal class to hold an additional dso"""
    cdef iop_dso_t *dso

    def __dealloc__(_InternalAdditionalDso self):
        """Destructor of _InternalAdditionalDso"""
        iop_dso_close(&self.dso)


@cython.final
cdef class Plugin:
    """Iopy Plugin object.

    This object represents an IOP plugin (correspond to either a .so library
    or the current program).
    All underlying packages and modules are members.
    connect() can be used to create an associated IOPy Channel Object.
    """
    cdef dict __dict__
    cdef iop_dso_t *dso
    cdef dict types
    cdef dict interfaces
    cdef dict additional_dsos
    cdef readonly dict modules
    cdef readonly object metaclass
    cdef readonly object metaclass_interfaces

    def __cinit__(Plugin self):
        """Cython constructor.

        Init the plugin without loading any dso.
        """
        cdef _InternalBaseHolder metaclass
        cdef _InternalBaseHolder metaclass_iface

        self.types = {}
        self.interfaces = {}
        self.modules = {}
        self.additional_dsos = {}

        metaclass = _InternalBaseHolder.__new__(_InternalBaseHolder,
                                                'Metaclass', (type,), {
            '__new__': metaclass_cls_new,
        })
        metaclass.plugin = self
        metaclass.is_metaclass_upgraded = True
        self.metaclass = metaclass

        metaclass_iface = _InternalBaseHolder.__new__(_InternalBaseHolder,
                                                      'MetaclassIface',
                                                      (type,), {
            '__new__': metaclass_iface_cls_new,
        })
        metaclass_iface.plugin = self
        metaclass_iface.is_metaclass_upgraded = True
        self.metaclass_interfaces = metaclass_iface

        # Force loading IOPy IC package instead of the one in the DSO
        plugin_add_package(self, &ic__pkg)

    def __init__(Plugin self, object dso_path=None):
        """Contructor.

        Load the given IOP plugin (.so) and create all the appropriate IOPy
        classes.

        Parameters
        ----------
        dso_path : str, optional
            The optional IOP plugin dso path.
        """
        self.dso = plugin_open_dso(self, dso_path)
        plugin_run_register_scripts(self, self.dso)

    def __dealloc__(Plugin self):
        """Close the IOP plugin"""
        iop_dso_close(&self.dso)

    @property
    def dsopath(Plugin self):
        """Get the path of the IOP plugin"""
        return lstr_to_py_str(self.dso.path)

    @property
    def __dsopath__(Plugin self):
        """Deprecated, use dsopath instead."""
        return lstr_to_py_str(self.dso.path)

    @property
    def __modules__(Plugin self):
        """Deprecated, use modules instead."""
        return self.modules

    def get_type_from_fullname(Plugin self, object fullname):
        """Get the public class for the given IOP type fullname.

        Parameters
        ----------
        fullname
            The fullname of the IOP type.

        Returns
        -------
            The public class of the IOP type.
        """
        return plugin_get_type_from_fullname(self, fullname)

    def __get_type_from_fullname__(Plugin self, object fullname):
        """Deprecated, use get_type_from_fullname() instead."""
        return plugin_get_type_from_fullname(self, fullname)

    def get_iface_type_from_fullname(Plugin self, object fullname):
        """Get the public class for the given IOP interface fullname.

        Parameters
        ----------
        fullname
            The fullname of the IOP interface.

        Returns
        -------
            The public class of the IOP interface.
        """
        return plugin_get_iface_type_from_fullname(self, fullname)

    def __get_iface_type_from_fullname__(Plugin self, object fullname):
        """Deprecated, use get_iface_type_from_fullname() instead."""
        return plugin_get_iface_type_from_fullname(self, fullname)

    def register(Plugin self):
        """Get legacy IOPy register.

        With IOPy Cython, the plugin and register are the same.
        """
        return self

    def _get_plugin(self):
        """Get legacy IOPy plugin.

        With IOPy Cython, the plugin and register are the same.
        """
        return self

    def upgrade(Plugin self, object index=None, cbool force_replace=False):
        """Upgrade IOP type by making the original IOP type inherits the
        decorated class.

        Parameters
        ----------
        index : int, optional
            You can choose which IOP type to upgrade with the index argument.
            If the wrapped class's base at given index is not a IOP type, a
            TypeError exception is raised.
        force_replace : bool, optional
            If True, all the upgrades already performed for the IOP type will
            be replaced by this one. If False, we will add this upgrade to the
            other upgrades of the IOP type. Default is False.
        """
        cdef int index_i = 0
        cdef object cls

        def wrapper(object cls):
            cdef object iopy_child = cls.__bases__[index_i]
            cdef object fullname
            cdef _InternalTypeClasses classes
            cdef object old_bases
            cdef object new_bases

            try:
                fullname = iopy_child.__fullname__()
            except AttributeError:
                raise TypeError("%s is not a valid IOP type, you may want "
                                "to set the 'index' argument appropriately" %
                                iopy_child)

            if issubclass(iopy_child, Basic):
                classes = plugin_get_type_classes(self, fullname)
            elif issubclass(iopy_child, IfaceBase):
                classes = plugin_get_interface_classes(self, fullname)
            else:
                raise TypeError('%s is not a valid IOP type or interface' %
                                fullname)

            if classes is None:
                raise TypeError('unknown IOP type/interface %s' % fullname)

            if classes.public_cls != iopy_child:
                raise TypeError('%s is not the current IOP public of %s' %
                                (iopy_child, fullname))

            if force_replace:
                old_bases = (classes.proxy_cls,)
            else:
                old_bases = iopy_child.__bases__

            new_bases = (
                cls.__bases__[:index_i]
              + old_bases
              + cls.__bases__[index_i + 1:]
            )
            cls.__bases__ = new_bases
            iopy_child.__bases__ = (cls,)

            return cls

        if isinstance(index, type):
            cls = index
            index_i = 0
            return wrapper(cls)
        else:
            if index is not None:
                index_i = index
            return wrapper


    def connect(Plugin self, object uri=None, *, object host=None,
                int port=-1, object timeout=None, object _timeout=None,
                **kwargs):
        """Connect to an IC and return the created IOPy Channel.

        Parameters
        ----------
        uri : str
            The URI to connect to. This is the only allowed positional
            argument.
        host : str
            The host to connect to. If set, port must also be set and uri must
            not be set.
        port : int
            The port to connect to. If set, host must also be set and uri must
            not be set.
        timeout : int
            The default and connection timeout for the IC channel.
            -1 means forever, default is 60.
        _timeout : int
            Backward compatibility parameter for timeout parameter.
        _login : str
            The login to be put in the default IC header.
        _group : str
            The group to be put in the default IC header.
        _password : str
            The password to be put in the default IC header.
        _kind : str
            The kind to be put in the default IC header.
        _workspace_id : int
            The id of workspace to be put in the default IC header.
        _dealias : bool
            The dealias flag to be put in the default IC header.
        _hdr : ic.SimpleHdr
            The default IC header to be used for this channel. If set, the
            above arguments must not be set.

        Returns
        -------
        iopy.Channel
            The IOPy client channel.
        """
        cdef Channel channel
        cdef int default_timeout

        if _timeout is not None and timeout is None:
            timeout = _timeout
        if timeout is not None:
            default_timeout = int(timeout)
        else:
            default_timeout = 60

        channel = Channel.__new__(Channel)
        client_channel_init(channel, self, uri, host, port, default_timeout,
                            kwargs)
        client_channel_connect(channel, default_timeout)
        return channel

    def channel_server(Plugin self):
        """Create an IC channel server.

        Returns
        -------
        iopy.ChannelServer
            The IOPy server channel.
        """
        return ChannelServer(self)

    def ChannelServer(Plugin self):
        """Create an IC channel server.

        Deprecated, use channel_server() instead.

        Returns
        -------
        iopy.ChannelServer
            The IOPy server channel.
        """
        return self.channel_server()

    @property
    def default_pre_hook(Plugin self):
        """Get the default pre hook to be used for all interfaces"""
        return IfaceBase.__pre_hook__

    @default_pre_hook.setter
    def default_pre_hook(Plugin self, object value):
        """Set the default pre hook to be used for all interfaces"""
        IfaceBase.__pre_hook__ = value

    @default_pre_hook.deleter
    def default_pre_hook(Plugin self):
        """Delete the default pre hook to be used for all interfaces"""
        del IfaceBase.__pre_hook__

    @property
    def default_post_hook(Plugin self):
        """Get the default post hook to be used for all interfaces"""
        return IfaceBase.__post_hook__

    @default_post_hook.setter
    def default_post_hook(Plugin self, object value):
        """Set the default post hook to be used for all interfaces"""
        IfaceBase.__post_hook__ = value

    @default_post_hook.deleter
    def default_post_hook(Plugin self):
        """Delete the default post hook to be used for all interfaces"""
        del IfaceBase.__post_hook__

    def load_dso(Plugin self, object key, object dso_path):
        """Load an additional dso.

        Parameters
        ----------
        key
            The unique key representing the DSO.
        dso_path
            The DSO file path.
        """
        cdef _InternalAdditionalDso additional_dso

        if key in self.additional_dsos:
            raise Error('additional dso with key `%s` is already registered' %
                        key)

        additional_dso = (
            _InternalAdditionalDso.__new__(_InternalAdditionalDso)
        )
        additional_dso.dso = plugin_open_dso(self, dso_path)

        self.additional_dsos[key] = additional_dso

    def unload_dso(Plugin self, object key):
        """Unload an additional dso.

        Parameters
        ----------
        key
            The unique key representing the DSO.
        """
        cdef _InternalAdditionalDso additional_dso

        additional_dso = <_InternalAdditionalDso>self.additional_dsos.get(key)
        if not additional_dso:
            raise Error('additional dso with key `%s` is not registered' %
                        key)

        plugin_unload_dso(self, additional_dso.dso)
        iop_dso_close(&additional_dso.dso)
        del self.additional_dsos[key]


cdef object plugin_get_type_from_fullname(Plugin plugin, object fullname):
    """Get the public class for the given IOP type fullname.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    fullname
        The fullname of the IOP type.

    Returns
    -------
        The public class of the IOP type.
    """
    cdef _InternalTypeClasses classes

    classes = plugin_get_type_classes(plugin, fullname)
    if classes is None:
        raise KeyError('unknown IOPy type `%s`' % fullname)
    return classes.public_cls


cdef object plugin_get_iface_type_from_fullname(Plugin plugin,
                                                object fullname):
    """Get the public class for the given IOP interface fullname.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    fullname
        The fullname of the IOP interface.

    Returns
    -------
        The public class of the IOP interface.
    """
    cdef _InternalTypeClasses classes

    classes = plugin_get_interface_classes(plugin, fullname)
    if classes is None:
        raise KeyError('unknown IOPy interface `%s`' % fullname)
    return classes.public_cls


cdef inline _InternalTypeClasses plugin_get_type_classes(Plugin plugin,
                                                         object fullname):
    """Get the classes for the given IOP type fullname.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    fullname
        The fullname of the IOP type.

    Returns
    -------
        The internal class that contains the proxy and public python
        classes of the IOP type. None if not found.
    """
    return <_InternalTypeClasses>plugin.types.get(fullname)


cdef inline _InternalTypeClasses plugin_get_interface_classes(
    Plugin plugin, object fullname):
    """Get the classes for the given IOP interface fullname.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    fullname
        The fullname of the IOP interface.

    Returns
    -------
        The internal class that contains the proxy and public python
        classes of the IOP interface. None if not found.
    """
    return <_InternalTypeClasses>plugin.interfaces.get(fullname)


cdef inline object plugin_get_class_type_st(Plugin plugin,
                                            const iop_struct_t *st):
    """Get the public class for the given IOP type struct description.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The struct or union IOP type description.

    Returns
    -------
        The public class of the IOP type.
    """
    cdef object fullname = lstr_to_py_str(st.fullname)
    cdef _InternalTypeClasses classes
    cdef IopPath iop_path
    cdef Package py_pkg

    classes = plugin_get_type_classes(plugin, fullname)
    if unlikely(classes is None):
        iop_path = make_iop_path(fullname)
        py_pkg = plugin_create_or_get_py_pkg(plugin, iop_path.pkg_name)
        classes = plugin_add_struct_union(plugin, py_pkg, st)

    return classes.public_cls


cdef inline object plugin_get_class_type_en(Plugin plugin,
                                            const iop_enum_t *en):
    """Get the public class for the given IOP type enum description.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    en
        The enum IOP type description.

    Returns
    -------
        The public class of the IOP type.
    """
    cdef object fullname = lstr_to_py_str(en.fullname)
    cdef _InternalTypeClasses classes
    cdef IopPath iop_path
    cdef Package py_pkg

    classes = plugin_get_type_classes(plugin, fullname)
    if unlikely(classes is None):
        iop_path = make_iop_path(fullname)
        py_pkg = plugin_create_or_get_py_pkg(plugin, iop_path.pkg_name)
        classes = plugin_add_enum(plugin, py_pkg, en)

    return classes.public_cls


cdef iop_dso_t *plugin_open_dso(Plugin plugin, object dso_path) except NULL:
    """Open and load specified dso to the plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    dso_path
        The path of the dso. Can be None.

    Returns
    -------
        The loaded DSO.
    """
    cdef sb_buf_1k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef const char *dso_path_str = NULL
    cdef bytes dso_path_bytes
    cdef iop_dso_t *dso

    if dso_path is not None:
        if isinstance(dso_path, str):
            dso_path_bytes = py_str_to_py_bytes(<str>dso_path)
        elif isinstance(dso_path, bytes):
            dso_path_bytes = <bytes>dso_path
        else:
            raise TypeError('dso path argument is not a valid str')
        dso_path_str = dso_path_bytes

    dso = iop_dso_open(dso_path_str, LM_ID_BASE, &sb)
    if not dso:
        raise Error('IOP module import fail: %s' %
                    lstr_to_py_str(LSTR_SB_V(&sb)))

    plugin_load_dso(plugin, dso)

    return dso


cdef void plugin_load_dso(Plugin plugin, iop_dso_t *dso):
    """Load packages and modules of the dso to the plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    dso
        The C IOP dso.
    """
    cdef QHashIterator it

    it = qhash_iter_make(&dso.pkg_h.qh)
    while qhash_iter_next(&it):
        plugin_add_package(plugin, dso.pkg_h.values[it.pos])

    it = qhash_iter_make(&dso.mod_h.qh)
    while qhash_iter_next(&it):
        plugin_add_module(plugin, dso.mod_h.values[it.pos])


cdef void plugin_add_package(Plugin plugin, const iop_pkg_t *pkg):
    """Add the C iop package to the plugin and create all appropriate
    classes.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    pkg
        The C iop package.
    """
    cdef str pkg_name
    cdef Package py_pkg
    cdef const iop_enum_t *const *enums
    cdef const iop_struct_t *const *structs

    pkg_name = make_py_pkg_name(lstr_to_py_str(pkg.name))
    py_pkg = plugin_create_or_get_py_pkg(plugin, pkg_name)
    py_pkg.refcnt += 1
    if py_pkg.refcnt > 1:
        return

    py_pkg.pkg = pkg

    enums = pkg.enums
    while enums[0]:
        plugin_add_enum(plugin, py_pkg, enums[0])
        enums += 1

    structs = pkg.structs
    while structs[0]:
        plugin_add_struct_union(plugin, py_pkg, structs[0])
        structs += 1


cdef Package plugin_create_or_get_py_pkg(Plugin plugin, str pkg_name):
    """Create or get existing package from the plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    pkg_name
        The name of the iop package.

    Returns
    -------
        The python IOP package.
    """
    cdef Package py_pkg

    py_pkg = <Package>getattr(plugin, pkg_name, None)
    if py_pkg is None:
        py_pkg = Package.__new__(Package)
        setattr(plugin, pkg_name, py_pkg)
    return py_pkg


cdef inline dict plugin_make_class_attrs_dict(str qualname):
    """Make class attrs dict used when creating classes.

    Warning: For optimization purposes, the same dict instance is returned
             each time. So you shouldn't modify the returned dict.
    """
    class_attrs_dict_g['__qualname__'] = qualname
    return class_attrs_dict_g


cdef _InternalTypeClasses plugin_add_enum(Plugin plugin, Package py_pkg,
                                          const iop_enum_t *en):
    """Create enum type and add it to the python package.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    py_pkg
        The iop python package.
    en
        The iop enum descrition.
    """
    cdef str iop_fullname = lstr_to_py_str(en.fullname)
    cdef IopPath iop_path = make_iop_path(iop_fullname)
    cdef _InternalEnumType enum_type
    cdef _InternalTypeClasses classes

    enum_type = _InternalEnumType.__new__(
        _InternalEnumType, iop_path.py_name + '_metaclass',
        (_InternalEnumMetaclass,), empty_init_metaclass_dict_g
    )
    enum_type.plugin = plugin
    enum_type.desc = en
    classes = plugin_add_type(plugin, enum_type, Enum, iop_path, None)
    setattr(py_pkg, iop_path.local_name, classes.public_cls)
    return classes


cdef _InternalStructUnionType plugin_create_st_iop_type(
    Plugin plugin, IopPath iop_path, const iop_struct_t *st,
    object base_cls):
    """Create internal struct or union type for given iopy type desc.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The iop struct or union description.
    iop_path
        The iop symbol path for the struct or union type.
    base_cls
        The base class of the metaclass.

    Returns
    -------
        The internal struct or union type to the hold C iop desc.
    """
    cdef _InternalStructUnionType iop_type

    iop_type = _InternalStructUnionType.__new__(
        _InternalStructUnionType, iop_path.py_name + '_metaclass',
        (base_cls,), empty_init_metaclass_dict_g
    )
    iop_type.desc = st
    iop_type.plugin = plugin
    return iop_type


cdef _InternalTypeClasses plugin_add_struct_union(Plugin plugin,
                                                  Package py_pkg,
                                                  const iop_struct_t *st):
    """Create struct or union type and add it to the python package.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    py_pkg
        The iop python package.
    st
        The iop struct or union description.
    """
    cdef str iop_fullname = lstr_to_py_str(st.fullname)
    cdef cbool is_class
    cdef IopPath iop_path
    cdef _InternalTypeClasses classes

    is_class = not st.is_union and iop_struct_is_class(st)
    if is_class:
        classes = plugin.types.get(iop_fullname)
        if classes is not None:
            return classes

    iop_path = make_iop_path(iop_fullname)

    if st.is_union:
        classes = plugin_add_union(plugin, iop_path, st)
    elif is_class:
        classes = plugin_add_class(plugin, iop_path, st)
    else:
        classes = plugin_add_struct(plugin, iop_path, st)
    setattr(py_pkg, iop_path.local_name, classes.public_cls)
    return classes


cdef _InternalTypeClasses plugin_add_union(Plugin plugin, IopPath iop_path,
                                           const iop_struct_t *st):
    """Create union type and add it to the python package.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    iop_path
        The iop symbol path for the union type.
    iop_type
        The internal object that holds the iop union description.
    """
    cdef _InternalStructUnionType iop_type

    iop_type = plugin_create_st_iop_type(plugin, iop_path, st,
                                         _InternalStructUnionMetaclass)
    return plugin_add_type(plugin, iop_type, Union, iop_path, None)


cdef _InternalTypeClasses plugin_add_struct(Plugin plugin, IopPath iop_path,
                                            const iop_struct_t *st):
    """Create struct type and add it to the python package.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    iop_path
        The iop symbol path for the struct type.
    st
        The iop struct description.
    """
    cdef _InternalStructUnionType iop_type

    iop_type = plugin_create_st_iop_type(plugin, iop_path, st,
                                         _InternalStructUnionMetaclass)
    return plugin_add_type(plugin, iop_type, Struct, iop_path, None)


cdef _InternalTypeClasses plugin_add_class(Plugin plugin, IopPath iop_path,
                                           const iop_struct_t *st):
    """Create class type and add it to the python package.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    iop_path
        The iop symbol path for the class type.
    iop_type
        The internal object that holds the iop class description.
    """
    cdef _InternalTypeClasses parent_classes
    cdef _InternalStructUnionType iop_type
    cdef dict proxy_cls = {}

    parent_classes = plugin_create_or_get_parent_classes(plugin, st)
    iop_type = plugin_create_st_iop_type(plugin, iop_path, st,
                                         parent_classes.metaclass)
    populate_static_fields_cls(st, proxy_cls)
    return plugin_add_type(plugin, iop_type, parent_classes.public_cls,
                           iop_path, proxy_cls)


cdef _InternalTypeClasses plugin_create_or_get_parent_classes(
    Plugin plugin, const iop_struct_t *child_st):
    """Create or get the parent class base type of the given child class.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    child_st
        The child iop class description.

    Returns
    -------
        The class that should be used as base for the given child class
        type.
    """
    cdef const iop_struct_t *st
    cdef _InternalTypeClasses root_classes
    cdef str iop_fullname
    cdef object classes_obj
    cdef IopPath iop_path
    cdef Package py_pkg
    cdef _InternalTypeClasses res

    st = child_st.class_attrs.parent
    if not st:
        root_classes = _InternalTypeClasses.__new__(_InternalTypeClasses)
        root_classes.metaclass = _InternalStructUnionMetaclass
        root_classes.proxy_cls = None
        root_classes.public_cls = Struct
        return root_classes

    iop_fullname = lstr_to_py_str(st.fullname)
    classes_obj = plugin.types.get(iop_fullname)
    if classes_obj is not None:
        return <_InternalTypeClasses>classes_obj

    iop_path = make_iop_path(iop_fullname)
    res = plugin_add_class(plugin, iop_path, st)
    py_pkg = plugin_create_or_get_py_pkg(plugin, iop_path.pkg_name)
    setattr(py_pkg, iop_path.local_name, res.public_cls)
    return res


cdef _InternalTypeClasses plugin_add_type(
    Plugin plugin, object metaclass, object base_type,
    IopPath iop_path, dict proxy_cls):
    """Create iop type and add it to the python package.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    metaclass
        The metaclass to be used for the classes.
    base_type
        The python base type of the proxy class to create.
    iop_path
        The iop symbol path for the class type.
    proxy_dct
        The class attributes dictionary for the proxy class.
        For classes IOP types, this will be set to the static attributes
        of the class.
    """
    cdef _InternalTypeClasses classes
    cdef str proxy_name = iop_path.py_name + '_proxy'

    classes = _InternalTypeClasses.__new__(_InternalTypeClasses)

    if proxy_cls is None:
        proxy_cls = plugin_make_class_attrs_dict(proxy_name)
    else:
        proxy_cls.update(plugin_make_class_attrs_dict(proxy_name))

    classes.metaclass = metaclass
    classes.proxy_cls = metaclass.__new__(
        metaclass, proxy_name, (base_type,), proxy_cls
    )

    classes.public_cls = metaclass.__new__(
        metaclass, iop_path.py_name, (classes.proxy_cls,),
        plugin_make_class_attrs_dict(iop_path.py_name)
    )
    plugin.types[iop_path.iop_fullname] = classes
    return classes


cdef void plugin_add_module(Plugin plugin, const iop_mod_t *module):
    """Add iop module to plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    module
        The module to add.
    """
    cdef str iop_fullname
    cdef _InternalModuleHolder py_module
    cdef IopPath iop_path
    cdef uint16_t i
    cdef const iop_iface_alias_t *iface_alias
    cdef object py_iface

    iop_fullname = lstr_to_py_str(module.fullname)
    py_module = <_InternalModuleHolder>plugin.modules.get(iop_fullname)
    if py_module is not None:
        py_module.refcnt += 1
        return

    iop_path = make_iop_path(iop_fullname)
    py_module = _InternalModuleHolder.__new__(
        _InternalModuleHolder, iop_path.py_name, (Module,),
        plugin_make_class_attrs_dict(iop_path.py_name)
    )
    py_module.module = module
    py_module.refcnt = 1

    for i in range(module.ifaces_len):
        iface_alias = &module.ifaces[i]
        py_iface = plugin_add_iface(plugin, iface_alias.iface)
        setattr(py_module, lstr_to_py_str(iface_alias.name), py_iface)

    plugin.modules[iop_fullname] = py_module


cdef object plugin_add_iface(Plugin plugin, const iop_iface_t *iface):
    """Add iop interface to plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    iface
        The interface to add.

    Returns
    -------
    object
        The public class of the interface
    """
    cdef str iop_fullname
    cdef _InternalTypeClasses classes
    cdef str metaclass_name
    cdef _InternalIfaceHolder metaclass
    cdef str proxy_name
    cdef object proxy_cls
    cdef object public_cls
    cdef IopPath iop_path
    cdef Package py_pkg
    cdef uint16_t i
    cdef const iop_rpc_t *rpc
    cdef RPCBase py_rpc
    cdef str rpc_name

    iop_fullname = lstr_to_py_str(iface.fullname)
    classes = <_InternalTypeClasses>plugin.interfaces.get(iop_fullname)
    if classes is not None:
        metaclass = <_InternalIfaceHolder>classes.metaclass
        metaclass.refcnt += 1
        return classes.public_cls

    iop_path = make_iop_path(iop_fullname)

    metaclass_name = iop_path.py_name + '_metaclass'
    metaclass = _InternalIfaceHolder.__new__(
        _InternalIfaceHolder, metaclass_name, (_InternalIfaceBaseMetaclass,),
        empty_init_metaclass_dict_g
    )
    metaclass.refcnt = 1
    metaclass.plugin = plugin
    metaclass.iface = iface

    proxy_name = iop_path.py_name + '_proxy'
    proxy_cls = metaclass.__new__(
        metaclass, proxy_name, (IfaceBase,),
        plugin_make_class_attrs_dict(proxy_name)
    )

    public_cls = metaclass.__new__(
        metaclass, iop_path.py_name, (proxy_cls,),
        plugin_make_class_attrs_dict(iop_path.py_name)
    )

    for i in range(iface.funs_len):
        rpc = &iface.funs[i]
        py_rpc = plugin_create_iface_rpc(plugin, metaclass, rpc)
        rpc_name = lstr_to_py_str(rpc.name)
        setattr(proxy_cls, rpc_name, py_rpc)
        setattr(public_cls, rpc_name, py_rpc)

    classes = _InternalTypeClasses.__new__(_InternalTypeClasses)
    classes.metaclass = metaclass
    classes.proxy_cls = proxy_cls
    classes.public_cls = public_cls

    py_pkg = plugin_create_or_get_py_pkg(plugin, iop_path.pkg_name)
    setattr(py_pkg.interfaces, iop_path.local_name, public_cls)
    plugin.interfaces[iop_fullname] = classes
    return public_cls


cdef RPCBase plugin_create_iface_rpc(Plugin plugin,
                                     _InternalIfaceHolder iface_holder,
                                     const iop_rpc_t *rpc):
    """Create python interface rpc.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    rpc
        The C iop rpc.
    iface
        The C iop interface.

    Returns
    -------
        The RpcBase for the IOP rpc.
    """
    cdef RPCBase py_rpc

    if rpc.args:
        plugin_add_iface_rpc_st(plugin, rpc.args)

    if rpc.result:
        plugin_add_iface_rpc_st(plugin, rpc.result)

    if rpc.exn:
        plugin_add_iface_rpc_st(plugin, rpc.exn)

    py_rpc = RPCBase.__new__(RPCBase)
    py_rpc.rpc = rpc
    py_rpc.iface_holder = iface_holder
    return py_rpc


cdef void plugin_add_iface_rpc_st(Plugin plugin, const iop_struct_t *st):
    """Create struct or union from RPC if not already created.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The IOP struct or union description.
    """
    cdef str iop_fullname = lstr_to_py_str(st.fullname)
    cdef IopPath iop_path

    if iop_fullname in plugin.types:
        return

    iop_path = make_iop_path(iop_fullname)

    if iop_fullname == 'Void':
        plugin_add_void_type(plugin, st, iop_path)
        return

    if st.is_union:
        plugin_add_union(plugin, iop_path, st)
    elif iop_struct_is_class(st):
        plugin_add_class(plugin, iop_path, st)
    else:
        plugin_add_struct(plugin, iop_path, st)


cdef void plugin_add_void_type(Plugin plugin, const iop_struct_t *st,
                               IopPath iop_path):
    """Create void type for iop interface rpcs

    Parameters
    ----------
    plugin
        The IOPy plugin.
    st
        The IOP struct description of the void struct.
    iop_path
        The IOP path of the void struct.
    """
    cdef _InternalStructUnionType iop_type
    cdef _InternalTypeClasses classes

    iop_path.py_name = 'Void'
    iop_type = plugin_create_st_iop_type(plugin, iop_path, st,
                                         _InternalStructUnionMetaclass)

    classes = _InternalTypeClasses.__new__(_InternalTypeClasses)
    classes.metaclass = iop_type
    classes.proxy_cls = None
    classes.public_cls = iop_type.__new__(
        iop_type, iop_path.py_name, (Struct,),
        plugin_make_class_attrs_dict(iop_path.py_name)
    )

    plugin.types[iop_path.py_name] = classes


cdef void plugin_unload_dso(Plugin plugin, iop_dso_t *dso):
    """Unload the corresponding dso.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    dso
        The IOP dso to unload.
    """
    cdef QHashIterator it

    it = qhash_iter_make(&dso.pkg_h.qh)
    while qhash_iter_next(&it):
        plugin_remove_package(plugin, dso.pkg_h.values[it.pos])

    it = qhash_iter_make(&dso.mod_h.qh)
    while qhash_iter_next(&it):
        plugin_remove_module(plugin, dso.mod_h.values[it.pos])


cdef void plugin_remove_package(Plugin plugin, const iop_pkg_t *pkg):
    """Remove the C iop package from the plugin and all associated classes.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    pkg
        The C iop package.
    """
    cdef str pkg_name
    cdef Package py_pkg
    cdef const iop_enum_t *const *enums
    cdef const iop_struct_t *const *structs

    pkg_name = make_py_pkg_name(lstr_to_py_str(pkg.name))
    py_pkg = <Package>getattr(plugin, pkg_name, None)
    if py_pkg is None:
        # Already removed?
        return

    py_pkg.refcnt -= 1
    if py_pkg.refcnt > 0:
        # Still used by another dso.
        return

    enums = pkg.enums
    while enums[0]:
        plugin_remove_enum(plugin, enums[0])
        enums += 1

    structs = pkg.structs
    while structs[0]:
        plugin_remove_struct_union(plugin, structs[0])
        structs += 1

    delattr(plugin, pkg_name)


cdef void plugin_remove_enum(Plugin plugin, const iop_enum_t *en):
    """Remove enum type.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    en
        The iop enum descrition.
    """
    cdef str iop_fullname = lstr_to_py_str(en.fullname)

    plugin.types.pop(iop_fullname, None)


cdef void plugin_remove_struct_union(Plugin plugin, const iop_struct_t *st):
    """Remove struct or union type.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    en
        The iop enum descrition.
    """
    cdef str iop_fullname = lstr_to_py_str(st.fullname)

    plugin.types.pop(iop_fullname, None)


cdef void plugin_remove_module(Plugin plugin, const iop_mod_t *module):
    """Remove iop module from plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    module
        The module to remove.
    """
    cdef str iop_fullname
    cdef _InternalModuleHolder py_module
    cdef uint16_t i
    cdef const iop_iface_alias_t *iface_alias
    cdef str iface_fullname

    iop_fullname = lstr_to_py_str(module.fullname)
    py_module = <_InternalModuleHolder>plugin.modules.get(iop_fullname)
    if py_module is None:
        # Already removed?
        return

    py_module.refcnt -= 1
    if py_module.refcnt > 0:
        # Still used by another dso.
        return

    for i in range(module.ifaces_len):
        iface_alias = &module.ifaces[i]
        iface_fullname = lstr_to_py_str(iface_alias.iface.fullname)
        plugin.interfaces.pop(iface_fullname, None)

    del plugin.modules[iop_fullname]


cdef void plugin_remove_iface(Plugin plugin, const iop_iface_t *iface):
    """Remove iop module from plugin.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    iface
        The interface to remove.
    """
    cdef str iop_fullname
    cdef _InternalTypeClasses classes
    cdef IopPath iop_path
    cdef Package py_pkg

    iop_fullname = lstr_to_py_str(iface.fullname)
    classes = <_InternalTypeClasses>plugin.interfaces.get(iop_fullname)
    if classes is None:
        # Already removed?
        return

    classes.metaclass.refcnt -= 1
    if classes.metaclass.refcnt > 0:
        # Still used by another dso.
        return

    del plugin.interfaces[iop_fullname]

    iop_path = make_iop_path(iop_fullname)
    py_pkg = <Package>getattr(plugin, iop_path.pkg_name, None)
    if py_pkg is not None:
        delattr(py_pkg.interfaces, iop_path.local_name)


cdef void plugin_run_register_scripts(Plugin plugin, const iop_dso_t *dso):
    """Run register scripts in the IOP DSO.

    Parameters
    ----------
    plugin
        The IOPy plugin.
    dso
        The IOP DSO.
    """
    cdef t_scope_t t_scope_guard = t_scope_init()
    cdef dict globals_dict = globals()
    cdef const farch_entry_t *script
    cdef const farch_entry_t * const *script_ptr
    cdef lstr_t script_data
    cdef str script_str
    cdef farch_name_t name
    cdef object message

    t_scope_ignore(t_scope_guard)

    globals_dict['_iopy_register'] = plugin

    script_ptr = iopy_dso_get_scripts(dso)
    while script_ptr and script_ptr[0]:
        script = script_ptr[0]
        while script.name.len:
            script_data = t_farch_get_data(script, NULL)
            script_str = lstr_to_py_str(script_data)

            try:
                exec(script_str, globals_dict, globals_dict)
            except:
                farch_get_filename(script, name)
                message = ('error when running register script %s:\n%s' %
                           (name, traceback.format_exc()))
                warnings.warn(Warning, message, stacklevel=1)

            script += 1

        script_ptr += 1

    del globals_dict['_iopy_register']


# }}}
# {{{ Module functions


def set_json_flags(**kwargs):
    """Set the default json pack flags to use when converting an Iopy object
    to a JSON representation.

    See StructUnionBase::to_json() for the accepted parameters.
    """
    iopy_g.jpack_flags = iopy_kwargs_to_jpack_flags(kwargs, True)


def thr_attach():
    """Set the internals of the iopy library up for use in python-created
    threads (the main thread is automatically attached).
    """
    c_thr_attach()


def thr_detach():
    """Properly clean the iopy library internals set via thr_attach
    (this function should be called before thread end).
    """
    c_thr_detach()

# }}}
# {{{ Exported functions


cdef public const iop_struct_t *Iopy_struct_union_type_get_desc(object cls):
    """Get the IOP C struct or union descriptor from the python class.

    Parameters
    ----------
    cls
        The python IOPy class type.

    Returns
    -------
        The IOP C struct or union descriptor.
    """
    return struct_union_get_iop_type_cls(cls).desc


cdef public cbool Iopy_has_pytype_from_fullname(object obj):
    """Check if object has get_type_from_fullname attribute.

    Parameters
    ----------
    obj
        The object to check.

    Returns
    -------
        True of the object has 'get_type_from_fullname' attribute, False
        otherwise.
    """
    return hasattr(obj, 'get_type_from_fullname')


cdef public object Iopy_get_pytype_from_fullname_(object obj,
                                                  lstr_t fullname):
    """Get IOPy class type from fullname.

    Parameters
    ----------
    obj
        The object where to get the IOPy class type.
    fullname
        The IOP fullname of the type.

    Returns
    -------
        The IOPy class type.
    """
    return obj.get_type_from_fullname(lstr_to_py_str(fullname))


cdef public cbool Iopy_Struct_to_iop_ptr(mem_pool_t *mp, void **ptr,
                                         const iop_struct_t *s,
                                         object obj) except False:
    """Create C iop value from a python struct or class object.

    Parameters
    ----------
    mp
        The memory pool used to create the C iop value. It must be a frame
        based memory pool.
    ptr
        The pointer where to put the C iop value.
    s
        The supposed IOP description of the object. It is ignored.
    obj
        The python object.

    Returns
    -------
        True if the object has been created, False otherwise with an
        exception.
    """
    cdef StructBase py_st = obj

    mp_iop_py_obj_to_c_val(mp, False, py_st, ptr)
    return True


cdef public cbool Iopy_Union_to_iop_ptr(mem_pool_t *mp, void **ptr,
                                        const iop_struct_t *s,
                                        object obj) except False:
    """Create C iop value from a python union object.

    Parameters
    ----------
    mp
        The memory pool used to create the C iop value. It must be a frame
        based memory pool.
    ptr
        The pointer where to put the C iop value.
    s
        The supposed IOP description of the object. It is ignored.
    obj
        The python object.

    Returns
    -------
        True if the object has been created, False otherwise with an
        exception.
    """
    cdef UnionBase py_un = obj

    mp_iop_py_obj_to_c_val(mp, False, py_un, ptr)
    return True


cdef public object Iopy_from_iop_struct_or_union(object cls,
                                                 const iop_struct_t *desc,
                                                 const void *val):
    """Create an IOPy python object from a struct or union.

    Parameters
    ----------
    cls
        The IOPy python class of the object to create.
    desc
        The IOP C description.
    val
        The IOP C value.

    Returns
    -------
        The IOPy python object.
    """
    cdef Plugin plugin = struct_union_get_iop_type_cls(cls).plugin

    return iop_c_val_to_py_obj(cls, desc, val, plugin)


cdef public void Iopy_add_iop_package(const iop_pkg_t *p, object plugin_obj):
    """Add the C iop package to the plugin and create all appropriate
    classes.

    Parameters
    ----------
    p
        The C iop package.
    plugin_obj
        The IOPy plugin.
    """
    cdef Plugin plugin = plugin_obj

    plugin_add_package(plugin, p)


cdef public int Iopy_remove_iop_package(const iop_pkg_t *p,
                                        object plugin_obj) except -1:
    """Remove the C iop package from the plugin and all associated classes.

    Parameters
    ----------
    p
        The C iop package.
    plugin_obj
        The IOPy plugin.
    """
    cdef Plugin plugin = plugin_obj

    plugin_remove_package(plugin, p)


cdef public object Iopy_make_plugin_from_handle(void *handle,
                                                const char *path):
    """Make a IOPy plugin by loading the DSO from the handle.

    On success, the plugin will own the handle afterwards.
    On error, the handle is not closed.

    Parameters
    ----------
    handle
        The C IOP DSO handle.
    path
        The C IOP modules.

    Returns
    -------
        The IOPy plugin.
    """
    cdef sb_buf_1k_t sb_buf
    cdef sb_scope_t sb = sb_scope_init_static(sb_buf)
    cdef Plugin plugin
    cdef iop_dso_t *dso

    plugin = Plugin.__new__(Plugin)
    dso = iop_dso_load_handle(handle, path, LM_ID_BASE, &sb)
    if not dso:
        raise Error('IOP module import fail: %s' %
                    lstr_to_py_str(LSTR_SB_V(&sb)))

    plugin_load_dso(plugin, dso)
    plugin.dso = dso
    return plugin


# }}}
# {{{ Init


cdef void init_module_versions():
    """Init the IOPy versions to the module"""
    cdef dict globals_dict = globals()
    cdef tuple version_info = (IOPY_MAJOR, IOPY_MINOR, IOPY_PATCH)
    cdef object version = '%d.%d.%d' % version_info
    cdef object revision = (<bytes>iopy_git_revision)[:40]

    globals_dict['__version_info__'] = version_info
    globals_dict['__version__'] = version
    globals_dict['__revision__'] = revision


cdef object thread_start_new_thread
cdef object iopy_start_new_thread
def iopy_start_new_thread(object func, *args, **kwargs):
    """Wrapper around thread.start_new_thread to call c_thr_attach() and
    c_thr_detach() on thread start"""
    cdef object new_func

    def new_func(*func_args, **func_kwargs):
        cdef object res

        c_thr_attach()
        try:
            res = func(*func_args, **func_kwargs)
        finally:
            c_thr_detach()
        return res

    return thread_start_new_thread(new_func, *args, **kwargs)


cdef void init_thread_attach():
    """Hack to override thread and threading module start_new_thread to call
    c_thr_attach() and c_thr_detach().

    This is required in order to use IOPy in python threads.
    """
    cdef object thread
    cdef object threading

    try:
        import thread
    except ImportError:
        import _thread as thread
    import threading

    global thread_start_new_thread
    thread_start_new_thread = thread.start_new_thread

    thread.start_new_thread = iopy_start_new_thread
    thread.start_new = iopy_start_new_thread
    threading._start_new_thread = iopy_start_new_thread


cdef void iopy_atexit_rpc_stop_cb():
    """Callback to stop the RPC module when the interpreter is still valid"""
    with nogil:
        iopy_rpc_module_stop()


cdef int init_iopy_atexit() except -1:
    """Register atexit callback to stop and clean up RPC module.

    We need to split up the shutdown of the RPC module in two parts:
      - Firstly, we need to stop the RPC module, with iopy_rpc_module_stop(),
        which will stop the el thread loop, while the Python interpreter is
        still valid.
        When stopping the el thread, it can still call some Python callbacks,
        so we need to be sure the interpreter is still valid.
      - Secondly, we clean up all the resources of the RPC module after the
        Python interpreter has been cleaned up, with
        iopy_rpc_module_cleanup().
        When this function is called, we are sure that no Python functions
        will ever be called.
    """
    cdef object atexit

    # Use atexit Python module to call iopy_rpc_module_stop() while the Python
    # interpreter is still valid.
    import atexit
    atexit.register(iopy_atexit_rpc_stop_cb)

    # Use Py_AtExit() to call iopy_rpc_module_cleanup() after the Python
    # interpreter has been cleaned up.
    if Py_AtExit(&iopy_rpc_module_cleanup) < 0:
        raise RuntimeError('unable to register iopy at_exit callback')


init_iopy_atexit()
py_eval_init_threads()
iopy_rpc_module_init()
init_module_versions()
init_thread_attach()


# }}}
