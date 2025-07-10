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

//! Module to derive libcommon entities and implement traits.
//!
//! WIP

use proc_macro::{self, TokenStream};
use proc_macro2::{Ident, Span};
use quote::quote;
use syn::ext::IdentExt;
use syn::{DeriveInput, parse_macro_input};

// {{{ Helpers

#[allow(unreachable_code)]
fn make_libcommon_crate_ident() -> Ident {
    // We must use "crate::" instead of "::libcommon::" when building `libcommon`.
    let libcommon_crate_name = if std::env::var("IS_LIBCOMMON_PKG").is_ok() {
        "crate"
    } else {
        "::libcommon"
    };

    Ident::new(libcommon_crate_name, Span::call_site())
}

// }}}
// {{{ IOP
// {{{ IopBase

#[proc_macro_derive(IopBase)]
pub fn derive_iop_base(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, .. } = parse_macro_input!(input);
    let libcommon_crate_ident = make_libcommon_crate_ident();

    let output = quote! {
        #[automatically_derived]
        impl #libcommon_crate_ident::iop::Base for #ident {}
    };
    output.into()
}

// }}}
// {{{ IopEnum

#[allow(clippy::missing_panics_doc)]
#[proc_macro_derive(IopEnum)]
pub fn derive_iop_enum(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, .. } = parse_macro_input!(input);
    let libcommon_crate_ident = make_libcommon_crate_ident();

    let ident_name = ident.unraw().to_string();

    // Convert "my_type__t" to "my_type__ep"
    let c_desc_name = ident_name[0..ident_name.len() - 1].to_owned() + "ep";

    let c_desc_span = Ident::new(&c_desc_name, Span::call_site());

    let output = quote! {
        #[automatically_derived]
        impl #libcommon_crate_ident::iop::Enum for #ident {
            const CDESC: *const iop_enum_t = unsafe { #c_desc_span };
        }
    };
    output.into()
}

// }}}
// {{{ IopStructUnion

#[allow(clippy::missing_panics_doc)]
#[proc_macro_derive(IopStructUnion)]
pub fn derive_iop_struct_union(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, .. } = parse_macro_input!(input);
    let libcommon_crate_ident = make_libcommon_crate_ident();

    let ident_name = ident.unraw().to_string();

    // Convert "my_type__t" to "my_type__sp"
    let c_desc_name = ident_name[0..ident_name.len() - 1].to_owned() + "sp";

    let c_desc_span = Ident::new(&c_desc_name, Span::call_site());

    let output = quote! {
        #[automatically_derived]
        impl #libcommon_crate_ident::iop::StructUnion for #ident {
            const CDESC: *const iop_struct_t = unsafe { #c_desc_span };
        }
    };
    output.into()
}

// }}}
// {{{ IopUnion

#[allow(clippy::missing_panics_doc)]
#[proc_macro_derive(IopUnion)]
pub fn derive_iop_union(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, .. } = parse_macro_input!(input);
    let libcommon_crate_ident = make_libcommon_crate_ident();

    let output = quote! {
        #[automatically_derived]
        impl #libcommon_crate_ident::iop::Union for #ident {}
    };
    output.into()
}

// }}}
// {{{ IopStruct

#[allow(clippy::missing_panics_doc)]
#[proc_macro_derive(IopStruct)]
pub fn derive_iop_struct(input: TokenStream) -> TokenStream {
    let DeriveInput { ident, .. } = parse_macro_input!(input);
    let libcommon_crate_ident = make_libcommon_crate_ident();

    let output = quote! {
        #[automatically_derived]
        impl #libcommon_crate_ident::iop::Struct for #ident {}
    };
    output.into()
}

// }}}
// }}}
