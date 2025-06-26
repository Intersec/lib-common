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

//! Unarchive embedded content in C files.

use std::collections::HashMap;
use std::os::raw::{c_char, c_int, c_void};
use std::ptr;
use std::sync::Mutex;

use ctor::ctor;

use crate::bindings::*;
use crate::lstr::lstr_t;
use crate::mem_stack::TScope;

pub use crate::bindings::farch_entry_t;

// {{{ Globals

static mut FARCH_MODULE: *mut module_t = ptr::null_mut();
static FARCH_PERSISTED: Mutex<Option<HashMap<usize, Vec<u8>>>> = Mutex::new(None);

// }}}
// {{{ Helpers

fn lstr_unobfuscate(in_: lstr_t, key: u64, out: lstr_t) {
    unsafe { lstr_obfuscate(in_, key, out) }
}

// }}}
// {{{ Private functions

/// Unobfuscate and get the filename of an entry as a buffer and lstr_t.
fn farch_get_filename_tuple(entry: &farch_entry_t) -> (Vec<u8>, lstr_t) {
    let name_buf = vec![0u8; entry.name.len()];
    let name_lstr = lstr_t::from_bytes(&name_buf);

    lstr_unobfuscate(entry.name, entry.nb_chunks as u64, name_lstr);

    (name_buf, name_lstr)
}

/// Aggregate and unobfuscate all the chuncks of an entry.
fn farch_aggregate(entry: &farch_entry_t) -> Option<Vec<u8>> {
    let entry_compressed_size = entry.compressed_size as usize;
    let contents = vec![0u8; entry_compressed_size];
    let mut compressed_size: usize = 0;

    for i in 0..(entry.nb_chunks as usize) {
        let chunk = unsafe { *entry.chunks.add(i) };
        let chunk_len = chunk.len();
        let content_chunk = &contents[compressed_size..(compressed_size + chunk_len)];
        let content_chunk_lstr: lstr_t = content_chunk.into();

        compressed_size += chunk_len;
        if compressed_size > entry_compressed_size {
            debug_assert!(false); // TODO: use expect().
            return None;
        }

        lstr_unobfuscate(chunk, chunk.len as u64, content_chunk_lstr);
    }

    if compressed_size != entry_compressed_size {
        debug_assert!(false); // TODO: use expect().
        return None;
    }

    Some(contents)
}

/// Unarchive with potential decompress the entry as an optional vec buffer.
fn farch_unarchive_opt(entry: &farch_entry_t) -> Option<Vec<u8>> {
    let contents = farch_aggregate(entry)?;

    if entry.compressed_size == entry.size {
        // Uncompressed entry.
        return Some(contents);
    }

    let contents_ps = pstream_t::from_bytes(&contents);
    let entry_size = entry.size as usize;
    let mut res = vec![0u8; entry_size];
    let res_ptr = res.as_ptr() as *mut c_void;

    // Uncompress the contents in the res buffer.
    let res_len = unsafe { qlzo1x_decompress_safe(res_ptr, entry_size, contents_ps) };

    if res_len != (entry.size as isize) {
        return None;
    }

    unsafe {
        // Force the length of the vec buffer as the res length.
        res.set_len(res_len as usize);
    };

    Some(res)
}

/// Unarchive with potential decompress the entry as lstr_t and panic if unable to decompress.
fn farch_unarchive_buf(entry: &farch_entry_t) -> Vec<u8> {
    let res_opt = farch_unarchive_opt(entry);

    res_opt.unwrap_or_else(|| {
        let (_name_buf, name_lstr) = farch_get_filename_tuple(entry);
        panic!("cannot uncompress farch entry `{}`", name_lstr);
    })
}

/// Get the entry corresponding to the filename
unsafe fn farch_get_entry(
    files: *const farch_entry_t,
    name: *const c_char,
) -> Option<&'static farch_entry_t> {
    if name.is_null() {
        return unsafe { files.as_ref() };
    }

    let name_lstr = unsafe { lstr_t::from_ptr(name) };
    let mut files = files;

    loop {
        let current_entry = unsafe { files.as_ref().unwrap() };
        if current_entry.name.len == 0 {
            break;
        }

        let (_entry_name_buf, entry_name_lstr) = farch_get_filename_tuple(current_entry);
        if name_lstr.equals(&entry_name_lstr) {
            return Some(current_entry);
        }

        unsafe {
            files = files.add(1);
        }
    }

    None
}

