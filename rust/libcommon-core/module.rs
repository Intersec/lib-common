/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
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

//! Infrastructure for creating C-compatible modules from Rust.
//!
//! This module provides the [`ModuleBuilder`] builder pattern and the [`c_module!`] macro
//! to create modules that can be registered and managed by the C module system.
//!
//! C module methods can be created via the [`c_module_method!`] macro.
//!
//! # Overview
//!
//! The module system allows Rust code to register modules with:
//! - Initialization and shutdown callbacks
//! - Module dependencies
//! - A type-safe context accessible throughout the module's lifetime
//!
//! # Examples
//!
//! ## Module with context and callbacks
//!
//! ```
//! # use std::os::raw::c_void;
//! # use libcommon_core::c_module;
//!
//! #[derive(Default)]
//! struct MyModuleContext {
//!     data: Vec<String>,
//! }
//!
//! c_module!(my_module, MyModuleContext, |builder| {
//!     builder
//!         .initialize(|_ctx: &mut MyModuleContext, _arg: *mut c_void| {
//!             println!("module initialized");
//!             Ok(())
//!         })
//!         .shutdown(|_ctx: &mut MyModuleContext| {
//!             println!("module shutdown");
//!             Ok(())
//!         });
//! });
//!
//! # fn main() {
//! #     my_module_get_module();
//! # }
//! ```
//!
//! ## Module with context and without callbacks
//!
//! ```
//! # use libcommon_core::c_module;
//!
//! #[derive(Default)]
//! struct SimpleContext {
//!     counter: usize,
//! }
//!
//! // No initialization or shutdown needed
//! c_module!(simple_module, SimpleContext);
//!
//! # fn main() {
//! #     simple_module_get_module();
//! # }
//! ```
//!
//! ## Module without context and with callbacks
//!
//! ```
//! # use std::os::raw::c_void;
//! # use libcommon_core::c_module;
//!
//! // No context needed
//! c_module!(my_module, |builder| {
//!     builder
//!         .initialize(|_ctx, _arg: *mut c_void| {
//!             println!("module initialized");
//!             Ok(())
//!         })
//!         .shutdown(|_ctx| {
//!             println!("module shutdown");
//!             Ok(())
//!         });
//! });
//!
//! # fn main() {
//! #     my_module_get_module();
//! # }
//! ```
//!
//! ## Module without context or callbacks
//!
//! ```
//! # use libcommon_core::c_module;
//!
//! // No context, initialization or shutdown needed
//! c_module!(simple_module);
//!
//! # fn main() {
//! #     simple_module_get_module();
//! # }
//! ```

#[doc(inline)]
#[allow(clippy::module_name_repetitions)]
pub use crate::{c_module, c_module_method};

use std::error::Error;
use std::os::raw::{c_int, c_void};
use std::ptr;

use crate::bindings::{
    data_t, log_get_module, module_add_dep, module_implement, module_implement_method_generic,
    module_implement_method_int, module_implement_method_ptr, module_implement_method_void,
    module_method_type_t, module_register, module_run_method,
};
use crate::lstr;

#[cfg(debug_assertions)]
use crate::bindings::thr_assert_is_main_thread;

/// Reexport `module_t` and `module_method_t` as they are heavily used by this module.
#[allow(clippy::module_name_repetitions)]
pub use crate::bindings::{module_method_t, module_t};

// {{{ Internal types

type InitializeFn<T> = Box<dyn Fn(&mut T, *mut c_void) -> Result<(), Box<dyn Error>>>;
type ShutdownFn<T> = Box<dyn Fn(&mut T) -> Result<(), Box<dyn Error>>>;
type MethodImplVoid = Box<dyn Fn()>;
type MethodImplInt = Box<dyn Fn(c_int)>;
type MethodImplPtr = Box<dyn Fn(*mut c_void)>;
type MethodImplGeneric = Box<dyn Fn(data_t)>;

enum MethodImplContainer {
    Void(MethodImplVoid),
    Int(MethodImplInt),
    Ptr(MethodImplPtr),
    Generic(MethodImplGeneric),
}

// }}}
// {{{ Method implementation helpers

unsafe extern "C" fn method_impl_void(custom_data: *mut c_void) {
    let cb_ref: &MethodImplVoid = unsafe { &*(custom_data as *const MethodImplVoid) };

    cb_ref();
}

unsafe extern "C" fn method_impl_int(arg: c_int, custom_data: *mut c_void) {
    let cb_ref: &MethodImplInt = unsafe { &*(custom_data as *const MethodImplInt) };

    cb_ref(arg);
}

unsafe extern "C" fn method_impl_ptr(arg: *mut c_void, custom_data: *mut c_void) {
    let cb_ref: &MethodImplPtr = unsafe { &*(custom_data as *const MethodImplPtr) };

    cb_ref(arg);
}

unsafe extern "C" fn method_impl_generic(arg: data_t, custom_data: *mut c_void) {
    let cb_ref: &MethodImplGeneric = unsafe { &*(custom_data as *const MethodImplGeneric) };

    cb_ref(arg);
}

