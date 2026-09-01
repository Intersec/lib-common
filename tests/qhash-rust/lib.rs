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

//! Tests of the map side of [`libcommon::qhashmap`].
//!
//! They live in a crate of their own because a `QMap` needs a C `qm_t` type, and no header that
//! `libcommon-core` binds declares one: the nine tables it sees are all sets. The maps come from
//! `libcommon`, which this crate depends on.
//!
//! `qm_iop_struct_t` is used below: its keys are strings, which exercises a real hash and equality
//! function, and its values are pointers that the table only stores.

#[cfg(test)]
#[allow(clippy::redundant_test_prefix)]
mod tests {
    use std::cell::Cell;
    use std::mem;
    use std::ptr;

    use libcommon::bindings::{
        iop_struct_t, lstr_t, qh_lstr_t, qhash_t, qhash_wipe, qm_iop_struct_t,
    };
    use libcommon::lstr::{from_raw_utf8, from_str};
    use libcommon::mem_stack::{TCollect as _, TScope};
    use libcommon::qhash::{QEntryWipe, QHashType};
    use libcommon::qhashmap::{Entry, QMap};
    use libcommon::qhashset::QHash;
    use libcommon::qvector::QVector;

    // {{{ Test helpers

    /// The map under test.
    type Map<'a> = QMap<'a, qm_iop_struct_t>;

    /// Build the `lstr_t` of a static string.
    fn key(s: &'static str) -> lstr_t {
        from_str(s).as_raw()
    }

    /// Build a value that the map only stores, and never reads through.
    fn value(address: usize) -> *const iop_struct_t {
        ptr::without_provenance(address)
    }

    /// Get the address that a stored value holds.
    fn addr_of(value: *const iop_struct_t) -> usize {
        value.addr()
    }

    /// Collect the entries of a map, sorted by key.
    fn sorted_entries(map: &Map<'_>) -> Vec<(&'static str, usize)> {
        // The keys are built from string literals, so they live as long as the program.
        let mut entries: Vec<(&'static str, usize)> = map
            .iter()
            .map(|(key, value)| (unsafe { from_raw_utf8(*key).as_str() }, addr_of(*value)))
            .collect();

        entries.sort_unstable();
        entries
    }

    // }}}
    // {{{ Basic operations

    #[test]
    fn test_new_is_empty() {
        let map = Map::new();

        assert!(map.is_empty());
        assert_eq!(map.len(), 0);
        assert!(map.get(&key("a")).is_none());
        assert_eq!(map.iter().count(), 0);
    }

    #[test]
    fn test_insert_returns_the_previous_value() {
        let mut map = Map::new();

        assert!(map.insert(key("a"), value(1)).is_none());
        assert!(map.insert(key("b"), value(2)).is_none());
        assert_eq!(map.len(), 2);

        // Inserting an existing key overwrites it and gives back the previous value.
        let previous = map.insert(key("a"), value(10));

        assert_eq!(previous.map(addr_of), Some(1));
        assert_eq!(map.len(), 2);
        assert_eq!(sorted_entries(&map), [("a", 10), ("b", 2)]);
    }

    #[test]
    fn test_try_insert_keeps_the_existing_entry() {
        let mut map = Map::new();

        assert!(map.try_insert(key("a"), value(1)).is_ok());

        // The key is already there, so nothing is written: the error gives the refused key and
        // value back, with the entry that refused them.
        let Err(error) = map.try_insert(key("a"), value(99)) else {
            panic!("the key must be refused");
        };

        assert_eq!(unsafe { from_raw_utf8(error.key).as_str() }, "a");
        assert_eq!(addr_of(error.value), 99);
        assert_eq!(addr_of(*error.entry.get()), 1);

        assert_eq!(map.len(), 1);
        assert_eq!(sorted_entries(&map), [("a", 1)]);
    }

    #[test]
    fn test_get_and_contains() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        assert!(map.contains_key(&key("a")));
        assert!(!map.contains_key(&key("b")));
        assert_eq!(map.get(&key("a")).copied().map(addr_of), Some(1));
        assert!(map.get(&key("b")).is_none());
    }

    #[test]
    fn test_get_mut() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let Some(slot) = map.get_mut(&key("a")) else {
            panic!("the key must be there");
        };

