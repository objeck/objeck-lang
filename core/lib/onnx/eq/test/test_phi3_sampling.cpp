/***************************************************************************
 * Unit tests for sample_token (phi3_sampling.h). Pure logic, no ONNX Runtime
 * or model needed. Build+run:
 *   c++ -std=c++17 -Wall -Wextra test_phi3_sampling.cpp -o t && ./t
 *
 * Copyright (c) 2026, Randy Hollines
 ***************************************************************************/

#include "../phi3_sampling.h"

#include <cassert>
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while(0)

int main() {
   // greedy (temperature 0) returns the argmax
   {
      const float logits[] = {0.1f, 0.2f, 5.0f, 0.3f, -1.0f};
      CHECK(sample_token(logits, 5, 0.0) == 2, "greedy picks the max index");
   }

   // greedy ties resolve to the first max (std::max_element semantics)
   {
      const float logits[] = {2.0f, 2.0f, 1.0f};
      CHECK(sample_token(logits, 3, 0.0) == 0, "greedy tie -> first");
   }

   // a single-element vocabulary always yields index 0
   {
      const float one[] = {42.0f};
      CHECK(sample_token(one, 1, 0.0) == 0, "single element greedy");
      CHECK(sample_token(one, 1, 1.0) == 0, "single element sampled");
   }

   // empty / null / non-positive vocab must not read past the buffer
   {
      const float logits[] = {1.0f};
      CHECK(sample_token(logits, 0, 1.0) == 0, "vocab 0 -> token 0");
      CHECK(sample_token(logits, -5, 1.0) == 0, "negative vocab -> token 0");
      CHECK(sample_token(nullptr, 10, 1.0) == 0, "null logits -> token 0");
   }

   // a degenerate distribution (all -inf) falls back to greedy rather than
   // dividing by a zero sum
   {
      const float neg_inf = -std::numeric_limits<float>::infinity();
      const float logits[] = {neg_inf, neg_inf, neg_inf};
      const int64_t t = sample_token(logits, 3, 1.0);
      CHECK(t >= 0 && t < 3, "all -inf sampling stays in range");
   }

   // temperature sampling stays within [0, vocab) over many draws, and a
   // spiked distribution almost always returns the spike
   {
      float logits[64];
      for(int i = 0; i < 64; ++i) { logits[i] = 0.0f; }
      logits[40] = 50.0f;   // overwhelmingly likely
      int spike = 0;
      for(int i = 0; i < 2000; ++i) {
         const int64_t t = sample_token(logits, 64, 1.0);
         CHECK(t >= 0 && t < 64, "sampled token in range");
         if(t == 40) { ++spike; }
      }
      CHECK(spike > 1900, "a sharply spiked softmax almost always hits the spike");
   }

   if(failures == 0) {
      std::printf("ALL phi3 sampling tests passed\n");
      return 0;
   }
   std::printf("%d check(s) failed\n", failures);
   return 1;
}
