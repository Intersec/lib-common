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

//! Module to interact with IOPs in Rust
//!
//! WIP

use std::marker::PhantomData;
use std::mem::MaybeUninit;
use std::os::raw::c_void;
use std::ptr;

use crate::bindings::{
    iop_enum_t, iop_env_ctx_acquire, iop_env_ctx_dup, iop_env_ctx_get_struct, iop_env_ctx_guard_t,
    iop_env_ctx_release, iop_env_delete, iop_env_new, iop_env_t, iop_init_desc, iop_pkg_t,
    iop_register_packages, iop_sb_jpack, iop_struct_t, mp_iop_dup_desc_sz, t_iop_junpack_ptr_ps,
    t_iop_new_desc, t_iop_sb_ypack, t_iop_yunpack_ptr_ps,
};

use crate::lstr;
use crate::mem_stack::TScope;
use crate::pstream::pstream_t;
use libcommon_core::bindings::{mem_pool_libc, mp_ifree};
use libcommon_core::{SB_1k, sb::Sb};
use std::ops::Deref;

// {{{ Errors

// TODO: use `thiserror` package to provide a nice error.
pub type UnpackError = String;

// }}}
// {{{ IOP Base

/// Base trait for IOP types.
pub trait Base {}

// }}}
// {{{ IOP Enum

/// IOP trait for enum that can be used for dyn dispatch.
pub trait Enum: Base {
    /// Get the C description of the IOP enum.
    fn get_cdesc(&self) -> *const iop_enum_t;
}

/// IOP trait implemented by a C IOP enum.
pub trait CEnum: Sized + Enum {
    /// The C description of the IOP enum.
    const CDESC: *const iop_enum_t;
}

// }}}
// {{{ IOP StructUnion

/// IOP trait for struct or union that can be used for dyn dispatch.
pub trait StructUnion: Base {
    /// Get the C description of the IOP structure or union.
    fn get_cdesc(&self) -> *const iop_struct_t;

    /// Get the C pointer of the IOP structure or union.
    fn get_cptr(&self) -> *const c_void {
        ptr::from_ref(self) as *const c_void
    }

    /// Get the mutable C pointer of the IOP structure or union.
    fn get_cptr_mut(&mut self) -> *mut c_void {
        ptr::from_mut(self) as *mut c_void
    }

    /// Export the IOP struct or union as JSON
    fn as_json(&self) -> String {
        SB_1k!(sb);

        unsafe {
            iop_sb_jpack(sb.as_mut_ptr(), self.get_cdesc(), self.get_cptr(), 0);
        }

        sb.to_string()
    }

    /// Export the IOP struct or union as YAML
    fn as_yaml(&self) -> String {
        let _t_scope = TScope::new_scope();
        SB_1k!(sb);

        unsafe {
            t_iop_sb_ypack(
                sb.as_mut_ptr(),
                self.get_cdesc(),
                self.get_cptr(),
                ptr::null(),
            );
        };

        sb.to_string()
    }
}

/// IOP trait implemented by a C IOP struct or union.
pub trait CStructUnion: Sized + StructUnion {
    /// The C description of the IOP struct or union.
    const CDESC: *const iop_struct_t;

    /// Create an empty `IopStructUnion` with the default arguments.
    fn new() -> Self {
        let mut res = MaybeUninit::<Self>::uninit();

        unsafe {
            iop_init_desc(Self::CDESC, res.as_mut_ptr().cast::<c_void>());
        }

        unsafe { res.assume_init() }
    }

    /// Create a new IOP struct or union on a `t_scope`.
    ///
    /// The returned reference is only valid within the `t_scope` lifetime.
    fn t_new(_t_scope: &TScope) -> &Self {
        unsafe {
            let ptr = t_iop_new_desc(Self::CDESC);
            &*(ptr as *const Self)
        }
    }
}

// }}}
// {{{ IOP Union

