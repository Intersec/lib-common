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

//! Type-safe wrappers for C `lstr_t` with ownership and content type safety.
//!
//! This module provides 6 wrapper types for `lstr_t`:
//!
//! ## Ownership Levels
//!
//! * **Unsafe** (`UnsafeBytesLstr`, `UnsafeUtf8Lstr`): No lifetime tracking. Accessing the
//!   underlying buffer is unsafe. Used when receiving raw `lstr_t` from C code.
//!
//! * **Borrowed** (`BorrowedBytesLstr<'a>`, `BorrowedUtf8Lstr<'a>`): Lifetime-tracked borrowed
//!   `lstr_t`. Accessing the underlying buffer is safe. Used when converting Rust references
//!   to `lstr_t`.
//!
//! * **Owned** (`OwnedBytesLstr`, `OwnedUtf8Lstr`): Owns the underlying buffer which is freed
//!   when dropped. Accessing the underlying buffer is safe.
//!
//! ## Content Types
//!
//! * **Bytes**: Arbitrary byte sequences. Derefs to `&[u8]`.
//! * **Utf8**: Valid UTF-8 strings. Derefs to `&str`. UTF-8 validity is trusted (not validated).
//!
//! ## Conversion Rules
//!
//! * UTF-8 → Bytes: Always safe via `From`
//! * Bytes → UTF-8: Fallible via `TryFrom` (validates UTF-8)
//! * Higher ownership → Lower ownership: Via `From` (loses safety guarantees)

use std::convert::TryFrom;
use std::fmt;
use std::marker::PhantomData;
use std::mem::ManuallyDrop;
use std::ops::{Deref, Drop};
use std::os::raw::c_char;
use std::ptr;
use std::str::{self, Utf8Error};

use crate::bindings::{lstr_dup, lstr_equal, lstr_t, lstr_t__bindgen_ty_1, lstr_wipe, t_lstr_dup};
use crate::helpers::slice_from_nullable_raw_parts;
use crate::mem_stack::TScope;

// {{{ Internal helpers

/// Use libc `strlen()` to compute the length of C string.
///
/// # Safety
///
/// ptr must not be null and must point to a valid C string.
#[inline]
unsafe fn libc_strlen(ptr: *const c_char) -> u64 {
    unsafe extern "C" {
        /// Provided by libc or `compiler_builtins`.
        fn strlen(s: *const c_char) -> u64;
    }

    unsafe { strlen(ptr) }
}

/// Get the underlying buffer as a reference.
///
/// # Safety
///
/// The `lstr_t` may not own the underlying buffer.
#[inline]
const unsafe fn get_as_bytes<'a>(lstr: &lstr_t) -> &'a [u8] {
    let ptr = unsafe { lstr.__bindgen_anon_1.data as *const u8 };
    let len = lstr.len as usize;

    unsafe { slice_from_nullable_raw_parts(ptr, len) }
}

/// Create an `lstr_t` from raw parts.
#[inline]
const fn lstr_from_raw_parts(ptr: *const c_char, len: usize) -> lstr_t {
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr },
        len: len.cast_signed() as i32,
        mem_pool: 0,
    }
}

// }}}
// {{{ AsRawLstr trait

/// Trait to convert a value to a raw `lstr_t`.
///
/// This trait is implemented by all lstr wrapper types and allows generic code
/// to work with any lstr type when calling C functions.
#[allow(clippy::module_name_repetitions)]
pub trait AsRawLstr {
    /// Create a raw `lstr_t` from this value.
    ///
    /// Note: The lifetime of the underlying data is lost in the returned `lstr_t`.
    /// The caller must ensure the returned `lstr_t` does not outlive the source.
    /// Creating the raw `lstr_t` is safe (like creating a raw pointer), but using
    /// it to access data requires appropriate unsafe blocks.
    fn as_raw(&self) -> lstr_t;
}

// }}}
// {{{ Type definitions

/// Unsafe wrapper for a bytes `lstr_t` with no lifetime tracking.
///
/// This type is used when receiving raw `lstr_t` from C code where lifetime
/// cannot be statically tracked. All buffer access is unsafe.
#[allow(clippy::module_name_repetitions)]
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct UnsafeBytesLstr {
    lstr: lstr_t,
}

/// Unsafe wrapper for a UTF-8 `lstr_t` with no lifetime tracking.
///
/// This type is used when receiving raw `lstr_t` from C code where lifetime
/// cannot be statically tracked. UTF-8 validity is trusted (not validated).
/// All buffer access is unsafe.
#[allow(clippy::module_name_repetitions)]
#[repr(transparent)]
#[derive(Clone, Copy)]
pub struct UnsafeUtf8Lstr {
    lstr: lstr_t,
}

/// Borrowed bytes `lstr_t` with lifetime tracking.
///
/// This type provides safe access to the underlying buffer because the
/// lifetime ensures the data remains valid.
#[allow(clippy::module_name_repetitions)]
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct BorrowedBytesLstr<'a> {
    lstr: lstr_t,
    _phantom: PhantomData<&'a [u8]>,
}

/// Borrowed UTF-8 `lstr_t` with lifetime tracking.
///
/// This type provides safe access to the underlying string because the
/// lifetime ensures the data remains valid. UTF-8 validity is trusted.
#[allow(clippy::module_name_repetitions)]
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct BorrowedUtf8Lstr<'a> {
    lstr: lstr_t,
    _phantom: PhantomData<&'a str>,
}

/// Owned bytes `lstr_t` that manages its own memory.
///
/// The underlying buffer is freed when this value is dropped.
#[allow(clippy::module_name_repetitions)]
#[repr(transparent)]
pub struct OwnedBytesLstr {
    lstr: lstr_t,
}

/// Owned UTF-8 `lstr_t` that manages its own memory.
///
/// The underlying buffer is freed when this value is dropped.
/// UTF-8 validity is trusted.
#[allow(clippy::module_name_repetitions)]
#[repr(transparent)]
pub struct OwnedUtf8Lstr {
    lstr: lstr_t,
}

// }}}
// {{{ Common implementation macro

