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

//! [`QMap`], the Rust wrapper around a C `qm_t` hash map.
//!
//! It behaves like a [`HashMap`](std::collections::HashMap), including its [`Entry`] API. The
//! allocator, the entry ownership and the C interfacing rules are shared with
//! [`QHash`](crate::qhashset::QHash): the [`qhash`](crate::qhash) module documentation describes
//! them.

use std::fmt;
use std::marker::PhantomData;
use std::mem::{self, ManuallyDrop, MaybeUninit};
use std::ptr;

use crate::bindings::{
    QHASH_COLLISION, mem_pool_t, qhash_clear, qhash_del_at, qhash_memory_footprint, qhash_scan,
    qhash_set_minsize, qhash_t, qhash_unseal, qhash_wipe, t_pool,
};
use crate::mem_stack::TScope;
use crate::qhash::{
    Keys, NoWipe, QEntryWipe, QMapType, SCAN_END, first_pos, next_pos, qhash_common_impl,
};

// {{{ QMap

/// Wrapper around a C `qm_t` for safe manipulation in Rust.
///
/// See the [`qhash`](crate::qhash) module documentation for the allocator and entry ownership rules.
#[repr(transparent)]
pub struct QMap<'a, Q: QMapType, W: QEntryWipe<Q> = NoWipe> {
    qh: Q,

    // Ties the table to its allocator scope, and names the destructor of its entries.
    _marker: PhantomData<(&'a (), W)>,
}

qhash_common_impl!(QMap, QMapType);

// }}}
// {{{ Map operations

