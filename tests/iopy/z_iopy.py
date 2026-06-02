#!/usr/bin/env python3
###########################################################################
#                                                                         #
# Copyright 2026 INTERSEC SA                                              #
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
from __future__ import annotations

import asyncio
import copy
import json
import multiprocessing
import os
import signal
import socket
import subprocess
import sys
import threading
import time
import warnings
from collections.abc import Callable, Generator
from contextlib import contextmanager
from typing import TYPE_CHECKING, Any, TypeVar, assert_type, cast

from typing_extensions import ParamSpec, override

import iopy
import zpycore as z

SELF_PATH = os.path.dirname(__file__)
TEST_PATH = os.path.join(SELF_PATH, 'testsuite')


if TYPE_CHECKING:
    import ic__iop
    import test__iop
    import test_dso__iop
    import test_iop_plugin2__iop
    import test_iop_plugin__iop


# {{{ Helpers


PORT_COUNT = 9900


def make_uri() -> str:
    global PORT_COUNT
    PORT_COUNT -= 1
    return f'127.0.0.1:{PORT_COUNT:d}'


def z_iopy_thread_cb(
    iface: test__iop.InterfaceA_Iface,
    obj_a: test__iop.ClassA_ParamType,
    res_list: list[test__iop.InterfaceA_funA_Res],
) -> None:
    try:
        res = iface.funA(a=obj_a)
    except Exception:  # noqa: BLE001 (blind-except)
        return
    res_list.append(res)


def z_iopy_fork_child(
    iface: test__iop.InterfaceA_Iface,
    obj_a: test__iop.ClassA_ParamType,
    exp_res: test__iop.InterfaceA_funA_Res,
    do_threads: bool,
) -> None:
    try:
        res = iface.funA(a=obj_a)
    except Exception as e:  # noqa: BLE001 (blind-except)
        sys.stderr.write(f'{e!s}\n')
        os._exit(1)

    if res != exp_res:
        sys.stderr.write(
            'unexpected result for interfaceA.funA, expected '
            f'{exp_res}, got {res}\n'
        )
        os._exit(2)

    try:
        z_iopy_test_threads_and_forks(
            iface, obj_a, exp_res, do_threads, False
        )
    except Exception as e:  # noqa: BLE001 (blind-except)
        sys.stderr.write(f'{e!s}\n')
        os._exit(3)

    os._exit(0)


def z_iopy_test_threads_and_forks(
    iface: test__iop.InterfaceA_Iface,
    obj_a: test__iop.ClassA_ParamType,
    exp_res: test__iop.InterfaceA_funA_Res,
    do_threads: bool,
    do_forks: bool,
) -> None:
    res_list: list[test__iop.InterfaceA_funA_Res] = []
    threads = []
    pids = []
    res_codes = []

    if do_threads:
        for _ in range(10):
            t = threading.Thread(
                target=z_iopy_thread_cb, args=(iface, obj_a, res_list)
            )
            t.start()
            threads.append(t)

    if do_forks:
        for _ in range(10):
            new_pid = os.fork()
            assert new_pid >= 0
            if new_pid == 0:
                z_iopy_fork_child(iface, obj_a, exp_res, do_threads)
            else:
                pids.append(new_pid)

    for t in threads:
        t.join()

    for p in pids:
        _, code = os.waitpid(p, 0)
        res_codes.append(code)

    assert all(x == exp_res for x in res_list), (
        f'threads results are not all valid: {res_list}'
    )

    assert all(x == 0 for x in res_codes), (
        f"child processes don't all exit successfully: {res_codes}"
    )


@contextmanager
def z_iopy_use_fake_tcp_server(uri: str) -> Generator[socket.socket]:
    addr, port_str = uri.split(':')
    port = int(port_str)

    fake_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    fake_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    fake_server.bind((addr, port))
    fake_server.listen(1)

    try:
        yield fake_server
    finally:
        fake_server.close()


def z_monkey_patch(iop_cls: type) -> Callable[[type], type]:
    def wrapper(py_cls: type) -> type:
        # Retrieve the methods and attributes of the python class to be
        # monkey-patched in the IOP class.
        # Skip __dict__, __weakref__ and __doc__ if set.
        py_cls_dict = dict(py_cls.__dict__)
        py_cls_dict.pop('__dict__', None)
        py_cls_dict.pop('__weakref__', None)
        if py_cls_dict.get('__doc__') is None:
            py_cls_dict.pop('__doc__', None)

        for k, v in py_cls_dict.items():
            setattr(iop_cls, k, v)

        # Add the custom base/parent classes of the python class to the IOP
        # class.
        # Only keep last iop_cls base to support multiple monkey patches.
        new_bases: tuple[type, ...] = (iop_cls.__bases__[-1],)
        if py_cls.__bases__ != (object,):
            new_bases = py_cls.__bases__ + new_bases
        iop_cls.__bases__ = new_bases

        return iop_cls

    return wrapper


# }}}
# {{{ IopyTest


