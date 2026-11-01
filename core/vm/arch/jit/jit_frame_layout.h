/************************************************************************
 * The frame layout a compiled method and the collector agree on.
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

#include <cstddef>

// A compiled method's frame has two readers: the backend that emits the
// prologue, the spills and the entry zeroing, and the collector, which walks
// the frame's words from StackFrame::jit_mem (CheckJitRoots, FixupRoots in
// memory.cpp). Both used to carry their own copy of the numbers: the backend
// in its header, the collector as "6 integer temp addresses" and a window of
// JIT_TMP_LOOK_BACK words. When 5555f2960b widened the AMD64 spill slots from
// six to ten, the entry zeroing kept a count derived from six and left
// TMP_REG_0 unzeroed (#1468, docs/GC_ROOTS_DESIGN.md section 3.1). This
// header is the one place the layout lives, and the static_asserts at the
// bottom derive every count the collector and the entry zeroing use from the
// slot offsets, so a widened slot range cannot go stale again.
//
// Frame pointer offsets, by backend. The selection order matters: a Windows
// ARM64 build defines _ARM64 and _WIN64 both.
//
//   AMD64 (RBP-relative; negative offsets grow downward from RBP)
//     Win64: the home area the caller provides holds CLS_ID .. FRAME_MEM
//            above RBP; below RBP come the float temps, the ten integer
//            temps, one RED_ZONE word, then the and/or temp (when the method
//            has one) and the declared locals, highest id lowest in memory.
//     SysV:  CLS_ID .. STACK_POS are below RBP (no home area), then the
//            float temps and the integer temps; RED_ZONE is TMP_REG_9
//            itself, so the locals start right under the last temp.
//   ARM64 (SP-relative; positive offsets)
//     the frame record and the JIT_* words, the float temps, the six
//     integer temps, the D8-D15 save area, RED_ZONE (the and/or temp), then
//     the declared locals upward from RED_ZONE + 8, lowest id first.
//
// StackFrame::jit_mem points at the lowest-addressed declared local on all
// three. On AMD64 the collector walks the declarations upward from there
// (highest id first) and the and/or temp is the word after them; on ARM64 it
// walks them upward from jit_mem (lowest id first) and the and/or temp is the
// word before. The conservative window (JIT_TMP_LOOK_BACK) then runs from the
// and/or temp's word away from the locals: upward on AMD64, downward on
// ARM64. The asserts below prove it reaches every integer spill slot.

#if defined(_ARM64)
  // offsets for ARM A64 addresses
#define TMP_LR 104
#define INT_CONSTS 96
#define CLS_ID 88
#define MTHD_ID 80
#define CLASS_MEM 72
#define INSTANCE_MEM 64
#define OP_STACK 56
#define OP_STACK_POS 48
#define CALL_STACK 40
#define CALL_STACK_POS 32
#define JIT_MEM 24
#define JIT_OFFSET 16
  // float temps
#define TMP_D0 112
#define TMP_D1 120
#define TMP_D2 128
#define TMP_D3 136
  // integer temps
#define TMP_X0 144
#define TMP_X1 152
#define TMP_X2 160
#define TMP_X3 168
#define TMP_X4 176
#define TMP_X5 184
  // callee-saved FP register save slots
#define SAVE_D8  192
#define SAVE_D9  200
#define SAVE_D10 208
#define SAVE_D11 216
#define SAVE_D12 224
#define SAVE_D13 232
#define SAVE_D14 240
#define SAVE_D15 248
#define RED_ZONE 256

// The frame record block a native entry builds in its own frame (see
// JitArm64::EmitNativePrologue): the StackFrame, the two mem words
// (self, 0) the record points at, and the entry-kind word RTRN tests
// (0 bridge, 1 native). It sits above the locals, outside the region the
// entry zeroing and the collector's walk cover, as the outgoing area does.
#define REC_MEM 56
#define REC_KIND 72
#define REC_SIZE 80

#elif defined(_WIN64)
  // offsets for Win64
#define CLS_ID 16
#define MTHD_ID 24
#define CLASS_MEM 32
#define INSTANCE_MEM 40
#define OP_STACK 48
#define STACK_POS 56
#define CALL_STACK 64
#define CALL_STACK_POS 72
#define JIT_MEM 80
#define JIT_OFFSET 88
#define FRAME_MEM 96
  // float temps
#define TMP_XMM_0 -8
#define TMP_XMM_1 -16
#define TMP_XMM_2 -24
  // integer temps
#define TMP_REG_0 -32
#define TMP_REG_1 -40
#define TMP_REG_2 -48
#define TMP_REG_3 -56
#define TMP_REG_4 -64
#define TMP_REG_5 -72
#define TMP_REG_6 -80
#define TMP_REG_7 -88
#define TMP_REG_8 -96
#define TMP_REG_9 -104
#define RED_ZONE -112

#else
  // offset for Posix 64-bit
#define CLS_ID -8
#define MTHD_ID -16
#define CLASS_MEM -24
#define INSTANCE_MEM -32
#define OP_STACK -40
#define STACK_POS -48
#define CALL_STACK 16
#define CALL_STACK_POS 24
#define JIT_MEM 32
#define JIT_OFFSET 40
#define FRAME_MEM 48
  // float temps
#define TMP_XMM_0 -64
#define TMP_XMM_1 -72
#define TMP_XMM_2 -80
  // integer temps
#define TMP_REG_0 -88
#define TMP_REG_1 -96
#define TMP_REG_2 -104
#define TMP_REG_3 -112
#define TMP_REG_4 -120
#define TMP_REG_5 -128
#define TMP_REG_6 -136
#define TMP_REG_7 -144
#define TMP_REG_8 -152
#define TMP_REG_9 -160
#define RED_ZONE -160
#endif

#if !defined(_ARM64)
  // The register-argument entry (EmitNativePrologue). A native caller's
  // arguments start at NATIVE_ARGS from the callee's frame pointer: past the
  // return address and the saved frame pointer, the register homes and stack
  // slots the bridge entry receives there (CLS_ID .. FRAME_MEM on Windows,
  // CALL_STACK .. FRAME_MEM on POSIX), which the native prologue fills for
  // itself. The caller writes them at OUT_ARGS from its stack pointer, in
  // the outgoing area its prologue reserves (out_area).
#ifdef _WIN64
#define NATIVE_ARGS 104
#else
#define NATIVE_ARGS 56
#endif
#define OUT_ARGS (NATIVE_ARGS - 16)
  // the frame record block below a method's locals (rec_base): its
  // StackFrame, the two mem words (self, 0) the record points at, and the
  // entry-kind word RTRN tests (0 bridge, 1 native)
#define REC_MEM 56
#define REC_KIND 72
#define REC_SIZE 80
#endif

namespace Runtime {
  // The counts the backends and the collector derive from the offsets above.
  // Nothing here is a new number: each is the slot range divided by the word
  // size, checked against what the code relied on before it was derived.
  struct JitFrameLayout {
    static constexpr long WORD = (long)sizeof(size_t);

    // how many words beside every compiled frame the collector scans as
    // untyped roots (CheckJitRoots, FixupRoots): from the and/or temp's word
    // away from the locals
    static constexpr int WINDOW = 16;

#if defined(_ARM64)
    // the six integer spill slots TMP_X0 .. TMP_X5 and the four float ones
    static constexpr int TMP_INT_SLOTS = (TMP_X5 - TMP_X0) / WORD + 1;
    static constexpr int TMP_FLOAT_SLOTS = (TMP_D3 - TMP_D0) / WORD + 1;
    // the window starts at the and/or temp (RED_ZONE, the word below the first
    // local) and runs downward; the last integer slot it must reach is TMP_X0
    static constexpr int WORDS_TO_LAST_INT_SLOT = (RED_ZONE - TMP_X0) / WORD + 1;
    // the save area the window crosses before the temps: harmless, never
    // meaningful (docs/GC_ROOTS_DESIGN.md section 5.1)
    static constexpr int SAVE_AREA_WORDS = (SAVE_D15 - SAVE_D8) / WORD + 1;
    static_assert(TMP_X1 - TMP_X0 == WORD && TMP_D1 - TMP_D0 == WORD, "the temps are consecutive words");
    static_assert(TMP_X0 == TMP_D3 + WORD && SAVE_D8 == TMP_X5 + WORD && RED_ZONE == SAVE_D15 + WORD,
                  "the float temps, the integer temps, the save area and RED_ZONE are contiguous");
    static_assert(TMP_INT_SLOTS == 6, "RegisterRoot zeroes six integer slots");
    static_assert(TMP_FLOAT_SLOTS == 4, "ProcessStackCallback has four float slots");
    static_assert(WORDS_TO_LAST_INT_SLOT == 1 + SAVE_AREA_WORDS + TMP_INT_SLOTS, "what lies between the and/or temp and TMP_X0");
#else
    // the ten integer spill slots TMP_REG_0 .. TMP_REG_9 and the three float ones
    static constexpr int TMP_INT_SLOTS = (TMP_REG_0 - TMP_REG_9) / WORD + 1;
    static constexpr int TMP_FLOAT_SLOTS = (TMP_XMM_0 - TMP_XMM_2) / WORD + 1;
    // words between the highest local (or the and/or temp) and TMP_REG_9: the
    // RED_ZONE word on Win64, none on SysV where RED_ZONE is TMP_REG_9 itself.
    // The entry zeroing covers them along with the locals (RegisterRoot).
    static constexpr int LOCALS_TO_TMP_WORDS = (TMP_REG_9 - RED_ZONE) / WORD;
    // the window starts at the word after the highest local, which is the
    // and/or temp when the method has one, and runs upward; the last integer
    // slot it must reach is TMP_REG_0. Count from the and/or temp, the longer
    // case.
    static constexpr int WORDS_TO_LAST_INT_SLOT = 1 + (TMP_REG_0 - RED_ZONE) / WORD + 1;
    static_assert(TMP_REG_0 - TMP_REG_1 == WORD && TMP_XMM_0 - TMP_XMM_1 == WORD, "the temps are consecutive words");
    static_assert(TMP_XMM_2 - TMP_REG_0 == WORD, "the integer temps sit right under the float temps");
    static_assert(RED_ZONE <= TMP_REG_9, "RED_ZONE is TMP_REG_9 or the word under it");
    static_assert(TMP_INT_SLOTS == 10, "ProcessStackCallback and the entry zeroing count ten integer slots");
    static_assert(TMP_FLOAT_SLOTS == 3, "ProcessStackCallback has three float slots");
#ifdef _WIN64
    static_assert(LOCALS_TO_TMP_WORDS == 1, "Win64 keeps one RED_ZONE word under the temps");
#else
    static_assert(LOCALS_TO_TMP_WORDS == 0, "SysV locals start right under TMP_REG_9");
#endif
    static_assert(WORDS_TO_LAST_INT_SLOT == 1 + LOCALS_TO_TMP_WORDS + TMP_INT_SLOTS, "what lies between the and/or temp and TMP_REG_0");
#endif

    // The window covers every integer spill slot, whichever end of the frame
    // the and/or temp is at. This is the check that would have caught the
    // ten-slot widening: the window was fixed at sixteen while the count the
    // zeroing used was not.
    static_assert(WORDS_TO_LAST_INT_SLOT <= WINDOW, "the conservative window must reach every integer spill slot");
  };
}

// the collector's name for the window
#define JIT_TMP_LOOK_BACK (Runtime::JitFrameLayout::WINDOW)
