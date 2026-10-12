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

use std::error;
use std::fs;

use waf_cargo_build::WafBuild;

#[path = "build/layout_tests.rs"]
mod layout_tests;
#[path = "build/native_tests.rs"]
mod native_tests;

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::build()?;
    let iop_files = waf_build
        .native_iop_pkgs
        .iter()
        .map(|pkg| Ok((pkg, syn::parse_file(&fs::read_to_string(&pkg.path)?)?)))
        .collect::<Result<Vec<_>, Box<dyn error::Error>>>()?;

    layout_tests::generate(&waf_build, &iop_files)?;
    native_tests::generate(&waf_build, &iop_files)
}
