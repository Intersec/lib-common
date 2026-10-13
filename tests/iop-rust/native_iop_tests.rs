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

/// Wires in the tests that `iopc` writes for each listed package, when
/// `IOPC_RUST_GEN_TESTS` was set for the `waf` build that generated them.
///
/// The native bindings of every package that the crate binds go in one
/// module, and each listed package gets the alias `<pkg>_iop_module` of
/// it, which the generated tests use.
///
/// `build.rs` reads the list with a text scan: keep it a plain list of
/// package names.
macro_rules! native_iop_test {
    ([$($pkg:ident),* $(,)?]) => {
        // The generated code names the items of libcommon by their full
        // path.
        #[allow(clippy::absolute_paths)]
        pub mod native_iop_pkgs {
            pub use crate::modules::*;
            include!(concat!(env!("OUT_DIR"), "/native_iop_pkgs.rs"));
        }

        paste::paste! {
            $(
                pub use self::native_iop_pkgs as [<$pkg _iop_module>];
            )*
        }

        #[cfg(iop_rust_gen_tests)]
        pub mod native_iop_test {
            $(
                include!(concat!(
                    env!("OUT_DIR"), "/", stringify!($pkg), ".iop.tests.rs"
                ));
            )*
        }
    };
}

native_iop_test!([
    core,
    ic,
    yaml,
    tstiop_void_type,
    tstiop,
    tstiop_inheritance
]);
