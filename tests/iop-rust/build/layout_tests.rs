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

//! Generates the layout tests of the Rust that `iopc` writes.
//!
//! `iopc` computes the layout of its Rust with the same rules as its C,
//! so it cannot check itself. bindgen reads the C headers with clang,
//! which gives the layout of the C compiler. The tests compare the two.

use std::collections::HashMap;
use std::error;
use std::fmt::Write as _;
use std::fs;

use bindgen::CodegenConfig;
use syn::{Fields, Item, Type};
use waf_cargo_build::{NativeIopPkg, WafBuildCtx};

type Error = Box<dyn error::Error>;

/// Writes `c_layout.rs` and `layout_tests.rs` to `OUT_DIR`.
///
/// `c_layout.rs` is the bindgen output of `wrapper.h` without the native
/// IOP bindings, because these are what the tests check. It holds only the
/// C types of the IOP packages and the types that they use.
pub fn generate(
    build: &WafBuildCtx,
    iop_files: &[(&NativeIopPkg, syn::File)],
) -> Result<(), Error> {
    let builder = build
        .waf
        .bindgen_builder()
        .header("wrapper.h")
        .with_codegen_config(CodegenConfig::TYPES);
    let c_layout = iop_files
        .iter()
        .fold(builder, |builder, (pkg, _)| {
            builder.allowlist_type(format!("{}.*", pkg.prefix))
        })
        .generate()?
        .to_string();
    let tests = generate_tests(&syn::parse_file(&c_layout)?, iop_files)?;

    fs::write(build.out_dir.join("c_layout.rs"), &c_layout)?;
    fs::write(build.out_dir.join("layout_tests.rs"), tests)?;

    Ok(())
}

/// The named types of a bindgen output.
struct CTypes<'a>(HashMap<String, &'a Item>);

impl<'a> CTypes<'a> {
    fn new(file: &'a syn::File) -> Self {
        Self(
            file.items
                .iter()
                .filter_map(|item| {
                    let ident = match item {
                        Item::Struct(s) => &s.ident,
                        Item::Union(u) => &u.ident,
                        Item::Enum(e) => &e.ident,
                        Item::Type(t) => &t.ident,
                        _ => return None,
                    };

                    Some((ident.to_string(), item))
                })
                .collect(),
        )
    }

    fn get(&self, name: &str) -> Result<&'a Item, Error> {
        self.0.get(name).copied().ok_or_else(|| {
            format!("bindgen did not generate `{name}`: is it reachable from `wrapper.h`?").into()
        })
    }

    /// The struct or union that `name` designates, through its typedefs.
    fn resolve(&self, name: &str) -> Result<&'a Item, Error> {
        let mut item = self.get(name)?;

        while let Item::Type(alias) = item {
            item = self.get(&type_name(&alias.ty).ok_or("unsupported typedef")?)?;
        }

        Ok(item)
    }

    /// The path of each leaf field of the struct or union `item`, in
    /// declaration order.
    ///
    /// bindgen keeps the C nesting of an IOP class: an anonymous union of
    /// the parent struct and of an anonymous struct with the fields of the
    /// class. Only the `super_` branch of that union is walked, because
    /// `iopc` writes the fields of the parent classes first.
    fn field_paths(&self, item: &Item, prefix: &str, out: &mut Vec<String>) -> Result<(), Error> {
        let fields: Vec<_> = match item {
            Item::Struct(s) => s.fields.iter().collect(),
            Item::Union(u) => u.fields.named.iter().collect(),
            _ => return Err("expected a struct or a union".into()),
        };

        for field in fields {
            let ident = field.ident.as_ref().ok_or("unnamed field")?.to_string();

            if !ident.starts_with("__bindgen_anon_") {
                out.push(format!("{prefix}{ident}"));
                continue;
            }

            let anon = self.resolve(&type_name(&field.ty).ok_or("unsupported field")?)?;
            let parent = match anon {
                Item::Union(u) => u
                    .fields
                    .named
                    .iter()
                    .find(|f| f.ident.as_ref().is_some_and(|i| i == "super_")),
                _ => None,
            };

            if let Some(parent) = parent {
                let parent = self.resolve(&type_name(&parent.ty).ok_or("unsupported parent")?)?;

                self.field_paths(parent, &format!("{prefix}{ident}.super_."), out)?;
            } else {
                self.field_paths(anon, &format!("{prefix}{ident}."), out)?;
            }
        }

        Ok(())
    }
}