impl<'a, Q, W> QMap<'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    /// Check whether the map holds a key.
    pub fn contains_key(&self, key: &Q::Key) -> bool {
        unsafe { Q::find_safe(self.as_ptr(), key) >= 0 }
    }

    /// Get the value of a key.
    pub fn get(&self, key: &Q::Key) -> Option<&Q::Value> {
        let pos = unsafe { Q::find_safe(self.as_ptr(), key) };

        if pos < 0 {
            return None;
        }
        Some(unsafe { &*self.values_raw().add(pos as usize) })
    }

    /// Get the value of a key, mutably.
    pub fn get_mut(&mut self, key: &Q::Key) -> Option<&mut Q::Value> {
        let pos = unsafe { Q::find(self.as_mut_ptr(), key) };

        if pos < 0 {
            return None;
        }
        Some(unsafe { &mut *self.values_raw().add(pos as usize) })
    }

    /// Get the stored key that is equal to the given one, and its value.
    ///
    /// The two keys can differ, for instance two equal `lstr_t` that point to different buffers.
    pub fn get_key_value(&self, key: &Q::Key) -> Option<(&Q::Key, &Q::Value)> {
        let pos = unsafe { Q::find_safe(self.as_ptr(), key) };

        if pos < 0 {
            return None;
        }

        let pos = pos as usize;

        Some(unsafe { (&*self.keys_raw().add(pos), &*self.values_raw().add(pos)) })
    }

    /// Get the values of `N` keys at once, mutably.
    ///
    /// The result holds `None` for every key that is absent.
    ///
    /// # Panics
    ///
    /// Two of the given keys are equal.
    pub fn get_disjoint_mut<const N: usize>(
        &mut self,
        keys: [&Q::Key; N],
    ) -> [Option<&mut Q::Value>; N] {
        let mut positions = [-1i32; N];

        // `find` can move the entries of a pending resize, which would invalidate the positions
        // found so far; `find_safe` moves nothing, and the mutable borrow forbids any other
        // modification while the references live.
        for (slot, key) in positions.iter_mut().zip(keys) {
            *slot = unsafe { Q::find_safe(self.as_ptr(), key) };
        }

        for (i, pos) in positions.iter().enumerate() {
            assert!(
                *pos < 0 || !positions[..i].contains(pos),
                "the keys must be disjoint"
            );
        }

        // The positions are pairwise distinct, so the references never alias.
        positions.map(|pos| {
            if pos < 0 {
                None
            } else {
                Some(unsafe { &mut *self.values_raw().add(pos as usize) })
            }
        })
    }

    /// Get the values of `N` keys at once, mutably, without the disjointness check.
    ///
    /// The result holds `None` for every key that is absent.
    ///
    /// # Safety
    ///
    /// The given keys must be pairwise distinct: the values of two equal keys would alias.
    pub unsafe fn get_disjoint_unchecked_mut<const N: usize>(
        &mut self,
        keys: [&Q::Key; N],
    ) -> [Option<&mut Q::Value>; N] {
        let mut positions = [-1i32; N];

        // `find` can move the entries of a pending resize, which would invalidate the positions
        // found so far; `find_safe` moves nothing, and the mutable borrow forbids any other
        // modification while the references live.
        for (slot, key) in positions.iter_mut().zip(keys) {
            *slot = unsafe { Q::find_safe(self.as_ptr(), key) };
        }

        positions.map(|pos| {
            if pos < 0 {
                None
            } else {
                Some(unsafe { &mut *self.values_raw().add(pos as usize) })
            }
        })
    }

    /// Insert a key and its value.
    ///
    /// Return the previous value of the key: the caller takes over what it owns.
    ///
    /// Like [`HashMap::insert`](std::collections::HashMap::insert), an existing entry keeps its
    /// stored key, and the given key is released.
    pub fn insert(&mut self, mut key: Q::Key, value: Q::Value) -> Option<Q::Value> {
        let pos = unsafe { Q::reserve(self.as_mut_ptr(), &key, 0) };
        let existed = pos & QHASH_COLLISION != 0;
        let slot = unsafe { self.values_raw().add((pos & !QHASH_COLLISION) as usize) };

        if existed {
            W::wipe_key(&mut key);

            let previous = unsafe { slot.read() };

            unsafe {
                slot.write(value);
            }
            Some(previous)
        } else {
            unsafe {
                slot.write(value);
            }
            None
        }
    }

    /// Insert a key and its value, unless the key is already there.
    ///
    /// Return a reference to the inserted value.
    ///
    /// This is the C `qm_add()`.
    ///
    /// # Errors
    ///
    /// When the key is already there, nothing changes: the [`OccupiedError`] gives the refused key
    /// and value back to the caller, who keeps what they own, with the entry that refused them.
    pub fn try_insert(
        &mut self,
        key: Q::Key,
        value: Q::Value,
    ) -> Result<&mut Q::Value, OccupiedError<'_, 'a, Q, W>> {
        let pos = unsafe { Q::reserve(self.as_mut_ptr(), &key, 0) };

        if pos & QHASH_COLLISION != 0 {
            return Err(OccupiedError {
                entry: OccupiedEntry {
                    pos: pos & !QHASH_COLLISION,
                    map: self,
                },
                key,
                value,
            });
        }

        let slot = unsafe { self.values_raw().add(pos as usize) };

        unsafe {
            slot.write(value);
        }
        Ok(unsafe { &mut *slot })
    }

    /// Get the entry of a key, occupied or vacant, for in-place manipulation.
    ///
    /// This is the [`Entry`] API of the standard
    /// [`HashMap::entry`](std::collections::HashMap::entry). The entry takes over the given key:
    /// a vacant entry stores it on [`VacantEntry::insert`] and releases it when it is dropped,
    /// while an occupied entry keeps the stored key and releases the given one at once.
    pub fn entry(&mut self, mut key: Q::Key) -> Entry<'_, 'a, Q, W> {
        let pos = unsafe { Q::find(self.as_mut_ptr(), &key) };

        if pos < 0 {
            return Entry::Vacant(VacantEntry { map: self, key });
        }

        // The map keeps the stored key, so the given key is released, as the standard `entry()`
        // drops it.
        W::wipe_key(&mut key);
        Entry::Occupied(OccupiedEntry {
            map: self,
            pos: pos as u32,
        })
    }

    /// Remove a key and return its value.
    ///
    /// What the stored key owns is released. The value is returned, so the caller takes it over.
    pub fn remove(&mut self, key: &Q::Key) -> Option<Q::Value> {
        let pos = unsafe { Q::find(self.as_mut_ptr(), key) };

        if pos < 0 {
            return None;
        }

        let value = unsafe { self.values_raw().add(pos as usize).read() };

        self.wipe_key_at(pos as u32);
        self.del_at(pos as u32);
        Some(value)
    }

    /// Remove a key and return the stored key with its value.
    ///
    /// Nothing is released: the caller takes over what the stored key and the value own. Use
    /// [`Self::remove`] to release the key.
    pub fn remove_entry(&mut self, key: &Q::Key) -> Option<(Q::Key, Q::Value)> {
        let pos = unsafe { Q::find(self.as_mut_ptr(), key) };

        if pos < 0 {
            return None;
        }

        let stored = unsafe { self.keys_raw().add(pos as usize).read() };
        let value = unsafe { self.values_raw().add(pos as usize).read() };

        self.del_at(pos as u32);
        Some((stored, value))
    }

    /// Keep only the entries for which `keep` returns `true`.
    ///
    /// What the removed entries own, the key and the value alike, is released.
    pub fn retain<F>(&mut self, mut keep: F)
    where
        F: FnMut(&Q::Key, &mut Q::Value) -> bool,
    {
        let mut pos = first_pos(self.as_qhash());

        while pos != SCAN_END {
            let key = unsafe { &*self.keys_raw().add(pos as usize) };
            let value = unsafe { &mut *self.values_raw().add(pos as usize) };

            if !keep(key, value) {
                W::wipe_value(value);
                self.wipe_key_at(pos);
                self.del_at(pos);
            }
            pos = unsafe { qhash_scan(self.as_qhash(), pos + 1) };
        }
    }

    /// Remove every entry and yield its key and value, keeping the allocated memory.
    ///
    /// The caller takes over what every yielded entry owns. When the iterator is dropped before
    /// its end, the entries it did not yield are released, like [`Self::clear`] does.
    pub fn drain(&mut self) -> Drain<'_, 'a, Q, W> {
        let pos = first_pos(self.as_qhash());

        Drain { map: self, pos }
    }

    /// Remove and yield the entries for which `pred` returns `true`.
    ///
    /// The caller takes over what every yielded entry owns. The iterator is lazy: it removes an
    /// entry when it yields it, so the entries it did not visit stay in the map when it is
    /// dropped.
    pub fn extract_if<F>(&mut self, pred: F) -> ExtractIf<'_, 'a, Q, W, F>
    where
        F: FnMut(&Q::Key, &mut Q::Value) -> bool,
    {
        let pos = first_pos(self.as_qhash());

        ExtractIf {
            map: self,
            pos,
            pred,
        }
    }

    /// Convert the map into an owning iterator over its keys.
    ///
    /// The caller takes over what every yielded key owns, and what the matching value owns is
    /// released.
    pub fn into_keys(self) -> IntoKeys<'a, Q, W> {
        let pos = first_pos(self.as_qhash());

        IntoKeys { map: self, pos }
    }

    /// Convert the map into an owning iterator over its values.
    ///
    /// The caller takes over what every yielded value owns, and what the matching stored key owns
    /// is released.
    pub fn into_values(self) -> IntoValues<'a, Q, W> {
        let pos = first_pos(self.as_qhash());

        IntoValues { map: self, pos }
    }

    /// Iterate over the entries of the map, in an unspecified order.
    pub fn iter(&self) -> Iter<'_, Q> {
        Iter {
            qh: self.as_qhash(),
            pos: first_pos(self.as_qhash()),
            _marker: PhantomData,
        }
    }

    /// Iterate over the entries of the map, with a mutable value.
    pub fn iter_mut(&mut self) -> IterMut<'_, Q> {
        let pos = first_pos(self.as_qhash());

        IterMut {
            qh: self.as_qhash_mut(),
            pos,
            _marker: PhantomData,
        }
    }

    /// Iterate over the values of the map, in an unspecified order.
    pub fn values(&self) -> Values<'_, Q> {
        Values { iter: self.iter() }
    }

    /// Iterate over the values of the map, mutably.
    pub fn values_mut(&mut self) -> ValuesMut<'_, Q> {
        ValuesMut {
            iter: self.iter_mut(),
        }
    }

    /// Release what every entry owns, the keys and the values alike.
    #[inline]
    fn wipe_entries(&mut self) {
        if !W::WIPES {
            return;
        }

        let keys = self.keys_raw();
        let values = self.values_raw();
        let mut pos = first_pos(self.as_qhash());

        while pos != SCAN_END {
            unsafe {
                W::wipe_key(&mut *keys.add(pos as usize));
                W::wipe_value(&mut *values.add(pos as usize));
            }
            pos = unsafe { qhash_scan(self.as_qhash(), pos + 1) };
        }
    }

    /// Get a pointer to the array of values.
    ///
    /// The values are stored at the position of their key, so this array is not packed: only the
    /// positions that the iterators yield hold a value. It is null while the map has never
    /// allocated.
    #[inline]
    pub const fn values_ptr(&self) -> *const Q::Value {
        self.as_qhash().values.cast::<Q::Value>()
    }

    /// Get a mutable pointer to the array of values.
    #[inline]
    const fn values_raw(&self) -> *mut Q::Value {
        self.as_qhash().values.cast::<Q::Value>()
    }
}

