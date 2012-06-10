/***************************************************************************
 * Defines how the intermediate code is written to output files
 *
 * Copyright (c) 2008-2012, Randy Hollines
 * All rights reserved.string
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

#ifndef __TARGET_H__
#define __TARGET_H__

#include "types.h"
#include "linker.h"
#include "tree.h"
#include "../shared/instrs.h"
#include "../shared/version.h"

using namespace std;
using namespace instructions;

namespace backend {
class IntermediateClass;

/****************************
 * Intermediate class
 ****************************/
class Intermediate {

public:
  Intermediate() {
  }

  virtual ~Intermediate() {
  }

protected:
  void WriteString(const string &value, ofstream* file_out) {
    int size = (int)value.size();
    file_out->write((char*)&size, sizeof(size));
    file_out->write(value.c_str(), value.size());
  }

  void WriteByte(char value, ofstream* file_out) {
    file_out->write(&value, sizeof(value));
  }

  void WriteInt(int value, ofstream* file_out) {
    file_out->write((char*)&value, sizeof(value));
  }

  void WriteDouble(FLOAT_VALUE value, ofstream* file_out) {
    file_out->write((char*)&value, sizeof(value));
  }
};

/****************************
 * IntermediateInstruction
 * class
 ****************************/
class IntermediateInstruction : public Intermediate {
  friend class IntermediateFactory;
  InstructionType type;
  int operand;
  int operand2;
  int operand3;
  FLOAT_VALUE operand4;
  string operand5;
  string operand6;
  int line_num;

  IntermediateInstruction(int l, InstructionType t) {
    line_num = l;
    type = t;
  }

  IntermediateInstruction(int l, InstructionType t, int o1) {
    line_num = l;
    type = t;
    operand = o1;
  }

  IntermediateInstruction(int l, InstructionType t, int o1, int o2) {
    line_num = l;
    type = t;
    operand = o1;
    operand2 = o2;
  }

  IntermediateInstruction(int l, InstructionType t, int o1, int o2, int o3) {
    line_num = l;
    type = t;
    operand = o1;
    operand2 = o2;
    operand3 = o3;
  }

  IntermediateInstruction(int l, InstructionType t, FLOAT_VALUE o4) {
    line_num = l;
    type = t;
    operand4 = o4;
  }

  IntermediateInstruction(int l, InstructionType t, string o5) {
    line_num = l;
    type = t;
    operand5 = o5;
  }

  IntermediateInstruction(int l, InstructionType t, int o3, string o5, string o6) {
    line_num = l;
    type = t;
    operand3 = o3;
    operand5 = o5;
    operand6 = o6;
  }

  IntermediateInstruction(LibraryInstr* lib_instr) {
    type = lib_instr->GetType();
    line_num = lib_instr->GetLineNumber();
    operand = lib_instr->GetOperand();
    operand2 = lib_instr->GetOperand2();
    operand3 = lib_instr->GetOperand3();
    operand4 = lib_instr->GetOperand4();
    operand5 = lib_instr->GetOperand5();
    operand6 = lib_instr->GetOperand6();
  }

  ~IntermediateInstruction() {
  }

public:
  InstructionType GetType() {
    return type;
  }

  int GetOperand() {
    return operand;
  }

  int GetOperand2() {
    return operand2;
  }

  FLOAT_VALUE GetOperand4() {
    return operand4;
  }
  
  void SetOperand3(int o3) {
    operand3 = o3;
  }
  
