/***************************************************************************
 * Links pre-compiled code into existing program
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

#include "linker.h"
#include "../shared/instrs.h"
#include "../shared/version.h"

using namespace instructions;

/****************************
 * Creates associations with instructions
 * that reference library classes
 ****************************/
void Linker::ResloveExternalClass(LibraryClass* klass) 
{
  map<const wstring, LibraryMethod*> methods = klass->GetMethods();
  map<const wstring, LibraryMethod*>::iterator mthd_iter;
  for(mthd_iter = methods.begin(); mthd_iter != methods.end(); ++mthd_iter) {
    vector<LibraryInstr*> instrs =  mthd_iter->second->GetInstructions();
    for(size_t j = 0; j < instrs.size(); j++) {
      LibraryInstr* instr = instrs[j];
      // check library call
      switch(instr->GetType()) {
        // test
      case LIB_NEW_OBJ_INST:
      case LIB_OBJ_INST_CAST:
      case LIB_MTHD_CALL: {
        LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
        if(lib_klass) {
          if(!lib_klass->GetCalled()) {
            lib_klass->SetCalled(true);
            ResloveExternalClass(lib_klass);
          }
        } 
        else {
          wcerr << L"Error: Unable to resolve external library class: '"
                << instr->GetOperand5() << L"'; check library path" << endl;
          exit(1);
        }
      }
        break;

      default:
        break;
      }
    }
  }

}

void Linker::ResloveExternalClasses()
{
  // all libraries
  map<const wstring, Library*>::iterator lib_iter;
  for(lib_iter = libraries.begin(); lib_iter != libraries.end(); ++lib_iter) {
    // all classes
    vector<LibraryClass*> classes = lib_iter->second->GetClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      // all methods
      if(classes[i]->GetCalled()) {
        ResloveExternalClass(classes[i]);
      }
    }
  }
}

void Linker::ResolveExternalMethodCalls()
{
  // all libraries
  map<const wstring, Library*>::iterator lib_iter;
  for(lib_iter = libraries.begin(); lib_iter != libraries.end(); ++lib_iter) {
    // all classes
    vector<LibraryClass*> classes = lib_iter->second->GetClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      // all methods
      map<const wstring, LibraryMethod*> methods = classes[i]->GetMethods();
      map<const wstring, LibraryMethod*>::iterator mthd_iter;
      for(mthd_iter = methods.begin(); mthd_iter != methods.end(); ++mthd_iter) {
        vector<LibraryInstr*> instrs =  mthd_iter->second->GetInstructions();
        for(size_t j = 0; j < instrs.size(); j++) {
          LibraryInstr* instr = instrs[j];

          switch(instr->GetType()) {

            // NEW_OBJ_INST
          case LIB_NEW_OBJ_INST: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              instr->SetType(instructions::NEW_OBJ_INST);
              instr->SetOperand(lib_klass->GetId());
            } else {
              wcerr << L"Error: Unable to resolve external library class: '"
                    << instr->GetOperand5() << L"'; check library path" << endl;
              exit(1);
            }
          }
            break;
	    
	  case LIB_OBJ_TYPE_OF: {
	    LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              instr->SetType(instructions::OBJ_TYPE_OF);
              instr->SetOperand(lib_klass->GetId());
            } else {
              wcerr << L"Error: Unable to resolve external library class: '"
                    << instr->GetOperand5() << L"'; check library path" << endl;
              exit(1);
            }
	  }
	    break;

          case LIB_OBJ_INST_CAST: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              instr->SetType(instructions::OBJ_INST_CAST);
              instr->SetOperand(lib_klass->GetId());
            } else {
              wcerr << L"Error: Unable to resolve external library class: '"
                    << instr->GetOperand5() << L"'; check library path" << endl;
              exit(1);
            }
          }
            break;

            // MTHD_CALL
          case instructions::LIB_MTHD_CALL: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              LibraryMethod* lib_method = lib_klass->GetMethod(instr->GetOperand6());
              if(lib_method) {
                instr->SetType(instructions::MTHD_CALL);
                instr->SetOperand(lib_klass->GetId());
                instr->SetOperand2(lib_method->GetId());
              } 
              else {
                wcerr << L"Error: Unable to resolve external library method: '" << instr->GetOperand6() << L"'; check library path" << endl;
                exit(1);
              }
            } 
            else {
              wcerr << L"Error: Unable to resolve external library class: '" << instr->GetOperand5() << L"'; check library path" << endl;
              exit(1);
            }
          }
            break;

          case instructions::LIB_FUNC_DEF: {
            LibraryClass* lib_klass = SearchClassLibraries(instr->GetOperand5());
            if(lib_klass) {
              LibraryMethod* lib_method = lib_klass->GetMethod(instr->GetOperand6());
              if(lib_method) {
                // set method
                instr->SetType(instructions::LOAD_INT_LIT);
                instr->SetOperand(lib_method->GetId());
                // set class
                instrs[j + 1]->SetType(instructions::LOAD_INT_LIT);
                instrs[j + 1]->SetOperand(lib_klass->GetId());
              } 
              else {
                wcerr << L"Error: Unable to resolve external library method: '" << instr->GetOperand6() << L"'; check library path" << endl;
                exit(1);
              }
            } 
            else {
              wcerr << L"Error: Unable to resolve external library class: '" << instr->GetOperand5() << L"'; check library path" << endl;
              exit(1);
            }
          }
            break;

          default:
            break;
          }
        }
      }
    }
  }
}

