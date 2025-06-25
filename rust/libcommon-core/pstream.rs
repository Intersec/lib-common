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

//! Conversion between a C pstream_t and Rust string/bytes types.

use std::convert::TryFrom;
use std::ops::Deref;
use std::slice::from_raw_parts;
use std::str::{FromStr, Utf8Error};

use crate::bindings::*;

pub use crate::bindings::pstream_t;

impl pstream_t {
    /// Create a non-owned pstream_t from a Rust bytes slice.
    pub const fn from_bytes(bytes: &[u8]) -> Self {
        let start_ptr: *const u8 = bytes.as_ptr();
        let end_ptr: *const u8 = bytes.last().unwrap();

        unsafe {
            Self {
                __bindgen_anon_1: pstream_t__bindgen_ty_1 { b: start_ptr },
                __bindgen_anon_2: pstream_t__bindgen_ty_2 {
                    b_end: end_ptr.offset(1),
                },
            }
        }
    }

    /// Create a non-owned pstream_t from a Rust string.
    #[allow(clippy::should_implement_trait)]
    pub const fn from_str(str: &str) -> Self {
        Self::from_bytes(str.as_bytes())
    }

    /// Convert a pstream_t to a bytes slice.
    pub const fn as_bytes(&self) -> &[u8] {
        let ptr = unsafe { self.__bindgen_anon_1.b };
        let len = unsafe {
            self.__bindgen_anon_2
                .b_end
                .offset_from(self.__bindgen_anon_1.b)
        } as usize;

        unsafe { from_raw_parts(ptr, len) }
    }

    /// Convert a pstream_t to a non-owned Rust str and check UTF-8 errors.
    pub const fn as_str(&self) -> Result<&str, Utf8Error> {
        str::from_utf8(self.as_bytes())
    }

    /// Convert a pstream_t to a non-owned Rust str without checking for UTF-8 errors.
    ///
    /// # Safety
    ///
    /// This method is unsafe because converting a slice to a string without checking UTF-8 errors
    /// is unsafe.
    pub const unsafe fn as_str_unchecked(&self) -> &str {
        unsafe { str::from_utf8_unchecked(self.as_bytes()) }
    }
}

impl From<&[u8]> for pstream_t {
    fn from(bytes: &[u8]) -> Self {
        pstream_t::from_bytes(bytes)
    }
}

impl From<&str> for pstream_t {
    fn from(str: &str) -> Self {
        pstream_t::from_str(str)
    }
}

impl From<lstr_t> for pstream_t {
    fn from(lstr: lstr_t) -> Self {
        pstream_t::from_bytes(&lstr)
    }
}

impl From<pstream_t> for lstr_t {
    fn from(ps: pstream_t) -> Self {
        lstr_t::from_bytes(&ps)
    }
}

impl<'a> TryFrom<&'a pstream_t> for &'a str {
    type Error = Utf8Error;

    fn try_from(ps: &'a pstream_t) -> Result<Self, Self::Error> {
        ps.as_str()
    }
}

impl FromStr for pstream_t {
    type Err = ();

    fn from_str(str: &str) -> Result<Self, ()> {
        Ok(pstream_t::from_str(str))
    }
}

impl Deref for pstream_t {
    type Target = [u8];

    #[inline]
    fn deref(&self) -> &Self::Target {
        self.as_bytes()
    }
}
