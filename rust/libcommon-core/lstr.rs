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

//! Conversion between a C `lstr_t` and Rust string/bytes types.
//!
//! There three kinds of `lstr_t` structures:
//!
//! * The raw non-owned `lstr_t`.
//!   This structure is unsafe. This means that getting the underlying buffer and converting a safe
//!   string structure to a raw `lstr_t` is unsafe.
//!
//! * `BorrowedLstr`, a non-owned `lstr_t` bounded to a lifetime.
//!   Accessing the underlying buffer is safe.
//!   This structure is used when converting a Rust string or slice to a `lstr_t`.
//!   It keep tracks of the lifetime of the original Rust string or slice.
//!
//! * `OwnedLstr`, a owned `lstr_t`.
//!   Accessing the underlying buffer is safe.
//!   This structure owns the underlying buffer which is dropped when the structure is dropped.

use std::convert::TryFrom;
use std::fmt;
use std::marker::PhantomData;
use std::ops::{Deref, Drop};
use std::os::raw::c_char;
use std::ptr;
use std::str::Utf8Error;

use crate::bindings::{lstr_dup, lstr_equal, lstr_t__bindgen_ty_1, lstr_wipe, t_lstr_dup};
use crate::helpers::slice_from_nullable_raw_parts;
use crate::mem_stack::TScope;

#[allow(clippy::module_name_repetitions)]
pub use crate::bindings::lstr_t;

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
const unsafe fn get_as_bytes(lstr: &lstr_t) -> &[u8] {
    let ptr = unsafe { lstr.__bindgen_anon_1.data as *const u8 };
    let len = lstr.len as usize;

    unsafe { slice_from_nullable_raw_parts(ptr, len) }
}

// }}}
// {{{ Public constructors

/// Create a null `lstr_t`.
pub const fn null() -> lstr_t {
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr::null() },
        len: 0,
        mem_pool: 0,
    }
}

/// Create an empty `lstr_t`.
pub const fn empty() -> lstr_t {
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: c"".as_ptr() },
        len: 0,
        mem_pool: 0,
    }
}

/// Create a non-owned `lstr_t` from a Rust bytes slice.
pub const fn from_bytes(bytes: &[u8]) -> BorrowedLstr<'_> {
    BorrowedLstr {
        lstr: lstr_t {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 {
                s: bytes.as_ptr().cast::<i8>(),
            },
            len: bytes.len().cast_signed() as i32,
            mem_pool: 0,
        },
        _phantom: PhantomData,
    }
}

/// Create a non-owned `lstr_t` from a Rust `str`.
pub const fn from_str(s: &str) -> BorrowedLstr<'_> {
    from_bytes(s.as_bytes())
}

/// Create a non-owned `lstr_t` from a C raw pointer.
///
/// # Safety
///
/// `ptr` shall not be `null` and should point to a valid C string.
pub unsafe fn from_ptr(ptr: *const c_char) -> lstr_t {
    let len = unsafe { libc_strlen(ptr) };
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr },
        len: len.cast_signed() as i32,
        mem_pool: 0,
    }
}

/// Create a non-owned `lstr_t` from a C raw pointer and length.
pub const fn from_ptr_and_len(ptr: *const c_char, len: usize) -> lstr_t {
    lstr_t {
        __bindgen_anon_1: lstr_t__bindgen_ty_1 { s: ptr },
        len: len.cast_signed() as i32,
        mem_pool: 0,
    }
}

/// Create a raw `lstr_t` from a static value (`str` or `slice`).
pub fn raw<T>(v: T) -> lstr_t
where
    T: Into<BorrowedLstr<'static>>,
{
    let bounded_lstr: BorrowedLstr<'static> = v.into();
    unsafe { bounded_lstr.as_raw() }
}

// }}}
// {{{ AsRaw trait.

/// Trait to convert a value to a raw `lstr_t`.
pub trait AsRaw {
    /// Create a raw `lstr_t` from this value.
    ///
    /// # Safety
    ///
    /// This method is unsafe because we lose the potential lifetime of the `lstr_t`.
    unsafe fn as_raw(&self) -> lstr_t;
}

