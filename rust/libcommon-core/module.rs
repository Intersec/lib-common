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
//! # use libcommon_core::c_module;
//! # use libcommon_core::bindings::module_t;
//!
//! #[derive(Default)]
//! struct MyModuleContext {
//!     data: Vec<String>,
//! }
//!
//! c_module!(my_module, MyModuleContext, |builder| {
//!     builder
//!         .initialize(|_arg| {
//!             println!("module initialized");
//!             Ok(())
//!         })
//!         .shutdown(|| {
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
//! # use libcommon_core::bindings::module_t;
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
//! # use libcommon_core::c_module;
//! # use libcommon_core::bindings::module_t;
//!
//! // No context needed
//! c_module!(my_module, |builder| {
//!     builder
//!         .initialize(|_arg| {
//!             println!("module initialized");
//!             Ok(())
//!         })
//!         .shutdown(|| {
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
//! # use libcommon_core::bindings::module_t;
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
    log_get_module, module_add_dep, module_get_name, module_implement, module_register, module_t,
};
use crate::lstr;

#[cfg(debug_assertions)]
use crate::bindings::thr_assert_is_main_thread;

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
    module: *mut module_t,
    initialize: Option<InitializeFn>,
    shutdown: Option<ShutdownFn>,
    ctx: Option<T>,
}

impl<T> InternalModule<T>
where
    T: ModuleContext,
{
    /// Create a new internal module in a constant way.
    pub const fn new_const() -> Self {
        Self {
            module: ptr::null_mut(),
            initialize: None,
            shutdown: None,
            ctx: None,
        }
    }

    /// Create the context and call the initialize closure.
    pub fn initialize(&mut self, arg: *mut c_void) -> c_int {
        self.ctx = Some(T::with_arg(arg));
        if let Some(f) = &self.initialize
            && let Err(e) = f(arg)
        {
            let module_name_lstr = unsafe { lstr::from_ptr(module_get_name(self.module)) };
            let module_name_rs_str = unsafe { module_name_lstr.as_str_unchecked() };

            eprintln!("error during initialization of module `{module_name_rs_str}`: {e}",);
            return -1;
        }
        0
    }

    /// Call the shutdown closure and drop the context.
    pub fn shutdown(&mut self) -> c_int {
        let result = {
            if let Some(f) = &self.shutdown
                && let Err(e) = f()
            {
                let module_name_lstr = unsafe { lstr::from_ptr(module_get_name(self.module)) };
                let module_name_rs_str = unsafe { module_name_lstr.as_str_unchecked() };

                eprintln!("error during shutdown of module `{module_name_rs_str}`: {e}",);
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
        self.ctx.as_mut().expect("module is not initialized")
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

type InitializeFn = Box<dyn Fn(*mut c_void) -> Result<(), Box<dyn Error>> + 'static>;
type ShutdownFn = Box<dyn Fn() -> Result<(), Box<dyn Error>> + 'static>;

/// Builder for configuring a C module's behavior.
///
/// This builder allows you to specify:
/// - An initialization function called when the module is loaded
/// - A shutdown function called when the module is unloaded
/// - Module dependencies (what this module depends on)
/// - Reverse dependencies (what modules depend on this one)
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
    /// # use libcommon_core::bindings::module_t;
    ///
    /// c_module!(my_module, |builder| {
    ///     builder.initialize(|_arg| {
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
        F: Fn(*mut c_void) -> Result<(), Box<dyn Error>> + 'static,
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
    /// # use libcommon_core::bindings::module_t;
    ///
    /// c_module!(my_module, |builder| {
    ///     builder.shutdown(|| {
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
        F: Fn() -> Result<(), Box<dyn Error>> + 'static,
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
    /// # use libcommon_core::bindings::module_t;
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
    /// # Example
    ///
    /// ```
    /// # use libcommon_core::c_module;
    /// # use libcommon_core::bindings::module_t;
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

    /// Create a new module builder for the given internal module.
    ///
    /// This creates the C module for the internal module.
    /// This is called internally by the `c_module!()` macro.
    ///
    /// It should not be called directly.
    #[doc(hidden)]
    pub fn from_internal_module(
        internal_module: &'static mut InternalModule<T>,
        name: &'static str,
        initialize: unsafe extern "C" fn(*mut c_void) -> c_int,
        shutdown: unsafe extern "C" fn() -> c_int,
    ) -> Self {
        debug_assert!(
            internal_module.module.is_null(),
            "module `{name}` has already been created",
        );

        unsafe {
            let ptr = module_register(lstr::raw(name));

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
/// # use libcommon_core::bindings::{module_release, module_require, module_t};
/// # use libcommon_core::c_module;
///
/// # #[derive(Default)]
/// # struct MyModuleContext {
/// # }
/// #
/// # c_module!(my_module, MyModuleContext);
/// #
/// # fn main() {
/// #     unsafe {
/// #         module_require(my_module_get_module());
/// #     }
/// #
/// let ctx: &mut MyModuleContext = my_module_c_mod::get_ctx();
/// #
/// #     unsafe {
/// #         module_release(my_module_get_module());
/// #     }
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
                use $crate::bindings::module_t;

                static mut MODULE:
                    $crate::module::InternalModule<super::[<$name _ModuleContextType>]> =
                        $crate::module::InternalModule::new_const();

                extern "C" fn initialize(arg: *mut c_void) -> c_int {
                    unsafe { MODULE.initialize(arg) }
                }

                extern "C" fn shutdown() -> c_int {
                    unsafe { MODULE.shutdown() }
                }

                pub(super) fn get_ctx() -> &'static mut super::[<$name _ModuleContextType>] {
                    unsafe { MODULE.ctx() }
                }

                pub(super) fn get_module() -> *mut module_t {
                    unsafe {
                        if MODULE.module().is_null() {
                            let mut builder = $crate::module::ModuleBuilder::from_internal_module(
                                &mut MODULE, stringify!($name), initialize, shutdown);

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
            pub extern "C" fn [<$name _get_module>]() -> *mut module_t {
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
            pub static [<$name _method>]: $crate::bindings::module_method_t =
                $crate::bindings::module_method_t {
                    type_: $crate::bindings::module_method_type_t::[<METHOD_ $type>],
                    order: $crate::bindings::module_order_t::[<MODULE_ $order>],
                };
        }
    };
}

// }}}
