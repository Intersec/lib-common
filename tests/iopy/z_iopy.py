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

import os
import sys
import time
import copy
import warnings
import subprocess
import threading

from six.moves import xrange

SELF_PATH = os.path.dirname(__file__)
TEST_PATH = os.path.join(SELF_PATH, 'testsuite')

import zpycore as z
import iopy
import z_compatibility


PORT_COUNT = 9900
def make_uri():
    global PORT_COUNT
    PORT_COUNT -= 1
    return "127.0.0.1:%d" % PORT_COUNT


def z_iopy_thread_cb(iface, obj_a, res_list):
    try:
        res = iface.funA(a=obj_a)
    except Exception as e: # pylint: disable=broad-except
        res = e
    res_list.append(res)


def z_iopy_fork_child(iface, obj_a, exp_res, do_threads):
    # pylint: disable=protected-access

    try:
        res = iface.funA(a=obj_a)
    except Exception as e: # pylint: disable=broad-except
        sys.stderr.write('{0}\n'.format(str(e)))
        os._exit(1)

    if res != exp_res:
        sys.stderr.write('unexpected result for interfaceA.funA, expected '
                         '{0}, got {1}\n'.format(exp_res, res))
        os._exit(2)

    try:
        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, do_threads,
                                      False)
    except Exception as e: # pylint: disable=broad-except
        sys.stderr.write('{0}\n'.format(str(e)))
        os._exit(3)

    os._exit(0)


def z_iopy_test_threads_and_forks(iface, obj_a, exp_res, do_threads,
                                  do_forks):
    res_list = []
    threads = []
    pids = []
    res_codes = []

    if do_threads:
        for _ in xrange(10):
            t = threading.Thread(target=z_iopy_thread_cb,
                                 args=(iface, obj_a, res_list))
            t.start()
            threads.append(t)

    if do_forks:
        for _ in xrange(10):
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
        'threads results are not all valid: {0}'.format(res_list)
    )

    assert all(x == 0 for x in res_codes) , (
        "child processes don't all exit successfully: {0}".format(res_codes)
    )