// }}}
// {{{ Common implementation.

/// Macro rule to generate the common implementation for the different `lstr_t` structures below.
macro_rules! lstr_common_impl {
    ($name:ident, $self:ident, $lstr_attr:expr$(, $elided_lifetime:lifetime)?) => {
        impl $name$(< $elided_lifetime >)? {
            /// Get the length of the `lstr_t`.
            #[inline]
            pub const fn len(&$self) -> usize {
                let lstr = $lstr_attr;
                lstr.len as usize
            }

            /// Check if the `lstr_t` is empty (len == 0).
            #[inline]
            pub const fn is_empty(&$self) -> bool {
                let lstr = $lstr_attr;
                lstr.len == 0
            }

            /// Check if the `lstr_t` is null (!s).
            #[inline]
            pub const fn is_null(&$self) -> bool {
                let lstr = $lstr_attr;
                unsafe { lstr.__bindgen_anon_1.s.is_null() }
            }

            /// Check if two `lstr_t` are equals.
            #[inline]
            pub fn equals<T: AsRaw>(&$self, other: &T) -> bool {
                let lstr = $lstr_attr;
                unsafe { lstr_equal(lstr, other.as_raw()) }
            }

            /// Duplicate the `lstr_t` on the given `TScope`.
            #[must_use]
            #[inline]
            pub fn t_dup<'t>(&$self, _t_scope: &'t TScope) -> BorrowedLstr<'t> {
                let lstr = $lstr_attr;
                BorrowedLstr {
                    lstr: unsafe { t_lstr_dup(lstr) },
                    _phantom: PhantomData,
                }
            }

            /// Duplicate the `lstr_t` and return a `OwnedLstr`.
            #[must_use]
            #[inline]
            pub fn dup(&$self) -> OwnedLstr {
                let lstr = $lstr_attr;
                OwnedLstr {
                    lstr: unsafe { lstr_dup(lstr) },
                }
            }
        }

        impl AsRaw for $name$(< $elided_lifetime >)? {
            #[inline]
            unsafe fn as_raw(&$self) -> lstr_t {
                $lstr_attr
            }
        }
    };
}

// }}}
// {{{ Unsafe str conversion

/// Macro rule to generate the unsafe string buffer conversion implementation for the `lstr_t`
/// structure below.
macro_rules! lstr_unsafe_str_conv_impl {
    ($name:ident, $self:ident, $lstr_ref:expr) => {
        impl $name {
            /// Convert a `lstr_t` to a bytes slice.
            ///
            /// # Safety
            ///
            /// The `lstr_t` does not own the underlying buffer.
            #[inline]
            pub const unsafe fn as_bytes(&$self) -> &[u8] {
                unsafe { get_as_bytes($lstr_ref) }
            }

            /// Convert a `lstr_t` to a non-owned Rust str and check UTF-8 errors.
            ///
            /// # Safety
            ///
            /// The `lstr_t` does not own the underlying buffer.
            ///
            /// # Errors
            ///
            /// The `lstr_t` does not correspond to a valid UTF-8 string.
            #[inline]
            pub const unsafe fn as_str(&$self) -> Result<&str, Utf8Error> {
                unsafe { str::from_utf8($self.as_bytes()) }
            }

            /// Convert a `lstr_t` to a non-owned Rust str without checking for UTF-8 errors.
            ///
            /// # Safety
            ///
            /// The `lstr_t` does not own the underlying buffer.
            /// This method is unsafe because converting a slice to a string without checking UTF-8
            /// errors is unsafe.
            #[inline]
            pub const unsafe fn as_str_unchecked(&$self) -> &str {
                unsafe { str::from_utf8_unchecked($self.as_bytes()) }
            }
        }
    };
}

// }}}
// {{{ Safe str conversion

