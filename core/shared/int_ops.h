/***************************************************************************
 * Objeck integer semantics, shared by the compiler and the VM
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
 ***************************************************************************/

#pragma once

#include <cstdint>

/**
 * The one definition of Objeck's Int arithmetic (docs/FEATURES.md, "Integer
 * arithmetic"). The interpreter, obc's constant folder and both JIT constant
 * folders call these, so a result never depends on which of them computed it:
 * - + - * wrap (two's complement) and never trap;
 * - shift counts are taken modulo 64 (n & 63); >> sign-fills, >>> zero-fills;
 * - / truncates toward zero, % takes the dividend's sign;
 * - MIN / -1 = MIN and MIN % -1 = 0; a zero divisor is the only trap, and the
 *   caller checks for it BEFORE calling IntDiv/IntMod.
 * Plain C++ gets several of these wrong: signed overflow and shifts by >= 64
 * are undefined, and MIN / -1 raises a hardware fault on x64.
 */
namespace objeck_int {
  inline int64_t Add(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a + (uint64_t)b);
  }

  inline int64_t Sub(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a - (uint64_t)b);
  }

  inline int64_t Mul(int64_t a, int64_t b) {
    return (int64_t)((uint64_t)a * (uint64_t)b);
  }

  inline int64_t Shl(int64_t a, int64_t n) {
    return (int64_t)((uint64_t)a << (n & 63));
  }

  // arithmetic (sign-filling) shift, Objeck's '>>'
  inline int64_t Sar(int64_t a, int64_t n) {
    return a >> (n & 63);
  }

  // logical (zero-filling) shift, Objeck's '>>>'
  inline int64_t Shr(int64_t a, int64_t n) {
    return (int64_t)((uint64_t)a >> (n & 63));
  }

  // b must not be 0
  inline int64_t Div(int64_t a, int64_t b) {
    return b == -1 ? Sub(0, a) : a / b;
  }

  // b must not be 0
  inline int64_t Mod(int64_t a, int64_t b) {
    return b == -1 ? 0 : a % b;
  }
}

// the one runtime message for a zero divisor, interpreted or compiled
#define OBJECK_DIVIDE_BY_ZERO_MESSAGE L">>> Divide by zero <<<"