// }}}
// {{{ InternalModule

/// Internal module storage for C module used by the macro `c_module!()`.
///
/// It should not be used directly.
#[allow(clippy::module_name_repetitions)]
#[doc(hidden)]
pub struct InternalModule<T>
where
    T: ModuleContext,
{
    name: &'static str,
    module: *mut module_t,
    initialize: Option<InitializeFn<T>>,
    shutdown: Option<ShutdownFn<T>>,
    method_impls: Vec<MethodImplContainer>,
    ctx: Option<T>,
}

impl<T> InternalModule<T>
where
    T: ModuleContext,
{
    /// Create a new internal module in a constant way.
    pub const fn new_const(name: &'static str) -> Self {
        Self {
            name,
            module: ptr::null_mut(),
            initialize: None,
            shutdown: None,
            method_impls: Vec::new(),
            ctx: None,
        }
    }

    /// Create the context and call the initialize closure.
    pub fn initialize(&mut self, arg: *mut c_void) -> c_int {
        self.ctx = Some(T::with_arg(arg));

        #[allow(clippy::missing_panics_doc)]
        let ctx_ref = self
            .ctx
            .as_mut()
            .expect("unable to get the context that was just created");

        if let Some(f) = &self.initialize
            && let Err(e) = f(ctx_ref, arg)
        {
            eprintln!("error during initialization of module `{}`: {e}", self.name);
            return -1;
        }
        0
    }

    /// Call the shutdown closure and drop the context.
    pub fn shutdown(&mut self) -> c_int {
        #[allow(clippy::missing_panics_doc)]
        let ctx_ref = self.ctx.as_mut().expect("module was not initialized");

        let result = {
            if let Some(f) = &self.shutdown
                && let Err(e) = f(ctx_ref)
            {
                eprintln!("error during shutdown of module `{}`: {e}", self.name);
                return -1;
            }
            0
        };
        self.ctx = None;
        result
    }

    /// Get the current module.
    ///
    /// The pointer can be NULL.
    pub fn module(&self) -> *mut module_t {
        #[cfg(debug_assertions)]
        unsafe {
            thr_assert_is_main_thread();
        }
        self.module
    }

    /// Get the initialized context.
    ///
    /// # Panics
    ///
    /// Panic if the module has not been initialized.
    pub fn ctx(&mut self) -> &mut T {
        #[cfg(debug_assertions)]
        unsafe {
            thr_assert_is_main_thread();
        }

        self.ctx.as_mut().unwrap_or_else(|| {
            panic!("module `{}` is not initialized", self.name);
        })
    }
}

// }}}
// {{{ ModuleContext

/// Trait to build a module context.
///
/// For structs that implements `Default`, a blanket implementation is available which ignore
/// `arg`.
#[allow(clippy::module_name_repetitions)]
pub trait ModuleContext {
    /// Build the module context with the argument.
    fn with_arg(arg: *mut c_void) -> Self;
}

/// Blanket implementation for `default` structs which ignore `arg`.
impl<T> ModuleContext for T
where
    T: Default,
{
    fn with_arg(_arg: *mut c_void) -> Self {
        Self::default()
    }
}

// }}}
// {{{ ModuleBuilder

/// Builder for configuring a C module's behavior.
///
/// This builder allows you to specify:
/// - An initialization function called when the module is loaded
/// - A shutdown function called when the module is unloaded
/// - Module dependencies (what this module depends on)
/// - Reverse dependencies (what modules depend on this one)
/// - Method implementations
#[allow(clippy::module_name_repetitions)]
pub struct ModuleBuilder<T>
where
    T: ModuleContext + 'static,
{
    internal_module: &'static mut InternalModule<T>,
}

