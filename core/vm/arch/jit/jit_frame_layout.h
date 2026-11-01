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
// sixteen untyped words. When 5555f2960b widened the AMD64 spill slots from
// six to ten, the entry zeroing kept a count derived from six and left
// TMP_REG_0 unzeroed (#1468, docs/GC_ROOTS_DESIGN.md section 3.1). This
// header is the one place the layout lives, and the static_asserts at the
// bottom derive every count the collector and the entry zeroing use from the
// slot offsets, so a widened slot range cannot go stale again.
//
// Frame pointer offsets, by backend. The selection order matters: a Windows
// ARM64 build defines _ARM64 and _WIN64 both. The ARM64 layout is selected on
// the compiler's own target macro as well as the build's _ARM64, because not
// every project that compiles the ARM64 backend defines _ARM64 (the Windows
// debugger project compiles jit_arm_a64.cpp with _WIN64 alone, and got the
// Win64 block). JIT_LAYOUT_ARM64 is the one test the collector uses for the
// frame walk, so it and the backend can never disagree about the layout.
#if defined(_ARM64) || defined(_M_ARM64) || defined(__aarch64__)
#define JIT_LAYOUT_ARM64 1
#endif
//
//   AMD64 (RBP-relative; negative offsets grow downward from RBP)
//     Win64: the home area the caller provides holds CLS_ID .. FRAME_MEM
//            above RBP; below RBP come the float temps, then three classes
//            of integer slot (below), one RED_ZONE word, then the and/or
//            temp (when the method has one) and the declared locals, highest
//            id lowest in memory.
//     SysV:  CLS_ID .. STACK_POS are below RBP (no home area), then the
//            float temps and the integer slots; RED_ZONE is TMP_REF_9
//            itself, so the locals start right under the last slot.
//
//     The integer slots are typed (docs/GC_ROOTS_DESIGN.md, phase 3):
//       TMP_SCR_0..3  scratch saves inside one instruction (RAX/RDX around a
//                     division, RCX around a shift, RAX-RDX around the inline
//                     allocator). No collection can run between the save and
//                     the restore, so the collector never reads them.
//       TMP_VAL_0..9  pending values the method's reference map (ref_map.h)
//                     proves are Int at that instruction, spilled across a
//                     call, a loop-header park or a libm call. Never scanned,
//                     never relocated: an Int that happens to equal an
//                     object's address stays what it was (section 3.3).
//       TMP_REF_0..9  every other pending value: a reference, or one the map
//                     leaves unknown. Zeroed at entry, cleared after each
//                     reload, and the only words beside the locals the
//                     collector scans and relocates. A map imprecision lands
//                     here, so it costs retention, never a missed root.
//     The and/or temp keeps its conservative scan until phase 5 types it.
//   ARM64 (SP-relative; positive offsets)
//     the frame record and the JIT_* words, the float temps, then the same
//     three classes of integer slot (TMP_SCR_0..2, TMP_VAL_0..5), the D8-D15
//     save area, TMP_REF_0..5, the RED_ZONE word, the and/or temp when the
//     method has one, then the declared locals upward, lowest id first. The
//     reference slots sit right under RED_ZONE so the entry zeroing is one
//     range from TMP_REF_0 through the locals.
//
// StackFrame::jit_mem points at the lowest-addressed declared local on all
// three. On AMD64 the collector walks the declarations upward from there
// (highest id first) and the and/or temp is the word after them; on ARM64 it
// walks them upward from jit_mem (lowest id first) and the and/or temp is the
// word before. On both the collector reads the TMP_REF_* slots by their
// offsets from the RED_ZONE word (JitFrameLayout::RefSlot), and scans the old
// conservative window over every slot class only under
// OBJECK_GC_ROOTS=conservative, the negative control.

#if defined(JIT_LAYOUT_ARM64)
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
  // scratch saves, never scanned: X9 across a libm call; the destination
  // object and the pending values across the write barrier's slow path
#define TMP_SCR_0 144
#define TMP_SCR_1 152
#define TMP_SCR_2 160
  // pending Int values, never scanned
#define TMP_VAL_0 168
#define TMP_VAL_1 176
#define TMP_VAL_2 184
#define TMP_VAL_3 192
#define TMP_VAL_4 200
#define TMP_VAL_5 208
  // callee-saved FP register save slots
#define SAVE_D8  216
#define SAVE_D9  224
#define SAVE_D10 232
#define SAVE_D11 240
#define SAVE_D12 248
#define SAVE_D13 256
#define SAVE_D14 264
#define SAVE_D15 272
  // pending references (and unknowns): zeroed at entry, scanned, relocated.
  // Right under RED_ZONE and the locals, so the entry zeroing is one range.
#define TMP_REF_0 280
#define TMP_REF_1 288
#define TMP_REF_2 296
#define TMP_REF_3 304
#define TMP_REF_4 312
#define TMP_REF_5 320
#define RED_ZONE 328

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
  // scratch saves, never scanned
