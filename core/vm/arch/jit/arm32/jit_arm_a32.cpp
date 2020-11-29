/***************************************************************************
 * JIT compiler for ARMv7 architecture (A1 encoding)
 *
 * Copyright (c) 2019-2020, Randy Hollines
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
 ***************************************************************************/

#include "jit_arm_a32.h"
#include <string>

using namespace Runtime;

/********************************
 * JitCompilerA32 class
 ********************************/
StackProgram* JitCompilerA32::program;
PageManager* JitCompilerA32::page_manager;

void JitCompilerA32::Initialize(StackProgram* p) {
  program = p;
  page_manager = new PageManager;
}

// setup of stackframe
void JitCompilerA32::Prolog() {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [<prolog>]" << endl;
#endif

  uint32_t setup_code[] = {
    0xe52db004,                 // push  {fp}
    0xe92d01f0,						      // push {r4-r8}
		0xe28db000,						      // add fp, sp, #0
		0xe24dd000 + local_space,   // sub sp, sp, #local_space
		0xe50b0008,						      // str r0, [fp, #-8]
		0xe50b100c,						      // str r1, [fp, #-12]
		0xe50b2010,						      // str r2, [fp, #-16]
		0xe50b3014						      // str r3, [fp, #-20]
	};
  const int32_t setup_size = sizeof(setup_code) / sizeof(int32_t);
  // copy setup
  for(int32_t i = 0; i < setup_size; ++i) {
    AddMachineCode(setup_code[i]);
  }
}

// teardown of stackframe
void JitCompilerA32::Epilog() {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [<epilog>]" << endl;
#endif
  
  epilog_index = code_index;
  
  // nominal
  AddMachineCode(0xea000005);
  
  // nullptr deref
  move_imm_reg(-1, R0);
  AddMachineCode(0xea000004);

  // under bounds
  move_imm_reg(-2, R0);
  AddMachineCode(0xea000002);

  // over bounds
  move_imm_reg(-3, R0);
  AddMachineCode(0xea000000);
    
  move_imm_reg(0, R0);
  uint32_t teardown_code[] = {
    0xe24bd000, // sub sp, fp, #0
    0xe8bd01f0, // pop {r4-r7}‬
    0xe49db004, // pop {fp}
    0xe12fff1e  // bx  lr
  };
  
  // copy teardown
  const int32_t teardown_size = sizeof(teardown_code) / sizeof(int32_t);
  for(int32_t i = 0; i < teardown_size; ++i) {
    AddMachineCode(teardown_code[i]);
  }
}

void JitCompilerA32::RegisterRoot() {
  RegisterHolder* holder = GetRegister();
  RegisterHolder* mem_holder = GetRegister();
    
  // offset required to get to the first local variable
  int32_t offset = local_space + TMP_REG_5 - 4;
  if(realign_stack) {
    offset -= 4;
  }
  
  move_reg_reg(FP, holder->GetRegister());
  sub_imm_reg(offset, holder->GetRegister());
  
  // set JIT memory pointer to stack
  move_mem_reg(JIT_MEM, FP, mem_holder->GetRegister());
  move_reg_mem(holder->GetRegister(), 0, mem_holder->GetRegister());
  
  // set JIT offset to 0
  move_mem_reg(JIT_OFFSET, FP, mem_holder->GetRegister());
  move_imm_mem(offset, 0, mem_holder->GetRegister());
  
  // clean up
  ReleaseRegister(mem_holder);
  ReleaseRegister(holder);
  
  // zero out memory
  RegisterHolder* start_reg = GetRegister();
  RegisterHolder* end_reg = GetRegister();
  RegisterHolder* cur_reg = GetRegister();
  
  // set start
  move_reg_reg(FP, start_reg->GetRegister());
  sub_imm_reg(-TMP_REG_0, start_reg->GetRegister());
  
  // set end
  move_reg_reg(FP, end_reg->GetRegister());
  sub_imm_reg(offset, end_reg->GetRegister());
  
  // compare
  cmp_reg_reg(start_reg->GetRegister(), end_reg->GetRegister());
#ifdef _DEBUG
  std::wcout << L"  " << (++instr_count) << L": [bgt]" << std::endl;
#endif
  AddMachineCode(0xca000004);
  
  // zero out address and advance
  move_reg_reg(start_reg->GetRegister(), cur_reg->GetRegister());
  move_imm_mem(0, 0, cur_reg->GetRegister());               
  sub_imm_reg(4, start_reg->GetRegister());
  
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [b <imm>]" << endl;
#endif
  AddMachineCode(0xeafffff8);
  
  ReleaseRegister(cur_reg);
  ReleaseRegister(end_reg);
  ReleaseRegister(start_reg);
}

void JitCompilerA32::ProcessParameters(int32_t params) {
#ifdef _DEBUG
  wcout << L"CALLED_PARMS: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  for(int32_t i = 0; i < params; ++i) {
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, FP, op_stack_holder->GetRegister());

    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);  

    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());
      
    if(instr->GetType() == STOR_LOCL_INT_VAR || instr->GetType() == STOR_CLS_INST_INT_VAR) {
      RegisterHolder* dest_holder = GetRegister();
      dec_mem(0, stack_pos_holder->GetRegister());  
      move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
      shl_imm_reg(2, stack_pos_holder->GetRegister());
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
      shl_imm_reg(2, stack_pos_holder->GetRegister());
      add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());
      move_mem_reg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
      
      RegisterHolder* dest_holder2 = GetRegister();
      move_mem_reg(-(long)(sizeof(int32_t)), op_stack_holder->GetRegister(), dest_holder2->GetRegister());
      
      move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());
      dec_mem(0, stack_pos_holder->GetRegister());      
      
      working_stack.push_front(new RegInstr(dest_holder2));
      working_stack.push_front(new RegInstr(dest_holder));
      
      // store int
      ProcessStore(instr);
      i++;
    }
    else {
      RegisterHolder* dest_holder = GetFpRegister();
      sub_imm_mem(2, 0, stack_pos_holder->GetRegister());
      move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
      shl_imm_reg(2, stack_pos_holder->GetRegister());
      add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister()); 
      move_mem_xreg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
      working_stack.push_front(new RegInstr(dest_holder));
      // store float
      ProcessStore(instr);
    }
    ReleaseRegister(op_stack_holder);
    ReleaseRegister(stack_pos_holder);
  }
}

void JitCompilerA32::ProcessIntCallParameter() {
#ifdef _DEBUG
  wcout << L"INT_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, FP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());
  
  dec_mem(0, stack_pos_holder->GetRegister());  
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(2, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());  
  move_mem_reg(0, op_stack_holder->GetRegister(), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));
  
  ReleaseRegister(stack_pos_holder);
}

void JitCompilerA32::ProcessFunctionCallParameter() {
#ifdef _DEBUG
  wcout << L"FUNC_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, FP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());
  
  sub_imm_mem(2, 0, stack_pos_holder->GetRegister());

  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(2, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());  
  
  RegisterHolder* holder = GetRegister();
  move_reg_reg(op_stack_holder->GetRegister(), holder->GetRegister());
  
  move_mem_reg(0, op_stack_holder->GetRegister(), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));
  
  move_mem_reg(4, holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));
  
  ReleaseRegister(stack_pos_holder);
}

void JitCompilerA32::ProcessFloatCallParameter() {
#ifdef _DEBUG
  wcout << L"FLOAT_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, FP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());
  
  RegisterHolder* dest_holder = GetFpRegister();
  sub_imm_mem(2, 0, stack_pos_holder->GetRegister());
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(2, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister()); 
  move_mem_xreg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
  working_stack.push_front(new RegInstr(dest_holder));
  
  ReleaseRegister(op_stack_holder);
  ReleaseRegister(stack_pos_holder);
}

