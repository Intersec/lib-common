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
//! The Waf build system generates a `_waf_build_env.json` to pass the environment that is used by
//! this library.
//! This library is used in build scripts.
//!
//! The main entry point is the structure [`WafBuild`].

use bindgen::callbacks::{
    AttributeInfo, DeriveInfo, DeriveTrait, DiscoveredItem, DiscoveredItemId,
    EnumVariantCustomBehavior, EnumVariantValue, FieldInfo, ImplementsTrait, IntKind, ItemInfo,
    MacroParsingBehavior,
};
use bindgen::{Builder, FieldVisibilityKind};
use serde::Deserialize;
use serde::de::DeserializeOwned;
use std::cell::RefCell;
use std::collections::HashMap;
use std::fs::File;
use std::io::BufReader;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::{env, error, fs, io};

// {{{ Helpers

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

// }}}
// {{{ Callbacks items exports

#[derive(Debug)]
struct ItemsExportCallback {
    cargo_callbacks: bindgen::CargoCallbacks,
    exported_items: Rc<RefCell<Vec<String>>>,
}

impl ItemsExportCallback {
    pub fn new(exported_items: Rc<RefCell<Vec<String>>>) -> Self {
        Self {
            cargo_callbacks: bindgen::CargoCallbacks::new(),
            exported_items,
        }
    }
}

impl bindgen::callbacks::ParseCallbacks for ItemsExportCallback {
    // {{{ Forwarded implemented methods to CargoCallback

    fn will_parse_macro(&self, _name: &str) -> MacroParsingBehavior {
        self.cargo_callbacks.will_parse_macro(_name)
    }

    fn generated_name_override(&self, _item_info: ItemInfo<'_>) -> Option<String> {
        self.cargo_callbacks.generated_name_override(_item_info)
    }

    fn generated_link_name_override(&self, _item_info: ItemInfo<'_>) -> Option<String> {
        self.cargo_callbacks
            .generated_link_name_override(_item_info)
    }

    fn int_macro(&self, _name: &str, _value: i64) -> Option<IntKind> {
        self.cargo_callbacks.int_macro(_name, _value)
    }

    fn str_macro(&self, _name: &str, _value: &[u8]) {
        self.cargo_callbacks.str_macro(_name, _value)
    }

    fn func_macro(&self, _name: &str, _value: &[&[u8]]) {
        self.cargo_callbacks.func_macro(_name, _value)
    }

    fn enum_variant_behavior(
        &self,
        _enum_name: Option<&str>,
        _original_variant_name: &str,
        _variant_value: EnumVariantValue,
    ) -> Option<EnumVariantCustomBehavior> {
        self.cargo_callbacks.enum_variant_behavior(
            _enum_name,
            _original_variant_name,
            _variant_value,
        )
    }

    fn enum_variant_name(
        &self,
        _enum_name: Option<&str>,
        _original_variant_name: &str,
        _variant_value: EnumVariantValue,
    ) -> Option<String> {
        self.cargo_callbacks
            .enum_variant_name(_enum_name, _original_variant_name, _variant_value)
    }

    fn item_name(&self, _item_info: ItemInfo) -> Option<String> {
        // XXX: Unfortunately, this called before filtering the items.
        // So it does not represent the list of exported items.
        self.cargo_callbacks.item_name(_item_info)
    }

    fn header_file(&self, _filename: &str) {
        self.cargo_callbacks.header_file(_filename)
    }

    fn include_file(&self, _filename: &str) {
        self.cargo_callbacks.include_file(_filename)
    }

    fn read_env_var(&self, _key: &str) {
        self.cargo_callbacks.read_env_var(_key)
    }

    fn blocklisted_type_implements_trait(
        &self,
        _name: &str,
        _derive_trait: DeriveTrait,
    ) -> Option<ImplementsTrait> {
        self.cargo_callbacks
            .blocklisted_type_implements_trait(_name, _derive_trait)
    }

    fn add_derives(&self, _info: &DeriveInfo<'_>) -> Vec<String> {
        self.cargo_callbacks.add_derives(_info)
    }

    fn add_attributes(&self, _info: &AttributeInfo<'_>) -> Vec<String> {
        self.cargo_callbacks.add_attributes(_info)
    }

    fn process_comment(&self, _comment: &str) -> Option<String> {
        self.cargo_callbacks.process_comment(_comment)
    }

    fn field_visibility(&self, _info: FieldInfo<'_>) -> Option<FieldVisibilityKind> {
        self.cargo_callbacks.field_visibility(_info)
    }

    // }}}

