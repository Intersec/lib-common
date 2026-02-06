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

//! IOP bindings generation for bindgen output.

use quote::format_ident;
use std::env;
use syn::{Attribute, File as SynFile, Ident, Item, parse_quote};

// {{{ IOP annotation parsing

/// Type of an IOP field.
#[derive(Debug)]
enum IopFieldType {
    I8,
    U8,
    I16,
    U16,
    I32,
    U32,
    I64,
    U64,
    Bool,
    Float,
    Str,
    Bytes,
    Xml,
    Void,
    #[allow(dead_code)]
    ComplexType(String),
}

/// Whether a field is required, optional, or repeated.
#[derive(Debug)]
enum IopRepeat {
    Required,
    Optional,
    Repeated,
}

/// Description of an IOP field
#[derive(Debug)]
#[allow(dead_code)]
struct IopField {
    name: String,
    ftype: IopFieldType,
    repeat: IopRepeat,
    is_ref: bool,
}

/// The kind of an IOP type (struct, union, class, or enum).
#[derive(Debug)]
#[allow(dead_code)]
enum IopKind {
    Struct {
        fields: Vec<IopField>,
    },
    Union {
        variants: Vec<IopField>,
    },
    Class {
        parent: Option<String>,
        fields: Vec<IopField>,
    },
    Enum,
}

/// Parse an IOP field annotation.
///
/// Format is: name: type[&][?|[]]
fn parse_iop_field(s: &str) -> Option<IopField> {
    let (name, s) = s.split_once(": ")?;

    let (ftype_str, repeat, is_ref) = if let Some(inner) = s.strip_suffix("[]") {
        // Arrays: check for reference modifier before []
        if let Some(base) = inner.strip_suffix('&') {
            (base, IopRepeat::Repeated, true)
        } else {
            (inner, IopRepeat::Repeated, false)
        }
    } else if let Some(inner) = s.strip_suffix('?') {
        // Optional: check for reference modifier before ?
        if let Some(base) = inner.strip_suffix('&') {
            (base, IopRepeat::Optional, true)
        } else {
            (inner, IopRepeat::Optional, false)
        }
    } else if let Some(base) = s.strip_suffix('&') {
        // Required reference
        (base, IopRepeat::Required, true)
    } else {
        (s, IopRepeat::Required, false)
    };

    let ftype = match ftype_str {
        "i8" => IopFieldType::I8,
        "u8" => IopFieldType::U8,
        "i16" => IopFieldType::I16,
        "u16" => IopFieldType::U16,
        "i32" => IopFieldType::I32,
        "u32" => IopFieldType::U32,
        "i64" => IopFieldType::I64,
        "u64" => IopFieldType::U64,
        "bool" => IopFieldType::Bool,
        "float" => IopFieldType::Float,
        "str" => IopFieldType::Str,
        "bytes" => IopFieldType::Bytes,
        "xml" => IopFieldType::Xml,
        "void" => IopFieldType::Void,
        s => IopFieldType::ComplexType(s.to_owned()),
    };

    Some(IopField {
        name: name.to_owned(),
        ftype,
        repeat,
        is_ref,
    })
}

/// Parse the fields of an IOP struct/union/class.
///
/// Format is: { field1, field2, ... }
/// Where field is: name: type[&][?|[]]
fn parse_iop_fields(s: &str) -> Option<Vec<IopField>> {
    let s = s.strip_prefix('{')?.strip_suffix('}')?.trim();

    if s.is_empty() {
        Some(Vec::new())
    } else {
        s.split(", ").map(parse_iop_field).collect()
    }
}

// Parse an IOP class annotation.
//
// Format is: [:parent] { fields }
fn parse_iop_class(s: &str) -> Option<IopKind> {
    let (parent, fields) = s.split_at(s.find('{')?);

    Some(IopKind::Class {
        parent: if parent.trim().is_empty() {
            None
        } else {
            Some(parent.strip_prefix(":")?.to_owned())
        },
        fields: parse_iop_fields(fields)?,
    })
}

/// Parse the IOP kind annotation (struct, union, class, class:Parent, enum).
fn parse_iop_kind(s: &str) -> Option<IopKind> {
    match s {
        "enum" => Some(IopKind::Enum),
        s if s.starts_with("struct ") => Some(IopKind::Struct {
            fields: parse_iop_fields(s.strip_prefix("struct ")?)?,
        }),
        s if s.starts_with("union ") => Some(IopKind::Union {
            variants: parse_iop_fields(s.strip_prefix("union ")?)?,
        }),
        s if s.starts_with("class") => parse_iop_class(s.strip_prefix("class")?),
        _ => None,
    }
}

/// Get the contents of an IOP annotation from a doc comment "@iop ..."
fn get_iop_annotation_contents(doc: &str) -> Option<&str> {
    doc.trim().strip_prefix("@iop ")
}

/// Extract doc comment string from an attribute.
fn get_doc_string(attr: &Attribute) -> Option<String> {
    if !attr.path().is_ident("doc") {
        return None;
    }

    if let syn::Meta::NameValue(meta) = &attr.meta
        && let syn::Expr::Lit(expr_lit) = &meta.value
        && let syn::Lit::Str(lit_str) = &expr_lit.lit
    {
        return Some(lit_str.value());
    }
    None
}

