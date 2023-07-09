/***************************************************************************
 * Language formatter
 *
 * Copyright (c) 2023, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and uses in source and binary forms, with or without
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

#ifndef __FORMATTER_H__
#define __FORMATTER_H__

#include "../shared/sys.h"
#include <unordered_map>
#include <sstream>

/**
 * Formatter
 */
class CodeFormatter {
  wchar_t* buffer;
  size_t buffer_size;
  size_t indent_space;

  static inline void LeftTrim(std::wstring& str) {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](wchar_t ch) {
      return !std::isspace(ch);
                                        }));
  }

  static inline void RightTrim(std::wstring& str) {
    str.erase(std::find_if(str.rbegin(), str.rend(), [](wchar_t ch) {
      return !std::isspace(ch);
                           }).base(), str.end());
  }

  static inline std::wstring& Trim(std::wstring& str) {
    LeftTrim(str);
    RightTrim(str);
    return str;
  }

public:
  CodeFormatter(const std::wstring& s, bool f = false);
  ~CodeFormatter();

  std::wstring Format();
};

#endif
