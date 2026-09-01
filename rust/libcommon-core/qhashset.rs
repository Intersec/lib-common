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
// {{{ Tests

#[cfg(test)]
#[allow(clippy::redundant_test_prefix)]
mod tests {
    use std::cell::Cell;
    use std::mem;

    use super::*;
    use crate::bindings::{lstr_t, qh_lstr_t, qh_u32_t};
    use crate::lstr::{from_raw_utf8, from_str};

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
        assert!(!set.contains_key(&1));
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
        assert!(set.contains_key(&1));
        assert!(set.contains_key(&2));
        assert!(!set.contains_key(&3));
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
        assert!(!set.contains_key(&1));
        assert!(set.contains_key(&2));
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
        assert!(!set.contains_key(&0));

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
            assert!(set.contains_key(&i));
        }
        assert!(!set.contains_key(&10_000));
    }

    #[test]
    fn test_with_capacity_and_reserve() {
        let mut set = QHash::<qh_u32_t>::with_capacity(128);

        assert!(set.is_empty());
        set.insert(1);
        assert_eq!(set.len(), 1);

        set.reserve(4096);
        assert!(set.contains_key(&1));
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
    // {{{ Keys that need a hash function

    #[test]
    fn test_string_keys() {
        let mut set = QHash::<qh_lstr_t>::new();

        assert!(set.insert(key("one")));
        assert!(set.insert(key("two")));

        // Equal keys are equal by content, not by pointer: the string is rebuilt here.
        assert!(!set.insert(key("one")));

        assert_eq!(set.len(), 2);
        assert!(set.contains_key(&key("two")));
        assert!(!set.contains_key(&key("three")));
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
        assert!(set.contains_key(&999));
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
        assert!(set.contains_key(&50));

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