/****************************
 * Returns all children related
 * to this class.
 ****************************/
vector<LibraryClass*> LibraryClass::GetLibraryChildren()
{
  if(!lib_children.size()) {
    map<const wstring, const wstring> hierarchies = library->GetHierarchies();
    map<const wstring, const wstring>::iterator iter;
    for(iter = hierarchies.begin(); iter != hierarchies.end(); ++iter) {
      if(iter->second == name) {
        lib_children.push_back(library->GetClass(iter->first));
      }
    }
  }

  return lib_children;
}

/****************************
 * Loads library file
 ****************************/
void Library::Load()
{
#ifdef _DEBUG
  GetLogger() << L"=== Loading file: '" << lib_path << L"' ===" << endl;
#endif
  LoadFile(lib_path);
}

/****************************
 * Reads a file
 ****************************/
void Library::LoadFile(const wstring &file_name)
{
  // read file into memory
  ReadFile(file_name);

  int ver_num = ReadInt();
  if(ver_num != VER_NUM) {
    wcerr << L"The " << lib_path << L" library appears to be compiled with a different version of the toolchain.  Please recompile the libraries or link the correct version." << endl;
    exit(1);
  } 

  int magic_num = ReadInt();
  if(magic_num ==  0xdddd) {
    wcerr << L"Unable to use executable '" << file_name << L"' as linked library." << endl;
    exit(1);
  } else if(magic_num !=  0xddde) {
    wcerr << L"Unable to link invalid library file '" << file_name << L"'." << endl;
    exit(1);
  }

  // read float strings
  const int num_float_strings = ReadInt();
  for(int i = 0; i < num_float_strings; i++) {
    frontend::FloatStringHolder* holder = new frontend::FloatStringHolder;
    holder->length = ReadInt();
    holder->value = new FLOAT_VALUE[holder->length];
    for(int j = 0; j < holder->length; j++) {
      holder->value[j] = ReadDouble();
    }
#ifdef _DEBUG
    GetLogger() << L"float string: id=" << i << L"; value=";
    for(int j = 0; j < holder->length; j++) {
      GetLogger() << holder->value[j] << L",";
    }
    GetLogger() << endl;
#endif
    FloatStringInstruction* str_instr = new FloatStringInstruction;
    str_instr->value = holder;
    float_strings.push_back(str_instr);
  }
  // read int strings
  const int num_int_strings = ReadInt();
  for(int i = 0; i < num_int_strings; i++) {
    frontend::IntStringHolder* holder = new frontend::IntStringHolder;
    holder->length = ReadInt();
    holder->value = new INT_VALUE[holder->length];
    for(int j = 0; j < holder->length; j++) {
      holder->value[j] = ReadInt();
    }
#ifdef _DEBUG
    GetLogger() << L"int string: id=" << i << L"; value=";
    for(int j = 0; j < holder->length; j++) {
      GetLogger() << holder->value[j] << L",";
    }
    GetLogger() << endl;
#endif
    IntStringInstruction* str_instr = new IntStringInstruction;
    str_instr->value = holder;
    int_strings.push_back(str_instr);
  }
  // read char wstrings
  const int num_char_strings = ReadInt();
  for(int i = 0; i < num_char_strings; i++) {
    const wstring &char_str_value = ReadString();
#ifdef _DEBUG
    const wstring &msg = L"char string: id=" + Linker::ToString(i) + L"; value='" + char_str_value + L"'";
    Linker::Show(msg, -1, 0);
#endif
    CharStringInstruction* str_instr = new CharStringInstruction;
    str_instr->value = char_str_value;
    char_strings.push_back(str_instr);
  }

  // read bundle names
  const int num_bundle_name = ReadInt();
  for(int i = 0; i < num_bundle_name; i++) {
    const wstring &str_value = ReadString();
    bundle_names.push_back(str_value);
#ifdef _DEBUG
    const wstring &msg = L"bundle name='" + str_value + L"'";
    Linker::Show(msg, -1, 0);
#endif
  }

  // load enums and classes
  LoadEnums();
  LoadClasses();
}

