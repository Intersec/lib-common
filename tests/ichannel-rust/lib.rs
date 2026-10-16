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
//! This crate exercises the queries and the implementations of the
//! `ichannel` crate end-to-end, both over a real socket and over a local
//! (in-process, loopback) `IChannel`. It is here and not directly in
//! `rust/ichannel/ichannel.rs` so that it can use the RPCs defined in
//! `tests/iop/tstiop_rpc.iop`.

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use ichannel::bindings::*;
    waf_cargo_build::include_bindings!();
}

#[cfg(test)]
mod tests {
    use crate::bindings::{
        __socket_type::SOCK_SEQPACKET, AF_UNIX, O_NONBLOCK, ev_priority_t, ic__hdr__t_raw,
        ic__hdr__u, ic__ic_status__e, ic__simple_hdr__t, ic_delete, ic_event_t, ic_get_module,
        ic_new, ic_set_local, ic_spawn, ichannel_t, iop_mod_find_rpc, qm_ic_cbs_t, socketpairx,
        tstiop_rpc__pkg, tstiop_rpc__rpc__test__echo__rpc as EchoRpc,
        tstiop_rpc__rpc__test__echo_or_throw__rpc as EchoOrThrowRpc,
        tstiop_rpc__test__echo_args__t, tstiop_rpc__test__echo_or_throw_args__t,
        tstiop_rpc__test__echo_or_throw_exn__t, tstiop_rpc__test__echo_or_throw_res__t,
        tstiop_rpc__test__echo_res__t, z_ic_cbs_init, z_ic_cbs_wipe,
    };
    use ichannel::{ICQuery, IcError, IcImpls, IcReply, IcReplySlot, IopRPCImpl};
    use libcommon::bindings::thr_get_module;
    use libcommon::c_main_waker::run_future;
    use libcommon::iop::{Env, IopIface, IopModule as _, IopRPC};
    use libcommon::iop_new;
    use libcommon::qhashmap::QMap;
    use libcommon_core::lstr;
    use libcommon_core::{
        bindings::{
            data_t, el_blocker_register, el_loop, el_loop_timeout, el_t, el_timer_register,
            el_unloop, el_unregister, ev_t, ev_timer_flags_t,
        },
        c_event_loop_test,
        module::{module_release, module_require},
        thr::main_c_queue_schedule,
    };
    use std::mem::zeroed;
    use std::process::abort;
    use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
    use std::sync::mpsc::{RecvTimeoutError, Sender, channel};
    use std::sync::{Arc, Mutex};
    use std::thread::{JoinHandle, spawn};
    use std::time::Duration;

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
    impl IopRPCImpl for EchoRpc {
        fn handle(arg: &Self::Arg<'_>, hdr: Option<&ic__hdr__t_raw>, reply: IcReplySlot<Self>) {
            record_hdr(hdr);
            reply.reply_res(&tstiop_rpc__test__echo_res__t { i: arg.i });
        }
    }

    /// The positive arguments of `echoOrThrow`, in the order the server got
    /// them.
    static SERVED_ORDER: Mutex<Vec<i32>> = Mutex::new(Vec::new());

    /// Replies to the slot that `echoOrThrow` kept, once its callback has
    /// returned.
    ///
    /// # Safety
    ///
    /// `data.ptr` must be the slot leaked by `echoOrThrow`, and this hook
    /// must run only once for it.
    unsafe extern "C" fn reply_deferred(_el: el_t, data: data_t) {
        let reply = unsafe { Box::from_raw(data.ptr.cast::<IcReplySlot<EchoOrThrowRpc>>()) };

        assert!(reply.channel().is_none(), "the callback has returned");
        reply.reply_res(&tstiop_rpc__test__echo_or_throw_res__t { i: -3 });
    }

