/**
 * JIT compiler for ARMv8 architecture (Apple Silicon and Linux ARM 64-bit)
 *
 * Copyright (c) 2020-2024, Randy Hollines
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
 * - Neither the name of the Objeck team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILSITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "jit_arm_a64.h"

#include <bitset>

using namespace Runtime;

PageManager* JitArm64::page_manager;

void JitArm64::Initialize(StackProgram* p) {
  JitCompiler::Initialize(p);
  page_manager = new PageManager;
}

// setup of stack frame
void JitArm64::Prolog() {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [<prolog>]" << std::endl;
#endif

  const long final_local_space = local_space + RED_ZONE;
  uint32_t sub_offset = 0xd10183ff;
  sub_offset |= final_local_space << 10;
  
  uint32_t setup_code[] = {
    0xF94003E8, // ldr x8, [sp, #0]
    0xF94007E9, // ldr x9, [sp, #8]
    0xF9400BEA, // ldr x10, [sp, #16]
    sub_offset, // sub sp, sp, #final_local_space
    0xf9002fe0, // str x0, [sp, #88]
    0xf9002be1, // str x1, [sp, #80]
    0xf90027e2, // str x2, [sp, #72]
    0xf90023e3, // str x3, [sp, #64]
    0xf9001fe4, // str x4, [sp, #56]
    0xf9001be5, // str x5, [sp, #48]
    0xf90017e6, // str x6, [sp, #40]
    0xf90013e7, // str x7, [sp, #32]
    0xf9000fe8, // str x8, [sp, #24]
    0xf9000be9, // str x9, [sp, #16]
    0xF90033Ea, // str x10, [sp, #96]
    // Save callee-saved FP registers D8-D15
    0xFD003408, // str d8, [sp, #104]
    0xFD003809, // str d9, [sp, #112]
    0xFD003C0A, // str d10, [sp, #120]
    0xFD00400B, // str d11, [sp, #128]
    0xFD00440C, // str d12, [sp, #136]
    0xFD00480D, // str d13, [sp, #144]
    0xFD004C0E, // str d14, [sp, #152]
    0xFD00500F  // str d15, [sp, #160]
  };
  
  // copy setup
  const int setup_size = sizeof(setup_code) / sizeof(uint32_t);
  for(int i = 0; i < setup_size; ++i) {
    AddMachineCode(setup_code[i]);
  }
}

// tear down of stack frame
void JitArm64::Epilog() {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [<epilog>]" << std::endl;
#endif
  
  epilog_index = code_index;
  
  // nominal
  uint32_t op_code = B_INSTR;
  op_code |= 9;
  AddMachineCode(op_code);
  
  // nullptr deref
  move_imm_reg(-1, X0);
  op_code = B_INSTR;
  op_code |= 8;
  AddMachineCode(op_code);
  
  // under bounds
  move_imm_reg(-2, X0);
  op_code = B_INSTR;
  op_code |= 6;
  AddMachineCode(op_code);

  // over bounds
  move_imm_reg(-3, X0);
  op_code = B_INSTR;
  op_code |= 4;
  AddMachineCode(op_code);

  // divide by zero
  move_imm_reg(-4, X0);
  op_code = B_INSTR;
  op_code |= 2;
  AddMachineCode(op_code);
  
  const long final_local_space = local_space + RED_ZONE;
  uint32_t add_offset = 0x910183ff;
  add_offset |= final_local_space << 10;
  
  move_imm_reg(0, X0);
  // Restore callee-saved FP registers D8-D15
  uint32_t teardown_code[] = {
    0xFD403408, // ldr d8, [sp, #104]
    0xFD403809, // ldr d9, [sp, #112]
    0xFD403C0A, // ldr d10, [sp, #120]
    0xFD40400B, // ldr d11, [sp, #128]
    0xFD40440C, // ldr d12, [sp, #136]
    0xFD40480D, // ldr d13, [sp, #144]
    0xFD404C0E, // ldr d14, [sp, #152]
    0xFD40500F, // ldr d15, [sp, #160]
    add_offset, // add sp, sp, #final_local_space
    0xd65f03c0  // ret
  };
  
  // copy tear down
  const int teardown_size = sizeof(teardown_code) / sizeof(uint32_t);
  for(int i = 0; i < teardown_size; ++i) {
    AddMachineCode(teardown_code[i]);
  }
}

// register with memory manager
void JitArm64::RegisterRoot() {
  const size_t mem_offset = RED_ZONE + sizeof(size_t);
  size_t offset = local_space - TMP_D3;
  if(realign_stack) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"*** Realign Stack ***" << std::endl;
#endif
    offset += sizeof(size_t);
  }
  
  RegisterHolder* holder = GetRegister();
  RegisterHolder* mem_holder = GetRegister();
 
  move_sp_reg(holder->GetRegister());
  add_imm_reg(mem_offset, holder->GetRegister());
  
  // set JIT memory pointer to stack
  move_mem_reg(JIT_MEM, SP, mem_holder->GetRegister());
  move_reg_mem(holder->GetRegister(), 0, mem_holder->GetRegister());

  // set JIT offset value
  move_mem_reg(JIT_OFFSET, SP, mem_holder->GetRegister());
  move_imm_mem((long)offset, 0, mem_holder->GetRegister());

  // clean up
  ReleaseRegister(mem_holder);
  ReleaseRegister(holder);
    
  // zero out memory
  RegisterHolder* start_reg = GetRegister();
  RegisterHolder* end_reg = GetRegister();
  RegisterHolder* cur_reg = GetRegister();
  
  // set start
  move_sp_reg(start_reg->GetRegister());
  add_imm_reg(TMP_X0, start_reg->GetRegister());
  
  // set end
  move_sp_reg(end_reg->GetRegister());
  add_imm_reg((long)(TMP_X0 + offset), end_reg->GetRegister());
  
  // compare
  cmp_reg_reg(start_reg->GetRegister(), end_reg->GetRegister());
#ifdef _DEBUG_JIT_JIT
  std::std::wcout << L"  " << (++instr_count) << L": [b.lt]" << std::endl;
#endif
  AddMachineCode(0x540000CB);
  
  // zero out address and advance
  move_reg_reg(start_reg->GetRegister(), cur_reg->GetRegister());
  move_imm_mem(0, 0, cur_reg->GetRegister());
  add_imm_reg(8, start_reg->GetRegister());
  
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [b <imm>]" << std::endl;
#endif
  
  uint32_t op_code = 0x17000000;
  op_code |= -6 & 0x00ffffff;
  AddMachineCode(op_code);
  
  sub_imm_reg(2, X3);
  
  ReleaseRegister(cur_reg);
  ReleaseRegister(end_reg);
  ReleaseRegister(start_reg);
}

void JitArm64::ProcessParameters(long params) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"CALLED_PARMS: regs=" << aval_regs.size() << endl;
#endif
  
  for(long i = 0; i < params; ++i) {
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, SP, op_stack_holder->GetRegister());

    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);

    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
      
    if(instr->GetType() == STOR_LOCL_INT_VAR || instr->GetType() == STOR_CLS_INST_INT_VAR) {
      RegisterHolder* dest_holder = GetRegister();
      dec_mem(0, stack_pos_holder->GetRegister());
      move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
      shl_imm_reg(3, stack_pos_holder->GetRegister());
      add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
      move_mem_reg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
      working_stack.push_front(new RegInstr(dest_holder));
      // store int
      ProcessStore(instr);
    }
    else if(instr->GetType() == STOR_FUNC_VAR) {
      RegisterHolder* dest_holder = GetRegister();
      dec_mem(0, stack_pos_holder->GetRegister());
      move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
      shl_imm_reg(3, stack_pos_holder->GetRegister());
      add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
      move_mem_reg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
      
      RegisterHolder* dest_holder2 = GetRegister();
      move_mem_reg(-(long)(sizeof(size_t)), op_stack_holder->GetRegister(), dest_holder2->GetRegister());
      
      move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
      dec_mem(0, stack_pos_holder->GetRegister());
      
      working_stack.push_front(new RegInstr(dest_holder2));
      working_stack.push_front(new RegInstr(dest_holder));
      
      // store int
      ProcessStore(instr);
      i++;
    }
    else {
      RegisterHolder* dest_holder = GetFpRegister();
      dec_mem(0, stack_pos_holder->GetRegister());
      move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
      shl_imm_reg(3, stack_pos_holder->GetRegister());
      add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
      move_mem_freg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
      working_stack.push_front(new RegInstr(dest_holder));
      // store float
      ProcessStore(instr);
    }
    ReleaseRegister(op_stack_holder);
    ReleaseRegister(stack_pos_holder);
  }
}

void JitArm64::ProcessIntCallParameter() {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"INT_CALL: regs=" << aval_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, SP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
  
  dec_mem(0, stack_pos_holder->GetRegister());
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(3, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
  move_mem_reg(0, op_stack_holder->GetRegister(), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));
  
  ReleaseRegister(stack_pos_holder);
}

void JitArm64::ProcessFunctionCallParameter() {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"FUNC_CALL: regs=" << aval_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, SP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
  
  sub_imm_mem(2, 0, stack_pos_holder->GetRegister());

  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(3, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
  
  RegisterHolder* holder = GetRegister();
  move_reg_reg(op_stack_holder->GetRegister(), holder->GetRegister());
  
  move_mem_reg(0, op_stack_holder->GetRegister(), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));
  
  move_mem_reg(4, holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));
  
  ReleaseRegister(stack_pos_holder);
}

void JitArm64::ProcessFloatCallParameter() {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"FLOAT_CALL: regs=" << aval_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, SP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
  
  RegisterHolder* dest_holder = GetFpRegister();
  dec_mem(0, stack_pos_holder->GetRegister());
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(3, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
  move_mem_freg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
  working_stack.push_front(new RegInstr(dest_holder));
  
  ReleaseRegister(op_stack_holder);
  ReleaseRegister(stack_pos_holder);
}

void JitArm64::ProcessInstructions() {
  while(instr_index < method->GetInstructionCount() && compile_success) {
    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);
    
    switch(instr->GetType()) {
      // load literal
    case LOAD_CHAR_LIT:
    case LOAD_INT_LIT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_INT: value=" << instr->GetOperand() << L"; regs=" << aval_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
      break;

      // float literal
    case LOAD_FLOAT_LIT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_FLOAT_LIT: value=" << instr->GetFloatOperand()
            << L"; regs=" << aval_regs.size() << endl;
#endif
      float_consts[floats_index] = instr->GetFloatOperand();
      working_stack.push_front(new RegInstr(&float_consts[floats_index++]));
      break;
      
      // load self
    case LOAD_INST_MEM: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_INST_MEM; regs=" << aval_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;

      // load self
    case LOAD_CLS_MEM: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_CLS_MEM; regs=" << aval_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;
      
      // load variable
    case LOAD_LOCL_INT_VAR:
    case LOAD_CLS_INST_INT_VAR:
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_INT_VAR/LOAD_FLOAT_VAR/LOAD_FUNC_VAR: id=" << instr->GetOperand() << L"; regs="
            << aval_regs.size() << endl;
#endif
      ProcessLoad(instr);
      break;
    
      // store value
    case STOR_LOCL_INT_VAR:
    case STOR_CLS_INST_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"STOR_INT_VAR/STOR_FLOAT_VAR/STOR_FUNC_VAR: id=" << instr->GetOperand()
            << L"; regs=" << aval_regs.size() << endl;
#endif
      ProcessStore(instr);
      break;

      // copy value
    case COPY_LOCL_INT_VAR:
    case COPY_CLS_INST_INT_VAR:
    case COPY_FLOAT_VAR:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"COPY_INT_VAR/COPY_FLOAT_VAR: id=" << instr->GetOperand()
            << L"; regs=" << aval_regs.size() << endl;
#endif
      ProcessCopy(instr);
      break;
      
      // mathematical
    case AND_INT:
    case OR_INT:
    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
      // comparison
    case LES_INT:
    case GTR_INT:
    case LES_EQL_INT:
    case GTR_EQL_INT:
    case EQL_INT:
    case NEQL_INT:
    case SHL_INT:
    case SHR_INT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"INT ADD/SUB/MUL/DIV/MOD/BIT_AND/BIT_OR/BIT_XOR/LES/GTR/EQL/NEQL/SHL_INT/SHR_INT:: regs="
            << aval_regs.size() << endl;
#endif
      ProcessIntCalculation(instr);
      break;
      
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT ADD/SUB/MUL/DIV/: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);
      break;
    
    case SIN_FLOAT:
    case COS_FLOAT:
    case TAN_FLOAT:
    case ASIN_FLOAT:
    case ACOS_FLOAT:
    case ATAN_FLOAT:
    case ACOSH_FLOAT:
    case ASINH_FLOAT:
    case ATANH_FLOAT:
    case COSH_FLOAT:
    case SINH_FLOAT:
    case TANH_FLOAT:
    case LOG2_FLOAT:
    case CBRT_FLOAT:
    case LOG_FLOAT:
    case EXP_FLOAT:
    case LOG10_FLOAT:
    case TRUNC_FLOAT:
    case GAMMA_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT SIN/COS/TAN/SQRT/FLOR/CEIL: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatOperation(instr);
      break;
        
    case ROUND_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT ROUND: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatRound(instr, L'r');
      break;
        
    case FLOR_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT FLOR: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatRound(instr, L'f');
      break;
        
    case CEIL_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT CEIL: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatRound(instr, L'c');
      break;
        
    case SQRT_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT SQRT: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatSquareRoot(instr);
      break;
      
    case ATAN2_FLOAT:
    case POW_FLOAT:
    case MOD_FLOAT:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"POW/ATAN2/MOD_FLOAT: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatOperation2(instr);
      break;
      
    case LES_FLOAT:
    case GTR_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT:

#ifdef _DEBUG_JIT_JIT
      std::wcout << L"FLOAT LES/GTR/EQL/NEQL: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);
      break;
      
    case RTRN:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"RTRN: regs=" << aval_regs.size() << endl;
#endif
      ProcessReturn();
      // teardown
      Epilog();
      break;
      
    case MTHD_CALL: {
      StackMethod* called_method = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
      if(called_method) {
#ifdef _DEBUG_JIT_JIT
        assert(called_method);
        std::wcout << L"MTHD_CALL: name='" << called_method->GetName() << L"': id="<< instr->GetOperand()
              << L"," << instr->GetOperand2() << L", params=" << (called_method->GetParamCount() + 1)
              << L": regs=" << aval_regs.size() << endl;
#endif
        // passing instance variable
        ProcessStackCallback(MTHD_CALL, instr, instr_index, called_method->GetParamCount() + 1);
        ProcessReturnParameters(called_method->GetReturn());
      }
    }
      break;
      
    case DYN_MTHD_CALL: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"DYN_MTHD_CALL: regs=" << aval_regs.size() << endl;
#endif
      // passing instance variable
      ProcessStackCallback(DYN_MTHD_CALL, instr, instr_index, instr->GetOperand() + 3);
      ProcessReturnParameters((MemoryType)instr->GetOperand2());
    }
      break;
      
    case NEW_BYTE_ARY:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"NEW_BYTE_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << endl;
#endif
      ProcessStackCallback(NEW_BYTE_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_CHAR_ARY:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"NEW_CHAR_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << endl;
#endif
      ProcessStackCallback(NEW_CHAR_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_INT_ARY:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"NEW_INT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << endl;
#endif
      ProcessStackCallback(NEW_INT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_FLOAT_ARY:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"NEW_FLOAT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << endl;
#endif
      ProcessStackCallback(NEW_FLOAT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_OBJ_INST: {
#ifdef _DEBUG_JIT_JIT
      StackClass* called_klass = program->GetClass(instr->GetOperand());
      std::wcout << L"NEW_OBJ_INST: name='" << called_klass->GetName() << L"': id=" << instr->GetOperand()
            << L": regs=" << aval_regs.size() << endl;
#endif
      // note: object id passed in instruction param
      ProcessStackCallback(NEW_OBJ_INST, instr, instr_index, 0);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
     
    case THREAD_JOIN: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"THREAD_JOIN: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(THREAD_JOIN, instr, instr_index, 0);
    }
      break;

    case THREAD_SLEEP: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"THREAD_SLEEP: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(THREAD_SLEEP, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_START: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"CRITICAL_START: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(CRITICAL_START, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_END: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"CRITICAL_END: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(CRITICAL_END, instr, instr_index, 1);
    }
      break;
      
    case CPY_BYTE_ARY: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"CPY_BYTE_ARY: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_BYTE_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_CHAR_ARY: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"CPY_CHAR_ARY: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_CHAR_ARY, instr, instr_index, 5);
    }
      break;
      
    case CPY_INT_ARY: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"CPY_INT_ARY: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_INT_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_FLOAT_ARY: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"CPY_FLOAT_ARY: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_FLOAT_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_BYTE_ARY: {
#ifdef _DEBUG_JIT
      std::std::wcout << L"ZERO_BYTE_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_BYTE_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_CHAR_ARY: {
#ifdef _DEBUG_JIT
      std::std::wcout << L"ZERO_CHAR_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_CHAR_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_INT_ARY: {
#ifdef _DEBUG_JIT
      std::std::wcout << L"ZERO_INT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_INT_ARY, instr, instr_index, 5);
    }
      break;

    case ZERO_FLOAT_ARY: {
#ifdef _DEBUG_JIT
      std::std::wcout << L"ZERO_FLOAT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(ZERO_FLOAT_ARY, instr, instr_index, 5);
    }
      break;
 
    case TRAP:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"TRAP: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(TRAP, instr, instr_index, instr->GetOperand());
      break;

    case TRAP_RTRN:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"TRAP_RTRN: args=" << instr->GetOperand() << L"; regs="
            << aval_regs.size() << endl;
      assert(instr->GetOperand());
#endif
      ProcessStackCallback(TRAP_RTRN, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case STOR_BYTE_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"STOR_BYTE_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessStoreByteElement(instr);
      break;

    case STOR_CHAR_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"STOR_CHAR_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessStoreCharElement(instr);
      break;
      
    case STOR_INT_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"STOR_INT_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessStoreIntElement(instr);
      break;

    case STOR_FLOAT_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"STOR_FLOAT_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessStoreFloatElement(instr);
      break;

    case SWAP_INT: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"SWAP_INT: regs=" << aval_regs.size() << endl;
#endif
      RegInstr* left = working_stack.front();
      working_stack.pop_front();

      RegInstr* right = working_stack.front();
      working_stack.pop_front();

      working_stack.push_front(left);
      working_stack.push_front(right);
    }
      break;

    case POP_INT:
    case POP_FLOAT: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"POP_INT/POP_FLOAT: regs=" << aval_regs.size() << endl;
#endif
      // note: there may be constants that aren't
      // in registers and don't need to be popped
      if(!working_stack.empty()) {
        // pop and release
        RegInstr* left = working_stack.front();
        working_stack.pop_front();
        if(left->GetType() == REG_INT) {
          ReleaseRegister(left->GetRegister());
        }
        else if(left->GetType() == REG_FLOAT) {
          ReleaseFpRegister(left->GetRegister());
        }
        // clean up
        delete left;
        left = nullptr;
      }
    }
      break;
      
    case F2I:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"F2I: regs=" << aval_regs.size() << endl;
#endif
      ProcessFloatToInt(instr);
      break;

    case I2F:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"I2F: regs=" << aval_regs.size() << endl;
#endif
      ProcessIntToFloat(instr);
      break;

    case I2S:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"I2S: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(I2S, instr, instr_index, 3);
      break;
      
    case F2S:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"F2S: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(F2S, instr, instr_index, 2);
      break;
      
    case S2F:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"S2F: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(S2F, instr, instr_index, 2);
      ProcessReturnParameters(FLOAT_TYPE);
      break;

    case RAND_FLOAT:
#ifdef _DEBUG_JIT
      std::wcout << L"RAND_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
      ProcessStackCallback(RAND_FLOAT, instr, instr_index, 1);
      ProcessReturnParameters(FLOAT_TYPE);
      break;
      
    case S2I:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"S2I: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(S2I, instr, instr_index, 2);
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case OBJ_TYPE_OF: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"OBJ_TYPE_OF: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(OBJ_TYPE_OF, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case OBJ_INST_CAST: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"OBJ_INST_CAST: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(OBJ_INST_CAST, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;

    case LOAD_ARY_SIZE: {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_ARY_SIZE: regs=" << aval_regs.size() << endl;
#endif
      ProcessStackCallback(LOAD_ARY_SIZE, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case LOAD_BYTE_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_BYTE_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessLoadByteElement(instr);
      break;

    case LOAD_CHAR_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_CHAR_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessLoadCharElement(instr);
      break;
      
    case LOAD_INT_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_INT_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessLoadIntElement(instr);
      break;

    case LOAD_FLOAT_ARY_ELM:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LOAD_FLOAT_ARY_ELM: regs=" << aval_regs.size() << endl;
#endif
      ProcessLoadFloatElement(instr);
      break;

      case BIT_NOT_INT:
#ifdef _DEBUG_JIT
      std::std::wcout << L"BIT_NOT_INT: regs=" << aval_regs.size() << endl;
#endif
      ProcessNot(instr);
      break;
      
    case JMP:
      ProcessJump(instr);
      break;
      
    case LBL:
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"LBL: id=" << instr->GetOperand() << endl;
#endif
      break;
      
    default: {
      InstructionType error = (InstructionType)instr->GetType();
      wcerr << L"Unknown instruction: " << error << L"!" << std::endl;
      exit(1);
    }
      break;
    }
  }
}

void Runtime::JitArm64::ProcessNot(StackInstr* instr)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
  case IMM_INT: {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(left->GetOperand(), holder->GetRegister());
    not_reg(holder->GetRegister());
    working_stack.push_front(new RegInstr(holder));
  }
    break;

  case REG_INT:
    not_reg(left->GetRegister()->GetRegister());
    working_stack.push_front(new RegInstr(left->GetRegister()));
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    not_reg(holder->GetRegister());
    working_stack.push_front(new RegInstr(holder));
  }
    break;

  default:
    std::wcerr << L">>> Should never occur (compiler bug?) type=" << left->GetType() << L" <<<" << std::endl;
    exit(1);
    break;
  }
}

void JitArm64::ProcessLoad(StackInstr* instr) {
  // method/function memory
  if(instr->GetOperand2() == LOCL) {
    if(instr->GetType() == LOAD_FUNC_VAR) {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(instr->GetOperand3() + sizeof(size_t), SP, holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
      
      RegisterHolder* holder2 = GetRegister();
      move_mem_reg(instr->GetOperand3(), SP, holder2->GetRegister());
      working_stack.push_front(new RegInstr(holder2));
    }
    else {
      working_stack.push_front(new RegInstr(instr));
    }
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();
    
    RegisterHolder* holder;
    if(left->GetType() == REG_INT) {
      holder = left->GetRegister();
    }
    else {
      holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    }
    CheckNilDereference(holder->GetRegister());
    
    // int value
    if(instr->GetType() == LOAD_LOCL_INT_VAR ||
       instr->GetType() == LOAD_CLS_INST_INT_VAR) {
      move_mem_reg(instr->GetOperand3(), holder->GetRegister(), holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
    // function value
    else if(instr->GetType() == LOAD_FUNC_VAR) {
      RegisterHolder* holder2 = GetRegister();
      move_mem_reg(instr->GetOperand3() + sizeof(size_t), holder->GetRegister(), holder2->GetRegister());
      working_stack.push_front(new RegInstr(holder2));
      
      move_mem_reg(instr->GetOperand3(), holder->GetRegister(), holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
    }
    // float value
    else {
      RegisterHolder* fp_holder = GetFpRegister();
      move_mem_freg(instr->GetOperand3(), holder->GetRegister(), fp_holder->GetRegister());
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(fp_holder));
    }

    delete left;
    left = nullptr;
  }
}

void JitArm64::ProcessJump(StackInstr* instr) {
  if(!skip_jump) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"JMP: id=" << instr->GetOperand() << L", regs=" << aval_regs.size()
          << endl;
#endif
    if(instr->GetOperand2() < 0) {
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"  " << (++instr_count) << L": [b <imm>]" << std::endl;
#endif
      AddMachineCode(B_INSTR);
    }
    else {
      RegInstr* left = working_stack.front();
      working_stack.pop_front();

      switch(left->GetType()) {
      case IMM_INT:{
        RegisterHolder* holder = GetRegister();
        move_imm_reg(left->GetOperand(), holder->GetRegister());
        cmp_imm_reg(instr->GetOperand2(), holder->GetRegister());
        ReleaseRegister(holder);
      }
        break;
        
      case REG_INT:
        cmp_imm_reg(instr->GetOperand2(), left->GetRegister()->GetRegister());
        ReleaseRegister(left->GetRegister());
        break;

      case MEM_INT: {
        RegisterHolder* holder = GetRegister();
        move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
        cmp_imm_reg(instr->GetOperand2(), holder->GetRegister());
        ReleaseRegister(holder);
      }
        break;

      default:
        wcerr << L">>> Should never occur (compiler bug?) type=" << left->GetType() << L" <<<" << std::endl;
        exit(1);
        break;
      }

      // compare with register
#ifdef _DEBUG_JIT_JIT
      std::std::wcout << L"  " << (++instr_count) << L": [b.eq]" << std::endl;
#endif
      AddMachineCode(0x54000000);
      
      // clean up
      delete left;
      left = nullptr;
    }
    
    // store update index
    jump_table.insert(pair<long, StackInstr*>(code_index, instr));
  }
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();
    skip_jump = false;

    // release register
    if(left->GetType() == REG_INT) {
      ReleaseRegister(left->GetRegister());
    }

    // clean up
    delete left;
    left = nullptr;
  }
}

void JitArm64::ProcessReturnParameters(MemoryType type) {
  switch(type) {
  case INT_TYPE:
    ProcessIntCallParameter();
    break;

  case FLOAT_TYPE:
    ProcessFloatCallParameter();
    break;

  case FUNC_TYPE:
    ProcessFunctionCallParameter();
    break;
    
  default:
    break;
  }
}

void JitArm64::ProcessLoadByteElement(StackInstr* instr) {
  RegisterHolder* holder = GetRegister();
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());
  move_mem8_reg(0, elem_holder->GetRegister(), holder->GetRegister());
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitArm64::ProcessLoadCharElement(StackInstr* instr) {
  RegisterHolder* holder = GetRegister();
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());
#ifdef _M_ARM64
  move_mem16_reg(0, elem_holder->GetRegister(), holder->GetRegister());
#else
  move_mem32_reg(0, elem_holder->GetRegister(), holder->GetRegister());
#endif
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitArm64::ProcessLoadIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  move_mem_reg(0, elem_holder->GetRegister(), elem_holder->GetRegister());
  working_stack.push_front(new RegInstr(elem_holder));
}

void JitArm64::ProcessLoadFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(0, elem_holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));
  ReleaseRegister(elem_holder);
}

void JitArm64::ProcessStoreByteElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem8((int8_t)left->GetOperand(), 0, elem_holder->GetRegister());
    ReleaseRegister(elem_holder);
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  default:
    break;
  }
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessStoreCharElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem32(int32_t(left->GetOperand()), 0, elem_holder->GetRegister());
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
#ifdef _M_ARM64
    move_reg_mem16(holder->GetRegister(), 0, elem_holder->GetRegister());
#else
    move_reg_mem32(holder->GetRegister(), 0, elem_holder->GetRegister());
#endif
    ReleaseRegister(holder);
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
#ifdef _M_ARM64
    move_reg_mem16(holder->GetRegister(), 0, elem_holder->GetRegister());
#else
    move_reg_mem32(holder->GetRegister(), 0, elem_holder->GetRegister());
#endif
    ReleaseRegister(holder);
  }
    break;

  default:
    break;
  }
  
  ReleaseRegister(elem_holder);
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessStoreIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem((long)left->GetOperand(), 0, elem_holder->GetRegister());
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
  }
    break;

  default:
    break;
  }
  ReleaseRegister(elem_holder);
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessStoreFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT:
    move_imm_memf(left, 0, elem_holder->GetRegister());
    break;

  case MEM_FLOAT:
  case MEM_INT: {
    RegisterHolder* holder = GetFpRegister();
    move_mem_freg((long)left->GetOperand(), SP, holder->GetRegister());
    move_freg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseFpRegister(holder);
  }
    break;

  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_freg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseFpRegister(holder);
  }
    break;

  default:
    break;
  }
  ReleaseRegister(elem_holder);
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessFloatToInt(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetRegister();
  switch(left->GetType()) {
  case IMM_FLOAT:
    vcvt_imm_reg(left, holder->GetRegister());
    break;
    
  case MEM_FLOAT:
  case MEM_INT:
    vcvt_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    break;

  case REG_FLOAT:
    vcvt_freg_reg(left->GetRegister()->GetRegister(), holder->GetRegister());
    ReleaseFpRegister(left->GetRegister());
    break;

  default:
    break;
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitArm64::ProcessIntToFloat(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetFpRegister();
  switch(left->GetType()) {
  case IMM_INT:
    vcvt_imm_freg(left, holder->GetRegister());
    break;
    
  case MEM_INT:
    vcvt_mem_freg((long)left->GetOperand(),
     SP, holder->GetRegister());
    break;

  case REG_INT:
    vcvt_reg_freg(left->GetRegister()->GetRegister(),
                 holder->GetRegister());
    ReleaseRegister(left->GetRegister());
    break;

  default:
    break;
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitArm64::ProcessStore(StackInstr* instr) {
  Register dest;
  RegisterHolder* addr_holder = nullptr;

  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = SP;
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();
    
    if(left->GetRegister()) {
      addr_holder = left->GetRegister();
    }
    else {
      addr_holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), SP, addr_holder->GetRegister());
    }
    dest = addr_holder->GetRegister();
    CheckNilDereference(dest);
    
    delete left;
    left = nullptr;
  }
  
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    if(instr->GetType() == STOR_FUNC_VAR) {
      move_imm_mem((long)left->GetOperand(), instr->GetOperand3(), dest);
      
      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_imm_mem((long)left2->GetOperand(), instr->GetOperand3() + sizeof(size_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_imm_mem((long)left->GetOperand(), instr->GetOperand3(), dest);
    }
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    if(instr->GetType() == STOR_FUNC_VAR) {
      move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);

      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_mem_reg((long)left2->GetOperand(), SP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3() + sizeof(size_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    }
    ReleaseRegister(holder);
  }
    break;
    
  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    if(instr->GetType() == STOR_FUNC_VAR) {
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
      
      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      RegisterHolder* holder2  = left2->GetRegister();
      move_reg_mem(holder2->GetRegister(), instr->GetOperand3() + sizeof(size_t), dest);
      ReleaseRegister(holder2);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    }
    ReleaseRegister(holder);
  }
    break;
    
  case IMM_FLOAT:
    move_imm_memf(left, instr->GetOperand3(), dest);
    break;
    
  case MEM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    move_mem_freg((long)left->GetOperand(), SP, holder->GetRegister());
    move_freg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    ReleaseFpRegister(holder);
  }
    break;
    
  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_freg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    ReleaseFpRegister(holder);
  }
    break;
  }

  if(addr_holder) {
    ReleaseRegister(addr_holder);
  }

  delete left;
  left = nullptr;
}

void JitArm64::ProcessCopy(StackInstr* instr) {
  Register dest;
  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = SP;
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();

    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    CheckNilDereference(holder->GetRegister());
    dest = holder->GetRegister();
    ReleaseRegister(holder);
    
    delete left;
    left = nullptr;
  }
  
  RegInstr* left = working_stack.front();
  switch(left->GetType()) {
  case IMM_INT: {
    RegisterHolder* holder = GetRegister();
    move_imm_reg(left->GetOperand(), holder->GetRegister());
    move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case REG_INT: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
  }
    break;

  case IMM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    move_imm_freg(left, holder->GetRegister());
    move_freg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case MEM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    move_mem_freg((long)left->GetOperand(), SP, holder->GetRegister());
    move_freg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;
    
  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_freg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
  }
    break;
  }
}

void JitArm64::ProcessStackCallback(long instr_id, StackInstr* instr, long &instr_index, long params) {
  int32_t non_params;
  if(params < 0) {
    non_params = 0;
  }
  else {
    non_params = (int32_t)working_stack.size() - params;
  }
  
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"Return: params=" << params << L", non-params=" << non_params << endl;
#endif
  
  stack<RegInstr*> regs;
  stack<int32_t> dirty_regs;
  int32_t reg_offset = TMP_X0;

  stack<RegInstr*> fp_regs;
  stack<int32_t> dirty_fp_regs;
  int32_t fp_offset = TMP_D0;
  
  int32_t i = 0;
  for(deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
    RegInstr* left = (*iter);
    if(i < non_params) {
      switch(left->GetType()) {
      case REG_INT:
        move_reg_mem(left->GetRegister()->GetRegister(), reg_offset, SP);
        dirty_regs.push(reg_offset);
        regs.push(left);
        reg_offset += sizeof(size_t);
        break;

      case REG_FLOAT:
        move_freg_mem(left->GetRegister()->GetRegister(), fp_offset, SP);
        dirty_fp_regs.push(fp_offset);
        fp_regs.push(left);
        fp_offset += sizeof(size_t);
        break;

      default:
        break;
      }
      // update
      i++;
    }
  }

#ifdef _DEBUG_JIT_JIT
  assert(reg_offset <= TMP_X5);
  assert(fp_offset <= TMP_D3);
#endif

  if(dirty_regs.size() > 6 || dirty_fp_regs.size() > 4 ) {
    compile_success = false;
  }

  // copy values to execution stack
  ProcessReturn(params);
  
  // set parameters
  move_imm_mem(instr_index - 1, 8, SP);
  move_mem_reg(CALL_STACK_POS, SP, X10);
  move_reg_mem(X10, 0, SP);
  
  move_mem_reg(CALL_STACK, SP, X7);
  move_mem_reg(OP_STACK_POS, SP, X6);
  move_mem_reg(OP_STACK, SP, X5);
  move_mem_reg(INSTANCE_MEM, SP, X4);
  move_mem_reg(MTHD_ID, SP, X3);
  move_mem_reg(CLS_ID, SP, X2);
  move_imm_reg((size_t)instr, X1);
  move_imm_reg(instr_id, X0);
  
  move_imm_reg((size_t)JitArm64::JitStackCallback, X10);
  call_reg(X10);
  
  // restore register values
  while(!dirty_regs.empty()) {
    RegInstr* left = regs.top();
    move_mem_reg(dirty_regs.top(), SP, left->GetRegister()->GetRegister());
    // update
    regs.pop();
    dirty_regs.pop();
  }
  
  while(!dirty_fp_regs.empty()) {
    RegInstr* left = fp_regs.top();
    move_mem_freg(dirty_fp_regs.top(), SP, left->GetRegister()->GetRegister());
    // update
    fp_regs.pop();
    dirty_fp_regs.pop();
  }
}

void JitArm64::ProcessReturn(long params) {
  if(!working_stack.empty()) {
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, SP, op_stack_holder->GetRegister());
    
    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
    move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
    shl_imm_reg(3, stack_pos_holder->GetRegister());
    add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());

    int32_t non_params;
    if(params < 0) {
      non_params = 0;
    }
    else {
      non_params = (int32_t)(working_stack.size() - params);
    }
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"Return: params=" << params << L", non-params=" << non_params << endl;
#endif
    
    int32_t i = 0;
    for(deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
      // skip non-params... processed above
      RegInstr* left = (*iter);
      if(i < non_params) {
        i++;
      }
      else {
        move_mem_reg(OP_STACK_POS, SP, stack_pos_holder->GetRegister());
        switch(left->GetType()) {
        case IMM_INT:
          move_imm_mem((long)left->GetOperand(), 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(size_t), op_stack_holder->GetRegister());
          break;
  
        case MEM_INT: {
            RegisterHolder* temp_holder = GetRegister();
            move_mem_reg((long)left->GetOperand(), SP, temp_holder->GetRegister());
            move_reg_mem(temp_holder->GetRegister(), 0, op_stack_holder->GetRegister());
            inc_mem(0, stack_pos_holder->GetRegister());
            add_imm_reg(sizeof(size_t), op_stack_holder->GetRegister());
            ReleaseRegister(temp_holder);
          }
          break;
  
        case REG_INT:
          move_reg_mem(left->GetRegister()->GetRegister(), 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(size_t), op_stack_holder->GetRegister());
          break;
  
        case IMM_FLOAT:
          move_imm_memf(left, 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(double), op_stack_holder->GetRegister());
          break;
  
        case MEM_FLOAT: {
            RegisterHolder* temp_holder = GetFpRegister();
            move_mem_freg((long)left->GetOperand(), SP, temp_holder->GetRegister());
            move_freg_mem(temp_holder->GetRegister(), 0, op_stack_holder->GetRegister());
            inc_mem(0, stack_pos_holder->GetRegister());
            add_imm_reg(sizeof(double), op_stack_holder->GetRegister());
            ReleaseFpRegister(temp_holder);
          }
          break;
  
        case REG_FLOAT:
          move_freg_mem(left->GetRegister()->GetRegister(), 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(double), op_stack_holder->GetRegister());
          break;
        }
      }
    }
    ReleaseRegister(op_stack_holder);
    ReleaseRegister(stack_pos_holder);
    
    // clean up working stack
    if(params < 0) {
      params = (long)working_stack.size();
    }
    for(int32_t i = 0; i < params; ++i) {
      RegInstr* left = working_stack.front();
      working_stack.pop_front();

      // release register
      switch(left->GetType()) {
      case REG_INT:
        ReleaseRegister(left->GetRegister());
        break;

      case REG_FLOAT:
        ReleaseFpRegister(left->GetRegister());
        break;

      default:
        break;
      }
      
      // clean up
      delete left;
      left = nullptr;
    }
  }
}

RegInstr* JitArm64::ProcessIntFold(long left_imm, long right_imm, InstructionType type) {
  switch(type) {
  case AND_INT:
    // Bug fix: Use bitwise AND (&), not logical AND (&&)
    return new RegInstr(IMM_INT, left_imm & right_imm);

  case OR_INT:
    // Bug fix: Use bitwise OR (|), not logical OR (||)
    return new RegInstr(IMM_INT, left_imm | right_imm);
    
  case ADD_INT:
    return new RegInstr(IMM_INT, left_imm + right_imm);
    
  case SUB_INT:
    return new RegInstr(IMM_INT, left_imm - right_imm);
    
  case MUL_INT:
    return new RegInstr(IMM_INT, left_imm * right_imm);
    
  case DIV_INT:
    return new RegInstr(IMM_INT, left_imm / right_imm);
    
  case MOD_INT:
    return new RegInstr(IMM_INT, left_imm % right_imm);
    
  case SHL_INT:
    return new RegInstr(IMM_INT, left_imm << right_imm);
    
  case SHR_INT:
    return new RegInstr(IMM_INT, left_imm >> right_imm);
    
  case BIT_AND_INT:
    return new RegInstr(IMM_INT, left_imm & right_imm);
    
  case BIT_OR_INT:
    return new RegInstr(IMM_INT, left_imm | right_imm);
    
  case BIT_XOR_INT:
    return new RegInstr(IMM_INT, left_imm ^ right_imm);
    
  case LES_INT:
    return new RegInstr(IMM_INT, left_imm < right_imm);
    
  case GTR_INT:
    return new RegInstr(IMM_INT, left_imm > right_imm);
    
  case EQL_INT:
    return new RegInstr(IMM_INT, left_imm == right_imm);
    
  case NEQL_INT:
    return new RegInstr(IMM_INT, left_imm != right_imm);
    
  case LES_EQL_INT:
    return new RegInstr(IMM_INT, left_imm <= right_imm);
    
  case GTR_EQL_INT:
    return new RegInstr(IMM_INT, left_imm >= right_imm);
    
  default:
    return nullptr;
  }
}

void JitArm64::ProcessIntCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
    // intermidate
  case IMM_INT:
    switch(right->GetType()) {
    case IMM_INT:
      working_stack.push_front(ProcessIntFold((long)left->GetOperand(), (long)right->GetOperand(), instruction->GetType()));
      break;
      
    case REG_INT: {
      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());
      RegisterHolder* holder = right->GetRegister();

      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(), instruction->GetType());
      
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)right->GetOperand(), SP, holder->GetRegister());

      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());

      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(), instruction->GetType());
      
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    default:
      break;
    }
    break;

    // register
  case REG_INT:
    switch(right->GetType()) {
    case IMM_INT: {
      RegisterHolder* holder = left->GetRegister();
      math_imm_reg((long)right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
    case REG_INT: {
      RegisterHolder* holder = right->GetRegister();
      math_reg_reg(holder->GetRegister(), left->GetRegister()->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(left->GetRegister()));
      ReleaseRegister(holder);
    }
      break;
    case MEM_INT: {
      RegisterHolder* lhs = left->GetRegister();
      RegisterHolder* rhs = GetRegister();
      move_mem_reg((long)right->GetOperand(), SP, rhs->GetRegister());
      math_reg_reg(rhs->GetRegister(), lhs->GetRegister(), instruction->GetType());
      ReleaseRegister(rhs);
      working_stack.push_front(new RegInstr(lhs));
    }
      break;

    default:
      break;
    }
    break;

    // memory
  case MEM_INT:
    switch(right->GetType()) {
    case IMM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
      math_imm_reg((long)right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
    case REG_INT: {
      RegisterHolder* lhs = right->GetRegister();
      RegisterHolder* rhs = GetRegister();
      move_mem_reg((long)left->GetOperand(), SP, rhs->GetRegister());
      math_reg_reg(lhs->GetRegister(), rhs->GetRegister(), instruction->GetType());
      ReleaseRegister(lhs);
      working_stack.push_front(new RegInstr(rhs));
    }
      break;
    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg((long)left->GetOperand(), SP, holder->GetRegister());
      math_mem_reg((long)right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;

    default:
      break;
    }
    break;

  default:
    break;
  }

  delete left;
  left = nullptr;
    
  delete right;
  right = nullptr;
}

 void JitArm64::ProcessFloatCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
  switch(left->GetType()) {
    // intermidate
  case IMM_FLOAT:
    switch(right->GetType()) {
    case IMM_FLOAT: {
      RegisterHolder* left_holder = GetFpRegister();
      move_imm_freg(left, left_holder->GetRegister());
      
      RegisterHolder* right_holder = GetFpRegister();
      move_imm_freg(right, right_holder->GetRegister());
      
      math_freg_freg(right_holder->GetRegister(), left_holder, instruction->GetType());
      ReleaseFpRegister(right_holder);
      working_stack.push_front(new RegInstr(left_holder));
    }
      break;
      
    case REG_FLOAT: {
      RegisterHolder* imm_holder = GetFpRegister();
      move_imm_freg(left, imm_holder->GetRegister());
      
      math_freg_freg(right->GetRegister()->GetRegister(), imm_holder, type);
      ReleaseFpRegister(right->GetRegister());
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = GetFpRegister();
      move_mem_freg((long)right->GetOperand(), SP, holder->GetRegister());

      RegisterHolder* imm_holder = GetFpRegister();
      move_imm_freg(left, imm_holder->GetRegister());

      math_freg_freg(holder->GetRegister(), imm_holder, type);
      ReleaseFpRegister(holder);
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    default:
      break;
    }
    break;

    // register
  case REG_FLOAT:
    switch(right->GetType()) {
    case IMM_FLOAT: {
      RegisterHolder* left_holder = left->GetRegister();
      RegisterHolder* right_holder = GetFpRegister();
      move_imm_freg(right, right_holder->GetRegister());
      
      math_freg_freg(right_holder->GetRegister(), left_holder, instruction->GetType());
      ReleaseFpRegister(right_holder);
      working_stack.push_front(new RegInstr(left_holder));
    }
      break;

    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      RegisterHolder* temp = left->GetRegister();
      math_freg_freg(holder->GetRegister(), temp, instruction->GetType());
      working_stack.push_front(new RegInstr(temp));
      ReleaseFpRegister(holder);
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = left->GetRegister();
      math_mem_freg((long)right->GetOperand(), holder, instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;

    default:
      break;
    }
    break;

    // memory
  case MEM_FLOAT:
    switch(right->GetType()) {
    case IMM_FLOAT: {
      RegisterHolder* holder = GetFpRegister();
      move_mem_freg((long)left->GetOperand(), SP, holder->GetRegister());
      
      RegisterHolder* imm_holder = GetFpRegister();
      move_imm_freg(right, imm_holder->GetRegister());
      math_freg_freg(imm_holder->GetRegister(), holder, type);
      ReleaseFpRegister(imm_holder);
      working_stack.push_front(new RegInstr(holder));
    }
      break;
      
    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      RegisterHolder* right_holder = GetFpRegister();
      move_mem_freg((long)left->GetOperand(), SP, right_holder->GetRegister());
      math_freg_freg(holder->GetRegister(), right_holder, instruction->GetType());
      ReleaseFpRegister(holder);
      working_stack.push_front(new RegInstr(right_holder));
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* left_holder = GetFpRegister();
      move_mem_freg((long)left->GetOperand(), SP, left_holder->GetRegister());

      RegisterHolder* right_holder = GetFpRegister();
      move_mem_freg((long)right->GetOperand(), SP, right_holder->GetRegister());
      math_freg_freg(right_holder->GetRegister(), left_holder, instruction->GetType());
      ReleaseFpRegister(right_holder);
      working_stack.push_front(new RegInstr(left_holder));
    }
      break;

    default:
      break;
    }
    break;

  default:
    break;
  }

  delete left;
  left = nullptr;
    
  delete right;
  right = nullptr;
}

//
// -------- Start: Port to A64 encoding --------
//

void JitArm64::move_sp_reg(Register dest) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [sub " << GetRegisterName(dest)<< L", sp, #0]" << std::endl;
#endif
  
  uint32_t op_code = 0xD10003E0;
  op_code |= dest;
  AddMachineCode(op_code);
}

void JitArm64::move_reg_reg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG_JIT_JIT
    assert(src != SP);
    std::wcout << L"  " << (++instr_count) << L": [mov " << GetRegisterName(dest)
    << L", " << GetRegisterName(src) << L"]" << std::endl;
#endif
    
    uint32_t op_code = 0xAA0003E0;
    
    op_code |= dest;
    
    const uint32_t op_offset = src << 16;
    op_code |= op_offset;
    
    // encode
    AddMachineCode(op_code);
  }
}

void JitArm64::move_reg_mem(Register src, long offset, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [str " << GetRegisterName(src) << L", (" << GetRegisterName(dest) << L", #" << offset << L")]" << std::endl;
  assert(offset > -1);
#endif
  
  uint32_t op_code = 0xF9000000;
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  uint32_t op_src = src;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset / sizeof(size_t) << 10;
    
  // encode
  AddMachineCode(op_code);
}

void JitArm64::move_reg_mem32(Register src, long offset, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [str.w " << GetRegisterName(src) << L", (" << GetRegisterName(dest) << L", #" << offset << L")]" << std::endl;
  assert(offset > -1);
#endif
  
  uint32_t op_code = 0xB9000000;
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  uint32_t op_src = src;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset / sizeof(size_t) << 10;
    
  // encode
  AddMachineCode(op_code);
}

void JitArm64::move_reg_mem16(Register src, long offset, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [str.w " << GetRegisterName(src) << L", (" << GetRegisterName(dest) << L", #" << offset << L")]" << std::endl;
  assert(offset > -1);
#endif

  uint32_t op_code = 0x59000000;
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  uint32_t op_src = src;
  op_code |= op_src;

  uint32_t op_offset = abs(offset);
  op_code |= op_offset / sizeof(size_t) << 10;

  // encode
  AddMachineCode(op_code);
}


void JitArm64::move_mem_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [ldr " << GetRegisterName(dest) << L", (" << GetRegisterName(src) << L", #" << offset << L")]" << std::endl;
    assert(offset > -1);
#endif
  
  uint32_t op_code = 0xF9400000;
  uint32_t op_src = src << 5;
  op_code |= op_src;
  
  uint32_t op_dest = dest;
  op_code |= op_dest;
  
  uint32_t op_offset = abs(offset) / sizeof(size_t);
  op_code |= op_offset << 10;
  
  // encode
  AddMachineCode(op_code);
}

void JitArm64::move_mem32_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [ldr.w " << GetRegisterName(dest) << L", (" << GetRegisterName(src) << L", #" << offset << L")]" << std::endl;
    assert(offset > -1);
#endif
  
  uint32_t op_code = 0xB9400000;
  uint32_t op_src = src << 5;
  op_code |= op_src;
  
  uint32_t op_dest = dest;
  op_code |= op_dest;
  
  uint32_t op_offset = abs(offset) / sizeof(size_t);
  op_code |= op_offset << 10;
  
  // encode
  AddMachineCode(op_code);
}

// TODO: 64-bits all around?
#ifdef _WIN64
void JitArm64::move_imm_reg(int64_t imm, Register reg) {
#else
void JitArm64::move_imm_reg(long imm, Register reg) {
#endif
  // Optimization: Use MOV from zero register for zero (XZR/WZR)
  // This is encoded as ORR Xd, XZR, XZR which is often faster
  if(imm == 0) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [mov " << GetRegisterName(reg) << L", xzr]" << std::endl;
#endif
    // MOV Xd, XZR (encoded as ORR Xd, XZR, XZR)
    // Opcode: 0xAA1F03E0 | (dest)
    uint32_t op_code = 0xAA1F03E0;
    op_code |= reg;
    AddMachineCode(op_code);
  }
  else if(imm >= -4096 && imm < 0) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [mvn " << GetRegisterName(reg) << L", #" << imm << L"]" << std::endl;
#endif
    uint32_t op_code = 0x92800000;

    op_code |= (abs(imm) - 1) << 5;
    op_code |= reg;

    AddMachineCode(op_code);
  }
  else if(imm >= 0 && imm <= 4095) {
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [mov " << GetRegisterName(reg) << L", #" << imm << L"]" << std::endl;
#endif
    uint32_t op_code = 0xd2800000;

    op_code |= abs(imm) << 5;
    op_code |= reg;

    AddMachineCode(op_code);
  }
  else {
    // Optimization: Try to synthesize with MOVZ/MOVK for values with 1-2 non-zero 16-bit chunks
    // This is faster than constant pool access (avoids memory load)
    uint64_t val = (uint64_t)imm;
    int non_zero_chunks = 0;
    int chunk_positions[4];

    // Count non-zero 16-bit chunks
    for(int i = 0; i < 4; i++) {
      uint16_t chunk = (val >> (i * 16)) & 0xFFFF;
      if(chunk != 0) {
        chunk_positions[non_zero_chunks++] = i;
      }
    }

    // Use MOVZ/MOVK synthesis for 1-2 non-zero chunks (more efficient than constant pool)
    if(non_zero_chunks > 0 && non_zero_chunks <= 2) {
      // First instruction: MOVZ with first non-zero chunk
      int pos = chunk_positions[0];
      uint16_t chunk = (val >> (pos * 16)) & 0xFFFF;

      uint32_t op_code = 0xd2800000;  // MOVZ
      op_code |= ((uint32_t)pos << 21);  // shift amount
      op_code |= ((uint32_t)chunk << 5); // immediate value
      op_code |= reg;
      AddMachineCode(op_code);

      // Second instruction: MOVK for second chunk if present
      if(non_zero_chunks == 2) {
        pos = chunk_positions[1];
        chunk = (val >> (pos * 16)) & 0xFFFF;

        op_code = 0xf2800000;  // MOVK (keep other bits)
        op_code |= ((uint32_t)pos << 21);
        op_code |= ((uint32_t)chunk << 5);
        op_code |= reg;
        AddMachineCode(op_code);
      }
    }
    else {
      // Fall back to constant pool for complex values (3-4 non-zero chunks)
      move_mem_reg(INT_CONSTS, SP, X9);
      move_mem_reg(0, X9, reg);
      const_int_pool.insert(pair<size_t, size_t>(imm, code_index - 1));
    }
  }
}

void JitArm64::move_imm_reg32(int32_t imm, Register reg) {
  move_imm_reg(imm, reg);
}

void JitArm64::add_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [add " << GetRegisterName(dest)
  << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x8B000000;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::add_imm_reg(long imm, Register reg) {
  if(imm < 0) {
    sub_imm_reg(abs(imm), reg);
  }
  else if(imm >= 0 && imm <= 4095) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [add " << GetRegisterName(reg) << L", "
        << GetRegisterName(reg)  << L", #" << imm << L"]" << std::endl;
#endif
    uint32_t op_code = 0x91000000;
      
    uint32_t op_src = reg << 5;
    op_code |= op_src;
    
    uint32_t op_dest = reg;
    op_code |= op_dest;
    
    uint32_t op_imm = imm << 10;
    op_code |= op_imm;
    
    // encode
    AddMachineCode(op_code);
  }
  else {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    add_reg_reg(reg, imm_holder->GetRegister());
    ReleaseRegister(imm_holder);
  }
}

void JitArm64::add_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  add_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::inc_mem(long offset, Register dest) {
  add_imm_mem(1, offset, dest);
}

void JitArm64::add_imm_mem(long imm, long offset, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, dest, mem_holder->GetRegister());
  add_imm_reg(imm, mem_holder->GetRegister());
  move_reg_mem(mem_holder->GetRegister(), offset, dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::sub_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [subs " << GetRegisterName(dest) << L", "
  << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0xEB000000;
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::sub_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  sub_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::sub_imm_reg(long imm, Register reg) {
  if(imm < 0) {
    add_imm_reg(abs(imm), reg);
  }
  else if(imm >= 0 && imm <= 4095) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [sub " << GetRegisterName(reg) << L", " << GetRegisterName(reg)  << L", #" << imm << L"]" << std::endl;
#endif
  
    uint32_t op_code = 0xF1000000;
    
    uint32_t op_src = reg << 5;
    op_code |= op_src;
    
    uint32_t op_dest = reg;
    op_code |= op_dest;
    
    op_code |= imm << 10;
    
    // encode
    AddMachineCode(op_code);
  }
  else {
    RegisterHolder* imm_holder = GetRegister();
    move_imm_reg(imm, imm_holder->GetRegister());
    sub_reg_reg(reg, imm_holder->GetRegister());
    ReleaseRegister(imm_holder);
  }
}

void JitArm64::sub_imm_mem(long imm, long offset, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, dest, mem_holder->GetRegister());
  sub_imm_reg(imm, mem_holder->GetRegister());
  move_reg_mem(mem_holder->GetRegister(), offset, dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::dec_mem(long offset, Register dest) {
  sub_imm_mem(1, offset, dest);
}

void JitArm64::dec_reg(Register dest) {
  sub_imm_reg(1, dest);
}

void JitArm64::mul_imm_reg(long imm, Register reg) {
  // Optimization: Replace multiply by power-of-2 with left shift
  // Example: x * 8 becomes x << 3 (faster, no register allocation needed)
  if(imm > 0 && (imm & (imm - 1)) == 0) {
    // imm is a power of 2, calculate shift amount
    int shift = 0;
    long temp = imm;
    while(temp > 1) {
      temp >>= 1;
      shift++;
    }
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [lsl " << GetRegisterName(reg)
          << L", " << GetRegisterName(reg) << L", #" << shift
          << L"] (optimized from mul #" << imm << L")" << std::endl;
#endif
    shl_imm_reg(shift, reg);
  }
  else {
    // Use regular multiply for non-power-of-2 values
    RegisterHolder* src_holder = GetRegister();
    move_imm_reg(imm, src_holder->GetRegister());
    mul_reg_reg(src_holder->GetRegister(), reg);
    ReleaseRegister(src_holder);
  }
}

void JitArm64::mul_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  mul_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder);
}

void JitArm64::mul_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [mul " << GetRegisterName(dest) << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x9B007C00;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::div_imm_reg(long imm, Register reg, bool is_mod) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  div_reg_reg(src_holder->GetRegister(), reg, is_mod);
  ReleaseRegister(src_holder);
}

void JitArm64::div_mem_reg(long offset, Register src, Register dest, bool is_mod) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  div_reg_reg(src_holder->GetRegister(), dest, is_mod);
  ReleaseRegister(src_holder);
}

void JitArm64::div_reg_reg(Register src, Register dest, bool is_mod) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [sdiv " << GetRegisterName(dest) << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  CheckIntDivideByZero(src);
  
  uint32_t op_code = 0x9AC00C00;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  if(is_mod) {
    RegisterHolder* holder = GetRegister();
    
    op_code |= holder->GetRegister();
    AddMachineCode(op_code);
    mul_reg_reg(src, holder->GetRegister());
    sub_reg_reg(holder->GetRegister(), dest);
    
    ReleaseRegister(holder);
  }
  else {    
    op_dest = dest;
    op_code |= op_dest;
    AddMachineCode(op_code);
  }
}

void JitArm64::shl_imm_reg(long value, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [lsl " << GetRegisterName(dest) << L", " << GetRegisterName(dest) << L", #" << value << L"]" << std::endl;
#endif
                                 
  uint32_t op_code = 0xD3400000;
  
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  uint32_t op_src = dest;
  op_code |= op_src;

  // set bit field
  bitset<6> imm_bits;
  imm_bits = abs(value);
  imm_bits.flip();
  
  const uint8_t imms = (uint8_t)imm_bits.to_ulong();
  op_code |= imms << 10;
  
  const uint8_t immr = imms + 1;
  op_code |= immr << 16;
  
  // encode
  AddMachineCode(op_code);
}

void JitArm64::shl_mem_reg(long offset, Register src, Register dest)
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shl_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::shl_reg_reg(Register src, Register dest)
{
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [lsl " << GetRegisterName(dest) << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x9AC02000;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::shr_imm_reg(long value, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [asr $" << value << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x9340FC00;
  
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  uint32_t op_src = dest;
  op_code |= op_src;
  
  op_code |= abs(value) << 16;
  
  AddMachineCode(op_code);
}

void JitArm64::shr_mem_reg(long offset, Register src, Register dest)
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shr_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::shr_reg_reg(Register src, Register dest)
{
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [asr " << GetRegisterName(dest) << L", " << GetRegisterName(src) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x9AC02800;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

// --- function calls ---
void JitArm64::call_reg(Register reg) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [blr %" << GetRegisterName(reg) << L"]" << std::endl;
#endif
  
  move_reg_mem(LR, TMP_LR, SP);

  uint32_t op_code = 0xD63F0000;
  op_code |= reg << 5;
  AddMachineCode(op_code);
  
  move_mem_reg(TMP_LR, SP, LR);
}

void JitArm64::and_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [and " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x8A000000;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::and_imm_reg(long imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  and_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitArm64::and_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  and_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::not_reg(Register reg) {
#ifdef _DEBUG_JIT
  std::std::wcout << L"  " << (++instr_count) << L": [not " << GetRegisterName(reg) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x2A2003E0;
  
  // rn <- src
  uint32_t op_src = reg << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = reg << 5;
  op_code |= op_dest;

  op_dest = reg;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::or_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [orr " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0xAA000000;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::or_imm_reg(long imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  or_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitArm64::or_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  or_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder);
}

void JitArm64::xor_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [eor " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0xCA000000;
  uint32_t op_src = src << 16;
  op_code |= op_src;

  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  // rm <- dest
  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::xor_imm_reg(long imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  xor_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitArm64::xor_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  xor_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder);
}

void JitArm64::cmp_imm_reg(long imm, Register reg) {
  if(imm >= 0 && imm <= 4096) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [cmp/cmn " << GetRegisterName(reg) << L", " << imm << L"]" << std::endl;
#endif
    
    uint32_t op_code = 0xF100001F;
    
    uint32_t op_dest = reg << 5;
    op_code |= op_dest;
  
    op_code |= abs(imm) << 10;
    
    AddMachineCode(op_code);
  }
  else {
    RegisterHolder* src_holder = GetRegister();
    move_imm_reg(imm, src_holder->GetRegister());
    cmp_reg_reg(src_holder->GetRegister(), reg);
    ReleaseRegister(src_holder);
  }
}

void JitArm64::cmp_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  cmp_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder);
}

void JitArm64::cmp_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [cmp " << GetRegisterName(dest)
       << L", " << GetRegisterName(src) << L"]" << std::endl;
#endif
  uint32_t op_code = 0xEB00001F;
  
  op_code |= dest << 5;
  op_code |= src << 16;
  
  AddMachineCode(op_code);
}

void JitArm64::move_mem_freg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [f.ldr " << offset << L"(%"
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0xFD400000;
  uint32_t op_src = src << 5;
  op_code |= op_src;
  
  uint32_t op_dest = dest;
  op_code |= op_dest;
  
  uint32_t op_offset = abs(offset) / sizeof(size_t);
  op_code |= op_offset << 10;
  
  // encode
  AddMachineCode(op_code);
}

void JitArm64::move_freg_mem(Register src, long offset, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [f.str %" << GetRegisterName(src)
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]"
        << endl;
#endif
  
  uint32_t op_code = 0xFD000000;
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  uint32_t op_src = src;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset / sizeof(size_t) << 10;
    
  // encode
  AddMachineCode(op_code);
}

void JitArm64::move_imm_memf(RegInstr* instr, long offset, Register dest) {
  RegisterHolder* tmp_holder = GetFpRegister();
  move_imm_freg(instr, tmp_holder->GetRegister());
  move_freg_mem(tmp_holder->GetRegister(), offset, dest);
  ReleaseFpRegister(tmp_holder);
}

void JitArm64::move_imm_freg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  move_mem_freg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::add_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fadd " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E602800;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::sub_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fsub " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E603800;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::mul_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fmul " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E600800;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::div_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fdiv " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif

  CheckFloatDivideByZero(src);
  
  uint32_t op_code = 0x1e601800;
  
  // rn <- src
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::round_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [frinta " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E664000;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::floor_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [frintm " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E654000;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::ceil_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [frintp " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E64C000;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::sqrt_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fsqrt " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E61C000;
  
  // rm=rd <- dest
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitArm64::vcvt_imm_reg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  vcvt_mem_reg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::vcvt_imm_freg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  vcvt_reg_freg(imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::vcvt_reg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [scvtf %" << GetRegisterName(src)
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x9E620000;
  
  op_code |= dest;
  op_code |= src << 5;
  
  AddMachineCode(op_code);
}

void JitArm64::vcvt_mem_freg(long offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  vcvt_reg_freg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitArm64::vcvt_freg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fcvtzs %" << GetRegisterName(src)
        << L", %" << GetRegisterName(dest) << L"]" << std::endl;
#endif
  uint32_t op_code = 0x9E780000;
  
  op_code |= dest;
  op_code |= src << 5;
  
  AddMachineCode(op_code);
}

void JitArm64::vcvt_mem_reg(long offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetFpRegister();
  move_mem_freg(offset, src, mem_holder->GetRegister());
  vcvt_freg_reg(mem_holder->GetRegister(), dest);
  ReleaseFpRegister(mem_holder);
}

void JitArm64::move_imm_mem8(int8_t imm, long offset, Register dest) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg32(imm, imm_holder->GetRegister());
  move_reg_mem8(imm_holder->GetRegister(), offset, dest);
  ReleaseRegister(imm_holder);
}

void JitArm64::move_imm_mem(long imm, long offset, Register dest) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  move_reg_mem(imm_holder->GetRegister(), offset, dest);
  ReleaseRegister(imm_holder);
}

void JitArm64::move_imm_mem32(int32_t imm, long offset, Register dest) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg32(imm, imm_holder->GetRegister());
  move_reg_mem(imm_holder->GetRegister(), offset, dest);
  ReleaseRegister(imm_holder);
}

void JitArm64::add_imm_freg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  add_mem_freg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::sub_imm_freg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  sub_mem_freg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::div_imm_freg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  div_mem_freg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::mul_imm_freg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  mul_mem_freg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::math_imm_freg(RegInstr *instr, RegisterHolder *&reg, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_imm_freg(instr, reg->GetRegister());
    break;

  case SUB_FLOAT:
    sub_imm_freg(instr, reg->GetRegister());
    break;

  case MUL_FLOAT:
    mul_imm_freg(instr, reg->GetRegister());
    break;

  case DIV_FLOAT:
    div_imm_freg(instr, reg->GetRegister());
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_imm_freg(instr->GetOperand(), reg->GetRegister());
    if(!cond_jmp(type)) {
      ReleaseFpRegister(reg);
      reg = GetRegister();
      cmov_reg(reg->GetRegister(), type);
    }
    break;
    
  default:
    break;
  }
}

void JitArm64::math_mem_freg(long offset, RegisterHolder* &dest, InstructionType type) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(offset, SP, holder->GetRegister());
  math_freg_freg(holder->GetRegister(), dest, type);
  ReleaseFpRegister(holder);
}

void JitArm64::math_freg_freg(Register src, RegisterHolder *&dest, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_freg_freg(src, dest->GetRegister());
    break;

  case SUB_FLOAT:
    sub_freg_freg(src, dest->GetRegister());
    break;

  case MUL_FLOAT:
    mul_freg_freg(src, dest->GetRegister());
    break;

  case DIV_FLOAT:
    div_freg_freg(src, dest->GetRegister());
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_freg_freg(src, dest->GetRegister());
    if(!cond_jmp(type)) {
      ReleaseFpRegister(dest);
      dest = GetRegister();
      cmov_reg(dest->GetRegister(), type);
    }
    break;
    
  default:
    break;
  }
}

void JitArm64::cmp_mem_freg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(offset, src, holder->GetRegister());
  cmp_freg_freg(dest, holder->GetRegister());
  move_freg_freg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitArm64::add_mem_freg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(offset, src, holder->GetRegister());
  add_freg_freg(dest, holder->GetRegister());
  move_freg_freg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitArm64::mul_mem_freg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(offset, src, holder->GetRegister());
  mul_freg_freg(dest, holder->GetRegister());
  move_freg_freg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitArm64::sub_mem_freg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(offset, src, holder->GetRegister());
  sub_freg_freg(dest, holder->GetRegister());
  move_freg_freg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitArm64::div_mem_freg(long offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_freg(offset, src, holder->GetRegister());
  div_freg_freg(dest, holder->GetRegister());
  move_freg_freg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitArm64::cmp_imm_freg(size_t addr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(addr, imm_holder->GetRegister());
  cmp_mem_freg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitArm64::math_imm_reg(long imm, Register reg, InstructionType type) {
  switch(type) {
  case AND_INT:
    and_imm_reg(imm, reg);
    break;

  case OR_INT:
    or_imm_reg(imm, reg);
    break;
    
  case ADD_INT:
    add_imm_reg(imm, reg);
    break;

  case SUB_INT:
    sub_imm_reg(imm, reg);
    break;

  case MUL_INT:
    mul_imm_reg(imm, reg);
    break;

  case DIV_INT:
    div_imm_reg(imm, reg);
    break;

  case MOD_INT:
    div_imm_reg(imm, reg, true);
    break;

  case SHL_INT:
    shl_imm_reg(imm, reg);
    break;

  case SHR_INT:
    shr_imm_reg(imm, reg);
    break;
    
  case BIT_AND_INT:
    and_imm_reg(imm, reg);
    break;
    
  case BIT_OR_INT:
    or_imm_reg(imm, reg);
    break;
    
  case BIT_XOR_INT:
    xor_imm_reg(imm, reg);
    break;
    
  case LES_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
  case LES_EQL_INT:
  case GTR_EQL_INT:
    cmp_imm_reg(imm, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;

  default:
    break;
  }
}

void JitArm64::math_reg_reg(Register src, Register dest, InstructionType type) {
  switch(type) {
  case SHL_INT:
    shl_reg_reg(src, dest);
    break;

  case SHR_INT:
    shr_reg_reg(src, dest);
    break;

  case AND_INT:
    and_reg_reg(src, dest);
    break;

  case OR_INT:
    or_reg_reg(src, dest);
    break;
    
  case ADD_INT:
    add_reg_reg(src, dest);
    break;

  case SUB_INT:
    sub_reg_reg(src, dest);
    break;

  case MUL_INT:
    mul_reg_reg(src, dest);
    break;

  case DIV_INT:
    div_reg_reg(src, dest);
    break;

  case MOD_INT:
    div_reg_reg(src, dest, true);
    break;

  case BIT_AND_INT:
    and_reg_reg(src, dest);
    break;

  case BIT_OR_INT:
    or_reg_reg(src, dest);
    break;

  case BIT_XOR_INT:
    xor_reg_reg(src, dest);
    break;
    
  case LES_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
  case LES_EQL_INT:
  case GTR_EQL_INT:
    cmp_reg_reg(src, dest);
    if(!cond_jmp(type)) {
      cmov_reg(dest, type);
    }
    break;

  default:
    break;
  }
}

void JitArm64::math_mem_reg(long offset, Register reg, InstructionType type) {
  switch(type) {
  case SHL_INT:
    shl_mem_reg(offset, SP, reg);
    break;

  case SHR_INT:
    shr_mem_reg(offset, SP, reg);
    break;
    
  case AND_INT:
    and_mem_reg(offset, SP, reg);
    break;

  case OR_INT:
    or_mem_reg(offset, SP, reg);
    break;
    
  case ADD_INT:
    add_mem_reg(offset, SP, reg);
    break;

  case SUB_INT:
    sub_mem_reg(offset, SP, reg);
    break;

  case MUL_INT:
    mul_mem_reg(offset, SP, reg);
    break;

  case DIV_INT:
    div_mem_reg(offset, SP, reg, false);
    break;
    
  case MOD_INT:
    div_mem_reg(offset, SP, reg, true);
    break;

  case BIT_AND_INT:
    and_mem_reg(offset, SP, reg);
    break;

  case BIT_OR_INT:
    or_mem_reg(offset, SP, reg);
    break;

  case BIT_XOR_INT:
    xor_mem_reg(offset, SP, reg);
    break;
    
  case LES_INT:
  case LES_EQL_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
  case GTR_EQL_INT:
    cmp_mem_reg(offset, SP, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;

  default:
    break;
  }
}

void JitArm64::move_freg_freg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fmov " << GetRegisterName(X10)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(src) << L"]" << std::endl;
#endif
    uint32_t op_code = 0x9E67000A;
    op_code |= src << 5;
    AddMachineCode(op_code);
    
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fmov " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(X19) << L"]" << std::endl;
#endif
    op_code = 0x9E660140;
    op_code |= dest;
    AddMachineCode(op_code);
  }
}

void JitArm64::cmp_freg_freg(Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [fcmp " << GetRegisterName(dest)
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << std::endl;
#endif
  
  uint32_t op_code = 0x1E602000;
  
  op_code |= src << 16;
  op_code |= dest << 5;
  
  // encode
  AddMachineCode(op_code);
}

// --- 8-bit operations ---
void JitArm64::move_reg_mem8(Register src, long offset, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [strb " << GetRegisterName(src)
        << L", (" << GetRegisterName(dest) << L", #" << offset << L")]" << std::endl;
  assert(offset > -1);
#endif
  
  uint32_t op_code = 0x39000000;
  uint32_t op_dest = dest << 5;
  op_code |= op_dest;
  
  uint32_t op_src = src;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset / sizeof(size_t) << 10;
    
  // encode
  AddMachineCode(op_code);
}

void JitArm64::move_mem16_reg(long offset, Register src, Register dest) {
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"  " << (++instr_count) << L": [ldrb " << GetRegisterName(dest)
    << L", (" << GetRegisterName(src) << L", #" << offset << L")]" << std::endl;
  assert(offset > -1);
#endif

  uint32_t op_code = 0x39400000;
  uint32_t op_src = src << 5;
  op_code |= op_src;

  uint32_t op_dest = dest;
  op_code |= op_dest;

  uint32_t op_offset = abs(offset) / sizeof(size_t);
  op_code |= op_offset << 10;

  // encode
  AddMachineCode(op_code);
}


void JitArm64::move_mem8_reg(long offset, Register src, Register dest) {
  #ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [ldrb " << GetRegisterName(dest)
          << L", (" << GetRegisterName(src) << L", #" << offset << L")]" << std::endl;
    assert(offset > -1);
  #endif

  uint32_t op_code = 0x39400000;
  uint32_t op_src = src << 5;
  op_code |= op_src;

  uint32_t op_dest = dest;
  op_code |= op_dest;

  uint32_t op_offset = abs(offset) / sizeof(size_t);
  op_code |= op_offset << 10;

  // encode
  AddMachineCode(op_code);
}

//
// -------- End: Port to A64 encoding --------
//

bool JitArm64::cond_jmp(InstructionType type) {
  if(instr_index >= method->GetInstructionCount()) {
    return false;
  }
  
  StackInstr* next_instr = method->GetInstruction(instr_index);
  if(next_instr->GetType() == JMP && next_instr->GetOperand2() > -1) {
    //
    // jump if true
    //
    if(next_instr->GetOperand2() == 1) {
      switch(type) {
      case LES_INT:
      case LES_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.lt]" << std::endl;
#endif
        AddMachineCode(0x5400000B);
        break;

      case GTR_INT:
      case GTR_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.gt]" << std::endl;
#endif
        AddMachineCode(0x5400000C);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.eq]" << std::endl;
#endif
        AddMachineCode(0x54000000);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [bne]" << std::endl;
#endif
        AddMachineCode(0x54000001);
        break;

      case LES_EQL_INT:
      case LES_EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.le]" << std::endl;
#endif
        AddMachineCode(0x5400000D);
        break;
        
      case GTR_EQL_INT:
      case GTR_EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.ge]" << std::endl;
#endif
        AddMachineCode(0x5400000A);
        break;
        
      default:
        break;
      }
    }
    //
    // jump - false
    //
    else {
      switch(type) {
      case LES_INT:
      case LES_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.ge]" << std::endl;
#endif
        AddMachineCode(0x5400000A);
        break;

      case GTR_INT:
      case GTR_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.le]" << std::endl;
#endif
        AddMachineCode(0x5400000D);
        break;
          
      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.ne]" << std::endl;
#endif
        AddMachineCode(0x54000001);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.eq]" << std::endl;
#endif
        AddMachineCode(0x54000000);
        break;

      case LES_EQL_INT:
      case LES_EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.gt]" << std::endl;
#endif
        AddMachineCode(0x5400000C);
        break;
        
      case GTR_EQL_INT:
      case GTR_EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
        std::std::wcout << L"  " << (++instr_count) << L": [b.lt]" << std::endl;
#endif
        AddMachineCode(0x5400000B);
        break;
        
      default:
        break;
      }
    }
    // store update index
    jump_table.insert(pair<long, StackInstr*>(code_index, next_instr));
    
    // temp offset
    skip_jump = true;
    return true;
  }
  
  return false;
}

void JitArm64::cmov_reg(Register reg, InstructionType oper)
{
  uint32_t op_code;
  
  switch (oper) {
  case LES_INT:
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [cset  w9, lt]" << std::endl;
#endif
    AddMachineCode(0x1A9FA7E9);
    break;
      
  case LES_FLOAT:
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [cset  w9, mi]" << std::endl;
#endif
    AddMachineCode(0x1A9F57E9);
    break;
    
  case GTR_INT:
  case GTR_FLOAT:
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"  " << (++instr_count) << L": [cset  w9, gt]" << std::endl;
#endif
    AddMachineCode(0x1A9FD7E9);
    break;
    
  case EQL_INT:
  case EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
    std::std::wcout << L"  " << (++instr_count) << L": [cset  w9, eq]" << std::endl;
#endif
    AddMachineCode(0x1A9F17E9);
    break;
      
  case NEQL_INT:
  case NEQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
    std::std::wcout << L"  " << (++instr_count) << L": [cset  w9, ne]" << std::endl;
#endif
    AddMachineCode(0x1A9F07E9);
    break;
    
  case LES_EQL_INT:
#ifdef _DEBUG_JIT_JIT
      std::std::wcout << L"  " << (++instr_count) << L": [cset  w9, le]" << std::endl;
#endif
    AddMachineCode(0x1A9FC7E9);
    break;
    
  case LES_EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
    std::std::wcout << L"  " << (++instr_count) << L": [cset  w9, ls]" << std::endl;
#endif
    AddMachineCode(0X1A9F87E9);
    break;
    
  case GTR_EQL_INT:
  case GTR_EQL_FLOAT:
#ifdef _DEBUG_JIT_JIT
    std::std::wcout << L"  " << (++instr_count) << L": [cset  w9, ge]" << std::endl;
#endif
    AddMachineCode(0x1A9FB7E9);
    break;

  default:
    break;
  }
  
#ifdef _DEBUG_JIT_JIT
    std::std::wcout << L"  " << (++instr_count) << L": [and x8, x0, #0x1]" << std::endl;
#endif
  op_code = 0x92400120;
  op_code |= reg;
  AddMachineCode(op_code);
}

void JitArm64::ProcessFloatOperation(StackInstr* instruction)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
#ifdef _DEBUG_JIT_JIT
  assert(left->GetType() == MEM_FLOAT);
#endif
  
  // save D0, if needed
  RegisterHolder *holder = GetFpRegister();
  if(holder->GetRegister() != D0) {
    move_freg_mem(D0, TMP_D0, SP);
  }
  
  // load D0
  move_mem_freg((long)left->GetOperand(), SP, D0);
  
  // choose function
  double(*func_ptr)(double);
  switch (type) {
  case SIN_FLOAT:
    func_ptr = sin;
    break;
    
  case COS_FLOAT:
    func_ptr = cos;
    break;

  case TAN_FLOAT:
    func_ptr = tan;
    break;

  case ASIN_FLOAT:
    func_ptr = asin;
    break;

  case ATAN_FLOAT:
    func_ptr = atan;
    break;

  case ACOS_FLOAT:
    func_ptr = acos;
    break;

  case ACOSH_FLOAT:
    func_ptr = acosh;
    break;

  case ASINH_FLOAT:
    func_ptr = asinh;
    break;

  case ATANH_FLOAT:
    func_ptr = atanh;
    break;

  case COSH_FLOAT:
    func_ptr = cosh;
    break;

  case SINH_FLOAT:
    func_ptr = sinh;
    break;

  case TANH_FLOAT:
    func_ptr = tanh;
    break;

  case EXP_FLOAT:
    func_ptr = exp;
    break;

  case LOG10_FLOAT:
    func_ptr = log10;
    break;
    
  case TRUNC_FLOAT:
    func_ptr = trunc;
    break;

  case LOG_FLOAT:
    func_ptr = log;
    break;

  case GAMMA_FLOAT:
    func_ptr = tgamma;
    break;

  case LOG2_FLOAT:
    func_ptr = log2;
    break;

  case CBRT_FLOAT:
    func_ptr = cbrt;
    break;
     
  default:
    throw runtime_error("Invalid function call!");
    break;
  }
  
  // call function
  move_reg_mem(X9, TMP_X0, SP);
  move_imm_reg((size_t)func_ptr, X9);
  call_reg(X9);
  move_mem_reg(TMP_X0, SP, X9);
  
  // get return and restore D0, if needed
  move_freg_freg(D0, holder->GetRegister());
  if(holder->GetRegister() != D0) {
    move_mem_freg(TMP_D0, SP, D0);
  }
  working_stack.push_front(new RegInstr(holder));
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessFloatRound(StackInstr* instruction, const wchar_t mode)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
#ifdef _DEBUG_JIT_JIT
  assert(left->GetType() == MEM_FLOAT);
#endif
  
  // save D0, if needed
  RegisterHolder *holder = GetFpRegister();
  if(holder->GetRegister() != D0) {
    move_freg_mem(D0, TMP_D0, SP);
  }
  
  // load D0
  move_mem_freg((long)left->GetOperand(), SP, D0);
  
  if(mode == L'f') {
    floor_freg_freg(D0 , D0);
  }
  else if(mode == L'c') {
    ceil_freg_freg(D0 , D0);
  }
  else {
    round_freg_freg(D0 , D0);
  }
  
  // get return and restore D0, if needed
  move_freg_freg(D0, holder->GetRegister());
  if(holder->GetRegister() != D0) {
    move_mem_freg(TMP_D0, SP, D0);
  }
  working_stack.push_front(new RegInstr(holder));
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessFloatSquareRoot(StackInstr* instruction)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
#ifdef _DEBUG_JIT_JIT
  assert(left->GetType() == MEM_FLOAT);
#endif
  
  // save D0, if needed
  RegisterHolder *holder = GetFpRegister();
  if(holder->GetRegister() != D0) {
    move_freg_mem(D0, TMP_D0, SP);
  }
  
  // load D0
  move_mem_freg((long)left->GetOperand(), SP, D0);
  sqrt_freg_freg(D0 , D0);
  
  // get return and restore D0, if needed
  move_freg_freg(D0, holder->GetRegister());
  if(holder->GetRegister() != D0) {
    move_mem_freg(TMP_D0, SP, D0);
  }
  working_stack.push_front(new RegInstr(holder));
  
  delete left;
  left = nullptr;
}

void JitArm64::ProcessFloatOperation2(StackInstr* instruction)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
#ifdef _DEBUG_JIT_JIT
  assert(left->GetType() == MEM_FLOAT);
#endif
  
  // save D0, if needed
  RegisterHolder *holder = GetFpRegister();
  if(holder->GetRegister() != D0) {
    move_freg_mem(D0, TMP_D0, SP);
  }
  move_freg_mem(D1, TMP_D1, SP);
   
  // load D0
  move_mem_freg((long)right->GetOperand(), SP, D0);
  move_mem_freg((long)left->GetOperand(), SP, D1);
  
  // choose function
  double(*func_ptr)(double, double);
  switch (type) {
    case ATAN2_FLOAT:
      func_ptr = atan2;
      break;
    
    case POW_FLOAT:
      func_ptr = pow;
      break;

    case MOD_FLOAT:
      func_ptr = fmod;
      break;
      
    default:
      throw runtime_error("Invalid function call!");
      break;
  }
  
  // call function
  move_reg_mem(X9, TMP_X0, SP);
  move_imm_reg((size_t)func_ptr, X9);
  call_reg(X9);
  move_mem_reg(TMP_X0, SP, X9);
  
  // get return and restore D0, if needed
  move_freg_freg(D0, holder->GetRegister());
  move_mem_freg(TMP_D1, SP, D1);
  if(holder->GetRegister() != D0) {
    move_mem_freg(TMP_D0, SP, D0);
  }
  working_stack.push_front(new RegInstr(holder));
  
  delete left;
  left = nullptr;
  
  delete right;
  right = nullptr;
}

// --- push/pop cpu stack ---
void JitArm64::push_mem(long offset, Register dest) {
  throw runtime_error("Method 'push_mem(..)' not implemented for ARM64 target");
}

void JitArm64::push_reg(Register reg) {
  throw runtime_error("Method 'push_reg(..)' not implemented for ARM64 target");
}

void JitArm64::push_imm(int32_t value) {
  throw runtime_error("Method 'push_imm(..)' not implemented for ARM64 target");
}

void JitArm64::pop_reg(Register reg) {
  throw runtime_error("Method 'pop_reg(..)' not implemented for ARM64 target");
}

//
// Get register name
//
std::wstring JitArm64::GetRegisterName(Register reg)
{
  switch(reg) {
  case X0:
    return L"X0/D0";

  case X1:
    return L"X1/D1";

  case X2:
    return L"X2/D2";

  case X3:
    return L"X3/D3";
    
  case X4:
    return L"X4/D4";
  
  case X5:
    return L"X5/D5";
    
  case X6:
    return L"X6/D6";
    
  case X7:
    return L"X7/D7";
    
  case XS0:
    return L"XS0/D8";
      
  case X9:
    return L"X9/D9";
        
  case X10:
    return L"X10/D10";
      
  case X11:
    return L"X11/D11";
    
  case X12:
    return L"X12/D12";
      
  case X13:
    return L"X13/D13";
      
  case X14:
    return L"X14/D14";

  case X15:
    return L"X14/D15";
    
  case XS1:
    return L"XS1/D16";
      
  case XS2:
    return L"XS2/D17";
    
  case XS3:
    return L"XS3/D18";
      
  case X19:
    return L"X19/D19";

  case X20:
    return L"X20/D20";

  case X21:
    return L"X21/D21";

  case X22:
    return L"X22/D22";

  case X23:
    return L"X23/D23";

  case X24:
    return L"X24/D24";

  case X25:
    return L"X25/D25";

  case X26:
    return L"X26/D26";

  case X27:
    return L"X27/D27";

  case X28:
    return L"X28/D28";

  case FP:
    return L"FP/D29";

  case LR:
    return L"LR/D30";

  case SP:
    return L"SP/D31";
  }
  
  return L"unknown";
}

//
// calculate array index
//
RegisterHolder* JitArm64::ArrayIndex(StackInstr* instr, MemoryType type)
{
  RegInstr* holder = working_stack.front();
  working_stack.pop_front();

  RegisterHolder* array_holder;
  switch(holder->GetType()) {
  case IMM_INT:
    wcerr << L">>> trying to index a constant! <<<" << std::endl;
    exit(1);
    break;

  case REG_INT:
    array_holder = holder->GetRegister();
    break;

  case MEM_INT:
    array_holder = GetRegister();
    move_mem_reg((long)holder->GetOperand(), SP, array_holder->GetRegister());
    break;

  default:
    wcerr << L">>> Internal error! <<<" << std::endl;
    exit(1);
    break;
  }
  CheckNilDereference(array_holder->GetRegister());
  
  /* Algorithm:
     int32_t index = PopInt();
     const int32_t dim = instr->GetOperand();

     for(int i = 1; i < dim; ++i) {
       index *= array[i];
       index += PopInt();
     }
  */

  delete holder;
  holder = nullptr;

  // get initial index
  RegisterHolder* index_holder;
  holder = working_stack.front();
  working_stack.pop_front();
  switch(holder->GetType()) {
  case IMM_INT:
    index_holder = GetRegister();
    move_imm_reg(holder->GetOperand(), index_holder->GetRegister());
    break;

  case REG_INT:
    index_holder = holder->GetRegister();
    break;

  case MEM_INT:
    index_holder = GetRegister();
    move_mem_reg((long)holder->GetOperand(), SP, index_holder->GetRegister());
    break;

  default:
    wcerr << L">>> Internal error! <<<" << std::endl;
    exit(1);
    break;
  }

  const long dim = instr->GetOperand();
  // Optimization: Skip loop for 1D arrays (most common case)
  // Multi-dimensional array indexing requires multiply-accumulate loop
  if(dim > 1) {
    for(int i = 1; i < dim; ++i) {
      // index *= array[i];
      mul_mem_reg((i + 2) * sizeof(size_t), array_holder->GetRegister(), index_holder->GetRegister());
      delete holder;
      holder = nullptr;

      holder = working_stack.front();
      working_stack.pop_front();
      switch(holder->GetType()) {
      case IMM_INT:
        add_imm_reg((long)holder->GetOperand(), index_holder->GetRegister());
        break;

      case REG_INT:
        add_reg_reg(holder->GetRegister()->GetRegister(),
                    index_holder->GetRegister());
        break;

      case MEM_INT:
        add_mem_reg((long)holder->GetOperand(), SP, index_holder->GetRegister());
        break;

      default:
        wcerr << L">>> Internal error! <<<" << std::endl;
        exit(1);
        break;
      }
    }
  }

  // bounds check
  RegisterHolder* bounds_holder = GetRegister();
  move_mem_reg(0, array_holder->GetRegister(), bounds_holder->GetRegister());

  // adjust indices
  switch(type) {
  case BYTE_ARY_TYPE:
    break;

  case CHAR_ARY_TYPE:
#ifdef _WIN64
    shl_imm_reg(1, index_holder->GetRegister());
    shl_imm_reg(1, bounds_holder->GetRegister());
#else
    shl_imm_reg(2, index_holder->GetRegister());
    shl_imm_reg(2, bounds_holder->GetRegister());
#endif
    break;
    
  case INT_TYPE:
  case FLOAT_TYPE:
    shl_imm_reg(3, index_holder->GetRegister());
    shl_imm_reg(3, bounds_holder->GetRegister());
    break;

  default:
    break;
  }
  CheckArrayBounds(index_holder->GetRegister(), bounds_holder->GetRegister());
  ReleaseRegister(bounds_holder);

  // skip first 2 integers (size and dimension) and all dimension indices
  add_imm_reg((instr->GetOperand() + 2) * sizeof(size_t), index_holder->GetRegister());
  add_reg_reg(index_holder->GetRegister(), array_holder->GetRegister());
  ReleaseRegister(index_holder);

  delete holder;
  holder = nullptr;

  return array_holder;
}

