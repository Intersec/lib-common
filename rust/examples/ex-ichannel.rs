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

//! An example of Rust `IChannel` usage: a chat between users.
//!
//! On login, a client sends a message to the server, which broadcasts it
//! (one-shot) to every other logged-in client. The server detects clients
//! logging in and out, and clients detect a disconnected server.
//!
//! # How to use
//!
//! Build from the repository root with `waf`; the binary is produced in
//! `rust/examples`. Run one server and as many clients as wanted, each in its
//! own terminal:
//!
//! ```bash
//! ./ex-ichannel -S 127.0.0.1:3030   # server
//! ./ex-ichannel -C 127.0.0.1:3030   # client
//! ```
//!
//! Each client's message reaches the server and the already-connected clients.
//! Exit with `SIGINT` / `SIGTERM` / `SIGQUIT` (e.g. Ctrl+C).

use std::cell::RefCell;
use std::env;
use std::ffi::c_int;
use std::mem::zeroed;
use std::process;
use std::ptr;

use ichannel::bindings::ichannel_t;
use ichannel::{ICQuery, IcError, ic_query};
use libcommon::bindings::thr_get_module;
use libcommon::c_main_waker::run_future;
use libcommon::iop::{Env, IopDup as _};
use libcommon_core::lstr;
use libcommon_core::module::{module_release, module_require};

use crate::bindings::ic_event_t::{IC_EVT_CONNECTED, IC_EVT_DISCONNECTED};
use crate::bindings::{
    AF_UNSPEC, SIGINT, SIGQUIT, SIGTERM, addr_info, addr_parse, data_t, el_blocker_register,
    el_loop, el_signal_register, el_t, el_unregister, exiop__hello_iface__send_args__t,
    exiop__hello_iface__send_async_args__t, exiop__hello_iface__send_exn__t,
    exiop__hello_iface__send_res__t, exiop__pkg, exiop_ic_cbs_init, exiop_ic_cbs_wipe,
    exiop_proto_tcp, exiop_register_send, exiop_register_send_async, exiop_reply_send,
    exiop_send_async_query, exiop_send_cmd, exiop_send_rpc, exiop_sock_stream, ic__hdr__t, ic_bye,
    ic_connect, ic_event_t, ic_get_module, ic_init, ic_listento, ic_new, ic_spawn, ic_wipe,
    in_port_t, iop_env_t, ps_initlstr, pstream_t, qm_ic_cbs_t, sa_family_t, sockunion_t,
};

#[waf_cargo_build::bindings_mod]
pub mod bindings {
    pub use ichannel::bindings::*;
    waf_cargo_build::include_bindings!();
}

/// Gathers the "global" state of the example in a single place.
struct Global {
    is_closing: bool,
    opt_client: bool,

    blocker: el_t,
    remote_ic: *mut ichannel_t,
    iop_env: *const iop_env_t,
    ic_impl: *const qm_ic_cbs_t,
    clients: Vec<*mut ichannel_t>,
}

impl Global {
    const fn new() -> Self {
        Self {
            is_closing: false,
            opt_client: false,
            blocker: ptr::null_mut(),
            remote_ic: ptr::null_mut(),
            iop_env: ptr::null(),
            ic_impl: ptr::null(),
            clients: Vec::new(),
        }
    }
}

thread_local! {
    static G: RefCell<Global> = const { RefCell::new(Global::new()) };
}

struct SendPtr<T>(*mut T);
unsafe impl<T> Send for SendPtr<T> {}

// {{{ Utils

/// Parse and resolve `addr`, writing the resulting socket address into `out`.
///
/// # Panics
///
/// Panics if `addr` cannot be parsed or resolved.
fn exiop_addr_resolve(addr: &str, out: *mut sockunion_t) {
    let s = lstr::from_str(addr).as_raw();
    let mut host: pstream_t = unsafe { zeroed() };
    let mut port: in_port_t = 0;

    assert!(
        unsafe { addr_parse(ps_initlstr(&raw const s), &raw mut host, &raw mut port, -1) } == 0,
        "unable to parse address: {addr}"
    );
    assert!(
        unsafe { addr_info(out, AF_UNSPEC as sa_family_t, host, port) } == 0,
        "unable to resolve address: {addr}"
    );
}

/// Allow the server to listen on an address and port so that it can receive
/// messages, calling `on_accept` on each incoming connection.
///
/// The returned listener is owned by the event loop.
///
/// # Panics
///
/// Panics if the listener cannot bind on `addr`.
fn exiop_ic_listento(
    addr: &str,
    on_accept: unsafe extern "C" fn(ev: el_t, fd: c_int) -> c_int,
) -> el_t {
    let mut su: sockunion_t = unsafe { zeroed() };

    exiop_addr_resolve(addr, &raw mut su);

    let ev = unsafe {
        ic_listento(
            &raw const su,
            exiop_sock_stream(),
            exiop_proto_tcp(),
            Some(on_accept),
        )
    };
    assert!(!ev.is_null(), "cannot bind on {addr}");

    ev
}

