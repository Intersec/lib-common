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

//! `t_pool` implementation and manipulation in Rust.
//!
//! Unlike in C the [`TScope`] allocator object needs to be passed around to properly associate the
//! lifetimes of the variables.
//!
//! The destructor is not called for values allocated by the [`TScope`].

use std::marker::PhantomData;
use std::ops::Drop;
use std::os::raw::c_void;
use std::{mem, ptr};

use crate::bindings::{
    MEM_RAW, mem_stack_pool_pop, mem_stack_pool_push, mp_imalloc, t_pool, t_stack_pool,
};

/// Trait of struct that can be clone on the `t_scope`.
pub trait TScopeClone {
    /// Clone the value on the given `t_scope`.
    #[must_use]
    fn t_clone(&self, t_scope: &TScope<'_>) -> Self;
}

/// Rust representation of a `TScope`
pub struct TScope<'a> {
    new_scope: Option<*const c_void>,
    phantom: PhantomData<&'a c_void>,
}

impl<'a> TScope<'a> {
    /// Initialize the `TScope` from a parent `t_scope`.
    pub fn from_parent() -> TScope<'static> {
        TScope {
            new_scope: None,
            phantom: PhantomData,
        }
    }

    /// Create a new `t_scope` for the `TScope`.
    ///
    /// It is pop when the `TScope` object is dropped.
    pub fn new_scope() -> Self {
        let new_scope = unsafe { mem_stack_pool_push(t_stack_pool()) };
        Self {
            new_scope: Some(new_scope),
            phantom: PhantomData,
        }
    }

    /// Create a new value allocated on the `t_scope`.
    pub fn t_new<T>(&self, val: T) -> &'a mut T {
        unsafe {
            let p = mp_imalloc(t_pool(), mem::size_of::<T>(), mem::align_of::<T>(), MEM_RAW);
            let p = p.cast::<T>();
            ptr::write(p, val);
            &mut *p
        }
    }

    /// Duplicate a value on the `t_scope` by using the [`TScopeClone`] trait.
    pub fn t_clone<T>(&self, val: &T) -> T
    where
        T: TScopeClone,
    {
        val.t_clone(self)
    }
}

/// Drop the `t_scope` if it was created.
impl Drop for TScope<'_> {
    fn drop(&mut self) {
        if let Some(new_scope) = self.new_scope {
            let pop_scope = unsafe { mem_stack_pool_pop(t_stack_pool()) };
            assert!(pop_scope == new_scope);
        }
    }
}