impl<T> ModuleBuilder<T>
where
    T: ModuleContext,
{
    /// Set the initialize function for the module.
    ///
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::c_module;
    ///
    /// c_module!(my_module, |builder| {
    ///     builder.initialize(|_ctx, _arg| {
    ///         println!("module initialized");
    ///         Ok(())
    ///     });
    /// });
    ///
    /// # fn main() {
    /// #     my_module_get_module();
    /// # }
    /// ```
    pub fn initialize<F>(&mut self, f: F) -> &mut Self
    where
        F: Fn(&mut T, *mut c_void) -> Result<(), Box<dyn Error>> + 'static,
    {
        self.internal_module.initialize = Some(Box::new(f));
        self
    }

    /// Set the shutdown function for the module.
    ///
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::c_module;
    ///
    /// c_module!(my_module, |builder| {
    ///     builder.shutdown(|_ctx| {
    ///         println!("module shutdown");
    ///         Ok(())
    ///     });
    /// });
    ///
    /// # fn main() {
    /// #     my_module_get_module();
    /// # }
    /// ```
    pub fn shutdown<F>(&mut self, f: F) -> &Self
    where
        F: Fn(&mut T) -> Result<(), Box<dyn Error>> + 'static,
    {
        self.internal_module.shutdown = Some(Box::new(f));
        self
    }

    /// Add a dependency from this module to an other module.
    ///
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::c_module;
    ///
    /// c_module!(other_module);
    ///
    /// c_module!(my_module, |builder| {
    ///     builder.depends_on(other_module_get_module());
    /// });
    ///
    /// # fn main() {
    /// #     my_module_get_module();
    /// # }
    /// ```
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn depends_on(&mut self, dep: *mut module_t) -> &mut Self {
        unsafe {
            module_add_dep(self.internal_module.module, dep);
        }
        self
    }

    /// Add a dependency from an other module to this module.
    ///
    /// # Warning
    ///
    /// Unlike in C where the module builder function is called at the startup of the program
    /// (if the linker does not prune the compilation unit), in Rust, the module builder callback is
    /// called on the first call of `<module_name>_get_module()` function.
    ///
    /// This means that the dependency injection done by using `needed_by()` is only effective
    /// once this module is used with `<module_name>_get_module()` (or the C macro `MODULE()`).
    ///
    /// So, in order to use `needed_by()` from an other module to this module, it is required to
    /// call `other_module_get_module()` at least once before loading this module.
    ///
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::c_module;
    ///
    /// c_module!(other_module);
    ///
    /// c_module!(my_module, |builder| {
    ///     builder.needed_by(other_module_get_module());
    /// });
    ///
    /// # fn main() {
    /// #     my_module_get_module();
    /// # }
    /// ```
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    pub fn needed_by(&mut self, need: *mut module_t) -> &mut Self {
        unsafe {
            module_add_dep(need, self.internal_module.module);
        }
        self
    }

    /// Implement a VOID method.
    ///
    /// This is the equivalent of the C macro `MODULE_IMPLEMENTS_VOID()`.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `method.type_` is `METHOD_VOID`.
    ///
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::{c_module, c_module_method};
    /// # use libcommon_core::module::{
    /// #     module_require, module_release, method_run_void,
    /// # };
    /// #
    /// c_module_method!(VOID, DEPS_BEFORE, do_something);
    ///
    /// #[derive(Default)]
    /// struct MyCtx { }
    ///
    /// c_module!(my_module, MyCtx, |builder| {
    ///     builder.implement_void(&raw const do_something_method, |_ctx: &mut MyCtx| {
    ///         println!("Done something!");
    ///     });
    /// });
    ///
    /// # fn main() {
    /// #     module_require(my_module_get_module());
    /// #     method_run_void(&raw const do_something_method);
    /// #     module_release(my_module_get_module());
    /// # }
    /// ```
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    #[allow(clippy::unnecessary_safety_doc)]
    pub fn implement_void<F>(&mut self, method: *const module_method_t, cb: F)
    where
        F: Fn(&mut T) + 'static,
    {
        let internal_mod_ptr = &raw mut *self.internal_module;
        let trampoline_cb = move || {
            // XXX: This is very ugly, but we know that the pointer will live longer than its use
            // in the module implementation as it is guarantee to be static.
            let internal_mod_ref = unsafe { &mut *internal_mod_ptr };
            cb(internal_mod_ref.ctx());
        };

        self.internal_module
            .method_impls
            .push(MethodImplContainer::Void(Box::new(trampoline_cb)));

        #[allow(clippy::missing_panics_doc)]
        let container_ref = self
            .internal_module
            .method_impls
            .last_mut()
            .expect("retrieve the last value just inserted");

        #[allow(clippy::missing_panics_doc)]
        let MethodImplContainer::Void(cb_ref) = &container_ref else {
            panic!("func void just inserted");
        };
        let cb_ref: &MethodImplVoid = cb_ref;
        let cb_ptr: *const MethodImplVoid = &raw const *cb_ref;

        unsafe {
            module_implement_method_void(
                self.internal_module.module,
                method,
                Some(method_impl_void),
                cb_ptr as *mut c_void,
            );
        }
    }

    /// Implement an INT method.
    ///
    /// This is the equivalent of the C macro `MODULE_IMPLEMENTS_INT()`.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `method.type_` is `METHOD_INT`.
    ///
    /// # Example
    ///
    /// ```
    /// # use std::os::raw::c_int;
    /// # use libcommon_core::{c_module, c_module_method};
    /// # use libcommon_core::module::{
    /// #     module_require, module_release, method_run_int,
    /// # };
    ///
    /// c_module_method!(INT, DEPS_BEFORE, do_something);
    ///
    /// #[derive(Default)]
    /// struct MyCtx { }
    ///
    /// c_module!(my_module, MyCtx, |builder| {
    ///     builder.implement_int(&raw const do_something_method, |_ctx: &mut MyCtx, a: c_int| {
    ///         println!("Done something! {a}");
    ///     });
    /// });
    ///
    /// # fn main() {
    /// #     module_require(my_module_get_module());
    /// #     method_run_int(&raw const do_something_method, 42);
    /// #     module_release(my_module_get_module());
    /// # }
    /// ```
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    #[allow(clippy::unnecessary_safety_doc)]
    pub fn implement_int<F>(&mut self, method: *const module_method_t, cb: F)
    where
        F: Fn(&mut T, c_int) + 'static,
    {
        let internal_mod_ptr = &raw mut *self.internal_module;
        let trampoline_cb = move |arg: c_int| {
            // XXX: This is very ugly, but we know that the pointer will live longer than its use
            // in the module implementation as it is guarantee to be static.
            let internal_mod_ref = unsafe { &mut *internal_mod_ptr };
            cb(internal_mod_ref.ctx(), arg);
        };

        self.internal_module
            .method_impls
            .push(MethodImplContainer::Int(Box::new(trampoline_cb)));

        #[allow(clippy::missing_panics_doc)]
        let container_ref = self
            .internal_module
            .method_impls
            .last_mut()
            .expect("retrieve the last value just inserted");

        #[allow(clippy::missing_panics_doc)]
        let MethodImplContainer::Int(cb_ref) = &container_ref else {
            panic!("func int just inserted");
        };
        let cb_ref: &MethodImplInt = cb_ref;
        let cb_ptr: *const MethodImplInt = &raw const *cb_ref;

        unsafe {
            module_implement_method_int(
                self.internal_module.module,
                method,
                Some(method_impl_int),
                cb_ptr as *mut c_void,
            );
        }
    }

    /// Implement a PTR method.
    ///
    /// This is the equivalent of the C macro `MODULE_IMPLEMENTS_PTR()`.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `method.type_` is `METHOD_PTR`.
    ///
    /// # Example
    ///
    /// ```
    /// # use std::os::raw::c_void;
    /// # use libcommon_core::{c_module, c_module_method};
    /// # use libcommon_core::module::{
    /// #     module_require, module_release, method_run_ptr,
    /// # };
    ///
    /// c_module_method!(PTR, DEPS_BEFORE, do_something);
    ///
    /// #[derive(Default)]
    /// struct MyCtx { }
    ///
    /// c_module!(my_module, MyCtx, |builder| {
    ///     builder.implement_ptr(
    ///         &raw const do_something_method,
    ///         |_ctx: &mut MyCtx, p: *mut c_void| {
    ///             println!("Done something! {:p}", p);
    ///         },
    ///     );
    /// });
    ///
    /// # fn main() {
    /// #     module_require(my_module_get_module());
    /// #     method_run_ptr(&raw const do_something_method, std::ptr::null_mut());
    /// #     module_release(my_module_get_module());
    /// # }
    /// ```
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    #[allow(clippy::unnecessary_safety_doc)]
    pub fn implement_ptr<F>(&mut self, method: *const module_method_t, cb: F)
    where
        F: Fn(&mut T, *mut c_void) + 'static,
    {
        let internal_mod_ptr = &raw mut *self.internal_module;
        let trampoline_cb = move |arg: *mut c_void| {
            // XXX: This is very ugly, but we know that the pointer will live longer than its use
            // in the module implementation as it is guarantee to be static.
            let internal_mod_ref = unsafe { &mut *internal_mod_ptr };
            cb(internal_mod_ref.ctx(), arg);
        };

        self.internal_module
            .method_impls
            .push(MethodImplContainer::Ptr(Box::new(trampoline_cb)));

        #[allow(clippy::missing_panics_doc)]
        let container_ref = self
            .internal_module
            .method_impls
            .last_mut()
            .expect("retrieve the last value just inserted");

        #[allow(clippy::missing_panics_doc)]
        let MethodImplContainer::Ptr(cb_ref) = &container_ref else {
            panic!("func int just inserted");
        };
        let cb_ref: &MethodImplPtr = cb_ref;
        let cb_ptr: *const MethodImplPtr = &raw const *cb_ref;

        unsafe {
            module_implement_method_ptr(
                self.internal_module.module,
                method,
                Some(method_impl_ptr),
                cb_ptr as *mut c_void,
            );
        }
    }

    /// Implement a GENERIC method.
    ///
    /// This is the equivalent of the C macro `MODULE_IMPLEMENTS_GENERIC()`.
    ///
    /// # Safety
    ///
    /// The caller must ensure that `method.type_` is `METHOD_GENERIC`.
    ///
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::{c_module, c_module_method};
    /// # use libcommon_core::module::{
    /// #     module_require, module_release, method_run_generic,
    /// # };
    /// # use libcommon_core::bindings::data_t;
    ///
    /// c_module_method!(GENERIC, DEPS_BEFORE, do_something);
    ///
    /// #[derive(Default)]
    /// struct MyCtx { }
    ///
    /// c_module!(my_module, MyCtx, |builder| {
    ///     builder.implement_generic(
    ///         &raw const do_something_method,
    ///         |ctx: &mut MyCtx, d: data_t| {
    ///             println!("Done something! {:p}", unsafe { d.ptr });
    ///         },
    ///     );
    /// });
    ///
    /// # fn main() {
    /// #     module_require(my_module_get_module());
    /// #     method_run_generic(&raw const do_something_method,
    /// #                        data_t { ptr: std::ptr::null_mut() } );
    /// #     module_release(my_module_get_module());
    /// # }
    /// ```
    #[allow(clippy::not_unsafe_ptr_arg_deref)]
    #[allow(clippy::unnecessary_safety_doc)]
    pub fn implement_generic<F>(&mut self, method: *const module_method_t, cb: F)
    where
        F: Fn(&mut T, data_t) + 'static,
    {
        let internal_mod_ptr = &raw mut *self.internal_module;
        let trampoline_cb = move |arg: data_t| {
            // XXX: This is very ugly, but we know that the pointer will live longer than its use
            // in the module implementation as it is guarantee to be static.
            let internal_mod_ref = unsafe { &mut *internal_mod_ptr };
            cb(internal_mod_ref.ctx(), arg);
        };

        self.internal_module
            .method_impls
            .push(MethodImplContainer::Generic(Box::new(trampoline_cb)));

        #[allow(clippy::missing_panics_doc)]
        let container_ref = self
            .internal_module
            .method_impls
            .last_mut()
            .expect("retrieve the last value just inserted");

        #[allow(clippy::missing_panics_doc)]
        let MethodImplContainer::Generic(cb_ref) = &container_ref else {
            panic!("func int just inserted");
        };
        let cb_ref: &MethodImplGeneric = cb_ref;
        let cb_ptr: *const MethodImplGeneric = &raw const *cb_ref;

        unsafe {
            module_implement_method_generic(
                self.internal_module.module,
                method,
                Some(method_impl_generic),
                cb_ptr as *mut c_void,
            );
        }
    }

    /// Create a new module builder for the given internal module.
    ///
    /// This creates the C module for the internal module.
    /// This is called internally by the `c_module!()` macro.
    ///
    /// It should not be called directly.
    #[doc(hidden)]
    pub fn from_internal_module(
        internal_module: &'static mut InternalModule<T>,
        initialize: unsafe extern "C" fn(*mut c_void) -> c_int,
        shutdown: unsafe extern "C" fn() -> c_int,
    ) -> Self {
        debug_assert!(
            internal_module.module.is_null(),
            "module `{}` has already been created",
            internal_module.name,
        );

        unsafe {
            let ptr = module_register(lstr::raw(internal_module.name));

            module_implement(ptr, Some(initialize), Some(shutdown), log_get_module());
            internal_module.module = ptr;
        }

        Self { internal_module }
    }
}