impl<'t, Q, W> IntoIterator for &'t QMap<'_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = (&'t Q::Key, &'t Q::Value);
    type IntoIter = Iter<'t, Q>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<'t, Q, W> IntoIterator for &'t mut QMap<'_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = (&'t Q::Key, &'t mut Q::Value);
    type IntoIter = IterMut<'t, Q>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter_mut()
    }
}

impl<Q, W> fmt::Debug for QMap<'_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
    Q::Key: fmt::Debug,
    Q::Value: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_map().entries(self.iter()).finish()
    }
}

// }}}
// {{{ Entry

/// A view into a single entry of a [`QMap`], which is either occupied or vacant.
///
/// It is created by [`QMap::entry`].
pub enum Entry<'e, 'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    /// The key is in the map.
    Occupied(OccupiedEntry<'e, 'a, Q, W>),
    /// The key is not in the map.
    Vacant(VacantEntry<'e, 'a, Q, W>),
}

impl<'e, Q, W> Entry<'e, '_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    /// Insert `default` if the entry is vacant, and return the value of the entry.
    pub fn or_insert(self, default: Q::Value) -> &'e mut Q::Value {
        match self {
            Entry::Occupied(entry) => entry.into_mut(),
            Entry::Vacant(entry) => entry.insert(default),
        }
    }

    /// Insert the value that `default` computes if the entry is vacant, and return the value of
    /// the entry.
    pub fn or_insert_with<F: FnOnce() -> Q::Value>(self, default: F) -> &'e mut Q::Value {
        match self {
            Entry::Occupied(entry) => entry.into_mut(),
            Entry::Vacant(entry) => entry.insert(default()),
        }
    }

    /// Insert the value that `default` computes from the key if the entry is vacant, and return
    /// the value of the entry.
    pub fn or_insert_with_key<F>(self, default: F) -> &'e mut Q::Value
    where
        F: FnOnce(&Q::Key) -> Q::Value,
    {
        match self {
            Entry::Occupied(entry) => entry.into_mut(),
            Entry::Vacant(entry) => {
                let value = default(entry.key());

                entry.insert(value)
            }
        }
    }

    /// Insert the default value if the entry is vacant, and return the value of the entry.
    pub fn or_default(self) -> &'e mut Q::Value
    where
        Q::Value: Default,
    {
        self.or_insert_with(Q::Value::default)
    }

    /// Get the key of the entry.
    ///
    /// An occupied entry gives the stored key, a vacant entry the key it owns.
    pub fn key(&self) -> &Q::Key {
        match self {
            Entry::Occupied(entry) => entry.key(),
            Entry::Vacant(entry) => entry.key(),
        }
    }

    /// Modify the value if the entry is occupied.
    #[must_use]
    pub fn and_modify<F: FnOnce(&mut Q::Value)>(mut self, f: F) -> Self {
        if let Entry::Occupied(entry) = &mut self {
            f(entry.get_mut());
        }
        self
    }
}

