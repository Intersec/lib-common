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

// bindgen's own reading of the headers of `bindings`, without the native
// IOP bindings, for the layout tests to compare both. See
// `build/layout_tests.rs`.
#[cfg(test)]
#[waf_cargo_build::bindings_mod]
mod c_layout {
    include!(concat!(env!("OUT_DIR"), "/c_layout.rs"));
}

#[cfg(test)]
#[allow(nonstandard_style)]
mod layout {
    use crate::{bindings as iop_rust, c_layout};
    use std::mem::offset_of;

    include!(concat!(env!("OUT_DIR"), "/layout_tests.rs"));
}

#[cfg(test)]
mod iop_testable;

// Round-trip, constraint and cast tests of each type that `iopc` writes a
// constructor for. See `build/native_tests.rs`.
#[cfg(test)]
#[allow(nonstandard_style)]
mod native_tests {
    use crate::bindings::*;
    use crate::iop_testable::{self, Pack};

    include!(concat!(env!("OUT_DIR"), "/native_tests.rs"));
}

#[cfg(test)]
mod iop_tests {
    use crate::native_tests::iop_env::ENV;
    use libcommon::iop::{
        CEnum, CInstance as _, CStruct, CStructUnion as _, CUnion, Class as _, Enum as _, Env,
        EnvCtx, IopDup as _, IopIface, IopModule as _, IopRPC, StructUnion as _,
    };
    use libcommon::mem_stack::TScope;
    use libcommon::{iop_get, iop_new, iop_set, lstr};
    use std::ptr;

    use crate::bindings::{
        core__pkg, ic__hdr__u, ic__ic_priority__e, ic__ic_priority__edesc, ic__pkg,
        ic__simple_hdr__t, ic__tracer__t, tstiop__basic_struct__t, tstiop__full_opt__t,
        tstiop__full_ref__t, tstiop__full_repeated__t, tstiop__full_required__t,
        tstiop__get_bpack_sz_en__e, tstiop__get_bpack_sz_st__t, tstiop__get_bpack_sz_u__u,
        tstiop__my_parent__t, tstiop__my_ref_union__u, tstiop__my_referenced_struct__t,
        tstiop__my_referenced_union__u, tstiop__my_union_a__u, tstiop__my_union_b__u,
        tstiop__my_union_c__u, tstiop__small_class__t, tstiop__test_class__t,
        tstiop__test_class_child__t, tstiop__test_class_child2__t, tstiop__test_enum__e,
        tstiop__test_struct__t, tstiop__test_union__u, tstiop_backward_compat__enum1__e,
        tstiop_rust_keywords__keyword_union__u, tstiop_rust_keywords__keywords__t,
        tstiop_typedef__array_test__t, tstiop_typedef__basic_struct_list__t,
        tstiop_typedef__enum1_list__e, tstiop_typedef__enum1_list__t_raw,
        tstiop_typedef__enum2_list__e, tstiop_typedef__number_struct_list__t,
        tstiop_typedef__remote_class__t, tstiop_typedef__remote_enum__e,
        tstiop_typedef__remote_struct__t, tstiop_typedef__remote_struct__t_raw,
        tstiop_typedef__remote_typedef_class__t_raw,
        tstiop_typedef__struct_with_typedefs_from_ext__t, tstiop_void_type__void_alone__u,
        tstiop_void_type__void_only__u,
    };

    // {{{ Test helpers

