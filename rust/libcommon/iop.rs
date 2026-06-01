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
    iop_register_packages, iop_sb_jpack, iop_struct_t, t_iop_junpack_ptr_ps, t_iop_new_desc,
    t_iop_sb_ypack, t_iop_yunpack_ptr_ps,
};

use crate::lstr;
use crate::mem_stack::TScope;
use crate::pstream::pstream_t;
use libcommon_core::{SB_1k, sb::Sb};

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
/// `_ctx` keeps the ctx snapshot `cdesc` was resolved from alive, or is
/// `None` for a static / caller-owned descriptor (via `new`).
pub struct GenericStructUnion<'a> {
    cdesc: *const iop_struct_t,
    cptr: *mut c_void,
    _ctx: Option<EnvCtx>,
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
            _ctx: None,
            _phantom: PhantomData,
        }
    }

    /// Like [`new`](Self::new) but keeps `ctx` alive so a descriptor resolved
    /// from that snapshot stays valid for the returned value's lifetime.
    fn new_with_ctx(cdesc: *const iop_struct_t, cptr: *mut c_void, ctx: EnvCtx) -> Self {
        Self {
            cdesc,
            cptr,
            _ctx: Some(ctx),
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
