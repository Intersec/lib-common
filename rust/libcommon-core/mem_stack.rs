/***************************************************************************/
/*                                                                         */
/* Copyright 2026 INTERSEC SA                                              */
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

//! `t_pool` implementation and manipulation in Rust.
//!
//! Unlike in C the [`TScope`] allocator object needs to be passed around to properly associate the
//! lifetimes of the variables.
//!
//! The destructor is not called for values allocated by the [`TScope`].
//!
//! Using `thr::attach()` and `thr:detach()` are required to use [`TScope`].
//!
//! # Nested scopes
//!
//! The `t_pool` allocates on the innermost `t_scope` of the thread, so a [`TScope`] can only be
//! allocated on while it is itself the innermost one. Allocating on an outer [`TScope`] while a
//! nested `t_scope` is active would return a reference to memory that the nested scope releases:
//! it is undefined behavior, and debug builds panic instead; see [`TScope::assert_innermost`].
//!
//! ```no_run
//! use libcommon_core::mem_stack::TScope;
//!
//! let outer = TScope::new_scope();
//! let inner = TScope::new_scope();
//!
//! // Wrong: `inner` releases the value before the end of `outer`. Debug builds panic here.
//! let value: &mut u32 = outer.t_new();
//! ```

use std::mem::{self, MaybeUninit};
use std::ops::Drop;
use std::os::raw::c_void;
use std::slice::from_raw_parts_mut;

use crate::bindings::{
    MEM_RAW, mem_stack_pool_pop, mem_stack_pool_push, mp_imalloc, t_pool, t_stack_pool,
};

/// Rust representation of a `TScope`
pub struct TScope {
    /// Frame of the `t_pool` stack that the scope allocates on.
    frame: *const c_void,

    /// Whether the scope pushed `frame` itself, and has to pop it when dropped.
    owns_frame: bool,
}

impl TScope {
    /// Initialize the `TScope` from a parent `t_scope`.
    pub fn from_parent() -> TScope {
        TScope {
            frame: Self::current_frame(),
            owns_frame: false,
        }
    }

    /// Create a new `t_scope` for the `TScope`.
    ///
    /// It is popped when the `TScope` object is dropped.
    pub fn new_scope() -> Self {
        let frame = unsafe { mem_stack_pool_push(t_stack_pool()) };

        Self {
            frame,
            owns_frame: true,
        }
    }

    /// Get the frame currently on top of the `t_pool` stack.
    fn current_frame() -> *const c_void {
        unsafe { (*t_stack_pool()).stack.cast::<c_void>() }
    }

    /// Check that `self` is the innermost `t_scope` currently active.
    ///
    /// The `t_pool` always allocates on the innermost frame of its stack, whereas the lifetime
    /// given to a value allocated on a [`TScope`] is the one of that [`TScope`]. Both have to
    /// designate the same frame: otherwise the value is released by the innermost `t_scope`,
    /// before the end of the [`TScope`], and every reference to it dangles.
    ///
    /// Every function that allocates on a [`TScope`] must call this method first. Those that
    /// allocate through [`Self::t_new`] or one of its variants get the check for free; those
    /// that call a C `t_*()` function have to call it explicitly.
    ///
    /// The check is only compiled in debug builds, like the `t_scope` checks of the C side: in
    /// release builds, allocating on a [`TScope`] that is not the innermost one is undefined
    /// behavior.
    ///
    /// # Panics
    ///
    /// In debug builds, `self` is not the innermost `t_scope` currently active.
    #[track_caller]
    pub fn assert_innermost(&self) {
        debug_assert!(
            self.frame == Self::current_frame(),
            "this TScope is not the innermost t_scope currently active: the allocation \
             would be released before the end of the TScope"
        );
    }

    /// Create a new value allocated on the `t_scope`.
    ///
    /// It is initialized to 0.
    ///
    /// # Panics
    ///
    /// In debug builds, `self` is not the innermost `t_scope` currently active.
    #[allow(clippy::mut_from_ref)]
    #[track_caller]
    pub fn t_new<T>(&self) -> &mut T {
        self.assert_innermost();

        unsafe {
            let p = mp_imalloc(t_pool(), mem::size_of::<T>(), mem::align_of::<T>(), 0);
            let p: *mut T = p.cast();
            &mut *p
        }
    }

