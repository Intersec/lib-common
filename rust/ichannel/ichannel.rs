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
//! An RPC is named by the marker type that `iopc` writes for it, which
//! implements [`IopRPC`](../libcommon/iop/trait.IopRPC.html): the RPC
//! descriptor, the command id and the three types of the RPC come from the
//! marker, so they cannot designate two different RPCs.
//!
//! [`ICQuery::builder`] sets up a query for an RPC on a channel, and
//! [`ICQueryBuilder::build`] locks it with its arguments. [`ICQuery::run`]
//! sends it, and returns a
//! [`CallbackFut`](../libcommon/run_callback/struct.CallbackFut.html)
//! resolving to either the result or an [`IcError`].

use crate::bindings::{__ic_msg_build, ic__ic_status__e, ic_is_local, ic_msg_set_timeout};
use libcommon::iop::{IopDup as _, IopRPC, Owned, OwnedGeneric, OwnedIop as _, StructUnion as _};
use libcommon::run_callback::{Callback, CallbackFut, run_callback, run_now};
use libcommon_core::thr::is_main_c_thread;

use std::ffi::c_void;
use std::marker::PhantomData;
use std::ptr::{self, read, write};

const IC_MSG_OK: ic__ic_status__e = ic__ic_status__e::OK;
const IC_MSG_EXN: ic__ic_status__e = ic__ic_status__e::EXN;

use bindings::{__ic_query, ic__hdr__t_raw, ic__hdr__u, ic_msg_new, ic_msg_t, ichannel_t};

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use libcommon::bindings::*;
    waf_cargo_build::include_bindings!();
}

// {{{ ICQuery builder

/// Settings of a query for the RPC `RPC`, before the query is built.
///
/// [`Self::build`] turns the settings and the arguments into an
/// [`ICQuery`], which has no setter: once built, a query cannot change.
///
/// ```ignore
/// ICQuery::<EchoRpc>::builder(ic).timeout(500).build(args).run().await
/// ```
#[must_use = "a query builder does nothing until it builds a query"]
pub struct ICQueryBuilder<'a, RPC: IopRPC> {
    ic: *mut ichannel_t,
    hdr: Option<ic__hdr__u<'a>>,
    timeout: u32,
    _rpc: PhantomData<RPC>,
}

impl<'a, RPC: IopRPC + 'static> ICQueryBuilder<'a, RPC> {
    /// Sends `hdr` with the query.
    pub fn hdr(mut self, hdr: ic__hdr__u<'a>) -> Self {
        self.hdr = Some(hdr);
        self
    }

    /// Fails the query with `IC_MSG_TIMEDOUT` if the `IChannel` has not
    /// processed it after `ms` milliseconds. Zero, the default, waits for as
    /// long as the channel lives.
    pub fn timeout(mut self, ms: u32) -> Self {
        self.timeout = ms;
        self
    }

    /// Builds the query with the arguments `arg`.
    pub fn build(self, arg: RPC::Arg<'a>) -> ICQuery<'a, RPC> {
        ICQuery {
            settings: self,
            args: arg,
        }
    }
}

/// Query for the RPC `RPC`, ready to be sent on its `IChannel`.
///
/// The descriptor and the command id are not stored: they come from `RPC`,
/// so they cannot designate another RPC than the type parameter does.
///
/// The query holds its arguments and its header, and their strings and
/// arrays still borrow `'a`. The `IChannel` only reads them while it sends
/// the query: it packs them, or a local channel runs the RPC at once. So the
/// borrow ends with [`Self::run`], and the future that it returns borrows
/// nothing.
#[must_use = "a query does nothing until it is run"]
pub struct ICQuery<'a, RPC: IopRPC> {
    settings: ICQueryBuilder<'a, RPC>,
    args: RPC::Arg<'a>,
}

// `RPC: 'static` is not a restriction: a marker is a unit struct generated at
// module scope. The bound is what makes the reply `'static`, as the callback
// that the `IChannel` core resolves later requires.
type IcReply<RPC> =
    Result<Owned<<RPC as IopRPC>::Res<'static>>, IcError<Owned<<RPC as IopRPC>::Exn<'static>>>>;

