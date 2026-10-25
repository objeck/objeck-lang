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

// --- HTTP header field validation -------------------------------------------
//
// Caller-supplied header content must never be able to add a header line of
// its own. On Windows this is a live request-splitting vector: WinHTTP's
// AddRequestHeaders takes a CRLF-SEPARATED BLOCK, so a CR/LF inside a value is
// the API's own separator, not a parse error. On the ngtcp2 path the framing
// is length-prefixed so it does not split there, but nghttp3 performs no
// validation on submit (the check_header_* helpers exist precisely because it
// is the caller's job), and an h3->h1.1 gateway downstream will happily turn a
// CR/LF-bearing value into a split at the origin.
//
// Octets per RFC 9110 5.1/5.5 as tightened by RFC 9113 8.2.1 (adopted by
// RFC 9114 4.3). Validated on the UTF-8 bytes, since that is what goes on the
// wire.
static bool h3_valid_header_value(const std::string& v)
{
  for(size_t i = 0; i < v.size(); ++i) {
    const unsigned char c = (unsigned char)v[i];
    // reject CR, LF, NUL, DEL and every other C0 control; SP and HTAB are ok
    if(c == 0x7F || (c < 0x20 && c != 0x09)) {
      return false;
    }
  }
  // leading/trailing whitespace makes the field malformed
  if(!v.empty()) {
    const unsigned char f = (unsigned char)v.front(), b = (unsigned char)v.back();
    if(f == 0x20 || f == 0x09 || b == 0x20 || b == 0x09) {
      return false;
    }
  }
  return true;
}

static bool h3_valid_header_name(const std::string& n)
{
  if(n.empty()) {
    return false;
  }
  // A caller must never supply a pseudo-header: both backends build :method,
  // :path, :scheme and :authority themselves, and a duplicate is malformed
  // (RFC 9114 4.3.1) -- the classic smuggling / cache-poisoning primitive.
  if(n[0] == ':') {
    return false;
  }
  for(size_t i = 0; i < n.size(); ++i) {
    const unsigned char c = (unsigned char)n[i];
    const bool tchar = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                       c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
                       c == 0x27 /* apostrophe */ || c == '*' || c == '+' ||
                       c == '-' || c == '.' ||
                       c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
    // uppercase is rejected rather than folded: HTTP/2/3 require lowercase and
    // nghttp3 does not lower-case for us
    if(!tchar) {
      return false;
    }
  }
  return true;
}

// Which HTTP/3 backend owns instance[0]. Derived ONCE so Connect, Request and
// Close cannot disagree -- picking the wrong type in Close is a wrong-type
// delete (heap corruption), not a compile error.
#if defined(OBJECK_HAS_NGTCP2)
#  define OBJECK_H3_NGTCP2 1
#elif defined(OBJECK_HAS_WINHTTP_H3)
#  define OBJECK_H3_WINHTTP 1
#  include "win_http3.h"
#endif
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
#include <mutex>
#include <condition_variable>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#elif !defined(_WIN32)
#include <unistd.h>
#include <cstdio>
#include <sys/resource.h>
#endif

// FILE_CREATE_NEW (open with O_EXCL) and PROCESS_ALIVE (kill with signal 0)
#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
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
#include <cmath>
#include <filesystem>
#include <charconv>
#include <version>

// a TLS or DTLS failure as text (defined with the DTLS traps)
static std::wstring TlsErrorMessage(int error, uint32_t verify_flags, const SocketError& reason);

// floating-point std::to_chars (shortest round trip): MSVC 19.24+, libstdc++
// 11+. libc++ has it from 14 but announces the macro only much later, so
// Apple builds take the snprintf fallback in ShortestDigits, which gives the
// same digits.
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define OBJECK_HAS_FLOAT_TO_CHARS
#endif

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
  // the W API: the A one answers in the ANSI code page, which was then decoded as UTF-8
  wchar_t install_path[MAX_PATH];
  DWORD status = GetModuleFileNameW(nullptr, install_path, MAX_PATH);
  if(status > 0 && status < MAX_PATH) {
    std::wstring exe_path(install_path);
    size_t install_index = exe_path.find_last_of(L'\\');
    if(install_index != std::wstring::npos) {
      exe_path = exe_path.substr(0, install_index);
      install_index = exe_path.find_last_of(L'\\');
      if(install_index != std::wstring::npos) {
        std::wstring install_dir = exe_path.substr(0, install_index);
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

  wchar_t tmp_dir[MAX_PATH + 1];
  if(GetTempPathW(MAX_PATH + 1, tmp_dir)) {
    std::wstring tmp_dir_value(tmp_dir);
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

/********************************
 * Skips one encoded function type
 * "m.(<params>)~<return>" starting at
 * its 'm'; returns the index of the
 * ',' (or end) that follows it.
 *
 * Scanning to the first '~' and then
 * the next ',' stopped inside a nested
 * function type: for a parameter of
 * type ((Int) ~ Int, Int) ~ Int, i.e.
 * "m.(m.(i,)~i,i,)~i,", the parser
 * resumed at ")~i," and rejected the
 * ')' as "Invalid method signature!",
 * so such a method never loaded.
 ********************************/
static size_t SkipFunctionType(const std::wstring& str, size_t index)
{
  bool more = true;
  while(more && index < str.size()) {
    // parameter list: to the ')' matching the first '('
    while(index < str.size() && str[index] != L'(') {
      index++;
    }
    int nesting = 0;
    while(index < str.size()) {
      if(str[index] == L'(') {
        nesting++;
      }
      else if(str[index] == L')' && --nesting == 0) {
        index++;
        break;
      }
      index++;
    }

    // return type, itself possibly a function type
    if(index < str.size() && str[index] == L'~') {
      index++;
    }
    more = index < str.size() && str[index] == L'm';
  }

  while(index < str.size() && str[index] != L',') {
    index++;
  }

  return index;
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
        index = SkipFunctionType(params_name, index);
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
            SerializeArrayHeader(array);
            char* array_ptr = (char*)(array + 2 + array[1]);

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
            // a multi-dimensional array is no string: write its dimensions and
            // then each character (#1174)
            if(array[1] > 1) {
              SerializeArrayHeader(array);
              const wchar_t* array_ptr = (wchar_t*)(array + 2 + array[1]);
              for(size_t j = 0; j < array[0]; ++j) {
                SerializeChar(array_ptr[j]);
              }
              mem++;
              break;
            }

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
            SerializeArrayHeader(array);
            size_t* array_ptr = array + 2 + array[1];

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
            SerializeArrayHeader(array);
            FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 2 + array[1]);

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
            SerializeArrayHeader(array);
            size_t* array_ptr = array + 2 + array[1];

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
    VmExit(1);
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
            size_t dims[8];
            if(!DeserializeDimensions(byte_array_size, byte_array_dim, byte_array_size_dim, dims) || !CanRead(byte_array_size)) {
              return nullptr;
            }
            size_t* byte_array = MemoryManager::AllocateArray((size_t)(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t))),
                                                              BYTE_ARY_TYPE, op_stack, *stack_pos, false);
            byte_array[0] = byte_array_size;
            byte_array[1] = byte_array_dim;
            memcpy(byte_array + 2, dims, byte_array_dim * sizeof(size_t));
            char* byte_array_ptr = (char*)(byte_array + 2 + byte_array_dim);
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
            // a multi-dimensional array carries every dimension, then each
            // character (#1174)
            if(char_array_dim > 1) {
              size_t dims[8];
              if(!DeserializeDimensions(char_array_size, char_array_dim, char_array_size_dim, dims)) {
                return nullptr;
              }
              size_t* char_array = MemoryManager::AllocateArray(char_array_size + 1 +
                ((char_array_dim + 2) * sizeof(size_t)), CHAR_ARY_TYPE, op_stack, *stack_pos, false);
              char_array[0] = char_array_size;
              char_array[1] = char_array_dim;
              memcpy(char_array + 2, dims, char_array_dim * sizeof(size_t));
              wchar_t* char_array_ptr = (wchar_t*)(char_array + 2 + char_array_dim);
              for(INT_VALUE j = 0; j < char_array_size; ++j) {
                char_array_ptr[j] = DeserializeChar();
                if(read_error) {
                  return nullptr;
                }
              }
              mem_cache[-mem_id] = char_array;
              instance[instance_pos++] = (size_t)char_array;
              break;
            }
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
            size_t dims[8];
            if(!DeserializeDimensions(array_size, array_dim, array_size_dim, dims)) {
              return nullptr;
            }
            size_t* array = MemoryManager::AllocateArray(array_size + array_dim + 2, instructions::INT_TYPE,
                                                         op_stack, *stack_pos, false);
            array[0] = array_size;
            array[1] = array_dim;
            memcpy(array + 2, dims, array_dim * sizeof(size_t));
            size_t* array_ptr = array + 2 + array_dim;
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
            size_t dims[8];
            if(!DeserializeDimensions(array_size, array_dim, array_size_dim, dims) ||
               !CanRead((INT64_VALUE)array_size * (INT64_VALUE)sizeof(FLOAT_VALUE))) {
              return nullptr;
            }
            size_t* array = MemoryManager::AllocateArray(array_size * 2 + array_dim + 2, instructions::INT_TYPE,
                                                         op_stack, *stack_pos, false);

            array[0] = array_size;
            array[1] = array_dim;
            memcpy(array + 2, dims, array_dim * sizeof(size_t));
            FLOAT_VALUE* array_ptr = (FLOAT_VALUE*)(array + 2 + array_dim);
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
            size_t dims[8];
            if(!DeserializeDimensions(array_size, array_dim, array_size_dim, dims)) {
              return nullptr;
            }
            size_t* array = MemoryManager::AllocateArray((size_t)(array_size + array_dim + 2), instructions::INT_TYPE,
                                                         op_stack, *stack_pos, false);
            array[0] = array_size;
            array[1] = array_dim;
            memcpy(array + 2, dims, array_dim * sizeof(size_t));
            size_t* array_ptr = array + 2 + array_dim;

            // copy content
            for(int i = 0; i < array_size; i++) {
              if(!DeserializeByte()) {
                // a Nil element belongs to the array; writing it through
                // instance_pos shifted every later field of the enclosing object
                array_ptr[i] = 0;
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
      VmExit(1);
    }
  }
  else {
    std::wcerr << L">>> DLL call: Unable to locate class; id=" << cls_id << L" <<<" << std::endl;
    VmExit(1);
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
      VmExit(1);
    }
  }
  else {
    std::wcerr << L">>> Unable to locate class; name='" << cls_id << L"' <<<" << std::endl;
    VmExit(1);
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
      VmExit(1);
    }
  }
  else {
    std::wcerr << L">>> DLL call: Unable to locate class; id=" << cls_id << L" <<<" << std::endl;
    VmExit(1);
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
    // no such class: Class->Instance(name) is Nil, as it is for a Nil name.
    // This aborted the VM ("Unable to load class"), so a program could not
    // ask whether a class exists (#1175).
    PushInt(0, op_stack, stack_pos);
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
    VmExit(1);
  }

  const std::wstring &semi_qual_mthd_string = qual_mthd_name.substr(semi_qual_mthd_index + 1);
  const size_t mthd_index = semi_qual_mthd_string.find(':');
  if(mthd_index == std::wstring::npos) {
    std::wcerr << L">>> Internal error: invalid method name <<<" << std::endl;
    VmExit(1);
  }
  const std::wstring &mthd_string = semi_qual_mthd_string.substr(0, mthd_index);
  mthd_obj[2] = (size_t)CreateStringObject(mthd_string, program, op_stack, stack_pos);

  // One encoded type, from 'index' up to the next ',' or the end: a DataType
  // with the DataType->TypeId code, the class name and the dimension (#1175).
  // The codes were -1000..-994, so `p->GetType() = DataType->TypeId->INT` was
  // never true; the class name was cut with substr(start, index - 2), which
  // passes a position as a length, so a later object parameter carried the
  // rest of the signature; and an object array kept its '*'s in the name,
  // with dimension 0. Returns Nil for a Nil return ('n').
  auto parse_data_type = [&](const std::wstring& sig, size_t& index) -> size_t* {
    if(index >= sig.size() || sig[index] == L'n') {
      index = sig.size();
      return (size_t*)nullptr;
    }

    size_t* data_type_obj = MemoryManager::AllocateObject(program->GetDataTypeObjectId(), op_stack, *stack_pos, false);
    switch(sig[index]) {
    case L'l':
      data_type_obj[0] = (size_t)-60;
      index++;
      break;

    case L'b':
      data_type_obj[0] = (size_t)-59;
      index++;
      break;

    case L'c':
      data_type_obj[0] = (size_t)-58;
      index++;
      break;

    case L'i':
      data_type_obj[0] = (size_t)-57;
      index++;
      break;

    case L'f':
      data_type_obj[0] = (size_t)-56;
      index++;
      break;

    case L'o': {
      data_type_obj[0] = (size_t)-55;
      // 'o.' then the class name, which ends at a '*', a ',' or the end
      index += 2;
      const size_t name_start = index;
      while(index < sig.size() && sig[index] != L',' && sig[index] != L'*') {
        index++;
      }
      data_type_obj[1] = (size_t)CreateStringObject(sig.substr(name_start, index - name_start), program, op_stack, stack_pos);
    }
      break;

    case L'm':
      data_type_obj[0] = (size_t)-54;
      index = SkipFunctionType(sig, index);
      break;

    default:
      throw std::runtime_error("Invalid method signature!");
    }

    // array dimension
    int dimension = 0;
    while(index < sig.size() && sig[index] == L'*') {
      dimension++;
      index++;
    }
    data_type_obj[2] = dimension;

    return data_type_obj;
  };

  // parse parameter string
  size_t index = 0;
  const std::wstring &params_string = semi_qual_mthd_string.substr(mthd_index + 1);
  std::vector<size_t*> data_type_obj_holder;
  while(index < params_string.size()) {
    size_t* data_type_obj = parse_data_type(params_string, index);
    if(data_type_obj) {
      data_type_obj_holder.push_back(data_type_obj);
    }

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
  // Write barrier: an old array (AllocateArray never uses the nursery) filled
  // with young objects. See the note in DirList.
  MemoryManager::WriteBarrier(type_obj_array);
  // set type array
  mthd_obj[3] = (size_t)type_obj_array;

  // return type: Nil for a method that returns nothing
  size_t rtrn_index = 0;
  mthd_obj[4] = (size_t)parse_data_type(mthd->GetReturnName(), rtrn_index);

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
  MemoryManager::WriteBarrier(mthd_obj_array);
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
  char_array[0] = char_array_size;
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
 *
 * A GMT date is calendar arithmetic in UTC, never mktime/gmtime. mktime reads
 * its fields as LOCAL time and applies the machine's DST rules, so a GMT date
 * built in July on a DST-observing machine came back an hour off, and its Unix
 * time was off by the zone offset (#1179); on Windows it also rejects years
 * before 1970 or after 3000, and gmtime_s rejects a negative Unix time. The
 * helpers below are the proleptic Gregorian calendar (H. Hinnant's
 * days_from_civil), exact for any year.
 ********************************/
static INT64_VALUE FloorDiv(INT64_VALUE a, INT64_VALUE b)
{
  INT64_VALUE q = a / b;
  if((a % b != 0) && ((a < 0) != (b < 0))) {
    --q;
  }
  return q;
}

static INT64_VALUE DaysFromCivil(INT64_VALUE y, INT64_VALUE m, INT64_VALUE d)
{
  y -= m <= 2 ? 1 : 0;
  const INT64_VALUE era = FloorDiv(y, 400);
  const INT64_VALUE yoe = y - era * 400;
  const INT64_VALUE doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const INT64_VALUE doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void CivilFromDays(INT64_VALUE z, INT64_VALUE& y, INT64_VALUE& m, INT64_VALUE& d)
{
  z += 719468;
  const INT64_VALUE era = FloorDiv(z, 146097);
  const INT64_VALUE doe = z - era * 146097;
  const INT64_VALUE yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const INT64_VALUE doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const INT64_VALUE mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = yoe + era * 400 + (m <= 2 ? 1 : 0);
}

// Seconds since 1970-01-01T00:00:00Z. Out-of-range fields roll over the way
// mktime's do: month 13 is January of the next year, day 32 the next month.
static INT64_VALUE UtcSeconds(INT64_VALUE year, INT64_VALUE month, INT64_VALUE day,
                              INT64_VALUE hours, INT64_VALUE mins, INT64_VALUE secs)
{
  const INT64_VALUE y = year + FloorDiv(month - 1, 12);
  const INT64_VALUE m = (month - 1) - FloorDiv(month - 1, 12) * 12 + 1;
  return (DaysFromCivil(y, m, 1) + day - 1) * 86400 + hours * 3600 + mins * 60 + secs;
}

// Day of the week, 0 = Sunday, for days since 1970-01-01 (a Thursday)
static INT64_VALUE WeekdayFromDays(INT64_VALUE days)
{
  const INT64_VALUE w = (days + 4) % 7;
  return w < 0 ? w + 7 : w;
}

// Fills a Date instance from UTC seconds since the epoch
static void SetUtcInstance(size_t* instance, INT64_VALUE seconds)
{
  const INT64_VALUE days = FloorDiv(seconds, 86400);
  const INT64_VALUE rem = seconds - days * 86400;
  INT64_VALUE y, m, d;
  CivilFromDays(days, y, m, d);
  instance[0] = (size_t)d;                    // day
  instance[1] = (size_t)m;                    // month
  instance[2] = (size_t)y;                    // year
  instance[3] = (size_t)(rem / 3600);         // hours
  instance[4] = (size_t)((rem % 3600) / 60);  // mins
  instance[5] = (size_t)(rem % 60);           // secs
  instance[6] = 0;                            // no savings time in UTC
  instance[7] = (size_t)WeekdayFromDays(days);// day of week
  instance[8] = true;                         // is GMT
}

static INT64_VALUE UtcSecondsOf(size_t* instance)
{
  return UtcSeconds((INT64_VALUE)instance[2], (INT64_VALUE)instance[1], (INT64_VALUE)instance[0],
                    (INT64_VALUE)instance[3], (INT64_VALUE)instance[4], (INT64_VALUE)instance[5]);
}

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
    // A date's day of the week does not depend on the zone, so the calendar
    // gives it for both; this never set it, and every such Date reported a
    // Sunday (#1179).
    const INT64_VALUE days = FloorDiv(UtcSeconds(year, month, day, 0, 0, 0), 86400);
    INT64_VALUE y, m, d;
    CivilFromDays(days, y, m, d);

    // set instance values
    instance[0] = (size_t)d;                  // day
    instance[1] = (size_t)m;                  // month
    instance[2] = (size_t)y;                  // year
    instance[3] = 0;                          // hours
    instance[4] = 0;                          // mins
    instance[5] = 0;                          // secs
    instance[7] = (size_t)WeekdayFromDays(days); // day of week
    instance[8] = is_gmt;                     // is GMT

    instance[6] = 0;                          // savings time
    if(!is_gmt) {
      struct tm curr_time;
      memset(&curr_time, 0, sizeof(struct tm));
      curr_time.tm_mday = static_cast<int>(d);
      curr_time.tm_mon = static_cast<int>(m - 1);
      curr_time.tm_year = static_cast<int>(y - 1900);
      curr_time.tm_isdst = -1;
      if(mktime(&curr_time) != (time_t)-1) {
        instance[6] = curr_time.tm_isdst > 0 ? 1 : 0;
      }
    }
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
    // UTC by the calendar: through mktime a GMT 12:00 in July came back 13:00
    // on a DST-observing machine, and Clone() (which builds through here) was
    // not equal to its original (#1179)
    if(is_gmt) {
      SetUtcInstance(instance, UtcSeconds(year, month, day, hours, mins, secs));
      return;
    }

    struct tm curr_time;
    memset(&curr_time, 0, sizeof(struct tm));

    // update time; the C library decides whether DST is in effect
    curr_time.tm_year = static_cast<int>(year - 1900);
    curr_time.tm_mon = static_cast<int>(month - 1);
    curr_time.tm_mday = static_cast<int>(day);
    curr_time.tm_hour = static_cast<int>(hours);
    curr_time.tm_min = static_cast<int>(mins);
    curr_time.tm_sec = static_cast<int>(secs);
    curr_time.tm_isdst = -1;
    if(mktime(&curr_time) == (time_t)-1) {
      // out of the C library's range (Windows: before 1970 or after 3000):
      // keep the calendar date and weekday rather than garbage
      SetUtcInstance(instance, UtcSeconds(year, month, day, hours, mins, secs));
      instance[8] = false;
      return;
    }

    // set instance values
    instance[6] = curr_time.tm_isdst > 0 ? 1 : 0; // savings time
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

    // a GMT date moves by the calendar, with no zone and no DST
    const bool is_gmt = instance[8];
    if(is_gmt) {
      SetUtcInstance(instance, UtcSecondsOf(instance) + offset);
      return;
    }

    // create time structure
    struct tm set_time;
    memset(&set_time, 0, sizeof(struct tm));
    set_time.tm_mday = (int)instance[0];          // day
    set_time.tm_mon = (int)instance[1] - 1;       // month
    set_time.tm_year = (int)instance[2] - 1900;   // year
    set_time.tm_hour = (int)instance[3];          // hours
    set_time.tm_min = (int)instance[4];           // mins
    set_time.tm_sec = (int)instance[5];           // secs
    set_time.tm_isdst = (int)instance[6];         // savings time

    // calculate difference
    time_t raw_time = mktime(&set_time);
    if(raw_time == (time_t)-1) {
      // out of the C library's range: move by the calendar instead
      SetUtcInstance(instance, UtcSecondsOf(instance) + offset);
      instance[8] = false;
      return;
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
void TrapProcessor::ProcessFileOwner(const wchar_t* name, bool is_account,
                                     StackProgram* program, size_t* &op_stack, size_t* &stack_pos) {
  const std::wstring value_str = File::FileOwner(ToNativePath(name).c_str(), is_account);
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
        // Write barrier, as the object-array case above already does: an old
        // destination array holding freshly deserialized young objects.
        MemoryManager::WriteBarrier(dest_array);
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
    byte_array[0] = byte_array_size;
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
// A failing trap's error (#1247): recorded for a ?-> handler on this thread,
// which then recovers from it, or printed when nothing guards. Returns false,
// the trap's failure.
static bool TrapError(long kind, const std::wstring& message)
{
  if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
    Runtime::StackInterpreter::RecordRecoveredError(kind, message);
    Runtime::StackInterpreter::TrapErrorRecorded() = true;
  }
  else {
    std::wcerr << L">>> " << message << L" <<<" << std::endl;
  }
  return false;
}

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
  case HTTP2_REQUEST_HDRS:
    return Http2RequestHdrs(program, inst, op_stack, stack_pos, frame);

  case HTTP2_CLOSE:
    return Http2Close(program, inst, op_stack, stack_pos, frame);

  case HTTP3_CONNECT:
    return Http3Connect(program, inst, op_stack, stack_pos, frame);

  case HTTP3_REQUEST:
    return Http3Request(program, inst, op_stack, stack_pos, frame);
  case HTTP3_REQUEST_HDRS:
    return Http3RequestHdrs(program, inst, op_stack, stack_pos, frame);

  case FLOAT_TO_STR_PREC:
    return FloatToStrPrec(program, inst, op_stack, stack_pos, frame);

  case FILE_IN_LINE:
    return FileInLine(program, inst, op_stack, stack_pos, frame);

  case COND_NEW:
    return CondNew(program, inst, op_stack, stack_pos, frame);

  case COND_WAIT:
    return CondWait(program, inst, op_stack, stack_pos, frame);

  case COND_SIGNAL:
    return CondSignal(program, inst, op_stack, stack_pos, frame);

  case RUNTIME_GET_ERROR:
    return RuntimeGetError(program, inst, op_stack, stack_pos, frame);

  case RUNTIME_GET_ERROR_KIND:
    return RuntimeGetErrorKind(program, inst, op_stack, stack_pos, frame);

  case RUNTIME_CLEAR_ERROR:
    return RuntimeClearError(program, inst, op_stack, stack_pos, frame);

  case RUNTIME_RAISE:
    return RuntimeRaise(program, inst, op_stack, stack_pos, frame);

  case RUNTIME_GET_LAST_ERROR:
    return RuntimeGetLastError(program, inst, op_stack, stack_pos, frame);

  case ARY_COPY:
    return AryCopy(program, inst, op_stack, stack_pos, frame);

  case ARY_FILL_INT:
    return AryFillInt(program, inst, op_stack, stack_pos, frame);

  case ARY_FILL_FLOAT:
    return AryFillFloat(program, inst, op_stack, stack_pos, frame);

  case FLOAT_TO_STR_SHORT:
    return FloatToStrShort(program, inst, op_stack, stack_pos, frame);

  case DESERL_BOOL_ARY:
    return DeserlBoolAry(program, inst, op_stack, stack_pos, frame);

  case ARY_TO_BYTES:
    return AryToBytes(program, inst, op_stack, stack_pos, frame);

  case BYTES_TO_ARY:
    return BytesToAry(program, inst, op_stack, stack_pos, frame);

  case COMPRESS_ZLIB_LEVEL:
    return CompressZlibLevel(program, inst, op_stack, stack_pos, frame);

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

  case COMPRESS_DEFLATE_BYTES:
    return CompressDeflateBytes (program, inst, op_stack, stack_pos, frame);

  case UNCOMPRESS_DEFLATE_BYTES:
    return UncompressDeflateBytes (program, inst, op_stack, stack_pos, frame);

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

  case FILE_CREATE_NEW:
    return FileCreateNew(program, inst, op_stack, stack_pos, frame);

  case PROCESS_ID:
    return ProcessId(program, inst, op_stack, stack_pos, frame);

  case PROCESS_ALIVE:
    return ProcessAlive(program, inst, op_stack, stack_pos, frame);

  case UNCOMPRESS_DEFLATE_LIMIT:
    return UncompressLimit(-MAX_WBITS, program, inst, op_stack, stack_pos, frame);

  case UNCOMPRESS_GZIP_LIMIT:
    return UncompressLimit(MAX_WBITS | 16, program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_BIND_ADDR:
    return SockTcpBindAddress(program, inst, op_stack, stack_pos, frame);

  case SOCK_TCP_SSL_LISTEN_ADDR:
    return SockTcpSslListenAddress(program, inst, op_stack, stack_pos, frame);

  case SOCK_DTLS_LISTEN_ADDR:
    return SockDtlsListenAddress(program, inst, op_stack, stack_pos, frame);

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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }
  const std::wstring out = BytesToUnicode((char*)(array + 3));

  // create character array
  const long char_array_size = (long)out.size();
  const long char_array_dim = 1;
  size_t* char_array = MemoryManager::AllocateArray(char_array_size + 1 + ((char_array_dim + 2) * sizeof(size_t)), 
                                                    CHAR_ARY_TYPE, op_stack, *stack_pos, false);
  char_array[0] = char_array_size;
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }
  const std::string out = UnicodeToBytes((wchar_t*)(array + 3));

  // create byte array
  const long byte_array_size = (long)out.size();
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)),
                                                    BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // allocate 'size' array and copy metadata
  long size = (long)array[1];
  long dim = 1;
  // 'array' was popped off the op_stack above, so nothing roots it any more, and it
  // is dereferenced again below. Allocating with collect enabled would make this a
  // collection point while that raw pointer is live: see AllocateObject in memory.h.
  size_t* mem = MemoryManager::AllocateArray(size + dim + 2, instructions::INT_TYPE,
                                             op_stack, *stack_pos, false);
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element");
  }
  // the element count and where the elements start, as the other literal copies read
  // them: a 1-D array's single extent sits where a 2-D one's first extent does, so
  // reading array[2] and writing at array + 3 overwrote a 2-D Char literal's second
  // extent and copied the wrong number of characters
  const long size = (long)array[0];
  const long dim = (long)array[1];
  wchar_t* str = (wchar_t*)(array + dim + 2);
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element");
  }
  const long size = (long)array[0];
  const long dim = (long)array[1];
  // copy elements
  size_t* str = (size_t*)(array + dim + 2);
  for(long i = 0; i < size; i++) {
    str[i] = PopInt(op_stack, stack_pos);
  }
  // The array is old (AllocateArray) and the Strings are young: record it, or a minor
  // GC neither marks nor repairs elements held only through this array (G13).
  MemoryManager::WriteBarrier(array);
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element");
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element");
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element");
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory element");
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

  // a Byte prints as its unsigned decimal value, as ToString, interpolation
  // and String->Append(Byte) give it (#1176); it printed as "0x41"
  const int value = (int)(unsigned char)PopInt(op_stack, stack_pos);