// }}}
// {{{ c_module!()

/// Creates a C-compatible module with a typed context.
///
/// This macro generates all the necessary boilerplate to register a module
/// with the C module system, including:
/// - A static module context with your custom type
/// - C-compatible `initialize` and `shutdown` functions
/// - A `get_module()` function that returns the module pointer
/// - An exported `<name>_get_module()` function for C code
///
/// # Parameters
///
/// - `$name`: The module name (used for registration and function naming)
/// - `$ctx`: The type of the module context (must implement `Default`)
/// - `$builder`: The identifier for the builder parameter in the configuration block
/// - `$body`: A block that configures the module using the builder
///
/// context, builder and body are optional. If not provided, the module will be registered
/// with no context, initialization or shutdown functions.
///
/// # Context Access
///
/// Within your module's code, you can access the context which is mutable using:
/// ```
/// # use libcommon_core::module::{module_release, module_require};
/// # use libcommon_core::c_module;
///
/// # #[derive(Default)]
/// # struct MyModuleContext {
/// # }
/// #
/// # c_module!(my_module, MyModuleContext);
/// #
/// # fn main() {
/// #     module_require(my_module_get_module());
/// #
/// let ctx: &mut MyModuleContext = my_module_c_mod::get_ctx();
/// #
/// #     module_release(my_module_get_module());
/// # }
/// ```
///
/// # Safety
///
/// The generated code uses `static mut` for the module context and relies on
/// the C module system to ensure proper initialization order and single-threaded
/// access during initialization/shutdown.
///
/// # Example
///
/// See module-level documentation for a complete example.
#[macro_export]
#[allow(clippy::module_name_repetitions)]
macro_rules! c_module {
    ($name:ident, $ctx:ty, |$builder:ident| $body:block) => {
        c_module!(@internal $name, $ctx, |$builder| $body);
    };
    ($name:ident, $ctx:ty) => {
        c_module!(@internal $name, $ctx, |_builder| {});
    };
    ($name:ident, |$builder:ident| $body:block) => {
        c_module!(@internal $name, (), |$builder| $body);
    };
    ($name:ident) => {
        c_module!(@internal $name, (), |_builder| {});
    };
    (@internal $name:ident, $ctx:ty, |$builder:ident| $body:block) => {
        paste::paste! {
            #[allow(static_mut_refs)]
            mod [<$name _c_mod>] {
                use std::os::raw::{c_int, c_void};
                use $crate::module::module_t;

                static mut MODULE:
                    $crate::module::InternalModule<super::[<$name _ModuleContextType>]> =
                        $crate::module::InternalModule::new_const(stringify!($name));

                extern "C" fn initialize(arg: *mut c_void) -> c_int {
                    unsafe { MODULE.initialize(arg) }
                }

                extern "C" fn shutdown() -> c_int {
                    unsafe { MODULE.shutdown() }
                }

                #[allow(unused)]
                pub(super) fn get_ctx() -> &'static mut super::[<$name _ModuleContextType>] {
                    unsafe { MODULE.ctx() }
                }

                pub(super) fn get_module() -> *mut module_t {
                    unsafe {
                        if MODULE.module().is_null() {
                            let mut builder = $crate::module::ModuleBuilder::from_internal_module(
                                &mut MODULE, initialize, shutdown);

                            super::[<$name _MODULE_BUILDER_CB>](&mut builder);
                        }
                        MODULE.module()
                    }
                }
            }

            #[allow(non_camel_case_types)]
            type [<$name _ModuleContextType>] = $ctx;

            #[allow(non_upper_case_globals)]
            static [<$name _MODULE_BUILDER_CB>]:
                fn(&mut $crate::module::ModuleBuilder<$ctx>) =
                    |$builder: &mut $crate::module::ModuleBuilder<$ctx>| $body;

            #[unsafe(no_mangle)]
            #[allow(clippy::macro_metavars_in_unsafe)]
            pub extern "C" fn [<$name _get_module>]() -> *mut $crate::module::module_t {
                [<$name _c_mod>]::get_module()
            }
        }
    };
}

