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
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};

use waf_cargo_build::WafBuild;

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::build()?;

    let iop_rs_files: Vec<PathBuf> = waf_build
        .native_iop_pkgs
        .into_iter()
        .map(|pkg| pkg.path)
        .collect();

    wire_iopc_tests(&iop_rs_files)
}

/// The `<pkg>.iop.rs` of `pkg` among the ones this crate binds.
fn iop_rs_file<'a>(iop_rs_files: &'a [PathBuf], pkg: &str) -> Option<&'a Path> {
    iop_rs_files.iter().map(PathBuf::as_path).find(|path| {
        path.file_name()
            .is_some_and(|name| name == format!("{pkg}.iop.rs").as_str())
    })
}

/// Wires in the `iopc`-generated tests of the packages `native_iop_tests.rs`
/// lists.
fn wire_iopc_tests(iop_rs_files: &[PathBuf]) -> Result<(), Box<dyn error::Error>> {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);
    let tests_path = manifest_dir.join("native_iop_tests.rs");

    println!("cargo::rerun-if-changed={}", tests_path.display());

    let tested_pkgs = parse_native_iop_test_pkgs(&tests_path)?;

    for pkg in &tested_pkgs {
        if iop_rs_file(iop_rs_files, pkg).is_none() {
            return Err(format!(
                "IOP package `{pkg}` is under test in `{}` but this crate \
                 does not bind it: add the IOP library defining it to the \
                 `use` of its `waf` target",
                tests_path.display(),
            )
            .into());
        }
    }

    write_native_iop_pkgs_rs(iop_rs_files)?;

    println!("cargo::rustc-check-cfg=cfg(iop_rust_gen_tests)");
    if copy_iop_tests_to_out_dir(&tested_pkgs, iop_rs_files)? {
        println!("cargo::rustc-cfg=iop_rust_gen_tests");
    }

    Ok(())
}

/// Extracts the flat package list out of `native_iop_tests.rs`'s
/// `native_iop_test!([...])` invocation, so that list stays the single
/// place a developer needs to edit to wire a package's tests in (see the
/// macro's doc comment in that file).
fn parse_native_iop_test_pkgs(tests_path: &Path) -> Result<Vec<String>, Box<dyn error::Error>> {
    let content = fs::read_to_string(tests_path)
        .map_err(|e| format!("cannot read `{}`: {e}", tests_path.display()))?;

    let invalid = || {
        format!(
            "no valid `native_iop_test!([...])` invocation found in `{}`",
            tests_path.display()
        )
    };

    let marker = "native_iop_test!([";
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

/// Writes `<OUT_DIR>/native_iop_pkgs.rs`, the `include!` of every native
/// IOP package the crate binds, for `native_iop_tests.rs` to pull them all
/// into a single module.
///
/// The dependencies of a tested package are in there without having to be
/// named: `waf` hands over the IOP files of the whole `use` closure.
fn write_native_iop_pkgs_rs(iop_rs_files: &[PathBuf]) -> Result<(), Box<dyn error::Error>> {
    let out_dir = env::var("OUT_DIR")?;
    let mut content = String::new();

    for path in iop_rs_files {
        writeln!(content, "include!(\"{}\");", path.display())?;
    }

    fs::write(Path::new(&out_dir).join("native_iop_pkgs.rs"), content)?;

    Ok(())
}

/// Copies the `<pkg>.iop.tests.rs` test file of every tested package into
/// `OUT_DIR`, so `native_iop_tests.rs` can `include!` them.
///
/// Returns whether they were all found: `iopc` only writes these when
/// `IOPC_RUST_GEN_TESTS` was set for the `waf` build that generated them,
/// so one missing means that env var wasn't set back then, and none of
/// them is wired in.
fn copy_iop_tests_to_out_dir(
    tested_pkgs: &[String],
    iop_rs_files: &[PathBuf],
) -> Result<bool, Box<dyn error::Error>> {
    let out_dir = env::var("OUT_DIR")?;
    let suffix = ".iop.tests.rs";
    let mut tests = Vec::new();

    for pkg in tested_pkgs {
        let Some(iop_rs_path) = iop_rs_file(iop_rs_files, pkg) else {
            return Ok(false);
        };
        let src = iop_rs_path.with_file_name(format!("{pkg}{suffix}"));
        let Ok(content) = fs::read_to_string(&src) else {
            return Ok(false);
        };

        println!("cargo::rerun-if-changed={}", src.display());
        tests.push((Path::new(&out_dir).join(format!("{pkg}{suffix}")), content));
    }

    if tests.is_empty() {
        return Ok(false);
    }

    for (dst, content) in tests {
        fs::write(dst, content)?;
    }

    Ok(true)
}
