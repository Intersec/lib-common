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

//! Infrastructure to send and to serve `IChannel` queries from Rust code.
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
//! resolving to either the result or an [`IcError`]. [`IcImpls`] is the
//! table of the RPCs that a channel serves, each implemented with
//! [`IopRPCImpl`] or [`IopRPCAsyncImpl`].

use bindings::{
    __ic_msg_build, __ic_query, __ic_reply, ev_priority_t, ic__hdr__t_raw, ic__hdr__u,
    ic__ic_status__e, ic_cb_entry_t, ic_cb_entry_type_t, ic_is_local, ic_msg_cancel, ic_msg_delete,
    ic_msg_new, ic_msg_set_priority, ic_msg_set_timeout, ic_msg_t, ic_reply_err, ic_slot_is_async,
    ichannel_t, iop_struct_t, qm_ic_cbs_t,
};
use libcommon::c_main_waker::run_future;
use libcommon::iop::{IopDup, IopRPC, Owned, OwnedGeneric, OwnedIop as _, StructUnion};
use libcommon::qhashmap::QMap;
use libcommon::run_callback::{Callback, CallbackFut, run_callback, run_now};
use libcommon_core::thr::{is_main_c_thread, main_c_queue_schedule};

use std::cell::Cell;
use std::ffi::c_void;
use std::marker::PhantomData;
use std::mem::{ManuallyDrop, zeroed};
use std::ptr::{self, read, write};

const IC_MSG_OK: ic__ic_status__e = ic__ic_status__e::OK;
const IC_MSG_EXN: ic__ic_status__e = ic__ic_status__e::EXN;
const IC_MSG_SERVER_ERROR: ic__ic_status__e = ic__ic_status__e::SERVER_ERROR;

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use libcommon::bindings::*;
    waf_cargo_build::include_bindings!();
}

// {{{ Errors

/// Error returned by an `IChannel` query.
pub enum IcError<E> {
    /// A modeled IOP exception returned by the RPC (`IC_MSG_EXN`).
    Exn(E),
    /// A status failure with no modeled exception - e.g. timeout, canceled,
    /// or unimplemented RPC (any status other than OK/EXN).
    Status(ic__ic_status__e),
}

// }}}
// {{{ Query

/// What the future of [`ICQuery::run`] resolves to.
// `RPC: 'static` is not a restriction: a marker is a unit struct generated at
// module scope. The bound is what makes the reply `'static`, as the callback
// that the `IChannel` core resolves later requires.
pub type IcReply<RPC> =
    Result<Owned<<RPC as IopRPC>::Res<'static>>, IcError<Owned<<RPC as IopRPC>::Exn<'static>>>>;

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
    priority: ev_priority_t,
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

    /// Places the query in the send queue by priority rather than by arrival
    /// order. Left alone, the query keeps `EV_PRIORITY_NORMAL`.
    ///
    /// A queue saturated with high-priority messages never pops the others,
    /// so raising this is a decision about the whole channel, not about one
    /// query.
    pub fn priority(mut self, priority: ev_priority_t) -> Self {
        self.priority = priority;
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

impl<'a, RPC: IopRPC + 'static> ICQuery<'a, RPC> {
    /// Starts the settings of a query for `RPC` on the channel `ic`.
    pub fn builder(ic: *mut ichannel_t) -> ICQueryBuilder<'a, RPC> {
        ICQueryBuilder {
            ic,
            hdr: None,
            timeout: 0,
            priority: ev_priority_t::EV_PRIORITY_NORMAL,
            _rpc: PhantomData,
        }
    }

    /// Sends the query on its `IChannel`.
    ///
    /// On the main C thread, the query is sent before this returns. Only
    /// that thread may use an `IChannel`, so another thread copies the
    /// arguments and the header, and the main C thread sends the copy later.
    ///
    /// Dropping the future before the reply cancels the query on its
    /// channel, as `ic_msg_cancel()` does in C.
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

// }}}
// {{{ Sending a query

