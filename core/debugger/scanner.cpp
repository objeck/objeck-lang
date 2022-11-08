/**************************************************************************
 * Debugger scanner.
 *
 * Copyright (c) 2010-2013 Randy Hollines
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

#include "scanner.h"

#define EOB L'\0'

 /****************************
  * Scanner constructor
  ****************************/
Scanner::Scanner(const std::wstring &line)
{
  // create tokens
  for(int i = 0; i < LOOK_AHEAD; i++) {
    tokens[i] = new Token;
  }
  // load identifiers into map
  LoadKeywords();
  ReadLine(line);
}

/****************************
 * Scanner destructor
 ****************************/
Scanner::~Scanner()
{
  // delete buffer
  if(buffer) {
    delete[] buffer;
    buffer = nullptr;
  }

  for(int i = 0; i < LOOK_AHEAD; i++) {
    Token* temp = tokens[i];
    delete temp;
    temp = nullptr;
  }
}

/****************************
 * Loads language keywords
 ****************************/
void Scanner::LoadKeywords()
{
  ident_map[L"?next"] = TOKEN_NEXT_LINE_ID;
  ident_map[L"?n"] = TOKEN_NEXT_LINE_ID;
  ident_map[L"?step"] = TOKEN_NEXT_ID;
  ident_map[L"?s"] = TOKEN_NEXT_ID;
  ident_map[L"?jump"] = TOKEN_OUT_ID;
  ident_map[L"?j"] = TOKEN_OUT_ID;
  ident_map[L"?cont"] = TOKEN_CONT_ID;
  ident_map[L"?c"] = TOKEN_CONT_ID;
  ident_map[L"?exe"] = TOKEN_EXE_ID;
  ident_map[L"?src"] = TOKEN_SRC_ID;
  ident_map[L"?args"] = TOKEN_ARGS_ID;
  ident_map[L"?quit"] = TOKEN_QUIT_ID;
  ident_map[L"?q"] = TOKEN_QUIT_ID;
  ident_map[L"?break"] = TOKEN_BREAK_ID;
  ident_map[L"?b"] = TOKEN_BREAK_ID;
  ident_map[L"?breaks"] = TOKEN_BREAKS_ID;
  ident_map[L"?stack"] = TOKEN_STACK_ID;
  ident_map[L"?print"] = TOKEN_PRINT_ID;
  ident_map[L"?p"] = TOKEN_PRINT_ID;
  ident_map[L"?memory"] = TOKEN_MEMORY_ID;
  ident_map[L"?m"] = TOKEN_MEMORY_ID;
  ident_map[L"?info"] = TOKEN_INFO_ID;
  ident_map[L"?i"] = TOKEN_INFO_ID;
  ident_map[L"?frame"] = TOKEN_FRAME_ID;
  ident_map[L"?f"] = TOKEN_FRAME_ID;
  ident_map[L"?clear"] = TOKEN_CLEAR_ID;
  ident_map[L"?delete"] = TOKEN_DELETE_ID;
  ident_map[L"?d"] = TOKEN_DELETE_ID;
  ident_map[L"?run"] = TOKEN_RUN_ID;
  ident_map[L"?r"] = TOKEN_RUN_ID;
  ident_map[L"?list"] = TOKEN_LIST_ID;
  ident_map[L"?l"] = TOKEN_LIST_ID;
  ident_map[L"@self"] = TOKEN_SELF_ID;
  ident_map[L"class"] = TOKEN_CLASS_ID;
  ident_map[L"method"] = TOKEN_METHOD_ID;
}

/****************************
 * Processes language
 * identifies
 ****************************/
