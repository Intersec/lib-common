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

use std::mem::MaybeUninit;
use std::os::raw::c_void;

use crate::bindings::{
    iop_enum_t, iop_env_delete, iop_env_new, iop_env_t, iop_init_desc, iop_struct_t,
};

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