    // Get the items and it in the list
    fn new_item_found(&self, id: DiscoveredItemId, item: DiscoveredItem) {
        let item_name = match &item {
            DiscoveredItem::Struct {
                original_name: _,
                final_name,
            } => final_name,
            DiscoveredItem::Union {
                original_name: _,
                final_name,
            } => final_name,
            DiscoveredItem::Alias {
                alias_name,
                alias_for: _,
            } => alias_name,
            DiscoveredItem::Enum { final_name } => final_name,
            DiscoveredItem::Function { final_name } => final_name,
            DiscoveredItem::Method {
                final_name,
                parent: _,
            } => final_name,
        };

        self.exported_items.borrow_mut().push(item_name.into());

        self.cargo_callbacks.new_item_found(id, item)
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
    profile_suffix: String,
}

impl WafBuildEnvJson {
    pub fn read(path: &Path) -> Result<WafBuildEnvJson, Box<dyn error::Error>> {
        read_json_file(path)
    }
}

// }}}
// {{{ WafBuild

pub struct WafBuild {
    package_dir: PathBuf,
    waf_env_json_file: PathBuf,
    json_env: WafBuildEnvJson,
}

impl WafBuild {
    /// Read the `_waf_build_env.json` file from the cargo manifest directory using the library.
    pub fn read_build_env() -> Result<Self, Box<dyn error::Error>> {
        let package_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

        let waf_env_json_file = package_dir.join("_waf_build_env.json");
        if !waf_env_json_file.is_file() {
            return Err("build.rs couldn't find _waf_build_env.json (not using waf?)".into());
        }

        let json_env = WafBuildEnvJson::read(&waf_env_json_file)?;

        Ok(Self {
            package_dir,
            waf_env_json_file,
            json_env,
        })
    }

    /// Print the cargo instructions for the compilation of the package using this library.
    ///
    /// See https://doc.rust-lang.org/cargo/reference/build-scripts.html#outputs-of-the-build-script
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

        // Rerun if _waf_build_env.json or one of the libs have changed
        println!(
            "cargo::rerun-if-changed={}",
            self.waf_env_json_file.display()
        );
        for rerun_lib in &self.json_env.rerun_libs {
            println!("cargo::rerun-if-changed={rerun_lib}");
        }

        println!("cargo::rustc-link-arg=-no-pie");

        if self.json_env.profile_suffix == "-pic" {
            // Only "pic" is supported for now.
            println!("cargo::rustc-link-arg=-pic");
        }
    }

    /// Generate the bindings of C code using bindgen.
    ///
    /// This function takes a function callback to be able to specify what functions and variables
    /// to export.
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
        let exported_items = Rc::new(RefCell::new(Vec::new()));

        let binding_gen_file = self.package_dir.join("_bindings.rs");
        let binding_items_file = self.package_dir.join("_bindings_items.json");

        let out_dir = PathBuf::from(env::var("OUT_DIR")?);
        let static_wrapper_path = out_dir.join("static-wrappers.c");

        let mut builder = Builder::default()
            // Tell cargo to invalidate the built crate whenever any of the
            // included header files changed.
            .parse_callbacks(Box::new(ItemsExportCallback::new(exported_items.clone())))
            .wrap_static_fns(true)
            .wrap_static_fns_path(static_wrapper_path.to_str().unwrap());

        // Add the waf environment arguments
        for define in &self.json_env.defines {
            builder = builder.clang_arg(format!("-D{define}"));
        }
        for include in &self.json_env.includes {
            builder = builder.clang_arg(format!("-I{include}"));
        }

        // Block the items already exported by other dependencies
        for dep_path_str in self.json_env.local_recursive_dependencies.values() {
            let dep_path = Path::new(dep_path_str);
            let dep_bind_json = dep_path.join("_bindings_items.json");

            if !dep_bind_json.exists() {
                continue;
            }

            // Read the dep binding items json file
            let dep_items: Vec<String> = read_json_file(&dep_bind_json)?;

            // Block all already exported items
            for dep_item in dep_items {
                builder = builder.blocklist_item(dep_item);
            }
        }

        // Call the callback to add the headers and exported functions.
        builder = cb(builder)?;

        // Finish the builder and generate the bindings.
        let bindings = builder.generate()?;

        // Write the binding to the files.
        write_read_only_file(&binding_gen_file, || {
            bindings.write_to_file(binding_gen_file.to_str().unwrap())?;
            Ok(())
        })?;

        // If the wrapper file is generated, then, compile it into a stlib.
        if static_wrapper_path.is_file() {
            let mut cc = cc::Build::new();

            cc.compiler(&self.json_env.cc);

            cc.file(static_wrapper_path.to_str().unwrap());

            cc.no_default_flags(true);

            for define in &self.json_env.defines {
                cc.define(define, None);
            }

            for include in &self.json_env.includes {
                cc.include(include);
            }
            cc.include(&self.package_dir);

            for flag in &self.json_env.cflags {
                cc.flag(flag);
            }

            // Ignore all warnings in static-wrappers.
            // This is not user code, we don't care about warnings.
            cc.flag("-w");

            cc.compile("libcommon-static-wrappers");
            // XXX: nice thing about cc it emits cargo metadata
            // cargo:rustc-link-search=native=...
            // so the linker can find the compiled lib.
        }

        // Generate the binding items file
        write_read_only_file(&binding_items_file, || {
            let item_names: &Vec<String> = &exported_items.borrow();
            let json_data = serde_json::to_string_pretty(item_names).unwrap();
            let mut file = File::create(&binding_items_file)?;
            file.write_all(json_data.as_bytes())?;
            Ok(())
        })?;

        Ok(())
    }
}

// }}}