/// IOP trait for union that can be used for dyn dispatch.
pub trait Union: StructUnion {}

/// IOP trait implemented by a C IOP union.
pub trait CUnion: Union + CStructUnion {}

// }}}
// {{{ IOP Struct

/// IOP trait for struct that can be used for dyn dispatch.
pub trait Struct: StructUnion {}

/// IOP trait implemented by a C IOP struct.
pub trait CStruct: Struct + CStructUnion {
    /// Create an empty struct with the default value.
    ///
    /// Use by the `iop_new!()` macro.
    fn default_new() -> Self {
        Self::new()
    }
}

// }}}
// {{{ IOP Generic struct or union

/// Generic struct or union that contains a pointer and its description.
///
/// It implements the IOP `StructUnion` trait.
///
/// `ctx` keeps the ctx snapshot `cdesc` was resolved from alive, or is
/// `None` for a static / caller-owned descriptor (via `new`).
pub struct GenericStructUnion<'a> {
    cdesc: *const iop_struct_t,
    cptr: *mut c_void,
    ctx: Option<EnvCtx>,
    _phantom: PhantomData<&'a c_void>,
}

impl GenericStructUnion<'_> {
    /// Create a new `GenericStructUnion`.
    ///
    /// `cdesc` must outlive the returned value; this is the case for static
    /// (compiled-in) descriptors. For a descriptor resolved from an
    /// [`EnvCtx`], build the value through that `EnvCtx` (e.g.
    /// [`EnvCtx::t_junpack_desc`]) so the snapshot is kept alive.
    pub fn new(cdesc: *const iop_struct_t, cptr: *mut c_void) -> Self {
        Self {
            cdesc,
            cptr,
            ctx: None,
            _phantom: PhantomData,
        }
    }

    /// Like [`new`](Self::new) but keeps `ctx` alive so a descriptor resolved
    /// from that snapshot stays valid for the returned value's lifetime.
    fn new_with_ctx(cdesc: *const iop_struct_t, cptr: *mut c_void, ctx: EnvCtx) -> Self {
        Self {
            cdesc,
            cptr,
            ctx: Some(ctx),
            _phantom: PhantomData,
        }
    }
}

impl Base for GenericStructUnion<'_> {}

impl StructUnion for GenericStructUnion<'_> {
    fn get_cdesc(&self) -> *const iop_struct_t {
        self.cdesc
    }

    fn get_cptr(&self) -> *const c_void {
        self.cptr
    }

    fn get_cptr_mut(&mut self) -> *mut c_void {
        self.cptr
    }
}

impl IopDup for GenericStructUnion<'_> {
    type Owned = OwnedGeneric;

    fn dup(&self) -> OwnedGeneric {
        let cdesc = self.cdesc;
        let raw = unsafe {
            mp_iop_dup_desc_sz(&raw mut mem_pool_libc, cdesc, self.cptr, ptr::null_mut())
        };
        let cptr = ptr::NonNull::new(raw)
            .expect("IOP dup returned null (null source or allocation failure)")
            .as_ptr();

        // Rebuild the handle over the owned blob, cloning the ctx (bumping its
        // refcount) so a DSO-resolved descriptor stays valid.
        OwnedGeneric {
            inner: GenericStructUnion {
                cdesc,
                cptr,
                ctx: self.ctx.clone(),
                _phantom: PhantomData,
            },
        }
    }

    unsafe fn dup_from_raw(cdesc: *const iop_struct_t, blob: *const c_void) -> OwnedGeneric {
        // Wrap the raw blob in a transient handle over `cdesc`, then deep-dup
        // it. The descriptor is treated as static / caller-owned (`ctx` stays
        // `None`), which holds for the compiled-in RPC descriptors this is
        // reached from.
        GenericStructUnion::new(cdesc, blob.cast_mut()).dup()
    }
}

// }}}
// {{{ IOP Owned
// {{{ IopDup trait

