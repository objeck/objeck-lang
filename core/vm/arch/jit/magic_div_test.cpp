/***************************************************************************
 * Unit tests for the JIT's division-by-a-constant numbers (magic_div.h).
 *
 * Build and run:
 *   g++ -O2 -std=c++17 -Wall -Wextra -o magic_div_test magic_div_test.cpp && ./magic_div_test
 *
 * Both backends replace n / d and n % d for a constant d with a multiply by
 * the pair MagicSigned64 returns. This runs the sequence they emit, in C++,
 * against the machine's own division: for every divisor the method covers
 * (|d| >= 2, the extremes included) and dividends chosen to sit on every
 * boundary. A wrong pair here is a wrong quotient in compiled code and a
 * right one in the interpreter.
 *
 * It also pins the divisor's precondition: 0, 1 and -1 are the callers' to
 * handle (see magic_div.h), and the helper divides by the divisor's magnitude
 * and by a second value derived from it, so the test shows neither is ever
 * zero for a divisor a caller may pass.
 ***************************************************************************/

#include "magic_div.h"

#include <cstdio>
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

// the sequence EmitMagicDivision emits (jit_amd_lp64.cpp, jit_arm_a64.cpp)
static int64_t MagicQuotient(int64_t n, int64_t d, int64_t magic, int shift) {
  int64_t q = (int64_t)(((__int128)n * (__int128)magic) >> 64);
  if(d > 0 && magic < 0) {
    q = (int64_t)((uint64_t)q + (uint64_t)n);
  }
  if(d < 0 && magic > 0) {
    q = (int64_t)((uint64_t)q - (uint64_t)n);
  }
  q >>= shift;
  q = (int64_t)((uint64_t)q + ((uint64_t)q >> 63));
  return q;
}

// xorshift64*: the same numbers on every run
static uint64_t state = 0x9E3779B97F4A7C15ULL;
static uint64_t Next() {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 0x2545F4914F6CDD1DULL;
}

int main() {
  const int64_t min = INT64_MIN;
  const int64_t max = INT64_MAX;

  std::vector<int64_t> divisors;
  for(int64_t d = 2; d <= 1025; ++d) {
    divisors.push_back(d);
    divisors.push_back(-d);
  }
  for(int bit = 2; bit < 63; ++bit) {
    const int64_t power = (int64_t)1 << bit;
    for(int64_t near = -1; near <= 1; ++near) {
      divisors.push_back(power + near);
      divisors.push_back(-(power + near));
    }
  }
  const int64_t edges[] = { max, max - 1, max / 2, max / 3, min, min + 1, min / 2, min / 3,
                            10, 100, 1000, 1000000007, 86400, 3600000, 0x100000001LL, -0x100000001LL };
  for(int64_t d : edges) {
    divisors.push_back(d);
  }
  for(int i = 0; i < 4000; ++i) {
    // every magnitude: a random value shifted right by a random amount
    const int by = (int)(Next() % 63);
    const int64_t d = (int64_t)Next() >> by;
    if(d != 0 && d != 1 && d != -1) {
      divisors.push_back(d);
    }
  }

  std::vector<int64_t> dividends = { 0, 1, -1, 2, -2, 3, -3, 7, -7, 9, -9, 10, -10, 99, -99, 100, -100,
                                     max, max - 1, max - 2, min, min + 1, min + 2, max / 2, min / 2,
                                     (int64_t)1 << 31, -((int64_t)1 << 31), ((int64_t)1 << 32) - 1, (int64_t)1 << 32,
                                     ((int64_t)1 << 62) - 1, (int64_t)1 << 62, -((int64_t)1 << 62) };
  for(int i = 0; i < 400; ++i) {
    const int by = (int)(Next() % 63);
    dividends.push_back((int64_t)Next() >> by);
  }

  long long wrong_quotient = 0;
  long long wrong_remainder = 0;
  long long zero_divisor = 0;
  long long pairs = 0;
  for(int64_t d : divisors) {
    // the two values MagicSigned64 divides by, computed as it computes them
    const uint64_t two63 = 0x8000000000000000ULL;
    const uint64_t ad = d < 0 ? (uint64_t)0 - (uint64_t)d : (uint64_t)d;
    const uint64_t t = two63 + ((uint64_t)d >> 63);
    const uint64_t anc = t - 1 - t % ad;
    if(ad == 0 || anc == 0) {
      ++zero_divisor;
      continue;
    }

    int64_t magic;
    int shift;
    MagicSigned64(d, magic, shift);
    if(shift < 0 || shift > 63) {
      ++wrong_quotient;
      continue;
    }

    // the multiples of d and their neighbours are where a quotient is off by one
    std::vector<int64_t> all = dividends;
    for(int64_t k = -3; k <= 3; ++k) {
      const __int128 multiple = (__int128)d * k;
      for(int64_t near = -1; near <= 1; ++near) {
        const __int128 n = multiple + near;
        if(n >= min && n <= max) {
          all.push_back((int64_t)n);
        }
      }
    }

    for(int64_t n : all) {
      ++pairs;
      const int64_t q = MagicQuotient(n, d, magic, shift);
      const int64_t want_q = n / d;   // d is never -1, so this cannot overflow
      if(q != want_q) {
        if(++wrong_quotient <= 5) {
          std::printf("    %lld / %lld: magic gives %lld, the machine %lld\n", (long long)n, (long long)d, (long long)q, (long long)want_q);
        }
      }
      // the backends form the remainder as n - q * d
      const int64_t r = (int64_t)((uint64_t)n - (uint64_t)q * (uint64_t)d);
      if(r != n % d) {
        ++wrong_remainder;
      }
    }
  }

  std::printf("  %zu divisors, %lld divisor/dividend pairs\n", divisors.size(), pairs);
  check("every quotient equals the machine's", wrong_quotient == 0);
  check("every remainder equals the machine's", wrong_remainder == 0);
  check("neither value the helper divides by is zero for a divisor other than 0", zero_divisor == 0);
  check("the divisors include INT64_MIN and INT64_MAX", divisors.size() > 6000);

  std::printf("magic_div_test: %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
