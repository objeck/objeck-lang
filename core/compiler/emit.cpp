/***************************************************************************
 * Defines how the intermediate code is written to output files
 *
 * Copyright (c) 2008-2022, Randy Hollines
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

#include "emit.h"

using namespace backend;

wstring backend::ReplaceSubstring(wstring s, const wstring &f, const wstring &r) {
  const size_t index = s.find(f);
  if(index != string::npos) {
    s.replace(index, f.size(), r);
  }
  
  return s;
}

/****************************
 * IntermediateFactory class
 ****************************/
IntermediateFactory* IntermediateFactory::instance;

IntermediateFactory* IntermediateFactory::Instance()
{
  if(!instance) {
    instance = new IntermediateFactory;
  }

  return instance;
}

/****************************
 * Writes target code to an output file.
 ****************************/
void FileEmitter::Emit()
{
#ifdef _DEBUG
  show_asm = true;
#endif
 
#ifndef _SYSTEM 
  if(show_asm) {
    GetLogger() << L"\n--------- Emitting Target Code ---------" << endl;
    program->Debug();
  }
#endif

  // library target
  if(emit_lib) {
    if(!frontend::EndsWith(file_name, L".obl")) {
      wcerr << L"Error: Libraries must end in '.obl'" << endl;
      exit(1);
    }
  } 
  // web target
  else if(is_web) {
    if(!frontend::EndsWith(file_name, L".obw")) {
      wcerr << L"Error: Web applications must end in '.obw'" << endl;
      exit(1);
    }
  } 
  // application target
  else {
    if(!frontend::EndsWith(file_name, L".obe")) {
      wcerr << L"Error: Executables must end in '.obe'" << endl;
      exit(1);
    }
  }
  
  OutputStream out_stream(file_name);
  program->Write(emit_lib, is_debug, is_web, out_stream);
  if(out_stream.WriteFile()) {
    wcout << L"Wrote target file: '" << file_name << L"'\n---" << endl;
  }
  else {
    wcerr << L"Unable to write file: '" << file_name << L"'" << endl;
  }
}

/****************************
 * IntermediateProgram class
 ****************************/
IntermediateProgram* IntermediateProgram::instance;

void IntermediateProgram::Write(bool emit_lib, bool is_debug, bool is_web, OutputStream& out_stream) {
  // version
  WriteInt(VER_NUM, out_stream);

  // magic number
  if(emit_lib) {
    WriteInt(MAGIC_NUM_LIB, out_stream);
  }
  else if(is_web) {
    WriteInt(MAGIC_NUM_WEB, out_stream);
  }
  else {
    WriteInt(MAGIC_NUM_EXE, out_stream);
  }

  // write wstring id
  if(!emit_lib) {
#ifdef _DEBUG
    assert(string_cls_id > 0);
#endif
    WriteInt(string_cls_id, out_stream);
  }

  // write float strings
  WriteInt((int)float_strings.size(), out_stream);
  for(size_t i = 0; i < float_strings.size(); ++i) {
    frontend::FloatStringHolder* holder = float_strings[i];
    WriteInt(holder->length, out_stream);
    for(int j = 0; j < holder->length; ++j) {
      WriteDouble(holder->value[j], out_stream);
    }
  }
  
  // write int strings
  WriteInt((int)int_strings.size(), out_stream);
  for(size_t i = 0; i < int_strings.size(); ++i) {
    frontend::IntStringHolder* holder = int_strings[i];
    WriteInt(holder->length, out_stream);
    for(int j = 0; j < holder->length; ++j) {
      WriteInt(holder->value[j], out_stream);
    }
  }
  
  // write char strings
  WriteInt((int)char_strings.size(), out_stream);
  for(size_t i = 0; i < char_strings.size(); ++i) {
    WriteString(char_strings[i], out_stream);
  }

  if(emit_lib) {
    // write bundle names
    WriteInt((int)bundle_names.size(), out_stream);
    for(size_t i = 0; i < bundle_names.size(); ++i) {
      WriteString(bundle_names[i], out_stream);
    }

    // write aliases
    WriteInt((int)alias_encodings.size(), out_stream);
    for(size_t i = 0; i < alias_encodings.size(); ++i) {
      WriteString(alias_encodings[i], out_stream);
    }
  }

  // program start
  if(!emit_lib) {
    WriteInt(class_id, out_stream);
    WriteInt(method_id, out_stream);
  }
  
  // program enums
  if(emit_lib) {
    WriteInt((int)enums.size(), out_stream);
    for(size_t i = 0; i < enums.size(); ++i) {
      enums[i]->Write(out_stream);
    }
  }
  
  // program classes
  WriteInt((int)classes.size(), out_stream);
  for(size_t i = 0; i < classes.size(); ++i) {
    if(classes[i]->IsLibrary()) {
      num_lib_classes++;
    }
    else {
      num_src_classes++;
    }
    classes[i]->Write(emit_lib, out_stream);
  }
  
  wcout << L"Compiled " << num_src_classes << (num_src_classes > 1 ? L" source classes" : L" source class");
  if(is_debug) {
    wcout << L" with debug symbols";
  }
  wcout << L'.' << endl;
  
  wcout << L"Linked " << num_lib_classes << (num_lib_classes > 1 ? L" library classes." : L" library class.") << endl;
}