void JitArm64::ProcessIndices()
{
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"Calculating indices for variables..." << std::endl;
#endif
  multimap<long, StackInstr*> values;
  for(long i = 0; i < method->GetInstructionCount(); ++i) {
    StackInstr* instr = method->GetInstruction(i);
    switch(instr->GetType()) {
    case LOAD_LOCL_INT_VAR:
    case LOAD_CLS_INST_INT_VAR:
    case STOR_LOCL_INT_VAR:
    case STOR_CLS_INST_INT_VAR:
    case LOAD_FUNC_VAR:
    case STOR_FUNC_VAR:
    case COPY_LOCL_INT_VAR:
    case COPY_CLS_INST_INT_VAR:
    case LOAD_FLOAT_VAR:
    case STOR_FLOAT_VAR:
    case COPY_FLOAT_VAR:
      values.insert(pair<long, StackInstr*>(instr->GetOperand(), instr));
      break;

    default:
      break;
    }
  }
  
  long index = RED_ZONE;
  long last_id = -1;
  multimap<long, StackInstr*>::iterator value;
  for(value = values.begin(); value != values.end(); ++value) {
    long id = value->first;
    StackInstr* instr = (*value).second;
    // instance reference
    if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
      instr->SetOperand3(instr->GetOperand() * sizeof(size_t));
    }
    // local reference
    else {
      if(last_id != id) {
        if(instr->GetType() == LOAD_LOCL_INT_VAR ||
           instr->GetType() == LOAD_CLS_INST_INT_VAR ||
           instr->GetType() == STOR_LOCL_INT_VAR ||
           instr->GetType() == STOR_CLS_INST_INT_VAR ||
           instr->GetType() == COPY_LOCL_INT_VAR ||
           instr->GetType() == COPY_CLS_INST_INT_VAR) {
          index += sizeof(size_t);
        }
        else if(instr->GetType() == LOAD_FUNC_VAR ||
                instr->GetType() == STOR_FUNC_VAR) {
          index += sizeof(size_t) * 2;
        }
        else {
          index += sizeof(double);
        }
      }
      instr->SetOperand3(index);
      last_id = id;
    }
#ifdef _DEBUG_JIT_JIT
    if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
      std::wcout << L"native memory: index=" << instr->GetOperand() << L"; jit index="
        << instr->GetOperand3() << endl;
    }
    else {
      std::wcout << L"native stack: index=" << instr->GetOperand() << L"; jit index="
        << instr->GetOperand3() << endl;
    }
