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

use std::{
    env,
    fs,
    io,
    path,
    process,
};
use bindgen::Builder;

pub struct WafEnvParams {
    pub binding_gen_file: path::PathBuf,
    pub static_wrapper_path: path::PathBuf,
    pub cflags: Vec<String>,
    pub defines: Vec<String>,
    pub includes: Vec<String>,
}

pub fn decode_waf_env_params() -> WafEnvParams {
    if env::var("WAFCARGO").is_err() {
        eprintln!("build.rs couldn't find the env var WAFCARGO (not using waf?)");
        process::exit(1);
    }

    let build_dir = path::PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let binding_gen_file = build_dir.join("_bindings.rs");

    let out_dir = path::PathBuf::from(env::var("OUT_DIR").unwrap());
    let static_wrapper_path = out_dir.join("static-wrappers.c");

    let waf_cflags = env::var("WAFCARGO_CFLAGS").unwrap();
    let waf_defines = env::var("WAFCARGO_DEFINES").unwrap();
    let waf_includes = env::var("WAFCARGO_INCLUDES").unwrap();
    let waf_libs = env::var("WAFCARGO_LIBS").unwrap();
    let waf_libpaths = env::var("WAFCARGO_LIBPATHS").unwrap();

    // metadata exported for dependent packages:
    // https://doc.rust-lang.org/cargo/reference/build-script-examples.html#using-another-sys-crate
    println!("cargo::metadata=defines={waf_defines}");
    println!("cargo::metadata=includes={waf_includes}");
    println!("cargo::metadata=cflags={waf_cflags}");
    println!("cargo::metadata=libs={waf_libs}");
    println!("cargo::metadata=libpaths={waf_libpaths}");

    // emits link libs
    for lib in shlex::split(&waf_libs).unwrap() {
        println!("cargo::rustc-link-lib={lib}");
    }
    for libpath in shlex::split(&waf_libpaths).unwrap() {
        println!("cargo::rustc-link-search={libpath}");
    }

    println!("cargo::rustc-link-arg=-no-pie");

    WafEnvParams {
        binding_gen_file,
        static_wrapper_path,
        cflags: shlex::split(&waf_cflags).unwrap(),
        defines: shlex::split(&waf_defines).unwrap(),
        includes: shlex::split(&waf_includes).unwrap(),
    }
}

pub fn make_builder(waf_env_params: &WafEnvParams) -> Builder {
    let mut builder = Builder::default()
        // Tell cargo to invalidate the built crate whenever any of the
        // included header files changed.
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .wrap_static_fns(true)
        .wrap_static_fns_path(waf_env_params.static_wrapper_path.to_str().unwrap());

    for define in &waf_env_params.defines {
        builder = builder.clang_arg(format!("-D{define}"));
    }

    for include in &waf_env_params.includes {
        builder = builder.clang_arg(format!("-I{include}"));
    }

    builder
}

fn set_readonly<P: AsRef<path::Path>>(path: P, readonly : bool) -> io::Result<()> {
    let mut permissions = fs::metadata(&path)?.permissions();
    permissions.set_readonly(readonly);
    fs::set_permissions(&path,permissions)
}

pub fn generate_bindings(builder: Builder, waf_env_params: &WafEnvParams) {
    // Finish the builder and generate the bindings.
    let bindings = builder.generate()
        // Unwrap the Result and panic on failure.
        .expect("couldn't generate bindings");

    // Remove file if it exists
    if waf_env_params.binding_gen_file.is_file() {
        set_readonly(&waf_env_params.binding_gen_file, false).unwrap();
        fs::remove_file(&waf_env_params.binding_gen_file).unwrap();
    }

    // Write the binding to the files.
    bindings.write_to_file(waf_env_params.binding_gen_file.to_str().unwrap())
        .expect("couldn't write bindings");

    // Set the file as read-only
    set_readonly(&waf_env_params.binding_gen_file, true).unwrap();

    // If the wrapper file is generated, then, compile it into a stlib.
    if waf_env_params.static_wrapper_path.is_file() {
        let mut cc = cc::Build::new();

        cc.file(waf_env_params.static_wrapper_path.to_str().unwrap());

        cc.no_default_flags(true);

        for define in &waf_env_params.defines {
            cc.define(define, None);
        }

        for include in &waf_env_params.includes {
            cc.include(include);
        }

        for flag in &waf_env_params.cflags {
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
}