@z.ZGroup
class IopyTest(z.TestCase):
    def setUp(self):
        self.plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = iopy.Plugin(self.plugin_file)
        self.r = self.p.register()

    def test_type_name_parsing(self):
        a = self.r.tst1.A(_json='{ "a": "A1", "b": "B2" }')
        self.assertEqual(a, self.r.tst1.A(a='A1', b='B2'))

    def test_inheritance(self):
        self.assertTrue(issubclass(self.r.test.ClassB, self.r.test.ClassA),
                        "class inheritance failed")

    def test_fields(self):
        a = self.r.test.ClassA()
        a.a = 'a'
        self.assertEqual(a.field1, 0)
        self.assertEqual(a.a, 'a', "append field failed")

    def test_from_file_json(self):
        path = os.path.join(TEST_PATH, 'test_class_b.json')
        b = self.r.test.ClassB.from_file(_json=path)
        self.assertEqual(b.field1, 42)
        self.assertEqual(b.field2, 10)
        self.assertEqual(b.optField, 20)
        b2 = self.r.test.ClassB.__from_file__(_json=path)
        self.assertEqual(b, b2)

    def test_from_file_yaml(self):
        path = os.path.join(TEST_PATH, 'test_class_b.yaml')
        b = self.r.test.ClassB.from_file(_yaml=path)
        self.assertEqual(b.field1, 9)
        self.assertEqual(b.field2, 8)
        self.assertEqual(b.optField, 7)
        b2 = self.r.test.ClassB.__from_file__(_yaml=path)
        self.assertEqual(b, b2)

    def test_from_str_yaml(self):
        path = os.path.join(TEST_PATH, 'test_class_b.yaml')
        b = self.r.test.ClassB.from_file(_yaml=path)
        with open(path) as f:
            b2 = self.r.test.ClassB(_yaml=f.read())
            self.assertEqual(b, b2)

    def test_from_file_xml(self):
        path = os.path.join(TEST_PATH, 'test_class_b.xml')
        b = self.r.test.ClassB.from_file(_xml=path)
        self.assertEqual(b.field1, 45)
        self.assertEqual(b.field2, 20)
        self.assertEqual(b.optField, 36)
        b2 = self.r.test.ClassB.__from_file__(_xml=path)
        self.assertEqual(b, b2)

    def test_from_file_hex(self):
        path = os.path.join(TEST_PATH, 'test_class_b.hex')
        b = self.r.test.ClassB.from_file(_hex=path)
        self.assertEqual(b.field1, 1)
        self.assertEqual(b.field2, 2)
        self.assertEqual(b.optField, 3)
        b2 = self.r.test.ClassB.__from_file__(_hex=path)
        self.assertEqual(b, b2)

    def test_from_file_bin(self):
        path = os.path.join(TEST_PATH, 'test_class_b.bin')
        b = self.r.test.ClassB.from_file(_bin=path)
        self.assertEqual(b.field1, 4)
        self.assertEqual(b.field2, 5)
        self.assertEqual(b.optField, 6)
        b2 = self.r.test.ClassB.__from_file__(_bin=path)
        self.assertEqual(b, b2)

    def test__json__(self):
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = '{"a":"plop","b":"plip","tab":["plup"]}'
        self.assertEqual(exp, b.__json__(minimal=True))

    def test__yaml__(self):
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = 'a: plop\nb: plip\ntab:\n  - plup'
        self.assertEqual(exp, b.__yaml__())

    def test__bin__(self):
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = b'\x01\x05plop\x00\x02\x05plip\x00\x03\x05plup\x00'
        self.assertEqual(exp, b.__bin__())

    def test__hex__(self):
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = '0105706c6f70000205706c6970000305706c757000'
        self.assertEqual(exp, b.__hex__())

    def test__xml__(self):
        b = self.p.test.StructB(a='plop', b='plip', tab=['plup'])
        exp = ('<test.StructB><a>plop</a><b>plip</b>'
               '<tab>plup</tab></test.StructB>')
        self.assertEqual(exp, b.__xml__())

    def test_custom_methods(self):
        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_ClassA(object):
            __metaclass__ = self.r.metaclass
            def fun(self):
                return self.field1

        b = self.r.test.ClassB(field1=1)
        self.assertTrue(hasattr(b, 'fun'), "method inheritance failed")
        self.assertEqual(b.fun(), 1, "method inheritance failed")

    def test_subtyping(self):
        u = self.r.test.UnionA(a=self.r.test.ClassB())
        self.assertIsInstance(u.a, self.r.test.ClassB, "subtyping failed")
        self.assertTrue(hasattr(u.a, 'field2'), "subtyping failed")

    def test_rpc_client_server(self):
        # to hide connections / disconnections message
        warnings.filterwarnings("ignore", category=iopy.ServerWarning)

        def rpc_impl_a(rpc_args):
            login = None
            password = None
            if rpc_args.hdr is not None and hasattr(rpc_args.hdr, 'simple'):
                login    = rpc_args.hdr.simple.login
                password = rpc_args.hdr.simple.password
            if login != 'root' or password != '1234':
                desc = 'invalid login, hdr: %s' % repr(rpc_args.hdr)
                return rpc_args.exn(code=1, desc=desc)
            status = self.r.test.EnumA(str(rpc_args.arg.a.__fullname__()[-1]))
            return rpc_args.res(status=status, res=rpc_args.arg.a.field1)

        s = self.r.ChannelServer()

        s.test_ModuleA.interfaceA.funA.impl = rpc_impl_a
        self.assertEqual(s.test_ModuleA.interfaceA.funA.impl, rpc_impl_a)
        self.assertIsNone(s.test_ModuleA.interfaceA.funAsync.impl)

        self.async_done = False
        def rpc_impl_async(rpc_args):
            self.async_done = True
        s.test_ModuleA.interfaceA.funAsync.impl = rpc_impl_async

        self.connections = 0
        def server_on_connect(server, remote_addr):
            self.connections += 1
        def server_on_disconnect(server, remote_addr):
            self.connections -= 1
        s.on_connect = server_on_connect
        s.on_disconnect = server_on_disconnect

        uri = make_uri()
        s.listen(uri=uri)
        c = self.r.connect(uri)

        c.test_ModuleA.interfaceA.funAsync(a=self.r.test.ClassA())

        b = self.r.test.ClassB(field1=1)
        res = c.test_ModuleA.interfaceA.funA(a=b, _login='root',
                                             _password='1234')
        self.assertEqual(res.status.get_as_str(), 'B')
        self.assertEqual(res.res, 1)

        def test_ok(_hdr=None):
            if _hdr:
                res = c.test_ModuleA.interfaceA.funA(a=b, _hdr=_hdr)
            else:
                res = c.test_ModuleA.interfaceA.funA(a=b)
            self.assertEqual(res.status.get_as_str(), 'B')
            self.assertEqual(res.res, 1)

        shdr = self.r.ic.SimpleHdr(login='root', password='1234')
        hdr = self.r.ic.Hdr(simple=shdr)
        test_ok(_hdr=hdr)

        def test_ko():
            try:
                c.test_ModuleA.interfaceA.funA(a=b)
            except iopy.RpcError as e:
                self.assertEqual(len(e.args), 1)
                e = e.args[0]
                self.assertIsInstance(e, c.test_ModuleA.interfaceA.funA.exn())
                self.assertEqual(e.code, 1)
                self.assertTrue('invalid login, hdr:' in e.desc)

        test_ko()

        c.change_default_hdr(_login='root', _password='1234')
        test_ok()

        c.change_default_hdr(_hdr=hdr)
        test_ok()

        res = c.get_default_hdr()
        self.assertTrue(hasattr(res, 'simple'))
        self.assertEqual(res.simple.login, shdr.login)
        self.assertEqual(res.simple.password, shdr.password)

        hdr.simple.password = 'toto'
        c.change_default_hdr(_hdr=hdr)
        test_ko()

        self.assertTrue(self.async_done, "async RPC failed")

        self.assertEqual(self.connections, 1, "on_connect cb failed")

        self.assertEqual(c.is_connected(), True)
        c.disconnect()
        self.assertEqual(c.is_connected(), False)

        for _ in range(0, 100):
            if not self.connections:
                break
            time.sleep(0.01)
        self.assertEqual(self.connections, 0, "on_disconnect cb failed")

        s.stop()
        s.on_connect = None
        s.on_disconnect = None

        p_args = ['python3', os.path.join(SELF_PATH, 'z_iopy_process1.py'),
                  self.plugin_file, uri]
        proc = subprocess.Popen(p_args)
        self.assertIsNotNone(proc)
        s.test_ModuleA.interfaceA.funA.wait(uri=uri, timeout=20)
        proc.wait()
        msg = ("server blocking failed; subprocess status: %s" %
               str(proc.returncode))
        self.assertEqual(proc.returncode, 0, msg)

    def test_objects_comparisons(self):
        u1 = self.r.test.UnionA(a=self.r.test.ClassB(field1=1, field2=2))
        u2 = self.r.test.UnionA(a=self.r.test.ClassB(field1=1, field2=2))
        u3 = self.r.test.UnionA(a=self.r.test.ClassB(field1=1, field2=3))
        u4 = self.r.test.UnionA(a=self.r.test.ClassA(field1=1))
        self.assertTrue(u1 == u1)
        self.assertTrue(u1 == u2)
        self.assertTrue(u1 != u3)
        self.assertTrue(u1 != u4)
        self.assertTrue(u1 != u1.a)

        e1 = self.r.test.EnumA(0)
        e2 = self.r.test.EnumA(0)
        e3 = self.r.test.EnumA(1)
        self.assertTrue(e1 == e1)
        self.assertTrue(e1 <= e1)
        self.assertTrue(e1 >= e1)
        self.assertTrue(e1 == e2)
        self.assertTrue(e1 <= e2)
        self.assertTrue(e1 >= e2)
        self.assertTrue(e1 != e3)
        self.assertTrue(e1 <= e3)
        self.assertTrue(e1 <  e3)
        self.assertTrue(e3 >= e1)
        self.assertTrue(e3 >  e1)

        tab = self.r.test_emptystuffs.Tab( \
            a=[self.r.test_emptystuffs.A(), self.r.test_emptystuffs.B()],
            emptyStructs=[self.r.test_emptystuffs.EmptyStruct()])
        self.assertEqual(tab.a[1], self.r.test_emptystuffs.B(),
                         "empty stuff comparison failed")
        self.assertNotEqual(tab.a[0], self.r.test_emptystuffs.B(),
                            "empty stuff comparison failed")

    def test_packing(self):
        u = self.r.test.UnionA(self.r.test.ClassB(field1=1, field2=2))
        # check union packing after unamed field init
        j = u.__json__()
        self.assertEqual(u, self.r.test.UnionA(_json=j))
        # check union field init from a cast
        a = self.r.test.StructA(u=self.r.test.ClassB(field2=1))
        j = a.__json__()
        self.assertEqual(a, self.r.test.StructA(_json=j))
        # check struct field init from union and packing
        a = self.r.test.StructA(a=u)
        j = a.__json__()
        self.assertEqual(a, self.r.test.StructA(_json=j))
        # check enum field init from cast and packing
        a = self.r.test.StructA(e=0)
        j = a.__json__()
        self.assertEqual(a, self.r.test.StructA(_json=j))
        a = self.r.test.StructA(e='A')
        j = a.__json__()
        self.assertEqual(a, self.r.test.StructA(_json=j))

        # check compact option is working
        j = a.__json__(compact=True)
        self.assertEqual(a, self.r.test.StructA(_json=j))

        # check that private fields are skipped
        c = self.r.test.StructC(u=1, priv = "toto")
        d = self.r.test.StructC(u=1)
        j = c.__json__()
        self.assertEqual(c, self.r.test.StructC(_json=j))

        j = c.__json__(skip_private=True)
        self.assertNotEqual(c, self.r.test.StructC(_json=j))
        self.assertEqual(d, self.r.test.StructC(_json=j))

    def test_unicode(self):
        # unicode string and non unicode strings
        b = self.r.test.StructB(a=z_compatibility.b('string a'),
                                b=z_compatibility.u('string b'),
                                tab=[z_compatibility.b('first string'),
                                     z_compatibility.u('second string')])
        j = b.__json__()
        self.assertEqual(b, self.r.test.StructB(_json=j),
                         "unicode strings in iopy fields failed")
        b2 = self.r.test.StructB(a='string a', b='string b',
                                 tab=['first string', 'second string'])
        self.assertTrue(b == b2, "string fields comparison failed")
        b3 = self.r.test.StructB(a=z_compatibility.u('non asçii éé', 'utf-8'),
                                 b='', tab=[])
        self.assertTrue(b3 == self.r.test.StructB(_json=b3.__json__()),
                        "real unicode fields failed")
        u = self.r.test.UnionA(s=z_compatibility.b('bytes string'))
        j = u.__json__()
        self.assertEqual(u, self.r.test.UnionA(_json=j),
                         "string in iopy union failed")
        u = self.r.test.UnionA(s=z_compatibility.u('unicode string'))
        j = u.__json__()
        self.assertEqual(u, self.r.test.UnionA(_json=j),
                         "unicode string in iopy union failed")
        u = self.r.test.UnionA(s=z_compatibility.b('bytes string'))
        j = u.__json__()
        self.assertEqual(u, self.r.test.UnionA(_json=j),
                         "bytes string in iopy union failed")
        kwargs = {'a': '0', z_compatibility.u('b'): '1', 'tab':[]}
        b = self.p.test.StructB(**kwargs)
        j = b.__json__()
        self.assertEqual(b, self.p.test.StructB(_json=j),
                         "unicode keys in iopy object failed")

    def test_constraints(self):
        self.r.test.UnionA(i=100)
        a = self.r.test.UnionA(100)
        a.i = 1
        exp = r"violation of constraint max \(100\) on field i: val=101$"
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.UnionA(i=101)
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.UnionA(101)
        with self.assertRaisesRegexp(iopy.Error, exp):
            a.i = 101
        self.assertEqual(a.i, 1)

        b = self.r.test.UnionB(a=100)
        self.assertTrue(hasattr(b, 'a'))
        self.assertEqual(getattr(b.a, 'i', None), 100)
        b.a = 1
        exp = (r"^error when parsing test\.UnionB: "
               r"invalid selected union field .+a.+: in a of type "
               r"test\.UnionA: violation of constraint max \(100\) on "
               r"field i: val=101$")
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.UnionB(a=101)
        with self.assertRaisesRegexp(iopy.Error, exp):
            b.a = 101
        self.assertEqual(getattr(b.a, 'i', None), 1)

        b = self.r.test.ClassB(field1=1000)
        b.field1 = 1
        exp = (r"violation of constraint max \(1000\) on field field1: "
               r"val=1001$")
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.ClassB(field1=1001)
        with self.assertRaisesRegexp(iopy.Error, exp):
            b.field1 = 1001
        self.assertEqual(b.field1, 1)

        self.r.test.StructF(s='', i=[0])
        exp = (r"^error when parsing test.StructF: "
               r"field s \(type: ?str\) is required but absent; "
               r"field i \(type: ?long\[\]\) is not allowed: empty array$")
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.StructF()

        a = self.r.test.StructA()
        self.assertEqual(a, self.r.test.StructA(tu=[]))

        a = self.r.test.StructA(tu=[1, self.r.test.ClassB(), ''])
        exp = (r"^error when parsing test.StructA: "
               r"invalid argument .+tu.+: in tu\[1\] of type test.UnionA: "
               r"violation of constraint max \(100\) on field i: val=101$")
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.StructA(tu=[100, 101])

        b = self.r.test.ConstraintsB(name='ab', i=1000)
        exp = (r"^error when parsing test.ConstraintsB: "
               r"invalid argument .+name.+: in type test.ConstraintsA: "
               r"violation of constraint pattern \(\[a-z\]\*\) on field "
               r"name: a b; invalid argument .+i.+: in type "
               r"test.ConstraintsB: violation of constraint max \(1000\) "
               r"on field i: val=1001$")
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.ConstraintsB(name='a b', i=1001)

    def test_unsigned_long_reading(self):
        c = self.r.test.StructC()
        self.assertEqual(c.u, (1 << 64) - 1)

    def test_field_deletion(self):
        a = self.r.test.ClassA(optField=1)
        delattr(a, 'optField')
        self.assertFalse(hasattr(a, 'optField'),
                         'deletion of optional field has failed')

        err = False
        try:
            delattr(a, 'field1')
        except iopy.Error:
            err = True
        self.assertTrue(hasattr(a, 'field1') and err,
                        'check of deletion of mandatory field has failed')

        u = self.r.test.UnionA(i=0)
        err = False
        try:
            delattr(u, 'i')
        except iopy.Error:
            err = True
        self.assertTrue(hasattr(u, 'i') and err,
                        'check of deletion of union field has failed')

    def test_required_fields(self):
        exp = (r"^error when parsing test.StructB: field a \(type: ?str\) is "
               r"required but absent$")
        with self.assertRaisesRegexp(iopy.Error, exp):
            self.r.test.StructB(b='')

    def test_unspecified_optional_fields(self):
        d = self.r.test.StructD()
        self.assertEqual(d, self.r.test.StructD(a=self.r.test.StructA()))
        e = self.r.test.StructE()
        self.assertEqual(e, self.r.test.StructE(d=d))

    def test_custom_init(self):
        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_ClassA(object):
            __metaclass__ = self.r.metaclass
            def __custom_init__(self, field1=10, _my_field='value'):
                self._my_field = _my_field

        a = self.r.test.ClassA(optField=0)
        self.assertEqual(getattr(a, '_my_field', None), 'value',
                         '__custom_init__ method has not been called')
        self.assertEqual(getattr(a, 'field1', None), 10,
                         'custom init of iop field has failed')
        self.assertEqual(getattr(a, 'optField', None), 0,
                         'init of optional iop field has failed')

        b = self.r.test.ClassB()
        self.assertEqual(getattr(b, 'field1', None), 10,
                         'custom init of inherited iop field has failed')

        a = self.r.test.ClassA(field1=11)
        self.assertEqual(getattr(a, 'field1', None), 11,
                         'custom init of iop field has failed')

        a = self.r.test.ClassA(_bin=a.__bin__())
        self.assertEqual(getattr(a, '_my_field', None), 'value',
                         '__custom_init__ method has not been called'
                         ' from iop creation')

        a = self.r.test.ClassA(field1=42, _my_field='test')
        self.assertEqual(getattr(a, 'field1', None), 42,
                         'custom init of iop field has failed')
        self.assertEqual(getattr(a, '_my_field', None), 'test',
                         'custom init of custom value has failed')

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_ClassA(object):
            __metaclass__ = self.r.metaclass
            def __custom_init__(self, field1=10, _my_field='value'):
                var = field1 * 10 # check #33039
                setattr(self, 'optField', var)
                self._my_field = _my_field

        a = self.r.test.ClassA()
        self.assertEqual(getattr(a, '_my_field', None), 'value',
                         'custom init with internal variables has failed')
        self.assertEqual(getattr(a, 'field1', None), 10,
                         'custom init with internal variables has failed')
        self.assertEqual(getattr(a, 'optField', None), 100,
                         'custom init with internal variables has failed')

        @z_compatibility.metaclass
        class test_ClassB(object):
            __metaclass__ = self.r.metaclass
            def __custom_init__(self, field1=20):
                # pylint: disable=bad-super-call
                super(test_ClassB, self).__custom_init__(field1=field1)
                self.field2 = 42

        b = self.r.test.ClassB()
        self.assertEqual(getattr(b, 'field1', None), 20,
                         'custom init inheritance has failed')
        self.assertEqual(getattr(b, 'field2', None), 42,
                         'custom init inheritance has failed')

        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_StructA(object):
            __metaclass__ = self.r.metaclass
            r = self.r   # pylint: disable=invalid-name
            def __custom_init__(self, **kwargs):
                self.class_a = self.r.test.ClassA(**kwargs)

        sta = self.r.test.StructA(field1=5)
        self.assertIsNotNone(getattr(sta, 'class_a', None),
                             'custom init with kwargs failed')
        self.assertEqual(getattr(sta.class_a, 'field1', None), 5,
                         'custom init with kwargs failed')

        # reinitialize ClassB and StructA
        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_ClassB(object):
            __metaclass__ = self.r.metaclass

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_StructA(object):
            __metaclass__ = self.r.metaclass

    def test_custom_inheritance(self):
        class CommonClass1(object):
            def foo(self):
                self.common_val1 = 42

        @z_compatibility.metaclass
        class test_StructA(CommonClass1):
            __metaclass__ = self.r.metaclass

            def foo(self):
                # pylint: disable=bad-super-call
                super(test_StructA, self).foo()
                self.common_val2 = 12

        st = self.r.test.StructA()
        st.foo()
        self.assertEqual(st.common_val1, 42)
        self.assertEqual(st.common_val2, 12)

        class BaseCommonClass1(object):
            def bar(self):
                self.common_val2 = 7777

        class CommonClass2(BaseCommonClass1):
            def foo(self):
                self.bar()
                self.common_val1 = 84

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_StructA(CommonClass2, CommonClass1):
            __metaclass__ = self.r.metaclass

        st = self.r.test.StructA()
        st.foo()
        self.assertEqual(st.common_val1, 84)
        self.assertEqual(st.common_val2, 7777)

        class CommonClass3(object):
            def __init__(self, *args, **kwargs):
                # pylint: disable=bad-super-call
                super(CommonClass3, self).__init__(*args, **kwargs)
                self.common_val1 = 10

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_StructA(CommonClass3):
            __metaclass__ = self.r.metaclass

        st = self.r.test.StructA()
        self.assertEqual(st.common_val1, 10)

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_StructA(object):
            __metaclass__ = self.r.metaclass

        st = self.r.test.StructA()
        self.assertFalse(hasattr(self.r.test.StructA, 'foo'))
        self.assertFalse(hasattr(st, 'common_val1'))
        self.assertFalse(hasattr(st, 'common_val2'))

        # Test consistent MRO
        class CommonClass4(object):
            pass

        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_ClassA(CommonClass4):
            __metaclass__ = self.r.metaclass

        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_ClassB(CommonClass4):
            __metaclass__ = self.r.metaclass

    def test_json_serialize(self):
        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_StructA(object):
            __metaclass__ = self.r.metaclass

            def __custom_init__(self, arg1=0):
                self.var1 = arg1

        structa = self.r.test.StructA()
        structa.tu.append(self.r.test.ClassA())
        structa.u = self.r.test.UnionA(s="toto")
        copy_struct_from_plugin = self.p.test.StructA(_json=str(structa))
        copy_struct_from_register = self.r.test.StructA(_json=str(structa))

        self.assertEqual(str(structa), str(copy_struct_from_plugin))
        self.assertEqual(structa.var1, copy_struct_from_register.var1)
        self.assertEqual(str(structa), str(copy_struct_from_register))

        b1 = self.r.test.ClassB(field2=3)
        b2 = self.r.test.ClassA(_json=str(b1))
        self.assertIsInstance(b2, self.r.test.ClassB)
        self.assertEqual(b2.field2, 3)

    def run_test_copy(self, is_deepcopy):
        copy_method = copy.deepcopy if is_deepcopy else copy.copy

        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_StructA(object):
            __metaclass__ = self.r.metaclass

            def __custom_init__(self, val=0, **kwargs):
                self.val = val
                self.__dict__.update(kwargs)

        err = self.r.test.Error(code=42, desc='test')
        structa = self.r.test.StructA(val=5, foo=err)
        structa.tu.append(self.r.test.ClassA())
        structa.u = self.r.test.UnionA("toto")
        structa.r = self.r.test.Error(code=42, desc='test')
        enuma = self.r.test.EnumA('B')
        enuma.baz = self.r.test.ClassB()
        structa.e = enuma
        structa.bar = 24

        copy_structa = copy_method(structa)
        self.assertEqual(structa, copy_structa)
        self.assertEqual(structa.val, copy_structa.val)
        self.assertEqual(structa.foo, copy_structa.foo)
        self.assertEqual(structa.bar, copy_structa.bar)
        self.assertEqual(structa.e.baz, copy_structa.e.baz)
        self.assertEqual(str(structa), str(copy_structa))
        self.assertNotEqual(id(structa), id(copy_structa))
        self.assertNotEqual(is_deepcopy,
                            id(structa.foo) == id(copy_structa.foo))
        self.assertNotEqual(is_deepcopy,
                            id(structa.tu[0]) == id(copy_structa.tu[0]))
        self.assertNotEqual(is_deepcopy,
                            id(structa.u) == id(copy_structa.u))
        self.assertNotEqual(is_deepcopy,
                            id(structa.e) == id(copy_structa.e))
        self.assertNotEqual(is_deepcopy,
                            id(structa.r) == id(copy_structa.r))
        self.assertNotEqual(is_deepcopy,
                            id(structa.e.baz) == id(copy_structa.e.baz))

        classb = self.r.test.ClassB(field1=42, field2=20)
        classb.plop = err

        copy_classb = copy_method(classb)
        self.assertEqual(classb, copy_classb)
        self.assertEqual(classb.field1, copy_classb.field1)
        self.assertEqual(classb.field2, copy_classb.field2)
        self.assertEqual(classb.plop, copy_classb.plop)
        self.assertEqual(str(classb), str(copy_classb))
        self.assertNotEqual(id(classb), id(copy_classb))
        self.assertNotEqual(is_deepcopy,
                            id(classb.plop) == id(copy_classb.plop))

        enuma = self.r.test.EnumA('A')
        enuma.plop = err

        copy_enuma = copy_method(enuma)
        self.assertEqual(enuma, copy_enuma)
        self.assertEqual(enuma.plop, copy_enuma.plop)
        self.assertEqual(str(enuma), str(copy_enuma))
        self.assertNotEqual(id(enuma), id(copy_enuma))
        self.assertNotEqual(is_deepcopy,
                            id(enuma.plop) == id(copy_enuma.plop))

        uniona = self.r.test.UnionA('A')
        uniona.plop = err

        copy_uniona = copy_method(uniona)
        self.assertEqual(uniona, copy_uniona)
        self.assertEqual(uniona.plop, copy_uniona.plop)
        self.assertEqual(str(uniona), str(copy_uniona))
        self.assertNotEqual(id(uniona), id(copy_uniona))
        self.assertNotEqual(is_deepcopy,
                            id(uniona.plop) == id(copy_uniona.plop))

        structg1 = self.r.test.StructG(a=2)
        structg2 = self.r.test.StructG(a=10, parent=structg1)
        structg1.child = structg2

        copy_structg1 = copy_method(structg1)
        copy_structg2 = copy_structg1.child
        self.assertNotEqual(id(structg1), id(copy_structg1))
        self.assertNotEqual(is_deepcopy, id(structg2) == id(copy_structg2))
        self.assertEqual(is_deepcopy,
                         id(copy_structg2.parent) == id(copy_structg1))

    def test_copy(self):
        self.run_test_copy(False)

    def test_deepcopy(self):
        self.run_test_copy(True)

    def test_unambiguous_union(self):
        self.assertEqual(self.r.test.UnionA(1), self.r.test.UnionA(i=1))
        self.assertEqual(self.r.test.UnionA('foo'),
                         self.r.test.UnionA(s='foo'))

    def test_safe_array_init(self):
        tab = [42]
        self.r.test.StructA(tu=tab)
        self.assertIs(tab[0], 42)

    def test_implicit_array(self):
        a0 = self.r.test.StructA(tu=[42])
        a1 = self.r.test.StructA(tu=42)
        self.assertEqual(a0, a1)
        a2 = self.r.test.StructA(tu=[])
        a2.tu = 42
        self.assertEqual(a0, a2)

    def test_cast_and_no_cast(self):
        tab = [1, self.r.test.UnionA(i=2)]
        self.r.test.StructA(tu=tab)
        tab = [self.r.test.UnionA(i=1), 2]
        self.r.test.StructA(tu=tab)

    def test_static_attrs(self):
        exp_static_attrs = {'intAttr': 999, 'strAttr': 'truc'}
        class_attrs = self.r.test.StaticAttrsC.__get_class_attrs__()
        self.assertEqual(class_attrs['statics'], exp_static_attrs)
        self.assertEqual(self.r.test.StaticAttrsB.intAttr, 999)
        self.assertEqual(self.r.test.StaticAttrsB.strAttr, 'plop')

    def test_unhashable(self):
        def _check_unhashable(x):
            with self.assertRaisesRegexp(TypeError, 'unhashable type'):
                hash(x)

        # Test classes are unhashable
        _check_unhashable(self.r.test.ClassA())

        # Test structs are unhashable
        _check_unhashable(self.r.test.StructA())

        # Test unions are unhashable
        _check_unhashable(self.r.test.UnionA(i=0))

        # Test enums are unhashable
        _check_unhashable(self.r.test.EnumA('A'))

        # Test we can redefine the hash if needed
        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_StructA(object):
            __metaclass__ = self.r.metaclass

            def __hash__(self):
                return id(self)

        a = self.r.test.StructA()
        self.assertEqual(hash(a), id(a))

    def test_enums(self):
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

    def test_union_object_key(self):
        """Test union __object__ and __key__ methods"""
        a = self.r.test.UnionA(i=58)
        self.assertEqual(a.__object__(), 58)
        self.assertEqual(a.__key__(), 'i')

    def test_abstract_class(self):
        """Test we cannot instantiate an abstract class"""
        msg = 'Cannot instantiate an abstract class'
        with self.assertRaisesRegexp(iopy.Error, msg):
            self.r.test.ConstraintsA(name='plop')

    def test_from_file_child_class(self):
        """Test using from_file from base class to create an instance of a
        child class."""
        path = os.path.join(TEST_PATH, 'test_class_b.json')
        b = self.r.test.ClassA.from_file(_json=path)
        self.assertEqual(b.field1, 42)
        self.assertEqual(b.field2, 10)
        self.assertEqual(b.optField, 20)

    def test_type_metaclass_double_upgrade(self):
        """Test type metaclass with double level of upgrade"""
        #pylint: disable=unused-variable, function-redefined
        @z_compatibility.metaclass
        class test_EnumA(object):
            __metaclass__ = self.p.metaclass

            @staticmethod
            def foo():
                return 'foo'

        class test_EnumA(test_EnumA):
            @staticmethod
            def bar():
                return 'bar'

        a1 = self.p.test.EnumA('A')
        self.assertEqual(a1.foo(), 'foo')
        self.assertEqual(a1.bar(), 'bar')
        a2 = test_EnumA('A')
        self.assertEqual(a2.foo(), 'foo')
        self.assertEqual(a2.bar(), 'bar')
        self.assertEqual(a1, a2)

        @z_compatibility.metaclass
        class test_ClassA(object):
            __metaclass__ = self.p.metaclass

            @staticmethod
            def foo():
                return 'foo'

        class test_ClassA(test_ClassA):
            @staticmethod
            def bar():
                return 'bar'

        @self.p.upgrade(force_replace=True)
        class ClassB(self.r.test.ClassB):
            @staticmethod
            def baz():
                return 'baz'

        b1 = self.p.test.ClassB()
        self.assertEqual(b1.foo(), 'foo')
        self.assertEqual(b1.bar(), 'bar')
        self.assertEqual(b1.baz(), 'baz')
        b2 = ClassB()
        self.assertEqual(b2.foo(), 'foo')
        self.assertEqual(b2.bar(), 'bar')
        self.assertEqual(b2.baz(), 'baz')
        self.assertEqual(b1, b2)

    def test_enum_description(self):
        desc = self.p.test.EnumDescription.get_iop_description()

        self.assertEqual(desc.help.brief, 'Brief enum documentation.')
        self.assertEqual(desc.help.details, 'Detailed enum documentation.')
        self.assertEqual(desc.help.warning, 'Warning enum documentation.')
        self.assertIsNone(desc.help.example)

        self.assertTrue(desc.strict)

        self.assertEqual(desc.generic_attributes, {
            'test:gen1': 1,
        })

        a_desc = desc.values['A']

        self.assertEqual(a_desc.help.brief, 'A brief documentation.')
        self.assertEqual(a_desc.help.details, 'A detailed documentation.')
        self.assertEqual(a_desc.help.warning, 'A warning documentation.')
        self.assertIsNone(a_desc.help.example)

        self.assertEqual(a_desc.generic_attributes, {
            'test:gen2': 2.2,
            'test:gen3': 'jiojj'
        })

        self.assertEqual(a_desc.aliases, ('A_ALIAS',))

        b_desc = desc.values['B']

        self.assertEqual(b_desc.help.brief, 'B brief documentation.')
        self.assertEqual(b_desc.help.details, 'B detailed documentation.')
        self.assertEqual(b_desc.help.warning, 'B warning documentation.')
        self.assertIsNone(b_desc.help.example)

        self.assertEqual(b_desc.generic_attributes, {
            'test:gen4': 1,
            'test:gen5': '{"field":{"f1":"val1","f2":1}}',
        })

        self.assertEqual(b_desc.aliases, ('B1_ALIAS', 'B2_ALIAS'))

    def test_union_description(self):
        desc = self.p.test.UnionDescription.get_iop_description()

        self.assertEqual(desc.help.brief, 'Brief union documentation.')
        self.assertEqual(desc.help.details, 'Detailed union documentation.')
        self.assertEqual(desc.help.warning, 'Warning union documentation.')
        self.assertIsNone(desc.help.example)

        self.assertEqual(desc.generic_attributes, {
            'test:gen1': 1,
        })

        # FIXME: There is a bug in iopc.
        # self.assertTrue(desc.deprecated)

        a_desc = desc.fields['a']

        self.assertEqual(a_desc.help.brief, 'A brief documentation.')
        self.assertEqual(a_desc.help.details, 'A detailed documentation.')
        self.assertEqual(a_desc.help.warning, 'A warning documentation.')
        self.assertIsNone(a_desc.help.example)

        self.assertEqual(a_desc.generic_attributes, {
            'test:gen2': 'plop',
        })

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

        self.assertEqual(b_desc.generic_attributes, {
            'test:gen3': 15,
            'test:gen4': '{"plop":4}',
        })

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

        self.assertEqual(c_desc.generic_attributes, {
            'test:gen5': 1,
        })

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

    def test_struct_description(self):
        desc = self.p.test.StructDescription.get_iop_description()

        self.assertEqual(desc.help.brief, 'Brief struct documentation.')
        self.assertEqual(desc.help.details, 'Detailed struct documentation.')
        self.assertEqual(desc.help.warning, 'Warning struct documentation.')
        self.assertIsNone(desc.help.example)

        self.assertEqual(desc.generic_attributes, {
            'test:gen1': 1,
        })

        # FIXME: There is a bug in iopc.
        # self.assertTrue(desc.deprecated)

        a_desc = desc.fields['a']

        self.assertEqual(a_desc.help.brief, 'A brief documentation.')
        self.assertEqual(a_desc.help.details, 'A detailed documentation.')
        self.assertEqual(a_desc.help.warning, 'A warning documentation.')
        self.assertIsNone(a_desc.help.example)

        self.assertEqual(a_desc.generic_attributes, {
            'test:gen2': 'plop',
        })

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

        self.assertEqual(b_desc.generic_attributes, {
            'test:gen3': 15,
            'test:gen4': '{"plop":4}',
        })

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

        self.assertEqual(c_desc.generic_attributes, {
            'test:gen5': 1,
        })

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

    def test_class_description(self):
        # Base class
        base_desc = self.p.test.BaseClassDescription.get_iop_description()

        self.assertEqual(base_desc.help.brief,
                         'Brief base class documentation.')
        self.assertEqual(base_desc.help.details,
                         'Detailed base class documentation.')
        self.assertEqual(base_desc.help.warning,
                         'Warning base class documentation.')
        self.assertIsNone(base_desc.help.example)

        self.assertEqual(base_desc.generic_attributes, {
            'test:gen1': 1,
        })

        # FIXME: There is a bug in iopc.
        # self.assertTrue(base_desc.deprecated)

        self.assertIsNone(base_desc.parent)
        self.assertTrue(base_desc.is_abstract)
        self.assertTrue(base_desc.is_private)
        self.assertEqual(base_desc.class_id, 0)

        # Static A
        base_static_a_desc = base_desc.statics['staticA']
        self.assertIs(base_desc.cls_statics['staticA'], base_static_a_desc)

        self.assertEqual(base_static_a_desc.help.brief,
                         'Static A brief documentation.')
        self.assertEqual(base_static_a_desc.help.details,
                         'Static A detailed documentation.')
        self.assertEqual(base_static_a_desc.help.warning,
                         'Static A warning documentation.')
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

        self.assertEqual(base_static_c_desc.help.brief,
                         'Static C base brief documentation.')
        self.assertEqual(base_static_c_desc.help.details,
                         'Static C base detailed documentation.')
        self.assertEqual(base_static_c_desc.help.warning,
                         'Static C base warning documentation.')
        self.assertIsNone(base_static_c_desc.help.example)

        self.assertEqual(base_static_c_desc.iop_type, 'ulong')
        self.assertEqual(base_static_c_desc.py_type, int)
        self.assertEqual(base_static_c_desc.value, 42)

        # A field
        a_desc = base_desc.fields['a']

        self.assertEqual(a_desc.generic_attributes, {
            'test:gen2': 'plop',
        })

        self.assertEqual(a_desc.iop_type, 'test.StructA')
        self.assertEqual(a_desc.py_type, self.p.test.StructA)


        # Child class
        child_desc = self.p.test.ChildClassDescription.get_iop_description()

        self.assertEqual(child_desc.help.brief,
                         'Brief child class documentation.')
        self.assertEqual(child_desc.help.details,
                         'Detailed child class documentation.')
        self.assertEqual(child_desc.help.warning,
                         'Warning child class documentation.')
        self.assertIsNone(child_desc.help.example)

        self.assertEqual(child_desc.generic_attributes, {
            'test:gen3': 7,
        })

        self.assertIs(child_desc.parent, self.p.test.BaseClassDescription)
        self.assertFalse(child_desc.is_abstract)
        self.assertFalse(child_desc.is_private)
        self.assertEqual(child_desc.class_id, 1)

        # Static A
        child_static_a_desc = child_desc.statics['staticA']
        self.assertTrue('staticA' not in child_desc.cls_statics)

        self.assertEqual(child_static_a_desc.help.brief,
                         'Static A brief documentation.')
        self.assertEqual(child_static_a_desc.help.details,
                         'Static A detailed documentation.')
        self.assertEqual(child_static_a_desc.help.warning,
                         'Static A warning documentation.')
        self.assertIsNone(child_static_a_desc.help.example)

        self.assertEqual(child_static_a_desc.iop_type, 'string')
        self.assertEqual(child_static_a_desc.py_type, str)
        self.assertEqual(child_static_a_desc.value, 'plop')

        # Static B
        child_static_b_desc = child_desc.statics['staticB']
        self.assertIs(child_desc.cls_statics['staticB'], child_static_b_desc)

        # XXX: It is not exported by iopc.
        #self.assertEqual(child_static_b_desc.help.brief,
        #                 'Static B brief documentation.')
        #self.assertEqual(child_static_b_desc.help.details,
        #                 'Static B detailed documentation.')
        #self.assertEqual(child_static_b_desc.help.warning,
        #                 'Static B warning documentation.')
        #self.assertIsNone(child_static_b_desc.help.example)

        self.assertEqual(child_static_b_desc.iop_type, 'long')
        self.assertEqual(child_static_b_desc.py_type, int)
        self.assertEqual(child_static_b_desc.value, 26)

        # Static C
        child_static_c_desc = child_desc.statics['staticC']
        self.assertIs(child_desc.cls_statics['staticC'], child_static_c_desc)

        self.assertEqual(child_static_c_desc.help.brief,
                         'Static C child brief documentation.')
        self.assertEqual(child_static_c_desc.help.details,
                         'Static C child detailed documentation.')
        self.assertEqual(child_static_c_desc.help.warning,
                         'Static C child warning documentation.')
        self.assertIsNone(child_static_c_desc.help.example)

        self.assertEqual(child_static_c_desc.iop_type, 'ulong')
        self.assertEqual(child_static_c_desc.py_type, int)
        self.assertEqual(child_static_c_desc.value, 14)

        # A field
        a_desc = child_desc.fields['a']

        self.assertEqual(a_desc.generic_attributes, {
            'test:gen2': 'plop',
        })

        self.assertEqual(a_desc.iop_type, 'test.StructA')
        self.assertEqual(a_desc.py_type, self.p.test.StructA)

        # B field
        b_desc = child_desc.fields['b']

        self.assertEqual(b_desc.generic_attributes, {
            'test:gen4': 3.4,
        })

        self.assertEqual(b_desc.iop_type, 'test.ClassB')
        self.assertEqual(b_desc.py_type, self.p.test.ClassB)

    def test_dict_init(self):
        # Test working case
        old_struct_a = self.r.test.StructA(
            e=self.r.test.EnumA('A'),
            a=self.r.test.ClassA(
                field1=10
            ),
            tu=[self.r.test.UnionA(
                i=24
            ), self.r.test.UnionA(
                a=self.r.test.ClassB(
                    field2=87
                )
            ), self.r.test.UnionA(
                s='toto'
            )]
        )

        new_struct_a = self.r.test.StructA({
            'e': 'A',
            'a': {
                'field1': 10
            },
            'tu': [{
                'i': 24
            }, {
                'a': {
                    '_class': 'test.ClassB',
                    'field2': 87
                }
            }, {
                's': 'toto'
            }]
        })

        self.assertEqual(old_struct_a, new_struct_a)

        # Fail test multiple args
        with self.assertRaises(TypeError):
            self.r.test.StructA({ 'e': 'A' }, { 'e': 'B' })

        # Fail test with dict arg and kwargs
        with self.assertRaises(TypeError):
            self.r.test.StructA({ 'e': 'A' }, e='B')

        # Fail test not a class
        exp = r'IOPy type `test.StructA` is not a class'
        with self.assertRaisesRegexp(TypeError, exp):
            self.r.test.StructA({'_class': 'test.ClassA'})

        # Fail test unknown type
        exp = r'unknown IOPy type `plop.Plip`'
        with self.assertRaisesRegexp(TypeError, exp):
            self.r.test.ClassA({'_class': 'plop.Plip'})

        # Fail test not a valid child
        exp = (r'IOPy type `test.ClassC` is not a child type of IOPy type '
               r'`test.ClassA`')
        with self.assertRaisesRegexp(TypeError, exp):
            self.r.test.ClassA({'_class': 'test.ClassC'})