/// Macro to generate common methods for all lstr types.
macro_rules! lstr_common_impl {
    ($name:ident $(< $lt:lifetime >)?) => {
        impl $( < $lt > )? $name $( < $lt > )? {
            /// Get the length of the `lstr_t` in bytes.
            #[inline]
            pub const fn len(&self) -> usize {
                self.lstr.len as usize
            }

            /// Check if the `lstr_t` is empty (len == 0).
            #[inline]
            pub const fn is_empty(&self) -> bool {
                self.lstr.len == 0
            }

            /// Check if the `lstr_t` is null (!s).
            #[inline]
            pub const fn is_null(&self) -> bool {
                unsafe { self.lstr.__bindgen_anon_1.s.is_null() }
            }

            /// Check if two `lstr_t` are equal.
            #[inline]
            pub fn equals<T: AsRawLstr>(&self, other: &T) -> bool {
                unsafe { lstr_equal(self.lstr, other.as_raw()) }
            }
        }

        impl $( < $lt > )? AsRawLstr for $name $( < $lt > )? {
            #[inline]
            fn as_raw(&self) -> lstr_t {
                self.lstr
            }
        }
    };
}

/// Macro to generate const `into_raw` for non-owned types.
macro_rules! lstr_const_into_raw_impl {
    ($name:ident $(< $lt:lifetime >)?) => {
        impl $( < $lt > )? $name $( < $lt > )? {
            /// Get the raw `lstr_t` (const version).
            ///
            /// # Safety
            ///
            /// The lifetime of the underlying data is lost.
            #[inline]
            pub const unsafe fn into_raw(self) -> lstr_t {
                self.lstr
            }
        }
    };
}

/// Macro to generate non-const `into_raw` for owned types (must not run destructor).
macro_rules! lstr_owned_into_raw_impl {
    ($name:ident) => {
        impl $name {
            /// Consume and get the raw `lstr_t`.
            ///
            /// This consumes the wrapper without running the destructor.
            ///
            /// # Safety
            ///
            /// The caller takes ownership of the underlying buffer and is
            /// responsible for calling `lstr_wipe` on it.
            #[inline]
            pub unsafe fn into_raw(self) -> lstr_t {
                ManuallyDrop::new(self).lstr
            }
        }
    };
}

// }}}
// {{{ Unsafe bytes implementation macro

/// Macro to generate unsafe bytes access methods.
macro_rules! lstr_unsafe_bytes_impl {
    ($name:ident $(< $lt:lifetime >)?) => {
        impl $( < $lt > )? $name $( < $lt > )? {
            /// Convert to a bytes slice.
            ///
            /// # Safety
            ///
            /// The caller must ensure the underlying buffer is valid for the
            /// lifetime `'a`.
            #[inline]
            pub const unsafe fn as_bytes<'a>(&self) -> &'a [u8] {
                unsafe { get_as_bytes(&self.lstr) }
            }

            /// Convert to a string slice without UTF-8 validation.
            ///
            /// # Safety
            ///
            /// The caller must ensure the underlying buffer is valid for the
            /// lifetime `'a` and contains valid UTF-8 data.
            #[inline]
            pub const unsafe fn as_str_unchecked<'a>(&self) -> &'a str {
                unsafe { str::from_utf8_unchecked(self.as_bytes()) }
            }

            /// Duplicate on the given `TScope` as bytes.
            #[must_use]
            #[inline]
            pub fn t_dup_bytes<'t>(&self, _t_scope: &'t TScope) -> BorrowedBytesLstr<'t> {
                BorrowedBytesLstr {
                    lstr: unsafe { t_lstr_dup(self.lstr) },
                    _phantom: PhantomData,
                }
            }

            /// Duplicate and return an owned bytes lstr.
            #[must_use]
            #[inline]
            pub fn dup_bytes(&self) -> OwnedBytesLstr {
                OwnedBytesLstr {
                    lstr: unsafe { lstr_dup(self.lstr) },
                }
            }
        }

        impl $( < $lt > )? fmt::Debug for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:x?}", unsafe { self.as_bytes() })
            }
        }

        impl $( < $lt > )? fmt::Display for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:x?}", unsafe { self.as_bytes() })
            }
        }
    };
}

// }}}
// {{{ Unsafe UTF-8 implementation macro

/// Macro to generate unsafe UTF-8 access methods.
macro_rules! lstr_unsafe_utf8_impl {
    ($name:ident $(< $lt:lifetime >)?) => {
        impl $( < $lt > )? $name $( < $lt > )? {
            /// Convert to a bytes slice.
            ///
            /// # Safety
            ///
            /// The caller must ensure the underlying buffer is valid for the
            /// lifetime `'a`.
            #[inline]
            pub const unsafe fn as_bytes<'a>(&self) -> &'a [u8] {
                unsafe { get_as_bytes(&self.lstr) }
            }

            /// Convert to a string slice without UTF-8 validation.
            ///
            /// # Safety
            ///
            /// The caller must ensure the underlying buffer is valid for the
            /// lifetime `'a`. UTF-8 validity is trusted.
            #[inline]
            pub const unsafe fn as_str<'a>(&self) -> &'a str {
                unsafe { str::from_utf8_unchecked(self.as_bytes()) }
            }

            /// Duplicate on the given `TScope` as UTF-8.
            #[must_use]
            #[inline]
            pub fn t_dup_utf8<'t>(&self, _t_scope: &'t TScope) -> BorrowedUtf8Lstr<'t> {
                BorrowedUtf8Lstr {
                    lstr: unsafe { t_lstr_dup(self.lstr) },
                    _phantom: PhantomData,
                }
            }

            /// Duplicate and return an owned UTF-8 lstr.
            #[must_use]
            #[inline]
            pub fn dup_utf8(&self) -> OwnedUtf8Lstr {
                OwnedUtf8Lstr {
                    lstr: unsafe { lstr_dup(self.lstr) },
                }
            }

            /// Duplicate on the given `TScope` as bytes.
            #[must_use]
            #[inline]
            pub fn t_dup_bytes<'t>(&self, _t_scope: &'t TScope) -> BorrowedBytesLstr<'t> {
                BorrowedBytesLstr {
                    lstr: unsafe { t_lstr_dup(self.lstr) },
                    _phantom: PhantomData,
                }
            }

            /// Duplicate and return an owned bytes lstr.
            #[must_use]
            #[inline]
            pub fn dup_bytes(&self) -> OwnedBytesLstr {
                OwnedBytesLstr {
                    lstr: unsafe { lstr_dup(self.lstr) },
                }
            }
        }

        impl $( < $lt > )? fmt::Debug for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:?}", unsafe { self.as_str() })
            }
        }

        impl $( < $lt > )? fmt::Display for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(unsafe { self.as_str() })
            }
        }
    };
}

// }}}
// {{{ Safe bytes implementation macro