#endif
  }
  
  // calculate local space (adjust for alignment)
  local_space += index;
  realign_stack = false;
  if(local_space % 16 != 0) {
    local_space += sizeof(size_t);
    realign_stack = true;
  }
  
#ifdef _DEBUG_JIT_JIT
  std::wcout << L"Local space required: " << local_space << L" byte(s)" << std::endl;
#endif
}

//
// translate bytecode to machine code
//
bool JitArm64::Compile(StackMethod* cm)
{
  compile_success = true;

  if(!cm->GetNativeCode()) {
    skip_jump = false;
    method = cm;
    
#ifdef _DEBUG_JIT_JIT
    const long cls_id = method->GetClass()->GetId();
    const long mthd_id = method->GetId();
    std::wcout << L"---------- Compiling Native Code: method_id=" << cls_id << L","
      << mthd_id << L"; mthd_name='" << method->GetName() << L"'; params="
      << method->GetParamCount() << L" ----------" << std::endl;
#endif

    code = (uint32_t*)malloc(BUFFER_SIZE);
    code_buf_max = BUFFER_SIZE;
    
    ints = new int64_t[MAX_INTS];
    float_consts = new double[MAX_DBLS];
    
    local_space = floats_index = instr_index = code_index = instr_count = 0;
    float_consts[floats_index++] = 0.0;
    
    // general use registers
    aval_regs.push_back(new RegisterHolder(X7, false));
    aval_regs.push_back(new RegisterHolder(X6, false));
    aval_regs.push_back(new RegisterHolder(X5, false));
    aval_regs.push_back(new RegisterHolder(X4, false));
    aval_regs.push_back(new RegisterHolder(X3, false));
    aval_regs.push_back(new RegisterHolder(X2, false));
    aval_regs.push_back(new RegisterHolder(X1, false));
    aval_regs.push_back(new RegisterHolder(X0, false));
    
    // aux general use registers
/*  other registers
   aval_regs.push_back(new RegisterHolder(X15, false));
   aval_regs.push_back(new RegisterHolder(X14, false));
   aval_regs.push_back(new RegisterHolder(X13, false));
   aval_regs.push_back(new RegisterHolder(X12, false));
   aval_regs.push_back(new RegisterHolder(X11, false));
   aval_regs.push_back(new RegisterHolder(X10, false)); // used
   aval_regs.push_back(new RegisterHolder(X9, false)); // used
*/
    
    
    // floating point registers
    // D8-D15 are callee-saved (will be saved/restored in prolog/epilog)
    aval_fregs.push_back(new RegisterHolder(D15, true));
    aval_fregs.push_back(new RegisterHolder(D14, true));
    aval_fregs.push_back(new RegisterHolder(D13, true));
    aval_fregs.push_back(new RegisterHolder(D12, true));
    aval_fregs.push_back(new RegisterHolder(D11, true));
    aval_fregs.push_back(new RegisterHolder(D10, true));
    aval_fregs.push_back(new RegisterHolder(D9, true));
    aval_fregs.push_back(new RegisterHolder(D8, true));
    // D0-D7 are caller-saved (no save/restore needed)
    aval_fregs.push_back(new RegisterHolder(D7, true));
    aval_fregs.push_back(new RegisterHolder(D6, true));
    aval_fregs.push_back(new RegisterHolder(D5, true));
    aval_fregs.push_back(new RegisterHolder(D4, true));
    aval_fregs.push_back(new RegisterHolder(D3, true));
    aval_fregs.push_back(new RegisterHolder(D2, true));
    aval_fregs.push_back(new RegisterHolder(D1, true));
    aval_fregs.push_back(new RegisterHolder(D0, true));
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"Compiling code for AArch64 architecture..." << std::endl;
#endif
    
    // process offsets
    ProcessIndices();
    
    // setup
    Prolog();
    
    // register root
    RegisterRoot();
    
    // translate parameters
    ProcessParameters(method->GetParamCount());
    
    // tranlsate program
    ProcessInstructions();
    
    if(!compile_success) {
      free(code);
      code = nullptr;
      
      delete[] ints;
      ints = nullptr;
      
      delete[] float_consts;
      float_consts = nullptr;
      
      return false;
    }
    
    // update jump addresses
    unordered_map<long, StackInstr*>::iterator jmp_iter;
    for(jmp_iter = jump_table.begin(); jmp_iter != jump_table.end(); ++jmp_iter) {
      StackInstr* instr = jmp_iter->second;
      const long src_offset = jmp_iter->first - 1;
      const long dest_index = instr->GetOperand() - 1;
      const long dest_offset = method->GetInstruction(dest_index)->GetOffset();
      const long offset = dest_offset - src_offset;
      
      // unconditional jump
      if(code[src_offset] == B_INSTR) {
        if(offset < 0) {
          uint32_t value = 0x17000000;
          value |= offset & 0x00ffffff;
          code[src_offset] = value;
        }
        else {
          code[src_offset] |= offset;
        }
      }
      // conditional jump
      else {
        if(offset < 0) {
          code[src_offset] |= 0xFFFFE0;
          code[src_offset] ^= (abs(offset) - 1) << 5;
        }
        else {
          code[src_offset] |= offset << 5;
        }
      }
      
#ifdef _DEBUG_JIT_JIT
      std::wcout << L"jump update: src=" << src_offset << L"; dest=" << dest_offset << endl;
#endif
    }
    
    // update error return codes
    for(size_t i = 0; i < deref_offsets.size(); ++i) {
      const long index = deref_offsets[i];
      const long offset = epilog_index - index + 1;
      code[index] |= offset << 5;
    }
    
    for(size_t i = 0; i < bounds_less_offsets.size(); ++i) {
      const long index = bounds_less_offsets[i];
      const long offset = epilog_index - index + 3;
      code[index] |= offset << 5;
    }

    for(size_t i = 0; i < bounds_greater_offsets.size(); ++i) {
      const long index = bounds_greater_offsets[i];
      const long offset = epilog_index - index + 5;
      code[index] |= offset << 5;
    }
    
    for(size_t i = 0; i < div_by_zero_offsets.size(); ++i) {
      const long index = div_by_zero_offsets[i];
      const long offset = epilog_index - index + 7;
      code[index] |= offset << 5;
    }
    
    // update consts pools
    int ints_index = 0;
    unordered_map<size_t, size_t> int_pool_cache;
    multimap<size_t, size_t>::iterator int_pool_iter = const_int_pool.begin();
    for(; int_pool_iter != const_int_pool.end(); ++int_pool_iter) {
      const int64_t const_value = int_pool_iter->first;
      const int64_t src_offset = int_pool_iter->second;
      
      // 12-bit max for ldr offset
      if(ints_index >= MAX_INTS) {
        free(code);
        code = nullptr;
        
        delete[] ints;
        ints = nullptr;

        delete[] float_consts;
        float_consts = nullptr;

        return false;
      }
      
#ifdef _DEBUG_JIT_JIT
      assert(ints_index < MAX_INTS);
#endif
      
      unordered_map<size_t, size_t>::iterator int_pool_found = int_pool_cache.find(const_value);
      if(int_pool_found != int_pool_cache.end()) {
        code[src_offset] |= int_pool_found->second << 10;
      }
      else {
        code[src_offset] |= ints_index << 10;
        int_pool_cache.insert(pair<size_t, size_t>(const_value, ints_index));
        ints[ints_index++] = const_value;
      }
    }
        
#ifdef _DEBUG_JIT_JIT
    std::wcout << L"------------------------" << std::endl;
    std::wcout << L"int const pool: size=" << int_pool_cache.size() << L" ["
          << int_pool_cache.size() * sizeof(size_t) << L" of " << sizeof(size_t) * MAX_INTS << L" byte(s)]" << std::endl;
    std::wcout << L"Caching JIT code: actual=" << code_index << L", buffer=" << code_buf_max << L" byte(s)" << std::endl;
#endif
    
    // store compiled code
    method->SetNativeCode(new NativeCode(page_manager->GetPage(code, code_index), code_index, ints, float_consts));
    
    free(code);
    code = nullptr;
    
    compile_success = true;
  }

  return compile_success;
}

