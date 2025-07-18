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

//! # Waf Cargo binding library
//!
//! This library helps binding the Waf build system and Cargo build system.
//!
//! The Waf build system generates a `.waf-build/waf_build_env.json` to pass the environment for
//! the package using this library.
//! This library is used in build scripts.
//!
//! The main entry point is the structure [`WafBuild`].

use bindgen::Builder;
use bindgen::callbacks::{DeriveInfo, ItemInfo, ParseCallbacks, TypeKind};
use serde::Deserialize;
use serde::de::DeserializeOwned;
use std::collections::{HashMap, HashSet};
use std::fs::File;
use std::io::{BufReader, Cursor, Write as _};
use std::path::{Path, PathBuf};
use std::{env, error, fs, io};
use syn::ext::IdentExt as _;
use syn::{
    File as SynFile, Ident, Item, ItemMod, parse_str,
    visit_mut::{VisitMut, visit_file_mut, visit_item_mod_mut},
};

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
    // Open the file in read-only mode with buffer.
    let file = File::open(path)?;
    let reader = BufReader::new(file);

    // Read the JSON contents of the file as an instance of `WafBuildEnvJson`.
    let json = serde_json::from_reader(reader)?;

    // Return the json struct
    Ok(json)
}

/// Get the waf build directory for the given package.
fn get_pkg_waf_build_dir(is_pic_profile: bool, pkg_dir: &Path) -> PathBuf {
    let mut pkg_waf_build_name = ".waf-build".to_owned();
    if is_pic_profile {
        pkg_waf_build_name += "-pic";
    }
    pkg_dir.join(pkg_waf_build_name)
}

// }}}
// {{{ Retrieve generated items

struct GeneratedItemsVisitor(Vec<String>);

#[allow(clippy::renamed_function_params)]
impl VisitMut for GeneratedItemsVisitor {
    fn visit_file_mut(&mut self, node: &mut SynFile) {
        self.visit_items(&node.items);
        visit_file_mut(self, node);
    }

    fn visit_item_mod_mut(&mut self, item_mod: &mut ItemMod) {
        if let Some((_, items)) = &item_mod.content {
            self.visit_items(items);
        }
        visit_item_mod_mut(self, item_mod);
    }
}

impl GeneratedItemsVisitor {
    fn visit_items(&mut self, items: &[Item]) {
        for item in items {
            if let Some(ident) = GeneratedItemsVisitor::get_item_ident(item) {
                let name = ident.unraw().to_string();
                if name != "_" {
                    self.0.push(name);
                }
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
            _ => None,
        }
    }
}

fn retrieve_generated_items(content: &str) -> Vec<String> {
    let mut file =
        parse_str::<SynFile>(content).expect("bindgen should generate a valid rust file");
    let mut visitor = GeneratedItemsVisitor(Vec::new());

    visitor.visit_file_mut(&mut file);
    visitor.0
}

// }}}
// {{{ Buildgen parse callbacks

#[derive(Debug)]
struct LibcommonParseCallbacks {
    blocked_items: HashSet<String>,
}

impl LibcommonParseCallbacks {
    pub fn new(blocked_items: HashSet<String>) -> Self {
        Self { blocked_items }
    }
}

impl ParseCallbacks for LibcommonParseCallbacks {
    /// Block the items in the list
    fn block_item(&self, item_info: ItemInfo<'_>) -> bool {
        self.blocked_items.contains(item_info.name)
    }