#ifdef _MODULE_STDIO
  program->output_buffer << value;
#else
  std::wcout << value;
#endif

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
    // A console read waits for input; park for it, as the socket reads do. The
    // bytes land in a C buffer (the collector can move a parked thread's young
    // array) and the array ref is re-rooted on the op_stack, then re-read.
    char* temp = new char[num > 0 ? num : 1];
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const size_t read = fread(temp, num, 1, stdin);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    if(read) {
      memcpy((char*)(array + 3) + offset, temp, num);
    }
    delete[] temp;
    PushInt(read, op_stack, stack_pos);
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
    // allocate temporary buffer
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry): the temp
    // is sized for 'num' and offset was checked against the destination, so a
    // large offset overflowed the heap allocation. A console read waits for
    // input; park for it, with the array ref re-rooted on the op_stack.
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    size_t read = fread(byte_buffer, 1, num, stdin);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    wchar_t* buffer = (wchar_t*)(array + 3);
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
    // A line read waits for input, indefinitely on an interactive console or a
    // pipe (the LSP reads stdin on one thread while others allocate); park for
    // it. Only C++ locals are touched while parked; the array ref is re-rooted
    // on the op_stack so GC promotion fixup relocates it, then re-read.
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    std::wstring wbuffer;
#ifdef _WIN32
    if(Runtime::StackInterpreter::IsBinaryStdio()) {
      std::string buffer;
      std::getline(std::cin, buffer);
      wbuffer = BytesToUnicode(buffer);
    }
    else {
      std::getline(std::wcin, wbuffer);
    }
#else
    // Bytes, as the array reads (fread) are. Reading lines through std::wcin
    // made stdin wide-oriented, and C forbids byte functions on a wide stream:
    // a program that read a line and then a buffer -- the LSP over stdio, a
    // header line and then a Content-Length body -- got no body at all, so
    // every stdio client on Linux and macOS was answered with a parse error,
    // or the server crashed. Decoded as wcin was: UTF-8, the locale posix_main
    // sets up.
    //
    // getc_unlocked: a thread blocked here waiting for input otherwise holds
    // stdin's lock, and exit() -- which takes every stream's lock to flush it --
    // then waits for it forever (glibc). A program that reads stdin on one
    // thread and exits from another, as the LSP does, never ended.
    std::string buffer;
    int c;
    while((c = getc_unlocked(stdin)) != EOF && c != '\n') {
      buffer += (char)c;
    }
    wbuffer = BytesToUnicode(buffer);
#endif
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);

    // copy to dest, bounded by its size: an unbounded copy of a long line ran
    // past the array
    wchar_t* dest = (wchar_t*)(array + 3);
    const size_t capacity = array[0];
    const size_t count = wbuffer.size() < capacity ? wbuffer.size() : capacity;
#ifdef _WIN32
    wcsncpy_s(dest, capacity + 1, wbuffer.c_str(), count);
#else
    wcsncpy(dest, wbuffer.c_str(), count);
    dest[count] = L'\0';
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
    const wchar_t* chars = (wchar_t*)(array + 3) + offset;
#ifdef _MODULE_STDIO
    program->output_buffer.write(chars, num);
    PushInt(num, op_stack, stack_pos);
#else
    // stdout is in a wide CRT mode -- _O_U8TEXT by default, or whatever
    // --objeck-stdio selected -- so the CRT performs the UTF-8 conversion. This
    // encoded to UTF-8 here as well and wrote the resulting BYTES, which that
    // stream reads back a pair at a time as one wchar_t: 'A','B' arrived as
    // U+4241, and the invalid sequence left the stream unusable for every later
    // write, so one call corrupted the rest of the program's output. It also
    // ignored num and stopped at a NUL instead. Hand wcout the wide characters
    // and let the stream's own mode decide the encoding, as the rest of the VM does.
    std::wcout.write(chars, num);
    PushInt(std::wcout.good() ? num : -1, op_stack, stack_pos);
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
  // decimal, as STD_OUT_BYTE (#1176); it wrote the byte as a character
  std::wcerr << (int)(unsigned char)PopInt(op_stack, stack_pos);

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
    // Save, modify and restore the SAME stream. This used to read the saved
    // state from std::wcout, modify std::wcerr, and then restore onto std::cout
    // -- three different objects. The effect was that wcerr kept the fixed/
    // scientific/precision setting permanently, so every later float written to
    // stderr came out in whatever format the last call happened to set, while
    // narrow std::cout had its flags overwritten with wcout's.
    const std::ios_base::fmtflags flags(std::wcerr.flags());
    const std::streamsize ss = std::wcerr.precision();

    if(precision == L"fixed") {
      std::wcerr << std::fixed;
    }
    else if(precision == L"scientific") {
      std::wcerr << std::scientific;
    }
    else {
      // SetSysProp refuses a value that is not one; this was an uncaught stoll
      int digits = 0;
      if(ParseFloatPrecision(precision, digits)) {
        std::wcerr << std::setprecision(digits);
      }
    }

    std::wcerr << value;
    std::wcerr.precision(ss);
    std::wcerr.flags(flags);
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Assert failed!");
  }
  
  return true;
}

bool TrapProcessor::SysCmd(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
#ifdef _WIN32
    // UTF-16, as System::CommandOutput runs its command: system() read the
    // UTF-8 bytes in the ANSI code page
    const std::wstring cmd((wchar_t*)(array + 3));
#else
    const std::string cmd = UnicodeToBytes((wchar_t*)(array + 3));
#endif
    // system() waits for the child to exit; park for it, so a collection on
    // another thread is not held up for the length of the command (cmd is C heap)
    MemoryManager::BeginBlocking();
#ifdef _WIN32
    const int status = _wsystem(cmd.c_str());
#else
    const int status = system(cmd.c_str());
#endif
    MemoryManager::EndBlocking();
    PushInt(status, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
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
  // name -> (was it set, its value): restored once the command has run
  std::map<std::wstring, std::pair<bool, std::wstring>> prev_env_variables;

  // Set the command's environment variables, saving what they replace. A
  // variable that was already set kept its old value: only unset ones were
  // assigned. On Windows the names and values are UTF-16 (_wgetenv_s and
  // _wputenv_s); the narrow calls took the UTF-8 bytes as ANSI.
  size_t* env_array = (size_t*)PopInt(op_stack, stack_pos);
  if(env_array) {
    const size_t env_str_count = env_array[2];
    for(size_t i = 0; i < env_str_count; ++i) {
      size_t* env_str_obj = (size_t*)env_array[i + 3];
      if(!env_str_obj) {
        continue;
      }
      env_str_obj = (size_t*)*env_str_obj;
      const std::wstring env_str((wchar_t*)(env_str_obj + 3));
      const size_t index = env_str.find(L'=');
      if(index != std::wstring::npos && index > 0) {
        const std::wstring name = env_str.substr(0, index);
        const std::wstring value = env_str.substr(index + 1);

        if(prev_env_variables.find(name) == prev_env_variables.end()) {
#ifdef _WIN32
          std::wstring prev_value;
          const bool was_set = GetEnvironmentValue(name, prev_value);
#else
          const char* prev_chars = getenv(UnicodeToBytes(name).c_str());
          const bool was_set = prev_chars != nullptr;
          const std::wstring prev_value = was_set ? BytesToUnicode(prev_chars) : L"";
#endif
          prev_env_variables[name] = std::make_pair(was_set, prev_value);
        }

#ifdef _WIN32
        _wputenv_s(name.c_str(), value.c_str());
#else
        setenv(UnicodeToBytes(name).c_str(), UnicodeToBytes(value).c_str(), 1);
#endif
      }
    }
  }

  size_t* command_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* str_array = (size_t*)command_obj[0];
  if(str_array) {
    const std::string cmd = UnicodeToBytes((wchar_t*)(str_array + 3));

    // The command runs to completion; park for it (cmd and the output lines are
    // C heap). command_obj is re-rooted on the op_stack, and stays there through
    // the allocations below -- any of them can move it -- until it is stored.
    int status = 0;
    PushInt((size_t)command_obj, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    std::vector<std::string> output_lines = System::CommandOutput(cmd.c_str(), status);
    MemoryManager::EndBlocking();
    
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

    // Write barrier: the array is old (AllocateArray never uses the nursery) and
    // the elements are young objects. A minor GC repairs references only in
    // objects the barrier recorded, so without this the elements are promoted
    // out from under an array the collector will not look inside.
    MemoryManager::WriteBarrier(str_obj_array);


    size_t* command_output_obj = MemoryManager::AllocateObject(program->GetCommandOutputObjectId(), op_stack, *stack_pos, false);
    command_obj = (size_t*)PopInt(op_stack, stack_pos);
    command_output_obj[0] = (size_t)command_obj;
    command_output_obj[1] = status;
    command_output_obj[2] = (size_t)str_obj_array;

    PushInt((size_t)command_output_obj, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  // restore the environment: a variable that was unset is unset again, rather
  // than left set to "" (POSIX) or to the command's value
  for(const auto& prev : prev_env_variables) {
    const bool was_set = prev.second.first;
#ifdef _WIN32
    // an empty value removes the variable
    _wputenv_s(prev.first.c_str(), was_set ? prev.second.second.c_str() : L"");
#else
    const std::string name = UnicodeToBytes(prev.first);
    if(was_set) {
      setenv(name.c_str(), UnicodeToBytes(prev.second.second).c_str(), 1);
    }
    else {
      unsetenv(name.c_str());
    }
#endif
  }

  return true;
}

bool TrapProcessor::Exit(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  VmExit((int)PopInt(op_stack, stack_pos));

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
    // a GMT date's Unix time is the calendar's; mktime read it as local time and
    // answered off by the zone offset (#1179)
    if(instance[8]) {
      PushInt((size_t)UtcSecondsOf(instance), op_stack, stack_pos);
      return true;
    }

    // create time structure
    struct tm set_time;
    memset(&set_time, 0, sizeof(struct tm));
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
    // by the calendar: gmtime_s rejects a negative time, which aborted the VM
    // with ">>> Unable to get GMT time <<<" for any date before 1970 (#1179)
    SetUtcInstance(instance, (INT64_VALUE)value);
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
      return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Unable to get local time");
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
    if(fscanf(fp, "%*s %ld", &pages) == 1 && pages > 0) {
      // /proc is parsed input, so the page count is not trusted to be sane: a value
      // large enough to wrap the multiply would report a nonsense RSS rather than
      // fail. sysconf() returns -1 on error, which would cast to SIZE_MAX.
      const long page_size = sysconf(_SC_PAGESIZE);
      if(page_size > 0 && (size_t)pages <= SIZE_MAX / (size_t)page_size) {
        rss = (size_t)pages * (size_t)page_size;
      }
    }
    fclose(fp);
  }
  return rss;
#endif
}

// Peak resident set size of this process, in bytes; 0 if unavailable. Windows:
// peak working set. POSIX: ru_maxrss, which Linux reports in kilobytes and
// macOS in bytes. Declared in memory.h for the OBJECK_GC_STATS exit summary.
size_t GetProcessPeakResidentBytes()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc;
  pmc.cb = sizeof(pmc);
  if(K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return (size_t)pmc.PeakWorkingSetSize;
  }
  return 0;
#else
  struct rusage ru;
  if(getrusage(RUSAGE_SELF, &ru) == 0 && ru.ru_maxrss > 0) {
#ifdef __APPLE__
    return (size_t)ru.ru_maxrss;
#else
    return (size_t)ru.ru_maxrss * 1024;
#endif
  }
  return 0;
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
    // Never below a current sample: the OS peak and the current RSS come from two
    // reads (two sources on POSIX), and a peak lower than "used" would be nonsense.
    { L"runtime.memory.peak",      []() -> size_t { const size_t p = GetProcessPeakResidentBytes(),
                                                                 r = GetProcessResidentBytes();
                                                    return p > r ? p : r; } },
    { L"runtime.memory.allocated", []() -> size_t { return MemoryManager::GetHeapAllocatedSize(); } },
    { L"runtime.memory.max",       []() -> size_t { return MemoryManager::GetHeapMaxSize(); } },
    // RSS minus the Objeck heap. The two are separate reads, so a heap that
    // grew between them can exceed the older RSS sample; report no overhead
    // rather than wrapping the subtraction around SIZE_MAX.
    { L"runtime.memory.overhead",  []() -> size_t { const size_t r = GetProcessResidentBytes(),
                                                                 a = MemoryManager::GetHeapAllocatedSize();
                                                    if(a >= r) {
                                                      return 0;
                                                    }
                                                    return r - a; } },
    { L"runtime.gc.minor",         []() -> size_t { return (size_t)MemoryManager::GetMinorGcCount(); } },
    { L"runtime.gc.major",         []() -> size_t { return (size_t)MemoryManager::GetMajorGcCount(); } },
    { L"runtime.gc.total",         []() -> size_t { return (size_t)(MemoryManager::GetMinorGcCount() +
                                                                    MemoryManager::GetMajorGcCount()); } },
    { L"runtime.gc.stw",           []() -> size_t { return MemoryManager::IsStwActive() ? 1 : 0; } },
    { L"runtime.gc.nursery.used",  []() -> size_t { return MemoryManager::GetNurseryUsed(); } },
    { L"runtime.gc.nursery.capacity", []() -> size_t { return MemoryManager::GetNurseryCapacity(); } },
    { L"runtime.gc.nursery.occupancy_permille", []() -> size_t {
                                                    const size_t cap = MemoryManager::GetNurseryCapacity();
                                                    if(cap == 0) {
                                                      return 0;
                                                    }
                                                    const size_t used = MemoryManager::GetNurseryUsed();
                                                    // Saturate rather than report >100%: used can exceed cap
                                                    // transiently while a collection is in flight.
                                                    if(used >= cap) {
                                                      return 1000;
                                                    }
                                                    // used * 1000 overflows once used passes SIZE_MAX/1000. That
                                                    // needs an ~18PB nursery, so it cannot happen -- but nothing
                                                    // here enforced it, and "cannot happen" is not the same as
                                                    // "cannot overflow". Scale the divisor instead of the dividend
                                                    // in that range so neither branch can wrap.
                                                    return (cap > (SIZE_MAX / 1000)) ? (used / (cap / 1000))
                                                                                     : ((used * 1000) / cap); } },
    { L"runtime.gc.remembered",    []() -> size_t { return MemoryManager::GetRememberedCount(); } },
    { L"runtime.gc.pause.last_us", []() -> size_t { return (size_t)MemoryManager::GetPauseLastUs(); } },
    { L"runtime.gc.pause.max_us",  []() -> size_t { return (size_t)MemoryManager::GetPauseMaxUs(); } },
    { L"runtime.gc.pause.avg_us",  []() -> size_t { return (size_t)MemoryManager::GetPauseAvgUs(); } },
    { L"runtime.gc.promoted.last", []() -> size_t { return MemoryManager::GetPromotedLast(); } },
    { L"runtime.gc.promoted.total",[]() -> size_t { return MemoryManager::GetPromotedTotal(); } },
    { L"runtime.gc.promoted.bytes",[]() -> size_t { return MemoryManager::GetPromotedBytes(); } },
    { L"runtime.gc.old.bytes",     []() -> size_t { return MemoryManager::GetOldGenBytes(); } },
    { L"runtime.gc.contention",    []() -> size_t { return (size_t)MemoryManager::GetGcContention(); } },
    // Compiled-in protocol support. Http3Connect is compiled unconditionally --
    // its #else branch just fails -- so "the trap exists" proves nothing about
    // whether HTTP/3 is actually built. These are the only reliable way for a
    // program (or a test) to tell an unsupported build from a network failure.
    { L"runtime.feature.http3",    []() -> size_t {
#if defined(OBJECK_HAS_NGTCP2) || defined(OBJECK_HAS_WINHTTP_H3)
                                                    return 1;
#else
                                                    return 0;
#endif
                                                  } },
    { L"runtime.feature.http2",    []() -> size_t {
#ifdef OBJECK_HAS_NGHTTP2
                                                    return 1;
#else
                                                    return 0;
#endif
                                                  } },
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

// Runtime->SetLocale: switches the C library's locale and, outside Windows,
// the one std::wcout converts through, or neither. A name the system lacks
// makes setlocale return NULL, and macOS's setlocale accepts a per-category
// composite ("C/C.UTF-8/C/C/C/C") that libc++ still cannot build into a
// std::locale; the constructor used to run unguarded on either, and its
// exception ended the program with ">>> virtual machine: internal error:
// locale constructed with null <<<". Such a name is refused and everything is
// left as it was. The program asked for this name itself, so it gets the
// refusal rather than a fallback locale it did not ask for.
static bool SetProgramLocale(const std::string& name)
{
#if defined(_WIN32) || !(defined(_X64) || defined(_ARM64))
  // Windows sets the C library's locale and leaves std::wcout alone, as the
  // x64 build always did. obr puts stdout in a Unicode or binary CRT mode
  // (_O_U8TEXT by default, or per --objeck-stdio) and writes wide characters
  // through wcout as the CRT expects; a locale imbued here makes wcout hand
  // narrow bytes to that stream instead, which the C runtime treats as an
  // invalid parameter. The ARM64 build, which imbued one, was stopped
  // (0xC0000409, a fast fail) once a program set a valid name, and its output
  // was lost.
  return setlocale(LC_ALL, name.c_str()) != nullptr;
#else
  const char* current = setlocale(LC_ALL, nullptr);
  const std::string previous(current ? current : "");

  const char* accepted = setlocale(LC_ALL, name.c_str());
  if(!accepted) {
    // a failed setlocale changes nothing
    return false;
  }

  // copied because the next setlocale call reuses the buffer
  const std::string chosen(accepted);
  try {
    std::locale lollocale(chosen.c_str());
    setlocale(LC_ALL, chosen.c_str());
    std::wcout.imbue(lollocale);
  }
  catch(const std::runtime_error&) {
    // the C library took the name and the C++ library cannot build it
    if(!previous.empty()) {
      setlocale(LC_ALL, previous.c_str());
    }
    return false;
  }
#if defined(_ARM64)
  setlocale(LC_ALL, "en_US.utf8");
#endif

  return true;
#endif
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

    // a name the system cannot supply is refused, and the property not stored
    if(!wcscmp(key, L"locale") && !SetProgramLocale(UnicodeToBytes(value))) {
      return true;
    }

    // A float-formatting precision must be a whole int: Float->ToString reads
    // "float:string:precision", and the stderr float writer "precision", which
    // also takes "fixed" or "scientific". Anything else was stored, and the
    // next float formatted died on std::stoll's uncaught exception. Now the
    // program is told here, where the bad value came from, with an error a ?->
    // handler recovers from; the property keeps its old value. An empty value
    // still clears the setting, as it always has.
    const bool is_float_precision = !wcscmp(key, L"float:string:precision");
    const bool is_err_precision = !wcscmp(key, L"precision") && wcscmp(value, L"fixed") && wcscmp(value, L"scientific");
    if((is_float_precision || is_err_precision) && value[0] != L'\0') {
      int precision = 0;
      if(!ParseFloatPrecision(value, precision)) {
        return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER,
                         std::wstring(L"Invalid float precision for '") + key + L"': '" + value + L"'");
      }
    }

    // The fixtures for the VM's last-resort catches (vm_error_exit.obs and
    // jit_bridge_exception.obs) need a C++ exception to leave a trap, and no
    // correct trap throws one. They rode on unguarded parses -- std::stod in
    // String->ToFloat, then std::stoll here -- and each fix took their trigger
    // away. This key throws on purpose, under the VM's own reserved "runtime."
    // namespace, so those catches stay tested without keeping a defect alive.
    if(!wcscmp(key, L"runtime.test.throw")) {
      throw std::runtime_error(UnicodeToBytes(value));
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
#ifdef _WIN32
    // UTF-16 both ways: getenv_s answered in the ANSI code page, which was then
    // decoded as UTF-8, and a value longer than its 1 KB buffer read as unset
    std::wstring value;
    if(GetEnvironmentValue((wchar_t*)(key_array + 3), value) && !value.empty()) {
      PushInt((size_t)CreateStringObject(value, program, op_stack, stack_pos), op_stack, stack_pos);
    }
#else
    const std::string key = UnicodeToBytes((wchar_t*)(key_array + 3));
    const char* value = getenv(key.c_str());
    if(value) {
      PushInt((size_t)CreateStringObject(BytesToUnicode(value), program, op_stack, stack_pos), op_stack, stack_pos);
    }
#endif
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

#ifdef _WIN32
    // _wputenv_s updates the CRT's wide and narrow tables and the process
    // environment that children inherit; _putenv_s took the UTF-8 bytes as ANSI
    _wputenv_s((wchar_t*)(key_array + 3), (wchar_t*)(value_array + 3));
#else
    const std::string key = UnicodeToBytes((wchar_t*)(key_array + 3));
    const std::string value = UnicodeToBytes((wchar_t*)(value_array + 3));
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
    // DNS resolution can block for seconds; park for it (name is C heap)
    MemoryManager::BeginBlocking();
    std::vector<std::string> addrs = IPSocket::Resolve(name.c_str());
    MemoryManager::EndBlocking();

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

    // Write barrier: the array is old (AllocateArray never uses the nursery) and
    // the elements are young objects. A minor GC repairs references only in
    // objects the barrier recorded, so without this the elements are promoted
    // out from under an array the collector will not look inside.
    MemoryManager::WriteBarrier(str_obj_array);

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

// the calling thread's last socket failures (see common.h)
thread_local SocketError socket_last_error = { 0, false };
thread_local SocketError tls_socket_error = { 0, false };
thread_local SocketError dtls_socket_error = { 0, false };

// the socket error of the call that just failed
static int CurrentSocketError()
{
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

// "Invalid argument": an address that is not one
#ifdef _WIN32
static const int SOCKET_INVALID_ARGUMENT = WSAEINVAL;
#else
static const int SOCKET_INVALID_ARGUMENT = EINVAL;
#endif

//
// Records a plain socket call's outcome for GetLastError (socket_last_error):
// its failure, 0 when it worked. Every trap that reaches the system records
// one, so the error always belongs to the thread's latest socket call; a trap
// on a socket that is not open does nothing, and leaves the reason it is not.
//
static inline void RecordSocketError(int error)
{
  socket_last_error = { error, false };
}

// a new TLS or DTLS operation on this thread: nothing has failed yet
static inline void ClearTlsError()
{
  tls_last_error = 0;
  tls_last_verify_flags = 0;
  tls_socket_error = { 0, false };
}

static inline void ClearDtlsError()
{
  dtls_last_error = 0;
  dtls_last_verify_flags = 0;
  dtls_socket_error = { 0, false };
}

// the system's text for a socket error code
static std::wstring SocketErrorText(int error)
{
#ifdef _WIN32
  char error_msg[SMALL_BUFFER_MAX] = {0};
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, (DWORD)error, 0, error_msg, SMALL_BUFFER_MAX, 0);
  // FormatMessage ends its text with CR LF
  size_t len = strlen(error_msg);
  while(len > 0 && (error_msg[len - 1] == '\n' || error_msg[len - 1] == '\r' || error_msg[len - 1] == ' ')) {
    error_msg[--len] = '\0';
  }
  if(len == 0) {
    return L"socket error " + std::to_wstring(error);
  }
  return BytesToUnicode(error_msg);
#else
  return BytesToUnicode(strerror(error));
#endif
}

// ... and for a recorded failure, which may be the resolver's
static std::wstring SocketErrorText(const SocketError& error)
{
#ifndef _WIN32
  // getaddrinfo's EAI_* codes are not errno values (on Windows they are WSA codes)
  if(error.resolver) {
    return BytesToUnicode(gai_strerror(error.code));
  }
#endif
  return SocketErrorText(error.code);
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
    // The reason comes back with the socket. It used to be read when
    // GetLastError ran, from the live WSAGetLastError()/errno -- after each
    // failed address's closesocket() had reset it (Nil on Windows) and the park
    // and anything else since had had their turn.
    SocketError error;
    const SOCKET sock = IPSocket::Open(addr.c_str(), static_cast<int>(port), static_cast<int>(timeout_ms), error);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);
    socket_last_error = error;
#ifdef _DEBUG
    std::wcout << L"# socket connect: addr='" << BytesToUnicode(addr) << "'; instance="
	       << instance << L"(" << (size_t)instance << L")" << L"; addr=" << sock << L"("
	       << (long)sock << L") #" << std::endl;
#endif
    instance[0] = sock;
  }
  else if(instance) {
    // no address: not open (the handle was left at 0, which read as open)
    instance[0] = (size_t)-1;
    RecordSocketError(SOCKET_INVALID_ARGUMENT);
  }

  return true;
}

//
// The port a bound socket actually has, by getsockname: the one asked for, or,
// for a bind of port 0, the one the system chose. Written back into a server
// object's @port after each bind (TCP, TLS, UDP and DTLS servers) so that
// GetPort() answers it, and tests need no fixed port numbers. 0 when the
// socket is not bound or the lookup fails.
//
static int BoundPort(SOCKET sock)
{
  struct sockaddr_storage bound;
  memset(&bound, 0, sizeof(bound));
  socklen_t bound_len = sizeof(bound);
  if(getsockname(sock, (struct sockaddr*)&bound, &bound_len) != 0) {
    return 0;
  }

  if(bound.ss_family == AF_INET6) {
    return ntohs(((const struct sockaddr_in6*)&bound)->sin6_port);
  }
  else if(bound.ss_family == AF_INET) {
    return ntohs(((const struct sockaddr_in*)&bound)->sin_port);
  }

  return 0;
}

bool TrapProcessor::SockTcpBind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  long port = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    SOCKET server = IPSocket::Bind(static_cast<int>(port));
    // captured now: by the time GetLastError runs, the live value is gone
#ifdef _WIN32
    const bool bound = server != INVALID_SOCKET;
#else
    const bool bound = server >= 0;
#endif
    RecordSocketError(bound ? 0 : CurrentSocketError());
#ifdef _DEBUG
    std::wcout << L"# socket bind: port=" << port << L"; instance=" << instance << L"("
      << (size_t)instance << L")" << L"; addr=" << server << L"(" << (size_t)server
      << L") #" << std::endl;
#endif
    instance[0] = (long)server;
    // @port: the port bound, which for a port of 0 the system chose
    if(bound) {
      const int bound_port = BoundPort(server);
      if(bound_port > 0) {
        instance[1] = bound_port;
      }
    }
  }

  return true;
}