// }}}
// {{{ c_module_method!()

/// Declare and define a C module method.
///
/// This works similarly to the C macro `MODULE_METHOD()` with the same `type` and `order`.
///
/// # Parameters
///
/// - `$type`: The type of the method. See `module_method_type_t`.
/// - `$order`: The order of calls of the implementations. See `module_order_t`.
/// - `$name`: The name of the method.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module_method;
///
/// c_module_method!(VOID, DEPS_BEFORE, my_method);
///
/// # fn main() { }
/// ```
///
#[macro_export]
#[allow(clippy::module_name_repetitions)]
macro_rules! c_module_method {
    ($type:ident, $order:ident, $name:ident) => {
        paste::paste! {
            #[unsafe(no_mangle)]
            pub static [<$name _method>]: $crate::module::module_method_t =
                $crate::module::module_method_t {
                    type_: $crate::bindings::module_method_type_t::[<METHOD_ $type>],
                    order: $crate::bindings::module_order_t::[<MODULE_ $order>],
                };
        }
    };
}

// }}}
// {{{ Method run functions

/// Run a VOID method.
///
/// This is equivalent of using the C macro `MODULE_METHOD_RUN_VOID()`.
///
/// # Safety
///
/// The caller must ensure that `method.type_` is `METHOD_VOID`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module_method;
/// # use libcommon_core::module::method_run_void;
/// #
/// c_module_method!(VOID, DEPS_BEFORE, do_something);
/// #
/// # fn main() {
///
/// method_run_void(&raw const do_something_method);
///
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::unnecessary_safety_doc)]
pub fn method_run_void(method: *const module_method_t) {
    unsafe {
        let data = data_t {
            ptr: ptr::null_mut(),
        };

        debug_assert_eq!((*method).type_, module_method_type_t::METHOD_VOID);
        module_run_method(method, data);
    }
}

