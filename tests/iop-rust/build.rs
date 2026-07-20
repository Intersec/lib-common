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
use waf_cargo_build::WafBuild;

mod native_iop_bindings;

/// IOP packages this crate generates itself via `iopc --language rust` and
/// splices into its own `bindings.rs` (see
/// [`WafBuild::splice_native_iop_bindings`]).
const NATIVE_IOP_PKG: &[&str] = &[
    "core",
    // "ic",
    "yaml",
    // "tstiop",
    "tstiop_void_type",
    "tstiop_inheritance",
    "tstiop_backward_compat_deleted_struct_1",
];

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::read_build_env()?;

    waf_build.print_cargo_instructions();

    println!("cargo::rustc-check-cfg=cfg(iop_rust_gen_tests)");
    if native_iop_bindings::copy_iop_tests_to_out_dir()? {
        println!("cargo::rustc-cfg=iop_rust_gen_tests");
    }

    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let iop_rust_dir = Path::new(&manifest_dir).join("../../src/iop-rust");

    waf_build.generate_bindings(|builder| {
        let mut b = builder.header("wrapper.h");
        for pkg in NATIVE_IOP_PKG {
            b = b.blocklist_file(format!(r".*/{pkg}(-t|-tdef)?\.iop\.h$"));
        }
        Ok(b)
    })?;

    waf_build.splice_native_iop_bindings(NATIVE_IOP_PKG, &iop_rust_dir)?;

    Ok(())
}
