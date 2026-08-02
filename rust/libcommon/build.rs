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
use std::path::Path;

use waf_cargo_build::{NATIVE_IOP_PKG, WafBuild, hide_native_iop_pkgs};

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::read_build_env()?;

    waf_build.print_cargo_instructions();

    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let iop_rust_dir = Path::new(&manifest_dir).join("../../src/iop-rust");

    waf_build.generate_bindings(|builder| {
        // Export everything from wrapper.h, but let the native `*.iop.rs`
        // files own the IOP packages they describe.
        Ok(hide_native_iop_pkgs(builder.header("wrapper.h")))
    })?;

    waf_build.splice_native_iop_bindings(NATIVE_IOP_PKG, &iop_rust_dir)?;

    Ok(())
}
