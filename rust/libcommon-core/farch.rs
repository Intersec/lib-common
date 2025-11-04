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
use std::mem::MaybeUninit;
use std::os::raw::{c_char, c_int, c_void};
use std::ptr;
use std::ptr::from_ref;
use std::sync::Mutex;

use crate::bindings::{
    log_get_module, lstr_obfuscate, module_implement, module_register, module_t, pstream_t,
    qlzo1x_decompress_safe,
};
use crate::helpers::slice_assume_init_mut;
use crate::lstr::{self, AsRaw, BorrowedLstr, lstr_t};
use crate::mem_stack::TScope;

#[allow(clippy::module_name_repetitions)]
pub use crate::bindings::farch_entry_t;

// {{{ Globals

static mut FARCH_MODULE: *mut module_t = ptr::null_mut();
static FARCH_PERSISTED: Mutex<Option<HashMap<usize, Box<[u8]>>>> = Mutex::new(None);

// }}}
// {{{ Helpers

fn lstr_unobfuscate(in_: &impl AsRaw, key: u64, out: &impl AsRaw) {
    unsafe { lstr_obfuscate(in_.as_raw(), key, out.as_raw()) }
}

// }}}
// {{{ Private functions

/// Unobfuscate and get the filename of an entry in a buffer.
fn t_farch_get_filename<'t>(t_scope: &'t TScope, entry: &farch_entry_t) -> BorrowedLstr<'t> {
    let name_buf_uninit: &'t mut [MaybeUninit<u8>] = t_scope.t_new_slice_uninit(entry.name.len());
    let name_buf_ptr = name_buf_uninit.as_ptr();
    let name_lstr =
        lstr::from_borrowed_ptr_and_len::<'t>(name_buf_ptr.cast::<c_char>(), entry.name.len());

    lstr_unobfuscate(&entry.name, entry.nb_chunks as u64, &name_lstr);

    name_lstr
}

/// Aggregate and unobfuscate all the chuncks of an entry.
fn t_farch_aggregate<'t>(t_scope: &'t TScope, entry: &farch_entry_t) -> Option<&'t [u8]> {
    let entry_compressed_size = entry.compressed_size as usize;
    // +1 for the null character at the end of the buffer.
    let entry_compressed_size_1 = entry_compressed_size + 1;
    let contents_uninit_1: &'t mut [MaybeUninit<u8>] =
        t_scope.t_new_slice_uninit(entry_compressed_size_1);
    let contents_ptr_uninit_1: *mut MaybeUninit<u8> = contents_uninit_1.as_mut_ptr();
    let contents_ptr_1: *mut u8 = contents_ptr_uninit_1.cast();

    let mut compressed_size: usize = 0;

    for i in 0..(entry.nb_chunks as usize) {
        let chunk = unsafe { *entry.chunks.add(i) };
        let chunk_len = chunk.len();
        let content_chunk_ptr: *mut u8 = unsafe { contents_ptr_1.add(compressed_size) };
        let content_chunk_lstr =
            lstr::from_ptr_and_len(content_chunk_ptr.cast::<c_char>(), chunk_len);

        compressed_size += chunk_len;
        if compressed_size > entry_compressed_size {
            debug_assert!(false); // TODO: use expect().
            return None;
        }

        lstr_unobfuscate(&chunk, chunk.len as u64, &content_chunk_lstr);
    }

    if compressed_size != entry_compressed_size {
        debug_assert!(false); // TODO: use expect().
        return None;
    }

    // Set the null character at the end of the buffer.
    unsafe {
        contents_ptr_1.add(entry_compressed_size).write(b'\0');
    }

    let contents_1 = unsafe { slice_assume_init_mut(contents_uninit_1) };

    let contents = &mut contents_1[..entry_compressed_size];

    Some(contents)
}

