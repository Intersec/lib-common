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
# pylint: disable=no-self-use


import typing


# {{{ Errors


class Error(Exception):
    pass


class RpcError(Error):
    pass


# }}}
# {{{ Types
# {{{ Base


class Basic:
    pass


class IopHelpDescription:
    brief: typing.Optional[str]
    details: typing.Optional[str]
    warning: typing.Optional[str]
    example: typing.Optional[str]


# }}}
# {{{ Enum


class IopEnumValueDescription:
    help: IopHelpDescription
    generic_attributes: typing.Dict[str, object]
    aliases: typing.Tuple[str]


class IopEnumDescription:
    help: IopHelpDescription
    strict: bool
    generic_attributes: typing.Dict[str, object]
    values: typing.Dict[str, IopEnumValueDescription]


class EnumBase(Basic):
    def __init__(self, val: typing.Union[int, str]) -> None: ...

    @classmethod
    def name(cls) -> str: ...

    @classmethod
    def fullname(cls) -> str: ...

    @classmethod
    def __fullname__(cls) -> str: ...

    @classmethod
    def values(cls) -> typing.Dict[str, int]: ...

    @classmethod
    def __values__(cls) -> typing.Dict[str, int]: ...

    @classmethod
    def ranges(cls) -> typing.Dict[str, int]: ...

    @classmethod
    def __ranges__(cls) -> typing.Dict[str, int]: ...

    @classmethod
    def get_iop_description(cls) -> IopEnumDescription: ...

    def set(self, val: typing.Union[int, str]) -> None: ...

    def get_as_int(self) -> int: ...

    def __int__(self) -> int: ...

    def __index__(self) -> int: ...

    def get_as_str(self) -> str: ...

    def __str__(self) -> str: ...

    def __richcmp__(self, other: object, op: int) -> bool: ...


class Enum(EnumBase):
    pass


# }}}
# {{{ StructUnionBase


