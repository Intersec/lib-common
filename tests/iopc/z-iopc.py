#!/usr/bin/env python3
#vim:set fileencoding=utf-8:
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

import os
import json

SELF_PATH = os.path.dirname(__file__)
TEST_PATH = os.path.join(SELF_PATH, 'testsuite')
IOPC = os.path.join(SELF_PATH, '../../src/iopc/iopc')

import zpycore as z
import subprocess

@z.ZGroup
class IopcTest(z.TestCase):

    # {{{ Helpers

    def run_iopc(self, iop, expect_pass, errors, lang='',
                 class_id_range='', additional_args=None):
        iopc_args = [IOPC, os.path.join(TEST_PATH, iop)]

        # in case of expected success if no language is specified
        # the success must be for all the languages
        if expect_pass and not lang:
            lang = 'C,json'

        # specific language(s)
        if lang:
            iopc_args.append('-l' + lang)
        else:
            iopc_args.append('-lc')

        if class_id_range:
            iopc_args.append('--class-id-range')
            iopc_args.append(class_id_range)

        iopc_args.append('--Wextra')

        if additional_args:
            iopc_args.extend(additional_args)

        with subprocess.Popen(iopc_args, stderr=subprocess.PIPE) as iopc_p:
            self.assertIsNotNone(iopc_p)
            output = iopc_p.communicate()[1]

            context = "when executing %s" % ' '.join(iopc_args)

            if (expect_pass):
                self.assertEqual(iopc_p.returncode, 0,
                                 "unexpected failure (%d) on %s %s: %s"
                                 % (iopc_p.returncode, iop, context, output))
            else:
                self.assertEqual(iopc_p.returncode, 255,
                                 "unexpected return code %d on %s %s: %s"
                                 % (iopc_p.returncode, iop, context, output))

        if (errors):
            if isinstance(errors, str):
                errors = [errors]
            for error in errors:
                output = str(output)
                self.assertTrue(output.find(error) >= 0,
                                "did not find '%s' in '%s' %s" \
                                % (error, output, context))
        else:
            self.assertTrue(len(output) == 0,
                            "unexpected output: %s" % output)

    def run_iopc_pass(self, iop, lang='', class_id_range=''):
        self.run_iopc(iop, True, None, lang, class_id_range)

    # compilation will fail
    def run_iopc_fail(self, iop, errors, lang='', class_id_range=''):
        self.run_iopc(iop, False, errors, lang, class_id_range)

    @staticmethod
    def get_iop_json(iop):
        with open(os.path.join(TEST_PATH, iop+'.json'),
                  encoding='utf-8') as f:
            return json.load(f)

    def run_gcc(self, iop, expect_pass=True):
        iop_c = iop + '.c'
        gcc_args = ['gcc', '-c', '-o', '/dev/null', '-std=gnu99',
                    '-O', '-Wall', '-Werror', '-Wextra',
                    '-Wno-error=deprecated-declarations',
                    '-Wchar-subscripts', '-Wshadow',
                    '-Wwrite-strings', '-Wsign-compare', '-Wunused',
                    '-Wno-unused-parameter', '-Wuninitialized', '-Winit-self',
                    '-Wpointer-arith', '-Wredundant-decls',
                    '-Wformat-nonliteral', '-Wno-format-y2k',
                    '-Wmissing-format-attribute', '-Wstrict-prototypes',
                    '-Wmissing-prototypes', '-Wmissing-declarations',
                    '-Wnested-externs', '-Wdeclaration-after-statement',
                    '-Wno-format-zero-length', '-Wno-uninitialized',
                    '-D_GNU_SOURCE',
                    '-I' + os.path.join(SELF_PATH, '../../src/compat'),
                    '-I' + os.path.join(SELF_PATH, '../../'),
                    os.path.join(TEST_PATH, iop_c) ]

        with subprocess.Popen(gcc_args, stderr=subprocess.PIPE) as gcc_p:
            self.assertIsNotNone(gcc_p)
            _, err = gcc_p.communicate()

            if expect_pass:
                self.assertEqual(
                    gcc_p.returncode, 0,
                    "unexpected failure (%d) on %s when executing:\n"
                    "%s:\n%s" % (gcc_p.returncode, iop_c,
                                 ' '.join(gcc_args), err)
                )
            else:
                self.assertNotEqual(gcc_p.returncode, 0)

    def check_file(self, file_name, string_list, wanted = True):
        with open(os.path.join(TEST_PATH, file_name), encoding='utf-8') as f:
            content = f.read()

        for s in string_list:
            if wanted:
                self.assertTrue(content.find(s) >= 0,
                                "did not find '%s' in '%s'" % (s, file_name))
            else:
                self.assertTrue(content.find(s) < 0,
                                "found '%s' in '%s'" % (s, file_name))

    def check_ref(self, pkg, lang):
        self.assertEqual(subprocess.call(['diff', '-u',
                                          pkg + '.iop.' + lang,
                                          pkg + '.ref' + "." + lang ]), 0)

    # }}}

    # {{{ "Circular" tests

    def test_circular_type_valid(self):
        f = 'circular_type_valid.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_circular_type_invalid(self):
        self.run_iopc_fail('circular_type_invalid.iop', 'circular dependency')

    def test_circular_pkg_valid(self):
        self.run_iopc_pass('pkg_a.iop')
        self.run_iopc_pass('pkg_b.iop')
        self.run_iopc_pass('pkg_c.iop')
        self.run_gcc('pkg_a.iop')
        self.run_gcc('pkg_b.iop')
        self.run_gcc('pkg_c.iop')

    def test_circular_pkg_invalid(self):
        self.run_iopc_fail('circular_pkg_a.iop', 'circular dependency')
        self.run_iopc_fail('circular_pkg_b.iop', 'circular dependency')
        self.run_iopc_fail('circular_pkg_c.iop', 'circular dependency')

    # }}}
    # {{{ Enums

    def test_prefix_enum(self):
        self.run_iopc_pass('enum1.iop')
        f = 'enum2.iop'
        self.run_iopc(f, True, None)
        self.run_gcc(f)

    def test_dup_enum(self):
        self.run_iopc('enum3.iop', False, 'enum field name `A` is used twice')
        self.run_iopc('enum4.iop', False, 'enum field name `A` is used twice')

    def test_ambiguous_enum(self):
        self.run_iopc_pass('enum1.iop')
        f = 'enum5.iop'
        self.run_iopc(f, True,
                      'enum field identifier `MY_ENUM_A` is ambiguous')
        self.run_gcc(f)

    def test_deprecated_enum(self):
        self.run_iopc('enum6.iop', True, None)

    def test_invalid_comma_enum(self):
        self.run_iopc('enum_invalid_comma.iop', False,
                      '`,` expected on every line')

    # }}}
    # {{{ SNMP

    def test_snmp_struct_obj(self):
        f = 'snmp_obj.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_snmp_struct_obj_2(self):
        f = 'snmp_obj2.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_snmp_valid_iface(self):
        f = 'snmp_valid_iface.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_snmp_valid_iface_params_from_diff_pkg(self):
        f = 'snmp_valid_params_from_different_pkg.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_snmp_invalid_use_out_throw_snmp_iface(self):
        self.run_iopc('snmp_invalid_iface1.iop', False,
                      'snmpIface cannot out and/or throw')

    def test_snmp_invalid_params_from_snmp_iface(self):
        self.run_iopc('snmp_invalid_iface2.iop', False,
                      'pkg `snmp_invalid_iface2` does not provide snmpObj '  \
                      '`IncorrectParams` when resolving snmp params of '     \
                      'snmpIface `Notifications`')

    def test_snmp_invalid_several_identic_field_snmp_iface(self):
        self.run_iopc('snmp_invalid_iface3.iop', False,
                      'several snmpObjs given by the attribute '             \
                      'snmpParamsFrom have a field with the same name `c`')

    def test_snmp_invalid_type_fields(self):
        self.run_iopc('snmp_invalid_fields.iop', False,
                      'only int/string/boolean/enum types are handled for '
                      'snmp objects\' fields')

    def test_snmp_invalid_brief_field(self):
        self.run_iopc('snmp_invalid_brief_field.iop', False,
                      'field `a` needs a brief that would be used as a '
                      'description in the generated MIB')

    def test_snmp_invalid_brief_rpc(self):
        self.run_iopc('snmp_invalid_brief_rpc.iop', False,
                      'notification `notif` needs a brief that would be used '
                      'as a description in the generated MIB')

    def test_snmp_invalid_struct_type_for_field(self):
        self.run_iopc('snmp_invalid1.iop', False,
                      'only int/string/boolean/enum types are handled for '
                      'snmp objects\' fields')

    def test_snmp_invalid_snmp_obj_type_for_field(self):
        self.run_iopc('snmp_invalid2.iop', False,
                      'snmp objects cannot be used to define a field type')

    def test_snmp_invalid_index_type(self):
        self.run_iopc('snmp_invalid_index_type.iop', False,
                      "a snmp index should be declared with the 'uint' or "
                      "'string' type")

    def test_snmp_invalid_index(self):
        self.run_iopc('snmp_invalid_index.iop', False,
                      "field 'st1' does not support @snmpIndex attribute")

    def test_snmp_invalid_missing_index(self):
        self.run_iopc('snmp_invalid_missing_index.iop', False,
                      "each snmp table must contain at least one field that "
                      "has attribute @snmpIndex of type 'uint' or 'string'")

    def test_snmp_invalid_from(self):
        self.run_iopc('snmp_invalid_from.iop', False,
                      "error: invalid snmpParamsFrom `Params.`")

    def test_snmp_valid_tbl(self):
        f = 'snmp_tbl.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_snmp_valid_enum(self):
        f = 'snmp_valid_enum.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    # }}}