/// Unarchive with potential decompress the entry as an optional vec buffer.
fn t_farch_unarchive_opt<'t>(t_scope: &'t TScope, entry: &farch_entry_t) -> Option<&'t [u8]> {
    let contents = t_farch_aggregate(t_scope, entry)?;

    if entry.compressed_size == entry.size {
        // Uncompressed entry.
        return Some(contents);
    }

    let contents_ps = pstream_t::from_bytes(contents);

    let entry_size = entry.size as usize;
    let entry_size_1 = entry_size + 1; // `+ 1` to add the null character at the end.
    //
    let res_uninit_1: &'t mut [MaybeUninit<u8>] = t_scope.t_new_slice_uninit(entry_size_1);
    let res_ptr_uninit_1: *mut MaybeUninit<u8> = res_uninit_1.as_mut_ptr();
    let res_ptr_1: *mut u8 = res_ptr_uninit_1.cast();
    let res_ptr_void: *mut c_void = res_ptr_1.cast();

    // Uncompress the contents in the res buffer.
    let res_len = unsafe { qlzo1x_decompress_safe(res_ptr_void, entry_size, contents_ps) };

    if res_len != (entry_size as isize) {
        return None;
    }

    // Set the null character at the end of the buffer.
    unsafe {
        res_ptr_1.add(entry_size).write(b'\0');
    }

    let res_1 = unsafe { slice_assume_init_mut(res_uninit_1) };

    let res = &res_1[..entry_size];

    Some(res)
}

/// Unarchive with potential decompress the entry as slice buffer and panic if unable to decompress.
///
/// # Panic
///
/// If not being able to decompress the archive.
fn t_farch_unarchive_buf<'t>(t_scope: &'t TScope, entry: &farch_entry_t) -> &'t [u8] {
    let res_opt = t_farch_unarchive_opt(t_scope, entry);

    res_opt.unwrap_or_else(|| {
        let name_lstr = t_farch_get_filename(t_scope, entry);
        panic!("cannot uncompress farch entry `{name_lstr}`");
    })
}

/// Unarchive with potential decompress the entry as `lstr_t` and panic if unable to decompress.
///
/// # Panic
///
/// If not being able to decompress the archive.
fn t_farch_unarchive_lstr<'t>(t_scope: &'t TScope, entry: &farch_entry_t) -> BorrowedLstr<'t> {
    lstr::from_bytes(t_farch_unarchive_buf(t_scope, entry))
}

/// Get the entry corresponding to the filename
unsafe fn farch_get_entry(
    files: *const farch_entry_t,
    name: *const c_char,
) -> Option<&'static farch_entry_t> {
    if name.is_null() {
        return unsafe { files.as_ref() };
    }

    let name_lstr = unsafe { lstr::from_ptr(name) };
    let mut files = files;

    loop {
        let current_entry = unsafe { files.as_ref().expect("files should be a valid pointer") };
        if current_entry.name.len == 0 {
            break;
        }

        let t_scope = TScope::new_scope();
        let entry_name_lstr = t_farch_get_filename(&t_scope, current_entry);
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
    let entry_addr = from_ref::<farch_entry_t>(entry).addr();

    let mut map_opt = FARCH_PERSISTED.lock().expect("unable to lock mutex");
    let map_ref = map_opt.as_mut().expect("farch module not initialized");

    let persisted_buf_1 = map_ref.entry(entry_addr).or_insert_with(|| {
        let t_scope = TScope::new_scope();
        let t_buf = t_farch_unarchive_buf(&t_scope, entry);

        // `+ 1` to add the null character at the end.
        let mut buf_uninit = Box::<[u8]>::new_uninit_slice(t_buf.len() + 1);

        let (buf_uninit_entry, buf_uninit_null_char) = buf_uninit.split_at_mut(t_buf.len());
        let buf_uninit_entry_ptr: *mut MaybeUninit<u8> = buf_uninit_entry.as_mut_ptr();
        let buf_entry_ptr: *mut u8 = buf_uninit_entry_ptr.cast();

        // Copy t_buf to the buffer.
        unsafe {
            buf_entry_ptr.copy_from_nonoverlapping(t_buf.as_ptr(), t_buf.len());
        }

        // Add the null character at the end.
        buf_uninit_null_char[0].write(b'\0');

        // Return the buffer.
        unsafe { buf_uninit.assume_init() }
    });

    // The buffer holds the null character in its buffer
    let persisted_buf = &persisted_buf_1[..persisted_buf_1.len() - 1];

    unsafe { lstr::from_bytes(persisted_buf).as_raw() }
}

