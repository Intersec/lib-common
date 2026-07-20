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
        __ic_reply, __socket_type::SOCK_SEQPACKET, AF_UNIX, O_NONBLOCK, ic__hdr__t,
        ic__ic_status__t, ic_delete, ic_event_t, ic_get_module, ic_new, ic_set_local, ic_spawn,
        ichannel_t, qm_ic_cbs_t, socketpairx, tstiop_rpc__pkg, tstiop_rpc__test__echo_args__t,
        tstiop_rpc__test__echo_exn__t, tstiop_rpc__test__echo_res__t, z_echo_cmd, z_echo_rpc,
        z_ic_cbs_init, z_ic_cbs_wipe, z_ic_register_echo,
    };
    use ichannel::{ICQuery, IcError, ic_query};
    use libcommon::bindings::thr_get_module;
    use libcommon::c_main_waker::run_future;
    use libcommon::iop::{Env, IopDup as _};
    use libcommon_core::{
        bindings::{el_blocker_register, el_loop, el_unregister},
        c_event_loop_test,
        module::{module_release, module_require},
        thr::main_c_queue_schedule,
    };
    use std::mem::zeroed;
    use std::sync::{Arc, Mutex};
    use std::{ffi::c_void, ptr};

    use tokio_c_mod::tokio_get_module;

    // {{{ Setup

    unsafe extern "C" fn dummy_on_event(_ic: *mut ichannel_t, _evt: ic_event_t) {}

    // server-side echo
    unsafe extern "C" fn echo_impl(
        ic: *mut ichannel_t,
        slot: u64,
        arg: *mut tstiop_rpc__test__echo_args__t,
        _hdr: *const ic__hdr__t,
    ) {
        let res = tstiop_rpc__test__echo_res__t {
            i: unsafe { (*arg).i },
        };
        let rpc = unsafe { z_echo_rpc() };
        unsafe {
            __ic_reply(
                ic,
                slot,
                ic__ic_status__t::IC_MSG_OK as i32,
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

    fn run_echo(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut qm_ic_cbs_t,
    ) -> Result<i32, String> {
        let blocker = unsafe { el_blocker_register() };
        let client = SendPtr(client);
        let bl = SendPtr(blocker);
        let cbs = SendPtr(cbs);
        let to_delete: Vec<SendPtr<ichannel_t>> = ics.iter().map(|&ic| SendPtr(ic)).collect();

        let outcome = Arc::new(Mutex::new(None::<Result<i32, String>>));
        let outcome_t = Arc::clone(&outcome);

        tokio_c_mod::spawn(async move {
            // Force capture of the whole `Send` wrappers.
            let (c, bl, cbs, to_delete) = (client, bl, cbs, to_delete);

            let args = tstiop_rpc__test__echo_args__t { i: 1 };
            let icq = ICQuery {
                ic: c.0,
                rpc: unsafe { z_echo_rpc() },
                hdr: ptr::null(),
                args: args.dup(),
                cmd: unsafe { z_echo_cmd() },
            };
            // `echo` has no exception, so E is an unused placeholder.
            let result = ic_query::<
                tstiop_rpc__test__echo_args__t,
                tstiop_rpc__test__echo_res__t,
                tstiop_rpc__test__echo_exn__t,
            >(icq)
            .await;
            *outcome_t.lock().expect("poisoned") = Some(match result {
                Ok(res) => Ok(res.i),
                Err(IcError::Status(s)) => Err(format!("query failed with status {s:?}")),
                Err(IcError::Exn(_)) => Err("query returned an exception".to_owned()),
            });

            main_c_queue_schedule(move || unsafe {
                let (bl, cbs, mut to_del) = (bl, cbs, to_delete);
                for ic in &mut to_del {
                    ic_delete(&raw mut ic.0);
                }
                z_ic_cbs_wipe(cbs.0);
                let mut bl = bl.0;
                el_unregister(&raw mut bl);
            });
        });

        unsafe {
            el_loop();
        }

        outcome
            .lock()
            .map_err(|_unused| "outcome mutex poisoned".to_owned())?
            .take()
            .ok_or_else(|| "query never completed".to_owned())?
    }

    fn run_echo_on_main_c_thread(
        client: *mut ichannel_t,
        ics: &[*mut ichannel_t],
        cbs: *mut qm_ic_cbs_t,
    ) -> Result<i32, String> {
        let blocker = unsafe { el_blocker_register() };

        // Same ownership rules as `run_echo`: `cbs` points into the caller's
        // frame, kept alive by the synchronous `el_loop` below, and the
        // ichannels live until the completion closure deletes them. Unlike
        // `run_echo`, the future is polled on the main C thread, so `on_done`
        // can clean up directly instead of scheduling another callback.
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
            let icq = ICQuery {
                ic: c.0,
                rpc: unsafe { z_echo_rpc() },
                hdr: ptr::null(),
                args: args.dup(),
                cmd: unsafe { z_echo_cmd() },
            };
            // `echo` has no exception, so E is an unused placeholder.
            ic_query::<
                tstiop_rpc__test__echo_args__t,
                tstiop_rpc__test__echo_res__t,
                tstiop_rpc__test__echo_exn__t,
            >(icq)
            .await
        };

        run_future(future, move |result| {
            // Force capture of the whole `Send` wrappers.
            let (bl, cbs, mut to_del) = (bl, cbs, to_delete);

            *outcome_c.lock().expect("poisoned") = Some(match result {
                Ok(res) => Ok(res.i),
                Err(IcError::Status(s)) => Err(format!("query failed with status {s:?}")),
                Err(IcError::Exn(_)) => Err("query returned an exception".to_owned()),
            });

            unsafe {
                for ic in &mut to_del {
                    ic_delete(&raw mut ic.0);
                }
                z_ic_cbs_wipe(cbs.0);
                let mut blocker = bl.0;
                el_unregister(&raw mut blocker);
            }
        });

        unsafe {
            el_loop();
        }

        outcome
            .lock()
            .map_err(|_unused| "outcome mutex poisoned".to_owned())?
            .take()
            .ok_or_else(|| "query never completed".to_owned())?
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

    // }}}
    // {{{ Tests

    #[c_event_loop_test]
    fn ic_query_local_sync() {
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
        module_require(tokio_get_module());
        // `ic_new()` asserts on `_G.ics` being initialized, which only happens
        // once the `ic` module is loaded.
        module_require(unsafe { ic_get_module() });
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

        let mut cbs: qm_ic_cbs_t = unsafe { zeroed() };
        unsafe {
            z_ic_cbs_init(&raw mut cbs);
            z_ic_register_echo(&raw mut cbs, Some(echo_impl));
        }

        // connected seqpacket pair, then spawn both ends.
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
            (*ic2).impl_ = &raw const cbs;
        }
        assert!(unsafe { (*ic1).is_connected() && (*ic2).is_connected() });

        let outcome = run_echo(ic1, &[ic1, ic2], &raw mut cbs);

        module_release(unsafe { ic_get_module() });
        module_release(tokio_get_module());

        assert_eq!(outcome.unwrap_or_else(|e| panic!("{e}")), 1);
    }

    // }}}
}
