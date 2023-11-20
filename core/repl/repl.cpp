/***************************************************************************
* REPL shell
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

#include "repl.h"
#include <codecvt>

/****************************
* Program start
****************************/
int main(int argc, char* argv[])
{
#ifndef _MSYS2_CLANG
  SetEnv();
#else
  std::ios_base::sync_with_stdio(false);
  std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
  std::wcout.imbue(utf8);
  std::wcin.imbue(utf8);
#endif

#ifdef _DEBUG
  OpenLogger("debug.log");
#endif

  // parse command line
  std::wstring cmd_line;
  std::map<const std::wstring, std::wstring> arguments = ParseCommnadLine(argc, argv, cmd_line);
  
  std::wstring input;
  std::wstring libs;
  std::wstring opt;
  int mode = 0;
  bool is_exit = false;

  // help
  std::wstring help_flags[] = { HELP_PARAM, HELP_ALT_PARAM };
  auto result = GetArgument(arguments, help_flags);
  if(!result.empty()) {
    Usage();
    RemoveArgument(arguments, help_flags);
  }
  else {
    std::wstring file_flags[] = { FILE_PARAM, FILE_ALT_PARAM };
    result = GetArgument(arguments, file_flags);
    if(!result.empty()) {
      input = result;
      mode = 1;
      RemoveArgument(arguments, file_flags);
    }

    std::wstring inline_flags[] = { INLINE_PARAM, INLINE_ALT_PARAM };
    result = GetArgument(arguments, inline_flags);
    if(!result.empty()) {
      input = result;
      mode = 2;
      RemoveArgument(arguments, inline_flags);
    }

    std::wstring exit_flags[] = { EXIT_PARAM, EXIT_ALT_PARAM };
    if(HasArgument(arguments, exit_flags)) {
      is_exit = true;
      RemoveArgument(arguments, exit_flags);
    }

    std::wstring lib_flags[] = { LIBS_PARAM, LIBS_ALT_PARAM };
    result = GetArgument(arguments, lib_flags);
    if(!result.empty()) {
      libs = result;
      mode = 2;
      RemoveArgument(arguments, lib_flags);
    }

    std::wstring opt_flags[] = { OPT_PARAM, OPT_ALT_PARAM };
    result = GetArgument(arguments, opt_flags);
    if(!result.empty()) {
      input = result;
      RemoveArgument(arguments, opt_flags);
    }

    // input check
    if(arguments.size()) {
      Usage();
    }
    // start repl loop
    else {
      Editor editor;
      editor.Edit(input, libs, opt, mode, is_exit);
    }
  }

#ifdef _DEBUG
  CloseLogger();
#endif
}

void Usage()
{
  std::wstring usage;
  usage += L"Usage: obi <options>\n\n";
  usage += L"Options:\n";
  usage += L"  -help|-h:   [optional] shows help\n";
  usage += L"  -file|-f:   [optional] optional source files (separated by commas)\n";
  usage += L"  -inline|-i: [optional] inline source code statements\n";
  usage += L"  -lib|-l:    [optional] list of linked libraries (separated by commas)\n";
  usage += L"  -opt|-o:    [optional] compiler optimizations s0-s3 (s3 being the most aggressive and default)\n";
  usage += L"  -quit|-q:   [optional] exits shell after executiong code\n";
  usage += L"\nExample: \"obi -f hello.obs\"\n\nVersion: ";
  usage += VERSION_STRING;

#if defined(_WIN64) && defined(_WIN32)
  usage += L" (Windows x86_64)";
#elif _WIN32
  usage += L" (Windows x86)";
#elif _OSX
#ifdef _ARM64
  usage += L" (macOS ARM64)";
#else
  usage += L" (macOS x86_64)";
#endif
#elif _ARM64
  usage += L" (Linux ARM64)";
#elif _X64
  usage += L" (Linux x86_64)";
#elif _ARM32
  usage += L" (Linux ARMv7)";
#else
  usage += L" (Linux x86)";
#endif

  usage += L"\nWeb: https://www.objeck.org";

  std::wcerr << usage << std::endl;
}

void SetEnv() 
{
#ifdef _WIN32
#ifdef _MSYS2_CLANG
  std::ios_base::sync_with_stdio(false);
  std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
  std::wcout.imbue(utf8);
  std::wcin.imbue(utf8);
#else
  if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }

  if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }

  // set to efficiency mode
  SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
  PROCESS_POWER_THROTTLING_STATE PowerThrottling = { 0 };
  PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  PowerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));

#endif
#else
#if defined(_X64)
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale(locale);
  std::setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
#elif defined(_ARM64)
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale(locale);
  std::setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
  std::setlocale(LC_ALL, "en_US.utf8");
#else    
  setlocale(LC_ALL, "en_US.utf8");
#endif
#endif
}

std::wstring GetArgument(std::map<const std::wstring, std::wstring> arguments, const std::wstring values[]) 
{
  auto result = arguments.find(values[0]);
  if(result != arguments.end()) {
    return result->second;
  }

  result = arguments.find(values[1]);
  if(result != arguments.end()) {
    return result->second;
  }

  return L"";
}

bool HasArgument(std::map<const std::wstring, std::wstring> arguments, const std::wstring values[]) 
{
  auto result = arguments.find(values[0]);
  if(result != arguments.end()) {
    return true;
  }

  result = arguments.find(values[1]);
  if(result != arguments.end()) {
    return true;
  }

  return false;
}

void RemoveArgument(std::map<const std::wstring, std::wstring>& arguments, const std::wstring values[]) 
{
  arguments.erase(values[0]);
  arguments.erase(values[1]);
}