@z.ZGroup
class IopyTest(z.TestCase):
    async_done: bool
    connections: int

    @override
    def setUp(self) -> None:
        self.plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = cast(
            'test_iop_plugin__iop.Plugin', iopy.Plugin(self.plugin_file)
        )
        self.r = self.p.register()

    if not hasattr(z.TestCase, 'assertIsSubclass'):

        def assertIsSubclass(  # noqa: N802 (invalid-function-name)
            self: z.TestCase,
            cls: type[object],
            class_or_tuple: type[object],
            msg: str | None = None,
        ) -> None:
            if not issubclass(cls, class_or_tuple):
                message = f'{cls!r} is not a subclass of {class_or_tuple!r}'
                if msg is not None:
                    message += f' : {msg}'
                raise self.failureException(message)

    def test_type_name_parsing(self) -> None:
        a = self.r.tst1.A(_json='{ "a": "A1", "b": "B2" }')
        self.assertEqual(a, self.r.tst1.A(a='A1', b='B2'))

    def test_pkg_mod_deps(self) -> None:
        """
        Test that IOPs objects of packages that are referenced through
        modules are well loaded
        """
        void_opt = self.r.testvoid.VoidOptional(a=None)
        self.assertIsNone(void_opt.a)

    def test_inheritance(self) -> None:
        self.assertTrue(
            issubclass(self.r.test.ClassB, self.r.test.ClassA),
            'class inheritance failed',
        )

    def test_fields(self) -> None:
        a = self.r.test.ClassA()
        a.a = 'a'  # pyrefly: ignore[missing-attribute]
        self.assertEqual(a.field1, 0)
        self.assertEqual(a.a, 'a', 'append field failed')  # pyrefly: ignore[missing-attribute]

    def test_ignore_unkwnon(self) -> None:
        a = self.r.tst1.A(
            _json='{ "a": "A1", "b": "B2", "c": "D4" }', _ignore_unknown=True
        )
        self.assertEqual(a, self.r.tst1.A(a='A1', b='B2'))

    def test_from_file_json(self) -> None:
        path = os.path.join(TEST_PATH, 'test_class_b.json')
        b = self.r.test.ClassB.from_file(_json=path)
        self.assertEqual(b.field1, 42)
        self.assertEqual(b.field2, 10)
        self.assertEqual(b.optField, 20)
        b2 = self.r.test.ClassB.from_file(_json=path)
        self.assertEqual(b, b2)
        extra_field_path = os.path.join(TEST_PATH, 'test_class_b_extra.json')

        with self.assertRaises(iopy.Error):
            self.r.test.ClassB.from_file(_json=extra_field_path)

        b_extra = self.r.test.ClassB.from_file(
            _json=extra_field_path,
            _ignore_unknown=True,
            _forbid_private=False,
            _use_c_case=True,
        )
        self.assertEqual(b_extra.field1, 42)
        self.assertEqual(b_extra.field2, 10)
        self.assertEqual(b_extra.optField, 20)
        self.assertFalse(hasattr(b_extra, 'extraField'))

    def test_from_file_yaml(self) -> None:
        path = os.path.join(TEST_PATH, 'test_class_b.yaml')
        b = self.r.test.ClassB.from_file(_yaml=path)
        self.assertEqual(b.field1, 9)
        self.assertEqual(b.field2, 8)
        self.assertEqual(b.optField, 7)
        b2 = self.r.test.ClassB.from_file(_yaml=path)
        self.assertEqual(b, b2)
        extra_field_path = os.path.join(TEST_PATH, 'test_class_b_extra.yaml')

        with self.assertRaises(iopy.Error):
            self.r.test.ClassB.from_file(_yaml=extra_field_path)

        b_extra = self.r.test.ClassB.from_file(
            _yaml=extra_field_path,
            _ignore_unknown=True,
            _forbid_private=False,
        )
        self.assertEqual(b_extra.field1, 9)
        self.assertEqual(b_extra.field2, 8)
        self.assertEqual(b_extra.optField, 7)

    def test_from_str_yaml(self) -> None:
        path = os.path.join(TEST_PATH, 'test_class_b.yaml')
        b = self.r.test.ClassB.from_file(_yaml=path)
        with open(path, 'r') as f:
            b2 = self.r.test.ClassB(_yaml=f.read())
            self.assertEqual(b, b2)

    def test_from_file_xml(self) -> None:
        path = os.path.join(TEST_PATH, 'test_class_b.xml')
        b = self.r.test.ClassB.from_file(_xml=path)
        self.assertEqual(b.field1, 45)
        self.assertEqual(b.field2, 20)
        self.assertEqual(b.optField, 36)
        b2 = self.r.test.ClassB.from_file(_xml=path)
        self.assertEqual(b, b2)

        extra_field_path = os.path.join(TEST_PATH, 'test_class_b_extra.xml')

        with self.assertRaises(iopy.Error):
            self.r.test.ClassB.from_file(_xml=extra_field_path)

        b_extra = self.r.test.ClassB.from_file(
            _xml=extra_field_path, _ignore_unknown=True, _forbid_private=False
        )
        self.assertEqual(b_extra.field1, 9)
        self.assertEqual(b_extra.field2, 8)
        self.assertEqual(b_extra.optField, 7)

    def test_from_file_hex(self) -> None:
        path = os.path.join(TEST_PATH, 'test_class_b.hex')
        b = self.r.test.ClassB.from_file(_hex=path)
        self.assertEqual(b.field1, 1)
        self.assertEqual(b.field2, 2)
        self.assertEqual(b.optField, 3)
        b2 = self.r.test.ClassB.from_file(_hex=path)
        self.assertEqual(b, b2)

    def test_from_file_bin(self) -> None:
        path = os.path.join(TEST_PATH, 'test_class_b.bin')
        b = self.r.test.ClassB.from_file(_bin=path)
        self.assertEqual(b.field1, 4)
        self.assertEqual(b.field2, 5)
        self.assertEqual(b.optField, 6)
        b2 = self.r.test.ClassB.from_file(_bin=path)
        self.assertEqual(b, b2)

    def test_to_json(self) -> None:
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = '{"a":"plop","b":"plip","tab":["plup"]}'
        self.assertEqual(exp, b.to_json(minimal=True))

    def test_to_yaml(self) -> None:
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = 'a: plop\nb: plip\ntab:\n  - plup'
        self.assertEqual(exp, b.to_yaml())

    def test_to_bin(self) -> None:
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = b'\x01\x05plop\x00\x02\x05plip\x00\x03\x05plup\x00'
        self.assertEqual(exp, b.to_bin())

    def test_to_hex(self) -> None:
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = '0105706c6f70000205706c6970000305706c757000'
        self.assertEqual(exp, b.to_hex())

    def test_to_xml(self) -> None:
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = (
            '<test.StructB><a>plop</a><b>plip</b>'
            '<tab>plup</tab></test.StructB>'
        )
        self.assertEqual(exp, b.to_xml())

    def test_to_dict(self) -> None:
        options_list = [
            'skip_private',
            'skip_default',
            'skip_empty_arrays',
            'skip_empty_structs',
            'skip_class_names',
            'skip_optional_class_names',
            'enums_as_int',
            'minimal',
        ]

        def check_json_compat_options(obj: iopy.StructUnionBase) -> None:
            self.assertEqual(json.loads(obj.to_json()), obj.to_dict())
            for option in options_list:
                kwargs = {option: True}
                self.assertEqual(
                    json.loads(obj.to_json(**kwargs)), obj.to_dict(**kwargs)
                )

        # Create struct to check
        class_b_dict: test__iop.ClassB_DictType = {
            '_class': 'test.ClassB',
            'field1': 0,
            'field2': 87,
        }
        # Annotated to help pyrefly infer the element type: when the list has
        # multiple TypedDict literals with disjoint keys, pyrefly joins them
        # into `dict[str, ...]` instead of the expected `UnionA_DictType`.
        tu_list: list[test__iop.UnionA_DictType] = [
            {
                'i': 77,
            },
            {
                's': 'pouet',
            },
            {
                'a': {
                    '_class': 'test.ClassA',
                    'field1': 7,
                    'optField': 642,
                },
            },
        ]
        dict_struct_to_dict: test__iop.StructToDict_DictType = {
            'structA': {
                'e': 'B',
                'a': class_b_dict,
                'u': {
                    's': 'aaaa',
                },
                'tu': tu_list,
            },
            'privateField': 12,
            'emptyArray': [],
            'voidUnion': {
                'a': None,
            },
            'voidOptional': {
                'a': None,
            },
            'voidRequired': {},
        }

        struct_to_dict = self.p.test.StructToDict(dict_struct_to_dict)

        # Check plain struct with everything printed
        self.assertEqual(dict_struct_to_dict, struct_to_dict.to_dict())

        # Check we can recreate the same object from the dict
        self.assertEqual(
            struct_to_dict, self.p.test.StructToDict(struct_to_dict.to_dict())
        )

        # Check minimal option
        minimal_dict_struct_to_dict: dict[str, Any] = {
            'structA': {
                'e': 'B',
                'a': {
                    '_class': 'test.ClassB',
                    'field2': 87,
                },
                'u': {
                    's': 'aaaa',
                },
                'tu': [
                    {
                        'i': 77,
                    },
                    {
                        's': 'pouet',
                    },
                    {
                        'a': {
                            'field1': 7,
                            'optField': 642,
                        },
                    },
                ],
            },
            'privateField': 12,
            'voidUnion': {
                'a': None,
            },
            'voidOptional': {
                'a': None,
            },
        }

        self.assertEqual(
            minimal_dict_struct_to_dict, struct_to_dict.to_dict(minimal=True)
        )

        # Check JSON compatibility and all options
        check_json_compat_options(struct_to_dict)

        # Check with a class directly
        dict_class_b: test__iop.ClassB_DictType = {
            '_class': 'test.ClassB',
            'field1': 1,
            'field2': 0,
        }

        class_b = self.p.test.ClassA(dict_class_b)
        self.assertEqual(dict_class_b, class_b.to_dict())

        # Check minimal option
        minimal_dict_class_b = {
            'field1': 1,
        }
        self.assertEqual(minimal_dict_class_b, class_b.to_dict(minimal=True))

        # Check JSON compatibility and all options
        check_json_compat_options(class_b)

        # Check with a union
        dict_union_a: test__iop.UnionA_DictType = {
            'a': {
                '_class': 'test.ClassA',
                'field1': 0,
                'optField': 987,
            },
        }

        union_a = self.p.test.UnionA(dict_union_a)
        self.assertEqual(dict_union_a, union_a.to_dict())

        # Check minimal option
        minimal_dict_union_a = {
            'a': {
                'optField': 987,
            },
        }
        self.assertEqual(minimal_dict_union_a, union_a.to_dict(minimal=True))

        # Check JSON compatibility and all options
        check_json_compat_options(union_a)

    def test_custom_methods(self) -> None:
        @z_monkey_patch(self.r.test.ClassA)
        class test_ClassA:  # noqa: N801 (invalid-class-name)
            def fun(self) -> int:
                res: int = self.field1  # pyrefly: ignore[missing-attribute]
                return res

        b = self.r.test.ClassB(field1=1)
        self.assertTrue(hasattr(b, 'fun'), 'method inheritance failed')
        self.assertEqual(
            b.fun(),
            1,
            'method inheritance failed',
        )

    def test_subtyping(self) -> None:
        u = self.r.test.UnionA(a=self.r.test.ClassB())
        self.assertIsInstance(u.a, self.r.test.ClassB, 'subtyping failed')
        self.assertTrue(hasattr(u.a, 'field2'), 'subtyping failed')

    def test_rpc_client_server(self) -> None:
        def rpc_impl_a(
            rpc_args: test__iop.InterfaceA_funA_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
            login = None
            password = None
            if rpc_args.hdr is not None and hasattr(rpc_args.hdr, 'simple'):
                login = rpc_args.hdr.simple.login
                password = rpc_args.hdr.simple.password
            if login != 'root' or password != '1234':
                desc = f'invalid login, hdr: {rpc_args.hdr!r}'
                return rpc_args.exn(code=1, desc=desc)
            status = self.r.test.EnumA(rpc_args.arg.a.get_iop_fullname()[-1])
            return rpc_args.res(status=status, res=rpc_args.arg.a.field1)

        s = self.r.channel_server()

        s.test_ModuleA.interfaceA.funA.impl = rpc_impl_a
        self.assertEqual(s.test_ModuleA.interfaceA.funA.impl, rpc_impl_a)
        self.assertIsNone(s.test_ModuleA.interfaceA.funAsync.impl)

        self.async_done = False

        def rpc_impl_async(
            rpc_args: test__iop.InterfaceA_funAsync_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funAsync_RPCServer.RpcRes:
            self.async_done = True
            return rpc_args.res()

        s.test_ModuleA.interfaceA.funAsync.impl = rpc_impl_async

        self.connections = 0

        def server_on_connect(
            server: iopy.ChannelServer, remote_addr: str
        ) -> None:
            self.connections += 1

        def server_on_disconnect(
            server: iopy.ChannelServer, remote_addr: str
        ) -> None:
            self.connections -= 1

        s.on_connect = server_on_connect
        s.on_disconnect = server_on_disconnect

        uri = make_uri()
        s.listen(uri=uri)
        c = self.r.connect(uri)

        c.test_ModuleA.interfaceA.funAsync(a=self.r.test.ClassA())

        b = self.r.test.ClassB(field1=1)
        res = c.test_ModuleA.interfaceA.funA(
            a=b, _login='root', _password='1234'
        )
        self.assertEqual(res.status.get_as_str(), 'B')
        self.assertEqual(res.res, 1)

        def test_ok(_hdr: ic__iop.Hdr | None = None) -> None:
            if _hdr:
                res = c.test_ModuleA.interfaceA.funA(a=b, _hdr=_hdr)
            else:
                res = c.test_ModuleA.interfaceA.funA(a=b)
            self.assertEqual(res.status.get_as_str(), 'B')
            self.assertEqual(res.res, 1)

        shdr = self.r.ic.SimpleHdr(login='root', password='1234')
        hdr = self.r.ic.Hdr(simple=shdr)
        test_ok(_hdr=hdr)

        def test_ko() -> None:
            try:
                c.test_ModuleA.interfaceA.funA(a=b)
            except iopy.RpcError as e:
                self.assertEqual(len(e.args), 1)
                exn: test__iop.InterfaceA_funA_Exn = e.args[0]
                self.assertIsInstance(
                    exn, c.test_ModuleA.interfaceA.funA.exn()
                )
                self.assertEqual(exn.code, 1)
                self.assertTrue('invalid login, hdr:' in exn.desc)

        test_ko()

        c.change_default_hdr(_login='root', _password='1234')
        test_ok()

        c.change_default_hdr(_hdr=hdr)
        test_ok()

        hdr = c.get_default_hdr()
        self.assertTrue(hasattr(hdr, 'simple'))
        self.assertEqual(hdr.simple.login, shdr.login)
        self.assertEqual(hdr.simple.password, shdr.password)

        hdr.simple.password = 'toto'
        c.change_default_hdr(_hdr=hdr)
        test_ko()

        self.assertTrue(self.async_done, 'async RPC failed')

        self.assertEqual(self.connections, 1, 'on_connect cb failed')

        self.assertEqual(c.is_connected(), True)
        c.disconnect()
        self.assertEqual(c.is_connected(), False)

        for _ in range(100):
            if not self.connections:
                break
            time.sleep(0.01)
        self.assertEqual(self.connections, 0, 'on_disconnect cb failed')

        s.stop()
        s.on_connect = None
        s.on_disconnect = None

        p_args = [
            'python3',
            os.path.join(SELF_PATH, 'z_iopy_process1.py'),
            self.plugin_file,
            uri,
        ]
        with subprocess.Popen(p_args) as proc:
            self.assertIsNotNone(proc)
            s.test_ModuleA.interfaceA.funA.wait(uri=uri, timeout=20)
            proc.wait()
            msg = (
                'server blocking failed; '
                f'subprocess status: {proc.returncode}'
            )
            self.assertEqual(proc.returncode, 0, msg)

    def test_objects_comparisons(self) -> None:
        u1 = self.r.test.UnionA(a=self.r.test.ClassB(field1=1, field2=2))
        u2 = self.r.test.UnionA(a=self.r.test.ClassB(field1=1, field2=2))
        u3 = self.r.test.UnionA(a=self.r.test.ClassB(field1=1, field2=3))
        u4 = self.r.test.UnionA(a=self.r.test.ClassA(field1=1))
        self.assertTrue(u1 == u1)  # noqa: PLR0124 (comparison-with-itself)
        self.assertTrue(u1 == u2)
        self.assertTrue(u1 != u3)
        self.assertTrue(u1 != u4)
        self.assertTrue(u1 != u1.a)

        e1 = self.r.test.EnumA(0)
        e2 = self.r.test.EnumA(0)
        e3 = self.r.test.EnumA(1)
        self.assertTrue(e1 == e1)  # noqa: PLR0124 (comparison-with-itself)
        self.assertTrue(e1 <= e1)  # noqa: PLR0124 (comparison-with-itself)
        self.assertTrue(e1 >= e1)  # noqa: PLR0124 (comparison-with-itself)
        self.assertTrue(e1 == e2)
        self.assertTrue(e1 <= e2)
        self.assertTrue(e1 >= e2)
        self.assertTrue(e1 != e3)
        self.assertTrue(e1 <= e3)
        self.assertTrue(e1 < e3)
        self.assertTrue(e3 >= e1)
        self.assertTrue(e3 > e1)

        tab = self.r.test_emptystuffs.Tab(
            a=[self.r.test_emptystuffs.A(), self.r.test_emptystuffs.B()],
            emptyStructs=[self.r.test_emptystuffs.EmptyStruct()],
        )
        self.assertEqual(
            tab.a[1],
            self.r.test_emptystuffs.B(),
            'empty stuff comparison failed',
        )
        self.assertNotEqual(
            tab.a[0],
            self.r.test_emptystuffs.B(),
            'empty stuff comparison failed',
        )

    def test_packing(self) -> None:
        u = self.r.test.UnionA(self.r.test.ClassB(field1=1, field2=2))
        # check union packing after unamed field init
        j = u.to_json()
        self.assertEqual(u, self.r.test.UnionA(_json=j))
        # check union field init from a cast
        a = self.r.test.StructA(u=self.r.test.ClassB(field2=1))
        j = a.to_json()
        self.assertEqual(a, self.r.test.StructA(_json=j))
        # check struct field init from union and packing
        # XXX: This is a weird case not worth to be supported with typing.
        a = self.r.test.StructA(a=u)  # pyrefly: ignore[no-matching-overload]
        j = a.to_json()
        self.assertEqual(a, self.r.test.StructA(_json=j))
        # check enum field init from cast and packing
        a = self.r.test.StructA(e=0)
        j = a.to_json()
        self.assertEqual(a, self.r.test.StructA(_json=j))
        a = self.r.test.StructA(e='A')
        j = a.to_json()
        self.assertEqual(a, self.r.test.StructA(_json=j))

        # check compact option is working
        # XXX: 'compact' is obsolete
        j = a.to_json(compact=True)  # pyrefly: ignore[unexpected-keyword]
        self.assertEqual(a, self.r.test.StructA(_json=j))

        # check that private fields are skipped
        c = self.r.test.StructC(u=1, priv='toto')
        d = self.r.test.StructC(u=1)
        j = c.to_json()
        self.assertEqual(c, self.r.test.StructC(_json=j))

        j = c.to_json(skip_private=True)
        self.assertNotEqual(c, self.r.test.StructC(_json=j))
        self.assertEqual(d, self.r.test.StructC(_json=j))

    def test_unicode(self) -> None:
        # unicode string and non unicode strings
        b = self.r.test.StructB(
            a=b'string a',
            b='string b',
            tab=[b'first string', 'second string'],
        )
        j = b.to_json()
        self.assertEqual(
            b,
            self.r.test.StructB(_json=j),
            'unicode strings in iopy fields failed',
        )
        b2 = self.r.test.StructB(
            a='string a', b='string b', tab=['first string', 'second string']
        )
        self.assertTrue(b == b2, 'string fields comparison failed')
        b3 = self.r.test.StructB(a='non asçii éé', b='', tab=[])
        self.assertTrue(
            b3 == self.r.test.StructB(_json=b3.to_json()),
            'real unicode fields failed',
        )
        u = self.r.test.UnionA(s=b'bytes string')
        j = u.to_json()
        self.assertEqual(
            u, self.r.test.UnionA(_json=j), 'string in iopy union failed'
        )
        u = self.r.test.UnionA(s='unicode string')
        j = u.to_json()
        self.assertEqual(
            u,
            self.r.test.UnionA(_json=j),
            'unicode string in iopy union failed',
        )
        u = self.r.test.UnionA(s=b'bytes string')
        j = u.to_json()
        self.assertEqual(
            u,
            self.r.test.UnionA(_json=j),
            'bytes string in iopy union failed',
        )

    def test_constraints(self) -> None:
        self.r.test.UnionA(i=100)
        u_a = self.r.test.UnionA(100)
        u_a.i = 1
        exp = r'violation of constraint max \(100\) on field i: val=101$'
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.UnionA(i=101)
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.UnionA(101)
        with self.assertRaisesRegex(iopy.Error, exp):
            u_a.i = 101
        self.assertEqual(u_a.i, 1)

        u_b = self.r.test.UnionB(a=100)
        self.assertTrue(hasattr(u_b, 'a'))
        self.assertEqual(getattr(u_b.a, 'i', None), 100)
        u_b.a = 1  # pyrefly: ignore[bad-assignment]
        exp = (
            r'^error when parsing test\.UnionB: '
            r'invalid selected union field .+a.+: in a of type '
            r'test\.UnionA: violation of constraint max \(100\) on '
            r'field i: val=101$'
        )
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.UnionB(a=101)
        with self.assertRaisesRegex(iopy.Error, exp):
            u_b.a = 101  # pyrefly: ignore[bad-assignment]
        self.assertEqual(getattr(u_b.a, 'i', None), 1)

        c_b = self.r.test.ClassB(field1=1000)
        c_b.field1 = 1
        exp = (
            r'violation of constraint max \(1000\) on field field1: '
            r'val=1001$'
        )
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.ClassB(field1=1001)
        with self.assertRaisesRegex(iopy.Error, exp):
            c_b.field1 = 1001
        self.assertEqual(c_b.field1, 1)

        self.r.test.StructF(s='', i=[0])
        exp = (
            r'^error when parsing test.StructF: '
            r'field s \(type: ?str\) is required but absent; '
            r'field i \(type: ?int\[\]\) is not allowed: empty array$'
        )
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.StructF()  # pyrefly: ignore[missing-argument]

        s_a = self.r.test.StructA()
        self.assertEqual(s_a, self.r.test.StructA(tu=[]))

        s_a = self.r.test.StructA(tu=[1, self.r.test.ClassB(), ''])
        exp = (
            r'^error when parsing test.StructA: '
            r'invalid argument .+tu.+: in tu\[1\] of type test.UnionA: '
            r'violation of constraint max \(100\) on field i: val=101$'
        )
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.StructA(tu=[100, 101])

        _ = self.r.test.ConstraintsB(name='ab', i=1000)
        exp = (
            r'^error when parsing test.ConstraintsB: '
            r'invalid argument .+name.+: in type test.ConstraintsA: '
            r'violation of constraint pattern \(\[a-z\]\*\) on field '
            r'name: a b; invalid argument .+i.+: in type '
            r'test.ConstraintsB: violation of constraint max \(1000\) '
            r'on field i: val=1001$'
        )
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.ConstraintsB(name='a b', i=1001)

    def test_unsigned_long_reading(self) -> None:
        c = self.r.test.StructC()
        self.assertEqual(c.u, (1 << 64) - 1)

    def test_field_deletion(self) -> None:
        a = self.r.test.ClassA(optField=1)
        del a.optField
        self.assertFalse(
            hasattr(a, 'optField'), 'deletion of optional field has failed'
        )

        err = False
        try:
            del a.field1
        except iopy.Error:
            err = True
        self.assertTrue(
            hasattr(a, 'field1') and err,
            'check of deletion of mandatory field has failed',
        )

        u = self.r.test.UnionA(i=0)
        err = False
        try:
            del u.i
        except iopy.Error:
            err = True
        self.assertTrue(
            hasattr(u, 'i') and err,
            'check of deletion of union field has failed',
        )

    def test_required_fields(self) -> None:
        exp = (
            r'^error when parsing test.StructB: field a \(type: ?str\) is '
            r'required but absent$'
        )
        with self.assertRaisesRegex(iopy.Error, exp):
            self.r.test.StructB(b='')  # pyrefly: ignore[no-matching-overload]

    def test_unspecified_optional_fields(self) -> None:
        d = self.r.test.StructD()
        self.assertEqual(d, self.r.test.StructD(a=self.r.test.StructA()))
        e = self.r.test.StructE()
        self.assertEqual(e, self.r.test.StructE(d=d))

    def test_custom_init(self) -> None:
        @z_monkey_patch(self.r.test.ClassA)
        class test_ClassA1:  # noqa: N801 (invalid-class-name)
            def __init__(
                self,
                field1: int = 10,
                _my_field: str = 'value',
                **kwargs: Any,
            ) -> None:
                self._my_field = _my_field
                kwargs['field1'] = field1
                super(test_ClassA1, self).__init__(**kwargs)

        a = self.r.test.ClassA(optField=0)
        self.assertEqual(
            getattr(a, '_my_field', None),
            'value',
            'custom init method has not been called',
        )
        self.assertEqual(
            getattr(a, 'field1', None),
            10,
            'custom init of iop field has failed',
        )
        self.assertEqual(
            getattr(a, 'optField', None),
            0,
            'init of optional iop field has failed',
        )

        b = self.r.test.ClassB()
        self.assertEqual(
            getattr(b, 'field1', None),
            10,
            'custom init of inherited iop field has failed',
        )

        a = self.r.test.ClassA(field1=11)
        self.assertEqual(
            getattr(a, 'field1', None),
            11,
            'custom init of iop field has failed',
        )

        a = self.r.test.ClassA(_bin=a.to_bin())
        self.assertEqual(
            getattr(a, '_my_field', None),
            'value',
            'custom init method has not been called from iop creation',
        )

        a = self.r.test.ClassA(  # pyrefly: ignore[no-matching-overload]
            field1=42, _my_field='test'
        )
        self.assertEqual(
            getattr(a, 'field1', None),
            42,
            'custom init of iop field has failed',
        )
        self.assertEqual(
            getattr(a, '_my_field', None),
            'test',
            'custom init of custom value has failed',
        )

        @z_monkey_patch(self.r.test.ClassA)
        class test_ClassA2:  # noqa: N801 (invalid-class-name)
            def __init__(
                self,
                field1: int = 10,
                _my_field: str = 'value',
                **kwargs: Any,
            ) -> None:
                var = field1 * 10  # check #33039
                self.optField = var
                self._my_field = _my_field
                kwargs['field1'] = field1
                super(test_ClassA2, self).__init__(**kwargs)

        a = self.r.test.ClassA()
        self.assertEqual(
            getattr(a, '_my_field', None),
            'value',
            'custom init with internal variables has failed',
        )
        self.assertEqual(
            getattr(a, 'field1', None),
            10,
            'custom init with internal variables has failed',
        )
        self.assertEqual(
            getattr(a, 'optField', None),
            100,
            'custom init with internal variables has failed',
        )

        @z_monkey_patch(self.r.test.ClassB)
        class test_ClassB:  # noqa: N801 (invalid-class-name)
            def __init__(self, field1: int = 20, **kwargs: Any) -> None:
                kwargs['field1'] = field1
                super(test_ClassB, self).__init__(**kwargs)
                self.field2 = 42

        b = self.r.test.ClassB()
        self.assertEqual(
            getattr(b, 'field1', None),
            20,
            'custom init inheritance has failed',
        )
        self.assertEqual(
            getattr(b, 'field2', None),
            42,
            'custom init inheritance has failed',
        )

        @z_monkey_patch(self.r.test.StructA)
        class test_StructA:  # noqa: N801 (invalid-class-name)
            r = self.r

            def __init__(self, **kwargs: Any) -> None:
                self.class_a = self.r.test.ClassA(**kwargs)
                super(test_StructA, self).__init__()

        sta: test_StructA = self.r.test.StructA(field1=5)  # pyrefly: ignore[bad-assignment, no-matching-overload]
        self.assertIsNotNone(
            getattr(sta, 'class_a', None), 'custom init with kwargs failed'
        )
        self.assertEqual(
            getattr(sta.class_a, 'field1', None),
            5,
            'custom init with kwargs failed',
        )

    def test_custom_inheritance(self) -> None:
        class CommonClass1:
            common_val1: int

            def foo(self) -> None:
                self.common_val1 = 42

        @z_monkey_patch(self.r.test.StructA)
        class test_StructA1(CommonClass1):  # noqa: N801 (invalid-class-name)
            common_val2: int

            @override
            def foo(self) -> None:
                super(test_StructA1, self).foo()
                self.common_val2 = 12

        st = self.r.test.StructA()
        st.foo()  # pyrefly: ignore[missing-attribute]
        self.assertEqual(st.common_val1, 42)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(st.common_val2, 12)  # pyrefly: ignore[missing-attribute]

        class BaseCommonClass1:
            common_val2: int

            def bar(self) -> None:
                self.common_val2 = 7777

        class CommonClass2(BaseCommonClass1):
            common_val1: int

            def foo(self) -> None:
                self.bar()
                self.common_val1 = 84

        @z_monkey_patch(self.r.test.StructA)
        class test_StructA2(CommonClass2, CommonClass1):  # noqa: N801 (invalid-class-name)
            pass

        # Delete previous foo() method due to test_StructA1 monkey-patch
        del test_StructA1.foo

        st = self.r.test.StructA()
        st.foo()  # pyrefly: ignore[missing-attribute]
        self.assertEqual(st.common_val1, 84)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(st.common_val2, 7777)  # pyrefly: ignore[missing-attribute]

        class CommonClass3:
            def __init__(self, *args: Any, **kwargs: Any) -> None:
                super().__init__(*args, **kwargs)
                self.common_val1 = 10

        @z_monkey_patch(self.r.test.StructA)
        class test_StructA3(CommonClass3):  # noqa: N801 (invalid-class-name)
            pass

        st = self.r.test.StructA()
        self.assertEqual(st.common_val1, 10)  # pyrefly: ignore[missing-attribute]

        @z_monkey_patch(self.r.test.StructA)
        class test_StructA4:  # noqa: N801 (invalid-class-name)
            pass

        st = self.r.test.StructA()
        self.assertFalse(hasattr(self.r.test.StructA, 'foo'))
        self.assertFalse(hasattr(st, 'common_val1'))
        self.assertFalse(hasattr(st, 'common_val2'))

    def test_json_serialize(self) -> None:
        @z_monkey_patch(self.r.test.StructA)
        class test_StructA:  # noqa: N801 (invalid-class-name)
            def __init__(self, arg1: int = 0, **kwargs: Any) -> None:
                self.var1 = arg1
                super(test_StructA, self).__init__(**kwargs)

        structa = self.r.test.StructA()
        # XXX: This is supported in IOPy, but very hard to represent with
        # types
        structa.tu.append(self.r.test.ClassA())  # pyrefly: ignore[bad-argument-type]
        structa.u = self.r.test.UnionA(s='toto')
        copy_struct_from_plugin = self.p.test.StructA(_json=str(structa))
        copy_struct_from_register = self.r.test.StructA(_json=str(structa))

        self.assertEqual(str(structa), str(copy_struct_from_plugin))
        self.assertEqual(
            structa.var1,  # pyrefly: ignore[missing-attribute]
            copy_struct_from_register.var1,  # pyrefly: ignore[missing-attribute]
        )
        self.assertEqual(str(structa), str(copy_struct_from_register))

        b1 = self.r.test.ClassB(field2=3)
        b2 = self.r.test.ClassA(_json=str(b1))
        self.assertIsInstance(b2, self.r.test.ClassB)
        assert isinstance(b2, self.r.test.ClassB)
        self.assertEqual(b2.field2, 3)

    def run_test_copy(self, is_deepcopy: bool) -> None:
        copy_method = copy.deepcopy if is_deepcopy else copy.copy

        @z_monkey_patch(self.r.test.StructA)
        class test_StructA:  # noqa: N801 (invalid-class-name)
            def __init__(self, val: int = 0, **kwargs: Any) -> None:
                super(test_StructA, self).__init__()
                self.val = val
                self.__dict__.update(kwargs)

        err = self.r.test.Error(code=42, desc='test')
        structa = self.r.test.StructA(  # pyrefly: ignore[no-matching-overload]
            val=5, foo=err
        )
        structa.tu.append(self.r.test.UnionA(self.r.test.ClassA()))
        structa.u = self.r.test.UnionA('toto')
        structa.r = self.r.test.Error(code=42, desc='test')  # pyrefly: ignore[missing-attribute]
        enuma = self.r.test.EnumA('B')
        enuma.baz = self.r.test.ClassB()  # pyrefly: ignore[missing-attribute]
        structa.e = enuma
        structa.bar = 24  # pyrefly: ignore[missing-attribute]

        copy_structa = copy_method(structa)
        self.assertEqual(structa, copy_structa)
        self.assertEqual(structa.val, copy_structa.val)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(structa.foo, copy_structa.foo)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(structa.bar, copy_structa.bar)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(structa.e.baz, copy_structa.e.baz)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(str(structa), str(copy_structa))
        self.assertNotEqual(id(structa), id(copy_structa))
        self.assertNotEqual(
            is_deepcopy,
            id(structa.foo) == id(copy_structa.foo),  # pyrefly: ignore[missing-attribute]
        )
        self.assertNotEqual(
            is_deepcopy, id(structa.tu[0]) == id(copy_structa.tu[0])
        )
        self.assertNotEqual(is_deepcopy, id(structa.u) == id(copy_structa.u))
        self.assertNotEqual(is_deepcopy, id(structa.e) == id(copy_structa.e))
        self.assertNotEqual(is_deepcopy, id(structa.r) == id(copy_structa.r))  # pyrefly: ignore[missing-attribute]
        self.assertNotEqual(
            is_deepcopy,
            id(structa.e.baz) == id(copy_structa.e.baz),  # pyrefly: ignore[missing-attribute]
        )

        classb = self.r.test.ClassB(field1=42, field2=20)
        classb.plop = err  # pyrefly: ignore[missing-attribute]

        copy_classb = copy_method(classb)
        self.assertEqual(classb, copy_classb)
        self.assertEqual(classb.field1, copy_classb.field1)
        self.assertEqual(classb.field2, copy_classb.field2)
        self.assertEqual(classb.plop, copy_classb.plop)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(str(classb), str(copy_classb))
        self.assertNotEqual(id(classb), id(copy_classb))
        self.assertNotEqual(
            is_deepcopy,
            id(classb.plop) == id(copy_classb.plop),  # pyrefly: ignore[missing-attribute]
        )

        enuma = self.r.test.EnumA('A')
        enuma.plop = err  # pyrefly: ignore[missing-attribute]

        copy_enuma = copy_method(enuma)
        self.assertEqual(enuma, copy_enuma)
        self.assertEqual(enuma.plop, copy_enuma.plop)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(str(enuma), str(copy_enuma))
        self.assertNotEqual(id(enuma), id(copy_enuma))
        self.assertNotEqual(
            is_deepcopy,
            id(enuma.plop) == id(copy_enuma.plop),  # pyrefly: ignore[missing-attribute]
        )

        uniona = self.r.test.UnionA('A')
        uniona.plop = err  # pyrefly: ignore[missing-attribute]

        copy_uniona = copy_method(uniona)
        self.assertEqual(uniona, copy_uniona)
        self.assertEqual(uniona.plop, copy_uniona.plop)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(str(uniona), str(copy_uniona))
        self.assertNotEqual(id(uniona), id(copy_uniona))
        self.assertNotEqual(
            is_deepcopy,
            id(uniona.plop) == id(copy_uniona.plop),  # pyrefly: ignore[missing-attribute]
        )

        structg1 = self.r.test.StructG(a=2)
        structg2 = self.r.test.StructG(a=10, parent=structg1)
        structg1.child = structg2

        copy_structg1 = copy_method(structg1)
        copy_structg2 = copy_structg1.child
        self.assertNotEqual(id(structg1), id(copy_structg1))
        self.assertNotEqual(is_deepcopy, id(structg2) == id(copy_structg2))
        self.assertEqual(
            is_deepcopy, id(copy_structg2.parent) == id(copy_structg1)
        )

    def test_copy(self) -> None:
        self.run_test_copy(False)

    def test_deepcopy(self) -> None:
        self.run_test_copy(True)

    def test_unambiguous_union(self) -> None:
        self.assertEqual(self.r.test.UnionA(1), self.r.test.UnionA(i=1))
        self.assertEqual(
            self.r.test.UnionA('foo'), self.r.test.UnionA(s='foo')
        )
        self.assertEqual(self.r.test.UnionA(10.1), self.r.test.UnionA(d=10.1))

    def test_safe_array_init(self) -> None:
        tab = [42]
        self.r.test.StructA(tu=tab)
        self.assertIs(tab[0], 42)

    def test_implicit_array(self) -> None:
        a0 = self.r.test.StructA(tu=[42])
        a1 = self.r.test.StructA(tu=42)
        self.assertEqual(a0, a1)
        a2 = self.r.test.StructA(tu=[])
        # XXX: too complicated for the type system
        a2.tu = 42  # pyrefly: ignore[bad-assignment]
        self.assertEqual(a0, a2)

    def test_cast_and_no_cast(self) -> None:
        tab = [1, self.r.test.UnionA(i=2)]
        # XXX: too complicated for the type system
        self.r.test.StructA(tu=tab)
        tab = [self.r.test.UnionA(i=1), 2]
        self.r.test.StructA(tu=tab)

    def test_static_attrs(self) -> None:
        exp_static_attrs = {'intAttr': 999, 'strAttr': 'truc'}
        class_attrs = self.r.test.StaticAttrsC.get_class_attrs()
        self.assertEqual(class_attrs['statics'], exp_static_attrs)
        # TODO: support static attrs though class in type system
        self.assertEqual(
            self.r.test.StaticAttrsB.intAttr,  # pyrefly: ignore[missing-attribute]
            999,
        )
        self.assertEqual(
            self.r.test.StaticAttrsB.strAttr,  # pyrefly: ignore[missing-attribute]
            'plop',
        )

    def test_unhashable(self) -> None:
        def _check_unhashable(x: iopy.Basic) -> None:
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                hash(x)
            # Direct `__hash__()` call is caught statically by pyrefly
            # because the stub declares `Basic.__hash__: ClassVar[None]`,
            # hence the `not-callable` suppression below ("None not
            # callable").
            with self.assertRaisesRegex(TypeError, 'not callable'):
                x.__hash__()  # noqa: PLC2801  # pyrefly: ignore[not-callable]
            # Set / frozenset / dict membership tries to hash the LHS
            # first, so it fails at runtime (pyrefly does not flag this).
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                _ = x in {0, 1}
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                _ = x in {'A', 'B'}
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                _ = x in frozenset([0, 1])
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                _ = x in {0: 'a', 1: 'b'}
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                _ = {x}
            with self.assertRaisesRegex(TypeError, 'unhashable type'):
                _ = {x: 1}

        # Test classes are unhashable
        _check_unhashable(self.r.test.ClassA())

        # Test structs are unhashable
        _check_unhashable(self.r.test.StructA())

        # Test unions are unhashable
        _check_unhashable(self.r.test.UnionA(i=0))

        # Test enums are unhashable
        _check_unhashable(self.r.test.EnumA('A'))

        # Test we can redefine the hash if needed
        @z_monkey_patch(self.r.test.StructA)
        class test_StructA:  # noqa: N801 (invalid-class-name)
            def __hash__(self) -> int:
                return id(self)

        a = self.r.test.StructA()
        self.assertEqual(hash(a), id(a))

    def test_enums(self) -> None:
        # Create variables
        a_upper = self.r.test.EnumA('A')
        b_upper = self.r.test.EnumA('B')
        ab_upper = self.r.test.EnumA('A|B')

        a_lower = self.r.test.EnumA('a')
        b_lower = self.r.test.EnumA('b')
        ab_lower = self.r.test.EnumA('a | b')

        a1 = self.r.test.EnumA(1)
        b2 = self.r.test.EnumA(2)
        ab3 = self.r.test.EnumA(3)
        undef4 = self.r.test.EnumA(4)

        a_alias_upper = self.r.test.EnumA('A_ALIAS')
        a_alias_lower = self.r.test.EnumA('a_alias')
        b1_alias = self.r.test.EnumA('B1_ALIAS')
        b2_alias = self.r.test.EnumA('b2_alias')

        a_alias_b = self.r.test.EnumA('a_alias |B')
        a_b2_alias = self.r.test.EnumA('A|  b2_alias')
        b1_b2_alias = self.r.test.EnumA('b1_alias  |B2_ALIAS')
        a_alias_a = self.r.test.EnumA('a|A_ALIAS')

        # Basic tests
        self.assertEqual(a_upper.get_as_int(), 1)
        self.assertEqual(a_upper.get_as_str(), 'A')
        self.assertEqual(a_upper, 1)
        self.assertEqual(a_upper, 'A')
        self.assertEqual(int(a_upper), 1)
        self.assertEqual(str(a_upper), 'A')
        self.assertEqual(b_upper.get_as_int(), 2)
        self.assertEqual(b_upper.get_as_str(), 'B')
        self.assertEqual(b_upper, 2)
        self.assertEqual(b_upper, 'B')
        self.assertEqual(int(b_upper), 2)
        self.assertEqual(str(b_upper), 'B')

        # Combining values
        self.assertEqual(ab_upper.get_as_int(), 3)
        self.assertEqual(ab_upper.get_as_str(), 'A|B')
        self.assertEqual(ab_upper, 3)
        self.assertEqual(ab_upper, 'A|B')
        self.assertEqual(int(ab_upper), 3)
        self.assertEqual(str(ab_upper), 'A|B')

        # Matching should be case insensitive
        self.assertEqual(a_lower.get_as_int(), 1)
        self.assertEqual(a_lower.get_as_str(), 'A')
        self.assertEqual(a_lower, 1)
        self.assertEqual(a_lower, 'A')
        self.assertEqual(int(a_lower), 1)
        self.assertEqual(str(a_lower), 'A')
        self.assertEqual(b_lower.get_as_int(), 2)
        self.assertEqual(b_lower.get_as_str(), 'B')
        self.assertEqual(b_lower, 2)
        self.assertEqual(b_lower, 'B')
        self.assertEqual(int(b_lower), 2)
        self.assertEqual(str(b_lower), 'B')
        self.assertEqual(ab_lower.get_as_int(), 3)
        self.assertEqual(ab_lower.get_as_str(), 'A|B')
        self.assertEqual(ab_lower, 3)
        self.assertEqual(ab_lower, 'A|B')
        self.assertEqual(int(ab_lower), 3)
        self.assertEqual(str(ab_lower), 'A|B')

        # Numerical values
        self.assertEqual(a1.get_as_int(), 1)
        self.assertEqual(a1.get_as_str(), 'A')
        self.assertEqual(a1, 1)
        self.assertEqual(a1, 'A')
        self.assertEqual(int(a1), 1)
        self.assertEqual(str(a1), 'A')
        self.assertEqual(b2.get_as_int(), 2)
        self.assertEqual(b2.get_as_str(), 'B')
        self.assertEqual(b2, 2)
        self.assertEqual(b2, 'B')
        self.assertEqual(int(b2), 2)
        self.assertEqual(str(b2), 'B')
        self.assertEqual(ab3.get_as_int(), 3)
        self.assertEqual(ab3.get_as_str(), 'A|B')
        self.assertEqual(ab3, 3)
        self.assertEqual(ab3, 'A|B')
        self.assertEqual(int(ab3), 3)
        self.assertEqual(str(ab3), 'A|B')
        self.assertEqual(undef4.get_as_int(), 4)
        self.assertEqual(undef4.get_as_str(), 'undefined')
        self.assertEqual(undef4, 4)
        self.assertEqual(int(undef4), 4)
        self.assertEqual(str(undef4), 'undefined')

        # Inclusion in non-hashing containers works (list/tuple iterate
        # and use `==`).
        self.assertIn(a1, [0, 1])
        self.assertIn(a1, ['A', 'B'])
        self.assertIn(a1, (0, 1))
        self.assertNotIn(b2, [0, 1])
        self.assertNotIn(b2, ['A'])
        self.assertIn(ab3, [3])
        self.assertIn(ab3, ['A|B'])

        # Inclusion in a set / frozenset does NOT work because the
        # enum is unhashable. See `test_unhashable` for the runtime
        # check.

        # Aliases
        self.assertEqual(a_alias_upper, a_upper)
        self.assertEqual(a_alias_upper, a_lower)
        self.assertEqual(a_alias_lower, a_upper)
        self.assertEqual(a_alias_lower, a_lower)

        self.assertEqual(b1_alias, b_lower)
        self.assertEqual(b2_alias, b_upper)

        self.assertEqual(a_alias_b, ab_lower)
        self.assertEqual(a_b2_alias, ab_lower)
        self.assertEqual(b1_b2_alias, b_upper)
        self.assertEqual(a_alias_a, a_lower)

    def test_union_object_key(self) -> None:
        """Test union get_object() and get_key() methods"""
        a = self.r.test.UnionA(i=58)
        self.assertEqual(a.get_object(), 58)
        self.assertEqual(a.get_key(), 'i')

    def test_abstract_class(self) -> None:
        """Test we cannot instantiate an abstract class"""
        msg = 'cannot instantiate an abstract class: test.ConstraintsA'
        with self.assertRaisesRegex(iopy.Error, msg):
            self.r.test.ConstraintsA(name='plop')

    def test_from_file_child_class(self) -> None:
        """
        Test using from_file from base class to create an instance of a
        child class.
        """
        path = os.path.join(TEST_PATH, 'test_class_b.json')
        b = self.r.test.ClassA.from_file(_json=path)
        assert isinstance(b, self.r.test.ClassB)
        self.assertEqual(b.field1, 42)
        self.assertEqual(b.field2, 10)
        self.assertEqual(b.optField, 20)

    def test_type_double_upgrade(self) -> None:
        """Test type with double level of upgrade"""

        @z_monkey_patch(self.r.test.EnumA)
        class test_EnumA1:  # noqa: N801 (invalid-class-name)
            @staticmethod
            def foo() -> str:
                return 'foo'

        @z_monkey_patch(self.r.test.EnumA)
        class test_EnumA2:  # noqa: N801 (invalid-class-name)
            @staticmethod
            def bar() -> str:
                return 'bar'

        a1 = self.p.test.EnumA('A')
        self.assertEqual(a1.foo(), 'foo')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(a1.bar(), 'bar')  # pyrefly: ignore[missing-attribute]
        a2 = test_EnumA1('A')  # pyrefly: ignore[bad-argument-count]
        self.assertEqual(a2.foo(), 'foo')
        self.assertEqual(a2.bar(), 'bar')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(a1, a2)

        class test_ClassA1:  # noqa: N801 (invalid-class-name)
            @staticmethod
            def foo() -> str:
                return 'foo'

        @z_monkey_patch(self.r.test.ClassA)
        class test_ClassA2(test_ClassA1):  # noqa: N801 (invalid-class-name)
            @staticmethod
            def bar() -> str:
                return 'bar'

        @z_monkey_patch(self.r.test.ClassB)
        class ClassB:
            @staticmethod
            def baz() -> str:
                return 'baz'

        b1 = self.p.test.ClassB()
        self.assertEqual(b1.foo(), 'foo')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(b1.bar(), 'bar')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(b1.baz(), 'baz')  # pyrefly: ignore[missing-attribute]
        b2 = ClassB()
        self.assertEqual(b2.foo(), 'foo')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(b2.bar(), 'bar')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(b2.baz(), 'baz')
        self.assertEqual(b1, b2)

    def test_enum_description(self) -> None:
        desc = self.p.test.EnumDescription.get_iop_description()

        self.assertEqual(desc.help.brief, 'Brief enum documentation.')
        self.assertEqual(desc.help.details, 'Detailed enum documentation.')
        self.assertEqual(desc.help.warning, 'Warning enum documentation.')
        self.assertIsNone(desc.help.example)

        self.assertTrue(desc.strict)

        self.assertEqual(
            desc.generic_attributes,
            {
                'test:gen1': 1,
            },
        )

        a_desc = desc.values['A']

        self.assertEqual(a_desc.help.brief, 'A brief documentation.')
        self.assertEqual(a_desc.help.details, 'A detailed documentation.')
        self.assertEqual(a_desc.help.warning, 'A warning documentation.')
        self.assertIsNone(a_desc.help.example)

        self.assertEqual(
            a_desc.generic_attributes,
            {
                'test:gen2': 2.2,
                'test:gen3': 'jiojj',
            },
        )

        self.assertEqual(a_desc.aliases, ('A_ALIAS',))

        b_desc = desc.values['B']

        self.assertEqual(b_desc.help.brief, 'B brief documentation.')
        self.assertEqual(b_desc.help.details, 'B detailed documentation.')
        self.assertEqual(b_desc.help.warning, 'B warning documentation.')
        self.assertIsNone(b_desc.help.example)

        self.assertEqual(
            b_desc.generic_attributes,
            {
                'test:gen4': 1,
                'test:gen5': '{"field":{"f1":"val1","f2":1}}',
            },
        )

        self.assertEqual(b_desc.aliases, ('B1_ALIAS', 'B2_ALIAS'))

    def test_union_description(self) -> None:
        desc = self.p.test.UnionDescription.get_iop_description()

        self.assertEqual(desc.help.brief, 'Brief union documentation.')
        self.assertEqual(desc.help.details, 'Detailed union documentation.')
        self.assertEqual(desc.help.warning, 'Warning union documentation.')
        self.assertIsNone(desc.help.example)

        self.assertEqual(
            desc.generic_attributes,
            {
                'test:gen1': 1,
            },
        )

        # FIXME: There is a bug in iopc.
        # self.assertTrue(desc.deprecated)

        a_desc = desc.fields['a']

        self.assertEqual(a_desc.help.brief, 'A brief documentation.')
        self.assertEqual(a_desc.help.details, 'A detailed documentation.')
        self.assertEqual(a_desc.help.warning, 'A warning documentation.')
        self.assertIsNone(a_desc.help.example)

        self.assertEqual(
            a_desc.generic_attributes,
            {
                'test:gen2': 'plop',
            },
        )

        self.assertEqual(a_desc.iop_type, 'int')
        self.assertEqual(a_desc.py_type, int)

        self.assertEqual(a_desc.min, -15)
        self.assertEqual(a_desc.max, 75)
        self.assertTrue(a_desc.non_zero)

        self.assertIsNone(a_desc.default_value)
        self.assertFalse(a_desc.optional)
        self.assertFalse(a_desc.repeated)
        self.assertFalse(a_desc.private)
        self.assertFalse(a_desc.deprecated)
        self.assertIsNone(a_desc.min_occurs)
        self.assertIsNone(a_desc.max_occurs)
        self.assertIsNone(a_desc.min_length)
        self.assertIsNone(a_desc.max_length)
        self.assertIsNone(a_desc.length)
        self.assertFalse(a_desc.cdata)
        self.assertFalse(a_desc.non_empty)
        self.assertIsNone(a_desc.pattern)

        b_desc = desc.fields['b']

        self.assertEqual(b_desc.help.brief, 'B brief documentation.')
        self.assertEqual(b_desc.help.details, 'B detailed documentation.')
        self.assertEqual(b_desc.help.warning, 'B warning documentation.')
        self.assertIsNone(b_desc.help.example)

        self.assertEqual(
            b_desc.generic_attributes,
            {
                'test:gen3': 15,
                'test:gen4': '{"plop":4}',
            },
        )

        self.assertEqual(b_desc.iop_type, 'string')
        self.assertEqual(b_desc.py_type, str)

        self.assertEqual(b_desc.min_length, 1)
        self.assertEqual(b_desc.max_length, 15)
        self.assertTrue(b_desc.non_empty)
        self.assertEqual(b_desc.pattern, '[a-zA-Z0-9]*')
        self.assertTrue(b_desc.cdata)

        self.assertIsNone(b_desc.default_value)
        self.assertFalse(b_desc.optional)
        self.assertFalse(b_desc.repeated)
        self.assertFalse(b_desc.private)
        self.assertFalse(b_desc.deprecated)
        self.assertIsNone(b_desc.min)
        self.assertIsNone(b_desc.max)
        self.assertIsNone(b_desc.min_occurs)
        self.assertIsNone(b_desc.max_occurs)
        self.assertIsNone(b_desc.length)
        self.assertFalse(b_desc.non_zero)

        c_desc = desc.fields['c']

        self.assertEqual(c_desc.help.brief, 'C brief documentation.')
        self.assertEqual(c_desc.help.details, 'C detailed documentation.')
        self.assertEqual(c_desc.help.warning, 'C warning documentation.')
        self.assertIsNone(c_desc.help.example)

        self.assertEqual(
            c_desc.generic_attributes,
            {
                'test:gen5': 1,
            },
        )

        self.assertEqual(c_desc.iop_type, 'bytes')
        self.assertEqual(c_desc.py_type, bytes)

        self.assertEqual(c_desc.min_length, 12)
        self.assertEqual(c_desc.max_length, 12)
        self.assertEqual(c_desc.length, 12)
        self.assertTrue(c_desc.deprecated)

        self.assertIsNone(c_desc.default_value)
        self.assertFalse(c_desc.optional)
        self.assertFalse(c_desc.repeated)
        self.assertFalse(c_desc.private)
        self.assertIsNone(c_desc.min)
        self.assertIsNone(c_desc.max)
        self.assertIsNone(c_desc.min_occurs)
        self.assertIsNone(c_desc.max_occurs)
        self.assertFalse(c_desc.non_zero)
        self.assertFalse(c_desc.non_empty)
        self.assertIsNone(c_desc.pattern)
        self.assertFalse(c_desc.cdata)

    def test_struct_description(self) -> None:
        desc = self.p.test.StructDescription.get_iop_description()

        self.assertEqual(desc.help.brief, 'Brief struct documentation.')
        self.assertEqual(desc.help.details, 'Detailed struct documentation.')
        self.assertEqual(desc.help.warning, 'Warning struct documentation.')
        self.assertIsNone(desc.help.example)

        self.assertEqual(
            desc.generic_attributes,
            {
                'test:gen1': 1,
            },
        )

        # FIXME: There is a bug in iopc.
        # self.assertTrue(desc.deprecated)

        a_desc = desc.fields['a']

        self.assertEqual(a_desc.help.brief, 'A brief documentation.')
        self.assertEqual(a_desc.help.details, 'A detailed documentation.')
        self.assertEqual(a_desc.help.warning, 'A warning documentation.')
        self.assertIsNone(a_desc.help.example)

        self.assertEqual(
            a_desc.generic_attributes,
            {
                'test:gen2': 'plop',
            },
        )

        self.assertEqual(a_desc.iop_type, 'double')
        self.assertEqual(a_desc.py_type, float)

        self.assertEqual(a_desc.default_value, 0.5)
        self.assertEqual(a_desc.min, -20.12)
        self.assertEqual(a_desc.max, 1.2)
        self.assertTrue(a_desc.non_zero)

        self.assertFalse(a_desc.optional)
        self.assertFalse(a_desc.repeated)
        self.assertFalse(a_desc.private)
        self.assertFalse(a_desc.deprecated)
        self.assertIsNone(a_desc.min_occurs)
        self.assertIsNone(a_desc.max_occurs)
        self.assertIsNone(a_desc.min_length)
        self.assertIsNone(a_desc.max_length)
        self.assertIsNone(a_desc.length)
        self.assertFalse(a_desc.cdata)
        self.assertFalse(a_desc.non_empty)
        self.assertIsNone(a_desc.pattern)

        b_desc = desc.fields['b']

        self.assertEqual(b_desc.help.brief, 'B brief documentation.')
        self.assertEqual(b_desc.help.details, 'B detailed documentation.')
        self.assertEqual(b_desc.help.warning, 'B warning documentation.')
        self.assertIsNone(b_desc.help.example)

        self.assertEqual(
            b_desc.generic_attributes,
            {
                'test:gen3': 15,
                'test:gen4': '{"plop":4}',
            },
        )

        self.assertEqual(b_desc.iop_type, 'test.UnionB')
        self.assertEqual(b_desc.py_type, self.p.test.UnionB)

        self.assertTrue(b_desc.optional)

        self.assertIsNone(b_desc.default_value)
        self.assertFalse(b_desc.repeated)
        self.assertFalse(b_desc.private)
        self.assertFalse(b_desc.deprecated)
        self.assertIsNone(b_desc.min)
        self.assertIsNone(b_desc.max)
        self.assertIsNone(b_desc.min_length)
        self.assertIsNone(b_desc.max_length)
        self.assertIsNone(b_desc.length)
        self.assertIsNone(b_desc.min_occurs)
        self.assertIsNone(b_desc.max_occurs)
        self.assertIsNone(b_desc.pattern)
        self.assertFalse(b_desc.non_zero)
        self.assertFalse(b_desc.non_empty)
        self.assertFalse(b_desc.cdata)

        c_desc = desc.fields['c']

        self.assertEqual(c_desc.help.brief, 'C brief documentation.')
        self.assertEqual(c_desc.help.details, 'C detailed documentation.')
        self.assertEqual(c_desc.help.warning, 'C warning documentation.')
        self.assertIsNone(c_desc.help.example)

        self.assertEqual(
            c_desc.generic_attributes,
            {
                'test:gen5': 1,
            },
        )

        self.assertEqual(c_desc.iop_type, 'test.EnumA')
        self.assertEqual(c_desc.py_type, self.p.test.EnumA)

        self.assertTrue(c_desc.repeated)
        self.assertEqual(c_desc.min_occurs, 1)
        self.assertEqual(c_desc.max_occurs, 10)
        self.assertTrue(c_desc.deprecated)

        self.assertIsNone(c_desc.default_value)
        self.assertFalse(c_desc.optional)
        self.assertFalse(c_desc.private)
        self.assertIsNone(c_desc.min)
        self.assertIsNone(c_desc.max)
        self.assertIsNone(c_desc.min_length)
        self.assertIsNone(c_desc.max_length)
        self.assertIsNone(c_desc.length)
        self.assertFalse(c_desc.non_zero)
        self.assertFalse(c_desc.non_empty)
        self.assertIsNone(c_desc.pattern)
        self.assertFalse(c_desc.cdata)

    def test_class_description(self) -> None:
        # Base class
        base_desc = self.p.test.BaseClassDescription.get_iop_description()
        assert isinstance(base_desc, iopy.IopClassDescription)

        self.assertEqual(
            base_desc.help.brief, 'Brief base class documentation.'
        )
        self.assertEqual(
            base_desc.help.details, 'Detailed base class documentation.'
        )
        self.assertEqual(
            base_desc.help.warning, 'Warning base class documentation.'
        )
        self.assertIsNone(base_desc.help.example)

        self.assertEqual(
            base_desc.generic_attributes,
            {
                'test:gen1': 1,
            },
        )

        # FIXME: There is a bug in iopc.
        # self.assertTrue(base_desc.deprecated)

        self.assertIsNone(base_desc.parent)
        self.assertTrue(base_desc.is_abstract)
        self.assertTrue(base_desc.is_private)
        self.assertEqual(base_desc.class_id, 0)

        # Static A
        base_static_a_desc = base_desc.statics['staticA']
        self.assertIs(base_desc.cls_statics['staticA'], base_static_a_desc)

        self.assertEqual(
            base_static_a_desc.help.brief, 'Static A brief documentation.'
        )
        self.assertEqual(
            base_static_a_desc.help.details,
            'Static A detailed documentation.',
        )
        self.assertEqual(
            base_static_a_desc.help.warning, 'Static A warning documentation.'
        )
        self.assertIsNone(base_static_a_desc.help.example)

        self.assertEqual(base_static_a_desc.iop_type, 'string')
        self.assertEqual(base_static_a_desc.py_type, str)
        self.assertEqual(base_static_a_desc.value, 'plop')

        # Static B
        self.assertTrue('staticB' not in base_desc.cls_statics)
        self.assertTrue('staticB' not in base_desc.statics)

        # Static C
        base_static_c_desc = base_desc.statics['staticC']
        self.assertIs(base_desc.cls_statics['staticC'], base_static_c_desc)

        self.assertEqual(
            base_static_c_desc.help.brief,
            'Static C base brief documentation.',
        )
        self.assertEqual(
            base_static_c_desc.help.details,
            'Static C base detailed documentation.',
        )
        self.assertEqual(
            base_static_c_desc.help.warning,
            'Static C base warning documentation.',
        )
        self.assertIsNone(base_static_c_desc.help.example)

        self.assertEqual(base_static_c_desc.iop_type, 'ulong')
        self.assertEqual(base_static_c_desc.py_type, int)
        self.assertEqual(base_static_c_desc.value, 42)

        # A field
        a_desc = base_desc.fields['a']

        self.assertEqual(
            a_desc.generic_attributes,
            {
                'test:gen2': 'plop',
            },
        )

        self.assertEqual(a_desc.iop_type, 'test.StructA')
        self.assertEqual(a_desc.py_type, self.p.test.StructA)

        # Child class
        child_desc = self.p.test.ChildClassDescription.get_iop_description()
        assert isinstance(child_desc, iopy.IopClassDescription)

        self.assertEqual(
            child_desc.help.brief, 'Brief child class documentation.'
        )
        self.assertEqual(
            child_desc.help.details, 'Detailed child class documentation.'
        )
        self.assertEqual(
            child_desc.help.warning, 'Warning child class documentation.'
        )
        self.assertIsNone(child_desc.help.example)

        self.assertEqual(
            child_desc.generic_attributes,
            {
                'test:gen3': 7,
            },
        )

        self.assertIs(child_desc.parent, self.p.test.BaseClassDescription)
        self.assertFalse(child_desc.is_abstract)
        self.assertFalse(child_desc.is_private)
        self.assertEqual(child_desc.class_id, 1)

        # Static A
        child_static_a_desc = child_desc.statics['staticA']
        self.assertTrue('staticA' not in child_desc.cls_statics)

        self.assertEqual(
            child_static_a_desc.help.brief, 'Static A brief documentation.'
        )
        self.assertEqual(
            child_static_a_desc.help.details,
            'Static A detailed documentation.',
        )
        self.assertEqual(
            child_static_a_desc.help.warning,
            'Static A warning documentation.',
        )
        self.assertIsNone(child_static_a_desc.help.example)

        self.assertEqual(child_static_a_desc.iop_type, 'string')
        self.assertEqual(child_static_a_desc.py_type, str)
        self.assertEqual(child_static_a_desc.value, 'plop')

        # Static B
        child_static_b_desc = child_desc.statics['staticB']
        self.assertIs(child_desc.cls_statics['staticB'], child_static_b_desc)

        # XXX: It is not exported by iopc.
        # self.assertEqual(child_static_b_desc.help.brief,
        #                 'Static B brief documentation.')
        # self.assertEqual(child_static_b_desc.help.details,
        #                 'Static B detailed documentation.')
        # self.assertEqual(child_static_b_desc.help.warning,
        #                 'Static B warning documentation.')
        # self.assertIsNone(child_static_b_desc.help.example)

        self.assertEqual(child_static_b_desc.iop_type, 'long')
        self.assertEqual(child_static_b_desc.py_type, int)
        self.assertEqual(child_static_b_desc.value, 26)

        # Static C
        child_static_c_desc = child_desc.statics['staticC']
        self.assertIs(child_desc.cls_statics['staticC'], child_static_c_desc)

        self.assertEqual(
            child_static_c_desc.help.brief,
            'Static C child brief documentation.',
        )
        self.assertEqual(
            child_static_c_desc.help.details,
            'Static C child detailed documentation.',
        )
        self.assertEqual(
            child_static_c_desc.help.warning,
            'Static C child warning documentation.',
        )
        self.assertIsNone(child_static_c_desc.help.example)

        self.assertEqual(child_static_c_desc.iop_type, 'ulong')
        self.assertEqual(child_static_c_desc.py_type, int)
        self.assertEqual(child_static_c_desc.value, 14)

        # A field
        a_desc = child_desc.fields['a']

        self.assertEqual(
            a_desc.generic_attributes,
            {
                'test:gen2': 'plop',
            },
        )

        self.assertEqual(a_desc.iop_type, 'test.StructA')
        self.assertEqual(a_desc.py_type, self.p.test.StructA)

        # B field
        b_desc = child_desc.fields['b']

        self.assertEqual(
            b_desc.generic_attributes,
            {
                'test:gen4': 3.4,
            },
        )

        self.assertEqual(b_desc.iop_type, 'test.ClassB')
        self.assertEqual(b_desc.py_type, self.p.test.ClassB)

    def test_dict_init(self) -> None:
        # Test working case
        old_struct_a = self.r.test.StructA(
            e=self.r.test.EnumA('A'),
            a=self.r.test.ClassA(
                field1=10,
            ),
            tu=[
                self.r.test.UnionA(
                    i=24,
                ),
                self.r.test.UnionA(
                    a=self.r.test.ClassB(
                        field2=87,
                    ),
                ),
                self.r.test.UnionA(
                    s='toto',
                ),
            ],
        )

        cls_b_dct: test__iop.ClassB_DictType = {
            '_class': 'test.ClassB',
            'field2': 87,
        }
        tu_list: list[test__iop.UnionA_DictType] = [
            {
                'i': 24,
            },
            {
                'a': cls_b_dct,
            },
            {
                's': 'toto',
            },
        ]
        new_struct_a = self.r.test.StructA(
            {
                'e': 'A',
                'a': {
                    'field1': 10,
                },
                'tu': tu_list,
            }
        )

        self.assertEqual(old_struct_a, new_struct_a)

        # Fail test multiple args
        with self.assertRaises(TypeError):
            self.r.test.StructA(  # pyrefly: ignore[no-matching-overload]
                {'e': 'A'}, {'e': 'B'}
            )

        # Fail test with dict arg and kwargs
        with self.assertRaises(TypeError):
            self.r.test.StructA(  # pyrefly: ignore[no-matching-overload]
                {'e': 'A'}, e='B'
            )

        # Fail test not a class
        exp = r'IOPy type `test.StructA` is not a class'
        with self.assertRaisesRegex(TypeError, exp):
            self.r.test.StructA(
                {'_class': 'test.ClassA'}  # pyrefly: ignore[bad-argument-type]
            )

        # Fail test unknown type
        exp = r'unknown IOPy type `plop.Plip`'
        with self.assertRaisesRegex(TypeError, exp):
            self.r.test.ClassA({'_class': 'plop.Plip'})

        # Fail test not a valid child
        exp = (
            r'IOPy type `test.ClassC` is not a child type of IOPy type '
            r'`test.ClassA`'
        )
        with self.assertRaisesRegex(TypeError, exp):
            self.r.test.ClassA({'_class': 'test.ClassC'})

    def test_different_str_encoding(self) -> None:
        utf8_json = b"""
        {
            "s": "M\xc3\xa9xico"
        }
        """
        union = self.r.test.UnionA(_json=utf8_json)
        self.assertEqual('México', union.s)

        latin1_json = b"""
        {
            "s": "M\xe9xico"
        }
        """
        union = self.r.test.UnionA(_json=latin1_json)
        self.assertEqual('M\\xe9xico', union.s)

    def test_init_class_twice_with_dict(self) -> None:
        class_b_dict: test__iop.ClassB_DictType = {
            '_class': 'test.ClassB',
            'field1': 28,
            'field2': 78,
        }

        union_a_1 = self.r.test.UnionA(
            {
                'a': class_b_dict,
            }
        )
        union_a_2 = self.r.test.UnionA(
            {
                'a': class_b_dict,
            }
        )
        self.assertEqual(union_a_1, union_a_2)

    def test_init_int_from_float(self) -> None:
        union_a = self.r.test.UnionA(i=42.24)
        self.assertEqual(union_a.i, 42)

    def test_init_float_from_int(self) -> None:
        union_a = self.r.test.UnionA(d=21)
        self.assertEqual(union_a.d, 21.0)

    def test_typedef(self) -> None:
        def _check(
            td_name: str,
            ref_type: type[Any],
            ref_can_be_subclassed: bool,
            **init_kwargs: Any,
        ) -> None:
            # Get from the package
            td_type = getattr(self.r.test, td_name)

            # Get from the plugin fullname
            td_type_fullname = self.r.get_type_from_fullname(
                f'test.{td_name}'
            )

            # Check that the two typedef types are the same type
            self.assertIs(td_type, td_type_fullname)

            # Check that the typedef is a proxy class to the referenced type
            self.assertEqual(td_type, ref_type)
            self.assertIsSubclass(ref_type, td_type)
            if ref_can_be_subclassed:
                self.assertIsSubclass(td_type, ref_type)

            # Check that the "standard fullname is the same for the typedef
            # and referenced type
            if hasattr(ref_type, 'fullname'):
                self.assertTrue(hasattr(td_type, 'fullname'))
                self.assertEqual(td_type.fullname(), ref_type.fullname())

            # Create instance of the typedef type
            td_obj = td_type(**init_kwargs)

            # Check that the instance is of the type of the typedef and
            # strictly of type of the reference type
            self.assertIsInstance(td_obj, td_type)
            self.assertIsInstance(td_obj, ref_type)
            self.assertIs(type(td_obj), ref_type)

            # Get the typedef description
            # TODO: Add typedef stubs
            td_desc = td_type.get_typedef_description()  # pyrefly: ignore[missing-attribute]

            # Check the typedef description fullname
            self.assertEqual(td_desc.fullname, f'test.{td_name}')

            # Check the typedef generic attributes
            self.assertEqual(td_desc.attrs.generic_attributes, {'is:td': 1})

        _check('ByteTypedef', int, True)
        _check('UbyteTypedef', int, True)
        _check('ShortTypedef', int, True)
        _check('UshortTypedef', int, True)
        _check('IntTypedef', int, True)
        _check('UintTypedef', int, True)
        _check('LongTypedef', int, True)
        _check('UlongTypedef', int, True)
        _check('DoubleTypedef', float, True)
        _check('XmlTypedef', str, True)
        _check('StringTypedef', str, True)
        _check('BytesTypedef', bytes, True)

        _check('BoolTypedef', bool, False)
        _check('VoidTypedef', type(None), False)

        _check('EnumATypedef', self.r.test.EnumA, True)
        _check('UnionATypedef', self.r.test.UnionA, True, i=10)
        _check('StructATypedef', self.r.test.StructA, True)
        _check('ClassATypedef', self.r.test.ClassA, True)
        _check('VoidRequired', self.r.testvoid.VoidRequired, True)

    def test_field_name_python_keyword(self) -> None:
        # Struct
        s = self.r.test.PythonKeywordStruct(
            {
                'from': 12,
            }
        )
        self.assertEqual(getattr(s, 'from'), 12)

        # Class
        c = self.r.test.PythonKeywordClassChild(
            {
                'await': 9,
                'async': 8,
            }
        )
        self.assertEqual(getattr(c, 'await'), 9)
        self.assertEqual(getattr(c, 'async'), 8)

        # Union
        u = self.r.test.PythonKeywordUnion(
            {
                'raise': 7,
            }
        )
        self.assertEqual(getattr(u, 'raise'), 7)
        self.assertFalse(hasattr(c, 'nonlocal'))

    def test_manual_inheritance(self) -> None:
        class ManualEnum(self.r.test.EnumA):
            pass

        enum = ManualEnum('A')
        self.assertEqual(enum, 'A')
        self.assertEqual(enum, 1)
        self.assertEqual(enum, ManualEnum('A'))

        class ManualStruct(self.r.test.StructB):
            pass

        struct = ManualStruct(a='test', b='foo')
        self.assertEqual(
            struct.to_dict(),
            {
                'a': 'test',
                'b': 'foo',
                'tab': [],
            },
        )
        self.assertEqual(struct, ManualStruct(a='test', b='foo'))

        class ManualUnion(self.r.test.UnionA):
            pass

        union = ManualUnion(i=20)
        self.assertEqual(
            union.to_dict(),
            {
                'i': 20,
            },
        )
        self.assertEqual(union, ManualUnion(i=20))

        class ManualClass(self.r.test.ClassB):
            pass

        klass = ManualClass(optField=1)
        self.assertEqual(
            klass.to_dict(),
            {
                '_class': 'test.ClassB',
                'field1': 0,
                'field2': 0,
                'optField': 1,
            },
        )
        self.assertEqual(klass, ManualClass(optField=1))


# }}}
# {{{ IopyIfaceTests


@z.ZGroup
class IopyIfaceTests(z.TestCase):
    @override
    def setUp(self) -> None:
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = cast('test_iop_plugin__iop.Plugin', iopy.Plugin(plugin_file))
        self.r = self.p.register()

        self.uri = make_uri()

        def rpc_impl_a(
            rpc_args: test__iop.InterfaceA_funA_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
            return rpc_args.res(status='A', res=1000)

        def rpc_impl_b(
            rpc_args: test__iop.InterfaceA_funB_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funB_RPCServer.RpcRes:
            str_field: str | None = getattr(rpc_args.arg.a, 'strField', None)
            return rpc_args.res(status='B', res=0, strField=str_field)

        def rpc_impl_v(
            rpc_args: test__iop.InterfaceA_funToggleVoid_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funToggleVoid_RPCServer.RpcRes:
            if hasattr(rpc_args.arg, 'ov'):
                return rpc_args.res()
            return rpc_args.res(ov=None)

        self.s = self.r.channel_server()
        _rpcs = (
            self.s.test_ModuleA.interfaceA._rpcs  # noqa: SLF001 (private-member-access)  # pyrefly: ignore[missing-attribute]
        )
        _rpcs.funA.impl = rpc_impl_a
        self.s.test_ModuleA.interfaceA.funB.impl = rpc_impl_b
        self.s.test_ModuleA.interfaceA.funToggleVoid.impl = rpc_impl_v
        self.s.listen(uri=self.uri)

    def test_iopy_iface(self) -> None:
        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA:  # noqa: N801 (invalid-class-name)
            cls_attr = 0

            def __init__(self) -> None:
                self.attr1: int | None = 1
                self.attr2 = 2

            def my_method(self) -> int | None:
                self.cls_attr = 10
                return self.attr1

            def funA(  # noqa: N802 (invalid-function-name)
                self,
                *args: Any,
                **kwargs: Any,
            ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
                self.attr1 = kwargs.get('a')
                rpcs = self._rpcs  # pyrefly: ignore[missing-attribute]
                res: test__iop.InterfaceA_funA_Res = rpcs.funA(
                    *args, **kwargs
                )
                self.attr2 = int(res.status)
                return res

            def funToggleVoid(  # noqa: N802 (invalid-function-name)
                self,
                *args: Any,
                **kwargs: Any,
            ) -> test__iop.InterfaceA_funToggleVoid_RPCServer.RpcRes:
                rpcs = self._rpcs  # pyrefly: ignore[missing-attribute]
                res: test__iop.InterfaceA_funToggleVoid_Res = (
                    rpcs.funToggleVoid(*args, **kwargs)
                )
                return res

        c = self.r.connect(self.uri)

        iface = c.test_ModuleA.interfaceA

        attr = getattr(iface, 'cls_attr', None)
        self.assertEqual(
            attr, 0, f'class attribute failed; value: {attr}, expected: 0'
        )

        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            1,
            f'custom init failed; value of attr1: {attr}, expected: 1,',
        )

        self.assertTrue(
            hasattr(iface, 'my_method'), 'custom method not added'
        )
        ret = iface.my_method()
        self.assertEqual(
            ret, 1, f'custom method failed; result: {ret}, expected: 1,'
        )
        attr = getattr(iface, 'cls_attr', None)
        self.assertEqual(
            attr,
            10,
            f'custom method failed; value of cls_attr: {attr}, expected: 10',
        )

        a = self.r.test.ClassA(field1=100)
        ret = iface.funA(a=a)

        ret = getattr(ret, 'res', None)
        self.assertEqual(
            ret,
            1000,
            f'rpc override call failed; res.res: {ret}, expected: 1000',
        )

        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            a,
            f'rpc override failed; attr1 value: {attr}, expected: {a}',
        )

        attr = getattr(iface, 'attr2', None)
        exp = self.r.test.EnumA('A')
        self.assertEqual(
            attr,
            exp,
            f'rpc override failed; attr2 value: {attr}, expected: {exp}',
        )

        ret = iface.funB(a=self.r.test.ClassA())
        ret = getattr(ret, 'status', None)
        exp = self.r.test.EnumA('B')
        self.assertEqual(
            ret, exp, f'rpc failed; status: {ret}, expected: {exp}'
        )

        ret = iface.funToggleVoid(ov=None)
        self.assertFalse(hasattr(ret, 'ov'))

        ret = iface.funToggleVoid()
        self.assertTrue(hasattr(ret, 'ov'))
        self.assertIsNone(ret.ov)

    def test_iopy_iface_hooks(self) -> None:
        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA1:  # noqa: N801 (invalid-class-name)
            pre_hook_rpc: iopy.RPCBase[Any, Any, Any]
            pre_hook_args: tuple[Any, ...]
            pre_hook_kwargs: dict[str, Any]
            post_hook_rpc: iopy.RPCBase[Any, Any, Any]
            post_hook_res: iopy.StructUnionBase

            def __pre_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                self,
                rpc: iopy.RPCBase[Any, Any, Any],
                *args: Any,
                **kwargs: Any,
            ) -> None:
                self.pre_hook_rpc = rpc
                self.pre_hook_args = args
                self.pre_hook_kwargs = kwargs

            def __post_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                self,
                rpc: iopy.RPCBase[Any, Any, Any],
                res: iopy.StructUnionBase,
            ) -> None:
                self.post_hook_rpc = rpc
                self.post_hook_res = res

        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA

        kwargs: test__iop.InterfaceA_funA_Arg_ParamDictType = {
            'a': self.r.test.ClassA(),
        }
        iface.funA(**kwargs)

        attr = getattr(iface, 'pre_hook_rpc', None)
        self.assertEqual(
            attr,
            iface.funA,
            f'pre_hook failed for rpc argument; value: {attr}; '
            f'expected: {iface.funA}',
        )

        attr = getattr(iface, 'pre_hook_args', None)
        self.assertEqual(
            attr,
            (),
            f'pre_hook failed for args argument; value: {attr}; expected: ()',
        )

        attr = getattr(iface, 'pre_hook_kwargs', None)
        self.assertEqual(
            attr,
            kwargs,
            f'pre_hook failed for kwargs argument; value: {attr}; '
            f'expected: {kwargs}',
        )

        attr = getattr(iface, 'post_hook_rpc', None)
        self.assertEqual(
            attr,
            iface.funA,
            f'post_hook failed for rpc argument; value: {attr}; '
            f'expected: {iface.funA}',
        )

        attr = getattr(iface, 'post_hook_res', None)
        exp = iface.funA.res()(status='A', res=1000)
        self.assertEqual(
            attr,
            exp,
            f'post_hook failed for rpc argument; value: {attr}; '
            f'expected: {exp}',
        )

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA2:  # noqa: N801 (invalid-class-name)
            r = self.r

            def __pre_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                self,
                rpc: iopy.RPCBase[Any, Any, Any],
                *args: Any,
                **kwargs: Any,
            ) -> tuple[tuple[Any, ...], dict[str, Any]]:
                return ((), {'a': type(self).r.test.ClassA()})

            @classmethod
            def __post_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                cls,
                rpc: iopy.RPCBase[Any, Any, Any],
                res: iopy.StructUnionBase,
            ) -> int:
                return 0

        ret = iface.funA(0, x=0, y=0)  # pyrefly: ignore[no-matching-overload]
        self.assertEqual(
            ret,
            0,
            f'hooks arguments/result replacement failed; result: {ret}; '
            'expected: 0',
        )

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA3:  # noqa: N801 (invalid-class-name)
            r = self.r

            def __pre_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                self,
                rpc: iopy.RPCBase[Any, Any, Any],
                *args: Any,
                **kwargs: Any,
            ) -> tuple[tuple[Any, ...], dict[str, Any]]:
                rpc_cast = cast('test__iop.InterfaceA_funA_RPC', rpc)
                new_args = (rpc_cast.arg()(a=type(self).r.test.ClassA()),)
                return (new_args, {})

            @classmethod
            def __post_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                cls,
                rpc: iopy.RPCBase[Any, Any, Any],
                res: iopy.StructUnionBase,
            ) -> int:
                return 0

        ret = iface.funA(0, x=0, y=0)  # pyrefly: ignore[no-matching-overload]
        self.assertEqual(
            ret,
            0,
            f'hooks arguments/result replacement failed; result: {ret}; '
            'expected: 0',
        )

        def default_pre_hook(
            self: iopy.IfaceBase,
            rpc: iopy.RPCBase[Any, Any, Any],
            *args: Any,
            **kwargs: Any,
        ) -> None:
            self.attr1 = 1  # pyrefly: ignore[missing-attribute, implicitly-defined-attribute]

        def default_post_hook(
            self: iopy.IfaceBase,
            rpc: iopy.RPCBase[Any, Any, Any],
            res: iopy.StructUnionBase,
        ) -> None:
            self.attr2 = 1  # pyrefly: ignore[missing-attribute, implicitly-defined-attribute]

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA4:  # noqa: N801 (invalid-class-name)
            pass

        # Delete previous hooks due to test_InterfaceA3 monkey-patch
        del test_InterfaceA3.__pre_hook__
        del test_InterfaceA3.__post_hook__

        self.r.default_pre_hook = default_pre_hook
        self.r.default_post_hook = default_post_hook

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            1,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: 1',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            1,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: 1',
        )

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA5:  # noqa: N801 (invalid-class-name)
            pass

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            1,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: 1',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            1,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: 1',
        )

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA6:  # noqa: N801 (invalid-class-name)
            attr1: int
            attr2: int

            def __pre_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                self,
                rpc: iopy.RPCBase[Any, Any, Any],
                *args: Any,
                **kwargs: Any,
            ) -> None:
                self.attr1 = 2

            def __post_hook__(  # noqa: PLW3201 (bad-dunder-method-name)
                self,
                rpc: iopy.RPCBase[Any, Any, Any],
                res: iopy.StructUnionBase,
            ) -> None:
                self.attr2 = 2

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            2,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: 2',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            2,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: 2',
        )

        # custom pre/post hooks from external functions are used instead of
        # default pre/post hooks
        def iface_pre_hook_1(
            self: iopy.IfaceBase,
            rpc: iopy.RPCBase[Any, Any, Any],
            *args: Any,
            **kwargs: Any,
        ) -> None:
            self.attr1 = 3  # pyrefly: ignore[missing-attribute]

        def iface_post_hook_1(
            self: iopy.IfaceBase,
            rpc: iopy.RPCBase[Any, Any, Any],
            res: iopy.StructUnionBase,
        ) -> None:
            self.attr2 = 3  # pyrefly: ignore[missing-attribute]

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA7:  # noqa: N801 (invalid-class-name)
            __pre_hook__ = iface_pre_hook_1
            __post_hook__ = iface_post_hook_1

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            3,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: 3',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            3,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: 3',
        )

        # added pre/post hooks after class definition are used instead of
        # default pre/post hooks
        def iface_pre_hook_2(
            self: iopy.IfaceBase,
            rpc: iopy.RPCBase[Any, Any, Any],
            *args: Any,
            **kwargs: Any,
        ) -> None:
            self.attr1 = 4  # pyrefly: ignore[missing-attribute]

        def iface_post_hook_2(
            self: iopy.IfaceBase,
            rpc: iopy.RPCBase[Any, Any, Any],
            res: iopy.StructUnionBase,
        ) -> None:
            self.attr2 = 4  # pyrefly: ignore[missing-attribute]

        # Delete previous hooks due to test_InterfaceA7 monkey-patch
        del test_InterfaceA7.__pre_hook__
        del test_InterfaceA7.__post_hook__

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA8:  # noqa: N801 (invalid-class-name)
            pass

        type(iface).__pre_hook__ = iface_pre_hook_2
        type(iface).__post_hook__ = iface_post_hook_2

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            4,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: 4',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            4,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: 4',
        )

        # reset default pre/post hooks
        del self.r.default_pre_hook
        del self.r.default_post_hook
        iface.attr1 = None  # pyrefly: ignore[missing-attribute]
        iface.attr2 = None  # pyrefly: ignore[missing-attribute]

        # iface should still have its custom pre/post hooks
        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            4,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: 4',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            4,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: 4',
        )

        # reset iface pre/post hooks
        iface.attr1 = None  # pyrefly: ignore[missing-attribute]
        iface.attr2 = None  # pyrefly: ignore[missing-attribute]
        del type(iface).__pre_hook__
        del type(iface).__post_hook__

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA9:  # noqa: N801 (invalid-class-name)
            pass

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(
            attr,
            None,
            f'default pre_hook failed for rpc argument; value: {attr}; '
            'expected: None',
        )
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(
            attr,
            None,
            f'default post_hook failed for rpc argument; value: {attr}; '
            'expected: None',
        )

    def test_iopy_iface_inheritance(self) -> None:
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA

        class CommonClass1:
            common_val1: int

            def foo(self) -> None:
                self.common_val1 = 42

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA1(CommonClass1):  # noqa: N801 (invalid-class-name)
            common_val2: int

            @override
            def foo(self) -> None:
                super(test_InterfaceA1, self).foo()
                self.common_val2 = 12

        iface.foo()  # pyrefly: ignore[missing-attribute]
        self.assertEqual(iface.common_val1, 42)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(iface.common_val2, 12)  # pyrefly: ignore[missing-attribute]

        class BaseCommonClass1:
            common_val2: int

            def bar(self) -> None:
                self.common_val2 = 7777

        class CommonClass2(BaseCommonClass1):
            common_val1: int

            def foo(self) -> None:
                self.bar()
                self.common_val1 = 84

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA2(CommonClass2, CommonClass1):  # noqa: N801 (invalid-class-name)
            pass

        # Delete previous foo() method due to test_InterfaceA1 monkey-patch
        del test_InterfaceA1.foo

        iface.foo()  # pyrefly: ignore[missing-attribute]
        self.assertEqual(iface.common_val1, 84)
        self.assertEqual(iface.common_val2, 7777)

        c.disconnect()
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA3:  # noqa: N801 (invalid-class-name)
            pass

        self.assertFalse(hasattr(iface, 'common_val1'))
        self.assertFalse(hasattr(iface, 'common_val2'))

    def test_iopy_threads(self) -> None:
        """Test IOPy with threads"""
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        exp_res = iface.funA(a=obj_a)

        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, True, False)

    def test_iopy_forks(self) -> None:
        """Test IOPy with forks"""
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        exp_res = iface.funA(a=obj_a)

        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, False, True)

    def test_iopy_threads_and_forks(self) -> None:
        """Test IOPy with threads, forks and threads inside forks"""
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        exp_res = iface.funA(a=obj_a)

        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, True, True)

    def test_module_short_name(self) -> None:
        """Test we add modules with short name when not ambiguous"""
        # XXX: Deprecated, and not exposed with the type system
        self.assertIs(self.s.test_ModuleA, self.s.ModuleA)  # pyrefly: ignore[missing-attribute]

    def test_iface_double_upgrade(self) -> None:
        """Test iface with double level of upgrade"""

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA1:  # noqa: N801 (invalid-class-name)
            @staticmethod
            def foo() -> str:
                return 'foo'

        @z_monkey_patch(self.r.test.interfaces.InterfaceA)
        class test_InterfaceA2:  # noqa: N801 (invalid-class-name)
            @staticmethod
            def bar() -> str:
                return 'bar'

        iface1 = self.p.test.interfaces.InterfaceA
        self.assertEqual(iface1.foo(), 'foo')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(iface1.bar(), 'bar')  # pyrefly: ignore[missing-attribute]
        iface2 = test_InterfaceA1
        self.assertEqual(iface2.foo(), 'foo')
        self.assertEqual(iface2.bar(), 'bar')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(str(iface1), str(iface2))

    def test_iface_with_dict_arg(self) -> None:
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        res = iface.funA(
            {
                'a': {
                    'field1': 1,
                },
            }
        )
        self.assertEqual(res.status, 'A')
        self.assertEqual(res.res, 1000)

    def test_client_connect_callbacks(self) -> None:
        """Test client connection and disconnection callbacks"""

        # Create object and callbacks
        class CbsCalled:
            __slots__ = ('connect', 'disconnect', 'was_connected')

            def __init__(self) -> None:
                self.connect = False
                self.disconnect = False
                self.was_connected = False

        cbs_called = CbsCalled()

        def connect_cb(channel: iopy.Channel) -> None:
            cbs_called.connect = True

        def disconnect_cb(channel: iopy.Channel, connected: bool) -> None:
            cbs_called.disconnect = True
            cbs_called.was_connected = connected

        # Create the client with the callbacks
        client = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, self.uri)
        )
        client.on_connect = connect_cb
        client.on_disconnect = disconnect_cb

        # Connect the client, the callback should be called
        client.connect()
        self.assertTrue(cbs_called.connect)
        cbs_called.connect = False

        # Disconnect the client, the callback should be called
        client.disconnect()
        self.assertTrue(cbs_called.disconnect)
        self.assertTrue(cbs_called.was_connected)
        cbs_called.disconnect = False
        cbs_called.was_connected = False

        # Reconnect the client, the callback should be called
        client.connect()
        self.assertTrue(cbs_called.connect)
        cbs_called.connect = False

        # Stop the server
        self.s.stop()

        # Wait for the client to be disconnected, the callback should be
        # called
        for _ in range(100):
            if not client.is_connected() and cbs_called.disconnect:
                break
            time.sleep(0.01)
        else:
            self.fail('client is not disconnected')
        self.assertTrue(cbs_called.was_connected)
        cbs_called.disconnect = False
        cbs_called.was_connected = False

        # Restart the server
        self.s.listen(uri=self.uri)

        # Make an RPC call, the client should reconnect and the callback
        # should be called
        iface = client.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        iface.funA(a=obj_a)
        self.assertTrue(cbs_called.connect)
        cbs_called.connect = False

        # Create a client to an invalid server
        invalid_uri = make_uri()
        client = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, invalid_uri)
        )
        client.on_connect = connect_cb
        client.on_disconnect = disconnect_cb

        # Try connect to the invalid server, the connect should not be called,
        # the disconnect callback should be called with `connected` set to
        # False
        try:
            client.connect()
        except iopy.Error:
            pass
        else:
            self.fail('expected connection error')
        self.assertFalse(cbs_called.connect)
        self.assertTrue(cbs_called.disconnect)
        self.assertFalse(cbs_called.was_connected)

    def test_string_conversion_on_rpc(self) -> None:
        """Test string conversion is well handled when calling an RPC"""
        # Connect the client
        client = self.p.connect(self.uri)
        iface = client.test_ModuleA.interfaceA

        # Do the query, strField should be converted to a string
        ret = iface.funB({'a': {'strField': b'plop'}})
        exp = type(ret)(
            {
                'status': 'B',
                'res': 0,
                'strField': 'plop',
            }
        )
        self.assertEqual(
            ret, exp, f'rpc failed; status: {ret}, expected: {exp}'
        )

    def test_disconnect_on_connect_cb(self) -> None:
        """Test disconnecting client on client connect callback"""
        client = iopy.Channel(self.p, self.uri)

        # Specify the connection callback to disconnect the client on
        # connection
        def connect_cb(channel: iopy.Channel) -> None:
            client.disconnect()

        client.on_connect = connect_cb

        # Connect the client
        try:
            client.connect()
        except iopy.Error:
            # The client can be disconnected directly on connection
            pass

        # Wait for the client to be disconnected
        for _ in range(100):
            if not client.is_connected():
                break
            time.sleep(0.01)
        else:
            self.fail('client is not disconnected')

    def test_rpc_server_exception(self) -> None:
        # Create a server with an error raised by the RPC implementation, and
        # a channel to the server
        def rpc_impl_a(
            rpc_args: test__iop.InterfaceA_funA_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
            raise RuntimeError('test exception')

        s = self.r.channel_server()

        s.test_ModuleA.interfaceA.funA.impl = rpc_impl_a

        uri = make_uri()
        s.listen(uri=uri)
        c = self.r.connect(uri)

        def call_rpc() -> None:
            try:
                c.test_ModuleA.interfaceA.funA(a=self.r.test.ClassB(field1=1))
            except iopy.Error:
                pass
            else:
                self.fail('expected iopy.Error SERVER_ERROR')

        # Check without an exception callback, the error is raised as a
        # warning
        with warnings.catch_warnings(record=True) as w:
            call_rpc()
            self.assertEqual(len(w), 1)
            self.assertEqual(w[0].category, iopy.UnexpectedExceptionWarning)
            self.assertIn('RuntimeError', str(w[0].message))

        # Check with an exception callback, the error should be retrieved by
        # the callback
        exc_type: list[type[Exception]] = []

        def on_exception_cb_catch(exc: Exception) -> None:
            exc_type.append(type(exc))

        s.on_exception = on_exception_cb_catch

        with warnings.catch_warnings(record=True) as w:
            call_rpc()
            self.assertEqual(len(w), 0)
        self.assertEqual(len(exc_type), 1)
        self.assertEqual(exc_type[0], RuntimeError)

        # Check with an exception callback, but the exception callback raises
        # an exception, the error is raised as a warning
        def on_exception_cb_err(exc: Exception) -> None:
            raise ValueError

        s.on_exception = on_exception_cb_err

        with warnings.catch_warnings(record=True) as w:
            call_rpc()
            self.assertEqual(len(w), 1)
            self.assertEqual(w[0].category, iopy.UnexpectedExceptionWarning)
            self.assertIn('ValueError', str(w[0].message))

    def test_rpc_client_exception(self) -> None:
        # Create a simple RPC server
        s = self.r.channel_server()
        uri = make_uri()
        s.listen(uri=uri)
        c = iopy.Channel(self.p, uri)

        # Make the client callback on_connect raise an exception
        def on_connect_cb(channel: iopy.Channel) -> None:
            raise RuntimeError

        c.on_connect = on_connect_cb

        # Check without an exception callback, the error is raised as a
        # warning
        with warnings.catch_warnings(record=True) as w:
            c.connect()
            self.assertEqual(len(w), 1)
            self.assertEqual(w[0].category, iopy.UnexpectedExceptionWarning)
            self.assertIn('RuntimeError', str(w[0].message))

        # Disconnect the client
        def disconnect_and_wait() -> None:
            c.disconnect()
            for _ in range(100):
                if not c.is_connected():
                    break
                time.sleep(0.01)
            else:
                self.fail('client is not disconnected')

        disconnect_and_wait()

        # Check with an exception callback, the error should be retrieved by
        # the callback
        exc_type: list[type[Exception]] = []

        def on_exception_cb_connect_catch(exc: Exception) -> None:
            exc_type.append(type(exc))

        c.on_exception = on_exception_cb_connect_catch

        with warnings.catch_warnings(record=True) as w:
            c.connect()
            self.assertEqual(len(w), 0)
        self.assertEqual(len(exc_type), 1)
        self.assertEqual(exc_type[0], RuntimeError)

        # Disconnect the client
        disconnect_and_wait()

        # Check with an exception callback, but the exception callback raises
        # an exception, the error is raised as a warning
        def on_exception_cb_connect_err(exc: Exception) -> None:
            raise ValueError

        c.on_exception = on_exception_cb_connect_err

        with warnings.catch_warnings(record=True) as w:
            c.connect()
            self.assertEqual(len(w), 1)
            self.assertEqual(w[0].category, iopy.UnexpectedExceptionWarning)
            self.assertIn('ValueError', str(w[0].message))

        # Disconnect the client, reset the callbacks and connect the client
        disconnect_and_wait()
        c.on_connect = None
        c.on_exception = None
        c.connect()

        # Make the client on_disconnect raise an exception
        def on_disconnect_cb(channel: iopy.Channel, connected: bool) -> None:
            raise KeyError

        c.on_disconnect = on_disconnect_cb

        # Check without an exception callback, the error is raised as a
        # warning
        exc_type = []
        with warnings.catch_warnings(record=True) as w:
            disconnect_and_wait()
            self.assertEqual(len(w), 1)
            self.assertEqual(w[0].category, iopy.UnexpectedExceptionWarning)
            self.assertIn('KeyError', str(w[0].message))

        # Check with an exception callback, the error should be retrieved by
        # the callback
        exc_type = []

        def on_exception_cb_disconnect_catch(exc: Exception) -> None:
            exc_type.append(type(exc))

        c.on_exception = on_exception_cb_disconnect_catch

        c.connect()
        with warnings.catch_warnings(record=True) as w:
            disconnect_and_wait()
            self.assertEqual(len(w), 0)
        self.assertEqual(len(exc_type), 1)
        self.assertEqual(exc_type[0], KeyError)

        # Check with an exception callback, but the exception callback raises
        # an exception, the error is raised as a warning
        def on_exception_cb_disconnect_err(exc: Exception) -> None:
            raise TypeError

        c.on_exception = on_exception_cb_disconnect_err

        c.connect()
        with warnings.catch_warnings(record=True) as w:
            disconnect_and_wait()
            self.assertEqual(len(w), 1)
            self.assertEqual(w[0].category, iopy.UnexpectedExceptionWarning)
            self.assertIn('TypeError', str(w[0].message))

    def test_accessors(self) -> None:
        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )

        self.assertEqual(self.p.test.get_iop_name(), 'test')
        self.assertEqual(self.p.test.get_py_name(), 'test')

        self.assertEqual(c.test_ModuleA.get_iop_fullname(), 'test.ModuleA')
        self.assertEqual(c.test_ModuleA.get_py_fullname(), 'test_ModuleA')
        self.assertEqual(
            self.p.modules.test_ModuleA.get_iop_fullname(), 'test.ModuleA'
        )
        self.assertEqual(
            self.p.modules.test_ModuleA.get_py_fullname(), 'test_ModuleA'
        )

        self.assertEqual(
            c.test_ModuleA.interfaceA.get_iop_fullname(), 'test.InterfaceA'
        )
        self.assertEqual(
            c.test_ModuleA.interfaceA.get_py_fullname(), 'test.InterfaceA'
        )
        self.assertEqual(
            self.p.modules.test_ModuleA.interfaceA.get_iop_fullname(),
            'test.InterfaceA',
        )
        self.assertEqual(
            self.p.modules.test_ModuleA.interfaceA.get_py_fullname(),
            'test.InterfaceA',
        )

        self.assertEqual(
            c.test_ModuleA.interfaceA.get_basename(), 'interfaceA'
        )
        self.assertEqual(
            self.p.modules.test_ModuleA.interfaceA.get_basename(),
            'interfaceA',
        )

        self.assertEqual(c.test_ModuleA.interfaceA.get_tag(), 1)
        self.assertEqual(self.p.modules.test_ModuleA.interfaceA.get_tag(), 1)

        self.assertEqual(c.test_ModuleA.interfaceA.funA.get_tag(), 1)
        self.assertEqual(
            self.p.modules.test_ModuleA.interfaceA.funA.get_tag(), 1
        )

        self.assertEqual(c.test_ModuleA.interfaceA.funA.get_cmd(), 65537)
        self.assertEqual(
            self.p.modules.test_ModuleA.interfaceA.funA.get_cmd(), 65537
        )

    def test_emptystuffs_fullnames(self) -> None:
        """
        Test get_iop_fullname / get_py_fullname / get_*_module_fullname
        on the multi-component package `test.emptystuffs`, where the
        IOPy form replaces the dots in the package portion with
        underscores.
        """
        pkg = self.p.test_emptystuffs
        self.assertEqual(pkg.get_iop_name(), 'test.emptystuffs')
        self.assertEqual(pkg.get_py_name(), 'test_emptystuffs')

        # Modules from a multi-component package: all dots replaced.
        module_cls = self.p.modules.test_emptystuffs_EmptyIfaceModule
        self.assertEqual(
            module_cls.get_iop_fullname(),
            'test.emptystuffs.EmptyIfaceModule',
        )
        self.assertEqual(
            module_cls.get_py_fullname(),
            'test_emptystuffs_EmptyIfaceModule',
        )

        # Interfaces from a multi-component package: only the package
        # portion is underscored, the local name stays joined by a dot.
        iface_cls = (
            self.p.modules.test_emptystuffs_EmptyIfaceModule.emptyIface
        )
        self.assertEqual(
            iface_cls.get_iop_fullname(), 'test.emptystuffs.EmptyIface'
        )
        self.assertEqual(
            iface_cls.get_py_fullname(), 'test_emptystuffs.EmptyIface'
        )

        # Module-rooted iface fullname uses the iface basename in the
        # module (`emptyIface`) joined to the module fullname by a dot.
        self.assertEqual(
            iface_cls.get_iop_module_fullname(),
            'test.emptystuffs.EmptyIfaceModule.emptyIface',
        )
        self.assertEqual(
            iface_cls.get_py_module_fullname(),
            'test_emptystuffs_EmptyIfaceModule.emptyIface',
        )

        # Same distinction for IOP types (struct/enum) declared in the
        # multi-component package.
        struct_cls = self.p.test_emptystuffs.EmptyStruct
        self.assertEqual(
            struct_cls.get_iop_fullname(),
            'test.emptystuffs.EmptyStruct',
        )
        self.assertEqual(
            struct_cls.get_py_fullname(), 'test_emptystuffs.EmptyStruct'
        )

        enum_cls = self.p.tst1_tst2.Code
        self.assertEqual(enum_cls.get_iop_fullname(), 'tst1.tst2.Code')
        self.assertEqual(enum_cls.get_py_fullname(), 'tst1_tst2.Code')

    def test_struct_union_enum_fullnames(self) -> None:
        """
        Test get_iop_fullname / get_py_fullname on struct, union and
        enum types from a single-component package, where the IOP and
        IOPy forms are identical.
        """
        struct_cls = self.p.test.StructA
        self.assertEqual(struct_cls.get_iop_fullname(), 'test.StructA')
        self.assertEqual(struct_cls.get_py_fullname(), 'test.StructA')

        union_cls = self.p.test.UnionA
        self.assertEqual(union_cls.get_iop_fullname(), 'test.UnionA')
        self.assertEqual(union_cls.get_py_fullname(), 'test.UnionA')

        enum_cls = self.p.test.EnumA
        self.assertEqual(enum_cls.get_iop_fullname(), 'test.EnumA')
        self.assertEqual(enum_cls.get_py_fullname(), 'test.EnumA')

    def test_enum_get_name_values_ranges(self) -> None:
        """Test EnumBase get_name / get_values / get_ranges accessors."""
        enum_cls = self.p.test.EnumA
        self.assertEqual(enum_cls.get_name(), 'EnumA')

        values = enum_cls.get_values()
        self.assertEqual(values['A'], 1)
        self.assertEqual(values['B'], 2)

        self.assertIsInstance(enum_cls.get_ranges(), dict)

    def test_iface_get_module(self) -> None:
        """
        Test iface get_module returns the iface's module.

        Called on the iface class, get_module returns the module class
        (the first one the iface was registered with). Called on a
        channel-bound iface instance, it returns the module instance bound
        to the channel.
        """
        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )

        iface_cls = self.p.modules.test_ModuleA.interfaceA
        module_cls = self.p.modules.test_ModuleA
        self.assertIs(iface_cls.get_module(), module_cls)

        iface_inst = c.test_ModuleA.interfaceA
        module_inst = c.test_ModuleA
        self.assertIs(iface_inst.get_module(), module_inst)

    def test_iface_get_module_fullname(self) -> None:
        """
        Test iface get_iop_module_fullname / get_py_module_fullname
        return the module fullname + iface basename, in either IOP or
        IOPy form.
        """
        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )

        iface_cls = self.p.modules.test_ModuleA.interfaceA
        self.assertEqual(
            iface_cls.get_iop_module_fullname(),
            'test.ModuleA.interfaceA',
        )
        self.assertEqual(
            iface_cls.get_py_module_fullname(), 'test_ModuleA.interfaceA'
        )

        iface_inst = c.test_ModuleA.interfaceA
        self.assertEqual(
            iface_inst.get_iop_module_fullname(),
            'test.ModuleA.interfaceA',
        )
        self.assertEqual(
            iface_inst.get_py_module_fullname(),
            'test_ModuleA.interfaceA',
        )

    def test_rpc_get_iface(self) -> None:
        """
        Test RPC get_iface returns the iface class on RPCBase and the
        iface instance on RPCs bound to a channel.
        """
        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )

        iface_cls = self.p.modules.test_ModuleA.interfaceA
        rpc_cls = iface_cls.funA
        self.assertIsInstance(rpc_cls, iopy.RPCBase)
        self.assertIs(rpc_cls.get_iface(), iface_cls)

        iface_inst = c.test_ModuleA.interfaceA
        rpc_inst = iface_inst.funA
        self.assertIsInstance(rpc_inst, iopy.RPCBase)
        self.assertIs(rpc_inst.get_iface(), iface_inst)

    def test_rpc_get_module_fullname(self) -> None:
        """
        Test RPC get_iop_module_fullname / get_py_module_fullname return
        the module fullname + iface basename + RPC name on both RPCBase
        (iface-class RPCs) and channel-bound RPCs.
        """
        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )

        rpc_cls = self.p.modules.test_ModuleA.interfaceA.funA
        self.assertIsInstance(rpc_cls, iopy.RPCBase)
        self.assertEqual(
            rpc_cls.get_iop_module_fullname(),
            'test.ModuleA.interfaceA.funA',
        )
        self.assertEqual(
            rpc_cls.get_py_module_fullname(),
            'test_ModuleA.interfaceA.funA',
        )

        rpc_inst = c.test_ModuleA.interfaceA.funA
        self.assertIsInstance(rpc_inst, iopy.RPCBase)
        self.assertEqual(
            rpc_inst.get_iop_module_fullname(),
            'test.ModuleA.interfaceA.funA',
        )
        self.assertEqual(
            rpc_inst.get_py_module_fullname(),
            'test_ModuleA.interfaceA.funA',
        )

    def test_rpc_args_types(self) -> None:
        """Test rpc_args types correspond to the RPC Arg/Res/Exn types"""
        iface_cls = self.p.test.interfaces.InterfaceA
        server_excs: list[BaseException] = []

        # Server impls verify the runtime types of `rpc_args` match the
        # RPC `Arg`/`Res`/`Exn` types declared by the interface.
        def rpc_impl_a(
            rpc_args: test__iop.InterfaceA_funA_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
            self.assertIsInstance(rpc_args.arg, iface_cls.funA.Arg)
            self.assertIs(type(rpc_args.arg), iface_cls.funA.Arg)
            self.assertIs(rpc_args.res, iface_cls.funA.Res)
            self.assertIs(rpc_args.exn, iface_cls.funA.Exn)
            return rpc_args.res(status='A', res=1000)

        def rpc_impl_async(
            rpc_args: test__iop.InterfaceA_funAsync_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funAsync_RPCServer.RpcRes:
            self.assertIsInstance(rpc_args.arg, iface_cls.funAsync.Arg)
            self.assertIs(type(rpc_args.arg), iface_cls.funAsync.Arg)
            self.assertIs(rpc_args.res, iface_cls.funAsync.Res)
            self.assertIs(rpc_args.exn, iface_cls.funAsync.Exn)
            self.assertIs(rpc_args.res, self.p.Void)
            self.assertIs(rpc_args.exn, self.p.Void)
            return rpc_args.res()

        def rpc_impl_err(
            rpc_args: test__iop.InterfaceA_funErrOnly_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funErrOnly_RPCServer.RpcRes:
            self.assertIsInstance(rpc_args.arg, iface_cls.funErrOnly.Arg)
            self.assertIs(type(rpc_args.arg), iface_cls.funErrOnly.Arg)
            self.assertIs(rpc_args.res, iface_cls.funErrOnly.Res)
            self.assertIs(rpc_args.exn, iface_cls.funErrOnly.Exn)
            self.assertIs(rpc_args.res, self.p.Void)
            return rpc_args.exn(code=42)

        def rpc_impl_err_no_throw(
            rpc_args: test__iop.InterfaceA_funErrOnly_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funErrOnly_RPCServer.RpcRes:
            self.assertIsInstance(rpc_args.arg, iface_cls.funErrOnly.Arg)
            self.assertIs(type(rpc_args.arg), iface_cls.funErrOnly.Arg)
            self.assertIs(rpc_args.res, iface_cls.funErrOnly.Res)
            self.assertIs(rpc_args.exn, iface_cls.funErrOnly.Exn)
            self.assertIs(rpc_args.res, self.p.Void)
            # Exercise the server-side `None` → void result path.
            return None  # pyrefly: ignore[bad-return]

        uri = make_uri()
        server = self.p.channel_server()
        server.on_exception = server_excs.append
        server.test_ModuleA.interfaceA.funA.impl = rpc_impl_a
        server.test_ModuleA.interfaceA.funAsync.impl = rpc_impl_async
        server.test_ModuleA.interfaceA.funErrOnly.impl = rpc_impl_err
        server.listen(uri=uri)

        # Client side: verify the result is an instance of the RPC `Res`.
        client = self.p.connect(uri)
        iface = client.test_ModuleA.interfaceA

        res_a = iface.funA(a=self.p.test.ClassA(field1=10))
        self.assertIsInstance(res_a, iface_cls.funA.Res)

        # `funAsync` has `out null`: the client receives a `Void` instance.
        res_async = iface.funAsync(a=self.p.test.ClassA(field1=10))
        self.assertIsInstance(res_async, self.p.Void)

        # `funAsync` has `out null`: `Res` and `Exn` are the `Void` class.
        self.assertIs(iface_cls.funAsync.Res, self.p.Void)
        self.assertIs(iface_cls.funAsync.Exn, self.p.Void)

        # `funErrOnly` has `out null` and `throw (int code)`: the client
        # receives the exn instance via an `iopy.RpcError`.
        with self.assertRaises(iopy.RpcError) as cm:
            iface.funErrOnly(val=1)
        self.assertEqual(len(cm.exception.args), 1)
        self.assertIsInstance(cm.exception.args[0], iface_cls.funErrOnly.Exn)

        # Same RPC, but the impl returns `None` instead of throwing: the
        # client should get a `Void` instance as a successful response.
        server.test_ModuleA.interfaceA.funErrOnly.impl = rpc_impl_err_no_throw
        res_err = iface.funErrOnly(val=1)
        self.assertIsInstance(res_err, self.p.Void)

        server.stop()

        # Surface any assertion error raised on the server side.
        if server_excs:
            raise server_excs[0]


# }}}
# {{{ IopyVoidTest


@z.ZGroup
class IopyVoidTest(z.TestCase):
    @override
    def setUp(self) -> None:
        self.plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = cast(
            'test_iop_plugin__iop.Plugin', iopy.Plugin(self.plugin_file)
        )
        self.r = self.p.register()

    def test_void_union_json(self) -> None:
        u = self.r.testvoid.VoidUnion(_json='{ a: null }')
        self.assertEqual(str(u), '{ "a": null }\n')
        self.assertIsNone(u.a)
        self.assertFalse(hasattr(u, 'b'))

    def test_void_struct_required_json(self) -> None:
        s = self.r.testvoid.VoidRequired(_json='{a: null}')
        self.assertIsNone(s.a)
        # required void can be omited
        s = self.r.testvoid.VoidRequired(_json='{}')
        self.assertIsNone(s.a)
        # json output
        self.assertEqual(str(s), '{\n}\n')

    def test_void_struct_optional_json(self) -> None:
        # check struct creation from json works correctly with a field
        s = self.r.testvoid.VoidOptional(_json='{ a: null}')
        self.assertIsNone(s.a)
        self.assertEqual(str(s), '{\n    "a": null\n}\n')

        s = self.r.testvoid.VoidOptional(_json='{ }')
        self.assertFalse(hasattr(s, 'a'))
        self.assertEqual(str(s), '{\n}\n')

    def test_void_union(self) -> None:
        # check union creation from args works correctly
        u = self.r.testvoid.VoidUnion(a=None)
        self.assertIsNone(u.a)
        u = self.r.testvoid.VoidUnion(b=1)
        self.assertFalse(hasattr(u, 'a'))

        # check setting to anything but None fails
        msg = r'invalid type: got int \(0\), expected NoneType'
        with self.assertRaisesRegex(iopy.Error, msg):
            u.a = 0
        # try setting other field, then setting back our void field
        u.b = 0
        self.assertEqual(u.b, 0)
        self.assertFalse(hasattr(u, 'a'))
        u.a = None
        self.assertIsNone(u.a)

        msg = r'[Ii]nvalid argument .plumbus.'
        with self.assertRaisesRegex(iopy.Error, msg):
            _ = self.r.testvoid.VoidUnion(  # pyrefly: ignore[no-matching-overload]
                plumbus=666
            )

    def test_void_struct_required(self) -> None:
        # required void arg can be omited
        s = self.r.testvoid.VoidRequired()
        self.assertFalse(hasattr(s, 'a'))

        # required void arg can be passed as None, or any other value
        # in any case, it should not be present in the final object
        s = self.r.testvoid.VoidRequired(a=None)
        self.assertFalse(hasattr(s, 'a'))
        s = self.r.testvoid.VoidRequired(a='plop')
        self.assertFalse(hasattr(s, 'a'))

        # manually setting it with any value works but has no effect
        s.a = None
        self.assertFalse(hasattr(s, 'a'))
        s.a = 'plop'
        self.assertFalse(hasattr(s, 'a'))

        # deleting required field fails
        with self.assertRaises(iopy.Error):
            del s.a

    def test_void_struct_optional(self) -> None:
        # optional void arg can be set
        s = self.r.testvoid.VoidOptional(a=None)
        self.assertIsNone(s.a)

        # optional void arg can be skipped
        s = self.r.testvoid.VoidOptional()
        self.assertFalse(hasattr(s, 'a'))

        # setting to None works
        s.a = None
        self.assertEqual(str(s), '{\n    "a": null\n}\n')
        self.assertIsNone(s.a)

        # check deleting optional field clears it
        del s.a
        self.assertFalse(hasattr(s, 'a'))

        # Setting to anything but None behave the same way
        s.a = 'toto'  # pyrefly: ignore[bad-assignment]
        self.assertEqual(str(s), '{\n    "a": null\n}\n')
        self.assertIsNone(s.a)


# }}}
# {{{ IopyDsoTests


@z.ZGroup
class IopyDsoTests(z.TestCase):
    @override
    def setUp(self) -> None:
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin2.so')
        self.p = cast(
            'test_iop_plugin2__iop.Plugin', iopy.Plugin(plugin_file)
        )
        self.r = self.p.register()

    def test_iopy_load_unload_dso(self) -> None:
        def rpc_impl_fun(
            rpc_args: test_dso__iop.InterfaceTest_fun_RPCServer.RpcArgs,
        ) -> test_dso__iop.InterfaceTest_fun_RPCServer.RpcRes:
            return rpc_args.res(val=21)

        # package test should be loaded but not tst1
        self.assertTrue(
            hasattr(self.p, 'test'), 'package "test" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test'), 'package "test" should be loaded'
        )
        self.assertFalse(
            hasattr(self.p, 'tst1'), 'package "tst1" should not be loaded'
        )
        self.assertFalse(
            hasattr(self.r, 'tst1'), 'package "tst1" should not be loaded'
        )

        # load additional plugin
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin-dso.so')
        self.p.load_dso('plugin', plugin_file)

        # the two packages should be loaded
        self.assertTrue(
            hasattr(self.p, 'test_dso'), 'package "test_dso" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test_dso'), 'package "test_dso" should be loaded'
        )
        self.assertTrue(
            hasattr(self.p, 'test'), 'package "test" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test'), 'package "test" should be loaded'
        )

        # we should be able to query test_dso_ModuleTest.interfaceTest.fun
        uri = make_uri()
        s = self.r.channel_server()
        s.listen(uri=uri)
        s_mod: test_dso__iop.ModuleTest_ModuleServer = s.test_dso_ModuleTest  # pyrefly: ignore[missing-attribute]
        s_mod.interfaceTest.fun.impl = rpc_impl_fun
        c = self.r.connect(uri)
        c_mod: test_dso__iop.ModuleTest_Module = c.test_dso_ModuleTest  # pyrefly: ignore[missing-attribute]
        self.assertEqual(21, c_mod.interfaceTest.fun().val)

        # load the same plugin twice, it should be ok
        self.p.load_dso('plugin_the_return', plugin_file)

        # the two packages and module should still be loaded
        self.assertTrue(
            hasattr(self.p, 'test_dso'), 'package "test_dso" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test_dso'), 'package "test_dso" should be loaded'
        )
        self.assertTrue(
            hasattr(self.p, 'test'), 'package "test" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test'), 'package "test" should be loaded'
        )
        c = self.r.connect(uri)
        c_mod = c.test_dso_ModuleTest  # pyrefly: ignore[missing-attribute]
        self.assertEqual(21, c_mod.interfaceTest.fun().val)

        # unload additional plugin
        self.p.unload_dso('plugin')

        # the two packages and module should still be loaded
        self.assertTrue(
            hasattr(self.p, 'test_dso'), 'package "test_dso" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test_dso'), 'package "test_dso" should be loaded'
        )
        self.assertTrue(
            hasattr(self.p, 'test'), 'package "test" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test'), 'package "test" should be loaded'
        )
        c = self.r.connect(uri)
        c_mod = c.test_dso_ModuleTest  # pyrefly: ignore[missing-attribute]
        self.assertEqual(21, c_mod.interfaceTest.fun().val)

        # unload additional plugin
        self.p.unload_dso('plugin_the_return')

        # package test should still be loaded but not tst1
        self.assertTrue(
            hasattr(self.p, 'test'), 'package "test" should be loaded'
        )
        self.assertTrue(
            hasattr(self.r, 'test'), 'package "test" should be loaded'
        )
        self.assertFalse(
            hasattr(self.p, 'test_dso'),
            'package "test_dso" should not be loaded',
        )
        self.assertFalse(
            hasattr(self.r, 'test_dso'),
            'package "test_dso" should not be loaded',
        )

        # module test_dso_ModuleTest should be deleted
        c = self.r.connect(uri)
        self.assertFalse(
            hasattr(c, 'test_dso_ModuleTest'),
            'module "test_dso_ModuleTest" should not be loaded',
        )

    def test_iopy_iface_in_multiple_modules(self) -> None:
        """
        Test an iface registered through two independent modules.

        `test.dso.InterfaceTest` is referenced by both `ModuleTest` (alias
        `interfaceTest`) and `ModuleTestBis` (alias `interfaceTestBis`),
        with different alias tags. Each module gets its own
        `_InternalIfaceType` instance so that command numbers are unique
        per module. The package-level `interfaces` attribute keeps a
        single class for the iface fullname (the first inserted) and is
        re-pointed or dropped on removal.
        """

        def rpc_impl_fun(
            rpc_args: test_dso__iop.InterfaceTest_fun_RPCServer.RpcArgs,
        ) -> test_dso__iop.InterfaceTest_fun_RPCServer.RpcRes:
            return rpc_args.res(val=21)

        def rpc_impl_fun_bis(
            rpc_args: test_dso__iop.InterfaceTest_fun_RPCServer.RpcArgs,
        ) -> test_dso__iop.InterfaceTest_fun_RPCServer.RpcRes:
            return rpc_args.res(val=42)

        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin-dso.so')
        self.p.load_dso('plugin', plugin_file)

        # Both modules should be exposed by the plugin.
        self.assertTrue(hasattr(self.p.modules, 'test_dso_ModuleTest'))
        self.assertTrue(hasattr(self.p.modules, 'test_dso_ModuleTestBis'))

        # The package-level `interfaces` attribute keeps a single class
        # per iface local name; check it is reachable.
        self.assertTrue(hasattr(self.p.test_dso.interfaces, 'InterfaceTest'))  # pyrefly: ignore[missing-attribute]

        # The two modules must yield distinct interface classes, so that
        # each carries its own alias tag and produces unique command
        # numbers.
        iface_cls = self.p.modules.test_dso_ModuleTest.interfaceTest
        iface_cls_bis = self.p.modules.test_dso_ModuleTestBis.interfaceTestBis
        self.assertIsNot(iface_cls, iface_cls_bis)

        # Independently register a server impl on each module and check
        # that calls are dispatched to the right one. If both modules
        # shared the same command number, the second `impl` assignment
        # would clobber the first and both calls would return 42.
        uri = make_uri()
        s = self.r.channel_server()
        s.listen(uri=uri)
        s_mod: test_dso__iop.ModuleTest_ModuleServer = (
            s.test_dso_ModuleTest  # pyrefly: ignore[missing-attribute]
        )
        s_mod_bis: test_dso__iop.ModuleTestBis_ModuleServer = (
            s.test_dso_ModuleTestBis  # pyrefly: ignore[missing-attribute]
        )
        s_mod.interfaceTest.fun.impl = rpc_impl_fun
        s_mod_bis.interfaceTestBis.fun.impl = rpc_impl_fun_bis

        c = self.r.connect(uri)
        c_mod: test_dso__iop.ModuleTest_Module = (
            c.test_dso_ModuleTest  # pyrefly: ignore[missing-attribute]
        )
        c_mod_bis: test_dso__iop.ModuleTestBis_Module = (
            c.test_dso_ModuleTestBis  # pyrefly: ignore[missing-attribute]
        )
        self.assertEqual(21, c_mod.interfaceTest.fun().val)
        self.assertEqual(42, c_mod_bis.interfaceTestBis.fun().val)

        # Unloading the DSO must drop both modules and the package-level
        # `interfaces.InterfaceTest` attribute.
        self.p.unload_dso('plugin')
        self.assertFalse(hasattr(self.p, 'test_dso'))
        self.assertFalse(hasattr(self.p.modules, 'test_dso_ModuleTest'))
        self.assertFalse(hasattr(self.p.modules, 'test_dso_ModuleTestBis'))


