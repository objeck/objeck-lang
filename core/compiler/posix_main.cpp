/***************************************************************************
* Starting point of the language compiler
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


#define USAGE_ERROR -1
#define SYSTEM_ERROR -2

#include "compiler.h"
#include "../shared/version.h"
#include <iostream>
#include <string>
#include <list>
#include <map>

/****************************
* Program start. Parses command
* line parameters.
****************************/
int main(int argc, char* argv[])
{
  std::wstring usage;
  usage += L"Usage: obc -src <source files> <optionals> -dest <output file>\n\n";
  usage += L"Options:\n";
  usage += L"  -src:    [input] source files (separated by commas)\n";
  usage += L"  -in:     [input] inline source code statements instead of specifying files\n";
  usage += L"  -lib:    [input] list of linked libraries (separated by commas)\n";
  usage += L"  -ver:    [input] displays the compiler version\n";
  usage += L"  -tar:    [output] target type 'lib' for linkable library or 'exe' for executable (the default)\n";
  usage += L"  -dest:   [output] output file name\n";
  usage += L"  -asm:    [output][end-flag] emits a human readable debug byte assembly file\n";
  usage += L"  -opt:    [optional] compiler optimizations s0-s3 (s3 being the most aggressive and default)\n";
  usage += L"  -alt:    [optional][end-flag] use alternative C like syntax\n";
  usage += L"  -debug:  [optional][end-flag] compile with debug symbols\n";
  usage += L"  -strict: [input][end-flag] exclude default system libraries and specify them manually\n";
  usage += L"\nExample: \"obc -src hello.obs\"\n\nVersion: ";
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

  int status;
  if(argc > 0) {
    // parse command line
    std::wstring cmd_line;    
    std::map<const std::wstring, std::wstring> arguments = ParseCommnadLine(argc, argv, cmd_line);

    // single command line optional is the source file
    if(argc == 2 && arguments.empty()) {
      arguments[L"src"] = cmd_line.erase(0, 1);
    }
    
    std::list<std::wstring> argument_optionals;
    for(std::map<const std::wstring, std::wstring>::iterator intr = arguments.begin(); intr != arguments.end(); ++intr) {
      argument_optionals.push_back(intr->first);
    }
    
#ifdef _DEBUG
    OpenLogger("debug.log");
#endif

    // compile source with optionals
    status = OptionsCompile(arguments, argument_optionals, usage);

#ifdef _DEBUG
    CloseLogger();
#endif
  } 
  else {
    std::wcerr << usage << std::endl;
    status = USAGE_ERROR;
  }

  return status;
}
