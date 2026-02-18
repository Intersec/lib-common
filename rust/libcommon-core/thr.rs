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

//! Module to export the thread functions used by `libcommon` in a safe way
//!
//! Using `attach()` and `detach()` are required to use `TScope`.
//!
//! `main_c_queue_schedule()` can be used to run a closure into the main C thread.

use std::os::raw::c_void;

use crate::bindings::{
    module_is_loaded, thr_attach, thr_detach, thr_get_module, thr_is_on_queue,
    thr_main_queue_schedule, thr_queue_main_g,
};

// {{{ Attach/Detach

/// Attach the thread to `libcommon` and call the attach callbacks.
pub fn attach() {
    unsafe {
        thr_attach();
    };
}

/// Detach the thread from `libcommon` and call the detach callbacks.
pub fn detach() {
    unsafe {
        thr_detach();
    };
}

// }}}
// {{{ Run main thread

/// Trampoline function to call the closure from the main C thread.
unsafe extern "C" fn main_c_thread_call_closure<F>(data: *mut c_void)
where
    F: FnOnce() + Send + 'static,
{
    let callback_ptr = data.cast::<F>();
    let callback = unsafe { Box::from_raw(callback_ptr) };
    callback();
}

/// Schedule a closure to be run on the main C thread.
///
/// # Example
///
/// ```ignore
/// main_c_queue_schedule(move || {
///     unsafe {
///         ic_reply(...);
///     }
/// });
/// ```
pub fn main_c_queue_schedule<F>(callback: F)
where
    F: FnOnce() + Send + 'static,
{
    // Assert that the `thr` module is loaded.
    debug_assert!(unsafe { module_is_loaded(thr_get_module()) });

    if unsafe { thr_is_on_queue(thr_queue_main_g) } {
        // We are already on the main C thread, run the callback immediately.
        callback();
        return;
    }

    // Else, schedule the callback on the main thread.
    let data = Box::into_raw(Box::new(callback));
    unsafe {
        // Bindgen is weird and expects an Option here.
        thr_main_queue_schedule(Some(main_c_thread_call_closure::<F>), data.cast());
    }
}

// }}}