    /// Provide a list of custom derive attributes.
    ///
    /// If no additional attributes are wanted, this function should return an
    /// empty `Vec`.
    fn add_derives(&self, info: &DeriveInfo<'_>) -> Vec<String> {
        let mut res: Vec<String> = vec![];

        // Add IOP derives traits.
        // FIXME: We should use the source file to detect that we are indeed using an IOP
        //        (check that the file ends with `-t.iop.h`).
        //        Unfortunately, this callback does not provide the source file context, and
        //        `include_file()` and `header_file()` only triggers when entering a new source
        //        file, and not when exiting the source file, we cannot know here in which source
        //        we are in.
        //        The solution might be to add the source location to this callback in bindgen.
        if info.name.ends_with("__t") {
            match info.kind {
                TypeKind::Enum => {
                    res.push("::libcommon_derive::IopEnum".to_owned());
                }
                TypeKind::Union => {
                    res.push("::libcommon_derive::IopUnion".to_owned());
                }
                TypeKind::Struct => {
                    res.push("::libcommon_derive::IopStruct".to_owned());
                }
            }
        }

        res
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

pub struct WafBuild {
    is_pic_profile: bool,
    pkg_waf_build_dir: PathBuf,
    waf_env_json_file: PathBuf,
    json_env: WafBuildEnvJson,
}

impl WafBuild {
    /// Read the `.waf-build*/waf_build_env.json` file from the cargo manifest directory using the library.
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

    /// Generate the bindings of C code using bindgen.
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
    /// ```
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
    pub fn generate_bindings(
        &self,
        cb: fn(Builder) -> Result<Builder, Box<dyn error::Error>>,
    ) -> Result<(), Box<dyn error::Error>> {
        // Build the different paths
        let binding_gen_file = self.pkg_waf_build_dir.join("bindings.rs");
        let binding_items_file = self.pkg_waf_build_dir.join("bindings_items.json");

        let out_dir = PathBuf::from(env::var("OUT_DIR")?);
        let static_wrapper_path = out_dir.join("static-wrappers.c");

        // Generate the list of blocked items
        let mut blocked_items: HashSet<String> = HashSet::new();
        for dep_path_str in self.json_env.local_recursive_dependencies.values() {
            let dep_path = Path::new(dep_path_str);
            let dep_pkg_waf_build_dir = get_pkg_waf_build_dir(self.is_pic_profile, dep_path);
            let dep_bind_json = dep_pkg_waf_build_dir.join("bindings_items.json");

            if !dep_bind_json.exists() {
                continue;
            }

            // Read the dep binding items json file
            let dep_items: Vec<String> = read_json_file(&dep_bind_json)?;

            // Add the items to the block list
            blocked_items.extend(dep_items);
        }

        let mut builder = Builder::default()
            // Tell cargo to invalidate the built crate whenever any of the
            // included header files changed.
            .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
            .parse_callbacks(Box::new(LibcommonParseCallbacks::new(blocked_items)))
            .wrap_static_fns(true)
            .wrap_static_fns_path(
                static_wrapper_path
                    .to_str()
                    .ok_or_else(|| "invalid static wrapper path".to_owned())?,
            );

        // Add the waf environment arguments
        for define in &self.json_env.defines {
            builder = builder.clang_arg(format!("-D{define}"));
        }
        for include in &self.json_env.includes {
            builder = builder.clang_arg(format!("-I{include}"));
        }

        // Create a new type for all IOP enums.
        builder = builder.newtype_global_enum(".*__t");

        // Call the callback to add the headers and exported functions.
        builder = cb(builder)?;

        // Finish the builder and generate the bindings.
        let bindings = builder.generate()?;

        // Write the bindings to a local buffer first.
        let mut bindings_buff = Cursor::new(Vec::<u8>::new());
        bindings.write(Box::new(&mut bindings_buff))?;

        // Get the generated items.
        let item_names = retrieve_generated_items(str::from_utf8(bindings_buff.get_ref())?);

        // Write the binding to the files.
        write_read_only_file(&binding_gen_file, || {
            let mut file = File::create(&binding_gen_file)?;
            file.write_all(bindings_buff.get_ref())?;
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
        }

        // Generate the binding items file
        write_read_only_file(&binding_items_file, || {
            let json_data = serde_json::to_string_pretty(&item_names)?;
            let mut file = File::create(&binding_items_file)?;
            file.write_all(json_data.as_bytes())?;
            Ok(())
        })?;

        Ok(())
    }
}

// }}}