/****************************
 * Class class
 ****************************/
void IntermediateClass::Write(bool emit_lib, OutputStream& out_stream) {
  // write id and name
  WriteInt(id, out_stream);
  WriteString(name, out_stream);
  WriteInt(pid, out_stream);
  WriteString(parent_name, out_stream);

  // interface ids
  WriteInt((int)interface_ids.size(), out_stream);
  for(size_t i = 0; i < interface_ids.size(); ++i) {
    WriteInt(interface_ids[i], out_stream);
  }
  
  if(emit_lib) {
    // interface names
    WriteInt((int)interface_names.size(), out_stream);
    for(size_t i = 0; i < interface_names.size(); ++i) {
      WriteString(interface_names[i], out_stream);
    }
    WriteByte(is_interface, out_stream);
    WriteByte(is_public, out_stream);

    // generic names
    WriteInt((int)generic_classes.size(), out_stream);
    for(size_t i = 0; i < generic_classes.size(); ++i) {
      WriteString(generic_classes[i], out_stream);
    }
  }
  
  WriteByte(is_virtual, out_stream);
  WriteByte(is_debug, out_stream);
  if(is_debug) {
    WriteString(file_name, out_stream);
  }

  // write local space size
  WriteInt(cls_space, out_stream);
  WriteInt(inst_space, out_stream);

  // write class and instance declarations
  cls_entries->Write(is_debug, out_stream);
  inst_entries->Write(is_debug, out_stream);

  // write closure declarations
  WriteInt((int)closure_entries.size(), out_stream);
  map<IntermediateDeclarations*, pair<wstring, int> >::iterator iter;
  for(iter = closure_entries.begin(); iter != closure_entries.end(); ++iter) {
    pair<wstring, int> id = iter->second;
    IntermediateDeclarations* closure_dclrs = iter->first;
    if(emit_lib) {
      WriteString(id.first, out_stream);
    }
    else {
      WriteInt(id.second, out_stream);
    }
    closure_dclrs->Write(is_debug, out_stream);
  }

  // write methods
  WriteInt((int)methods.size(), out_stream);
  for(size_t i = 0; i < methods.size(); ++i) {
    methods[i]->Write(emit_lib, is_debug, out_stream);
  }
}

/****************************
 * Method class
 **************************/