/// What sending a query reads, wherever it comes from.
struct QueryParts {
    ic: *mut ichannel_t,
    hdr: *const ic__hdr__t_raw,
    args: *const c_void,
    timeout: u32,
    priority: ev_priority_t,
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
            priority: settings.priority,
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

/// Allocates a `msg`, decides whether to `do_bpack`, packs the args via
/// [`__ic_msg_build`], then calls [`__ic_query`] to send the query on the
/// `IChannel`.
///
/// `Owned<T>` is an associated-type projection, which is not injective, so
/// `RPC` cannot be inferred back from `cb`: callers name it explicitly.
fn call_ic_query<RPC: IopRPC>(cb: Callback<IcReply<RPC>>, ic_query: &QueryParts) {
    // A channel that is aborting its queries frees a new one without
    // calling it back, which would leave the future pending, and its cancel
    // hook on a freed message. Abort it here instead.
    if unsafe { (*ic_query.ic).cancel_guard() } {
        cb.call(Err(IcError::Status(ic__ic_status__e::ABORT)));
        return;
    }

    let msg = unsafe { &mut *ic_msg_new(size_of_val(&cb) as i32) };

    // The C side never completes the query of an asynchronous RPC, and
    // frees its message once sent: there is nothing to cancel.
    if unsafe { (*RPC::rpc_desc()).async_() } == 0 {
        let pending = PendingMsg(msg);

        if !cb.on_cancel(move || pending.cancel()) {
            // The future is gone, so nobody waits for the reply.
            let mut msg: *mut ic_msg_t = msg;

            unsafe {
                ic_msg_delete(&raw mut msg);
            }
            return;
        }
    }
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

/// The message of a query that its future may cancel.
struct PendingMsg(*mut ic_msg_t);

impl PendingMsg {
    fn cancel(self) {
        // SAFETY: a cancel hook runs on the main C thread, and only before
        // the completion callback. Until then, the `IChannel` keeps the
        // message alive. `ic_msg_cancel()` calls the completion callback
        // with `IC_MSG_CANCELED`, and the `IChannel` drops the answer of a
        // cancelled message, so the callback still runs once.
        unsafe {
            ic_msg_cancel(self.0);
        }
    }
}

// SAFETY: the pointer is only dereferenced by `cancel()`, on the main C
// thread.
unsafe impl Send for PendingMsg {}

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
    unsafe {
        ic_msg_set_timeout(msg, query.timeout);
        ic_msg_set_priority(msg, query.priority);
    }
}

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

// }}}
// {{{ RPC implementation

/// Slot of a received query, awaiting its reply.
///
/// Replying consumes the slot, so an RPC cannot be answered twice. A slot
/// dropped without a reply answers `IC_MSG_SERVER_ERROR`, so the caller
/// does not wait for its timeout. Together they carry an invariant the C
/// side can only document. An asynchronous RPC (`out null`) expects no
/// reply: its implementation gives the slot up with [`Self::discard`].
///
/// A reply is only valid on the main C thread, the thread the
/// implementation callback runs on. From another thread, the slot copies
/// the reply, and the main C thread sends the copy.
///
/// An implementation can keep the slot and reply after `handle()` returns.
/// The channel may be gone by then, so such a reply names the query by its
/// slot only, as the C side requires.
#[must_use = "a query slot must be replied to, or explicitly discarded"]
pub struct IcReplySlot<RPC: IopRPC> {
    slot: u64,
    _rpc: PhantomData<RPC>,
}

thread_local! {
    /// The channel and the slot of the query whose implementation callback
    /// runs, if any.
    static SERVED: Cell<Option<(*mut ichannel_t, u64)>> = const { Cell::new(None) };
}

impl<RPC: IopRPC> IcReplySlot<RPC> {
    /// The channel to reply on: the one of the query while its
    /// implementation callback runs, NULL after it, for a deferred reply.
    fn reply_ic(&self) -> *mut ichannel_t {
        self.channel().unwrap_or(ptr::null_mut())
    }

    /// Consumes the slot without its `Drop`, and gives the channel and the
    /// slot to reply with.
    fn take(self) -> (*mut ichannel_t, u64) {
        let this = ManuallyDrop::new(self);

        (this.reply_ic(), this.slot)
    }

    /// Answers the query with the RPC's result.
    pub fn reply_res(self, res: &RPC::Res<'_>) {
        // SAFETY: the descriptor of an RPC is static.
        let desc = unsafe { (*RPC::rpc_desc()).result };

        self.reply(IC_MSG_OK, desc, res);
    }

