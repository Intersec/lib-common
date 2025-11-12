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
#![allow(clippy::module_name_repetitions)]

//! Module to interact with `sb_t` in Rust.
//!
//! A `sb_t` is always wrapped in a Rust struct to be able to initialize and deallocate it.

use std::fmt;
use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::ops::{Deref, DerefMut};
use std::slice::from_raw_parts;
use std::str::Utf8Error;

use crate::bindings::{
    __sb_slop, mem_pool_libc, mem_pool_static, mem_pool_t, sb_t, sb_wipe, t_pool,
};
use crate::mem_stack::TScope;

// {{{ Sb

/// Wrapper around `sb_t` for safe manipulation in Rust.
///
/// The default constructor `new()` creates a string buffer on the heap.
///
/// The macros [`SB_1k`] and [`SB_8k`], and [`t_SB_1k`] and [`t_SB_8k`], are provided as convenient
/// helpers to create `Sb` instances with 1KB or 8KB buffers on the stack and `TScope`
/// respectively.
#[repr(transparent)]
pub struct Sb<'a> {
    sb: sb_t,
    _marker: PhantomData<&'a mut [u8]>,
}

// {{{ Macro SB_1k

/// Create a `Sb` with a 1KB buffer on the stack.
///
/// # Example
///
/// ```rust
/// use libcommon_core::{SB_1k, sb::Sb};
///
/// SB_1k!(sb);
/// ```
#[macro_export]
macro_rules! SB_1k {
    ($name:ident) => {
        let mut $name = [MaybeUninit::<u8>::uninit(); 1024];
        let mut $name = Sb::new_from_stack_buffer(&mut $name);
    };
}

// }}}
// {{{ Macro SB_8k

/// Create a `Sb` with a 8KB buffer on the stack.
///
/// # Example
///
/// ```rust
/// use libcommon_core::{SB_8k, sb::Sb};
///
/// SB_8k!(sb);
/// ```
#[macro_export]
macro_rules! SB_8k {
    ($name:ident) => {
        let mut $name = [MaybeUninit::<u8>::uninit(); 8192];
        let mut $name = Sb::new_from_stack_buffer(&mut $name);
    };
}

// }}}
// {{{ Macro t_SB_1k

/// Create a `Sb` with a 1KB buffer on the `TScope`.
///
/// # Example
///
/// ```rust
/// use libcommon_core::{t_SB_1k, sb::Sb};
///
/// t_SB_1k!(&t_scope, sb);
/// ```
#[macro_export]
macro_rules! t_SB_1k {
    ($t_scope:expr, $name:ident) => {
        let mut $name = Sb::new_from_tscope($t_scope, 1024);
    };
}

// }}}
// {{{ Macro t_SB_8k

/// Create a `Sb` with a 8KB buffer on the `TScope`.
///
/// # Example
///
/// ```rust
/// use libcommon_core::{t_SB_8k, sb::Sb};
///
/// t_SB_8k!(&t_scope, sb);
/// ```
#[macro_export]
macro_rules! t_SB_8k {
    ($t_scope:expr, $name:ident) => {
        let mut $name = Sb::new_from_tscope($t_scope, 8192);
    };
}

// }}}

impl<'a> Sb<'a> {
    /// Create a string buffer on the heap.
    pub fn new() -> Self {
        Self {
            sb: sb_t {
                data: unsafe { __sb_slop.as_ptr().cast_mut() },
                size: 1,
                mp: &raw mut mem_pool_libc,
                len: 0,
                skip: 0,
            },
            _marker: PhantomData,
        }
    }

    /// Create a new string buffer with an initial buffer on the stack.
    ///
    /// This constructor variant receives an **uninitialized buffer** (`[MaybeUninit<u8>]`)
    /// provided by the caller.
    /// Only the first byte is initialized to `\0` to represent an empty string.
    ///
    /// # Panics
    ///
    /// This function will panic if the buffer's length is 0.
    pub fn new_from_stack_buffer(buffer: &'a mut [MaybeUninit<u8>]) -> Self {
        Self::new_from_mp_buffer(&raw mut mem_pool_static, buffer)
    }

    /// Create a new string buffer on the `TScope`.
    ///
    /// This constructor variant creates a buffer of length `len` on the `TScope`.
    pub fn new_from_tscope<'t>(t_scope: &'t TScope, len: usize) -> Sb<'t> {
        let buffer = t_scope.t_new_slice_uninit::<u8>(len);

        Sb::<'t>::new_from_mp_buffer(unsafe { t_pool() }, buffer)
    }

    /// Create a new string buffer with an initial buffer and its memory pool.
    ///
    /// This constructor variant receives an **uninitialized buffer** (`[MaybeUninit<u8>]`)
    /// provided by the caller and its memory pool.
    /// Only the first byte is initialized to `\0` to represent an empty string.
    ///
    /// # Panics
    ///
    /// This function will panic if the buffer's length is 0.
    pub fn new_from_mp_buffer(mp: *mut mem_pool_t, buffer: &'a mut [MaybeUninit<u8>]) -> Self {
        assert!(!buffer.is_empty(), "Buffer size must be greater than zero");

        // Write the first byte to `\0`.
        buffer[0].write(b'\0');

        // Return the safe Rust wrapper
        Self {
            sb: sb_t {
                data: buffer.as_mut_ptr().cast(),
                size: buffer.len() as i32,
                mp,
                len: 0,
                skip: 0,
            },
            _marker: PhantomData,
        }
    }

    /// Retrieve the C `sb_t` pointer.
    pub fn as_ptr(&self) -> *const sb_t {
        &raw const self.sb
    }

    /// Retrieve the C `sb_t` pointer as mutable.
    pub fn as_mut_ptr(&mut self) -> *mut sb_t {
        &raw mut self.sb
    }
}

impl Drop for Sb<'_> {
    fn drop(&mut self) {
        unsafe {
            sb_wipe(&raw mut self.sb);
        }
    }
}

impl Deref for Sb<'_> {
    type Target = sb_t;

    fn deref(&self) -> &Self::Target {
        &self.sb
    }
}

impl DerefMut for Sb<'_> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.sb
    }
}

impl Default for Sb<'_> {
    fn default() -> Self {
        Self::new()
    }
}

// }}}
// {{{ sb_t

/// Method implement for `sb_t`.
impl sb_t {
    /// Convert a `sb_t` to a bytes slice.
    pub const fn as_bytes(&self) -> &[u8] {
        unsafe { from_raw_parts(self.data.cast::<u8>(), self.len as usize) }
    }

    /// Convert a `sb_t` to a non-owned Rust str and check UTF-8 errors.
    ///
    /// # Errors
    ///
    /// The `sb_t` is not a valid UTF-8 string.
    pub const fn as_str(&self) -> Result<&str, Utf8Error> {
        str::from_utf8(self.as_bytes())
    }

    /// Convert a `sb_t` to a non-owned Rust str without checking for UTF-8 errors.
    ///
    /// # Safety
    ///
    /// This method is unsafe because converting a slice to a string without checking UTF-8 errors
    /// is unsafe.
    pub const unsafe fn as_str_unchecked(&self) -> &str {
        unsafe { str::from_utf8_unchecked(self.as_bytes()) }
    }

    // TODO: add method to manipulate the string buffer.
}

impl Deref for sb_t {
    type Target = [u8];

    #[inline]
    fn deref(&self) -> &Self::Target {
        self.as_bytes()
    }
}

impl fmt::Display for sb_t {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Ok(s) = self.as_str() {
            f.write_str(s)
        } else {
            write!(f, "{:x?}", self.as_bytes())
        }
    }
}

// }}}
