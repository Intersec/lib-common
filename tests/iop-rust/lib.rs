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
    use libcommon::lstr;
    use libcommon::mem_stack::TScope;
    use std::ptr;

    use crate::bindings::{
        core__pkg, ic__hdr__t, ic__hdr__tag_t, ic__ic_priority__e, ic__ic_priority__t, ic__pkg,
        ic__simple_hdr__t, tstiop__full_opt__t, tstiop__full_ref__t, tstiop__full_repeated__t,
        tstiop__full_required__t, tstiop__test_class__t, tstiop__test_class_child__t,
        tstiop__test_class_child2__t, tstiop__test_enum__t, tstiop__test_struct__t,
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
    // {{{ Struct accessors

    #[test]
    #[allow(clippy::float_cmp)]
    fn iop_struct_accessors_required() {
        let mut obj = tstiop__full_required__t::new();

        // byte/ubyte field
        obj.i8__set(-1);
        assert_eq!(obj.i8__get(), -1);
        obj.u8__set(1);
        assert_eq!(obj.u8__get(), 1);

        // short/ushort field
        obj.i16__set(-2);
        assert_eq!(obj.i16__get(), -2);
        obj.u16__set(2);
        assert_eq!(obj.u16__get(), 2);

        // int/uint field
        obj.i32__set(-3);
        assert_eq!(obj.i32__get(), -3);
        obj.u32__set(3);
        assert_eq!(obj.u32__get(), 3);

        // long/ulong field
        obj.i64__set(-2_147_483_650);
        assert_eq!(obj.i64__get(), -2_147_483_650);
        obj.u64__set(2_147_483_650);
        assert_eq!(obj.u64__get(), 2_147_483_650);

        // bool field
        obj.b__set(true);
        assert!(obj.b__get());

        // double field
        obj.d__set(0.2);
        assert_eq!(obj.d__get(), 0.2);

        // string field
        assert!(obj.s__get().equals(&lstr::null_utf8()));
        let test_str = String::from("Hello world");
        obj.s__set(lstr::from_str(&test_str).into());
        assert!(obj.s__get().equals(&lstr::from_str("Hello world")));

        // xml field
        assert!(obj.xml__get().equals(&lstr::null_utf8()));
        obj.xml__set(lstr::from_str("<a/>").into());
        assert!(obj.xml__get().equals(&lstr::from_str("<a/>")));

        // bytes field
        assert!(obj.data__get().equals(&lstr::null_bytes()));
        let test_data = lstr::from_bytes(b"hello world");
        obj.data__set(test_data.into());
        assert!(obj.data__get().equals(&lstr::from_bytes(b"hello world")));

        // enum field
        obj.e__set(tstiop__test_enum__t::TEST_ENUM_B);
        assert_eq!(obj.e__get(), tstiop__test_enum__t::TEST_ENUM_B);
        assert!(matches!(obj.e__get(), tstiop__test_enum__t::TEST_ENUM_B));

        // struct field
        obj.st__set({
            let mut st = tstiop__test_struct__t::new();
            st.i__set(123);
            st
        });
        assert_eq!(obj.st__get().i__get(), 123);
        assert!(obj.st__get().s__get().equals(&lstr::null_utf8()));

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        let child = {
            let mut child = tstiop__test_class_child2__t::new();
            child.u__set(5);
            child
        };
        obj.o__set(unsafe { &*ptr::from_ref(&child).cast::<tstiop__test_class__t>() });
        let child2: &tstiop__test_class_child2__t =
            unsafe { &*ptr::from_ref(obj.o__get()).cast::<tstiop__test_class_child2__t>() };
        assert!(ptr::eq(child2, &raw const child));
        assert_eq!(child2.u__get(), 5);

        // TODO: Union
    }

    #[test]
    #[allow(clippy::too_many_lines)]
    fn iop_struct_accessors_optional() {
        let mut obj = tstiop__full_opt__t::new();

        // byte/ubyte field
        assert_eq!(obj.i8__get(), None);
        obj.i8__set(Some(-1));
        assert_eq!(obj.i8__get(), Some(-1));
        obj.i8__set(None);
        assert_eq!(obj.i8__get(), None);

        assert_eq!(obj.u8__get(), None);
        obj.u8__set(Some(1));
        assert_eq!(obj.u8__get(), Some(1));
        obj.u8__set(None);
        assert_eq!(obj.u8__get(), None);

        // short/ushort field
        assert_eq!(obj.i16__get(), None);
        obj.i16__set(Some(-2));
        assert_eq!(obj.i16__get(), Some(-2));
        obj.i16__set(None);
        assert_eq!(obj.i16__get(), None);

        assert_eq!(obj.u16__get(), None);
        obj.u16__set(Some(2));
        assert_eq!(obj.u16__get(), Some(2));
        obj.u16__set(None);
        assert_eq!(obj.u16__get(), None);

        // int/uint field
        assert_eq!(obj.i32__get(), None);
        obj.i32__set(Some(-3));
        assert_eq!(obj.i32__get(), Some(-3));
        obj.i32__set(None);
        assert_eq!(obj.i32__get(), None);

        assert_eq!(obj.u32__get(), None);
        obj.u32__set(Some(3));
        assert_eq!(obj.u32__get(), Some(3));
        obj.u32__set(None);
        assert_eq!(obj.u32__get(), None);

        // long/ulong field
        assert_eq!(obj.i64__get(), None);
        obj.i64__set(Some(-2_147_483_650));
        assert_eq!(obj.i64__get(), Some(-2_147_483_650));
        obj.i64__set(None);
        assert_eq!(obj.i64__get(), None);

        assert_eq!(obj.u64__get(), None);
        obj.u64__set(Some(2_147_483_650));
        assert_eq!(obj.u64__get(), Some(2_147_483_650));
        obj.u64__set(None);
        assert_eq!(obj.u64__get(), None);

        // bool field
        assert_eq!(obj.b__get(), None);
        obj.b__set(Some(false));
        assert_eq!(obj.b__get(), Some(false));
        obj.b__set(None);
        assert_eq!(obj.b__get(), None);
        obj.b__set(Some(true));
        assert_eq!(obj.b__get(), Some(true));

        // double field
        assert_eq!(obj.d__get(), None);
        obj.d__set(Some(0.2));
        assert_eq!(obj.d__get(), Some(0.2));
        obj.d__set(None);
        assert_eq!(obj.d__get(), None);

        // string field
        assert!(obj.s__get().is_none());
        let test_str = String::from("Hello world");
        obj.s__set(Some(lstr::from_str(&test_str).into()));
        if let Some(s) = obj.s__get() {
            assert!(s.equals(&lstr::from_str("Hello world")));
        } else {
            panic!("expected Some for string field");
        }
        obj.s__set(None);
        assert!(obj.s__get().is_none());

        // xml field
        assert!(obj.xml__get().is_none());
        let test_xml = lstr::from_str("<a/>");
        obj.xml__set(Some(test_xml.into()));
        if let Some(xml) = obj.xml__get() {
            assert!(xml.equals(&lstr::from_str("<a/>")));
        } else {
            panic!("expected Some for xml field");
        }
        obj.xml__set(None);
        assert!(obj.xml__get().is_none());

        // bytes field
        assert!(obj.data__get().is_none());
        let test_data = lstr::from_bytes(b"hello world");
        obj.data__set(Some(test_data.into()));
        if let Some(data) = obj.data__get() {
            assert!(data.equals(&lstr::from_bytes(b"hello world")));
        } else {
            panic!("expected Some for data field");
        }
        obj.data__set(None);
        assert!(obj.data__get().is_none());

        // enum field
        assert_eq!(obj.e__get(), None);
        obj.e__set(Some(tstiop__test_enum__t::TEST_ENUM_B));
        if let Some(e) = obj.e__get() {
            assert_eq!(e, tstiop__test_enum__t::TEST_ENUM_B);
        } else {
            panic!("expected Some for enum field");
        }
        obj.e__set(None);
        assert_eq!(obj.e__get(), None);

        // void field
        assert_eq!(obj.v__get(), None);
        obj.v__set(Some(()));
        assert_eq!(obj.v__get(), Some(()));

        // struct field
        assert!(obj.st__get().is_none());
        let mut st = tstiop__test_struct__t::new();
        st.i__set(123);
        obj.st__set(Some(&st));
        if let Some(val) = obj.st__get() {
            assert_eq!(val.i__get(), 123);
        } else {
            panic!("expected Some for struct field");
        }
        obj.st__set(None);
        assert!(obj.st__get().is_none());

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        assert!(obj.o__get().is_none());
        let child = {
            let mut child = tstiop__test_class_child2__t::new();
            child.u__set(5);
            child
        };
        obj.o__set(Some(unsafe {
            &*ptr::from_ref(&child).cast::<tstiop__test_class__t>()
        }));
        if let Some(val) = obj.o__get() {
            let child2: &tstiop__test_class_child2__t =
                unsafe { &*ptr::from_ref(val).cast::<tstiop__test_class_child2__t>() };
            assert!(ptr::eq(child2, &raw const child));
            assert_eq!(child2.u__get(), 5);
        } else {
            panic!("expected Some for class field");
        }
        obj.o__set(None);
        assert!(obj.o__get().is_none());

        // TODO: Union
    }

    #[test]
    fn iop_struct_accessors_repeated() {
        let mut obj = tstiop__full_repeated__t::new();

        // scalar fields (only test with a static slice and dynamic vector, don't bother test with
        // all field types because implementation is the same for all)
        assert_eq!(obj.i32__get(), &[] as &[i32]);
        obj.i32__set(&[1, 2, 3]);
        assert_eq!(obj.i32__get(), &[1, 2, 3]);

        let vec = vec![4, 5];
        obj.u32__set(&vec);
        assert_eq!(obj.u32__get(), &[4, 5]);

        // string field (bytes/xml have the same implem)
        assert_eq!(obj.s__get().len(), 0);
        let test_str = String::from("Hello world");
        let vec = vec![lstr::from_str(&test_str).into()];
        obj.s__set(&vec);
        assert_eq!(obj.s__get().len(), 1);
        assert!(obj.s__get()[0].equals(&lstr::from_str("Hello world")));

        // enum field
        assert_eq!(obj.e__get(), &[]);
        obj.e__set(&[
            tstiop__test_enum__t::TEST_ENUM_B,
            tstiop__test_enum__t::TEST_ENUM_A,
        ]);
        assert_eq!(
            obj.e__get(),
            &[
                tstiop__test_enum__t::TEST_ENUM_B,
                tstiop__test_enum__t::TEST_ENUM_A
            ]
        );

        // struct field
        assert_eq!(obj.st__get().len(), 0);
        let structs = [
            {
                let mut st = tstiop__test_struct__t::new();
                st.i__set(123);
                st
            },
            {
                let mut st = tstiop__test_struct__t::new();
                st.i__set(1234);
                st
            },
        ];
        obj.st__set(&structs);
        assert_eq!(obj.st__get().len(), 2);
        assert_eq!(obj.st__get()[0].i__get(), 123);
        assert_eq!(obj.st__get()[1].i__get(), 1234);

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        assert_eq!(obj.o__get().len(), 0);

        let cls1 = {
            let mut cls1 = tstiop__test_class_child2__t::new();
            cls1.u__set(5);
            cls1
        };
        let cls2 = tstiop__test_class_child__t::new();
        let classes = [
            unsafe { &*ptr::from_ref(&cls1).cast::<tstiop__test_class__t>() },
            unsafe { &*ptr::from_ref(&cls2).cast::<tstiop__test_class__t>() },
        ];
        obj.o__set(&classes);
        assert_eq!(obj.o__get().len(), 2);

        // TODO union

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
    \"un\": [  ],
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
        let mut obj = tstiop__full_ref__t::new();

        // reference struct
        let mut st = tstiop__test_struct__t::new();
        st.i__set(123);
        obj.st__set(&st);
        assert_eq!(obj.st__get().i__get(), 123);
        assert!(obj.st__get().s__get().equals(&lstr::null_utf8()));
        st.i__set(124);
        assert_eq!(obj.st__get().i__get(), 124);

        // TODO: reference union
    }

    // }}}
}
