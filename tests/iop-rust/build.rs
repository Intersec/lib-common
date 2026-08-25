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

mod native_iop_bindings;

/// Extracts the flat package list out of `layout_tests.rs`'s
/// `condensed_tests!([...])` invocation, so that list stays the single
/// place a developer needs to edit to wire a package's tests in (see the
/// macro's doc comment in that file).
fn parse_condensed_tests_pkgs(
    layout_tests_path: &Path,
) -> Result<Vec<String>, Box<dyn error::Error>> {
    let content = fs::read_to_string(layout_tests_path)
        .map_err(|e| format!("cannot read `{}`: {e}", layout_tests_path.display()))?;

    let invalid = || {
        format!(
            "no valid `condensed_tests!([...])` invocation found in `{}`",
            layout_tests_path.display()
        )
    };

    let marker = "condensed_tests!([";
    let after_marker = content
        .find(marker)
        .and_then(|i| content.get(i + marker.len()..))
        .ok_or_else(invalid)?;
    let list = after_marker
        .find(']')
        .and_then(|i| after_marker.get(..i))
        .ok_or_else(invalid)?;

    Ok(list
        .split(',')
        .map(str::trim)
        .filter(|pkg| !pkg.is_empty())
        .map(str::to_owned)
        .collect())
}

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::read_build_env()?;

    waf_build.print_cargo_instructions();

    println!("cargo::rustc-check-cfg=cfg(iop_rust_gen_tests)");
    if native_iop_bindings::copy_iop_tests_to_out_dir()? {
        println!("cargo::rustc-cfg=iop_rust_gen_tests");
    }

    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let iop_rust_dir = Path::new(&manifest_dir).join("../../src/iop-rust");
    let layout_tests_path = Path::new(&manifest_dir).join("layout_tests.rs");

    println!("cargo::rerun-if-changed={}", layout_tests_path.display());

    let native_iop_pkg = parse_condensed_tests_pkgs(&layout_tests_path)?;
    let native_iop_pkg: Vec<&str> = native_iop_pkg.iter().map(String::as_str).collect();

    waf_build.generate_bindings(|builder| {
        let mut b = builder.header("wrapper.h");
        for pkg in &native_iop_pkg {
            b = b.blocklist_file(format!(r".*/{pkg}(-t|-tdef)?\.iop\.h$"));
        }
        Ok(b)
    })?;

    waf_build.splice_native_iop_bindings(&native_iop_pkg, &iop_rust_dir)?;

    Ok(())
}
