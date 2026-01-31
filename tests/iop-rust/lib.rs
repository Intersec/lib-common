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

// Re-export iop module at crate root so generated bindings can use `crate::iop::`
// FIXME this should not be necessary
pub use libcommon::iop;

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    use crate::iop;
    pub use libcommon::bindings::*;
    waf_cargo_build::include_bindings!();
}

#[cfg(test)]
mod iop_tests {
    use libcommon::iop::{
        CEnum, CStruct, CStructUnion as _, CUnion, Enum as _, Env, StructUnion as _,
    };
    use libcommon::mem_stack::TScope;
    use std::ptr;

    use crate::bindings::{
        core__pkg, ic__hdr__t, ic__hdr__tag_t, ic__ic_priority__e, ic__ic_priority__t, ic__pkg,
        ic__simple_hdr__t,
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
    fn iop_struct_trait() {
        // Test a C IOP structure implements the CStruct trait
        fn assert_impl_struct<T: CStruct>() {}
        assert_impl_struct::<ic__simple_hdr__t>();

        // Test IOP structs have a new() method, that set the default values
        let hdr = ic__simple_hdr__t::new();
        assert_eq!(hdr.payload, -1);

        // Test they have a json packing method
        let json = hdr.as_json();
        assert_eq!(json, "{\n    \"payload\": -1\n}\n");
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
}