/// Deep-duplicate an IOP value into an owned allocation.
///
/// The owned representation is specialized per family through the [`Owned`]
/// associated type: a compiled ([`CStructUnion`]) value only needs a pointer
/// (its descriptor is `'static`), while a [`GenericStructUnion`] must keep its
/// [`EnvCtx`] snapshot alive. `dup` cannot live on [`StructUnion`] itself
/// because that trait is object-safe and would require a single uniform return
/// type.
#[allow(clippy::module_name_repetitions)]
pub trait IopDup: StructUnion + Sized {
    /// Minimal owned representation for this type.
    ///
    /// Owned IOP values live in the process-global libc pool and carry no
    /// borrows, so every representation is `Send` — required to hand a dup'd
    /// value back through an ichannel callback (whose payload must be `Send`).
    /// [`OwnedIop`] gives access to the raw `(blob, descriptor)` pair without
    /// knowing which representation was selected.
    type Owned: Send + OwnedIop;

    /// Deep-duplicate this value into `mem_pool_libc`.
    fn dup(&self) -> Self::Owned;

    /// Deep-duplicate an IOP value from a raw `(descriptor, blob)` pair into
    /// `mem_pool_libc`.
    ///
    /// Counterpart to [`dup`](Self::dup) for the case where only a raw C
    /// pointer and its descriptor are available rather than a typed `&self` —
    /// e.g. reconstructing an ichannel RPC result or exception from inside a C
    /// callback. For a compiled type `cdesc` is redundant (it must equal
    /// `Self::CDESC`; only the layout matters); for [`GenericStructUnion`] the
    /// descriptor is what makes the owned value usable at all.
    ///
    /// # Safety
    ///
    /// `blob` must be non-null and point to a valid IOP value described by
    /// `cdesc`. For a compiled `Self`, the blob must have `Self`'s layout
    /// (equivalently `cdesc == Self::CDESC`). `cdesc` must stay valid for the
    /// lifetime of the returned value — guaranteed for static / compiled-in
    /// descriptors, which is the ichannel RPC case.
    unsafe fn dup_from_raw(cdesc: *const iop_struct_t, blob: *const c_void) -> Self::Owned;
}

/// Name-preserving alias: `Owned<Foo>` and `Owned<GenericStructUnion>` resolve
/// to their specialized representations.
pub type Owned<T> = <T as IopDup>::Owned;

// }}}
// {{{ OwnedIop trait

mod owned_sealed {
    pub trait Sealed {}
}

/// Shared surface over any owned IOP value, regardless of representation.
#[allow(clippy::module_name_repetitions)]
pub trait OwnedIop: owned_sealed::Sealed {
    /// Raw pointer to the owned IOP blob.
    fn as_raw(&self) -> *const c_void;

    /// Descriptor of the owned value.
    fn cdesc(&self) -> *const iop_struct_t;
}

// }}}
// {{{ OwnedStruct: compiled representation

/// Owned compiled IOP value: a single pointer into `mem_pool_libc`. The
/// descriptor is recovered from `T::CDESC` (`'static`, zero storage) and there
/// is never a ctx to keep alive.
pub struct OwnedStruct<T: CStructUnion> {
    ptr: ptr::NonNull<T>,
}

// The blob lives in the process-global libc pool, so the owned value can move
// across threads.
unsafe impl<T: CStructUnion> Send for OwnedStruct<T> {}

impl<T: CStructUnion> owned_sealed::Sealed for OwnedStruct<T> {}

impl<T: CStructUnion> OwnedIop for OwnedStruct<T> {
    fn as_raw(&self) -> *const c_void {
        self.ptr.as_ptr().cast()
    }

    fn cdesc(&self) -> *const iop_struct_t {
        T::CDESC
    }
}

// Only compiled types are layout-compatible with the blob, so `Deref` is
// bounded on `CStructUnion`.
impl<T: CStructUnion> Deref for OwnedStruct<T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { self.ptr.as_ref() }
    }
}