  void Write(bool is_debug, ofstream* file_out) {
    WriteByte((int)type, file_out);
    if(is_debug) {
      WriteInt(line_num, file_out);
    }
    switch(type) {
    case LOAD_INT_LIT:
    case NEW_FLOAT_ARY:
    case NEW_INT_ARY:
    case NEW_BYTE_ARY:
    case NEW_OBJ_INST:
    case OBJ_INST_CAST:
    case OBJ_TYPE_OF:
    case TRAP:
    case TRAP_RTRN:
      WriteInt(operand, file_out);
      break;

    case instructions::ASYNC_MTHD_CALL:
    case MTHD_CALL:
      WriteInt(operand, file_out);
      WriteInt(operand2, file_out);
      WriteInt(operand3, file_out);
      break;

    case LIB_NEW_OBJ_INST:
    case LIB_OBJ_INST_CAST:
      WriteString(operand5, file_out);
      break;

    case LIB_MTHD_CALL:
      WriteInt(operand3, file_out);
      WriteString(operand5, file_out);
      WriteString(operand6, file_out);
      break;
      
    case LIB_FUNC_DEF:
      WriteString(operand5, file_out);
      WriteString(operand6, file_out);
      break;
      
    case JMP:
    case DYN_MTHD_CALL:
    case LOAD_INT_VAR:
    case LOAD_FLOAT_VAR:
    case LOAD_FUNC_VAR:
    case STOR_INT_VAR:
    case STOR_FLOAT_VAR:
    case STOR_FUNC_VAR:
    case COPY_INT_VAR:
    case COPY_FLOAT_VAR:
    case COPY_FUNC_VAR:
    case LOAD_BYTE_ARY_ELM:
    case LOAD_INT_ARY_ELM:
    case LOAD_FLOAT_ARY_ELM:
    case STOR_BYTE_ARY_ELM:
    case STOR_INT_ARY_ELM:
    case STOR_FLOAT_ARY_ELM:
      WriteInt(operand, file_out);
      WriteInt(operand2, file_out);
      break;

    case LOAD_FLOAT_LIT:
      WriteDouble(operand4, file_out);
      break;

    case LBL:
      WriteInt(operand, file_out);
      break;

    default:
      break;
    }
  }

