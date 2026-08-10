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

use std::env;
use std::error;
use std::fs;
use std::path::Path;
use waf_cargo_build::WafBuild;

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::read_build_env()?;

    waf_build.print_cargo_instructions();

    println!("cargo::rustc-check-cfg=cfg(iop_rust_gen_tests)");
    if copy_iop_tests_to_out_dir()? {
        println!("cargo::rustc-cfg=iop_rust_gen_tests");
    }

    waf_build.generate_bindings(|builder| Ok(builder.header("wrapper.h")))?;

    Ok(())
}

/// Returns whether any file was found: `iopc` only writes these when
/// `IOPC_RUST_GEN_TESTS` was set for the `waf` build that generated them,
/// so their absence here means that env var wasn't set back then.
fn copy_iop_tests_to_out_dir() -> Result<bool, Box<dyn error::Error>> {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let iop_rust_dir = Path::new(&manifest_dir).join("../../src/iop-rust");
    let out_dir = env::var("OUT_DIR")?;

    println!("cargo:rerun-if-changed={}", iop_rust_dir.display());

    let suffix = ".iop.tests.rs";
    let mut pkgs: Vec<String> = Vec::new();

    for entry in fs::read_dir(&iop_rust_dir)? {
        let entry = entry?;
        let file_name = entry.file_name();
        let Some(name) = file_name.to_str() else {
            continue;
        };

        if let Some(pkg) = name.strip_suffix(suffix) {
            pkgs.push(pkg.to_owned());
        }
    }

    for pkg in &pkgs {
        let src = iop_rust_dir.join(format!("{pkg}{suffix}"));
        let dst = Path::new(&out_dir).join(format!("{pkg}{suffix}"));

        let content = fs::read_to_string(&src)
            .map_err(|e| format!("cannot read `{}`: {e}", src.display()))?;

        fs::write(&dst, content)?;
    }
    Ok(!pkgs.is_empty())
}