@z.ZGroup
class IopyIfaceTests(z.TestCase):
    def setUp(self):
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = iopy.Plugin(plugin_file)
        self.r = self.p.register()

        self.uri = make_uri()

        def rpc_impl_a(rpc_args):
            return rpc_args.res(status='A', res=1000)

        def rpc_impl_b(rpc_args):
            return rpc_args.res(status='B', res=0)

        def rpc_impl_v(rpc_args):
            if hasattr(rpc_args.arg, 'ov'):
                return rpc_args.res()
            else:
                return rpc_args.res(ov=None)

        self.s = self.r.channel_server()
        # pylint: disable=protected-access
        self.s.test_ModuleA.interfaceA._rpcs.funA.impl = rpc_impl_a
        self.s.test_ModuleA.interfaceA.funB.impl = rpc_impl_b
        self.s.test_ModuleA.interfaceA.funToggleVoid.impl = rpc_impl_v
        self.s.listen(uri=self.uri)

    def test_iopy_iface(self):
        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

            cls_attr = 0

            def __custom_init__(self):
                self.attr1 = 1
                self.attr2 = 2

            def my_method(self):
                self.cls_attr = 10
                return self.attr1

            def funA(self, *args, **kwargs):  # pylint: disable=invalid-name
                self.attr1 = kwargs.get('a', None)
                res = self._rpcs.funA(*args, **kwargs)
                self.attr2 = res.status
                return res

            def funToggleVoid(self, *args, **kwargs):
                # pylint: disable=invalid-name
                return self._rpcs.funToggleVoid(*args, **kwargs)

        c = self.r.connect(self.uri)

        iface = c.test_ModuleA.interfaceA

        attr = getattr(iface, 'cls_attr', None)
        self.assertEqual(attr, 0, 'class attribute failed; value: %s,'
                         ' expected: 0' % attr)

        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 1, 'custom init failed; value of attr1: %s,'
                         ' expected: 1' % attr)

        self.assertTrue(hasattr(iface, 'my_method'),
                        'custom method not added')
        ret = iface.my_method()
        self.assertEqual(ret, 1, 'custom method failed; result: %s,'
                         ' expected: 1' % ret)
        attr = getattr(iface, 'cls_attr', None)
        self.assertEqual(attr, 10, 'custom method failed;'
                         ' value of cls_attr: %s, expected: 10' % attr)

        a = self.r.test.ClassA(field1=100)
        ret = iface.funA(a=a)

        ret = getattr(ret, 'res', None)
        self.assertEqual(ret, 1000, 'rpc override call failed; res.res: %s,'
                         ' expected: 1000' % ret)

        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, a, 'rpc override failed; attr1 value: %s,'
                         ' expected: %s' % (attr, a))

        attr = getattr(iface, 'attr2', None)
        exp = self.r.test.EnumA('A')
        self.assertEqual(attr, exp, 'rpc override failed; attr2 value: %s,'
                         ' expected: %s' % (attr, exp))

        ret = iface.funB(a=self.r.test.ClassA())
        ret = getattr(ret, 'status', None)
        exp = self.r.test.EnumA('B')
        self.assertEqual(ret, exp, 'rpc failed; status: %s,'
                         ' expected: %s' % (ret, exp))

        ret = iface.funToggleVoid(ov=None)
        self.assertFalse(hasattr(ret, 'ov'))

        ret = iface.funToggleVoid()
        self.assertTrue(hasattr(ret, 'ov'))
        self.assertIsNone(ret.ov)

    def test_iopy_iface_hooks(self):
        @z_compatibility.metaclass  # pylint: disable=unused-variable
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

            def __pre_hook__(self, rpc, *args, **kwargs):
                self.pre_hook_rpc = rpc
                self.pre_hook_args = args
                self.pre_hook_kwargs = kwargs

            def __post_hook__(self, rpc, res):
                self.post_hook_rpc = rpc
                self.post_hook_res = res

        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA

        kwargs = dict(a=self.r.test.ClassA())
        iface.funA(**kwargs)

        attr = getattr(iface, 'pre_hook_rpc', None)
        self.assertEqual(attr, iface.funA, 'pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, iface.funA))

        attr = getattr(iface, 'pre_hook_args', None)
        self.assertEqual(attr, (), 'pre_hook failed for args argument;'
                         ' value: %s; expected: ()' % str(attr))

        attr = getattr(iface, 'pre_hook_kwargs', None)
        self.assertEqual(attr, kwargs, 'pre_hook failed for kwargs argument;'
                         ' value: %s; expected: %s' % (attr, kwargs))

        attr = getattr(iface, 'post_hook_rpc', None)
        self.assertEqual(attr, iface.funA, 'post_hook failed for rpc argument'
                         '; value: %s; expected: %s' % (attr, iface.funA))

        attr = getattr(iface, 'post_hook_res', None)
        exp = iface.funA.res()(status='A', res=1000)
        self.assertEqual(attr, exp, 'post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, exp))

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces
            r = self.r  # pylint: disable=invalid-name

            def __pre_hook__(self, rpc, *args, **kwargs):
                return ((), dict(a=type(self).r.test.ClassA()))

            @classmethod
            def __post_hook__(cls, rpc, res):
                return 0

        ret = iface.funA(0, x=0, y=0)
        self.assertEqual(ret, 0, 'hooks arguments/result replacement failed'
                         '; result: %s; expected: 0' % ret)

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces
            r = self.r  # pylint: disable=invalid-name

            def __pre_hook__(self, rpc, *args, **kwargs):
                new_args = (rpc.arg()(a=type(self).r.test.ClassA()),)
                return (new_args, {})

            @classmethod
            def __post_hook__(cls, rpc, res):
                return 0

        ret = iface.funA(0, x=0, y=0)
        self.assertEqual(ret, 0, 'hooks arguments/result replacement failed'
                         '; result: %s; expected: 0' % ret)

        def default_pre_hook(self, rpc, *args, **kwargs):
            self.attr1 = 1

        def default_post_hook(self, rpc, res):
            self.attr2 = 1

        # define class before setting default pre/post hooks
        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

        self.r.default_pre_hook = default_pre_hook
        self.r.default_post_hook = default_post_hook

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 1, 'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 1))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, 1, 'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 1))

        # define class after setting default pre/post hooks
        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 1, 'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 1))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, 1, 'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 1))

        # custom pre/post hooks are used instead of default pre/post hooks
        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

            def __pre_hook__(self, rpc, *args, **kwargs):
                self.attr1 = 2

            def __post_hook__(self, rpc, res):
                self.attr2 = 2

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 2, 'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 2))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, 2, 'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 2))

        # custom pre/post hooks from external functions are used instead of
        # default pre/post hooks
        def iface_pre_hook_1(self, rpc, *args, **kwargs):
            self.attr1 = 3

        def iface_post_hook_1(self, rpc, res):
            self.attr2 = 3

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces
            __pre_hook__ = iface_pre_hook_1
            __post_hook__ = iface_post_hook_1

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 3, 'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 3))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, 3, 'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 3))

        # added pre/post hooks after class definition are used instead of
        # default pre/post hooks
        def iface_pre_hook_2(self, rpc, *args, **kwargs):
            self.attr1 = 4

        def iface_post_hook_2(self, rpc, res):
            self.attr2 = 4

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

        type(iface).__pre_hook__ = iface_pre_hook_2
        type(iface).__post_hook__ = iface_post_hook_2

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 4, 'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 4))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, 4, 'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 4))

        # reset default pre/post hooks
        del self.r.default_pre_hook
        del self.r.default_post_hook
        iface.attr1 = None
        iface.attr2 = None

        # iface should still have its custom pre/post hooks
        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, 4, 'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 4))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, 4, 'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, 4))

        # reset iface pre/post hooks
        iface.attr1 = None
        iface.attr2 = None
        del type(iface).__pre_hook__
        del type(iface).__post_hook__

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

        iface.funA(a=self.r.test.ClassA())
        attr = getattr(iface, 'attr1', None)
        self.assertEqual(attr, None,
                         'default pre_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, None))
        attr = getattr(iface, 'attr2', None)
        self.assertEqual(attr, None,
                         'default post_hook failed for rpc argument;'
                         ' value: %s; expected: %s' % (attr, None))

    def test_iopy_iface_inheritance(self):
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA

        class CommonClass1(object):
            def foo(self):
                self.common_val1 = 42

        @z_compatibility.metaclass
        class test_InterfaceA(CommonClass1):
            __metaclass__ = self.r.metaclass_interfaces

            def foo(self):
                # pylint: disable=bad-super-call
                super(test_InterfaceA, self).foo()
                self.common_val2 = 12

        iface.foo()
        self.assertEqual(iface.common_val1, 42)
        self.assertEqual(iface.common_val2, 12)

        class BaseCommonClass1(object):
            def bar(self):
                self.common_val2 = 7777

        class CommonClass2(BaseCommonClass1):
            def foo(self):
                self.bar()
                self.common_val1 = 84

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(CommonClass2, CommonClass1):
            __metaclass__ = self.r.metaclass_interfaces

        iface.foo()
        self.assertEqual(iface.common_val1, 84)
        self.assertEqual(iface.common_val2, 7777)

        c.disconnect()
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA

        @z_compatibility.metaclass  # pylint: disable=function-redefined
        class test_InterfaceA(object):
            __metaclass__ = self.r.metaclass_interfaces

        self.assertFalse(hasattr(iface, 'common_val1'))
        self.assertFalse(hasattr(iface, 'common_val2'))

    def test_iopy_threads(self):
        """Test IOPy with threads"""
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        exp_res = iface.funA(a=obj_a)

        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, True, False)

    def test_iopy_forks(self):
        """Test IOPy with forks"""
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        exp_res = iface.funA(a=obj_a)

        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, False, True)

    def test_iopy_threads_and_forks(self):
        """Test IOPy with threads, forks and threads inside forks"""
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        obj_a = self.r.test.ClassA()
        exp_res = iface.funA(a=obj_a)

        z_iopy_test_threads_and_forks(iface, obj_a, exp_res, True, True)

    def test_module_short_name(self):
        """Test we add modules with short name when not ambiguous"""
        self.assertIs(self.s.test_ModuleA, self.s.ModuleA)

    def test_iface_metaclass_double_upgrade(self):
        """Test iface metaclass with double level of upgrade"""
        #pylint: disable=unused-variable, function-redefined
        @z_compatibility.metaclass
        class test_InterfaceA(object):
            __metaclass__ = self.p.metaclass_interfaces
            @staticmethod
            def foo():
                return 'foo'

        class test_InterfaceA(test_InterfaceA):
            @staticmethod
            def bar():
                return 'bar'

        iface1 = self.p.test.interfaces.InterfaceA
        self.assertEqual(iface1.foo(), 'foo')
        self.assertEqual(iface1.bar(), 'bar')
        iface2 = test_InterfaceA
        self.assertEqual(iface2.foo(), 'foo')
        self.assertEqual(iface2.bar(), 'bar')
        self.assertEqual(str(iface1), str(iface2))

    def test_iface_with_dict_arg(self):
        c = self.r.connect(self.uri)
        iface = c.test_ModuleA.interfaceA
        res = iface.funA({
            'a': {
                'field1': 1
            }
        })
        self.assertEqual(res.status, 'A')
        self.assertEqual(res.res, 1000)


