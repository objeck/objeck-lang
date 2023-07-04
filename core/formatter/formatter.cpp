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
Scanner::Scanner(wchar_t* b, size_t s)
{
  buffer = b;
  buffer_size = s;
  buffer_pos = 0;
  prev_char = cur_char = next_char = L'\0';
}

Scanner::~Scanner()
{
  if(buffer) {
    delete[] buffer;
    buffer = nullptr;
  }
}

void Scanner::LoadKeywords()
{
  keywords[L"class"] = Token::Type::KEYWORD_TYPE;
  keywords[L"Nil"] = Token::Type::KEYWORD_TYPE;
}

void Scanner::NextChar()
{
  if(buffer_pos + 1 < buffer_size) {
    if(buffer_pos) {
      prev_char = cur_char;
      cur_char = next_char;
    }
    else {
      cur_char = buffer[buffer_pos];
    }
    next_char = buffer[++buffer_pos];
  }
  else {
    prev_char = cur_char;
    cur_char = next_char;
    next_char = buffer[buffer_pos];
  }
}

void Scanner::Whitespace()
{
  while(iswspace(cur_char)) {
    NextChar();
  }
}

std::vector<Token*> Scanner::Scan()
{
  LoadKeywords();
  NextChar();

  std::vector<Token*> tokens;
  while(cur_char) {
    //
    // whitespace
    //
    Whitespace();

    // comment
    if(cur_char == L'#') {
      NextChar();
      
      // multi-line
      if(cur_char == L'~') {
        NextChar();
        while(!(cur_char == L'~' && next_char == L'#')) {
          NextChar();
        }
        NextChar();
        NextChar();
      }
      // single line
      else {

      }
    }
    //
    // character string
    // 
    if(cur_char == L'"') {
      size_t str_start = buffer_pos - 1;
      NextChar();
      while(!(prev_char != L'\\' && cur_char == L'"')) {
        NextChar();
      }
      NextChar();

      const std::wstring comment_str(buffer, str_start, buffer_pos - str_start - 1);
#ifdef _DEBUG
      std::wcout << L"COMMENT: |" << comment_str << std::endl;
#endif
    }
    //
    // identifier
    //
    else if(iswalpha(cur_char)) {
      size_t str_start = buffer_pos - 1;
      while(iswalpha(cur_char) || iswdigit(cur_char) || cur_char == L'_') {
        NextChar();
      }
      const std::wstring str(buffer, str_start, buffer_pos - str_start - 1);
#ifdef _DEBUG
      std::wcout << L"IDENT: |" << str << L'|' << std::endl;
#endif

      auto keyword = keywords.find(str);
      if(keyword != keywords.end()) {
        tokens.push_back(new Token(Token::KEYWORD_TYPE, str));
      }
      else {
        tokens.push_back(new Token(Token::IDENT_TYPE, str));
      }
    }
    //
    // number
    //
    else if(iswdigit(cur_char)) {
      size_t str_start = buffer_pos - 1;
      while(iswdigit(cur_char) || cur_char == L'.' || (cur_char >= L'a' && cur_char <= L'f') || (cur_char >= L'A' && cur_char <= L'F')) {
        NextChar();
      }
      const std::wstring num_str(buffer, str_start, buffer_pos - str_start - 1);
    }
    // operator or control
    else {
      switch(cur_char) {
      case L'{':
        tokens.push_back(new Token(Token::CTRL_TYPE, L"{"));
        NextChar();
        break;

      case L'}':
        tokens.push_back(new Token(Token::CTRL_TYPE, L"}"));
        NextChar();
        break;

      case L'[':
        tokens.push_back(new Token(Token::CTRL_TYPE, L"["));
        NextChar();
        break;

      case L']':
        tokens.push_back(new Token(Token::CTRL_TYPE, L"]"));
        NextChar();
        break;

      case L'(':
        tokens.push_back(new Token(Token::CTRL_TYPE, L"("));
        NextChar();
        break;

      case L')':
        tokens.push_back(new Token(Token::CTRL_TYPE, L")"));
        NextChar();
        break;

      case L':':
        tokens.push_back(new Token(Token::CTRL_TYPE, L":"));
        NextChar();
        break;

      case L'~':
        tokens.push_back(new Token(Token::CTRL_TYPE, L"~"));
        NextChar();
        break;

      case L';':
        tokens.push_back(new Token(Token::CTRL_TYPE, L","));
        NextChar();
        break;

      case L'-':
        if(next_char == L'>') {
          NextChar();
          tokens.push_back(new Token(Token::CTRL_TYPE, L"->"));
          NextChar();
        }
        else if(next_char == L'-') {
          NextChar();
          tokens.push_back(new Token(Token::CTRL_TYPE, L"--"));
          NextChar();
        }
        else {
          tokens.push_back(new Token(Token::CTRL_TYPE, L"-"));
          NextChar();
        }
        break;

      case L'→':
        tokens.push_back(new Token(Token::CTRL_TYPE, L","));
        break;

      default:
        break;
      }
    }
  }

  return tokens;
}

/**
 * Formatter
 */
CodeFormatter::CodeFormatter(const std::wstring& s, bool f)
{
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
  Scanner scanner(buffer, buffer_size);
  std::vector<Token*> tokens = scanner.Scan();

  return L"";
}
