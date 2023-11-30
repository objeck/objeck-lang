/***************************************************************************
 * Starting point for the VM on Linux and macOS
 *
 * Copyright (c) 2008-2017 Randy Hollines
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
    // check for command line parameters
    //  
    size_t gc_threshold = 0;
    int vm_param_count = 0;
    
    // bool set_foo_bar_param = false; // TODO: add if needed
    for(int i = 1; i < argc; ++i) {
      const std::string name_value(argv[i]);
      // check for GC_THRESHOLD
      if(!name_value.rfind("--GC_THRESHOLD=", 0)) {
        const size_t name_value_index = name_value.find_first_of(L'=');
        if(name_value_index != std::string::npos) {
          ++vm_param_count;
          const std::string value(name_value.substr(name_value_index + 1));

          char* str_end;
          gc_threshold = strtol(value.c_str(), &str_end, 10);
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
      }
      /* TODO: add if needed
      // check for FOO_BAR
      else if(!name_value.rfind("--FOO_BAR=", 0)) {
      }
      */
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
    setlocale(LC_ALL, "en_US.utf8");
#else    
    setlocale(LC_ALL, "en_US.utf8"); 
#endif
    
    //
    // Note: OBJECK_STDIO not needed for POSIX-like environments, ignore for MSYS2
    //
#ifdef _MSYS2
    return Execute(argc - vm_param_count, argv + vm_param_count, false, gc_threshold);
#else    
    Execute(argc - vm_param_count, argv + vm_param_count, gc_threshold);
#endif    
  } 
  else {
    wstring usage;
    usage += L"Usage: obr <program>\n\n";

    usage += L"Options:\n";
    usage += L"\t--GC_THRESHOLD:\t[prepend] inital garbage collection threshold <number>(k|m|g)\n";

    usage += L"\nExample: \"obr hello.obe\"\n\nVersion: ";
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