/// Run a PTR method.
///
/// This is equivalent of using the C macro `MODULE_METHOD_RUN_PTR()`.
///
/// # Safety
///
/// The caller must ensure that `method.type_` is `METHOD_PTR`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module_method;
/// # use libcommon_core::module::method_run_ptr;
/// #
/// c_module_method!(PTR, DEPS_BEFORE, do_something);
/// #
/// # fn main() {
///
/// method_run_ptr(&raw const do_something_method, std::ptr::null_mut());
///
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::unnecessary_safety_doc)]
pub fn method_run_ptr(method: *const module_method_t, arg: *mut c_void) {
    unsafe {
        let data = data_t { ptr: arg };

        debug_assert_eq!((*method).type_, module_method_type_t::METHOD_PTR);
        module_run_method(method, data);
    }
}

/// Run an INT method.
///
/// This is equivalent of using the C macro `MODULE_METHOD_RUN_INT()`.
///
/// # Safety
///
/// The caller must ensure that `method.type_` is `METHOD_INT`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module_method;
/// # use libcommon_core::module::method_run_int;
/// #
/// c_module_method!(INT, DEPS_BEFORE, do_something);
/// #
/// # fn main() {
///
/// method_run_int(&raw const do_something_method, 42);
///
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::unnecessary_safety_doc)]
pub fn method_run_int(method: *const module_method_t, arg: c_int) {
    unsafe {
        let data = data_t { u32_: arg as u32 };

        debug_assert_eq!((*method).type_, module_method_type_t::METHOD_INT);
        module_run_method(method, data);
    }
}

