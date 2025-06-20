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
//! The main entry point is the structure [`WafEnvParams`].

use bindgen::Builder;
use serde::Deserialize;
use std::fs::File;
use std::io::BufReader;
use std::path::{Path, PathBuf};
use std::{env, error, fs, io};

// {{{ helpers

fn set_readonly(path: &Path, readonly: bool) -> io::Result<()> {
    let mut permissions = fs::metadata(path)?.permissions();
    permissions.set_readonly(readonly);
    fs::set_permissions(path, permissions)
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
}

impl WafBuildEnvJson {
    pub fn read(path: &Path) -> Result<WafBuildEnvJson, Box<dyn error::Error>> {
        // Open the file in read-only mode with buffer.
        let file = File::open(path)?;
        let reader = BufReader::new(file);

        // Read the JSON contents of the file as an instance of `WafBuildEnvJson`.
        let json_env = serde_json::from_reader(reader)?;

        // Return the json struct
        Ok(json_env)
    }
}

// }}}
// {{{ WafEnvParams

pub struct WafEnvParams {
    package_dir: PathBuf,
    waf_env_json_file: PathBuf,
    binding_gen_file: PathBuf,
    static_wrapper_path: PathBuf,
    json_env: WafBuildEnvJson,
}

impl WafEnvParams {
    /// Read the `_waf_build_env.json` file from the cargo manifest directory using the library.
    pub fn read() -> Result<Self, Box<dyn error::Error>> {
        let package_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

        let waf_env_json_file = package_dir.join("_waf_build_env.json");
        if !waf_env_json_file.is_file() {
            return Err("build.rs couldn't find _waf_build_env.json (not using waf?)".into());
        }

        let binding_gen_file = package_dir.join("_bindings.rs");

        let out_dir = PathBuf::from(env::var("OUT_DIR")?);
        let static_wrapper_path = out_dir.join("static-wrappers.c");

        let json_env = WafBuildEnvJson::read(&waf_env_json_file)?;

        Ok(WafEnvParams {
            package_dir,
            waf_env_json_file,
            binding_gen_file,
            static_wrapper_path,
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
        let mut builder = Builder::default()
            // Tell cargo to invalidate the built crate whenever any of the
            // included header files changed.
            .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
            .wrap_static_fns(true)
            .wrap_static_fns_path(self.static_wrapper_path.to_str().unwrap());

        // Add the waf environment arguments
        for define in &self.json_env.defines {
            builder = builder.clang_arg(format!("-D{define}"));
        }
        for include in &self.json_env.includes {
            builder = builder.clang_arg(format!("-I{include}"));
        }

        // Call the callback to add the headers and exported functions.
        builder = cb(builder)?;

        // Finish the builder and generate the bindings.
        let bindings = builder.generate()?;

        // Remove file if it exists
        if self.binding_gen_file.is_file() {
            set_readonly(&self.binding_gen_file, false)?;
            fs::remove_file(&self.binding_gen_file)?;
        }

        // Write the binding to the files.
        bindings.write_to_file(self.binding_gen_file.to_str().unwrap())?;

        // Set the file as read-only
        set_readonly(&self.binding_gen_file, true)?;

        // If the wrapper file is generated, then, compile it into a stlib.
        if self.static_wrapper_path.is_file() {
            let mut cc = cc::Build::new();

            cc.compiler(&self.json_env.cc);

            cc.file(self.static_wrapper_path.to_str().unwrap());

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

            let add_cflags = ["-Wno-missing-prototypes", "-Wno-missing-declarations"];

            for flag in add_cflags {
                cc.flag(flag);
            }

            cc.compile("libcommon-static-wrappers");
            // XXX: nice thing about cc it emits cargo metadata
            // cargo:rustc-link-search=native=...
            // so the linker can find the compiled lib.
        }

        Ok(())
    }
}

// }}}
