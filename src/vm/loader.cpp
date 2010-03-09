/***************************************************************************
 * Program loader.
 *
 * Copyright (c) 2008-2009, Randy Hollines
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

#include "loader.h"
#include "common.h"

void Loader::Load()
{
  int magic_num = ReadInt();
  if(magic_num == 0xddde) {
    cerr << "Unable to use execute shared library '" << filename << "'." << endl;
    exit(1);
  } else if(magic_num != 0xdddd) {
    cerr << "Unable to execute invalid program file '" << filename << "'." << endl;
    exit(1);
  }

  // read strings
  num_char_strings = ReadInt();
  BYTE_VALUE** char_strings = new BYTE_VALUE*[num_char_strings + arguments.size()];
  // copy static program strings
  int i;
  for(i = 0; i < num_char_strings; i++) {
    string value = ReadString();
    BYTE_VALUE* char_string = new BYTE_VALUE[value.size() + 1];
    // copy string
    unsigned int j = 0;
    for(; j < value.size(); j++) {
      char_string[j] = value[j];
    }
    char_string[j] = '\0';
#ifdef _DEBUG
    cout << "Loaded static string: '" << char_string << "'" << endl;
#endif
    char_strings[i] = char_string;
  }

  // copy command line params
  for(unsigned int j = 0; j < arguments.size(); i++, j++) {
    char_strings[i] = (BYTE_VALUE*)strdup(arguments[j].c_str());
#ifdef _DEBUG
    cout << "Loaded static string: '" << char_strings[i] << "'" << endl;
#endif
  }
  program->SetCharStrings(char_strings, num_char_strings);

#ifdef _DEBUG
  cout << "=======================================" << endl;
#endif

  // read start class and method ids
  start_class_id = ReadInt();
  start_method_id = ReadInt();
#ifdef _DEBUG
  cout << "Program starting point: " << start_class_id << ","
       << start_method_id << endl;
#endif

  LoadEnums();
  LoadClasses();

  string name = "$Initialization$:";
  StackDclr** dclrs = new StackDclr*[1];
  dclrs[0] = new StackDclr;
  dclrs[0]->type = OBJ_ARY_PARM;
  dclrs[0]->id = string_cls_id;

  init_method = new StackMethod(-1, name, false, false, dclrs,	1, 0, 1, NIL_TYPE, NULL);
  LoadInitializationCode(init_method);
  program->SetInitializationMethod(init_method);
  program->SetStringClassId(string_cls_id);
}

void Loader::LoadEnums()
{
  const int number = ReadInt();
  for(int i = 0; i < number; i++) {
    // read enum
    // const string &enum_name = ReadString();
    ReadString();

    // const long enum_offset = ReadInt();
    ReadInt();

    // read enum items
    const long num_items = ReadInt();
    for(int i = 0; i < num_items; i++) {
      // const string &item_name = ReadString();
      ReadString();

      // const long item_id = ReadInt();
      ReadInt();
    }
  }
}

void Loader::LoadClasses()
{
  const int number = ReadInt();
  int* cls_hierarchy = new int[number];
  StackClass** classes = new StackClass*[number];

#ifdef _DEBUG
  cout << "Reading " << number << " classe(s)..." << endl;
#endif

  for(int i = 0; i < number; i++) {
    const int id = ReadInt();
    string name = ReadString();
    const int pid = ReadInt();
    string parent_name = ReadString();
    // is virtual
    const bool is_virtual = ReadInt();
    // space
    const int cls_space = ReadInt();
    const int inst_space = ReadInt();
    // read type parameters
    const int num_dclrs = ReadInt();
    StackDclr** dclrs = new StackDclr*[num_dclrs];
    for(int i = 0; i < num_dclrs; i++) {
      // set type
      int type = ReadInt();
      dclrs[i] = new StackDclr;
      dclrs[i]->type = (ParamType)type;
      // set id
      switch(type) {
      case instructions::OBJ_PARM:
      case instructions::OBJ_ARY_PARM:
        dclrs[i]->id = ReadInt();
        break;
      }
    }

    cls_hierarchy[id] = pid;
    StackClass* cls = new StackClass(id, name, pid, is_virtual, dclrs,
                                     num_dclrs, cls_space, inst_space);

    if(string_cls_id < 0 && name == "System.String") {
      string_cls_id = id;
    }

#ifdef _DEBUG
    cout << "Class(" << cls << "): id=" << id << "; name='" << name << "'; parent='"
         << parent_name << "'; class_bytes=" << cls_space << "'; instance_bytes="
         << inst_space << endl;
#endif

    // load methods
    LoadMethods(cls);
    // add class
#ifdef _DEBUG
    assert(id < number);
#endif
    classes[id] = cls;
  }
  // set class hierarchy
  program->SetClasses(classes, number);
  program->SetHierarchy(cls_hierarchy);
}

void Loader::LoadMethods(StackClass* cls)
{
  const int number = ReadInt();
#ifdef _DEBUG
  cout << "Reading " << number << " method(s)..." << endl;
#endif

  StackMethod** methods = new StackMethod*[number];
  for(int i = 0; i < number; i++) {
    // id
    const int id = ReadInt();
    // method type
    ReadInt();
    // virtual
    const bool is_virtual = ReadInt();
    // has and/or
    const bool has_and_or = ReadInt();
    // is native
    ReadInt();
    // is static
    ReadInt();
    // name
    string name = ReadString();
    // return
    string rtrn_name = ReadString();
    // params
    const int params = ReadInt();
    // space
    const int mem_size = ReadInt();
    // read type parameters
    const int num_dclrs = ReadInt();
    StackDclr** dclrs = new StackDclr*[num_dclrs];
    for(int i = 0; i < num_dclrs; i++) {
      // set type
      const int type = ReadInt();
      dclrs[i] = new StackDclr;
      dclrs[i]->type = (ParamType)type;
      // set id
      switch(type) {
      case instructions::OBJ_PARM:
      case instructions::OBJ_ARY_PARM:
        dclrs[i]->id = ReadInt();
        break;
      }
    }

    // parse return
    MemoryType rtrn_type;
    switch(rtrn_name[0]) {
    case 'l': // bool
    case 'b': // byte
    case 'c': // character
    case 'i': // int
    case 'o': // object
      rtrn_type = INT_TYPE;
      break;

    case 'f': // float
      if(rtrn_name.size() > 1) {
        rtrn_type = INT_TYPE;
      } else {
        rtrn_type = FLOAT_TYPE;
      }
      break;

    case 'n': // nil
      rtrn_type = NIL_TYPE;
      break;

    default:
      cerr << ">>> unknown type <<<" << endl;
      exit(1);
      break;
    }

    StackMethod* mthd = new StackMethod(id, name, is_virtual, has_and_or, dclrs,
                                        num_dclrs, params, mem_size, rtrn_type, cls);

#ifdef _DEBUG
    cout << "Method(" << mthd << "): id=" << id << "; name='" << name << "'; return='" << rtrn_name
         << "'; params=" << params << "; bytes=" << mem_size << endl;
#endif

    // load statements
    LoadStatements(mthd);

    // add method
#ifdef _DEBUG
    assert(id < number);
#endif
    methods[id] = mthd;
  }
  cls->SetMethods(methods, number);
}

void Loader::LoadInitializationCode(StackMethod* method)
{
  method->AddInstruction(new StackInstr(LOAD_INT_LIT, (long)arguments.size()));
  method->AddInstruction(new StackInstr(NEW_INT_ARY, (long)1));
  method->AddInstruction(new StackInstr(STOR_INT_VAR, 0L, LOCL));

  for(unsigned int i = 0; i < arguments.size(); i++) {
    method->AddInstruction(new StackInstr(LOAD_INT_LIT, (long)arguments[i].size()));
    method->AddInstruction(new StackInstr(NEW_BYTE_ARY, 1L));
    method->AddInstruction(new StackInstr(LOAD_INT_LIT, (long)(num_char_strings + i)));
    method->AddInstruction(new StackInstr(LOAD_INT_LIT, -3998L));
    method->AddInstruction(new StackInstr(TRAP_RTRN, 3L));

    method->AddInstruction(new StackInstr(NEW_OBJ_INST, (long)string_cls_id));
    // note: method ID is position dependant
    method->AddInstruction(new StackInstr(MTHD_CALL, (long)string_cls_id, 2L, 0L));

    method->AddInstruction(new StackInstr(LOAD_INT_LIT, (long)i));
    method->AddInstruction(new StackInstr(LOAD_INT_VAR, 0L, LOCL));
    method->AddInstruction(new StackInstr(STOR_INT_ARY_ELM, 1L, LOCL));
  }

  method->AddInstruction(new StackInstr(LOAD_INT_VAR, 0L, LOCL));
  method->AddInstruction (new StackInstr(LOAD_INST_MEM));
  method->AddInstruction(new StackInstr(MTHD_CALL, (long)start_class_id, (long)start_method_id, 0L));
  method->AddInstruction(new StackInstr(RTRN));
}

void Loader::LoadStatements(StackMethod* method)
{
  int index = 0;

  int type = ReadInt();
  while(type != END_STMTS) {
    switch(type) {
    case LOAD_INT_LIT:
      method->AddInstruction(new StackInstr(LOAD_INT_LIT, (long)ReadInt()));
      break;

    case SHL_INT:
      method->AddInstruction(new StackInstr(SHL_INT, (long)ReadInt()));
      break;

    case SHR_INT:
      method->AddInstruction(new StackInstr(SHR_INT, (long)ReadInt()));
      break;

    case LOAD_INT_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(LOAD_INT_VAR, id, mem_context));
    }
      break;

    case LOAD_FLOAT_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(LOAD_FLOAT_VAR, id, mem_context));
    }
      break;

    case STOR_INT_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(STOR_INT_VAR, id, mem_context));
    }
      break;

    case STOR_FLOAT_VAR: {
      long id = ReadInt();
      long mem_context = ReadInt();
      method->AddInstruction(new StackInstr(STOR_FLOAT_VAR, id, mem_context));
    }
      break;

    case COPY_INT_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(COPY_INT_VAR, id, mem_context));
    }
      break;

    case COPY_FLOAT_VAR: {
      long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(COPY_FLOAT_VAR, id, mem_context));
    }
      break;

    case LOAD_BYTE_ARY_ELM: {
      long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(LOAD_BYTE_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_INT_ARY_ELM: {
      long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(LOAD_INT_ARY_ELM, dim, mem_context));
    }
      break;

    case LOAD_FLOAT_ARY_ELM: {
      long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(LOAD_FLOAT_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_BYTE_ARY_ELM: {
      long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(STOR_BYTE_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_INT_ARY_ELM: {
      long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      method->AddInstruction(new StackInstr(STOR_INT_ARY_ELM, dim, mem_context));
    }
      break;

    case STOR_FLOAT_ARY_ELM: {
      long dim = ReadInt();
      long mem_context = ReadInt();
      method->AddInstruction(new StackInstr(STOR_FLOAT_ARY_ELM, dim, mem_context));
    }
      break;

    case NEW_FLOAT_ARY: {
      long dim = ReadInt();
      method->AddInstruction(new StackInstr(NEW_FLOAT_ARY, dim));
    }
      break;

    case NEW_INT_ARY: {
      long dim = ReadInt();
      method->AddInstruction(new StackInstr(NEW_INT_ARY, dim));
    }
      break;

    case NEW_BYTE_ARY: {
      long dim = ReadInt();
      method->AddInstruction(new StackInstr(NEW_BYTE_ARY, dim));

    }
      break;

    case NEW_OBJ_INST: {
      long obj_id = ReadInt();
      method->AddInstruction(new StackInstr(NEW_OBJ_INST, obj_id));
    }
      break;

    case MTHD_CALL: {
      long cls_id = ReadInt();
      long mthd_id = ReadInt();
      long is_native = ReadInt();
      method->AddInstruction(new StackInstr(MTHD_CALL, cls_id, mthd_id, is_native));
    }
      break;

    case ASYNC_MTHD_CALL: {
      long cls_id = ReadInt();
      long mthd_id = ReadInt();
      long is_native = ReadInt();
      method->AddInstruction(new StackInstr(ASYNC_MTHD_CALL, cls_id, mthd_id, is_native));
    }
      break;
      
    case LIB_OBJ_INST_CAST:
      cerr << ">>> unsupported instruction for executable: LIB_OBJ_INST_CAST <<<" << endl;
      exit(1);

    case LIB_NEW_OBJ_INST:
      cerr << ">>> unsupported instruction for executable: LIB_NEW_OBJ_INST <<<" << endl;
      exit(1);

    case LIB_MTHD_CALL:
      cerr << ">>> unsupported instruction for executable: LIB_MTHD_CALL <<<" << endl;
      exit(1);

    case JMP: {
      long label = ReadInt();
      long cond = ReadInt();
      method->AddInstruction(new StackInstr(JMP, label, cond));
    }
      break;

    case LBL: {
      long id = ReadInt();
      method->AddInstruction(new StackInstr(LBL, id));
      method->AddLabel(id, index);
    }
      break;

    case OBJ_INST_CAST: {
      long to = ReadInt();
      method->AddInstruction(new StackInstr(OBJ_INST_CAST, to));
    }
      break;

    case OR_INT:
      method->AddInstruction(new StackInstr(OR_INT));
      break;

    case AND_INT:
      method->AddInstruction(new StackInstr(AND_INT));
      break;

    case ADD_INT:
      method->AddInstruction(new StackInstr(ADD_INT));
      break;

    case CEIL_FLOAT:
      method->AddInstruction(new StackInstr(CEIL_FLOAT));
      break;

    case FLOR_FLOAT:
      method->AddInstruction(new StackInstr(FLOR_FLOAT));
      break;

    case F2I:
      method->AddInstruction(new StackInstr(F2I));
      break;

    case I2F:
      method->AddInstruction(new StackInstr(I2F));
      break;

    case POP_INT:
      method->AddInstruction(new StackInstr(POP_INT));
      break;

    case POP_FLOAT:
      method->AddInstruction(new StackInstr(POP_FLOAT));
      break;

    case LOAD_CLS_MEM:
      method->AddInstruction(new StackInstr(LOAD_CLS_MEM));
      break;

    case LOAD_INST_MEM:
      method->AddInstruction (new StackInstr(LOAD_INST_MEM));
      break;

    case SUB_INT:
      method->AddInstruction(new StackInstr(SUB_INT));
      break;

    case MUL_INT:
      method->AddInstruction(new StackInstr(MUL_INT));
      break;

    case DIV_INT:
      method->AddInstruction(new StackInstr(DIV_INT));
      break;

    case MOD_INT:
      method->AddInstruction(new StackInstr(MOD_INT));
      break;

    case EQL_INT:
      method->AddInstruction(new StackInstr(EQL_INT));
      break;

    case NEQL_INT:
      method->AddInstruction(new StackInstr(NEQL_INT));
      break;

    case LES_INT:
      method->AddInstruction(new StackInstr(LES_INT));
      break;

    case GTR_INT:
      method->AddInstruction(new StackInstr(GTR_INT));
      break;

    case LES_EQL_INT:
      method->AddInstruction(new StackInstr(LES_EQL_INT));
      break;

    case LES_EQL_FLOAT:
      method->AddInstruction(new StackInstr(LES_EQL_FLOAT));
      break;

    case GTR_EQL_INT:
      method->AddInstruction(new StackInstr(GTR_EQL_INT));
      break;

    case GTR_EQL_FLOAT:
      method->AddInstruction(new StackInstr(GTR_EQL_FLOAT));
      break;

    case ADD_FLOAT:
      method->AddInstruction(new StackInstr(ADD_FLOAT));
      break;

    case SUB_FLOAT:
      method->AddInstruction(new StackInstr(SUB_FLOAT));
      break;

    case MUL_FLOAT:
      method->AddInstruction(new StackInstr(MUL_FLOAT));
      break;

    case DIV_FLOAT:
      method->AddInstruction(new StackInstr(DIV_FLOAT));
      break;

    case EQL_FLOAT:
      method->AddInstruction(new StackInstr(EQL_FLOAT));
      break;

    case NEQL_FLOAT:
      method->AddInstruction(new StackInstr(NEQL_FLOAT));
      break;

    case LES_FLOAT:
      method->AddInstruction(new StackInstr(LES_FLOAT));
      break;

    case GTR_FLOAT:
      method->AddInstruction(new StackInstr(GTR_FLOAT));
      break;

    case LOAD_FLOAT_LIT:
      method->AddInstruction(new StackInstr(LOAD_FLOAT_LIT,
                                            ReadDouble()));
      break;

    case RTRN:
      method->AddInstruction(new StackInstr(RTRN));
      break;

    case THREAD_JOIN:
      method->AddInstruction(new StackInstr(THREAD_JOIN));
      break;
      
    case THREAD_SLEEP:
      method->AddInstruction(new StackInstr(THREAD_SLEEP));
      break;

    case CRITICAL_START:
      method->AddInstruction(new StackInstr(CRITICAL_START));
      break;

    case CRITICAL_END:
      method->AddInstruction(new StackInstr(CRITICAL_END));
      break;

    case TRAP: {
      long args = ReadInt();
      method->AddInstruction(new StackInstr(TRAP, args));
    }
      break;

    case TRAP_RTRN: {
      long args = ReadInt();
      method->AddInstruction(new StackInstr(TRAP_RTRN, args));
    }
      break;

    default: {
#ifdef _DEBUG
      InstructionType instr = (InstructionType)type;
      cout << ">>> unknown instruction: id=" << instr << " <<<" << endl;
#endif
      exit(1);
    }
      break;

    }
    // update
    type = ReadInt();
    index++;
  }
}
