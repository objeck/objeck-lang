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
std::stack<StackFrame*> StackInterpreter::cached_frames;
std::set<StackInterpreter*> StackInterpreter::intpr_threads;
InstrPtr* StackInterpreter::instr_pointers;

#ifdef _WIN32
bool StackInterpreter::is_stdio_binary;
#endif

#ifdef _WIN32
CRITICAL_SECTION StackInterpreter::cached_frames_cs;
CRITICAL_SECTION StackInterpreter::intpr_threads_cs;
#else
pthread_mutex_t StackInterpreter::cached_frames_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t StackInterpreter::intpr_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/********************************
 * VM initialization
 ********************************/
void StackInterpreter::Initialize(StackProgram* p, size_t m)
{
  program = p;

  instr_pointers = new InstrPtr[END_STMTS];
  memset(instr_pointers, 0, sizeof(InstrPtr) * END_STMTS);

  instr_pointers[STOR_LOCL_INT_VAR] = &StackInterpreter::StorLoclIntVar;
  instr_pointers[STOR_CLS_INST_INT_VAR] = &StackInterpreter::StorClsInstIntVar;
  instr_pointers[COPY_LOCL_INT_VAR] = &StackInterpreter::CopyLoclIntVar;
  instr_pointers[COPY_CLS_INST_INT_VAR] = &StackInterpreter::CopyClsInstIntVar;
  instr_pointers[LOAD_LOCL_INT_VAR] = &StackInterpreter::LoadLoclIntVar;
  instr_pointers[LOAD_CLS_INST_INT_VAR] = &StackInterpreter::LoadClsInstIntVar;
  instr_pointers[S2I] = &StackInterpreter::Str2Int;
  instr_pointers[S2F] = &StackInterpreter::Str2Float;
  instr_pointers[I2S] = &StackInterpreter::Int2Str;
  instr_pointers[F2S] = &StackInterpreter::Float2Str;
  instr_pointers[SHL_INT] = &StackInterpreter::ShlInt;
  instr_pointers[SHR_INT] = &StackInterpreter::ShrInt;
  instr_pointers[AND_INT] = &StackInterpreter::AndInt;
  instr_pointers[OR_INT] = &StackInterpreter::OrInt;
  instr_pointers[ADD_INT] = &StackInterpreter::AddInt;
  instr_pointers[ADD_FLOAT] = &StackInterpreter::AddFloat;
  instr_pointers[SUB_INT] = &StackInterpreter::SubInt;
  instr_pointers[SUB_FLOAT] = &StackInterpreter::SubFloat;
  instr_pointers[MUL_INT] = &StackInterpreter::MulInt;
  instr_pointers[DIV_INT] = &StackInterpreter::DivInt;
  instr_pointers[MUL_FLOAT] = &StackInterpreter::MulFloat;
  instr_pointers[DIV_FLOAT] = &StackInterpreter::DivFloat;
  instr_pointers[MOD_INT] = &StackInterpreter::ModInt;
  instr_pointers[BIT_AND_INT] = &StackInterpreter::BitAndInt;
  instr_pointers[BIT_OR_INT] = &StackInterpreter::BitOrInt;
  instr_pointers[BIT_XOR_INT] = &StackInterpreter::BitXorInt;
  instr_pointers[BIT_NOT_INT] = &StackInterpreter::BitNotInt;
  instr_pointers[LES_EQL_INT] = &StackInterpreter::LesEqlInt;
  instr_pointers[GTR_EQL_INT] = &StackInterpreter::GtrEqlInt;
  instr_pointers[LES_EQL_FLOAT] = &StackInterpreter::LesEqlFloat;
  instr_pointers[GTR_EQL_FLOAT] = &StackInterpreter::GtrEqlFloat;
  instr_pointers[EQL_INT] = &StackInterpreter::EqlInt;
  instr_pointers[NEQL_INT] = &StackInterpreter::NeqlInt;
  instr_pointers[LES_INT] = &StackInterpreter::LesInt;
  instr_pointers[GTR_INT] = &StackInterpreter::GtrInt;
  instr_pointers[EQL_FLOAT] = &StackInterpreter::EqlFloat;
  instr_pointers[NEQL_FLOAT] = &StackInterpreter::NeqlFloat;
  instr_pointers[LES_FLOAT] = &StackInterpreter::LesFloat;
  instr_pointers[GTR_FLOAT] = &StackInterpreter::GtrFloat;
  instr_pointers[LOAD_ARY_SIZE] = &StackInterpreter::LoadArySize;
  instr_pointers[CPY_BYTE_ARY] = &StackInterpreter::CpyByteAry;
  instr_pointers[CPY_CHAR_ARY] = &StackInterpreter::CpyCharAry;
  instr_pointers[CPY_INT_ARY] = &StackInterpreter::CpyIntAry;
  instr_pointers[CPY_FLOAT_ARY] = &StackInterpreter::CpyFloatAry;
  instr_pointers[ZERO_BYTE_ARY] = &StackInterpreter::ZeroByteAry;
  instr_pointers[ZERO_CHAR_ARY] = &StackInterpreter::ZeroCharAry;
  instr_pointers[ZERO_INT_ARY] = &StackInterpreter::ZeroIntAry;
  instr_pointers[ZERO_FLOAT_ARY] = &StackInterpreter::ZeroFloatAry;
  instr_pointers[OBJ_TYPE_OF] = &StackInterpreter::ObjTypeOf;
  instr_pointers[OBJ_INST_CAST] = &StackInterpreter::ObjInstCast;
  instr_pointers[ASYNC_MTHD_CALL] = &StackInterpreter::AsyncMthdCall;
  instr_pointers[THREAD_JOIN] = &StackInterpreter::ThreadJoin;
  instr_pointers[THREAD_MUTEX] = &StackInterpreter::ThreadMutex;
  instr_pointers[CRITICAL_START] = &StackInterpreter::CriticalStart;
  instr_pointers[CRITICAL_END] = &StackInterpreter::CriticalEnd;
  instr_pointers[LOAD_INT_ARY_ELM] = &StackInterpreter::ProcessLoadIntArrayElement;
  instr_pointers[STOR_INT_ARY_ELM] = &StackInterpreter::ProcessStoreIntArrayElement;
  instr_pointers[LOAD_FLOAT_ARY_ELM] = &StackInterpreter::ProcessLoadFloatArrayElement;
  instr_pointers[STOR_FLOAT_ARY_ELM] = &StackInterpreter::ProcessStoreFloatArrayElement;
  instr_pointers[LOAD_BYTE_ARY_ELM] = &StackInterpreter::ProcessLoadByteArrayElement;
  instr_pointers[STOR_BYTE_ARY_ELM] = &StackInterpreter::ProcessStoreByteArrayElement;
  instr_pointers[LOAD_CHAR_ARY_ELM] = &StackInterpreter::ProcessLoadCharArrayElement;
  instr_pointers[STOR_CHAR_ARY_ELM] = &StackInterpreter::ProcessStoreCharArrayElement;
  instr_pointers[STOR_FUNC_VAR] = &StackInterpreter::ProcessStoreFunctionVar;
  instr_pointers[LOAD_FUNC_VAR] = &StackInterpreter::ProcessLoadFunctionVar;
  instr_pointers[STOR_FLOAT_VAR] = &StackInterpreter::ProcessStoreFloat;
  instr_pointers[LOAD_FLOAT_VAR] = &StackInterpreter::ProcessLoadFloat;
  instr_pointers[COPY_FLOAT_VAR] = &StackInterpreter::ProcessCopyFloat;
  instr_pointers[NEW_CHAR_ARY] = &StackInterpreter::ProcessNewCharArray;
  instr_pointers[NEW_OBJ_INST] = &StackInterpreter::ProcessNewObjectInstance;
  instr_pointers[NEW_FUNC_INST] = &StackInterpreter::ProcessNewFunctionInstance;
  instr_pointers[NEW_BYTE_ARY] = &StackInterpreter::ProcessNewByteArray;
  instr_pointers[MOD_FLOAT] = &StackInterpreter::ModFloat;
  instr_pointers[POW_FLOAT] = &StackInterpreter::PowFloat;
  instr_pointers[EXT_LIB_LOAD] = &StackInterpreter::ExtLibLoad;
  instr_pointers[EXT_LIB_UNLOAD] = &StackInterpreter::ExtLibUnload;
  instr_pointers[LOAD_CHAR_LIT] = &StackInterpreter::LoadCharLit;
  instr_pointers[LOAD_INT_LIT] = &StackInterpreter::LoadIntLit;
  instr_pointers[LOAD_FLOAT_LIT] = &StackInterpreter::LoadFloatLit;
  instr_pointers[CEIL_FLOAT] = &StackInterpreter::CeilFloat;
  instr_pointers[TRUNC_FLOAT] = &StackInterpreter::TruncFloat;
  instr_pointers[FLOR_FLOAT] = &StackInterpreter::FlorFloat;
  instr_pointers[SIN_FLOAT] = &StackInterpreter::SinFloat;
  instr_pointers[COS_FLOAT] = &StackInterpreter::CosFloat;
  instr_pointers[TAN_FLOAT] = &StackInterpreter::TanFloat;
  instr_pointers[ASIN_FLOAT] = &StackInterpreter::AsinFloat;
  instr_pointers[ACOS_FLOAT] = &StackInterpreter::AcosFloat;
  instr_pointers[ATAN_FLOAT] = &StackInterpreter::AtanFloat;
  instr_pointers[LOG2_FLOAT] = &StackInterpreter::Log2Float;
  instr_pointers[CBRT_FLOAT] = &StackInterpreter::CbrtFloat;
  instr_pointers[LOG_FLOAT] = &StackInterpreter::LogFloat;
  instr_pointers[ROUND_FLOAT] = &StackInterpreter::RoundFloat;
  instr_pointers[EXP_FLOAT] = &StackInterpreter::ExpFloat;
  instr_pointers[LOG10_FLOAT] = &StackInterpreter::Log10Float;
  instr_pointers[SQRT_FLOAT] = &StackInterpreter::SqrtFloat;
  instr_pointers[GAMMA_FLOAT] = &StackInterpreter::GammaFloat;
  instr_pointers[NAN_INT] = &StackInterpreter::NanInt;
  instr_pointers[INF_INT] = &StackInterpreter::InfInt;
  instr_pointers[NEG_INF_INT] = &StackInterpreter::NegInfInt;
  instr_pointers[NAN_FLOAT] = &StackInterpreter::NanFloat;
  instr_pointers[INF_FLOAT] = &StackInterpreter::InfFloat;
  instr_pointers[NEG_INF_FLOAT] = &StackInterpreter::NegInfFloat;
  instr_pointers[RAND_FLOAT] = &StackInterpreter::RandFloat;
  instr_pointers[ACOSH_FLOAT] = &StackInterpreter::AcoshFloat;
  instr_pointers[ASINH_FLOAT] = &StackInterpreter::AsinhFloat;
  instr_pointers[ATANH_FLOAT] = &StackInterpreter::AtanhFloat;
  instr_pointers[COSH_FLOAT] = &StackInterpreter::CoshFloat;
  instr_pointers[SINH_FLOAT] = &StackInterpreter::SinhFloat;
  instr_pointers[TANH_FLOAT] = &StackInterpreter::TanhFloat;
  instr_pointers[ATAN2_FLOAT] = &StackInterpreter::Atan2Float;
  instr_pointers[I2F] = &StackInterpreter::IntToFloat;
  instr_pointers[F2I] = &StackInterpreter::Float2Int;
  instr_pointers[SWAP_INT] = &StackInterpreter::SwapInt;
  instr_pointers[POP_INT] = &StackInterpreter::PopInt;
  instr_pointers[POP_FLOAT] = &StackInterpreter::PopFloat;
  instr_pointers[THREAD_SLEEP] = &StackInterpreter::ThreadSleep;
  instr_pointers[LOAD_CLS_MEM] = &StackInterpreter::LoadClsMem;
  instr_pointers[LOAD_INST_MEM] = &StackInterpreter::LoadInstMem;

#ifdef _WIN32
  InitializeCriticalSection(&cached_frames_cs);
  InitializeCriticalSection(&intpr_threads_cs);
#endif

#ifndef _SANITIZE
  // allocate frames
  for(size_t i = 0; i < FRAME_CACHE_SIZE; ++i) {
    StackFrame* frame = new StackFrame();
    frame->mem = (size_t*)calloc(LOCAL_SIZE, sizeof(char));
    cached_frames.push(frame);
  }
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
void StackInterpreter::Execute(size_t* op_stack, long* stack_pos, long i, StackMethod* method, size_t* instance, bool jit_called)
{
#ifdef _TIMING
  clock_t start = clock();
#endif

  // initial setup
  if(stack_frame_monitor) {
    (*call_stack_pos) = 0;
  }
  (*stack_frame) = GetStackFrame(method, instance);
  
#ifdef _DEBUG
  std::wcout << L"creating frame=" << (*stack_frame) << std::endl;
#endif
  (*stack_frame)->jit_called = jit_called;
  StackInstr** instrs = (*stack_frame)->method->GetInstructions();
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

  // execute
  do {
    StackInstr* instr = instrs[ip++];

#ifdef _DEBUGGER
    debugger->ProcessInstruction(instr, ip, call_stack, (*call_stack_pos), (*stack_frame));
#endif

    const InstructionType instr_type = instr->GetType();
    if(instr_pointers[instr_type]) {
      // call the instruction handler
      (this->*instr_pointers[instr_type])(instr, op_stack, stack_pos);
      continue;
    }
    
    switch(instr_type) {
    case RTRN:
      ProcessReturn(instrs, ip);
      // return directly back to JIT code
      if((*stack_frame) && (*stack_frame)->jit_called) {
        (*stack_frame)->jit_called = false;
        ReleaseStackFrame(*stack_frame);
        return;
      }
      break;

    case MTHD_CALL:
      ProcessMethodCall(instr, instrs, ip, op_stack, stack_pos);
      // return directly back to JIT code
      if((*stack_frame)->jit_called) {
        (*stack_frame)->jit_called = false;
        ReleaseStackFrame(*stack_frame);
        return;
      }
      break;

    case DYN_MTHD_CALL:
      ProcessDynamicMethodCall(instr, instrs, ip, op_stack, stack_pos);
      // return directly back to JIT code
      if((*stack_frame)->jit_called) {
        (*stack_frame)->jit_called = false;
        ReleaseStackFrame(*stack_frame);
        return;
      }
      break;

    case NEW_INT_ARY:
      ProcessNewArray(instr, op_stack, stack_pos);
      break;

    case NEW_FLOAT_ARY:
      ProcessNewArray(instr, op_stack, stack_pos, true);
      break;

    case JMP:
#ifdef _DEBUG
      std::wcout << L"stack oper: JMP; call_pos=" << (*call_stack_pos) << std::endl;
#endif
      if(instr->GetOperand2() < 0) {
        ip = instr->GetOperand();
      }
      else if((INT64_VALUE)PopInt(op_stack, stack_pos) == instr->GetOperand2()) {
        ip = instr->GetOperand();
      }      
      break;

    case TRAP:
    case TRAP_RTRN:
#ifdef _DEBUG
      std::wcout << L"stack oper: TRAP; call_pos=" << (*call_stack_pos) << std::endl;
#endif
      if(!TrapProcessor::ProcessTrap(program, (size_t*)(*stack_frame)->mem[0], op_stack, stack_pos, (*stack_frame))) {
        StackErrorUnwind();
#ifdef _NO_HALT
        halt = true;
        return;
#else
        exit(1);
#endif
      }
      break;    

      // note: just for debugger
    case END_STMTS:
      break;

    default:
      // std::wcerr << L">>> Unknown instruction type: " << instr_type << L" <<<" << std::endl;
      break;
    }
  }
  while(!halt);
  
#ifdef _TIMING
  clock_t end = clock();
  std::wcout << L"---------------------------" << std::endl;
  std::wcout << L"Dispatch method='" << mthd_name << L"', time=" << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
#endif
}

void StackInterpreter::ExtLibLoad(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  SharedLibraryLoad(instr);
}

void StackInterpreter::ExtLibUnload(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  SharedLibraryUnload(instr);
}

void StackInterpreter::LoadCharLit(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INT_LIT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushInt(instr->GetOperand(), op_stack, stack_pos);
}

void StackInterpreter::LoadIntLit(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INT_LIT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushInt(instr->GetInt64Operand(), op_stack, stack_pos);
}

void StackInterpreter::LoadFloatLit(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_FLOAT_LIT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushFloat(instr->GetFloatOperand(), op_stack, stack_pos);
}

void StackInterpreter::CeilFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(ceil(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::TruncFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(trunc(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::FlorFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(floor(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::SinFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(sin(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::CosFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(cos(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::TanFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(tan(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::AsinFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(asin(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::AcosFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(acos(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::AtanFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(atan(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::Log2Float(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(log2(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::CbrtFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(cbrt(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::LogFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(log(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::RoundFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(round(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::ExpFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(exp(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::Log10Float(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(log10(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::SqrtFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(sqrt(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::GammaFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(tgamma(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::NanInt(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(std::numeric_limits<INT_VALUE>::quiet_NaN(), op_stack, stack_pos);
}

void StackInterpreter::InfInt(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(std::numeric_limits<INT_VALUE>::infinity(), op_stack, stack_pos);
}

void StackInterpreter::NegInfInt(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(-1 * std::numeric_limits<INT_VALUE>::infinity(), op_stack, stack_pos);
}

void StackInterpreter::NanFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(std::numeric_limits<double>::quiet_NaN(), op_stack, stack_pos);
}

void StackInterpreter::InfFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(std::numeric_limits<double>::infinity(), op_stack, stack_pos);
}

void StackInterpreter::NegInfFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(-1.0 * std::numeric_limits<double>::infinity(), op_stack, stack_pos);
}

void StackInterpreter::RandFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(MemoryManager::GetRandomValue(), op_stack, stack_pos);
}

void StackInterpreter::AcoshFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(acosh(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::AsinhFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(asinh(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::AtanhFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(atanh(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::CoshFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(cosh(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::SinhFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(sinh(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::TanhFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  PushFloat(tanh(PopFloat(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::Atan2Float(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = atan2(left_double, right_double);
  (*stack_pos)--;
}

void StackInterpreter::IntToFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: I2F; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushFloat((double)((INT64_VALUE)PopInt(op_stack, stack_pos)), op_stack, stack_pos);
}

void StackInterpreter::Float2Int(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: F2I; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushInt((INT64_VALUE)PopFloat(op_stack, stack_pos), op_stack, stack_pos);
}

void StackInterpreter::SwapInt(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: SWAP_INT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  SwapInt(op_stack, stack_pos);
}

void StackInterpreter::PopInt(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: PopInt; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PopInt(op_stack, stack_pos);
}

void StackInterpreter::PopFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: POP_FLOAT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PopFloat(op_stack, stack_pos);
}

void StackInterpreter::ThreadSleep(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: THREAD_SLEEP; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)PopInt(op_stack, stack_pos);
  std::this_thread::sleep_for(std::chrono::milliseconds(left));
}

void StackInterpreter::LoadClsMem(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CLS_MEM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushInt((size_t)(*stack_frame)->method->GetClass()->GetClassMemory(), op_stack, stack_pos);
}

void StackInterpreter::LoadInstMem(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INST_MEM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  PushInt((*stack_frame)->mem[0], op_stack, stack_pos);
}

void inline StackInterpreter::ModFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
    FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
    FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
    *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = fmod(left_double, right_double);
    (*stack_pos)--;
}
void inline StackInterpreter::PowFloat(StackInstr* instr, size_t*& op_stack, long*& stack_pos) {
  FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = pow(left_double, right_double);
  (*stack_pos)--;
}

void StackInterpreter::StorLoclIntVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_LOCL_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif
  size_t* mem = (*stack_frame)->mem;
  mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
}

void StackInterpreter::StorClsInstIntVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_CLS_INST_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif

  size_t* cls_inst_mem = (size_t*)op_stack[(*stack_pos) - 1];
  if(!cls_inst_mem) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  size_t mem = op_stack[(*stack_pos) - 2];
  (*stack_pos) -= 2;
  cls_inst_mem[instr->GetOperand()] = mem;
}

void StackInterpreter::CopyLoclIntVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: COPY_LOCL_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif
  size_t* mem = (*stack_frame)->mem;
  mem[instr->GetOperand() + 1] = TopInt(op_stack, stack_pos);
}

void StackInterpreter::CopyClsInstIntVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: COPY_CLS_INST_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif

  size_t* cls_inst_mem = (size_t*)PopInt(op_stack, stack_pos);
  if(!cls_inst_mem) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  cls_inst_mem[instr->GetOperand()] = TopInt(op_stack, stack_pos);
}

void StackInterpreter::Str2Int(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: S2I; call_pos=" << (*call_stack_pos) << std::endl;
#endif

  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  long base = (long)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    const wchar_t* str = (wchar_t*)(str_ptr + 3);
    try {
      if(wcslen(str) > 3) {
        if(str[0] == L'-') {
          switch(str[2]) {
            // binary
          case L'b':
            PushInt(-std::stoll(str + 3, nullptr, 2), op_stack, stack_pos);
            return;

            // octal
          case L'o':
            PushInt(-std::stoll(str + 3, nullptr, 8), op_stack, stack_pos);
            return;

            // hexadecimal
          case L'x':
          case L'X':
            PushInt(-std::stoll(str + 3, nullptr, 16), op_stack, stack_pos);
            return;

          default:
            break;
          }
        }
        else {
          switch(str[1]) {
            // binary
          case L'b':
            PushInt(std::stoll(str + 2, nullptr, 2), op_stack, stack_pos);
            return;

            // octal
          case L'o':
            PushInt(std::stoll(str + 2, nullptr, 8), op_stack, stack_pos);
            return;

            // hexadecimal
          case L'x':
          case L'X':
            PushInt(std::stoll(str + 2, nullptr, 16), op_stack, stack_pos);
            return;

          default:
            break;
          }
        }
      }
      else if(wcslen(str) > 2) {
        switch(str[1]) {
          // binary
        case L'b':
          PushInt(std::stoll(str + 2, nullptr, 2), op_stack, stack_pos);
          return;

          // octal
        case L'o':
          PushInt(std::stoll(str + 2, nullptr, 8), op_stack, stack_pos);
          return;

          // hexadecimal
        case L'x':
        case L'X':
          PushInt(std::stoll(str + 2, nullptr, 16), op_stack, stack_pos);
          return;

        default:
          break;
        }
      }
      PushInt(std::stoll(str, nullptr, base), op_stack, stack_pos);
    }
    catch(std::exception &e) {
#ifdef _WIN32    
      UNREFERENCED_PARAMETER(e);
#endif
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
}

void StackInterpreter::Str2Float(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: S2F; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  
  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    wchar_t* str = (wchar_t*)(str_ptr + 3);
    try {
      const FLOAT_VALUE value = std::stod(str);
      PushFloat(value, op_stack, stack_pos);
    }
    catch (std::invalid_argument& e) {
#ifdef _WIN32    
      UNREFERENCED_PARAMETER(e);
#endif
      PushFloat(0.0, op_stack, stack_pos);
    }
  }
  else {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
}

void StackInterpreter::ByteChar2Int(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
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

void StackInterpreter::Int2Str(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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

void inline StackInterpreter::Float2Str(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
  size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
  if(str_ptr) {
    wchar_t* str = (wchar_t*)(str_ptr + 3);
    const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
		
    std::wstringstream formatter;
    std::wstring conv;

		const std::wstring float_format = program->GetProperty(L"float:string:format");
		const std::wstring float_precision = program->GetProperty(L"float:string:precision");

    if(!float_format.empty() && !float_precision.empty()) {
      if(float_format == L"fixed") {
        formatter << std::fixed;
      }
      else if(float_format == L"scientific") {
        formatter << std::scientific;
      }
      else if(float_format == L"hex") {
        formatter << std::hexfloat;
      }
      formatter << std::setprecision(stoll(float_precision));
      
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
    else if(!float_precision.empty()) {
			formatter << std::setprecision(stoll(float_precision));

			formatter << value;
			conv = formatter.str();
    }
    else {
      conv = std::to_wstring(value);
    }

    const size_t max = conv.size() < 64 ? conv.size() : 64;
#ifdef _WIN32
		wcsncpy_s(str, str_ptr[0], conv.c_str(), max);
#else
		wcsncpy(str, conv.c_str(), max);
#endif
  }
}

void StackInterpreter::ShlInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SHL_INT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left << right;
  (*stack_pos)--;
}

void StackInterpreter::ShrInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SHR_INT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left >> right;
  (*stack_pos)--;
}

void StackInterpreter::LoadLoclIntVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_LOCL_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif
  size_t* mem = (*stack_frame)->mem;
  PushInt(mem[instr->GetOperand() + 1], op_stack, stack_pos);
}

void StackInterpreter::LoadClsInstIntVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CLS_INST_INT_VAR; index=" << instr->GetOperand() << std::endl;
#endif      
  size_t* cls_inst_mem = (size_t*)op_stack[(*stack_pos) - 1];
  if(!cls_inst_mem) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  op_stack[(*stack_pos) - 1] = cls_inst_mem[instr->GetOperand()];
}

void StackInterpreter::AndInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: AND; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left && right;
  (*stack_pos)--;
}

void StackInterpreter::OrInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: OR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left || right;
  (*stack_pos)--;
}

void StackInterpreter::AddInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: ADD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left + right;
  (*stack_pos)--;
}

void StackInterpreter::AddFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: ADD; call_pos=" << (*call_stack_pos) << std::endl;
#endif  
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = left_double + right_double;
  (*stack_pos)--;
}

void StackInterpreter::SubInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SUB; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left - right;
  (*stack_pos)--;
}

void StackInterpreter::SubFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: SUB; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = left_double - right_double;
  (*stack_pos)--;
}

void StackInterpreter::MulInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: MUL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left *  right;
  (*stack_pos)--;
}

void StackInterpreter::DivInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: DIV; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  if(!right) {
    std::wcerr << L">>> Divide by zero <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  op_stack[(*stack_pos) - 2] = left / right;
  (*stack_pos)--;
}

void StackInterpreter::MulFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: MUL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = left_double * right_double;
  (*stack_pos)--;
}

void StackInterpreter::DivFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: DIV; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  if(right_double == 0.0) {
    std::wcerr << L">>> Divide by zero <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2])) = left_double / right_double;
  (*stack_pos)--;
}

void StackInterpreter::ModInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: MOD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left % right;
  (*stack_pos)--;
}

void StackInterpreter::BitAndInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_AND; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left & right;
  (*stack_pos)--;
}

void StackInterpreter::BitOrInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_OR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left | right;
  (*stack_pos)--;
}

void StackInterpreter::BitNotInt(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_NOT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  op_stack[(*stack_pos) - 1] = ~left;
}

void StackInterpreter::BitXorInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: BIT_XOR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left ^ right;
  (*stack_pos)--;
}

void StackInterpreter::LesEqlInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left <= right;
  (*stack_pos)--;
}

void StackInterpreter::GtrEqlInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left >= right;
  (*stack_pos)--;
}

void StackInterpreter::LesEqlFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  op_stack[(*stack_pos) - 2] = left_double <= right_double;
  (*stack_pos)--;
}

void StackInterpreter::GtrEqlFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR_EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  op_stack[(*stack_pos) - 2] = left_double >= right_double;
  (*stack_pos)--;
}

void StackInterpreter::EqlInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left == right;
  (*stack_pos)--;
}
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
void StackInterpreter::NeqlInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left != right;
  (*stack_pos)--;
}

void StackInterpreter::LesInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left < right;
  (*stack_pos)--;
}

void StackInterpreter::GtrInt(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const INT64_VALUE left = (INT64_VALUE)op_stack[(*stack_pos) - 1];
  const INT64_VALUE right = (INT64_VALUE)op_stack[(*stack_pos) - 2];
  op_stack[(*stack_pos) - 2] = left > right;
  (*stack_pos)--;
}

void StackInterpreter::EqlFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: EQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  op_stack[(*stack_pos) - 2] = left_double == right_double;
  (*stack_pos)--;
}

void StackInterpreter::NeqlFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEQL; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  op_stack[(*stack_pos) - 2] = left_double != right_double;
  (*stack_pos)--;
}

void StackInterpreter::LesFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LES; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  op_stack[(*stack_pos) - 2] = left_double < right_double;
  (*stack_pos)--;
}

void StackInterpreter::GtrFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: GTR_FLOAT; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  const FLOAT_VALUE left_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 1]));
  const FLOAT_VALUE right_double = *((FLOAT_VALUE*)(&op_stack[(*stack_pos) - 2]));
  op_stack[(*stack_pos) - 2] = left_double > right_double;
  (*stack_pos)--;;
}

void StackInterpreter::LoadArySize(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_ARY_SIZE; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  PushInt(array[2], op_stack, stack_pos);
}

void StackInterpreter::CpyByteAry(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }

  const INT64_VALUE src_array_len = (INT64_VALUE)src_array[2];
  const INT64_VALUE dest_array_len = (INT64_VALUE)dest_array[2];
  if(length > 0 && static_cast<long long>(src_offset) + length <= src_array_len && dest_offset + length <= dest_array_len) {
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

void StackInterpreter::CpyCharAry(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }

  const long src_array_len = (long)src_array[2];
  const long dest_array_len = (long)dest_array[2];
  if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
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

void StackInterpreter::CpyIntAry(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }

  const long src_array_len = (long)src_array[0];
  const long dest_array_len = (long)dest_array[0];
  if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
    size_t* src_array_ptr = src_array + 3;
    size_t* dest_array_ptr = dest_array + 3;
    if(src_array_ptr == dest_array_ptr) {
      memmove(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
    }
    else {
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
    }
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

void StackInterpreter::CpyFloatAry(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
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
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }

  const long src_array_len = (long)src_array[0];
  const long dest_array_len = (long)dest_array[0];
  if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
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

void StackInterpreter::ZeroByteAry(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  char* buffer = (char*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(char));
}

void StackInterpreter::ZeroCharAry(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  wchar_t* buffer = (wchar_t*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(wchar_t));
}

void StackInterpreter::ZeroIntAry(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  size_t* buffer = (size_t*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(size_t));
}

void StackInterpreter::ZeroFloatAry(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
{
  size_t* array_ptr = (size_t*)PopInt(op_stack, stack_pos);
  const size_t array_len = array_ptr[0];
  FLOAT_VALUE* buffer = (FLOAT_VALUE*)(array_ptr + 3);
  memset(buffer, 0, array_len * sizeof(FLOAT_VALUE));
}

void StackInterpreter::ObjTypeOf(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
    exit(1);
#endif
  }
}

void StackInterpreter::ObjInstCast(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
  size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
  const size_t result = (size_t)MemoryManager::ValidObjectCast(mem, instr->GetOperand(), program->GetHierarchy(), program->GetInterfaces());
#ifdef _DEBUG
  std::wcout << L"stack oper: OBJ_INST_CAST: from=" << mem << L", to=" << instr->GetOperand() << std::endl;
#endif
  if(!result && mem) {
    StackClass* to_cls = MemoryManager::GetClass((size_t*)mem);
    std::wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
          << L"' to '" << program->GetClass(instr->GetOperand())->GetName() << L"' <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  PushInt(result, op_stack, stack_pos);
}

void StackInterpreter::AsyncMthdCall(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  size_t* param = (size_t*)(*stack_frame)->mem[1];

  StackClass* impl_class = MemoryManager::GetClass(instance);
  if(!impl_class) {
    PopFrame();
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
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

void StackInterpreter::ThreadJoin(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: THREAD_JOIN; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  if(!instance) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }

#ifdef _WIN32
  HANDLE vm_thread = (HANDLE)instance[0];
  if(WaitForSingleObject(vm_thread, INFINITE) != WAIT_OBJECT_0) {
    std::wcerr << L">>> Unable to join thread! <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    exit(1);
#endif
  }
#else
  void* status;
  pthread_t vm_thread = (pthread_t)instance[0];
  if(pthread_join(vm_thread, &status)) {
    std::wcerr << L">>> Unable to join thread! <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    exit(1);
#endif
  }
#endif
}

void StackInterpreter::ThreadMutex(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: THREAD_MUTEX; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  if(!instance) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
#ifdef _WIN32
  InitializeCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
  pthread_mutex_init((pthread_mutex_t*)&instance[1], nullptr);
#endif
}

void StackInterpreter::CriticalStart(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CRITICAL_START; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!instance) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif        
  }
#ifdef _WIN32
  EnterCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
  pthread_mutex_lock((pthread_mutex_t*)&instance[1]);
#endif
}

void StackInterpreter::CriticalEnd(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: CRITICAL_END; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!instance) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
#ifdef _WIN32
  LeaveCriticalSection((CRITICAL_SECTION*)&instance[1]);
#else
  pthread_mutex_unlock((pthread_mutex_t*)&instance[1]);
#endif
}

/********************************
 * Processes a load function variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFunctionVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      exit(1);
#endif
    }
    PushInt(cls_inst_mem[instr->GetOperand() + 1], op_stack, stack_pos);
    PushInt(cls_inst_mem[instr->GetOperand()], op_stack, stack_pos);
  }
}

/********************************
 * Processes a load float variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      exit(1);
#endif
    }
    value = *((FLOAT_VALUE*)(&cls_inst_mem[instr->GetOperand()]));
  }
  PushFloat(value, op_stack, stack_pos);
}

/********************************
 * Processes a store function variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFunctionVar(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      exit(1);
#endif
    }
    cls_inst_mem[instr->GetOperand()] = PopInt(op_stack, stack_pos);
    cls_inst_mem[instr->GetOperand() + 1] = PopInt(op_stack, stack_pos);
  }
}

/********************************
 * Processes a store float
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      exit(1);
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
void StackInterpreter::ProcessCopyFloat(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      exit(1);
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
void StackInterpreter::ProcessNewObjectInstance(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
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
void StackInterpreter::ProcessNewFunctionInstance(StackInstr* instr, size_t*& op_stack, long*& stack_pos)
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
void StackInterpreter::ProcessNewArray(StackInstr* instr, size_t* &op_stack, long* &stack_pos, bool is_float)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_INT_ARY/NEW_FLOAT_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t indices[8]{};
  const size_t value = PopInt(op_stack, stack_pos);
  size_t size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    const size_t value = PopInt(op_stack, stack_pos);
    size *= value;
    indices[dim++] = value;
  }

  size_t* mem = is_float ? 
    (size_t*)MemoryManager::AllocateArray(static_cast<long>(size) + dim + 2, FLOAT_TYPE, op_stack, *stack_pos) :
    (size_t*)MemoryManager::AllocateArray(static_cast<long>(size) + dim + 2, INT_TYPE, op_stack, *stack_pos);
  mem[0] = size;
  mem[1] = dim;

  memcpy(mem + 2, indices, dim * sizeof(size_t));
  PushInt((size_t)mem, op_stack, stack_pos);
}

/********************************
 * Processes a new byte array instance request.
 ********************************/
void StackInterpreter::ProcessNewByteArray(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_BYTE_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t indices[8]{};
  const size_t value = PopInt(op_stack, stack_pos);
  size_t size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    const size_t value = PopInt(op_stack, stack_pos);
    size *= value;
    indices[dim++] = value;
  }

  // null terminated string 
  size++;
  size_t* mem = MemoryManager::AllocateArray((long)(size + ((static_cast<size_t>(dim) + 2) * sizeof(size_t))), BYTE_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size - 1;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(size_t));
  PushInt((size_t)mem, op_stack, stack_pos);
}

/********************************
 * Processes a new char array instance request.
 ********************************/
void StackInterpreter::ProcessNewCharArray(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: NEW_CHAR_ARY; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t indices[8]{};
  const size_t value = PopInt(op_stack, stack_pos);
  size_t size = value;
  indices[0] = value;
  long dim = 1;
  for(long i = 1; i < instr->GetOperand(); i++) {
    const size_t value = PopInt(op_stack, stack_pos);
    size *= value;
    indices[dim++] = value;
  }

  // null-terminated string 
  size++;
  size_t* mem = MemoryManager::AllocateArray((long)(size + ((static_cast<size_t>(dim) + 2) * sizeof(size_t))), CHAR_ARY_TYPE, op_stack, *stack_pos);
  mem[0] = size - 1;
  mem[1] = dim;
  memcpy(mem + 2, indices, dim * sizeof(size_t));
  PushInt((size_t)mem, op_stack, stack_pos);
}

/********************************
 * Processes a return instruction, 
 * this modifies the call std::stack.
 ********************************/
void StackInterpreter::ProcessReturn(StackInstr** &instrs, long &ip)
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

#ifdef _WIN32
  HANDLE vm_thread = (HANDLE)_beginthreadex(nullptr, 0, AsyncMethodCall, holder, 0, nullptr);
  if(!vm_thread) {
    std::wcerr << L">>> Internal error: Unable to create garbage collection thread! <<<" << std::endl;
    exit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);

  // execute thread
  pthread_t vm_thread;
  if(pthread_create(&vm_thread, &attrs, AsyncMethodCall, (void*)holder)) {
    std::wcerr << L">>> Internal error: Internal error: Unable to create runtime thread! <<<" << std::endl;
    exit(-1);
  }
#endif  
  
  // assign thread ID
  if(!instance) {
    std::wcerr << L">>> Internal error: Unable to create runtime thread! <<<" << std::endl;
    exit(-1);
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
  size_t* thread_op_stack = new size_t[OP_STACK_SIZE];
  long* thread_stack_pos = new long;
  (*thread_stack_pos) = 0;
  
  // set parameter
  thread_op_stack[(*thread_stack_pos)++] = (size_t)holder->param;

#ifdef _DEBUG
  HANDLE vm_thread;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &vm_thread, 0, TRUE, DUPLICATE_SAME_ACCESS);
  std::wcout << L"# Starting thread=" << vm_thread << L" #" << std::endl;
#endif  

  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter;
  AddThread(intpr);
  intpr->Execute(thread_op_stack, thread_stack_pos, 0, holder->called, holder->self, false);
  
#ifdef _DEBUG
  std::wcout << L"# final std::stack: pos=" << (*thread_stack_pos) << L", thread=" << vm_thread << L" #" << std::endl;
#endif

  // clean up
  delete[] thread_op_stack;
  thread_op_stack = nullptr;

  delete thread_stack_pos;
  thread_stack_pos = nullptr;
  
  RemoveThread(intpr);
  delete intpr;
  intpr = nullptr;

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
  size_t* thread_op_stack = new size_t[OP_STACK_SIZE];
  long* thread_stack_pos = new long;
  (*thread_stack_pos) = 0;

  // set parameter
  thread_op_stack[(*thread_stack_pos)++] = (size_t)holder->param;

#ifdef _DEBUG
  std::wcout << L"# Starting thread=" << pthread_self() << L" #" << std::endl;
#endif  

  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter;
  AddThread(intpr);
  intpr->Execute(thread_op_stack, thread_stack_pos, 0, holder->called, holder->self, false);

#ifdef _DEBUG
  std::wcout << L"# final std::stack: pos=" << (*thread_stack_pos) << L", thread=" << pthread_self() << L" #" << std::endl;
#endif
  
  // clean up
  delete[] thread_op_stack;
  thread_op_stack = nullptr;

  delete thread_stack_pos;
  thread_stack_pos = nullptr;

  RemoveThread(intpr);
  delete intpr;
  intpr = nullptr;
  
  delete holder;
  holder = nullptr;
  
  return nullptr;
}
#endif

/********************************
 * Processes a synchronous dynamic method call.
 ********************************/
void StackInterpreter::ProcessDynamicMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, size_t* &op_stack, long* &stack_pos)
{
  // save current method
  (*stack_frame)->ip = ip;
  PushFrame((*stack_frame));

  // make call
  const size_t mthd_cls_id = PopInt(op_stack, stack_pos);
  const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
  const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;

  if(mthd_id < 0 || cls_id < 0) {
    std::wcerr << L"Internal VM error." << std::endl;
    exit(1);
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
  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(called, instance, instrs, ip, op_stack, stack_pos);
  }
  // execute interpreter
  else {
    (*stack_frame) = GetStackFrame(called, instance);    
    instrs = (*stack_frame)->method->GetInstructions();
    ip = 0;
  }
#else
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
#endif
}

/********************************
 * Processes a synchronous method call.
 ********************************/
void StackInterpreter::ProcessMethodCall(StackInstr* instr, StackInstr** &instrs, long &ip, size_t* &op_stack, long* &stack_pos)
{
  // save current method
  (*stack_frame)->ip = ip;
  PushFrame((*stack_frame));

  // pop instance
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  // make call
  StackMethod* concrete_call = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());

	// dynamic method call
  if(concrete_call->IsVirtual()) {
    // lookup binding
    StackClass* concrete_class = MemoryManager::GetClass((size_t*)instance);
    if(!concrete_class) {
      std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
      StackErrorUnwind();
#ifdef _NO_HALT
      halt = true;
      return;
#else
      exit(1);
#endif
    }

    StackMethod* virtual_call = concrete_class->GetVirtualMethod(instr->GetOperand(), instr->GetOperand2());
    if(!virtual_call) {
      // binding method
      const std::wstring qualified_method_name = concrete_call->GetName();
      const std::wstring method_ending = qualified_method_name.substr(qualified_method_name.find(L':'));

      // check method cache
      std::wstring method_name = concrete_class->GetName() + method_ending;
      virtual_call = concrete_class->GetMethod(method_name);
      while(!virtual_call) {
        concrete_class = concrete_class->GetParent();
        method_name = concrete_class->GetName() + method_ending;
        virtual_call = concrete_class->GetMethod(method_name);
      }
      // bind method call
      concrete_class->AddVirutalMethod(instr->GetOperand(), instr->GetOperand2(), virtual_call);
    }
#ifdef _DEBUG
    assert(virtual_call);
#endif
    concrete_call = virtual_call;
  }

#ifndef _NO_JIT
  // execute JIT call
  if(instr->GetOperand3()) {
    ProcessJitMethodCall(concrete_call, instance, instrs, ip, op_stack, stack_pos);
  }
  // execute interpreter
  else {
    ProcessInterpretedMethodCall(concrete_call, instance, instrs, ip);
  }
#else
  ProcessInterpretedMethodCall(concrete_call, instance, instrs, ip);
#endif
}

/********************************
 * Processes an interpreted
 * synchronous method call.
 ********************************/
void StackInterpreter::ProcessJitMethodCall(StackMethod* called, size_t* instance, StackInstr** &instrs, long &ip, size_t* &op_stack, long* &stack_pos)
{
#if defined(_DEBUGGER) || defined(_NO_JIT)
  ProcessInterpretedMethodCall(called, instance, instrs, ip);
#else
  // compile, if needed
  if(!called->GetNativeCode()) {   
#if defined(_M_ARM64)
    JitArm64 jit_compiler;
#elif defined(_WIN64) || defined(_X64)
    JitAmd64 jit_compiler;
#else
    JitArm64 jit_compiler;
#endif

    if(!jit_compiler.Compile(called)) {
      ProcessInterpretedMethodCall(called, instance, instrs, ip);
#ifdef _DEBUG
      std::wcerr << L"### Unable to compile: " << called->GetName() << L" ###" << std::endl;
#endif
      return;
    }
  }
  
  // execute
  (*stack_frame) = GetStackFrame(called, instance);
  JitRuntime jit_executor;
  const long status = jit_executor.Execute(called, instance, op_stack, stack_pos, call_stack, call_stack_pos, *stack_frame);
  if(status < 0) {
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
      std::wcerr << L">>> Divide by zero in native JIT code <<<" << std::endl;
      break;
    }
    StackErrorUnwind(called);
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
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
void StackInterpreter::ProcessInterpretedMethodCall(StackMethod* called, size_t* instance, StackInstr** &instrs, long &ip)
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
void StackInterpreter::ProcessLoadIntArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const INT64_VALUE size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  PushInt(array[index + instr->GetOperand()], op_stack, stack_pos);
}

/********************************
 * Processes a load store array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreIntArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_INT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif

  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  
  const INT64_VALUE size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  array[index + instr->GetOperand()] = PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a load byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadByteArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_BYTE_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const INT64_VALUE size = (INT64_VALUE)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  array += instr->GetOperand();
  PushInt(((unsigned char*)array)[index], op_stack, stack_pos);
}

/********************************
 * Processes a load char array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadCharArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CHAR_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const INT64_VALUE size = (INT64_VALUE)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  array += instr->GetOperand();
  const wchar_t* char_array = (wchar_t*)array;
  PushInt(char_array[index], op_stack, stack_pos);
}

/********************************
 * Processes a store byte array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreByteArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_BYTE_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const INT64_VALUE size = (INT64_VALUE)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  array += instr->GetOperand();
  ((unsigned char*)array)[index] = (char)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a store char array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreCharArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_CHAR_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const long size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  array += instr->GetOperand();
  ((wchar_t*)array)[index] = (wchar_t)PopInt(op_stack, stack_pos);
}

/********************************
 * Processes a load float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessLoadFloatArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_FLOAT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const long size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  FLOAT_VALUE value;
  memcpy(&value, array + index + instr->GetOperand(), sizeof(FLOAT_VALUE));
  PushFloat(value, op_stack, stack_pos);
}

/********************************
 * Processes a store float array
 * variable instruction.
 ********************************/
void StackInterpreter::ProcessStoreFloatArrayElement(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: STOR_FLOAT_ARY_ELM; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
    return;
#else
    exit(1);
#endif
  }
  const long size = (long)array[0];
  array += 2;
  const INT64_VALUE index = ArrayIndex(instr, array, size, op_stack, stack_pos);
#ifdef _NO_HALT
  if(halt) {
    return;
  }
#endif
  FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
  memcpy(array + index + instr->GetOperand(), &value, sizeof(FLOAT_VALUE));
}

/********************************
 * Shared library operations
 ********************************/

typedef void (*ext_load_def)(VMContext& callbacks);
void StackInterpreter::SharedLibraryLoad(StackInstr* instr)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: shared LIBRARY_LOAD; call_pos=" << (*call_stack_pos) << std::endl;
#endif
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  if(!instance) {
    std::wcerr << L">>> Unable to load shared library! <<<" << std::endl;
#ifdef _NO_HALT
    exit(1);
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
    exit(1);
#endif
  }
  
  std::wstring path_str;
#ifdef _OBJECK_NATIVE_LIB_PATH
#ifdef _WIN32
  size_t value_len;
  char value[SMALL_BUFFER_MAX];
  if(!getenv_s(&value_len, value, SMALL_BUFFER_MAX, "OBJECK_LIB_PATH") && strlen(value) > 0) {
    path_str += BytesToUnicode(value);
    path_str += L"\\native\\";
  }
  else {
    path_str += L"..\\lib\\native\\";
  }
#else
  char* value = getenv("OBJECK_LIB_PATH");
  if(value) {
    path_str += BytesToUnicode(value);
    path_str += L"/native/";
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
    exit(1);
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
  HINSTANCE dll_handle = LoadLibrary(dll_string.c_str());
  if(!dll_handle) {
    std::wcerr << L">>> Runtime error loading shared library: " << dll_string.c_str() << L" <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    exit(1);
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
    exit(1);
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
  context.alloc_managed_obj = MemoryManager::AllocateObject;
  (*ext_load)(context);
#else
  void* dll_handle = dlopen(dll_string.c_str(), RTLD_LAZY);
  if(!dll_handle) {
    std::wcerr << L">>> Runtime error loading shared library: " << dlerror() << L" <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    exit(1);
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
    exit(1);
#endif
  }
  // call function
  VMContext context;
  context.data_array = nullptr;
  context.op_stack = nullptr;
  context.stack_pos = nullptr;
  context.call_method_by_id = APITools_MethodCallId;
  context.call_method_by_name = APITools_MethodCall;
  context.alloc_managed_array = MemoryManager::AllocateArray;
  context.alloc_managed_obj = MemoryManager::AllocateObject;
  (*ext_load)(context);
#endif
}

typedef void (*ext_unload_def)();
void StackInterpreter::SharedLibraryUnload(StackInstr* instr)
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
      exit(1);
#endif
    }
    (*ext_unload)();
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
      exit(1);
#endif
    }
    // call function
    (*ext_unload)();
    // unload lib
    dlclose(dll_handle);
  }
#endif
}

typedef void (*lib_func_def) (VMContext& callbacks);
void StackInterpreter::SharedLibraryCall(StackInstr* instr, size_t* &op_stack, long* &stack_pos)
{
  size_t* instance = (size_t*)(*stack_frame)->mem[0];
  size_t* str_obj = (size_t*)(*stack_frame)->mem[1];
  size_t* array = (size_t*)str_obj[0];
  if(!array) {
    std::wcerr << L">>> Runtime error calling function <<<" << std::endl;
#ifdef _NO_HALT
    return;
#else
    exit(1);
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
    // get function pointer
    const std::string str =   UnicodeToBytes(wstr);
    ext_func = (lib_func_def)GetProcAddress(dll_handle, str.c_str());
    if(!ext_func) {
      std::wcerr << L">>> Runtime error calling function: " << wstr << L" <<<" << std::endl;
      FreeLibrary(dll_handle);
#ifdef _NO_HALT
      return;
#else
      exit(1);
#endif
    }
    // call function
    VMContext context;
    context.data_array = args;
    context.op_stack = op_stack;
    context.stack_pos = stack_pos;
    context.call_method_by_name = APITools_MethodCall;
    context.call_method_by_id = APITools_MethodCallId;
    context.alloc_managed_array = MemoryManager::AllocateArray;
    context.alloc_managed_obj = MemoryManager::AllocateObject;
    (*ext_func)(context);
  }
#else
  // load function
  void* dll_handle = (void*)instance[1];
  if(dll_handle) {
    const std::string str = UnicodeToBytes(wstr);
    ext_func = (lib_func_def)dlsym(dll_handle, str.c_str());
    char* error;
    if((error = dlerror()) != nullptr)  {
      std::wcerr << L">>> Runtime error calling function: " << error << L" <<<" << std::endl;
#ifdef _NO_HALT
      return;
#else
      exit(1);
#endif
    }
    // call function
    VMContext context;
    context.data_array = args;
    context.op_stack = op_stack;
    context.stack_pos = stack_pos;
    context.call_method_by_name = APITools_MethodCall;
    context.call_method_by_id = APITools_MethodCallId;
    context.alloc_managed_array = MemoryManager::AllocateArray;
    context.alloc_managed_obj = MemoryManager::AllocateObject;
    (*ext_func)(context);
  }  
#endif
}

StackFrame* Runtime::StackInterpreter::GetStackFrame(StackMethod* method, size_t* instance)
{
#ifdef _WIN32
  EnterCriticalSection(&cached_frames_cs);
#else
  pthread_mutex_lock(&cached_frames_mutex);
#endif
  if(cached_frames.empty()) {
    // load cache
    for(int i = 0; i < CALL_STACK_SIZE; ++i) {
      StackFrame* frame = new StackFrame();
      frame->mem = (size_t*)calloc(LOCAL_SIZE, sizeof(char));
      cached_frames.push(frame);
    }
  }
  StackFrame* frame = cached_frames.top();
  cached_frames.pop();

  frame->method = method;
  frame->mem[0] = (size_t)instance;
  frame->ip = -1;
  frame->jit_called = false;
  frame->jit_mem = nullptr;
  frame->jit_offset = 0;
#ifdef _DEBUG
  std::wcout << L"fetching frame=" << frame << std::endl;
#endif

#ifdef _WIN32
  LeaveCriticalSection(&cached_frames_cs);
#else
  pthread_mutex_unlock(&cached_frames_mutex);
#endif
  return frame;
}

void Runtime::StackInterpreter::ReleaseStackFrame(StackFrame* frame)
{
#ifdef _WIN32
  EnterCriticalSection(&cached_frames_cs);
#else
  pthread_mutex_lock(&cached_frames_mutex);
#endif      

  // load cache
  frame->jit_mem = nullptr;
  memset(frame->mem, 0, LOCAL_SIZE * sizeof(char));
  cached_frames.push(frame);
#ifdef _DEBUG
  std::wcout << L"caching frame=" << frame << std::endl;
#endif    

#ifdef _WIN32
  LeaveCriticalSection(&cached_frames_cs);
#else
  pthread_mutex_unlock(&cached_frames_mutex);
#endif
}

void Runtime::StackInterpreter::StackErrorUnwind()
{
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
    while(--pos) {
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
    while(--pos && pos > -1) {
      std::wcerr << L"  method: pos=" << pos << L", name='"
            << MethodFormatter::Format(call_stack[pos]->method->GetName()) << L"'" << std::endl;
    }
  }
  std::wcerr << L"  ..." << std::endl;
#endif
}

INT64_VALUE Runtime::StackInterpreter::ArrayIndex(StackInstr* instr, size_t* array, const INT64_VALUE size, size_t*& op_stack, long*& stack_pos)
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
    std::wcerr << L">>> Index out of bounds: " << index << L"," << size << L" <<<" << std::endl;
    StackErrorUnwind();
#ifdef _NO_HALT
    halt = true;
#else
    exit(1);
#endif
  }

  return index;
}

size_t* Runtime::StackInterpreter::CreateStringObject(const std::wstring& value_str, size_t*& op_stack, long*& stack_pos)
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
  while(--pos) {
    if(pos > -1) {
      std::wcerr << L"  method: pos=" << pos << L", name='" << MethodFormatter::Format(call_stack[pos]->method->GetName()) << L"'" << std::endl;
    }
  }
  std::wcerr << L"  ..." << std::endl;
}
