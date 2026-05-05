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
use std::os::raw::{c_int, c_void};
use std::thread::{self, JoinHandle};
use tokio::runtime::Builder;
use tokio::sync::oneshot::{self, Sender};

#[derive(Default)]
struct TokioCMod {
    shutdown_send: Option<Sender<()>>,
    tokio_thr: Option<JoinHandle<()>>,
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
    }
}

c_module!(tokio, TokioCMod, |builder| {
    builder
        .depends_on(unsafe { thr_get_module() })
        .initialize(tokio_c_mod_initialize)
        .implement_int(&raw const on_term_method, tokio_c_mod_on_term)
        .shutdown(tokio_c_mod_shutdown);
});
