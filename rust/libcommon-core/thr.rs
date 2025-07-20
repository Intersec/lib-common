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

//! Module to export the thread functions used by `libcommon` in a safe way
//!
//! Using `attach()` and `detach()` are required to use `TScope`.

use crate::bindings;

/// Attach the thread to `libcommon` and call the attach callbacks.
pub fn attach() {
    unsafe {
        bindings::thr_attach();
    };
}

/// Detach the thread from `libcommon` and call the detach callbacks.
pub fn detach() {
    unsafe {
        bindings::thr_detach();
    };
}
