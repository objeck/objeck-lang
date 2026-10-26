/***************************************************************************
 * VM stack machine.
 *
 * Copyright (c) 2025, Randy Hollines
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

#include "interpreter.h"
#include "vm_options.h"
#include "dispatch.h"

// IEEE comparison semantics for this translation unit.
//
// core/vm/vs/vm.vcxproj builds the VM with <FloatingPointModel>Fast</...>,
// i.e. MSVC /fp:fast, which permits assuming operands are not NaN and folding
// comparisons accordingly. The C++ below is IEEE-correct -- `left < right` on
// two doubles -- but under /fp:fast it compiled into something that was not:
// EVERY comparison involving NaN returned true, in both operand orders, so NaN
// compared equal to 1.0 and was simultaneously less than and greater than it.
//
// It also diverged by platform. This is the only project in the repository that
// sets FloatingPointModel, and core/vm/Makefile passes no -ffast-math, so the
// same program answered differently on Windows than on Linux or macOS.
//
// The whole file is pinned rather than the six comparison helpers, because the
// non-debugger build handles LES_FLOAT and friends in the inline hot-opcode
// switch inside Execute() and only falls through to those helpers in `default:`
// -- both paths are here, and a pragma can only wrap whole functions.
//
// Scoped to this file so the rest of the VM keeps the faster model. Issue #1000.
#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif
#include "lib_api.h"

#ifndef _NO_JIT
#if defined(_M_ARM64)
#include "arch/jit/arm64/jit_arm_a64.h"
#elif defined(_WIN64) || defined(_X64)
#include "arch/jit/amd64/jit_amd_lp64.h"
#else
#include "arch/jit/arm64/jit_arm_a64.h"
#endif
#endif

#ifdef _WIN32
#include "arch/win32/win32.h"
#else
#include "arch/posix/posix.h"
#endif

#ifdef _DEBUGGER
#include "../debugger/debugger.h"
#endif

#include <sstream>
#include <math.h>

using namespace Runtime;

StackProgram* StackInterpreter::program;
std::set<StackInterpreter*> StackInterpreter::intpr_threads;

#ifdef _WIN32
bool StackInterpreter::is_stdio_binary;
#endif

#ifdef _WIN32
CRITICAL_SECTION StackInterpreter::intpr_threads_cs;
#else
pthread_mutex_t StackInterpreter::intpr_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/********************************
 * Frame pool: one free list per thread. A frame is acquired and released on
 * the thread whose call stack it sits on, so the list needs no lock; the
 * global stack it replaces cost a critical-section round trip on each side
 * of every call. A thread's list is filled on demand and freed when the
 * thread ends (the frames a thread holds at any moment are on its call
 * stack, never in the list).
 ********************************/
namespace {
  struct FramePool {
    std::vector<StackFrame*> free_frames;

    ~FramePool() {
      for(StackFrame* frame : free_frames) {
        free(frame->mem);
        delete frame;
      }
    }
  };
  thread_local FramePool tl_frame_pool;
  const size_t FRAME_POOL_FILL = 64;
}

/********************************
 * VM initialization
 ********************************/
void StackInterpreter::Initialize(StackProgram* p, size_t m)
{
  program = p;

  // Every VM start readies the process for sockets and pipes (SIGPIPE ignored
  // on POSIX, Winsock started on Windows), whichever host started it: obr,
  // obd, obi or an embedding program. See IPSocket::Startup.
  IPSocket::Startup();
    
#ifdef _WIN32
  InitializeCriticalSection(&intpr_threads_cs);
#endif

#ifndef _NO_JIT
#if defined(_M_ARM64)
  JitArm64::Initialize(program);
#elif defined(_WIN64) || defined(_X64)
  JitAmd64::Initialize(program);
#else
  JitArm64::Initialize(program);
#endif
#endif
  MemoryManager::Initialize(program, m);

#ifdef _MODULE
  TrapProcessor::Initialize(program);
#endif
}

/********************************
 * Main VM execution loop. Method 
 * also used for C API callbacks.
 ********************************/
void StackInterpreter::Execute(size_t* op_stack, size_t* stack_pos, long i, StackMethod* method, size_t* instance, bool jit_called)
{
#ifdef _TIMING
  clock_t start = clock();
#endif

  // initial setup. ORDER MATTERS: the collector reads a monitor's current frame
  // as soon as call_stack_pos is >= 0, so the frame must be stored -- and the
  // store made visible -- before that. Setting call_stack_pos to 0 first left a
  // window in which a collection on another thread dereferenced a frame pointer
  // this thread had not written yet.
  (*stack_frame) = GetStackFrame(method, instance);
  if(stack_frame_monitor) {
    // register op_stack for young-gen fixup across threads
    stack_frame_monitor->op_stack = op_stack;
    stack_frame_monitor->stack_pos = stack_pos;
    // pairs with the acquire fence the mark and fixup phases take after reading
    // call_stack_pos (CheckPdaRoots / FixupRoots)
    std::atomic_thread_fence(std::memory_order_release);
    (*call_stack_pos) = 0;
  }
  
#ifdef _DEBUG
  std::wcout << L"creating frame=" << (*stack_frame) << std::endl;
#endif
  (*stack_frame)->jit_called = jit_called;
  if(jit_called) {
    bridge_call_pos = *call_stack_pos;
    bridge_deferred = false;
  }
  else if(is_callback) {
    // runs above the thread's frames: returns when the entry method returns to
    // this depth, and leaves an error a Try() beneath it would take to the C++
    // that called back (see CallBack), as a bridge does to compiled code
    base_call_pos = *call_stack_pos;
    bridge_call_pos = base_call_pos;
    bridge_deferred = false;
  }

#if !defined(_DEBUGGER) && !defined(_NO_JIT)
  // An entry method -- Main, a thread's Run -- is called once, so the auto-JIT's
  // call count never reaches it, and a hot loop written there ran interpreted for
  // the life of the program (~50x slower than the same loop in a helper). Compile
  // it on entry when it holds a loop and the JIT is on. A method the JIT rejects
  // (try regions, frame-dependent traps) stays interpreted exactly as before, and
  // the compiled path ends the way ProcessReturn ends the entry frame.
  if(!jit_called && i == 0 && JitEagerLoops() && JitCompiler::HasLoop(method)) {
    JitCompiler::TryAutoJitCompile(method);
    if(method->GetNativeCode()) {
      static const bool report = JitEnvFlag("OBJECK_JIT_REPORT");
      if(report) {
        std::wcerr << L"[jit] " << method->GetName() << L": compiled on entry (has a loop)" << std::endl;
      }
      JitRuntime jit_executor;
      const long status = jit_executor.Execute(method, instance, op_stack, stack_pos, call_stack, call_stack_pos, *stack_frame);
      if(status < 0) {
        // A guard stub's error is recoverable like the interpreter's own: the
        // compiled frame has already returned, so a Try() region below can take
        // over (#900). An invalid cast raised in a bridge callback arrives here
        // the same way since #925 -- as status -5, because the handler stack is
        // per-thread and the callback can report instead of exiting. Since
        // gap 3 a compiled callee's status arrives the same way, having
        // travelled outward one compiled frame at a time, so this is also where
        // an unguarded JIT-to-JIT failure is reported -- with the stack listing
        // below, which the old JitNativeCallError did not produce.
        if(HasTryHandler()) {
          RecordRecoveredStatus(status);
        }
        if(TryErrorRecovery(stack_pos, RECOVERED_NONE)) {
          return;
        }

        switch(status) {
        case -1:
          std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance in native JIT code <<<" << std::endl;
          break;

        case -2:
          std::wcerr << L">>> Index under bounds in native JIT code <<<" << std::endl;
          break;

        case -3:
          std::wcerr << L">>> Index over bounds in native JIT code <<<" << std::endl;
          break;

        case -4:
          // the same text the interpreter prints (S4)
          std::wcerr << OBJECK_DIVIDE_BY_ZERO_MESSAGE << std::endl;
          break;
        }
        StackErrorUnwind(method);
#ifdef _NO_HALT
        halt = true;
        return;
#else
        VmExit(1);
#endif
      }
      ReleaseStackFrame(*stack_frame);
      (*stack_frame) = nullptr;
      halt = true;
      return;
    }
  }
#endif

  StackInstr* instrs = (*stack_frame)->method->GetInstructions();
  long ip = i;

#ifdef _TIMING
  const std::wstring mthd_name = (*frame)->method->GetName();
#endif

#ifdef _DEBUG
  std::wcout << L"\n---------- Executing Interpreted Code: id=" 
        << (((*stack_frame)->method->GetClass()) ? (*stack_frame)->method->GetClass()->GetId() : -1) << L","
        << (*stack_frame)->method->GetId() << L"; method_name='" << (*stack_frame)->method->GetName() 
        << L"' ---------\n" << std::endl;
#endif

  // Setup dispatch context
  DispatchContext ctx;
  ctx.op_stack = op_stack;
  ctx.stack_pos = stack_pos;
  ctx.instrs = instrs;
  ctx.ip = &ip;
  ctx.stack_frame = stack_frame;
  ctx.call_stack = call_stack;
  ctx.call_stack_pos = call_stack_pos;
  ctx.interp = this;
  ctx.halt = &halt;

  // GC safepoint polling. Poll every 1024 opcodes so a thread in a no-allocation
  // loop still reaches a safepoint promptly (bounded collector wait) without a
  // per-opcode atomic on the hot path. Allocation is also a safepoint (below),
  // so allocation-heavy threads park immediately. Single-threaded runs never park.
  unsigned safepoint_ticks = 0;

  // execute using dispatch table
  do {
    if((++safepoint_ticks & 0x3FF) == 0) {
      MemoryManager::SafePoint();
    }
    StackInstr* instr = &instrs[ip++];
    ctx.instr = instr;

#ifdef _DEBUGGER
    // Null only when no debugger exists in the process (a plain obr build with
    // _DEBUGGER defined but no Debugger constructed). Spawned threads attach to
    // the active debugger now, so breakpoints inside a thread body fire.
    if(debugger) {
      debugger->ProcessInstruction(instr, ip, call_stack, (*call_stack_pos), (*stack_frame));
    }
    DispatchResult result = instr_dispatch[instr->GetType()](ctx);
    if(result == DispatchResult::RETURN_JIT) {
      return;
    }
    if(result == DispatchResult::HALT) {
      break;
    }
#else
    // Inline hot opcodes for performance — these use 'continue' to skip
    // the recovery check (they can't trigger errors or change instrs).
    // The default dispatch path falls through to the recovery check below.
    switch(instr->GetType()) {
    case LOAD_INT_LIT:
      op_stack[(*stack_pos)++] = (size_t)instr->GetInt64Operand();
      continue;

    case LOAD_LOCL_INT_VAR:
      op_stack[(*stack_pos)++] = (*stack_frame)->mem[instr->GetOperand() + 1];
      continue;

    case STOR_LOCL_INT_VAR:
      (*stack_frame)->mem[instr->GetOperand() + 1] = op_stack[--(*stack_pos)];
      continue;

    case COPY_LOCL_INT_VAR:
      (*stack_frame)->mem[instr->GetOperand() + 1] = op_stack[(*stack_pos) - 1];
      continue;

    // integer semantics (wrapping, masked shifts) live in shared/int_ops.h
    case ADD_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)objeck_int::Add((INT64_VALUE)op_stack[sp - 1], (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    case SUB_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)objeck_int::Sub((INT64_VALUE)op_stack[sp - 1], (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    case MUL_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)objeck_int::Mul((INT64_VALUE)op_stack[sp - 1], (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    case LOAD_CHAR_LIT:
      op_stack[(*stack_pos)++] = (size_t)instr->GetOperand();
      continue;

    // integer comparisons (pop 2, push 0/1)
    case EQL_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] == (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }
    case NEQL_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] != (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }
    case LES_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] < (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }
    case GTR_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] > (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }
    case LES_EQL_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] <= (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }
    case GTR_EQL_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] >= (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }

    // logical ops
    case AND_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] && (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }
    case OR_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (INT64_VALUE)op_stack[sp - 1] || (INT64_VALUE)op_stack[sp - 2];
      *stack_pos = sp - 1;
      continue;
    }

    // bitwise ops
    case BIT_AND_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)((INT64_VALUE)op_stack[sp - 1] & (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case BIT_OR_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)((INT64_VALUE)op_stack[sp - 1] | (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case BIT_XOR_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)((INT64_VALUE)op_stack[sp - 1] ^ (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    // shift ops
    case SHL_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)objeck_int::Shl((INT64_VALUE)op_stack[sp - 1], (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case SHR_INT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = (size_t)objeck_int::Sar((INT64_VALUE)op_stack[sp - 1], (INT64_VALUE)op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    // float arithmetic (operands bit-stored in op_stack slots; left=sp-1, right=sp-2,
    // matching AddFloat/SubFloat/MulFloat). DIV_FLOAT is NOT inlined — its
    // divide-by-zero path needs the recovery check below, same as DIV_INT/MOD_INT.
    case ADD_FLOAT: {
      const size_t sp = *stack_pos;
      *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) =
        *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) + *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case SUB_FLOAT: {
      const size_t sp = *stack_pos;
      *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) =
        *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) - *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case MUL_FLOAT: {
      const size_t sp = *stack_pos;
      *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) =
        *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) * *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    // float comparisons (pop 2, push 0/1)
    case LES_FLOAT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) < *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case GTR_FLOAT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) > *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case LES_EQL_FLOAT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) <= *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case GTR_EQL_FLOAT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) >= *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case EQL_FLOAT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) == *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }
    case NEQL_FLOAT: {
      const size_t sp = *stack_pos;
      op_stack[sp - 2] = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]) != *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
      *stack_pos = sp - 1;
      continue;
    }

    case LBL:
      continue;

    case JMP:
      if(instr->GetOperand2() < 0) {
        ip = instr->GetOperand();
      }
      else if((INT64_VALUE)op_stack[--(*stack_pos)] == instr->GetOperand2()) {
        ip = instr->GetOperand();
      }
      continue;

    default: {
      DispatchResult result = instr_dispatch[instr->GetType()](ctx);
      if(result == DispatchResult::RETURN_JIT) {
        return;
      }
      if(result == DispatchResult::HALT) {
        goto loop_exit;
      }
    }
    }
    // Only reached by default dispatch path (inlined opcodes skip via continue)