/****************************
 * Reads enums
 ****************************/
void Library::LoadEnums()
{
  const int number = ReadInt();
  for(int i = 0; i < number; i++) {
    // read enum
    const wstring &enum_name = ReadString();
#ifdef _DEBUG
    const wstring& msg = L"[enum: name='" + enum_name + L"']";
    Linker::Show(msg, 0, 1);
#endif
    const INT_VALUE enum_offset = ReadInt();
    LibraryEnum* eenum = new LibraryEnum(enum_name, enum_offset);

    // read enum items
    const INT_VALUE num_items = ReadInt();
    for(int i = 0; i < num_items; i++) {
      const wstring &item_name = ReadString();
      const INT_VALUE item_id = ReadInt();
      eenum->AddItem(new LibraryEnumItem(item_name, item_id, eenum));
    }
    // add enum
    AddEnum(eenum);
  }
}

/****************************
 * Reads classes
 ****************************/
void Library::LoadClasses()
{
  // we ignore all class ids
  const int number = ReadInt();
  for(int i = 0; i < number; i++) {
    // id
    ReadInt();
    const wstring &name = ReadString();

    // pid
    ReadInt();
    const wstring &parent_name = ReadString();

    // read interface ids
    const int interface_size = ReadInt();
    for(int i = 0; i < interface_size; i++) {
      ReadInt();
    }

    // read interface names
    vector<wstring> interface_names;
    const int interface_names_size = ReadInt();
    for(int i = 0; i < interface_names_size; i++) {
      interface_names.push_back(ReadString());
    }

    bool is_interface = ReadInt() != 0;
    bool is_virtual = ReadInt() != 0;
    bool is_debug = ReadInt() != 0;
    wstring file_name;
    if(is_debug) {
      file_name = ReadString();
    }
    const int cls_space = ReadInt();
    const int inst_space = ReadInt();

    // read type parameters
    backend::IntermediateDeclarations* cls_entries = LoadEntries(is_debug);
    backend::IntermediateDeclarations* inst_entries = LoadEntries(is_debug);      
    hierarchies.insert(pair<const wstring, const wstring>(name, parent_name));

#ifdef _DEBUG
    const wstring& msg = L"[class: name='" + name + L"'; parent='" + parent_name + 
      L"'; interface=" + Linker::ToString(is_interface) +       
      L"; virtual=" + Linker::ToString(is_virtual) + 
      L"; class_mem_size=" + Linker::ToString(cls_space) +
      L"; instance_mem_size=" + Linker::ToString(inst_space) + 
      L"; is_debug=" + Linker::ToString(is_debug) + L"]";
    Linker::Show(msg, 0, 1);
#endif

    LibraryClass* cls = new LibraryClass(name, parent_name, interface_names, is_interface, is_virtual, 
                                         cls_space, inst_space, cls_entries, inst_entries, this, 
                                         file_name, is_debug);
    // load method
    LoadMethods(cls, is_debug);
    // add class
    AddClass(cls);
  }
}

