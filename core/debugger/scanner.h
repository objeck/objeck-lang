/***************************************************************************
 * Debugger scanner.
 *
 * Copyright (c) 2010-2013 Randy Hollines
 * All rights reserved.
 *
 * Reistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the StackVM Team nor the names of its
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

#ifndef __SCANNER_H__
#define __SCANNER_H__

#include "../vm/common.h"
#include "../shared/sys.h"

// comment
#define COMMENT '#'
#define EXTENDED_COMMENT '~'

// look ahead value
#define LOOK_AHEAD 3
// white space
#define WHITE_SPACE (cur_char == ' ' || cur_char == '\t' || cur_char == '\r' || cur_char == '\n')

/****************************
 * Token types
 ****************************/
enum TokenType {
  // misc
  TOKEN_END_OF_STREAM = -1000,
  TOKEN_NO_INPUT,
  TOKEN_UNKNOWN,
  // symbols
  TOKEN_PERIOD,
  TOKEN_COLON,
  TOKEN_SEMI_COLON,
  TOKEN_COMMA,
  TOKEN_ASSIGN,
  TOKEN_OPEN_BRACE,
  TOKEN_CLOSED_BRACE,
  TOKEN_OPEN_PAREN,
  TOKEN_CLOSED_PAREN,
  TOKEN_OPEN_BRACKET,
  TOKEN_CLOSED_BRACKET,
  TOKEN_ASSESSOR,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_EQL,
  TOKEN_NEQL,
  TOKEN_LES,
  TOKEN_GTR,
  TOKEN_GEQL,
  TOKEN_LEQL,
  TOKEN_ADD,
  TOKEN_SUB,
  TOKEN_MUL,
  TOKEN_DIV,
  TOKEN_MOD,
  // literals
  TOKEN_IDENT,
  TOKEN_INT_LIT,
  TOKEN_FLOAT_LIT,
  TOKEN_CHAR_LIT,
  TOKEN_CHAR_STRING_LIT,
  // reserved words
  TOKEN_RUN_ID,
  TOKEN_CLEAR_ID,
  TOKEN_DELETE_ID,
  TOKEN_EXE_ID,
  TOKEN_QUIT_ID,
  TOKEN_BREAK_ID,
  TOKEN_BREAKS_ID,
  TOKEN_PRINT_ID,
  TOKEN_MEMORY_ID,
  TOKEN_INFO_ID,
  TOKEN_FRAME_ID,
  TOKEN_CONT_ID,
  TOKEN_NEXT_ID,
  TOKEN_NEXT_LINE_ID,
  TOKEN_OUT_ID,
  TOKEN_STACK_ID,
  TOKEN_SELF_ID,
  TOKEN_LIST_ID,
  TOKEN_CLASS_ID,
  TOKEN_METHOD_ID,
  TOKEN_SRC_ID,
  TOKEN_ARGS_ID,
};

/****************************
 * Token class
 ****************************/
class Token {
 private:
  enum TokenType token_type;
  std::wstring ident;

  INT_VALUE int_lit;
  FLOAT_VALUE double_lit;
  wchar_t char_lit;
  char byte_lit;

 public:

  inline void Copy(Token* token) {
    char_lit = token->GetCharLit();
    int_lit = token->GetIntLit();
    double_lit = token->GetFloatLit();
    ident = token->GetIdentifier();
    token_type = token->GetType();
  }
  
  inline void  SetIntLit(INT_VALUE i) {
    int_lit = i;
  }

  inline void SetFloatLit(FLOAT_VALUE d) {
    double_lit = d;
  }

  inline void SetByteLit(char b) {
    byte_lit = b;
  }

  inline void SetCharLit(wchar_t c) {
    char_lit = c;
  }

  inline void SetIdentifier(std::wstring i) {
    ident = i;
  }

  inline const INT_VALUE GetIntLit() {
    return int_lit;
  }

  inline const FLOAT_VALUE GetFloatLit() {
    return double_lit;
  }

  inline const char GetByteLit() {
    return byte_lit;
  }

  inline const wchar_t GetCharLit() {
    return char_lit;
  }

  inline const std::wstring GetIdentifier() {
    return ident;
  }

  inline const enum TokenType GetType() {
    return token_type;
  }

  inline void SetType(enum TokenType t) {
    token_type = t;
  }
};

/**********************************
 * Token scanner with k lookahead
 * tokens
 **********************************/
class Scanner {
 private:
  // input buffer
  wchar_t* buffer;
  // buffer size
  std::streamoff buffer_size;
  // input buffer position
  int buffer_pos;
  // start marker position
  int start_pos;
  // end marker position
  int end_pos;
  // input characters
  wchar_t cur_char, nxt_char, nxt_nxt_char;
  // map of reserved identifiers
  std::map<const std::wstring, enum TokenType> ident_map;
  // array of tokens for lookahead
  Token* tokens[LOOK_AHEAD];

  // warning message
  void ProcessWarning() {
    std::wcout << L"Parse warning: Unknown token: '" << cur_char << L"'" << std::endl;
  }

  // parsers a character std::wstring
  inline void CheckString(int index) {
    // copy std::wstring
    const int length = end_pos - start_pos;
    std::wstring char_string(buffer, start_pos, length);
    // set std::wstring
    tokens[index]->SetType(TOKEN_CHAR_STRING_LIT);
    tokens[index]->SetIdentifier(char_string);
  }

  // parse an integer
  inline void ParseInteger(int index, int base = 0) {
    // copy std::wstring
    int length = end_pos - start_pos;
    std::wstring ident(buffer, start_pos, length);

    // set token
    wchar_t* end;
    tokens[index]->SetType(TOKEN_INT_LIT);
    tokens[index]->SetIntLit(wcstol(ident.c_str(), &end, base));
  }

  // parse a double
  inline void ParseDouble(int index) {
    // copy std::wstring
    const int length = end_pos - start_pos;
    std::wstring wident(buffer, start_pos, length);
    // set token
    tokens[index]->SetType(TOKEN_FLOAT_LIT);
    const std::string ident = UnicodeToBytes(wident);
    tokens[index]->SetFloatLit(atof(ident.c_str()));
  }

  // parsers an unicode character
  inline void ParseUnicodeChar(int index) {
    // copy std::wstring
    const int length = end_pos - start_pos;
    std::wstring ident(buffer, start_pos, length);
    // set token
    wchar_t* end;
    tokens[index]->SetType(TOKEN_CHAR_LIT);
    tokens[index]->SetCharLit((wchar_t)wcstol(ident.c_str(), &end, 16));
  }

  // reads a line as input
  void ReadLine(const std::wstring &line);
  // ignore white space
  void Whitespace();
  // next character
  void NextChar();
  // load reserved keywords
  void LoadKeywords();
  // parses a new token
  void ParseToken(int index);
  // check identifier
  void CheckIdentifier(int index);

 public:
  // default constructor
  Scanner(const std::wstring &line);
  // default destructor
  ~Scanner();

  // next token
  void NextToken();

  // token accessor
  Token* GetToken(int index = 0);
};

#endif
