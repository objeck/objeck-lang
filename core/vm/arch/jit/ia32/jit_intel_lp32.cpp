/***************************************************************************
 * JIT compiler for 32-bit x86 architectures (Windows and Linux).
 *
 * Copyright (c) 2008-2018, Randy Hollines
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

#include "jit_intel_lp32.h"
#include <string>

using namespace Runtime;

/********************************
 * JitCompilerIA32 class
 ********************************/
StackProgram* JitCompilerIA32::program;
PageManager* JitCompilerIA32::page_manager;

void JitCompilerIA32::Initialize(StackProgram* p) {
  program = p;
  page_manager = new PageManager;
}

// setup of stack frame
void JitCompilerIA32::Prolog() {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [<prolog>]" << endl;
#endif

  local_space += 8;
  unsigned char buffer[4];
  ByteEncode32(buffer, local_space);

  unsigned char setup_code[] = {
    // setup stack frame
    0x55,                                                        // push %ebp
    0x89, 0xe5,                                                  // mov  %esp, %ebp
    0x81, 0xec, buffer[0], buffer[1], buffer[2], buffer[3],      // sub  $imm, %esp
                                                                 // save registers
    0x53,                                                        // push $ebx
    0x51,                                                        // push $ecx
    0x52,                                                        // push $edx
    0x57,                                                        // push $edi
    0x56                                                         // push $esi
  };
  const int32_t setup_size = sizeof(setup_code);
  // copy setup
  for(int32_t i = 0; i < setup_size; ++i) {
    AddMachineCode(setup_code[i]);
  }
}

// teardown of stackframe
void JitCompilerIA32::Epilog() {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [<epilog>]" << endl;
#endif

  epilog_index = code_index;

  // nominal
  AddMachineCode(0xe9);
  AddImm(30);

  // nullptr deref
  move_imm_reg(-1, EAX);
  AddMachineCode(0xe9);
  AddImm(25);

  // under bounds
  move_imm_reg(-2, EAX);
  AddMachineCode(0xe9);
  AddImm(15);

  // over bounds
  move_imm_reg(-3, EAX);
  AddMachineCode(0xe9);
  AddImm(5);

  // nominal
  move_imm_reg(0, EAX);

  // assume return value is in EAX
  unsigned char teardown_code[] = {
    // restore registers
    0x5e,             // pop $esi
    0x5f,             // pop $edi
    0x5a,             // pop $edx
    0x59,             // pop $ecx
    0x5b,             // pop $ebx
    // tear down stack frame and return
    0x89, 0xec,       // mov %ebp, %esp
    0x5d,             // pop %ebp
    0xc3              // rtn
  };
  const int32_t teardown_size = sizeof(teardown_code);

  // copy teardown
  for(int32_t i = 0; i < teardown_size; ++i) {
    AddMachineCode(teardown_code[i]);
  }
}

void JitCompilerIA32::RegisterRoot() {
  // calculate root address
  RegisterHolder* holder = GetRegister();
  
  // note: -8 is the offset required to 
  // get to the first local variable
  const int32_t offset = local_space + TMP_REG_5 - 8;
  move_reg_reg(EBP, holder->GetRegister());
  sub_imm_reg(offset, holder->GetRegister());

  // store modified ebp value in **ptr
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(JIT_MEM, EBP, mem_holder->GetRegister());
  move_reg_mem(holder->GetRegister(), 0, mem_holder->GetRegister());

  const int index = ((offset + TMP_REG_5) >> 2) + 6;
  if(index > 0) {
    move_imm_reg(index, ECX);
    move_imm_mem(0, 0, holder->GetRegister());
    add_imm_reg(sizeof(int32_t), holder->GetRegister());
    loop(-18);
  }

  // store offset value in **ptr
  move_mem_reg(JIT_OFFSET, EBP, mem_holder->GetRegister());
  move_imm_mem(offset, 0, mem_holder->GetRegister());
  
  // clean up
  ReleaseRegister(mem_holder);
  ReleaseRegister(holder);
}

void JitCompilerIA32::ProcessParameters(int32_t params) {
#ifdef _DEBUG_JIT
  wcout << L"CALLED_PARMS: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  for(int32_t i = 0; i < params; ++i) {
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, EBP, op_stack_holder->GetRegister());

    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);  

    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());
      
    if(instr->GetType() == STOR_LOCL_INT_VAR || 
       instr->GetType() == STOR_CLS_INST_INT_VAR) {
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
      move_mem_reg(-(long)(sizeof(int32_t)), op_stack_holder->GetRegister(), 
                   dest_holder2->GetRegister());
      
      move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());
      dec_mem(0, stack_pos_holder->GetRegister());      
      
      working_stack.push_front(new RegInstr(dest_holder2));
      working_stack.push_front(new RegInstr(dest_holder));
      
      // store int
      ProcessStore(instr);
      i++;
    }
    else {
      RegisterHolder* dest_holder = GetXmmRegister();
      sub_imm_mem(2, 0, stack_pos_holder->GetRegister());
      move_mem_reg(0, stack_pos_holder->GetRegister(), 
                   stack_pos_holder->GetRegister());
      shl_imm_reg(2, stack_pos_holder->GetRegister());
      add_reg_reg(stack_pos_holder->GetRegister(),
                  op_stack_holder->GetRegister()); 
      move_mem_xreg(0, op_stack_holder->GetRegister(), 
                    dest_holder->GetRegister());
      working_stack.push_front(new RegInstr(dest_holder));
      // store float
      ProcessStore(instr);
    }
    ReleaseRegister(op_stack_holder);
    ReleaseRegister(stack_pos_holder);
  }
}

void JitCompilerIA32::ProcessIntCallParameter() {
#ifdef _DEBUG_JIT
  wcout << L"INT_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, EBP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());
  
  dec_mem(0, stack_pos_holder->GetRegister());  
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(2, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister());  
  move_mem_reg(0, op_stack_holder->GetRegister(), op_stack_holder->GetRegister());
  working_stack.push_front(new RegInstr(op_stack_holder));
  
  ReleaseRegister(stack_pos_holder);
}

void JitCompilerIA32::ProcessFunctionCallParameter() {
#ifdef _DEBUG_JIT
  wcout << L"FUNC_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, EBP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());
  
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

void JitCompilerIA32::ProcessFloatCallParameter() {
#ifdef _DEBUG_JIT
  wcout << L"FLOAT_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
  
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, EBP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());
  
  RegisterHolder* dest_holder = GetXmmRegister();
  sub_imm_mem(2, 0, stack_pos_holder->GetRegister());
  move_mem_reg(0, stack_pos_holder->GetRegister(), stack_pos_holder->GetRegister());
  shl_imm_reg(2, stack_pos_holder->GetRegister());
  add_reg_reg(stack_pos_holder->GetRegister(), op_stack_holder->GetRegister()); 
  move_mem_xreg(0, op_stack_holder->GetRegister(), dest_holder->GetRegister());
  working_stack.push_front(new RegInstr(dest_holder));
  
  ReleaseRegister(op_stack_holder);
  ReleaseRegister(stack_pos_holder);
}

