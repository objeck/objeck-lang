/***************************************************************************
 * VM common.
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

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "common.h"
#include "loader.h"
#include "interpreter.h"
#include "../shared/version.h"

#ifdef _WIN32
#include "arch/win32/win32.h"
#include "arch/memory.h"
#include <psapi.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "psapi.lib")
#else
#include "arch/memory.h"
#include "arch/posix/posix.h"
#endif

// Process/system stats for Runtime->GetProperty("runtime.*") diagnostics
#include <thread>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif !defined(_WIN32)
#include <unistd.h>
#include <cstdio>
#include <sys/resource.h>
#endif

#ifdef _WIN32
// Write a wide string to a Windows handle. Uses WriteConsoleW for live
// console handles so surrogate pairs arrive intact; falls back to wcout/wcerr
// for pipes and file redirection (where _O_U8TEXT produces correct UTF-8).
static inline void WinWriteWide(HANDLE h, std::wostream& fallback, const wchar_t* str, size_t len) {
  DWORD consoleMode;
  if(GetConsoleMode(h, &consoleMode)) {
    DWORD written;
    if(len > MAXDWORD) return;
    WriteConsoleW(h, str, (DWORD)len, &written, nullptr);
  }
  else {
    fallback.write(str, len);
  }
}
#endif

#include <csignal>
#include <filesystem>

#ifdef _WIN32
CRITICAL_SECTION StackProgram::program_cs;
CRITICAL_SECTION StackProgram::prop_cs;
#else
pthread_mutex_t StackProgram::program_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t StackProgram::prop_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

std::map<std::wstring, std::wstring> StackProgram::properties_map;
std::unordered_map<long, StackMethod*> StackProgram::signal_handler_func;

bool StackProgram::AddSignalHandler(long signal_id, StackMethod* mthd)
{
  signal_handler_func.insert(std::make_pair(signal_id, mthd));

  switch(signal_id) {
  case VM_SIGABRT:
    std::signal(SIGABRT, StackProgram::SignalHandler);
    break;

  case VM_SIGFPE:
    std::signal(SIGFPE, StackProgram::SignalHandler);
    break;

  case VM_SIGILL:
    std::signal(SIGILL, StackProgram::SignalHandler);
    break;

  case VM_SIGINT:
    std::signal(SIGINT, StackProgram::SignalHandler);
    break;

  case VM_SIGSEGV:
    std::signal(SIGSEGV, StackProgram::SignalHandler);
    break;

  case VM_SIGTERM:
    std::signal(SIGTERM, StackProgram::SignalHandler);
    break;

  default:
    return false;
  }

  return true;
}

StackMethod* StackProgram::GetSignalHandler(long key)
{
 std::unordered_map<long, StackMethod*>::iterator found = signal_handler_func.find(key);
  if(found != signal_handler_func.end()) {
    return found->second;
  }

  return nullptr;
}

void StackProgram::SignalHandler(int signal)
{
  StackMethod* called_method = nullptr;

  long sys_value = 0;
  switch(signal) {
  case SIGABRT: {
    std::unordered_map<long, StackMethod*>::iterator  found = signal_handler_func.find(VM_SIGABRT);
    if(found != signal_handler_func.end()) {
      called_method = found->second;
      sys_value = VM_SIGABRT;
    }
  }
    break;

  case SIGFPE: {
    std::unordered_map<long, StackMethod*>::iterator  found = signal_handler_func.find(VM_SIGFPE);
    if(found != signal_handler_func.end()) {
      called_method = found->second;
      sys_value = VM_SIGFPE;
    }
  }
    break;

  case SIGILL: {
    std::unordered_map<long, StackMethod*>::iterator  found = signal_handler_func.find(VM_SIGILL);
    if(found != signal_handler_func.end()) {
      called_method = found->second;
      sys_value = VM_SIGILL;
    }
  }
    break;

  case SIGINT: {
    std::unordered_map<long, StackMethod*>::iterator  found = signal_handler_func.find(VM_SIGINT);
    if(found != signal_handler_func.end()) {
      called_method = found->second;
      sys_value = VM_SIGINT;
    }
  }
    break;

  case SIGSEGV: {
   std::unordered_map<long, StackMethod*>::iterator  found = signal_handler_func.find(VM_SIGSEGV);
    if(found != signal_handler_func.end()) {
      called_method = found->second;
      sys_value = VM_SIGSEGV;
    }
  }
    break;

  case SIGTERM: {
     std::unordered_map<long, StackMethod*>::iterator  found = signal_handler_func.find(VM_SIGTERM);
      if(found != signal_handler_func.end()) {
        called_method = found->second;
        sys_value = VM_SIGTERM;
      }
    }
    break;
  }

  if(called_method) {
    // init
    size_t* op_stack = new size_t[OP_STACK_SIZE];
    (*op_stack) = sys_value;
    size_t* stack_pos = new size_t;
    (*stack_pos) = 1;

    // execute
    Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter;
    Runtime::StackInterpreter::AddThread(intpr);
    intpr->Execute(op_stack, stack_pos, 0, called_method, nullptr, false);

    // clean up
    delete[] op_stack;
    op_stack = nullptr;

    delete stack_pos;
    stack_pos = nullptr;

    Runtime::StackInterpreter::RemoveThread(intpr);

    delete intpr;
    intpr = nullptr;
  }
}

void StackProgram::InitializeProprieties()
{
  // install directory
#ifdef _DEBUG
#ifdef _WIN32  
  char install_path[MAX_PATH];
  DWORD status = GetModuleFileNameA(nullptr, install_path, sizeof(install_path));
  if(status > 0) {
    std::string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('\\');
    if(install_index != std::string::npos) {
      std::wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
      properties_map.insert(std::pair<std::wstring, std::wstring>(L"install_dir", install_dir));
    }
  }
#else
  ssize_t status = 0;
  char install_path[SMALL_BUFFER_MAX] = {0};
#ifdef _OSX
  uint32_t size = SMALL_BUFFER_MAX;
  if(_NSGetExecutablePath(install_path, &size) != 0) {
    status = -1;
  }
#else
  status = ::readlink("/proc/self/exe", install_path, sizeof(install_path) - 1);
#endif
  if(status != -1) {
    std::string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('/');
    if(install_index != std::string::npos) {
      std::wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
      properties_map.insert(std::pair<std::wstring, std::wstring>(L"install_dir", install_dir));
    }
  }
#endif
#else
#ifdef _WIN32  
  char install_path[MAX_PATH];
  DWORD status = GetModuleFileNameA(nullptr, install_path, sizeof(install_path));
  if(status > 0) {
    std::string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('\\');
    if(install_index != std::string::npos) {
      exe_path = exe_path.substr(0, install_index);
      install_index = exe_path.find_last_of('\\');
      if(install_index != std::string::npos) {
        std::wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
        properties_map.insert(std::pair<std::wstring, std::wstring>(L"install_dir", install_dir));
      }
    }
  }
#else
  ssize_t status = 0;
  char install_path[SMALL_BUFFER_MAX] = {0};
#ifdef _OSX
  uint32_t size = SMALL_BUFFER_MAX;
  if(_NSGetExecutablePath(install_path, &size) != 0) {
    status = -1;
  }
#else
  status = ::readlink("/proc/self/exe", install_path, sizeof(install_path) - 1);
  if(status != -1) {
    install_path[status] = '\0';
  }
#endif
  if(status != -1) {
    std::string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('/');
    if(install_index != std::string::npos) {
      exe_path = exe_path.substr(0, install_index);
      install_index = exe_path.find_last_of('/');
      if(install_index != std::string::npos) {
        std::wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
        properties_map.insert(std::pair<std::wstring, std::wstring>(L"install_dir", install_dir));
      }
    }
  }
#endif
#endif
   
   properties_map.insert(std::pair<std::wstring, std::wstring>(L"lib_dir", GetLibraryPath()));

  // user and temp directories
#ifdef _WIN32  
  wchar_t user_dir[MAX_PATH];
  if(GetUserDirectory(user_dir, MAX_PATH)) {
    std::wstring user_dir_value(user_dir);
    if(user_dir_value.back() == L'/' || user_dir_value.back() == L'\\') {
      user_dir_value.pop_back();
    }
    properties_map.insert(std::pair<std::wstring, std::wstring>(L"user_dir", user_dir_value));
  }

  char tmp_dir[MAX_PATH];
  if(GetTempPath(MAX_PATH, tmp_dir)) {
    std::wstring tmp_dir_value = BytesToUnicode(tmp_dir);
    if(tmp_dir_value.back() == L'/' || tmp_dir_value.back() == L'\\') {
      tmp_dir_value.pop_back();
    }
    properties_map.insert(std::pair<std::wstring, std::wstring>(L"tmp_dir", tmp_dir_value));
  }
#else
  struct passwd* user = getpwuid(getuid());
  if(user) {
    properties_map.insert(std::pair<std::wstring, std::wstring>(L"user_dir", BytesToUnicode(user->pw_dir)));
  }

  const char* tmp_dir = P_tmpdir;
  if(tmp_dir) {
    properties_map.insert(std::pair<std::wstring, std::wstring>(L"tmp_dir", BytesToUnicode(tmp_dir)));
  }
#endif

  // read configuration properties
  const int line_max = 80;
  char buffer[line_max + 1];
  std::ifstream config("config.prop");
  if(config.good()) {
    config.getline(buffer, line_max);
    while(strlen(buffer) > 0) {
      // read line and parse
      std::wstring line = BytesToUnicode(buffer);
      if(line.size() > 0 && line[0] != L'#') {
        size_t offset = line.find_first_of(L'=');
        // set name/value pairs
        const std::wstring name = line.substr(0, offset);
        const std::wstring value = line.substr(offset + 1);
        if(name.size() > 0 && value.size() > 0) {
          properties_map.insert(std::pair<std::wstring, std::wstring>(name, value));
        }
      }
      // update
      config.getline(buffer, 80);
    }
  }
  config.close();
}

StackClass* StackClass::GetParent() {
  if(!parent) {
    parent = Loader::GetProgram()->GetClass(pid);
  }

  return parent;
}

const std::wstring StackMethod::ParseName(const std::wstring& name) const
{
  int state;
  size_t index = name.find_last_of(L':');
  if(index > 0) {
    std::wstring params_name = name.substr(index + 1);

    // check return type
    index = 0;
    while(index < params_name.size()) {
#ifdef _DEBUG
      ParamType param;
#endif
      switch(params_name[index]) {
        // bool
      case 'l':
#ifdef _DEBUG
        param = INT_PARM;
#endif
        state = 0;
        index++;
        break;

        // byte
      case 'b':
#ifdef _DEBUG
        param = INT_PARM;
#endif
        state = 1;
        index++;
        break;

        // int
      case 'i':
#ifdef _DEBUG
        param = INT_PARM;
#endif
        state = 2;
        index++;
        break;

        // float
      case 'f':
#ifdef _DEBUG
        param = FLOAT_PARM;
#endif
        state = 3;
        index++;
        break;

        // char
      case 'c':
#ifdef _DEBUG
        param = CHAR_PARM;
#endif
        state = 4;
        index++;
        break;

        // obj
      case 'o':
#ifdef _DEBUG
        param = OBJ_PARM;
#endif
        state = 5;
        index++;
        while(index < params_name.size() && params_name[index] != ',') {
          index++;
        }
        break;

        // func
      case 'm':
#ifdef _DEBUG
        param = FUNC_PARM;
#endif
        state = 6;
        index++;
        while(index < params_name.size() && params_name[index] != '~') {
          index++;
        }
        while(index < params_name.size() && params_name[index] != ',') {
          index++;
        }
        break;

      default:
        throw std::runtime_error("Invalid method signature!");
        break;
      }

      // check array
      int dimension = 0;
      while(index < params_name.size() && params_name[index] == '*') {
        dimension++;
        index++;
      }

      if(dimension) {
        switch(state) {
        case 0:
        case 1:
#ifdef _DEBUG
          param = BYTE_ARY_PARM;
#endif
          break;

        case 4:
#ifdef _DEBUG
          param = CHAR_ARY_PARM;
#endif
          break;

        case 2:
#ifdef _DEBUG
          param = INT_ARY_PARM;
#endif
          break;

        case 3:
#ifdef _DEBUG
          param = FLOAT_ARY_PARM;
#endif
          break;

        case 5:
#ifdef _DEBUG
          param = OBJ_ARY_PARM;
#endif
          break;
        }
      }

#ifdef _DEBUG
      switch(param) {
      case CHAR_PARM:
        std::wcout << L"  CHAR_PARM" << std::endl;
        break;

      case INT_PARM:
        std::wcout << L"  INT_PARM" << std::endl;
        break;

      case FLOAT_PARM:
        std::wcout << L"  FLOAT_PARM" << std::endl;
        break;

      case BYTE_ARY_PARM:
        std::wcout << L"  BYTE_ARY_PARM" << std::endl;
        break;

      case CHAR_ARY_PARM:
        std::wcout << L"  CHAR_ARY_PARM" << std::endl;
        break;

      case INT_ARY_PARM:
        std::wcout << L"  INT_ARY_PARM" << std::endl;
        break;

      case FLOAT_ARY_PARM:
        std::wcout << L"  FLOAT_ARY_PARM" << std::endl;
        break;

      case OBJ_PARM:
        std::wcout << L"  OBJ_PARM" << std::endl;
        break;

      case OBJ_ARY_PARM:
        std::wcout << L"  OBJ_ARY_PARM" << std::endl;
        break;

      case FUNC_PARM:
        std::wcout << L"  FUNC_PARM" << std::endl;
        break;

      default:
        assert(false);
        break;
      }
#endif

      // match ','
      index++;
    }
  }

  return name;
}

/********************************
 * ObjectSerializer struct
 ********************************/
void ObjectSerializer::CheckObject(size_t* mem, bool is_obj, long depth) {
  if(mem) {
    SerializeByte(1);
    StackClass* cls = MemoryManager::GetClass(mem);
    if(cls) {
      // write object id
      const std::string cls_name = UnicodeToBytes(cls->GetName());
      const INT_VALUE cls_name_size = (INT_VALUE)cls_name.size();
      SerializeInt(cls_name_size);
      SerializeBytes(cls_name.c_str(), cls_name_size);
      
      if(!WasSerialized(mem)) {
#ifdef _DEBUG
        std::wcout << L"\t----- SERIALIZING object: cls_id=" << cls->GetId() << L", name='" << cls->GetName() << L"', mem_id="
          << cur_id << L" -----" << std::endl;
#endif
        CheckMemory(mem, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), depth);
      }
    }
    else {
#ifdef _DEBUG
      for(int i = 0; i < depth; i++) {
        std::wcout << L"\t";
      }
      std::wcout << L"$: addr/value=" << mem << std::endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(!WasSerialized(mem)) {
        size_t* array = (mem);
        const size_t size = array[0];
        const size_t dim = array[1];
        size_t* objects = (size_t*)(array + 2 + dim);

#ifdef _DEBUG
        for(int i = 0; i < depth; i++) {
          std::wcout << L"\t";
        }
        std::wcout << L"\t----- SERIALIZE: size=" << (size * sizeof(INT_VALUE)) << L" -----" << std::endl;
#endif

        for(size_t k = 0; k < size; ++k) {
          CheckObject((size_t*)objects[k], false, 2);
        }
      }
    }
  }
  else {
#ifdef _DEBUG
    for(int i = 0; i < depth; i++) {
      std::wcout << L"\t";
    }
    std::wcout << L"\t----- SERIALIZING object: value=Nil -----" << std::endl;
#endif
    SerializeByte(0);
  }
}

void ObjectSerializer::CheckMemory(size_t* mem, StackDclr** dclrs, const long dcls_size, long depth) {
  // check method
  for(long i = 0; i < dcls_size; i++) {
#ifdef _DEBUG
    for(long j = 0; j < depth; j++) {
      std::wcout << L"\t";
    }
#endif

    // update address based upon type
    switch(dclrs[i]->type) {
      case CHAR_PARM:
#ifdef _DEBUG
        std::wcout << L"\t" << i << L": ----- serializing char: value="
          << (*mem) << L", size=" << sizeof(INT_VALUE) << L" byte(s) -----" << std::endl;
#endif
        SerializeChar((wchar_t)*mem);
        // update
        mem++;
        break;

      case INT_PARM:
#ifdef _DEBUG
        std::wcout << L"\t" << i << L": ----- serializing int: value="
          << (*mem) << L", size=" << sizeof(INT_VALUE) << L" byte(s) -----" << std::endl;
#endif
        SerializeInt((INT64_VALUE)*mem);
        // update
        mem++;
        break;

      case FLOAT_PARM:
      {
        FLOAT_VALUE value;
        memcpy(&value, mem, sizeof(FLOAT_VALUE));
#ifdef _DEBUG
        std::wcout << L"\t" << i << L": ----- serializing float: value=" << value << L", size="
          << sizeof(FLOAT_VALUE) << L" byte(s) -----" << std::endl;
#endif
        SerializeFloat(value);
        // update: a Float occupies ONE instance slot in the 64-bit object layout
        // (a slot is size_t = 8 bytes). Advancing by 2 (a 32-bit-era leftover, when
        // a slot was 4 bytes) read the field after a Float from the wrong slot, so
        // it serialized as 0/Nil.
        mem += 1;
      }
      break;

      case FUNC_PARM:
      {
        // A function reference occupies TWO instance slots: a packed method
        // reference (slot 0) and a captured-instance pointer (slot 1). The method
        // reference (class/method ids) is process-independent and is serialized;
        // the captured pointer is process-specific and is dropped (restored Nil).
        // So a non-capturing method reference round-trips, a capturing closure
        // degrades safely, and — crucially — the field AFTER the func no longer
        // desyncs (there was previously no FUNC_PARM case, so mem wasn't advanced).
        const size_t method_ref = *mem;
        if(method_ref) {
          SerializeByte(1);
          SerializeInt((INT64_VALUE)method_ref);
        }
        else {
          SerializeByte(0);
        }
        mem += 2;
      }
      break;

      case BYTE_ARY_PARM:
      {
        size_t* array = (size_t*)(*mem);
        if(array) {
          SerializeByte(1);
          // mark data
          if(!WasSerialized((size_t*)(*mem))) {
            const long array_size = (long)array[0];
#ifdef _DEBUG
            std::wcout << L"\t" << i << L": ----- serializing byte array: mem_id=" << cur_id << L", size="
              << array_size << L" byte(s) -----" << std::endl;
#endif
            // write metadata
            SerializeInt((INT_VALUE)array[0]);
            SerializeInt((INT_VALUE)array[1]);
            SerializeInt((INT_VALUE)array[2]);
            char* array_ptr = (char*)(array + 3);

            // values
            SerializeBytes(array_ptr, array_size);
          }
        }
        else {
          SerializeByte(0);
        }
        // update
        mem++;
      }
      break;

      case CHAR_ARY_PARM:
      {
        size_t* array = (size_t*)(*mem);
        if(array) {
          SerializeByte(1);
          // mark data
          if(!WasSerialized((size_t*)(*mem))) {
            // convert
            const std::string buffer = UnicodeToBytes((wchar_t*)(array + 3));
            const INT_VALUE array_size = (INT_VALUE)buffer.size();
#ifdef _DEBUG
            std::wcout << L"\t" << i << L": ----- serializing char array: value='" << ((wchar_t*)(array + 3)) << L", mem_id=" << cur_id << L", size=" << array_size << L" byte(s) -----" << std::endl;
#endif
            // write metadata
            SerializeInt(array_size);
            SerializeInt((INT_VALUE)array[1]);
            SerializeInt(array_size);
            
            // values
            SerializeBytes(buffer.c_str(), array_size);
          }
        }
        else {
          SerializeByte(0);
        }
        // update
        mem++;
      }
      break;

      case INT_ARY_PARM:
      {
        size_t* array = (size_t*)(*mem);
        if(array) {
          SerializeByte(1);
          // mark data
          if(!WasSerialized((size_t*)(*mem))) {
            const long array_size = (long)array[0];
#ifdef _DEBUG
            std::wcout << L"\t" << i << L": ----- serializing int array: mem_id=" << cur_id << L", size="
              << array_size << L" byte(s) -----" << std::endl;
#endif
            // write metadata
            SerializeInt((INT_VALUE)array[0]);
            SerializeInt((INT_VALUE)array[1]);
            SerializeInt((INT_VALUE)array[2]);
            size_t* array_ptr = array + 3;

            // values: Int elements are stored as size_t (8 bytes); SerializeInt
            // would truncate each to int32 (losing values > 2^32). Write the raw
            // 8-byte elements (the deserializer reads them back the same way).
            SerializeBytes(array_ptr, array_size * (long)sizeof(size_t));
          }
        }
        else {
          SerializeByte(0);
        }
        // update
        mem++;
      }
      break;

      case FLOAT_ARY_PARM:
      {
        size_t* array = (size_t*)(*mem);
        if(array) {
          SerializeByte(1);
          // mark data
          if(!WasSerialized((size_t*)(*mem))) {
            const long array_size = (long)array[0];
#ifdef _DEBUG
            std::wcout << L"\t" << i << L": ----- serializing float array: mem_id=" << cur_id << L", size="
              << array_size << L" byte(s) -----" << std::endl;
#endif
            // write metadata
            SerializeInt((INT_VALUE)array[0]);
            SerializeInt((INT_VALUE)array[1]);
            SerializeInt((INT_VALUE)array[2]);
            FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 3);

            // write values
            SerializeBytes(array_ptr, array_size * sizeof(FLOAT_VALUE));
          }
        }
        else {
          SerializeByte(0);
        }
        // update
        mem++;
      }
      break;

      case OBJ_ARY_PARM:
      {
        size_t* array = (size_t*)(*mem);
        if(array) {
          SerializeByte(1);
          // mark data
          if(!WasSerialized((size_t*)(*mem))) {
            const long array_size = (long)array[0];
#ifdef _DEBUG
            std::wcout << L"\t" << i << L": ----- serializing object array: mem_id=" << cur_id << L", size="
              << array_size << L" byte(s) -----" << std::endl;
#endif
            // write metadata
            SerializeInt((INT_VALUE)array[0]);
            SerializeInt((INT_VALUE)array[1]);
            SerializeInt((INT_VALUE)array[2]);
            size_t* array_ptr = array + 3;

            // write values
            for(int i = 0; i < array_size; i++) {
              CheckObject((size_t*)(array_ptr[i]), true, depth + 1);
            }
          }
        }
        else {
          SerializeByte(0);
        }
        // update
        mem++;
      }
      break;

      case OBJ_PARM:
      {
        // check object
        CheckObject((size_t*)(*mem), true, depth + 1);
        // update
        mem++;
      }
      break;

      default:
        break;
    }
  }
}

void ObjectSerializer::Serialize(size_t* inst) {
  next_id = 0;
  CheckObject(inst, true, 0);
}

/********************************
 * ObjectDeserializer class
 ********************************/
size_t* ObjectDeserializer::DeserializeObject() {
  // read object id
  const INT_VALUE char_array_size = DeserializeInt32();
  if(read_error || char_array_size < 0 || !CanRead(char_array_size)) {
    return nullptr;
  }
  char* temp = new char[char_array_size + 1];
  memcpy(temp, buffer + buffer_offset, char_array_size);
  buffer_offset += char_array_size;
  temp[char_array_size] = '\0';
  const std::wstring cls_name = BytesToUnicode(temp);
  // clean up
  delete[] temp;
  temp = nullptr;
  
  cls = Loader::GetProgram()->GetClass(cls_name);
  if(cls) {
#ifdef _DEBUG
    std::wcout << L"--- DESERIALIZING object: name='" << cls_name << L"' ---" << std::endl;
#endif
    
    INT_VALUE mem_id = DeserializeInt32();
    if(read_error) {
      return nullptr;
    }
    if(mem_id < 0) {
      instance = MemoryManager::AllocateObject(cls->GetId(), op_stack, *stack_pos, false);
      mem_cache[-mem_id] = instance;
    }
    else {
      std::map<INT_VALUE, size_t*>::iterator found = mem_cache.find(mem_id);
      if(found == mem_cache.end()) {
        return nullptr;
      }
      return found->second;
    }
  }
  else {
    std::wcerr << L">>> Unable to deserialize class " << cls_name << L", class appears to not be linked <<<" << std::endl;
    exit(1);
  }

  long dclr_pos = 0;
  StackDclr** dclrs = cls->GetInstanceDeclarations();
  const long dclr_num = cls->GetNumberInstanceDeclarations();
  while(dclr_pos < dclr_num && buffer_offset < buffer_array_size) {
    ParamType type = dclrs[dclr_pos++]->type;

    switch(type) {
      case CHAR_PARM:
        instance[instance_pos++] = DeserializeChar();
#ifdef _DEBUG
        std::wcout << L"--- DESERIALIZING char: value=" << instance[instance_pos - 1] << L" ---" << std::endl;
#endif
        break;

      case INT_PARM:
        instance[instance_pos++] = DeserializeInt();
#ifdef _DEBUG
        std::wcout << L"--- DESERIALIZING int: value=" << instance[instance_pos - 1] << L" ---" << std::endl;
#endif
        break;

      case FLOAT_PARM:
      {
        FLOAT_VALUE value = DeserializeFloat();
        memcpy(&instance[instance_pos], &value, sizeof(value));
#ifdef _DEBUG
        std::wcout << L"--- DESERIALIZING float: value=" << value << L" ---" << std::endl;
#endif
        // A Float occupies one instance slot (see the serialize side); advancing
        // by 2 wrote the next field past its slot.
        instance_pos += 1;
      }
      break;

      case FUNC_PARM:
      {
        // Restore the packed method reference (slot 0); the captured instance
        // (slot 1) was not serialized, so it is restored as Nil. A func ref is
        // TWO slots — see the serialize side.
        if(!DeserializeByte()) {
          instance[instance_pos] = 0;
        }
        else {
          instance[instance_pos] = (size_t)DeserializeInt();
        }
        instance[instance_pos + 1] = 0;
        instance_pos += 2;
      }
      break;

      case BYTE_ARY_PARM:
      {
        if(!DeserializeByte()) {
          instance[instance_pos++] = 0;
        }
        else {
          INT_VALUE mem_id = DeserializeInt32();
          if(mem_id < 0) {
            const INT_VALUE byte_array_size = DeserializeInt32();
            const INT_VALUE byte_array_dim = DeserializeInt32();
            const INT_VALUE byte_array_size_dim = DeserializeInt32();
            if(read_error || byte_array_size < 0 || byte_array_dim < 0 || !CanRead(byte_array_size)) {
              return nullptr;
            }
            size_t* byte_array = MemoryManager::AllocateArray((size_t)(byte_array_size + ((byte_array_dim + 2) * sizeof(size_t))),
                                                              BYTE_ARY_TYPE, op_stack, *stack_pos, false);
            char* byte_array_ptr = (char*)(byte_array + 3);
            byte_array[0] = byte_array_size;
            byte_array[1] = byte_array_dim;
            byte_array[2] = byte_array_size_dim;
            // copy content
            memcpy(byte_array_ptr, buffer + buffer_offset, byte_array_size);
            buffer_offset += byte_array_size;
#ifdef _DEBUG
            std::wcout << L"--- DESERIALIZING: byte array; value=" << byte_array << L", size=" << byte_array_size << L" ---" << std::endl;
#endif
            // update cache
            mem_cache[-mem_id] = byte_array;
            instance[instance_pos++] = (size_t)byte_array;
          }
          else {
            std::map<INT_VALUE, size_t*>::iterator found = mem_cache.find(mem_id);
            if(found == mem_cache.end()) {
              return nullptr;
            }
            instance[instance_pos++] = (size_t)found->second;
          }
        }
      }
      break;

      case CHAR_ARY_PARM:
      {
        if(!DeserializeByte()) {
          instance[instance_pos++] = 0;
        }
        else {
          INT_VALUE mem_id = DeserializeInt32();
          if(mem_id < 0) {
            INT_VALUE char_array_size = DeserializeInt32();
            const INT_VALUE char_array_dim = DeserializeInt32();
            INT_VALUE char_array_size_dim = DeserializeInt32();
            if(read_error || char_array_size < 0 || char_array_dim < 0 || !CanRead(char_array_size)) {
              return nullptr;
            }
            // copy content
            char* in = new char[char_array_size + 1];
            memcpy(in, buffer + buffer_offset, char_array_size);
            buffer_offset += char_array_size;
            in[char_array_size] = '\0';
            const std::wstring out = BytesToUnicode(in);
            // clean up
            delete[] in;
            in = nullptr;
#ifdef _DEBUG
            std::wcout << L"--- DESERIALIZING: char array; value=" << out << L", size="
              << char_array_size << L" ---" << std::endl;
#endif
            char_array_size = char_array_size_dim = (INT_VALUE)out.size();
            size_t* char_array = MemoryManager::AllocateArray(char_array_size +
              ((char_array_dim + 2) * sizeof(size_t)), CHAR_ARY_TYPE, op_stack, *stack_pos, false);
            char_array[0] = char_array_size;
            char_array[1] = char_array_dim;
            char_array[2] = char_array_size_dim;

            wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
            memcpy(char_array_ptr, out.c_str(), char_array_size * sizeof(wchar_t));

            // update cache
            mem_cache[-mem_id] = char_array;
            instance[instance_pos++] = (size_t)char_array;
          }
          else {
            std::map<INT_VALUE, size_t*>::iterator found = mem_cache.find(mem_id);
            if(found == mem_cache.end()) {
              return nullptr;
            }
            instance[instance_pos++] = (size_t)found->second;
          }
        }
      }
      break;

      case INT_ARY_PARM:
      {
        if(!DeserializeByte()) {
          instance[instance_pos++] = 0;
        }
        else {
          INT_VALUE mem_id = DeserializeInt32();
          if(mem_id < 0) {
            const INT_VALUE array_size = DeserializeInt32();
            const INT_VALUE array_dim = DeserializeInt32();
            const INT_VALUE array_size_dim = DeserializeInt32();
            if(read_error || array_size < 0 || array_dim < 0) {
              return nullptr;
            }
            size_t* array = MemoryManager::AllocateArray(array_size + array_dim + 2, instructions::INT_TYPE,
                                                         op_stack, *stack_pos, false);
            array[0] = array_size;
            array[1] = array_dim;
            array[2] = array_size_dim;
            size_t* array_ptr = array + 3;
            // copy content: raw 8-byte elements (must match SerializeBytes on the
            // serialize side). Bounds-check the attacker-controlled length first,
            // multiplying in 64-bit so the byte count cannot wrap.
            if(!CanRead((INT64_VALUE)array_size * (INT64_VALUE)sizeof(size_t))) {
              return nullptr;
            }
            memcpy(array_ptr, buffer + buffer_offset, (size_t)array_size * sizeof(size_t));
            buffer_offset += array_size * sizeof(size_t);
#ifdef _DEBUG
            std::wcout << L"--- DESERIALIZING: int array; value=" << array << L",  size=" << array_size << L" ---" << std::endl;
#endif
            // update cache
            mem_cache[-mem_id] = array;
            instance[instance_pos++] = (size_t)array;
          }
          else {
            std::map<INT_VALUE, size_t*>::iterator found = mem_cache.find(mem_id);
            if(found == mem_cache.end()) {
              return nullptr;
            }
            instance[instance_pos++] = (size_t)found->second;
          }
        }
      }
      break;

      case FLOAT_ARY_PARM:
      {
        if(!DeserializeByte()) {
          instance[instance_pos++] = 0;
        }
        else {
          INT_VALUE mem_id = DeserializeInt32();
          if(mem_id < 0) {
            const INT_VALUE array_size = DeserializeInt32();
            const INT_VALUE array_dim = DeserializeInt32();
            const INT_VALUE array_size_dim = DeserializeInt32();
            if(read_error || array_size < 0 || array_dim < 0 ||
               !CanRead((INT64_VALUE)array_size * (INT64_VALUE)sizeof(FLOAT_VALUE))) {
              return nullptr;
            }
            size_t* array = MemoryManager::AllocateArray(array_size * 2 + array_dim + 2, instructions::INT_TYPE,
                                                         op_stack, *stack_pos, false);

            array[0] = array_size;
            array[1] = array_dim;
            array[2] = array_size_dim;
            FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 3);
            // copy content
            memcpy(array_ptr, buffer + buffer_offset, array_size * sizeof(FLOAT_VALUE));
            buffer_offset += array_size * sizeof(FLOAT_VALUE);
#ifdef _DEBUG
            std::wcout << L"--- DESERIALIZING: float array; value=" << array << L", size=" << array_size << L" ---" << std::endl;
#endif
            // update cache
            mem_cache[-mem_id] = array;
            instance[instance_pos++] = (size_t)array;
          }
          else {
            std::map<INT_VALUE, size_t*>::iterator found = mem_cache.find(mem_id);
            if(found == mem_cache.end()) {
              return nullptr;
            }
            instance[instance_pos++] = (size_t)found->second;
          }
        }
      }
      break;

      case OBJ_ARY_PARM:
      {
        if(!DeserializeByte()) {
          instance[instance_pos++] = 0;
        }
        else {
          INT_VALUE mem_id = DeserializeInt32();
          if(mem_id < 0) {
            const INT_VALUE array_size = DeserializeInt32();
            const INT_VALUE array_dim = DeserializeInt32();
            const INT_VALUE array_size_dim = DeserializeInt32();
            if(read_error || array_size < 0 || array_dim < 0) {
              return nullptr;
            }
            size_t* array = MemoryManager::AllocateArray((size_t)(array_size + array_dim + 2), instructions::INT_TYPE,
                                                         op_stack, *stack_pos, false);
            array[0] = array_size;
            array[1] = array_dim;
            array[2] = array_size_dim;
            size_t* array_ptr = array + 3;

            // copy content
            for(int i = 0; i < array_size; i++) {
              if(!DeserializeByte()) {
                instance[instance_pos++] = 0;
              }
              else {
                ObjectDeserializer deserializer(buffer, buffer_offset, mem_cache,
                                                buffer_array_size, op_stack, stack_pos);
                array_ptr[i] = (size_t)deserializer.DeserializeObject();
                buffer_offset = deserializer.GetOffset();
                mem_cache = deserializer.GetMemoryCache();
              }
            }
            // write barrier: array may be old if deserialization triggers GC
            MemoryManager::WriteBarrier(array);
#ifdef _DEBUG
            std::wcout << L"--- DESERIALIZING: object array; value=" << array << L",  size="
              << array_size << L" ---" << std::endl;
#endif
            // update cache
            mem_cache[-mem_id] = array;
            instance[instance_pos++] = (size_t)array;
            MemoryManager::WriteBarrier(instance);
          }
          else {
            std::map<INT_VALUE, size_t*>::iterator found = mem_cache.find(mem_id);
            if(found == mem_cache.end()) {
              return nullptr;
            }
            instance[instance_pos++] = (size_t)found->second;
            MemoryManager::WriteBarrier(instance);
          }
        }
      }
      break;

      case OBJ_PARM:
      {
        if(!DeserializeByte()) {
          instance[instance_pos++] = 0;
        }
        else {
          ObjectDeserializer deserializer(buffer, buffer_offset, mem_cache, buffer_array_size, op_stack, stack_pos);
          instance[instance_pos++] = (size_t)deserializer.DeserializeObject();
          buffer_offset = deserializer.GetOffset();
          mem_cache = deserializer.GetMemoryCache();
          // write barrier: instance stores a reference to deserialized object
          MemoryManager::WriteBarrier(instance);
        }
      }
      break;

      default:
        break;
    }
  }

  return read_error ? nullptr : instance;
}

