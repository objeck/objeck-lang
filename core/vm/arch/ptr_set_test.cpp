/***************************************************************************
 * Unit tests for PtrSet, the old generation's membership structure.
 *
 * Build and run:
 *   g++ -O2 -std=c++17 -Wall -o ptr_set_test ptr_set_test.cpp && ./ptr_set_test
 *
 * This is the collector's membership test, so a fault here is a use-after-free
 * or a lost object rather than a wrong answer. Each case below is a property
 * the collector actually relies on -- see the comment on each.
 ***************************************************************************/

#include "ptr_set.h"

#include <cstdio>
#include <set>
#include <vector>

static int failures = 0;

static void check(const char* what, bool cond) {
  if(cond) {
    std::printf("  [PASS] %s\n", what);
  }
  else {
    std::printf("  [FAIL] %s\n", what);
    ++failures;
  }
}

// Object addresses: 8-byte aligned, non-null, not all-ones.
static size_t* Addr(uintptr_t n) {
  return (size_t*)((n + 1) * 8);
}

int main() {
  std::printf("\nbasic insert / count / size:\n");
  {
    PtrSet set;
    check("a fresh set is empty", set.size() == 0 && set.empty());
    set.insert(Addr(1));
    set.insert(Addr(2));
    check("size counts inserts", set.size() == 2);
    check("a member is found", set.count(Addr(1)) == 1);
    check("a non-member is not", set.count(Addr(99)) == 0);

    // The collector inserts the same object again when a major collection
    // re-promotes survivors; a duplicate must not double-count or the sweep's
    // size accounting drifts.
    set.insert(Addr(1));
    check("a duplicate insert is a no-op", set.size() == 2);
  }

  std::printf("\ncount() never dereferences its argument:\n");
  {
    // ForwardedAddr passes words taken from conservative scans -- operand
    // stacks, JIT temps -- which may not be pointers at all. If count() ever
    // dereferenced the candidate this would segfault.
    PtrSet set;
    set.insert(Addr(1));
    check("an unmapped address is simply absent", set.count((size_t*)0xdeadbeefULL) == 0);
    check("null is absent", set.count(nullptr) == 0);
    check("all-ones is absent", set.count((size_t*)~(uintptr_t)0) == 0);
    check("a misaligned value is absent", set.count((size_t*)0x12345ULL) == 0);
  }

  std::printf("\ngrowth preserves every key:\n");
  {
    // Promotion inserts thousands of objects per collection; a rehash that drops
    // one loses an object the sweep will then free while it is still reachable.
    PtrSet set;
    std::vector<size_t*> keys;
    for(uintptr_t i = 0; i < 5000; ++i) {
      keys.push_back(Addr(i));
      set.insert(keys.back());
    }
    check("size after 5000 inserts", set.size() == 5000);

    bool all_present = true;
    for(size_t i = 0; i < keys.size(); ++i) {
      if(set.count(keys[i]) != 1) {
        all_present = false;
        break;
      }
    }
    check("every key survived the rehashes", all_present);

    bool none_extra = true;
    for(uintptr_t i = 5000; i < 5200; ++i) {
      if(set.count(Addr(i)) != 0) {
        none_extra = false;
        break;
      }
    }
    check("no key appeared that was never inserted", none_extra);
  }

  std::printf("\niteration visits exactly the live keys:\n");
  {
    // The major sweep walks the set to find unmarked objects. A missed entry is
    // a leak; a repeated one is a double free.
    PtrSet set;
    std::set<size_t*> expected;
    for(uintptr_t i = 0; i < 500; ++i) {
      set.insert(Addr(i));
      expected.insert(Addr(i));
    }

    std::set<size_t*> seen;
    size_t visits = 0;
    for(PtrSet::Iterator iter = set.begin(); iter != set.end(); ++iter) {
      seen.insert(*iter);
      ++visits;
    }
    check("visited each key once", visits == 500);
    check("visited exactly the inserted keys", seen == expected);
  }

  std::printf("\nerase during iteration (the sweep's pattern):\n");
  {
    // CollectMemory writes `iter = old_generation.erase(iter)` while walking.
    // An erase that invalidated the walk would skip or revisit objects.
    PtrSet set;
    for(uintptr_t i = 0; i < 1000; ++i) {
      set.insert(Addr(i));
    }

    // drop every key at an even index, exactly as a sweep drops unmarked objects
    size_t erased = 0;
    for(PtrSet::Iterator iter = set.begin(); iter != set.end(); ) {
      const uintptr_t value = (uintptr_t)*iter / 8 - 1;
      if(value % 2 == 0) {
        iter = set.erase(iter);
        ++erased;
      }
      else {
        ++iter;
      }
    }
    check("erased half of them", erased == 500);
    check("size reflects the erases", set.size() == 500);

    bool correct = true;
    for(uintptr_t i = 0; i < 1000; ++i) {
      const size_t want = (i % 2 == 0) ? 0 : 1;
      if(set.count(Addr(i)) != want) {
        correct = false;
        break;
      }
    }
    check("the survivors are exactly the odd keys", correct);

    size_t remaining = 0;
    for(PtrSet::Iterator iter = set.begin(); iter != set.end(); ++iter) {
      ++remaining;
    }
    check("iteration agrees with size after erases", remaining == 500);
  }

  std::printf("\ntombstones do not accumulate:\n");
  {
    // A long-running program promotes and sweeps repeatedly. If erased slots were
    // never reclaimed the table would fill with tombstones and every probe would
    // degrade into a full scan -- slower than the container this replaced.
    PtrSet set;
    for(uintptr_t round = 0; round < 200; ++round) {
      for(uintptr_t i = 0; i < 100; ++i) {
        set.insert(Addr(round * 100 + i));
      }
      for(PtrSet::Iterator iter = set.begin(); iter != set.end(); ) {
        iter = set.erase(iter);
      }
    }
    check("the set is empty after 200 fill/drain rounds", set.size() == 0);

    // and it still works
    set.insert(Addr(7));
    check("still usable afterwards", set.count(Addr(7)) == 1 && set.size() == 1);
  }

  std::printf("\nclear and reserve:\n");
  {
    PtrSet set;
    set.reserve(65536);
    for(uintptr_t i = 0; i < 100; ++i) {
      set.insert(Addr(i));
    }
    check("reserve did not disturb inserts", set.size() == 100 && set.count(Addr(50)) == 1);

    set.clear();
    check("clear empties the set", set.size() == 0 && set.empty());
    check("a cleared key is gone", set.count(Addr(50)) == 0);

    size_t visits = 0;
    for(PtrSet::Iterator iter = set.begin(); iter != set.end(); ++iter) {
      ++visits;
    }
    check("a cleared set iterates zero times", visits == 0);

    set.insert(Addr(1));
    check("usable after clear", set.count(Addr(1)) == 1);
  }

  std::printf("\n============================================\n");
  std::printf("  %s\n", failures ? "FAILED" : "all passed");
  std::printf("============================================\n");
  return failures ? 1 : 0;
}
