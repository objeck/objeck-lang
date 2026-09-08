/***************************************************************************
 * VM command-line options, shared by every platform's main.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck team nor the
 * names of its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifndef __VM_OPTIONS_H__
#define __VM_OPTIONS_H__

#include "../shared/sys.h"
#include "../shared/version.h"
#include <string>
#include <vector>

// Why this header exists.
//
// posix_main.cpp and win_main.cpp each carried their own copy of the option
// parsing and their own copy of the usage text, and the two had drifted: the
// POSIX build did not list --objeck-stdio at all and, worse, did not consume it,
// so the flag fell through to the program as an argument. Both copies of the
// size parser multiplied a 'g' suffix by 1099511627776 -- that is 2^40, a
// terabyte -- so --gc-threshold=1g asked for a thousand times what it said.
// And --objeck-stdio accepted any value while acting only on "binary": the help
// promised "binary mode if set", and --objeck-stdio=1 did nothing, silently.
//
// One parser, one usage string, one set of value rules. A value that is not
// among the ones documented is an ERROR that names what was expected, never a
// silent no-op.

// Set by --jit on the command line: 0 = not set, -1 = off, >0 = call count.
// Lives here rather than in the JIT header because the debugger build compiles
// interpreter.cpp without that header in scope; both the setter (interpreter.cpp)
// and the reader (jit_common.h) include this file.
inline long& JitAutoThresholdOverride() {
  static long override_value = 0;
  return override_value;
}

namespace Runtime {

  struct VmOptions {
    // 0 means the VM default.
    size_t gc_threshold = 0;
    // 0 = default call count, -1 = JIT off, >0 = calls before a method is compiled.
    long jit_threshold = 0;
    // "" (unset), "binary", "utf16" or "utf8". Acted on by Windows only.
    std::wstring stdio_mode;
    // "" means OBJECK_LIB_PATH or the install default. This is the lib ROOT
    // (the directory the .obl files live in), not lib/native: the VM appends
    // native/ to it for libobjk_*, reads cacert.pem from it, and reports it
    // as the lib_dir runtime property.
    std::wstring lib_path;
    // Leading argv entries the VM used for itself; the rest go to the program.
    int consumed = 0;
    // Non-empty means refuse to run and print this followed by the usage.
    std::wstring error;
  };

  // <number>(k|m|g): the whole string must be consumed, and the suffix is a
  // single optional letter. 'g' is 2^30 -- see the note above.
  inline bool ParseSizeSuffix(const std::wstring& text, size_t& out) {
    if(text.empty()) {
      return false;
    }

    size_t i = 0;
    size_t value = 0;
    while(i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
      value = value * 10 + (size_t)(text[i] - L'0');
      ++i;
    }
    if(i == 0) {
      return false;
    }

    if(i < text.size()) {
      switch(text[i]) {
      case L'k': case L'K': value *= 1024ULL; break;
      case L'm': case L'M': value *= 1048576ULL; break;
      case L'g': case L'G': value *= 1073741824ULL; break;
      default: return false;
      }
      ++i;
    }
    if(i != text.size()) {
      return false;
    }

    out = value;
    return true;
  }

  inline VmOptions ParseVmOptions(const CommandLineParseResult& cmd) {
    VmOptions opts;
    const auto& args = cmd.arguments;

    // --gc-threshold=<size>   (legacy --GC_THRESHOLD, --GC-THRESHOLD)
    {
      const std::vector<std::wstring> names = {L"gc-threshold", L"GC_THRESHOLD", L"GC-THRESHOLD"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(!ParseSizeSuffix(value, opts.gc_threshold)) {
          opts.error = L"--gc-threshold: expected <number>(k|m|g), got '" + value + L"'";
          return opts;
        }
      }
    }

    // --objeck-stdio=binary|utf16   (legacy --OBJECK_STDIO, --OBJECK-STDIO)
    //
    // Accepted on every platform so a script shared across them does not have
    // to know which one is running; it is COUNTED everywhere and acted on by
    // Windows only, which the usage says.
    {
      const std::vector<std::wstring> names = {L"objeck-stdio", L"OBJECK_STDIO", L"OBJECK-STDIO"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(value == L"binary" || value == L"utf16" || value == L"utf8") {
          opts.stdio_mode = value;
        }
        else {
          opts.error = L"--objeck-stdio: expected 'binary', 'utf16' or 'utf8', got '" + value + L"'";
          return opts;
        }
      }
    }

    // --jit=off|<calls>   (env OBJECK_JIT_DISABLE=1 / OBJECK_JIT_THRESHOLD=N remain)
    {
      const std::vector<std::wstring> names = {L"jit", L"JIT"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(value == L"off") {
          opts.jit_threshold = -1;
        }
        else {
          long calls = 0;
          bool ok = !value.empty();
          for(wchar_t c : value) {
            if(c < L'0' || c > L'9') { ok = false; break; }
            calls = calls * 10 + (c - L'0');
          }
          if(!ok || calls < 1) {
            opts.error = L"--jit: expected 'off' or a positive call count, got '" + value + L"'";
            return opts;
          }
          opts.jit_threshold = calls;
        }
      }
    }

    // --lib-path=<dir>   (env OBJECK_LIB_PATH remains)
    {
      const std::vector<std::wstring> names = {L"lib-path", L"LIB_PATH", L"LIB-PATH"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(value.empty()) {
          opts.error = L"--lib-path: expected a directory";
          return opts;
        }
        opts.lib_path = value;
      }
    }

    return opts;
  }

  // Defined in interpreter.cpp, which can see the JIT headers; the mains cannot
  // include those without dragging the whole interpreter in.
  void SetJitAutoThreshold(long threshold);

  // Applies the options that must be in place before the program loads: the
  // library directory (the loader reads OBJECK_LIB_PATH, so the flag sets the
  // same variable in-process and the two paths cannot disagree) and the JIT
  // policy (consulted on the first method call).
  inline void ApplyVmOptions(const VmOptions& opts) {
    if(!opts.lib_path.empty()) {
      const std::string narrow = UnicodeToBytes(opts.lib_path);
#ifdef _WIN32
      _putenv_s("OBJECK_LIB_PATH", narrow.c_str());
#else
      setenv("OBJECK_LIB_PATH", narrow.c_str(), 1);
#endif
    }
    if(opts.jit_threshold != 0) {
      SetJitAutoThreshold(opts.jit_threshold);
    }
  }

  // The one usage text. Every flag states its valid values, because a flag
  // documented as "<value>" invites "1" and "true", and both used to be
  // accepted and ignored.
  inline std::wstring VmUsage() {
    std::wstring usage;
    usage += L"Usage: obr [options] <program> [program arguments]\n\n";
    usage += L"Options:\n";
    usage += L"  --gc-threshold=<size>     Initial garbage-collection threshold\n";
    usage += L"                            <size> is <number>(k|m|g), e.g. 512k, 2m, 1g\n";
    usage += L"                            Legacy: --GC_THRESHOLD=<size>\n";
    usage += L"  --jit=off|<calls>         JIT control: 'off' runs the interpreter only;\n";
    usage += L"                            a positive number is how many calls a method\n";
    usage += L"                            makes before it is compiled (default 10)\n";
    usage += L"                            Env: OBJECK_JIT_DISABLE=1, OBJECK_JIT_THRESHOLD=<calls>\n";
    usage += L"  --lib-path=<dir>          Objeck lib root: obr loads <dir>/native/libobjk_*\n";
    usage += L"                            and <dir>/cacert.pem; obc reads <dir>/*.obl\n";
    usage += L"                            (Windows: SDL2/onnx/opencv DLLs stay beside obr.exe)\n";
    usage += L"                            Env: OBJECK_LIB_PATH=<dir>\n";
    usage += L"  --objeck-stdio=binary|utf16|utf8\n";
    usage += L"                            Console I/O mode (Windows only; accepted and\n";
    usage += L"                            ignored elsewhere)\n";
    usage += L"                            Legacy: --OBJECK_STDIO=binary\n";
    usage += L"\nExamples:\n";
    usage += L"  obr hello.obe\n";
    usage += L"  obr --gc-threshold=2m hello.obe\n";
    usage += L"  obr --jit=off hello.obe          (does it still happen without the JIT?)\n";
    usage += L"  obr --lib-path=/opt/objeck/lib hello.obe\n";
    usage += L"  obr --objeck-stdio=binary hello.obe\n";
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
#elif _ARM64
    usage += L" (ARM64 Linux)";
#elif _X64
    usage += L" (x86_64 Linux)";
#elif _ARM32
    usage += L" (ARMv7 Linux)";
#else
    usage += L" (x86 Linux)";
#endif

    usage += L"\nWeb: https://www.objeck.org";
    return usage;
  }
}

#endif