#define TMP_SCR_0 -32
#define TMP_SCR_1 -40
#define TMP_SCR_2 -48
#define TMP_SCR_3 -56
  // pending Int values, never scanned
#define TMP_VAL_0 -64
#define TMP_VAL_1 -72
#define TMP_VAL_2 -80
#define TMP_VAL_3 -88
#define TMP_VAL_4 -96
#define TMP_VAL_5 -104
#define TMP_VAL_6 -112
#define TMP_VAL_7 -120
#define TMP_VAL_8 -128
#define TMP_VAL_9 -136
  // pending references (and unknowns): zeroed at entry, scanned, relocated
#define TMP_REF_0 -144
#define TMP_REF_1 -152
#define TMP_REF_2 -160
#define TMP_REF_3 -168
#define TMP_REF_4 -176
#define TMP_REF_5 -184
#define TMP_REF_6 -192
#define TMP_REF_7 -200
#define TMP_REF_8 -208
#define TMP_REF_9 -216
#define RED_ZONE -224

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
  // scratch saves, never scanned
#define TMP_SCR_0 -88
#define TMP_SCR_1 -96
#define TMP_SCR_2 -104
#define TMP_SCR_3 -112
  // pending Int values, never scanned
#define TMP_VAL_0 -120
#define TMP_VAL_1 -128
#define TMP_VAL_2 -136
#define TMP_VAL_3 -144
#define TMP_VAL_4 -152
#define TMP_VAL_5 -160
#define TMP_VAL_6 -168
#define TMP_VAL_7 -176
#define TMP_VAL_8 -184
#define TMP_VAL_9 -192
  // pending references (and unknowns): zeroed at entry, scanned, relocated
#define TMP_REF_0 -200
#define TMP_REF_1 -208
#define TMP_REF_2 -216
#define TMP_REF_3 -224
#define TMP_REF_4 -232
#define TMP_REF_5 -240
#define TMP_REF_6 -248
#define TMP_REF_7 -256
#define TMP_REF_8 -264
#define TMP_REF_9 -272
#define RED_ZONE -272
#endif

#if !defined(JIT_LAYOUT_ARM64)
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

#if defined(JIT_LAYOUT_ARM64)
    // the three slot classes and the float temps (GC roots phase 4)
    static constexpr int TMP_REF_SLOTS = (TMP_REF_5 - TMP_REF_0) / WORD + 1;
    static constexpr int TMP_VAL_SLOTS = (TMP_VAL_5 - TMP_VAL_0) / WORD + 1;
    static constexpr int TMP_SCR_SLOTS = (TMP_SCR_2 - TMP_SCR_0) / WORD + 1;
    static constexpr int TMP_FLOAT_SLOTS = (TMP_D3 - TMP_D0) / WORD + 1;
    static constexpr int SAVE_AREA_WORDS = (SAVE_D15 - SAVE_D8) / WORD + 1;
    // The old conservative window, kept for OBJECK_GC_ROOTS=conservative:
    // from the RED_ZONE word (below the and/or temp and the locals) downward
    // through the first scratch slot, so it covers every integer slot of
    // every class, as the pre-phase-4 window covered TMP_X0..TMP_X5.
    static constexpr int CONSERVATIVE_WINDOW = (RED_ZONE - TMP_SCR_0) / WORD + 1;
    static_assert(TMP_REF_1 - TMP_REF_0 == WORD && TMP_VAL_1 - TMP_VAL_0 == WORD &&
                  TMP_SCR_1 - TMP_SCR_0 == WORD && TMP_D1 - TMP_D0 == WORD && SAVE_D9 - SAVE_D8 == WORD,
                  "the temps are consecutive words");
    static_assert(TMP_SCR_0 == TMP_D3 + WORD && TMP_VAL_0 == TMP_SCR_2 + WORD && SAVE_D8 == TMP_VAL_5 + WORD &&
                  TMP_REF_0 == SAVE_D15 + WORD && RED_ZONE == TMP_REF_5 + WORD,
                  "float temps, scratch, value slots, the save area, reference slots and RED_ZONE are contiguous, in that order upward");
    static_assert(TMP_REF_SLOTS == 6 && TMP_VAL_SLOTS == 6, "six pending values of either class, as the one untyped class allowed before");
    static_assert(TMP_SCR_SLOTS == 3, "the write barrier saves its destination and up to two pending values");
    static_assert(TMP_FLOAT_SLOTS == 4, "ProcessStackCallback has four float slots");
    static_assert(SAVE_AREA_WORDS == 8, "D8-D15");
    static_assert(CONSERVATIVE_WINDOW == 1 + TMP_REF_SLOTS + SAVE_AREA_WORDS + TMP_VAL_SLOTS + TMP_SCR_SLOTS,
                  "the conservative window is the RED_ZONE word and every slot below it");
    static_assert(TMP_REF_0 + (TMP_REF_SLOTS - 1) * WORD == TMP_REF_5, "RefSlot reaches TMP_REF_5 last");
    static_assert(TMP_VAL_0 + (TMP_VAL_SLOTS - 1) * WORD == TMP_VAL_5, "ValSlot reaches TMP_VAL_5 last");

    // The collector's view: `red_zone` is the word below the first local
    // (jit_mem - 1, the RED_ZONE word); the slots lie at fixed distances below it.
    static inline size_t* RefSlot(size_t* red_zone, int k) {
      return (size_t*)((char*)red_zone - (RED_ZONE - TMP_REF_0) + k * WORD);
    }
    static inline size_t* ValSlot(size_t* red_zone, int k) {
      return (size_t*)((char*)red_zone - (RED_ZONE - TMP_VAL_0) + k * WORD);
    }
