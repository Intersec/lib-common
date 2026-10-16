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

//! Rust API of `libcommon-minimal`, the part of lib-common that the code generators need.
//!
//! The code generators of lib-common, such as `farchc` and `iopc`, are built before the full
//! `libcommon` C library. This crate only wraps `libcommon-minimal`, so that these tools can use
//! it.
//!
//! Use its modules through [`libcommon`](../libcommon/index.html): it re-exports all of them, and
//! adds the modules that need the full C library.
//!
//! A package that has bindings must still depend on this crate, because the code that
//! `waf-cargo-build` generates for the `qv_t`, `qh_t` and IOP types names it.
//!
//! The `test-support` feature is for the tests of a package. It enables the `c_event_loop_test`
//! attribute and the `test_sync` module.

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    waf_cargo_build::include_bindings!();
}

pub mod farch;
pub mod helpers;
pub mod log;
pub mod lstr;
pub mod mem_stack;
pub mod module;
pub mod pstream;
pub mod qhash;
pub mod qhashmap;
pub mod qhashset;
pub mod qvector;
pub mod sb;
pub mod thr;

// Test-only synchronization helpers. Always available to this crate's own
// tests, and to downstream crates that enable the `test-support` feature.
#[cfg(any(test, feature = "test-support"))]
pub mod test_sync;

// Re-export the test-support proc-macros so downstream crates only need a
// dev-dependency on libcommon-core (with the `test-support` feature) rather
// than on the macro crate directly.
#[cfg(feature = "test-support")]
pub use libcommon_test_macros::c_event_loop_test;

// Re-export the IOP ArcSwap primitives so every consumer of
// `libcommon-core` transitively links the `#[no_mangle]` FFI symbols
// from `libcommon-iop-arcswap`.
pub use libcommon_iop_arcswap as iop_env_arcswap;
