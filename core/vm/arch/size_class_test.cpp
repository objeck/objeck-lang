/***************************************************************************
 * Unit tests for the free-block cache's size classes.
 *
 * Build and run:
 *   g++ -O2 -std=c++17 -Wall -o size_class_test size_class_test.cpp && ./size_class_test
 *
 * The cache hands out the head of a class's list without looking at it, so the
 * two rounding rules ARE its correctness: a request must map to a class no
 * smaller than itself, and a freed block to a class no bigger than itself. A
 * mistake in either returns a block too small for the object placed in it.
 ***************************************************************************/

#include "size_class.h"

#include <cstdio>

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

// The memory manager's largest cached size (ALIGN_POOL_MAX in memory.h)
static const size_t MAX_SIZE = (size_t)1 << 22;

// Every size up to 64K, then a spread of sizes up to MAX_SIZE that includes each
// class boundary and its neighbours
template<typename Test> static bool ForSizes(size_t from, Test test) {
  for(size_t size = from; size <= 65536; ++size) {
    if(!test(size)) {
      std::printf("    first failure at %zu\n", size);
      return false;
    }
  }
  const size_t count = size_class::Count(MAX_SIZE);
  for(size_t index = 0; index < count; ++index) {
    const size_t edge = size_class::Size(index);
    for(size_t size = edge - 1; size <= edge + 1 && size <= MAX_SIZE; ++size) {
      if(size >= from && !test(size)) {
        std::printf("    first failure at %zu\n", size);
        return false;
      }
    }
  }
  return true;
}

int main() {
  const size_t count = size_class::Count(MAX_SIZE);

  std::printf("\nthe classes themselves:\n");
  {
    check("the first class is the minimum size", size_class::Size(0) == size_class::MIN_SIZE);
    check("the last class is the cache's largest size", size_class::Size(count - 1) == MAX_SIZE);

    bool rising = true;
    for(size_t index = 1; index < count; ++index) {
      rising = rising && size_class::Size(index) > size_class::Size(index - 1);
    }
    check("class sizes strictly rise", rising);

    bool aligned = true;
    for(size_t index = 0; index < count; ++index) {
      aligned = aligned && size_class::Size(index) % sizeof(size_t) == 0;
    }
    check("every class is word aligned", aligned);

    bool exact = true;
    for(size_t index = 0; index < count; ++index) {
      const size_t size = size_class::Size(index);
      exact = exact && size_class::Ceil(size) == index && size_class::Floor(size) == index;
    }
    check("a class's own size maps to it both ways", exact);
  }

  std::printf("\nrequests round up (Ceil):\n");
  {
    // the property the cache depends on: the block handed out is big enough
    check("a request's class is at least the request", ForSizes(1, [](size_t size) {
      return size_class::Size(size_class::Ceil(size)) >= size;
    }));
    // and the class chosen is the tightest one, so no memory is wasted needlessly
    check("and the class below it is too small", ForSizes(1, [](size_t size) {
      const size_t index = size_class::Ceil(size);
      return index == 0 || size_class::Size(index - 1) < size;
    }));
    check("rounding wastes at most 25% past the minimum size", ForSizes(size_class::MIN_SIZE, [](size_t size) {
      return size_class::Size(size_class::Ceil(size)) * 4 <= size * 5;
    }));
  }

  std::printf("\nfreed blocks round down (Floor):\n");
  {
    // the other half: a block placed in a class can serve that class's requests
    check("a block's class is at most the block", ForSizes(size_class::MIN_SIZE, [](size_t size) {
      return size_class::Size(size_class::Floor(size)) <= size;
    }));
    const size_t last = count;
    check("and the class above it is too big", ForSizes(size_class::MIN_SIZE, [last](size_t size) {
      const size_t index = size_class::Floor(size);
      return index + 1 == last || size_class::Size(index + 1) > size;
    }));
    check("a block of the largest size is in range", size_class::Floor(MAX_SIZE) == count - 1);
  }

  std::printf("\nthe cache round trip:\n");
  {
    // a block allocated for a request (at its class size) and freed again goes
    // back to the very class it came from, so the cache does not drift downward
    check("an allocated block returns to its own class", ForSizes(1, [](size_t size) {
      const size_t index = size_class::Ceil(size);
      return size_class::Floor(size_class::Size(index)) == index;
    }));
    // any block on a class's list serves any request that maps to that class
    check("every block in a class serves every request of that class", ForSizes(size_class::MIN_SIZE, [](size_t block) {
      const size_t index = size_class::Floor(block);
      const size_t largest_request = size_class::Size(index);
      return size_class::Ceil(largest_request) == index && block >= largest_request;
    }));
  }

  std::printf("\n%s\n", failures ? "FAILED" : "all passed");
  return failures ? 1 : 0;
}
