/***************************************************************************
 * Token sampling for SLM/phi3 generation.
 *
 * Deliberately free of any ONNX Runtime / OpenCV dependency so it can be unit
 * tested in isolation (see test/test_phi3_sampling.cpp) -- the numeric logic
 * is where an off-by-one or a degenerate distribution would silently corrupt
 * generated text, and that must be testable without a model or a GPU.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __PHI3_SAMPLING_H__
#define __PHI3_SAMPLING_H__

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

// Selects the next token from a vocabulary-sized logits row. temperature < ~0
// (below 1e-6) is greedy argmax; otherwise softmax sampling at that
// temperature. Returns 0 for an empty/absent row rather than reading past it,
// and falls back to greedy for a degenerate (all -inf / overflowing)
// distribution instead of dividing by zero.
static int64_t sample_token(const float* logits, int vocab_size, double temperature) {
   if(!logits || vocab_size < 1) {
      return 0;   // nothing to sample from; do not read an empty range
   }

   if(temperature < 1e-6) {
      // Greedy
      return (int64_t)std::distance(logits,
         std::max_element(logits, logits + vocab_size));
   }

   // Temperature sampling
   std::vector<float> probs(vocab_size);
   float max_logit = *std::max_element(logits, logits + vocab_size);
   float sum = 0.f;
   for(int j = 0; j < vocab_size; ++j) {
      probs[j] = std::exp((logits[j] - max_logit) / (float)temperature);
      sum += probs[j];
   }
   if(sum <= 0.f || !std::isfinite(sum)) {
      // degenerate distribution (all -inf, or overflow); fall back to greedy
      return (int64_t)std::distance(logits, std::max_element(logits, logits + vocab_size));
   }
   for(int j = 0; j < vocab_size; ++j) {
      probs[j] /= sum;
   }

   // Seed once per thread rather than re-seeding a Mersenne Twister from
   // random_device on every token (slow, and random_device is deterministic
   // on some toolchains) -- the same fix the VM applied to RAND_FLOAT.
   static thread_local std::mt19937 gen(std::random_device{}());
   std::discrete_distribution<int> dist(probs.begin(), probs.end());
   return (int64_t)dist(gen);
}

#endif
