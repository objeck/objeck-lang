/***************************************************************************
 * Language parser.
 *
 * Copyright (c) 2025, Randy Hollines
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

#pragma once

#include "scanner.h"
#include <random>

using namespace frontend;

#define SECOND_INDEX 1
#define THIRD_INDEX 2
#define DEFAULT_BUNDLE_NAME L"Default"

/****************************
 * Identifier position
 ****************************/
class IdentifierContext {
  std::wstring ident;
  int line_num;
  int line_pos;

public:
  IdentifierContext(const IdentifierContext &c) {
    ident = c.ident;
    line_num = c.line_num;
    line_pos = c.line_pos;
  }
  
  IdentifierContext(const std::wstring i, int l, int p) {
    ident = i;
    line_num = l;
    line_pos = p;
  }

  ~IdentifierContext() {

  }

  int GetLineNumber() {
    return line_num;
  }

  int GetLinePosition() {
    return line_pos;
  }

  const std::wstring GetIdentifier() {
    return ident;
  }
};

/****************************
 * Turns tokens into trees
 ****************************/
class Parser {
  bool alt_syntax;
  ParsedProgram* program;
  ParsedBundle* current_bundle;
  Class* current_class;
  Method* current_method;
  Method* prev_method;
  Scanner* scanner;
  SymbolTableManager* symbol_table;
  std::map<ScannerTokenType, std::wstring> error_msgs;
  std::map<int, std::wstring> errors;
  std::wstring src_path;
  std::vector<std::pair<std::wstring, std::wstring> > programs;
  bool expand_generic_def;
  bool is_semi_colon;

#if defined(_DIAG_LIB) || defined(_MODULE)
  std::vector<std::wstring> error_strings;
#endif

  // gets the scanner token
  inline void NextToken() {
    scanner->NextToken();
  }
  // returns true of scanner token matches 
  // expected token, false otherwise
  inline bool Match(ScannerTokenType type, int index = 0) {
    return scanner->GetToken(index)->GetType() == type;
  }
  // return true if basic type, false otherwise
  bool IsBasicType(ScannerTokenType type);
  // get token by index
  inline ScannerTokenType GetToken(int index = 0) {
    return scanner->GetToken(index)->GetType();
  }
  // get line number of current token
  inline int GetLineNumber() {
    return scanner->GetToken()->GetLineNumber();
  }
  // get line position of current token
  inline int GetLinePosition() {
    const int line_pos = scanner->GetToken()->GetLinePosition();
    return line_pos > -1 ? line_pos : 0;
  }
  // get filename of current token
  inline const std::wstring GetFileName() {
    return scanner->GetToken()->GetFileName();
  }
  // gets the symbol table's name for the current scope
  const std::wstring GetScopeName(const std::wstring &ident);
  // gets the enum scope name
  const std::wstring GetEnumScopeName(const std::wstring &ident);

  // helper functions
  void Debug(const std::wstring &msg, int depth) {
    GetLogger() << std::setw(4) << GetLineNumber() << L": ";
    for(int i = 0; i < depth; ++i) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << std::endl;
  }

  inline std::wstring ToString(int v) {
    std::wostringstream str;
    str << v;
    return str.str();
  }

  static std::wstring RandomString(size_t len);

  std::wstring ParseBundleName();

  Declaration* AddDeclaration(IdentifierContext &context, Type* type, bool is_static, Declaration* child,
                              const int line_num, const int line_pos, int depth);

  // error processing
  void LoadErrorCodes();
  void ProcessError(const ScannerTokenType type);
  void ProcessError(const std::wstring &msg);
  void ProcessError(const std::wstring &msg, ParseNode * node);
  void ProcessError(const std::wstring &msg, const ScannerTokenType sync);
  bool CheckErrors();

