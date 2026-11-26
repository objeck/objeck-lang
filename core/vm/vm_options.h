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
#include <climits>
#include <cstdint>
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
    // Nursery (young-generation) limit in bytes; 0 means the VM default (128m).
    // From --nursery, else OBJECK_NURSERY.
    size_t nursery_size = 0;
    // 0 = default call count, -1 = JIT off, >0 = calls before a method is compiled.
    long jit_threshold = 0;
    // Maximum call depth in frames; 0 means the VM default (CALL_DEPTH_DEFAULT).
    // From --call-depth, else OBJECK_CALL_DEPTH.
    long call_depth = 0;
    // From --thread-faults: -1 not given (OBJECK_THREAD_FAULTS decides, default exit),
    // 0 = an uncaught error in any thread ends the process, 1 = it ends only that thread.
    int thread_faults = -1;
    // "" (unset), "binary", "utf16" or "utf8". Acted on by Windows only.
    std::wstring stdio_mode;
    // "" means OBJECK_LIB_PATH or the install default. This is the lib ROOT
    // (the directory the .obl files live in), not lib/native: the VM appends
    // native/ to it for libobjk_*, reads cacert.pem from it, and reports it
    // as the lib_dir runtime property.
    std::wstring lib_path;
    // From --mcp / --mcp-http: serve the program's MCP tools, resources and prompts
    // instead of running its Main (docs/design/MCP_FIRST_CLASS.md, Stage 3).
    // 0 = not asked, 1 = standard input and output, 2 = streamable HTTP.
    int mcp_mode = 0;
    // --mcp-http=<port>, 0 to 65535 (0: any free port, which the server reports)
    long mcp_port = 0;
    // --mcp-http-host=<address>, default 127.0.0.1
    std::wstring mcp_host;
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
    // A size that does not fit is refused. It used to wrap, so
    // --gc-threshold=18446744073709551617 ran with a threshold of 1 byte.
    while(i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
      const size_t digit = (size_t)(text[i] - L'0');
      if(value > (SIZE_MAX - digit) / 10) {
        return false;
      }
      value = value * 10 + digit;
      ++i;
    }
    if(i == 0) {
      return false;
    }

    if(i < text.size()) {
      size_t scale = 1;
      switch(text[i]) {
      case L'k': case L'K': scale = 1024ULL; break;
      case L'm': case L'M': scale = 1048576ULL; break;
      case L'g': case L'G': scale = 1073741824ULL; break;
      default: return false;
      }
      if(value > SIZE_MAX / scale) {
        return false;
      }
      value *= scale;
      ++i;
    }
    if(i != text.size()) {
      return false;
    }

    out = value;
    return true;
  }

  // The nursery is carved out of a fixed 128 MB reservation, so it can shrink
  // but never grow past it. Below 64k nearly every allocation collects.
  const size_t NURSERY_SIZE_MIN = 64ULL * 1024ULL;
  const size_t NURSERY_SIZE_MAX = 128ULL * 1048576ULL;

  inline bool ParseNurserySize(const std::wstring& text, size_t& out) {
    size_t value = 0;
    if(!ParseSizeSuffix(text, value) || value < NURSERY_SIZE_MIN || value > NURSERY_SIZE_MAX) {
      return false;
    }
    out = value;
    return true;
  }

  // The range --call-depth accepts. Repeated from call_stack.h, which the mains
  // do not include; interpreter.cpp checks that the two agree.
  const long CALL_DEPTH_FLAG_MIN = 256;
  const long CALL_DEPTH_FLAG_MAX = 1048576;

  // a whole decimal number of frames from CALL_DEPTH_FLAG_MIN to CALL_DEPTH_FLAG_MAX
  inline bool ParseCallDepth(const std::wstring& text, long& out) {
    if(text.empty() || text.size() > 8) {
      return false;
    }
    long value = 0;
    for(wchar_t c : text) {
      if(c < L'0' || c > L'9') {
        return false;
      }
      value = value * 10 + (c - L'0');
    }
    if(value < CALL_DEPTH_FLAG_MIN || value > CALL_DEPTH_FLAG_MAX) {
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

    // --nursery=<size>   (env OBJECK_NURSERY=<size>; the flag wins, so a bad
    // variable is not even read when the flag is given)
    {
      const std::vector<std::wstring> names = {L"nursery", L"NURSERY"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(!ParseNurserySize(value, opts.nursery_size)) {
          opts.error = L"--nursery: expected <number>(k|m) from 64k to 128m, got '" + value + L"'";
          return opts;
        }
      }
      else {
        std::string narrow;
#ifdef _WIN32
        char buffer[64];
        size_t len = 0;
        if(!getenv_s(&len, buffer, sizeof(buffer), "OBJECK_NURSERY") && len > 1) {
          narrow = buffer;
        }
        else if(len > sizeof(buffer)) {
          narrow = "<too long>";
        }
#else
        const char* env = getenv("OBJECK_NURSERY");
        if(env) {
          narrow = env;
        }
#endif
        if(!narrow.empty()) {
          const std::wstring value(narrow.begin(), narrow.end());
          if(!ParseNurserySize(value, opts.nursery_size)) {
            opts.error = L"OBJECK_NURSERY: expected <number>(k|m) from 64k to 128m, got '" + value + L"'";
            return opts;
          }
        }
      }
    }

    // --call-depth=<frames>   (env OBJECK_CALL_DEPTH=<frames>; the flag wins, so a
    // bad variable is not even read when the flag is given)
    {
      const std::vector<std::wstring> names = {L"call-depth", L"CALL_DEPTH", L"CALL-DEPTH"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(!ParseCallDepth(value, opts.call_depth)) {
          opts.error = L"--call-depth: expected a number of frames from 256 to 1048576, got '" + value + L"'";
          return opts;
        }
      }
      else {
        std::string narrow;
#ifdef _WIN32
        char buffer[64];
        size_t len = 0;
        if(!getenv_s(&len, buffer, sizeof(buffer), "OBJECK_CALL_DEPTH") && len > 1) {
          narrow = buffer;
        }
        else if(len > sizeof(buffer)) {
          narrow = "<too long>";
        }
#else
        const char* env = getenv("OBJECK_CALL_DEPTH");
        if(env) {
          narrow = env;
        }
#endif
        if(!narrow.empty()) {
          const std::wstring value(narrow.begin(), narrow.end());
          if(!ParseCallDepth(value, opts.call_depth)) {
            opts.error = L"OBJECK_CALL_DEPTH: expected a number of frames from 256 to 1048576, got '" + value + L"'";
            return opts;
          }
        }
      }
    }

    // --thread-faults=exit|isolate   (env OBJECK_THREAD_FAULTS=isolate; the flag wins)
    {
      const std::vector<std::wstring> names = {L"thread-faults", L"THREAD_FAULTS", L"THREAD-FAULTS"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(value == L"isolate") {
          opts.thread_faults = 1;
        }
        else if(value == L"exit") {
          opts.thread_faults = 0;
        }
        else {
          opts.error = L"--thread-faults: expected 'exit' or 'isolate', got '" + value + L"'";
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
          // A count past a long (32 bits on Windows) is refused. It used to
          // overflow: --jit=4294967297 ran as --jit=1 on Windows.
          long calls = 0;
          bool ok = !value.empty();
          for(wchar_t c : value) {
            if(c < L'0' || c > L'9' || calls > (LONG_MAX - (c - L'0')) / 10) { ok = false; break; }
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

    // --mcp, --mcp=stdio, --mcp-http=<port>, --mcp-http-host=<address>
    //
    // A bare "--mcp" followed by the program is read by the command line parser as
    // "--mcp=<program>" (a flag takes the next word as its value); the VM then still
    // counts one entry, which is exactly the flag, so the program is where it
    // should be. A value other than "stdio" is therefore taken to be the program.
    {
      const std::vector<std::wstring> names = {L"mcp"};
      if(HasCommandLineArgumentWithAliases(args, names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, names, L"");
        if(value == L"http") {
          opts.error = L"--mcp: serves standard input and output; use --mcp-http=<port> for HTTP";
          return opts;
        }
        opts.mcp_mode = 1;
      }
      const std::vector<std::wstring> http_names = {L"mcp-http"};
      if(HasCommandLineArgumentWithAliases(args, http_names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, http_names, L"");
        long port = 0;
        bool ok = !value.empty() && value.size() <= 5;
        for(wchar_t c : value) {
          if(c < L'0' || c > L'9') { ok = false; break; }
          port = port * 10 + (c - L'0');
        }
        if(!ok || port > 65535) {
          opts.error = L"--mcp-http: expected a port from 0 to 65535, got '" + value + L"'";
          return opts;
        }
        if(opts.mcp_mode == 1) {
          opts.error = L"--mcp and --mcp-http choose different transports; give one";
          return opts;
        }
        opts.mcp_mode = 2;
        opts.mcp_port = port;
      }
      const std::vector<std::wstring> host_names = {L"mcp-http-host"};
      if(HasCommandLineArgumentWithAliases(args, host_names)) {
        ++opts.consumed;
        const std::wstring value = GetCommandLineArgumentWithAliases(args, host_names, L"");
        if(opts.mcp_mode != 2) {
          opts.error = L"--mcp-http-host: only with --mcp-http=<port>";
          return opts;
        }
        bool ok = !value.empty();
        for(wchar_t c : value) {
          if(c <= L' ') { ok = false; break; }
        }
        if(!ok) {
          opts.error = L"--mcp-http-host: expected an address such as 127.0.0.1 or 0.0.0.0, got '" + value + L"'";
          return opts;
        }
        opts.mcp_host = value;
      }
    }

    return opts;
  }

  // Defined in interpreter.cpp, which can see the JIT headers; the mains cannot
  // include those without dragging the whole interpreter in.
  void SetJitAutoThreshold(long threshold);
  // Defined in interpreter.cpp; read by MemoryManager::Initialize.
  void SetNurserySize(size_t size);
  // Defined in interpreter.cpp; read when each thread's call stack is created.
  void SetMaxCallDepth(long depth);
  // Defined in interpreter.cpp; read when a spawned thread starts.
  void SetThreadFaults(bool isolate);

  // Applies the options that must be in place before the program loads: the
  // library directory (the loader reads OBJECK_LIB_PATH, so the flag sets the
  // same variable in-process and the two paths cannot disagree), the JIT
  // policy (consulted on the first method call) and the nursery limit (read
  // when the interpreter initializes the collector).
  inline void ApplyVmOptions(const VmOptions& opts) {
    if(!opts.lib_path.empty()) {
#ifdef _WIN32
      // UTF-16, as GetLibraryPath reads it back: _putenv_s took UTF-8 as ANSI
      _wputenv_s(L"OBJECK_LIB_PATH", opts.lib_path.c_str());
#else
      setenv("OBJECK_LIB_PATH", UnicodeToBytes(opts.lib_path).c_str(), 1);
#endif
    }
    if(opts.jit_threshold != 0) {
      SetJitAutoThreshold(opts.jit_threshold);
    }
    if(opts.nursery_size != 0) {
      SetNurserySize(opts.nursery_size);
    }
    if(opts.call_depth != 0) {
      SetMaxCallDepth(opts.call_depth);
    }
    if(opts.thread_faults >= 0) {
      SetThreadFaults(opts.thread_faults == 1);
    }
    // The loader reads this: it adds the transport to the program's arguments and
    // starts the program's McpMain instead of its Main (loader.cpp). Nothing else
    // reads it, so a run without the flag is not touched.
    if(opts.mcp_mode != 0) {
      std::wstring value = L"stdio";
      if(opts.mcp_mode == 2) {
        value = L"http " + (opts.mcp_host.empty() ? std::wstring(L"127.0.0.1") : opts.mcp_host) + L" " + std::to_wstring(opts.mcp_port);
      }
#ifdef _WIN32
      _wputenv_s(L"OBJECK_MCP", value.c_str());
#else
      setenv("OBJECK_MCP", UnicodeToBytes(value).c_str(), 1);
#endif
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
    usage += L"  --nursery=<size>          Young-generation size; a full nursery triggers\n";
    usage += L"                            a minor collection. <size> is <number>(k|m)\n";
    usage += L"                            from 64k to 128m (default 128m)\n";
    usage += L"                            Env: OBJECK_NURSERY=<size>\n";
    usage += L"  --call-depth=<frames>     How deep calls may nest before the program stops\n";
    usage += L"                            with 'call stack bounds have been exceeded' (a\n";
    usage += L"                            Try() recovers from it), from 256 to 1048576\n";
    usage += L"                            (default 16384). Compiled code can reach the end\n";
    usage += L"                            of the native stack first: the same error\n";
    usage += L"                            Env: OBJECK_CALL_DEPTH=<frames>\n";
    usage += L"  --thread-faults=exit|isolate\n";
    usage += L"                            What an error nothing catches does in a thread the\n";
    usage += L"                            program started: 'exit' (default) ends the program;\n";
    usage += L"                            'isolate' ends only that thread, and Thread->Join\n";
    usage += L"                            can report it (runtime.thread.join.fault). The main\n";
    usage += L"                            thread and Runtime->Exit always end the program\n";
    usage += L"                            Env: OBJECK_THREAD_FAULTS=isolate\n";
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
    usage += L"  --mcp                     Serve the program's MCP tools, resources and prompts\n";
    usage += L"                            (the functions marked @tool, @resource, @prompt) on\n";
    usage += L"                            standard input and output instead of running Main.\n";
    usage += L"                            Standard output carries the protocol only: the\n";
    usage += L"                            program must not print to it (use standard error)\n";
    usage += L"  --mcp-http=<port>         The same over streamable HTTP on <port> (0 to 65535;\n";
    usage += L"                            0 picks a free port and prints it to standard error)\n";
    usage += L"  --mcp-http-host=<address> The address --mcp-http listens on (default 127.0.0.1)\n";
    usage += L"\nExamples:\n";
    usage += L"  obr hello.obe\n";
    usage += L"  obr --gc-threshold=2m hello.obe\n";
    usage += L"  obr --call-depth=100000 deep.obe\n";
    usage += L"  obr --jit=off hello.obe          (does it still happen without the JIT?)\n";
    usage += L"  obr --lib-path=/opt/objeck/lib hello.obe\n";
    usage += L"  obr --objeck-stdio=binary hello.obe\n";
    usage += L"  obr --mcp tools.obe              (an MCP server for Claude Desktop or Claude Code)\n";
    usage += L"  obr --mcp-http=8080 tools.obe    (the same over HTTP, 127.0.0.1:8080)\n";
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