void IntermediateMethod::Write(bool emit_lib, bool is_debug, OutputStream& out_stream) {
  // write attributes
  WriteInt(id, out_stream);
  
  if(emit_lib) {
    WriteInt(type, out_stream);
  }
  
  WriteByte(is_virtual, out_stream);
  WriteByte(has_and_or, out_stream);
  WriteByte(is_lambda, out_stream);
  
  if(emit_lib) {
    WriteByte(is_native, out_stream);
    WriteByte(is_function, out_stream);
  }
  
  WriteString(name, out_stream);
  WriteString(rtrn_name, out_stream);

  // write local space size
  WriteInt(params, out_stream);
  WriteInt(space, out_stream);
  entries->Write(is_debug, out_stream);

  // write statements
  uint32_t num_instrs = 0;
  for(size_t i = 0; i < blocks.size(); ++i) {
    num_instrs += (int)blocks[i]->GetInstructions().size();
  }
  WriteUnsigned(num_instrs, out_stream);

  for(size_t i = 0; i < blocks.size(); ++i) {
    blocks[i]->Write(is_debug, out_stream);
  }
}

void IntermediateMethod::Debug() {
  GetLogger() << L"---------------------------------------------------------" << endl;
  GetLogger() << L"Method: id=" << klass->GetId() << L"," << id << L"; name='" << name << L"'; return='" << rtrn_name
    << L"';\n  blocks=" << blocks.size() << L"; is_function=" << (is_function ? L"true" : L"false") << L"; num_params="
    << params << L"; mem_size=" << space << endl;
  GetLogger() << L"---------------------------------------------------------" << endl;
  entries->Debug(has_and_or);
  GetLogger() << L"---------------------------------------------------------" << endl;
  for (size_t i = 0; i < blocks.size(); ++i) {
    blocks[i]->Debug();
  }
}

/****************************
* IntermediateBlock class
****************************/
void IntermediateBlock::Write(bool is_debug, OutputStream& out_stream) {
  for(size_t i = 0; i < instructions.size(); ++i) {
    instructions[i]->Write(is_debug, out_stream);
  }
}

/****************************
 * Instruction class
 ****************************/
void IntermediateInstruction::Write(bool is_debug, OutputStream& out_stream) {
  WriteByte(type, out_stream);
  if(is_debug) {
    WriteInt(line_num, out_stream);
  }

  switch(type) {
  case LOAD_INT_LIT:
  case NEW_FLOAT_ARY:
  case NEW_INT_ARY:
  case NEW_BYTE_ARY:
  case NEW_CHAR_ARY:
  case NEW_OBJ_INST:
  case NEW_FUNC_INST:
  case OBJ_INST_CAST:
  case OBJ_TYPE_OF:
  case TRAP:
  case TRAP_RTRN:
    WriteInt(operand, out_stream);
    break;

  case LOAD_CHAR_LIT:
    WriteChar(operand, out_stream);
    break;

  case instructions::ASYNC_MTHD_CALL:
  case MTHD_CALL:
    WriteInt(operand, out_stream);
    WriteInt(operand2, out_stream);
    WriteInt(operand3, out_stream);
    break;

  case LIB_NEW_OBJ_INST:
  case LIB_OBJ_INST_CAST:
  case LIB_OBJ_TYPE_OF:
    WriteString(operand5, out_stream);
    break;

  case LIB_MTHD_CALL:
    WriteInt(operand3, out_stream);
    WriteString(operand5, out_stream);
    WriteString(operand6, out_stream);
    break;

  case LIB_FUNC_DEF:
    WriteString(operand5, out_stream);
    WriteString(operand6, out_stream);
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
    WriteInt(operand, out_stream);
    WriteInt(operand2, out_stream);
    break;

  case LOAD_FLOAT_LIT:
    WriteDouble(operand4, out_stream);
    break;

  case LBL:
    WriteInt(operand, out_stream);
    break;

  default:
    break;
  }
}