@z.ZGroup
class IopyScriptsTests(z.TestCase):
    def setUp(self):
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin-scripts.so')
        self.p = iopy.Plugin(plugin_file)
        self.r = self.p.register()

    def test_iopy_register_scripts(self):
        self.assertTrue(hasattr(self.r.test.ClassA, 'user_method'),
                        "register script failed for test_1.py")

        self.assertTrue(hasattr(self.r.test_emptystuffs.EmptyStruct, 'fun'),
                        "register script failed for test_3.py")

        self.assertFalse(self.r.test.ClassA().user_method(),
                         "user method of classA failed")
        self.assertTrue(self.r.test.ClassA(field1=1).user_method(),
                        "user method of classA failed")
        self.assertFalse(self.r.test.ClassB(field1=1).user_method(),
                         "user method of classB failed")
        self.assertTrue(self.r.test.ClassB(field1=1, field2=2).user_method(),
                        "user method of classB failed")
        self.assertTrue(self.r.test_emptystuffs.EmptyStruct().fun(),
                        "user method of EmptyStruct failed")

@z.ZGroup
class IopyVoidTest(z.TestCase): # {{{
    def setUp(self):
        self.plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = iopy.Plugin(self.plugin_file)
        self.r = self.p.register()

    def test_void_union_json(self):
        u = self.r.testvoid.VoidUnion(_json="{ a: null }")
        self.assertEqual(str(u), "{ \"a\": null }\n")
        self.assertIsNone(u.a)
        self.assertFalse(hasattr(u, 'b'))

    def test_void_struct_required_json(self):
        s = self.r.testvoid.VoidRequired(_json="{a: null}")
        self.assertIsNone(s.a)
        # required void can be omited
        s = self.r.testvoid.VoidRequired(_json="{}")
        self.assertIsNone(s.a)
        # json output
        self.assertEqual(str(s), "{\n}\n")

    def test_void_struct_optional_json(self):
        # check struct creation from json works correctly with a field
        s = self.r.testvoid.VoidOptional(_json="{ a: null}")
        self.assertIsNone(s.a)
        self.assertEqual(str(s), "{\n    \"a\": null\n}\n")

        s = self.r.testvoid.VoidOptional(_json="{ }")
        self.assertFalse(hasattr(s, 'a'))
        self.assertEqual(str(s), "{\n}\n")

    def test_void_union(self):
        # check union creation from args works correctly
        u = self.r.testvoid.VoidUnion(a=None)
        self.assertIsNone(u.a)
        u = self.r.testvoid.VoidUnion(b=1)
        self.assertFalse(hasattr(u, 'a'))

        # check setting to anything but None fails
        msg = r"invalid type: got int \(0\), expected NoneType"
        with self.assertRaisesRegexp(iopy.Error, msg):
            u.a = 0
        # try setting other field, then setting back our void field
        u.b = 0
        self.assertEqual(u.b, 0)
        self.assertFalse(hasattr(u, 'a'))
        u.a = None
        self.assertIsNone(u.a)

        msg = (r"[Ii]nvalid argument .plumbus.")
        with self.assertRaisesRegexp(iopy.Error, msg):
            _ = self.r.testvoid.VoidUnion(plumbus=666)

    def test_void_struct_required(self):
        # required void arg can be omited
        s = self.r.testvoid.VoidRequired()
        self.assertIsNone(s.a)
        # required void arg can be passed as None
        s = self.r.testvoid.VoidRequired(a=None)
        self.assertIsNone(s.a)
        # setting to None works
        s.a = None
        self.assertIsNone(s.a)
        # setting to anything but None fails
        msg = r"invalid type: got int \(0\), expected NoneType"
        with self.assertRaisesRegexp(iopy.Error, msg):
            s.a = 0
        # deleting required field fails
        with self.assertRaises(iopy.Error):
            del(s.a)

    def test_void_struct_optional(self):
        # optional void arg can be set
        s = self.r.testvoid.VoidOptional(a=None)
        self.assertIsNone(s.a)

        # optional void arg can be skipped
        s = self.r.testvoid.VoidOptional()
        self.assertFalse(hasattr(s, 'a'))
        # setting to None works
        s.a = None
        self.assertEqual(str(s), "{\n    \"a\": null\n}\n")
        self.assertIsNone(s.a)
        # check setting to anything but None fails
        msg = r"invalid type: got int \(0\), expected NoneType"
        with self.assertRaisesRegexp(iopy.Error, msg):
            s.a = 0
        # check deleting optional field clears it
        del(s.a)
        self.assertFalse(hasattr(s, 'a'))