/**
 * JitRuntime class
 */
StackProgram* JitRuntime::program;

void JitRuntime::Initialize(StackProgram* p)
{
  program = p;
}

// Executes machine code
long JitRuntime::Execute(StackMethod* method, size_t* inst, size_t* op_stack, size_t* stack_pos,
                          StackFrame** call_stack, long* call_stack_pos, StackFrame* frame)
{
  const int32_t cls_id = method->GetClass()->GetId();
  const int32_t mthd_id = method->GetId();
  NativeCode* native_code = method->GetNativeCode();
  int64_t* int_consts = native_code->GetInts();

#ifdef _DEBUG_JIT_JIT
  size_t code_size = native_code->GetSize();
  std::wcout << L"=== MTHD_CALL (native): id=" << cls_id << L"," << mthd_id << L"; name='" << method->GetName()
        << L"'; self=" << inst << L"(" << (size_t)inst << L"); stack=" << op_stack << L"; stack_pos="
        << (*stack_pos) << L"; params=" << method->GetParamCount() << L"; code=" << (size_t *)native_code->GetCode() << L"; code_index="
        << code_size << L" ===" << std::endl;
  assert((*stack_pos) >= method->GetParamCount());
#endif
  
  // create function
  uint32_t* code = native_code->GetCode();
  jit_fun_ptr jit_fun = (jit_fun_ptr)code;
  
  // execute
  const long rtrn_value = jit_fun(cls_id, mthd_id, method->GetClass()->GetClassMemory(), inst, op_stack, stack_pos,
                                  call_stack, call_stack_pos, &(frame->jit_mem), &(frame->jit_offset), int_consts);

#ifdef _DEBUG_JIT_JIT
  std::wcout << L"JIT return: " << rtrn_value << endl;
#endif
   
   return rtrn_value;
}

