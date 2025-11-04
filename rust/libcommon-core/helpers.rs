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

//! Utility helpers functions.

use std::mem::{MaybeUninit, transmute};
use std::ptr::NonNull;
use std::slice::from_raw_parts;

/// Create a slice from a potential null pointer and length.
///
/// `std::slice::from_raw_parts()` must be used with a non-null pointer.
/// If the pointer is null, use a dandling non-null pointer.
///
/// # Safety
///
/// See `from_raw_parts.html#safety`, except that `data` can be null.
pub const unsafe fn slice_from_nullable_raw_parts<'a, T>(data: *const T, len: usize) -> &'a [T] {
    let mut data = data;

    if data.is_null() {
        assert!(len == 0); // rust-lang#119826
        data = NonNull::dangling().as_ptr();
    }

    unsafe { from_raw_parts(data, len) }
}

/// Gets a mutable (unique) reference to a maybe uninitialized slice.
///
/// # Warning
///
/// This function assumes that the slice is already initialized. Using this function on an
/// uninitialized slice is undefined behavior and can lead to safety issues, crashes, or other
/// unpredictable behaviors. Do not use it to initialize the slice; use a mutable pointer instead.
///
/// # Safety
///
/// The caller must ensure that all elements in the slice are fully initialized before calling
/// this function. Calling this function with an uninitialized slice is undefined behavior.
///
/// TODO: use `slice::assume_init_mut` once stabilized.
pub const unsafe fn slice_assume_init_mut<T>(slice: &mut [MaybeUninit<T>]) -> &mut [T] {
    unsafe { transmute(slice) }
}
