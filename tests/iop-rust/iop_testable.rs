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

//! The bodies of the tests that `build/native_tests.rs` generates for each
//! IOP type.
//!
//! A test does nothing for a type whose default-constructed object cannot
//! pack or does not satisfy its constraints. `iop_testable.h` decides it
//! from the IOP descriptor.

use crate::bindings::{iop_testable_is_private, iop_testable_is_testable};
use libcommon::iop::{CStruct, Class, EnvCtx, StructUnion as _};
use libcommon::mem_stack::TScope;
use std::ptr;

/// The packing formats of a round-trip test.
#[derive(Clone, Copy)]
pub enum Pack {
    Bin,
    Json,
    Yaml,
}

fn is_testable<T: CStruct>() -> bool {
    // SAFETY: `CDESC` is a static IOP descriptor.
    unsafe { iop_testable_is_testable(T::CDESC) }
}

/// Asserts that a default-constructed `T` packs and unpacks to the same
/// bytes.
///
/// # Panics
///
/// The round-trip fails or gives other bytes.
pub fn roundtrip<T: CStruct>(env: &EnvCtx, pack: Pack) {
    // SAFETY: `CDESC` is a static IOP descriptor.
    if !is_testable::<T>()
        || (matches!(pack, Pack::Yaml) && unsafe { iop_testable_is_private(T::CDESC) })
    {
        return;
    }

    let t_scope = TScope::new_scope();
    let obj = T::new();

    match pack {
        Pack::Bin => {
            let packed = obj.as_bin();
            let unpacked = env
                .t_bunpack_desc(&t_scope, &packed, T::CDESC, 0)
                .unwrap_or_else(|e| panic!("cannot unpack: {e}"));

            assert_eq!(packed, unpacked.as_bin());
        }
        Pack::Json => {
            let packed = obj.as_json();
            let unpacked = env
                .t_junpack_desc(&t_scope, &packed, T::CDESC, 0)
                .unwrap_or_else(|e| panic!("cannot unpack: {e}"));

            assert_eq!(packed, unpacked.as_json());
        }
        Pack::Yaml => {
            let packed = obj.as_yaml();
            let unpacked = env
                .t_yunpack_desc(&t_scope, &packed, T::CDESC, 0)
                .unwrap_or_else(|e| panic!("cannot unpack: {e}"));

            assert_eq!(packed, unpacked.as_yaml());
        }
    }
}

/// Asserts that a default-constructed `T` satisfies its constraints.
///
/// # Panics
///
/// A constraint fails.
pub fn check_constraints<T: CStruct>() {
    if is_testable::<T>() {
        T::new()
            .check_constraints()
            .expect("constraints should be satisfied");
    }
}

/// Asserts that a class and its parent part share the same address, and
/// that a downcast of the parent part gives the class back.
///
/// # Panics
///
/// One of the two fails.
pub fn cast<C>()
where
    C: CStruct + Class,
    C::Parent: Class,
{
    let child = C::new();
    let parent = child.upcast();

    assert!(ptr::eq(
        ptr::from_ref(parent),
        ptr::from_ref(&child).cast::<C::Parent>()
    ));
    assert!(ptr::eq(
        parent.downcast::<C>().expect("downcast should succeed"),
        &raw const child,
    ));
}