    /// One branch per way a slot can be given up, picked by the argument, so
    /// that a test names the path it wants by the value it sends. The exits
    /// are the whole point of `IcReplySlot`: each is reachable, and exactly
    /// one is taken.
    impl IopRPCImpl for EchoOrThrowRpc {
        fn handle(arg: &Self::Arg<'_>, _hdr: Option<&ic__hdr__t_raw>, reply: IcReplySlot<Self>) {
            match arg.i {
                i if i > 0 => {
                    SERVED_ORDER.lock().expect("poisoned").push(i);
                    reply.reply_res(&tstiop_rpc__test__echo_or_throw_res__t { i });
                }
                0 => reply.reply_err(ic__ic_status__e::SERVER_ERROR),
                -1 => reply.reply_exn(&tstiop_rpc__test__echo_or_throw_exn__t {
                    desc: lstr::from_str("i is -1"),
                }),
                // Reply after the callback, from a one-shot timer that owns
                // the slot.
                -3 => unsafe {
                    el_timer_register(
                        0,
                        0,
                        ev_timer_flags_t::EL_TIMER_NOMISS,
                        Some(reply_deferred),
                        Box::into_raw(Box::new(reply)).cast(),
                    );
                },
                // Answering never comes: the query can only end on its
                // timeout, which is what an unanswered slot looks like from
                // the other end.
                _ => reply.discard(),
            }
        }
    }

    struct SendPtr<T>(*mut T);
    unsafe impl<T> Send for SendPtr<T> {}

    // }}}
    // {{{ Helper

    /// What a test releases once its query has completed.
    struct Teardown {
        ics: Vec<SendPtr<ichannel_t>>,
        cbs: SendPtr<IcImpls<'static>>,
        blocker: SendPtr<ev_t>,
    }

    impl Teardown {
        /// Registers a blocker, which keeps `el_loop()` running until the
        /// release.
        fn new(ics: &[*mut ichannel_t], cbs: *mut IcImpls<'_>) -> Self {
            Self {
                ics: ics.iter().map(|&ic| SendPtr(ic)).collect(),
                cbs: SendPtr(cbs.cast()),
                blocker: SendPtr(unsafe { el_blocker_register() }),
            }
        }

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
                (*self.cbs.0).wipe();
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

    /// Drives `query` on the tokio runtime, and the event loop until the
    /// query has completed. Then releases `ics` and `cbs` from the main C
    /// thread.
    ///
    /// Returns `None` if the event loop returned before the query
    /// completed.
    fn run_on_tokio<T: Send + 'static>(
        ics: &[*mut ichannel_t],
        cbs: *mut IcImpls<'_>,
        query: impl Future<Output = T> + Send + 'static,
    ) -> Option<T> {
        let td = Teardown::new(ics, cbs);
        let outcome = Arc::new(Mutex::new(None));
        let outcome_t = Arc::clone(&outcome);

        tokio_c_mod::spawn(async move {
            let result = query.await;

            *outcome_t.lock().expect("poisoned") = Some(result);
            main_c_queue_schedule(move || unsafe { td.release() });
        });

        unsafe {
            el_loop();
        }

        outcome.lock().expect("outcome mutex poisoned").take()
    }

    fn run_echo(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut IcImpls<'_>,
    ) -> Result<i32, String> {
        let result = run_on_tokio(ics, cbs, echo_query(SendPtr(client)))
            .ok_or_else(|| "query never completed".to_owned())?;

        check_hdr()?;
        echo_result(result)
    }

    /// Sends an `echo` query with its header on `client`, from the thread
    /// that first polls the future.
    async fn echo_query(client: SendPtr<ichannel_t>) -> IcReply<EchoRpc> {
        let args = tstiop_rpc__test__echo_args__t { i: 1 };

        ICQuery::<EchoRpc>::builder(client.0)
            .hdr(echo_hdr())
            .build(args)
            .run()
            .await
    }

    /// The reply of an `echo` query, or what went wrong.
    fn echo_result(result: IcReply<EchoRpc>) -> Result<i32, String> {
        match result {
            Ok(res) => Ok(res.i),
            Err(IcError::Status(s)) => Err(format!("query failed with status {s:?}")),
            Err(IcError::Exn(_)) => Err("query returned an exception".to_owned()),
        }
    }

