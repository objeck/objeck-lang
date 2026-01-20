/***************************************************************************
 * VM dispatch table for opcode handlers.
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

#include "dispatch.h"
#include "interpreter.h"
#include <cmath>
#include <limits>
#include <thread>
#include <chrono>

using namespace Runtime;

//
// Load operations (0-9)
//
static DispatchResult Handle_LOAD_INT_LIT(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INT_LIT; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushInt(ctx.instr->GetInt64Operand(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_CHAR_LIT(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CHAR_LIT; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushInt(ctx.instr->GetOperand(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_FLOAT_LIT(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_FLOAT_LIT; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushFloat(ctx.instr->GetFloatOperand(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_INT_VAR(DispatchContext& ctx) {
  // Not used directly in interpreter, placeholder
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_LOCL_INT_VAR(DispatchContext& ctx) {
  ctx.interp->LoadLoclIntVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_CLS_INST_INT_VAR(DispatchContext& ctx) {
  ctx.interp->LoadClsInstIntVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_FLOAT_VAR(DispatchContext& ctx) {
  ctx.interp->ProcessLoadFloat(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_FUNC_VAR(DispatchContext& ctx) {
  ctx.interp->ProcessLoadFunctionVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_CLS_MEM(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CLS_MEM; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushInt((size_t)(*ctx.stack_frame)->method->GetClass()->GetClassMemory(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_INST_MEM(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_INST_MEM; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushInt((*ctx.stack_frame)->mem[0], ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Store operations (10-14)
//
static DispatchResult Handle_STOR_INT_VAR(DispatchContext& ctx) {
  // Not used directly in interpreter, placeholder
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_LOCL_INT_VAR(DispatchContext& ctx) {
  ctx.interp->StorLoclIntVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_CLS_INST_INT_VAR(DispatchContext& ctx) {
  ctx.interp->StorClsInstIntVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_FLOAT_VAR(DispatchContext& ctx) {
  ctx.interp->ProcessStoreFloat(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_FUNC_VAR(DispatchContext& ctx) {
  ctx.interp->ProcessStoreFunctionVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Copy operations (15-19)
//
static DispatchResult Handle_COPY_INT_VAR(DispatchContext& ctx) {
  // Not used directly in interpreter, placeholder
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_COPY_LOCL_INT_VAR(DispatchContext& ctx) {
  ctx.interp->CopyLoclIntVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_COPY_CLS_INST_INT_VAR(DispatchContext& ctx) {
  ctx.interp->CopyClsInstIntVar(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_COPY_FLOAT_VAR(DispatchContext& ctx) {
  ctx.interp->ProcessCopyFloat(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_COPY_FUNC_VAR(DispatchContext& ctx) {
  // Not used directly in interpreter, placeholder
  return DispatchResult::CONTINUE;
}

//
// Array operations (20-28)
//
static DispatchResult Handle_LOAD_BYTE_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessLoadByteArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_CHAR_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessLoadCharArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_INT_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessLoadIntArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_FLOAT_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessLoadFloatArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_BYTE_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessStoreByteArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_CHAR_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessStoreCharArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_INT_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessStoreIntArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_STOR_FLOAT_ARY_ELM(DispatchContext& ctx) {
  ctx.interp->ProcessStoreFloatArrayElement(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOAD_ARY_SIZE(DispatchContext& ctx) {
  ctx.interp->LoadArySize(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Special values (29-34)
//
static DispatchResult Handle_NAN_INT(DispatchContext& ctx) {
  ctx.interp->PushFloat(std::numeric_limits<INT_VALUE>::quiet_NaN(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_INF_INT(DispatchContext& ctx) {
  ctx.interp->PushFloat(std::numeric_limits<INT_VALUE>::infinity(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEG_INF_INT(DispatchContext& ctx) {
  ctx.interp->PushFloat(-1 * std::numeric_limits<INT_VALUE>::infinity(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NAN_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(std::numeric_limits<double>::quiet_NaN(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_INF_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(std::numeric_limits<double>::infinity(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEG_INF_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(-1.0 * std::numeric_limits<double>::infinity(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Comparison operations (35-46)
//
static DispatchResult Handle_EQL_INT(DispatchContext& ctx) {
  ctx.interp->EqlInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEQL_INT(DispatchContext& ctx) {
  ctx.interp->NeqlInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LES_INT(DispatchContext& ctx) {
  ctx.interp->LesInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_GTR_INT(DispatchContext& ctx) {
  ctx.interp->GtrInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LES_EQL_INT(DispatchContext& ctx) {
  ctx.interp->LesEqlInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_GTR_EQL_INT(DispatchContext& ctx) {
  ctx.interp->GtrEqlInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_EQL_FLOAT(DispatchContext& ctx) {
  ctx.interp->EqlFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEQL_FLOAT(DispatchContext& ctx) {
  ctx.interp->NeqlFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LES_FLOAT(DispatchContext& ctx) {
  ctx.interp->LesFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_GTR_FLOAT(DispatchContext& ctx) {
  ctx.interp->GtrFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LES_EQL_FLOAT(DispatchContext& ctx) {
  ctx.interp->LesEqlFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_GTR_EQL_FLOAT(DispatchContext& ctx) {
  ctx.interp->GtrEqlFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Integer arithmetic operations (47-59)
//
static DispatchResult Handle_AND_INT(DispatchContext& ctx) {
  ctx.interp->AndInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_OR_INT(DispatchContext& ctx) {
  ctx.interp->OrInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ADD_INT(DispatchContext& ctx) {
  ctx.interp->AddInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SUB_INT(DispatchContext& ctx) {
  ctx.interp->SubInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_MUL_INT(DispatchContext& ctx) {
  ctx.interp->MulInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_DIV_INT(DispatchContext& ctx) {
  ctx.interp->DivInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_MOD_INT(DispatchContext& ctx) {
  ctx.interp->ModInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_BIT_AND_INT(DispatchContext& ctx) {
  ctx.interp->BitAndInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_BIT_OR_INT(DispatchContext& ctx) {
  ctx.interp->BitOrInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_BIT_XOR_INT(DispatchContext& ctx) {
  ctx.interp->BitXorInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_BIT_NOT_INT(DispatchContext& ctx) {
  ctx.interp->BitNotInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SHL_INT(DispatchContext& ctx) {
  ctx.interp->ShlInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SHR_INT(DispatchContext& ctx) {
  ctx.interp->ShrInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Float arithmetic operations (60-90)
//
static DispatchResult Handle_ADD_FLOAT(DispatchContext& ctx) {
  ctx.interp->AddFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SUB_FLOAT(DispatchContext& ctx) {
  ctx.interp->SubFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_MUL_FLOAT(DispatchContext& ctx) {
  ctx.interp->MulFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_DIV_FLOAT(DispatchContext& ctx) {
  ctx.interp->DivFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_FLOR_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(floor(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CEIL_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(ceil(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_TRUNC_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(trunc(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SIN_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(sin(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_COS_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(cos(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_TAN_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(tan(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ASIN_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(asin(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ACOS_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(acos(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ATAN_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(atan(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOG2_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(log2(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CBRT_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(cbrt(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_COSH_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(cosh(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SINH_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(sinh(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_TANH_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(tanh(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ATAN2_FLOAT(DispatchContext& ctx) {
  const FLOAT_VALUE left = *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 2]));
  const FLOAT_VALUE right = *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 1]));
  *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 2])) = atan2(left, right);
  (*ctx.stack_pos)--;
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ACOSH_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(acosh(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ASINH_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(asinh(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ATANH_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(atanh(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_MOD_FLOAT(DispatchContext& ctx) {
  const FLOAT_VALUE left = *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 2]));
  const FLOAT_VALUE right = *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 1]));
  *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 2])) = fmod(left, right);
  (*ctx.stack_pos)--;
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOG_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(log(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ROUND_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(round(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_EXP_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(exp(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LOG10_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(log10(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_POW_FLOAT(DispatchContext& ctx) {
  const FLOAT_VALUE left = *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 2]));
  const FLOAT_VALUE right = *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 1]));
  *((FLOAT_VALUE*)(&ctx.op_stack[(*ctx.stack_pos) - 2])) = pow(left, right);
  (*ctx.stack_pos)--;
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SQRT_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(sqrt(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_GAMMA_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(tgamma(ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_RAND_FLOAT(DispatchContext& ctx) {
  ctx.interp->PushFloat(MemoryManager::GetRandomValue(), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Conversions (91-96)
//
static DispatchResult Handle_I2F(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: I2F; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushFloat((double)((INT64_VALUE)ctx.interp->PopInt(ctx.op_stack, ctx.stack_pos)), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_F2I(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: F2I; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PushInt((INT64_VALUE)ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos), ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_S2I(DispatchContext& ctx) {
  ctx.interp->Str2Int(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_S2F(DispatchContext& ctx) {
  ctx.interp->Str2Float(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_I2S(DispatchContext& ctx) {
  ctx.interp->Int2Str(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_F2S(DispatchContext& ctx) {
  ctx.interp->Float2Str(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Control flow (97-101)
//
static DispatchResult Handle_MTHD_CALL(DispatchContext& ctx) {
  ctx.interp->ProcessMethodCall(ctx.instr, ctx.instrs, *ctx.ip, ctx.op_stack, ctx.stack_pos);
  // return directly back to JIT code
  if((*ctx.stack_frame)->jit_called) {
    (*ctx.stack_frame)->jit_called = false;
    StackInterpreter::ReleaseStackFrame(*ctx.stack_frame);
    return DispatchResult::RETURN_JIT;
  }
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_DYN_MTHD_CALL(DispatchContext& ctx) {
  ctx.interp->ProcessDynamicMethodCall(ctx.instr, ctx.instrs, *ctx.ip, ctx.op_stack, ctx.stack_pos);
  // return directly back to JIT code
  if((*ctx.stack_frame)->jit_called) {
    (*ctx.stack_frame)->jit_called = false;
    StackInterpreter::ReleaseStackFrame(*ctx.stack_frame);
    return DispatchResult::RETURN_JIT;
  }
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_JMP(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: JMP; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  if(ctx.instr->GetOperand2() < 0) {
    *ctx.ip = ctx.instr->GetOperand();
  }
  else if((INT64_VALUE)ctx.interp->PopInt(ctx.op_stack, ctx.stack_pos) == ctx.instr->GetOperand2()) {
    *ctx.ip = ctx.instr->GetOperand();
  }
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LBL(DispatchContext& ctx) {
  // Label - no operation needed
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_RTRN(DispatchContext& ctx) {
  ctx.interp->ProcessReturn(ctx.instrs, *ctx.ip);
  // return directly back to JIT code
  if((*ctx.stack_frame) && (*ctx.stack_frame)->jit_called) {
    (*ctx.stack_frame)->jit_called = false;
    StackInterpreter::ReleaseStackFrame(*ctx.stack_frame);
    return DispatchResult::RETURN_JIT;
  }
  return DispatchResult::CONTINUE;
}

//
// Memory operations (102-117)
//
static DispatchResult Handle_NEW_BYTE_ARY(DispatchContext& ctx) {
  ctx.interp->ProcessNewByteArray(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEW_CHAR_ARY(DispatchContext& ctx) {
  ctx.interp->ProcessNewCharArray(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEW_INT_ARY(DispatchContext& ctx) {
  ctx.interp->ProcessNewArray(ctx.instr, ctx.op_stack, ctx.stack_pos, false);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEW_FLOAT_ARY(DispatchContext& ctx) {
  ctx.interp->ProcessNewArray(ctx.instr, ctx.op_stack, ctx.stack_pos, true);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEW_OBJ_INST(DispatchContext& ctx) {
  ctx.interp->ProcessNewObjectInstance(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_NEW_FUNC_INST(DispatchContext& ctx) {
  ctx.interp->ProcessNewFunctionInstance(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CPY_BYTE_ARY(DispatchContext& ctx) {
  ctx.interp->CpyByteAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CPY_CHAR_ARY(DispatchContext& ctx) {
  ctx.interp->CpyCharAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CPY_INT_ARY(DispatchContext& ctx) {
  ctx.interp->CpyIntAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CPY_FLOAT_ARY(DispatchContext& ctx) {
  ctx.interp->CpyFloatAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ZERO_BYTE_ARY(DispatchContext& ctx) {
  ctx.interp->ZeroByteAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ZERO_CHAR_ARY(DispatchContext& ctx) {
  ctx.interp->ZeroCharAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ZERO_INT_ARY(DispatchContext& ctx) {
  ctx.interp->ZeroIntAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_ZERO_FLOAT_ARY(DispatchContext& ctx) {
  ctx.interp->ZeroFloatAry(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Type operations (118-119)
//
static DispatchResult Handle_OBJ_INST_CAST(DispatchContext& ctx) {
  ctx.interp->ObjInstCast(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_OBJ_TYPE_OF(DispatchContext& ctx) {
  ctx.interp->ObjTypeOf(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Trap operations (120-123)
//
static DispatchResult Handle_TRAP(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: TRAP; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  if(!ctx.interp->ProcessTrap(ctx.op_stack, ctx.stack_pos)) {
    ctx.interp->StackErrorUnwind();
#ifdef _NO_HALT
    *ctx.halt = true;
    return DispatchResult::HALT;
#else
    exit(1);
#endif
  }
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_TRAP_RTRN(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: TRAP_RTRN; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  if(!ctx.interp->ProcessTrap(ctx.op_stack, ctx.stack_pos)) {
    ctx.interp->StackErrorUnwind();
#ifdef _NO_HALT
    *ctx.halt = true;
    return DispatchResult::HALT;
#else
    exit(1);
#endif
  }
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_SET_SIGNAL(DispatchContext& ctx) {
  // Not used in interpreter switch
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_RAISE_SIGNAL(DispatchContext& ctx) {
  // Not used in interpreter switch
  return DispatchResult::CONTINUE;
}

//
// External library operations (124-126)
//
static DispatchResult Handle_EXT_LIB_LOAD(DispatchContext& ctx) {
  ctx.interp->SharedLibraryLoad(ctx.instr);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_EXT_LIB_UNLOAD(DispatchContext& ctx) {
  ctx.interp->SharedLibraryUnload(ctx.instr);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_EXT_LIB_FUNC_CALL(DispatchContext& ctx) {
  ctx.interp->SharedLibraryCall(ctx.instr, ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Stack operations (127-129)
//
static DispatchResult Handle_SWAP_INT(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: SWAP_INT; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->SwapInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_POP_INT(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: PopInt; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PopInt(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_POP_FLOAT(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: POP_FLOAT; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  ctx.interp->PopFloat(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Thread operations (130-135)
//
static DispatchResult Handle_ASYNC_MTHD_CALL(DispatchContext& ctx) {
  ctx.interp->AsyncMthdCall(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_THREAD_JOIN(DispatchContext& ctx) {
  ctx.interp->ThreadJoin(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_THREAD_SLEEP(DispatchContext& ctx) {
#ifdef _DEBUG
  std::wcout << L"stack oper: THREAD_SLEEP; call_pos=" << (*ctx.call_stack_pos) << std::endl;
#endif
  INT64_VALUE sleep_time = (INT64_VALUE)ctx.interp->PopInt(ctx.op_stack, ctx.stack_pos);
  std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_THREAD_MUTEX(DispatchContext& ctx) {
  ctx.interp->ThreadMutex(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CRITICAL_START(DispatchContext& ctx) {
  ctx.interp->CriticalStart(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_CRITICAL_END(DispatchContext& ctx) {
  ctx.interp->CriticalEnd(ctx.op_stack, ctx.stack_pos);
  return DispatchResult::CONTINUE;
}

//
// Library directives (136-140)
//
static DispatchResult Handle_LIB_OBJ_TYPE_OF(DispatchContext& ctx) {
  // Used for library calls, not in interpreter
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LIB_NEW_OBJ_INST(DispatchContext& ctx) {
  // Used for library calls, not in interpreter
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LIB_MTHD_CALL(DispatchContext& ctx) {
  // Used for library calls, not in interpreter
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LIB_OBJ_INST_CAST(DispatchContext& ctx) {
  // Used for library calls, not in interpreter
  return DispatchResult::CONTINUE;
}

static DispatchResult Handle_LIB_FUNC_DEF(DispatchContext& ctx) {
  // Used for library calls, not in interpreter
  return DispatchResult::CONTINUE;
}

//
// End marker (141)
//
static DispatchResult Handle_END_STMTS(DispatchContext& ctx) {
  // End of statements marker, no operation
  return DispatchResult::CONTINUE;
}

//
// Global dispatch table indexed by InstructionType enum
//
OpcodeHandler Runtime::instr_dispatch[] = {
  // Load operations (0-9)
  Handle_LOAD_INT_LIT,          // 0: LOAD_INT_LIT
  Handle_LOAD_CHAR_LIT,         // 1: LOAD_CHAR_LIT
  Handle_LOAD_FLOAT_LIT,        // 2: LOAD_FLOAT_LIT
  Handle_LOAD_INT_VAR,          // 3: LOAD_INT_VAR
  Handle_LOAD_LOCL_INT_VAR,     // 4: LOAD_LOCL_INT_VAR
  Handle_LOAD_CLS_INST_INT_VAR, // 5: LOAD_CLS_INST_INT_VAR
  Handle_LOAD_FLOAT_VAR,        // 6: LOAD_FLOAT_VAR
  Handle_LOAD_FUNC_VAR,         // 7: LOAD_FUNC_VAR
  Handle_LOAD_CLS_MEM,          // 8: LOAD_CLS_MEM
  Handle_LOAD_INST_MEM,         // 9: LOAD_INST_MEM

  // Store operations (10-14)
  Handle_STOR_INT_VAR,          // 10: STOR_INT_VAR
  Handle_STOR_LOCL_INT_VAR,     // 11: STOR_LOCL_INT_VAR
  Handle_STOR_CLS_INST_INT_VAR, // 12: STOR_CLS_INST_INT_VAR
  Handle_STOR_FLOAT_VAR,        // 13: STOR_FLOAT_VAR
  Handle_STOR_FUNC_VAR,         // 14: STOR_FUNC_VAR

  // Copy operations (15-19)
  Handle_COPY_INT_VAR,          // 15: COPY_INT_VAR
  Handle_COPY_LOCL_INT_VAR,     // 16: COPY_LOCL_INT_VAR
  Handle_COPY_CLS_INST_INT_VAR, // 17: COPY_CLS_INST_INT_VAR
  Handle_COPY_FLOAT_VAR,        // 18: COPY_FLOAT_VAR
  Handle_COPY_FUNC_VAR,         // 19: COPY_FUNC_VAR

  // Array operations (20-28)
  Handle_LOAD_BYTE_ARY_ELM,     // 20: LOAD_BYTE_ARY_ELM
  Handle_LOAD_CHAR_ARY_ELM,     // 21: LOAD_CHAR_ARY_ELM
  Handle_LOAD_INT_ARY_ELM,      // 22: LOAD_INT_ARY_ELM
  Handle_LOAD_FLOAT_ARY_ELM,    // 23: LOAD_FLOAT_ARY_ELM
  Handle_STOR_BYTE_ARY_ELM,     // 24: STOR_BYTE_ARY_ELM
  Handle_STOR_CHAR_ARY_ELM,     // 25: STOR_CHAR_ARY_ELM
  Handle_STOR_INT_ARY_ELM,      // 26: STOR_INT_ARY_ELM
  Handle_STOR_FLOAT_ARY_ELM,    // 27: STOR_FLOAT_ARY_ELM
  Handle_LOAD_ARY_SIZE,         // 28: LOAD_ARY_SIZE

  // Special values (29-34)
  Handle_NAN_INT,               // 29: NAN_INT
  Handle_INF_INT,               // 30: INF_INT
  Handle_NEG_INF_INT,           // 31: NEG_INF_INT
  Handle_NAN_FLOAT,             // 32: NAN_FLOAT
  Handle_INF_FLOAT,             // 33: INF_FLOAT
  Handle_NEG_INF_FLOAT,         // 34: NEG_INF_FLOAT

  // Comparison operations (35-46)
  Handle_EQL_INT,               // 35: EQL_INT
  Handle_NEQL_INT,              // 36: NEQL_INT
  Handle_LES_INT,               // 37: LES_INT
  Handle_GTR_INT,               // 38: GTR_INT
  Handle_LES_EQL_INT,           // 39: LES_EQL_INT
  Handle_GTR_EQL_INT,           // 40: GTR_EQL_INT
  Handle_EQL_FLOAT,             // 41: EQL_FLOAT
  Handle_NEQL_FLOAT,            // 42: NEQL_FLOAT
  Handle_LES_FLOAT,             // 43: LES_FLOAT
  Handle_GTR_FLOAT,             // 44: GTR_FLOAT
  Handle_LES_EQL_FLOAT,         // 45: LES_EQL_FLOAT
  Handle_GTR_EQL_FLOAT,         // 46: GTR_EQL_FLOAT

  // Integer operations (47-59)
  Handle_AND_INT,               // 47: AND_INT
  Handle_OR_INT,                // 48: OR_INT
  Handle_ADD_INT,               // 49: ADD_INT
  Handle_SUB_INT,               // 50: SUB_INT
  Handle_MUL_INT,               // 51: MUL_INT
  Handle_DIV_INT,               // 52: DIV_INT
  Handle_MOD_INT,               // 53: MOD_INT
  Handle_BIT_AND_INT,           // 54: BIT_AND_INT
  Handle_BIT_OR_INT,            // 55: BIT_OR_INT
  Handle_BIT_XOR_INT,           // 56: BIT_XOR_INT
  Handle_BIT_NOT_INT,           // 57: BIT_NOT_INT
  Handle_SHL_INT,               // 58: SHL_INT
  Handle_SHR_INT,               // 59: SHR_INT

  // Float operations (60-90)
  Handle_ADD_FLOAT,             // 60: ADD_FLOAT
  Handle_SUB_FLOAT,             // 61: SUB_FLOAT
  Handle_MUL_FLOAT,             // 62: MUL_FLOAT
  Handle_DIV_FLOAT,             // 63: DIV_FLOAT
  Handle_FLOR_FLOAT,            // 64: FLOR_FLOAT
  Handle_CEIL_FLOAT,            // 65: CEIL_FLOAT
  Handle_TRUNC_FLOAT,           // 66: TRUNC_FLOAT
  Handle_SIN_FLOAT,             // 67: SIN_FLOAT
  Handle_COS_FLOAT,             // 68: COS_FLOAT
  Handle_TAN_FLOAT,             // 69: TAN_FLOAT
  Handle_ASIN_FLOAT,            // 70: ASIN_FLOAT
  Handle_ACOS_FLOAT,            // 71: ACOS_FLOAT
  Handle_ATAN_FLOAT,            // 72: ATAN_FLOAT
  Handle_LOG2_FLOAT,            // 73: LOG2_FLOAT
  Handle_CBRT_FLOAT,            // 74: CBRT_FLOAT
  Handle_COSH_FLOAT,            // 75: COSH_FLOAT
  Handle_SINH_FLOAT,            // 76: SINH_FLOAT
  Handle_TANH_FLOAT,            // 77: TANH_FLOAT
  Handle_ATAN2_FLOAT,           // 78: ATAN2_FLOAT
  Handle_ACOSH_FLOAT,           // 79: ACOSH_FLOAT
  Handle_ASINH_FLOAT,           // 80: ASINH_FLOAT
  Handle_ATANH_FLOAT,           // 81: ATANH_FLOAT
  Handle_MOD_FLOAT,             // 82: MOD_FLOAT
  Handle_LOG_FLOAT,             // 83: LOG_FLOAT
  Handle_ROUND_FLOAT,           // 84: ROUND_FLOAT
  Handle_EXP_FLOAT,             // 85: EXP_FLOAT
  Handle_LOG10_FLOAT,           // 86: LOG10_FLOAT
  Handle_POW_FLOAT,             // 87: POW_FLOAT
  Handle_SQRT_FLOAT,            // 88: SQRT_FLOAT
  Handle_GAMMA_FLOAT,           // 89: GAMMA_FLOAT
  Handle_RAND_FLOAT,            // 90: RAND_FLOAT

  // Conversions (91-96)
  Handle_I2F,                   // 91: I2F
  Handle_F2I,                   // 92: F2I
  Handle_S2I,                   // 93: S2I
  Handle_S2F,                   // 94: S2F
  Handle_I2S,                   // 95: I2S
  Handle_F2S,                   // 96: F2S

  // Control flow (97-101)
  Handle_MTHD_CALL,             // 97: MTHD_CALL
  Handle_DYN_MTHD_CALL,         // 98: DYN_MTHD_CALL
  Handle_JMP,                   // 99: JMP
  Handle_LBL,                   // 100: LBL
  Handle_RTRN,                  // 101: RTRN

  // Memory operations (102-117)
  Handle_NEW_BYTE_ARY,          // 102: NEW_BYTE_ARY
  Handle_NEW_CHAR_ARY,          // 103: NEW_CHAR_ARY
  Handle_NEW_INT_ARY,           // 104: NEW_INT_ARY
  Handle_NEW_FLOAT_ARY,         // 105: NEW_FLOAT_ARY
  Handle_NEW_OBJ_INST,          // 106: NEW_OBJ_INST
  Handle_NEW_FUNC_INST,         // 107: NEW_FUNC_INST
  Handle_CPY_BYTE_ARY,          // 108: CPY_BYTE_ARY
  Handle_CPY_CHAR_ARY,          // 109: CPY_CHAR_ARY
  Handle_CPY_INT_ARY,           // 110: CPY_INT_ARY
  Handle_CPY_FLOAT_ARY,         // 111: CPY_FLOAT_ARY
  Handle_ZERO_BYTE_ARY,         // 112: ZERO_BYTE_ARY
  Handle_ZERO_CHAR_ARY,         // 113: ZERO_CHAR_ARY
  Handle_ZERO_INT_ARY,          // 114: ZERO_INT_ARY
  Handle_ZERO_FLOAT_ARY,        // 115: ZERO_FLOAT_ARY

  // Type operations (116-117)
  Handle_OBJ_INST_CAST,         // 116: OBJ_INST_CAST
  Handle_OBJ_TYPE_OF,           // 117: OBJ_TYPE_OF

  // Trap operations (118-121)
  Handle_TRAP,                  // 118: TRAP
  Handle_TRAP_RTRN,             // 119: TRAP_RTRN
  Handle_SET_SIGNAL,            // 120: SET_SIGNAL
  Handle_RAISE_SIGNAL,          // 121: RAISE_SIGNAL

  // External library (122-124)
  Handle_EXT_LIB_LOAD,          // 122: EXT_LIB_LOAD
  Handle_EXT_LIB_UNLOAD,        // 123: EXT_LIB_UNLOAD
  Handle_EXT_LIB_FUNC_CALL,     // 124: EXT_LIB_FUNC_CALL

  // Stack operations (125-127)
  Handle_SWAP_INT,              // 125: SWAP_INT
  Handle_POP_INT,               // 126: POP_INT
  Handle_POP_FLOAT,             // 127: POP_FLOAT

  // Thread operations (128-133)
  Handle_ASYNC_MTHD_CALL,       // 128: ASYNC_MTHD_CALL
  Handle_THREAD_JOIN,           // 129: THREAD_JOIN
  Handle_THREAD_SLEEP,          // 130: THREAD_SLEEP
  Handle_THREAD_MUTEX,          // 131: THREAD_MUTEX
  Handle_CRITICAL_START,        // 132: CRITICAL_START
  Handle_CRITICAL_END,          // 133: CRITICAL_END

  // Library directives (134-138)
  Handle_LIB_OBJ_TYPE_OF,       // 134: LIB_OBJ_TYPE_OF
  Handle_LIB_NEW_OBJ_INST,      // 135: LIB_NEW_OBJ_INST
  Handle_LIB_MTHD_CALL,         // 136: LIB_MTHD_CALL
  Handle_LIB_OBJ_INST_CAST,     // 137: LIB_OBJ_INST_CAST
  Handle_LIB_FUNC_DEF,          // 138: LIB_FUNC_DEF

  // End marker (139)
  Handle_END_STMTS              // 139: END_STMTS
};