/// Macro to generate safe bytes access methods for borrowed/owned types.
macro_rules! lstr_safe_bytes_impl {
    ($name:ident $(< $lt:lifetime >)?) => {
        impl $( < $lt > )? $name $( < $lt > )? {
            /// Convert to a bytes slice.
            #[inline]
            pub const fn as_bytes(&self) -> &[u8] {
                unsafe { get_as_bytes(&self.lstr) }
            }

            /// Duplicate on the given `TScope`.
            #[must_use]
            #[inline]
            pub fn t_dup<'t>(&self, _t_scope: &'t TScope) -> BorrowedBytesLstr<'t> {
                BorrowedBytesLstr {
                    lstr: unsafe { t_lstr_dup(self.lstr) },
                    _phantom: PhantomData,
                }
            }

            /// Duplicate and return an owned lstr.
            #[must_use]
            #[inline]
            pub fn dup(&self) -> OwnedBytesLstr {
                OwnedBytesLstr {
                    lstr: unsafe { lstr_dup(self.lstr) },
                }
            }
        }

        impl $( < $lt > )? Deref for $name $( < $lt > )? {
            type Target = [u8];

            #[inline]
            fn deref(&self) -> &Self::Target {
                self.as_bytes()
            }
        }

        impl $( < $lt > )? AsRef<[u8]> for $name $( < $lt > )? {
            #[inline]
            fn as_ref(&self) -> &[u8] {
                self.as_bytes()
            }
        }

        impl $( < $lt > )? fmt::Debug for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:x?}", self.as_bytes())
            }
        }

        impl $( < $lt > )? fmt::Display for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:x?}", self.as_bytes())
            }
        }
    };
}

// }}}
// {{{ Safe UTF-8 implementation macro

/// Macro to generate safe UTF-8 access methods for borrowed/owned types.
macro_rules! lstr_safe_utf8_impl {
    ($name:ident $(< $lt:lifetime >)?) => {
        impl $( < $lt > )? $name $( < $lt > )? {
            /// Convert to a string slice. UTF-8 validity is trusted.
            #[inline]
            pub const fn as_str(&self) -> &str {
                unsafe { str::from_utf8_unchecked(get_as_bytes(&self.lstr)) }
            }

            /// Convert to a bytes slice.
            #[inline]
            pub const fn as_bytes(&self) -> &[u8] {
                unsafe { get_as_bytes(&self.lstr) }
            }

            /// Duplicate on the given `TScope`.
            #[must_use]
            #[inline]
            pub fn t_dup<'t>(&self, _t_scope: &'t TScope) -> BorrowedUtf8Lstr<'t> {
                BorrowedUtf8Lstr {
                    lstr: unsafe { t_lstr_dup(self.lstr) },
                    _phantom: PhantomData,
                }
            }

            /// Duplicate and return an owned lstr.
            #[must_use]
            #[inline]
            pub fn dup(&self) -> OwnedUtf8Lstr {
                OwnedUtf8Lstr {
                    lstr: unsafe { lstr_dup(self.lstr) },
                }
            }
        }

        impl $( < $lt > )? Deref for $name $( < $lt > )? {
            type Target = str;

            #[inline]
            fn deref(&self) -> &Self::Target {
                self.as_str()
            }
        }

        impl $( < $lt > )? AsRef<str> for $name $( < $lt > )? {
            #[inline]
            fn as_ref(&self) -> &str {
                self.as_str()
            }
        }

        impl $( < $lt > )? AsRef<[u8]> for $name $( < $lt > )? {
            #[inline]
            fn as_ref(&self) -> &[u8] {
                self.as_bytes()
            }
        }

        impl $( < $lt > )? fmt::Debug for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{:?}", self.as_str())
            }
        }

        impl $( < $lt > )? fmt::Display for $name $( < $lt > )? {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(self.as_str())
            }
        }
    };
}

// }}}
// {{{ Raw lstr_t implementation (internal use only)

/// Implementations for raw `lstr_t` - for internal crate use when working with C FFI.
impl lstr_t {
    /// Get the length of the `lstr_t` in bytes.
    #[inline]
    pub const fn len(&self) -> usize {
        self.len as usize
    }

    /// Check if the `lstr_t` is empty (len == 0).
    #[inline]
    pub const fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Check if the `lstr_t` is null (!s).
    #[inline]
    pub const fn is_null(&self) -> bool {
        unsafe { self.__bindgen_anon_1.s.is_null() }
    }
}

impl AsRawLstr for lstr_t {
    #[inline]
    fn as_raw(&self) -> lstr_t {
        *self
    }
}

// }}}
// {{{ Apply macros to types

// UnsafeBytesLstr
lstr_common_impl!(UnsafeBytesLstr);
lstr_const_into_raw_impl!(UnsafeBytesLstr);
lstr_unsafe_bytes_impl!(UnsafeBytesLstr);

// UnsafeUtf8Lstr
lstr_common_impl!(UnsafeUtf8Lstr);
lstr_const_into_raw_impl!(UnsafeUtf8Lstr);
lstr_unsafe_utf8_impl!(UnsafeUtf8Lstr);