/// View into an occupied entry of a [`QMap`].
///
/// It is a variant of [`Entry`], and part of the [`OccupiedError`] of [`QMap::try_insert`].
pub struct OccupiedEntry<'e, 'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    map: &'e mut QMap<'a, Q, W>,
    pos: u32,
}

impl<'e, Q, W> OccupiedEntry<'e, '_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    /// Get the stored key of the entry.
    pub fn key(&self) -> &Q::Key {
        unsafe { &*self.map.keys_raw().add(self.pos as usize) }
    }

    /// Get the value of the entry.
    pub fn get(&self) -> &Q::Value {
        unsafe { &*self.map.values_raw().add(self.pos as usize) }
    }

    /// Get the value of the entry, mutably.
    pub fn get_mut(&mut self) -> &mut Q::Value {
        unsafe { &mut *self.map.values_raw().add(self.pos as usize) }
    }

    /// Convert the entry into a mutable reference to its value.
    ///
    /// Unlike [`Self::get_mut`], the reference outlives the entry: it borrows the map.
    pub fn into_mut(self) -> &'e mut Q::Value {
        unsafe { &mut *self.map.values_raw().add(self.pos as usize) }
    }

    /// Set the value of the entry.
    ///
    /// Return the previous value: the caller takes over what it owns.
    pub fn insert(&mut self, value: Q::Value) -> Q::Value {
        mem::replace(self.get_mut(), value)
    }

    /// Remove the entry and return its value.
    ///
    /// What the stored key owns is released, like [`QMap::remove`] does.
    pub fn remove(self) -> Q::Value {
        let value = unsafe { self.map.values_raw().add(self.pos as usize).read() };

        self.map.wipe_key_at(self.pos);
        self.map.del_at(self.pos);
        value
    }

    /// Remove the entry and return its key and value.
    ///
    /// Nothing is released: the caller takes over what the stored key and the value own.
    pub fn remove_entry(self) -> (Q::Key, Q::Value) {
        let key = unsafe { self.map.keys_raw().add(self.pos as usize).read() };
        let value = unsafe { self.map.values_raw().add(self.pos as usize).read() };

        self.map.del_at(self.pos);
        (key, value)
    }
}