// }}}
// {{{ Public C functions

#[allow(
    clippy::missing_safety_doc,
    clippy::missing_panics_doc,
    clippy::module_name_repetitions
)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn farch_get_filename(
    entry: *const farch_entry_t,
    name_outbuf: *mut c_char,
) -> *mut c_char {
    // Use a reference so it is easier to manipulate
    let entry: &farch_entry_t = unsafe { entry.as_ref().expect("entry should be a valid pointer") };

    if entry.name.is_null() {
        // If the entry name is null, return null.
        return ptr::null_mut();
    }

    let out = lstr::from_ptr_and_len(name_outbuf, entry.name.len());

    // Deobfuscate the name.
    lstr_unobfuscate(&entry.name, entry.nb_chunks as u64, &out);

    // Ensure that the name ends with a null character.
    unsafe {
        *name_outbuf.add(entry.name.len()) = '\0' as c_char;
    };

    name_outbuf
}

#[allow(
    clippy::missing_safety_doc,
    clippy::missing_panics_doc,
    clippy::module_name_repetitions
)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn t_farch_unarchive(entry: *const farch_entry_t) -> lstr_t {
    let entry: &farch_entry_t = unsafe { entry.as_ref().expect("entry should be a valid pointer") };

    let t_scope = TScope::from_parent();
    let res_lstr = t_farch_unarchive_lstr(&t_scope, entry);
    unsafe { res_lstr.as_raw() }
}

#[allow(
    clippy::missing_safety_doc,
    clippy::missing_panics_doc,
    clippy::module_name_repetitions
)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn farch_unarchive_persist(entry: *const farch_entry_t) -> lstr_t {
    let entry: &farch_entry_t = unsafe { entry.as_ref().expect("entry should be a valid pointer") };

    farch_unarchive_persist_ref(entry)
}

#[allow(
    clippy::missing_safety_doc,
    clippy::missing_panics_doc,
    clippy::module_name_repetitions
)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn t_farch_get_data(
    files: *const farch_entry_t,
    name: *const c_char,
) -> lstr_t {
    let entry = unsafe { farch_get_entry(files, name) };

    let Some(entry) = entry else {
        return lstr::null();
    };

    let t_scope = TScope::from_parent();
    let res_lstr = t_farch_unarchive_lstr(&t_scope, entry);
    unsafe { res_lstr.as_raw() }
}

#[allow(
    clippy::missing_safety_doc,
    clippy::missing_panics_doc,
    clippy::module_name_repetitions
)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn farch_get_data_persist(
    files: *const farch_entry_t,
    name: *const c_char,
) -> lstr_t {
    let entry = unsafe { farch_get_entry(files, name) };

    let Some(entry) = entry else {
        return lstr::null();
    };

    farch_unarchive_persist_ref(entry)
}

// }}}
// {{{ Module

// TODO: Find a way to generalize how to create module in Rust.

extern "C" fn farch_initialize(_arg: *mut c_void) -> c_int {
    let mut map_opt = FARCH_PERSISTED.lock().expect("unable to lock mutex");
    _ = map_opt.insert(HashMap::new());
    0
}

extern "C" fn farch_shutdown() -> c_int {
    let mut map_opt = FARCH_PERSISTED.lock().expect("unable to lock mutex");
    map_opt.take();
    0
}

#[allow(clippy::module_name_repetitions)]
#[unsafe(no_mangle)]
pub extern "C" fn farch_get_module() -> *mut module_t {
    unsafe {
        if FARCH_MODULE.is_null() {
            FARCH_MODULE = module_register(lstr::raw("farch"));

            module_implement(
                FARCH_MODULE,
                Some(farch_initialize),
                Some(farch_shutdown),
                log_get_module(), // Taken from MODULE_BEGIN()
            );
        }
        FARCH_MODULE
    }
}

// }}}