void Scanner::CheckIdentifier(int index)
{
  // copy std::wstring
  const int length = end_pos - start_pos;
  std::wstring ident(buffer, start_pos, length);
  // check std::wstring
  enum TokenType ident_type = ident_map[ident];
  switch(ident_type) {
    case TOKEN_STACK_ID:
    case TOKEN_SRC_ID:
    case TOKEN_ARGS_ID:
    case TOKEN_CLASS_ID:
    case TOKEN_METHOD_ID:
    case TOKEN_LIST_ID:
    case TOKEN_SELF_ID:
    case TOKEN_NEXT_ID:
    case TOKEN_NEXT_LINE_ID:
    case TOKEN_OUT_ID:
    case TOKEN_CONT_ID:
    case TOKEN_EXE_ID:
    case TOKEN_QUIT_ID:
    case TOKEN_BREAK_ID:
    case TOKEN_BREAKS_ID:
    case TOKEN_PRINT_ID:
    case TOKEN_MEMORY_ID:
    case TOKEN_INFO_ID:
    case TOKEN_FRAME_ID:
    case TOKEN_CLEAR_ID:
    case TOKEN_DELETE_ID:
    case TOKEN_RUN_ID:
      tokens[index]->SetType(ident_type);
      break;
    default:
      tokens[index]->SetType(TOKEN_IDENT);
      tokens[index]->SetIdentifier(ident);
      break;
  }
}

/****************************
 * Reads a source input file
 ****************************/
void Scanner::ReadLine(const std::wstring &line)
{
  buffer_pos = 0;
  const int buffer_max = (int)line.size() + 1;
  buffer = new wchar_t[buffer_max];
#ifdef _WIN32
  wcsncpy_s(buffer, buffer_max, line.c_str(), line.size());
#else
  wcsncpy(buffer, line.c_str(), line.size());
#endif
  buffer[line.size()] = '\0';
  buffer_size = line.size() + 1;
#ifdef _DEBUG
  std::wcout << L"---------- Source ---------" << std::endl;
  std::wcout << buffer << std::endl;
  std::wcout << L"---------------------------" << std::endl;
#endif
}

/****************************
 * Processes the next token
 ****************************/
void Scanner::NextToken()
{
  if(buffer_pos == 0) {
    NextChar();
    for(int i = 0; i < LOOK_AHEAD; i++) {
      ParseToken(i);
    }
  }
  else {
    int i = 1;
    for(; i < LOOK_AHEAD; i++) {
      tokens[i - 1]->Copy(tokens[i]);
    }
    ParseToken(i - 1);
  }
}

/****************************
 * Gets the current token
 ****************************/
Token* Scanner::GetToken(int index)
{
  if(index < LOOK_AHEAD) {
    return tokens[index];
  }

  return nullptr;
}

/****************************
 * Gets the next character.
 * Note, EOB is returned at
 * end of a stream
 ****************************/
void Scanner::NextChar()
{
  if(buffer_pos < buffer_size) {
    // current character
    cur_char = buffer[buffer_pos++];
    // next character
    if(buffer_pos < buffer_size) {
      nxt_char = buffer[buffer_pos];
      // next next character
      if(buffer_pos + 1 < buffer_size) {
        nxt_nxt_char = buffer[buffer_pos + 1];
      }
      // end of file
      else {
        nxt_nxt_char = EOB;
      }
    }
    // end of file
    else {
      nxt_char = EOB;
    }
  }
  // end of file
  else {
    cur_char = EOB;
  }
}

/****************************
 * Processes white space
 ****************************/
void Scanner::Whitespace()
{
  while(WHITE_SPACE && cur_char != EOB) {
    NextChar();
  }
}

/****************************
 * Parses a token
 ****************************/
