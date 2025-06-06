###########################################################################
#                                                                         #
# Copyright 2025 INTERSEC SA                                              #
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

import asyncio
import typing

import ic__iop
import typing_extensions

# {{{ Errors

class Error(Exception): ...
class RpcError(Error): ...
class UnexpectedExceptionWarning(Warning): ...

# }}}
# {{{ Types
# {{{ Optional field

@typing.type_check_only
class IopFieldIsOpt:
    # Metadata to indicate that the field is an optional field
    ...

# Type of an IOP field
_TField = typing.TypeVar('_TField')
IopOptField: typing_extensions.TypeAlias = typing.Annotated[
    _TField,
    IopFieldIsOpt,
]

# }}}
# {{{ Base

class Basic:
    # Try to auto-deduce the type of an object created from a `Basic` class.
    # `Basic` cannot be instantiated on run-time, by the type checker can
    # still use it, and `Package.__getattr__()` returns a `type[Basic]`.
    # Depending on the arguments passed onto the constructor, we can try to
    # deduce the type of the created object:
    # - If we try to do a copy of the IOPy object, return the same type of the
    #   object (EnumBase, StructBase or UnionBase).
    # - If there is only one argument of type `str` or `int`, we assume we try
    #   to create an enum (EnumBase). Technically, we can also create a union
    #   with an unambiguous type, but it is less likely.
    # - If there is only one argument of type dict, we try to either create a
    #   union or a struct (StructUnionBase).
    # - If we use keyword arguments, we try to either create a union or a
    #   struct (StructUnionBase).
    # - Otherwise if there is only argument, we try to create a union with an
    #   unambiguous type (UnionBase).
    @typing.overload
    def __new__(cls, enum_val: EnumBase, /) -> EnumBase: ...
    @typing.overload
    def __new__(cls, struct_val: StructBase, /) -> StructBase: ...
    @typing.overload
    def __new__(cls, union_val: UnionBase, /) -> UnionBase: ...
    @typing.overload
    def __new__(cls, val: int | str, /) -> EnumBase: ...
    @typing.overload
    def __new__(
        cls, self_dict: dict[str, typing.Any], /) -> StructUnionBase: ...
    @typing.overload
    def __new__(cls, **kwargs: typing.Any) -> StructUnionBase: ...
    @typing.overload
    def __new__(cls, val: typing.Any, /) -> UnionBase: ...

class IopHelpDescription:
    brief: str | None
    details: str | None
    warning: str | None
    example: str | None

# }}}
# {{{ Enum

class IopEnumValueDescription:
    help: IopHelpDescription
    generic_attributes: dict[str, typing.Any]
    aliases: tuple[str]

class IopEnumDescription:
    help: IopHelpDescription
    strict: bool
    generic_attributes: dict[str, typing.Any]
    values: dict[str, IopEnumValueDescription]

class EnumBase(Basic):
    # Reset the auto-deduction of created objects in `Basic`.
    def __new__(
        cls, *args: typing.Any, **kwargs: typing.Any,
    ) -> typing_extensions.Self: ...
    @typing.overload
    def __init__(self, self_val: typing_extensions.Self, /) -> None: ...
    @typing.overload
    def __init__(self, val: int | str, /) -> None: ...
    @classmethod
    def name(cls) -> str: ...
    @classmethod
    def fullname(cls) -> str: ...
    @classmethod
    def __fullname__(cls) -> str: ...
    @classmethod
    def values(cls) -> dict[str, int]: ...
    @classmethod
    def __values__(cls) -> dict[str, int]: ...
    @classmethod
    def ranges(cls) -> dict[str, int]: ...
    @classmethod
    def __ranges__(cls) -> dict[str, int]: ...
    @classmethod
    def get_iop_description(cls) -> IopEnumDescription: ...
    def set(self, val: int | str) -> None: ...
    def get_as_int(self) -> int: ...
    def __int__(self) -> int: ...
    def __index__(self) -> int: ...
    def get_as_str(self) -> str: ...
    def __richcmp__(self, other: object, op: int) -> bool: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...
    def __lt__(self, other: object) -> bool: ...
    def __le__(self, other: object) -> bool: ...
    def __gt__(self, other: object) -> bool: ...
    def __ge__(self, other: object) -> bool: ...

