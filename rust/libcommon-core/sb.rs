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
use std::ops::{Deref, DerefMut};
use std::pin::Pin;
use std::ptr;
use std::slice::from_raw_parts;
use std::str::Utf8Error;

use crate::bindings::{mem_pool_libc, mem_pool_static, sb_t, sb_wipe};

/// `sb_t` with a initial buffer of size N on the stack.
///
/// We need to use an external pin buffer on the stack to make it work and avoid dangling pointers.
/// See <https://doc.rust-lang.org/std/pin/index.html> and
/// <https://github.com/dureuill/stackpin/blob/keep_only_stacklet/src/lib.rs>
///
/// TODO: Use a macro to hide this.
pub struct SbStack<'pin> {
    buf: Pin<&'pin mut [u8]>,
    sb: sb_t,
}

impl<'pin> SbStack<'pin> {
    /// Create a new string buffer with an initial buffer on the stack.
    #[allow(clippy::cast_possible_truncation, clippy::cast_possible_wrap)]
    pub fn new(buffer: Pin<&'pin mut [u8]>) -> Self {
        let mut res = Self {
            buf: buffer,
            sb: sb_t {
                data: ptr::null_mut(),
                size: 0,
                mp: &raw mut mem_pool_static,
                len: 0,
                skip: 0,
            },
        };
        let buf_ref = res.buf.as_mut().get_mut();
        res.sb.data = buf_ref.as_mut_ptr().cast();
        res.sb.size = buf_ref.len() as i32;
        res
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

impl Drop for SbStack<'_> {
    fn drop(&mut self) {
        unsafe {
            sb_wipe(&raw mut self.sb);
        }
    }
}

impl Deref for SbStack<'_> {
    type Target = sb_t;

    fn deref(&self) -> &Self::Target {
        &self.sb
    }
}

impl DerefMut for SbStack<'_> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.sb
    }
}

/// `sb_t` with allocated on the heap.
pub struct SbHeap {
    sb: sb_t,
}

impl SbHeap {
    /// Create a string on the heap.
    pub fn new() -> Self {
        Self {
            sb: sb_t {
                data: ptr::null_mut(),
                size: 0,
                mp: &raw mut mem_pool_libc,
                len: 0,
                skip: 0,
            },
        }
    }
}

impl Default for SbHeap {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for SbHeap {
    fn drop(&mut self) {
        unsafe {
            sb_wipe(&raw mut self.sb);
        }
    }
}

impl Deref for SbHeap {
    type Target = sb_t;

    fn deref(&self) -> &Self::Target {
        &self.sb
    }
}

impl DerefMut for SbHeap {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.sb
    }
}

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