void Scanner::ParseToken(int index)
{
  // unable to load buffer
  if(!buffer) {
    tokens[index]->SetType(TOKEN_NO_INPUT);
    return;
  }
  // ignore white space
  Whitespace();
  // ignore comments
  while(cur_char == COMMENT && cur_char != EOB) {
    NextChar();
    // extended comment
    if(cur_char == EXTENDED_COMMENT) {
      NextChar();
      while(!(cur_char == EXTENDED_COMMENT && nxt_char == COMMENT) && cur_char != EOB) {
        NextChar();
      }
      NextChar();
      NextChar();
    }
    // line comment
    else {
      while(cur_char != L'\n' && cur_char != EOB) {
        NextChar();
      }
    }
    Whitespace();
  }
  // character string
  if(cur_char == L'\"') {
    NextChar();
    // mark
    start_pos = buffer_pos - 1;
    while(cur_char != L'\"' && cur_char != EOB) {
      if(cur_char == L'\\') {
        NextChar();
        switch(cur_char) {
          case L'"':
            break;

          case L'\\':
            break;

          case L'n':
            break;

          case L'r':
            break;

          case L't':
            break;

          case L'0':
            break;

          default:
            tokens[index]->SetType(TOKEN_UNKNOWN);
            NextChar();
            break;
        }
      }
      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    // check std::wstring
    NextChar();
    CheckString(index);
    return;
  }
  // character
  else if(cur_char == L'\'') {
    NextChar();
    // escape or hex/unicode encoding
    if(cur_char == L'\\') {
      NextChar();
      // read unicode std::wstring
      if(cur_char == L'u') {
        NextChar();
        start_pos = buffer_pos - 1;
        while(isdigit(cur_char) || (cur_char >= L'a' && cur_char <= L'f') ||
          (cur_char >= L'A' && cur_char <= L'F')) {
          NextChar();
        }
        end_pos = buffer_pos - 1;
        ParseUnicodeChar(index);
        if(cur_char != L'\'') {
          tokens[index]->SetType(TOKEN_UNKNOWN);
        }
        NextChar();
        return;
      }
      // escape
      else if(nxt_char == L'\'') {
        switch(cur_char) {
          case L'n':
            tokens[index]->SetType(TOKEN_CHAR_LIT);
            tokens[index]->SetCharLit('\n');
            NextChar();
            NextChar();
            return;
          case L'r':
            tokens[index]->SetType(TOKEN_CHAR_LIT);
            tokens[index]->SetCharLit('\r');
            NextChar();
            NextChar();
            return;
          case L't':
            tokens[index]->SetType(TOKEN_CHAR_LIT);
            tokens[index]->SetCharLit('\t');
            NextChar();
            NextChar();
            return;
          case L'\\':
            tokens[index]->SetType(TOKEN_CHAR_LIT);
            tokens[index]->SetCharLit('\\');
            NextChar();
            NextChar();
            return;
          case L'0':
            tokens[index]->SetType(TOKEN_CHAR_LIT);
            tokens[index]->SetCharLit('\0');
            NextChar();
            NextChar();
            return;
        }
      }
      // error
      else {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        NextChar();
        return;
      }
    }
    else {
      // error
      if(nxt_char != L'\'') {
        tokens[index]->SetType(TOKEN_UNKNOWN);
        NextChar();
        return;
      }
      else {
        tokens[index]->SetType(TOKEN_CHAR_LIT);
        tokens[index]->SetCharLit(cur_char);
        NextChar();
        NextChar();
        return;
      }
    }
  }
  // identifier
  else if(isalpha(cur_char) || cur_char == L'@' || cur_char == L'_' || cur_char == L'?' ||
          (iswdigit(cur_char) && (isalpha(nxt_char) || nxt_char == L'_'))) {
    // mark
    start_pos = buffer_pos - 1;

    while((isalpha(cur_char) || isdigit(cur_char) || cur_char == L'_' ||
          cur_char == L'@' || cur_char == L'?' || cur_char == L'.') && cur_char != EOB) {
      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    // check identifier
    CheckIdentifier(index);
    return;
  }
  // number
  else if(iswdigit(cur_char) || (cur_char == L'.' && iswdigit(nxt_char))) {
    bool is_double = false;
    int hex_state = 0;
    // mark
    start_pos = buffer_pos - 1;

    // test hex state
    if(cur_char == L'0') {
      hex_state = 1;
    }
    while(iswdigit(cur_char) || (cur_char == L'.' && iswdigit(nxt_char)) || cur_char == L'x' ||
          (cur_char >= L'a' && cur_char <= L'f') || (cur_char >= L'A' && cur_char <= L'F')) {
      // decimal double
      if(cur_char == L'.') {
        // error
        if(is_double) {
          tokens[index]->SetType(TOKEN_UNKNOWN);
          NextChar();
          break;
        }
        is_double = true;
      }
      // hex integer
      if(cur_char == L'x') {
        if(hex_state == 1) {
          hex_state = 2;
        }
        else {
          hex_state = 1;
        }
      }
      else {
        hex_state = 0;
      }
      // next character
      NextChar();
    }
    // mark
    end_pos = buffer_pos - 1;
    if(is_double) {
      ParseDouble(index);
    }
    else if(hex_state == 2) {
      ParseInteger(index, 16);
    }
    else if(hex_state) {
      tokens[index]->SetType(TOKEN_UNKNOWN);
    }
    else {
      ParseInteger(index);
    }
    return;
  }
  // other
  else {
    switch(cur_char) {
      case L':':
        if(nxt_char == L'=') {
          NextChar();
          tokens[index]->SetType(TOKEN_ASSIGN);
          NextChar();
        }
        else {
          tokens[index]->SetType(TOKEN_COLON);
          NextChar();
        }
        break;

      case L'-':
        if(nxt_char == L'>') {
          NextChar();
          tokens[index]->SetType(TOKEN_ASSESSOR);
          NextChar();
        }
        else {
          tokens[index]->SetType(TOKEN_SUB);
          NextChar();
        }
        break;

      case L'{':
        tokens[index]->SetType(TOKEN_OPEN_BRACE);
        NextChar();
        break;

      case L'.':
        tokens[index]->SetType(TOKEN_PERIOD);
        NextChar();
        break;

      case L'}':
        tokens[index]->SetType(TOKEN_CLOSED_BRACE);
        NextChar();
        break;

      case L'[':
        tokens[index]->SetType(TOKEN_OPEN_BRACKET);
        NextChar();
        break;

      case L']':
        tokens[index]->SetType(TOKEN_CLOSED_BRACKET);
        NextChar();
        break;

      case L'(':
        tokens[index]->SetType(TOKEN_OPEN_PAREN);
        NextChar();
        break;

      case L')':
        tokens[index]->SetType(TOKEN_CLOSED_PAREN);
        NextChar();
        break;

      case L',':
        tokens[index]->SetType(TOKEN_COMMA);
        NextChar();
        break;

      case L';':
        tokens[index]->SetType(TOKEN_SEMI_COLON);
        NextChar();
        break;

      case L'&':
        tokens[index]->SetType(TOKEN_AND);
        NextChar();
        break;

      case L'|':
        tokens[index]->SetType(TOKEN_OR);
        NextChar();
        break;

      case L'=':
        tokens[index]->SetType(TOKEN_EQL);
        NextChar();
        break;

      case L'<':
        if(nxt_char == L'>') {
          NextChar();
          tokens[index]->SetType(TOKEN_NEQL);
          NextChar();
        }
        else if(nxt_char == L'=') {
          NextChar();
          tokens[index]->SetType(TOKEN_LEQL);
          NextChar();
        }
        else {
          tokens[index]->SetType(TOKEN_LES);
          NextChar();
        }
        break;

      case L'>':
        if(nxt_char == L'=') {
          NextChar();
          tokens[index]->SetType(TOKEN_GEQL);
          NextChar();
        }
        else {
          tokens[index]->SetType(TOKEN_GTR);
          NextChar();
        }
        break;

      case L'+':
        tokens[index]->SetType(TOKEN_ADD);
        NextChar();
        break;

      case L'*':
        tokens[index]->SetType(TOKEN_MUL);
        NextChar();
        break;

      case L'/':
        tokens[index]->SetType(TOKEN_DIV);
        NextChar();
        break;

      case L'%':
        tokens[index]->SetType(TOKEN_MOD);
        NextChar();
        break;

      case EOB:
        tokens[index]->SetType(TOKEN_END_OF_STREAM);
        break;

      default:
        ProcessWarning();
        tokens[index]->SetType(TOKEN_UNKNOWN);
        NextChar();
        break;
    }
    return;
  }
}
