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

use crate::bindings::*;

pub use crate::bindings::pstream_t;

impl From<&[u8]> for pstream_t {
    fn from(buf: &[u8]) -> Self {
        let start_ptr: *const u8 = buf.as_ptr();
        let end_ptr: *const u8 = buf.last().unwrap();
        unsafe {
            Self {
                __bindgen_anon_1: pstream_t__bindgen_ty_1 { b: start_ptr },
                __bindgen_anon_2: pstream_t__bindgen_ty_2 {
                    b_end: end_ptr.offset(1),
                },
            }
        }
    }
}
