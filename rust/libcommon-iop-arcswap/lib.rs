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

//! Lock-free `ArcSwap` primitive for the IOP environment context.
//!
//! `iop_env_t` itself stays a plain C struct (see `src/iop/priv.h`).
//! Only the mutable `iop_env_ctx_t` storage is held here, behind an
//! `ArcSwap<CtxBox>`. C code calls into these FFI functions to acquire
//! snapshots, mutate uniquely-owned copies, and install new contexts
//! atomically.
//!
//! Layout-wise, the Rust side never inspects `iop_env_ctx_t` — it carries
//! it around as `*mut c_void` plus a C-supplied drop function pointer.
//! This avoids forcing bindgen to ingest `lib-common/iop.h` from
//! `libcommon-core` (which would cascade into static-inline wrappers
//! whose underlying symbols only live in the full `libcommon`), and
//! avoids creating a build-time dependency cycle between
//! `libcommon-core-rs` (Rust) and `libcommon-minimal` (C).

#![allow(non_camel_case_types)]

use std::ffi::c_void;
use std::sync::Arc;

use arc_swap::ArcSwap;

// {{{ CtxBox

/// C-supplied drop function for an opaque ctx pointer.
pub type iop_env_ctx_drop_f = unsafe extern "C" fn(*mut c_void);

/// Owning wrapper around an opaque ctx pointer; calls the C-supplied
/// drop function on `Drop`.
pub struct CtxBox {
    ptr: *mut c_void,
    drop_fn: iop_env_ctx_drop_f,
}

// SAFETY: the underlying ctx is logically immutable once published into
// the ArcSwap. Writers replace the whole Arc; they never edit in place
// while readers are active.
unsafe impl Send for CtxBox {}
unsafe impl Sync for CtxBox {}

impl Drop for CtxBox {
    fn drop(&mut self) {
        unsafe {
            (self.drop_fn)(self.ptr);
        }
    }
}

// }}}
// {{{ ArcSwap handle

/// Opaque handle exposed to C as `iop_env_ctx_arcswap_t *`. Holds the
/// lock-free `ArcSwap<CtxBox>` keeping the published ctx alive.
pub struct iop_env_ctx_arcswap_t {
    inner: ArcSwap<CtxBox>,
}

// }}}
// {{{ Acquire/release guard

/// Guard returned by [`iop_env_ctx_arcswap_acquire`]: `ctx` is the
/// readable ctx pointer; `arc_handle` is the `Arc::into_raw` keepalive
/// that [`iop_env_ctx_arcswap_release`] uses to decrement the strong
/// count.
///
/// Exposed to C as a `#[repr(C)]` struct so the scope macro in `iop.h`
/// can hold both fields and clean up on scope exit.
#[repr(C)]
pub struct iop_env_ctx_guard_t {
    pub ctx: *const c_void,
    pub arc_handle: *const CtxBox,
}

// }}}
// {{{ FFI

/// Allocate a new `ArcSwap`, taking an already-initialized ctx and the
/// drop function to use for that ctx and any subsequent swap.
///
/// # Safety
///
/// `initial_ctx` must be a valid pointer to a ctx that `drop_fn` knows
/// how to free. Ownership of `initial_ctx` is transferred to the `ArcSwap`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iop_env_ctx_arcswap_new(
    initial_ctx: *mut c_void,
    drop_fn: iop_env_ctx_drop_f,
) -> *mut iop_env_ctx_arcswap_t {
    let box_inner = Arc::new(CtxBox {
        ptr: initial_ctx,
        drop_fn,
    });
    Box::into_raw(Box::new(iop_env_ctx_arcswap_t {
        inner: ArcSwap::new(box_inner),
    }))
}

/// Free an `ArcSwap` previously returned by [`iop_env_ctx_arcswap_new`].
///
/// # Safety
///
/// `p` must come from `iop_env_ctx_arcswap_new` and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iop_env_ctx_arcswap_free(p: *mut iop_env_ctx_arcswap_t) {
    if p.is_null() {
        return;
    }
    drop(unsafe { Box::from_raw(p) });
}

/// Acquire a refcounted snapshot of the current ctx.
///
/// Must be paired with exactly one [`iop_env_ctx_arcswap_release`].
///
/// # Safety
///
/// `p` must point to a valid `iop_env_ctx_arcswap_t`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iop_env_ctx_arcswap_acquire(
    p: *const iop_env_ctx_arcswap_t,
) -> iop_env_ctx_guard_t {
    let arc = unsafe { (*p).inner.load_full() };
    let ctx_ptr = arc.ptr.cast_const();
    let arc_handle = Arc::into_raw(arc);
    iop_env_ctx_guard_t {
        ctx: ctx_ptr,
        arc_handle,
    }
}

/// Release a guard previously returned by [`iop_env_ctx_arcswap_acquire`].
///
/// # Safety
///
/// `guard.arc_handle` must come from `iop_env_ctx_arcswap_acquire` and
/// not have been released.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iop_env_ctx_arcswap_release(guard: iop_env_ctx_guard_t) {
    if guard.arc_handle.is_null() {
        return;
    }
    drop(unsafe { Arc::from_raw(guard.arc_handle) });
}

/// Atomically install a new ctx in the `ArcSwap`, dropping the previous
/// one once its last reader releases it. Concurrent readers observe the
/// store atomically.
///
/// # Safety
///
/// `p` must point to a valid `iop_env_ctx_arcswap_t`. `new_ctx` must be
/// a valid pointer to a ctx that `drop_fn` knows how to free. Ownership
/// of `new_ctx` is transferred to the `ArcSwap`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn iop_env_ctx_arcswap_store(
    p: *mut iop_env_ctx_arcswap_t,
    new_ctx: *mut c_void,
    drop_fn: iop_env_ctx_drop_f,
) {
    let box_inner = Arc::new(CtxBox {
        ptr: new_ctx,
        drop_fn,
    });
    unsafe {
        (*p).inner.store(box_inner);
    }
}

// }}}
