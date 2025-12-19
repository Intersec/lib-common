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
//! # Overview
//!
//! The module system allows Rust code to register modules with:
//! - Initialization and shutdown callbacks
//! - Module dependencies
//! - A type-safe context accessible throughout the module's lifetime
//!
//! # Example
//!
//! ```ignore
//! use crate::c_module;
//!
//!#[derive(Default)]
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
//! ```

use std::error::Error;
use std::os::raw::c_void;

use crate::bindings::module_t;

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

pub type InitializeFn = Box<dyn Fn(*mut c_void) -> Result<(), Box<dyn Error>> + 'static>;
pub type ShutdownFn = Box<dyn Fn() -> Result<(), Box<dyn Error>> + 'static>;

/// Builder for configuring a C module's behavior.
///
/// This builder allows you to specify:
/// - An initialization function called when the module is loaded
/// - A shutdown function called when the module is unloaded
/// - Module dependencies (what this module depends on)
/// - Reverse dependencies (what modules depend on this one)
#[allow(clippy::module_name_repetitions)]
#[derive(Default)]
pub struct ModuleBuilder {
    pub initialize: Option<InitializeFn>,
    pub shutdown: Option<ShutdownFn>,
    pub dependencies: Vec<*mut module_t>,
    pub needed_by: Vec<*mut module_t>,
}

impl ModuleBuilder {
    pub fn initialize<F>(&mut self, f: F) -> &mut Self
    where
        F: Fn(*mut c_void) -> Result<(), Box<dyn Error>> + 'static,
    {
        self.initialize = Some(Box::new(f));
        self
    }

    pub fn shutdown<F>(&mut self, f: F) -> &Self
    where
        F: Fn() -> Result<(), Box<dyn Error>> + 'static,
    {
        self.shutdown = Some(Box::new(f));
        self
    }

    pub fn depends_on(&mut self, dep: *mut module_t) -> &mut Self {
        self.dependencies.push(dep);
        self
    }

    pub fn needed_by(&mut self, need: *mut module_t) -> &mut Self {
        self.needed_by.push(need);
        self
    }
}

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
/// # Context Access
///
/// Within your module's code, you can access the context which is mutable using:
/// ```ignore
/// let ctx = <name>_c_mod::get_ctx();
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
        paste::paste! {
            #[allow(clippy::absolute_paths)]
            mod [<$name _c_mod>] {
                use std::ptr;
                use std::os::raw::{c_int, c_void};
                use $crate::bindings::{
                    log_get_module, module_add_dep, module_implement, module_register, module_t,
                };
                use $crate::lstr;
                use $crate::module::ModuleContext as _;

                pub struct InternalModuleContext {
                    module: *mut module_t,
                    initialize: Option<$crate::module::InitializeFn>,
                    shutdown: Option<$crate::module::ShutdownFn>,
                    ctx: Option<$ctx>,
                }

                pub static mut MODULE : InternalModuleContext = InternalModuleContext {
                    module: ptr::null_mut(),
                    initialize: None,
                    shutdown: None,
                    ctx: None,
                };

                pub fn get_ctx() -> &'static mut $ctx {
                    #[allow(static_mut_refs)]
                    unsafe {
                        MODULE.ctx.as_mut().expect("module context not initialized")
                    }
                }

                pub extern "C" fn initialize(arg: *mut c_void) -> c_int {
                    #[allow(static_mut_refs)]
                    unsafe {
                        MODULE.ctx = Some($ctx::with_arg(arg));
                        if let Some(f) = &MODULE.initialize {
                            if let Err(e) = f(arg) {
                                eprintln!("error during initialization of module `{}`: {}",
                                    stringify!($name),
                                    e,
                                );
                                return -1
                            }
                        }
                        0
                    }
                }

                pub extern "C" fn shutdown() -> c_int {
                    #[allow(static_mut_refs)]
                    unsafe {
                        let result = {
                            if let Some(f) = &MODULE.shutdown {
                                if let Err(e) =  f() {
                                    eprintln!("error during shutdown of module `{}`: {}",
                                        stringify!($name),
                                        e,
                                    );
                                    return -1;
                                }
                            }
                            0
                        };
                        MODULE.ctx = None;
                        result
                    }
                }

                pub fn get_module() -> *mut module_t {
                    #[allow(clippy::macro_metavars_in_unsafe)]
                    unsafe {
                        if MODULE.module.is_null() {
                            let ptr = module_register(lstr::raw(stringify!($name)));

                            let mut $builder = $crate::module::ModuleBuilder::default();
                            $body;

                            MODULE.initialize = $builder.initialize;
                            MODULE.shutdown = $builder.shutdown;

                            module_implement(
                                ptr,
                                Some(initialize),
                                Some(shutdown),
                                log_get_module(),
                            );
                            MODULE.module = ptr;

                            for dep in $builder.dependencies {
                                module_add_dep(ptr, dep);
                            }

                            for need in $builder.needed_by {
                                module_add_dep(need, ptr);
                            }
                        }
                        MODULE.module
                    }
                }

            }

            #[unsafe(no_mangle)]
            #[allow(clippy::macro_metavars_in_unsafe)]
            pub extern "C" fn [<$name _get_module>]() -> *mut module_t {
                [<$name _c_mod>]::get_module()
            }
        }
    };
}