# }}}
# {{{ IopyCompatibilityTests


@z.ZGroup
class IopyCompatibilityTests(z.TestCase):
    """Comaptibility tests with previous versions of IOPy"""

    @override
    def setUp(self) -> None:
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = cast('test_iop_plugin__iop.Plugin', iopy.Plugin(plugin_file))

    def test_iface_name(self) -> None:
        """Test iface __name__ works as a property and as a method"""
        iface = self.p.test.interfaces.InterfaceA
        self.assertEqual(iface.__name__, 'test.InterfaceA')
        self.assertEqual(iface.__name__(), 'test.InterfaceA')  # pyrefly: ignore[not-callable]

        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )
        self.assertEqual(
            c.test_ModuleA.interfaceA.__name__(),  # pyrefly: ignore[missing-attribute]
            'interfaceA',
        )

    def test_package_fullname(self) -> None:
        """Test deprecated package __name__ matches get_iop_name"""
        pkg = self.p.test
        self.assertEqual(pkg.__name__(), pkg.get_iop_name())  # pyrefly: ignore[missing-attribute]

    def test_enum_name_values_ranges(self) -> None:
        """
        Test deprecated EnumBase name/values/ranges match
        get_name/get_values/get_ranges.
        """
        enum_cls = self.p.test.EnumA
        self.assertEqual(enum_cls.name(), enum_cls.get_name())  # pyrefly: ignore[missing-attribute]
        self.assertEqual(enum_cls.values(), enum_cls.get_values())  # pyrefly: ignore[missing-attribute]
        self.assertEqual(enum_cls.ranges(), enum_cls.get_ranges())  # pyrefly: ignore[missing-attribute]

    def test_iface_fullname(self) -> None:
        """Test deprecated iface __fullname__ matches get_iop_fullname"""
        iface = self.p.test.interfaces.InterfaceA
        self.assertEqual(iface.__fullname__(), iface.get_iop_fullname())  # pyrefly: ignore[missing-attribute]

        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )
        self.assertEqual(
            c.test_ModuleA.interfaceA.__fullname__(),  # pyrefly: ignore[missing-attribute]
            c.test_ModuleA.interfaceA.get_iop_fullname(),
        )

    def test_module_fullname(self) -> None:
        """Test deprecated module __fullname__ matches get_iop_fullname"""
        c = cast(
            'test_iop_plugin__iop.Channel', iopy.Channel(self.p, make_uri())
        )
        self.assertEqual(
            c.test_ModuleA.__fullname__(),  # pyrefly: ignore[missing-attribute]
            c.test_ModuleA.get_iop_fullname(),
        )
        self.assertEqual(
            self.p.modules.test_ModuleA.__fullname__(),  # pyrefly: ignore[missing-attribute]
            self.p.modules.test_ModuleA.get_iop_fullname(),
        )

    def test_plugin_get_plugin(self) -> None:
        """Test deprecated plugin _get_plugin returns the plugin itself"""
        self.assertIs(
            self.p._get_plugin(),  # noqa: SLF001 (private-member-access)  # pyrefly: ignore[missing-attribute]
            self.p,
        )

    def test_plugin_channel_server(self) -> None:
        """Test deprecated plugin ChannelServer creates a channel server"""
        server = self.p.ChannelServer()  # pyrefly: ignore[missing-attribute]
        self.assertIsInstance(server, iopy.ChannelServer)

    def test_iface_types(self) -> None:
        """
        Test interfaces are child types of iopy.IfaceBase and iopy.Iface.
        """
        iface = self.p.test.interfaces.InterfaceA
        self.assertTrue(issubclass(iface, iopy.IfaceBase))
        self.assertTrue(issubclass(iface, iopy.Iface))

    def test_init_with_type_argument(self) -> None:
        """
        Test we can init types with field set up to another type.

        This keeps some compatibility with IOPyV1 where types where
        instances and not real python types.
        """
        a = self.p.test.StructA(  # pyrefly: ignore[no-matching-overload]
            a=self.p.test.ClassA
        )
        self.assertEqual(a.a.field1, 0)

    def test_invalid_int_no_exceptions(self) -> None:
        """Test setting an invalid int does not raise an exception"""
        v = (1 << 64) - 3
        d = self.p.test.StructD(i=v)
        self.assertEqual(d.i, v)
        self.assertIsNotNone(str(d))

    def test_enum_field_cast_not_done_at_init(self) -> None:
        """
        Test enum cast from string an int is not done directly when setting
        the field, but when the conversion to C is done
        """
        a1 = self.p.test.StructA(e='A')
        self.assertEqual(a1.e, 'A')
        a2 = self.p.test.StructA(e=1)
        self.assertEqual(a2.e, 1)

        self.assertEqual(a1, a2)

        a3 = self.p.test.StructA(_json=str(a1))
        self.assertEqual(a3.e, self.p.test.EnumA('A'))
        self.assertEqual(a3, a1)
        self.assertEqual(a3, a2)

    def test_type_vars(self) -> None:
        """
        Test vars keys for iopy types.

        Optional fields are skipped.
        """
        class_b_keys = {'field1', 'field2'}
        self.assertEqual(set(vars(self.p.test.ClassB).keys()), class_b_keys)

        struct_b_keys = {'a', 'b', 'tab'}
        self.assertEqual(set(vars(self.p.test.StructB).keys()), struct_b_keys)

        union_a_keys = {'i', 'a', 'tab', 's', 'd'}
        self.assertEqual(set(vars(self.p.test.UnionA).keys()), union_a_keys)

    def test_deprecated_underscore_methods(self) -> None:
        """
        Test deprecated underscore methods of the different classes are
        equal to the new methods.
        """

        def check_method(
            obj: Any,
            methods: list[
                tuple[str, str]
                | tuple[str, str, tuple[Any, ...]]
                | tuple[str, str, tuple[Any, ...], dict[str, Any]]
            ],
        ) -> None:
            for method in methods:
                old_method_name = method[0]
                new_method_name = method[1]
                try:
                    args = method[2]  # pyrefly: ignore[bad-index]
                except IndexError:
                    args = ()

                kwargs: dict[str, Any]
                try:
                    kwargs = method[3]  # pyrefly: ignore[bad-index]
                except IndexError:
                    kwargs = {}
                old_res = getattr(obj, old_method_name)(*args, **kwargs)
                new_res = getattr(obj, new_method_name)(*args, **kwargs)
                self.assertEqual(old_res, new_res)

        # EnumBase
        enum_a = self.p.test.EnumA('A')
        check_method(
            enum_a,
            [
                ('__values__', 'get_values'),
                ('__ranges__', 'get_ranges'),
            ],
        )

        # StructUnionBase
        struct_a = self.p.test.StructA(e='A')
        check_method(
            struct_a,
            [
                ('__json__', 'to_json'),
                ('__yaml__', 'to_yaml'),
                ('__bin__', 'to_bin'),
                ('__hex__', 'to_hex'),
                ('__xml__', 'to_xml'),
            ],
        )

        path = os.path.join(TEST_PATH, 'test_class_b.json')
        check_method(
            self.p.test.ClassB,
            [
                ('__from_file__', 'from_file', (), {'_json': path}),
                ('__get_fields_name__', 'get_fields_name'),
                ('__desc__', 'get_desc'),
                ('__values__', 'get_values'),
            ],
        )

        # UnionBase
        union_a = self.p.test.UnionA(i=1)
        check_method(
            union_a,
            [
                ('__object__', 'get_object'),
                ('__key__', 'get_key'),
            ],
        )

        # StructBase
        check_method(
            self.p.test.StructA,
            [
                ('__iopslots__', 'get_iopslots'),
                ('__get_class_attrs__', 'get_class_attrs'),
            ],
        )

        # Plugin
        check_method(
            self.p,
            [
                (
                    '__get_type_from_fullname__',
                    'get_type_from_fullname',
                    ('test.ClassB',),
                ),
                (
                    '__get_iface_type_from_fullname__',
                    'get_iface_type_from_fullname',
                    ('test.InterfaceA',),
                ),
            ],
        )
        self.assertEqual(self.p.__dsopath__, self.p.dsopath)  # pyrefly: ignore[missing-attribute]

        self.assertIs(
            self.p.modules.test_ModuleA,
            self.p.__modules__['test.ModuleA'],  # pyrefly: ignore[missing-attribute]
        )


