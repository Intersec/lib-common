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

#include <math.h>

#include "z.h"
#include "iop-yaml.h"
#include "iop/tstiop.iop.h"

/* {{{ IOP testing helpers */

static int t_z_yaml_pack_struct(const iop_struct_t *st, const void *v,
                                unsigned flags, const char *info, sb_t *out)
{
    /* XXX: Use a small t_sb here to force a realloc during (un)packing
     *      and detect possible illegal usage of the t_pool in the
     *      (un)packing functions. */
    t_sb_init(out, 10);

    /* packing */
    Z_ASSERT_N(iop_ypack(st, v, iop_sb_write, out, flags),
               "YAML packing failure for (%s, %s)", st->fullname.s, info);

    Z_HELPER_END;
}

#if 0
static int iop_yaml_test_struct(const iop_struct_t *st, void *v,
                                const char *info)
{
    t_scope;
    pstream_t ps;
    uint8_t buf1[20], buf2[20];
    sb_t sb;
    void *res = NULL;
    SB_1k(err);

    Z_HELPER_RUN(t_z_yaml_pack_struct(st, v, 0, info, &sb));

    /* unpacking */
    ps = ps_initsb(&sb);
    Z_ASSERT_N(t_iop_yunpack_ptr_ps(&ps, st, &res, &err),
               "YAML unpacking error (%s, %s): %pL", st->fullname.s,
               info, &err);

    /* check hashes equality */
    iop_hash_sha1(st, v,   buf1, 0);
    iop_hash_sha1(st, res, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "YAML packing/unpacking hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    Z_HELPER_END;
}

static int iop_yaml_test_yaml(const iop_struct_t *st, const char *yaml,
                              const void *expected, const char *info)
{
    t_scope;
    pstream_t ps;
    void *res = NULL;
    uint8_t buf1[20], buf2[20];
    SB_1k(err);

    ps = ps_initstr(yaml);
    Z_ASSERT_N(t_iop_yunpack_ptr_ps(&ps, st, &res, &err),
               "YAML unpacking error (%s, %s): %pL", st->fullname.s, info,
               &err);

    /* check hashes equality */
    iop_hash_sha1(st, res,      buf1, 0);
    iop_hash_sha1(st, expected, buf2, 0);
    Z_ASSERT_EQUAL(buf1, sizeof(buf1), buf2, sizeof(buf2),
                   "YAML unpacking hashes don't match! (%s, %s)",
                   st->fullname.s, info);

    /* TODO: test pack unpack from file */

    Z_HELPER_END;
}
#endif

static int
iop_yaml_test_unpack_error(const iop_struct_t *st, const char *yaml,
                           const char *expected_err)
{
    t_scope;
    pstream_t ps;
    void *res = NULL;
    int ret;
    SB_1k(err);

    ps = ps_initstr(yaml);
    ret = t_iop_yunpack_ptr_ps(&ps, st, &res, &err);
    Z_ASSERT_NEG(ret, "YAML unpacking unexpected success");
    Z_ASSERT_STREQUAL(err.data, expected_err);

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

    Z_HELPER_RUN(t_z_yaml_pack_struct(st, value, flags, "pack", &sb));
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

        TST_FLAGS(0, true, true,
                  "def: 1\n"
                  "rep: ~");
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
        TST_FLAGS(flags, true, true,
                  "priv: 12\n"
                  "def: 1");
        TST_FLAGS(flags | IOP_JPACK_SKIP_PRIVATE, false, false,
                  "def: 1");

#undef TST_FLAGS
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
        const char *err;

#define TST_ERROR(_yaml, _error)                                             \
        Z_HELPER_RUN(iop_yaml_test_unpack_error(&tstiop__full_opt__s,        \
                                                (_yaml), (_error)))

#define ERR_COMMON  \
        "cannot unpack YAML as object of type `tstiop.FullOpt`"


        /* --- Type mismatches --- */

        /* nil -> scalar */
        TST_ERROR("d: ~",
                  "1:4: "ERR_COMMON": cannot set field `d`: "
                  "cannot set a nil value in a field of type double");
        /* string -> scalar */
        TST_ERROR("d: str",
                  "1:4: "ERR_COMMON": cannot set field `d`: "
                  "cannot set a string value in a field of type double");
        /* double -> scalar */
        TST_ERROR("s: 4.2",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set a double value in a field of type string");
        /* uint -> scalar */
        TST_ERROR("s: 42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set an unsigned integer value in a field of type "
                  "string");
        /* int -> scalar */
        TST_ERROR("s: -42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set an integer value in a field of type string");
        /* seq -> scalar */
        TST_ERROR("s: - 42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set a sequence in a non-array field");
        /* seq -> struct */
        TST_ERROR("st: - 42",
                  "1:5: "ERR_COMMON": cannot set field `st`: "
                  "cannot unpack YAML as a `tstiop.TestStruct` IOP struct: "
                  "cannot unpack a sequence into a struct");
        /* obj -> scalar */
        TST_ERROR("s: a: 42",
                  "1:4: "ERR_COMMON": cannot set field `s`: "
                  "cannot set an object in a field of type string");

        /* --- OOB --- */

        /* byte */
        err = "1:5: "ERR_COMMON": cannot set field `i8`: "
              "the value is out of range for the field of type byte";
        TST_ERROR("i8: 128", err);
        TST_ERROR("i8: -129", err);

        /* ubyte */
        err = "1:5: "ERR_COMMON": cannot set field `u8`: "
              "the value is out of range for the field of type ubyte";
        TST_ERROR("u8: 256", err);
        TST_ERROR("u8: -1", err);

        /* short */
        err = "1:6: "ERR_COMMON": cannot set field `i16`: "
              "the value is out of range for the field of type short";
        TST_ERROR("i16: 32768", err);
        TST_ERROR("i16: -32769", err);

        /* ushort */
        err = "1:6: "ERR_COMMON": cannot set field `u16`: "
              "the value is out of range for the field of type ushort";
        TST_ERROR("u16: 65536", err);
        TST_ERROR("u16: -1", err);

        /* int */
        err = "1:6: "ERR_COMMON": cannot set field `i32`: "
              "the value is out of range for the field of type int";
        TST_ERROR("i32: 2147483648", err);
        TST_ERROR("i32: -2147483649", err);

        /* uint */
        err = "1:6: "ERR_COMMON": cannot set field `u32`: "
              "the value is out of range for the field of type uint";
        TST_ERROR("u32: 4294967296", err);
        TST_ERROR("u32: -1", err);

        /* long */
        err = "1:6: "ERR_COMMON": cannot set field `i64`: "
              "the value is out of range for the field of type long";
        TST_ERROR("i64: 9223372036854775808", err);

        /* ulong */
        err = "1:6: "ERR_COMMON": cannot set field `u64`: "
              "the value is out of range for the field of type ulong";
        TST_ERROR("u64: -1", err);

        /* --- object field errors --- */

        /* unknown field in struct */
        TST_ERROR("st: z: 42",
                  "1:5: "ERR_COMMON": cannot set field `st`: "
                  "cannot unpack YAML as a `tstiop.TestStruct` IOP struct: "
                  "unknown field `z`");
        /* missing field in struct */
        TST_ERROR("st: i: 42",
                  "1:5: "ERR_COMMON": cannot set field `st`: "
                  "cannot unpack YAML as a `tstiop.TestStruct` IOP struct: "
                  "missing field `s`");

#undef ERR_COMMON
#undef TST_ERROR
    } Z_TEST_END;
    /* }}} */

    MODULE_RELEASE(iop_yaml);
} Z_GROUP_END;

/* LCOV_EXCL_STOP */
