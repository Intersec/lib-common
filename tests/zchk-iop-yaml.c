/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

/* LCOV_EXCL_START */

#include <lib-common/z.h>
#include <lib-common/iop-yaml.h>

#include "iop/tstiop.iop.h"

/* {{{ IOP testing helpers */

static int t_z_yaml_pack_struct(const iop_struct_t *st, const void *v,
                                unsigned flags, sb_t *out)
{
    /* XXX: Use a small t_sb here to force a realloc during (un)packing
     *      and detect possible illegal usage of the t_pool in the
     *      (un)packing functions. */
    t_sb_init(out, 10);

    /* packing */
    if (flags == 0) {
        Z_ASSERT_N(iop_ypack(st, v, iop_sb_write, out),
                   "YAML packing failure for %s", st->fullname.s);
    } else {
        Z_ASSERT_N(iop_ypack_with_flags(st, v, iop_sb_write, out, flags),
                   "YAML packing failure for %s", st->fullname.s);
    }

    Z_HELPER_END;
}

static int
iop_yaml_test_unpack_error(const iop_struct_t *st, const char *yaml,
                           const char *expected_err, bool exact_match)
{
    t_scope;
    pstream_t ps;
    void *res = NULL;
    int ret;
    SB_1k(err);

    ps = ps_initstr(yaml);
    ret = t_iop_yunpack_ptr_ps(&ps, st, &res, &err);
    Z_ASSERT_NEG(ret, "YAML unpacking unexpected success");
    if (exact_match) {
        Z_ASSERT_STREQUAL(err.data, expected_err);
    } else {
        Z_ASSERT(lstr_contains(LSTR_SB_V(&err), LSTR(expected_err)));
    }

    Z_HELPER_END;
}

static int
iop_yaml_test_unpack(const iop_struct_t * nonnull st,
                     const char * nonnull yaml,
                     const char * nullable new_yaml)
{
    t_scope;
    const char *path;
    pstream_t ps;
    void *res = NULL;
    void *file_res = NULL;
    int ret;
    SB_1k(err);
    SB_1k(packed);

    ps = ps_initstr(yaml);
    ret = t_iop_yunpack_ptr_ps(&ps, st, &res, &err);
    Z_ASSERT_N(ret, "YAML unpacking error: %pL", &err);

    Z_HELPER_RUN(t_z_yaml_pack_struct(st, res, 0, &packed));
    Z_ASSERT_STREQUAL(new_yaml ?: yaml, packed.data);

    /* Test iop_ypack_file / t_iop_yunpack_file */
    path = t_fmt("%*pM/tstyaml.yml", LSTR_FMT_ARG(z_tmpdir_g));
    Z_ASSERT_N(iop_ypack_file(path, st, res, &err), "%pL", &err);
    Z_ASSERT_N(t_iop_yunpack_ptr_file(path, st, &file_res, &err),
               "%pL", &err);
    Z_ASSERT_IOPEQUAL_DESC(st, res, file_res);

    Z_HELPER_END;
}

static int iop_yaml_test_pack(const iop_struct_t *st, const void *value,
                              unsigned flags, bool test_unpack,
                              bool must_be_equal, const char *expected)
{
    t_scope;
    t_SB_1k(sb);
    void *unpacked = NULL;
    SB_1k(err);

    Z_HELPER_RUN(t_z_yaml_pack_struct(st, value, flags, &sb));
    Z_ASSERT_STREQUAL(sb.data, expected);

    if (test_unpack) {
        pstream_t ps = ps_initsb(&sb);

        Z_ASSERT_N(t_iop_yunpack_ptr_ps(&ps, st, &unpacked, &err),
                   "YAML unpacking error (%s): %pL", st->fullname.s, &err);
        if (must_be_equal) {
            Z_ASSERT(iop_equals_desc(st, value, unpacked));
        }
    }

    Z_HELPER_END;
}

/* }}} */

/* }}} */

