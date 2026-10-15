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

//! Connect the build script of a package to the waf build of lib-common.
//!
//! waf builds the C code, and calls cargo to build the Rust code. For each package that it
//! builds, waf writes a `.waf-cargo-build/waf_build_env.json` file next to its `Cargo.toml`. This
//! file holds the include paths, the libraries and the flags of the C code that the package uses.
//!
//! [`WafBuild`] reads this file in the `build.rs` of the package. It gives the link instructions
//! to cargo, and it runs `bindgen` to generate the Rust bindings of the C headers.
//!
//! # Example
//!
//! The `build.rs` of a package that uses the C functions declared in its `wrapper.h`:
//!
//! ```no_run
//! use std::error;
//!
//! use waf_cargo_build::WafBuild;
//!
//! fn main() -> Result<(), Box<dyn error::Error>> {
//!     let waf_build = WafBuild::read_build_env()?;
//!
//!     waf_build.print_cargo_instructions();
//!     waf_build.generate_bindings(|builder| Ok(builder.header("wrapper.h")))?;
//!     Ok(())
//! }
//! ```
//!
//! The package then includes the bindings in its `bindings` module:
//!
//! ```ignore
//! #[waf_cargo_build::bindings_mod]
//! pub mod bindings {
//!     pub use libcommon::bindings::*;
//!     waf_cargo_build::include_bindings!();
//! }
//! ```
//!
//! The bindings of a package leave out the C items that the bindings of its dependencies
//! already define. The `pub use` line gives access to them.
//!
//! `README-rust.adoc`, at the root of lib-common, describes the waf side.

use bindgen::callbacks::{AllowOrBlockItem, ItemInfo, ParseCallbacks};
use bindgen::{Builder, EnumVariation, Formatter};
use quote::format_ident;
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::{env, error, fs, io};
use syn::ext::IdentExt as _;
use syn::{File as SynFile, ForeignItem, Ident, Item, ItemUse, UseTree, parse_str};

mod qhash_bindgen;
mod qvector_bindgen;

// Reexport proc-macros for easy use.
pub use waf_cargo_build_macros::{bindings_mod, include_bindings};

// {{{ Helpers

/// Set the given file as readonly or not.
fn set_readonly(path: &Path, readonly: bool) -> io::Result<()> {
    let mut permissions = fs::metadata(path)?.permissions();
    permissions.set_readonly(readonly);
    fs::set_permissions(path, permissions)
}

/// Write the file and set it as read only after write
fn write_read_only_file<F>(path: &Path, write_cb: F) -> Result<(), Box<dyn error::Error>>
where
    F: FnOnce() -> Result<(), Box<dyn error::Error>>,
{
    if path.is_file() {
        set_readonly(path, false)?;
        fs::remove_file(path)?;
    }

    // Write the file with the callback.
    write_cb()?;

    // Set the file as read-only
    set_readonly(path, true)?;

    Ok(())
}

/// Read a JSON file to a structure.
fn read_json_file<T>(path: &Path) -> Result<T, Box<dyn error::Error>>
where
    T: DeserializeOwned,
{
    // Read the whole file first: `serde_json::from_reader()` is much slower.
    Ok(serde_json::from_slice(&fs::read(path)?)?)
}

/// Get the path that reaches `crate_name` from the generated bindings.
///
/// The bindings of a crate cannot reach that crate by its name, so return `crate` when generating
/// the bindings of the crate that declares the item to name.
///
/// # Panics
///
/// `$CARGO_MANIFEST_DIR` is missing.
fn get_crate_ident(crate_name: &str) -> Ident {
    let crate_dir = format!("/rust/{}", crate_name.replace('_', "-"));

    if env::var("CARGO_MANIFEST_DIR")
        .expect("missing $CARGO_MANIFEST_DIR")
        .ends_with(&crate_dir)
    {
        format_ident!("crate")
    } else {
        format_ident!("{crate_name}")
    }
}

/// Get the waf build directory for the given package.
fn get_pkg_waf_build_dir(is_pic_profile: bool, pkg_dir: &Path) -> PathBuf {
    let mut pkg_waf_build_name = ".waf-cargo-build".to_owned();
    if is_pic_profile {
        pkg_waf_build_name += "-pic";
    }
    pkg_dir.join(pkg_waf_build_name)
}