class StructUnionBase(Basic):
    @typing.overload
    @classmethod
    def from_file(cls, _json: str,
                  single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def from_file(cls, _yaml: str,
                  single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def from_file(cls, _xml: str,
                  single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def from_file(cls, _hex: str,
                  single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def from_file(cls, _bin: str,
                  single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def __from_file__(cls, _json: str,
                      single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def __from_file__(cls, _yaml: str,
                      single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def __from_file__(cls, _xml: str,
                      single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def __from_file__(cls, _hex: str,
                      single: bool = True) -> 'StructUnionBase': ...

    @typing.overload
    @classmethod
    def __from_file__(cls, _bin: str,
                      single: bool = True) -> 'StructUnionBase': ...

    def __richcmp__(self, other: object, op: int) -> bool: ...

    def to_json(self, no_whitespaces: typing.Optional[bool] = None,
                no_trailing_eol: typing.Optional[bool] = None,
                skip_private: typing.Optional[bool] = None,
                skip_default: typing.Optional[bool] = None,
                skip_empty_arrays: typing.Optional[bool] = None,
                skip_empty_structs: typing.Optional[bool] = None,
                shorten_data: typing.Optional[bool] = None,
                skip_class_names: typing.Optional[bool] = None,
                skip_optional_class_names: typing.Optional[bool] = None,
                minimal: typing.Optional[bool] = None) -> str: ...

    def __json__(self, no_whitespaces: typing.Optional[bool] = None,
                 no_trailing_eol: typing.Optional[bool] = None,
                 skip_private: typing.Optional[bool] = None,
                 skip_default: typing.Optional[bool] = None,
                 skip_empty_arrays: typing.Optional[bool] = None,
                 skip_empty_structs: typing.Optional[bool] = None,
                 shorten_data: typing.Optional[bool] = None,
                 skip_class_names: typing.Optional[bool] = None,
                 skip_optional_class_names: typing.Optional[bool] = None,
                 minimal: typing.Optional[bool] = None) -> str: ...

    def to_yaml(self, no_whitespaces: typing.Optional[bool] = None,
                no_trailing_eol: typing.Optional[bool] = None,
                skip_private: typing.Optional[bool] = None,
                skip_default: typing.Optional[bool] = None,
                skip_empty_arrays: typing.Optional[bool] = None,
                skip_empty_structs: typing.Optional[bool] = None,
                shorten_data: typing.Optional[bool] = None,
                skip_class_names: typing.Optional[bool] = None,
                skip_optional_class_names: typing.Optional[bool] = None,
                minimal: typing.Optional[bool] = None) -> str: ...

    def __yaml__(self, no_whitespaces: typing.Optional[bool] = None,
                 no_trailing_eol: typing.Optional[bool] = None,
                 skip_private: typing.Optional[bool] = None,
                 skip_default: typing.Optional[bool] = None,
                 skip_empty_arrays: typing.Optional[bool] = None,
                 skip_empty_structs: typing.Optional[bool] = None,
                 shorten_data: typing.Optional[bool] = None,
                 skip_class_names: typing.Optional[bool] = None,
                 skip_optional_class_names: typing.Optional[bool] = None,
                 minimal: typing.Optional[bool] = None) -> str: ...

    def to_bin(self, no_whitespaces: typing.Optional[bool] = None,
               no_trailing_eol: typing.Optional[bool] = None,
               skip_private: typing.Optional[bool] = None,
               skip_default: typing.Optional[bool] = None,
               skip_empty_arrays: typing.Optional[bool] = None,
               skip_empty_structs: typing.Optional[bool] = None,
               shorten_data: typing.Optional[bool] = None,
               skip_class_names: typing.Optional[bool] = None,
               skip_optional_class_names: typing.Optional[bool] = None,
               minimal: typing.Optional[bool] = None) -> str: ...

    def __bin__(self, no_whitespaces: typing.Optional[bool] = None,
                no_trailing_eol: typing.Optional[bool] = None,
                skip_private: typing.Optional[bool] = None,
                skip_default: typing.Optional[bool] = None,
                skip_empty_arrays: typing.Optional[bool] = None,
                skip_empty_structs: typing.Optional[bool] = None,
                shorten_data: typing.Optional[bool] = None,
                skip_class_names: typing.Optional[bool] = None,
                skip_optional_class_names: typing.Optional[bool] = None,
                minimal: typing.Optional[bool] = None) -> str: ...

    def to_hex(self, no_whitespaces: typing.Optional[bool] = None,
               no_trailing_eol: typing.Optional[bool] = None,
               skip_private: typing.Optional[bool] = None,
               skip_default: typing.Optional[bool] = None,
               skip_empty_arrays: typing.Optional[bool] = None,
               skip_empty_structs: typing.Optional[bool] = None,
               shorten_data: typing.Optional[bool] = None,
               skip_class_names: typing.Optional[bool] = None,
               skip_optional_class_names: typing.Optional[bool] = None,
               minimal: typing.Optional[bool] = None) -> str: ...

    def __hex__(self, no_whitespaces: typing.Optional[bool] = None,
                no_trailing_eol: typing.Optional[bool] = None,
                skip_private: typing.Optional[bool] = None,
                skip_default: typing.Optional[bool] = None,
                skip_empty_arrays: typing.Optional[bool] = None,
                skip_empty_structs: typing.Optional[bool] = None,
                shorten_data: typing.Optional[bool] = None,
                skip_class_names: typing.Optional[bool] = None,
                skip_optional_class_names: typing.Optional[bool] = None,
                minimal: typing.Optional[bool] = None) -> str: ...

    def to_xml(self, no_whitespaces: typing.Optional[bool] = None,
               no_trailing_eol: typing.Optional[bool] = None,
               skip_private: typing.Optional[bool] = None,
               skip_default: typing.Optional[bool] = None,
               skip_empty_arrays: typing.Optional[bool] = None,
               skip_empty_structs: typing.Optional[bool] = None,
               shorten_data: typing.Optional[bool] = None,
               skip_class_names: typing.Optional[bool] = None,
               skip_optional_class_names: typing.Optional[bool] = None,
               minimal: typing.Optional[bool] = None) -> str: ...

    def __xml__(self, no_whitespaces: typing.Optional[bool] = None,
                no_trailing_eol: typing.Optional[bool] = None,
                skip_private: typing.Optional[bool] = None,
                skip_default: typing.Optional[bool] = None,
                skip_empty_arrays: typing.Optional[bool] = None,
                skip_empty_structs: typing.Optional[bool] = None,
                shorten_data: typing.Optional[bool] = None,
                skip_class_names: typing.Optional[bool] = None,
                skip_optional_class_names: typing.Optional[bool] = None,
                minimal: typing.Optional[bool] = None) -> str: ...

    def to_dict(self, skip_private: typing.Optional[bool] = None,
                skip_default: typing.Optional[bool] = None,
                skip_empty_arrays: typing.Optional[bool] = None,
                skip_empty_structs: typing.Optional[bool] = None,
                skip_class_names: typing.Optional[bool] = None,
                skip_optional_class_names: typing.Optional[bool] = None,
                minimal: typing.Optional[bool] = None) -> str: ...

    def __str__(self) -> str: ...

    @classmethod
    def __fullname__(cls) -> str: ...

    @classmethod
    def get_fields_name(cls) -> typing.List[str]: ...

    @classmethod
    def __get_fields_name__(cls) -> typing.List[str]: ...

    @classmethod
    def get_desc(cls) -> str: ...

    @classmethod
    def __desc__(cls) -> str: ...

    @classmethod
    def get_values(cls) -> typing.Dict[str, type]: ...

    @classmethod
    def __values__(cls) -> typing.Dict[str, type]: ...


class IopStructUnionFieldDescription:
    help: IopHelpDescription
    generic_attributes: typing.Dict[str, object]
    iop_type: str
    py_type: type
    default_value: typing.Optional[object]
    optional: bool
    repeated: bool
    private: bool
    deprecated: bool
    min: typing.Optional[typing.Union[int, float]]
    max: typing.Optional[typing.Union[int, float]]
    min_occurs: typing.Optional[int]
    max_occurs: typing.Optional[int]
    min_length: typing.Optional[int]
    max_length: typing.Optional[int]
    length: typing.Optional[int]
    cdata: bool
    non_zero: bool
    non_empty: bool
    pattern: typing.Optional[str]


class IopStructUnionDescription:
    help: IopHelpDescription
    deprecated: bool
    generic_attributes: typing.Dict[str, object]
    fields: typing.Dict[str, IopStructUnionFieldDescription]


# }}}
# {{{ Union


class IopUnionDescription(IopStructUnionDescription):
    pass


class UnionBase(StructUnionBase):
    @typing.overload
    def __init__(self, self_dict: typing.Dict[str, object]) -> None: ...

    @typing.overload
    def __init__(self, self_val: 'UnionBase') -> None: ...

    @typing.overload
    def __init__(self, val: object) -> None: ...

    @typing.overload
    def __init__(self, **kwargs: object) -> None: ...

    def get_object(self) -> object: ...

    def __object__(self) -> object: ...

    def get_key(self) -> str: ...

    def __key__(self) -> str: ...

    @classmethod
    def get_iop_description(cls) -> IopUnionDescription: ...


class Union(UnionBase):
    pass


# }}}
# {{{ Struct


class IopStructDescription(IopStructUnionDescription):
    pass


class IopClassStaticFieldDescription(IopStructUnionFieldDescription):
    value: object


class IopClassDescription(IopStructDescription):
    parent: typing.Optional[typing.Type['StructBase']]
    is_abstract: bool
    is_private: bool
    class_id: int
    statics: typing.Dict[str, IopClassStaticFieldDescription]
    cls_statics: typing.Dict[str, IopClassStaticFieldDescription]


class StructBase(StructUnionBase):
    @typing.overload
    def __init__(self, self_dict: typing.Dict[str, object]) -> None: ...

    @typing.overload
    def __init__(self, self_val: 'UnionBase') -> None: ...

    @typing.overload
    def __init__(self, **kwargs: object) -> None: ...

    @classmethod
    def get_iopslots(cls) -> str: ...

    @classmethod
    def __iopslots__(cls) -> str: ...

    @classmethod
    def get_class_attrs(cls) -> typing.Dict[str, object]: ...

    @classmethod
    def __get_class_attrs__(cls) -> typing.Dict[str, object]: ...

    @classmethod
    def get_iop_description(cls) -> typing.Union[IopStructDescription,
                                                 IopClassDescription]: ...


class Struct(StructBase):
    pass


# }}}
# }}}
# {{{ RPCs


class ChannelBase:
    pass


class RPCBase:
    def name(self) -> str: ...

    def _async(self) -> bool: ...

    def arg(self) -> typing.Optional[typing.Type[StructUnionBase]]: ...

    def res(self) -> typing.Optional[typing.Type[StructUnionBase]]: ...

    def exn(self) -> typing.Optional[typing.Type[StructUnionBase]]: ...

    def desc(self) -> str: ...


IfacePreHookCb = typing.Callable[..., typing.Any]
IfacePostHookCb = typing.Callable[..., typing.Any]


class IfaceBase:
    __pre_hook__: typing.ClassVar[typing.Optional[IfacePreHookCb]]
    __post_hook__: typing.ClassVar[typing.Optional[IfacePreHookCb]]

    @classmethod
    def __fullname__(cls) -> str: ...

    def __name__(self) -> str: ...


Iface = IfaceBase


class Module:
    @classmethod
    def __fullname__(cls) -> str: ...


# {{{ Client RPC


ChannelOnConnectCb = typing.Callable[['Channel'], None]
ChannelOnDisconnectCb = typing.Callable[['Channel', bool], None]


class Channel(ChannelBase):
    @typing.overload
    def __init__(self, plugin: 'Plugin', uri: str, *,
                 default_timeout: float = 60.0, no_act_timeout: float = 0.0,
                 _login: typing.Optional[str] = None,
                 _group: typing.Optional[str] = None,
                 _password: typing.Optional[str] = None,
                 _kind: typing.Optional[str] = None,
                 _workspace_id: typing.Optional[int] = None,
                 _dealias: typing.Optional[bool] = None,
                 _hdr: typing.Optional[StructUnionBase] = None,
    ) -> None: ...

    @typing.overload
    def __init__(self, plugin: 'Plugin', *,
                 host: str, port: int,
                 default_timeout: float = 60.0, no_act_timeout: float = 0.0,
                 _login: typing.Optional[str] = None,
                 _group: typing.Optional[str] = None,
                 _password: typing.Optional[str] = None,
                 _kind: typing.Optional[str] = None,
                 _workspace_id: typing.Optional[int] = None,
                 _dealias: typing.Optional[bool] = None,
                 _hdr: typing.Optional[StructUnionBase] = None,
    ) -> None: ...

    def connect(self, timeout: typing.Optional[float] = None) -> None: ...

    def is_connected(self) -> bool: ...

    def disconnect(self) -> None: ...

    def change_default_hdr(self,
                           _login: typing.Optional[str] = None,
                           _group: typing.Optional[str] = None,
                           _password: typing.Optional[str] = None,
                           _kind: typing.Optional[str] = None,
                           _workspace_id: typing.Optional[int] = None,
                           _dealias: typing.Optional[bool] = None,
                           _hdr: typing.Optional[StructUnionBase] = None,
    ) -> None: ...

    def get_default_hdr(self) -> StructUnionBase: ...

    @property
    def on_connect(self) -> typing.Optional[ChannelOnConnectCb]: ...

    @on_connect.setter
    def on_connect(self,
                   value: typing.Optional[ChannelOnConnectCb]) -> None: ...

    @on_connect.deleter
    def on_connect(self) -> None: ...

    @property
    def on_disconnect(self) -> typing.Optional[ChannelOnDisconnectCb]: ...

    @on_disconnect.setter
    def on_disconnect(self, value: typing.Optional[ChannelOnDisconnectCb]
    ) -> None: ...

    @on_disconnect.deleter
    def on_disconnect(self) -> None: ...


class RPC(RPCBase):
    @property
    def channel(self) -> Channel: ...

    @typing.overload
    def call(self, arg_val: object,
             _timeout: typing.Optional[float] = None,
             _login: typing.Optional[str] = None,
             _group: typing.Optional[str] = None,
             _password: typing.Optional[str] = None,
             _kind: typing.Optional[str] = None,
             _workspace_id: typing.Optional[int] = None,
             _dealias: typing.Optional[bool] = None,
             _hdr: typing.Optional[StructUnionBase] = None,
    ) -> typing.Optional[StructUnionBase]: ...

    @typing.overload
    def call(self,
             _timeout: typing.Optional[float] = None,
             _login: typing.Optional[str] = None,
             _group: typing.Optional[str] = None,
             _password: typing.Optional[str] = None,
             _kind: typing.Optional[str] = None,
             _workspace_id: typing.Optional[int] = None,
             _dealias: typing.Optional[bool] = None,
             _hdr: typing.Optional[StructUnionBase] = None,
             **kwargs: object
    ) -> typing.Optional[StructUnionBase]: ...

    @typing.overload
    def __call__(self, arg_val: object,
                 _timeout: typing.Optional[float] = None,
                 _login: typing.Optional[str] = None,
                 _group: typing.Optional[str] = None,
                 _password: typing.Optional[str] = None,
                 _kind: typing.Optional[str] = None,
                 _workspace_id: typing.Optional[int] = None,
                 _dealias: typing.Optional[bool] = None,
                 _hdr: typing.Optional[StructUnionBase] = None,
    ) -> typing.Optional[StructUnionBase]: ...

    @typing.overload
    def __call__(self,
                 _timeout: typing.Optional[float] = None,
                 _login: typing.Optional[str] = None,
                 _group: typing.Optional[str] = None,
                 _password: typing.Optional[str] = None,
                 _kind: typing.Optional[str] = None,
                 _workspace_id: typing.Optional[int] = None,
                 _dealias: typing.Optional[bool] = None,
                 _hdr: typing.Optional[StructUnionBase] = None,
                 **kwargs: object
    ) -> typing.Optional[StructUnionBase]: ...


class AsyncChannel(Channel):
    def connect(self, # type: ignore[override]
                timeout: typing.Optional[float] = None,
    ) -> typing.Awaitable[None]: ...


class AsyncRPC(RPCBase):
    @property
    def channel(self) -> AsyncChannel: ...

    @typing.overload
    def call(self, arg_val: object,
             _timeout: typing.Optional[float] = None,
             _login: typing.Optional[str] = None,
             _group: typing.Optional[str] = None,
             _password: typing.Optional[str] = None,
             _kind: typing.Optional[str] = None,
             _workspace_id: typing.Optional[int] = None,
             _dealias: typing.Optional[bool] = None,
             _hdr: typing.Optional[StructUnionBase] = None,
    ) -> typing.Awaitable[typing.Optional[StructUnionBase]]: ...

    @typing.overload
    def call(self,
             _timeout: typing.Optional[float] = None,
             _login: typing.Optional[str] = None,
             _group: typing.Optional[str] = None,
             _password: typing.Optional[str] = None,
             _kind: typing.Optional[str] = None,
             _workspace_id: typing.Optional[int] = None,
             _dealias: typing.Optional[bool] = None,
             _hdr: typing.Optional[StructUnionBase] = None,
             **kwargs: object
    ) -> typing.Awaitable[typing.Optional[StructUnionBase]]: ...

    @typing.overload
    def __call__(self, arg_val: object,
                 _timeout: typing.Optional[float] = None,
                 _login: typing.Optional[str] = None,
                 _group: typing.Optional[str] = None,
                 _password: typing.Optional[str] = None,
                 _kind: typing.Optional[str] = None,
                 _workspace_id: typing.Optional[int] = None,
                 _dealias: typing.Optional[bool] = None,
                 _hdr: typing.Optional[StructUnionBase] = None,
    ) -> typing.Awaitable[typing.Optional[StructUnionBase]]: ...

    @typing.overload
    def __call__(self,
                 _timeout: typing.Optional[float] = None,
                 _login: typing.Optional[str] = None,
                 _group: typing.Optional[str] = None,
                 _password: typing.Optional[str] = None,
                 _kind: typing.Optional[str] = None,
                 _workspace_id: typing.Optional[int] = None,
                 _dealias: typing.Optional[bool] = None,
                 _hdr: typing.Optional[StructUnionBase] = None,
                 **kwargs: object
    ) -> typing.Awaitable[typing.Optional[StructUnionBase]]: ...

# }}}
# {{{ Server RPC


ChannelServerOnConnectCb = typing.Callable[['ChannelServer', str], None]
ChannelServerOnDisconnectCb = typing.Callable[['ChannelServer', str], None]


class ChannelServer(ChannelBase):
    @typing.overload
    def listen(self, uri: str) -> None: ...

    @typing.overload
    def listen(self, *, host: str, port: int) -> None: ...

    @typing.overload
    def listen_block(self, timeout: float, uri: str) -> None: ...

    @typing.overload
    def listen_block(self, timeout: float, *, host: str,
                     port: int) -> None: ...

    def stop(self) -> None: ...

    @property
    def on_connect(self) -> typing.Optional[ChannelServerOnConnectCb]: ...

    @on_connect.setter
    def on_connect(self, value: typing.Optional[ChannelServerOnConnectCb]
    ) -> None: ...

    @on_connect.deleter
    def on_connect(self) -> None: ...

    @property
    def on_disconnect(self
    ) -> typing.Optional[ChannelServerOnDisconnectCb]: ...

    @on_disconnect.setter
    def on_disconnect(self,
                      value: typing.Optional[ChannelServerOnDisconnectCb]
    ) -> None: ...

    @on_disconnect.deleter
    def on_disconnect(self) -> None: ...

    @property
    def is_listening(self) -> bool: ...


class RPCServerArgs:
    rpc: 'RPCServer'
    arg: StructUnionBase
    res: typing.Optional[typing.Type[StructUnionBase]]
    exn: typing.Optional[typing.Type[StructUnionBase]]
    hdr: StructUnionBase

RPCServerImplCb = typing.Callable[[RPCServerArgs],
                                  typing.Optional[StructUnionBase]]


class RPCServer(RPCBase):
    @property
    def channel(self) -> ChannelServer: ...

    @property
    def impl(self) -> typing.Optional[RPCServerImplCb]: ...

    @impl.setter
    def impl(self, value: typing.Optional[RPCServerImplCb]) -> None: ...

    @impl.deleter
    def impl(self) -> None: ...

    @typing.overload
    def wait(self, timeout: float, uri: str, count: int = 1) -> None: ...

    @typing.overload
    def wait(self, timeout: float, *, host: str, port: int,
             count: int = 1) -> None: ...

# }}}
# }}}
# {{{ Plugin


class Interfaces:
    pass


class Package:
    interfaces: Interfaces

    def __name__(self) -> str: ...


@typing.type_check_only
class Void(Struct):
    pass


class Plugin:
    modules: typing.Dict[str, Module]
    metaclass: type
    metaclass_interfaces: type
    Void: typing.Type[Void]

    def __init__(self, dso_path: typing.Union[str, bytes]) -> None: ...

    @property
    def dsopath(self) -> str: ...

    @property
    def __dsopath__(self) -> str: ...

    @property
    def __modules__(self) -> typing.Dict[str, Module]: ...

    def get_type_from_fullname(self, fullname: str) -> typing.Type[Basic]: ...

    def __get_type_from_fullname__(self,
                                   fullname: str) -> typing.Type[Basic]: ...

    def get_iface_type_from_fullname(self, fullname: str
    ) -> typing.Type[IfaceBase]: ...

    def __get_iface_type_from_fullname__(self, fullname: str
    ) -> typing.Type[IfaceBase]: ...

    def register(self) -> 'Plugin': ...

    def _get_plugin(self) -> 'Plugin': ...

    def upgrade(self, index: typing.Optional[int] = None,
                force_replace: bool = False) -> typing.Any: ...

    @typing.overload
    def connect(self, uri: str, *,
                default_timeout: typing.Optional[float] = None,
                connect_timeout: typing.Optional[float] = None,
                no_act_timeout: float = 0.0,
                timeout: typing.Optional[float] = None,
                _timeout: typing.Optional[float] = None,
                _login: typing.Optional[str] = None,
                _group: typing.Optional[str] = None,
                _password: typing.Optional[str] = None,
                _kind: typing.Optional[str] = None,
                _workspace_id: typing.Optional[int] = None,
                _dealias: typing.Optional[bool] = None,
                _hdr: typing.Optional[StructUnionBase] = None,
    ) -> Channel: ...

    @typing.overload
    def connect(self, *, host: str, port: int,
                default_timeout: typing.Optional[float] = None,
                connect_timeout: typing.Optional[float] = None,
                no_act_timeout: float = 0.0,
                timeout: typing.Optional[float] = None,
                _timeout: typing.Optional[float] = None,
                _login: typing.Optional[str] = None,
                _group: typing.Optional[str] = None,
                _password: typing.Optional[str] = None,
                _kind: typing.Optional[str] = None,
                _workspace_id: typing.Optional[int] = None,
                _dealias: typing.Optional[bool] = None,
                _hdr: typing.Optional[StructUnionBase] = None,
    ) -> Channel: ...

    @typing.overload
    def async_connect(self, plugin: 'Plugin', uri: str, *,
                      default_timeout: typing.Optional[float] = None,
                      connect_timeout: typing.Optional[float] = None,
                      no_act_timeout: float = 0.0,
                      timeout: typing.Optional[float] = None,
                      _timeout: typing.Optional[float] = None,
                      _login: typing.Optional[str] = None,
                      _group: typing.Optional[str] = None,
                      _password: typing.Optional[str] = None,
                      _kind: typing.Optional[str] = None,
                      _workspace_id: typing.Optional[int] = None,
                      _dealias: typing.Optional[bool] = None,
                      _hdr: typing.Optional[StructUnionBase] = None,
    ) -> typing.Awaitable[AsyncChannel]: ...

    @typing.overload
    def async_connect(self, plugin: 'Plugin', *, host: str, port: int,
                      default_timeout: typing.Optional[float] = None,
                      connect_timeout: typing.Optional[float] = None,
                      no_act_timeout: float = 0.0,
                      timeout: typing.Optional[float] = None,
                      _timeout: typing.Optional[float] = None,
                      _login: typing.Optional[str] = None,
                      _group: typing.Optional[str] = None,
                      _password: typing.Optional[str] = None,
                      _kind: typing.Optional[str] = None,
                      _workspace_id: typing.Optional[int] = None,
                      _dealias: typing.Optional[bool] = None,
                      _hdr: typing.Optional[StructUnionBase] = None,
    ) -> typing.Awaitable[AsyncChannel]: ...

    def channel_server(self) -> ChannelServer: ...

    def ChannelServer(self) -> ChannelServer: ...

    @property
    def default_pre_hook(self) -> typing.Optional[IfacePreHookCb]: ...

    @default_pre_hook.setter
    def default_pre_hook(self, value: typing.Optional[IfacePreHookCb]
    ) -> None: ...

    @default_pre_hook.deleter
    def default_pre_hook(self) -> None: ...

    @property
    def default_post_hook(self) -> typing.Optional[IfacePostHookCb]: ...

    @default_post_hook.setter
    def default_post_hook(self, value: typing.Optional[IfacePostHookCb]
    ) -> None: ...

    @default_post_hook.deleter
    def default_post_hook(self) -> None: ...

    def load_dso(self, key: str,
                 dso_path: typing.Union[str, bytes]) -> None: ...

    def unload_dso(self, key: str) -> None: ...


# }}}
# {{{ Module functions


def set_json_flags(no_whitespaces: typing.Optional[bool] = None,
                   no_trailing_eol: typing.Optional[bool] = None,
                   skip_private: typing.Optional[bool] = None,
                   skip_default: typing.Optional[bool] = None,
                   skip_empty_arrays: typing.Optional[bool] = None,
                   skip_empty_structs: typing.Optional[bool] = None,
                   shorten_data: typing.Optional[bool] = None,
                   skip_class_names: typing.Optional[bool] = None,
                   skip_optional_class_names: typing.Optional[bool] = None,
                   minimal: typing.Optional[bool] = None) -> None: ...


def thr_attach() -> None: ...


def thr_detach() -> None: ...


# }}}
