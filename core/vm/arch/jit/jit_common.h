/***************************************************************************
 * Common JIT compiler functions
 *
 * Copyright (c) 2025 Randy Hollines
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

  inline static size_t PopInt(size_t* op_stack, size_t* stack_pos);
  inline static void PushInt(size_t* op_stack, size_t* stack_pos, size_t value);
  inline static FLOAT_VALUE PopFloat(size_t* op_stack, size_t* stack_pos);
  inline static void PushFloat(const FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos);
};