// }}}
// {{{ GeneratedItemsVisitor: retrieve generated items

struct GeneratedItemsVisitor(Vec<String>);

impl GeneratedItemsVisitor {
    fn visit_item(&mut self, item: &Item) {
        if let Some(ident) = Self::get_item_ident(item) {
            let name = ident.unraw().to_string();
            if name != "_" {
                self.0.push(name);
            }
        }
    }

    fn get_item_ident(item: &Item) -> Option<&Ident> {
        match item {
            Item::Type(item_val) => Some(&item_val.ident),
            Item::Struct(item_val) => Some(&item_val.ident),
            Item::Const(item_val) => Some(&item_val.ident),
            Item::Fn(item_val) => Some(&item_val.sig.ident),
            Item::Enum(item_val) => Some(&item_val.ident),
            Item::Union(item_val) => Some(&item_val.ident),
            Item::Static(item_val) => Some(&item_val.ident),
            Item::ForeignMod(item_val) => {
                assert_eq!(item_val.items.len(), 1);
                match &item_val.items[0] {
                    ForeignItem::Fn(foreign_item_val) => Some(&foreign_item_val.sig.ident),
                    ForeignItem::Static(foreign_item_val) => Some(&foreign_item_val.ident),
                    ForeignItem::Type(foreign_item_val) => Some(&foreign_item_val.ident),
                    _ => None,
                }
            }
            Item::Use(item_use) => Some(Self::get_item_use_ident(item_use)),
            _ => None,
        }
    }

    // Note: this helper is intentionally strict and only matches the exact
    // AST pattern `pub use self::orig as alias;`. Other `use` statements can
    // also introduce renames (e.g., `pub use crate::a::b as x;`,
    // `pub use self::a::{b as x, c};`, or nested path/group forms).
    // For now, the strict match is sufficient because bindgen currently
    // emits only this specific form.
    fn get_item_use_ident(item_use: &ItemUse) -> &Ident {
        // EXACTLY match: `pub use self::orig as alias;`
        // must be `pub`
        assert!(matches!(item_use.vis, syn::Visibility::Public(_)));

        // must not have a leading ::
        assert!(item_use.leading_colon.is_none());

        // must be `self::orig as alias`
        if let UseTree::Path(path) = &item_use.tree {
            assert_eq!(path.ident, "self");

            if let UseTree::Rename(rename) = &*path.tree {
                return &rename.rename;
            }
        }

        panic!("unexpected `use` form in bindgen output");
    }
}

// }}}
// {{{ process_bindings (visiting generated code)

struct ProcessedBindings {
    generated_items: Vec<String>,
    extra_qvector_bindings: String,
    extra_qhash_bindings: String,
}

fn process_bindings(content: &str) -> ProcessedBindings {
    let file = parse_str::<SynFile>(content).expect("bindgen should generate a valid rust file");
    let mut qvector_bindings = qvector_bindgen::QVectorBindingsGenerator::new();
    let mut qhash_bindings = qhash_bindgen::QHashBindingsGenerator::new();

    for item in &file.items {
        qvector_bindings.visit_item(item);
        qhash_bindings.visit_item(item);
    }

    ProcessedBindings {
        generated_items: item_names(&file),
        extra_qvector_bindings: qvector_bindings.into_bindings(),
        extra_qhash_bindings: qhash_bindings.into_bindings(),
    }
}

/// Parses a `<pkg>.iop.rs` file. `iopc` wrote it, so invalid Rust is a bug
/// of `iopc`.
fn parse_native_iop_file(content: &str) -> SynFile {
    parse_str::<SynFile>(content).expect("iopc-generated .iop.rs file should be valid rust")
}

/// The names of the top-level items of `file`: a bindgen output or a
/// `<pkg>.iop.rs` file that `iopc` writes.
fn item_names(file: &SynFile) -> Vec<String> {
    let mut generated_items = GeneratedItemsVisitor(Vec::new());

    for item in &file.items {
        generated_items.visit_item(item);
    }

    generated_items.0
}

// }}}
// {{{ Native IOP bindings

/// The file where a package lists the native bindings that its bindgen run
/// read. A package depending on it reuses them instead of parsing the same
/// `.iop.rs` files again.
const NATIVE_IOP_PKGS_FILE: &str = "native_iop_pkgs.json";