/****************************
 * Reads methods
 ****************************/
void Library::LoadMethods(LibraryClass* cls, bool is_debug)
{
  int number = ReadInt();
  for(int i = 0; i < number; i++) {
    int id = ReadInt();
    frontend::MethodType type = (frontend::MethodType)ReadInt();
    bool is_virtual = ReadInt() != 0;
    bool has_and_or = ReadInt() != 0;
    bool is_native = ReadInt() != 0;
    bool is_static = ReadInt() != 0;
    const wstring &name = ReadString();
    const wstring &rtrn_name = ReadString();
    int params = ReadInt();
    int mem_size = ReadInt();

    // read type parameters
    backend::IntermediateDeclarations* entries = new backend::IntermediateDeclarations;
    int num_params = ReadInt();
    for(int i = 0; i < num_params; i++) {
      instructions::ParamType type = (instructions::ParamType)ReadInt();
      wstring var_name;
      if(is_debug) {
        var_name = ReadString();
      }
      entries->AddParameter(new backend::IntermediateDeclaration(var_name, type));
    }

#ifdef _DEBUG
    const wstring &msg = L"(method: name=" + name + L"; id=" + Linker::ToString(id) + L"; num_params: " +
      Linker::ToString(params) + L"; mem_size=" + Linker::ToString(mem_size) + L"; is_native=" +
      Linker::ToString(is_native) +  L"; is_debug=" + Linker::ToString(is_debug) + L"]";
    Linker::Show(msg, 0, 2);
#endif

    LibraryMethod* mthd = new LibraryMethod(id, name, rtrn_name, type, is_virtual, has_and_or,
                                            is_native, is_static, params, mem_size, cls, entries);
    // load statements
    LoadStatements(mthd, is_debug);

    // add method
    cls->AddMethod(mthd);
  }
}

/****************************
 * Reads statements
 ****************************/