    fn setup_env_with_packages() -> EnvCtx {
        let mut env = Env::new();
        env.register_packages(&[&raw const ic__pkg, &raw const core__pkg]);
        env.acquire()
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
        let env2 = unsafe { Env::from_ptr(ptr) };
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
        let desc = env.acquire().get_struct_desc("ic.SimpleHdr");
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

    #[test]
    fn generic_struct_union_outlives_source_ctx() {
        // A GenericStructUnion keeps alive (a clone of) the EnvCtx it was
        // unpacked from, so its descriptor stays valid even after the
        // source ctx is dropped. Exercises EnvCtx::clone + the dup/release
        // balance (ASAN-checked).
        let ctx = setup_env_with_packages();
        let t_scope = TScope::new_scope();

        let desc = ctx
            .get_struct_desc("ic.Tracer")
            .expect("ic.Tracer should exist");
        let obj = ctx
            .t_junpack_desc(&t_scope, r#"{"token": 1, "epoch": 2}"#, desc, 0)
            .expect("valid JSON should unpack");

        // Drop the ctx we resolved `desc` from; `obj` holds its own clone.
        drop(ctx);

        // `obj`'s descriptor is still valid (read by `as_json`).
        assert_eq!(obj.as_json(), "{\n    \"token\": 1,\n    \"epoch\": 2\n}\n");
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
    #[allow(elided_lifetimes_in_paths)]
    fn iop_struct_class_trait() {
        // Test a C IOP structure implements the CStruct trait
        fn assert_impl_struct<T: CStruct>() {}
        assert_impl_struct::<ic__simple_hdr__t<'_>>();

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
    #[allow(elided_lifetimes_in_paths)]
    fn iop_union_trait() {
        // Test a C IOP union implements the CUnion trait
        fn assert_impl_union<T: CUnion>() {}
        assert_impl_union::<ic__hdr__u<'_>>();

        // Test they have a json packing method
        let hdr = iop_new!(ic__hdr::simple, ic__simple_hdr__t::new());
        let json = hdr.as_json();
        assert_eq!(json, "{ \"simple\": {\n    \"payload\": -1\n}\n }\n");
    }

    #[test]
    fn iop_enum_trait() {
        // Test a C IOP enum implement the CEnum trait
        fn assert_impl_enum<T: CEnum>() {}
        assert_impl_enum::<ic__ic_priority__e>();

        // Test the get_cdesc method
        let prio = ic__ic_priority__e::NORMAL;
        assert!(ptr::eq(prio.get_cdesc(), &raw const ic__ic_priority__edesc));
    }

    // }}}
    // {{{ Structs manipulation

    #[test]
    #[allow(clippy::float_cmp)]
    fn iop_struct_required_fields() {
        // The union and the class have no default value: the builder
        // requires them.
        let mut first_child = iop_new!(tstiop__test_class_child2, { u: 4 });
        let mut obj = iop_new!(tstiop__full_required, {
            un: iop_new!(tstiop__test_union::i, 0),
            o: first_child.upcast_mut(),
        });

        // byte/ubyte field
        iop_set!(obj, { i8__var: -1 });
        assert_eq!(iop_get!(obj, i8__var), -1);
        iop_set!(obj, { u8__var: 1 });
        assert_eq!(iop_get!(obj, u8__var), 1);

        // short/ushort field
        iop_set!(obj, { i16__var: -2 });
        assert_eq!(iop_get!(obj, i16__var), -2);
        iop_set!(obj, { u16__var: 2 });
        assert_eq!(iop_get!(obj, u16__var), 2);

        // int/uint field
        iop_set!(obj, { i32__var: -3 });
        assert_eq!(iop_get!(obj, i32__var), -3);
        iop_set!(obj, { u32__var: 3 });
        assert_eq!(iop_get!(obj, u32__var), 3);

        // long/ulong field
        iop_set!(obj, { i64__var: -2_147_483_650 });
        assert_eq!(iop_get!(obj, i64__var), -2_147_483_650);
        iop_set!(obj, { u64__var: 2_147_483_650 });
        assert_eq!(iop_get!(obj, u64__var), 2_147_483_650);

        // bool field
        iop_set!(obj, { b: true });
        let b: bool = iop_get!(obj, b);
        assert!(b);

        // double field
        iop_set!(obj, { d: 0.2 });
        assert_eq!(iop_get!(obj, d), 0.2);

        // string field
        assert!(iop_get!(obj, s).is_null());
        let test_str = String::from("Hello world");
        iop_set!(obj, { s: lstr::from_str(&test_str) });
        assert_eq!(iop_get!(obj, s), lstr::from_str("Hello world"));

        // xml field
        assert!(iop_get!(obj, xml).is_null());
        iop_set!(obj, { xml: lstr::from_str("<a/>") });
        assert_eq!(iop_get!(obj, xml), lstr::from_str("<a/>"));

        // bytes field
        assert!(iop_get!(obj, data).is_null());
        let test_data = lstr::from_bytes(b"hello world");
        iop_set!(obj, { data: test_data });
        assert_eq!(iop_get!(obj, data), lstr::from_bytes(b"hello world"));

        // enum field
        iop_set!(obj, { e: tstiop__test_enum__e::B });
        assert_eq!(iop_get!(obj, e), tstiop__test_enum__e::B);
        assert!(matches!(iop_get!(obj, e), tstiop__test_enum__e::B));

        // struct field
        iop_set!(obj, { st: iop_new!(tstiop__test_struct, { i: 123 }) });
        assert_eq!(iop_get!(obj, st.i), 123);
        assert!(iop_get!(obj, st.s).is_null());

        // class field: the field holds the child through its parent class
        let mut child = iop_new!(tstiop__test_class_child2, { u: 5 });
        let child_addr = ptr::from_ref(&child);
        iop_set!(obj, { o: child.upcast_mut() });
        let child2 = iop_get!(obj, o)
            .downcast::<tstiop__test_class_child2__t>()
            .expect("the field should hold a `TestClassChild2`");
        assert!(ptr::eq(child2, child_addr));
        assert_eq!(iop_get!(child2, u), 5);

        // union field
        iop_set!(obj, { un: iop_new!(tstiop__test_union::i, 56) });
        assert!(matches!(iop_get!(obj, un), tstiop__test_union__u::i(val) if val == 56));
    }

    #[test]
    #[allow(clippy::too_many_lines)]
    #[allow(clippy::float_cmp)]
    fn iop_struct_optional_fields() {
        let mut obj = iop_new!(tstiop__full_opt);

        // byte/ubyte field
        assert_eq!(iop_get!(obj, i8__var?), None);
        iop_set!(obj, { i8__var: Some(-1) });
        assert_eq!(iop_get!(obj, i8__var?), Some(-1));
        let v: i8 = iop_get!(obj, i8__var!);
        assert_eq!(v, -1);
        iop_set!(obj, { i8__var: None });
        assert_eq!(iop_get!(obj, i8__var?), None);

        assert_eq!(iop_get!(obj, u8__var?), None);
        iop_set!(obj, { u8__var: Some(1) });
        let v: u8 = iop_get!(obj, u8__var!);
        assert_eq!(v, 1);
        iop_set!(obj, { u8__var: None });
        assert_eq!(iop_get!(obj, u8__var?), None);

        // short/ushort field
        assert_eq!(iop_get!(obj, i16__var?), None);
        iop_set!(obj, { i16__var: Some(-2) });
        let v: i16 = iop_get!(obj, i16__var!);
        assert_eq!(v, -2);
        iop_set!(obj, { i16__var: None });
        assert_eq!(iop_get!(obj, i16__var?), None);

        assert_eq!(iop_get!(obj, u16__var?), None);
        iop_set!(obj, { u16__var: Some(2) });
        let v: u16 = iop_get!(obj, u16__var!);
        assert_eq!(v, 2);
        iop_set!(obj, { u16__var: None });
        assert_eq!(iop_get!(obj, u16__var?), None);

        // int/uint field
        assert_eq!(iop_get!(obj, i32__var?), None);
        iop_set!(obj, { i32__var: Some(-3) });
        let v: i32 = iop_get!(obj, i32__var!);
        assert_eq!(v, -3);
        iop_set!(obj, { i32__var: None });
        assert_eq!(iop_get!(obj, i32__var?), None);

        assert_eq!(iop_get!(obj, u32__var?), None);
        iop_set!(obj, { u32__var: Some(3) });
        let v: u32 = iop_get!(obj, u32__var!);
        assert_eq!(v, 3);
        iop_set!(obj, { u32__var: None });
        assert_eq!(iop_get!(obj, u32__var?), None);

        // long/ulong field
        assert_eq!(iop_get!(obj, i64__var?), None);
        iop_set!(obj, { i64__var: Some(-2_147_483_650) });
        let v: i64 = iop_get!(obj, i64__var!);
        assert_eq!(v, -2_147_483_650);
        iop_set!(obj, { i64__var: None });
        assert_eq!(iop_get!(obj, i64__var?), None);

        assert_eq!(iop_get!(obj, u64__var?), None);
        iop_set!(obj, { u64__var: Some(2_147_483_650) });
        let v: u64 = iop_get!(obj, u64__var!);
        assert_eq!(v, 2_147_483_650);
        iop_set!(obj, { u64__var: None });
        assert_eq!(iop_get!(obj, u64__var?), None);

        // bool field
        assert_eq!(iop_get!(obj, b?), None);
        iop_set!(obj, { b: Some(false) });
        let v: bool = iop_get!(obj, b!);
        assert!(!v);
        iop_set!(obj, { b: None });
        assert_eq!(iop_get!(obj, b?), None);
        iop_set!(obj, { b: Some(true) });
        let v: bool = iop_get!(obj, b!);
        assert!(v);

        // double field
        assert_eq!(iop_get!(obj, d?), None);
        iop_set!(obj, { d: Some(0.2) });
        let v: f64 = iop_get!(obj, d!);
        assert_eq!(v, 0.2);
        iop_set!(obj, { d: None });
        assert_eq!(iop_get!(obj, d?), None);

        // string field (no Option wrapper: a null lstr is the "unset"
        // sentinel). `iop_init_desc` leaves those fields null, so a freshly
        // built object carries the value that means "unset".
        let unset = iop_new!(tstiop__full_opt);

        assert!(iop_get!(obj, s).is_null());
        let test_str = String::from("Hello world");
        iop_set!(obj, { s: lstr::from_str(&test_str) });
        assert_eq!(iop_get!(obj, s), lstr::from_str("Hello world"));
        iop_set!(obj, { s: iop_get!(unset, s) });
        assert!(iop_get!(obj, s).is_null());

        // xml field
        assert!(iop_get!(obj, xml).is_null());
        let test_xml = lstr::from_str("<a/>");
        iop_set!(obj, { xml: test_xml });
        assert_eq!(iop_get!(obj, xml), lstr::from_str("<a/>"));
        iop_set!(obj, { xml: iop_get!(unset, xml) });
        assert!(iop_get!(obj, xml).is_null());

        // bytes field
        assert!(iop_get!(obj, data).is_null());
        let test_data = lstr::from_bytes(b"hello world");
        iop_set!(obj, { data: test_data });
        assert_eq!(iop_get!(obj, data), lstr::from_bytes(b"hello world"));
        iop_set!(obj, { data: iop_get!(unset, data) });
        assert!(iop_get!(obj, data).is_null());

        // enum field
        assert_eq!(iop_get!(obj, e?), None);
        iop_set!(obj, { e: Some(tstiop__test_enum__e::B) });
        assert_eq!(iop_get!(obj, e!), tstiop__test_enum__e::B);
        iop_set!(obj, { e: None });
        assert_eq!(iop_get!(obj, e?), None);

        // void field (stored as a plain bool, not an Option<()> wrapper)
        let void_v: bool = iop_get!(obj, v);
        assert!(!void_v);
        iop_set!(obj, { v: true });
        let void_v: bool = iop_get!(obj, v);
        assert!(void_v);

        // struct field
        // NOTE: `obj.st` is `Option<&mut T>` (not `Copy`), so each
        // `iop_get!` read below moves it out of `obj` — it's re-assigned
        // before every read to keep `obj` valid for the next one.
        assert!(iop_get!(obj, st).is_none());
        let mut st = iop_new!(tstiop__test_struct, { i: 123 });
        iop_set!(obj, { st: Some(&mut st) });
        assert_eq!(iop_get!(obj, st!.i), 123);
        // A second fresh binding is used here (rather than re-pointing `obj.st`
        // at `st` again) since `obj`'s lifetime parameter stays tied to the
        // first `&mut st` borrow for as long as `obj` is alive.
        let mut st2 = iop_new!(tstiop__test_struct, { i: 123 });
        iop_set!(obj, { st: Some(&mut st2) });
        assert_eq!(iop_get!(obj, st?.i), Some(123));
        iop_set!(obj, { st: None });
        assert!(iop_get!(obj, st).is_none());
        iop_set!(obj, { st: None });
        assert_eq!(iop_get!(obj, st?.i), None);

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        assert!(iop_get!(obj, o).is_none());
        let mut child = iop_new!(tstiop__test_class_child2, { u: 5 });
        iop_set!(obj, { o: Some(unsafe {
            &mut *ptr::from_mut(&mut child).cast::<tstiop__test_class__t>()
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
        let mut un = tstiop__test_union__u::i(56);
        iop_set!(obj, { un: Some(&mut un) });
        let v: &mut tstiop__test_union__u<'_> = iop_get!(obj, un!);
        assert!(matches!(v, tstiop__test_union__u::i(val) if *val == 56));
        iop_set!(obj, { un: None });
        assert!(iop_get!(obj, un).is_none());
    }

    #[test]
    #[allow(clippy::too_many_lines)]
    fn iop_struct_repeated_fields() {
        let mut obj = iop_new!(tstiop__full_repeated);

        // scalar fields (only test with a static slice and dynamic vector, don't bother test with
        // all field types because implementation is the same for all)
        let i32_slice: &[i32] = iop_get!(obj, i32__var).into();
        assert_eq!(i32_slice, &[] as &[i32]);
        iop_set!(obj, { i32__var: [1, 2, 3].as_slice() });
        let i32_slice: &[i32] = iop_get!(obj, i32__var).into();
        assert_eq!(i32_slice, &[1, 2, 3]);

        let arr = [4, 5];
        iop_set!(obj, { u32__var: arr.as_slice() });
        let u32_slice: &[u32] = iop_get!(obj, u32__var).into();
        assert_eq!(u32_slice, &[4, 5]);

        // string field (bytes/xml have the same implem)
        let str_slice: &[lstr::BorrowedUtf8Lstr<'_>] = iop_get!(obj, s).into();
        assert_eq!(str_slice.len(), 0);
        let test_str = String::from("Hello world");
        let strs = [lstr::from_str(&test_str)];
        iop_set!(obj, { s: strs.as_slice() });
        let str_slice: &[lstr::BorrowedUtf8Lstr<'_>] = iop_get!(obj, s).into();
        assert_eq!(str_slice.len(), 1);
        assert_eq!(str_slice[0], lstr::from_str("Hello world"));

        // enum field
        let enum_slice: &[tstiop__test_enum__e] = iop_get!(obj, e).into();
        assert_eq!(enum_slice, &[]);
        iop_set!(obj, { e: [
            tstiop__test_enum__e::B,
            tstiop__test_enum__e::A,
        ].as_slice() });
        let enum_slice: &[tstiop__test_enum__e] = iop_get!(obj, e).into();
        assert_eq!(
            enum_slice,
            &[tstiop__test_enum__e::B, tstiop__test_enum__e::A]
        );

        // struct field
        let struct_slice: &[tstiop__test_struct__t<'_>] = iop_get!(obj, st).into();
        assert_eq!(struct_slice.len(), 0);
        let structs = [
            iop_new!(tstiop__test_struct, { i: 123 }),
            iop_new!(tstiop__test_struct, { i: 1234 }),
        ];
        iop_set!(obj, { st: structs.as_slice() });
        let struct_slice: &[tstiop__test_struct__t<'_>] = iop_get!(obj, st).into();
        assert_eq!(struct_slice.len(), 2);
        assert_eq!(iop_get!(struct_slice[0], i), 123);
        assert_eq!(iop_get!(struct_slice[1], i), 1234);

        // class field
        // FIXME: we should not have to make all these unsafe conversions manually
        let class_slice: &[&mut tstiop__test_class__t] = iop_get!(obj, o).into();
        assert_eq!(class_slice.len(), 0);

        let mut cls1 = iop_new!(tstiop__test_class_child2, { u: 5 });
        let mut cls2 = iop_new!(tstiop__test_class_child);
        let classes = [
            unsafe { &mut *ptr::from_mut(&mut cls1).cast::<tstiop__test_class__t>() },
            unsafe { &mut *ptr::from_mut(&mut cls2).cast::<tstiop__test_class__t>() },
        ];
        iop_set!(obj, { o: classes.as_slice() });
        let class_slice: &[&mut tstiop__test_class__t] = iop_get!(obj, o).into();
        assert_eq!(class_slice.len(), 2);

        // union field
        let union_slice: &[tstiop__test_union__u<'_>] = iop_get!(obj, un).into();
        assert_eq!(union_slice.len(), 0);
        let un1 = iop_new!(tstiop__test_union::i, 56);
        let un2 = iop_new!(tstiop__test_union::s, lstr::from_str(&test_str));
        let uns = [un1, un2];
        iop_set!(obj, { un: uns.as_slice() });
        let union_slice: &[tstiop__test_union__u<'_>] = iop_get!(obj, un).into();
        assert_eq!(union_slice.len(), 2);
        assert!(matches!(&union_slice[0],
                     tstiop__test_union__u::i(val) if *val == 56));
        assert!(matches!(&union_slice[1],
                         tstiop__test_union__u::s(val)
                         if *val == lstr::from_str("Hello world")));

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
        // NOTE: a ref field is generated as `&'a mut T<'a>`, which ties the
        // borrow to the referenced value's own lifetime parameter: once
        // lent, `st` and `un` can never be named again, not even after
        // `obj` dies. Aliasing is therefore checked by address identity
        // rather than by reading the referenced value back. Whole-value
        // union access goes through the plain `obj.field` (not
        // `iop_get!`), since `iop_get!` would try to move the `&mut`
        // reference out of `obj`.
        let mut st = iop_new!(tstiop__test_struct, { i: 123 });
        let mut un = iop_new!(tstiop__test_union::i, 56);
        let st_addr = ptr::from_ref(&st);
        let un_addr = ptr::from_ref(&un);
        let obj = iop_new!(tstiop__full_ref, { st: &mut st, un: &mut un });

        // reference struct: the field points at `st` itself, not a copy.
        assert!(ptr::eq(ptr::from_ref(&*obj.st), st_addr));
        assert_eq!(iop_get!(obj.st, i), 123);
        assert!(iop_get!(obj.st, s).is_null());
        iop_set!(obj.st, { i: 124 });
        assert_eq!(iop_get!(obj.st, i), 124);

        // reference union: same, and re-pointing writes through the
        // reference rather than replacing it.
        assert!(ptr::eq(ptr::from_ref(&*obj.un), un_addr));
        assert!(matches!(&*obj.un, tstiop__test_union__u::i(val)
                         if *val == 56));
        *obj.un = iop_new!(tstiop__test_union::i, 57);
        assert!(ptr::eq(ptr::from_ref(&*obj.un), un_addr));
        assert!(matches!(&*obj.un, tstiop__test_union__u::i(val)
                         if *val == 57));
    }

    // }}}
    // {{{ Unions manipulation

    /// Test union variants holding a reference to the value they carry
    #[test]
    #[allow(clippy::float_cmp)]
    fn iop_union_ref_variants() {
        // scalar fields
        let un = tstiop__my_union_c__u::i_of_c(12);
        assert!(matches!(&un, tstiop__my_union_c__u::i_of_c(val) if *val == 12));

        let un = tstiop__my_union_c__u::d_of_c(13.);
        assert!(matches!(&un, tstiop__my_union_c__u::d_of_c(val) if *val == 13.));

        match un {
            tstiop__my_union_c__u::i_of_c(_) => panic!("unexpected i_of_c"),
            tstiop__my_union_c__u::d_of_c(val) => assert_eq!(val, 13.),
        }

        // reference fields
        // NOTE: each re-point of a ref-like union field below uses a fresh
        // binding (rather than mutating the previously-referenced value in
        // place) since the union holds a real `&mut` borrow of it for as
        // long as the union itself is alive.
        let mut st = iop_new!(tstiop__my_referenced_struct, { a: 42 });
        let st_addr = ptr::from_ref(&st);
        let mut un = iop_new!(tstiop__my_ref_union::s, &mut st);

        // The variant aliases `st` itself, and writing through it is
        // visible on the next read through the variant.
        if let tstiop__my_ref_union__u::s(val) = &mut un {
            assert!(ptr::eq(ptr::from_ref(&**val), st_addr));
            iop_set!(**val, { a: 43 });
        } else {
            panic!("expected struct field");
        }
        assert!(matches!(&un, tstiop__my_ref_union__u::s(val) if val.a == 43));

        let mut run = iop_new!(tstiop__my_referenced_union::b, 56);
        let run_addr = ptr::from_ref(&run);
        let mut un = iop_new!(tstiop__my_ref_union::u, &mut run);

        if let tstiop__my_ref_union__u::u(val_u) = &mut un {
            assert!(ptr::eq(ptr::from_ref(&**val_u), run_addr));
            assert!(matches!(**val_u, tstiop__my_referenced_union__u::b(b)
                             if b == 56));
            **val_u = iop_new!(tstiop__my_referenced_union::b, 57);
        } else {
            panic!("expected union field");
        }
        assert!(matches!(&un, tstiop__my_ref_union__u::u(val_u)
                         if matches!(**val_u,
                                     tstiop__my_referenced_union__u::b(b)
                                     if b == 57)));
    }

    /// Test union variants holding their value directly
    #[test]
    fn iop_union_value_variants() {
        // scalar field
        let un = tstiop__get_bpack_sz_u__u::i8(12);
        assert!(matches!(un, tstiop__get_bpack_sz_u__u::i8(val) if val == 12));

        // string field
        let un = tstiop__get_bpack_sz_u__u::s(lstr::from_str("coucou world"));
        assert!(matches!(un,
                         tstiop__get_bpack_sz_u__u::s(val)
                         if val == lstr::from_str("coucou world")));

        // enum field
        let un = tstiop__get_bpack_sz_u__u::en(tstiop__get_bpack_sz_en__e::B);
        assert!(matches!(un,
                         tstiop__get_bpack_sz_u__u::en(val)
                         if val == tstiop__get_bpack_sz_en__e::B));

        // struct field
        let un = tstiop__get_bpack_sz_u__u::st(iop_new!(tstiop__get_bpack_sz_st, { a: 42 }));
        assert!(matches!(un, tstiop__get_bpack_sz_u__u::st(val) if iop_get!(val, a) == 42));
    }

    /// Test the variants of the void members of a union. They carry no
    /// value, and C reads the same member as Rust.
    #[test]
    fn iop_union_void_variants() {
        let un = tstiop_void_type__void_alone__u::field;
        assert_eq!(un.as_json(), "{ \"field\": null }\n");

        let un = tstiop_void_type__void_alone__u::other(3);
        assert_eq!(un.as_json(), "{ \"other\": 3 }\n");

        let un = tstiop_void_type__void_only__u::b;
        assert_eq!(un.as_json(), "{ \"b\": null }\n");
    }

    /// Test reassigning a union to a different variant: the old value must
    /// be dropped correctly. With the union represented as a real Rust
    /// `enum`, this is now compiler-guaranteed by ordinary move/assignment
    /// semantics — no manual drop bookkeeping needed, even for a nested
    /// union-typed variant.
    #[test]
    // Several `un = Variant(...)` assignments below exist purely to be
    // immediately overwritten, exercising the drop of the previous value —
    // the lint can't tell that from a data-flow read.
    #[allow(unused_assignments)]
    fn iop_union_set_variant() {
        // Primitive fields: plain reassignment.
        let mut un = tstiop__test_union__u::i(1);
        assert!(matches!(un, tstiop__test_union__u::i(v) if v == 1));

        un = tstiop__test_union__u::s(lstr::from_str("hello"));
        assert!(matches!(un, tstiop__test_union__u::s(v) if v == lstr::from_str("hello")));

        un = tstiop__test_union__u::i(42);
        assert!(matches!(un, tstiop__test_union__u::i(v) if v == 42));

        // Nested union field: switching away from it must drop the old
        // value without leaking or double-freeing.
        let mut un = tstiop__my_union_b__u::bval(10);
        un = tstiop__my_union_b__u::a(tstiop__my_union_a__u::us(lstr::from_str("nested")));
        assert!(matches!(un, tstiop__my_union_b__u::a(v) if
            matches!(v, tstiop__my_union_a__u::us(s) if s == lstr::from_str("nested"))));

        un = tstiop__my_union_b__u::bval(99);
        assert!(matches!(un, tstiop__my_union_b__u::bval(v) if v == 99));

        // Switch back to the union-typed variant, then overwrite it again
        // while already active: the old nested value must still be dropped.
        un = tstiop__my_union_b__u::a(tstiop__my_union_a__u::ua(7));
        un = tstiop__my_union_b__u::a(tstiop__my_union_a__u::ua(8));
        assert!(matches!(un, tstiop__my_union_b__u::a(v) if
            matches!(v, tstiop__my_union_a__u::ua(i) if i == 8)));

        // Escaped field names: the enum variant keeps the natural name
        // (`i8`) even though the underlying field is `i8__var` in the IOP
        // source.
        let mut un = tstiop__get_bpack_sz_u__u::i8(1);
        un = tstiop__get_bpack_sz_u__u::i8(5);
        assert!(matches!(un, tstiop__get_bpack_sz_u__u::i8(v) if v == 5));

        un = tstiop__get_bpack_sz_u__u::u8(6);
        assert!(matches!(un, tstiop__get_bpack_sz_u__u::u8(v) if v == 6));

        // Ref-like fields: the variant just holds a `&mut T`; the
        // referenced value is borrowed, never owned or dropped by the
        // union itself.
        let mut st = iop_new!(tstiop__my_referenced_struct, { a: 1 });
        let mut run = tstiop__my_referenced_union__u::b(2);
        let mut un = tstiop__my_ref_union__u::s(&mut st);
        un = tstiop__my_ref_union__u::u(&mut run);
        assert!(matches!(un, tstiop__my_ref_union__u::u(v) if
            matches!(&*v, tstiop__my_referenced_union__u::b(b) if *b == 2)));
    }

    /// Test the `iop_new!`/`iop_set!`/`iop_get!` macro plumbing against the
    /// `ic` package: so far it had only been exercised on `tstiop`, so this
    /// makes sure it isn't package-specific.
    #[test]
    fn iop_ic_package_get_set_new() {
        // Plain struct, required scalar fields.
        let mut tracer = iop_new!(ic__tracer, { token: 1, epoch: 2 });
        assert_eq!(iop_get!(tracer, token), 1);
        assert_eq!(iop_get!(tracer, epoch), 2);
        iop_set!(tracer, { token: 10 });
        assert_eq!(iop_get!(tracer, token), 10);

        // Struct with a default value (`payload`), plus optional
        // string/bool/u64 fields.
        let mut hdr = iop_new!(ic__simple_hdr);
        assert_eq!(iop_get!(hdr, payload), -1);
        iop_set!(hdr, { payload: 42 });
        assert_eq!(iop_get!(hdr, payload), 42);

        assert!(iop_get!(hdr, login).is_null());
        iop_set!(hdr, { login: lstr::from_str("bob") });
        assert_eq!(iop_get!(hdr, login), lstr::from_str("bob"));

        let dealias_opt: Option<bool> = iop_get!(hdr, dealias).into();
        assert_eq!(dealias_opt, None);
        iop_set!(hdr, { dealias: Some(true) });
        let dealias_opt: Option<bool> = iop_get!(hdr, dealias).into();
        assert_eq!(dealias_opt, Some(true));

        let workspace_id_opt: Option<u64> = iop_get!(hdr, workspace_id).into();
        assert_eq!(workspace_id_opt, None);
        iop_set!(hdr, { workspace_id: Some(7) });
        let workspace_id_opt: Option<u64> = iop_get!(hdr, workspace_id).into();
        assert_eq!(workspace_id_opt, Some(7));

        // Union: reassigning to the `simple` variant drops whatever variant
        // was active before.
        let mut un = ic__hdr__u::simple(iop_new!(ic__simple_hdr, { payload: 1 }));
        assert!(matches!(un, ic__hdr__u::simple(v) if v.payload == 1));
        un = ic__hdr__u::simple(iop_new!(ic__simple_hdr, { payload: 2 }));
        assert!(matches!(un, ic__hdr__u::simple(v) if v.payload == 2));
    }

    // }}}
    // {{{ IOP dup / Owned tests

    #[test]
    fn dup_compiled_struct() {
        // A compiled (`CStructUnion`) value dups into a one-pointer
        // `OwnedStruct` that derefs straight to the concrete type and owns its
        // own deep copy of the blob.
        let src = ic__simple_hdr__t::new();
        let owned = src.dup();

        // Deref exposes the concrete fields directly.
        assert_eq!(owned.payload, -1);
        // The owned copy is content-equal to the source.
        assert_eq!(owned.as_json(), src.as_json());
    }

    #[test]
    fn dup_generic_outlives_source() {
        // `dup` on a `GenericStructUnion` yields a lifetime-free `OwnedGeneric`
        // that clones the `EnvCtx`, so it stays valid after both the source
        // handle, its `t_scope`, and the ctx it was resolved from are gone.
        let owned = {
            let ctx = setup_env_with_packages();
            let t_scope = TScope::new_scope();
            let desc = ctx
                .get_struct_desc("ic.Tracer")
                .expect("ic.Tracer should exist");
            let obj = ctx
                .t_junpack_desc(&t_scope, r#"{"token": 1, "epoch": 2}"#, desc, 0)
                .expect("valid JSON should unpack");

            obj.dup()
            // `obj`, `t_scope` and `ctx` are all dropped here.
        };

        // Descriptor kept alive through the cloned ctx: still packable, and its
        // deep-copied content survived the source going away (ASAN-checked).
        assert_eq!(
            owned.as_json(),
            "{\n    \"token\": 1,\n    \"epoch\": 2\n}\n"
        );
    }

    // }}}
    // {{{ @ctype and @alias

    use crate::bindings::{
        core__http_code__array_t_raw, core__http_code__opt_t_raw, http_code__array_t,
        http_code__opt_t, tstiop__first_diff_c1__t, tstiop__first_diff_st__array_t_raw,
        tstiop__first_diff_st__t, tstiop__my_enum_a__e, tstiop_void_type__void_only__tag_t,
        z_first_diff_c1__t, z_first_diff_st__array_t, z_first_diff_st__t, z_first_diff_st__t_raw,
        z_void_only__tag_t,
    };

    #[test]
    fn an_alias_of_an_enum_value_is_that_value() {
        assert_eq!(tstiop__my_enum_a__e::A_ALIAS, tstiop__my_enum_a__e::A);
        assert_eq!(tstiop__my_enum_a__e::C_ALIAS_1, tstiop__my_enum_a__e::C);
        assert_eq!(tstiop__my_enum_a__e::C_ALIAS_2, tstiop__my_enum_a__e::C);
    }

    #[test]
    fn a_ctype_names_the_type() {
        // The `@ctype` name keeps the lifetime of a borrowing type, and its
        // `_raw` alias pins it to `'static` like the other C names.
        let _ = |st: z_first_diff_st__t<'static>| -> tstiop__first_diff_st__t<'static> { st };
        let _ = |st: z_first_diff_st__t_raw| -> tstiop__first_diff_st__t<'static> { st };
        let _ = |cl: z_first_diff_c1__t| -> tstiop__first_diff_c1__t { cl };

        // The `@ctype` of a union names its tag enum too.
        let _ = |tag: z_void_only__tag_t| -> tstiop_void_type__void_only__tag_t { tag };

        // It names the C array of the type, and the C option of an enum.
        let _ = |a: z_first_diff_st__array_t| -> tstiop__first_diff_st__array_t_raw { a };
        let _ = |a: http_code__array_t| -> core__http_code__array_t_raw { a };
        let _ = |o: http_code__opt_t| -> core__http_code__opt_t_raw { o };
    }

    #[test]
    fn the_tag_enum_of_a_union_holds_the_tags_that_the_iop_declares() {
        assert_eq!(
            tstiop_void_type__void_only__tag_t::tstiop_void_type__void_only__a__ft as u32,
            1
        );
        assert_eq!(
            tstiop_void_type__void_only__tag_t::tstiop_void_type__void_only__b__ft as u32,
            2
        );
    }

    // }}}
    // {{{ Constants

    // The C header defines these constants with `#define`.

    use crate::bindings::{
        MY_ENUM_A_count, MY_ENUM_A_max, MY_ENUM_A_min, tstiop__my_class2__class_id,
        tstiop__my_enum_a__edesc, z_first_diff_c1__class_id,
    };
    use std::slice;

    #[test]
    fn the_constants_of_an_enum_match_its_descriptor() {
        let desc = unsafe { &tstiop__my_enum_a__edesc };
        let values = unsafe { slice::from_raw_parts(desc.values, desc.enum_len.into()) };

        assert_eq!(MY_ENUM_A_count, i32::from(desc.enum_len));
        assert_eq!(Some(&MY_ENUM_A_min), values.iter().min());
        assert_eq!(Some(&MY_ENUM_A_max), values.iter().max());
    }

    #[test]
    fn a_class_id_is_the_one_that_the_iop_declares() {
        // `class MyClass2 : 2` and `class FirstDiffC1 : 1`, whose `@ctype`
        // names the class id too.
        assert_eq!(tstiop__my_class2__class_id, 2);
        assert_eq!(z_first_diff_c1__class_id, 1);
    }

    // }}}
    // {{{ RPCs and modules

    use crate::bindings::{
        iop__void__t, iop_rpc_t, tstiop__both_iface__a__TAG, tstiop__both_iface__a__fun_c__rpc,
        tstiop__both_iface__iface__TAG, tstiop__both_iface__mod, tstiop__iface__if,
        tstiop__my_iface_a__fun_a__rpc__tag, tstiop__my_iface_a__fun_a_args__s,
        tstiop__my_iface_a__fun_a_args__t, tstiop__my_iface_a__fun_async__rpc__async,
        tstiop__my_iface_a__fun_async__rpc__tag, tstiop__my_iface_a__fun_c__rpc__async,
        tstiop__my_iface_a__fun_c__rpc__tag, tstiop__my_iface_a__fun_c_args__t,
        tstiop__my_iface_a__fun_c_res__t, tstiop__my_iface_a__fun_h_res__t, tstiop__my_iface_a__if,
        tstiop__my_struct_a__s, tstiop__my_struct_a__t, tstiop__my_struct_b__s,
        tstiop__my_struct_b__t, tstiop_cross_mount__cross_mount__a__fun_a__rpc,
        tstiop_cross_mount__cross_mount__mod, tstiop_dox__my_iface__fun_c__rpc__tag,
        tstiop_dox__my_module__iface__fun_c__rpc, tstiop_dox__my_struct__s,
    };

    /// The RPC of the interface `MyIfaceA` with the tag `tag`, from its C
    /// descriptor.
    fn my_iface_a_rpc(tag: u16) -> &'static iop_rpc_t {
        let iface = unsafe { &tstiop__my_iface_a__if };
        let rpcs = unsafe { slice::from_raw_parts(iface.funs, iface.funs_len.into()) };

        rpcs.iter()
            .find(|rpc| rpc.tag == u32::from(tag))
            .expect("the interface should have the RPC")
    }

    #[test]
    fn an_rpc_reusing_a_struct_names_that_struct() {
        // `funC in MyStructA out MyStructB`. The alias keeps the lifetime of
        // the struct.
        let _ = |a: tstiop__my_iface_a__fun_c_args__t<'static>| -> tstiop__my_struct_a__t<'static> {
            a
        };
        let _ =
            |r: tstiop__my_iface_a__fun_c_res__t<'static>| -> tstiop__my_struct_b__t<'static> { r };
        // `funH in void out void`.
        let _ = |r: tstiop__my_iface_a__fun_h_res__t| -> iop__void__t { r };

        let rpc = my_iface_a_rpc(tstiop__my_iface_a__fun_c__rpc__tag);

        assert_eq!(rpc.args, &raw const tstiop__my_struct_a__s);
        assert_eq!(rpc.result, &raw const tstiop__my_struct_b__s);
    }

    #[test]
    fn the_arguments_of_an_rpc_are_its_own_struct() {
        // `funA in (int a, MyStructA b)`.
        let rpc = my_iface_a_rpc(tstiop__my_iface_a__fun_a__rpc__tag);

        assert_eq!(rpc.args, &raw const tstiop__my_iface_a__fun_a_args__s);
    }

    #[test]
    fn the_constants_of_an_rpc_match_its_descriptor() {
        // `funC` is the third RPC, and `funAsync in (int test) out null`.
        assert_eq!(tstiop__my_iface_a__fun_c__rpc__tag, 3);
        for (tag, is_async) in [
            (
                tstiop__my_iface_a__fun_c__rpc__tag,
                tstiop__my_iface_a__fun_c__rpc__async,
            ),
            (
                tstiop__my_iface_a__fun_async__rpc__tag,
                tstiop__my_iface_a__fun_async__rpc__async,
            ),
        ] {
            assert_eq!(my_iface_a_rpc(tag).async_() != 0, is_async);
        }
    }

    #[test]
    fn the_tag_of_an_interface_in_a_module_matches_its_descriptor() {
        // `module BothIface { MyIfaceA a; Iface iface; }`.
        let module = unsafe { &tstiop__both_iface__mod };
        let aliases = unsafe { slice::from_raw_parts(module.ifaces, module.ifaces_len.into()) };
        let iface_of = |tag: u16| {
            aliases
                .iter()
                .find(|alias| alias.tag == u32::from(tag))
                .map(|alias| alias.iface)
        };

        assert_eq!(
            iface_of(tstiop__both_iface__a__TAG),
            Some(&raw const tstiop__my_iface_a__if)
        );
        assert_eq!(
            iface_of(tstiop__both_iface__iface__TAG),
            Some(&raw const tstiop__iface__if)
        );
    }

    #[test]
    fn the_markers_of_an_rpc_name_its_c_descriptors() {
        // `module BothIface { MyIfaceA a; ... }`, and `funC` has the tag 3.
        type Rpc = tstiop__both_iface__a__fun_c__rpc;
        let rpc = unsafe { &*Rpc::rpc_desc() };

        assert_eq!(rpc.tag, u32::from(tstiop__my_iface_a__fun_c__rpc__tag));
        assert_eq!(rpc.args, &raw const tstiop__my_struct_a__s);
        assert_eq!(Rpc::CMD, (1 << 16) | 3);
        assert_eq!(
            <Rpc as IopRPC>::Iface::iface_desc(),
            &raw const tstiop__my_iface_a__if
        );
        assert_eq!(
            <<Rpc as IopRPC>::Iface as IopIface>::Module::mod_desc(),
            &raw const tstiop__both_iface__mod
        );
    }

    #[test]
    fn the_marker_of_an_rpc_points_after_the_aliases_of_the_previous_ones() {
        // The descriptor has an entry for each `@alias` of an RPC, after
        // the entry of the RPC.
        // `@alias(funBal1) @alias(funBal2) funB`, then `funC`.
        let fun_c = unsafe { &*tstiop_dox__my_module__iface__fun_c__rpc::rpc_desc() };

        assert_eq!(fun_c.tag, u32::from(tstiop_dox__my_iface__fun_c__rpc__tag));
        assert_eq!(
            fun_c.args, &raw const tstiop_dox__my_struct__s,
            "funC takes a MyStruct"
        );
    }

    #[test]
    fn the_markers_of_a_mount_from_another_package() {
        // `module CrossMount { tstiop.MyIfaceA a; }` in its own package.
        type Rpc = tstiop_cross_mount__cross_mount__a__fun_a__rpc;

        // The anonymous struct of `funA` holds a `MyStructA`, which
        // borrows: the marker keeps its lifetime.
        fn same_arg(a: <Rpc as IopRPC>::Arg<'_>) -> tstiop__my_iface_a__fun_a_args__t<'_> {
            a
        }

        let rpc = unsafe { &*Rpc::rpc_desc() };

        assert_eq!(rpc.tag, u32::from(tstiop__my_iface_a__fun_a__rpc__tag));
        assert_eq!(rpc.args, &raw const tstiop__my_iface_a__fun_a_args__s);
        assert_eq!(Rpc::CMD, (1 << 16) | 1);
        assert_eq!(
            <Rpc as IopRPC>::Iface::iface_desc(),
            &raw const tstiop__my_iface_a__if
        );
        assert_eq!(
            <<Rpc as IopRPC>::Iface as IopIface>::Module::mod_desc(),
            &raw const tstiop_cross_mount__cross_mount__mod
        );
        let _ = same_arg;
    }

    #[test]
    fn the_marker_of_an_rpc_names_its_types() {
        // `funC in MyStructA out MyStructB`, a struct of the package.
        type Rpc = tstiop__both_iface__a__fun_c__rpc;
        let _ = |a: <Rpc as IopRPC>::Arg<'static>| -> tstiop__my_struct_a__t<'static> { a };
        let _ = |r: <Rpc as IopRPC>::Res<'static>| -> tstiop__my_struct_b__t<'static> { r };
        let _ = |e: <Rpc as IopRPC>::Exn<'static>| -> iop__void__t { e };
    }

    // }}}
    // {{{ Typedef aliases

    // Nothing else looks at the aliases `iopc` writes for a typedef: they
    // carry no descriptor of their own, so neither the layout tests nor the
    // generated ones reach them.
    //
    // What they must hold is that an alias names the very type a field
    // declared with that typedef gets, so each binding below fails to
    // compile if the two drift apart.

    #[test]
    fn a_repeated_typedef_aliases_the_array_the_field_gets() {
        let obj = <tstiop_typedef__array_test__t<'static>>::new();

        let _: &tstiop_typedef__basic_struct_list__t<'static> = &obj.list1;
        let _: &tstiop_typedef__enum1_list__e<'static> = &obj.list2;
        let _: &tstiop_typedef__enum2_list__e<'static> = &obj.list3;
        let _: &tstiop_typedef__number_struct_list__t<'static> = &obj.list4;
    }

    #[test]
    fn a_typedef_of_another_package_aliases_the_type_it_names() {
        // `tstiop_typedef` names types of `tstiop` through typedefs, and
        // declares the fields of this struct with these typedefs.
        let mut parent = iop_new!(tstiop__my_parent);
        let obj = iop_new!(tstiop_typedef__struct_with_typedefs_from_ext, {
            tdef_c: &mut parent,
        });

        let _: &tstiop_typedef__remote_struct__t = &obj.tdef_s;
        let _: &tstiop_typedef__remote_enum__e = &obj.tdef_e;

        // A class is the exception: the alias names the class, and the
        // field holds it through a reference that the typedef knows
        // nothing about.
        let _: &&mut tstiop_typedef__remote_class__t = &obj.tdef_c;
    }

    #[test]
    fn a_raw_alias_of_a_typedef_names_its_c_type() {
        // bindgen renames each C name of a typedef with `_raw`. In C, the
        // name designates the struct, union, class or enum of the typedef,
        // without the array of its declaration.
        let _ = |en: tstiop_typedef__enum1_list__t_raw| -> tstiop_backward_compat__enum1__e { en };
        let _ = |st: tstiop_typedef__remote_struct__t_raw| -> tstiop__basic_struct__t { st };
        let _ = |cl: tstiop_typedef__remote_typedef_class__t_raw| -> tstiop__small_class__t { cl };
    }

    // }}}
    // {{{ Non-strict enums

    #[test]
    fn a_non_strict_enum_holds_a_value_that_it_does_not_name() {
        let t_scope = TScope::new_scope();
        let obj = ENV
            .t_junpack_desc(&t_scope, r#"{"e": 7}"#, tstiop__full_opt__t::CDESC, 0)
            .expect("a non-strict enum should accept any value");
        // SAFETY: `obj` holds a `tstiop.FullOpt`, which `t_scope` keeps
        // alive.
        let opt = unsafe { &*obj.get_cptr().cast::<tstiop__full_opt__t<'_>>() };

        assert_eq!(Option::from(opt.e), Some(tstiop__test_enum__e(7)));
    }

    // }}}
    // {{{ Rust keywords

    #[test]
    fn a_name_that_is_a_rust_keyword_gets_a_suffix() {
        let mut obj = <tstiop_rust_keywords__keywords__t>::new();

        iop_set!(obj, { match__var: 1, type__var: 2, u8__var: 3 });
        assert_eq!(iop_get!(obj, match__var), 1);
        assert_eq!(iop_get!(obj, type__var), 2);
        assert_eq!(iop_get!(obj, u8__var), 3);

        let un = tstiop_rust_keywords__keyword_union__u::ref__var(4);

        assert!(matches!(
            un,
            tstiop_rust_keywords__keyword_union__u::ref__var(4)
        ));

        let un = tstiop_rust_keywords__keyword_union__u::u8(5);

        assert!(matches!(un, tstiop_rust_keywords__keyword_union__u::u8(5)));
    }

    // }}}
}