    fn run_echo_on_main_c_thread(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut IcImpls<'_>,
    ) -> Result<i32, String> {
        // Same ownership rules as `run_echo`: `cbs` points into the caller's
        // frame, kept alive by the synchronous `el_loop` below, and the
        // ichannels live until the deferred teardown deletes them.
        let td = Teardown::new(ics, cbs);
        let outcome = Arc::new(Mutex::new(None));
        let outcome_c = Arc::clone(&outcome);

        run_future(echo_query(SendPtr(client)), move |result| {
            *outcome_c.lock().expect("poisoned") = Some(result);

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
            let td = Box::into_raw(Box::new(td));

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
        echo_result(result)
    }

    /// What a query ended as, so a test can name the reply path it expects.
    #[derive(Debug, PartialEq, Eq)]
    enum Outcome {
        Res(i32),
        Exn(String),
        Status(ic__ic_status__e),
    }

    /// What an `echoOrThrow` query ended as.
    fn echo_or_throw_outcome(result: IcReply<EchoOrThrowRpc>) -> Outcome {
        match result {
            Ok(res) => Outcome::Res(res.i),
            Err(IcError::Exn(exn)) => Outcome::Exn(exn.desc.to_string()),
            Err(IcError::Status(s)) => Outcome::Status(s),
        }
    }

    /// Sends one `echoOrThrow` carrying `i`, and reports which way the
    /// implementation gave the slot up.
    ///
    /// The timeout is what makes the discarding branch observable: without
    /// it the query would wait for as long as the channel lives.
    fn run_echo_or_throw(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut IcImpls<'_>,
        i: i32,
    ) -> Outcome {
        let client = SendPtr(client);
        let result = run_on_tokio(ics, cbs, async move {
            // Force capture of the whole `Send` wrapper.
            let c = client;

            let args = tstiop_rpc__test__echo_or_throw_args__t { i };
            ICQuery::<EchoOrThrowRpc>::builder(c.0)
                .timeout(QUERY_TIMEOUT_MS)
                .priority(ev_priority_t::EV_PRIORITY_HIGH)
                .build(args)
                .run()
                .await
        })
        .expect("query never completed");

        echo_or_throw_outcome(result)
    }

    /// Long enough that a local reply always wins the race, short enough
    /// that the discarding branch does not stall the suite.
    const QUERY_TIMEOUT_MS: u32 = 500;

    /// The environment of the test RPCs.
    fn test_env() -> Env {
        let mut env = Env::new();

        env.register_packages(&[&raw const tstiop_rpc__pkg]);
        env
    }

    /// A channel that the test deletes, with the types of `env`.
    fn new_ic(env: &Env) -> *mut ichannel_t {
        let ic = unsafe { ic_new() };

        unsafe {
            (*ic).set_no_autodel(true);
            (*ic).iop_env = env.as_ptr();
            (*ic).on_event = Some(dummy_on_event);
        }
        ic
    }

    fn run_local(is_async: bool) -> (Env, *mut ichannel_t, IcImpls<'static>) {
        let env = test_env();
        let ic = new_ic(&env);
        let mut cbs = IcImpls::new();

        cbs.register::<EchoRpc>();
        cbs.register::<EchoOrThrowRpc>();
        unsafe {
            // The table is boxed, so the pointer stays valid when `cbs`
            // moves out.
            (*ic).impl_ = cbs.as_ptr();
            ic_set_local(ic, is_async);
        }
        (env, ic, cbs)
    }

    /// Runs iterations of the event loop until `done` returns true.
    fn loop_until(mut done: impl FnMut() -> bool) {
        while !done() {
            unsafe {
                el_loop_timeout(10);
            }
        }
    }

    /// Two connected channels over a seqpacket socket pair.
    fn socketpair_ics() -> (Env, *mut ichannel_t, *mut ichannel_t) {
        let env = test_env();
        let (ic1, ic2) = (new_ic(&env), new_ic(&env));

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

    /// Two connected channels over a seqpacket socket pair, the second one
    /// serving the implementations of `register`.
    fn served_socketpair(
        register: fn(&mut IcImpls<'static>),
    ) -> (Env, *mut ichannel_t, *mut ichannel_t, IcImpls<'static>) {
        let (env, ic1, ic2) = socketpair_ics();
        let mut cbs = IcImpls::new();

        register(&mut cbs);
        unsafe {
            (*ic2).impl_ = cbs.as_ptr();
        }
        (env, ic1, ic2, cbs)
    }

    /// Deletes the channels of [`served_socketpair`], then their table.
    fn release_socketpair(ics: [*mut ichannel_t; 2], mut cbs: IcImpls<'static>) {
        for mut ic in ics {
            unsafe {
                ic_delete(&raw mut ic);
            }
        }
        cbs.wipe();
    }

    /// Serves `echo` over a socket pair, from a table that C owns, as in a C
    /// product, and sends one query with `runner`.
    fn echo_over_socketpair(
        runner: fn(*mut ichannel_t, &[*mut ichannel_t], *mut IcImpls<'_>) -> Result<i32, String>,
    ) -> Result<i32, String> {
        let (_env, ic1, ic2) = socketpair_ics();
        let mut c_cbs: qm_ic_cbs_t = unsafe { zeroed() };

        unsafe {
            z_ic_cbs_init(&raw mut c_cbs);
        }
        let mut cbs = IcImpls::borrow(unsafe { QMap::from_c_ptr_mut(&raw mut c_cbs) });

        cbs.register::<EchoRpc>();
        unsafe {
            (*ic2).impl_ = cbs.as_ptr();
        }

        let outcome = runner(ic1, &[ic1, ic2], &raw mut cbs);

        unsafe {
            z_ic_cbs_wipe(&raw mut c_cbs);
        }
        outcome
    }

    // }}}
    // {{{ Tests

    #[c_event_loop_test]
    fn ic_query_local_sync() {
        let _watchdog = watchdog();

        module_require(tokio_get_module());
        module_require(unsafe { ic_get_module() });
        let (_env, ic, mut cbs) = run_local(false);

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

        let outcome = run_echo_on_main_c_thread(ic, &[ic], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(unsafe { thr_get_module() });
        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    /// Each way an implementation can give up a reply slot, and what the
    /// caller sees for it, a reply after the callback included.
    #[c_event_loop_test]
    fn ic_query_reply_paths() {
        let _watchdog = watchdog();

        for (i, expected) in [
            (1, Outcome::Res(1)),
            (0, Outcome::Status(ic__ic_status__e::SERVER_ERROR)),
            (-1, Outcome::Exn("i is -1".to_owned())),
            (-2, Outcome::Status(ic__ic_status__e::TIMEDOUT)),
            (-3, Outcome::Res(-3)),
        ] {
            module_require(tokio_get_module());
            module_require(unsafe { ic_get_module() });
            let (_env, ic, mut cbs) = run_local(false);

            let outcome = run_echo_or_throw(ic, &[ic], &raw mut cbs, i);

            module_release(unsafe { ic_get_module() });
            module_release(tokio_get_module());

            assert_eq!(outcome, expected, "for i = {i}");
        }
    }

    #[c_event_loop_test]
    fn ic_query_local_async() {
        let _watchdog = watchdog();

        module_require(tokio_get_module());
        module_require(unsafe { ic_get_module() });
        let (_env, ic, mut cbs) = run_local(true);

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
        let outcome = echo_over_socketpair(run_echo);

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
        let outcome = echo_over_socketpair(run_echo_on_main_c_thread);

        module_release(unsafe { ic_get_module() });
        module_release(unsafe { thr_get_module() });

        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    /// A query of a higher priority overtakes the queries that wait in the
    /// send queue of its channel.
    #[c_event_loop_test]
    fn a_high_priority_query_goes_out_first() {
        let _watchdog = watchdog();

        module_require(unsafe { thr_get_module() });
        module_require(unsafe { ic_get_module() });
        let (_env, ic1, ic2, cbs) = served_socketpair(IcImpls::register::<EchoOrThrowRpc>);
        SERVED_ORDER.lock().expect("poisoned").clear();

        // On the main C thread, both queries wait in the send queue until
        // the event loop runs.
        let [low, high] = [
            (1, ev_priority_t::EV_PRIORITY_LOW),
            (2, ev_priority_t::EV_PRIORITY_HIGH),
        ]
        .map(|(i, priority)| {
            ICQuery::<EchoOrThrowRpc>::builder(ic1)
                .priority(priority)
                .build(tstiop_rpc__test__echo_or_throw_args__t { i })
                .run()
        });
        let done = Arc::new(AtomicBool::new(false));
        let done_c = Arc::clone(&done);

        run_future(
            async move {
                let _unused = (low.await, high.await);
            },
            move |()| done_c.store(true, Ordering::Relaxed),
        );
        loop_until(|| done.load(Ordering::Relaxed));

        assert_eq!(*SERVED_ORDER.lock().expect("poisoned"), [2, 1]);

        release_socketpair([ic1, ic2], cbs);
        module_release(unsafe { ic_get_module() });
        module_release(unsafe { thr_get_module() });
    }

    // }}}
}
