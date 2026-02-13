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
use std::collections::HashSet;
use std::env;
use std::mem;
use syn::{Arm, Attribute, File as SynFile, Ident, ImplItemFn, Item, Type, Variant, parse_quote};

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
    Enum(String),
    Union(String),
    Struct(String),
    Class(String),
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
struct IopField {
    name: String,
    ftype: IopFieldType,
    repeat: IopRepeat,
    is_ref: bool,
}

/// The kind of an IOP type (struct, union, class, or enum).
#[derive(Debug)]
enum IopKind {
    Struct {
        fields: Vec<IopField>,
    },
    Union {
        #[allow(dead_code)]
        variants: Vec<IopField>,
    },
    Class {
        #[allow(dead_code)]
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
        s if s.starts_with("enum:") => IopFieldType::Enum(s.strip_prefix("enum:")?.to_owned()),
        s if s.starts_with("union:") => IopFieldType::Union(s.strip_prefix("union:")?.to_owned()),
        s if s.starts_with("struct:") => {
            IopFieldType::Struct(s.strip_prefix("struct:")?.to_owned())
        }
        s if s.starts_with("class:") => IopFieldType::Class(s.strip_prefix("class:")?.to_owned()),
        _ => return None,
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
// {{{ Helpers

/// Get the Rust type for a field type
fn get_rust_type(ftype: &IopFieldType) -> Type {
    match ftype {
        IopFieldType::I8 => parse_quote! { i8 },
        IopFieldType::U8 => parse_quote! { u8 },
        IopFieldType::I16 => parse_quote! { i16 },
        IopFieldType::U16 => parse_quote! { u16 },
        IopFieldType::I32 => parse_quote! { i32 },
        IopFieldType::U32 => parse_quote! { u32 },
        IopFieldType::I64 => parse_quote! { i64 },
        IopFieldType::U64 => parse_quote! { u64 },
        IopFieldType::Bool => parse_quote! { bool },
        IopFieldType::Float => parse_quote! { f64 },
        IopFieldType::Str | IopFieldType::Xml => {
            parse_quote! { ::libcommon_core::lstr::UnsafeUtf8Lstr }
        }
        IopFieldType::Bytes => {
            parse_quote! { ::libcommon_core::lstr::UnsafeBytesLstr }
        }
        IopFieldType::Enum(name)
        | IopFieldType::Union(name)
        | IopFieldType::Struct(name)
        | IopFieldType::Class(name) => {
            let ident = format_ident!("{name}");
            parse_quote! { #ident }
        }
        IopFieldType::Void => parse_quote! { () },
    }
}

/// Get a Rust field name for an IOP field name.
///
/// Bindgen appends '_' to field names that are Rust keywords. This function is an attempt of
/// replicating this logic.
fn get_rust_field_name(name: &str) -> Ident {
    fn is_rust_primitive(s: &str) -> bool {
        matches!(
            s,
            "i8" | "i16"
                | "i32"
                | "i64"
                | "i128"
                | "isize"
                | "u8"
                | "u16"
                | "u32"
                | "u64"
                | "u128"
                | "usize"
                | "f32"
                | "f64"
                | "bool"
                | "char"
                | "str"
        )
    }
    fn is_special_ident(s: &str) -> bool {
        matches!(s, "self" | "crate" | "super")
    }
    fn is_reserved(s: &str) -> bool {
        syn::parse_str::<Ident>(s).is_err() || is_rust_primitive(s) || is_special_ident(s)
    }

    if is_reserved(name) {
        format_ident!("{name}_")
    } else {
        format_ident!("{name}")
    }
}

// }}}
// {{{ IOP bindings generator (and items visitor)

struct IopUnion {
    name: String,
    variants: Vec<IopField>,
}

/// Structure used as a context for the code that generates union bindings code
struct IopUnionGenCtx<'a> {
    // Parameters of the enum
    is_rust_union: bool,
    type_prefix: &'a str,
    variant_enum_ident: Ident,
    tag_enum_ident: Ident,

    // Outputs
    needs_lifetime: bool,
    enum_variants: Vec<Variant>,
    match_arms: Vec<Arm>,
    setter_methods: Vec<ImplItemFn>,
}

/// Structure used as a context for the code that generates bindings for a union field
struct IopUnionFieldGenCtx {
    rust_field_name: Ident,
    rust_type: Type,
    variant_ident: Ident,
    tag_ident: Ident,
    setter_name: Ident,
}

pub struct IopBindingsGenerator {
    /// Path to the libcommon crate
    libcommon_crate: Ident,

    /// List of generated IOP bindings
    bindings: Vec<Item>,

    /// List of unions to generate code for.
    ///
    /// Generating code for IOP unions cannot be done on-the-fly when visiting the union's struct
    /// because we need to know the kind of the underlying object generated by bindgen for the C
    /// anonymous union (Rust union or struct). So we store the unions information in this vector,
    /// and generate the associated code at the end of the parsing.
    unions: Vec<IopUnion>,

    /// Set of IOP unions that have been generated as Rust unions.
    ///
    /// Those that are not in this set were generated as Rust structs.
    unions_as_rust_unions: HashSet<String>,
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
            unions: Vec::new(),
            unions_as_rust_unions: HashSet::new(),
        }
    }

