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
//! It behaves like a [`HashSet`](std::collections::HashSet), including the [`Entry`] API of the
//! unstable `HashSet::entry`. The allocator, the entry ownership and the C interfacing rules are
//! shared with [`QMap`](crate::qhashmap::QMap): the [`qhash`](crate::qhash) module documentation
//! describes them.

use std::fmt;
use std::iter::Chain;
use std::marker::PhantomData;
use std::mem::{ManuallyDrop, MaybeUninit};
use std::ptr;

use crate::bindings::{
    QHASH_COLLISION, mem_pool_t, qhash_clear, qhash_del_at, qhash_memory_footprint, qhash_scan,
    qhash_set_minsize, qhash_t, qhash_unseal, qhash_wipe, t_pool,
};
use crate::mem_stack::{TFromIterator, TScope};
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

impl<'a, Q, W> QHash<'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// Check whether the set holds a key.
    pub fn contains(&self, key: &Q::Key) -> bool {
        unsafe { Q::find_safe(self.as_ptr(), key) >= 0 }
    }

    /// Get the stored key that is equal to the given one.
    ///
    /// The two keys can differ, for instance two equal `lstr_t` that point to different buffers.
    pub fn get(&self, key: &Q::Key) -> Option<&Q::Key> {
        let pos = unsafe { Q::find_safe(self.as_ptr(), key) };

        if pos < 0 {
            return None;
        }
        Some(unsafe { &*self.keys_raw().add(pos as usize) })
    }

    /// Add a key to the set unless it is already there, and get the stored key.
    ///
    /// When the key is already there, the set keeps the stored key and releases the given one,
    /// like [`Self::insert`] does.
    pub fn get_or_insert(&mut self, mut key: Q::Key) -> &Q::Key {
        let pos = unsafe { Q::reserve(self.as_mut_ptr(), &key, 0) };

        if pos & QHASH_COLLISION != 0 {
            W::wipe_key(&mut key);
        }
        unsafe { &*self.keys_raw().add((pos & !QHASH_COLLISION) as usize) }
    }

    /// Add the key that `make` builds unless `key` is already there, and get the stored key.
    ///
    /// `make` builds the key to store from `key`, for instance with a deep copy of a borrowed
    /// buffer. The built key must be equal to `key`. When the key is already there, `make` is not
    /// called.
    pub fn get_or_insert_with<F>(&mut self, key: &Q::Key, make: F) -> &Q::Key
    where
        F: FnOnce(&Q::Key) -> Q::Key,
    {
        let pos = unsafe { Q::find(self.as_mut_ptr(), key) };

        if pos >= 0 {
            return unsafe { &*self.keys_raw().add(pos as usize) };
        }

        let owned = make(key);
        let pos = unsafe { Q::reserve(self.as_mut_ptr(), &owned, 0) };

        unsafe { &*self.keys_raw().add((pos & !QHASH_COLLISION) as usize) }
    }

    /// Get the entry of a key, occupied or vacant, for in-place manipulation.
    ///
    /// This is the [`Entry`] API of the unstable `HashSet::entry`. The entry takes over the given
    /// key: a vacant entry stores it on [`VacantEntry::insert`] and releases it when it is
    /// dropped, while an occupied entry keeps the stored key and releases the given one at once.
    pub fn entry(&mut self, mut key: Q::Key) -> Entry<'_, 'a, Q, W> {
        let pos = unsafe { Q::find(self.as_mut_ptr(), &key) };

        if pos < 0 {
            return Entry::Vacant(VacantEntry { set: self, key });
        }

        // The set keeps the stored key, so the given key is released, as the standard `entry()`
        // drops it.
        W::wipe_key(&mut key);
        Entry::Occupied(OccupiedEntry {
            set: self,
            pos: pos as u32,
        })
    }

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

    /// Keep only the keys for which `keep` returns `true`.
    ///
    /// What the removed keys own is released.
    pub fn retain<F>(&mut self, mut keep: F)
    where
        F: FnMut(&Q::Key) -> bool,
    {
        let mut pos = first_pos(self.as_qhash());

        while pos != SCAN_END {
            if !keep(unsafe { &*self.keys_raw().add(pos as usize) }) {
                self.wipe_key_at(pos);
                self.del_at(pos);
            }
            pos = unsafe { qhash_scan(self.as_qhash(), pos + 1) };
        }
    }

    /// Remove every key and yield it, keeping the allocated memory.
    ///
    /// The caller takes over what every yielded key owns. When the iterator is dropped before its
    /// end, the keys it did not yield are released, like [`Self::clear`] does.
    pub fn drain(&mut self) -> Drain<'_, 'a, Q, W> {
        let pos = first_pos(self.as_qhash());

        Drain { set: self, pos }
    }

    /// Remove and yield the keys for which `pred` returns `true`.
    ///
    /// The caller takes over what every yielded key owns. The iterator is lazy: it removes a key
    /// when it yields it, so the keys it did not visit stay in the set when it is dropped.
    pub fn extract_if<F>(&mut self, pred: F) -> ExtractIf<'_, 'a, Q, W, F>
    where
        F: FnMut(&Q::Key) -> bool,
    {
        let pos = first_pos(self.as_qhash());

        ExtractIf {
            set: self,
            pos,
            pred,
        }
    }

    /// Iterate over the keys of the set that are not in `other`, in an unspecified order.
    ///
    /// The destructor of `other` can differ: only its keys are read.
    pub fn difference<'t, W2>(&'t self, other: &'t QHash<'_, Q, W2>) -> Difference<'t, Q>
    where
        W2: QEntryWipe<Q>,
    {
        Difference {
            keys: self.keys(),
            other: &other.qh,
        }
    }

    /// Iterate over the keys that are both in the set and in `other`, in an unspecified order.
    pub fn intersection<'t, W2>(&'t self, other: &'t QHash<'_, Q, W2>) -> Intersection<'t, Q>
    where
        W2: QEntryWipe<Q>,
    {
        Intersection {
            keys: self.keys(),
            other: &other.qh,
        }
    }

    /// Iterate over the keys of the set and of `other`, in an unspecified order.
    ///
    /// A key that is in both sets is yielded once, from the set.
    pub fn union<'t, W2>(&'t self, other: &'t QHash<'_, Q, W2>) -> Union<'t, Q>
    where
        W2: QEntryWipe<Q>,
    {
        Union {
            iter: self.keys().chain(other.difference(self)),
        }
    }

    /// Iterate over the keys that are in exactly one of the set and `other`, in an unspecified
    /// order.
    pub fn symmetric_difference<'t, W2>(
        &'t self,
        other: &'t QHash<'_, Q, W2>,
    ) -> SymmetricDifference<'t, Q>
    where
        W2: QEntryWipe<Q>,
    {
        SymmetricDifference {
            iter: self.difference(other).chain(other.difference(self)),
        }
    }

    /// Check whether the set and `other` have no key in common.
    pub fn is_disjoint<W2>(&self, other: &QHash<'_, Q, W2>) -> bool
    where
        W2: QEntryWipe<Q>,
    {
        if self.len() <= other.len() {
            self.intersection(other).next().is_none()
        } else {
            other.intersection(self).next().is_none()
        }
    }

    /// Check whether every key of the set is in `other`.
    pub fn is_subset<W2>(&self, other: &QHash<'_, Q, W2>) -> bool
    where
        W2: QEntryWipe<Q>,
    {
        self.len() <= other.len() && self.difference(other).next().is_none()
    }

    /// Check whether every key of `other` is in the set.
    pub fn is_superset<W2>(&self, other: &QHash<'_, Q, W2>) -> bool
    where
        W2: QEntryWipe<Q>,
    {
        other.is_subset(self)
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

impl<Q, W> Extend<Q::Key> for QHash<'_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// Add every key to the set, like [`QHash::insert`] does: a duplicate key is released.
    fn extend<I: IntoIterator<Item = Q::Key>>(&mut self, iter: I) {
        let iter = iter.into_iter();

        self.reserve(self.len() + iter.size_hint().0);
        for key in iter {
            self.insert(key);
        }
    }
}

/// Collect an iterator into a set allocated by libc.
impl<Q, W> FromIterator<Q::Key> for QHash<'_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    fn from_iter<I: IntoIterator<Item = Q::Key>>(iter: I) -> Self {
        let mut set = Self::new();

        set.extend(iter);
        set
    }
}

/// Collect an iterator into a set allocated on the `t_pool` of a scope.
///
/// This backs [`TCollect::t_collect`](crate::mem_stack::TCollect::t_collect). A duplicate key is
/// released, like [`QHash::insert`] does.
impl<'a, Q, W> TFromIterator<'a, Q::Key> for QHash<'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    fn t_from_iter<I: IntoIterator<Item = Q::Key>>(t_scope: &'a TScope, iter: I) -> Self {
        let iter = iter.into_iter();
        let mut set = Self::t_with_capacity(t_scope, iter.size_hint().0);

