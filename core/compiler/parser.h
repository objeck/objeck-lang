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
enum McpKind {
    MK_INT, MK_FLOAT, MK_BOOL, MK_BYTE, MK_CHAR, MK_STRING, MK_JSON, MK_ENUM, MK_RECORD, MK_ARRAY, MK_VECTOR, MK_MAP, MK_CALL
};


// what a parameter, a record field or a collection element is, as far as MCP is concerned
struct McpTy {
  McpKind kind = MK_STRING;
  bool boxed = false;               // IntRef, FloatRef, ...: an element of a collection
  std::wstring name;                // an enum or a record, as written in the source
  std::vector<std::wstring> items;  // the items of an enum, the fields of a record (in order)
  std::vector<McpTy> kids;          // the element of an array, Vector or Map; the types of a record's fields
  std::wstring text;                // the type as Objeck source text
};


// one tagged function seen in a class
struct McpCandidate {
  Method* method;
  ToolDoc doc;
};

// a class with tagged functions, waiting for every file to be read
struct McpPending {
  Class* klass;
  ParsedBundle* bundle;
  std::vector<McpCandidate> tools;
  bool uses_mcp;   // its file has 'use API.MCP'
};


class Parser {
  bool alt_syntax;
  ParsedProgram* program;
  ParsedBundle* current_bundle;
  Class* current_class;
  // the 'use' names of the file being parsed, while ParseBundle reads it (null otherwise)
  std::vector<std::wstring>* current_lib_uses = nullptr;
  Method* current_method;
  // classes with '@tool' functions, derived when every file has been read; and the files that did not parse
  std::vector<McpPending> mcp_pending;
  std::set<std::wstring> mcp_broken_files;
  // the method being parsed: its type parameters and their bounds, as they erase (#1242)
  std::map<std::wstring, std::wstring> method_generic_bounds;
  // constructor parameters written '@name' that fill the field of that name (field promotion)
  std::vector<std::wstring> promoted_fields;
  bool ClassDeclaresField(const std::wstring& name);
  Method* prev_method;
  Scanner* scanner;
  SymbolTableManager* symbol_table;
  std::map<ScannerTokenType, std::wstring> error_msgs;
  DiagnosticSink diagnostics;
  std::wstring src_path;
  std::vector<std::pair<std::wstring, std::wstring> > programs;
  // '>' still owed by a '>>' or '>>>' that closed several generics at once
  int pending_generic_closes;
  bool is_semi_colon;
  // one-shot: the next ParseMethodCall(IdentifierContext&) is inside a Try() region
  bool next_call_try_guarded = false;
  // line of the token NextToken() last consumed: two statements on one line
  // need a ';' between them (#1184)
  int prev_token_line = -1;
  // tokens NextToken() has consumed: a statement list compares it before and
  // after each statement, so one that reads nothing cannot repeat forever
  size_t tokens_consumed = 0;
  // hidden declarations a statement needs to run before it, such as the local
  // that 'GetCells()[0] := 99' stores through; the enclosing list takes them
  std::vector<Statement*> pending_statements;
  // the copies back after a statement with 'ref' arguments (#1243)
  std::vector<Statement*> trailing_statements;
  int hidden_local_id = 0;
  // set once the scanner has rejected a token: the parse is skipped ahead to
  // the next ';', and what it reports at the end of the file after that is
  // the same mistake again (a skipped '{' or '}' leaves a block unbalanced)
  bool lexical_error_seen = false;
  // true from a lexical error until the next ';' is consumed: the errors raised
  // while the parser finds its footing again are echoes of the first
  bool lexical_recovery = false;
  // true from a syntax error until a declaration ('function', 'method', 'class'...)
  // starts a line: once an error has broken the structure of a body, what the parser
  // reports before it finds a declaration again is follow-on noise (a missing '{' or
  // ')' makes every later line "unexpected"), so a broken declaration reports once
  bool syntax_quiet = false;
  // set by the first syntax error: a file that has lost its structure to it reads as garbage at
  // the top level afterwards, and those "expected a class" complaints are all follow-ons
  bool syntax_error_seen = false;

  // The parser is recursive, so nesting that is deeper than the stack can hold
  // (thousands of '(' or '{' in a row, a generated 'else if' ladder) would crash the
  // compiler. Each level costs a different amount of stack (a call nests more heavily
  // than a '('), so the limit is the stack itself: a parse that has used this much of
  // it is an error instead. Real code nests a few dozen levels, far under it. The
  // later passes recurse over the tree that the parse built, so they stay in bounds too.
  static const size_t STACK_BUDGET = 400 * 1024;
  // where the parse began, the high end of the stack it grows down from
  const char* stack_base = nullptr;
  static const int MAX_CHAIN_OPERATORS = 2000;
  // operators chained in the statement being parsed ('a + b + c ...' builds a tree as deep as it is long)
  int chain_operators = 0;
  struct NestingTooDeep {};
  // each statement counts its own chained operators (a lambda's body is a statement too)
  struct ChainScope {
    int& ref;
    int saved;
    explicit ChainScope(int& r) : ref(r), saved(r) { ref = 0; }
    ~ChainScope() { ref = saved; }
  };
  void CheckDepth(int depth);
  void CheckChain();