/// The native bindings that `iopc --language rust` writes for an IOP
/// package.
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct NativeIopPkg {
    /// The `<pkg>.iop.rs` file.
    pub path: PathBuf,
    /// The prefix of the names of the package: `a__b__` for `a.b`.
    pub prefix: String,
    /// The names of the items that the file defines.
    pub items: Vec<String>,
}

impl NativeIopPkg {
    /// Reads the `<pkg>.iop.rs` file at `path`.
    ///
    /// Returns `None` if the file defines no item. `iopc` writes such a
    /// file for a package with only interfaces or modules. That package
    /// has no native definition to substitute, so bindgen keeps its C
    /// definitions.
    fn read(path: PathBuf) -> Result<Option<Self>, Box<dyn error::Error>> {
        let content = fs::read_to_string(&path)
            .map_err(|e| format!("cannot read `{}`: {e}", path.display()))?;
        let items = item_names(&parse_native_iop_file(&content));

        if items.is_empty() {
            return Ok(None);
        }

        // The file name does not give the package: the file of `a.b` is
        // `b.iop.rs`. `iopc` writes the package name at the top.
        let name = content
            .lines()
            .find_map(|line| line.strip_prefix("// IOP package: "))
            .ok_or_else(|| format!("`{}` names no IOP package", path.display()))?;

        Ok(Some(Self {
            prefix: format!("{}__", name.replace('.', "__")),
            path,
            items,
        }))
    }
}

/// The `<pkg>.iop.rs` file that `iopc` writes next to the IOP header
/// `header`, if it is one.
///
/// The path is canonical: it ends up in an `include!()` of a file in
/// `OUT_DIR`, from which a relative path does not resolve.
fn native_iop_file_of(header: &str) -> Option<PathBuf> {
    let header = Path::new(header);
    let base = header.file_name()?.to_str()?.strip_suffix(".iop.h")?;
    let stem = base
        .strip_suffix("-tdef")
        .or_else(|| base.strip_suffix("-t"))
        .unwrap_or(base);

    header
        .with_file_name(format!("{stem}.iop.rs"))
        .canonicalize()
        .ok()
}

/// The native bindings that a crate splices into its own `bindings.rs`:
/// those of every IOP package that it binds, minus the packages that a
/// dependency already provides. The crate imports these from that
/// dependency.
fn iop_pkgs_to_splice<'a>(
    pkgs: &'a [NativeIopPkg],
    blocked_items: &HashSet<String>,
) -> Vec<&'a NativeIopPkg> {
    pkgs.iter()
        .filter(|pkg| !pkg.items.iter().any(|item| blocked_items.contains(item)))
        .collect()
}

/// Writes the `include!()` of the native bindings `pkgs` to `bindings.rs`.
///
/// `pkgs` must be sorted: cargo rebuilds the crate when `bindings.rs`
/// changes, so its content must not change from one run to the next.
fn write_native_iop_bindings(file: &mut File, pkgs: &[&NativeIopPkg]) -> io::Result<()> {
    if pkgs.is_empty() {
        return Ok(());
    }

    file.write_all("\n// {{{ Native IOP bindings\n\n".as_bytes())?;
    for pkg in pkgs {
        writeln!(file, "include!(\"{}\");", pkg.path.display())?;
    }
    file.write_all("\n// }}}".as_bytes())
}

// }}}
// {{{ Buildgen parse callbacks

/// Suffix that [`LibcommonParseCallbacks::item_name`] appends to the C
/// name of a native IOP item. `iopc` writes an alias with that name for
/// each type.
const NATIVE_ITEM_SUFFIX: &str = "_raw";

#[derive(Debug)]
struct LibcommonParseCallbacks {
    blocked_items: Rc<HashSet<String>>,
    /// The native bindings that the dependencies have already read, by
    /// path.
    dep_pkgs: HashMap<PathBuf, NativeIopPkg>,
    /// The native bindings of the IOP headers that bindgen has walked.
    pkgs: Rc<RefCell<Vec<NativeIopPkg>>>,
    /// The item names of `pkgs`. bindgen looks up each of its items.
    pkg_items: RefCell<HashSet<String>>,
    /// The prefixes of `pkgs`, `a__b__` for `a.b`.
    pkg_prefixes: RefCell<HashSet<String>>,
    /// The `<pkg>.iop.rs` files already read, with or without items.
    seen_files: RefCell<HashSet<PathBuf>>,
}