        set.extend(iter);
        set
    }
}

// }}}
// {{{ Entry

/// A view into a single key of a [`QHash`], which is either occupied or vacant.
///
/// It is created by [`QHash::entry`].
pub enum Entry<'e, 'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// The key is in the set.
    Occupied(OccupiedEntry<'e, 'a, Q, W>),
    /// The key is not in the set.
    Vacant(VacantEntry<'e, 'a, Q, W>),
}

impl<'e, 'a, Q, W> Entry<'e, 'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// Insert the key if the entry is vacant, and get the occupied entry.
    pub fn insert(self) -> OccupiedEntry<'e, 'a, Q, W> {
        match self {
            Entry::Occupied(entry) => entry,
            Entry::Vacant(entry) => entry.insert_entry(),
        }
    }

    /// Insert the key if the entry is vacant.
    pub fn or_insert(self) {
        if let Entry::Vacant(entry) = self {
            entry.insert();
        }
    }

    /// Get the key of the entry.
    ///
    /// An occupied entry gives the stored key, a vacant entry the key it owns.
    pub fn get(&self) -> &Q::Key {
        match self {
            Entry::Occupied(entry) => entry.get(),
            Entry::Vacant(entry) => entry.get(),
        }
    }
}

/// View into an occupied entry of a [`QHash`].
///
/// It is a variant of [`Entry`].
pub struct OccupiedEntry<'e, 'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    set: &'e mut QHash<'a, Q, W>,
    pos: u32,
}

impl<Q, W> OccupiedEntry<'_, '_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// Get the stored key of the entry.
    pub fn get(&self) -> &Q::Key {
        unsafe { &*self.set.keys_raw().add(self.pos as usize) }
    }

    /// Remove the entry and return its stored key.
    ///
    /// Nothing is released: the caller takes over what the stored key owns, like [`QHash::take`].
    pub fn remove(self) -> Q::Key {
        let key = unsafe { self.set.keys_raw().add(self.pos as usize).read() };

        self.set.del_at(self.pos);
        key
    }
}

/// View into a vacant entry of a [`QHash`].
///
/// It owns the key given to [`QHash::entry`]: [`Self::insert`] stores it in the set,
/// [`Self::into_value`] gives it back, and dropping the entry releases it, as the standard
/// `VacantEntry` drops its value.
pub struct VacantEntry<'e, 'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    set: &'e mut QHash<'a, Q, W>,
    key: Q::Key,
}

impl<'e, 'a, Q, W> VacantEntry<'e, 'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    /// Get the key that the entry owns.
    pub fn get(&self) -> &Q::Key {
        &self.key
    }

    /// Take the key back out of the entry.
    pub fn into_value(self) -> Q::Key {
        // Do not release the key: the caller takes it over.
        let this = ManuallyDrop::new(self);

        unsafe { ptr::read(&raw const this.key) }
    }

    /// Insert the key of the entry into the set.
    pub fn insert(self) {
        self.insert_entry();
    }

    /// Insert the key of the entry and get the occupied entry.
    fn insert_entry(self) -> OccupiedEntry<'e, 'a, Q, W> {
        // Do not release the key: it moves into the set.
        let this = ManuallyDrop::new(self);
        let key = unsafe { ptr::read(&raw const this.key) };
        let set = unsafe { ptr::read(&raw const this.set) };

        // The key is known to be absent, so the position carries no collision bit.
        let pos = unsafe { Q::reserve(set.as_mut_ptr(), &key, 0) };

        OccupiedEntry { set, pos }
    }
}

impl<Q, W> Drop for VacantEntry<'_, '_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    fn drop(&mut self) {
        W::wipe_key(&mut self.key);
    }
}

