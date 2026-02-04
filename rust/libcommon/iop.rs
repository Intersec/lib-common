/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
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

//! Module to interact with IOPs in Rust
//!
//! WIP

use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::os::raw::c_void;
use std::ptr;

use crate::bindings::{
    iop_enum_t, iop_env_delete, iop_env_get_struct, iop_env_new, iop_env_t, iop_init_desc,
    iop_pkg_t, iop_register_packages, iop_sb_jpack, iop_struct_t, t_iop_junpack_ptr_ps,
    t_iop_new_desc, t_iop_sb_ypack, t_iop_yunpack_ptr_ps,
};

use crate::lstr;
use crate::mem_stack::TScope;
use crate::pstream::pstream_t;
use libcommon_core::{SB_1k, sb::Sb};

// {{{ Errors

// TODO: use `thiserror` package to provide a nice error.
pub type UnpackError = String;

// }}}
// {{{ IOP Base

/// Base trait for IOP types.
pub trait Base {}

// }}}
// {{{ IOP Enum

/// IOP trait for enum that can be used for dyn dispatch.
pub trait Enum: Base {
    /// Get the C description of the IOP enum.
    fn get_cdesc(&self) -> *const iop_enum_t;
}

/// IOP trait implemented by a C IOP enum.
pub trait CEnum: Sized + Enum {
    /// The C description of the IOP enum.
    const CDESC: *const iop_enum_t;
}

// }}}
// {{{ IOP StructUnion

/// IOP trait for struct or union that can be used for dyn dispatch.
pub trait StructUnion: Base {
    /// Get the C description of the IOP structure or union.
    fn get_cdesc(&self) -> *const iop_struct_t;

    /// Get the C pointer of the IOP structure or union.
    fn get_cptr(&self) -> *const c_void;

    /// Get the mutable C pointer of the IOP structure or union.
    fn get_cptr_mut(&mut self) -> *mut c_void;

    /// Export the IOP struct or union as JSON
    fn as_json(&self) -> String {
        SB_1k!(sb);

        unsafe {
            iop_sb_jpack(sb.as_mut_ptr(), self.get_cdesc(), self.get_cptr(), 0);
        }

        sb.to_string()
    }

    /// Export the IOP struct or union as YAML
    fn as_yaml(&self) -> String {
        let _t_scope = TScope::new_scope();
        SB_1k!(sb);

        unsafe {
            t_iop_sb_ypack(
                sb.as_mut_ptr(),
                self.get_cdesc(),
                self.get_cptr(),
                ptr::null(),
            );
        };

        sb.to_string()
    }
}

/// IOP trait implemented by a C IOP struct or union.
pub trait CStructUnion: Sized + StructUnion {
    /// The C description of the IOP struct or union.
    const CDESC: *const iop_struct_t;

    /// Create an empty `IopStructUnion` with the default arguments.
    fn new() -> Self {
        let mut res = MaybeUninit::<Self>::uninit();

        unsafe {
            iop_init_desc(Self::CDESC, res.as_mut_ptr().cast::<c_void>());
        }

        unsafe { res.assume_init() }
    }

    /// Create a new IOP struct or union on a `t_scope`.
    ///
    /// The returned reference is only valid within the `t_scope` lifetime.
    fn t_new(_t_scope: &TScope) -> &Self {
        unsafe {
            let ptr = t_iop_new_desc(Self::CDESC);
            &*(ptr as *const Self)
        }
    }
}

// }}}
// {{{ IOP Union

/// IOP trait for union that can be used for dyn dispatch.
pub trait Union: StructUnion {}

/// IOP trait implemented by a C IOP union.
pub trait CUnion: Union + CStructUnion {}

// }}}
// {{{ IOP Struct

/// IOP trait for struct that can be used for dyn dispatch.
pub trait Struct: StructUnion {}

/// IOP trait implemented by a C IOP struct.
pub trait CStruct: Struct + CStructUnion {}

// }}}
// {{{ IOP Generic struct or union

/// Generic struct or union that contains a pointer and its description.
///
/// It implements the IOP `StructUnion` trait.
pub struct GenericStructUnion<'a> {
    cdesc: *const iop_struct_t,
    cptr: *mut c_void,
    _phantom: PhantomData<&'a c_void>,
}

impl GenericStructUnion<'_> {
    /// Create a new `GenericStructUnion`
    pub fn new(cdesc: *const iop_struct_t, cptr: *mut c_void) -> Self {
        Self {
            cdesc,
            cptr,
            _phantom: PhantomData,
        }
    }
}

impl Base for GenericStructUnion<'_> {}

impl StructUnion for GenericStructUnion<'_> {
    fn get_cdesc(&self) -> *const iop_struct_t {
        self.cdesc
    }

    fn get_cptr(&self) -> *const c_void {
        self.cptr
    }

    fn get_cptr_mut(&mut self) -> *mut c_void {
        self.cptr
    }
}

// }}}
// {{{ IOP Env

/// Wrapper around `iop_env_t` for easy manipulation in Rust.
pub struct Env<'a> {
    env: &'a mut iop_env_t,
    owned: bool,
}