impl<T: CStructUnion> Drop for OwnedStruct<T> {
    fn drop(&mut self) {
        unsafe {
            mp_ifree(&raw mut mem_pool_libc, self.ptr.as_ptr().cast::<c_void>());
        }
    }
}

// }}}
// {{{ OwnedGeneric: generic representation

/// Owned generic IOP value. Reuses [`GenericStructUnion`] as its storage; the
/// `'static` inner is sound because the blob is owned here (freed on drop) and
/// the borrows handed out via `Deref` are bounded by this value's lifetime.
pub struct OwnedGeneric {
    inner: GenericStructUnion<'static>,
}

unsafe impl Send for OwnedGeneric {}

impl owned_sealed::Sealed for OwnedGeneric {}

impl OwnedIop for OwnedGeneric {
    fn as_raw(&self) -> *const c_void {
        self.inner.get_cptr()
    }

    fn cdesc(&self) -> *const iop_struct_t {
        self.inner.get_cdesc()
    }
}

impl Deref for OwnedGeneric {
    type Target = GenericStructUnion<'static>;
    fn deref(&self) -> &GenericStructUnion<'static> {
        &self.inner
    }
}

impl Drop for OwnedGeneric {
    fn drop(&mut self) {
        // Free the blob; `inner.ctx` drops afterwards, releasing the snapshot.
        unsafe {
            mp_ifree(&raw mut mem_pool_libc, self.inner.cptr);
        }
    }
}

// }}}
// {{{ IopDup implementations

impl<T: CStructUnion> IopDup for T {
    type Owned = OwnedStruct<T>;

    fn dup(&self) -> OwnedStruct<T> {
        unsafe { <T as IopDup>::dup_from_raw(T::CDESC, self.get_cptr()) }
    }

    unsafe fn dup_from_raw(_cdesc: *const iop_struct_t, blob: *const c_void) -> OwnedStruct<T> {
        // A compiled type is laid out exactly like its IOP blob, so the blob is
        // duplicated straight through `T::CDESC` and reinterpreted as `T`.
        let raw =
            unsafe { mp_iop_dup_desc_sz(&raw mut mem_pool_libc, T::CDESC, blob, ptr::null_mut()) };
        let ptr = ptr::NonNull::new(raw.cast::<T>())
            .expect("IOP dup returned null (null source or allocation failure)");
        OwnedStruct { ptr }
    }
}

// }}}
// }}}
// {{{ IOP Env

/// Main-thread handle to an `iop_env_t`.
///
/// `iop_env_t` is **not** itself thread-safe: package registration and the
/// handle's (non-atomic) refcount are main-thread-only. The thread-safe unit
/// is a *context snapshot* — obtain one with [`Env::acquire`] and share that
/// [`EnvCtx`] across threads for read-only IOP operations.
///
/// `Env` holds a raw `*mut iop_env_t` and is therefore neither `Send` nor
/// `Sync`: it stays on the thread that owns the env (registration, drop).
pub struct Env {
    env: *mut iop_env_t,
    owned: bool,
}

impl Env {
    /// Create a new owned IOP env.
    ///
    /// # Panics
    ///
    /// `iop_env_new()` returns a NULL pointer.
    #[must_use]
    pub fn new() -> Self {
        let env = unsafe { iop_env_new() };
        assert!(!env.is_null(), "iop_env_new returned NULL");
        Self { env, owned: true }
    }

    /// Create a non-owned Rust IOP env from an existing C IOP env.
    ///
    /// The returned `Env` does NOT release the underlying env on drop.
    ///
    /// # Panics
    ///
    /// Panics if `env` is NULL.
    ///
    /// # Safety
    ///
    /// `env` must point to a valid `iop_env_t` whose lifetime exceeds the
    /// returned `Env`.
    #[must_use]
    pub unsafe fn from_ptr(env: *mut iop_env_t) -> Self {
        assert!(!env.is_null(), "Env::from_ptr called with NULL");
        Self { env, owned: false }
    }