#endif
    // check for try error recovery
    long recovery_ip = CheckTryRecovery();
    if(recovery_ip >= 0) {
      ip = recovery_ip;
      ctx.instrs = (*stack_frame)->method->GetInstructions();
    }

    // instrs may have changed from method calls
    instrs = ctx.instrs;
  }
  while(!halt.load(std::memory_order_relaxed));
#ifndef _DEBUGGER
loop_exit: ;
#endif

  // A bridge that left an error to its compiled caller stopped on its entry
  // frame; release it as the return to compiled code does (#1171)
  if(bridge_deferred && *stack_frame) {
    (*stack_frame)->jit_called = false;
    ReleaseStackFrame(*stack_frame);
    *stack_frame = nullptr;
  }

#ifdef _TIMING
  clock_t end = clock();
  std::wcout << L"---------------------------" << std::endl;
  std::wcout << L"Dispatch method='" << mthd_name << L"', time=" << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
#endif
}

bool StackInterpreter::ProcessTrap(size_t* &op_stack, size_t* &stack_pos)
{
  if(TrapProcessor::ProcessTrap(program, (size_t*)(*stack_frame)->mem[0], op_stack, stack_pos, (*stack_frame))) {
    return true;
  }

  // A failing trap -- Runtime->Raise among them -- recovers under a ?-> handler
  // like any runtime error (#1247). It went straight to exit before.
  bool& recorded = TrapErrorRecorded();
  const bool had_record = recorded;
  recorded = false;
  if(HasTryHandler()) {
    return TryErrorRecovery(stack_pos, had_record ? RECOVERED_NONE : RECOVERED_OTHER,
                            had_record ? L"" : L"A runtime library call failed");
  }
  return false;
}

void StackInterpreter::StorLoclIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_LOCL_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif
  size_t* mem = (*stack_frame)->mem;
  mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
}

void StackInterpreter::StorClsInstIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_CLS_INST_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif

  size_t* cls_inst_mem = (size_t*)op_stack[(*stack_pos) - 1];
  if(!cls_inst_mem) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  size_t mem = op_stack[(*stack_pos) - 2];
  (*stack_pos) -= 2;
  cls_inst_mem[instr->GetOperand()] = mem;
  // Generational GC write barrier (skip for class static memory which has no GC header)
  if(instr->GetOperand2() != CLS) {
    MemoryManager::WriteBarrier(cls_inst_mem);
  }
}

void StackInterpreter::CopyLoclIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: COPY_LOCL_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif
  size_t* mem = (*stack_frame)->mem;
  mem[instr->GetOperand() + 1] = TopInt(op_stack, stack_pos);
}

void StackInterpreter::CopyClsInstIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: COPY_CLS_INST_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif

  size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
  if(!cls_inst_mem) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  cls_inst_mem[instr->GetOperand()] = TopInt(op_stack, stack_pos);
  // Generational GC write barrier (skip for class static memory which has no GC header)
  if(instr->GetOperand2() != CLS) {
    MemoryManager::WriteBarrier(cls_inst_mem);
  }
}

void StackInterpreter::Str2Int(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: S2I; call_pos=" << (*call_stack_pos) << std::endl;
#endif

  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  long base = (long)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    PushInt((size_t)ParseIntText((wchar_t*)(str_ptr + 3), static_cast<int>(base)), op_stack, stack_pos);
  }
  else {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
}

void StackInterpreter::Str2Float(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: S2F; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  
  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    const wchar_t* str = (wchar_t*)(str_ptr + 3);
    PushFloat(ParseFloatText(str), op_stack, stack_pos);
  }
  else {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
}

void StackInterpreter::ByteChar2Int(size_t*& op_stack, size_t*& stack_pos)
{
  int64_t value = (int64_t)PopInt(op_stack, stack_pos);
  if(value < UCHAR_MAX + 1) {
    value += UCHAR_MAX + 1;
  }
  else if(value < USHRT_MAX + 1) {
    value += USHRT_MAX + 1;
  }
  PushInt(value, op_stack, stack_pos);
}

void StackInterpreter::Int2Str(size_t* &op_stack, size_t* &stack_pos)
{
  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    wchar_t* str = (wchar_t*)(str_ptr + 3);
    const size_t base = PopInt(op_stack, stack_pos);
    const INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
    
    std::wstring conv;
    std::wstringstream formatter;

    const std::wstring int_format = program->GetProperty(L"int:string:format");
    if(!int_format.empty()) {
      if(int_format == L"dec") {
        formatter << std::dec;
      }
      else if(int_format == L"oct") {
        formatter << std::oct;
      }
      else if(int_format == L"hex") {
        formatter << std::hex;
      }

      formatter << value;
      conv = formatter.str();
    }
    else {
      switch(base) {
      case 8:
        formatter << std::oct;
        break;

      case 10:
        formatter << std::dec;
        break;

      case 16:
        formatter << std::hex << L"0x";
        break;
      }

      formatter << value;
      conv = formatter.str();
    }

		const size_t max = conv.size() < 32 ? conv.size() : 32;
#ifdef _WIN32
		wcsncpy_s(str, str_ptr[0], conv.c_str(), max);
#else
		wcsncpy(str, conv.c_str(), max);
#endif
  }
}

void StackInterpreter::Float2Str(size_t* &op_stack, size_t* &stack_pos)
{
  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    wchar_t* str = (wchar_t*)(str_ptr + 3);
    const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
		
    std::wstringstream formatter;
    std::wstring conv;

		const std::wstring float_format = program->GetProperty(L"float:string:format");
		const std::wstring float_precision = program->GetProperty(L"float:string:precision");
		// not std::stoll, which threw on a value that was not a number (common.h)
		int precision = 0;
		const bool has_precision = ParseFloatPrecision(float_precision, precision);

    if(!float_format.empty() && has_precision) {
      if(float_format == L"fixed") {
        formatter << std::fixed;
      }
      else if(float_format == L"scientific") {
        formatter << std::scientific;
      }
      else if(float_format == L"hex") {
        formatter << std::hexfloat;
      }
      formatter << std::setprecision(precision);
      
      formatter << value;
      conv = formatter.str();
    }
    else if(!float_format.empty()) {
			if(float_format == L"fixed") {
				formatter << std::fixed;
			}
			else if(float_format == L"scientific") {
				formatter << std::scientific;
			}
			else if(float_format == L"hex") {
				formatter << std::hexfloat;
			}

			formatter << value;
			conv = formatter.str();
    }
    else if(has_precision) {
			formatter << std::setprecision(precision);

			formatter << value;
			conv = formatter.str();
    }
    else {
      conv = std::to_wstring(value);
    }

    // The buffer is the caller's Char->New[64]: str_ptr[0] elements. Fixed-point
    // text of a large value does not fit -- 1e60 is 61 digits and ".000000" --
    // and copying 64 characters into it left no room for the terminator:
    // MSVC's invalid-parameter handler ended the process with no message, and
    // POSIX left the string unterminated (#1173). Such a value is written in
    // scientific notation instead, and the copy always keeps the terminator.
    const size_t capacity = str_ptr[0];
    if(conv.size() >= capacity) {
      std::wstringstream scientific;
      scientific << std::scientific << value;
      conv = scientific.str();
    }
    const size_t max = conv.size() < capacity ? conv.size() : (capacity > 0 ? capacity - 1 : 0);
#ifdef _WIN32
    wcsncpy_s(str, capacity, conv.c_str(), max);
#else
    wcsncpy(str, conv.c_str(), max);
    str[max] = L'\0';
#endif
  }
}

void StackInterpreter::ShlInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SHL_INT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = objeck_int::Shl(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::ShrInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SHR_INT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = objeck_int::Sar(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::LoadLoclIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_LOCL_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif
  size_t* mem = (*stack_frame)->mem;
  PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
}

void StackInterpreter::LoadClsInstIntVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CLS_INST_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif      
  size_t* cls_inst_mem = (size_t*)op_stack[(*stack_pos) - 1];
  if(!cls_inst_mem) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  op_stack[(*stack_pos) - 1] = cls_inst_mem[instr->GetOperand()];
}

void StackInterpreter::AndInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: AND; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left && right;
  *stack_pos = sp - 1;
}

void StackInterpreter::OrInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: OR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left || right;
  *stack_pos = sp - 1;
}

void StackInterpreter::AddInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: ADD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = objeck_int::Add(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::AddFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: ADD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) = left_double + right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::SubInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SUB; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = objeck_int::Sub(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::SubFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SUB; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) = left_double - right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::MulInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: MUL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = objeck_int::Mul(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::DivInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: DIV; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  if(right == 0) [[unlikely]] {
    if(TryErrorRecovery(stack_pos, RECOVERED_DIVIDE_BY_ZERO, OBJECK_DIVIDE_BY_ZERO_MESSAGE)) {
      return;
    }
    std::wcerr << OBJECK_DIVIDE_BY_ZERO_MESSAGE << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  // MIN / -1 wraps to MIN instead of faulting (shared/int_ops.h)
  op_stack[sp - 2] = objeck_int::Div(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::MulFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: MUL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) = left_double * right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::DivFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: DIV; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  if(right_double == 0.0) [[unlikely]] {
    if(TryErrorRecovery(stack_pos, RECOVERED_DIVIDE_BY_ZERO, OBJECK_DIVIDE_BY_ZERO_MESSAGE)) {
      return;
    }
    std::wcerr << OBJECK_DIVIDE_BY_ZERO_MESSAGE << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]) = left_double / right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::ModInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: MOD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  // modulus by zero raises the same hardware fault division does; without
  // this guard it died silently instead of trapping like DivInt
  if(right == 0) [[unlikely]] {
    if(TryErrorRecovery(stack_pos, RECOVERED_DIVIDE_BY_ZERO, OBJECK_DIVIDE_BY_ZERO_MESSAGE)) {
      return;
    }
    std::wcerr << OBJECK_DIVIDE_BY_ZERO_MESSAGE << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  // MIN % -1 is 0 instead of faulting (shared/int_ops.h)
  op_stack[sp - 2] = objeck_int::Mod(left, right);
  *stack_pos = sp - 1;
}

void StackInterpreter::BitAndInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_AND; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left & right;
  *stack_pos = sp - 1;
}

