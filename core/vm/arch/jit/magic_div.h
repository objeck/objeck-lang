/************************************************************************
 * Division by a constant as a multiply: the numbers both JIT backends use.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *************************************************************************/

#pragma once

#include <cassert>
#include <cstdint>

// Signed 64-bit "magic number" division (Granlund-Montgomery; Hacker's
// Delight 10-1). For a constant divisor d with |d| >= 2 the quotient of any
// int64 n is
//   q = high64(n * M);  q += n if d > 0 && M < 0;  q -= n if d < 0 && M > 0;
//   q >>= s (arithmetic);  q += (q >>> 63)
// exact for every n including INT64_MIN, with a 3-cycle multiply in place of
// a 10-20 cycle idiv/sdiv. Both backends emit that sequence; this computes
// (M, s) at compile time. magic_div_test.cpp checks the pair against the
// machine's own division.
//
// d must not be 0, 1 or -1. The method has no multiplier for 1 and -1, and for
// 0 the two divisions below divide by zero, here in the VM while it compiles
// (a divide trap in the host, not the program's "divide by zero" error). No
// caller passes them: div_imm_reg in each backend, the only caller, returns
// for 1 and -1 before it gets here (a no-op, a negation, or the divide
// instruction) and sends 0 to the runtime divide so that its zero check
// raises the error. The assert states the same thing to a debug build.
inline void MagicSigned64(int64_t d, int64_t& magic, int& shift) {
  assert(d != 0 && d != 1 && d != -1);
  const uint64_t two63 = 0x8000000000000000ULL;
  const uint64_t ad = d < 0 ? (uint64_t)0 - (uint64_t)d : (uint64_t)d;
  const uint64_t t = two63 + ((uint64_t)d >> 63);
  const uint64_t anc = t - 1 - t % ad;
  int p = 63;
  uint64_t q1 = two63 / anc, r1 = two63 - q1 * anc;
  uint64_t q2 = two63 / ad, r2 = two63 - q2 * ad;
  uint64_t delta;
  do {
    p++;
    q1 = 2 * q1; r1 = 2 * r1;
    if(r1 >= anc) { q1++; r1 -= anc; }
    q2 = 2 * q2; r2 = 2 * r2;
    if(r2 >= ad) { q2++; r2 -= ad; }
    delta = ad - r2;
  }
  while(q1 < delta || (q1 == delta && r1 == 0));
  magic = (int64_t)(q2 + 1);
  if(d < 0) {
    magic = (int64_t)((uint64_t)0 - (uint64_t)magic);
  }
  shift = p - 64;
}