impl LibcommonParseCallbacks {
    pub fn new(
        blocked_items: Rc<HashSet<String>>,
        dep_pkgs: HashMap<PathBuf, NativeIopPkg>,
        pkgs: Rc<RefCell<Vec<NativeIopPkg>>>,
    ) -> Self {
        Self {
            blocked_items,
            dep_pkgs,
            pkgs,
            pkg_items: RefCell::default(),
            pkg_prefixes: RefCell::default(),
            seen_files: RefCell::default(),
        }
    }

    /// A `@ctype` name does not carry the `<pkg>__` prefix, so only the
    /// item names of the package find it.
    fn is_native_pkg_item(&self, name: &str) -> bool {
        if self.pkg_items.borrow().contains(name) {
            return true;
        }

        // A prefix ends with `__`: look up each start of the name that ends
        // there. bindgen calls this for each item of each header, and a crate
        // reads hundreds of packages.
        let pkg_prefixes = self.pkg_prefixes.borrow();
        name.match_indices("__").any(|(pos, sep)| {
            name.get(..pos + sep.len())
                .is_some_and(|prefix| pkg_prefixes.contains(prefix))
        })
    }
}

impl ParseCallbacks for LibcommonParseCallbacks {
    fn include_file(&self, filename: &str) {
        let Some(path) = native_iop_file_of(filename) else {
            return;
        };

        // Each package has up to three headers.
        if !self.seen_files.borrow_mut().insert(path.clone()) {
            return;
        }
        println!("cargo::rerun-if-changed={}", path.display());
        // Parsing a `.iop.rs` file costs: reuse what a dependency read.
        let read = match self.dep_pkgs.get(&path) {
            Some(pkg) => Ok(Some(pkg.clone())),
            None => NativeIopPkg::read(path),
        };
        match read {
            Ok(Some(pkg)) => {
                self.pkg_items
                    .borrow_mut()
                    .extend(pkg.items.iter().cloned());
                self.pkg_prefixes.borrow_mut().insert(pkg.prefix.clone());
                self.pkgs.borrow_mut().push(pkg);
            }
            Ok(None) => {}
            // The C definitions of the package stay, which surfaces later
            // as an unrelated type error.
            Err(e) => println!("cargo::warning={e}"),
        }
    }

    fn allow_or_block_item(&self, item_info: &ItemInfo<'_>) -> Option<AllowOrBlockItem> {
        (self.blocked_items.contains(item_info.name) || self.is_native_pkg_item(item_info.name))
            .then_some(AllowOrBlockItem::Block)
    }

    fn item_name(&self, item_info: ItemInfo<'_>) -> Option<String> {
        self.is_native_pkg_item(item_info.name)
            .then(|| format!("{}{NATIVE_ITEM_SUFFIX}", item_info.name))
    }
}

// }}}
// {{{ WafBuildEnvJson

#[derive(Default, Deserialize)]
struct WafBuildEnvJson {
    includes: Vec<String>,
    defines: Vec<String>,
    cflags: Vec<String>,
    libs: Vec<String>,
    libpaths: Vec<String>,
    link_args: Vec<String>,
    rerun_libs: Vec<String>,
    cc: String,
    local_recursive_dependencies: HashMap<String, String>,
}

impl WafBuildEnvJson {
    pub fn read(path: &Path) -> Result<WafBuildEnvJson, Box<dyn error::Error>> {
        read_json_file(path)
    }
}

// }}}
// {{{ WafBuild

/// The waf build environment of a package, read by its build script with
/// [`WafBuild::read_build_env`].
pub struct WafBuild {
    is_pic_profile: bool,
    pkg_waf_build_dir: PathBuf,
    waf_env_json_file: PathBuf,
    json_env: WafBuildEnvJson,
}

pub struct WafBuildCtx {
    pub waf: WafBuild,
    pub out_dir: PathBuf,
    /// The native bindings of every IOP package the crate binds, sorted.
    pub native_iop_pkgs: Vec<NativeIopPkg>,
}