  void Debug() {
    switch(type) {
    case SWAP_INT:
      cout << "SWAP_INT" << endl;
      break;
      
    case POP_INT:
      cout << "POP_INT" << endl;
      break;

    case POP_FLOAT:
      cout << "POP_FLOAT" << endl;
      break;

    case LOAD_INT_LIT:
      cout << "LOAD_INT_LIT: value=" << operand << endl;
      break;
      
    case DYN_MTHD_CALL:
      cout << "DYN_MTHD_CALL num_params=" << operand 
	   << ", rtrn_type=" << operand2 << endl;
      break;
      
    case SHL_INT:
      cout << "SHL_INT" << endl;
      break;

    case SHR_INT:
      cout << "SHR_INT" << endl;
      break;

    case LOAD_FLOAT_LIT:
      cout << "LOAD_FLOAT_LIT: value=" << operand4 << endl;
      break;

    case LOAD_FUNC_VAR:
      cout << "LOAD_FUNC_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;
      
    case LOAD_INT_VAR:
      cout << "LOAD_INT_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case LOAD_FLOAT_VAR:
      cout << "LOAD_FLOAT_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case LOAD_BYTE_ARY_ELM:
      cout << "LOAD_BYTE_ARY_ELM: dimension=" << operand
           << ", local=" << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case LOAD_INT_ARY_ELM:
      cout << "LOAD_INT_ARY_ELM: dimension=" << operand
           << ", local=" << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case LOAD_FLOAT_ARY_ELM:
      cout << "LOAD_FLOAT_ARY_ELM: dimension=" << operand
           << ", local=" << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case LOAD_CLS_MEM:
      cout << "LOAD_CLS_MEM" << endl;
      break;

    case LOAD_INST_MEM:
      cout << "LOAD_INST_MEM" << endl;
      break;
      
    case STOR_FUNC_VAR:
      cout << "STOR_FUNC_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;
      
    case STOR_INT_VAR:
      cout << "STOR_INT_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case STOR_FLOAT_VAR:
      cout << "STOR_FLOAT_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case COPY_FUNC_VAR:
      cout << "COPY_FUNC_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;
      
    case COPY_INT_VAR:
      cout << "COPY_INT_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case COPY_FLOAT_VAR:
      cout << "COPY_FLOAT_VAR: id=" << operand << ", local="
           << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case STOR_BYTE_ARY_ELM:
      cout << "STOR_BYTE_ARY_ELM: dimension=" << operand
           << ", local=" << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case STOR_INT_ARY_ELM:
      cout << "STOR_INT_ARY_ELM: dimension=" << operand
           << ", local=" << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case STOR_FLOAT_ARY_ELM:
      cout << "STOR_FLOAT_ARY_ELM: dimension=" << operand
           << ", local=" << (operand2 == LOCL ? "true" : "false") << endl;
      break;

    case instructions::ASYNC_MTHD_CALL:
      cout << "ASYNC_MTHD_CALL: class=" << operand << ", method="
           << operand2 << "; native=" << (operand3 ? "true" : "false") << endl;
      break;
      
    case instructions::DLL_LOAD:
      cout << "DLL_LOAD" << endl;
      break;
    
    case instructions::DLL_UNLOAD:
      cout << "DLL_UNLOAD" << endl;
      break;
      
    case instructions::DLL_FUNC_CALL:
      cout << "DLL_FUNC_CALL" << endl;
      break;

    case instructions::THREAD_JOIN:
      cout << "THREAD_JOIN" << endl;
      break;
      
    case instructions::THREAD_SLEEP:
      cout << "THREAD_SLEEP" << endl;
      break;

    case instructions::THREAD_MUTEX:
      cout << "THREAD_MUTEX" << endl;
      break;
      
    case CRITICAL_START:
      cout << "CRITICAL_START" << endl;
      break;

    case CRITICAL_END:
      cout << "CRITICAL_END" << endl;
      break;

    case AND_INT:
      cout << "AND_INT" << endl;
      break;

    case OR_INT:
      cout << "OR_INT" << endl;
      break;

    case ADD_INT:
      cout << "ADD_INT" << endl;
      break;

    case SUB_INT:
      cout << "SUB_INT" << endl;
      break;

    case MUL_INT:
      cout << "MUL_INT" << endl;
      break;

    case DIV_INT:
      cout << "DIV_INT" << endl;
      break;

    case MOD_INT:
      cout << "MOD_INT" << endl;
      break;
      
    case BIT_AND_INT:
      cout << "BIT_AND_INT" << endl;
      break;

    case BIT_OR_INT:
      cout << "BIT_OR_INT" << endl;
      break;
      
    case BIT_XOR_INT:
      cout << "BIT_XOR_INT" << endl;
      break;
      
    case EQL_INT:
      cout << "EQL_INT" << endl;
      break;

    case NEQL_INT:
      cout << "NEQL_INT" << endl;
      break;

    case LES_INT:
      cout << "LES_INT" << endl;
      break;

    case GTR_INT:
      cout << "GTR_INT" << endl;
      break;

    case LES_EQL_INT:
      cout << "LES_EQL_INT" << endl;
      break;

    case GTR_EQL_INT:
      cout << "GTR_EQL_INT" << endl;
      break;

    case ADD_FLOAT:
      cout << "ADD_FLOAT" << endl;
      break;

    case SUB_FLOAT:
      cout << "SUB_FLOAT" << endl;
      break;

    case MUL_FLOAT:
      cout << "MUL_FLOAT" << endl;
      break;

    case DIV_FLOAT:
      cout << "DIV_FLOAT" << endl;
      break;

    case EQL_FLOAT:
      cout << "EQL_FLOAT" << endl;
      break;

    case NEQL_FLOAT:
      cout << "NEQL_FLOAT" << endl;
      break;

    case LES_FLOAT:
      cout << "LES_FLOAT" << endl;
      break;

    case GTR_FLOAT:
      cout << "GTR_FLOAT" << endl;
      break;

    case instructions::FLOR_FLOAT:
      cout << "FLOR_FLOAT" << endl;
      break;

    case instructions::CPY_BYTE_ARY:
      cout << "CPY_BYTE_ARY" << endl;
      break;
      
    case instructions::CPY_INT_ARY:
      cout << "CPY_INT_ARY" << endl;
      break;
      
    case instructions::CPY_FLOAT_ARY:
      cout << "CPY_FLOAT_ARY" << endl;
      break;
      
    case instructions::CEIL_FLOAT:
      cout << "CEIL_FLOAT" << endl;
      break;
      
    case instructions::RAND_FLOAT:
      cout << "RAND_FLOAT" << endl;
      break;
      
    case instructions::SIN_FLOAT:
      cout << "SIN_FLOAT" << endl;
      break;
      
    case instructions::COS_FLOAT:
      cout << "COS_FLOAT" << endl;
      break;
      
    case instructions::TAN_FLOAT:
      cout << "TAN_FLOAT" << endl;
      break;

    case instructions::ASIN_FLOAT:
      cout << "ASIN_FLOAT" << endl;
      break;
      
    case instructions::ACOS_FLOAT:
      cout << "ACOS_FLOAT" << endl;
      break;
      
    case instructions::ATAN_FLOAT:
      cout << "ATAN_FLOAT" << endl;
      break;
      
    case instructions::LOG_FLOAT:
      cout << "LOG_FLOAT" << endl;
      break;

    case instructions::POW_FLOAT:
      cout << "POW_FLOAT" << endl;
      break;

    case instructions::SQRT_FLOAT:
      cout << "SQRT_FLOAT" << endl;
      break;

    case I2F:
      cout << "I2F" << endl;
      break;

    case F2I:
      cout << "F2I" << endl;
      break;

    case RTRN:
      cout << "RTRN" << endl;
      break;

    case MTHD_CALL:
      cout << "MTHD_CALL: class=" << operand << ", method="
           << operand2 << "; native=" << (operand3 ? "true" : "false") << endl;
      break;

    case LIB_NEW_OBJ_INST:
      cout << "LIB_NEW_OBJ_INST: class='" << operand5 << "'" << endl;
      break;

    case LIB_OBJ_INST_CAST:
      cout << "LIB_OBJ_INST_CAST: to_class='" << operand5 << "'" << endl;
      break;

    case LIB_MTHD_CALL:
      cout << "LIB_MTHD_CALL: class='" << operand5 << "', method='"
           << operand6 << "'; native=" << (operand3 ? "true" : "false") << endl;
      break;
      
    case LIB_FUNC_DEF:
      cout << "LIB_FUNC_DEF: class='" << operand5 << "', method='" 
	   << operand6 << "'" << endl;
      break;
      
    case LBL:
      cout << "LBL: id=" << operand << endl;
      break;

    case JMP:
      if(operand2 == -1) {
        cout << "JMP: id=" << operand << endl;
      } else {
        cout << "JMP: id=" << operand << " conditional="
             << (operand2 ? "true" : "false") << endl;
      }
      break;

    case OBJ_INST_CAST:
      cout << "OBJ_INST_CAST: to=" << operand << endl;
      break;

    case OBJ_TYPE_OF:
      cout << "OBJ_TYPE_OF: check=" << operand << endl;
      break;
      
    case NEW_FLOAT_ARY:
      cout << "NEW_FLOAT_ARY: dimension=" << operand << endl;
      break;

    case NEW_INT_ARY:
      cout << "NEW_INT_ARY: dimension=" << operand << endl;
      break;

    case NEW_BYTE_ARY:
      cout << "NEW_BYTE_ARY: dimension=" << operand << endl;
      break;

    case NEW_OBJ_INST:
      cout << "NEW_OBJ_INST: class=" << operand << endl;
      break;

    case TRAP:
      cout << "TRAP: args=" << operand << endl;
      break;

    case TRAP_RTRN:
      cout << "TRAP_RTRN: args=" << operand << endl;
      break;

    default:
      break;
    }
  }
};