/// The name of a named type.
pub fn type_name(ty: &Type) -> Option<String> {
    match ty {
        Type::Path(path) => path.path.segments.last().map(|seg| seg.ident.to_string()),
        _ => None,
    }
}

/// The C type of the `iopc` type `name`, if it has one.
///
/// The C typedef always ends with `__t`, where `iopc` uses `__u` for a
/// union and `__e` for an enum. An RPC marker has no C type.
fn c_type_name(name: &str) -> Option<String> {
    let stem = name
        .strip_suffix("__t")
        .or_else(|| name.strip_suffix("__u"))
        .or_else(|| name.strip_suffix("__e"))?;

    Some(format!("{stem}__t"))
}

/// The fields of an `iopc` struct, in declaration order.
///
/// `iopc` pads the fields inherited by a class with `_pad_*` members,
/// where the C struct leaves the gap implicit.
fn iop_field_names(name: &str, fields: &Fields) -> Result<Vec<String>, Error> {
    if let Fields::Unnamed(_) = fields {
        return Err(format!("`{name}` is a tuple struct").into());
    }

    Ok(fields
        .iter()
        .filter_map(|field| field.ident.as_ref().map(ToString::to_string))
        .filter(|ident| !ident.starts_with("_pad_"))
        .collect())
}

/// Writes the test of the `iopc` type `name` against the C type `c_name`.
///
/// `ty` is `name` with its lifetime, if it takes one.
fn write_test(
    tests: &mut String,
    c_types: &CTypes<'_>,
    (name, ty): (&str, &str),
    c_name: &str,
    fields: Option<&Fields>,
) -> Result<(), Error> {
    let c_item = c_types.resolve(c_name)?;

    writeln!(tests, "#[test]")?;
    writeln!(tests, "fn {name}__layout() {{")?;
    writeln!(tests, "    type Iop = iop_rust::{ty};")?;
    writeln!(tests, "    type C = c_layout::{c_name};")?;
    writeln!(tests, "    assert_eq!(size_of::<Iop>(), size_of::<C>());")?;
    writeln!(tests, "    assert_eq!(align_of::<Iop>(), align_of::<C>());")?;

    if let Some(fields) = fields {
        let iop_fields = iop_field_names(name, fields)?;
        let mut c_fields = Vec::new();

        c_types.field_paths(c_item, "", &mut c_fields)?;
        if iop_fields.len() != c_fields.len() {
            return Err(format!(
                "`{name}` has {} fields in the Rust of `iopc` and {} in C",
                iop_fields.len(),
                c_fields.len(),
            )
            .into());
        }

        for (iop_field, c_field) in iop_fields.iter().zip(&c_fields) {
            writeln!(
                tests,
                "    assert_eq!(offset_of!(Iop, {iop_field}), offset_of!(C, {c_field}));"
            )?;
        }
    }

    writeln!(tests, "}}\n")?;

    Ok(())
}

fn generate_tests(
    c_layout: &syn::File,
    iop_files: &[(&NativeIopPkg, syn::File)],
) -> Result<String, Error> {
    let c_types = CTypes::new(c_layout);
    let mut tests = String::new();

    for (_, iop_file) in iop_files {
        for item in &iop_file.items {
            let (ident, generics, fields) = match item {
                Item::Struct(st) => (&st.ident, &st.generics, Some(&st.fields)),
                Item::Enum(en) => (&en.ident, &en.generics, None),
                _ => continue,
            };
            let name = ident.to_string();
            let ty = if generics.params.is_empty() {
                name.clone()
            } else {
                format!("{name}<'static>")
            };

            if let Some(c_name) = c_type_name(&name) {
                write_test(&mut tests, &c_types, (&name, &ty), &c_name, fields)?;
            }
        }
    }

    Ok(tests)
}