/// View into a vacant entry of a [`QMap`].
///
/// It owns the key given to [`QMap::entry`]: [`Self::insert`] stores it in the map,
/// [`Self::into_key`] gives it back, and dropping the entry releases it, as the standard
/// `VacantEntry` drops its key.
pub struct VacantEntry<'e, 'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    map: &'e mut QMap<'a, Q, W>,
    key: Q::Key,
}

impl<'e, Q, W> VacantEntry<'e, '_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    /// Get the key that the entry owns.
    pub fn key(&self) -> &Q::Key {
        &self.key
    }

    /// Take the key back out of the entry.
    pub fn into_key(self) -> Q::Key {
        // Do not release the key: the caller takes it over.
        let this = ManuallyDrop::new(self);

        unsafe { ptr::read(&raw const this.key) }
    }

    /// Insert the key of the entry with the given value.
    ///
    /// Return a reference to the inserted value: it borrows the map, so it outlives the entry.
    pub fn insert(self, value: Q::Value) -> &'e mut Q::Value {
        // Do not release the key: it moves into the map.
        let this = ManuallyDrop::new(self);
        let key = unsafe { ptr::read(&raw const this.key) };
        let map = unsafe { ptr::read(&raw const this.map) };

        // The key is known to be absent, so the position carries no collision bit.
        let pos = unsafe { Q::reserve(map.as_mut_ptr(), &key, 0) };
        let slot = unsafe { map.values_raw().add(pos as usize) };

        unsafe {
            slot.write(value);
        }
        unsafe { &mut *slot }
    }
}

impl<Q, W> Drop for VacantEntry<'_, '_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    fn drop(&mut self) {
        W::wipe_key(&mut self.key);
    }
}

/// Error of [`QMap::try_insert`] when the key is already there.
///
/// This is the `OccupiedError` of the unstable `HashMap::try_insert`: it carries the entry that
/// refused the insertion, and gives the refused key and value back to the caller.
pub struct OccupiedError<'e, 'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    /// The entry of the key that was already in the map.
    pub entry: OccupiedEntry<'e, 'a, Q, W>,
    /// The refused key: the caller keeps what it owns.
    pub key: Q::Key,
    /// The refused value: the caller keeps what it owns.
    pub value: Q::Value,
}