    /// Retrieve the C IOP env pointer.
    #[must_use]
    pub fn as_ptr(&self) -> *const iop_env_t {
        self.env
    }

    /// Retrieve the C IOP env pointer as mutable.
    #[must_use]
    pub fn as_mut_ptr(&mut self) -> *mut iop_env_t {
        self.env
    }

    /// Register some IOP packages in the IOP env.
    ///
    /// Internally does copy-modify-swap on the underlying `ArcSwap`, so
    /// concurrent readers on other threads keep seeing the previous
    /// registered set until the swap commits.
    pub fn register_packages(&mut self, pkgs: &[*const iop_pkg_t]) {
        unsafe {
            iop_register_packages(self.env, pkgs.as_ptr(), pkgs.len() as i32);
        };
    }

    /// Acquire a refcounted snapshot of the env's current IOP context.
    ///
    /// The returned [`EnvCtx`] is a thread-safe, immutable view of the IOP
    /// objects registered at acquisition time. It can be moved/shared to
    /// other threads for read-only IOP operations and keeps its descriptors
    /// valid until dropped, even if the env registers/unregisters packages
    /// (or a backing DSO is closed) concurrently.
    #[must_use]
    pub fn acquire(&self) -> EnvCtx {
        let guard = unsafe { iop_env_ctx_acquire(self.env) };
        EnvCtx { guard }
    }
}

/// A thread-safe, refcounted snapshot of an [`Env`]'s IOP context.
///
/// Obtained via [`Env::acquire`]. It owns an immutable, atomically-refcounted
/// `ArcSwap` snapshot of the registered IOP objects, so it is `Send + Sync`
/// and may be handed to worker threads while the (main-thread-bound) [`Env`]
/// keeps being updated. The snapshot — and every descriptor reachable through
/// it — stays valid until the `EnvCtx` is dropped.
pub struct EnvCtx {
    guard: iop_env_ctx_guard_t,
}

unsafe impl Send for EnvCtx {}
unsafe impl Sync for EnvCtx {}

impl EnvCtx {
    /// Get a IOP struct or union from its fullname.
    #[must_use]
    pub fn get_struct_desc(&self, fullname: &str) -> Option<*const iop_struct_t> {
        let fullname_lstr = lstr::from_str(fullname);
        let res = unsafe { iop_env_ctx_get_struct(self.guard.ctx, fullname_lstr.as_raw()) };

        if res.is_null() {
            return None;
        }
        Some(res)
    }

    /// Unpack an IOP struct or union as JSON on a `t_scope`.
    ///
    /// # Errors
    ///
    /// The content cannot be unpacked a valid IOP JSON for the given type.
    ///
    /// # Panics
    ///
    /// The error returned from `t_iop_junpack_ptr_ps()` is not a valid UTF-8.
    #[allow(clippy::not_unsafe_ptr_arg_deref, clippy::unwrap_in_result)]
    pub fn t_junpack_desc<'t>(
        &self,
        _t_scope: &'t TScope,
        content: &str,
        st: *const iop_struct_t,
        flags: u32,
    ) -> Result<GenericStructUnion<'t>, UnpackError> {
        SB_1k!(err);
        let mut ps = pstream_t::from(content);
        let mut out = ptr::null_mut();

        let res = unsafe {
            t_iop_junpack_ptr_ps(
                self.guard.ctx,
                ps.as_mut_ptr(),
                st,
                &raw mut out,
                flags as i32,
                err.as_mut_ptr(),
            )
        };

        if res < 0 {
            return Err(err
                .as_str()
                .expect("error should be a valid UTF-8")
                .to_owned());
        }

        Ok(GenericStructUnion::new_with_ctx(st, out, self.clone()))
    }

    /// Unpack an IOP struct or union as YAML on a `t_scope`.
    ///
    /// # Errors
    ///
    /// The content cannot be unpacked a valid IOP YAML for the given type.
    ///
    /// # Panics
    ///
    /// The error returned from `t_iop_yunpack_ptr_ps()` is not a valid UTF-8.
    #[allow(clippy::not_unsafe_ptr_arg_deref, clippy::unwrap_in_result)]
    pub fn t_yunpack_desc<'t>(
        &self,
        _t_scope: &'t TScope,
        content: &str,
        st: *const iop_struct_t,
        flags: u32,
    ) -> Result<GenericStructUnion<'t>, UnpackError> {
        SB_1k!(err);
        let mut ps = pstream_t::from(content);
        let mut out = ptr::null_mut();

        let res = unsafe {
            t_iop_yunpack_ptr_ps(
                self.guard.ctx,
                ps.as_mut_ptr(),
                st,
                &raw mut out,
                flags,
                ptr::null_mut(),
                err.as_mut_ptr(),
            )
        };

        if res < 0 {
            return Err(err
                .as_str()
                .expect("error should be a valid UTF-8")
                .to_owned());
        }

        Ok(GenericStructUnion::new_with_ctx(st, out, self.clone()))
    }
}

