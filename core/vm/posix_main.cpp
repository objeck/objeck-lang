/***************************************************************************
 * Starting point for the VM.
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
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
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
    // enable UTF-8 environment
#ifdef _X64
    char* locale = setlocale(LC_ALL, ""); 
    std::locale lollocale(locale);
    setlocale(LC_ALL, locale); 
    wcout.imbue(lollocale);
#else    
    setlocale(LC_ALL, "en_US.utf8"); 
#endif
    
    return Execute(argc, argv);
  } 
  else {
    wstring usage;
    usage += L"Usage: obr <program>\n\n";
    usage += L"Example: \"obr hello.obe\"\n\nVersion: ";
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
#elif _X64
    usage += L" (Linux x86_64)";
#elif _ARM32
    usage += L" (Linux ARMv7)";
#else
    usage += L" (Linux x86)";
#endif
    
    usage += L"\nWeb: www.objeck.org";
    wcerr << usage << endl;

    return 1;
  }
}