  // true for a token that can only begin a declaration, never appear inside a body
  static inline bool IsDeclarationStart(ScannerTokenType type) {
    return type == TOKEN_FUNCTION_ID || type == TOKEN_METHOD_ID || type == TOKEN_CLASS_ID ||
           type == TOKEN_INTERFACE_ID || type == TOKEN_ENUM_ID || type == TOKEN_CONSTS_ID ||
           type == TOKEN_ALIAS_ID || type == TOKEN_BUNDLE_ID || type == TOKEN_USE_ID;
  }

#if defined(_DIAG_LIB) || defined(_MODULE)
  std::vector<Diagnostic> error_diagnostics;
#endif

  // gets the scanner token
  inline void NextToken() {
    if(lexical_recovery && scanner->GetToken()->GetType() == TOKEN_SEMI_COLON) {
      lexical_recovery = false;
    }
    prev_token_line = scanner->GetToken()->GetLineNumber();
    scanner->NextToken();
    ++tokens_consumed;
    if(syntax_quiet && IsDeclarationStart(scanner->GetToken()->GetType()) &&
       scanner->GetToken()->GetLineNumber() > prev_token_line) {
      syntax_quiet = false;
    }
  }
  // returns true of scanner token matches 
  // expected token, false otherwise
  inline bool Match(ScannerTokenType type, int index = 0) {
    return scanner->GetToken(index)->GetType() == type;
  }
  // return true if basic type, false otherwise
  bool IsBasicType(ScannerTokenType type);

  // True for the operators ParseFactor chains together. '>>>' is checked
  // separately because it sits outside the ordered TOKEN_MUL..TOKEN_XOR_ID span
  // on purpose -- see the note in scanner.h.
  inline bool IsFactorOperator() {
    return (GetToken() >= TOKEN_MUL && GetToken() <= TOKEN_XOR_ID) || Match(TOKEN_SHR_UNSIGNED);
  }
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
  // a class's enum is 'Klass#Name'; with 'in_bundle', a bundle-level enum is
  // named with its bundle, as a class is: 'Bundle.Name'
  const std::wstring GetEnumScopeName(const std::wstring &ident, bool in_bundle = false);

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

  std::wstring ParseBundleName();

  Declaration* AddDeclaration(IdentifierContext &context, Type* type, bool is_static, Declaration* child,
                              const int line_num, const int line_pos, int depth);

  // error processing
  void LoadErrorCodes();
  void ProcessError(const ScannerTokenType type);
  void ProcessError(const std::wstring &msg);
  void ProcessError(const std::wstring &msg, ParseNode * node);
  void ProcessError(const std::wstring &msg, const ScannerTokenType sync);
  // an error at the top of a file, where a file that has lost its structure to an earlier
  // syntax error reads as garbage: once one was reported it says nothing more (see
  // syntax_error_seen). Carried by the call, not read back out of the message text.
  void ProcessTopLevelError(const std::wstring &msg);
  void ProcessTopLevelError(const std::wstring &msg, const ScannerTokenType sync);
  // each file starts with none of the previous file's recovery state
  void ResetErrorState();
  // reports the scanner's reason when the current token was rejected; true if it did
  // 'panic' also holds back the errors that follow until the next ';' (for a parse that
  // skips ahead); a parse that recovers on the spot passes false
  bool ReportLexicalError(bool panic = true);
  // a rejected token where an expression starts: the scanner's reason is reported and the
  // parse goes on as if the token had been valid, so one mistake is one error
  Expression* ParseRejectedToken(int depth, const std::wstring &file_name, int line_num, int line_pos);
  // an "Expected X" message made specific: what was found instead, and at the end
  // of the file, which '{' is still open
  std::wstring Elaborate(const std::wstring &msg);
  std::wstring ElaborateExpected(const std::wstring &msg);
  // true when an error here would only repeat a string that never ended
  bool IsEchoOfLexicalError();
  void EnterSyntaxQuiet();
  void ReportSyntaxError(const std::wstring &msg, bool top_level = false);
  // A diagnosis the parser makes of a rejected token itself, in place of the scanner's
  // reason: reported at the place the scanner found wrong, with the source line.
  void ProcessTokenError(const std::wstring &msg, const ScannerTokenType sync);
  Expression* ParseBadNumericLiteral(bool negate, const std::wstring &file_name, const int line_num, const int line_pos);
  bool CheckErrors();