void IntermediateInstruction::Debug(size_t i) {
  switch(type) {
  case SWAP_INT:
    GetLogger()  << i << L":\tSWAP_INT" << endl;
    break;

  case POP_INT:
    GetLogger()  << i << L":\tPOP_INT" << endl;
    break;

  case POP_FLOAT:
    GetLogger()  << i << L":\tPOP_FLOAT" << endl;
    break;

  case LOAD_INT_LIT:
    GetLogger()  << i << L":\tLOAD_INT_LIT: value=" << operand << endl;
    break;

  case LOAD_CHAR_LIT: {
    const bool is_print = iswprint((wchar_t)operand);
    GetLogger()  << i << L":\tLOAD_CHAR_LIT value='" << (is_print ? (wchar_t)operand : L'?') << L"'" << endl;
  }
    break;

  case DYN_MTHD_CALL: {
    GetLogger()  << i << L":\tDYN_MTHD_CALL num_params=" << operand;

    switch(operand2) {
    case NIL_TYPE:
      GetLogger()  << i << L":\t; rtrn_type=Nil";
      break;

    case BYTE_ARY_TYPE:
      GetLogger()  << i << L":\t; rtrn_type=Byte[]";
      break;

    case CHAR_ARY_TYPE:
      GetLogger()  << i << L":\t; rtrn_type=Char[]";
      break;

    case INT_TYPE:
      GetLogger()  << i << L":\t; rtrn_type=Int";
      break;

    case FLOAT_TYPE:
      GetLogger()  << i << L":\t; rtrn_type=Float";
      break;

    case FUNC_TYPE:
      GetLogger()  << i << L":\t; rtrn_type=Func";
      break;

    default:
      GetLogger()  << i << L":\t; rtrn_type=Unknown";
      break;
    }

    GetLogger() << endl;
  }
    break;

  case SHL_INT:
    GetLogger()  << i << L":\tSHL_INT" << endl;
    break;

  case SHR_INT:
    GetLogger()  << i << L":\tSHR_INT" << endl;
    break;

  case LOAD_FLOAT_LIT:
    GetLogger()  << i << L":\tLOAD_FLOAT_LIT: value=" << operand4 << endl;
    break;

  case LOAD_FUNC_VAR:
    GetLogger()  << i << L":\tLOAD_FUNC_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_INT_VAR:
    GetLogger()  << i << L":\tLOAD_INT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_FLOAT_VAR:
    GetLogger()  << i << L":\tLOAD_FLOAT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_BYTE_ARY_ELM:
    GetLogger()  << i << L":\tLOAD_BYTE_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_CHAR_ARY_ELM:
    GetLogger()  << i << L":\tLOAD_CHAR_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_INT_ARY_ELM:
    GetLogger()  << i << L":\tLOAD_INT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_FLOAT_ARY_ELM:
    GetLogger()  << i << L":\tLOAD_FLOAT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case LOAD_CLS_MEM:
    GetLogger()  << i << L":\tLOAD_CLS_MEM" << endl;
    break;

  case LOAD_INST_MEM:
    GetLogger()  << i << L":\tLOAD_INST_MEM" << endl;
    break;

  case STOR_FUNC_VAR:
    GetLogger()  << i << L":\tSTOR_FUNC_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case STOR_INT_VAR:
    GetLogger()  << i << L":\tSTOR_INT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case STOR_FLOAT_VAR:
    GetLogger()  << i << L":\tSTOR_FLOAT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case COPY_FUNC_VAR:
    GetLogger()  << i << L":\tCOPY_FUNC_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case COPY_INT_VAR:
    GetLogger()  << i << L":\tCOPY_INT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case COPY_FLOAT_VAR:
    GetLogger()  << i << L":\tCOPY_FLOAT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case STOR_BYTE_ARY_ELM:
    GetLogger()  << i << L":\tSTOR_BYTE_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case STOR_CHAR_ARY_ELM:
    GetLogger()  << i << L":\tSTOR_CHAR_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case STOR_INT_ARY_ELM:
    GetLogger()  << i << L":\tSTOR_INT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case STOR_FLOAT_ARY_ELM:
    GetLogger()  << i << L":\tSTOR_FLOAT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << endl;
    break;

  case instructions::ASYNC_MTHD_CALL: {
    IntermediateMethod* async_method = IntermediateProgram::Instance()->GetClass(operand)->GetMethod(operand2);
    GetLogger()  << i << L":\tASYNC_MTHD_CALL: method='" << async_method->GetName() << L"'; native=" << (operand3 ? "true" : "false") << endl;
  }
    break;

  case instructions::DLL_LOAD:
    GetLogger()  << i << L":\tDLL_LOAD" << endl;
    break;

  case instructions::DLL_UNLOAD:
    GetLogger()  << i << L":\tDLL_UNLOAD" << endl;
    break;

  case instructions::DLL_FUNC_CALL:
    GetLogger()  << i << L":\tDLL_FUNC_CALL" << endl;
    break;

  case instructions::THREAD_JOIN:
    GetLogger()  << i << L":\tTHREAD_JOIN" << endl;
    break;

  case instructions::THREAD_SLEEP:
    GetLogger()  << i << L":\tTHREAD_SLEEP" << endl;
    break;

  case instructions::THREAD_MUTEX:
    GetLogger()  << i << L":\tTHREAD_MUTEX" << endl;
    break;

  case CRITICAL_START:
    GetLogger()  << i << L":\tCRITICAL_START" << endl;
    break;

  case CRITICAL_END:
    GetLogger()  << i << L":\tCRITICAL_END" << endl;
    break;

  case AND_INT:
    GetLogger()  << i << L":\tAND_INT" << endl;
    break;

  case OR_INT:
    GetLogger()  << i << L":\tOR_INT" << endl;
    break;

  case ADD_INT:
    GetLogger()  << i << L":\tADD_INT" << endl;
    break;

  case SUB_INT:
    GetLogger()  << i << L":\tSUB_INT" << endl;
    break;

  case MUL_INT:
    GetLogger()  << i << L":\tMUL_INT" << endl;
    break;

  case DIV_INT:
    GetLogger()  << i << L":\tDIV_INT" << endl;
    break;

  case MOD_INT:
    GetLogger()  << i << L":\tMOD_INT" << endl;
    break;

  case BIT_AND_INT:
    GetLogger()  << i << L":\tBIT_AND_INT" << endl;
    break;

  case BIT_OR_INT:
    GetLogger()  << i << L":\tBIT_OR_INT" << endl;
    break;

  case BIT_XOR_INT:
    GetLogger()  << i << L":\tBIT_XOR_INT" << endl;
    break;

  case EQL_INT:
    GetLogger()  << i << L":\tEQL_INT" << endl;
    break;

  case NEQL_INT:
    GetLogger()  << i << L":\tNEQL_INT" << endl;
    break;

  case LES_INT:
    GetLogger()  << i << L":\tLES_INT" << endl;
    break;

  case GTR_INT:
    GetLogger()  << i << L":\tGTR_INT" << endl;
    break;

  case LES_EQL_INT:
    GetLogger()  << i << L":\tLES_EQL_INT" << endl;
    break;

  case GTR_EQL_INT:
    GetLogger()  << i << L":\tGTR_EQL_INT" << endl;
    break;

  case ADD_FLOAT:
    GetLogger()  << i << L":\tADD_FLOAT" << endl;
    break;

  case SUB_FLOAT:
    GetLogger()  << i << L":\tSUB_FLOAT" << endl;
    break;

  case MUL_FLOAT:
    GetLogger()  << i << L":\tMUL_FLOAT" << endl;
    break;

  case DIV_FLOAT:
    GetLogger()  << i << L":\tDIV_FLOAT" << endl;
    break;

  case EQL_FLOAT:
    GetLogger()  << i << L":\tEQL_FLOAT" << endl;
    break;

  case NEQL_FLOAT:
    GetLogger()  << i << L":\tNEQL_FLOAT" << endl;
    break;

  case LES_EQL_FLOAT:
    GetLogger()  << i << L":\tLES_EQL_FLOAT" << endl;
    break;

  case LES_FLOAT:
    GetLogger()  << i << L":\tLES_FLOAT" << endl;
    break;

  case GTR_FLOAT:
    GetLogger()  << i << L":\tGTR_FLOAT" << endl;
    break;

  case GTR_EQL_FLOAT:
    GetLogger()  << i << L":\tLES_EQL_FLOAT" << endl;
    break;

  case instructions::FLOR_FLOAT:
    GetLogger()  << i << L":\tFLOR_FLOAT" << endl;
    break;

  case instructions::LOAD_ARY_SIZE:
    GetLogger()  << i << L":\tLOAD_ARY_SIZE" << endl;
    break;

  case instructions::CPY_BYTE_ARY:
    GetLogger()  << i << L":\tCPY_BYTE_ARY" << endl;
    break;

  case instructions::CPY_CHAR_ARY:
    GetLogger()  << i << L":\tCPY_CHAR_ARY" << endl;
    break;

  case instructions::CPY_INT_ARY:
    GetLogger()  << i << L":\tCPY_INT_ARY" << endl;
    break;

  case instructions::CPY_FLOAT_ARY:
    GetLogger()  << i << L":\tCPY_FLOAT_ARY" << endl;
    break;

  case instructions::CEIL_FLOAT:
    GetLogger()  << i << L":\tCEIL_FLOAT" << endl;
    break;

  case instructions::RAND_FLOAT:
    GetLogger()  << i << L":\tRAND_FLOAT" << endl;
    break;

  case instructions::SIN_FLOAT:
    GetLogger()  << i << L":\tSIN_FLOAT" << endl;
    break;

  case instructions::COS_FLOAT:
    GetLogger()  << i << L":\tCOS_FLOAT" << endl;
    break;

  case instructions::TAN_FLOAT:
    GetLogger()  << i << L":\tTAN_FLOAT" << endl;
    break;

  case instructions::ASIN_FLOAT:
    GetLogger()  << i << L":\tASIN_FLOAT" << endl;
    break;

  case instructions::ACOS_FLOAT:
    GetLogger()  << i << L":\tACOS_FLOAT" << endl;
    break;

  case instructions::ATAN_FLOAT:
    GetLogger()  << i << L":\tATAN_FLOAT" << endl;
    break;

  case instructions::ATAN2_FLOAT:
    GetLogger()  << i << L":\tATAN2_FLOAT" << endl;
    break;

  case instructions::MOD_FLOAT:
    GetLogger()  << i << L":\tMOD_FLOAT" << endl;
    break;

  case instructions::LOG_FLOAT:
    GetLogger()  << i << L":\tLOG_FLOAT" << endl;
    break;

  case instructions::POW_FLOAT:
    GetLogger()  << i << L":\tPOW_FLOAT" << endl;
    break;

  case instructions::SQRT_FLOAT:
    GetLogger()  << i << L":\tSQRT_FLOAT" << endl;
    break;

  case I2F:
    GetLogger()  << i << L":\tI2F" << endl;
    break;

  case F2I:
    GetLogger()  << i << L":\tF2I" << endl;
    break;

  case instructions::S2F:
    GetLogger()  << i << L":\tS2F" << endl;
    break;

  case instructions::S2I:
    GetLogger()  << i << L":\tS2I" << endl;
    break;

  case instructions::I2S:
    GetLogger()  << i << L":\tI2S" << endl;
    break;

  case instructions::F2S:
    GetLogger()  << i << L":\tF2S" << endl;
    break;

  case RTRN:
    GetLogger()  << i << L":\tRTRN" << endl;
    break;

  case MTHD_CALL: {
    IntermediateMethod* method = IntermediateProgram::Instance()->GetClass(operand)->GetMethod(operand2);
    GetLogger()  << i << L":\tMTHD_CALL: method='" << method->GetName() << L"'; native=" << (operand3 ? "true" : "false") << endl;
  }
    break;

  case LIB_NEW_OBJ_INST:
    GetLogger()  << i << L":\tLIB_NEW_OBJ_INST: class='" << operand5 << L"'" << endl;
    break;

  case LIB_OBJ_TYPE_OF:
    GetLogger()  << i << L":\tLIB_OBJ_TYPE_OF: class='" << operand5 << L"'" << endl;
    break;

  case LIB_OBJ_INST_CAST:
    GetLogger()  << i << L":\tLIB_OBJ_INST_CAST: to_class='" << operand5 << L"'" << endl;
    break;

  case LIB_MTHD_CALL:
    GetLogger()  << i << L":\tLIB_MTHD_CALL: method='" << operand6 << L"'; native=" << (operand3 ? "true" : "false") << endl;
    break;

  case LIB_FUNC_DEF:
    GetLogger()  << i << L":\tLIB_FUNC_DEF: class='" << operand5 << L"'; method='"
      << operand6 << L"'" << endl;
    break;

  case LBL:
    GetLogger()  << i << L":\tLBL" << endl;
    break;

  case JMP:
    if(operand2 == -1) {
      GetLogger()  << i << L":\tJMP: index=" << operand << endl;
    }
    else {
      GetLogger()  << i << L":\tJMP: index=" << operand << L", conditional=" << (operand2 ? "true" : "false") << endl;
    }
    break;

  case OBJ_INST_CAST: {
    IntermediateClass* klass = IntermediateProgram::Instance()->GetClass(operand);
    GetLogger()  << i << L":\tOBJ_INST_CAST: to='" << klass->GetName() << L"', id=" << operand << endl;
  }
    break;

  case OBJ_TYPE_OF: {
    IntermediateClass* klass = IntermediateProgram::Instance()->GetClass(operand);
    GetLogger()  << i << L":\tOBJ_TYPE_OF: check='" << klass->GetName() << L"', id=" << operand << endl;
  }
    break;

  case NEW_FLOAT_ARY:
    GetLogger()  << i << L":\tNEW_FLOAT_ARY: dimension=" << operand << endl;
    break;

  case NEW_INT_ARY:
    GetLogger()  << i << L":\tNEW_INT_ARY: dimension=" << operand << endl;
    break;

  case NEW_BYTE_ARY:
    GetLogger()  << i << L":\tNEW_BYTE_ARY: dimension=" << operand << endl;
    break;

  case NEW_CHAR_ARY:
    GetLogger()  << i << L":\tNEW_CHAR_ARY: dimension=" << operand << endl;
    break;

  case NEW_OBJ_INST: {
    IntermediateClass* klass = IntermediateProgram::Instance()->GetClass(operand);
    GetLogger()  << i << L":\tNEW_OBJ_INST: class='" << klass->GetName() << L"'" << endl;
  }
    break;

  case NEW_FUNC_INST:
    GetLogger()  << i << L":\tNEW_FUNC_INST: mem_size=" << operand << endl;
    break;

  case TRAP:
    GetLogger()  << i << L":\tTRAP: args=" << operand << endl;
    break;

  case TRAP_RTRN:
    GetLogger()  << i << L":\tTRAP_RTRN: args=" << operand << endl;
    break;

  default:
    break;
  }
}

/****************************
 * IntermediateEnumItem class
 ****************************/
void IntermediateEnumItem::Write(OutputStream& out_stream) {
  WriteString(name, out_stream);
  WriteInt(id, out_stream);
}

/****************************
 * Enum class
 ****************************/
void IntermediateEnum::Write(OutputStream& out_stream) {
  WriteString(name, out_stream);
  WriteInt(offset, out_stream);
  // write items
  WriteInt((int)items.size(), out_stream);
  for(size_t i = 0; i < items.size(); ++i) {
    items[i]->Write(out_stream);
  }
}