// }}}
// {{{ Set iterators

/// Draining iterator over the keys of a set.
///
/// It is created by [`QHash::drain`].
pub struct Drain<'d, 'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    set: &'d mut QHash<'a, Q, W>,
    pos: u32,
}

impl<Q, W> Iterator for Drain<'_, '_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    type Item = Q::Key;

    fn next(&mut self) -> Option<Q::Key> {
        if self.pos == SCAN_END {
            return None;
        }

        let pos = self.pos;
        let key = unsafe { self.set.keys_raw().add(pos as usize).read() };

        self.set.del_at(pos);
        self.pos = unsafe { qhash_scan(self.set.as_qhash(), pos + 1) };
        Some(key)
    }
}

impl<Q, W> Drop for Drain<'_, '_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    fn drop(&mut self) {
        // The keys that were not yielded are released, and the memory is kept.
        let mut pos = self.pos;

        while pos != SCAN_END {
            self.set.wipe_key_at(pos);
            pos = unsafe { qhash_scan(self.set.as_qhash(), pos + 1) };
        }
        unsafe {
            qhash_clear(self.set.as_qhash_mut());
        }
    }
}

/// Extracting iterator over the keys of a set.
///
/// It is created by [`QHash::extract_if`].
pub struct ExtractIf<'d, 'a, Q, W, F>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
    F: FnMut(&Q::Key) -> bool,
{
    set: &'d mut QHash<'a, Q, W>,
    pos: u32,
    pred: F,
}

impl<Q, W, F> Iterator for ExtractIf<'_, '_, Q, W, F>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
    F: FnMut(&Q::Key) -> bool,
{
    type Item = Q::Key;

    fn next(&mut self) -> Option<Q::Key> {
        while self.pos != SCAN_END {
            let pos = self.pos;
            let stored = unsafe { &*self.set.keys_raw().add(pos as usize) };
            let extract = (self.pred)(stored);

            self.pos = unsafe { qhash_scan(self.set.as_qhash(), pos + 1) };
            if extract {
                let key = unsafe { self.set.keys_raw().add(pos as usize).read() };

                self.set.del_at(pos);
                return Some(key);
            }
        }
        None
    }
}

/// Iterator over the keys of a set that are not in another one.
///
/// It is created by [`QHash::difference`].
pub struct Difference<'t, Q: QHashType<Value = ()>> {
    keys: Keys<'t, Q>,

    // The C table of the other set: the destructor type of the set is not needed to read it.
    other: &'t Q,
}

impl<'t, Q: QHashType<Value = ()>> Iterator for Difference<'t, Q> {
    type Item = &'t Q::Key;

    fn next(&mut self) -> Option<&'t Q::Key> {
        let other = self.other;

        self.keys
            .by_ref()
            .find(|&key| unsafe { Q::find_safe(other, key) } < 0)
    }
}

/// Iterator over the keys of a set that are also in another one.
///
/// It is created by [`QHash::intersection`].
pub struct Intersection<'t, Q: QHashType<Value = ()>> {
    keys: Keys<'t, Q>,

    // The C table of the other set: the destructor type of the set is not needed to read it.
    other: &'t Q,
}

impl<'t, Q: QHashType<Value = ()>> Iterator for Intersection<'t, Q> {
    type Item = &'t Q::Key;

    fn next(&mut self) -> Option<&'t Q::Key> {
        let other = self.other;

        self.keys
            .by_ref()
            .find(|&key| unsafe { Q::find_safe(other, key) } >= 0)
    }
}

/// Iterator over the keys of two sets.
///
/// It is created by [`QHash::union`].
pub struct Union<'t, Q: QHashType<Value = ()>> {
    iter: Chain<Keys<'t, Q>, Difference<'t, Q>>,
}

impl<'t, Q: QHashType<Value = ()>> Iterator for Union<'t, Q> {
    type Item = &'t Q::Key;

    fn next(&mut self) -> Option<&'t Q::Key> {
        self.iter.next()
    }
}

/// Iterator over the keys that are in exactly one of two sets.
///
/// It is created by [`QHash::symmetric_difference`].
pub struct SymmetricDifference<'t, Q: QHashType<Value = ()>> {
    iter: Chain<Difference<'t, Q>, Difference<'t, Q>>,
}

impl<'t, Q: QHashType<Value = ()>> Iterator for SymmetricDifference<'t, Q> {
    type Item = &'t Q::Key;

    fn next(&mut self) -> Option<&'t Q::Key> {
        self.iter.next()
    }
}

/// Owning iterator over the keys of a set.
///
/// It is created by the `IntoIterator` implementation of [`QHash`]. The caller takes over what
/// every yielded key owns; the keys that are not yielded are released with the set.
pub struct IntoIter<'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    set: QHash<'a, Q, W>,
    pos: u32,
}

impl<Q, W> Iterator for IntoIter<'_, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    type Item = Q::Key;

    fn next(&mut self) -> Option<Q::Key> {
        if self.pos == SCAN_END {
            return None;
        }

        let pos = self.pos;
        let key = unsafe { self.set.keys_raw().add(pos as usize).read() };

        self.set.del_at(pos);
        self.pos = unsafe { qhash_scan(self.set.as_qhash(), pos + 1) };
        Some(key)
    }
}

impl<'a, Q, W> IntoIterator for QHash<'a, Q, W>
where
    Q: QHashType<Value = ()>,
    W: QEntryWipe<Q>,
{
    type Item = Q::Key;
    type IntoIter = IntoIter<'a, Q, W>;

    fn into_iter(self) -> IntoIter<'a, Q, W> {
        let pos = first_pos(self.as_qhash());

        IntoIter { set: self, pos }
    }
}

// }}}
// {{{ Tests

#[cfg(test)]
#[allow(clippy::redundant_test_prefix)]
mod tests {
    use std::cell::Cell;
    use std::mem;

    use super::*;
    use crate::bindings::{lstr_t, qh_lstr_t, qh_u32_t};
    use crate::lstr::{from_raw_utf8, from_str};
    use crate::mem_stack::TCollect as _;
    use crate::qvector::QVector;

    // {{{ Test helpers

    /// Build the `lstr_t` of a static string.
    fn key(s: &'static str) -> lstr_t {
        from_str(s).as_raw()
    }