# }}}
# {{{ IopyAsyncTests


@z.ZGroup
class IopyAsyncTests(z.TestCase):
    """Tests with asynchronous connections and queries"""

    @override
    def setUp(self) -> None:
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = cast('test_iop_plugin__iop.Plugin', iopy.Plugin(plugin_file))

        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)

        self.uri = make_uri()

        shdr = self.p.ic.SimpleHdr(login='root', password='1234')
        self.hdr = self.p.ic.Hdr(simple=shdr)

        def check_hdr(rpc_args: iopy.RPCArgs[Any, Any, Any]) -> None:
            assert rpc_args.hdr and rpc_args.hdr.simple
            assert rpc_args.hdr.simple.login == self.hdr.simple.login
            assert rpc_args.hdr.simple.password == self.hdr.simple.password

        def rpc_impl_b(
            rpc_args: test__iop.InterfaceA_funB_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funB_RPCServer.RpcRes:
            check_hdr(rpc_args)
            return rpc_args.res(status='B', res=0)

        self.async_done = False

        def rpc_impl_async(
            rpc_args: test__iop.InterfaceA_funAsync_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funAsync_RPCServer.RpcRes:
            check_hdr(rpc_args)
            self.async_done = True
            return rpc_args.res()

        self.server = self.p.channel_server()
        self.server.test_ModuleA.interfaceA.funB.impl = rpc_impl_b
        self.server.test_ModuleA.interfaceA.funAsync.impl = rpc_impl_async
        self.server.listen(uri=self.uri)

    @override
    def tearDown(self) -> None:
        self.loop.close()

    def test_async_connection(self) -> None:
        """Test asynchronous connection"""
        # Make the client
        client = iopy.AsyncChannel(self.p, self.uri)

        # Connect the client
        self.loop.run_until_complete(client.connect())
        self.assertTrue(client.is_connected())

        # Make and connect the client through the plugin
        client = self.loop.run_until_complete(self.p.async_connect(self.uri))
        self.assertTrue(client.is_connected())

    def test_async_call_rpc(self) -> None:
        """Test asynchronous RPC calls"""
        obj_a = self.p.test.ClassA()

        # Connect the client
        client = self.loop.run_until_complete(self.p.async_connect(self.uri))
        iface = client.test_ModuleA.interfaceA

        # Make a RPC call with results
        ret = self.loop.run_until_complete(iface.funB(a=obj_a, _hdr=self.hdr))
        exp = type(ret)(
            {
                'status': 'B',
                'res': 0,
            }
        )
        self.assertEqual(
            ret, exp, f'rpc failed; status: {ret}, expected: {exp}'
        )

        # Make a RPC call with async RPC
        self.async_done = False
        ret_void = self.loop.run_until_complete(
            iface.funAsync(a=obj_a, _hdr=self.hdr)
        )
        self.assertIsInstance(ret_void, self.p.Void)
        for _ in range(10):
            if self.async_done:
                break
            time.sleep(0.01)
        else:
            self.fail('expected async RPC implementation to be done')

        # Make a RPC call without connect first
        client = cast(
            'test_iop_plugin__iop.AsyncChannel',
            iopy.AsyncChannel(self.p, self.uri),
        )
        iface = client.test_ModuleA.interfaceA
        ret = self.loop.run_until_complete(iface.funB(a=obj_a, _hdr=self.hdr))
        exp = type(ret)(
            {
                'status': 'B',
                'res': 0,
            }
        )
        self.assertEqual(
            ret, exp, f'rpc failed; status: {ret}, expected: {exp}'
        )


# }}}
# {{{ IopyIopEnvironmentTests


@z.ZGroup
class IopyIopEnvironmentTests(z.TestCase):
    """Tests with IOP envinroment"""

    def test_iop_env_isolation(self) -> None:
        """Test IOP environment isolation"""
        # Create plugin from test-iop-plugin.so
        test_iop_plugin = cast(
            'test_iop_plugin__iop.Plugin',
            iopy.Plugin(
                os.path.join(TEST_PATH, 'test-iop-plugin.so'),
            ),
        )

        # Create plugin from test-iop-plugin2.so
        test_iop_plugin2 = cast(
            'test_iop_plugin2__iop.Plugin',
            iopy.Plugin(
                os.path.join(TEST_PATH, 'test-iop-plugin2.so'),
            ),
        )

        # The two plugins should be different
        self.assertNotEqual(test_iop_plugin, test_iop_plugin2)

        # The objects created from these plugins should be different
        obj1_sa = test_iop_plugin.test.StructA()
        obj2_sa = test_iop_plugin2.test.StructA()
        self.assertNotEqual(obj1_sa, obj2_sa)

        # Even from JSON, the objects created from these plugins should be
        # different
        json_str = '{"_class": "test.ClassB"}'
        obj1_ca = test_iop_plugin.test.ClassA(_json=json_str)
        obj2_ca = test_iop_plugin2.test.ClassA(_json=json_str)
        self.assertNotEqual(obj1_ca, obj2_ca)

        # Try to mixing objects from two different IOP plugins.
        msg = (
            r'argument type `test.ClassA` comes from plugin '
            r'`.*test-iop-plugin2.so` \(0x[0-9a-f]+\), while expecting type '
            r'`test.ClassA` from plugin `.*test-iop-plugin.so` '
            r'\(0x[0-9a-f]+\)'
        )
        with self.assertRaisesRegex(iopy.Error, msg):
            _ = test_iop_plugin.test.StructA(a=test_iop_plugin2.test.ClassA())

        # It should not be possible to open 'test-iop-plugin-dso.so' as a
        # plugin because it can only be opened as an additional DSO
        msg = 'undefined symbol'
        with self.assertRaisesRegex(iopy.Error, msg):
            iopy.Plugin(
                os.path.join(TEST_PATH, 'test-iop-plugin-dso.so'),
            )

    def test_additional_dso(self) -> None:
        # Create plugin from test-iop-plugin.so
        plugin = cast(
            'test_iop_plugin__iop.Plugin',
            iopy.Plugin(
                os.path.join(TEST_PATH, 'test-iop-plugin.so'),
            ),
        )

        # Cannot create ClassDso as the additional DSO is not already loaded
        msg = "object has no attribute 'test_dso'"
        with self.assertRaisesRegex(AttributeError, msg):
            plugin.test_dso.ClassDso()  # pyrefly: ignore[missing-attribute]

        # Load the additional DSO
        plugin.load_dso(
            'plugin-dso',
            os.path.join(TEST_PATH, 'test-iop-plugin-dso.so'),
        )

        # We can now create ClassDso objects
        _ = plugin.test_dso.ClassDso()  # pyrefly: ignore[missing-attribute]

        # Also in JSON
        json_str = '{"_class": "test.dso.ClassDso"}'
        _ = plugin.test.ClassA(_json=json_str)

        # Unload the additional DSO
        plugin.unload_dso('plugin-dso')

        # Cannot create ClassDso anymore
        msg = "object has no attribute 'test_dso'"
        with self.assertRaisesRegex(AttributeError, msg):
            plugin.test_dso.ClassDso()  # pyrefly: ignore[missing-attribute]

        # Also in JSON
        msg = 'expected a child of `test.ClassA\', got `"test.dso.ClassDso"\''
        with self.assertRaisesRegex(iopy.Error, msg):
            json_str = '{"_class": "test_dso.ClassDso"}'
            plugin.test.ClassA(_json=json_str)


# }}}
# {{{ IopyIopTypingTests


@z.ZGroup
class IopyIopStubsTests(z.TestCase):
    """Tests with typing with IOP stubs"""

    @override
    def setUp(self) -> None:
        # Create plugin from test-iop-plugin.so without stubs typing
        self.plugin_no_stub = iopy.Plugin(
            os.path.join(TEST_PATH, 'test-iop-plugin.so'),
        )

        # Type the plugin with the stubs
        self.plugin_stub = cast(
            'test_iop_plugin__iop.Plugin', self.plugin_no_stub
        )

    def test_object_typing_without_stub(self) -> None:
        """Test IOP object typing without stub typing"""
        # Without stubs the plugin is typed as the bare `iopy.Plugin`,
        # which no longer exposes a catch-all `__getattr__`, so every
        # dynamic access is an error for the type checker even though it
        # works at runtime. Once the first hop is ignored the rest of the
        # chain is `Any`, so no further suppression is needed.
        with self.assertRaises(AttributeError):
            _ = self.plugin_no_stub.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = self.plugin_no_stub.test.invalid_attr  # pyrefly: ignore[missing-attribute]

        cls_a = self.plugin_no_stub.test.ClassA(field1=10)  # pyrefly: ignore[missing-attribute]
        self.assertEqual(cls_a.field1, 10)
        with self.assertRaises(AttributeError):
            _ = cls_a.invalid_attr
        cls_a.field1 = 20
        cls_a.custom_attr = 30

        union_a = self.plugin_no_stub.test.UnionA(s='plop')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(union_a.s, 'plop')
        with self.assertRaises(AttributeError):
            _ = union_a.invalid_attr
        union_a.s = 'plop'
        union_a.custom_attr = 40

        enum_a = self.plugin_no_stub.test.EnumA('A')  # pyrefly: ignore[missing-attribute]
        self.assertEqual(enum_a.get_as_str(), 'A')
        with self.assertRaises(AttributeError):
            _ = enum_a.invalid_attr
        enum_a.custom_attr = 50

    def test_object_typing_with_stub(self) -> None:
        """Test IOP object typing with stub typing"""
        with self.assertRaises(AttributeError):
            _ = self.plugin_stub.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = self.plugin_stub.test.invalid_attr  # pyrefly: ignore[missing-attribute]

        cls_a = self.plugin_stub.test.ClassA(field1=10)
        self.assertEqual(cls_a.field1, 10)
        with self.assertRaises(AttributeError):
            _ = cls_a.invalid_attr  # pyrefly: ignore[missing-attribute]
        cls_a.field1 = 20
        cls_a.custom_attr = 30  # pyrefly: ignore[missing-attribute]

        union_a = self.plugin_stub.test.UnionA(s='plop')
        self.assertEqual(union_a.s, 'plop')
        with self.assertRaises(AttributeError):
            _ = union_a.invalid_attr  # pyrefly: ignore[missing-attribute]
        union_a.s = 'plop'
        union_a.custom_attr = 40  # pyrefly: ignore[missing-attribute]

        enum_a = self.plugin_stub.test.EnumA('A')
        self.assertEqual(enum_a.get_as_str(), 'A')
        with self.assertRaises(AttributeError):
            _ = enum_a.invalid_attr  # pyrefly: ignore[missing-attribute]
        enum_a.custom_attr = 50  # pyrefly: ignore[missing-attribute]

    def test_rpc_typing_without_stub(self) -> None:
        """Test IOP RPC typing with and without stub typing"""

        # Server
        def rpc_impl_a(
            rpc_args: test__iop.InterfaceA_funA_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
            assert issubclass(rpc_args.res, iopy.StructUnionBase)
            return rpc_args.res(status='A', res=1000)

        uri = make_uri()
        server = self.plugin_no_stub.channel_server()
        server.test_ModuleA.interfaceA.funA.impl = rpc_impl_a  # pyrefly: ignore[missing-attribute]
        server.listen(uri=uri)

        # Client
        client = self.plugin_no_stub.connect(uri)
        res = client.test_ModuleA.interfaceA.funA(  # pyrefly: ignore[missing-attribute]
            a=self.plugin_no_stub.test.ClassA(field1=10)  # pyrefly: ignore[missing-attribute]
        )
        assert res is not None
        self.assertEqual(res.status, 'A')

        # Test invalid attributes. Without stubs `server`/`client` are the
        # bare `iopy.ChannelServer`/`iopy.Channel`, which no longer expose a
        # catch-all `__getattr__`, so every dynamic access is a type error.
        with self.assertRaises(AttributeError):
            _ = server.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = server.test_ModuleA.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = server.test_ModuleA.funA.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = client.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = client.test_ModuleA.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = client.test_ModuleA.funA.invalid_attr  # pyrefly: ignore[missing-attribute]

        server.stop()

    def test_rpc_typing_with_stub(self) -> None:
        """Test IOP RPC typing with stub typing"""

        # Server
        def rpc_impl_a(
            rpc_args: test__iop.InterfaceA_funA_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funA_RPCServer.RpcRes:
            return rpc_args.res(status='A', res=1000)

        uri = make_uri()
        server = self.plugin_stub.channel_server()
        server.test_ModuleA.interfaceA.funA.impl = rpc_impl_a
        server.listen(uri=uri)

        # Client
        client = self.plugin_stub.connect(uri)
        res = client.test_ModuleA.interfaceA.funA(
            a=self.plugin_stub.test.ClassA(field1=10)
        )
        self.assertEqual(res.status, 'A')

        # Test invalid attributes
        with self.assertRaises(AttributeError):
            _ = server.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = server.test_ModuleA.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = server.test_ModuleA.funA.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = client.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = client.test_ModuleA.invalid_attr  # pyrefly: ignore[missing-attribute]
        with self.assertRaises(AttributeError):
            _ = client.test_ModuleA.funA.invalid_attr  # pyrefly: ignore[missing-attribute]

        server.stop()

    def test_interface_query_typing(self) -> None:
        _p = ParamSpec('_p')
        _TRpcRes = TypeVar('_TRpcRes')

        # -- Test with RPC call via a channel

        def call_rpc(
            rpc: Callable[_p, _TRpcRes],
            *args: _p.args,
            **kwargs: _p.kwargs,
        ) -> tuple[int, _TRpcRes]:
            rpc_inst = rpc
            assert isinstance(rpc_inst, iopy.RPCBase)
            return rpc_inst.get_cmd(), rpc(*args, **kwargs)

        # Server
        def rpc_impl_b(
            rpc_args: test__iop.InterfaceA_funB_RPCServer.RpcArgs,
        ) -> test__iop.InterfaceA_funB_RPCServer.RpcRes:
            return rpc_args.res(
                status='A', res=1000, strField=rpc_args.hdr.simple.login
            )

        uri = make_uri()
        server = self.plugin_stub.channel_server()
        server.test_ModuleA.interfaceA.funB.impl = rpc_impl_b
        server.listen(uri=uri)

        # Client
        client = self.plugin_stub.connect(uri)

        # Tests
        def _test_with_client(
            cb: Callable[[], tuple[int, test__iop.InterfaceA_funB_Res]],
        ) -> None:
            res_call = cb()
            fun_b = self.plugin_stub.test.interfaces.InterfaceA.funB
            exp_rpc_res_impl_b = fun_b.Res(
                status='A', res=1000, strField='plop'
            )
            self.assertEqual(res_call, (65538, exp_rpc_res_impl_b))

        # => With only kwargs
        def _with_only_kwargs() -> tuple[int, test__iop.InterfaceA_funB_Res]:
            return call_rpc(
                client.test_ModuleA.interfaceA.funB,
                a=self.plugin_stub.test.ClassA(field1=10),
                _login='plop',
            )

        _test_with_client(_with_only_kwargs)

        # => With wrong kwargs
        def _with_wrong_kwargs() -> tuple[int, test__iop.InterfaceA_funB_Res]:
            with self.assertRaises(iopy.Error):
                # FIXME: pyrefly should not accept this case
                call_rpc(
                    client.test_ModuleA.interfaceA.funB,
                    a=self.plugin_stub.test.ClassA(field1=10),
                    _login='plop',
                    _login2='wrong_plop',
                )

            return call_rpc(
                client.test_ModuleA.interfaceA.funB,
                a=self.plugin_stub.test.ClassA(field1=10),
                _login='plop',
            )

        _test_with_client(_with_wrong_kwargs)

        # => With dict and IC kwargs
        def _with_dict_ic_kwargs() -> tuple[
            int, test__iop.InterfaceA_funB_Res
        ]:
            # FIXME: pyrefly should accept this case
            return call_rpc(
                client.test_ModuleA.interfaceA.funB,
                {
                    'a': {'field1': 10},
                },
                _login='plop',
            )

        _test_with_client(_with_dict_ic_kwargs)

        # => With object and IC kwargs as dict
        def _with_dict_ic_kwargs_dict() -> tuple[
            int, test__iop.InterfaceA_funB_Res
        ]:
            ic_kwargs_dict = {
                '_login': 'plop',
            }
            # FIXME: pyrefly should accept this case
            return call_rpc(
                client.test_ModuleA.interfaceA.funB,
                {
                    'a': {'field1': 10},
                },
                **ic_kwargs_dict,
            )

        _test_with_client(_with_dict_ic_kwargs_dict)

        # => With dict and wrong IC kwargs
        def _with_dict_wrong_ic_kwargs() -> tuple[
            int, test__iop.InterfaceA_funB_Res
        ]:
            # FIXME: pyrefly should not accept this case (and the
            # runtime is a bit broken too)
            return call_rpc(
                client.test_ModuleA.interfaceA.funB,
                {
                    'a': {'field1': 10},
                },
                _login='plop',
                _login2='wrong_plop',
            )

        _test_with_client(_with_dict_wrong_ic_kwargs)

        # => With object and wrong IC kwargs as dict
        def _with_dict_wrong_ic_kwargs_dict() -> tuple[
            int, test__iop.InterfaceA_funB_Res
        ]:
            ic_kwargs_dict = {
                '_login': 'plop',
                '_login2': 'wrong_plop',
            }

            # FIXME: pyrefly should not accept this case (and the
            # runtime is a bit broken too)
            return call_rpc(
                client.test_ModuleA.interfaceA.funB,
                {
                    'a': {'field1': 10},
                },
                **ic_kwargs_dict,
            )

        _test_with_client(_with_dict_wrong_ic_kwargs_dict)

        # -- Test without RPC call with just the module

        def no_call(
            rpc: Callable[_p, _TRpcRes],
            *args: _p.args,
            **kwargs: _p.kwargs,
        ) -> tuple[int, _TRpcRes]:
            rpc_inst = rpc
            assert isinstance(rpc_inst, iopy.RPCBase)
            return rpc_inst.get_cmd(), rpc_inst.Res()

        # - funAsync
        res_no_call_async = no_call(
            self.plugin_stub.modules.test_ModuleA.interfaceA.funAsync,
            a=self.plugin_stub.test.ClassA(field1=10),
        )
        # `funAsync` has `out null`: `Res` is the `Void` class, so `Res()`
        # yields a `Void` instance.
        self.assertEqual(res_no_call_async, (65539, self.plugin_stub.Void()))

        # - funToggleVoid
        res_no_call_fun_toggle_void = no_call(
            self.plugin_stub.modules.test_ModuleA.interfaceA.funToggleVoid,
        )
        fun_toggle_void = (
            self.plugin_stub.test.interfaces.InterfaceA.funToggleVoid
        )
        exp_rpc_res_toggle_void = fun_toggle_void.Res()
        self.assertEqual(
            res_no_call_fun_toggle_void, (65540, exp_rpc_res_toggle_void)
        )

    def test_submodule_inheritance_typing(self) -> None:
        """
        A submodule of `test.ModuleA` defined in a separate package
        (`test.submodule.ModuleSubA`) must, in its generated pystub,
        properly type the inherited `interfaceA` field as
        `test.InterfaceA`.

        The only foreign dependency of `test.submodule` is via the
        module inheritance, so if the pystub generator omits
        `import test__iop`, the inherited field falls back to `Any`
        for static type checkers. `assert_type` asserts that the
        static type is the expected one.
        """
        sub_mod = self.plugin_stub.modules.test_submodule_ModuleSubA
        assert_type(sub_mod.interfaceA, 'test__iop.InterfaceA_Iface')


# }}}
# {{{ IopySlowTests


@z.ZFlags('slow')
@z.ZGroup
class IopySlowTests(z.TestCase):
    """Tests that takes some fixed time to complete"""

    @override
    def setUp(self) -> None:
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = cast('test_iop_plugin__iop.Plugin', iopy.Plugin(plugin_file))

    def test_connection_timeout(self) -> None:
        """Test the timeout argument is well respected on connection"""
        uri = make_uri()

        # Use a fake TCP server that never accepts connections to trigger
        # connection timeout
        with z_iopy_use_fake_tcp_server(uri):
            # Do it 5 times (5s) to make sure a success is not random
            for _ in range(5):
                start_time = time.time()
                try:
                    # Make a connection that should timeout in 1s
                    self.p.connect(uri, connect_timeout=1)
                except iopy.Error:
                    pass
                else:
                    self.fail('expected connection timeout error')
                end_time = time.time()
                diff_time = end_time - start_time
                # We should have a timeout in less than 1.5s
                self.assertLessEqual(
                    diff_time,
                    1.5,
                    f'connection timeout took {diff_time:.2f}s, '
                    'expected less than 1.5s',
                )

    def test_non_deadlock_on_exit(self) -> None:
        """
        Test IOPy does not deadlock on process exit while waiting for
        connection in a thread
        """
        uri = make_uri()

        # Use a queue to indicate startup
        queue: multiprocessing.Queue[None] = multiprocessing.Queue()

        def thread_cb(client: iopy.Channel) -> None:
            queue.put(None)
            try:
                client.connect(timeout=2)
            except (KeyboardInterrupt, iopy.Error):
                pass

        def process_cb() -> None:
            client = iopy.Channel(self.p, uri)
            thread1 = threading.Thread(target=thread_cb, args=(client,))
            thread2 = threading.Thread(target=thread_cb, args=(client,))

            thread1.start()
            thread2.start()

            thread1.join()
            thread2.join()

        # Use a fake TCP server that never accepts connections to wait for
        # connection forever
        with z_iopy_use_fake_tcp_server(uri):
            # Fork to a new process
            child_pid = os.fork()
            assert child_pid >= 0
            if child_pid == 0:
                process_cb()
                os._exit(0)

            # Wait for the two threads to be started
            queue.get()
            queue.get()

            start_time = time.time()

            # Terminate process
            os.kill(child_pid, signal.SIGTERM)
            _, code = os.waitpid(child_pid, 0)
            self.assertEqual(code, 0)

            # We should have a timeout in less than 1.0s
            end_time = time.time()
            diff_time = end_time - start_time
            self.assertLessEqual(
                diff_time,
                1.0,
                f'exit timeout took {diff_time:.2f}s, expected less '
                'than 1.0s',
            )

    def test_async_connection_timeout(self) -> None:
        """Test the timeout argument is well respected on async connection"""
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)

        uri = make_uri()

        client = iopy.AsyncChannel(self.p, uri)
        with z_iopy_use_fake_tcp_server(uri):
            start_time = time.time()

            # Start the connection
            future = client.connect(timeout=1)

            # Wait 0.6 seconds, the connection should not be etablished and
            # not yet timeout.
            loop.run_until_complete(asyncio.sleep(0.6))
            self.assertFalse(future.done())
            self.assertFalse(client.is_connected())

            # Wait for timeout
            try:
                loop.run_until_complete(future)
            except iopy.Error:
                pass
            else:
                self.fail('expected connection timeout error')

            end_time = time.time()
            diff_time = end_time - start_time

            # We should have a timeout in less than 1.5s
            self.assertLessEqual(
                diff_time,
                1.5,
                f'connection timeout took {diff_time:.2f}s, '
                'expected less than 1.5s',
            )
        loop.close()


# }}}


if __name__ == '__main__':
    z.main()