/********************************
 * SDK functions
 ********************************/
void APITools_MethodCall(size_t* op_stack, size_t *stack_pos, size_t* instance, int cls_id, int mthd_id) {
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute(op_stack, stack_pos, 0, mthd, instance, false);
    }
    else {
      std::wcerr << L">>> DLL call: Unable to locate method; id=" << mthd_id << L" <<<" << std::endl;
      exit(1);
    }
  }
  else {
    std::wcerr << L">>> DLL call: Unable to locate class; id=" << cls_id << L" <<<" << std::endl;
    exit(1);
  }
}

void APITools_MethodCall(size_t* op_stack, size_t* stack_pos, size_t* instance,
                         const wchar_t* cls_id, const wchar_t* mthd_id)
{
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute(op_stack, stack_pos, 0, mthd, instance, false);
    }
    else {
      std::wcerr << L">>> Unable to locate method; name=': " << mthd_id << L"' <<<" << std::endl;
      exit(1);
    }
  }
  else {
    std::wcerr << L">>> Unable to locate class; name='" << cls_id << L"' <<<" << std::endl;
    exit(1);
  }
}

void APITools_MethodCallId(size_t* op_stack, size_t *stack_pos, size_t* instance,
                           const int cls_id, const int mthd_id)
{
  StackClass* cls = Loader::GetProgram()->GetClass(cls_id);
  if(cls) {
    StackMethod* mthd = cls->GetMethod(mthd_id);
    if(mthd) {
      Runtime::StackInterpreter intpr;
      intpr.Execute(op_stack, stack_pos, 0, mthd, instance, false);
    }
    else {
      std::wcerr << L">>> DLL call: Unable to locate method; id=: " << mthd_id << L" <<<" << std::endl;
      exit(1);
    }
  }
  else {
    std::wcerr << L">>> DLL call: Unable to locate class; id=" << cls_id << L" <<<" << std::endl;
    exit(1);
  }
}

/********************************
 *  TrapManager class
 ********************************/
#ifdef _MODULE
StackProgram* TrapProcessor::program;
#endif

bool TrapProcessor::CreateNewObject(const std::wstring &cls_id, size_t* &op_stack, size_t* &stack_pos) {
  size_t* obj = MemoryManager::AllocateObject(cls_id.c_str(), op_stack, *stack_pos, false);
  if(obj) {
    // instance will be put on stack by method call
    const std::wstring mthd_name = cls_id + L":New:";
    APITools_MethodCall(op_stack, stack_pos, obj, cls_id.c_str(), mthd_name.c_str());
  }
  else {
    std::wcerr << L">>> Unable to load class; name='" << cls_id.c_str() << L"' <<<" << std::endl;
    return false;
  }

  return true;
}

/********************************
 * Creates a container for a method
 ********************************/
size_t* TrapProcessor::CreateMethodObject(size_t* cls_obj, StackMethod* mthd, StackProgram* program,
                                          size_t* &op_stack, size_t* &stack_pos) {
  size_t* mthd_obj = MemoryManager::AllocateObject(program->GetMethodObjectId(),
                                                   op_stack, *stack_pos,
                                                   false);
  // method and class object
  mthd_obj[0] = (size_t)mthd;
  mthd_obj[1] = (size_t)cls_obj;

  // set method name
  const std::wstring &qual_mthd_name = mthd->GetName();
  const size_t semi_qual_mthd_index = qual_mthd_name.find(':');
  if(semi_qual_mthd_index == std::wstring::npos) {
    std::wcerr << L">>> Internal error: invalid method name <<<" << std::endl;
    exit(1);
  }

  const std::wstring &semi_qual_mthd_string = qual_mthd_name.substr(semi_qual_mthd_index + 1);
  const size_t mthd_index = semi_qual_mthd_string.find(':');
  if(mthd_index == std::wstring::npos) {
    std::wcerr << L">>> Internal error: invalid method name <<<" << std::endl;
    exit(1);
  }
  const std::wstring &mthd_string = semi_qual_mthd_string.substr(0, mthd_index);
  mthd_obj[2] = (size_t)CreateStringObject(mthd_string, program, op_stack, stack_pos);

  // parse parameter string      
  int index = 0;
  const std::wstring &params_string = semi_qual_mthd_string.substr(mthd_index + 1);
  std::vector<size_t*> data_type_obj_holder;
  while(index < (int)params_string.size()) {
    size_t* data_type_obj = MemoryManager::AllocateObject(program->GetDataTypeObjectId(),
                                                          op_stack, *stack_pos,
                                                          false);
    data_type_obj_holder.push_back(data_type_obj);

    switch(params_string[index]) {
      case L'l':
        data_type_obj[0] = -1000;
        index++;
        break;

      case L'b':
        data_type_obj[0] = -999;
        index++;
        break;

      case L'i':
        data_type_obj[0] = -997;
        index++;
        break;

      case L'f':
        data_type_obj[0] = -996;
        index++;
        break;

      case L'c':
        data_type_obj[0] = -998;
        index++;
        break;

      case L'o': {
        data_type_obj[0] = -995;
        index++;
        const int start_index = index + 1;
        while(index < (int)params_string.size() && params_string[index] != L',') {
          index++;
        }
        data_type_obj[1] = (size_t)CreateStringObject(params_string.substr(start_index, index - 2),
                                                      program, op_stack, stack_pos);
      }
      break;

      case L'm':
        data_type_obj[0] = -994;
        index++;
        while(index < (int)params_string.size() && params_string[index] != L'~') {
          index++;
        }
        while(index < (int)params_string.size() && params_string[index] != L',') {
          index++;
        }
        break;

      default:
        throw std::runtime_error("Invalid method signature!");
        break;
    }

    // check array dimension
    int dimension = 0;
    while(index < (int)params_string.size() && params_string[index] == L'*') {
      dimension++;
      index++;
    }
    data_type_obj[2] = dimension;

    // match ','
    index++;
  }

  // create type array
  const long type_obj_array_size = (long)data_type_obj_holder.size();
  const long type_obj_array_dim = 1;
  size_t* type_obj_array = MemoryManager::AllocateArray(type_obj_array_size +
                                                        type_obj_array_dim + 2,
                                                        instructions::INT_TYPE, op_stack,
                                                        *stack_pos, false);
  type_obj_array[0] = type_obj_array_size;
  type_obj_array[1] = type_obj_array_dim;
  type_obj_array[2] = type_obj_array_size;
  size_t* type_obj_array_ptr = type_obj_array + 3;
  // copy types objects
  for(int i = 0; i < type_obj_array_size; i++) {
    type_obj_array_ptr[i] = (size_t)data_type_obj_holder[i];
  }
  // set type array
  mthd_obj[3] = (size_t)type_obj_array;

  return mthd_obj;
}

/********************************
 * Creates a container for a class
 ********************************/
void TrapProcessor::CreateClassObject(StackClass* cls, size_t* cls_obj, size_t* &op_stack,
                                      size_t* &stack_pos, StackProgram* program) {
  // create and set methods
  const long mthd_obj_array_size = cls->GetMethodCount();
  const long mthd_obj_array_dim = 1;
  size_t* mthd_obj_array = MemoryManager::AllocateArray(mthd_obj_array_size +
                                                        mthd_obj_array_dim + 2,
                                                        instructions::INT_TYPE, op_stack,
                                                        *stack_pos, false);

  mthd_obj_array[0] = mthd_obj_array_size;
  mthd_obj_array[1] = mthd_obj_array_dim;
  mthd_obj_array[2] = mthd_obj_array_size;

  StackMethod** methods = cls->GetMethods();
  size_t* mthd_obj_array_ptr = mthd_obj_array + 3;
  for(int i = 0; i < mthd_obj_array_size; i++) {
    size_t* mthd_obj = CreateMethodObject(cls_obj, methods[i], program, op_stack, stack_pos);
    mthd_obj_array_ptr[i] = (size_t)mthd_obj;
  }
  cls_obj[1] = (size_t)mthd_obj_array;
}

/********************************
 * Create a string instance
 ********************************/
size_t* TrapProcessor::CreateStringObject(const std::wstring &value_str, StackProgram* program,
                                          size_t* &op_stack, size_t* &stack_pos) {
  // create character array
  const long char_array_size = (long)value_str.size();
  const long char_array_dim = 1;
  size_t* char_array = MemoryManager::AllocateArray(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)), 
                                                    CHAR_ARY_TYPE, op_stack, *stack_pos, false);
  char_array[0] = char_array_size + 1;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy string
  wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
#ifdef _WIN32
  wcsncpy_s(char_array_ptr, char_array_size + 1, value_str.c_str(), char_array_size);
#else
  wcsncpy(char_array_ptr, value_str.c_str(), char_array_size);
#endif

  // create 'System.String' object instance
  size_t* str_obj = MemoryManager::AllocateObject(program->GetStringObjectId(), op_stack, *stack_pos, false);
  str_obj[0] = (size_t)char_array;
  str_obj[1] = char_array_size;
  str_obj[2] = char_array_size;

  return str_obj;
}

/********************************
 * Date/time calculations
 ********************************/
void TrapProcessor::ProcessTimerStart(size_t* &op_stack, size_t* &stack_pos)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  instance[0] = clock();
}

void TrapProcessor::ProcessTimerEnd(size_t* &op_stack, size_t* &stack_pos)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  instance[0] = clock() - (clock_t)instance[0];
}

void TrapProcessor::ProcessTimerElapsed(size_t* &op_stack, size_t* &stack_pos)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  PushFloat((double)instance[0] / (double)CLOCKS_PER_SEC, op_stack, stack_pos);
}

/********************************
 * Creates a Date object with
 * current time
 ********************************/
void TrapProcessor::ProcessCurrentTime(StackFrame* frame, bool is_gmt)
{
  time_t raw_time;
  raw_time = time(nullptr);

  struct tm* curr_time;
  const bool got_time = GetTime(curr_time, raw_time, is_gmt);

  size_t* instance = (size_t*)frame->mem[0];
  if(got_time && instance) {
    instance[0] = curr_time->tm_mday;          // day
    instance[1] = curr_time->tm_mon + 1;       // month
    instance[2] = curr_time->tm_year + 1900;   // year
    instance[3] = curr_time->tm_hour;          // hours
    instance[4] = curr_time->tm_min;           // mins
    instance[5] = curr_time->tm_sec;           // secs
    instance[6] = curr_time->tm_isdst;         // savings time
    instance[7] = curr_time->tm_wday;          // day of week
    instance[8] = is_gmt;                      // is GMT
  }
}

/********************************
 * Set a time instance
 ********************************/
void TrapProcessor::ProcessSetTime1(size_t* &op_stack, size_t* &stack_pos)
{
  // get time values
  long is_gmt = (long)PopInt(op_stack, stack_pos);
  long year = (long)PopInt(op_stack, stack_pos);
  long month = (long)PopInt(op_stack, stack_pos);
  long day = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance) {
    struct tm curr_time;
    memset(&curr_time, 0, sizeof(struct tm));

    curr_time.tm_mday = static_cast<int>(day);
    curr_time.tm_mon = static_cast<int>(month - 1);
    curr_time.tm_year = static_cast<int>(year - 1900);
    mktime(&curr_time);

    // set instance values
    instance[0] = curr_time.tm_mday;          // day
    instance[1] = curr_time.tm_mon + 1;       // month
    instance[2] = curr_time.tm_year + 1900;   // year
    instance[8] = is_gmt;                     // is GMT
  }
}

/********************************
 * Sets a time instance
 ********************************/
void TrapProcessor::ProcessSetTime2(size_t* &op_stack, size_t* &stack_pos)
{
  // get time values
  long is_gmt = (long)PopInt(op_stack, stack_pos);
  long secs = (long)PopInt(op_stack, stack_pos);
  long mins = (long)PopInt(op_stack, stack_pos);
  long hours = (long)PopInt(op_stack, stack_pos);
  long year = (long)PopInt(op_stack, stack_pos);
  long month = (long)PopInt(op_stack, stack_pos);
  long day = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance) {
    struct tm curr_time;
    memset(&curr_time, 0, sizeof(struct tm));

    // update time
    curr_time.tm_year = static_cast<int>(year - 1900);
    curr_time.tm_mon = static_cast<int>(month - 1);
    curr_time.tm_mday = static_cast<int>(day);
    curr_time.tm_hour = static_cast<int>(hours);
    curr_time.tm_min = static_cast<int>(mins);
    curr_time.tm_sec = static_cast<int>(secs);
    mktime(&curr_time);

    // set instance values
    instance[0] = curr_time.tm_mday;          // day
    instance[1] = curr_time.tm_mon + 1;       // month
    instance[2] = curr_time.tm_year + 1900;   // year
    instance[3] = curr_time.tm_hour;          // hours
    instance[4] = curr_time.tm_min;           // mins
    instance[5] = curr_time.tm_sec;           // secs
    instance[7] = curr_time.tm_wday;          // day of week
    instance[8] = is_gmt;                     // is GMT
  }
}

/********************************
 * Set a time instance
 ********************************/
void TrapProcessor::ProcessSetTime3(size_t* &op_stack, size_t* &stack_pos)
{
}

void TrapProcessor::ProcessAddTime(TimeInterval t, size_t* &op_stack, size_t* &stack_pos)
{
  INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance) {
    // calculate change in seconds
    INT64_VALUE offset;
    switch(t) {
      case DAY_TIME:
        offset = 86400 * value;
        break;

      case HOUR_TIME:
        offset = 3600 * value;
        break;

      case MIN_TIME:
        offset = 60 * value;
        break;

      default:
        offset = value;
        break;
    }

    // create time structure
    struct tm set_time;
    set_time.tm_mday = (int)instance[0];          // day
    set_time.tm_mon = (int)instance[1] - 1;       // month
    set_time.tm_year = (int)instance[2] - 1900;   // year
    set_time.tm_hour = (int)instance[3];          // hours
    set_time.tm_min = (int)instance[4];           // mins
    set_time.tm_sec = (int)instance[5];           // secs
    set_time.tm_isdst = (int)instance[6];         // savings time

    // calculate difference
    time_t raw_time = mktime(&set_time);
    const bool is_gmt = instance[8];
    if(is_gmt) {
#ifdef _WIN32
      long timezone;
      _get_timezone(&timezone);
#endif
      raw_time -= timezone;
    }
    raw_time += offset;
  
    struct tm* curr_time;
    const bool got_time = GetTime(curr_time, raw_time, is_gmt);

    // set instance values
    if(got_time) {
      instance[0] = curr_time->tm_mday;           // day
      instance[1] = curr_time->tm_mon + 1;        // month
      instance[2] = curr_time->tm_year + 1900;    // year
      instance[3] = curr_time->tm_hour;           // hours
      instance[4] = curr_time->tm_min;            // mins
      instance[5] = curr_time->tm_sec;            // secs
      instance[6] = curr_time->tm_isdst;          // savings time
      instance[7] = curr_time->tm_wday;           // day of week
      instance[8] = is_gmt;                       // is GMT
    }
  }
}

/********************************
 * Get platform string
 ********************************/
void TrapProcessor::ProcessPlatform(StackProgram* program, size_t* &op_stack, size_t* &stack_pos)
{
  const std::wstring value_str = BytesToUnicode(System::GetPlatform());
  size_t* str_obj = CreateStringObject(value_str, program, op_stack, stack_pos);
  PushInt((size_t)str_obj, op_stack, stack_pos);
}

/********************************
 * Get file owner string
 ********************************/
void TrapProcessor::ProcessFileOwner(const char* name, bool is_account,
                                     StackProgram* program, size_t* &op_stack, size_t* &stack_pos) {
  const std::wstring value_str = File::FileOwner(name, is_account);
  if(value_str.size() > 0) {
    size_t* str_obj = CreateStringObject(value_str, program, op_stack, stack_pos);
    PushInt((size_t)str_obj, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
}

/********************************
 * Get version string
 ********************************/
void TrapProcessor::ProcessVersion(StackProgram* program, size_t* &op_stack, size_t* &stack_pos)
{
  size_t* str_obj = CreateStringObject(VERSION_STRING, program, op_stack, stack_pos);
  PushInt((size_t)str_obj, op_stack, stack_pos);
}

//
// deserializes an array of objects
// 
inline size_t* TrapProcessor::DeserializeArray(ParamType type, size_t* inst, size_t* &op_stack, size_t* &stack_pos) {
  if(!DeserializeByte(inst)) {
    return nullptr;
  }

  size_t* src_array = (size_t*)inst[0];
  long dest_pos = (long)inst[1];

  if(dest_pos < (long)src_array[0]) {
    const INT64_VALUE dest_array_size64 = DeserializeInt(inst);
    const INT64_VALUE dest_array_dim64 = DeserializeInt(inst);
    const INT64_VALUE dest_array_dim_size64 = DeserializeInt(inst);
    // sizes are attacker-controlled; reject anything that doesn't fit in 32 bits
    if(dest_array_size64 < 0 || dest_array_size64 > INT32_MAX ||
       dest_array_dim64 < 0 || dest_array_dim64 > INT32_MAX ||
       dest_array_dim_size64 < 0 || dest_array_dim_size64 > INT32_MAX) {
      return nullptr;
    }
    const long dest_array_size = (long)dest_array_size64;
    const long dest_array_dim = (long)dest_array_dim64;
    const long dest_array_dim_size = (long)dest_array_dim_size64;

    size_t* dest_array;
    if(type == BYTE_ARY_PARM) {
      dest_array = MemoryManager::AllocateArray(dest_array_size + ((dest_array_dim + 2) * sizeof(size_t)),
                                                instructions::BYTE_ARY_TYPE, op_stack, *stack_pos, false);
    }
    else if(type == CHAR_ARY_PARM) {
      dest_array = MemoryManager::AllocateArray(dest_array_size + ((dest_array_dim + 2) * sizeof(size_t)),
                                                instructions::CHAR_ARY_TYPE, op_stack, *stack_pos, false);
    }
    else if(type == INT_ARY_PARM || type == OBJ_ARY_PARM) {
      dest_array = MemoryManager::AllocateArray(dest_array_size + dest_array_dim + 2,
                                                instructions::INT_TYPE, op_stack, *stack_pos, false);
    }
    else {
      dest_array = MemoryManager::AllocateArray(dest_array_size + dest_array_dim + 2,
                                                instructions::FLOAT_TYPE, op_stack, *stack_pos, false);
    }

    // read array meta data
    dest_array[0] = dest_array_size;
    dest_array[1] = dest_array_dim;
    dest_array[2] = dest_array_dim_size;

    if(type == OBJ_ARY_PARM) {
      size_t* dest_array_ptr = dest_array + 3;
      for(int i = 0; i < dest_array_size; ++i) {
        if(!DeserializeByte(inst)) {
          dest_array_ptr[i] = 0;
        }
        else {
          const long dest_pos = (long)inst[1];
          const long byte_array_dim_size = (long)src_array[2];
          const char* byte_array_ptr = ((char*)(src_array + 3) + dest_pos);

          ObjectDeserializer deserializer(byte_array_ptr, byte_array_dim_size, op_stack, stack_pos);
          dest_array_ptr[i] = (size_t)deserializer.DeserializeObject();
          inst[1] = dest_pos + deserializer.GetOffset();
        }
      }
    }
    else {
      ReadSerializedBytes(dest_array, src_array, type, inst);
    }

    return dest_array;
  }

  return nullptr;
}

//
// expand buffer
//
size_t* TrapProcessor::ExpandSerialBuffer(const long src_buffer_size, size_t* dest_buffer,
                                          size_t* inst, size_t* &op_stack, size_t* &stack_pos) {
  long dest_buffer_size = (long)dest_buffer[2];
  const long dest_pos = (long)inst[1];
  const long calc_offset = src_buffer_size + dest_pos;

  if(calc_offset >= dest_buffer_size) {
    const long dest_pos = (long)inst[1];
    while(calc_offset >= dest_buffer_size) {
      dest_buffer_size += calc_offset / 2;
    }
    // create byte array
    const long byte_array_size = dest_buffer_size;
    const long byte_array_dim = 1;
    size_t* byte_array = (size_t*)MemoryManager::AllocateArray((size_t)(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t))),
                                                               BYTE_ARY_TYPE, op_stack, *stack_pos, false);
    byte_array[0] = byte_array_size + 1;
    byte_array[1] = byte_array_dim;
    byte_array[2] = byte_array_size;

    // copy content
    char* byte_array_ptr = (char*)(byte_array + 3);
    const char* dest_buffer_ptr = (char*)(dest_buffer + 3);
    memcpy(byte_array_ptr, dest_buffer_ptr, dest_pos);

    return byte_array;
  }

  return dest_buffer;
}

void TrapProcessor::SerializeByte(char value, size_t* inst, size_t*& op_stack, size_t*& stack_pos)
{
  const long src_buffer_size = sizeof(value);
  size_t* dest_buffer = (size_t*)inst[0];

  if(dest_buffer) {
    const long dest_pos = (long)inst[1];

    // expand buffer, if needed
    dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
    inst[0] = (size_t)dest_buffer;

    // copy content
    char* dest_buffer_ptr = (char*)(dest_buffer + 3);
    memcpy(dest_buffer_ptr + dest_pos, &value, src_buffer_size);
    inst[1] = dest_pos + src_buffer_size;
  }
}

char TrapProcessor::DeserializeByte(size_t* inst)
{
  size_t* byte_array = (size_t*)inst[0];
  const long dest_pos = (long)inst[1];

  if(byte_array && dest_pos < (long)byte_array[0]) {
    const char* byte_array_ptr = (char*)(byte_array + 3);
    char value;
    memcpy(&value, byte_array_ptr + dest_pos, sizeof(value));
    inst[1] = dest_pos + sizeof(value);

    return value;
  }

  return 0;
}

void TrapProcessor::SerializeChar(wchar_t value, size_t* inst, size_t*& op_stack, size_t*& stack_pos)
{
  // convert to bytes
  std::string out;
  CharacterToBytes(value, out);
  const long src_buffer_size = (long)out.size();
  SerializeInt((INT_VALUE)out.size(), inst, op_stack, stack_pos);

  // prepare copy   
  size_t* dest_buffer = (size_t*)inst[0];
  if(dest_buffer) {
    const long dest_pos = (long)inst[1];

    // expand buffer, if needed
    dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
    inst[0] = (size_t)dest_buffer;

    // copy content
    char* dest_buffer_ptr = (char*)(dest_buffer + 3);
    memcpy(dest_buffer_ptr + dest_pos, out.c_str(), src_buffer_size);
    inst[1] = dest_pos + src_buffer_size;
  }
}

wchar_t TrapProcessor::DeserializeChar(size_t* inst)
{
  const INT64_VALUE num = DeserializeInt(inst);
  size_t* byte_array = (size_t*)inst[0];
  const long dest_pos = (long)inst[1];

  // Require the full 'num' bytes to be in range, not just the start offset
  // (num and the source buffer are attacker-controlled). Bounding num also
  // keeps 'num + 1' below from overflowing. Compare in 64-bit so a huge num
  // cannot truncate.
  if(byte_array && num >= 0 && dest_pos >= 0 && dest_pos <= (long)byte_array[0] &&
     num <= (INT64_VALUE)((long)byte_array[0] - dest_pos)) {
    const char* byte_array_ptr = (char*)(byte_array + 3);
    char* in = new char[num + 1];
    memcpy(in, byte_array_ptr + dest_pos, num);
    in[num] = '\0';

    wchar_t out = L'\0';
    BytesToCharacter(in, out);

    // clean up
    delete[] in;
    in = nullptr;

    inst[1] = dest_pos + num;

    return out;
  }

  return 0;
}