impl Env<'_> {
    /// Create a new owned IOP env.
    ///
    /// # Panics
    ///
    /// `iop_env_new()` returns a NULL pointer.
    pub fn new() -> Self {
        let env = unsafe { iop_env_new() };
        Self {
            env: unsafe { env.as_mut().expect("created env should not be null") },
            owned: true,
        }
    }

    /// Create a non-owned Rust IOP env from an existing C IOP env.
    ///
    /// # Panics
    ///
    /// `env` is a NULL pointer.
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn from_ptr(env: *mut iop_env_t) -> Self {
        Self {
            env: unsafe { env.as_mut().expect("env should not be null") },
            owned: false,
        }
    }

    /// Retrieve the C IOP env pointer.
    pub fn as_ptr(&self) -> *const iop_env_t {
        self.env
    }

    /// Retrieve the C IOP env pointer as mutable.
    pub fn as_mut_ptr(&mut self) -> *mut iop_env_t {
        self.env
    }

    /// Register some IOP packages in the IOP env.
    pub fn register_packages(&mut self, pkgs: &[*const iop_pkg_t]) {
        unsafe {
            iop_register_packages(self.env, pkgs.as_ptr(), pkgs.len() as i32);
        };
    }

    /// Get a IOP struct or union from its fullname.
    pub fn get_struct_desc(&self, fullname: &str) -> Option<*const iop_struct_t> {
        let fullname_lstr = lstr::from_str(fullname);
        let res = unsafe { iop_env_get_struct(self.env, fullname_lstr.as_raw()) };

        if res.is_null() {
            return None;
        }
        Some(res)
    }

    /// Unpack an IOP struct or union as JSON on a `t_scope`.
    ///
    /// # Errors
    ///
    /// The content cannot be unpacked a valid IOP JSON for the given type.
    ///
    /// # Panics
    ///
    /// The error returned from `t_iop_junpack_ptr_ps()` is not a valid UTF-8.
    #[allow(clippy::not_unsafe_ptr_arg_deref, clippy::unwrap_in_result)]
    pub fn t_junpack_desc<'t>(
        &self,
        _t_scope: &'t TScope,
        content: &str,
        st: *const iop_struct_t,
        flags: u32,
    ) -> Result<GenericStructUnion<'t>, UnpackError> {
        SB_1k!(err);
        let mut ps = pstream_t::from(content);
        let mut out = ptr::null_mut();

        let res = unsafe {
            t_iop_junpack_ptr_ps(
                self.env,
                ps.as_mut_ptr(),
                st,
                &raw mut out,
                flags as i32,
                err.as_mut_ptr(),
            )
        };

        if res < 0 {
            return Err(err
                .as_str()
                .expect("error should be a valid UTF-8")
                .to_owned());
        }

        Ok(GenericStructUnion::new(st, out))
    }

    /// Unpack an IOP struct or union as YAML on a `t_scope`.
    ///
    /// # Errors
    ///
    /// The content cannot be unpacked a valid IOP YAML for the given type.
    ///
    /// # Panics
    ///
    /// The error returned from `t_iop_yunpack_ptr_ps()` is not a valid UTF-8.
    #[allow(clippy::not_unsafe_ptr_arg_deref, clippy::unwrap_in_result)]
    pub fn t_yunpack_desc<'t>(
        &self,
        _t_scope: &'t TScope,
        content: &str,
        st: *const iop_struct_t,
        flags: u32,
    ) -> Result<GenericStructUnion<'t>, UnpackError> {
        SB_1k!(err);
        let mut ps = pstream_t::from(content);
        let mut out = ptr::null_mut();

        let res = unsafe {
            t_iop_yunpack_ptr_ps(
                self.env,
                ps.as_mut_ptr(),
                st,
                &raw mut out,
                flags,
                ptr::null_mut(),
                err.as_mut_ptr(),
            )
        };

        if res < 0 {
            return Err(err
                .as_str()
                .expect("error should be a valid UTF-8")
                .to_owned());
        }

        Ok(GenericStructUnion::new(st, out))
    }
}

impl Default for Env<'_> {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Env<'_> {
    fn drop(&mut self) {
        if self.owned {
            let mut env_ptr = self.as_mut_ptr();
            let env_ptr_ptr: *mut *mut iop_env_t = &raw mut env_ptr;

            unsafe {
                iop_env_delete(env_ptr_ptr);
            }
        }
    }
}

// }}}
// {{{ Tests

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bindings::{core__pkg, ic__pkg};
    use crate::bindings::{
        ic__hdr__t, ic__hdr__tag_t, ic__ic_priority__e, ic__ic_priority__t, ic__simple_hdr__t,
    };
    use crate::mem_stack::TScope;

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
        // Test a C IOP structures implement the CStruct trait
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
        // Test a C IOP union implement the CUnion trait
        // FIXME: currently they implement the CStruct instead of CUnion
        fn assert_impl_union<T: CStruct>() {}
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

// }}}
