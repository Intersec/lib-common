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

//! Procedural macros supporting the `libcommon` test suites.

use proc_macro::TokenStream;
use quote::quote;
use syn::{ItemFn, parse_macro_input};

/// Declare a `#[test]` that needs exclusive access to the C runtime singletons
/// (a C module and/or the C event loop).
///
/// C modules and the C event loop are process-wide singletons. Unlike the C
/// test suite, where tests run sequentially, the Rust harness runs a crate's
/// tests concurrently within one process, so two tests touching those
/// singletons would race against each other.
///
/// This attribute wraps the annotated function in a `#[test]` that holds the
/// shared lock (`libcommon_core::test_sync::c_event_loop_guard`) for its whole
/// body, serializing it against every other test declared the same way.
///
/// The annotated crate must depend on `libcommon-core` with its `test-support`
/// feature enabled (typically in `[dev-dependencies]`); the attribute is
/// re-exported from there, so consumers use it as `libcommon_core::
/// c_event_loop_test` rather than depending on this crate directly.
///
/// # Examples
///
/// ```ignore
/// use libcommon_core::c_event_loop_test;
///
/// #[c_event_loop_test]
/// fn drives_the_event_loop() {
///     module_require(tokio_get_module());
///     /* ... el_loop() ... */
/// }
/// ```
#[proc_macro_attribute]
pub fn c_event_loop_test(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let ItemFn {
        attrs,
        vis,
        sig,
        block,
    } = parse_macro_input!(item as ItemFn);

    quote! {
        #[test]
        #(#attrs)*
        #vis #sig {
            let _guard = ::libcommon_core::test_sync::c_event_loop_guard();
            #block
        }
    }
    .into()
}