// }}}
// {{{ Iterators

/// Iterator over the entries of a map.
///
/// It is created by [`QMap::iter`].
pub struct Iter<'t, Q: QMapType> {
    qh: *const qhash_t,
    pos: u32,
    _marker: PhantomData<&'t Q>,
}

impl<'t, Q: QMapType> Iterator for Iter<'t, Q> {
    type Item = (&'t Q::Key, &'t Q::Value);

    fn next(&mut self) -> Option<Self::Item> {
        let qh = unsafe { &*self.qh };
        let pos = next_pos(qh, &mut self.pos)?;

        Some(unsafe {
            (
                &*qh.keys.cast::<Q::Key>().add(pos as usize),
                &*qh.values.cast::<Q::Value>().add(pos as usize),
            )
        })
    }
}

/// Iterator over the entries of a map, with a mutable value.
///
/// It is created by [`QMap::iter_mut`].
pub struct IterMut<'t, Q: QMapType> {
    qh: *mut qhash_t,
    pos: u32,
    _marker: PhantomData<&'t mut Q>,
}

impl<'t, Q: QMapType> Iterator for IterMut<'t, Q> {
    type Item = (&'t Q::Key, &'t mut Q::Value);

    fn next(&mut self) -> Option<Self::Item> {
        let qh = unsafe { &*self.qh };
        let pos = next_pos(qh, &mut self.pos)?;

        // Every position is yielded once, so the values do not alias.
        Some(unsafe {
            (
                &*qh.keys.cast::<Q::Key>().add(pos as usize),
                &mut *qh.values.cast::<Q::Value>().add(pos as usize),
            )
        })
    }
}

/// Iterator over the values of a map.
///
/// It is created by [`QMap::values`].
pub struct Values<'t, Q: QMapType> {
    iter: Iter<'t, Q>,
}

impl<'t, Q: QMapType> Iterator for Values<'t, Q> {
    type Item = &'t Q::Value;

    fn next(&mut self) -> Option<&'t Q::Value> {
        self.iter.next().map(|(_, value)| value)
    }
}

/// Iterator over the values of a map, mutably.
///
/// It is created by [`QMap::values_mut`].
pub struct ValuesMut<'t, Q: QMapType> {
    iter: IterMut<'t, Q>,
}

impl<'t, Q: QMapType> Iterator for ValuesMut<'t, Q> {
    type Item = &'t mut Q::Value;

    fn next(&mut self) -> Option<&'t mut Q::Value> {
        self.iter.next().map(|(_, value)| value)
    }
}

/// Draining iterator over the entries of a map.
///
/// It is created by [`QMap::drain`].
pub struct Drain<'d, 'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    map: &'d mut QMap<'a, Q, W>,
    pos: u32,
}

impl<Q, W> Iterator for Drain<'_, '_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = (Q::Key, Q::Value);

    fn next(&mut self) -> Option<(Q::Key, Q::Value)> {
        if self.pos == SCAN_END {
            return None;
        }

        let pos = self.pos;
        let key = unsafe { self.map.keys_raw().add(pos as usize).read() };
        let value = unsafe { self.map.values_raw().add(pos as usize).read() };

        self.map.del_at(pos);
        self.pos = unsafe { qhash_scan(self.map.as_qhash(), pos + 1) };
        Some((key, value))
    }
}

impl<Q, W> Drop for Drain<'_, '_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    fn drop(&mut self) {
        // The entries that were not yielded are released, and the memory is kept.
        let mut pos = self.pos;

        while pos != SCAN_END {
            W::wipe_value(unsafe { &mut *self.map.values_raw().add(pos as usize) });
            self.map.wipe_key_at(pos);
            pos = unsafe { qhash_scan(self.map.as_qhash(), pos + 1) };
        }
        unsafe {
            qhash_clear(self.map.as_qhash_mut());
        }
    }
}