    /// Consume the generator and return the generated IOP bindings.
    pub fn into_bindings(mut self) -> String {
        // Generate code for unions
        for union in mem::take(&mut self.unions) {
            self.generate_union_code(&union);
        }

        // Generate the final bindings
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
                    IopKind::Struct { fields } | IopKind::Class { fields, .. } => {
                        self.generate_struct_trait_impl(&c_name);
                        self.generate_struct_fields_accessors(&c_name, &fields);
                    }
                    IopKind::Union { variants } => self.unions.push(IopUnion {
                        name: c_name,
                        variants,
                    }),
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
            Item::Union(item_union) => {
                let c_name = item_union.ident.to_string();
                if let Some(c_union_name) = c_name.strip_suffix("__bindgen_ty_1") {
                    self.unions_as_rust_unions.insert(c_union_name.to_owned());
                }
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
        let type_ident = format_ident!("{type_name}");
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
        let type_ident = format_ident!("{type_name}");
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
        let type_ident = format_ident!("{type_name}");
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
    // {{{ struct/class: fields accessors generation

    /// Generate fields accessors for a struct
    fn generate_struct_fields_accessors(&mut self, type_name: &str, fields: &[IopField]) {
        let type_ident = format_ident!("{type_name}");
        let mut methods = Vec::new();

        for field in fields {
            Self::generate_struct_field_accessors(field, &mut methods);
        }

        self.bindings.push(parse_quote! {
            impl #type_ident {
                #(#methods)*
            }
        });
    }

    /// Generate accessors for a struct field
    fn generate_struct_field_accessors(field: &IopField, out: &mut Vec<Item>) {
        let rust_field_name = get_rust_field_name(&field.name);
        let getter_name = format_ident!("{}__get", field.name);
        let setter_name = format_ident!("{}__set", field.name);
        let rust_type = get_rust_type(&field.ftype);

        match field.ftype {
            IopFieldType::I8
            | IopFieldType::I16
            | IopFieldType::I32
            | IopFieldType::I64
            | IopFieldType::U8
            | IopFieldType::U16
            | IopFieldType::U32
            | IopFieldType::U64
            | IopFieldType::Bool
            | IopFieldType::Float
            | IopFieldType::Enum(_) => Self::generate_struct_scalar_accessors(
                field,
                &rust_field_name,
                &getter_name,
                &setter_name,
                &rust_type,
                out,
            ),

            IopFieldType::Str | IopFieldType::Xml | IopFieldType::Bytes => {
                let from_raw_fn = if matches!(field.ftype, IopFieldType::Bytes) {
                    format_ident!("from_raw_bytes")
                } else {
                    format_ident!("from_raw_utf8")
                };
                Self::generate_struct_string_accessors(
                    field,
                    &rust_field_name,
                    &getter_name,
                    &setter_name,
                    &rust_type,
                    &from_raw_fn,
                    out,
                );
            }

            IopFieldType::Void => Self::generate_struct_void_accessors(
                field,
                &rust_field_name,
                &getter_name,
                &setter_name,
                out,
            ),

            IopFieldType::Union(_) | IopFieldType::Struct(_) | IopFieldType::Class(_) => {
                Self::generate_struct_complex_accessors(
                    field,
                    &rust_field_name,
                    &getter_name,
                    &setter_name,
                    &rust_type,
                    out,
                );
            }
        }
    }

    /// Generate accessors for a struct scalar field
    fn generate_struct_scalar_accessors(
        field: &IopField,
        rust_field_name: &Ident,
        getter_name: &Ident,
        setter_name: &Ident,
        rust_type: &Type,
        out: &mut Vec<Item>,
    ) {
        match field.repeat {
            IopRepeat::Required => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> #rust_type {
                        self.#rust_field_name
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: #rust_type) {
                        self.#rust_field_name = val;
                    }
                });
            }
            IopRepeat::Optional => {
                out.push(parse_quote! {
                        #[inline]
                        pub fn #getter_name(&self) -> Option<#rust_type> {
                            if self.#rust_field_name.has_field { Some(self.#rust_field_name.v) } else { None }
                        }
                    });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: Option<#rust_type>) {
                        match val {
                            Some(val) => {
                                self.#rust_field_name.v = val;
                                self.#rust_field_name.has_field = true;
                            }
                            None => {
                                self.#rust_field_name.has_field = false;
                            }
                        }
                    }
                });
            }
            IopRepeat::Repeated => Self::generate_struct_repeated_accessors(
                rust_field_name,
                getter_name,
                setter_name,
                rust_type,
                out,
            ),
        }
    }

    /// Generate accessors for a struct string field
    fn generate_struct_string_accessors(
        field: &IopField,
        rust_field_name: &Ident,
        getter_name: &Ident,
        setter_name: &Ident,
        rust_type: &Type,
        from_raw_fn: &Ident,
        out: &mut Vec<Item>,
    ) {
        match field.repeat {
            IopRepeat::Required => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> #rust_type {
                        unsafe { ::libcommon_core::lstr::#from_raw_fn(self.#rust_field_name) }
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: #rust_type) {
                        self.#rust_field_name = val.as_raw();
                    }
                });
            }
            IopRepeat::Optional => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> Option<#rust_type> {
                        let lstr = unsafe { ::libcommon_core::lstr::#from_raw_fn(self.#rust_field_name) };
                        if lstr.is_null() { None } else { Some(lstr) }
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: Option<#rust_type>) {
                        match val {
                            Some(val) => self.#rust_field_name = val.as_raw(),
                            None => self.#rust_field_name = ::libcommon_core::lstr::null_raw(),
                        }
                    }
                });
            }
            IopRepeat::Repeated => Self::generate_struct_repeated_accessors(
                rust_field_name,
                getter_name,
                setter_name,
                rust_type,
                out,
            ),
        }
    }

    /// Generate accessors for a struct void field
    fn generate_struct_void_accessors(
        field: &IopField,
        rust_field_name: &Ident,
        getter_name: &Ident,
        setter_name: &Ident,
        out: &mut Vec<Item>,
    ) {
        match field.repeat {
            IopRepeat::Optional => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> Option<()> {
                        self.#rust_field_name.then_some(())
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: Option<()>) {
                        match val {
                            Some(()) => self.#rust_field_name = true,
                            None => self.#rust_field_name = false,
                        }
                    }
                });
            }

            // Required void fields have no corresponding C field
            IopRepeat::Required => {}

            // Required void fields do not exist
            IopRepeat::Repeated => panic!("unexpected repeated void field `{field:#?}`"),
        }
    }

    /// Generate accessors for a struct complex field (enum/union/struct/class)
    fn generate_struct_complex_accessors(
        field: &IopField,
        rust_field_name: &Ident,
        getter_name: &Ident,
        setter_name: &Ident,
        rust_type: &Type,
        out: &mut Vec<Item>,
    ) {
        match (field.is_ref, &field.repeat) {
            // Non-pointed required complex fields
            (false, IopRepeat::Required) => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> &#rust_type {
                        &self.#rust_field_name
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: #rust_type) {
                        self.#rust_field_name = val;
                    }
                });
            }

            // Non-pointed optional or repeated complex fields (ie. optional enums or repeated
            // enums/structs/unions); same as scalar field
            (false, IopRepeat::Optional | IopRepeat::Repeated) => {
                Self::generate_struct_scalar_accessors(
                    field,
                    rust_field_name,
                    getter_name,
                    setter_name,
                    rust_type,
                    out,
                );
            }

            // Pointed required complex fields
            (true, IopRepeat::Required) => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> &#rust_type {
                        unsafe { &*self.#rust_field_name }
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: &#rust_type) {
                        self.#rust_field_name = val as *const #rust_type as *mut #rust_type;
                    }
                });
            }

            // Pointed optional complex fields
            (true, IopRepeat::Optional) => {
                out.push(parse_quote! {
                    #[inline]
                    pub fn #getter_name(&self) -> Option<&#rust_type> {
                        if self.#rust_field_name.is_null() { None } else { Some(unsafe { &*self.#rust_field_name }) }
                    }
                });
                out.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: Option<&#rust_type>) {
                        self.#rust_field_name = match val {
                            Some(val) => val as *const #rust_type as *mut #rust_type,
                            None => ::std::ptr::null_mut(),
                        }
                    }
                });
            }

            // Pointed repeated complex fields
            (true, IopRepeat::Repeated) => Self::generate_struct_repeated_accessors(
                rust_field_name,
                getter_name,
                setter_name,
                &parse_quote! { &#rust_type },
                out,
            ),
        }
    }

    /// Generate accessors for a struct repeated field
    fn generate_struct_repeated_accessors(
        rust_field_name: &Ident,
        getter_name: &Ident,
        setter_name: &Ident,
        rust_type: &Type,
        out: &mut Vec<Item>,
    ) {
        out.push(parse_quote! {
            #[inline]
            pub fn #getter_name(&self) -> &[#rust_type] {
                unsafe {
                    ::libcommon_core::helpers::slice_from_nullable_raw_parts(
                        self.#rust_field_name.tab as *const _,
                        self.#rust_field_name.len as usize,
                    )
                }
            }
        });
        out.push(parse_quote! {
            #[inline]
            pub fn #setter_name(&mut self, val: &[#rust_type]) {
                self.#rust_field_name.tab = val.as_ptr() as *mut _;
                self.#rust_field_name.len = val.len() as i32;
            }
        });
    }

    // }}}
    // {{{ union code generation

    /// Generate code related to an IOP union.
    ///
    /// For each IOP unions, we generate:
    /// 1. a Rust enum type (called <type>__variant) with a variant for each union field
    /// 2. a `iop_match()` method that returns the enum variant corresponding to the union tag
    /// 3. a setter method for each union field
    fn generate_union_code(&mut self, union_def: &IopUnion) {
        let type_prefix = union_def.name.strip_suffix("__t").expect("");
        let type_ident = format_ident!("{}", union_def.name);
        let mut ctx = IopUnionGenCtx {
            is_rust_union: self.unions_as_rust_unions.contains(&union_def.name),
            type_prefix,
            variant_enum_ident: format_ident!("{type_prefix}__variant"),
            tag_enum_ident: format_ident!("{type_prefix}__tag_t"),
            needs_lifetime: false,
            enum_variants: Vec::new(),
            match_arms: Vec::new(),
            setter_methods: Vec::new(),
        };

        self.generate_union_trait_impl(&union_def.name);

        for field in &union_def.variants {
            Self::generate_union_field_code(field, &mut ctx);
        }

        let variant_enum_ident = &ctx.variant_enum_ident;
        let enum_variants = &ctx.enum_variants;
        let match_arms = &ctx.match_arms;
        let setter_methods = &ctx.setter_methods;

        if ctx.needs_lifetime {
            self.bindings.push(parse_quote! {
                pub enum #variant_enum_ident<'a> {
                    #(#enum_variants),*
                }
            });
        } else {
            self.bindings.push(parse_quote! {
                pub enum #variant_enum_ident {
                    #(#enum_variants),*
                }
            });
        }

        let match_method: ImplItemFn = if ctx.needs_lifetime {
            parse_quote! {
                #[inline]
                pub fn iop_match(&self) -> #variant_enum_ident<'_> {
                    match self.iop_tag {
                        #(#match_arms),*
                    }
                }
            }
        } else {
            parse_quote! {
                #[inline]
                pub fn iop_match(&self) -> #variant_enum_ident {
                    match self.iop_tag {
                        #(#match_arms),*
                    }
                }
            }
        };

        self.bindings.push(parse_quote! {
            impl #type_ident {
                #match_method
                #(#setter_methods)*
            }
        });
    }

    fn generate_union_field_code(field: &IopField, ctx: &mut IopUnionGenCtx<'_>) {
        let field_ctx = IopUnionFieldGenCtx {
            rust_field_name: get_rust_field_name(&field.name),
            rust_type: get_rust_type(&field.ftype),
            variant_ident: format_ident!("{}", field.name),
            tag_ident: format_ident!("{}__{}__ft", ctx.type_prefix, field.name),
            setter_name: format_ident!("{}__set", field.name),
        };

        match field.ftype {
            IopFieldType::I8
            | IopFieldType::I16
            | IopFieldType::I32
            | IopFieldType::I64
            | IopFieldType::U8
            | IopFieldType::U16
            | IopFieldType::U32
            | IopFieldType::U64
            | IopFieldType::Bool
            | IopFieldType::Float
            | IopFieldType::Enum(_) => Self::generate_union_field_code_scalar(ctx, &field_ctx),

            IopFieldType::Str | IopFieldType::Xml | IopFieldType::Bytes => {
                Self::generate_union_field_code_string(ctx, field, &field_ctx);
            }

            IopFieldType::Void => Self::generate_union_field_code_void(ctx, &field_ctx),

            IopFieldType::Union(_) | IopFieldType::Struct(_) | IopFieldType::Class(_) => {
                Self::generate_union_field_code_complex(ctx, field, &field_ctx);
            }
        }
    }

    fn generate_union_field_code_scalar(
        ctx: &mut IopUnionGenCtx<'_>,
        field_ctx: &IopUnionFieldGenCtx,
    ) {
        let tag_enum_ident = &ctx.tag_enum_ident;
        let variant_enum_ident = &ctx.variant_enum_ident;
        let rust_field_name = &field_ctx.rust_field_name;
        let rust_type = &field_ctx.rust_type;
        let variant_ident = &field_ctx.variant_ident;
        let tag_ident = &field_ctx.tag_ident;
        let setter_name = &field_ctx.setter_name;

        ctx.enum_variants.push(parse_quote! {
            #variant_ident(#rust_type)
        });

        if ctx.is_rust_union {
            ctx.match_arms.push(parse_quote! {
                #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(unsafe {
                    self.__bindgen_anon_1.#rust_field_name
                })
            });
            ctx.setter_methods.push(parse_quote! {
                #[inline]
                pub fn #setter_name(&mut self, val: #rust_type) {
                    self.iop_tag = #tag_enum_ident::#tag_ident;
                    self.__bindgen_anon_1.#rust_field_name = val;
                }
            });
        } else {
            ctx.match_arms.push(parse_quote! {
                #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(unsafe {
                    *self.__bindgen_anon_1.#rust_field_name.as_ref()
                })
            });
            ctx.setter_methods.push(parse_quote! {
                #[inline]
                pub fn #setter_name(&mut self, val: #rust_type) {
                    self.iop_tag = #tag_enum_ident::#tag_ident;
                    unsafe { *self.__bindgen_anon_1.#rust_field_name.as_mut() = val };
                }
            });
        }
    }

    fn generate_union_field_code_string(
        ctx: &mut IopUnionGenCtx<'_>,
        field: &IopField,
        field_ctx: &IopUnionFieldGenCtx,
    ) {
        let tag_enum_ident = &ctx.tag_enum_ident;
        let variant_enum_ident = &ctx.variant_enum_ident;
        let rust_field_name = &field_ctx.rust_field_name;
        let rust_type = &field_ctx.rust_type;
        let variant_ident = &field_ctx.variant_ident;
        let tag_ident = &field_ctx.tag_ident;
        let setter_name = &field_ctx.setter_name;

        // Strings cannot be copied, so bindgen should always generate a Rust struct to
        // represent the union
        assert!(!ctx.is_rust_union, "unexpected string field in Rust union");

        ctx.enum_variants.push(parse_quote! {
            #variant_ident(#rust_type)
        });

        let from_raw_fn = if matches!(field.ftype, IopFieldType::Bytes) {
            format_ident!("from_raw_bytes")
        } else {
            format_ident!("from_raw_utf8")
        };
        ctx.match_arms.push(parse_quote! {
            #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(unsafe {
                ::libcommon_core::lstr::#from_raw_fn(
                    *self.__bindgen_anon_1.#rust_field_name.as_ref())
            })
        });

        ctx.setter_methods.push(parse_quote! {
            #[inline]
            pub fn #setter_name(&mut self, val: #rust_type) {
                self.iop_tag = #tag_enum_ident::#tag_ident;
                unsafe { *self.__bindgen_anon_1.#rust_field_name.as_mut() = val.as_raw() };
            }
        });
    }

    fn generate_union_field_code_void(
        ctx: &mut IopUnionGenCtx<'_>,
        field_ctx: &IopUnionFieldGenCtx,
    ) {
        let tag_enum_ident = &ctx.tag_enum_ident;
        let variant_enum_ident = &ctx.variant_enum_ident;
        let rust_type = &field_ctx.rust_type;
        let variant_ident = &field_ctx.variant_ident;
        let tag_ident = &field_ctx.tag_ident;
        let setter_name = &field_ctx.setter_name;

        ctx.enum_variants.push(parse_quote! {
            #variant_ident
        });
        ctx.match_arms.push(parse_quote! {
            #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident
        });
        ctx.setter_methods.push(parse_quote! {
            #[inline]
            pub fn #setter_name(&mut self, val: #rust_type) {
                self.iop_tag = #tag_enum_ident::#tag_ident;
            }
        });
    }

    fn generate_union_field_code_complex(
        ctx: &mut IopUnionGenCtx<'_>,
        field: &IopField,
        field_ctx: &IopUnionFieldGenCtx,
    ) {
        let tag_enum_ident = &ctx.tag_enum_ident;
        let variant_enum_ident = &ctx.variant_enum_ident;
        let rust_field_name = &field_ctx.rust_field_name;
        let rust_type = &field_ctx.rust_type;
        let variant_ident = &field_ctx.variant_ident;
        let tag_ident = &field_ctx.tag_ident;
        let setter_name = &field_ctx.setter_name;

        ctx.needs_lifetime = true;

        ctx.enum_variants.push(parse_quote! {
            #variant_ident(&'a #rust_type)
        });

        match (ctx.is_rust_union, field.is_ref) {
            // Rust union, non-pointed field
            (true, false) => {
                ctx.match_arms.push(parse_quote! {
                    #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(
                        unsafe { &self.__bindgen_anon_1.#rust_field_name })
                });
                ctx.setter_methods.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: #rust_type) {
                        self.iop_tag = #tag_enum_ident::#tag_ident;
                        self.__bindgen_anon_1.#rust_field_name = val;
                    }
                });
            }

            // Rust union, pointed field
            (true, true) => {
                ctx.match_arms.push(parse_quote! {
                    #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(
                        unsafe { &*self.__bindgen_anon_1.#rust_field_name })
                });
                ctx.setter_methods.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: &#rust_type) {
                        self.iop_tag = #tag_enum_ident::#tag_ident;
                        self.__bindgen_anon_1.#rust_field_name =
                            val as *const #rust_type as *mut #rust_type;
                    }
                });
            }

            // Rust struct, non-pointed field
            (false, false) => {
                ctx.match_arms.push(parse_quote! {
                    #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(
                        unsafe { self.__bindgen_anon_1.#rust_field_name.as_ref() })
                });
                ctx.setter_methods.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: #rust_type) {
                        self.iop_tag = #tag_enum_ident::#tag_ident;
                        unsafe { *self.__bindgen_anon_1.#rust_field_name.as_mut() = val };
                    }
                });
            }

            // Rust struct, pointed field
            (false, true) => {
                ctx.match_arms.push(parse_quote! {
                    #tag_enum_ident::#tag_ident => #variant_enum_ident::#variant_ident(
                        unsafe { &*(*self.__bindgen_anon_1.#rust_field_name.as_ref()) })
                });
                ctx.setter_methods.push(parse_quote! {
                    #[inline]
                    pub fn #setter_name(&mut self, val: &#rust_type) {
                        self.iop_tag = #tag_enum_ident::#tag_ident;
                        unsafe { *self.__bindgen_anon_1.#rust_field_name.as_mut() =
                                     val as *const #rust_type as *mut #rust_type };
                    }
                });
            }
        }
    }

    // }}}
}

// }}}