void JitCompilerIA32::ProcessInstructions() {
  while(instr_index < method->GetInstructionCount() && compile_success) {
    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);
    
    switch(instr->GetType()) {
      // load literal
    case LOAD_CHAR_LIT:
    case LOAD_INT_LIT:
#ifdef _DEBUG_JIT
      wcout << L"LOAD_INT: value=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
      break;

      // float literal
    case LOAD_FLOAT_LIT:
#ifdef _DEBUG_JIT
      wcout << L"LOAD_FLOAT_LIT: value=" << instr->GetFloatOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      float_consts[floats_index] = instr->GetFloatOperand();
      working_stack.push_front(new RegInstr(instr, &float_consts[floats_index++]));
      break;
      
      // load self
    case LOAD_INST_MEM: {
#ifdef _DEBUG_JIT
      wcout << L"LOAD_INST_MEM; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      working_stack.push_front(new RegInstr(instr));
    }
      break;

      // load self
    case LOAD_CLS_MEM: {
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
      wcout << L"STOR_INT_VAR/STOR_FLOAT_VAR/STOR_FUNC_VAR: id=" << instr->GetOperand() 
            << L"; regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStore(instr);
      break;

      // copy value
    case COPY_LOCL_INT_VAR: 
    case COPY_CLS_INST_INT_VAR:
    case COPY_FLOAT_VAR:
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
      wcout << L"INT ADD/SUB/MUL/DIV/MOD/BIT_AND/BIT_OR/BIT_XOR/LES/GTR/EQL/NEQL/SHL_INT/SHR_INT:: regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessIntCalculation(instr);
      break;
      
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
#ifdef _DEBUG_JIT
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
    case ATAN2_FLOAT:
    case POW_FLOAT:
    case LOG_FLOAT:
#ifdef _DEBUG_JIT
      wcout << L"FLOAT SIN/COS/TAN/SQRT/POW: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatOperation(instr);
      break;
      
    case LES_FLOAT:
    case GTR_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT: {
#ifdef _DEBUG_JIT
      wcout << L"FLOAT LES/GTR/EQL/NEQL: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);

      RegInstr* left = working_stack.front();
      working_stack.pop_front(); // pop invalid xmm register
      ReleaseXmmRegister(left->GetRegister());

      delete left; 
      left = nullptr;
      
      RegisterHolder* holder = GetRegister();
      cmov_reg(holder->GetRegister(), instr->GetType());
      working_stack.push_front(new RegInstr(holder));
      
    }
      break;
      
    case RTRN:
#ifdef _DEBUG_JIT
      wcout << L"RTRN: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessReturn();
      // teardown
      Epilog();
      break;
      
    case MTHD_CALL: {
      StackMethod* called_method = program->GetClass(instr->GetOperand())->GetMethod(instr->GetOperand2());
      if(called_method) {
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
      wcout << L"DYN_MTHD_CALL: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif  
      // passing instance variable
      ProcessStackCallback(DYN_MTHD_CALL, instr, instr_index, instr->GetOperand() + 3);
      ProcessReturnParameters((MemoryType)instr->GetOperand2());
    }
      break;
      
    case NEW_BYTE_ARY:
#ifdef _DEBUG_JIT
      wcout << L"NEW_BYTE_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_BYTE_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_CHAR_ARY:
#ifdef _DEBUG_JIT
      wcout << L"NEW_CHAR_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size()
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_CHAR_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_INT_ARY:
#ifdef _DEBUG_JIT
      wcout << L"NEW_INT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size() 
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_INT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;

    case NEW_FLOAT_ARY:
#ifdef _DEBUG_JIT
      wcout << L"NEW_FLOAT_ARY: dim=" << instr->GetOperand() << L" regs=" << aval_regs.size() 
            << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(NEW_FLOAT_ARY, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case NEW_OBJ_INST: {
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
      wcout << L"THREAD_JOIN: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(THREAD_JOIN, instr, instr_index, 0);
    }
      break;

    case THREAD_SLEEP: {
#ifdef _DEBUG_JIT
      wcout << L"THREAD_SLEEP: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(THREAD_SLEEP, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_START: {
#ifdef _DEBUG_JIT
      wcout << L"CRITICAL_START: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CRITICAL_START, instr, instr_index, 1);
    }
      break;
      
    case CRITICAL_END: {
#ifdef _DEBUG_JIT
      wcout << L"CRITICAL_END: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CRITICAL_END, instr, instr_index, 1);
    }
      break;
      
    case CPY_BYTE_ARY: {
#ifdef _DEBUG_JIT
      wcout << L"CPY_BYTE_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_BYTE_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_CHAR_ARY: {
#ifdef _DEBUG_JIT
      wcout << L"CPY_CHAR_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_CHAR_ARY, instr, instr_index, 5);
    }
      break;
      
    case CPY_INT_ARY: {
#ifdef _DEBUG_JIT
      wcout << L"CPY_INT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_INT_ARY, instr, instr_index, 5);
    }
      break;

    case CPY_FLOAT_ARY: {
#ifdef _DEBUG_JIT
      wcout << L"CPY_FLOAT_ARY: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(CPY_FLOAT_ARY, instr, instr_index, 5);
    }
      break;
 
    case TRAP:
#ifdef _DEBUG_JIT
      wcout << L"TRAP: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(TRAP, instr, instr_index, instr->GetOperand());
      break;

    case TRAP_RTRN:
#ifdef _DEBUG_JIT
      wcout << L"TRAP_RTRN: args=" << instr->GetOperand() << L"; regs=" 
            << aval_regs.size() << L"," << aux_regs.size() << endl;
      assert(instr->GetOperand());
#endif      
      ProcessStackCallback(TRAP_RTRN, instr, instr_index, instr->GetOperand());
      ProcessReturnParameters(INT_TYPE);
      break;
      
    case STOR_BYTE_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"STOR_BYTE_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessStoreByteElement(instr);
      break;

    case STOR_CHAR_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"STOR_CHAR_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessStoreCharElement(instr);
      break;
      
    case STOR_INT_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"STOR_INT_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStoreIntElement(instr);
      break;

    case STOR_FLOAT_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"STOR_FLOAT_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStoreFloatElement(instr);
      break;

    case SWAP_INT: {
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
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
          ReleaseXmmRegister(left->GetRegister());
        }
        // clean up
        delete left;
        left = nullptr;
      }
    }
      break;

    case FLOR_FLOAT:
#ifdef _DEBUG_JIT
      wcout << L"FLOR_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloor(instr);
      break;

    case CEIL_FLOAT:
#ifdef _DEBUG_JIT
      wcout << L"CEIL_FLOAT: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessCeiling(instr);
      break;
      
    case F2I:
#ifdef _DEBUG_JIT
      wcout << L"F2I: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessFloatToInt(instr);
      break;

    case I2F:
#ifdef _DEBUG_JIT
      wcout << L"I2F: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessIntToFloat(instr);
      break;

    case I2S:
#ifdef _DEBUG_JIT
      wcout << L"I2S: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(I2S, instr, instr_index, 3);
      break;
      
      case S2I:
#ifdef _DEBUG_JIT
      wcout << L"S2I: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(S2I, instr, instr_index, 2);
      ProcessReturnParameters(INT_TYPE);
      break;
      
      case F2S:
#ifdef _DEBUG_JIT
      wcout << L"F2S: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(F2S, instr, instr_index, 2);
      break;
      
      case S2F:
#ifdef _DEBUG_JIT
      wcout << L"S2F: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(S2F, instr, instr_index, 2);
      ProcessReturnParameters(FLOAT_TYPE);
      break;
      
    case OBJ_TYPE_OF: {
#ifdef _DEBUG_JIT
      wcout << L"OBJ_TYPE_OF: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(OBJ_TYPE_OF, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case OBJ_INST_CAST: {
#ifdef _DEBUG_JIT
      wcout << L"OBJ_INST_CAST: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(OBJ_INST_CAST, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;

    case LOAD_ARY_SIZE: {
#ifdef _DEBUG_JIT
      wcout << L"LOAD_ARY_SIZE: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessStackCallback(LOAD_ARY_SIZE, instr, instr_index, 1);
      ProcessReturnParameters(INT_TYPE);
    }
      break;
      
    case LOAD_BYTE_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"LOAD_BYTE_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessLoadByteElement(instr);
      break;

    case LOAD_CHAR_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"LOAD_CHAR_ARY_ELM: regs=" << aval_regs.size() << L"," << aux_regs.size() << endl;
#endif
      ProcessLoadCharElement(instr);
      break;
      
    case LOAD_INT_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"LOAD_INT_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessLoadIntElement(instr);
      break;

    case LOAD_FLOAT_ARY_ELM:
#ifdef _DEBUG_JIT
      wcout << L"LOAD_FLOAT_ARY_ELM: regs=" << aval_regs.size() << L"," 
            << aux_regs.size() << endl;
#endif
      ProcessLoadFloatElement(instr);
      break;
      
    case JMP:
      ProcessJump(instr);
      break;
      
    case LBL:
#ifdef _DEBUG_JIT
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

void JitCompilerIA32::ProcessLoad(StackInstr* instr) {
  // method/function memory
  if(instr->GetOperand2() == LOCL) {
    if(instr->GetType() == LOAD_FUNC_VAR) {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(instr->GetOperand3() + sizeof(int32_t), EBP, holder->GetRegister());
      working_stack.push_front(new RegInstr(holder));
      
      RegisterHolder* holder2 = GetRegister();
      move_mem_reg(instr->GetOperand3(), EBP, holder2->GetRegister());
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
      move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
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
      RegisterHolder* xmm_holder = GetXmmRegister();
      move_mem_xreg(instr->GetOperand3(), holder->GetRegister(), xmm_holder->GetRegister());
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(xmm_holder));	  
    }

    delete left;
    left = nullptr;
  }
}

void JitCompilerIA32::ProcessJump(StackInstr* instr) {
  if(!skip_jump) {
#ifdef _DEBUG_JIT
    wcout << L"JMP: id=" << instr->GetOperand() << L", regs=" << aval_regs.size() 
          << L"," << aux_regs.size() << endl;
#endif
    if(instr->GetOperand2() < 0) {
      AddMachineCode(0xe9);
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
        move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
        cmp_imm_reg(instr->GetOperand2(), holder->GetRegister());
        ReleaseRegister(holder);
      }
        break;

      default:
        wcerr << L">>> Should never occur (compiler bug?) type=" << left->GetType() << L" <<<" << endl;
        exit(1);
        break;
      }

#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [je]" << std::endl;
#endif
      // compare with register
      AddMachineCode(0x0f);
      AddMachineCode(0x84);
      
      // clean up
      delete left;
      left = nullptr;
    }
    // store update index
    jump_table.insert(pair<int32_t, StackInstr*>(code_index, instr));
    // temp offset, updated in next pass
    AddImm(0);
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

void JitCompilerIA32::ProcessReturnParameters(MemoryType type) {
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

void JitCompilerIA32::ProcessLoadByteElement(StackInstr* instr) {
  RegisterHolder* holder = GetRegister();
  RegisterHolder* elem_holder = ArrayIndex(instr, BYTE_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());
  move_mem8_reg(0, elem_holder->GetRegister(), holder->GetRegister());
  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitCompilerIA32::ProcessLoadCharElement(StackInstr* instr) {
#ifdef _WIN32
  RegisterHolder* holder = GetRegister(false);
#else
  RegisterHolder* holder = GetRegister();
#endif
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  xor_reg_reg(holder->GetRegister(), holder->GetRegister());

#ifdef _WIN32
  move_mem16_reg(0, elem_holder->GetRegister(), holder->GetRegister());
#else
  move_mem_reg(0, elem_holder->GetRegister(), holder->GetRegister());
#endif

  ReleaseRegister(elem_holder);
  working_stack.push_front(new RegInstr(holder));
}

void JitCompilerIA32::ProcessLoadIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  move_mem_reg(0, elem_holder->GetRegister(), elem_holder->GetRegister());
  working_stack.push_front(new RegInstr(elem_holder));
}

void JitCompilerIA32::ProcessLoadFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(0, elem_holder->GetRegister(), holder->GetRegister());
  working_stack.push_front(new RegInstr(holder));
  ReleaseRegister(elem_holder);
}

void JitCompilerIA32::ProcessStoreByteElement(StackInstr* instr) {
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
    move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  case REG_INT: {
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = left->GetRegister();
    if(holder->GetRegister() == EDI || holder->GetRegister() == ESI) {
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

void JitCompilerIA32::ProcessStoreCharElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, CHAR_ARY_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
#ifdef _WIN32
    move_imm_mem16(left->GetOperand(), 0, elem_holder->GetRegister());
#else
    move_imm_mem(left->GetOperand(), 0, elem_holder->GetRegister());
#endif
    break;

  case MEM_INT: {    
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = GetRegister(false);
    move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
#ifdef _WIN32
    move_reg_mem16(holder->GetRegister(), 0, elem_holder->GetRegister());
#else
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
#endif
    ReleaseRegister(holder);
  }
    break;

  case REG_INT: {
    // movb can only use al, bl, cl and dl registers
    RegisterHolder* holder = left->GetRegister();
    if(holder->GetRegister() == EDI || holder->GetRegister() == ESI) {
      RegisterHolder* tmp_holder = GetRegister(false);
      move_reg_reg(holder->GetRegister(), tmp_holder->GetRegister());
#ifdef _WIN32
      move_reg_mem16(tmp_holder->GetRegister(), 0, elem_holder->GetRegister());      
#else
      move_reg_mem(tmp_holder->GetRegister(), 0, elem_holder->GetRegister());      
#endif
      ReleaseRegister(tmp_holder);
    }
    else {
#ifdef _WIN32
      move_reg_mem16(holder->GetRegister(), 0, elem_holder->GetRegister());   
#else   
      move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());   
#endif
    }
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

void JitCompilerIA32::ProcessStoreIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, INT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_INT:
    move_imm_mem(left->GetOperand(), 0, elem_holder->GetRegister());
    break;

  case MEM_INT: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
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

void JitCompilerIA32::ProcessStoreFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayIndex(instr, FLOAT_TYPE);
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT:
    move_imm_memx(left, 0, elem_holder->GetRegister());
    break;

  case MEM_FLOAT: 
  case MEM_INT: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg(left->GetOperand(), 
                  EBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
    break;

  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
    break;

  default:
    break;
  }
  ReleaseRegister(elem_holder);
  
  delete left;
  left = nullptr;
}

void JitCompilerIA32::ProcessFloor(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT: {
    RegisterHolder* holder = GetXmmRegister();
    round_imm_xreg(left, holder->GetRegister(), true);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case MEM_FLOAT:
  case MEM_INT: {
    RegisterHolder* holder = GetXmmRegister();
    round_mem_xreg(left->GetOperand(), EBP, holder->GetRegister(), true);
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

void JitCompilerIA32::ProcessCeiling(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  switch(left->GetType()) {
  case IMM_FLOAT: {
    RegisterHolder* holder = GetXmmRegister();
    round_imm_xreg(left, holder->GetRegister(), false);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case MEM_FLOAT:
  case MEM_INT: {
    RegisterHolder* holder = GetXmmRegister();
    round_mem_xreg(left->GetOperand(), EBP, holder->GetRegister(), false);
    working_stack.push_front(new RegInstr(holder));
    delete left;
    left = nullptr;
  }
    break;
    
  case REG_FLOAT:
    round_xreg_xreg(left->GetRegister()->GetRegister(), 
                    left->GetRegister()->GetRegister(), false);
    working_stack.push_front(left);
    break;

  default:
    break;
  }
}

void JitCompilerIA32::ProcessFloatToInt(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetRegister();
  switch(left->GetType()) {
  case IMM_FLOAT:
    cvt_imm_reg(left, holder->GetRegister());
    break;
    
  case MEM_FLOAT:
  case MEM_INT:
    cvt_mem_reg(left->GetOperand(), 
                EBP, holder->GetRegister());
    break;

  case REG_FLOAT:
    cvt_xreg_reg(left->GetRegister()->GetRegister(), 
                 holder->GetRegister());
    ReleaseXmmRegister(left->GetRegister());
    break;

  default:
    break;
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitCompilerIA32::ProcessIntToFloat(StackInstr* instr) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();
  
  RegisterHolder* holder = GetXmmRegister();
  switch(left->GetType()) {
  case IMM_INT:
    cvt_imm_xreg(left, holder->GetRegister());
    break;
    
  case MEM_INT:
    cvt_mem_xreg(left->GetOperand(), 
                 EBP, holder->GetRegister());
    break;

  case REG_INT:
    cvt_reg_xreg(left->GetRegister()->GetRegister(), 
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

void JitCompilerIA32::ProcessStore(StackInstr* instr) {
  Register dest;
  RegisterHolder* addr_holder = nullptr;

  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = EBP;
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
      move_mem_reg(left->GetOperand(), EBP, addr_holder->GetRegister());
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
      move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);

      RegInstr* left2 = working_stack.front();
      working_stack.pop_front();
      move_mem_reg(left2->GetOperand(), EBP, holder->GetRegister());
      move_reg_mem(holder->GetRegister(), instr->GetOperand3() + sizeof(int32_t), dest);

      delete left2;
      left2 = nullptr;
    }
    else {      
      move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
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
      /*
      // mark static reference
      if(instr->GetOperand2() == CLS) {
      ProcessAddStaticMemory(holder->GetRegister());
      }
      */
      move_reg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    }
    ReleaseRegister(holder);
  }
    break;
    
  case IMM_FLOAT:
    move_imm_memx(left, instr->GetOperand3(), dest);
    break;
    
  case MEM_FLOAT: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg(left->GetOperand(), EBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    ReleaseXmmRegister(holder);
  }
    break;
    
  case REG_FLOAT: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), instr->GetOperand3(), dest);
    ReleaseXmmRegister(holder);
  }
    break;
  }

  if(addr_holder) {
    ReleaseRegister(addr_holder);
  }

  delete left;
  left = nullptr;
}

void JitCompilerIA32::ProcessCopy(StackInstr* instr) {
  Register dest;
  // instance/method memory
  if(instr->GetOperand2() == LOCL) {
    dest = EBP;
  }
  // class or instance memory
  else {
    RegInstr* left = working_stack.front();
    working_stack.pop_front();

    RegisterHolder* holder = GetRegister();
    move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
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
    move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
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
    RegisterHolder* holder = GetXmmRegister();
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
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg(left->GetOperand(), EBP, holder->GetRegister());
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

void JitCompilerIA32::ProcessStackCallback(int32_t instr_id, StackInstr* instr, int32_t &instr_index, int32_t params) {
  int32_t non_params;
  if(params < 0) {
    non_params = 0;
  }
  else {
    non_params = working_stack.size() - params;
  }
  
#ifdef _DEBUG_JIT
  wcout << L"Return: params=" << params << L", non-params=" << non_params << endl;
#endif
  
  stack<RegInstr*> regs;
  stack<int32_t> dirty_regs;
  int32_t reg_offset = TMP_REG_0;  

  stack<RegInstr*> xmms;
  stack<int32_t> dirty_xmms;
  int32_t xmm_offset = TMP_XMM_0;
  
  int32_t i = 0;     
  for(deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); iter != working_stack.rend(); ++iter) {
    RegInstr* left = (*iter);
    if(i < non_params) {
      switch(left->GetType()) {
      case REG_INT:
        move_reg_mem(left->GetRegister()->GetRegister(), reg_offset, EBP);
        dirty_regs.push(reg_offset);
        regs.push(left);
        reg_offset -= 4;
        break;

      case REG_FLOAT:
        move_xreg_mem(left->GetRegister()->GetRegister(), xmm_offset, EBP);
        dirty_xmms.push(xmm_offset);
        xmms.push(left);
        xmm_offset -= 8;
        break;

      default:
        break;
      }
      // update
      i++;
    }
  }

#ifdef _DEBUG_JIT
  assert(reg_offset >= TMP_REG_5);
  assert(xmm_offset >= TMP_XMM_2);
#endif

  if(dirty_regs.size() > 6 || dirty_xmms.size() > 3 ) {
    compile_success = false;
  }

  // copy values to execution stack
  ProcessReturn(params);
  
  // set parameters
  push_imm(instr_index - 1);
  push_mem(CALL_STACK_POS, EBP);
  push_mem(CALL_STACK, EBP);
  push_mem(STACK_POS, EBP);
  push_mem(OP_STACK, EBP);
  push_mem(INSTANCE_MEM, EBP);
  push_mem(MTHD_ID, EBP);
  push_mem(CLS_ID, EBP);
  push_imm((int32_t)instr);
  push_imm(instr_id);
  
  // call function
  RegisterHolder* call_holder = GetRegister();
  move_imm_reg((int32_t)JitCompilerIA32::JitStackCallback, call_holder->GetRegister());
  call_reg(call_holder->GetRegister());
  add_imm_reg(40, ESP);
  ReleaseRegister(call_holder);

  // restore register values
  while(!dirty_regs.empty()) {
    RegInstr* left = regs.top();
    move_mem_reg(dirty_regs.top(), EBP, left->GetRegister()->GetRegister());
    // update
    regs.pop();
    dirty_regs.pop();
  }
  
  while(!dirty_xmms.empty()) {
    RegInstr* left = xmms.top();
    move_mem_xreg(dirty_xmms.top(), EBP, left->GetRegister()->GetRegister());
    // update
    xmms.pop();
    dirty_xmms.pop();
  }
}

void JitCompilerIA32::ProcessReturn(int32_t params) {
  if(!working_stack.empty()) {
    RegisterHolder* op_stack_holder = GetRegister();
    move_mem_reg(OP_STACK, EBP, op_stack_holder->GetRegister());
    
    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());    
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
#ifdef _DEBUG_JIT
    wcout << L"Return: params=" << params << L", non-params=" << non_params << endl;
#endif
    
    int32_t i = 0;     
    for(deque<RegInstr*>::reverse_iterator iter = working_stack.rbegin(); 
        iter != working_stack.rend(); ++iter) {
      // skip non-params... processed above
      RegInstr* left = (*iter);
      if(i < non_params) {
        i++;
      }
      else {
        move_mem_reg(STACK_POS, EBP, stack_pos_holder->GetRegister());            
        switch(left->GetType()) {
        case IMM_INT:
          move_imm_mem(left->GetOperand(), 0, op_stack_holder->GetRegister());
          inc_mem(0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(int32_t), op_stack_holder->GetRegister());
          break;
	
        case MEM_INT: {
          RegisterHolder* temp_holder = GetRegister();
          move_mem_reg(left->GetOperand(), EBP, temp_holder->GetRegister());
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
          RegisterHolder* temp_holder = GetXmmRegister();
          move_mem_xreg(left->GetOperand(), EBP, temp_holder->GetRegister());
          move_xreg_mem(temp_holder->GetRegister(), 0, op_stack_holder->GetRegister());
          add_imm_mem(2, 0, stack_pos_holder->GetRegister());
          add_imm_reg(sizeof(double), op_stack_holder->GetRegister());
          ReleaseXmmRegister(temp_holder); 
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
        ReleaseXmmRegister(left->GetRegister());
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

RegInstr* JitCompilerIA32::ProcessIntFold(long left_imm, long right_imm, InstructionType type) {
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

void JitCompilerIA32::ProcessIntCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  RegInstr* right = working_stack.front();
  working_stack.pop_front();

  switch(left->GetType()) {
    // intermidate
  case IMM_INT:
    switch(right->GetType()) {
    case IMM_INT:
      working_stack.push_front(ProcessIntFold(left->GetOperand(), 
                                              right->GetOperand(), 
                                              instruction->GetType()));
      break;
      
    case REG_INT: {
      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());
      RegisterHolder* holder = right->GetRegister();

      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(), 
                   instruction->GetType());
      
      ReleaseRegister(holder);
      working_stack.push_front(new RegInstr(imm_holder));
    }
      break;

    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(right->GetOperand(), EBP, holder->GetRegister());

      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());

      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(), 
                   instruction->GetType());
      
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
      move_mem_reg(right->GetOperand(), EBP, rhs->GetRegister());
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
      move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
      math_imm_reg(right->GetOperand(), holder->GetRegister(), instruction->GetType());
      working_stack.push_front(new RegInstr(holder));
    }
      break;
    case REG_INT: {
      RegisterHolder* lhs = right->GetRegister();
      RegisterHolder* rhs = GetRegister();
      move_mem_reg(left->GetOperand(), EBP, rhs->GetRegister());
      math_reg_reg(lhs->GetRegister(), rhs->GetRegister(), instruction->GetType());
      ReleaseRegister(lhs);
      working_stack.push_front(new RegInstr(rhs));
    }
      break;
    case MEM_INT: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(left->GetOperand(), EBP, holder->GetRegister());
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

 void JitCompilerIA32::ProcessFloatCalculation(StackInstr* instruction) {
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
      RegisterHolder* left_holder = GetXmmRegister();
      move_imm_xreg(left, left_holder->GetRegister());      
      RegisterHolder* right_holder = GetXmmRegister();
      move_imm_xreg(right, right_holder->GetRegister());      
      
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left_holder->GetRegister(), right_holder->GetRegister(), 
                       instruction->GetType());
        ReleaseXmmRegister(left_holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_xreg_xreg(right_holder->GetRegister(), left_holder->GetRegister(), 
                       instruction->GetType());
        ReleaseXmmRegister(right_holder);
        working_stack.push_front(new RegInstr(left_holder));
      }
    }
      break;
      
    case REG_FLOAT: {      
      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(left, imm_holder->GetRegister());
      
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(imm_holder->GetRegister(), right->GetRegister()->GetRegister(), type);
        ReleaseXmmRegister(imm_holder);
        working_stack.push_front(new RegInstr(right->GetRegister()));
      }
      else {
        math_xreg_xreg(right->GetRegister()->GetRegister(), imm_holder->GetRegister(), type);
        ReleaseXmmRegister(right->GetRegister());
        working_stack.push_front(new RegInstr(imm_holder));
      }
    }
      break;

    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg(right->GetOperand(), EBP, holder->GetRegister());

      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(left, imm_holder->GetRegister());

      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(imm_holder->GetRegister(), holder->GetRegister(), type);
        ReleaseXmmRegister(imm_holder);
        working_stack.push_front(new RegInstr(holder));
      }
      else {
        math_xreg_xreg(holder->GetRegister(), imm_holder->GetRegister(), type);
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(imm_holder));
      }
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
      RegisterHolder* right_holder = GetXmmRegister();
      move_imm_xreg(right, right_holder->GetRegister());
      
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left_holder->GetRegister(), right_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(left_holder);      
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_xreg_xreg(right_holder->GetRegister(), left_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(right_holder);      
        working_stack.push_front(new RegInstr(left_holder));
      }
    }
      break;

    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left->GetRegister()->GetRegister(), holder->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(holder));
        ReleaseXmmRegister(left->GetRegister());
      }
      else {
        math_xreg_xreg(holder->GetRegister(), left->GetRegister()->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(left->GetRegister()));
        ReleaseXmmRegister(holder);
      }
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* holder = left->GetRegister();
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        RegisterHolder* right_holder = GetXmmRegister();
        move_mem_xreg(right->GetOperand(), EBP, right_holder->GetRegister());
        math_xreg_xreg(holder->GetRegister(), right_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_mem_xreg(right->GetOperand(), holder->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(holder));
      }
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
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg(left->GetOperand(), EBP, holder->GetRegister());
      
      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(right, imm_holder->GetRegister());
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(holder->GetRegister(), imm_holder->GetRegister(), type);
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(imm_holder));
      }
      else {
        math_xreg_xreg(imm_holder->GetRegister(), holder->GetRegister(), type);
        ReleaseXmmRegister(imm_holder);
        working_stack.push_front(new RegInstr(holder));
      }
    }
      break;
      
    case REG_FLOAT: {
      RegisterHolder* holder = right->GetRegister();
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_mem_xreg(left->GetOperand(), holder->GetRegister(), instruction->GetType());
        working_stack.push_front(new RegInstr(holder));
      }
      else {
        RegisterHolder* right_holder = GetXmmRegister();
        move_mem_xreg(left->GetOperand(), EBP, right_holder->GetRegister());
        math_xreg_xreg(holder->GetRegister(), right_holder->GetRegister(), instruction->GetType());
        ReleaseXmmRegister(holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
    }
      break;
      
    case MEM_FLOAT:
    case MEM_INT: {
      RegisterHolder* left_holder = GetXmmRegister();
      move_mem_xreg(left->GetOperand(), EBP, left_holder->GetRegister());

      RegisterHolder* right_holder = GetXmmRegister();
      move_mem_xreg(right->GetOperand(), EBP, right_holder->GetRegister());
      if(type == LES_FLOAT || type == LES_EQL_FLOAT) {
        math_xreg_xreg(left_holder->GetRegister(), right_holder->GetRegister(),  
                       instruction->GetType());
        ReleaseXmmRegister(left_holder);
        working_stack.push_front(new RegInstr(right_holder));
      }
      else {
        math_xreg_xreg(right_holder->GetRegister(), left_holder->GetRegister(),  
                       instruction->GetType());	
        ReleaseXmmRegister(right_holder);
        working_stack.push_front(new RegInstr(left_holder));
      }
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

void JitCompilerIA32::ProcessFloatOperation(StackInstr* instruction) {
  RegInstr* left = working_stack.front();
  working_stack.pop_front();

  InstructionType type = instruction->GetType();
#ifdef _DEBUG_JIT
  assert(left->GetType() == MEM_FLOAT);
#endif

  RegisterHolder* holder = nullptr;
  switch(type) {
  case SIN_FLOAT:
    fld_mem(left->GetOperand(), EBP);
    fsin();
    break;

  case COS_FLOAT:
    fld_mem(left->GetOperand(), EBP);
    fcos();
    break;

  case TAN_FLOAT:
    fld_mem(left->GetOperand(), EBP);
    ftan();
    break;

  case SQRT_FLOAT:
    fld_mem(left->GetOperand(), EBP);
    fsqrt();
    break;

  case ASIN_FLOAT:
    holder = call_xfunc(asin, left);
    break;

  case ACOS_FLOAT:
    holder = call_xfunc(acos, left);
    break;

  case LOG_FLOAT:
    holder = call_xfunc(log, left);
    break;

  case ATAN2_FLOAT:
    holder = call_xfunc2(atan2, left);
    break;

  case POW_FLOAT:
    holder = call_xfunc2(pow, left);
    break;

  default:
#ifdef _DEBUG_JIT
    assert(false);
#endif
    break;
  }

  if(!holder) {
    holder = GetXmmRegister();
    fstp_mem(left->GetOperand(), EBP);
    move_mem_xreg(left->GetOperand(), EBP, holder->GetRegister());
  }
  working_stack.push_front(new RegInstr(holder));

  delete left;
  left = nullptr;
}

void JitCompilerIA32::move_reg_reg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG_JIT
    wcout << L"  " << (++instr_count) << L": [movl %" << GetRegisterName(src) 
          << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
    // encode
    AddMachineCode(0x89);
    unsigned char code = 0xc0;
    // write value
    RegisterEncode3(code, 2, src);
    RegisterEncode3(code, 5, dest);
    AddMachineCode(code);
  }
}

