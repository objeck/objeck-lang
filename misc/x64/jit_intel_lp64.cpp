/***************************************************************************
 * JIT compiler for the x64 architecture.
 *
 * Copyright (c) 2008, 2009, 2010 Randy Hollines
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
 * - Neither the name of the StackVM Team nor the names of its 
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

#include "instr_conv_amd64.h"
#include "common.h"
#include <string>

using namespace Runtime;

StackProgram* JitCompilerAmd64::prgm;

void JitCompilerAmd64::Initialize(StackProgram* p) {
  prgm = p;
}

void JitCompilerAmd64::Prolog() {
  BYTE setup_code[] = {
    0x48, 0x55,       // push %ebp
    0x48, 0x89, 0xe5, // mov  %esp, %ebp
    0x48, 0x53,       // push %rbx
    0x49, 0x52,       // push %r10
    0x49, 0x55,       // push %r13
    0x49, 0x56,       // push %r14
    0x49, 0x57,       // push %r15
  };
  const INT_LIT setup_size = sizeof(setup_code);
  // copy setup
  for(INT_LIT i = 0; i < setup_size; i++) {
    AddMachineCode(setup_code[i]);
  }
}

void JitCompilerAmd64::Epilog() {
  BYTE teardown_code[] = {
    // restore registers
    0x49, 0x5f,        // pop %r15  
    0x49, 0x5e,        // pop %r14
    0x49, 0x5d,        // pop %r13  
    0x49, 0x5a,        // pop %r10
    0x48, 0x5b,        // pop %rbx
    0x48, 0x89, 0xec,  // mov  %ebp, %esp
    0x48, 0x5d,        // pop %ebp
    0xc3               // rtn
  };
  const INT_LIT teardown_size = sizeof(teardown_code);
  // copy teardown
  for(INT_LIT i = 0; i < teardown_size; i++) {
    AddMachineCode(teardown_code[i]);
  }
}

void JitCompilerAmd64::ExecuteMachineCode(INT_LIT cls_id, INT_LIT mthd_id, 
				     void* inst, BYTE* code, 
				     const INT_LIT code_size, 
				     INT_LIT* op_stack, INT_LIT& stack_pos) {
  // create function
  jit_fun_ptr jit_fun = (jit_fun_ptr)code;
  mprotect((void*)jit_fun, code_size, PROT_EXEC);
  // execute
  jit_fun(cls_id, mthd_id, inst, op_stack, stack_pos);
}

void JitCompilerAmd64::ProcessParameters(INT_LIT count) {
#ifdef _DEBUG
  cout << "MTHD_PARMS" << endl;
#endif
  move_reg_mem(RCX, OP_STACK, RBP);
  move_reg_mem(R8, STACK_POS, RBP);
  
  INT_LIT index = 0;
  INT_LIT offset = TMP_POS_2 - sizeof(INT_LIT);
  while(index < count) {
    StackInstr* instr = mthd->GetInstruction(instr_index++);
    instr->SetOffset(code_index);  
    
    if(instr->GetType() == STOR_INT_VAR) {
      move_mem_reg(index, RCX, RBX);
      move_reg_mem(RBX, offset, RBP);
    }
    else {
      move_mem_xreg(index, RCX, XMM0);
      move_xreg_mem(XMM0, offset, RBP);
    }
    // update
    dec_mem(0, R8);
    index++;
    offset -= sizeof(INT_LIT);
  }
}

void JitCompilerAmd64::ProcessReturnParameter() {
#ifdef _DEBUG
  cout << "RTRN_PARM" << endl;
#endif
  RegisterHolder* op_stack_holder = GetRegister();
  move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());
  
  RegisterHolder* stack_pos_holder = GetRegister();
  move_mem_reg(STACK_POS, RBP, stack_pos_holder->GetRegister());
  
  dec_mem(0, stack_pos_holder->GetRegister());  
  move_mem_reg(0, stack_pos_holder->GetRegister(), 
	       stack_pos_holder->GetRegister());
  shl_reg(stack_pos_holder->GetRegister(), 3);
  add_reg_reg(stack_pos_holder->GetRegister(),
	      op_stack_holder->GetRegister());  
  move_mem_reg(0, op_stack_holder->GetRegister(),
	       op_stack_holder->GetRegister());
  working_stack.push(new RegInstr(op_stack_holder));
  
  ReleaseRegister(stack_pos_holder);
  ReleaseRegister(op_stack_holder);
}

void JitCompilerAmd64::ProcessInstructions() {
  while(instr_index < mthd->GetInstructionCount()) {
    StackInstr* instr = mthd->GetInstruction(instr_index++);
    instr->SetOffset(code_index);
    
    switch(instr->GetType()) {
      // load literal
    case LOAD_INT_LIT:
#ifdef _DEBUG
      cout << "LOAD_INT_LIT: " << instr->GetOperand() 
	   << "; avail regs: " << aval_regs.size() << endl;
#endif
      working_stack.push(new RegInstr(instr));
      break;

      // float literal
    case LOAD_FLOAT_LIT:
#ifdef _DEBUG
      cout << "LOAD_FLOAT_LIT: " << instr->GetFloatOperand() 
	   << "; avail regs: " << aval_regs.size() << endl;
#endif
      floats[floats_index] = instr->GetFloatOperand();
      working_stack.push(new RegInstr(instr, &floats[floats_index++]));
      break;

      // load self
    case LOAD_INST_MEM:
#ifdef _DEBUG
      cout << "LOAD_INST_MEM" << endl;
#endif
      working_stack.push(new RegInstr(instr));
      break;
      
      // load variable
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
#ifdef _DEBUG
      cout << "LOAD_INT_VAR/LOAD_FLOAT_VAR: " 
	   << instr->GetOperand() << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessLoad(instr);
      break;
    
      // store value
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
#ifdef _DEBUG
      cout << "STOR_INT_VAR/STOR_FLOAT_VAR: " << instr->GetOperand() 
	   << "; avail regs: " << aval_regs.size() << endl;
#endif
      ProcessStore(instr);
      break;
      
    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case LES_INT:
    case GTR_INT:
    case EQL_INT:
    case NEQL_INT:
#ifdef _DEBUG
      cout << "INT ADD/SUB/MUL/DIV/MOD/LES/GTR/EQL/NEQL" 
	   << "; avail regs: " << aval_regs.size() << endl;
#endif
      ProcessIntCalculation(instr);
      break;
      
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
#ifdef _DEBUG
      cout << "FLOAT ADD/SUB/MUL/DIV/" 
	   << "; avail regs: " << aval_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);
      break;

    case LES_FLOAT:
    case GTR_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT: {
#ifdef _DEBUG
      cout << "FLOAT LES/GTR/EQL/NEQL" 
	   << "; avail regs: " << aval_regs.size() << endl;
#endif
      ProcessFloatCalculation(instr);

      RegInstr* left = working_stack.top();
      working_stack.pop(); // pop invalid xmm register
      ReleaseXmmRegister(left->GetRegister());

      delete left;
      left = NULL;
      
      RegisterHolder* holder = GetRegister();
      cmov_reg(holder->GetRegister(), instr->GetType());
      working_stack.push(new RegInstr(holder));
      
    }
      break;
      
    case RTRN:
#ifdef _DEBUG
      cout << "RTRN" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessReturn();
      break;
      
    case MTHD_CALL:
#ifdef _DEBUG
      cout << "MTHD_CALL" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(MTHD_CALL, instr_index, 1);
      break;

    case EXT_VOID_CALL:
#ifdef _DEBUG
      cout << "EXT_VOID_CALL" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(EXT_VOID_CALL, instr_index, 2);
      break;

    case EXT_RTRN_CALL:
#ifdef _DEBUG
      cout << "EXT_RTRN_CALL" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(EXT_RTRN_CALL, instr_index, 2);
      ProcessReturnParameter();
      break;
      
    case NEW_INT_ARY:
#ifdef _DEBUG
      cout << "NEW_ARY" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(NEW_INT_ARY, instr_index, 1);
      ProcessReturnParameter();
      break;

    case NEW_FLOAT_ARY:
#ifdef _DEBUG
      cout << "NEW_ARY" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(NEW_FLOAT_ARY, instr_index, 1);
      ProcessReturnParameter();
      break;

    case NEW_OBJ_INST: {
#ifdef _DEBUG
      cout << "NEW_OBJ_INST" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      RegisterHolder* cls_holder = GetRegister();
      move_imm_reg(instr->GetOperand(), cls_holder->GetRegister());
      working_stack.push(new RegInstr(cls_holder));
      ProcessStackCall(NEW_OBJ_INST, instr_index, 1);
      ProcessReturnParameter();
    }
      break;

    case STD_OUT_BYTE:
#ifdef _DEBUG
      cout << "STD_OUT_BYTE" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(STD_OUT_BYTE, instr_index, 1);
      break;
      
    case STD_OUT_INT:
#ifdef _DEBUG
      cout << "STD_OUT_INT" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(STD_OUT_INT, instr_index, 1);
      break;
      
    case STD_OUT_FLOAT:
#ifdef _DEBUG
      cout << "STD_OUT_FLOAT" << "; avail regs: " << aval_regs.size() 
	   << endl;
#endif
      ProcessStackCall(STD_OUT_FLOAT, instr_index, 1);
      break;
      
    case STOR_BYTE_ARY_ELM:
#ifdef _DEBUG
      cout << "STOR_BYTE_ARY_ELM" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessStoreByteElement(instr);
      break;
      
    case STOR_INT_ARY_ELM:
#ifdef _DEBUG
      cout << "STOR_INT_ARY_ELM" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessStoreIntElement(instr);
      break;

    case STOR_FLOAT_ARY_ELM:
#ifdef _DEBUG
      cout << "STOR_FLOAT_ARY_ELM" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessStoreFloatElement(instr);
      break;

    case F2I:
#ifdef _DEBUG
      cout << "F2I" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessFloatToInt(instr);
      break;

    case I2F:
#ifdef _DEBUG
      cout << "I2F" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessIntToFloat(instr);
      break;

    case LOAD_BYTE_ARY_ELM:
#ifdef _DEBUG
      cout << "LOAD_BYTE_ARY_ELM" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessLoadByteElement(instr);
      break;
      
    case LOAD_INT_ARY_ELM:
#ifdef _DEBUG
      cout << "LOAD_INT_ARY_ELM" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessLoadIntElement(instr);
      break;

    case LOAD_FLOAT_ARY_ELM:
#ifdef _DEBUG
      cout << "LOAD_FLOAT_ARY_ELM" << "; avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessLoadFloatElement(instr);
      break;
      
    case JMP:
#ifdef _DEBUG
      cout << "JMP: avail regs: " 
	   << aval_regs.size() << endl;
#endif
      ProcessJump(instr);
      break;
      
    case LBL:
      break;
      
    default:
      cerr << "Unknown instruction: " << instr->GetType() << "!" << endl;
      exit(1);
    }
  }
}

void JitCompilerAmd64::ProcessLoad(StackInstr* instr) {
  if(instr->GetOperand2() == LOCL) {
    working_stack.push(new RegInstr(instr));
  }
  else {
    RegisterHolder* obj_holder = GetRegister();
    RegInstr* left = working_stack.top();
    move_mem_reg(left->GetOperand(), 
		 RBP, obj_holder->GetRegister());
    working_stack.pop();
    // INT_LIT value
    if(instr->GetType() == LOAD_INT_VAR) {
      move_mem_reg(instr->GetOperand3(), 
		   obj_holder->GetRegister(), 
		   obj_holder->GetRegister());
      working_stack.push(new RegInstr(obj_holder));
    }
    // float value
    else {
      RegisterHolder* xmm_holder = GetXmmRegister();
      move_mem_xreg(instr->GetOperand3(),
		    obj_holder->GetRegister(), 
		    xmm_holder->GetRegister());
      ReleaseRegister(obj_holder);
      working_stack.push(new RegInstr(xmm_holder));	  
    }

    delete left;
    left = NULL;
  }
}

void JitCompilerAmd64::ProcessJump(StackInstr* instr) {
  if(instr->GetOperand2() < 0) {
    AddMachineCode(0xe9);
  }
  else {
    RegInstr* left = working_stack.top();
    working_stack.pop(); // 1 byte compare with register
    cmp_imm_reg(1, left->GetRegister()->GetRegister());
    AddMachineCode(0x0f);
    AddMachineCode(0x85);
    // clean up
    delete left;
    left = NULL;
  }
  // store update index
  jump_table.insert(pair<int, StackInstr*>(code_index, instr));
  // temp offset
  AddImm(0);
}

void JitCompilerAmd64::ProcessLoadByteElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayOffset(instr, BYTE_TYPE);
  move_mem8_reg(0, elem_holder->GetRegister(), elem_holder->GetRegister());
  working_stack.push(new RegInstr(elem_holder));
}

void JitCompilerAmd64::ProcessLoadIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayOffset(instr, INT_TYPE);
  move_mem_reg(0, elem_holder->GetRegister(), elem_holder->GetRegister());
  working_stack.push(new RegInstr(elem_holder));
}

void JitCompilerAmd64::ProcessLoadFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayOffset(instr, FLOAT_TYPE);
  RegisterHolder* holder = GetXmmRegister();
  move_mem_xreg(0, elem_holder->GetRegister(), holder->GetRegister());
  working_stack.push(new RegInstr(holder));
  ReleaseRegister(elem_holder);
}

void JitCompilerAmd64::ProcessStoreByteElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayOffset(instr, BYTE_TYPE);
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  switch(left->GetType()) {
  case IMM_32:
    move_imm_mem8(left->GetOperand(), 0, elem_holder->GetRegister());
    ReleaseRegister(elem_holder);
    break;

  case MEM_32: {
    RegisterHolder* holder = GetRegister();
    move_mem8_reg(left->GetOperand(), RBP, holder->GetRegister());
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;

  case REG_32: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem8(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
    ReleaseRegister(elem_holder);
  }
    break;
  }
  
  delete left;
  left = NULL;
}

void JitCompilerAmd64::ProcessStoreIntElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayOffset(instr, INT_TYPE);
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  switch(left->GetType()) {
  case IMM_32:
    move_imm_mem(left->GetOperand(), 0, elem_holder->GetRegister());
    break;

  case MEM_32: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg(left->GetOperand(), 
		 RBP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
  }
    break;

  case REG_32: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseRegister(holder);
  }
    break;
  }
  ReleaseRegister(elem_holder);
  
  delete left;
  left = NULL;
}

void JitCompilerAmd64::ProcessStoreFloatElement(StackInstr* instr) {
  RegisterHolder* elem_holder = ArrayOffset(instr, FLOAT_TYPE);
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  switch(left->GetType()) {
  case IMM_64:
    move_imm_memx(left, 0, elem_holder->GetRegister());
    break;

  case MEM_64: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg(left->GetOperand(), 
		  RBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
    break;

  case REG_64: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), 0, elem_holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
    break;
  }
  ReleaseRegister(elem_holder);
  
  delete left;
  left = NULL;
}

void JitCompilerAmd64::ProcessFloatToInt(StackInstr* instr) {
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  RegisterHolder* holder = GetRegister();
  switch(left->GetType()) {
  case IMM_64:
    cvt_imm_reg(left, holder->GetRegister());
    break;
    
  case MEM_64:
    cvt_mem_reg(left->GetOperand(), 
		RBP, holder->GetRegister());
    break;

  case REG_64:
    cvt_xreg_reg(left->GetRegister()->GetRegister(), 
		 holder->GetRegister());
    ReleaseXmmRegister(left->GetRegister());
    break;
  }
  working_stack.push(new RegInstr(holder));

  delete left;
  left = NULL;
}

void JitCompilerAmd64::ProcessIntToFloat(StackInstr* instr) {
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  RegisterHolder* holder = GetXmmRegister();
  switch(left->GetType()) {
  case IMM_32:
    cvt_imm_xreg(left, holder->GetRegister());
    break;
    
  case MEM_32:
    cvt_mem_xreg(left->GetOperand(), 
		 RBP, holder->GetRegister());
    break;

  case REG_32:
    cvt_reg_xreg(left->GetRegister()->GetRegister(), 
		 holder->GetRegister());
    ReleaseRegister(left->GetRegister());
    break;
  }
  working_stack.push(new RegInstr(holder));

  delete left;
  left = NULL;
}

void JitCompilerAmd64::ProcessStore(StackInstr* instr) {
  Register dest;
  // int mod = 0;

  if(instr->GetOperand2() == LOCL) {
    dest = RBP;
  }
  else {
    RegisterHolder* holder = GetRegister();
    RegInstr* left = working_stack.top();
    // mod = sizeof(INT_LIT);
    
    move_mem_reg(left->GetOperand(), 
		 RBP, holder->GetRegister());

    dest = holder->GetRegister();
    working_stack.pop();
    ReleaseRegister(holder);

    delete left;
    left = NULL;
  }
  
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  switch(left->GetType()) {
  case IMM_32:
    move_imm_mem(left->GetOperand(), 
		 instr->GetOperand3(), 
		 dest);
    break;
  case MEM_32: {
    RegisterHolder* holder = GetRegister();
    move_mem_reg(left->GetOperand(), 
		 RBP, holder->GetRegister());
    move_reg_mem(holder->GetRegister(), 
		 instr->GetOperand3(), dest);
    ReleaseRegister(holder);
  }
    break;
  case REG_32: {
    RegisterHolder* holder = left->GetRegister();
    move_reg_mem(holder->GetRegister(), 
		 instr->GetOperand3(), dest);
    ReleaseRegister(holder);
  }
    break;

  case IMM_64:
    move_imm_memx(left, instr->GetOperand3(), dest);
    break;
  case MEM_64: {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg(left->GetOperand(), 
		  RBP, holder->GetRegister());
    move_xreg_mem(holder->GetRegister(), 
		  instr->GetOperand3(), dest);
    ReleaseXmmRegister(holder);
  }
    break;
  case REG_64: {
    RegisterHolder* holder = left->GetRegister();
    move_xreg_mem(holder->GetRegister(), 
		  instr->GetOperand3(), dest);
    ReleaseXmmRegister(holder);
  }
    break;
  }
  
  delete left;
  left = NULL;
}

void JitCompilerAmd64::ProcessStackCall(INT_LIT instr_id, INT_LIT &instr_index, INT_LIT params) {
  ProcessReturn(params);
  // at this state, all registers should be freed
  move_imm_reg(instr_id, RDI);
  move_mem_reg(CLS_ID, RBP, RSI);
  move_mem_reg(MTHD_ID, RBP, RDX);
  move_mem_reg(INSTANCE, RBP, RCX);
  move_mem_reg(OP_STACK, RBP, R8);
  move_mem_reg(STACK_POS, RBP, R9);
  push_imm(instr_index - 1);
  
  // call function
  move_imm_reg((INT_LIT)JitCompilerAmd64::StackCall, R10);
  call_reg(R10);
}

void JitCompilerAmd64::ProcessReturn(INT_LIT params) {
  if(!working_stack.empty()) {
    RegisterHolder* op_stack_holder = GetRegister();
    RegisterHolder* stack_pos_holder = GetRegister();
    move_mem_reg(OP_STACK, RBP, op_stack_holder->GetRegister());

    INT_LIT count;
    if(params) {
      count = params;
    }
    else {
      count = working_stack.size();
    }

    while(count--) {
      RegInstr* left = working_stack.top();
      working_stack.pop();
    
      move_mem_reg(STACK_POS, RBP, 
		   stack_pos_holder->GetRegister());
      move_mem_reg(0, stack_pos_holder->GetRegister(), 
		   stack_pos_holder->GetRegister());
      shl_reg(stack_pos_holder->GetRegister(), 3);
      add_reg_reg(stack_pos_holder->GetRegister(),
		  op_stack_holder->GetRegister());
      move_mem_reg(STACK_POS, RBP, 
		   stack_pos_holder->GetRegister());
  
      switch(left->GetType()) {
      case IMM_32:
	move_imm_mem(left->GetOperand(), 0, op_stack_holder->GetRegister());
	break;
	
      case MEM_32: {
	RegisterHolder* temp_holder = GetRegister();
	move_mem_reg(left->GetOperand(), RBP, 
		     temp_holder->GetRegister());
	move_reg_mem(temp_holder->GetRegister(), 0, 
		     op_stack_holder->GetRegister());
	ReleaseRegister(temp_holder);
      }
	break;
	
      case REG_32:
	move_reg_mem(left->GetRegister()->GetRegister(),
		     0, op_stack_holder->GetRegister());
	ReleaseRegister(left->GetRegister());
	break;
	
      case IMM_64:
	move_imm_memx(left, 0, op_stack_holder->GetRegister());
	break;
	
      case MEM_64: {
	RegisterHolder* temp_holder = GetXmmRegister();
	move_mem_xreg(left->GetOperand(), RBP,
		      temp_holder->GetRegister());
	move_xreg_mem(temp_holder->GetRegister(), 0, 
		      op_stack_holder->GetRegister());
	ReleaseXmmRegister(temp_holder);
      }
	break;
	
      case REG_64:
	move_xreg_mem(left->GetRegister()->GetRegister(),
		      0, op_stack_holder->GetRegister());
	ReleaseXmmRegister(left->GetRegister());
	break;
      }
      inc_mem(0, stack_pos_holder->GetRegister());
      
      delete left;
      left = NULL;
    }    
    ReleaseRegister(stack_pos_holder);
    ReleaseRegister(op_stack_holder);
#ifdef _VERBOSE
    cout << "# avail regs: " << aval_regs.size() << " #" << endl;
#endif
  }
}

void JitCompilerAmd64::ProcessIntCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  RegInstr* right = working_stack.top();
  working_stack.pop();

  switch(left->GetType()) {
    // intermidate
  case IMM_32:
    switch(right->GetType()) {
    case IMM_32: {
      RegisterHolder* holder = GetRegister();
      move_imm_reg(left->GetOperand(), holder->GetRegister());
      math_imm_reg(right->GetOperand(), holder->GetRegister(), 
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    case REG_32: {
      RegisterHolder* holder = GetRegister();
      move_imm_reg(left->GetOperand(), holder->GetRegister());
      math_reg_reg(right->GetRegister()->GetRegister(), holder->GetRegister(),
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    case MEM_32: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(right->GetOperand(), RBP, holder->GetRegister());

      RegisterHolder* imm_holder = GetRegister();
      move_imm_reg(left->GetOperand(), imm_holder->GetRegister());
      
      math_reg_reg(holder->GetRegister(), imm_holder->GetRegister(), 
		   instruction->GetType());
      
      ReleaseRegister(holder);
      working_stack.push(new RegInstr(imm_holder));
    }
      break;
    }	    
    break; 
    // register
  case REG_32:
    switch(right->GetType()) {
    case IMM_32: {
      RegisterHolder* holder = left->GetRegister();
      math_imm_reg(right->GetOperand(), holder->GetRegister(), 
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    case REG_32: {
      RegisterHolder* holder = right->GetRegister();
      math_reg_reg(left->GetRegister()->GetRegister(), 
		   holder->GetRegister(), 
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
      ReleaseRegister(left->GetRegister());
    }
      break;
    case MEM_32: {
      RegisterHolder* holder = left->GetRegister();
      math_mem_reg(right->GetOperand(), 
		   holder->GetRegister(), 
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    }
    break;
    // memory
  case MEM_32:
    switch(right->GetType()) {
    case IMM_32: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(left->GetOperand(), RBP, holder->GetRegister());
      math_imm_reg(right->GetOperand(), holder->GetRegister(),
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    case REG_32: {
      RegisterHolder* holder = right->GetRegister();
      math_mem_reg(left->GetOperand(), 
		   holder->GetRegister(), 
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    case MEM_32: {
      RegisterHolder* holder = GetRegister();
      move_mem_reg(left->GetOperand(), RBP, holder->GetRegister());
      math_mem_reg(right->GetOperand(), 
		   holder->GetRegister(), 
		   instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    }
    break;
  }

  delete left;
  left = NULL;
    
  delete right;
  right = NULL;
}

void JitCompilerAmd64::ProcessFloatCalculation(StackInstr* instruction) {
  RegInstr* left = working_stack.top();
  working_stack.pop();
  
  RegInstr* right = working_stack.top();
  working_stack.pop();

  switch(left->GetType()) {
    // intermidate
  case IMM_64:
    switch(right->GetType()) {
    case IMM_64: {
      RegisterHolder* holder = GetXmmRegister();
      move_imm_xreg(left, holder->GetRegister());
      math_imm_xreg(right, holder->GetRegister(), 
		    instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;

    case REG_64: {
      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(left, imm_holder->GetRegister());
      math_xreg_xreg(right->GetRegister()->GetRegister(),
		    imm_holder->GetRegister(),
		    instruction->GetType());
      working_stack.push(new RegInstr(imm_holder));
    }
      break;

    case MEM_64: {
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg(right->GetOperand(), RBP, holder->GetRegister());

      RegisterHolder* imm_holder = GetXmmRegister();
      move_imm_xreg(left, imm_holder->GetRegister());

      math_xreg_xreg(holder->GetRegister(), imm_holder->GetRegister(), 
		     instruction->GetType());
      
      ReleaseXmmRegister(holder);
      working_stack.push(new RegInstr(imm_holder));
    }
      break;
    }	    
    break; 

    // register
  case REG_64:
    switch(right->GetType()) {
    case IMM_64: {
      RegisterHolder* holder = left->GetRegister();
      math_imm_xreg(right, holder->GetRegister(), 
		    instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;

    case REG_64: {
      RegisterHolder* holder = right->GetRegister();
      math_xreg_xreg(left->GetRegister()->GetRegister(), 
		     holder->GetRegister(), 
		     instruction->GetType());
      working_stack.push(new RegInstr(holder));
      ReleaseXmmRegister(left->GetRegister());
    }
      break;

    case MEM_64: {
      RegisterHolder* holder = left->GetRegister();
      math_mem_xreg(right->GetOperand(), 
		    holder->GetRegister(), 
		    instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    }
    break;

    // memory
  case MEM_64:
    switch(right->GetType()) {
    case IMM_64: {
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg(left->GetOperand(), RBP, holder->GetRegister());
      math_imm_xreg(right, holder->GetRegister(),
		    instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;

    case REG_64: {
      RegisterHolder* holder = right->GetRegister();
      math_mem_xreg(left->GetOperand(), 
		    holder->GetRegister(), 
		    instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;

    case MEM_64: {
      RegisterHolder* holder = GetXmmRegister();
      move_mem_xreg(left->GetOperand(), RBP, holder->GetRegister());
      math_mem_xreg(right->GetOperand(), 
		    holder->GetRegister(), 
		    instruction->GetType());
      working_stack.push(new RegInstr(holder));
    }
      break;
    }
    break;
  }

  delete left;
  left = NULL;
    
  delete right;
  right = NULL;
}














void JitCompilerAmd64::move_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movl %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x89);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerAmd64::move_reg_mem8(Register src, INT_LIT offset, Register dest) { 
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movb %" << GetRegisterName(src) 
       << ", " << offset << "(%" << GetRegisterName(dest) << ")" << "]" 
       << endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x88);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}
    
void JitCompilerAmd64::move_reg_mem(Register src, INT_LIT offset, Register dest) { 
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movl %" << GetRegisterName(src) 
       << ", " << offset << "(%" << GetRegisterName(dest) << ")" << "]" 
       << endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x89);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::move_mem8_reg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movb " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest)
       << "]" << endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x8a);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::move_mem_reg(INT_LIT offset, Register src, Register dest) {
  stor_regs.insert(pair<int, Register>(offset, dest));

#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movl " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest)
       << "]" << endl;
#endif
  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x8b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitCompilerAmd64::move_imm_memx(RegInstr* instr, INT_LIT offset, Register dest) {
  RegisterHolder* tmp_holder = GetXmmRegister();
  move_imm_xreg(instr, tmp_holder->GetRegister());
  move_xreg_mem(tmp_holder->GetRegister(), offset, dest);
  ReleaseXmmRegister(tmp_holder);
}

void JitCompilerAmd64::move_imm_mem8(INT_LIT imm, INT_LIT offset, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movb $" << imm << ", " << offset 
       << "(%" << GetRegisterName(dest) << ")" << "]" << endl;
#endif

  // encode
  if(dest <= RSP) {
    AddMachineCode(0x42);
  }
  else {
    AddMachineCode(0x43);
  }
  AddMachineCode(0xc6);
  BYTE code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  // write value
  AddImm(offset);
  AddMachineCode(imm);
}

void JitCompilerAmd64::move_imm_mem(INT_LIT imm, INT_LIT offset, Register dest) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  move_reg_mem(imm_holder->GetRegister(), offset, dest);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::move_imm_reg(INT_LIT imm, Register reg) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movl $" << imm << ", %" 
       << GetRegisterName(reg) << "]" << endl;
#endif
  // encode
  AddMachineCode(B(reg));
  BYTE code = 0xb8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm64(imm);
}

void JitCompilerAmd64::move_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());  
  move_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}
    
void JitCompilerAmd64::move_mem_xreg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movsd " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest)
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x10);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitCompilerAmd64::move_xreg_mem(Register src, INT_LIT offset, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movsd %" << GetRegisterName(src) 
       << ", " << offset << "(%" << GetRegisterName(dest) << ")" << "]" 
       << endl;
#endif 
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x11);
  AddMachineCode(ModRM(dest, src));
  // write value
  AddImm(offset);
}
    
void JitCompilerAmd64::move_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [movsd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x10);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerAmd64::math_imm_reg(INT_LIT imm, Register reg, InstructionType type) {
  switch(type) {
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

  case LES_INT:	
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
    cmp_imm_reg(imm, reg);
    cmov_reg(reg, type);
    break;
  }
}

void JitCompilerAmd64::math_reg_reg(Register src, Register dest, InstructionType type) {
  switch(type) {
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

  case LES_INT:	
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
    cmp_reg_reg(src, dest);
    cmov_reg(dest, type);
    break;
  }
}

void JitCompilerAmd64::math_mem_reg(INT_LIT offset, Register reg, InstructionType type) {
  map<int, Register>::iterator result = stor_regs.find(offset);
  if(result != stor_regs.end()) {
    // TODO: should this be mru
    math_reg_reg((*result).second, reg, type);
    return;
  }
  
  switch(type) {
  case ADD_INT:
    add_mem_reg(offset, RBP, reg);
    break;

  case SUB_INT: {
    RegisterHolder* tmp = GetRegister();
    move_mem_reg(offset, RBP, tmp->GetRegister());
    move_reg_mem(reg, offset, RBP);
    sub_mem_reg(offset, RBP, tmp->GetRegister());
    move_reg_reg(tmp->GetRegister(), reg);
  }
    break;

  case MUL_INT:
    mul_mem_reg(offset, RBP, reg);
    break;

  case DIV_INT: {
    RegisterHolder* tmp = GetRegister();
    move_mem_reg(offset, RBP, tmp->GetRegister());
    move_reg_mem(reg, offset, RBP);
    div_mem_reg(offset, RBP, tmp->GetRegister(), false);
    move_reg_reg(tmp->GetRegister(), reg);
  }
    break;

  case MOD_INT:
    div_mem_reg(offset, RBP, reg, true);
    break;

  case LES_INT:
  case GTR_INT:
  case EQL_INT:
  case NEQL_INT:
    cmp_mem_reg(offset, RBP, reg);
    cmov_reg(reg, type);
    break;
  }
}

void JitCompilerAmd64::math_imm_xreg(RegInstr* instr, Register reg, InstructionType type) {
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
    cmp_imm_xreg(instr, reg, true);
    break;
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
    cmp_imm_xreg(instr, reg);
    break;
  }
}

void JitCompilerAmd64::math_mem_xreg(INT_LIT offset, Register reg, InstructionType type) {
  switch(type) {
  case ADD_FLOAT:
    add_mem_xreg(offset, RBP, reg);
    break;

  case SUB_FLOAT:
    sub_mem_xreg(offset, RBP, reg);
    break;

  case MUL_FLOAT:
    mul_mem_xreg(offset, RBP, reg);
    break;

  case DIV_FLOAT:
    div_mem_xreg(offset, RBP, reg);
    break;

  case LES_FLOAT:
    cmp_mem_xreg(offset, RBP, reg, true);
    break;
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
    cmp_mem_xreg(offset, RBP, reg);
    break;
  }
}

void JitCompilerAmd64::math_xreg_xreg(Register src, Register dest, InstructionType type) {
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
    cmp_xreg_xreg(dest, src);
    break;
  case GTR_FLOAT:
  case EQL_FLOAT:
  case NEQL_FLOAT:
    cmp_xreg_xreg(src, dest);
    break;
  }
}    

void JitCompilerAmd64::cmp_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cmpll %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x39);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerAmd64::cmp_mem_reg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cmpl " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x3b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
    
void JitCompilerAmd64::cmp_imm_reg(INT_LIT imm, Register reg) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cmpl $" << imm << ", %"
       << GetRegisterName(reg) << "]" << endl;
#endif
  // encode
  AddMachineCode(B(reg));  
  AddMachineCode(0x81);
  BYTE code = 0xf8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerAmd64::cmov_reg(Register reg, InstructionType oper) {
  // set register to 0; if eflag than set to 1
  move_imm_reg(0, reg);
  RegisterHolder* true_holder = GetRegister();
  move_imm_reg(1, true_holder->GetRegister());
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cmovl %"
       << GetRegisterName(reg) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(reg, true_holder->GetRegister()));
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
    
  case LES_FLOAT: // values are swapped for less
  case GTR_FLOAT:
    AddMachineCode(0x47);
    break;
  }
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, reg);
  RegisterEncode3(code, 5, true_holder->GetRegister());
  AddMachineCode(code);
  ReleaseRegister(true_holder);
}

void JitCompilerAmd64::add_imm_mem(INT_LIT imm, INT_LIT offset, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [addl " << imm 
       << offset << "(%"<< GetRegisterName(dest) << ")]" << endl;
#endif

  // encode
  AddMachineCode(XB(dest));
  AddMachineCode(0x81);
  AddMachineCode(ModRM(dest, RAX));
  // write value
  AddImm(offset);
  AddImm(imm);
}
    
void JitCompilerAmd64::add_imm_reg(INT_LIT imm, Register reg) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [addl $" << imm << ", %"
       << GetRegisterName(reg) << "]" << endl;
#endif
  // encode
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  BYTE code = 0xc0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}
    
void JitCompilerAmd64::add_imm_xreg(RegInstr* instr, Register reg) {
  // copy addresss of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  add_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::sub_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  sub_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::div_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  div_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::mul_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  mul_mem_xreg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::add_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [addl %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x01);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerAmd64::sub_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [subsd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x5c);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);	     
}

void JitCompilerAmd64::mul_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [mulsd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x59);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerAmd64::div_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [divsd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x5e);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerAmd64::add_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [addsd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x58);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}
    
void JitCompilerAmd64::add_mem_reg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [addl " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x03);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::add_mem_xreg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [addsd " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x58);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);  
}

void JitCompilerAmd64::sub_mem_xreg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [subsd " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x5c);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::mul_mem_xreg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [mulsd " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x59);
  AddMachineCode(ModRM(src, dest));
  AddImm(offset);
}

void JitCompilerAmd64::div_mem_xreg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [divsd " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x5e);
  AddMachineCode(ModRM(src, dest));
  AddImm(offset);
}

void JitCompilerAmd64::sub_imm_reg(INT_LIT imm, Register reg) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [subl $" << imm << ", %"
       << GetRegisterName(reg) << "]" << endl;
#endif
  // encode
  AddMachineCode(B(reg));
  AddMachineCode(0x81);
  BYTE code = 0xe8;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  AddImm(imm);
}

void JitCompilerAmd64::sub_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [subl %" << GetRegisterName(src)
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x29);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerAmd64::sub_mem_reg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [subl " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif

  // encode
  AddMachineCode(RXB(dest, src));
  AddMachineCode(0x2b);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::mul_imm_reg(INT_LIT imm, Register reg) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [imul $" << imm 
       << ", %"<< GetRegisterName(reg) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(reg, reg));
  AddMachineCode(0x69);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, reg);
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
  // write value
  AddImm(imm);
}

void JitCompilerAmd64::mul_reg_reg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [imul %" 
       << GetRegisterName(src) << ", %"<< GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0xaf);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, src);
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
}

void JitCompilerAmd64::mul_mem_reg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [imul " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0xaf);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::div_imm_reg(INT_LIT imm, Register reg, bool is_mod) {
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(imm, imm_holder->GetRegister());
  div_reg_reg(imm_holder->GetRegister(), reg, is_mod);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::div_mem_reg(INT_LIT offset, Register src,
			      Register dest, bool is_mod) {
  INT_LIT state = 0;
  
  if(src == RAX) {
    // move_reg_mem(RAX, TMP_POS_1, RBP);
    if(dest != RDX) {
      move_reg_mem(RDX, TMP_POS_2, RBP);
      state = 1;
    }
  }
  else {
    // move_reg_mem(RDX, TMP_POS_1, RBP);
    if(dest != RAX) {
      move_reg_mem(RAX, TMP_POS_2, RBP);
      state = 2;
    }
  }
  // ============
  move_reg_reg(dest, RAX);
  move_reg_reg(RAX, RDX);
  shr_reg(RDX, 63);
    
  // encode
  AddMachineCode(XB(RBP));
  AddMachineCode(0xf7);
  AddMachineCode(ModRM(RBP, RDI));
  // write value
  AddImm(offset);
    
#ifdef _DEBUG
  cout << "  " << (++instr_count) << "[idiv " << offset << "(%" 
       << GetRegisterName(src) << ")" << endl;
#endif
  // ============
  if(dest != RAX) {
    move_reg_reg(RAX, dest);
  }
    
  if(state == 1) {
    move_mem_reg(TMP_POS_2, RBP, RDX);
  }
    
  if(state == 2) {
    move_mem_reg(TMP_POS_2, RBP, RAX);
  }
}

void JitCompilerAmd64::div_reg_reg(Register src, Register dest, bool is_mod) {
  if(src == RAX || src == RDX) {
    INT_LIT state = 0;
    
    if(src == RAX) {
      move_reg_mem(RAX, TMP_POS_1, RBP);
      if(dest != RDX) {
	move_reg_mem(RDX, TMP_POS_2, RBP);
	state = 1;
      }
    }
    else {
      move_reg_mem(RDX, TMP_POS_1, RBP);
      if(dest != RAX) {
	move_reg_mem(RAX, TMP_POS_2, RBP);
	state = 2;
      }
    }
    // ============
    move_reg_reg(dest,RAX);
    move_reg_reg(RAX, RDX);
    shr_reg(RDX, 63);
    
    // encode
    AddMachineCode(XB(RBP));
    AddMachineCode(0xf7);
    AddMachineCode(ModRM(RBP, RDI));
    // write value
    AddImm(TMP_POS_1);
    
#ifdef _DEBUG
    cout << "  " << (++instr_count) << "[idiv " << TMP_POS_1 << "(%" 
	 << GetRegisterName(src) << ")" << endl;
#endif
    // ============
    if(dest != RAX) {
      move_reg_reg(RAX, dest);
    }
    
    if(state == 1) {
      move_mem_reg(TMP_POS_2, RBP, RDX);
    }
    
    if(state == 2) {
      move_mem_reg(TMP_POS_2, RBP, RAX);
    }
  }
  else {
    bool state = false;
    if(dest != RAX) {
      move_reg_mem(RAX, TMP_POS_1, RBP);
      state = true;
    }
    move_reg_mem(RDX, TMP_POS_2, RBP);

    // ============
    move_reg_reg(dest, RAX);
    move_reg_reg(RAX, RDX);
    shr_reg(RDX, 63);
    
    // encode
    AddMachineCode(B(src));
    AddMachineCode(0xf7);
    BYTE code = 0xf8;
    // write value
    RegisterEncode3(code, 5, src);
    AddMachineCode(code);
    
#ifdef _DEBUG
    cout << "  " << (++instr_count) << ": [idiv %" 
	 << GetRegisterName(src) << "]" << endl;
#endif
    // ============
    if(dest != RDX) {
      move_mem_reg(TMP_POS_2, RBP, RDX);
    }

    if(state) {
      move_reg_reg(RAX, dest);
      move_mem_reg(TMP_POS_1, RBP, RAX);
    }
  }
}

void JitCompilerAmd64::dec_mem(INT_LIT offset, Register dest) {
  AddMachineCode(B(dest));
  AddMachineCode(0xff);
  BYTE code = 0x88;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [decl " << offset << "(%" 
       << GetRegisterName(dest) << ")" << "]" << endl;
#endif
}

void JitCompilerAmd64::inc_mem(INT_LIT offset, Register dest) {
  AddMachineCode(B(dest));
  AddMachineCode(0xff);
  BYTE code = 0x80;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [incl " << offset << "(%" 
       << GetRegisterName(dest) << ")" << "]" << endl;
#endif
}

void JitCompilerAmd64::shl_reg(Register dest, INT_LIT value) {
  AddMachineCode(B(dest));
  AddMachineCode(0xc1);
  BYTE code = 0xe0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode(value);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [shl $" << value << ", %" 
       << GetRegisterName(dest) << "]" << endl;
#endif
}

void JitCompilerAmd64::shr_reg(Register dest, INT_LIT value) {
  AddMachineCode(B(dest));
  AddMachineCode(0xc1);
  BYTE code = 0xe8;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddMachineCode(value);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [shr $" << value << ", %" 
       << GetRegisterName(dest) << "]" << endl;
#endif
}

void JitCompilerAmd64::push_mem(INT_LIT offset, Register dest) {
  AddMachineCode(B(dest));
  AddMachineCode(0xff);
  BYTE code = 0xb0;
  RegisterEncode3(code, 5, dest);
  AddMachineCode(code);
  AddImm(offset);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [pushl " << offset << "(%" 
       << GetRegisterName(dest) << ")" << "]" << endl;
#endif
}

void JitCompilerAmd64::push_reg(Register reg) {
  AddMachineCode(B(reg));
  BYTE code = 0x50;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [pushl %" << GetRegisterName(reg) 
       << "]" << endl;
#endif
}

void JitCompilerAmd64::push_imm(INT_LIT value) {
  AddMachineCode(0x68);
  AddImm(value);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [pushl $" << value << "]" << endl;
#endif
}

void JitCompilerAmd64::pop_reg(Register reg) {
  AddMachineCode(B(reg));
  BYTE code = 0x58;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [popl %" << GetRegisterName(reg) 
       << "]" << endl;
#endif
}

void JitCompilerAmd64::call_reg(Register reg) {
  AddMachineCode(B(reg));
  AddMachineCode(0xff);
  BYTE code = 0xd0;
  RegisterEncode3(code, 5, reg);
  AddMachineCode(code);
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [call %" << GetRegisterName(reg)
       << "]" << endl;
#endif
}

// TODO: continue
void JitCompilerAmd64::cmp_xreg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [ucomisd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(ROB(src, dest));
  AddMachineCode(0x66);
  AddMachineCode(0x0f);
  AddMachineCode(0x2e);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerAmd64::cmp_mem_xreg(INT_LIT offset, Register src, Register dest,
			       bool swap) {
  if(swap) {
    RegisterHolder* holder = GetXmmRegister();
    move_mem_xreg(offset, src, holder->GetRegister());
    cmp_xreg_xreg(dest, holder->GetRegister());
    ReleaseXmmRegister(holder);
  }
  else {
#ifdef _DEBUG
    cout << "  " << (++instr_count) << ": [ucomisd " << offset << "(%" 
	 << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
	 << "]" << endl;
#endif
    // encode
    AddMachineCode(RXB(src, dest));
    AddMachineCode(0x66);
    AddMachineCode(0x0f);
    AddMachineCode(0x2e);
    AddMachineCode(ModRM(src, dest));
    // write value
    AddImm(offset);
  }
}

void JitCompilerAmd64::cmp_imm_xreg(RegInstr* instr, Register reg, bool swap) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cmp_mem_xreg(0, imm_holder->GetRegister(), reg, swap);
  ReleaseRegister(imm_holder);
}

void JitCompilerAmd64::cvt_xreg_reg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cvtsd2si %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2c);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerAmd64::cvt_imm_reg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cvt_mem_reg(0, imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

// TODO:
void JitCompilerAmd64::cvt_mem_reg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cvtsd2di " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x2d);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}

void JitCompilerAmd64::cvt_reg_xreg(Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cvtsi2sd %" << GetRegisterName(src) 
       << ", %" << GetRegisterName(dest) << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(ROB(dest, src));
  AddMachineCode(0x0f);
  AddMachineCode(0x2a);
  BYTE code = 0xc0;
  // write value
  RegisterEncode3(code, 2, dest);
  RegisterEncode3(code, 5, src);
  AddMachineCode(code);
}

void JitCompilerAmd64::cvt_imm_xreg(RegInstr* instr, Register reg) {
  // copy address of imm value
  RegisterHolder* imm_holder = GetRegister();
  move_imm_reg(instr->GetOperand(), imm_holder->GetRegister());
  cvt_reg_xreg(imm_holder->GetRegister(), reg);
  ReleaseRegister(imm_holder);
}

// TODO:
void JitCompilerAmd64::cvt_mem_xreg(INT_LIT offset, Register src, Register dest) {
#ifdef _DEBUG
  cout << "  " << (++instr_count) << ": [cvtsi2sd " << offset << "(%" 
       << GetRegisterName(src) << "), %" << GetRegisterName(dest) 
       << "]" << endl;
#endif
  // encode
  AddMachineCode(0xf2);
  AddMachineCode(RXB(src, dest));
  AddMachineCode(0x0f);
  AddMachineCode(0x2a);
  AddMachineCode(ModRM(src, dest));
  // write value
  AddImm(offset);
}
