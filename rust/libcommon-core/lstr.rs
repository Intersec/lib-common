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

//! Conversion between a C lstr_t and Rust string/bytes types.
//!
//! A converted lstr_t from and to Rust is always non-owned.
//! If you want a owned string, consider using primitive strings in Rust.

use std::convert::TryFrom;
use std::fmt;
use std::ops::Deref;
use std::os::raw::c_char;
use std::ptr::null;
use std::slice::from_raw_parts;
use std::str::{FromStr, Utf8Error};

use crate::bindings::*;

pub use crate::bindings::lstr_t;

/// Use libc strlen() to compute the length of C string.
///
/// # Safety
///
/// ptr must not be null and must point to a valid C string.
#[inline]
pub unsafe fn libc_strlen(ptr: *const c_char) -> u64 {
    unsafe extern "C" {
        /// Provided by libc or compiler_builtins.
        fn strlen(s: *const c_char) -> u64;
    }

    unsafe { strlen(ptr) }
}

impl lstr_t {
    /// Create a null lstr_t.
    pub const fn null() -> Self {
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: null() },
            len: 0,
            mem_pool: 0,
        }
    }

    /// Create an empty lstr_t.
    pub const fn empty() -> Self {
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: c"".as_ptr() },
            len: 0,
            mem_pool: 0,
        }
    }

    /// Create a non-owned lstr_t from a Rust bytes slice.
    pub const fn from_bytes(bytes: &[u8]) -> Self {
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 {
                s: bytes.as_ptr() as *const i8,
            },
            len: bytes.len() as i32,
            mem_pool: 0,
        }
    }

    /// Create a non-owned lstr_t from a Rust string.
    #[allow(clippy::should_implement_trait)]
    pub const fn from_str(str: &str) -> Self {
        Self::from_bytes(str.as_bytes())
    }

    /// Create a non-owned lstr_t from a C raw pointer.
    ///
    /// # Safety
    ///
    /// `ptr` shall not be `null` and should point to a valid C string.
    pub unsafe fn from_ptr(ptr: *const c_char) -> Self {
        let len = unsafe { libc_strlen(ptr) };
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr },
            len: len as i32,
            mem_pool: 0,
        }
    }

    /// Create a non-owned lstr_t from a C raw pointer and length.
    pub const fn from_ptr_and_len(ptr: *const c_char, len: usize) -> Self {
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr },
            len: len as i32,
            mem_pool: 0,
        }
    }

    /// Convert a lstr_t to a bytes slice.
    pub const fn as_bytes(&self) -> &[u8] {
        let ptr = unsafe { self.__bindgen_anon_1.data as *const u8 };
        let len = self.len as usize;

        unsafe { from_raw_parts(ptr, len) }
    }

    /// Convert a lstr_t to a non-owned Rust str and check UTF-8 errors.
    pub const fn as_str(&self) -> Result<&str, Utf8Error> {
        str::from_utf8(self.as_bytes())
    }

    /// Convert a lstr_t to a non-owned Rust str without checking for UTF-8 errors.
    ///
    /// # Safety
    ///
    /// This method is unsafe because converting a slice to a string without checking UTF-8 errors
    /// is unsafe.
    pub const unsafe fn as_str_unchecked(&self) -> &str {
        unsafe { str::from_utf8_unchecked(self.as_bytes()) }
    }

    /// Get the length of the lstr.
    pub const fn len(&self) -> usize {
        self.len as usize
    }

    /// Check if the lstr_t is empty (len == 0).
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Check if the lstr_t is null (!s).
    pub const fn is_null(&self) -> bool {
        unsafe { self.__bindgen_anon_1.s.is_null() }
    }

    /// Check if two lstr_t are equals.
    #[inline]
    pub fn equals(&self, other: &lstr_t) -> bool {
        unsafe { lstr_equal(*self, *other) }
    }
}

impl From<&[u8]> for lstr_t {
    fn from(bytes: &[u8]) -> Self {
        lstr_t::from_bytes(bytes)
    }
}

impl From<&str> for lstr_t {
    fn from(str: &str) -> Self {
        lstr_t::from_str(str)
    }
}

impl<'a> TryFrom<&'a lstr_t> for &'a str {
    type Error = Utf8Error;

    fn try_from(lstr: &'a lstr_t) -> Result<Self, Self::Error> {
        lstr.as_str()
    }
}

impl FromStr for lstr_t {
    type Err = ();

    fn from_str(str: &str) -> Result<Self, ()> {
        Ok(lstr_t::from_str(str))
    }
}

impl Deref for lstr_t {
    type Target = [u8];

    #[inline]
    fn deref(&self) -> &Self::Target {
        self.as_bytes()
    }
}

impl fmt::Display for lstr_t {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Ok(s) = self.as_str() {
            write!(f, "{}", s)
        } else {
            write!(f, "{:x?}", self.as_bytes())
        }
    }
}