/// Extracting iterator over the entries of a map.
///
/// It is created by [`QMap::extract_if`].
pub struct ExtractIf<'d, 'a, Q, W, F>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
    F: FnMut(&Q::Key, &mut Q::Value) -> bool,
{
    map: &'d mut QMap<'a, Q, W>,
    pos: u32,
    pred: F,
}

impl<Q, W, F> Iterator for ExtractIf<'_, '_, Q, W, F>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
    F: FnMut(&Q::Key, &mut Q::Value) -> bool,
{
    type Item = (Q::Key, Q::Value);

    fn next(&mut self) -> Option<(Q::Key, Q::Value)> {
        while self.pos != SCAN_END {
            let pos = self.pos;
            let key = unsafe { &*self.map.keys_raw().add(pos as usize) };
            let value = unsafe { &mut *self.map.values_raw().add(pos as usize) };
            let extract = (self.pred)(key, value);

            self.pos = unsafe { qhash_scan(self.map.as_qhash(), pos + 1) };
            if extract {
                let key = unsafe { self.map.keys_raw().add(pos as usize).read() };
                let value = unsafe { self.map.values_raw().add(pos as usize).read() };

                self.map.del_at(pos);
                return Some((key, value));
            }
        }
        None
    }
}

/// Owning iterator over the keys of a map.
///
/// It is created by [`QMap::into_keys`]. The caller takes over what every yielded key owns, and
/// what the values own is released. The entries that are not yielded are released with the map.
pub struct IntoKeys<'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    map: QMap<'a, Q, W>,
    pos: u32,
}

impl<Q, W> Iterator for IntoKeys<'_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = Q::Key;

    fn next(&mut self) -> Option<Q::Key> {
        if self.pos == SCAN_END {
            return None;
        }

        let pos = self.pos;
        let key = unsafe { self.map.keys_raw().add(pos as usize).read() };

        W::wipe_value(unsafe { &mut *self.map.values_raw().add(pos as usize) });
        self.map.del_at(pos);
        self.pos = unsafe { qhash_scan(self.map.as_qhash(), pos + 1) };
        Some(key)
    }
}

/// Owning iterator over the values of a map.
///
/// It is created by [`QMap::into_values`]. The caller takes over what every yielded value owns,
/// and what the stored keys own is released. The entries that are not yielded are released with
/// the map.
pub struct IntoValues<'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    map: QMap<'a, Q, W>,
    pos: u32,
}

impl<Q, W> Iterator for IntoValues<'_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = Q::Value;

    fn next(&mut self) -> Option<Q::Value> {
        if self.pos == SCAN_END {
            return None;
        }

        let pos = self.pos;
        let value = unsafe { self.map.values_raw().add(pos as usize).read() };

        self.map.wipe_key_at(pos);
        self.map.del_at(pos);
        self.pos = unsafe { qhash_scan(self.map.as_qhash(), pos + 1) };
        Some(value)
    }
}

/// Owning iterator over the entries of a map.
///
/// It is created by the `IntoIterator` implementation of [`QMap`]. The caller takes over what
/// every yielded key and value own. The entries that are not yielded are released with the map.
pub struct IntoIter<'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    map: QMap<'a, Q, W>,
    pos: u32,
}

impl<Q, W> Iterator for IntoIter<'_, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = (Q::Key, Q::Value);

    fn next(&mut self) -> Option<(Q::Key, Q::Value)> {
        if self.pos == SCAN_END {
            return None;
        }

        let pos = self.pos;
        let key = unsafe { self.map.keys_raw().add(pos as usize).read() };
        let value = unsafe { self.map.values_raw().add(pos as usize).read() };

        self.map.del_at(pos);
        self.pos = unsafe { qhash_scan(self.map.as_qhash(), pos + 1) };
        Some((key, value))
    }
}

impl<'a, Q, W> IntoIterator for QMap<'a, Q, W>
where
    Q: QMapType,
    W: QEntryWipe<Q>,
{
    type Item = (Q::Key, Q::Value);
    type IntoIter = IntoIter<'a, Q, W>;

    fn into_iter(self) -> IntoIter<'a, Q, W> {
        let pos = first_pos(self.as_qhash());

        IntoIter { map: self, pos }
    }
}

// }}}