void JitCompilerIA32::move_reg_mem16(Register src, int32_t offset, Register dest) { 
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movw %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << endl;
#endif
  // encode
  AddMachineCode(0x66);
  AddMachineCode(0x89);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::move_reg_mem8(Register src, int32_t offset, Register dest) { 
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movb %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << endl;
#endif
  // encode
  AddMachineCode(0x88);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}
    
void JitCompilerIA32::move_reg_mem(Register src, int32_t offset, Register dest) { 
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movl %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << endl;
#endif
  // encode
  AddMachineCode(0x89);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::move_mem8_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movb " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x0f);
  AddMachineCode(0xb6);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::move_mem16_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movw " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x0f);
  AddMachineCode(0xb7);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::move_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x8b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitCompilerIA32::move_imm_memx(RegInstr* instr, int32_t offset, Register dest) {
  RegisterHolder* tmp_holder = GetXmmRegister();
  move_imm_xreg(instr, tmp_holder->GetRegister());
  move_xreg_mem(tmp_holder->GetRegister(), offset, dest);
  ReleaseXmmRegister(tmp_holder);
}

void JitCompilerIA32::move_imm_mem8(int32_t imm, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movb $" << imm << L", " << offset 
        << L"(%" << GetRegisterName(dest) << L")" << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xc6);
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  // write value
  AddImm(offset);
  AddMachineCode(imm);
}

void JitCompilerIA32::move_imm_mem16(int32_t imm, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movw $" << imm << L", " << offset 
        << L"(%" << GetRegisterName(dest) << L")" << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x66);    
  AddMachineCode(0xc7);    
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  // write value
  AddImm(offset);
  AddImm16(imm);
}