// }}}
// {{{ IOP bindings generator (and items visitor)

pub struct IopBindingsGenerator {
    /// Path to the libcommon crate
    libcommon_crate: Ident,

    /// List of generated IOP bindings
    bindings: Vec<Item>,
}

impl IopBindingsGenerator {
    pub fn new() -> Self {
        Self {
            libcommon_crate: {
                // We must use "crate::" instead of "libcommon::" when building `libcommon`.
                if env::var("CARGO_MANIFEST_DIR")
                    .expect("missing $CARGO_MANIFEST_DIR")
                    .ends_with("/rust/libcommon")
                {
                    format_ident!("crate")
                } else {
                    format_ident!("libcommon")
                }
            },
            bindings: Vec::new(),
        }
    }

    /// Consume the generator and return the generated IOP bindings.
    pub fn into_bindings(self) -> String {
        let file = SynFile {
            shebang: None,
            attrs: Vec::new(),
            items: self.bindings,
        };
        prettyplease::unparse(&file)
    }

    // {{{ Items visitor

    pub fn visit_item(&mut self, item: &Item) {
        match item {
            Item::Struct(item_struct) => {
                let c_name = item_struct.ident.to_string();

                if !c_name.ends_with("__t") {
                    return;
                }

                let Some(kind) = Self::get_iop_kind_from_attrs(&item_struct.attrs) else {
                    return;
                };
                match kind {
                    IopKind::Struct { .. } | IopKind::Class { .. } => {
                        self.generate_struct_trait_impl(&c_name);
                    }
                    IopKind::Union { .. } => {
                        self.generate_union_trait_impl(&c_name);
                    }
                    IopKind::Enum => panic!("unexpected IOP kind `{kind:#?}` on `{c_name}`"),
                }
            }
            Item::Enum(item_enum) => {
                let c_name = item_enum.ident.to_string();

                if !c_name.ends_with("__t") {
                    return;
                }
                let Some(kind) = Self::get_iop_kind_from_attrs(&item_enum.attrs) else {
                    return;
                };
                assert!(
                    matches!(kind, IopKind::Enum),
                    "unexpected IOP kind `{kind:#?}` on `{c_name}`"
                );
                self.generate_enum_trait_impl(&c_name);
            }
            _ => {}
        }
    }

    /// Check if a struct's attributes contain an IOP kind annotation.
    fn get_iop_kind_from_attrs(attrs: &[Attribute]) -> Option<IopKind> {
        for attr in attrs {
            if let Some(doc) = get_doc_string(attr)
                && let Some(iop_str) = get_iop_annotation_contents(&doc)
            {
                if let Some(kind) = parse_iop_kind(iop_str) {
                    return Some(kind);
                }
                panic!("invalid IOP kind annonation `{iop_str}`");
            }
        }
        None
    }

    // }}}
    // {{{ traits impl generation

    /// Generate common IOP trait implementations for a struct/union types.
    fn generate_struct_union_trait_impl(&mut self, type_ident: &Ident, desc_ident: &Ident) {
        let libcommon_crate = &self.libcommon_crate;

        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::Base for #type_ident {}
        });
        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::StructUnion for #type_ident {
                fn get_cdesc(&self) -> *const iop_struct_t {
                    &raw const #desc_ident
                }
            }
        });
        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::CStructUnion for #type_ident {
                const CDESC: *const iop_struct_t = &raw const #desc_ident;
            }
        });
    }

    /// Generate IOP trait implementations for a struct type.
    fn generate_struct_trait_impl(&mut self, type_name: &str) {
        let type_ident = format_ident!("{}", type_name);
        let desc_ident = format_ident!("{}__s", type_name.strip_suffix("__t").expect(""));

        self.generate_struct_union_trait_impl(&type_ident, &desc_ident);

        let libcommon_crate = &self.libcommon_crate;

        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::Struct for #type_ident {}
        });
        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::CStruct for #type_ident {}
        });
    }

    /// Generate IOP trait implementations for a union type.
    fn generate_union_trait_impl(&mut self, type_name: &str) {
        let type_ident = format_ident!("{}", type_name);
        let desc_ident = format_ident!("{}__s", type_name.strip_suffix("__t").expect(""));

        self.generate_struct_union_trait_impl(&type_ident, &desc_ident);

        let libcommon_crate = &self.libcommon_crate;

        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::Union for #type_ident {}
        });
        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::CUnion for #type_ident {}
        });
    }

    /// Generate IOP trait implementations for an enum type.
    fn generate_enum_trait_impl(&mut self, type_name: &str) {
        let libcommon_crate = &self.libcommon_crate;
        let type_ident = format_ident!("{}", type_name);
        let desc_ident = format_ident!("{}__e", type_name.strip_suffix("__t").expect(""));

        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::Base for #type_ident {}
        });
        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::Enum for #type_ident {
                fn get_cdesc(&self) -> *const iop_enum_t {
                    &raw const #desc_ident
                }
            }
        });
        self.bindings.push(parse_quote! {
            impl #libcommon_crate::iop::CEnum for #type_ident {
                const CDESC: *const iop_enum_t = &raw const #desc_ident;
            }
        });
    }

    // }}}
}

// }}}
