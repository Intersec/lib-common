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

use crate::{
    bindings::{__ic_msg_build, ic__ic_status__t, ic_is_local, iop_rpc_t},
    ic__ic_status__t::{IC_MSG_EXN, IC_MSG_OK},
};
use libcommon::iop::{IopDup, Owned, OwnedIop as _};
use libcommon::run_callback::{Callback, CallbackFut, run_callback};
use std::ffi::c_void;
use std::ptr::{read, write};

use bindings::{__ic_query, ic__hdr__t, ic_msg_new, ic_msg_t, ichannel_t};

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use libcommon::bindings::*;
    waf_cargo_build::include_bindings!();
}

// {{{ ICQuery builder

pub struct ICQuery<A: IopDup> {
    pub ic: *mut ichannel_t,
    pub rpc: *const iop_rpc_t,
    pub hdr: *const ic__hdr__t,
    pub args: Owned<A>,
    pub cmd: i32,
}

unsafe impl<A: IopDup> Send for ICQuery<A> {}

// }}}
// {{{ Error Handler

pub enum IcError<E> {
    Exn(E),
    Status(ic__ic_status__t),
}

// }}}
// {{{ Macro equivalent

unsafe extern "C" fn cb_func<R, E>(
    _ic: *mut ichannel_t,
    msg_p: *mut ic_msg_t,
    status: ic__ic_status__t,
    res: *mut c_void,
    exn: *mut c_void,
) where
    R: IopDup + 'static,
    E: IopDup + 'static,
{
    // `ic` is unused: everything we need is available on `msg`/`status`/`res`/`exn`

    let msg = unsafe { &mut *msg_p };

    let ptr = (&raw mut msg.priv_) as *mut Callback<Result<Owned<R>, IcError<Owned<E>>>>;
    debug_assert!(ptr.is_aligned(), "ic_msg_t::priv_ is not properly aligned");
    let cb = unsafe { read(ptr) };
    // The RPC descriptors are what let the generic (`GenericStructUnion`) `R`/`E`
    // rebuild an owned value from the raw blob; the compiled path ignores them.
    let rpc = unsafe { &*msg.rpc };
    if status == IC_MSG_OK {
        debug_assert!(!res.is_null(), "IC_MSG_OK with null result");
        let result = unsafe { R::dup_from_raw(rpc.result, res) };
        cb.call(Ok(result));
    } else if status == IC_MSG_EXN {
        debug_assert!(!exn.is_null(), "IC_MSG_EXN with null exn");
        let result = unsafe { E::dup_from_raw(rpc.exn, exn) };
        cb.call(Err(IcError::Exn(result)));
    } else {
        cb.call(Err(IcError::Status(status)));
    }
}

fn ic_prepare_msg<R, E>(
    msg: &mut ic_msg_t,
    rpc: *const iop_rpc_t,
    hdr: *const ic__hdr__t,
    cb: Callback<Result<Owned<R>, IcError<Owned<E>>>>,
    cmd: i32,
) where
    R: IopDup + 'static,
    E: IopDup + 'static,
{
    let ptr = (&raw mut msg.priv_) as *mut Callback<Result<Owned<R>, IcError<Owned<E>>>>;
    debug_assert!(ptr.is_aligned(), "ic_msg_t::priv_ is not properly aligned");
    unsafe {
        write(ptr, cb);
    }
    msg.cb = Some(cb_func::<R, E>);
    msg.rpc = rpc;
    msg.set_async(unsafe { (*rpc).async_() } != 0);
    msg.cmd = cmd;
    msg.hdr = hdr;
    msg.set_trace(false);
}

// }}}
// {{{ Rust IChannel query

fn call_ic_query<A, R, E>(cb: Callback<Result<Owned<R>, IcError<Owned<E>>>>, ic_query: &ICQuery<A>)
where
    A: IopDup,
    R: IopDup + 'static,
    E: IopDup + 'static,
{
    let msg = unsafe { &mut *ic_msg_new(size_of_val(&cb) as i32) };
    // `Owned<R>` is an associated-type projection, which is not injective, so
    // the generic args can't be inferred back from `cb`: name them explicitly.
    ic_prepare_msg::<R, E>(msg, ic_query.rpc, ic_query.hdr, cb, ic_query.cmd);
    let args = unsafe { (*msg.rpc).args };
    let do_bpack = !unsafe { ic_is_local(ic_query.ic) } || msg.force_pack();
    unsafe {
        __ic_msg_build(msg, args, ic_query.args.as_raw(), do_bpack);
    }
    unsafe {
        __ic_query(ic_query.ic, msg);
    }
}

pub fn ic_query<A, R, E>(ic_query: ICQuery<A>) -> CallbackFut<Result<Owned<R>, IcError<Owned<E>>>>
where
    A: IopDup + 'static,
    R: IopDup + 'static,
    E: IopDup + 'static,
{
    run_callback(move |cb: Callback<Result<Owned<R>, IcError<Owned<E>>>>| {
        call_ic_query::<A, R, E>(cb, &ic_query);
    })
}

// }}}
