/***************************************************************************
 * Defines how the intermediate code is written to output files
 *
 * Copyright (c) 2008-2019, Randy Hollines
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

wstring backend::ReplaceSubstring(wstring s, const wstring& f, const wstring &r) {
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
  GetLogger() << L"\n--------- Emitting Target Code ---------" << endl;
  program->Debug();
#endif

  // library target
  if(emit_lib) {
    if(!EndsWith(file_name, L".obl")) {
      wcerr << L"Error: Libraries must end in '.obl'" << endl;
      exit(1);
    }
  } 
  // web target
  else if(is_web) {
    if(!EndsWith(file_name, L".obw")) {
      wcerr << L"Error: Web applications must end in '.obw'" << endl;
      exit(1);
    }
  } 
  // application target
  else {
    if(!EndsWith(file_name, L".obe")) {
      wcerr << L"Error: Executables must end in '.obe'" << endl;
      exit(1);
    }
  }
  
  OutputStream out_stream(file_name);
  program->Write(emit_lib, is_debug, is_web, out_stream);
  if(out_stream.WriteFile()) {
    wcout << L"Wrote target file: '" << file_name << L"'" << endl;
  }
  else {
    wcerr << L"Unable to write file: '" << file_name << L"'" << endl;
  }
}

/****************************
 * IntermediateProgram class
 ****************************/
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

  // write bundle names
  if(emit_lib) {
    WriteInt((int)bundle_names.size(), out_stream);
    for(size_t i = 0; i < bundle_names.size(); ++i) {
      WriteString(bundle_names[i], out_stream);
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
  
  wcout << L"Compiled " << num_src_classes
        << (num_src_classes > 1 ? L" source classes" : L" source class");
  if(is_debug) {
    wcout << L" with debug symbols";
  }
  wcout << L'.' << endl;
  
  wcout << L"Linked " << num_lib_classes
        << (num_lib_classes > 1 ? L" library classes." : L" library class.") << endl;
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

  // interface names
  if(emit_lib) {
    WriteInt((int)interface_names.size(), out_stream);
    for(size_t i = 0; i < interface_names.size(); ++i) {
      WriteString(interface_names[i], out_stream);
    }
    WriteInt(is_interface, out_stream);
  }
  
  WriteInt(is_virtual, out_stream);
  WriteInt(is_debug, out_stream);
  if(is_debug) {
    WriteString(file_name, out_stream);
  }

  // write local space size
  WriteInt(cls_space, out_stream);
  WriteInt(inst_space, out_stream);
  cls_entries->Write(is_debug, out_stream);
  inst_entries->Write(is_debug, out_stream);

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
  
  WriteInt(is_virtual, out_stream);
  WriteInt(has_and_or, out_stream);
  
  if(emit_lib) {
    WriteInt(is_native, out_stream);
    WriteInt(is_function, out_stream);
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