// BorrowedBytesLstr
lstr_common_impl!(BorrowedBytesLstr<'a>);
lstr_const_into_raw_impl!(BorrowedBytesLstr<'a>);
lstr_safe_bytes_impl!(BorrowedBytesLstr<'a>);

// BorrowedUtf8Lstr
lstr_common_impl!(BorrowedUtf8Lstr<'a>);
lstr_const_into_raw_impl!(BorrowedUtf8Lstr<'a>);
lstr_safe_utf8_impl!(BorrowedUtf8Lstr<'a>);

// OwnedBytesLstr
lstr_common_impl!(OwnedBytesLstr);
lstr_owned_into_raw_impl!(OwnedBytesLstr);
lstr_safe_bytes_impl!(OwnedBytesLstr);

// OwnedUtf8Lstr
lstr_common_impl!(OwnedUtf8Lstr);
lstr_owned_into_raw_impl!(OwnedUtf8Lstr);
lstr_safe_utf8_impl!(OwnedUtf8Lstr);

// }}}
// {{{ Drop implementations for owned types

impl Drop for OwnedBytesLstr {
    fn drop(&mut self) {
        unsafe {
            lstr_wipe(&raw mut self.lstr);
        }
    }
}

impl Drop for OwnedUtf8Lstr {
    fn drop(&mut self) {
        unsafe {
            lstr_wipe(&raw mut self.lstr);
        }
    }
}

// }}}
// {{{ Clone implementations for owned types

impl Clone for OwnedBytesLstr {
    fn clone(&self) -> Self {
        self.dup()
    }
}

impl Clone for OwnedUtf8Lstr {
    fn clone(&self) -> Self {
        self.dup()
    }
}

// }}}
// {{{ From conversions: Ownership downgrades

// BorrowedBytesLstr -> UnsafeBytesLstr
impl<'a> From<BorrowedBytesLstr<'a>> for UnsafeBytesLstr {
    fn from(borrowed: BorrowedBytesLstr<'a>) -> Self {
        UnsafeBytesLstr {
            lstr: borrowed.lstr,
        }
    }
}

// BorrowedUtf8Lstr -> UnsafeUtf8Lstr
impl<'a> From<BorrowedUtf8Lstr<'a>> for UnsafeUtf8Lstr {
    fn from(borrowed: BorrowedUtf8Lstr<'a>) -> Self {
        UnsafeUtf8Lstr {
            lstr: borrowed.lstr,
        }
    }
}

// &OwnedBytesLstr -> UnsafeBytesLstr
impl From<&OwnedBytesLstr> for UnsafeBytesLstr {
    fn from(owned: &OwnedBytesLstr) -> Self {
        UnsafeBytesLstr { lstr: owned.lstr }
    }
}

// &OwnedUtf8Lstr -> UnsafeUtf8Lstr
impl From<&OwnedUtf8Lstr> for UnsafeUtf8Lstr {
    fn from(owned: &OwnedUtf8Lstr) -> Self {
        UnsafeUtf8Lstr { lstr: owned.lstr }
    }
}

// &OwnedBytesLstr -> BorrowedBytesLstr
impl<'a> From<&'a OwnedBytesLstr> for BorrowedBytesLstr<'a> {
    fn from(owned: &'a OwnedBytesLstr) -> Self {
        BorrowedBytesLstr {
            lstr: owned.lstr,
            _phantom: PhantomData,
        }
    }
}

// &OwnedUtf8Lstr -> BorrowedUtf8Lstr
impl<'a> From<&'a OwnedUtf8Lstr> for BorrowedUtf8Lstr<'a> {
    fn from(owned: &'a OwnedUtf8Lstr) -> Self {
        BorrowedUtf8Lstr {
            lstr: owned.lstr,
            _phantom: PhantomData,
        }
    }
}

// }}}
// {{{ From conversions: UTF-8 -> Bytes (infallible)

// UnsafeUtf8Lstr -> UnsafeBytesLstr
impl From<UnsafeUtf8Lstr> for UnsafeBytesLstr {
    fn from(utf8: UnsafeUtf8Lstr) -> Self {
        UnsafeBytesLstr { lstr: utf8.lstr }
    }
}

// BorrowedUtf8Lstr -> BorrowedBytesLstr
impl<'a> From<BorrowedUtf8Lstr<'a>> for BorrowedBytesLstr<'a> {
    fn from(utf8: BorrowedUtf8Lstr<'a>) -> Self {
        BorrowedBytesLstr {
            lstr: utf8.lstr,
            _phantom: PhantomData,
        }
    }
}

// OwnedUtf8Lstr -> OwnedBytesLstr
impl From<OwnedUtf8Lstr> for OwnedBytesLstr {
    fn from(utf8: OwnedUtf8Lstr) -> Self {
        OwnedBytesLstr {
            lstr: ManuallyDrop::new(utf8).lstr,
        }
    }
}

// }}}
// {{{ TryFrom conversions: Bytes -> UTF-8 (fallible)

// UnsafeBytesLstr -> UnsafeUtf8Lstr
impl TryFrom<UnsafeBytesLstr> for UnsafeUtf8Lstr {
    type Error = Utf8Error;

    fn try_from(bytes: UnsafeBytesLstr) -> Result<Self, Self::Error> {
        // Validate UTF-8 (unsafe access required)
        str::from_utf8(unsafe { bytes.as_bytes() })?;
        Ok(UnsafeUtf8Lstr { lstr: bytes.lstr })
    }
}

// BorrowedBytesLstr -> BorrowedUtf8Lstr
impl<'a> TryFrom<BorrowedBytesLstr<'a>> for BorrowedUtf8Lstr<'a> {
    type Error = Utf8Error;

    fn try_from(bytes: BorrowedBytesLstr<'a>) -> Result<Self, Self::Error> {
        // Validate UTF-8
        str::from_utf8(bytes.as_bytes())?;
        Ok(BorrowedUtf8Lstr {
            lstr: bytes.lstr,
            _phantom: PhantomData,
        })
    }
}

// OwnedBytesLstr -> OwnedUtf8Lstr
impl TryFrom<OwnedBytesLstr> for OwnedUtf8Lstr {
    type Error = Utf8Error;

    fn try_from(bytes: OwnedBytesLstr) -> Result<Self, Self::Error> {
        // Validate UTF-8
        str::from_utf8(bytes.as_bytes())?;
        Ok(OwnedUtf8Lstr {
            lstr: ManuallyDrop::new(bytes).lstr,
        })
    }
}

// }}}
// {{{ From conversions: Rust types -> Borrowed lstr types

// &[u8] -> BorrowedBytesLstr
impl<'a> From<&'a [u8]> for BorrowedBytesLstr<'a> {
    fn from(bytes: &'a [u8]) -> BorrowedBytesLstr<'a> {
        BorrowedBytesLstr {
            lstr: lstr_from_raw_parts(bytes.as_ptr().cast::<c_char>(), bytes.len()),
            _phantom: PhantomData,
        }
    }
}

// &str -> BorrowedUtf8Lstr
impl<'a> From<&'a str> for BorrowedUtf8Lstr<'a> {
    fn from(s: &'a str) -> BorrowedUtf8Lstr<'a> {
        BorrowedUtf8Lstr {
            lstr: lstr_from_raw_parts(s.as_ptr().cast::<c_char>(), s.len()),
            _phantom: PhantomData,
        }
    }
}

// }}}
// {{{ Constructor functions

/// Create a non-owned `BorrowedBytesLstr` from a Rust bytes slice.
pub const fn from_bytes(bytes: &[u8]) -> BorrowedBytesLstr<'_> {
    BorrowedBytesLstr {
        lstr: lstr_from_raw_parts(bytes.as_ptr().cast::<c_char>(), bytes.len()),
        _phantom: PhantomData,
    }
}

/// Create a non-owned `BorrowedUtf8Lstr` from a Rust `str`.
pub const fn from_str(s: &str) -> BorrowedUtf8Lstr<'_> {
    BorrowedUtf8Lstr {
        lstr: lstr_from_raw_parts(s.as_ptr().cast::<c_char>(), s.len()),
        _phantom: PhantomData,
    }
}