/****************************
 * IntermediateFactory
 * class
 ****************************/
class IntermediateFactory {
  static IntermediateFactory* instance;
  vector<IntermediateInstruction*> instructions;

public:
  static IntermediateFactory* Instance();

  void Clear() {
    while(!instructions.empty()) {
      IntermediateInstruction* tmp = instructions.front();
      instructions.erase(instructions.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    delete instance;
    instance = NULL;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t, int o1) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o1);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t, int o1, int o2) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o1, o2);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t, int o1, int o2, int o3) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o1, o2, o3);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t, FLOAT_VALUE o4) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o4);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t, string o5) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o5);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(int l, InstructionType t, int o3, string o5, string o6) {
    IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o3, o5, o6);
    instructions.push_back(tmp);
    return tmp;
  }

  IntermediateInstruction* MakeInstruction(LibraryInstr* lib_instr) {
    IntermediateInstruction* tmp = new IntermediateInstruction(lib_instr);
    instructions.push_back(tmp);
    return tmp;
  }
};

/****************************
 * IntermediateBlock class
 ****************************/
class IntermediateBlock : public Intermediate {
  vector<IntermediateInstruction*> instructions;

public:
  IntermediateBlock() {
  }

  ~IntermediateBlock() {
  }

  void AddInstruction(IntermediateInstruction* i) {
    instructions.push_back(i);
  }

  vector<IntermediateInstruction*> GetInstructions() {
    return instructions;
  }

  int GetSize() {
    return instructions.size();
  }

  void Clear() {
    instructions.clear();
  }

  bool IsEmpty() {
    return instructions.size() == 0;
  }

  void Write(bool is_debug, ofstream* file_out) {
    for(size_t i = 0; i < instructions.size(); i++) {
      instructions[i]->Write(is_debug, file_out);
    }
  }

  void Debug() {
    if(instructions.size() > 0) {
      for(size_t i = 0; i < instructions.size(); i++) {
        instructions[i]->Debug();
      }
      cout << "--" << endl;
    }
  }
};

/****************************
 * IntermediateMethod class
 ****************************/
