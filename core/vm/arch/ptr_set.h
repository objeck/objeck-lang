/***************************************************************************
 * An open-addressing set of object pointers, for the old generation.
 *
 * Copyright (c) 2025-2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __PTR_SET_H__
#define __PTR_SET_H__

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

/**
 * Why this exists rather than std::unordered_set<size_t*>.
 *
 * unordered_set is node-based: every insert is a separate heap allocation. The
 * old generation gains one entry per promoted object, so a minor collection
 * pays that allocation once per survivor -- on top of the object's own calloc.
 * Measured per promoted object on one Ryzen 9 7950X3D (#871):
 *
 *     unordered_set::insert    MSVC 239.5 ns    g++ 67.5 ns
 *     calloc                   MSVC  38.5 ns    g++  8.5 ns
 *
 * The container cost more than the allocation it accompanied, and promotion
 * cost tracks promoted bytes rather than collection count, so it dominated
 * minor GC on Windows: binarytrees promoted 125-160 MB and paid 33.8 ms/MB
 * against Linux's 9.9 ms/MB on the same machine.
 *
 * Here the keys live directly in one power-of-two array. An insert is a hash, a
 * probe and a store, and allocation happens only when the table grows.
 *
 * Why a set at all, when an object's header already carries GC_OLD_BIT: the bit
 * can only be read by dereferencing the pointer, and the collector tests words
 * taken from conservative scans (operand stacks, JIT temps) that may not be
 * pointers. Membership has to be decidable WITHOUT dereferencing the candidate,
 * which is exactly what a set of known-good addresses gives. See ForwardedAddr
 * in memory.h.
 *
 * Not thread-safe: every mutation is made under allocated_lock, and the reads
 * during a collection happen with all mutators parked at a safepoint.
 */
class PtrSet {
  // A live key is a real object address: 8-byte aligned, never null, and never
  // all-ones. Those two values are therefore free to use as slot markers.
  static size_t* Empty() { return nullptr; }
  static size_t* Tombstone() { return (size_t*)~(uintptr_t)0; }

  size_t** slots;
  size_t capacity;      // always a power of two
  size_t live;          // occupied slots
  size_t used;          // occupied + tombstones; drives growth

  static size_t Hash(size_t* key) {
    // Just the address, shifted past the three always-zero alignment bits. No
    // multiply, no mixing.
    //
    // Mixing was the first choice here and it was slower. Allocator addresses
    // arrive in rising runs, so an unmixed hash puts objects allocated near each
    // other in slots near each other, and a probe touches cache lines the last
    // probe already pulled in. Fibonacci hashing scatters them across the whole
    // table and pays a miss per lookup. Measured over 2M realistic addresses
    // (varied sizes, 16-byte aligned, occasional page gaps):
    //
    //                   insert     count
    //   unordered_set   78.1 ns    19.6 ns
    //   fibonacci        9.4 ns     8.2 ns
    //   shift            6.8 ns     4.0 ns
    //
    // The risk of not mixing is clustering when addresses are spaced at an exact
    // multiple of the table stride. That needs every object to be 8*capacity
    // bytes apart -- megabytes, for a table this size -- which no run of object
    // allocations produces. A regular 32-byte stride, the worst realistic case,
    // lands on every fourth slot and measures 1.6 ns / 1.2 ns.
    return (size_t)((uintptr_t)key >> 3);
  }

  void Allocate(size_t new_capacity) {
    slots = (size_t**)std::calloc(new_capacity, sizeof(size_t*));
    if(!slots) {
      throw std::bad_alloc();
    }
    capacity = new_capacity;
  }

  // Rehash into a table of 'new_capacity'. Tombstones are not carried over, so
  // this is also how a table that has been swept clean reclaims its probes.
  void Rehash(size_t new_capacity) {
    size_t** old_slots = slots;
    const size_t old_capacity = capacity;

    Allocate(new_capacity);       // throws before anything is disturbed
    live = used = 0;

    for(size_t i = 0; i < old_capacity; ++i) {
      size_t* key = old_slots[i];
      if(key != Empty() && key != Tombstone()) {
        InsertKnownAbsent(key);
      }
    }
    std::free(old_slots);
  }