  // parsing operations
  void ParseFile(const std::wstring &file_name);
  void ParseText(std::pair<std::wstring, std::wstring>& progam);
  void ParseBundle(int depth);
  Class* ParseClass(const std::wstring& bundle_id, int depth);
  Class* ParseInterface(const std::wstring &bundle_id, int depth);
  Method* ParseMethod(bool is_function, bool virtual_required, int depth);
  Lambda* ParseLambda(int depth);
  Variable* ParseVariable(IdentifierContext &context, int depth);
  std::vector<Type*> ParseGenericTypes(int depth);
  std::vector<Class*> ParseGenericClasses(const std::wstring &bundle_name, int depth);
  MethodCall* ParseMethodCall(int depth);
  MethodCall* ParseMethodCall(IdentifierContext &context, int depth);
  void ParseMethodCall(Expression* expression, int depth);
  MethodCall* ParseMethodCall(Variable* variable, int depth);
  void ParseAnonymousClass(MethodCall* method_call, int depth);
  StatementList* ParseStatementList(int depth);
  Statement* ParseStatement(int depth, bool semi_colon = true);
  Statement* ParseIdentifierStatement(const std::wstring& ident, int line_num, int line_pos, const std::wstring& file_name, int depth);
  Statement* ParsePrefixOperatorStatement(int line_num, int line_pos, const std::wstring& file_name, int depth);
  Statement* ParseControlFlowStatement(int line_num, int line_pos, const std::wstring& file_name, int depth);
#ifdef _SYSTEM
  Statement* ParseSystemStatement(int line_num, int line_pos, const std::wstring& file_name, int depth);
#endif
  Assignment* ParseAssignment(Variable* variable, int depth);
  StaticArray* ParseStaticArray(int depth);
  If* ParseIf(int depth);
  DoWhile* ParseDoWhile(int depth);
  While* ParseWhile(int depth);
  Select* ParseSelect(int depth);
  Enum* ParseEnum(int depth);
  Alias* ParseLambdas(int depth);
  Enum* ParseConsts(int depth);
  void CalculateConst(Expression* expression, std::stack<INT64_VALUE> &values, int depth);
  For* ParseFor(int depth);
  For* ParseEach(int depth);
  CriticalSection* ParseCritical(int depth);
  For* ParseEach(bool reverse, int depth);
  Return* ParseReturn(int depth);
  Leaving* ParseLeaving(int depth);
  Declaration* ParseDeclaration(IdentifierContext &context, bool is_stmt, int depth);
  DeclarationList* ParseDecelerationList(int depth);
  ExpressionList* ParseExpressionList(int& end_pos, int depth, ScannerTokenType open = TOKEN_OPEN_PAREN,
                                      ScannerTokenType close = TOKEN_CLOSED_PAREN);
  ExpressionList* ParseIndices(int depth);
  void ParseCastTypeOf(Expression* expression, int depth);
  Type* ParseType(int depth);
  Expression* ParseExpression(int depth);
  Expression* ParseLogic(int depth);
  Expression* ParseMathLogic(int depth);
  Expression* ParseTerm(int depth);
  Expression* ParseFactor(int depth);
  Expression* ParseSimpleExpression(int depth);

 public:
  Parser(const std::wstring &s, bool a, std::vector<std::pair<std::wstring, std::wstring> > &p) {
    src_path = s;
    alt_syntax = a;
    programs = p;
    program = new ParsedProgram;
    LoadErrorCodes();
    current_class = nullptr;
    current_method = prev_method = nullptr;
    expand_generic_def = is_semi_colon  = false;
  }

  ~Parser() {
#ifndef _DIAG_LIB
    if(program) {
      delete program;
      program = nullptr;
    }
#endif
  }

  bool Parse();

  ParsedProgram* GetProgram() {
    return program;
  }

  SymbolTableManager* GetSymbolTable() {
    return symbol_table;
  }

  // Parses an expression from a text string (used for string interpolation)
  static Expression* ParseExpressionText(const std::wstring& text, const std::wstring& filename, int line_num, int line_pos);

#ifdef _MODULE
  std::vector<std::wstring> GetErrors();
#endif
};