void JitCompilerIA32::move_imm_mem(int32_t imm, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movl $" << imm << L", " << offset 
        << L"(%" << GetRegisterName(dest) << L")" << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xc7);    
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  // write value
  AddImm(offset);
  AddImm(imm);
}

void JitCompilerIA32::move_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movl $" << imm << L", %" 
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  unsigned char code = 0xb8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerIA32::move_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());  
  move_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}
    
void JitCompilerIA32::move_mem_xreg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movsd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest)
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x10);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitCompilerIA32::move_xreg_mem(Register src, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [movsd %" << GetRegisterName(src) 
        << L", " << offset << L"(%" << GetRegisterName(dest) << L")" << L"]" 
        << endl;
#endif 
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x11);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::move_xreg_xreg(Register src, Register dest) {
  if(src != dest) {
#ifdef _DEBUG_JIT
    wcout << L"  " << (++instr_count) << L": [movsd %" << GetRegisterName(src) 
          << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
    // encode
    AddMachineCode(0xf2);
    AddMachineCode(0x0f);
    AddMachineCode(0x11);
    unsigned char code = 0xc0;
    // write value
    RegisterEncode3(code, 2, src);
    RegisterEncode3(code, 5, dest);
    AddMachineCode(code);
  }
}

bool JitCompilerIA32::cond_jmp(InstructionType type) {
  if(instr_index >= method->GetInstructionCount()) {
    return false;
  }

  StackInstr* next_instr = method->GetInstruction(instr_index);
  if(next_instr->GetType() == JMP && next_instr->GetOperand2() > -1) {
#ifdef _DEBUG_JIT
    std::wcout << L"JMP: id=" << next_instr->GetOperand() << L", regs=" << aval_regs.size() << L"," << aux_regs.size() << std::endl;
#endif
    AddMachineCode(0x0f);

    //
    // jump if true
    //
    if(next_instr->GetOperand2() == 1) {
      switch(type) {
      case LES_INT:	
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jl]" << std::endl;
#endif
        AddMachineCode(0x8c);
        break;

      case GTR_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jg]" << std::endl;
#endif
        AddMachineCode(0x8f);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [je]" << std::endl;
#endif
        AddMachineCode(0x84);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jne]" << std::endl;
#endif
        AddMachineCode(0x85);
        break;

      case LES_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jle]" << std::endl;
#endif
        AddMachineCode(0x8e);
        break;
        
      case GTR_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jge]" << std::endl;