/**
 * Manage executable buffers of memory
 */
PageManager::PageManager()
{
  for(int i = 0; i < 4; ++i) {
    holders.push_back(new PageHolder(PAGE_SIZE * (i + 1)));
  }
}

PageManager::~PageManager()
{
  while(!holders.empty()) {
    PageHolder* tmp = holders.front();
    holders.erase(holders.begin());
    // delete
    delete tmp;
    tmp = nullptr;
  }
}

uint32_t* PageManager::GetPage(uint32_t* code, int32_t size)
{
  bool placed = false;

  uint32_t* temp = nullptr;
  for(size_t i = 0; !placed && i < holders.size(); ++i) {
    PageHolder* holder = holders[i];
    if(holder->CanAddCode(size)) {
      temp = holder->AddCode(code, size);
      placed = true;
    }
  }

  if(!placed) {
    PageHolder* buffer = new PageHolder(size);
    temp = buffer->AddCode(code, size);
    holders.push_back(buffer);
  }

  return temp;
}

/**
 * PageHolder class
 */
uint32_t* PageHolder::AddCode(uint32_t* code, int32_t size) {
  // get index into buffer
  uint32_t* temp = buffer + index;
  
  // copy and flush instruction cache
  const uint32_t byte_size = size * sizeof(uint32_t);
  
#ifdef _OSX
  pthread_jit_write_protect_np(false);			  
#endif
  
  memcpy(temp, code, byte_size);

#if defined(_OSX)
  __clear_cache(temp, temp + byte_size);
#elif defined(_M_ARM64)
  if(!FlushInstructionCache(GetCurrentProcess(), temp, byte_size)) {
    wcerr << L">>> Unable to flush instruction cache! <<<" << std::endl;
    exit(1);
  }
#else
  __builtin___clear_cache(temp, temp + byte_size);
#endif
  
#ifdef _OSX
  pthread_jit_write_protect_np(true);
#endif
  
  index += size;
  available -= byte_size;

  return temp;
}