    /// Answers the query with the RPC's modeled exception.
    pub fn reply_exn(self, exn: &RPC::Exn<'_>) {
        // SAFETY: as `reply_res`.
        let desc = unsafe { (*RPC::rpc_desc()).exn };

        self.reply(IC_MSG_EXN, desc, exn);
    }

    /// Answers the query with `value`, which `desc` describes.
    fn reply<V: StructUnion>(self, status: ic__ic_status__e, desc: *const iop_struct_t, value: &V) {
        let (ic, slot) = self.take();

        if is_main_c_thread() {
            // SAFETY: `value` is described by `desc`, both being projections
            // of the same `RPC`, and the slot is answered once since
            // replying consumes it.
            unsafe {
                __ic_reply(ic, slot, status.0, -1, desc, value.get_cptr());
            }
        } else {
            // `value` can die before the main C thread runs.
            let value = value.as_generic().dup();

            // SAFETY: as above, with the descriptor of the copy. A NULL
            // channel names the query by its slot only.
            main_c_queue_schedule(move || unsafe {
                __ic_reply(
                    ptr::null_mut(),
                    slot,
                    status.0,
                    -1,
                    value.cdesc(),
                    value.as_raw(),
                );
            });
        }
    }

    /// Answers the query with a transport status, for a failure the RPC does
    /// not model as an exception.
    ///
    /// # Panics
    ///
    /// In debug builds, if `status` is `OK` or `EXN`: those two carry a
    /// payload, which this path has no way to send. Use [`Self::reply_res`]
    /// or [`Self::reply_exn`].
    pub fn reply_err(self, status: ic__ic_status__e) {
        debug_assert!(
            !matches!(status, IC_MSG_OK | IC_MSG_EXN),
            "reply_err cannot carry a payload"
        );

        let (ic, slot) = self.take();

        Self::send_err(ic, slot, status);
    }

    /// Answers the query `slot` with `status`, from the main C thread.
    fn send_err(ic: *mut ichannel_t, slot: u64, status: ic__ic_status__e) {
        if is_main_c_thread() {
            // SAFETY: the channel is the one of the query while its
            // callback runs, and NULL after it, as a deferred reply
            // requires.
            unsafe {
                ic_reply_err(ic, slot, status.0);
            }
        } else {
            // SAFETY: a NULL channel names the query by its slot only.
            main_c_queue_schedule(move || unsafe {
                ic_reply_err(ptr::null_mut(), slot, status.0);
            });
        }
    }

    /// The channel the query came from, while the implementation callback
    /// runs. After it, the channel may be gone, and this is `None`.
    ///
    /// An implementation sometimes needs it before answering -- to reach the
    /// other channels of a broadcast, say -- and replying consumes the slot.
    #[must_use]
    pub fn channel(&self) -> Option<*mut ichannel_t> {
        SERVED
            .get()
            .filter(|&(_, slot)| slot == self.slot)
            .map(|(ic, _)| ic)
    }

