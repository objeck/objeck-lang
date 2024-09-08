/***************************************************************************
 * Defines how the intermediate code is written to output files
 *
 * Copyright (c) 2023, Randy Hollines
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

std::wstring backend::ReplaceSubstring(std::wstring s, const std::wstring &f, const std::wstring &r) {
  const size_t index = s.find(f);
  if(index != std::string::npos) {
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
    GetLogger() << L"\n--------- Emitting Target Code ---------" << std::endl;
    program->Debug();
  }
#endif

  // library target
  if(emit_lib) {
    if(!frontend::EndsWith(file_name, L".obl")) {
      std::wcerr << L"Error: Libraries must end in '.obl'" << std::endl;
      exit(1);
    }
  }
  // application target
  else {
    if(!frontend::EndsWith(file_name, L".obe")) {
      std::wcerr << L"Error: Executables must end in '.obe'" << std::endl;
      exit(1);
    }
  }
  
  OutputStream out_stream(file_name);
  program->Write(emit_lib, is_debug, out_stream, false);
  if(out_stream.WriteFile()) {
    std::wcout << L"Wrote target file: '" << file_name << L"'";
    
    if(show_asm) {
      std::wcout << L" with assembly output";
    }

    std::wcout  << L".\n---" << std::endl;
  }
  else {
    std::wcerr << L"Unable to write file: '" << file_name << L"'" << std::endl;
  }
}

#ifdef _MODULE
/****************************
 * Get target binary code
 ****************************/
char* FileEmitter::GetBinary()
{
  OutputStream out_stream;
  program->Write(emit_lib, is_debug, out_stream, true);

  size_t size;
  char* buffer = out_stream.Get(size);

  return buffer;
}
#endif

/****************************
 * IntermediateProgram class
 ****************************/
IntermediateProgram* IntermediateProgram::instance;

