/***************************************************************************
 * Debugger parser.
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

#ifndef __PARSER_H__
#define __PARSER_H__

#include "scanner.h"
#include "tree.h"

using namespace frontend;

#define SECOND_INDEX 1
#define THIRD_INDEX 2

/****************************
 * Parsers source files.
 ****************************/
class Parser {
  Scanner* scanner;
  std::map<enum TokenType, std::wstring> error_msgs;
  std::vector<std::wstring> errors;
  
  inline void NextToken() {
    scanner->NextToken();
  }

  inline bool Match(enum TokenType type, int index = 0) {
    return scanner->GetToken(index)->GetType() == type;
  }

  inline enum TokenType GetToken(int index = 0) {
    return scanner->GetToken(index)->GetType();
  }

  void Show(const std::wstring &msg, int depth) {
    for(int i = 0; i < depth; i++) {
      std::wcout << L"  ";
    }
    std::wcout << msg << std::endl;
  }
  
  inline std::wstring ToString(int v) {
    std::wostringstream str;
    str << v;
    return str.str();
  }

  // error processing
  void LoadErrorCodes();
  void ProcessError(const enum TokenType type);
  void ProcessError(const std::wstring &msg);
  bool CheckErrors();

  // parsing operations
  Command* ParseLine(const std::wstring& file_name);
  Command* ParseStatement(int depth);
  Command* ParseLoad(CommandType type, int depth);
  Command* ParseList(int depth);
  Command* ParseBreakDelete(bool is_break, int depth);
  Command* ParsePrint(int depth);
  Command* ParseInfo(int depth);
  Command* ParseFrame(int depth);
  ExpressionList* ParseIndices(int depth);
  Expression* ParseExpression(int depth);
  Expression* ParseLogic(int depth);
  Expression* ParseMathLogic(int depth);
  Expression* ParseTerm(int depth);
  Expression* ParseFactor(int depth);
  Expression* ParseSimpleExpression(int depth);
  Reference* ParseReference(int depth);
  Reference* ParseReference(const std::wstring &ident, int depth);
  void ParseReference(Reference* reference, int depth);
  
 public:
  Parser() {
    LoadErrorCodes();
  }
  
  ~Parser() {
  }
  
  Command* Parse(const std::wstring &line);
};

#endif
