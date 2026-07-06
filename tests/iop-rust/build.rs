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
    waf_build.generate_bindings(|builder| Ok(builder.header("wrapper.h")))?;

    copy_iop_tests_to_out_dir()?;

    Ok(())
}

fn copy_iop_tests_to_out_dir() -> Result<(), Box<dyn error::Error>> {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let iop_rust_dir = Path::new(&manifest_dir).join("../../src/iop-rust");
    let out_dir = env::var("OUT_DIR")?;

    println!("cargo:rerun-if-changed={}", iop_rust_dir.display());

    for pkg in ["core", "ic", "yaml"] {
        let src = iop_rust_dir.join(format!("{pkg}.iop.tests.rs"));
        let dst = Path::new(&out_dir).join(format!("{pkg}.iop.tests.rs"));

        fs::write(&dst, fs::read_to_string(&src).unwrap_or_default())?;
    }

    Ok(())
}
