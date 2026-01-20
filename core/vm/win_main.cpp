/***************************************************************************
 * Starting point for the VM in Windows
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

bool SetStdIo(const char* value);

// program start
int main(const int argc, const char* argv[])
{
  if(argc > 1) {
    //
    // Parse command line parameters using enhanced parser
    //
    CommandLineParseResult cmd_result = ParseCommandLine(argc, argv);

    bool set_stdio_param = false;
    size_t gc_threshold = 0;
    int vm_param_count = 0;

    // Check for OBJECK_STDIO (Windows-only, support both new and legacy formats)
    std::wstring stdio_value = GetCommandLineArgumentWithAliases(
      cmd_result.arguments,
      {L"objeck-stdio", L"OBJECK_STDIO", L"OBJECK-STDIO"},
      L""
    );

    if(!stdio_value.empty()) {
      ++vm_param_count;

      // Convert wide string to narrow for SetStdIo
      std::string value_str;
      for(wchar_t wc : stdio_value) {
        value_str += static_cast<char>(wc);
      }

      SetStdIo(value_str.c_str());
      set_stdio_param = true;
    }

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

    //
    // environment variables
    //

    // check for OBJECK_STDIO
    bool is_stdio_binary = false;
    size_t value_len;
    char value[SMALL_BUFFER_MAX];
    if(!set_stdio_param) {
      if(!getenv_s(&value_len, value, SMALL_BUFFER_MAX, "OBJECK_STDIO") && strlen(value) > 0) {
        is_stdio_binary = SetStdIo(value);
      }
      // set default as utf8
      else {
        SetEnv();
      }
    }
    /* TODO: add if needed
    // check for FOO_BAR
    else if(!set_foo_bar_param) {
      if(!getenv_s(&value_len, value, SMALL_BUFFER_MAX, "FOO_BAR") && strlen(value) > 0) {
      }
      else {
      }
    }
    */

    // initialize Winsock
    WSADATA data;
    int status;
    if(WSAStartup(MAKEWORD(2, 2), &data)) {
      std::wcerr << L"Unable to load Winsock 2.2!" << std::endl;
      status = SYSTEM_ERROR;
    }
    else {
      // execute program
      status = Execute(argc - vm_param_count, argv + vm_param_count, is_stdio_binary, gc_threshold);
    }

    // release Winsock
    WSACleanup();
    return status;
  }
  else {
    std::wstring usage;
    usage += L"Usage: obr [options] <program>\n\n";

    usage += L"Options:\n";
    usage += L"  --objeck-stdio=<value>    STDIO output mode (binary mode if set)\n";
    usage += L"                            Legacy: --OBJECK_STDIO=<value>\n";
    usage += L"  --gc-threshold=<size>     Initial garbage collection threshold\n";
    usage += L"                            Size format: <number>(k|m|g)\n";
    usage += L"                            Legacy: --GC_THRESHOLD=<size>\n";
    usage += L"\nExamples:\n";
    usage += L"  obr hello.obe\n";
    usage += L"  obr --gc-threshold=2m hello.obe\n";
    usage += L"  obr --objeck-stdio=binary hello.obe\n";
    usage += L"  obr --GC_THRESHOLD=2m hello.obe  (legacy)\n";
    usage += L"\nVersion: ";

    usage += VERSION_STRING;
    
#if defined(_WIN64) && defined(_WIN32) && defined(_M_ARM64)
    usage += L" (arm64 Windows)";
#elif defined(_WIN64) && defined(_WIN32)
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

bool SetStdIo(const char* value)
{
  // set as binary
  if(!strcmp("binary", value)) {
#ifndef _MSYS2_CLANG
    if(_setmode(_fileno(stdin), _O_BINARY) < 0) {
      std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
      exit(1);
    }

    if(_setmode(_fileno(stdout), _O_BINARY) < 0) {
      std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
      exit(1);
    }

    return true;
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
      std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
      exit(1);
    }

    if(_setmode(_fileno(stdout), _O_U16TEXT) < 0) {
      std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
      exit(1);
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
      std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
      exit(1);
    }

    if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
      std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
      exit(1);
    }
#endif
  }

  return false;
}
