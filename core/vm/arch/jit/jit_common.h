/***************************************************************************
 * Common JIT compiler functions
 *
 * Copyright (c) 2026 Randy Hollines
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

#ifdef _WIN32
#include "../win32/win32.h"
#else
#include "../posix/posix.h"
#include <sys/mman.h>
#include <errno.h>
#endif
#include "../../common.h"
#include "../../interpreter.h"
#include <climits>
#include "../../vm_options.h"

// Auto-JIT: methods called more than threshold times are JIT compiled.
// Pre-scan validation (CanJitInstruction) runs before resource allocation,
// so unsupported instructions cause immediate return false with no corruption.
//
// NOTE: lowering this default to 1 yields a large win on closure/numeric code
// (spectralnorm 2000: 0.44s vs 7.0s at 10, 16x — only threshold=1 JITs the
// outermost hot method before its single huge first call). It is NOT safe to
// do yet: threshold=1 compiles many rarely-exercised methods and surfaces
// several latent JIT miscompiles (chained transcendental float calls produce
// NaN, string-interpolation paths, etc.). Those must be fixed first. See the
// DYN_MTHD_CALL fixes in this change set for two such bugs already addressed.
// Tunables:
//   OBJECK_JIT_DISABLE=1   — turn auto-JIT off entirely
//   OBJECK_JIT_THRESHOLD=N — call-count threshold (must be positive)
#define JIT_AUTO_THRESHOLD_DEFAULT 10
#define JIT_AUTO_THRESHOLD_DISABLED LONG_MAX

// Signed 64-bit "magic number" division (Granlund-Montgomery; Hacker's
// Delight 10-1). For a constant divisor d with |d| >= 2 the quotient of any
// int64 n is
//   q = high64(n * M);  q += n if d > 0 && M < 0;  q -= n if d < 0 && M > 0;
//   q >>= s (arithmetic);  q += (q >>> 63)
// exact for every n including INT64_MIN, with a 3-cycle multiply in place of
// a 10-20 cycle idiv/sdiv. Both backends emit that sequence; this computes
// (M, s) at compile time.
// Reads an environment variable the way the platform prefers (getenv is
// deprecated under MSVC). True when the variable is set; its text in `value`
// when the caller wants it.
inline bool JitEnvFlag(const char* name, std::string* value = nullptr) {
#ifdef _WIN32
  char* buf = nullptr;
  size_t len = 0;
  if(_dupenv_s(&buf, &len, name) != 0 || !buf) {
    return false;
  }
  if(value) {
    *value = buf;
  }
  free(buf);
  return true;
#else
  const char* env = std::getenv(name);
  if(!env) {
    return false;
  }
  if(value) {
    *value = env;
  }
  return true;
#endif
}

inline void MagicSigned64(int64_t d, int64_t& magic, int& shift) {
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

inline long GetJitAutoThreshold() {
  const long forced = JitAutoThresholdOverride();
  if(forced != 0) {
    return forced < 0 ? JIT_AUTO_THRESHOLD_DISABLED : forced;
  }

  static long threshold = -1;
  if(threshold < 0) {
    threshold = JIT_AUTO_THRESHOLD_DEFAULT;

#ifdef _WIN32
    char* disable_val = nullptr;
    size_t disable_len = 0;
    if(_dupenv_s(&disable_val, &disable_len, "OBJECK_JIT_DISABLE") == 0 && disable_val) {
      const bool disabled = (disable_val[0] == '1' && disable_val[1] == '\0');
      free(disable_val);
      if(disabled) {
        threshold = JIT_AUTO_THRESHOLD_DISABLED;
        return threshold;
      }
    }

    char* env_val = nullptr;
    size_t len = 0;
    if(_dupenv_s(&env_val, &len, "OBJECK_JIT_THRESHOLD") == 0 && env_val) {
      const long parsed = std::atol(env_val);
      if(parsed > 0) {
        threshold = parsed;
      }
      free(env_val);
    }
#else
    const char* disable_val = std::getenv("OBJECK_JIT_DISABLE");
    if(disable_val && disable_val[0] == '1' && disable_val[1] == '\0') {
      threshold = JIT_AUTO_THRESHOLD_DISABLED;
      return threshold;
    }

    const char* env_val = std::getenv("OBJECK_JIT_THRESHOLD");
    if(env_val) {
      const long parsed = std::atol(env_val);
      if(parsed > 0) {
        threshold = parsed;
      }
    }
#endif
  }
  return threshold;
}

#define JIT_AUTO_THRESHOLD GetJitAutoThreshold()
// True when the JIT is at least as eager as its default: a method holding a
// loop is then compiled on its first entry or call instead of its tenth. A
// raised threshold means someone is steering the JIT away deliberately -- the
// LSP server runs with OBJECK_JIT_THRESHOLD=999999999 to keep it off entirely,
// which is not the DISABLED sentinel -- and the loop rules step aside for it.
inline bool JitEagerLoops() {
  return JIT_AUTO_THRESHOLD <= JIT_AUTO_THRESHOLD_DEFAULT;
}

class JitCompiler {
protected:
  static StackProgram* program;

  // The compiled-to-compiled path shared by the opcode bridge and the direct
  // entry. False when the callee has no native code (or is a `virtual`
  // declaration): the caller then takes the interpreter trampoline, which
  // counts the call toward the auto-JIT threshold and compiles the callee.
  static bool CallCompiled(StackMethod* callee, const bool is_dynamic, const long cls_id, const long mthd_id,
                           size_t* op_stack, size_t* stack_pos, StackFrame** call_stack, long* call_stack_pos);

public:
  static void Initialize(StackProgram* p);

  JitCompiler();

  ~JitCompiler();

  static void JitStackCallback(const long instr_id, StackInstr* instr, const long cls_id,
                               const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                               StackFrame** call_stack, long* call_stack_pos, const long ip);

  // The direct bridge entry: a MTHD_CALL whose callee is bound at compile
  // time (anything but a `virtual` declaration) passes the StackMethod* in
  // place of the opcode, so a call is neither switched on nor looked up.
  // Same register layout as JitStackCallback; instr is kept for symmetry.
  static void JitDirectCall(StackMethod* callee, StackInstr* instr, const long cls_id,
                            const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                            StackFrame** call_stack, long* call_stack_pos, const long ip);

  // Called from compiled code when a callee it called directly (phase 3 of
  // the calling convention: no bridge between them) returned one of the
  // guard stubs' statuses. Reports the way the bridge does and exits; the
  // last two are the caller's ids, for the message.
  static void JitNativeCallError(const long status, StackMethod* callee, const long cls_id, const long mthd_id);

  static bool TryAutoJitCompile(StackMethod* callee);
  static void PatchCallSites(StackMethod* callee, long patch_value);

  // True if the method contains a trap that reads or writes interpreter
  // locals via frame->mem (SERL_* writers, SYS_TIME/GMT_TIME, FILE_*_TIME,
  // LOAD_CLS_BY_INST). JIT-compiled methods keep locals in native stack
  // slots and the JIT trap callback passes a null frame, so methods with
  // these traps must remain interpreted (both AMD64 and ARM64 reject them
  // in their pre-scans).
  // A method with a loop is worth compiling on its first entry. The auto-JIT
  // counts calls, and a Main (called once) or a thread's Run (once per thread)
  // holding a hot loop never crossed the threshold, so the loop ran interpreted
  // for the life of the program. A back-edge is a JMP whose target index is not
  // beyond its own (the compiler resolves labels to instruction indices).
  static bool HasLoop(StackMethod* mthd) {
    for(long i = 0; i < mthd->GetInstructionCount(); ++i) {
      StackInstr* instr = mthd->GetInstruction(i);
      if(instr->GetType() == JMP && instr->GetOperand() <= i) {
        return true;
      }
    }
    return false;
  }

  static bool HasFrameDependentTrap(StackMethod* mthd) {
    for(long i = 0; i < mthd->GetInstructionCount(); ++i) {
      const InstructionType type = mthd->GetInstruction(i)->GetType();
      if(type == TRAP || type == TRAP_RTRN) {
        // the trap id is the integer literal pushed directly before the trap
        if(i == 0) {
          return true;
        }
        StackInstr* id_instr = mthd->GetInstruction(i - 1);
        if(id_instr->GetType() != LOAD_INT_LIT) {
          // can't identify the trap statically; be conservative
          return true;
        }
        switch(id_instr->GetInt64Operand()) {
        case instructions::SYS_TIME:
        case instructions::GMT_TIME:
        case instructions::FILE_CREATE_TIME:
        case instructions::FILE_MODIFIED_TIME:
        case instructions::FILE_ACCESSED_TIME:
        case instructions::LOAD_CLS_BY_INST:
        case instructions::SERL_CHAR:
        case instructions::SERL_INT:
        case instructions::SERL_FLOAT:
        case instructions::SERL_OBJ_INST:
        case instructions::SERL_BYTE_ARY:
        case instructions::SERL_CHAR_ARY:
        case instructions::SERL_INT_ARY:
        case instructions::SERL_OBJ_ARY:
        case instructions::SERL_FLOAT_ARY:
          return true;
        default:
          break;
        }
      }
    }
    return false;
  }

  inline static size_t PopInt(size_t* op_stack, size_t* stack_pos);
  inline static void PushInt(size_t* op_stack, size_t* stack_pos, size_t value);
  inline static FLOAT_VALUE PopFloat(size_t* op_stack, size_t* stack_pos);
  inline static void PushFloat(const FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos);
};
