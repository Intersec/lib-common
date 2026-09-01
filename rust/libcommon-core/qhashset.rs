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

//! [`QHash`], the Rust wrapper around a C `qh_t` hash set.
//!
//! It behaves like a [`HashSet`](std::collections::HashSet). The allocator, the entry ownership
//! and the C interfacing rules are shared with [`QMap`](crate::qhashmap::QMap): the
//! [`qhash`](crate::qhash) module documentation describes them.

use std::fmt;
use std::marker::PhantomData;
use std::mem::{ManuallyDrop, MaybeUninit};
use std::ptr;

use crate::bindings::{
    QHASH_COLLISION, mem_pool_t, qhash_clear, qhash_del_at, qhash_memory_footprint, qhash_scan,
    qhash_set_minsize, qhash_t, qhash_unseal, qhash_wipe, t_pool,
};
use crate::mem_stack::TScope;
use crate::qhash::{Keys, NoWipe, QEntryWipe, QHashType, SCAN_END, first_pos, qhash_common_impl};

// {{{ QHash

/// Wrapper around a C `qh_t` for safe manipulation in Rust.
///
/// See the [`qhash`](crate::qhash) module documentation for the allocator and entry ownership rules.
#[repr(transparent)]
pub struct QHash<'a, Q: QHashType<Value = ()>, W: QEntryWipe<Q> = NoWipe> {
    qh: Q,

    // Ties the table to its allocator scope, and names the destructor of its entries.
    _marker: PhantomData<(&'a (), W)>,
}

qhash_common_impl!(QHash, QHashType<Value = ()>);

// }}}
// {{{ Set operations

impl<Q, W> QHash<'_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// Add a key to the set.
    ///
    /// Return whether the key was added. When the key is already there, the set keeps the stored
    /// key and releases the given one, like [`HashSet::insert`](std::collections::HashSet::insert)
    /// drops it. Use [`Self::replace`] to store the given key instead.
    ///
    /// This is the C `qh_add()`.
    pub fn insert(&mut self, mut key: Q::Key) -> bool {
        let pos = unsafe { Q::reserve(self.as_mut_ptr(), &key, 0) };

        if pos & QHASH_COLLISION != 0 {
            W::wipe_key(&mut key);
            return false;
        }
        true
    }

    /// Add a key to the set, replacing an existing one.
    ///
    /// Return the replaced key, like [`HashSet::replace`](std::collections::HashSet::replace): the
    /// caller takes over what it owns. Replacing matters when two equal keys are not
    /// interchangeable, for instance two `lstr_t` that point to different buffers.
    ///
    /// This is the C `qh_replace()`, except that the previous key is returned rather than
    /// overwritten in place.
    pub fn replace(&mut self, key: Q::Key) -> Option<Q::Key> {
        let pos = unsafe { Q::reserve(self.as_mut_ptr(), &key, 0) };

        if pos & QHASH_COLLISION == 0 {
            return None;
        }

        let slot = unsafe { self.keys_raw().add((pos & !QHASH_COLLISION) as usize) };
        let previous = unsafe { slot.read() };

        unsafe {
            slot.write(key);
        }
        Some(previous)
    }

    /// Remove a key from the set.
    ///
    /// What the stored key owns is released. Use [`Self::take`] to get it instead.
    ///
    /// Return whether the key was there.
    pub fn remove(&mut self, key: &Q::Key) -> bool {
        let pos = unsafe { Q::find(self.as_mut_ptr(), key) };

        if pos < 0 {
            return false;
        }
        self.wipe_key_at(pos as u32);
        self.del_at(pos as u32);
        true
    }

    /// Remove a key from the set and return it.
    ///
    /// The returned key is the one the set stored, which can differ from `key` even though the two
    /// are equal. What it owns is not released: the caller takes it over.
    pub fn take(&mut self, key: &Q::Key) -> Option<Q::Key> {
        let pos = unsafe { Q::find(self.as_mut_ptr(), key) };

        if pos < 0 {
            return None;
        }

        let found = unsafe { self.keys_raw().add(pos as usize).read() };

        self.del_at(pos as u32);
        Some(found)
    }

    /// Iterate over the keys of the set, in an unspecified order.
    ///
    /// This is the same as [`Self::keys`].
    pub fn iter(&self) -> Keys<'_, Q> {
        self.keys()
    }

    /// Release what every entry owns.
    ///
    /// A set has no value, so only the keys are released.
    #[inline]
    fn wipe_entries(&mut self) {
        if !W::WIPES {
            return;
        }

        let keys = self.keys_raw();
        let mut pos = first_pos(self.as_qhash());

        while pos != SCAN_END {
            unsafe {
                W::wipe_key(&mut *keys.add(pos as usize));
            }
            pos = unsafe { qhash_scan(self.as_qhash(), pos + 1) };
        }
    }
}

impl<'t, Q, W> IntoIterator for &'t QHash<'_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    type Item = &'t Q::Key;
    type IntoIter = Keys<'t, Q>;

    fn into_iter(self) -> Self::IntoIter {
        self.keys()
    }
}

impl<Q, W> fmt::Debug for QHash<'_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
    Q::Key: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_set().entries(self.keys()).finish()
    }
}

// }}}
