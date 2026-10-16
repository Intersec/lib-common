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

//! `IChannel` Rust tests.
//!
//! This crate exercises the `ichannel` crate's `ic_query` end-to-end, both
//! over a real socket and over a local (in-process, loopback) `IChannel`. It
//! is here and not directly in `rust/ichannel/ichannel.rs` so that it can
//! use the RPC defined in `tests/iop/tstiop_rpc.iop` and the test-only C
//! shims declared in `wrapper.h`.

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use ichannel::bindings::*;
    waf_cargo_build::include_bindings!();
}

#[cfg(test)]
mod tests {
    use crate::bindings::{
        __ic_reply, __socket_type::SOCK_SEQPACKET, AF_UNIX, O_NONBLOCK, ic__hdr__t_raw, ic__hdr__u,
        ic__ic_status__e, ic__simple_hdr__t, ic_delete, ic_event_t, ic_get_module, ic_new,
        ic_set_local, ic_spawn, ichannel_t, iop_mod_find_rpc, qm_ic_cbs_t, socketpairx,
        tstiop_rpc__pkg, tstiop_rpc__rpc__test__echo__rpc as EchoRpc,
        tstiop_rpc__test__echo_args__t, tstiop_rpc__test__echo_res__t, z_ic_cbs_init,
        z_ic_cbs_wipe, z_ic_register_echo,
    };
    use ichannel::{ICQuery, IcError};
    use libcommon::bindings::thr_get_module;
    use libcommon::c_main_waker::run_future;
    use libcommon::iop::{Env, IopIface, IopModule as _, IopRPC};
    use libcommon::iop_new;
    use libcommon_core::{
        bindings::{
            data_t, el_blocker_register, el_loop, el_t, el_timer_register, el_unloop,
            el_unregister, ev_t, ev_timer_flags_t,
        },
        c_event_loop_test,
        module::{module_release, module_require},
        thr::main_c_queue_schedule,
    };
    use std::mem::zeroed;
    use std::process::abort;
    use std::sync::atomic::{AtomicI32, Ordering};
    use std::sync::mpsc::{RecvTimeoutError, Sender, channel};
    use std::sync::{Arc, Mutex};
    use std::thread::{JoinHandle, spawn};
    use std::time::Duration;
    use std::{ffi::c_void, ptr};

    use tokio_c_mod::tokio_get_module;

    // {{{ Marker chain

    /// The chain the markers encode -- RPC, then interface, then module --
    /// has to match what the C descriptors say.
    ///
    /// Asking the module for the RPC's command id is the whole chain in one
    /// assertion: it fails if `mod_desc()` names the wrong symbol, if the
    /// interface is bound to the wrong module, or if the command id is
    /// built from the wrong tag.
    #[test]
    fn marker_chain_matches_the_c_descriptors() {
        type Iface = <EchoRpc as IopRPC>::Iface;
        type Module = <Iface as IopIface>::Module;

        // SAFETY: both descriptors are static, and `iop_mod_find_rpc` only
        // walks them.
        let found = unsafe { iop_mod_find_rpc(Module::mod_desc(), EchoRpc::CMD as u32) };

        assert_eq!(found, EchoRpc::rpc_desc());
    }

    // }}}
    // {{{ Setup

    unsafe extern "C" fn dummy_on_event(_ic: *mut ichannel_t, _evt: ic_event_t) {}

    /// The payload of the header that the runners send with each query.
    const HDR_PAYLOAD: i32 = 42;

    /// The payload of the header that the echo implementation got last.
    static RECEIVED_HDR_PAYLOAD: AtomicI32 = AtomicI32::new(0);

