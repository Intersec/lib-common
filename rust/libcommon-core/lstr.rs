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

pub use crate::bindings::lstr_t;

impl From<&[u8]> for lstr_t {
    fn from(buf: &[u8]) -> Self {
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 {
                s: buf.as_ptr() as *const i8,
            },
            len: buf.len() as i32,
            mem_pool: 0,
        }
    }
}
