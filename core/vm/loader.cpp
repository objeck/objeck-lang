/***************************************************************************
 * Program loader.
 *
 * Copyright (c) 2025, Randy Hollines
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

// the trap ids a program may use (with the verification code below)
static bool IsTrapId(const INT64_VALUE id);

StackProgram* Loader::GetProgram() {
  return program;
}

void Loader::LoadConfiguration()
{
  std::ifstream in("obr.conf");
  if(in.good()) {
    std::string line;
    do {
      std::getline(in, line);
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

bool Loader::Load()
{
  error.str(L"");
  error.clear();
  try {
    if(!from_mem && !alloc_buffer) {
      ReadFile();
    }
    LoadImage();
  }
  catch(const LoadError&) {
    return false;
  }

  return true;
}

void Loader::LoadImage()
{
  // An .obe is tied to the toolchain that compiled it: the VM's traps, call
  // stack and bytecode change between versions with no versioned format of
  // their own, so a program compiled by another version is refused here,
  // before a byte of it is interpreted. The message names both versions and
  // says which side to act on, as the linker's library check does; the old
  // text ("appears to be invalid or compiled with an incompatible version")
  // told the reader neither.
  const int ver_num = static_cast<int>(ReadInt());
  if(ver_num != VER_NUM) {
    error << L"Toolchain version mismatch reading " << filename << std::endl
               << L"\tthe program was compiled by toolchain " << ver_num << std::endl
               << L"\tthis runtime is                       " << VER_NUM << std::endl;
    if(ver_num > VER_NUM) {
      error << L"\t-> this runtime is older than the program. Upgrade the runtime, or recompile" << std::endl
                 << L"\t   the program's source with this toolchain's obc." << std::endl;
    }
    else {
      error << L"\t-> the program is older than this runtime. Recompile its source with this" << std::endl
                 << L"\t   toolchain's obc (an .obe does not run across toolchain versions)." << std::endl;
    }
    LoadFailed();
  }

  const int magic_num = static_cast<int>(ReadInt());
  switch(magic_num) {
  case MAGIC_NUM_LIB:
  case MAGIC_NUM_LIB_UNQUALIFIED_ENUMS:
  case MAGIC_NUM_LIB_NO_GENERIC_MARKS:
  case MAGIC_NUM_LIB_NO_PARAM_NAMES:
    error << L"Cannot run '" << filename << L"': it is a library (.obl), not a program (.obe)." << std::endl
               << L"\tLink it into a program with 'obc -lib' and run that program's .obe." << std::endl;
    LoadFailed();

  case MAGIC_NUM_EXE:
    break;

  default:
    error << L"Unknown file type for '" << filename << L"'." << std::endl;
    LoadFailed();
  }

  // read string id
  string_cls_id = static_cast<int>(ReadInt());

  int i;

  //
  // read float strings
  //
  num_float_strings = ReadCount();
  // Each table is zeroed and handed to the program before it is filled, so a
  // load that stops part way leaves ~StackProgram what was read (no leak).
  FLOAT_VALUE** float_strings = new FLOAT_VALUE*[num_float_strings]();
  program->SetFloatStrings(float_strings, num_float_strings);
  for(i = 0; i < num_float_strings; ++i) {
    const int float_string_length = ReadCount();
    FLOAT_VALUE* float_string = float_strings[i] = new FLOAT_VALUE[float_string_length];
    // copy string    
#ifdef _DEBUG
    std::wcout << L"Loaded static float string[" << i << L"]: '";
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
  }

  //
  // read boolean strings
  //
  num_bool_strings = ReadCount();
  bool** bool_strings = new bool* [num_bool_strings]();
  program->SetBoolStrings(bool_strings, num_bool_strings);
  for(i = 0; i < num_bool_strings; ++i) {
    const int bool_string_length = ReadCount();
    bool* bool_string = bool_strings[i] = new bool[bool_string_length];
    // copy string    
#ifdef _DEBUG
    std::wcout << L"Loaded static bool string[" << i << L"]: '";
#endif
    for(int j = 0; j < bool_string_length; j++) {
      bool_string[j] = ReadByte() ? true : false;
#ifdef _DEBUG
      std::wcout << bool_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    std::wcout << L"'" << std::endl;
#endif
  }


  //
  // read byte strings
  //
  num_byte_strings = ReadCount();
  char** byte_strings = new char*[num_byte_strings]();
  program->SetByteStrings(byte_strings, num_byte_strings);
  for(i = 0; i < num_byte_strings; ++i) {
    const int byte_string_length = ReadCount();
    char* byte_string = byte_strings[i] = new char[byte_string_length];
    // copy string    
#ifdef _DEBUG
    std::wcout << L"Loaded static byte string[" << i << L"]: '";
#endif
    for(int j = 0; j < byte_string_length; j++) {
      byte_string[j] = ReadByte();
#ifdef _DEBUG
      std::wcout << byte_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    std::wcout << L"'" << std::endl;
#endif
  }

  //
  // read int strings
  //
  num_int_strings = ReadCount();
  INT64_VALUE** int_strings = new INT64_VALUE *[num_int_strings]();
  program->SetIntStrings(int_strings, num_int_strings);
  for(i = 0; i < num_int_strings; ++i) {
    const int int_string_length = ReadCount();
    INT64_VALUE* int_string = int_strings[i] = new INT64_VALUE[int_string_length];
    // copy string    
#ifdef _DEBUG
    std::wcout << L"Loaded static int string[" << i << L"]: '";
#endif
    for(int j = 0; j < int_string_length; ++j) {
      int_string[j] = ReadInt64();
#ifdef _DEBUG
      std::wcout << int_string[j] << L",";
#endif
    }
#ifdef _DEBUG
    std::wcout << L"'" << std::endl;
#endif
  }
  
  //
  // read char strings
  //
  num_char_strings = ReadCount();
  // the program's arguments follow its own strings, and the table counts them
  // too: they were copied with wcsdup and never freed
  const int num_all_char_strings = num_char_strings + (int)arguments.size();
  wchar_t** char_strings = new wchar_t*[num_all_char_strings]();
  program->SetCharStrings(char_strings, num_all_char_strings);
  for(i = 0; i < num_char_strings; ++i) {
    const std::wstring value = ReadString();
    wchar_t* char_string = char_strings[i] = new wchar_t[value.size() + 1];
    // copy string
#ifdef _WIN32
    wcscpy_s(char_string, value.size() + 1, value.c_str());
#else
    wcscpy(char_string, value.c_str());
#endif

#ifdef _DEBUG
    std::wcout << L"Loaded static character string[" << i << L"]: '" << char_string << L"'" << std::endl;
#endif
  }

  // copy command line params
  for(size_t j = 0; j < arguments.size(); ++i, ++j) {
    const std::wstring& value = arguments[j];
    char_strings[i] = new wchar_t[value.size() + 1];
    std::copy(value.c_str(), value.c_str() + value.size() + 1, char_strings[i]);

#ifdef _DEBUG
    std::wcout << L"Loaded static std::string: '" << char_strings[i] << L"'" << std::endl;
#endif
  }

#ifdef _DEBUG
  std::wcout << L"=======================================" << std::endl;
#endif
  
  // read start class and method ids
  start_class_id = static_cast<int>(ReadInt());
  start_method_id = static_cast<int>(ReadInt());
#ifdef _DEBUG
  std::wcout << L"Program starting point: " << start_class_id << L"," << start_method_id << std::endl;
#endif

  LoadClasses();
  Verify();
  
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

char* Loader::DecodeImage(const char* data, size_t size, size_t& image_size)
{
  image_size = 0;

  // new format: [uncmp_size:4][zlib...]; old format: raw zlib (CMF byte = 0x78)
  // When size LSB == 0x78, bytes[4..5] disambiguate: compress2(Z_BEST_COMPRESSION)
  // always emits 0x78 0xDA; check (CMF*256+FLG)%31==0 to confirm valid zlib header.
  uint32_t uncmp_hint = 0;
  const char* src;
  uLong src_size;
  const bool zlib_at_4 = size >= 6 &&
                         (uint8_t)data[4] == 0x78 &&
                         (0x78u * 256u + (uint8_t)data[5]) % 31u == 0u;
  if(size > 0 && (uint8_t)data[0] == 0x78 && !zlib_at_4) {
    src = data;
    src_size = (uLong)size;
  }
  else {
    // A file shorter than the size header (an empty or truncated .obe) used to
    // be read four bytes deep regardless, and its zlib length underflowed to
    // nearly 2^64, which sent inflate far past the end of the buffer.
    if(size < sizeof(uncmp_hint)) {
      return nullptr;
    }
    memcpy(&uncmp_hint, data, sizeof(uncmp_hint));
    src = data + sizeof(uncmp_hint);
    src_size = (uLong)(size - sizeof(uncmp_hint));
  }

  uLong dest_len;
  char* out = OutputStream::UncompressZlib(src, src_size, dest_len, uncmp_hint);
  if(!out) {
    return nullptr;
  }
#ifdef _DEBUG
  std::wcout << L"--- file in: compressed=" << size << L", uncompressed=" << dest_len << L" ---" << std::endl;
#endif

  image_size = (size_t)dest_len;
  return out;
}

char* Loader::LoadFileBuffer(std::wstring filename, size_t& buffer_size)
{
  char* buffer;
  std::ifstream in(ToFsPath(filename).c_str(), std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
  if(in.good()) {
    // get file size
    in.seekg(0, std::ios::end);
    buffer_size = (size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    buffer = (char*)calloc(buffer_size + 1, sizeof(char));
    if(!buffer) {
      in.close();
      error << L"Unable to allocate memory for file: '" << filename << L"'" << std::endl;
      LoadFailed();
    }
    in.read(buffer, buffer_size);
    // close file
    in.close();

    size_t image_size = 0;
    char* out = DecodeImage(buffer, buffer_size, image_size);
    free(buffer);
    buffer = nullptr;
    if(!out) {
      error << L"Unable to uncompress file: " << filename << std::endl;
      LoadFailed();
    }

    // Report the DECOMPRESSED length to the caller so every subsequent Read* can
    // be bounds-checked. Previously buffer_size kept the (smaller) compressed
    // size, which made bounds checking impossible.
    buffer_size = image_size;
    return out;
  }
  else {
    error << L"Unable to open file: '" << filename << L"'" << std::endl;
    LoadFailed();
  }

  return nullptr;
}

// Frees a declaration table built by LoadDeclarations; null entries and a null
// table are fine.
static void DeleteDeclarations(StackDclr** dclrs, const int num_dclrs)
{
  if(dclrs) {
    for(int i = 0; i < num_dclrs; ++i) {
      delete dclrs[i];
    }
    delete[] dclrs;
  }
}

// What LoadClasses has read so far. The arrays reach the program only after
// the last class, because SetClasses reads every entry; until then this owns
// them, so a load that stops part way frees what it read rather than leaking
// it (a refused .obe in an embedding host, and the fuzz target, which loads
// millions of them in one process).
struct PartialClasses {
  int num;
  long* hierarchy;
  long** interfaces;
  StackClass** classes;

  ~PartialClasses() {
    if(classes) {
      for(int i = 0; i < num; ++i) {
        delete classes[i];
      }
      delete[] classes;
    }
    if(interfaces) {
      for(int i = 0; i < num; ++i) {
        delete[] interfaces[i];
      }
      delete[] interfaces;
    }
    delete[] hierarchy;
  }
};

// The declaration tables of a class not yet constructed; freed unless released.
struct PartialClassDeclarations {
  StackDclr** cls_dclrs = nullptr;
  int cls_num_dclrs = 0;
  StackDclr** inst_dclrs = nullptr;
  int inst_num_dclrs = 0;
  std::map<int, std::pair<int, StackDclr**> > closures;

  ~PartialClassDeclarations() {
    DeleteDeclarations(cls_dclrs, cls_num_dclrs);
    DeleteDeclarations(inst_dclrs, inst_num_dclrs);
    for(auto& closure : closures) {
      DeleteDeclarations(closure.second.second, closure.second.first);
    }
  }
};

void Loader::LoadClasses()
{
  // ReadCount: a count no larger than the bytes left. A plain ReadInt let a
  // crafted count size these arrays at up to 16 GB (found by the fuzz target).
  const int num_classes = ReadCount();
  // Zeroed, and every id may be stored once: a repeated id used to leave
  // another slot holding whatever the allocator returned, which SetClasses and
  // ~StackProgram then dereferenced (found by the fuzz target).
  PartialClasses loaded;
  loaded.num = num_classes;
  loaded.hierarchy = new long[num_classes]();
  loaded.interfaces = new long*[num_classes]();
  loaded.classes = new StackClass*[num_classes]();
  long* cls_hierarchy = loaded.hierarchy;
  long** cls_interfaces = loaded.interfaces;
  StackClass** classes = loaded.classes;
  class_spaces.assign(num_classes, 0);
  verify.max_called.assign(num_classes, -1);
  verify.called_where.assign(num_classes, std::pair<StackMethod*, long>(nullptr, -1));
  verify.inst_end = 0;
  verify.inst_where = nullptr;
  verify.inst_ip = -1;

#ifdef _DEBUG
  std::wcout << L"Reading " << num_classes << L" classe(s)..." << std::endl;
#endif

  for(int i = 0; i < num_classes; ++i) {
    // read id and pid
    const int id = static_cast<int>(ReadInt());
    // Validate before any classes[id]/cls_interfaces[id]/cls_hierarchy[id] store:
    // a crafted .obe could otherwise drive an arbitrary-offset heap write.
    if(id < 0 || id >= num_classes) {
      error << L">>> Corrupt bytecode: class id out of range <<<" << std::endl;
      LoadFailed();
    }
    if(classes[id]) {
      error << L">>> Corrupt bytecode: duplicate class id <<<" << std::endl;
      LoadFailed();
    }
    std::wstring name = ReadString();
    const int pid = static_cast<int>(ReadInt());
    std::wstring parent_name = ReadString();

    // read interface ids: none when the count is not positive, as before, and
    // never more than the four-byte ids left in the buffer (a crafted count
    // sized this array at up to 16 GB; found by the fuzz target)
    const int num_interfaces = static_cast<int>(ReadInt());
    if(num_interfaces > 0 && buffer_end && (size_t)num_interfaces > (size_t)(buffer_end - buffer) / sizeof(int32_t)) {
      error << L">>> Corrupt bytecode: invalid interface count <<<" << std::endl;
      LoadFailed();
    }
    if(num_interfaces > 0) {
      long* interfaces = cls_interfaces[id] = new long[num_interfaces + 1];
      int j = 0;
      while(j < num_interfaces) {
        interfaces[j++] = ReadInt();
      }
      interfaces[j] = INF_ENDING;
    }

    const bool is_virtual = ReadByte() != 0;
    const bool is_debug = ReadByte() != 0;
    std::wstring file_name;
    if(is_debug) {
      file_name = ReadString();
    }

    // space
    const int cls_space = static_cast<int>(ReadInt());
    const int inst_space = static_cast<int>(ReadInt());
    class_spaces[id] = cls_space;

    PartialClassDeclarations dclrs;

    // read class declarations
    dclrs.cls_num_dclrs = ReadCount();
    dclrs.cls_dclrs = LoadDeclarations(dclrs.cls_num_dclrs, is_debug);

    // read instance declarations
    dclrs.inst_num_dclrs = ReadCount();
    dclrs.inst_dclrs = LoadDeclarations(dclrs.inst_num_dclrs, is_debug);

    // read closure declarations
    const int num_closure_dclrs = ReadCount();
    for(int j = 0; j < num_closure_dclrs; ++j) {
      const int closure_mthd_id = static_cast<int>(ReadInt());
      // read closure declarations
      const int closure_num_dclrs = ReadCount();
      StackDclr** closure_dclrs = LoadDeclarations(closure_num_dclrs, is_debug);
      // add declarations to map; a repeated id replaces the earlier entry, as
      // before, which is now freed rather than lost
      auto found = dclrs.closures.find(closure_mthd_id);
      if(found != dclrs.closures.end()) {
        DeleteDeclarations(found->second.second, found->second.first);
      }
      dclrs.closures[closure_mthd_id] = std::pair<int, StackDclr**>(closure_num_dclrs, closure_dclrs);
    }

    cls_hierarchy[id] = pid;
    StackClass* cls = new StackClass(id, name, file_name, pid, is_virtual, dclrs.cls_dclrs, dclrs.cls_num_dclrs,
                                     dclrs.inst_dclrs, dclrs.closures, dclrs.inst_num_dclrs, cls_space, inst_space,
                                     is_debug);
    // the class owns the declarations now
    dclrs.cls_dclrs = dclrs.inst_dclrs = nullptr;
    dclrs.closures.clear();
    // stored before its methods load, so a failure there frees it too
    classes[id] = cls;

#ifdef _DEBUG
    std::wcout << L"Class(" << cls << L"): id=" << id << L"; name='" << name << L"'; parent='"
    << parent_name << L"'; class_bytes=" << cls_space << L"'; instance_bytes="
    << inst_space << std::endl;
#endif

    // load methods
    LoadMethods(cls, is_debug);
  }

  // set class hierarchy and interfaces
  program->SetClasses(classes, num_classes);
  program->SetHierarchy(cls_hierarchy);
  program->SetInterfaces(cls_interfaces);
  loaded.classes = nullptr;
  loaded.interfaces = nullptr;
  loaded.hierarchy = nullptr;
}

StackDclr** Loader::LoadDeclarations(const int num_dclrs, const bool is_debug)
{
  StackDclr** dclrs = new StackDclr * [num_dclrs]();
  try {
    for(int j = 0; j < num_dclrs; ++j) {
      // set type: the collector walks frames and objects by these, so an
      // unknown one is refused (verification)
      int type = static_cast<int>(ReadInt());
      if(type < CHAR_PARM || type > FUNC_PARM) {
        error << L">>> Invalid bytecode: unknown declaration type " << type << L" <<<" << std::endl;
        LoadFailed();
      }
      // set name
      std::wstring name;
      if(is_debug) {
        name = ReadString();
      }
      dclrs[j] = new StackDclr;
      dclrs[j]->name = name;
      dclrs[j]->type = (ParamType)type;
    }
  }
  catch(...) {
    DeleteDeclarations(dclrs, num_dclrs);
    throw;
  }

  return dclrs;
}

void Loader::LoadMethods(StackClass* cls, bool is_debug)
{
  // bounded by the bytes left and zeroed, as LoadClasses does for classes
  const int number = ReadCount();
#ifdef _DEBUG
  std::wcout << L"Reading " << number << L" method(s)..." << std::endl;
#endif

  StackMethod** methods = new StackMethod*[number]();
  try {
    for(int i = 0; i < number; ++i) {
      // id
      const int id = static_cast<int>(ReadInt());
      // Validate before the methods[id] store (crafted .obe → arbitrary heap write).
      if(id < 0 || id >= number) {
        error << L">>> Corrupt bytecode: method id out of range <<<" << std::endl;
        LoadFailed();
      }
      if(methods[id]) {
        error << L">>> Corrupt bytecode: duplicate method id <<<" << std::endl;
        LoadFailed();
      }
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
      const int params = static_cast<int>(ReadInt());
      // space
      const int mem_size = static_cast<int>(ReadInt());
      // read type parameters
      const int num_dclrs = ReadCount();
      StackDclr** dclrs = LoadDeclarations(num_dclrs, is_debug);

      // parse return
      MemoryType rtrn_type = NIL_TYPE;
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
        DeleteDeclarations(dclrs, num_dclrs);
        error << L">>> unknown type: " << rtrn_name[0] << L"(" << (int)rtrn_name[0] << L") <<" << std::endl;
        LoadFailed();
        break;
      }

      StackMethod* mthd;
      try {
        mthd = new StackMethod(id, name, is_virtual, has_and_or, is_lambda, dclrs,
                               num_dclrs, params, mem_size, rtrn_type, cls);
      }
      catch(...) {
        // an unparsable signature throws from the constructor, which then owns nothing
        DeleteDeclarations(dclrs, num_dclrs);
        throw;
      }
      // stored before its statements load, so a failure there frees it too
      methods[id] = mthd;
      mthd->SetReturnName(rtrn_name);
      // load statements
#ifdef _DEBUG
      std::wcout << L"Method(" << mthd << L"): id=" << id << L"; name='" << name << L"'; return='"
      << rtrn_name << L"'; params=" << params << L"; bytes="
      << mem_size << std::endl;
#endif
      LoadStatements(mthd, is_debug);
    }
  }
  catch(...) {
    for(int i = 0; i < number; ++i) {
      delete methods[i];
    }
    delete[] methods;
    throw;
  }
  cls->SetMethods(methods, number);
}

void Loader::LoadInitializationCode(StackMethod* method)
{
  std::vector<StackInstr> instrs;

  instrs.push_back(StackInstr(-1, arguments.size()));
  instrs.push_back(StackInstr(-1, NEW_INT_ARY, (long)1));
  instrs.push_back(StackInstr(-1, STOR_LOCL_INT_VAR, 0L, LOCL));

  for(size_t i = 0; i < arguments.size(); ++i) {
    instrs.push_back(StackInstr(-1, arguments[i].size()));
    instrs.push_back(StackInstr(-1, NEW_CHAR_ARY, 1L));
    instrs.push_back(StackInstr(-1, (long)(num_char_strings + i)));
    instrs.push_back(StackInstr(-1, (long)instructions::CPY_CHAR_STR_ARY));
    instrs.push_back(StackInstr(-1, TRAP_RTRN, 3L));

    instrs.push_back(StackInstr(-1, NEW_OBJ_INST, (long)string_cls_id));
    // note: method ID is position dependent
    instrs.push_back(StackInstr(-1, MTHD_CALL, (long)string_cls_id, 2L, 0L));

    instrs.push_back(StackInstr(-1, i));
    instrs.push_back(StackInstr(-1, LOAD_LOCL_INT_VAR, 0L, LOCL));
    instrs.push_back(StackInstr(-1, STOR_INT_ARY_ELM, 1L, LOCL));
  }

  instrs.push_back(StackInstr(-1, LOAD_LOCL_INT_VAR, 0L, LOCL));
  instrs.push_back(StackInstr(-1, LOAD_INST_MEM));
  instrs.push_back(StackInstr(-1, MTHD_CALL, (long)start_class_id, (long)start_method_id, 0L));
  instrs.push_back(*cached_instrs[RTRN]);

  // copy to contiguous array
  StackInstr* mthd_instrs = new StackInstr[instrs.size()];
  std::copy(instrs.begin(), instrs.end(), mthd_instrs);
  method->SetInstructions(mthd_instrs, (int)instrs.size());
}

void Loader::LoadStatements(StackMethod* method, bool is_debug)
{
  int line_num = -1;
  const unsigned long num_instrs = ReadUnsigned();
  // Each instruction is at least one byte (opcode) in the stream, so a count
  // exceeding the bytes remaining is corrupt -- reject before the allocation.
  if(buffer_end && num_instrs > (size_t)(buffer_end - buffer)) {
    error << L">>> Corrupt bytecode: invalid instruction count <<<" << std::endl;
    LoadFailed();
  }
  StackInstr* mthd_instrs = new StackInstr[num_instrs];
  // the method owns the array from here, so a failure below frees it
  method->SetInstructions(mthd_instrs, static_cast<int>(num_instrs));

  // Verification, as each instruction is read (see Loader::Verify): the
  // operands the reading below relates to the method, its class and the class
  // table. A separate walk over the finished program cost a fifth of the load
  // time on a large one; here the checks ride on the switch that builds the
  // instruction anyway.
  StackClass* cls = method->GetClass();
  const INT64_VALUE mem_size = method->GetMemorySize();
  if(mem_size < 0) {
    VerifyFailed(cls, method, -1, L"negative frame size");
  }
  const INT64_VALUE cls_space = class_spaces[cls->GetId()];
  const long count = (long)num_instrs;
  long slots_until = -1;

  for(unsigned long i = 0; i < num_instrs; ++i) {
    const int type = ReadByte();
    if(is_debug) {
      line_num = static_cast<int>(ReadInt());
    }
    const long ip = (long)i;
    // the slots a JMP_TABLE reads follow it, and nothing else may stand there
    if(ip <= slots_until && type != JMP_TABLE_SLOT) {
      VerifyFailed(cls, method, ip, L"jump table slot missing");
    }

    switch(type) {
    case LOAD_INST_MEM:
      mthd_instrs[i] = StackInstr(line_num, LOAD_INST_MEM);
      break;

    case LOAD_CLS_MEM:
      mthd_instrs[i] = StackInstr(line_num, LOAD_CLS_MEM);
      break;

    case LOAD_INT_LIT:
      mthd_instrs[i] = StackInstr(line_num, ReadInt64());
      break;

    case LOAD_CHAR_LIT:
      mthd_instrs[i] = StackInstr(line_num, LOAD_CHAR_LIT, (long)ReadChar());
      break;

    case TRAP:
    case TRAP_RTRN: {
      const long num_args = ReadInt();
      // the compiler pushes the trap id as a literal just ahead of the trap
      if(num_args < 1 || i == 0 || mthd_instrs[i - 1].GetType() != LOAD_INT_LIT ||
         !IsTrapId(mthd_instrs[i - 1].GetInt64Operand())) {
        VerifyFailed(cls, method, ip, L"trap without a known trap id");
      }
      mthd_instrs[i] = StackInstr(line_num, (InstructionType)type, num_args);
    }
      break;

    case OBJ_INST_CAST:
      mthd_instrs[i] = StackInstr(line_num, OBJ_INST_CAST, CheckClassId(method, ip, ReadInt()));
      break;

    case LOAD_FLOAT_LIT:
      mthd_instrs[i] = StackInstr(line_num, LOAD_FLOAT_LIT, ReadDouble());
      break;

    case NEW_FLOAT_ARY:
      mthd_instrs[i] = StackInstr(line_num, NEW_FLOAT_ARY, CheckDimension(method, ip, ReadInt()));
      break;

    case NEW_INT_ARY:
      mthd_instrs[i] = StackInstr(line_num, NEW_INT_ARY, CheckDimension(method, ip, ReadInt()));
      break;

    case NEW_BYTE_ARY:
      mthd_instrs[i] = StackInstr(line_num, NEW_BYTE_ARY, CheckDimension(method, ip, ReadInt()));
      break;

    case NEW_CHAR_ARY:
      mthd_instrs[i] = StackInstr(line_num, NEW_CHAR_ARY, CheckDimension(method, ip, ReadInt()));
      break;

    case NEW_OBJ_INST:
      mthd_instrs[i] = StackInstr(line_num, NEW_OBJ_INST, CheckClassId(method, ip, ReadInt()));
      break;

    case NEW_FUNC_INST:
      mthd_instrs[i] = StackInstr(line_num, NEW_FUNC_INST, ReadInt());
      break;

    case LBL:
      mthd_instrs[i] = StackInstr(line_num, LBL, ReadInt());
      break;

    case OBJ_TYPE_OF:
      mthd_instrs[i] = StackInstr(line_num, OBJ_TYPE_OF, CheckClassId(method, ip, ReadInt()));
      break;

    case LOAD_INT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 1, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, mem_context == LOCL ? LOAD_LOCL_INT_VAR : LOAD_CLS_INST_INT_VAR, id, mem_context);
    }
      break;

    case LOAD_FUNC_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 2, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, LOAD_FUNC_VAR, id, mem_context);
    }
      break;

    case LOAD_FLOAT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 1, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, LOAD_FLOAT_VAR, id, mem_context);
    }
      break;

    case STOR_INT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 1, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, mem_context == LOCL ? STOR_LOCL_INT_VAR : STOR_CLS_INST_INT_VAR, id, mem_context);
    }
      break;

    case STOR_FUNC_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 2, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, STOR_FUNC_VAR, id, mem_context);
    }
      break;

    case STOR_FLOAT_VAR: {
      const long id = ReadInt();
      const long mem_context = ReadInt();
      CheckSlot(method, ip, id, mem_context, 1, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, STOR_FLOAT_VAR, id, mem_context);
    }
      break;

    case COPY_INT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 1, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, mem_context == LOCL ? COPY_LOCL_INT_VAR : COPY_CLS_INST_INT_VAR, id, mem_context);
    }
      break;

    case COPY_FLOAT_VAR: {
      const long id = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckSlot(method, ip, id, mem_context, 1, mem_size, cls_space);
      mthd_instrs[i] = StackInstr(line_num, COPY_FLOAT_VAR, id, mem_context);
    }
      break;

    case LOAD_BYTE_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, LOAD_BYTE_ARY_ELM, dim, mem_context);
    }
      break;

    case LOAD_CHAR_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, LOAD_CHAR_ARY_ELM, dim, mem_context);
    }
      break;
      
    case LOAD_INT_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, LOAD_INT_ARY_ELM, dim, mem_context);
    }
      break;

    case LOAD_FLOAT_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, LOAD_FLOAT_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_BYTE_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, STOR_BYTE_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_CHAR_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, STOR_CHAR_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_INT_ARY_ELM: {
      const long dim = ReadInt();
      const MemoryContext mem_context = (MemoryContext)ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, STOR_INT_ARY_ELM, dim, mem_context);
    }
      break;

    case STOR_FLOAT_ARY_ELM: {
      const long dim = ReadInt();
      const long mem_context = ReadInt();
      CheckDimension(method, ip, dim);
      CheckContext(method, ip, mem_context);
      mthd_instrs[i] = StackInstr(line_num, STOR_FLOAT_ARY_ELM, dim, mem_context);
    }
      break;

    case DYN_MTHD_CALL: {
      const long num_params = ReadInt();
      const long rtrn_type = ReadInt();
      mthd_instrs[i] = StackInstr(line_num, DYN_MTHD_CALL, num_params, rtrn_type);
    }
      break;

    case MTHD_CALL: {
      const long cls_id = CheckClassId(method, ip, ReadInt());
      const long mthd_id = ReadInt();
      const long is_native = ReadInt();
      // The callee may not have loaded yet, so the highest method id named in
      // each class is compared with its method count afterwards (Verify).
      // ASYNC_MTHD_CALL is not checked: it takes its target from the stack.
      if(mthd_id < 0) {
        VerifyFailed(cls, method, ip, L"method id out of range");
      }
      if(mthd_id > verify.max_called[cls_id]) {
        verify.max_called[cls_id] = mthd_id;
        verify.called_where[cls_id] = std::make_pair(method, ip);
      }
      mthd_instrs[i] = StackInstr(line_num, MTHD_CALL, cls_id, mthd_id, is_native);
    }
      break;

    case ASYNC_MTHD_CALL: {
      const long cls_id = ReadInt();
      const long mthd_id = ReadInt();
      const long is_native = ReadInt();
      mthd_instrs[i] = StackInstr(line_num, ASYNC_MTHD_CALL, cls_id, mthd_id, is_native);
    }
      break;

    case JMP: {
      const long label = CheckTarget(method, ip, ReadInt(), count);
      const long cond = ReadInt();
      mthd_instrs[i] = StackInstr(line_num, JMP, label, cond);
    }
      break;

    case TRY_START: {
      const long handler = CheckTarget(method, ip, ReadInt(), count);
      const long cond = ReadInt();
      mthd_instrs[i] = StackInstr(line_num, TRY_START, handler, cond);
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
    case BIT_NOT_INT:
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
    case TRUNC_FLOAT:
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
    case LOG2_FLOAT:
    case CBRT_FLOAT:
    case ATAN2_FLOAT:
    case COSH_FLOAT:
    case SINH_FLOAT:
    case TANH_FLOAT:
    case ACOSH_FLOAT:
    case ASINH_FLOAT:
    case ATANH_FLOAT:
    case LOG_FLOAT:
    case ROUND_FLOAT:
    case EXP_FLOAT:
    case LOG10_FLOAT:
    case POW_FLOAT:
    case GAMMA_FLOAT:
    case NAN_INT:
    case INF_INT:
    case NEG_INF_INT:
    case NAN_FLOAT:
    case INF_FLOAT:
    case NEG_INF_FLOAT:
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
    case TRY_END:
    case CPY_BYTE_ARY:
    case CPY_CHAR_ARY:
    case CPY_INT_ARY:
    case CPY_FLOAT_ARY:
    case POP_INT:
    case POP_FLOAT:
    case RTRN:
    case ZERO_BYTE_ARY:
    case ZERO_CHAR_ARY:
    case ZERO_INT_ARY:
    case ZERO_FLOAT_ARY:
      mthd_instrs[i] = *cached_instrs[type];
      break;
      //
      // End: instruction caching
      //

    case JMP_TABLE: {
      const long base = ReadInt();
      const long range = ReadInt();
      const long default_ip = CheckTarget(method, ip, ReadInt(), count);
      // 'range' slots follow the header; the selector picks one
      if(range < 0 || range >= count - ip) {
        VerifyFailed(cls, method, ip, L"jump table larger than the method");
      }
      slots_until = ip + range;
      mthd_instrs[i] = StackInstr(line_num, JMP_TABLE, base, range, default_ip);
    }
      break;

    case JMP_TABLE_SLOT: {
      const long target_ip = CheckTarget(method, ip, ReadInt(), count);
      mthd_instrs[i] = StackInstr(line_num, JMP_TABLE_SLOT, target_ip);
    }
      break;

    case LIB_OBJ_INST_CAST:
      error << L">>> unsupported instruction for executable: LIB_OBJ_INST_CAST <<<" << std::endl;
      LoadFailed();

    case LIB_NEW_OBJ_INST:
      error << L">>> unsupported instruction for executable: LIB_NEW_OBJ_INST <<<" << std::endl;
      LoadFailed();

    case LIB_MTHD_CALL:
      error << L">>> unsupported instruction for executable: LIB_MTHD_CALL <<<" << std::endl;
      LoadFailed();

    default:
      // said only in debug builds before: a release obr exited 1 without a word
      error << L">>> Corrupt bytecode: unknown instruction: id=" << type << L" <<<" << std::endl;
      LoadFailed();
      break;
    }
  }


  // Control must not run past the last instruction: the interpreter would read
  // the word after the array.
  if(count > 0) {
    const StackInstr& last = mthd_instrs[count - 1];
    if(last.GetType() != RTRN && !(last.GetType() == JMP && last.GetOperand2() < 0)) {
      VerifyFailed(cls, method, count - 1, L"the method can run past its last instruction");
    }
  }
}

//
// Verification: checks made as a program loads, before any of it runs: per
// instruction in LoadStatements (the helpers below), and for the whole program
// in Verify. Loading proves the file is well formed; these relate each operand to
// the program it is part of, the checks docs/SECURITY_MODEL.md listed as
// missing, so a crafted .obe is refused instead of steering the interpreter
// outside the method, the frame or the class tables. Each rule holds for every
// program the toolchain compiles (all of programs/regression, which links the
// shipped libraries); a program that breaks one was not written by obc.
//
// Not checked: operand-stack depth. A trap pops as many words as its own id
// says, so a depth bound needs a per-trap table the compiler and the VM do not
// share yet.
//

// The Traps enum's first and last ids (core/shared/traps.h). A trap added after
// LAST_TRAP would be refused here, so tools/cicd/check_trap_handlers.py fails
// when LAST_TRAP is not the enum's last enumerator.
static const INT64_VALUE FIRST_TRAP = instructions::LOAD_CLS_INST_ID;
static const INT64_VALUE LAST_TRAP = instructions::GC_TEST_ADDRESS;

// The ids a program may push ahead of TRAP or TRAP_RTRN: the Traps enum, and the
// instruction ids the compiler writes as trap ids (check_trap_handlers.py keeps
// this list whole: every id IntermediateEmitter::EmitSystemDirective writes).
static bool IsTrapId(const INT64_VALUE id)
{
  if(id >= FIRST_TRAP && id <= LAST_TRAP) {
    return true;
  }

  switch(id) {
  case instructions::SET_SIGNAL:
  case instructions::RAISE_SIGNAL:
    return true;

  default:
    return false;
  }
}

void Loader::VerifyFailed(StackClass* cls, StackMethod* mthd, long ip, const wchar_t* why)
{
  error << L">>> Invalid bytecode: ";
  if(mthd) {
    error << mthd->GetName();
  }
  else if(cls) {
    error << cls->GetName();
  }
  else {
    error << L"the program";
  }
  if(ip >= 0) {
    error << L", instruction " << ip;
  }
  error << L": " << why << L" <<<" << std::endl;
  LoadFailed();
}

void Loader::CheckSlot(StackMethod* mthd, long ip, INT64_VALUE id, long context, int width,
                       INT64_VALUE mem_size, INT64_VALUE cls_space)
{
  if(id < 0) {
    VerifyFailed(nullptr, mthd, ip, L"negative variable id");
  }
  switch(context) {
  case LOCL:
    // the frame is mem_size + 2 words, and a local is mem[id + 1]
    if(id + width > mem_size + 1) {
      VerifyFailed(nullptr, mthd, ip, L"local variable id beyond the frame");
    }
    break;

  case CLS:
    if((id + width) * (INT64_VALUE)sizeof(size_t) > cls_space) {
      VerifyFailed(nullptr, mthd, ip, L"class variable id beyond the class's memory");
    }
    break;

  case INST:
    // compared with the largest object once every class has loaded (Verify)
    if((id + width) * (INT64_VALUE)sizeof(size_t) > verify.inst_end) {
      verify.inst_end = (id + width) * (INT64_VALUE)sizeof(size_t);
      verify.inst_where = mthd;
      verify.inst_ip = ip;
    }
    break;

  default:
    VerifyFailed(nullptr, mthd, ip, L"unknown memory context");
  }
}

void Loader::CheckContext(StackMethod* mthd, long ip, long context)
{
  if(context != LOCL && context != INST && context != CLS) {
    VerifyFailed(nullptr, mthd, ip, L"unknown memory context");
  }
}

long Loader::CheckDimension(StackMethod* mthd, long ip, long dimension)
{
  if(dimension < 1) {
    VerifyFailed(nullptr, mthd, ip, L"array dimension below one");
  }
  return dimension;
}

long Loader::CheckClassId(StackMethod* mthd, long ip, long id)
{
  if(id < 0 || id >= (long)verify.max_called.size()) {
    VerifyFailed(nullptr, mthd, ip, L"class id out of range");
  }
  return id;
}

long Loader::CheckTarget(StackMethod* mthd, long ip, long target, long count)
{
  if(target < 0 || target >= count) {
    VerifyFailed(nullptr, mthd, ip, L"jump target outside the method");
  }
  return target;
}

// The checks that need the whole program, run once every class has loaded;
// each instruction was checked as LoadStatements read it.
void Loader::Verify()
{
  const long num_classes = program->GetClassNumber();
  StackClass** classes = program->GetClasses();
  long* hierarchy = program->GetHierarchy();
  long** interfaces = program->GetInterfaces();

  // An instance field is read from whatever object is on the stack, and the
  // compiler addresses a field through a holder class whose own layout can be
  // smaller (System.$Byte), so the bound is the largest object any class makes.
  INT64_VALUE max_inst_space = 0;
  for(long i = 0; i < num_classes; ++i) {
    if(classes[i]->GetInstanceMemorySize() > max_inst_space) {
      max_inst_space = classes[i]->GetInstanceMemorySize();
    }
  }
  if(verify.inst_end > max_inst_space) {
    VerifyFailed(nullptr, verify.inst_where, verify.inst_ip, L"instance variable id beyond every object's memory");
  }

  for(long i = 0; i < num_classes; ++i) {
    StackClass* cls = classes[i];
    if(hierarchy[i] < -1 || hierarchy[i] >= num_classes) {
      VerifyFailed(cls, nullptr, -1, L"parent class id out of range");
    }
    if(interfaces[i]) {
      // -1 marks an interface that was not linked in
      for(long* inf = interfaces[i]; *inf != INF_ENDING; ++inf) {
        if(*inf < -1 || *inf >= num_classes) {
          VerifyFailed(cls, nullptr, -1, L"interface id out of range");
        }
      }
    }
    // the highest method id any MTHD_CALL names in this class
    if(verify.max_called[i] >= cls->GetMethodCount()) {
      VerifyFailed(nullptr, verify.called_where[i].first, verify.called_where[i].second, L"method id out of range");
    }
  }

  // the entry point, and String's constructor, which the initialization code
  // calls (method 2) once per program argument
  StackClass* start_cls = program->GetClass(start_class_id);
  if(!start_cls || !start_cls->GetMethod(start_method_id)) {
    VerifyFailed(nullptr, nullptr, -1, L"the start method does not exist");
  }
  StackClass* string_cls = program->GetClass(string_cls_id);
  if(!arguments.empty() && (!string_cls || string_cls->GetMethodCount() < 3)) {
    VerifyFailed(nullptr, nullptr, -1, L"the String class id is out of range");
  }
}