# }}}

# pylint: disable=super-on-old-class, bad-super-call
@z.ZGroup
class IopyV3Tests(z.TestCase):
    def setUp(self):
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = iopy.Plugin(plugin_file)
        self.r = self.p.register()

    def test_type_simple(self):
        @self.r.upgrade(force_replace=True)
        class ClassA(self.r.test.ClassA):
            def __init__(self, field1=20, my_val=10, *args, **kwargs):
                super(ClassA, self).__init__(field1=field1, *args, **kwargs)
                self.my_val = my_val

            def foo(self):
                return self.my_val

        a = self.r.test.ClassA(optField=0)
        self.assertEqual(getattr(a, 'field1', None), 20,
                         'init of iop field has failed')
        self.assertEqual(getattr(a, 'optField', None), 0,
                         'init of optional iop field has failed')
        self.assertEqual(getattr(a, 'my_val', None), 10,
                         'init of custom variable has failed')
        self.assertEqual(a.foo(), 10,
                         'method of upgraded class has failed')

    def test_multiple_inheritance(self):
        class BaseClassA(object):
            def __init__(self, base_val, *args, **kwargs):
                super(BaseClassA, self).__init__(*args, **kwargs)
                self.base_val = base_val

        @self.r.upgrade(index=1, force_replace=True)
        class ClassA(BaseClassA, self.r.test.ClassA):
            def __init__(self, my_val, *args, **kwargs):
                super(ClassA, self).__init__(*args, **kwargs)
                self.my_val = my_val

        a = self.r.test.ClassA(base_val=20, my_val=15, field1=10)
        self.assertEqual(getattr(a, 'base_val', None), 20,
                         'init of base_val has failed')
        self.assertEqual(getattr(a, 'my_val', None), 15,
                         'init of my_val has failed')
        self.assertEqual(getattr(a, 'field1', None), 10,
                         'init of iop field1 has failed')

    def test_json_copy(self):
        @self.r.upgrade(force_replace=True)
        class ClassA(self.r.test.ClassA):
            def __init__(self, my_val=13, *args, **kwargs):
                super(ClassA, self).__init__(*args, **kwargs)
                self.my_val = my_val

        a = self.r.test.ClassA(field1=42, optField=20)
        a_cpy = self.r.test.ClassA(_json=str(a))

        self.assertEqual(getattr(a_cpy, 'field1', None), 42,
                         'copy of iop for field1 has failed')
        self.assertEqual(getattr(a_cpy, 'optField', None), 20,
                         'copy of iop for optField has failed')
        self.assertEqual(getattr(a_cpy, 'my_val', None), 13,
                         'init of custom variable has failed')

    def test_json_init(self):
        @self.r.upgrade(force_replace=True)
        class StructA(self.r.test.StructA):
            def __init__(self, my_val=12, *args, **kwargs):
                super(StructA, self).__init__(*args, **kwargs)
                self.my_val = my_val

        @self.r.upgrade(force_replace=True)
        class ClassA(self.r.test.ClassA):
            def __init__(self, field1=20, *args, **kwargs):
                field1 *= 3
                super(ClassA, self).__init__(field1=field1, *args, **kwargs)

        @self.r.upgrade(force_replace=True)
        class EnumA(self.r.test.EnumA):
            def __init__(self, *args, **kwargs):
                super(EnumA, self).__init__(*args, **kwargs)
                self.plop = "plop"

        @self.r.upgrade(force_replace=True)
        class UnionA(self.r.test.UnionA):
            def __init__(self, *args, **kwargs):
                super(UnionA, self).__init__(s="toto")

        path = os.path.join(TEST_PATH, 'test_struct_a.json')
        with open(path, 'r') as f:
            json = f.read()

        a = self.r.test.StructA(_json=json)
        self.assertEqual(a.my_val, 12)
        self.assertIsInstance(a.a, self.r.test.ClassB)
        self.assertEqual(a.a.field1, 30)
        self.assertEqual(a.e.get_as_str(), "B")
        self.assertEqual(a.u.s, "toto")

    def test_interface(self):
        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class InterfaceA(self.r.tst1.interfaces.InterfaceTest):
            def fun(self, *args, **kwargs):
                res = self._rpcs.fun(*args, **kwargs)
                self.val = res.val
                return res

        def rpc_impl_fun(rpc_args):
            return rpc_args.res(val=42)

        uri = make_uri()
        s = self.r.ChannelServer()
        s.listen(uri=uri)
        # pylint: disable=protected-access
        s.tst1_ModuleTest.interfaceTest._rpcs.fun.impl = rpc_impl_fun

        c = self.r.connect(uri)
        iface = c.tst1_ModuleTest.interfaceTest
        self.assertEqual(42, iface.fun().val)
        self.assertEqual(42, iface.val)

    def test_custom_cast_union(self):
        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class UnionC(self.r.test.UnionC):
            @staticmethod
            def __from_python__(val):
                if isinstance(val, str):
                    return self.r.test.UnionC(s=val)
                elif val < 0:
                    return self.r.test.UnionC(negative=val)
                elif val > 0:
                    return self.r.test.UnionC(positive=val)
                else:
                    return self.r.test.UnionC(zero=val)

        container = self.r.test.StructH(c=-42)
        self.assertEqual(-42, container.c.negative)

        container = self.r.test.StructH(c=42)
        self.assertEqual(42, container.c.positive)

        container = self.r.test.StructH(c=0)
        self.assertEqual(0, container.c.zero)

        container = self.r.test.StructH(c='foo')
        self.assertEqual('foo', container.c.s)

        container = self.r.test.StructH(c=self.r.test.UnionC(positive=123))
        self.assertEqual(123, container.c.positive)

    def test_custom_cast_class(self):
        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class ClassA(self.r.test.ClassA):
            @staticmethod
            def __from_python__(val):
                return self.r.test.ClassB(field1=val, field2=val)

        container = self.r.test.UnionA(a=42)
        self.assertEqual(42, container.a.field1)
        self.assertEqual(42, container.a.field2)

        container = self.r.test.UnionA(a=self.r.test.ClassB(field1=42,
                                                            field2=42))
        self.assertEqual(42, container.a.field1)
        self.assertEqual(42, container.a.field2)

    def test_custom_cast_nested(self):
        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class ClassA(self.r.test.ClassA):
            @staticmethod
            def __from_python__(val):
                return self.r.test.ClassB(field1=val, field2=val)

        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class UnionA(self.r.test.UnionA):
            @staticmethod
            def __from_python__(val):
                return self.r.test.UnionA(a=val)

        container = self.r.test.StructA(u=43)
        self.assertEqual(43, container.u.a.field1)
        self.assertEqual(43, container.u.a.field2)

    def test_custom_cast_none_optional(self):
        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class UnionA(self.r.test.UnionA):
            @staticmethod
            def __from_python__(val):
                raise ValueError('__from_python__ must not be called with '
                                 'None on an optional field')

        container = self.r.test.StructA(u=None)
        self.assertFalse(hasattr(container, 'u'))

    def test_custom_cast_none_mandatory(self):
        # pylint: disable=unused-variable
        @self.r.upgrade(force_replace=True)
        class UnionA(self.r.test.UnionA):
            @staticmethod
            def __from_python__(val):
                assert val is None
                return self.r.test.UnionA(i=1)

        container = self.r.test.UnionB(a=None)
        self.assertEqual(container.a.i, 1)