    /// Collect the keys of a set of integers, sorted.
    fn sorted_keys(set: &QHash<'_, qh_u32_t>) -> Vec<u32> {
        let mut keys: Vec<u32> = set.keys().copied().collect();

        keys.sort_unstable();
        keys
    }

    /// Collect the keys of a set of strings, sorted.
    fn sorted_str_keys(set: &QHash<'_, qh_lstr_t>) -> Vec<&'static str> {
        // The keys are built from string literals, so they live as long as the program.
        let mut keys: Vec<&'static str> = set
            .keys()
            .map(|raw| unsafe { from_raw_utf8(*raw).as_str() })
            .collect();

        keys.sort_unstable();
        keys
    }

    // }}}
    // {{{ Basic operations

    #[test]
    fn test_new_is_empty() {
        let set = QHash::<qh_u32_t>::new();

        assert!(set.is_empty());
        assert_eq!(set.len(), 0);
        assert!(!set.contains(&1));
        assert_eq!(set.keys().count(), 0);
    }

    #[test]
    fn test_default() {
        let set = QHash::<qh_u32_t>::default();

        assert!(set.is_empty());
    }

    #[test]
    fn test_insert_and_contains() {
        let mut set = QHash::<qh_u32_t>::new();

        assert!(set.insert(1));
        assert!(set.insert(2));

        // A key that is already there is not added again.
        assert!(!set.insert(1));

        assert_eq!(set.len(), 2);
        assert!(set.contains(&1));
        assert!(set.contains(&2));
        assert!(!set.contains(&3));
    }

    #[test]
    fn test_replace() {
        let mut set = QHash::<qh_u32_t>::new();

        assert_eq!(set.replace(1), None);
        assert_eq!(set.replace(1), Some(1));
        assert_eq!(set.len(), 1);
    }

    #[test]
    fn test_remove() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(1);
        set.insert(2);

        assert!(set.remove(&1));
        assert!(!set.remove(&1));
        assert_eq!(set.len(), 1);
        assert!(!set.contains(&1));
        assert!(set.contains(&2));
    }

    #[test]
    fn test_take_returns_the_stored_key() {
        let mut set = QHash::<qh_lstr_t>::new();
        let stored = key("one");

        set.insert(stored);

        // The key given here is a different `lstr_t` with the same content; `take()` gives back
        // the one the set stored, which is what a deep delete has to release.
        let Some(taken) = set.take(&key("one")) else {
            panic!("the key must be there");
        };

        let taken = unsafe { from_raw_utf8(taken).as_str() };
        let stored = unsafe { from_raw_utf8(stored).as_str() };

        assert_eq!(taken, stored);
        assert!(ptr::eq(taken.as_ptr(), stored.as_ptr()));
        assert!(set.is_empty());
        assert!(set.take(&key("one")).is_none());
    }

    #[test]
    fn test_clear() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..10 {
            set.insert(i);
        }
        set.clear();

        assert!(set.is_empty());
        assert!(!set.contains(&0));

