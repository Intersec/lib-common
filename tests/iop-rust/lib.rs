/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

//! IOP Rust tests.
//!
//! This crate tests Rust IOP features. It is here and not directly in `rust/libcommon/iop.rs`
//! so that it can use the IOPs defined in `tests/iop/tstiop.iop`.

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use libcommon::bindings::*;
    waf_cargo_build::include_bindings!();
}

#[cfg(test)]
mod iop_tests {
    use libcommon::iop::{
        CEnum, CStruct, CStructUnion as _, CUnion, Enum as _, Env, StructUnion as _,
    };
    use libcommon::mem_stack::TScope;
    use libcommon::{iop_get, iop_new, iop_set, lstr};
    use std::ptr;

    use crate::bindings::{
        core__pkg, ic__hdr__t, ic__hdr__tag_t, ic__ic_priority__e, ic__ic_priority__t, ic__pkg,
        ic__simple_hdr__t, tstiop__full_opt__t, tstiop__full_ref__t, tstiop__full_repeated__t,
        tstiop__full_required__t, tstiop__get_bpack_sz_en__t, tstiop__get_bpack_sz_st__t,
        tstiop__get_bpack_sz_u__t, tstiop__get_bpack_sz_u__variant, tstiop__my_ref_union__t,
        tstiop__my_ref_union__variant, tstiop__my_referenced_struct__t,
        tstiop__my_referenced_union__t, tstiop__my_referenced_union__variant,
        tstiop__my_union_c__t, tstiop__my_union_c__variant, tstiop__test_class__t,
        tstiop__test_class_child__t, tstiop__test_class_child2__t, tstiop__test_enum__t,
        tstiop__test_struct__t, tstiop__test_union__t, tstiop__test_union__variant,
    };

    // {{{ Test helpers