impl Clone for EnvCtx {
    fn clone(&self) -> Self {
        let guard = unsafe { iop_env_ctx_dup(self.guard) };
        EnvCtx { guard }
    }
}

impl Drop for EnvCtx {
    fn drop(&mut self) {
        unsafe {
            iop_env_ctx_release(self.guard);
        }
    }
}

impl Default for Env {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for Env {
    fn drop(&mut self) {
        if self.owned {
            let mut env_ptr = self.env;
            unsafe {
                iop_env_delete(&raw mut env_ptr);
            }
        }
    }
}

// }}}
// {{{ Macros

/// Create a new IOP struct/class with optional field initialization, or a union with a required
/// variant.
///
/// # Usage
///
/// ```ignore
/// // For structs: create with default values
/// let tracer = iop_new!(ic__tracer);
///
/// // For structs: create with field initialization
/// let tracer = iop_new!(ic__tracer, {
///     token: 123,
///     epoch: 456,
/// });
///
/// // For unions: must specify a variant (no-args form is a compile error)
/// let scalar = iop_new!(yaml__scalar_value, { u: 123 });
/// ```
///
/// The type name should be the IOP type name without the `__t` suffix (e.g., `ic__tracer`
/// for `ic__tracer__t`).
#[macro_export]
macro_rules! iop_new {
    // No fields: only works for structs (unions don't implement default_new())
    ($type:ident) => {{
        $crate::paste::paste! {
            <[<$type __t>]>::default_new()
        }
    }};
    // With fields: works for both structs and unions
    ($type:ident, { $($field:ident : $value:expr),* $(,)? }) => {{
        $crate::paste::paste! {
            let mut obj = <[<$type __t>]>::new();
            $(
                obj.[<$field __set>]($value);
            )*
            obj
        }
    }};
}

/// Set multiple fields on an IOP struct or union.
///
/// # Usage
///
/// ```ignore
/// let mut tracer = iop_new!(ic__tracer);
/// iop_set!(tracer, {
///     token: 123,
///     epoch: 456,
/// });
/// ```
#[macro_export]
macro_rules! iop_set {
    ($obj:expr, { $($field:ident : $value:expr),* $(,)? }) => {{
        $crate::paste::paste! {
            $(
                ($obj).[<$field __set>]($value);
            )*
        }
    }};
}

/// Internal helper macro for `iop_get!` optional chaining.
///
/// Once a `?` is encountered in the field path, this macro takes over to ensure
/// the entire expression returns `Option<T>`. It wraps terminal values in `Some()`
/// and uses `.and_then()` for nested `?` to flatten `Option<Option<T>>` into `Option<T>`.
#[doc(hidden)]
#[macro_export]
macro_rules! __iop_get_opt {
    // Optional chain: field?.rest → and_then
    ($obj:expr, $field:ident ? . $($rest:tt)+) => {
        $crate::paste::paste! {
            ($obj).[<$field __get>]().and_then(|__iop_v| {
                $crate::__iop_get_opt!(__iop_v, $($rest)+)
            })
        }
    };
    // Unwrap chain: field!.rest → expect then continue
    ($obj:expr, $field:ident ! . $($rest:tt)+) => {
        $crate::paste::paste! {
            $crate::__iop_get_opt!(
                ($obj).[<$field __get>]()
                    .expect(concat!("IOP field '", stringify!($field), "' is None")),
                $($rest)+
            )
        }
    };
    // Plain chain: field.rest
    ($obj:expr, $field:ident . $($rest:tt)+) => {
        $crate::paste::paste! {
            $crate::__iop_get_opt!(($obj).[<$field __get>](), $($rest)+)
        }
    };
    // Terminal unwrap
    ($obj:expr, $field:ident !) => {
        $crate::paste::paste! {
            Some(($obj).[<$field __get>]()
                .expect(concat!("IOP field '", stringify!($field), "' is None")))
        }
    };
    // Terminal simple access
    ($obj:expr, $field:ident) => {
        $crate::paste::paste! { Some(($obj).[<$field __get>]()) }
    };
}

/// Get a field value from an IOP struct or union.
///
/// # Usage
///
/// ```ignore
/// let tracer = iop_new!(ic__tracer, { token: 123, epoch: 456 });
///
/// // Simple field access
/// let token = iop_get!(tracer, token);
///
/// // Chained access for nested structs
/// let nested_val = iop_get!(obj, nested.field);
///
/// // Optional field unwrap (panics if None)
/// let required_val = iop_get!(obj, optional_field!);
///
/// // Unwrap then chain
/// let nested_val = iop_get!(obj, optional_struct!.field);
///
/// // Optional chaining (returns None if the optional field is None)
/// let maybe_val: Option<i32> = iop_get!(obj, optional_struct?.field);
/// ```
#[macro_export]
macro_rules! iop_get {
    // Optional chain: field?.rest → transitions to __iop_get_opt
    ($obj:expr, $field:ident ? . $($rest:tt)+) => {
        $crate::paste::paste! {
            ($obj).[<$field __get>]().and_then(|__iop_v| {
                $crate::__iop_get_opt!(__iop_v, $($rest)+)
            })
        }
    };
    // Unwrap then chain: field!.rest
    ($obj:expr, $field:ident ! . $($rest:tt)+) => {
        $crate::paste::paste! {
            $crate::iop_get!(
                ($obj).[<$field __get>]()
                    .expect(concat!("IOP field '", stringify!($field), "' is None")),
                $($rest)+
            )
        }
    };
    // Chained access: field.rest
    ($obj:expr, $field:ident . $($rest:tt)+) => {
        $crate::paste::paste! {
            $crate::iop_get!(($obj).[<$field __get>](), $($rest)+)
        }
    };
    // Optional unwrap: field! (panics if None)
    ($obj:expr, $field:ident !) => {
        $crate::paste::paste! {
            ($obj).[<$field __get>]()
                .expect(concat!("IOP field '", stringify!($field), "' is None"))
        }
    };
    // Simple field access
    ($obj:expr, $field:ident) => {
        $crate::paste::paste! { ($obj).[<$field __get>]() }
    };
}

// }}}
// {{{ Tests

#[cfg(test)]
mod env_send_sync_assertions {
    use super::EnvCtx;

    const fn assert_send_sync<T: Send + Sync>() {}
    // The env *handle* is intentionally !Send + !Sync (main-thread
    // bound); the shareable, thread-safe unit is the ctx snapshot.
    // Statically check that iop::Env implements Send + Sync.
    const _: () = assert_send_sync::<EnvCtx>();
}

// }}}
