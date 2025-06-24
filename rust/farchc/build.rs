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

use std::error;
use waf_cargo_build::WafBuild;

const ITEMS_TO_EXPOSE: &[&str] = &["qlzo1x_compress", "lzo_cbuf_size"];

fn main() -> Result<(), Box<dyn error::Error>> {
    let waf_build = WafBuild::read_build_env()?;

    waf_build.print_cargo_instructions();
    waf_build.generate_bindings(|builder| {
        let mut builder = builder;

        builder = builder.header("wrapper.h");

        for item in ITEMS_TO_EXPOSE.iter() {
            builder = builder.allowlist_item(item);
        }

        Ok(builder)
    })?;

    Ok(())
}
