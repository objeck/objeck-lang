/***************************************************************************
* Starting point of the language compiler
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
int main(int argc, const char* argv[])
{
  std::wstring usage;
  usage += L"Usage: obc [options] <source files>\n\n";
  usage += L"Options:\n";
  usage += L"  --source, -src, -s <files>      Source files (comma-separated)\n";
  usage += L"  --destination, -dest, -d <file> Output file name\n";
  usage += L"  --library, -lib, -l <libs>      Linked libraries (comma-separated)\n";
  usage += L"  --target, -tar, -t <type>       Target: 'lib' or 'exe' (default: exe)\n";
  usage += L"  --optimize, -opt, -o <level>    Optimization: s0-s3 (default: s3)\n";
  usage += L"  --inline, -in, -i <code>        Inline code statements\n";
  usage += L"  --debug, -D                     Include debug symbols\n";
  usage += L"  --alt-syntax, -alt              Use alternative C-like syntax\n";
  usage += L"  --assembly, -asm, -a            Emit assembly file\n";
  usage += L"  --strict                        Exclude default libraries\n";
  usage += L"  --version, -ver, -v             Show version\n";
  usage += L"\nExamples:\n";
  usage += L"  obc hello.obs\n";
  usage += L"  obc --source hello.obs --destination hello.obe\n";
  usage += L"  obc -s hello.obs -d hello.obe -o s3 -D\n";
  usage += L"  obc -src file1.obs,file2.obs --debug\n";
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

  int status;
  if(argc > 0) {
    // Parse command line using enhanced parser
    CommandLineParseResult cmd_result = ParseCommandLine(argc, argv);
    std::map<const std::wstring, std::wstring> cmd_options = cmd_result.arguments;

    // Look for source file as first parameter (support --source, -src, -s)
    if(argc > 1 && !HasCommandLineArgumentWithAliases(cmd_options, {L"source", L"src", L"s"})) {
      const std::wstring& cmd_line = cmd_result.reconstructed_path;
      const size_t space_delim_index = cmd_line.find(L' ', 1);
      if(space_delim_index > 0 && space_delim_index != std::wstring::npos) {
        cmd_options[L"src"] = cmd_line.substr(1, space_delim_index - 1);
      }
      else {
        cmd_options[L"src"] = cmd_line.substr(1);
      }
    }

    std::list<std::wstring> argument_optionals;
    for(std::map<const std::wstring, std::wstring>::iterator intr = cmd_options.begin(); intr != cmd_options.end(); ++intr) {
      argument_optionals.push_back(intr->first);
    }
    
#ifdef _DEBUG
    OpenLogger("debug.log");
#endif

    // compile source with options
    status = OptionsCompile(cmd_options, argument_optionals, usage);

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