        *slot = value(42);
        assert_eq!(map.get(&key("a")).copied().map(addr_of), Some(42));
        assert!(map.get_mut(&key("b")).is_none());
    }

    #[test]
    fn test_remove_returns_the_value() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        assert_eq!(map.remove(&key("a")).map(addr_of), Some(1));
        assert!(map.remove(&key("a")).is_none());
        assert_eq!(map.len(), 1);
        assert_eq!(sorted_entries(&map), [("b", 2)]);
    }

    #[test]
    fn test_get_key_value_returns_the_stored_key() {
        let mut map = Map::new();
        let stored = key("a");

        map.insert(stored, value(1));

        // The key given here is a different `lstr_t` with the same content: the stored key comes
        // back with the value.
        let Some((found, found_value)) = map.get_key_value(&key("a")) else {
            panic!("the key must be there");
        };

        assert_eq!(addr_of(*found_value), 1);

        let found = unsafe { from_raw_utf8(*found).as_str() };
        let stored = unsafe { from_raw_utf8(stored).as_str() };

        assert!(ptr::eq(found.as_ptr(), stored.as_ptr()));
        assert!(map.get_key_value(&key("b")).is_none());
    }

    #[test]
    fn test_remove_entry_hands_the_stored_key_over() {
        let mut map = Map::new();
        let stored = key("a");

        map.insert(stored, value(1));

        let Some((removed, removed_value)) = map.remove_entry(&key("a")) else {
            panic!("the key must be there");
        };

        assert_eq!(addr_of(removed_value), 1);

        let removed = unsafe { from_raw_utf8(removed).as_str() };
        let stored = unsafe { from_raw_utf8(stored).as_str() };

        assert!(ptr::eq(removed.as_ptr(), stored.as_ptr()));
        assert!(map.is_empty());
        assert!(map.remove_entry(&key("a")).is_none());
    }

    #[test]
    fn test_get_disjoint_mut() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        let [a, missing, b] = map.get_disjoint_mut([&key("a"), &key("z"), &key("b")]);

        assert!(missing.is_none());

        let (Some(a), Some(b)) = (a, b) else {
            panic!("the keys must be there");
        };

        // Both values are mutable at the same time.
        *a = value(10);
        *b = value(20);
        assert_eq!(sorted_entries(&map), [("a", 10), ("b", 20)]);
    }

    #[test]
    #[should_panic(expected = "the keys must be disjoint")]
    fn test_get_disjoint_mut_refuses_equal_keys() {
        // The test aborts inside the panic, so nothing is dropped: the map borrows the `t_pool`
        // rather than libc, and the leak checker stays quiet.
        let t_scope = TScope::new_scope();
        let mut map = Map::t_new(&t_scope);

        map.insert(key("a"), value(1));

        // Two equal keys would give two mutable references to one value.
        map.get_disjoint_mut([&key("a"), &key("a")]);
    }

    #[test]
    fn test_clear() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));
        map.insert(key("c"), value(3));
        // A key that is already there replaces its value, it does not add an entry.
        map.insert(key("a"), value(4));
        assert_eq!(map.len(), 3);

        map.clear();

        assert!(map.is_empty());

        // The map is still usable after a clear.
        map.insert(key("z"), value(7));
        assert_eq!(sorted_entries(&map), [("z", 7)]);
    }

    #[test]
    fn test_many_entries_resizes() {
        // The names must outlive the map: the stored keys point into them.
        let names: Vec<String> = (0..1_000).map(|i| format!("key-{i}")).collect();
        let mut map = Map::new();

        for (i, name) in names.iter().enumerate() {
            assert!(map.try_insert(from_str(name).as_raw(), value(i)).is_ok());
        }

        assert_eq!(map.len(), names.len());
        for (i, name) in names.iter().enumerate() {
            let found = map.get(&from_str(name).as_raw());

            assert_eq!(found.copied().map(addr_of), Some(i));
        }
    }

    // }}}
    // {{{ Entry

    #[test]
    fn test_entry_or_insert() {
        let mut map = Map::new();

        // A vacant entry inserts the given value.
        assert_eq!(addr_of(*map.entry(key("a")).or_insert(value(1))), 1);

        // An occupied entry keeps the stored value, and the reference can change it.
        *map.entry(key("a")).or_insert(value(9)) = value(2);

        assert_eq!(map.len(), 1);
        assert_eq!(sorted_entries(&map), [("a", 2)]);
    }

    #[test]
    fn test_entry_or_insert_with_key() {
        let mut map = Map::new();

        let inserted = map
            .entry(key("abc"))
            .or_insert_with_key(|key| value(unsafe { from_raw_utf8(*key).as_str().len() }));

        assert_eq!(addr_of(*inserted), 3);
    }

    #[test]
    fn test_entry_and_modify() {
        let mut map = Map::new();

        map.entry(key("a"))
            .and_modify(|slot| *slot = value(9))
            .or_insert(value(1));
        assert_eq!(sorted_entries(&map), [("a", 1)]);

        map.entry(key("a"))
            .and_modify(|slot| *slot = value(9))
            .or_insert(value(1));
        assert_eq!(sorted_entries(&map), [("a", 9)]);
    }

    #[test]
    fn test_entry_key() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        // The occupied entry names the stored key, the vacant entry the key it owns.
        for name in ["a", "b"] {
            let entry = map.entry(key(name));

            assert_eq!(unsafe { from_raw_utf8(*entry.key()).as_str() }, name);
        }
    }

    #[test]
    fn test_occupied_entry() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let Entry::Occupied(mut entry) = map.entry(key("a")) else {
            panic!("the entry must be occupied");
        };

        assert_eq!(addr_of(*entry.get()), 1);

        // `insert` gives the previous value back.
        assert_eq!(addr_of(entry.insert(value(2))), 1);

        *entry.get_mut() = value(3);

        // The reference of `into_mut` outlives the entry.
        let slot = entry.into_mut();

        *slot = value(addr_of(*slot) + 1);
        assert_eq!(sorted_entries(&map), [("a", 4)]);
    }

    #[test]
    fn test_occupied_entry_remove() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let Entry::Occupied(entry) = map.entry(key("a")) else {
            panic!("the entry must be occupied");
        };

        assert_eq!(addr_of(entry.remove()), 1);
        assert!(map.is_empty());
    }

    #[test]
    fn test_occupied_entry_remove_entry() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let Entry::Occupied(entry) = map.entry(key("a")) else {
            panic!("the entry must be occupied");
        };

        let (stored, previous) = entry.remove_entry();

        assert_eq!(unsafe { from_raw_utf8(stored).as_str() }, "a");
        assert_eq!(addr_of(previous), 1);
        assert!(map.is_empty());
    }

    #[test]
    fn test_vacant_entry_into_key() {
        let mut map = Map::new();

        let Entry::Vacant(entry) = map.entry(key("a")) else {
            panic!("the entry must be vacant");
        };

        // The entry gives the key back, and inserts nothing.
        assert_eq!(unsafe { from_raw_utf8(entry.into_key()).as_str() }, "a");
        assert!(map.is_empty());
    }

    // }}}
    // {{{ Iteration

    #[test]
    fn test_iterate() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        assert_eq!(sorted_entries(&map), [("a", 1), ("b", 2)]);

        // Keys and values can be iterated on their own.
        let mut keys: Vec<&str> = map
            .keys()
            .map(|key| unsafe { from_raw_utf8(*key).as_str() })
            .collect();

        keys.sort_unstable();
        assert_eq!(keys, ["a", "b"]);

        let mut values: Vec<usize> = map.values().copied().map(addr_of).collect();

        values.sort_unstable();
        assert_eq!(values, [1, 2]);
    }

    #[test]
    fn test_iterate_mutably() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        for slot in map.values_mut() {
            *slot = value(addr_of(*slot) * 10);
        }

        assert_eq!(sorted_entries(&map), [("a", 10), ("b", 20)]);

        // `IntoIterator` on a mutable reference gives the entries.
        for (_, slot) in &mut map {
            *slot = value(addr_of(*slot) + 1);
        }

        assert_eq!(sorted_entries(&map), [("a", 11), ("b", 21)]);
    }

    #[test]
    fn test_iterate_by_reference() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let total: usize = (&map).into_iter().map(|(_, value)| addr_of(*value)).sum();

        assert_eq!(total, 1);
    }

    // }}}
    // {{{ Allocators

    #[test]
    fn test_t_pool() {
        let t_scope = TScope::new_scope();
        let mut map = Map::t_new(&t_scope);

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        assert_eq!(sorted_entries(&map), [("a", 1), ("b", 2)]);
    }

    #[test]
    fn test_t_with_capacity() {
        let t_scope = TScope::new_scope();
        let mut map = Map::t_with_capacity(&t_scope, 64);

        map.insert(key("a"), value(1));
        assert_eq!(map.len(), 1);
    }

    // }}}
    // {{{ Entry ownership

    /// A map whose entries own something, so that the destructor can be observed.
    type CountingMap<'a> = QMap<'a, qm_iop_struct_t, CountingWipe>;

    // Number of keys and of values that `CountingWipe` released, for the running test. The test
    // harness gives every test its own thread, so these counters are per test.
    thread_local! {
        static WIPED_KEYS: Cell<usize> = const { Cell::new(0) };
        static WIPED_VALUES: Cell<usize> = const { Cell::new(0) };
    }

    /// Entry destructor that counts what it releases.
    struct CountingWipe;

    impl QEntryWipe<qm_iop_struct_t> for CountingWipe {
        fn wipe_key(_key: &mut lstr_t) {
            WIPED_KEYS.set(WIPED_KEYS.get() + 1);
        }

        fn wipe_value(_value: &mut *const iop_struct_t) {
            WIPED_VALUES.set(WIPED_VALUES.get() + 1);
        }
    }

    #[test]
    fn test_drop_releases_the_entries() {
        {
            let mut map = CountingMap::new();

            map.insert(key("a"), value(1));
            map.insert(key("b"), value(2));
            assert_eq!(WIPED_KEYS.get(), 0);
        }

        assert_eq!(WIPED_KEYS.replace(0), 2);
        assert_eq!(WIPED_VALUES.replace(0), 2);
    }

    #[test]
    fn test_clear_releases_the_entries() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        map.clear();

        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 1);
        assert!(map.is_empty());
    }

    #[test]
    fn test_remove_releases_the_key_but_hands_the_value_over() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));

        assert_eq!(map.remove(&key("a")).map(addr_of), Some(1));
        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 0);
    }

    #[test]
    fn test_insert_releases_the_given_key() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        assert_eq!(WIPED_KEYS.replace(0), 0);

        // The map keeps the stored key and releases the given one, like `HashMap::insert` drops
        // it. The previous value is returned, so it is not released.
        assert_eq!(map.insert(key("a"), value(2)).map(addr_of), Some(1));
        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 0);
    }

    #[test]
    fn test_try_insert_releases_nothing() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));

        // The refused key and value are given back: the caller keeps what they own.
        let Err(error) = map.try_insert(key("a"), value(2)) else {
            panic!("the key must be refused");
        };

        assert_eq!(addr_of(error.value), 2);
        assert_eq!(WIPED_KEYS.replace(0), 0);
        assert_eq!(WIPED_VALUES.replace(0), 0);
    }

    #[test]
    fn test_entry_releases_the_given_key_when_occupied() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        assert_eq!(WIPED_KEYS.replace(0), 0);

        // The map keeps the stored key, so the entry releases the given key at once.
        map.entry(key("a")).or_insert(value(9));
        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 0);
    }

    #[test]
    fn test_vacant_entry_releases_its_key_on_drop() {
        let mut map = CountingMap::new();

        // The vacant entry owns the key: dropping it without an insertion releases the key.
        drop(map.entry(key("a")));

        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert!(map.is_empty());
    }

    #[test]
    fn test_a_map_without_a_destructor_releases_nothing() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.clear();

        assert_eq!(WIPED_KEYS.replace(0), 0);
        assert_eq!(WIPED_VALUES.replace(0), 0);
    }

    // }}}
    // {{{ Bulk removal

    #[test]
    fn test_retain() {
        let mut map = CountingMap::new();

        for (i, name) in ["a", "b", "c", "d"].iter().enumerate() {
            map.insert(key(name), value(i));
        }
        map.retain(|_, slot| addr_of(*slot).is_multiple_of(2));

        // The removed entries are released, the keys and the values alike.
        assert_eq!(WIPED_KEYS.replace(0), 2);
        assert_eq!(WIPED_VALUES.replace(0), 2);
        assert_eq!(sorted_entries(map.with_wipe()), [("a", 0), ("c", 2)]);
    }

    #[test]
    fn test_drain_hands_the_entries_over() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        // The keys are built from string literals, so they live as long as the program.
        let mut drained: Vec<(&str, usize)> = map
            .drain()
            .map(|(key, value)| (unsafe { from_raw_utf8(key).as_str() }, addr_of(value)))
            .collect();

        drained.sort_unstable();
        assert_eq!(drained, [("a", 1), ("b", 2)]);

        // The caller takes the yielded entries over, so nothing is released.
        assert_eq!(WIPED_KEYS.replace(0), 0);
        assert_eq!(WIPED_VALUES.replace(0), 0);
        assert!(map.is_empty());

        // The map is still usable after a drain.
        map.insert(key("z"), value(7));
        assert_eq!(map.len(), 1);
    }

    #[test]
    fn test_drain_releases_the_entries_it_did_not_yield() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        {
            let mut drain = map.drain();

            assert!(drain.next().is_some());
        }

        // The entry that was not yielded is released, like `clear()` does.
        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 1);
        assert!(map.is_empty());
    }

    #[test]
    fn test_extract_if() {
        let mut map = CountingMap::new();

        for (i, name) in ["a", "b", "c", "d"].iter().enumerate() {
            map.insert(key(name), value(i));
        }

        let extracted: Vec<usize> = map
            .extract_if(|_, slot| addr_of(*slot) % 2 == 1)
            .map(|(_, value)| addr_of(value))
            .collect();

        assert_eq!(extracted.len(), 2);

        // The yielded entries are handed over, and the kept entries are not touched.
        assert_eq!(WIPED_KEYS.replace(0), 0);
        assert_eq!(WIPED_VALUES.replace(0), 0);
        assert_eq!(sorted_entries(map.with_wipe()), [("a", 0), ("c", 2)]);
    }

    #[test]
    fn test_extract_if_is_lazy() {
        let mut map = Map::new();

        for (i, name) in ["a", "b", "c", "d"].iter().enumerate() {
            map.insert(key(name), value(i));
        }

        // Dropping the iterator early keeps the entries it did not visit.
        assert!(map.extract_if(|_, _| true).next().is_some());

        assert_eq!(map.len(), 3);
    }

    // }}}
    // {{{ Owning iteration

    #[test]
    fn test_into_keys_releases_the_values() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        // The keys are built from string literals, so they live as long as the program.
        let mut keys: Vec<&str> = map
            .into_keys()
            .map(|key| unsafe { from_raw_utf8(key).as_str() })
            .collect();

        keys.sort_unstable();
        assert_eq!(keys, ["a", "b"]);

        // The caller takes the keys over; the values are released.
        assert_eq!(WIPED_KEYS.replace(0), 0);
        assert_eq!(WIPED_VALUES.replace(0), 2);
    }

    #[test]
    fn test_into_values_releases_the_keys() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        let mut values: Vec<usize> = map.into_values().map(addr_of).collect();

        values.sort_unstable();
        assert_eq!(values, [1, 2]);

        // The caller takes the values over; the stored keys are released.
        assert_eq!(WIPED_KEYS.replace(0), 2);
        assert_eq!(WIPED_VALUES.replace(0), 0);
    }

    #[test]
    fn test_into_iter_hands_the_entries_over() {
        let mut map = CountingMap::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        let mut iter = map.into_iter();

        assert!(iter.next().is_some());
        drop(iter);

        // The yielded entry is handed over; the map is dropped with the iterator and releases
        // the other one.
        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 1);
    }

    // }}}
    // {{{ Conversions

    // In C, converting between a `qv_t`, a `qh_t` and a `qm_t` needs a hand-written loop per
    // pair of types. The iterators make every conversion one `collect()`.

    #[test]
    fn test_collect_a_vector_into_a_map_and_back() {
        let pairs: QVector<'_, (lstr_t, usize)> = [("a", 1), ("b", 2), ("a", 3)]
            .into_iter()
            .map(|(name, address)| (key(name), address))
            .collect();

        let map: Map<'_> = pairs
            .iter()
            .map(|(name, address)| (*name, value(*address)))
            .collect();

        // The last value of a duplicate key wins, like the standard `HashMap`.
        assert_eq!(sorted_entries(&map), [("a", 3), ("b", 2)]);

        // And the entries collect back into a vector.
        let mut back: QVector<'_, (&str, usize)> = map
            .iter()
            .map(|(name, address)| (unsafe { from_raw_utf8(*name).as_str() }, addr_of(*address)))
            .collect();

        back.sort_unstable();
        assert_eq!(back, [("a", 3), ("b", 2)]);
    }

    #[test]
    fn test_collect_a_map_into_a_set_and_back() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("bc"), value(2));

        // The keys of the map collect into a set...
        let set: QHash<'_, qh_lstr_t> = map.keys().copied().collect();

        assert_eq!(set.len(), 2);
        assert!(set.contains(&key("a")));
        assert!(set.contains(&key("bc")));

        // ... and the set collects back into a map, with values computed from the keys.
        let back: Map<'_> = set
            .iter()
            .map(|name| (*name, value(unsafe { from_raw_utf8(*name).as_str().len() })))
            .collect();

        assert_eq!(sorted_entries(&back), [("a", 1), ("bc", 2)]);
    }

    #[test]
    fn test_t_collect_a_map() {
        let t_scope = TScope::new_scope();

        // The map lives on the `t_pool` of the scope, like a `Map::t_new` one.
        let map: QMap<'_, qm_iop_struct_t> = [("a", 1), ("b", 2)]
            .into_iter()
            .map(|(name, address)| (key(name), value(address)))
            .t_collect(&t_scope);

        assert_eq!(sorted_entries(&map), [("a", 1), ("b", 2)]);
    }

    #[test]
    fn test_extend_releases_the_replaced_values() {
        let mut map = CountingMap::new();

        map.extend([(key("a"), value(1)), (key("a"), value(2))]);

        // The duplicate given key and the replaced value are released: nobody can take them over.
        assert_eq!(WIPED_KEYS.replace(0), 1);
        assert_eq!(WIPED_VALUES.replace(0), 1);
        assert_eq!(sorted_entries(map.with_wipe()), [("a", 2)]);
    }

    // }}}
    // {{{ C interoperability

    /// Insert an entry the way a C function does: through a pointer.
    ///
    /// # Safety
    ///
    /// `qh` must point to an initialized C table.
    unsafe extern "C" fn insert_like_c(
        qh: *mut qm_iop_struct_t,
        name: *const lstr_t,
        value: *const iop_struct_t,
    ) {
        // The table type comes from the pointer: no type has to be named here.
        let map: &mut Map<'_> = unsafe { QMap::from_c_ptr_mut(qh) };

        map.insert(unsafe { *name }, value);
    }

    /// Look an entry up the way a C function does: through a const pointer.
    ///
    /// # Safety
    ///
    /// `qh` must point to an initialized C table.
    unsafe extern "C" fn get_like_c(
        qh: *const qm_iop_struct_t,
        name: *const lstr_t,
    ) -> *const iop_struct_t {
        let map: &Map<'_> = unsafe { QMap::from_c_ptr(qh) };

        map.get(unsafe { &*name }).copied().unwrap_or(ptr::null())
    }

    #[test]
    fn test_borrow_through_a_pointer() {
        let mut map = Map::new();
        let name = key("a");

        // A C prototype takes a pointer, which is what the conversions take.
        unsafe {
            insert_like_c(map.as_mut_ptr(), &raw const name, value(7));
        }

        assert_eq!(sorted_entries(&map), [("a", 7)]);
        assert_eq!(
            addr_of(unsafe { get_like_c(map.as_ptr(), &raw const name) }),
            7
        );

        let missing = key("b");

        assert!(unsafe { get_like_c(map.as_ptr(), &raw const missing) }.is_null());
    }

    #[test]
    fn test_borrow_through_a_pointer_is_zero_copy() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let keys = map.keys_ptr();
        let values = map.values_ptr();
        let qh = map.as_mut_ptr();

        {
            let borrowed: &mut Map<'_> = unsafe { QMap::from_c_ptr_mut(qh) };

            assert!(ptr::eq(borrowed.keys_ptr(), keys));
            assert!(ptr::eq(borrowed.values_ptr(), values));
            borrowed.insert(key("b"), value(2));
        }

        // Dropping the borrow must not wipe the table.
        assert_eq!(sorted_entries(&map), [("a", 1), ("b", 2)]);
        assert!(ptr::eq(map.keys_ptr(), keys));
    }

    #[test]
    fn test_take_ownership_through_a_pointer() {
        let mut source = Map::new();

        source.insert(key("a"), value(1));
        source.insert(key("b"), value(2));

        let keys = source.keys_ptr();
        let values = source.values_ptr();
        let mut c_map: qm_iop_struct_t = source.into_c();

        // Take the map over without copying it.
        let taken = unsafe { QMap::take_from_c_ptr(&raw mut c_map) };

        assert!(ptr::eq(taken.keys_ptr(), keys));
        assert!(ptr::eq(taken.values_ptr(), values));
        assert_eq!(sorted_entries(&taken), [("a", 1), ("b", 2)]);

        // The C map is left empty, and still usable: the values need their size back, which
        // `qhash_wipe()` would have dropped.
        {
            let left: &mut Map<'_> = unsafe { QMap::from_c_ptr_mut(&raw mut c_map) };

            assert!(left.is_empty());
            assert!(left.insert(key("c"), value(3)).is_none());
            assert_eq!(sorted_entries(left), [("c", 3)]);
        }

        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_map).cast::<qhash_t>());
        }

        assert_eq!(sorted_entries(&taken), [("a", 1), ("b", 2)]);
    }

    #[test]
    fn test_move_ownership_through_a_pointer() {
        // A C out parameter: an initialized, empty map.
        let mut c_map: qm_iop_struct_t = Map::new().into_c();
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let keys = map.keys_ptr();

        // Hand the map over without copying it.
        unsafe {
            map.move_into_c_ptr(&raw mut c_map);
        }

        {
            let moved: &Map<'_> = unsafe { QMap::from_c_ptr(&raw const c_map) };

            assert!(ptr::eq(moved.keys_ptr(), keys));
            assert_eq!(sorted_entries(moved), [("a", 1)]);
        }

        // The C code owns the map now, so it releases it.
        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_map).cast::<qhash_t>());
        }
    }

    #[test]
    fn test_ownership_round_trip_through_pointers() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));

        let keys = map.keys_ptr();
        let mut c_map: qm_iop_struct_t = Map::new().into_c();

        unsafe {
            map.move_into_c_ptr(&raw mut c_map);
        }

        let map = unsafe { QMap::take_from_c_ptr(&raw mut c_map) };

        // The buffers never moved, and the C map is empty again.
        assert!(ptr::eq(map.keys_ptr(), keys));
        assert_eq!(sorted_entries(&map), [("a", 1)]);
        let left: &Map<'_> = unsafe { QMap::from_c_ptr(&raw const c_map) };

        assert!(left.is_empty());
    }

    #[test]
    fn test_borrow_a_null_pointer() {
        let null: *mut qm_iop_struct_t = ptr::null_mut();

        assert!(unsafe { Map::from_c_ptr_opt(null.cast_const()) }.is_none());
        assert!(unsafe { Map::from_c_ptr_mut_opt(null) }.is_none());
    }

    #[test]
    fn test_ownership_round_trip_is_zero_copy() {
        let mut map = Map::new();

        map.insert(key("a"), value(1));
        map.insert(key("b"), value(2));

        let keys = map.keys_ptr();
        let values = map.values_ptr();

        assert!(!keys.is_null());
        assert!(!values.is_null());

        // Give the table to C: the entries are not copied, only the descriptor is moved.
        let c_map: qm_iop_struct_t = map.into_c();

        // Borrow the C value to read it, rather than naming a field of the union: bindgen
        // generates the union differently depending on the crate.
        {
            let borrowed = unsafe { Map::borrow_c(&c_map) };

            assert!(ptr::eq(borrowed.keys_ptr(), keys));
            assert!(ptr::eq(borrowed.values_ptr(), values));
            assert_eq!(borrowed.len(), 2);
        }

        // Take it back: still the same buffers, and this map wipes them.
        let map = unsafe { Map::from_c(c_map) };

        assert!(ptr::eq(map.keys_ptr(), keys));
        assert!(ptr::eq(map.values_ptr(), values));
        assert_eq!(sorted_entries(&map), [("a", 1), ("b", 2)]);
    }

    #[test]
    fn test_borrow_a_table_that_c_owns() {
        // Build a map the way the C code does, then use it without copying anything.
        let mut c_map: qm_iop_struct_t = unsafe { mem::zeroed() };

        unsafe {
            <qm_iop_struct_t as QHashType>::init(&raw mut c_map, false, ptr::null_mut());
        }

        {
            let map = unsafe { Map::borrow_c_mut(&mut c_map) };

            map.insert(key("a"), value(1));
            map.insert(key("b"), value(2));
            assert_eq!(sorted_entries(map), [("a", 1), ("b", 2)]);
        }

        // Dropping the borrow must not wipe the table.
        assert_eq!(unsafe { Map::borrow_c(&c_map) }.len(), 2);

        {
            let map = unsafe { Map::borrow_c(&c_map) };

            assert_eq!(map.get(&key("a")).copied().map(addr_of), Some(1));
        }

        // The C code still owns the table, so wipe it the C way.
        unsafe {
            qhash_wipe(ptr::from_mut(&mut c_map).cast::<qhash_t>());
        }
    }

    // }}}
}
