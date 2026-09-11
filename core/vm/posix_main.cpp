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
#include "vm_options.h"
#include "../shared/version.h"
#include <iostream>
#include <string>
#include <exception>
#include <new>

using namespace std;

// Renamed from main so the backstop below covers the whole body. main had no
// try/catch, so anything thrown during argument parsing or setup escaped and
// called terminate() instead of reporting. Mirrors win_main.cpp, which already
// reports on these same failures.
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
      wcerr << opts.error << L"\n\n" << Runtime::VmUsage() << endl;
      return 1;
    }
    Runtime::ApplyVmOptions(opts);

    // enable UTF-8 environment
#if defined(_X64) || defined(_ARM64)
    char* locale = setlocale(LC_ALL, "");
    // Bytecode string literals are UTF-8. If the ambient locale is not UTF-8
    // (e.g. LANG unset -> "C"), they can neither be decoded (BytesToUnicode ->
    // mbstowcs, at load) nor printed (wcout), so obr aborts with
    // ">>> Unable to read unicode std::string <<<". Switch to a UTF-8 locale:
    // "C.UTF-8" is always present on glibc and keeps '.' as the decimal
    // separator (LC_NUMERIC unchanged for float output); "UTF-8" is the macOS
    // spelling. An ambient UTF-8 locale is left untouched.
    {
      const std::string loc(locale ? locale : "");
      if(loc.find("UTF-8") == std::string::npos && loc.find("utf8") == std::string::npos &&
         loc.find("UTF8") == std::string::npos) {
        char* utf8 = setlocale(LC_ALL, "C.UTF-8");
        if(!utf8) { utf8 = setlocale(LC_ALL, "en_US.UTF-8"); }
        if(!utf8) { utf8 = setlocale(LC_ALL, "UTF-8"); }  // macOS
        if(utf8) { locale = utf8; }
      }
    }
    // Built by ConsoleLocale (vm.cpp), which never throws: libc++ on macOS
    // refuses the per-category composite the C library returns when only
    // LC_CTYPE is set ("C/C.UTF-8/C/C/C/C", what Python 3 hands every child
    // when LANG is unset), and std::locale's constructor threw out of here
    // with obr exiting before the program ran.
    std::locale lollocale = ConsoleLocale(locale);
    setlocale(LC_ALL, locale);
    wcout.imbue(lollocale);
#else
    setlocale(LC_ALL, "en_US.utf8");
#endif
    
    //
    // Note: OBJECK_STDIO not needed for POSIX-like environments, ignore for MSYS2
    //
#ifdef _WIN32
    return Execute(argc - opts.consumed, argv + opts.consumed, false, opts.gc_threshold);
#else    
    Execute(argc - opts.consumed, argv + opts.consumed, opts.gc_threshold);
#endif    

    // The POSIX branch above deliberately discards Execute's result, so this
    // path used to fall off the end of main -- legal there (implicit return 0)
    // but undefined behaviour now that the body lives in an ordinary function.
    // At -O3 no return instruction is emitted and control runs into garbage,
    // which segfaulted obr on every POSIX target while Windows was unaffected
    // because it takes the returning branch above. Returning 0 preserves the
    // original exit status exactly.
    return 0;
  }
  else {
    wcerr << Runtime::VmUsage() << endl;

    return 1;
  }
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
