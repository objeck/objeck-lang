/***************************************************************************
* Starting point of the language compiler
*
* Copyright (c) 2008-2020, Randy Hollines
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

using namespace std;

/****************************
* Program start. Parses command
* line parameters.
****************************/
int main(int argc, char* argv[])
{
  wstring usage;
  usage += L"Usage: obc -src <source files> <options> -dest <output file>\n\n";
  usage += L"Options:\n";
  usage += L"  -src:   [input]  source files (separated by ',')\n";
  usage += L"  -in:    [input]  inlined code instead of source files\n";
  usage += L"  -lib:   [input]  linked libraries (separated by ',')\n";
  usage += L"  -ver:   [input]  displays the compiler version number\n";
  usage += L"  -tar:   [output] target type 'lib' for linkable library or 'exe' for\n";
  usage += L"          executable default is 'exe'\n";
  usage += L"  -dest:  [output] file name\n";
  usage += L"  -opt:   [option] compiler optimizations s0-s3 (s3 being the most aggressive)\n";
  usage += L"          default is s3\n";
  usage += L"  -alt:   [option][end-flag] allow use of C++ like syntax verses the UML like default\n";
  usage += L"  -debug: [option][end-flag] compile with debug symbols";
  usage += L"\n\nExample: \"obc -src ../examples/hello.obs -dest hello.obe\"\n\nVersion: ";
  usage += VERSION_STRING;
  
#if defined(_WIN64) && defined(_WIN32)
  usage += L" (x86_64 Windows)";
#elif _WIN32
  usage += L" (x86 Windows)";
#elif _OSX
  usage += L" (x86_64 macOS)";
#elif _X64
  usage += L" (x86_64 Linux)";
#elif _ARM32
  usage += L" (ARMv7 Linux)";
#else
  usage += L" (x86 Linux)";
#endif

  int status;
  if(argc > 0) {
    // reconstruct command line
    string path;
    for(int i = 1; i < 32 && i < argc; ++i) {
      path += " ";
      char* cmd_param = argv[i];
      if(strlen(cmd_param) > 0 && cmd_param[0]  != L'\'' && 
        (strrchr(cmd_param, L' ') || strrchr(cmd_param, L'\t'))) {
          path += "'";
          path += cmd_param;
          path += "'";
      }
      else {
        path += cmd_param;
      }
    }    
    const wstring path_string(path.begin(), path.end());
    
    // get command line parameters
    list<wstring> argument_options;
    map<const wstring, wstring> arguments = ParseCommnadLine(path_string);
    for(map<const wstring, wstring>::iterator intr = arguments.begin(); intr != arguments.end(); ++intr) {
      argument_options.push_back(intr->first);
    }
    
#ifdef _DEBUG
    OpenLogger("debug.log");
#endif

    // compile source with options
    status = OptionsCompile(arguments, argument_options, usage);

#ifdef _DEBUG
    CloseLogger();
#endif
  } 
  else {
    wcerr << usage << endl << endl;
    status = USAGE_ERROR;
  }

  return status;
}