Z_GROUP_EXPORT(iop_yaml)
{
    IOP_REGISTER_PACKAGES(&tstiop__pkg);
    MODULE_REQUIRE(iop_yaml);

    Z_TEST(pack_flags, "test IOP YAML (un)packer flags") { /* {{{ */
        t_scope;
        tstiop__struct_jpack_flags__t st_jpack;
        tstiop__my_class1__t my_class_1;
        tstiop__my_class2__t my_class_2;
        unsigned flags = 0;

        iop_init(tstiop__struct_jpack_flags, &st_jpack);
        iop_init(tstiop__my_class1, &my_class_1);
        iop_init(tstiop__my_class2, &my_class_2);

#define TST_FLAGS(_flags, _test_unpack, _must_be_equal, _exp)                \
        Z_HELPER_RUN(iop_yaml_test_pack(&tstiop__struct_jpack_flags__s,      \
                                        &st_jpack, _flags, _test_unpack,     \
                                        _must_be_equal, _exp))

        /* default is to skip everything optional */
        TST_FLAGS(0, true, true, "~");
        /* NO_WHITESPACES is not valid for YAML */
        TST_FLAGS(IOP_JPACK_NO_WHITESPACES, true, true,
                  "def: 1\n"
                  "rep: ~");
        /* FIXME: NO_TRAILING_EOL to handle */
        TST_FLAGS(IOP_JPACK_NO_TRAILING_EOL, true, true,
                  "def: 1\n"
                  "rep: ~");

        /* SKIP_DEFAULT */
        TST_FLAGS(IOP_JPACK_SKIP_DEFAULT, true, true, "rep: ~");
        st_jpack.def = 2;
        TST_FLAGS(flags | IOP_JPACK_SKIP_DEFAULT, true, true,
                  "def: 2\n"
                  "rep: ~");
        st_jpack.def = 1;

        /* SKIP_EMPTY_ARRAYS */
        TST_FLAGS(flags | IOP_JPACK_SKIP_EMPTY_ARRAYS, true, true,
                  "def: 1");
        st_jpack.rep.tab = &st_jpack.def;
        st_jpack.rep.len = 1;
        TST_FLAGS(flags | IOP_JPACK_SKIP_EMPTY_ARRAYS, true, true,
                  "def: 1\n"
                  "rep:\n"
                  "  - 1");
        st_jpack.rep.len = 0;
        flags |= IOP_JPACK_SKIP_EMPTY_ARRAYS;

        /* SKIP_OPTIONAL_CLASS_NAME */
        st_jpack.my_class = &my_class_1;
        TST_FLAGS(flags, false, true,
                  "def: 1\n"
                  "myClass: !tstiop.MyClass1\n"
                  "  int1: 0");
        TST_FLAGS(flags | IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES, false, true,
                  "def: 1\n"
                  "myClass:\n"
                  "  int1: 0");
        st_jpack.my_class = &my_class_2.super;
        TST_FLAGS(flags | IOP_JPACK_SKIP_OPTIONAL_CLASS_NAMES, false, true,
                  "def: 1\n"
                  "myClass: !tstiop.MyClass2\n"
                  "  int1: 0\n"
                  "  int2: 0");

        /* SKIP_CLASS_NAMES is not valid for YAML */
        TST_FLAGS(flags | IOP_JPACK_SKIP_CLASS_NAMES, false, false,
                  "def: 1\n"
                  "myClass: !tstiop.MyClass2\n"
                  "  int1: 0\n"
                  "  int2: 0");
        st_jpack.my_class = NULL;

        /* SKIP_PRIVATE */
        OPT_SET(st_jpack.priv, 12);
        TST_FLAGS(flags, false, true,
                  "priv: 12\n"
                  "def: 1");
        TST_FLAGS(flags | IOP_JPACK_SKIP_PRIVATE, false, false,
                  "def: 1");

#undef TST_FLAGS
    } Z_TEST_END;
    /* }}} */
    Z_TEST(pack_string, "test IOP YAML string packing") { /* {{{ */
        tstiop__my_union_a__t obj;

        obj = IOP_UNION(tstiop__my_union_a, us, LSTR(""));

#define TST(str, _exp)                                                       \
        do {                                                                 \
            obj.us = LSTR(str);                                              \
            Z_HELPER_RUN(iop_yaml_test_pack(&tstiop__my_union_a__s, &obj, 0, \
                                            true, true, (_exp)));            \
        } while(0)

        /* test cases when packing surrounds the string with quotes */

        /* for empty string */
        TST("", "us: \"\"");

        /* when starting with -, '&', '*' or '!' */
        TST("- muda", "us: \"- muda\"");
        TST("mu - da", "us: mu - da");
        TST("&muda", "us: \"&muda\"");
        TST("mu&da", "us: mu&da");
        TST("*muda", "us: \"*muda\"");
        TST("mu*da", "us: mu*da");
        TST("!muda", "us: \"!muda\"");
        TST("mu!da", "us: mu!da");

        /* when containing ':' or '#' */
        TST(":muda", "us: \":muda\"");
        TST(": muda", "us: \": muda\"");
        TST("mu:da", "us: \"mu:da\"");
        TST("mu: da", "us: \"mu: da\"");
        TST("#muda", "us: \"#muda\"");
        TST("# muda", "us: \"# muda\"");
        TST("mu#da", "us: \"mu#da\"");
        TST("mu# da", "us: \"mu# da\"");

        /* when containing quotes or \X characters */
        TST("mu\"da", "us: mu\"da");
        TST("\"muda", "us: \"\\\"muda\"");
        TST("mu\rda\t", "us: \"mu\\rda\\t\"");
        TST("\a \b \e \f \n \r \t \v",
            "us: \"\\a \\b \\e \\f \\n \\r \\t \\v\"");

        TST("mùda", "us: \"m\\u00f9da\"");

        /* when it would be parsed as something else */
        TST("~", "us: \"~\"");
        TST("null", "us: \"null\"");
        TST("TruE", "us: TruE");
        TST("FalSe", "us: FalSe");

        TST("4.2", "us: 4.2");
        TST("42", "us: 42");
#undef TST
    } Z_TEST_END;
    /* }}} */
    Z_TEST(empty_struct_pack_flags, /* {{{ */
           "test IOP YAML (un)packer flags on empty struct")
    {
        t_scope;
        tstiop__jpack_empty_struct__t empty_jpack;
        tstiop__struct_jpack_flags__t sub_st;
        tstiop__jpack_empty_cls_b__t clsb;
        tstiop__jpack_empty_cls_c__t clsc;
        unsigned flags = IOP_JPACK_MINIMAL;

        iop_init(tstiop__jpack_empty_struct, &empty_jpack);
        iop_init(tstiop__jpack_empty_cls_b, &clsb);
        empty_jpack.sub.cls = &clsb;

#define TST(_flags, _must_be_equal, _exp)                                    \
        Z_HELPER_RUN(iop_yaml_test_pack(&tstiop__jpack_empty_struct__s,      \
                                        &empty_jpack, _flags, false,         \
                                        _must_be_equal, _exp))

        TST(flags, true, "~");

        OPT_SET(empty_jpack.sub.priv, 8);
        TST(flags, true,
            "sub:\n"
            "  priv: 8");
        TST(flags | IOP_JPACK_SKIP_PRIVATE, false, "~");
        OPT_CLR(empty_jpack.sub.priv);

        OPT_SET(empty_jpack.sub.opt, 12);
        TST(flags, true,
            "sub:\n"
            "  opt: 12");
        OPT_CLR(empty_jpack.sub.opt);

        empty_jpack.sub.def = 99;
        TST(flags, true,
            "sub:\n"
            "  def: 99");
        empty_jpack.sub.def = 42;

        empty_jpack.sub.rep.tab = &empty_jpack.sub.def;
        empty_jpack.sub.rep.len = 1;
        TST(flags, true,
            "sub:\n"
            "  rep:\n"
            "    - 42");
        empty_jpack.sub.rep.len = 0;

        OPT_SET(empty_jpack.sub.req_st.opt, 65);
        TST(flags, true,
            "sub:\n"
            "  reqSt:\n"
            "    opt: 65");
        OPT_CLR(empty_jpack.sub.req_st.opt);

        iop_init(tstiop__struct_jpack_flags, &sub_st);
        empty_jpack.sub.opt_st = &sub_st;
        TST(flags, true,
            "sub:\n"
            "  optSt: ~");
        empty_jpack.sub.opt_st = NULL;

        clsb.a = 10;
        TST(flags, true,
            "sub:\n"
            "  cls:\n"
            "    a: 10");
        clsb.a = 1;

        iop_init(tstiop__jpack_empty_cls_c, &clsc);
        empty_jpack.sub.cls = &clsc.super;
        TST(flags, true,
            "sub:\n"
            "  cls: !tstiop.JpackEmptyClsC ~");
        empty_jpack.sub.cls = &clsb;

#undef TST
    } Z_TEST_END;

    /* }}} */
    Z_TEST(unpack_errors, "test IOP YAML unpacking errors") { /* {{{ */
        t_scope;
        const iop_struct_t *st;
        const char *path;
        const char *expected_err;
        void *res = NULL;
        SB_1k(err);

#define TST_ERROR(_yaml, _error)                                             \
        Z_HELPER_RUN(iop_yaml_test_unpack_error(st, (_yaml), (_error), true))

        st = &tstiop__full_opt__s;
#define ERR_COMMON  \
        "cannot unpack YAML as a `tstiop.FullOpt` IOP struct"

        /* --- Type mismatches --- */

        /* null -> scalar */
        TST_ERROR("d: ~",
                  "1:4: "ERR_COMMON": cannot set field `d`: "
                  "cannot set a null value in a field of type double\n"
                  "d: ~\n"
                  "   ^");
        /* string -> scalar */
        TST_ERROR("d: str",
                  "1:4: "ERR_COMMON": cannot set field `d`: "
                  "cannot set a string value in a field of type double\n"
                  "d: str\n"
                  "   ^^^");
        /* double -> scalar */
        TST_ERROR("data: 4.2",
                  "1:7: "ERR_COMMON": cannot set field `data`: "
                  "cannot set a double value in a field of type bytes\n"
                  "data: 4.2\n"
                  "      ^^^");
        /* uint -> scalar */
        TST_ERROR("data: 42",
                  "1:7: "ERR_COMMON": cannot set field `data`: "
                  "cannot set an unsigned integer value in a field of type "
                  "bytes\n"
                  "data: 42\n"
                  "      ^^");
        /* int -> scalar */
        TST_ERROR("s: -42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set an integer value in a field of type string\n"
                  "s: -42\n"
                  "   ^^^");
        /* seq -> scalar */
        TST_ERROR("s: - 42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set a sequence in a non-array field\n"
                  "s: - 42\n"
                  "   ^^^^");
        /* seq -> struct */
        TST_ERROR("st: - 42",
                  "1:5: "ERR_COMMON": cannot set field `st`: "
                  "cannot unpack YAML as a `tstiop.TestStruct` IOP struct: "
                  "cannot unpack a sequence into a struct\n"
                  "st: - 42\n"
                  "    ^^^^");
        /* obj -> scalar */
        TST_ERROR("s: a: 42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set an object in a field of type string\n"
                  "s: a: 42\n"
                  "   ^^^^^");
        /* scalar -> union */
        TST_ERROR("un: true",
                  "1:5: "ERR_COMMON": cannot set field `un`: "
                  "cannot unpack YAML as a `tstiop.TestUnion` IOP union: "
                  "cannot unpack a boolean value into a union\n"
                  "un: true\n"
                  "    ^^^^");

        /* --- OOB --- */

        /* byte */
#define ERR  "1:5: "ERR_COMMON": cannot set field `i8`: "                    \
             "the value is out of range for the field of type byte\n"

        TST_ERROR("i8: 128",
                  ERR
                  "i8: 128\n"
                  "    ^^^");
        TST_ERROR("i8: -129",
                  ERR
                  "i8: -129\n"
                  "    ^^^^");

        /* ubyte */
#undef ERR
#define ERR  "1:5: "ERR_COMMON": cannot set field `u8`: "                    \
             "the value is out of range for the field of type ubyte\n"
        TST_ERROR("u8: 256",
                  ERR
                  "u8: 256\n"
                  "    ^^^");
        TST_ERROR("u8: -1",
                  ERR
                  "u8: -1\n"
                  "    ^^");

        /* short */
#undef ERR
#define ERR  "1:6: "ERR_COMMON": cannot set field `i16`: "                   \
             "the value is out of range for the field of type short\n"
        TST_ERROR("i16: 32768",
                  ERR
                  "i16: 32768\n"
                  "     ^^^^^");
        TST_ERROR("i16: -32769",
                  ERR
                  "i16: -32769\n"
                  "     ^^^^^^");

        /* ushort */
#undef ERR
#define ERR  "1:6: "ERR_COMMON": cannot set field `u16`: "                   \
             "the value is out of range for the field of type ushort\n"
        TST_ERROR("u16: 65536",
                  ERR
                  "u16: 65536\n"
                  "     ^^^^^");
        TST_ERROR("u16: -1",
                  ERR
                  "u16: -1\n"
                  "     ^^");

        /* int */
#undef ERR
#define ERR  "1:6: "ERR_COMMON": cannot set field `i32`: "                   \
             "the value is out of range for the field of type int\n"
        TST_ERROR("i32: 2147483648",
                  ERR
                  "i32: 2147483648\n"
                  "     ^^^^^^^^^^");
        TST_ERROR("i32: -2147483649",
                  ERR
                  "i32: -2147483649\n"
                  "     ^^^^^^^^^^^");

        /* uint */
#undef ERR
#define ERR  "1:6: "ERR_COMMON": cannot set field `u32`: "                   \
             "the value is out of range for the field of type uint\n"
        TST_ERROR("u32: 4294967296",
                  ERR
                  "u32: 4294967296\n"
                  "     ^^^^^^^^^^");
        TST_ERROR("u32: -1",
                  ERR
                  "u32: -1\n"
                  "     ^^");

        /* long */
#undef ERR
#define ERR  "1:6: "ERR_COMMON": cannot set field `i64`: "                   \
             "the value is out of range for the field of type long\n"
        TST_ERROR("i64: 9223372036854775808",
                  ERR
                  "i64: 9223372036854775808\n"
                  "     ^^^^^^^^^^^^^^^^^^^");

        /* ulong */
#undef ERR
#define ERR  "1:6: "ERR_COMMON": cannot set field `u64`: "                   \
             "the value is out of range for the field of type ulong\n"
        TST_ERROR("u64: -1",
                  ERR
                  "u64: -1\n"
                  "     ^^");

#undef ERR

        /* --- object field errors --- */

        /* unknown field in struct */
        TST_ERROR("z: 42",
                  "1:1: "ERR_COMMON": unknown field `z`\n"
                  "z: 42\n"
                  "^^^^^");

        /* missing field in struct */
        /* TODO: weird location? */
        TST_ERROR("st: i: 42",
                  "1:5: "ERR_COMMON": cannot set field `st`: "
                  "cannot unpack YAML as a `tstiop.TestStruct` IOP struct: "
                  "missing field `s`\n"
                  "st: i: 42\n"
                  "    ^^^^^");

        /* --- union errors --- */

        /* use of tag */
        TST_ERROR("un: !tstiop.TestUnion i: 42",
                  "1:5: "ERR_COMMON": cannot set field `un`: "
                  "cannot unpack YAML as a `tstiop.TestUnion` IOP union: "
                  "specifying a tag is not allowed\n"
                  "un: !tstiop.TestUnion i: 42\n"
                  "    ^^^^^^^^^^^^^^^^^^^^^^^");

        /* multiple keys */
        TST_ERROR("un: i: 42\n"
                  "    s: foo",
                  "1:5: "ERR_COMMON": cannot set field `un`: "
                  "cannot unpack YAML as a `tstiop.TestUnion` IOP union: "
                  "a single key must be specified\n"
                  "un: i: 42\n"
                  "    ^ starting here");

        /* wrong keys */
        TST_ERROR("un: a: 42",
                  "1:5: "ERR_COMMON": cannot set field `un`: "
                  "cannot unpack YAML as a `tstiop.TestUnion` IOP union: "
                  "unknown field `a`\n"
                  "un: a: 42\n"
                  "    ^^^^^");

        /* --- enum errors --- */

        /* invalid string */
        TST_ERROR("e: D",
                  "1:4: "ERR_COMMON": cannot set field `e`: "
                  "the value is not valid for the enum `TestEnum`\n"
                  "e: D\n"
                  "   ^");

        /* --- blob errors --- */

        /* invalid b64 value */
        TST_ERROR("data: D",
                  "1:7: "ERR_COMMON": cannot set field `data`: "
                  "the value must be encoded in base64\n"
                  "data: D\n"
                  "      ^");

        /* --- class errors --- */

        /* abstract class */
        /* TODO: improve location */
        TST_ERROR("o: i: 42",
                  "1:4: "ERR_COMMON": cannot set field `o`: "
                  "cannot unpack YAML as a `tstiop.TestClass` IOP struct: "
                  "`tstiop.TestClass` is abstract and cannot be unpacked\n"
                  "o: i: 42\n"
                  "   ^^^^^");

        /* unknown class */
        TST_ERROR("o: !foo\n"
                  "  i: 42",
                  "1:4: "ERR_COMMON": cannot set field `o`: "
                  "cannot unpack YAML as a `tstiop.TestClass` IOP struct: "
                  "unknown type `foo` provided in tag, "
                  "or not a child of `tstiop.TestClass`\n"
                  "o: !foo\n"
                  "   ^ starting here");

        /* unrelated class */
        TST_ERROR("o: !tstiop.MyClass1\n"
                  "  int1: 42",
                  "1:4: "ERR_COMMON": cannot set field `o`: "
                  "cannot unpack YAML as a `tstiop.TestClass` IOP struct: "
                  "unknown type `tstiop.MyClass1` provided in tag, "
                  "or not a child of `tstiop.TestClass`\n"
                  "o: !tstiop.MyClass1\n"
                  "   ^ starting here");

        st = &tstiop__my_class2__s;
#undef ERR_COMMON
#define ERR_COMMON  \
        "cannot unpack YAML as a `tstiop.MyClass2` IOP struct"

        /* same parent but not a child */
        TST_ERROR("!tstiop.MyClass1\n"
                  "int1: 42",
                  "1:1: "ERR_COMMON": "
                  "provided tag `tstiop.MyClass1` is not a child of "
                  "`tstiop.MyClass2`\n"
                  "!tstiop.MyClass1\n"
                  "^ starting here");

        st = &tstiop__struct_jpack_flags__s;
#undef ERR_COMMON
#define ERR_COMMON  \
        "cannot unpack YAML as a `tstiop.StructJpackFlags` IOP struct"

        /* private field */
        TST_ERROR("priv: 42\n",
                  "1:1: "ERR_COMMON": unknown field `priv`\n"
                  "priv: 42\n"
                  "^^^^^^^^");

        /* private class */
        TST_ERROR("myClass: !tstiop.MyClass2Priv\n"
                  "  int1: 4\n"
                  "  int2: 2",
                  "1:10: "ERR_COMMON": cannot set field `myClass`: "
                  "cannot unpack YAML as a `tstiop.MyClass2Priv` IOP struct: "
                  "`tstiop.MyClass2Priv` is private and cannot be "
                  "unpacked\n"
                  "myClass: !tstiop.MyClass2Priv\n"
                  "         ^ starting here");

        /* test unpacking directly as a union */
        st = &tstiop__my_union_a__s;
#undef ERR_COMMON
#define ERR_COMMON  \
        "cannot unpack YAML as a `tstiop.MyUnionA` IOP union"

        /* wrong field */
        TST_ERROR("o: ra\n",
                  "1:1: "ERR_COMMON": unknown field `o`\n"
                  "o: ra\n"
                  "^^^^^");

        /* test an error when unpacking a file: should display the filename */
        path = t_fmt("%*pM/test-data/yaml/invalid_union.yml",
                     LSTR_FMT_ARG(z_cmddir_g));
        Z_ASSERT_NEG(t_iop_yunpack_ptr_file(path, st, &res, &err));
        expected_err = t_fmt("%s:1:1: "ERR_COMMON": unknown field `o`\n"
                             "o: ra\n"
                             "^^^^^", path);
        Z_ASSERT_STREQUAL(err.data, expected_err);

#undef ERR_COMMON
#undef TST_ERROR
    } Z_TEST_END;
    /* }}} */
    Z_TEST(unpack, "test IOP YAML unpacking") { /* {{{ */
#define TST(_st, _yaml)                                                      \
        Z_HELPER_RUN(iop_yaml_test_unpack((_st), (_yaml), NULL))

        TST(&tstiop__my_struct_a__s,
            "a: -1\n"
            "b: 2\n"
            "cOfMyStructA: -3\n"
            "d: 4\n"
            "e: -5\n"
            "f: 6\n"
            "g: -7\n"
            "h: 8\n"
            "htab:\n"
            "  - 9\n"
            "  - 10\n"
            "i: YmxvYg==\n"
            "j: foo\n"
            "xmlField: <b><bar /><foobar attr=\"value\">baz</foobar></b>\n"
            "k: B\n"
            "l:\n"
            "  ua: 11\n"
            "lr:\n"
            "  ub: 12\n"
            "cls2: !tstiop.MyClass3\n"
            "  int1: -13\n"
            "  int2: -14\n"
            "  int3: 15\n"
            "  bool1: true\n"
            "m: 1.5\n"
            "n: false\n"
            "p: 16\n"
            "q: 17\n"
            "r: 18\n"
            "s: 19\n"
            "t: 20"
        );

        /* ~ can be unpacked into a struct */
        TST(&tstiop__my_struct_a_opt__s, "~");

        /* unpacking a class as a base class should work */
        TST(&tstiop__my_class2__s,
            "!tstiop.MyClass3\n"
            "int1: 1\n"
            "int2: 2\n"
            "int3: 3\n"
            "bool1: true\n"
            "string1: a");

        /* Test with a parent with more fields than the child */
        TST(&tstiop__small_child__s,
            "a: a\n"
            "b: b\n"
            "c: c");

#undef TST
    } Z_TEST_END;
    /* }}} */
    Z_TEST(unpack_compat, "test YAML unpacking backward compat") { /* {{{ */
#define TST(_st, _yaml, _new_yaml)                                           \
        Z_HELPER_RUN(iop_yaml_test_unpack((_st), (_yaml), (_new_yaml)))
#define TST_ERROR(_st, _yaml, _error)                                        \
        Z_HELPER_RUN(iop_yaml_test_unpack_error((_st), (_yaml), (_error),    \
                                                false))

        /* a scalar can be unpacked into an array */
        TST(&tstiop__my_struct_a_opt__s,
            "u: 3",
            "u:\n  - 3");
        /* must be of compatible type however */
        TST_ERROR(&tstiop__my_struct_a_opt__s,
                  "u: wry",
                  "cannot set a string value in a field of type int");

#undef TST_ERROR
#undef TST
    } Z_TEST_END;
    /* }}} */
    Z_TEST(constraints, "test IOP constraints") { /* {{{ */
        tstiop__constraint_u__t u;
        tstiop__constraint_s__t s;
        lstr_t string = LSTR("ora");

#define TST_ERROR(st, v, yaml, err)                                          \
    do {                                                                     \
        Z_ASSERT_NEG(iop_check_constraints_desc((st), (v)));                 \
        Z_HELPER_RUN(iop_yaml_test_pack((st), (v), IOP_JPACK_MINIMAL,        \
                                        false, false, (yaml)));              \
        Z_HELPER_RUN(iop_yaml_test_unpack_error((st), (yaml), (err), true)); \
    } while(0)

        /* check constraints are properly checked on unions */
        u = IOP_UNION(tstiop__constraint_u, u8, 0);
        TST_ERROR(&tstiop__constraint_u__s, &u,
            "u8: 0",
            "1:1: cannot unpack YAML as a `tstiop.ConstraintU` IOP union: "
            "field `u8` is invalid: in type tstiop.ConstraintU: "
            "violation of constraint nonZero on field u8\n"
            "u8: 0\n"
            "^^^^^");

        /* check constraints on arrays */
        iop_init(tstiop__constraint_s, &s);
        TST_ERROR(&tstiop__constraint_s__s, &s,
            "~",
            "1:1: cannot unpack YAML as a `tstiop.ConstraintS` IOP struct: "
            "field `s` is invalid: in type tstiop.ConstraintS: "
            "empty array not allowed for field `s`\n"
            "~\n"
            "^");

        /* check constraint on field */
        s.s.tab = &string;
        s.s.len = 1;
        TST_ERROR(&tstiop__constraint_s__s, &s,
            "s:\n"
            "  - ora",
            "1:1: cannot unpack YAML as a `tstiop.ConstraintS` IOP struct: "
            "field `s` is invalid: in type tstiop.ConstraintS: "
            "violation of constraint minOccurs (2) on field s: length=1\n"
            "s:\n"
            "^ starting here");

#undef TST_ERROR
    } Z_TEST_END
    /* }}} */

    MODULE_RELEASE(iop_yaml);
} Z_GROUP_END;

/* LCOV_EXCL_STOP */