/// Create an `UnsafeBytesLstr` from a raw `lstr_t`.
///
/// # Safety
///
/// The caller must ensure the `lstr_t` contains valid data.
pub const unsafe fn from_raw_bytes(lstr: lstr_t) -> UnsafeBytesLstr {
    UnsafeBytesLstr { lstr }
}

/// Create an `UnsafeUtf8Lstr` from a raw `lstr_t`.
///
/// # Safety
///
/// The caller must ensure the `lstr_t` contains valid UTF-8 data.
pub const unsafe fn from_raw_utf8(lstr: lstr_t) -> UnsafeUtf8Lstr {
    UnsafeUtf8Lstr { lstr }
}

/// Create a `BorrowedBytesLstr` from a C raw pointer and length.
///
/// # Safety
///
/// The caller must ensure the pointer and length are valid for lifetime `'a`.
pub const unsafe fn from_ptr_and_len<'a>(ptr: *const c_char, len: usize) -> BorrowedBytesLstr<'a> {
    BorrowedBytesLstr {
        lstr: lstr_from_raw_parts(ptr, len),
        _phantom: PhantomData,
    }
}

/// Create a `BorrowedUtf8Lstr` from a C raw pointer and length.
///
/// # Safety
///
/// The caller must ensure the pointer and length are valid for lifetime `'a`,
/// and that the data is valid UTF-8.
pub const unsafe fn from_str_ptr_and_len<'a>(
    ptr: *const c_char,
    len: usize,
) -> BorrowedUtf8Lstr<'a> {
    BorrowedUtf8Lstr {
        lstr: lstr_from_raw_parts(ptr, len),
        _phantom: PhantomData,
    }
}

/// Create a non-owned `UnsafeBytesLstr` from a C raw pointer.
///
/// # Safety
///
/// `ptr` shall not be `null` and should point to a valid C string.
pub unsafe fn from_ptr(ptr: *const c_char) -> UnsafeBytesLstr {
    let len = unsafe { libc_strlen(ptr) };
    UnsafeBytesLstr {
        lstr: lstr_from_raw_parts(ptr, len as usize),
    }
}

/// Create a null `UnsafeBytesLstr`.
pub const fn null_bytes() -> UnsafeBytesLstr {
    UnsafeBytesLstr {
        lstr: lstr_t {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr::null() },
            len: 0,
            mem_pool: 0,
        },
    }
}

/// Create an empty `UnsafeBytesLstr`.
pub const fn empty_bytes() -> UnsafeBytesLstr {
    UnsafeBytesLstr {
        lstr: lstr_t {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: c"".as_ptr() },
            len: 0,
            mem_pool: 0,
        },
    }
}

/// Create a null `UnsafeUtf8Lstr`.
pub const fn null_utf8() -> UnsafeUtf8Lstr {
    UnsafeUtf8Lstr { lstr: null_raw() }
}

/// Create an empty `UnsafeUtf8Lstr`.
pub const fn empty_utf8() -> UnsafeUtf8Lstr {
    UnsafeUtf8Lstr { lstr: empty_raw() }
}

/// Create a raw `lstr_t` from a static UTF-8 string.
///
/// This is a convenience function for passing static strings to C functions.
/// The returned `lstr_t` is valid for the lifetime of the static string.
pub const fn raw(s: &'static str) -> lstr_t {
    unsafe { from_str(s).into_raw() }
}

/// Create a null `lstr_t`.
pub const fn null_raw() -> lstr_t {
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr::null() },
        len: 0,
        mem_pool: 0,
    }
}

/// Create an empty `lstr_t`.
pub const fn empty_raw() -> lstr_t {
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: c"".as_ptr() },
        len: 0,
        mem_pool: 0,
    }
}

// }}}
// {{{ Tests

#[cfg(test)]
#[allow(clippy::redundant_test_prefix)]
mod tests {
    use std::convert::TryFrom as _;

    use super::*;
    use crate::mem_stack::TScope;

    // {{{ Test data

    const TEST_STR: &str = "hello world";
    const TEST_BYTES: &[u8] = b"hello world";
    const INVALID_UTF8: &[u8] = &[0xff, 0xfe, 0x00, 0x01];

    // }}}
    // {{{ UnsafeBytesLstr tests

    #[test]
    fn test_unsafe_bytes_from_raw() {
        let borrowed = from_bytes(TEST_BYTES);
        let raw_lstr = borrowed.as_raw();
        let unsafe_bytes = unsafe { from_raw_bytes(raw_lstr) };

        assert_eq!(unsafe_bytes.len(), TEST_BYTES.len());
        assert!(!unsafe_bytes.is_empty());
        assert!(!unsafe_bytes.is_null());
        assert_eq!(unsafe { unsafe_bytes.as_bytes() }, TEST_BYTES);
    }

    #[test]
    fn test_unsafe_bytes_null() {
        let null = null_bytes();

        assert!(null.is_null());
        assert!(null.is_empty());
        assert_eq!(null.len(), 0);
    }

    #[test]
    fn test_unsafe_bytes_empty() {
        let empty = empty_bytes();

        assert!(!empty.is_null());
        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
    }

    #[test]
    fn test_unsafe_bytes_as_str_unchecked() {
        let borrowed = from_bytes(TEST_BYTES);
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        assert_eq!(unsafe { unsafe_bytes.as_str_unchecked() }, TEST_STR);
    }

    #[test]
    fn test_unsafe_bytes_equals() {
        let a = from_bytes(TEST_BYTES);
        let b = from_bytes(TEST_BYTES);
        let c = from_bytes(b"different");

        assert!(a.equals(&b));
        assert!(!a.equals(&c));
    }

