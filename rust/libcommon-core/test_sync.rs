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
//! [`C_EVENT_LOOP`] serializes them: each such test must hold the lock for its
//! whole body so that at most one of them touches those singletons at a time.
//!
//! This module is only compiled for the crate's own tests and, for downstream
//! crates, when they enable the `test-support` feature on `libcommon-core` in
//! their `[dev-dependencies]`.

use std::sync::Mutex;

/// Serialize the tests that require a C module or run the C event loop.
///
/// Those tests rely on process-wide singletons and cannot run concurrently
/// with each other, so each such test must hold this lock for its whole body.
pub static C_EVENT_LOOP: Mutex<()> = Mutex::new(());