class Enum(EnumBase): ...

# }}}
# {{{ StructUnionBase

class StructUnionBase(Basic):
    # Reset the auto-deduction of created objects in `Basic`.
    def __new__(
        cls, *args: typing.Any, **kwargs: typing.Any,
    ) -> typing_extensions.Self: ...
    @typing.overload
    def __init__(self, self_dict: dict[str, typing.Any], /) -> None: ...
    @typing.overload
    def __init__(self, self_val: typing_extensions.Self, /) -> None: ...
    @typing.overload
    def __init__(self, **kwargs: typing.Any) -> None: ...
    @typing.overload
    @classmethod
    def from_file(
        cls,
        *,
        _json: str,
        single: bool = True,
        _ignore_unknown: bool = False,
        _forbid_private: bool = False,
        _use_c_case: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def from_file(
        cls,
        *,
        _yaml: str,
        single: bool = True,
        _ignore_unknown: bool = False,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def from_file(
        cls,
        *,
        _xml: str,
        single: bool = True,
        _ignore_unknown: bool = False,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def from_file(
        cls,
        *,
        _hex: str,
        single: bool = True,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def from_file(
        cls,
        *,
        _bin: str,
        single: bool = True,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def __from_file__(
        cls,
        *,
        _json: str,
        single: bool = True,
        _ignore_unknown: bool = False,
        _forbid_private: bool = False,
        _use_c_case: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def __from_file__(
        cls,
        *,
        _yaml: str,
        single: bool = True,
        _ignore_unknown: bool = False,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def __from_file__(
        cls,
        *,
        _xml: str,
        single: bool = True,
        _ignore_unknown: bool = False,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def __from_file__(
        cls,
        *,
        _hex: str,
        single: bool = True,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    @typing.overload
    @classmethod
    def __from_file__(
        cls,
        *,
        _bin: str,
        single: bool = True,
        _forbid_private: bool = False,
    ) -> typing_extensions.Self: ...
    def __richcmp__(self, other: object, op: int) -> bool: ...
    def to_json(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def __json__(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def to_yaml(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def __yaml__(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def to_bin(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> bytes: ...
    def __bin__(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> bytes: ...
    def to_hex(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def __hex__(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def to_xml(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def __xml__(
        self,
        no_whitespaces: bool | None = None,
        no_trailing_eol: bool | None = None,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        shorten_data: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> str: ...
    def to_dict(
        self,
        skip_private: bool | None = None,
        skip_default: bool | None = None,
        skip_empty_arrays: bool | None = None,
        skip_empty_structs: bool | None = None,
        skip_class_names: bool | None = None,
        skip_optional_class_names: bool | None = None,
        minimal: bool | None = None,
    ) -> dict[str, typing.Any]: ...
    @classmethod
    def __fullname__(cls) -> str: ...
    @classmethod
    def get_fields_name(cls) -> list[str]: ...
    @classmethod
    def __get_fields_name__(cls) -> list[str]: ...
    @classmethod
    def get_desc(cls) -> str: ...
    @classmethod
    def __desc__(cls) -> str: ...
    @classmethod
    def get_values(cls) -> dict[str, type]: ...
    @classmethod
    def __values__(cls) -> dict[str, type]: ...
    # Every unknown attributes of a StructUnionBase is potentially an IOP
    # field, so an attribute of type Any.
    # It is also possible to set any attributes in a StructUnionBase as they
    # potentially are some IOP fields.
    # These methods are reset when generating the specific struct and union
    # IOPy stubs.
    def __getattr__(self, name: str) -> typing.Any: ...
    def __setattr__(self, name: str, val: typing.Any) -> None: ...

class IopStructUnionFieldDescription:
    help: IopHelpDescription
    generic_attributes: dict[str, typing.Any]
    iop_type: str
    py_type: type
    default_value: typing.Any | None
    optional: bool
    repeated: bool
    private: bool
    deprecated: bool
    min: int | float | None
    max: int | float | None
    min_occurs: int | None
    max_occurs: int | None
    min_length: int | None
    max_length: int | None
    length: int | None
    cdata: bool
    non_zero: bool
    non_empty: bool
    pattern: str | None

class IopStructUnionDescription:
    help: IopHelpDescription
    deprecated: bool
    generic_attributes: dict[str, typing.Any]
    fields: dict[str, IopStructUnionFieldDescription]

# }}}
# {{{ Union

class IopUnionDescription(IopStructUnionDescription): ...

class UnionBase(StructUnionBase):
    @typing.overload
    def __init__(self, self_dict: dict[str, typing.Any], /) -> None: ...
    @typing.overload
    def __init__(self, self_val: typing_extensions.Self, /) -> None: ...
    @typing.overload
    def __init__(self, val: typing.Any, /) -> None: ...
    @typing.overload
    def __init__(self, **kwargs: typing.Any) -> None: ...
    def get_object(self) -> typing.Any: ...
    def __object__(self) -> typing.Any: ...
    def get_key(self) -> str: ...
    def __key__(self) -> str: ...
    @classmethod
    def get_iop_description(cls) -> IopUnionDescription: ...

class Union(UnionBase): ...

# }}}
# {{{ Struct

class IopStructDescription(IopStructUnionDescription): ...

class IopClassStaticFieldDescription(IopStructUnionFieldDescription):
    value: typing.Any

class IopClassDescription(IopStructDescription):
    parent: type[StructBase] | None
    is_abstract: bool
    is_private: bool
    class_id: int
    statics: dict[str, IopClassStaticFieldDescription]
    cls_statics: dict[str, IopClassStaticFieldDescription]

class StructBase(StructUnionBase):
    @classmethod
    def get_iopslots(cls) -> str: ...
    @classmethod
    def __iopslots__(cls) -> str: ...
    @classmethod
    def get_class_attrs(cls) -> dict[str, typing.Any]: ...
    @classmethod
    def __get_class_attrs__(cls) -> dict[str, typing.Any]: ...
    @classmethod
    def get_iop_description(
        cls,
    ) -> IopStructDescription | IopClassDescription: ...

class Struct(StructBase): ...

@typing.type_check_only
class IsIopFieldOptional: ...

# }}}
# }}}
# {{{ RPCs

class ChannelBase: ...

_TRpcArg = typing.TypeVar('_TRpcArg')
_TRpcRes = typing.TypeVar('_TRpcRes')
_TRpcExn = typing.TypeVar('_TRpcExn')

class RPCArgs(typing.Generic[_TRpcArg, _TRpcRes, _TRpcExn]):
    rpc: RPCServer
    arg: _TRpcArg
    res: type[_TRpcRes]
    exn: type[_TRpcExn]
    hdr: ic__iop.Hdr

class RPCBase:
    Arg: type[StructUnionBase] | None
    Res: type[StructUnionBase] | None
    Exn: type[StructUnionBase] | None

    is_async: bool

    def arg(self) -> type[StructUnionBase] | None: ...
    def res(self) -> type[StructUnionBase] | None: ...
    def exn(self) -> type[StructUnionBase] | None: ...
    def name(self) -> str: ...
    def desc(self) -> str: ...

IfacePreHookCb: typing_extensions.TypeAlias = typing.Callable[..., typing.Any]
IfacePostHookCb: typing_extensions.TypeAlias = typing.Callable[
    ...,
    typing.Any,
]

class IfaceBase:
    __pre_hook__: typing.ClassVar[IfacePreHookCb | None]
    __post_hook__: typing.ClassVar[IfacePreHookCb | None]

    @classmethod
    def __fullname__(cls) -> str: ...
    @classmethod
    def __name__(cls) -> str: ...

class Iface(IfaceBase):
    # Every unknown attributes of an Iface is potentially a RPC.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> RPC: ...

@typing.type_check_only
class AsyncIface(IfaceBase):
    # Every unknown attributes of an AsyncIface is potentially an AsyncRPC.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> AsyncRPC: ...

@typing.type_check_only
class IfaceServer(IfaceBase):
    # Every unknown attributes of an IfaceServer is potentially a RPCServer.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> RPCServer: ...

class Module:
    @classmethod
    def __fullname__(cls) -> str: ...
    # Every unknown attributes of a Module is potentially an Iface.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> Iface: ...

@typing.type_check_only
class AsyncModule(Module):
    # Every unknown attributes of an AsyncModule is potentially an AsyncIface.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> AsyncIface: ...  # type: ignore[override]

@typing.type_check_only
class ModuleServer(Module):
    # Every unknown attributes of a ModuleServer is potentially a IfaceServer.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> IfaceServer: ...  # type: ignore[override]

# {{{ Client RPC

ChannelOnConnectCb: typing_extensions.TypeAlias = typing.Callable[
    [Channel],
    None,
]
ChannelOnDisconnectCb: typing_extensions.TypeAlias = typing.Callable[
    [Channel, bool],
    None,
]
ChannelOnExceptionCb: typing_extensions.TypeAlias = typing.Callable[
    [Exception],
    None,
]

class Channel(ChannelBase):
    @typing.overload
    def __init__(
        self,
        plugin: Plugin,
        uri: str,
        *,
        default_timeout: float = 60.0,
        no_act_timeout: float = 0.0,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> None: ...
    @typing.overload
    def __init__(
        self,
        plugin: Plugin,
        *,
        host: str,
        port: int,
        default_timeout: float = 60.0,
        no_act_timeout: float = 0.0,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> None: ...
    def connect(self, timeout: float | None = None) -> None: ...
    def is_connected(self) -> bool: ...
    def disconnect(self) -> None: ...
    def change_default_hdr(
        self,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> None: ...
    def get_default_hdr(self) -> ic__iop.Hdr: ...
    @property
    def on_connect(self) -> ChannelOnConnectCb | None: ...
    @on_connect.setter
    def on_connect(self, value: ChannelOnConnectCb | None) -> None: ...
    @on_connect.deleter
    def on_connect(self) -> None: ...
    @property
    def on_disconnect(self) -> ChannelOnDisconnectCb | None: ...
    @on_disconnect.setter
    def on_disconnect(self, value: ChannelOnDisconnectCb | None) -> None: ...
    @on_disconnect.deleter
    def on_disconnect(self) -> None: ...
    @property
    def on_exception(self) -> ChannelOnExceptionCb | None: ...
    @on_exception.setter
    def on_exception(self, value: ChannelOnExceptionCb | None) -> None: ...
    @on_exception.deleter
    def on_exception(self) -> None: ...
    # Every unknown attributes of a Channel is potentially a Module.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> Module: ...

class RPC(RPCBase):
    @property
    def channel(self) -> Channel: ...
    @typing.overload
    def call(
        self,
        obj: StructUnionBase | None,
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> StructUnionBase | None: ...
    @typing.overload
    def call(
        self,
        dct: dict[str, typing.Any],
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> StructUnionBase | None: ...
    @typing.overload
    def call(
        self,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
        **kwargs: typing.Any,
    ) -> StructUnionBase | None: ...
    @typing.overload
    def __call__(
        self,
        obj: StructUnionBase | None,
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> StructUnionBase | None: ...
    @typing.overload
    def __call__(
        self,
        dct: dict[str, typing.Any],
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> StructUnionBase | None: ...
    @typing.overload
    def __call__(
        self,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
        **kwargs: typing.Any,
    ) -> StructUnionBase | None: ...

class AsyncChannel(Channel):
    def connect(  # type: ignore[override]
        self,
        timeout: float | None = None,
    ) -> asyncio.Future[None]: ...
    # Every unknown attributes of an AsyncChannel is potentially an
    # AsyncModule.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> AsyncModule: ...

class AsyncRPC(RPCBase):
    @property
    def channel(self) -> AsyncChannel: ...
    @typing.overload
    def call(
        self,
        obj: StructUnionBase | None,
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> asyncio.Future[StructUnionBase | None]: ...
    @typing.overload
    def call(
        self,
        dct: dict[str, typing.Any],
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> asyncio.Future[StructUnionBase | None]: ...
    @typing.overload
    def call(
        self,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
        **kwargs: typing.Any,
    ) -> asyncio.Future[StructUnionBase | None]: ...
    @typing.overload
    def __call__(
        self,
        obj: StructUnionBase | None,
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> asyncio.Future[StructUnionBase | None]: ...
    @typing.overload
    def __call__(
        self,
        dct: dict[str, typing.Any],
        /,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> asyncio.Future[StructUnionBase | None]: ...
    @typing.overload
    def __call__(
        self,
        *,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
        **kwargs: typing.Any,
    ) -> asyncio.Future[StructUnionBase | None]: ...

# }}}
# {{{ Server RPC

ChannelServerOnConnectCb: typing_extensions.TypeAlias = typing.Callable[
    [ChannelServer, str],
    None,
]
ChannelServerOnDisconnectCb: typing_extensions.TypeAlias = typing.Callable[
    [ChannelServer, str],
    None,
]
ChannelServerOnExceptionCb: typing_extensions.TypeAlias = typing.Callable[
    [Exception],
    None,
]

class ChannelServer(ChannelBase):
    @typing.overload
    def listen(self, uri: str) -> None: ...
    @typing.overload
    def listen(self, *, host: str, port: int) -> None: ...
    @typing.overload
    def listen_block(self, timeout: float, uri: str) -> None: ...
    @typing.overload
    def listen_block(
        self,
        timeout: float,
        *,
        host: str,
        port: int,
    ) -> None: ...
    def stop(self) -> None: ...
    @property
    def on_connect(self) -> ChannelServerOnConnectCb | None: ...
    @on_connect.setter
    def on_connect(self, value: ChannelServerOnConnectCb | None) -> None: ...
    @on_connect.deleter
    def on_connect(self) -> None: ...
    @property
    def on_disconnect(self) -> ChannelServerOnDisconnectCb | None: ...
    @on_disconnect.setter
    def on_disconnect(
        self,
        value: ChannelServerOnDisconnectCb | None,
    ) -> None: ...
    @on_disconnect.deleter
    def on_disconnect(self) -> None: ...
    @property
    def on_exception(self) -> ChannelServerOnExceptionCb | None: ...
    @on_exception.setter
    def on_exception(
        self,
        value: ChannelServerOnExceptionCb | None,
    ) -> None: ...
    @on_exception.deleter
    def on_exception(self) -> None: ...
    @property
    def is_listening(self) -> bool: ...
    # Every unknown attributes of a ChannelServer is potentially a
    # ModuleServer.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> ModuleServer: ...

RPCServerImplCb: typing_extensions.TypeAlias = typing.Callable[
    [RPCArgs[_TRpcArg, _TRpcRes, _TRpcExn]],
    _TRpcRes | _TRpcExn,
]

class RPCServer(RPCBase):
    @property
    def channel(self) -> ChannelServer: ...

    RpcArgs: typing_extensions.TypeAlias = RPCArgs[
        StructUnionBase | None,
        StructUnionBase | None,
        StructUnionBase | None,
    ]
    RpcRes: typing_extensions.TypeAlias = type[StructUnionBase] | None

    @property
    def impl(
        self,
    ) -> (
        RPCServerImplCb[
            StructUnionBase | None,
            StructUnionBase | None,
            StructUnionBase | None,
        ]
        | None
    ): ...
    @impl.setter
    def impl(
        self,
        value: RPCServerImplCb[
            StructUnionBase | None,
            StructUnionBase | None,
            StructUnionBase | None,
        ]
        | None,
    ) -> None: ...
    @impl.deleter
    def impl(self) -> None: ...
    @typing.overload
    def wait(self, timeout: float, uri: str, count: int = 1) -> None: ...
    @typing.overload
    def wait(
        self,
        timeout: float,
        *,
        host: str,
        port: int,
        count: int = 1,
    ) -> None: ...

# }}}
# }}}
# {{{ Plugin

class Interfaces:
    # Every unknown attributes of an Interfaces is potentially a IfaceBase.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> IfaceBase: ...

class Package:
    interfaces: Interfaces

    def __name__(self) -> str: ...
    # Every unknown attributes of a Package is potentially a type[Basic].
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> type[Basic]: ...

@typing.type_check_only
class Void(Struct): ...

class Plugin:
    modules: dict[str, Module]
    Void: type[Void]

    def __init__(self, dso_path: str | bytes): ...
    @property
    def dsopath(self) -> str: ...
    @property
    def __dsopath__(self) -> str: ...
    @property
    def __modules__(self) -> dict[str, Module]: ...
    def get_type_from_fullname(self, fullname: str) -> type[Basic]: ...
    def __get_type_from_fullname__(self, fullname: str) -> type[Basic]: ...
    def get_iface_type_from_fullname(
        self,
        fullname: str,
    ) -> type[IfaceBase]: ...
    def __get_iface_type_from_fullname__(
        self,
        fullname: str,
    ) -> type[IfaceBase]: ...
    def register(self) -> typing_extensions.Self: ...
    def _get_plugin(self) -> typing_extensions.Self: ...
    @typing.overload
    def connect(
        self,
        uri: str,
        *,
        default_timeout: float | None = None,
        connect_timeout: float | None = None,
        no_act_timeout: float = 0.0,
        timeout: float | None = None,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> Channel: ...
    @typing.overload
    def connect(
        self,
        *,
        host: str,
        port: int,
        default_timeout: float | None = None,
        connect_timeout: float | None = None,
        no_act_timeout: float = 0.0,
        timeout: float | None = None,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> Channel: ...
    @typing.overload
    def async_connect(
        self,
        uri: str,
        *,
        default_timeout: float | None = None,
        connect_timeout: float | None = None,
        no_act_timeout: float = 0.0,
        timeout: float | None = None,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> asyncio.Future[AsyncChannel]: ...
    @typing.overload
    def async_connect(
        self,
        *,
        host: str,
        port: int,
        default_timeout: float | None = None,
        connect_timeout: float | None = None,
        no_act_timeout: float = 0.0,
        timeout: float | None = None,
        _timeout: float | None = None,
        _login: str | None = None,
        _group: str | None = None,
        _password: str | None = None,
        _kind: str | None = None,
        _workspace_id: int | None = None,
        _dealias: bool | None = None,
        _hdr: ic__iop.Hdr | None = None,
    ) -> asyncio.Future[AsyncChannel]: ...
    def channel_server(self) -> ChannelServer: ...
    def ChannelServer(self) -> ChannelServer: ...  # noqa: N802 (invalid-function-name)
    @property
    def default_pre_hook(self) -> IfacePreHookCb | None: ...
    @default_pre_hook.setter
    def default_pre_hook(self, value: IfacePreHookCb | None) -> None: ...
    @default_pre_hook.deleter
    def default_pre_hook(self) -> None: ...
    @property
    def default_post_hook(self) -> IfacePostHookCb | None: ...
    @default_post_hook.setter
    def default_post_hook(self, value: IfacePostHookCb | None) -> None: ...
    @default_post_hook.deleter
    def default_post_hook(self) -> None: ...
    def load_dso(self, key: str, dso_path: str | bytes) -> None: ...
    def unload_dso(self, key: str) -> None: ...
    # Every unknown attributes of a Plugin is potentially a Package.
    # This method is reset when generating the specific IOP stubs.
    def __getattr__(self, name: str) -> Package: ...

# }}}
# {{{ Module functions

def set_json_flags(
    no_whitespaces: bool | None = None,
    no_trailing_eol: bool | None = None,
    skip_private: bool | None = None,
    skip_default: bool | None = None,
    skip_empty_arrays: bool | None = None,
    skip_empty_structs: bool | None = None,
    shorten_data: bool | None = None,
    skip_class_names: bool | None = None,
    skip_optional_class_names: bool | None = None,
    minimal: bool | None = None,
) -> None: ...
def thr_attach() -> None: ...
def thr_detach() -> None: ...

# }}}
