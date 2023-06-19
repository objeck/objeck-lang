/***************************************************************************
 * Starting point for the VM in Windows
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

#define SYSTEM_ERROR -2

#ifdef _DEBUG
// #include "vld.h"
#endif

#include "vm.h"
#include "windows.h"
#include "../shared/version.h"
#include <iostream>

#ifdef _MSYS2_CLANG
#include <locale>
#include <codecvt>
#endif

// program start
int main(const int argc, const char* argv[])
{
  if(argc > 1) {
    size_t value_len;
    char value[LARGE_BUFFER_MAX];
    if(getenv_s(&value_len, value, LARGE_BUFFER_MAX, "OBJECK_STDIO")) {
      // set as binary
      if(!strcmp("binary", value)) {
#ifndef _MSYS2_CLANG
        if(_setmode(_fileno(stdin), _O_BINARY) < 0) {
          return 1;
        }

        if(_setmode(_fileno(stdout), _O_BINARY) < 0) {
          return 1;
        }
#endif
      }
      // set as utf16
      else if(!strcmp("utf16", value)) {
#ifdef _MSYS2_CLANG
        std::ios_base::sync_with_stdio(false);
        std::locale utf16(std::locale(), new std::codecvt_utf16<wchar_t>);
        std::wcout.imbue(utf16);
        std::wcin.imbue(utf16);
#else
        if(_setmode(_fileno(stdin), _O_U16TEXT) < 0) {
          return 1;
        }

        if(_setmode(_fileno(stdout), _O_U16TEXT) < 0) {
          return 1;
        }
#endif
      }
      // set as utf8
      else if(!strcmp("utf8", value)) {
#ifdef _MSYS2_CLANG
        std::ios_base::sync_with_stdio(false);
        std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
        std::wcout.imbue(utf8);
        std::wcin.imbue(utf8);
#else
        if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
          return 1;
        }

        if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
          return 1;
        }
#endif
      }
    }
    // set default as utf8
    else {
#ifdef _MSYS2_CLANG
      std::ios_base::sync_with_stdio(false);
      std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
      std::wcout.imbue(utf8);
      std::wcin.imbue(utf8);
#else
      if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
        return 1;
      }

      if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
        return 1;
      }
#endif
    }

    // initialize Winsock
    WSADATA data;
    int status;
    if(WSAStartup(MAKEWORD(2, 2), &data)) {
      std::wcerr << L"Unable to load Winsock 2.2!" << std::endl;
      status = SYSTEM_ERROR;
    }
    else {
      // execute program
      status = Execute(argc, argv);
    }

    // release Winsock
    WSACleanup();
    return status;
  }
  else {
    std::wstring usage;
    usage += L"Usage: obr <program>\n\n";
    usage += L"Example: \"obr hello.obe\"\n\nVersion: ";
    usage += VERSION_STRING;
    
#if defined(_WIN64) && defined(_WIN32)
    usage += L" (x86_64 Windows)";
#elif _WIN32
    usage += L" (x86 Windows)";
#elif _OSX
#ifdef _ARM64
    usage += L" (ARM64 macOS)";
#else
    usage += L" (x86_64 macOS)";
#endif
#elif _X64
    usage += L" (x86_64 Linux)";
#elif _ARM32
    usage += L" (ARMv7 Linux)";
#else
    usage += L" (x86 Linux)";
#endif
    
    usage += L"\nWeb: https://www.objeck.org";
    std::wcerr << usage << std::endl;

    return 1;
  }

  return 1;
}