@z.ZGroup
class IopyDsoTests(z.TestCase):
    def setUp(self):
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin-scripts.so')
        self.p = iopy.Plugin(plugin_file)
        self.r = self.p.register()

    def test_iopy_load_unload_dso(self):
        def rpc_impl_fun(rpc_args):
            return rpc_args.res(val=21)

        # package test should be loaded but not tst1
        self.assertTrue(hasattr(self.p, 'test'),
                        'package "test" should be loaded')
        self.assertTrue(hasattr(self.r, 'test'),
                        'package "test" should be loaded')
        self.assertFalse(hasattr(self.p, 'tst1'),
                         'package "tst1" should not be loaded')
        self.assertFalse(hasattr(self.r, 'tst1'),
                         'package "tst1" should not be loaded')

        # load additional plugin
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p.load_dso('plugin', plugin_file)

        # the two packages should be loaded
        self.assertTrue(hasattr(self.p, 'tst1'),
                        'package "tst1" should be loaded')
        self.assertTrue(hasattr(self.r, 'tst1'),
                        'package "tst1" should be loaded')
        self.assertTrue(hasattr(self.p, 'test'),
                        'package "test" should be loaded')
        self.assertTrue(hasattr(self.r, 'test'),
                        'package "test" should be loaded')

        # we should be able to query tst1_ModuleTest.interfaceTest.fun
        uri = make_uri()
        s = self.r.ChannelServer()
        s.listen(uri=uri)
        # pylint: disable=protected-access
        s.tst1_ModuleTest.interfaceTest._rpcs.fun.impl = rpc_impl_fun
        c = self.r.connect(uri)
        self.assertEqual(21, c.tst1_ModuleTest.interfaceTest.fun().val)

        # load the same plugin twice, it should be ok
        self.p.load_dso('plugin_the_return', plugin_file)

        # the two packages and module should still be loaded
        self.assertTrue(hasattr(self.p, 'tst1'),
                        'package "tst1" should be loaded')
        self.assertTrue(hasattr(self.r, 'tst1'),
                        'package "tst1" should be loaded')
        self.assertTrue(hasattr(self.p, 'test'),
                        'package "test" should be loaded')
        self.assertTrue(hasattr(self.r, 'test'),
                        'package "test" should be loaded')
        c = self.r.connect(uri)
        self.assertEqual(21, c.tst1_ModuleTest.interfaceTest.fun().val)

        # unload additional plugin
        self.p.unload_dso('plugin')

        # the two packages and module should still be loaded
        self.assertTrue(hasattr(self.p, 'tst1'),
                        'package "tst1" should be loaded')
        self.assertTrue(hasattr(self.r, 'tst1'),
                        'package "tst1" should be loaded')
        self.assertTrue(hasattr(self.p, 'test'),
                        'package "test" should be loaded')
        self.assertTrue(hasattr(self.r, 'test'),
                        'package "test" should be loaded')
        c = self.r.connect(uri)
        self.assertEqual(21, c.tst1_ModuleTest.interfaceTest.fun().val)

        # unload additional plugin
        self.p.unload_dso('plugin_the_return')

        # package test should still be loaded but not tst1
        self.assertTrue(hasattr(self.p, 'test'),
                        'package "test" should be loaded')
        self.assertTrue(hasattr(self.r, 'test'),
                        'package "test" should be loaded')
        self.assertFalse(hasattr(self.p, 'tst1'),
                         'package "tst1" should not be loaded')
        self.assertFalse(hasattr(self.r, 'tst1'),
                         'package "tst1" should not be loaded')

        # module tst1_ModuleTest should be deleted
        c = self.r.connect(uri)
        self.assertFalse(hasattr(c, 'tst1_ModuleTest'),
                         'module "tst1_ModuleTest" should not be loaded')