    /// Create a new value allocated on the `t_scope`.
    ///
    /// It is uninitialized.
    ///
    /// # Panics
    ///
    /// In debug builds, `self` is not the innermost `t_scope` currently active.
    #[allow(clippy::mut_from_ref)]
    #[track_caller]
    pub fn t_new_uninit<T>(&self) -> &mut MaybeUninit<T> {
        self.assert_innermost();

        unsafe {
            let p = mp_imalloc(t_pool(), mem::size_of::<T>(), mem::align_of::<T>(), MEM_RAW);
            let p: *mut MaybeUninit<T> = p.cast();
            &mut *p
        }
    }

    /// Create a new slice allocated on the `t_scope`.
    ///
    /// It is initialized to 0.
    ///
    /// # Panics
    ///
    /// In debug builds, `self` is not the innermost `t_scope` currently active.
    #[allow(clippy::mut_from_ref)]
    #[track_caller]
    pub fn t_new_slice<T>(&self, len: usize) -> &mut [T] {
        self.assert_innermost();

        unsafe {
            let p = mp_imalloc(t_pool(), mem::size_of::<T>() * len, mem::align_of::<T>(), 0);
            let p: *mut T = p.cast();
            from_raw_parts_mut(p, len)
        }
    }

    /// Create a new slice allocated on the `t_scope`.
    ///
    /// It is uninitialized.
    ///
    /// # Panics
    ///
    /// In debug builds, `self` is not the innermost `t_scope` currently active.
    #[allow(clippy::mut_from_ref)]
    #[track_caller]
    pub fn t_new_slice_uninit<T>(&self, len: usize) -> &mut [MaybeUninit<T>] {
        self.assert_innermost();

        unsafe {
            let p = mp_imalloc(
                t_pool(),
                mem::size_of::<T>() * len,
                mem::align_of::<T>(),
                MEM_RAW,
            );
            let p: *mut MaybeUninit<T> = p.cast();
            from_raw_parts_mut(p, len)
        }
    }
}

/// Drop the `t_scope` if it was created.
impl Drop for TScope {
    fn drop(&mut self) {
        if self.owns_frame {
            let popped = unsafe { mem_stack_pool_pop(t_stack_pool()) };

            debug_assert!(popped == self.frame, "unbalanced t_scope");
        }
    }
}

// {{{ Tests

#[cfg(test)]
#[allow(clippy::redundant_test_prefix)]
mod tests {
    use super::*;

    #[test]
    fn test_t_new() {
        let t_scope = TScope::new_scope();
        let value: &mut u32 = t_scope.t_new();

        assert_eq!(*value, 0);
        *value = 1234;
        assert_eq!(*value, 1234);

        let slice: &mut [u32] = t_scope.t_new_slice(4);

        assert_eq!(slice, &[0, 0, 0, 0]);
    }

    #[test]
    fn test_t_new_on_the_innermost_scope() {
        let outer = TScope::new_scope();
        let outer_value: &mut u32 = outer.t_new();

        *outer_value = 1;

        {
            // Allocating on the innermost scope is always allowed.
            let inner = TScope::new_scope();
            let inner_value: &mut u32 = inner.t_new();

            *inner_value = 2;
            assert_eq!(*inner_value, 2);
        }

        // And the outer scope is usable again once the inner one is popped.
        let other: &mut u32 = outer.t_new();

        *other = 3;
        assert_eq!((*outer_value, *other), (1, 3));
    }

    #[test]
    fn test_from_parent_allocates_on_the_current_scope() {
        let _outer = TScope::new_scope();
        let parent = TScope::from_parent();
        let value: &mut u32 = parent.t_new();

        *value = 1234;
        assert_eq!(*value, 1234);
    }

    /// Allocating on an outer scope while a nested one is active would return a reference to
    /// memory released by the nested scope, so it panics instead.
    #[test]
    #[cfg(debug_assertions)]
    #[should_panic(expected = "this TScope is not the innermost t_scope")]
    fn test_t_new_from_a_nested_scope_panics() {
        let outer = TScope::new_scope();
        let _inner = TScope::new_scope();

        let _value: &mut u32 = outer.t_new();
    }

    #[test]
    #[cfg(debug_assertions)]
    #[should_panic(expected = "this TScope is not the innermost t_scope")]
    fn test_t_new_slice_from_a_nested_scope_panics() {
        let outer = TScope::new_scope();
        let _inner = TScope::new_scope();

        let _slice: &mut [u32] = outer.t_new_slice(4);
    }

    #[test]
    #[cfg(debug_assertions)]
    #[should_panic(expected = "this TScope is not the innermost t_scope")]
    fn test_from_parent_then_nested_scope_panics() {
        let parent = TScope::from_parent();
        let _inner = TScope::new_scope();

        let _value: &mut u32 = parent.t_new();
    }
}

// }}}
