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

//! `qh_t` and `qm_t` bindings generation for bindgen output.
//!
//! The C macros declare a `__##pfx##_types_t` structure that names the types of every table, so
//! that this module reads them instead of guessing them:
//!
//! ```ignore
//! pub struct __qm_iop_struct_types_t {
//!     pub key: *mut lstr_t,
//!     pub ckey: *mut *const lstr_t,
//!     pub value: *mut *const iop_struct_t,
//! }
//! ```
//!
//! Every field points to the type it names, because bindgen drops a typedef whose target is a
//! pointer type. So one pointer is stripped from each field to get:
//!
//! - `key`, the type of a stored key,
//! - `ckey`, the type that the generated lookup and insertion take,
//! - `value`, the type of a value, or `void` for a set.
//!
//! Unlike a `qv_t`, a hash table cannot be manipulated through generic functions only: the C
//! macros bake the hash and equality functions of the table into a set of static inline functions,
//! and those functions are the only way to reach them. bindgen wraps them, so this module generates
//! a `QHashType` implementation that forwards to them:
//!
//! ```ignore
//! unsafe impl libcommon_core::qhash::QHashType for qm_iop_struct_t {
//!     type Key = lstr_t;
//!     type Value = *const iop_struct_t;
//!     unsafe fn find(qh: *mut Self, key: &Self::Key) -> i32 {
//!         unsafe { qm_iop_struct_find_int(qh, ::std::ptr::null(), ::std::ptr::from_ref(key)) }
//!     }
//!     ...
//! }
//! ```
//!
//! # Passing the key
//!
//! The generated functions take the key either by value or by pointer, depending on the C macro
//! that declared the table. `ckey` states which: it is the key itself in the first case, and a
//! pointer to it in the second.
//!
//! | table                | `key`            | `ckey`            | passing    |
//! |----------------------|------------------|-------------------|------------|
//! | `qh_u32_t`           | `u32`            | `u32`             | by value   |
//! | `qh_lstr_t`          | `lstr_t`         | `*const lstr_t`   | by pointer |
//! | `qm_part_t`          | `*mut lstr_t`    | `*const lstr_t`   | by value   |
//!
//! The `qm_part_t` row is why the comparison ignores the mutability of the outermost pointer, and
//! why the by-value case is tested first: its key *is* a pointer, so its `ckey` looks exactly like
//! the one of a table whose key is passed by pointer.

use quote::{ToTokens as _, format_ident, quote};
use std::collections::BTreeMap;
use std::collections::HashMap;
use syn::{Fields, File as SynFile, ForeignItem, Ident, Item, ItemStruct, Type, parse_quote};

use crate::get_crate_ident;

// {{{ Helpers

/// Prefix and suffix of the structure that names the types of a table.
const TYPES_PREFIX: &str = "__";
const TYPES_SUFFIX: &str = "_types_t";

/// Fields of that structure, in order.
const TYPES_FIELDS: [&str; 3] = ["key", "ckey", "value"];

/// Suffixes of the generated functions that a `QHashType` implementation needs.
const NEEDED_FNS: [&str; 6] = [
    "init",
    "hash",
    "find_int",
    "find_safe_int",
    "reserve_int",
    "seal",
];

/// Get the name of a table from the name of one of its generated items.
///
/// Return `None` if the name is not that of a `qh_t` or `qm_t` item.
fn table_name(name: &str, suffix: &str) -> Option<String> {
    let table = name.strip_suffix(suffix)?;

    (table.starts_with("qh_") || table.starts_with("qm_")).then(|| table.to_owned())
}

/// Get the normalized text of a type, to compare it with another one.
fn type_text(ty: &Type) -> String {
    ty.to_token_stream().to_string()
}

/// Compare two types, ignoring the mutability of the outermost pointer.
fn same_type(a: &Type, b: &Type) -> bool {
    if type_text(a) == type_text(b) {
        return true;
    }
    match (a, b) {
        (Type::Ptr(a), Type::Ptr(b)) => type_text(&a.elem) == type_text(&b.elem),
        _ => false,
    }
}

/// How the generated functions of a table take its key.
#[derive(Clone, Copy)]
enum KeyPassing {
    /// The key is passed by value, as in `qh_u32_find_int(qh, ph, key)`.
    Value,