// Unarchive and persist the entry given as a reference.
fn farch_unarchive_persist_ref(entry: &farch_entry_t) -> lstr_t {
    let entry_addr = (entry as *const farch_entry_t).addr();

    let mut map_opt = FARCH_PERSISTED.lock().unwrap();
    let map_ref = map_opt.as_mut().unwrap();

    let persisted_buf = map_ref
        .entry(entry_addr)
        .or_insert_with(|| farch_unarchive_buf(entry));

    lstr_t::from_bytes(persisted_buf)
}

// }}}
// {{{ Public C functions

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn farch_get_filename(
    entry: *const farch_entry_t,
    name_outbuf: *mut c_char,
) -> *mut c_char {
    // Use a reference so it is easier to manipulate
    let entry: &farch_entry_t = unsafe { entry.as_ref().unwrap() };

    if entry.name.is_null() {
        // If the entry name is null, return null.
        return ptr::null_mut();
    }

    let out = lstr_t::from_ptr_and_len(name_outbuf, entry.name.len());

    // Deobfuscate the name.
    lstr_unobfuscate(entry.name, entry.nb_chunks as u64, out);

    // Ensure that the name ends with a null character.
    unsafe {
        *name_outbuf.add(entry.name.len()) = '\0' as c_char;
    };

    name_outbuf
}

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn t_farch_unarchive(entry: *const farch_entry_t) -> lstr_t {
    let entry: &farch_entry_t = unsafe { entry.as_ref().unwrap() };

    let res_buf = farch_unarchive_buf(entry);
    let res_lstr = lstr_t::from_bytes(&res_buf);
    let t_scope = TScope::from_parent();
    t_scope.t_clone(&res_lstr)
}

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn farch_unarchive_persist(entry: *const farch_entry_t) -> lstr_t {
    let entry: &farch_entry_t = unsafe { entry.as_ref().unwrap() };

    farch_unarchive_persist_ref(entry)
}

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn t_farch_get_data(
    files: *const farch_entry_t,
    name: *const c_char,
) -> lstr_t {
    let entry = unsafe { farch_get_entry(files, name) };

    if entry.is_none() {
        return lstr_t::null();
    }

    let res_buf = farch_unarchive_buf(entry.unwrap());
    let res_lstr = lstr_t::from_bytes(&res_buf);
    let t_scope = TScope::from_parent();
    t_scope.t_clone(&res_lstr)
}

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn farch_get_data_persist(
    files: *const farch_entry_t,
    name: *const c_char,
) -> lstr_t {
    let entry = unsafe { farch_get_entry(files, name) };

    if entry.is_none() {
        return lstr_t::null();
    }

    farch_unarchive_persist_ref(entry.unwrap())
}

// }}}
// {{{ Module

// TODO: Find a way to generalize how to create module in Rust.

extern "C" fn farch_initialize(_arg: *mut c_void) -> c_int {
    let mut map_opt = FARCH_PERSISTED.lock().unwrap();
    _ = map_opt.insert(HashMap::new());
    0
}

extern "C" fn farch_shutdown() -> c_int {
    let mut map_opt = FARCH_PERSISTED.lock().unwrap();
    map_opt.take();
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn farch_get_module() -> *mut module_t {
    unsafe {
        if FARCH_MODULE.is_null() {
            FARCH_MODULE = module_register("farchc".into());
        }
        FARCH_MODULE
    }
}

#[ctor]
fn farch_module_register() {
    unsafe {
        if !FARCH_MODULE.is_null() {
            return;
        }

        module_implement(
            farch_get_module(),
            Some(farch_initialize),
            Some(farch_shutdown),
            log_get_module(), // Taken from MODULE_BEGIN()
        );
    };
}

// }}}