    fn setup_env_with_packages() -> Env<'static> {
        let mut env = Env::new();
        env.register_packages(&[&raw const ic__pkg, &raw const core__pkg]);
        env
    }

    // }}}
    // {{{ Env lifecycle tests

    #[test]
    fn env_new() {
        let env = Env::new();
        assert!(!env.as_ptr().is_null());
    }

    #[test]
    fn env_default() {
        let env = Env::default();
        assert!(!env.as_ptr().is_null());
    }

    #[test]
    fn env_from_ptr() {
        let env1 = Env::new();
        let ptr = env1.as_ptr().cast_mut();

        /* Create non-owned Env from existing pointer */
        let env2 = Env::from_ptr(ptr);
        assert_eq!(env2.as_ptr(), ptr.cast_const());

        /* env2 is non-owned, so dropping it should not free the underlying env */
        drop(env2);

        /* env1 should still be valid */
        assert!(!env1.as_ptr().is_null());
    }

    #[test]
    fn env_as_ptr() {
        let mut env = Env::new();
        let const_ptr = env.as_ptr();
        let mut_ptr = env.as_mut_ptr();

        assert!(!const_ptr.is_null());
        assert!(!mut_ptr.is_null());
        assert_eq!(const_ptr, mut_ptr.cast_const());
    }

    // }}}
    // {{{ Package registration tests

    #[test]
    fn env_register_packages() {
        let mut env = Env::new();
        env.register_packages(&[&raw const ic__pkg, &raw const core__pkg]);

        /* Verify packages are registered by looking up a known struct */
        let desc = env.get_struct_desc("ic.SimpleHdr");
        assert!(desc.is_some());
    }

    // }}}
    // {{{ Struct descriptor lookup tests

    #[test]
    fn env_get_struct_desc_found() {
        let env = setup_env_with_packages();

        let simple_hdr = env.get_struct_desc("ic.SimpleHdr");
        assert!(simple_hdr.is_some());

        let tracer = env.get_struct_desc("ic.Tracer");
        assert!(tracer.is_some());
    }

    #[test]
    fn env_get_struct_desc_not_found() {
        let env = setup_env_with_packages();

        let unknown = env.get_struct_desc("unknown.NonExistent");
        assert!(unknown.is_none());

        let partial = env.get_struct_desc("ic.NonExistent");
        assert!(partial.is_none());
    }

    // }}}
    // {{{ GenericStructUnion tests

    #[test]
    fn generic_struct_union_new() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let json = r#"{"token": 123, "epoch": 456}"#;

        let obj = env
            .t_junpack_desc(&t_scope, json, desc, 0)
            .expect("valid JSON should unpack");
        assert_eq!(obj.get_cdesc(), desc);
        assert!(!obj.get_cptr().is_null());
    }

    #[test]
    fn generic_struct_union_get_cptr_mut() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let json = r#"{"token": 1, "epoch": 2}"#;

        let mut obj = env
            .t_junpack_desc(&t_scope, json, desc, 0)
            .expect("valid JSON should unpack");
        let cptr = obj.get_cptr();
        let cptr_mut = obj.get_cptr_mut();

        assert_eq!(cptr, cptr_mut.cast_const());
    }

    // }}}
    // {{{ Generic JSON pack/unpack tests

    #[test]
    fn junpack() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let json = r#"{"token": 999, "epoch": 888}"#;

        env.t_junpack_desc(&t_scope, json, desc, 0)
            .expect("valid JSON should unpack");
    }

    #[test]
    fn junpack_invalid_json() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let invalid_json = r#"{"token": "not_a_number"}"#;

        let Err(err) = env.t_junpack_desc(&t_scope, invalid_json, desc, 0) else {
            panic!("expected error for invalid JSON");
        };
        assert_eq!(err, "1:11: cannot parse number `\"not_a_number\"'");
    }

    #[test]
    fn json_roundtrip() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let json = r#"{"token": 111, "epoch": 222}"#;

        let obj = env
            .t_junpack_desc(&t_scope, json, desc, 0)
            .expect("valid JSON should unpack");
        let output = obj.as_json();

        assert_eq!(output, "{\n    \"token\": 111,\n    \"epoch\": 222\n}\n");
    }

    // }}}
    // {{{ Generic YAML pack/unpack tests

    #[test]
    fn yunpack() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let yaml = "token: 123\nepoch: 456\n";

        env.t_yunpack_desc(&t_scope, yaml, desc, 0)
            .expect("valid YAML should unpack");
    }

    #[test]
    fn yunpack_invalid_yaml() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let invalid_yaml = "token: not_a_number\n";

        let Err(err) = env.t_yunpack_desc(&t_scope, invalid_yaml, desc, 0) else {
            panic!("expected error for invalid YAML");
        };
        assert!(err.contains("cannot set a string value in a field of type ulong"));
    }

    #[test]
    fn yaml_roundtrip() {
        let env = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = env
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let json = r#"{"token": 333, "epoch": 444}"#;

        let obj = env
            .t_junpack_desc(&t_scope, json, desc, 0)
            .expect("valid JSON should unpack");
        let output = obj.as_yaml();

        assert_eq!(output, "token: 333\nepoch: 444");
    }

    // }}}
    // {{{ IOP traits on C objects

    #[test]
    fn iop_struct_class_trait() {
        // Test a C IOP structure implements the CStruct trait
        fn assert_impl_struct<T: CStruct>() {}
        assert_impl_struct::<ic__simple_hdr__t>();

        // Test IOP structs have a new() method, that set the default values
        let hdr = ic__simple_hdr__t::new();
        assert_eq!(hdr.payload, -1);

        // Test they have a json packing method
        let json = hdr.as_json();
        assert_eq!(json, "{\n    \"payload\": -1\n}\n");

        // Test a C IOP class also implements the CStruct trait
        assert_impl_struct::<tstiop__test_class_child2__t>();
    }

    #[test]
    fn iop_union_trait() {
        // Test a C IOP union implements the CUnion trait
        fn assert_impl_union<T: CUnion>() {}
        assert_impl_union::<ic__hdr__t>();

        // Test IOP unions have a new() method
        // FIXME: we should not have to do all this manually
        let mut hdr = ic__hdr__t::new();
        hdr.iop_tag = ic__hdr__tag_t::ic__hdr__simple__ft;
        unsafe {
            *hdr.__bindgen_anon_1.simple.as_mut() = ic__simple_hdr__t::new();
        }

        // Test they have a json packing method
        let json = hdr.as_json();
        assert_eq!(json, "{ \"simple\": {\n    \"payload\": -1\n}\n }\n");
    }

    #[test]
    fn iop_enum_trait() {
        // Test a C IOP enum implement the CEnum trait
        fn assert_impl_enum<T: CEnum>() {}
        assert_impl_enum::<ic__ic_priority__t>();

        // Test the get_cdesc method
        let prio = ic__ic_priority__t::IC_PRIORITY_NORMAL;
        assert!(ptr::eq(prio.get_cdesc(), &raw const ic__ic_priority__e));
    }

    // }}}
    // {{{ Structs manipulation

    #[test]
    #[allow(clippy::float_cmp)]
    fn iop_struct_required_fields() {
        let mut obj = iop_new!(tstiop__full_required);

        // byte/ubyte field
        iop_set!(obj, { i8: -1 });
        assert_eq!(iop_get!(obj, i8), -1);
        iop_set!(obj, { u8: 1 });
        assert_eq!(iop_get!(obj, u8), 1);

        // short/ushort field
        iop_set!(obj, { i16: -2 });
        assert_eq!(iop_get!(obj, i16), -2);
        iop_set!(obj, { u16: 2 });
        assert_eq!(iop_get!(obj, u16), 2);

        // int/uint field
        iop_set!(obj, { i32: -3 });
        assert_eq!(iop_get!(obj, i32), -3);
        iop_set!(obj, { u32: 3 });
        assert_eq!(iop_get!(obj, u32), 3);

        // long/ulong field
        iop_set!(obj, { i64: -2_147_483_650 });
        assert_eq!(iop_get!(obj, i64), -2_147_483_650);
        iop_set!(obj, { u64: 2_147_483_650 });
        assert_eq!(iop_get!(obj, u64), 2_147_483_650);

        // bool field
        iop_set!(obj, { b: true });
        assert!(iop_get!(obj, b));

        // double field
        iop_set!(obj, { d: 0.2 });
        assert_eq!(iop_get!(obj, d), 0.2);

        // string field
        assert!(iop_get!(obj, s).equals(&lstr::null_utf8()));
        let test_str = String::from("Hello world");
        iop_set!(obj, { s: lstr::from_str(&test_str).into() });
        assert!(iop_get!(obj, s).equals(&lstr::from_str("Hello world")));

        // xml field
        assert!(iop_get!(obj, xml).equals(&lstr::null_utf8()));
        iop_set!(obj, { xml: lstr::from_str("<a/>").into() });
        assert!(iop_get!(obj, xml).equals(&lstr::from_str("<a/>")));

        // bytes field
        assert!(iop_get!(obj, data).equals(&lstr::null_bytes()));
        let test_data = lstr::from_bytes(b"hello world");
        iop_set!(obj, { data: test_data.into() });
        assert!(iop_get!(obj, data).equals(&lstr::from_bytes(b"hello world")));

        // enum field
        iop_set!(obj, { e: tstiop__test_enum__t::TEST_ENUM_B });
        assert_eq!(iop_get!(obj, e), tstiop__test_enum__t::TEST_ENUM_B);
        assert!(matches!(
            iop_get!(obj, e),
            tstiop__test_enum__t::TEST_ENUM_B
        ));

        // struct field
        iop_set!(obj, { st: iop_new!(tstiop__test_struct, { i: 123 }) });
        assert_eq!(iop_get!(obj, st.i), 123);
        assert!(iop_get!(obj, st.s).equals(&lstr::null_utf8()));

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        let child = iop_new!(tstiop__test_class_child2, { u: 5 });
        iop_set!(obj, { o: unsafe { &*ptr::from_ref(&child).cast::<tstiop__test_class__t>() } });
        let child2: &tstiop__test_class_child2__t =
            unsafe { &*ptr::from_ref(iop_get!(obj, o)).cast::<tstiop__test_class_child2__t>() };
        assert!(ptr::eq(child2, &raw const child));
        assert_eq!(iop_get!(child2, u), 5);

        // union field
        iop_set!(obj, { un: iop_new!(tstiop__test_union, { i: 56 }) });
        assert!(matches!(iop_get!(obj, un).iop_match(),
                         tstiop__test_union__variant::i(val) if val == 56));
    }

    #[test]
    #[allow(clippy::too_many_lines)]
    #[allow(clippy::float_cmp)]
    fn iop_struct_optional_fields() {
        let mut obj = iop_new!(tstiop__full_opt);

        // byte/ubyte field
        assert_eq!(iop_get!(obj, i8), None);
        iop_set!(obj, { i8: Some(-1) });
        assert_eq!(iop_get!(obj, i8), Some(-1));
        assert_eq!(iop_get!(obj, i8!), -1);
        iop_set!(obj, { i8: None });
        assert_eq!(iop_get!(obj, i8), None);

        assert_eq!(iop_get!(obj, u8), None);
        iop_set!(obj, { u8: Some(1) });
        assert_eq!(iop_get!(obj, u8!), 1);
        iop_set!(obj, { u8: None });
        assert_eq!(iop_get!(obj, u8), None);

        // short/ushort field
        assert_eq!(iop_get!(obj, i16), None);
        iop_set!(obj, { i16: Some(-2) });
        assert_eq!(iop_get!(obj, i16!), -2);
        iop_set!(obj, { i16: None });
        assert_eq!(iop_get!(obj, i16), None);

        assert_eq!(iop_get!(obj, u16), None);
        iop_set!(obj, { u16: Some(2) });
        assert_eq!(iop_get!(obj, u16!), 2);
        iop_set!(obj, { u16: None });
        assert_eq!(iop_get!(obj, u16), None);

        // int/uint field
        assert_eq!(iop_get!(obj, i32), None);
        iop_set!(obj, { i32: Some(-3) });
        assert_eq!(iop_get!(obj, i32!), -3);
        iop_set!(obj, { i32: None });
        assert_eq!(iop_get!(obj, i32), None);

        assert_eq!(iop_get!(obj, u32), None);
        iop_set!(obj, { u32: Some(3) });
        assert_eq!(iop_get!(obj, u32!), 3);
        iop_set!(obj, { u32: None });
        assert_eq!(iop_get!(obj, u32), None);

        // long/ulong field
        assert_eq!(iop_get!(obj, i64), None);
        iop_set!(obj, { i64: Some(-2_147_483_650) });
        assert_eq!(iop_get!(obj, i64!), -2_147_483_650);
        iop_set!(obj, { i64: None });
        assert_eq!(iop_get!(obj, i64), None);

        assert_eq!(iop_get!(obj, u64), None);
        iop_set!(obj, { u64: Some(2_147_483_650) });
        assert_eq!(iop_get!(obj, u64!), 2_147_483_650);
        iop_set!(obj, { u64: None });
        assert_eq!(iop_get!(obj, u64), None);

        // bool field
        assert_eq!(iop_get!(obj, b), None);
        iop_set!(obj, { b: Some(false) });
        assert!(!iop_get!(obj, b!));
        iop_set!(obj, { b: None });
        assert_eq!(iop_get!(obj, b), None);
        iop_set!(obj, { b: Some(true) });
        assert!(iop_get!(obj, b!));

        // double field
        assert_eq!(iop_get!(obj, d), None);
        iop_set!(obj, { d: Some(0.2) });
        assert_eq!(iop_get!(obj, d!), 0.2);
        iop_set!(obj, { d: None });
        assert_eq!(iop_get!(obj, d), None);

        // string field
        assert!(iop_get!(obj, s).is_none());
        let test_str = String::from("Hello world");
        iop_set!(obj, { s: Some(lstr::from_str(&test_str).into()) });
        assert!(iop_get!(obj, s!).equals(&lstr::from_str("Hello world")));
        iop_set!(obj, { s: None });
        assert!(iop_get!(obj, s).is_none());

        // xml field
        assert!(iop_get!(obj, xml).is_none());
        let test_xml = lstr::from_str("<a/>");
        iop_set!(obj, { xml: Some(test_xml.into()) });
        assert!(iop_get!(obj, xml!).equals(&lstr::from_str("<a/>")));
        iop_set!(obj, { xml: None });
        assert!(iop_get!(obj, xml).is_none());

        // bytes field
        assert!(iop_get!(obj, data).is_none());
        let test_data = lstr::from_bytes(b"hello world");
        iop_set!(obj, { data: Some(test_data.into()) });
        assert!(iop_get!(obj, data!).equals(&lstr::from_bytes(b"hello world")));
        iop_set!(obj, { data: None });
        assert!(iop_get!(obj, data).is_none());

        // enum field
        assert_eq!(iop_get!(obj, e), None);
        iop_set!(obj, { e: Some(tstiop__test_enum__t::TEST_ENUM_B) });
        assert_eq!(iop_get!(obj, e!), tstiop__test_enum__t::TEST_ENUM_B);
        iop_set!(obj, { e: None });
        assert_eq!(iop_get!(obj, e), None);

        // void field
        assert_eq!(iop_get!(obj, v), None);
        iop_set!(obj, { v: Some(()) });
        assert_eq!(iop_get!(obj, v!), ());

        // struct field
        assert!(iop_get!(obj, st).is_none());
        let st = iop_new!(tstiop__test_struct, { i: 123 });
        iop_set!(obj, { st: Some(&st) });
        assert_eq!(iop_get!(iop_get!(obj, st!), i), 123);
        iop_set!(obj, { st: None });
        assert!(iop_get!(obj, st).is_none());

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        assert!(iop_get!(obj, o).is_none());
        let child = iop_new!(tstiop__test_class_child2, { u: 5 });
        iop_set!(obj, { o: Some(unsafe {
            &*ptr::from_ref(&child).cast::<tstiop__test_class__t>()
        })});
        if let Some(val) = iop_get!(obj, o) {
            let child2: &tstiop__test_class_child2__t =
                unsafe { &*ptr::from_ref(val).cast::<tstiop__test_class_child2__t>() };
            assert!(ptr::eq(child2, &raw const child));
            assert_eq!(iop_get!(child2, u), 5);
        } else {
            panic!("expected Some for class field");
        }
        iop_set!(obj, { o: None });
        assert!(iop_get!(obj, o).is_none());

        // union field
        assert!(iop_get!(obj, un).is_none());
        let un = iop_new!(tstiop__test_union, { i: 56 });
        iop_set!(obj, { un: Some(&un) });
        assert!(matches!(iop_get!(obj, un!).iop_match(),
                         tstiop__test_union__variant::i(val) if val == 56));
        iop_set!(obj, { un: None });
        assert!(iop_get!(obj, un).is_none());
    }

    #[test]
    #[allow(clippy::too_many_lines)]
    fn iop_struct_repeated_fields() {
        let mut obj = iop_new!(tstiop__full_repeated);

        // scalar fields (only test with a static slice and dynamic vector, don't bother test with
        // all field types because implementation is the same for all)
        assert_eq!(iop_get!(obj, i32), &[] as &[i32]);
        iop_set!(obj, { i32: &[1, 2, 3] });
        assert_eq!(iop_get!(obj, i32), &[1, 2, 3]);

        let vec = vec![4, 5];
        iop_set!(obj, { u32: &vec });
        assert_eq!(iop_get!(obj, u32), &[4, 5]);

        // string field (bytes/xml have the same implem)
        assert_eq!(iop_get!(obj, s).len(), 0);
        let test_str = String::from("Hello world");
        let vec = vec![lstr::from_str(&test_str).into()];
        iop_set!(obj, { s: &vec });
        assert_eq!(iop_get!(obj, s).len(), 1);
        assert!(iop_get!(obj, s)[0].equals(&lstr::from_str("Hello world")));

        // enum field
        assert_eq!(iop_get!(obj, e), &[]);
        iop_set!(obj, { e: &[
            tstiop__test_enum__t::TEST_ENUM_B,
            tstiop__test_enum__t::TEST_ENUM_A,
        ]});
        assert_eq!(
            iop_get!(obj, e),
            &[
                tstiop__test_enum__t::TEST_ENUM_B,
                tstiop__test_enum__t::TEST_ENUM_A
            ]
        );

        // struct field
        assert_eq!(iop_get!(obj, st).len(), 0);
        let structs = [
            iop_new!(tstiop__test_struct, { i: 123 }),
            iop_new!(tstiop__test_struct, { i: 1234 }),
        ];
        iop_set!(obj, { st: &structs });
        assert_eq!(iop_get!(obj, st).len(), 2);
        assert_eq!(iop_get!(iop_get!(obj, st)[0], i), 123);
        assert_eq!(iop_get!(iop_get!(obj, st)[1], i), 1234);

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        assert_eq!(iop_get!(obj, o).len(), 0);

        let cls1 = iop_new!(tstiop__test_class_child2, { u: 5 });
        let cls2 = iop_new!(tstiop__test_class_child);
        let classes = [
            unsafe { &*ptr::from_ref(&cls1).cast::<tstiop__test_class__t>() },
            unsafe { &*ptr::from_ref(&cls2).cast::<tstiop__test_class__t>() },
        ];
        iop_set!(obj, { o: &classes });
        assert_eq!(iop_get!(obj, o).len(), 2);

        // union field
        assert_eq!(iop_get!(obj, un).len(), 0);
        let uns = [
            iop_new!(tstiop__test_union, { i: 56 }),
            iop_new!(tstiop__test_union, { s: lstr::from_str(&test_str).into() }),
        ];
        iop_set!(obj, { un: &uns });
        assert_eq!(iop_get!(obj, un).len(), 2);
        assert!(matches!(iop_get!(obj, un)[0].iop_match(),
                     tstiop__test_union__variant::i(val) if val == 56));
        assert!(matches!(iop_get!(obj, un)[1].iop_match(),
                         tstiop__test_union__variant::s(val)
                         if val.equals(&lstr::from_str("Hello world"))));

        // Final check on json representation
        assert_eq!(
            obj.as_json(),
            "{
    \"i8\": [  ],
    \"u8\": [  ],
    \"i16\": [  ],
    \"u16\": [  ],
    \"i32\": [ 1, 2, 3 ],
    \"u32\": [ 4, 5 ],
    \"i64\": [  ],
    \"u64\": [  ],
    \"b\": [  ],
    \"e\": [ \"B\", \"A\" ],
    \"d\": [  ],
    \"s\": [ \"Hello world\" ],
    \"data\": [  ],
    \"un\": [ { \"i\": 56 }, { \"s\": \"Hello world\" } ],
    \"st\": [ {
        \"i\": 123,
        \"s\": \"\"
    }, {
        \"i\": 1234,
        \"s\": \"\"
    } ],
    \"o\": [ {
        \"_class\": \"tstiop.TestClassChild2\",
        \"i\": 0,
        \"u\": 5
    }, {
        \"_class\": \"tstiop.TestClassChild\",
        \"i\": 0,
        \"s\": \"\"
    } ],
    \"xml\": [  ]
}
"
        );
    }

    #[test]
    fn iop_struct_accessors_ref() {
        let mut obj = iop_new!(tstiop__full_ref);

        // reference struct
        let mut st = iop_new!(tstiop__test_struct, { i: 123 });
        iop_set!(obj, { st: &st });
        assert_eq!(iop_get!(obj, st.i), 123);
        assert!(iop_get!(obj, st.s).equals(&lstr::null_utf8()));
        iop_set!(st, { i: 124 });
        assert_eq!(iop_get!(obj, st.i), 124);

        // reference union
        let mut un = iop_new!(tstiop__test_union, { i: 56 });
        iop_set!(obj, { un: &un });
        assert!(matches!(iop_get!(obj, un).iop_match(),
                     tstiop__test_union__variant::i(val) if val == 56));
        iop_set!(un, { i: 57 });
        assert!(matches!(iop_get!(obj, un).iop_match(),
                     tstiop__test_union__variant::i(val) if val == 57));
    }

    // }}}
    // {{{ Unions manipulation

    /// Test union manipulation for unions represented as Rust unions
    #[test]
    #[allow(clippy::float_cmp)]
    fn iop_union_as_rust_union() {
        // scalar fields
        let un = iop_new!(tstiop__my_union_c, { i_of_c: 12 });
        assert!(matches!(un.iop_match(), tstiop__my_union_c__variant::i_of_c(val) if val == 12));

        let un = iop_new!(tstiop__my_union_c, { d_of_c: 13. });
        assert!(matches!(un.iop_match(), tstiop__my_union_c__variant::d_of_c(val) if val == 13.));

        match un.iop_match() {
            tstiop__my_union_c__variant::i_of_c(_) => panic!("unexpected i_of_c"),
            tstiop__my_union_c__variant::d_of_c(val) => assert_eq!(val, 13.),
        }

        // reference fields
        let mut st = iop_new!(tstiop__my_referenced_struct, { a: 42 });
        let mut un = iop_new!(tstiop__my_ref_union, { s: &st });
        assert!(matches!(un.iop_match(),
                         tstiop__my_ref_union__variant::s(val) if iop_get!(val, a) == 42));
        iop_set!(st, { a: 43 });
        assert!(matches!(un.iop_match(),
                         tstiop__my_ref_union__variant::s(val) if iop_get!(val, a) == 43));

        let mut run = iop_new!(tstiop__my_referenced_union, { b: 56 });
        iop_set!(un, { u: &run });
        if let tstiop__my_ref_union__variant::u(val_u) = un.iop_match() {
            assert!(matches!(val_u.iop_match(),
                             tstiop__my_referenced_union__variant::b(b) if b == 56));
        } else {
            panic!("expected union field");
        }
        iop_set!(run, { b: 57 });
        if let tstiop__my_ref_union__variant::u(val_u) = un.iop_match() {
            assert!(matches!(val_u.iop_match(),
                             tstiop__my_referenced_union__variant::b(b) if b == 57));
        } else {
            panic!("expected union field");
        }
    }

    /// Test union manipulation for unions represented as Rust structs
    #[test]
    fn iop_union_as_rust_struct() {
        // scalar field
        let un = iop_new!(tstiop__get_bpack_sz_u, { i8: 12 });
        assert!(matches!(un.iop_match(), tstiop__get_bpack_sz_u__variant::i8(val) if val == 12));

        // string field
        let un = iop_new!(tstiop__get_bpack_sz_u, { s: lstr::from_str("coucou world").into() });
        assert!(matches!(un.iop_match(),
                         tstiop__get_bpack_sz_u__variant::s(val)
                         if val.equals(&lstr::from_str("coucou world"))));

        // enum field
        let un = iop_new!(tstiop__get_bpack_sz_u, {
            en: tstiop__get_bpack_sz_en__t::GET_BPACK_SZ_EN_B
        });
        assert!(matches!(un.iop_match(),
                         tstiop__get_bpack_sz_u__variant::en(val)
                         if val == tstiop__get_bpack_sz_en__t::GET_BPACK_SZ_EN_B));

        // struct field
        let un = iop_new!(tstiop__get_bpack_sz_u, {
            st: iop_new!(tstiop__get_bpack_sz_st, { a: 42 })
        });
        assert!(matches!(un.iop_match(),
                         tstiop__get_bpack_sz_u__variant::st(val) if iop_get!(val, a) == 42));
    }

    // }}}
}
