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

use proc_macro::TokenStream;
use quote::quote;

/// Macro to simplify the bindings module declaration.
///
/// This macro expands to the standard bindings module with all the necessary
/// allow attributes.
///
/// # Example
///
/// ```ignore
/// #[waf_cargo_build::bindings_mod]
/// pub mod bindings {
///     pub use libcommon::bindings::*;
///     waf_cargo_build::include_bindings!();
/// }
/// ```
///
/// This expands to:
///
/// ```ignore
/// #[allow(...)]
/// pub mod bindings {
///     pub use libcommon::bindings::*;
///     include!(concat!(env!("PKG_WAF_BUILD_DIR"), "/bindings.rs"));
/// }
/// ```
#[proc_macro_attribute]
pub fn bindings_mod(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let item = proc_macro2::TokenStream::from(item);

    quote! {
        #[allow(
            warnings,
            deprecated_safe,
            future_incompatible,
            keyword_idents,
            let_underscore,
            nonstandard_style,
            refining_impl_trait,
            rust_2018_compatibility,
            rust_2018_idioms,
            rust_2021_compatibility,
            rust_2024_compatibility,
            unused,
            clippy::all,
            clippy::pedantic,
            clippy::restriction
        )]

        #item
    }
    .into()
}

/// Macro to include bindings.
///
/// This macro expands to include the generated bindings file.
///
/// # Example
///
/// ```ignore
/// #[waf_cargo_build::bindings_mod]
/// pub mod bindings {
///     pub use libcommon::bindings::*;
///     waf_cargo_build::include_bindings!();
/// }
/// ```
///
/// This expands to:
///
/// ```ignore
/// #[allow(...)]
/// pub mod bindings {
///     pub use libcommon::bindings::*;
///     include!(concat!(env!("PKG_WAF_BUILD_DIR"), "/bindings.rs"));
/// }
/// ```
#[proc_macro]
pub fn include_bindings(_input: TokenStream) -> TokenStream {
    quote! {
        include!(concat!(env!("PKG_WAF_BUILD_DIR"), "/bindings.rs"));
    }
    .into()
}
