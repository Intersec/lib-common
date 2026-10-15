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

//! Rust API of lib-common, the C base library of Intersec.
//!
//! The Rust crates of lib-common wrap its C code: they do not rewrite it. This crate is the entry
//! point. It holds the raw bindings of the C library, and a safe Rust API over the C types that
//! Rust code uses most.
//!
//! ```
//! use libcommon::mem_stack::{TCollect as _, TScope};
//! use libcommon::qvector::QVector;
//!
//! // A C `qv_t(u32)`, allocated on the `t_pool` and sorted with the slice API.
//! let t_scope = TScope::new_scope();
//! let mut ids: QVector<'_, u32> = [3, 1, 2].into_iter().t_collect(&t_scope);
//! ids.sort_unstable();
//!
//! assert_eq!(*ids, [1, 2, 3]);
//! ```
//!
//! # Crates
//!
//! | Crate | Use it to |
//! |-------|-----------|
//! | [`libcommon`](crate) | Use lib-common from Rust. Start here. |
//! | [`ichannel`](../ichannel/index.html) | Send `IChannel` queries from Rust. |
//! | [`tokio_c_mod`](../tokio_c_mod/index.html) | Run a tokio runtime next to the C event loop. |
//! | [`waf_cargo_build`] | Connect the `build.rs` of a package to the waf build. |
//! | [`libcommon_core`](../libcommon_core/index.html) | Name the crate in `Cargo.toml`: the generated bindings need it. `libcommon` re-exports its modules. |
//! | [`farchc`](../farchc/index.html) | Compile the farch scripts into C files. waf runs it. |
//! | [`ex_ichannel`](../ex_ichannel/index.html) | Read a full example: a chat over `IChannel`. |
//!
//! The other crates are internal: [`libcommon_iop_arcswap`](../libcommon_iop_arcswap/index.html),
//! [`libcommon_test_macros`](../libcommon_test_macros/index.html),
//! [`waf_cargo_build_macros`](../waf_cargo_build_macros/index.html) and
//! [`deps_workspace_hack`](../deps_workspace_hack/index.html).
//!
//! # Modules
//!
//! | Topic | Modules |
//! |-------|---------|
//! | Strings and buffers | [`lstr`], [`sb`], [`pstream`] |
//! | Containers | [`qvector`], [`qhashset`], [`qhashmap`], and [`qhash`] for what they share |
//! | Memory | [`mem_stack`] |
//! | IOP | [`iop`], and the [`iop_new!`], [`iop_get!`] and [`iop_set!`] macros |
//! | Modules and logs | [`module`], [`log`] |
//! | Threads and event loop | [`thr`], [`c_main_waker`], [`run_callback`] |
//! | Embedded files | [`farch`] |
//! | C API | [`bindings`], [`helpers`] |
//!
//! # Concepts
//!
//! ## Bindings
//!
//! `bindgen` generates the [`bindings`] module from the C headers. It holds the C functions, types
//! and constants, with their C names. The functions are `unsafe` to call: if a module of this
//! crate covers the C type, use it instead.
//!
//! Each package that uses C code has its own `bindings` module. It re-exports the `bindings`
//! module of its dependencies, and adds the C items of its own headers. So `crate::bindings`
//! gives every C item that the package can use.
//!
//! ## Allocators
//!
//! A wrapper of a C type takes its allocator from its constructor. `new()` allocates with libc.
//! `t_new()` allocates on the `t_pool` of a [`TScope`](mem_stack::TScope): the value borrows the
//! [`TScope`](mem_stack::TScope), so the compiler rejects a use after the end of the scope.
//!
//! ## Threads
//!
//! Most of the C code of lib-common runs on the main thread, in the C event loop (`el_loop()`).
//! A Rust thread that the C code did not create uses these tools:
//!
//! - [`thr::attach()`] makes the thread able to use a [`TScope`](mem_stack::TScope).
//! - [`thr::main_c_queue_schedule()`] runs a closure on the main thread.
//! - [`run_callback::run_callback()`] runs a C function that ends with a callback on the main
//!   thread, and gives a future of its result.
//! - [`c_main_waker::run_future()`] polls a future on the main thread.
//! - [`tokio_c_mod`](../tokio_c_mod/index.html) runs a tokio runtime, and stops it when the
//!   process stops.
//!
//! ## C modules
//!
//! A lib-common module has an initialization, a shutdown and dependencies, like the C
//! `MODULE_BEGIN()`. The [`c_module!`](module::c_module) macro defines one in Rust.
//! [`module::module_require()`] and [`module::module_release()`] start and stop it.
//!
//! # Add a Rust package to a project
//!
//! The project shares the Cargo workspace of lib-common. In this example, the package is
//! `my-pkg`, in the `my-pkg/` directory of the project.
//!
//! 1. Add `"my-pkg"` to the `members` of the workspace `Cargo.toml`.
//! 2. In `my-pkg/Cargo.toml`, depend on the lib-common crates:
//!
//!    ```toml
//!    [lints]
//!    workspace = true
//!
//!    [build-dependencies]
//!    waf-cargo-build = { path = "../lib-common/rust/waf-cargo-build" }
//!
//!    [dependencies]
//!    waf-cargo-build = { path = "../lib-common/rust/waf-cargo-build" }
//!    libcommon = { path = "../lib-common/rust/libcommon" }
//!    libcommon-core = { path = "../lib-common/rust/libcommon-core" }
//!    ```
//!
//! 3. In `my-pkg/wscript_build`, declare a waf target for the package. waf builds the C code that
//!    it uses, and the bindings of its dependencies, before cargo builds it:
//!
//!    ```python
//!    ctx(
//!        target='my-pkg-rs',
//!        features='rust',
//!        cargo_package='my-pkg',
//!        use=['libcommon-rs', 'libcommon-core-rs'],
//!    )
//!    ```
//!
//! 4. In `my-pkg/wrapper.h`, include the C headers of the package.
//! 5. In `my-pkg/build.rs`, generate the bindings with [`waf_cargo_build`]. Its documentation
//!    gives the `build.rs` and the `bindings` module to copy.
//!
//! [`ex_ichannel`](../ex_ichannel/index.html) is a complete package built this way, in the
//! `rust/examples/` directory of lib-common. `README-rust.adoc`, at the root of lib-common,
//! describes how waf and cargo work together.
//!
//! # Build this documentation
//!
//! The Rust code calls the C code, so waf must build the C code first:
//!
//! ```sh
//! waf configure
//! waf
//! cargo doc --workspace --no-deps
//! ```