void JitCompilerA32::ProcessInstructions() {
  while(instr_index < method->GetInstructionCount() && compile_success) {
    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);
    
    switch(instr->GetType()) {
      // load literal
    case LOAD_CHAR_LIT:
    case LOAD_INT_LIT:
#ifdef _DEBUG
      wcout << L"LOAD_INT: value=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
      break;

      // float literal
    case LOAD_FLOAT_LIT:
#ifdef _DEBUG
      wcout << L"LOAD_FLOAT_LIT: value=" << instr->GetFloatOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      float_consts[floats_index] = instr->GetFloatOperand();
      working_stack.push_front(new RegInstr(instr, &float_consts[floats_index++]));
      break;
      
      // load self
    case LOAD_INST_MEM: {
#ifdef _DEBUG
      wcout << L"LOAD_INST_MEM; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;

      // load self
    case LOAD_CLS_MEM: {
#ifdef _DEBUG
      wcout << L"LOAD_CLS_MEM; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;
      
      // load variable
    case LOAD_LOCL_INT_VAR:
    case LOAD_CLS_INST_INT_VAR:
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
#ifdef _DEBUG
      wcout << L"LOAD_INT_VAR/LOAD_FLOAT_VAR/LOAD_FUNC_VAR: id=" << instr->GetOperand() << L"; regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessLoad(instr);
      break;
    
      // store value
    case STOR_LOCL_INT_VAR:
    case STOR_CLS_INST_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
#ifdef _DEBUG
      wcout << L"STOR_INT_VAR/STOR_FLOAT_VAR/STOR_FUNC_VAR: id=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStore(instr);
      break;

      // copy value
    case COPY_LOCL_INT_VAR: 
    case COPY_CLS_INST_INT_VAR:
    case COPY_FLOAT_VAR:
#ifdef _DEBUG
      wcout << L"COPY_INT_VAR/COPY_FLOAT_VAR: id=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
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
#ifdef _DEBUG
      wcout << L"INT ADD/SUB/MUL/DIV/MOD/BIT_AND/BIT_OR/BIT_XOR/LES/GTR/EQL/NEQL/SHL_INT/SHR_INT:: regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessIntCalculation(instr);
      break;
      
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
#ifdef _DEBUG
      wcout << L"FLOAT ADD/SUB/MUL/DIV/: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);
      break;
      
    case SIN_FLOAT:
    case COS_FLOAT:
    case TAN_FLOAT:
    case SQRT_FLOAT:
    case ASIN_FLOAT:
    case ACOS_FLOAT:
    case LOG_FLOAT:
#ifdef _DEBUG
      wcout << L"FLOAT SIN/COS/TAN/SQRT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatOperation(instr);
      break;
      
    case ATAN2_FLOAT:
    case POW_FLOAT:
#ifdef _DEBUG
      wcout << L"POW/ATAN2: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatOperation2(instr);
      break;
      
    case LES_FLOAT:
    case GTR_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT:

#ifdef _DEBUG
      wcout << L"FLOAT LES/GTR/EQL/NEQL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);
      break;
      
    case RTRN:
#ifdef _DEBUG
      wcout << L"RTRN: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessReturn();
      // teardown
      Epilog();
      break;
      
    case MTHD_CALL: {
      StackMethod* called_method = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
      if(called_method) {
#ifdef _DEBUG
        assert(called_method);
        wcout << L"MTHD_CALL: name='" << called_method->GetName() << L"': id="<< instr->GetOperand() 
              << L"," << instr->GetOperand2() << L", params=" << (called_method->GetParamCount() + 1) 
              << L": regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif      
        // passing instance variable
        ProcessStackCallback(MTHD_CALL, instr, instr_index, called_method->GetParamCount() + 1);
        ProcessReturnParameters(called_method->GetReturn());	
      }
    }
      break;
      
    case DYN_MTHD_CALL: {
#ifdef _DEBUG
      wcout << L"DYN_MTHD_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif  
      // passing instance variable
      ProcessStackCallback(DYN_MTHD_CALL, instr, instr_index, instr->GetOperand() + 3);
      ProcessReturnParameters((MemoryType)instr->GetOperand2());
    }
      break;
      
    case NEW_BYTE_ARY:
#ifdef _DEBUG
      wcout << L"NEW_BYTE_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_BYTE_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_CHAR_ARY:
#ifdef _DEBUG
      wcout << L"NEW_CHAR_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_CHAR_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_INT_ARY:
#ifdef _DEBUG
      wcout << L"NEW_INT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size() 
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_INT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_FLOAT_ARY:
#ifdef _DEBUG
      wcout << L"NEW_FLOAT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size() 
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_FLOAT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_OBJ_INST: {
#ifdef _DEBUG
      StackClass* called_klass = program->GetClass(instr->GetOperand());      
      wcout << L"NEW_OBJ_INST: name='" << called_klass->GetName() << L"': id=" << instr->GetOperand() 
            << L": regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      // note: object id passed in instruction param
      ProcessStackCallback(NEW_OBJ_INST, instr, instr_index, 0);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
     
    case THREAD_JOIN: {
#ifdef _DEBUG
      wcout << L"THREAD_JOIN: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(THREAD_JOIN, instr, instr_index, 0);
    }
      break;

    case THREAD_SLEEP: {
#ifdef _DEBUG
      wcout << L"THREAD_SLEEP: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(THREAD_SLEEP, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_START: {
#ifdef _DEBUG
      wcout << L"CRITICAL_START: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CRITICAL_START, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_END: {
#ifdef _DEBUG
      wcout << L"CRITICAL_END: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CRITICAL_END, instr, instr_index, 1);
    }
      break;
      
    case CPY_BYTE_ARY: {
#ifdef _DEBUG
      wcout << L"CPY_BYTE_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_BYTE_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_CHAR_ARY: {
#ifdef _DEBUG
      wcout << L"CPY_CHAR_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_CHAR_ARY, instr, instr_index, 5);
    }
      break;
      
    case CPY_INT_ARY: {
#ifdef _DEBUG
      wcout << L"CPY_INT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_INT_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_FLOAT_ARY: {
#ifdef _DEBUG
      wcout << L"CPY_FLOAT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_FLOAT_ARY, instr, instr_index, 5);
    }
      break;
 
    case TRAP:
#ifdef _DEBUG
      wcout << L"TRAP: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(TRAP, instr, instr_index, instr->GetOperand());
      break;

    case TRAP_RTRN:
#ifdef _DEBUG
      wcout << L"TRAP_RTRN: args=" << instr->GetOperand() << L"; regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << endl;
      assert(instr->GetOperand());
#endif      
      ProcessStackCallback(TRAP_RTRN, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case STOR_BYTE_ARY_ELM:
#ifdef _DEBUG
      wcout << L"STOR_BYTE_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessStoreByteElement(instr);
      break;

    case STOR_CHAR_ARY_ELM:
#ifdef _DEBUG
      wcout << L"STOR_CHAR_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessStoreCharElement(instr);
      break;
      
    case STOR_INT_ARY_ELM:
#ifdef _DEBUG
      wcout << L"STOR_INT_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStoreIntElement(instr);
      break;

    case STOR_FLOAT_ARY_ELM:
#ifdef _DEBUG
      wcout << L"STOR_FLOAT_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStoreFloatElement(instr);
      break;

    case SWAP_INT: {
#ifdef _DEBUG
      wcout << L"SWAP_INT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
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
#ifdef _DEBUG
      wcout << L"POP_INT/POP_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
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

    case FLOR_FLOAT:
#ifdef _DEBUG
      wcout << L"FLOR_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloor(instr);
      break;

    case CEIL_FLOAT:
#ifdef _DEBUG
      wcout << L"CEIL_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessCeiling(instr);
      break;
      
    case F2I:
#ifdef _DEBUG
      wcout << L"F2I: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatToInt(instr);
      break;

    case I2F:
#ifdef _DEBUG
      wcout << L"I2F: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessIntToFloat(instr);
      break;

    case I2S:
#ifdef _DEBUG
      wcout << L"I2S: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(I2S, instr, instr_index, 3);
      break;
      
    case F2S:
#ifdef _DEBUG
      wcout << L"F2S: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(F2S, instr, instr_index, 2);
      break;
      
    case S2F:
#ifdef _DEBUG
      wcout << L"S2F: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(S2F, instr, instr_index, 2);
      ProcessReturnParameters(FLOAT_TYPE);
      break;
      
    case S2I:
#ifdef _DEBUG
      wcout << L"S2I: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(S2I, instr, instr_index, 2);
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case OBJ_TYPE_OF: {
#ifdef _DEBUG
      wcout << L"OBJ_TYPE_OF: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(OBJ_TYPE_OF, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case OBJ_INST_CAST: {
#ifdef _DEBUG
      wcout << L"OBJ_INST_CAST: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(OBJ_INST_CAST, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;

    case LOAD_ARY_SIZE: {
#ifdef _DEBUG
      wcout << L"LOAD_ARY_SIZE: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(LOAD_ARY_SIZE, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case LOAD_BYTE_ARY_ELM:
#ifdef _DEBUG
      wcout << L"LOAD_BYTE_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessLoadByteElement(instr);
      break;

    case LOAD_CHAR_ARY_ELM:
#ifdef _DEBUG
      wcout << L"LOAD_CHAR_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessLoadCharElement(instr);
      break;
      
    case LOAD_INT_ARY_ELM:
#ifdef _DEBUG
      wcout << L"LOAD_INT_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessLoadIntElement(instr);
      break;

    case LOAD_FLOAT_ARY_ELM:
#ifdef _DEBUG
      wcout << L"LOAD_FLOAT_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessLoadFloatElement(instr);
      break;
      
    case JMP:
      ProcessJump(instr);
      break;
      
    case LBL:
#ifdef _DEBUG
      wcout << L"LBL: id=" << instr->GetOperand() << endl;
#endif
      break;
      
    default: {
      InstructionType error = (InstructionType)instr->GetType();
      wcerr << L"Unknown instruction: " << error << L"!" << endl;
      exit(1);
    }
      break;
    }
  }
}

void JitCompilerA32::ProcessLoad(StackInstr* instr) {
  // method/function memory
  if(instr->GetOperand2() == LOCL) {
    if(instr->GetType() == LOAD_FUNC_VAR) {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(instr->GetOperand3() + sizeof(int32_t), FP, holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
      
      RegisterHolder* holder2 = GetRegister();
      move_mem_reg(instr->GetOperand3(), FP, holder2->GetRegister());
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
      move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
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
      move_mem_reg(instr->GetOperand3() + sizeof(int32_t), holder->GetRegister(), holder2->GetRegister());
      working_stack.push_front(new RegInstr(holder2));
      
      move_mem_reg(instr->GetOperand3(), holder->GetRegister(), holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));    
    }
    // float value
    else {
      RegisterHolder* fp_holder = GetFpRegister();
      move_mem_xreg(instr->GetOperand3(), holder->GetRegister(), fp_holder->GetRegister());
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(fp_holder));	  
    }

    delete left;
    left = nullptr;
  }
}

void JitCompilerA32::ProcessJump(StackInstr* instr) {
  if(!skip_jump) {
#ifdef _DEBUG
    wcout << L"JMP: id=" << instr->GetOperand() << L", regs=" << aval_regs.size() 
          << L"," << aux_regs.size() << endl;
#endif
    if(instr->GetOperand2() < 0) {
#ifdef _DEBUG
      wcout << L"  " << (++instr_count) << L": [b <imm>]" << endl;
#endif
      AddMachineCode(0xea000000);
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
        move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
        cmp_imm_reg(instr->GetOperand2(), holder->GetRegister());
        ReleaseRegister(holder);
      }
        break;

      default:
        wcerr << L">>> Should never occur (compiler bug?) type=" << left->GetType() << L" <<<" << endl;
        exit(1);
        break;
      }

      // compare with register
#ifdef _DEBUG
      std::wcout << L"  " << (++instr_count) << L": [beq]" << std::endl;
#endif
      AddMachineCode(0x0a000000);
      
      // clean up
      delete left;
      left = nullptr;
    }
    
    // store update index
    jump_table.insert(pair<int32_t, StackInstr*>(code_index, instr));
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

void JitCompilerA32::ProcessReturnParameters(MemoryType type) {
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

void JitCompilerA32::ProcessLoadByteElement(StackInstr* instr) {
  RegisterHolder* holder = GetRegister();
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());
  move_mem8_reg(0, elem_holder->GetRegister(), holder->GetRegister());
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitCompilerA32::ProcessLoadCharElement(StackInstr* instr) {
  RegisterHolder* holder = GetRegister();
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());

  move_mem_reg(0, elem_holder->GetRegister(), holder->GetRegister());
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitCompilerA32::ProcessLoadIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  move_mem_reg(0, elem_holder->GetRegister(), elem_holder->GetRegister());
  working_stack.push_front(new RegInstr(elem_holder));
}

void JitCompilerA32::ProcessLoadFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(0, elem_holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));
  ReleaseRegister(elem_holder);
}

void JitCompilerA32::ProcessStoreByteElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem8(left->GetOperand(), 0, elem_holder->GetRegister());
    ReleaseRegister(elem_holder);
    break;

  case MEM_INT: {    
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  case REG_INT: {
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = left->GetRegister();
    if(holder->GetRegister() == R12) {
      RegisterHolder* tmp_holder = GetRegister(false);
      move_reg_reg(holder->GetRegister(), tmp_holder->GetRegister());
      move_reg_mem8(tmp_holder->GetRegister(), 0, elem_holder->GetRegister());      
      ReleaseRegister(tmp_holder);
    }
    else {
      move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());      
    }
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

void JitCompilerA32::ProcessStoreCharElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
  move_imm_mem(left->GetOperand(), 0, elem_holder->GetRegister());
    ReleaseRegister(elem_holder);
    break;

  case MEM_INT: {    
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  case REG_INT: {
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = left->GetRegister();
    if(holder->GetRegister() == R12) {
      RegisterHolder* tmp_holder = GetRegister(false);
      move_reg_reg(holder->GetRegister(), tmp_holder->GetRegister());
      move_reg_mem(tmp_holder->GetRegister(), 0, elem_holder->GetRegister());
      ReleaseRegister(tmp_holder);
    }
    else {
      move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    }
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

void JitCompilerA32::ProcessStoreIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem(left->GetOperand(), 0, elem_holder->GetRegister());
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
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

void JitCompilerA32::ProcessStoreFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT:
    move_imm_memx(left, 0, elem_holder->GetRegister());
    break;

  case MEM_FLOAT: 
  case MEM_INT: {
    RegisterHolder* holder = GetFpRegister();
    move_mem_xreg(left->GetOperand(), FP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseFpRegister(holder);
  }
    break;

  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
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

void JitCompilerA32::ProcessFloor(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    round_imm_xreg(left, holder->GetRegister(), true);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case MEM_FLOAT:
  case MEM_INT: {
    RegisterHolder* holder = GetFpRegister();
    round_mem_xreg(left->GetOperand(), FP, holder->GetRegister(), true);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case REG_FLOAT:
    round_xreg_xreg(left->GetRegister()->GetRegister(), 
                    left->GetRegister()->GetRegister(), true);
    working_stack.push_front(left);
    break;

  default:
    break;
  }
}

void JitCompilerA32::ProcessCeiling(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    round_imm_xreg(left, holder->GetRegister(), false);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case MEM_FLOAT:
  case MEM_INT: {
    RegisterHolder* holder = GetFpRegister();
    round_mem_xreg(left->GetOperand(), FP, holder->GetRegister(), false);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case REG_FLOAT:
    round_xreg_xreg(left->GetRegister()->GetRegister(), left->GetRegister()->GetRegister(), false);
    working_stack.push_front(left);
    break;

  default:
    break;
  }
}

void JitCompilerA32::ProcessFloatToInt(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetRegister();
  switch(left->GetType()) {
  case IMM_FLOAT:
    vcvt_imm_reg(left, holder->GetRegister());
    break;
    
  case MEM_FLOAT:
  case MEM_INT:
    vcvt_mem_reg(left->GetOperand(), FP, holder->GetRegister());
    break;

  case REG_FLOAT:
    vcvt_xreg_reg(left->GetRegister()->GetRegister(), holder->GetRegister());
    ReleaseFpRegister(left->GetRegister());
    break;

  default:
    break;
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitCompilerA32::ProcessIntToFloat(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetFpRegister();
  switch(left->GetType()) {
  case IMM_INT:
    vcvt_imm_xreg(left, holder->GetRegister());
    break;
    
  case MEM_INT:
    vcvt_mem_xreg(left->GetOperand(), 
		 FP, holder->GetRegister());
    break;

  case REG_INT:
    vcvt_reg_xreg(left->GetRegister()->GetRegister(), 
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

void JitCompilerA32::ProcessStore(StackInstr* instr) {
  Register dest;
  RegisterHolder* addr_holder = nullptr;

  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = FP;
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
      move_mem_reg(left->GetOperand(), FP, addr_holder->GetRegister());
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
      move_imm_mem(left->GetOperand(), instr->GetOperand3(), dest);
      
      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_imm_mem(left2->GetOperand(), instr->GetOperand3() + sizeof(int32_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {
      move_imm_mem(left->GetOperand(), instr->GetOperand3(), dest);
    }
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    if(instr->GetType() == STOR_FUNC_VAR) {
      move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);

      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_mem_reg(left2->GetOperand(), FP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3() + sizeof(int32_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {      
      move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
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
      move_reg_mem(holder2->GetRegister(), instr->GetOperand3() + sizeof(int32_t), dest);
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
    move_imm_memx(left, instr->GetOperand3(), dest);
    break;
    
  case MEM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    move_mem_xreg(left->GetOperand(), FP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    ReleaseFpRegister(holder);
  }
    break;
    
  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
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

void JitCompilerA32::ProcessCopy(StackInstr* instr) {
  Register dest;
  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = FP;
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();

    RegisterHolder* holder = GetRegister();
    move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
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
    move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
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
    move_imm_xreg(left, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;

  case MEM_FLOAT: {
    RegisterHolder* holder = GetFpRegister();
    move_mem_xreg(left->GetOperand(), FP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    // save register
    working_stack.pop_front();
    working_stack.push_front(new RegInstr(holder));

    delete left;
    left = nullptr;
  }
    break;
    
  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
  }
    break;
  }
}

void JitCompilerA32::ProcessStackCallback(int32_t instr_id, StackInstr* instr, int32_t &instr_index, int32_t params) {
  int32_t non_params;
  if(params < 0) {
    non_params = 0;
  }
  else {
    non_params = working_stack.size() - params;
  }
  
#ifdef _DEBUG
  wcout << L"Return: params=" << params << L", non-params=" << non_params << endl;
#endif
  
  stack<RegInstr*> regs;
  stack<int32_t> dirty_regs;
  int32_t reg_offset = TMP_REG_0;  

  stack<RegInstr*> fp_regs;
  stack<int32_t> dirty_fp_regs;
  int32_t fp_offset = TMP_D_0;
  
  int32_t i = 0;     
  for(deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
    RegInstr* left = (*iter);
    if(i < non_params) {
      switch(left->GetType()) {
      case REG_INT:
	      move_reg_mem(left->GetRegister()->GetRegister(), reg_offset, FP);
        dirty_regs.push(reg_offset);
        regs.push(left);
        reg_offset -= 4;
        break;

      case REG_FLOAT:
	      move_xreg_mem(left->GetRegister()->GetRegister(), fp_offset, FP);
        dirty_fp_regs.push(fp_offset);
        fp_regs.push(left);
        fp_offset -= 8;
        break;

      default:
        break;
      }
      // update
      i++;
    }
  }

#ifdef _DEBUG
  assert(reg_offset >= TMP_REG_5);
  assert(fp_offset >= TMP_D_2);
#endif

  if(dirty_regs.size() > 6 || dirty_fp_regs.size() > 3 ) {
    compile_success = false;
  }

  // copy values to execution stack
  ProcessReturn(params);
  
  // set parameters
  move_imm_mem(instr_index - 1, 20, SP);
  
  RegisterHolder* reg_holder = GetRegister();
  
  move_mem_reg(CALL_STACK_POS, FP, reg_holder->GetRegister());
  move_reg_mem(reg_holder->GetRegister(), 16, SP);
  
  move_mem_reg(CALL_STACK, FP, reg_holder->GetRegister());
  move_reg_mem(reg_holder->GetRegister(), 12, SP);
  
  move_mem_reg(OP_STACK_POS, FP, reg_holder->GetRegister());
  move_reg_mem(reg_holder->GetRegister(), 8, SP);
  
  move_mem_reg(OP_STACK, FP, reg_holder->GetRegister());
  move_reg_mem(reg_holder->GetRegister(), 4, SP);
  
  move_mem_reg(INSTANCE_MEM, FP, reg_holder->GetRegister());
  move_reg_mem(reg_holder->GetRegister(), 0, SP);

  ReleaseRegister(reg_holder);
  
  move_mem_reg(MTHD_ID, FP, R3);
  move_mem_reg(CLS_ID, FP, R2);
  move_imm_reg((int32_t)instr, R1);
  move_imm_reg(instr_id, R0);
  
  move_imm_reg((uint32_t)JitCompilerA32::JitStackCallback, R8);
  call_reg(R8);
  
  // restore register values
  while(!dirty_regs.empty()) {
    RegInstr* left = regs.top();
    move_mem_reg(dirty_regs.top(), FP, left->GetRegister()->GetRegister());
    // update
    regs.pop();
    dirty_regs.pop();
  }
  
  while(!dirty_fp_regs.empty()) {
    RegInstr* left = fp_regs.top();
    move_mem_xreg(dirty_fp_regs.top(), FP, left->GetRegister()->GetRegister());
    // update
    fp_regs.pop();
    dirty_fp_regs.pop();
  }
}

void JitCompilerA32::ProcessReturn(int32_t params) {
  if(!working_stack.empty()) {
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, FP, op_stack_holder->GetRegister());
    
    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());    
    move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
    shl_imm_reg(2, stack_pos_holder->GetRegister());
    add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());  

    int32_t non_params;
    if(params < 0) {
      non_params = 0;
    }
    else {
      non_params = working_stack.size() - params;
    }
#ifdef _DEBUG
    wcout << L"Return: params=" << params << L", non-params=" << non_params << endl;
#endif
    
    int32_t i = 0;     
    for(deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
      // skip non-params... processed above
      RegInstr* left = (*iter);
      if(i < non_params) {
        i++;
      }
      else {
        move_mem_reg(OP_STACK_POS, FP, stack_pos_holder->GetRegister());            
        switch(left->GetType()) {
        case IMM_INT:
          move_imm_mem(left->GetOperand(), 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(int32_t), op_stack_holder->GetRegister());
          break;
	
        case MEM_INT: {
            RegisterHolder* temp_holder = GetRegister();
            move_mem_reg(left->GetOperand(), FP, temp_holder->GetRegister());
            move_reg_mem(temp_holder->GetRegister(), 0, op_stack_holder->GetRegister());
            inc_mem(0, stack_pos_holder->GetRegister());
            add_imm_reg(sizeof(int32_t), op_stack_holder->GetRegister()); 
            ReleaseRegister(temp_holder);
          }
          break;
	
        case REG_INT:
          move_reg_mem(left->GetRegister()->GetRegister(), 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(int32_t), op_stack_holder->GetRegister()); 
          break;
	
        case IMM_FLOAT:
          move_imm_memx(left, 0, op_stack_holder->GetRegister());
          add_imm_mem(2, 0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(double), op_stack_holder->GetRegister()); 
          break;
	
        case MEM_FLOAT: {
            RegisterHolder* temp_holder = GetFpRegister();
            move_mem_xreg(left->GetOperand(), FP, temp_holder->GetRegister());
            move_xreg_mem(temp_holder->GetRegister(), 0, op_stack_holder->GetRegister());
            add_imm_mem(2, 0, stack_pos_holder->GetRegister());
            add_imm_reg(sizeof(double), op_stack_holder->GetRegister());
            ReleaseFpRegister(temp_holder); 
          }
          break;
	
        case REG_FLOAT:
          move_xreg_mem(left->GetRegister()->GetRegister(), 0, op_stack_holder->GetRegister());
          add_imm_mem(2, 0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(double), op_stack_holder->GetRegister());
          break;
        }    
      }
    }
    ReleaseRegister(op_stack_holder);
    ReleaseRegister(stack_pos_holder);
    
    // clean up working stack
    if(params < 0) {
      params = working_stack.size();
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

RegInstr* JitCompilerA32::ProcessIntFold(long left_imm, long right_imm, InstructionType type) {
  switch(type) {
  case AND_INT:
    return new RegInstr(IMM_INT, left_imm && right_imm);
    
  case OR_INT:
    return new RegInstr(IMM_INT, left_imm || right_imm);
    
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

void JitCompilerA32::ProcessIntCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
    // intermidate
  case IMM_INT:
    switch(right->GetType()) {
    case IMM_INT:
      working_stack.push_front(ProcessIntFold(left->GetOperand(), right->GetOperand(), instruction->GetType()));
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
      move_mem_reg(right->GetOperand(), FP, holder->GetRegister());

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
      math_imm_reg(right->GetOperand(), holder->GetRegister(), 
                   instruction->GetType());
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
      move_mem_reg(right->GetOperand(), FP, rhs->GetRegister());
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
      move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
      math_imm_reg(right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
    case REG_INT: {
      RegisterHolder* lhs = right->GetRegister();
      RegisterHolder* rhs = GetRegister();
      move_mem_reg(left->GetOperand(), FP, rhs->GetRegister());
      math_reg_reg(lhs->GetRegister(), rhs->GetRegister(), instruction->GetType());
      ReleaseRegister(lhs);
      working_stack.push_front(new RegInstr(rhs));
    }
      break;
    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(left->GetOperand(), FP, holder->GetRegister());
      math_mem_reg(right->GetOperand(), holder->GetRegister(), instruction->GetType());
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

 void JitCompilerA32::ProcessFloatCalculation(StackInstr* instruction) {
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
      move_imm_xreg(left, left_holder->GetRegister());      
      
      RegisterHolder* right_holder = GetFpRegister();
      move_imm_xreg(right, right_holder->GetRegister());      
      
      math_xreg_xreg(right_holder->GetRegister(), left_holder, instruction->GetType());
      ReleaseFpRegister(right_holder);
      working_stack.push_front(new RegInstr(left_holder));
    }
      break;
      
    case REG_FLOAT: {      
      RegisterHolder* imm_holder = GetFpRegister();
      move_imm_xreg(left, imm_holder->GetRegister());
      
      math_xreg_xreg(right->GetRegister()->GetRegister(), imm_holder, type);
      ReleaseFpRegister(right->GetRegister());
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = GetFpRegister();
      move_mem_xreg(right->GetOperand(), FP, holder->GetRegister());

      RegisterHolder* imm_holder = GetFpRegister();
      move_imm_xreg(left, imm_holder->GetRegister());

      math_xreg_xreg(holder->GetRegister(), imm_holder, type);
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
      move_imm_xreg(right, right_holder->GetRegister());
      
      math_xreg_xreg(right_holder->GetRegister(), left_holder, instruction->GetType());
      ReleaseFpRegister(right_holder);      
      working_stack.push_front(new RegInstr(left_holder));
    }
      break;

    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      RegisterHolder* temp = left->GetRegister();
      math_xreg_xreg(holder->GetRegister(), temp, instruction->GetType());
      working_stack.push_front(new RegInstr(temp));
      ReleaseFpRegister(holder);
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = left->GetRegister();
      math_mem_xreg(right->GetOperand(), holder, instruction->GetType());
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
      move_mem_xreg(left->GetOperand(), FP, holder->GetRegister());
      
      RegisterHolder* imm_holder = GetFpRegister();
      move_imm_xreg(right, imm_holder->GetRegister());
      math_xreg_xreg(imm_holder->GetRegister(), holder, type);
      ReleaseFpRegister(imm_holder);
      working_stack.push_front(new RegInstr(holder));
    }
      break;
      
    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      RegisterHolder* right_holder = GetFpRegister();
	    move_mem_xreg(left->GetOperand(), FP, right_holder->GetRegister());
      math_xreg_xreg(holder->GetRegister(), right_holder, instruction->GetType());
      ReleaseFpRegister(holder);
      working_stack.push_front(new RegInstr(right_holder));
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* left_holder = GetFpRegister();
      move_mem_xreg(left->GetOperand(), FP, left_holder->GetRegister());

      RegisterHolder* right_holder = GetFpRegister();
      move_mem_xreg(right->GetOperand(), FP, right_holder->GetRegister());
      math_xreg_xreg(right_holder->GetRegister(), left_holder, instruction->GetType());	
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

void JitCompilerA32::move_reg_reg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [mov " << GetRegisterName(dest) 
	  << L", " << GetRegisterName(src) << L"]" << endl;
#endif    
    
    uint32_t op_code = 0xe1a00000;
    
    uint32_t op_dest = dest << 12;
    op_code |= op_dest;
    
    uint32_t op_offset = src;
    op_code |= op_offset;
    
    // encode
    AddMachineCode(op_code);
  }
}

void JitCompilerA32::move_reg_mem(Register src, int32_t offset, Register dest) { 
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [str " << GetRegisterName(src) 
	<< L", (" << GetRegisterName(dest) << L", #" << offset << L")]" << endl;
#endif
  
  uint32_t op_code;
  if(offset >= 0) {
    // forward
    op_code = 0xe5800000;
  }
  else {
    // backward
    op_code = 0xe5000000;
  }
  
  uint32_t op_dest = dest << 16;
  op_code |= op_dest;
  
  uint32_t op_src = src << 12;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::move_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [ldr " << GetRegisterName(dest) 
	<< L", (" << GetRegisterName(src) << L", #" << offset << L")]" << endl;
#endif
  
  uint32_t op_code;
  if(offset >= 0) {
    // forward
    op_code = 0xe5900000;
  }
  else {
    // backward
    op_code = 0xe5100000;    
  }
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset;
  
  // encode
  AddMachineCode(op_code);
}

// --- 8-bit operations ---
void JitCompilerA32::move_reg_mem8(Register src, int32_t offset, Register dest) { 
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [strb " << GetRegisterName(src) 
	      << L", (" << GetRegisterName(dest) << L", #" << offset << L")]" << endl;
#endif
    
  uint32_t op_code;
  if(offset >= 0) {
    // forward
    op_code = 0xe5c00000;
  }
  else {
    // backward
    op_code = 0xe5400000;
  }
  
  uint32_t op_dest = dest << 16;
  op_code |= op_dest;
  
  uint32_t op_src = src << 12;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::move_mem8_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [ldrb " << GetRegisterName(dest) 
	      << L", (" << GetRegisterName(src) << L", #" << offset << L")]" << endl;
#endif
    
  uint32_t op_code;
  if(offset >= 0) {
    // forward
    op_code = 0xe5d00000;
  }
  else {
    // backward
    op_code = 0xe5500000;    
  }
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
  
  uint32_t op_offset = abs(offset);
  op_code |= op_offset;
  
  // encode
  AddMachineCode(op_code);
}
    
void JitCompilerA32::move_imm_mem8(int32_t imm, int32_t offset, Register dest) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  move_reg_mem8(imm_holder->GetRegister(), offset, dest);
  ReleaseRegister(imm_holder); 
}

void JitCompilerA32::move_imm_mem(int32_t imm, int32_t offset, Register dest) { 
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  move_reg_mem(imm_holder->GetRegister(), offset, dest);
  ReleaseRegister(imm_holder); 
}

void JitCompilerA32::move_imm_reg(int32_t imm, Register reg) {
  if(imm < 0 && imm >= -256) {
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [mvn " << GetRegisterName(reg)
	  << L", #" << imm << L"]" << endl;
#endif
    uint32_t op_code = 0xe3e00000;

    uint32_t op_dest = reg << 12;
    op_code |= op_dest;
    op_code |= abs(imm) - 1;

    AddMachineCode(op_code);
  }
  else if(imm <= 255 && imm >= 0) {
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [mov " << GetRegisterName(reg) << L", #" << imm << L"]" << endl;
#endif
    uint32_t op_code = 0xe3a00000;
    
    uint32_t op_dest = reg << 12;
    op_code |= op_dest;
    op_code |= abs(imm);

    AddMachineCode(op_code);
  }
  else {
    move_mem_reg(INT_CONSTS, FP, R8);
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [ldr " << GetRegisterName(reg) << L", #" << imm << L"]" << endl;
#endif
    uint32_t op_code = 0xe5980000;
    
    uint32_t op_dest = reg << 12;
    op_code |= op_dest;
    
    // save code index
    const_int_pool.insert(pair<int32_t, int32_t>(imm, code_index));
        
    // encode
    AddMachineCode(op_code);
  }
}

void JitCompilerA32::move_imm_memx(RegInstr* instr, int32_t offset, Register dest) {
  RegisterHolder* tmp_holder = GetFpRegister();
  move_imm_xreg(instr, tmp_holder->GetRegister());
  move_xreg_mem(tmp_holder->GetRegister(), offset, dest);
  ReleaseFpRegister(tmp_holder);
}

void JitCompilerA32::move_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());  
  move_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::move_mem_xreg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vldr " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code;
  if(offset >= 0) {
    // forward
    op_code = 0xed900b00;
  }
  else {
    // backward
    op_code = 0xed100b00;    
  }
    
  uint32_t op_dest = src << 16;
  op_code |= op_dest;
  
  uint32_t op_src = dest << 12;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset) >> 2;
  op_code |= op_offset;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::move_xreg_mem(Register src, int32_t offset, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vstr %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << endl;
#endif 
  
  uint32_t op_code;
  if(offset >= 0) {
    // forward
    op_code = 0xed800b00;
  }
  else {
    // backward
    op_code = 0xed000b00;
  }
  
  uint32_t op_dest = dest << 16;
  op_code |= op_dest;
  
  uint32_t op_src = src << 12;
  op_code |= op_src;
  
  uint32_t op_offset = abs(offset) >> 2;
  op_code |= op_offset;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::add_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vadd.f64 " << GetRegisterName(dest) 
	      << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code = 0xee300b00;
  
  uint32_t op_src = dest << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
	
  op_code |= src;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::sub_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vsub.f64 " << GetRegisterName(dest) 
	      << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code = 0xee300b40;
  
  uint32_t op_src = dest << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
	
  op_code |= src;
  
  // encode
  AddMachineCode(op_code);    
}

void JitCompilerA32::mul_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vmul.f64 " << GetRegisterName(dest) 
	      << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code = 0xee200b00;
  
  uint32_t op_src = dest << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
	
  op_code |= src;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::div_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [div.f64 " << GetRegisterName(dest) 
	      << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code = 0xee800b00;
  
  uint32_t op_src = dest << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;
	
  op_code |= src;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::add_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  add_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::sub_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  sub_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::div_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  div_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::mul_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  mul_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::math_imm_xreg(RegInstr *instr, RegisterHolder *&reg, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_imm_xreg(instr, reg->GetRegister());
    break;

  case SUB_FLOAT:
    sub_imm_xreg(instr, reg->GetRegister());
    break;

  case MUL_FLOAT:
    mul_imm_xreg(instr, reg->GetRegister());
    break;

  case DIV_FLOAT:
    div_imm_xreg(instr, reg->GetRegister());
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_imm_xreg(instr, reg->GetRegister());
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

void JitCompilerA32::math_mem_xreg(int32_t offset, RegisterHolder* &dest, InstructionType type) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(offset, FP, holder->GetRegister());
  math_xreg_xreg(holder->GetRegister(), dest, type);
  ReleaseFpRegister(holder);
}

void JitCompilerA32::math_xreg_xreg(Register src, RegisterHolder *&dest, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_xreg_xreg(src, dest->GetRegister());
    break;

  case SUB_FLOAT:
    sub_xreg_xreg(src, dest->GetRegister());
    break;

  case MUL_FLOAT:
    mul_xreg_xreg(src, dest->GetRegister());
    break;

  case DIV_FLOAT:
    div_xreg_xreg(src, dest->GetRegister());
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_xreg_xreg(src, dest->GetRegister());
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

void JitCompilerA32::move_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vmov.f64 " << GetRegisterName(dest) 
	      << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code = 0xeeb00b40;
  
  uint32_t op_src = dest << 12;
  op_code |= op_src;
	
  op_code |= src;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::cmp_xreg_xreg(Register src, Register dest) {  
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vcmp.f64 " << GetRegisterName(dest) 
	      << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  
  uint32_t op_code = 0xeeb40b40;
  
  uint32_t op_src = dest << 12;
  op_code |= op_src;
	
  op_code |= src;
  
  // encode
  AddMachineCode(op_code);

  // update flags
#ifdef _DEBUG
      wcout << L"  " << (++instr_count) << L": [vmrs APSR_nzcv, fpscr]" << endl;
#endif
  AddMachineCode(0xeef1fa10);
}

void JitCompilerA32::cmp_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  cmp_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitCompilerA32::and_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [and " << GetRegisterName(dest) 
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  uint32_t op_code = 0xe0000000;
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitCompilerA32::and_imm_reg(int32_t imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  and_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitCompilerA32::and_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  and_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder); 
}

void JitCompilerA32::or_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [orr " << GetRegisterName(dest) 
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  uint32_t op_code = 0xe1800000;
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitCompilerA32::or_imm_reg(int32_t imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  or_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitCompilerA32::or_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  or_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder); 
}

void JitCompilerA32::xor_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [eor " << GetRegisterName(dest) 
        << L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  uint32_t op_code = 0xe0200000;
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitCompilerA32::xor_imm_reg(int32_t imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  xor_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitCompilerA32::xor_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  xor_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder); 
}

void JitCompilerA32::shl_reg_reg(Register src, Register dest)
{
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [lsl " << GetRegisterName(dest) 
       << L", " << GetRegisterName(src) << L"]" << endl;
#endif
  uint32_t op_code = 0xe1a00010;
  
  uint32_t op_src = dest << 12;
  op_code |= op_src;
  
  uint32_t op_dest = src << 8;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;
  
  AddMachineCode(op_code);
}

void JitCompilerA32::shl_mem_reg(int32_t offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shl_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

// TODO: missed this one... ha... will fix... after i boot up the rp4
void JitCompilerA32::shr_imm_reg(int32_t value, Register dest) {
  AddMachineCode(0xc1);
  unsigned char code = 0xe8;
  // RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode(value);
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [asr $" << value << L", %" 
        << GetRegisterName(dest) << L"]" << endl;
#endif
}

void JitCompilerA32::shr_reg_reg(Register src, Register dest)
{
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [asr " << GetRegisterName(dest) 
       << L", " << GetRegisterName(src) << L"]" << endl;
#endif
  uint32_t op_code = 0xe1a00050;
  
  uint32_t op_src = dest << 12;
  op_code |= op_src;
  
  uint32_t op_dest = src << 8;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;
  
  AddMachineCode(op_code);

}

void JitCompilerA32::shr_mem_reg(int32_t offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shr_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitCompilerA32::add_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  add_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitCompilerA32::mul_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  mul_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitCompilerA32::sub_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  sub_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitCompilerA32::div_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetFpRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  div_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseFpRegister(holder);
}

void JitCompilerA32::cmp_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cmp_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::add_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  add_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder); 
}

void JitCompilerA32::add_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [add " << GetRegisterName(dest) 
	<< L", " << GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  uint32_t op_code = 0xe0800000;
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitCompilerA32::add_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [add " << GetRegisterName(reg) << L", "
	      << GetRegisterName(reg)	<< L", #" << imm << L"]" << endl;
#endif
  
  uint32_t op_code = 0xe2800000;
  
  uint32_t op_src = reg << 16;
  op_code |= op_src;
  
  uint32_t op_dest = reg << 12;
  op_code |= op_dest;
  
  uint32_t op_imm = imm;
  op_code |= op_imm;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::add_imm_mem(int32_t imm, int32_t offset, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, dest, mem_holder->GetRegister());
  add_imm_reg(imm, mem_holder->GetRegister());
  move_reg_mem(mem_holder->GetRegister(), offset, dest);
  ReleaseRegister(mem_holder); 
}

void JitCompilerA32::inc_mem(int32_t offset, Register dest) {
  add_imm_mem(1, offset, dest);
}

void JitCompilerA32::dec_mem(int32_t offset, Register dest) {
  sub_imm_mem(1, offset, dest);
}

void JitCompilerA32::dec_reg(Register dest) {
  sub_imm_reg(1, dest);
}

void JitCompilerA32::sub_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  sub_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder); 
}

void JitCompilerA32::sub_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [rsb " << GetRegisterName(dest) << L", "
	<< GetRegisterName(src) << L", " << GetRegisterName(dest) << L"]" << endl;
#endif
  uint32_t op_code = 0xe0600000;
  
  uint32_t op_src = src << 16;
  op_code |= op_src;
  
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;

  op_dest = dest;
  op_code |= op_dest;

  AddMachineCode(op_code);
}

void JitCompilerA32::sub_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [sub " << GetRegisterName(reg) << L", "
	<< GetRegisterName(reg)	<< L", #" << imm << L"]" << endl;
#endif
  
  uint32_t op_code = 0xe2400000;
  
  uint32_t op_src = reg << 16;
  op_code |= op_src;
  
  uint32_t op_dest = reg << 12;
  op_code |= op_dest;
  
  uint32_t op_imm = imm;
  op_code |= op_imm;
  
  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::sub_imm_mem(int32_t imm, int32_t offset, Register dest) {
  RegisterHolder* mem_holder = GetRegister();  
  move_mem_reg(offset, dest, mem_holder->GetRegister());
  sub_imm_reg(imm, mem_holder->GetRegister());
  move_reg_mem(mem_holder->GetRegister(), offset, dest);  
  ReleaseRegister(mem_holder); 
}

void JitCompilerA32::shl_imm_reg(int32_t value, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [lsl " << GetRegisterName(dest) << L", "
	<< GetRegisterName(dest) << L", #" << value << L"]" << endl;
#endif
                                 
  uint32_t op_code = 0xe1a00000;
      
  uint32_t op_dest = dest << 12;
  op_code |= op_dest;

  
  uint32_t op_imm = value << 7;
  op_code |= op_imm;
  
  uint32_t op_src = dest;
  op_code |= op_src;

  // encode
  AddMachineCode(op_code);
}

void JitCompilerA32::mul_imm_reg(int32_t imm, Register reg) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  mul_reg_reg(src_holder->GetRegister(), reg);
  ReleaseRegister(src_holder);
}

void JitCompilerA32::mul_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  mul_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder); 
}

void JitCompilerA32::mul_reg_reg(Register src, Register dest) {
  uint32_t op_code = 0xe0000090;
  
  uint32_t op_dest = dest << 16;
  op_code |= op_dest;
  
  uint32_t op_src = src << 8;
  op_code |= op_src;
  
  op_code |= dest;
  
  AddMachineCode(op_code);
}

void JitCompilerA32::div_imm_reg(int32_t imm, Register reg, bool is_mod) {
  RegisterHolder* src_holder = GetRegister();
  move_imm_reg(imm, src_holder->GetRegister());
  div_reg_reg(src_holder->GetRegister(), reg, is_mod);
  ReleaseRegister(src_holder); 
}

void JitCompilerA32::div_mem_reg(int32_t offset, Register src, Register dest, bool is_mod) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  div_reg_reg(src_holder->GetRegister(), dest, is_mod);
  ReleaseRegister(src_holder);   
}

void JitCompilerA32::div_reg_reg(Register src, Register dest, bool is_mod) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [sdiv " << GetRegisterName(dest) 
	<< L", " << GetRegisterName(dest) << L", " << GetRegisterName(src) << L"]" << endl;
#endif

  RegisterHolder* result_holder = nullptr;

  // sign divide
  uint32_t op_code = 0xe710f010;
  if(is_mod) {
    result_holder = GetRegister();
    uint32_t op_dest = result_holder->GetRegister() << 16;
    op_code |= op_dest;
  }
  else {
    uint32_t op_dest = dest << 16;
    op_code |= op_dest;
  }
  
  uint32_t op_src = src << 8;
  op_code |= op_src;
  
  op_code |= dest;
  
  AddMachineCode(op_code);
  
  // mod from result
  if(is_mod) {
    mul_reg_reg(src, result_holder->GetRegister());
    sub_reg_reg(result_holder->GetRegister(), dest);
    ReleaseRegister(result_holder);
  }
}

void JitCompilerA32::cmp_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [cmp/cmn " << GetRegisterName(reg) << L", " << imm << L"]" << endl;
#endif
  
  if (imm < 0 && imm >= -256) {
    uint32_t op_code = 0xe3700000;
    
    uint32_t op_dest = reg << 16;
    op_code |= op_dest;
  
    op_code |= abs(imm);
  
    AddMachineCode(op_code);
  }
  else if(imm <= 255 && imm >= 0) {
    uint32_t op_code = 0xe3500000;
    
    uint32_t op_dest = reg << 16;
    op_code |= op_dest;
  
    op_code |= abs(imm);
    
    AddMachineCode(op_code);
  }
  else {
    RegisterHolder* src_holder = GetRegister();
    move_imm_reg(imm, src_holder->GetRegister());
    cmp_reg_reg(src_holder->GetRegister(), reg);
    ReleaseRegister(src_holder);
  }
}

void JitCompilerA32::cmp_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* src_holder = GetRegister();
  move_mem_reg(offset, src, src_holder->GetRegister());
  cmp_reg_reg(src_holder->GetRegister(), dest);
  ReleaseRegister(src_holder);
}

void JitCompilerA32::cmp_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [cmp " << GetRegisterName(dest) 
       << L", " << GetRegisterName(src) << L"]" << endl;
#endif
  uint32_t op_code = 0xe1500000;
  
  uint32_t op_dest = dest << 16;
  op_code |= op_dest;
  
  op_code |= src;
  
  AddMachineCode(op_code);
}

bool JitCompilerA32::cond_jmp(InstructionType type) {
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
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [blt]" << std::endl;
#endif
        AddMachineCode(0xba000000);
        break;

      case GTR_INT:
      case GTR_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [bgt]" << std::endl;
#endif
        AddMachineCode(0xca000000);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [beq]" << std::endl;
#endif
        AddMachineCode(0x0a000000);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [bne]" << std::endl;
#endif
        AddMachineCode(0x1a000000);
        break;

      case LES_EQL_INT:
      case LES_EQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [ble]" << std::endl;
#endif
        AddMachineCode(0xda000000);
        break;
        
      case GTR_EQL_INT:
      case GTR_EQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [bge]" << std::endl;
#endif
        AddMachineCode(0xaa000000);
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
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [bge]" << std::endl;
#endif
        AddMachineCode(0xaa000000);
        break;

      case GTR_INT:
      case GTR_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [ble]" << std::endl;
#endif
        AddMachineCode(0xda000000);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [bne]" << std::endl;
#endif
        AddMachineCode(0x1a000000);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [beq]" << std::endl;
#endif
        AddMachineCode(0x0a000000);
        break;

      case LES_EQL_INT:
      case LES_EQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [bgt]" << std::endl;
#endif
        AddMachineCode(0xca000000);
        break;
        
      case GTR_EQL_INT:
      case GTR_EQL_FLOAT:
#ifdef _DEBUG
        std::wcout << L"  " << (++instr_count) << L": [blt]" << std::endl;
#endif
        AddMachineCode(0xba000000);
        break;
				
      default:
        break;
      }  
    }    
    // store update index
    jump_table.insert(pair<int32_t, StackInstr*>(code_index, next_instr));
    
    // temp offset
    skip_jump = true;
    return true;
  }
  
  return false;
}

void JitCompilerA32::math_imm_reg(int32_t imm, Register reg, InstructionType type) {
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

void JitCompilerA32::math_reg_reg(Register src, Register dest, InstructionType type) {
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
    mul_reg_reg(dest, src);
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

void JitCompilerA32::math_mem_reg(int32_t offset, Register reg, InstructionType type) {
  switch(type) {
  case SHL_INT:
    shl_mem_reg(offset, FP, reg);
    break;

  case SHR_INT:
    shr_mem_reg(offset, FP, reg);
    break;
    
  case AND_INT:
    and_mem_reg(offset, FP, reg);
    break;

  case OR_INT:
    or_mem_reg(offset, FP, reg);
    break;
    
  case ADD_INT:
    add_mem_reg(offset, FP, reg);
    break;

  case SUB_INT:
    sub_mem_reg(offset, FP, reg);
    break;

  case MUL_INT:
    mul_mem_reg(offset, FP, reg);
    break;

  case DIV_INT:
    div_mem_reg(offset, FP, reg, false);
    break;
    
  case MOD_INT:
    div_mem_reg(offset, FP, reg, true);
    break;

  case BIT_AND_INT:
    and_mem_reg(offset, FP, reg);
    break;

  case BIT_OR_INT:
    or_mem_reg(offset, FP, reg);
    break;

  case BIT_XOR_INT:
    xor_mem_reg(offset, FP, reg);
    break;
    
  case LES_INT:
  case LES_EQL_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:  
  case GTR_EQL_INT:
    cmp_mem_reg(offset, FP, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;

  default:
    break;
  }
}

// --- function calls ---
void JitCompilerA32::call_reg(Register reg) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [blx %" << GetRegisterName(reg) << L"]" << endl;
#endif
  
  move_reg_mem(LR, TMP_REG_LR, FP);

  uint32_t op_code = 0xe12fff30;
  op_code |= reg;
  AddMachineCode(op_code);
  
  move_mem_reg(TMP_REG_LR, FP, LR);
}

void JitCompilerA32::cmov_reg(Register reg, InstructionType oper)
{
  uint32_t op_code, op_dest;
  
  switch (oper) {
  case LES_INT:
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [movle " << GetRegisterName(reg) << L", #1]" << endl;
#endif
    op_code = 0xd3a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [movgt " << GetRegisterName(reg) << L", #0]" << endl;
#endif
    op_code = 0xc3a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;
    
  case LES_FLOAT:
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [movmi " << GetRegisterName(reg) << L", #1]" << endl;
#endif
    op_code = 0x43a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [movpl " << GetRegisterName(reg) << L", #0]" << endl;
#endif
    op_code = 0x53a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;
    
  case GTR_INT:
  case GTR_FLOAT:
#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [movgt " << GetRegisterName(reg) << L", #1]" << endl;
#endif
    op_code = 0xc3a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    wcout << L"  " << (++instr_count) << L": [movle " << GetRegisterName(reg) << L", #0]" << endl;
#endif
    op_code = 0xd3a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;
    
  case EQL_INT:
  case EQL_FLOAT:
#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [moveq]" << std::endl;
#endif
    op_code = 0x03a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movne]" << std::endl;
#endif
    op_code = 0x13a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;
        
  case NEQL_INT:
  case NEQL_FLOAT:
#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movne]" << std::endl;
#endif
    op_code = 0x13a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [moveq]" << std::endl;
#endif
    op_code = 0x03a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;
    
  case LES_EQL_INT:
#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movle]" << std::endl;
#endif
    op_code = 0xd3a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movgt]" << std::endl;
#endif
    op_code = 0xc3a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;

  case LES_EQL_FLOAT:
#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movls]" << std::endl;
#endif
    op_code = 0x93a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movhi]" << std::endl;
#endif
    op_code = 0x83a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;
        
  case GTR_EQL_INT:
#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movgt]" << std::endl;
#endif
    op_code = 0xc3a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movle]" << std::endl;
#endif
    op_code = 0xd3a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;

  case GTR_EQL_FLOAT:
#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movge]" << std::endl;
#endif
    op_code = 0xa3a00001;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);

#ifdef _DEBUG
    std::wcout << L"  " << (++instr_count) << L": [movlt]" << std::endl;
#endif
    op_code = 0xb3a00000;
    op_dest = reg << 12;
    op_code |= op_dest;
    AddMachineCode(op_code);
    break;

  default:
    break;
  }
}

void JitCompilerA32::ProcessFloatOperation(StackInstr* instruction)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
#ifdef _DEBUG
  assert(left->GetType() == MEM_FLOAT);
#endif
  
  // save D0, if needed
  RegisterHolder *holder = GetFpRegister();
  if(holder->GetRegister() != D0) {
    move_xreg_mem(D0, TMP_D_0, FP);
  }
  
  // load D0
  move_mem_xreg(left->GetOperand(), FP, D0);
  
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

  case SQRT_FLOAT:
    func_ptr = sqrt;
    break;

  case ASIN_FLOAT:
    func_ptr = asin;
    break;

  case ACOS_FLOAT:
    func_ptr = acos;
    break;

  case LOG_FLOAT:
    func_ptr = log;
    break;
      
  default:
    throw runtime_error("Invalid function call!");
    break;
  }
  
  // call function
  move_reg_mem(R8, TMP_REG_0, FP);
  move_imm_reg((uint32_t)func_ptr, R8);
  call_reg(R8);
  move_mem_reg(TMP_REG_0, FP, R8);
  
  // get return and restore D0, if needed
  move_xreg_xreg(D0, holder->GetRegister());
  if(holder->GetRegister() != D0) {
    move_mem_xreg(TMP_D_0, FP, D0);
  }
  working_stack.push_front(new RegInstr(holder));  
  
  delete left;
  left = nullptr;
}

void JitCompilerA32::ProcessFloatOperation2(StackInstr* instruction)
{
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
#ifdef _DEBUG
  assert(left->GetType() == MEM_FLOAT);
#endif
  
  // save D0, if needed
  RegisterHolder *holder = GetFpRegister();
  if(holder->GetRegister() != D0) {
    move_xreg_mem(D0, TMP_D_0, FP);
  }
  move_xreg_mem(D1, TMP_D_1, FP);
   
  // load D0
  move_mem_xreg(right->GetOperand(), FP, D0);
  move_mem_xreg(left->GetOperand(), FP, D1);
  
  // choose function
  double(*func_ptr)(double, double);
  switch (type) {
    case ATAN2_FLOAT:
      func_ptr = atan2;
      break;
    
    case POW_FLOAT:
      func_ptr = pow;
      break;
      
    default:
      throw runtime_error("Invalid function call!");
      break;
  }
  
  // call function
  move_reg_mem(R8, TMP_REG_0, FP);
  move_imm_reg((uint32_t)func_ptr, R8);
  call_reg(R8);
  move_mem_reg(TMP_REG_0, FP, R8);
  
  // get return and restore D0, if needed
  move_xreg_xreg(D0, holder->GetRegister());
  move_mem_xreg(TMP_D_1, FP, D1);
  if(holder->GetRegister() != D0) {
    move_mem_xreg(TMP_D_0, FP, D0);
  }
  working_stack.push_front(new RegInstr(holder));  
  
  delete left;
  left = nullptr;
  
  delete right;
  right = nullptr;
}

// --- push/pop cpu stack ---
void JitCompilerA32::push_mem(int32_t offset, Register dest) {
  throw runtime_error("Method 'push_mem(..)' not implemented for ARM32 target");
}

void JitCompilerA32::push_reg(Register reg) {
  throw runtime_error("Method 'push_reg(..)' not implemented for ARM32 target");
}

void JitCompilerA32::push_imm(int32_t value) {
  throw runtime_error("Method 'push_imm(..)' not implemented for ARM32 target");
}

void JitCompilerA32::pop_reg(Register reg) {
  throw runtime_error("Method 'pop_reg(..)' not implemented for ARM32 target");
}

void JitCompilerA32::vcvt_imm_reg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  vcvt_mem_reg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::vcvt_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  vcvt_reg_xreg(imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::vcvt_reg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vcvt.f64.s32 %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  uint32_t op_code = 0xee070a90;
  uint32_t op_dest = src << 12;
  op_code |= op_dest;
  AddMachineCode(op_code);
  
  op_code = 0xeeb80be7;
  op_dest = dest << 12;
  op_code |= op_dest; 
  AddMachineCode(op_code);
}

void JitCompilerA32::vcvt_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  vcvt_reg_xreg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitCompilerA32::vcvt_xreg_reg(Register src, Register dest) {  
#ifdef _DEBUG
  wcout << L"  " << (++instr_count) << L": [vcvt.s32.f64 %" << GetRegisterName(dest) 
        << L", %" << GetRegisterName(src) << L"]" << endl;
#endif
  uint32_t op_code = 0xeefd7bc0;
  op_code |= src;
  AddMachineCode(op_code);
  
  op_code = 0xee170a90;
  const uint32_t op_dest = dest << 12;
  op_code |= op_dest;
  AddMachineCode(op_code);
}

void JitCompilerA32::vcvt_mem_reg(int32_t offset, Register src, Register dest) {
  RegisterHolder* mem_holder = GetFpRegister();
  move_mem_xreg(offset, src, mem_holder->GetRegister());
  vcvt_xreg_reg(mem_holder->GetRegister(), dest);
  ReleaseFpRegister(mem_holder);
}

void JitCompilerA32::round_imm_xreg(RegInstr* instr, Register reg, bool is_floor) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  round_xreg_xreg(imm_holder->GetRegister(), reg, is_floor);
  ReleaseRegister(imm_holder);
}

void JitCompilerA32::round_mem_xreg(int32_t offset, Register src, Register dest, bool is_floor) {
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  round_xreg_xreg(mem_holder->GetRegister(), dest, is_floor);
  ReleaseRegister(mem_holder);
}

void JitCompilerA32::round_xreg_xreg(Register src, Register dest, bool is_floor) {
  throw runtime_error("Method 'round_xreg_xreg(..)' not implemented for ARM32 target");
}

//
// Get register name
//
std::wstring JitCompilerA32::GetRegisterName(Register reg)
{
	switch(reg) {
	case R0:
		return L"r0/d0";

	case R1:
  	return L"r1/d1";

	case R2:
  	return L"r2/d2";

	case R3:
  	return L"r3/d3";
		
	case R4:
  	return L"r4/d4";
	
	case R5:
  	return L"r5/d5";
		
	case R6:
  	return L"r6/d6";
		
	case R7:
  	return L"r7/d7";	
		
	case R8:
  	return L"r8/d8";
		
	case R9:
  	return L"r9/d9";
				
	case R10:
  	return L"r10/d10";
			
	case SP:
  	return L"sp/d11";
		
	case R12:
  	return L"r12/d12";

	case FP:
  	return L"fp/d13";

	case LR:
  	return L"r14/d14";

	case R15:
  	return L"r15/d15";
	}
  
	return L"unknown";
}

//
// callback to stack interpreter
//
void JitCompilerA32::JitStackCallback(const int32_t instr_id, StackInstr* instr, const int32_t cls_id, const int32_t mthd_id, int32_t* inst, size_t* op_stack, int32_t* stack_pos, StackFrame** call_stack, long* call_stack_pos, const int32_t ip)
{
#ifdef _DEBUG
  wcout << L"Stack Call: instr=" << instr_id << L", oper_1=" << instr->GetOperand() << L", oper_2=" 
        << instr->GetOperand2()<< L", oper_3=" << instr->GetOperand3() << L", self=" << inst 
        << L"(" << (size_t)inst << L"), stack=" << op_stack << L", stack_addr=" << stack_pos 
        << L", stack_pos=" << (*stack_pos) << endl;
#endif
  switch(instr_id) {
  case MTHD_CALL:
  case DYN_MTHD_CALL: {
#ifdef _DEBUG
    wcout << L"jit oper: MTHD_CALL: cls=" << instr->GetOperand() << L", mthd=" << instr->GetOperand2() << endl;
#endif
    StackInterpreter intpr(call_stack, call_stack_pos);
    intpr.Execute(op_stack, (long*)stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), (size_t*)inst, true);
  }
    break;

  case LOAD_ARY_SIZE: {
    size_t* array = (size_t*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
    PushInt(op_stack, stack_pos, array[2]);
  }
    break;

  case NEW_BYTE_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    size++;
    int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(int32_t)), BYTE_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size - 1;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(int32_t));
    PushInt(op_stack, stack_pos, (int32_t)mem);

#ifdef _DEBUG
    wcout << L"jit oper: NEW_BYTE_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
#endif
  }
     break;

  case NEW_CHAR_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    size++;
    int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(int32_t)),
                                                          CHAR_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size - 1;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(int32_t));
    PushInt(op_stack, stack_pos, (int32_t)mem);

#ifdef _DEBUG
    wcout << L"jit oper: NEW_CHAR_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
#endif
  }
     break;

  case NEW_INT_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG
    wcout << L"jit oper: NEW_INT_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
#endif
    mem[0] = size;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(int32_t));
    PushInt(op_stack, stack_pos, (int32_t)mem);
  }
    break;

  case NEW_FLOAT_ARY: {
    size_t indices[8];
    size_t value = PopInt(op_stack, stack_pos);
    size_t size = value;
    indices[0] = value;
    long dim = 1;
    for(long i = 1; i < instr->GetOperand(); ++i) {
      size_t value = PopInt(op_stack, stack_pos);
      size *= value;
      indices[dim++] = value;
    }

    size *= 2;
    int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + dim + 2, INT_TYPE, op_stack, *stack_pos);
#ifdef _DEBUG
    wcout << L"jit oper: NEW_FLOAT_ARY: dim=" << dim << L"; size=" << size
      << L"; index=" << (*stack_pos) << L"; mem=" << mem << endl;
#endif
    mem[0] = size / 2;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(int32_t));
    PushInt(op_stack, stack_pos, (int32_t)mem);
  }
    break;

  case NEW_OBJ_INST: {
#ifdef _DEBUG
    wcout << L"jit oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl;
#endif
    int32_t* mem = (int32_t*)MemoryManager::AllocateObject(instr->GetOperand(),
                                                           op_stack, *stack_pos);
    PushInt(op_stack, stack_pos, (int32_t)mem);
  }
   break;

  case I2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if (str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const size_t base = PopInt(op_stack, stack_pos);
      const long value = (long)PopInt(op_stack, stack_pos);

      wstringstream stream;
      if (base == 16) {
        stream << std::hex << value;
        wstring conv(stream.str());
        const size_t max = conv.size() < 16 ? conv.size() : 16;
#ifdef _WIN32
        wcsncpy_s(str, str_ptr[0], conv.c_str(), max);
#else
        wcsncpy(str, conv.c_str(), max);
#endif
      }
      else {
        stream << value;
        wstring conv(stream.str());
        const size_t max = conv.size() < 16 ? conv.size() : 16;
#ifdef _WIN32
        wcsncpy_s(str, str_ptr[0], conv.c_str(), max);
#else
        wcsncpy(str, conv.c_str(), max);
#endif
      }
    }
  }
    break;

  case F2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const double value = PopFloat(op_stack, stack_pos);
    
      wstringstream stream;
      stream << value;
      wstring conv(stream.str());
      const size_t max = conv.size() < 16 ? conv.size() : 16; 
#ifdef _WIN32
      wcsncpy_s(str, str_ptr[0], conv.c_str(), max);
#else
      wcsncpy(str, conv.c_str(), max);
#endif
    }
  }
    break;

  case S2F: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      wstringstream stream(str);
      FLOAT_VALUE value;
      stream >> value;
      PushFloat(value, op_stack, stack_pos);
    }
    else {
      wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << endl;
      exit(1);
    }
  }
    break;
    
  case S2I: {
#ifdef _DEBUG
    wcout << L"stack oper: S2I; call_pos=" << (*call_stack_pos) << endl;
#endif

    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    long base = (long)PopInt(op_stack, stack_pos);
    if (str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      try {
        if (wcslen(str) > 2) {
          switch (str[1]) {
            // binary
          case 'b':
            PushInt(op_stack, stack_pos, stoi(str + 2, nullptr, 2));
            return;

            // octal
          case 'o':
            PushInt(op_stack, stack_pos, stoi(str + 2, nullptr, 8));
            return;

            // hexadecimal
          case 'x':
            PushInt(op_stack, stack_pos, stoi(str + 2, nullptr, 16));
            return;

          default:
            break;
          }
        }
        PushInt(op_stack, stack_pos, stoi(str, nullptr, base));
      }
      catch (std::invalid_argument & e) {
#ifdef _WIN32    
        UNREFERENCED_PARAMETER(e);
#endif
        PushInt(op_stack, stack_pos, 0);
      }
    }
    else {
      wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << endl;
      exit(1);
    }
  }
    break;

  case OBJ_TYPE_OF: {
    size_t* mem = (size_t*)PopInt(op_stack, stack_pos);
    size_t* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(),
                                                    program->GetHierarchy(),
                                                    program->GetInterfaces());
    if(result) {
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case OBJ_INST_CAST: {
    int32_t* mem = (int32_t*)PopInt(op_stack, stack_pos);
    int32_t to_id = instr->GetOperand();
#ifdef _DEBUG
    wcout << L"jit oper: OBJ_INST_CAST: from=" << mem << L", to=" << to_id << endl;
#endif	
    int32_t result = (int32_t)MemoryManager::ValidObjectCast((size_t*)mem, to_id,
                                                             program->GetHierarchy(), program->GetInterfaces());
    if(!result && mem) {
      StackClass* to_cls = MemoryManager::GetClass((size_t*)mem);
      wcerr << L">>> Invalid object cast: '" << (to_cls ? to_cls->GetName() : L"?")
        << L"' to '" << program->GetClass(to_id)->GetName() << L"' <<<" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
    PushInt(op_stack, stack_pos, result);
  }
    break;

  //----------- threads -----------

  case THREAD_JOIN: {
    int32_t* instance = inst;
    if(!instance) {
      wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
    void* status;
    pthread_t vm_thread = (pthread_t)instance[0];
    if(pthread_join(vm_thread, &status)) {
      wcerr << L"Unable to join thread!" << endl;
      exit(-1);
    }
  }
    break;

  case THREAD_SLEEP:
    usleep(PopInt(op_stack, stack_pos) * 1000);
    break;

  case THREAD_MUTEX: {
    int32_t* instance = inst;
    if(!instance) {
      wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
    pthread_mutex_init((pthread_mutex_t*)& instance[1], nullptr);
  }
     break;

  case CRITICAL_START: {
    int32_t* instance = (int32_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
    pthread_mutex_lock((pthread_mutex_t*)& instance[1]);
  }
     break;

  case CRITICAL_END: {
    int32_t* instance = (int32_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      wcerr << L"Atempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
    pthread_mutex_unlock((pthread_mutex_t*)& instance[1]);
  }
    break;

    // ---------------- memory copy ----------------
  case CPY_BYTE_ARY: {
    long length = PopInt(op_stack, stack_pos);;
    const long src_offset = PopInt(op_stack, stack_pos);;
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
    const long dest_offset = PopInt(op_stack, stack_pos);;
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

    if(!src_array || !dest_array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }

    const long src_array_len = src_array[2];
    const long dest_array_len = dest_array[2];
    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      unsigned char* src_array_ptr = (unsigned char*)(src_array + 3);
      unsigned char* dest_array_ptr = (unsigned char*)(dest_array + 3);
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length);
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_CHAR_ARY: {
    long length = PopInt(op_stack, stack_pos);;
    const long src_offset = PopInt(op_stack, stack_pos);;
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
    const long dest_offset = PopInt(op_stack, stack_pos);;
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

    if(!src_array || !dest_array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }

    const long src_array_len = src_array[2];
    const long dest_array_len = dest_array[2];

    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      wchar_t* src_array_ptr = (wchar_t*)(src_array + 3);
      wchar_t* dest_array_ptr = (wchar_t*)(dest_array + 3);
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(wchar_t));
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
     break;

  case CPY_INT_ARY: {
    long length = PopInt(op_stack, stack_pos);;
    const long src_offset = PopInt(op_stack, stack_pos);;
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
    const long dest_offset = PopInt(op_stack, stack_pos);;
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

    if(!src_array || !dest_array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }

    const long src_array_len = src_array[0];
    const long dest_array_len = dest_array[0];
    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      size_t* src_array_ptr = src_array + 3;
      size_t* dest_array_ptr = dest_array + 3;
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(size_t));
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case CPY_FLOAT_ARY: {
    long length = PopInt(op_stack, stack_pos);;
    const long src_offset = PopInt(op_stack, stack_pos);;
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);;
    const long dest_offset = PopInt(op_stack, stack_pos);;
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);;

    if(!src_array || !dest_array) {
      wcerr << L">>> Atempting to dereference a 'Nil' memory instance <<<" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }

    const long src_array_len = src_array[0];
    const long dest_array_len = dest_array[0];
    if(length > 0 && src_offset + length <= src_array_len && dest_offset + length <= dest_array_len) {
      size_t* src_array_ptr = src_array + 3;
      size_t* dest_array_ptr = dest_array + 3;
      memcpy(dest_array_ptr + dest_offset, src_array_ptr + src_offset, length * sizeof(FLOAT_VALUE));
      PushInt(op_stack, stack_pos, 1);
    }
    else {
      PushInt(op_stack, stack_pos, 0);
    }
  }
    break;

  case TRAP:
  case TRAP_RTRN: {
    size_t* stack = (size_t*)op_stack;
    long* pos = (long*)stack_pos;
    if(!TrapProcessor::ProcessTrap(program, (size_t*)inst, stack, pos, nullptr)) {
      wcerr << L"  JIT compiled machine code..." << endl;
      exit(1);
    }
  }
     break;

#ifdef _DEBUG
  default:
    wcerr << L"Unknown callback!" << endl;
    break;

    wcout << L"  ending stack: pos=" << (*stack_pos) << endl;
#endif
  }
}

//
// calculate array index
//
RegisterHolder* JitCompilerA32::ArrayIndex(StackInstr* instr, MemoryType type)
{
  RegInstr* holder = working_stack.front();
  working_stack.pop_front();

  RegisterHolder* array_holder;
  switch(holder->GetType()) {
  case IMM_INT:
    wcerr << L">>> trying to index a constant! <<<" << endl;
    exit(1);
    break;

  case REG_INT:
    array_holder = holder->GetRegister();
    break;

  case MEM_INT:
    array_holder = GetRegister();
    move_mem_reg(holder->GetOperand(), FP, array_holder->GetRegister());
    break;

  default:
    wcerr << L"internal error" << endl;
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
    move_mem_reg(holder->GetOperand(), FP, index_holder->GetRegister());
    break;

  default:
    wcerr << L"internal error" << endl;
    exit(1);
    break;
  }

  const int32_t dim = instr->GetOperand();
  for(int i = 1; i < dim; ++i) {
    // index *= array[i];
    mul_mem_reg((i + 2) * sizeof(int32_t), array_holder->GetRegister(),
                index_holder->GetRegister());
    if(holder) {
      delete holder;
      holder = nullptr;
    }

    holder = working_stack.front();
    working_stack.pop_front();
    switch(holder->GetType()) {
    case IMM_INT:
      add_imm_reg(holder->GetOperand(), index_holder->GetRegister());
      break;

    case REG_INT:
      add_reg_reg(holder->GetRegister()->GetRegister(),
                  index_holder->GetRegister());
      break;

    case MEM_INT:
      add_mem_reg(holder->GetOperand(), FP, index_holder->GetRegister());
      break;

    default:
      wcerr << L"internal error" << endl;
      exit(1);
      break;
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
  case INT_TYPE:
    shl_imm_reg(2, index_holder->GetRegister());
    shl_imm_reg(2, bounds_holder->GetRegister());
    break;

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
  add_imm_reg((instr->GetOperand() + 2) * sizeof(int32_t), index_holder->GetRegister());
  add_reg_reg(index_holder->GetRegister(), array_holder->GetRegister());
  ReleaseRegister(index_holder);

  delete holder;
  holder = nullptr;

  return array_holder;
}

void JitCompilerA32::ProcessIndices()
{
#ifdef _DEBUG
  wcout << L"Calculating indices for variables..." << endl;
#endif
  multimap<int32_t, StackInstr*> values;
  for(int32_t i = 0; i < method->GetInstructionCount(); ++i) {
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
      values.insert(pair<int32_t, StackInstr*>(instr->GetOperand(), instr));
      break;

    default:
      break;
    }
  }

  int32_t index = TMP_REG_LR;
  int32_t last_id = -1;
  multimap<int32_t, StackInstr*>::iterator value;
  for(value = values.begin(); value != values.end(); ++value) {
    int32_t id = value->first;
    StackInstr* instr = (*value).second;
    // instance reference
    if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
      // note: all instance variables are allocated in 4-byte blocks,
      // for float_consts the assembler allocates 2 4-byte blocks
      instr->SetOperand3(instr->GetOperand() * sizeof(int32_t));
    }
    // local reference
    else {
      // note: all local variables are allocated in 4 or 8 bytes
      // blocks depending upon type
      if(last_id != id) {
        if(instr->GetType() == LOAD_LOCL_INT_VAR ||
           instr->GetType() == LOAD_CLS_INST_INT_VAR ||
           instr->GetType() == STOR_LOCL_INT_VAR ||
           instr->GetType() == STOR_CLS_INST_INT_VAR ||
           instr->GetType() == COPY_LOCL_INT_VAR ||
           instr->GetType() == COPY_CLS_INST_INT_VAR) {
          index -= sizeof(int32_t);
        }
        else if(instr->GetType() == LOAD_FUNC_VAR ||
                instr->GetType() == STOR_FUNC_VAR) {
          index -= sizeof(int32_t) * 2;
        }
        else {
          index -= sizeof(double);
        }
      }
      instr->SetOperand3(index);
      last_id = id;
    }
#ifdef _DEBUG
    if(instr->GetOperand2() == INST || instr->GetOperand2() == CLS) {
      wcout << L"native memory: index=" << instr->GetOperand() << L"; jit index="
        << instr->GetOperand3() << endl;
    }
    else {
      wcout << L"native stack: index=" << instr->GetOperand() << L"; jit index="
        << instr->GetOperand3() << endl;
    }
#endif
  }
  
  // calculate local space (adjust for alignment)
  local_space = -(index + TMP_REG_LR);
  realign_stack = false;
  if(local_space % 8 != 0) {
    local_space += 4;
    realign_stack = true;
  }
    
#ifdef _DEBUG
  wcout << L"Local space required: " << local_space << L" byte(s)" << endl;
#endif
}

//
// translate bytecode to machine code
//
bool JitCompilerA32::Compile(StackMethod* cm)
{
  compile_success = true;

  if(!cm->GetNativeCode()) {
    skip_jump = false;
    method = cm;

    int32_t cls_id = method->GetClass()->GetId();
    int32_t mthd_id = method->GetId();
#ifdef _DEBUG
    wcout << L"---------- Compiling Native Code: method_id=" << cls_id << L","
      << mthd_id << L"; mthd_name='" << method->GetName() << L"'; params="
      << method->GetParamCount() << L" ----------" << endl;
#endif

    code = (uint32_t*)malloc(BUFFER_SIZE);
    code_buf_max = BUFFER_SIZE;
    
    ints = new int32_t[MAX_INTS];
    float_consts = new double[MAX_DBLS];
    
    local_space = floats_index = instr_index = code_index = epilog_index = instr_count = 0;
    // general use registers
    aval_regs.push_back(new RegisterHolder(R3, false));
    aval_regs.push_back(new RegisterHolder(R2, false));
    aval_regs.push_back(new RegisterHolder(R1, false));
    aval_regs.push_back(new RegisterHolder(R0, false));
    // aux general use registers
    aux_regs.push(new RegisterHolder(R7, false));
    aux_regs.push(new RegisterHolder(R6, false));	
    aux_regs.push(new RegisterHolder(R5, false));
    aux_regs.push(new RegisterHolder(R4, false));
    // floating point registers
    aval_xregs.push_back(new RegisterHolder(D7, true));
    aval_xregs.push_back(new RegisterHolder(D6, true));
    aval_xregs.push_back(new RegisterHolder(D5, true));
    aval_xregs.push_back(new RegisterHolder(D4, true)); 
    aval_xregs.push_back(new RegisterHolder(D3, true));
    aval_xregs.push_back(new RegisterHolder(D2, true)); 
    aval_xregs.push_back(new RegisterHolder(D1, true));
    aval_xregs.push_back(new RegisterHolder(D0, true));
#ifdef _DEBUG
    wcout << L"Compiling code for AARCH32 architecture..." << endl;
#endif
    
    // process offsets
    ProcessIndices();
    // setup
    Prolog();
    // method information
    move_imm_mem(cls_id, CLS_ID, FP);
    move_imm_mem(mthd_id, MTHD_ID, FP);
    
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
    unordered_map<int32_t, StackInstr*>::iterator jmp_iter;
    for(jmp_iter = jump_table.begin(); jmp_iter != jump_table.end(); ++jmp_iter) {
      StackInstr* instr = jmp_iter->second;
      const int32_t src_offset = jmp_iter->first - 1;
      const int32_t dest_index = method->GetLabelIndex(instr->GetOperand());
      const int32_t dest_offset = method->GetInstruction(dest_index)->GetOffset();
      const int32_t offset = dest_offset - src_offset - 2;
      if(offset < 0) {
        code[src_offset] |= offset & 0x00ffffff;
      }
      else {
        code[src_offset] |= offset;
      }
#ifdef _DEBUG
      wcout << L"jump update: src=" << src_offset << L"; dest=" << dest_offset << endl;
#endif
    }
    
    // update error return codes
    for(size_t i = 0; i < deref_offsets.size(); ++i) {
      const int32_t index = deref_offsets[i] - 1;
      int32_t offset = epilog_index - index - 2 + 1;
      code[index] |= offset;
    }

    for(size_t i = 0; i < bounds_less_offsets.size(); ++i) {
      const int32_t index = bounds_less_offsets[i] - 1;
      int32_t offset = epilog_index - index - 2 + 5;
      code[index] |= offset;
    }

    for(size_t i = 0; i < bounds_greater_offsets.size(); ++i) {
      const int32_t index = bounds_greater_offsets[i]  - 1;
      int32_t offset = epilog_index - index - 2 + 3;
      code[index] |= offset;
    }
    
    // update consts pools
    int ints_index = 0;
    unordered_map<int32_t, int32_t> int_pool_cache;
    multimap<int32_t, int32_t>::iterator int_pool_iter = const_int_pool.begin();
    for(; int_pool_iter != const_int_pool.end(); ++int_pool_iter) {
      const int32_t const_value = int_pool_iter->first;
      const int32_t src_offset = int_pool_iter->second;
      const int32_t offset = ints_index * sizeof(int32_t);
      
      // 12-bit max for ldr offset
      if(offset >= PAGE_SIZE * (int32_t)sizeof(uint32_t)) {
        free(code);
        code = nullptr;
        
        delete[] ints;
        ints = nullptr;

        delete[] float_consts;
        float_consts = nullptr;

        return false;
      }

#ifdef _DEBUG
      assert(ints_index < MAX_INTS);
      assert(offset < 4096); // max that can addressed in 12-bits
#endif
      
      unordered_map<int32_t, int32_t>::iterator int_pool_found = int_pool_cache.find(const_value);
      if(int_pool_found != int_pool_cache.end()) {
        code[src_offset] |= int_pool_found->second;
      }
      else {
        ints[ints_index++] = const_value;
        code[src_offset] |= offset;
        int_pool_cache.insert(pair<int32_t, int32_t>(const_value, offset));
      }
    }
    
#ifdef _DEBUG
    wcout << L"------------------------" << endl;
    wcout << L"int const pool: size=" << int_pool_cache.size() << L" [" 
          << int_pool_cache.size() * sizeof(int32_t) << L" of " << sizeof(int32_t) * MAX_INTS << L" byte(s)]" << endl;
    wcout << L"Caching JIT code: actual=" << code_index << L", buffer=" << code_buf_max << L" byte(s)" << endl;
#endif
    // store compiled code
    method->SetNativeCode(new NativeCode(page_manager->GetPage(code, code_index), code_index, ints, float_consts));
    
    free(code);
    code = nullptr;
    
    compile_success = true;
  }

  return compile_success;
}

/********************************
 * JitExecutor class
 ********************************/
StackProgram* JitExecutor::program;

void JitExecutor::Initialize(StackProgram* p) 
{
  program = p;
}

// Executes machine code
long JitExecutor::Execute(StackMethod* method, size_t* inst, size_t* op_stack, long* stack_pos, StackFrame** call_stack, long* call_stack_pos, StackFrame* frame)
{
  const int32_t cls_id = method->GetClass()->GetId();
  const int32_t mthd_id = method->GetId();
  NativeCode* native_code = method->GetNativeCode();
  int32_t* int_consts = native_code->GetInts();

#ifdef _DEBUG
  const long code_index = native_code->GetSize();
  wcout << L"=== MTHD_CALL (native): id=" << cls_id << L"," << mthd_id << L"; name='" << method->GetName()
        << L"'; self=" << inst << L"(" << (size_t)inst << L"); stack=" << op_stack << L"; stack_pos="
        << (*stack_pos) << L"; params=" << method->GetParamCount() << L"; code=" << (size_t *)native_code->GetCode() << L"; code_index="
        << native_code->GetSize() << L" ===" << endl;
  assert((*stack_pos) >= method->GetParamCount());
#endif

  // create function
  jit_fun_ptr jit_fun = (jit_fun_ptr)native_code->GetCode();
  
  // execute
  const int32_t rtrn_value = jit_fun(cls_id, mthd_id, method->GetClass()->GetClassMemory(), inst, op_stack, stack_pos,
                                     call_stack, call_stack_pos, &(frame->jit_mem), &(frame->jit_offset), int_consts);

#ifdef _DEBUG
  wcout << L"JIT return: " << rtrn_value << endl;
#endif

  return rtrn_value;
}

/********************************
 * PageManager class
 ********************************/
PageManager::PageManager()
{
  for(int i = 0; i < 4; ++i) {
    holders.push_back(new PageHolder(2048 * i));
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

/********************************
 * PageHolder class
 ********************************/
uint32_t* PageHolder::AddCode(uint32_t* code, int32_t size) {
  // get index into buffer
  uint32_t* temp = buffer + index;
  
  // copy and flush instruction cache
  const uint32_t byte_size = size * sizeof(uint32_t);
  memcpy(temp, code, byte_size);
  __clear_cache(temp, temp + byte_size);
  
  index += size;
  available -= byte_size;
  return temp;
}
