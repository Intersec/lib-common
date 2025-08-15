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

//! Conversion between a C `pstream_t` and Rust string/bytes types.
//!
//! A `pstream_t` is unsafe, it manipulates a buffer that it does not own nor know the lifetime.
//! So accessing the underlying buffer is unsafe.
//!
//! Use Rust slices instead when possible.

use std::ptr;
use std::str::{FromStr, Utf8Error};

use crate::bindings::{pstream_t__bindgen_ty_1, pstream_t__bindgen_ty_2};
use crate::helpers::slice_from_nullable_raw_parts;
use crate::lstr::{self, AsRaw as _, lstr_t};

#[allow(clippy::module_name_repetitions)]
pub use crate::bindings::pstream_t;

impl pstream_t {
    /// Create an empty `pstream_t`.
    pub const fn new() -> Self {
        Self {
            __bindgen_anon_1: pstream_t__bindgen_ty_1 { b: ptr::null() },
            __bindgen_anon_2: pstream_t__bindgen_ty_2 { b_end: ptr::null() },
        }
    }

    /// Create a non-owned `pstream_t` from a Rust bytes slice.
    pub const fn from_bytes(bytes: &[u8]) -> Self {
        let ptr_range = bytes.as_ptr_range();

        Self {
            __bindgen_anon_1: pstream_t__bindgen_ty_1 { b: ptr_range.start },
            __bindgen_anon_2: pstream_t__bindgen_ty_2 {
                b_end: ptr_range.end,
            },
        }
    }

    /// Convert a `pstream_t` to a bytes slice.
    ///
    /// # Safety
    ///
    /// The `pstream_t` does not own the underlying buffer.
    pub const unsafe fn as_bytes(&self) -> &[u8] {
        // XXX: We can use `std::slice::from_ptr_range()` when it is stabilized.
        let ptr = unsafe { self.__bindgen_anon_1.b };
        let len = unsafe {
            self.__bindgen_anon_2
                .b_end
                .offset_from(self.__bindgen_anon_1.b)
        } as usize;

        unsafe { slice_from_nullable_raw_parts(ptr, len) }
    }

    /// Convert a `pstream_t` to a non-owned Rust str and check UTF-8 errors.
    ///
    /// # Safety
    ///
    /// The `pstream_t` does not own the underlying buffer.
    ///
    /// # Errors
    ///
    /// The `pstream_t` is not a valid UTF-8 string.
    pub const unsafe fn as_str(&self) -> Result<&str, Utf8Error> {
        unsafe { str::from_utf8(self.as_bytes()) }
    }

    /// Convert a `pstream_t` to a non-owned Rust str without checking for UTF-8 errors.
    ///
    /// # Safety
    ///
    /// The `pstream_t` does not own the underlying buffer.
    /// This method is unsafe because converting a slice to a string without checking UTF-8 errors
    /// is unsafe.
    pub const unsafe fn as_str_unchecked(&self) -> &str {
        unsafe { str::from_utf8_unchecked(self.as_bytes()) }
    }

    /// Convert it to a C pointer.
    pub fn as_ptr(&self) -> *const Self {
        self
    }

    /// Convert it to a mutable C pointer.
    pub fn as_mut_ptr(&mut self) -> *mut Self {
        self
    }
}

impl Default for pstream_t {
    fn default() -> Self {
        Self::new()
    }
}

impl From<&[u8]> for pstream_t {
    fn from(bytes: &[u8]) -> Self {
        pstream_t::from_bytes(bytes)
    }
}

impl From<&str> for pstream_t {
    fn from(str: &str) -> Self {
        pstream_t::from_bytes(str.as_bytes())
    }
}

impl From<lstr_t> for pstream_t {
    fn from(lstr: lstr_t) -> Self {
        pstream_t::from_bytes(unsafe { lstr.as_bytes() })
    }
}

impl From<pstream_t> for lstr_t {
    fn from(ps: pstream_t) -> Self {
        unsafe { lstr::from_bytes(ps.as_bytes()).as_raw() }
    }
}

impl FromStr for pstream_t {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, ()> {
        Ok(pstream_t::from(s))
    }
}
