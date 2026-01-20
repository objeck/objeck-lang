/***************************************************************************
* REPL shell
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

#include "repl.h"
#include <codecvt>

/****************************
* Program start
****************************/
int main(int argc, const char* argv[])
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

  // Parse command line using enhanced parser
  CommandLineParseResult cmd_result = ParseCommandLine(argc, argv);
  std::map<const std::wstring, std::wstring> arguments = cmd_result.arguments;

  std::wstring input;
  std::wstring libs;
  std::wstring opt;
  int mode = 0;
  bool is_exit = false;

  // Check for help (support --help, -help, -h)
  if(HasCommandLineArgumentWithAliases(arguments, {L"help", L"h"})) {
    Usage();
    arguments.erase(L"help");
    arguments.erase(L"h");
  }
  else {
    // Check for file input (support --file, -file, -f)
    std::wstring file_value = GetCommandLineArgumentWithAliases(arguments, {L"file", L"f"});
    if(!file_value.empty()) {
      input = file_value;
      mode = 1;
      arguments.erase(L"file");
      arguments.erase(L"f");
    }

    // Check for inline code (support --inline, -inline, -i)
    std::wstring inline_value = GetCommandLineArgumentWithAliases(arguments, {L"inline", L"i"});
    if(!inline_value.empty()) {
      input = inline_value;
      mode = 2;
      arguments.erase(L"inline");
      arguments.erase(L"i");
    }

    // Check for quit flag (support --quit, -quit, -q)
    if(HasCommandLineArgumentWithAliases(arguments, {L"quit", L"q"})) {
      is_exit = true;
      arguments.erase(L"quit");
      arguments.erase(L"q");
    }

    // Check for libraries (support --library, -lib, -l)
    std::wstring lib_value = GetCommandLineArgumentWithAliases(arguments, {L"library", L"lib", L"l"});
    if(!lib_value.empty()) {
      libs = lib_value;
      arguments.erase(L"library");
      arguments.erase(L"lib");
      arguments.erase(L"l");
    }

    // Check for optimization level (support --optimize, -opt, -o)
    std::wstring opt_value = GetCommandLineArgumentWithAliases(arguments, {L"optimize", L"opt", L"o"});
    if(!opt_value.empty()) {
      opt = opt_value;
      arguments.erase(L"optimize");
      arguments.erase(L"opt");
      arguments.erase(L"o");
    }

    // Check for unknown arguments
    if(arguments.size()) {
      Usage();
    }
    // Start REPL loop
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
  usage += L"Usage: obi [options]\n\n";
  usage += L"Options:\n";
  usage += L"  --help, -h              Show this help message\n";
  usage += L"  --file, -f <files>      Source files (comma-separated)\n";
  usage += L"  --inline, -i <code>     Inline source code statements\n";
  usage += L"  --library, -l <libs>    Linked libraries (comma-separated)\n";
  usage += L"  --optimize, -o <level>  Optimization level: s0-s3 (default: s3)\n";
  usage += L"  --quit, -q              Exit shell after executing code\n";
  usage += L"\nExamples:\n";
  usage += L"  obi\n";
  usage += L"  obi --file hello.obs\n";
  usage += L"  obi -f hello.obs --library mylib\n";
  usage += L"  obi --inline 'Int->New(42)->PrintLine();' --quit\n";
  usage += L"\nVersion: ";
  usage += VERSION_STRING;

#if defined(_WIN64) && defined(_WIN32) && defined(_M_ARM64)
  usage += L" (arm64 Windows)";
#elif defined(_WIN64) && defined(_WIN32)
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