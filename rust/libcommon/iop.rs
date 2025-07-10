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

//! Module to interact with IOPs in Rust
//!
//! WIP

use std::marker::Sized;
use std::mem::MaybeUninit;
use std::os::raw::c_void;

use crate::bindings::{iop_enum_t, iop_init_desc, iop_struct_t};

// {{{ IOP Base

pub trait Base {}

// }}}
// {{{ IOP Enum

pub trait Enum {
    /// The C description of the IOP enum.
    const CDESC: *const iop_enum_t;
}

// }}}
// {{{ IOP StructUnion

pub trait StructUnion: Sized {
    /// The C description of the IOP structure or union.
    const CDESC: *const iop_struct_t;

    /// Create an empty `IopStructUnion` with the default arguments.
    fn new() -> Self {
        let mut res = MaybeUninit::<Self>::uninit();

        unsafe {
            iop_init_desc(Self::CDESC, res.as_mut_ptr().cast::<c_void>());
        }

        unsafe { res.assume_init() }
    }
}

// }}}
// {{{ IOP Union

pub trait Union {}

// }}}
// {{{ IOP Struct

pub trait Struct {}

// }}}
