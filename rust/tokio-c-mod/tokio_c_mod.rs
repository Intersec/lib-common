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

//! Infrastructure for creating tokio threads able to communicate with C event loop.
//!
//! This provides a `TokioCMod` exposed to C with [`c_module!`] as
//! `tokio_c_module`. A global [`OnceLock`] stores the tokio runtime's [`Handle`]
//! allowing us to run a thread with a custom [`Future`] object.
//!
//! # Overview
//!
//! The `tokio_c_module` is implemented with 3 methods:
//! - `initialize` : initialize the module, with a `shutdown_send` signal and a runtime.
//! - `on_term` : send the shutdown signal to the Tokio thread when the process
//!   receives a termination signal.
//! - `shutdown` : terminate the thread and make sure it is finished.
//!
//! The [`spawn`] function allows us to spawn a task using the `tokio_c_module`
//! runtime and the [`Future`] input. It returns a [`tokio::task::JoinHandle`].
//!
//! # Example
//!
//! ## Use `tokio_c_module` with `main_c_queue_schedule`
//! ```
//! # use tokio_c_mod::tokio_get_module;
//! # use libcommon_core::bindings::{el_blocker_register, el_loop, el_unregister, ev_t};
//! # use libcommon_core::module::{module_is_loaded, module_release, module_require};
//! # use libcommon_core::thr::main_c_queue_schedule;
//! # struct ElBlocker(*mut ev_t);
//! # unsafe impl Send for ElBlocker {}
//! module_require(tokio_get_module());
//! let blocker = ElBlocker(unsafe { el_blocker_register() });
//!
//! tokio_c_mod::spawn(async move {
//!     main_c_queue_schedule(move || {
//!         let mut blocker = blocker;
//!         unsafe {
//!             el_unregister(&raw mut blocker.0);
//!         }
//!     });
//! });
//!
//! unsafe {
//!     el_loop();
//! }
//! module_release(tokio_get_module());
//! ```

use libcommon_core::bindings::{on_term_method, thr_get_module};
use libcommon_core::{c_module, thr};
use std::future::Future;
use std::os::raw::{c_int, c_void};
use std::sync::OnceLock;
use std::thread::{self, JoinHandle};
use tokio::runtime::{Builder, Handle};
use tokio::sync::oneshot::{self, Sender};
use tokio::task::JoinHandle as TokioJoinHandle;

// {{{ TokioCMod

/// Global variable to store Tokio C Module runtime.
static TOKIO_RUNTIME: OnceLock<Handle> = OnceLock::new();

/// Structure to build a tokio c module.
///
/// Fields are Options so the struct can derive Default, since neither
/// the Sender nor the thread `JoinHandle` exists before initialization.
#[derive(Default)]
struct TokioCMod {
    shutdown_send: Option<Sender<()>>,
    tokio_thr: Option<JoinHandle<()>>,
}

/// Spawn a future on the `tokio_c_module` runtime.
///
/// # Arguments
///
/// `future` - the async task to execute on the `TOKIO_RUNTIME`
///
/// # Returns
///
/// Returns a [`tokio::task::JoinHandle`] for the spawned future.
///
/// # Example
///
/// ```
/// # use tokio_c_mod::tokio_get_module;
/// # use libcommon_core::module::{module_require,module_release};
/// #
/// # module_require(tokio_get_module());
///   tokio_c_mod::spawn(async move {
///      println!("some task");
///   });
/// # module_release(tokio_get_module());
/// ```
pub fn spawn<F>(future: F) -> TokioJoinHandle<F::Output>
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
{
    TOKIO_RUNTIME.wait().spawn(future)
}

// }}}
// {{{ tokio c module

/// Initialize a `tokio_c_module` and the global variable `TOKIO_RUNTIME`.
fn tokio_c_mod_initialize(ctx: &mut TokioCMod, _arg: *mut c_void) {
    // Oneshot because tokio_c_mod must be reinitialized after shutdown, not reused.
    let (shutdown_send, shutdown_recv) = oneshot::channel();
    // Oneshot to hand the runtime handle back so we can wait for the tokio
    // thread to have built its runtime before returning.
    let (runtime_send, runtime_recv) = oneshot::channel();
    ctx.shutdown_send = Some(shutdown_send);
    ctx.tokio_thr = Some(thread::spawn(move || {
        let runtime = Builder::new_multi_thread()
            .enable_all()
            .on_thread_start(thr::attach)
            .on_thread_stop(thr::detach)
            .build()
            .expect("failed to build tokio runtime");
        runtime_send
            .send(runtime.handle().clone())
            .expect("tokio runtime handle receiver dropped");

        let _unused = runtime.block_on(shutdown_recv);
    }));

    // Wait for the tokio thread to have built its runtime. blocking_recv() is
    // safe here: initialize() runs on the main C thread, outside any tokio
    // runtime.
    let handle = runtime_recv
        .blocking_recv()
        .expect("tokio thread stopped before sending its runtime handle");
    TOKIO_RUNTIME
        .set(handle)
        .expect("TOKIO_RUNTIME already set");
}

/// Send a signal to terminate the tokio c mod thread.
fn send_shutdown(ctx: &mut TokioCMod) {
    if let Some(shutdown_send) = ctx.shutdown_send.take() {
        let _unused: Result<(), ()> = shutdown_send.send(());
    }
}

fn tokio_c_mod_on_term(ctx: &mut TokioCMod, _arg: c_int) {
    send_shutdown(ctx);
}

/// Shutdown the `tokio_c_module` and terminate the thread.
fn tokio_c_mod_shutdown(ctx: &mut TokioCMod) {
    send_shutdown(ctx);
    if let Some(thread) = ctx.tokio_thr.take() {
        thread.join().expect("tokio thread panicked");
        unsafe {
            let p = (&raw const TOKIO_RUNTIME).cast_mut();
            (*p).take();
        }
    }
}

// `tokio_c_module` module builder. See `module.rs` for further documentation
c_module!(tokio, TokioCMod, |builder| {
    builder
        .depends_on(unsafe { thr_get_module() })
        .initialize(tokio_c_mod_initialize)
        .implement_int(&raw const on_term_method, tokio_c_mod_on_term)
        .shutdown(tokio_c_mod_shutdown);
});
// }}}

#[cfg(test)]
mod tests {

    use crate::tokio_get_module;
    use libcommon_core::bindings::{el_blocker_register, el_loop, el_unregister, ev_t};
    use libcommon_core::module::{module_is_loaded, module_release, module_require};
    use libcommon_core::thr::main_c_queue_schedule;
    struct ElBlocker(*mut ev_t);
    unsafe impl Send for ElBlocker {}

    #[test]
    fn shutdown_tokio_c_mod_after_c_event_loop() {
        module_require(tokio_get_module());
        assert!(module_is_loaded(tokio_get_module()));

        //  Register a blocker so el_loop() keeps running until the spawned
        //  future fires. The tokio runtime is already live at this point
        //  (module_require returned), so spawn() succeeds immediately. When
        //  tokio polls the future it posts el_unregister to the C main queue
        //  via main_c_queue_schedule, which wakes el_loop. el_loop then
        //  processes the callback, clears the blocker, and returns. There is
        //  no race: el_loop cannot exit before the blocker is cleared.
        let blocker = ElBlocker(unsafe { el_blocker_register() });

        crate::spawn(async move {
            main_c_queue_schedule(move || {
                let mut blocker = blocker;

                unsafe {
                    el_unregister(&raw mut blocker.0);
                }
            });
        });

        unsafe {
            el_loop();
        }

        module_release(tokio_get_module());
        assert!(!module_is_loaded(tokio_get_module()));
    }
}
