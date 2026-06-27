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

//! Synchronization helpers shared between the workspace's tests.
//!
//! C modules and the C event loop are process-wide singletons. Requiring a
//! module that is already required aborts with a `module '<name>' has been
//! recursively required` fatal error, and the event loop cannot be driven from
//! several threads at once.
//!
//! Unlike the C test suite, where tests run sequentially, the Rust test
//! harness runs the tests of a single crate concurrently within one process.
//! So two Rust tests that require the same module or run the event loop would
//! race against each other.
//!
//! Such tests must therefore be serialized. Rather than guarding each one by
//! hand, declare them with the [`c_event_loop_test`](crate::c_event_loop_test)
//! attribute, which acquires the shared lock (via [`c_event_loop_guard`]) for
//! the whole body of every test it generates:
//!
//! ```ignore
//! use libcommon_core::c_event_loop_test;
//!
//! #[c_event_loop_test]
//! fn drives_the_event_loop() {
//!     /* require a C module, run el_loop(), ... */
//! }
//! ```
//!
//! This module is only compiled for the crate's own tests and, for downstream
//! crates, when they enable the `test-support` feature on `libcommon-core` in
//! their `[dev-dependencies]`.

use std::sync::{Mutex, MutexGuard, PoisonError};

/// Serializes the tests that require a C module or run the C event loop.
static C_EVENT_LOOP: Mutex<()> = Mutex::new(());

/// Acquire exclusive access to the C runtime singletons (C modules and the C
/// event loop) for the lifetime of the returned guard.
///
/// Prefer the [`c_event_loop_test`](crate::c_event_loop_test) attribute, which
/// calls this for you. Use this directly only when the attribute does not fit.
pub fn c_event_loop_guard() -> MutexGuard<'static, ()> {
    // Ignore poisoning: a test panicking while holding the lock must not
    // cascade into failing every other test that takes it.
    C_EVENT_LOOP.lock().unwrap_or_else(PoisonError::into_inner)
}