    #[test]
    fn test_unsafe_bytes_dup() {
        let borrowed = from_bytes(TEST_BYTES);
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        let owned = unsafe_bytes.dup_bytes();
        assert_eq!(owned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_unsafe_bytes_t_dup() {
        let t_scope = TScope::new_scope();
        let borrowed = from_bytes(TEST_BYTES);
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        let t_borrowed = unsafe_bytes.t_dup_bytes(&t_scope);
        assert_eq!(t_borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_unsafe_bytes_display() {
        let borrowed = from_bytes(b"abc");
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        let display = format!("{unsafe_bytes}");
        assert!(display.contains("61")); // 'a' = 0x61
    }

    #[test]
    fn test_unsafe_bytes_into_raw() {
        let borrowed = from_bytes(TEST_BYTES);
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        let raw_lstr = unsafe { unsafe_bytes.into_raw() };
        assert_eq!(raw_lstr.len(), TEST_BYTES.len());
    }

    #[test]
    fn test_unsafe_bytes_copy() {
        let borrowed = from_bytes(TEST_BYTES);
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        let copy = unsafe_bytes;
        assert_eq!(unsafe { copy.as_bytes() }, TEST_BYTES);
        // Original still valid (Copy trait)
        assert_eq!(unsafe { unsafe_bytes.as_bytes() }, TEST_BYTES);
    }

    // }}}
    // {{{ UnsafeUtf8Lstr tests

    #[test]
    fn test_unsafe_utf8_from_raw() {
        let borrowed = from_str(TEST_STR);
        let raw_lstr = borrowed.as_raw();
        let unsafe_utf8 = unsafe { from_raw_utf8(raw_lstr) };

        assert_eq!(unsafe_utf8.len(), TEST_STR.len());
        assert!(!unsafe_utf8.is_empty());
        assert!(!unsafe_utf8.is_null());
        assert_eq!(unsafe { unsafe_utf8.as_str() }, TEST_STR);
    }

    #[test]
    fn test_unsafe_utf8_null() {
        let null = null_utf8();

        assert!(null.is_null());
        assert!(null.is_empty());
        assert_eq!(null.len(), 0);
    }

    #[test]
    fn test_unsafe_utf8_empty() {
        let empty = empty_utf8();

        assert!(!empty.is_null());
        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
    }

    #[test]
    fn test_unsafe_utf8_as_bytes() {
        let borrowed = from_str(TEST_STR);
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        assert_eq!(unsafe { unsafe_utf8.as_bytes() }, TEST_BYTES);
    }

    #[test]
    fn test_unsafe_utf8_dup() {
        let borrowed = from_str(TEST_STR);
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        let owned = unsafe_utf8.dup_utf8();
        assert_eq!(owned.as_str(), TEST_STR);
    }

    #[test]
    fn test_unsafe_utf8_dup_bytes() {
        let borrowed = from_str(TEST_STR);
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        let owned = unsafe_utf8.dup_bytes();
        assert_eq!(owned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_unsafe_utf8_t_dup() {
        let t_scope = TScope::new_scope();
        let borrowed = from_str(TEST_STR);
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        let t_borrowed = unsafe_utf8.t_dup_utf8(&t_scope);
        assert_eq!(t_borrowed.as_str(), TEST_STR);
    }

    #[test]
    fn test_unsafe_utf8_display() {
        let borrowed = from_str("abc");
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        let display = format!("{unsafe_utf8}");
        assert_eq!(display, "abc");
    }

    #[test]
    fn test_unsafe_utf8_debug() {
        let borrowed = from_str("abc");
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        let debug = format!("{unsafe_utf8:?}");
        assert!(debug.contains("abc"));
    }

    #[test]
    fn test_unsafe_utf8_copy() {
        let borrowed = from_str(TEST_STR);
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        let copy = unsafe_utf8;
        assert_eq!(unsafe { copy.as_str() }, TEST_STR);
        // Original still valid (Copy trait)
        assert_eq!(unsafe { unsafe_utf8.as_str() }, TEST_STR);
    }

    // }}}
    // {{{ BorrowedBytesLstr tests

    #[test]
    fn test_borrowed_bytes_from_bytes() {
        let borrowed = from_bytes(TEST_BYTES);

        assert_eq!(borrowed.len(), TEST_BYTES.len());
        assert!(!borrowed.is_empty());
        assert!(!borrowed.is_null());
        assert_eq!(borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_from_trait() {
        let borrowed: BorrowedBytesLstr<'_> = TEST_BYTES.into();

        assert_eq!(borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_deref() {
        let borrowed = from_bytes(TEST_BYTES);

        // Deref to &[u8]
        let slice: &[u8] = &borrowed;
        assert_eq!(slice, TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_as_ref() {
        let borrowed = from_bytes(TEST_BYTES);

        let slice: &[u8] = borrowed.as_ref();
        assert_eq!(slice, TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_dup() {
        let borrowed = from_bytes(TEST_BYTES);

        let owned = borrowed.dup();
        assert_eq!(owned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_t_dup() {
        let t_scope = TScope::new_scope();
        let borrowed = from_bytes(TEST_BYTES);

        let t_borrowed = borrowed.t_dup(&t_scope);
        assert_eq!(t_borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_copy() {
        let borrowed = from_bytes(TEST_BYTES);
        let copy = borrowed;

        // Both are still valid (Copy trait)
        assert_eq!(borrowed.as_bytes(), TEST_BYTES);
        assert_eq!(copy.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_clone() {
        let borrowed = from_bytes(TEST_BYTES);
        #[allow(clippy::clone_on_copy)]
        let cloned = borrowed.clone();

        assert_eq!(cloned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_display() {
        let borrowed = from_bytes(b"abc");

        let display = format!("{borrowed}");
        assert!(display.contains("61")); // 'a' = 0x61
    }

    #[test]
    fn test_borrowed_bytes_to_unsafe() {
        let borrowed = from_bytes(TEST_BYTES);
        let unsafe_bytes: UnsafeBytesLstr = borrowed.into();

        assert_eq!(unsafe { unsafe_bytes.as_bytes() }, TEST_BYTES);
    }

    #[test]
    fn test_borrowed_bytes_try_into_utf8_valid() {
        let borrowed = from_bytes(TEST_BYTES);
        let utf8 = BorrowedUtf8Lstr::try_from(borrowed).expect("valid UTF-8");

        assert_eq!(utf8.as_str(), TEST_STR);
    }

    #[test]
    fn test_borrowed_bytes_try_into_utf8_invalid() {
        let borrowed = from_bytes(INVALID_UTF8);
        let utf8: Result<BorrowedUtf8Lstr<'_>, _> = BorrowedUtf8Lstr::try_from(borrowed);

        utf8.expect_err("invalid UTF-8 should fail");
    }

    // }}}
    // {{{ BorrowedUtf8Lstr tests

    #[test]
    fn test_borrowed_utf8_from_str() {
        let borrowed = from_str(TEST_STR);

        assert_eq!(borrowed.len(), TEST_STR.len());
        assert!(!borrowed.is_empty());
        assert!(!borrowed.is_null());
        assert_eq!(borrowed.as_str(), TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_from_trait() {
        let borrowed: BorrowedUtf8Lstr<'_> = TEST_STR.into();

        assert_eq!(borrowed.as_str(), TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_as_bytes() {
        let borrowed = from_str(TEST_STR);

        assert_eq!(borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_borrowed_utf8_deref() {
        let borrowed = from_str(TEST_STR);

        // Deref to &str
        let s: &str = &borrowed;
        assert_eq!(s, TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_as_ref_str() {
        let borrowed = from_str(TEST_STR);

        let s: &str = borrowed.as_ref();
        assert_eq!(s, TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_as_ref_bytes() {
        let borrowed = from_str(TEST_STR);

        let bytes: &[u8] = borrowed.as_ref();
        assert_eq!(bytes, TEST_BYTES);
    }

    #[test]
    fn test_borrowed_utf8_dup() {
        let borrowed = from_str(TEST_STR);

        let owned = borrowed.dup();
        assert_eq!(owned.as_str(), TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_t_dup() {
        let t_scope = TScope::new_scope();
        let borrowed = from_str(TEST_STR);

        let t_borrowed = borrowed.t_dup(&t_scope);
        assert_eq!(t_borrowed.as_str(), TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_copy() {
        let borrowed = from_str(TEST_STR);
        let copy = borrowed;

        // Both are still valid (Copy trait)
        assert_eq!(borrowed.as_str(), TEST_STR);
        assert_eq!(copy.as_str(), TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_display() {
        let borrowed = from_str("abc");

        let display = format!("{borrowed}");
        assert_eq!(display, "abc");
    }

    #[test]
    fn test_borrowed_utf8_debug() {
        let borrowed = from_str("abc");

        let debug = format!("{borrowed:?}");
        assert!(debug.contains("abc"));
    }

    #[test]
    fn test_borrowed_utf8_to_unsafe() {
        let borrowed = from_str(TEST_STR);
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        assert_eq!(unsafe { unsafe_utf8.as_str() }, TEST_STR);
    }

    #[test]
    fn test_borrowed_utf8_to_bytes() {
        let borrowed = from_str(TEST_STR);
        let bytes: BorrowedBytesLstr<'_> = borrowed.into();

        assert_eq!(bytes.as_bytes(), TEST_BYTES);
    }

    // }}}
    // {{{ OwnedBytesLstr tests

    #[test]
    fn test_owned_bytes_from_dup() {
        let borrowed = from_bytes(TEST_BYTES);
        let owned = borrowed.dup();

        assert_eq!(owned.len(), TEST_BYTES.len());
        assert!(!owned.is_empty());
        assert!(!owned.is_null());
        assert_eq!(owned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_deref() {
        let owned = from_bytes(TEST_BYTES).dup();

        let slice: &[u8] = &owned;
        assert_eq!(slice, TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_as_ref() {
        let owned = from_bytes(TEST_BYTES).dup();

        let slice: &[u8] = owned.as_ref();
        assert_eq!(slice, TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_dup() {
        let owned = from_bytes(TEST_BYTES).dup();
        let owned2 = owned.dup();

        assert_eq!(owned.as_bytes(), TEST_BYTES);
        assert_eq!(owned2.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_clone() {
        let owned = from_bytes(TEST_BYTES).dup();
        let cloned = owned.clone();

        assert_eq!(owned.as_bytes(), TEST_BYTES);
        assert_eq!(cloned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_t_dup() {
        let t_scope = TScope::new_scope();
        let owned = from_bytes(TEST_BYTES).dup();

        let t_borrowed = owned.t_dup(&t_scope);
        assert_eq!(t_borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_to_unsafe() {
        let owned = from_bytes(TEST_BYTES).dup();
        let unsafe_bytes: UnsafeBytesLstr = (&owned).into();

        assert_eq!(unsafe { unsafe_bytes.as_bytes() }, TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_to_borrowed() {
        let owned = from_bytes(TEST_BYTES).dup();
        let borrowed: BorrowedBytesLstr<'_> = (&owned).into();

        assert_eq!(borrowed.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_bytes_try_into_utf8_valid() {
        let owned = from_bytes(TEST_BYTES).dup();
        let utf8 = OwnedUtf8Lstr::try_from(owned).expect("valid UTF-8");

        assert_eq!(utf8.as_str(), TEST_STR);
    }

    #[test]
    fn test_owned_bytes_try_into_utf8_invalid() {
        let owned = from_bytes(INVALID_UTF8).dup();
        let utf8: Result<OwnedUtf8Lstr, _> = OwnedUtf8Lstr::try_from(owned);

        utf8.expect_err("invalid UTF-8 should fail");
    }

    #[test]
    fn test_owned_bytes_into_raw() {
        let owned = from_bytes(TEST_BYTES).dup();
        let raw_lstr = unsafe { owned.into_raw() };

        // The raw lstr should have the same content
        assert_eq!(raw_lstr.len(), TEST_BYTES.len());

        // Clean up manually since we took ownership
        unsafe {
            let mut raw_lstr = raw_lstr;
            lstr_wipe(&raw mut raw_lstr);
        }
    }

    // }}}
    // {{{ OwnedUtf8Lstr tests

    #[test]
    fn test_owned_utf8_from_dup() {
        let borrowed = from_str(TEST_STR);
        let owned = borrowed.dup();

        assert_eq!(owned.len(), TEST_STR.len());
        assert!(!owned.is_empty());
        assert!(!owned.is_null());
        assert_eq!(owned.as_str(), TEST_STR);
    }

    #[test]
    fn test_owned_utf8_as_bytes() {
        let owned = from_str(TEST_STR).dup();

        assert_eq!(owned.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_utf8_deref() {
        let owned = from_str(TEST_STR).dup();

        let s: &str = &owned;
        assert_eq!(s, TEST_STR);
    }

    #[test]
    fn test_owned_utf8_as_ref_str() {
        let owned = from_str(TEST_STR).dup();

        let s: &str = owned.as_ref();
        assert_eq!(s, TEST_STR);
    }

    #[test]
    fn test_owned_utf8_as_ref_bytes() {
        let owned = from_str(TEST_STR).dup();

        let bytes: &[u8] = owned.as_ref();
        assert_eq!(bytes, TEST_BYTES);
    }

    #[test]
    fn test_owned_utf8_dup() {
        let owned = from_str(TEST_STR).dup();
        let owned2 = owned.dup();

        assert_eq!(owned.as_str(), TEST_STR);
        assert_eq!(owned2.as_str(), TEST_STR);
    }

    #[test]
    fn test_owned_utf8_clone() {
        let owned = from_str(TEST_STR).dup();
        let cloned = owned.clone();

        assert_eq!(owned.as_str(), TEST_STR);
        assert_eq!(cloned.as_str(), TEST_STR);
    }

    #[test]
    fn test_owned_utf8_t_dup() {
        let t_scope = TScope::new_scope();
        let owned = from_str(TEST_STR).dup();

        let t_borrowed = owned.t_dup(&t_scope);
        assert_eq!(t_borrowed.as_str(), TEST_STR);
    }

    #[test]
    fn test_owned_utf8_to_unsafe() {
        let owned = from_str(TEST_STR).dup();
        let unsafe_utf8: UnsafeUtf8Lstr = (&owned).into();

        assert_eq!(unsafe { unsafe_utf8.as_str() }, TEST_STR);
    }

    #[test]
    fn test_owned_utf8_to_borrowed() {
        let owned = from_str(TEST_STR).dup();
        let borrowed: BorrowedUtf8Lstr<'_> = (&owned).into();

        assert_eq!(borrowed.as_str(), TEST_STR);
    }

    #[test]
    fn test_owned_utf8_to_bytes() {
        let owned = from_str(TEST_STR).dup();
        let bytes: OwnedBytesLstr = owned.into();

        assert_eq!(bytes.as_bytes(), TEST_BYTES);
    }

    #[test]
    fn test_owned_utf8_into_raw() {
        let owned = from_str(TEST_STR).dup();
        let raw_lstr = unsafe { owned.into_raw() };

        // The raw lstr should have the same content
        assert_eq!(raw_lstr.len(), TEST_STR.len());

        // Clean up manually since we took ownership
        unsafe {
            let mut raw_lstr = raw_lstr;
            lstr_wipe(&raw mut raw_lstr);
        }
    }

    // }}}
    // {{{ Constructor function tests

    #[test]
    fn test_from_ptr_and_len() {
        let data = b"test data";
        let borrowed = unsafe { from_ptr_and_len(data.as_ptr().cast(), data.len()) };

        assert_eq!(borrowed.as_bytes(), data);
    }

    #[test]
    fn test_from_str_ptr_and_len() {
        let data = "test data";
        let borrowed = unsafe { from_str_ptr_and_len(data.as_ptr().cast(), data.len()) };

        assert_eq!(borrowed.as_str(), data);
    }

    #[test]
    fn test_from_ptr() {
        let cstr = c"test";
        let unsafe_bytes = unsafe { from_ptr(cstr.as_ptr()) };

        assert_eq!(unsafe { unsafe_bytes.as_bytes() }, b"test");
    }

    #[test]
    fn test_raw_function() {
        let raw_lstr = raw("static string");

        assert_eq!(raw_lstr.len(), 13);
        assert!(!raw_lstr.is_null());
    }

    #[test]
    fn test_null_raw() {
        let raw_lstr = null_raw();

        assert!(raw_lstr.is_null());
        assert!(raw_lstr.is_empty());
        assert_eq!(raw_lstr.len(), 0);
    }

    #[test]
    fn test_empty_raw() {
        let raw_lstr = empty_raw();

        assert!(!raw_lstr.is_null());
        assert!(raw_lstr.is_empty());
        assert_eq!(raw_lstr.len(), 0);
    }

    // }}}
    // {{{ Equality tests

    #[test]
    fn test_equals_same_content() {
        let a = from_bytes(TEST_BYTES);
        let b = from_bytes(TEST_BYTES);

        assert!(a.equals(&b));
        assert!(b.equals(&a));
    }

    #[test]
    fn test_equals_different_content() {
        let a = from_bytes(TEST_BYTES);
        let b = from_bytes(b"different");

        assert!(!a.equals(&b));
        assert!(!b.equals(&a));
    }

    #[test]
    fn test_equals_different_types() {
        let bytes = from_bytes(TEST_BYTES);
        let utf8 = from_str(TEST_STR);

        assert!(bytes.equals(&utf8));
        assert!(utf8.equals(&bytes));
    }

    #[test]
    fn test_equals_owned_borrowed() {
        let borrowed = from_bytes(TEST_BYTES);
        let owned = borrowed.dup();

        assert!(borrowed.equals(&owned));
        assert!(owned.equals(&borrowed));
    }

    // }}}
    // {{{ Conversion chain tests

    #[test]
    fn test_conversion_chain_utf8_to_bytes_to_unsafe() {
        let utf8 = from_str(TEST_STR);
        let bytes: BorrowedBytesLstr<'_> = utf8.into();
        let unsafe_bytes: UnsafeBytesLstr = bytes.into();

        assert_eq!(unsafe { unsafe_bytes.as_bytes() }, TEST_BYTES);
    }

    #[test]
    fn test_conversion_chain_owned_to_borrowed_to_unsafe() {
        let owned = from_str(TEST_STR).dup();
        let borrowed: BorrowedUtf8Lstr<'_> = (&owned).into();
        let unsafe_utf8: UnsafeUtf8Lstr = borrowed.into();

        assert_eq!(unsafe { unsafe_utf8.as_str() }, TEST_STR);
    }

    // }}}
    // {{{ AsRawLstr trait tests

    #[test]
    fn test_as_raw_lstr_trait() {
        fn generic_len<T: AsRawLstr>(lstr: &T) -> usize {
            lstr.as_raw().len()
        }

        let bytes = from_bytes(TEST_BYTES);
        let utf8 = from_str(TEST_STR);
        let owned = from_str(TEST_STR).dup();

        assert_eq!(generic_len(&bytes), TEST_BYTES.len());
        assert_eq!(generic_len(&utf8), TEST_STR.len());
        assert_eq!(generic_len(&owned), TEST_STR.len());
    }

    // }}}
    // {{{ lstr_t direct tests

    #[test]
    fn test_lstr_t_len() {
        let borrowed = from_bytes(TEST_BYTES);
        let raw_lstr = borrowed.as_raw();

        assert_eq!(raw_lstr.len(), TEST_BYTES.len());
    }

    #[test]
    fn test_lstr_t_is_empty() {
        let empty = from_bytes(b"");
        let non_empty = from_bytes(TEST_BYTES);

        assert!(empty.as_raw().is_empty());
        assert!(!non_empty.as_raw().is_empty());
    }

    #[test]
    fn test_lstr_t_is_null() {
        let null = null_bytes();
        let non_null = from_bytes(TEST_BYTES);

        assert!(null.as_raw().is_null());
        assert!(!non_null.as_raw().is_null());
    }

    // }}}
}

// }}}