    /// Gives the slot up without answering, for an asynchronous RPC. Any
    /// other query then waits for its timeout, or forever without one.
    pub fn discard(self) {
        let _unused = self.take();
    }
}

impl<RPC: IopRPC> Drop for IcReplySlot<RPC> {
    /// Answers a query that nobody answered. Silence would leave the caller
    /// waiting for its timeout, or forever without one.
    fn drop(&mut self) {
        // SAFETY: `ic_slot_is_async()` only reads the bits of the slot.
        if unsafe { ic_slot_is_async(self.slot) } {
            return;
        }
        Self::send_err(self.reply_ic(), self.slot, IC_MSG_SERVER_ERROR);
    }
}

/// Rust implementation of one RPC.
///
/// The argument type is projected from the marker rather than spelled out,
/// so an implementation cannot read the query through the wrong struct --
/// which is what `IOP_RPC_IMPL_ARGS()` buys C callers, and what a
/// hand-written `extern "C"` callback loses.
pub trait IopRPCImpl: IopRPC + Sized {
    /// Serves one query. `hdr` is `None` when the query carries no header.
    fn handle(arg: &Self::Arg<'_>, hdr: Option<&ic__hdr__t_raw>, reply: IcReplySlot<Self>);
}

/// Untyped callback the `IChannel` core stores and calls, one instance per
/// implemented RPC.
///
/// This and [`ic_rust_async_impl_cb`] are the only places where the argument
/// blob is typed.
extern "C" fn ic_rust_impl_cb<RPC: IopRPCImpl>(
    ic: *mut ichannel_t,
    slot: u64,
    arg: *mut c_void,
    hdr: *const ic__hdr__t_raw,
) {
    // SAFETY: the core calls this with the blob of the RPC it was registered
    // for, described by that RPC's `args`; `IcImpls::register` ties the two
    // together by taking both from a single `RPC`. The borrow cannot escape
    // the call, which is exactly the C contract on `arg`.
    let arg: &RPC::Arg<'_> = unsafe { &*arg.cast() };
    let hdr = unsafe { hdr.as_ref() };

    serve(ic, slot, |reply| RPC::handle(arg, hdr, reply));
}

/// Gives `f` the reply slot of the query `slot`, with `ic` as the channel
/// it replies on while `f` runs.
fn serve<RPC: IopRPC>(ic: *mut ichannel_t, slot: u64, f: impl FnOnce(IcReplySlot<RPC>)) {
    let served = SERVED.replace(Some((ic, slot)));

    f(IcReplySlot {
        slot,
        _rpc: PhantomData,
    });
    SERVED.set(served);
}

/// Rust implementation of one RPC, with an `async fn`.
///
/// The `IChannel` only lends the argument and the header while its callback
/// runs, so the implementation gets copies. Its future is polled on the
/// main C thread, through [`run_future`], which needs the `thr` module: the
/// slot replies there without a copy. The future drops where its last waker
/// drops, which can be another thread.
///
/// ```ignore
/// impl IopRPCAsyncImpl for EchoRpc {
///     async fn handle(arg: Owned<Self::Arg<'static>>, _hdr: ..., reply: IcReplySlot<Self>) {
///         let i = compute(arg.i).await;
///
///         reply.reply_res(&tstiop_rpc__test__echo_res__t { i });
///     }
/// }
/// ```
pub trait IopRPCAsyncImpl: IopRPC + Sized + 'static {
    /// Serves one query. `hdr` is `None` when the query carries no header.
    fn handle(
        arg: Owned<Self::Arg<'static>>,
        hdr: Option<Owned<ic__hdr__t_raw>>,
        reply: IcReplySlot<Self>,
    ) -> impl Future<Output = ()> + Send + 'static;
}

/// Untyped callback of an [`IopRPCAsyncImpl`].
extern "C" fn ic_rust_async_impl_cb<RPC: IopRPCAsyncImpl>(
    ic: *mut ichannel_t,
    slot: u64,
    arg: *mut c_void,
    hdr: *const ic__hdr__t_raw,
) {
    // SAFETY: as `ic_rust_impl_cb`. The copies own their data, so they can
    // outlive the call.
    let arg = unsafe { <RPC::Arg<'static> as IopDup>::dup_from_raw((*RPC::rpc_desc()).args, arg) };
    let hdr = unsafe { hdr.as_ref() }.map(IopDup::dup);

    // On the main C thread, `run_future()` polls the future at once: a
    // future that does not wait replies on the channel of the query.
    serve(ic, slot, |reply| {
        run_future(RPC::handle(arg, hdr, reply), |()| {});
    });
}

/// The callback of an implementation, as the `IChannel` core calls it.
type IcImplCb = unsafe extern "C" fn(*mut ichannel_t, u64, *mut c_void, *const ic__hdr__t_raw);

/// Table of the RPCs an `IChannel` implements.
///
/// The `qm_t(ic_cbs)` the C side expects, driven through [`QMap`]: what
/// the C header reaches through `qm_init()`, `qm_add()` and the
/// `ic_cb_entry_t` initializer -- all macros, hence out of reach of
/// bindgen -- the Rust binding of the table does directly.
///
/// The table is either owned by Rust ([`Self::new`]), or owned by C and
/// borrowed ([`Self::borrow`]): most of the code that serves RPCs stays in
/// C, with its own table, and Rust adds its implementations to it.
///
/// An owned table is released when its `IcImpls` drops, or before with
/// [`Self::wipe`]. The channels pointing at it must be done with it by
/// then, so keep the `IcImpls` as long as they live.
pub struct IcImpls<'t> {
    cbs: IcImplsTable<'t>,
}

/// Where the table of an [`IcImpls`] lives.
enum IcImplsTable<'t> {
    /// Owned by Rust. Boxed, so that the pointer of [`IcImpls::as_ptr`]
    /// stays valid when the `IcImpls` moves.
    Owned(Box<QMap<'static, qm_ic_cbs_t>>),
    /// Owned by C, which also wipes it.
    Borrowed(&'t mut QMap<'static, qm_ic_cbs_t>),
}

impl IcImpls<'static> {
    /// Creates an empty table, owned by Rust.
    #[must_use]
    pub fn new() -> Self {
        Self {
            cbs: IcImplsTable::Owned(Box::new(QMap::new())),
        }
    }
}