void TrapProcessor::SerializeInt(INT64_VALUE value, size_t* inst, size_t*& op_stack, size_t*& stack_pos)
{
  const long src_buffer_size = sizeof(value);
  size_t* dest_buffer = (size_t*)inst[0];

  if(dest_buffer) {
    const long dest_pos = (long)inst[1];

    // expand buffer, if needed
    dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
    inst[0] = (size_t)dest_buffer;

    // copy content
    char* dest_buffer_ptr = (char*)(dest_buffer + 3);
    memcpy(dest_buffer_ptr + dest_pos, &value, src_buffer_size);
    inst[1] = dest_pos + src_buffer_size;
  }
}

INT64_VALUE TrapProcessor::DeserializeInt(size_t* inst)
{
  size_t* byte_array = (size_t*)inst[0];
  const long dest_pos = (long)inst[1];

  // Require all sizeof(value) bytes to be in range, not just the start offset.
  if(byte_array && dest_pos >= 0 && dest_pos <= (long)byte_array[0] &&
     (long)sizeof(INT64_VALUE) <= (long)byte_array[0] - dest_pos) {
    const char* byte_array_ptr = (char*)(byte_array + 3);
    INT64_VALUE value;
    memcpy(&value, byte_array_ptr + dest_pos, sizeof(value));
    inst[1] = dest_pos + sizeof(value);

    return value;
  }

  return 0;
}

void TrapProcessor::SerializeFloat(FLOAT_VALUE value, size_t* inst, size_t*& op_stack, size_t*& stack_pos)
{
  const long src_buffer_size = sizeof(value);
  size_t* dest_buffer = (size_t*)inst[0];

  if(dest_buffer) {
    const long dest_pos = (long)inst[1];

    // expand buffer, if needed
    dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
    inst[0] = (size_t)dest_buffer;

    // copy content
    char* dest_buffer_ptr = (char*)(dest_buffer + 3);
    memcpy(dest_buffer_ptr + dest_pos, &value, src_buffer_size);
    inst[1] = dest_pos + src_buffer_size;
  }
}

FLOAT_VALUE TrapProcessor::DeserializeFloat(size_t* inst)
{
  size_t* byte_array = (size_t*)inst[0];
  const long dest_pos = (long)inst[1];

  if(byte_array && dest_pos < (long)byte_array[0]) {
    const char* byte_array_ptr = (char*)(byte_array + 3);
    FLOAT_VALUE value;
    memcpy(&value, byte_array_ptr + dest_pos, sizeof(value));
    inst[1] = dest_pos + sizeof(value);
    return value;
  }

  return 0.0;
}

/********************************
 * Serializes an object graph
 ********************************/
void TrapProcessor::SerializeObject(size_t* inst, StackFrame* frame, size_t* &op_stack, size_t* &stack_pos)
{
  size_t* obj = (size_t*)frame->mem[1];
  ObjectSerializer serializer(obj);
  std::vector<char> src_buffer = serializer.GetValues();
  const long src_buffer_size = (long)src_buffer.size();
  size_t* dest_buffer = (size_t*)inst[0];
  long dest_pos = (long)inst[1];

  // expand buffer, if needed
  dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
  inst[0] = (size_t)dest_buffer;

  // copy content
  char* dest_buffer_ptr = ((char*)(dest_buffer + 3) + dest_pos);
  for(int i = 0; i < src_buffer_size; i++, dest_pos++) {
    dest_buffer_ptr[i] = src_buffer[i];
  }
  inst[1] = dest_pos;
}

/********************************
 * Deserializes an object graph
 ********************************/
void TrapProcessor::DeserializeObject(size_t* inst, size_t* &op_stack, size_t* &stack_pos) {
  if(!DeserializeByte(inst)) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    size_t* byte_array = (size_t*)inst[0];
    const long dest_pos = (long)inst[1];
    const long byte_array_dim_size = (long)byte_array[2];
    const char* byte_array_ptr = ((char*)(byte_array + 3) + dest_pos);

    ObjectDeserializer deserializer(byte_array_ptr, byte_array_dim_size, op_stack, stack_pos);
    PushInt((size_t)deserializer.DeserializeObject(), op_stack, stack_pos);
    inst[1] = dest_pos + deserializer.GetOffset();
  }
}

/********************************
 * Handles callback traps from
 * the interpreter and JIT code
 ********************************/
