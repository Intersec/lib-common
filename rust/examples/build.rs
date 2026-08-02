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

use waf_cargo_build::{WafBuild, hide_native_iop_pkgs};

/// IOP packages this crate splices in from `iopc --language rust`'s
/// output, on top of the ones `libcommon` already owns.
const NATIVE_IOP_PKG: &[&str] = &["exiop"];

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::read_build_env()?;

    waf_build.print_cargo_instructions();

    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let iop_rust_dir = Path::new(&manifest_dir).join("../../src/iop-rust");

    waf_build.generate_bindings(|builder| {
        let mut builder = hide_native_iop_pkgs(builder.header("wrapper.h"));

        for pkg in NATIVE_IOP_PKG {
            builder = builder.blocklist_file(format!(r".*/{pkg}(-t|-tdef)?\.iop\.h$"));
        }
        Ok(builder)
    })?;

    waf_build.splice_native_iop_bindings(NATIVE_IOP_PKG, &iop_rust_dir)?;

    Ok(())
}
