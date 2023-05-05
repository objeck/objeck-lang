/***************************************************************************
 * Program loader.
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
  std::ifstream in("obr.conf");
  if(in.good()) {
    std::string line;
    do {
      getline(in, line);
      size_t pos = line.find('=');
      if(pos != std::string::npos) {
        std::string name = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        params.insert(std::pair<const std::wstring, int>(BytesToUnicode(name), atoi(value.c_str())));
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
    std::wcerr << L"This executable appears to be invalid or compiled with a different version of the toolchain." << std::endl;
    exit(1);
  } 

  const int magic_num = ReadInt();
  switch(magic_num) {
  case MAGIC_NUM_LIB:
    std::wcerr << L"Unable to use execute shared library '" << filename << L"'." << std::endl;
    exit(1);

  case MAGIC_NUM_EXE:
    break;

  default:
    std::wcerr << L"Unknown file type for '" << filename << L"'." << std::endl;
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
    std::wcout << L"Loaded static float std::string[" << i << L"]: '";
#endif
    for(int j = 0; j < float_string_length; j++) {
      float_string[j] = ReadDouble();
#ifdef _DEBUG
      std::wcout << float_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    std::wcout << L"'" << std::endl;
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
    std::wcout << L"Loaded static int std::string[" << i << L"]: '";
#endif
    for(int j = 0; j < int_string_length; ++j) {
      int_string[j] = ReadInt();
#ifdef _DEBUG
      std::wcout << int_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    std::wcout << L"'" << std::endl;
#endif
    int_strings[i] = int_string;
  }
  program->SetIntStrings(int_strings, num_int_strings);
  
  // read char strings
  num_char_strings = ReadInt();
  wchar_t** char_strings = new wchar_t*[num_char_strings + arguments.size()];
  for(i = 0; i < num_char_strings; ++i) {
    const std::wstring value = ReadString();
    wchar_t* char_string = new wchar_t[value.size() + 1];
    // copy string
#ifdef _WIN32
    wcscpy_s(char_string, value.size() + 1, value.c_str());
#else
    wcscpy(char_string, value.c_str());
#endif

#ifdef _DEBUG
    std::wcout << L"Loaded static character std::string[" << i << L"]: '" << char_string << L"'" << std::endl;
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
    std::wcout << L"Loaded static std::string: '" << char_strings[i] << L"'" << std::endl;
#endif
  }
  program->SetCharStrings(char_strings, num_char_strings);

#ifdef _DEBUG
  std::wcout << L"=======================================" << std::endl;
#endif
  
  // read start class and method ids
  start_class_id = ReadInt();
  start_method_id = ReadInt();
#ifdef _DEBUG
  std::wcout << L"Program starting point: " << start_class_id << L"," << start_method_id << std::endl;
#endif

  LoadClasses();
  
  const std::wstring name = L"$Initialization$:";
  StackDclr** dclrs = new StackDclr*[1];
  dclrs[0] = new StackDclr;
  dclrs[0]->name = L"args";
  dclrs[0]->type = OBJ_ARY_PARM;

  init_method = new StackMethod(-1, name, false, false, false, dclrs,  1, 0, 1, NIL_TYPE, nullptr);
  LoadInitializationCode(init_method);
  program->SetInitializationMethod(init_method);
  program->SetStringObjectId(string_cls_id);
}

char* Loader::LoadFileBuffer(std::wstring filename, size_t& buffer_size)
{
  char* buffer;
  const std::string open_filename = UnicodeToBytes(filename);

  std::ifstream in(open_filename.c_str(), std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
  if(in.good()) {
    // get file size
    in.seekg(0, std::ios::end);
    buffer_size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    in.read(buffer, buffer_size);
    // close file
    in.close();

    uLong dest_len;
    char* out = OutputStream::UncompressZlib(buffer, (uLong)buffer_size, dest_len);
    if(!out) {
      std::wcerr << L"Unable to uncompress file: " << filename << std::endl;
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
    std::wcerr << L"Unable to open file: " << filename << std::endl;
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
  std::wcout << L"Reading " << num_classes << L" classe(s)..." << std::endl;
#endif

  for(int i = 0; i < num_classes; ++i) {
    // read id and pid
    const int id = ReadInt();
    std::wstring name = ReadString();
    const int pid = ReadInt();
    std::wstring parent_name = ReadString();

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
    std::wstring file_name;
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
    std::map<int, std::pair<int, StackDclr**> > closure_dclr_map;
    const int num_closure_dclrs = ReadInt();
    for(int i = 0; i < num_closure_dclrs; ++i) {
      const int closure_mthd_id = ReadInt();
      // read closure declarations
      const int closure_num_dclrs = ReadInt();
      StackDclr** closure_dclrs = LoadDeclarations(closure_num_dclrs, is_debug);
      // add declarations to map
      closure_dclr_map[closure_mthd_id] = std::pair<int, StackDclr**>(closure_num_dclrs, closure_dclrs);
    }

    cls_hierarchy[id] = pid;
    StackClass* cls = new StackClass(id, name, file_name, pid, is_virtual, cls_dclrs, cls_num_dclrs, inst_dclrs, 
                                     closure_dclr_map, inst_num_dclrs, cls_space, inst_space, is_debug);

#ifdef _DEBUG
    std::wcout << L"Class(" << cls << L"): id=" << id << L"; name='" << name << L"'; parent='"
    << parent_name << L"'; class_bytes=" << cls_space << L"'; instance_bytes="
    << inst_space << std::endl;
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
    std::wstring name;
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
  std::wcout << L"Reading " << number << L" method(s)..." << std::endl;
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
    const std::wstring name = ReadString();
    // return
    const std::wstring rtrn_name = ReadString();
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
      std::wstring name;
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
      std::wcerr << L">>> unknown type: " << rtrn_name[0] << L"(" << (int)rtrn_name[0] << L") <<" << std::endl;
      exit(1);
      break;
    }

    StackMethod* mthd = new StackMethod(id, name, is_virtual, has_and_or, is_lambda, dclrs,
          num_dclrs, params, mem_size, rtrn_type, cls);    
    // load statements
#ifdef _DEBUG
    std::wcout << L"Method(" << mthd << L"): id=" << id << L"; name='" << name << L"'; return='" 
    << rtrn_name << L"'; params=" << params << L"; bytes=" 
    << mem_size << std::endl;
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
  std::vector<StackInstr*> instrs;

  instrs.push_back(new StackInstr(-1, arguments.size()));
  instrs.push_back(new StackInstr(-1, NEW_INT_ARY, (long)1));
  instrs.push_back(new StackInstr(-1, STOR_LOCL_INT_VAR, 0L, LOCL));

  for(size_t i = 0; i < arguments.size(); ++i) {
    instrs.push_back(new StackInstr(-1, arguments[i].size()));
    instrs.push_back(new StackInstr(-1, NEW_CHAR_ARY, 1L));
    instrs.push_back(new StackInstr(-1, (long)(num_char_strings + i)));
    instrs.push_back(new StackInstr(-1, (long)instructions::CPY_CHAR_STR_ARY));
    instrs.push_back(new StackInstr(-1, TRAP_RTRN, 3L));

    instrs.push_back(new StackInstr(-1, NEW_OBJ_INST, (long)string_cls_id));
    // note: method ID is position dependent
    instrs.push_back(new StackInstr(-1, MTHD_CALL, (long)string_cls_id, 2L, 0L));

    instrs.push_back(new StackInstr(-1, i));
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
  const unsigned long num_instrs = ReadUnsigned();
  StackInstr** mthd_instrs = new StackInstr*[num_instrs];

  for(unsigned long i = 0; i < num_instrs; ++i) {
    const int type = ReadByte();
    if(is_debug) {
      line_num = ReadInt();
    }

    switch(type) {
    case LOAD_INT_LIT: {
      const INT64_VALUE value = ReadInt64();
      mthd_instrs[i] = new StackInstr(line_num, value);
    }
      break;

    case LOAD_CHAR_LIT:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_CHAR_LIT, (long)ReadChar());
      break;

    case LOAD_INST_MEM:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_INST_MEM);
      break;

    case LOAD_CLS_MEM:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_CLS_MEM);
      break;

    case LOAD_INT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, mem_context == LOCL ? LOAD_LOCL_INT_VAR : LOAD_CLS_INST_INT_VAR, id, mem_context);
    }
      break;

    case LOAD_FUNC_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FUNC_VAR, id, mem_context);
    }
      break;

    case LOAD_FLOAT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FLOAT_VAR, id, mem_context);
    }
      break;

    case STOR_INT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, mem_context == LOCL ? STOR_LOCL_INT_VAR : STOR_CLS_INST_INT_VAR, id, mem_context);
    }
      break;

    case STOR_FUNC_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
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
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, mem_context == LOCL ? COPY_LOCL_INT_VAR : COPY_CLS_INST_INT_VAR, id, mem_context);
    }
      break;

    case COPY_FLOAT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, COPY_FLOAT_VAR, id, mem_context);
    }
      break;

    case LOAD_BYTE_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_BYTE_ARY_ELM, dim, mem_context);
    }
      break;

    case LOAD_CHAR_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_CHAR_ARY_ELM, dim, mem_context);
    }
      break;
      
    case LOAD_INT_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_INT_ARY_ELM, dim, mem_context);
    }
      break;

    case LOAD_FLOAT_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FLOAT_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_BYTE_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_BYTE_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_CHAR_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      mthd_instrs[i] = new StackInstr(line_num, STOR_CHAR_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_INT_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
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

    case LOAD_FLOAT_LIT:
      mthd_instrs[i] = new StackInstr(line_num, LOAD_FLOAT_LIT, ReadDouble());
      break;

    case RTRN:
      if(is_debug) {
        mthd_instrs[i] = new StackInstr(line_num + 1, RTRN);
      }
      else {
        mthd_instrs[i] = new StackInstr(line_num, RTRN);
    }
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

      //
      // Start: instruction caching
      //

    case SHL_INT:
    case SHR_INT:
    case BIT_AND_INT:
    case BIT_OR_INT:
    case BIT_XOR_INT:
    case OR_INT:
    case AND_INT:
    case SWAP_INT:
    case ADD_INT:
    case SUB_INT:
    case MUL_INT:
    case DIV_INT:
    case MOD_INT:
    case EQL_INT:
    case NEQL_INT:
    case LES_INT:
    case GTR_INT:
    case LES_EQL_INT:
    case GTR_EQL_INT:
    case CEIL_FLOAT:
    case FLOR_FLOAT:
    case SIN_FLOAT:
    case COS_FLOAT:
    case TAN_FLOAT:
    case SQRT_FLOAT:
    case RAND_FLOAT:
    case ADD_FLOAT:
    case SUB_FLOAT:
    case MUL_FLOAT:
    case DIV_FLOAT:
    case MOD_FLOAT:
    case EQL_FLOAT:
    case NEQL_FLOAT:
    case LES_EQL_FLOAT:
    case GTR_EQL_FLOAT:
    case LES_FLOAT:
    case GTR_FLOAT:
    case ASIN_FLOAT:
    case ACOS_FLOAT:
    case ATAN_FLOAT:
    case ATAN2_FLOAT:
    case ACOSH_FLOAT:
    case ASINH_FLOAT:
    case ATANH_FLOAT:
    case LOG_FLOAT:
    case ROUND_FLOAT:
    case EXP_FLOAT:
    case LOG10_FLOAT:
    case POW_FLOAT:
    case GAMMA_FLOAT:
    case F2I:
    case I2F:
    case S2I:
    case S2F:
    case I2S:
    case F2S:
    case LOAD_ARY_SIZE:
    case EXT_LIB_LOAD:
    case EXT_LIB_UNLOAD:
    case EXT_LIB_FUNC_CALL:
    case THREAD_JOIN:
    case THREAD_SLEEP:
    case THREAD_MUTEX:
    case CRITICAL_START:
    case CRITICAL_END:
    case CPY_BYTE_ARY:
    case CPY_CHAR_ARY:
    case CPY_INT_ARY:
    case CPY_FLOAT_ARY:
    case POP_INT:
    case POP_FLOAT:
    case ZERO_BYTE_ARY:
    case ZERO_CHAR_ARY:
    case ZERO_INT_ARY:
    case ZERO_FLOAT_ARY:
      mthd_instrs[i] = cached_instrs[type];
      break;

      //
      // End: instruction caching
      //

    case LIB_OBJ_INST_CAST:
      std::wcerr << L">>> unsupported instruction for executable: LIB_OBJ_INST_CAST <<<" << std::endl;
      exit(1);

    case LIB_NEW_OBJ_INST:
      std::wcerr << L">>> unsupported instruction for executable: LIB_NEW_OBJ_INST <<<" << std::endl;
      exit(1);

    case LIB_MTHD_CALL:
      std::wcerr << L">>> unsupported instruction for executable: LIB_MTHD_CALL <<<" << std::endl;
      exit(1);

    default: {
#ifdef _DEBUG
      InstructionType instr = (InstructionType)type;
      std::wcout << L">>> unknown instruction: id=" << instr << L" <<<" << std::endl;
#endif
      exit(1);
    }
      break;
    }
  }

  // copy and set instructions
  method->SetInstructions(mthd_instrs, num_instrs);
}