    /// The key is passed by pointer, as in `qh_lstr_find_int(qh, ph, &key)`.
    Pointer,
}

impl KeyPassing {
    /// Tell how a table takes its key.
    ///
    /// Return `None` for a shape this module does not know.
    fn of(key: &Type, ckey: &Type) -> Option<Self> {
        // The by-value case must be tested first: the key of a table of pointers is a pointer
        // itself, so its `ckey` looks like the one of a key passed by pointer.
        if same_type(key, ckey) {
            return Some(Self::Value);
        }
        if let Type::Ptr(ckey) = ckey
            && same_type(key, &ckey.elem)
        {
            return Some(Self::Pointer);
        }
        None
    }

    /// Get the expression that passes `key` to a generated function.
    fn key_expr(self) -> proc_macro2::TokenStream {
        match self {
            Self::Value => quote! { *key },
            Self::Pointer => quote! { ::std::ptr::from_ref(key) },
        }
    }
}

// }}}
// {{{ Table description

/// Types of a table, read from the structure that names them.
struct Table {
    key: Type,
    ckey: Type,
    value: Type,

    /// Whether the table has no value, ie. it is a set.
    is_set: bool,
}

impl Table {
    /// Read the types of a table from the structure that names them.
    ///
    /// Return `None` if the structure is not shaped like that of a table.
    fn from_types_struct(item: &ItemStruct) -> Option<Self> {
        let Fields::Named(fields) = &item.fields else {
            return None;
        };
        if fields.named.len() != TYPES_FIELDS.len() {
            return None;
        }

        let names: Vec<String> = fields
            .named
            .iter()
            .filter_map(|field| field.ident.as_ref())
            .map(Ident::to_string)
            .collect();
        if names != TYPES_FIELDS {
            return None;
        }

        // Every field points to the type it names.
        let key = pointee(&fields.named[0].ty)?;
        let ckey = pointee(&fields.named[1].ty)?;
        let value = pointee(&fields.named[2].ty)?;

        // A set has no value: its `value` field points to void.
        let void: Type = parse_quote! { ::std::os::raw::c_void };
        let is_set = type_text(&value) == type_text(&void);

        Some(Self {
            key,
            ckey,
            value: if is_set {
                parse_quote! { () }
            } else {
                value
            },
            is_set,
        })
    }
}

/// Get the type a pointer type points to.
fn pointee(ty: &Type) -> Option<Type> {
    let Type::Ptr(ty) = ty else {
        return None;
    };

    Some((*ty.elem).clone())
}

// }}}
// {{{ QHash bindings generator (and items visitor)

pub struct QHashBindingsGenerator {
    /// Path to the `libcommon-core` crate.
    libcommon_core_crate: Ident,

    /// Tables found, by name. Sorted, to generate a stable output.
    tables: BTreeMap<String, Table>,

    /// Names of the generated functions found, by table name.
    functions: HashMap<String, Vec<String>>,
}

impl QHashBindingsGenerator {
    pub fn new() -> Self {
        Self {
            libcommon_core_crate: get_crate_ident("libcommon_core"),
            tables: BTreeMap::new(),
            functions: HashMap::new(),
        }
    }

    /// Consume the generator and return the generated bindings.
    pub fn into_bindings(self) -> String {
        let mut items = Vec::new();

        for (name, table) in &self.tables {
            self.generate_table(name, table, &mut items);
        }

        if items.is_empty() {
            return String::new();
        }

        let file = SynFile {
            shebang: None,
            attrs: Vec::new(),
            items,
        };
        prettyplease::unparse(&file)
    }

    // {{{ Items visitor

    pub fn visit_item(&mut self, item: &Item) {
        match item {
            Item::Struct(item_struct) => self.visit_struct(item_struct),
            Item::ForeignMod(foreign_mod) => {
                for item in &foreign_mod.items {
                    if let ForeignItem::Fn(item_fn) = item {
                        self.visit_fn(&item_fn.sig.ident.to_string());
                    }
                }
            }
            _ => {}
        }
    }