class IntermediateMethod : public Intermediate {
  int id;
  string name;
  string rtrn_name;
  int space;
  int params;
  frontend::MethodType type;
  bool is_native;
  bool is_function;
  bool is_lib;
  bool is_virtual;
  bool has_and_or;
  int instr_count;
  vector<IntermediateBlock*> blocks;
  IntermediateDeclarations* entries;
  IntermediateClass* klass;
  map<IntermediateMethod*, int> registered_inlined_mthds; // TODO: remove
  
public:
  IntermediateMethod(int i, const string &n, bool v, bool h, const string &r,
                     frontend::MethodType t, bool nt, bool f, int c, int p,
                     IntermediateDeclarations* e, IntermediateClass* k) {
    id = i;
    name = n;
    is_virtual = v;
    has_and_or = h;
    rtrn_name = r;
    type = t;
    is_native = nt;
    is_function = f;
    space = c;
    params = p;
    entries = e;
    is_lib = false;
    klass = k;
    instr_count = 0;
  }

  IntermediateMethod(LibraryMethod* lib_method, IntermediateClass* k) {
    // set attributes
    id = lib_method->GetId();
    name = lib_method->GetName();
    is_virtual = lib_method->IsVirtual();
    has_and_or = lib_method->HasAndOr();
    rtrn_name = lib_method->GetEncodedReturn();
    type = lib_method->GetMethodType();
    is_native = lib_method->IsNative();
    is_function = lib_method->IsStatic();
    space = lib_method->GetSpace();
    params = lib_method->GetNumParams();
    entries = lib_method->GetEntries();
    is_lib = true;
    instr_count = 0;
    klass = k;
    // process instructions
    IntermediateBlock* block = new IntermediateBlock;
    vector<LibraryInstr*> lib_instructions = lib_method->GetInstructions();
    for(size_t i = 0; i < lib_instructions.size(); i++) {
      block->AddInstruction(IntermediateFactory::Instance()->MakeInstruction(lib_instructions[i]));
    }
    AddBlock(block);
  }

