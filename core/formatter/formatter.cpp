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

/**
 * Token 
 */
Token::Token(Token::Type t, const std::wstring& v)
{
  type = t;
  value = v;
}

Token::~Token()
{

}

/**
 * Scanner
 */
Scanner::Scanner()
{
  buffer = nullptr;
  buffer_pos = buffer_pos = 0;
  prev_char = cur_char = next_char = L'\0';
}

Scanner::~Scanner()
{
  if(buffer) {
    delete[] buffer;
    buffer = nullptr;
  }
}

void Scanner::SetBuffer(wchar_t* b, size_t s)
{
  buffer = b;
  buffer_size = s;
}

void Scanner::NextChar()
{
  if(!buffer_pos) {

  }
  else if(buffer_pos < buffer_size) {

  }
  else {

  }
}

void Scanner::Whitespace()
{

}

std::vector<Token*> Scanner::Scan()
{
  if(buffer && buffer_size) {
    Whitespace();
  }

  return tokens;
}



/**
 * Formatter
 */
CodeFormatter::CodeFormatter(const std::wstring& s, bool f)
{
  size_t buffer_size;

  // process file input
  if(f) {
    wchar_t* buffer = LoadFileBuffer(s, buffer_size);
  }
  // process string input
  else {
    buffer_size = s.size();
    wchar_t* buffer = new wchar_t[buffer_size];
    std::wmemcpy(buffer, s.c_str(), buffer_size);
  }
}

CodeFormatter::~CodeFormatter()
{
  if(scanner) {
    delete scanner;
    scanner = nullptr;
  }
}

std::wstring CodeFormatter::Format()
{
  return L"";
}
