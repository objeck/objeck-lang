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

// Auto-JIT: methods called more than threshold times are JIT compiled.
// Pre-scan validation (CanJitInstruction) runs before resource allocation,
// so unsupported instructions cause immediate return false with no corruption.
// Tunables:
//   OBJECK_JIT_DISABLE=1   — turn auto-JIT off entirely
//   OBJECK_JIT_THRESHOLD=N — call-count threshold (must be positive)
#define JIT_AUTO_THRESHOLD_DEFAULT 10
#define JIT_AUTO_THRESHOLD_DISABLED LONG_MAX

inline long GetJitAutoThreshold() {
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

class JitCompiler {
protected:
  static StackProgram* program;

public:
  static void Initialize(StackProgram* p);

  JitCompiler();

  ~JitCompiler();

  static void JitStackCallback(const long instr_id, StackInstr* instr, const long cls_id,
                               const long mthd_id, size_t* inst, size_t* op_stack, size_t* stack_pos,
                               StackFrame** call_stack, long* call_stack_pos, const long ip);

  static bool TryAutoJitCompile(StackMethod* callee);
  static void PatchCallSites(StackMethod* callee, long patch_value);

  // True if the method contains a trap that reads or writes interpreter
  // locals via frame->mem (SERL_* writers, SYS_TIME/GMT_TIME, FILE_*_TIME,
  // LOAD_CLS_BY_INST). JIT-compiled methods keep locals in native stack
  // slots and the JIT trap callback passes a null frame, so methods with
  // these traps must remain interpreted (both AMD64 and ARM64 reject them
  // in their pre-scans).
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
