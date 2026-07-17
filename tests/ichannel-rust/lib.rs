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
//! This crate exercises the `ichannel` crate's `ic_query` end-to-end over a
//! real socket. It is here and not directly in `rust/ichannel/ichannel.rs` so
//! that it can use the RPC defined in `tests/iop/tstiop_rpc.iop` and the
//! test-only C shims declared in `wrapper.h`.

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use ichannel::bindings::*;
    waf_cargo_build::include_bindings!();
}

#[cfg(test)]
mod tests {
    use crate::bindings::{
        __ic_reply, __socket_type::SOCK_SEQPACKET, AF_UNIX, O_NONBLOCK, ic__hdr__t,
        ic__ic_status__t, ic_delete, ic_event_t, ic_get_module, ic_new, ic_spawn, ichannel_t,
        qm_ic_cbs_t, socketpairx, tstiop_rpc__pkg, tstiop_rpc__test__echo_args__t,
        tstiop_rpc__test__echo_exn__t, tstiop_rpc__test__echo_res__t, z_echo_cmd, z_echo_rpc,
        z_ic_cbs_init, z_ic_cbs_wipe, z_ic_register_echo,
    };
    use ichannel::{ICQuery, IcError, ic_query};
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

        let blocker = unsafe { el_blocker_register() };
        let (c1, c2, bl) = (SendPtr(ic1), SendPtr(ic2), SendPtr(blocker));
        // `cbs` stays pinned on this stack frame: `ic2` holds `&cbs` in `impl_`,
        // so it must not move, and `el_loop()` below keeps the frame alive
        // until the cleanup callback runs. Only a raw pointer to it crosses to
        // the tokio worker thread.
        let cbs_ptr = SendPtr(&raw mut cbs);

        let outcome = Arc::new(Mutex::new(None::<Result<i32, String>>));
        let outcome_task = Arc::clone(&outcome);

        tokio_c_mod::spawn(async move {
            // Force capture of the whole `Send` wrappers
            let (c1, c2, bl, cbs_ptr) = (c1, c2, bl, cbs_ptr);

            let args = tstiop_rpc__test__echo_args__t { i: 1 };
            let icq = ICQuery {
                ic: c1.0,
                rpc: unsafe { z_echo_rpc() },
                hdr: ptr::null(),
                args: args.dup(),
                cmd: unsafe { z_echo_cmd() },
            };
            // `echo` declares no exception, so its exn type is the IOP `void`.
            let result = ic_query::<
                tstiop_rpc__test__echo_args__t,
                tstiop_rpc__test__echo_res__t,
                tstiop_rpc__test__echo_exn__t,
            >(icq)
            .await;
            *outcome_task.lock().expect("poisoned") = Some(match result {
                Ok(res) => Ok(res.i),
                Err(IcError::Status(s)) => Err(format!("query failed with status {s:?}")),
                Err(IcError::Exn(_)) => Err("query returned an exception".to_owned()),
            });

            // Must run on the loop thread so el_loop() can return
            main_c_queue_schedule(move || unsafe {
                // Same here: capture the whole wrappers, not their pointers.
                let (c1, c2, bl, cbs_ptr) = (c1, c2, bl, cbs_ptr);
                let (mut a, mut b, mut bl) = (c1.0, c2.0, bl.0);
                ic_delete(&raw mut a);
                ic_delete(&raw mut b);
                z_ic_cbs_wipe(cbs_ptr.0);
                el_unregister(&raw mut bl);
            });
        });

        unsafe {
            el_loop();
        }

        module_release(unsafe { ic_get_module() });
        module_release(tokio_get_module());

        match outcome.lock().expect("poisoned").take() {
            Some(Ok(i)) => assert_eq!(i, 1),
            Some(Err(e)) => panic!("{e}"),
            None => panic!("query never completed"),
        }
    }
}
