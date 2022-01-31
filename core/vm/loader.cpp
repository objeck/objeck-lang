/***************************************************************************
 * Program loader.
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
 * - Neither the name of the Objeck Team nor the names of its
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
#include "../shared/version.h"

StackProgram* Loader::program;

StackProgram* Loader::GetProgram() {
  return program;
}

void Loader::LoadConfiguration()
{
  ifstream in("obr.conf");
  if(in.good()) {
    string line;
    do {
      getline(in, line);
      size_t pos = line.find('=');
      if(pos != string::npos) {
        string name = line.substr(0, pos);
        string value = line.substr(pos + 1);
        params.insert(pair<const wstring, int>(BytesToUnicode(name), atoi(value.c_str())));
      }
    }
    while(!in.eof());
  }
  in.close();
}

void Loader::Load()
{
  const int ver_num = ReadInt();
  if(ver_num != VER_NUM) {
    wcerr << L"This executable appears to be invalid or compiled with a different version of the toolchain." << endl;
    exit(1);
  } 

  const int magic_num = ReadInt();
  switch(magic_num) {
  case MAGIC_NUM_LIB:
    wcerr << L"Unable to use execute shared library '" << filename << L"'." << endl;
    exit(1);

  case MAGIC_NUM_EXE:
    break;

  case MAGIC_NUM_WEB:
    is_web = true;
    break;

  default:
    wcerr << L"Unknown file type for '" << filename << L"'." << endl;
    exit(1);
  }

  // read string id
  string_cls_id = ReadInt();

  int i;
  // read float strings
  num_float_strings = ReadInt();
  FLOAT_VALUE** float_strings = new FLOAT_VALUE*[num_float_strings];
  for(i = 0; i < num_float_strings; ++i) {
    const int float_string_length = ReadInt();
    FLOAT_VALUE* float_string = new FLOAT_VALUE[float_string_length];
    // copy string    
#ifdef _DEBUG
    wcout << L"Loaded static float string[" << i << L"]: '";
#endif
    for(int j = 0; j < float_string_length; j++) {
      float_string[j] = ReadDouble();
#ifdef _DEBUG
      wcout << float_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    wcout << L"'" << endl;
#endif
    float_strings[i] = float_string;
  }
  program->SetFloatStrings(float_strings, num_float_strings);

  // read int strings
  num_int_strings = ReadInt();
  INT_VALUE** int_strings = new INT_VALUE*[num_int_strings];
  for(i = 0; i < num_int_strings; ++i) {
    const int int_string_length = ReadInt();
    INT_VALUE* int_string = new INT_VALUE[int_string_length];
    // copy string    
#ifdef _DEBUG
    wcout << L"Loaded static int string[" << i << L"]: '";
#endif
    for(int j = 0; j < int_string_length; ++j) {
      int_string[j] = ReadInt();
#ifdef _DEBUG
      wcout << int_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    wcout << L"'" << endl;
#endif
    int_strings[i] = int_string;
  }
  program->SetIntStrings(int_strings, num_int_strings);
  
  // read char strings
  num_char_strings = ReadInt();
  wchar_t** char_strings = new wchar_t*[num_char_strings + arguments.size()];
  for(i = 0; i < num_char_strings; ++i) {
    const wstring value = ReadString();
    wchar_t* char_string = new wchar_t[value.size() + 1];
    // copy string
#ifdef _WIN32
    wcscpy_s(char_string, value.size() + 1, value.c_str());
#else
    wcscpy(char_string, value.c_str());
#endif

#ifdef _DEBUG
    wcout << L"Loaded static character string[" << i << L"]: '" << char_string << L"'" << endl;
#endif
    char_strings[i] = char_string;
  }

  // copy command line params
  for(size_t j = 0; j < arguments.size(); ++i, ++j) {
#ifdef _WIN32
    char_strings[i] = _wcsdup((arguments[j]).c_str());
#else
    char_strings[i] = wcsdup((arguments[j]).c_str());
#endif

#ifdef _DEBUG
    wcout << L"Loaded static string: '" << char_strings[i] << L"'" << endl;
#endif
  }
  program->SetCharStrings(char_strings, num_char_strings);

#ifdef _DEBUG
  wcout << L"=======================================" << endl;
#endif
  
  // read start class and method ids
  start_class_id = ReadInt();
  start_method_id = ReadInt();
#ifdef _DEBUG
  wcout << L"Program starting point: " << start_class_id << L"," << start_method_id << endl;
#endif

  LoadClasses();
  
  const wstring name = L"$Initialization$:";
  StackDclr** dclrs = new StackDclr*[1];
  dclrs[0] = new StackDclr;
  dclrs[0]->name = L"args";
  dclrs[0]->type = OBJ_ARY_PARM;

  init_method = new StackMethod(-1, name, false, false, false, dclrs,  1, 0, 1, NIL_TYPE, nullptr);
  LoadInitializationCode(init_method);
  program->SetInitializationMethod(init_method);
  program->SetStringObjectId(string_cls_id);
}

char* Loader::LoadFileBuffer(wstring filename, size_t& buffer_size)
{
  char* buffer;
  const string open_filename = UnicodeToBytes(filename);

  ifstream in(open_filename.c_str(), ios_base::in | ios_base::binary | ios_base::ate);
  if(in.good()) {
    // get file size
    in.seekg(0, ios::end);
    buffer_size = (size_t)in.tellg();
    in.seekg(0, ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    in.read(buffer, buffer_size);
    // close file
    in.close();

    uLong dest_len;
    char* out = OutputStream::Uncompress(buffer, (uLong)buffer_size, dest_len);
    if(!out) {
      wcerr << L"Unable to uncompress file: " << filename << endl;
      exit(1);
    }
#ifdef _DEBUG
    std::wcout << L"--- file in: compressed=" << buffer_size << L", uncompressed=" << dest_len << L" ---" << std::endl;
#endif

    free(buffer);
    buffer = nullptr;
    return out;
  }
  else {
    wcerr << L"Unable to open file: " << filename << endl;
    exit(1);
  }

  return nullptr;
}

void Loader::LoadClasses()
{
  const int num_classes = ReadInt();
  long* cls_hierarchy = new long[num_classes];
  long** cls_interfaces = new long*[num_classes];
  StackClass** classes = new StackClass*[num_classes];

#ifdef _DEBUG
  wcout << L"Reading " << num_classes << L" classe(s)..." << endl;
#endif

  for(int i = 0; i < num_classes; ++i) {
    // read id and pid
    const int id = ReadInt();
    wstring name = ReadString();
    const int pid = ReadInt();
    wstring parent_name = ReadString();

    // read interface ids
    const int num_interfaces = ReadInt();
    if(num_interfaces > 0) {
      long* interfaces = new long[num_interfaces + 1];
      int j = 0;
      while(j < num_interfaces) {
        interfaces[j++] = ReadInt();
      }
      interfaces[j] = INF_ENDING;
      cls_interfaces[id] = interfaces;
    }
    else {
      cls_interfaces[id] = nullptr;
    }
    
    const bool is_virtual = ReadByte() != 0;
    const bool is_debug = ReadByte() != 0;
    wstring file_name;
    if(is_debug) {
      file_name = ReadString();
    }

    // space
    const int cls_space = ReadInt();
    const int inst_space = ReadInt();

    // read class declarations
    const int cls_num_dclrs = ReadInt();
    StackDclr** cls_dclrs = LoadDeclarations(cls_num_dclrs, is_debug);

    // read instance declarations
    const int inst_num_dclrs = ReadInt();
    StackDclr** inst_dclrs = LoadDeclarations(inst_num_dclrs, is_debug);
    
    // read closure declarations
    map<int, pair<int, StackDclr**> > closure_dclr_map;
    const int num_closure_dclrs = ReadInt();
    for(int i = 0; i < num_closure_dclrs; ++i) {
      const int closure_mthd_id = ReadInt();
      // read closure declarations
      const int closure_num_dclrs = ReadInt();
      StackDclr** closure_dclrs = LoadDeclarations(closure_num_dclrs, is_debug);
      // add declarations to map
      closure_dclr_map[closure_mthd_id] = pair<int, StackDclr**>(closure_num_dclrs, closure_dclrs);
    }

    cls_hierarchy[id] = pid;
    StackClass* cls = new StackClass(id, name, file_name, pid, is_virtual, cls_dclrs, cls_num_dclrs, inst_dclrs, 
                                     closure_dclr_map, inst_num_dclrs, cls_space, inst_space, is_debug);

#ifdef _DEBUG
    wcout << L"Class(" << cls << L"): id=" << id << L"; name='" << name << L"'; parent='"
    << parent_name << L"'; class_bytes=" << cls_space << L"'; instance_bytes="
    << inst_space << endl;
#endif

    // load methods
    LoadMethods(cls, is_debug);
    // add class
#ifdef _DEBUG
    assert(id < num_classes);
#endif
    classes[id] = cls;
  }

  // set class hierarchy and interfaces
  program->SetClasses(classes, num_classes);
  program->SetHierarchy(cls_hierarchy);
  program->SetInterfaces(cls_interfaces);
}

StackDclr** Loader::LoadDeclarations(const int num_dclrs, const bool is_debug)
{
  StackDclr** dclrs = new StackDclr * [num_dclrs];
  for(int j = 0; j < num_dclrs; ++j) {
    // set type
    int type = ReadInt();
    // set name
    wstring name;
    if(is_debug) {
      name = ReadString();
    }
    dclrs[j] = new StackDclr;
    dclrs[j]->name = name;
    dclrs[j]->type = (ParamType)type;
  }

  return dclrs;
}

void Loader::LoadMethods(StackClass* cls, bool is_debug)
{
  const int number = ReadInt();
#ifdef _DEBUG
  wcout << L"Reading " << number << L" method(s)..." << endl;
#endif
  
  StackMethod** methods = new StackMethod*[number];
  for(int i = 0; i < number; ++i) {
    // id
    const int id = ReadInt();
    // method type
    const bool is_virtual = ReadByte() != 0;
    // has and/or
    const bool has_and_or = ReadByte() != 0;
    // is lambda expression
    const bool is_lambda = ReadByte() != 0;
    // name
    const wstring name = ReadString();
    // return
    const wstring rtrn_name = ReadString();
    // params
    const int params = ReadInt();
    // space
    const int mem_size = ReadInt();
    // read type parameters
    const int num_dclrs = ReadInt();

    StackDclr** dclrs = new StackDclr*[num_dclrs];
    for(int j = 0; j < num_dclrs; ++j) {
      // set type
      const int type = ReadInt();
      // set name
      wstring name;
      if(is_debug) {
        name = ReadString();
      }
      dclrs[j] = new StackDclr;
      dclrs[j]->name = name;
      dclrs[j]->type = (ParamType)type;
    }

    // parse return
    MemoryType rtrn_type;
    switch(rtrn_name[0]) {
    case L'l': // bool
    case L'b': // byte
    case L'c': // character
    case L'i': // int
    case L'o': // object
      rtrn_type = INT_TYPE;
      break;

    case L'f': // float
      if(rtrn_name.size() > 1) {
        rtrn_type = INT_TYPE;
      } 
      else {
        rtrn_type = FLOAT_TYPE;
      }
      break;

    case L'n': // nil
      rtrn_type = NIL_TYPE;
      break;

    case L'm': // function
      rtrn_type = FUNC_TYPE;
      break;

    default:
      wcerr << L">>> unknown type <<<" << endl;
      exit(1);
      break;
    }

    StackMethod* mthd = new StackMethod(id, name, is_virtual, has_and_or, is_lambda, dclrs,
          num_dclrs, params, mem_size, rtrn_type, cls);    
    // load statements
#ifdef _DEBUG
    wcout << L"Method(" << mthd << L"): id=" << id << L"; name='" << name << L"'; return='" 
    << rtrn_name << L"'; params=" << params << L"; bytes=" 
    << mem_size << endl;
#endif    
    LoadStatements(mthd, is_debug);

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
    // note: method ID is position dependent
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
  method->SetInstructions(mthd_instrs, (int)instrs.size());
}

void Loader::LoadStatements(StackMethod* method, bool is_debug)
{
  int line_num = -1;
  const uint32_t num_instrs = ReadUnsigned();
  StackInstr** mthd_instrs = new StackInstr*[num_instrs];

  for(uint32_t i = 0; i < num_instrs; ++i) {
    const int type = ReadByte();
    if(is_debug) {
      line_num = ReadInt();
    }

    switch(type) {
    case LOAD_INT_LIT:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_INT_LIT, (long)ReadInt());
      break;

    case LOAD_CHAR_LIT:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_CHAR_LIT, (long)ReadChar());
      break;

    case SHL_INT:
      mthd_instrs[i] = new StackInstr(line_num, SHL_INT);
      break;

    case SHR_INT:
      mthd_instrs[i] = new StackInstr(line_num, SHR_INT);
      break;

    case LOAD_INT_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, 
              mem_context == LOCL ? LOAD_LOCL_INT_VAR : LOAD_CLS_INST_INT_VAR, 
              id, mem_context);
    }
      break;

    case LOAD_FUNC_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FUNC_VAR, id, mem_context);
    }
      break;

    case LOAD_FLOAT_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FLOAT_VAR, id, mem_context);
    }
      break;

    case STOR_INT_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, 
              mem_context == LOCL ? STOR_LOCL_INT_VAR : STOR_CLS_INST_INT_VAR, 
              id, mem_context);
    }
      break;

    case STOR_FUNC_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_FUNC_VAR, id, mem_context);
    }
      break;

    case STOR_FLOAT_VAR: {
      const long id = ReadInt();
      const long mem_context = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_FLOAT_VAR, id, mem_context);
    }
      break;

    case COPY_INT_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, 
              mem_context == LOCL ? COPY_LOCL_INT_VAR : COPY_CLS_INST_INT_VAR, 
              id, mem_context);
    }
      break;

    case COPY_FLOAT_VAR: {
      const long id = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, COPY_FLOAT_VAR, id, mem_context);
    }
      break;

    case LOAD_BYTE_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_BYTE_ARY_ELM, dim, mem_context);
    }
      break;

    case LOAD_CHAR_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_CHAR_ARY_ELM, dim, mem_context);
    }
      break;
      
    case LOAD_INT_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_INT_ARY_ELM, dim, mem_context);
    }
      break;

    case LOAD_FLOAT_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FLOAT_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_BYTE_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_BYTE_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_CHAR_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_CHAR_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_INT_ARY_ELM: {
      const long dim = ReadInt();
      MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_INT_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_FLOAT_ARY_ELM: {
      const long dim = ReadInt();
      const long mem_context = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_FLOAT_ARY_ELM, dim, mem_context);
    }
      break;

    case NEW_FLOAT_ARY: {
      const long dim = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, NEW_FLOAT_ARY, dim);
    }
      break;

    case NEW_INT_ARY: {
      const long dim = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, NEW_INT_ARY, dim);
    }
      break;

    case NEW_BYTE_ARY: {
      const long dim = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, NEW_BYTE_ARY, dim);
    }
      break;

    case NEW_CHAR_ARY: {
      const long dim = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, NEW_CHAR_ARY, dim);
    }
      break;

    case NEW_OBJ_INST: {
      const long obj_id = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, NEW_OBJ_INST, obj_id);
    }
      break;

    case NEW_FUNC_INST: {
      const long mem_size = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, NEW_FUNC_INST, mem_size);
    }
      break;

    case DYN_MTHD_CALL: {
      const long num_params = ReadInt();
      const long rtrn_type = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, DYN_MTHD_CALL, num_params, rtrn_type);
    }
      break;

    case MTHD_CALL: {
      const long cls_id = ReadInt();
      const long mthd_id = ReadInt();
      const long is_native = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, MTHD_CALL, cls_id, mthd_id, is_native);
    }
      break;

    case ASYNC_MTHD_CALL: {
      const long cls_id = ReadInt();
      const long mthd_id = ReadInt();
      const long is_native = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, ASYNC_MTHD_CALL, cls_id, mthd_id, is_native);
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
      const long label = ReadInt();
      const long cond = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, JMP, label, cond);
    }
      break;

    case LBL: {
      const long id = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LBL, id);
    }
      break;

    case OBJ_INST_CAST: {
      const long to = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, OBJ_INST_CAST, to);
    }
      break;

    case OBJ_TYPE_OF: {
      const long check = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, OBJ_TYPE_OF, check);
    }
      break;

    case OR_INT:
      mthd_instrs[i] = new StackInstr(line_num, OR_INT);
      break;

    case AND_INT:
      mthd_instrs[i] = new StackInstr(line_num, AND_INT);
      break;

    case ADD_INT:
      mthd_instrs[i] = new StackInstr(line_num, ADD_INT);
      break;

    case CEIL_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, CEIL_FLOAT);
      break;

    case CPY_BYTE_ARY:
      mthd_instrs[i] = new StackInstr(line_num, CPY_BYTE_ARY);
      break;
      
    case CPY_CHAR_ARY:
      mthd_instrs[i] = new StackInstr(line_num, CPY_CHAR_ARY);
      break;
      
    case CPY_INT_ARY:
      mthd_instrs[i] = new StackInstr(line_num, CPY_INT_ARY);
      break;

    case CPY_FLOAT_ARY:
      mthd_instrs[i] = new StackInstr(line_num, CPY_FLOAT_ARY);
      break;

    case FLOR_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, FLOR_FLOAT);
      break;

    case SIN_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, SIN_FLOAT);
      break;

    case COS_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, COS_FLOAT);
      break;

    case TAN_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, TAN_FLOAT);
      break;

    case ASIN_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, ASIN_FLOAT);
      break;

    case ACOS_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, ACOS_FLOAT);
      break;

    case ATAN_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, ATAN_FLOAT);
      break;

    case ATAN2_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, ATAN2_FLOAT);
      break;

    case LOG_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, LOG_FLOAT);
      break;

    case POW_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, POW_FLOAT);
      break;

    case SQRT_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, SQRT_FLOAT);
      break;

    case RAND_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, RAND_FLOAT);
      break;

    case F2I:
      mthd_instrs[i] = new StackInstr(line_num, F2I);
      break;

    case I2F:
      mthd_instrs[i] = new StackInstr(line_num, I2F);
      break;

    case S2I:
      mthd_instrs[i] = new StackInstr(line_num, S2I);
      break;

    case S2F:
      mthd_instrs[i] = new StackInstr(line_num, S2F);
      break;

    case I2S:
      mthd_instrs[i] = new StackInstr(line_num, I2S);
      break;
      
    case F2S:
      mthd_instrs[i] = new StackInstr(line_num, F2S);
      break;

    case F2S_FORMAT:
      mthd_instrs[i] = new StackInstr(line_num, F2S_FORMAT);
      break;
      
    case SWAP_INT:
      mthd_instrs[i] = new StackInstr(line_num, SWAP_INT);
      break;

    case POP_INT:
      mthd_instrs[i] = new StackInstr(line_num, POP_INT);
      break;

    case POP_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, POP_FLOAT);
      break;

    case LOAD_CLS_MEM:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_CLS_MEM);
      break;

    case LOAD_INST_MEM:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_INST_MEM);
      break;

    case LOAD_ARY_SIZE:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_ARY_SIZE);
      break;

    case SUB_INT:
      mthd_instrs[i] = new StackInstr(line_num, SUB_INT);
      break;

    case MUL_INT:
      mthd_instrs[i] = new StackInstr(line_num, MUL_INT);
      break;

    case DIV_INT:
      mthd_instrs[i] = new StackInstr(line_num, DIV_INT);
      break;

    case MOD_INT:
      mthd_instrs[i] = new StackInstr(line_num, MOD_INT);
      break;

    case BIT_AND_INT:
      mthd_instrs[i] = new StackInstr(line_num, BIT_AND_INT);
      break;

    case BIT_OR_INT:
      mthd_instrs[i] = new StackInstr(line_num, BIT_OR_INT);
      break;

    case BIT_XOR_INT:
      mthd_instrs[i] = new StackInstr(line_num, BIT_XOR_INT);
      break;

    case EQL_INT:
      mthd_instrs[i] = new StackInstr(line_num, EQL_INT);
      break;

    case NEQL_INT:
      mthd_instrs[i] = new StackInstr(line_num, NEQL_INT);
      break;

    case LES_INT:
      mthd_instrs[i] = new StackInstr(line_num, LES_INT);
      break;

    case GTR_INT:
      mthd_instrs[i] = new StackInstr(line_num, GTR_INT);
      break;

    case LES_EQL_INT:
      mthd_instrs[i] = new StackInstr(line_num, LES_EQL_INT);
      break;

    case LES_EQL_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, LES_EQL_FLOAT);
      break;

    case GTR_EQL_INT:
      mthd_instrs[i] = new StackInstr(line_num, GTR_EQL_INT);
      break;

    case GTR_EQL_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, GTR_EQL_FLOAT);
      break;

    case ADD_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, ADD_FLOAT);
      break;

    case SUB_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, SUB_FLOAT);
      break;

    case MUL_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, MUL_FLOAT);
      break;

    case DIV_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, DIV_FLOAT);
      break;

    case EQL_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, EQL_FLOAT);
      break;

    case NEQL_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, NEQL_FLOAT);
      break;

    case LES_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, LES_FLOAT);
      break;

    case GTR_FLOAT:
      mthd_instrs[i] = new StackInstr(line_num, GTR_FLOAT);
      break;

    case LOAD_FLOAT_LIT:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FLOAT_LIT,
              ReadDouble());
      break;

    case RTRN:
      if(is_debug) {
        mthd_instrs[i] = new StackInstr(line_num + 1, RTRN);
      }
      else {
        mthd_instrs[i] = new StackInstr(line_num, RTRN);
      }
      break;

    case DLL_LOAD:
      mthd_instrs[i] = new StackInstr(line_num, DLL_LOAD);
      break;

    case DLL_UNLOAD:
      mthd_instrs[i] = new StackInstr(line_num, DLL_UNLOAD);
      break;

    case DLL_FUNC_CALL:
      mthd_instrs[i] = new StackInstr(line_num, DLL_FUNC_CALL);
      break;

    case THREAD_JOIN:
      mthd_instrs[i] = new StackInstr(line_num, THREAD_JOIN);
      break;

    case THREAD_SLEEP:
      mthd_instrs[i] = new StackInstr(line_num, THREAD_SLEEP);
      break;

    case THREAD_MUTEX:
      mthd_instrs[i] = new StackInstr(line_num, THREAD_MUTEX);
      break;

    case CRITICAL_START:
      mthd_instrs[i] = new StackInstr(line_num, CRITICAL_START);
      break;

    case CRITICAL_END:
      mthd_instrs[i] = new StackInstr(line_num, CRITICAL_END);
      break;

    case TRAP: {
      const long args = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, TRAP, args);
    }
      break;

    case TRAP_RTRN: {
      const long args = ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, TRAP_RTRN, args);
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
  }

  // copy and set instructions
  method->SetInstructions(mthd_instrs, num_instrs);
}