impl<'a, RPC: IopRPC + 'static> ICQuery<'a, RPC> {
    /// Starts the settings of a query for `RPC` on the channel `ic`.
    pub fn builder(ic: *mut ichannel_t) -> ICQueryBuilder<'a, RPC> {
        ICQueryBuilder {
            ic,
            hdr: None,
            timeout: 0,
            _rpc: PhantomData,
        }
    }

    /// Sends the query on its `IChannel`.
    ///
    /// On the main C thread, the query is sent before this returns. Only
    /// that thread may use an `IChannel`, so another thread copies the
    /// arguments and the header, and the main C thread sends the copy later.
    ///
    /// # Returns
    ///
    /// a [`CallbackFut`](../libcommon/run_callback/struct.CallbackFut.html)
    /// with either the result or the error produced.
    ///
    /// # Panics
    ///
    /// In debug builds, for an asynchronous RPC (`out null`): nothing
    /// answers its query, so the future would never resolve.
    pub fn run(self) -> CallbackFut<IcReply<RPC>> {
        debug_assert!(
            unsafe { (*RPC::rpc_desc()).async_() } == 0,
            "an asynchronous RPC gets no reply to await"
        );
        if is_main_c_thread() {
            let parts = QueryParts::new(
                &self.settings,
                self.settings.hdr.as_ref().map_or(ptr::null(), |hdr| {
                    ptr::from_ref(hdr).cast::<ic__hdr__t_raw>()
                }),
                self.args.get_cptr(),
            );

            run_now(|cb| {
                call_ic_query::<RPC>(cb, &parts);
            })
        } else {
            let owned = OwnedQuery::new(&self);

            run_callback(move |cb| {
                // Capture the whole `Send` copy, not its `parts` field.
                let owned = owned;

                call_ic_query::<RPC>(cb, &owned.parts);
            })
        }
    }
}

/// What sending a query reads, wherever it comes from.
struct QueryParts {
    ic: *mut ichannel_t,
    hdr: *const ic__hdr__t_raw,
    args: *const c_void,
    timeout: u32,
}

impl QueryParts {
    /// Takes the settings from `settings`, and the header and the arguments
    /// from wherever they live.
    fn new<RPC: IopRPC>(
        settings: &ICQueryBuilder<'_, RPC>,
        hdr: *const ic__hdr__t_raw,
        args: *const c_void,
    ) -> Self {
        Self {
            ic: settings.ic,
            hdr,
            args,
            timeout: settings.timeout,
        }
    }
}

/// Copy of a query, for a thread that must let the main C thread send it.
struct OwnedQuery {
    /// Points into the two copies below.
    parts: QueryParts,
    _hdr: Option<OwnedGeneric>,
    _args: OwnedGeneric,
}

impl OwnedQuery {
    fn new<RPC: IopRPC>(query: &ICQuery<'_, RPC>) -> Self {
        let hdr = query
            .settings
            .hdr
            .as_ref()
            .map(|hdr| hdr.as_generic().dup());
        let args = query.args.as_generic().dup();

        // The copies live in the libc pool, so the pointers stay valid when
        // the `OwnedQuery` moves.
        Self {
            parts: QueryParts::new(
                &query.settings,
                hdr.as_ref().map_or(ptr::null(), |hdr| hdr.as_raw().cast()),
                args.as_raw(),
            ),
            _hdr: hdr,
            _args: args,
        }
    }
}

// SAFETY: the arguments and the header are owned deep copies in the
// process-global libc pool, and `OwnedGeneric` is `Send`; what blocks the
// auto impl is the raw pointers. Those of `parts` point into the copies,
// which the `OwnedQuery` owns. The one to the `IChannel` is only
// dereferenced on the main C event loop, when the query is sent, and
// remains the caller's responsibility until then.
unsafe impl Send for OwnedQuery {}

// }}}
// {{{ Error Handler