#endif
        AddMachineCode(0x8d);
        break;

      case LES_FLOAT:
        AddMachineCode(0x87);
        break;
				
      case GTR_FLOAT:
        AddMachineCode(0x87);
        break;

      case LES_EQL_FLOAT:
        AddMachineCode(0x83);
        break;
				
      case GTR_EQL_FLOAT:
        AddMachineCode(0x83);
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
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jge]" << std::endl;
#endif
        AddMachineCode(0x8d);
        break;

      case GTR_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jle]" << std::endl;
#endif
        AddMachineCode(0x8e);
        break;

      case EQL_INT:
      case EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jne]" << std::endl;
#endif
        AddMachineCode(0x85);
        break;

      case NEQL_INT:
      case NEQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [je]" << std::endl;
#endif
        AddMachineCode(0x84);
        break;

      case LES_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jg]" << std::endl;
#endif
        AddMachineCode(0x8f);
        break;
        
      case GTR_EQL_INT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jl]" << std::endl;
#endif
        AddMachineCode(0x8c);
        break;

      case LES_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [ja]" << std::endl;
#endif
        AddMachineCode(0x86);
        break;
				
      case GTR_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [ja]" << std::endl;
#endif
        AddMachineCode(0x86);
        break;

      case LES_EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jae]" << std::endl;
#endif
        AddMachineCode(0x82);
        break;
				
      case GTR_EQL_FLOAT:
#ifdef _DEBUG_JIT
        std::wcout << L"  " << (++instr_count) << L": [jae]" << std::endl;
#endif
        AddMachineCode(0x82);
        break;
				
      default:
        break;
      }  
    }    
    // store update index
    jump_table.insert(pair<int32_t, StackInstr*>(code_index, next_instr));
    // temp offset
    AddImm(0);
    skip_jump = true;
		
    return true;
  }
  
  return false;
}

void JitCompilerIA32::math_imm_reg(int32_t imm, Register reg, InstructionType type) {
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

void JitCompilerIA32::math_reg_reg(Register src, Register dest, InstructionType type) {
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

void JitCompilerIA32::math_mem_reg(int32_t offset, Register reg, InstructionType type) {
  switch(type) {
  case SHL_INT:
    shl_mem_reg(offset, EBP, reg);
    break;

  case SHR_INT:
    shr_mem_reg(offset, EBP, reg);
    break;
    
  case AND_INT:
    and_mem_reg(offset, EBP, reg);
    break;

  case OR_INT:
    or_mem_reg(offset, EBP, reg);
    break;
    
  case ADD_INT:
    add_mem_reg(offset, EBP, reg);
    break;

  case SUB_INT:
    sub_mem_reg(offset, EBP, reg);
    break;

  case MUL_INT:
    mul_mem_reg(offset, EBP, reg);
    break;

  case DIV_INT:
    div_mem_reg(offset, EBP, reg, false);
    break;
    
  case MOD_INT:
    div_mem_reg(offset, EBP, reg, true);
    break;

  case BIT_AND_INT:
    and_mem_reg(offset, EBP, reg);
    break;

  case BIT_OR_INT:
    or_mem_reg(offset, EBP, reg);
    break;

  case BIT_XOR_INT:
    xor_mem_reg(offset, EBP, reg);
    break;
    
  case LES_INT:
  case LES_EQL_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:  
  case GTR_EQL_INT:
    cmp_mem_reg(offset, EBP, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;

  default:
    break;
  }
}

void JitCompilerIA32::math_imm_xreg(RegInstr* instr, Register reg, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_imm_xreg(instr, reg);
    break;

  case SUB_FLOAT:
    sub_imm_xreg(instr, reg);
    break;

  case MUL_FLOAT:
    mul_imm_xreg(instr, reg);
    break;

  case DIV_FLOAT:
    div_imm_xreg(instr, reg);
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_imm_xreg(instr, reg);
    if(!cond_jmp(type)) {
      cmov_reg(reg, type);
    }
    break;
		
  default:
    break;
  }
}

void JitCompilerIA32::loop(int32_t offset)
{
  AddMachineCode(0xe2);
  AddMachineCode(offset);
}
 
RegisterHolder* JitCompilerIA32::call_xfunc(double (*func_ptr)(double), RegInstr* left)
{
  move_xreg_mem(XMM0, TMP_XMM_0, EBP);
  move_mem_xreg(left->GetOperand(), EBP, XMM0);

  RegisterHolder* call_holder = GetRegister();
  move_imm_reg((size_t)func_ptr, call_holder->GetRegister());
  call_reg(call_holder->GetRegister());
  ReleaseRegister(call_holder);

  RegisterHolder* result_holder = GetXmmRegister();
  if(result_holder->GetRegister() != XMM0) {
    move_xreg_xreg(XMM0, result_holder->GetRegister());
    move_mem_xreg(TMP_XMM_0, EBP, XMM0);
  }
  
  return result_holder;
}

RegisterHolder* JitCompilerIA32::call_xfunc2(double(*func_ptr)(double, double), RegInstr* left)
{
  RegInstr* right = working_stack.front();
  working_stack.pop_front();

#ifdef _DEBUG_JIT
  assert(right->GetType() == MEM_FLOAT);
#endif

  move_xreg_mem(XMM1, TMP_XMM_1, EBP);
  move_mem_xreg(left->GetOperand(), EBP, XMM1);

  move_xreg_mem(XMM0, TMP_XMM_0, EBP);
  move_mem_xreg(right->GetOperand(), EBP, XMM0);
  
  RegisterHolder* call_holder = GetRegister();
  move_imm_reg((size_t)func_ptr, call_holder->GetRegister());
  call_reg(call_holder->GetRegister());
  ReleaseRegister(call_holder);

  RegisterHolder* result_holder = GetXmmRegister();
  move_mem_xreg(TMP_XMM_1, EBP, XMM1);
  if(result_holder->GetRegister() != XMM0) {
    move_xreg_xreg(XMM0, result_holder->GetRegister());
    move_mem_xreg(TMP_XMM_0, EBP, XMM0);
  }

  delete right;
  right = nullptr;

  return result_holder;
}

void JitCompilerIA32::math_mem_xreg(int32_t offset, Register dest, InstructionType type) {
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(offset, EBP, holder->GetRegister());
  math_xreg_xreg(holder->GetRegister(), dest, type);
  ReleaseXmmRegister(holder);
}

void JitCompilerIA32::math_xreg_xreg(Register src, Register dest, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_xreg_xreg(src, dest);
    break;

  case SUB_FLOAT:
    sub_xreg_xreg(src, dest);
    break;

  case MUL_FLOAT:
    mul_xreg_xreg(src, dest);
    break;

  case DIV_FLOAT:
    div_xreg_xreg(src, dest);
    break;
    
  case LES_FLOAT:
  case LES_EQL_FLOAT:
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
  case GTR_EQL_FLOAT:
    cmp_xreg_xreg(src, dest);
    if(!cond_jmp(type)) {
      cmov_reg(dest, type);
    }
    break;
		
  default:
    break;
  }
}    

void JitCompilerIA32::cmp_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cmpll %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x39);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::cmp_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cmpl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x3b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitCompilerIA32::cmp_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cmpl $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  unsigned char code = 0xf8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerIA32::cmov_reg(Register reg, InstructionType oper) {
  // set register to 0; if eflag than set to 1
  move_imm_reg(0, reg);
  RegisterHolder* true_holder = GetRegister();
  move_imm_reg(1, true_holder->GetRegister());
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cmovl %" << GetRegisterName(reg) << L", %" 
        << GetRegisterName(true_holder->GetRegister()) << L" ]" << endl;
#endif
  // encode
  AddMachineCode(0x0f);
  switch(oper) {    
  case GTR_INT:
    AddMachineCode(0x4f);
    break;

  case LES_INT:
    AddMachineCode(0x4c);
    break;
    
  case EQL_INT:
  case EQL_FLOAT:
    AddMachineCode(0x44);
    break;

  case NEQL_INT:
  case NEQL_FLOAT:
    AddMachineCode(0x45);
    break;
    
  case LES_FLOAT:
    AddMachineCode(0x47);
    break;
    
  case GTR_FLOAT:
    AddMachineCode(0x47);
    break;

  case LES_EQL_INT:
    AddMachineCode(0x4e);
    break;

  case GTR_EQL_INT:
    AddMachineCode(0x4d);
    break;
    
  case LES_EQL_FLOAT:
    AddMachineCode(0x43);
    break;

  case GTR_EQL_FLOAT:
    AddMachineCode(0x43);
    break;

  default:
    wcerr << L">>> Unknown compare! <<<" << endl;
    exit(1);
    break;
  }
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, reg);
  RegisterEncode3(code, 5, true_holder->GetRegister());
  AddMachineCode(code);
  ReleaseRegister(true_holder);
}

void JitCompilerIA32::add_imm_mem(int32_t imm, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [addl $" << imm << L", " 
        << offset << L"(%"<< GetRegisterName(dest) << L")]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  AddMachineCode(ModRM(dest, EAX));
  // write value
  AddImm(offset);
  AddImm(imm);
}
    
void JitCompilerIA32::add_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [addl $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  unsigned char code = 0xc0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}
    
void JitCompilerIA32::add_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  add_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::sub_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  sub_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::div_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  div_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::mul_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  mul_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::add_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [addl %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x01);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::sub_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [subsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x5c);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);	     
}

void JitCompilerIA32::mul_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [mulsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x59);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerIA32::div_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [divsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x5e);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerIA32::add_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [addsd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x58);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}
    
