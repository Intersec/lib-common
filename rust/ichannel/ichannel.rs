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

//! Infrastructure to send an `IChannel` query from Rust code.
//!
//! This provides an [`ICQuery`] struct with the information needed for an
//! `IChannel` query.
//!
//! It also provides the [`ic_query`] function which takes the [`ICQuery`] as
//! input and launches it on an `IChannel`, sending back a [`CallbackFut`] with
//! either the result or the [`IcError`] resulting from the query.

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

/// A structure to hold message payload data and a target `IChannel`.
///
/// # Fields
///
/// - `ic` - the target [`ichannel_t`]
/// - `rpc` - the [`iop_rpc_t`] descriptor
/// - `hdr` - the optional query [`ic__hdr__t`] (null means none)
/// - `args` - an owned deep copy of the IOP arg struct, packed when the query is launched
/// - `cmd` - the RPC command id (`IOP_RPC_CMD`), stored in [`ic_msg_t`]'s `.cmd` field.
pub struct ICQuery<A: IopDup> {
    pub ic: *mut ichannel_t,
    pub rpc: *const iop_rpc_t,
    pub hdr: *const ic__hdr__t,
    pub args: Owned<A>,
    pub cmd: i32,
}

// SAFETY: `args` is an owned deep copy in the process-global libc pool and
// `Owned<A>` is `Send` by construction; what blocks the auto impl is the raw
// pointers. `ic`, `rpc` and `hdr` are only dereferenced on the main C event
// loop, when the query is launched, and the caller guarantees they stay
// valid until then. Transferring the `ICQuery` between threads is therefore
// safe.
unsafe impl<A: IopDup> Send for ICQuery<A> {}

// }}}
// {{{ Error Handler

/// Error returned by an `IChannel` query.
pub enum IcError<E> {
    /// A modeled IOP exception returned by the RPC (`IC_MSG_EXN`).
    Exn(E),
    /// A status failure with no modeled exception - e.g. timeout, canceled,
    /// or unimplemented RPC (any status other than OK/EXN).
    Status(ic__ic_status__t),
}

// }}}
// {{{ Macro equivalent

/// C completion callback: reads the Rust [`Callback`] from `msg.priv_` and
/// resolves it with the decoded reply ([`IC_MSG_OK`]), IOP exception
/// ([`IC_MSG_EXN`]), or transport status (anything else).
///
/// # Safety
///
/// Must only be installed as `msg.cb` by [`ic_prepare_msg`] and invoked by the
/// `IChannel` core, which must guarantee that:
/// - `msg_p` is a valid, non-null, properly aligned pointer to the `ic_msg_t`
/// - `msg.priv_` holds an initialized `Callback<Result<Owned<R>,
///   IcError<Owned<E>>>>` written by `ic_prepare_msg` with the *same* `R` and
///   `E`.
/// - `msg.rpc` is a valid `iop_rpc_t` whose `result` and `exn` descriptors
///   match `R` and `E` respectively.
/// - On [`IC_MSG_OK`], `res` points to a value described by `rpc.result`; on
///   [`IC_MSG_EXN`], `exn` points to a value described by `rpc.exn`.
///
/// # Panics
///
/// Debug-only :
/// Panics if `msg.priv_` is not properly aligned for the expected
/// `Callback<...>`, if `status` is [`IC_MSG_OK`] but `res` is null, or if
/// `status` is [`IC_MSG_EXN`] but `exn` is null. Each of these conditions
/// means the safety contract above was violated.
///
/// `_ic` is unused and imposes no requirement.
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

/// Writes and wires the elements needed to send a `msg` on an `IChannel` and
/// links a callback function.
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

/// Allocates a `msg`, decides whether to `do_bpack`, packs the args via
/// [`__ic_msg_build`], then calls [`__ic_query`] to send the query on the
/// `IChannel`.
fn call_ic_query<A, R, E>(cb: Callback<Result<Owned<R>, IcError<Owned<E>>>>, ic_query: &ICQuery<A>)
where
    A: IopDup + 'static,
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

/// `ic_query` allows the user to launch an `IChannel` query from Rust.
///
/// # Arguments
///
/// - `ic_query` - an [`ICQuery`] holding message payload and the target
///   `IChannel`
///
/// # Returns
///
/// a [`CallbackFut`] with either the result or the error produced.
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
