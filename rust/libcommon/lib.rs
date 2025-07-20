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

pub mod iop;

pub mod bindings {
    #![allow(
        warnings,
        deprecated_safe,
        future_incompatible,
        keyword_idents,
        let_underscore,
        nonstandard_style,
        refining_impl_trai,
        rust_2018_compatibility,
        rust_2018_idioms,
        rust_2021_compatibility,
        rust_2024_compatibility,
        unused,
        clippy::all,
        clippy::pedantic,
        clippy::restriction
    )]
    use crate::iop;
    pub use libcommon_core::bindings::*;
    include!(concat!(env!("PKG_WAF_BUILD_DIR"), "/bindings.rs"));
}

// Reexport `libcommon_core` types.
pub use libcommon_core::{farch, lstr, mem_stack, pstream, sb, thr};