/// Run a GENERIC method.
///
/// This is equivalent of using the C macro `MODULE_METHOD_RUN()`.
///
/// # Safety
///
/// The caller must ensure that `method.type_` is `METHOD_GENERIC`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module_method;
/// # use libcommon_core::module::method_run_generic;
/// # use libcommon_core::bindings::data_t;
/// #
/// c_module_method!(GENERIC, DEPS_BEFORE, do_something);
/// #
/// # fn main() {
///
/// let data = data_t { u64_: 15158448 };
/// method_run_generic(&raw const do_something_method, data);
///
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::unnecessary_safety_doc)]
pub fn method_run_generic(method: *const module_method_t, arg: data_t) {
    unsafe {
        debug_assert_eq!((*method).type_, module_method_type_t::METHOD_GENERIC);
        module_run_method(method, arg);
    }
}

// }}}
// {{{ Module require/release/provide functions

/// Safe wrapper around `module_require()`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release};
///
/// c_module!(my_module);
///
/// # fn main() {
///
/// module_require(my_module_get_module());
///
/// # module_release(my_module_get_module());
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_require(module: *mut module_t) {
    use crate::bindings::module_require as c_module_require;

    unsafe {
        c_module_require(module);
    }
}

/// Safe wrapper around `module_release()`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release};
///
/// c_module!(my_module);
///
/// # fn main() {
/// #
/// # module_require(my_module_get_module());
///
/// module_release(my_module_get_module());
///
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_release(module: *mut module_t) {
    use crate::bindings::module_release as c_module_release;

    unsafe {
        c_module_release(module);
    }
}

/// Safe wrapper around `module_provide()`.
///
/// # Warning
///
/// You need to make sure that the argument must live at least until the module is initialized.
///
/// # Example
///
/// ```
/// # use std::os::raw::c_void;
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release, module_provide};
///
/// c_module!(my_module);
///
/// static mut MY_ARG: u32 = 12;
///
/// # fn main() {
///
/// module_provide(my_module_get_module(), &raw mut MY_ARG as *mut c_void);
///
/// # module_require(my_module_get_module());
/// #
/// # module_release(my_module_get_module());
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_provide(module: *mut module_t, arg: *mut c_void) {
    use crate::bindings::module_provide as c_module_provide;

    unsafe {
        c_module_provide(module, arg);
    }
}

// }}}
// {{{ Module helpers

/// Safe wrapper around `module_is_loaded()`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release, module_is_loaded};
/// #
/// # c_module!(my_module);
/// #
/// # fn main() {
///
/// module_require(my_module_get_module());
///
/// assert!(module_is_loaded(my_module_get_module()));
///
/// # module_release(my_module_get_module());
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_is_loaded(module: *mut module_t) -> bool {
    use crate::bindings::module_is_loaded as c_module_is_loaded;

    unsafe { c_module_is_loaded(module) }
}

/// Safe wrapper around `module_is_initializing()`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release, module_is_initializing};
/// #
/// # c_module!(my_module);
/// #
/// # fn main() {
///
/// module_require(my_module_get_module());
///
/// assert!(!module_is_initializing(my_module_get_module()));
///
/// # module_release(my_module_get_module());
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_is_initializing(module: *mut module_t) -> bool {
    use crate::bindings::module_is_initializing as c_module_is_initializing;

    unsafe { c_module_is_initializing(module) }
}

/// Safe wrapper around `module_is_shutting_down()`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release, module_is_shutting_down};
/// #
/// # c_module!(my_module);
/// #
/// # fn main() {
///
/// module_require(my_module_get_module());
///
/// assert!(!module_is_shutting_down(my_module_get_module()));
///
/// # module_release(my_module_get_module());
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_is_shutting_down(module: *mut module_t) -> bool {
    use crate::bindings::module_is_shutting_down as c_module_is_shutting_down;

    unsafe { c_module_is_shutting_down(module) }
}

/// Safe wrapper around `module_get_name()`.
///
/// # Example
///
/// ```
/// # use libcommon_core::c_module;
/// # use libcommon_core::module::{module_require, module_release, module_get_name};
/// # use libcommon_core::lstr;
/// #
/// # c_module!(my_module);
/// #
/// # fn main() {
///
/// module_require(my_module_get_module());
///
/// assert_eq!(module_get_name(my_module_get_module()), "my_module");
///
/// # module_release(my_module_get_module());
/// # }
/// ```
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[allow(clippy::module_name_repetitions)]
pub fn module_get_name(module: *mut module_t) -> &'static str {
    use crate::bindings::module_get_name as c_module_get_name;

    #[allow(clippy::similar_names)]
    unsafe {
        let name_c_char = c_module_get_name(module);
        let name_lstr = lstr::from_ptr(name_c_char);

        name_lstr.as_str_unchecked()
    }
}

// }}}
// {{{ Tests

#[cfg(test)]
mod test {
    use std::os::raw::{c_int, c_void};
    use std::ptr;

    use crate::bindings::data_t;
    use crate::module::{
        method_run_generic, method_run_int, method_run_ptr, method_run_void, module_is_loaded,
        module_provide, module_release, module_require,
    };