  // Place a key that is known not to be present and known to fit.
  void InsertKnownAbsent(size_t* key) {
    const size_t mask = capacity - 1;
    size_t i = Hash(key) & mask;
    while(slots[i] != Empty() && slots[i] != Tombstone()) {
      i = (i + 1) & mask;
    }
    slots[i] = key;
    ++live;
    ++used;
  }

  // Grow when the table is 70% used. Counting tombstones in 'used' is what stops
  // a long insert/erase cycle from filling the table with them and turning every
  // probe into a full scan.
  void GrowIfNeeded() {
    if(used * 10 >= capacity * 7) {
      // Size to the LIVE count, not the used count: a table that is mostly
      // tombstones should shrink its probe chains, not double in size.
      size_t target = 16;
      while(target * 10 < (live + 1) * 20) {     // aim for a 50% load
        target <<= 1;
      }
      Rehash(target);
    }
  }

 public:
  class Iterator {
    size_t** slots;
    size_t capacity;
    size_t index;

    void SkipFree() {
      while(index < capacity && (slots[index] == Empty() || slots[index] == Tombstone())) {
        ++index;
      }
    }

   public:
    Iterator(size_t** s, size_t c, size_t i) : slots(s), capacity(c), index(i) {
      SkipFree();
    }

    size_t* operator*() const { return slots[index]; }
    bool operator==(const Iterator& other) const { return index == other.index; }
    bool operator!=(const Iterator& other) const { return index != other.index; }

    Iterator& operator++() {
      ++index;
      SkipFree();
      return *this;
    }

    size_t Index() const { return index; }
  };

  PtrSet() : slots(nullptr), capacity(0), live(0), used(0) {
    Allocate(16);
  }

  ~PtrSet() {
    std::free(slots);
    slots = nullptr;
  }

  PtrSet(const PtrSet&) = delete;
  PtrSet& operator=(const PtrSet&) = delete;

  size_t size() const { return live; }
  bool empty() const { return live == 0; }

  // Sized for the expected population rather than grown into it, so the early
  // promotions of a run do not each pay a rehash.
  void reserve(size_t count) {
    size_t target = 16;
    while(target * 7 < count * 10) {
      target <<= 1;
    }
    if(target > capacity) {
      Rehash(target);
    }
  }

  // 0 or 1, matching the std::unordered_set call sites this replaced. Safe to
  // call with a value that is not a pointer at all: nothing is dereferenced.
  size_t count(size_t* key) const {
    if(key == Empty() || key == Tombstone()) {
      return 0;
    }
    const size_t mask = capacity - 1;
    size_t i = Hash(key) & mask;
    // Terminates because the table is never full: GrowIfNeeded keeps at least
    // 30% of the slots Empty, and only an Empty slot ends the probe.
    while(slots[i] != Empty()) {
      if(slots[i] == key) {
        return 1;
      }
      i = (i + 1) & mask;
    }
    return 0;
  }

  void insert(size_t* key) {
    if(key == Empty() || key == Tombstone()) {
      return;                    // not a real object address
    }
    GrowIfNeeded();

    const size_t mask = capacity - 1;
    size_t i = Hash(key) & mask;
    size_t first_free = capacity;      // reuse the earliest tombstone seen
    while(slots[i] != Empty()) {
      if(slots[i] == key) {
        return;                  // already present
      }
      if(slots[i] == Tombstone() && first_free == capacity) {
        first_free = i;
      }
      i = (i + 1) & mask;
    }

    if(first_free != capacity) {
      slots[first_free] = key;   // filling a tombstone does not change 'used'
    }
    else {
      slots[i] = key;
      ++used;
    }
    ++live;
  }

  Iterator begin() { return Iterator(slots, capacity, 0); }
  Iterator end() { return Iterator(slots, capacity, capacity); }

  // Erase at an iterator and return one positioned at the next live key, so the
  // sweep can write `iter = set.erase(iter)` exactly as it did with the
  // std::unordered_set this replaced.
  Iterator erase(const Iterator& iter) {
    const size_t index = iter.Index();
    slots[index] = Tombstone();      // 'used' stays: the probe chain is intact
    --live;
    return Iterator(slots, capacity, index + 1);
  }

  void clear() {
    for(size_t i = 0; i < capacity; ++i) {
      slots[i] = Empty();
    }
    live = used = 0;
  }
};

#endif
