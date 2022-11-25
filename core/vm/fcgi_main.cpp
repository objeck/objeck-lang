/***************************************************************************
 * Starting point for FastCGI module
 *
 * Copyright (c) 2012-2020 Randy Hollines
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

#include "fcgi_main.h"

/******************************
 * FCGI entry point
 ******************************/
int main(const int argc, const char* argv[])
{
  std::wstring program_path;
  const char* raw_program_path = FCGX_GetParam("FCGI_CONFIG_PATH", environ);
  if(!raw_program_path) {
    std::wcout << L"Unable to find program, please ensure the 'FCGI_CONFIG_PATH' variable has been set correctly." << std::endl;
    exit(1);
  }
  else {
    program_path = BytesToUnicode(raw_program_path);
  }
  
#ifdef _WIN32
#ifndef _MSYS2_CLANG
  // enable Unicode console support
  if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
    return 1;
  }
  
  if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
    return 1;
  }
#endif
  
  // initialize Winsock
  WSADATA data;
  int version = MAKEWORD(2, 2);
  if(WSAStartup(version, &data)) {
    std::wcout << L"Unable to load Winsock 2.2!" << std::endl;
    exit(1);
  }
#else
#ifdef _X64
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale(locale);
  setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
#else    
  setlocale(LC_ALL, "en_US.utf8");
#endif
#endif

  Loader loader(program_path.c_str());
  loader.Load();

  // ignore web applications
  if(!loader.IsWeb()) {
    std::wcout << L"Please recompile the code to be a web application." << std::endl;
    exit(1);
  }

#ifdef _TIMING
  clock_t start = clock();
#endif

  // locate starting class and method
  StackMethod* mthd = loader.GetStartMethod();
  if(!mthd) {
    std::wcout << L"Unable to locate the 'Request(...)' function." << std::endl;
    exit(1);
  }

#ifdef _DEBUG
  std::wcout << L"### Loaded method: " << mthd->GetName() << L" ###" << std::endl;
#endif

  Runtime::StackInterpreter* intpr = new Runtime::StackInterpreter(Loader::GetProgram());
  Runtime::StackInterpreter::AddThread(intpr);

  size_t* op_stack = new size_t[OP_STACK_SIZE];
  long* stack_pos = new long;

  // enter 'accept' loop
  FCGX_Stream*in; FCGX_Stream* out; FCGX_Stream* err; FCGX_ParamArray envp;
  while(mthd && (FCGX_Accept(&in, &out, &err, &envp) >= 0)) {
    // execute method
    (*stack_pos) = 0;

    // create request and response
    size_t* req_obj = MemoryManager::AllocateObject(L"Web.FastCgi.Request",  op_stack, *stack_pos, false);
    size_t* res_obj = MemoryManager::AllocateObject(L"Web.FastCgi.Response", op_stack, *stack_pos, false);

    if(req_obj && res_obj) {
      req_obj[0] = (size_t)in;
      req_obj[1] = (size_t)envp;

      res_obj[0] = (size_t)out;
      res_obj[1] = (size_t)err;

      // set method calling parameters
      op_stack[0] = (size_t)req_obj;
      op_stack[1] = (size_t)res_obj;
      *stack_pos = 2;

      // execute method
      intpr->Execute(op_stack, stack_pos, 0, mthd, nullptr, false);
    }
    else {
      FCGX_FPrintF(out, ">>> Shared library call: Unable to allocate Web.FastCgi.Request or Web.FastCgi.Response <<<\r\n");
      return 1;
    }

#ifdef _DEBUG
    FCGX_FPrintF(out, "# final stack: pos=%i #\r\n", *stack_pos);
    if((*stack_pos) > 0) {
      for(int i = 0; i < (*stack_pos); ++i) {
        FCGX_FPrintF(out, "dump: value=%i #\r\n", op_stack[i]);
      }
    }

    PrintEnv(out, "Request environment", envp);
    PrintEnv(out, "Initial environment", environ);
#endif

    
  }

  // clean up
  delete[] op_stack;
  op_stack = nullptr;

  delete stack_pos;
  stack_pos = nullptr;

  Runtime::StackInterpreter::RemoveThread(intpr);
  Runtime::StackInterpreter::HaltAll();

  delete intpr;
  intpr = nullptr;

  return 0;
}

/******************************
* Dump environment variables
******************************/
void PrintEnv(FCGX_Stream* out, const char* label, char** envp)
{
  FCGX_FPrintF(out, "\n", label, "\r\n");
  for(; *envp != nullptr; envp++) {
    FCGX_FPrintF(out, "\t", *envp, "\r\n");
  }
}
