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

use libcommon_core::bindings::{on_term_method, thr_get_module};
use libcommon_core::{c_module, thr};
use std::future::Future;
use std::os::raw::{c_int, c_void};
use std::sync::OnceLock;
use std::thread::{self, JoinHandle};
use tokio::runtime::{Builder, Handle};
use tokio::sync::oneshot::{self, Sender};
use tokio::task::JoinHandle as TokioJoinHandle;

static TOKIO_RUNTIME: OnceLock<Handle> = OnceLock::new();

#[derive(Default)]
struct TokioCMod {
    shutdown_send: Option<Sender<()>>,
    tokio_thr: Option<JoinHandle<()>>,
}

pub fn spawn<F>(future: F) -> TokioJoinHandle<F::Output>
where
    F: Future + Send + 'static,
    F::Output: Send + 'static,
{
    TOKIO_RUNTIME.wait().spawn(future)
}

fn tokio_c_mod_initialize(ctx: &mut TokioCMod, _arg: *mut c_void) {
    // Oneshot because tokio_c_mod must be reinitialized after shutdown, not reused.
    let (shutdown_send, shutdown_recv) = oneshot::channel();
    ctx.shutdown_send = Some(shutdown_send);
    ctx.tokio_thr = Some(thread::spawn(move || {
        let runtime = Builder::new_multi_thread()
            .enable_all()
            .on_thread_start(thr::attach)
            .on_thread_stop(thr::detach)
            .build()
            .expect("failed to build tokio runtime");
        TOKIO_RUNTIME
            .set(runtime.handle().clone())
            .expect("TOKIO_RUNTIME already set");

        let _unused = runtime.block_on(shutdown_recv);
    }));
}

fn send_shutdown(ctx: &mut TokioCMod) {
    if let Some(shutdown_send) = ctx.shutdown_send.take() {
        let _unused: Result<(), ()> = shutdown_send.send(());
    }
}

fn tokio_c_mod_on_term(ctx: &mut TokioCMod, _arg: c_int) {
    send_shutdown(ctx);
}

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

c_module!(tokio, TokioCMod, |builder| {
    builder
        .depends_on(unsafe { thr_get_module() })
        .initialize(tokio_c_mod_initialize)
        .implement_int(&raw const on_term_method, tokio_c_mod_on_term)
        .shutdown(tokio_c_mod_shutdown);
});

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
