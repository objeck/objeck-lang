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
  keywords[L"function"] = Token::Type::KEYWORD_TYPE;
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
    next_char = '\0';
  }

  if(cur_char == L'\n') {
  //  tokens.push_back(new Token(Token::Type::VSPACE, L"\n"));
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

  while(cur_char) {
    //
    // whitespace
    //
    Whitespace();

    //
    // comment
    //
    if(cur_char == L'#') {
      const size_t str_start = buffer_pos - 1;
      NextChar();
      
      // multi-line comment
      if(cur_char == L'~') {
        
        NextChar();
        while(!(cur_char == L'~' && next_char == L'#')) {
          NextChar();
        }
        NextChar();
        NextChar();

        const std::wstring comment_str(buffer, str_start, buffer_pos - str_start - 1);
        tokens.push_back(new Token(Token::Type::MULTI_COMMENT, L'\n' + comment_str+ L'\n'));
      }
      // single line comment
      else {
        NextChar();
        while(cur_char != L'\n') {
          NextChar();
        }
        NextChar();

        const std::wstring comment_str(buffer, str_start, buffer_pos - str_start - 1);
        tokens.push_back(new Token(Token::Type::LINE_COMMENT, comment_str));
      }
    }
    //
    // character string
    // 
    if(cur_char == L'"') {
      const size_t str_start = buffer_pos - 1;
      NextChar();
      while(!(prev_char != L'\\' && cur_char == L'"')) {
        NextChar();
      }
      NextChar();

      const std::wstring char_str(buffer, str_start, buffer_pos - str_start - 1);
      tokens.push_back(new Token(Token::Type::CHAR_STRING, char_str));
    }
    //
    // identifier
    //
    else if(iswalpha(cur_char)) {
      const size_t str_start = buffer_pos - 1;
      while(iswalpha(cur_char) || iswdigit(cur_char) || cur_char == L'_' || cur_char == L'.') {
        NextChar();
      }
      const std::wstring ident_str(buffer, str_start, buffer_pos - str_start - 1);
#ifdef _DEBUG
      std::wcout << L"IDENT: |" << ident_str << L'|' << std::endl;
#endif

      auto keyword = keywords.find(ident_str);
      if(keyword != keywords.end()) {
        tokens.push_back(new Token(Token::Type::KEYWORD_TYPE, ident_str));
      }
      else {
        tokens.push_back(new Token(Token::Type::IDENT_TYPE, ident_str));
      }
    }
    //
    // number
    //
    else if(iswdigit(cur_char)) {
      const size_t str_start = buffer_pos - 1;
      while(iswdigit(cur_char) || cur_char == L'.' || cur_char == L'x' || cur_char == L'X' || (cur_char >= L'a' && cur_char <= L'f') || (cur_char >= L'A' && cur_char <= L'F')) {
        NextChar();
      }
      const std::wstring num_str(buffer, str_start, buffer_pos - str_start - 1);
      tokens.push_back(new Token(Token::Type::NUM_TYPE, num_str));
    }
    // operator or control
    else {
      switch(cur_char) {
      case L'{':
        tokens.push_back(new Token(Token::Type::OPEN_CBRACE, L"{"));
        NextChar();
        break;

      case L'}':
        tokens.push_back(new Token(Token::Type::CLOSED_CBRACE, L"}"));
        NextChar();
        break;

      case L'[':
        tokens.push_back(new Token(Token::Type::BRACKET_TYPE, L"["));
        NextChar();
        break;

      case L']':
        tokens.push_back(new Token(Token::Type::BRACKET_TYPE, L"]"));
        NextChar();
        break;

      case L'(':
        tokens.push_back(new Token(Token::Type::BRACKET_TYPE, L"("));
        NextChar();
        break;

      case L')':
        tokens.push_back(new Token(Token::Type::BRACKET_TYPE, L")"));
        NextChar();
        break;

      case L':':
        if(next_char == L'=') {
          NextChar();
          tokens.push_back(new Token(Token::Type::OPER_TYPE, L":="));
          NextChar();
        }
        else {
          tokens.push_back(new Token(Token::Type::CTRL_TYPE, L":"));
          NextChar();
        }
        break;

      case L'→':
        tokens.push_back(new Token(Token::Type::CTRL_TYPE, L"→"));
        NextChar();
        break;

      case L'~':
        tokens.push_back(new Token(Token::Type::CTRL_TYPE, L"~"));
        NextChar();
        break;

      case L',':
        tokens.push_back(new Token(Token::Type::CTRL_TYPE, L","));
        NextChar();
        break;

      case L';':
        tokens.push_back(new Token(Token::Type::END_STMT_TYPE, L";"));
        NextChar();
        break;

      case L'<':
        if(next_char == L'>') {
          NextChar();
          tokens.push_back(new Token(Token::Type::OPER_TYPE, L"<>"));
          NextChar();
        }
        else {
          tokens.push_back(new Token(Token::Type::OPER_TYPE, L"<"));
          NextChar();
        }
        break;

      case L'>':
        if(next_char == L'>') {
          NextChar();
          tokens.push_back(new Token(Token::Type::OPER_TYPE, L"<>"));
          NextChar();
        }
        else {
          tokens.push_back(new Token(Token::Type::OPER_TYPE, L">"));
          NextChar();
        }
        break;

      case L'-':
        if(next_char == L'>') {
          NextChar();
          tokens.push_back(new Token(Token::Type::CTRL_TYPE, L"->"));
          NextChar();
        }
        else if(next_char == L'-') {
          NextChar();
          tokens.push_back(new Token(Token::Type::CTRL_TYPE, L"--"));
          NextChar();
        }
        else {
          tokens.push_back(new Token(Token::Type::CTRL_TYPE, L"-"));
          NextChar();
        }
        break;

      case L'\r':
      case L'\n':
      case L' ':
      case L'\t':
        break;

      default:
        std::wcerr << L"### Unknown parsed token: '" << cur_char << L"' ###" << std::endl;
        NextChar();
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
  std::wstring output;

  Scanner scanner(buffer, buffer_size);
  std::vector<Token*> tokens = scanner.Scan();
  for(auto token : tokens) {
    bool skip = false;

    switch(token->GetType()) {
    case Token::Type::IDENT_TYPE:
    case Token::Type::KEYWORD_TYPE:
    case Token::Type::OPER_TYPE:
    case Token::Type::CTRL_TYPE:
    case Token::Type::BRACKET_TYPE:
    case Token::Type::CHAR_STRING:
      output += token->GetValue();
      break;

    case Token::Type::NUM_TYPE:
      output += token->GetValue();
      break;

    case Token::Type::MULTI_COMMENT:
      output += L'\n' + token->GetValue();
      for(size_t i = 0; i < indent_space; ++i) {
        output += '\t';
      }
      break;

    case Token::Type::LINE_COMMENT:
      output += token->GetValue();
      for(size_t i = 0; i < indent_space; ++i) {
        output += '\t';
      }
      break;

    case Token::Type::OPEN_CBRACE:
      ++indent_space;
      output += token->GetValue() + L'\n';
      for(size_t i = 0; i < indent_space; ++i) {
        output += '\t';
      }
      break;

    case Token::Type::CLOSED_CBRACE:
      --indent_space;
      output += L'\n';
      for(size_t i = 0; i < indent_space; ++i) {
        output += '\t';
      }
      output += token->GetValue();
      break;

    case Token::Type::END_STMT_TYPE:
      output.pop_back();
      output += token->GetValue();
      output += L'\n';
      for(size_t i = 0; i < indent_space; ++i) {
        output += '\t';
      }
      break;

    default:
      std::wcerr << L"### Unknown formatted token: \"" << token->GetValue() << L"\" ###" << std::endl;
      break;
    }

    output += L' ';
  }

  return output;
}