        // The set is still usable after a clear.
        set.insert(42);
        assert_eq!(sorted_keys(&set), [42]);
    }

    #[test]
    fn test_iterate() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..5 {
            set.insert(i);
        }

        assert_eq!(sorted_keys(&set), [0, 1, 2, 3, 4]);

        // `IntoIterator` on a reference iterates over the keys.
        let mut from_ref: Vec<u32> = (&set).into_iter().copied().collect();

        from_ref.sort_unstable();
        assert_eq!(from_ref, [0, 1, 2, 3, 4]);
    }

    #[test]
    fn test_many_keys_resizes() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..10_000 {
            assert!(set.insert(i));
        }

        assert_eq!(set.len(), 10_000);
        for i in 0..10_000 {
            assert!(set.contains(&i));
        }
        assert!(!set.contains(&10_000));
    }

    #[test]
    fn test_with_capacity_and_reserve() {
        let mut set = QHash::<qh_u32_t>::with_capacity(128);

        assert!(set.is_empty());
        set.insert(1);
        assert_eq!(set.len(), 1);

        set.reserve(4096);
        assert!(set.contains(&1));
    }

    #[test]
    fn test_memory_footprint() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..100 {
            set.insert(i);
        }

        assert!(set.memory_footprint() > 0);
    }

    #[test]
    fn test_debug() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(7);
        assert_eq!(format!("{set:?}"), "{7}");
    }

    // }}}
    // {{{ Entry

    #[test]
    fn test_entry_or_insert() {
        let mut set = QHash::<qh_u32_t>::new();

        set.entry(1).or_insert();
        set.entry(1).or_insert();

        assert_eq!(set.len(), 1);
        assert!(set.contains(&1));
    }

    #[test]
    fn test_entry_insert_returns_the_occupied_entry() {
        let mut set = QHash::<qh_u32_t>::new();

        // A vacant entry inserts its key.
        let entry = set.entry(1).insert();

        assert_eq!(*entry.get(), 1);
        assert_eq!(set.len(), 1);

        // The entry of a key that is already there is given back as is.
        let entry = set.entry(1).insert();

        assert_eq!(*entry.get(), 1);
        assert_eq!(set.len(), 1);
    }

    #[test]
    fn test_entry_get_names_the_key() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(1);

        // The occupied entry names the stored key, the vacant entry the key it owns.
        assert_eq!(*set.entry(1).get(), 1);
        assert_eq!(*set.entry(2).get(), 2);

        // Reading an entry inserts nothing.
        assert_eq!(set.len(), 1);
    }

    #[test]
    fn test_entry_releases_the_given_key_when_occupied() {
        let mut set = CountingSet::new();

        set.insert(1);
        assert_eq!(take_wiped(), 0);

        // The set keeps the stored key, so the entry releases the given key at once.
        set.entry(1).or_insert();
        assert_eq!(take_wiped(), 1);
        assert_eq!(set.len(), 1);
    }

    #[test]
    fn test_occupied_entry_remove_hands_the_key_over() {
        let mut set = CountingSet::new();

        set.insert(1);

        let Entry::Occupied(entry) = set.entry(1) else {
            panic!("the entry must be occupied");
        };

        // The given key is released by `entry()`; the stored key is handed over by `remove()`.
        assert_eq!(take_wiped(), 1);
        assert_eq!(entry.remove(), 1);
        assert_eq!(take_wiped(), 0);
        assert!(set.is_empty());
    }

    #[test]
    fn test_vacant_entry_releases_its_key_on_drop() {
        let mut set = CountingSet::new();

        // The vacant entry owns the key: dropping it without an insertion releases the key.
        drop(set.entry(1));

        assert_eq!(take_wiped(), 1);
        assert!(set.is_empty());
    }

    #[test]
    fn test_vacant_entry_into_value() {
        let mut set = CountingSet::new();

        let Entry::Vacant(entry) = set.entry(1) else {
            panic!("the entry must be vacant");
        };

        // The entry gives the key back, and inserts nothing.
        assert_eq!(entry.into_value(), 1);
        assert_eq!(take_wiped(), 0);
        assert!(set.is_empty());
    }

    // }}}
    // {{{ Keys that need a hash function

    #[test]
    fn test_string_keys() {
        let mut set = QHash::<qh_lstr_t>::new();

        assert!(set.insert(key("one")));
        assert!(set.insert(key("two")));

        // Equal keys are equal by content, not by pointer: the string is rebuilt here.
        assert!(!set.insert(key("one")));

        assert_eq!(set.len(), 2);
        assert!(set.contains(&key("two")));
        assert!(!set.contains(&key("three")));
        assert_eq!(sorted_str_keys(&set), ["one", "two"]);

        assert!(set.remove(&key("one")));
        assert_eq!(sorted_str_keys(&set), ["two"]);
    }

    #[test]
    fn test_hash_of_is_stable() {
        let set = QHash::<qh_lstr_t>::new();

        assert_eq!(set.hash_of(&key("abc")), set.hash_of(&key("abc")));
    }

    #[test]
    fn test_cached_hashes() {
        let mut set = QHash::<qh_lstr_t>::new_cached();

        for i in 0..1_000 {
            set.insert(key(if i % 2 == 0 { "even" } else { "odd" }));
        }

        assert_eq!(set.len(), 2);
        assert_eq!(sorted_str_keys(&set), ["even", "odd"]);
    }

    #[test]
    fn test_get_returns_the_stored_key() {
        let mut set = QHash::<qh_lstr_t>::new();
        let stored = key("one");

        set.insert(stored);

        // The key given here is a different `lstr_t` with the same content; `get()` gives the
        // stored one.
        let Some(found) = set.get(&key("one")) else {
            panic!("the key must be there");
        };

        let found = unsafe { from_raw_utf8(*found).as_str() };
        let stored = unsafe { from_raw_utf8(stored).as_str() };

        assert!(ptr::eq(found.as_ptr(), stored.as_ptr()));
        assert!(set.get(&key("two")).is_none());
    }

    #[test]
    fn test_get_or_insert_with() {
        let mut set = QHash::<qh_lstr_t>::new();
        let built = key("one");

        // The key is absent: the key that the closure builds is stored.
        let inserted = *set.get_or_insert_with(&key("one"), |_| built);
        let inserted = unsafe { from_raw_utf8(inserted).as_str() };
        let built = unsafe { from_raw_utf8(built).as_str() };

        assert!(ptr::eq(inserted.as_ptr(), built.as_ptr()));

        // The key is there: the closure is not called.
        set.get_or_insert_with(&key("one"), |_| panic!("the closure must not be called"));
        assert_eq!(set.len(), 1);
    }

    // }}}
    // {{{ Allocators

    #[test]
    fn test_t_pool() {
        let t_scope = TScope::new_scope();
        let mut set = QHash::<qh_u32_t>::t_new(&t_scope);

        for i in 0..1_000 {
            set.insert(i);
        }

        assert_eq!(set.len(), 1_000);
        assert!(set.contains(&999));
    }

    #[test]
    fn test_t_with_capacity() {
        let t_scope = TScope::new_scope();
        let mut set = QHash::<qh_lstr_t>::t_with_capacity(&t_scope, 64);

        set.insert(key("a"));
        assert_eq!(sorted_str_keys(&set), ["a"]);
    }

    // }}}
    // {{{ Sealing

    #[test]
    fn test_seal_and_unseal() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..100 {
            set.insert(i);
        }

        set.seal();

        // A sealed table can still be read.
        assert_eq!(set.len(), 100);
        assert!(set.contains(&50));

        set.unseal();

        // And it accepts the modifications again.
        assert!(set.insert(100));
        assert_eq!(set.len(), 101);
    }

    // }}}
    // {{{ Entry ownership

    /// A set whose entries own something, so that the destructor can be observed.
    type CountingSet<'a> = QHash<'a, qh_u32_t, CountingWipe>;

    // Number of keys that `CountingWipe` released, for the running test. The test harness gives
    // every test its own thread, so this counter is per test.
    thread_local! {
        static WIPED: Cell<usize> = const { Cell::new(0) };
    }

    /// Get the number of keys released so far, and reset the count.
    fn take_wiped() -> usize {
        WIPED.replace(0)
    }

    /// Entry destructor that counts the keys it releases.
    struct CountingWipe;

    impl QEntryWipe<qh_u32_t> for CountingWipe {
        fn wipe_key(_key: &mut u32) {
            WIPED.set(WIPED.get() + 1);
        }

        fn wipe_value(_value: &mut ()) {}
    }

    #[test]
    fn test_drop_releases_the_entries() {
        assert_eq!(take_wiped(), 0);

        {
            let mut set = CountingSet::new();

            for i in 0..5 {
                set.insert(i);
            }
            assert_eq!(WIPED.get(), 0);
        }

        assert_eq!(take_wiped(), 5);
    }

    #[test]
    fn test_clear_releases_the_entries() {
        let mut set = CountingSet::new();

        for i in 0..5 {
            set.insert(i);
        }
        set.clear();

        assert_eq!(take_wiped(), 5);
        assert!(set.is_empty());
    }

    #[test]
    fn test_remove_releases_the_entry_but_take_does_not() {
        let mut set = CountingSet::new();

        set.insert(1);
        set.insert(2);

        assert!(set.remove(&1));
        assert_eq!(take_wiped(), 1);

        // `take()` hands the stored key over, so it must not release it.
        assert_eq!(set.take(&2), Some(2));
        assert_eq!(take_wiped(), 0);
        assert!(set.is_empty());
    }

    #[test]
    fn test_insert_releases_the_refused_key() {
        let mut set = CountingSet::new();

        set.insert(1);
        assert_eq!(take_wiped(), 0);

        // The set keeps the stored key, so the refused key is released, as `HashSet::insert`
        // drops it.
        assert!(!set.insert(1));
        assert_eq!(take_wiped(), 1);
    }

    #[test]
    fn test_replace_returns_the_previous_key() {
        let mut set = CountingSet::new();

        set.insert(1);

        // The previous key is returned, so the caller takes it over: nothing is released.
        assert_eq!(set.replace(1), Some(1));
        assert_eq!(take_wiped(), 0);

        // A key that was not there replaces nothing.
        assert_eq!(set.replace(2), None);
        assert_eq!(take_wiped(), 0);
    }

    #[test]
    fn test_view_the_table_with_another_destructor() {
        /// Read a set that names no destructor, as a helper of another crate would.
        fn sum(set: &QHash<'_, qh_u32_t>) -> u32 {
            set.keys().copied().sum()
        }

        let mut set = CountingSet::new();

        set.insert(1);
        set.insert(2);

        // The same table, handed to code that names the other destructor.
        assert_eq!(sum(set.with_wipe()), 3);

        // Nothing was released: only the view changed.
        assert_eq!(take_wiped(), 0);
        assert_eq!(sorted_keys(set.with_wipe()), [1, 2]);

        // And the table still releases its entries when it is dropped.
        drop(set);
        assert_eq!(take_wiped(), 2);
    }

    #[test]
    fn test_view_the_table_with_another_destructor_mutably() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(1);

        // Take a destructor on, then clear through it.
        {
            let owning: &mut CountingSet<'_> = unsafe { set.with_wipe_mut() };

            owning.clear();
        }

        assert_eq!(take_wiped(), 1);
        assert!(set.is_empty());
    }

    #[test]
    fn test_a_table_without_a_destructor_releases_nothing() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..5 {
            set.insert(i);
        }
        set.clear();

        assert_eq!(take_wiped(), 0);
    }

    #[test]
    fn test_get_or_insert_releases_the_refused_key() {
        let mut set = CountingSet::new();

        assert_eq!(*set.get_or_insert(1), 1);
        assert_eq!(take_wiped(), 0);

        // The set keeps the stored key and releases the given one, like `insert` does.
        assert_eq!(*set.get_or_insert(1), 1);
        assert_eq!(take_wiped(), 1);
        assert_eq!(set.len(), 1);
    }

    // }}}
    // {{{ Bulk removal

    #[test]
    fn test_retain() {
        let mut set = CountingSet::new();

        for i in 0..10 {
            set.insert(i);
        }
        set.retain(|key| key % 2 == 0);

        // The removed keys are released, the kept keys are not touched.
        assert_eq!(take_wiped(), 5);
        assert_eq!(sorted_keys(set.with_wipe()), [0, 2, 4, 6, 8]);
    }

    #[test]
    fn test_drain_hands_the_keys_over() {
        let mut set = CountingSet::new();

        for i in 0..4 {
            set.insert(i);
        }

        // The caller takes the yielded keys over, so nothing is released.
        let mut drained: Vec<u32> = set.drain().collect();

        drained.sort_unstable();
        assert_eq!(drained, [0, 1, 2, 3]);
        assert_eq!(take_wiped(), 0);
        assert!(set.is_empty());

        // The set is still usable after a drain.
        set.insert(42);
        assert_eq!(set.len(), 1);
    }

    #[test]
    fn test_drain_releases_the_keys_it_did_not_yield() {
        let mut set = CountingSet::new();

        for i in 0..4 {
            set.insert(i);
        }

        {
            let mut drain = set.drain();

            assert!(drain.next().is_some());
            assert!(drain.next().is_some());
        }

        // The two keys that were not yielded are released, like `clear()` does.
        assert_eq!(take_wiped(), 2);
        assert!(set.is_empty());
    }

    #[test]
    fn test_extract_if() {
        let mut set = CountingSet::new();

        for i in 0..10 {
            set.insert(i);
        }

        let mut extracted: Vec<u32> = set.extract_if(|key| key % 2 == 0).collect();

        extracted.sort_unstable();
        assert_eq!(extracted, [0, 2, 4, 6, 8]);

        // The yielded keys are handed over, and the kept keys are not touched.
        assert_eq!(take_wiped(), 0);
        assert_eq!(sorted_keys(set.with_wipe()), [1, 3, 5, 7, 9]);
    }

    #[test]
    fn test_extract_if_is_lazy() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..10 {
            set.insert(i);
        }

        // Dropping the iterator early keeps the keys it did not visit.
        assert!(set.extract_if(|key| key % 2 == 0).next().is_some());

        assert_eq!(set.len(), 9);
    }

    // }}}
    // {{{ Set algebra

    #[test]
    fn test_set_algebra() {
        let mut a = QHash::<qh_u32_t>::new();
        let mut b = QHash::<qh_u32_t>::new();

        for i in [1, 2, 3] {
            a.insert(i);
        }
        for i in [2, 3, 4] {
            b.insert(i);
        }

        let mut difference: Vec<u32> = a.difference(&b).copied().collect();
        let mut intersection: Vec<u32> = a.intersection(&b).copied().collect();
        let mut union: Vec<u32> = a.union(&b).copied().collect();
        let mut symmetric: Vec<u32> = a.symmetric_difference(&b).copied().collect();

        difference.sort_unstable();
        intersection.sort_unstable();
        union.sort_unstable();
        symmetric.sort_unstable();

        assert_eq!(difference, [1]);
        assert_eq!(intersection, [2, 3]);
        assert_eq!(union, [1, 2, 3, 4]);
        assert_eq!(symmetric, [1, 4]);
    }

    #[test]
    fn test_set_predicates() {
        let mut small = QHash::<qh_u32_t>::new();
        let mut big = QHash::<qh_u32_t>::new();
        let mut apart = QHash::<qh_u32_t>::new();

        for i in [1, 2] {
            small.insert(i);
        }
        for i in [1, 2, 3] {
            big.insert(i);
        }
        apart.insert(9);

        assert!(small.is_subset(&big));
        assert!(!big.is_subset(&small));
        assert!(big.is_superset(&small));
        assert!(small.is_disjoint(&apart));
        assert!(!small.is_disjoint(&big));
    }

    #[test]
    fn test_set_algebra_ignores_the_destructor() {
        let mut counting = CountingSet::new();
        let mut plain = QHash::<qh_u32_t>::new();

        counting.insert(1);
        counting.insert(2);
        plain.insert(2);

        // The two sets name different destructors, and the comparison reads the keys only.
        assert_eq!(
            counting.difference(&plain).copied().collect::<Vec<u32>>(),
            [1]
        );
        assert!(!counting.is_disjoint(&plain));
        assert_eq!(take_wiped(), 0);
    }

    // }}}
    // {{{ Owning iteration

    #[test]
    fn test_into_iter_hands_the_keys_over() {
        let mut set = CountingSet::new();

        for i in 0..3 {
            set.insert(i);
        }

        let mut keys: Vec<u32> = set.into_iter().collect();

        keys.sort_unstable();
        assert_eq!(keys, [0, 1, 2]);
        assert_eq!(take_wiped(), 0);
    }

    #[test]
    fn test_into_iter_releases_the_keys_it_did_not_yield() {
        let mut set = CountingSet::new();

        for i in 0..3 {
            set.insert(i);
        }

        let mut iter = set.into_iter();

        assert!(iter.next().is_some());
        drop(iter);

        // The set is dropped with the iterator, so it releases the two remaining keys.
        assert_eq!(take_wiped(), 2);
    }

    // }}}
    // {{{ Conversions

    #[test]
    fn test_collect_a_vector_into_a_set() {
        let vector: QVector<'_, u32> = [3, 1, 2, 3, 1].into_iter().collect();

        // The set deduplicates the vector.
        let set: QHash<'_, qh_u32_t> = vector.iter().copied().collect();

        assert_eq!(sorted_keys(&set), [1, 2, 3]);
    }

    #[test]
    fn test_collect_a_set_into_a_vector() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..5 {
            set.insert(i);
        }

        let mut vector: QVector<'_, u32> = set.iter().copied().collect();

        vector.sort_unstable();
        assert_eq!(vector, [0, 1, 2, 3, 4]);
    }

    #[test]
    fn test_extend_a_set() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(0);
        set.extend([1, 2]);

        let vector: QVector<'_, u32> = (2..4).collect();

        set.extend(vector.iter().copied());
        assert_eq!(sorted_keys(&set), [0, 1, 2, 3]);
    }

    #[test]
    fn test_collect_releases_the_duplicate_keys() {
        let set: CountingSet<'_> = [1, 2, 1].into_iter().collect();

        // The duplicate key is released, like `insert` does.
        assert_eq!(take_wiped(), 1);
        assert_eq!(set.len(), 2);
    }

    #[test]
    fn test_t_collect_a_set() {
        let t_scope = TScope::new_scope();
        let vector: QVector<'_, u32> = [3, 1, 2, 3].into_iter().t_collect(&t_scope);

        // The vector and the set both live on the `t_pool` of the scope.
        let set: QHash<'_, qh_u32_t> = vector.iter().copied().t_collect(&t_scope);

        assert_eq!(sorted_keys(&set), [1, 2, 3]);
    }

    // }}}
    // {{{ C interoperability

    /// Fill a set the way a C function does: through a pointer.
    ///
    /// # Safety
    ///
    /// `qh` must point to an initialized C table.
    unsafe extern "C" fn fill_like_c(qh: *mut qh_u32_t, count: u32) {
        // The table type comes from the pointer: no type has to be named here.
        let set: &mut QHash<'_, qh_u32_t> = unsafe { QHash::from_c_ptr_mut(qh) };

        for i in 0..count {
            set.insert(i);
        }
    }

    /// Count the keys of a set the way a C function does: through a const pointer.
    ///
    /// # Safety
    ///
    /// `qh` must point to an initialized C table.
    unsafe extern "C" fn count_like_c(qh: *const qh_u32_t) -> usize {
        let set: &QHash<'_, qh_u32_t> = unsafe { QHash::from_c_ptr(qh) };

        set.len()
    }

    #[test]
    fn test_borrow_through_a_pointer() {
        let mut set = QHash::<qh_u32_t>::new();

        // A C prototype takes a pointer, which is what the conversions take.
        unsafe {
            fill_like_c(set.as_mut_ptr(), 5);
        }

        assert_eq!(sorted_keys(&set), [0, 1, 2, 3, 4]);
        assert_eq!(unsafe { count_like_c(set.as_ptr()) }, 5);
    }

    #[test]
    fn test_borrow_through_a_pointer_is_zero_copy() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(1);

        let keys = set.keys_ptr();
        let qh = set.as_mut_ptr();

        {
            let borrowed: &mut QHash<'_, qh_u32_t> = unsafe { QHash::from_c_ptr_mut(qh) };

            assert!(ptr::eq(borrowed.keys_ptr(), keys));
            borrowed.insert(2);
        }

        // Dropping the borrow must not wipe the table.
        assert_eq!(sorted_keys(&set), [1, 2]);
        assert!(ptr::eq(set.keys_ptr(), keys));
    }

    #[test]
    fn test_take_ownership_through_a_pointer() {
        let mut source = QHash::<qh_u32_t>::new();

        for i in 0..10 {
            source.insert(i);
        }

        let keys = source.keys_ptr();
        let mut c_set: qh_u32_t = source.into_c();

        // Take the table over without copying it.
        let taken = unsafe { QHash::take_from_c_ptr(&raw mut c_set) };

        assert!(ptr::eq(taken.keys_ptr(), keys));
        assert_eq!(sorted_keys(&taken), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);

        // The C table is left empty, so wiping it releases nothing.
        {
            let left = unsafe { QHash::<qh_u32_t>::from_c_ptr(&raw const c_set) };

            assert!(left.is_empty());
            assert!(left.keys_ptr().is_null());
        }
        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_set).cast::<qhash_t>());
        }

        assert_eq!(taken.len(), 10);
    }

    #[test]
    fn test_the_emptied_table_stays_usable() {
        let mut source = QHash::<qh_lstr_t>::new_cached();

        source.insert(key("one"));

        let mut c_set: qh_lstr_t = source.into_c();
        let taken = unsafe { QHash::take_from_c_ptr(&raw mut c_set) };

        assert_eq!(sorted_str_keys(&taken), ["one"]);

        // `qhash_wipe()` forgets the size of the keys, so it cannot be used to empty the source.
        // The table left behind must still be a working table of the same kind.
        let left = unsafe { QHash::<qh_lstr_t>::from_c_ptr_mut(&raw mut c_set) };

        assert!(left.insert(key("two")));
        assert!(!left.insert(key("two")));
        assert_eq!(sorted_str_keys(left), ["two"]);

        // The memory pool and the hash caching are kept too.
        assert!(left.as_qhash().hdr.mp.is_null());
        assert_ne!(left.as_qhash().h_size, 0);

        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_set).cast::<qhash_t>());
        }
    }

    #[test]
    fn test_take_ownership_keeps_the_memory_pool() {
        let t_scope = TScope::new_scope();
        let mut source = QHash::<qh_u32_t>::t_new(&t_scope);

        source.insert(1);

        let mut c_set: qh_u32_t = source.into_c();
        let taken = unsafe { QHash::take_from_c_ptr(&raw mut c_set) };

        assert_eq!(sorted_keys(&taken), [1]);

        // The emptied table keeps the `t_pool`, so the C code can keep filling it there.
        let left = unsafe { QHash::<qh_u32_t>::from_c_ptr_mut(&raw mut c_set) };

        assert!(ptr::eq(left.as_qhash().hdr.mp, unsafe { t_pool() }));
        assert!(left.insert(9));
        assert_eq!(sorted_keys(left), [9]);
    }

    #[test]
    fn test_move_ownership_through_a_pointer() {
        // A C out parameter: an initialized, empty table.
        let mut c_set: qh_u32_t = QHash::<qh_u32_t>::new().into_c();
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(4);
        set.insert(5);

        let keys = set.keys_ptr();

        // Hand the table over without copying it.
        unsafe {
            set.move_into_c_ptr(&raw mut c_set);
        }

        {
            let moved = unsafe { QHash::<qh_u32_t>::from_c_ptr(&raw const c_set) };

            assert!(ptr::eq(moved.keys_ptr(), keys));
            assert_eq!(sorted_keys(moved), [4, 5]);
        }

        // The C code owns the table now, so it releases it.
        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_set).cast::<qhash_t>());
        }
    }

    #[test]
    fn test_ownership_round_trip_through_pointers() {
        let mut set = QHash::<qh_u32_t>::new();

        set.insert(1);
        set.insert(2);

        let keys = set.keys_ptr();
        let mut c_set: qh_u32_t = QHash::<qh_u32_t>::new().into_c();

        unsafe {
            set.move_into_c_ptr(&raw mut c_set);
        }

        let set = unsafe { QHash::take_from_c_ptr(&raw mut c_set) };

        // The buffers never moved, and the C table is empty again.
        assert!(ptr::eq(set.keys_ptr(), keys));
        assert_eq!(sorted_keys(&set), [1, 2]);
        assert!(unsafe { QHash::<qh_u32_t>::from_c_ptr(&raw const c_set) }.is_empty());
    }

    #[test]
    fn test_borrow_a_null_pointer() {
        let null: *mut qh_u32_t = ptr::null_mut();

        assert!(unsafe { QHash::<qh_u32_t>::from_c_ptr_opt(null.cast_const()) }.is_none());
        assert!(unsafe { QHash::<qh_u32_t>::from_c_ptr_mut_opt(null) }.is_none());
    }

    #[test]
    #[should_panic(expected = "from_c_ptr_mut called with NULL")]
    fn test_borrow_a_null_pointer_panics() {
        let null: *mut qh_u32_t = ptr::null_mut();

        let _set = unsafe { QHash::<qh_u32_t>::from_c_ptr_mut(null) };
    }

    #[test]
    fn test_ownership_round_trip_is_zero_copy() {
        let mut set = QHash::<qh_u32_t>::new();

        for i in 0..10 {
            set.insert(i);
        }

        let keys = set.keys_ptr();

        assert!(!keys.is_null());

        // Give the table to C: the entries are not copied, only the descriptor is moved.
        let c_set: qh_u32_t = set.into_c();

        // Borrow the C value to read it, rather than naming a field of the union: bindgen
        // generates the union differently depending on the crate.
        {
            let borrowed = unsafe { QHash::<qh_u32_t>::borrow_c(&c_set) };

            assert!(ptr::eq(borrowed.keys_ptr(), keys));
            assert_eq!(borrowed.len(), 10);
        }

        // Take it back: still the same buffers, and this table wipes them.
        let set = unsafe { QHash::<qh_u32_t>::from_c(c_set) };

        assert!(ptr::eq(set.keys_ptr(), keys));
        assert_eq!(set.len(), 10);
        assert_eq!(sorted_keys(&set), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
    }

    #[test]
    fn test_borrow_c_does_not_wipe() {
        let mut set = QHash::<qh_lstr_t>::new();

        set.insert(key("kept"));

        let keys = set.keys_ptr();
        let c_set = set.as_mut_ptr();

        {
            // Borrow the same table through the C type: no copy at all.
            let borrowed = unsafe { QHash::<qh_lstr_t>::borrow_c_mut(&mut *c_set) };

            assert!(ptr::eq(borrowed.keys_ptr(), keys));
            assert!(borrowed.insert(key("added")));
        }

        // Dropping the borrow must not wipe the table.
        assert_eq!(set.len(), 2);
        assert_eq!(sorted_str_keys(&set), ["added", "kept"]);
    }

    #[test]
    fn test_borrow_a_table_that_c_owns() {
        // Build a table the way the C code does, then borrow it without copying.
        let mut c_set: qh_u32_t = unsafe { mem::zeroed() };

        unsafe {
            <qh_u32_t as QHashType>::init(&raw mut c_set, false, ptr::null_mut());
        }

        {
            let set = unsafe { QHash::<qh_u32_t>::borrow_c_mut(&mut c_set) };

            for i in 0..4 {
                set.insert(i);
            }
            assert_eq!(sorted_keys(set), [0, 1, 2, 3]);
        }

        assert_eq!(unsafe { QHash::<qh_u32_t>::borrow_c(&c_set) }.len(), 4);

        // The C code still owns the table, so wipe it the C way.
        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_set).cast::<qhash_t>());
        }
    }

    // }}}
}

// }}}