void JitCompilerIA32::add_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [addl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x03);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::add_mem_xreg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [addsd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x58);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);  
}

void JitCompilerIA32::sub_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  sub_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseXmmRegister(holder);
}

void JitCompilerIA32::mul_mem_xreg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [mulsd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x59);
  AddMachineCode(ModRM(src, dest));
  AddImm(offset);
}

void JitCompilerIA32::div_mem_xreg(int32_t offset, Register src, Register dest) {
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(offset, src, holder->GetRegister());
  div_xreg_xreg(dest, holder->GetRegister());
  move_xreg_xreg(holder->GetRegister(), dest);
  ReleaseXmmRegister(holder);
}

void JitCompilerIA32::sub_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [subl $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  unsigned char code = 0xe8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm(imm);
}

void JitCompilerIA32::sub_imm_mem(int32_t imm, int32_t offset, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [subl $" << imm << L", " 
        << offset << L"(%"<< GetRegisterName(dest) << L")]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  AddMachineCode(ModRM(dest, EBP));
  // write value
  AddImm(offset);
  AddImm(imm);
}

void JitCompilerIA32::sub_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [subl %" << GetRegisterName(src)
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x29);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::sub_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [subl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif

  // encode
  AddMachineCode(0x2b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::mul_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [imul $" << imm 
        << L", %"<< GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x69);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, reg);
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerIA32::mul_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [imul %" 
        << GetRegisterName(src) << L", %"<< GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x0f);
  AddMachineCode(0xaf);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::mul_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [imul " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x0f);
  AddMachineCode(0xaf);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::div_imm_reg(int32_t imm, Register reg, bool is_mod) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  div_reg_reg(imm_holder->GetRegister(), reg, is_mod);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::div_mem_reg(int32_t offset, Register src, Register dest, bool is_mod) {
  if(is_mod) {
    if(dest != EDX) {
      move_reg_mem(EDX, TMP_REG_1, EBP);
    }
    move_reg_mem(EAX, TMP_REG_0, EBP);
  }
  else {
    if(dest != EAX) {
      move_reg_mem(EAX, TMP_REG_0, EBP);
    }
    move_reg_mem(EDX, TMP_REG_1, EBP);
  }

  // ============
  move_reg_reg(dest, EAX);
  AddMachineCode(0x99); // cdq
  
  // encode
  AddMachineCode(0xf7);
  AddMachineCode(ModRM(src, EDI));
  // write value
  AddImm(offset);
  
#ifdef _DEBUG_JIT
  if(is_mod) {
    wcout << L"  " << (++instr_count) << L": [imod " << offset << L"(%" 
          << GetRegisterName(src) << L")]" << endl;
  }
  else {
    wcout << L"  " << (++instr_count) << L": [idiv " << offset << L"(%" 
          << GetRegisterName(src) << L")]" << endl;
  }
#endif
  // ============

  if(is_mod) {
    if(dest != EDX) {
      move_reg_reg(EDX, dest);
      move_mem_reg(TMP_REG_1, EBP, EDX);
    }

    if(dest != EAX) {
      move_mem_reg(TMP_REG_0, EBP, EAX);
    }
  }
  else {
    if(dest != EAX) {
      move_reg_reg(EAX, dest);
      move_mem_reg(TMP_REG_0, EBP, EAX);
    }
    
    if(dest != EDX) {
      move_mem_reg(TMP_REG_1, EBP, EDX);
    }
  }
}

void JitCompilerIA32::div_reg_reg(Register src, Register dest, bool is_mod) {
  if(is_mod) {
    if(dest != EDX) {
      move_reg_mem(EDX, TMP_REG_1, EBP);
    }
    move_reg_mem(EAX, TMP_REG_0, EBP);
  }
  else {
    if(dest != EAX) {
      move_reg_mem(EAX, TMP_REG_0, EBP);
    }
    move_reg_mem(EDX, TMP_REG_1, EBP);
  }
  
  // ============
  move_reg_reg(dest, EAX);
  AddMachineCode(0x99); // cdq

  if(src != EAX && src != EDX) {
    // encode
    AddMachineCode(0xf7);
    unsigned char code = 0xf8;
    // write value
    RegisterEncode3(code, 5, src);
    AddMachineCode(code);
    
#ifdef _DEBUG_JIT
    if(is_mod) {
      wcout << L"  " << (++instr_count) << L": [imod %" 
            << GetRegisterName(src) << L"]" << endl;
    }
    else {
      wcout << L"  " << (++instr_count) << L": [idiv %" 
            << GetRegisterName(src) << L"]" << endl;
    }
#endif
  }
  else {
    // encode
    AddMachineCode(0xf7);
    AddMachineCode(ModRM(EBP, EDI));
    // write value
    if(src == EAX) {
      AddImm(TMP_REG_0);
    }
    else {
      AddImm(TMP_REG_1);
    }
    
#ifdef _DEBUG_JIT
    if(is_mod) {
      wcout << L"  " << (++instr_count) << L": [imod " << TMP_REG_0 << L"(%" 
            << GetRegisterName(EBP) << L")]" << endl;
    }
    else {
      wcout << L"  " << (++instr_count) << L": [idiv " << TMP_REG_0 << L"(%" 
            << GetRegisterName(EBP) << L")]" << endl;
    }
#endif
  }
  // ============
  
  if(is_mod) {
    if(dest != EDX) {
      move_reg_reg(EDX, dest);
      move_mem_reg(TMP_REG_1, EBP, EDX);
    }

    if(dest != EAX) {
      move_mem_reg(TMP_REG_0, EBP, EAX);
    }
  }
  else {
    if(dest != EAX) {
      move_reg_reg(EAX, dest);
      move_mem_reg(TMP_REG_0, EBP, EAX);
    }
    
    if(dest != EDX) {
      move_mem_reg(TMP_REG_1, EBP, EDX);
    }
  }
}

void JitCompilerIA32::dec_reg(Register dest) {
  unsigned char code = 0x48;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [decl %" 
        << GetRegisterName(dest) << L"]" << endl;
#endif
}

void JitCompilerIA32::dec_mem(int32_t offset, Register dest) {
  AddMachineCode(0xff);
  unsigned char code = 0x88;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [decl " << offset << L"(%" 
        << GetRegisterName(dest) << L")" << L"]" << endl;
#endif
}

void JitCompilerIA32::inc_mem(int32_t offset, Register dest) {
  AddMachineCode(0xff);
  unsigned char code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [incl " << offset << L"(%" 
        << GetRegisterName(dest) << L")" << L"]" << endl;
#endif
}

void JitCompilerIA32::shl_imm_reg(int32_t value, Register dest) {
  AddMachineCode(0xc1);
  unsigned char code = 0xe0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode(value);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [shl $" << value << L", %" 
        << GetRegisterName(dest) << L"]" << endl;
#endif
}

void JitCompilerIA32::shl_reg_reg(Register src, Register dest)
{
  Register old_dest;
  RegisterHolder* reg_holder = nullptr;
  if(dest == ECX) {
    reg_holder = GetRegister();
    old_dest = dest;
    dest = reg_holder->GetRegister();
    move_reg_reg(old_dest, dest);
  }
  
  if(src != ECX) {
    move_reg_mem(ECX, TMP_REG_0, EBP);
    move_reg_reg(src, ECX);
  }
  
  // --------------------

  // encode
  AddMachineCode(0xd3);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, ESP);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [shl %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif

  // --------------------
  
  if(src != ECX) {
    move_mem_reg(TMP_REG_0, EBP, ECX);
  }
  
  if(reg_holder) {
    move_reg_reg(dest, old_dest);
    ReleaseRegister(reg_holder);
  }
}

void JitCompilerIA32::shl_mem_reg(int32_t offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shl_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitCompilerIA32::shr_imm_reg(int32_t value, Register dest) {
  AddMachineCode(0xc1);
  unsigned char code = 0xe8;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode(value);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [shr $" << value << L", %" 
        << GetRegisterName(dest) << L"]" << endl;
#endif
}

void JitCompilerIA32::shr_reg_reg(Register src, Register dest)
{
  Register old_dest;
  RegisterHolder* reg_holder = nullptr;
  if(dest == ECX) {
    reg_holder = GetRegister();
    old_dest = dest;
    dest = reg_holder->GetRegister();
    move_reg_reg(old_dest, dest);
  }
  
  if(src != ECX) {
    move_reg_mem(ECX, TMP_REG_0, EBP);
    move_reg_reg(src, ECX);
  }
  
  // --------------------
  
  // encode
  AddMachineCode(0xd3);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, EBP);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [shr %" << GetRegisterName(ECX) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif

  // --------------------
  
  if(src != ECX) {
    move_mem_reg(TMP_REG_0, EBP, ECX);
  }
  
  if(reg_holder) {
    move_reg_reg(dest, old_dest);
    ReleaseRegister(reg_holder);
  }
}

void JitCompilerIA32::shr_mem_reg(int32_t offset, Register src, Register dest) 
{
  RegisterHolder* mem_holder = GetRegister();
  move_mem_reg(offset, src, mem_holder->GetRegister());
  shr_reg_reg(mem_holder->GetRegister(), dest);
  ReleaseRegister(mem_holder);
}

void JitCompilerIA32::push_mem(int32_t offset, Register dest) {
  AddMachineCode(0xff);
  unsigned char code = 0xb0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [pushl " << offset << L"(%" 
        << GetRegisterName(dest) << L")" << L"]" << endl;
#endif
}

void JitCompilerIA32::push_reg(Register reg) {
  unsigned char code = 0x50;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [pushl %" << GetRegisterName(reg) 
        << L"]" << endl;
#endif
}

void JitCompilerIA32::push_imm(int32_t value) {
  AddMachineCode(0x68);
  AddImm(value);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [pushl $" << value << L"]" << endl;
#endif
}

void JitCompilerIA32::pop_reg(Register reg) {
  unsigned char code = 0x58;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [popl %" << GetRegisterName(reg) 
        << L"]" << endl;
#endif
}

void JitCompilerIA32::call_reg(Register reg) {
  AddMachineCode(0xff);
  unsigned char code = 0xd0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [call %" << GetRegisterName(reg)
        << L"]" << endl;
#endif
}

void JitCompilerIA32::cmp_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [ucomisd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x66);
  AddMachineCode(0x0f);
  AddMachineCode(0x2e);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerIA32::cmp_mem_xreg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [ucomisd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x66);
  AddMachineCode(0x0f);
  AddMachineCode(0x2e);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::cmp_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cmp_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::cvt_xreg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cvtsd2si %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x2c);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerIA32::round_imm_xreg(RegInstr* instr, Register reg, bool is_floor) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  round_mem_xreg(0, imm_holder->GetRegister(), reg, is_floor);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::round_mem_xreg(int32_t offset, Register src, Register dest, bool is_floor) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << (is_floor ? ": [floor " : ": [ceil ") 
        << offset << L"(%" << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  
  AddMachineCode(0x66);
  AddMachineCode(0x0f);
  AddMachineCode(0x3a);
  AddMachineCode(0x0b);
  // memory
  AddMachineCode(ModRM(src, dest));
  AddImm(offset);
  // mode
  if(is_floor) {
    AddMachineCode(0x03);
  }
  else {
    AddMachineCode(0x05);
  }
}

void JitCompilerIA32::round_xreg_xreg(Register src, Register dest, bool is_floor) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << (is_floor ? ": [floor %" : ": [ceil %") 
        << GetRegisterName(src) << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  
  AddMachineCode(0x66);
  AddMachineCode(0x0f);
  AddMachineCode(0x3a);
  AddMachineCode(0x0b);
  // registers
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  // mode
  if(is_floor) {
    AddMachineCode(0x03);
  }
  else {
    AddMachineCode(0x05);
  }
}

void JitCompilerIA32::cvt_imm_reg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cvt_mem_reg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::cvt_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cvtsd2si " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x2c);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::cvt_reg_xreg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cvtsi2sd %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x2a);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerIA32::cvt_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cvt_reg_xreg(imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerIA32::cvt_mem_xreg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [cvtsi2sd " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(0x0f);
  AddMachineCode(0x2a);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::and_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [andl $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  unsigned char code = 0xe0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerIA32::and_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [andl %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x21);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::and_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [andl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x23);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::or_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [orl $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  unsigned char code = 0xc8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}


void JitCompilerIA32::or_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [orl %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x09);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::or_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [orl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x0b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::xor_imm_reg(int32_t imm, Register reg) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [xorl $" << imm << L", %"
        << GetRegisterName(reg) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x81);
  unsigned char code = 0xf0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerIA32::xor_reg_reg(Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [xor %" << GetRegisterName(src) 
        << L", %" << GetRegisterName(dest) << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x31);
  unsigned char code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerIA32::xor_mem_reg(int32_t offset, Register src, Register dest) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [xorl " << offset << L"(%" 
        << GetRegisterName(src) << L"), %" << GetRegisterName(dest) 
        << L"]" << endl;
#endif
  // encode
  AddMachineCode(0x33);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

// --- x87 ---

void JitCompilerIA32::fld_mem(int32_t offset, Register src) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [fld " << offset << L"(%" 
        << GetRegisterName(src) << L")]" << endl;
#endif
  // encode
  AddMachineCode(0xdd);
  AddMachineCode(ModRM(src, EAX));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::fstp_mem(int32_t offset, Register src) {
#ifdef _DEBUG_JIT
  wcout << L"  " << (++instr_count) << L": [fld " << offset << L"(%" 
        << GetRegisterName(src) << L")]" << endl;
#endif
  // encode
  AddMachineCode(0xdd);
  AddMachineCode(ModRM(src, EBX));
  // write value
  AddImm(offset);
}

void JitCompilerIA32::fsin() {
  AddMachineCode(0xd9);
  AddMachineCode(0xfe);
}

void JitCompilerIA32::fcos() {
  AddMachineCode(0xd9);
  AddMachineCode(0xff);
}

void JitCompilerIA32::ftan() {
  AddMachineCode(0xd9);
  AddMachineCode(0xf2);
  AddMachineCode(0xdd);
  AddMachineCode(0xd8);
}

void JitCompilerIA32::fsqrt() {
  AddMachineCode(0xd9);
  AddMachineCode(0xfa);
}

unsigned JitCompilerIA32::ModRM(Register eff_adr, Register mod_rm)
{
  unsigned byte = 0;

  switch(mod_rm) {
  case ESP:
  case XMM4:
    byte = 0xa0;
    break;

  case EAX:
  case XMM0:
    byte = 0x80;
    break;

  case EBX:
  case XMM3:
    byte = 0x98;
    break;

  case ECX:
  case XMM1:
    byte = 0x88;
    break;

  case EDX:
  case XMM2:
    byte = 0x90;
    break;

  case EDI:
  case XMM7:
    byte = 0xb8;
    break;

  case ESI:
  case XMM6:
    byte = 0xb0;
    break;

  case EBP:
  case XMM5:
    byte = 0xa8;
    break;
  }

  switch(eff_adr) {
  case EAX:
  case XMM0:
    break;

  case EBX:
  case XMM3:
    byte += 3;
    break;

  case ECX:
  case XMM1:
    byte += 1;
    break;

  case EDX:
  case XMM2:
    byte += 2;
    break;

  case EDI:
  case XMM7:
    byte += 7;
    break;

  case ESI:
  case XMM6:
    byte += 6;
    break;

  case EBP:
  case XMM5:
    byte += 5;
    break;

  case XMM4:
    byte += 4;
    break;

    // should never happen for esp
  case ESP:
    wcerr << L">>> invalid register reference <<<" << endl;
    exit(1);
    break;
  }

  return byte;
}

std::wstring JitCompilerIA32::GetRegisterName(Register reg)
{
  switch(reg) {
  case EAX:
    return L"eax";

  case EBX:
    return L"ebx";

  case ECX:
    return L"ecx";

  case EDX:
    return L"edx";

  case EDI:
    return L"edi";

  case ESI:
    return L"esi";

  case EBP:
    return L"ebp";

  case ESP:
    return L"esp";

  case XMM0:
    return L"xmm0";

  case XMM1:
    return L"xmm1";

  case XMM2:
    return L"xmm2";

  case XMM3:
    return L"xmm3";

  case XMM4:
    return L"xmm4";

  case XMM5:
    return L"xmm5";

  case XMM6:
    return L"xmm6";

  case XMM7:
    return L"xmm7";
  }

  return L"unknown";
}

void JitCompilerIA32::RegisterEncode3(unsigned char& code, int32_t offset, Register reg)
{
#ifdef _DEBUG_JIT
  assert(offset == 2 || offset == 5);
#endif

  unsigned char reg_id;
  switch(reg) {
  case EAX:
  case XMM0:
    reg_id = 0x0;
    break;

  case EBX:
  case XMM3:
    reg_id = 0x3;
    break;

  case ECX:
  case XMM1:
    reg_id = 0x1;
    break;

  case EDX:
  case XMM2:
    reg_id = 0x2;
    break;

  case EDI:
  case XMM7:
    reg_id = 0x7;
    break;

  case ESI:
  case XMM6:
    reg_id = 0x6;
    break;

  case ESP:
  case XMM4:
    reg_id = 0x4;
    break;

  case EBP:
  case XMM5:
    reg_id = 0x5;
    break;

  default:
    wcerr << L"internal error" << endl;
    exit(1);
    break;
  }

  if(offset == 2) {
    reg_id = reg_id << 3;
  }
  code = code | reg_id;
}

void JitCompilerIA32::JitStackCallback(const int32_t instr_id, StackInstr* instr, const int32_t cls_id, const int32_t mthd_id, int32_t* inst, size_t* op_stack, int32_t* stack_pos, StackFrame** call_stack, long* call_stack_pos, const int32_t ip)
{
#ifdef _DEBUG_JIT
  wcout << L"Stack Call: instr=" << instr_id
    << L", oper_1=" << instr->GetOperand() << L", oper_2=" << instr->GetOperand2()
    << L", oper_3=" << instr->GetOperand3() << L", self=" << inst << L"(" << (size_t)inst << L"), stack="
    << op_stack << L", stack_addr=" << stack_pos << L", stack_pos=" << (*stack_pos) << endl;
#endif
  switch(instr_id) {
  case MTHD_CALL:
  case DYN_MTHD_CALL: {
#ifdef _DEBUG_JIT
    wcout << L"jit oper: MTHD_CALL: cls=" << instr->GetOperand() << L", mthd=" << instr->GetOperand2() << endl;
#endif
    StackInterpreter intpr(call_stack, call_stack_pos);
    intpr.Execute(op_stack, (long*)stack_pos, ip, program->GetClass(cls_id)->GetMethod(mthd_id), (size_t*)inst, true);
  }
    break;

  case LOAD_ARY_SIZE: {
    size_t* array = (size_t*)PopInt(op_stack, stack_pos);
    if(!array) {
      wcerr << L"Attempting to dereference a 'Nil' memory instance" << endl;
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

#ifdef _DEBUG_JIT
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
    int32_t* mem = (int32_t*)MemoryManager::AllocateArray(size + ((dim + 2) * sizeof(int32_t)), CHAR_ARY_TYPE, op_stack, *stack_pos);
    mem[0] = size - 1;
    mem[1] = dim;
    memcpy(mem + 2, indices, dim * sizeof(int32_t));
    PushInt(op_stack, stack_pos, (int32_t)mem);

#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
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
#ifdef _DEBUG_JIT
    wcout << L"jit oper: NEW_OBJ_INST: id=" << instr->GetOperand() << endl;
#endif
    int32_t* mem = (int32_t*)MemoryManager::AllocateObject(instr->GetOperand(), op_stack, *stack_pos);
    PushInt(op_stack, stack_pos, (int32_t)mem);
  }
    break;

  case I2S: {
    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      const size_t base = PopInt(op_stack, stack_pos);
      const long value = (long)PopInt(op_stack, stack_pos);
    
      wstringstream stream;
      if(base == 16) {
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
#ifdef _DEBUG_JIT
    wcout << L"stack oper: S2I; call_pos=" << (*call_stack_pos) << endl;
#endif

    size_t* str_ptr = (size_t*)PopInt(op_stack, stack_pos);
    long base = (long)PopInt(op_stack, stack_pos);
    if(str_ptr) {
      wchar_t* str = (wchar_t*)(str_ptr + 3);
      try {
        if(wcslen(str) > 2) {
          switch(str[1]) {
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
      catch(std::invalid_argument &e) {
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
    size_t* result = MemoryManager::ValidObjectCast(mem, instr->GetOperand(), program->GetHierarchy(), program->GetInterfaces());
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
#ifdef _DEBUG_JIT
    wcout << L"jit oper: OBJ_INST_CAST: from=" << mem << L", to=" << to_id << endl;
#endif	
    int32_t result = (int32_t)MemoryManager::ValidObjectCast((size_t*)mem, to_id, program->GetHierarchy(), program->GetInterfaces());
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
      wcerr << L"Attempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
#ifdef _WIN32
    HANDLE vm_thread = (HANDLE)instance[0];
    if(WaitForSingleObject(vm_thread, INFINITE) != WAIT_OBJECT_0) {
      wcerr << L"Unable to join thread!" << endl;
      exit(-1);
    }
#else
    void* status;
    pthread_t vm_thread = (pthread_t)instance[0];
    if(pthread_join(vm_thread, &status)) {
      wcerr << L"Unable to join thread!" << endl;
      exit(-1);
    }
#endif
  }
                    break;

  case THREAD_SLEEP:
#ifdef _WIN32
    Sleep(PopInt(op_stack, stack_pos));
#else
    usleep(PopInt(op_stack, stack_pos) * 1000);
#endif
    break;

  case THREAD_MUTEX: {
    int32_t* instance = inst;
    if(!instance) {
      wcerr << L"Attempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
#ifdef _WIN32
    InitializeCriticalSection((CRITICAL_SECTION*)& instance[1]);
#else
    pthread_mutex_init((pthread_mutex_t*)& instance[1], nullptr);
#endif
  }
                     break;

  case CRITICAL_START: {
    int32_t* instance = (int32_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      wcerr << L"Attempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
#ifdef _WIN32
    EnterCriticalSection((CRITICAL_SECTION*)& instance[1]);
#else     
    pthread_mutex_lock((pthread_mutex_t*)& instance[1]);
#endif
  }
     break;

  case CRITICAL_END: {
    int32_t* instance = (int32_t*)PopInt(op_stack, stack_pos);
    if(!instance) {
      wcerr << L"Attempting to dereference a 'Nil' memory instance" << endl;
      wcerr << L"  native method: name=" << program->GetClass(cls_id)->GetMethod(mthd_id)->GetName() << endl;
      exit(1);
    }
#ifdef _WIN32
    LeaveCriticalSection((CRITICAL_SECTION*)& instance[1]);
#else     
    pthread_mutex_unlock((pthread_mutex_t*)& instance[1]);
#endif
  }
                     break;

                     // ---------------- memory copy ----------------
  case CPY_BYTE_ARY: {
    long length = PopInt(op_stack, stack_pos);
    const long src_offset = PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << endl;
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
    long length = PopInt(op_stack, stack_pos);
    const long src_offset = PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << endl;
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
    long length = PopInt(op_stack, stack_pos);
    const long src_offset = PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << endl;
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
    long length = PopInt(op_stack, stack_pos);
    const long src_offset = PopInt(op_stack, stack_pos);
    size_t* src_array = (size_t*)PopInt(op_stack, stack_pos);
    const long dest_offset = PopInt(op_stack, stack_pos);
    size_t* dest_array = (size_t*)PopInt(op_stack, stack_pos);

    if(!src_array || !dest_array) {
      wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << endl;
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

#ifdef _DEBUG_JIT
  default:
    wcerr << L"Unknown callback!" << endl;
    break;

    wcout << L"  ending stack: pos=" << (*stack_pos) << endl;
#endif
  }
}

RegisterHolder* JitCompilerIA32::ArrayIndex(StackInstr* instr, MemoryType type)
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
    move_mem_reg(holder->GetOperand(), EBP, array_holder->GetRegister());
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
    move_mem_reg(holder->GetOperand(), EBP, index_holder->GetRegister());
    break;

  default:
    wcerr << L"internal error" << endl;
    exit(1);
    break;
  }

  const int32_t dim = instr->GetOperand();
  for(int i = 1; i < dim; ++i) {
    // index *= array[i];
    mul_mem_reg((i + 2) * sizeof(int32_t), array_holder->GetRegister(), index_holder->GetRegister());
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
      add_reg_reg(holder->GetRegister()->GetRegister(), index_holder->GetRegister());
      break;

    case MEM_INT:
      add_mem_reg(holder->GetOperand(), EBP, index_holder->GetRegister());
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

  // ajust indices
  switch(type) {
  case BYTE_ARY_TYPE:
    break;

#ifdef _WIN32
  case CHAR_ARY_TYPE:
    shl_imm_reg(1, index_holder->GetRegister());
    shl_imm_reg(1, bounds_holder->GetRegister());
    break;
#else
  case CHAR_ARY_TYPE:
#endif
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

void JitCompilerIA32::ProcessIndices()
{
#ifdef _DEBUG_JIT
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

  int32_t index = TMP_REG_5;
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
#ifdef _DEBUG_JIT
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
  local_space = -(index + TMP_REG_5);

#ifdef _DEBUG_JIT
  wcout << L"Local space required: " << (local_space + 8) << L" byte(s)" << endl;
#endif
}

bool JitCompilerIA32::Compile(StackMethod* cm)
{
  compile_success = true;

  if(!cm->GetNativeCode()) {
    skip_jump = false;
    method = cm;

    int32_t cls_id = method->GetClass()->GetId();
    int32_t mthd_id = method->GetId();
#ifdef _DEBUG_JIT
    wcout << L"---------- Compiling Native Code: method_id=" << cls_id << L","
      << mthd_id << L"; mthd_name='" << method->GetName() << L"'; params="
      << method->GetParamCount() << L" ----------" << endl;
#endif

    code_buf_max = BUFFER_SIZE;
    code = (unsigned char*)malloc(code_buf_max);
#ifdef _WIN32
    float_consts = new double[MAX_DBLS];
#else
    if(posix_memalign((void**)& float_consts, PAGE_SIZE, sizeof(double) * MAX_DBLS)) {
      wcerr << L"Unable to allocate JIT memory!" << endl;
      exit(1);
    }
#endif

    local_space = floats_index = instr_index = code_index = epilog_index = instr_count = 0;
    // general use registers
    reg_eax = new RegisterHolder(EAX);
    aval_regs.push_back(new RegisterHolder(EDX));
    aval_regs.push_back(new RegisterHolder(ECX));
    aval_regs.push_back(new RegisterHolder(EBX));
    aval_regs.push_back(reg_eax);
    // aux general use registers
    aux_regs.push(new RegisterHolder(EDI));
    aux_regs.push(new RegisterHolder(ESI));
    // floating point registers
    aval_xregs.push_back(new RegisterHolder(XMM7));
    aval_xregs.push_back(new RegisterHolder(XMM6));
    aval_xregs.push_back(new RegisterHolder(XMM5));
    aval_xregs.push_back(new RegisterHolder(XMM4));
    aval_xregs.push_back(new RegisterHolder(XMM3));
    aval_xregs.push_back(new RegisterHolder(XMM2));
    aval_xregs.push_back(new RegisterHolder(XMM1));
    aval_xregs.push_back(new RegisterHolder(XMM0));
#ifdef _DEBUG_JIT
    wcout << L"Compiling code for IA-32 architecture..." << endl;
#endif

    // process offsets
    ProcessIndices();
    // setup
    Prolog();
    // method information
    move_imm_mem(cls_id, CLS_ID, EBP);
    move_imm_mem(mthd_id, MTHD_ID, EBP);
    // register root
    RegisterRoot();
    // translate parameters
    ProcessParameters(method->GetParamCount());
    // translate program
    ProcessInstructions();
    if(!compile_success) {
      delete[] float_consts;
      float_consts = nullptr;

      return false;
    }

    // show content
    unordered_map<int32_t, StackInstr*>::iterator iter;
    for(iter = jump_table.begin(); iter != jump_table.end(); ++iter) {
      StackInstr* instr = iter->second;
      const int32_t src_offset = iter->first;
      const int32_t dest_index = instr->GetOperand();
      const int32_t dest_offset = method->GetInstruction(dest_index)->GetOffset();
      const int32_t offset = dest_offset - src_offset - 4;
      memcpy(&code[src_offset], &offset, 4);
#ifdef _DEBUG_JIT
      wcout << L"jump update: src=" << src_offset << L"; dest=" << dest_offset << endl;
#endif
    }

    for(size_t i = 0; i < deref_offsets.size(); ++i) {
      const int32_t index = deref_offsets[i];
      int32_t offset = epilog_index - index + 1;
      memcpy(&code[index], &offset, 4);
    }

    for(size_t i = 0; i < bounds_less_offsets.size(); ++i) {
      const int32_t index = bounds_less_offsets[i];
      int32_t offset = epilog_index - index + 11;
      memcpy(&code[index], &offset, 4);
    }

    for(size_t i = 0; i < bounds_greater_offsets.size(); ++i) {
      const int32_t index = bounds_greater_offsets[i];
      int32_t offset = epilog_index - index + 21;
      memcpy(&code[index], &offset, 4);
    }

#ifdef _DEBUG_JIT
    wcout << L"Caching JIT code: actual=" << code_index << L", buffer=" << code_buf_max << L" byte(s)" << endl;
#endif
    // store compiled code
    method->SetNativeCode(new NativeCode(page_manager->GetPage(code, code_index), code_index, float_consts));
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
  const long cls_id = method->GetClass()->GetId();
  const long mthd_id = method->GetId();
  NativeCode* native_code = method->GetNativeCode();

#ifdef _DEBUG_JIT
  wcout << L"=== MTHD_CALL (native): id=" << cls_id << L"," << mthd_id << L"; name='" << method->GetName()
        << L"'; self=" << inst << L"(" << (size_t)inst << L"); stack=" << op_stack << L"; stack_pos="
        << (*stack_pos) << L"; params=" << method->GetParamCount() << L"; code=" << (size_t*)native_code->GetCode() << L"; code_index="
        << native_code->GetSize() << L" ===" << endl;
  assert((*stack_pos) >= method->GetParamCount());
#endif

  // create function
  jit_fun_ptr jit_fun = (jit_fun_ptr)native_code->GetCode();

  // execute
  const long status = jit_fun(cls_id, mthd_id, method->GetClass()->GetClassMemory(), inst, op_stack,
                              stack_pos, call_stack, call_stack_pos, &(frame->jit_mem), &(frame->jit_offset));

#ifdef _DEBUG_JIT
  wcout << L"JIT return=: " << status << endl;
#endif 

  return status;
}

/********************************
 * PageManager class
 ********************************/
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

unsigned char* PageManager::GetPage(unsigned char* code, int32_t size)
{
  bool placed = false;

  unsigned char* temp = nullptr;
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