void IntermediateProgram::Write(bool emit_lib, bool is_debug, OutputStream& out_stream, bool mute) {
  // version
  WriteInt(VER_NUM, out_stream);

  // magic number
  if(emit_lib) {
    WriteInt(MAGIC_NUM_LIB, out_stream);
  }
  else {
    WriteInt(MAGIC_NUM_EXE, out_stream);
  }

  // write string id
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
  
  // write boolean strings
  WriteInt((int)bool_strings.size(), out_stream);
  for(size_t i = 0; i < bool_strings.size(); ++i) {
    frontend::BoolStringHolder* holder = bool_strings[i];
    WriteInt(holder->length, out_stream);
    for(int j = 0; j < holder->length; ++j) {
      WriteByte(holder->value[j] ? 1 : 0, out_stream);
    }
  }

  // write int strings
  WriteInt((int)int_strings.size(), out_stream);
  for(size_t i = 0; i < int_strings.size(); ++i) {
    frontend::IntStringHolder* holder = int_strings[i];
    WriteInt(holder->length, out_stream);
    for(int j = 0; j < holder->length; ++j) {
      WriteInt64(holder->value[j], out_stream);
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
  
  if(!mute) {
    std::wcout << L"Compiled " << num_src_classes << (num_src_classes > 1 ? L" classes" : L" class");
    if(is_debug) {
      std::wcout << L" with debug symbols";
    }
    std::wcout << L'.' << std::endl;

    if(num_lib_classes > 0) {
      std::wcout << L"Linked " << num_lib_classes << (num_lib_classes > 1 ? L" library classes." : L" library class.") << std::endl;
    }
  }
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
  std::map<IntermediateDeclarations*, std::pair<std::wstring, int> >::iterator iter;
  for(iter = closure_entries.begin(); iter != closure_entries.end(); ++iter) {
    std::pair<std::wstring, int> id = iter->second;
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
  unsigned long num_instrs = 0;
  for(size_t i = 0; i < blocks.size(); ++i) {
    num_instrs += (int)blocks[i]->GetInstructions().size();
  }
  WriteUnsigned(num_instrs, out_stream);

  for(size_t i = 0; i < blocks.size(); ++i) {
    blocks[i]->Write(is_debug, out_stream);
  }
}

void IntermediateMethod::Debug() {
  GetLogger() << L"---------------------------------------------------------" << std::endl;
  GetLogger() << L"Method: id=" << klass->GetId() << L"," << id << L"; name='" << name << L"'; return='" << rtrn_name
    << L"';\n  blocks=" << blocks.size() << L"; is_function=" << (is_function ? L"true" : L"false") << L"; num_params="
    << params << L"; mem_size=" << space << std::endl;
  GetLogger() << L"---------------------------------------------------------" << std::endl;
  entries->Debug(has_and_or);
  GetLogger() << L"---------------------------------------------------------" << std::endl;
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
    WriteInt64(operand7, out_stream);
    break;

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
    WriteChar((wchar_t)operand, out_stream);
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
    GetLogger() << std::left << std::setw(6) << i << L"SWAP_INT" << std::endl;
    break;

  case POP_INT:
    GetLogger() << std::left << std::setw(6) << i << L"POP_INT" << std::endl;
    break;

  case POP_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"POP_FLOAT" << std::endl;
    break;

  case LOAD_INT_LIT:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_INT_LIT: value=" << operand7 << std::endl;
    break;

  case LOAD_CHAR_LIT: {
    const bool is_print = iswprint((wchar_t)operand);
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_CHAR_LIT value='" << (is_print ? (wchar_t)operand : L'?') << L"'" << std::endl;
  }
    break;

  case DYN_MTHD_CALL: {
    const long cls_id = (operand >> (16 * (1))) & 0xFFFF;
    const long mthd_id = (operand >> (16 * (0))) & 0xFFFF;
     
    GetLogger() << std::left << std::setw(6) << i << L"DYN_MTHD_CALL: cls_id=" << cls_id << L", mthd_id=" << mthd_id;
    switch(operand2) {
    case NIL_TYPE:
      GetLogger() << L", rtrn_type=Nil";
      break;

    case BYTE_ARY_TYPE:
      GetLogger() << L", rtrn_type=Byte[]";
      break;

    case CHAR_ARY_TYPE:
      GetLogger() << L", rtrn_type=Char[]";
      break;

    case INT_TYPE:
      GetLogger() << L", rtrn_type=Bool/Char/Int";
      break;

    case FLOAT_TYPE:
      GetLogger() << L", rtrn_type=Float";
      break;

    case FUNC_TYPE:
      GetLogger() << L", rtrn_type=Func";
      break;

    default:
      GetLogger() << L", rtrn_type=Unknown";
      break;
    }

    GetLogger() << std::endl;
  }
    break;

  case SHL_INT:
    GetLogger() << i << L"SHL_INT" << std::endl;
    break;

  case SHR_INT:
    GetLogger() << std::left << std::setw(6) << i << L"SHR_INT" << std::endl;
    break;

  case LOAD_FLOAT_LIT:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_FLOAT_LIT: value=" << operand4 << std::endl;
    break;

  case LOAD_FUNC_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_FUNC_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_INT_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_INT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_FLOAT_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_FLOAT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_BYTE_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_BYTE_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_CHAR_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_CHAR_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_INT_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_INT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_FLOAT_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_FLOAT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case LOAD_CLS_MEM:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_CLS_MEM" << std::endl;
    break;

  case LOAD_INST_MEM:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_INST_MEM" << std::endl;
    break;

  case STOR_FUNC_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_FUNC_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case STOR_INT_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_INT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case STOR_FLOAT_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_FLOAT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case COPY_FUNC_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"COPY_FUNC_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case COPY_INT_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"COPY_INT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case COPY_FLOAT_VAR:
    GetLogger() << std::left << std::setw(6) << i << L"COPY_FLOAT_VAR: id=" << operand << L"; local="
      << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case STOR_BYTE_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_BYTE_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case STOR_CHAR_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_CHAR_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case STOR_INT_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_INT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case STOR_FLOAT_ARY_ELM:
    GetLogger() << std::left << std::setw(6) << i << L"STOR_FLOAT_ARY_ELM: dimension=" << operand
      << L"; local=" << (operand2 == LOCL ? "true" : "false") << std::endl;
    break;

  case instructions::ASYNC_MTHD_CALL:
    GetLogger() << std::left << std::setw(6) << i << L"ASYNC_MTHD_CALL" << std::endl;
    break;

  case instructions::EXT_LIB_LOAD:
    GetLogger() << std::left << std::setw(6) << i << L"DLL_LOAD" << std::endl;
    break;

  case instructions::EXT_LIB_UNLOAD:
    GetLogger() << std::left << std::setw(6) << i << L"DLL_UNLOAD" << std::endl;
    break;

  case instructions::EXT_LIB_FUNC_CALL:
    GetLogger() << std::left << std::setw(6) << i << L"DLL_FUNC_CALL" << std::endl;
    break;

  case instructions::THREAD_JOIN:
    GetLogger() << std::left << std::setw(6) << i << L"THREAD_JOIN" << std::endl;
    break;

  case instructions::THREAD_SLEEP:
    GetLogger() << std::left << std::setw(6) << i << L"THREAD_SLEEP" << std::endl;
    break;

  case instructions::THREAD_MUTEX:
    GetLogger() << std::left << std::setw(6) << i << L"THREAD_MUTEX" << std::endl;
    break;

  case CRITICAL_START:
    GetLogger() << std::left << std::setw(6) << i << L"CRITICAL_START" << std::endl;
    break;

  case CRITICAL_END:
    GetLogger() << std::left << std::setw(6) << i << L"CRITICAL_END" << std::endl;
    break;

  case AND_INT:
    GetLogger() << std::left << std::setw(6) << i << L"AND_INT" << std::endl;
    break;

  case OR_INT:
    GetLogger() << std::left << std::setw(6) << i << L"OR_INT" << std::endl;
    break;

  case ADD_INT:
    GetLogger() << std::left << std::setw(6) << i << L"ADD_INT" << std::endl;
    break;

  case SUB_INT:
    GetLogger() << std::left << std::setw(6) << i << L"SUB_INT" << std::endl;
    break;

  case MUL_INT:
    GetLogger() << std::left << std::setw(6) << i << L"MUL_INT" << std::endl;
    break;

  case DIV_INT:
    GetLogger() << std::left << std::setw(6) << i << L"DIV_INT" << std::endl;
    break;

  case MOD_INT:
    GetLogger() << std::left << std::setw(6) << i << L"MOD_INT" << std::endl;
    break;

  case BIT_AND_INT:
    GetLogger() << std::left << std::setw(6) << i << L"BIT_AND_INT" << std::endl;
    break;

  case BIT_OR_INT:
    GetLogger() << std::left << std::setw(6) << i << L"BIT_OR_INT" << std::endl;
    break;

  case BIT_XOR_INT:
    GetLogger() << std::left << std::setw(6) << i << L"BIT_XOR_INT" << std::endl;
    break;

  case BIT_NOT_INT:
    GetLogger() << i << L"BIT_NOT_INT" << std::endl;
    break;

  case EQL_INT:
    GetLogger() << std::left << std::setw(6) << i << L"EQL_INT" << std::endl;
    break;

  case NEQL_INT:
    GetLogger() << std::left << std::setw(6) << i << L"NEQL_INT" << std::endl;
    break;

  case LES_INT:
    GetLogger() << std::left << std::setw(6) << i << L"LES_INT" << std::endl;
    break;

  case GTR_INT:
    GetLogger() << std::left << std::setw(6) << i << L"GTR_INT" << std::endl;
    break;

  case LES_EQL_INT:
    GetLogger() << std::left << std::setw(6) << i << L"LES_EQL_INT" << std::endl;
    break;

  case GTR_EQL_INT:
    GetLogger() << std::left << std::setw(6) << i << L"GTR_EQL_INT" << std::endl;
    break;

  case ADD_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"ADD_FLOAT" << std::endl;
    break;

  case SUB_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"SUB_FLOAT" << std::endl;
    break;

  case MUL_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"MUL_FLOAT" << std::endl;
    break;

  case DIV_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"DIV_FLOAT" << std::endl;
    break;

  case EQL_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"EQL_FLOAT" << std::endl;
    break;

  case NEQL_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"NEQL_FLOAT" << std::endl;
    break;

  case LES_EQL_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"LES_EQL_FLOAT" << std::endl;
    break;

  case LES_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"LES_FLOAT" << std::endl;
    break;

  case GTR_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"GTR_FLOAT" << std::endl;
    break;

  case GTR_EQL_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"LES_EQL_FLOAT" << std::endl;
    break;

  case instructions::FLOR_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"FLOR_FLOAT" << std::endl;
    break;

  case instructions::LOAD_ARY_SIZE:
    GetLogger() << std::left << std::setw(6) << i << L"LOAD_ARY_SIZE" << std::endl;
    break;

  case instructions::CPY_BYTE_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"CPY_BYTE_ARY" << std::endl;
    break;

  case instructions::CPY_CHAR_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"CPY_CHAR_ARY" << std::endl;
    break;

  case instructions::CPY_INT_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"CPY_INT_ARY" << std::endl;
    break;

  case instructions::CPY_FLOAT_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"CPY_FLOAT_ARY" << std::endl;
    break;

  case instructions::ZERO_BYTE_ARY:
    GetLogger() << i << L"ZERO_BYTE_ARY" << std::endl;
    break;

  case instructions::ZERO_CHAR_ARY:
    GetLogger() << i << L"ZERO_CHAR_ARY" << std::endl;
    break;

  case instructions::ZERO_INT_ARY:
    GetLogger() << i << L"ZERO_INT_ARY" << std::endl;
    break;

  case instructions::ZERO_FLOAT_ARY:
    GetLogger() << i << L"ZERO_FLOAT_ARY" << std::endl;
    break;

  case instructions::CEIL_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"CEIL_FLOAT" << std::endl;
    break;

  case instructions::TRUNC_FLOAT:
    GetLogger() << i << L"TRUC_FLOAT" << std::endl;
    break;

  case instructions::RAND_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"RAND_FLOAT" << std::endl;
    break;

  case instructions::SIN_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"SIN_FLOAT" << std::endl;
    break;

  case instructions::COS_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"COS_FLOAT" << std::endl;
    break;

  case instructions::TAN_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"TAN_FLOAT" << std::endl;
    break;

  case instructions::ASIN_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"ASIN_FLOAT" << std::endl;
    break;

  case instructions::ACOS_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"ACOS_FLOAT" << std::endl;
    break;

  case instructions::ATAN_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"ATAN_FLOAT" << std::endl;
    break;

  case instructions::LOG2_FLOAT:
    GetLogger() << i << L"LOG2_FLOAT" << std::endl;
    break;

  case instructions::CBRT_FLOAT:
    GetLogger() << i << L"CBRT_FLOAT" << std::endl;
    break;

  case instructions::ATAN2_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"ATAN2_FLOAT" << std::endl;
    break;

  case instructions::COSH_FLOAT:
    GetLogger() << i << L"COSH_FLOAT" << std::endl;
    break;

  case instructions::SINH_FLOAT:
    GetLogger() << i << L"SINH_FLOAT" << std::endl;
    break;

  case instructions::TANH_FLOAT:
    GetLogger() << i << L"TANH_FLOAT" << std::endl;
    break;

  case instructions::ACOSH_FLOAT:
    GetLogger() << i << L"ACOSH_FLOAT" << std::endl;
    break;

  case instructions::ASINH_FLOAT:
    GetLogger() << i << L"ASINH_FLOAT" << std::endl;
    break;

  case instructions::ATANH_FLOAT:
    GetLogger() << i << L"ATANH_FLOAT" << std::endl;
    break;

  case instructions::MOD_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"MOD_FLOAT" << std::endl;
    break;

  case instructions::LOG_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"LOG_FLOAT" << std::endl;
    break;

  case instructions::ROUND_FLOAT:
    GetLogger() << i << L"ROUND_FLOAT" << std::endl;
    break;

  case instructions::EXP_FLOAT:
    GetLogger() << i << L"EXP_FLOAT" << std::endl;
    break;

  case instructions::LOG10_FLOAT:
    GetLogger() << i << L"LOG10_FLOAT" << std::endl;
    break;

  case instructions::POW_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"POW_FLOAT" << std::endl;
    break;

  case instructions::SQRT_FLOAT:
    GetLogger() << std::left << std::setw(6) << i << L"SQRT_FLOAT" << std::endl;
    break;

  case instructions::GAMMA_FLOAT:
    GetLogger() << i << L"TGAMMA_FLOAT" << std::endl;
    break;

  case instructions::NAN_INT:
    GetLogger() << i << L"TNAN_INT" << std::endl;
    break;

  case instructions::INF_INT:
    GetLogger() << i << L"TINF_INT" << std::endl;
    break;

  case instructions::NEG_INF_INT:
    GetLogger() << i << L"TNEG_INF_INT" << std::endl;
    break;

  case instructions::NAN_FLOAT:
    GetLogger() << i << L"TNAN_FLOAT" << std::endl;
    break;

  case instructions::INF_FLOAT:
    GetLogger() << i << L"TINF_FLOAT" << std::endl;
    break;

  case instructions::NEG_INF_FLOAT:
    GetLogger() << i << L"NEG_INF_FLOAT" << std::endl;
    break;

  case I2F:
    GetLogger() << std::left << std::setw(6) << i << L"I2F" << std::endl;
    break;

  case F2I:
    GetLogger() << std::left << std::setw(6) << i << L"F2I" << std::endl;
    break;

  case instructions::S2F:
    GetLogger() << std::left << std::setw(6) << i << L"S2F" << std::endl;
    break;

  case instructions::S2I:
    GetLogger() << std::left << std::setw(6) << i << L"S2I" << std::endl;
    break;

  case instructions::I2S:
    GetLogger() << std::left << std::setw(6) << i << L"I2S" << std::endl;
    break;

  case instructions::F2S:
    GetLogger() << std::left << std::setw(6) << i << L"F2S" << std::endl;
    break;

  case RTRN:
    GetLogger() << std::left << std::setw(6) << i << L"RTRN" << std::endl;
    break;

  case MTHD_CALL: {
    IntermediateMethod* method = IntermediateProgram::Instance()->GetClass(operand)->GetMethod(operand2);
    GetLogger() << std::left << std::setw(6) << i << L"MTHD_CALL: method='" << method->GetName() << L"'; native=" << (operand3 ? "true" : "false") << std::endl;
  }
    break;

  case LIB_NEW_OBJ_INST:
    GetLogger() << std::left << std::setw(6) << i << L"LIB_NEW_OBJ_INST: class='" << operand5 << L"'" << std::endl;
    break;

  case LIB_OBJ_TYPE_OF:
    GetLogger() << std::left << std::setw(6) << i << L"LIB_OBJ_TYPE_OF: class='" << operand5 << L"'" << std::endl;
    break;

  case LIB_OBJ_INST_CAST:
    GetLogger() << std::left << std::setw(6) << i << L"LIB_OBJ_INST_CAST: to_class='" << operand5 << L"'" << std::endl;
    break;

  case LIB_MTHD_CALL:
    GetLogger() << std::left << std::setw(6) << i << L"LIB_MTHD_CALL: method='" << operand6 << L"'; native=" << (operand3 ? "true" : "false") << std::endl;
    break;

  case LIB_FUNC_DEF:
    GetLogger() << std::left << std::setw(6) << i << L"LIB_FUNC_DEF: class='" << operand5 << L"'; method='"
      << operand6 << L"'" << std::endl;
    break;

  case LBL:
    GetLogger() << std::left << std::setw(6) << i << L"LBL" << std::endl;
    break;

  case JMP:
    if(operand2 == -1) {
      GetLogger() << std::left << std::setw(6) << i << L"JMP: index=" << operand << std::endl;
    }
    else {
      GetLogger() << std::left << std::setw(6) << i << L"JMP: index=" << operand << L", conditional=" << (operand2 ? "true" : "false") << std::endl;
    }
    break;

  case OBJ_INST_CAST: {
    IntermediateClass* klass = IntermediateProgram::Instance()->GetClass(operand);
    GetLogger() << std::left << std::setw(6) << i << L"OBJ_INST_CAST: to='" << klass->GetName() << L"', id=" << operand << std::endl;
  }
    break;
    
  case OBJ_TYPE_OF: {
    IntermediateClass* klass = IntermediateProgram::Instance()->GetClass(operand);
    GetLogger() << std::left << std::setw(6) << i << L"OBJ_TYPE_OF: check='" << klass->GetName() << L"', id=" << operand << std::endl;
  }
    break;

  case NEW_FLOAT_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"NEW_FLOAT_ARY: dimension=" << operand << std::endl;
    break;

  case NEW_INT_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"NEW_INT_ARY: dimension=" << operand << std::endl;
    break;

  case NEW_BYTE_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"NEW_BYTE_ARY: dimension=" << operand << std::endl;
    break;

  case NEW_CHAR_ARY:
    GetLogger() << std::left << std::setw(6) << i << L"NEW_CHAR_ARY: dimension=" << operand << std::endl;
    break;

  case NEW_OBJ_INST: {
    IntermediateClass* klass = IntermediateProgram::Instance()->GetClass(operand);
    GetLogger() << std::left << std::setw(6) << i << L"NEW_OBJ_INST: class='" << klass->GetName() << L"'" << std::endl;
  }
    break;

  case NEW_FUNC_INST:
    GetLogger() << std::left << std::setw(6) << i << L"NEW_FUNC_INST: mem_size=" << operand << std::endl;
    break;

  case TRAP:
    GetLogger() << std::left << std::setw(6) << i << L"TRAP: args=" << operand << std::endl;
    break;

  case TRAP_RTRN:
    GetLogger() << std::left << std::setw(6) << i << L"TRAP_RTRN: args=" << operand << std::endl;
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
  WriteInt64(id, out_stream);
}

/****************************
 * Enum class
 ****************************/
void IntermediateEnum::Write(OutputStream& out_stream) {
  WriteString(name, out_stream);
  WriteInt64(offset, out_stream);
  // write items
  WriteInt((int)items.size(), out_stream);
  for(size_t i = 0; i < items.size(); ++i) {
    items[i]->Write(out_stream);
  }
}