/// Error returned by an `IChannel` query.
pub enum IcError<E> {
    /// A modeled IOP exception returned by the RPC (`IC_MSG_EXN`).
    Exn(E),
    /// A status failure with no modeled exception - e.g. timeout, canceled,
    /// or unimplemented RPC (any status other than OK/EXN).
    Status(ic__ic_status__e),
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
/// - `msg.priv_` holds an initialized `Callback<IcReply<RPC>>` written by
///   `ic_prepare_msg` with the *same* `RPC`.
/// - On [`IC_MSG_OK`], `res` points to a value described by the `result` of
///   `RPC`; on [`IC_MSG_EXN`], `exn` points to a value described by its
///   `exn`.
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
unsafe extern "C" fn cb_func<RPC: IopRPC>(
    _ic: *mut ichannel_t,
    msg_p: *mut ic_msg_t,
    status: ic__ic_status__e,
    res: *mut c_void,
    exn: *mut c_void,
) {
    let msg = unsafe { &mut *msg_p };

    let ptr = (&raw mut msg.priv_) as *mut Callback<IcReply<RPC>>;
    debug_assert!(ptr.is_aligned(), "ic_msg_t::priv_ is not properly aligned");
    let cb = unsafe { read(ptr) };
    // SAFETY: the descriptor of an RPC is static.
    let rpc = unsafe { &*RPC::rpc_desc() };
    if status == IC_MSG_OK {
        debug_assert!(!res.is_null(), "IC_MSG_OK with null result");
        let result = unsafe { RPC::Res::<'static>::dup_from_raw(rpc.result, res) };
        cb.call(Ok(result));
    } else if status == IC_MSG_EXN {
        debug_assert!(!exn.is_null(), "IC_MSG_EXN with null exn");
        let result = unsafe { RPC::Exn::<'static>::dup_from_raw(rpc.exn, exn) };
        cb.call(Err(IcError::Exn(result)));
    } else {
        cb.call(Err(IcError::Status(status)));
    }
}

/// Writes and wires the elements needed to send a `msg` on an `IChannel` and
/// links a callback function.
fn ic_prepare_msg<RPC: IopRPC>(msg: &mut ic_msg_t, cb: Callback<IcReply<RPC>>, query: &QueryParts) {
    let rpc = RPC::rpc_desc();
    let ptr = (&raw mut msg.priv_) as *mut Callback<IcReply<RPC>>;

    debug_assert!(ptr.is_aligned(), "ic_msg_t::priv_ is not properly aligned");
    unsafe {
        write(ptr, cb);
    }
    msg.cb = Some(cb_func::<RPC>);
    msg.rpc = rpc;
    msg.set_async(unsafe { (*rpc).async_() } != 0);
    msg.cmd = RPC::CMD;
    // The header only has to live until `__ic_query()` returns: the
    // `IChannel` packs it, or a local channel runs the RPC at once.
    msg.hdr = query.hdr;
    msg.set_trace(false);
    if query.timeout != 0 {
        unsafe {
            ic_msg_set_timeout(msg, query.timeout);
        }
    }
}

// }}}
// {{{ Rust IChannel query

/// Allocates a `msg`, decides whether to `do_bpack`, packs the args via
/// [`__ic_msg_build`], then calls [`__ic_query`] to send the query on the
/// `IChannel`.
///
/// `Owned<T>` is an associated-type projection, which is not injective, so
/// `RPC` cannot be inferred back from `cb`: callers name it explicitly.
fn call_ic_query<RPC: IopRPC>(cb: Callback<IcReply<RPC>>, ic_query: &QueryParts) {
    let msg = unsafe { &mut *ic_msg_new(size_of_val(&cb) as i32) };

    ic_prepare_msg::<RPC>(msg, cb, ic_query);
    let args = unsafe { (*msg.rpc).args };
    let do_bpack = !unsafe { ic_is_local(ic_query.ic) } || msg.force_pack();
    unsafe {
        __ic_msg_build(msg, args, ic_query.args, do_bpack);
    }
    unsafe {
        __ic_query(ic_query.ic, msg);
    }
}

// }}}