// }}}
// {{{ Client

/// Allow a client to receive a message sent through the server.
unsafe extern "C" fn exiop_send_async_impl(
    _ic: *mut ichannel_t,
    _slot: u64,
    arg: *mut exiop__hello_iface__send_async_args__t,
    _hdr: *const ic__hdr__t,
) {
    let arg = unsafe { &*arg };
    eprintln!(
        "received: msg = '{}', from client = {}",
        arg.msg__get(),
        arg.seqnum
    );
}

/// State machine of the client.
unsafe extern "C" fn exiop_client_on_event(ic: *mut ichannel_t, evt: ic_event_t) {
    match evt {
        IC_EVT_CONNECTED => {
            eprintln!("connected to server");
            let ic = SendPtr(ic);
            let future = async move {
                // Force capture of the whole `Send` wrapper.
                let ic = ic;

                let icq = {
                    let args = exiop__hello_iface__send_args__t {
                        seqnum: 1,
                        msg: lstr::raw("From client : Hello (1)"),
                    };
                    ICQuery {
                        ic: ic.0,
                        rpc: unsafe { exiop_send_rpc() },
                        hdr: ptr::null(),
                        args: args.dup(),
                        cmd: unsafe { exiop_send_cmd() },
                    }
                };
                ic_query::<
                    exiop__hello_iface__send_args__t,
                    exiop__hello_iface__send_res__t,
                    exiop__hello_iface__send_exn__t,
                >(icq)
                .await
            };

            // Drive the query on the main C thread: it is polled from the C
            // event loop itself, so no other runtime is needed.
            run_future(future, |result| match result {
                Ok(res) => eprintln!("helloworld: res = {}", res.res),
                Err(IcError::Exn(exn)) => {
                    eprintln!("cannot send (exception {}): {}", exn.code, exn.desc__get());
                }
                Err(IcError::Status(status)) => {
                    eprintln!("cannot send: query failed with status {status:?}");
                }
            });
        }
        IC_EVT_DISCONNECTED => eprintln!("disconnected from server"),
        _ => {}
    }
}

/// Initialize a client session on `remote_ic`, connected to `addr`.
///
/// # Panics
///
/// Panics if `addr` cannot be resolved or the connection cannot be established.
fn exiop_client_initialize(
    addr: &str,
    remote_ic: *mut ichannel_t,
    iop_env: *const iop_env_t,
    ic_impl: *mut qm_ic_cbs_t,
) {
    unsafe {
        ic_init(remote_ic);
        (*remote_ic).iop_env = iop_env;
        (*remote_ic).on_event = Some(exiop_client_on_event);
        (*remote_ic).impl_ = ic_impl.cast_const();
    }

    exiop_addr_resolve(addr, unsafe { &raw mut (*remote_ic).su });

    assert!(
        unsafe { ic_connect(remote_ic) } >= 0,
        "cannot connect to {addr}"
    );

    // Register RPCs: the client receives the broadcast `sendAsync`.
    unsafe {
        exiop_register_send_async(ic_impl, Some(exiop_send_async_impl));
    }
}

// }}}
// {{{ Server implementation

/// Allow the server to respond to a message and broadcast it on the channel.
unsafe extern "C" fn exiop_send_impl(
    ic: *mut ichannel_t,
    slot: u64,
    arg: *mut exiop__hello_iface__send_args__t,
    _hdr: *const ic__hdr__t,
) {
    let arg = unsafe { &*arg };
    eprintln!(
        "helloworld: msg = '{}', seqnum = {}",
        arg.msg__get(),
        arg.seqnum
    );

    unsafe {
        exiop_reply_send(ic, slot, 1);
    }

    // Broadcast the message to the other clients.
    G.with_borrow(|g| {
        for &client in &g.clients {
            if client != ic {
                unsafe {
                    exiop_send_async_query(client, 0, arg.msg);
                }
            }
        }
    });
}

/// Allow the server to choose an action depending on whether the client
/// connects or disconnects from the channel.
unsafe extern "C" fn exiop_server_on_event(ic: *mut ichannel_t, evt: ic_event_t) {
    match evt {
        IC_EVT_CONNECTED => {
            eprintln!("client {ic:?} connected");
            G.with_borrow_mut(|g| g.clients.push(ic));
        }
        IC_EVT_DISCONNECTED => {
            eprintln!("client {ic:?} disconnected");
            G.with_borrow_mut(|g| {
                if let Some(pos) = g.clients.iter().position(|&client| client == ic) {
                    g.clients.remove(pos);
                }
            });
        }
        _ => {}
    }
}

/// For each incoming client on `fd`, allocate a channel, wire it up with the
/// shared env table and start serving it.
unsafe extern "C" fn exiop_on_accept(_ev: el_t, fd: c_int) -> c_int {
    eprintln!("incoming connection");

    let (iop_env, ic_impl) = G.with_borrow(|g| (g.iop_env, g.ic_impl));

    let ic = unsafe { ic_new() };
    unsafe {
        (*ic).iop_env = iop_env;
        (*ic).on_event = Some(exiop_server_on_event);
        (*ic).impl_ = ic_impl;
        (*ic).set_do_el_unref(true);
        ic_spawn(ic, fd, None);
    }
    0
}