    /// Collect the types of a table from the structure that names them.
    ///
    /// The bindings contain every structure of the C code. Only the `__*_types_t` structures
    /// that the C table macros declare describe a table: their name and their shape are fixed. A
    /// structure that does not match is not a table and is ignored, so each early return is a
    /// normal outcome, not an error.
    fn visit_struct(&mut self, item: &ItemStruct) {
        // Not a `__*_types_t` structure.
        let Some(internal) = item
            .ident
            .to_string()
            .strip_prefix(TYPES_PREFIX)
            .map(String::from)
        else {
            return;
        };
        // Not named after a `qh_t` or a `qm_t`.
        let Some(name) = table_name(&internal, TYPES_SUFFIX) else {
            return;
        };
        // Not shaped like the structure of a table.
        let Some(table) = Table::from_types_struct(item) else {
            return;
        };

        // A set is named `qh_*` and a map `qm_*`; ignore anything else, it is not a table.
        if table.is_set != name.starts_with("qh_") {
            return;
        }
        self.tables.insert(name, table);
    }

    /// Collect the generated functions of a table.
    fn visit_fn(&mut self, name: &str) {
        for needed in NEEDED_FNS {
            let Some(table) = table_name(name, &format!("_{needed}")) else {
                continue;
            };

            self.functions
                .entry(table)
                .or_default()
                .push(needed.to_owned());
            return;
        }
    }

    // }}}
    // {{{ Bindings generation

    /// Generate the `QHashType` implementation of a table.
    ///
    /// A table is skipped, without an error, when the wrapper cannot use it: some of its
    /// functions are missing from the bindings, or its key is passed in a way this module does
    /// not know.
    fn generate_table(&self, name: &str, table: &Table, out: &mut Vec<Item>) {
        // Skip a table whose functions were not all generated: it is not usable.
        let Some(functions) = self.functions.get(name) else {
            return;
        };
        if !NEEDED_FNS
            .iter()
            .all(|needed| functions.iter().any(|found| found == needed))
        {
            return;
        }

        // Skip a table whose key is passed in a way this module does not know.
        let Some(passing) = KeyPassing::of(&table.key, &table.ckey) else {
            return;
        };

        let core = &self.libcommon_core_crate;
        let type_ident = format_ident!("{name}_t");
        let key = &table.key;
        let value = &table.value;
        let key_expr = passing.key_expr();

        let init = format_ident!("{name}_init");
        let hash = format_ident!("{name}_hash");
        let find = format_ident!("{name}_find_int");
        let find_safe = format_ident!("{name}_find_safe_int");
        let reserve = format_ident!("{name}_reserve_int");
        let seal = format_ident!("{name}_seal");

        // The wrapper manipulates the table through a `qhash_t` pointer.
        out.push(parse_quote! {
            const _: () = {
                assert!(
                    ::std::mem::size_of::<#type_ident>() == ::std::mem::size_of::<qhash_t>()
                );
                assert!(
                    ::std::mem::align_of::<#type_ident>() == ::std::mem::align_of::<qhash_t>()
                );
            };
        });

        out.push(parse_quote! {
            unsafe impl #core::qhash::QHashType for #type_ident {
                type Key = #key;
                type Value = #value;

                unsafe fn init(qh: *mut Self, cached: bool, mp: *mut mem_pool_t) {
                    unsafe { #init(qh, cached, mp) }
                }

                unsafe fn hash(qh: *const Self, key: &Self::Key) -> u32 {
                    unsafe { #hash(qh, #key_expr) }
                }

                unsafe fn find(qh: *mut Self, key: &Self::Key) -> i32 {
                    unsafe { #find(qh, ::std::ptr::null(), #key_expr) }
                }

                unsafe fn find_safe(qh: *const Self, key: &Self::Key) -> i32 {
                    unsafe { #find_safe(qh, ::std::ptr::null(), #key_expr) }
                }

                unsafe fn reserve(qh: *mut Self, key: &Self::Key, flags: u32) -> u32 {
                    unsafe { #reserve(qh, ::std::ptr::null(), #key_expr, flags) }
                }

                unsafe fn seal(qh: *mut Self) {
                    unsafe { #seal(qh) }
                }
            }
        });

        if !table.is_set {
            out.push(parse_quote! {
                unsafe impl #core::qhash::QMapType for #type_ident {}
            });
        }
    }

    // }}}
}

// }}}