void Library::LoadStatements(LibraryMethod* method, bool is_debug)
{
  vector<LibraryInstr*> instrs;
  
  int line_num = -1;

  const uint32_t num_instrs = ReadUnsigned();
  for(uint32_t i = 0; i < num_instrs; ++i) {
    if(is_debug) {
      line_num = ReadInt();
    }    

    int type = ReadByte();
    switch(type) {
    case LOAD_INT_LIT:
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_LIT, (INT_VALUE)ReadInt()));
      break;
      
    case LOAD_CHAR_LIT:
      instrs.push_back(new LibraryInstr(line_num, LOAD_CHAR_LIT, (INT_VALUE)ReadChar()));
      break;
      
    case SHL_INT:
      instrs.push_back(new LibraryInstr(line_num, SHL_INT));
      break;

    case SHR_INT:
      instrs.push_back(new LibraryInstr(line_num, SHR_INT));
      break;

    case LOAD_INT_VAR: {
      INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_VAR, id, mem_context));
    }
      break;

    case LOAD_FUNC_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_FUNC_VAR, id, mem_context));
    }
      break;

    case LOAD_FLOAT_VAR: {
      INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_FLOAT_VAR, id, mem_context));
    }
      break;

    case STOR_INT_VAR: {
      INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_INT_VAR, id, mem_context));
    }
      break;

    case STOR_FUNC_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_FUNC_VAR, id, mem_context));
    }
      break;

    case STOR_FLOAT_VAR: {
      INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_FLOAT_VAR, id, mem_context));
    }
      break;

    case COPY_INT_VAR: {
      INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, COPY_INT_VAR, id, mem_context));
    }
      break;

    case COPY_FLOAT_VAR: {
      INT_VALUE id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, COPY_FLOAT_VAR, id, mem_context));
    }
      break;

    case NEW_INT_ARY: {
      INT_VALUE dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_INT_ARY, dim));
    }
      break;

    case NEW_FLOAT_ARY: {
      INT_VALUE dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_FLOAT_ARY, dim));
    }
      break;

    case NEW_BYTE_ARY: {
      INT_VALUE dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_BYTE_ARY, dim));

    }
      break;

    case NEW_CHAR_ARY: {
      INT_VALUE dim = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_CHAR_ARY, dim));
      
    }
      break;
      
    case NEW_OBJ_INST: {
      INT_VALUE obj_id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, NEW_OBJ_INST, obj_id));
    }
      break;

    case JMP: {
      INT_VALUE label = ReadInt();
      INT_VALUE cond = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, JMP, label, cond));
    }
      break;

    case LBL: {
      INT_VALUE id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LBL, id));
    }
      break;

    case MTHD_CALL: {
      int cls_id = ReadInt();
      int mthd_id = ReadInt();
      int is_native = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, MTHD_CALL, cls_id, mthd_id, is_native));
    }
      break;

    case DYN_MTHD_CALL: {
      int num_params = ReadInt();
      int rtrn_type = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, DYN_MTHD_CALL, num_params, rtrn_type));
    }
      break;

    case LIB_OBJ_INST_CAST: {
      const wstring& cls_name = ReadString();
#ifdef _DEBUG
      const wstring &msg = L"LIB_OBJ_INST_CAST: class=" + cls_name;
      Linker::Show(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_OBJ_INST_CAST, cls_name));
    }
      break;

    case LIB_NEW_OBJ_INST: {
      const wstring& cls_name = ReadString();
#ifdef _DEBUG
      const wstring &msg = L"LIB_NEW_OBJ_INST: class=" + cls_name;
      Linker::Show(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_NEW_OBJ_INST, cls_name));
    }
      break;
      
    case LIB_OBJ_TYPE_OF: {
      const wstring& cls_name = ReadString();
#ifdef _DEBUG
      const wstring &msg = L"LIB_OBJ_TYPE_OF: class=" + cls_name;
      Linker::Show(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_OBJ_TYPE_OF, cls_name));
    }
      break;
      
    case LIB_MTHD_CALL: {
      int is_native = ReadInt();
      const wstring& cls_name = ReadString();
      const wstring& mthd_name = ReadString();
#ifdef _DEBUG
      const wstring &msg = L"LIB_MTHD_CALL: class=" + cls_name + L", method=" + mthd_name;
      Linker::Show(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_MTHD_CALL, is_native, cls_name, mthd_name));
    }
      break;

    case LIB_FUNC_DEF: {
      const wstring& cls_name = ReadString();
      const wstring& mthd_name = ReadString();
#ifdef _DEBUG
      const wstring &msg = L"LIB_FUNC_DEF: class=" + cls_name + L", method=" + mthd_name;
      Linker::Show(msg, 0, 3);
#endif
      instrs.push_back(new LibraryInstr(line_num, LIB_FUNC_DEF, -1, cls_name, mthd_name));
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_LIT, -1));
    }
      break;

    case OBJ_INST_CAST: {
      INT_VALUE to_id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, OBJ_INST_CAST, to_id));
    }
      break;

    case OBJ_TYPE_OF: {
      INT_VALUE check_id = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, OBJ_TYPE_OF, check_id));
    }
      break;

    case LOAD_BYTE_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_BYTE_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_CHAR_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_CHAR_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_INT_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_INT_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_FLOAT_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, LOAD_FLOAT_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_BYTE_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_BYTE_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_CHAR_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_CHAR_ARY_ELM, dim, mem_context));
    }
      break;
      
    case STOR_INT_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_INT_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_FLOAT_ARY_ELM: {
      INT_VALUE dim = ReadInt();
      INT_VALUE mem_context = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, STOR_FLOAT_ARY_ELM, dim, mem_context));
    }
      break;

    case RTRN:
      instrs.push_back(new LibraryInstr(line_num, RTRN));
      break;

    case TRAP:
      instrs.push_back(new LibraryInstr(line_num, TRAP, ReadInt()));
      break;

    case TRAP_RTRN: {
      const int id = instrs.back()->GetOperand();
      if(id == instructions::CPY_CHAR_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        CharStringInstruction* str_instr = char_strings[cpy_instr->GetOperand()];
        str_instr->instrs.push_back(cpy_instr);
      }
      else if(id == instructions::CPY_INT_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        IntStringInstruction* str_instr = int_strings[cpy_instr->GetOperand()];
        str_instr->instrs.push_back(cpy_instr);
      }
      else if(id == instructions::CPY_FLOAT_STR_ARY) {
        LibraryInstr* cpy_instr = instrs[instrs.size() - 2];
        FloatStringInstruction* str_instr = float_strings[cpy_instr->GetOperand()];
        str_instr->instrs.push_back(cpy_instr);
      }
      instrs.push_back(new LibraryInstr(line_num, TRAP_RTRN, ReadInt()));
      break;
    }

    case SWAP_INT:
      instrs.push_back(new LibraryInstr(line_num, SWAP_INT));
      break;

    case POP_INT:
      instrs.push_back(new LibraryInstr(line_num, POP_INT));
      break;

    case POP_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, POP_FLOAT));
      break;

    case AND_INT:
      instrs.push_back(new LibraryInstr(line_num, AND_INT));
      break;

    case OR_INT:
      instrs.push_back(new LibraryInstr(line_num, OR_INT));
      break;

    case ADD_INT:
      instrs.push_back(new LibraryInstr(line_num, ADD_INT));
      break;

    case FLOR_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, FLOR_FLOAT));
      break;

    case CPY_BYTE_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_BYTE_ARY));
      break;
      
    case LOAD_ARY_SIZE:
      instrs.push_back(new LibraryInstr(line_num, LOAD_ARY_SIZE));
      break;
      
    case CPY_CHAR_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_CHAR_ARY));
      break;
      
    case CPY_INT_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_INT_ARY));
      break;

    case CPY_FLOAT_ARY:
      instrs.push_back(new LibraryInstr(line_num, CPY_FLOAT_ARY));
      break;

    case CEIL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, CEIL_FLOAT));
      break;

    case SIN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SIN_FLOAT));
      break;

    case COS_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, COS_FLOAT));
      break;

    case TAN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, TAN_FLOAT));
      break;

    case ASIN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ASIN_FLOAT));
      break;

    case ACOS_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ACOS_FLOAT));
      break;

    case ATAN_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ATAN_FLOAT));
      break;
      
    case ATAN2_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ATAN2_FLOAT));
      break;
      
    case LOG_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LOG_FLOAT));
      break;

    case POW_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, POW_FLOAT));
      break;

    case SQRT_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SQRT_FLOAT));
      break;

    case RAND_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, RAND_FLOAT));
      break;

    case F2I:
      instrs.push_back(new LibraryInstr(line_num, F2I));
      break;

    case I2F:
      instrs.push_back(new LibraryInstr(line_num, I2F));
      break;

    case S2I:
      instrs.push_back(new LibraryInstr(line_num, S2I));
      break;

    case S2F:
      instrs.push_back(new LibraryInstr(line_num, S2F));
      break;
      
    case I2S:
      instrs.push_back(new LibraryInstr(line_num, I2S));
      break;
      
    case F2S:
      instrs.push_back(new LibraryInstr(line_num, F2S)); 
      break;
      
    case LOAD_CLS_MEM:
      instrs.push_back(new LibraryInstr(line_num, LOAD_CLS_MEM));
      break;

    case LOAD_INST_MEM:
      instrs.push_back(new LibraryInstr(line_num, LOAD_INST_MEM));
      break;

    case SUB_INT:
      instrs.push_back(new LibraryInstr(line_num, SUB_INT));
      break;

    case MUL_INT:
      instrs.push_back(new LibraryInstr(line_num, MUL_INT));
      break;

    case DIV_INT:
      instrs.push_back(new LibraryInstr(line_num, DIV_INT));
      break;

    case MOD_INT:
      instrs.push_back(new LibraryInstr(line_num, MOD_INT));
      break;

    case BIT_AND_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_AND_INT));
      break;

    case BIT_OR_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_OR_INT));
      break;

    case BIT_XOR_INT:
      instrs.push_back(new LibraryInstr(line_num, BIT_XOR_INT));
      break;

    case EQL_INT:
      instrs.push_back(new LibraryInstr(line_num, EQL_INT));
      break;

    case NEQL_INT:
      instrs.push_back(new LibraryInstr(line_num, NEQL_INT));
      break;

    case LES_INT:
      instrs.push_back(new LibraryInstr(line_num, LES_INT));
      break;

    case GTR_INT:
      instrs.push_back(new LibraryInstr(line_num, GTR_INT));
      break;

    case LES_EQL_INT:
      instrs.push_back(new LibraryInstr(line_num, LES_EQL_INT));
      break;

    case GTR_EQL_INT:
      instrs.push_back(new LibraryInstr(line_num, GTR_EQL_INT));
      break;

    case ADD_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, ADD_FLOAT));
      break;

    case SUB_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, SUB_FLOAT));
      break;

    case MUL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, MUL_FLOAT));
      break;

    case DIV_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, DIV_FLOAT));
      break;

    case EQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, EQL_FLOAT));
      break;

    case NEQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, NEQL_FLOAT));
      break;

    case LES_EQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LES_EQL_FLOAT));
      break;

    case GTR_EQL_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, GTR_EQL_FLOAT));
      break;

    case LES_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, LES_FLOAT));
      break;

    case GTR_FLOAT:
      instrs.push_back(new LibraryInstr(line_num, GTR_FLOAT));
      break;

    case LOAD_FLOAT_LIT:
      instrs.push_back(new LibraryInstr(line_num, LOAD_FLOAT_LIT, ReadDouble()));
      break;

    case ASYNC_MTHD_CALL: {
      int cls_id = ReadInt();
      int mthd_id = ReadInt();
      int is_native = ReadInt();
      instrs.push_back(new LibraryInstr(line_num, ASYNC_MTHD_CALL, cls_id, mthd_id, is_native));
    }
      break;

    case DLL_LOAD:
      instrs.push_back(new LibraryInstr(line_num, DLL_LOAD));
      break;

    case DLL_UNLOAD:
      instrs.push_back(new LibraryInstr(line_num, DLL_UNLOAD));
      break;

    case DLL_FUNC_CALL:
      instrs.push_back(new LibraryInstr(line_num, DLL_FUNC_CALL));
      break;

    case THREAD_JOIN:
      instrs.push_back(new LibraryInstr(line_num, THREAD_JOIN));
      break;

    case THREAD_SLEEP:
      instrs.push_back(new LibraryInstr(line_num, THREAD_SLEEP));
      break;

    case THREAD_MUTEX:
      instrs.push_back(new LibraryInstr(line_num, THREAD_MUTEX));
      break;

    case CRITICAL_START:
      instrs.push_back(new LibraryInstr(line_num, CRITICAL_START));
      break;

    case CRITICAL_END:
      instrs.push_back(new LibraryInstr(line_num, CRITICAL_END));
      break;

    default: {
#ifdef _DEBUG
      InstructionType instr = (InstructionType)type;
      wcerr << L">>> unknown instruction: " << instr << L" <<<" << endl;
#endif
      exit(1);
    }
      break;
    }    
  }
  method->AddInstructions(instrs);
}