    fn echo_hdr() -> ic__hdr__u<'static> {
        ic__hdr__u::simple(iop_new!(ic__simple_hdr, { payload: HDR_PAYLOAD }))
    }

    fn record_hdr(hdr: Option<&ic__hdr__t_raw>) {
        if let Some(ic__hdr__u::simple(hdr)) = hdr {
            RECEIVED_HDR_PAYLOAD.store(hdr.payload, Ordering::Relaxed);
        }
    }

    /// Fails unless the implementation got the header of the runner since
    /// the previous check.
    fn check_hdr() -> Result<(), String> {
        match RECEIVED_HDR_PAYLOAD.swap(0, Ordering::Relaxed) {
            HDR_PAYLOAD => Ok(()),
            payload => Err(format!(
                "the implementation got the header payload {payload}"
            )),
        }
    }

    // server-side echo
    unsafe extern "C" fn echo_impl(
        ic: *mut ichannel_t,
        slot: u64,
        arg: *mut tstiop_rpc__test__echo_args__t,
        hdr: *const ic__hdr__t_raw,
    ) {
        record_hdr(unsafe { hdr.as_ref() });
        let res = tstiop_rpc__test__echo_res__t {
            i: unsafe { (*arg).i },
        };
        let rpc = EchoRpc::rpc_desc();
        unsafe {
            __ic_reply(
                ic,
                slot,
                ic__ic_status__e::OK.0,
                -1,
                (*rpc).result,
                ptr::from_ref(&res).cast::<c_void>(),
            );
        }
    }

    struct SendPtr<T>(*mut T);
    unsafe impl<T> Send for SendPtr<T> {}

    // }}}
    // {{{ Helper

    /// What a test releases once its query has completed.
    struct Teardown {
        ics: Vec<SendPtr<ichannel_t>>,
        cbs: SendPtr<qm_ic_cbs_t>,
        blocker: SendPtr<ev_t>,
    }

    impl Teardown {
        /// Deletes the channels, wipes the table, and unregisters the
        /// blocker, which lets `el_loop()` return.
        ///
        /// # Safety
        ///
        /// Runs on the main C thread, outside of any callback of the
        /// channels.
        unsafe fn release(mut self) {
            unsafe {
                for ic in &mut self.ics {
                    ic_delete(&raw mut ic.0);
                }
                z_ic_cbs_wipe(self.cbs.0);
                el_unregister(&raw mut self.blocker.0);
            }
        }
    }

    /// Releases the test's channels from a zero-delay timer, that is at the
    /// very top of an event loop iteration, outside of any `IChannel`
    /// callback.
    ///
    /// The completion closure cannot do it itself: it runs from the callback
    /// that delivered the reply, where `ic->cb_level` is non-zero and
    /// `ic_wipe()` asserts on it. The C `ic_delete_later()` defers the
    /// same release, but through a before hook, which does not fit here.
    ///
    /// Timers are what `el_loop_timeout()` processes first, and it returns
    /// right after them when `el_unloop()` has been called. Deferring to an
    /// `el_before` hook instead would release everything just as well, but
    /// the iteration would then go on to block in `epoll_wait()` for the
    /// whole loop timeout before noticing it has nothing left to serve.
    ///
    /// One-shot timers unregister themselves once fired, so this hook does
    /// not unregister its timer.
    ///
    /// # Safety
    ///
    /// `data.ptr` must be the `Teardown` leaked by
    /// `run_echo_on_main_c_thread`, and this hook must run only once for it.
    unsafe extern "C" fn run_echo_teardown(_el: el_t, data: data_t) {
        unsafe {
            Box::from_raw(data.ptr.cast::<Teardown>()).release();
            el_unloop();
        }
    }

    /// How long a test may drive the event loop before the watchdog calls
    /// it stuck.
    ///
    /// Well above the slowest test rather than close to it: what this has
    /// to tell apart is a slow run from a loop that will never return.
    /// Turning a passing test into an abort would cost more than the minutes
    /// a deadlock wastes.
    const WATCHDOG_SECS: u64 = 300;

    /// Guard of a watchdog: dropping it disarms the watchdog and waits for
    /// its thread.
    struct Watchdog {
        disarm: Option<Sender<()>>,
        thread: Option<JoinHandle<()>>,
    }

    impl Drop for Watchdog {
        fn drop(&mut self) {
            // Dropping the sender wakes the thread at once.
            drop(self.disarm.take());

            if let Some(thread) = self.thread.take() {
                // Joining is what keeps a passing test from leaving a thread
                // behind for the next one to run alongside.
                let _unused = thread.join();
            }
        }
    }

    /// Aborts the test binary if the event loop has not returned after
    /// `WATCHDOG_SECS`, rather than hanging forever.
    ///
    /// A deadlock inside the event loop freezes the loop itself, so an `el`
    /// timer would never fire: only an OS thread can notice. And since every
    /// `#[c_event_loop_test]` shares one global mutex, a hung test blocks all
    /// the others with no output at all, so aborting loudly is strictly
    /// better than waiting for the outer harness to time out.
    ///
    /// That argument holds for every test driving the loop, so each one
    /// binds a guard rather than picking. The guard disarms on drop, which
    /// a failing assertion reaches just as well as a passing one.
    #[must_use = "the watchdog is disarmed as soon as its guard is dropped"]
    fn watchdog() -> Watchdog {
        let (disarm, armed) = channel::<()>();

        let thread = spawn(move || {
            if let Err(RecvTimeoutError::Timeout) =
                armed.recv_timeout(Duration::from_secs(WATCHDOG_SECS))
            {
                eprintln!("watchdog: the C event loop is stuck, aborting");
                abort();
            }
        });

        Watchdog {
            disarm: Some(disarm),
            thread: Some(thread),
        }
    }

    fn run_echo(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut qm_ic_cbs_t,
    ) -> Result<i32, String> {
        let td = Teardown {
            ics: ics.iter().map(|&ic| SendPtr(ic)).collect(),
            cbs: SendPtr(cbs),
            blocker: SendPtr(unsafe { el_blocker_register() }),
        };
        let client = SendPtr(client);

        let outcome = Arc::new(Mutex::new(None::<Result<i32, String>>));
        let outcome_t = Arc::clone(&outcome);

        tokio_c_mod::spawn(async move {
            // Force capture of the whole `Send` wrappers.
            let (c, td) = (client, td);

            let args = tstiop_rpc__test__echo_args__t { i: 1 };
            let result = ICQuery::<EchoRpc>::builder(c.0)
                .hdr(echo_hdr())
                .build(args)
                .run()
                .await;
            *outcome_t.lock().expect("poisoned") = Some(match result {
                Ok(res) => Ok(res.i),
                Err(IcError::Status(s)) => Err(format!("query failed with status {s:?}")),
                Err(IcError::Exn(_)) => Err("query returned an exception".to_owned()),
            });

            main_c_queue_schedule(move || unsafe { td.release() });
        });

        unsafe {
            el_loop();
        }

        let result = outcome
            .lock()
            .map_err(|_unused| "outcome mutex poisoned".to_owned())?
            .take()
            .ok_or_else(|| "query never completed".to_owned())?;

        check_hdr()?;
        result
    }

    fn run_echo_on_main_c_thread(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut qm_ic_cbs_t,
    ) -> Result<i32, String> {
        let blocker = unsafe { el_blocker_register() };

        // Same ownership rules as `run_echo`: `cbs` points into the caller's
        // frame, kept alive by the synchronous `el_loop` below, and the
        // ichannels live until the deferred teardown deletes them. Unlike
        // `run_echo`, the future is polled on the main C thread, so the
        // completion closure runs from the very callback that delivered the
        // reply: the teardown has to be deferred to the next event loop
        // iteration rather than done on the spot.
        let client = SendPtr(client);
        let bl = SendPtr(blocker);
        let cbs = SendPtr(cbs);
        let to_delete: Vec<SendPtr<ichannel_t>> = ics.iter().map(|&ic| SendPtr(ic)).collect();

        let outcome = Arc::new(Mutex::new(None::<Result<i32, String>>));
        let outcome_c = Arc::clone(&outcome);

        let future = async move {
            // Force capture of the whole `Send` wrapper.
            let c = client;

            let args = tstiop_rpc__test__echo_args__t { i: 1 };
            ICQuery::<EchoRpc>::builder(c.0)
                .hdr(echo_hdr())
                .build(args)
                .run()
                .await
        };

        run_future(future, move |result| {
            // Force capture of the whole `Send` wrappers.
            let (bl, cbs, to_del) = (bl, cbs, to_delete);

            *outcome_c.lock().expect("poisoned") = Some(match result {
                Ok(res) => Ok(res.i),
                Err(IcError::Status(s)) => Err(format!("query failed with status {s:?}")),
                Err(IcError::Exn(_)) => Err("query returned an exception".to_owned()),
            });

            // Defer the teardown to the next event loop iteration: this
            // closure runs from the callback that delivered the reply, and
            // deleting a channel from one of its own callbacks trips the
            // assertion in `ic_wipe()`. `main_c_queue_schedule` would not
            // help here: on the main C thread it runs its callbacks inline.
            //
            // The box is handed to the timer, which owns it from here on.
            // Were `el_loop()` to return before the timer fires, the box
            // and the ichannels it names would leak; that only happens if
            // something else already unregistered the blocker, which is
            // what `run_echo_teardown` itself does.
            let td = Box::into_raw(Box::new(Teardown {
                ics: to_del,
                cbs,
                blocker: bl,
            }));

            unsafe {
                el_timer_register(
                    // `next` and `repeat`, both in milliseconds: fire at
                    // the top of the next loop iteration, once.
                    0,
                    0,
                    ev_timer_flags_t::EL_TIMER_NOMISS,
                    Some(run_echo_teardown),
                    td.cast(),
                );
            }
        });

        unsafe {
            el_loop();
        }

        let result = outcome
            .lock()
            .map_err(|_unused| "outcome mutex poisoned".to_owned())?
            .take()
            .ok_or_else(|| "query never completed".to_owned())?;

        check_hdr()?;
        result
    }

    fn run_local(is_async: bool) -> (Env, *mut ichannel_t, qm_ic_cbs_t) {
        let mut env = Env::new();
        env.register_packages(&[&raw const tstiop_rpc__pkg]);
        let ic = unsafe { ic_new() };
        unsafe {
            (*ic).set_no_autodel(true);
            (*ic).iop_env = env.as_ptr();
            (*ic).on_event = Some(dummy_on_event);
        }
        let mut cbs: qm_ic_cbs_t = unsafe { zeroed() };

        unsafe {
            z_ic_cbs_init(&raw mut cbs);
            z_ic_register_echo(&raw mut cbs, Some(echo_impl));
            ic_set_local(ic, is_async);
        };
        (env, ic, cbs)
    }

    /// Two connected channels over a seqpacket socket pair.
    fn socketpair_ics() -> (Env, *mut ichannel_t, *mut ichannel_t) {
        let mut env = Env::new();
        env.register_packages(&[&raw const tstiop_rpc__pkg]);
        let (ic1, ic2) = unsafe { (ic_new(), ic_new()) };
        for ic in [ic1, ic2] {
            unsafe {
                (*ic).set_no_autodel(true);
                (*ic).iop_env = env.as_ptr();
                (*ic).on_event = Some(dummy_on_event);
            }
        }

        let mut sv = [0i32; 2];
        assert!(
            unsafe {
                socketpairx(
                    AF_UNIX as i32,
                    SOCK_SEQPACKET as i32,
                    0,
                    O_NONBLOCK as i32,
                    sv.as_mut_ptr(),
                )
            } >= 0
        );
        unsafe {
            ic_spawn(ic1, sv[0], None);
            ic_spawn(ic2, sv[1], None);
        }
        assert!(unsafe { (*ic1).is_connected() && (*ic2).is_connected() });
        (env, ic1, ic2)
    }

    // }}}
    // {{{ Tests

    #[c_event_loop_test]
    fn ic_query_local_sync() {
        let _watchdog = watchdog();

        module_require(tokio_get_module());
        module_require(unsafe { ic_get_module() });
        let (_env, ic, mut cbs) = run_local(false);
        unsafe {
            (*ic).impl_ = &raw const cbs;
        }

        let outcome = run_echo(ic, &[ic], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(tokio_get_module());

        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    #[c_event_loop_test]
    fn ic_query_local_sync_on_main_c_thread() {
        let _watchdog = watchdog();

        module_require(unsafe { thr_get_module() });
        module_require(unsafe { ic_get_module() });
        let (_env, ic, mut cbs) = run_local(false);
        unsafe {
            (*ic).impl_ = &raw const cbs;
        }

        let outcome = run_echo_on_main_c_thread(ic, &[ic], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(unsafe { thr_get_module() });
        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    #[c_event_loop_test]
    fn ic_query_local_async() {
        let _watchdog = watchdog();

        module_require(tokio_get_module());
        module_require(unsafe { ic_get_module() });
        let (_env, ic, mut cbs) = run_local(true);
        unsafe {
            (*ic).impl_ = &raw const cbs;
        }

        let outcome = run_echo(ic, &[ic], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(tokio_get_module());

        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    #[c_event_loop_test]
    fn ic_query_over_socketpair() {
        let _watchdog = watchdog();

        module_require(tokio_get_module());
        // `ic_new()` asserts on `_G.ics` being initialized, which only happens
        // once the `ic` module is loaded.
        module_require(unsafe { ic_get_module() });
        let (_env, ic1, ic2) = socketpair_ics();
        let mut cbs: qm_ic_cbs_t = unsafe { zeroed() };

        unsafe {
            z_ic_cbs_init(&raw mut cbs);
            z_ic_register_echo(&raw mut cbs, Some(echo_impl));
            (*ic2).impl_ = &raw const cbs;
        }

        let outcome = run_echo(ic1, &[ic1, ic2], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(tokio_get_module());

        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    /// Same as `ic_query_over_socketpair`, but polled on the main C thread.
    ///
    /// This is the closest thing to a real client: the reply comes back from
    /// the socket, through `ic_read_process_answer()`, so the whole wake and
    /// re-poll happens inside an `IChannel` callback.
    #[c_event_loop_test]
    fn ic_query_over_socketpair_on_main_c_thread() {
        let _watchdog = watchdog();

        module_require(unsafe { thr_get_module() });
        // `ic_new()` asserts on `_G.ics` being initialized, which only happens
        // once the `ic` module is loaded.
        module_require(unsafe { ic_get_module() });
        let (_env, ic1, ic2) = socketpair_ics();
        let mut cbs: qm_ic_cbs_t = unsafe { zeroed() };

        unsafe {
            z_ic_cbs_init(&raw mut cbs);
            z_ic_register_echo(&raw mut cbs, Some(echo_impl));
            (*ic2).impl_ = &raw const cbs;
        }

        let outcome = run_echo_on_main_c_thread(ic1, &[ic1, ic2], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(unsafe { thr_get_module() });

        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    // }}}
}
