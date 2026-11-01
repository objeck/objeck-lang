/***************************************************************************
 * Compiler diagnostics: one record, one sink.
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

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef _DEBUG
#include "../shared/logger.h"
#endif

namespace frontend {
  enum DiagnosticSeverity {
    DIAG_ERROR,
    DIAG_WARNING
  };

  // Codes. 0 is a diagnostic nobody has classified yet, which is every message
  // the parser and the analyzer raise today: the code is the place a stable
  // identifier goes once a message is given one, without changing the record
  // again. A host that wants to react to one kind of failure uses the codes
  // below rather than matching its text.
  enum DiagnosticCode {
    DIAG_CODE_NONE = 0,
    // the compiler could not go on (CompilerFatal), reported by an embedding host
    DIAG_CODE_FATAL = 1
  };

  // A message with its place. The range starts at (line, pos), both 1-based; the
  // end is -1 where the compiler did not record one, which is every diagnostic
  // today.
  struct Diagnostic {
    int code;
    DiagnosticSeverity severity;
    std::wstring file;
    int line;
    int pos;
    int end_line;
    int end_pos;
    std::wstring message;

    Diagnostic() : code(DIAG_CODE_NONE), severity(DIAG_ERROR), line(1), pos(1), end_line(-1), end_pos(-1) {
    }

    Diagnostic(DiagnosticSeverity s, const std::wstring& f, int l, int p, const std::wstring& m, int c = DIAG_CODE_NONE) :
      code(c), severity(s), file(f), line(l), pos(p), end_line(-1), end_pos(-1), message(m) {
    }

    // The line the command line prints and the tests match:
    // "file:(line,pos): message", with "Warning: " ahead of a warning's message.
    std::wstring Format() const {
      return file + L":(" + std::to_wstring(line) + L',' + std::to_wstring(pos) + L"): " +
        (severity == DIAG_WARNING ? L"Warning: " : L"") + message;
    }
  };

  // Where the parser and the analyzer put what they find. Each keeps one, in
  // place of its own copy of the code that built the "file:(line,pos): message"
  // string and the multimap it went into.
  //
  // Diagnostics stay in the order they have always printed: by line, and in the
  // order raised within a line. Unique* drops repeats of the same text, which a
  // diagnostic raised from more than one resolution path produces, before any cap
  // is applied (an undefined method call can be reported three times at one
  // position, and identical copies pushed real errors off the end of the output).
  class DiagnosticSink {
    std::multimap<int, Diagnostic> errors;
    std::multimap<int, Diagnostic> warnings;

    static std::vector<Diagnostic> Unique(const std::multimap<int, Diagnostic>& all) {
      std::set<std::wstring> seen;
      std::vector<Diagnostic> unique;
      for(std::multimap<int, Diagnostic>::const_iterator iter = all.begin(); iter != all.end(); ++iter) {
        if(seen.insert(iter->second.Format()).second) {
          unique.push_back(iter->second);
        }
      }

      return unique;
    }

   public:
    void Error(const std::wstring& file, int line, int pos, const std::wstring& message) {
#ifdef _DEBUG
      GetLogger() << L"\tError: " << file << L":(" << line << L',' << pos << L"): " << message << std::endl;
#endif
      errors.insert(std::pair<int, Diagnostic>(line, Diagnostic(DIAG_ERROR, file, line, pos, message)));
    }

    // an error about a whole file: reported at its first character, sorted first
    void Error(const std::wstring& file, const std::wstring& message) {
#ifdef _DEBUG
      GetLogger() << L"\tError: " << message << std::endl;
#endif
      errors.insert(std::pair<int, Diagnostic>(1, Diagnostic(DIAG_ERROR, file, 1, 1, message)));
    }

    void Warning(const std::wstring& file, int line, int pos, const std::wstring& message) {
#ifdef _DEBUG
      GetLogger() << L"\tWarning: " << file << L":(" << line << L',' << pos << L"): " << message << std::endl;
#endif
      warnings.insert(std::pair<int, Diagnostic>(line, Diagnostic(DIAG_WARNING, file, line, pos, message)));
    }

    bool HasErrors() const {
      return !errors.empty();
    }

    bool HasWarnings() const {
      return !warnings.empty();
    }

    std::vector<Diagnostic> UniqueErrors() const {
      return Unique(errors);
    }

    std::vector<Diagnostic> UniqueWarnings() const {
      return Unique(warnings);
    }
  };
}