//
// TCPSocketServer->New(address : String, port : Int): binds one local address
// (IPSocket::BindAddress); a Nil or empty address is the dual-stack wildcard,
// as New(port) binds. A failed bind leaves the handle at -1, so Listen()
// answers false and GetLastError() says why.
//
bool TrapProcessor::SockTcpBindAddress(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const long port = (long)PopInt(op_stack, stack_pos);
  size_t* str_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    std::string host;
    if(str_obj && str_obj[0]) {
      host = UnicodeToBytes((wchar_t*)((size_t*)str_obj[0] + 3));
    }

    // a name lookup can block: park, with only C locals touched while parked.
    // The instance is re-rooted on the op_stack so a collection that moves it
    // fixes the reference, and is re-read after (#874).
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    SocketError error;
    const SOCKET server = IPSocket::BindAddress(host.c_str(), static_cast<int>(port), SOCK_STREAM, error);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);

    // recorded as TCPSocketServer->New(port) does: GetLastError() says why --
    // for a name that does not resolve, the resolver's reason
    socket_last_error = error;

#ifdef _DEBUG
    std::wcout << L"# socket bind: address='" << BytesToUnicode(host) << L"'; port=" << port
      << L"; addr=" << server << L"(" << (long)server << L") #" << std::endl;
#endif
    instance[0] = (long)server;
    // @port: the port bound, which for a port of 0 the system chose
    if((long)server >= 0) {
      const int bound_port = BoundPort(server);
      if(bound_port > 0) {
        instance[1] = bound_port;
      }
    }
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
      RecordSocketError(0);
      PushInt(1, op_stack, stack_pos);
    }
    else {
      RecordSocketError(CurrentSocketError());
      PushInt(0, op_stack, stack_pos);
    }
  }
  else {
    // the bind failed: socket_last_error still holds why
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
      const int ready = socket_ready_for_io(sock, is_write);
      RecordSocketError(ready < 0 ? CurrentSocketError() : 0);
      PushInt(ready, op_stack, stack_pos);
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
    // can proceed (only C locals are touched while parked). 'server' is read from
    // instance[0] ABOVE for that reason: while parked, a collection can promote this
    // object and leave 'instance' pointing into the recycled nursery (#874), so it is
    // deliberately never dereferenced after this point -- the debug line below prints
    // the pointer only. Anything added here that reads instance[...] must re-root it
    // on the op_stack first and re-read it after, as SockTcpConnect does.
    MemoryManager::BeginBlocking();
    SOCKET client = IPSocket::Accept(server, client_address, client_port);
    RecordSocketError((long)client < 0 ? CurrentSocketError() : 0);
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
    // -1, not 0: every socket guard in this file tests `(long)instance[0] > -1`,
    // and the open path already stores INVALID_SOCKET/-1 on failure. Writing 0
    // here left a closed socket reading as *open*, so IsConnected() answered
    // true afterwards and later reads/writes/closes fell through to handle 0 --
    // harmless on Windows (WSAENOTSOCK), but fd 0 is stdin on POSIX.
    instance[0] = (size_t)-1;
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
    int error;
    MemoryManager::BeginBlocking();
    IPSocket::WriteBytes(data.c_str(), (int)data.size(), sock, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
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
      // the first failure is the line's
      int error = 0;
      int read_error;
      PushInt((size_t)array, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      do {
        value = IPSocket::ReadByte(sock, status, read_error);
        if(status < 0 && error == 0) {
          error = read_error;
        }
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
        IPSocket::ReadByte(sock, status, read_error);
        if(status < 0 && error == 0) {
          error = read_error;
        }
      }
      MemoryManager::EndBlocking();
      array = (size_t*)PopInt(op_stack, stack_pos);
      RecordSocketError(error);

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

    // connect() and the TLS handshake block on the peer; park for the duration,
    // as SockTcpConnect does. A thread blocked here unparked holds up every
    // stop-the-world collection, and when the peer is a thread of this VM that
    // is waiting for that collection, nothing moves again. Only C locals are
    // touched while parked; the instance ref is re-rooted on the op_stack so GC
    // promotion fixup relocates it, then re-read after the wait.
    SecureSocketCtx* old_sctx = (SecureSocketCtx*)instance[0];
    SecureSocketCtx* sctx = nullptr;
    // GetLastError() reports this connect, not an earlier failure
    ClearTlsError();
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    // Close existing connection if any
    IPSecureSocket::Close(old_sctx);
    const bool is_open = IPSecureSocket::Open(addr.c_str(), static_cast<int>(port), pem_file, sctx);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);
    // The old context is freed whatever happens next. A failed open used to
    // leave instance[0] pointing at it and instance[3] still claiming open, so
    // the next read, write or Close used, or freed, the context a second time.
    instance[0] = instance[1] = instance[2] = instance[3] = 0;
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

// TCPSecureSocket and TCPSecureSocketServer both call SOCK_TCP_SSL_ISSUER and
// SOCK_TCP_SSL_SUBJECT, but instance[0] is a SecureSocketCtx in one and a
// SecureServerCtx in the other. A server's context was read as a socket's,
// handing mbedtls_ssl_get_peer_cert a "ssl" that was really the server's
// configuration: garbage, or a crash. A socket reports its peer's
// certificate, a server its own, told apart by the instance's class.
static const mbedtls_x509_name* TlsCertName(size_t* instance, bool subject)
{
  if(!instance || !instance[0]) {
    return nullptr;
  }

  const mbedtls_x509_crt* cert = nullptr;
  StackClass* cls = MemoryManager::GetClass(instance);
  if(cls && cls->GetName() == L"System.IO.Net.TCPSecureSocketServer") {
    // set only once Listen has parsed the certificate
    cert = &((SecureServerCtx*)instance[0])->srvcert;
  }
  else {
    cert = mbedtls_ssl_get_peer_cert(&((SecureSocketCtx*)instance[0])->ssl);
  }

  if(!cert) {
    return nullptr;
  }

  return subject ? &cert->subject : &cert->issuer;
}

bool TrapProcessor::SockTcpSslIssuer(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  const mbedtls_x509_name* name = TlsCertName(instance, false);
  if(name) {
    char buffer[LARGE_BUFFER_MAX] = {0};
    mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, name);
    const std::wstring in = BytesToUnicode(buffer);
    PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockTcpSslSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  const mbedtls_x509_name* name = TlsCertName(instance, true);
  if(name) {
    char buffer[LARGE_BUFFER_MAX] = {0};
    mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, name);
    const std::wstring in = BytesToUnicode(buffer);
    PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
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
  // the close_notify alert is a write, which can block on a full send buffer;
  // park for it (sctx is C heap, the instance ref is re-rooted)
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  IPSecureSocket::Close(sctx);
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);
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
      ClearTlsError();
      const std::string out = UnicodeToBytes((wchar_t*)(array + 3));
      // a TLS write can block on a full send buffer; park (out is C heap)
      MemoryManager::BeginBlocking();
      IPSecureSocket::WriteBytes(out.c_str(), (int)out.size(), sctx);
      MemoryManager::EndBlocking();
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
      ClearTlsError();
      const size_t capacity = array[0];
      size_t index = 0;
      char value;
      bool end_line = false;
      // the read loop blocks on the peer; park for its duration, as
      // SockTcpInString does. Unparked, a thread waiting here for a response
      // stalls every stop-the-world collection, and when the responder is a
      // thread of this VM that triggered one, both wait forever. Only C locals
      // are touched while parked; the array ref is re-rooted on the op_stack so
      // GC promotion fixup relocates it, then re-read after the wait.
      PushInt((size_t)array, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      do {
        value = IPSecureSocket::ReadByte(sctx, status);
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
        IPSecureSocket::ReadByte(sctx, status);
      }
      MemoryManager::EndBlocking();
      array = (size_t*)PopInt(op_stack, stack_pos);

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

//
// Binds a server's listening socket for mbedtls: a wildcard socket that takes
// IPv4 and IPv6 clients (IPSocket::BindAny), listening when it is TCP.
// mbedtls_net_bind(nullptr, ...) bound whichever passive address getaddrinfo
// offered first -- [::] with IPV6_V6ONLY on, so IPv6 only, on Windows; 0.0.0.0
// on Linux -- so a TLS or DTLS server was reachable over one family, and
// which one depended on the platform. A client of "localhost" went wherever
// its resolver listed first and, on a mismatch, found nothing there.
//
// A non-empty host binds that one address instead (IPSocket::BindAddress).
//
// reason is the OS reason behind a failure, for GetLastError:
// MBEDTLS_ERR_NET_BIND_FAILED alone does not say why (tls_socket_error for a
// TLS server, dtls_socket_error for a DTLS one).
//
static int BindServerSocket(mbedtls_net_context& ctx, int port, int type, SocketError& reason, const char* host = nullptr)
{
  const SOCKET sock = IPSocket::BindAddress(host, port, type, reason);
#ifdef _WIN32
  if(sock == INVALID_SOCKET) {
#else
  if(sock < 0) {
#endif
    return MBEDTLS_ERR_NET_BIND_FAILED;
  }

  if(type == SOCK_STREAM && listen(sock, MBEDTLS_NET_LISTEN_BACKLOG) != 0) {
    reason = { CurrentSocketError(), false };
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return MBEDTLS_ERR_NET_LISTEN_FAILED;
  }

  ctx.fd = (int)sock;
  return 0;
}

//
// mbedtls_net_accept for a DTLS server. A datagram "accept" connects the
// listening socket to the client that sent the first datagram, hands it to the
// client, and binds a new listener. mbedtls binds that listener to the local
// address the first client happened to use, so after one IPv4 client the
// server stopped hearing IPv6 ones (and the reverse); on Windows, an IPv4
// client of the dual-stack socket failed the accept outright, since the
// mapped local address cannot be bound by a new IPv6-only socket. This binds
// the new listener to the wildcard, dual-stack, every time -- or, for a server
// given one address, to that address again (DtlsRebind): the wildcard there
// would open the server to every interface after its first client.
//
// client_ip receives the client's address bytes (4 or 16), the transport id
// DTLS cookies are bound to.
//
//
// A fresh DTLS listener on the one address a server was given (bound_addr,
// recorded by getsockname at Listen), as IPSocket::BindAddress bound it:
// SO_REUSEADDR, so it can share the port with the socket just handed to the
// client, and IPv6-only for an IPv6 address. No name lookup: nothing here
// blocks.
//
static SOCKET DtlsRebind(DtlsServerCtx* srv)
{
  const int family = ((const struct sockaddr*)srv->bound_addr)->sa_family;
  SOCKET sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if(sock == INVALID_SOCKET) {
    return INVALID_SOCKET;
  }
#else
  if(sock < 0) {
    return -1;
  }
#endif

  int on = 1;
  if(family == AF_INET6) {
    setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(on));
  }
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

  if(::bind(sock, (const struct sockaddr*)srv->bound_addr, srv->bound_len) != 0) {
#ifdef _WIN32
    closesocket(sock);
    return INVALID_SOCKET;
#else
    close(sock);
    return -1;
#endif
  }

  return sock;
}

static int DtlsAccept(DtlsServerCtx* srv, mbedtls_net_context* client, unsigned char* client_ip,
                      size_t buffer_size, size_t* ip_len)
{
  const int fd = srv->listen_fd.fd;
  if(fd < 0) {
    return MBEDTLS_ERR_NET_ACCEPT_FAILED;
  }

  // Wait for a ClientHello, leaving it queued for the handshake. Anything else
  // arriving here -- typically the close_notify of a client that was just
  // served, landing after its own socket closed -- is discarded: connecting
  // to its sender made the next handshake wait out its timeout for a hello
  // that sender never sends, while the real next client went unheard.
  struct sockaddr_storage client_addr;
  socklen_t addr_len;
  for(;;) {
    memset(&client_addr, 0, sizeof(client_addr));
    addr_len = sizeof(client_addr);
    unsigned char peek[16] = {0};
    int ret = (int)recvfrom(fd, (char*)peek, sizeof(peek), MSG_PEEK, (struct sockaddr*)&client_addr, &addr_len);
#ifdef _WIN32
    // a datagram longer than the peek buffer is still a datagram, peeked in part
    if(ret == SOCKET_ERROR && WSAGetLastError() == WSAEMSGSIZE) {
      ret = (int)sizeof(peek);
    }
#endif
    if(ret < 0) {
      return MBEDTLS_ERR_NET_ACCEPT_FAILED;
    }

    // DTLS record header: content type 22 (handshake), version, epoch 0, ...;
    // then the handshake message type, 1 (ClientHello), at offset 13
    if(ret >= 14 && peek[0] == 22 && peek[3] == 0 && peek[4] == 0 && peek[13] == 1) {
      break;
    }

    // not a hello: consume it and keep waiting
    char discard[1];
    recvfrom(fd, discard, sizeof(discard), 0, nullptr, nullptr);
  }

  if(connect(fd, (struct sockaddr*)&client_addr, addr_len) != 0) {
    return MBEDTLS_ERR_NET_ACCEPT_FAILED;
  }

  // connect() filters what arrives from now on, but not what is already
  // queued: a datagram another peer sent before it -- the close_notify of the
  // client served last, landing after its socket closed -- would be read as
  // this client's next handshake flight, failing it ("processing of the
  // ClientHello failed"). Drop every queued datagram that is not this client's.
  for(;;) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    struct timeval no_wait = { 0, 0 };
    if(select(fd + 1, &readable, nullptr, nullptr, &no_wait) <= 0) {
      break;
    }

    struct sockaddr_storage from;
    memset(&from, 0, sizeof(from));
    socklen_t from_len = sizeof(from);
    char head[1];
    const int peeked = (int)recvfrom(fd, head, sizeof(head), MSG_PEEK, (struct sockaddr*)&from, &from_len);
#ifdef _WIN32
    if(peeked == SOCKET_ERROR && WSAGetLastError() != WSAEMSGSIZE) {
#else
    if(peeked < 0) {
#endif
      break;
    }

    if(from_len == addr_len && !memcmp(&from, &client_addr, addr_len)) {
      // this client's: everything after it is filtered by connect()
      break;
    }

    char discard[1];
    recvfrom(fd, discard, sizeof(discard), 0, nullptr, nullptr);
  }

  // the connected socket is the client's now; listen again on a fresh one
  client->fd = fd;
  srv->listen_fd.fd = -1;
  const SOCKET next = srv->bound_len > 0 ? DtlsRebind(srv) : IPSocket::BindAny(srv->port, SOCK_DGRAM);
#ifdef _WIN32
  if(next != INVALID_SOCKET) {
#else
  if(next >= 0) {
#endif
    srv->listen_fd.fd = (int)next;
  }
  // else the next Accept fails; this client is served regardless

  if(client_addr.ss_family == AF_INET6) {
    const struct sockaddr_in6* addr6 = (const struct sockaddr_in6*)&client_addr;
    *ip_len = sizeof(addr6->sin6_addr);
    if(*ip_len > buffer_size) {
      return MBEDTLS_ERR_NET_BUFFER_TOO_SMALL;
    }
    memcpy(client_ip, &addr6->sin6_addr, *ip_len);
  }
  else {
    const struct sockaddr_in* addr4 = (const struct sockaddr_in*)&client_addr;
    *ip_len = sizeof(addr4->sin_addr);
    if(*ip_len > buffer_size) {
      return MBEDTLS_ERR_NET_BUFFER_TOO_SMALL;
    }
    memcpy(client_ip, &addr4->sin_addr, *ip_len);
  }

  return 0;
}

bool TrapProcessor::SockTcpSslListen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  SecureServerListen(instance, std::string(), op_stack, stack_pos);
  return true;
}

//
// TCPSecureSocketServer->Listen() for a server given an address: as
// SockTcpSslListen, but the listener binds that one address. Called as
// method : ListenAddress(address : String) ~ Bool.
//
bool TrapProcessor::SockTcpSslListenAddress(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* str_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  std::string host;
  if(str_obj && str_obj[0]) {
    host = UnicodeToBytes((wchar_t*)((size_t*)str_obj[0] + 3));
  }
  SecureServerListen(instance, host, op_stack, stack_pos);

  return true;
}

//
// Sets up a TLS server and binds its listener: the wildcard when host is
// empty, else that address. Pushes true on success, false otherwise.
//
void TrapProcessor::SecureServerListen(size_t* instance, const std::string& host, size_t*& op_stack, size_t*& stack_pos)
{
  if(instance) {
    size_t* cert_obj = (size_t*)instance[3];
    size_t* key_obj = (size_t*)instance[4];
    size_t* passwd_obj = (size_t*)instance[5];
    const long port = (long)instance[6];

    if(cert_obj && key_obj) {
      ClearTlsError();
      SecureServerCtx* srv = new SecureServerCtx();

      // Seed the random number generator
      const char* pers = "objeck_ssl_server";
      int ret = mbedtls_ctr_drbg_seed(&srv->ctr_drbg, mbedtls_entropy_func, &srv->entropy,
                                       (const unsigned char*)pers, strlen(pers));
      if(ret != 0) {
        tls_last_error = ret;
        tls_last_verify_flags = 0;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      // Load server certificate
      const std::string cert_path = UnicodeToBytes((wchar_t*)((size_t*)cert_obj[0] + 3));
      ret = TlsParseCertFile(&srv->srvcert, cert_path);
      if(ret != 0) {
        tls_last_error = ret;
        tls_last_verify_flags = 0;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
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

      ret = TlsParseKeyFile(&srv->pkey, key_path, passwd_cstr, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      if(ret != 0) {
        tls_last_error = ret;
        tls_last_verify_flags = 0;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      // Bind to port: IPv4 and IPv6 alike (see BindServerSocket), or the
      // one address asked for
      if(host.empty()) {
        ret = BindServerSocket(srv->listen_fd, (int)port, SOCK_STREAM, tls_socket_error);
      }
      else {
        // a name lookup can block: park, touching only C locals and srv (C
        // heap). The instance is re-rooted on the op_stack so a collection
        // that moves it fixes the reference, and is re-read after (#874).
        PushInt((size_t)instance, op_stack, stack_pos);
        MemoryManager::BeginBlocking();
        ret = BindServerSocket(srv->listen_fd, (int)port, SOCK_STREAM, tls_socket_error, host.c_str());
        MemoryManager::EndBlocking();
        instance = (size_t*)PopInt(op_stack, stack_pos);
      }
      if(ret != 0) {
        tls_last_error = ret;
        tls_last_verify_flags = 0;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      // Configure SSL for server
      ret = mbedtls_ssl_config_defaults(&srv->conf, MBEDTLS_SSL_IS_SERVER,
                                         MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
      if(ret != 0) {
        tls_last_error = ret;
        tls_last_verify_flags = 0;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      mbedtls_ssl_conf_rng(&srv->conf, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      mbedtls_ssl_conf_own_cert(&srv->conf, &srv->srvcert, &srv->pkey);

      instance[0] = (size_t)srv;
      instance[1] = 0;
      instance[2] = 0;
      // @port: the port bound, which for a port of 0 the system chose
      const int bound_port = BoundPort((SOCKET)srv->listen_fd.fd);
      if(bound_port > 0) {
        instance[6] = bound_port;
      }

      PushInt(1, op_stack, stack_pos);
      return;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return;
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
      ClearTlsError();

      // Accept raw TCP connection. accept() blocks indefinitely; park so STW GC
      // elsewhere can proceed. The instance ref is re-rooted on the op_stack so
      // promotion fixup relocates it (srv/client are C heap, safe across a park).
      // The handshake blocks on the peer just as accept() does, so it stays
      // inside the parked region: a peer that is a thread of this VM may need a
      // collection to finish before it can send its next flight.
      PushInt((size_t)instance, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      int ret = mbedtls_net_accept(&srv->listen_fd, &client->net, nullptr, 0, nullptr);
      if(ret == 0) {
        // Setup SSL session for this client (uses server's config)
        ret = mbedtls_ssl_setup(&client->ssl, &srv->conf);
        if(ret == 0) {
          mbedtls_ssl_set_bio(&client->ssl, &client->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

          // Perform handshake
          while((ret = mbedtls_ssl_handshake(&client->ssl)) != 0) {
            if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
              break;
            }
          }
        }
      }
      MemoryManager::EndBlocking();
      instance = (size_t*)PopInt(op_stack, stack_pos);
      tls_last_error = ret;
      if(ret != 0) {
        client->SetError(ret);
        delete client;
        PushInt(0, op_stack, stack_pos);
        return true;
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

      // numeric; an IPv4 client of the dual-stack listener arrives as
      // ::ffff:a.b.c.d and is reported as a.b.c.d
      char host_name[NI_MAXHOST] = {0};
      int peer_port;
      IPSocket::FormatAddress((struct sockaddr*)&pin, host_name, sizeof(host_name), peer_port);

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
  // The calling thread's last socket call's failure, captured as it happened
  // (see socket_last_error), and no error at all is Nil, as documented. The
  // live WSAGetLastError()/errno read here before said "The operation
  // completed successfully." or Nil for a refused connect, and whatever an
  // unrelated call had left for one that worked.
  const SocketError error = socket_last_error;
  if(error.code != 0) {
    PushInt((size_t)CreateStringObject(SocketErrorText(error), program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

//
// TCPSecureSocket->GetLastError() and TCPSecureSocketServer->GetLastError(),
// functions: the calling thread's last TLS failure (tls_last_error), which
// also covers a connect, listen or accept that failed and freed its context.
// This always answered Nil, so a refused certificate, an unreachable host and
// a bad key file were indistinguishable to a program.
//
bool TrapProcessor::SockTcpSslError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  if(tls_last_error != 0) {
    const std::wstring error_msg = TlsErrorMessage(tls_last_error, tls_last_verify_flags, tls_socket_error);
    PushInt((size_t)CreateStringObject(error_msg, program, op_stack, stack_pos), op_stack, stack_pos);
    return true;
  }

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

// A socket option call's result, recorded as any other socket call's. The
// guards are the open-socket test every other trap uses: "(SOCKET)handle > 0"
// was true for a closed socket's -1 on Windows, where SOCKET is unsigned.
static size_t SocketOptionResult(bool ok)
{
  RecordSocketError(ok ? 0 : CurrentSocketError());
  return ok ? 1 : 0;
}

bool TrapProcessor::SockTcpSetKeepAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool enable = (bool)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance && (long)instance[0] > -1) {
    PushInt(SocketOptionResult(IPSocket::SetKeepAlive((SOCKET)instance[0], enable)), op_stack, stack_pos);
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
  if(instance && (long)instance[0] > -1) {
    PushInt(SocketOptionResult(IPSocket::SetNoDelay((SOCKET)instance[0], enable)), op_stack, stack_pos);
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
  if(instance && (long)instance[0] > -1) {
    PushInt(SocketOptionResult(IPSocket::SetRecvTimeout((SOCKET)instance[0], ms)), op_stack, stack_pos);
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
  if(instance && (long)instance[0] > -1) {
    PushInt(SocketOptionResult(IPSocket::SetSendTimeout((SOCKET)instance[0], ms)), op_stack, stack_pos);
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
  if(instance && (long)instance[0] > -1) {
    PushInt(SocketOptionResult(IPSocket::SetRecvBufSize((SOCKET)instance[0], bytes)), op_stack, stack_pos);
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
  if(instance && (long)instance[0] > -1) {
    PushInt(SocketOptionResult(IPSocket::SetSendBufSize((SOCKET)instance[0], bytes)), op_stack, stack_pos);
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

//
// A decoded byte-order mark: U+FEFF, or U+FFFE when its bytes were swapped.
// The text reads dropped a first character equal to any of 0xFEFF, 0xFFFE,
// 0xFFFE0000 and 0xEFBBBF. The last two are not characters -- the UTF-32
// mark's bytes in the wrong order, and the UTF-8 mark's three bytes -- and a
// 16-bit Windows wchar_t truncates them to U+0000 and U+BBBF, so text that
// began with the Hangul syllable U+BBBF lost it. FileInCharAry compared
// through size_t, which on POSIX could never equal 0xFFFE0000 (-Wtype-limits).
//
static inline bool IsByteOrderMark(wchar_t c)
{
  return c == (wchar_t)0xFEFF || c == (wchar_t)0xFFFE;
}

//
// UDP endpoints. A UDPSocket is a client of one peer; a UDPSocketServer
// answers whoever wrote to it last. Both keep a UdpEndpoint (C heap, in the
// instance's second field) and their handle in the first.
//
// A client's socket is connected to its peer, so it hears that peer only and
// the network's answers about it reach the socket: an ICMP port unreachable
// for a datagram is reported on a later call as a refusal (ECONNREFUSED;
// WSAECONNRESET on Windows, which reports it only on a receive). The name
// can resolve to several addresses -- "localhost" is ::1 and 127.0.0.1 -- and
// the socket moves on to the next address when the current one is refused,
// as a TCP connect tries each address in turn: a client of a server that
// listens on one family reaches it. The socket is dual-stack (IPv4 addresses
// are reached as IPv4-mapped IPv6 ones) where the host has IPv6, so moving on
// re-connects the same socket: a thread blocked reading it is not left on a
// closed one. When every address has refused, the call that found out fails
// and GetLastError says "Connection refused".
//
// A server is not connected: one peer that has gone must not fail the next
// read of a server that serves many. Linux never reports it to an
// unconnected socket; Windows did, failing that read with "An existing
// connection was forcibly closed by the remote host.", and SIO_UDP_CONNRESET
// turns that off.
//
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

struct UdpEndpoint {
  SOCKET sock;
  bool client;
  // a server's last sender, the address a write goes to
  struct sockaddr_storage peer;
  socklen_t peer_len;
  // a client's addresses, in the resolver's order, as the socket's family
  // reaches them, and the one it is connected to
  std::vector<struct sockaddr_storage> candidates;
  std::vector<socklen_t> candidate_lens;
  size_t current;
};

static inline bool UdpValid(SOCKET sock)
{
#ifdef _WIN32
  return sock != INVALID_SOCKET;
#else
  return (long)sock > -1;
#endif
}

static inline void UdpCloseHandle(SOCKET sock)
{
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

// the network's answer that nobody listens at the peer
static inline bool IsRefusal(int error)
{
#ifdef _WIN32
  return error == WSAECONNRESET || error == WSAECONNREFUSED;
#else
  return error == ECONNREFUSED;
#endif
}

// a refusal as the error a program sees: Windows reports a refused datagram
// as WSAECONNRESET ("An existing connection was forcibly closed ..."), which
// says nothing of a datagram nobody was there to take
static inline int Refused(int error)
{
#ifdef _WIN32
  return IsRefusal(error) ? WSAECONNREFUSED : error;
#else
  return error;
#endif
}

//
// A refusal the network has already reported to a connected UDP socket, taken
// without blocking and without consuming a datagram; 0 when there is none.
// POSIX keeps it as the socket's pending error. Windows hands it to the next
// receive only, so a sender that never reads learned nothing: a readable socket
// is peeked, and a refusal comes back from the peek (a datagram waiting there
// answers WSAEMSGSIZE to a one-byte peek, and stays).
//
static int PendingRefusal(SOCKET sock)
{
#ifdef _WIN32
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(sock, &readable);
  struct timeval now = { 0, 0 };
  if(select(0, &readable, nullptr, nullptr, &now) <= 0) {
    return 0;
  }

  char value;
  if(recv(sock, &value, 1, MSG_PEEK) == SOCKET_ERROR) {
    const int error = WSAGetLastError();
    if(!IsRefusal(error)) {
      return 0;
    }
    // A peek leaves the report in place, so it would be found again after
    // moving on to the next address; a receive takes it (and returns at once:
    // the report is first in line).
    recv(sock, &value, 1, 0);
    return Refused(error);
  }
  return 0;
#else
  int error = 0;
  socklen_t len = sizeof(error);
  if(getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
    return 0;
  }
  return IsRefusal(error) ? error : 0;
#endif
}

// connects the client to its next address that connects; false, and nothing
// changed, when none is left
static bool UdpAdvance(UdpEndpoint* ep, SocketError& error)
{
  const size_t start = ep->current == (size_t)-1 ? 0 : ep->current + 1;
#ifndef _WIN32
  // The first connect fixed the socket's local address as well as its peer
  // (::1 for a peer of ::1), and connecting again keeps it: datagrams then go
  // out to the next address, but its replies, sent back to 127.0.0.1, never
  // match the socket. Dissolving the association first lets the next connect
  // choose the local address to suit the peer.
  if(ep->current != (size_t)-1) {
    struct sockaddr unspecified;
    memset(&unspecified, 0, sizeof(unspecified));
    unspecified.sa_family = AF_UNSPEC;
    connect(ep->sock, &unspecified, sizeof(unspecified));
  }
#endif
  for(size_t i = start; i < ep->candidates.size(); ++i) {
    if(connect(ep->sock, (const struct sockaddr*)&ep->candidates[i], ep->candidate_lens[i]) == 0) {
      ep->current = i;
      error = { 0, false };
      return true;
    }
    // the first reason is the preferred address's
    if(error.code == 0) {
      error = { CurrentSocketError(), false };
    }
  }
  return false;
}

//
// A client endpoint for address and port: every address the name resolves to,
// connected to the first that connects. A name lookup can block: the caller
// parks around it.
//
static UdpEndpoint* UdpOpen(const char* address, int port, SocketError& error)
{
  IPSocket::Startup();
  error = { 0, false };

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo* result = nullptr;
  const std::string port_str = std::to_string(port);
  const int status = getaddrinfo(address, port_str.c_str(), &hints, &result);
  if(status != 0) {
#ifndef _WIN32
    // the EAI_* codes are not errno values; EAI_SYSTEM means errno says why
    if(status == EAI_SYSTEM && errno != 0) {
      error = { errno, false };
      return nullptr;
    }
#endif
    error = { status, true };
    return nullptr;
  }

  // dual-stack where the host has IPv6, else IPv4 only
  bool dual_stack = false;
  SOCKET sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if(UdpValid(sock)) {
    int v6_only = 0;
    if(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6_only, sizeof(v6_only)) == 0) {
      dual_stack = true;
    }
    else {
      UdpCloseHandle(sock);
      sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
  }
  else {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  }
  if(!UdpValid(sock)) {
    error = { CurrentSocketError(), false };
    freeaddrinfo(result);
    return nullptr;
  }

  UdpEndpoint* ep = new UdpEndpoint();
  ep->sock = sock;
  ep->client = true;
  ep->current = (size_t)-1;
  memset(&ep->peer, 0, sizeof(ep->peer));
  ep->peer_len = 0;

  for(struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = 0;
    if(ptr->ai_family == AF_INET6) {
      if(!dual_stack) {
        continue;
      }
      memcpy(&addr, ptr->ai_addr, ptr->ai_addrlen);
      len = (socklen_t)ptr->ai_addrlen;
    }
    else if(ptr->ai_family == AF_INET) {
      if(dual_stack) {
        // ::ffff:a.b.c.d
        const struct sockaddr_in* in4 = (const struct sockaddr_in*)ptr->ai_addr;
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)&addr;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = in4->sin_port;
        unsigned char* bytes = (unsigned char*)&in6->sin6_addr;
        bytes[10] = 0xff;
        bytes[11] = 0xff;
        memcpy(bytes + 12, &in4->sin_addr, 4);
        len = sizeof(struct sockaddr_in6);
      }
      else {
        memcpy(&addr, ptr->ai_addr, ptr->ai_addrlen);
        len = (socklen_t)ptr->ai_addrlen;
      }
    }
    else {
      continue;
    }
    ep->candidates.push_back(addr);
    ep->candidate_lens.push_back(len);
  }
  freeaddrinfo(result);

  if(!UdpAdvance(ep, error)) {
    if(error.code == 0) {
#ifdef _WIN32
      error = { WSAEAFNOSUPPORT, false };
#else
      error = { EAFNOSUPPORT, false };
#endif
    }
    UdpCloseHandle(ep->sock);
    delete ep;
    return nullptr;
  }

  return ep;
}

//
// A server endpoint: the dual-stack wildcard (IPSocket::BindAny, as the TCP,
// TLS and DTLS servers bind), or one address. No SO_REUSEADDR: it would let a
// second server bind the same port. A name lookup can block: the caller
// parks around it.
//
static UdpEndpoint* UdpBind(const std::string& host, int port, SocketError& error)
{
  const SOCKET sock = IPSocket::BindAddress(host.c_str(), port, SOCK_DGRAM, error, false);
  if(!UdpValid(sock)) {
    return nullptr;
  }

#ifdef _WIN32
  // a reply to a client that has gone must not fail the next read (see above)
  BOOL report = FALSE;
  DWORD returned = 0;
  WSAIoctl(sock, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);
#endif

  UdpEndpoint* ep = new UdpEndpoint();
  ep->sock = sock;
  ep->client = false;
  ep->current = (size_t)-1;
  // the bound address, until a read replaces it with a sender's
  memset(&ep->peer, 0, sizeof(ep->peer));
  ep->peer_len = sizeof(ep->peer);
  if(getsockname(sock, (struct sockaddr*)&ep->peer, &ep->peer_len) != 0) {
    ep->peer.ss_family = AF_INET6;
    ep->peer_len = sizeof(struct sockaddr_in6);
  }

  return ep;
}

//
// Sends one datagram: a client to its peer, moving on to its next address
// while the current one has been refused, a server to its last sender. The
// number of bytes sent, or -1 with the reason in error.
//
static int UdpSend(UdpEndpoint* ep, const char* buffer, int len, int& error)
{
  error = 0;
  if(!ep->client) {
    const int sent = static_cast<int>(sendto(ep->sock, buffer, len, 0, (const struct sockaddr*)&ep->peer, ep->peer_len));
    if(sent < 0) {
      error = CurrentSocketError();
    }
    return sent;
  }

  while(true) {
    SocketError advance;
    advance = { 0, false };
    const int refused = PendingRefusal(ep->sock);
    if(refused) {
      if(UdpAdvance(ep, advance)) {
        continue;
      }
      // every address has refused; nothing is sent
      error = refused;
      return -1;
    }

    const int sent = static_cast<int>(send(ep->sock, buffer, len, 0));
    if(sent < 0) {
      const int send_error = CurrentSocketError();
      if(IsRefusal(send_error) && UdpAdvance(ep, advance)) {
        continue;
      }
      error = Refused(send_error);
      return -1;
    }
    return sent;
  }
}

//
// Receives one datagram: a client from its peer, a server from anyone (who
// becomes its peer). A client's refusal moves it on to its next address, for
// the calls that follow; this one fails with it.
//
static int UdpReceive(UdpEndpoint* ep, char* buffer, int len, int& error)
{
  error = 0;
  if(ep->client) {
    const int read = static_cast<int>(recv(ep->sock, buffer, len, 0));
    if(read < 0) {
      error = Refused(CurrentSocketError());
      if(IsRefusal(error)) {
        SocketError advance;
        advance = { 0, false };
        UdpAdvance(ep, advance);
      }
    }
    return read;
  }

  struct sockaddr_storage sender;
  socklen_t sender_len = sizeof(sender);
  const int read = static_cast<int>(recvfrom(ep->sock, buffer, len, 0, (struct sockaddr*)&sender, &sender_len));
  if(read < 0) {
    error = CurrentSocketError();
  }
  else {
    ep->peer = sender;
    ep->peer_len = sender_len;
  }
  return read;
}

static void UdpClose(UdpEndpoint* ep)
{
  UdpCloseHandle(ep->sock);
  delete ep;
}

// the handshake is done (ssl.state is private from mbedTLS 3)
static inline bool DtlsHandshakeOver(const mbedtls_ssl_context* ssl)
{
#if MBEDTLS_VERSION_NUMBER >= 0x03020000
  return mbedtls_ssl_is_handshake_over((mbedtls_ssl_context*)ssl) != 0;
#else
  return ssl->state == MBEDTLS_SSL_HANDSHAKE_OVER;
#endif
}

//
// A DTLS alert record waiting to be read: the first datagram queued is one
// whose record header says content type 21. Peeked, not consumed, without
// blocking. (A peek shorter than the datagram fails with WSAEMSGSIZE on
// Windows, with the buffer filled.)
//
static bool PendingAlert(SOCKET sock)
{
  unsigned char head[1] = { 0 };
#ifdef _WIN32
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(sock, &readable);
  struct timeval now = { 0, 0 };
  if(select(0, &readable, nullptr, nullptr, &now) <= 0) {
    return false;
  }
  const int peeked = recv(sock, (char*)head, sizeof(head), MSG_PEEK);
  if(peeked == SOCKET_ERROR && WSAGetLastError() != WSAEMSGSIZE) {
    return false;
  }
#else
  const ssize_t peeked = recv(sock, head, sizeof(head), MSG_PEEK | MSG_DONTWAIT);
  if(peeked < 1) {
    return false;
  }
#endif
  return head[0] == 21;
}

//
// DTLS record I/O (see common.h). The context is the DtlsSocketCtx.
//
int DtlsBioSend(void* ctx, const unsigned char* buf, size_t len)
{
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)ctx;
  // An established session whose peer has sent an alert -- a close_notify, or
  // a fatal one -- is over: the write fails, and the alert stays queued for the
  // next read to report in full. On Windows this is also the only way a write
  // can learn the peer has gone: the network's refusal of a later datagram
  // waits behind that alert in the queue. (Not during the handshake, where
  // mbedTLS reads the peer's alerts itself.)
  if(DtlsHandshakeOver(&sctx->ssl) && PendingAlert((SOCKET)sctx->net.fd)) {
    sctx->net_reason = { 0, false };
    return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
  }

  const int refused = PendingRefusal((SOCKET)sctx->net.fd);
  if(refused) {
    sctx->net_reason = { refused, false };
    return MBEDTLS_ERR_NET_CONN_RESET;
  }

  const int ret = mbedtls_net_send(&sctx->net, buf, len);
  if(ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
    sctx->net_reason = { CurrentSocketError(), false };
  }
  return ret;
}

int DtlsBioRecv(void* ctx, unsigned char* buf, size_t len)
{
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)ctx;
  const int ret = mbedtls_net_recv(&sctx->net, buf, len);
  if(ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
    sctx->net_reason = { CurrentSocketError(), false };
  }
  return ret;
}

int DtlsBioRecvTimeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout)
{
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)ctx;
  const int ret = mbedtls_net_recv_timeout(&sctx->net, buf, len, timeout);
  if(ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_TIMEOUT) {
    sctx->net_reason = { CurrentSocketError(), false };
  }
  return ret;
}

bool TrapProcessor::SockUdpCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  const long port = (long)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance) {
    array = (size_t*)array[0];
    const std::string addr_str = UnicodeToBytes((wchar_t*)(array + 3));

    // the address is resolved, which can block: park, with the instance
    // re-rooted on the op_stack and re-read after
    SocketError error;
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    UdpEndpoint* ep = UdpOpen(addr_str.c_str(), static_cast<int>(port), error);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);

    socket_last_error = error;
    if(ep) {
      instance[0] = (size_t)ep->sock;
      instance[1] = (size_t)ep;
    }
  }
  else if(instance) {
    // no address
    RecordSocketError(SOCKET_INVALID_ARGUMENT);
  }

  return true;
}

//
// An open UDP socket: one that New or Bind opened, which set its endpoint too,
// and that Close has not closed. The traps tested the handle instead, three
// ways -- non-zero, above -1, above 0 -- and a socket whose New failed kept
// handle 0, so some reads and writes went to handle 0: stdin on POSIX.
//
static bool UdpSocketOpen(const size_t* instance)
{
  return instance && instance[1];
}

//
// UDPSocketServer->Bind(): the port in the third field, the address (Nil for
// every interface) in the fourth.
//
bool TrapProcessor::SockUdpBind(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  if(inst) {
    const long port = (long)inst[2];
    std::string host;
    const size_t* str_obj = (size_t*)inst[3];
    if(str_obj && str_obj[0]) {
      host = UnicodeToBytes((wchar_t*)((size_t*)str_obj[0] + 3));
    }

    // A name lookup can block: park, with the instance re-rooted on the
    // op_stack so a collection that moves it fixes the reference, and re-read
    // after. Not from frame->mem: JIT-compiled code calls traps without a
    // frame to read (see HasFrameDependentTrap).
    SocketError error;
    PushInt((size_t)inst, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    UdpEndpoint* ep = UdpBind(host, static_cast<int>(port), error);
    MemoryManager::EndBlocking();
    inst = (size_t*)PopInt(op_stack, stack_pos);

    socket_last_error = error;
    if(ep) {
      inst[0] = (size_t)ep->sock;
      inst[1] = (size_t)ep;
      // @port: the port bound, which for a port of 0 the system chose
      const int bound_port = BoundPort(ep->sock);
      if(bound_port > 0) {
        inst[2] = bound_port;
      }
    }
  }

  return true;
}

bool TrapProcessor::SockUdpClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  if(UdpSocketOpen(inst)) {
    UdpEndpoint* ep = (UdpEndpoint*)inst[1];
    // Forgotten once closed. The handle and the freed address were left in
    // place, so a second Close() deleted the address again (heap corruption),
    // and a read or write after Close() used a handle the system may have
    // given to something else.
    inst[0] = 0;
    inst[1] = 0;
    UdpClose(ep);
  }

  return true;
}

//
// UDP traps. A receive blocks until a datagram arrives and a send can block on
// a full send buffer, so each call is parked: unparked, a thread waiting here
// stalls every stop-the-world collection, and when the sender is a thread of
// this VM that needs a collection first, neither moves again. The endpoint is
// C heap, read from the instance before parking; a GC array is never handed
// to the kernel while parked (the collector can move a parked thread's young
// objects) -- reads land in a C buffer and are copied in after, with the array
// ref re-rooted on the op_stack so promotion fixup relocates it.
//
bool TrapProcessor::SockUdpInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(UdpSocketOpen(instance)) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    char value = '\0';
    int error;
    MemoryManager::BeginBlocking();
    const int read = UdpReceive(ep, &value, 1, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
    // the byte read, or 0 when nothing was
    PushInt(read > 0 ? (unsigned char)value : 0, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const int num = (int)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && UdpSocketOpen(instance) && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    char* temp = new char[num > 0 ? num : 1];
    int error;
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const int read = UdpReceive(ep, temp, num, error);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    RecordSocketError(error);
    if(read > 0) {
      memcpy((char*)(array + 3) + offset, temp, read);
    }
    delete[] temp;
    // the number of bytes read, or -1 on error
    PushInt(read < 0 ? -1 : read, op_stack, stack_pos);
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

  if(array && UdpSocketOpen(instance) && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    char* byte_buffer = new char[num * 2 + 1];
    int error;
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const int read = UdpReceive(ep, byte_buffer, static_cast<int>(num), error);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    RecordSocketError(error);
    wchar_t* buffer = (wchar_t*)(array + 3);
    // only terminate/decode when bytes were read
    if(read > -1) {
      byte_buffer[read] = '\0';
      std::wstring in(BytesToUnicode(byte_buffer));

      // copy and remove file BOM UTF (8, 16, 32)
      if(in.size() > 0 && IsByteOrderMark(in[0])) {
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

  if(array && UdpSocketOpen(instance) && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    const wchar_t* buffer = (wchar_t*)(array + 3);
    const std::string buffer_out = UnicodeToBytes(buffer);

    // buffer_out is C heap
    int error;
    MemoryManager::BeginBlocking();
    const int sent = UdpSend(ep, buffer_out.c_str(), (int)buffer_out.size(), error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
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
  if(UdpSocketOpen(instance)) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    int error;
    MemoryManager::BeginBlocking();
    const int sent = UdpSend(ep, &value, 1, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
    // true when the byte was sent
    PushInt(sent > 0, op_stack, stack_pos);
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

  if(array && UdpSocketOpen(instance) && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    // staged in a C buffer: the GC array can move while this thread is parked
    char* temp = new char[num > 0 ? num : 1];
    memcpy(temp, (const char*)(array + 3) + offset, num);

    int error;
    MemoryManager::BeginBlocking();
    const int sent = UdpSend(ep, temp, num, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
    delete[] temp;
    // the number of bytes sent, or -1 on error
    PushInt(sent < 0 ? -1 : sent, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockUdpInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && UdpSocketOpen(instance)) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
    char buffer[MID_BUFFER_MAX] = {0};
    int error;
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const int read = UdpReceive(ep, buffer, MID_BUFFER_MAX - 1, error);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    RecordSocketError(error);
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

  return true;
}

bool TrapProcessor::SockUdpOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) {
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(array && UdpSocketOpen(instance)) {
    UdpEndpoint* ep = (UdpEndpoint*)instance[1];
#ifdef _DEBUG
    std::wcout << L"# udp write std::string: instance=" << instance << L"(" << (size_t)instance << L")" << L"; array=" << array << L"(" << (size_t)array << L")" << std::endl;
#endif
    const std::string data = UnicodeToBytes((wchar_t*)(array + 3));
    // data is C heap. WriteString returns Nil, so nothing is pushed, as in
    // SockTcpOutString.
    int error;
    MemoryManager::BeginBlocking();
    UdpSend(ep, data.c_str(), (int)data.size(), error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
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

// method : ReadBoolArray() ~ Bool[]
//
// A Bool[] is a byte array (the emitter allocates one with NEW_BYTE_ARY), so
// Serializer->Write(Bool[]) now writes the byte-array encoding. It used to
// write the Int[] one -- array_size * 8 bytes from a one-byte-per-element
// array, a heap over-read into the output -- and ReadBoolArray read that back
// as an INT array posing as a Bool[]. That legacy encoding is still read: its
// first n payload bytes are the bools, and the rest (the over-read) is
// consumed and dropped, so the values after it stay aligned.
bool TrapProcessor::DeserlBoolAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  const ParamType tag = (ParamType)DeserializeInt(inst);
  if(tag == BYTE_ARY_PARM) {
    PushInt((size_t)DeserializeArray(BYTE_ARY_PARM, inst, op_stack, stack_pos), op_stack, stack_pos);
    return true;
  }

  if(tag == INT_ARY_PARM) {
    // neither allocation collects, so the int array needs no root in between
    size_t* legacy = DeserializeArray(INT_ARY_PARM, inst, op_stack, stack_pos);
    if(!legacy) {
      PushInt(0, op_stack, stack_pos);
      return true;
    }
    const size_t size = legacy[0];
    size_t* bools = MemoryManager::AllocateArray(size + 1 + ((1 + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
    bools[0] = size;
    bools[1] = 1;
    bools[2] = size;
    memcpy(bools + 3, legacy + 3, size);
    PushInt((size_t)bools, op_stack, stack_pos);
    return true;
  }

  PushInt(0, op_stack, stack_pos);
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
  return CompressZlibArray(array, Z_BEST_COMPRESSION, op_stack, stack_pos);
}

// function : CompressZlib(input : Byte[], level : Int) ~ Byte[]: zlib's
// levels, 0 (store) to 9 (smallest); -1 is zlib's default (6). Byte->CompressZlib
// without a level keeps level 9.
bool TrapProcessor::CompressZlibLevel(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE level = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(level < Z_DEFAULT_COMPRESSION || level > Z_BEST_COMPRESSION) {
    level = Z_DEFAULT_COMPRESSION;
  }
  return CompressZlibArray(array, (int)level, op_stack, stack_pos);
}

bool TrapProcessor::CompressZlibArray(size_t* array, int level, size_t* &op_stack, size_t* &stack_pos)
{
  if(!array) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::CompressZlib(in, in_len, out_len, level);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"zlib compression failed");
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::UncompressZlib(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid, truncated or oversized zlib data");
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::CompressGzip(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"gzip compression failed");
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::UncompressGzip(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid, truncated or oversized gzip data");
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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

bool TrapProcessor::CompressDeflateBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::CompressDeflate(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"deflate compression failed");
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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

bool TrapProcessor::UncompressDeflateBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // setup buffers
  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  uLong out_len;
  char* out = OutputStream::UncompressDeflate(in, in_len, out_len);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid, truncated or oversized deflate data");
  }

  // create character array
  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
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
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
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
    const NativePath filename = ToNativePath((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "rb");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << FromNativePath(filename) << L"'; instance=" << instance << L"("
	       << (size_t)instance << L")" << L"; addr=" << file << L"(" << (size_t)file << L") #" << std::endl;
#endif
    // look ahead one byte, as FileInString does after each line: an empty file
    // is then at end of file from the start, and ReadLine returns Nil rather
    // than one empty line (#1180)
    if(file) {
      const int next = fgetc(file);
      if(next != EOF) {
        ungetc(next, file);
      }
    }
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
    const NativePath filename = ToNativePath((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "ab");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << FromNativePath(filename) << L"'; instance=" << instance << L"("
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
    const NativePath filename = ToNativePath((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "wb");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << FromNativePath(filename) << L"'; instance=" << instance << L"("
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
    const NativePath filename = ToNativePath((wchar_t*)(array + 3));
    FILE* file = File::FileOpen(filename.c_str(), "w+b");
#ifdef _DEBUG
    std::wcout << L"# file open: name='" << FromNativePath(filename) << L"'; instance=" << instance << L"("
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
    // flush only: clearing instance[0] here (copied from Close) made the file
    // read as closed after a Flush -- later writes were dropped, Close did
    // nothing, and the FILE* leaked, keeping the file locked on Windows (#1180)
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
      if(in.size() > 0 && IsByteOrderMark(in[0])) {
        in.erase(in.begin(), in.begin() + 1);
      }

      // always room for the terminator: a line as long as the buffer would
      // otherwise trip MSVC's invalid-parameter handler (as in #1173)
      wchar_t* out = (wchar_t*)(array + 3);
      const size_t max = in.size() < array[0] ? in.size() : (array[0] > 0 ? array[0] - 1 : 0);
#ifdef _WIN32
      wcsncpy_s(out, array[0], in.c_str(), max);
#else
      wcsncpy(out, in.c_str(), max);
      out[max] = 0;
#endif

      // Look ahead one byte, so end of file is known as soon as the last line
      // has been read. Without it IsEoF() stayed false after the final newline,
      // and the documented `while(line <> Nil)` loop got one extra "" (#1180).
      const int next = fgetc(file);
      if(next != EOF) {
        ungetc(next, file);
      }
    }
  }

  return true;
}

// A new one-dimensional Char[] holding 'value'
static size_t* NewCharArray(const std::wstring& value, size_t* op_stack, size_t* stack_pos)
{
  const size_t size = value.size();
  size_t* array = MemoryManager::AllocateArray(size + 1 + ((1 + 2) * sizeof(size_t)), CHAR_ARY_TYPE, op_stack, *stack_pos, false);
  array[0] = size;
  array[1] = 1;
  array[2] = size;
  wchar_t* out = (wchar_t*)(array + 3);
  if(size > 0) {
    memcpy(out, value.c_str(), size * sizeof(wchar_t));
  }
  out[size] = L'\0';

  return array;
}

// The shortest decimal digits that read back as 'value' (finite, not zero),
// as a digit string and the decimal exponent of its first digit:
// 1.25e-05 gives "125" and -5.
static void ShortestDigits(FLOAT_VALUE value, std::string& digits, int& exponent)
{
  char buffer[64];
  const char* end = buffer;
#ifdef OBJECK_HAS_FLOAT_TO_CHARS
  // no precision argument: the shortest form that round-trips (C++17)
  const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value, std::chars_format::scientific);
  end = result.ptr;
  // to_chars does not terminate: atoi below must not read the stack's leftovers
  *result.ptr = '\0';
#else
  // the first precision whose correctly rounded %e reads back exactly is the
  // shortest length, and that rounding is the nearest such decimal: what
  // to_chars picks
  for(int precision = 0; precision < 17; ++precision) {
    const int size = snprintf(buffer, sizeof(buffer), "%.*e", precision, value);
    if(strtod(buffer, nullptr) == value || precision == 16) {
      end = buffer + size;
      break;
    }
  }
#endif

  // "-d.ddde+XX"
  digits.clear();
  const char* p = buffer;
  if(*p == '-') {
    ++p;
  }
  for(; p < end && *p != 'e'; ++p) {
    if(*p != '.') {
      digits.push_back(*p);
    }
  }
  exponent = (p < end) ? atoi(p + 1) : 0;
  while(digits.size() > 1 && digits.back() == '0') {
    digits.pop_back();
  }
}

// Float->ToShortString: the shortest text that reads back as the same value,
// in Python's repr layout -- fixed notation for exponents -4 to 15 (always
// with a fractional digit: "100.0"), otherwise "d.ddde-07"
static std::wstring FormatFloatShortest(FLOAT_VALUE value)
{
  if(std::isnan(value)) {
    return L"nan";
  }
  if(std::isinf(value)) {
    return value < 0.0 ? L"-inf" : L"inf";
  }

  std::string out;
  if(std::signbit(value)) {
    out.push_back('-');
  }
  if(value == 0.0) {
    out += "0.0";
    return std::wstring(out.begin(), out.end());
  }

  std::string digits;
  int exponent;
  ShortestDigits(value, digits, exponent);
  const int count = (int)digits.size();

  if(exponent >= -4 && exponent < 16) {
    if(exponent < 0) {
      out += "0.";
      out.append((size_t)(-exponent - 1), '0');
      out += digits;
    }
    else if(count <= exponent + 1) {
      out += digits;
      out.append((size_t)(exponent + 1 - count), '0');
      out += ".0";
    }
    else {
      out.append(digits, 0, (size_t)exponent + 1);
      out.push_back('.');
      out.append(digits, (size_t)exponent + 1, std::string::npos);
    }
  }
  else {
    out.push_back(digits[0]);
    if(count > 1) {
      out.push_back('.');
      out.append(digits, 1, std::string::npos);
    }
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "e%c%02d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
    out += suffix;
  }

  return std::wstring(out.begin(), out.end());
}

// function : FormatShort(value : Float) ~ Char[]
bool TrapProcessor::FloatToStrShort(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
  PushInt((size_t)NewCharArray(FormatFloatShortest(value), op_stack, stack_pos), op_stack, stack_pos);
  return true;
}

// Float->ToString(value, precision): printf's %.*f, exact at any magnitude.
// The library version went through ->As(Int), so above 2^63 it printed
// "-9223372036854775808.-9223372036854775808", and it lost the sign of -0.0
// (#1173).
bool TrapProcessor::FloatToStrPrec(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  INT64_VALUE precision = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);

  // 1074 digits are enough to write the smallest subnormal exactly
  if(precision < 0) {
    precision = 0;
  }
  else if(precision > 1074) {
    precision = 1074;
  }

  std::wstring out;
  if(std::isnan(value)) {
    out = L"nan";
  }
  else if(std::isinf(value)) {
    out = value < 0.0 ? L"-inf" : L"inf";
  }
  else {
    const int prec = (int)precision;
    const int size = snprintf(nullptr, 0, "%.*f", prec, value);
    if(size > 0) {
      std::vector<char> buffer((size_t)size + 1);
      snprintf(buffer.data(), buffer.size(), "%.*f", prec, value);
      out.assign(buffer.begin(), buffer.begin() + size);
    }
  }

  PushInt((size_t)NewCharArray(out, op_stack, stack_pos), op_stack, stack_pos);
  return true;
}

// FileReader->ReadLine(): the whole line, however long. FILE_IN_STRING reads
// into the caller's buffer with fgets, so a line longer than 8 KB came back
// as several lines (#1180). Returns Nil at end of file or for a closed reader.
bool TrapProcessor::FileInLine(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  FILE* file = instance ? (FILE*)instance[0] : nullptr;
  if(!file) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  std::string line;
  bool read_any = false;
  int c;
  while((c = fgetc(file)) != EOF) {
    read_any = true;
    if(c == '\n') {
      break;
    }
    line.push_back((char)c);
  }

  if(!read_any) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  // "\r\n" endings, as FILE_IN_STRING strips them
  if(!line.empty() && line.back() == '\r') {
    line.pop_back();
  }

  // a byte-order mark, as FILE_IN_STRING removes it
  std::wstring in = BytesToUnicode(line);
  if(in.size() > 0 && (in[0] == (wchar_t)0xFEFF || in[0] == (wchar_t)0xFFFE)) {
    in.erase(in.begin(), in.begin() + 1);
  }

  // look ahead one byte, so IsEoF() is true as soon as the last line is read
  const int next = fgetc(file);
  if(next != EOF) {
    ungetc(next, file);
  }

  PushInt((size_t)NewCharArray(in, op_stack, stack_pos), op_stack, stack_pos);
  return true;
}

//
// System.Concurrency.Condition (#1186)
//
// The wait state lives on the C++ heap, not in the Condition object: a young
// object is moved when it is promoted, and a thread blocked on a condition
// variable holds that variable's address. The object keeps only the pointer.
// Like a ThreadMutex's critical section, it is never freed.
struct ConditionVariable {
  std::mutex lock;
  std::condition_variable cond;
};

#ifdef _WIN32
typedef CRITICAL_SECTION NativeThreadMutex;
#else
typedef pthread_mutex_t NativeThreadMutex;
#endif

void ThreadMutexCreate(size_t* instance)
{
  // like the critical section it replaces, never freed
  NativeThreadMutex* mutex = new NativeThreadMutex;
#ifdef _WIN32
  InitializeCriticalSection(mutex);
#else
  pthread_mutex_init(mutex, nullptr);
#endif
  instance[1] = (size_t)mutex;
}

void ThreadMutexLock(size_t* instance)
{
  NativeThreadMutex* mutex = (NativeThreadMutex*)instance[1];
  if(!mutex) {
    return;
  }
#ifdef _WIN32
  if(!TryEnterCriticalSection(mutex)) {
    MemoryManager::BeginBlocking();
    EnterCriticalSection(mutex);
    MemoryManager::EndBlocking();
  }
#else
  if(pthread_mutex_trylock(mutex) != 0) {
    MemoryManager::BeginBlocking();
    pthread_mutex_lock(mutex);
    MemoryManager::EndBlocking();
  }
#endif
}

void ThreadMutexUnlock(size_t* instance)
{
  NativeThreadMutex* mutex = (NativeThreadMutex*)instance[1];
  if(!mutex) {
    return;
  }
#ifdef _WIN32
  LeaveCriticalSection(mutex);
#else
  pthread_mutex_unlock(mutex);
#endif
}

// The error a Try() region last recovered from, on this thread (#1247): its
// message (Nil when there is none), its kind, and clearing it.
bool TrapProcessor::RuntimeGetError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const Runtime::StackInterpreter::RecoveredErrorState& state = Runtime::StackInterpreter::RecoveredError();
  if(state.kind == Runtime::StackInterpreter::RECOVERED_NONE) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    PushInt((size_t)CreateStringObject(state.message, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  return true;
}

bool TrapProcessor::RuntimeGetErrorKind(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  PushInt((size_t)Runtime::StackInterpreter::RecoveredError().kind, op_stack, stack_pos);
  return true;
}

bool TrapProcessor::RuntimeClearError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  Runtime::StackInterpreter::RecoveredErrorState& state = Runtime::StackInterpreter::RecoveredError();
  state.kind = Runtime::StackInterpreter::RECOVERED_NONE;
  state.message.clear();
  Runtime::StackInterpreter::RaisedError().object = nullptr;
  return true;
}

// function : RaiseNative(e : Error, message : String) ~ Nil (#1247). Never
// returns: under a ?-> handler the error is recorded and the region recovers;
// with none, it is reported and the program ends, as a runtime fault does.
bool TrapProcessor::RuntimeRaise(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* message_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* error = (size_t*)PopInt(op_stack, stack_pos);
  if(!error) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  // a try block raising again passes no message: Error's @message, field 0
  if(!message_obj) {
    message_obj = (size_t*)error[0];
  }
  std::wstring message;
  if(message_obj && message_obj[0]) {
    message = (wchar_t*)((size_t*)message_obj[0] + 3);
  }

  if(Runtime::StackInterpreter::ThreadHasTryHandler()) {
    Runtime::StackInterpreter::RecordRecoveredError(Runtime::StackInterpreter::RECOVERED_RAISED, message);
    Runtime::StackInterpreter::SetRaisedErrorObject(error);
    Runtime::StackInterpreter::TrapErrorRecorded() = true;
  }
  else {
    StackClass* cls = MemoryManager::GetClass(error);
    std::wcerr << L">>> Unhandled " << (cls ? cls->GetName() : L"System.Error") << L": " << message << L" <<<" << std::endl;
  }
  return false;
}

// function : GetLastError() ~ Error (#1247): the Error a ?-> region on this
// thread last recovered from -- the one raised, or, for a runtime fault, an
// instance of its built-in subclass made on first request -- or Nil
bool TrapProcessor::RuntimeGetLastError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const Runtime::StackInterpreter::RecoveredErrorState& state = Runtime::StackInterpreter::RecoveredError();
  Runtime::StackInterpreter::RaisedErrorState& raised = Runtime::StackInterpreter::RaisedError();
  if(state.kind == Runtime::StackInterpreter::RECOVERED_NONE) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  if(!raised.object) {
    const wchar_t* name;
    switch(state.kind) {
    case Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE:
      name = L"System.NilError";
      break;

    case Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS:
      name = L"System.IndexError";
      break;

    case Runtime::StackInterpreter::RECOVERED_DIVIDE_BY_ZERO:
      name = L"System.DivideByZeroError";
      break;

    case Runtime::StackInterpreter::RECOVERED_INVALID_CAST:
      name = L"System.CastError";
      break;

    default:
      name = L"System.RuntimeError";
      break;
    }

    // A program links only the library classes it names. One that never names
    // DivideByZeroError cannot test for it either, so System.Error stands in.
    size_t* error = MemoryManager::AllocateObject(name, op_stack, *stack_pos, false);
    if(!error) {
      error = MemoryManager::AllocateObject(L"System.Error", op_stack, *stack_pos, false);
    }
    if(!error) {
      PushInt(0, op_stack, stack_pos);
      return true;
    }
    // Error's one field, @message
    error[0] = (size_t)CreateStringObject(state.message, program, op_stack, stack_pos);
    Runtime::StackInterpreter::SetRaisedErrorObject(error);
  }

  PushInt((size_t)raised.object, op_stack, stack_pos);
  return true;
}

//
// Bulk primitive array moves: Int/Float/Byte/Char->Copy and ->Fill. The
// element kind comes from the library wrapper, which knows the static type;
// no header word is consulted, so an array is never misread.
//
enum AryKind : INT64_VALUE {
  ARY_KIND_BYTE = 1,
  ARY_KIND_CHAR,
  ARY_KIND_INT,
  ARY_KIND_FLOAT
};

static size_t AryKindWidth(INT64_VALUE kind)
{
  switch(kind) {
  case ARY_KIND_BYTE:
    return sizeof(char);
  case ARY_KIND_CHAR:
    return sizeof(wchar_t);
  case ARY_KIND_INT:
    return sizeof(size_t);
  case ARY_KIND_FLOAT:
    return sizeof(FLOAT_VALUE);
  default:
    return 0;
  }
}

// offset and count name a range inside an array of 'size' elements; written
// so that no sum can overflow
static bool AryRangeValid(INT64_VALUE offset, INT64_VALUE count, INT64_VALUE size)
{
  return offset >= 0 && count >= 0 && offset <= size && count <= size - offset;
}

static std::wstring AryRangeMessage(const wchar_t* what, INT64_VALUE offset, INT64_VALUE count, INT64_VALUE size)
{
  std::wstringstream out;
  out << L"Index out of bounds: " << what << L" offset " << offset << L" and count " << count
      << L" do not fit an array of " << size;
  return out.str();
}

// function : CopyNative(dest : T[], dest_offset : Int, src : T[], src_offset : Int, count : Int, kind : Int) ~ Nil
//
// Overlapping ranges in one array behave like memmove. Runtime->Copy, the old
// form, answers false for a bad range; this raises an IndexError, so a caller
// cannot lose a failed copy by ignoring a Bool.
bool TrapProcessor::AryCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE kind = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE count = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE src_offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* src = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE dest_offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* dest = (size_t*)PopInt(op_stack, stack_pos);

  if(!src || !dest) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  const size_t width = AryKindWidth(kind);
  if(!width) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid array copy");
  }

  const INT64_VALUE src_size = (INT64_VALUE)src[0];
  const INT64_VALUE dest_size = (INT64_VALUE)dest[0];
  if(!AryRangeValid(src_offset, count, src_size)) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, AryRangeMessage(L"source", src_offset, count, src_size));
  }
  if(!AryRangeValid(dest_offset, count, dest_size)) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, AryRangeMessage(L"destination", dest_offset, count, dest_size));
  }

  if(count > 0) {
    char* dest_ptr = (char*)(dest + 3) + (size_t)dest_offset * width;
    const char* src_ptr = (const char*)(src + 3) + (size_t)src_offset * width;
    memmove(dest_ptr, src_ptr, (size_t)count * width);
    // an Int[] slot may hold an object reference (object arrays are Int arrays
    // of pointers), so record the destination for the minor collector
    if(kind == ARY_KIND_INT) {
      MemoryManager::WriteBarrier(dest);
    }
  }

  return true;
}

// function : FillNative(array : Int[]/Byte[]/Char[], value, offset : Int, count : Int, kind : Int) ~ Nil
bool TrapProcessor::AryFillInt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE kind = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE count = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t value = PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);

  if(!array) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  const INT64_VALUE size = (INT64_VALUE)array[0];
  if(!AryRangeValid(offset, count, size)) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, AryRangeMessage(L"fill", offset, count, size));
  }

  switch(kind) {
  case ARY_KIND_BYTE: {
    char* start = (char*)(array + 3) + offset;
    memset(start, (int)(unsigned char)value, (size_t)count);
  }
    break;

  case ARY_KIND_CHAR: {
    wchar_t* start = (wchar_t*)(array + 3) + offset;
    std::fill(start, start + count, (wchar_t)value);
  }
    break;

  case ARY_KIND_INT: {
    size_t* start = array + 3 + offset;
    std::fill(start, start + count, value);
  }
    break;

  default:
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid array fill");
  }

  return true;
}

// function : FillNative(array : Float[], value : Float, offset : Int, count : Int) ~ Nil
bool TrapProcessor::AryFillFloat(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE count = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const FLOAT_VALUE value = PopFloat(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);

  if(!array) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  const INT64_VALUE size = (INT64_VALUE)array[0];
  if(!AryRangeValid(offset, count, size)) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, AryRangeMessage(L"fill", offset, count, size));
  }

  FLOAT_VALUE* start = (FLOAT_VALUE*)(array + 3) + offset;
  std::fill(start, start + count, value);

  return true;
}

// Int/Float->ToBytes and Byte->ToInts/ToFloats copy element bytes as they lie
// in memory and document them as little-endian. Every supported target (x64,
// arm64; Windows, Linux, macOS) is; a big-endian port must byte-swap here.
// std::endian needs C++20, which not every build here compiles as (macOS's Xcode project), so
// the check uses the compilers' own byte-order macros; every Windows target is little-endian.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "ARY_TO_BYTES/BYTES_TO_ARY assume a little-endian host");
#elif !defined(_WIN32)
#error "ARY_TO_BYTES/BYTES_TO_ARY need the host's byte order; add a check for this compiler"
#endif

// function : ToBytesNative(values : Int[]/Float[], offset : Int, count : Int, kind : Int) ~ Byte[]
// 'offset' and 'count' are in elements; the bytes are count * 8, little-endian
bool TrapProcessor::AryToBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE kind = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE count = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* values = (size_t*)PopInt(op_stack, stack_pos);

  if(!values) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }
  if(kind != ARY_KIND_INT && kind != ARY_KIND_FLOAT) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid array conversion");
  }

  const INT64_VALUE size = (INT64_VALUE)values[0];
  if(!AryRangeValid(offset, count, size)) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, AryRangeMessage(L"ToBytes", offset, count, size));
  }

  const size_t width = AryKindWidth(kind);
  const size_t byte_count = (size_t)count * width;
  size_t* bytes = MemoryManager::AllocateArray(byte_count + 1 + ((1 + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  bytes[0] = byte_count;
  bytes[1] = 1;
  bytes[2] = byte_count;
  if(byte_count) {
    memcpy(bytes + 3, (const char*)(values + 3) + (size_t)offset * width, byte_count);
  }

  PushInt((size_t)bytes, op_stack, stack_pos);
  return true;
}

// function : IntsNative/FloatsNative(bytes : Byte[], offset : Int, count : Int, kind : Int) ~ Int[]/Float[]
// 'offset' is in bytes and 'count' in elements; a count of -1 takes every
// byte from 'offset' on, which must then be a whole number of elements
bool TrapProcessor::BytesToAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE kind = (INT64_VALUE)PopInt(op_stack, stack_pos);
  INT64_VALUE count = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* bytes = (size_t*)PopInt(op_stack, stack_pos);

  if(!bytes) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }
  if(kind != ARY_KIND_INT && kind != ARY_KIND_FLOAT) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid array conversion");
  }

  const INT64_VALUE width = (INT64_VALUE)AryKindWidth(kind);
  const INT64_VALUE size = (INT64_VALUE)bytes[0];
  if(offset < 0 || offset > size) {
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, AryRangeMessage(L"byte", offset, count, size));
  }
  if(count == -1) {
    if((size - offset) % width) {
      std::wstringstream message;
      message << L"Byte count " << (size - offset) << L" is not a whole number of " << width << L"-byte values";
      return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, message.str());
    }
    count = (size - offset) / width;
  }
  if(count < 0 || count > (size - offset) / width) {
    std::wstringstream message;
    message << L"Index out of bounds: " << count << L" values of " << width << L" bytes from byte offset " << offset
            << L" do not fit an array of " << size << L" bytes";
    return TrapError(Runtime::StackInterpreter::RECOVERED_INDEX_OUT_OF_BOUNDS, message.str());
  }

  size_t* values = MemoryManager::AllocateArray((size_t)count + 1 + 2, kind == ARY_KIND_INT ? INT_TYPE : FLOAT_TYPE,
                                                op_stack, *stack_pos, false);
  values[0] = (size_t)count;
  values[1] = 1;
  values[2] = (size_t)count;
  if(count) {
    memcpy(values + 3, (const char*)(bytes + 3) + offset, (size_t)(count * width));
  }

  PushInt((size_t)values, op_stack, stack_pos);
  return true;
}

// function : Create() ~ Int
bool TrapProcessor::CondNew(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  PushInt((size_t)new ConditionVariable, op_stack, stack_pos);
  return true;
}

// method : WaitNative(mutex : ThreadMutex, millis : Int) ~ Bool
//
// The caller holds 'mutex' (it is inside critical(mutex)). It is released only
// once the condition's own lock is held, and a signal takes that lock, so a
// signal sent after the release cannot be lost. While waiting the thread is
// parked, so other threads' collections run; both native locks are on the C++
// heap, so the objects moving does not matter.
//
// Returns false if 'millis' (when not negative) passed without a signal. Like
// any condition variable it may also return early, so callers wait in a loop.
bool TrapProcessor::CondWait(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE millis = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* mutex = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  ConditionVariable* condition = instance ? (ConditionVariable*)instance[0] : nullptr;
  if(!condition || !mutex) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  // copy the lock's address: the object holding it can move once we park
  size_t native_mutex[2] = { 0, mutex[1] };

  bool signaled = true;
  std::unique_lock<std::mutex> guard(condition->lock);
  ThreadMutexUnlock(native_mutex);
  MemoryManager::BeginBlocking();
  if(millis < 0) {
    condition->cond.wait(guard);
  }
  else {
    signaled = condition->cond.wait_for(guard, std::chrono::milliseconds(millis)) == std::cv_status::no_timeout;
  }
  guard.unlock();
  MemoryManager::EndBlocking();

  ThreadMutexLock(native_mutex);

  PushInt(signaled ? 1 : 0, op_stack, stack_pos);
  return true;
}

// method : SignalNative(all : Bool) ~ Nil
bool TrapProcessor::CondSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const bool all = PopInt(op_stack, stack_pos) != 0;
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  ConditionVariable* condition = instance ? (ConditionVariable*)instance[0] : nullptr;
  if(condition) {
    std::lock_guard<std::mutex> guard(condition->lock);
    if(all) {
      condition->cond.notify_all();
    }
    else {
      condition->cond.notify_one();
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
    const std::wstring filename = std::wstring(L"\\\\.\\pipe\\") + name;
    HANDLE pipe;
#else
    const std::string filename = "/tmp/" + UnicodeToBytes(name);
    int pipe;
#endif

    // Creating the pipe waits for a client to connect; park for it, as accept()
    // does. The instance ref is re-rooted on the op_stack and re-read after.
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const bool created = Pipe::Create(filename.c_str(), pipe);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);
    if(created) {
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
    const std::wstring filename = std::wstring(L"\\\\.\\pipe\\") + name;
    HANDLE pipe;
#else
    const std::string filename = "/tmp/" + UnicodeToBytes(name);
    int pipe;
#endif

    // Opening can wait for a busy server (WaitNamedPipe); park for it. The
    // instance ref is re-rooted on the op_stack and re-read after.
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const bool opened = Pipe::Open(filename.c_str(), pipe);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);
    if(opened) {
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

    // blocking read; park so a stop-the-world collection elsewhere can proceed
    MemoryManager::BeginBlocking();
    const bool read = Pipe::ReadByte(value, pipe);
    MemoryManager::EndBlocking();
    if(read) {
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
#else
    int pipe = (int)instance[0];
#endif
    // a write blocks while the pipe is full; park for it
    MemoryManager::BeginBlocking();
    const bool written = Pipe::WriteByte(value, pipe);
    MemoryManager::EndBlocking();
    PushInt(written, op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    // blocking read; park for the duration. Bytes land in a temp C buffer (a
    // parked thread's GC array can be moved by promotion mid-read), and the
    // array ref is re-rooted on the op_stack so fixup relocates it.
    char* temp = new char[num > 0 ? num : 1];
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const size_t read = Pipe::ReadByteArray(temp, 0, num, pipe);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    // a failed POSIX recv() comes back as (size_t)-1
    if(read <= (size_t)num) {
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

bool TrapProcessor::PipeInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const INT64_VALUE num = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (INT64_VALUE)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && (FILE*)instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif

    // read from pipe
    char* byte_buffer = new char[num * 2 + 1];
    // offset 0: the temp buffer is sized for 'num'; offset belongs to the
    // destination, not this buffer (see FileInCharAry heap-overflow fix).
    // Blocking read: park for the duration, with the array ref re-rooted on the
    // op_stack so GC promotion fixup relocates it while we wait.
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    size_t read = Pipe::ReadByteArray(byte_buffer, 0, num, pipe);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    wchar_t* out = (wchar_t*)(array + 3);
    // a failed POSIX recv() comes back as (size_t)-1, which indexed far past
    // the buffer here; read nothing instead
    if(read > (size_t)num) {
      read = 0;
    }
    byte_buffer[read] = '\0';
    std::wstring in(BytesToUnicode(byte_buffer));

    // copy and remove file BOM UTF (8, 16, 32)
    if(in.size() > 0 && IsByteOrderMark(in[0])) {
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
    // a write blocks while the pipe is full; park for it. Data is staged in a
    // temp C buffer first -- a parked thread's GC array can be moved.
    char* temp = new char[num > 0 ? num : 1];
    memcpy(temp, (const char*)(array + 3) + offset, num);
    MemoryManager::BeginBlocking();
    const size_t written = Pipe::WriteByteArray(temp, 0, num, pipe);
    MemoryManager::EndBlocking();
    delete[] temp;
    PushInt(written, op_stack, stack_pos);
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
    // a write blocks while the pipe is full; park (buffer_out is C heap)
    MemoryManager::BeginBlocking();
    const size_t written = Pipe::WriteByteArray(buffer_out.c_str(), 0, buffer_out.size(), pipe);
    MemoryManager::EndBlocking();
    PushInt(written, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::PipeInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  if(array && instance && instance[0]) {
#ifdef _WIN32
    HANDLE pipe = (HANDLE)instance[0];
#else
    int pipe = (int)instance[0];
#endif
    // the line read blocks on the peer; park for it, with the array ref
    // re-rooted on the op_stack and re-read after the wait
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    std::string buffer = Pipe::ReadString(pipe);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    
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
    // a write blocks while the pipe is full; park (output is C heap)
    MemoryManager::BeginBlocking();
    Pipe::WriteString(output, pipe);
    MemoryManager::EndBlocking();
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
    int error;
    // blocking read; park so a stop-the-world collection elsewhere can proceed
    MemoryManager::BeginBlocking();
    const char value = IPSocket::ReadByte(sock, status, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
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
    int error;
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const int read = IPSocket::ReadBytes(temp, (int)num, sock, error);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    RecordSocketError(error);
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
    int error;
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    int read = IPSocket::ReadBytes(byte_buffer, static_cast<int>(num), sock, error);
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);
    RecordSocketError(error);
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
    int error;
    MemoryManager::BeginBlocking();
    const int sent = IPSocket::WriteByte((char)value, sock, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
    // true when the byte was sent, as documented (this pushed true whatever happened)
    PushInt(sent > 0 ? 1 : 0, op_stack, stack_pos);
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
    int error;
    MemoryManager::BeginBlocking();
    const int sent = IPSocket::WriteBytes(temp, (int)num, sock, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
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
    int error;
    MemoryManager::BeginBlocking();
    const int sent = IPSocket::WriteBytes(buffer_out.c_str(), (int)buffer_out.size(), sock, error);
    MemoryManager::EndBlocking();
    RecordSocketError(error);
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
  // a socket whose connect failed has no context: there is nothing to read
  // or write (this used it, and crashed), and its reason stays reported
  if(instance && instance[0]) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    ClearTlsError();
    int status;
    // blocking read; park so a stop-the-world collection elsewhere can proceed
    MemoryManager::BeginBlocking();
    const char value = IPSecureSocket::ReadByte(sctx, status);
    MemoryManager::EndBlocking();
    PushInt(value, op_stack, stack_pos);
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

  // no context: the connect failed (see SockTcpSslInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    ClearTlsError();

    // blocking read; park for the duration. Bytes land in a temp C buffer (a
    // parked thread's GC array can be moved by promotion mid-read), and the
    // array ref is re-rooted on the op_stack so fixup relocates it.
    char* temp = new char[num > 0 ? num : 1];
    int status; int read = 0;
    bool done = false;
    bool failed = false;
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    for(long i = 0; !done && i < num; ++i) {
      temp[i] = IPSecureSocket::ReadByte(sctx, status);
      if(!status) {
        done = true;
      }
      else if(status < 0) {
        done = failed = true;
      }
      ++read;
    }
    MemoryManager::EndBlocking();
    array = (size_t*)PopInt(op_stack, stack_pos);

    if(failed) {
      PushInt(-1, op_stack, stack_pos);
    }
    else {
      memcpy((char*)(array + 3) + offset, temp, read);
      PushInt(read, op_stack, stack_pos);
    }
    delete[] temp;
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

  // no context: the connect failed (see SockTcpSslInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    ClearTlsError();
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry heap-overflow fix).
    // Blocking read: park for the duration, with the array ref re-rooted on the
    // op_stack so GC promotion fixup relocates it while we wait.
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    int read = IPSecureSocket::ReadBytes(byte_buffer, static_cast<int>(num), sctx);
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

bool TrapProcessor::SockTcpSslOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  // a socket whose connect failed has no context: there is nothing to read
  // or write (this used it, and crashed), and its reason stays reported
  if(instance && instance[0]) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    ClearTlsError();
    // a TLS write can block on a full send buffer; park so STW GC can proceed
    MemoryManager::BeginBlocking();
    const bool written = IPSecureSocket::WriteByte((char)value, sctx);
    MemoryManager::EndBlocking();
    PushInt(written ? 1 : 0, op_stack, stack_pos);
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

  // no context: the connect failed (see SockTcpSslInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    ClearTlsError();
    // a TLS write can block on a full send buffer; park for the duration. Data
    // is staged in a temp C buffer first -- a parked thread's GC array can be
    // moved (and its young-gen slot reused) by promotion mid-send.
    char* temp = new char[num > 0 ? num : 1];
    memcpy(temp, (char*)(array + 3) + offset, num);
    MemoryManager::BeginBlocking();
    const int sent = IPSecureSocket::WriteBytes(temp, static_cast<int>(num), sctx);
    MemoryManager::EndBlocking();
    delete[] temp;
    PushInt(sent, op_stack, stack_pos);
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

  // no context: the connect failed (see SockTcpSslInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    SecureSocketCtx* sctx = (SecureSocketCtx*)instance[0];
    ClearTlsError();
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    const std::wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    std::string buffer_out = UnicodeToBytes(sub_buffer);
    // a TLS write can block on a full send buffer; park (buffer_out is C heap)
    MemoryManager::BeginBlocking();
    const int sent = IPSecureSocket::WriteBytes(buffer_out.c_str(), (int)buffer_out.size(), sctx);
    MemoryManager::EndBlocking();
    PushInt(sent, op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

// A response's :status pseudo-header, which RFC 9110 makes three digits. The
// HTTP/2 callback read it with an unguarded std::stoi, so a server that sent
// anything else threw through nghttp2's C frames and ended the VM; the HTTP/3
// one caught the throw but kept a partial parse ("2xx" was 2). Text that is not
// a status is 0, the value a response without one has always reported.
static inline int ParseHttpStatus(const std::string& v) {
  if(v.size() != 3) {
    return 0;
  }
  int status = 0;
  for(const char c : v) {
    if(c < '0' || c > '9') {
      return 0;
    }
    status = status * 10 + (c - '0');
  }
  return status;
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
      ctx->response_status = ParseHttpStatus(v);
    }
    else {
      ctx->response_headers[k] = std::move(v);
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
    // connect(), the TLS handshake and the SETTINGS write block on the peer;
    // park for the duration, as SockTcpSslConnect does. Unparked, a thread
    // waiting here stalls every stop-the-world collection, and when the peer
    // is a thread of this VM that is waiting for that collection, neither
    // moves again. Only C memory (ctx, host) is touched while parked; the
    // instance ref is re-rooted on the op_stack and re-read after the wait.
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const bool is_open = IPSecureSocket::OpenH2(host.c_str(), port, pem_file, ctx->tls);
    if(is_open) {
      ctx->session = h2_make_session(ctx);

      nghttp2_settings_entry iv[1] = {{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100}};
      nghttp2_submit_settings(ctx->session, NGHTTP2_FLAG_NONE, iv, 1);
      nghttp2_session_send(ctx->session);
    }
    else {
      delete ctx;
      ctx = nullptr;
    }
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);

    instance[0] = (size_t)ctx;
  }
#else
  if(instance) instance[0] = 0;
  std::wcerr << L">>> HTTP/2 not available: build with OBJECK_HAS_NGHTTP2 <<<" << std::endl;
#endif
  return true;
}


// Reads a String[] of alternating key/value into a header map.
//
// The array has already been popped off the op stack, so it is no longer a GC
// root -- every string is copied into std::string immediately, before any
// allocation can move the young generation.
//
// Deliberately does not follow lib_api.h's String[] walker, which bounds-checks
// against [0] while iterating to [2] and dereferences elements with no null
// check. Anything malformed or invalid is skipped rather than trusted.
static void h3_read_header_array(size_t* hdr_array,
                                 std::map<std::string, std::string>& out)
{
  if(!hdr_array) {
    return;
  }
  // NOTE: no [0] indirection here. That step belongs to a String *object*
  // (which holds its char array at [0]); an array object is indexed directly,
  // as SysCmdOut does with env_array[i + 3]. Applying it here read garbage and
  // crashed the VM.
  if(hdr_array[1] != 1) {   // dim: the +2 offset below assumes one dimension
    return;
  }

  size_t count = hdr_array[2];
  if(count > 128) {         // bound caller-controlled work
    count = 128;
  }
  count &= ~(size_t)1;      // odd length would read past the last pair

  size_t* elems = hdr_array + hdr_array[1] + 2;
  for(size_t i = 0; i + 1 < count; i += 2) {
    size_t* key_obj = (size_t*)elems[i];
    size_t* val_obj = (size_t*)elems[i + 1];
    if(!key_obj || !val_obj) {
      continue;
    }
    size_t* key_chars = (size_t*)key_obj[0];
    size_t* val_chars = (size_t*)val_obj[0];
    if(!key_chars || !val_chars) {
      continue;
    }
    const std::string key = UnicodeToBytes((wchar_t*)(key_chars + 3));
    const std::string val = UnicodeToBytes((wchar_t*)(val_chars + 3));
    // Objeck-side AddHeader already refuses these; re-checked here because the
    // native side must never trust a caller-supplied array.
    if(h3_valid_header_name(key) && h3_valid_header_value(val)) {
      out[key] = val;
    }
  }
}

bool TrapProcessor::Http2RequestHdrs(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  // headers were pushed LAST, so they pop FIRST; the remaining five are
  // exactly what Http2Request expects to find.
  std::map<std::string, std::string> hdrs;
  h3_read_header_array((size_t*)PopInt(op_stack, stack_pos), hdrs);
  return Http2Request(program, inst, op_stack, stack_pos, frame, &hdrs);
}

bool TrapProcessor::Http3RequestHdrs(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  std::map<std::string, std::string> hdrs;
  h3_read_header_array((size_t*)PopInt(op_stack, stack_pos), hdrs);
  return Http3Request(program, inst, op_stack, stack_pos, frame, &hdrs);
}

bool TrapProcessor::Http2Request(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame,
                                 const std::map<std::string, std::string>* extra_headers)
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
  // Caller headers, when the _HDRS trap supplied them. Set before the
  // request is built so both backends pick them up from the map they
  // already consume.
  if(ctx && extra_headers) {
    ctx->request_headers = *extra_headers;
  }

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
      const std::string ctype_val = UnicodeToBytes((wchar_t*)(ctype_array + 3));
      // Caller-supplied: refuse anything that could add a header line of its
      // own, or that a downstream h2/h3 -> HTTP/1.1 gateway would re-emit as
      // one. Both libraries validate nothing on submit.
      if(!h3_valid_header_value(ctype_val)) {
        PushInt(0, op_stack, stack_pos); return true;
      }
      hdr_keys.push_back("content-type");
      hdr_vals.push_back(ctype_val);
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

  // The send/recv loop blocks on the peer; park for it (see Http2Connect). Its
  // callbacks touch only ctx (C heap) and body_data (a C++ copy); the instance
  // ref is re-rooted on the op_stack and re-read after the wait.
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  const bool completed = h2_run_loop(ctx);
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!completed) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

  // Store results in instance fields [4]=status, [5]=body, [6]=content_type
  const std::string ct = ctx->response_headers.count("content-type")
                          ? ctx->response_headers["content-type"] : "";

  instance[4] = (size_t)ctx->response_status;

  // Allocate Byte[] for the response body. Layout:
  //   [0] = element count, [1] = dims, [2] = size of dim 0, [3+] = data;
  //   the allocation carries one extra NUL byte that is NOT counted in [0].
  const size_t body_size = ctx->response_body.size();
  const size_t body_dim  = 1;
  size_t* body_obj = MemoryManager::AllocateArray(
      body_size + 1 + ((body_dim + 2) * sizeof(size_t)),
      instructions::BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  body_obj[0] = body_size;
  body_obj[1] = body_dim;
  body_obj[2] = body_size;
  if(body_size > 0) {
    memcpy((uint8_t*)(body_obj + 3), ctx->response_body.data(), body_size);
  }
  instance[5] = (size_t)body_obj;
  instance[6] = (size_t)CreateStringObject(BytesToUnicode(ct), program, op_stack, stack_pos);
  // Write barrier: `instance` is the caller's response object, which may well be
  // old by the time a request completes, and the content-type String is young.
  MemoryManager::WriteBarrier(instance);

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
    // the destructor's close_notify is a write that can block; park for it
    MemoryManager::BeginBlocking();
    delete ctx;
    MemoryManager::EndBlocking();
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

// Connection IDs and the stateless reset token are secrets that go out in
// cleartext QUIC headers. A failed CSPRNG call can leave the buffer untouched,
// so an unchecked call publishes whatever was on the stack -- a memory
// disclosure and a predictable CID. Every call site checks, and a failure
// fails the operation.
static bool h3_random_bytes(void* dest, size_t destlen) {
  return RAND_bytes((uint8_t*)dest, destlen) == 1;
}

static void h3_rand_cb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx*) {
  if(!h3_random_bytes(dest, destlen)) {
    // this callback cannot signal failure; zero rather than leak stack bytes
    memset(dest, 0, destlen);
  }
}

static int h3_get_new_connection_id_cb(ngtcp2_conn*, ngtcp2_cid* cid,
                                        uint8_t* token, size_t cidlen, void*) {
  if(!h3_random_bytes(cid->data, cidlen) ||
     !h3_random_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN)) {
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  cid->datalen = cidlen;
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
    ctx->response_status = ParseHttpStatus(v);
  } else {
    ctx->response_headers[k] = std::move(v);
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

    // vec must outlive the block below: vec_ptr is handed to
    // ngtcp2_conn_writev_stream after it, so a block-scoped array would be
    // read after its lifetime ended. It worked only because nothing had
    // reused that stack yet (Coverity CID 1678917, RETURN_LOCAL).
    nghttp3_vec vec[16];
    if(ctx->h3conn) {
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
    if(nwrite == 0) {
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

// Opens a QUIC connection and drives the handshake until the HTTP/3 session
// is ready. Returns nullptr on any failure. Touches only C memory: the caller
// runs it parked, so the resolver, the handshake and its waits never hold up a
// stop-the-world collection.
static Http3SessionCtx* h3_open_session(const std::string& host, const int port) {
  Http3SessionCtx* ctx = new Http3SessionCtx();
  ctx->host = host;

  // Resolve hostname and create non-blocking UDP socket
  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%d", port);
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  if(getaddrinfo(host.c_str(), portstr, &hints, &res) != 0) {
    delete ctx; return nullptr;
  }

  ctx->udp_fd = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if(ctx->udp_fd < 0) {
    freeaddrinfo(res); delete ctx; return nullptr;
  }
  // The read/write loop below assumes a non-blocking socket; a blocking one
  // would park the VM thread in recvfrom instead of driving the QUIC timers,
  // so a failure here fails the connect like the socket() call above.
  const int fflags = fcntl(ctx->udp_fd, F_GETFL, 0);
  if(fflags < 0 || fcntl(ctx->udp_fd, F_SETFL, fflags | O_NONBLOCK) < 0) {
    freeaddrinfo(res); delete ctx; return nullptr;
  }

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
    freeaddrinfo(res); delete ctx; return nullptr;
  }
  // The bound ephemeral address becomes the local end of the ngtcp2 path; if
  // it cannot be read back there is no path to build, so fail like bind().
  socklen_t actual_local = sizeof(ctx->local_addr);
  if(getsockname(ctx->udp_fd, (struct sockaddr*)&ctx->local_addr, &actual_local) < 0) {
    freeaddrinfo(res); delete ctx; return nullptr;
  }
  ctx->local_addrlen = actual_local;

  memcpy(&ctx->remote_addr, res->ai_addr, res->ai_addrlen);
  ctx->remote_addrlen = (socklen_t)res->ai_addrlen;
  freeaddrinfo(res);

  // TLS 1.3 client for QUIC, ALPN "h3". AWS-LC is linked statically, so obr
  // needs no system TLS library: v2026.6.3 through v2026.9.2 linked GnuTLS and
  // ngtcp2's GnuTLS backend, which Homebrew does not ship, and obr could not
  // start on a user's Mac.
  ctx->ssl_ctx = SSL_CTX_new(TLS_client_method());
  if(!ctx->ssl_ctx || ngtcp2_crypto_boringssl_configure_client_context(ctx->ssl_ctx) != 0) {
    delete ctx; return nullptr;
  }
  SSL_CTX_set_min_proto_version(ctx->ssl_ctx, TLS1_3_VERSION);

  // Verify the server certificate chain, against the same cacert.pem the HTTPS
  // client trusts, and that it matches the requested hostname. Without this the
  // QUIC handshake would complete against any certificate (MITM). On failure the
  // handshake aborts with a verification error. Operators can skip this for
  // testing via OBJECK_TLS_INSECURE_SKIP_VERIFY.
  const bool verify_peer = !IPSecureSocket::InsecureSkipVerify();
  if(verify_peer) {
    const std::string cert_path = UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE;
    if(SSL_CTX_load_verify_locations(ctx->ssl_ctx, cert_path.c_str(), nullptr) != 1) {
      std::wcerr << L">>> Unable to load HTTP/3 CA certificates from '"
                 << BytesToUnicode(cert_path) << L"' <<<" << std::endl;
      delete ctx; return nullptr;
    }
    SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER, nullptr);
  }

  ctx->ssl = SSL_new(ctx->ssl_ctx);
  if(!ctx->ssl) {
    delete ctx; return nullptr;
  }
  SSL_set_connect_state(ctx->ssl);
  static const uint8_t alpn_h3[] = { 2, 'h', '3' };
  SSL_set_alpn_protos(ctx->ssl, alpn_h3, sizeof(alpn_h3));
  SSL_set_tlsext_host_name(ctx->ssl, host.c_str());
  if(verify_peer && SSL_set1_host(ctx->ssl, host.c_str()) != 1) {
    delete ctx; return nullptr;
  }

  // Link the TLS session to ngtcp2 via conn_ref
  ctx->conn_ref.get_conn  = h3_get_conn;
  ctx->conn_ref.user_data = ctx;
  SSL_set_app_data(ctx->ssl, &ctx->conn_ref);

  // Generate random QUIC connection IDs
  // zero-initialised so a CSPRNG failure can never transmit stack contents
  ngtcp2_cid dcid = {}, scid = {};
  dcid.datalen = 20;
  scid.datalen = 8;
  if(!h3_random_bytes(dcid.data, dcid.datalen) ||
     !h3_random_bytes(scid.data, scid.datalen)) {
    std::wcerr << L">>> Unable to generate QUIC connection IDs <<<" << std::endl;
    delete ctx; return nullptr;
  }

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
    delete ctx; return nullptr;
  }
  ngtcp2_conn_set_tls_native_handle(ctx->conn, ctx->ssl);

  // Drive QUIC handshake; h3_run_loop returns once nghttp3 session is ready
  if(!h3_run_loop(ctx)) {
    // A rejected certificate and an unreachable host both end here; say which,
    // so a security failure is not mistaken for a network problem. AWS-LC
    // reports X509_V_ERR_INVALID_CALL when no certificate was ever verified
    // (the host refused QUIC, or never answered), which is not a rejection.
    const long verify_result = SSL_get_verify_result(ctx->ssl);
    if(verify_peer && verify_result != X509_V_OK && verify_result != X509_V_ERR_INVALID_CALL) {
      std::wcerr << L">>> HTTP/3 certificate verification failed for '" << BytesToUnicode(host)
                 << L"': " << BytesToUnicode(X509_verify_cert_error_string(verify_result)) << L" <<<" << std::endl;
    }
    delete ctx; return nullptr;
  }

  return ctx;
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

  // getaddrinfo() and the QUIC handshake block on the network; park for the
  // duration, as Http2Connect does. Unparked, a thread waiting here stalls
  // every stop-the-world collection, and when the peer is a thread of this VM
  // that needs a collection before it can answer, neither moves again. Only C
  // memory is touched while parked; the instance ref is re-rooted on the
  // op_stack and re-read after the wait.
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  Http3SessionCtx* ctx = h3_open_session(host, port);
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);

  instance[0] = (size_t)ctx;   // nullptr on failure
#elif defined(OBJECK_HAS_WINHTTP_H3)
  if(!host_array || !instance) { if(instance) { instance[0] = 0; } return true; }

  host_array = (size_t*)host_array[0];
  const std::string host = UnicodeToBytes((wchar_t*)(host_array + 3));

  // h3win_connect fails closed when this Windows build has no HTTP/3, rather
  // than connecting and silently delivering HTTP/2 on the first request.
  // WinHttpOpen can block resolving the proxy configuration; park for it, with
  // the instance ref re-rooted on the op_stack and re-read after.
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  Http3WinCtx* wctx = h3win_connect(host, port);
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);
  instance[0] = (size_t)wctx;   // nullptr on failure -- same contract as POSIX
#else
  if(instance) instance[0] = 0;
  std::wcerr << L">>> HTTP/3 not available: this VM was built without an HTTP/3 engine "
                L"(ngtcp2 on Linux/macOS, WinHTTP on Windows 11+) <<<" << std::endl;
#endif
  return true;
}

bool TrapProcessor::Http3Request(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame,
                                 const std::map<std::string, std::string>* extra_headers)
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
  // Caller headers, when the _HDRS trap supplied them. Set before the
  // request is built so both backends pick them up from the map they
  // already consume.
  if(ctx && extra_headers) {
    ctx->request_headers = *extra_headers;
  }

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
      const std::string ctype_val = UnicodeToBytes((wchar_t*)(ctype_array + 3));
      // Caller-supplied: refuse anything that could add a header line of its
      // own, or that a downstream h2/h3 -> HTTP/1.1 gateway would re-emit as
      // one. Both libraries validate nothing on submit.
      if(!h3_valid_header_value(ctype_val)) {
        PushInt(0, op_stack, stack_pos); return true;
      }
      hdr_keys.push_back("content-type");
      hdr_vals.push_back(ctype_val);
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

  // The QUIC send/poll/recv loop blocks on the peer; park for it (see
  // Http3Connect). Its callbacks touch only ctx (C heap) and body_data (a C++
  // copy); the instance ref is re-rooted on the op_stack and re-read after.
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  const bool completed = h3_run_loop(ctx);
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!completed) {
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
  body_obj[0] = body_size;
  body_obj[1] = body_dim;
  body_obj[2] = body_size;
  if(body_size > 0)
    memcpy((uint8_t*)(body_obj + 3), ctx->response_body.data(), body_size);
  instance[5] = (size_t)body_obj;
  instance[6] = (size_t)CreateStringObject(BytesToUnicode(ct), program, op_stack, stack_pos);
  // Write barrier: `instance` is the caller's response object, which may well be
  // old by the time a request completes, and the content-type String is young.
  MemoryManager::WriteBarrier(instance);

  PushInt(1, op_stack, stack_pos);
#elif defined(OBJECK_HAS_WINHTTP_H3)
  Http3WinCtx* ctx = instance ? (Http3WinCtx*)instance[0] : nullptr;
  // Caller headers, when the _HDRS trap supplied them. Set before the
  // request is built so both backends pick them up from the map they
  // already consume.
  if(ctx && extra_headers) {
    ctx->request_headers = *extra_headers;
  }

  if(!ctx || !method_array || !path_array) {
    PushInt(0, op_stack, stack_pos); return true;
  }

  method_array = (size_t*)method_array[0];
  const std::string method = UnicodeToBytes((wchar_t*)(method_array + 3));
  path_array   = (size_t*)path_array[0];
  const std::string path   = UnicodeToBytes((wchar_t*)(path_array + 3));

  // Content-type only travels with a body, matching the POSIX path.
  std::vector<uint8_t> body_data;
  std::string ctype;
  if(body_array) {
    const size_t blen = body_array[2];
    uint8_t* bptr = (uint8_t*)(body_array + 3);
    body_data.assign(bptr, bptr + blen);
    if(ctype_array) {
      ctype_array = (size_t*)ctype_array[0];
      ctype = UnicodeToBytes((wchar_t*)(ctype_array + 3));
      // WinHttpAddRequestHeaders takes a CRLF-SEPARATED BLOCK, so a CR/LF here
      // is the API's own separator -- request splitting, not a parse error.
      if(!h3_valid_header_value(ctype)) {
        PushInt(0, op_stack, stack_pos); return true;
      }
    }
  }

  // Returns false if the exchange was not actually HTTP/3 -- see win_http3.h.
  // WinHTTP's send, receive and read calls block on the network; park for them
  // (only ctx and the C++ copies above are touched), with the instance ref
  // re-rooted on the op_stack and re-read after the wait.
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  const bool completed = h3win_request(ctx, method, path, ctype,
                                       body_data.empty() ? nullptr : body_data.data(),
                                       body_data.size());
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!completed) {
    PushInt(0, op_stack, stack_pos); return true;
  }

  // Same instance slots as the POSIX path: [4]=status, [5]=body, [6]=content-type
  const std::string resp_ct = ctx->response_headers.count("content-type")
                               ? ctx->response_headers["content-type"] : "";
  instance[4] = (size_t)ctx->response_status;

  const size_t body_size = ctx->response_body.size();
  const size_t body_dim  = 1;
  size_t* body_obj = MemoryManager::AllocateArray(
      body_size + 1 + ((body_dim + 2) * sizeof(size_t)),
      instructions::BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  body_obj[0] = body_size;
  body_obj[1] = body_dim;
  body_obj[2] = body_size;
  if(body_size > 0) {
    memcpy((uint8_t*)(body_obj + 3), ctx->response_body.data(), body_size);
  }
  instance[5] = (size_t)body_obj;
  instance[6] = (size_t)CreateStringObject(BytesToUnicode(resp_ct), program, op_stack, stack_pos);
  MemoryManager::WriteBarrier(instance);

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
#ifdef OBJECK_H3_WINHTTP
    Http3WinCtx* ctx = (Http3WinCtx*)instance[0];
#else
    Http3SessionCtx* ctx = (Http3SessionCtx*)instance[0];
#endif
    instance[0] = 0;
    delete ctx;
  }
  return true;
}

// --- DTLS trap implementations ---

// the calling thread's last DTLS failure (see common.h)
thread_local int dtls_last_error = 0;
thread_local uint32_t dtls_last_verify_flags = 0;
thread_local int tls_last_error = 0;
thread_local uint32_t tls_last_verify_flags = 0;

//
// A TLS or DTLS failure as text: mbedTLS's description of the error and, for
// a certificate that failed verification, each reason it gives ("The
// certificate is not correctly signed by the trusted CA; ..."), so a caller can
// tell an untrusted issuer from a name mismatch or an expired certificate.
//
static std::wstring TlsErrorMessage(int error, uint32_t verify_flags, const SocketError& reason)
{
  char error_buf[256] = {0};
  mbedtls_strerror(error, error_buf, sizeof(error_buf) - 1);
  std::string message = error_buf;

  // a connect, bind or listen failure also says why the system refused it
  // ("No connection could be made because the target machine actively refused
  // it.", "Connection refused", "Address already in use", "No such host is
  // known.")
  const bool net_failure = error == MBEDTLS_ERR_NET_CONNECT_FAILED || error == MBEDTLS_ERR_NET_UNKNOWN_HOST ||
    error == MBEDTLS_ERR_NET_SOCKET_FAILED || error == MBEDTLS_ERR_NET_BIND_FAILED || error == MBEDTLS_ERR_NET_LISTEN_FAILED ||
    error == MBEDTLS_ERR_NET_CONN_RESET || error == MBEDTLS_ERR_NET_SEND_FAILED || error == MBEDTLS_ERR_NET_RECV_FAILED;
  if(net_failure && reason.code != 0) {
    message += ": " + UnicodeToBytes(SocketErrorText(reason));
  }

  if(verify_flags != 0) {
    char info_buf[1024] = {0};
    if(mbedtls_x509_crt_verify_info(info_buf, sizeof(info_buf) - 1, "", verify_flags) > 0) {
      // one reason per line; joined with "; "
      std::string reasons;
      std::string line;
      for(const char* p = info_buf; *p; ++p) {
        if(*p == '\n') {
          if(!line.empty()) {
            reasons += reasons.empty() ? line : "; " + line;
          }
          line.clear();
        }
        else {
          line += *p;
        }
      }
      if(!line.empty()) {
        reasons += reasons.empty() ? line : "; " + line;
      }
      if(!reasons.empty()) {
        message += ": " + reasons;
      }
    }
  }

  return BytesToUnicode(message);
}

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

    // The DTLS handshake blocks on the peer (with retransmissions, for up to a
    // minute); park for it, as SockTcpSslConnect does. Unparked, a thread waiting
    // here stalls every stop-the-world collection, and when the peer is a thread
    // of this VM that needs a collection before it can answer, neither moves
    // again. Only C locals are touched while parked; the instance ref is
    // re-rooted on the op_stack so GC promotion fixup relocates it, then re-read.
    DtlsSocketCtx* old_sctx = (DtlsSocketCtx*)instance[0];
    DtlsSocketCtx* sctx = nullptr;
    ClearDtlsError();
    PushInt((size_t)instance, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    // Close existing connection if any
    IPDtlsSocket::Close(old_sctx);
    const bool is_open = IPDtlsSocket::Open(addr.c_str(), static_cast<int>(port), pem_file, sctx, verify);
    MemoryManager::EndBlocking();
    instance = (size_t*)PopInt(op_stack, stack_pos);
    // The old context is freed whatever happens next. A failed open used to
    // leave instance[0] pointing at it and instance[3] still claiming open, so
    // the next read, write or Close used, or freed, the context a second time.
    instance[0] = instance[1] = instance[2] = instance[3] = 0;
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

// DTLSSocket and DTLSSocketServer both call SOCK_DTLS_ISSUER and
// SOCK_DTLS_SUBJECT, but instance[0] is a DtlsSocketCtx in one and a
// DtlsServerCtx in the other. Reading a server's context as a socket's handed
// mbedtls a garbage ssl context. A socket reports its peer's certificate, a
// server its own.
static const mbedtls_x509_name* DtlsCertName(StackProgram* program, size_t* instance, bool subject)
{
  if(!instance || !instance[0]) {
    return nullptr;
  }

  const mbedtls_x509_crt* cert = nullptr;
  if(MemoryManager::GetObjectID(instance) == program->GetDtlsSocketObjectId()) {
    cert = mbedtls_ssl_get_peer_cert(&((DtlsSocketCtx*)instance[0])->ssl);
  }
  else {
    // set only once Listen has parsed the certificate
    cert = &((DtlsServerCtx*)instance[0])->srvcert;
  }

  if(!cert) {
    return nullptr;
  }

  return subject ? &cert->subject : &cert->issuer;
}

bool TrapProcessor::SockDtlsIssuer(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  const mbedtls_x509_name* name = DtlsCertName(program, instance, false);
  if(name) {
    char buffer[LARGE_BUFFER_MAX] = {0};
    mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, name);
    const std::wstring in = BytesToUnicode(buffer);
    PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  const mbedtls_x509_name* name = DtlsCertName(program, instance, true);
  if(name) {
    char buffer[LARGE_BUFFER_MAX] = {0};
    mbedtls_x509_dn_gets(buffer, LARGE_BUFFER_MAX - 1, name);
    const std::wstring in = BytesToUnicode(buffer);
    PushInt((size_t)CreateStringObject(in, program, op_stack, stack_pos), op_stack, stack_pos);
  }
  else {
    PushInt(0, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::SockDtlsClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(!instance) {
    return true;
  }
  DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];

#ifdef _DEBUG
  std::wcout << L"# dtls socket close: sctx=" << sctx << L"("
    << (size_t)sctx << L") #" << std::endl;
#endif
  // the close_notify alert is a send, which can block; park for it (sctx is C
  // heap, the instance ref is re-rooted)
  PushInt((size_t)instance, op_stack, stack_pos);
  MemoryManager::BeginBlocking();
  IPDtlsSocket::Close(sctx);
  MemoryManager::EndBlocking();
  instance = (size_t*)PopInt(op_stack, stack_pos);
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
      ClearDtlsError();
      const std::string out = UnicodeToBytes((wchar_t*)(array + 3));
      // a DTLS write can block; park (out is C heap)
      MemoryManager::BeginBlocking();
      IPDtlsSocket::WriteBytes(out.c_str(), (int)out.size(), sctx);
      MemoryManager::EndBlocking();
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
      ClearDtlsError();
      const size_t capacity = array[0];
      size_t index = 0;
      char value;
      bool end_line = false;
      // the read loop blocks on the peer; park for its duration, as
      // SockTcpSslInString does. Only C locals are touched while parked; the
      // array ref is re-rooted on the op_stack so GC promotion fixup relocates
      // it, then re-read after the wait.
      PushInt((size_t)array, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      do {
        value = IPDtlsSocket::ReadByte(sctx, status);
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
        IPDtlsSocket::ReadByte(sctx, status);
      }
      MemoryManager::EndBlocking();
      array = (size_t*)PopInt(op_stack, stack_pos);

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
  DtlsServerListen(instance, std::string(), op_stack, stack_pos);
  return true;
}

//
// DTLSSocketServer->Listen() for a server given an address: as SockDtlsListen,
// but the listener binds that one address, and so does every listener after
// it (DtlsAccept). Called as method : ListenAddress(address : String) ~ Bool.
//
bool TrapProcessor::SockDtlsListenAddress(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* str_obj = (size_t*)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  std::string host;
  if(str_obj && str_obj[0]) {
    host = UnicodeToBytes((wchar_t*)((size_t*)str_obj[0] + 3));
  }
  DtlsServerListen(instance, host, op_stack, stack_pos);

  return true;
}

//
// Sets up a DTLS server and binds its listener: the wildcard when host is
// empty, else that address. Pushes true on success, false otherwise.
//
void TrapProcessor::DtlsServerListen(size_t* instance, const std::string& host, size_t*& op_stack, size_t*& stack_pos)
{
  if(instance) {
    size_t* cert_obj = (size_t*)instance[3];
    size_t* key_obj = (size_t*)instance[4];
    size_t* passwd_obj = (size_t*)instance[5];
    const long port = (long)instance[6];

    if(cert_obj && key_obj) {
      ClearDtlsError();
      DtlsServerCtx* srv = new DtlsServerCtx();

      // Seed the random number generator
      const char* pers = "objeck_dtls_server";
      int ret = mbedtls_ctr_drbg_seed(&srv->ctr_drbg, mbedtls_entropy_func, &srv->entropy,
                                       (const unsigned char*)pers, strlen(pers));
      if(ret != 0) {
        dtls_last_error = ret;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      // Load server certificate
      const std::string cert_path = UnicodeToBytes((wchar_t*)((size_t*)cert_obj[0] + 3));
      ret = TlsParseCertFile(&srv->srvcert, cert_path);
      if(ret != 0) {
        dtls_last_error = ret;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
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

      ret = TlsParseKeyFile(&srv->pkey, key_path, passwd_cstr, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      if(ret != 0) {
        dtls_last_error = ret;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      // Bind to port: IPv4 and IPv6 alike (see IPSocket::BindAny and
      // DtlsAccept), or the one address asked for
      srv->port = (int)port;
      if(host.empty()) {
        ret = BindServerSocket(srv->listen_fd, (int)port, SOCK_DGRAM, dtls_socket_error);
      }
      else {
        // a name lookup can block: park, touching only C locals and srv (C
        // heap). The instance is re-rooted on the op_stack so a collection
        // that moves it fixes the reference, and is re-read after (#874).
        PushInt((size_t)instance, op_stack, stack_pos);
        MemoryManager::BeginBlocking();
        ret = BindServerSocket(srv->listen_fd, (int)port, SOCK_DGRAM, dtls_socket_error, host.c_str());
        MemoryManager::EndBlocking();
        instance = (size_t*)PopInt(op_stack, stack_pos);

        // the address bound, for the listener each accept binds afresh
        if(ret == 0) {
          struct sockaddr_storage bound;
          memset(&bound, 0, sizeof(bound));
          socklen_t bound_len = sizeof(bound);
          if(getsockname(srv->listen_fd.fd, (struct sockaddr*)&bound, &bound_len) == 0 &&
             bound_len <= (socklen_t)sizeof(srv->bound_addr)) {
            memcpy(srv->bound_addr, &bound, bound_len);
            srv->bound_len = (int)bound_len;
          }
          else {
            ret = MBEDTLS_ERR_NET_BIND_FAILED;
          }
        }
      }
      if(ret != 0) {
        dtls_last_error = ret;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      // Configure SSL for DTLS server
      ret = mbedtls_ssl_config_defaults(&srv->conf, MBEDTLS_SSL_IS_SERVER,
                                         MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
      if(ret != 0) {
        dtls_last_error = ret;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }

      mbedtls_ssl_conf_rng(&srv->conf, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      mbedtls_ssl_conf_own_cert(&srv->conf, &srv->srvcert, &srv->pkey);

      // Setup DTLS cookies
      ret = mbedtls_ssl_cookie_setup(&srv->cookie_ctx, mbedtls_ctr_drbg_random, &srv->ctr_drbg);
      if(ret != 0) {
        // this path pushed no result, leaving Listen's caller to read whatever
        // lay on the operand stack
        dtls_last_error = ret;
        delete srv;
        PushInt(0, op_stack, stack_pos);
        instance[0] = instance[1] = instance[2] = 0;
        return;
      }
      mbedtls_ssl_conf_dtls_cookies(&srv->conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &srv->cookie_ctx);

      instance[0] = (size_t)srv;
      instance[1] = 0;
      instance[2] = 0;
      // @port: the port bound, which for a port of 0 the system chose. srv->port
      // follows it too: the wildcard listener each accept binds afresh
      // (DtlsAccept, IPSocket::BindAny(srv->port)) must take this port again,
      // not another one of the system's choosing.
      const int bound_port = BoundPort((SOCKET)srv->listen_fd.fd);
      if(bound_port > 0) {
        srv->port = bound_port;
        instance[6] = bound_port;
      }

      PushInt(1, op_stack, stack_pos);
      return;
    }
  }

  PushInt(0, op_stack, stack_pos);
  return;
}

bool TrapProcessor::SockDtlsAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  if(instance) {
    DtlsServerCtx* srv = (DtlsServerCtx*)instance[0];

    if(srv) {
      DtlsSocketCtx* client_sctx = new DtlsSocketCtx();

      // Accept raw connection. accept() blocks indefinitely; park so STW GC
      // elsewhere can proceed (see SockTcpSslAccept for the re-rooting rationale).
      // The handshake blocks on the peer just as accept() does, so it stays
      // inside the parked region: a peer that is a thread of this VM may need a
      // collection to finish before it can send its next flight. Only srv and
      // client_sctx (C heap) are touched while parked.
      // The server verifies cookies (HelloVerifyRequest), and a cookie is bound
      // to the client's transport id -- its address. Without one set, every
      // cookie check failed, so no client could ever complete a handshake.
      unsigned char client_ip[16] = {0};
      size_t client_ip_len = 0;
      ClearDtlsError();

      PushInt((size_t)instance, op_stack, stack_pos);
      MemoryManager::BeginBlocking();
      int ret = DtlsAccept(srv, &client_sctx->net, client_ip, sizeof(client_ip), &client_ip_len);
      if(ret == 0) {
        // Setup SSL session for this client (uses server's config)
        ret = mbedtls_ssl_setup(&client_sctx->ssl, &srv->conf);
        if(ret == 0) {
          ret = mbedtls_ssl_set_client_transport_id(&client_sctx->ssl, client_ip, client_ip_len);
        }
        if(ret == 0) {
          mbedtls_ssl_set_bio(&client_sctx->ssl, client_sctx, DtlsBioSend, DtlsBioRecv, DtlsBioRecvTimeout);
          mbedtls_ssl_set_timer_cb(&client_sctx->ssl, &client_sctx->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

          // Perform DTLS handshake with cookie verification
          int retries = 0;
          const int max_retries = 10;
          while((ret = mbedtls_ssl_handshake(&client_sctx->ssl)) != 0) {
            if(ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
              if(++retries > max_retries) {
                break;
              }
              // the client answers the HelloVerifyRequest with a new
              // ClientHello carrying the cookie; a reset clears the transport id
              mbedtls_ssl_session_reset(&client_sctx->ssl);
              mbedtls_ssl_set_client_transport_id(&client_sctx->ssl, client_ip, client_ip_len);
              mbedtls_ssl_set_bio(&client_sctx->ssl, client_sctx, DtlsBioSend, DtlsBioRecv, DtlsBioRecvTimeout);
              mbedtls_ssl_set_timer_cb(&client_sctx->ssl, &client_sctx->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);
              continue;
            }
            if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
              break;
            }
          }
          if(ret != 0) {
            // a failed handshake sends close_notify, still parked
            IPDtlsSocket::Close(client_sctx);
            client_sctx = nullptr;
          }
        }
      }
      MemoryManager::EndBlocking();
      if(ret != 0) {
        dtls_last_error = ret;
      }
      instance = (size_t*)PopInt(op_stack, stack_pos);
      if(ret != 0) {
        // nullptr when the failed handshake already closed it
        delete client_sctx;
        PushInt(0, op_stack, stack_pos);
        return true;
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

      // numeric; an IPv4 client of the dual-stack listener arrives as
      // ::ffff:a.b.c.d and is reported as a.b.c.d
      char host_name[NI_MAXHOST] = {0};
      int peer_port;
      IPSocket::FormatAddress((struct sockaddr*)&pin, host_name, sizeof(host_name), peer_port);

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
  // GetLastError() is a function: nothing is pushed for it, and popping an
  // "instance" took the caller's operand. The error is the calling thread's
  // last DTLS failure, which also covers a connect or accept that failed and
  // freed its context.
  if(dtls_last_error != 0) {
    const std::wstring error_msg = TlsErrorMessage(dtls_last_error, dtls_last_verify_flags, dtls_socket_error);
    PushInt((size_t)CreateStringObject(error_msg, program, op_stack, stack_pos), op_stack, stack_pos);
    return true;
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
  // a socket whose connect failed has no context: there is nothing to read
  // or write (this used it, and crashed), and its reason stays reported
  if(instance && instance[0]) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    ClearDtlsError();
    int status;
    // blocking read; park so a stop-the-world collection elsewhere can proceed
    MemoryManager::BeginBlocking();
    const char value = IPDtlsSocket::ReadByte(sctx, status);
    MemoryManager::EndBlocking();
    PushInt(value, op_stack, stack_pos);
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

  // no context: the connect failed (see SockDtlsInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    ClearDtlsError();
    // blocking read; park for the duration. Bytes land in a temp C buffer (a
    // parked thread's GC array can be moved by promotion mid-read), and the
    // array ref is re-rooted on the op_stack so fixup relocates it.
    char* temp = new char[num > 0 ? num : 1];
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    const int read = IPDtlsSocket::ReadBytes(temp, static_cast<int>(num), sctx);
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

bool TrapProcessor::SockDtlsInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  const long num = (long)PopInt(op_stack, stack_pos);
  const INT64_VALUE offset = (long)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);

  // no context: the connect failed (see SockDtlsInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    ClearDtlsError();
    char* byte_buffer = new char[num * 2 + 1];
    // Read into the temp buffer at 0, not +offset (see FileInCharAry heap-overflow fix).
    // Blocking read: park for the duration, with the array ref re-rooted on the
    // op_stack so GC promotion fixup relocates it while we wait.
    PushInt((size_t)array, op_stack, stack_pos);
    MemoryManager::BeginBlocking();
    int read = IPDtlsSocket::ReadBytes(byte_buffer, static_cast<int>(num), sctx);
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

bool TrapProcessor::SockDtlsOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame)
{
  INT64_VALUE value = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* instance = (size_t*)PopInt(op_stack, stack_pos);
  // a socket whose connect failed has no context: there is nothing to read
  // or write (this used it, and crashed), and its reason stays reported
  if(instance && instance[0]) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    ClearDtlsError();
    // a DTLS write can block; park so a stop-the-world collection can proceed
    MemoryManager::BeginBlocking();
    const bool written = IPDtlsSocket::WriteByte((char)value, sctx);
    MemoryManager::EndBlocking();
    PushInt(written ? 1 : 0, op_stack, stack_pos);
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

  // no context: the connect failed (see SockDtlsInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    ClearDtlsError();
    // a DTLS write can block; park for the duration. Data is staged in a temp C
    // buffer first -- a parked thread's GC array can be moved (and its young-gen
    // slot reused) by promotion mid-send.
    char* temp = new char[num > 0 ? num : 1];
    memcpy(temp, (char*)(array + 3) + offset, num);
    // WriteBytes reports failure as -1 and leaves the mbedtls/OpenSSL code in the
    // socket context, so fold any error onto the same -1 the guarded path pushes
    // rather than converting a negative int into PushInt's size_t.
    MemoryManager::BeginBlocking();
    const int sent = IPDtlsSocket::WriteBytes(temp, static_cast<int>(num), sctx);
    MemoryManager::EndBlocking();
    delete[] temp;
    PushInt(sent < 0 ? -1 : sent, op_stack, stack_pos);
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

  // no context: the connect failed (see SockDtlsInByte)
  if(array && instance && instance[0] && offset >= 0 && num >= 0 && offset <= (INT64_VALUE)array[0] && num <= (INT64_VALUE)array[0] - offset) {
    DtlsSocketCtx* sctx = (DtlsSocketCtx*)instance[0];
    ClearDtlsError();
    const wchar_t* buffer = (wchar_t*)(array + 3);
    // copy sub buffer
    const std::wstring sub_buffer(buffer + offset, num);
    // convert to bytes and write out
    std::string buffer_out = UnicodeToBytes(sub_buffer);
    // a DTLS write can block; park (buffer_out is C heap)
    MemoryManager::BeginBlocking();
    const int sent = IPDtlsSocket::WriteBytes(buffer_out.c_str(), (int)buffer_out.size(), sctx);
    MemoryManager::EndBlocking();
    PushInt(sent < 0 ? -1 : sent, op_stack, stack_pos);
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
    if(in.size() > 0 && IsByteOrderMark(in[0])) {
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
    PushInt(File::FileSize(name.c_str()), op_stack, stack_pos);
  }
  else {
    PushInt(-1, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::FileTempName(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame) 
{
  // decoded, not widened byte by byte: a temp directory under a user folder with
  // an accented name came back as mojibake
  const std::wstring full_path = FromNativePath(File::TempName());
  if(full_path.size() > 0) {
    const size_t str_obj = (size_t)CreateStringObject(full_path, program, op_stack, stack_pos);
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
    const std::wstring full_path = FromNativePath(File::FullPathName(name));
    if(full_path.size() > 0) {
      const size_t str_obj = (size_t)CreateStringObject(full_path, program, op_stack, stack_pos);
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
    ProcessFileOwner((wchar_t*)(array + 3), true, program, op_stack, stack_pos);
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
    ProcessFileOwner((wchar_t*)(array + 3), false, program, op_stack, stack_pos);
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
    // the error_code overload: the throwing one aborted the VM ("internal error:
    // remove: The process cannot access the file") for an open file on Windows,
    // where Delete is documented to return false (#1180)
    std::error_code error_code;
    PushInt(std::filesystem::remove(name.c_str(), error_code), op_stack, stack_pos);
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
  from = (size_t*)from[0];

#ifdef _WIN32
  // The CRT's rename() refuses an existing target on Windows, so a save could not
  // replace its file the way POSIX rename() does. MoveFileExW replaces it, atomically
  // on one volume, and takes the names as UTF-16 rather than narrowed to bytes.
  const BOOL renamed = MoveFileExW((const wchar_t*)(from + 3), (const wchar_t*)(to + 3),
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
  PushInt(renamed ? 1 : 0, op_stack, stack_pos);
#else
  const NativePath to_name = ToNativePath((wchar_t*)(to + 3));
  const NativePath from_name = ToNativePath((wchar_t*)(from + 3));

  if(rename(from_name.c_str(), to_name.c_str()) != 0) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    PushInt(1, op_stack, stack_pos);
  }
#endif

  return true;
}

// function : CreateNew(name : String) ~ Bool: creates an empty file only if no file of that
// name exists, as one step, so two processes racing for a lock file cannot both win
bool TrapProcessor::FileCreateNew(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* name = (size_t*)PopInt(op_stack, stack_pos);
  if(!name) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }
  name = (size_t*)name[0];

#ifdef _WIN32
  HANDLE handle = CreateFileW((const wchar_t*)(name + 3), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if(handle == INVALID_HANDLE_VALUE) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    CloseHandle(handle);
    PushInt(1, op_stack, stack_pos);
  }
#else
  const std::string name_str = UnicodeToBytes((wchar_t*)(name + 3));
  const int fd = open(name_str.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  if(fd < 0) {
    PushInt(0, op_stack, stack_pos);
  }
  else {
    close(fd);
    PushInt(1, op_stack, stack_pos);
  }
#endif

  return true;
}

// function : GetProcessId() ~ Int
bool TrapProcessor::ProcessId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
#ifdef _WIN32
  PushInt((size_t)GetCurrentProcessId(), op_stack, stack_pos);
#else
  PushInt((size_t)getpid(), op_stack, stack_pos);
#endif
  return true;
}

// function : IsProcessAlive(pid : Int) ~ Bool: whether a process with that id is running on
// this machine; one this process may not inspect still counts as running
bool TrapProcessor::ProcessAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE pid = (INT64_VALUE)PopInt(op_stack, stack_pos);
  if(pid <= 0) {
    PushInt(0, op_stack, stack_pos);
    return true;
  }

#ifdef _WIN32
  bool alive = false;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
  if(process) {
    DWORD code = 0;
    alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
  }
  else {
    alive = GetLastError() == ERROR_ACCESS_DENIED;
  }
  PushInt(alive ? 1 : 0, op_stack, stack_pos);
#else
  const bool alive = kill((pid_t)pid, 0) == 0 || errno == EPERM;
  PushInt(alive ? 1 : 0, op_stack, stack_pos);
#endif

  return true;
}

// function : UncompressDeflate(input : Byte[], limit : Int) ~ Byte[] and
// UncompressGzip(input : Byte[], limit : Int) ~ Byte[]: inflate at most 'limit' bytes.
// Nil when the stream would inflate past the limit (a zip bomb, or a size header that
// lies); raises on damaged or truncated data, as the one-argument forms do.
bool TrapProcessor::UncompressLimit(int window_bits, StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  const INT64_VALUE limit = (INT64_VALUE)PopInt(op_stack, stack_pos);
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(!array) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_NIL_DEREFERENCE, L"Attempting to dereference a 'Nil' memory instance");
  }

  if(limit < 0) {
    PushInt(0, op_stack, stack_pos);
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"A negative uncompress limit");
  }

  const char* in = (char*)(array + 3);
  const uLong in_len = (uLong)array[2];

  // past the VM's own buffer limit the call fails as damaged data always has
  const unsigned long bounded = limit < (INT64_VALUE)(COMPRESS_BUFFER_LIMIT) ? (unsigned long)limit : (unsigned long)(COMPRESS_BUFFER_LIMIT);
  unsigned long out_len = 0;
  bool over_limit = false;
  char* out = OutputStream::InflateStream(in, in_len, window_bits, out_len, bounded, &over_limit);
  if(!out) {
    PushInt(0, op_stack, stack_pos);
    if(over_limit) {
      return true;
    }
    return TrapError(Runtime::StackInterpreter::RECOVERED_OTHER, L"Invalid, truncated or oversized compressed data");
  }

  const long byte_array_size = (long)out_len;
  const long byte_array_dim = 1;
  size_t* byte_array = MemoryManager::AllocateArray(byte_array_size + 1 + ((byte_array_dim + 2) * sizeof(size_t)), BYTE_ARY_TYPE, op_stack, *stack_pos, false);
  byte_array[0] = byte_array_size;
  byte_array[1] = byte_array_dim;
  byte_array[2] = byte_array_size;

  memcpy((char*)(byte_array + 3), out, byte_array_size);
  free(out);

  PushInt((size_t)byte_array, op_stack, stack_pos);
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
  const NativePath to_name = ToNativePath((wchar_t*)(to + 3));

  from = (size_t*)from[0];
  const NativePath from_name = ToNativePath((wchar_t*)(from + 3));

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
  const NativePath to_name = ToNativePath((wchar_t*)(to + 3));

  from = (size_t*)from[0];
  const NativePath from_name = ToNativePath((wchar_t*)(from + 3));

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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    // native(), not string(): on Windows string() narrows to the ANSI code page
    const std::wstring dir_cur_str = FromNativePath(dir_cur_path.native());
    const size_t* dir_cur_obj = CreateStringObject(dir_cur_str, program, op_stack, stack_pos);
    PushInt((size_t)dir_cur_obj, op_stack, stack_pos);
  }

  return true;
}

bool TrapProcessor::DirSetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame)
{
  size_t* array = (size_t*)PopInt(op_stack, stack_pos);
  if(array) {
    array = (size_t*)array[0];
    const NativePath to_dir_str = ToNativePath((wchar_t*)(array + 3));
#ifdef _WIN32
    const int status = _wchdir(to_dir_str.c_str());
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
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
  if(array) {
    array = (size_t*)array[0];
    const NativePath name = ToNativePath((wchar_t*)(array + 3));
    const std::vector<NativePath> files = File::ListDir(name.c_str());

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
      const std::wstring wfile = FromNativePath(files[i]);
      str_obj_array_ptr[i] = (size_t)CreateStringObject(wfile, program, op_stack, stack_pos);
    }

    // Write barrier: the array is old (AllocateArray never uses the nursery) and
    // the elements are young objects. A minor GC repairs references only in
    // objects the barrier recorded, so without this the elements are promoted
    // out from under an array the collector will not look inside.
    MemoryManager::WriteBarrier(str_obj_array);

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
  if(array) {
    array = (size_t*)array[0];
    const NativePath dir_name = ToNativePath((wchar_t*)(array + 3));
    std::error_code remove_error;
    const auto removed = std::filesystem::remove_all(dir_name, remove_error);
    const auto count = remove_error ? 0 : removed;
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
    const NativePath target_str = ToNativePath((wchar_t*)(target_obj + 3));

    link_obj = (size_t*)link_obj[0];
    const NativePath link_str = ToNativePath((wchar_t*)(link_obj + 3));

    std::error_code error_code;
    std::error_code kind_error;
    if(std::filesystem::is_directory(target_str, kind_error)) {
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
    const NativePath to_str = ToNativePath((wchar_t*)(to + 3));

    from = (size_t*)from[0];
    const NativePath from_str = ToNativePath((wchar_t*)(from + 3));

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
  if(array) {
    array = (size_t*)array[0];
    const NativePath link_str = ToNativePath((wchar_t*)(array + 3));

    std::error_code error_code;
    const auto target_path = std::filesystem::read_symlink(link_str, error_code);
    if(error_code) {
      PushInt(0, op_stack, stack_pos);
    }
    else {
      PushInt((size_t)CreateStringObject(FromNativePath(target_path.native()), program, op_stack, stack_pos), op_stack, stack_pos);
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
    const NativePath path_str = ToNativePath((wchar_t*)(array + 3));
    std::error_code link_error;
    PushInt(std::filesystem::is_symlink(path_str, link_error), op_stack, stack_pos);
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
    const NativePath target_str = ToNativePath((wchar_t*)(target_obj + 3));

    link_obj = (size_t*)link_obj[0];
    const NativePath link_str = ToNativePath((wchar_t*)(link_obj + 3));

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

/********************************
 * Formats an encoded parameter
 * list "i,f*,m.(i,)~i," the way the
 * source declares it:
 * "(a:Int, b:Float[], c:(Int)~Int)".
 *
 * Types are read by recursive
 * descent with the rule
 * SkipFunctionType uses: a function
 * type's parameter list runs to its
 * MATCHING ')', and the return type
 * after '~' may itself be a function
 * type. The old scan counted every
 * "m.(" in the whole signature and
 * skipped that many '~', so one
 * function-typed parameter swallowed
 * the parameters after it, and the
 * nested list was re-split at the
 * last '(' and first ')':
 * 'P1->Crash(a:(a:Int, , , b:Int,
 * c:Int, , , d:Int)~Int)'.
 ********************************/
std::wstring MethodFormatter::FormatParameters(const std::wstring &param_str)
{
  std::wstring formatted_str = L"(";
  size_t index = 0;
  size_t count = 0;

  while(index < param_str.size()) {
    if(count > 0) {
      formatted_str += L", ";
    }

    if(count < 26) {
      formatted_str += (wchar_t)(L'a' + count);
    }
    else {
      formatted_str += L"p" + std::to_wstring(count + 1);
    }
    formatted_str += L':';
    formatted_str += FormatType(param_str, index);
    count++;

    // to the parameter's ',' (a malformed type stops short of it)
    while(index < param_str.size() && param_str[index] != L',') {
      index++;
    }
    index++;
  }
  formatted_str += L")";

  return formatted_str;
}

// ends a class or generic name inside an encoded signature
static bool IsEncodedNameEnd(wchar_t c)
{
  return c == L'*' || c == L',' || c == L'|' || c == L')' || c == L'<' || c == L'>' || c == L'~';
}

/********************************
 * Formats the one encoded type that
 * starts at 'index' and leaves
 * 'index' just past it (its
 * generics and dimensions included)
 ********************************/
std::wstring MethodFormatter::FormatType(const std::wstring &type_str, size_t &index)
{
  std::wstring formatted_str;
  const size_t size = type_str.size();
  if(index >= size) {
    return formatted_str;
  }

  switch(type_str[index]) {
  case L'l':
    formatted_str = L"Boolean";
    index++;
    break;

  case L'b':
    formatted_str = L"Byte";
    index++;
    break;

  case L'i':
    formatted_str = L"Int";
    index++;
    break;

  case L'f':
    formatted_str = L"Float";
    index++;
    break;

  case L'c':
    formatted_str = L"Char";
    index++;
    break;

  case L'n':
    formatted_str = L"Nil";
    index++;
    break;

  case L'v':
    formatted_str = L"Var";
    index++;
    break;

  case L'm':
    formatted_str = FormatFunctionalType(type_str, index);
    break;

  case L'o': {
    // "o.<class name>"
    index = index + 2 < size ? index + 2 : size;
    const size_t start = index;
    while(index < size && !IsEncodedNameEnd(type_str[index])) {
      index++;
    }
    formatted_str = type_str.substr(start, index - start);
  }
    break;
  }

  // generics: "<A|B>"
  if(index < size && type_str[index] == L'<') {
    formatted_str += L'<';
    int nesting = 0;
    while(index < size) {
      const wchar_t c = type_str[index++];
      if(c == L'<') {
        if(nesting++ > 0) {
          formatted_str += c;
        }
      }
      else if(c == L'>') {
        if(--nesting == 0) {
          break;
        }
        formatted_str += c;
      }
      else if(c == L'|') {
        formatted_str += L", ";
      }
      else {
        formatted_str += c;
      }
    }
    formatted_str += L'>';
  }
  // generics: "|A|B"
  else if(index < size && type_str[index] == L'|') {
    formatted_str += L'<';
    bool first = true;
    while(index < size && type_str[index] == L'|') {
      index++;
      if(!first) {
        formatted_str += L", ";
      }
      first = false;

      const size_t start = index;
      while(index < size && !IsEncodedNameEnd(type_str[index])) {
        index++;
      }
      formatted_str += type_str.substr(start, index - start);
    }
    formatted_str += L'>';
  }

  // dimensions: one '*' per dimension, "Int[]", "Char[,]"
  if(index < size && type_str[index] == L'*') {
    formatted_str += L'[';
    bool first = true;
    while(index < size && type_str[index] == L'*') {
      if(!first) {
        formatted_str += L',';
      }
      first = false;
      index++;
    }
    formatted_str += L']';
  }

  return formatted_str;
}

/********************************
 * Formats the encoded function type
 * "m.(<type>,...)~<type>" that starts
 * at 'index' as "(<type>, ...)~<type>";
 * nested function types recurse
 ********************************/
std::wstring MethodFormatter::FormatFunctionalType(const std::wstring &func_str, size_t &index)
{
  std::wstring formatted_str = L"(";
  const size_t size = func_str.size();

  // "m."
  if(index < size && func_str[index] == L'm') {
    index++;
  }
  if(index < size && func_str[index] == L'.') {
    index++;
  }

  // parameters, to the matching ')'
  if(index < size && func_str[index] == L'(') {
    index++;
    size_t count = 0;
    while(index < size && func_str[index] != L')') {
      if(count++ > 0) {
        formatted_str += L", ";
      }
      formatted_str += FormatType(func_str, index);

      while(index < size && func_str[index] != L',' && func_str[index] != L')') {
        index++;
      }
      if(index < size && func_str[index] == L',') {
        index++;
      }
    }

    if(index < size) {
      index++;
    }
  }
  formatted_str += L")";

  // return type, itself possibly a function type
  if(index < size && func_str[index] == L'~') {
    index++;
    formatted_str += L"~";
    formatted_str += FormatType(func_str, index);
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
