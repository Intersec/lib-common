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

const FUNCTIONS_TO_EXPOSE: &[&str] =
    &["lstr_obfuscate"];

const VARS_TO_EXPOSE: &[&str] = &[];

fn main() {
    let waf_env_params = waf_cargo_bind::decode_waf_env_params();

    let mut builder = waf_cargo_bind::make_builder(&waf_env_params)
        .header("wrapper.h");

    for fun in FUNCTIONS_TO_EXPOSE.iter() {
        builder = builder.allowlist_function(fun);
    }

    for var in VARS_TO_EXPOSE.iter() {
        builder = builder.allowlist_var(var);
    }

    waf_cargo_bind::generate_bindings(builder, &waf_env_params);
}