// The Rust that `iopc` writes names the items of libcommon by their full
// path, `::libcommon::…`, also in the bindings of libcommon itself.
extern crate self as libcommon;

pub mod c_main_waker;
pub mod iop;
pub mod run_callback;

/// The C functions, types and constants of lib-common, generated by `bindgen`.
#[waf_cargo_build::bindings_mod]
pub mod bindings {
    use crate::iop::{Base, CStruct, CStructUnion, Struct, StructUnion};
    pub use libcommon_core::bindings::*;
    waf_cargo_build::include_bindings!();

    // `iop.Void` has no `.iop` file of its own, so iopc emits no native
    // Rust for it; implement its traits by hand on bindgen's type.
    impl Base for iop__void__t {}
    impl StructUnion for iop__void__t {
        fn get_cdesc(&self) -> *const iop_struct_t {
            &raw const iop__void__s
        }
    }
    impl CStructUnion for iop__void__t {
        const CDESC: *const iop_struct_t = &raw const iop__void__s;
    }
    impl Struct for iop__void__t {}
    impl CStruct for iop__void__t {}
}

// Reexport `libcommon_core` types.
pub use libcommon_core::{
    farch, helpers, log, lstr, mem_stack, module, pstream, qhash, qhashmap, qhashset, qvector, sb,
    thr,
};

// Reexport `paste` so that downstream crates using iop macros don't need it as
// a direct dependency. It is an implementation detail of these macros.
#[doc(hidden)]
pub use paste;