impl WafBuild {
    /// Run the whole build script of a package built by both `waf` and
    /// `cargo`, deducing what to do from the package layout:
    ///
    /// - the `waf` build environment is read and forwarded to cargo;
    /// - if the package has a `wrapper.h`, its bindings are generated.
    ///
    /// `wrapper.h` at the package root is the switch: with one, bindgen runs
    /// on it; without one, the package gets no bindings at all -- what a
    /// package reading its dependency's `bindings` module wants, and what a
    /// missing or misnamed header silently looks like too.
    ///
    /// ```ignore
    /// use waf_cargo_build::WafBuild;
    ///
    /// fn main() -> Result<(), Box<dyn std::error::Error>> {
    ///     WafBuild::build()?;
    ///
    ///     Ok(())
    /// }
    /// ```
    ///
    /// A package needing more -- extra bindgen options, or tests to
    /// generate -- can still drive [`Self::read_build_env`],
    /// [`Self::print_cargo_instructions`] and [`Self::generate_bindings`]
    /// itself.
    ///
    /// # Errors
    ///
    /// I/O errors, missing file, or bindgen failure.
    pub fn build() -> Result<WafBuildCtx, Box<dyn error::Error>> {
        let waf_build = Self::read_build_env()?;

        waf_build.print_cargo_instructions();

        let out_dir = PathBuf::from(env::var("OUT_DIR")?);
        let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

        let native_iop_pkgs = if manifest_dir.join("wrapper.h").is_file() {
            // The bindings of a package block the items that its
            // dependencies generate, so the build scripts of the
            // dependencies must run first. Cargo orders them only for a
            // direct dependency with a `links` key: without one, a single
            // `cargo build` of several packages can run them in any order.
            if env::var_os("CARGO_MANIFEST_LINKS").is_none() {
                return Err("a package with a `wrapper.h` needs a `links` key".into());
            }
            waf_build.generate_bindings(|b| Ok(b.header("wrapper.h")))?
        } else {
            Vec::new()
        };

        Ok(WafBuildCtx {
            waf: waf_build,
            out_dir,
            native_iop_pkgs,
        })
    }

    /// Read the `.waf-cargo-build*/waf_build_env.json` file from the cargo manifest directory
    /// using the library.
    ///
    /// # Result
    ///
    /// The created [`WafBuild`] for the package.
    ///
    /// # Errors
    ///
    /// I/O errors or missing file.
    ///
    /// # Panics
    ///
    /// Unexpected missing or wrong environment variables.
    pub fn read_build_env() -> Result<Self, Box<dyn error::Error>> {
        let out_dir = PathBuf::from(env::var("OUT_DIR")?);
        // The out directory is located in the cargo build directory in
        // 'target/<profile>/build/<pkg-name>-<obj-hash>/out.
        // We want to get 'target/<profile>'.
        let cargo_build_dir = out_dir
            .parent()
            .ok_or_else(|| "missing parent of OUTDIR".to_owned())?
            .parent()
            .ok_or_else(|| "missing parent of parent of OUTDIR".to_owned())?
            .parent()
            .ok_or_else(|| "missing parent of parent of parent of OUTDIR".to_owned())?;

        let profile_name = cargo_build_dir
            .file_name()
            .ok_or_else(|| "no filename of cargo build dir".to_owned())?
            .to_str()
            .ok_or_else(|| "unable to convert filename to string".to_owned())?;
        let is_pic_profile = profile_name.ends_with("-pic");

        let package_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);
        let pkg_waf_build_dir = get_pkg_waf_build_dir(is_pic_profile, &package_dir);

        let waf_env_json_file = pkg_waf_build_dir.join("waf_build_env.json");
        if !waf_env_json_file.is_file() {
            return Err(format!(
                "build.rs couldn't find {} (not using waf?)",
                waf_env_json_file.display()
            )
            .into());
        }

        let json_env = WafBuildEnvJson::read(&waf_env_json_file)?;