void StackInterpreter::BitOrInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_OR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left | right;
  *stack_pos = sp - 1;
}

void StackInterpreter::BitNotInt(size_t*& op_stack, size_t*& stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_NOT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  op_stack[sp - 1] = ~left;
}

void StackInterpreter::BitXorInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_XOR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left ^ right;
  *stack_pos = sp - 1;
}

void StackInterpreter::LesEqlInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left <= right;
  *stack_pos = sp - 1;
}

void StackInterpreter::GtrEqlInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left >= right;
  *stack_pos = sp - 1;
}

void StackInterpreter::LesEqlFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  op_stack[sp - 2] = left_double <= right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::GtrEqlFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  op_stack[sp - 2] = left_double >= right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::EqlInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left == right;
  *stack_pos = sp - 1;
}
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
void StackInterpreter::NeqlInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left != right;
  *stack_pos = sp - 1;
}

void StackInterpreter::LesInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left < right;
  *stack_pos = sp - 1;
}

void StackInterpreter::GtrInt(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const INT64_VALUE left = static_cast<INT64_VALUE>(op_stack[sp - 1]);
  const INT64_VALUE right = static_cast<INT64_VALUE>(op_stack[sp - 2]);
  op_stack[sp - 2] = left > right;
  *stack_pos = sp - 1;
}

void StackInterpreter::EqlFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  op_stack[sp - 2] = left_double == right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::NeqlFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  op_stack[sp - 2] = left_double != right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::LesFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  op_stack[sp - 2] = left_double < right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::GtrFloat(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR_FLOAT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const size_t sp = *stack_pos;  // Cache stack position locally
  const FLOAT_VALUE left_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 1]);
  const FLOAT_VALUE right_double = *reinterpret_cast<FLOAT_VALUE*>(&op_stack[sp - 2]);
  op_stack[sp - 2] = left_double > right_double;
  *stack_pos = sp - 1;
}

void StackInterpreter::LoadArySize(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_ARY_SIZE; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  PushInt(array[2], op_stack, stack_pos);
}

