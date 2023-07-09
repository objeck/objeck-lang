/***************************************************************************
 * Language formatter
 *
 * Copyright (c) 2023, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, hare permitted provided that the following conditions are met:
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

#include "formatter.h"

CodeFormatter::CodeFormatter(const std::wstring& s, bool f)
{
  indent_space = 0;

  // process file input
  if(f) {
    buffer = LoadFileBuffer(s, buffer_size);
  }
  // process string input
  else {
    buffer_size = s.size();
    buffer = new wchar_t[buffer_size];
    std::wmemcpy(buffer, s.c_str(), buffer_size);
  }
}

CodeFormatter::~CodeFormatter()
{

}

std::wstring CodeFormatter::Format()
{
  std::wstringstream out;

  size_t ident_count = 0;
  std::wstring line;
  for(size_t i = 0; i < buffer_size; ++i) {
    const wchar_t c = buffer[i];
    if(c == L'\n') {
      Trim(line);

      if(!line.empty() && line.front() == L'}') {
        ident_count--;
      }

      for(size_t j = 0; j < ident_count; ++j) {
        out << '\t';
      }
      out << line << std::endl;

      if(!line.empty() && (line.front() == L'{' || line.back() == L'{')) {
        ident_count++;
      }
      
      line.clear();
    }
    else if(c != L'\r' && c != L'\t') {
      line += c;
    }
  }

  return out.str();
}
