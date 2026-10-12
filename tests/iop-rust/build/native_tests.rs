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

//! Generates the runtime tests of the Rust that `iopc` writes: pack and
//! unpack round-trips, constraint checks and class casts.
//!
//! The test bodies are in `iop_testable.rs`. This file only lists the
//! types to test.

use std::collections::HashSet;
use std::error;
use std::fmt::Write as _;
use std::fs;

use syn::{ImplItem, Item, ItemImpl, Type};
use waf_cargo_build::{NativeIopPkg, WafBuildCtx};

use crate::layout_tests::type_name;

type Error = Box<dyn error::Error>;

/// Writes `native_tests.rs` to `OUT_DIR`.
pub fn generate(
    build: &WafBuildCtx,
    iop_files: &[(&NativeIopPkg, syn::File)],
) -> Result<(), Error> {
    let mut tests = String::new();

    write_env(&mut tests, iop_files)?;
    for (_, file) in iop_files {
        for tested in tested_types(file) {
            write_tests(&mut tests, &tested)?;
        }
    }

    fs::write(build.out_dir.join("native_tests.rs"), tests)?;

    Ok(())
}

/// Writes the IOP environment that the round-trip tests unpack with. It
/// registers each package that the crate binds.
///
/// `iopc` does not declare the package descriptors in Rust, so the
/// generated code declares them.
fn write_env(tests: &mut String, iop_files: &[(&NativeIopPkg, syn::File)]) -> Result<(), Error> {
    let mut declared = String::new();
    let mut registered = String::new();

    for (pkg, _) in iop_files {
        let prefix = &pkg.prefix;

        writeln!(declared, "        static {prefix}pkg: iop_pkg_t;")?;
        writeln!(registered, "            &raw const {prefix}pkg,")?;
    }

    write!(
        tests,
        r#"mod iop_env {{
    use crate::bindings::iop_pkg_t;
    use libcommon::iop::{{Env, EnvCtx}};
    use std::sync::LazyLock;

    unsafe extern "C" {{
{declared}    }}

    pub static ENV: LazyLock<EnvCtx> = LazyLock::new(|| {{
        let mut env = Env::new();

        env.register_packages(&[
{registered}        ]);
        env.acquire()
    }});
}}

"#
    )?;

    Ok(())
}

/// A type that `iopc` generated a constructor for.
struct TestedType {
    name: String,
    /// The type takes a lifetime.
    is_generic: bool,
    /// The type is a class that extends another one.
    has_parent: bool,
}

/// The trait that `item` implements, and the type that implements it.
fn impl_trait_for(item: &ItemImpl) -> Option<(String, String)> {
    let (_, trait_path, _) = item.trait_.as_ref()?;

    Some((
        trait_path.segments.last()?.ident.to_string(),
        type_name(&item.self_ty)?,
    ))
}

/// Whether an `impl Class` names a parent: a root class is its own parent.
fn has_parent(item: &ItemImpl) -> bool {
    item.items.iter().any(|item| {
        matches!(item, ImplItem::Type(ty)
            if ty.ident == "Parent"
                && !matches!(&ty.ty, Type::Path(p) if p.path.is_ident("Self")))
    })
}

/// The types of a `.iop.rs` file that a test can construct.
///
/// `iopc` implements `CStruct`, which provides `new()`, only for the types
/// that can be instantiated.
fn tested_types(file: &syn::File) -> Vec<TestedType> {
    let mut generic = HashSet::new();
    let mut constructible = Vec::new();
    let mut with_parent = HashSet::new();

    for item in &file.items {
        match item {
            Item::Struct(st) if !st.generics.params.is_empty() => {
                generic.insert(st.ident.to_string());
            }
            Item::Impl(item) => match impl_trait_for(item) {
                Some((tr, name)) if tr == "CStruct" => constructible.push(name),
                Some((tr, name)) if tr == "Class" && has_parent(item) => {
                    with_parent.insert(name);
                }
                _ => {}
            },
            _ => {}
        }
    }

    constructible
        .into_iter()
        .map(|name| TestedType {
            is_generic: generic.contains(&name),
            has_parent: with_parent.contains(&name),
            name,
        })
        .collect()
}

fn write_test(tests: &mut String, name: &str, suffix: &str, body: &str) -> Result<(), Error> {
    writeln!(tests, "#[test]")?;
    writeln!(tests, "fn {name}__{suffix}() {{")?;
    writeln!(tests, "    {body}")?;
    writeln!(tests, "}}\n")?;

    Ok(())
}

fn write_tests(tests: &mut String, tested: &TestedType) -> Result<(), Error> {
    let name = &tested.name;
    // A default-constructed object borrows nothing.
    let ty = if tested.is_generic {
        format!("{name}<'static>")
    } else {
        name.clone()
    };

    for (suffix, pack) in [
        ("bpack_roundtrip", "Bin"),
        ("jpack_roundtrip", "Json"),
        ("ypack_roundtrip", "Yaml"),
    ] {
        write_test(
            tests,
            name,
            suffix,
            &format!("iop_testable::roundtrip::<{ty}>(&iop_env::ENV, Pack::{pack});"),
        )?;
    }
    write_test(
        tests,
        name,
        "constraints",
        &format!("iop_testable::check_constraints::<{ty}>();"),
    )?;
    if tested.has_parent {
        write_test(
            tests,
            name,
            "cast",
            &format!("iop_testable::cast::<{ty}>();"),
        )?;
    }

    Ok(())
}