void StackInterpreter::CpyByteAry(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_BYTE_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const long length = (long)PopInt(op_stack, stack_pos);
  const long src_offset = (long)PopInt(op_stack, stack_pos);
  size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
  const long dest_offset = (long)PopInt(op_stack, stack_pos);
  size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

  if(!src_array || !dest_array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  const INT64_VALUE src_array_len = (INT64_VALUE)src_array[2];
  const INT64_VALUE dest_array_len = (INT64_VALUE)dest_array[2];
  if(length > 0 && src_offset >= 0 && dest_offset >= 0 && static_cast<long long>(src_offset) + length <= src_array_len && dest_offset + length <= dest_array_len) {
    const char* src_array_ptr = (char*)(src_array + 3);
    char* dest_array_ptr = (char*)(dest_array + 3);
    if(src_array_ptr == dest_array_ptr) {
      memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
    }
    else {
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
    }
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

void StackInterpreter::CpyCharAry(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_CHAR_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const long length = (long)PopInt(op_stack, stack_pos);
  const long src_offset = (long)PopInt(op_stack, stack_pos);
  size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
  const long dest_offset = (long)PopInt(op_stack, stack_pos);
  size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

  if(!src_array || !dest_array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  const long src_array_len = (long)src_array[2];
  const long dest_array_len = (long)dest_array[2];
  if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
    wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
    wchar_t* dest_array_ptr = (wchar_t*)(dest_array + 3);
    if(src_array_ptr == dest_array_ptr) {
      memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
    }
    else {
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
    }
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

void StackInterpreter::CpyIntAry(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_INT_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const long length = (long)PopInt(op_stack, stack_pos);
  const long src_offset = (long)PopInt(op_stack, stack_pos);
  size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
  const long dest_offset = (long)PopInt(op_stack, stack_pos);
  size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

  if(!src_array || !dest_array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  const long src_array_len = (long)src_array[0];
  const long dest_array_len = (long)dest_array[0];
  if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
    size_t* src_array_ptr = src_array + 3;
    size_t* dest_array_ptr = dest_array + 3;
    if(src_array_ptr == dest_array_ptr) {
      memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
    }
    else {
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
    }
    // Generational GC write barrier: an Int[] can hold object references (object
    // arrays are int arrays of pointers), so a bulk copy can deposit young refs into
    // an old destination array invisibly to a minor GC. Record the destination.
    MemoryManager::WriteBarrier(dest_array);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

void StackInterpreter::CpyFloatAry(size_t*& op_stack, size_t*& stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_FLOAT_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const long length = (long)PopInt(op_stack, stack_pos);
  const long src_offset = (long)PopInt(op_stack, stack_pos);
  size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
  const long dest_offset = (long)PopInt(op_stack, stack_pos);
  size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

  if(!src_array || !dest_array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  const long src_array_len = (long)src_array[0];
  const long dest_array_len = (long)dest_array[0];
  if(length > 0 && src_offset >= 0 && dest_offset >= 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
    size_t* src_array_ptr = src_array + 3;
    size_t* dest_array_ptr = dest_array + 3;
    if(src_array_ptr == dest_array_ptr) {
      memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
    }
    else {
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
    }
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

void StackInterpreter::ZeroByteAry(size_t*& op_stack, size_t*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  char* buffer = (char*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(char));
}

void StackInterpreter::ZeroCharAry(size_t*& op_stack, size_t*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  wchar_t* buffer = (wchar_t*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(wchar_t));
}

void StackInterpreter::ZeroIntAry(size_t*& op_stack, size_t*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  size_t* buffer = (size_t*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(size_t));
}

void StackInterpreter::ZeroFloatAry(size_t*& op_stack, size_t*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  FLOAT_VALUE* buffer = (FLOAT_VALUE*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(FLOAT_VALUE));
}

void StackInterpreter::ObjTypeOf(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
  size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
  if(mem) {
    const size_t* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(), program->GetHierarchy(), program->GetInterfaces());
    if(result) {
      PushInt(1, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    std::wcerr << L">>> TypeOf(..) check on Nil value <<<" << std::endl;
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
}

void StackInterpreter::ObjInstCast(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
  size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
  const size_t result = (size_t)MemoryManager::ValidObjectCast(mem, instr->GetOperand(), program->GetHierarchy(), program->GetInterfaces());
#ifdef _DEBUG
  std::wcout << L"stack oper: OBJ_INST_CAST: from=" << mem << L", to=" << instr->GetOperand() << std::endl;
#endif
  if(!result && mem) {
    // Recoverable like the other runtime errors: a?->b() yields Nil rather than
    // ending the program (#900).
    if(TryErrorRecovery(stack_pos, RECOVERED_INVALID_CAST, std::wstring(L"Invalid object cast: '") + (MemoryManager::GetClass((size_t*)mem) ? MemoryManager::GetClass((size_t*)mem)->GetName() : L"?") + L"' to '" + program->GetClass(instr->GetOperand())->GetName() + L"'")) {
      return;
    }

    StackClass* to_cls = MemoryManager::GetClass((size_t*)mem);
    std::wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
          << L"' to '" << program->GetClass(instr->GetOperand())->GetName() << L"' <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  PushInt(result, op_stack, stack_pos);
}

void StackInterpreter::AsyncMthdCall([[maybe_unused]] size_t* &op_stack, size_t* &stack_pos)
{
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  size_t* param = (size_t*)(*stack_frame)->mem[1];

  StackClass* impl_class = MemoryManager::GetClass(instance);
  if(!impl_class) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  std::wstring method_name = impl_class->GetName() + L":Run:o.System.Base,";
  StackMethod* called = impl_class->GetMethod(method_name);
  while(!called) {
    impl_class = impl_class->GetParent();
    method_name = impl_class->GetName() + L":Run:o.System.Base,";
    called = impl_class->GetMethod(method_name);
  }

#ifdef _DEBUG
  assert(called);
  std::wcout << L"=== ASYNC_MTHD_CALL: id=" << called->GetClass()->GetId() << L","
        << called->GetId() << L"; name='" << called->GetName()
        << L"'; param=" << param << L" ===" << std::endl;
#endif

  // create and execute the new thread
  // make sure that calls to the model are synced.  Are find method synced?
  ProcessAsyncMethodCall(called, param);
}

void StackInterpreter::ThreadJoin([[maybe_unused]] size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: THREAD_JOIN; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  if(!instance) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  // The handle is read BEFORE parking. Between BeginBlocking and the read this
  // thread counts as parked, so another thread's collection can run to completion:
  // it promotes this Thread object out of the nursery and fixes up the frame's self
  // slot, but not the C++ local 'instance' taken above, which then points into the
  // recycled nursery. Reading instance[0] there returned whatever reused that memory
  // -- once on macOS CI a handle of 0x4, and pthread_join answered ESRCH (#874). The
  // handle value itself never moves, so reading it first is enough.
#ifdef _WIN32
  HANDLE vm_thread = (HANDLE)instance[0];

  // Joining blocks in a syscall — count this thread as parked so a collection on
  // another thread doesn't wait forever for it to reach a safepoint.
  MemoryManager::BeginBlocking();
  const DWORD wait_result = WaitForSingleObject(vm_thread, INFINITE);
  if(wait_result != WAIT_OBJECT_0) {
    // captured before EndBlocking so nothing can overwrite it (#874)
    const DWORD last_error = GetLastError();
    MemoryManager::EndBlocking();
    std::wcerr << L">>> Unable to join thread! <<<" << std::endl;
    std::wcerr << L"  wait result=" << wait_result << L", GetLastError=" << last_error
               << L", handle=0x" << std::hex << (size_t)vm_thread << L", thread object=0x" << (size_t)instance
               << std::dec << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }
#else
  void* status;
  pthread_t vm_thread = (pthread_t)instance[0];

  // see the note above: the handle is read before this thread parks
  MemoryManager::BeginBlocking();
  const int join_result = pthread_join(vm_thread, &status);
  if(join_result) {
    MemoryManager::EndBlocking();
    // the return code is the diagnosis: ESRCH (not a live thread), EINVAL (not
    // joinable, or already joined), EDEADLK (joining itself) (#874)
    std::wcerr << L">>> Unable to join thread! <<<" << std::endl;
    std::wcerr << L"  pthread_join=" << join_result << L" (" << strerror(join_result) << L"), handle=0x"
               << std::hex << (size_t)vm_thread << L", thread object=0x" << (size_t)instance << std::dec << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }
#ifdef __linux__
  // the stack the VM allocated for it (ProcessAsyncMethodCall)
  ThreadStacks::Release(vm_thread);
#endif
#endif
  MemoryManager::EndBlocking();
}

void StackInterpreter::ThreadMutex([[maybe_unused]] size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: THREAD_MUTEX; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  if(!instance) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  ThreadMutexCreate(instance);
}

void StackInterpreter::CriticalStart(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CRITICAL_START; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!instance) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif        
  }
  ThreadMutexLock(instance);
}

void StackInterpreter::CriticalEnd(size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CRITICAL_END; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!instance) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  ThreadMutexUnlock(instance);
}

/********************************
 * Processes a load function variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFunctionVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_FUNC_VAR; index=" << instr->GetOperand()
        << L"; local=" << ((instr->GetOperand2() == LOCL) ? L"true" : L"false") << std::endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    size_t* mem = (*stack_frame)->mem;
    PushInt(mem[instr->GetOperand() + 2], op_stack, stack_pos);
    PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
  } 
  else {
    size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
        return;
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      VmExit(1);
#endif
    }
    PushInt(cls_inst_mem[instr->GetOperand() + 1], op_stack, stack_pos);
    PushInt(cls_inst_mem[instr->GetOperand()], op_stack, stack_pos);
  }
}

/********************************
 * Processes a load float variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloat(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_FLOAT_VAR; index=" << instr->GetOperand()
        << L"; local=" << ((instr->GetOperand2() == LOCL) ? L"true" : L"false") << std::endl;
#endif
  FLOAT_VALUE value;
  if(instr->GetOperand2() == LOCL) {
    size_t* mem = (*stack_frame)->mem;
    value = *((FLOAT_VALUE*)(&mem[instr->GetOperand() + 1]));

  } else {
    size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
        return;
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      VmExit(1);
#endif
    }
    value = *((FLOAT_VALUE*)(&cls_inst_mem[instr->GetOperand()]));
  }
  PushFloat(value, op_stack, stack_pos);
}

/********************************
 * Processes a store function variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFunctionVar(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_FUNC_VAR; index=" << instr->GetOperand()
        << L"; local=" << ((instr->GetOperand2() == LOCL) ? L"true" : L"false") << std::endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    size_t* mem = (*stack_frame)->mem;
    mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
    mem[instr->GetOperand() + 2] = PopInt(op_stack, stack_pos);
  } 
  else {
    size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
        return;
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      VmExit(1);
#endif
    }
    cls_inst_mem[instr->GetOperand()] = PopInt(op_stack, stack_pos);
    cls_inst_mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
    // Generational GC write barrier (skip for class static memory which has no GC header)
    if(instr->GetOperand2() != CLS) {
      MemoryManager::WriteBarrier(cls_inst_mem);
    }
  }
}

/********************************
 * Processes a store float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloat(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_FLOAT_VAR; index=" << instr->GetOperand()
        << L"; local=" << ((instr->GetOperand2() == LOCL) ? L"true" : L"false") << std::endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
    size_t* mem = (*stack_frame)->mem;
    *((FLOAT_VALUE*)(&mem[instr->GetOperand() + 1])) = value;
  } 
  else {
    size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
        return;
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      VmExit(1);
#endif
    }
    const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
    *((FLOAT_VALUE*)(&cls_inst_mem[instr->GetOperand()])) = value;
  }
}

/********************************
 * Processes a copy float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessCopyFloat(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: COPY_FLOAT_VAR; index=" << instr->GetOperand()
        << L"; local=" << ((instr->GetOperand2() == LOCL) ? L"true" : L"false") << std::endl;
#endif
  if(instr->GetOperand2() == LOCL) {
    FLOAT_VALUE value = TopFloat(op_stack, stack_pos);
    size_t* mem = (*stack_frame)->mem;
    *((FLOAT_VALUE*)(&mem[instr->GetOperand() + 1])) = value;
  } else {
    size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
    if(!cls_inst_mem) {
      if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
        return;
      }
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      VmExit(1);
#endif
    }
    FLOAT_VALUE value = TopFloat(op_stack, stack_pos);
    *((FLOAT_VALUE*)(&cls_inst_mem[instr->GetOperand()])) = value;
  }
}

/********************************
 * Processes a new object instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewObjectInstance(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_OBJ_INST: id=" << instr->GetOperand() << std::endl;
#endif

  size_t inst_mem = (size_t)MemoryManager::AllocateObject(instr->GetOperand(), op_stack, *stack_pos);
  PushInt(inst_mem, op_stack, stack_pos);
}

/********************************
 * Processes a new object instance
 * request.
 ********************************/
void StackInterpreter::ProcessNewFunctionInstance(StackInstr* instr, size_t*& op_stack, size_t*& stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_FUNC_INST: mem_size=" << instr->GetOperand() << std::endl;
#endif

  const long size = (long)instr->GetOperand();
  size_t func_mem = (size_t)MemoryManager::AllocateArray(size, BYTE_ARY_TYPE, op_stack, *stack_pos);
  PushInt(func_mem, op_stack, stack_pos);
}

/********************************
 * Processes a new array instance request.
 ********************************/
void StackInterpreter::ProcessNewArray(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos, bool is_float)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_INT_ARY/NEW_FLOAT_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t indices[8]{};
  const size_t value = PopInt(op_stack, stack_pos);
  // a negative dimension is a huge size_t; with a zero beside it the product
  // would pass the overflow checks (#1181)
  if((int64_t)value < 0) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }
  size_t size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    const size_t value = PopInt(op_stack, stack_pos);
    // overflow-safe accumulation; AllocateArray applies the final element-size guard
    if((int64_t)value < 0 || (value != 0 && size > (~(size_t)0) / value)) {
      std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
      VmExit(1);
    }
    size *= value;
    indices[dim++] = value;
  }

  // Pass the full size_t element count (no narrowing cast — a 32-bit 'long'
  // truncation would under-allocate while mem[0] below stays huge → OOB).
  if(size > (~(size_t)0) - (size_t)dim - 2) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }
  size_t* mem = is_float ?
    (size_t*)MemoryManager::AllocateArray(size + (size_t)dim + 2, FLOAT_TYPE, op_stack, *stack_pos) :
    (size_t*)MemoryManager::AllocateArray(size + (size_t)dim + 2, INT_TYPE, op_stack, *stack_pos);
  mem[0] = size;
  mem[1] = dim;

  memcpy(mem + 2, indices, dim * sizeof(size_t));
  PushInt((size_t)mem, op_stack, stack_pos);
}

/********************************
 * Processes a new byte array instance request.
 ********************************/
void StackInterpreter::ProcessNewByteArray(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_BYTE_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t indices[8]{};
  const size_t value = PopInt(op_stack, stack_pos);
  // a negative dimension is a huge size_t; with a zero beside it the product
  // would pass the overflow checks (#1181)
  if((int64_t)value < 0) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }
  size_t size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    const size_t value = PopInt(op_stack, stack_pos);
    if((int64_t)value < 0 || (value != 0 && size > (~(size_t)0) / value)) {
      std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
      VmExit(1);
    }
    size *= value;
    indices[dim++] = value;
  }

  // null terminated string
  size++;
  // overflow-safe header addition; no narrowing cast (a 32-bit 'long' truncation
  // would under-allocate while mem[0] stays huge → OOB).
  if(size == 0 || size > (~(size_t)0) - (static_cast<size_t>(dim) + 2) * sizeof(size_t)) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }
  size_t* mem = MemoryManager::AllocateArray(size + ((static_cast<size_t>(dim) + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size - 1;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(size_t));
  PushInt((size_t)mem, op_stack, stack_pos);
}

/********************************
 * Processes a new char array instance request.
 ********************************/
void StackInterpreter::ProcessNewCharArray(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_CHAR_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t indices[8]{};
  const size_t value = PopInt(op_stack, stack_pos);
  // a negative dimension is a huge size_t; with a zero beside it the product
  // would pass the overflow checks (#1181)
  if((int64_t)value < 0) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }
  size_t size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    const size_t value = PopInt(op_stack, stack_pos);
    if((int64_t)value < 0 || (value != 0 && size > (~(size_t)0) / value)) {
      std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
      VmExit(1);
    }
    size *= value;
    indices[dim++] = value;
  }

  // null-terminated string
  size++;
  // overflow-safe header addition; no narrowing cast (a 32-bit 'long' truncation
  // would under-allocate while mem[0] stays huge → OOB).
  if(size == 0 || size > (~(size_t)0) - (static_cast<size_t>(dim) + 2) * sizeof(size_t)) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    VmExit(1);
  }
  size_t* mem = MemoryManager::AllocateArray(size + ((static_cast<size_t>(dim) + 2) * sizeof(size_t)), CHAR_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size - 1;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(size_t));
  PushInt((size_t)mem, op_stack, stack_pos);
}

/********************************
 * Processes a return instruction, 
 * this modifies the call std::stack.
 ********************************/
void StackInterpreter::ProcessReturn(StackInstr* &instrs, long &ip)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: RTRN; call_pos=" << (*call_stack_pos) << std::endl;
#endif

  // unregister old frame
#ifdef _DEBUG
  std::wcout << L"removing frame=" << (*stack_frame) << std::endl;
#endif
  
  ReleaseStackFrame(*stack_frame);
  
  // restore previous frame
  if(!StackEmpty()) {
    (*stack_frame) = PopFrame();
    instrs = (*stack_frame)->method->GetInstructions();
    ip = (*stack_frame)->ip;
  } 
  else {
    (*stack_frame) = nullptr;
    halt = true;
  }
}

/********************************
 * Processes a asynchronous method call.
 ********************************/
void StackInterpreter::ProcessAsyncMethodCall(StackMethod* called, size_t* param)
{
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  ThreadHolder* holder = new ThreadHolder;
  holder->called = called;
  holder->param = param;
  holder->self = instance;

  // self/param are raw object pointers living in this heap holder until the child
  // installs them into its own scannable roots inside Execute. Register them so the
  // moving collector marks AND relocates them across any collection during the spawn
  // handoff; otherwise a promotion between here and the child reading them leaves the
  // child a stale (interior) pointer that a later minor GC marks-but-cannot-forward,
  // writing a bare GC mark bit into the self slot -> access violation. Unregistered in
  // AsyncMethodCall at teardown. No collection can run between these stores and the
  // registration (no allocation/safepoint), so the values cannot move in the gap.
  MemoryManager::AddPendingThreadRoot(&holder->self);
  MemoryManager::AddPendingThreadRoot(&holder->param);

#ifdef _WIN32
  // The native stack is sized for the maximum call depth (call_stack.h):
  // reserved, not committed. The 1 MB default held about 1,800 compiled frames.
  HANDLE vm_thread = (HANDLE)_beginthreadex(nullptr, (unsigned)NativeThreadStackBytes(), AsyncMethodCall, holder,
                                            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if(!vm_thread) {
    std::wcerr << L">>> Internal error: Unable to create garbage collection thread! <<<" << std::endl;
    VmExit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
  // 8 MB, or more for a --call-depth above the default (call_stack.h); macOS's
  // own default is 512 KB
  const size_t native_stack = NativeThreadStackBytes();
#ifdef __linux__
  // allocated here with huge pages off, freed at the join (call_stack.h)
  size_t stack_mapped = 0;
  void* stack_base = native_stack ? ThreadStacks::Allocate(&attrs, native_stack, stack_mapped) : nullptr;
  if(native_stack && !stack_base) {
    pthread_attr_setstacksize(&attrs, native_stack);
  }
#else
  if(native_stack) {
    pthread_attr_setstacksize(&attrs, native_stack);
  }
#endif

  // execute thread
  pthread_t vm_thread;
  if(pthread_create(&vm_thread, &attrs, AsyncMethodCall, (void*)holder)) {
    std::wcerr << L">>> Internal error: Internal error: Unable to create runtime thread! <<<" << std::endl;
    VmExit(-1);
  }
  pthread_attr_destroy(&attrs);
#ifdef __linux__
  if(stack_base) {
    ThreadStacks::Record(vm_thread, stack_base, stack_mapped);
  }
#endif
#endif
  
  // assign thread ID
  if(!instance) {
    std::wcerr << L">>> Internal error: Unable to create runtime thread! <<<" << std::endl;
    VmExit(-1);
  }

  instance[0] = (size_t)vm_thread;
#ifdef _DEBUG
  std::wcout << L"*** New Thread ID: " << vm_thread  << L": " << instance << L" ***" << std::endl;
#endif
}

#ifdef _WIN32
//
// windows thread callback
//
unsigned int WINAPI StackInterpreter::AsyncMethodCall(LPVOID arg)
{
  ThreadHolder* holder = (ThreadHolder*)arg;

  // execute
  size_t* thread_stack_pos = new size_t;
  (*thread_stack_pos) = 0;

#ifdef _DEBUG
  HANDLE vm_thread;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &vm_thread, 0, TRUE, DUPLICATE_SAME_ACCESS);
  std::wcout << L"# Starting thread=" << vm_thread << L" #" << std::endl;
#endif

  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter;
  // owned by the interpreter's call stack, reserved with it (call_stack.h)
  size_t* thread_op_stack = intpr->GetOpStack();
  AddThread(intpr);
  MemoryManager::RegisterMutator();
  MemoryManager::SafePoint();   // park immediately if a collection is already underway
  // Read self/param from the GC-tracked holder at the last GC-safe moment. The
  // pending-thread-root registration (ProcessAsyncMethodCall) kept them relocated
  // across every collection during the spawn handoff, so these reads see the current
  // (post-promotion) addresses rather than stale nursery pointers. Execute's prologue
  // wires op_stack/cur_frame before any allocation or safepoint, so the param placed
  // here becomes GC-scannable before this thread can park -> it cannot go stale.
  thread_op_stack[(*thread_stack_pos)++] = (size_t)holder->param;
  intpr->Execute(thread_op_stack, thread_stack_pos, 0, holder->called, holder->self, false);
  MemoryManager::UnregisterMutator();

#ifdef _DEBUG
  std::wcout << L"# final std::stack: pos=" << (*thread_stack_pos) << L", thread=" << vm_thread << L" #" << std::endl;
#endif

  // clean up — ORDER MATTERS. Remove this thread's GC roots (its monitor, via
  // ~StackInterpreter -> RemovePdaMethodRoot) BEFORE freeing the op/stack memory
  // those roots point to. RemovePdaMethodRoot takes pda_monitor_lock, which the
  // collector also holds while scanning/fixing monitors, so once delete intpr
  // returns no collector can reach this thread's op_stack/stack_pos. Freeing them
  // first (the previous order) let a collection on another thread read a freed
  // stack_pos (garbage length) and fix up a freed op_stack -> use-after-free:
  // garbage values and crashes that scale with thread-teardown churn.
  RemoveThread(intpr);
  delete intpr;
  intpr = nullptr;
  // freed with intpr, after its roots were removed
  thread_op_stack = nullptr;

  delete thread_stack_pos;
  thread_stack_pos = nullptr;

  // The child's own frame/op_stack covered self/param for the whole run; drop the
  // spawn-handoff roots before freeing the holder they point into.
  MemoryManager::RemovePendingThreadRoot(&holder->self);
  MemoryManager::RemovePendingThreadRoot(&holder->param);
  delete holder;
  holder = nullptr;

  return 0;
}
#else
//
// posix thread callback
//
void* StackInterpreter::AsyncMethodCall(void* arg)
{
  ThreadHolder* holder = (ThreadHolder*)arg;

  // execute
  size_t* thread_stack_pos = new size_t;
  (*thread_stack_pos) = 0;

#ifdef _DEBUG
  std::wcout << L"# Starting thread=" << pthread_self() << L" #" << std::endl;
#endif

  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter;
  // owned by the interpreter's call stack, reserved with it (call_stack.h)
  size_t* thread_op_stack = intpr->GetOpStack();
  AddThread(intpr);
  MemoryManager::RegisterMutator();
  MemoryManager::SafePoint();   // park immediately if a collection is already underway
  // Read self/param from the GC-tracked holder at the last GC-safe moment. The
  // pending-thread-root registration (ProcessAsyncMethodCall) kept them relocated
  // across every collection during the spawn handoff, so these reads see the current
  // (post-promotion) addresses rather than stale nursery pointers. Execute's prologue
  // wires op_stack/cur_frame before any allocation or safepoint, so the param placed
  // here becomes GC-scannable before this thread can park -> it cannot go stale.
  thread_op_stack[(*thread_stack_pos)++] = (size_t)holder->param;
  intpr->Execute(thread_op_stack, thread_stack_pos, 0, holder->called, holder->self, false);
  MemoryManager::UnregisterMutator();

#ifdef _DEBUG
  std::wcout << L"# final std::stack: pos=" << (*thread_stack_pos) << L", thread=" << pthread_self() << L" #" << std::endl;
#endif
  
  // clean up — ORDER MATTERS. Remove this thread's GC roots (its monitor, via
  // ~StackInterpreter -> RemovePdaMethodRoot) BEFORE freeing the op/stack memory
  // those roots point to. RemovePdaMethodRoot takes pda_monitor_lock, which the
  // collector also holds while scanning/fixing monitors, so once delete intpr
  // returns no collector can reach this thread's op_stack/stack_pos. Freeing them
  // first (the previous order) let a collection on another thread read a freed
  // stack_pos (garbage length) and fix up a freed op_stack -> use-after-free:
  // garbage values and crashes that scale with thread-teardown churn.
  RemoveThread(intpr);
  delete intpr;
  intpr = nullptr;
  // freed with intpr, after its roots were removed
  thread_op_stack = nullptr;

  delete thread_stack_pos;
  thread_stack_pos = nullptr;

  // The child's own frame/op_stack covered self/param for the whole run; drop the
  // spawn-handoff roots before freeing the holder they point into.
  MemoryManager::RemovePendingThreadRoot(&holder->self);
  MemoryManager::RemovePendingThreadRoot(&holder->param);
  delete holder;
  holder = nullptr;

  return nullptr;
}
#endif

#ifndef _NO_JIT
/********************************
 * Auto-JIT: check if a method should be JIT-compiled.
 * Kept out-of-line to avoid icache bloat in the dispatch loop.
 ********************************/
#ifdef _MSC_VER
__declspec(noinline) void StackInterpreter::CheckAutoJit(StackMethod* called, StackInstr* instr)
#else
void __attribute__((noinline)) StackInterpreter::CheckAutoJit(StackMethod* called, StackInstr* instr)
#endif
{
  // Already compiled elsewhere: nothing to count (the *_JIT opcode / operand3
  // patch drives dispatch from here on).
  if(called->GetNativeCode()) {
    return;
  }

  // A callee with a loop is compiled on its first call, not its tenth. Main is
  // the case that matters: the program's real entry is the loader's
  // $Initialization$ routine, which calls Main exactly once, so a hot loop
  // written in Main never crossed the call-count threshold and ran interpreted
  // for the life of the program. The same holds for any once-called driver.
  // Straight-line methods keep the counted threshold; a raised threshold turns
  // this off (JitEagerLoops), and a callee the JIT rejects is marked as before.
  if(called->GetJitCallCount() == 0 && JitEagerLoops() && JitCompiler::HasLoop(called)) {
    if(!JitCompiler::TryAutoJitCompile(called)) {
      instr->SetOperand3(-1);
    }
    return;
  }

  // Once the threshold is reached we stop bumping the counter (no unbounded
  // growth / overflow) and just consult the shared compile state.
  if(called->GetJitCallCount() >= JIT_AUTO_THRESHOLD) {
    if(!JitCompiler::TryAutoJitCompile(called)) {
      instr->SetOperand3(-1);   // definitively failed: interpret this site
    }
    return;
  }

  // fetch_add returns the previous value, so exactly one thread observes the
  // increment that crosses the threshold and triggers the (compile-once) compile.
  const long prev = called->IncrementJitCallCount();
  if(prev + 1 == JIT_AUTO_THRESHOLD) {
    if(!JitCompiler::TryAutoJitCompile(called)) {
      // JIT failed: mark this call site so we skip auto-JIT on future calls
      instr->SetOperand3(-1);
    }
    // success: PatchCallSites already set operand3=1 on all call sites
  }
}
#endif

/********************************
 * Processes a synchronous dynamic method call.
 ********************************/
/********************************
 * A call through an unassigned function variable: its value word is 0,
 * since every real function value carries FUNC_VALUE_TAG. Calling it ran
 * class 0's method 0 and usually segfaulted with no message (#1183); it is
 * a Nil dereference, recoverable under Try() like any other. Returns true
 * when it handled the call.
 ********************************/
bool StackInterpreter::NilFunctionValue(size_t* &op_stack, size_t* &stack_pos)
{
  if(op_stack[(*stack_pos) - 1] != 0) {
    return false;
  }

  // the value's two words: the method word and the instance word
  (*stack_pos) -= 2;
  if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
    return true;
  }

  std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
  std::wcerr << L"  the function variable was never assigned" << std::endl;
  StackErrorUnwind();
#ifdef _NO_HALT
  halt = true;
  return true;
#else
  VmExit(1);
  return true;
#endif
}

void StackInterpreter::ProcessDynamicMethodCall(StackInstr* instr, StackInstr* &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos)
{
  if(NilFunctionValue(op_stack, stack_pos)) {
    return;
  }

  // save current method
  (*stack_frame)->ip = ip;
  if(!PushFrame((*stack_frame), stack_pos)) {
    if(TryErrorRecovery(stack_pos, RECOVERED_OTHER, L"call stack bounds have been exceeded!")) {
      return;
    }
    std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  // make call
  const size_t mthd_cls_id = PopInt(op_stack, stack_pos);
  const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
  const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;

  if(mthd_id < 0 || cls_id < 0) {
    std::wcerr << L"Internal VM error." << std::endl;
    VmExit(1);
  }

  // pop instance
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"stack oper: DYN_MTHD_CALL; cls_mtd_id=" << cls_id << L"," << mthd_id << std::endl;
#endif
  StackMethod* called = program->GetClass(cls_id)->GetMethod(mthd_id);
#ifdef _DEBUG
  std::wcout << L"=== Binding function call: to: '" << called->GetName() << L"' ===" << std::endl;
#endif

#ifndef _NO_JIT
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
  }
  else {
    CheckAutoJit(called, instr);
    if(instr->GetType() == DYN_MTHD_CALL_JIT) {
      ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
    }
    else {
      (*stack_frame) = GetStackFrame(called, instance);
      instrs = (*stack_frame)->method->GetInstructions();
      ip = 0;
    }
  }
#else
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
#endif
}

/********************************
 * Processes a synchronous method call.
 ********************************/
/********************************
 * Cold path: virtual method resolution + auto-JIT.
 * Kept noinline to reduce icache pressure on the dispatch hot path.
 ********************************/
#ifdef _MSC_VER
__declspec(noinline) StackMethod* StackInterpreter::ResolveVirtualMethod(
#else
StackMethod* __attribute__((noinline, cold)) StackInterpreter::ResolveVirtualMethod(
#endif
    StackMethod* concrete_call, size_t* instance, StackInstr* instr,
    [[maybe_unused]] StackInstr* &instrs, [[maybe_unused]] long &ip, size_t* &stack_pos)
{
  StackClass* concrete_class = MemoryManager::GetClass((size_t*)instance);
  if(!concrete_class) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance")) {
      return nullptr;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return nullptr;
#else
    VmExit(1);
#endif
  }

  return ResolveVirtualTarget(concrete_class, concrete_call, instr->GetOperand(), instr->GetOperand2());
}

/********************************
 * The override a virtual declaration reaches for a receiver of
 * concrete_class. The class caches the answer by (virtual class id, virtual
 * method id); a miss walks the receiver's class and its parents by the
 * method's name. The entry goes on the receiver's class -- the class
 * GetVirtualMethod is asked about next time -- where it used to go on the
 * class the walk ended at, so an inherited override missed on every call.
 ********************************/
StackMethod* StackInterpreter::ResolveVirtualTarget(StackClass* concrete_class, StackMethod* concrete_call, const long virtual_cls_id, const long virtual_mthd_id)
{
  StackMethod* virtual_call = concrete_class->GetVirtualMethod(virtual_cls_id, virtual_mthd_id);
  if(!virtual_call) {
    const std::wstring qualified_method_name = concrete_call->GetName();
    const std::wstring method_ending = qualified_method_name.substr(qualified_method_name.find(L':'));

    StackClass* klass = concrete_class;
    std::wstring method_name = klass->GetName() + method_ending;
    virtual_call = klass->GetMethod(method_name);
    while(!virtual_call) {
      klass = klass->GetParent();
      method_name = klass->GetName() + method_ending;
      virtual_call = klass->GetMethod(method_name);
    }
    concrete_class->AddVirutalMethod(virtual_cls_id, virtual_mthd_id, virtual_call);
  }
#ifdef _DEBUG
  assert(virtual_call);
#endif
  return virtual_call;
}

void StackInterpreter::ProcessMethodCall(StackInstr* instr, StackInstr* &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos)
{
  // save current method
  (*stack_frame)->ip = ip;
  if(!PushFrame((*stack_frame), stack_pos)) {
    if(TryErrorRecovery(stack_pos, RECOVERED_OTHER, L"call stack bounds have been exceeded!")) {
      return;
    }
    std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  // pop instance
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  // make call
  StackMethod* concrete_call = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());

  // virtual method call -- resolve in cold path
  if(concrete_call->IsVirtual()) {
    concrete_call = ResolveVirtualMethod(concrete_call, instance, instr, instrs, ip, stack_pos);
    if(!concrete_call) return;
  }

#ifndef _NO_JIT
  // MTHD_CALL dispatch: operand3 > 0 means JIT compiled, < 0 means failed,
  // 0 means not yet attempted. Failed methods go straight to interpreter
  // to avoid re-attempting Compile() on every call.
  if(instr->GetOperand3() > 0) {
    ProcessJitMethodCall(concrete_call, instance, instrs, ip, op_stack, stack_pos);
  }
  else if(instr->GetOperand3() == 0) {
    // auto-JIT counting (first 10 calls only, noinline)
    CheckAutoJit(concrete_call, instr);
    // Take the JIT path when the callee has native code, not only when this
    // site was rewritten to MTHD_CALL_JIT: PatchCallSites walks the program's
    // classes, and the loader's $Initialization$ routine -- the one caller of
    // Main -- belongs to none, so its call site is never rewritten. Dispatching
    // on the opcode alone sent a freshly compiled Main to the interpreter for
    // its one and only call.
    if(instr->GetType() == MTHD_CALL_JIT || concrete_call->GetNativeCode()) {
      ProcessJitMethodCall(concrete_call, instance, instrs, ip, op_stack, stack_pos);
    }
    else {
      ProcessInterpretedMethodCall(concrete_call, instance, instrs, ip);
    }
  }
  else {
    // operand3 < 0: auto-JIT failed, always interpret
    ProcessInterpretedMethodCall(concrete_call, instance, instrs, ip);
  }
#else
  ProcessInterpretedMethodCall(concrete_call, instance, instrs, ip);
#endif
}

/********************************
 * JIT-only method call (dispatched via MTHD_CALL_JIT opcode).
 * No auto-JIT checks, no operand3 branches -- straight to JIT.
 ********************************/
void StackInterpreter::ProcessJitOnlyMethodCall(StackInstr* instr, StackInstr* &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos)
{
  (*stack_frame)->ip = ip;
  if(!PushFrame((*stack_frame), stack_pos)) {
    if(TryErrorRecovery(stack_pos, RECOVERED_OTHER, L"call stack bounds have been exceeded!")) {
      return;
    }
    std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  StackMethod* concrete_call = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());

  if(concrete_call->IsVirtual()) {
    concrete_call = ResolveVirtualMethod(concrete_call, instance, instr, instrs, ip, stack_pos);
    if(!concrete_call) return;
  }

  ProcessJitMethodCall(concrete_call, instance, instrs, ip, op_stack, stack_pos);
}

/********************************
 * JIT-only dynamic method call (dispatched via DYN_MTHD_CALL_JIT opcode).
 ********************************/
void StackInterpreter::ProcessJitOnlyDynamicMethodCall([[maybe_unused]] StackInstr* instr, StackInstr* &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos)
{
  if(NilFunctionValue(op_stack, stack_pos)) {
    return;
  }

  (*stack_frame)->ip = ip;
  if(!PushFrame((*stack_frame), stack_pos)) {
    if(TryErrorRecovery(stack_pos, RECOVERED_OTHER, L"call stack bounds have been exceeded!")) {
      return;
    }
    std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  const size_t mthd_cls_id = PopInt(op_stack, stack_pos);
  const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
  const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;

  if(mthd_id < 0 || cls_id < 0) {
    std::wcerr << L"Internal VM error." << std::endl;
    VmExit(1);
  }

  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  StackMethod* called = program->GetClass(cls_id)->GetMethod(mthd_id);

  ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessJitMethodCall(StackMethod* called, size_t* instance, StackInstr* &instrs, long &ip, size_t* &op_stack, size_t* &stack_pos)
{
#if defined(_DEBUGGER) || defined(_NO_JIT)
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
#else
  // Compile if needed, via the shared compile-once path (claims the method so two
  // threads never compile/patch it concurrently). If it is still not available --
  // a definitive failure, or another thread is mid-compile / patched a call site
  // before publishing native_code -- safely fall back to interpreting this call.
  if(!called->GetNativeCode()) {
    JitCompiler::TryAutoJitCompile(called);
    if(!called->GetNativeCode()) {
      ProcessInterpretedMethodCall(called, instance, instrs, ip);
#ifdef _DEBUG
      std::wcerr << L"### Interpreting (native code unavailable): " << called->GetName() << L" ###" << std::endl;
#endif
      return;
    }
  }

  // A compiled method runs on the native stack, which can run out before the
  // call stack does (call_stack.h). That is a full call stack too: the same
  // report, and recoverable the same way. The caller's frame is pushed;
  // recovery pops it with the rest.
  if(CallStack::FromPos(call_stack_pos)->NativeStackLow()) {
    if(TryErrorRecovery(stack_pos, RECOVERED_OTHER, L"call stack bounds have been exceeded!")) {
      return;
    }
    std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  // execute
  (*stack_frame) = GetStackFrame(called, instance);
  JitRuntime jit_executor;
  const long status = jit_executor.Execute(called, instance, op_stack, stack_pos, call_stack, call_stack_pos, *stack_frame);
  if(status < 0) {
    // Recoverable, as above (#900).
    if(HasTryHandler()) {
      RecordRecoveredStatus(status);
    }
    if(TryErrorRecovery(stack_pos, RECOVERED_NONE)) {
      return;
    }

    if(HasTryHandler()) {
      RecordRecoveredStatus(status);
    }
    if(TryErrorRecovery(stack_pos, RECOVERED_NONE)) {
      return;
    }
    switch(status) {
    case -1:
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance in native JIT code <<<" << std::endl;
      break;

    case -2:
      std::wcerr << L">>> Index under bounds in native JIT code <<<" << std::endl;
      break;

    case -3:
      std::wcerr << L">>> Index over bounds in native JIT code <<<" << std::endl;
      break;

    case -4:
      // the same text the interpreter prints (S4)
      std::wcerr << OBJECK_DIVIDE_BY_ZERO_MESSAGE << std::endl;
      break;
    }
    StackErrorUnwind(called);
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }

  // restore previous state
  ReleaseStackFrame(*stack_frame);
  (*stack_frame) = PopFrame();
  instrs = (*stack_frame)->method->GetInstructions();
  ip = (*stack_frame)->ip;
#endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessInterpretedMethodCall(StackMethod* called, size_t* instance, StackInstr* &instrs, long &ip)
{
#ifdef _DEBUG
  std::wcout << L"=== MTHD_CALL: id=" << called->GetClass()->GetId() << L","
        << called->GetId() << L"; name='" << called->GetName() << L"' ===" << std::endl;
#endif  
  (*stack_frame) = GetStackFrame(called, instance);
  instrs = (*stack_frame)->method->GetInstructions();
  ip = 0;
#ifdef _DEBUG
  std::wcout << L"creating frame=" << (*stack_frame) << std::endl;
#endif
}

/********************************
 * Processes a load integer array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadIntArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const INT64_VALUE size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  PushInt(array[index + instr->GetOperand()], op_stack, stack_pos);
}

/********************************
 * Processes a load store array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreIntArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_INT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif

  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  
  const INT64_VALUE size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  array[index + instr->GetOperand()] = PopInt(op_stack, stack_pos);
  // Generational GC write barrier for array stores (array base is at array - 2)
  MemoryManager::WriteBarrier(array - 2);
}

/********************************
 * Processes a load byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadByteArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_BYTE_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const INT64_VALUE size = (INT64_VALUE)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  array += instr->GetOperand();
  PushInt(((unsigned char*)array)[index], op_stack, stack_pos);
}

/********************************
 * Processes a load char array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadCharArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CHAR_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const INT64_VALUE size = (INT64_VALUE)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  array += instr->GetOperand();
  const wchar_t* char_array = (wchar_t*)array;
  PushInt(char_array[index], op_stack, stack_pos);
}

/********************************
 * Processes a store byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreByteArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_BYTE_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const INT64_VALUE size = (INT64_VALUE)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  array += instr->GetOperand();
  ((unsigned char*)array)[index] = (char)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a store char array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreCharArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_CHAR_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const long size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  array += instr->GetOperand();
  ((wchar_t*)array)[index] = (wchar_t)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a load float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloatArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_FLOAT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const long size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  FLOAT_VALUE value;
  memcpy(&value, array + index + instr->GetOperand(), sizeof(FLOAT_VALUE));
  PushFloat(value, op_stack, stack_pos);
}

/********************************
 * Processes a store float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloatArrayElement(StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_FLOAT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    if(TryErrorRecovery(stack_pos, RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element")) {
      return;
    }
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    VmExit(1);
#endif
  }
  const long size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
  // a recovered (or, under _NO_HALT, halting) bounds error: nothing to load or store
  if(index < 0) {
    return;
  }
  FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
  memcpy(array + index + instr->GetOperand(), &value, sizeof(FLOAT_VALUE));
}

/********************************
 * Shared library operations
 ********************************/

typedef void (*ext_load_def)(VMContext& callbacks);
void StackInterpreter::SharedLibraryLoad([[maybe_unused]] StackInstr* instr)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: shared LIBRARY_LOAD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  if(!instance) {
    std::wcerr << L">>> Unable to load shared library! <<<" << std::endl;
#ifdef _NO_HALT
    VmExit(1);
#else
    return;
#endif
  }

  size_t* str_obj = (size_t*)instance[0];
  if(!str_obj || !(size_t*)str_obj[0]) {
    std::wcerr << L">>> Name of runtime shared library was not specified! <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }
  
  std::wstring path_str;
  const std::wstring pre_path = GetLibraryPath();
#ifdef _OBJECK_NATIVE_LIB_PATH
#ifdef _WIN32
  if(!pre_path.empty()) {
    path_str += pre_path + L"\\native\\";
  }
  else {
    path_str += L"..\\lib\\native\\";
  }
#else
  if(!pre_path.empty()) {
     path_str += pre_path + L"/native/";
  }
  else {
    path_str += L"../lib/native/";
  }
#endif
#else
#ifdef _WIN32
  path_str += L"..\\lib\\native\\";
#else
  path_str += L"../lib/native/";
#endif
#endif
  size_t* array = (size_t*)str_obj[0];
  const std::wstring post_path_str((wchar_t*)(array + 3));
  path_str += post_path_str;
  
  std::string dll_string = UnicodeToBytes(path_str);
  if(dll_string.size() == 0) {
    std::wcerr << L">>> Name of runtime shared library was not specified! <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }

#ifdef _WIN32
  dll_string += ".dll";  
#elif _OSX
  dll_string += ".dylib";
#else
  dll_string += ".so";
#endif 

  // load shared library
#ifdef _WIN32
  // Load shared library file
  // by the UTF-16 name: LoadLibraryA read the UTF-8 bytes in the ANSI code page,
  // so an install directory with a non-ASCII name could load no native library
  HINSTANCE dll_handle = LoadLibraryW((path_str + L".dll").c_str());
  if(!dll_handle) {
    // Report WHY. The POSIX branch below has always printed dlerror(); this
    // one printed only the path, so every Windows load failure looked
    // identical whether the file was missing, built for another
    // architecture, or present but missing a dependency of its own. That
    // last case is the common one and the least guessable -- a native
    // library sitting right where it belongs, failing because something it
    // imports is absent (error 126).
    const DWORD load_error = GetLastError();
    wchar_t* error_text = nullptr;
    const DWORD error_len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                           FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, load_error,
                                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&error_text, 0, nullptr);
    std::wstring error_str;
    if(error_len && error_text) {
      error_str.assign(error_text, error_len);
      while(!error_str.empty() && (error_str.back() == L'\r' || error_str.back() == L'\n')) {
        error_str.pop_back();
      }
    }
    if(error_text) {
      LocalFree(error_text);
    }

    std::wcerr << L">>> Runtime error loading shared library: " << path_str << L".dll"
               << L" (error " << load_error;
    if(!error_str.empty()) {
      std::wcerr << L": " << error_str;
    }
    std::wcerr << L") <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }
  instance[1] = (size_t)dll_handle;

  // call load function
  ext_load_def ext_load = (ext_load_def)GetProcAddress(dll_handle, "load_lib");
  if(!ext_load) {
    std::wcerr << L">>> Runtime error calling function: load_lib <<<" << std::endl;
    FreeLibrary(dll_handle);
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }

  // call function
  VMContext context{};
  context.data_array = nullptr;
  context.op_stack = nullptr;
  context.stack_pos = nullptr;
  context.call_method_by_id = APITools_MethodCallId;
  context.call_method_by_name = APITools_MethodCall;
  context.alloc_managed_array = MemoryManager::AllocateArray;
  context.alloc_managed_obj = MemoryManager::AllocateObjectNative;
  context.write_barrier = MemoryManager::JitWriteBarrier;
  context.blocking_abi = APITOOLS_BLOCKING_ABI;
  context.begin_blocking = MemoryManager::BeginBlocking;
  context.end_blocking = MemoryManager::EndBlocking;
  (*ext_load)(context);
#else
  void* dll_handle = dlopen(dll_string.c_str(), RTLD_LAZY);
  if(!dll_handle) {
    std::wcerr << L">>> Runtime error loading shared library: " << dlerror() << L" <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }
  instance[1] = (size_t)dll_handle;

  // call load function
  ext_load_def ext_load = (ext_load_def)dlsym(dll_handle, "load_lib");
  char* error;
  if((error = dlerror()) != nullptr)  {
    std::wcerr << L">>> Runtime error calling function: " << error << L" <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }
  // call function
  VMContext context{};
  context.data_array = nullptr;
  context.op_stack = nullptr;
  context.stack_pos = nullptr;
  context.call_method_by_id = APITools_MethodCallId;
  context.call_method_by_name = APITools_MethodCall;
  context.alloc_managed_array = MemoryManager::AllocateArray;
  context.alloc_managed_obj = MemoryManager::AllocateObjectNative;
  context.write_barrier = MemoryManager::JitWriteBarrier;
  context.blocking_abi = APITOOLS_BLOCKING_ABI;
  context.begin_blocking = MemoryManager::BeginBlocking;
  context.end_blocking = MemoryManager::EndBlocking;
  (*ext_load)(context);
#endif
}

typedef void (*ext_unload_def)();
void StackInterpreter::SharedLibraryUnload([[maybe_unused]] StackInstr* instr)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: shared library_UNLOAD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  // unload shared library
#ifdef _WIN32
  HINSTANCE dll_handle = (HINSTANCE)instance[1];
  if(dll_handle) {
    // call unload function  
    ext_unload_def ext_unload = (ext_unload_def)GetProcAddress(dll_handle, "unload_lib");
    if(!ext_unload) {
      std::wcerr << L">>> Runtime error calling function: unload_lib <<<" << std::endl;
      FreeLibrary(dll_handle);
#ifdef _NO_HALT
      return;
#else
      VmExit(1);
#endif
    }
    (*ext_unload)();
    // Nothing may hold a resolved pointer into a library that is about to be
    // unloaded, on this thread or any other.
    InvalidateSharedLibraryCache();
    // free handle
    FreeLibrary(dll_handle);
  }
#else
  void* dll_handle = (void*)instance[1];
  if(dll_handle) {
    // call unload function
    ext_unload_def ext_unload = (ext_unload_def)dlsym(dll_handle, "unload_lib");
    char* error;
    if((error = dlerror()) != nullptr)  {
      std::wcerr << L">>> Runtime error calling function: " << error << L" <<<" << std::endl;
#ifdef _NO_HALT
      return;
#else
      VmExit(1);
#endif
    }
    // call function
    (*ext_unload)();
    // Nothing may hold a resolved pointer into a library that is about to be
    // unloaded, on this thread or any other.
    InvalidateSharedLibraryCache();
    // unload lib
    dlclose(dll_handle);
  }
#endif
}

typedef void (*lib_func_def) (VMContext& callbacks);

// A failed call back leaves a zero in place of each word of its result, so the
// native code that made it reads a 0 or a Nil rather than popping what lies
// beneath; recovery then unwinds the operand stack below all of it. Whatever
// the failed call left above its entry position goes first: a runaway fails
// once per nested call back on the way out, and without that the leftovers
// of thousands of them ran past the operand stack's committed end.
static void PushCallbackPlaceholder(StackMethod* method, size_t* op_stack, size_t* stack_pos, const size_t entry_pos)
{
  if(*stack_pos > entry_pos) {
    *stack_pos = entry_pos;
  }
  switch(method->GetReturn()) {
  case NIL_TYPE:
    break;

  case FUNC_TYPE:
    op_stack[(*stack_pos)++] = 0;
    op_stack[(*stack_pos)++] = 0;
    break;

  default:
    op_stack[(*stack_pos)++] = 0;
    break;
  }
}

//
// A call back into Objeck from C++ (see the header).
//
// Each call back used to build an interpreter with a call stack of its own, so
// the calls nested in it were counted from zero again, and its operand-stack
// limit was SIZE_MAX, since the operand stack it ran on was not that call
// stack's. A recursion through a callback was bounded by nothing: native code
// calling back into Objeck that called the native code again, or a constructor
// that made another of its class through Class->Instance, recursed until the
// native stack ran out and the process died in the OS (0xC00000FD, SIGSEGV).
//
// It runs on the thread's own call stack now (CallStack::Current), above the
// frames already there, like the bridge compiled code starts. Every call it
// makes is checked against the thread's depth, operand stack and native stack,
// and the entry is checked too, since each nested callback also costs native
// stack for the C++ between it and the last.
//
bool StackInterpreter::CallBack(size_t* op_stack, size_t* stack_pos, StackMethod* method, size_t* instance)
{
  CallStack* stack = CallStack::Current();
  if(!stack || op_stack != stack->op_stack) {
    // a thread the VM did not start: it has no call stack to share, so the
    // call runs as it always did
    StackInterpreter intpr;
    intpr.Execute(op_stack, stack_pos, 0, method, instance, false);
    return true;
  }

  const size_t entry_pos = *stack_pos;

  // a call back on this thread already failed and its caller has not taken
  // the error yet: native code is unwinding to the VM, so do not run more
  bool& failed = CallbackFailed();
  if(failed) {
    PushCallbackPlaceholder(method, op_stack, stack_pos, entry_pos);
    return false;
  }

  StackInterpreter intpr(stack->frames, &stack->pos);
  intpr.is_callback = true;

  // room for the entry: a frame slot, OP_HEADROOM operand words and the
  // native stack the nested interpreter runs on
  const bool room = (stack->pos < stack->depth_limit && *stack_pos <= stack->op_limit) || stack->Grow(*stack_pos);
  if(!room || stack->NativeStackLow()) {
    if(ThreadHasTryHandler()) {
      RecordRecoveredError(RECOVERED_OTHER, L"call stack bounds have been exceeded!");
      failed = true;
      PushCallbackPlaceholder(method, op_stack, stack_pos, entry_pos);
      return false;
    }
    std::wcerr << L">>> call stack bounds have been exceeded! <<<" << std::endl;
    intpr.StackErrorUnwind(method);
    VmExit(1);
  }

  intpr.Execute(op_stack, stack_pos, 0, method, instance, false);
  if(intpr.bridge_deferred) {
    // Execute released its entry frame, except where a method compiled on
    // entry returned early with the error
    if(*intpr.stack_frame) {
      ReleaseStackFrame(*intpr.stack_frame);
      *intpr.stack_frame = nullptr;
    }
    failed = true;
    PushCallbackPlaceholder(method, op_stack, stack_pos, entry_pos);
    return false;
  }

  return true;
}

//
// Resolved native entry points, cached.
//
// Every EXT_LIB_FUNC_CALL used to convert the function's name from wide to
// narrow and then ask the loader for its address -- GetProcAddress walks a
// DLL's export table, dlsym walks the module's symbol hash -- and it did that
// on every call, for a name that never changes. Measured on this tree at 110ns
// per call: 69ns in the lookup and 39ns in the conversion that feeds it.
//
// The cache is THREAD-LOCAL rather than shared behind a lock. A native call is
// on the hot path of anything using a native library, and an uncontended mutex
// would give back a useful fraction of what the cache saves. The cost is that
// each thread resolves a given symbol once, which is a handful of lookups per
// thread rather than one per call.
//
// Invalidation is a generation counter rather than per-entry bookkeeping. A
// library being unloaded is rare and a thread cannot reach into another
// thread's cache to purge it, so an unload bumps the counter and every thread
// drops its whole cache the next time it looks something up. Without that, a
// library unloaded and a different one loaded at the same address would be
// called through a stale pointer.
//
static std::atomic<uint64_t> ext_lib_generation{ 0 };

void StackInterpreter::InvalidateSharedLibraryCache()
{
  ext_lib_generation.fetch_add(1, std::memory_order_relaxed);
}

static lib_func_def ResolveSharedLibraryFunction(void* dll_handle, const std::wstring& name)
{
  thread_local uint64_t seen_generation = 0;
  thread_local std::unordered_map<const void*, std::unordered_map<std::wstring, lib_func_def> > resolved;

  const uint64_t current = ext_lib_generation.load(std::memory_order_relaxed);
  if(seen_generation != current) {
    resolved.clear();
    seen_generation = current;
  }

  std::unordered_map<std::wstring, lib_func_def>& per_library = resolved[dll_handle];
  const std::unordered_map<std::wstring, lib_func_def>::const_iterator found = per_library.find(name);
  if(found != per_library.end()) {
    // A miss is cached too. The name comes from the same call site every time,
    // so a symbol that is absent now will be absent on the next million calls,
    // and re-asking the loader each time only slows down the error path.
    return found->second;
  }

  const std::string narrow = UnicodeToBytes(name);
  lib_func_def address = nullptr;
#ifdef _WIN32
  address = (lib_func_def)GetProcAddress((HINSTANCE)dll_handle, narrow.c_str());
#else
  // Clear any error left by something else before asking, so the check below is
  // about THIS lookup. dlsym returning null is legitimate for a null symbol,
  // which is why dlerror rather than the return value is the test.
  dlerror();
  address = (lib_func_def)dlsym(dll_handle, narrow.c_str());
  if(dlerror() != nullptr) {
    address = nullptr;
  }
#endif

  per_library[name] = address;

  return address;
}

void StackInterpreter::SharedLibraryCall([[maybe_unused]] StackInstr* instr, size_t* &op_stack, size_t* &stack_pos)
{
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  size_t* str_obj = (size_t*)(*stack_frame)->mem[1];
  size_t* array = (size_t*)str_obj[0];
  if(!array) {
    std::wcerr << L">>> Runtime error calling function <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    VmExit(1);
#endif
  }

  const std::wstring wstr((wchar_t*)(array + 3));
  size_t* args = (size_t*)(*stack_frame)->mem[2];
  lib_func_def ext_func;

#ifdef _DEBUG
  std::wcout << L"stack oper: shared LIBRARY_FUNC_CALL; call_pos=" << (*call_stack_pos) << "; function='" << wstr << L"'" << std::endl;
#endif

#ifdef _WIN32
  HINSTANCE dll_handle = (HINSTANCE)instance[1];
  if(dll_handle) {
    // resolved once per library per thread; see ResolveSharedLibraryFunction
    ext_func = ResolveSharedLibraryFunction((void*)dll_handle, wstr);
    if(!ext_func) {
      std::wcerr << L">>> Runtime error calling function: " << wstr << L" <<<" << std::endl;
      // The library is going away, so nothing may keep a pointer into it.
      InvalidateSharedLibraryCache();
      FreeLibrary(dll_handle);
#ifdef _NO_HALT
      return;
#else
      VmExit(1);
#endif
    }
    // call function
    VMContext context{};
    context.data_array = args;
    context.op_stack = op_stack;
    context.stack_pos = stack_pos;
    context.call_method_by_name = APITools_MethodCall;
    context.call_method_by_id = APITools_MethodCallId;
    context.alloc_managed_array = MemoryManager::AllocateArray;
    context.alloc_managed_obj = MemoryManager::AllocateObjectNative;
    context.write_barrier = MemoryManager::JitWriteBarrier;
    context.blocking_abi = APITOOLS_BLOCKING_ABI;
    context.begin_blocking = MemoryManager::BeginBlocking;
    context.end_blocking = MemoryManager::EndBlocking;
    (*ext_func)(context);
    SharedLibraryCallbackFailure(stack_pos);
  }
#else
  // load function
  void* dll_handle = (void*)instance[1];
  if(dll_handle) {
    // resolved once per library per thread; see ResolveSharedLibraryFunction
    ext_func = ResolveSharedLibraryFunction(dll_handle, wstr);
    if(!ext_func) {
      std::wcerr << L">>> Runtime error calling function: " << wstr << L" <<<" << std::endl;
      InvalidateSharedLibraryCache();
#ifdef _NO_HALT
      return;
#else
      VmExit(1);
#endif
    }
    // call function
    VMContext context{};
    context.data_array = args;
    context.op_stack = op_stack;
    context.stack_pos = stack_pos;
    context.call_method_by_name = APITools_MethodCall;
    context.call_method_by_id = APITools_MethodCallId;
    context.alloc_managed_array = MemoryManager::AllocateArray;
    context.alloc_managed_obj = MemoryManager::AllocateObjectNative;
    context.write_barrier = MemoryManager::JitWriteBarrier;
    context.blocking_abi = APITOOLS_BLOCKING_ABI;
    context.begin_blocking = MemoryManager::BeginBlocking;
    context.end_blocking = MemoryManager::EndBlocking;
    (*ext_func)(context);
    SharedLibraryCallbackFailure(stack_pos);
  }
#endif
}

//
// After a native call returns: a call back into Objeck that it made failed
// under a Try() beneath this native call (see CallBack). The native frames are
// gone now, so the error is this call's, recovered here -- or left further out,
// when this interpreter is itself a call back from native code.
//
void StackInterpreter::SharedLibraryCallbackFailure(size_t* &stack_pos)
{
  bool& failed = CallbackFailed();
  if(!failed) {
    return;
  }
  failed = false;

  // the error was recorded where it happened
  if(TryErrorRecovery(stack_pos, RECOVERED_NONE)) {
    return;
  }

  // a call back fails this way only while a handler exists, so this is not
  // reached; report rather than run on if it ever is
  std::wcerr << L">>> " << RecoveredError().message << L" <<<" << std::endl;
  StackErrorUnwind();
#ifdef _NO_HALT
  halt = true;
#else
  VmExit(1);
#endif
}

StackFrame* Runtime::StackInterpreter::GetStackFrame(StackMethod* method, size_t* instance)
{
  FramePool& pool = tl_frame_pool;
  if(pool.free_frames.empty()) {
    pool.free_frames.reserve(FRAME_POOL_FILL * 4);
    for(size_t i = 0; i < FRAME_POOL_FILL; ++i) {
      StackFrame* frame = new StackFrame();
      frame->mem = (size_t*)calloc(FRAME_MEM_SIZE, sizeof(char));
      pool.free_frames.push_back(frame);
    }
  }
  StackFrame* frame = pool.free_frames.back();
  pool.free_frames.pop_back();

  // Zero what this method can address: its local space in bytes (mem_size, a
  // multiple of the word size) after the instance word and the and/or slot --
  // the layout StackMethod::NewMemory allocates. An uninitialized local reads
  // as 0/Nil because of this. The whole LOCAL_SIZE buffer used to be cleared
  // on release, for a compiled callee that addresses one word of it; the
  // compiler refuses a method that needs more than LOCAL_SIZE.
  size_t bytes = (size_t)method->GetMemorySize() + 2 * sizeof(size_t);
  if(bytes > FRAME_MEM_SIZE) {
    bytes = FRAME_MEM_SIZE;
  }
  memset(frame->mem, 0, bytes);

  frame->method = method;
  frame->mem[0] = (size_t)instance;
  frame->ip = -1;
  frame->jit_called = false;
  frame->jit_mem = nullptr;
  frame->jit_offset = 0;
#ifdef _DEBUG
  std::wcout << L"fetching frame=" << frame << std::endl;
#endif

  return frame;
}

void Runtime::StackInterpreter::ReleaseStackFrame(StackFrame* frame)
{
  frame->jit_mem = nullptr;
  tl_frame_pool.free_frames.push_back(frame);
#ifdef _DEBUG
  std::wcout << L"caching frame=" << frame << std::endl;
#endif
}

// An uncaught error used to list every frame, and there were at most 256 of
// them. With thousands, the listing buried the error: list the innermost
// UNWIND_SHOW_TOP and the outermost UNWIND_SHOW_BOTTOM, and count the rest.
static const long UNWIND_SHOW_TOP = 40;
static const long UNWIND_SHOW_BOTTOM = 10;

// whether the unwind listing shows the frame at pos when the stack holds top
// frames; says, once, how many it leaves out
static bool UnwindShows(long pos, long top, bool& noted)
{
  if(pos > top - UNWIND_SHOW_TOP || pos <= UNWIND_SHOW_BOTTOM) {
    return true;
  }
  if(!noted) {
    noted = true;
    std::wcerr << L"  ... " << (top - UNWIND_SHOW_TOP - UNWIND_SHOW_BOTTOM) << L" frame(s) not shown ..." << std::endl;
  }
  return false;
}

void Runtime::StackInterpreter::StackErrorUnwind()
{
#ifdef _DEBUGGER
  // An uncaught runtime error reached the top of the stack (no try/error
  // handler recovered it). Give the debugger a chance to break here before the
  // VM halts, so DAP "uncaught" exception breakpoints can stop and inspect.
  if(debugger) {
    debugger->OnRuntimeError(call_stack, *call_stack_pos, *stack_frame);
  }
#endif

  long pos = (*call_stack_pos);
#ifdef _NO_HALT
  std::wcerr << L"Unwinding local stack (" << this << L"):" << std::endl;
  StackMethod* method = (*stack_frame)->method;
  if((*stack_frame)->ip > 0 && pos > -1 &&
     method->GetInstruction((*stack_frame)->ip)->GetLineNumber() > 0) {
    std::wcerr << L"  method: pos=" << pos << L", file="
          << (*stack_frame)->method->GetClass()->GetFileName() << L", name='"
          << MethodFormatter::Format((*stack_frame)->method->GetName()) << L"', line="
          << method->GetInstruction((*stack_frame)->ip)->GetLineNumber() << std::endl;
  }
  if(pos != 0) {
    const long top = pos;
    bool noted = false;
    while(--pos) {
      if(!UnwindShows(pos, top, noted)) {
        continue;
      }
      StackMethod* method = call_stack[pos]->method;
      if(call_stack[pos]->ip > 0 && pos > -1 &&
         method->GetInstruction(call_stack[pos]->ip)->GetLineNumber() > 0) {
        std::wcerr << L"  method: pos=" << pos << L", file="
              << call_stack[pos]->method->GetClass()->GetFileName() << L", name='"
              << MethodFormatter::Format(call_stack[pos]->method->GetName()) << L"', line="
              << method->GetInstruction(call_stack[pos]->ip)->GetLineNumber() << std::endl;
      }
    }
  }
  std::wcerr << L"  ..." << std::endl;
#else
  std::wcerr << L"Unwinding local stack (" << this << L"):" << std::endl;
  std::wcerr << L"  method: pos=" << pos << L", name='"
        << MethodFormatter::Format((*stack_frame)->method->GetName()) << L"'" << std::endl;
  if(pos != 0) {
    const long top = pos;
    bool noted = false;
    while(--pos && pos > -1) {
      if(UnwindShows(pos, top, noted)) {
        std::wcerr << L"  method: pos=" << pos << L", name='"
              << MethodFormatter::Format(call_stack[pos]->method->GetName()) << L"'" << std::endl;
      }
    }
  }
  std::wcerr << L"  ..." << std::endl;
#endif
}

INT64_VALUE Runtime::StackInterpreter::ArrayIndex(StackInstr* instr, size_t* array, const INT64_VALUE size, size_t*& op_stack, size_t*& stack_pos)
{
  // generate index
  INT64_VALUE index = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const long dim = instr->GetOperand();

  for(long i = 1; i < dim; ++i) {
    index *= (long)array[i];
    index += (INT64_VALUE)PopInt(op_stack, stack_pos);
  }

#ifdef _DEBUG
  std::wcout << L"  [raw index=" << index << L", raw size=" << size << L"]" << std::endl;
#endif

  // bounds check
  if(index < 0 || index >= size) {
    if(TryErrorRecovery(stack_pos, RECOVERED_INDEX_OUT_OF_BOUNDS, L"Index out of bounds: " + std::to_wstring(index) + L"," + std::to_wstring(size))) {
      return -1;
    }
    std::wcerr << L">>> Index out of bounds: " << index << L"," << size << L" <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return -1;
#else
    VmExit(1);
#endif
  }

  return index;
}

size_t* Runtime::StackInterpreter::CreateStringObject(const std::wstring& value_str, size_t*& op_stack, size_t*& stack_pos)
{
  // create character array
  const long char_array_size = (long)value_str.size();
  const long char_array_dim = 1;
  size_t* char_array = (size_t*)MemoryManager::AllocateArray(static_cast<size_t>(char_array_size) + 1 + ((static_cast<size_t>(char_array_dim) + 2) * sizeof(size_t)),
                                                             CHAR_ARY_TYPE, op_stack, *stack_pos, false);
  char_array[0] = static_cast<size_t>(char_array_size) + 1;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy string
  wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
#ifdef _WIN32
  wcsncpy_s(char_array_ptr, static_cast<rsize_t>(char_array_size) + 1, value_str.c_str(), char_array_size);
#else
  wcsncpy(char_array_ptr, value_str.c_str(), char_array_size);
#endif

  // create 'System.String' object instance
  size_t * str_obj = MemoryManager::AllocateObject(program->GetStringObjectId(), op_stack, *stack_pos, false);
  str_obj[0] = (size_t)char_array;
  str_obj[1] = char_array_size;
  str_obj[2] = char_array_size;

  return str_obj;
}

void Runtime::StackInterpreter::StackErrorUnwind(StackMethod* method)
{
  long pos = (*call_stack_pos);
  std::wcerr << L"Unwinding local stack (" << this << L"):" << std::endl;
  std::wcerr << L"  method: pos=" << pos << L", name='" << MethodFormatter::Format(method->GetName()) << L"'" << std::endl;
  // stops at zero: with an empty call stack (a thread's entry method compiled
  // on entry) the old test never reached it and the walk ran below the array
  const long top = pos;
  bool noted = false;
  while(--pos > 0) {
    if(UnwindShows(pos, top, noted)) {
      std::wcerr << L"  method: pos=" << pos << L", name='" << MethodFormatter::Format(call_stack[pos]->method->GetName()) << L"'" << std::endl;
    }
  }
  std::wcerr << L"  ..." << std::endl;
}

// --jit from the command line. Declared in vm_options.h so the platform mains
// can call it without including the JIT headers.
void Runtime::SetJitAutoThreshold(long threshold)
{
  JitAutoThresholdOverride() = threshold;
}

// --nursery / OBJECK_NURSERY, validated by ParseVmOptions. Applied when the
// interpreter calls MemoryManager::Initialize.
void Runtime::SetNurserySize(size_t size)
{
  MemoryManager::SetNurserySize(size);
}

// --call-depth / OBJECK_CALL_DEPTH, validated by ParseVmOptions. Applied before
// the first call stack is created; every thread's is reserved for it.
static_assert(CALL_DEPTH_MIN == Runtime::CALL_DEPTH_FLAG_MIN && CALL_DEPTH_MAX == Runtime::CALL_DEPTH_FLAG_MAX,
              "call_stack.h and vm_options.h must agree on the --call-depth range");

void Runtime::SetMaxCallDepth(long depth)
{
  if(depth >= CALL_DEPTH_MIN && depth <= CALL_DEPTH_MAX) {
    MaxCallDepthRef() = depth;
  }
}