  // parsing operations
  void ParseFile(const std::wstring &file_name);
  void ParseText(std::pair<std::wstring, std::wstring>& progam);
  void ParseBundle(int depth);
  Class* ParseClass(const std::wstring& bundle_id, int depth);
  void GenerateRecordMethods(Class* klass, const std::wstring& file_name, int line_num, int line_pos, int depth);
  void GenerateFieldAccessors(Class* klass, const std::wstring& file_name, int line_num, int line_pos, int depth);
  // '@tool' functions (docs/design/MCP_FIRST_CLASS.md): the functions written for a class that has some
  void GenerateMcpTools(Class* klass, std::vector<McpCandidate>& tools, bool uses_mcp, int depth);
  void GeneratePendingMcpTools();
  void McpAddFunction(Class* klass, const std::wstring& code, int line, int depth);
  bool McpAnalyze(Type* type, McpTy& out, std::wstring& why, bool in_collection, int depth);
  Enum* McpFindEnum(const std::wstring& name);
  Class* McpFindClass(const std::wstring& name);
  // a tagged doc comment before something that is not a function
  void RejectToolDoc(const wchar_t* what);
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
  bool IsCurriableCall(MethodCall* method_call);
  MethodCall* ParseCurriedCalls(MethodCall* method_call, int depth);
  bool IsOperationAssignToken();
  Statement* ParseVariableOperation(Variable* variable, int line_num, int line_pos, const std::wstring& file_name, int depth);
  Statement* ParseCallElementOperation(MethodCall* head, int line_num, int line_pos, const std::wstring& file_name, int depth);
  void TakePendingStatements(StatementList* statements);
  void TakeTrailingStatements(StatementList* statements, Statement* statement);
  Expression* ParseReferenceArgument(Expression* expression);
  void CheckStatementSeparator(Statement* statement);
  void ParseAnonymousClass(MethodCall* method_call, int depth);
  StatementList* ParseStatementList(int depth);
  Statement* ParseStatement(int depth, bool semi_colon = true);
  Statement* ParseIdentifierStatement(const std::wstring& ident, int line_num, int line_pos, const std::wstring& file_name, int depth);
  Statement* ParsePrefixOperatorStatement(int line_num, int line_pos, const std::wstring& file_name, int depth);
  Statement* ParseControlFlowStatement(int line_num, int line_pos, const std::wstring& file_name, int depth);
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
  Try* ParseTry(int depth);
  For* ParseEach(bool reverse, int depth);
  Expression* HoistEachCollection(Expression* collection, const std::wstring& bound_ident, StatementList* pre_statements,
                                  const int line_num, const int line_pos, int depth);
  Return* ParseReturn(int depth);
  Leaving* ParseLeaving(int depth);
  Declaration* ParseDeclaration(IdentifierContext &context, bool is_stmt, int depth);
  DeclarationList* ParseDecelerationList(int depth);
  ExpressionList* ParseExpressionList(int& end_pos, int depth, ScannerTokenType open = TOKEN_OPEN_PAREN,
                                      ScannerTokenType close = TOKEN_CLOSED_PAREN, bool allow_named = false);
  ExpressionList* ParseCallArguments(int& end_pos, int depth, const std::wstring &method_name);
  ExpressionList* ParseIndices(int depth);
  void ParseCastTypeOf(Expression* expression, int depth);
  Type* ParseType(int depth);
  Expression* ParseExpression(int depth);
  Expression* ParseLogic(int depth);
  Expression* ParseMathLogic(int depth);
  Expression* ParseTerm(int depth);
  Expression* ParseFactor(int depth);
  Expression* ParseSimpleExpression(int depth);
  Type* ExpressionToType(Expression* expression);

 public:
  Parser(const std::wstring &s, bool a, std::vector<std::pair<std::wstring, std::wstring> > &p) {
    src_path = s;
    alt_syntax = a;
    programs = p;
    program = new ParsedProgram;
    LoadErrorCodes();
    current_class = nullptr;
    current_method = prev_method = nullptr;
    pending_generic_closes = 0;
    is_semi_colon = false;
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
  // Parses the text of a string interpolation. Null when it is not one expression; 'reason'
  // then holds why when the scanner rejected a token in it (a malformed literal, say).
  static Expression* ParseExpressionText(const std::wstring& text, const std::wstring& filename, int line_num, int line_pos, Class* context_class = nullptr,
                                         std::wstring* reason = nullptr);

#ifdef _MODULE
  std::vector<std::wstring> GetErrors();
#endif
};
