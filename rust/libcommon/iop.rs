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
    iop_pkg_t, iop_register_packages, iop_struct_t, t_iop_junpack_ptr_ps, t_iop_sb_ypack,
};

use crate::{mem_stack::TScope, pstream::pstream_t, sb::Sb1K};

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

    /// Export the IOP struct or union as YAML
    fn as_yaml(&self) -> String {
        let _t_scope = TScope::new_scope();
        let mut sb = Sb1K::new();

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
pub struct Env {
    env: *mut iop_env_t,
    owned: bool,
}

impl Env {
    /// Create a new owned IOP env.
    pub fn new() -> Self {
        Self {
            env: unsafe { iop_env_new() },
            owned: true,
        }
    }

    /// Create a non-owned Rust IOP env from an existing C IOP env.
    pub fn from_ptr(env: *mut iop_env_t) -> Self {
        Self { env, owned: false }
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
        #[allow(clippy::cast_possible_truncation, clippy::cast_possible_wrap)]
        unsafe {
            iop_register_packages(self.env, pkgs.as_ptr(), pkgs.len() as i32);
        };
    }

    /// Get a IOP struct or union from its fullname.
    pub fn get_struct_desc(&self, fullname: &str) -> Option<*const iop_struct_t> {
        let res = unsafe { iop_env_get_struct(self.env, fullname.into()) };

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
        _t_scope: &TScope<'t>,
        content: &str,
        st: *const iop_struct_t,
        flags: u32,
    ) -> Result<GenericStructUnion<'t>, UnpackError> {
        let mut err = Sb1K::new();
        let mut ps = pstream_t::from(content);
        let mut out = ptr::null_mut();

        #[allow(clippy::cast_possible_wrap)]
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
}

impl Default for Env {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Env {
    fn drop(&mut self) {
        if self.owned {
            let env_ptr: *mut *mut iop_env_t = &raw mut self.env;

            unsafe {
                iop_env_delete(env_ptr);
            }
        }
    }
}

// }}}