/// Initialize the server side by setting up a listener on `addr`.
fn exiop_server_initialize(addr: &str, ic_impl: *mut qm_ic_cbs_t) {
    // Start listening. The listener is owned by the event loop.
    let _listener = exiop_ic_listento(addr, exiop_on_accept);

    // Register RPCs: the server implements `send`.
    unsafe {
        exiop_register_send(ic_impl, Some(exiop_send_impl));
    }
}

// }}}
// {{{ Initialize & shutdown

struct Options {
    client: bool,
    server: bool,
    version: bool,
    address: Option<String>,
}

/// Print the usage message and exit with `code`.
fn usage(prog: &str, code: i32) -> ! {
    eprintln!("usage: {prog} [-h] [-v] [-C | -S] <address>");
    eprintln!();
    eprintln!("Options:");
    eprintln!("    -h, --help     show this help");
    eprintln!("    -v, --version  show version");
    eprintln!("    -C, --client   client mode");
    eprintln!("    -S, --server   server mode");
    process::exit(code);
}

/// Parse command-line arguments into [`Options`]; print usage and exit on
/// invalid input.
fn parse_options(args: &[String], prog: &str) -> Options {
    let mut opts = Options {
        client: false,
        server: false,
        version: false,
        address: None,
    };

    for arg in args.iter().skip(1) {
        match arg.as_str() {
            "-h" | "--help" => usage(prog, 1),
            "-v" | "--version" => opts.version = true,
            "-C" | "--client" => opts.client = true,
            "-S" | "--server" => opts.server = true,
            other if other.starts_with('-') => {
                eprintln!("unknown option: {other}");
                usage(prog, 1);
            }
            other if opts.address.is_none() => opts.address = Some(other.to_owned()),
            _ => usage(prog, 1),
        }
    }

    opts
}

/// On a termination signal, cleanly say goodbye (for the client mode), then
/// tell the event loop to stop.
unsafe extern "C" fn exiop_on_term(_idx: el_t, _signum: c_int, _priv: data_t) {
    G.with_borrow_mut(|g| {
        if g.is_closing {
            return;
        }

        // Close the remote connection.
        if g.opt_client {
            unsafe {
                ic_bye(g.remote_ic);
            }
        }

        // Make the event loop stop.
        unsafe {
            el_unregister(&raw mut g.blocker);
        }

        g.is_closing = true;
    });
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let prog = args
        .first()
        .map_or("ex-ichannel", String::as_str)
        .to_owned();

    let opts = parse_options(&args, &prog);

    if opts.version {
        eprintln!("HELLO - Version 1.0");
        return;
    }

    let Some(address) = opts.address else {
        usage(&prog, 1);
    };
    let mut iop_env = Env::new();
    iop_env.register_packages(&[&raw const exiop__pkg]);
    module_require(unsafe { ic_get_module() });
    // `run_future` schedules the polls on the main C thread queue.
    module_require(unsafe { thr_get_module() });

    let mut ic_impl: Box<qm_ic_cbs_t> = Box::new(unsafe { zeroed() });
    unsafe {
        exiop_ic_cbs_init(&raw mut *ic_impl);
    }
    let mut remote_ic: Box<ichannel_t> = Box::new(unsafe { zeroed() });

    G.with_borrow_mut(|g| {
        g.opt_client = opts.client;
        g.iop_env = iop_env.as_ptr();
        g.ic_impl = &raw const *ic_impl;
    });

    if opts.client {
        eprintln!("launching in client mode…");
        let remote_ptr: *mut ichannel_t = &raw mut *remote_ic;
        exiop_client_initialize(&address, remote_ptr, iop_env.as_ptr(), &raw mut *ic_impl);
        G.with_borrow_mut(|g| g.remote_ic = remote_ptr);
    } else if opts.server {
        eprintln!("launching in server mode…");
        exiop_server_initialize(&address, &raw mut *ic_impl);
    }

    let blocker = unsafe { el_blocker_register() };
    G.with_borrow_mut(|g| g.blocker = blocker);
    unsafe {
        el_signal_register(SIGTERM as c_int, Some(exiop_on_term), ptr::null_mut());
        el_signal_register(SIGINT as c_int, Some(exiop_on_term), ptr::null_mut());
        el_signal_register(SIGQUIT as c_int, Some(exiop_on_term), ptr::null_mut());
    }

    unsafe {
        el_loop();
    }

    if opts.client {
        unsafe {
            ic_wipe(&raw mut *remote_ic);
        }
    }

    G.with_borrow_mut(|g| g.clients.clear());

    unsafe {
        exiop_ic_cbs_wipe(&raw mut *ic_impl);
    }

    module_release(unsafe { thr_get_module() });
    module_release(unsafe { ic_get_module() });
}

// }}}
