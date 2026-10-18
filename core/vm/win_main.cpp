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
#include "vm_options.h"
#include "windows.h"
#include "../shared/version.h"
#include <iostream>
#include <exception>
#include <new>

#ifdef _MSYS2_CLANG
#include <locale>
#include <codecvt>
#endif

bool SetStdIo(const char* value);

// program start
// Renamed from main so the backstop below covers the ENTIRE body. The existing
// try/catch inside started well after the opening brace, leaving the whole
// prologue -- argument parsing and the wstring/usage construction that can
// throw bad_alloc -- outside any handler, where a throw calls terminate().
static int objeck_main(const int argc, const char* argv[])
{
  if(argc > 1) {
    //
    // Parse command line parameters using enhanced parser
    //
    CommandLineParseResult cmd_result = ParseCommandLine(argc, argv);

    // One parser for every platform (vm_options.h). A bad value is an error
    // that names what was expected, not something silently ignored.
    const Runtime::VmOptions opts = Runtime::ParseVmOptions(cmd_result);
    if(!opts.error.empty()) {
      std::wcerr << opts.error << L"\n\n" << Runtime::VmUsage() << std::endl;
      return 1;
    }
    // Every argument was a flag, so there is no program to run. Execute
    // returns USAGE_ERROR for that without printing a word; say what a bare
    // "obr" says instead, and exit the way it does.
    if(argc - opts.consumed < 2) {
      std::wcerr << Runtime::VmUsage() << std::endl;
      return 1;
    }
    Runtime::ApplyVmOptions(opts);

    //
    // console mode: the flag, else the environment, else the UTF-8 default
    //
    // SetStdIo's result is what Execute is told. The old code called SetStdIo
    // for the flag and discarded the result, so --objeck-stdio=binary switched
    // the console and then told Execute it had not.
    bool is_stdio_binary = false;
    if(!opts.stdio_mode.empty()) {
      const std::string narrow = UnicodeToBytes(opts.stdio_mode);
      is_stdio_binary = SetStdIo(narrow.c_str());
    }
    else {
      size_t value_len;
      char value[SMALL_BUFFER_MAX];
      if(!getenv_s(&value_len, value, SMALL_BUFFER_MAX, "OBJECK_STDIO") && strlen(value) > 0) {
        is_stdio_binary = SetStdIo(value);
      }
      else {
        SetEnv();
      }
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
      try {
        status = Execute(argc - opts.consumed, argv + opts.consumed, is_stdio_binary, opts.gc_threshold);
      }
      catch(const std::bad_alloc&) {
        std::wcerr << L">>> virtual machine: out of memory <<<" << std::endl;
        status = SYSTEM_ERROR;
      }
      catch(const std::exception& e) {
        const std::string msg(e.what());
        std::wcerr << L">>> virtual machine: " << std::wstring(msg.begin(), msg.end()) << L" <<<" << std::endl;
        status = SYSTEM_ERROR;
      }
      catch(...) {
        std::wcerr << L">>> virtual machine: unexpected error <<<" << std::endl;
        status = SYSTEM_ERROR;
      }
    }

    // No WSACleanup() here. The process is exiting, so Windows reclaims every
    // Winsock resource regardless -- but WSACleanup also unblocks threads still
    // parked in accept()/recv(), which is precisely the wrong thing to do on the
    // way out: such a thread wakes into VM code and runs against a program image
    // that teardown has already released. That was the whole of issue #681 (a
    // server started with WebServer->Serve, then a normal return from Main):
    // 8 segfaults in 8 runs became 0 in 8 with this single call removed, and it
    // is why the same program was always clean on Linux, which has no equivalent.
    // Same reasoning as the getaddrinfo failure path in arch/win32/win32.cpp.
    return status;
  }
  else {
    std::wcerr << Runtime::VmUsage() << std::endl;

    return 1;
  }

  return 1;
}

int main(const int argc, const char* argv[])
{
  try {
    return objeck_main(argc, argv);
  }
  catch(const std::bad_alloc&) {
    std::wcerr << L">>> virtual machine: out of memory <<<" << std::endl;
  }
  catch(const std::exception& e) {
    const std::string msg(e.what());
    std::wcerr << L">>> virtual machine: " << std::wstring(msg.begin(), msg.end()) << L" <<<" << std::endl;
  }
  catch(...) {
    std::wcerr << L">>> virtual machine: unexpected error <<<" << std::endl;
  }

  return 1;
}

#ifndef _MSYS2_CLANG
//
// Wide output as UTF-8 bytes, for a stdout in binary mode. On a binary stream
// the C runtime writes each wchar_t as its two bytes, so with OBJECK_STDIO=binary
// every program's output -- the LSP's framed replies over stdio among them --
// left as UTF-16LE, which no client reads: "Content-Length" arrived as
// "C\0o\0n\0...". Input was already read as bytes and decoded from UTF-8.
//
// Unbuffered, so the bytes keep their order with anything written to stdout
// directly. A surrogate pair split across two calls is held until its second
// half arrives.
//
class Utf8StdoutBuffer : public std::wstreambuf {
  FILE* file;
  wchar_t pending_high;

  void Put(char32_t code) {
    char bytes[4];
    size_t size;
    if(code < 0x80) {
      bytes[0] = (char)code;
      size = 1;
    }
    else if(code < 0x800) {
      bytes[0] = (char)(0xc0 | (code >> 6));
      bytes[1] = (char)(0x80 | (code & 0x3f));
      size = 2;
    }
    else if(code < 0x10000) {
      bytes[0] = (char)(0xe0 | (code >> 12));
      bytes[1] = (char)(0x80 | ((code >> 6) & 0x3f));
      bytes[2] = (char)(0x80 | (code & 0x3f));
      size = 3;
    }
    else {
      bytes[0] = (char)(0xf0 | (code >> 18));
      bytes[1] = (char)(0x80 | ((code >> 12) & 0x3f));
      bytes[2] = (char)(0x80 | ((code >> 6) & 0x3f));
      bytes[3] = (char)(0x80 | (code & 0x3f));
      size = 4;
    }
    fwrite(bytes, 1, size, file);
  }

  void PutUnit(wchar_t unit) {
    if(pending_high) {
      const wchar_t high = pending_high;
      pending_high = 0;
      if(unit >= 0xdc00 && unit <= 0xdfff) {
        Put(0x10000 + (((char32_t)high - 0xd800) << 10) + ((char32_t)unit - 0xdc00));
        return;
      }
      // a lone high surrogate: U+FFFD, as a decoder would show it
      Put(0xfffd);
    }

    if(unit >= 0xd800 && unit <= 0xdbff) {
      pending_high = unit;
    }
    else if(unit >= 0xdc00 && unit <= 0xdfff) {
      Put(0xfffd);
    }
    else {
      Put((char32_t)unit);
    }
  }

protected:
  int_type overflow(int_type c) override {
    if(!traits_type::eq_int_type(c, traits_type::eof())) {
      PutUnit(traits_type::to_char_type(c));
    }
    return traits_type::not_eof(c);
  }

  std::streamsize xsputn(const wchar_t* s, std::streamsize n) override {
    for(std::streamsize i = 0; i < n; ++i) {
      PutUnit(s[i]);
    }
    return n;
  }

  int sync() override {
    return fflush(file) == 0 ? 0 : -1;
  }

public:
  Utf8StdoutBuffer(FILE* f) : file(f), pending_high(0) {}
};
#endif

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

    // wide output goes out as UTF-8 bytes; lives for the whole process
    static Utf8StdoutBuffer utf8_stdout(stdout);
    std::wcout.rdbuf(&utf8_stdout);

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