@z.ZGroup
class IopyCompatibilityTests(z.TestCase):
    """Comaptibility tests with previous versions of IOPy"""

    def setUp(self):
        plugin_file = os.path.join(TEST_PATH, 'test-iop-plugin.so')
        self.p = iopy.Plugin(plugin_file)

    def test_iface_name(self):
        """Test iface __name__ works as a property and as a method"""
        iface = self.p.test.interfaces.InterfaceA
        self.assertEqual(iface.__name__,   'test.InterfaceA')
        self.assertEqual(iface.__name__(), 'test.InterfaceA')

    def test_iface_types(self):
        """Test interfaces are child types of iopy.IfaceBase and iopy.Iface.
        """
        iface = self.p.test.interfaces.InterfaceA
        self.assertTrue(issubclass(iface, iopy.IfaceBase))
        self.assertTrue(issubclass(iface, iopy.Iface))

    def test_init_with_type_argument(self):
        """Test we can init types with field set up to another type.

        This keeps some compatibility with IOPyV1 where types where
        instances and not real python types.
        """
        a = self.p.test.StructA(a=self.p.test.ClassA)
        self.assertEqual(a.a.field1, 0)

    def test_invalid_int_no_exceptions(self):
        """Test setting an invalid int does not raise an exception"""
        v = (1 << 64) - 3
        d = self.p.test.StructD(i=v)
        self.assertEqual(d.i, v)
        self.assertIsNotNone(str(d))

    def test_enum_field_cast_not_done_at_init(self):
        """Test enum cast from string an int is not done directly when setting
        the field, but when the conversion to C is done"""
        a1 = self.p.test.StructA(e='A')
        self.assertEqual(a1.e, 'A')
        a2 = self.p.test.StructA(e=1)
        self.assertEqual(a2.e, 1)

        self.assertEqual(a1, a2)

        a3 = self.p.test.StructA(_json=str(a1))
        self.assertEqual(a3.e, self.p.test.EnumA('A'))
        self.assertEqual(a3, a1)
        self.assertEqual(a3, a2)

    def test_type_vars(self):
        """Test vars keys for iopy types.

        Optional fields are skipped."""
        class_b_keys = set(('field1', 'field2'))
        self.assertEqual(set(vars(self.p.test.ClassB).keys()), class_b_keys)

        struct_b_keys = set(('a', 'b', 'tab'))
        self.assertEqual(set(vars(self.p.test.StructB).keys()), struct_b_keys)

        union_a_keys = set(('i', 'a', 'tab', 's'))
        self.assertEqual(set(vars(self.p.test.UnionA).keys()), union_a_keys)


if __name__ == "__main__":
    z.main()