  ~IntermediateMethod() {
    // clean up
    while(!blocks.empty()) {
      IntermediateBlock* tmp = blocks.front();
      blocks.erase(blocks.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    if(entries) {
      delete entries;
      entries = NULL;
    }
  }
  
  int GetId() {
    return id;
  }

  IntermediateClass* GetClass() {
    return klass;
  }

  int GetSpace() {
    return space;
  }

  void SetSpace(int s) {
    space = s;
  }

  string GetName() {
    return name;
  }

  bool IsVirtual() {
    return is_virtual;
  }

  bool IsLibrary() {
    return is_lib;
  }

  void AddBlock(IntermediateBlock* b) {
    instr_count += b->GetSize();
    blocks.push_back(b);
  }

  int GetInstructionCount() {
    return instr_count;
  }

  int GetNumParams() {
    return params;
  }

  vector<IntermediateBlock*> GetBlocks() {
    return blocks;
  }

  void SetBlocks(vector<IntermediateBlock*> b) {
    blocks = b;
  }

  void Write(bool is_debug, ofstream* file_out) {
    // write attributes
    WriteInt(id, file_out);
    WriteInt(type, file_out);
    WriteInt(is_virtual, file_out);
    WriteInt(has_and_or, file_out);
    WriteInt(is_native, file_out);
    WriteInt(is_function, file_out);
    WriteString(name, file_out);
    WriteString(rtrn_name, file_out);

    // write local space size
    WriteInt(params, file_out);
    WriteInt(space, file_out);
    entries->Write(is_debug, file_out);

    // write statements
    for(size_t i = 0; i < blocks.size(); i++) {
      blocks[i]->Write(is_debug, file_out);
    }
    WriteByte(END_STMTS, file_out);
  }

  void Debug() {
    cout << "---------------------------------------------------------" << endl;
    cout << "Method: id=" << id << "; name='" << name << "'; return='" << rtrn_name
         << "';\n  blocks=" << blocks.size() << "; is_function=" << is_function << "; num_params="
         << params << "; mem_size=" << space << endl;
    cout << "---------------------------------------------------------" << endl;
    entries->Debug();
    cout << "---------------------------------------------------------" << endl;
    for(size_t i = 0; i < blocks.size(); i++) {
      blocks[i]->Debug();
    }
  }
};

/****************************
 * IntermediateClass class
 ****************************/
class IntermediateClass : public Intermediate {
  int id;
  string name;
  int pid;
  vector<int> interface_ids;
  string parent_name;
  vector<string> interface_names;
  int cls_space;
  int inst_space;
  vector<IntermediateBlock*> blocks;
  vector<IntermediateMethod*> methods;
  map<int, IntermediateMethod*> method_map;
  IntermediateDeclarations* entries;
  bool is_lib;
  bool is_interface;
  bool is_virtual;
  bool is_debug;
  string file_name;
  
public:
  IntermediateClass(int i, const string &n, int pi, const string &p, vector<int> infs, vector<string> in, 
		    bool is_inf, bool is_vrtl, int cs, int is, IntermediateDeclarations* e, const string &fn, bool d) {
    id = i;
    name = n;
    pid = pi;
    parent_name = p;
    interface_ids = infs;
    interface_names = in;
    is_interface = is_inf;
    is_virtual = is_vrtl;
    cls_space = cs;
    inst_space = is;
    entries = e;
    is_lib = false;
    is_debug = d;
    file_name = fn;
  }

  IntermediateClass(LibraryClass* lib_klass) {
    // set attributes
    id = lib_klass->GetId();
    name = lib_klass->GetName();
    pid = -1;
    parent_name = lib_klass->GetParentName();
    interface_names = lib_klass->GetInterfaceNames();
    is_interface = lib_klass->IsInterface();
    is_virtual = lib_klass->IsVirtual();
    is_debug = lib_klass->IsDebug();
    cls_space = lib_klass->GetClassSpace();
    inst_space = lib_klass->GetInstanceSpace();
    entries = lib_klass->GetEntries();
    // process methods
    map<const string, LibraryMethod*> lib_methods = lib_klass->GetMethods();
    map<const string, LibraryMethod*>::iterator mthd_iter;
    for(mthd_iter = lib_methods.begin(); mthd_iter != lib_methods.end(); ++mthd_iter) {
      LibraryMethod* lib_method = mthd_iter->second;
      IntermediateMethod* imm_method = new IntermediateMethod(lib_method, this);
      AddMethod(imm_method);
    }
    file_name = lib_klass->GetFileName();
    is_lib = true;
  }

  ~IntermediateClass() {
    // clean up
    while(!blocks.empty()) {
      IntermediateBlock* tmp = blocks.front();
      blocks.erase(blocks.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
    // clean up
    while(!methods.empty()) {
      IntermediateMethod* tmp = methods.front();
      methods.erase(methods.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
    // clean up
    if(entries) {
      delete entries;
      entries = NULL;
    }
  }

  int GetId() {
    return id;
  }

  const string& GetName() {
    return name;
  }

  bool IsLibrary() {
    return is_lib;
  }

  int GetInstanceSpace() {
    return inst_space;
  }

  void SetInstanceSpace(int s) {
    inst_space = s;
  }

  int GetClassSpace() {
    return cls_space;
  }

  void SetClassSpace(int s) {
    cls_space = s;
  }
  
  void AddMethod(IntermediateMethod* m) {
    methods.push_back(m);
    method_map.insert(pair<int, IntermediateMethod*>(m->GetId(), m));
  }

  void AddBlock(IntermediateBlock* b) {
    blocks.push_back(b);
  }

  IntermediateMethod* GetMethod(int id) {
    map<int, IntermediateMethod*>::iterator result = method_map.find(id);
#ifdef _DEBUG
    assert(result != method_map.end());
#endif
    return result->second;
  }

  vector<IntermediateMethod*> GetMethods() {
    return methods;
  }

  void Write(ofstream* file_out) {
    // write id and name
    WriteInt(id, file_out);
    WriteString(name, file_out);
    WriteInt(pid, file_out);
    WriteString(parent_name, file_out);
    
    // interface ids
    WriteInt(interface_ids.size(), file_out);
    for(size_t i = 0; i < interface_ids.size(); i++) {
      WriteInt(interface_ids[i], file_out);
    }

    // interface names
    WriteInt(interface_names.size(), file_out);
    for(size_t i = 0; i < interface_names.size(); i++) {
      WriteString(interface_names[i], file_out);
    }
    
    WriteInt(is_interface, file_out);
    WriteInt(is_virtual, file_out);
    WriteInt(is_debug, file_out);
    if(is_debug) {
      WriteString(file_name, file_out);
    }
    // write local space size
    WriteInt(cls_space, file_out);
    WriteInt(inst_space, file_out);
    entries->Write(is_debug, file_out);

    // write methods
    WriteInt((int)methods.size(), file_out);
    for(size_t i = 0; i < methods.size(); i++) {
      methods[i]->Write(is_debug, file_out);
    }
  }

  void Debug() {
    cout << "=========================================================" << endl;
    cout << "Class: id=" << id << "; name='" << name << "'; parent='" << parent_name
         << "'; pid=" << pid << ";\n interface=" << is_interface << "; virtual=" << is_virtual 
	 << "; num_methods=" << methods.size() << "; class_mem_size=" << cls_space 
	 << ";\n instance_mem_size=" << inst_space << "; is_debug=" << is_debug << endl;
    cout << "=========================================================" << endl;
    entries->Debug();
    cout << "=========================================================" << endl;
    for(size_t i = 0; i < blocks.size(); i++) {
      blocks[i]->Debug();
    }

    for(size_t i = 0; i < methods.size(); i++) {
      methods[i]->Debug();
    }
  }
};

/****************************
 * IntermediateEnumItem class
 ****************************/
class IntermediateEnumItem : public Intermediate {
  string name;
  INT_VALUE id;

public:
  IntermediateEnumItem(const string &n, const INT_VALUE i) {
    name = n;
    id = i;
  }

  IntermediateEnumItem(LibraryEnumItem* i) {
    name = i->GetName();
    id = i->GetId();
  }

  void Write(ofstream* file_out) {
    WriteString(name, file_out);
    WriteInt(id, file_out);
  }

  void Debug() {
    cout << "Item: name='" << name << "'; id='" << id << endl;
  }
};

/****************************
 * IntermediateEnum class
 ****************************/
class IntermediateEnum : public Intermediate {
  string name;
  INT_VALUE offset;
  vector<IntermediateEnumItem*> items;

public:
  IntermediateEnum(const string &n, const INT_VALUE o) {
    name = n;
    offset = o;
  }

  IntermediateEnum(LibraryEnum* e) {
    name = e->GetName();
    offset = e->GetOffset();
    // write items
    map<const string, LibraryEnumItem*> items = e->GetItems();
    map<const string, LibraryEnumItem*>::iterator iter;
    for(iter = items.begin(); iter != items.end(); ++iter) {
      LibraryEnumItem* lib_enum_item = iter->second;
      IntermediateEnumItem* imm_enum_item = new IntermediateEnumItem(lib_enum_item);
      AddItem(imm_enum_item);
    }
  }

  void AddItem(IntermediateEnumItem* i) {
    items.push_back(i);
  }

  void Write(ofstream* file_out) {
    WriteString(name, file_out);
    WriteInt(offset, file_out);
    // write items
    WriteInt((int)items.size(), file_out);
    for(size_t i = 0; i < items.size(); i++) {
      items[i]->Write(file_out);
    }
  }

  void Debug() {
    cout << "=========================================================" << endl;
    cout << "Enum: name='" << name << "'; items=" << items.size() << endl;
    cout << "=========================================================" << endl;

    for(size_t i = 0; i < items.size(); i++) {
      items[i]->Debug();
    }
  }
};

/****************************
 * IntermediateProgram class
 ****************************/
class IntermediateProgram : public Intermediate {
  int class_id;
  int method_id;
  vector<IntermediateEnum*> enums;
  vector<IntermediateClass*> classes;
  map<int, IntermediateClass*> class_map;
  vector<string> char_strings;
  vector<frontend::IntStringHolder*> int_strings;
  vector<frontend::FloatStringHolder*> float_strings;
  vector<string> bundle_names;
  int num_src_classes;
  int num_lib_classes;
  int string_cls_id;
  
public:
  IntermediateProgram() {
    num_src_classes = num_lib_classes = 0;
    string_cls_id = -1;
  }

  ~IntermediateProgram() {
    // clean up
    while(!enums.empty()) {
      IntermediateEnum* tmp = enums.front();
      enums.erase(enums.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    while(!classes.empty()) {
      IntermediateClass* tmp = classes.front();
      classes.erase(classes.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    while(!int_strings.empty()) {
      frontend::IntStringHolder* tmp = int_strings.front();
      delete[] tmp->value;
      tmp->value = NULL;
      int_strings.erase(int_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
    
    while(!float_strings.empty()) {
      frontend::FloatStringHolder* tmp = float_strings.front();
      delete[] tmp->value;
      tmp->value = NULL;
      float_strings.erase(float_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    IntermediateFactory::Instance()->Clear();
  }

  void AddClass(IntermediateClass* c) {
    classes.push_back(c);
    class_map.insert(pair<int, IntermediateClass*>(c->GetId(), c));
  }

  IntermediateClass* GetClass(int id) {
    map<int, IntermediateClass*>::iterator result = class_map.find(id);
#ifdef _DEBUG
    assert(result != class_map.end());
#endif
    return result->second;
  }

  void AddEnum(IntermediateEnum* e) {
    enums.push_back(e);
  }

  vector<IntermediateClass*> GetClasses() {
    return classes;
  }

  void SetCharStrings(vector<string> s) {
    char_strings = s;
  }
  
  void SetIntStrings(vector<frontend::IntStringHolder*> s) {
    int_strings = s;
  }

  void SetFloatStrings(vector<frontend::FloatStringHolder*> s) {
    float_strings = s;
  }
  
  void SetStartIds(int c, int m) {
    class_id = c;
    method_id = m;
  }

  int GetStartClassId() {
    return class_id;
  }

  int GetStartMethodId() {
    return method_id;
  }

  void SetBundleNames(vector<string> n) {
    bundle_names = n;
  }

  void SetStringClassId(int i) {
    string_cls_id = i;
  }

  void Write(ofstream* file_out, bool is_lib) {
    // version
    WriteInt(VER_NUM, file_out);
    
    // magic number
    if(is_lib) {
      WriteInt(MAGIC_NUM_LIB, file_out);
    } 
    else {
      WriteInt(MAGIC_NUM_EXE, file_out);
    }    
    
    // write string id
    if(!is_lib) {
#ifdef _DEBUG
      assert(string_cls_id > 0);
#endif
      WriteInt(string_cls_id, file_out);
    }
    
    // write float strings
    WriteInt((int)float_strings.size(), file_out);
    for(size_t i = 0; i < float_strings.size(); i++) {
      frontend::FloatStringHolder* holder = float_strings[i];
      WriteInt(holder->length, file_out);
      for(int j = 0; j < holder->length; j++) {
	WriteDouble(holder->value[j], file_out);
      }
    }
    // write int strings
    WriteInt((int)int_strings.size(), file_out);
    for(size_t i = 0; i < int_strings.size(); i++) {
      frontend::IntStringHolder* holder = int_strings[i];
      WriteInt(holder->length, file_out);
      for(int j = 0; j < holder->length; j++) {
	WriteInt(holder->value[j], file_out);
      }
    }
    // write char strings
    WriteInt((int)char_strings.size(), file_out);
    for(size_t i = 0; i < char_strings.size(); i++) {
      WriteString(char_strings[i], file_out);
    }
    
    // write bundle names
    if(is_lib) {
      WriteInt((int)bundle_names.size(), file_out);
      for(size_t i = 0; i < bundle_names.size(); i++) {
        WriteString(bundle_names[i], file_out);
      }
    }

    // program start
    if(!is_lib) {
      WriteInt(class_id, file_out);
      WriteInt(method_id, file_out);
    }
    // program enums
    WriteInt((int)enums.size(), file_out);
    for(size_t i = 0; i < enums.size(); i++) {
      enums[i]->Write(file_out);
    }
    // program classes
    WriteInt((int)classes.size(), file_out);
    for(size_t i = 0; i < classes.size(); i++) {
      if(classes[i]->IsLibrary()) {
        num_lib_classes++;
      } else {
        num_src_classes++;
      }
      classes[i]->Write(file_out);
    }
    
    cout << "Compiled " << num_src_classes
         << (num_src_classes > 1 ? " source classes." : " source class.")  << endl;
    cout << "Linked " << num_lib_classes
         << (num_lib_classes > 1 ? " library classes." : " library class.")  << endl;
  }

  void Debug() {
    cout << "Strings:" << endl;
    for(size_t i = 0; i < char_strings.size(); i++) {
      cout << "string id=" << i << ", value='" << char_strings[i] << "'" << endl;
    }
    cout << endl;

    cout << "Program: enums=" << enums.size() << ", classes="
         << classes.size() << "; start=" << class_id << "," << method_id << endl;
    // enums
    for(size_t i = 0; i < enums.size(); i++) {
      enums[i]->Debug();
    }
    // classes
    for(size_t i = 0; i < classes.size(); i++) {
      classes[i]->Debug();
    }
  }
};

/****************************
 * TargetEmitter class
 ****************************/
class TargetEmitter {
  IntermediateProgram* program;
  string file_name;
  bool is_lib;
  
  bool EndsWith(string const &str, string const &ending) {
    if(str.length() >= ending.length()) {
      return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
    } 
    
    return false;
  }
  
public:
  TargetEmitter(IntermediateProgram* p, bool l, const string &n) {
    program = p;
    is_lib = l;
    file_name = n;
  }

  ~TargetEmitter() {
    delete program;
    program = NULL;
  }

  void Emit();
};
}

#endif