    c_module_method!(VOID, DEPS_BEFORE, void);
    c_module_method!(INT, DEPS_BEFORE, int);
    c_module_method!(PTR, DEPS_BEFORE, ptr);
    c_module_method!(GENERIC, DEPS_BEFORE, generic);

    static mut SHUTDOWN_MY_MODULE: bool = false;

    #[derive(Default)]
    struct ModuleCtx {
        init: u32,
        void: bool,
        int: c_int,
        ptr: *mut c_void,
        generic: u32,
    }

    c_module!(depend_on_module, ModuleCtx, |builder| {
        builder.initialize(|ctx, arg| {
            ctx.init = arg.is_null().into();
            Ok(())
        });
    });

    c_module!(my_module, ModuleCtx, |builder| {
        builder.depends_on(depend_on_module_get_module());

        builder.initialize(|ctx, arg| {
            let arg_u32 = arg as *const u32;
            if !arg_u32.is_null() {
                ctx.init = unsafe { *arg_u32 };
            }
            Ok(())
        });

        builder.shutdown(|_ctx| {
            unsafe {
                SHUTDOWN_MY_MODULE = true;
            }
            Ok(())
        });

        builder.implement_void(&raw const void_method, |ctx| {
            ctx.void = true;
        });

        builder.implement_int(&raw const int_method, |ctx, arg| {
            ctx.int = arg;
        });

        builder.implement_ptr(&raw const ptr_method, |ctx, arg| {
            ctx.ptr = arg;
        });

        builder.implement_generic(&raw const generic_method, |ctx, arg| {
            ctx.generic = unsafe { arg.u32_ };
        });
    });

    c_module!(needed_by_module, ModuleCtx, |builder| {
        builder.needed_by(my_module_get_module());

        builder.implement_int(&raw const int_method, |ctx, arg| {
            ctx.int = arg;
        });
    });

    #[test]
    fn module() {
        let mut ptr_data: u32 = 20;

        // Required to inject the dependency.
        needed_by_module_get_module();

        // Provide the argument to initialize for my_module.
        module_provide(my_module_get_module(), &raw mut ptr_data as *mut c_void);

        // Check that the three modules are not loaded.
        assert!(!module_is_loaded(my_module_get_module()));
        assert!(!module_is_loaded(depend_on_module_get_module()));
        assert!(!module_is_loaded(needed_by_module_get_module()));

        // Require the three modules through dependencies.
        module_require(my_module_get_module());

        // Check that the three modules are loaded.
        assert!(module_is_loaded(my_module_get_module()));
        assert!(module_is_loaded(depend_on_module_get_module()));
        assert!(module_is_loaded(needed_by_module_get_module()));

        // Check that the modules are initialized according to the provided arguments.
        assert_eq!(my_module_c_mod::get_ctx().init, 20);
        assert_eq!(depend_on_module_c_mod::get_ctx().init, 1);
        assert_eq!(needed_by_module_c_mod::get_ctx().init, 0);

        // Call the methods.
        method_run_void(&raw const void_method);
        method_run_int(&raw const int_method, 10);
        method_run_ptr(&raw const ptr_method, &raw mut ptr_data as *mut c_void);

        let data = data_t { u32_: 48448 };
        method_run_generic(&raw const generic_method, data);

        // Check the data for my_module.
        assert!(my_module_c_mod::get_ctx().void);
        assert_eq!(my_module_c_mod::get_ctx().int, 10);
        assert_eq!(
            my_module_c_mod::get_ctx().ptr,
            &raw mut ptr_data as *mut c_void
        );
        assert_eq!(my_module_c_mod::get_ctx().generic, 48448);

        // Check the data for depend_on_module.
        assert!(!depend_on_module_c_mod::get_ctx().void);
        assert_eq!(depend_on_module_c_mod::get_ctx().int, 0);
        assert_eq!(depend_on_module_c_mod::get_ctx().ptr, ptr::null_mut());
        assert_eq!(depend_on_module_c_mod::get_ctx().generic, 0);

        // Check the data for needed_by_module_c_mod.
        assert!(!needed_by_module_c_mod::get_ctx().void);
        assert_eq!(needed_by_module_c_mod::get_ctx().int, 10);
        assert_eq!(needed_by_module_c_mod::get_ctx().ptr, ptr::null_mut());
        assert_eq!(needed_by_module_c_mod::get_ctx().generic, 0);

        // Try running `int_method` again.
        method_run_int(&raw const int_method, 40);
        assert_eq!(my_module_c_mod::get_ctx().int, 40);
        assert_eq!(depend_on_module_c_mod::get_ctx().int, 0);
        assert_eq!(needed_by_module_c_mod::get_ctx().int, 40);

        // Release the three modules.
        module_release(my_module_get_module());

        // Check that the three modules are not longer loaded, and the shutdown callback as been
        // called.
        assert!(!module_is_loaded(my_module_get_module()));
        assert!(!module_is_loaded(depend_on_module_get_module()));
        assert!(!module_is_loaded(needed_by_module_get_module()));
        assert!(unsafe { SHUTDOWN_MY_MODULE });
    }
}

// }}}
