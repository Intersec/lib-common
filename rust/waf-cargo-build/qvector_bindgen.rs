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

//! `qv_t` bindings generation for bindgen output.
//!
//! A C `qv_t(n)` is a union of a `qvector_t` and an anonymous struct holding the typed fields.
//! bindgen generates the anonymous struct as `qv_<n>_t__bindgen_ty_1`:
//!
//! ```ignore
//! pub struct qv_lstr_t__bindgen_ty_1 {
//!     pub tab: *mut lstr_t,
//!     pub mp: *mut mem_pool_t,
//!     pub len: ::std::os::raw::c_int,
//!     pub size: ::std::os::raw::c_int,
//! }
//! ```
//!
//! This module reads the element type from the `tab` field of that struct, and generates the
//! `QVectorType` implementation that binds the C type to its element type:
//!
//! ```ignore
//! unsafe impl libcommon_core::qvector::QVectorType for qv_lstr_t {
//!     type Element = lstr_t;
//! }
//! ```
//!
//! Only the anonymous struct is visited, never the `qv_t` union itself: bindgen generates the
//! union as a real Rust `union` when it knows that `qvector_t` is `Copy`, and as a struct of
//! `__BindgenUnionField` when `qvector_t` is blocked because it belongs to another crate. The
//! anonymous struct is the same in both cases.

use quote::format_ident;
use syn::{Fields, File as SynFile, Ident, Item, ItemStruct, Type, parse_quote};

use crate::get_crate_ident;

// {{{ Helpers

/// Prefix of the C vector types.
const QV_PREFIX: &str = "qv_";

/// Suffix bindgen adds to the anonymous struct of a `qv_t` union.
const QV_ANON_SUFFIX: &str = "_t__bindgen_ty_1";

/// Fields of the anonymous struct of a `qv_t` union, in order.
const QV_FIELDS: [&str; 4] = ["tab", "mp", "len", "size"];

// }}}
// {{{ QVector bindings generator (and items visitor)

pub struct QVectorBindingsGenerator {
    /// Path to the `libcommon-core` crate.
    libcommon_core_crate: Ident,

    /// List of generated `qv_t` bindings.
    bindings: Vec<Item>,
}

impl QVectorBindingsGenerator {
    pub fn new() -> Self {
        Self {
            libcommon_core_crate: get_crate_ident("libcommon_core"),
            bindings: Vec::new(),
        }
    }

    /// Consume the generator and return the generated bindings.
    pub fn into_bindings(self) -> String {
        if self.bindings.is_empty() {
            return String::new();
        }

        let file = SynFile {
            shebang: None,
            attrs: Vec::new(),
            items: self.bindings,
        };
        prettyplease::unparse(&file)
    }

    // {{{ Items visitor

    pub fn visit_item(&mut self, item: &Item) {
        let Item::Struct(item_struct) = item else {
            return;
        };
        let name = item_struct.ident.to_string();

        // Only visit the anonymous struct of a `qv_t` union, ie. `qv_<n>_t__bindgen_ty_1`.
        let Some(qv_prefix) = name.strip_suffix(QV_ANON_SUFFIX) else {
            return;
        };
        if !qv_prefix.starts_with(QV_PREFIX) {
            return;
        }

        let Some(element) = Self::get_element_type(item_struct) else {
            return;
        };
        let type_ident = format_ident!("{qv_prefix}_t");
        let libcommon_core_crate = &self.libcommon_core_crate;

        self.bindings.push(parse_quote! {
            unsafe impl #libcommon_core_crate::qvector::QVectorType for #type_ident {
                type Element = #element;
            }
        });
    }

    /// Get the element type of a `qv_t` union from its anonymous struct.
    ///
    /// The element type is the type pointed to by the `tab` field.
    ///
    /// Return `None` if the struct is not shaped like the anonymous struct of a `qv_t` union.
    fn get_element_type(item_struct: &ItemStruct) -> Option<Type> {
        let Fields::Named(fields) = &item_struct.fields else {
            return None;
        };
        if fields.named.len() != QV_FIELDS.len() {
            return None;
        }

        let field_names: Vec<String> = fields
            .named
            .iter()
            .filter_map(|field| field.ident.as_ref())
            .map(Ident::to_string)
            .collect();
        if field_names != QV_FIELDS {
            return None;
        }

        // `tab` is `*mut <element>`.
        let Type::Ptr(tab) = &fields.named[0].ty else {
            return None;
        };
        Some((*tab.elem).clone())
    }

    // }}}
}

// }}}