        Ok(Self {
            is_pic_profile,
            pkg_waf_build_dir,
            waf_env_json_file,
            json_env,
        })
    }

    /// Print the cargo instructions for the compilation of the package using this library.
    ///
    /// See <https://doc.rust-lang.org/cargo/reference/build-scripts.html#outputs-of-the-build-script>
    pub fn print_cargo_instructions(&self) {
        let waf_cflags = format!("{:?}", self.json_env.cflags);
        let waf_defines = format!("{:?}", self.json_env.defines);
        let waf_includes = format!("{:?}", self.json_env.includes);
        let waf_libs = format!("{:?}", self.json_env.libs);
        let waf_libpaths = format!("{:?}", self.json_env.libpaths);

        // metadata exported for dependent packages:
        // https://doc.rust-lang.org/cargo/reference/build-script-examples.html#using-another-sys-crate
        println!("cargo::metadata=defines={waf_defines}");
        println!("cargo::metadata=includes={waf_includes}");
        println!("cargo::metadata=cflags={waf_cflags}");
        println!("cargo::metadata=libs={waf_libs}");
        println!("cargo::metadata=libpaths={waf_libpaths}");

        // emits link libs
        for link_arg in &self.json_env.link_args {
            println!("cargo::rustc-link-arg={link_arg}");
        }
        for libpath in &self.json_env.libpaths {
            println!("cargo::rustc-link-search={libpath}");
        }
        for lib in &self.json_env.libs {
            println!("cargo::rustc-link-lib={lib}");
        }

        // Rerun if .cargo-build/waf_build_env.json or one of the libs have changed
        println!(
            "cargo::rerun-if-changed={}",
            self.waf_env_json_file.display()
        );
        for rerun_lib in &self.json_env.rerun_libs {
            println!("cargo::rerun-if-changed={rerun_lib}");
        }

        println!("cargo::rustc-link-arg=-no-pie");

        if self.is_pic_profile {
            // Only "pic" is supported for now.
            println!("cargo::rustc-link-arg=-pic");
        }

        println!(
            "cargo::rustc-env=PKG_WAF_BUILD_DIR={}",
            self.pkg_waf_build_dir.display()
        );
    }

    /// The contents of the JSON file `file_name` that each dependency of
    /// this package wrote in its waf build directory.
    ///
    /// # Errors
    ///
    /// A dependency's file cannot be read.
    fn read_deps_json_files<T: DeserializeOwned>(
        &self,
        file_name: &str,
    ) -> Result<Vec<T>, Box<dyn error::Error>> {
        let mut res = Vec::new();
        for dep_path_str in self.json_env.local_recursive_dependencies.values() {
            let dep_pkg_waf_build_dir =
                get_pkg_waf_build_dir(self.is_pic_profile, Path::new(dep_path_str));
            let file = dep_pkg_waf_build_dir.join(file_name);

            if !file.exists() {
                continue;
            }

            // The files of a dependency decide what bindgen generates here:
            // the Rust items, and the static wrappers compiled next to them.
            // Rerun when they change, otherwise a stale block list makes this
            // package wrap a function that the dependency wraps too, and the
            // two definitions collide when the final binary is linked.
            //
            // A missing file is not declared: cargo reruns a build script on
            // every build when a declared path does not exist, and most
            // dependencies generate no bindings at all.
            println!("cargo::rerun-if-changed={}", file.display());

            res.push(read_json_file(&file)?);
        }

        Ok(res)
    }

    /// The items that this package must not generate, because a dependency
    /// already exports them.
    ///
    /// # Errors
    ///
    /// A dependency's `bindings_items.json` cannot be read.
    fn blocked_items(&self) -> Result<HashSet<String>, Box<dyn error::Error>> {
        let deps_items: Vec<Vec<String>> = self.read_deps_json_files("bindings_items.json")?;

        Ok(deps_items.into_iter().flatten().collect())
    }

    /// The native bindings that the dependencies of this package have read,
    /// by path. See [`NATIVE_IOP_PKGS_FILE`].
    ///
    /// # Errors
    ///
    /// A dependency's file cannot be read.
    fn dep_native_iop_pkgs(&self) -> Result<HashMap<PathBuf, NativeIopPkg>, Box<dyn error::Error>> {
        let deps_pkgs: Vec<Vec<NativeIopPkg>> = self.read_deps_json_files(NATIVE_IOP_PKGS_FILE)?;

        Ok(deps_pkgs
            .into_iter()
            .flatten()
            .map(|pkg| (pkg.path.clone(), pkg))
            .collect())
    }

    /// A bindgen
    /// [`Builder`](https://docs.rs/bindgen/0.72/bindgen/struct.Builder.html)
    /// set up with the `waf` environment of the package and the conventions
    /// of the repository, with no header and no callback.
    ///
    /// A package that reads its headers a second time, with another
    /// configuration, starts from this.
    #[must_use]
    pub fn bindgen_builder(&self) -> Builder {
        let mut builder = Builder::default();

        // Add the waf environment arguments
        for define in &self.json_env.defines {
            builder = builder.clang_arg(format!("-D{define}"));
        }
        for include in &self.json_env.includes {
            builder = builder.clang_arg(format!("-I{include}"));
        }

        // Use Rust enums as it is much more easy to use in Rust, and works well with match().
        // We just need to be careful with it, as we always are :).
        builder = builder.default_enum_style(EnumVariation::Rust {
            non_exhaustive: false,
        });

        // Disable bindgen layout tests. They are incompatible with our
        // cross-crate type deduplication: a type blocked here and re-imported
        // from a dependency may have been generated there from an incomplete
        // definition (e.g. `sockaddr_in6` is a 1-byte opaque placeholder in
        // libcommon-core because glibc only forward-declares it), so the
        // `size_of`/`offset_of` assertions of structs embedding it by value no
        // longer hold. Some system structs also cannot be laid out identically
        // to C in Rust at all (`packed` structs with an over-aligned member,
        // e.g. `sctp_paddrparams` with `sockaddr_storage`).
        builder = builder.layout_tests(false);

        // Format in process: `rustfmt` costs a process, started through the
        // rustup proxy, for each package.
        builder = builder.formatter(Formatter::Prettyplease);

        builder
    }

    /// Generate the bindings of C code using bindgen, and splice in the
    /// `iopc --language rust` bindings of the IOP packages they cover.
    ///
    /// This function takes a function callback to be able to specify what functions and variables
    /// to export.
    ///
    /// # Errors
    ///
    /// I/O errors or missing file.
    ///
    /// # Panics
    ///
    /// Unexpected path.
    ///
    /// # Examples
    ///
    /// ```ignore
    /// const FUNCTIONS_TO_EXPOSE: &[&str] = &[...];
    ///
    /// const VARS_TO_EXPOSE: &[&str] = &[...];
    ///
    /// waf_env_params.generate_bindings(|builder| {
    ///     let mut builder = builder;
    ///
    ///     builder = builder.header("wrapper.h");
    ///
    ///     for fun in FUNCTIONS_TO_EXPOSE.iter() {
    ///         builder = builder.allowlist_function(fun);
    ///     }
    ///
    ///     for var in VARS_TO_EXPOSE.iter() {
    ///         builder = builder.allowlist_var(var);
    ///     }
    ///
    ///     Ok(builder)
    /// })?;
    /// ```
    pub fn generate_bindings<F>(&self, cb: F) -> Result<Vec<NativeIopPkg>, Box<dyn error::Error>>
    where
        F: FnOnce(Builder) -> Result<Builder, Box<dyn error::Error>>,
    {
        // Build the different paths
        let binding_gen_file = self.pkg_waf_build_dir.join("bindings.rs");
        let binding_items_file = self.pkg_waf_build_dir.join("bindings_items.json");

        let out_dir = PathBuf::from(env::var("OUT_DIR")?);
        let static_wrapper_path = out_dir.join("static-wrappers.c");

        let blocked_items = Rc::new(self.blocked_items()?);

        let native_iop_pkgs = Rc::new(RefCell::new(Vec::new()));
        let dep_native_iop_pkgs = self.dep_native_iop_pkgs()?;
        let dep_native_iop_paths: HashSet<PathBuf> = dep_native_iop_pkgs.keys().cloned().collect();

        let mut builder = self
            .bindgen_builder()
            // Tell cargo to invalidate the built crate whenever any of the
            // included header files changed.
            .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
            .parse_callbacks(Box::new(LibcommonParseCallbacks::new(
                Rc::clone(&blocked_items),
                dep_native_iop_pkgs,
                Rc::clone(&native_iop_pkgs),
            )))
            .wrap_static_fns(true)
            .wrap_static_fns_path(
                static_wrapper_path
                    .to_str()
                    .ok_or_else(|| "invalid static wrapper path".to_owned())?,
            );

        // Call the callback to add the headers and exported functions.
        builder = cb(builder)?;

        // Finish the builder and generate the bindings.
        let bindings_str = builder.generate()?.to_string();

        // Process bindings, to get the generated item names.
        let mut processed_bindings = process_bindings(&bindings_str);

        // Splice in the native IOP bindings, and register their items like
        // bindgen's own: a crate depending on this one then imports them
        // from here instead of generating its own copy.
        let mut native_iop_pkgs = native_iop_pkgs.take();

        native_iop_pkgs.sort();

        let spliced_pkgs = iop_pkgs_to_splice(&native_iop_pkgs, &blocked_items);

        processed_bindings.generated_items.extend(
            spliced_pkgs
                .iter()
                .flat_map(|pkg| pkg.items.iter().cloned()),
        );

        // Write the binding to the files.
        write_read_only_file(&binding_gen_file, || {
            let mut file = File::create(&binding_gen_file)?;

            file.write_all("// {{{ Bindings generated by bindgen\n\n".as_bytes())?;
            file.write_all(bindings_str.as_bytes())?;
            file.write_all("\n// }}}".as_bytes())?;

            write_native_iop_bindings(&mut file, &spliced_pkgs)?;

            if !processed_bindings.extra_qvector_bindings.is_empty() {
                file.write_all(
                    "\n// {{{ qvector bindings generated by waf-cargo-build\n\n".as_bytes(),
                )?;
                file.write_all(processed_bindings.extra_qvector_bindings.as_bytes())?;
                file.write_all("\n// }}}".as_bytes())?;
            }

            if !processed_bindings.extra_qhash_bindings.is_empty() {
                file.write_all(
                    "\n// {{{ qhash bindings generated by waf-cargo-build\n\n".as_bytes(),
                )?;
                file.write_all(processed_bindings.extra_qhash_bindings.as_bytes())?;
                file.write_all("\n// }}}".as_bytes())?;
            }
            Ok(())
        })?;

        // If the wrapper file is generated, then, compile it into a stlib.
        if static_wrapper_path.is_file() {
            let mut cc = cc::Build::new();

            cc.compiler(&self.json_env.cc);

            cc.file(
                static_wrapper_path
                    .to_str()
                    .ok_or_else(|| "invalid static wrapper path".to_owned())?,
            );

            cc.no_default_flags(true);

            for define in &self.json_env.defines {
                cc.define(define, None);
            }

            for include in &self.json_env.includes {
                cc.include(include);
            }
            cc.include(env::var("CARGO_MANIFEST_DIR")?);

            for flag in &self.json_env.cflags {
                cc.flag(flag);
            }

            // Ignore all warnings in static-wrappers.
            // This is not user code, we don't care about warnings.
            cc.flag("-w");

            cc.compile("bindgen-static-wrappers");
            // XXX: nice thing about cc it emits cargo metadata
            // cargo:rustc-link-search=native=...
            // so the linker can find the compiled lib.

            // The wrappers call the very functions they wrap, so they need the
            // C libraries, which print_cargo_instructions() has already named:
            // name them again. A linker reads an archive once, in the order it
            // is given, and takes from it only what is undefined at that
            // point, so a library listed before the wrappers cannot resolve
            // what the wrappers need. Whether it happens to work depends on
            // what pulled the same archive members in earlier: it did on
            // x86-64 and it did not on aarch64, where the link failed on
            // object_class, cls_inherits and object_panic.
            for lib in &self.json_env.libs {
                println!("cargo::rustc-link-lib={lib}");
            }
        }

        // List the native bindings read, for the packages depending on this one.
        // They read the files of all their recursive dependencies, so list only
        // the bindings that no dependency has listed.
        let read_pkgs: Vec<&NativeIopPkg> = native_iop_pkgs
            .iter()
            .filter(|pkg| !dep_native_iop_paths.contains(&pkg.path))
            .collect();
        let native_iop_pkgs_file = self.pkg_waf_build_dir.join(NATIVE_IOP_PKGS_FILE);
        write_read_only_file(&native_iop_pkgs_file, || {
            fs::write(&native_iop_pkgs_file, serde_json::to_vec(&read_pkgs)?)?;
            Ok(())
        })?;

        // Generate the binding items file
        write_read_only_file(&binding_items_file, || {
            let json_data = serde_json::to_string_pretty(&processed_bindings.generated_items)?;
            let mut file = File::create(&binding_items_file)?;
            file.write_all(json_data.as_bytes())?;
            Ok(())
        })?;

        Ok(native_iop_pkgs)
    }
}

// }}}
