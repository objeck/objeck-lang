/***************************************************************************
 * Size classes for the old generation's free-block cache.
 *
 * Copyright (c) 2025-2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __SIZE_CLASS_H__
#define __SIZE_CLASS_H__

#include <cstddef>

/**
 * Why classes, rather than one list per power of two.
 *
 * The cache used to keep one list per power-of-two pool, holding blocks of any
 * size within it. A request for 100 bytes then walked the (64, 128] list for a
 * block of at least 100, past every cached block of 72 or 96 -- and when none
 * fit, it walked the whole list and allocated fresh anyway. A major collection
 * that freed thousands of small blocks made every later allocation of a
 * slightly larger size pay that full walk: loading a 4000-row table (12000
 * small arrays) took ~1 s on the first pass and 4 ms on the second.
 *
 * Here every class has one size, and the two directions round opposite ways:
 *
 *   - a REQUEST takes the smallest class at least its size (Ceil);
 *   - a freed BLOCK goes to the largest class at most its size (Floor).
 *
 * So any block in a class is big enough for any request mapped to it, and a
 * cache hit is one pop with no search. Blocks the cache hands out are allocated
 * at their class size, so they return to the class they came from; blocks of
 * other sizes (promoted survivors are copied at their exact size) still land in
 * a class they satisfy.
 *
 * Four classes per power of two (32, 40, 48, 56, 64, 80, 96, 112, 128, ...) bound
 * the rounding at 25%; one list per power of two would waste up to 100%.
 */
namespace size_class {
  // The smallest class. Every request is at least the three header words (24
  // bytes), and starting at 32 makes every class a whole number of words, since
  // the step between classes is then at least 8. Smaller blocks are not cached.
  constexpr size_t MIN_SIZE = 32;
  constexpr size_t MIN_LOG2 = 5;
  constexpr size_t STEPS_LOG2 = 2;                 // 4 classes per power of two
  constexpr size_t STEPS = (size_t)1 << STEPS_LOG2;

  constexpr size_t FloorLog2(size_t value) {
    size_t bits = 0;
    while(value >>= 1) {
      ++bits;
    }
    return bits;
  }

  // Number of classes up to and including 'max_size', a power of two >= MIN_SIZE
  constexpr size_t Count(size_t max_size) {
    return (FloorLog2(max_size) - MIN_LOG2) * STEPS + 1;
  }

  // The size of class 'index'
  constexpr size_t Size(size_t index) {
    const size_t base = MIN_SIZE << (index / STEPS);
    return base + (index % STEPS) * (base >> STEPS_LOG2);
  }

  // The smallest class at least 'size' bytes; 'size' is at most the largest class
  constexpr size_t Ceil(size_t size) {
    if(size <= MIN_SIZE) {
      return 0;
    }
    const size_t log2 = FloorLog2(size);
    const size_t base = (size_t)1 << log2;
    const size_t step = base >> STEPS_LOG2;
    const size_t steps = (size - base + step - 1) / step;   // 0..STEPS
    return (log2 - MIN_LOG2) * STEPS + steps;
  }

  // The largest class at most 'size' bytes; 'size' is at least MIN_SIZE
  constexpr size_t Floor(size_t size) {
    const size_t log2 = FloorLog2(size);
    const size_t base = (size_t)1 << log2;
    const size_t step = base >> STEPS_LOG2;
    return (log2 - MIN_LOG2) * STEPS + (size - base) / step;
  }
}

#endif