impl<'t> IcImpls<'t> {
    /// Adds implementations to `cbs`, a table that C owns.
    ///
    /// ```ignore
    /// let cbs = unsafe { QMap::from_c_ptr_mut(&raw mut _G.ic_cbs) };
    /// let mut impls = IcImpls::borrow(cbs);
    /// ```
    #[must_use]
    pub fn borrow(cbs: &'t mut QMap<'static, qm_ic_cbs_t>) -> Self {
        Self {
            cbs: IcImplsTable::Borrowed(cbs),
        }
    }

    fn qm(&mut self) -> &mut QMap<'static, qm_ic_cbs_t> {
        match &mut self.cbs {
            IcImplsTable::Owned(qm) => qm,
            IcImplsTable::Borrowed(qm) => qm,
        }
    }

    /// Registers the implementation of the RPC `RPC`.
    ///
    /// # Panics
    ///
    /// The command id of `RPC` already has an implementation.
    pub fn register<RPC: IopRPCImpl>(&mut self) {
        self.insert::<RPC>(ic_rust_impl_cb::<RPC>);
    }

    /// Registers the `async fn` implementation of the RPC `RPC`.
    ///
    /// # Panics
    ///
    /// The command id of `RPC` already has an implementation.
    pub fn register_async<RPC: IopRPCAsyncImpl>(&mut self) {
        self.insert::<RPC>(ic_rust_async_impl_cb::<RPC>);
    }

    fn insert<RPC: IopRPC>(&mut self, cb: IcImplCb) {
        // SAFETY: `ic_cb_entry_t` is a C aggregate of a tag, pointers and
        // function pointers; all-zero is the empty `IC_CB_NORMAL` entry the
        // three assignments below complete, leaving the hooks unset just as
        // the C initializer does.
        let mut entry: ic_cb_entry_t = unsafe { zeroed() };

        // The callers pass the callback of the same `RPC` as the descriptor,
        // so the entry cannot describe one RPC and serve another.
        entry.cb_type = ic_cb_entry_type_t::IC_CB_NORMAL;
        entry.rpc = RPC::rpc_desc();
        entry.u.cb.cb = Some(cb);

        assert!(
            self.qm().try_insert(RPC::CMD as u32, entry).is_ok(),
            "duplicate registration for RPC command {:#x}",
            RPC::CMD
        );
    }

    /// Pointer to store in `ichannel_t::impl_`.
    #[must_use]
    pub fn as_ptr(&self) -> *const qm_ic_cbs_t {
        match &self.cbs {
            IcImplsTable::Owned(qm) => qm.as_ptr(),
            IcImplsTable::Borrowed(qm) => qm.as_ptr(),
        }
    }

    /// Releases a table owned by Rust. The channels pointing at it must be
    /// done with it. A table owned by C is left to C.
    ///
    /// An empty table is left behind, so the pointer [`Self::as_ptr`] hands
    /// out stays valid and wiping twice is harmless.
    pub fn wipe(&mut self) {
        if let IcImplsTable::Owned(qm) = &mut self.cbs {
            **qm = QMap::new();
        }
    }
}

impl Default for IcImpls<'static> {
    fn default() -> Self {
        Self::new()
    }
}

// SAFETY: the table only holds RPC descriptors, which live in iopc-generated
// statics, and monomorphised callbacks, which are code pointers. Neither is
// tied to a thread; what blocks the auto impl is the raw pointers inside the
// qm.
unsafe impl Send for IcImpls<'_> {}

// }}}
