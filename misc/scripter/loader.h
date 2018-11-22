/***************************************************************************
 * Defines how the intermediate code is loaded for scripting VM
 *
 * Copyright (c) 2017, Randy Hollines
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

#include "../compiler/types.h"
#include "../compiler/linker.h"
#include "../compiler/tree.h"
#include "../shared/instrs.h"
#include "../shared/version.h"
#include "../vm/common.h"

using namespace std;
using namespace instructions;

namespace backend {
  class IntermediateClass;
  
  wstring ReplaceSubstring(wstring s, const wstring& f, const wstring &r);
  
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
    void WriteString(const wstring &in, ofstream &file_out) {
      string out;
      if(!UnicodeToBytes(in, out)) {
        wcerr << L">>> Unable to write unicode string <<<" << endl;
        exit(1);
      }
      WriteInt(out.size(), file_out);
      file_out.write(out.c_str(), out.size());
    }

    void WriteByte(char value, ofstream &file_out) {
      file_out.write(&value, sizeof(value));
    }

    void WriteInt(int value, ofstream &file_out) {
      file_out.write((char*)&value, sizeof(value));
    }

    void WriteChar(wchar_t value, ofstream &file_out) {
      string buffer;
      if(!CharacterToBytes(value, buffer)) {
        wcerr << L">>> Unable to write character <<<" << endl;
        exit(1);
      }

      // write bytes
      if(buffer.size()) {
        WriteInt(buffer.size(), file_out);
        file_out.write(buffer.c_str(), buffer.size());
      }
      else {
        WriteInt(0, file_out);
      }
    }

    void WriteDouble(FLOAT_VALUE value, ofstream &file_out) {
      file_out.write((char*)&value, sizeof(value));
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
    wstring operand5;
    wstring operand6;
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

    IntermediateInstruction(int l, InstructionType t, wstring o5) {
      line_num = l;
      type = t;
      operand5 = o5;
    }

    IntermediateInstruction(int l, InstructionType t, int o3, wstring o5, wstring o6) {
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

    void Load(StackMethod* vm_method, vector<StackInstr*>& instrs, int index, bool is_debug) {
      switch(type) {
      case LOAD_INT_LIT:
        instrs.push_back(new StackInstr(line_num, LOAD_INT_LIT, (long)operand));
        break;

      case LOAD_CHAR_LIT:
        instrs.push_back(new StackInstr(line_num, LOAD_CHAR_LIT, (long)operand));
        break;

      case SHL_INT:
        instrs.push_back(new StackInstr(line_num, SHL_INT));
        break;

      case SHR_INT:
        instrs.push_back(new StackInstr(line_num, SHR_INT));
        break;

      case LOAD_INT_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num,
                         mem_context == LOCL ? LOAD_LOCL_INT_VAR : LOAD_CLS_INST_INT_VAR,
                         id, mem_context));
      }
        break;

      case LOAD_FUNC_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, LOAD_FUNC_VAR, id, mem_context));
      }
         break;

      case LOAD_FLOAT_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, LOAD_FLOAT_VAR, id, mem_context));
      }
          break;

      case STOR_INT_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num,
                         mem_context == LOCL ? STOR_LOCL_INT_VAR : STOR_CLS_INST_INT_VAR,
                         id, mem_context));
      }
        break;

      case STOR_FUNC_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, STOR_FUNC_VAR, id, mem_context));
      }
         break;

      case STOR_FLOAT_VAR: {
        long id = operand;
        long mem_context = operand2;
        instrs.push_back(new StackInstr(line_num, STOR_FLOAT_VAR, id, mem_context));
      }
          break;

      case COPY_INT_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num,
                         mem_context == LOCL ? COPY_LOCL_INT_VAR : COPY_CLS_INST_INT_VAR,
                         id, mem_context));
      }
        break;

      case COPY_FLOAT_VAR: {
        long id = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, COPY_FLOAT_VAR, id, mem_context));
      }
          break;

      case LOAD_BYTE_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, LOAD_BYTE_ARY_ELM, dim, mem_context));
      }
             break;

      case LOAD_CHAR_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, LOAD_CHAR_ARY_ELM, dim, mem_context));
      }
             break;

      case LOAD_INT_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, LOAD_INT_ARY_ELM, dim, mem_context));
      }
            break;

      case LOAD_FLOAT_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, LOAD_FLOAT_ARY_ELM, dim, mem_context));
      }
              break;

      case STOR_BYTE_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, STOR_BYTE_ARY_ELM, dim, mem_context));
      }
             break;

      case STOR_CHAR_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, STOR_CHAR_ARY_ELM, dim, mem_context));
      }
             break;

      case STOR_INT_ARY_ELM: {
        long dim = operand;
        MemoryContext mem_context = (MemoryContext)operand2;
        instrs.push_back(new StackInstr(line_num, STOR_INT_ARY_ELM, dim, mem_context));
      }
            break;

      case STOR_FLOAT_ARY_ELM: {
        long dim = operand;
        long mem_context = operand2;
        instrs.push_back(new StackInstr(line_num, STOR_FLOAT_ARY_ELM, dim, mem_context));
      }
              break;

      case NEW_FLOAT_ARY: {
        long dim = operand;
        instrs.push_back(new StackInstr(line_num, NEW_FLOAT_ARY, dim));
      }
         break;

      case NEW_INT_ARY: {
        long dim = operand;
        instrs.push_back(new StackInstr(line_num, NEW_INT_ARY, dim));
      }
          break;

      case NEW_BYTE_ARY: {
        long dim = operand;
        instrs.push_back(new StackInstr(line_num, NEW_BYTE_ARY, dim));

      }
        break;

      case NEW_CHAR_ARY: {
        long dim = operand;
        instrs.push_back(new StackInstr(line_num, NEW_CHAR_ARY, dim));

      }
        break;

      case NEW_OBJ_INST: {
        long obj_id = operand;
        instrs.push_back(new StackInstr(line_num, NEW_OBJ_INST, obj_id));
      }
        break;

      case DYN_MTHD_CALL: {
        long num_params = operand;
        long rtrn_type = operand2;
        instrs.push_back(new StackInstr(line_num, DYN_MTHD_CALL, num_params, rtrn_type));
      }
         break;

      case MTHD_CALL: {
        long cls_id = operand;
        long mthd_id = operand2;
        long is_native = operand3;
        instrs.push_back(new StackInstr(line_num, MTHD_CALL, cls_id, mthd_id, is_native));
      }
        break;

      case ASYNC_MTHD_CALL: {
        long cls_id = operand;
        long mthd_id = operand2;
        long is_native = operand3;
        instrs.push_back(new StackInstr(line_num, ASYNC_MTHD_CALL, cls_id, mthd_id, is_native));
      }
           break;

      case LIB_OBJ_INST_CAST:
        wcerr << L">>> unsupported instruction for executable: LIB_OBJ_INST_CAST <<<" << endl;
        exit(1);

      case LIB_NEW_OBJ_INST:
        wcerr << L">>> unsupported instruction for executable: LIB_NEW_OBJ_INST <<<" << endl;
        exit(1);

      case LIB_MTHD_CALL:
        wcerr << L">>> unsupported instruction for executable: LIB_MTHD_CALL <<<" << endl;
        exit(1);

      case JMP: {
        long label = operand;
        long cond = operand2;
        instrs.push_back(new StackInstr(line_num, JMP, label, cond));
      }
             break;

      case LBL: {
        long id = operand;
        instrs.push_back(new StackInstr(line_num, LBL, id));
        vm_method->AddLabel(id, index);
      }
             break;

      case OBJ_INST_CAST: {
        long to = operand;
        instrs.push_back(new StackInstr(line_num, OBJ_INST_CAST, to));
      }
         break;

      case OBJ_TYPE_OF: {
        long check = operand;
        instrs.push_back(new StackInstr(line_num, OBJ_TYPE_OF, check));
      }
          break;

      case OR_INT:
        instrs.push_back(new StackInstr(line_num, OR_INT));
        break;

      case AND_INT:
        instrs.push_back(new StackInstr(line_num, AND_INT));
        break;

      case ADD_INT:
        instrs.push_back(new StackInstr(line_num, ADD_INT));
        break;

      case CEIL_FLOAT:
        instrs.push_back(new StackInstr(line_num, CEIL_FLOAT));
        break;

      case CPY_BYTE_ARY:
        instrs.push_back(new StackInstr(line_num, CPY_BYTE_ARY));
        break;

      case CPY_CHAR_ARY:
        instrs.push_back(new StackInstr(line_num, CPY_CHAR_ARY));
        break;

      case CPY_INT_ARY:
        instrs.push_back(new StackInstr(line_num, CPY_INT_ARY));
        break;

      case CPY_FLOAT_ARY:
        instrs.push_back(new StackInstr(line_num, CPY_FLOAT_ARY));
        break;

      case FLOR_FLOAT:
        instrs.push_back(new StackInstr(line_num, FLOR_FLOAT));
        break;

      case SIN_FLOAT:
        instrs.push_back(new StackInstr(line_num, SIN_FLOAT));
        break;

      case COS_FLOAT:
        instrs.push_back(new StackInstr(line_num, COS_FLOAT));
        break;

      case TAN_FLOAT:
        instrs.push_back(new StackInstr(line_num, TAN_FLOAT));
        break;

      case ASIN_FLOAT:
        instrs.push_back(new StackInstr(line_num, ASIN_FLOAT));
        break;

      case ACOS_FLOAT:
        instrs.push_back(new StackInstr(line_num, ACOS_FLOAT));
        break;

      case ATAN_FLOAT:
        instrs.push_back(new StackInstr(line_num, ATAN_FLOAT));
        break;

      case LOG_FLOAT:
        instrs.push_back(new StackInstr(line_num, LOG_FLOAT));
        break;

      case POW_FLOAT:
        instrs.push_back(new StackInstr(line_num, POW_FLOAT));
        break;

      case SQRT_FLOAT:
        instrs.push_back(new StackInstr(line_num, SQRT_FLOAT));
        break;

      case RAND_FLOAT:
        instrs.push_back(new StackInstr(line_num, RAND_FLOAT));
        break;

      case F2I:
        instrs.push_back(new StackInstr(line_num, F2I));
        break;

      case I2F:
        instrs.push_back(new StackInstr(line_num, I2F));
        break;

      case S2I:
        instrs.push_back(new StackInstr(line_num, S2I));
        break;

      case S2F:
        instrs.push_back(new StackInstr(line_num, S2F));
        break;

      case SWAP_INT:
        instrs.push_back(new StackInstr(line_num, SWAP_INT));
        break;

      case POP_INT:
        instrs.push_back(new StackInstr(line_num, POP_INT));
        break;

      case POP_FLOAT:
        instrs.push_back(new StackInstr(line_num, POP_FLOAT));
        break;

      case LOAD_CLS_MEM:
        instrs.push_back(new StackInstr(line_num, LOAD_CLS_MEM));
        break;

      case LOAD_INST_MEM:
        instrs.push_back(new StackInstr(line_num, LOAD_INST_MEM));
        break;

      case LOAD_ARY_SIZE:
        instrs.push_back(new StackInstr(line_num, LOAD_ARY_SIZE));
        break;

      case SUB_INT:
        instrs.push_back(new StackInstr(line_num, SUB_INT));
        break;

      case MUL_INT:
        instrs.push_back(new StackInstr(line_num, MUL_INT));
        break;

      case DIV_INT:
        instrs.push_back(new StackInstr(line_num, DIV_INT));
        break;

      case MOD_INT:
        instrs.push_back(new StackInstr(line_num, MOD_INT));
        break;

      case BIT_AND_INT:
        instrs.push_back(new StackInstr(line_num, BIT_AND_INT));
        break;

      case BIT_OR_INT:
        instrs.push_back(new StackInstr(line_num, BIT_OR_INT));
        break;

      case BIT_XOR_INT:
        instrs.push_back(new StackInstr(line_num, BIT_XOR_INT));
        break;

      case EQL_INT:
        instrs.push_back(new StackInstr(line_num, EQL_INT));
        break;

      case NEQL_INT:
        instrs.push_back(new StackInstr(line_num, NEQL_INT));
        break;

      case LES_INT:
        instrs.push_back(new StackInstr(line_num, LES_INT));
        break;

      case GTR_INT:
        instrs.push_back(new StackInstr(line_num, GTR_INT));
        break;

      case LES_EQL_INT:
        instrs.push_back(new StackInstr(line_num, LES_EQL_INT));
        break;

      case LES_EQL_FLOAT:
        instrs.push_back(new StackInstr(line_num, LES_EQL_FLOAT));
        break;

      case GTR_EQL_INT:
        instrs.push_back(new StackInstr(line_num, GTR_EQL_INT));
        break;

      case GTR_EQL_FLOAT:
        instrs.push_back(new StackInstr(line_num, GTR_EQL_FLOAT));
        break;

      case ADD_FLOAT:
        instrs.push_back(new StackInstr(line_num, ADD_FLOAT));
        break;

      case SUB_FLOAT:
        instrs.push_back(new StackInstr(line_num, SUB_FLOAT));
        break;

      case MUL_FLOAT:
        instrs.push_back(new StackInstr(line_num, MUL_FLOAT));
        break;

      case DIV_FLOAT:
        instrs.push_back(new StackInstr(line_num, DIV_FLOAT));
        break;

      case EQL_FLOAT:
        instrs.push_back(new StackInstr(line_num, EQL_FLOAT));
        break;

      case NEQL_FLOAT:
        instrs.push_back(new StackInstr(line_num, NEQL_FLOAT));
        break;

      case LES_FLOAT:
        instrs.push_back(new StackInstr(line_num, LES_FLOAT));
        break;

      case GTR_FLOAT:
        instrs.push_back(new StackInstr(line_num, GTR_FLOAT));
        break;

      case LOAD_FLOAT_LIT:
        instrs.push_back(new StackInstr(line_num, LOAD_FLOAT_LIT, operand4));
        break;

      case RTRN:
        if(is_debug) {
          instrs.push_back(new StackInstr(line_num + 1, RTRN));
        }
        else {
          instrs.push_back(new StackInstr(line_num, RTRN));
        }
        break;

      case DLL_LOAD:
        instrs.push_back(new StackInstr(line_num, DLL_LOAD));
        break;

      case DLL_UNLOAD:
        instrs.push_back(new StackInstr(line_num, DLL_UNLOAD));
        break;

      case DLL_FUNC_CALL:
        instrs.push_back(new StackInstr(line_num, DLL_FUNC_CALL));
        break;

      case THREAD_JOIN:
        instrs.push_back(new StackInstr(line_num, THREAD_JOIN));
        break;

      case THREAD_SLEEP:
        instrs.push_back(new StackInstr(line_num, THREAD_SLEEP));
        break;

      case THREAD_MUTEX:
        instrs.push_back(new StackInstr(line_num, THREAD_MUTEX));
        break;

      case CRITICAL_START:
        instrs.push_back(new StackInstr(line_num, CRITICAL_START));
        break;

      case CRITICAL_END:
        instrs.push_back(new StackInstr(line_num, CRITICAL_END));
        break;

      case TRAP: {
        long args = operand;
        instrs.push_back(new StackInstr(line_num, TRAP, args));
      }
              break;

      case TRAP_RTRN: {
        long args = operand;
        instrs.push_back(new StackInstr(line_num, TRAP_RTRN, args));
      }
        break;

      default: {
#ifdef _DEBUG
        InstructionType instr = (InstructionType)type;
        wcout << L">>> unknown instruction: id=" << instr << L" <<<" << endl;
#endif
        exit(1);
      }
        break;

      }





      /*
      WriteByte((int)type, file_out);
      if(is_debug) {
        WriteInt(line_num, file_out);
      }
      switch(type) {
      case LOAD_INT_LIT:

      case NEW_FLOAT_ARY:
      case NEW_INT_ARY:
      case NEW_BYTE_ARY:
      case NEW_CHAR_ARY:
      case NEW_OBJ_INST:
      case OBJ_INST_CAST:
      case OBJ_TYPE_OF:
      case TRAP:
      case TRAP_RTRN:
        WriteInt(operand, file_out);
        break;

      case LOAD_CHAR_LIT:
        WriteChar(operand, file_out);
        break;

      case instructions::ASYNC_MTHD_CALL:
      case MTHD_CALL:
        WriteInt(operand, file_out);
        WriteInt(operand2, file_out);
        WriteInt(operand3, file_out);
        break;

      case LIB_NEW_OBJ_INST:
      case LIB_OBJ_INST_CAST:
      case LIB_OBJ_TYPE_OF:
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
      case LOAD_CHAR_ARY_ELM:
      case LOAD_INT_ARY_ELM:
      case LOAD_FLOAT_ARY_ELM:
      case STOR_BYTE_ARY_ELM:
      case STOR_CHAR_ARY_ELM:
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
      */
    }

    void Debug() {
      switch(type) {
      case SWAP_INT:
        wcout << L"SWAP_INT" << endl;
        break;

      case POP_INT:
        wcout << L"POP_INT" << endl;
        break;

      case POP_FLOAT:
        wcout << L"POP_FLOAT" << endl;
        break;

      case LOAD_INT_LIT:
        wcout << L"LOAD_INT_LIT: value=" << operand << endl;
        break;

      case LOAD_CHAR_LIT:
        wcout << L"LOAD_CHAR_LIT value='" << (wchar_t)operand << L"'" << endl;
        break;

      case DYN_MTHD_CALL:
        wcout << L"DYN_MTHD_CALL num_params=" << operand 
          << L", rtrn_type=" << operand2 << endl;
        break;

      case SHL_INT:
        wcout << L"SHL_INT" << endl;
        break;

      case SHR_INT:
        wcout << L"SHR_INT" << endl;
        break;

      case LOAD_FLOAT_LIT:
        wcout << L"LOAD_FLOAT_LIT: value=" << operand4 << endl;
        break;

      case LOAD_FUNC_VAR:
        wcout << L"LOAD_FUNC_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_INT_VAR:
        wcout << L"LOAD_INT_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_FLOAT_VAR:
        wcout << L"LOAD_FLOAT_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_BYTE_ARY_ELM:
        wcout << L"LOAD_BYTE_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_CHAR_ARY_ELM:
        wcout << L"LOAD_CHAR_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_INT_ARY_ELM:
        wcout << L"LOAD_INT_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_FLOAT_ARY_ELM:
        wcout << L"LOAD_FLOAT_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case LOAD_CLS_MEM:
        wcout << L"LOAD_CLS_MEM" << endl;
        break;

      case LOAD_INST_MEM:
        wcout << L"LOAD_INST_MEM" << endl;
        break;

      case STOR_FUNC_VAR:
        wcout << L"STOR_FUNC_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case STOR_INT_VAR:
        wcout << L"STOR_INT_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case STOR_FLOAT_VAR:
        wcout << L"STOR_FLOAT_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case COPY_FUNC_VAR:
        wcout << L"COPY_FUNC_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case COPY_INT_VAR:
        wcout << L"COPY_INT_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case COPY_FLOAT_VAR:
        wcout << L"COPY_FLOAT_VAR: id=" << operand << L", local="
          << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case STOR_BYTE_ARY_ELM:
        wcout << L"STOR_BYTE_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case STOR_CHAR_ARY_ELM:
        wcout << L"STOR_CHAR_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case STOR_INT_ARY_ELM:
        wcout << L"STOR_INT_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case STOR_FLOAT_ARY_ELM:
        wcout << L"STOR_FLOAT_ARY_ELM: dimension=" << operand
          << L", local=" << (operand2 == LOCL ? "true" : "false") << endl;
        break;

      case instructions::ASYNC_MTHD_CALL:
        wcout << L"ASYNC_MTHD_CALL: class=" << operand << L", method="
          << operand2 << L"; native=" << (operand3 ? "true" : "false") << endl;
        break;

      case instructions::DLL_LOAD:
        wcout << L"DLL_LOAD" << endl;
        break;

      case instructions::DLL_UNLOAD:
        wcout << L"DLL_UNLOAD" << endl;
        break;

      case instructions::DLL_FUNC_CALL:
        wcout << L"DLL_FUNC_CALL" << endl;
        break;

      case instructions::THREAD_JOIN:
        wcout << L"THREAD_JOIN" << endl;
        break;

      case instructions::THREAD_SLEEP:
        wcout << L"THREAD_SLEEP" << endl;
        break;

      case instructions::THREAD_MUTEX:
        wcout << L"THREAD_MUTEX" << endl;
        break;

      case CRITICAL_START:
        wcout << L"CRITICAL_START" << endl;
        break;

      case CRITICAL_END:
        wcout << L"CRITICAL_END" << endl;
        break;

      case AND_INT:
        wcout << L"AND_INT" << endl;
        break;

      case OR_INT:
        wcout << L"OR_INT" << endl;
        break;

      case ADD_INT:
        wcout << L"ADD_INT" << endl;
        break;

      case SUB_INT:
        wcout << L"SUB_INT" << endl;
        break;

      case MUL_INT:
        wcout << L"MUL_INT" << endl;
        break;

      case DIV_INT:
        wcout << L"DIV_INT" << endl;
        break;

      case MOD_INT:
        wcout << L"MOD_INT" << endl;
        break;

      case BIT_AND_INT:
        wcout << L"BIT_AND_INT" << endl;
        break;

      case BIT_OR_INT:
        wcout << L"BIT_OR_INT" << endl;
        break;

      case BIT_XOR_INT:
        wcout << L"BIT_XOR_INT" << endl;
        break;

      case EQL_INT:
        wcout << L"EQL_INT" << endl;
        break;

      case NEQL_INT:
        wcout << L"NEQL_INT" << endl;
        break;

      case LES_INT:
        wcout << L"LES_INT" << endl;
        break;

      case GTR_INT:
        wcout << L"GTR_INT" << endl;
        break;

      case LES_EQL_INT:
        wcout << L"LES_EQL_INT" << endl;
        break;

      case GTR_EQL_INT:
        wcout << L"GTR_EQL_INT" << endl;
        break;

      case ADD_FLOAT:
        wcout << L"ADD_FLOAT" << endl;
        break;

      case SUB_FLOAT:
        wcout << L"SUB_FLOAT" << endl;
        break;

      case MUL_FLOAT:
        wcout << L"MUL_FLOAT" << endl;
        break;

      case DIV_FLOAT:
        wcout << L"DIV_FLOAT" << endl;
        break;

      case EQL_FLOAT:
        wcout << L"EQL_FLOAT" << endl;
        break;

      case NEQL_FLOAT:
        wcout << L"NEQL_FLOAT" << endl;
        break;

      case LES_EQL_FLOAT:
        wcout << L"LES_EQL_FLOAT" << endl;
        break;

      case LES_FLOAT:
        wcout << L"LES_FLOAT" << endl;
        break;

      case GTR_FLOAT:
        wcout << L"GTR_FLOAT" << endl;
        break;

      case GTR_EQL_FLOAT:
        wcout << L"LES_EQL_FLOAT" << endl;
        break;

      case instructions::FLOR_FLOAT:
        wcout << L"FLOR_FLOAT" << endl;
        break;

      case instructions::LOAD_ARY_SIZE:
        wcout << L"LOAD_ARY_SIZE" << endl;
        break;

      case instructions::CPY_BYTE_ARY:
        wcout << L"CPY_BYTE_ARY" << endl;
        break;

      case instructions::CPY_CHAR_ARY:
        wcout << L"CPY_CHAR_ARY" << endl;
        break;

      case instructions::CPY_INT_ARY:
        wcout << L"CPY_INT_ARY" << endl;
        break;

      case instructions::CPY_FLOAT_ARY:
        wcout << L"CPY_FLOAT_ARY" << endl;
        break;

      case instructions::CEIL_FLOAT:
        wcout << L"CEIL_FLOAT" << endl;
        break;

      case instructions::RAND_FLOAT:
        wcout << L"RAND_FLOAT" << endl;
        break;

      case instructions::SIN_FLOAT:
        wcout << L"SIN_FLOAT" << endl;
        break;

      case instructions::COS_FLOAT:
        wcout << L"COS_FLOAT" << endl;
        break;

      case instructions::TAN_FLOAT:
        wcout << L"TAN_FLOAT" << endl;
        break;

      case instructions::ASIN_FLOAT:
        wcout << L"ASIN_FLOAT" << endl;
        break;

      case instructions::ACOS_FLOAT:
        wcout << L"ACOS_FLOAT" << endl;
        break;

      case instructions::ATAN_FLOAT:
        wcout << L"ATAN_FLOAT" << endl;
        break;

      case instructions::LOG_FLOAT:
        wcout << L"LOG_FLOAT" << endl;
        break;

      case instructions::POW_FLOAT:
        wcout << L"POW_FLOAT" << endl;
        break;

      case instructions::SQRT_FLOAT:
        wcout << L"SQRT_FLOAT" << endl;
        break;

      case I2F:
        wcout << L"I2F" << endl;
        break;

      case F2I:
        wcout << L"F2I" << endl;
        break;

      case RTRN:
        wcout << L"RTRN" << endl;
        break;

      case MTHD_CALL:
        wcout << L"MTHD_CALL: class=" << operand << L", method="
          << operand2 << L"; native=" << (operand3 ? "true" : "false") << endl;
        break;

      case LIB_NEW_OBJ_INST:
        wcout << L"LIB_NEW_OBJ_INST: class='" << operand5 << L"'" << endl;
        break;

      case LIB_OBJ_TYPE_OF:
        wcout << L"LIB_OBJ_TYPE_OF: class='" << operand5 << L"'" << endl;
        break;
	
      case LIB_OBJ_INST_CAST:
        wcout << L"LIB_OBJ_INST_CAST: to_class='" << operand5 << L"'" << endl;
        break;

      case LIB_MTHD_CALL:
        wcout << L"LIB_MTHD_CALL: class='" << operand5 << L"', method='"
          << operand6 << L"'; native=" << (operand3 ? "true" : "false") << endl;
        break;

      case LIB_FUNC_DEF:
        wcout << L"LIB_FUNC_DEF: class='" << operand5 << L"', method='" 
          << operand6 << L"'" << endl;
        break;

      case LBL:
        wcout << L"LBL: id=" << operand << endl;
        break;

      case JMP:
        if(operand2 == -1) {
          wcout << L"JMP: id=" << operand << endl;
        } else {
          wcout << L"JMP: id=" << operand << L" conditional="
            << (operand2 ? "true" : "false") << endl;
        }
        break;

      case OBJ_INST_CAST:
        wcout << L"OBJ_INST_CAST: to=" << operand << endl;
        break;

      case OBJ_TYPE_OF:
        wcout << L"OBJ_TYPE_OF: check=" << operand << endl;
        break;

      case NEW_FLOAT_ARY:
        wcout << L"NEW_FLOAT_ARY: dimension=" << operand << endl;
        break;

      case NEW_INT_ARY:
        wcout << L"NEW_INT_ARY: dimension=" << operand << endl;
        break;

      case NEW_BYTE_ARY:
        wcout << L"NEW_BYTE_ARY: dimension=" << operand << endl;
        break;

      case NEW_CHAR_ARY:
        wcout << L"NEW_CHAR_ARY: dimension=" << operand << endl;
        break;

      case NEW_OBJ_INST:
        wcout << L"NEW_OBJ_INST: class=" << operand << endl;
        break;

      case TRAP:
        wcout << L"TRAP: args=" << operand << endl;
        break;

      case TRAP_RTRN:
        wcout << L"TRAP_RTRN: args=" << operand << endl;
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

    IntermediateInstruction* MakeInstruction(int l, InstructionType t, wstring o5) {
      IntermediateInstruction* tmp = new IntermediateInstruction(l, t, o5);
      instructions.push_back(tmp);
      return tmp;
    }

    IntermediateInstruction* MakeInstruction(int l, InstructionType t, int o3, wstring o5, wstring o6) {
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

    void Load(StackMethod* vm_method, vector<StackInstr*>& instrs, bool is_debug) {
      for(size_t i = 0; i < instructions.size(); ++i) {
        instructions[i]->Load(vm_method, instrs, i, is_debug);
      }
    }

    void Debug() {
      if(instructions.size() > 0) {
        for(size_t i = 0; i < instructions.size(); ++i) {
          instructions[i]->Debug();
        }
        wcout << L"--" << endl;
      }
    }
  };

  /****************************
  * IntermediateMethod class
  **************************/
  class IntermediateMethod : public Intermediate {
    int id;
    wstring name;
    wstring rtrn_name;
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

  public:
    IntermediateMethod(int i, const wstring &n, bool v, bool h, const wstring &r,
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
      for(size_t i = 0; i < lib_instructions.size(); ++i) {
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
    
    IntermediateDeclarations* GetEntries() {
      return entries;
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

    wstring GetName() {
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

    bool HasAndOr() {
      return has_and_or;
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

    StackMethod* Load(StackClass* vm_cls, bool is_debug) {
      // parse return
      MemoryType rtrn_type;
      switch(rtrn_name[0]) {
      case L'l': // bool
      case L'b': // byte
      case L'c': // character
      case L'i': // int
      case L'o': // object
        rtrn_type = MemoryType::INT_TYPE;
        break;

      case L'f': // float
        if(rtrn_name.size() > 1) {
          rtrn_type = MemoryType::INT_TYPE;
        }
        else {
          rtrn_type = MemoryType::FLOAT_TYPE;
        }
        break;

      case L'n': // nil
        rtrn_type = MemoryType::NIL_TYPE;
        break;

      case L'm': // function
        rtrn_type = MemoryType::FUNC_TYPE;
        break;

      default:
        wcerr << L">>> unknown type <<<" << endl;
        exit(1);
        break;
      }

      // read instance types
      vector<IntermediateDeclaration*> vm_params = entries->GetParameters();
      StackDclr** dclrs = new StackDclr*[vm_params.size()];
      for(size_t i = 0; i < vm_params.size(); ++i) {
        IntermediateDeclaration* entry = vm_params[i];
        dclrs[i] = new StackDclr;
        dclrs[i]->type = entry->GetType();
        if(is_debug) {
          dclrs[i]->name = entry->GetName();
        }
      }

      StackMethod* vm_method = new StackMethod(id, name, is_virtual, has_and_or, dclrs,
                                             vm_params.size(), space, params , rtrn_type, vm_cls);

      // load blocks
      vector<StackInstr*> instrs;
      for(size_t i = 0; i < blocks.size(); ++i) {
        blocks[i]->Load(vm_method, instrs, is_debug);
      }
      
      return vm_method;
    }

    void Debug() {
      wcout << L"---------------------------------------------------------" << endl;
      wcout << L"Method: id=" << id << L"; name='" << name << L"'; return='" << rtrn_name
        << L"';\n  blocks=" << blocks.size() << L"; is_function=" << is_function << L"; num_params="
        << params << L"; mem_size=" << space << endl;
      wcout << L"---------------------------------------------------------" << endl;
      entries->Debug(has_and_or);
      wcout << L"---------------------------------------------------------" << endl;
      for(size_t i = 0; i < blocks.size(); ++i) {
        blocks[i]->Debug();
      }
    }
  };

  /****************************
  * IntermediateClass class
  ****************************/
  class IntermediateClass : public Intermediate {
    int id;
    wstring name;
    int pid;
    vector<int> interface_ids;
    wstring parent_name;
    vector<wstring> interface_names;
    int cls_space;
    int inst_space;
    vector<IntermediateBlock*> blocks;
    vector<IntermediateMethod*> methods;
    map<int, IntermediateMethod*> method_map;
    IntermediateDeclarations* cls_entries;
    IntermediateDeclarations* inst_entries;
    bool is_lib;
    bool is_interface;
    bool is_virtual;
    bool is_debug;
    wstring file_name;
    
  public:
    IntermediateClass(int i, const wstring &n, int pi, const wstring &p, 
		      vector<int> infs, vector<wstring> in, bool is_inf, 
		      bool is_vrtl, int cs, int is, IntermediateDeclarations* ce, 
		      IntermediateDeclarations* ie, const wstring &fn, bool d) {
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
        cls_entries = ce;
        inst_entries = ie;
        is_lib = false;
        is_debug = d;
        file_name = fn;
    }

    IntermediateClass(LibraryClass* lib_klass) {
      // set attributes
      id = lib_klass->GetId();
      name = lib_klass->GetName();
      pid = -1;
      interface_ids = lib_klass->GetInterfaceIds();
      parent_name = lib_klass->GetParentName();
      interface_names = lib_klass->GetInterfaceNames();
      is_interface = lib_klass->IsInterface();
      is_virtual = lib_klass->IsVirtual();
      is_debug = lib_klass->IsDebug();
      cls_space = lib_klass->GetClassSpace();
      inst_space = lib_klass->GetInstanceSpace();
      cls_entries = lib_klass->GetClassEntries();
      inst_entries = lib_klass->GetInstanceEntries();

      // process methods
      map<const wstring, LibraryMethod*> lib_methods = lib_klass->GetMethods();
      map<const wstring, LibraryMethod*>::iterator mthd_iter;
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
      if(cls_entries) {
        delete cls_entries;
        cls_entries = NULL;
      }

      if(inst_entries) {
        delete inst_entries;
        inst_entries = NULL;
      }
    }

    int GetId() {
      return id;
    }

    const wstring& GetName() {
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

    StackClass* Load(int* vm_cls_hierarchy, int** vm_cls_interfaces) {
      // read id and pid
      const int vm_id = id;
      wstring vm_name = name;
      const int vm_pid = pid;
      wstring vm_parent_name = parent_name;

      // read interface ids
      const int interface_size = interface_ids.size();
      if(interface_size > 0) {
        int* interfaces = new int[interface_size + 1];
        int j = 0;
        while(j < interface_size) {
          interfaces[j] = interface_ids[j];
          j++;
        }
        interfaces[j] = -1;
        vm_cls_interfaces[vm_id] = interfaces;
      }
      else {
        vm_cls_interfaces[vm_id] = NULL;
      }

/*
      if(is_debug) {
        file_name = ReadString();
      }
*/
      
      // read class types
      vector<IntermediateDeclaration*> cls_params = cls_entries->GetParameters();
      StackDclr** cls_dclrs = new StackDclr*[cls_params.size()];
      for(size_t i = 0; i < cls_params.size(); ++i) {
        IntermediateDeclaration* entry = cls_params[i];
        cls_dclrs[i] = new StackDclr;
        cls_dclrs[i]->type = entry->GetType();
        if(is_debug) {
          cls_dclrs[i]->name = entry->GetName();
        }
      }

      // read instance types
      vector<IntermediateDeclaration*> inst_params = inst_entries->GetParameters();
      StackDclr** inst_dclrs = new StackDclr*[inst_params.size()];
      for(size_t i = 0; i < inst_params.size(); ++i) {
        IntermediateDeclaration* entry = inst_params[i];
        inst_dclrs[i] = new StackDclr;
        inst_dclrs[i]->type = entry->GetType();
        if(is_debug) {
          inst_dclrs[i]->name = entry->GetName();
        }
      }

      StackClass* vm_cls = new StackClass(id, name, file_name, pid, is_virtual,
                                          cls_dclrs, cls_params.size(), inst_dclrs,
                                          inst_params.size(), cls_space, inst_space, is_debug);      
      vm_cls_hierarchy[id] = pid;

#ifdef _DEBUG
      wcout << L"Class(" << vm_cls << L"): id=" << id << L"; name='" << name << L"'; parent='"
        << parent_name << L"'; class_bytes=" << cls_space << L"'; instance_bytes="
        << inst_space << endl;
#endif
      
      // load methods
      StackMethod** vm_methods = new StackMethod*[methods.size()];
      for(size_t i = 0; i < methods.size(); ++i) {
        StackMethod * vm_method = methods[i]->Load(vm_cls, is_debug);
        vm_methods[vm_method->GetId()] = vm_method;
      }
      vm_cls->SetMethods(vm_methods, methods.size());

      return vm_cls;
    }

    void Debug() {
      wcout << L"=========================================================" << endl;
      wcout << L"Class: id=" << id << L"; name='" << name << L"'; parent='" << parent_name
            << L"'; pid=" << pid << L";\n interface=" << (is_interface ? L"true" : L"false") 
            << L"; virtual=" << is_virtual << L"; num_methods=" << methods.size() 
            << L"; class_mem_size=" << cls_space << L";\n instance_mem_size=" 
            << inst_space << L"; is_debug=" << (is_debug  ? L"true" : L"false") << endl;      
      wcout << endl << "Interfaces:" << endl;
      for(size_t i = 0; i < interface_names.size(); ++i) {
        wcout << L"\t" << interface_names[i] << endl;
      }      
      wcout << L"=========================================================" << endl;
      cls_entries->Debug(false);
      wcout << L"---------------------------------------------------------" << endl;
      inst_entries->Debug(false);
      wcout << L"=========================================================" << endl;
      for(size_t i = 0; i < blocks.size(); ++i) {
        blocks[i]->Debug();
      }

      for(size_t i = 0; i < methods.size(); ++i) {
        methods[i]->Debug();
      }
    }
  };

  /****************************
  * IntermediateEnumItem class
  ****************************/
  class IntermediateEnumItem : public Intermediate {
    wstring name;
    INT_VALUE id;

  public:
    IntermediateEnumItem(const wstring &n, const INT_VALUE i) {
      name = n;
      id = i;
    }

    IntermediateEnumItem(LibraryEnumItem* i) {
      name = i->GetName();
      id = i->GetId();
    }

    void Write(ofstream &file_out) {
      WriteString(name, file_out);
      WriteInt(id, file_out);
    }

    void Debug() {
      wcout << L"Item: name='" << name << L"'; id='" << id << endl;
    }
  };

  /****************************
  * IntermediateEnum class
  ****************************/
  class IntermediateEnum : public Intermediate {
    wstring name;
    INT_VALUE offset;
    vector<IntermediateEnumItem*> items;

  public:
    IntermediateEnum(const wstring &n, const INT_VALUE o) {
      name = n;
      offset = o;
    }

    IntermediateEnum(LibraryEnum* e) {
      name = e->GetName();
      offset = e->GetOffset();
      // write items
      map<const wstring, LibraryEnumItem*> items = e->GetItems();
      map<const wstring, LibraryEnumItem*>::iterator iter;
      for(iter = items.begin(); iter != items.end(); ++iter) {
        LibraryEnumItem* lib_enum_item = iter->second;
        IntermediateEnumItem* imm_enum_item = new IntermediateEnumItem(lib_enum_item);
        AddItem(imm_enum_item);
      }
    }

    ~IntermediateEnum() {
      while(!items.empty()) {
        IntermediateEnumItem* tmp = items.front();
        items.erase(items.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }
    }

    void AddItem(IntermediateEnumItem* i) {
      items.push_back(i);
    }

    void Write(ofstream &file_out) {
      WriteString(name, file_out);
      WriteInt(offset, file_out);
      // write items
      WriteInt((int)items.size(), file_out);
      for(size_t i = 0; i < items.size(); ++i) {
        items[i]->Write(file_out);
      }
    }

    void Debug() {
      wcout << L"=========================================================" << endl;
      wcout << L"Enum: name='" << name << L"'; items=" << items.size() << endl;
      wcout << L"=========================================================" << endl;

      for(size_t i = 0; i < items.size(); ++i) {
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
    vector<wstring> char_strings;
    vector<frontend::IntStringHolder*> int_strings;
    vector<frontend::FloatStringHolder*> float_strings;
    vector<wstring> bundle_names;
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

    void SetCharStrings(vector<wstring> s) {
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

    void SetBundleNames(vector<wstring> n) {
      bundle_names = n;
    }

    void SetStringClassId(int i) {
      string_cls_id = i;
    }

    void LoadInitializationCode(StackMethod* method)
    {
      /*
      vector<StackInstr*> instrs;

      instrs.push_back(new StackInstr(-1, LOAD_INT_LIT, (long)arguments.size()));
      instrs.push_back(new StackInstr(-1, NEW_INT_ARY, (long)1));
      instrs.push_back(new StackInstr(-1, STOR_LOCL_INT_VAR, 0L, LOCL));

      for(size_t i = 0; i < arguments.size(); ++i) {
        instrs.push_back(new StackInstr(-1, LOAD_INT_LIT, (long)arguments[i].size()));
        instrs.push_back(new StackInstr(-1, NEW_CHAR_ARY, 1L));
        instrs.push_back(new StackInstr(-1, LOAD_INT_LIT, (long)(num_char_strings + i)));
        instrs.push_back(new StackInstr(-1, LOAD_INT_LIT, (long)instructions::CPY_CHAR_STR_ARY));
        instrs.push_back(new StackInstr(-1, TRAP_RTRN, 3L));

        instrs.push_back(new StackInstr(-1, NEW_OBJ_INST, (long)string_cls_id));
        // note: method ID is position dependant
        instrs.push_back(new StackInstr(-1, MTHD_CALL, (long)string_cls_id, 2L, 0L));

        instrs.push_back(new StackInstr(-1, LOAD_INT_LIT, (long)i));
        instrs.push_back(new StackInstr(-1, LOAD_LOCL_INT_VAR, 0L, LOCL));
        instrs.push_back(new StackInstr(-1, STOR_INT_ARY_ELM, 1L, LOCL));
      }

      instrs.push_back(new StackInstr(-1, LOAD_LOCL_INT_VAR, 0L, LOCL));
      instrs.push_back(new StackInstr(-1, LOAD_INST_MEM));
      instrs.push_back(new StackInstr(-1, MTHD_CALL, (long)start_class_id,
        (long)start_method_id, 0L));
      instrs.push_back(new StackInstr(-1, RTRN));

      // copy and set instructions
      StackInstr** mthd_instrs = new StackInstr*[instrs.size()];
      copy(instrs.begin(), instrs.end(), mthd_instrs);
      method->SetInstructions(mthd_instrs, instrs.size());
      */
    }

    void Write(StackProgram* vm_program, bool is_lib, bool is_debug, bool is_web) {
      // write wstring id
      if(!is_lib) {
#ifdef _DEBUG
        assert(string_cls_id > 0);
#endif
        // WriteInt(string_cls_id, file_out);
        vm_program->SetStringObjectId(string_cls_id);
      }

      // write float strings
      FLOAT_VALUE** vm_float_strings = new FLOAT_VALUE*[float_strings.size()];
      for(size_t i = 0; i < float_strings.size(); ++i) {
        frontend::FloatStringHolder* holder = float_strings[i];
        FLOAT_VALUE* vm_float_string = new FLOAT_VALUE[holder->length];

        // copy string    
#ifdef _DEBUG
        wcout << L"Loaded static float string[" << i << L"]: '";
#endif
        for(int j = 0; j < holder->length; j++) {
          vm_float_string[j] = holder->value[j];
#ifdef _DEBUG
          wcout << vm_float_string[j] << L",";
#endif
        }
#ifdef _DEBUG
        wcout << L"'" << endl;
#endif
        vm_float_strings[i] = vm_float_string;
      }
      vm_program->SetFloatStrings(vm_float_strings, float_strings.size());

      // write int strings
      INT_VALUE** vm_int_strings = new INT_VALUE*[int_strings.size()];
      for(size_t i = 0; i < int_strings.size(); ++i) {
        frontend::IntStringHolder* holder = int_strings[i];
        INT_VALUE* vm_int_string = new INT_VALUE[holder->length];

        // copy string    
#ifdef _DEBUG
        wcout << L"Loaded static int string[" << i << L"]: '";
#endif
        for(int j = 0; j < holder->length; j++) {
          vm_int_string[j] = holder->value[j];
#ifdef _DEBUG
          wcout << vm_int_string[j] << L",";
#endif
        }
#ifdef _DEBUG
        wcout << L"'" << endl;
#endif
        vm_int_strings[i] = vm_int_string;
      }
      vm_program->SetIntStrings(vm_int_strings, int_strings.size());
    
      int i;
      wchar_t** vm_char_strings = new wchar_t*[char_strings.size() /*+ arguments.size()*/];
      for(i = 0; i < (int)char_strings.size(); ++i) {
        const wstring value = char_strings[i];
        wchar_t* char_string = new wchar_t[value.size() + 1];
        // copy string
        wcscpy(char_string, value.c_str());
#ifdef _DEBUG
        wcout << L"Loaded static character string[" << i << L"]: '" << char_string << L"'" << endl;
#endif
        char_strings[i] = char_string;
      }

/* TODO: COMMAND LINE
      // copy command line params
      for(size_t j = 0; j < arguments.size(); i++, j++) {
#ifdef _WIN32
        char_strings[i] = _wcsdup((arguments[j]).c_str());
#else
        char_strings[i] = wcsdup((arguments[j]).c_str());
#endif

#ifdef _DEBUG
        wcout << L"Loaded static string: '" << char_strings[i] << L"'" << endl;
#endif
      }
*/
      vm_program->SetCharStrings(vm_char_strings, char_strings.size());
  
      // program classes
      const int cls_number = (int)classes.size();
      int* vm_cls_hierarchy = new int[cls_number];
      int** vm_cls_interfaces = new int*[cls_number];
      StackClass** vm_classes = new StackClass*[cls_number];

#ifdef _DEBUG
      wcout << L"Reading " << cls_number << L" classe(s)..." << endl;
#endif
     
      // load classes
      for(int i = 0; i < cls_number; ++i) {
        if(classes[i]->IsLibrary()) {
          num_lib_classes++;
        } 
        else {
          num_src_classes++;
        }
        vm_classes[i] = classes[i]->Load(vm_cls_hierarchy, vm_cls_interfaces);
      }

      wstring name = L"$Initialization$:";
      StackDclr** dclrs = new StackDclr*[1];
      dclrs[0] = new StackDclr;
      dclrs[0]->name = L"args";
      dclrs[0]->type = OBJ_ARY_PARM;
      StackMethod* init_method = new StackMethod(-1, name, false, false, dclrs, 1, 0, 1, MemoryType::NIL_TYPE, NULL);

      LoadInitializationCode(init_method);
      vm_program->SetInitializationMethod(init_method);
      vm_program->SetStringObjectId(string_cls_id);

      // set class hierarchy and interfaces
      vm_program->SetClasses(vm_classes, cls_number);
      vm_program->SetHierarchy(vm_cls_hierarchy);
      vm_program->SetInterfaces(vm_cls_interfaces);

/*
      wcout << L"Compiled " << num_src_classes
        << (num_src_classes > 1 ? " source classes" : " source class");
      if(is_debug) {
        wcout << " with debug symbols";
      }
      wcout << L'.' << endl;

      wcout << L"Linked " << num_lib_classes
        << (num_lib_classes > 1 ? " library classes." : " library class.")  << endl;
*/
    }

    void Debug() {
      wcout << L"Strings:" << endl;
      for(size_t i = 0; i < char_strings.size(); ++i) {
        wcout << L"wstring id=" << i << L", size='" << ToString(char_strings[i].size()) << L"': '" << char_strings[i] << L"'" << endl;
      }
      wcout << endl;

      wcout << L"Program: enums=" << enums.size() << L", classes="
        << classes.size() << L"; start=" << class_id << L"," << method_id << endl;
      // enums
      for(size_t i = 0; i < enums.size(); ++i) {
        enums[i]->Debug();
      }
      // classes
      for(size_t i = 0; i < classes.size(); ++i) {
        classes[i]->Debug();
      }
    }

    inline wstring ToString(int v) {
      wostringstream str;
      str << v;
      return str.str();
    }
  };

  /****************************
  * RuntimeLoader class
  ****************************/
  class RuntimeLoader {
    IntermediateProgram* compiled_program;
    wstring file_name; // TODO: remove
    StackProgram* vm_program;
    bool is_lib;
    bool is_debug;
    bool is_web;

    bool EndsWith(wstring const &str, wstring const &ending) {
      if(str.length() >= ending.length()) {
        return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
      } 

      return false;
    }

  public:
    RuntimeLoader(IntermediateProgram* p, bool l, bool d, bool w, const wstring &n) {
      compiled_program = p;
      is_lib = l;
      is_debug = d;
      is_web = w;
      file_name = n;

      vm_program = new StackProgram;
    }

    ~RuntimeLoader() {
      delete compiled_program;
      compiled_program = NULL;
    }

    StackProgram* Load();
  };
}

#endif