/// Macro rule to generate the safe string buffer conversion implementation for the `lstr_t`
/// structures bellow.
macro_rules! lstr_safe_str_conv_impl {
    ($name:ident, $self:ident, $lstr_ref:expr$(, $elided_lifetime:lifetime)?) => {
        impl $name$(< $elided_lifetime >)? {
            /// Convert a `lstr_t` to a bytes slice.
            #[inline]
            pub const fn as_bytes(&$self) -> &[u8] {
                unsafe { get_as_bytes($lstr_ref) }
            }

            /// Convert a `lstr_t` to a non-owned Rust str and check UTF-8 errors.
            ///
            /// # Errors
            ///
            /// The `lstr_t` does not correspond to a valid UTF-8 string.
            #[inline]
            pub const fn as_str(&$self) -> Result<&str, Utf8Error> {
                str::from_utf8($self.as_bytes())
            }

            /// Convert a `lstr_t` to a non-owned Rust str without checking for UTF-8 errors.
            ///
            /// # Safety
            ///
            /// This method is unsafe because converting a slice to a string without checking UTF-8
            /// errors is unsafe.
            #[inline]
            pub const unsafe fn as_str_unchecked(&$self) -> &str {
                unsafe { str::from_utf8_unchecked($self.as_bytes()) }
            }
        }

        impl Deref for $name$(< $elided_lifetime >)? {
            type Target = [u8];

            #[inline]
            fn deref(&$self) -> &Self::Target {
                $self.as_bytes()
            }
        }

        impl AsRef<[u8]> for $name$(< $elided_lifetime >)? {
            #[inline]
            fn as_ref(&$self) -> &[u8] {
                $self.as_bytes()
            }
        }

        impl fmt::Display for $name$(< $elided_lifetime >)? {
            fn fmt(&$self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                if let Ok(s) = $self.as_str() {
                    f.write_str(s)
                } else {
                    write!(f, "{:x?}", $self.as_bytes())
                }
            }
        }
    };
}

// }}}
// {{{ Raw lstr_t

lstr_common_impl!(lstr_t, self, *self);
lstr_unsafe_str_conv_impl!(lstr_t, self, self);

// }}}
// {{{ BorrowedLstr

/// Representation of a non-owned `lstr_t` bounded to a lifetime.
#[allow(clippy::module_name_repetitions)]
#[repr(transparent)]
pub struct BorrowedLstr<'a> {
    lstr: lstr_t,
    _phantom: PhantomData<&'a lstr_t>,
}

lstr_common_impl!(BorrowedLstr, self, self.lstr, '_);
lstr_safe_str_conv_impl!(BorrowedLstr, self, &self.lstr, '_);

impl<'a> TryFrom<&'a BorrowedLstr<'a>> for &'a str {
    type Error = Utf8Error;

    fn try_from(lstr: &'a BorrowedLstr<'a>) -> Result<&'a str, Self::Error> {
        lstr.as_str()
    }
}

impl<'a> From<&'a [u8]> for BorrowedLstr<'a> {
    fn from(bytes: &'a [u8]) -> BorrowedLstr<'a> {
        from_bytes(bytes)
    }
}

impl<'a> From<&'a str> for BorrowedLstr<'a> {
    fn from(str: &'a str) -> BorrowedLstr<'a> {
        BorrowedLstr::from(str.as_bytes())
    }
}

// }}}
// {{{ OwnedLstr

/// Representation of a owned `lstr_t`.
#[allow(clippy::module_name_repetitions)]
#[repr(transparent)]
pub struct OwnedLstr {
    lstr: lstr_t,
}

lstr_common_impl!(OwnedLstr, self, self.lstr);
lstr_safe_str_conv_impl!(OwnedLstr, self, &self.lstr);

impl Drop for OwnedLstr {
    fn drop(&mut self) {
        unsafe {
            lstr_wipe(&raw mut self.lstr);
        };
    }
}

impl Clone for OwnedLstr {
    fn clone(&self) -> Self {
        self.dup()
    }
}

// }}}