bool TrapProcessor::ProcessTrap(StackProgram* program, size_t* inst,
                                size_t* &op_stack, size_t* &stack_pos, StackFrame* frame) {
  const INT64_VALUE id = (INT64_VALUE)PopInt(op_stack, stack_pos);
  switch(id) {
  case LOAD_CLS_INST_ID:
    return LoadClsInstId(program, inst, op_stack, stack_pos, frame);

  case STRING_HASH_ID:
    return HashStringId(program, inst, op_stack, stack_pos, frame);

  case LOAD_NEW_OBJ_INST:
    return LoadNewObjInst(program, inst, op_stack, stack_pos, frame);

  case LOAD_CLS_BY_INST:
    return LoadClsByInst(program, inst, op_stack, stack_pos, frame);

  case BYTES_TO_UNICODE:
    return ConvertBytesToUnicode(program, inst, op_stack, stack_pos, frame);

  case UNICODE_TO_BYTES:
    return ConvertUnicodeToBytes(program, inst, op_stack, stack_pos, frame);

  case LOAD_MULTI_ARY_SIZE:
    return LoadMultiArySize(program, inst, op_stack, stack_pos, frame);

  case CPY_CHAR_STR_ARY:
    return CpyCharStrAry(program, inst, op_stack, stack_pos, frame);

  case CPY_CHAR_STR_ARYS:
    return CpyCharStrArys(program, inst, op_stack, stack_pos, frame);

  case CPY_INT_STR_ARY:
    return CpyIntStrAry(program, inst, op_stack, stack_pos, frame);

  case CPY_BOOL_STR_ARY:
    return CpyBoolStrAry(program, inst, op_stack, stack_pos, frame);

  case CPY_BYTE_STR_ARY:
    return CpyByteStrAry(program, inst, op_stack, stack_pos, frame);

  case CPY_FLOAT_STR_ARY:
    return CpyFloatStrAry(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_BOOL:
    return StdOutBool(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_BYTE:
    return StdOutByte(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_CHAR:
    return StdOutChar(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_INT:
    return StdOutInt(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_FLOAT:
    return StdOutFloat(program, inst, op_stack, stack_pos, frame);
      
  case STD_INT_FMT:
    return StdOutIntFrmt(program, inst, op_stack, stack_pos, frame);
    
  case STD_FLOAT_FMT:
    return StdOutFloatFrmt(program, inst, op_stack, stack_pos, frame);
    
  case STD_FLOAT_PER:
    return StdOutFloatPer(program, inst, op_stack, stack_pos, frame);
    
  case STD_WIDTH:
    return StdOutWidth(program, inst, op_stack, stack_pos, frame);
    
  case STD_FILL:
    return StdOutFill(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_STRING:
    return StdOutString(program, inst, op_stack, stack_pos, frame);
    
  case STD_IN_BYTE_ARY_LEN:
    return StdInByteAryLen(program, inst, op_stack, stack_pos, frame);

  case STD_IN_CHAR_ARY_LEN:
    return StdInCharAryLen(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_BYTE_ARY_LEN:
    return StdOutByteAryLen(program, inst, op_stack, stack_pos, frame);

  case STD_OUT_CHAR_ARY_LEN:
    return StdOutCharAryLen(program, inst, op_stack, stack_pos, frame);

  case STD_IN_STRING:
    return StdInString(program, inst, op_stack, stack_pos, frame);

  case STD_FLUSH:
    return StdFlush(program, inst, op_stack, stack_pos, frame);
      
  case STD_ERR_BOOL:
    return StdErrBool(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_BYTE:
    return StdErrByte(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_CHAR:
    return StdErrChar(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_INT:
    return StdErrInt(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_FLOAT:
    return StdErrFloat(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_STRING:
    return StdErrString(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_CHAR_ARY:
    return StdErrCharAry(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_BYTE_ARY:
    return StdErrByteAry(program, inst, op_stack, stack_pos, frame);

  case STD_ERR_FLUSH:
    return StdErrFlush(program, inst, op_stack, stack_pos, frame);
      
  case ASSERT_TRUE:
    return AssertTrue(program, inst, op_stack, stack_pos, frame);

  case SYS_CMD:
    return SysCmd(program, inst, op_stack, stack_pos, frame);

  case SET_SIGNAL:
    return SetSignal(program, inst, op_stack, stack_pos, frame);

  case RAISE_SIGNAL:
    return RaiseSignal(program, inst, op_stack, stack_pos, frame);

  case  SYS_CMD_OUT:
    return SysCmdOut(program, inst, op_stack, stack_pos, frame);

  case EXIT:
    return Exit(program, inst, op_stack, stack_pos, frame);

  case GMT_TIME:
    return GmtTime(program, inst, op_stack, stack_pos, frame);

  case SYS_TIME:
    return SysTime(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_SET_1:
    return DateTimeSet1(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_SET_2:
    return DateTimeSet2(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_DAYS:
    return DateTimeAddDays(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_HOURS:
    return DateTimeAddHours(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_MINS:
    return DateTimeAddMins(program, inst, op_stack, stack_pos, frame);

  case DATE_TIME_ADD_SECS:
    return DateTimeAddSecs(program, inst, op_stack, stack_pos, frame);

  case DATE_TO_UNIX_TIME:
    return DateToUnixTime(program, inst, op_stack, stack_pos, frame);

  case DATE_FROM_UNIX_GMT_TIME:
    return DateFromUnixTime(program, inst, op_stack, stack_pos, frame);

  case DATE_FROM_UNIX_LOCAL_TIME:
    return DateFromLocalTime(program, inst, op_stack, stack_pos, frame);

  case TIMER_START:
    return TimerStart(program, inst, op_stack, stack_pos, frame);

  case TIMER_END:
    return TimerEnd(program, inst, op_stack, stack_pos, frame);

  case TIMER_ELAPSED:
    return TimerElapsed(program, inst, op_stack, stack_pos, frame);

  case GET_PLTFRM:
    return GetPltfrm(program, inst, op_stack, stack_pos, frame);

  case GET_UUID:
     return GetUuid(program, inst, op_stack, stack_pos, frame);

  case GET_VERSION:
    return GetVersion(program, inst, op_stack, stack_pos, frame);

  case GET_SYS_PROP:
    return GetSysProp(program, inst, op_stack, stack_pos, frame);

  case SET_SYS_PROP:
    return SetSysProp(program, inst, op_stack, stack_pos, frame);

  case GET_SYS_ENV:
    return GetSysEnv(program, inst, op_stack, stack_pos, frame);

  case SET_SYS_ENV:
    return SetSysEnv(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_RESOLVE_NAME:
    return SockTcpResolveName(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_HOST_NAME:
    return SockTcpHostName(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_CONNECT:
    return SockTcpConnect(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_BIND:
    return SockTcpBind(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_LISTEN:
    return SockTcpListen(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_ACCEPT:
    return SockTcpAccept(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SELECT:
     return SockTcpSelect(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_CLOSE:
    return SockTcpClose(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_STRING:
    return SockTcpOutString(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_STRING:
    return SockTcpInString(program, inst, op_stack, stack_pos, frame);

  case SOCK_IP_ERROR:
    return SockTcpError(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_KEEPALIVE:
    return SockTcpSetKeepAlive(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_NODELAY:
    return SockTcpSetNoDelay(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_RECV_TIMEOUT:
    return SockTcpSetRecvTimeout(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_SEND_TIMEOUT:
    return SockTcpSetSendTimeout(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_CONN_TIMEOUT:
    return SockTcpSetConnTimeout(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_RCVBUF:
    return SockTcpSetRcvBuf(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SET_SNDBUF:
    return SockTcpSetSndBuf(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_CONNECT:
    return SockTcpSslConnect(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_ISSUER:
    return SockTcpSslIssuer(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SUBJECT:
    return SockTcpSslSubject(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_CLOSE:
    return SockTcpSslClose(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_STRING:
    return SockTcpSslOutString(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_STRING:
    return SockTcpSslInString(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_LISTEN:
    return SockTcpSslListen(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_ACCEPT:
    return SockTcpSslAccept(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SELECT:
     return SockTcpSslSelect(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SRV_CERT:
    return SockTcpSslCertSrv(program, inst, op_stack, stack_pos, frame);
  
  case SOCK_TCP_SSL_SRV_CLOSE:
    return SockTcpSslCloseSrv(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_ERROR:
    return SockTcpSslError(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_KEEPALIVE:
    return SockTcpSslSetKeepAlive(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_NODELAY:
    return SockTcpSslSetNoDelay(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_RECV_TIMEOUT:
    return SockTcpSslSetRecvTimeout(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_SEND_TIMEOUT:
    return SockTcpSslSetSendTimeout(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_RCVBUF:
    return SockTcpSslSetRcvBuf(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_SNDBUF:
    return SockTcpSslSetSndBuf(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_MIN_TLS:
    return SockTcpSslSetMinTLS(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_SET_VERIFY_PEER:
    return SockTcpSslSetVerifyPeer(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_GET_FINGERPRINT:
    return SockTcpSslGetFingerprint(program, inst, op_stack, stack_pos, frame);

  case HTTP2_CONNECT:
    return Http2Connect(program, inst, op_stack, stack_pos, frame);

  case HTTP2_REQUEST:
    return Http2Request(program, inst, op_stack, stack_pos, frame);

  case HTTP2_CLOSE:
    return Http2Close(program, inst, op_stack, stack_pos, frame);

  case HTTP3_CONNECT:
    return Http3Connect(program, inst, op_stack, stack_pos, frame);

  case HTTP3_REQUEST:
    return Http3Request(program, inst, op_stack, stack_pos, frame);

  case HTTP3_CLOSE:
    return Http3Close(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_CONNECT:
    return SockDtlsConnect(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_ISSUER:
    return SockDtlsIssuer(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_SUBJECT:
    return SockDtlsSubject(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_CLOSE:
    return SockDtlsClose(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_OUT_STRING:
    return SockDtlsOutString(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_IN_STRING:
    return SockDtlsInString(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_LISTEN:
    return SockDtlsListen(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_ACCEPT:
    return SockDtlsAccept(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_SELECT:
    return SockDtlsSelect(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_SRV_CERT:
    return SockDtlsCertSrv(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_SRV_CLOSE:
    return SockDtlsCloseSrv(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_ERROR:
    return SockDtlsError(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_CREATE:
      return SockUdpCreate(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_BIND:
      return SockUdpBind(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_CLOSE:
      return SockUdpClose(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_IN_BYTE:
      return SockUdpInByte(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_IN_BYTE_ARY:
      return SockUdpInByteAry(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_IN_CHAR_ARY:
      return SockUdpInCharAry(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_OUT_CHAR_ARY:
      return SockUdpOutCharAry(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_OUT_BYTE:
      return SockUdpOutByte(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_OUT_BYTE_ARY:
      return SockUdpOutByteAry(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_IN_STRING:
      return SockUdpInString(program, inst, op_stack, stack_pos, frame);

    case SOCK_UDP_OUT_STRING:
      return SockUdpOutString(program, inst, op_stack, stack_pos, frame);

  case SERL_CHAR:
    return SerlChar(program, inst, op_stack, stack_pos, frame);

  case SERL_INT:
    return SerlInt(program, inst, op_stack, stack_pos, frame);

  case SERL_FLOAT:
    return SerlFloat(program, inst, op_stack, stack_pos, frame);

  case SERL_OBJ_INST:
    return SerlObjInst(program, inst, op_stack, stack_pos, frame);

  case SERL_BYTE_ARY:
    return SerlByteAry(program, inst, op_stack, stack_pos, frame);

  case SERL_CHAR_ARY:
    return SerlCharAry(program, inst, op_stack, stack_pos, frame);

  case SERL_INT_ARY:
    return SerlIntAry(program, inst, op_stack, stack_pos, frame);

  case SERL_OBJ_ARY:
    return SerlObjAry(program, inst, op_stack, stack_pos, frame);

  case SERL_FLOAT_ARY:
    return SerlFloatAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_CHAR:
    return DeserlChar(program, inst, op_stack, stack_pos, frame);

  case DESERL_INT:
    return DeserlInt(program, inst, op_stack, stack_pos, frame);

  case DESERL_FLOAT:
    return DeserlFloat(program, inst, op_stack, stack_pos, frame);

  case DESERL_OBJ_INST:
    return DeserlObjInst(program, inst, op_stack, stack_pos, frame);

  case DESERL_BYTE_ARY:
    return DeserlByteAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_CHAR_ARY:
    return DeserlCharAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_INT_ARY:
    return DeserlIntAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_OBJ_ARY:
    return DeserlObjAry(program, inst, op_stack, stack_pos, frame);

  case DESERL_FLOAT_ARY:
    return DeserlFloatAry(program, inst, op_stack, stack_pos, frame);

  case COMPRESS_ZLIB_BYTES:
    return CompressZlibBytes(program, inst, op_stack, stack_pos, frame);

  case UNCOMPRESS_ZLIB_BYTES:
    return UncompressZlibBytes(program, inst, op_stack, stack_pos, frame);

  case COMPRESS_GZIP_BYTES:
    return CompressGzipBytes (program, inst, op_stack, stack_pos, frame);

  case UNCOMPRESS_GZIP_BYTES:
    return UncompressGzipBytes (program, inst, op_stack, stack_pos, frame);

  case COMPRESS_BR_BYTES:
    return CompressBrBytes (program, inst, op_stack, stack_pos, frame);

  case UNCOMPRESS_BR_BYTES:
    return UncompressBrBytes (program, inst, op_stack, stack_pos, frame);

  case CRC32_BYTES:
    return CRC32Bytes(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_READ:
    return FileOpenRead(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_APPEND:
    return FileOpenAppend(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_WRITE:
    return FileOpenWrite(program, inst, op_stack, stack_pos, frame);

  case FILE_OPEN_READ_WRITE:
    return FileOpenReadWrite(program, inst, op_stack, stack_pos, frame);

  case FILE_CLOSE:
    return FileClose(program, inst, op_stack, stack_pos, frame);

  case FILE_FLUSH:
    return FileFlush(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_STRING:
    return FileInString(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_STRING:
    return FileOutString(program, inst, op_stack, stack_pos, frame);

  case FILE_REWIND:
    return FileRewind(program, inst, op_stack, stack_pos, frame);

  case PIPE_OPEN:
    return PipeOpen(program, inst, op_stack, stack_pos, frame);

  case PIPE_CREATE:
    return PipeCreate(program, inst, op_stack, stack_pos, frame);

  case PIPE_IN_BYTE:
    return PipeInByte(program, inst, op_stack, stack_pos, frame);

  case PIPE_OUT_BYTE:
    return PipeOutByte(program, inst, op_stack, stack_pos, frame);

  case PIPE_IN_BYTE_ARY:
    return PipeInByteAry(program, inst, op_stack, stack_pos, frame);

  case PIPE_IN_CHAR_ARY:
    return PipeInCharAry(program, inst, op_stack, stack_pos, frame);

  case PIPE_OUT_BYTE_ARY:
    return PipeOutByteAry(program, inst, op_stack, stack_pos, frame);

  case PIPE_OUT_CHAR_ARY:
    return PipeOutCharAry(program, inst, op_stack, stack_pos, frame);

  case PIPE_IN_STRING:
    return PipeInString(program, inst, op_stack, stack_pos, frame);

  case PIPE_OUT_STRING:
    return PipeOutString(program, inst, op_stack, stack_pos, frame);

  case PIPE_CLOSE:
    return PipeClose(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IS_CONNECTED:
    return SockTcpIsConnected(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_BYTE:
    return SockTcpInByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_BYTE_ARY:
    return SockTcpInByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_IN_CHAR_ARY:
    return SockTcpInCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_BYTE:
    return SockTcpOutByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_BYTE_ARY:
    return SockTcpOutByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_OUT_CHAR_ARY:
    return SockTcpOutCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_BYTE:
    return SockTcpSslInByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_BYTE_ARY:
    return SockTcpSslInByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_IN_CHAR_ARY:
    return SockTcpSslInCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_BYTE:
    return SockTcpSslOutByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_BYTE_ARY:
    return SockTcpSslOutByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_OUT_CHAR_ARY:
    return SockTcpSslOutCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_IN_BYTE:
    return SockDtlsInByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_IN_BYTE_ARY:
    return SockDtlsInByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_IN_CHAR_ARY:
    return SockDtlsInCharAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_OUT_BYTE:
    return SockDtlsOutByte(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_OUT_BYTE_ARY:
    return SockDtlsOutByteAry(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_OUT_CHAR_ARY:
    return SockDtlsOutCharAry(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_BYTE:
    return FileInByte(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_CHAR_ARY:
    return FileInCharAry(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_BYTE_ARY:
    return FileInByteAry(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_BYTE:
    return FileOutByte(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_BYTE_ARY:
    return FileOutByteAry(program, inst, op_stack, stack_pos, frame);

  case FILE_OUT_CHAR_ARY:
    return FileOutCharAry(program, inst, op_stack, stack_pos, frame);

  case FILE_SEEK:
    return FileSeek(program, inst, op_stack, stack_pos, frame);

  case FILE_EOF:
    return FileEof(program, inst, op_stack, stack_pos, frame);

  case FILE_IS_OPEN:
    return FileIsOpen(program, inst, op_stack, stack_pos, frame);

  case FILE_EXISTS:
    return FileExists(program, inst, op_stack, stack_pos, frame);

  case FILE_CAN_WRITE_ONLY:
    return FileCanWriteOnly(program, inst, op_stack, stack_pos, frame);

  case FILE_CAN_READ_ONLY:
    return FileCanReadOnly(program, inst, op_stack, stack_pos, frame);

  case FILE_CAN_READ_WRITE:
    return FileCanReadWrite(program, inst, op_stack, stack_pos, frame);

  case FILE_SIZE:
    return FileSize(program, inst, op_stack, stack_pos, frame);

  case FILE_FULL_PATH:
    return FileFullPath(program, inst, op_stack, stack_pos, frame);

  case FILE_TEMP_NAME:
    return FileTempName(program, inst, op_stack, stack_pos, frame);

  case FILE_ACCOUNT_OWNER:
    return FileAccountOwner(program, inst, op_stack, stack_pos, frame);

  case FILE_GROUP_OWNER:
    return FileGroupOwner(program, inst, op_stack, stack_pos, frame);

  case FILE_DELETE:
    return FileDelete(program, inst, op_stack, stack_pos, frame);

  case FILE_RENAME:
    return FileRename(program, inst, op_stack, stack_pos, frame);

  case FILE_COPY:
    return FileCopy(program, inst, op_stack, stack_pos, frame);

  case FILE_CREATE_TIME:
    return FileCreateTime(program, inst, op_stack, stack_pos, frame);

  case FILE_MODIFIED_TIME:
    return FileModifiedTime(program, inst, op_stack, stack_pos, frame);

  case FILE_ACCESSED_TIME:
    return FileAccessedTime(program, inst, op_stack, stack_pos, frame);

  case DIR_CREATE:
    return DirCreate(program, inst, op_stack, stack_pos, frame);

  case DIR_SLASH:
    return DirSlash(program, inst, op_stack, stack_pos, frame);

  case DIR_EXISTS:
    return DirExists(program, inst, op_stack, stack_pos, frame);

  case DIR_LIST:
    return DirList(program, inst, op_stack, stack_pos, frame);

  case DIR_DELETE:
    return DirDelete(program, inst, op_stack, stack_pos, frame);

  case DIR_COPY:
    return DirCopy(program, inst, op_stack, stack_pos, frame);

  case DIR_GET_CUR:
    return DirGetCur(program, inst, op_stack, stack_pos, frame);

  case DIR_SET_CUR:
    return DirSetCur(program, inst, op_stack, stack_pos, frame);

  case SYM_LINK_CREATE:
    return SymLinkCreate(program, inst, op_stack, stack_pos, frame);

  case SYM_LINK_COPY:
    return SymLinkCopy(program, inst, op_stack, stack_pos, frame);

  case SYM_LINK_LOC:
    return SymLinkLoc(program, inst, op_stack, stack_pos, frame);

  case SYM_LINK_EXISTS:
    return SymLinkExists(program, inst, op_stack, stack_pos, frame);

  case HARD_LINK_CREATE:
    return HardLinkCreate(program, inst, op_stack, stack_pos, frame);
  }

  return false;
}

bool TrapProcessor::HashStringId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* str_obj = (size_t*)PopInt(op_stack, stack_pos);

  size_t* char_ary_obj = (size_t*)str_obj[0];
  const wchar_t* char_ary = (wchar_t*)(char_ary_obj + 3);
  const size_t char_ary_pos = str_obj[2];

  const size_t hash = HashString(char_ary, char_ary_pos);
  PushInt(hash, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::LoadClsInstId(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* obj = (size_t*)PopInt(op_stack, stack_pos);
  PushInt(MemoryManager::GetObjectID(obj), op_stack, stack_pos);
  return true;
}

bool TrapProcessor::LoadNewObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const wchar_t* name = (wchar_t*)(array + 3);
#ifdef _DEBUG
    std::wcout << L"stack oper: LOAD_NEW_OBJ_INST; name='" << name << L"'" << std::endl;
#endif
    return CreateNewObject(name, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::LoadClsByInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"stack oper: LOAD_CLS_BY_INST" << std::endl;
#endif

  StackClass* cls = MemoryManager::GetClass(inst);
  if(!cls) {
    std::wcerr << L">>> Internal error: looking up class instance " << inst << L" <<<" << std::endl;
    return false;
  }
  // set name and create 'Class' instance
  size_t* cls_obj = MemoryManager::AllocateObject(program->GetClassObjectId(), op_stack, *stack_pos, false);
  cls_obj[0] = (size_t)CreateStringObject(cls->GetName(), program, op_stack, stack_pos);
  frame->mem[1] = (size_t)cls_obj;
  CreateClassObject(cls, cls_obj, op_stack, stack_pos, program);

  return true;
}

bool TrapProcessor::ConvertBytesToUnicode(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }
  const std::wstring out = BytesToUnicode((char*)(array + 3));

  // create character array
  const long char_array_size = (long)out.size();
  const long char_array_dim = 1;
  size_t* char_array = MemoryManager::AllocateArray(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)), 
                                                    CHAR_ARY_TYPE, op_stack, *stack_pos, false);
  char_array[0] = char_array_size + 1;
  char_array[1] = char_array_dim;
  char_array[2] = char_array_size;

  // copy string
  wchar_t* char_array_ptr = (wchar_t*)(char_array + 3);
#ifdef _WIN32
  wcsncpy_s(char_array_ptr, char_array_size + 1, out.c_str(), char_array_size);
#else
  wcsncpy(char_array_ptr, out.c_str(), char_array_size);
#endif

  // push result
  PushInt((size_t)char_array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::ConvertUnicodeToBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }
  const std::string out = UnicodeToBytes((wchar_t*)(array + 3));

  // create byte array
  const long byte_array_size = (long)out.size();
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)),
                                                    BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;                                                                                                                                                       
  byte_array[2] = byte_array_size;

  // copy bytes
  char* byte_array_ptr = (char*)(byte_array + 3);
#ifdef _WIN32
  strncpy_s(byte_array_ptr, byte_array_size + 1, out.c_str(), byte_array_size);
#else
  strncpy(byte_array_ptr, out.c_str(), byte_array_size);
#endif
  
  // push result
  PushInt((size_t)byte_array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::LoadMultiArySize(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // allocate 'size' array and copy metadata
  long size = (long)array[1];
  long dim = 1;
  size_t* mem = MemoryManager::AllocateArray(size + dim + 2, instructions::INT_TYPE,
                                             op_stack, *stack_pos);
  int i, j;
  for(i = 0, j = static_cast<int>(size + 2); i < size; i++) {
    mem[i + 3] = array[--j];
  }
  mem[0] = size;
  mem[1] = dim;
  mem[2] = size;

  PushInt((size_t)mem, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::CpyCharStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE index = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const wchar_t* value_str = program->GetCharStrings()[index];
  // copy array
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    return false;
  }
  const long size = (long)array[2];
  wchar_t* str = (wchar_t*)(array + 3);
  memcpy(str, value_str, size * sizeof(wchar_t));
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_CHAR_STR_ARY: index=" << index << L", std::string='" << str << L"'" << std::endl;
#endif
  PushInt((size_t)array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::CpyCharStrArys(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  // copy array
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    return false;
  }
  const long size = (long)array[0];
  const long dim = (long)array[1];
  // copy elements
  size_t* str = (size_t*)(array + dim + 2);
  for(long i = 0; i < size; i++) {
    str[i] = PopInt(op_stack, stack_pos);
  }
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_CHAR_STR_ARYS" << std::endl;
#endif
  PushInt((size_t)array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::CpyBoolStrAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  INT64_VALUE index = (INT64_VALUE)PopInt(op_stack, stack_pos);
  bool* value_str = program->GetBoolStrings()[index];
  // copy array
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    return false;
  }
  const long size = (long)array[0];
  const long dim = (long)array[1];
  bool* str = (bool*)(array + dim + 2);
  for(long i = 0; i < size; i++) {
    str[i] = value_str[i];
  }

#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_Bool_STR_ARY" << std::endl;
#endif
  PushInt((size_t)array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::CpyByteStrAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  INT64_VALUE index = (INT64_VALUE)PopInt(op_stack, stack_pos);
  char* value_str = program->GetByteStrings()[index];
  // copy array
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    return false;
  }
  const long size = (long)array[0];
  const long dim = (long)array[1];
  char* str = (char*)(array + dim + 2);
  for(long i = 0; i < size; i++) {
    str[i] = value_str[i];
  }

#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_Bool_STR_ARY" << std::endl;
#endif
  PushInt((size_t)array, op_stack, stack_pos);

  return true;
}


bool TrapProcessor::CpyIntStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE index = (INT64_VALUE)PopInt(op_stack, stack_pos);
  INT64_VALUE* value_str = program->GetIntStrings()[index];
  // copy array
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    return false;
  }
  const long size = (long)array[0];
  const long dim = (long)array[1];
  size_t* str = (size_t*)(array + dim + 2);
  for(long i = 0; i < size; i++) {
    str[i] = value_str[i];
  }
#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_INT_STR_ARY" << std::endl;
#endif
  PushInt((size_t)array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::CpyFloatStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE index = (INT64_VALUE)PopInt(op_stack, stack_pos);
  FLOAT_VALUE* value_str = program->GetFloatStrings()[index];
  // copy array
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory element <<<" << std::endl;
    return false;
  }
  const long size = (long)array[0];
  const long dim = (long)array[1];
  FLOAT_VALUE* str = (FLOAT_VALUE*)(array + dim + 2);
  for(long i = 0; i < size; i++) {
    str[i] = value_str[i];
  }

#ifdef _DEBUG
  std::wcout << L"stack oper: CPY_FLOAT_STR_ARY" << std::endl;
#endif
  PushInt((size_t)array, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_FLUSH" << std::endl;
#endif
  fflush(stdout);

  return true;
}

bool TrapProcessor::StdOutBool(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_BOOL" << std::endl;
#endif
  
#ifdef _MODULE_STDIO
  program->output_buffer << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");
#else
  std::wcout << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");
#endif

  return true;
}

bool TrapProcessor::StdOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_BYTE" << std::endl;
#endif

  std::ios_base::fmtflags flags(std::wcout.flags());
#ifdef _MODULE_STDIO
  program->output_buffer << std::hex << L"0x" << ((unsigned char)PopInt(op_stack, stack_pos));
#else
  std::wcout << std::hex << L"0x" << ((unsigned char)PopInt(op_stack, stack_pos));
#endif
  std::wcout.flags(flags);

  return true;
}

bool TrapProcessor::StdOutChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_CHAR" << std::endl;
#endif

  const size_t value = PopInt(op_stack, stack_pos);

#ifdef _WIN32
  // On Windows, wchar_t is 16-bit; codepoints above U+FFFF need a surrogate pair
  if(value > 0xFFFF) {
    const size_t cp = value - 0x10000;
    wchar_t surrogates[3];
    surrogates[0] = (wchar_t)(0xD800 + (cp >> 10));
    surrogates[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
    surrogates[2] = L'\0';
#ifdef _MODULE_STDIO
    program->output_buffer << surrogates;
#else
    WinWriteWide(GetStdHandle(STD_OUTPUT_HANDLE), std::wcout, surrogates, 2);
#endif
  }
  else {
    wchar_t ch[2] = { (wchar_t)value, L'\0' };
#ifdef _MODULE_STDIO
    program->output_buffer << ch[0];
#else
    WinWriteWide(GetStdHandle(STD_OUTPUT_HANDLE), std::wcout, ch, 1);
#endif
  }
#else
#ifdef _MODULE_STDIO
  program->output_buffer << (wchar_t)value;
#else
  std::wcout << (wchar_t)value;
#endif
#endif

  return true;
}

bool TrapProcessor::StdOutInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_INT" << std::endl;
#endif

#ifdef _MODULE_STDIO
  program->output_buffer << (INT64_VALUE)PopInt(op_stack, stack_pos);
#else
  std::wcout << (INT64_VALUE)PopInt(op_stack, stack_pos);
#endif

  return true;
}

bool TrapProcessor::StdOutFloat(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_FLOAT" << std::endl;
#endif

  const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
#ifdef _MODULE_STDIO
  program->output_buffer << value;
#else
  std::wcout << value;
#endif

  return true;
}

bool TrapProcessor::StdOutIntFrmt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_INT_FMT" << std::endl;
#endif

  const INT64_VALUE std_format = (INT64_VALUE)PopInt(op_stack, stack_pos);
  switch(std_format) {
    // DEC
    // DEFAULT
  case -17:
#ifdef _MODULE_STDIO
    program->output_buffer << std::dec;
#else
    std::wcout << std::dec;
#endif
    break;

    // HEX
  case -18:
#ifdef _MODULE_STDIO
    program->output_buffer << std::hex;
#else
    std::wcout << std::hex;
#endif
    break;

    // OCT
  case -16:
#ifdef _MODULE_STDIO
    program->output_buffer << std::oct;
#else
    std::wcout << std::oct;
#endif
    break;

  default:
    break;
  }

  return true;
}

bool TrapProcessor::StdOutFloatFrmt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_FLOAT" << std::endl;
#endif

  const INT64_VALUE std_format = (INT64_VALUE)PopInt(op_stack, stack_pos);
  switch(std_format) {
    // FIXED
    // DEFAULT
  case -20:
#ifdef _MODULE_STDIO
    program->output_buffer << std::fixed;
#else
    std::wcout << std::fixed;
#endif
    break;

    // SCIENTIFIC
  case -19:
#ifdef _MODULE_STDIO
    program->output_buffer << std::scientific;
#else
    std::wcout << std::scientific;
#endif
    break;

    // HEX
  case -18:
#ifdef _MODULE_STDIO
    program->output_buffer << std::hexfloat;
#else
    std::wcout << std::hexfloat;
#endif
    break;

  default:
    break;
  }

  return true;
}

bool TrapProcessor::StdOutFloatPer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_FLOAT_PER" << std::endl;
#endif

  const size_t std_per = PopInt(op_stack, stack_pos);
#ifdef _MODULE_STDIO
  program->output_buffer << std::setprecision(std_per);
#else
  std::wcout << std::setprecision(static_cast<int>(std_per));
#endif

  return true;
}

bool TrapProcessor::StdOutWidth(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_WIDTH" << std::endl;
#endif

  const size_t std_width = PopInt(op_stack, stack_pos);
#ifdef _MODULE_STDIO
  program->output_buffer << std::setw(std_width);
#else
  std::wcout << std::setw(static_cast<int>(std_width));
#endif

  return true;
}

bool TrapProcessor::StdOutFill(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_FILL" << std::endl;
#endif

  const wchar_t std_fill = (wchar_t)PopInt(op_stack, stack_pos);
#ifdef _MODULE_STDIO
  program->output_buffer << std::setfill(std_fill);
#else
  std::wcout << std::setfill(std_fill);
#endif

  return true;
}

bool TrapProcessor::StdOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
#ifdef _DEBUG
  std::wcout << L"  STD_OUT_STRING: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array) {
    wchar_t* str = (wchar_t*)(array + 3);
#ifdef _MODULE_STDIO
    program->output_buffer << str;
#elif defined(_WIN32)
    WinWriteWide(GetStdHandle(STD_OUTPUT_HANDLE), std::wcout, str, wcslen(str));
#else
    std::wcout << str;
#endif
  }
  else {
#ifdef _MODULE_STDIO
    program->output_buffer << L"Nil";
#elif defined(_WIN32)
    WinWriteWide(GetStdHandle(STD_OUTPUT_HANDLE), std::wcout, L"Nil", 3);
#else
    std::wcout << L"Nil";
#endif
  }

  return true;
}

bool TrapProcessor::StdInByteAryLen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_IN_BYTE_ARY_LEN: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    char* buffer = (char*)(array + 3);
    PushInt(fread(buffer + offset, num, 1, stdin), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdInCharAryLen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_IN_CHAR_ARY: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    wchar_t* buffer = (wchar_t*)(array + 3);
    // allocate temporary buffer
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry): the temp
    // is sized for 'num' and offset was checked against the destination, so a
    // large offset overflowed the heap allocation.
    size_t read = fread(byte_buffer, 1, num, stdin);
    if(read) {
      byte_buffer[read] = '\0';
      std::wstring in = BytesToUnicode(byte_buffer);
#ifdef _WIN32
      wcsncpy_s(buffer, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(buffer, in.c_str(), in.size());
#endif
      PushInt(in.size(), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    std::wstring wbuffer;
    if(Runtime::StackInterpreter::IsBinaryStdio()) {
      std::string buffer;
      std::getline(std::cin, buffer);
      wbuffer = BytesToUnicode(buffer);
    }
    else {
      std::getline(std::wcin, wbuffer);
    }

    // copy to dest
    wchar_t* dest = (wchar_t*)(array + 3);
#ifdef _WIN32
    wcsncpy_s(dest, array[0] + 1, wbuffer.c_str(), wbuffer.size());
#else
    wcsncpy(dest, wbuffer.c_str(), wbuffer.size());
#endif
  }

  return true;
}

bool TrapProcessor::StdOutByteAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_OUT_BYTE_ARY_LEN: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    const char* buffer = (char*)(array + 3);
#ifdef _MODULE_STDIO
    const std::wstring wide_buffer(BytesToUnicode(buffer));
    program->output_buffer.write(wide_buffer.c_str(), wide_buffer.size());
#else
    PushInt(fwrite(buffer, 1, num, stdout), op_stack, stack_pos);
#endif
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdOutCharAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_OUT_STRING: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
#ifdef _MODULE_STDIO
    std::wstring wide_buffer((wchar_t*)(array + 3) + offset);
    program->output_buffer.write(wide_buffer.c_str(), wide_buffer.size());
#else
    std::string buffer = UnicodeToBytes((wchar_t*)(array + 3) + offset);
    PushInt(fwrite(buffer.c_str(), 1, buffer.size(), stdout), op_stack, stack_pos);
#endif
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdErrFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_ERR_FLUSH" << std::endl;
#endif
  
  fflush(stderr);
  return true;
}

bool TrapProcessor::StdErrBool(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_ERR_BOOL" << std::endl;
#endif
  std::wcerr << ((PopInt(op_stack, stack_pos) == 0) ? L"false" : L"true");

  return true;
}

bool TrapProcessor::StdErrByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_ERR_BYTE" << std::endl;
#endif
  std::wcerr << (unsigned char)PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdErrChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_ERR_CHAR" << std::endl;
#endif

  const size_t value = PopInt(op_stack, stack_pos);

#ifdef _WIN32
  // On Windows, wchar_t is 16-bit; codepoints above U+FFFF need a surrogate pair
  if(value > 0xFFFF) {
    const size_t cp = value - 0x10000;
    wchar_t surrogates[3];
    surrogates[0] = (wchar_t)(0xD800 + (cp >> 10));
    surrogates[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
    surrogates[2] = L'\0';
    WinWriteWide(GetStdHandle(STD_ERROR_HANDLE), std::wcerr, surrogates, 2);
  }
  else {
    wchar_t ch[2] = { (wchar_t)value, L'\0' };
    WinWriteWide(GetStdHandle(STD_ERROR_HANDLE), std::wcerr, ch, 1);
  }
#else
  std::wcerr << (wchar_t)value;
#endif

  return true;
}

bool TrapProcessor::StdErrInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_ERR_INT" << std::endl;
#endif
  std::wcerr << PopInt(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::StdErrFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"  STD_ERR_FLOAT" << std::endl;
#endif

  const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
  const std::wstring precision = program->GetProperty(L"precision");
  if(precision.size() > 0) {
    std::ios_base::fmtflags flags(std::wcout.flags());
    std::streamsize ss = std::wcout.precision();
    
    if(precision == L"fixed") {
      std::wcerr << std::fixed;
    }
    else if(precision == L"scientific") {
      std::wcerr << std::scientific;
    }
    else {
      std::wcerr << std::setprecision(static_cast<int>(stoll(precision)));
    }
    
    std::wcerr << value;
    std::cout.precision (ss);
    std::cout.flags(flags);
  }
  else {
    std::wcerr << std::setprecision(6) << value;
  }
  
  return true;
}

bool TrapProcessor::StdErrString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_ERR_STRING: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array) {
    const wchar_t* str = (wchar_t*)(array + 3);
#ifdef _WIN32
    WinWriteWide(GetStdHandle(STD_ERROR_HANDLE), std::wcerr, str, wcslen(str));
#else
    std::wcerr << str;
#endif
  }
  else {
    std::wcerr << L"Nil";
  }

  return true;
}

bool TrapProcessor::StdErrCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_ERR_BYTE_ARY: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array && offset > -1 && offset + num <= (long)array[2]) {
    const wchar_t* buffer = (wchar_t*)(array + 3);
    std::wcerr.write(buffer + offset, num);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    std::wcerr << L"Nil";
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::StdErrByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);

#ifdef _DEBUG
  std::wcout << L"  STD_ERR_BYTE_ARY: addr=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif

  if(array && offset > -1 && offset + num <= (long)array[2]) {
    const char* buffer = (char*)(array + 3);
    std::cerr.write(buffer + offset, num);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    std::wcerr << L"Nil";
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::AssertTrue(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  if(!PopInt(op_stack, stack_pos)) {
    std::wcerr << L">>> Assert failed! <<<" << std::endl;
    return false;
  }
  
  return true;
}

bool TrapProcessor::SysCmd(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  array = (size_t*)array[0];
  if(array) {
    const std::string cmd = UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(system(cmd.c_str()), op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SetSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const long mthd_cls_id = (long)PopInt(op_stack, stack_pos);
  const long signal_id = (long)PopInt(op_stack, stack_pos);

  const long cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
  const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;

  StackMethod* signal_mthd = Loader::GetProgram()->GetClass(cls_id)->GetMethod(mthd_id);
  if(signal_mthd) {
    return program->AddSignalHandler(signal_id, signal_mthd);
  }
  
  return false;
}

bool TrapProcessor::RaiseSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE signal_id = (INT64_VALUE)PopInt(op_stack, stack_pos);

  switch(signal_id) {
  case VM_SIGABRT:
    std::raise(SIGABRT);
    break;

  case VM_SIGFPE:
    std::raise(SIGFPE);
    break;

  case VM_SIGILL:
    std::raise(SIGILL);
    break;

  case VM_SIGINT:
    std::raise(SIGINT);
    break;

  case VM_SIGSEGV:
    std::raise(SIGSEGV);
    break;

  case VM_SIGTERM:
    std::raise(SIGTERM);
    break;

    default:
      return false;
  }

  return true;
}

bool TrapProcessor::SysCmdOut(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  std::map<std::string, std::string> prev_env_variables;

  // save existing environment variables
  size_t* env_array = (size_t*)PopInt(op_stack, stack_pos);
  if(env_array) {
    const size_t env_str_count = env_array[2];
    for(size_t i = 0; i < env_str_count; ++i) {
      size_t* env_str_obj = (size_t*)env_array[i + 3];
      env_str_obj = (size_t*)*env_str_obj;
      const std::string env_str = UnicodeToBytes((wchar_t*)(env_str_obj + 3));
      const size_t index = env_str.find('=');
      if(index != std::string::npos) {
        const std::string name = env_str.substr(0, index);
        const std::string value = env_str.substr(index + 1);

#ifdef _WIN32
        size_t prev_value_len; char prev_value[SMALL_BUFFER_MAX];
        if(!getenv_s(&prev_value_len, prev_value, SMALL_BUFFER_MAX, name.c_str()) && strlen(prev_value) > 0) {
          prev_env_variables[name] = prev_value;
        }
        else {
          prev_env_variables[name] = "";
          _putenv_s(name.c_str(), value.c_str());
        }
#else
        const char* prev_value = getenv(name.c_str());
        if(prev_value) {
          prev_env_variables[name] = prev_value;
        }
        else {
          prev_env_variables[name] = "";
          setenv(name.c_str(), value.c_str(), 1);
        }
#endif
      }
    }
  }

  size_t* command_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* str_array = (size_t*)command_obj[0];
  if(str_array) {
    const std::string cmd = UnicodeToBytes((wchar_t*)(str_array + 3));

    int status = 0;
    std::vector<std::string> output_lines = System::CommandOutput(cmd.c_str(), status);
    
    // create 'System.String' object array
    const long str_obj_array_size = (long)output_lines.size();
    const long str_obj_array_dim = 1;
    size_t* str_obj_array = MemoryManager::AllocateArray(str_obj_array_size + str_obj_array_dim + 2, instructions::INT_TYPE, op_stack, *stack_pos, false);
    str_obj_array[0] = str_obj_array_size;
    str_obj_array[1] = str_obj_array_dim;
    str_obj_array[2] = str_obj_array_size;
    size_t* str_obj_array_ptr = str_obj_array + 3;

    // create and assign 'System.String' instances to array
    for(size_t i = 0; i < output_lines.size(); ++i) {
      const std::wstring line = BytesToUnicode(output_lines[i]);
      str_obj_array_ptr[i] = (size_t)CreateStringObject(line, program, op_stack, stack_pos);
    }


    size_t* command_output_obj = MemoryManager::AllocateObject(program->GetCommandOutputObjectId(), op_stack, *stack_pos, false);
    command_output_obj[0] = (size_t)command_obj;
    command_output_obj[1] = status;
    command_output_obj[2] = (size_t)str_obj_array;

    PushInt((size_t)command_output_obj, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  // restore existing environment variables 
  std::map<std::string, std::string>::iterator iter;
  for(iter = prev_env_variables.begin(); iter != prev_env_variables.end(); ++iter) {
#ifdef _WIN32
    _putenv_s(iter->first.c_str(), iter->second.c_str());
#else
    setenv(iter->first.c_str(), iter->second.c_str(), 1);
#endif
  }

  return true;
}

bool TrapProcessor::Exit(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  exit((int)PopInt(op_stack, stack_pos));

  return true;
}

bool TrapProcessor::GmtTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessCurrentTime(frame, true);

  return true;
}

bool TrapProcessor::SysTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessCurrentTime(frame, false);

  return true;
}

bool TrapProcessor::DateTimeSet1(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessSetTime1(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeSet2(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessSetTime2(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddDays(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(DAY_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddHours(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(HOUR_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddMins(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(MIN_TIME, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DateTimeAddSecs(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessAddTime(SEC_TIME, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::DateToUnixTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    // create time structure
    struct tm set_time;
    set_time.tm_mday = (int)instance[0];          // day
    set_time.tm_mon = (int)instance[1] - 1;       // month
    set_time.tm_year = (int)instance[2] - 1900;   // year
    set_time.tm_hour = (int)instance[3];          // hours
    set_time.tm_min = (int)instance[4];           // mins
    set_time.tm_sec = (int)instance[5];           // secs
    set_time.tm_isdst = (int)instance[6];         // savings time

    // calculate difference
    const time_t raw_time = mktime(&set_time);
    PushInt((size_t)raw_time, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DateFromUnixTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  time_t value = (time_t)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance) {
#ifdef _WIN32
    struct tm set_time;
    if(gmtime_s(&set_time, &value)) {
      std::wcerr << L">>> Unable to get GMT time <<<" << std::endl;
      return false;
    }

    instance[0] = set_time.tm_mday;          // day
    instance[1] = set_time.tm_mon + 1;       // month
    instance[2] = set_time.tm_year + 1900;   // year
    instance[3] = set_time.tm_hour;          // hours
    instance[4] = set_time.tm_min;           // mins
    instance[5] = set_time.tm_sec;           // secs
    instance[6] = set_time.tm_isdst;         // savings time
    instance[7] = set_time.tm_wday;          // day of week
    instance[8] = true;                      // is GMT
#else
    struct tm* set_time = gmtime(&value);
    instance[0] = set_time->tm_mday;          // day
    instance[1] = set_time->tm_mon + 1;       // month
    instance[2] = set_time->tm_year + 1900;   // year
    instance[3] = set_time->tm_hour;          // hours
    instance[4] = set_time->tm_min;           // mins
    instance[5] = set_time->tm_sec;           // secs
    instance[6] = set_time->tm_isdst;         // savings time
    instance[7] = set_time->tm_wday;          // day of week
    instance[8] = true;                       // is GMT
#endif
  }

  return true;
}

bool TrapProcessor::DateFromLocalTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  time_t value = (time_t)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if (instance) {
#ifdef _WIN32
    struct tm set_time;
    if (localtime_s(&set_time, &value)) {
      std::wcerr << L">>> Unable to get local time <<<" << std::endl;
      return false;
    }

    instance[0] = set_time.tm_mday;          // day
    instance[1] = set_time.tm_mon + 1;       // month
    instance[2] = set_time.tm_year + 1900;   // year
    instance[3] = set_time.tm_hour;          // hours
    instance[4] = set_time.tm_min;           // mins
    instance[5] = set_time.tm_sec;           // secs
    instance[6] = set_time.tm_isdst;         // savings time
    instance[7] = set_time.tm_wday;          // day of week
    instance[8] = false;                      // is GMT
#else
    struct tm* set_time = localtime(&value);
    instance[0] = set_time->tm_mday;          // day
    instance[1] = set_time->tm_mon + 1;       // month
    instance[2] = set_time->tm_year + 1900;   // year
    instance[3] = set_time->tm_hour;          // hours
    instance[4] = set_time->tm_min;           // mins
    instance[5] = set_time->tm_sec;           // secs
    instance[6] = set_time->tm_isdst;         // savings time
    instance[7] = set_time->tm_wday;          // day of week
    instance[8] = false;                       // is GMT
#endif
  }

  return true;
}

bool TrapProcessor::TimerStart(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessTimerStart(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::TimerEnd(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessTimerEnd(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::TimerElapsed(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessTimerElapsed(op_stack, stack_pos);

  return true;
}

bool TrapProcessor::GetPltfrm(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessPlatform(program, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::GetUuid(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
   size_t* str_obj = CreateStringObject(uuidv4(), program, op_stack, stack_pos);
   PushInt((size_t)str_obj, op_stack, stack_pos);

   return true;
}

bool TrapProcessor::GetVersion(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  ProcessVersion(program, op_stack, stack_pos);

  return true;
}

// Resident set size (physical memory) of this process, in bytes; 0 if unavailable.
static size_t GetProcessResidentBytes()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc;
  pmc.cb = sizeof(pmc);
  if(K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return (size_t)pmc.WorkingSetSize;
  }
  return 0;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
    return (size_t)info.resident_size;
  }
  return 0;
#else
  size_t rss = 0;
  FILE* fp = fopen("/proc/self/statm", "r");
  if(fp) {
    long pages = 0;
    if(fscanf(fp, "%*s %ld", &pages) == 1) {
      rss = (size_t)pages * (size_t)sysconf(_SC_PAGESIZE);
    }
    fclose(fp);
  }
  return rss;
#endif
}

// Total CPU time (user + kernel) consumed by this process, in milliseconds; the
// caller samples it over an interval to derive a CPU-usage percentage.
static size_t GetProcessCpuTimeMs()
{
#ifdef _WIN32
  FILETIME ftCreate, ftExit, ftKernel, ftUser;
  if(GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser)) {
    ULARGE_INTEGER k, u;
    k.LowPart = ftKernel.dwLowDateTime; k.HighPart = ftKernel.dwHighDateTime;
    u.LowPart = ftUser.dwLowDateTime;   u.HighPart = ftUser.dwHighDateTime;
    return (size_t)((k.QuadPart + u.QuadPart) / 10000ULL);  // 100-ns units -> ms
  }
  return 0;
#else
  struct rusage ru;
  if(getrusage(RUSAGE_SELF, &ru) == 0) {
    return (size_t)ru.ru_utime.tv_sec * 1000 + (size_t)ru.ru_utime.tv_usec / 1000 +
           (size_t)ru.ru_stime.tv_sec * 1000 + (size_t)ru.ru_stime.tv_usec / 1000;
  }
  return 0;
#endif
}

// Live runtime/GC/CPU stats, surfaced through the 'runtime.*' property namespace so
// Objeck code can read them via Runtime->GetProperty(...) with no new bytecode trap.
// Returns false (and leaves 'out' untouched) for keys it doesn't handle, so the
// caller falls back to the normal program property store.
static bool GetRuntimeStat(const std::wstring& key, std::wstring& out)
{
  if(key.rfind(L"runtime.", 0) != 0) {
    return false;
  }

  // Table-driven dispatch: one row per key, each a non-capturing lambda (a plain
  // function pointer). std::wstring can't be switch()ed, and this keeps the
  // val=…/to_wstring boilerplate in one place and adding a metric to one row.
  static const struct { const wchar_t* key; size_t (*fn)(); } kStats[] = {
    { L"runtime.memory.used",      []() -> size_t { return GetProcessResidentBytes(); } },
    { L"runtime.memory.allocated", []() -> size_t { return MemoryManager::GetHeapAllocatedSize(); } },
    { L"runtime.memory.max",       []() -> size_t { return MemoryManager::GetHeapMaxSize(); } },
    { L"runtime.memory.overhead",  []() -> size_t { const size_t r = GetProcessResidentBytes(),
                                                                 a = MemoryManager::GetHeapAllocatedSize();
                                                    return r > a ? r - a : 0; } },
    { L"runtime.gc.minor",         []() -> size_t { return (size_t)MemoryManager::GetMinorGcCount(); } },
    { L"runtime.gc.major",         []() -> size_t { return (size_t)MemoryManager::GetMajorGcCount(); } },
    { L"runtime.gc.total",         []() -> size_t { return (size_t)(MemoryManager::GetMinorGcCount() +
                                                                    MemoryManager::GetMajorGcCount()); } },
    { L"runtime.gc.stw",           []() -> size_t { return MemoryManager::IsStwActive() ? 1 : 0; } },
    { L"runtime.gc.nursery.used",  []() -> size_t { return MemoryManager::GetNurseryUsed(); } },
    { L"runtime.gc.nursery.occupancy_permille", []() -> size_t {
                                                    const size_t cap = MemoryManager::GetNurseryCapacity();
                                                    return cap ? (MemoryManager::GetNurseryUsed() * 1000 / cap) : 0; } },
    { L"runtime.gc.remembered",    []() -> size_t { return MemoryManager::GetRememberedCount(); } },
    { L"runtime.gc.pause.last_us", []() -> size_t { return (size_t)MemoryManager::GetPauseLastUs(); } },
    { L"runtime.gc.pause.max_us",  []() -> size_t { return (size_t)MemoryManager::GetPauseMaxUs(); } },
    { L"runtime.gc.pause.avg_us",  []() -> size_t { return (size_t)MemoryManager::GetPauseAvgUs(); } },
    { L"runtime.gc.promoted.last", []() -> size_t { return MemoryManager::GetPromotedLast(); } },
    { L"runtime.gc.promoted.total",[]() -> size_t { return MemoryManager::GetPromotedTotal(); } },
    { L"runtime.gc.old.bytes",     []() -> size_t { return MemoryManager::GetOldGenBytes(); } },
    { L"runtime.gc.contention",    []() -> size_t { return (size_t)MemoryManager::GetGcContention(); } },
    { L"runtime.threads.active",   []() -> size_t { return (size_t)MemoryManager::GetMutatorCount(); } },
    { L"runtime.threads.parked",   []() -> size_t { return (size_t)MemoryManager::GetParkedCount(); } },
    { L"runtime.threads.running",  []() -> size_t { const long a = MemoryManager::GetMutatorCount(),
                                                               p = MemoryManager::GetParkedCount();
                                                    return (size_t)(a > p ? a - p : 0); } },
    { L"runtime.alloc.since_gc",   []() -> size_t { return MemoryManager::GetAllocSinceGc(); } },
    { L"runtime.cpu.count",        []() -> size_t { return (size_t)std::thread::hardware_concurrency(); } },
    { L"runtime.cpu.time",         []() -> size_t { return GetProcessCpuTimeMs(); } },
    { L"runtime.uptime_ms",        []() -> size_t { return (size_t)MemoryManager::GetUptimeMs(); } },
  };

  for(const auto& e : kStats) {
    if(key == e.key) {
      out = std::to_wstring(e.fn());
      return true;
    }
  }
  return false;
}

bool TrapProcessor::GetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* key_array = (size_t*)PopInt(op_stack, stack_pos);
  if(key_array) {
    key_array = (size_t*)key_array[0];
    const wchar_t* key = (wchar_t*)(key_array + 3);
    std::wstring stat;
    if(GetRuntimeStat(key, stat)) {
      size_t* value = CreateStringObject(stat, program, op_stack, stack_pos);
      PushInt((size_t)value, op_stack, stack_pos);
    }
    else {
      size_t* value = CreateStringObject(program->GetProperty(key), program, op_stack, stack_pos);
      PushInt((size_t)value, op_stack, stack_pos);
    }
  }
  else {
    size_t* value = CreateStringObject(L"", program, op_stack, stack_pos);
    PushInt((size_t)value, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* value_array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* key_array = (size_t*)PopInt(op_stack, stack_pos);

  if(key_array && value_array) {
    value_array = (size_t*)value_array[0];
    key_array = (size_t*)key_array[0];

    const wchar_t* key = (wchar_t*)(key_array + 3);
    const wchar_t* value = (wchar_t*)(value_array + 3);

    if(!wcscmp(key, L"locale")) {
      const std::string locale_value = UnicodeToBytes(value);
#if defined(_X64)
      char* locale = setlocale(LC_ALL, locale_value.c_str());
      std::locale lollocale(locale);
      setlocale(LC_ALL, locale);
      std::wcout.imbue(lollocale);
#elif defined(_ARM64)
      char* locale = setlocale(LC_ALL, locale_value.c_str());
      std::locale lollocale(locale);
      setlocale(LC_ALL, locale);
      std::wcout.imbue(lollocale);
      setlocale(LC_ALL, "en_US.utf8");
#else    
      setlocale(LC_ALL, locale_value.c_str());
#endif
    }

    program->SetProperty(key, value);
  }

  return true;
}

bool TrapProcessor::GetSysEnv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* key_array = (size_t*)PopInt(op_stack, stack_pos);
  if(key_array) {
    key_array = (size_t*)key_array[0];
    std::string key = UnicodeToBytes((wchar_t*)(key_array + 3));
#ifdef _WIN32
    size_t value_len; 
    char value[SMALL_BUFFER_MAX];
    if(!getenv_s(&value_len, value, SMALL_BUFFER_MAX, key.c_str()) && strlen(value) > 0) {
#else
    const char* value = getenv(key.c_str());
    if(value) {
#endif
      PushInt((size_t)CreateStringObject(BytesToUnicode(value), program, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt((size_t)CreateStringObject(L"", program, op_stack, stack_pos), op_stack, stack_pos);
    }
  }
  else {
    PushInt((size_t)CreateStringObject(L"", program, op_stack, stack_pos), op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SetSysEnv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* value_array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* key_array = (size_t*)PopInt(op_stack, stack_pos);

  if(key_array && value_array) {
    value_array = (size_t*)value_array[0];
    key_array = (size_t*)key_array[0];

    const std::string key = UnicodeToBytes((wchar_t*)(key_array + 3));
    const std::string value = UnicodeToBytes((wchar_t*)(value_array + 3));

#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
  }

  return true;
}

bool TrapProcessor::SockTcpResolveName(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  array = (size_t*)array[0];
  if(array) {
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    std::vector<std::string> addrs = IPSocket::Resolve(name.c_str());

    // create 'System.String' object array
    const long str_obj_array_size = (long)addrs.size();
    const long str_obj_array_dim = 1;
    size_t* str_obj_array = MemoryManager::AllocateArray(str_obj_array_size + str_obj_array_dim + 2,
                                                         instructions::INT_TYPE, op_stack, *stack_pos, false);
    str_obj_array[0] = str_obj_array_size;
    str_obj_array[1] = str_obj_array_dim;
    str_obj_array[2] = str_obj_array_size;
    size_t* str_obj_array_ptr = str_obj_array + 3;

    // create and assign 'System.String' instances to array
    for(size_t i = 0; i < addrs.size(); ++i) {
      const std::wstring waddr(addrs[i].begin(), addrs[i].end());
      str_obj_array_ptr[i] = (size_t)CreateStringObject(waddr, program, op_stack, stack_pos);
    }

    PushInt((size_t)str_obj_array, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpHostName(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    const long size = (long)array[2];
    wchar_t* str = (wchar_t*)(array + 3);

    // get host name
    char buffer[SMALL_BUFFER_MAX] = {0};
    if(!gethostname(buffer, SMALL_BUFFER_MAX - 1)) {
      // copy name   
      long i = 0;
      for(; buffer[i] != L'\0' && i < size; ++i) {
        str[i] = buffer[i];
      }
      str[i] = L'\0';
    }
    else {
      str[0] = L'\0';
    }
#ifdef _DEBUG
    std::wcout << L"stack oper: SOCK_TCP_HOST_NAME: host='" << str << L"'" << std::endl;
#endif
    PushInt((size_t)array, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const long port = (long)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (size_t*)array[0];
    const std::string addr = UnicodeToBytes((wchar_t*)(array + 3));
    // instance[3] holds connect timeout in ms (0 = blocking)
    const long timeout_ms = (long)instance[3];
    // connect() can block for a long time; park for the duration. The instance
    // ref is re-rooted on the op_stack so GC promotion fixup relocates it, then
    // re-read after the wait (a raw pointer held across a park can go stale).
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    SOCKET sock = (timeout_ms > 0)
      ? IPSocket::OpenWithTimeout(addr.c_str(), static_cast<int>(port), static_cast<int>(timeout_ms))
      : IPSocket::Open(addr.c_str(), static_cast<int>(port));
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);
#ifdef _DEBUG
    std::wcout << L"# socket connect: addr='" << BytesToUnicode(addr) << "'; instance="
	       << instance << L"(" << (size_t)instance << L")" << L"; addr=" << sock << L"("
	       << (long)sock << L") #" << std::endl;
#endif
    instance[0] = sock;
  }

  return true;
}

bool TrapProcessor::SockTcpBind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  long port = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    SOCKET server = IPSocket::Bind(static_cast<int>(port));
#ifdef _DEBUG
    std::wcout << L"# socket bind: port=" << port << L"; instance=" << instance << L"("
      << (size_t)instance << L")" << L"; addr=" << server << L"(" << (size_t)server
      << L") #" << std::endl;
#endif
    instance[0] = (long)server;
  }

  return true;
}

bool TrapProcessor::SockTcpListen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  long backlog = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && (long)instance[0] > -1) {
    SOCKET server = (SOCKET)instance[0];
#ifdef _DEBUG
    std::wcout << L"# socket listen: backlog=" << backlog << L"'; instance=" << instance
      << L"(" << (size_t)instance << L")" << L"; addr=" << server << L"("
      << (long)server << L") #" << std::endl;
#endif
    if(IPSocket::Listen(server, static_cast<int>(backlog))) {
      PushInt(1, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
   const bool is_write = (bool)PopInt(op_stack, stack_pos);
   size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

   if(instance && (long)instance[0] > -1) {
      SOCKET sock = (SOCKET)instance[0];
      PushInt(socket_ready_for_io(sock, is_write), op_stack, stack_pos);
      return true;
   }
   
   PushInt(-1, op_stack, stack_pos);
   return true;
}

bool TrapProcessor::SockTcpAccept(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET server = (SOCKET)instance[0];
    char client_address[SMALL_BUFFER_MAX] = {0};
    int client_port;
    // accept() blocks indefinitely; park so a stop-the-world collection elsewhere
    // can proceed (only C locals are touched while parked)
    MemoryManager::BeginBlocking();
    SOCKET client = IPSocket::Accept(server, client_address, client_port);
    MemoryManager::EndBlocking();
#ifdef _DEBUG
    std::wcout << L"# socket accept: instance=" << instance << L"(" << (size_t)instance << L")" << L"; ip="
      << BytesToUnicode(client_address) << L"; port=" << client_port << L"; addr=" << server << L"("
      << (long)server << L") #" << std::endl;
#endif
    const std::wstring wclient_address = BytesToUnicode(client_address);
    size_t* sock_obj = MemoryManager::AllocateObject(program->GetSocketObjectId(),
                                                     op_stack, *stack_pos, false);
    sock_obj[0] = client;
    sock_obj[1] = (size_t)CreateStringObject(wclient_address, program, op_stack, stack_pos);
    sock_obj[2] = client_port;

    PushInt((size_t)sock_obj, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
#ifdef _DEBUG
    std::wcout << L"# socket close: addr=" << sock << L"(" << (long)sock << L") #" << std::endl;
#endif  
    instance[0] = 0;
    IPSocket::Close(sock);
  }

  return true;
}

bool TrapProcessor::SockTcpOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];

#ifdef _DEBUG
    std::wcout << L"# socket write std::string: instance=" << instance << L"(" << (size_t)instance << L")"
      << L"; array=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif        
    const std::string data = UnicodeToBytes((wchar_t*)(array + 3));
    // send() can block on a full buffer; park (data is C heap, safe across a park)
    MemoryManager::BeginBlocking();
    IPSocket::WriteBytes(data.c_str(), (int)data.size(), sock);
    MemoryManager::EndBlocking();
  }

  return true;
}

bool TrapProcessor::SockTcpInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance && (long)instance[0] > -1) {
    char buffer[MID_BUFFER_MAX] = {0};
    SOCKET sock = (SOCKET)instance[0];
    int status;

    if((long)sock > -1) {
      const int capacity = (int)array[0];
      int index = 0;
      char value;
      bool end_line = false;
      // the read loop blocks on the socket; park for its duration. Only C locals
      // are touched while parked; the array ref is re-rooted on the op_stack so
      // GC promotion fixup relocates it, then re-read after the wait.
      PushInt((size_t)array, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      do {
        value = IPSocket::ReadByte(sock, status);
        if(value != '\0' && value != '\r' && value != '\n' && index < MID_BUFFER_MAX - 1 && status > 0) {
          buffer[index++] = value;
        }
        else {
          end_line = true;
        }
      }
      while(!end_line && index < capacity - 1);
      buffer[index] = '\0';

      // assume LF
      if(value == '\r') {
        IPSocket::ReadByte(sock, status);
      }
      MemoryManager::EndBlocking();
      array = (size_t*)PopInt(op_stack, stack_pos);

      // copy content
      const std::wstring in = BytesToUnicode(buffer);
      wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
      wcsncpy_s(out, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(out, in.c_str(), in.size());
#endif
    }
  }

  return true;
}

bool TrapProcessor::SockTcpSslConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* pem_file_array = (size_t*)PopInt(op_stack, stack_pos);
  const long port = (long)PopInt(op_stack, stack_pos);
  size_t* addr_array = (size_t*)PopInt(op_stack, stack_pos);

  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(addr_array && instance) {
    addr_array = (size_t*)addr_array[0];
    const std::string addr = UnicodeToBytes((wchar_t*)(addr_array + 3));

    std::string pem_file;
    if(pem_file_array) {
      pem_file_array = (size_t*)pem_file_array[0];
      pem_file = UnicodeToBytes((wchar_t*)(pem_file_array + 3));
    }

    // Close existing connection if any
    IPSecureSocket::Close((SecureSocketCtx*)instance[0]);

    SecureSocketCtx* sctx = nullptr;
    const bool is_open = IPSecureSocket::Open(addr.c_str(), static_cast<int>(port), pem_file, sctx);
    if(is_open) {
      instance[0] = (size_t)sctx;
      instance[1] = 0;
      instance[2] = 0;
      instance[3] = 1;

#ifdef _DEBUG
      std::wcout << L"# socket connect: addr='" << BytesToUnicode(addr) << L"'; instance="
      << instance << L"(" << (size_t)instance << L")" << L"; sctx=" << sctx << L"("
      << (size_t)sctx << L") #" << std::endl;
#endif
    }
  }

  return true;
}

bool TrapProcessor::SockTcpSslIssuer(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
  if(sctx) {
    const mbedtls_x509_crt* peer_cert = mbedtls_ssl_get_peer_cert(&sctx->ssl);
    if(peer_cert) {
      char buffer[LARGE_BUFFER_MAX] = {0};
      mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, &peer_cert->issuer);
      const std::wstring in = BytesToUnicode(buffer);
      PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
  if(sctx) {
    const mbedtls_x509_crt* peer_cert = mbedtls_ssl_get_peer_cert(&sctx->ssl);
    if(peer_cert) {
      char buffer[LARGE_BUFFER_MAX] = {0};
      mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, &peer_cert->subject);
      const std::wstring in = BytesToUnicode(buffer);
      PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];

#ifdef _DEBUG
  std::wcout << L"# socket close: sctx=" << sctx << L"("
    << (size_t)sctx << L") #" << std::endl;
#endif
  IPSecureSocket::Close(sctx);
  instance[0] = instance[1] = instance[2] = instance[3] = 0;

  return true;
}

bool TrapProcessor::SockTcpSslOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    if(instance[3] && sctx) {
      const std::string out = UnicodeToBytes((wchar_t*)(array + 3));
      IPSecureSocket::WriteBytes(out.c_str(), (int)out.size(), sctx);
    }
  }

  return true;
}

bool TrapProcessor::SockTcpSslInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    char buffer[MID_BUFFER_MAX] = {0};
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    int status;
    if(instance[3] && sctx) {
      size_t index = 0;
      char value;
      bool end_line = false;
      do {
        value = IPSecureSocket::ReadByte(sctx, status);
        if(value != '\0' && value != '\r' && value != '\n' && index < MID_BUFFER_MAX - 1 && status > 0) {
          buffer[index++] = value;
        }
        else {
          end_line = true;
        }
      }
      while(!end_line && index < array[0] - 1);
      buffer[index] = '\0';

      // assume LF
      if(value == '\r') {
        IPSecureSocket::ReadByte(sctx, status);
      }

      // copy content
      const std::wstring in = BytesToUnicode(buffer);
      wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
      wcsncpy_s(out, array[0], in.c_str(), in.size());
#else
      wcsncpy(out, in.c_str(), in.size());
#endif
    }
  }

  return true;
}

bool TrapProcessor::SockTcpSslListen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    size_t* cert_obj = (size_t*)instance[3];
    size_t* key_obj = (size_t*)instance[4];
    size_t* passwd_obj = (size_t*)instance[5];
    const long port = (long)instance[6];

    if(cert_obj && key_obj) {
      SecureServerCtx* srv = new SecureServerCtx();

      // Seed the random number generator
      const char* pers = "objeck_ssl_server";
      int ret = mbedtls_ctr_drbg_seed(&srv->ctr_drbg, mbedtls_entropy_func, &srv->entropy,
                                       (const unsigned char*)pers, strlen(pers));
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Load server certificate
      const std::string cert_path = UnicodeToBytes((wchar_t*)((size_t*)cert_obj[0] + 3));
      ret = mbedtls_x509_crt_parse_file(&srv->srvcert, cert_path.c_str());
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Load private key (with optional password)
      const std::string key_path = UnicodeToBytes((wchar_t*)((size_t*)key_obj[0] + 3));
      const char* passwd_cstr = nullptr;
      std::string passwd;
      if(passwd_obj) {
        const std::wstring passwd_str((wchar_t*)((size_t*)passwd_obj[0] + 3));
        if(!passwd_str.empty()) {
          passwd = UnicodeToBytes(passwd_str);
          passwd_cstr = passwd.c_str();
        }
      }

#if MBEDTLS_VERSION_MAJOR >= 3
      ret = mbedtls_pk_parse_keyfile(&srv->pkey, key_path.c_str(), passwd_cstr,
                                      mbedtls_ctr_drbg_random, &srv->ctr_drbg);
#else
      ret = mbedtls_pk_parse_keyfile(&srv->pkey, key_path.c_str(), passwd_cstr);
#endif
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Bind to port
      const std::string port_str = std::to_string(port);
      ret = mbedtls_net_bind(&srv->listen_fd, nullptr, port_str.c_str(), MBEDTLS_NET_PROTO_TCP);
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Configure SSL for server
      ret = mbedtls_ssl_config_defaults(&srv->conf, MBEDTLS_SSL_IS_SERVER,
                                         MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      mbedtls_ssl_conf_rng(&srv->conf, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      mbedtls_ssl_conf_own_cert(&srv->conf, &srv->srvcert, &srv->pkey);

      instance[0] = (size_t)srv;
      instance[1] = 0;
      instance[2] = 0;

      PushInt(1, op_stack, stack_pos);
      return true;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
   const bool is_write = (bool)PopInt(op_stack, stack_pos);
   size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

   if(instance && instance[0]) {
      SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
      PushInt(ssl_ready_for_io(sctx, is_write), op_stack, stack_pos);
      return true;
   }

   PushInt(-1, op_stack, stack_pos);
   return true;
}

bool TrapProcessor::SockTcpSslAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    SecureServerCtx* srv = (SecureServerCtx*)instance[0];

    if(srv) {
      SecureSocketCtx* client = new SecureSocketCtx();

      // Accept raw TCP connection. accept() blocks indefinitely; park so STW GC
      // elsewhere can proceed. The instance ref is re-rooted on the op_stack so
      // promotion fixup relocates it (srv/client are C heap, safe across a park).
      PushInt((size_t)instance, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      int ret = mbedtls_net_accept(&srv->listen_fd, &client->net, nullptr, 0, nullptr);
      MemoryManager::EndBlocking();
      instance = (size_t*)PopInt(op_stack, stack_pos);
      if(ret != 0) {
        delete client;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      // Setup SSL session for this client (uses server's config)
      ret = mbedtls_ssl_setup(&client->ssl, &srv->conf);
      if(ret != 0) {
        delete client;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      mbedtls_ssl_set_bio(&client->ssl, &client->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

      // Perform handshake
      while((ret = mbedtls_ssl_handshake(&client->ssl)) != 0) {
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
          delete client;
          PushInt(0, op_stack, stack_pos);
          return true;
        }
      }

      // Get peer address info
      int sock_fd = client->net.fd;

      struct sockaddr_storage pin;
      memset(&pin, 0, sizeof(pin));
      socklen_t pen_len = sizeof(pin);
      int status = getpeername(sock_fd, (sockaddr*)&pin, &pen_len);
      if(status < 0) {
        delete client;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      char host_name[NI_MAXHOST] = {0};
      char port[NI_MAXSERV] = {0};
      status = getnameinfo((struct sockaddr*)&pin, pen_len, host_name, sizeof(host_name), port,
                           sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
      if(status < 0) {
        delete client;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      size_t* sock_obj = MemoryManager::AllocateObject(program->GetSecureSocketObjectId(), op_stack, *stack_pos, false);
      sock_obj[0] = (size_t)client;
      sock_obj[1] = 0;
      sock_obj[3] = 1;
      sock_obj[4] = (size_t)CreateStringObject(BytesToUnicode(host_name), program, op_stack, stack_pos);
      sock_obj[5] = instance[6];

      PushInt((size_t)sock_obj, op_stack, stack_pos);
      return true;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslCertSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureServerCtx* srv = (SecureServerCtx*)instance[0];
  if(srv) {
    char buffer[LARGE_BUFFER_MAX] = {0};
    mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, &srv->srvcert.issuer);
    const std::wstring in = BytesToUnicode(buffer);
    PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _WIN32
  char error_msg[SMALL_BUFFER_MAX] = {0};
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,	0, WSAGetLastError(), 0, error_msg, SMALL_BUFFER_MAX, 0);
  char* newline = strrchr(error_msg, '\n');
  if(newline) {
    *newline = '\0';
  }
  const std::wstring err_msg = BytesToUnicode(error_msg);
  PushInt((size_t)CreateStringObject(err_msg, program, op_stack, stack_pos), op_stack, stack_pos);
#else
    const std::wstring err_msg = BytesToUnicode(strerror(errno));
    PushInt((size_t)CreateStringObject(err_msg, program, op_stack, stack_pos), op_stack, stack_pos);
#endif
  
  return true;
}

bool TrapProcessor::SockTcpSslError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // mbedTLS uses return codes rather than an error queue.
  // Check the last error from the most recent SecureSocketCtx operation.
  // The caller should pass the socket instance to retrieve the error from.
  // For backward compatibility, we check the stack for an instance pointer.
  PushInt(0, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SockTcpSslCloseSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    SecureServerCtx* srv = (SecureServerCtx*)instance[0];
    if(srv) {
      instance[0] = 0;
      delete srv;
    }
  }

  return true;
}

// --- TCP socket option implementations ---

bool TrapProcessor::SockTcpSetKeepAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool enable = (bool)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (SOCKET)instance[0] > 0) {
    PushInt(IPSocket::SetKeepAlive((SOCKET)instance[0], enable) ? 1 : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::SockTcpSetNoDelay(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool enable = (bool)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (SOCKET)instance[0] > 0) {
    PushInt(IPSocket::SetNoDelay((SOCKET)instance[0], enable) ? 1 : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::SockTcpSetRecvTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int ms = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (SOCKET)instance[0] > 0) {
    PushInt(IPSocket::SetRecvTimeout((SOCKET)instance[0], ms) ? 1 : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::SockTcpSetSendTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int ms = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (SOCKET)instance[0] > 0) {
    PushInt(IPSocket::SetSendTimeout((SOCKET)instance[0], ms) ? 1 : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::SockTcpSetConnTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // Store timeout in instance[3]; SockTcpConnect reads it before connecting
  const int ms = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    instance[3] = (size_t)ms;
  }
  return true;
}

bool TrapProcessor::SockTcpSetRcvBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int bytes = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (SOCKET)instance[0] > 0) {
    PushInt(IPSocket::SetRecvBufSize((SOCKET)instance[0], bytes) ? 1 : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::SockTcpSetSndBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int bytes = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (SOCKET)instance[0] > 0) {
    PushInt(IPSocket::SetSendBufSize((SOCKET)instance[0], bytes) ? 1 : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

// --- SSL socket option implementations ---

bool TrapProcessor::SockTcpSslSetKeepAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool enable = (bool)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetKeepAlive(sctx, enable) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetNoDelay(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool enable = (bool)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetNoDelay(sctx, enable) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetRecvTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int ms = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetRecvTimeout(sctx, ms) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetSendTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int ms = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetSendTimeout(sctx, ms) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetRcvBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int bytes = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetRecvBufSize(sctx, bytes) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetSndBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int bytes = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetSendBufSize(sctx, bytes) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetMinTLS(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const int ver = (int)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetMinTLSVersion(sctx, ver) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslSetVerifyPeer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool strict = (bool)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  PushInt(IPSecureSocket::SetVerifyPeer(sctx, strict) ? 1 : 0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockTcpSslGetFingerprint(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureSocketCtx* sctx = instance ? (SecureSocketCtx*)instance[0] : nullptr;
  const std::string fp = IPSecureSocket::GetCertFingerprint(sctx);
  if(!fp.empty()) {
    const std::wstring wfp = BytesToUnicode(fp);
    PushInt((size_t)CreateStringObject(wfp, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::SockUdpCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  const u_short port = (u_short)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  
  if(array && instance) {
    array = (size_t*)array[0];
    const std::wstring waddr = (wchar_t*)(array + 3);
    const std::string addr_str = UnicodeToBytes(waddr);
    
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32      
    if(sock != INVALID_SOCKET) {
#else
    if(sock > -1) {
#endif
      struct sockaddr_in bin_addr;
      if(inet_pton(AF_INET, addr_str.c_str(), &(bin_addr.sin_addr)) != 1) {
#ifdef _WIN32      
        closesocket(sock);
#else
	close(sock);
#endif	        
      }
      else {
        struct sockaddr_in* addr_in = new struct sockaddr_in;
        addr_in->sin_family = AF_INET;
        addr_in->sin_port = htons(port);
        addr_in->sin_addr = bin_addr.sin_addr;

        inst[0] = sock;
        inst[1] = (size_t)addr_in;
      }
    }
  }

  return true;
}

bool TrapProcessor::SockUdpBind(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  if(inst) {
    const long port = (long)inst[2];
    SOCKET sock; struct sockaddr_in* addr;
    if(UDPSocket::Bind(static_cast<int>(port), sock, addr)) {
      inst[0] = sock;
      inst[1] = (size_t)addr;
    }
  }

  return true;
}

bool TrapProcessor::SockUdpClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  if(inst) {
    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];
    UDPSocket::Close(sock, addr_in);
  }

  return true;
}

bool TrapProcessor::SockUdpInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && instance[0]) {
    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];

    char value;
    socklen_t addr_in_size = sizeof(struct sockaddr_in);
    if(recvfrom(sock, &value, 1, 0, (struct sockaddr*)addr_in, &addr_in_size) < 0) {
      PushInt(value, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const int num = (int)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];
    char* buffer = (char*)(array + 3);

    socklen_t addr_in_size = sizeof(struct sockaddr_in);
    const int read = static_cast<int>(recvfrom(sock, buffer, num, 0, (struct sockaddr*)addr_in, &addr_in_size));
    if(read < 0) {
      PushInt(read, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];

    wchar_t* buffer = (wchar_t*)(array + 3);
    char* byte_buffer = new char[num * 2 + 1];

    socklen_t addr_in_size = sizeof(struct sockaddr_in);
    const int read = static_cast<int>(recvfrom(sock, byte_buffer, num, 0, (struct sockaddr*)addr_in, &addr_in_size));
    // recvfrom returns -1 on error; only terminate/decode when bytes were read
    if(read > -1) {
      byte_buffer[read] = '\0';
      std::wstring in(BytesToUnicode(byte_buffer));

      // copy and remove file BOM UTF (8, 16, 32)
      if(in.size() > 0 && (in[0] == (wchar_t)0xFEFF || in[0] == (wchar_t)0xFFFE || in[0] == (wchar_t)0xFFFE0000 || in[0] == (wchar_t)0xEFBBBF)) {
        in.erase(in.begin(), in.begin() + 1);
      }

      // copy
#ifdef _WIN32
      wcsncpy_s(buffer, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(buffer, in.c_str(), in.size());
#endif
    }

    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;

    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpOutCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];
    socklen_t addr_in_size = sizeof(struct sockaddr_in);

    const wchar_t* buffer = (wchar_t*)(array + 3);
    std::string buffer_out = UnicodeToBytes(buffer);

    const int sent = static_cast<int>(sendto(sock, buffer_out.c_str(), (int)buffer_out.size(), 0, (struct sockaddr*)addr_in, addr_in_size));
    PushInt(sent, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpOutByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  char value = (char)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && instance[0]) {
    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];

    const socklen_t addr_in_size = sizeof(struct sockaddr_in);
    const int sent = static_cast<int>(sendto(sock, &value, 1, 0, (struct sockaddr*)addr_in, addr_in_size));
    if(sent < 0) {
      PushInt(sent > -1, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpOutByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const int num = (int)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {

    SOCKET sock = static_cast<int>(inst[0]);
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];
    const char* buffer = (char*)(array + 3);

    const socklen_t addr_in_size = sizeof(struct sockaddr_in);
    const int sent = static_cast<int>(sendto(sock, buffer, num, 0, (struct sockaddr*)addr_in, addr_in_size));
    if(sent < 0) {
      PushInt(sent, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance && (long)instance[0] > -1) {
    char buffer[MID_BUFFER_MAX] = {0};

    SOCKET sock = static_cast<int>(inst[0]);

    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];
    socklen_t addr_in_size = sizeof(struct sockaddr_in);

    if((long)sock > -1) {
      const int read = static_cast<int>(recvfrom(sock, buffer, MID_BUFFER_MAX - 1, 0, (struct sockaddr*)addr_in, &addr_in_size));
      if(read > -1) {
        buffer[read] = '\0';

        // copy content
        std::wstring in = BytesToUnicode(buffer);
        while(!in.empty() && (in.back() == L'\r' || in.back() == L'\n')) {
          in.pop_back();
        }
        wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
        wcsncpy_s(out, array[0] + 1, in.c_str(), in.size());
#else
        wcsncpy(out, in.c_str(), in.size());
#endif
      }
    }
  }

  return true;
}

bool TrapProcessor::SockUdpOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
    struct sockaddr_in* addr_in = (struct sockaddr_in*)inst[1];

#ifdef _DEBUG
    std::wcout << L"# udp write std::string: instance=" << instance << L"(" << (size_t)instance << L")" << L"; array=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif        
    const std::string data = UnicodeToBytes((wchar_t*)(array + 3));
    const socklen_t addr_in_size = sizeof(struct sockaddr_in);
    const int sent = static_cast<int>(sendto(sock, data.c_str(), (int)data.size(), 0, (struct sockaddr*)addr_in, addr_in_size));
    if(sent < 0) {
      PushInt(sent, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }

  return true;
}

bool TrapProcessor::SockUdpError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  return false;
}

bool TrapProcessor::SerlChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# serializing char #" << std::endl;
#endif
  SerializeInt(CHAR_PARM, inst, op_stack, stack_pos);
  SerializeChar((wchar_t)frame->mem[1], inst, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SerlInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# serializing int #" << std::endl;
#endif
  SerializeInt(INT_PARM, inst, op_stack, stack_pos);
  SerializeInt((INT64_VALUE)frame->mem[1], inst, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SerlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# serializing float #" << std::endl;
#endif
  SerializeInt(FLOAT_PARM, inst, op_stack, stack_pos);
  FLOAT_VALUE value;
  memcpy(&value, &(frame->mem[1]), sizeof(value));
  SerializeFloat(value, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  SerializeObject(inst, frame, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  SerializeInt(BYTE_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((size_t*)frame->mem[1], BYTE_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  SerializeInt(CHAR_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((size_t*)frame->mem[1], CHAR_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  SerializeInt(INT_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((size_t*)frame->mem[1], INT_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  SerializeInt(OBJ_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((size_t*)frame->mem[1], OBJ_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::SerlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  SerializeInt(FLOAT_ARY_PARM, inst, op_stack, stack_pos);
  SerializeArray((size_t*)frame->mem[1], FLOAT_ARY_PARM, inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DeserlChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing char #" << std::endl;
#endif
  if(CHAR_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt(DeserializeChar(inst), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing int #" << std::endl;
#endif
  if(INT_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt(DeserializeInt(inst), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing float #" << std::endl;
#endif
  if(FLOAT_PARM == (ParamType)DeserializeInt(inst)) {
    PushFloat(DeserializeFloat(inst), op_stack, stack_pos);
  }
  else {
    PushFloat(0.0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  DeserializeObject(inst, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::DeserlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing byte array #" << std::endl;
#endif
  if(BYTE_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((size_t)DeserializeArray(BYTE_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing char array #" << std::endl;
#endif
  if(CHAR_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((size_t)DeserializeArray(CHAR_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing int array #" << std::endl;
#endif
  if(INT_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((size_t)DeserializeArray(INT_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing an object array #" << std::endl;
#endif
  if(OBJ_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((size_t)DeserializeArray(OBJ_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DeserlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
#ifdef _DEBUG
  std::wcout << L"# deserializing float array #" << std::endl;
#endif
  if(FLOAT_ARY_PARM == (ParamType)DeserializeInt(inst)) {
    PushInt((size_t)DeserializeArray(FLOAT_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::CompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];
    
  uLong out_len;
  char* out = OutputStream::CompressZlib(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return false;
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy string
  char* byte_array_ptr = (char*)(byte_array + 3);
  memcpy(byte_array_ptr, out, byte_array_size);
  free(out);
  out = nullptr;
    
  PushInt((size_t)byte_array, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::UncompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::UncompressZlib(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return false;
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy string
  char* byte_array_ptr = (char*)(byte_array + 3);
  memcpy(byte_array_ptr, out, byte_array_size);
  free(out);
  out = nullptr;

  PushInt((size_t)byte_array, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::CompressGzipBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::CompressGzip(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return false;
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy string
  char* byte_array_ptr = (char*)(byte_array + 3);
  memcpy(byte_array_ptr, out, byte_array_size);
  free(out);
  out = nullptr;

  PushInt((size_t)byte_array, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::UncompressGzipBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::UncompressGzip(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return false;
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy string
  char* byte_array_ptr = (char*)(byte_array + 3);
  memcpy(byte_array_ptr, out, byte_array_size);
  free(out);
  out = nullptr;

  PushInt((size_t)byte_array, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::CompressBrBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::CompressBr(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return false;
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy string
  char* byte_array_ptr = (char*)(byte_array + 3);
  memcpy(byte_array_ptr, out, byte_array_size);
  free(out);
  out = nullptr;

  PushInt((size_t)byte_array, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::UncompressBrBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::UncompressBr(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return false;
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size + 1;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  // copy string
  char* byte_array_ptr = (char*)(byte_array + 3);
  memcpy(byte_array_ptr, out, byte_array_size);
  free(out);
  out = nullptr;

  PushInt((size_t)byte_array, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::TrapProcessor::CRC32Bytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if (!array) {
    std::wcerr << L">>> Attempting to dereference a 'Nil' memory instance <<<" << std::endl;
    return false;
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  // calculate CRC
  const uLong crc = crc32(0, (Bytef*)in, static_cast<uInt>(in_len));
  PushInt(crc, op_stack, stack_pos);

  return true;
}

bool TrapProcessor::FileOpenRead(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (size_t*)array[0];
    const std::string filename = UnicodeToBytes((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "rb");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << BytesToUnicode(filename) << L"'; instance=" << instance << L"("
	       << (size_t)instance << L")" << L"; addr=" << file << L"(" << (size_t)file << L") #" << std::endl;
#endif
    instance[0] = (size_t)file;
  }
  
  return true;
}

bool TrapProcessor::FileOpenAppend(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (size_t*)array[0];
    const std::string filename = UnicodeToBytes((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "ab");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << BytesToUnicode(filename) << L"'; instance=" << instance << L"("
	       << (size_t)instance << L")" << L"; addr=" << file << L"(" << (size_t)file << L") #" << std::endl;
#endif
    instance[0] = (size_t)file;
  }

  return true;
}

bool TrapProcessor::FileOpenWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (size_t*)array[0];
    const std::string filename = UnicodeToBytes((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "wb");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << BytesToUnicode(filename) << L"'; instance=" << instance << L"("
	       << (size_t)instance << L")" << L"; addr=" << file << L"(" << (size_t)file << L") #" << std::endl;
#endif
    instance[0] = (size_t)file;
  }

  return true;
}

bool TrapProcessor::FileOpenReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    array = (size_t*)array[0];
    const std::string filename = UnicodeToBytes((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "w+b");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << filename.c_str() << L"'; instance=" << instance << L"("
      << (size_t)instance << L")" << L"; addr=" << file << L"(" << (size_t)file
      << L") #" << std::endl;
#endif
    instance[0] = (size_t)file;
  }

  return true;
}

bool TrapProcessor::FileClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
    std::wcout << L"# file close: addr=" << file << L"(" << (size_t)file << L") #" << std::endl;
#endif
    instance[0] = 0;
    fclose(file);
  }

  return true;
}

bool TrapProcessor::FileFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
#ifdef _DEBUG
    std::wcout << L"# file flush: addr=" << file << L"(" << (size_t)file << L") #" << std::endl;
#endif
    instance[0] = 0;
    fflush(file);
  }

  return true;
}

bool TrapProcessor::FileInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance && instance[0]) {
    FILE* file = (FILE*)instance[0];
    char buffer[MID_BUFFER_MAX] = {0};
    if(file && fgets(buffer, MID_BUFFER_MAX - 1, file)) {
      long end_index = (long)strlen(buffer) - 1;
      if(end_index > -1) {
        if(buffer[end_index] == '\n' || buffer[end_index] == '\r') {
          buffer[end_index] = '\0';
        }

        if(--end_index > -1 && buffer[end_index] == '\r') {
          buffer[end_index] = '\0';
        }
      }
      else {
        buffer[0] = '\0';
      }
      
      // copy and remove file BOM UTF (8, 16, 32)
      std::wstring in = BytesToUnicode(buffer);
      if(in.size() > 0 && (in[0] == (wchar_t)0xFEFF || in[0] == (wchar_t)0xFFFE || in[0] == (wchar_t)0xFFFE0000 || in[0] == (wchar_t)0xEFBBBF)) {
        in.erase(in.begin(), in.begin() + 1);
      }

      wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
      wcsncpy_s(out, array[0], in.c_str(), in.size());
#else
      wcsncpy(out, in.c_str(), in.size());
#endif
    }
  }

  return true;
}

bool TrapProcessor::FileOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    FILE* file = (FILE*)instance[0];
    const wchar_t* data = (wchar_t*)(array + 3);
    if(file) {
      fputs(UnicodeToBytes(data).c_str(), file);
    }
  }

  return true;
}

bool TrapProcessor::FileRewind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    rewind(file);
  }

  return true;
}

// pipe operations
bool TrapProcessor::PipeCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const int mode = (int)PopInt(op_stack, stack_pos);
  (void)mode;
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && array) {
    const wchar_t* name = (wchar_t*)(((size_t*)array[0]) + 3);

#ifdef _WIN32
    const std::string filename = "\\\\.\\pipe\\" + UnicodeToBytes(name);
    HANDLE pipe;
#else
    const std::string filename = "/tmp/" + UnicodeToBytes(name);
    int pipe;
#endif

    // create
    if(Pipe::Create(filename.c_str(), pipe)) {
      instance[0] = (size_t)pipe;
    }
  }
  
  return true;
}

bool TrapProcessor::PipeOpen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const int mode = (int)PopInt(op_stack, stack_pos);
  (void)mode;
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && array) {
    const wchar_t* name = (wchar_t*)(((size_t*)array[0]) + 3);

#ifdef _WIN32
    const std::string filename = "\\\\.\\pipe\\" + UnicodeToBytes(name);
    HANDLE pipe;
#else
    const std::string filename = "/tmp/" + UnicodeToBytes(name);
    int pipe;
#endif

    // create
    if(Pipe::Open(filename.c_str(), pipe)) {
      instance[0] = (size_t)pipe;
    }
    else {
      instance[0] = 0;
    }
  }

  return true;
}

bool TrapProcessor::PipeClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* fram) 
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && instance[0]) {
#ifdef _WIN32
    const HANDLE pipe = (HANDLE)instance[0];
#else
    const int pipe = (int)instance[0];
#endif
    if(pipe != 0) {
      Pipe::Close(pipe);
    }
  }

  return true;
}

bool TrapProcessor::PipeInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && instance[0]) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    
    char value = '\0';

    if(Pipe::ReadByte(value, pipe)) {
      PushInt(value, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeOutByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const int value = (int)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && (FILE*)instance[0]) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
    PushInt(Pipe::WriteByte(value, pipe), op_stack, stack_pos);
#else
    int pipe = (int)instance[0];
    PushInt(Pipe::WriteByte(value, pipe), op_stack, stack_pos);
#endif
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    char* buffer = (char*)(array + 3);
    PushInt(Pipe::ReadByteArray(buffer, offset, num, pipe), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif

    // read from pipe
    char* byte_buffer = new char[num * 2 + 1];
    // offset 0: the temp buffer is sized for 'num'; offset belongs to the
    // destination, not this buffer (see FileInCharAry heap-overflow fix).
    const size_t read = Pipe::ReadByteArray(byte_buffer, 0, num, pipe);
    byte_buffer[read] = '\0';
    std::wstring in(BytesToUnicode(byte_buffer));

    // copy and remove file BOM UTF (8, 16, 32)
    if(in.size() > 0 && (in[0] == (wchar_t)0xFEFF || in[0] == (wchar_t)0xFFFE || in[0] == (wchar_t)0xFFFE0000 || in[0] == (wchar_t)0xEFBBBF)) {
      in.erase(in.begin(), in.begin() + 1);
    }

    // copy
#ifdef _WIN32
    wcsncpy_s(out, array[0] + 1, in.c_str(), in.size());
#else
    wcsncpy(out, in.c_str(), in.size());
#endif

    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;

    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeOutByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    const char* buffer = (char*)(array + 3);
    PushInt(Pipe::WriteByteArray(buffer, offset, num, pipe), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeOutCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    const std::wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    std::string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(Pipe::WriteByteArray(buffer_out.c_str(), 0, buffer_out.size(), pipe), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[0]) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
    std::string buffer = Pipe::ReadString(pipe);
#else
    int pipe = (int)instance[0];
    std::string buffer = Pipe::ReadString(pipe);
#endif
    
    if(!buffer.empty()) {
      // copy content
      std::wstring in = BytesToUnicode(buffer);
      while(!in.empty() && (in.back() == L'\r' || in.back() == L'\n')) {
        in.pop_back();
      }
      wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
      wcsncpy_s(out, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(out, in.c_str(), in.size());
#endif
    }
  }
  
  return true;
}

bool TrapProcessor::PipeOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance && instance[0]) {
    const std::string output = UnicodeToBytes((wchar_t*)(array + 3));
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    Pipe::WriteString(output, pipe);
  }

  return true;
}

// socket operations
bool TrapProcessor::SockTcpIsConnected(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
    int status;
    // blocking read; park so a stop-the-world collection elsewhere can proceed
    MemoryManager::BeginBlocking();
    const char value = IPSocket::ReadByte(sock, status);
    MemoryManager::EndBlocking();
    PushInt(value, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = (SOCKET)instance[0];
    // blocking read; park for the duration. recv() lands in a temp C buffer (a
    // parked thread's GC array can be moved by promotion mid-read), and the
    // array ref is re-rooted on the op_stack so fixup relocates it.
    char* temp = new char[num > 0 ? num : 1];
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const int read = IPSocket::ReadBytes(temp, (int)num, sock);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    if(read > 0) {
      memcpy((char*)(array + 3) + offset, temp, read);
    }
    delete[] temp;
    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = (SOCKET)instance[0];
    // allocate temporary buffer
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry heap-overflow fix).
    // Blocking read: park for the duration, with the array ref re-rooted on the
    // op_stack so GC promotion fixup relocates it while we wait.
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    int read = IPSocket::ReadBytes(byte_buffer, static_cast<int>(num), sock);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    wchar_t* buffer = (wchar_t*)(array + 3);
    if(read > -1) {
      byte_buffer[read] = '\0';
      std::wstring in = BytesToUnicode(byte_buffer);
#ifdef _WIN32
      wcsncpy_s(buffer, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(buffer, in.c_str(), in.size());
#endif
      PushInt(in.size(), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    SOCKET sock = (SOCKET)instance[0];
    // send() can block on a full buffer; park so STW GC elsewhere can proceed
    MemoryManager::BeginBlocking();
    IPSocket::WriteByte((char)value, sock);
    MemoryManager::EndBlocking();
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = (SOCKET)instance[0];
    // send() can block on a full buffer; park for the duration. Data is staged
    // in a temp C buffer first — a parked thread's GC array can be moved (and
    // its young-gen slot reused) by promotion mid-send.
    char* temp = new char[num > 0 ? num : 1];
    memcpy(temp, (char*)(array + 3) + offset, num);
    MemoryManager::BeginBlocking();
    const int sent = IPSocket::WriteBytes(temp, (int)num, sock);
    MemoryManager::EndBlocking();
    delete[] temp;
    PushInt(sent, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (long)instance[0] > -1 && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SOCKET sock = (SOCKET)instance[0];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    
    // convert to bytes and write out (buffer_out is C heap, safe across a park)
    std::string buffer_out = UnicodeToBytes(buffer);
    MemoryManager::BeginBlocking();
    const int sent = IPSocket::WriteBytes(buffer_out.c_str(), (int)buffer_out.size(), sock);
    MemoryManager::EndBlocking();
    PushInt(sent, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    int status;
    PushInt(IPSecureSocket::ReadByte(sctx, status), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    char* buffer = (char*)(array + 3);

    int status; int read = 0;
    char* temp = buffer + offset;
    bool done = false;
    for(long i = 0; !done && i < num; ++i) {
      temp[i] = IPSecureSocket::ReadByte(sctx, status);
      if(!status) {
        done = true;
      }
      else if(status < 0) {
        PushInt(-1, op_stack, stack_pos);
        return true;
      }
      ++read;
    }

    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    wchar_t* buffer = (wchar_t*)(array + 3);
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry heap-overflow fix).
    int read = IPSecureSocket::ReadBytes(byte_buffer, static_cast<int>(num), sctx);
    if(read > -1) {
      byte_buffer[read] = '\0';
      std::wstring in = BytesToUnicode(byte_buffer);
#ifdef _WIN32
      wcsncpy_s(buffer, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(buffer, in.c_str(), in.size());
#endif
      PushInt(in.size(), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    IPSecureSocket::WriteByte((char)value, sctx);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(IPSecureSocket::WriteBytes(buffer + offset, static_cast<int>(num), sctx), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    const std::wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    std::string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(IPSecureSocket::WriteBytes(buffer_out.c_str(), (int)buffer_out.size(), sctx), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

// --- HTTP/2 trap implementations ---

#ifdef OBJECK_HAS_NGHTTP2

static ssize_t h2_send_cb(nghttp2_session*, const uint8_t* data, size_t len, int, void* user_data) {
  Http2SessionCtx* ctx = (Http2SessionCtx*)user_data;
  int ret = IPSecureSocket::WriteBytes((const char*)data, (int)len, ctx->tls);
  if(ret < 0) return NGHTTP2_ERR_CALLBACK_FAILURE;
  return (ssize_t)ret;
}

static ssize_t h2_recv_cb(nghttp2_session*, uint8_t* buf, size_t len, int, void* user_data) {
  Http2SessionCtx* ctx = (Http2SessionCtx*)user_data;
  // Once the response stream is complete, return WOULDBLOCK so nghttp2_session_recv
  // exits gracefully instead of blocking on the next mbedtls_ssl_read.
  if(ctx->response_complete) return NGHTTP2_ERR_WOULDBLOCK;
  int ret = mbedtls_ssl_read(&ctx->tls->ssl, buf, (int)len);
  if(ret > 0) return (ssize_t)ret;
  if(ret == 0) return NGHTTP2_ERR_EOF;
  if(ret == MBEDTLS_ERR_SSL_WANT_READ) return NGHTTP2_ERR_WOULDBLOCK;
  return NGHTTP2_ERR_CALLBACK_FAILURE;
}

static int h2_on_header_cb(nghttp2_session*, const nghttp2_frame* frame,
                             const uint8_t* name, size_t namelen,
                             const uint8_t* value, size_t valuelen,
                             uint8_t, void* user_data) {
  Http2SessionCtx* ctx = (Http2SessionCtx*)user_data;
  if(frame->hd.type == NGHTTP2_HEADERS) {
    std::string k(reinterpret_cast<const char*>(name), namelen);
    std::string v(reinterpret_cast<const char*>(value), valuelen);
    if(k == ":status") {
      ctx->response_status = std::stoi(v);
    }
    else {
      ctx->response_headers[k] = v;
    }
  }
  return 0;
}

static int h2_on_data_chunk_cb(nghttp2_session*, uint8_t, int32_t,
                                const uint8_t* data, size_t len, void* user_data) {
  Http2SessionCtx* ctx = (Http2SessionCtx*)user_data;
  ctx->response_body.insert(ctx->response_body.end(), data, data + len);
  return 0;
}

static int h2_on_stream_close_cb(nghttp2_session*, int32_t stream_id,
                                  uint32_t, void* user_data) {
  Http2SessionCtx* ctx = (Http2SessionCtx*)user_data;
  if(stream_id == ctx->last_stream_id) {
    ctx->response_complete = true;
  }
  return 0;
}

static nghttp2_session* h2_make_session(Http2SessionCtx* ctx) {
  nghttp2_session_callbacks* cbs;
  nghttp2_session_callbacks_new(&cbs);
  nghttp2_session_callbacks_set_send_callback(cbs, h2_send_cb);
  nghttp2_session_callbacks_set_recv_callback(cbs, h2_recv_cb);
  nghttp2_session_callbacks_set_on_header_callback(cbs, h2_on_header_cb);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, h2_on_data_chunk_cb);
  nghttp2_session_callbacks_set_on_stream_close_callback(cbs, h2_on_stream_close_cb);

  nghttp2_session* session = nullptr;
  nghttp2_session_client_new(&session, cbs, ctx);
  nghttp2_session_callbacks_del(cbs);
  return session;
}

static bool h2_run_loop(Http2SessionCtx* ctx) {
  while(!ctx->response_complete) {
    if(nghttp2_session_want_write(ctx->session)) {
      if(nghttp2_session_send(ctx->session) != 0) return false;
    }
    if(nghttp2_session_want_read(ctx->session)) {
      int rc = nghttp2_session_recv(ctx->session);
      // rc==0: success (or WOULDBLOCK from recv_cb); rc<0: error
      if(rc != 0) return false;
    }
    if(ctx->response_complete) break;
    if(!nghttp2_session_want_read(ctx->session) && !nghttp2_session_want_write(ctx->session)) break;
  }
  return true;
}

#endif // OBJECK_HAS_NGHTTP2

bool TrapProcessor::Http2Connect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // Stack: port(Int), host(String), instance
  [[maybe_unused]] const int port = (int)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* host_array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

#ifdef OBJECK_HAS_NGHTTP2
  if(host_array && instance) {
    host_array = (size_t*)host_array[0];
    const std::string host = UnicodeToBytes((wchar_t*)(host_array + 3));

    Http2SessionCtx* ctx = new Http2SessionCtx();
    std::string pem_file;
    if(!IPSecureSocket::OpenH2(host.c_str(), port, pem_file, ctx->tls)) {
      delete ctx;
      instance[0] = 0;
      return true;
    }

    ctx->session = h2_make_session(ctx);

    nghttp2_settings_entry iv[1] = {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}};
    nghttp2_submit_settings(ctx->session, NGHTTP2_FLAG_NONE, iv, 1);
    nghttp2_session_send(ctx->session);

    instance[0] = (size_t)ctx;
  }
#else
  if(instance) instance[0] = 0;
  std::wcerr << L">>> HTTP/2 not available: build with OBJECK_HAS_NGHTTP2 <<<" << std::endl;
#endif
  return true;
}

bool TrapProcessor::Http2Request(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // Stack: body(Byte[]), content_type(String), path(String), method(String), instance
  // On success: sets instance[4]=status, instance[5]=body(Byte[]), instance[6]=content_type(String)
  // Pushes: Bool (1=ok, 0=fail)
  [[maybe_unused]] size_t* body_array   = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* ctype_array  = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* path_array   = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* method_array = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* instance     = (size_t*)PopInt(op_stack, stack_pos);

#ifdef OBJECK_HAS_NGHTTP2
  Http2SessionCtx* ctx = instance ? (Http2SessionCtx*)instance[0] : nullptr;
  if(!ctx || !ctx->session) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  method_array = (size_t*)method_array[0];
  const std::string method = UnicodeToBytes((wchar_t*)(method_array + 3));
  path_array = (size_t*)path_array[0];
  const std::string path = UnicodeToBytes((wchar_t*)(path_array + 3));

  // host is instance[1] (String object)
  size_t* host_str = (size_t*)instance[1];
  if(host_str) host_str = (size_t*)host_str[0];
  const std::string host = host_str ? UnicodeToBytes((wchar_t*)(host_str + 3)) : "";
  // Collect header key/value strings first, then build nva.
  // IMPORTANT: nva stores raw c_str() pointers. These become dangling if the
  // backing vectors reallocate after the pointers are captured. We therefore
  // populate hdr_keys/hdr_vals completely before building nva.
  std::vector<std::string> hdr_keys, hdr_vals;

  std::vector<uint8_t> body_data;
  nghttp2_data_provider* dp_ptr = nullptr;
  nghttp2_data_provider dp;

  hdr_keys.push_back(":method");    hdr_vals.push_back(method);
  hdr_keys.push_back(":path");      hdr_vals.push_back(path);
  hdr_keys.push_back(":scheme");    hdr_vals.push_back("https");
  hdr_keys.push_back(":authority"); hdr_vals.push_back(host);
  for(auto& kv : ctx->request_headers) {
    hdr_keys.push_back(kv.first); hdr_vals.push_back(kv.second);
  }

  if(body_array) {
    // Byte[] layout: [0]=alloc_count, [1]=dims, [2]=actual_count, [3+]=data
    size_t blen = body_array[2];
    uint8_t* bptr = (uint8_t*)(body_array + 3);
    body_data.assign(bptr, bptr + blen);

    if(ctype_array) {
      ctype_array = (size_t*)ctype_array[0];
      hdr_keys.push_back("content-type");
      hdr_vals.push_back(UnicodeToBytes((wchar_t*)(ctype_array + 3)));
    }

    struct BodyCtx { const uint8_t* data; size_t len; size_t pos; };
    static thread_local BodyCtx bctx;
    bctx = {body_data.data(), body_data.size(), 0};
    dp.source.ptr = &bctx;
    dp.read_callback = [](nghttp2_session*, int32_t, uint8_t* buf, size_t len,
                           uint32_t* df, nghttp2_data_source* src, void*) -> ssize_t {
      BodyCtx* bc = (BodyCtx*)src->ptr;
      size_t n = std::min(len, bc->len - bc->pos);
      memcpy(buf, bc->data + bc->pos, n);
      bc->pos += n;
      if(bc->pos == bc->len) *df |= NGHTTP2_DATA_FLAG_EOF;
      return (ssize_t)n;
    };
    dp_ptr = &dp;
  }

  // Build nva AFTER all strings are in their final positions (no more push_back below).
  // Storing c_str() before this point would be unsafe because push_back can reallocate.
  std::vector<nghttp2_nv> nva;
  nva.reserve(hdr_keys.size());
  for(size_t i = 0; i < hdr_keys.size(); i++) {
    nghttp2_nv nv;
    nv.name     = (uint8_t*)hdr_keys[i].c_str();
    nv.namelen  = hdr_keys[i].size();
    nv.value    = (uint8_t*)hdr_vals[i].c_str();
    nv.valuelen = hdr_vals[i].size();
    nv.flags    = NGHTTP2_NV_FLAG_NONE;
    nva.push_back(nv);
  }

  ctx->response_status   = 0;
  ctx->response_complete = false;
  ctx->response_headers.clear();
  ctx->response_body.clear();

  int stream_id = nghttp2_submit_request(ctx->session, nullptr,
                                          nva.data(), nva.size(), dp_ptr, ctx);
  if(stream_id < 0) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }
  ctx->last_stream_id = stream_id;

  if(!h2_run_loop(ctx)) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  // Store results in instance fields [4]=status, [5]=body, [6]=content_type
  const std::string ct = ctx->response_headers.count("content-type")
                          ? ctx->response_headers["content-type"] : "";

  instance[4] = (size_t)ctx->response_status;

  // Allocate Byte[] for the response body. Standard layout:
  //   [0] = allocated element count (size+1), [1] = dims, [2] = actual size, [3+] = data
  const size_t body_size = ctx->response_body.size();
  const size_t body_dim  = 1;
  size_t* body_obj = MemoryManager::AllocateArray(
      body_size + 1 + ((body_dim + 2) * sizeof(size_t)),
      instructions::BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  body_obj[0] = body_size + 1;
  body_obj[1] = body_dim;
  body_obj[2] = body_size;
  if(body_size > 0) {
    memcpy((uint8_t*)(body_obj + 3), ctx->response_body.data(), body_size);
  }
  instance[5] = (size_t)body_obj;
  instance[6] = (size_t)CreateStringObject(BytesToUnicode(ct), program, op_stack, stack_pos);

  PushInt(1, op_stack, stack_pos);
#else
  PushInt(0, op_stack, stack_pos);
#endif
  return true;
}

bool TrapProcessor::Http2Close(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && instance[0]) {
    Http2SessionCtx* ctx = (Http2SessionCtx*)instance[0];
    instance[0] = 0;
    delete ctx;
  }
  return true;
}

// --- HTTP/3 trap implementations ---

#ifdef OBJECK_HAS_NGTCP2

static ngtcp2_tstamp h3_timestamp() {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return (ngtcp2_tstamp)tp.tv_sec * NGTCP2_SECONDS + (ngtcp2_tstamp)tp.tv_nsec;
}

static ngtcp2_conn* h3_get_conn(ngtcp2_crypto_conn_ref* ref) {
  return ((Http3SessionCtx*)ref->user_data)->conn;
}

static void h3_rand_cb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx*) {
  gnutls_rnd(GNUTLS_RND_RANDOM, dest, destlen);
}

static int h3_get_new_connection_id_cb(ngtcp2_conn*, ngtcp2_cid* cid,
                                        uint8_t* token, size_t cidlen, void*) {
  gnutls_rnd(GNUTLS_RND_RANDOM, cid->data, cidlen);
  cid->datalen = cidlen;
  gnutls_rnd(GNUTLS_RND_RANDOM, token, NGTCP2_STATELESS_RESET_TOKENLEN);
  return 0;
}

static int h3_handshake_completed_cb(ngtcp2_conn*, void* user_data) {
  ((Http3SessionCtx*)user_data)->handshake_complete = true;
  return 0;
}

static int h3_recv_stream_data_cb(ngtcp2_conn* conn, uint32_t flags,
                                   int64_t stream_id, uint64_t,
                                   const uint8_t* data, size_t datalen,
                                   void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  if(!ctx->h3conn) return 0;
  int fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) ? 1 : 0;
  if(nghttp3_conn_read_stream(ctx->h3conn, stream_id, data, datalen, fin) < 0)
    return NGTCP2_ERR_CALLBACK_FAILURE;
  ngtcp2_conn_extend_max_stream_offset(conn, stream_id, datalen);
  ngtcp2_conn_extend_max_offset(conn, datalen);
  return 0;
}

static int h3_stream_close_cb(ngtcp2_conn*, uint32_t, int64_t stream_id,
                               uint64_t app_error_code, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  if(ctx->h3conn) nghttp3_conn_close_stream(ctx->h3conn, stream_id, app_error_code);
  return 0;
}

static int h3_stream_stop_sending_cb(ngtcp2_conn*, int64_t stream_id,
                                      uint64_t, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  if(ctx->h3conn) nghttp3_conn_shutdown_stream_read(ctx->h3conn, stream_id);
  return 0;
}

static int h3_stream_reset_cb(ngtcp2_conn*, int64_t stream_id,
                               uint64_t, uint64_t, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  if(ctx->h3conn) nghttp3_conn_shutdown_stream_read(ctx->h3conn, stream_id);
  return 0;
}

// nghttp3 callbacks

static int h3ng_recv_header_cb(nghttp3_conn*, int64_t, int32_t,
                                nghttp3_rcbuf* name, nghttp3_rcbuf* value,
                                uint8_t, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  nghttp3_vec nv = nghttp3_rcbuf_get_buf(name);
  nghttp3_vec vv = nghttp3_rcbuf_get_buf(value);
  std::string k((const char*)nv.base, nv.len);
  std::string v((const char*)vv.base, vv.len);
  if(k == ":status") {
    try { ctx->response_status = std::stoi(v); } catch(...) {}
  } else {
    ctx->response_headers[k] = v;
  }
  return 0;
}

static int h3ng_recv_data_cb(nghttp3_conn*, int64_t, const uint8_t* data,
                              size_t datalen, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  ctx->response_body.insert(ctx->response_body.end(), data, data + datalen);
  return 0;
}

static int h3ng_end_stream_cb(nghttp3_conn*, int64_t stream_id,
                               void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  if(stream_id == ctx->last_stream_id) ctx->response_complete = true;
  return 0;
}

static int h3ng_stream_close_cb(nghttp3_conn*, int64_t stream_id,
                                 uint64_t, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  if(stream_id == ctx->last_stream_id) ctx->response_complete = true;
  return 0;
}

static int h3ng_acked_stream_data_cb(nghttp3_conn*, int64_t, uint64_t, void*, void*) {
  return 0;
}

static int h3ng_deferred_consume_cb(nghttp3_conn*, int64_t stream_id,
                                     size_t consumed, void* user_data, void*) {
  Http3SessionCtx* ctx = (Http3SessionCtx*)user_data;
  ngtcp2_conn_extend_max_stream_offset(ctx->conn, stream_id, consumed);
  ngtcp2_conn_extend_max_offset(ctx->conn, consumed);
  return 0;
}

// Flush pending QUIC/HTTP3 packets to UDP socket.
static bool h3_send_packets(Http3SessionCtx* ctx) {
  uint8_t buf[NGTCP2_MAX_UDP_PAYLOAD_SIZE];
  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_pkt_info pi = {};
  ngtcp2_tstamp ts = h3_timestamp();

  for(;;) {
    ngtcp2_ssize datalen = 0;
    int64_t stream_id = -1;
    const ngtcp2_vec* vec_ptr = nullptr;
    size_t vec_cnt = 0;
    uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_NONE;

    if(ctx->h3conn) {
      nghttp3_vec vec[16];
      nghttp3_ssize vcnt;
      int fin;
      vcnt = nghttp3_conn_writev_stream(ctx->h3conn, &stream_id, &fin, vec, 16);
      if(vcnt < 0) return false;
      if(stream_id >= 0) {
        vec_ptr = (const ngtcp2_vec*)vec;
        vec_cnt = (size_t)vcnt;
        if(fin) flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
      }
    }

    ngtcp2_ssize nwrite = ngtcp2_conn_writev_stream(
        ctx->conn, &ps.path, &pi, buf, sizeof(buf),
        &datalen, flags, stream_id, vec_ptr, vec_cnt, ts);

    if(nwrite < 0) {
      return false;
    }
    if(nwrite < 0 || nwrite == 0) {
      if(nwrite < 0) { return false; }
      break;
    }

    if(stream_id >= 0 && datalen > 0 && ctx->h3conn)
      nghttp3_conn_add_write_offset(ctx->h3conn, stream_id, (size_t)datalen);

    ssize_t sent = sendto(ctx->udp_fd, buf, (size_t)nwrite, 0,
                          (const struct sockaddr*)&ctx->remote_addr, ctx->remote_addrlen);
    if(sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
  }
  return true;
}

// Drain all pending UDP datagrams into ngtcp2.
static bool h3_recv_packets(Http3SessionCtx* ctx) {
  uint8_t buf[65536];
  struct sockaddr_storage remote;
  socklen_t remotelen = sizeof(remote);

  for(;;) {
    ssize_t nread = recvfrom(ctx->udp_fd, buf, sizeof(buf), 0,
                              (struct sockaddr*)&remote, &remotelen);
    if(nread < 0) {
      if(errno == EAGAIN || errno == EWOULDBLOCK) return true;
      return false;
    }
    ngtcp2_path path;
    path.local.addrlen  = ctx->local_addrlen;
    path.local.addr     = (struct sockaddr*)&ctx->local_addr;
    path.remote.addrlen = remotelen;
    path.remote.addr    = (struct sockaddr*)&remote;
    ngtcp2_pkt_info pi = {};
    int ret = ngtcp2_conn_read_pkt(ctx->conn, &path, &pi, buf, (size_t)nread, h3_timestamp());
    if(ret == NGTCP2_ERR_DRAINING) {
      return true;
    }
    if(ret < 0) {
      return false;
    }
  }
}

// Create the nghttp3 session and open the three client-side control streams.
static nghttp3_conn* h3_make_h3conn(Http3SessionCtx* ctx) {
  nghttp3_settings h3s;
  nghttp3_settings_default(&h3s);
  nghttp3_callbacks h3cbs = {};
  h3cbs.recv_header       = h3ng_recv_header_cb;
  h3cbs.recv_data         = h3ng_recv_data_cb;
  h3cbs.stream_close      = h3ng_stream_close_cb;
  h3cbs.end_stream        = h3ng_end_stream_cb;
  h3cbs.acked_stream_data = h3ng_acked_stream_data_cb;
  h3cbs.deferred_consume  = h3ng_deferred_consume_cb;

  nghttp3_conn* h3conn = nullptr;
  if(nghttp3_conn_client_new(&h3conn, &h3cbs, &h3s, nullptr, ctx) != 0) return nullptr;

  int64_t ctrl_id, qenc_id, qdec_id;
  if(ngtcp2_conn_open_uni_stream(ctx->conn, &ctrl_id, nullptr) != 0 ||
     ngtcp2_conn_open_uni_stream(ctx->conn, &qenc_id, nullptr) != 0 ||
     ngtcp2_conn_open_uni_stream(ctx->conn, &qdec_id, nullptr) != 0 ||
     nghttp3_conn_bind_control_stream(h3conn, ctrl_id) != 0 ||
     nghttp3_conn_bind_qpack_streams(h3conn, qenc_id, qdec_id) != 0) {
    nghttp3_conn_del(h3conn);
    return nullptr;
  }
  return h3conn;
}

// Drive QUIC I/O until response_complete or until the handshake just finished
// (in which case it returns true with h3conn set, ready for request submission).
static bool h3_run_loop(Http3SessionCtx* ctx) {
  struct timespec tstart;
  clock_gettime(CLOCK_MONOTONIC, &tstart);

  while(!ctx->response_complete) {
    struct timespec tnow;
    clock_gettime(CLOCK_MONOTONIC, &tnow);
    double elapsed = (tnow.tv_sec - tstart.tv_sec) +
                     (tnow.tv_nsec - tstart.tv_nsec) * 1e-9;
    if(elapsed > 30.0) return false;

    if(!h3_send_packets(ctx)) return false;

    // After TLS handshake completes, set up the HTTP/3 session once.
    // Return to let the caller submit a request before looping again.
    if(ctx->handshake_complete && !ctx->h3conn) {
      ctx->h3conn = h3_make_h3conn(ctx);
      return ctx->h3conn != nullptr;
    }

    ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(ctx->conn);
    ngtcp2_tstamp now_ts = h3_timestamp();
    int poll_ms;
    if(expiry <= now_ts) {
      poll_ms = 0;
    } else if(expiry == UINT64_MAX) {
      poll_ms = 500;
    } else {
      uint64_t diff = (expiry - now_ts) / 1000000;
      poll_ms = (int)std::min(diff, (uint64_t)500u);
    }

    struct pollfd pfd = { ctx->udp_fd, POLLIN, 0 };
    int n = poll(&pfd, 1, poll_ms);

    if(n > 0 && (pfd.revents & POLLIN)) {
      if(!h3_recv_packets(ctx)) return false;
    }

    // Handle QUIC timer expiry
    ngtcp2_tstamp ts = h3_timestamp();
    if(ngtcp2_conn_get_expiry(ctx->conn) <= ts) {
      int expiry_ret = ngtcp2_conn_handle_expiry(ctx->conn, ts);
      if(expiry_ret != 0) return false;
    }

#if defined(NGTCP2_VERSION_NUM) && NGTCP2_VERSION_NUM >= 0x010000
    bool drain = ngtcp2_conn_in_draining_period(ctx->conn);
    bool close = ngtcp2_conn_in_closing_period(ctx->conn);
#else
    bool drain = ngtcp2_conn_is_in_draining_period(ctx->conn);
    bool close = ngtcp2_conn_is_in_closing_period(ctx->conn);
#endif
    if(drain || close) {
      return false;
    }
  }
  return true;
}

#endif // OBJECK_HAS_NGTCP2

bool TrapProcessor::Http3Connect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // Stack: port(Int), host(String), instance
  [[maybe_unused]] const int port = (int)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* host_array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance   = (size_t*)PopInt(op_stack, stack_pos);

#ifdef OBJECK_HAS_NGTCP2
  if(!host_array || !instance) { if(instance) instance[0] = 0; return true; }

  host_array = (size_t*)host_array[0];
  const std::string host = UnicodeToBytes((wchar_t*)(host_array + 3));

  Http3SessionCtx* ctx = new Http3SessionCtx();
  ctx->host = host;

  // Resolve hostname and create non-blocking UDP socket
  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%d", port);
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  if(getaddrinfo(host.c_str(), portstr, &hints, &res) != 0) {
    delete ctx; instance[0] = 0; return true;
  }

  ctx->udp_fd = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if(ctx->udp_fd < 0) {
    freeaddrinfo(res); delete ctx; instance[0] = 0; return true;
  }
  int fflags = fcntl(ctx->udp_fd, F_GETFL, 0);
  fcntl(ctx->udp_fd, F_SETFL, fflags | O_NONBLOCK);

  // Bind to ephemeral local address
  struct sockaddr_storage local_ss = {};
  if(res->ai_family == AF_INET6) {
    struct sockaddr_in6* la = (struct sockaddr_in6*)&local_ss;
    la->sin6_family = AF_INET6;
    la->sin6_addr   = in6addr_any;
    la->sin6_port   = 0;
    ctx->local_addrlen = sizeof(*la);
  } else {
    struct sockaddr_in* la = (struct sockaddr_in*)&local_ss;
    la->sin_family      = AF_INET;
    la->sin_addr.s_addr = INADDR_ANY;
    la->sin_port        = 0;
    ctx->local_addrlen  = sizeof(*la);
  }
  if(bind(ctx->udp_fd, (struct sockaddr*)&local_ss, ctx->local_addrlen) < 0) {
    freeaddrinfo(res); delete ctx; instance[0] = 0; return true;
  }
  socklen_t actual_local = sizeof(ctx->local_addr);
  getsockname(ctx->udp_fd, (struct sockaddr*)&ctx->local_addr, &actual_local);
  ctx->local_addrlen = actual_local;

  memcpy(&ctx->remote_addr, res->ai_addr, res->ai_addrlen);
  ctx->remote_addrlen = (socklen_t)res->ai_addrlen;
  freeaddrinfo(res);

  // Set up GnuTLS session for QUIC (TLS 1.3 + ALPN "h3")
  gnutls_certificate_allocate_credentials(&ctx->cred);
  gnutls_certificate_set_x509_system_trust(ctx->cred);
  if(gnutls_init(&ctx->tls_session, GNUTLS_CLIENT | GNUTLS_NONBLOCK) < 0) {
    delete ctx; instance[0] = 0; return true;
  }
  gnutls_credentials_set(ctx->tls_session, GNUTLS_CRD_CERTIFICATE, ctx->cred);
  gnutls_server_name_set(ctx->tls_session, GNUTLS_NAME_DNS, host.c_str(), host.size());
  // Verify the server certificate chain (against system trust set above) and
  // that it matches the requested hostname. Without this GnuTLS performs no peer
  // verification, so the QUIC handshake would complete against any certificate
  // (MITM). On failure the handshake aborts with a verification error. Operators
  // can skip this for testing via OBJECK_TLS_INSECURE_SKIP_VERIFY.
  if(!IPSecureSocket::InsecureSkipVerify()) {
    gnutls_session_set_verify_cert(ctx->tls_session, host.c_str(), 0);
  }
  gnutls_priority_set_direct(ctx->tls_session,
      "NORMAL:-VERS-ALL:+VERS-TLS1.3", nullptr);
  gnutls_datum_t alpn_h3 = { (unsigned char*)"h3", 2 };
  gnutls_alpn_set_protocols(ctx->tls_session, &alpn_h3, 1, 0);

  // Link GnuTLS session to ngtcp2 via conn_ref
  ctx->conn_ref.get_conn  = h3_get_conn;
  ctx->conn_ref.user_data = ctx;
  gnutls_session_set_ptr(ctx->tls_session, &ctx->conn_ref);
  ngtcp2_crypto_gnutls_configure_client_session(ctx->tls_session);

  // Generate random QUIC connection IDs
  ngtcp2_cid dcid, scid;
  dcid.datalen = 20;
  scid.datalen = 8;
  gnutls_rnd(GNUTLS_RND_RANDOM, dcid.data, dcid.datalen);
  gnutls_rnd(GNUTLS_RND_RANDOM, scid.data, scid.datalen);

  // Build the QUIC path
  ngtcp2_path path;
  path.local.addr     = (struct sockaddr*)&ctx->local_addr;
  path.local.addrlen  = ctx->local_addrlen;
  path.remote.addr    = (struct sockaddr*)&ctx->remote_addr;
  path.remote.addrlen = ctx->remote_addrlen;

  // ngtcp2 settings and transport params
  ngtcp2_settings settings;
  ngtcp2_settings_default(&settings);
  settings.initial_ts = h3_timestamp();

  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_stream_data_bidi_local  = 256 * 1024;
  params.initial_max_stream_data_bidi_remote = 256 * 1024;
  params.initial_max_stream_data_uni         = 256 * 1024;
  params.initial_max_data                    = 1 * 1024 * 1024;
  params.initial_max_streams_bidi            = 100;
  params.initial_max_streams_uni             = 3;

  // ngtcp2 callbacks: crypto ones come from libngtcp2_crypto, app ones defined above
  ngtcp2_callbacks cbs = {};
  cbs.client_initial           = ngtcp2_crypto_client_initial_cb;
  cbs.recv_crypto_data         = ngtcp2_crypto_recv_crypto_data_cb;
  cbs.encrypt                  = ngtcp2_crypto_encrypt_cb;
  cbs.decrypt                  = ngtcp2_crypto_decrypt_cb;
  cbs.hp_mask                  = ngtcp2_crypto_hp_mask_cb;
  cbs.update_key               = ngtcp2_crypto_update_key_cb;
  cbs.delete_crypto_aead_ctx   = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  cbs.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  cbs.get_path_challenge_data  = ngtcp2_crypto_get_path_challenge_data_cb;
  cbs.recv_retry               = ngtcp2_crypto_recv_retry_cb;
  cbs.handshake_completed      = h3_handshake_completed_cb;
  cbs.recv_stream_data         = h3_recv_stream_data_cb;
  cbs.stream_close             = h3_stream_close_cb;
  cbs.stream_stop_sending      = h3_stream_stop_sending_cb;
  cbs.stream_reset             = h3_stream_reset_cb;
  cbs.rand                     = h3_rand_cb;
  cbs.get_new_connection_id    = h3_get_new_connection_id_cb;

  int conn_ret = ngtcp2_conn_client_new(&ctx->conn, &dcid, &scid, &path,
                              NGTCP2_PROTO_VER_V1, &cbs, &settings, &params,
                              nullptr, ctx);
  if(conn_ret != 0) {
    delete ctx; instance[0] = 0; return true;
  }
  ngtcp2_conn_set_tls_native_handle(ctx->conn, ctx->tls_session);

  // Drive QUIC handshake; h3_run_loop returns once nghttp3 session is ready
  if(!h3_run_loop(ctx)) {
    delete ctx; instance[0] = 0; return true;
  }

  instance[0] = (size_t)ctx;
#else
  if(instance) instance[0] = 0;
  std::wcerr << L">>> HTTP/3 not available: build with OBJECK_HAS_NGTCP2 <<<" << std::endl;
#endif
  return true;
}

bool TrapProcessor::Http3Request(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // Stack: body(Byte[]), content_type(String), path(String), method(String), instance
  // Result stored in instance[4]=status, instance[5]=body, instance[6]=content_type
  // Pushes: Bool (1=success)
  [[maybe_unused]] size_t* body_array   = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* ctype_array  = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* path_array   = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* method_array = (size_t*)PopInt(op_stack, stack_pos);
  [[maybe_unused]] size_t* instance     = (size_t*)PopInt(op_stack, stack_pos);

#ifdef OBJECK_HAS_NGTCP2
  Http3SessionCtx* ctx = instance ? (Http3SessionCtx*)instance[0] : nullptr;
  if(!ctx || !ctx->conn || !ctx->h3conn) {
    PushInt(0, op_stack, stack_pos); return true;
  }

  method_array = (size_t*)method_array[0];
  const std::string method = UnicodeToBytes((wchar_t*)(method_array + 3));
  path_array   = (size_t*)path_array[0];
  const std::string path   = UnicodeToBytes((wchar_t*)(path_array + 3));

  size_t* host_str = (size_t*)instance[1];
  if(host_str) host_str = (size_t*)host_str[0];
  const std::string host = host_str ? UnicodeToBytes((wchar_t*)(host_str + 3)) : ctx->host;

  // Collect header strings before building nva to avoid dangling c_str() pointers
  std::vector<std::string> hdr_keys, hdr_vals;
  hdr_keys.push_back(":method");    hdr_vals.push_back(method);
  hdr_keys.push_back(":path");      hdr_vals.push_back(path);
  hdr_keys.push_back(":scheme");    hdr_vals.push_back("https");
  hdr_keys.push_back(":authority"); hdr_vals.push_back(host);
  for(auto& kv : ctx->request_headers) {
    hdr_keys.push_back(kv.first); hdr_vals.push_back(kv.second);
  }

  // Read request body into a local vector (lives through h3_run_loop)
  std::vector<uint8_t> body_data;
  if(body_array) {
    size_t blen = body_array[2];
    uint8_t* bptr = (uint8_t*)(body_array + 3);
    body_data.assign(bptr, bptr + blen);
    if(ctype_array) {
      ctype_array = (size_t*)ctype_array[0];
      hdr_keys.push_back("content-type");
      hdr_vals.push_back(UnicodeToBytes((wchar_t*)(ctype_array + 3)));
    }
  }

  // Build nghttp3_nv array after all push_backs are done
  std::vector<nghttp3_nv> nva;
  nva.reserve(hdr_keys.size());
  for(size_t i = 0; i < hdr_keys.size(); i++) {
    nghttp3_nv nv;
    nv.name     = (uint8_t*)hdr_keys[i].c_str();
    nv.namelen  = hdr_keys[i].size();
    nv.value    = (uint8_t*)hdr_vals[i].c_str();
    nv.valuelen = hdr_vals[i].size();
    nv.flags    = NGHTTP3_NV_FLAG_NONE;
    nva.push_back(nv);
  }

  ctx->response_status   = 0;
  ctx->response_complete = false;
  ctx->response_headers.clear();
  ctx->response_body.clear();

  // Open bidirectional QUIC stream for the HTTP/3 request
  int64_t stream_id;
  if(ngtcp2_conn_open_bidi_stream(ctx->conn, &stream_id, nullptr) != 0) {
    PushInt(0, op_stack, stack_pos); return true;
  }
  ctx->last_stream_id = stream_id;

  // Optional request body reader
  struct H3BodyCtx { const uint8_t* data; size_t len; size_t pos; };
  static thread_local H3BodyCtx h3_bctx;
  nghttp3_data_reader dr;
  nghttp3_data_reader* dr_ptr = nullptr;
  if(!body_data.empty()) {
    h3_bctx = { body_data.data(), body_data.size(), 0 };
    dr.read_data = [](nghttp3_conn*, int64_t, nghttp3_vec* vec, size_t,
                       uint32_t* pflags, void*, void* sdata) -> nghttp3_ssize {
      H3BodyCtx* bc = (H3BodyCtx*)sdata;
      if(bc->pos >= bc->len) { *pflags = NGHTTP3_DATA_FLAG_EOF; return 0; }
      vec[0].base = (uint8_t*)bc->data + bc->pos;
      vec[0].len  = bc->len - bc->pos;
      bc->pos = bc->len;
      *pflags = NGHTTP3_DATA_FLAG_EOF;
      return 1;
    };
    dr_ptr = &dr;
  }

  if(nghttp3_conn_submit_request(ctx->h3conn, stream_id,
                                  nva.data(), nva.size(), dr_ptr,
                                  dr_ptr ? (void*)&h3_bctx : nullptr) != 0) {
    PushInt(0, op_stack, stack_pos); return true;
  }

  if(!h3_run_loop(ctx)) {
    PushInt(0, op_stack, stack_pos); return true;
  }

  // Store results in instance fields [4]=status, [5]=body(Byte[]), [6]=content_type
  const std::string ct = ctx->response_headers.count("content-type")
                          ? ctx->response_headers["content-type"] : "";
  instance[4] = (size_t)ctx->response_status;

  const size_t body_size = ctx->response_body.size();
  const size_t body_dim  = 1;
  size_t* body_obj = MemoryManager::AllocateArray(
      body_size + 1 + ((body_dim + 2) * sizeof(size_t)),
      instructions::BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  body_obj[0] = body_size + 1;
  body_obj[1] = body_dim;
  body_obj[2] = body_size;
  if(body_size > 0)
    memcpy((uint8_t*)(body_obj + 3), ctx->response_body.data(), body_size);
  instance[5] = (size_t)body_obj;
  instance[6] = (size_t)CreateStringObject(BytesToUnicode(ct), program, op_stack, stack_pos);

  PushInt(1, op_stack, stack_pos);
#else
  PushInt(0, op_stack, stack_pos);
#endif
  return true;
}

bool TrapProcessor::Http3Close(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && instance[0]) {
    Http3SessionCtx* ctx = (Http3SessionCtx*)instance[0];
    instance[0] = 0;
    delete ctx;
  }
  return true;
}

// --- DTLS trap implementations ---

bool TrapProcessor::SockDtlsConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const bool verify = (bool)PopInt(op_stack, stack_pos);
  size_t* pem_file_array = (size_t*)PopInt(op_stack, stack_pos);
  const long port = (long)PopInt(op_stack, stack_pos);
  size_t* addr_array = (size_t*)PopInt(op_stack, stack_pos);

  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(addr_array && instance) {
    addr_array = (size_t*)addr_array[0];
    const std::string addr = UnicodeToBytes((wchar_t*)(addr_array + 3));

    std::string pem_file;
    if(pem_file_array) {
      pem_file_array = (size_t*)pem_file_array[0];
      pem_file = UnicodeToBytes((wchar_t*)(pem_file_array + 3));
    }

    // Close existing connection if any
    IPDtlsSocket::Close((DtlsSocketCtx*)instance[0]);

    DtlsSocketCtx* sctx = nullptr;
    const bool is_open = IPDtlsSocket::Open(addr.c_str(), static_cast<int>(port), pem_file, sctx, verify);
    if(is_open) {
      instance[0] = (size_t)sctx;
      instance[1] = 0;
      instance[2] = 0;
      instance[3] = 1;

#ifdef _DEBUG
      std::wcout << L"# dtls socket connect: addr='" << BytesToUnicode(addr) << L"'; instance="
      << instance << L"(" << (size_t)instance << L")" << L"; sctx=" << sctx << L"("
      << (size_t)sctx << L") #" << std::endl;
#endif
    }
  }

  return true;
}

bool TrapProcessor::SockDtlsIssuer(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
  if(sctx) {
    const mbedtls_x509_crt* peer_cert = mbedtls_ssl_get_peer_cert(&sctx->ssl);
    if(peer_cert) {
      char buffer[LARGE_BUFFER_MAX] = {0};
      mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, &peer_cert->issuer);
      const std::wstring in = BytesToUnicode(buffer);
      PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
  if(sctx) {
    const mbedtls_x509_crt* peer_cert = mbedtls_ssl_get_peer_cert(&sctx->ssl);
    if(peer_cert) {
      char buffer[LARGE_BUFFER_MAX] = {0};
      mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, &peer_cert->subject);
      const std::wstring in = BytesToUnicode(buffer);
      PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];

#ifdef _DEBUG
  std::wcout << L"# dtls socket close: sctx=" << sctx << L"("
    << (size_t)sctx << L") #" << std::endl;
#endif
  IPDtlsSocket::Close(sctx);
  instance[0] = instance[1] = instance[2] = instance[3] = 0;

  return true;
}

bool TrapProcessor::SockDtlsOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    if(instance[3] && sctx) {
      const std::string out = UnicodeToBytes((wchar_t*)(array + 3));
      IPDtlsSocket::WriteBytes(out.c_str(), (int)out.size(), sctx);
    }
  }

  return true;
}

bool TrapProcessor::SockDtlsInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && instance) {
    char buffer[MID_BUFFER_MAX] = {0};
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    int status;
    if(instance[3] && sctx) {
      size_t index = 0;
      char value;
      bool end_line = false;
      do {
        value = IPDtlsSocket::ReadByte(sctx, status);
        if(value != '\0' && value != '\r' && value != '\n' && index < MID_BUFFER_MAX - 1 && status > 0) {
          buffer[index++] = value;
        }
        else {
          end_line = true;
        }
      }
      while(!end_line && index < array[0] - 1);
      buffer[index] = '\0';

      // assume LF
      if(value == '\r') {
        IPDtlsSocket::ReadByte(sctx, status);
      }

      // copy content
      const std::wstring in = BytesToUnicode(buffer);
      wchar_t* out = (wchar_t*)(array + 3);
#ifdef _WIN32
      wcsncpy_s(out, array[0], in.c_str(), in.size());
#else
      wcsncpy(out, in.c_str(), in.size());
#endif
    }
  }

  return true;
}

bool TrapProcessor::SockDtlsListen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    size_t* cert_obj = (size_t*)instance[3];
    size_t* key_obj = (size_t*)instance[4];
    size_t* passwd_obj = (size_t*)instance[5];
    const long port = (long)instance[6];

    if(cert_obj && key_obj) {
      DtlsServerCtx* srv = new DtlsServerCtx();

      // Seed the random number generator
      const char* pers = "objeck_dtls_server";
      int ret = mbedtls_ctr_drbg_seed(&srv->ctr_drbg, mbedtls_entropy_func, &srv->entropy,
                                       (const unsigned char*)pers, strlen(pers));
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Load server certificate
      const std::string cert_path = UnicodeToBytes((wchar_t*)((size_t*)cert_obj[0] + 3));
      ret = mbedtls_x509_crt_parse_file(&srv->srvcert, cert_path.c_str());
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Load private key (with optional password)
      const std::string key_path = UnicodeToBytes((wchar_t*)((size_t*)key_obj[0] + 3));
      const char* passwd_cstr = nullptr;
      std::string passwd;
      if(passwd_obj) {
        const std::wstring passwd_str((wchar_t*)((size_t*)passwd_obj[0] + 3));
        if(!passwd_str.empty()) {
          passwd = UnicodeToBytes(passwd_str);
          passwd_cstr = passwd.c_str();
        }
      }

#if MBEDTLS_VERSION_MAJOR >= 3
      ret = mbedtls_pk_parse_keyfile(&srv->pkey, key_path.c_str(), passwd_cstr,
                                      mbedtls_ctr_drbg_random, &srv->ctr_drbg);
#else
      ret = mbedtls_pk_parse_keyfile(&srv->pkey, key_path.c_str(), passwd_cstr);
#endif
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Bind to port
      const std::string port_str = std::to_string(port);
      ret = mbedtls_net_bind(&srv->listen_fd, nullptr, port_str.c_str(), MBEDTLS_NET_PROTO_UDP);
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      // Configure SSL for DTLS server
      ret = mbedtls_ssl_config_defaults(&srv->conf, MBEDTLS_SSL_IS_SERVER,
                                         MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
      if(ret != 0) {
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return true;
      }

      mbedtls_ssl_conf_rng(&srv->conf, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      mbedtls_ssl_conf_own_cert(&srv->conf, &srv->srvcert, &srv->pkey);

      // Setup DTLS cookies
      ret = mbedtls_ssl_cookie_setup(&srv->cookie_ctx, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      if(ret != 0) {
        srv->last_error = ret;
        delete srv;
        srv = nullptr;
        return true;
      }
      mbedtls_ssl_conf_dtls_cookies(&srv->conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &srv->cookie_ctx);

      instance[0] = (size_t)srv;
      instance[1] = 0;
      instance[2] = 0;

      PushInt(1, op_stack, stack_pos);
      return true;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockDtlsAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    DtlsServerCtx* srv = (DtlsServerCtx*)instance[0];

    if(srv) {
      DtlsSocketCtx* client_sctx = new DtlsSocketCtx();

      // Accept raw connection. accept() blocks indefinitely; park so STW GC
      // elsewhere can proceed (see SockTcpSslAccept for the re-rooting rationale)
      PushInt((size_t)instance, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      int ret = mbedtls_net_accept(&srv->listen_fd, &client_sctx->net, nullptr, 0, nullptr);
      MemoryManager::EndBlocking();
      instance = (size_t*)PopInt(op_stack, stack_pos);
      if(ret != 0) {
        delete client_sctx;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      // Setup SSL session for this client (uses server's config)
      ret = mbedtls_ssl_setup(&client_sctx->ssl, &srv->conf);
      if(ret != 0) {
        delete client_sctx;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      mbedtls_ssl_set_bio(&client_sctx->ssl, &client_sctx->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
      mbedtls_ssl_set_timer_cb(&client_sctx->ssl, &client_sctx->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

      // Perform DTLS handshake with cookie verification
      int retries = 0;
      const int max_retries = 10;
      while((ret = mbedtls_ssl_handshake(&client_sctx->ssl)) != 0) {
        if(ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
          if(++retries > max_retries) {
            IPDtlsSocket::Close(client_sctx);
            PushInt(0, op_stack, stack_pos);
            return true;
          }
          mbedtls_ssl_session_reset(&client_sctx->ssl);
          mbedtls_ssl_set_bio(&client_sctx->ssl, &client_sctx->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
          mbedtls_ssl_set_timer_cb(&client_sctx->ssl, &client_sctx->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);
          continue;
        }
        if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
          IPDtlsSocket::Close(client_sctx);
          PushInt(0, op_stack, stack_pos);
          return true;
        }
      }

      // Get peer address info
      int sock_fd = client_sctx->net.fd;

      struct sockaddr_storage pin;
      memset(&pin, 0, sizeof(pin));
      socklen_t pen_len = sizeof(pin);
      int status = getpeername(sock_fd, (sockaddr*)&pin, &pen_len);
      if(status < 0) {
        delete client_sctx;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      char host_name[NI_MAXHOST] = {0};
      char port[NI_MAXSERV] = {0};
      status = getnameinfo((struct sockaddr*)&pin, pen_len, host_name, sizeof(host_name), port,
                           sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
      if(status < 0) {
        delete client_sctx;
        PushInt(0, op_stack, stack_pos);
        return true;
      }

      size_t* sock_obj = MemoryManager::AllocateObject(program->GetDtlsSocketObjectId(), op_stack, *stack_pos, false);
      sock_obj[0] = (size_t)client_sctx;
      sock_obj[1] = 0;
      sock_obj[3] = 1;
      sock_obj[4] = (size_t)CreateStringObject(BytesToUnicode(host_name), program, op_stack, stack_pos);
      sock_obj[5] = instance[6];

      PushInt((size_t)sock_obj, op_stack, stack_pos);
      return true;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockDtlsSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
   const bool is_write = (bool)PopInt(op_stack, stack_pos);
   size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

   if(instance && instance[0]) {
      DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
      PushInt(dtls_ready_for_io(sctx, is_write), op_stack, stack_pos);
      return true;
   }

   PushInt(-1, op_stack, stack_pos);
   return true;
}

bool TrapProcessor::SockDtlsCertSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  DtlsServerCtx* srv = (DtlsServerCtx*)instance[0];
  if(srv) {
    char buffer[LARGE_BUFFER_MAX] = {0};
    mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, &srv->srvcert.issuer);
    const std::wstring in = BytesToUnicode(buffer);
    PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    if(sctx && sctx->last_error != 0) {
      char error_buf[256] = {0};
      mbedtls_strerror(sctx->last_error, error_buf, sizeof(error_buf) - 1);
      const std::wstring error_msg = BytesToUnicode(error_buf);
      PushInt((size_t)CreateStringObject(error_msg, program, op_stack, stack_pos), op_stack, stack_pos);
      return true;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::SockDtlsCloseSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    DtlsServerCtx* srv = (DtlsServerCtx*)instance[0];
    if(srv) {
      instance[0] = 0;
      delete srv;
    }
  }

  return true;
}

bool TrapProcessor::SockDtlsInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    int status;
    PushInt(IPDtlsSocket::ReadByte(sctx, status), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    char* buffer = (char*)(array + 3);
    int read = IPDtlsSocket::ReadBytes(buffer + offset, static_cast<int>(num), sctx);
    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    wchar_t* buffer = (wchar_t*)(array + 3);
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry heap-overflow fix).
    int read = IPDtlsSocket::ReadBytes(byte_buffer, static_cast<int>(num), sctx);
    if(read > -1) {
      byte_buffer[read] = '\0';
      std::wstring in = BytesToUnicode(byte_buffer);
#ifdef _WIN32
      wcsncpy_s(buffer, array[0] + 1, in.c_str(), in.size());
#else
      wcsncpy(buffer, in.c_str(), in.size());
#endif
      PushInt(in.size(), op_stack, stack_pos);
    }
    else {
      PushInt(-1, op_stack, stack_pos);
    }
    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    IPDtlsSocket::WriteByte((char)value, sctx);
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(IPDtlsSocket::WriteBytes(buffer + offset, static_cast<int>(num), sctx), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    const std::wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    std::string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(IPDtlsSocket::WriteBytes(buffer_out.c_str(), (int)buffer_out.size(), sctx), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if((FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    const int value = fgetc(file);
    PushInt(value < 0 ? 0 : value, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  
  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    FILE* file = (FILE*)instance[0];
    wchar_t* out = (wchar_t*)(array + 3);

    // read from file
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not at +offset: byte_buffer is sized for
    // 'num' only, and 'offset' was bounds-checked against the destination array,
    // not this buffer — a large offset overflowed the heap allocation.
    const size_t read = fread(byte_buffer, 1, num, file);
    byte_buffer[read] = '\0';
    std::wstring in(BytesToUnicode(byte_buffer));
    
    // remove file BOM UTF (8, 16, 32)
    if(in.size() > 0 && ((size_t)in[0] == 0xFEFF || (size_t)in[0] == 0xFFFE || (size_t)in[0] == 0xFFFE0000 || (size_t)in[0] == 0xEFBBBF)) {
      in.erase(in.begin(), in.begin() + 1);
    }

    // copy
#ifdef _WIN32
    wcsncpy_s(out, array[0] + 1, in.c_str(), in.size());
#else
    wcsncpy(out, in.c_str(), array[2]);
#endif

    // clean up
    delete[] byte_buffer;
    byte_buffer = nullptr;

    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    FILE* file = (FILE*)instance[0];
    char* buffer = (char*)(array + 3);
    const size_t read = fread(buffer + offset, 1, num, file);
    PushInt(read, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const int value = (int)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    if(fputc(value, file) != value) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    FILE* file = (FILE*)instance[0];
    char* buffer = (char*)(array + 3);
    PushInt(fwrite(buffer + offset, 1, num, file), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    FILE* file = (FILE*)instance[0];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    const std::wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    std::string buffer_out = UnicodeToBytes(sub_buffer);
    PushInt(fwrite(buffer_out.c_str(), 1, buffer_out.size(), file), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileSeek(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  long pos = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    if(fseek(file, pos, SEEK_CUR) != 0) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileEof(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    FILE* file = (FILE*)instance[0];
    PushInt(feof(file) != 0, op_stack, stack_pos);
  }
  else {
    PushInt(1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileIsOpen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (FILE*)instance[0]) {
    PushInt(1, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileCanWriteOnly(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(File::FileWriteOnly(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileCanReadOnly(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(File::FileReadOnly(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileCanReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::wstring wname((wchar_t*)(array + 3));
    const std::string name =  UnicodeToBytes(wname);
    PushInt(File::FileReadWrite(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileExists(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(File::FileExists(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileSize(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(File::FileSize(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileTempName(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  const std::string full_path = File::TempName();
  if(full_path.size() > 0) {
    const std::wstring wfull_path(full_path.begin(), full_path.end());
    const size_t str_obj = (size_t)CreateStringObject(wfull_path, program, op_stack, stack_pos);
    PushInt(str_obj, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileFullPath(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    std::string full_path = File::FullPathName(name);
    if(full_path.size() > 0) {
      const std::wstring wfull_path(full_path.begin(), full_path.end());
      const size_t str_obj = (size_t)CreateStringObject(wfull_path, program, op_stack, stack_pos);
      PushInt(str_obj, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }

  return true;
}

bool TrapProcessor::FileAccountOwner(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    ProcessFileOwner(name.c_str(), true, program, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileGroupOwner(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    ProcessFileOwner(name.c_str(), false, program, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileDelete(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(std::filesystem::remove(name.c_str()), op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileRename(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* to = (size_t*)PopInt(op_stack, stack_pos);
  size_t* from = (size_t*)PopInt(op_stack, stack_pos);

  if(!to || !from) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  to = (size_t*)to[0];
  const std::string to_name = UnicodeToBytes((wchar_t*)(to + 3));

  from = (size_t*)from[0];
  const std::string from_name = UnicodeToBytes((wchar_t*)(from + 3));

  if(rename(from_name.c_str(), to_name.c_str()) != 0) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    PushInt(1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool overwrite = PopInt(op_stack, stack_pos);
  size_t* to = (size_t*)PopInt(op_stack, stack_pos);
  size_t* from = (size_t*)PopInt(op_stack, stack_pos);

  if(!to || !from) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  to = (size_t*)to[0];
  const std::string to_name = UnicodeToBytes((wchar_t*)(to + 3));

  from = (size_t*)from[0];
  const std::string from_name = UnicodeToBytes((wchar_t*)(from + 3));

  std::filesystem::copy_options options = std::filesystem::copy_options::none;
  if(overwrite) {
    options |= std::filesystem::copy_options::overwrite_existing;
  }

  std::error_code error_code;
  std::filesystem::copy_file(from_name, to_name, options, error_code);
  if(error_code) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    PushInt(1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool recursive = PopInt(op_stack, stack_pos);
  size_t* to = (size_t*)PopInt(op_stack, stack_pos);
  size_t* from = (size_t*)PopInt(op_stack, stack_pos);

  if(!to || !from) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  to = (size_t*)to[0];
  const std::string to_name = UnicodeToBytes((wchar_t*)(to + 3));

  from = (size_t*)from[0];
  const std::string from_name = UnicodeToBytes((wchar_t*)(from + 3));

  if(File::DirExists(from_name.c_str())) {
    std::filesystem::copy_options copy_options = std::filesystem::copy_options::overwrite_existing;
    if(recursive) {
      copy_options |= std::filesystem::copy_options::recursive;
    }

    std::error_code error_code;
    std::filesystem::copy(from_name, to_name, copy_options, error_code);
    if(error_code) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }
  
  return true;
}

bool TrapProcessor::FileCreateTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const bool is_gmt = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    time_t raw_time = File::FileCreatedTime(name.c_str());
    if(raw_time > 0) {
      struct tm* curr_time;
      const bool got_time = GetTime(curr_time, raw_time, is_gmt);

      if(got_time) {
        frame->mem[3] = curr_time->tm_mday;          // day
        frame->mem[4] = curr_time->tm_mon + 1;       // month
        frame->mem[5] = curr_time->tm_year + 1900;   // year
        frame->mem[6] = curr_time->tm_hour;          // hours
        frame->mem[7] = curr_time->tm_min;           // mins
        frame->mem[8] = curr_time->tm_sec;           // secs
      }
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }

  return true;
}

bool TrapProcessor::FileModifiedTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const long is_gmt = !PopInt(op_stack, stack_pos) ? false : true;
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    time_t raw_time = File::FileModifiedTime(name.c_str());
    if(raw_time > 0) {
      struct tm* curr_time;
      const bool got_time = GetTime(curr_time, raw_time, is_gmt);

      if(got_time) {
        frame->mem[3] = curr_time->tm_mday;          // day
        frame->mem[4] = curr_time->tm_mon + 1;       // month
        frame->mem[5] = curr_time->tm_year + 1900;   // year
        frame->mem[6] = curr_time->tm_hour;          // hours
        frame->mem[7] = curr_time->tm_min;           // mins
        frame->mem[8] = curr_time->tm_sec;           // secs
      }
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }

  return true;
}

bool TrapProcessor::FileAccessedTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const bool is_gmt = (bool)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    time_t raw_time = File::FileAccessedTime(name.c_str());
    if(raw_time > 0) {
      struct tm* curr_time;
      const bool got_time = GetTime(curr_time, raw_time, is_gmt);

      if(got_time) {
        frame->mem[3] = curr_time->tm_mday;          // day
        frame->mem[4] = curr_time->tm_mon + 1;       // month
        frame->mem[5] = curr_time->tm_year + 1900;   // year
        frame->mem[6] = curr_time->tm_hour;          // hours
        frame->mem[7] = curr_time->tm_min;           // mins
        frame->mem[8] = curr_time->tm_sec;           // secs
      }
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }

  return true;
}

bool TrapProcessor::DirSlash(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _WIN32  
  PushInt('\\', op_stack, stack_pos);
#else
  PushInt('/', op_stack, stack_pos);
#endif

  return true;
}

bool TrapProcessor::DirGetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  std::error_code error_code;
  std::filesystem::path dir_cur_path = std::filesystem::current_path(error_code);
  if(error_code) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    const std::wstring dir_cur_str = BytesToUnicode(dir_cur_path.string());
    const size_t* dir_cur_obj = CreateStringObject(dir_cur_str, program, op_stack, stack_pos);
    PushInt((size_t)dir_cur_obj, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirSetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  array = (size_t*)array[0];
  if(array) {
    const std::string to_dir_str = UnicodeToBytes((wchar_t*)(array + 3));
#ifdef _WIN32
    const int status = _chdir(to_dir_str.c_str());
#else
    const int status = chdir(to_dir_str.c_str());
#endif
    if(!status) {
      PushInt(1, op_stack, stack_pos);
    }
    else {
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirCreate([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t* &op_stack, size_t* &stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(File::MakeDir(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirExists([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t* &op_stack, size_t* &stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(File::DirExists(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirList(StackProgram* program, [[maybe_unused]] size_t* inst, size_t* &op_stack, size_t* &stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  array = (size_t*)array[0];
  if(array) {
    const std::string name =  UnicodeToBytes((wchar_t*)(array + 3));
    std::vector<std::string> files = File::ListDir(name.c_str());

    // create 'System.String' object array
    const long str_obj_array_size = (long)files.size();
    const long str_obj_array_dim = 1;
    size_t* str_obj_array = MemoryManager::AllocateArray(str_obj_array_size + str_obj_array_dim + 2,
                                                         instructions::INT_TYPE, op_stack, *stack_pos, false);
    str_obj_array[0] = str_obj_array_size;
    str_obj_array[1] = str_obj_array_dim;
    str_obj_array[2] = str_obj_array_size;
    size_t* str_obj_array_ptr = str_obj_array + 3;

    // create and assign 'System.String' instances to array
    for(size_t i = 0; i < files.size(); ++i) {
      const std::wstring wfile = BytesToUnicode(files[i]);
      str_obj_array_ptr[i] = (size_t)CreateStringObject(wfile, program, op_stack, stack_pos);
    }

    PushInt((size_t)str_obj_array, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirDelete([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t*& op_stack, size_t*& stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  array = (size_t*)array[0];
  if(array) {
    const std::string dir_name = UnicodeToBytes((wchar_t*)(array + 3));
    const auto count = std::filesystem::remove_all(dir_name);
    PushInt(count, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SymLinkCreate([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t*& op_stack, size_t*& stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* link_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* target_obj = (size_t*)PopInt(op_stack, stack_pos);

  if(target_obj && link_obj) {
    target_obj = (size_t*)target_obj[0];
    const std::string target_str = UnicodeToBytes((wchar_t*)(target_obj + 3));

    link_obj = (size_t*)link_obj[0];
    const std::string link_str = UnicodeToBytes((wchar_t*)(link_obj + 3));

    std::error_code error_code;
    if(std::filesystem::is_directory(target_str)) {
      std::filesystem::create_directory_symlink(link_str, target_str, error_code);
    }
    else {
      std::filesystem::create_symlink(link_str, target_str, error_code);
    }

    if(error_code) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SymLinkCopy([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t*& op_stack, size_t*& stack_pos, [[maybe_unused]] StackFrame* frame)
{
  [[maybe_unused]] const bool recursive = PopInt(op_stack, stack_pos);
  size_t* to = (size_t*)PopInt(op_stack, stack_pos);
  size_t* from = (size_t*)PopInt(op_stack, stack_pos);

  if(to && from) {
    to = (size_t*)to[0];
    const std::string to_str = UnicodeToBytes((wchar_t*)(to + 3));

    from = (size_t*)from[0];
    const std::string from_str = UnicodeToBytes((wchar_t*)(from + 3));

    std::error_code error_code;
    std::filesystem::copy_symlink(from_str, to_str, error_code);
    if(error_code) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SymLinkLoc(StackProgram* program, [[maybe_unused]] size_t* inst, size_t*& op_stack, size_t*& stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  array = (size_t*)array[0];
  if(array) {
    const std::string link_str = UnicodeToBytes((wchar_t*)(array + 3));

    std::error_code error_code;
    const auto target_path = std::filesystem::read_symlink(link_str, error_code);
    if(error_code) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt((size_t)CreateStringObject(BytesToUnicode(target_path.string()), program, op_stack, stack_pos), op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SymLinkExists([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t*& op_stack, size_t*& stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const std::string path_str = UnicodeToBytes((wchar_t*)(array + 3));
    PushInt(std::filesystem::is_symlink(path_str), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

//
// TODO: implement
//
bool TrapProcessor::HardLinkCreate([[maybe_unused]] StackProgram* program, [[maybe_unused]] size_t* inst, size_t*& op_stack, size_t*& stack_pos, [[maybe_unused]] StackFrame* frame)
{
  size_t* link_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* target_obj = (size_t*)PopInt(op_stack, stack_pos);

  if(target_obj && link_obj) {
    target_obj = (size_t*)target_obj[0];
    const std::string target_str = UnicodeToBytes((wchar_t*)(target_obj + 3));

    link_obj = (size_t*)link_obj[0];
    const std::string link_str = UnicodeToBytes((wchar_t*)(link_obj + 3));

    std::error_code error_code;
    std::filesystem::create_hard_link(link_str, target_str, error_code);

    if(error_code) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt(1, op_stack, stack_pos);
    }
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

void TrapProcessor::WriteSerializedBytes(const char* array, const long src_buffer_size, size_t* inst, size_t*& op_stack, size_t*& stack_pos)
{
  size_t* dest_buffer = (size_t*)inst[0];
  if(array && dest_buffer) {
    const long dest_pos = (long)inst[1];

    // expand buffer, if needed
    dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
    inst[0] = (size_t)dest_buffer;

    // copy content
    char* dest_buffer_ptr = (char*)(dest_buffer + 3);
    memcpy(dest_buffer_ptr + dest_pos, array, src_buffer_size);
    inst[1] = dest_pos + src_buffer_size;
  }
}

void TrapProcessor::SerializeArray(const size_t* array, ParamType type, size_t* inst, size_t*& op_stack, size_t*& stack_pos)
{
  if(array) {
    SerializeByte(1, inst, op_stack, stack_pos);
    const long array_size = (long)array[0];

    // write values
    switch(type) {
    case BYTE_ARY_PARM: {
      // write metadata
      char* array_ptr = (char*)(array + 3);
      SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);
      // write data
      WriteSerializedBytes(array_ptr, array_size, inst, op_stack, stack_pos);
    }
      break;

    case CHAR_ARY_PARM: {
      // convert
      wchar_t* array_ptr = (wchar_t*)(array + 3);
      const std::string buffer = UnicodeToBytes(array_ptr);
      // write metadata  
      SerializeInt((INT_VALUE)buffer.size(), inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)buffer.size(), inst, op_stack, stack_pos);
      // write data
      WriteSerializedBytes((const char*)buffer.c_str(), (long)buffer.size(), inst, op_stack, stack_pos);
    }
      break;

    case INT_ARY_PARM: {
      // write metadata
      char* array_ptr = (char*)(array + 3);
      SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);
      // write data. Int array elements are stored as size_t (8 bytes), not
      // INT_VALUE (int32) — using sizeof(INT_VALUE) here serialized only the
      // first half of the elements (ReadSerializedBytes must match).
      WriteSerializedBytes(array_ptr, array_size * sizeof(size_t), inst, op_stack, stack_pos);
    }
      break;

    case OBJ_ARY_PARM: {
      SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);

      size_t* array_ptr = (size_t*)(array + 3);
      for(int i = 0; i < array_size; ++i) {
        size_t* obj = (size_t*)array_ptr[i];
        ObjectSerializer serializer(obj);
        std::vector<char> src_buffer = serializer.GetValues();
        const long src_buffer_size = (long)src_buffer.size();
        size_t* dest_buffer = (size_t*)inst[0];
        long dest_pos = (long)inst[1];

        // expand buffer, if needed
        dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
        inst[0] = (size_t)dest_buffer;

        // copy content
        char* dest_buffer_ptr = ((char*)(dest_buffer + 3) + dest_pos);
        for(int j = 0; j < src_buffer_size; ++j, dest_pos++) {
          dest_buffer_ptr[j] = src_buffer[j];
        }
        inst[1] = dest_pos;
      }
    }
      break;

    case FLOAT_ARY_PARM: {
      // write metadata
      char* array_ptr = (char*)(array + 3);
      SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
      SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);
      // write data
      WriteSerializedBytes(array_ptr, array_size * sizeof(FLOAT_VALUE), inst, op_stack, stack_pos);
    }
        break;

    default:
      break;
    }
  }
  else {
    SerializeByte(0, inst, op_stack, stack_pos);
  }
}

void TrapProcessor::ReadSerializedBytes(size_t* dest_array, const size_t* src_array, ParamType type, size_t* inst)
{
  if(dest_array && src_array) {
    const long dest_pos = (long)inst[1];
    const long src_array_size = (long)src_array[0];
    long dest_array_size = (long)dest_array[0];

    // Source bytes consumed per logical element: 8 for int/float arrays, 1 for
    // byte/char. dest_array_size is attacker-controlled; previously only the
    // start offset was checked, allowing a large over-read past the source
    // buffer into a program-visible array (memory disclosure).
    const long elem = (type == INT_ARY_PARM) ? (long)sizeof(size_t)
                    : (type == FLOAT_ARY_PARM) ? (long)sizeof(FLOAT_VALUE) : 1;
    if(dest_pos >= 0 && dest_array_size >= 0 && dest_pos <= src_array_size &&
       dest_array_size <= (src_array_size - dest_pos) / elem) {
      const char* src_array_ptr = (char*)(src_array + 3);
      char* dest_array_ptr = (char*)(dest_array + 3);

      switch(type) {
      case BYTE_ARY_PARM:
        memcpy(dest_array_ptr, src_array_ptr + dest_pos, dest_array_size);
        break;

      case CHAR_ARY_PARM: {
        // convert
        const std::string in((const char*)src_array_ptr + dest_pos, dest_array_size);
        const std::wstring out = BytesToUnicode(in);
        // copy
        dest_array[0] = out.size();
        dest_array[2] = out.size();
        // NOTE: do NOT scale dest_array_size by sizeof(wchar_t) here. The source
        // consumed dest_array_size *bytes* (UnicodeToBytes output), and the shared
        // inst[1] advance below uses dest_array_size as that source byte count.
        // Scaling it over-advanced the read position and desynced every following
        // read (e.g. a String/Char[] followed by more serialized values).
        memcpy(dest_array_ptr, out.c_str(), out.size() * sizeof(wchar_t));
      }
         break;

      case INT_ARY_PARM:
        // Int elements are stored as size_t (8 bytes), not INT_VALUE (int32);
        // must match the serialize side in SerializeArray.
        dest_array_size *= sizeof(size_t);
        memcpy(dest_array_ptr, src_array_ptr + dest_pos, dest_array_size);
        break;

      case FLOAT_ARY_PARM:
        dest_array_size *= sizeof(FLOAT_VALUE);
        memcpy(dest_array_ptr, src_array_ptr + dest_pos, dest_array_size);
        break;

      default:
        break;
      }

      inst[1] = dest_pos + dest_array_size;
    }
  }
}

/********************************
 * Routines to format method 
 * signatures
 ********************************/
std::wstring MethodFormatter::Format(const std::wstring method_sig)
{
  std::wstring mthd_sig;

  size_t start = method_sig.rfind(':');
  if(start != std::wstring::npos) {
    std::wstring parameters = method_sig.substr(start + 1);
    mthd_sig = FormatParameters(parameters);
  }

  size_t mid = method_sig.rfind(':', start - 1);
  if(mid != std::wstring::npos) {
    const std::wstring mthd_name = method_sig.substr(mid + 1, start - mid - 1);
    const std::wstring cls_name = method_sig.substr(0, mid);
    return cls_name + L"->" + mthd_name + mthd_sig;
  }

  return L"<unknown>";
}

std::wstring MethodFormatter::FormatParameters(const std::wstring param_str)
{
  wchar_t param_name = L'a';
  std::wstring formatted_str = L"(";
  size_t index = 0;

  while(index < param_str.size() && param_name != L'{') {
    switch(param_str[index]) {
    case 'l':
      formatted_str += param_name++;
      formatted_str += L':';
      formatted_str += L"Boolean";
      index++;
      break;

    case 'b':
      formatted_str += param_name++;
      formatted_str += L':';
      formatted_str += L"Byte";
      index++;
      break;

    case 'i':
      formatted_str += param_name++;
      formatted_str += L':';
      formatted_str += L"Int";
      index++;
      break;

    case 'f':
      formatted_str += param_name++;
      formatted_str += L':';
      formatted_str += L"Float";
      index++;
      break;

    case 'c':
      formatted_str += param_name++;
      formatted_str += L':';
      formatted_str += L"Char";
      index++;
      break;

    case 'n':
      formatted_str += param_name++;
      formatted_str += L':';
      formatted_str += L"Nil";
      index++;
      break;

    case 'm': {
      formatted_str += param_name++;
      formatted_str += L':';

      size_t start = index;

      const std::wstring prefix = L"m.(";
      int nested_count = 1;
      size_t found = param_str.find(prefix);
      while(found != std::wstring::npos) {
        nested_count++;
        found = param_str.find(prefix, found + prefix.size());
      }

      while(nested_count--) {
        while(index < param_str.size() && param_str[index] != L'~') {
          index++;
        }
        if(param_str[index] == L'~') {
          index++;
        }
      }

      while(index < param_str.size() && param_str[index] != L',') {
        index++;
      }

      const std::wstring name = param_str.substr(start, index - start - 1);
      formatted_str += FormatFunctionalType(name);
    }
            break;

    case 'o': {
      formatted_str += param_name++;
      formatted_str += L':';

      index += 2;
      size_t start = index;
      while(index < param_str.size() && param_str[index] != L'*' && param_str[index] != L',' && param_str[index] != L'|') {
        index++;
      }
      size_t end = index;
      const std::wstring cls_name = param_str.substr(start, end - start);
      formatted_str += cls_name;
    }
            break;
    }

    // set generics
    if(index < param_str.size() && param_str[index] == L'|') {
      formatted_str += L"<";
      do {
        index++;
        size_t start = index;
        while(index < param_str.size() && param_str[index] != L'*' && param_str[index] != L',' && param_str[index] != L'|') {
          index++;
        }
        size_t end = index;

        const std::wstring generic_name = param_str.substr(start, end - start);
        formatted_str += generic_name;
      }       while(index < param_str.size() && param_str[index] == L'|');
      formatted_str += L">";
    }

    // set dimension
    int dimension = 0;
    if(index < param_str.size() && param_str[index] == L'*') {
      formatted_str += L"[";
      while(index < param_str.size() && param_str[index] == L'*') {
        dimension++;
        index++;

        if(index + 1 < param_str.size()) {
          formatted_str += L",";
        }
      }
      formatted_str += L"]";
    }

#ifdef _DEBUG
    assert(index >= param_str.size() || param_str[index] == L',');
#endif

    index++;
    if(index < param_str.size()) {
      formatted_str += L", ";
    }
  }
  formatted_str += L")";

  return formatted_str;
}

std::wstring MethodFormatter::FormatType(const std::wstring type_str)
{
  std::wstring formatted_str;

  size_t index = 0;
  switch(type_str[index]) {
  case L'l':
    formatted_str += L"Boolean";
    index++;
    break;

  case L'b':
    formatted_str += L"Byte";
    index++;
    break;

  case L'i':
    formatted_str += L"Int";
    index++;
    break;

  case L'f':
    formatted_str += L"Float";
    index++;
    break;

  case L'c':
    formatted_str += L"Char";
    index++;
    break;

  case L'n':
    formatted_str += L"Nil";
    index++;
    break;

  case L'm': {
    size_t start = index;

    int nested_count = 1;
    const std::wstring prefix = L"m.(";
    size_t found = type_str.find(prefix);
    while(found != std::wstring::npos) {
      nested_count++;
      found = type_str.find(prefix, found + prefix.size());
    }

    while(nested_count--) {
      while(index < type_str.size() && type_str[index] != L'~') {
        index++;
      }
      if(type_str[index] == L'~') {
        index++;
      }
    }

    while(index < type_str.size() && type_str[index] != L',') {
      index++;
    }

    const std::wstring name = type_str.substr(start, index - start - 1);
    formatted_str += FormatFunctionalType(name);
  }
    break;

  case L'o':
    index = 2;
    while(index < type_str.size() && type_str[index] != L'*' && type_str[index] != L'|') {
      index++;
    }
    const std::wstring cls_name = type_str.substr(2, index - 2);
    formatted_str += cls_name;
    break;
  }

  // set generics
  if(index < type_str.size() && type_str[index] == L'|') {
    formatted_str += L"<";
    do {
      index++;
      size_t start = index;
      while(index < type_str.size() && type_str[index] != L'*' && type_str[index] != L',' && type_str[index] != L'|') {
        index++;
      }
      size_t end = index;

      const std::wstring generic_name = type_str.substr(start, end - start);
      formatted_str += generic_name;
    } 
    while(index < type_str.size() && type_str[index] == L'|');
    formatted_str += L">";
  }

  // set dimension
  int dimension = 0;
  if(index < type_str.size() && type_str[index] == L'*') {
    formatted_str += L"[";
    while(index < type_str.size() && type_str[index] == L'*') {
      dimension++;
      index++;

      if(index + 1 < type_str.size()) {
        formatted_str += L",";
      }
    }
    formatted_str += L"]";
  }

  return formatted_str;
}

std::wstring MethodFormatter::FormatFunctionalType(const std::wstring func_str)
{
  std::wstring formatted_str;

  // parse parameters
  size_t start = func_str.rfind(L'(');
  size_t middle = func_str.find(L')');

  if(start != std::wstring::npos && middle != std::wstring::npos) {
    start++;
    const std::wstring params_str = func_str.substr(start, middle - start);
    formatted_str += FormatParameters(params_str);

    // parse return
    size_t end = func_str.find(L',', middle);
    if(end == std::wstring::npos) {
      end = func_str.size();
    }
    middle += 2;

    formatted_str += L"~";
    const std::wstring rtrn_str = func_str.substr(middle, end - middle);
    formatted_str += FormatType(rtrn_str);
  }

  return formatted_str;
}

bool EndsWith(std::wstring const& str, std::wstring const& ending)
{
  if(str.length() >= ending.length()) {
    return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
  }

  return false;
}
