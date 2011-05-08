/***************************************************************************
 * JIT compiler for the AMD64 architecture.
 *
 * Copyright (c) 2008-2010 Randy Hollines
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
 * "AS IS" AND ANY EXPRESS OR IPLIED WARRANTIES, INCLUDING, BUT NOT 
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

#include "stck_to_3addr.h"
#include <string>

using namespace Runtime;

/********************************
 * JitCompilerIA64 class
 ********************************/
StackProgram* JitCompilerIA64::program;
void JitCompilerIA64::Initialize(StackProgram* p) {
  program = p;
}

void Prolog() {
}

void JitCompilerIA64::RegisterRoot() {
}
 
void JitCompilerIA64::UnregisterRoot() {
}

void Epilog(long imm) {
}
    
void JitCompilerIA64::ProcessParameters(long count) {
}
 
void JitCompilerIA64::ProcessInstructions() {
  {
  while(instr_index < method->GetInstructionCount() && compile_success) {
    StackInstr* instr = method->GetInstruction(instr_index++);
    instr->SetOffset(code_index);
    
    switch(instr->GetType()) {
      // load literal
    case LOAD_INT_LIT:
      break;
      
      // float literal
    case LOAD_FLOAT_LIT:
      break;
      
      // load self
    case LOAD_INST_MEM: {
      break;

      // load self
    case LOAD_CLS_MEM: {
      break;
      
      // load variable
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
      break;
    
      // store value
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
      break;

      // copy value
    case COPY_INT_VAR:
    case COPY_FLOAT_VAR:
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
    case LES_INT:
    case GTR_INT:
    case LES_EQL_INT:
    case GTR_EQL_INT:
    case EQL_INT:
    case NEQL_INT:
    case SHL_INT:
    case SHR_INT:
      break;

    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
      break;

    case LES_FLOAT:
    case GTR_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT:
      break;
      
    case RTRN:
      break;
      
    case MTHD_CALL:
      break;

      // TODO: implement
    case DYN_MTHD_CALL:
      break;
      
    case NEW_INT_ARY:
      break;

    case NEW_FLOAT_ARY:
      break;
      
    case NEW_OBJ_INST:
      break;
      
    case THREAD_JOIN: 
      break;

    case THREAD_SLEEP: 
      break;
      
    case CRITICAL_START: 
      break;
      
    case CRITICAL_END: 
      break;
      
    case TRAP:
      break;

    case TRAP_RTRN:
      break;
      
    case STOR_BYTE_ARY_ELM:
      break;
      
    case STOR_INT_ARY_ELM:
      break;

    case STOR_FLOAT_ARY_ELM:
      break;

    case SWAP_INT:
      break;

    case POP_INT:
    case POP_FLOAT: 
      break;

    case FLOR_FLOAT:
      break;

    case CEIL_FLOAT:
      break;
      
    case F2I:
      break;

    case I2F:
      break;

    case OBJ_TYPE_OF: 
      break;
      
    case OBJ_INST_CAST: 
      break;
      
    case LOAD_BYTE_ARY_ELM:
      break;
      
    case LOAD_INT_ARY_ELM:
      break;

    case LOAD_FLOAT_ARY_ELM:
      break;
      
    case JMP:
      break;
      
    case LBL:
      break;
      
    default: {
      InstructionType error = (InstructionType)instr->GetType();
      cerr << "Unknown instruction: " << error << "!" << endl;
      exit(1);
    }
      break;
    }
  }
}
 
