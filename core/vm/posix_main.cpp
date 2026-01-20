/***************************************************************************
 * Starting point for the VM on Linux and macOS x64
 *
 * Copyright (c) 2008-2024 Randy Hollines
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include "vm.h"
#include "../shared/version.h"
#include <iostream>
#include <string>

using namespace std;

int main(const int argc, const char* argv[])
{
  if(argc > 1) {
    //
    // Parse command line parameters using enhanced parser
    //
    CommandLineParseResult cmd_result = ParseCommandLine(argc, argv);

    size_t gc_threshold = 0;
    int vm_param_count = 0;

    // Check for GC threshold (support both new and legacy formats)
    std::wstring gc_value = GetCommandLineArgumentWithAliases(
      cmd_result.arguments,
      {L"gc-threshold", L"GC_THRESHOLD", L"GC-THRESHOLD"},
      L""
    );

    if(!gc_value.empty()) {
      ++vm_param_count;

      // Convert wide string to narrow for parsing
      std::string value_str;
      for(wchar_t wc : gc_value) {
        value_str += static_cast<char>(wc);
      }

      // Parse numeric value and suffix
      char* str_end;
      gc_threshold = strtol(value_str.c_str(), &str_end, 10);
      if(str_end) {
        switch (*str_end) {
        case 'k':
        case 'K':
          gc_threshold *= 1024UL;
          break;

        case 'm':
        case 'M':
          gc_threshold *= 1048576UL;
          break;

        case 'g':
        case 'G':
          gc_threshold *= 1099511627776UL;
          break;
        }
      }
    }

    // enable UTF-8 environment
#if defined(_X64)
    char* locale = setlocale(LC_ALL, ""); 
    std::locale lollocale(locale);
    setlocale(LC_ALL, locale); 
    wcout.imbue(lollocale);
#elif defined(_ARM64)
    char* locale = setlocale(LC_ALL, "");
    std::locale lollocale(locale);
    setlocale(LC_ALL, locale);
    wcout.imbue(lollocale);
    setlocale(LC_ALL, "en_US.UTF-8");
#else
    setlocale(LC_ALL, "en_US.utf8"); 
#endif
    
    //
    // Note: OBJECK_STDIO not needed for POSIX-like environments, ignore for MSYS2
    //
#ifdef _WIN32
    return Execute(argc - vm_param_count, argv + vm_param_count, false, gc_threshold);
#else    
    Execute(argc - vm_param_count, argv + vm_param_count, gc_threshold);
#endif    
  }
  else {
    wstring usage;
    usage += L"Usage: obr [options] <program>\n\n";

    usage += L"Options:\n";
    usage += L"  --gc-threshold=<size>     Initial garbage collection threshold\n";
    usage += L"                            Size format: <number>(k|m|g)\n";
    usage += L"                            Legacy: --GC_THRESHOLD=<size>\n";
    usage += L"\nExamples:\n";
    usage += L"  obr hello.obe\n";
    usage += L"  obr --gc-threshold=2m hello.obe\n";
    usage += L"  obr --GC_THRESHOLD=2m hello.obe  (legacy)\n";
    usage += L"\nVersion: ";
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
    wcerr << usage << endl;

    return 1;
  }
}