#{{{Parsing values

    def test_integer_ext_overflow(self):
        self.run_iopc('integer_ext_overflow.iop', False, 'integer overflow')

    def test_integer_ext_invalid(self):
        self.run_iopc('integer_ext_invalid.iop', False,
                      'invalid integer extension')

    def test_unterminated_string(self):
        self.run_iopc('string_val_unterminated.iop', False,
                      '4:27:unterminated string')

    def test_unterminated_cstring(self):
        self.run_iopc('cstring_val_unterminated.iop', False,
                      '4:28:unterminated cstring')

    def test_unterminated_comment(self):
        self.run_iopc('unterminated_comment.iop', False,
                      '6:0:unterminated comment')

    def test_integer_ext_valid(self):
        b = 'integer_ext_valid'
        self.run_iopc_pass(b + '.iop')
        self.run_gcc(b + '.iop')
        self.check_file(b + '-tdef.iop.h', [
            'VAL_A = 123,', 'VAL_B = 83,', 'VAL_C = 6575,', 'VAL_D = 102400,',
            'VAL_E = 10240,', 'VAL_F = 10485760,', 'VAL_G = -10240,',
            'VAL_H = -2048,', 'VAL_I = 255,', 'VAL_O = 0,'])
        self.check_file(b + '.iop.c', [
            '{ .defval_u64 = 0x8000000000000000 }',
            '{ .defval_u64 = 0xffffffffffffffff }',
            '{ .defval_data = "RST" }'])

    def test_defval(self):
        tests_invalid = [
            {'f' : 'defval_bool_invalid.iop',
             's' : 'invalid default value on bool field'},
            {'f' : 'defval_double_nonzero.iop',
             's' : 'violation of @nonZero constraint'},
            {'f' : 'defval_double_string.iop',
             's' : 'string default value on double field'},
            {'f' : 'defval_enum_strict.iop',
             's' : 'invalid default value on strict enum field'},
            {'f' : 'defval_int_invalid.iop',
             's' : 'invalid default value on integer field'},
            {'f' : 'defval_int_max.iop',
             's' : 'violation of @max constraint'},
            {'f' : 'defval_int_min.iop',
             's' : 'violation of @min constraint'},
            {'f' : 'defval_int_nonzero.iop',
             's' : 'violation of @nonZero constraint'},
            {'f' : 'defval_int_unsigned.iop',
             's' : 'invalid default value on unsigned integer field'},
            {'f' : 'defval_str_invalid.iop',
             's' : 'invalid default value on string field'},
            {'f' : 'defval_str_maxlength.iop',
             's' : 'violation of @maxLength constraint'},
        ]
        for t in tests_invalid:
            self.run_iopc(t['f'], False, t['s'])

        f = 'defval_valid.iop'
        self.run_iopc(f, True, None)
        self.run_gcc(f)

    # }}}
    # {{{ Inheritance

    def test_inheritance_invalid_multiple_inheritance(self):
        self.run_iopc('inheritance_invalid_multiple_inheritance.iop', False,
                      'multiple inheritance is not supported')

    def test_inheritance_invalid_types(self):
        self.run_iopc('inheritance_invalid_types1.iop', False,
                      '`{` expected, but got `:`')
        self.run_iopc('inheritance_invalid_types2.iop', False,
                      '`{` expected, but got `:`')
        self.run_iopc('inheritance_invalid_types3.iop', False,
                      'parent object `Father` is not a class')
        self.run_iopc_fail('inheritance_invalid_types4.iop',
                           'only classes can be abstrac')

    def test_inheritance_invalid_circular1(self):
        self.run_iopc('inheritance_invalid_circular1.iop', False,
                      ['circular dependency',
                       'inheritance_invalid_circular1.iop:3:2:  '           \
                       'from: class A',
                       'class A inherits from class C2',
                       'class C2 inherits from class B',
                       'class B inherits from class A'])

    def test_inheritance_invalid_circular2(self):
        self.run_iopc('inheritance_invalid_circular2_pkg_a.iop', False,
                      'circular dependency')
        self.run_iopc('inheritance_invalid_circular2_pkg_b.iop', False,
                      'circular dependency')

    def test_inheritance_invalid_duplicated_fields(self):
        self.run_iopc('inheritance_invalid_duplicated_fields.iop', False,
                      'field name `a` is also used in child `C`')

    def test_inheritance_invalid_ids(self):
        self.run_iopc('inheritance_invalid_id_small.iop', False,
                      'id is too small (must be >= 0, got -1)')
        self.run_iopc('inheritance_invalid_id_large.iop', False,
                      'id is too large (must be <= 65535, got 65536)')
        self.run_iopc('inheritance_invalid_id_missing.iop', False,
                      'integer expected, but got identifier instead')
        self.run_iopc('inheritance_invalid_id_duplicated.iop', False,
                      '26:10: error: id 3 is also used by class `C1`')

    def test_inheritance_invalid_static(self):
        self.run_iopc('inheritance_invalid_static_struct.iop', False,
                      'static keyword is only authorized for class fields')
        self.run_iopc('inheritance_invalid_static_no_default.iop', False,
                      'static fields of non-abstract classes must '         \
                      'have a default value')
        self.run_iopc('inheritance_invalid_static_repeated.iop', False,
                      'repeated static members are forbidden')
        self.run_iopc('inheritance_invalid_static_optional.iop', False,
                      'optional static members are forbidden')
        self.run_iopc('inheritance_invalid_static_type_mismatch.iop', False,
                      'incompatible type for static field `toto`: '         \
                      'should be `int`')
        self.run_iopc('inheritance_invalid_static_already_defined.iop',
                      False, 'field `toto` already defined by parent `A`')
        self.run_iopc('inheritance_invalid_static_abstract_not_defined.iop',
                      False, 'error: class `D1` must define a static '      \
                      'field named `withoutDefval`')

    def test_inheritance_class_id_ranges(self):
        self.run_iopc_fail('', 'invalid class-id-range',
                           class_id_range = '10')
        self.run_iopc_fail('', 'invalid class-id-range',
                           class_id_range = '-10')
        self.run_iopc_fail('', 'invalid class-id-range',
                           class_id_range = '-10-10')
        self.run_iopc_fail('', 'invalid class-id-range',
                           class_id_range = '65536-10')
        self.run_iopc_fail('', 'invalid class-id-range',
                           class_id_range = '10-9')
        self.run_iopc_fail('', 'invalid class-id-range',
                           class_id_range = '10-65536')

        self.run_iopc_pass('inheritance_pkg_b.iop', class_id_range = '')
        self.run_iopc_pass('inheritance_pkg_b.iop',
                           class_id_range = '10-19')
        self.run_iopc('inheritance_pkg_b.iop', False,
                      'id is too small (must be >= 11, got 10)',
                      class_id_range = '11-19')
        self.run_iopc('inheritance_pkg_b.iop', False,
                      'id is too large (must be <= 18, got 19)',
                      class_id_range = '10-18')

    def test_inheritance(self):
        self.run_iopc_pass('inheritance_pkg_a.iop')
        self.run_iopc_pass('inheritance_pkg_b.iop')
        self.run_iopc_pass('inheritance_valid_local_pkg.iop')
        self.run_iopc_pass('inheritance_pkg_a.iop')
        self.run_iopc_pass('inheritance_pkg_b.iop')
        self.run_iopc_pass('inheritance_valid_local_pkg.iop')
        self.run_gcc('inheritance_pkg_a.iop')
        self.run_gcc('inheritance_pkg_b.iop')

        # Check that ClassContainer fields (which are classes) are pointed
        self.check_file('inheritance_pkg_a-t.iop.h', string_list = [
            'inheritance_pkg_a__a1__t *nonnull class_container_a1;',
            'inheritance_pkg_a__b1__t *nullable class_container_b1;',
            'inheritance_pkg_a__a2__array_t class_container_a2;'])

        # Check the "same as" feature with inheritance
        self.check_file('inheritance_pkg_b.iop.c', wanted = True,
                        string_list = [
                            'inheritance_pkg_b__a1__desc_fields',
                            'inheritance_pkg_b__a2__desc_fields',
                            'inheritance_pkg_b__a3__desc_fields',
                            'inheritance_pkg_b__a4__desc_fields',
                            'inheritance_pkg_b__a5__desc_fields',
                            'inheritance_pkg_b__a6__class_s',
                            'inheritance_pkg_b__a7__desc_fields',
                            'inheritance_pkg_b__a9__desc_fields',
                            'inheritance_pkg_b__a10__desc_fields',
                            'same as inheritance_pkg_b.A5',
                            'same as inheritance_pkg_b.A7'])
        self.check_file('inheritance_pkg_b.iop.c', wanted = False,
                        string_list = [
                            'same as inheritance_pkg_b.A1',
                            'same as inheritance_pkg_b.A3',
                            'same as inheritance_pkg_b.A9',
                            'inheritance_pkg_b__a6__desc_fields',
                            'inheritance_pkg_b__a8__desc_fields'])

    def test_inheritance_invalid_local_pkg(self):
        self.run_iopc('inheritance_invalid_local_pkg.iop', False,
                      'as the parent class `Local1` of class `NonLocal`'
                      ' is `local`, both classes need to be'
                      ' in the same package')

    def test_inheritance_tag_static_invalid(self):
        self.run_iopc('inheritance_tag_static_invalid.iop', False,
                      'tag is not authorized for static class fields')

    # }}}
    # {{{ Typedef

    @z.ZFlags('redmine_50352', 'redmine_76975')
    def test_typedef_valid(self):
        f  = 'typedef_valid_no_class.iop'
        f1 = 'typedef_valid.iop'
        f2 = 'typedef1.iop'
        f3 = 'typedef2.iop'

        self.run_iopc(f, True, None)
        self.run_iopc(f1, True, None)
        self.run_iopc(f2, True, None)
        self.run_iopc(f3, True, None)

        self.run_gcc(f)
        self.run_gcc(f1)
        self.run_gcc(f2)
        self.run_gcc(f3)

    @z.ZFlags('redmine_50352', 'redmine_76975')
    def test_typedef_invalid(self):
        self.run_iopc('typedef_invalid_1.iop', False,
                      'unable to find any pkg providing type `MyStruct`')
        self.run_iopc('typedef_invalid_2.iop', False,
                      'attribute ctype does not apply to typedefs')
        self.run_iopc('typedef_invalid_3.iop', False,
                      'attribute pattern does not apply to integer')
        self.run_iopc('typedef_invalid_6.iop', False,
                      'recursive typedef for type `MyTypedef1` in pkg '      \
                      '`typedef_invalid_6`')
        self.run_iopc('typedef_invalid_7.iop', False,
                      'cannot declare repeated optional fields')
        self.run_iopc('typedef_invalid_8.iop', False,
                      'optional members are forbidden in union types')
        self.run_iopc('typedef_invalid_9.iop', False,
                      'cannot declare repeated optional fields')
        self.run_iopc('typedef_invalid_10.iop', False,
                      'cannot declare repeated optional fields')
        self.run_iopc('typedef_invalid_11.iop', False,
                      'repeated members are forbidden in union types')
        # typedef_invalid_12.iop has been removed as we now allow inheritance
        # from a typedef, test made through 'typedef_valid.iop' parsing and
        # compilation
        self.run_iopc('typedef_invalid_13.iop', False,
                      'attribute minOccurs does not apply to required '      \
                      'typedefs')
        self.run_iopc('typedef_invalid_14.iop', False,
                      'cannot declare repeated optional fields')
        self.run_iopc('typedef_invalid_15.iop', False,
                      'cannot declare repeated optional fields')
        self.run_iopc('invalid_union_empty.iop', False,
                      'a union must contain at least one field')

    @z.ZFlags('redmine_8536')
    def test_void_types(self):
        self.run_iopc_pass('void_in_union.iop', lang="C,json")
        self.run_gcc('void_in_union.iop')

        # void tags have a defined value to make IOP_UNION_SET_V safe
        self.run_iopc_pass('void_in_union.iop', lang="C")
        self.run_gcc('void_in_union_field_def')

        self.run_iopc_pass('void_mandatory_in_struct.iop', lang="C,json")
        self.run_gcc('void_mandatory_in_struct.iop')
        self.run_iopc_pass('void_optional_in_struct.iop', lang="C,json")
        self.run_gcc('void_optional_in_struct.iop')
        self.run_iopc('invalid_void_repeated.iop', False,
                      'repeated void types are forbidden')
        self.run_iopc('invalid_void_default.iop', False,
                      'default values are forbidden for void types')
        self.run_iopc_pass('void_opt_rpc_arg.iop', lang="C,json")
        self.run_iopc('invalid_void_req_rpc_arg.iop', False,
                      'required void types are forbidden for rpc arguments')

    # }}}
    # {{{ Attributes

    def test_attrs_valid(self):
        f = 'attrs_valid.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)

    def test_attrs_valid_enum_aliases_headers(self):
        f = 'attrs_valid.iop'
        self.run_iopc_pass(f)
        self.run_gcc('enum_aliases')

    def test_attrs_valid_enum_aliases_json(self):
        f = 'attrs_valid.iop'
        self.run_iopc_pass(f)
        iop_json = self.get_iop_json(f)
        for obj in iop_json['objects']:
            if obj['fullName'] == 'attrs_valid.MyEnumB':
                self.assertEqual(obj['values']['A'],
                                 {'value': 0, 'aliases': ['FOO', 'BAR']})

    def test_attrs_valid_v5(self):
        f = 'attrs_valid_v5'
        self.run_iopc_pass(f + '.iop', 'C,json')
        self.run_gcc(f + '.iop')
        g = os.path.join(TEST_PATH, f)
        for lang in ['json', 'c']:
            self.check_ref(g, lang)

    def test_attrs_multi_valid(self):
        f = 'attrs_multi_valid.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)
        path_base = os.path.join(TEST_PATH,
                                 'attrs_multi_valid.iop.c')
        path_ref = os.path.join(TEST_PATH,
                                'reference_attrs_multi_valid.c')
        with open(path_base, "r", encoding='utf-8') as ref_base:
            with open(path_ref, "r", encoding='utf-8') as ref:
                self.assertEqual(ref.read(), ref_base.read())

    def test_attrs_multi_constraints(self):
        f = 'attrs_multi_constraints.iop'
        self.run_iopc_pass(f)
        self.run_gcc(f)
        path_base = os.path.join(TEST_PATH, 'attrs_multi_constraints.iop.c')
        path_ref = os.path.join(TEST_PATH,
                                'reference_attrs_multi_constraints.c')
        with open(path_base, "r", encoding='utf-8') as ref_base:
            with open(path_ref, "r", encoding='utf-8') as ref:
                self.assertEqual(ref.read(), ref_base.read())

    def test_attrs_invalid_1(self):
        self.run_iopc('attrs_invalid_1.iop', False,
                      'incorrect attribute name')

    def test_attrs_invalid_2(self):
        self.run_iopc('attrs_invalid_2.iop', False,
                      'attribute ctype does not apply to fields')

    def test_attrs_invalid_3(self):
        self.run_iopc('attrs_invalid_3.iop', False,
                      'attribute ctype does not apply to interface')

    def test_attrs_invalid_4(self):
        self.run_iopc('attrs_invalid_4.iop', False,
                      'attribute private does not apply to required fields')

    def test_attrs_invalid_5(self):
        self.run_iopc('attrs_invalid_5.iop', False,
                      'attribute prefix does not apply to fields')

    def test_attrs_invalid_6(self):
        self.run_iopc('attrs_invalid_6.iop', False,
                      'attribute minOccurs does not apply to fields with '  \
                      'default value')

    def test_attrs_invalid_7(self):
        self.run_iopc('attrs_invalid_7.iop', False,
                      'attribute maxOccurs does not apply to required '     \
                      'fields')

    def test_attrs_invalid_8(self):
        self.run_iopc('attrs_invalid_8.iop', False,
                      'unknown field c in MyUnion')

    def test_attrs_invalid_9(self):
        self.run_iopc('attrs_invalid_9.iop', False,
                      'unknown field c in MyUnion')

    def test_attrs_invalid_10(self):
        self.run_iopc('attrs_invalid_10.iop', False,
                      'cannot use both @allow and @disallow '                \
                      'on the same field')

    def test_attrs_invalid_11(self):
        self.run_iopc('attrs_invalid_11.iop', False,
                      'unknown field c in MyUnion')

    def test_attrs_invalid_12(self):
        self.run_iopc('attrs_invalid_12.iop', False,
                      'unknown field c in MyUnion')

    def test_attrs_invalid_13(self):
        self.run_iopc('attrs_invalid_13.iop', False,
                      'unknown field C in MyEnum')

    def test_attrs_invalid_14(self):
        self.run_iopc('attrs_invalid_14.iop', False,
                      'unknown field C in MyEnum')

    def test_attrs_invalid_15(self):
        self.run_iopc('attrs_invalid_15.iop', False,
                      'invalid default value on enum field: 1')

    def test_attrs_invalid_16(self):
        self.run_iopc('attrs_invalid_16.iop', False,
                      'invalid default value on enum field: 0')

    def test_attrs_invalid_17(self):
        f = 'attrs_invalid_17.iop'
        self.run_iopc(f, False,
                      'all static attributes must be declared first')

    def test_attrs_invalid_18(self):
        f = 'attrs_invalid_18.iop'
        self.run_iopc(f, False,
                      'error: invalid ctype `user_t`: missing __t suffix')

    def test_attrs_invalid_enumval(self):
        self.run_iopc('attrs_invalid_enumval.iop', False,
                      'invalid attribute min on enum field')

    def test_attrs_invalid_noreorder(self):
        self.run_iopc('attrs_invalid_noreorder.iop', False,
                      'attribute noReorder does not apply to union')

    def test_attrs_invalid_private_struct(self):
        f = 'attrs_invalid_private_struct.iop'
        self.run_iopc(f, False,
                      'attribute private does not apply to struct')

    def test_attrs_invalid_private_union(self):
        f = 'attrs_invalid_private_union.iop'
        self.run_iopc(f, False,
                      'attribute private does not apply to union')

    def test_attrs_invalid_private_enum(self):
        f = 'attrs_invalid_private_enum.iop'
        self.run_iopc(f, False,
                      'attribute private does not apply to enum')

    def test_attrs_invalid_enum_alias(self):
        f = 'attrs_invalid_enum_alias.iop'
        self.run_iopc(f, False, 'enum field alias `ENUM0` is used twice')
        f = 'attrs_invalid_enum_alias2.iop'
        self.run_iopc(f, False, 'enum field name `ENUM1` is used twice')
        f = 'attrs_invalid_enum_alias3.iop'
        self.run_iopc(f, False, 'enum field name `ENUM0` is used twice')

    def test_attrs_invalid_max_1(self):
        f = 'attrs_invalid_max_1.iop'
        self.run_iopc(f, False,
                      'attribute max is larger than maximum value of '
                      'type ubyte (256 > 255)')

    def test_attrs_invalid_max_2(self):
        f = 'attrs_invalid_max_2.iop'
        self.run_iopc(f, False,
                      'attribute max is larger than maximum value of '
                      'type int (2147483648 > 2147483647)')

    def test_attrs_valid_max_1(self):
        f = 'attrs_valid_max_1.iop'
        self.run_iopc(f, True, "")

    def test_attrs_invalid_min_1(self):
        f = 'attrs_invalid_min_1.iop'
        self.run_iopc(f, False,
                      'attribute min is lower than minimum value of '
                      'type int (-4294967296 < -2147483648)')

    def test_attrs_invalid_min_2(self):
        f = 'attrs_invalid_min_2.iop'
        self.run_iopc(f, False,
                      'attribute min is larger than maximum value of '
                      'type ubyte (256 > 255)')

    def test_attrs_empty_ctype(self):
        f = 'attrs_empty_ctype.iop'
        self.run_iopc(f, False,
                      'attribute ctype expects at least one argument')

    def test_attrs_bad_ctypes(self):
        f = 'attrs_bad_ctypes.iop'
        self.run_iopc(f, False,
                      'invalid ctype `invalid`: missing __t suffix')

    def test_attrs_bad_nb_args(self):
        f = 'attrs_bad_nb_args.iop'
        self.run_iopc(f, False,
                      'attribute prefix expects 1 arguments, got 0')

    # }}}
    # {{{ Generic attributes

    def test_generic_invalid_value(self):
        self.run_iopc('generic_attrs_invalid_1.iop', False,
                      'unable to parse value for generic argument '          \
                      '\'test:gen1\'')

    def test_generic_invalid_key(self):
        self.run_iopc('generic_attrs_invalid_2.iop', False,
                      'generic attribute name (namespaces:id) expected, '    \
                      'but got identifier instead')

    def test_generic_repeated_key(self):
        self.run_iopc('generic_attrs_invalid_3.iop', False,
                      'generic attribute \'test:gen3\' must be unique for '  \
                      'each IOP object')

    def test_generic_invalid_len(self):
        self.run_iopc('generic_attrs_invalid_4.iop', False,
                      'error: `)` expected, but got `,` instead')

    def test_generic_json(self):
        f = 'json_generic_attributes'
        g = os.path.join(TEST_PATH, f)
        self.run_iopc_pass(f + '.iop', 'C,json')
        self.run_gcc(f + '.iop')
        for lang in ['json', 'c']:
            self.check_ref(g, lang)
        self.run_iopc('json_generic_invalid1.iop', False,
                      'error: `:` expected, but got `)` instead')
        self.run_iopc('json_generic_invalid2.iop', False,
                      'error: invalid token when parsing json value')
        self.run_iopc('json_generic_invalid3.iop', False,
                      'error: string expected, but got `,` instead')
        self.run_iopc('json_generic_invalid4.iop', False,
                      'error: `]` expected, but got integer instead')
        self.run_iopc('json_generic_invalid5.iop', False,
                      'error: `)` expected, but got identifier instead')

    @z.ZFlags('redmine_56755')
    def test_generic_json_unterminated(self):
        self.run_iopc('json_generic_invalid6.iop', False,
                      "4:70:unterminated string")

    def test_generic_invalid_name(self):
        self.run_iopc('generic_attrs_invalid_name.iop', False,
                      "invalid name for generic attribute: `=` is forbidden")


    # }}}
    # {{{ References

    def test_reference_valid(self):
        f  = "reference.iop"
        self.run_iopc(f, True, None)
        self.run_gcc(f)

    def test_reference_invalid(self):
        self.run_iopc('reference_invalid_1.iop', False,
                      'references can only be applied to structures or '     \
                      'unions')
        self.run_iopc('reference_invalid_2.iop', False,
                      'references can only be applied to structures or '     \
                      'unions')
        self.run_iopc('reference_invalid_3.iop', False,
                      'references can only be applied to structures or '    \
                      'unions')
        self.run_iopc('reference_invalid_4.iop', False,
                      'references can only be applied to structures or '     \
                      'unions')
        self.run_iopc('reference_invalid_5.iop', False,
                      'circular dependency')
        self.run_iopc('reference_invalid_6.iop', False,
                      'identifier expected, but got `?` instead')
        self.run_iopc('reference_invalid_7.iop', False,
                      'identifier expected, but got `&` instead')
        self.run_iopc('reference_invalid_8.iop', False,
                      'referenced static members are forbidden')
        self.run_iopc('reference_invalid_9.iop', False,
                      'circular dependency')

    # }}}
    # {{{ Code generation

    def test_json(self):
        f = 'tstjson'
        g = os.path.join(TEST_PATH, f)
        for lang in ['json', 'C,json', 'json,C']:
            subprocess.call(['rm', '-f', g + '.iop.json'])
            self.run_iopc_pass(f + '.iop', lang)
            self.check_ref(g, 'json')

    def test_dox_c(self):
        f = 'tstdox'
        g = os.path.join(TEST_PATH, f)
        subprocess.call(['rm', '-f', g + '.iop.c'])
        self.run_iopc_pass(f + '.iop')
        self.run_gcc(f + '.iop')
        for lang in ['json', 'c']:
            self.check_ref(g, lang)

    def test_gen_c(self):
        f = 'tstgen'
        g = os.path.join(TEST_PATH, f)
        subprocess.call(['rm', '-f', g + '.iop.c'])
        self.run_iopc_pass(f + '.iop')
        self.run_iopc_pass('pkg_a.iop')
        self.run_gcc(f + '.iop')
        self.check_ref(g, 'c')
        self.check_ref(g + '-t', 'h')

    @z.ZFlags('redmine_50352')
    def test_unions_use_enums(self):
        f1 = ('typedef1.iop', 'unions_use_enums')
        f2 = ('attrs_valid.iop', 'unions_use_enums_ctype')

        # failing case
        self.run_iopc_pass(f1[0])
        self.run_gcc(f1[1], expect_pass=False)
        # all field definitions for a ctyped union still work
        self.run_iopc(f2[0], True, None)
        self.run_gcc(f2[1], expect_pass=True)

    # }}}
    # {{{ Various

    def test_default_char_valid(self):
        self.run_iopc_pass('pkg_d.iop')

    def test_same_as(self):
        self.run_iopc_pass('same_as.iop')
        self.run_gcc('same_as.iop')
        self.check_file('same_as.iop.c', wanted = True, string_list = [
            'same_as__struct1__desc_fields', 'same as same_as.Struct1',
            'same_as__union1__desc_fields',  'same as same_as.Union1',
            'same_as__class1__desc_fields',  'same as same_as.Class1',
            'same_as__interface1__bar_args__desc_fields',
            'same_as__interface1__bar_res__desc_fields',
            'same_as__interface1__bar_exn__desc_fields',
            'same as same_as.Interface1.barArgs',
            'same as same_as.Interface1.barRes',
            'same as same_as.Interface1.barExn'])
        self.check_file('same_as.iop.c', wanted = False, string_list = [
            'same_as__struct2__desc_fields',
            'same as same_as.Struct3',
            'same as same_as.Struct4',
            'same_as__union2__desc_fields',
            'same_as__class2__desc_fields',
            'same_as__interface1__foo_args__desc_fields',
            'same_as__interface1__foo_res__desc_fields',
            'same_as__interface1__foo_exn__desc_fields',
            'same_as__interface2__rpc_args__desc_fields',
            'same_as__interface2__rpc_res__desc_fields',
            'same_as__interface2__rpc_exn__desc_fields'])

    def test_struct_noreorder(self):
        f = 'struct_noreorder.iop'
        self.run_iopc(f, True, None)
        self.run_gcc(f)

    def test_unknown_pkg(self):
        self.run_iopc_fail('unknown_pkg.iop', 'unable to find file '
                           '`unknown.iop` in the include path')

    def test_invalid_pkg_syntax(self):
        self.run_iopc_fail('invalid_pkg_syntax.iop',
                           'package expected, but got packag instead')

    def test_unknown_file(self):
        self.run_iopc_fail('unknown_file.iop', 'unable to open file')

    def test_no_out_nor_throw(self):
        self.run_iopc_fail('async_candidate.iop',
                           ("no `out` nor `throw` for function "
                            "`asyncCandidate`"))

    def test_dup_struct(self):
        self.run_iopc('duplicated_struct.iop', False,
                      'error: something named `TopupEvent` already exists')

    def test_empty(self):
        self.run_iopc_fail('empty.iop', 'error: unexpected end of file')

    def test_dox_invalid(self):
        self.run_iopc_fail('tstdox_invalid1.iop',
                           ('doxygen unrelated `in` argument `unknown` for '
                            'RPC `funA`'))
        self.run_iopc_fail('tstdox_invalid2.iop',
                           ('error: invalid identifier when parsing json '
                            'value'))

    def test_check_name(self):
        err_str = 'error: You_Shall_Not_Pass contains a _'

        self.run_iopc_fail('check_name_class.iop', err_str)
        self.run_iopc_fail('check_name_struct.iop', err_str)
        self.run_iopc_fail('check_name_enum.iop', err_str)
        self.run_iopc_fail('check_name_union.iop', err_str)
        self.run_iopc_fail('check_name_interface.iop', err_str)

        self.run_iopc_fail('check_name_field.iop', 'error: identifier '
                           '\'You_Shall_Not_Pass\' contains a _')
        self.run_iopc_fail('check_name_rpc.iop', 'error: you_Shall_Not_Pass '
                           'contains a _')

    def test_missing_iface(self):
        self.run_iopc_fail('missing_iface.iop',
                           'error: unable to find any pkg providing '
                           'interface `MyIfaceA`')

    def test_missing_module(self):
        self.run_iopc_fail('missing_module.iop',
                           'error: unable to find any pkg providing '
                           'module `MyModuleA`')

    @z.ZFlags('redmine_69370')
    def test_error_in_other_pkg(self):
        self.run_iopc_fail('nr_enum_error.iop', [
            'error: identifier expected, but got integer instead'
        ])

    # }}}

if __name__ == "__main__":
    z.main()
