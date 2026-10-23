/***************************************************************************
 * Non-throwing number parsing, shared by the compiler, VM and tools
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

#pragma once

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>

/**
 * Whole-string integer parsing that never throws. std::stoi, std::stol,
 * std::stoll and std::stoul throw std::invalid_argument for text that is not a
 * number and std::out_of_range for one that does not fit; the VM, obc, obd and
 * the REPL called them on text that a program, a source file, a library or a
 * client supplied, and a throw nothing caught ended the process. These answer
 * false instead, and the caller decides what bad text means where it is: a
 * recoverable runtime error, a compile error, a usage error.
 *
 * The rules are strtoll's, made strict: leading whitespace and a sign are
 * allowed, at least one digit is required, and nothing may follow the digits.
 * A value outside the target type's range is refused, never saturated.
 */
namespace objeck_parse {
  inline bool ToInt64(const wchar_t* text, int base, int64_t& out) {
    if(!text) {
      return false;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const long long value = wcstoll(text, &end, base);
    if(end == text || errno == ERANGE || errno == EINVAL || *end != L'\0') {
      return false;
    }
    out = (int64_t)value;
    return true;
  }

  inline bool ToInt64(const char* text, int base, int64_t& out) {
    if(!text) {
      return false;
    }
    char* end = nullptr;
    errno = 0;
    const long long value = strtoll(text, &end, base);
    if(end == text || errno == ERANGE || errno == EINVAL || *end != '\0') {
      return false;
    }
    out = (int64_t)value;
    return true;
  }

  // the same, for a value that must fit an int
  template<typename CharT>
  inline bool ToInt(const std::basic_string<CharT>& text, int& out, int base = 10) {
    int64_t value = 0;
    if(!ToInt64(text.c_str(), base, value) || value < INT_MIN || value > INT_MAX) {
      return false;
    }
    out = (int)value;
    return true;
  }

  // a count or an index: digits only (no sign, no whitespace) that fit size_t
  template<typename CharT>
  inline bool ToIndex(const std::basic_string<CharT>& text, size_t& out) {
    if(text.empty()) {
      return false;
    }
    size_t value = 0;
    for(const CharT c : text) {
      if(c < (CharT)'0' || c > (CharT)'9') {
        return false;
      }
      const size_t digit = (size_t)(c - (CharT)'0');
      if(value > (SIZE_MAX - digit) / 10) {
        return false;
      }
      value = value * 10 + digit;
    }
    out = value;
    return true;
  }
}
