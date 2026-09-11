/***************************************************************************
 * Starting point for the VM.
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

#include "vm.h"
#include <codecvt>

#define SUCCESS 0
#define USAGE_ERROR -1

// common execution point for all platforms
#ifdef _WIN32
int Execute(int argc, const char* argv[], bool is_stdio_binary, size_t gc_threshold)
#else
int Execute(int argc, const char* argv[], size_t gc_threshold)
#endif
{
  if(argc > 1) {
    wchar_t** commands = ProcessCommandLine(argc, argv);

    Loader loader(argc, commands);
    try {
      loader.Load();
    }
    catch(const std::exception& e) {
      const std::string msg(e.what());
      std::wcerr << L">>> load error: " << std::wstring(msg.begin(), msg.end()) << L" <<<" << std::endl;
      CleanUpCommandLine(argc, commands);
      return USAGE_ERROR;
    }
    catch(...) {
      std::wcerr << L">>> load error: unable to load program <<<" << std::endl;
      CleanUpCommandLine(argc, commands);
      return USAGE_ERROR;
    }

    // execute
    size_t* op_stack = new size_t[OP_STACK_SIZE];
    size_t* stack_pos = new size_t;
    (*stack_pos) = 0;

#ifdef _TIMING
    clock_t start = clock();
#endif
    // start the interpreter...
#ifdef _WIN32
    Runtime::StackInterpreter::SetBinaryStdio(is_stdio_binary);
#endif
    Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter(Loader::GetProgram(), gc_threshold);
    Runtime::StackInterpreter::AddThread(intpr);
    try {
      intpr->Execute(op_stack, stack_pos, 0, loader.GetProgram()->GetInitializationMethod(), nullptr, false);
    }
    catch(const std::bad_alloc&) {
      std::wcerr << L">>> virtual machine: out of memory <<<" << std::endl;
      delete[] op_stack; delete stack_pos;
      CleanUpCommandLine(argc, commands);
      return USAGE_ERROR;
    }
    catch(const std::exception& e) {
      const std::string msg(e.what());
      std::wcerr << L">>> virtual machine: internal error: " << std::wstring(msg.begin(), msg.end()) << L" <<<" << std::endl;
      delete[] op_stack; delete stack_pos;
      CleanUpCommandLine(argc, commands);
      return USAGE_ERROR;
    }
    catch(...) {
      std::wcerr << L">>> virtual machine: unexpected error <<<" << std::endl;
      delete[] op_stack; delete stack_pos;
      CleanUpCommandLine(argc, commands);
      return USAGE_ERROR;
    }

    // Quiesce any still-running VM threads (e.g. web-server request workers)
    // before `loader` destructs and frees the program. Otherwise a worker can be
    // mid auto-JIT (PatchCallSites walking the class tables) while ~StackProgram
    // frees them -> use-after-free crash at exit. Halt the others (not `intpr`,
    // which is this thread) and wait briefly for them to unwind and deregister.
    Runtime::StackInterpreter::HaltAllExcept(intpr);
    if(!Runtime::StackInterpreter::WaitForThreadsToDrain(intpr, 2000)) {
      // A thread did not drain -- it is parked in a blocking syscall (a server
      // thread in accept(), a reader in recv()) where it cannot observe Halt, and
      // it is still live. Do not free the program out from under it: anything that
      // later wakes it -- on Windows the WSACleanup that used to run on the way
      // out of win_main -- would send it executing against a freed StackProgram.
      // Leak the image instead and let process teardown reclaim it.
      loader.Abandon();
    }

#ifdef _DEBUG
    std::wcout << L"# final std::stack: pos=" << (*stack_pos) << L" #" << std::endl;
    if((*stack_pos) > 0) {
      for(int i = 0; i < (*stack_pos); ++i) {
        std::wcout << L"dump: value=" << op_stack[i] << std::endl;
      }
    }
#endif

#ifdef _SANITIZE
#ifdef _DEBUG
    std::wcout << L"# final std::stack: pos=" << (*stack_pos) << L" #" << std::endl;
    if((*stack_pos) > 0) {
      for(int i = 0; i < (*stack_pos); ++i) {
        std::wcout << L"dump: value=" << *(stack_pos + 1) << std::endl;
      }
    }
    assert(!(*stack_pos));
#endif
    

    Runtime::StackInterpreter::RemoveThread(intpr);
    Runtime::StackInterpreter::HaltAll();

    MemoryManager::Clear();

    delete intpr;
    intpr = nullptr;
#endif

    // clean up
    delete[] op_stack;
    op_stack = nullptr;

    delete stack_pos;
    stack_pos = nullptr;
    
#ifdef _TIMING    
    clock_t end = clock();
    std::wcout << L"---------------------------" << std::endl;
    std::wcout << L"CPU Time: " << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
#endif

    CleanUpCommandLine(argc, commands);
    
    return SUCCESS;
  } 
  else {
    return USAGE_ERROR;
  }
}

wchar_t** ProcessCommandLine(int &argc, const char* argv[])
{
  bool is_response_file = 0;

  wchar_t** wide_args = new wchar_t* [argc];
  for(int i = 0; i < argc; ++i) {
    const char* arg = argv[i];
    const int len = (int)strlen(arg);
    wchar_t* wide_arg = new wchar_t[len + 1];
    for(int j = 0; j < len; ++j) {
      wide_arg[j] = arg[j];
    }
    wide_arg[len] = L'\0';

    if(argc == 3 && wcslen(wide_arg) > 0 && wide_arg[0] == L'@') {
      is_response_file = true;
    }
    wide_args[i] = wide_arg;
  }

  if(is_response_file) {
    // skip '@'
    std::ifstream file_in(UnicodeToBytes(wide_args[2] + 1));
    if(file_in.is_open()) {
      constexpr int ARG_OFFSET = 2;

      // copy vm and file parameters
      wchar_t* vm_name = wide_args[0];
      wchar_t* file_name = wide_args[1];

      delete[] wide_args;
      wide_args = nullptr;

      // copy file parameters
      std::string line;
      std::vector<std::wstring> file_line_params;
      while(std::getline(file_in, line)) {
        file_line_params.push_back(BytesToUnicode(line));
      }
      file_in.close();
      
      // rebuilt command line with file parameters
      wide_args = new wchar_t*[file_line_params.size() + ARG_OFFSET];
      wide_args[0] = vm_name;
      wide_args[1] = file_name;

      const size_t response_file_size = file_line_params.size();
      for(size_t i = 0; i < response_file_size; ++i) {
        const std::wstring file_line_arg = file_line_params[i];
        wchar_t* wide_arg = new wchar_t[file_line_arg.size() + 1];
#ifdef _WIN32
        wcsncpy_s(wide_arg, file_line_arg.size() + 1, file_line_arg.c_str(), file_line_arg.size());
#else
        wcsncpy(wide_arg, file_line_arg.c_str(), file_line_arg.size());
#endif
        wide_args[i + ARG_OFFSET] = wide_arg;
      }
      argc = (int)response_file_size + ARG_OFFSET;
    }
  }

  return wide_args;
}

void CleanUpCommandLine(const int argc, wchar_t** wide_args)
{
  for(int i = 0; i < argc; ++i) {
    wchar_t* wide_arg = wide_args[i];
    delete[] wide_arg;
    wide_arg = nullptr;
  }

  delete[] wide_args;
  wide_args = nullptr;
}

#ifndef _WIN32
// std::locale's constructor throws for a name the C++ library cannot build,
// and both POSIX entry points called it unguarded on whatever setlocale
// returned, so obr exited before the program ran. Two names get there: on
// macOS, the per-category composite the C library reports when only LC_CTYPE
// is set ("C/C.UTF-8/C/C/C/C" -- what Python 3 puts in every child's
// environment when LANG is unset, PEP 538), which libc++ cannot parse even
// though the C library set every category from it; and NULL, when the
// environment names a locale the system lacks (LANG=xx_YY.bogus) and no UTF-8
// locale could be substituted. Each candidate is tried in turn: the name
// itself; the LC_CTYPE category alone, which is the one that decides how wide
// characters are written and the one the composite was hiding; the UTF-8
// locales; and last the classic locale with a UTF-8 converter, which needs no
// locale data at all and so cannot fail. glibc names its composites
// "LC_CTYPE=C.UTF-8;LC_NUMERIC=C;..." and libstdc++ constructs from that form,
// so Linux takes the first candidate, as before.
std::locale ConsoleLocale(const char* name)
{
  std::string candidates[4];
  size_t count = 0;
  if(name) {
    candidates[count++] = name;
    // the C library accepted the environment, so its LC_CTYPE is what was asked for
    const char* ctype = setlocale(LC_CTYPE, nullptr);
    if(ctype) {
      candidates[count++] = ctype;
    }
  }
  candidates[count++] = "C.UTF-8";
  candidates[count++] = "en_US.UTF-8";

  for(size_t i = 0; i < count; ++i) {
    try {
      return std::locale(candidates[i].c_str());
    }
    catch(const std::runtime_error&) {
      // not one this C++ library can build; the next is coarser
    }
  }

  // std::codecvt_utf8 is deprecated since C++17 with nothing standard in its
  // place for this use; the MSYS2 build above imbues its sibling already
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  return std::locale(std::locale::classic(), new std::codecvt_utf8<wchar_t>);
#pragma GCC diagnostic pop
}
#endif

// Only win_main.cpp calls SetEnv; posix_main.cpp does its own setup and
// shares ConsoleLocale with the branches below.
void SetEnv() {
#ifdef _WIN32
#ifdef _MSYS2_CLANG
  std::ios_base::sync_with_stdio(false);
  std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
  std::wcout.imbue(utf8);
  std::wcin.imbue(utf8);
#else
  SetConsoleCP(CP_UTF8);
  SetConsoleOutputCP(CP_UTF8);

  if (_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }

  if (_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }
#endif
#else
#if defined(_X64)
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale = ConsoleLocale(locale);
  std::setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
#elif defined(_ARM64)
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale = ConsoleLocale(locale);
  std::setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
  std::setlocale(LC_ALL, "en_US.utf8");
#else    
  setlocale(LC_ALL, "en_US.utf8");
#endif
#endif
}