#else
    // the three slot classes and the float temps
    static constexpr int TMP_REF_SLOTS = (TMP_REF_0 - TMP_REF_9) / WORD + 1;
    static constexpr int TMP_VAL_SLOTS = (TMP_VAL_0 - TMP_VAL_9) / WORD + 1;
    static constexpr int TMP_SCR_SLOTS = (TMP_SCR_0 - TMP_SCR_3) / WORD + 1;
    static constexpr int TMP_FLOAT_SLOTS = (TMP_XMM_0 - TMP_XMM_2) / WORD + 1;
    // words between the highest local (or the and/or temp) and TMP_REF_9: the
    // RED_ZONE word on Win64, none on SysV where RED_ZONE is TMP_REF_9 itself.
    // The entry zeroing covers them along with the locals and the reference
    // slots (RegisterRoot): one contiguous range from the lowest local up to
    // TMP_REF_0, which is why the reference slots sit right above the locals.
    static constexpr int LOCALS_TO_REF_WORDS = (TMP_REF_9 - RED_ZONE) / WORD;
    // The old conservative window, kept for OBJECK_GC_ROOTS=conservative:
    // from the and/or temp upward through the last scratch slot, so it covers
    // every integer slot of every class, as the pre-phase-3 window did.
    static constexpr int CONSERVATIVE_WINDOW = 1 + (TMP_SCR_0 - RED_ZONE) / WORD + 1;
    static_assert(TMP_REF_0 - TMP_REF_1 == WORD && TMP_VAL_0 - TMP_VAL_1 == WORD &&
                  TMP_SCR_0 - TMP_SCR_1 == WORD && TMP_XMM_0 - TMP_XMM_1 == WORD, "the temps are consecutive words");
    static_assert(TMP_XMM_2 - TMP_SCR_0 == WORD && TMP_SCR_3 - TMP_VAL_0 == WORD && TMP_VAL_9 - TMP_REF_0 == WORD,
                  "float temps, scratch, value and reference slots are contiguous, in that order downward");
    static_assert(RED_ZONE <= TMP_REF_9, "RED_ZONE is TMP_REF_9 or the word under it");
    static_assert(TMP_REF_SLOTS == 10 && TMP_VAL_SLOTS == 10, "ten pending values of either class, as the one untyped class allowed before");
    static_assert(TMP_SCR_SLOTS == 4, "the inline allocator saves RAX, RBX, RCX and RDX");
    static_assert(TMP_FLOAT_SLOTS == 3, "ProcessStackCallback has three float slots");
#ifdef _WIN64
    static_assert(LOCALS_TO_REF_WORDS == 1, "Win64 keeps one RED_ZONE word under the reference slots");
#else
    static_assert(LOCALS_TO_REF_WORDS == 0, "SysV locals start right under TMP_REF_9");
#endif
    static_assert(CONSERVATIVE_WINDOW == 1 + LOCALS_TO_REF_WORDS + TMP_REF_SLOTS + TMP_VAL_SLOTS + TMP_SCR_SLOTS,
                  "the conservative window is the and/or temp, the RED_ZONE word and every integer slot");
    static_assert(TMP_REF_0 - (TMP_REF_SLOTS - 1) * WORD == TMP_REF_9, "RefSlot reaches TMP_REF_9 last");
    static_assert(TMP_VAL_0 - (TMP_VAL_SLOTS - 1) * WORD == TMP_VAL_9, "ValSlot reaches TMP_VAL_9 last");

    // The collector's view. After it walks the declared locals upward from
    // jit_mem, `after_locals` is the word above the highest one: the and/or
    // temp when the method has one, else the RED_ZONE word (Win64) or
    // TMP_REF_9 (SysV). The frame pointer is then a fixed distance above.
    static inline size_t* FramePointer(size_t* after_locals, bool has_andor) {
      return (size_t*)((char*)(after_locals + (has_andor ? 1 : 0)) - RED_ZONE);
    }
    // reference slot k (0 is TMP_REF_0, the first one every spill takes)
    static inline size_t* RefSlot(size_t* rbp, int k) {
      return (size_t*)((char*)rbp + TMP_REF_0 - k * WORD);
    }
    // value slot k (0 is TMP_VAL_0)
    static inline size_t* ValSlot(size_t* rbp, int k) {
      return (size_t*)((char*)rbp + TMP_VAL_0 - k * WORD);
    }
#endif

  };
}

