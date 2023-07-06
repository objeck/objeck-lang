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
  * Token
  */
class Token {
public:
  enum Type {
    IDENT_TYPE,
    NUM_TYPE,
    KEYWORD_TYPE,
    OPER_TYPE,
    CTRL_TYPE,
    COMMA_TYPE,
    OPEN_CBRACE,
    CLOSED_CBRACE,
    MULTI_COMMENT,
    LINE_COMMENT,
    OPEN_BRACKET_TYPE,
    CLOSED_BRACKET_TYPE,
    CHAR_STRING,
    ACCESSOR_TYPE,
    END_STMT_TYPE
  };

private:
  Token::Type type;
  std::wstring value;

public:
  Token(Token::Type t, const std::wstring &v);
  ~Token();

  Token::Type GetType() {
    return type;
  }

  const std::wstring GetValue() {
    return value;
  }
};

/**
 * Scanner
 */
class Scanner {
  wchar_t* buffer;
  size_t buffer_size;
  size_t buffer_pos;

  wchar_t prev_char;
  wchar_t cur_char;
  wchar_t next_char;

  std::unordered_map<std::wstring, Token::Type> keywords;
  std::vector<Token*> tokens;
  void NextChar();
  void Whitespace();
  void LoadKeywords();

public:
  Scanner(wchar_t* b, size_t s);
  ~Scanner();

  std::vector<Token*> Scan();
};

/**
 * Formatter
 */
class CodeFormatter {
  wchar_t* buffer;
  size_t buffer_size;
  size_t indent_space;

public:
  CodeFormatter(const std::wstring& s, bool f = false);
  ~CodeFormatter();

  std::wstring Format();
};

#endif
