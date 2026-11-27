/***************************************************************************
 * Language parser.
 *
 * Copyright (c) 2025, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met
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

#include "parser.h"

#include <set>      // CheckErrors dedups before the cap
#include <vector>
#include <algorithm>
#include <map>
#include <sstream>

bool Parser::IsBasicType([[maybe_unused]] ScannerTokenType type)
{
  switch(GetToken()) {
  case TOKEN_BOOLEAN_ID:
  case TOKEN_BYTE_ID:
  case TOKEN_INT_ID:
  case TOKEN_FLOAT_ID:
  case TOKEN_CHAR_ID:
    return true;

  default:
    break;
  }

  return false;
}

const std::wstring Parser::GetScopeName(const std::wstring& ident)
{
  std::wstring scope_name;
  if(current_method) {
    scope_name = current_method->GetName() + L':' + ident;
  }
  else if(current_class) {
    scope_name = current_class->GetName() + L':' + ident;
  }
  else {
    scope_name = ident;
  }

  return scope_name;
}

const std::wstring Parser::GetEnumScopeName(const std::wstring& ident, bool in_bundle)
{
  std::wstring scope_name;
  if(current_class) {
    scope_name = current_class->GetName() + L"#" + ident;
  }
  else if(in_bundle && current_bundle && !current_bundle->GetName().empty()) {
    scope_name = current_bundle->GetName() + L'.' + ident;
  }
  else {
    scope_name = ident;
  }

  return scope_name;
}

std::wstring Parser::ParseBundleName()
{
  std::wstring name;
  if(Match(TOKEN_IDENT)) {
    while(Match(TOKEN_IDENT)) {
      name += scanner->GetToken()->GetIdentifier();
      NextToken();
      if(Match(TOKEN_PERIOD)) {
        name += L'.';
        NextToken();
        // 'x.;' left the '.' on the end of the name, which then failed later as an
        // "Undefined variable: 'x.'" -- say what is actually wrong, where it is
        if(!Match(TOKEN_IDENT)) {
          ProcessError(L"Expected a name after '.'\n\tA dotted name such as System.IO needs an identifier after each '.'; "
                       L"to call a method use '->', as in x->Method()", TOKEN_SEMI_COLON);
          while(name.size() && name.back() == L'.') {
            name.pop_back();
          }
          break;
        }
      }
      else if(Match(TOKEN_IDENT)) {
        // 'if(value is Type name)': the value's name ends before 'is', and the
        // type's before the name (see ParseTypeTest)
        if(in_type_test_type || (in_if_condition > 0 && scanner->GetToken()->GetIdentifier() == L"is")) {
          break;
        }
        if(scanner->GetToken()->GetIdentifier() == L"is") {
          ProcessError(L"'is' tests a type only as the whole condition of an 'if': 'if(value is Type name) { ... }'", TOKEN_SEMI_COLON);
        }
        else {
          ProcessError(L"Expected '.'", TOKEN_SEMI_COLON);
        }
        NextToken();
      }
    }
  }
  else {
    ProcessError(TOKEN_IDENT);
  }

  return name;
}

/****************************
  * Loads parsing error codes.
  ****************************/
void Parser::LoadErrorCodes()
{
  error_msgs[TOKEN_NATIVE_ID] = L"Expected 'native'";
  error_msgs[TOKEN_AS_ID] = L"Expected 'As'";
  error_msgs[TOKEN_IDENT] = L"Expected identifier";
  error_msgs[TOKEN_OPEN_PAREN] = L"Expected '('";
  error_msgs[TOKEN_CLOSED_PAREN] = L"Expected ')'";
  error_msgs[TOKEN_OPEN_BRACKET] = L"Expected '['";
  error_msgs[TOKEN_CLOSED_BRACKET] = L"Expected ']'";
  error_msgs[TOKEN_OPEN_BRACE] = L"Expected '{'";
  error_msgs[TOKEN_CLOSED_BRACE] = L"Expected '}'";
  error_msgs[TOKEN_COLON] = L"Expected ':'";
  error_msgs[TOKEN_COMMA] = L"Expected ','";
  error_msgs[TOKEN_ASSIGN] = L"Expected ':='";
  error_msgs[TOKEN_SEMI_COLON] = L"Expected ';'";
  error_msgs[TOKEN_ASSESSOR] = L"Expected '->'";
  error_msgs[TOKEN_TILDE] = L"Expected '~'";
}

/****************************
 * When the current token is one
 * the scanner rejected, the
 * error is the scanner's own
 * reason: a bad number, an
 * unterminated string, an
 * escape that does not exist.
 ****************************/
bool Parser::ReportLexicalError(bool panic)
{
  Token* token = scanner->GetToken();
  const ScannerTokenType type = token->GetType();
  if((type == TOKEN_UNKNOWN || type == TOKEN_BAD_CHAR_STRING_LIT) && !token->GetError().empty()) {
    lexical_error_seen = true;
    if(panic) {
      lexical_recovery = true;
    }
    diagnostics.Error(token->GetFileName(), token->GetErrorLine(), token->GetErrorPos(), token->GetError());
    return true;
  }

  return false;
}

/****************************
 * A token the scanner rejected,
 * where an expression starts. A
 * bad literal stands in as a
 * literal. A stray operator (the
 * '!' of '!done') is skipped and
 * what follows it is parsed, so
 * the statement around it still
 * parses: one mistake, one error.
 ****************************/
Expression* Parser::ParseRejectedToken(int depth, const std::wstring &file_name, int line_num, int line_pos)
{
  CheckDepth(depth);
  Token* token = scanner->GetToken();
  const std::wstring source = scanner->GetSourceLine(token->GetErrorLine());
  const int at = token->GetErrorPos() - 1;
  const wchar_t first = at >= 0 && at < (int)source.size() ? source[at] : L' ';

  // a rejected word (a name with an invisible character in it) is a whole operand that
  // cannot be read, so what follows it on the statement is not reported a second time
  const bool is_word = iswalpha(first) || first == L'_';
  if(!ReportLexicalError(is_word)) {
    ProcessError(L"Invalid expression: unexpected token", TOKEN_SEMI_COLON);
    return nullptr;
  }

  NextToken();

  // what stands in for a literal is a literal; a skipped operator leaves its operand
  const bool is_literal = is_word || iswdigit(first) || first == L'.' || first == L'\'' || first == L'"';
  if(is_literal || Match(TOKEN_SEMI_COLON) || Match(TOKEN_CLOSED_PAREN) || Match(TOKEN_COMMA) ||
     Match(TOKEN_CLOSED_BRACKET) || Match(TOKEN_END_OF_STREAM)) {
    return TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);
  }

  return ParseSimpleExpression(depth + 1);
}

/****************************
 * The parser's own diagnosis of a
 * token the scanner rejected (a
 * lambda written inside a string
 * interpolation reads to the
 * scanner as a bad escape). It is
 * reported where the scanner found
 * the fault, with the source line.
 ****************************/
void Parser::ProcessTokenError(const std::wstring &msg, const ScannerTokenType sync)
{
  Token* token = scanner->GetToken();
  const int line = token->GetErrorLine();
  const int pos = token->GetErrorPos();
  lexical_error_seen = true;
  lexical_recovery = true;
  diagnostics.Error(token->GetFileName(), line, pos, msg + scanner->Excerpt(line, pos));

  ScannerTokenType current = GetToken();
  while(current != sync && current != TOKEN_END_OF_STREAM) {
    NextToken();
    current = GetToken();
  }
}

/****************************
 * True for an error that only
 * repeats a lexical error
 * already reported: one raised
 * while the parser finds its
 * footing after it, or at the
 * end of a file whose structure
 * that error disturbed.
 ****************************/
bool Parser::IsEchoOfLexicalError()
{
  return lexical_recovery || syntax_quiet || (lexical_error_seen && GetToken() == TOKEN_END_OF_STREAM);
}

/****************************
 * Stops a parse that nests
 * deeper than the stack can
 * hold: reports it, then
 * unwinds out of the file.
 ****************************/
void Parser::CheckDepth([[maybe_unused]] int depth)
{
  char marker = 0;
  if(stack_base) {
    const char* here = &marker;
    const size_t used = stack_base > here ? (size_t)(stack_base - here) : (size_t)(here - stack_base);
    if(used > STACK_BUDGET) {
      diagnostics.Error(GetFileName(), GetLineNumber(), GetLinePosition(),
                        L"Nesting is too deep: parentheses, blocks, calls or types are nested more deeply here than the compiler can read"
                        L"\n\tReal code rarely nests past a few dozen levels. Split this into methods or temporary variables");
      throw NestingTooDeep();
    }
  }
}

void Parser::CheckChain()
{
  if(++chain_operators > MAX_CHAIN_OPERATORS) {
    diagnostics.Error(GetFileName(), GetLineNumber(), GetLinePosition(),
                      L"Expression is too long: more than " + std::to_wstring(MAX_CHAIN_OPERATORS) +
                      L" chained operators in one statement. Split it into several statements");
    throw NestingTooDeep();
  }
}

/****************************
 * Called after a syntax error
 * is reported: the errors that
 * follow, until the next
 * declaration, are follow-ons.
 * An error raised at a
 * declaration's first token
 * needs no quiet: the parser is
 * already at a place to resume.
 ****************************/
void Parser::EnterSyntaxQuiet()
{
  Token* token = scanner->GetToken();
  if(!(IsDeclarationStart(token->GetType()) && token->GetLineNumber() > prev_token_line)) {
    syntax_quiet = true;
  }
}

/****************************
 * Makes an "Expected X"
 * message specific: names what
 * was found instead, and at the
 * end of the file the '{' that
 * was never closed. Other
 * messages are returned as given.
 ****************************/
std::wstring Parser::Elaborate(const std::wstring &msg)
{
  std::wstring out = ElaborateExpected(msg);

  // An error on the line where a multi-line string ends is very often a missing closing
  // quote at its start: the string ran on to the next quote, and what follows is read wrong.
  const Scanner::MultiLineString* multi = scanner->FindMultiLineStringEndingOn(GetLineNumber());
  if(multi && out.find(L"string literal") == std::wstring::npos) {
    out += L"\n\tNote: the string literal opened at line " + std::to_wstring(multi->line) + L", column " + std::to_wstring(multi->pos) +
           L" runs " + std::to_wstring(multi->end_line - multi->line) + L" line(s) and ends on this line. If it should have ended on line " +
           std::to_wstring(multi->line) + L", the closing '\"' is missing there:" + scanner->Excerpt(multi->line, multi->pos);
  }

  return out;
}

std::wstring Parser::ElaborateExpected(const std::wstring &msg)
{
  // only the short "Expected X" forms, and none that already carry detail
  if(msg.compare(0, 8, L"Expected") != 0 || msg.size() > 48 || msg.find(L'\n') != std::wstring::npos) {
    return msg;
  }

  std::wstring out = msg;
  Token* token = scanner->GetToken();
  const std::wstring found = scanner->DescribeToken(token);
  if(!found.empty()) {
    out += L", found " + found;
  }

  // a declaration keyword inside a body: the body above never closed
  if(IsDeclarationStart(token->GetType()) && token->GetLineNumber() > prev_token_line && token->GetType() != TOKEN_USE_ID) {
    out += L"\n\t" + scanner->DescribeToken(token) + L" begins a declaration, which cannot appear inside a method body: the block above is still open, so a closing '}' is missing before this line";
    const std::vector<Scanner::OpenBrace> open = scanner->FindUnclosedBraces();
    if(!open.empty()) {
      out += L"\n\tThe indentation suggests the '{' opened at line " + std::to_wstring(open.front().line) + L", column " + std::to_wstring(open.front().pos) +
             L" is the one never closed:" + scanner->Excerpt(open.front().line, open.front().pos);
    }
  }

  if(token->GetType() == TOKEN_END_OF_STREAM && msg == L"Expected '}'") {
    const std::vector<Scanner::OpenBrace> open = scanner->FindUnclosedBraces();
    if(!open.empty()) {
      const Scanner::OpenBrace& brace = open.front();
      out += L"\n\tThe '{' opened at line " + std::to_wstring(brace.line) + L", column " + std::to_wstring(brace.pos) +
             L" is never closed:" + scanner->Excerpt(brace.line, brace.pos) + L"\n\tAdd a matching '}' (the indentation suggests that block";
      out += open.size() > 1 ? L"; " + std::to_wstring(open.size()) + L" '{' in this file are unclosed, this is the likeliest)" : L")";
    }
  }

  return out;
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(ScannerTokenType type)
{
  if(ReportLexicalError() || IsEchoOfLexicalError()) {
    return;
  }

  ReportSyntaxError(error_msgs[type]);
}

/****************************
 * Reports a syntax error where
 * the parser stands, except a
 * missing ';': that is at the
 * end of the line it belongs to,
 * not at the next token, which
 * is on a later line.
 ****************************/
void Parser::ReportSyntaxError(const std::wstring &msg, bool top_level)
{
  // what a top-level parse says after an earlier error in this file is noise
  if(syntax_error_seen && top_level) {
    return;
  }
  syntax_error_seen = true;

  if(msg == L"Expected ';'" && prev_token_line > 0 && GetLineNumber() > prev_token_line && scanner->GetToken()->GetType() != TOKEN_END_OF_STREAM) {
    std::wstring text = scanner->GetSourceLine(prev_token_line);
    size_t end = text.find_last_not_of(L" \t\r");
    if(end != std::wstring::npos) {
      const std::wstring found = scanner->DescribeToken(scanner->GetToken());
      diagnostics.Error(GetFileName(), prev_token_line, (int)end + 2, L"Expected ';' at the end of this line, before " + found +
                        L" on line " + std::to_wstring(GetLineNumber()) + scanner->Excerpt(prev_token_line, (int)end + 1));
      EnterSyntaxQuiet();
      return;
    }
  }

  diagnostics.Error(GetFileName(), GetLineNumber(), GetLinePosition(), Elaborate(msg));
  EnterSyntaxQuiet();
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg)
{
  if(ReportLexicalError() || IsEchoOfLexicalError()) {
    return;
  }

  ReportSyntaxError(msg);
}

/****************************
 * Emits a top-level parsing error.
 ****************************/
void Parser::ProcessTopLevelError(const std::wstring &msg)
{
  if(ReportLexicalError() || IsEchoOfLexicalError()) {
    return;
  }

  ReportSyntaxError(msg, true);
}

void Parser::ProcessTopLevelError(const std::wstring &msg, ScannerTokenType sync)
{
  if(!ReportLexicalError() && !IsEchoOfLexicalError()) {
    ReportSyntaxError(msg, true);
  }

  ScannerTokenType token = GetToken();
  while(token != sync && token != TOKEN_END_OF_STREAM) {
    NextToken();
    token = GetToken();
  }
}

/****************************
 * Starts a file with none of the
 * previous file's error state: its
 * recovery flags described a
 * different token stream.
 ****************************/
void Parser::ResetErrorState()
{
  lexical_error_seen = false;
  lexical_recovery = false;
  syntax_quiet = false;
  syntax_error_seen = false;
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg, ScannerTokenType sync)
{
  if(!ReportLexicalError() && !IsEchoOfLexicalError()) {
    ReportSyntaxError(msg);
  }

  ScannerTokenType token = GetToken();
  while(token != sync && token != TOKEN_END_OF_STREAM) {
    NextToken();
    token = GetToken();
  }
}

/****************************
 * The fix-it for a decimal
 * literal with a leading zero:
 * '010' suggests 0o10 (octal 8)
 * or 10; '09' has no octal
 * reading, so it suggests 9.
 ****************************/
static std::wstring LeadingZeroAdvice(const std::wstring &digits)
{
  size_t start = digits.find_first_not_of(L'0');
  const std::wstring trimmed = start == std::wstring::npos ? L"0" : digits.substr(start);
  if(trimmed.find_first_of(L"89") == std::wstring::npos) {
    const unsigned long long octal = wcstoull(trimmed.c_str(), nullptr, 8);
    return L"write '0o" + trimmed + L"' for octal (" + std::to_wstring(octal) +
      L") or '" + trimmed + L"' for decimal";
  }
  return L"write '" + trimmed + L"' for decimal; octal literals take a '0o' prefix";
}

/****************************
 * An integer or float literal
 * outside its type's range (the
 * scanner's TOKEN_BAD_INT_LIT and
 * TOKEN_BAD_FLOAT_LIT). The error
 * names the literal, and the token
 * is then taken as a literal of
 * its type, so the parse goes on
 * as if the value were valid: the
 * one error is the only error. The
 * old recovery skipped to the next
 * ';', and the skipped half of the
 * expression (or array literal)
 * reported more errors after it.
 * '-9223372036854775808' is Int's
 * minimum and is no error.
 ****************************/
Expression* Parser::ParseBadNumericLiteral(bool negate, const std::wstring &file_name, const int line_num, const int line_pos)
{
  Token* token = scanner->GetToken();
  const std::wstring literal = (negate ? L"-" : L"") + token->GetIdentifier();

  Expression* expression = nullptr;
  if(token->GetType() == TOKEN_BAD_FLOAT_LIT) {
    ProcessError(L"Float literal out of range: '" + literal +
                 L"'\n\tFloat is a 64-bit double: a nonzero magnitude must be from 4.9406564584124654e-324 to 1.7976931348623157e308");
    expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos, 0.0);
  }
  else if(token->GetType() == TOKEN_LEADING_ZERO_INT_LIT) {
    std::wstring advice = LeadingZeroAdvice(token->GetIdentifier());
    advice[0] = L'W';
    ProcessError(L"Integer literal with a leading zero: '" + literal + L"'\n\t" + advice);
    expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);
  }
  else if(negate && token->GetInt64Lit() == INT64_MIN) {
    expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, INT64_MIN);
  }
  else {
    ProcessError(L"Integer literal out of range: '" + literal +
                 L"'\n\tInt is 64 bits: -9223372036854775808 to 9223372036854775807, or up to 18446744073709551615 with a 'u' suffix");
    expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);
  }
  NextToken();

  return expression;
}

/****************************
 * Negates an integer literal's
 * value. Through unsigned
 * arithmetic: negating
 * 0x8000000000000000 (Int's
 * minimum) is itself, where a
 * signed negation overflows.
 ****************************/
static INT64_VALUE NegateIntLiteral(INT64_VALUE value)
{
  return (INT64_VALUE)(0ULL - (unsigned long long)value);
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg, ParseNode * node)
{
  diagnostics.Error(node->GetFileName(), node->GetLineNumber(), node->GetLinePosition(), msg);
}

/****************************
 * Checks for parsing errors.
 ****************************/
bool Parser::CheckErrors()
{
  // check and process errors
  if(diagnostics.HasErrors()) {
    const size_t error_max = 8;

    // Repeats are dropped before the cap, not after (DiagnosticSink).
    const std::vector<Diagnostic> unique_errors = diagnostics.UniqueErrors();

    const size_t shown = unique_errors.size() < error_max ? unique_errors.size() : error_max;
    for(size_t i = 0; i < shown; ++i) {
#if defined(_DIAG_LIB) || defined(_MODULE)
      error_diagnostics.push_back(unique_errors[i]);
#else
      std::wcerr << unique_errors[i].Format() << std::endl;
#endif
    }

#ifdef _DIAG_LIB
    program->SetErrorDiagnostics(error_diagnostics);
#endif

    return false;
  }

  return true;
}

#ifdef _MODULE
std::vector<std::wstring> Parser::GetErrors()
{
  std::vector<std::wstring> lines;
  for(size_t i = 0; i < error_diagnostics.size(); ++i) {
    lines.push_back(error_diagnostics[i].Format());
  }

  return lines;
}
#endif

/****************************
 * Starts the parsing process.
 ****************************/
bool Parser::Parse()
{
#ifdef _DEBUG
  GetLogger() << L"\n---------- Scanning/Parsing ---------" << std::endl;
#endif

  // Everything parsed below belongs to this program's factories rather than to a
  // process-wide pair, which is what lets two threads parse at once (issue #659).
  ScopedProgramFactories scoped_factories(program);

  // parses source path
  if(src_path.size() > 0) {
    size_t offset = 0;
    size_t index = src_path.find(',');
    while(index != std::wstring::npos) {
      const std::wstring &file_name = src_path.substr(offset, index - offset);
      ParseFile(file_name);
      // update
      offset = index + 1;
      index = src_path.find(',', offset);
    }
    const std::wstring &file_name = src_path.substr(offset, src_path.size());
    ParseFile(file_name);
  }
  else if(programs.size() > 0) {
    for(size_t i = 0; i < programs.size(); ++i) {
      ParseText(programs[i]);
    }
  }

  if(!mcp_entry_refusals.empty()) {
    ReportMcpEntries();
  }
  GeneratePendingMcpTools();
  // after the tools are derived: the check looks for the function they are registered by
  if(!agent_tool_classes.empty()) {
    CheckAgentToolClasses();
  }

  return CheckErrors();
}

/****************************
 * Parses a file.
 ****************************/
void Parser::ParseFile(const std::wstring &file_name)
{
  ResetErrorState();
  const size_t errors_before = diagnostics.ErrorCount();
  diagnostics.NoteFile(file_name);
  scanner = new Scanner(file_name, alt_syntax);
  NextToken();
  char stack_mark = 0;
  stack_base = &stack_mark;
  try {
    ParseBundle(0);
  }
  catch(const NestingTooDeep&) {
    // reported where it was found; the rest of the file is not read
  }
  stack_base = nullptr;
  if(diagnostics.ErrorCount() != errors_before) {
    mcp_broken_files.insert(file_name);
  }
  // clean up
  delete scanner;
  scanner = nullptr;
}

/****************************
 * Parses std::string
 ****************************/
void Parser::ParseText(std::pair<std::wstring, std::wstring> &progam)
{
  ResetErrorState();
  const size_t errors_before = diagnostics.ErrorCount();
  diagnostics.NoteFile(progam.first);
  scanner = new Scanner(progam.first, alt_syntax, progam.second);
  NextToken();
  char stack_mark = 0;
  stack_base = &stack_mark;
  try {
    ParseBundle(0);
  }
  catch(const NestingTooDeep&) {
    // reported where it was found; the rest of the text is not read
  }
  stack_base = nullptr;
  if(diagnostics.ErrorCount() != errors_before) {
    mcp_broken_files.insert(progam.first);
  }
  // clean up
  delete scanner;
  scanner = nullptr;
}

/****************************
 * Parses a bundle.
 ****************************/
void Parser::ParseBundle(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  if(Match(TOKEN_END_OF_STREAM)) {
    if(!scanner->HasSource()) {
      ProcessError(L"Unable to open source file: '" + file_name + L"'\n\tThe file is missing or cannot be read, or its text is not valid UTF-8");
    }
    else if(scanner->SourceSize() == 0) {
      ProcessError(L"Source file is empty: '" + file_name + L"'\n\tA program needs at least one class");
    }
    else {
      ProcessError(L"Source file has no code: '" + file_name + L"'\n\tIt holds only comments and whitespace; a program needs at least one class");
    }
    return;
  }

  // default system uses
  std::vector<std::wstring> lib_uses;
  current_lib_uses = &lib_uses;
  struct ResetUses {
    std::vector<std::wstring>*& slot;
    ~ResetUses() { slot = nullptr; }
  } reset_uses{current_lib_uses};
  lib_uses.push_back(L"System");
  lib_uses.push_back(L"System.IO");
  lib_uses.push_back(L"System.Introspection");
  const size_t initial_uses_size = lib_uses.size();

  // parse 'use' type declarations 
  std::vector<Type*> static_uses;
  while(Match(TOKEN_USE_ID)) {
    NextToken();

    // 'use' or 'use bundle' to import classes via bundles
    if(Match(TOKEN_IDENT) || Match(TOKEN_BUNDLE_ID)) {
      if(Match(TOKEN_BUNDLE_ID)) {
        NextToken();
      }

      while(Match(TOKEN_IDENT)) {
        const std::wstring ident = ParseBundleName();
#ifdef _DEBUG
        Debug(L"search: " + ident, depth);
#endif
        lib_uses.push_back(ident);

        if(Match(TOKEN_COMMA) && !Match(TOKEN_SEMI_COLON, SECOND_INDEX)) {
          NextToken();
        }
      }

      if(lib_uses.size() == initial_uses_size) {
        ProcessError(L"Expected 'use bundle' or 'use' arguments", TOKEN_SEMI_COLON);
      }

      if(Match(TOKEN_SEMI_COLON)) {
        NextToken();
      }
    }
    // 'use class' to import functions via classes
    else if(Match(TOKEN_CLASS_ID)) {
      NextToken();

      bool is_types = false;
      while(!is_types) {
        switch(GetToken()) {
        case TOKEN_BYTE_ID:
          static_uses.push_back(TypeFactory::Instance()->MakeType(BYTE_TYPE));
          NextToken();
          break;

        case TOKEN_CHAR_ID:
          static_uses.push_back(TypeFactory::Instance()->MakeType(CHAR_TYPE));
          NextToken();
          break;

        case TOKEN_INT_ID:
          static_uses.push_back(TypeFactory::Instance()->MakeType(INT_TYPE));
          NextToken();
          break;

        case TOKEN_FLOAT_ID:
          static_uses.push_back(TypeFactory::Instance()->MakeType(FLOAT_TYPE));
          NextToken();
          break;

        case TOKEN_BOOLEAN_ID:
          static_uses.push_back(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE));
          NextToken();
          break;

        case TOKEN_IDENT: {
          const std::wstring ident = ParseBundleName();
          static_uses.push_back(TypeFactory::Instance()->MakeType(CLASS_TYPE, ident));
        }
          break;

        case TOKEN_COMMA:
          NextToken();
          switch(GetToken()) {
            case TOKEN_BYTE_ID:
            case TOKEN_CHAR_ID:
            case TOKEN_INT_ID:
            case TOKEN_FLOAT_ID:
            case TOKEN_BOOLEAN_ID:
            case TOKEN_IDENT:
              break;

            default:
              ProcessError(TOKEN_IDENT);
              break;
          }
          break;

        default:
          is_types = true;
          break;
        }
      }

      if(static_uses.empty()) {
        ProcessError(L"Expected 'use function' arguments", TOKEN_SEMI_COLON);
      }

      // semi-colon at the end of block statements, optional
      if(Match(TOKEN_SEMI_COLON)) {
        NextToken();
      }
    }
  }

  // parse file
  while(!Match(TOKEN_END_OF_STREAM)) {
    // parse bundle
    if(Match(TOKEN_BUNDLE_ID)) {
      while(Match(TOKEN_BUNDLE_ID)) {
        // where this bundle starts: the 'bundle' keyword, not the start of the
        // file's parse, which is the first 'use' and was reported as the start
        // of every bundle in the file
        const int bundle_line_num = GetLineNumber();
        const int bundle_line_pos = GetLinePosition();
        NextToken();

        std::wstring bundle_name = ParseBundleName();
        if(bundle_name == DEFAULT_BUNDLE_NAME) {
          bundle_name = L"";
        }
        else {
          lib_uses.push_back(bundle_name);
        }
        
        ParsedBundle* bundle = program->GetBundle(bundle_name);
        if(bundle) {
          symbol_table = bundle->GetSymbolTableManager();
        }
        else {
          symbol_table = new SymbolTableManager;
          bundle = new ParsedBundle(file_name, bundle_line_num, bundle_line_pos, bundle_name, symbol_table);
          program->AddBundle(bundle);
        }
        current_bundle = bundle;

        if(!Match(TOKEN_OPEN_BRACE)) {
          ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
        }
        NextToken();

#ifdef _DEBUG
        Debug(L"bundle: '" + current_bundle->GetName() + L"'", depth);
#endif

        // parse classes, interfaces and enums
        while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
          switch(GetToken()) {
          case TOKEN_ENUM_ID:
            bundle->AddEnum(ParseEnum(depth + 1));
            break;

          case TOKEN_CONSTS_ID:
            bundle->AddEnum(ParseConsts(depth + 1));
            break;

          case TOKEN_ALIAS_ID:
            bundle->AddLambdas(ParseLambdas(depth + 1));
            break;

          case TOKEN_CLASS_ID:
          case TOKEN_RECORD_ID:
            bundle->AddClass(ParseClass(bundle_name, depth + 1));
            break;

          case TOKEN_INTERFACE_ID:
            bundle->AddClass(ParseInterface(bundle_name, depth + 1));
            break;

          default:
            // 'agent Name {': a contextual keyword, so 'agent' stays an ordinary name
            if(IsAgentDeclaration()) {
              Class* agent_class = ParseAgent(bundle_name, depth + 1);
              if(agent_class) {
                bundle->AddClass(agent_class);
              }
              break;
            }
            ProcessTopLevelError(L"Expected 'class', 'record', 'interface', 'enum' or 'alias'", TOKEN_SEMI_COLON);
            NextToken();
            break;
          }
        }

        // A named bundle's end was never recorded -- only the unnamed bundle's
        // was -- so it read as uninitialized memory. Recorded by the file that
        // declared the bundle; a later file adding to it does not move it.
        if(bundle->GetFileName() == file_name) {
          bundle->SetEndLineNumber(GetLineNumber());
          bundle->SetEndLinePosition(GetLinePosition());
        }

        if(!Match(TOKEN_CLOSED_BRACE)) {
          ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
        }
        NextToken();

      }

      program->AddUses(lib_uses, static_uses, file_name);
    }
    // parse class
    else if(Match(TOKEN_CLASS_ID) || Match(TOKEN_RECORD_ID) || Match(TOKEN_ENUM_ID) || Match(TOKEN_CONSTS_ID) || Match(TOKEN_INTERFACE_ID) || Match(TOKEN_ALIAS_ID) ||
            IsAgentDeclaration()) {
      if(scanner->GetToken()->GetDocId() >= 0) {
        RejectToolDoc(L"a class, enum or other declaration");
      }
      std::wstring bundle_name = L"";
      
      ParsedBundle* bundle = program->GetBundle(bundle_name);
      if(bundle) {
        symbol_table = bundle->GetSymbolTableManager();
      }
      else {
        symbol_table = new SymbolTableManager;
        bundle = new ParsedBundle(file_name, line_num, line_pos, bundle_name, symbol_table);
        program->AddBundle(bundle);
      }
      current_bundle = bundle;

#ifdef _DEBUG
      Debug(L"bundle: '" + current_bundle->GetName() + L"'", depth);
#endif

      // parse classes, interfaces and enums
      while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
        switch(GetToken()) {
        case TOKEN_ENUM_ID:
          bundle->AddEnum(ParseEnum(depth + 1));
          break;

        case TOKEN_CONSTS_ID:
          bundle->AddEnum(ParseConsts(depth + 1));
          break;

        case TOKEN_CLASS_ID:
        case TOKEN_RECORD_ID:
          bundle->AddClass(ParseClass(bundle_name, depth + 1));
          break;

        case TOKEN_ALIAS_ID:
          bundle->AddLambdas(ParseLambdas(depth + 1));
          break;

        case TOKEN_INTERFACE_ID:
          bundle->AddClass(ParseInterface(bundle_name, depth + 1));
          break;

        default:
          // 'agent Name {': a contextual keyword, so 'agent' stays an ordinary name
          if(IsAgentDeclaration()) {
            Class* agent_class = ParseAgent(bundle_name, depth + 1);
            if(agent_class) {
              bundle->AddClass(agent_class);
            }
            break;
          }
          ProcessTopLevelError(L"Expected 'class', 'record', 'interface', 'enum' or 'alias'", TOKEN_SEMI_COLON);
          NextToken();
          break;
        }
      }

      bundle->SetEndLineNumber(GetLineNumber());
      bundle->SetEndLinePosition(GetLinePosition());

      program->AddUses(lib_uses, static_uses, file_name);
    }
    // error
    else {
      ProcessTopLevelError(L"Expected 'use', 'bundle', 'class', 'record', 'interface', 'enum' or 'consts'");
      NextToken();
    }
  }
}

/****************************
 * Parses an enum
 ****************************/
Enum* Parser::ParseEnum(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring enum_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(enum_name) || current_bundle->GetEnum(enum_name) || current_bundle->GetEnum(GetEnumScopeName(enum_name, true)) ||
     current_bundle->GetAlias(enum_name) || current_bundle->GetAlias(GetEnumScopeName(enum_name, true))) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();
  const std::wstring enum_scope_name = GetEnumScopeName(enum_name, true);
  // A class's enum, 'Klass#Name', is in scope in the class and its children
  // (ContextAnalyzer::SearchScopedEnumName). It was also added as a use of
  // 'Klass#' program-wide, so an unqualified 'Name' anywhere found the first
  // such class's enum.

#ifdef _DEBUG
  Debug(L"[Enum: name='" + enum_scope_name + L"']", depth);
#endif

  INT64_VALUE offset = 0;
  if(Match(TOKEN_ASSIGN)) {
    NextToken();
    Expression* label = ParseSimpleExpression(depth + 1);
    if(label) {
      if(label->GetExpressionType() == INT_LIT_EXPR) {
        offset = static_cast<IntegerLiteral*>(label)->GetValue();
      }
      else if(label->GetExpressionType() == CHAR_LIT_EXPR) {
        offset = static_cast<CharacterLiteral*>(label)->GetValue();
      }
      else {
        ProcessError(L"Expected integer/character literal", TOKEN_CLOSED_PAREN);
      }
    }
  }

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  Enum* eenum = TreeFactory::Instance()->MakeEnum(file_name, line_num, line_pos, -1, -1, enum_scope_name, offset);
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }

    const int item_line_num = GetLineNumber();
    const int item_line_pos = GetLinePosition();

    // identifier
    std::wstring label_name = scanner->GetToken()->GetIdentifier();
    NextToken();
    if(!eenum->AddItem(TreeFactory::Instance()->MakeEnumItem(file_name, item_line_num, item_line_pos, label_name, eenum))) {
      ProcessError(L"Duplicate enum label name '" + label_name + L"'", TOKEN_CLOSED_BRACE);
    }

    if(Match(TOKEN_COMMA)) {
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
    }
    else if(!Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected ',' or ')'", TOKEN_CLOSED_BRACE);
      NextToken();
    }
  }
  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  eenum->SetEndLineNumber(GetLineNumber());
  eenum->SetEndLinePosition(GetLinePosition());

  return eenum;
}

/****************************
 * Parses a function alias
 ****************************/
Alias* Parser::ParseLambdas(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring alias_name = scanner->GetToken()->GetIdentifier();
  // a bundle's alias is named with its bundle, as a class and an enum are
  // ('Bundle.Funcs'): two bundles' 'Funcs' were one name, and the first won
  const std::wstring alias_scope_name = GetEnumScopeName(alias_name, true);
  if(current_bundle->GetClass(alias_name) || current_bundle->GetEnum(alias_name) || current_bundle->GetEnum(alias_scope_name) ||
     current_bundle->GetAlias(alias_name) || current_bundle->GetAlias(alias_scope_name)) {
    ProcessError(L"Class, interface or alias name already defined in this bundle");
  }
  NextToken();

#ifdef _DEBUG
  Debug(L"[Alias: name='" + alias_scope_name + L"']", depth);
#endif

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  Alias* alias = TreeFactory::Instance()->MakeAlias(file_name, line_num, line_pos, alias_scope_name);
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    // identifier
    std::wstring label_name = scanner->GetToken()->GetIdentifier();
    NextToken();

    if(!Match(TOKEN_COLON)) {
      ProcessError(L"Expected ','", TOKEN_CLOSED_BRACE);
    }
    NextToken();

    Type* type = ParseType(depth + 1);
    if(!alias->AddType(label_name, type)) {
      ProcessError(L"Duplicate lambda label name '" + label_name + L"'", TOKEN_CLOSED_BRACE);
    }

    if(Match(TOKEN_COMMA)) {
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
    }
    else if(!Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected ',' or ')'", TOKEN_CLOSED_BRACE);
      NextToken();
    }
  }
  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  return alias;
}

/****************************
 * Parses a const (mixed value enum)
 ****************************/
Enum* Parser::ParseConsts(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring enum_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(enum_name) || current_bundle->GetEnum(enum_name) || current_bundle->GetEnum(GetEnumScopeName(enum_name, true)) || current_bundle->GetAlias(enum_name)) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();
  const std::wstring enum_scope_name = GetEnumScopeName(enum_name, true);

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  Enum* eenum = TreeFactory::Instance()->MakeEnum(file_name, line_num, line_pos, -1, -1, enum_scope_name);
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    // identifier
    std::wstring label_name = scanner->GetToken()->GetIdentifier();
    NextToken();

    if(!Match(TOKEN_ASSIGN)) {
      ProcessError(L"Expected ':='", TOKEN_CLOSED_BRACE);
    }

    NextToken();
    INT64_VALUE value = -1;
    Expression* expression = ParseTerm(depth + 1);
    if(expression) {
      switch (expression->GetExpressionType()) {
      case INT_LIT_EXPR:
        value = static_cast<IntegerLiteral*>(expression)->GetValue();
        break;

      case CHAR_LIT_EXPR:
        value = static_cast<CharacterLiteral*>(expression)->GetValue();
        break;

      case ADD_EXPR:
      case SUB_EXPR:
      case MUL_EXPR:
      case DIV_EXPR:
      case MOD_EXPR: {
        std::stack<INT64_VALUE> values;
        CalculateConst(expression, values, depth + 1);
        if(values.size() == 1) {
          value = values.top();
        }
      }
        break;

      default:
        ProcessError(L"Expected integer or character literal expression", TOKEN_CLOSED_PAREN);
        break;
      }
    }

    const int item_line_num = GetLineNumber();
    const int item_line_pos = GetLinePosition();

    if(!eenum->AddItem(TreeFactory::Instance()->MakeEnumItem(file_name, item_line_num, item_line_pos, label_name, eenum), value)) {
      ProcessError(L"Duplicate 'consts' label name '" + label_name + L"'", TOKEN_CLOSED_BRACE);
    }

    if(Match(TOKEN_COMMA)) {
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
    }
    else if(!Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected ',' or ')'", TOKEN_CLOSED_BRACE);
      NextToken();
    }
  }

  eenum->SetEndLineNumber(GetLineNumber());
  eenum->SetEndLinePosition(GetLinePosition());

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  return eenum;
}

/****************************
 * Calculates a constant expression
 ****************************/
void Parser::CalculateConst(Expression* expression, std::stack<INT64_VALUE>& values, int depth)
{
  switch (expression->GetExpressionType()) {
  case INT_LIT_EXPR:
    values.push(static_cast<IntegerLiteral*>(expression)->GetValue());
    break;

  case CHAR_LIT_EXPR:
    values.push(static_cast<CharacterLiteral*>(expression)->GetValue());
    break;

  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR: {
    CalculatedExpression* calc_expr = static_cast<CalculatedExpression*>(expression);
    if(calc_expr->GetLeft() && calc_expr->GetRight()) {
      CalculateConst(calc_expr->GetLeft(), values, depth + 1);
      CalculateConst(calc_expr->GetRight(), values, depth + 1);
    }
    
    if(values.size() > 1) {
      const INT64_VALUE right = values.top();
      values.pop();

      const INT64_VALUE left = values.top();
      values.pop();

      switch (expression->GetExpressionType()) {
      case ADD_EXPR:
        values.push(left + right);
        break;

      case SUB_EXPR:
        values.push(left - right);
        break;

      case MUL_EXPR:
        values.push(left * right);
        break;

      case DIV_EXPR:
        values.push(left / right);
        break;

      case MOD_EXPR:
        values.push(left % right);
        break;

       default:
          break;
      }
    }
    else {
      ProcessError(L"Expected integer/character literal expression", TOKEN_CLOSED_PAREN);
    }
  }
    break;

  default:
    ProcessError(L"Expected integer/character literal expression", TOKEN_CLOSED_PAREN);
    break;
  }
}

/****************************
 * `@x : get : T` / `@x : get set : T`: the getter and setter of the field,
 * written as the methods a programmer would. The name is Get + the field name in
 * PascalCase (input_tokens -> GetInputTokens); a Bool field is Is + the name, and
 * a leading is_/has_ is the prefix itself (is_error -> IsError, has_uses -> HasUses);
 * the setter is Set + the name without its is_/has_. A method of the same
 * signature written by hand takes precedence.
 ****************************/
static std::wstring AccessorBaseName(std::wstring name, std::wstring& prefix, bool is_bool)
{
  if(!name.empty() && name[0] == L'@') {
    name = name.substr(1);
  }
  prefix = is_bool ? L"Is" : L"Get";
  if(name.compare(0, 3, L"is_") == 0 && name.size() > 3) {
    name = name.substr(3);
  }
  else if(name.compare(0, 4, L"has_") == 0 && name.size() > 4) {
    name = name.substr(4);
    prefix = L"Has";
  }

  std::wstring result_name;
  bool upper = true;
  for(wchar_t c : name) {
    if(c == L'_') {
      upper = true;
    }
    else if(upper && c >= L'a' && c <= L'z') {
      result_name += (wchar_t)(c - L'a' + L'A');
      upper = false;
    }
    else {
      result_name += c;
      upper = false;
    }
  }
  return result_name;
}

void Parser::GenerateFieldAccessors(Class* klass, const std::wstring& file_name, int line_num, int line_pos, int depth)
{
  (void)depth;
  Method* previous_method = current_method;
  std::vector<Statement*> statements = klass->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    if(!statements[i] || statements[i]->GetStatementType() != DECLARATION_STMT) {
      continue;
    }

    std::vector<Declaration*> group;
    for(Declaration* declaration = static_cast<Declaration*>(statements[i]); declaration; declaration = declaration->GetChild()) {
      group.push_back(declaration);
    }

    for(size_t g = group.size(); g > 0; --g) {
      Declaration* declaration = group[g - 1];
      SymbolEntry* field_entry = declaration->GetEntry();
      if(!declaration->HasGetter() || !field_entry || !field_entry->GetType()) {
        continue;
      }

      std::wstring field_name = field_entry->GetName();
      const size_t index = field_name.find_last_of(L':');
      if(index != std::wstring::npos) {
        field_name = field_name.substr(index + 1);
      }

      const bool is_bool = field_entry->GetType()->GetType() == BOOLEAN_TYPE && field_entry->GetType()->GetDimension() == 0;
      std::wstring prefix;
      const std::wstring base = AccessorBaseName(field_name, prefix, is_bool);

      // getter
      const std::wstring getter_name = klass->GetName() + L":" + prefix + base;
      Method* getter = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, line_num, line_pos, getter_name, PUBLIC_METHOD, false, false);
      current_method = getter;
      symbol_table->NewParseScope();
      getter->SetDeclarations(TreeFactory::Instance()->MakeDeclarationList());
      StatementList* getter_body = TreeFactory::Instance()->MakeStatementList();
      getter_body->AddStatement(TreeFactory::Instance()->MakeReturn(file_name, line_num, line_pos, line_num, line_pos,
                                                                    TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, field_name)));
      getter->SetStatements(getter_body);
      getter->SetReturn(TypeFactory::Instance()->MakeType(field_entry->GetType()));
      getter->SetEndLineNumber(line_num);
      getter->SetEndLinePosition(line_pos);
      if(klass->AddMethod(getter)) {
        symbol_table->PreviousParseScope(getter->GetParsedName());
      }
      else {
        symbol_table->PreviousParseScope(getter->GetParsedName() + L" <accessor-generated>");
      }

      // setter
      if(declaration->HasSetter()) {
        std::wstring ignored;
        const std::wstring setter_name = klass->GetName() + L":Set" + AccessorBaseName(field_name, ignored, false);
        Method* setter = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, line_num, line_pos, setter_name, PUBLIC_METHOD, false, false);
        current_method = setter;
        symbol_table->NewParseScope();

        DeclarationList* declarations = TreeFactory::Instance()->MakeDeclarationList();
        SymbolEntry* param_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(L"value"),
                                                                            TypeFactory::Instance()->MakeType(field_entry->GetType()), false, true);
        symbol_table->CurrentParseScope()->AddEntry(param_entry);
        declarations->AddDeclaration(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, param_entry,
                                                                              (Declaration*)nullptr));
        setter->SetDeclarations(declarations);

        StatementList* setter_body = TreeFactory::Instance()->MakeStatementList();
        Variable* left = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, field_name);
        Variable* right = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, L"value");
        setter_body->AddStatement(TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, left, right));
        setter->SetStatements(setter_body);
        setter->SetReturn(TypeFactory::Instance()->MakeType(NIL_TYPE));
        setter->SetEndLineNumber(line_num);
        setter->SetEndLinePosition(line_pos);
        if(klass->AddMethod(setter)) {
          symbol_table->PreviousParseScope(setter->GetParsedName());
        }
        else {
          symbol_table->PreviousParseScope(setter->GetParsedName() + L" <accessor-generated>");
        }
      }
    }
  }
  current_method = previous_method;
}

/****************************
 * Parses a class.
 ****************************/
Class* Parser::ParseClass(const std::wstring &bundle_name, int depth)
{
  CheckDepth(depth);
  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const int class_line_num = line_num;
  const int class_line_pos = line_pos;
  const std::wstring file_name = GetFileName();

  bool is_public = true;
  bool is_record = Match(TOKEN_RECORD_ID);
  bool is_readonly_record = false;
  NextToken();

  if(Match(TOKEN_COLON)) {
    NextToken();
    while(!Match(TOKEN_IDENT) && !Match(TOKEN_END_OF_STREAM)) {
      if(Match(TOKEN_PUBLIC_ID)) {
        is_public = true;
        NextToken();
      }
      else if(Match(TOKEN_PRIVATE_ID)) {
        is_public = false;
        NextToken();
      }
      else if(is_record && Match(TOKEN_READONLY_ID)) {
        is_readonly_record = true;
        NextToken();
      }
      else {
        ProcessError(is_record ? L"Expected 'public', 'private' or 'readonly'" : L"Expected 'public' or 'private'");
      }

      if(!Match(TOKEN_COLON)) {
        ProcessError(TOKEN_COLON);
      }
      NextToken();
    }
  }

  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  std::wstring cls_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(cls_name) || current_bundle->GetEnum(cls_name) || current_bundle->GetAlias(cls_name) || current_bundle->GetAlias(GetEnumScopeName(cls_name, true))) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();

#ifdef _DEBUG
  Debug(L"[Class: name='" + cls_name + L"']", depth);
#endif

  // generic ids
  std::vector<Class*> generic_classes = ParseGenericClasses(bundle_name, depth);

  // 'class Name : json' or 'class Name : json(optional_field, ...)': the compiler writes the JSON Schema and the reader
  bool is_json = false;
  std::vector<std::wstring> json_optional;
  if(Match(TOKEN_COLON)) {
    NextToken();
    if(Match(TOKEN_IDENT) && scanner->GetToken()->GetIdentifier() == L"json") {
      is_json = true;
      NextToken();
      if(Match(TOKEN_OPEN_PAREN)) {
        NextToken();
        while(Match(TOKEN_IDENT)) {
          json_optional.push_back(scanner->GetToken()->GetIdentifier());
          NextToken();
          if(Match(TOKEN_COMMA)) {
            NextToken();
          }
          else {
            break;
          }
        }
        if(!Match(TOKEN_CLOSED_PAREN)) {
          ProcessError(L"Expected ')' after the optional fields of 'json(...)'", TOKEN_CLOSED_PAREN);
        }
        else {
          NextToken();
        }
      }
      if(is_record) {
        ProcessError(L"'json' belongs to a class; a record has its own constructor and is already readable from JSON");
      }
    }
    else {
      ProcessError(L"Expected 'json' after the class name, as in 'class Weather : json'");
    }
  }

  // from id
  std::wstring parent_cls_name;
  if(Match(TOKEN_FROM_ID)) {
    NextToken();
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    // identifier
    parent_cls_name = ParseBundleName();

    if(parent_cls_name == cls_name) {
      ProcessError(L"Child and parent class names are the same");
    }
  }

  // implements ids
  std::vector<std::wstring> interface_names;
  if(Match(TOKEN_IMPLEMENTS_ID)) {
    NextToken();
    while(!Match(TOKEN_OPEN_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
      // identifier
      const std::wstring &ident = ParseBundleName();
      interface_names.push_back(ident);
      if(Match(TOKEN_COMMA)) {
        NextToken();
        if(!Match(TOKEN_IDENT)) {
          ProcessError(TOKEN_IDENT);
        }
      }
      else if(!Match(TOKEN_OPEN_BRACE)) {
        ProcessError(L"Expected ',' or '{'", TOKEN_OPEN_BRACE);
        NextToken();
      }
    }
  }
  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }

  symbol_table->NewParseScope();
  NextToken();

  // perpend bundle name
  if(bundle_name.size() > 0) {
    cls_name.insert(0, L".");
    cls_name.insert(0, bundle_name);
  }

  if(current_bundle->GetClass(cls_name)) {
    ProcessError(L"Class has already been defined");
  }

  Class* klass = TreeFactory::Instance()->MakeClass(file_name, class_line_num, class_line_pos, -1, -1, cls_name, parent_cls_name,
                                                    interface_names, generic_classes, is_public, false);
  klass->SetRecord(is_record);
  klass->SetReadonlyRecord(is_readonly_record);
  if(is_json) {
    klass->SetJson(true, json_optional);
  }
  current_class = klass;

  // add '@self' entry
  Type* self_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, cls_name, file_name, line_num, line_pos);
  if(!generic_classes.empty()) {
    std::vector<Type*> generic_types;
    for(size_t i = 0; i < generic_classes.size(); ++i) {
      Class* generic_class = generic_classes[i];
      generic_types.push_back(TypeFactory::Instance()->MakeType(CLASS_TYPE, generic_class->GetName(), file_name, line_num, line_pos));
    }
    self_type->SetGenerics(generic_types);
  }
  SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(GetScopeName(SELF_ID), self_type, false, false, true);
  symbol_table->CurrentParseScope()->AddEntry(entry);

  std::vector<McpCandidate> mcp_tools;
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    // parse 'method | function | declaration'
    if(Match(TOKEN_FUNCTION_ID)) {
      // a doc comment with an '@tool' line directly before the function (nearly always none)
      const int tool_doc_id = scanner->GetToken()->GetDocId();
      const ToolDoc* tool_doc = tool_doc_id >= 0 ? scanner->GetToolDoc(tool_doc_id) : nullptr;
      McpCandidate candidate;
      candidate.method = nullptr;
      if(tool_doc) {
        candidate.doc = *tool_doc;
      }
      Method* method = ParseMethod(true, false, depth + 1);
      bool was_added = klass->AddMethod(method);
      if(!was_added) {
        ProcessError(L"Method/function already defined or overloaded '" + method->GetName() + L"'", method);
      }
      else if(tool_doc) {
        candidate.method = method;
        mcp_tools.push_back(std::move(candidate));
      }
    }
    else if(Match(TOKEN_METHOD_ID) || Match(TOKEN_NEW_ID)) {
      if(scanner->GetToken()->GetDocId() >= 0) {
        RejectToolDoc(Match(TOKEN_NEW_ID) ? L"a constructor" : L"a method (a tool is a 'function')");
      }
      Method* method = ParseMethod(false, false, depth + 1);
      bool was_added = klass->AddMethod(method);
      if(!was_added) {
        ProcessError(L"Method/function already defined or overloaded '" + method->GetName() + L"'", method);
      }
    }
    else if(Match(TOKEN_IDENT)) {
      if(scanner->GetToken()->GetDocId() >= 0) {
        RejectToolDoc(L"a variable");
      }
      const std::wstring ident = scanner->GetToken()->GetIdentifier();
      line_num = GetLineNumber();
      line_pos = GetLinePosition();
      IdentifierContext ident_context(ident, line_num, line_pos);

      NextToken();

      klass->AddStatement(ParseDeclaration(ident_context, true, depth + 1));
      if(!Match(TOKEN_SEMI_COLON)) {
        ProcessError(L"Expected ';'", TOKEN_SEMI_COLON);
      }
      NextToken();
    }
    else if(Match(TOKEN_ENUM_ID)) {
      if(scanner->GetToken()->GetDocId() >= 0) {
        RejectToolDoc(L"an enum");
      }
      current_bundle->AddEnum(ParseEnum(depth + 1));
    }
    else if(Match(TOKEN_CONSTS_ID)) {
      current_bundle->AddEnum(ParseConsts(depth + 1));
    }
    else {
      ProcessError(L"Expected declaration", TOKEN_SEMI_COLON);
      NextToken();
    }
  }

  klass->SetEndLineNumber(GetLineNumber());
  klass->SetEndLinePosition(GetLinePosition());

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  if(klass->IsRecord()) {
    GenerateRecordMethods(klass, file_name, class_line_num, class_line_pos, depth + 1);
  }

  GenerateFieldAccessors(klass, file_name, class_line_num, class_line_pos, depth + 1);

  // The tools, and the members of a 'class : json', are derived once every file has been read
  // (GeneratePendingMcpTools): a parameter's or a field's record, enum or json class may be declared
  // below this class, or in a later file.
  if(!mcp_tools.empty() || is_json) {
    McpPending pending;
    pending.klass = klass;
    pending.bundle = current_bundle;
    pending.tools = mcp_tools;
    pending.uses_mcp = current_lib_uses && std::find(current_lib_uses->begin(), current_lib_uses->end(), std::wstring(L"API.MCP")) != current_lib_uses->end();
    pending.is_json = is_json;
    pending.json_line = class_line_num;
    if(current_lib_uses) {
      pending.lib_uses = *current_lib_uses;
    }
    mcp_pending.push_back(pending);
  }

  symbol_table->PreviousParseScope(current_class->GetName());
  current_class = nullptr;

  return klass;
}

void Parser::GenerateRecordMethods(Class* klass, const std::wstring& file_name, int line_num, int line_pos, int depth)
{
  std::vector<Declaration*> fields;
  std::vector<Statement*> statements = klass->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    if(statements[i] && statements[i]->GetStatementType() == DECLARATION_STMT) {
      // comma declaration lists chain names last-to-first; walk the chain and
      // reverse the group so generated constructor parameters follow source
      // order. Static fields are class-level state, not per-instance record
      // components, so they get no constructor parameter or accessors.
      std::vector<Declaration*> group;
      Declaration* declaration = static_cast<Declaration*>(statements[i]);
      while(declaration) {
        SymbolEntry* entry = declaration->GetEntry();
        if(entry && !entry->IsStatic()) {
          group.push_back(declaration);
        }
        declaration = declaration->GetChild();
      }
      fields.insert(fields.end(), group.rbegin(), group.rend());
    }
  }

  Method* previous_method = current_method;

  if(!klass->HasDefaultNew()) {
    const std::wstring method_name = klass->GetName() + L":New";
    Method* method = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, line_num, line_pos, method_name, NEW_PUBLIC_METHOD, false, false);
    current_method = method;
    symbol_table->NewParseScope();

    DeclarationList* declarations = TreeFactory::Instance()->MakeDeclarationList();
    StatementList* body = TreeFactory::Instance()->MakeStatementList();

    if(!klass->GetParentName().empty()) {
      body->AddStatement(TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos, PARENT_CALL, L"",
                                                                 TreeFactory::Instance()->MakeExpressionList()));
    }

    for(size_t i = 0; i < fields.size(); ++i) {
      SymbolEntry* field_entry = fields[i]->GetEntry();
      if(field_entry && field_entry->GetType()) {
        std::wstring field_name = field_entry->GetName();
        const size_t index = field_name.find_last_of(L':');
        if(index != std::wstring::npos) {
          field_name = field_name.substr(index + 1);
        }

        std::wstring param_name = field_name;
        if(param_name.size() > 0 && param_name[0] == L'@') {
          param_name = param_name.substr(1);
        }

        Type* param_type = TypeFactory::Instance()->MakeType(field_entry->GetType());
        SymbolEntry* param_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(param_name),
                                                                            param_type, false, true);
        symbol_table->CurrentParseScope()->AddEntry(param_entry);
        declarations->AddDeclaration(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, param_entry,
                                                                              (Declaration*)nullptr));

        Variable* left = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, field_name);
        Variable* right = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, param_name);
        body->AddStatement(TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, left, right));
      }
    }

    method->SetDeclarations(declarations);
    method->SetStatements(body);
    method->SetReturn(TypeFactory::Instance()->MakeType(CLASS_TYPE, klass->GetName(), file_name, line_num, line_pos));
    method->SetEndLineNumber(line_num);
    method->SetEndLinePosition(line_pos);

    // a user-defined constructor with the same signature takes precedence
    // over the generated one; pop the discarded scope under a unique key so
    // the parse-scope stack stays balanced (PreviousParseScope early-returns
    // without popping when the name is already registered)
    if(klass->AddMethod(method)) {
      symbol_table->PreviousParseScope(method->GetParsedName());
    }
    else {
      symbol_table->PreviousParseScope(method->GetParsedName() + L" <record-generated>");
    }
  }

  for(size_t i = 0; i < fields.size(); ++i) {
    SymbolEntry* field_entry = fields[i]->GetEntry();
    if(field_entry && field_entry->GetType()) {
      std::wstring field_name = field_entry->GetName();
      const size_t index = field_name.find_last_of(L':');
      if(index != std::wstring::npos) {
        field_name = field_name.substr(index + 1);
      }

      std::wstring property_name = field_name;
      if(property_name.size() > 0 && property_name[0] == L'@') {
        property_name = property_name.substr(1);
      }
      if(property_name.size() > 0 && property_name[0] >= L'a' && property_name[0] <= L'z') {
        property_name[0] = property_name[0] - L'a' + L'A';
      }

      const std::wstring getter_name = klass->GetName() + L":Get" + property_name;
      Method* getter = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, line_num, line_pos, getter_name, PUBLIC_METHOD, false, false);
      current_method = getter;
      symbol_table->NewParseScope();
      getter->SetDeclarations(TreeFactory::Instance()->MakeDeclarationList());
      StatementList* getter_body = TreeFactory::Instance()->MakeStatementList();
      getter_body->AddStatement(TreeFactory::Instance()->MakeReturn(file_name, line_num, line_pos, line_num, line_pos,
                                                                    TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, field_name)));
      getter->SetStatements(getter_body);
      getter->SetReturn(TypeFactory::Instance()->MakeType(field_entry->GetType()));
      getter->SetEndLineNumber(line_num);
      getter->SetEndLinePosition(line_pos);
      // a user-defined getter with the same signature takes precedence
      if(klass->AddMethod(getter)) {
        symbol_table->PreviousParseScope(getter->GetParsedName());
      }
      else {
        symbol_table->PreviousParseScope(getter->GetParsedName() + L" <record-generated>");
      }

      if(!klass->IsReadonlyRecord()) {
        const std::wstring setter_name = klass->GetName() + L":Set" + property_name;
        Method* setter = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, line_num, line_pos, setter_name, PUBLIC_METHOD, false, false);
        current_method = setter;
        symbol_table->NewParseScope();

        DeclarationList* setter_declarations = TreeFactory::Instance()->MakeDeclarationList();
        std::wstring param_name = field_name;
        if(param_name.size() > 0 && param_name[0] == L'@') {
          param_name = param_name.substr(1);
        }
        Type* param_type = TypeFactory::Instance()->MakeType(field_entry->GetType());
        SymbolEntry* param_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(param_name),
                                                                            param_type, false, true);
        symbol_table->CurrentParseScope()->AddEntry(param_entry);
        setter_declarations->AddDeclaration(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, param_entry,
                                                                                    (Declaration*)nullptr));

        StatementList* setter_body = TreeFactory::Instance()->MakeStatementList();
        setter_body->AddStatement(TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos,
                                                                         TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, field_name),
                                                                         TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, param_name)));
        setter->SetDeclarations(setter_declarations);
        setter->SetStatements(setter_body);
        setter->SetReturn(TypeFactory::Instance()->MakeType(NIL_TYPE, file_name, line_num, line_pos));
        setter->SetEndLineNumber(line_num);
        setter->SetEndLinePosition(line_pos);
        // a user-defined setter with the same signature takes precedence
        if(klass->AddMethod(setter)) {
          symbol_table->PreviousParseScope(setter->GetParsedName());
        }
        else {
          symbol_table->PreviousParseScope(setter->GetParsedName() + L" <record-generated>");
        }
      }
    }
  }

  current_method = previous_method;
}

/****************************
 * Parses an interface
 ****************************/
Class* Parser::ParseInterface(const std::wstring &bundle_name, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  std::wstring cls_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(cls_name) || current_bundle->GetEnum(cls_name) || current_bundle->GetAlias(cls_name) || current_bundle->GetAlias(GetEnumScopeName(cls_name, true))) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();

#ifdef _DEBUG
  Debug(L"[Interface: name='" + cls_name + L"']", depth);
#endif

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  symbol_table->NewParseScope();
  NextToken();

  if(bundle_name.size() > 0) {
    cls_name.insert(0, L".");
    cls_name.insert(0, bundle_name);
  }

  if(current_bundle->GetClass(cls_name)) {
    ProcessError(L"Class has already been defined");
  }

  Class* klass = TreeFactory::Instance()->MakeClass(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), cls_name, false);
  current_class = klass;

  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    // parse 'method | function | declaration'
    if(Match(TOKEN_FUNCTION_ID)) {
      Method* method = ParseMethod(true, true, depth + 1);
      bool was_added = klass->AddMethod(method);
      if(!was_added) {
        ProcessError(L"Method/function already defined or overloaded '" + method->GetName() + L"'", method);
      }
    }
    else if(Match(TOKEN_METHOD_ID)) {
      Method* method = ParseMethod(false, true, depth + 1);
      bool was_added = klass->AddMethod(method);
      if(!was_added) {
        ProcessError(L"Method/function already defined or overloaded '" + method->GetName() + L"'", method);
      }
    }
    else {
      ProcessError(L"Expected declaration", TOKEN_SEMI_COLON);
      NextToken();
    }
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  symbol_table->PreviousParseScope(current_class->GetName());
  current_class = nullptr;
  return klass;
}

/****************************
 * True when a string literal the scanner rejected holds a lambda `\(` inside a
 * `{$ ... }` interpolation, so the error can name the real problem instead of
 * reporting a bad escape
 ****************************/
static bool HasLambdaInInterpolation(const std::wstring& str)
{
  bool in_interpolation = false;
  for(size_t i = 0; i + 1 < str.size(); ++i) {
    if(str[i] == L'{' && str[i + 1] == L'$') {
      in_interpolation = true;
    }
    else if(str[i] == L'}') {
      in_interpolation = false;
    }
    else if(in_interpolation && str[i] == L'\\' && str[i + 1] == L'(') {
      return true;
    }
  }

  return false;
}

/****************************
 * True when a string literal ends inside a `{$ ... }` interpolation that is
 * never closed. The scanner ends a string at the first unescaped quote, so a
 * quoted argument inside an interpolation -- "x: {$(Join(items, ", "))}" --
 * cuts the string at its first quote and leaves the '{$' open. Reporting that
 * names the real mistake; the parse otherwise fails tokens later on whatever
 * the rest of the line reads as.
 ****************************/
static bool HasUnclosedInterpolation(const std::wstring& str)
{
  bool in_interpolation = false;
  for(size_t i = 0; i < str.size(); ++i) {
    if(str[i] == L'\\') {
      // an escaped character, such as \{ or \", opens and closes nothing
      ++i;
    }
    else if(i + 1 < str.size() && str[i] == L'{' && str[i + 1] == L'$') {
      in_interpolation = true;
      ++i;
    }
    else if(str[i] == L'}') {
      in_interpolation = false;
    }
  }

  return in_interpolation;
}

/****************************
 * Parses lambda expression
 ****************************/
Lambda* Parser::ParseLambda(int depth, Type* block_type) {
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  // build method
  const std::wstring lambda_name = L"#{L" + ToString(current_class->NextLambda()) + L"}#";
  const std::wstring method_name = current_class->GetName() + L':' + lambda_name;
  Method* method = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, -1, -1, method_name);
  
  // declarations
  Method* outter_method = current_method;
  current_method = method;
  symbol_table->NewParseScope();

  // parse derived, alias, or types
  Type* type = block_type;
  std::wstring alias_name;
  // a block that is given its type (a 'spawn' body): no signature, no
  // parameters and no '=>', only the '{ ... }' that follows
  if(block_type) {
    method->SetSpawnBody(true);
    if(!Match(TOKEN_OPEN_BRACE)) {
      ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
    }
  }
  else if(Match(TOKEN_OPEN_PAREN)) {
    // Disambiguate the explicit-type form `\(<sig>) ~ R : (<params>) => body`
    // from the bare form `\(<params>) => body` (type inferred from context).
    // Both start with `(...)`; in the typed form the matching `)` is followed
    // by `~` (the function type's return marker), in the bare form it is not.
    // The matching `)` can be any distance away. The scanner only buffers
    // LOOK_AHEAD tokens (GetToken() past that is nullptr, which crashed obc,
    // and a fixed wider window misread long bare parameter lists as typed),
    // so the scanner peeks past its buffer and restores itself.
    if(scanner->PeekAfterParens(1) == TOKEN_TILDE) {
      // explicit function-type signature
      type = ParseType(depth + 1);

      if(!Match(TOKEN_COLON)) {
        ProcessError(TOKEN_COLON);
      }
      NextToken();
    }
    // else: bare lambda -- the `(...)` is the parameter list, type is inferred
    // from context; fall through to parameter parsing below with type == nullptr
  }
  else if(Match(TOKEN_HAT)) {
    NextToken();
  }
  else {
    alias_name = ParseBundleName();
    if(Match(TOKEN_ASSESSOR)) {
      NextToken();
      alias_name += L"#";
      alias_name += ParseBundleName();
    }

    if(!Match(TOKEN_COLON)) {
      ProcessError(TOKEN_COLON);
    }
    NextToken();
  }

  int end_pos = 0;
  ExpressionList* parameter_list = block_type ? TreeFactory::Instance()->MakeExpressionList() : ParseExpressionList(end_pos, depth + 1);

  std::vector<Expression*> parameters = parameter_list->GetExpressions();
  DeclarationList* declaration_list = TreeFactory::Instance()->MakeDeclarationList();
  for(size_t i = 0; i < parameters.size(); ++i) {
    Expression* expression = parameters[i];
    if(expression && expression->GetExpressionType() == VAR_EXPR) {
      Variable* var_expr = static_cast<Variable*>(expression);
      const std::wstring ident = var_expr->GetName();
      const int line_num = var_expr->GetLineNumber();
      const int line_pos = var_expr->GetLinePosition();

      IdentifierContext ident_context(ident, line_num, line_pos);
      declaration_list->AddDeclaration(AddDeclaration(ident_context, TypeFactory::Instance()->MakeType(VAR_TYPE), false, nullptr,
                                                      expression->GetLineNumber(), expression->GetLinePosition(), depth));
    }
    else {
      ProcessError(L"Expected variable parameter type" , TOKEN_SEMI_COLON);
    }
  }
  method->SetDeclarations(declaration_list);

  // return type
  if(!block_type) {
    if(!Match(TOKEN_LAMBDA)) {
      ProcessError(L"Expected '=>'", TOKEN_SEMI_COLON);
    }
    NextToken();
  }

  // parse body
  StatementList* statements;
  // block body: `\(...) => { stmt; ...; return expr; }` -- arbitrary statements,
  // the caller supplies the return (previously restricted to a single if/select)
  if(Match(TOKEN_OPEN_BRACE)) {
    statements = ParseStatementList(depth + 1);
  }
  // expression body: `\(...) => expr` -- sugar for `{ return expr; }`
  else {
    statements = TreeFactory::Instance()->MakeStatementList();
    Expression* expression = ParseExpression(depth + 1);
    Statement* rtrn_stmt = TreeFactory::Instance()->MakeReturn(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression);
    statements->AddStatement(rtrn_stmt);
  }
  method->SetStatements(statements);

  method->SetEndLineNumber(GetLineNumber());
  method->SetEndLinePosition(GetLinePosition());

  symbol_table->PreviousParseScope(method->GetParsedName());
  current_method = outter_method;
  
  return TreeFactory::Instance()->MakeLambda(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), type, alias_name, method, parameter_list);
}

// A McpMain(args : String[]) that 'obr --mcp' could not start (a method, a virtual one, one that
// returns a value) is an error in a program that is an MCP server, and an ordinary method in any
// other program: 'method : public : McpMain(args : String[]) ~ Int' in an application that has
// nothing to do with MCP used to be refused for its name. A program is a server when 'obr --mcp'
// has something to start in it:
//   * the file that declares the method has 'use API.MCP' (a server written by hand registers its
//     tools and calls McpServe->Run from its McpMain): reported here, where the method is; or
//   * some class of the program has an '@tool', '@resource' or '@prompt' function (the compiler
//     writes an entry for it, and a McpMain the program wrote is started instead of that one):
//     known when every file has been read, so the error is kept until then (ReportMcpEntries).
PARSER_NO_INLINE void Parser::RefuseMcpEntry(const std::wstring& file_name, int line_num, int line_pos, const std::wstring& message)
{
  if(current_lib_uses && std::find(current_lib_uses->begin(), current_lib_uses->end(), std::wstring(L"API.MCP")) != current_lib_uses->end()) {
    diagnostics.Error(file_name, line_num, line_pos, message);
    return;
  }

  McpEntryRefusal refusal;
  refusal.file_name = file_name;
  refusal.line = line_num;
  refusal.pos = line_pos;
  refusal.message = message;
  mcp_entry_refusals.push_back(refusal);
}

// Every file has been read: the errors RefuseMcpEntry kept are reported when the program has a
// tagged function, and dropped when it has none. Called before the tools are derived, which
// empties the list of classes that have some.
void Parser::ReportMcpEntries()
{
  bool tagged = false;
  for(size_t i = 0; i < mcp_pending.size() && !tagged; ++i) {
    tagged = !mcp_pending[i].tools.empty();
  }
  if(tagged) {
    for(size_t i = 0; i < mcp_entry_refusals.size(); ++i) {
      diagnostics.Error(mcp_entry_refusals[i].file_name, mcp_entry_refusals[i].line, mcp_entry_refusals[i].pos, mcp_entry_refusals[i].message);
    }
  }
  mcp_entry_refusals.clear();
}

// True for a method named McpMain whose one parameter is a String[]: the name and
// signature 'obr --mcp' looks for (Loader::SelectMcpEntry reads them from the method's
// encoded name, which carries neither its kind nor its return type). An overload named
// McpMain with any other parameter list is an ordinary method.
static bool HasMcpEntrySignature(Method* method)
{
  const std::wstring name = method->GetName();
  const size_t colon = name.find_last_of(L':');
  if((colon == std::wstring::npos ? name : name.substr(colon + 1)) != L"McpMain" || !method->GetDeclarations()) {
    return false;
  }

  Type* only = nullptr;
  int count = 0;
  std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  for(size_t i = 0; i < declarations.size(); ++i) {
    // 'a, b : T' is one declaration with children
    for(Declaration* declaration = declarations[i]; declaration; declaration = declaration->GetChild()) {
      ++count;
      only = declaration->GetEntry() ? declaration->GetEntry()->GetType() : nullptr;
    }
  }

  return count == 1 && only && only->GetType() == CLASS_TYPE && only->GetDimension() == 1 &&
         (only->GetName() == L"String" || only->GetName() == L"System.String");
}

/****************************
 * Parses a method.
 ****************************/
Method* Parser::ParseMethod(bool is_function, bool virtual_requried, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  int mid_line_num = -1;
  int mid_line_pos = -1;

  MethodType method_type = is_function ? PUBLIC_METHOD : PRIVATE_METHOD;
  std::wstring method_name;
  bool is_native = false;
  bool is_virtual = false;
  if(Match(TOKEN_NEW_ID)) {
    NextToken();

    if(Match(TOKEN_COLON)) {
      NextToken();
      if(!Match(TOKEN_PRIVATE_ID)) {
        ProcessError(L"Expected 'private'", TOKEN_SEMI_COLON);
      }
      NextToken();
      method_type = NEW_PRIVATE_METHOD;
    }
    else {
      method_type = NEW_PUBLIC_METHOD;
    }

    method_name = current_class->GetName() + L":New";
  }
  else {
    NextToken();

    if(!Match(TOKEN_COLON)) {
      ProcessError(L"Expected ':'", TOKEN_COLON);
    }
    NextToken();
    
    // detect method attributes
    // key:
    // 0: error state
    // 1: public
    // 2: private
    // 3: virtual
    // 4: native
    const size_t method_attribs_max = 5;
    bool method_attribs[method_attribs_max];
    memset(method_attribs, 0, sizeof(bool) * method_attribs_max);

    while(!Match(TOKEN_IDENT) && !method_attribs[0] && !Match(TOKEN_END_OF_STREAM)) {
      switch(GetToken()) {
      case TOKEN_PUBLIC_ID:
        if(method_attribs[1]) {
          ProcessError(L"Method/function attribute 'public' already specified", TOKEN_COLON);
          method_attribs[0] = true;
        }
        else {
          method_attribs[1] = true;
        }
        NextToken();

        if(!Match(TOKEN_COLON)) {
          ProcessError(L"Expected ':'", TOKEN_COLON);
          method_attribs[0] = true;
        }
        NextToken();
        break;

      case TOKEN_PRIVATE_ID:
        if(method_attribs[2]) {
          ProcessError(L"Method/function attribute 'private' already specified", TOKEN_COLON);
          method_attribs[0] = true;
        }
        else {
          method_attribs[2] = true;
        }
        NextToken();

        if(!Match(TOKEN_COLON)) {
          ProcessError(L"Expected ':'", TOKEN_COLON);
          method_attribs[0] = true;
        }
        NextToken();
        break;

      case TOKEN_VIRTUAL_ID:
        if(method_attribs[3]) {
          ProcessError(L"Method/function attribute 'virtual' already specified", TOKEN_COLON);
          method_attribs[0] = true;
        }
        else {
          method_attribs[3] = true;
        }
        NextToken();

        if(!Match(TOKEN_COLON)) {
          ProcessError(L"Expected ':'", TOKEN_COLON);
          method_attribs[0] = true;
        }
        NextToken();
        break;

      case TOKEN_NATIVE_ID:
        if(method_attribs[4]) {
          ProcessError(L"Method/function attribute 'native' already specified", TOKEN_COLON);
          method_attribs[0] = true;
        }
        else {
          method_attribs[4] = true;
        }
        NextToken();

        if(!Match(TOKEN_COLON)) {
          ProcessError(L"Expected ':'", TOKEN_COLON);
          method_attribs[0] = true;
        }
        NextToken();
        break;

      default:
        ProcessError(L"Expected method/function attribute", TOKEN_COLON);
        method_attribs[0] = true;
        break;
      }
    }

    // check for public and private
    if(method_attribs[1] && method_attribs[2]) {
      ProcessError(L"Method/function cannot be 'public' and 'private'");
    }

    // set method attributes
    //   key:
    //   1: public
    //   2: private
    //   3: virtual
    //   4: native
    for(size_t i = 1; i < method_attribs_max; ++i) {
      switch(i) {
        // 1: public
      case 1:
        if(method_attribs[1]) {
          method_type = PUBLIC_METHOD;
        }
        break;

        // 2: private
      case 2: 
        if(method_attribs[2]) {
          method_type = PRIVATE_METHOD;
        }
        break;

        // 3: virtual
      case 3:
        if(method_attribs[3]) {
          is_virtual = true;
          current_class->SetVirtual(true);
        }
        break;

        // 4: native
      case 4:
        if(method_attribs[4]) {
          is_native = true;
        }
        break;
        
        // should never happen
      default:
        break;
      }
    }
    
    // identifier
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    std::wstring ident = scanner->GetToken()->GetIdentifier();
    mid_line_num = GetLineNumber();
    mid_line_pos = GetLinePosition();
    method_name = current_class->GetName() + L':' + ident;
    NextToken();

    // 'Max<T : Compare>(...)': the method's own type parameters (#1242). Each is
    // erased to its first bound, or System.Base, as the method is parsed.
    method_generic_bounds.clear();
    if(Match(TOKEN_LES)) {
      std::vector<Class*> generic_params = ParseGenericClasses(L"", depth + 1);
      for(size_t i = 0; i < generic_params.size(); ++i) {
        Class* generic_param = generic_params[i];
        Type* bound = generic_param->GetGenericInterface();
        method_generic_bounds[generic_param->GetName()] = bound ? bound->GetName() : L"System.Base";
      }
    }
  }

#ifdef _DEBUG
  Debug(L"(Method/Function/New: name='" + method_name + L"')", depth);
#endif

  Method* method = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, mid_line_num, mid_line_pos, method_name, method_type, is_function, is_native);
  for(std::map<std::wstring, std::wstring>::iterator generic = method_generic_bounds.begin(); generic != method_generic_bounds.end(); ++generic) {
    method->AddGenericName(generic->first);
  }
  current_method = method;

  // declarations
  symbol_table->NewParseScope();
  promoted_fields.clear();
  method->SetDeclarations(ParseDecelerationList(depth + 1));
  const std::vector<std::wstring> promoted = promoted_fields;
  promoted_fields.clear();

  // return type
  Type* return_type;
  if(method_type != NEW_PUBLIC_METHOD && method_type != NEW_PRIVATE_METHOD) {
    if(!Match(TOKEN_TILDE)) {
      ProcessError(L"Expected '~'", TOKEN_TILDE);
    }
    NextToken();
    return_type = ParseType(depth + 1);
  }
  else {
    return_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, current_class->GetName(), file_name, line_num, line_pos);
  }
  method->SetReturn(return_type);

  // McpMain(args : String[]) is what 'obr --mcp' starts in place of Main, and the VM picks it
  // by its name and parameter alone: a program file does not record whether a method is a
  // function or what it returns. So in a program that 'obr --mcp' can serve (RefuseMcpEntry)
  // the one shape it can call is the only one allowed to have that signature. An instance
  // method would be started with no instance, a virtual one has no body and one that returns
  // a value leaves it on the stack.
  if(HasMcpEntrySignature(method)) {
    const wchar_t* why = nullptr;
    if(!is_function) {
      why = L"it is declared as a method, which needs an instance";
    }
    else if(is_virtual) {
      why = L"it is virtual, so it has no body";
    }
    else if(!return_type || return_type->GetType() != NIL_TYPE || return_type->GetDimension() > 0) {
      why = L"it returns a value";
    }
    if(why) {
      RefuseMcpEntry(file_name, line_num, line_pos, std::wstring(L"'McpMain(args : String[])' is the function 'obr --mcp' starts in place of Main, and must be declared 'function : McpMain(args : String[]) ~ Nil'; here ") +
                        why + L". Declare it that way or give it another name");
    }
  }

  // statements
  if(is_virtual) {
    method->SetStatements(nullptr);
    // virtual function/method ending
    if(!Match(TOKEN_SEMI_COLON)) {
      ProcessError(L"Expected ';'", TOKEN_SEMI_COLON);
    }
    NextToken();
  }
  else if(virtual_requried && !is_virtual) {
    ProcessError(L"Method/function must be declared as virtual", TOKEN_SEMI_COLON);
  }
  else {
    method->SetStatements(ParseStatementList(depth + 1));
    if(!promoted.empty() && method->GetStatements()) {
      // after a leading Parent(...) call, ahead of the written statements
      size_t at = 0;
      std::vector<Statement*> written = method->GetStatements()->GetStatements();
      // a first statement that did not parse is null: its syntax error has been reported
      if(!written.empty() && written[0] && written[0]->GetStatementType() == METHOD_CALL_STMT &&
         static_cast<MethodCall*>(written[0])->GetCallType() == PARENT_CALL) {
        at = 1;
      }
      for(size_t i = 0; i < promoted.size(); ++i) {
        Variable* left = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, L"@" + promoted[i]);
        Variable* right = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, promoted[i]);
        method->GetStatements()->InsertStatement(TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, left, right), at + i);
      }
    }
  }

  method->SetEndLineNumber(GetLineNumber());
  method->SetEndLinePosition(GetLinePosition());

  symbol_table->PreviousParseScope(method->GetParsedName());
  current_method = nullptr;
  method_generic_bounds.clear();

  return method;
}

/****************************
 * Parses a statement std::list.
 ****************************/
StatementList* Parser::ParseStatementList(int depth)
{
  CheckDepth(depth);
#ifdef _DEBUG
  Debug(L"Statement List", depth);
#endif

  // statement list
  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  // The statement this block belongs to -- an 'if' whose condition holds a call
  // -- may have hidden statements of its own waiting. They go around that
  // statement, so they are set aside: the block's first statement would take
  // them otherwise, and a temporary would be declared inside the block it is
  // read before (#1243).
  std::vector<Statement*> outer_pending = pending_statements;
  std::vector<Statement*> outer_trailing = trailing_statements;
  pending_statements.clear();
  trailing_statements.clear();

  StatementList* statements = TreeFactory::Instance()->MakeStatementList();
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    const size_t consumed = tokens_consumed;
    Statement* statement = ParseStatement(depth + 1);
    if(nil_guard_bail) {
      ProcessError(L"'?? return' guards a variable the statement assigns: 'x := E ?? return V;'");
      nil_guard_bail = nullptr;
    }
    TakePendingStatements(statements);
    statements->AddStatement(statement);
    TakeTrailingStatements(statements, statement);
    CheckStatementSeparator(statement);

    // A statement that consumed no token would be parsed again from the same
    // token on the next pass, and again, allocating nodes each time: obc ran
    // until it was killed, near 16 GB, on '"x: {$(Join(items, ", "))}"', where
    // the quote inside the interpolation ends the string early. Report the
    // token if nothing has yet, then step over it so the list moves on.
    if(tokens_consumed == consumed) {
      if(!diagnostics.HasErrors()) {
        ProcessError(L"Unexpected token in statement");
      }
      NextToken();
    }
  }
  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  pending_statements = outer_pending;
  trailing_statements = outer_trailing;

  return statements;
}

/****************************
 * Statements that ParseStatement and ParseIf hand to a function of their own,
 * and that must stay there (PARSER_NO_INLINE). Those two are on every path of
 * a nested block and of an 'else if' ladder, and the parser stops when its
 * stack passes STACK_BUDGET: a local of a rarely taken branch (a name, a
 * message being put together) is paid for at every level. With the branches
 * below written in place, the longest 'else if' ladder obc reads fell from
 * 1,800 rungs to 899 and the deepest block nesting from 395 to 332.
 ****************************/

// 'x ?? { ...; return };': leave when x is Nil
PARSER_NO_INLINE Statement* Parser::ParseNilGuardStatement(int line_num, int line_pos, int depth)
{
  const std::wstring guard_name = scanner->GetToken()->GetIdentifier();
  IdentifierContext guard_context(guard_name, line_num, line_pos);
  NextToken();
  Variable* guard_target = ParseVariable(guard_context, depth + 1);
  NextToken();
  StatementList* guard_bail = ParseNilGuardBail(depth + 1);
  return MakeNilGuard(guard_target, guard_bail);
}

// 'match subject { ... }' as a statement
PARSER_NO_INLINE Statement* Parser::ParseMatchStatement(int line_num, int line_pos, int depth)
{
  const std::wstring file_name = GetFileName();
  std::wstring value_ident;
  Select* select = ParseMatch(true, value_ident, depth + 1);
  if(!select->IsValue()) {
    return select;
  }

  // a value with a call on it, as any expression statement is
  Expression* expression = MakeValueSelect(select, value_ident, line_num, line_pos);
  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) && !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
    ParseMethodCall(expression, depth + 1);
  }
  else {
    ParseCastTypeOf(expression, depth + 1);
  }
  return TreeFactory::Instance()->MakeSimpleStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression);
}

// 'if(value is Type name)': whether 'is' follows the tested value
PARSER_NO_INLINE bool Parser::IsTypeTestStart()
{
  return Match(TOKEN_IDENT) && scanner->GetToken()->GetIdentifier() == L"is";
}

// the bound name is declared in the scope of the 'if' block; false when the block's scope has the name
PARSER_NO_INLINE bool Parser::DeclareTypeTestName(SymbolEntry* bind_entry, Assignment* bind_assign)
{
  if(!symbol_table->CurrentParseScope()->AddEntry(bind_entry)) {
    ProcessError(L"Variable already defined in this scope: '" + bind_assign->GetVariable()->GetName() + L"'");
    return false;
  }
  return true;
}

// 'name := value->As(Type)' is the block's first statement
PARSER_NO_INLINE void Parser::PrependTypeTestBinding(StatementList* if_statements, SymbolEntry* bind_entry, Assignment* bind_assign)
{
  if_statements->PrependStatement(TreeFactory::Instance()->MakeDeclaration(GetFileName(), bind_assign->GetLineNumber(), bind_assign->GetLinePosition(),
                                                                           bind_assign->GetLineNumber(), bind_assign->GetLinePosition(),
                                                                           bind_entry, bind_assign));
}

/****************************
 * Parses a statement.
 ****************************/
Statement* Parser::ParseStatement(int depth, bool semi_colon)
{
  CheckDepth(depth);
  ChainScope chain_scope(chain_operators);
#ifdef _DEBUG
  Debug(L"Statement", depth);
#endif

  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  Statement* statement = nullptr;
  is_semi_colon = false;

  // 'try {' (#1247): a contextual keyword, so 'try' stays an ordinary name
  if(Match(TOKEN_IDENT) && Match(TOKEN_OPEN_BRACE, 1) && scanner->GetToken()->GetIdentifier() == L"try") {
    statement = ParseTry(depth + 1);
  }
  // 'x ?? { ...; return };': leave when x is Nil
  else if(Match(TOKEN_IDENT) && Match(TOKEN_QUESTION_QUESTION, SECOND_INDEX) && Match(TOKEN_OPEN_BRACE, THIRD_INDEX)) {
    statement = ParseNilGuardStatement(line_num, line_pos, depth);
  }
  // 'assert E : "label";': a contextual keyword too, taken only where a name could not follow
  else if(Match(TOKEN_IDENT) && IsAssertStatement()) {
    statement = ParseAssert(depth + 1);
  }
  // 'using(w := E) {': a contextual keyword; a '(' ... ')' closed by '{' cannot be a call
  else if(Match(TOKEN_IDENT) && Match(TOKEN_OPEN_PAREN, 1) && scanner->GetToken()->GetIdentifier() == L"using" &&
          scanner->PeekAfterParens(2) == TOKEN_OPEN_BRACE) {
    statement = ParseUsing(depth + 1);
  }
  // 'json{...}->ToString()->PrintLine();': a JSON literal with a call on it
  else if(IsJsonLiteralStart()) {
    statement = TreeFactory::Instance()->MakeSimpleStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), ParseSimpleExpression(depth + 1));
  }
  // 'match subject {': a contextual keyword as well (see IsMatchStart)
  else if(IsMatchStart()) {
    statement = ParseMatchStatement(line_num, line_pos, depth);
  }
  // 'spawn {': a contextual keyword, so 'spawn' stays an ordinary name
  else if(IsSpawn()) {
    statement = ParseSpawn(depth + 1);
  }
  // a class named in an 'agent' declaration's tools, at the start of the statement written for it
  else if(IsAgentSplice()) {
    statement = ParseAgentName(depth);
  }
  // identifier
  else if(Match(TOKEN_IDENT) && !IsPromptLiteral()) {
    const std::wstring ident = ParseBundleName();
    statement = ParseIdentifierStatement(ident, line_num, line_pos, file_name, depth);
  }
  // 'Int->Fill(a, 0);': a function of a basic type's class called as a
  // statement, as 'Runtime->Copy(..);' is. It used to fall through to the
  // expression path, which rejected an unchained call as "Invalid statement".
  else if(IsBasicType(GetToken()) && Match(TOKEN_ASSESSOR, SECOND_INDEX) && Match(TOKEN_IDENT, THIRD_INDEX)) {
    std::wstring ident;
    switch(GetToken()) {
    case TOKEN_BOOLEAN_ID:
      ident = BOOL_CLASS_ID;
      break;

    case TOKEN_BYTE_ID:
      ident = BYTE_CLASS_ID;
      break;

    case TOKEN_INT_ID:
      ident = INT_CLASS_ID;
      break;

    case TOKEN_FLOAT_ID:
      ident = FLOAT_CLASS_ID;
      break;

    default:
      ident = CHAR_CLASS_ID;
      break;
    }
    NextToken();
    statement = ParseIdentifierStatement(ident, line_num, line_pos, file_name, depth);
  }
  // other
  else {
    switch(GetToken()) {
    case TOKEN_ADD_ADD:
    case TOKEN_SUB_SUB:
      statement = ParsePrefixOperatorStatement(line_num, line_pos, file_name, depth);
      break;

    case TOKEN_SEMI_COLON:
    case TOKEN_COMMA:
    case TOKEN_PARENT_ID:
    case TOKEN_RETURN_ID:
    case TOKEN_LEAVING_ID:
    case TOKEN_IF_ID:
    case TOKEN_DO_ID:
    case TOKEN_WHILE_ID:
    case TOKEN_BREAK_ID:
    case TOKEN_CONTINUE_ID:
    case TOKEN_SELECT_ID:
    case TOKEN_FOR_ID:
    case TOKEN_EACH_ID:
    case TOKEN_REVERSE_ID:
    case TOKEN_CRITICAL_ID:
      statement = ParseControlFlowStatement(line_num, line_pos, file_name, depth);
      break;

#ifdef _SYSTEM
    case ASYNC_MTHD_CALL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ASYNC_MTHD_CALL);
      NextToken();
      break;

    case EXT_LIB_LOAD:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::EXT_LIB_LOAD);
      NextToken();
      break;

    case EXT_LIB_UNLOAD:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::EXT_LIB_UNLOAD);
      NextToken();
      break;

    case EXT_LIB_FUNC_CALL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::EXT_LIB_FUNC_CALL);
      NextToken();
      break;

    case THREAD_MUTEX:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::THREAD_MUTEX);
      NextToken();
      break;

    case THREAD_SLEEP:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::THREAD_SLEEP);
      NextToken();
      break;

    case THREAD_JOIN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::THREAD_JOIN);
      NextToken();
      break;

    case SYS_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYS_TIME);
      NextToken();
      break;

    case BYTES_TO_UNICODE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::BYTES_TO_UNICODE);
      NextToken();
      break;

    case UNICODE_TO_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNICODE_TO_BYTES);
      NextToken();
      break;

    case GMT_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GMT_TIME);
      NextToken();
      break;

    case FILE_CREATE_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_CREATE_TIME);
      NextToken();
      break;


    case FILE_ACCOUNT_OWNER:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_ACCOUNT_OWNER);
      NextToken();
      break;

    case FILE_GROUP_OWNER:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_GROUP_OWNER);
      NextToken();
      break;

    case FILE_MODIFIED_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_MODIFIED_TIME);
      NextToken();
      break;

    case FILE_ACCESSED_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_ACCESSED_TIME);
      NextToken();
      break;

    case DATE_TIME_SET_1:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TIME_SET_1);
      NextToken();
      break;

    case DATE_TIME_SET_2:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TIME_SET_2);
      NextToken();
      break;

    case DATE_TIME_ADD_DAYS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TIME_ADD_DAYS);
      NextToken();
      break;

    case DATE_TIME_ADD_HOURS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TIME_ADD_HOURS);
      NextToken();
      break;

    case DATE_TIME_ADD_MINS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TIME_ADD_MINS);
      NextToken();
      break;

    case DATE_TIME_ADD_SECS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TIME_ADD_SECS);
      NextToken();
      break;

    case DATE_FROM_UNIX_GMT_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_FROM_UNIX_GMT_TIME);
      NextToken();
      break;

    case DATE_FROM_UNIX_LOCAL_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_FROM_UNIX_LOCAL_TIME);
      NextToken();
      break;

    case DATE_TO_UNIX_TIME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DATE_TO_UNIX_TIME);
      NextToken();
      break;

    case GET_PLTFRM:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GET_PLTFRM);
      NextToken();
      break;

    case GET_UUID:
       statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                instructions::GET_UUID);
       NextToken();
       break;

    case GET_VERSION:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GET_VERSION);
      NextToken();
      break;

    case GET_SYS_PROP:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GET_SYS_PROP);
      NextToken();
      break;

    case GET_SYS_ENV:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GET_SYS_ENV);
      NextToken();
      break;

    case SET_SYS_ENV:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SET_SYS_ENV);
      NextToken();
      break;

    case SET_SYS_PROP:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SET_SYS_PROP);
      NextToken();
      break;

    case ASSERT_TRUE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ASSERT_TRUE);
      NextToken();
      break;

    case SYS_CMD:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYS_CMD);
      NextToken();
      break;

    case SYS_CMD_OUT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYS_CMD_OUT);
      NextToken();
      break;

    case SET_SIGNAL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SET_SIGNAL);
      NextToken();
      break;

    case RAISE_SIGNAL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
        instructions::RAISE_SIGNAL);
      NextToken();
      break;

    case EXIT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::EXIT);
      NextToken();
      break;

    case TIMER_START:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::TIMER_START);
      NextToken();
      break;

    case TIMER_END:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::TIMER_END);
      NextToken();
      break;

    case TIMER_ELAPSED:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::TIMER_ELAPSED);
      NextToken();
      break;

    case FLOR_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FLOR_FLOAT);
      NextToken();
      break;

    case CPY_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CPY_BYTE_ARY);
      NextToken();
      break;

    case S2I:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::S2I);
      NextToken();
      break;

    case S2F:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::S2F);
      NextToken();
      break;

    case I2S:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::I2S);
      NextToken();
      break;

    case F2S:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::F2S);
      NextToken();
      break;

    case LOAD_ARY_SIZE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOAD_ARY_SIZE);
      NextToken();
      break;

    case CPY_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CPY_CHAR_ARY);
      NextToken();
      break;

    case CPY_INT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CPY_INT_ARY);
      NextToken();
      break;

    case CPY_FLOAT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CPY_FLOAT_ARY);
      NextToken();
      break;

    case ZERO_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ZERO_BYTE_ARY);
      NextToken();
      break;

    case ZERO_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ZERO_CHAR_ARY);
      NextToken();
      break;

    case ZERO_INT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ZERO_INT_ARY);
      NextToken();
      break;

    case ZERO_FLOAT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ZERO_FLOAT_ARY);
      NextToken();
      break;

    case CEIL_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CEIL_FLOAT);
      NextToken();
      break;

    case TRUNC_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::TRUNC_FLOAT);
      NextToken();
      break;

    case SIN_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SIN_FLOAT);
      NextToken();
      break;

    case COS_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COS_FLOAT);
      NextToken();
      break;

    case TAN_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::TAN_FLOAT);
      NextToken();
      break;

    case ASIN_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ASIN_FLOAT);
      NextToken();
      break;

    case ACOS_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ACOS_FLOAT);
      NextToken();
      break;

    case ATAN_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ATAN_FLOAT);
      NextToken();
      break;

    case LOG2_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOG2_FLOAT);
      NextToken();
      break;

    case CBRT_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CBRT_FLOAT);
      NextToken();
      break;

    case ATAN2_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ATAN2_FLOAT);
      NextToken();
      break;

    case ACOSH_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ACOSH_FLOAT);
      NextToken();
      break;

    case ASINH_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ASINH_FLOAT);
      NextToken();
      break;

    case ATANH_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ATANH_FLOAT);
      NextToken();
      break;

    case COSH_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COSH_FLOAT);
      NextToken();
      break;

    case SINH_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SINH_FLOAT);
      NextToken();
      break;

    case TANH_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::TANH_FLOAT);
      NextToken();
      break;

    case MOD_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
        instructions::MOD_FLOAT);
      NextToken();
      break;

    case LOG_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOG_FLOAT);
      NextToken();
      break;

    case ROUND_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ROUND_FLOAT);
      NextToken();
      break;

    case EXP_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::EXP_FLOAT);
      NextToken();
      break;

    case LOG10_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOG10_FLOAT);
      NextToken();
      break;

    case POW_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::POW_FLOAT);
      NextToken();
      break;

    case SQRT_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SQRT_FLOAT);
      NextToken();
      break;

    case GAMMA_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GAMMA_FLOAT);
      NextToken();
      break;

    case RAND_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::RAND_FLOAT);
      NextToken();
      break;

    case NAN_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::NAN_INT);
      NextToken();
      break;

    case INF_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::INF_INT);
      NextToken();
      break;

    case NEG_INF_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::NEG_INF_INT);
      NextToken();
      break;

    case NAN_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::NAN_FLOAT);
      NextToken();
      break;

    case INF_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::INF_FLOAT);
      NextToken();
      break;

    case NEG_INF_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::NEG_INF_FLOAT);
      NextToken();
      break;

    case STD_OUT_BOOL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_BOOL);
      NextToken();
      break;

    case STD_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_BYTE);
      NextToken();
      break;

    case STD_OUT_CHAR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_CHAR);
      NextToken();
      break;

    case STD_OUT_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_INT);
      NextToken();
      break;

    case STD_OUT_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_FLOAT);
      NextToken();
      break;

    case STD_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_STRING);
      NextToken();
      break;

    case STD_OUT_BYTE_ARY_LEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_BYTE_ARY_LEN);
      NextToken();
      break;

    case STD_OUT_CHAR_ARY_LEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_OUT_CHAR_ARY_LEN);
      NextToken();
      break;

    case STD_IN_BYTE_ARY_LEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_IN_BYTE_ARY_LEN);
      NextToken();
      break;

    case STD_IN_CHAR_ARY_LEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_IN_CHAR_ARY_LEN);
      NextToken();
      break;

    case STD_ERR_BOOL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_BOOL);
      NextToken();
      break;

    case STD_ERR_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_BYTE);
      NextToken();
      break;

    case STD_ERR_CHAR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_CHAR);
      NextToken();
      break;

    case STD_ERR_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_INT);
      NextToken();
      break;

    case STD_ERR_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_FLOAT);
      NextToken();
      break;

    case STD_ERR_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_STRING);
      NextToken();
      break;

    case STD_ERR_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_BYTE_ARY);
      NextToken();
      break;

    case STD_ERR_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_CHAR_ARY);
      NextToken();
      break;

    case STD_FLUSH:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_FLUSH);
      NextToken();
      break;

    case STD_ERR_FLUSH:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_ERR_FLUSH);
      NextToken();
      break;

    case LOAD_MULTI_ARY_SIZE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOAD_MULTI_ARY_SIZE);
      NextToken();
      break;

    case LOAD_CLS_INST_ID:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOAD_CLS_INST_ID);
      NextToken();
      break;

    case STRING_HASH_ID:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STRING_HASH_ID);
      NextToken();
      break;

    case LOAD_CLS_BY_INST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOAD_CLS_BY_INST);
      NextToken();
      break;

    case LOAD_NEW_OBJ_INST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOAD_NEW_OBJ_INST);
      NextToken();
      break;

    case LOAD_INST_UID:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::LOAD_INST_UID);
      NextToken();
      break;

    case STD_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::STD_IN_STRING);
      NextToken();
      break;

    case STD_INT_FMT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), instructions::STD_INT_FMT);
      NextToken();
      break;

    case STD_FLOAT_FMT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),	instructions::STD_FLOAT_FMT);
      NextToken();
      break;

    case STD_FLOAT_PER:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),	instructions::STD_FLOAT_PER);
      NextToken();
      break;

    case STD_WIDTH:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), instructions::STD_WIDTH);
      NextToken();
      break;

    case STD_FILL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),	instructions::STD_FILL);
      NextToken();
      break;

    case COMPRESS_ZLIB_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COMPRESS_ZLIB_BYTES);
      NextToken();
      break;

    case UNCOMPRESS_ZLIB_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_ZLIB_BYTES);
      NextToken();
      break;

    case COMPRESS_GZIP_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COMPRESS_GZIP_BYTES);
      NextToken();
      break;

    case UNCOMPRESS_GZIP_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_GZIP_BYTES);
      NextToken();
      break;

    case COMPRESS_DEFLATE_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COMPRESS_DEFLATE_BYTES);
      NextToken();
      break;

    case UNCOMPRESS_DEFLATE_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_DEFLATE_BYTES);
      NextToken();
      break;

    case CRC32_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::CRC32_BYTES);
      NextToken();
      break;

    case FILE_OPEN_READ:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OPEN_READ);
      NextToken();
      break;

    case FILE_OPEN_APPEND:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OPEN_APPEND);
      NextToken();
      break;

    case FILE_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_CLOSE);
      NextToken();
      break;

    case FILE_FLUSH:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_FLUSH);
      NextToken();
      break;

    case FILE_IN_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_IN_BYTE);
      NextToken();
      break;

    case FILE_IN_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_IN_BYTE_ARY);
      NextToken();
      break;

    case FILE_IN_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_IN_CHAR_ARY);
      NextToken();
      break;

    case FILE_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_IN_STRING);
      NextToken();
      break;

    case FILE_OPEN_WRITE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OPEN_WRITE);
      NextToken();
      break;

    case FILE_OPEN_READ_WRITE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OPEN_READ_WRITE);
      NextToken();
      break;

    case FILE_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OUT_BYTE);
      NextToken();
      break;

    case FILE_OUT_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OUT_BYTE_ARY);
      NextToken();
      break;

    case FILE_OUT_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OUT_CHAR_ARY);
      NextToken();
      break;

    case FILE_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_OUT_STRING);
      NextToken();
      break;

    case FILE_EXISTS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_EXISTS);
      NextToken();
      break;

    case FILE_CAN_READ_ONLY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_CAN_READ_ONLY);
      NextToken();
      break;

    case FILE_CAN_WRITE_ONLY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_CAN_WRITE_ONLY);
      NextToken();
      break;

    case FILE_CAN_READ_WRITE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_CAN_READ_WRITE);
      NextToken();
      break;

    case FILE_SIZE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_SIZE);
      NextToken();
      break;

    case FILE_FULL_PATH:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_FULL_PATH);
      NextToken();
      break;

    case FILE_TEMP_NAME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_TEMP_NAME);
      NextToken();
      break;

    case FILE_SEEK:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_SEEK);
      NextToken();
      break;

    case FILE_EOF:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_EOF);
      NextToken();
      break;

    case FILE_DELETE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_DELETE);
      NextToken();
      break;

    case FILE_RENAME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_RENAME);
      NextToken();
      break;

    case FILE_COPY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_COPY);
      NextToken();
      break;

    case PIPE_OPEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_OPEN);
      NextToken();
      break;

    case PIPE_CREATE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_CREATE);
      NextToken();
      break;

    case PIPE_IN_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_IN_BYTE);
      NextToken();
      break;

    case PIPE_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_OUT_BYTE);
      NextToken();
      break;

    case PIPE_IN_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_IN_BYTE_ARY);
      NextToken();
      break;

    case PIPE_IN_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_IN_CHAR_ARY);
      NextToken();
      break;

    case PIPE_OUT_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_OUT_BYTE_ARY);
      NextToken();
      break;

    case PIPE_OUT_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_OUT_CHAR_ARY);
      NextToken();
      break;

    case PIPE_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_IN_STRING);
      NextToken();
      break;

    case PIPE_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_OUT_STRING);
      NextToken();
      break;
      
    case PIPE_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PIPE_CLOSE);
      NextToken();
      break;

    case DIR_CREATE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_CREATE);
      NextToken();
      break;

    case DIR_SLASH:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_SLASH);
      NextToken();
      break;

    case DIR_EXISTS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_EXISTS);
      NextToken();
      break;

    case DIR_COPY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_COPY);
      NextToken();
      break;
      
    case DIR_LIST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_LIST);
      NextToken();
      break;

    case DIR_DELETE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_DELETE);
      NextToken();
      break;

    case DIR_GET_CUR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_GET_CUR);
      NextToken();
      break;

    case DIR_SET_CUR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DIR_SET_CUR);
      NextToken();
      break;

    case SYM_LINK_CREATE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYM_LINK_CREATE);
      NextToken();
      break;

    case SYM_LINK_COPY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYM_LINK_COPY);
      NextToken();
      break;

    case SYM_LINK_LOC:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYM_LINK_LOC);
      NextToken();
      break;

    case SYM_LINK_EXISTS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SYM_LINK_EXISTS);
      NextToken();
      break;

    case HARD_LINK_CREATE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HARD_LINK_CREATE);
      NextToken();
      break;

    case FILE_REWIND:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_REWIND);
      NextToken();
      break;

    case FILE_IS_OPEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_IS_OPEN);
      NextToken();
      break;

    case SOCK_TCP_CONNECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_CONNECT);
      NextToken();
      break;

    case SOCK_TCP_BIND:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_BIND);
      NextToken();
      break;

    case SOCK_UDP_CREATE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_CREATE);
      NextToken();
      break;

    case SOCK_UDP_BIND:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_BIND);
      NextToken();
      break;

    case SOCK_UDP_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_CLOSE);
      NextToken();
      break;

    case SOCK_UDP_IN_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_IN_BYTE);
      NextToken();
      break;

    case SOCK_UDP_IN_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_IN_BYTE_ARY);
      NextToken();
      break;

    case SOCK_UDP_IN_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_IN_CHAR_ARY);
      NextToken();
      break;

    case SOCK_UDP_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_OUT_BYTE);
      NextToken();
      break;

    case SOCK_UDP_OUT_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_OUT_BYTE_ARY);
      NextToken();
      break;

    case SOCK_UDP_OUT_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_OUT_CHAR_ARY);
      NextToken();
      break;

    case SOCK_UDP_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_IN_STRING);
      NextToken();
      break;

    case SOCK_UDP_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_UDP_OUT_STRING);
      NextToken();
      break;




    case SOCK_TCP_LISTEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_LISTEN);
      NextToken();
      break;

    case SOCK_TCP_ACCEPT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_ACCEPT);
      NextToken();
      break;

    case SOCK_TCP_SELECT:
       statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                instructions::SOCK_TCP_SELECT);
       NextToken();
       break;

    case SOCK_TCP_SSL_LISTEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), 
                                                               instructions::SOCK_TCP_SSL_LISTEN);
      NextToken();
      break;

    case SOCK_TCP_SSL_ACCEPT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), 
                                                               instructions::SOCK_TCP_SSL_ACCEPT);
      NextToken();
      break;

    case SOCK_TCP_SSL_SELECT:
       statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                instructions::SOCK_TCP_SSL_SELECT);
       NextToken();
       break;

    case SOCK_TCP_SSL_SRV_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), 
                                                               instructions::SOCK_TCP_SSL_SRV_CLOSE);
      NextToken();
      break;

    case SOCK_TCP_SSL_ERROR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), instructions::SOCK_TCP_SSL_ERROR);
      NextToken();
      break;

    case SOCK_IP_ERROR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), instructions::SOCK_IP_ERROR);
      NextToken();
      break;

    case SOCK_TCP_IS_CONNECTED:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_IS_CONNECTED);
      NextToken();
      break;

    case SOCK_TCP_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_CLOSE);
      NextToken();
      break;

    case SOCK_TCP_IN_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_IN_BYTE);
      NextToken();
      break;

    case SOCK_TCP_IN_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_IN_BYTE_ARY);
      NextToken();
      break;

    case SOCK_TCP_IN_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_IN_CHAR_ARY);
      NextToken();
      break;

    case SOCK_TCP_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_OUT_STRING);
      NextToken();
      break;

    case SOCK_TCP_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_IN_STRING);
      NextToken();
      break;

    case SOCK_TCP_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_OUT_BYTE);
      NextToken();
      break;

    case SOCK_TCP_OUT_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_OUT_BYTE_ARY);
      NextToken();
      break;

    case SOCK_TCP_OUT_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_OUT_CHAR_ARY);
      NextToken();
      break;

    case SOCK_TCP_HOST_NAME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_HOST_NAME);
      NextToken();
      break;

    case SOCK_TCP_RESOLVE_NAME:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_RESOLVE_NAME);
      NextToken();
      break;

    case SOCK_TCP_SSL_CONNECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_CONNECT);
      NextToken();
      break;

    case SOCK_TCP_SSL_ISSUER:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_ISSUER);
      NextToken();
      break;

    case SOCK_TCP_SSL_SUBJECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SUBJECT);
      NextToken();
      break;

    case SOCK_TCP_SSL_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_CLOSE);
      NextToken();
      break;

    case SOCK_TCP_SSL_IN_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_IN_BYTE);
      NextToken();
      break;

    case SOCK_TCP_SSL_IN_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_IN_BYTE_ARY);
      NextToken();
      break;

    case SOCK_TCP_SSL_IN_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_IN_CHAR_ARY);
      NextToken();
      break;

    case SOCK_TCP_SSL_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_OUT_STRING);
      NextToken();
      break;

    case SOCK_TCP_SSL_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_IN_STRING);
      NextToken();
      break;

    case SOCK_TCP_SSL_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_OUT_BYTE);
      NextToken();
      break;

    case SOCK_TCP_SSL_OUT_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_OUT_BYTE_ARY);
      NextToken();
      break;

    case SOCK_TCP_SSL_OUT_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_OUT_CHAR_ARY);
      NextToken();
      break;

    case SOCK_DTLS_CONNECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_CONNECT);
      NextToken();
      break;

    case SOCK_DTLS_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_CLOSE);
      NextToken();
      break;

    case SOCK_DTLS_ISSUER:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_ISSUER);
      NextToken();
      break;

    case SOCK_DTLS_SUBJECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_SUBJECT);
      NextToken();
      break;

    case SOCK_DTLS_SELECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_SELECT);
      NextToken();
      break;

    case SOCK_DTLS_ERROR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_ERROR);
      NextToken();
      break;

    case SOCK_DTLS_IN_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_IN_BYTE);
      NextToken();
      break;

    case SOCK_DTLS_IN_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_IN_BYTE_ARY);
      NextToken();
      break;

    case SOCK_DTLS_IN_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_IN_CHAR_ARY);
      NextToken();
      break;

    case SOCK_DTLS_IN_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_IN_STRING);
      NextToken();
      break;

    case SOCK_DTLS_OUT_BYTE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_OUT_BYTE);
      NextToken();
      break;

    case SOCK_DTLS_OUT_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_OUT_BYTE_ARY);
      NextToken();
      break;

    case SOCK_DTLS_OUT_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_OUT_CHAR_ARY);
      NextToken();
      break;

    case SOCK_DTLS_OUT_STRING:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_OUT_STRING);
      NextToken();
      break;

    case SOCK_DTLS_LISTEN:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_LISTEN);
      NextToken();
      break;

    case SOCK_DTLS_ACCEPT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_ACCEPT);
      NextToken();
      break;

    case SOCK_DTLS_SRV_CERT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_SRV_CERT);
      NextToken();
      break;

    case SOCK_DTLS_SRV_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_SRV_CLOSE);
      NextToken();
      break;

    case SOCK_TCP_SET_KEEPALIVE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_KEEPALIVE);
      NextToken();
      break;

    case SOCK_TCP_SET_NODELAY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_NODELAY);
      NextToken();
      break;

    case SOCK_TCP_SET_RECV_TIMEOUT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_RECV_TIMEOUT);
      NextToken();
      break;

    case SOCK_TCP_SET_SEND_TIMEOUT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_SEND_TIMEOUT);
      NextToken();
      break;

    case SOCK_TCP_SET_CONN_TIMEOUT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_CONN_TIMEOUT);
      NextToken();
      break;

    case SOCK_TCP_SET_RCVBUF:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_RCVBUF);
      NextToken();
      break;

    case SOCK_TCP_SET_SNDBUF:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_SNDBUF);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_KEEPALIVE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_KEEPALIVE);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_NODELAY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_NODELAY);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_RECV_TIMEOUT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_RECV_TIMEOUT);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_SEND_TIMEOUT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_SEND_TIMEOUT);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_RCVBUF:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_RCVBUF);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_SNDBUF:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_SNDBUF);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_MIN_TLS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_MIN_TLS);
      NextToken();
      break;

    case SOCK_TCP_SSL_SET_VERIFY_PEER:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_SET_VERIFY_PEER);
      NextToken();
      break;

    case SOCK_TCP_SSL_GET_FINGERPRINT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_GET_FINGERPRINT);
      NextToken();
      break;

    case HTTP2_CONNECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP2_CONNECT);
      NextToken();
      break;

    case HTTP2_REQUEST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP2_REQUEST);
      NextToken();
      break;

    case HTTP2_REQUEST_HDRS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP2_REQUEST_HDRS);
      NextToken();
      break;

    case HTTP2_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP2_CLOSE);
      NextToken();
      break;

    case HTTP3_CONNECT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP3_CONNECT);
      NextToken();
      break;

    case HTTP3_REQUEST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP3_REQUEST);
      NextToken();
      break;

    case HTTP3_REQUEST_HDRS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP3_REQUEST_HDRS);
      NextToken();
      break;

    case FLOAT_TO_STR_PREC:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FLOAT_TO_STR_PREC);
      NextToken();
      break;

    case FILE_IN_LINE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_IN_LINE);
      NextToken();
      break;

    case COND_NEW:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COND_NEW);
      NextToken();
      break;

    case COND_WAIT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COND_WAIT);
      NextToken();
      break;

    case COND_SIGNAL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COND_SIGNAL);
      NextToken();
      break;

    case RUNTIME_GET_ERROR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::RUNTIME_GET_ERROR);
      NextToken();
      break;

    case RUNTIME_GET_ERROR_KIND:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::RUNTIME_GET_ERROR_KIND);
      NextToken();
      break;

    case RUNTIME_CLEAR_ERROR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::RUNTIME_CLEAR_ERROR);
      NextToken();
      break;

    case RUNTIME_RAISE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::RUNTIME_RAISE);
      NextToken();
      break;

    case RUNTIME_GET_LAST_ERROR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::RUNTIME_GET_LAST_ERROR);
      NextToken();
      break;

    case ARY_COPY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ARY_COPY);
      NextToken();
      break;

    case ARY_FILL_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ARY_FILL_INT);
      NextToken();
      break;

    case ARY_FILL_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ARY_FILL_FLOAT);
      NextToken();
      break;

    case FLOAT_TO_STR_SHORT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FLOAT_TO_STR_SHORT);
      NextToken();
      break;

    case DESERL_BOOL_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_BOOL_ARY);
      NextToken();
      break;

    case ARY_TO_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::ARY_TO_BYTES);
      NextToken();
      break;

    case BYTES_TO_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::BYTES_TO_ARY);
      NextToken();
      break;

    case COMPRESS_ZLIB_LEVEL:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COMPRESS_ZLIB_LEVEL);
      NextToken();
      break;

    case FILE_CREATE_NEW:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::FILE_CREATE_NEW);
      NextToken();
      break;

    case PROCESS_ID:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PROCESS_ID);
      NextToken();
      break;

    case PROCESS_ALIVE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::PROCESS_ALIVE);
      NextToken();
      break;

    case UNCOMPRESS_DEFLATE_LIMIT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_DEFLATE_LIMIT);
      NextToken();
      break;

    case UNCOMPRESS_GZIP_LIMIT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_GZIP_LIMIT);
      NextToken();
      break;

    case SOCK_TCP_BIND_ADDR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_BIND_ADDR);
      NextToken();
      break;

    case SOCK_TCP_SSL_LISTEN_ADDR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SSL_LISTEN_ADDR);
      NextToken();
      break;

    case SOCK_DTLS_LISTEN_ADDR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_DTLS_LISTEN_ADDR);
      NextToken();
      break;

    case THREAD_CURRENT_ID:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::THREAD_CURRENT_ID);
      NextToken();
      break;

    case THREAD_IS_ALIVE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::THREAD_IS_ALIVE);
      NextToken();
      break;

    case GC_TEST_ADDRESS:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::GC_TEST_ADDRESS);
      NextToken();
      break;

    case SOCK_TCP_SET_DEADLINE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SET_DEADLINE);
      NextToken();
      break;

    case SOCK_TCP_SHUTDOWN_SEND:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SOCK_TCP_SHUTDOWN_SEND);
      NextToken();
      break;

    case UNCOMPRESS_ZLIB_TRY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_ZLIB_TRY);
      NextToken();
      break;

    case HTTP3_CLOSE:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::HTTP3_CLOSE);
      NextToken();
      break;

    case SERL_CHAR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_CHAR);
      NextToken();
      break;

    case SERL_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_INT);
      NextToken();
      break;

    case SERL_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_FLOAT);
      NextToken();
      break;

    case SERL_OBJ_INST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_OBJ_INST);
      NextToken();
      break;

    case SERL_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_BYTE_ARY);
      NextToken();
      break;

    case SERL_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_CHAR_ARY);
      NextToken();
      break;

    case SERL_INT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_INT_ARY);
      NextToken();
      break;

    case SERL_OBJ_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_OBJ_ARY);
      NextToken();
      break;

    case SERL_FLOAT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::SERL_FLOAT_ARY);
      NextToken();
      break;

    case DESERL_CHAR:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_CHAR);
      NextToken();
      break;

    case DESERL_INT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_INT);
      NextToken();
      break;

    case DESERL_FLOAT:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_FLOAT);
      NextToken();
      break;

    case DESERL_OBJ_INST:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_OBJ_INST);
      NextToken();
      break;

    case DESERL_BYTE_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_BYTE_ARY);
      NextToken();
      break;

    case DESERL_CHAR_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_CHAR_ARY);
      NextToken();
      break;

    case DESERL_INT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_INT_ARY);
      NextToken();
      break;

    case DESERL_OBJ_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_OBJ_ARY);
      NextToken();
      break;

    case DESERL_FLOAT_ARY:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::DESERL_FLOAT_ARY);
      NextToken();
      break;
#endif

    default:
      statement = TreeFactory::Instance()->MakeSimpleStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), ParseSimpleExpression(depth + 1));
      break;
    }
  }

  if(alt_syntax && statement) {
    switch(statement->GetStatementType()) {
    case IF_STMT:
    case WHILE_STMT:
    case DO_WHILE_STMT:
    case FOR_STMT:
    case SELECT_STMT:
    case LEAVING_STMT:
      break;

    default:
      if(semi_colon) {
        if(!Match(TOKEN_SEMI_COLON)) {
          ProcessError(L"Invalid statement; expected ';'", TOKEN_SEMI_COLON);
        }
        NextToken();
      }
      break;
    }
  }
  else {
    // semi-colon at the end of block statements, optional
    if(Match(TOKEN_SEMI_COLON)) {
      is_semi_colon = true;
      NextToken();
    }
  }

  return statement;
}

/****************************
 * True when the current token assigns to the target just parsed: ':=', the
 * compound forms, '++' and '--'.
 ****************************/
bool Parser::IsOperationAssignToken()
{
  switch(GetToken()) {
  case TOKEN_ASSIGN:
  case TOKEN_ADD_ASSIGN:
  case TOKEN_SUB_ASSIGN:
  case TOKEN_MUL_ASSIGN:
  case TOKEN_DIV_ASSIGN:
  case TOKEN_ADD_ADD:
  case TOKEN_SUB_SUB:
    return true;

  default:
    return false;
  }
}

/****************************
 * Parses the assignment to an indexed target, 'a[i] := v', 'a[i] += v' or
 * 'a[i]++'; the current token is the operator.
 ****************************/
Statement* Parser::ParseVariableOperation(Variable* variable, int line_num, int line_pos, const std::wstring& file_name, int depth)
{
  CheckDepth(depth);
  Statement* statement = nullptr;

  // 'rows[i][j] := v' (#1246): the row is read into a hidden local, which takes
  // the last subscript -- as for an indexed call result (ParseCallElementOperation)
  const std::vector<ExpressionList*> chained = variable->GetChainedIndices();
  if(!chained.empty()) {
    const std::wstring tmp_ident = L"#ary_" + std::to_wstring(hidden_local_id++);
    SymbolEntry* tmp_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(tmp_ident),
                                                                      TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
    symbol_table->CurrentParseScope()->AddEntry(tmp_entry);

    Variable* source = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, variable->GetName());
    source->SetIndices(variable->GetIndices());
    for(size_t i = 0; i + 1 < chained.size(); ++i) {
      source->AddChainedIndices(chained[i]);
    }

    Variable* tmp_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
    Assignment* tmp_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), tmp_var, source);
    pending_statements.push_back(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), tmp_entry, tmp_assign));

    Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
    target->SetIndices(chained.back());
    variable = target;
  }

  switch(GetToken()) {
  case TOKEN_ASSIGN:
    statement = ParseAssignment(variable, depth + 1);
    break;

  case TOKEN_ADD_ASSIGN: {
    NextToken();

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 variable, asgn_expr, ADD_ASSIGN_STMT);
  }
    break;

  case TOKEN_SUB_ASSIGN: {
    NextToken();

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 variable, asgn_expr, SUB_ASSIGN_STMT);
  }
    break;

  case TOKEN_MUL_ASSIGN: {
    NextToken();

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 variable, asgn_expr, MUL_ASSIGN_STMT);
  }
    break;

  case TOKEN_DIV_ASSIGN: {
    NextToken();

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 variable, asgn_expr, DIV_ASSIGN_STMT);
  }
    break;

  case TOKEN_ADD_ADD: {
    NextToken();

    Expression* asgn_lit = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1);
    asgn_lit->SetLinePosition(asgn_lit->GetLinePosition() + 1);

    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 variable, asgn_lit, ADD_ASSIGN_STMT);
  }
    break;

  case TOKEN_SUB_SUB: {
    NextToken();

    Expression* asgn_lit = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1);
    asgn_lit->SetLinePosition(asgn_lit->GetLinePosition() + 1);

    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 variable, asgn_lit, SUB_ASSIGN_STMT);
  }
    break;

  default:
    break;
  }

  return statement;
}

/****************************
 * 'f->GetCells()[0] := 99': the call's result goes into a hidden local,
 * declared just before this statement, and the element is assigned through
 * it, so 'f->GetCells()[0] += 5' evaluates the call once (#1184).
 ****************************/
Statement* Parser::ParseCallElementOperation(MethodCall* head, int line_num, int line_pos, const std::wstring& file_name, int depth)
{
  CheckDepth(depth);
  MethodCall* tail = head;
  while(tail->GetMethodCall()) {
    tail = tail->GetMethodCall();
  }

  ExpressionList* indices = tail->GetCallIndices();
  if(!indices) {
    ProcessError(L"Invalid assignment target; expected a variable or an indexed call result");
    return head;
  }
  tail->SetCallIndices(nullptr);

  const std::wstring tmp_ident = L"#ary_" + std::to_wstring(hidden_local_id++);
  SymbolEntry* tmp_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(tmp_ident),
                                                                    TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
  symbol_table->CurrentParseScope()->AddEntry(tmp_entry);

  Variable* tmp_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
  Assignment* tmp_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), tmp_var, head);
  pending_statements.push_back(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), tmp_entry, tmp_assign));

  Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
  target->SetIndices(indices);
  return ParseVariableOperation(target, line_num, line_pos, file_name, depth + 1);
}

/****************************
 * A 'ref x' argument (#1243): '#ref_N := [x]' goes ahead of the statement, the
 * call takes '#ref_N', and 'x := #ref_N[0]' follows the statement. Only a plain
 * variable or field can be passed: the copy back writes to it by name.
 ****************************/
Expression* Parser::ParseReferenceArgument(Expression* expression)
{
  const std::wstring file_name = expression->GetFileName();
  const int line_num = expression->GetLineNumber();
  const int line_pos = expression->GetLinePosition();

  if(expression->GetExpressionType() != VAR_EXPR || expression->GetMethodCall() ||
     static_cast<Variable*>(expression)->GetIndices() || expression->GetCastType()) {
    ProcessError(L"Only a variable or a field can be passed as 'ref'");
    return expression;
  }
  const std::wstring name = static_cast<Variable*>(expression)->GetName();

  const std::wstring tmp_ident = L"#ref_" + std::to_wstring(hidden_local_id++);
  SymbolEntry* tmp_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(tmp_ident),
                                                                    TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
  symbol_table->CurrentParseScope()->AddEntry(tmp_entry);

  // #ref_N := [x]
  ExpressionList* cell = TreeFactory::Instance()->MakeExpressionList();
  cell->AddExpression(expression);
  Variable* tmp_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
  Assignment* tmp_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, tmp_var,
                                                                   TreeFactory::Instance()->MakeStaticArray(file_name, line_num, line_pos, cell));
  pending_statements.push_back(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, tmp_entry, tmp_assign));

  // x := #ref_N[0]
  Variable* source = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
  ExpressionList* zero = TreeFactory::Instance()->MakeExpressionList();
  zero->AddExpression(TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0));
  source->SetIndices(zero);
  Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, name);
  trailing_statements.push_back(TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, target, source));

  return TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
}

/****************************
 * Moves the copies back the last statement's 'ref' arguments need (#1243)
 * into its list, after it. A statement that decides control flow -- a
 * condition, a loop, a return -- would copy back at the wrong time, so it
 * cannot take one.
 ****************************/
void Parser::TakeTrailingStatements(StatementList* statements, Statement* statement)
{
  if(trailing_statements.empty()) {
    return;
  }

  if(statement) {
    switch(statement->GetStatementType()) {
    case METHOD_CALL_STMT:
    case SIMPLE_STMT:
    case ASSIGN_STMT:
    case DECLARATION_STMT:
    case ADD_ASSIGN_STMT:
    case SUB_ASSIGN_STMT:
    case MUL_ASSIGN_STMT:
    case DIV_ASSIGN_STMT:
    case STRING_CONCAT_STMT:
      break;

    default:
      ProcessError(L"A call with 'ref' arguments must be a statement of its own or the right side of an assignment");
      trailing_statements.clear();
      return;
    }
  }

  for(size_t i = 0; i < trailing_statements.size(); ++i) {
    statements->AddStatement(trailing_statements[i]);
  }
  trailing_statements.clear();
}

/****************************
 * Declares the hidden local '#<what>_<n>' that a SequenceExpression leaves its
 * value in. Its type is the type of the first value assigned to it. Such an
 * expression needs a method's frame to hold the local, so it is an error where
 * there is none.
 ****************************/
SymbolEntry* Parser::AddHiddenLocal(const std::wstring& what, std::wstring& ident, int line_num, int line_pos)
{
  const std::wstring file_name = GetFileName();
  ident = L'#' + what + L'_' + std::to_wstring(hidden_local_id++);
  if(!current_method) {
    ProcessError(L"This expression is only allowed inside a function or method");
    return nullptr;
  }

  SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(ident),
                                                                TypeFactory::Instance()->MakeType(VAR_TYPE), false, true);
  if(!symbol_table->CurrentParseScope()->AddEntry(entry)) {
    ProcessError(L"Variable already defined in this scope: '" + ident + L"'");
    return nullptr;
  }

  return entry;
}

/****************************
 * Moves the hidden declarations the last statement needs into its list,
 * ahead of it.
 ****************************/
void Parser::TakePendingStatements(StatementList* statements)
{
  for(size_t i = 0; i < pending_statements.size(); ++i) {
    statements->AddStatement(pending_statements[i]);
  }
  pending_statements.clear();
}

/****************************
 * Semicolons between statements are optional, so 'Foo(2) Foo(3);' and
 * 'x := 1 Foo(3);' compiled as two statements without a word. Two statements
 * on one line need a ';' between them; a block statement ('if', 'while', ...)
 * ends at its '}' and may be followed directly (#1184).
 ****************************/
void Parser::CheckStatementSeparator(Statement* statement)
{
  if(alt_syntax || is_semi_colon || !statement || Match(TOKEN_CLOSED_BRACE) || Match(TOKEN_END_OF_STREAM) ||
     GetLineNumber() != prev_token_line) {
    return;
  }

  switch(statement->GetStatementType()) {
  case DECLARATION_STMT:
  case ASSIGN_STMT:
  case ADD_ASSIGN_STMT:
  case SUB_ASSIGN_STMT:
  case MUL_ASSIGN_STMT:
  case DIV_ASSIGN_STMT:
  case STRING_CONCAT_STMT:
  case METHOD_CALL_STMT:
  case SIMPLE_STMT:
  case RETURN_STMT:
  case BREAK_STMT:
  case CONTINUE_STMT:
    ProcessError(L"Expected ';' between two statements on the same line");
    break;

  default:
    break;
  }
}

/****************************
 * Parses identifier-starting statements.
 ****************************/
Statement* Parser::ParseIdentifierStatement(const std::wstring& ident, int line_num, int line_pos, const std::wstring& file_name, int depth)
{
  CheckDepth(depth);
  Statement* statement = nullptr;

  switch(GetToken()) {
  case TOKEN_COLON:
  case TOKEN_COMMA: {
    IdentifierContext ident_context(ident, line_num, line_pos);
    statement = ParseDeclaration(ident_context, true, depth + 1);
  }
    break;

  case TOKEN_ASSESSOR:
  case TOKEN_OPEN_PAREN: {
    IdentifierContext ident_context(ident, line_num, line_pos);
    MethodCall* method_call = ParseMethodCall(ident_context, depth + 1);
    // 'f->GetCells()[0] := 99' stores into the call's result (#1184)
    if(method_call && IsOperationAssignToken()) {
      statement = ParseCallElementOperation(method_call, line_num, line_pos, file_name, depth + 1);
    }
    else {
      statement = method_call;
    }
  }
    break;

  case TOKEN_OPEN_BRACKET: {
    IdentifierContext ident_context(ident, line_num, line_pos);
    Variable* variable = ParseVariable(ident_context, depth + 1);

    switch(GetToken()) {
    case TOKEN_ASSESSOR:
      // subsequent method
      if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) &&
         !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
        statement = ParseMethodCall(variable, depth + 1);
      }
      // type cast
      else {
        ParseCastTypeOf(variable, depth + 1);
        statement = TreeFactory::Instance()->MakeSimpleStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable);
      }
      break;

    default:
      if(IsOperationAssignToken()) {
        statement = ParseVariableOperation(variable, line_num, line_pos, file_name, depth + 1);
      }
      else {
        ProcessError(L"Expected statement", TOKEN_SEMI_COLON);
      }
      break;
    }
  }
    break;

  case TOKEN_ASSIGN: {
    IdentifierContext ident_context(ident, line_num, line_pos);
    statement = ParseAssignment(ParseVariable(ident_context, depth + 1), depth + 1);
  }
    break;

  case TOKEN_ADD_ASSIGN: {
    IdentifierContext ident_context(ident, line_num, line_pos - 1);
    NextToken();

    Variable* asgn_var = ParseVariable(ident_context, depth + 1);
    asgn_var->SetLinePosition(line_pos);

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 asgn_var, asgn_expr, ADD_ASSIGN_STMT);
  }
    break;

  case TOKEN_SUB_ASSIGN: {
    IdentifierContext ident_context(ident, line_num, line_pos - 1);
    NextToken();

    Variable* asgn_var = ParseVariable(ident_context, depth + 1);
    asgn_var->SetLinePosition(line_pos);

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 asgn_var, asgn_expr, SUB_ASSIGN_STMT);
  }
    break;

  case TOKEN_MUL_ASSIGN: {
    IdentifierContext ident_context(ident, line_num, line_pos - 1);
    NextToken();

    Variable* asgn_var = ParseVariable(ident_context, depth + 1);
    asgn_var->SetLinePosition(line_pos);

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 asgn_var, asgn_expr, MUL_ASSIGN_STMT);
  }
    break;

  case TOKEN_DIV_ASSIGN: {
    IdentifierContext ident_context(ident, line_num, line_pos - 1);
    NextToken();

    Variable* asgn_var = ParseVariable(ident_context, depth + 1);
    asgn_var->SetLinePosition(line_pos);

    Expression* asgn_expr = ParseExpression(depth + 1);
    if(!asgn_expr) {
      return nullptr;
    }

    asgn_expr->SetLinePosition(asgn_expr->GetLinePosition() + 1);
    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 asgn_var, asgn_expr, DIV_ASSIGN_STMT);
  }
    break;

  case TOKEN_ADD_ADD: {
    IdentifierContext ident_context(ident, line_num, line_pos - 1);
    NextToken();

    Variable* asgn_var = ParseVariable(ident_context, depth + 1);
    asgn_var->SetLinePosition(line_pos);

    Expression* asgn_lit = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1);
    asgn_lit->SetLinePosition(asgn_lit->GetLinePosition() + 1);

    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 asgn_var, asgn_lit, ADD_ASSIGN_STMT);
  }
    break;

  case TOKEN_SUB_SUB: {
    IdentifierContext ident_context(ident, line_num, line_pos - 1);
    NextToken();

    Variable* asgn_var = ParseVariable(ident_context, depth + 1);
    asgn_var->SetLinePosition(line_pos);

    Expression* asgn_lit = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1);
    asgn_lit->SetLinePosition(asgn_lit->GetLinePosition() + 1);

    statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                                 asgn_var, asgn_lit, SUB_ASSIGN_STMT);
  }
    break;

  default:
    ProcessError(L"Expected statement", TOKEN_SEMI_COLON);
    NextToken();
    break;
  }

  return statement;
}

/****************************
 * Parses prefix operator statements (++x, --x).
 ****************************/
Statement* Parser::ParsePrefixOperatorStatement(int line_num, int line_pos, const std::wstring& file_name, int depth)
{
  CheckDepth(depth);
  Statement* statement = nullptr;

  switch(GetToken()) {
  case TOKEN_ADD_ADD: {
    const std::wstring ident = ParseBundleName();
    line_num = GetLineNumber();
    line_pos = GetLinePosition();

    IdentifierContext ident_context(ident, line_num, line_pos);
    NextToken();

    if(Match(TOKEN_IDENT)) {
      Variable* variable = ParseVariable(ident_context, depth + 1);
      statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable,
                                                                   TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1),
                                                                   ADD_ASSIGN_STMT);
    }
    else {
      ProcessError(L"Expected identifier", TOKEN_SEMI_COLON);
    }
  }
    break;

  case TOKEN_SUB_SUB: {
    const std::wstring ident = ParseBundleName();
    IdentifierContext ident_context(ident, line_num, line_pos);
    NextToken();

    if(Match(TOKEN_IDENT)) {
      Variable* variable = ParseVariable(ident_context, depth + 1);
      statement = TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable,
                                                                   TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1),
                                                                   SUB_ASSIGN_STMT);
    }
    else {
      ProcessError(L"Expected identifier", TOKEN_SEMI_COLON);
    }
  }
    break;

  default:
    break;
  }

  return statement;
}

/****************************
 * Parses control flow statements.
 ****************************/
Statement* Parser::ParseControlFlowStatement(int line_num, int line_pos, const std::wstring& file_name, int depth)
{
  CheckDepth(depth);
  Statement* statement = nullptr;

  switch(GetToken()) {
  case TOKEN_SEMI_COLON:
  case TOKEN_COMMA:
    statement = TreeFactory::Instance()->MakeEmptyStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition());
    break;

  case TOKEN_PARENT_ID:
    statement = ParseMethodCall(depth + 1);
    break;

  case TOKEN_RETURN_ID:
    statement = ParseReturn(depth + 1);
    break;

  case TOKEN_LEAVING_ID:
    statement = ParseLeaving(depth + 1);
    break;

  case TOKEN_IF_ID:
    statement = ParseIf(depth + 1);
    break;

  case TOKEN_DO_ID:
    statement = ParseDoWhile(depth + 1);
    break;

  case TOKEN_WHILE_ID:
    statement = ParseWhile(depth + 1);
    break;

  case TOKEN_BREAK_ID:
    statement = TreeFactory::Instance()->MakeBreakContinue(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), BREAK_STMT);
    NextToken();
    break;

  case TOKEN_CONTINUE_ID:
    statement = TreeFactory::Instance()->MakeBreakContinue(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), CONTINUE_STMT);
    NextToken();
    break;

  case TOKEN_SELECT_ID:
    statement = ParseSelect(depth + 1);
    break;

  case TOKEN_FOR_ID:
    statement = ParseFor(depth + 1);
    break;

  case TOKEN_EACH_ID:
    statement = ParseEach(false, depth + 1);
    break;

  case TOKEN_REVERSE_ID:
    statement = ParseEach(true, depth + 1);
    break;

  case TOKEN_CRITICAL_ID:
    statement = ParseCritical(depth + 1);
    break;

  default:
    break;
  }

  return statement;
}

/****************************
 * Whether a JSON literal starts here: 'json' and then '{'. A name followed by
 * '{' starts nothing else in an expression, so 'json' is a keyword only in
 * this position and stays an ordinary name everywhere else.
 ****************************/
PARSER_NO_INLINE bool Parser::IsJsonLiteralStart()
{
  return Match(TOKEN_IDENT) && Match(TOKEN_OPEN_BRACE, SECOND_INDEX) && scanner->GetToken()->GetIdentifier() == L"json";
}

/****************************
 * 'Data.JSON.JsonElement->New(Data.JSON.JsonElement->JsonType-><type>)': an
 * empty object or array, or null
 ****************************/
Expression* Parser::MakeJsonElement(const std::wstring& json_type, int line_num, int line_pos)
{
  const std::wstring file_name = GetFileName();
  ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
  args->AddExpression(TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos,
                                                              L"Data.JSON.JsonElement#JsonType", json_type));
  return TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos, NEW_INST_CALL,
                                                 L"Data.JSON.JsonElement", args);
}

/****************************
 * The start of a JSON object or array: a hidden local holding a new element,
 * declared where the literal is evaluated. The members follow as calls on it.
 ****************************/
SequenceExpression* Parser::MakeJsonContainer(const std::wstring& json_type, std::wstring& ident, int line_num, int line_pos)
{
  const std::wstring file_name = GetFileName();
  SymbolEntry* entry = AddHiddenLocal(L"json", ident, line_num, line_pos);
  SequenceExpression* sequence = TreeFactory::Instance()->MakeSequenceExpression(file_name, line_num, line_pos,
                                                                              TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident));
  if(entry) {
    Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident);
    Assignment* assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, target,
                                                                 MakeJsonElement(json_type, line_num, line_pos));
    sequence->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, entry, assign));
  }

  return sequence;
}

/****************************
 * Parses a JSON object, from '{': '{ key: value, ... }'. It is a JsonElement
 * of type OBJECT and an 'Insert(key, value)' on it for each member, in the
 * order written. A key is any String expression; a value is a nested object
 * or array, or any expression JsonElement has an Insert for (a String, Int,
 * Float, Bool or JsonElement), and Nil is JSON's null.
 ****************************/
PARSER_NO_INLINE Expression* Parser::ParseJsonObject(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

#ifdef _DEBUG
  Debug(L"JSON object", depth);
#endif

  std::wstring ident;
  SequenceExpression* sequence = MakeJsonContainer(L"OBJECT", ident, line_num, line_pos);
  NextToken();

  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    const size_t consumed = tokens_consumed;
    const int member_line_num = GetLineNumber();
    const int member_line_pos = GetLinePosition();

    Expression* key = ParseExpression(depth + 1);
    if(!Match(TOKEN_COLON)) {
      ProcessError(L"Expected ':' after the name of a JSON member: '\"name\": value'");
    }
    else {
      NextToken();
    }

    Expression* value = ParseJsonValue(depth + 1);
    if(key && value) {
      ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
      args->AddExpression(key);
      args->AddExpression(value);
      sequence->AddStatement(TreeFactory::Instance()->MakeMethodCall(file_name, member_line_num, member_line_pos, member_line_num, member_line_pos,
                                                                     GetLineNumber(), GetLinePosition(), ident, L"Insert", args));
    }

    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
    else if(!Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected ',' or '}' after a JSON member");
    }

    // a member that read nothing would be read again from the same token
    if(tokens_consumed == consumed) {
      NextToken();
    }
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  return sequence;
}

/****************************
 * Parses a JSON array, from '[': '[ value, ... ]'. It is a JsonElement of type
 * ARRAY and an 'Add(value)' on it for each element, in the order written.
 ****************************/
Expression* Parser::ParseJsonArray(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

#ifdef _DEBUG
  Debug(L"JSON array", depth);
#endif

  std::wstring ident;
  SequenceExpression* sequence = MakeJsonContainer(L"ARRAY", ident, line_num, line_pos);
  NextToken();

  while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
    const size_t consumed = tokens_consumed;
    const int element_line_num = GetLineNumber();
    const int element_line_pos = GetLinePosition();

    Expression* value = ParseJsonValue(depth + 1);
    if(value) {
      ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
      args->AddExpression(value);
      sequence->AddStatement(TreeFactory::Instance()->MakeMethodCall(file_name, element_line_num, element_line_pos, element_line_num, element_line_pos,
                                                                     GetLineNumber(), GetLinePosition(), ident, L"Add", args));
    }

    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
    else if(!Match(TOKEN_CLOSED_BRACKET)) {
      ProcessError(L"Expected ',' or ']' after a JSON array element");
    }

    if(tokens_consumed == consumed) {
      NextToken();
    }
  }

  if(!Match(TOKEN_CLOSED_BRACKET)) {
    ProcessError(L"Expected ']'", TOKEN_CLOSED_BRACKET);
  }
  NextToken();

  return sequence;
}

/****************************
 * Parses a value in a JSON literal: a nested object or array, Nil (null), or
 * an expression whose value is inserted as it is
 ****************************/
Expression* Parser::ParseJsonValue(int depth)
{
  CheckDepth(depth);
  if(Match(TOKEN_OPEN_BRACE)) {
    return ParseJsonObject(depth + 1);
  }

  if(Match(TOKEN_OPEN_BRACKET)) {
    return ParseJsonArray(depth + 1);
  }

  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  Expression* value = ParseExpression(depth + 1);
  if(value && value->GetExpressionType() == NIL_LIT_EXPR && !value->GetCastType() && !value->GetMethodCall()) {
    return MakeJsonElement(L"NULL", line_num, line_pos);
  }

  return value;
}

/****************************
 * Parses a map literal, from '{': '{ key: value, ... }'. It is a map
 * constructed into a hidden local where the literal is evaluated, and an
 * 'Insert(key, value)' on it for each entry, in the order written. Which map
 * (Map, Hash, ...) and of what, the parser cannot know: the constructor is
 * left as MAP_LITERAL_MARKER and the analyzer names it from the type the
 * literal is assigned to or returned as (ContextAnalyzer::TypeMapLiteral).
 ****************************/
PARSER_NO_INLINE Expression* Parser::ParseMapLiteral(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

#ifdef _DEBUG
  Debug(L"Map literal", depth);
#endif

  std::wstring ident;
  SymbolEntry* entry = AddHiddenLocal(L"map", ident, line_num, line_pos);
  SequenceExpression* sequence = TreeFactory::Instance()->MakeSequenceExpression(file_name, line_num, line_pos,
                                                                              TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident));
  if(entry) {
    Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident);
    MethodCall* constructor = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos, NEW_INST_CALL,
                                                                      MAP_LITERAL_MARKER, TreeFactory::Instance()->MakeExpressionList());
    Assignment* assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, target, constructor);
    sequence->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, entry, assign));
  }
  NextToken();

  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    const size_t consumed = tokens_consumed;
    const int entry_line_num = GetLineNumber();
    const int entry_line_pos = GetLinePosition();

    Expression* key = ParseExpression(depth + 1);
    if(!Match(TOKEN_COLON)) {
      ProcessError(L"Expected ':' after a key of a map literal: '{key: value, ...}'");
    }
    else {
      NextToken();
    }

    Expression* value = ParseExpression(depth + 1);
    if(key && value) {
      ExpressionList* args = TreeFactory::Instance()->MakeExpressionList();
      args->AddExpression(key);
      args->AddExpression(value);
      sequence->AddStatement(TreeFactory::Instance()->MakeMethodCall(file_name, entry_line_num, entry_line_pos, entry_line_num, entry_line_pos,
                                                                     GetLineNumber(), GetLinePosition(), ident, L"Insert", args));
    }

    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
    else if(!Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected ',' or '}' after an entry of a map literal");
    }

    // an entry that read nothing would be read again from the same token
    if(tokens_consumed == consumed) {
      NextToken();
    }
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  return sequence;
}

/****************************
 * Parses a static array
 ****************************/
StaticArray* Parser::ParseStaticArray(int depth) {
  // each '[' of '[[[[...' is a call of this function
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Static Array", depth);
#endif

  NextToken();
  ExpressionList* expressions = TreeFactory::Instance()->MakeExpressionList();

  // array dimension
  if(Match(TOKEN_OPEN_BRACKET)) {
    while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
      expressions->AddExpression(ParseStaticArray(depth + 1));

      // the comma between rows belongs to the outer array
      if(Match(TOKEN_COMMA)) {
        NextToken();
      }
    }

    if(!Match(TOKEN_CLOSED_BRACKET)) {
      ProcessError(L"Expected ']'", TOKEN_CLOSED_BRACKET);
    }
    NextToken();
  }
  // array elements
  else {
    while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
      Expression* expression = nullptr;
      // A literal element is one a ',' or ']' follows; anything else -- a
      // variable, '2 * x', a call -- is an expression, and the array is built
      // when it is evaluated (#1245).
      const bool literal_end = Match(TOKEN_COMMA, SECOND_INDEX) || Match(TOKEN_CLOSED_BRACKET, SECOND_INDEX);
      const bool negative_literal = Match(TOKEN_SUB) && (Match(TOKEN_INT_LIT, SECOND_INDEX) || Match(TOKEN_FLOAT_LIT, SECOND_INDEX)) &&
        (Match(TOKEN_COMMA, THIRD_INDEX) || Match(TOKEN_CLOSED_BRACKET, THIRD_INDEX));
      bool is_literal = negative_literal;
      if(!Match(TOKEN_SUB) && literal_end) {
        switch(GetToken()) {
        case TOKEN_INT_LIT:
        case TOKEN_TRUE_ID:
        case TOKEN_FALSE_ID:
        case TOKEN_FLOAT_LIT:
        case TOKEN_CHAR_LIT:
        case TOKEN_CHAR_STRING_LIT:
        case TOKEN_BAD_CHAR_STRING_LIT:
          is_literal = true;
          break;

        default:
          break;
        }
      }

      if(!is_literal) {
        expression = ParseExpression(depth + 1);
      }
      else if(Match(TOKEN_SUB)) {
        NextToken();

        switch(GetToken()) {
        case TOKEN_INT_LIT:
          expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos,
                                                                   NegateIntLiteral(scanner->GetToken()->GetInt64Lit()));
          NextToken();
          break;

        case TOKEN_FLOAT_LIT:
          expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos,
                                                                 -scanner->GetToken()->GetFloatLit());
          NextToken();
          break;

        default:
          ProcessError(L"Expected literal expression", TOKEN_SEMI_COLON);
          break;
        }
      }
      else {
        switch(GetToken()) {
        case TOKEN_INT_LIT:
          expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos,
                                                                   scanner->GetToken()->GetInt64Lit());
          NextToken();
          break;

        case TOKEN_TRUE_ID:
          expression = TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, true);
          NextToken();
          break;

        case TOKEN_FALSE_ID:
          expression = TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, false);
          NextToken();
          break;

        case TOKEN_FLOAT_LIT:
          expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos,
                                                                 scanner->GetToken()->GetFloatLit());
          NextToken();
          break;

        case TOKEN_CHAR_LIT:
          expression = TreeFactory::Instance()->MakeCharacterLiteral(file_name, line_num, line_pos,
                                                                     scanner->GetToken()->GetCharLit());
          NextToken();
          break;

        case TOKEN_CHAR_STRING_LIT: {
          const std::wstring ident = scanner->GetToken()->GetIdentifier();
          const bool is_lit = scanner->GetToken()->GetByteLit();
          if(!is_lit && HasUnclosedInterpolation(ident)) {
            ProcessError(L"A quote inside '{$...}' ends the string early, leaving the interpolation open; compute the value into a local first and interpolate the local", TOKEN_SEMI_COLON);
            break;
          }
          expression = TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, ident, is_lit);
          NextToken();
        }
                                    break;

        case TOKEN_BAD_CHAR_STRING_LIT:
          if(HasLambdaInInterpolation(scanner->GetToken()->GetIdentifier())) {
            ProcessTokenError(L"Lambdas are not allowed inside string interpolation; assign the lambda to a local first"
                              L"\n\tIn a string a '\\' starts an escape sequence, so a lambda's '\\(' cannot be read inside '{$...}'."
                              L" Assign the lambda to a local variable first, then use that variable in the interpolation", TOKEN_SEMI_COLON);
          }
          else {
            ProcessError(L"Invalid escaped std::string literal", TOKEN_SEMI_COLON);
          }
          NextToken();
          break;

        default:
          ProcessError(L"Expected literal expression", TOKEN_SEMI_COLON);
          break;
        }
      }
      // add expression
      expressions->AddExpression(expression);

      // next expression
      if(Match(TOKEN_COMMA)) {
        NextToken();
      }
      else if(!Match(TOKEN_CLOSED_BRACKET)) {
        ProcessError(L"Expected ';' or ']'", TOKEN_SEMI_COLON);
        NextToken();
      }
    }

    if(!Match(TOKEN_CLOSED_BRACKET)) {
      ProcessError(L"Expected ']'", TOKEN_CLOSED_BRACKET);
    }
    NextToken();
    // A comma after the ']' is not this literal's: the row loop above takes
    // it between rows. Consuming it here also ate the separator after an
    // outermost literal, so F([1, 2, 3], -1) parsed as [1, 2, 3] - 1 (#1184).
  }

  return TreeFactory::Instance()->MakeStaticArray(file_name, line_num, line_pos, expressions);
}

/****************************
 * Parses a variable.
 ****************************/
Variable* Parser::ParseVariable(IdentifierContext& context, int depth)
{
  CheckDepth(depth);
  const int line_num = context.GetLineNumber();
  const int line_pos = context.GetLinePosition();
  const std::wstring ident = context.GetIdentifier();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Variable", depth);
#endif

  Variable* variable = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident);
  if(Match(TOKEN_LES) && Match(TOKEN_IDENT, SECOND_INDEX) && (Match(TOKEN_LES, THIRD_INDEX) || Match(TOKEN_GTR, THIRD_INDEX) ||
                                                              Match(TOKEN_COMMA, THIRD_INDEX) || Match(TOKEN_PERIOD, THIRD_INDEX))) {
    std::vector<Type*> generic_dclrs = ParseGenericTypes(depth);
    variable->SetConcreteTypes(generic_dclrs);
  }
  variable->SetIndices(ParseIndices(depth + 1));
  // 'rows[i][j]': a jagged array's element (#1246)
  while(variable->GetIndices() && Match(TOKEN_OPEN_BRACKET)) {
    variable->AddChainedIndices(ParseIndices(depth + 1));
  }

  return variable;
}

/****************************
 * Parses generic types
 ****************************/
std::vector<Type*> Parser::ParseGenericTypes(int depth)
{
  CheckDepth(depth);
  std::vector<Type*> generic_types;

  if(Match(TOKEN_LES)) {
    NextToken();

    while(!Match(TOKEN_GTR) && !Match(TOKEN_END_OF_STREAM)) {
      Type* type = ParseType(depth + 1);
      if(type) {
        if(type->GetType() != CLASS_TYPE) {
          ProcessError(L"Generic cannot be a basic type");
          return generic_types;
        }
        // A type argument is bound, checked against its bounds and erased as a
        // class; an array is none. 'Vector->New()<String[]>' was accepted here
        // and every AddBack(String[]) then failed as an undefined call.
        else if(type->GetDimension() > 0) {
          ProcessError(L"A type argument cannot be an array type ('" + type->GetName() +
                       L"[]'); hold the array in a class, or use a jagged array");
          return generic_types;
        }
        else {
          generic_types.push_back(type);

          if(pending_generic_closes > 0) {
            pending_generic_closes--;
            return generic_types;
          }
        }
      }
      else {
        return generic_types;
      }

      if(Match(TOKEN_COMMA) && !Match(TOKEN_GTR, SECOND_INDEX)) {
        NextToken();
      }
      else if(!Match(TOKEN_GTR)) {
        if(pending_generic_closes > 0) {
          pending_generic_closes--;
        }
        else if(Match(TOKEN_SHR)) {
          pending_generic_closes = 1;
          NextToken();
        }
        else if(Match(TOKEN_SHR_UNSIGNED)) {
          // three generics closed by one token, so two '>' remain owed
          pending_generic_closes = 2;
          NextToken();
        }
        else {
          pending_generic_closes = 0;
          ProcessError(L"Expected ',' or '>'");
        }

        return generic_types;
      }
    }

    NextToken();
  }

  return generic_types;
}

/****************************
 * Parses generic classes
 ****************************/
std::vector<Class*> Parser::ParseGenericClasses(const std::wstring &bundle_name, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  std::vector<Class*> generic_classes;

  if(Match(TOKEN_LES)) {
    NextToken();

    while(!Match(TOKEN_GTR) && !Match(TOKEN_END_OF_STREAM)) {
      // optional declaration-site variance preceding the parameter name:
      //   'in T'  -> contravariant   'out T' -> covariant
      // 'in' is a keyword (TOKEN_IN_ID); 'out' is matched contextually (an
      // identifier followed by the parameter identifier) so it stays a usable name.
      GenericVariance variance = GENERIC_INVARIANT;
      if(Match(TOKEN_IN_ID) && Match(TOKEN_IDENT, SECOND_INDEX)) {
        variance = GENERIC_CONTRAVARIANT;
        NextToken();
      }
      else if(Match(TOKEN_IDENT) && scanner->GetToken()->GetIdentifier() == L"out" && Match(TOKEN_IDENT, SECOND_INDEX)) {
        variance = GENERIC_COVARIANT;
        NextToken();
      }

      // identifier
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
      std::wstring generic_klass = scanner->GetToken()->GetIdentifier();
      NextToken();

      Class* klass = TreeFactory::Instance()->MakeClass(file_name, line_num, line_pos, -1, -1, generic_klass, true);
      klass->SetVariance(variance);
      for(size_t i = 0; i < generic_classes.size(); ++i) {
        if(bundle_name.size() > 0) {
          generic_klass.insert(0, L".");
          generic_klass.insert(0, bundle_name);
        }
      }
      generic_classes.push_back(klass);

      if(Match(TOKEN_COLON)) {
        NextToken();

        if(!Match(TOKEN_IDENT)) {
          ProcessError(TOKEN_IDENT);
        }
        const std::wstring interface_name = scanner->GetToken()->GetIdentifier();
        klass->SetGenericInterface(interface_name);
        NextToken();
        // F-bounded constraint (T : Compare<T>): accept and consume the bound's
        // type arguments. They are intentionally NOT retained — the constraint is
        // enforced against the bound's raw interface (consistent with erasure), and
        // keeping a self-referential argument would recurse during type resolution.
        if(Match(TOKEN_LES)) {
          ParseGenericTypes(depth + 1);
        }

        // additional bounds for compound constraints: T : A & B & ...
        while(Match(TOKEN_AND)) {
          NextToken();
          if(!Match(TOKEN_IDENT)) {
            ProcessError(TOKEN_IDENT);
          }
          const std::wstring extra_interface_name = scanner->GetToken()->GetIdentifier();
          klass->AddGenericInterface(extra_interface_name);
          NextToken();
          if(Match(TOKEN_LES)) {
            ParseGenericTypes(depth + 1);  // F-bounded extra: consume, do not retain
          }
        }

        // If an F-bound ended in '>>' (e.g. <T : Compare<T>>), ParseGenericTypes
        // consumed the shift-right token and the second '>' closes THIS generic
        // parameter list. Terminate here — the closer is already consumed, so we
        // must not fall through to the trailing NextToken below.
        if(pending_generic_closes > 0) {
          pending_generic_closes--;
          klass->SetEndLineNumber(GetLineNumber());
          klass->SetEndLinePosition(GetLinePosition());
          return generic_classes;
        }
      }

      if(Match(TOKEN_LES)) {
        std::vector<Class*> generic_klasss = ParseGenericClasses(bundle_name, depth + 1);
        klass->SetGenericClasses(generic_klasss);
      }

      if(Match(TOKEN_COMMA) && !Match(TOKEN_GTR, SECOND_INDEX)) {
        NextToken();
      }
      else if(!Match(TOKEN_GTR)) {
        if(pending_generic_closes > 0) {
          pending_generic_closes--;
        }
        else if(Match(TOKEN_SHR)) {
          pending_generic_closes = 1;
          NextToken();
        }
        else if(Match(TOKEN_SHR_UNSIGNED)) {
          // three generics closed by one token, so two '>' remain owed
          pending_generic_closes = 2;
          NextToken();
        }
        else {
          pending_generic_closes = 0;
          ProcessError(L"Expected ',' or '>'");
        }
      }

      klass->SetEndLineNumber(GetLineNumber());
      klass->SetEndLinePosition(GetLinePosition());
    }

    NextToken();
  }

  return generic_classes;
}

/****************************
 * Parses a declaration.
 ****************************/
Declaration* Parser::ParseDeclaration(IdentifierContext& context, bool is_stmt, int depth)
{
  CheckDepth(depth);
  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Declaration", depth);
#endif

  std::vector<IdentifierContext> idents;
  idents.push_back(context);

  // parse additional names
  if(is_stmt) {
    while(Match(TOKEN_COMMA)) {
      NextToken();

      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }

      const std::wstring ident = scanner->GetToken()->GetIdentifier();
      line_num = GetLineNumber();
      line_pos = GetLinePosition();

      IdentifierContext add_context(ident, line_num, line_pos);
      idents.push_back(add_context);

      NextToken();
    }
  }

  if(!Match(TOKEN_COLON)) {
    ProcessError(TOKEN_COLON);
  }
  NextToken();

  Declaration* declaration = nullptr;
  if(Match(TOKEN_OPEN_PAREN)) {
    // type
    Type* type = ParseType(depth + 1);

    // add declarations
    for(size_t i = 0; i < idents.size(); ++i) {
      IdentifierContext add_context = idents[i];
      declaration = AddDeclaration(add_context, type, false, declaration, add_context.GetLineNumber(), add_context.GetLinePosition(), depth);
    }
  }
  else {
    // static
    bool is_static = false;
    if(Match(TOKEN_STATIC_ID)) {
      is_static = true;
      NextToken();

      if(!Match(TOKEN_COLON)) {
        ProcessError(TOKEN_COLON);
      }
      NextToken();
    }

    // accessors: `@x : get : T` or `@x : get set : T`. 'get' and 'set' are
    // contextual: a modifier only when the word is followed by ':' (or by 'set'
    // and ':'), so a type that happens to be named get is still a type.
    bool want_get = false;
    bool want_set = false;
    if(Match(TOKEN_IDENT) && scanner->GetToken()->GetIdentifier() == L"get" &&
       (Match(TOKEN_COLON, SECOND_INDEX) ||
        (Match(TOKEN_IDENT, SECOND_INDEX) && scanner->GetToken(SECOND_INDEX)->GetIdentifier() == L"set" && Match(TOKEN_COLON, THIRD_INDEX)))) {
      if(!is_stmt || current_method) {
        ProcessError(L"'get' and 'set' apply to a field of a class, not to a local variable");
      }
      if(is_static) {
        ProcessError(L"'get' and 'set' apply to an instance field, not a static one");
      }
      want_get = true;
      NextToken();
      if(Match(TOKEN_IDENT)) {
        want_set = true;
        NextToken();
      }
      if(!Match(TOKEN_COLON)) {
        ProcessError(TOKEN_COLON);
      }
      NextToken();
    }

    // type
    Type* type = ParseType(depth + 1);

    // add declarations
    Assignment* temp = nullptr;
    for(size_t i = 0; i < idents.size(); ++i) {
      IdentifierContext add_context = idents[i];
      declaration = AddDeclaration(add_context, type, is_static, declaration, add_context.GetLineNumber(), add_context.GetLinePosition(), depth);

      // found assignment
      if(declaration->GetAssignment()) {
        temp = declaration->GetAssignment();
      }

      // apply assignment statement to other variables
      if(temp && !declaration->GetAssignment()) {
        Variable* left = ParseVariable(add_context, depth + 1);
        Assignment* assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), left, temp->GetExpression());
        declaration->SetAssignment(assignment);
      }
      if(want_get) {
        declaration->SetAccessors(want_get, want_set);
      }
    }
  }

  return declaration;
}

/****************************
 * Adds a declaration.
 ****************************/
frontend::Declaration* Parser::AddDeclaration(IdentifierContext& context, Type* type, bool is_static, Declaration* child,
                                              const int line_num, const int line_pos, int depth)
{
  const std::wstring file_name = GetFileName();
  const std::wstring ident = context.GetIdentifier();

  // add entry
  const std::wstring scope_name = GetScopeName(ident);
  SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, scope_name, 
                                                                type, is_static, current_method != nullptr);

#ifdef _DEBUG
  Debug(L"Adding variable: '" + scope_name + L"'", depth + 2);
#endif

  bool was_added = symbol_table->CurrentParseScope()->AddEntry(entry);
  if(!was_added) {
    ProcessError(L"Variable already defined in this scope: '" + ident + L"'");
  }

  Declaration* declaration;
  if(Match(TOKEN_ASSIGN)) {
    Variable* variable = ParseVariable(context, depth + 1);
    Assignment* asgn = ParseAssignment(variable, depth + 1);
    declaration = TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), entry, child, asgn);
  }
  else {
    declaration = TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), entry, child);
  }

  return declaration;
}

/****************************
 * True when the class being parsed has already declared the field `name` ('@x')
 ****************************/
bool Parser::ClassDeclaresField(const std::wstring& name)
{
  if(!current_class) {
    return false;
  }

  const std::wstring suffix = L":" + name;
  std::vector<Statement*> statements = current_class->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    if(statements[i] && statements[i]->GetStatementType() == DECLARATION_STMT) {
      for(Declaration* declaration = static_cast<Declaration*>(statements[i]); declaration; declaration = declaration->GetChild()) {
        SymbolEntry* entry = declaration->GetEntry();
        if(entry && !entry->IsStatic()) {
          const std::wstring& full = entry->GetName();
          if(full.size() >= suffix.size() && full.compare(full.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

/****************************
 * Parses a declaration std::list.
 ****************************/
DeclarationList* Parser::ParseDecelerationList(int depth)
{
  CheckDepth(depth);
#ifdef _DEBUG
  Debug(L"Declaration Parameters", depth);
#endif

  // a placeholder in the class written for an 'agent' declaration: the user's own parameter list is read in its place
  if(IsAgentSplice()) {
    DeclarationList* spliced = nullptr;
    ParseAgentSplice(&spliced, depth);
    return spliced;
  }

  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  DeclarationList* declarations = TreeFactory::Instance()->MakeDeclarationList();
  bool abandoned = false;
  while(!Match(TOKEN_CLOSED_PAREN) && !Match(TOKEN_END_OF_STREAM)) {
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    // 'ref name : Type' (#1243): a contextual word, so a parameter may still be named 'ref'
    bool is_reference = false;
    if(scanner->GetToken()->GetIdentifier() == L"ref" && Match(TOKEN_IDENT, SECOND_INDEX) && Match(TOKEN_COLON, THIRD_INDEX)) {
      is_reference = true;
      NextToken();
    }

    // identifier
    std::wstring ident = scanner->GetToken()->GetIdentifier();
    const int line_num = GetLineNumber();
    const int line_pos = GetLinePosition();

    // Field promotion: in a constructor, a parameter written '@name' where the class
    // already declares the field '@name' fills that field. The parameter is 'name'
    // (so it is also the argument's name for a named call), and the body starts with
    // '@name := name'. Before this the parameter shadowed the field, which stayed 0.
    if(ident.size() > 1 && ident[0] == L'@' && current_method &&
       (current_method->GetMethodType() == NEW_PUBLIC_METHOD || current_method->GetMethodType() == NEW_PRIVATE_METHOD) &&
       ClassDeclaresField(ident)) {
      promoted_fields.push_back(ident.substr(1));
      ident = ident.substr(1);
    }

    IdentifierContext ident_context(ident, line_num, line_pos);
    NextToken();

    Declaration* declaration = ParseDeclaration(ident_context, false, depth + 1);
    if(is_reference && declaration && declaration->GetEntry()) {
      Type* type = declaration->GetEntry()->GetType();
      if(type->GetDimension() > 0 || type->GetType() == FUNC_TYPE) {
        ProcessError(L"A 'ref' parameter takes a single value, not an array or a function");
      }
      else if(declaration->GetAssignment()) {
        ProcessError(L"A 'ref' parameter cannot have a default value");
      }
      else {
        // the caller passes a one-element cell; the name is its element
        type->SetDimension(1);
        declaration->GetEntry()->SetReference(true);
      }
    }
    declarations->AddDeclaration(declaration);

    if(Match(TOKEN_COMMA)) {
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
    }
    else if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ',' or ')'");
      // a missing ',' leaves the next parameter starting here; anything else is skipped
      // to where the list ends or the method's body begins, not on through the body
      if(!Match(TOKEN_IDENT)) {
        while(!Match(TOKEN_CLOSED_PAREN) && !Match(TOKEN_TILDE) && !Match(TOKEN_OPEN_BRACE) && !Match(TOKEN_CLOSED_BRACE) &&
              !Match(TOKEN_END_OF_STREAM) && !IsDeclarationStart(GetToken())) {
          NextToken();
        }
        if(!Match(TOKEN_CLOSED_PAREN)) {
          abandoned = true;
          break;
        }
      }
    }
  }
  if(abandoned) {
    return declarations;
  }
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();

  return declarations;
}

/****************************
 * Parses a expression std::list.
 ****************************/
ExpressionList* Parser::ParseExpressionList(int &end_pos, int depth, ScannerTokenType open, ScannerTokenType closed, bool allow_named)
{
  CheckDepth(depth);
#ifdef _DEBUG
  Debug(L"Calling Parameters", depth);
#endif

  ExpressionList* expressions = TreeFactory::Instance()->MakeExpressionList();
  // where the list opens, so a missing close can name it
  const int open_line = GetLineNumber();
  const int open_pos = GetLinePosition();
  bool list_broken = false;
  if(!Match(open)) {
    ProcessError(open);
  }
  NextToken();

  std::set<std::wstring> arg_names;
  while(!Match(closed) && !Match(TOKEN_END_OF_STREAM)) {
    // a named argument, 'name: value' (#1244): an argument never starts with an
    // identifier and a colon otherwise -- a ternary's ':' follows its '?'
    std::wstring arg_name;
    if(allow_named && Match(TOKEN_IDENT) && Match(TOKEN_COLON, SECOND_INDEX)) {
      arg_name = scanner->GetToken()->GetIdentifier();
      NextToken();
      NextToken();
      if(arg_name.empty() || arg_name[0] == L'@' || arg_name.find(L'.') != std::wstring::npos) {
        ProcessError(L"Expected a parameter name before ':'");
      }
      else if(!arg_names.insert(arg_name).second) {
        ProcessError(L"Parameter '" + arg_name + L"' is named more than once");
      }
    }
    else if(!arg_names.empty()) {
      ProcessError(L"Positional arguments must come before named ones");
    }

    // 'ref x' (#1243): the variable goes in a one-element cell for the call and is
    // set from it after the statement -- copy in, copy out
    bool is_reference = false;
    if(allow_named && Match(TOKEN_IDENT) && Match(TOKEN_IDENT, SECOND_INDEX) && scanner->GetToken()->GetIdentifier() == L"ref") {
      is_reference = true;
      NextToken();
    }

    // expression
    Expression* expression = ParseExpression(depth + 1);
    if(expression && is_reference) {
      expression = ParseReferenceArgument(expression);
    }
    if(expression) {
      if(arg_name.empty()) {
        expressions->AddExpression(expression);
      }
      else {
        expressions->AddNamedExpression(expression, arg_name);
      }

      if(Match(TOKEN_COMMA) && !Match(closed, SECOND_INDEX)) {
        NextToken();
      }
      else if((expression->GetExpressionType() == LAMBDA_EXPR && Match(TOKEN_SEMI_COLON))) {
        NextToken();
        break;
      }
      // Neither ',' nor the closing token follows the argument, so the list is
      // broken here. Reading on for another argument only produces errors that
      // are the same mistake again.
      else if(!Match(closed) && !lexical_recovery) {
        const std::wstring closing = closed == TOKEN_CLOSED_BRACKET ? L"']'" : L"')'";
        const std::wstring opening = open == TOKEN_OPEN_BRACKET ? L"'['" : L"'('";
        const std::wstring found = scanner->DescribeToken(scanner->GetToken());
        ProcessError(L"Expected ',' or " + closing + L" after this argument" + (found.empty() ? L"" : L", found " + found) +
                     L"\n\tThe " + opening + L" opened at line " + std::to_wstring(open_line) + L", column " + std::to_wstring(open_pos) +
                     L" is not closed:" + scanner->Excerpt(open_line, open_pos) +
                     L"\n\tSeparate arguments with ',' and close the list with " + closing);
        list_broken = true;
        break;
      }
    }
    else {
      NextToken();
    }
  }

  if(!list_broken && !Match(closed)) {
    ProcessError(closed);
  }
  end_pos = GetLinePosition() + 1;

  // when the list is broken the current token belongs to the statement that
  // follows (usually its ';'), so it is left for that statement
  if(!list_broken) {
    NextToken();
  }

  return expressions;
}

/****************************
 * Parses a call's arguments,
 * which may be named; records
 * the shape of a named call so
 * its forwarder can be generated
 * (#1244)
 ****************************/
ExpressionList* Parser::ParseCallArguments(int &end_pos, int depth, const std::wstring &method_name)
{
  CheckDepth(depth);
  ExpressionList* expressions = ParseExpressionList(end_pos, depth, TOKEN_OPEN_PAREN, TOKEN_CLOSED_PAREN, true);
  if(expressions->HasNames() && program) {
    program->AddNamedCallShape(method_name, expressions->GetNames());
  }
  return expressions;
}

/****************************
 * Parses array indices.
 ****************************/
ExpressionList* Parser::ParseIndices(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  ExpressionList* expressions = nullptr;
  if(Match(TOKEN_OPEN_BRACKET)) {
    expressions = TreeFactory::Instance()->MakeExpressionList();
    NextToken();

    if(Match(TOKEN_CLOSED_BRACKET)) {
      expressions->AddExpression(TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos -1, L"#"));
      NextToken();
    }
    else {
      if(Match(TOKEN_COMMA)) {
        expressions->AddExpression(TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos - 1, L"#"));
        while(Match(TOKEN_COMMA)) {
          expressions->AddExpression(TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos - 1, L"#"));
          NextToken();
        }
        if(!Match(TOKEN_CLOSED_BRACKET)) {
          ProcessError(L"Expected ',' or ']'", TOKEN_SEMI_COLON);
          NextToken();
        }
        NextToken();
      }
      else {
        while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
          // expression
          expressions->AddExpression(ParseExpression(depth + 1));

          if(Match(TOKEN_COMMA)) {
            NextToken();
          }
          else if(!Match(TOKEN_CLOSED_BRACKET)) {
            ProcessError(L"Expected ',' or ']'", TOKEN_SEMI_COLON);
            NextToken();
          }
        }

        if(!Match(TOKEN_CLOSED_BRACKET)) {
          ProcessError(L"Expected ']'", TOKEN_CLOSED_BRACKET);
        }
        NextToken();
      }
    }
  }

  return expressions;
}

/****************************
 * Parses an expression.
 ****************************/
Expression* Parser::ParseExpression(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Expression", depth);
#endif

  Expression* expression = nullptr;
  if(Match(TOKEN_NEQL) || (alt_syntax && Match(TOKEN_NOT))) {
    expression = ParseLogic(depth + 1);
  }
  else {
    expression = ParseLogic(depth + 1);
    //
    // parses '??', the nil-coalesce operator
    //
    // Desugars to the existing Otherwise() intrinsic, which the context
    // analyzer recognizes by method name and arity and which already emits its
    // default inside the nil branch -- so short-circuiting comes for free and
    // no new opcode is needed. Handled before the ternary below, so
    // 'a ?? b ? c : d' groups as '(a ?? b) ? c : d'.
    //
    while(Match(TOKEN_QUESTION_QUESTION)) {
#ifdef _DEBUG
      Debug(L"Nil coalesce", depth);
#endif
      NextToken();

      // 'E ?? return V', 'E ?? break', 'E ?? continue', 'E ?? { ...; return }': a statement
      // leaves when E is Nil. The expression stays E; the assignment that owns it adds
      // 'if(x = Nil) { ... }' after itself.
      //
      // A '{' whose first token is a literal followed by ':' is a map literal ('m ?? {"a": 1}'), which
      // no statement of a block starts with: it is the default value, and is typed (or refused) as a
      // map literal is anywhere inside an expression.
      const bool map_literal_default = Match(TOKEN_OPEN_BRACE) && Match(TOKEN_COLON, THIRD_INDEX) &&
        (Match(TOKEN_CHAR_STRING_LIT, SECOND_INDEX) || Match(TOKEN_INT_LIT, SECOND_INDEX) ||
         Match(TOKEN_CHAR_LIT, SECOND_INDEX) || Match(TOKEN_FLOAT_LIT, SECOND_INDEX));
      if(!map_literal_default &&
         (Match(TOKEN_RETURN_ID) || Match(TOKEN_BREAK_ID) || Match(TOKEN_CONTINUE_ID) || Match(TOKEN_OPEN_BRACE))) {
        if(nil_guard_bail) {
          ProcessError(L"Only one '?? return' (or '?? { }') is allowed in a statement");
        }
        nil_guard_bail = ParseNilGuardBail(depth + 1);
        break;
      }

      Expression* default_expression = ParseLogic(depth + 1);

      ExpressionList* otherwise_args = TreeFactory::Instance()->MakeExpressionList();
      otherwise_args->AddExpression(default_expression);

      // A bare variable receiver takes the Variable* form, the same node
      // 'a->Otherwise(b)' produces; anything else is appended to the end of the
      // existing call chain.
      if(expression->GetExpressionType() == VAR_EXPR && !expression->GetMethodCall()) {
        expression = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                             line_num, line_pos,
                                                             GetLineNumber(), GetLinePosition(),
                                                             static_cast<Variable*>(expression),
                                                             L"Otherwise", otherwise_args);
      }
      else {
        Expression* tail = expression;
        while(tail->GetMethodCall()) {
          tail = tail->GetMethodCall();
        }
        tail->SetMethodCall(TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                                    -1, -1,
                                                                    GetLineNumber(), GetLinePosition(),
                                                                    L"", L"Otherwise", otherwise_args));
      }
    }

    //
    // parses a ternary conditional
    //
    if(Match(TOKEN_QUESTION)) {
#ifdef _DEBUG
      Debug(L"Ternary conditional", depth);
#endif
      NextToken();
      Expression* if_expression = ParseLogic(depth + 1);
      if(!Match(TOKEN_COLON)) {
        ProcessError(L"Expected ':'", TOKEN_COLON);
      }
      NextToken();
      expression = TreeFactory::Instance()->MakeCond(file_name, line_num, line_pos, expression, if_expression, ParseLogic(depth + 1));
    }
  }

  return expression;
}

/****************************
 * Parses a logical expression.
 * This method delegates support
 * for other types of expressions.
 ****************************/
Expression* Parser::ParseLogic(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Boolean logic", depth);
#endif

  Expression* left;
  if(Match(TOKEN_NEQL) || (alt_syntax && Match(TOKEN_NOT))) {
    NextToken();
    CalculatedExpression* not_expr = static_cast<CalculatedExpression*>(TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, NEQL_EXPR));
    not_expr->SetLeft(ParseMathLogic(depth + 1));
    not_expr->SetRight(TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, true));
    left = not_expr;
  }
  else {
    left = ParseMathLogic(depth + 1);
  }

  CalculatedExpression* expression = nullptr;
  while((Match(TOKEN_AND) || Match(TOKEN_OR))) {
    if(expression) {
      left = expression;
    }

    switch(GetToken()) {
    case TOKEN_AND:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, AND_EXPR);
      break;
    case TOKEN_OR:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, OR_EXPR);
      break;

    default:
      break;
    }
    NextToken();

    Expression* right = ParseLogic(depth + 1);
    if(expression) {
      expression->SetLeft(right);
      expression->SetRight(left);
    }
  }

  if(expression) {
    return expression;
  }

  // pass-thru
  return left;
}

/****************************
 * Parses a mathematical expression.
 * This method delegates support
 * for other types of expressions.
 ****************************/
Expression* Parser::ParseMathLogic(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Boolean math", depth);
#endif
  
  Expression* left = ParseTerm(depth + 1);
  if(GetToken() >= TOKEN_LES && GetToken() <= TOKEN_NEQL) {
    CalculatedExpression* expression = nullptr;
    switch(GetToken()) {
    case TOKEN_LES:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, LES_EXPR);
      break;
    case TOKEN_GTR:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, GTR_EXPR);
      break;
    case TOKEN_LEQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, LES_EQL_EXPR);
      break;
    case TOKEN_GEQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, GTR_EQL_EXPR);
      break;
    case TOKEN_EQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, EQL_EXPR);
      break;
    case TOKEN_NEQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, NEQL_EXPR);
      break;

    default:
      break;
    }
    NextToken();

    if(expression) {
      Expression* right = ParseTerm(depth + 1);
      expression->SetLeft(left);
      expression->SetRight(right);
    }

    return expression;
  }

  // pass-thru
  return left;
}

/****************************
 * Parses a mathematical term.
 * This method delegates support
 * for other types of expressions.
 ****************************/
Expression* Parser::ParseTerm(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Term", depth);
#endif

  Expression* left = ParseFactor(depth + 1);
  if(!left) {
    return nullptr;
  }

  if(!Match(TOKEN_ADD) && !Match(TOKEN_SUB)) {
    return left;
  }

  CalculatedExpression* expression = nullptr;
  while((Match(TOKEN_ADD) || Match(TOKEN_SUB))) {
    CheckChain();
    if(expression) {
      CalculatedExpression* right;
      if(Match(TOKEN_ADD)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, ADD_EXPR);
      }
      else {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SUB_EXPR);
      }
      NextToken();

      Expression* temp = ParseFactor(depth + 1);

      right->SetRight(temp);
      right->SetLeft(expression);
      expression = right;
    }
    // first time in loop
    else {
      if(Match(TOKEN_ADD)) {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, ADD_EXPR);
      }
      else {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SUB_EXPR);
      }
      NextToken();

      Expression* temp = ParseFactor(depth + 1);

      if(expression) {
        expression->SetRight(temp);
        expression->SetLeft(left);
      }
    }
  }

  return expression;
}

/****************************
 * Parses a mathematical factor.
 * This method delegates support
 * for other types of expressions.
 ****************************/
Expression* Parser::ParseFactor(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Factor", depth);
#endif

  Expression* left = ParseSimpleExpression(depth + 1);
  if(!IsFactorOperator()) {
    return left;
  }

  // Every operator at this level is left-associative and shares one precedence,
  // so '>>>' -- which is sugar for a call rather than an opcode -- folds into
  // the same chain as the calculated ones instead of binding tighter or looser.
  Expression* expression = left;
  while(IsFactorOperator()) {
    CheckChain();
    if(Match(TOKEN_SHR_UNSIGNED)) {
      NextToken();
      ExpressionList* shift_args = TreeFactory::Instance()->MakeExpressionList();
      shift_args->AddExpression(expression);
      shift_args->AddExpression(ParseSimpleExpression(depth + 1));

      // emitted as the static call 'Int->ShiftRightUnsigned(value, count)' rather
      // than as a call on the value. A method call whose receiver is a plain
      // variable passes that receiver as the LAST argument, while one whose
      // receiver is any other expression passes it first, so a receiver-shaped
      // desugaring would swap the operands for 'v >>> n' but not for '-1 >>> n'.
      expression = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                           line_num, line_pos,
                                                           GetLineNumber(), GetLinePosition(),
                                                           INT_CLASS_ID, L"ShiftRightUnsigned",
                                                           shift_args);
      continue;
    }

    CalculatedExpression* calculated;
    switch(GetToken()) {
    case TOKEN_MUL:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MUL_EXPR);
      break;

    case TOKEN_DIV:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, DIV_EXPR);
      break;

    case TOKEN_MOD:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MOD_EXPR);
      break;

    case TOKEN_SHL:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SHL_EXPR);
      break;

    case TOKEN_SHR:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SHR_EXPR);
      break;

    case TOKEN_AND_ID:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_AND_EXPR);
      break;

    case TOKEN_OR_ID:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_OR_EXPR);
      break;

    case TOKEN_XOR_ID:
      calculated = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_XOR_EXPR);
      break;

    default:
      calculated = nullptr;
      break;
    }
    NextToken();

    Expression* temp = ParseSimpleExpression(depth + 1);
    if(calculated) {
      calculated->SetLeft(expression);
      calculated->SetRight(temp);
      expression = calculated;
    }
  }

  return expression;
}

/****************************
 * The type an expression names, for a function type's parameter list read
 * as expressions: `Int`, `String`, `Int[]`, or a nested `(Int) ~ Int`.
 * Nil when the expression is not a type name.
 ****************************/
Type* Parser::ExpressionToType(Expression* expression)
{
  if(!expression || expression->GetExpressionType() != VAR_EXPR || expression->GetMethodCall()) {
    return nullptr;
  }

  Variable* variable = static_cast<Variable*>(expression);
  const std::wstring name = variable->GetName();
  if(name == FUNC_TYPE_MARKER) {
    return variable->GetEvalType();
  }

  Type* type = nullptr;
  if(name == BOOL_CLASS_ID) {
    type = TypeFactory::Instance()->MakeType(BOOLEAN_TYPE);
  }
  else if(name == BYTE_CLASS_ID) {
    type = TypeFactory::Instance()->MakeType(BYTE_TYPE);
  }
  else if(name == INT_CLASS_ID) {
    type = TypeFactory::Instance()->MakeType(INT_TYPE);
  }
  else if(name == FLOAT_CLASS_ID) {
    type = TypeFactory::Instance()->MakeType(FLOAT_TYPE);
  }
  else if(name == CHAR_CLASS_ID) {
    type = TypeFactory::Instance()->MakeType(CHAR_TYPE);
  }
  else if(name == NIL_CLASS_ID) {
    type = TypeFactory::Instance()->MakeType(NIL_TYPE);
  }
  else {
    type = TypeFactory::Instance()->MakeType(CLASS_TYPE, name);
  }

  // `Int[]` reads as a variable indexed by empty slots
  if(variable->GetIndices()) {
    type->SetDimension((int)variable->GetIndices()->GetExpressions().size());
  }

  return type;
}

/****************************
 * Parses a simple expression.
 ****************************/
Expression* Parser::ParseSimpleExpression(int depth)
{
  CheckDepth(depth);
  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Simple expression", depth);
#endif

  // a placeholder in the class written for an 'agent' declaration: the user's own expression is read in its place
  if(IsAgentSplice()) {
    return ParseAgentSplice(nullptr, depth + 1);
  }

  Expression* expression = nullptr;

  // 'json{ ... }'
  if(IsJsonLiteralStart()) {
    NextToken();
    expression = ParseJsonObject(depth + 1);
  }
  // 'match subject { ... }' and 'select(subject) { ... }' as values
  else if(IsMatchStart()) {
    expression = ParseMatchExpression(depth + 1);
  }
  else if(Match(TOKEN_SELECT_ID)) {
    expression = ParseSelectExpression(depth + 1);
  }
  // 'spawn { ... }' is the started thread
  else if(IsSpawn()) {
    expression = ParseSpawn(depth + 1);
  }
  // 'prompt "..."': a contextual keyword, so 'prompt' stays an ordinary name
  else if(IsPromptLiteral()) {
    expression = ParsePromptLiteral(depth + 1);
  }
  else if(Match(TOKEN_IDENT) || Match(TOKEN_ADD_ADD) || Match(TOKEN_SUB_SUB) || IsBasicType(GetToken())) {
    std::wstring ident;
    bool pre_inc = false;
    bool pre_dec = false;

    switch(GetToken()) {
    case TOKEN_BOOLEAN_ID:
      ident = BOOL_CLASS_ID;
      NextToken();
      break;

    case TOKEN_BYTE_ID:
      ident = BYTE_CLASS_ID;
      NextToken();
      break;

    case TOKEN_INT_ID:
      ident = INT_CLASS_ID;
      NextToken();
      break;

    case TOKEN_FLOAT_ID:
      ident = FLOAT_CLASS_ID;
      NextToken();
      break;

    case TOKEN_CHAR_ID:
      ident = CHAR_CLASS_ID;
      NextToken();
      break;

    case TOKEN_ADD_ADD:
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(L"Expected identifier", TOKEN_SEMI_COLON);
        return nullptr;
      }
      pre_inc = true;
      ident = ParseBundleName();
      break;

    case TOKEN_SUB_SUB:
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(L"Expected identifier", TOKEN_SEMI_COLON);
        return nullptr;
      }
      pre_dec = true;
      ident = ParseBundleName();
      break;

    default:
      ident = ParseBundleName();
      break;
    }

    // 'Int[]->New[n]': n rows of a jagged array, an array of the row type
    // '$Int[]' (#1246)
    if(!pre_inc && !pre_dec && Match(TOKEN_OPEN_BRACKET) && Match(TOKEN_CLOSED_BRACKET, SECOND_INDEX) &&
       (Match(TOKEN_ASSESSOR, THIRD_INDEX) || Match(TOKEN_OPEN_BRACKET, THIRD_INDEX))) {
      std::wstring row_base = ident;
      if(ident == BOOL_CLASS_ID) {
        row_base = L"Bool";
      }
      else if(ident == BYTE_CLASS_ID) {
        row_base = L"Byte";
      }
      else if(ident == INT_CLASS_ID) {
        row_base = L"Int";
      }
      else if(ident == FLOAT_CLASS_ID) {
        row_base = L"Float";
      }
      else if(ident == CHAR_CLASS_ID) {
        row_base = L"Char";
      }

      int levels = 0;
      while(Match(TOKEN_OPEN_BRACKET) && Match(TOKEN_CLOSED_BRACKET, SECOND_INDEX)) {
        NextToken();
        NextToken();
        levels++;
      }
      if(!Match(TOKEN_ASSESSOR) || !Match(TOKEN_NEW_ID, SECOND_INDEX) || !Match(TOKEN_OPEN_BRACKET, THIRD_INDEX)) {
        ProcessError(L"Expected '->New[..]' to allocate the rows of a jagged array", TOKEN_SEMI_COLON);
      }
      ident = RowTypeName(row_base, levels);
    }

    switch(GetToken()) {
      // method call
    case TOKEN_ASSESSOR:
    case TOKEN_OPEN_PAREN:
      // class->enum reference
      if(!Match(TOKEN_AS_ID, SECOND_INDEX) && !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
        if(Match(TOKEN_ASSESSOR) && Match(TOKEN_IDENT, SECOND_INDEX) && Match(TOKEN_ASSESSOR, THIRD_INDEX)) {
          NextToken();
          ident += L'#' + scanner->GetToken()->GetIdentifier();
          NextToken();
        }

        IdentifierContext ident_context(ident, line_num, line_pos);
        expression = ParseMethodCall(ident_context, depth + 1);
      }
      else {
        IdentifierContext ident_context(ident, line_num, line_pos);
        expression = ParseVariable(ident_context, depth + 1);
        ParseCastTypeOf(expression, depth + 1);
      }
      break;

      // variable
    default: {
      IdentifierContext ident_context(ident, line_num, line_pos);
      Variable* variable = ParseVariable(ident_context, depth + 1);

      // pre operation
      if(pre_inc) {
        variable->SetPreStatement(TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable,
                                  TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1), ADD_ASSIGN_STMT));
      }
      else if(pre_dec) {
        variable->SetPreStatement(TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable,
                                  TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1), SUB_ASSIGN_STMT));
      }
      // post operation
      if(Match(TOKEN_ADD_ADD)) {
        NextToken();
        variable->SetPostStatement(TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable,
                                   TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1), ADD_ASSIGN_STMT));
      }
      else if(Match(TOKEN_SUB_SUB)) {
        NextToken();
        variable->SetPostStatement(TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable,
                                   TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1), SUB_ASSIGN_STMT));
      }
      expression = variable;
    }
      break;
    }
  }
  // parentheses expression
  else if(Match(TOKEN_OPEN_PAREN)) {
    NextToken();
    expression = ParseExpression(depth + 1);

    // A function type written as an argument of a function reference:
    // `Ap((Int) ~ Int, Int) ~ Int` (#1183). The scanner sees three tokens
    // ahead, too few to find the '~' first, so the group is read as
    // expressions and turned into parameter types once the '~' shows. The
    // type rides on a marker variable that EncodeFunctionReference encodes.
    std::vector<Expression*> group;
    group.push_back(expression);
    while(Match(TOKEN_COMMA)) {
      NextToken();
      group.push_back(ParseExpression(depth + 1));
    }

    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
    }
    NextToken();

    if(Match(TOKEN_TILDE)) {
      NextToken();
      std::vector<Type*> func_params;
      for(Expression* item : group) {
        Type* param_type = ExpressionToType(item);
        if(!param_type) {
          ProcessError(L"Expected a type in a function type", TOKEN_SEMI_COLON);
          return nullptr;
        }
        func_params.push_back(param_type);
      }
      Type* func_rtrn = ParseType(depth + 1);
      Variable* func_type_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, FUNC_TYPE_MARKER);
      func_type_var->SetEvalType(TypeFactory::Instance()->MakeType(func_params, func_rtrn), false);
      expression = func_type_var;
    }
    else if(group.size() > 1) {
      ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
    }
  }
  // lambda expression
  else if(Match(TOKEN_BACK_SLASH)) {
    NextToken();
    expression = ParseLambda(depth + 1);
  }
  // negation
  else if(Match(TOKEN_NOT_ID)) {
    NextToken();
    CalculatedExpression* calc_expr = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_NOT_EXPR);
    calc_expr->SetLeft(ParseTerm(depth + 1));
    expression = calc_expr;
  }
  // negation 
  else if(Match(TOKEN_SUB)) {
    NextToken();

    switch(GetToken()) {
    case TOKEN_INT_LIT:
      expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, NegateIntLiteral(scanner->GetToken()->GetInt64Lit()));
      NextToken();
      break;

    case TOKEN_FLOAT_LIT:
      expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos, -scanner->GetToken()->GetFloatLit());
      NextToken();
      break;

    case TOKEN_IDENT: {
      const std::wstring ident = scanner->GetToken()->GetIdentifier();
      const int line_num = GetLineNumber();
      const int line_pos = GetLinePosition();
      IdentifierContext ident_context(ident, line_num, line_pos);
      NextToken();

      if(Match(TOKEN_OPEN_PAREN)) {
        MethodCall* right = ParseMethodCall(ident_context, depth + 1);
        if(right) {
          Expression* left = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, -1);
          CalculatedExpression* calc = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MUL_EXPR, left, right);
          expression = calc;
        }
      }
      else {
        Variable* left = ParseVariable(ident_context, depth + 1);
        Expression* right = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, -1);
        CalculatedExpression* calc = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MUL_EXPR, left, right);
        expression = calc;
      }
    }
      break;

    // -(a + b), -(x), -(F()): a parenthesised operand, as `<>` already takes
    // (#1099). It is the same -1 * operand the call form above builds.
    case TOKEN_OPEN_PAREN: {
      Expression* right = ParseTerm(depth + 1);
      if(right) {
        Expression* left = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, -1);
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MUL_EXPR, left, right);
      }
    }
      break;

    case TOKEN_BAD_INT_LIT:
    case TOKEN_LEADING_ZERO_INT_LIT:
    case TOKEN_BAD_FLOAT_LIT:
      expression = ParseBadNumericLiteral(true, file_name, line_num, line_pos);
      break;

    case TOKEN_UNKNOWN:
      expression = ParseRejectedToken(depth, file_name, line_num, line_pos);
      break;

    default:
      ProcessError(L"Expected expression", TOKEN_SEMI_COLON);
      break;
    }
  }
  else {
    switch(GetToken()) {
    case TOKEN_CHAR_LIT:
      expression = TreeFactory::Instance()->MakeCharacterLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetCharLit());
      NextToken();
      break;

    case TOKEN_INT_LIT:
      expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetInt64Lit());
      NextToken();
      break;

    case TOKEN_FLOAT_LIT:
      expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetFloatLit());
      NextToken();
      break;

    case TOKEN_CHAR_STRING_LIT: {
      const std::wstring ident = scanner->GetToken()->GetIdentifier();
      const bool is_lit = scanner->GetToken()->GetByteLit();
      if(!is_lit && HasUnclosedInterpolation(ident)) {
        ProcessError(L"A quote inside '{$...}' ends the string early, leaving the interpolation open; compute the value into a local first and interpolate the local", TOKEN_SEMI_COLON);
        break;
      }
      expression = TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, ident, is_lit);
      NextToken();
    }
      break;

    case TOKEN_BAD_CHAR_STRING_LIT:
      if(HasLambdaInInterpolation(scanner->GetToken()->GetIdentifier())) {
        ProcessTokenError(L"Lambdas are not allowed inside string interpolation; assign the lambda to a local first"
                          L"\n\tIn a string a '\\' starts an escape sequence, so a lambda's '\\(' cannot be read inside '{$...}'."
                          L" Assign the lambda to a local variable first, then use that variable in the interpolation", TOKEN_SEMI_COLON);
      }
      else {
        ProcessError(L"Invalid escaped string literal", TOKEN_SEMI_COLON);
      }
      NextToken();
      break;

    case TOKEN_BAD_INT_LIT:
    case TOKEN_LEADING_ZERO_INT_LIT:
    case TOKEN_BAD_FLOAT_LIT:
      expression = ParseBadNumericLiteral(false, file_name, line_num, line_pos);
      break;

    case TOKEN_OPEN_BRACKET:
      expression = ParseStaticArray(depth + 1);
      // a subscript on the literal itself: '["a", "b"][1]' (#1184). Taken only
      // here, at the top of a literal: between the rows of a nested literal a
      // '[' starts the next row.
      if(Match(TOKEN_OPEN_BRACKET)) {
        static_cast<StaticArray*>(expression)->SetIndices(ParseIndices(depth + 1));
      }
      break;

    case TOKEN_OPEN_BRACE:
      expression = ParseMapLiteral(depth + 1);
      break;

    case TOKEN_NIL_ID:
      expression = TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos);
      NextToken();
      break;

    case TOKEN_TRUE_ID:
      expression = TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, true);
      NextToken();
      break;

    case TOKEN_FALSE_ID:
      expression = TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, false);
      NextToken();
      break;

    case TOKEN_UNKNOWN:
      expression = ParseRejectedToken(depth, file_name, line_num, line_pos);
      break;

    default:
      ProcessError(L"Expected expression", TOKEN_SEMI_COLON);
      break;
    }
  }

  // subsequent method calls
  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) &&
     !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
    if(expression && expression->GetExpressionType() == VAR_EXPR) {
      expression = ParseMethodCall(static_cast<Variable*>(expression), depth + 1);
    }
    else {
      ParseMethodCall(expression, depth + 1);
    }
  }
  // type cast
  else {
    ParseCastTypeOf(expression, depth + 1);
  }

  return expression;
}

/****************************
 * Parses an explicit type
 * cast or typeof
 ****************************/
void Parser::ParseCastTypeOf(Expression* expression, int depth)
{
  CheckDepth(depth);
  if(Match(TOKEN_ASSESSOR)) {
    NextToken();

    if(Match(TOKEN_AS_ID)) {
      NextToken();

      if(!Match(TOKEN_OPEN_PAREN)) {
        ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
      }
      NextToken();

      if(expression) {
        expression->SetCastType(ParseType(depth + 1), false);
      }

      if(!Match(TOKEN_CLOSED_PAREN)) {
        ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
      }
      NextToken();
    }
    else if(Match(TOKEN_TYPE_OF_ID)) {
      NextToken();

      if(!Match(TOKEN_OPEN_PAREN)) {
        ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
      }
      NextToken();

      if(expression) {
        expression->SetTypeOf(ParseType(depth + 1));
      }

      if(!Match(TOKEN_CLOSED_PAREN)) {
        ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
      }
      NextToken();
    }
    else {
      ProcessError(L"Expected cast or 'TypeOf(..)'", TOKEN_SEMI_COLON);
    }

    // subsequent method calls
    if(Match(TOKEN_ASSESSOR)) {
      ParseMethodCall(expression, depth + 1);
    }
  }
}

/****************************
 * Parses a method call.
 ****************************/
MethodCall* Parser::ParseMethodCall(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Parent call", depth);
#endif

  NextToken();

  int end_pos = 0;
  ExpressionList* exprs = ParseCallArguments(end_pos, depth + 1, L"New");

  return TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), end_pos, PARENT_CALL, L"", exprs);
}

/****************************
 * Parses a method call.
 ****************************/
MethodCall* Parser::ParseMethodCall(IdentifierContext& context, int depth)
{
  CheckDepth(depth);
  const int line_num = context.GetLineNumber();
  const int line_pos = context.GetLinePosition();
  const std::wstring ident = context.GetIdentifier();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Method call", depth);
#endif

  MethodCall* method_call = nullptr;
  // a caller already inside a Try() region (ParseMethodCall(Expression*)) hands
  // that down; taken once, so argument lists parsed below do not inherit it
  const bool inherited_guard = next_call_try_guarded;
  next_call_try_guarded = false;
  // '?->' -- capture before NextToken() consumes the accessor. The call itself
  // is built exactly as before; it is wrapped in Try() at the return, so every
  // branch below (generics, casts, further chaining) is untouched.
  bool nil_safe = false;
  if(Match(TOKEN_ASSESSOR)) {
    nil_safe = scanner->GetToken()->IsNilSafe();
    NextToken();

    // method call
    if(Match(TOKEN_IDENT)) {
      const int mid_line_num = GetLineNumber();
      const int mid_line_pos = GetLinePosition();
      const std::wstring method_ident = scanner->GetToken()->GetIdentifier();
      NextToken();

      if(Match(TOKEN_OPEN_PAREN)) {
        int end_pos = 0;
        ExpressionList* exprs = ParseCallArguments(end_pos, depth + 1, method_ident);

        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, mid_line_num, mid_line_pos, 
                                                              GetLineNumber(), end_pos, ident, method_ident, exprs);
        // function
        if(Match(TOKEN_TILDE)) {
          NextToken();
          Type* func_rtrn = ParseType(depth + 1);
          method_call->SetFunctionalReturn(func_rtrn);
        }
        // anonymous class
        else if(Match(TOKEN_LES) && Match(TOKEN_IDENT, SECOND_INDEX) &&
                (Match(TOKEN_LES, THIRD_INDEX) || Match(TOKEN_GTR, THIRD_INDEX) || 
                 Match(TOKEN_COMMA, THIRD_INDEX) || Match(TOKEN_PERIOD, THIRD_INDEX))) {
          std::vector<Type*> generic_dclrs = ParseGenericTypes(depth);
          method_call->SetConcreteTypes(generic_dclrs);
        }
      }
      else {
        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, 
                                                              GetLineNumber(), GetLinePosition(), ident, method_ident);
      }
    }
    // new call
    else if(Match(TOKEN_NEW_ID)) {
      // capture position of `New` token for hover (alt-range matching needs mid line/pos)
      const int new_mid_line_num = GetLineNumber();
      const int new_mid_line_pos = GetLinePosition();
      NextToken();
      // new array
      if(Match(TOKEN_OPEN_BRACKET)) {
        int end_pos = 0;
        ExpressionList* expressions = ParseExpressionList(end_pos, depth + 1, TOKEN_OPEN_BRACKET, TOKEN_CLOSED_BRACKET);

        // 'T->New[n]' of a method's own type parameter is an array of its bound,
        // as a 'T' the type parser reads is (#1242); the name is not a class
        std::map<std::wstring, std::wstring>::iterator method_generic = method_generic_bounds.find(ident);
        const std::wstring array_ident = method_generic != method_generic_bounds.end() ? method_generic->second : ident;
        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                              GetLineNumber(), end_pos, NEW_ARRAY_CALL, array_ident, expressions);
        method_call->SetMidLinePosition(new_mid_line_num, new_mid_line_pos);
        // array of generics
        if(Match(TOKEN_LES)) {
          std::vector<Type*> generic_dclrs = ParseGenericTypes(depth);
          method_call->SetConcreteTypes(generic_dclrs);
        }
      }
      // new object
      else {
        int end_pos = 0;
        ExpressionList* exprs = ParseCallArguments(end_pos, depth + 1, L"New");

        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                              GetLineNumber(), end_pos, NEW_INST_CALL, ident, exprs);
        method_call->SetMidLinePosition(new_mid_line_num, new_mid_line_pos);
        // anonymous class
        if(Match(TOKEN_LES)) {
          std::vector<Type*> generic_dclrs = ParseGenericTypes(depth);
          method_call->SetConcreteTypes(generic_dclrs);
        }
        else if(Match(TOKEN_OPEN_BRACE) || Match(TOKEN_IMPLEMENTS_ID)) {
          ParseAnonymousClass(method_call, depth);
        }
      }
    }
    else if(Match(TOKEN_AS_ID)) {
      Variable* variable = ParseVariable(context, depth + 1);

      NextToken();
      if(!Match(TOKEN_OPEN_PAREN)) {
        ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
      }
      NextToken();

      if(variable) {
        variable->SetCastType(ParseType(depth + 1), false);
      }

      if(!Match(TOKEN_CLOSED_PAREN)) {
        ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
      }
      NextToken();

      // an enum or consts item receiver, glued to 'C#R' by the expression parser:
      // 'C->R->As(Int)->ToString()'. Build the same enum-call head the statement
      // parser does, so the chain dispatches on the item's value; as a variable
      // the item was an undeclared local and the call read no receiver.
      const size_t enum_mark = ident.find(L'#');
      if(Match(TOKEN_ASSESSOR) && enum_mark != std::wstring::npos && variable) {
        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                              ident.substr(0, enum_mark), ident.substr(enum_mark + 1));
        method_call->SetCastType(variable->GetCastType(), false);
      }
      // subsequent method calls
      else if(Match(TOKEN_ASSESSOR)) {
        method_call = ParseMethodCall(variable, depth + 1);
        method_call->SetCastType(variable->GetCastType(), false);
      }
      else {
        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), ident, L"");
        method_call->SetCastType(variable->GetCastType(), false);
      }
    }
    else if(Match(TOKEN_TYPE_OF_ID)) {
      Variable* variable = ParseVariable(context, depth + 1);
      NextToken();

      if(!Match(TOKEN_OPEN_PAREN)) {
        ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
      }
      NextToken();

      method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable);
      method_call->SetTypeOf(ParseType(depth + 1));

      if(!Match(TOKEN_CLOSED_PAREN)) {
        ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
      }
      NextToken();
    }
    else {
      ProcessError(L"Expected identifier", TOKEN_SEMI_COLON);
    }
  }
  // method call
  else if(Match(TOKEN_OPEN_PAREN)) {
    // When current_class is null (e.g., parsing isolated expression for string interpolation),
    // use empty string - the context analyzer will determine the actual class from context
    const std::wstring klass_name = current_class ? current_class->GetName() : L"";

    int end_pos = 0;
    ExpressionList* exprs = ParseCallArguments(end_pos, depth + 1, ident);

    method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, -1, -1,
                                                          GetLineNumber(), end_pos, klass_name, ident, exprs);
    if(Match(TOKEN_TILDE)) {
      NextToken();
      method_call->SetFunctionalReturn(ParseType(depth + 1));
    }
  }
  else {
    method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, 
                                                          GetLineNumber(), GetLinePosition(), ident, L"");
  }

  // generics
  if(Match(TOKEN_LES) && Match(TOKEN_IDENT, SECOND_INDEX) &&
     (Match(TOKEN_LES, THIRD_INDEX) || Match(TOKEN_GTR, THIRD_INDEX) ||
      Match(TOKEN_COMMA, THIRD_INDEX) || Match(TOKEN_PERIOD, THIRD_INDEX))) {
    std::vector<Type*> generic_dclrs = ParseGenericTypes(depth);
    if(method_call) {
      method_call->SetConcreteTypes(generic_dclrs);
    }
  }

  // subscript on the call's result: 'GetItems()[0]'. Consumed here so it is not
  // re-read as a fresh static-array literal, which silently discarded the call.
  if(Match(TOKEN_OPEN_BRACKET) && method_call) {
    method_call->SetCallIndices(ParseIndices(depth + 1));
  }

  // 'F(a)(b)' calls the function value F(a) returns (#1184)
  if(Match(TOKEN_OPEN_PAREN) && IsCurriableCall(method_call)) {
    ParseCurriedCalls(method_call, depth + 1);
  }

  // under the Try() this call is about to get, BEFORE the rest of the chain is
  // parsed, so a later '?->' in it knows it is already guarded
  if((nil_safe || inherited_guard) && method_call) {
    method_call->SetTryGuarded(true);
  }

  // subsequent method calls
  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) &&
     !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
    ParseMethodCall(method_call, depth + 1);
  }
  // type cast
  else {
    ParseCastTypeOf(method_call, depth + 1);
  }

  // 'a?->b()' becomes 'a->Try()->b()'. Try() guards the whole remainder of the
  // chain, so wrapping the head is enough -- a later '?->' in the same chain is
  // already covered and behaves as a plain accessor. The receiver is named
  // once, so it is evaluated once.
  if(nil_safe && !inherited_guard && method_call) {
    MethodCall* try_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                                   line_num, line_pos,
                                                                   GetLineNumber(), GetLinePosition(),
                                                                   ident, L"Try",
                                                                   TreeFactory::Instance()->MakeExpressionList());
    try_call->SetMethodCall(method_call);
    return try_call;
  }

  return method_call;
}

/****************************
 * Parses a method call. This
 * is either an expression method
 * or a call from a method return
 * value.
 ****************************/
void Parser::ParseMethodCall(Expression* expression, int depth)
{
  CheckDepth(depth);
#ifdef _DEBUG
  Debug(L"Method call", depth);
#endif

  // '?->' after a call result or a literal: `R->G()?->Div(1, 0)`,
  // `"abc"?->SubString(5, 9)`. Capture it before NextToken() consumes the
  // accessor -- this path dropped the flag, so the call ran unguarded and an
  // error it should have recovered ended the program.
  const bool nil_safe = scanner->GetToken()->IsNilSafe();
  NextToken();

  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring ident = scanner->GetToken()->GetIdentifier();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  IdentifierContext ident_context(ident, line_num, line_pos);
  NextToken();

  if(expression) {
    // Attach to the END of any existing method-call chain. A parenthesized
    // expression such as (a->b())->c() arrives here with b() already attached
    // to 'expression'; without walking to the tail we would overwrite b() with
    // c() and silently drop the inner call.
    Expression* tail = expression;
    // Only the first '?->' in a chain introduces Try(): it guards the rest. The
    // Try() is usually BEHIND this receiver (`s?->ToUpper()?->ToLower()` arrives
    // here with ToUpper()), and back-links are not set while parsing, so the
    // calls under a Try() carry a flag instead.
    bool guarded = expression->GetExpressionType() == METHOD_CALL_EXPR &&
                   static_cast<MethodCall*>(expression)->IsTryGuarded();
    while(tail->GetMethodCall()) {
      tail = tail->GetMethodCall();
      if(static_cast<MethodCall*>(tail)->GetMethodName() == L"Try" || static_cast<MethodCall*>(tail)->IsTryGuarded()) {
        guarded = true;
      }
    }
    next_call_try_guarded = guarded || nil_safe;
    MethodCall* call = ParseMethodCall(ident_context, depth + 1);
    next_call_try_guarded = false;
    if(nil_safe && !guarded && call) {
      MethodCall* try_call = TreeFactory::Instance()->MakeMethodCall(GetFileName(), line_num, line_pos,
                                                                     line_num, line_pos,
                                                                     GetLineNumber(), GetLinePosition(),
                                                                     ident, L"Try",
                                                                     TreeFactory::Instance()->MakeExpressionList());
      try_call->SetMethodCall(call);
      tail->SetMethodCall(try_call);
    }
    else {
      tail->SetMethodCall(call);
    }
    // subscript on the call's result: 'a->GetItems()[0]->Name()'
    if(Match(TOKEN_OPEN_BRACKET) && call) {
      call->SetCallIndices(ParseIndices(depth + 1));
    }
    // subsequent method calls
    if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) &&
       !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
      ParseMethodCall(call, depth + 1);
    }
    // type cast
    else {
      ParseCastTypeOf(call, depth + 1);
    }
  }
}

/****************************
 * Parses a method call. This
 * is either an expression method
 * or a call from a method return
 * value.
 ****************************/
MethodCall* Parser::ParseMethodCall(Variable* variable, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  // '?->' -- capture before NextToken() consumes the accessor
  const bool nil_safe = scanner->GetToken()->IsNilSafe();

  NextToken();
  const std::wstring &method_ident = scanner->GetToken()->GetIdentifier();

  NextToken();
  const int mid_line_num = GetLineNumber();
  const int mid_line_pos = GetLinePosition();
  const std::wstring mid_ident = variable->GetName();
  IdentifierContext ident_context(mid_ident, mid_line_num, mid_line_pos);

  int end_pos = 0;
  ExpressionList* exprs = ParseCallArguments(end_pos, depth + 1, method_ident);

  // 'a?->b()' desugars to 'a->Try()->b()'. Try() guards the whole remainder of
  // the chain, so only the first '?->' introduces one; a later '?->' in the same
  // chain is already covered and parses as a plain accessor. The receiver is
  // evaluated once, by Try().
  MethodCall* call;
  MethodCall* result;
  if(nil_safe) {
    MethodCall* try_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                                   mid_line_num, mid_line_pos,
                                                                   GetLineNumber(), end_pos, variable, L"Try",
                                                                   TreeFactory::Instance()->MakeExpressionList());
    // The guarded call hangs off Try() as a chained call, built exactly the way
    // ParseMethodCall(IdentifierContext) builds one: the receiver slot carries
    // the VARIABLE's name, not the enclosing class name.
    call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos,
                                                   mid_line_num, mid_line_pos,
                                                   GetLineNumber(), end_pos, mid_ident, method_ident, exprs);
    call->SetTryGuarded(true);
    try_call->SetMethodCall(call);
    result = try_call;
  }
  else {
    call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, mid_line_num, mid_line_pos,
                                                   GetLineNumber(), end_pos, variable, method_ident, exprs);
    result = call;
  }

  // Subscript on the call's RESULT: 'GetItems()[0]'. Consumed here so it is not
  // re-read as a fresh static-array literal, which silently discarded the call
  // and evaluated '[0]' as a one-element Int[] instead.
  if(Match(TOKEN_OPEN_BRACKET)) {
    call->SetCallIndices(ParseIndices(depth + 1));
  }

  // 'v->Make(a)(b)' calls the function value Make(a) returns (#1184)
  if(Match(TOKEN_OPEN_PAREN) && IsCurriableCall(call)) {
    call = ParseCurriedCalls(call, depth + 1);
  }

  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) && !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
    // the rest of a '?->' chain is already under this Try()
    next_call_try_guarded = nil_safe;
    call->SetMethodCall(ParseMethodCall(ident_context, depth + 1));
    next_call_try_guarded = false;
  }

  return result;
}

/****************************
 * A call written with an argument list may be applied again: 'F(a)(b)'.
 * A function reference ('F(Int) ~ Int') and a call without parentheses
 * may not.
 ****************************/
bool Parser::IsCurriableCall(MethodCall* method_call)
{
  return method_call && method_call->GetCallType() == METHOD_CALL && method_call->GetCallingParameters() &&
    !method_call->IsFunctionDefinition();
}

/****************************
 * Parses the argument lists that follow a call, 'F(a)(b)(c)': each one is a
 * link that calls the function value the link before it returned. The
 * analyzer requires that value to be a function (#1184). Returns the last
 * link.
 ****************************/
MethodCall* Parser::ParseCurriedCalls(MethodCall* method_call, int depth)
{
  CheckDepth(depth);
  while(Match(TOKEN_OPEN_PAREN)) {
    const std::wstring file_name = GetFileName();
    const int line_num = GetLineNumber();
    const int line_pos = GetLinePosition();

    int end_pos = 0;
    ExpressionList* exprs = ParseExpressionList(end_pos, depth + 1);
    MethodCall* curried = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos,
                                                                  GetLineNumber(), end_pos, L"", L"", exprs);
    curried->SetCurriedCall();
    method_call->SetMethodCall(curried);

    if(Match(TOKEN_OPEN_BRACKET)) {
      curried->SetCallIndices(ParseIndices(depth + 1));
    }
    method_call = curried;
  }

  return method_call;
}

/****************************
 * 'spawn' is a word of the language only in front of a '{'
 ****************************/
PARSER_NO_INLINE bool Parser::IsSpawn()
{
  return Match(TOKEN_IDENT) && Match(TOKEN_OPEN_BRACE, SECOND_INDEX) && scanner->GetToken()->GetIdentifier() == L"spawn";
}

/****************************
 * Parses 'spawn { ... }': the block runs on a new thread and the expression
 * is that thread, already started. It is written out as constructs the
 * language already has, so there is nothing new behind the parser:
 *
 *   System.Concurrency.Thread->New() {
 *     @body : () ~ Nil;
 *     New() { Parent("spawn:<file>:<line>"); }
 *     method : public : SpawnStart(body : () ~ Nil) ~ System.Concurrency.Thread {
 *       @body := body; Execute(Nil); return @self;
 *     }
 *     method : public : Run(param : System.Base) ~ Nil { body := @body; body(); }
 *   }->SpawnStart(\() ~ Nil : () => { ... })
 *
 * The block is the lambda, so it holds the values of the locals it names as
 * they were when the thread was started, exactly as a lambda does. The class
 * is parsed from text; the block is parsed from the program.
 ****************************/
PARSER_NO_INLINE MethodCall* Parser::ParseSpawn(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Spawn", depth);
#endif

  if(!current_class || !current_method) {
    ProcessError(L"'spawn' must be used in a method or function", TOKEN_SEMI_COLON);
    return nullptr;
  }

  // the class of a 'spawn' is an anonymous class, and those do not nest
  if(prev_method) {
    ProcessError(L"'spawn' cannot be used in a method of an anonymous class", TOKEN_SEMI_COLON);
    return nullptr;
  }

  // 'spawn'
  NextToken();

  // the block
  std::vector<Type*> no_params;
  Type* body_type = TypeFactory::Instance()->MakeType(no_params, TypeFactory::Instance()->MakeType(NIL_TYPE));
  Lambda* body = ParseLambda(depth + 1, body_type);
  const int end_line_num = GetLineNumber();
  const int end_line_pos = GetLinePosition();

  // the thread's name tells where it was started
  std::wstring file_base = file_name;
  const size_t slash_pos = file_base.find_last_of(L"/\\");
  if(slash_pos != std::wstring::npos) {
    file_base = file_base.substr(slash_pos + 1);
  }
  std::wstring thread_name = L"spawn:";
  for(const wchar_t c : file_base) {
    const bool plain = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'-';
    thread_name += plain ? c : L'_';
  }
  thread_name += L':' + std::to_wstring(line_num);

  // the class, placed at the position of 'spawn'
  std::wstring text(line_num > 1 ? line_num - 1 : 0, L'\n');
  text += std::wstring(line_pos > 1 ? line_pos - 1 : 0, L' ');
  text += L"System.Concurrency.Thread->New() { @body : () ~ Nil; New() { Parent(\"" + thread_name + L"\"); } "
          L"method : public : SpawnStart(body : () ~ Nil) ~ System.Concurrency.Thread { @body := body; Execute(Nil); return @self; } "
          L"method : public : Run(param : System.Base) ~ Nil { body := @body; body(); } }";

  ScannerScope spawn_scanner(scanner);
  StateScope<bool> spawn_flag(spawn_class);
  scanner = new Scanner(file_name, alt_syntax, text);
  NextToken();
  spawn_class = true;
  Expression* thread = ParseSimpleExpression(depth + 1);
  spawn_class = false;
  const bool whole = Match(TOKEN_END_OF_STREAM);
  spawn_scanner.Restore();

  if(!thread || !whole || thread->GetExpressionType() != METHOD_CALL_EXPR) {
    diagnostics.Error(file_name, line_num, line_pos, L"Unable to build the thread for 'spawn'");
    return nullptr;
  }

  ExpressionList* arguments = TreeFactory::Instance()->MakeExpressionList();
  arguments->AddExpression(body);
  MethodCall* start = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, -1, -1, end_line_num, end_line_pos,
                                                              current_class->GetName(), L"SpawnStart", arguments);
  MethodCall* thread_call = static_cast<MethodCall*>(thread);
  thread_call->SetMethodCall(start);

  return thread_call;
}

/****************************
 * Parses an anonymous class
 ****************************/
void Parser::ParseAnonymousClass(MethodCall* method_call, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  if(prev_method && current_method) {
    ProcessError(L"Invalid anonymous nested classes");
    return;
  }

  // Build a DETERMINISTIC unique suffix from the source location instead of a
  // random token. A random suffix made the same source compile to a different
  // class name every time, which propagated into the emitted (.obl) bytecode and
  // made library builds non-reproducible. The source basename + line + column
  // uniquely identify an anonymous class (no two can share a source position),
  // are stable across compiles, path-independent, and collision-safe across
  // separately-compiled libraries.
  std::wstring file_base = file_name;
  const size_t slash_pos = file_base.find_last_of(L"/\\");
  if(slash_pos != std::wstring::npos) {
    file_base = file_base.substr(slash_pos + 1);
  }
  const size_t dot_pos = file_base.find_last_of(L'.');
  if(dot_pos != std::wstring::npos) {
    file_base = file_base.substr(0, dot_pos);
  }
  // The class a 'spawn' makes has a name of its own: its text is generated, so
  // its position could be that of a class the programmer wrote on the same
  // line. It is named for the class it is written in as well, so a library
  // and a program with a 'spawn' at one position of two files of one name do
  // not make the same class.
  const std::wstring cls_name = method_call->GetVariableName() +
    (spawn_class ? L".#Spawn." + (current_class ? current_class->GetName() + L'.' : L"") : L".#Anonymous.") +
    file_base + L'_' + std::to_wstring(line_num) + L'_' + std::to_wstring(line_pos) + L'#';

  std::vector<std::wstring> interface_names;
  if(Match(TOKEN_IMPLEMENTS_ID)) {
    NextToken();
    while(!Match(TOKEN_OPEN_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
      // identifier
      const std::wstring &ident = ParseBundleName();
      interface_names.push_back(ident);
      if(Match(TOKEN_COMMA)) {
        NextToken();
        if(!Match(TOKEN_IDENT)) {
          ProcessError(TOKEN_IDENT);
        }
      }
      else if(!Match(TOKEN_OPEN_BRACE)) {
        ProcessError(L"Expected ',' or '{'", TOKEN_OPEN_BRACE);
        NextToken();
      }
    }
  }

  // statement list
  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  Class* klass = TreeFactory::Instance()->MakeClass(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), cls_name, method_call->GetVariableName(), interface_names);

  Class* prev_class = current_class;
  prev_method = current_method;
  current_method = nullptr;
  current_class = klass;
  symbol_table->NewParseScope();

  // add '@self' entry
  SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(GetScopeName(SELF_ID), 
                                                                TypeFactory::Instance()->MakeType(CLASS_TYPE, cls_name, file_name, line_num, line_pos),
                                                                false, false, true);

  symbol_table->CurrentParseScope()->AddEntry(entry);

  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    // parse 'method | function | declaration'
    if(Match(TOKEN_FUNCTION_ID)) {
      Method* method = ParseMethod(true, false, depth + 1);
      bool was_added = klass->AddMethod(method);
      if(!was_added) {
        ProcessError(L"Method/function already defined or overloaded '" + method->GetName() + L"'", method);
      }
    }
    else if(Match(TOKEN_METHOD_ID) || Match(TOKEN_NEW_ID)) {
      Method* method = ParseMethod(false, false, depth + 1);
      bool was_added = klass->AddMethod(method);
      if(!was_added) {
        ProcessError(L"Method/function already defined or overloaded '" + method->GetName() + L"'", method);
      }
    }
    else if(Match(TOKEN_IDENT)) {
      const std::wstring ident = scanner->GetToken()->GetIdentifier();
      const int line_num = GetLineNumber();
      const int line_pos = GetLinePosition();
      IdentifierContext ident_context(ident, line_num, line_pos);
      NextToken();

      klass->AddStatement(ParseDeclaration(ident_context, false, depth + 1));
      if(!Match(TOKEN_SEMI_COLON)) {
        ProcessError(L"Expected ';'", TOKEN_SEMI_COLON);
      }
      NextToken();
    }
    else {
      ProcessError(L"Expected declaration", TOKEN_SEMI_COLON);
      NextToken();
    }
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  if(!klass->HasDefaultNew()) {
    const std::wstring method_name = klass->GetName() + L":New";
    Method* default_new = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), method_name, NEW_PUBLIC_METHOD, false, false);

    symbol_table->NewParseScope();
    default_new->SetDeclarations(TreeFactory::Instance()->MakeDeclarationList());
    default_new->SetStatements(TreeFactory::Instance()->MakeStatementList());
    default_new->SetReturn(TypeFactory::Instance()->MakeType(CLASS_TYPE, klass->GetName(), file_name, line_num, line_pos));
    symbol_table->PreviousParseScope(default_new->GetParsedName());
    
    // guarded by !HasDefaultNew() above, so a collision should be unreachable --
    // checked anyway to match the other AddMethod call sites, and so that a future
    // change to that guard cannot lose the synthesized New silently
    if(!klass->AddMethod(default_new)) {
      ProcessError(L"Method/function already defined or overloaded '" + method_name + L"'", default_new);
    }
  }

  symbol_table->PreviousParseScope(current_class->GetName());

  klass->SetAnonymousCall(method_call);
  method_call->SetAnonymousClass(klass);
  method_call->SetVariableName(cls_name);
  current_bundle->AddClass(klass);

  current_class = prev_class;
  current_method = prev_method;
  prev_method = nullptr;
}

/****************************
 * Parses an 'if' statement
 ****************************/
If* Parser::ParseIf(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"If", depth);
#endif

  NextToken();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  ++in_if_condition;
  Expression* expression = ParseExpression(depth + 1);
  --in_if_condition;

  // 'if(value is Type name)': 'is' is a keyword only here, after the tested
  // value, where a name cannot follow an expression. It stays an ordinary name.
  SymbolEntry* bind_entry = nullptr;
  Assignment* bind_assign = nullptr;
  if(expression && IsTypeTestStart()) {
    expression = ParseTypeTest(expression, bind_entry, bind_assign, depth + 1);
  }

  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  // the name belongs to the block: declared in its scope, by its first statement
  if(bind_entry && !DeclareTypeTestName(bind_entry, bind_assign)) {
    bind_entry = nullptr;
  }
  StatementList* if_statements = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();
  if(bind_entry) {
    PrependTypeTestBinding(if_statements, bind_entry, bind_assign);
  }

  If* if_stmt;
  if(Match(TOKEN_ELSE_ID) && Match(TOKEN_IF_ID, SECOND_INDEX)) {
    NextToken();
    If* next = ParseIf(depth + 1);
    if_stmt = TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression, if_statements, next);
  }
  else if(Match(TOKEN_ELSE_ID)) {
    NextToken();
    if_stmt = TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression, if_statements);
    symbol_table->CurrentParseScope()->NewParseScope();
    if_stmt->SetElseStatements(ParseStatementList(depth + 1));
    symbol_table->CurrentParseScope()->PreviousParseScope();
  }
  else {
    if_stmt = TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression, if_statements);
  }

  return if_stmt;
}

/****************************
 * The statements after '??' in a guard: 'return V', 'break', 'continue' or a block
 * that ends in one of those or Runtime->Exit(..). A block that can fall through would
 * leave the variable Nil, which is the case the guard exists to rule out.
 ****************************/
PARSER_NO_INLINE StatementList* Parser::ParseNilGuardBail(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

  StatementList* bail = nullptr;
  if(Match(TOKEN_OPEN_BRACE)) {
    symbol_table->CurrentParseScope()->NewParseScope();
    bail = ParseStatementList(depth + 1);
    symbol_table->CurrentParseScope()->PreviousParseScope();
  }
  else {
    // the analyzer enters a scope for the 'if' block: one must exist here too
    symbol_table->CurrentParseScope()->NewParseScope();
    bail = TreeFactory::Instance()->MakeStatementList();
    if(Match(TOKEN_RETURN_ID)) {
      bail->AddStatement(ParseReturn(depth + 1));
    }
    else if(Match(TOKEN_BREAK_ID)) {
      bail->AddStatement(TreeFactory::Instance()->MakeBreakContinue(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), BREAK_STMT));
      NextToken();
    }
    else {
      bail->AddStatement(TreeFactory::Instance()->MakeBreakContinue(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), CONTINUE_STMT));
      NextToken();
    }
    symbol_table->CurrentParseScope()->PreviousParseScope();
    return bail;
  }

  const std::vector<Statement*> written = bail->GetStatements();
  bool leaves = false;
  // a statement that did not parse is null: its syntax error has been reported
  if(!written.empty() && written.back()) {
    Statement* last = written.back();
    switch(last->GetStatementType()) {
    case RETURN_STMT:
    case BREAK_STMT:
    case CONTINUE_STMT:
      leaves = true;
      break;

    case METHOD_CALL_STMT: {
      MethodCall* call = static_cast<MethodCall*>(last);
      leaves = call->GetVariableName() == L"Runtime" && call->GetMethodName() == L"Exit";
    }
      break;

    default:
      break;
    }
  }
  if(!leaves) {
    // written in two pieces: "??'" in one literal is a trigraph to an older compiler (and a warning to this one)
    ProcessError(L"The block after '?" L"?' must end in return, break, continue or Runtime->Exit(..), or the variable can still be Nil");
  }

  return bail;
}

/****************************
 * What the guard of 'cells[i] := E ?? return' reads: the element that was
 * assigned, 'cells[i]' again. An index that is not an integer literal is
 * evaluated once: where the target evaluates it, it is left in a hidden local
 * ('cells[#guard_N := i]'), and the guard reads that ('cells[#guard_N]'). The
 * guard used to be made from the target's name alone, so it tested the array,
 * which is not Nil, and never left.
 ****************************/
PARSER_NO_INLINE Variable* Parser::MakeNilGuardElement(Variable* target)
{
  const std::wstring file_name = GetFileName();
  const int line_num = target->GetLineNumber();
  const int line_pos = target->GetLinePosition();

  ExpressionList* written = TreeFactory::Instance()->MakeExpressionList();
  ExpressionList* read = TreeFactory::Instance()->MakeExpressionList();
  const std::vector<Expression*> indices = target->GetIndices()->GetExpressions();
  for(size_t i = 0; i < indices.size(); ++i) {
    Expression* index = indices[i];
    // an index that did not parse is null: its syntax error has been reported
    if(!index) {
      return nullptr;
    }

    if(index->GetExpressionType() == INT_LIT_EXPR && !index->GetMethodCall() && !index->GetCastType()) {
      written->AddExpression(index);
      read->AddExpression(TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, static_cast<IntegerLiteral*>(index)->GetValue()));
      continue;
    }

    std::wstring ident;
    SymbolEntry* entry = AddHiddenLocal(L"guard", ident, line_num, line_pos);
    if(!entry) {
      return nullptr;
    }
    Variable* kept = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident);
    Assignment* keep = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos, kept, index);
    SequenceExpression* sequence = TreeFactory::Instance()->MakeSequenceExpression(file_name, line_num, line_pos,
                                                                                TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident));
    sequence->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, entry, keep));
    written->AddExpression(sequence);
    read->AddExpression(TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, ident));
  }
  target->SetIndices(written);

  Variable* element = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, target->GetName());
  element->SetIndices(read);
  return element;
}

/****************************
 * if(target = Nil) { bail }
 ****************************/
PARSER_NO_INLINE If* Parser::MakeNilGuard(Variable* target, StatementList* bail)
{
  const std::wstring file_name = GetFileName();
  const int line_num = target->GetLineNumber();
  const int line_pos = target->GetLinePosition();

  // a variable or a field is read again by its name; an element by its name and indices
  Variable* subject = target->GetIndices() ? MakeNilGuardElement(target) : nullptr;
  if(!subject) {
    subject = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, target->GetName());
  }
  CalculatedExpression* test = static_cast<CalculatedExpression*>(TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, EQL_EXPR));
  test->SetLeft(subject);
  test->SetRight(TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos));
  return TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), test, bail);
}

/****************************
 * Parses the rest of 'if(value is Type name)', from 'is'. It is
 * 'if(value <> Nil & value->TypeOf(Type)) { name := value->As(Type); ... }':
 * the test is returned, and the declaration that the block starts with is
 * handed back for ParseIf to place. A value that is more than a variable is
 * evaluated once, where the test is, into a hidden local that the test and the
 * cast read. Nil is of no type: the test is false, where a bare TypeOf on Nil
 * is a runtime error.
 ****************************/
PARSER_NO_INLINE Expression* Parser::ParseTypeTest(Expression* subject, SymbolEntry*& bind_entry, Assignment*& bind_assign, int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = subject->GetLineNumber();
  const int line_pos = subject->GetLinePosition();

#ifdef _DEBUG
  Debug(L"Type test", depth);
#endif

  // 'is'
  NextToken();

  const int type_line_num = GetLineNumber();
  const int type_line_pos = GetLinePosition();
  in_type_test_type = true;
  Type* type = ParseType(depth + 1);
  in_type_test_type = false;
  if(!type || type->GetType() != CLASS_TYPE || type->GetDimension() > 0) {
    ProcessError(L"Expected a class name after 'is': 'if(value is Type name)'");
    return subject;
  }

  if(!Match(TOKEN_IDENT)) {
    ProcessError(L"Expected a name for the value after its type: 'if(value is Type name)'");
    return subject;
  }
  const std::wstring bind_ident = scanner->GetToken()->GetIdentifier();
  const int bind_line_num = GetLineNumber();
  const int bind_line_pos = GetLinePosition();
  NextToken();

  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"'value is Type name' is the whole condition of an 'if'; test anything further inside its block");
    return subject;
  }

  // A variable is read as often as needed. Anything else is evaluated once into
  // a hidden local: the test becomes a sequence that declares the local and then
  // tests it, so the local has its type before the test that reads it is analyzed.
  std::wstring source_ident;
  SymbolEntry* source_entry = nullptr;
  if(subject->GetExpressionType() == VAR_EXPR && !subject->GetMethodCall() && !subject->GetCastType() && !subject->GetTypeOf() &&
     !static_cast<Variable*>(subject)->GetIndices() && !static_cast<Variable*>(subject)->GetPreStatement() &&
     !static_cast<Variable*>(subject)->GetPostStatement()) {
    source_ident = static_cast<Variable*>(subject)->GetName();
  }
  else {
    source_entry = AddHiddenLocal(L"is", source_ident, line_num, line_pos);
    if(!source_entry) {
      return subject;
    }
  }

  // value <> Nil & value->TypeOf(Type); the node of an '&' holds its first operand on the right (see ParseLogic)
  CalculatedExpression* not_nil = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, NEQL_EXPR);
  not_nil->SetLeft(source_entry ? TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, source_ident) : subject);
  not_nil->SetRight(TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos));
  Variable* tested = TreeFactory::Instance()->MakeVariable(file_name, type_line_num, type_line_pos, source_ident);
  tested->SetTypeOf(type);
  CalculatedExpression* both = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, AND_EXPR);
  both->SetLeft(tested);
  both->SetRight(not_nil);

  Expression* test = both;
  if(source_entry) {
    Variable* source_target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, source_ident);
    Assignment* source_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), source_target, subject);
    SequenceExpression* sequence = TreeFactory::Instance()->MakeSequenceExpression(file_name, line_num, line_pos, both);
    sequence->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), source_entry, source_assign));
    test = sequence;
  }

  // name := value->As(Type)
  Variable* source = TreeFactory::Instance()->MakeVariable(file_name, type_line_num, type_line_pos, source_ident);
  source->SetCastType(type, false);
  Variable* bind_target = TreeFactory::Instance()->MakeVariable(file_name, bind_line_num, bind_line_pos, bind_ident);
  bind_assign = TreeFactory::Instance()->MakeAssignment(file_name, bind_line_num, bind_line_pos, bind_line_num, bind_line_pos, bind_target, source);
  bind_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, bind_line_num, bind_line_pos, GetScopeName(bind_ident),
                                                        TypeFactory::Instance()->MakeType(type), false, current_method != nullptr);

  return test;
}

/****************************
 * Parses a 'do/while' statement
 ****************************/
DoWhile* Parser::ParseDoWhile(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Do/While", depth);
#endif

  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* statements = ParseStatementList(depth + 1);

  if(!Match(TOKEN_WHILE_ID)) {
    ProcessError(L"Expected 'while'", TOKEN_SEMI_COLON);
  }
  NextToken();

  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  Expression* expression = ParseExpression(depth + 1);
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();
  symbol_table->CurrentParseScope()->PreviousParseScope();

  return TreeFactory::Instance()->MakeDoWhile(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression, statements);
}

/****************************
 * Parses a 'while' statement
 ****************************/
While* Parser::ParseWhile(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"While", depth);
#endif

  NextToken();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  Expression* expression = ParseExpression(depth + 1);
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();

  StatementList* statements;
  symbol_table->CurrentParseScope()->NewParseScope();
  if(!Match(TOKEN_OPEN_BRACE)) {
    statements = TreeFactory::Instance()->MakeStatementList();
  }
  else {
    statements = ParseStatementList(depth + 1);
  }
  symbol_table->CurrentParseScope()->PreviousParseScope();

  return TreeFactory::Instance()->MakeWhile(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression, statements);
}

/****************************
 * Parses a critical section
 ****************************/
/****************************
 * Parses 'try { } on(e : T) { } finally { }'
 * (#1247). 'try', 'on' and 'finally' are
 * contextual: each is a keyword only where
 * it opens its part, so a parameter or
 * variable may still be named 'on' (the
 * standard library has one).
 ****************************/
Try* Parser::ParseTry(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

#ifdef _DEBUG
  Debug(L"Try", depth);
#endif

  NextToken();

  // the error a handler tests, and one to raise again after 'finally'
  const std::wstring id = std::to_wstring(hidden_local_id++);
  SymbolEntry* error_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(L"#try_error_" + id),
                                                                      TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Error", file_name, line_num, line_pos),
                                                                      false, current_method != nullptr);
  symbol_table->CurrentParseScope()->AddEntry(error_entry);
  SymbolEntry* raise_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(L"#try_raise_" + id),
                                                                      TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Error", file_name, line_num, line_pos),
                                                                      false, current_method != nullptr);
  symbol_table->CurrentParseScope()->AddEntry(raise_entry);

  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* body = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();

  // 'on(name : Type) { ... }', any number
  std::vector<TryHandler*> handlers;
  while(Match(TOKEN_IDENT) && Match(TOKEN_OPEN_PAREN, 1) && scanner->GetToken()->GetIdentifier() == L"on") {
    TryHandler* handler = new TryHandler;
    handler->file_name = file_name;
    handler->line_num = GetLineNumber();
    handler->line_pos = GetLinePosition();
    NextToken();
    NextToken();

    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    const std::wstring name = scanner->GetToken()->GetIdentifier();
    NextToken();

    if(!Match(TOKEN_COLON)) {
      ProcessError(L"Expected ':'", TOKEN_COLON);
    }
    NextToken();
    handler->type = ParseType(depth + 1);

    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
    }
    NextToken();

    symbol_table->CurrentParseScope()->NewParseScope();
    handler->entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, handler->line_num, handler->line_pos, GetScopeName(name),
                                                              handler->type, false, current_method != nullptr);
    if(!symbol_table->CurrentParseScope()->AddEntry(handler->entry)) {
      ProcessError(L"Variable already defined in this scope: '" + name + L"'");
    }
    handler->statements = ParseStatementList(depth + 1);
    symbol_table->CurrentParseScope()->PreviousParseScope();

    handlers.push_back(handler);
  }

  // 'finally { ... }'
  StatementList* finally_stmts = nullptr;
  if(Match(TOKEN_IDENT) && Match(TOKEN_OPEN_BRACE, 1) && scanner->GetToken()->GetIdentifier() == L"finally") {
    NextToken();
    symbol_table->CurrentParseScope()->NewParseScope();
    finally_stmts = ParseStatementList(depth + 1);
    symbol_table->CurrentParseScope()->PreviousParseScope();
  }

  return TreeFactory::Instance()->MakeTry(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), body, handlers, finally_stmts,
                                         error_entry, raise_entry);
}

/****************************
 * A copy of an assertion operand that is only read -- a variable, a literal,
 * arithmetic over them -- so the failure message can show its value without a
 * hidden local to hold it (a method has 768 bytes of locals). Null for anything
 * else: a call, an indexed read, '++x', a cast, an interpolated string.
 ****************************/
Expression* Parser::ClonePureOperand(Expression* expression, const std::wstring& file_name, int line_num, int line_pos)
{
  if(!expression || expression->GetMethodCall() || expression->GetCastType() || expression->GetTypeOf()) {
    return nullptr;
  }

  TreeFactory* factory = TreeFactory::Instance();
  switch(expression->GetExpressionType()) {
  case INT_LIT_EXPR:
    return factory->MakeIntegerLiteral(file_name, line_num, line_pos, static_cast<IntegerLiteral*>(expression)->GetValue());

  case FLOAT_LIT_EXPR:
    return factory->MakeFloatLiteral(file_name, line_num, line_pos, static_cast<FloatLiteral*>(expression)->GetValue());

  case CHAR_LIT_EXPR:
    return factory->MakeCharacterLiteral(file_name, line_num, line_pos, static_cast<CharacterLiteral*>(expression)->GetValue());

  case BOOLEAN_LIT_EXPR:
    return factory->MakeBooleanLiteral(file_name, line_num, line_pos, static_cast<BooleanLiteral*>(expression)->GetValue());

  case CHAR_STR_EXPR: {
    const std::wstring text = static_cast<CharacterString*>(expression)->GetString();
    return text.find(L'{') == std::wstring::npos ? factory->MakeCharacterString(file_name, line_num, line_pos, text, true) : nullptr;
  }

  case VAR_EXPR: {
    Variable* variable = static_cast<Variable*>(expression);
    if(variable->GetIndices() || !variable->GetChainedIndices().empty() || variable->GetPreStatement() || variable->GetPostStatement()) {
      return nullptr;
    }
    return factory->MakeVariable(file_name, line_num, line_pos, variable->GetName());
  }

  case AND_EXPR:
  case OR_EXPR:
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EQL_EXPR:
  case LES_EQL_EXPR:
  case GTR_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR: {
    CalculatedExpression* calculated = static_cast<CalculatedExpression*>(expression);
    Expression* left = ClonePureOperand(calculated->GetLeft(), file_name, line_num, line_pos);
    Expression* right = left ? ClonePureOperand(calculated->GetRight(), file_name, line_num, line_pos) : nullptr;
    return right ? factory->MakeCalculatedExpression(file_name, line_num, line_pos, calculated->GetExpressionType(), left, right) : nullptr;
  }

  default:
    return nullptr;
  }
}

/****************************
 * 'assert' is a contextual word, as 'try' is: an identifier named 'assert' is
 * the keyword only where what follows could not continue a statement that
 * starts with a name -- an expression's first token. 'assert := x', 'assert : Bool',
 * 'assert->Foo()', 'assert[0] := 1' and 'assert(x);' are what they always were.
 * 'assert (a) = b' and 'assert (a) : "label"' are assertions: after the closing
 * parenthesis comes a binary operator or the label's ':', which a call statement
 * cannot have.
 ****************************/
PARSER_NO_INLINE bool Parser::IsAssertStatement()
{
  if(scanner->GetToken()->GetIdentifier() != L"assert") {
    return false;
  }

  switch(GetToken(1)) {
  case TOKEN_IDENT:
  case TOKEN_INT_LIT:
  case TOKEN_FLOAT_LIT:
  case TOKEN_CHAR_LIT:
  case TOKEN_CHAR_STRING_LIT:
  case TOKEN_TRUE_ID:
  case TOKEN_FALSE_ID:
  case TOKEN_NIL_ID:
  case TOKEN_NOT:
  case TOKEN_NOT_ID:
    return true;

  case TOKEN_OPEN_PAREN:
    switch(scanner->PeekAfterParens(2)) {
    case TOKEN_COLON:
    case TOKEN_LES:
    case TOKEN_GTR:
    case TOKEN_LEQL:
    case TOKEN_GEQL:
    case TOKEN_EQL:
    case TOKEN_NEQL:
    case TOKEN_AND:
    case TOKEN_OR:
    case TOKEN_ADD:
    case TOKEN_SUB:
    case TOKEN_MUL:
    case TOKEN_DIV:
    case TOKEN_MOD:
      return true;

    default:
      return false;
    }

  default:
    return false;
  }
}

/****************************
 * The source text of an assertion's expression, from the column of its first
 * token to the column where it ended (both 1-based); an expression over several
 * lines is joined with single spaces.
 ****************************/
std::wstring Parser::AssertSourceText(int start_line, int start_pos, int end_line, int end_pos)
{
  if(!scanner->HasSource() || start_line < 1 || end_line < start_line || start_pos < 1 || end_pos < 1) {
    return L"";
  }

  std::wstring text;
  for(int line = start_line; line <= end_line; ++line) {
    std::wstring part = scanner->GetSourceLine(line);
    if(line == end_line) {
      part = part.substr(0, std::min<size_t>(part.size(), (size_t)(end_pos - 1)));
    }
    if(line == start_line) {
      part = part.size() >= (size_t)(start_pos - 1) ? part.substr(start_pos - 1) : L"";
    }

    const size_t first = part.find_first_not_of(L" \t\r");
    const size_t last = part.find_last_not_of(L" \t\r");
    part = first == std::wstring::npos ? L"" : part.substr(first, last - first + 1);
    if(!part.empty()) {
      text += text.empty() ? part : L" " + part;
    }
  }

  return text;
}

/****************************
 * 'assert E : "label";' becomes
 *
 *   [#l := L; #r := R;]            when E is 'L op R' (a comparison)
 *   if((E) = false) {
 *     System.AssertCheck->Fail("file:line", "source of E", label, "" + #l, "" + #r);
 *   };
 *
 * so the operands are evaluated once, and nothing but the test runs when the
 * assertion holds. The label is any String expression, and is optional.
 ****************************/
PARSER_NO_INLINE Statement* Parser::ParseAssert(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

#ifdef _DEBUG
  Debug(L"Assert", depth);
#endif

  NextToken();
  const int expr_line = GetLineNumber();
  const int expr_pos = GetLinePosition();
  Expression* expression = ParseExpression(depth + 1);
  if(!expression) {
    return TreeFactory::Instance()->MakeEmptyStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition());
  }
  const std::wstring source = AssertSourceText(expr_line, expr_pos, GetLineNumber(), GetLinePosition());

  // the label, which is the message of the failure
  Expression* label = nullptr;
  if(Match(TOKEN_COLON)) {
    NextToken();
    label = ParseExpression(depth + 1);
  }
  if(!label) {
    label = TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos);
  }
  const int end_line = GetLineNumber();
  const int end_pos = GetLinePosition();

  // a comparison shows both operands
  Expression* left_text = TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos);
  Expression* right_text = TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos);
  Expression* condition = expression;

  if(expression->GetExpressionType() != METHOD_CALL_EXPR && !expression->GetMethodCall() && !expression->GetCastType()) {
    switch(expression->GetExpressionType()) {
    case EQL_EXPR:
    case NEQL_EXPR:
    case LES_EXPR:
    case GTR_EXPR:
    case LES_EQL_EXPR:
    case GTR_EQL_EXPR: {
      CalculatedExpression* comparison = static_cast<CalculatedExpression*>(expression);
      Expression* operands[2] = { comparison->GetLeft(), comparison->GetRight() };
      Expression* texts[2] = { nullptr, nullptr };
      Expression* held[2] = { operands[0], operands[1] };

      for(int i = 0; i < 2; ++i) {
        // a 'Nil' operand has no type to hold in a local, and nothing to show
        if(!operands[i] || operands[i]->GetExpressionType() == NIL_LIT_EXPR) {
          continue;
        }

        // read-only operands are shown from a copy; anything else is held in a local
        Expression* copy = ClonePureOperand(operands[i], file_name, line_num, line_pos);
        if(copy) {
          texts[i] = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, ADD_EXPR,
                                                                      TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, L"", true), copy);
          continue;
        }

        const std::wstring tmp_ident = L"#assert_" + std::to_wstring(hidden_local_id++);
        SymbolEntry* tmp_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(tmp_ident),
                                                                          TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
        symbol_table->CurrentParseScope()->AddEntry(tmp_entry);

        Variable* tmp_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
        Assignment* tmp_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, end_line, end_pos, tmp_var, operands[i]);
        pending_statements.push_back(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, end_line, end_pos, tmp_entry, tmp_assign));

        held[i] = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident);
        texts[i] = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, ADD_EXPR,
                                                                    TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, L"", true),
                                                                    TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, tmp_ident));
      }

      if(held[0] != operands[0] || held[1] != operands[1]) {
        condition = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, comparison->GetExpressionType(), held[0], held[1]);
      }
      if(texts[0]) {
        left_text = texts[0];
      }
      if(texts[1]) {
        right_text = texts[1];
      }
    }
      break;

    default:
      break;
    }
  }

  // System.AssertCheck->Fail(where, source, label, left, right)
  const std::wstring where = file_name + L":" + std::to_wstring(line_num);
  ExpressionList* arguments = TreeFactory::Instance()->MakeExpressionList();
  arguments->AddExpression(TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, where, true));
  arguments->AddExpression(TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, source, true));
  arguments->AddExpression(label);
  arguments->AddExpression(left_text);
  arguments->AddExpression(right_text);
  MethodCall* fail_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos, end_line, end_pos,
                                                                  L"System.AssertCheck", L"Fail", arguments);
  // the block is a scope of its own, as an 'if' body is, so the analyzer's scopes stay in step
  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* fail_statements = TreeFactory::Instance()->MakeStatementList();
  fail_statements->AddStatement(fail_call);
  symbol_table->CurrentParseScope()->PreviousParseScope();

  // '(condition) = false'
  Expression* failed = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, EQL_EXPR, condition,
                                                                        TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, line_pos, false));
  return TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, end_line, end_pos, failed, fail_statements);
}

/****************************
 * Parses 'using(w := E) { ... }'. 'using' is contextual, as 'try' is: the
 * keyword only where the parenthesis that follows is closed by a '{', which a
 * call never is. The resource's name lives in a scope of its own around the
 * body, so two blocks in a method may both name theirs 'f'.
 *
 * What is closed is the value E gave, which the body's first (hidden)
 * statement keeps in '#using_res_N': the name is an ordinary variable and the
 * body may assign to it. The close used to read the name when the body was
 * left, so 'w := Nil;' in the body left the resource open and 'w := other;'
 * closed 'other' in its place.
 ****************************/
PARSER_NO_INLINE Using* Parser::ParseUsing(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

#ifdef _DEBUG
  Debug(L"Using", depth);
#endif

  NextToken();
  NextToken();

  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  const std::wstring name = scanner->GetToken()->GetIdentifier();
  NextToken();

  if(!Match(TOKEN_ASSIGN)) {
    ProcessError(L"Expected ':=' after the resource name in 'using'", TOKEN_ASSIGN);
  }
  NextToken();
  Expression* initial = ParseExpression(depth + 1);

  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();

  // scope of the resource: its declaration, the body, the close
  symbol_table->CurrentParseScope()->NewParseScope();
  // the error the body raised, held while the resource closes and raised again
  const std::wstring hidden_id = std::to_wstring(hidden_local_id++);
  const std::wstring raise_ident = L"#using_raise_" + hidden_id;
  SymbolEntry* raise_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(raise_ident),
                                                                      TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.Error", file_name, line_num, line_pos),
                                                                      false, current_method != nullptr);
  symbol_table->CurrentParseScope()->AddEntry(raise_entry);
  SymbolEntry* entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(name),
                                                                TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
  if(!symbol_table->CurrentParseScope()->AddEntry(entry)) {
    ProcessError(L"Variable already defined in this scope: '" + name + L"'");
  }
  Variable* variable = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, name);
  Assignment* assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable, initial);
  Statement* resource = TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), entry, assignment);

  // the value to close: '#using_res_N := w', the first statement of the body
  const std::wstring held_ident = L"#using_res_" + hidden_id;
  SymbolEntry* held_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(held_ident),
                                                                     TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
  symbol_table->CurrentParseScope()->AddEntry(held_entry);
  Assignment* hold = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, line_num, line_pos,
                                                             TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, held_ident),
                                                             TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, name));

  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* body = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();
  body->InsertStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, line_num, line_pos, held_entry, hold), 0);

  // if(#using_res_N <> Nil) { #using_res_N->Close(); } -- a Nil resource has nothing to close
  symbol_table->CurrentParseScope()->NewParseScope();   // the close list
  symbol_table->CurrentParseScope()->NewParseScope();   // the 'if' body
  StatementList* close_call = TreeFactory::Instance()->MakeStatementList();
  close_call->AddStatement(TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, line_num, line_pos, line_num, line_pos,
                                                                   held_ident, L"Close", TreeFactory::Instance()->MakeExpressionList()));
  symbol_table->CurrentParseScope()->PreviousParseScope();
  Expression* not_nil = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, NEQL_EXPR,
                                                                         TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, held_ident),
                                                                         TreeFactory::Instance()->MakeNilLiteral(file_name, line_num, line_pos));
  StatementList* close = TreeFactory::Instance()->MakeStatementList();
  close->AddStatement(TreeFactory::Instance()->MakeIf(file_name, line_num, line_pos, line_num, line_pos, not_nil, close_call));
  symbol_table->CurrentParseScope()->PreviousParseScope();
  symbol_table->CurrentParseScope()->PreviousParseScope();

  return TreeFactory::Instance()->MakeUsing(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), resource, body, close, raise_entry);
}

CriticalSection* Parser::ParseCritical(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Critical Section", depth);
#endif

  NextToken();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  // initialization statement
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  } 
  const std::wstring ident = scanner->GetToken()->GetIdentifier();
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  IdentifierContext ident_context(ident, line_num, line_pos);

  Variable* variable = ParseVariable(ident_context, depth + 1);

  NextToken();
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();

  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* statements = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();

  return TreeFactory::Instance()->MakeCriticalSection(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), variable, statements);
}

/****************************
 * Parses an 'each' statement
 ****************************/
For* Parser::ParseEach(bool reverse, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Each/Reverse", depth);
#endif

  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  // initialization statement
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  std::wstring count_ident = scanner->GetToken()->GetIdentifier();
  int line_pos = GetLinePosition() + (int)count_ident.size();
  NextToken();

  // colon with integer binding, assignment for variable binding
  bool is_bound = false;
  if(Match(TOKEN_ASSIGN) || Match(TOKEN_IN_ID)) {
    is_bound = true;
  }
  else if(!Match(TOKEN_COLON)) {
    ProcessError(L"Expected ':' or ':='", TOKEN_COLON);
  }
  NextToken();

  // add count entry
  Assignment* bind_assign = nullptr;
  std::wstring bound_ident;
  if(is_bound) {
    bound_ident = count_ident;
    count_ident = L'#' + count_ident + L"_index";
    
    const std::wstring bind_scope_name = GetScopeName(bound_ident);
    Type* bind_left_type = TypeFactory::Instance()->MakeType(VAR_TYPE);
    SymbolEntry* bind_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, bind_scope_name, 
                                                                       bind_left_type, false, current_method != nullptr);
#ifdef _DEBUG
    Debug(L"Adding bind variable: '" + bind_scope_name + L"'", depth + 2);
#endif
    if(!symbol_table->CurrentParseScope()->AddEntry(bind_entry)) {
      ProcessError(L"Variable already defined in this scope: '" + bound_ident + L"'");
    }
    Variable* bind_left_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, bound_ident);
    bind_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), 
                                                          GetLinePosition(), bind_left_var, nullptr);
  }

  // add count entry
  Type* count_type = nullptr;
  const std::wstring count_scope_name = GetScopeName(count_ident);
  if(Match(TOKEN_IDENT)) {
    const std::wstring ident_type = scanner->GetToken()->GetIdentifier();
    if(ident_type == L"CharRange" || ident_type == L"System.CharRange") {
      if(!is_bound) {
        count_type = TypeFactory::Instance()->MakeType(CHAR_TYPE);
      }
      else {
        ProcessError(L"'" + ident_type + L"' range index must be bound using the ':' operator");
      }
    }
    else if((ident_type == L"IntRange" || ident_type == L"System.IntRange")) {
      if(!is_bound) {
        count_type = TypeFactory::Instance()->MakeType(INT_TYPE);
      }
      else {
        ProcessError(L"'" + ident_type + L"' range index must be bound using the ':' operator");
      }
    }
    else if(ident_type == L"FloatRange" || ident_type == L"System.FloatRange") {
      if(!is_bound) {
        count_type = TypeFactory::Instance()->MakeType(FLOAT_TYPE);
      }
      else {
        ProcessError(L"'" + ident_type + L"' range index must be bound using the ':' operator");
      }
    }
  }

  if(!count_type) {
    count_type = TypeFactory::Instance()->MakeType(INT_TYPE);
  }
  SymbolEntry* count_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, count_scope_name, 
                                                                      count_type, false, current_method != nullptr);

#ifdef _DEBUG
  Debug(L"Adding count variable: '" + count_scope_name + L"'", depth + 2);
#endif
  if(!symbol_table->CurrentParseScope()->AddEntry(count_entry)) {
    ProcessError(L"Variable already defined in this scope: '" + count_ident + L"'");
  }

  StatementList* pre_statements = TreeFactory::Instance()->MakeStatementList();
  StatementList* update_stmts = TreeFactory::Instance()->MakeStatementList();

  CalculatedExpression* cond_expr = nullptr;
  StatementList* statements = nullptr;

  // reverse iteration 
  if(reverse) {
    Expression* left_pre_count = nullptr;
    switch(GetToken()) {
    case TOKEN_CHAR_LIT:
      left_pre_count = TreeFactory::Instance()->MakeCharacterLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetCharLit());
      NextToken();
      break;

    case TOKEN_INT_LIT:
      left_pre_count = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetInt64Lit());
      NextToken();
      break;

    case TOKEN_FLOAT_LIT:
      left_pre_count = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetFloatLit());
      NextToken();
      break;

    case TOKEN_IDENT: {
      left_pre_count = ParseExpression(depth + 1);
      if(left_pre_count->GetExpressionType() == VAR_EXPR) {
        Variable* variable = static_cast<Variable*>(left_pre_count);
        if(!variable->GetIndices()) {
          // set indices
          const std::wstring list_ident = variable->GetName();
          const int line_pos = GetLinePosition();
          left_pre_count = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), line_pos, -1, -1,
                                                                   list_ident, L"Size", TreeFactory::Instance()->MakeExpressionList());
        }
      }
      else {
        ProcessError(L"Expected variable or literal expression", TOKEN_SEMI_COLON);
      }
    }
      break;

    default:
      ProcessError(L"Expected variable or literal expression", TOKEN_SEMI_COLON);
      break;
    }

    // pre-condition
    Variable* count_left = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, count_ident);
    CalculatedExpression* pre_right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SUB_EXPR);
    pre_right->SetLeft(left_pre_count);
    pre_right->SetRight(TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1));
    Assignment* count_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), count_left, pre_right);
    pre_statements->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), count_entry, count_assign));

    // conditional expression
    Expression* count_right = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);
    Variable* list_left = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos - (int)count_ident.size(), count_ident);
    cond_expr = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, GTR_EQL_EXPR);
    cond_expr->SetLeft(list_left);
    cond_expr->SetRight(count_right);
    symbol_table->CurrentParseScope()->NewParseScope();

    // update statement
    Variable* update_left = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos - (int)count_ident.size(), count_ident);
    Expression* update_right = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1);
    update_stmts->AddStatement(TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, 
                               GetLineNumber(), GetLinePosition(), update_left, update_right, SUB_ASSIGN_STMT));
    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
    }
    NextToken();

    // statement list
    statements = ParseStatementList(depth + 1);
    symbol_table->CurrentParseScope()->PreviousParseScope();
    symbol_table->CurrentParseScope()->PreviousParseScope();
  }
  // forward iterator 
  else {
    Expression* left_pre_count = nullptr;
    switch(GetToken()) {
    case TOKEN_CHAR_LIT:
      if(is_bound) {
        ProcessError(L"index variable must be bound using the ':' operator");
      }
      else {
        left_pre_count = TreeFactory::Instance()->MakeCharacterLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetCharLit());
      }
      NextToken();
      break;

    case TOKEN_INT_LIT:
      if(is_bound) {
        ProcessError(L"index variable must be bound using the ':' operator");
      }
      else {
        left_pre_count = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetInt64Lit());
      }
      NextToken();
      break;

    case TOKEN_FLOAT_LIT:
      if(is_bound) {
        ProcessError(L"index variable must be bound using the ':' operator");
      }
      else {
        left_pre_count = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num, line_pos, scanner->GetToken()->GetFloatLit());
      }
      NextToken();
      break;

    // a bound collection that is not a plain variable: a literal, or a call
    // chain starting from one (#1184)
    case TOKEN_CHAR_STRING_LIT:
    case TOKEN_OPEN_BRACKET:
      if(is_bound) {
        left_pre_count = HoistEachCollection(ParseExpression(depth + 1), bound_ident, pre_statements, line_num, line_pos, depth);
      }
      else {
        ProcessError(L"Expected variable or literal expression", TOKEN_SEMI_COLON);
        NextToken();
      }
      break;

    case TOKEN_IDENT: {
      left_pre_count = ParseExpression(depth + 1);
      if(left_pre_count && left_pre_count->GetExpressionType() == VAR_EXPR) {
        Variable* variable = static_cast<Variable*>(left_pre_count);
        if(!variable->GetIndices()) {
          // set method call to 'x->Size()'
          const int line_pos = GetLinePosition();
          left_pre_count = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), line_pos, -1, -1,
                                                                   variable->GetName(), L"Size", TreeFactory::Instance()->MakeExpressionList());
        }
      }
      // A bound each over a call result, `each(s in list->Get())`: the call
      // was compared with the index as if it were the size. Ranges cannot be
      // bound, so a bound call is always a collection (#1184).
      else if(is_bound && left_pre_count && left_pre_count->GetExpressionType() == METHOD_CALL_EXPR) {
        left_pre_count = HoistEachCollection(left_pre_count, bound_ident, pre_statements, line_num, line_pos, depth);
      }
      else if(left_pre_count && left_pre_count->GetExpressionType() == METHOD_CALL_EXPR) {
        // add count entry
        const std::wstring count_scope_name = GetScopeName(L'#' + count_ident + L"_range");
        Type* count_type = TypeFactory::Instance()->MakeType(CLASS_TYPE);
        SymbolEntry* count_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, count_scope_name,
                                                                            count_type, false, current_method != nullptr);
#ifdef _DEBUG
        Debug(L"Adding count variable: '" + count_scope_name + L"'", depth + 2);
#endif
        if(!symbol_table->CurrentParseScope()->AddEntry(count_entry)) {
          ProcessError(L"Variable already defined in this scope: '" + count_ident + L"'");
        }
      }
      else if(left_pre_count && left_pre_count->GetExpressionType() != METHOD_CALL_EXPR) {
        ProcessError(L"Expected variable or literal expression", TOKEN_SEMI_COLON);
      }
    }
      break;

    default:
      ProcessError(L"Expected variable or literal expression", TOKEN_SEMI_COLON);
      NextToken();
      break;
    }

    // pre-condition
    Variable* count_left = TreeFactory::Instance()->MakeVariable(file_name, line_num, 1, count_ident);
    Expression* count_right = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 0);
    Assignment* count_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), count_left, count_right);
    pre_statements->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), count_entry, count_assign));

    // conditional expression
    Variable* list_left = TreeFactory::Instance()->MakeVariable(file_name, line_num, 1, count_ident);
    cond_expr = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, LES_EXPR);
    cond_expr->SetLeft(list_left);
    cond_expr->SetRight(left_pre_count);
    symbol_table->CurrentParseScope()->NewParseScope();

    // update statement
    Variable* update_left = TreeFactory::Instance()->MakeVariable(file_name, line_num, 1, count_ident);
    Expression* update_right = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, 1);
    update_stmts->AddStatement(TreeFactory::Instance()->MakeOperationAssignment(file_name, line_num, line_pos, 
                               GetLineNumber(), GetLinePosition(), update_left, update_right, ADD_ASSIGN_STMT));
    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
    }
    NextToken();

    // statement list
    statements = ParseStatementList(depth + 1);
    symbol_table->CurrentParseScope()->PreviousParseScope();
    symbol_table->CurrentParseScope()->PreviousParseScope();
  }

  if(is_bound) {
    return TreeFactory::Instance()->MakeFor(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), pre_statements, cond_expr, update_stmts, bind_assign, statements);
  }

  return TreeFactory::Instance()->MakeFor(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), pre_statements, cond_expr, update_stmts, statements);
}

/****************************
 * Evaluates an 'each' collection expression once, into a hidden local
 * '#<bound>_coll', and returns '#<bound>_coll->Size()' for the loop
 * condition; the analyzer then binds the element exactly as it does for a
 * named collection.
 ****************************/
Expression* Parser::HoistEachCollection(Expression* collection, const std::wstring& bound_ident, StatementList* pre_statements,
                                        const int line_num, const int line_pos, int depth)
{
  const std::wstring file_name = GetFileName();
  if(!collection) {
    return nullptr;
  }

  const std::wstring coll_ident = L'#' + bound_ident + L"_coll";
  SymbolEntry* coll_entry = TreeFactory::Instance()->MakeSymbolEntry(file_name, line_num, line_pos, GetScopeName(coll_ident),
                                                                     TypeFactory::Instance()->MakeType(VAR_TYPE), false, current_method != nullptr);
  if(!symbol_table->CurrentParseScope()->AddEntry(coll_entry)) {
    ProcessError(L"Variable already defined in this scope: '" + coll_ident + L"'");
  }

  Variable* coll_var = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, coll_ident);
  Assignment* coll_assign = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), coll_var, collection);
  pre_statements->AddStatement(TreeFactory::Instance()->MakeDeclaration(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), coll_entry, coll_assign));

  const int size_pos = GetLinePosition();
  return TreeFactory::Instance()->MakeMethodCall(file_name, line_num, size_pos, GetLineNumber(), size_pos, -1, -1,
                                                 coll_ident, L"Size", TreeFactory::Instance()->MakeExpressionList());
}

/****************************
 * Parses a 'for' statement
 ****************************/
For* Parser::ParseFor(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"For", depth);
#endif

  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();
  
  // pre-statement
  StatementList* pre_statements = TreeFactory::Instance()->MakeStatementList();
  while(Match(TOKEN_IDENT) && !is_semi_colon) {
    Statement* pre_statement = ParseStatement(depth + 1);
    TakePendingStatements(pre_statements);
    pre_statements->AddStatement(pre_statement);
    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
  }
  is_semi_colon = false;

  // conditional
  Expression* cond_expr = ParseExpression(depth + 1);
  if(!Match(TOKEN_SEMI_COLON)) {
    ProcessError(L"Expected ';'", TOKEN_SEMI_COLON);
  }
  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  
  // update statement
  StatementList* update_stmts = TreeFactory::Instance()->MakeStatementList();
  while(Match(TOKEN_IDENT) && !is_semi_colon) {
    Statement* update_stmt = ParseStatement(depth + 1, false);
    TakePendingStatements(update_stmts);
    update_stmts->AddStatement(update_stmt);
    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
  }
  is_semi_colon = false;

  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();
  
  // statement list
  StatementList* statements = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();
  symbol_table->CurrentParseScope()->PreviousParseScope();

  return TreeFactory::Instance()->MakeFor(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), pre_statements, 
                                          cond_expr, update_stmts, statements);
}

/****************************
 * Parses a 'select' statement
 ****************************/
Select* Parser::ParseSelect(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Select", depth);
#endif

  NextToken();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  Expression* eval_expression = ParseExpression(depth + 1);
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();

  Variable* variable = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos - 1, L"#");
  Assignment* eval_assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos - 1, -1, -1, variable, eval_expression);

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_BRACE);
  }
  NextToken();

  StatementList* other = nullptr;
  std::vector<StatementList*> statement_lists;
  std::map<ExpressionList*, StatementList*> statement_map;
  while((Match(TOKEN_LABEL_ID) || Match(TOKEN_OTHER_ID))) {
    bool is_other_label = false;
    ExpressionList* labels = TreeFactory::Instance()->MakeExpressionList();
    // parse labels
    while((Match(TOKEN_LABEL_ID) || Match(TOKEN_OTHER_ID))) {
      if(Match(TOKEN_LABEL_ID)) {
        NextToken();
        labels->AddExpression(ParseSimpleExpression(depth + 1));

        while(Match(TOKEN_COMMA)) {
          NextToken();
          labels->AddExpression(ParseSimpleExpression(depth + 1));
        }

        if(Match(TOKEN_COLON)) {
          NextToken();
        }
      }
      else {
        if(is_other_label) {
          ProcessError(L"Duplicate 'other' label", TOKEN_OTHER_ID);
        }
        is_other_label = true;
        NextToken();
        
        if(Match(TOKEN_COLON)) {
          NextToken();
        }
      }
    }

    // parse statements
    symbol_table->CurrentParseScope()->NewParseScope();
    StatementList* statements = ParseStatementList(depth + 1);
    symbol_table->CurrentParseScope()->PreviousParseScope();

    // 'other' label
    if(is_other_label) {
      if(!other) {
        other = statements;
      }
      else {
        ProcessError(L"Duplicate 'other' label", TOKEN_CLOSED_BRACE);
      }
    }
    // named label
    else {
      statement_map.insert(std::pair<ExpressionList*, StatementList*>(labels, statements));
    }

    // note: order matters during contextual analysis due 
    // to the way the nested symbol table manages scope
    statement_lists.push_back(statements);
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  return TreeFactory::Instance()->MakeSelect(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), eval_assignment, statement_map, statement_lists, other);
}

/****************************
 * Whether 'match' starts here: the word, then the start of its subject. 'match'
 * is a keyword only where what follows cannot follow a name, and stays an
 * ordinary name everywhere else ('match := 1', 'match->Size()', 'match(x)',
 * 'match[0]', 'match + 1'):
 *   - a name: 'match color {'. Two names in a row start nothing else.
 *   - a number, character or string: 'match 1 {'. Nor does a name and a literal.
 *   - a '(' whose matching ')' is followed by '{': 'match (value) {'. A call is
 *     never followed by a '{' (as for 'using'), so 'match(x);', 'if(match(x)) {'
 *     and 'match(x)->Size()' are calls of a method named 'match', as before.
 * A subject that starts with '(' and goes on after its ')' ('match (a) + b {')
 * is not told from a call: write it inside one pair of parentheses.
 ****************************/
PARSER_NO_INLINE bool Parser::IsMatchStart()
{
  if(!Match(TOKEN_IDENT)) {
    return false;
  }

  switch(GetToken(SECOND_INDEX)) {
  case TOKEN_IDENT:
  case TOKEN_INT_LIT:
  case TOKEN_FLOAT_LIT:
  case TOKEN_CHAR_LIT:
  case TOKEN_CHAR_STRING_LIT:
    return scanner->GetToken()->GetIdentifier() == L"match";

  case TOKEN_OPEN_PAREN:
    return scanner->GetToken()->GetIdentifier() == L"match" && scanner->PeekAfterParens(2) == TOKEN_OPEN_BRACE;

  default:
    return false;
  }
}

/****************************
 * Parses 'match subject { pattern | pattern => arm, ..., other => arm }'.
 * It is a 'select' on the subject whose labels are checked: for an enum
 * subject they are items of that enum, and without 'other' they name every
 * item. As a statement an arm is a block. As a value an arm is an expression,
 * assigned to a hidden local that 'value_ident' then names. Where a statement
 * starts the first arm decides which it is, so 'match c { A => "a", other =>
 * "b" }->PrintLine();' is a value like any other expression statement.
 ****************************/
PARSER_NO_INLINE Select* Parser::ParseMatch(bool is_statement, std::wstring& value_ident, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();
  bool is_value = !is_statement;
  bool is_first = true;
  if(is_value) {
    AddHiddenLocal(L"match", value_ident, line_num, line_pos);
  }

#ifdef _DEBUG
  Debug(L"Match", depth);
#endif

  NextToken();
  Expression* eval_expression = ParseExpression(depth + 1);
  Variable* variable = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, L"#");
  Assignment* eval_assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, -1, -1, variable, eval_expression);

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  StatementList* other = nullptr;
  std::vector<StatementList*> statement_lists;
  std::map<ExpressionList*, StatementList*> statement_map;
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    const size_t consumed = tokens_consumed;
    const int arm_line_num = GetLineNumber();
    const int arm_line_pos = GetLinePosition();

    // patterns
    bool is_other = false;
    ExpressionList* labels = TreeFactory::Instance()->MakeExpressionList();
    if(Match(TOKEN_OTHER_ID)) {
      is_other = true;
      NextToken();
    }
    else {
      labels->AddExpression(ParseSimpleExpression(depth + 1));
      while(Match(TOKEN_OR)) {
        NextToken();
        labels->AddExpression(ParseSimpleExpression(depth + 1));
      }
    }

    if(!Match(TOKEN_LAMBDA)) {
      ProcessError(L"Expected '=>' after a 'match' pattern");
    }
    else {
      NextToken();
    }

    // arm
    if(is_first && is_statement && !Match(TOKEN_OPEN_BRACE)) {
      is_value = true;
      AddHiddenLocal(L"match", value_ident, line_num, line_pos);
    }
    is_first = false;

    StatementList* statements;
    if(is_value) {
      statements = MakeValueArm(value_ident, ParseExpression(depth + 1), arm_line_num, arm_line_pos);
    }
    else if(Match(TOKEN_OPEN_BRACE)) {
      symbol_table->CurrentParseScope()->NewParseScope();
      statements = ParseStatementList(depth + 1);
      symbol_table->CurrentParseScope()->PreviousParseScope();
    }
    else {
      ProcessError(L"An arm of a 'match' statement is a block: 'pattern => { ... }'", TOKEN_CLOSED_BRACE);
      statements = TreeFactory::Instance()->MakeStatementList();
    }

    if(is_other) {
      if(other) {
        ProcessError(L"Duplicate 'other' arm");
      }
      else {
        other = statements;
      }
    }
    else {
      statement_map.insert(std::pair<ExpressionList*, StatementList*>(labels, statements));
    }
    // note: order matters during contextual analysis (see ParseSelect)
    statement_lists.push_back(statements);

    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
    else if(is_value && !Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected ',' or '}' after a 'match' arm");
    }

    // an arm that read nothing would be read again from the same token
    if(tokens_consumed == consumed) {
      NextToken();
    }
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  if(statement_lists.empty()) {
    ProcessError(L"A 'match' needs at least one arm: 'pattern => ...'");
  }

  Select* select = TreeFactory::Instance()->MakeSelect(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), eval_assignment, statement_map, statement_lists, other);
  select->SetChecked(L"match");
  if(is_value) {
    select->SetValue();
  }

  return select;
}

/****************************
 * Parses 'match' where a value is expected
 ****************************/
PARSER_NO_INLINE Expression* Parser::ParseMatchExpression(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();

  std::wstring value_ident;
  Select* select = ParseMatch(false, value_ident, depth + 1);
  return MakeValueSelect(select, value_ident, line_num, line_pos);
}

/****************************
 * Parses 'select' where a value is expected: a table of values,
 * 'select(subject) { label a, b: value  other: value }'. The arms are
 * expressions; apart from the spelling it is 'match' used as a value.
 ****************************/
PARSER_NO_INLINE Expression* Parser::ParseSelectExpression(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Select value", depth);
#endif

  std::wstring value_ident;
  AddHiddenLocal(L"select", value_ident, line_num, line_pos);

  NextToken();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  Expression* eval_expression = ParseExpression(depth + 1);
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();

  Variable* variable = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, L"#");
  Assignment* eval_assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, -1, -1, variable, eval_expression);

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  StatementList* other = nullptr;
  std::vector<StatementList*> statement_lists;
  std::map<ExpressionList*, StatementList*> statement_map;
  while(Match(TOKEN_LABEL_ID) || Match(TOKEN_OTHER_ID)) {
    const int arm_line_num = GetLineNumber();
    const int arm_line_pos = GetLinePosition();

    // labels
    bool is_other = false;
    ExpressionList* labels = TreeFactory::Instance()->MakeExpressionList();
    while(Match(TOKEN_LABEL_ID) || Match(TOKEN_OTHER_ID)) {
      if(Match(TOKEN_LABEL_ID)) {
        NextToken();
        labels->AddExpression(ParseSimpleExpression(depth + 1));
        while(Match(TOKEN_COMMA)) {
          NextToken();
          labels->AddExpression(ParseSimpleExpression(depth + 1));
        }
      }
      else {
        if(is_other) {
          ProcessError(L"Duplicate 'other' label");
        }
        is_other = true;
        NextToken();
      }

      if(!Match(TOKEN_COLON)) {
        ProcessError(L"Expected ':' after the label of a 'select' value");
      }
      else {
        NextToken();
      }
    }

    // value
    if(Match(TOKEN_OPEN_BRACE)) {
      ProcessError(L"A 'select' used as a value takes a value after each label, not a block: 'label 1: \"one\"'", TOKEN_CLOSED_BRACE);
    }
    StatementList* statements = MakeValueArm(value_ident, ParseExpression(depth + 1), arm_line_num, arm_line_pos);
    if(Match(TOKEN_SEMI_COLON) || Match(TOKEN_COMMA)) {
      NextToken();
    }

    if(is_other) {
      if(other) {
        ProcessError(L"Duplicate 'other' label");
      }
      else {
        other = statements;
      }
    }
    else {
      statement_map.insert(std::pair<ExpressionList*, StatementList*>(labels, statements));
    }
    statement_lists.push_back(statements);
  }

  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  if(statement_lists.empty()) {
    ProcessError(L"A 'select' used as a value needs at least one label: 'label 1: \"one\"'");
  }

  Select* select = TreeFactory::Instance()->MakeSelect(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), eval_assignment, statement_map, statement_lists, other);
  select->SetChecked(L"select");
  select->SetValue();

  return MakeValueSelect(select, value_ident, line_num, line_pos);
}

/****************************
 * One arm of a 'match' or 'select' used as a value: 'value_ident := value'
 ****************************/
StatementList* Parser::MakeValueArm(const std::wstring& value_ident, Expression* value, int line_num, int line_pos)
{
  const std::wstring file_name = GetFileName();
  StatementList* statements = TreeFactory::Instance()->MakeStatementList();
  if(value) {
    Variable* target = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, value_ident);
    statements->AddStatement(TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), target, value));
  }

  return statements;
}

/****************************
 * The value of a 'match' or 'select': the select runs where the expression is
 * evaluated, then the hidden local its arms assigned is read
 ****************************/
Expression* Parser::MakeValueSelect(Select* select, const std::wstring& value_ident, int line_num, int line_pos)
{
  const std::wstring file_name = GetFileName();
  Variable* value = TreeFactory::Instance()->MakeVariable(file_name, line_num, line_pos, value_ident);
  SequenceExpression* sequence = TreeFactory::Instance()->MakeSequenceExpression(file_name, line_num, line_pos, value);
  sequence->AddStatement(select);

  return sequence;
}

/****************************
 * Parses a return statement
 ****************************/
Return* Parser::ParseReturn(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Return", depth);
#endif

  NextToken();
  Expression* expression = nullptr;
  if(!Match(TOKEN_SEMI_COLON)) {
    expression = ParseExpression(depth + 1);
  }

  return TreeFactory::Instance()->MakeReturn(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), expression);
}

/****************************
 * Parses a leaving block
 ****************************/
Leaving* Parser::ParseLeaving(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Leaving", depth);
#endif

  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* statements = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();

  return TreeFactory::Instance()->MakeLeaving(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), statements);
}

/****************************
 * Parses an assignment statement
 ****************************/
Assignment* Parser::ParseAssignment(Variable* variable, int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Assignment", depth);
#endif
  std::stack<Expression*> expressions;
  expressions.push(variable);

  NextToken();
  Expression* expression = ParseExpression(depth + 1);
  if(!expression) {
    return nullptr;
  }
  expressions.push(expression);

  while(Match(TOKEN_ASSIGN)) {
    NextToken();

    expression = ParseExpression(depth + 1);
    if(!expression) {
      return nullptr;
    }
    expressions.push(expression);
  }

  Assignment* assignment = nullptr;
  while(!expressions.empty()) {
    Expression* right = expressions.top();
    expressions.pop();

    Expression* left = expressions.top();
    if(left->GetExpressionType() != VAR_EXPR) {
      ProcessError(L"Expected variable in statement", TOKEN_SEMI_COLON);
    }

    Variable* variable = static_cast<Variable*>(left);
    assignment = TreeFactory::Instance()->MakeAssignment(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(), assignment, variable, right);

    if(expressions.size() == 1) {
      expressions.pop();
    }
  }

  if(nil_guard_bail && assignment) {
    trailing_statements.push_back(MakeNilGuard(assignment->GetVariable(), nil_guard_bail));
    nil_guard_bail = nullptr;
  }

  return assignment;
}

/****************************
 * Parses a data type identifier.
 ****************************/
Type* Parser::ParseType(int depth)
{
  CheckDepth(depth);
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Data Type", depth);
#endif

  Type* type = nullptr;
  switch(GetToken()) {
  case TOKEN_BACK_SLASH: {
    NextToken();
    std::wstring alias_name = ParseBundleName();
    if(Match(TOKEN_ASSESSOR)) {
      NextToken();
      alias_name += L"#";
      alias_name += ParseBundleName();
      type = TypeFactory::Instance()->MakeType(ALIAS_TYPE, alias_name, file_name, line_num, line_pos);
    }
    else {
      // '\Name' alone names nothing: no type was made and none was reported, so a method
      // declared '~ \String' reached the analyzer with no return type, which it reads
      ProcessError(L"Expected '->' after the alias group: an alias type is written '\\Group->Name'", TOKEN_ASSESSOR);
    }
  }
    break;

  case TOKEN_BYTE_ID:
    type = TypeFactory::Instance()->MakeType(BYTE_TYPE, file_name, line_num, line_pos);
    NextToken();
    break;

  case TOKEN_INT_ID:
    type = TypeFactory::Instance()->MakeType(INT_TYPE, file_name, line_num, line_pos);
    NextToken();
    break;

  case TOKEN_FLOAT_ID:
    type = TypeFactory::Instance()->MakeType(FLOAT_TYPE, file_name, line_num, line_pos);
    NextToken();
    break;

  case TOKEN_CHAR_ID:
    type = TypeFactory::Instance()->MakeType(CHAR_TYPE, file_name, line_num, line_pos);
    NextToken();
    break;

  case TOKEN_NIL_ID:
    type = TypeFactory::Instance()->MakeType(NIL_TYPE, file_name, line_num, line_pos);
    NextToken();
    break;

  case TOKEN_BOOLEAN_ID:
    type = TypeFactory::Instance()->MakeType(BOOLEAN_TYPE, file_name, line_num, line_pos);
    NextToken();
    break;

  case TOKEN_IDENT: {
    std::wstring ident = ParseBundleName();
    if(Match(TOKEN_ASSESSOR)) {
      NextToken();
      ident += L"#";
      ident += ParseBundleName();
    }
    // a method type parameter is its bound, marked with its name (#1242)
    std::map<std::wstring, std::wstring>::iterator method_generic = method_generic_bounds.find(ident);
    if(method_generic != method_generic_bounds.end()) {
      type = TypeFactory::Instance()->MakeType(CLASS_TYPE, method_generic->second, file_name, line_num, line_pos);
      type->SetMethodGeneric(ident);
    }
    else {
      type = TypeFactory::Instance()->MakeType(CLASS_TYPE, ident, file_name, line_num, line_pos);
    }
  }
    break;

  case TOKEN_OPEN_PAREN: {
    NextToken();

    std::vector<Type*> func_params;
    while(!Match(TOKEN_CLOSED_PAREN) && !Match(TOKEN_END_OF_STREAM)) {
      Type* param = ParseType(depth + 1);
      if(param) {
        func_params.push_back(param);
      }

      if(Match(TOKEN_COMMA)) {
        NextToken();
      }
      else if(!Match(TOKEN_CLOSED_PAREN)) {
        ProcessError(L"Expected ',' or ')'", TOKEN_CLOSED_BRACE);
        NextToken();
      }
    }
    NextToken();

    if(!Match(TOKEN_TILDE)) {
      ProcessError(L"Expected '~'", TOKEN_TILDE);
    }
    NextToken();

    Type* func_rtrn = ParseType(depth + 1);

    return TypeFactory::Instance()->MakeType(func_params, func_rtrn);
  }
    break;

  default:
    ProcessError(L"Expected type", TOKEN_SEMI_COLON);
    break;
  }

  if(type) {
    // dimension
    int dimension = 0;
    if(Match(TOKEN_OPEN_BRACKET)) {
      NextToken();
      dimension++;
      while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
        dimension++;
        if(Match(TOKEN_COMMA)) {
          NextToken();
        }
        else if(!Match(TOKEN_CLOSED_BRACKET)) {
          ProcessError(L"Expected ',' or ';'", TOKEN_SEMI_COLON);
          NextToken();
        }
      }

      if(!Match(TOKEN_CLOSED_BRACKET)) {
        ProcessError(L"Expected ']'", TOKEN_CLOSED_BRACKET);
      }
      NextToken();
    }

    // 'Int[][]': an array of the row type '$Int[]' (#1246). Every level is
    // one-dimensional in this phase.
    int levels = dimension > 0 ? 1 : 0;
    while(dimension == 1 && Match(TOKEN_OPEN_BRACKET) && Match(TOKEN_CLOSED_BRACKET, SECOND_INDEX)) {
      NextToken();
      NextToken();
      levels++;
    }
    if(levels > 1) {
      type = TypeFactory::Instance()->MakeType(CLASS_TYPE, RowTypeName(RowBaseName(type), levels - 1), file_name, line_num, line_pos);
      dimension = 1;
    }
    else if(dimension > 1 && Match(TOKEN_OPEN_BRACKET) && Match(TOKEN_CLOSED_BRACKET, SECOND_INDEX)) {
      ProcessError(L"A jagged array's levels must each be one-dimensional, e.g. 'Int[][]'", TOKEN_SEMI_COLON);
    }
    type->SetDimension(dimension);

    // generic
    if(Match(TOKEN_LES)) {
      const std::vector<Type*> generic_types = ParseGenericTypes(depth);
      type->SetGenerics(generic_types);
    }
  }

  return type;
}

/****************************
 * Parses an expression from a text string.
 * Used for string interpolation expressions.
 ****************************/
Expression* Parser::ParseExpressionText(const std::wstring& text, const std::wstring& filename, [[maybe_unused]] int line_num, [[maybe_unused]] int line_pos, Class* context_class,
                                        std::wstring* reason)
{
  // Create scanner on the expression text
  Scanner* expr_scanner = new Scanner(filename, false, text);

  // Create minimal Parser instance
  std::vector<std::pair<std::wstring, std::wstring>> empty_programs;
  Parser expr_parser(L"", false, empty_programs);

  // Set the scanner
  expr_parser.scanner = expr_scanner;

  // Set the class context so function calls can be resolved
  expr_parser.current_class = context_class;

  // Initialize tokens
  expr_parser.NextToken();

  // Check for empty expression
  if(expr_parser.Match(TOKEN_END_OF_STREAM)) {
    delete expr_scanner;
    expr_parser.scanner = nullptr;
    expr_parser.program = nullptr;
    return nullptr;
  }

  // Parse the expression. One with more chained operators than a statement may have is given
  // up by an exception (CheckChain), which nothing between here and the compiler's last
  // resort caught: obc ended with "internal error: unexpected exception during compilation"
  // and this scanner was not released. The error has been reported to this parser, so the
  // interpolation is refused below with it as the reason, like any other that does not parse.
  Expression* result = nullptr;
  try {
    result = expr_parser.ParseExpression(0);
  }
  catch(const NestingTooDeep&) {
    result = nullptr;
  }

  // A token the scanner rejected is an error even where the parse recovered from it
  // (a malformed literal stands in as a literal): the interpolation is not valid.
  // So is a syntax error the parser reported and stepped over: '{$n->Size()?}' reads as
  // a conditional with no branches, and the tree it leaves has a part missing. That
  // tree used to be handed to the analyzer, which ended the compiler with an access
  // violation on the missing part.
  if(expr_parser.lexical_error_seen || expr_parser.diagnostics.HasErrors()) {
    if(reason) {
      const std::vector<Diagnostic> errors = expr_parser.diagnostics.UniqueErrors();
      if(!errors.empty()) {
        *reason = errors[0].message;
        // the text the scanner was given is the interpolation, not the file
        const std::wstring end_of_file = L"end of file";
        const size_t at = reason->find(end_of_file);
        if(at != std::wstring::npos) {
          reason->replace(at, end_of_file.size(), L"the end of the interpolation");
        }
      }
    }
    delete expr_scanner;
    expr_parser.scanner = nullptr;
    expr_parser.program = nullptr;
    return nullptr;
  }

  // Check that we consumed the entire expression
  if(!expr_parser.Match(TOKEN_END_OF_STREAM)) {
    delete expr_scanner;
    expr_parser.scanner = nullptr;
    expr_parser.program = nullptr;
    return nullptr;
  }

  // Clean up scanner
  delete expr_scanner;
  expr_parser.scanner = nullptr;

  // The expression above was allocated into whichever program is bound on this
  // thread -- the OUTER one being parsed -- because the temporary parser never
  // calls Parse() and so never binds its own. The nodes are therefore already
  // owned by the right program, and only the temporary's empty program is
  // dropped here. Before per-program factories this line stopped ~ParsedProgram
  // from clearing the one shared factory and destroying the main AST.
  expr_parser.program = nullptr;

  return result;
}

/****************************************************************************
 * MCP tools.
 *
 * A function whose doc comment ('#~ ... ~#') has a line starting with '@tool' is an MCP tool
 * (docs/design/MCP_FIRST_CLASS.md). Once every file has been read (so that a record or an enum declared after
 * the class, or in a later file, is known) the parser writes two kinds of ordinary static functions into the
 * class, as source text parsed by this same parser, exactly as the code a person would write:
 *
 *   McpTools(mcp_service : McpService) ~ Nil        declares every tool of the class (schema, hints)
 *   McpTool_<name>(mcp_call : McpCall) ~ McpToolResult   reads the typed arguments, calls the function
 *   McpRead_<n>(mcp_j : Data.JSON.JsonElement) ~ <type>     reads an array, Vector, Map, enum or record argument
 *
 * A class with no '@tool' function gets none of this, and nothing here runs for it. The generated code needs
 * the mcp library (-lib mcp); a program that tags a function without it gets the compiler's usual error about
 * the missing class, reported at the tagged function.
 ****************************************************************************/
namespace {

  struct McpDoc {
    std::wstring description;
    std::wstring name;
    std::wstring title;
    bool read_only = false;
    bool destructive = false;
    bool idempotent = false;
    bool open_world = false;
    int kind = 0;               // 0 '@tool', 1 '@resource', 2 '@prompt'
    std::wstring uri;           // the URI template of a '@resource'
    std::wstring mime;          // the media type of a '@resource'
    std::vector<std::pair<std::wstring, std::wstring> > params;
    std::vector<int> param_lines;
  };

  // the text of a string literal: the quotes are the caller's
  std::wstring McpLit(const std::wstring& s)
  {
    std::wstring out;
    for(size_t i = 0; i < s.size(); ++i) {
      const wchar_t c = s[i];
      if(c == L'\\') {
        out += L"\\\\";
      }
      else if(c == L'"') {
        out += L"\\\"";
      }
      else if(c == L'$') {
        out += L"\\$";
      }
      else if(c == L'\n') {
        out += L"\\n";
      }
      else if(c == L'\r' || c == L'\t') {
        out += L' ';
      }
      else {
        out += c;
      }
    }
    return out;
  }

  // the text of a JSON string, quotes included
  std::wstring McpJsonString(const std::wstring& s)
  {
    std::wstring out = L"\"";
    for(size_t i = 0; i < s.size(); ++i) {
      const wchar_t c = s[i];
      if(c == L'\\' || c == L'"') {
        out += L'\\';
        out += c;
      }
      else if(c < 32) {
        out += L' ';
      }
      else {
        out += c;
      }
    }
    return out + L"\"";
  }

  std::wstring McpTrim(const std::wstring& s)
  {
    size_t b = 0;
    size_t e = s.size();
    while(b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) {
      ++b;
    }
    while(e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) {
      --e;
    }
    return s.substr(b, e - b);
  }

  bool McpToolNameChars(const std::wstring& s)
  {
    if(s.empty()) {
      return false;
    }
    for(size_t i = 0; i < s.size(); ++i) {
      const wchar_t c = s[i];
      if(!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.')) {
        return false;
      }
    }
    return true;
  }

  // ReadFile becomes read_file, GetHTTPStatus becomes get_httpstatus
  std::wstring McpSnake(const std::wstring& s)
  {
    std::wstring out;
    for(size_t i = 0; i < s.size(); ++i) {
      const wchar_t c = s[i];
      if(c >= L'A' && c <= L'Z') {
        if(i > 0 && ((s[i - 1] >= L'a' && s[i - 1] <= L'z') || (s[i - 1] >= L'0' && s[i - 1] <= L'9'))) {
          out += L'_';
        }
        out += (wchar_t)(c - L'A' + L'a');
      }
      else {
        out += c;
      }
    }
    return out;
  }

  std::wstring McpAfterColon(const std::wstring& s)
  {
    const size_t index = s.find_last_of(L':');
    return index == std::wstring::npos ? s : s.substr(index + 1);
  }

  bool McpIsName(const std::wstring& name, const wchar_t* a, const wchar_t* b)
  {
    return name == a || name == b;
  }

  // the JSON Schema of a type, as JSON text
  std::wstring McpSchemaText(const McpTy& t)
  {
    switch(t.kind) {
    case MK_INT:
      return L"{\"type\":\"integer\"}";
    case MK_FLOAT:
      return L"{\"type\":\"number\"}";
    case MK_BOOL:
      return L"{\"type\":\"boolean\"}";
    case MK_BYTE:
      return L"{\"type\":\"integer\",\"minimum\":0,\"maximum\":255}";
    case MK_CHAR:
      return L"{\"type\":\"string\",\"minLength\":1,\"maxLength\":1}";
    case MK_STRING:
      return L"{\"type\":\"string\"}";
    case MK_JSON:
      return L"{}";
    case MK_ENUM: {
      std::wstring out = L"{\"type\":\"string\",\"enum\":[";
      for(size_t i = 0; i < t.items.size(); ++i) {
        out += (i ? L"," : L"") + McpJsonString(t.items[i]);
      }
      return out + L"]}";
    }
    case MK_RECORD: {
      std::wstring props;
      std::wstring required;
      for(size_t i = 0; i < t.items.size(); ++i) {
        props += (i ? L"," : L"") + McpJsonString(t.items[i]) + L":" + McpSchemaText(t.kids[i]);
        required += (i ? L"," : L"") + McpJsonString(t.items[i]);
      }
      return L"{\"type\":\"object\",\"properties\":{" + props + L"},\"required\":[" + required + L"]}";
    }
    case MK_JSONCLS: {
      std::wstring props;
      std::wstring required;
      for(size_t i = 0; i < t.items.size(); ++i) {
        const bool optional = i < t.optional.size() && t.optional[i];
        // an optional field may be left out or be null (a model often writes null for what it does not know)
        props += (i ? L"," : L"") + McpJsonString(t.items[i]) + L":" +
                 (optional ? L"{\"anyOf\":[" + McpSchemaText(t.kids[i]) + L",{\"type\":\"null\"}]}" : McpSchemaText(t.kids[i]));
        if(!optional) {
          required += (required.empty() ? L"" : L",") + McpJsonString(t.items[i]);
        }
      }
      return L"{\"type\":\"object\",\"properties\":{" + props + L"},\"required\":[" + required + L"]}";
    }
    case MK_ARRAY:
    case MK_VECTOR:
      return L"{\"type\":\"array\",\"items\":" + McpSchemaText(t.kids[0]) + L"}";
    case MK_MAP:
      return L"{\"type\":\"object\",\"additionalProperties\":" + McpSchemaText(t.kids[0]) + L"}";
    default:
      return L"{}";
    }
  }

  // what the helper functions of one class are called, so one is written once per type
  struct McpHelpers {
    std::wstring cls;
    std::wstring prefix = L"McpRead_";                        // 'class : json' writes JsonRead_, so a class with both has no clash
    std::map<std::wstring, std::wstring> names;               // type text -> function name
    std::vector<std::pair<std::wstring, std::wstring> > code; // function name -> source text
    int count = 0;
  };

  std::wstring McpRead(const McpTy& t, const std::wstring& j, McpHelpers& helpers);

  // the source text of a function that reads one type from JSON
  std::wstring McpHelper(const McpTy& t, McpHelpers& helpers)
  {
    const std::wstring key = t.text + L"|" + std::to_wstring((int)t.kind) + (t.boxed ? L"b" : L"");
    std::map<std::wstring, std::wstring>::iterator found = helpers.names.find(key);
    if(found != helpers.names.end()) {
      return found->second;
    }

    const std::wstring name = helpers.prefix + std::to_wstring(++helpers.count);
    helpers.names[key] = name;

    const std::wstring head = L"function : " + name + L"(mcp_j : Data.JSON.JsonElement) ~ " + t.text + L" { ";
    std::wstring body;
    switch(t.kind) {
    case MK_ARRAY: {
      body = L"mcp_r := " + t.kids[0].text + L"->New[mcp_j->Size()]; for(mcp_i := 0; mcp_i < mcp_j->Size(); mcp_i += 1;) { mcp_r[mcp_i] := " +
             McpRead(t.kids[0], L"mcp_j->Get(mcp_i)", helpers) + L"; }; return mcp_r; }";
      break;
    }

    case MK_VECTOR:
      body = L"mcp_r := Collection.Vector->New()<" + t.kids[0].text + L">; for(mcp_i := 0; mcp_i < mcp_j->Size(); mcp_i += 1;) { mcp_r->AddBack(" +
             McpRead(t.kids[0], L"mcp_j->Get(mcp_i)", helpers) + L"); }; return mcp_r; }";
      break;

    case MK_MAP:
      body = L"mcp_r := Collection.Map->New()<String, " + t.kids[0].text + L">; each(mcp_k in mcp_j->GetNames()) { mcp_r->Insert(mcp_k, " +
             McpRead(t.kids[0], L"mcp_j->Get(mcp_k)", helpers) + L"); }; return mcp_r; }";
      break;

    case MK_ENUM:
      body = L"mcp_s := mcp_j->GetString(); ";
      for(size_t i = 0; i < t.items.size(); ++i) {
        body += L"if(mcp_s->Equals(\"" + McpLit(t.items[i]) + L"\")) { return " + t.name + L"->" + t.items[i] + L"; }; ";
      }
      body += L"return " + t.name + L"->" + t.items[0] + L"; }";
      break;

    case MK_RECORD: {
      std::wstring args;
      for(size_t i = 0; i < t.items.size(); ++i) {
        body += L"if(<>mcp_j->Has(\"" + McpLit(t.items[i]) + L"\")) { Runtime->Raise(Error->New(\"missing field '" + McpLit(t.items[i]) +
                L"' of " + McpLit(t.name) + L"\")); }; ";
        args += (i ? L", " : L"") + McpRead(t.kids[i], L"mcp_j->Get(\"" + McpLit(t.items[i]) + L"\")", helpers);
      }
      body += L"return " + t.name + L"->New(" + args + L"); }";
      break;
    }

    default:
      body = L"return Nil; }";
      break;
    }

    helpers.code.push_back(std::make_pair(name, head + body));
    return name;
  }

  // an expression of type t that reads JSON expression j
  std::wstring McpRead(const McpTy& t, const std::wstring& j, McpHelpers& helpers)
  {
    std::wstring value;
    switch(t.kind) {
    case MK_INT:
      value = j + L"->GetInt()";
      if(t.boxed) {
        return L"IntRef->New(" + value + L")";
      }
      return value;
    case MK_FLOAT:
      value = j + L"->GetFloat()";
      if(t.boxed) {
        return L"FloatRef->New(" + value + L")";
      }
      return value;
    case MK_BOOL:
      value = L"(" + j + L"->GetType() = Data.JSON.JsonElement->JsonType->TRUE)";
      if(t.boxed) {
        return L"BoolRef->New(" + value + L")";
      }
      return value;
    case MK_BYTE:
      value = j + L"->GetInt()->As(Byte)";
      if(t.boxed) {
        return L"ByteRef->New(" + value + L")";
      }
      return value;
    case MK_CHAR:
      value = j + L"->GetString()->Get(0)";
      if(t.boxed) {
        return L"CharRef->New(" + value + L")";
      }
      return value;
    case MK_STRING:
      return j + L"->GetString()";
    case MK_JSON:
      return j;
    case MK_JSONCLS:
      return t.name + L"->FromJson(" + j + L")";
    default:
      return helpers.cls + L"->" + McpHelper(t, helpers) + L"(" + j + L")";
    }
  }
}

// the doc comment of a tool: the description, the name, the hints and the parameters' descriptions. False,
// with the problem reported, when a tag is malformed.
static bool McpParseDoc(const ToolDoc& doc, const std::wstring& function_name, const std::wstring& file_name, DiagnosticSink& diagnostics, McpDoc& out)
{
  bool ok = true;
  bool saw_tool = false;
  bool saw_kind = false;  // one of '@tool', '@resource', '@prompt' has been read
  int current = 0;       // 0 description, 1 a parameter, 2 ignored
  int line_no = 0;
  size_t pos = 0;
  const std::wstring& text = doc.text;
  while(pos <= text.size()) {
    size_t end = text.find(L'\n', pos);
    if(end == std::wstring::npos) {
      end = text.size();
    }
    const std::wstring line = McpTrim(text.substr(pos, end - pos));
    const int at_line = doc.line + line_no;
    pos = end + 1;
    ++line_no;

    if(!line.empty() && line[0] == L'@') {
      size_t word_end = 1;
      while(word_end < line.size() && line[word_end] != L' ' && line[word_end] != L'\t') {
        ++word_end;
      }
      const std::wstring tag = line.substr(1, word_end - 1);
      const std::wstring rest = McpTrim(line.substr(word_end));

      if(tag == L"resource" || tag == L"prompt") {
        if(saw_kind) {
          diagnostics.Error(file_name, at_line, doc.pos, L"a function is one of '@tool', '@resource' and '@prompt'; the doc comment of '" + function_name + L"' has more than one");
          ok = false;
        }
        saw_kind = true;
        current = 2;
        if(tag == L"resource") {
          out.kind = 1;
          out.uri = rest;
          if(rest.empty()) {
            diagnostics.Error(file_name, at_line, doc.pos, L"'@resource' needs a URI or a URI template, as in '@resource weather://{city}'");
            ok = false;
          }
        }
        else {
          out.kind = 2;
          if(!rest.empty()) {
            if(!McpToolNameChars(rest)) {
              diagnostics.Error(file_name, at_line, doc.pos, L"'@prompt " + rest + L"': a prompt name has letters, digits, '_', '-' and '.' only");
              ok = false;
            }
            out.name = rest;
          }
        }
      }
      else if(tag == L"mime") {
        current = 2;
        out.mime = rest;
      }
      else if(tag == L"tool") {
        if(saw_tool) {
          diagnostics.Error(file_name, at_line, doc.pos, L"'@tool' appears twice in the doc comment of '" + function_name + L"'");
          ok = false;
        }
        else if(saw_kind) {
          diagnostics.Error(file_name, at_line, doc.pos, L"a function is one of '@tool', '@resource' and '@prompt'; the doc comment of '" + function_name + L"' has more than one");
          ok = false;
        }
        saw_tool = true;
        saw_kind = true;
        current = 2;
        if(!rest.empty()) {
          if(!McpToolNameChars(rest)) {
            diagnostics.Error(file_name, at_line, doc.pos, L"'@tool " + rest + L"': a tool name has letters, digits, '_', '-' and '.' only");
            ok = false;
          }
          out.name = rest;
        }
      }
      else if(tag == L"readonly" || tag == L"destructive" || tag == L"idempotent" || tag == L"openworld") {
        current = 2;
        if(!rest.empty()) {
          diagnostics.Error(file_name, at_line, doc.pos, L"'@" + tag + L"' takes no text; found '" + rest + L"'");
          ok = false;
        }
        if(tag == L"readonly") {
          out.read_only = true;
        }
        else if(tag == L"destructive") {
          out.destructive = true;
        }
        else if(tag == L"idempotent") {
          out.idempotent = true;
        }
        else {
          out.open_world = true;
        }
      }
      else if(tag == L"title") {
        current = 2;
        out.title = rest;
      }
      else if(tag == L"param") {
        current = 1;
        size_t name_end = 0;
        while(name_end < rest.size() && rest[name_end] != L' ' && rest[name_end] != L'\t') {
          ++name_end;
        }
        if(name_end == 0) {
          diagnostics.Error(file_name, at_line, doc.pos, L"'@param' needs the name of a parameter");
          ok = false;
          current = 2;
        }
        else {
          out.params.push_back(std::make_pair(rest.substr(0, name_end), McpTrim(rest.substr(name_end))));
          out.param_lines.push_back(at_line);
        }
      }
      else {
        // @return and the rest of the documentation tags are not part of a tool
        current = 2;
      }
    }
    else if(!line.empty()) {
      if(current == 0) {
        out.description += (out.description.empty() ? L"" : L" ") + line;
      }
      else if(current == 1 && !out.params.empty()) {
        std::wstring& desc = out.params.back().second;
        desc += (desc.empty() ? L"" : L" ") + line;
      }
    }
    else if(current == 1) {
      // a blank line ends a parameter's description
      current = 2;
    }
  }

  if(out.read_only && out.destructive) {
    diagnostics.Error(file_name, doc.line, doc.pos, L"'@readonly' and '@destructive' contradict each other in the doc comment of '" + function_name + L"'");
    ok = false;
  }

  if(out.name.empty()) {
    out.name = McpSnake(function_name);
  }
  return ok;
}

Enum* Parser::McpFindEnum(const std::wstring& name)
{
  if(current_bundle && !current_bundle->GetName().empty()) {
    Enum* found = current_bundle->GetEnum(current_bundle->GetName() + L"." + name);
    if(found) {
      return found;
    }
  }
  if(current_class) {
    Enum* found = current_bundle->GetEnum(current_class->GetName() + L"#" + name);
    if(found) {
      return found;
    }
  }
  if(current_bundle) {
    Enum* found = current_bundle->GetEnum(name);
    if(found) {
      return found;
    }
  }

  const size_t dot = name.find_last_of(L'.');
  if(dot != std::wstring::npos) {
    ParsedBundle* bundle = program->GetBundle(name.substr(0, dot));
    if(bundle) {
      return bundle->GetEnum(name);
    }
  }
  return nullptr;
}

Class* Parser::McpFindClass(const std::wstring& name)
{
  if(current_bundle && !current_bundle->GetName().empty()) {
    Class* found = current_bundle->GetClass(current_bundle->GetName() + L"." + name);
    if(found) {
      return found;
    }
  }
  if(current_bundle) {
    Class* found = current_bundle->GetClass(name);
    if(found) {
      return found;
    }
  }

  const size_t dot = name.find_last_of(L'.');
  if(dot != std::wstring::npos) {
    ParsedBundle* bundle = program->GetBundle(name.substr(0, dot));
    if(bundle) {
      return bundle->GetClass(name);
    }
  }
  return nullptr;
}

// A type's name as it is written in source. An enum declared in a class, 'Palette->Color', is held as
// 'Palette#Color'; written back that way, the '#' would start a comment in the code generated below.
static std::wstring McpSourceName(const std::wstring& name)
{
  std::wstring out;
  for(size_t i = 0; i < name.size(); ++i) {
    if(name[i] == L'#') {
      out += L"->";
    }
    else {
      out += name[i];
    }
  }
  return out;
}

// the type as source text: Int, String, Vector<String>, Point[]
static std::wstring McpTypeText(Type* type)
{
  std::wstring text;
  switch(type->GetType()) {
  case INT_TYPE:
    text = L"Int";
    break;
  case FLOAT_TYPE:
    text = L"Float";
    break;
  case BOOLEAN_TYPE:
    text = L"Bool";
    break;
  case BYTE_TYPE:
    text = L"Byte";
    break;
  case CHAR_TYPE:
    text = L"Char";
    break;
  default:
    text = McpSourceName(type->GetName());
    break;
  }

  const std::vector<Type*> generics = type->GetGenerics();
  if(!generics.empty()) {
    text += L"<";
    for(size_t i = 0; i < generics.size(); ++i) {
      text += (i ? L"," : L"") + McpTypeText(generics[i]);
    }
    text += L">";
  }
  return text;
}

// The fields of a record or of a 'class : json' class: their names, types and (json) which are optional.
bool Parser::McpAnalyzeFields(Class* record, McpTy& element, std::wstring& why, int depth)
{
  const std::wstring name = record->GetName();
  std::vector<Statement*> statements = record->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    if(!statements[i] || statements[i]->GetStatementType() != DECLARATION_STMT) {
      continue;
    }
    std::vector<Declaration*> group;
    Declaration* declaration = static_cast<Declaration*>(statements[i]);
    while(declaration) {
      SymbolEntry* entry = declaration->GetEntry();
      if(entry && !entry->IsStatic()) {
        group.push_back(declaration);
      }
      declaration = declaration->GetChild();
    }
    for(size_t g = group.size(); g > 0; --g) {
      SymbolEntry* entry = group[g - 1]->GetEntry();
      std::wstring field = McpAfterColon(entry->GetName());
      if(!field.empty() && field[0] == L'@') {
        field = field.substr(1);
      }
      McpTy kid;
      std::wstring kid_why;
      if(!McpAnalyze(entry->GetType(), kid, kid_why, false, depth + 1)) {
        if(kid_why.empty()) {
          // the field's type is in a file with a syntax error, which is the answer
          why.clear();
          return false;
        }
        if(record->IsJson()) {
          for(size_t at = kid_why.find(L"tool parameter"); at != std::wstring::npos; at = kid_why.find(L"tool parameter", at)) {
            kid_why.replace(at, 14, L"JSON field");
          }
        }
        why = L"the field '" + field + L"' of the " + (record->IsJson() ? L"class '" : L"record '") + name + L"': " + kid_why;
        return false;
      }
      if(kid.kind == MK_CALL) {
        why = L"the field '" + field + L"' of the " + (record->IsJson() ? L"class '" : L"record '") + name + L"' cannot be a McpCall";
        return false;
      }
      element.items.push_back(field);
      element.kids.push_back(std::move(kid));
      if(record->IsJson()) {
        const std::vector<std::wstring>& optional = record->GetJsonOptional();
        element.optional.push_back(std::find(optional.begin(), optional.end(), field) != optional.end());
      }
    }
  }
  if(element.items.empty()) {
    why = L"the " + std::wstring(record->IsJson() ? L"class '" : L"record '") + name + L"' has no fields";
    return false;
  }
  return true;
}

// Reads what MCP needs to know of a type. False, with the reason in 'why', for a type that cannot be a tool's
// parameter. 'in_collection' is true for the element of a Vector or Map, which holds IntRef, not Int.
bool Parser::McpAnalyze(Type* type, McpTy& out, std::wstring& why, bool in_collection, int depth)
{
  if(depth > 8) {
    why = L"the type is nested too deeply";
    return false;
  }

  out = McpTy();
  const int dimension = type->GetDimension();
  if(dimension > 1) {
    why = L"an array of two or more dimensions cannot be a tool parameter; use an array of one dimension or a Vector";
    return false;
  }

  McpTy element;
  bool known = true;
  switch(type->GetType()) {
  case INT_TYPE:
    element.kind = MK_INT;
    break;
  case FLOAT_TYPE:
    element.kind = MK_FLOAT;
    break;
  case BOOLEAN_TYPE:
    element.kind = MK_BOOL;
    break;
  case BYTE_TYPE:
    element.kind = MK_BYTE;
    break;
  case CHAR_TYPE:
    element.kind = MK_CHAR;
    break;
  case FUNC_TYPE:
    why = L"a function type cannot be a tool parameter";
    return false;
  case CLASS_TYPE:
    known = false;
    break;
  default:
    why = L"this type cannot be a tool parameter";
    return false;
  }

  element.text = McpTypeText(type);

  if(!known) {
    const std::wstring name = type->GetName();
    const std::vector<Type*> generics = type->GetGenerics();
    element.name = McpSourceName(name);
    if(McpIsName(name, L"String", L"System.String")) {
      element.kind = MK_STRING;
    }
    else if(McpIsName(name, L"JsonElement", L"Data.JSON.JsonElement")) {
      element.kind = MK_JSON;
    }
    else if(McpIsName(name, L"McpCall", L"API.MCP.McpCall")) {
      element.kind = MK_CALL;
    }
    else if(in_collection && McpIsName(name, L"IntRef", L"System.IntRef")) {
      element.kind = MK_INT;
      element.boxed = true;
    }
    else if(in_collection && McpIsName(name, L"FloatRef", L"System.FloatRef")) {
      element.kind = MK_FLOAT;
      element.boxed = true;
    }
    else if(in_collection && McpIsName(name, L"BoolRef", L"System.BoolRef")) {
      element.kind = MK_BOOL;
      element.boxed = true;
    }
    else if(in_collection && McpIsName(name, L"ByteRef", L"System.ByteRef")) {
      element.kind = MK_BYTE;
      element.boxed = true;
    }
    else if(in_collection && McpIsName(name, L"CharRef", L"System.CharRef")) {
      element.kind = MK_CHAR;
      element.boxed = true;
    }
    else if(McpIsName(name, L"Vector", L"Collection.Vector")) {
      if(generics.size() != 1) {
        why = L"a Vector needs its element type, as in Vector<String>";
        return false;
      }
      element.kind = MK_VECTOR;
      McpTy kid;
      if(!McpAnalyze(generics[0], kid, why, true, depth + 1)) {
        return false;
      }
      element.kids.push_back(std::move(kid));
    }
    else if(McpIsName(name, L"Map", L"Collection.Map")) {
      if(generics.size() != 2 || !McpIsName(generics[0]->GetName(), L"String", L"System.String")) {
        why = L"a Map can be a tool parameter only with String keys, as in Map<String,IntRef>";
        return false;
      }
      element.kind = MK_MAP;
      McpTy kid;
      if(!McpAnalyze(generics[1], kid, why, true, depth + 1)) {
        return false;
      }
      element.kids.push_back(std::move(kid));
    }
    else if(Enum* eenum = McpFindEnum(name)) {
      element.kind = MK_ENUM;
      std::vector<EnumItem*> sorted;
      std::map<const std::wstring, EnumItem*> items = eenum->GetItems();
      for(std::map<const std::wstring, EnumItem*>::iterator item = items.begin(); item != items.end(); ++item) {
        sorted.push_back(item->second);
      }
      std::sort(sorted.begin(), sorted.end(), [](EnumItem* a, EnumItem* b) { return a->GetId() < b->GetId(); });
      for(size_t i = 0; i < sorted.size(); ++i) {
        element.items.push_back(McpAfterColon(sorted[i]->GetName()));
      }
      if(element.items.empty()) {
        why = L"the enum '" + name + L"' has no items";
        return false;
      }
    }
    else if(Class* record = McpFindClass(name)) {
      if(mcp_broken_files.find(record->GetFileName()) != mcp_broken_files.end()) {
        // its file has a syntax error, which is the answer: the class may be half built
        why.clear();
        return false;
      }
      if(!record->IsRecord() && !record->IsJson()) {
        why = L"the class '" + name + L"' cannot be a tool parameter: declare it as a record or as 'class " + name + L" : json', or take a JsonElement";
        return false;
      }
      element.kind = record->IsJson() ? MK_JSONCLS : MK_RECORD;
      // A class may be declared below the class that holds it, so a class can now be reached from
      // itself. A schema describes a value of a known shape: one that contains itself has none.
      if(std::find(mcp_class_stack.begin(), mcp_class_stack.end(), record) != mcp_class_stack.end()) {
        why = L"the class '" + name + L"' holds itself, directly or through its fields, and a JSON Schema describes a value of a known depth: use a JsonElement for the part that repeats";
        return false;
      }
      mcp_class_stack.push_back(record);
      const bool fields_ok = McpAnalyzeFields(record, element, why, depth);
      mcp_class_stack.pop_back();
      if(!fields_ok) {
        return false;
      }
    }
    else if(!mcp_broken_files.empty()) {
      // the type may be declared in a file that did not parse: that file's syntax error is the answer
      why.clear();
      return false;
    }
    else {
      why = L"the type '" + name + L"' cannot be a tool parameter: use Int, Float, Bool, Byte, Char, String, JsonElement, an enum, a record, or an array, Vector or Map of those (a class must be a record, or 'class X : json')";
      return false;
    }
  }

  if(dimension == 1) {
    if(element.kind == MK_CALL || element.kind == MK_ENUM) {
      why = element.kind == MK_ENUM ? L"an array of an enum is not supported; use an array of String" : L"an array of McpCall is not a tool parameter";
      return false;
    }
    out.kind = MK_ARRAY;
    out.text = element.text + L"[]";
    out.kids.push_back(std::move(element));
    return true;
  }

  if(in_collection && (element.kind == MK_ENUM)) {
    why = L"an enum cannot be the element of a Vector or Map; use String";
    return false;
  }
  out = std::move(element);
  return true;
}

// parses one generated function and adds it to the class
void Parser::McpAddFunction(Class* klass, const std::wstring& code, int line, int depth, const wchar_t* needed_by)
{
  std::wstring text(line > 1 ? line - 1 : 0, L'\n');
  text += code;

  ScannerScope function_scanner(scanner);
  StateScope<Method*> function_method(current_method);
  scanner = new Scanner(klass->GetFileName(), alt_syntax, std::move(text));
  NextToken();

  const std::wstring owner = needed_by ? needed_by : L"an '@tool' function";
  Method* method = nullptr;
  if(Match(TOKEN_FUNCTION_ID)) {
    method = ParseMethod(true, false, depth + 1);
  }
  else if(needed_by && Match(TOKEN_METHOD_ID)) {
    method = ParseMethod(false, false, depth + 1);
  }
  if(!Match(TOKEN_END_OF_STREAM)) {
    ProcessError(L"The code written for " + owner + L" has an extra '" + scanner->DescribeToken(scanner->GetToken()) + L"'");
  }

  function_scanner.Restore();
  current_method = function_method.saved;

  if(method && !klass->AddMethod(method)) {
    diagnostics.Error(klass->GetFileName(), line, 1, L"The class already has a function named '" + McpAfterColon(method->GetName()) + L"', which " + owner + L" needs for itself");
  }
}

// Derives the tools of every class that has some, and the members of every 'class : json', once every file
// has been read: a parameter's or a field's record, enum or json class may be declared below the class that
// takes it, or in a later file. A file with a syntax error is left
// alone (its functions may be half built, and the syntax error is the answer); the other files are not.
void Parser::GeneratePendingMcpTools()
{
  ParsedBundle* previous_bundle = current_bundle;
  SymbolTableManager* previous_table = symbol_table;
  for(size_t i = 0; i < mcp_pending.size(); ++i) {
    McpPending& pending = mcp_pending[i];
    if(mcp_broken_files.find(pending.klass->GetFileName()) != mcp_broken_files.end()) {
      continue;
    }

    current_bundle = pending.bundle;
    symbol_table = pending.bundle->GetSymbolTableManager();
    current_class = pending.klass;
    std::vector<std::wstring>* previous_uses = current_lib_uses;
    current_lib_uses = &pending.lib_uses;
    try {
      if(pending.is_json) {
        GenerateJsonClass(pending.klass, pending.json_line, 1);
      }
      if(!pending.tools.empty()) {
        GenerateMcpTools(pending.klass, pending.tools, pending.uses_mcp, 1);
      }
    }
    catch(const NestingTooDeep&) {
      // reported where it was found
    }
    current_lib_uses = previous_uses;
    current_class = nullptr;
  }
  mcp_pending.clear();
  current_bundle = previous_bundle;
  symbol_table = previous_table;
}

void Parser::GenerateMcpTools(Class* klass, std::vector<McpCandidate>& tools, bool uses_mcp, int depth)
{
  McpHelpers helpers;
  helpers.cls = klass->GetName();
  const std::wstring file_name = klass->GetFileName();

  // the code written below names API.MCP classes, whose generic arguments resolve by their short names: the file must use the bundle
  if(!uses_mcp) {
    diagnostics.Error(file_name, tools[0].method->GetLineNumber(), tools[0].method->GetLinePosition(),
                      L"a file with an '@tool' function needs 'use API.MCP;' (and -lib mcp): the tool is registered with that library's classes");
    return;
  }

  std::vector<std::wstring> registrations;
  std::vector<std::wstring> adapters;
  std::vector<std::wstring> names_seen;
  int first_line = 0;

  for(size_t t = 0; t < tools.size(); ++t) {
    Method* method = tools[t].method;
    const std::wstring function_name = McpAfterColon(method->GetName());
    const int fn_line = method->GetLineNumber();
    const int fn_pos = method->GetLinePosition();
    if(!first_line) {
      first_line = fn_line;
    }

    McpDoc doc;
    if(!McpParseDoc(tools[t].doc, function_name, file_name, diagnostics, doc)) {
      continue;
    }
    if(!doc.title.empty() && doc.title.size() > 200) {
      diagnostics.Error(file_name, tools[t].doc.line, tools[t].doc.pos, L"the '@title' of '" + function_name + L"' is longer than 200 characters");
      continue;
    }

    const wchar_t* kind_word = doc.kind == 0 ? L"tool" : (doc.kind == 1 ? L"resource" : L"prompt");
    if(doc.kind != 0 && (doc.read_only || doc.destructive || doc.idempotent || doc.open_world)) {
      diagnostics.Error(file_name, tools[t].doc.line, tools[t].doc.pos, L"'@readonly', '@destructive', '@idempotent' and '@openworld' describe a tool; '" + function_name + L"' is a " + kind_word);
      continue;
    }
    if(doc.kind != 1 && !doc.mime.empty()) {
      diagnostics.Error(file_name, tools[t].doc.line, tools[t].doc.pos, L"'@mime' belongs to a '@resource'; '" + function_name + L"' is a " + kind_word);
      continue;
    }

    bool duplicate = false;
    const std::wstring name_key = std::wstring(1, L'0' + (wchar_t)doc.kind) + (doc.kind == 1 ? doc.uri : doc.name);
    for(size_t n = 0; n < names_seen.size(); ++n) {
      if(names_seen[n] == name_key) {
        duplicate = true;
      }
    }
    if(duplicate) {
      diagnostics.Error(file_name, fn_line, fn_pos, std::wstring(L"two '@") + kind_word + L"' functions of this class are called '" + (doc.kind == 1 ? doc.uri : doc.name) + L"'" +
                        (doc.kind == 1 ? L"" : std::wstring(L"; give one a name with '@") + kind_word + L" <name>'"));
      continue;
    }
    names_seen.push_back(name_key);

    // the variables of a resource's URI template
    std::vector<std::wstring> uri_vars;
    if(doc.kind == 1) {
      size_t at = 0;
      bool bad_uri = false;
      while((at = doc.uri.find(L'{', at)) != std::wstring::npos) {
        const size_t close = doc.uri.find(L'}', at);
        if(close == std::wstring::npos || close == at + 1) {
          bad_uri = true;
          break;
        }
        uri_vars.push_back(doc.uri.substr(at + 1, close - at - 1));
        at = close + 1;
      }
      if(bad_uri || (doc.uri.find(L'}', 0) != std::wstring::npos && uri_vars.empty())) {
        diagnostics.Error(file_name, tools[t].doc.line, tools[t].doc.pos, L"'@resource " + doc.uri + L"': a '{' needs a name and a closing '}'");
        continue;
      }
    }

    // the parameters, in source order
    struct Param {
      std::wstring name;
      McpTy ty;
      Declaration* declaration;
    };
    std::vector<Param> params;
    bool ok = true;
    std::vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
    for(size_t d = 0; d < declarations.size() && ok; ++d) {
      std::vector<Declaration*> group;
      Declaration* declaration = declarations[d];
      while(declaration) {
        group.push_back(declaration);
        declaration = declaration->GetChild();
      }
      for(size_t g = group.size(); g > 0 && ok; --g) {
        Declaration* parameter = group[g - 1];
        SymbolEntry* entry = parameter->GetEntry();
        if(!entry) {
          continue;
        }

        Param param;
        param.name = McpAfterColon(entry->GetName());
        param.declaration = parameter;
        if(entry->IsReference()) {
          diagnostics.Error(file_name, parameter->GetLineNumber(), parameter->GetLinePosition(), L"the 'ref' parameter '" + param.name + L"' of the tool '" + doc.name + L"' cannot be filled from JSON");
          ok = false;
          break;
        }

        std::wstring why;
        if(!McpAnalyze(entry->GetType(), param.ty, why, false, 0)) {
          if(!why.empty()) {
            diagnostics.Error(file_name, parameter->GetLineNumber(), parameter->GetLinePosition(), L"the parameter '" + param.name + L"' of the tool '" + doc.name + L"': " + why);
          }
          ok = false;
          break;
        }
        if(doc.kind != 0 && !(param.ty.kind == MK_INT || param.ty.kind == MK_FLOAT || param.ty.kind == MK_BOOL || param.ty.kind == MK_CHAR || param.ty.kind == MK_STRING ||
                              (doc.kind == 2 && param.ty.kind == MK_CALL))) {
          diagnostics.Error(file_name, parameter->GetLineNumber(), parameter->GetLinePosition(),
                            L"the parameter '" + param.name + L"' of the " + kind_word + L" '" + (doc.kind == 1 ? doc.uri : doc.name) + L"': a " + kind_word +
                            L" gets its arguments as text, so a parameter is a String, Int, Float, Bool or Char" + (doc.kind == 2 ? L" (or the McpCall)" : L""));
          ok = false;
          break;
        }
        if(doc.kind == 1) {
          if(param.ty.kind == MK_CALL || std::find(uri_vars.begin(), uri_vars.end(), param.name) == uri_vars.end()) {
            diagnostics.Error(file_name, parameter->GetLineNumber(), parameter->GetLinePosition(),
                              L"the parameter '" + param.name + L"' of the resource '" + doc.uri + L"' is not a variable of its URI; a resource reads its parameters from '{name}' in the URI");
            ok = false;
            break;
          }
          if(parameter->GetAssignment()) {
            diagnostics.Error(file_name, parameter->GetLineNumber(), parameter->GetLinePosition(),
                              L"the parameter '" + param.name + L"' of the resource '" + doc.uri + L"' has a default; every variable of a URI template is required");
            ok = false;
            break;
          }
        }
        params.push_back(std::move(param));
      }
    }
    if(!ok) {
      continue;
    }
    if(doc.kind == 1) {
      for(size_t v = 0; v < uri_vars.size() && ok; ++v) {
        bool has = false;
        for(size_t q = 0; q < params.size(); ++q) {
          if(params[q].name == uri_vars[v]) {
            has = true;
          }
        }
        if(!has) {
          diagnostics.Error(file_name, fn_line, fn_pos, L"the URI '" + doc.uri + L"' has the variable '{" + uri_vars[v] + L"}', but the function '" + function_name + L"' has no parameter of that name");
          ok = false;
        }
      }
      if(!ok) {
        continue;
      }
    }

    // a '@param' line names a parameter the function has
    for(size_t p = 0; p < doc.params.size(); ++p) {
      bool found = false;
      for(size_t q = 0; q < params.size(); ++q) {
        if(params[q].name == doc.params[p].first && params[q].ty.kind != MK_CALL) {
          found = true;
        }
      }
      if(!found) {
        diagnostics.Error(file_name, doc.param_lines[p], tools[t].doc.pos, L"'@param " + doc.params[p].first + L"': the tool '" + doc.name + L"' has no such parameter");
        ok = false;
      }
    }
    if(!ok) {
      continue;
    }

    // the return type decides how the result is sent
    Type* rtrn = method->GetReturn();
    McpTy rty;
    int rkind = -1;  // 0 nil, 1 text from a value, 2 a String, 3 a JsonElement, 4 a McpToolResult
    if(rtrn->GetDimension() == 0) {
      switch(rtrn->GetType()) {
      case NIL_TYPE:
        rkind = 0;
        break;
      case INT_TYPE:
      case FLOAT_TYPE:
      case BOOLEAN_TYPE:
        rkind = 1;
        break;
      case CLASS_TYPE: {
        const std::wstring rname = rtrn->GetName();
        if(McpIsName(rname, L"String", L"System.String")) {
          rkind = 2;
        }
        else if(McpIsName(rname, L"JsonElement", L"Data.JSON.JsonElement")) {
          rkind = 3;
        }
        else if(doc.kind == 0 && McpIsName(rname, L"McpToolResult", L"API.MCP.McpToolResult")) {
          rkind = 4;
        }
        else if(doc.kind == 1 && McpIsName(rname, L"McpContents", L"API.MCP.McpContents")) {
          rkind = 4;
        }
        else if(doc.kind == 2 && McpIsName(rname, L"McpPromptResult", L"API.MCP.McpPromptResult")) {
          rkind = 4;
        }
        break;
      }
      default:
        break;
      }
    }
    if(doc.kind != 0 && rkind != 2 && rkind != 4) {
      rkind = -1;
    }
    if(rkind < 0) {
      if(doc.kind == 1) {
        diagnostics.Error(file_name, fn_line, fn_pos, L"the resource '" + doc.uri + L"' returns a type MCP cannot send: return a String (its text) or a McpContents");
      }
      else if(doc.kind == 2) {
        diagnostics.Error(file_name, fn_line, fn_pos, L"the prompt '" + doc.name + L"' returns a type MCP cannot send: return a String (one user message) or a McpPromptResult");
      }
      else {
        diagnostics.Error(file_name, fn_line, fn_pos, L"the tool '" + doc.name + L"' returns a type MCP cannot send: return String, Int, Float, Bool, JsonElement, McpToolResult or nothing");
      }
      continue;
    }

    // the declaration of the tool
    std::wstring spec = L"API.MCP.McpToolSpec->New(\"" + McpLit(doc.name) + L"\", \"" + McpLit(doc.description) + L"\")";
    if(!doc.title.empty()) {
      spec += L"->Title(\"" + McpLit(doc.title) + L"\")";
    }

    // a prompt is declared by statements, one variable for each
    const std::wstring prompt_var = L"mcp_pr" + std::to_wstring(registrations.size());
    std::wstring prompt_args;

    // and the function that calls the tool
    std::wstring body = L"mcp_a := mcp_call->GetArguments(); ";
    if(doc.kind == 1) {
      body = L"mcp_a := API.MCP.McpUriTemplate->Match(\"" + McpLit(doc.uri) + L"\", mcp_uri); if(mcp_a = Nil) { return Nil; }; ";
    }
    std::wstring call_args;
    for(size_t p = 0; p < params.size() && ok; ++p) {
      const std::wstring index = std::to_wstring(p);
      const std::wstring var = L"mcp_p" + index;
      call_args += (p ? L", " : L"");

      if(params[p].ty.kind == MK_CALL) {
        call_args += L"mcp_call";
        continue;
      }
      call_args += var;

      std::wstring description;
      for(size_t q = 0; q < doc.params.size(); ++q) {
        if(doc.params[q].first == params[p].name) {
          description = doc.params[q].second;
        }
      }

      Assignment* default_value = params[p].declaration->GetAssignment();
      const bool required = default_value == nullptr;
      if(doc.kind == 0) {
        spec += L"->AddSchema(\"" + McpLit(params[p].name) + L"\", \"" + McpLit(McpSchemaText(params[p].ty)) + L"\", \"" + McpLit(description) + L"\", " +
                (required ? L"true" : L"false") + L")";
      }
      else if(doc.kind == 2) {
        prompt_args += prompt_var + L"->AddArgument(\"" + McpLit(params[p].name) + L"\", \"" + McpLit(description) + L"\", " + (required ? L"true" : L"false") + L"); ";
      }

      std::wstring read;
      if(doc.kind == 0) {
        read = McpRead(params[p].ty, L"mcp_a->Get(\"" + McpLit(params[p].name) + L"\")", helpers);
      }
      else {
        // text from a URI or a prompt's arguments
        const std::wstring text = L"mcp_a->Get(\"" + McpLit(params[p].name) + L"\")->GetString()";
        switch(params[p].ty.kind) {
        case MK_INT:
          read = text + L"->ToInt()";
          break;
        case MK_FLOAT:
          read = text + L"->ToFloat()";
          break;
        case MK_BOOL:
          read = text + L"->Equals(\"true\")";
          break;
        case MK_CHAR:
          read = text + L"->Get(0)";
          break;
        default:
          read = text;
          break;
        }
      }
      if(required) {
        body += var + L" := " + read + L"; ";
      }
      else {
        // a default must be a literal, which is then the value of a parameter that the call leaves out
        Expression* expression = default_value->GetExpression();
        std::wstring literal;
        if(expression) {
          switch(expression->GetExpressionType()) {
          case INT_LIT_EXPR:
            literal = std::to_wstring(static_cast<IntegerLiteral*>(expression)->GetValue());
            break;
          case FLOAT_LIT_EXPR: {
            std::wostringstream stream;
            stream.precision(17);
            stream << static_cast<FloatLiteral*>(expression)->GetValue();
            literal = stream.str();
            if(literal.find_first_of(L".eE") == std::wstring::npos) {
              literal += L".0";
            }
            break;
          }
          case BOOLEAN_LIT_EXPR:
            literal = static_cast<BooleanLiteral*>(expression)->GetValue() ? L"true" : L"false";
            break;
          case NIL_LIT_EXPR:
            literal = L"Nil";
            break;
          case CHAR_LIT_EXPR: {
            const wchar_t c = static_cast<CharacterLiteral*>(expression)->GetValue();
            if(c >= 32 && c < 127 && c != L'\'' && c != L'\\') {
              literal = std::wstring(L"'") + c + L"'";
            }
            break;
          }
          case CHAR_STR_EXPR: {
            CharacterString* text = static_cast<CharacterString*>(expression);
            // The node holds the literal as it is spelled in the source, escapes and all, so it is written
            // back as it stands and read once more by the same scanner. One with a '{$...}' in it is not a
            // literal (the segments that say so are filled in later, by the analyzer, from this same text).
            const std::wstring spelling = text->GetString();
            if(spelling.find(L"{$") == std::wstring::npos) {
              literal = (text->IsLiteral() ? L"$\"" : L"\"") + spelling + L"\"";
            }
            break;
          }
          default:
            break;
          }
        }
        if(literal.empty() || params[p].ty.kind == MK_ENUM || params[p].ty.kind == MK_RECORD) {
          diagnostics.Error(file_name, params[p].declaration->GetLineNumber(), params[p].declaration->GetLinePosition(),
                            L"the default of the optional parameter '" + params[p].name + L"' of the tool '" + doc.name + L"' must be a number, Bool, Char, String or Nil literal");
          ok = false;
          break;
        }
        body += var + L" : " + params[p].ty.text + L"; if(mcp_a->Has(\"" + McpLit(params[p].name) + L"\")) { " + var + L" := " + read + L"; } else { " + var +
                L" := " + literal + L"; }; ";
      }
    }
    if(!ok) {
      continue;
    }

    if(doc.read_only) {
      spec += L"->ReadOnly()";
    }
    if(doc.destructive) {
      spec += L"->Destructive()";
    }
    if(doc.idempotent) {
      spec += L"->Idempotent()";
    }
    if(rkind == 3) {
      // a JsonElement result is the structured result, an object
      spec += L"->Returns(\"{\\\"type\\\":\\\"object\\\"}\")";
    }
    if(doc.open_world) {
      spec += L"->OpenWorld()";
    }
    std::wstring adapter_name = L"McpTool_" + function_name;
    if(doc.kind == 0) {
      spec += L"->AddTo(mcp_service, Func2Ref->New(" + helpers.cls + L"->" + adapter_name + L"(McpCall) ~ McpToolResult)<McpCall, McpToolResult>);";
    }
    else if(doc.kind == 1) {
      adapter_name = L"McpResource_" + function_name;
      spec = L"mcp_service->AddResource(API.MCP.McpResource->New(\"" + McpLit(doc.uri) + L"\", \"" + McpLit(doc.name) + L"\", \"" + McpLit(doc.description) + L"\", \"" +
             McpLit(doc.mime.empty() ? std::wstring(L"text/plain") : doc.mime) + L"\", Func2Ref->New(" + helpers.cls + L"->" + adapter_name +
             L"(String) ~ McpContents)<String, McpContents>));";
    }
    else {
      adapter_name = L"McpPrompt_" + function_name;
      spec = prompt_var + L" := API.MCP.McpPrompt->New(\"" + McpLit(doc.name) + L"\", \"" + McpLit(doc.description) + L"\", Func2Ref->New(" + helpers.cls + L"->" + adapter_name +
             L"(McpCall) ~ McpPromptResult)<McpCall, McpPromptResult>); " + prompt_args + L"mcp_service->AddPrompt(" + prompt_var + L");";
    }
    registrations.push_back(std::move(spec));

    const std::wstring invoke = helpers.cls + L"->" + function_name + L"(" + call_args + L")";
    if(doc.kind == 1) {
      const std::wstring mime = McpLit(doc.mime.empty() ? std::wstring(L"text/plain") : doc.mime);
      body += L"mcp_r := " + invoke + L"; ";
      body += rkind == 2 ? L"if(mcp_r = Nil) { return Nil; }; return API.MCP.McpContents->OfText(mcp_uri, \"" + mime + L"\", mcp_r);" : L"return mcp_r;";
      adapters.push_back(std::to_wstring(fn_line) + L"\n" + L"function : " + adapter_name + L"(mcp_uri : String) ~ McpContents { " + body + L" }");
      continue;
    }
    if(doc.kind == 2) {
      body += L"mcp_r := " + invoke + L"; ";
      body += rkind == 2 ? L"if(mcp_r = Nil) { return Nil; }; mcp_m := API.MCP.McpPromptResult->New(\"" + McpLit(doc.description) + L"\"); mcp_m->AddMessage(\"user\", mcp_r); return mcp_m;"
                         : L"return mcp_r;";
      adapters.push_back(std::to_wstring(fn_line) + L"\n" + L"function : " + adapter_name + L"(mcp_call : McpCall) ~ McpPromptResult { " + body + L" }");
      continue;
    }
    switch(rkind) {
    case 0:
      body += invoke + L"; return API.MCP.McpToolResult->Text(\"done\");";
      break;
    case 1:
      body += L"mcp_r := " + invoke + L"; return API.MCP.McpToolResult->Text(\"{$mcp_r}\");";
      break;
    case 2:
      body += L"mcp_r := " + invoke + L"; if(mcp_r = Nil) { return API.MCP.McpToolResult->Error(\"the tool returned no result\"); }; return API.MCP.McpToolResult->Text(mcp_r);";
      break;
    case 3:
      body += L"mcp_r := " + invoke + L"; if(mcp_r = Nil) { return API.MCP.McpToolResult->Error(\"the tool returned no result\"); }; return API.MCP.McpToolResult->Json(mcp_r);";
      break;
    default:
      body += L"return " + invoke + L";";
      break;
    }
    adapters.push_back(std::to_wstring(fn_line) + L"\n" + L"function : " + adapter_name + L"(mcp_call : McpCall) ~ McpToolResult { " + body + L" }");
  }

  if(registrations.empty()) {
    return;
  }

  // the helpers first, then each tool's function, then the one function that declares them all
  for(size_t i = 0; i < helpers.code.size(); ++i) {
    McpAddFunction(klass, helpers.code[i].second, first_line, depth);
  }
  for(size_t i = 0; i < adapters.size(); ++i) {
    const size_t cut = adapters[i].find(L'\n');
    McpAddFunction(klass, adapters[i].substr(cut + 1), std::stoi(adapters[i].substr(0, cut)), depth);
  }

  std::wstring all = L"function : McpTools(mcp_service : McpService) ~ Nil { ";
  for(size_t i = 0; i < registrations.size(); ++i) {
    all += registrations[i] + L" ";
  }
  all += L"}";
  McpAddFunction(klass, all, first_line, depth);

  // The entry point of 'obr --mcp' / 'obr --mcp-http': a server of this class's tools, resources and prompts.
  // A McpMain the program wrote itself wins (it can register several classes, which a generated one cannot).
  // Only the entry itself counts, McpMain(args : String[]), which ParseMethod has already held to
  // 'function ... ~ Nil': the name alone used to decide, so a class with any other method named McpMain
  // (another parameter list) got no entry at all and 'obr --mcp' found nothing to serve.
  bool has_entry = false;
  std::vector<Method*> existing = klass->GetMethods();
  for(size_t i = 0; i < existing.size(); ++i) {
    if(HasMcpEntrySignature(existing[i])) {
      has_entry = true;
    }
  }
  if(!has_entry) {
    // named for the class so that the VM can tell it from one the program wrote
    std::wstring entry_class = klass->GetName();
    for(size_t i = 0; i < entry_class.size(); ++i) {
      if(!iswalnum(entry_class[i])) {
        entry_class[i] = L'_';
      }
    }
    McpAddFunction(klass, L"function : McpMain_" + entry_class + L"(mcp_args : String[]) ~ Nil { mcp_service := API.MCP.McpService->New(\"" + McpLit(klass->GetName()) + L"\", \"1.0\"); " +
                          helpers.cls + L"->McpTools(mcp_service); API.MCP.McpServe->Run(mcp_service, mcp_args); }", first_line, depth);
  }
}

/****************************************************************************
 * 'class Name : json'.
 *
 * Writes into the class, as source text parsed by this same parser, from the class's own fields (the same
 * derivation an '@tool' parameter gets; docs/design/AGENT_SYNTAX.md):
 *
 *   JsonSchema() ~ JsonElement          the JSON Schema of the class (fields; 'json(a, b)' marks a and b optional)
 *   JsonFill(mcp_j)                     sets every field from a JSON object
 *   FromJson(mcp_j) ~ Name              checks the value against the schema, then makes and fills an object
 *   Ask(agent, prompt) ~ Name           asks the agent's model for a value of the class (needs 'use API.AgentJson;')
 *
 * A class without ': json' gets none of this, and nothing here runs for it.
 ****************************************************************************/
void Parser::GenerateJsonClass(Class* klass, int line, int depth)
{
  const std::wstring file_name = klass->GetFileName();
  const std::wstring cls = klass->GetName();
  const std::wstring qcls = McpLit(cls);

  if(!current_lib_uses || std::find(current_lib_uses->begin(), current_lib_uses->end(), std::wstring(L"API.LlmUtil")) == current_lib_uses->end()) {
    diagnostics.Error(file_name, line, 1, L"a 'json' class needs 'use API.LlmUtil;' (and -lib llm_util,json): FromJson checks the value against the schema with that library");
    return;
  }
  if(!klass->GetParentName().empty()) {
    diagnostics.Error(file_name, line, 1, L"a 'json' class cannot extend another class: its schema is made from its own fields");
    return;
  }
  if(!klass->GetGenericClasses().empty()) {
    diagnostics.Error(file_name, line, 1, L"a 'json' class cannot be generic: its schema is made from its own field types");
    return;
  }

  // FromJson makes the object with New()
  bool has_new = false;
  const bool has_default_new = klass->HasDefaultNew();
  std::vector<Method*> methods = klass->GetMethods();
  for(size_t i = 0; i < methods.size(); ++i) {
    if(McpAfterColon(methods[i]->GetName()) == L"New") {
      has_new = true;
    }
  }
  // the names the compiler writes: a function of the class with one of them (any parameters) would be a second overload, not a replacement
  for(size_t i = 0; i < methods.size(); ++i) {
    const std::wstring existing = McpAfterColon(methods[i]->GetName());
    if(existing == L"JsonSchema" || existing == L"JsonFill" || existing == L"FromJson" ||
       (existing == L"Ask" && std::find(current_lib_uses->begin(), current_lib_uses->end(), std::wstring(L"API.AgentJson")) != current_lib_uses->end())) {
      diagnostics.Error(file_name, line, 1, L"The class already has a function named '" + existing + L"', which a 'json' class needs for itself");
      return;
    }
  }
  if(has_new && !has_default_new) {
    diagnostics.Error(file_name, line, 1, L"a 'json' class needs a constructor with no parameters: FromJson makes the object with '" + cls + L"->New()' and sets the fields");
    return;
  }

  McpTy ty;
  ty.kind = MK_JSONCLS;
  ty.name = cls;
  ty.text = cls;
  std::wstring why;
  mcp_class_stack.push_back(klass);
  const bool fields_ok = McpAnalyzeFields(klass, ty, why, 0);
  mcp_class_stack.pop_back();
  if(!fields_ok) {
    // no reason: a field's type is in a file with a syntax error, and that error is the answer
    if(!why.empty()) {
      diagnostics.Error(file_name, line, 1, why);
    }
    return;
  }
  const std::vector<std::wstring>& optional = klass->GetJsonOptional();
  for(size_t o = 0; o < optional.size(); ++o) {
    if(std::find(ty.items.begin(), ty.items.end(), optional[o]) == ty.items.end()) {
      diagnostics.Error(file_name, line, 1, L"'json(" + optional[o] + L")': the class '" + cls + L"' has no field of that name");
      return;
    }
  }

  McpHelpers helpers;
  helpers.cls = cls;
  helpers.prefix = L"JsonRead_";

  std::wstring fill = L"method : public : JsonFill(mcp_j : Data.JSON.JsonElement) ~ Nil { ";
  for(size_t i = 0; i < ty.items.size(); ++i) {
    const std::wstring name = McpLit(ty.items[i]);
    const std::wstring read = McpRead(ty.kids[i], L"mcp_j->Get(\"" + name + L"\")", helpers);
    if(i < ty.optional.size() && ty.optional[i]) {
      fill += L"if(mcp_j->Has(\"" + name + L"\") & mcp_j->Get(\"" + name + L"\")->GetType() <> Data.JSON.JsonElement->JsonType->NULL) { @" + ty.items[i] + L" := " + read + L"; }; ";
    }
    else {
      fill += L"if(<>mcp_j->Has(\"" + name + L"\")) { Runtime->Raise(Error->New(\"missing field '" + name + L"' of " + qcls + L"\")); }; @" + ty.items[i] + L" := " + read + L"; ";
    }
  }
  fill += L"}";

  const wchar_t* owner = L"a 'json' class";
  for(size_t i = 0; i < helpers.code.size(); ++i) {
    McpAddFunction(klass, helpers.code[i].second, line, depth, owner);
  }
  McpAddFunction(klass, L"function : JsonSchema() ~ Data.JSON.JsonElement { return Data.JSON.JsonParser->TextToElement(\"" + McpLit(McpSchemaText(ty)) + L"\"); }", line, depth, owner);
  McpAddFunction(klass, fill, line, depth, owner);
  McpAddFunction(klass, L"function : FromJson(mcp_j : Data.JSON.JsonElement) ~ " + cls + L" { " +
                        L"if(mcp_j = Nil) { Runtime->Raise(Error->New(\"" + qcls + L": there is no JSON value\")); }; " +
                        L"mcp_e := API.LlmUtil.JsonSchema->Check(" + cls + L"->JsonSchema(), mcp_j); " +
                        L"if(mcp_e->Size() > 0) { Runtime->Raise(Error->New(\"" + qcls + L": \" + API.LlmUtil.JsonSchema->Describe(mcp_e))); }; " +
                        L"mcp_o := " + cls + L"->New(); mcp_o->JsonFill(mcp_j); return mcp_o; }", line, depth, owner);

  if(std::find(current_lib_uses->begin(), current_lib_uses->end(), std::wstring(L"API.AgentJson")) != current_lib_uses->end()) {
    McpAddFunction(klass, L"function : Ask(mcp_agent : API.Agent.Agent, mcp_prompt : String) ~ " + cls + L" { return " + cls +
                          L"->FromJson(API.AgentJson.AgentAsk->Value(mcp_agent, mcp_prompt, " + cls + L"->JsonSchema())); }", line, depth, owner);
  }
}

// '@tool' belongs to a function: say so where it sits before anything else
void Parser::RejectToolDoc(const wchar_t* what)
{
  const ToolDoc* doc = scanner->GetToolDoc(scanner->GetToken()->GetDocId());
  if(doc) {
    diagnostics.Error(GetFileName(), doc->line, doc->pos, std::wstring(L"'@tool' marks a function, but its doc comment is before ") + what + L"; move the comment to the function");
  }
}

/****************************************************************************
 * Prompt literals (docs/design/AGENT_SYNTAX.md, section 2).
 *
 *   p := prompt "Summarise {text} in {count : Int} bullets.";
 *
 * 'prompt' is a word only where a string literal follows it, so a variable, a parameter or a method called
 * 'prompt' is untouched. The literal is rewritten here into the calls a person would write,
 *
 *   API.LlmUtil.Prompt->New()->Text("Summarise ")->Slot("text", text)->Text(" in ")->Slot("count", count)->Text(" bullets.")
 *
 * with every node placed at the line and column of the text it came from. A hole is '{name}' or
 * '{name : Type}'; '{{' and '}}' are literal braces; '{$...}' is the ordinary string interpolation and is left
 * to the text piece it sits in. What a hole may name, and whether its value has the declared type, is checked
 * by the analyzer (ContextAnalyzer::AnalyzePromptHole), where the types are known.
 ****************************************************************************/
namespace {
  // the code of a hole's declared type: 1 none, 2 String, 3 Int, 4 Float, 5 Bool, 6 Char, 7 Byte; 0 when the name is no hole type
  int PromptHoleType(const std::wstring& name)
  {
    if(name.empty()) { return 1; }
    if(name == L"String") { return 2; }
    if(name == L"Int") { return 3; }
    if(name == L"Float") { return 4; }
    if(name == L"Bool") { return 5; }
    if(name == L"Char") { return 6; }
    if(name == L"Byte") { return 7; }
    return 0;
  }

  bool PromptIsName(const std::wstring& name)
  {
    size_t i = 0;
    if(!name.empty() && name[0] == L'@') {
      i = 1;
    }
    if(i >= name.size() || !(iswalpha(name[i]) || name[i] == L'_')) {
      return false;
    }
    for(; i < name.size(); ++i) {
      if(!(iswalnum(name[i]) || name[i] == L'_')) {
        return false;
      }
    }
    return true;
  }

  std::wstring PromptTrim(const std::wstring& text)
  {
    const size_t a = text.find_first_not_of(L" \t");
    if(a == std::wstring::npos) {
      return L"";
    }
    return text.substr(a, text.find_last_not_of(L" \t") - a + 1);
  }
}

PARSER_NO_INLINE Expression* Parser::ParsePromptLiteral(int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int word_line = GetLineNumber();
  const int word_pos = GetLinePosition();
  NextToken();

  // a string the scanner rejected gets the errors any string gets
  if(Match(TOKEN_BAD_CHAR_STRING_LIT)) {
    return ParseSimpleExpression(depth + 1);
  }

  // the string: its text as written (escapes not yet read), and where its opening quote is
  const std::wstring text = scanner->GetToken()->GetIdentifier();
  const bool is_raw = scanner->GetToken()->GetByteLit();
  // (line, pos) is the first character of the text: a string's token is placed there when the string is on one
  // line, and on its last line when it is not
  const int end_line = GetLineNumber();
  int line = end_line;
  int pos = GetLinePosition();
  if(text.find(L'\n') != std::wstring::npos) {
    const Scanner::MultiLineString* multi = scanner->FindMultiLineStringEndingOn(end_line);
    if(multi) {
      line = multi->line;
      pos = multi->pos + 1;
    }
  }
  NextToken();
  size_t i = 0;

  const std::wstring klass_name = current_class ? current_class->GetName() : L"";
  MethodCall* head = TreeFactory::Instance()->MakeMethodCall(file_name, word_line, word_pos, word_line, word_pos, NEW_INST_CALL,
                                                             L"API.LlmUtil.Prompt", TreeFactory::Instance()->MakeExpressionList());
  if(is_raw) {
    diagnostics.Error(file_name, word_line, word_pos, L"A prompt literal takes an ordinary string, not a '$\"...\"' string: its holes and escapes are read from the text");
    return head;
  }

  MethodCall* tail = head;
  // the text piece being collected and where it began; (at_line, at_pos) is where the character at 'i' is
  std::wstring piece;
  int piece_line = line;
  int piece_pos = pos;
  int at_line = line;
  int at_pos = pos;

  // ends the piece: '->Text("...")'
  auto flush = [&]() {
    if(piece.empty()) {
      return;
    }
    if(HasUnclosedInterpolation(piece)) {
      diagnostics.Error(file_name, piece_line, piece_pos, L"A '{$' interpolation in a prompt literal is not closed: expected '}'");
    }
    ExpressionList* arguments = TreeFactory::Instance()->MakeExpressionList();
    arguments->AddExpression(TreeFactory::Instance()->MakeCharacterString(file_name, piece_line, piece_pos, piece, false));
    MethodCall* call = TreeFactory::Instance()->MakeMethodCall(file_name, piece_line, piece_pos, -1, -1, at_line, at_pos,
                                                               klass_name, L"Text", arguments);
    tail->SetMethodCall(call);
    tail = call;
    piece.clear();
  };

  // takes 'count' characters of the text into the piece
  auto take = [&](size_t count) {
    if(piece.empty()) {
      piece_line = at_line;
      piece_pos = at_pos;
    }
    for(size_t k = 0; k < count; ++k) {
      piece += text[i + k];
      if(text[i + k] == L'\n') {
        at_line += 1;
        at_pos = 1;
      }
      else {
        at_pos += 1;
      }
    }
    i += count;
  };

  const size_t size = text.size();
  while(i < size) {
    const wchar_t c = text[i];
    // an escape is two characters of text, whatever it escapes
    if(c == L'\\' && i + 1 < size) {
      take(2);
    }
    // '{{' and '}}' are one literal brace
    else if((c == L'{' || c == L'}') && i + 1 < size && text[i + 1] == c) {
      if(piece.empty()) {
        piece_line = at_line;
        piece_pos = at_pos;
      }
      piece += c;
      at_pos += 2;
      i += 2;
      // a brace and then a dollar are not the start of an interpolation: they go to separate pieces
      if(c == L'{' && i < size && text[i] == L'$') {
        flush();
      }
    }
    // '{$...}' is the string's own interpolation: kept whole, braces and all
    else if(c == L'{' && i + 1 < size && text[i + 1] == L'$') {
      size_t close = i;
      while(close < size && text[close] != L'}') {
        ++close;
      }
      take((close < size ? close + 1 : size) - i);
    }
    // a hole, which closes on its own line
    else if(c == L'{') {
      size_t close = i + 1;
      while(close < size && text[close] != L'}' && text[close] != L'{' && text[close] != L'\n' && text[close] != L'\r') {
        ++close;
      }
      if(close >= size || text[close] != L'}') {
        diagnostics.Error(file_name, at_line, at_pos, L"A prompt hole is not closed: expected '}' on the line of its '{'\n\tWrite a hole as {name} or {name : Type}; write {{ for a literal '{'");
        // the brace is taken as text, so the rest of the literal is still read
        take(1);
        continue;
      }
      flush();

      const std::wstring inner = text.substr(i + 1, close - i - 1);
      const size_t colon = inner.find(L':');
      const std::wstring name = PromptTrim(colon == std::wstring::npos ? inner : inner.substr(0, colon));
      const std::wstring type_name = colon == std::wstring::npos ? L"" : PromptTrim(inner.substr(colon + 1));
      const int hole_type = PromptHoleType(type_name);
      const std::wstring shown = L"'{" + inner + L"}'";
      if(PromptTrim(inner).empty()) {
        diagnostics.Error(file_name, at_line, at_pos, L"An empty prompt hole '{}' names no variable\n\tWrite a hole as {name} or {name : Type}; write {{ and }} for literal braces");
      }
      else if(!PromptIsName(name)) {
        diagnostics.Error(file_name, at_line, at_pos, shown + L" is not a prompt hole: a hole names one variable, as in {text} or {count : Int}\n\tCompute other values into a local first; write {{ and }} for literal braces");
      }
      else if(colon != std::wstring::npos && (type_name.empty() || !hole_type)) {
        diagnostics.Error(file_name, at_line, at_pos, L"Unknown type '" + type_name + L"' in the prompt hole " + shown + L": a hole takes a String, Int, Float, Bool, Char or Byte");
      }
      else {
        const int name_pos = at_pos + 1 + (int)inner.find(name);
        const int end_pos = at_pos + (int)(close - i) + 1;
        ExpressionList* arguments = TreeFactory::Instance()->MakeExpressionList();
        arguments->AddExpression(TreeFactory::Instance()->MakeCharacterString(file_name, at_line, at_pos, name[0] == L'@' ? name.substr(1) : name, true));
        arguments->AddExpression(TreeFactory::Instance()->MakeVariable(file_name, at_line, name_pos, name));
        MethodCall* call = TreeFactory::Instance()->MakeMethodCall(file_name, at_line, at_pos, -1, -1, at_line, end_pos, klass_name, L"Slot", arguments);
        call->SetPromptHole(hole_type);
        tail->SetMethodCall(call);
        tail = call;
      }
      at_pos += (int)(close - i) + 1;
      i = close + 1;
    }
    else if(c == L'}') {
      diagnostics.Error(file_name, at_line, at_pos, L"A '}' in a prompt literal closes no hole\n\tWrite }} for a literal '}'");
      take(1);
    }
    else {
      take(1);
    }
  }
  flush();

  return head;
}

/****************************************************************************
 * Agent declarations (docs/design/AGENT_SYNTAX.md, section 3).
 *
 *   agent Helper(script : Vector<AgentReply>) {
 *     backend : ScriptedBackend->New(script);
 *     instructions : "Answer in one sentence.";
 *     tools : Tools, AgentTools->Calculator();
 *     budget { steps : 8; }
 *   }
 *
 * 'agent' is a word only as the first token of a declaration at class level, before a name and '(' or '{'.
 * The declaration becomes the class a person would write, parsed by this same parser as the '@tool' code is:
 *
 *   class Helper { function : Build(script : Vector<AgentReply>) ~ API.Agent.Agent { ... return agent__a; } }
 *
 * The user's own text (the parameter list, each expression) is not copied into that source. The written class
 * has a placeholder name where a piece belongs, and when the parser meets one it reads the piece with a second
 * scanner over the original characters, started at the piece's own line and column (Scanner::StartAt). An error
 * in 'backend : ...' is therefore reported exactly where it was written, and the expression is parsed inside
 * 'Build', where its parameters and lambdas mean what they should.
 ****************************************************************************/
namespace {
  // one setting or list entry of a declaration, as the first pass found it
  struct AgentItem {
    std::wstring key;           // backend, instructions, trace, tools, mcp, allow, deny, or a budget limit
    bool is_limit = false;      // a limit of the 'budget' block
    bool is_class = false;      // a 'tools' entry that names a class of '@tool' functions
    std::wstring class_name;
    int line = 0;               // where the setting's name is (for a later entry of a list: the entry)
    int pos = 0;
    int splice = -1;            // index of the entry's expression, or of a class entry's name, in the splices
    int name_splice = -1;       // index of the name declared for the value, placed where the value starts
  };

  // the name a placeholder starts with, followed by the index of its piece
  const std::wstring AGENT_SPLICE_PREFIX = L"agent__x";

  // the budget limits and the AgentBudget method each one sets; 'cost' is a Float, the others an Int
  const wchar_t* const AGENT_LIMITS[][2] = {
    { L"steps", L"SetMaxSteps" }, { L"tokens", L"SetMaxTokens" }, { L"tool_calls", L"SetMaxToolCalls" },
    { L"model_calls", L"SetMaxModelCalls" }, { L"wall_ms", L"SetMaxWallMs" }, { L"model_call_ms", L"SetModelCallMs" },
    { L"cost", L"SetMaxCost" }
  };

  const wchar_t* AgentLimitSetter(const std::wstring& name)
  {
    for(size_t i = 0; i < sizeof(AGENT_LIMITS) / sizeof(AGENT_LIMITS[0]); ++i) {
      if(name == AGENT_LIMITS[i][0]) {
        return AGENT_LIMITS[i][1];
      }
    }
    return nullptr;
  }

  // source text laid out so that a piece lands on a line and column of the user's file
  struct AgentText {
    std::wstring text;
    int line = 1;
    int pos = 1;

    void Add(const std::wstring& code) {
      text += code;
      pos += (int)code.size();
    }

    // moves to (to_line, to_pos) when that is still ahead, else adds one space
    void At(int to_line, int to_pos) {
      if(to_line > line) {
        text.append(to_line - line, L'\n');
        line = to_line;
        pos = 1;
      }
      if(to_line == line && to_pos >= pos) {
        text.append(to_pos - pos, L' ');
        pos = to_pos;
      }
      else {
        Add(L" ");
      }
    }
  };
}

bool Parser::IsAgentSpliceName()
{
  return Match(TOKEN_IDENT) && scanner->GetToken()->GetIdentifier().compare(0, AGENT_SPLICE_PREFIX.size(), AGENT_SPLICE_PREFIX) == 0;
}

/****************************
 * After every file is parsed: a class of the program that an agent's 'tools' names must have an '@tool'
 * function. A name that is no class of the program is left to the analyzer (a library's class, or none).
 ****************************/
void Parser::CheckAgentToolClasses()
{
  for(size_t i = 0; i < agent_tool_classes.size(); ++i) {
    const AgentToolClass& named = agent_tool_classes[i];
    bool is_class = false;
    bool has_tools = false;
    const std::vector<ParsedBundle*> bundles = program->GetBundles();
    for(size_t b = 0; b < bundles.size(); ++b) {
      const std::vector<Class*> classes = bundles[b]->GetClasses();
      for(size_t c = 0; c < classes.size(); ++c) {
        const std::wstring& full_name = classes[c]->GetName();
        const bool same = full_name == named.class_name ||
          (full_name.size() > named.class_name.size() && full_name[full_name.size() - named.class_name.size() - 1] == L'.' &&
           full_name.compare(full_name.size() - named.class_name.size(), named.class_name.size(), named.class_name) == 0);
        if(!same) {
          continue;
        }
        is_class = true;
        if(mcp_broken_files.find(classes[c]->GetFileName()) != mcp_broken_files.end()) {
          // its file has a syntax error, which is the answer: its tools were not derived
          has_tools = true;
        }
        const std::vector<Method*> methods = classes[c]->GetMethods();
        for(size_t m = 0; m < methods.size(); ++m) {
          if(McpAfterColon(methods[m]->GetName()) == L"McpTools") {
            has_tools = true;
          }
        }
      }
    }
    if(is_class && !has_tools) {
      diagnostics.Error(named.file_name, named.line, named.pos, L"The class '" + named.class_name + L"', named in 'tools' of the agent '" + named.agent_name +
                        L"', has no '@tool' function\n\tAn agent's tools are the functions of the class whose doc comment has an '@tool' line; a value that is"
                        L" an AgentTool, such as AgentTools->Calculator(), can be listed as well");
    }
  }
  agent_tool_classes.clear();
}

/****************************
 * A class named in an agent's 'tools': the statement written for it starts with a placeholder, and
 * the name is given the place it has in the user's file.
 ****************************/
PARSER_NO_INLINE Statement* Parser::ParseAgentName(int depth)
{
  const std::wstring ident = scanner->GetToken()->GetIdentifier();
  const AgentSplice& splice = (*agent_splices)[(size_t)std::stoi(ident.substr(AGENT_SPLICE_PREFIX.size()))];
  NextToken();

  return ParseIdentifierStatement(splice.name, splice.line, splice.pos, splice.file_name, depth);
}

/****************************
 * Reads the piece a placeholder stands for, in place. 'declarations' is set for the
 * parameter list, and the piece is then read as one; otherwise it is an expression.
 ****************************/
Expression* Parser::ParseAgentSplice(DeclarationList** declarations, int depth)
{
  const std::wstring ident = scanner->GetToken()->GetIdentifier();
  const AgentSplice& splice = (*agent_splices)[(size_t)std::stoi(ident.substr(AGENT_SPLICE_PREFIX.size()))];
  NextToken();

  // the user's text is read with no placeholders of its own: a name that looks like one is just a name
  ScannerScope piece_scanner(scanner);
  StateScope<std::vector<AgentSplice>*> piece_splices(agent_splices);
  agent_splices = nullptr;
  scanner = new Scanner(splice.file_name, alt_syntax, splice.text);
  scanner->StartAt(splice.line, splice.pos);
  NextToken();

  Expression* expression = nullptr;
  if(declarations) {
    *declarations = ParseDecelerationList(depth + 1);
  }
  else {
    expression = ParseExpression(depth + 1);
    // the ';', ',' or '}' that ended the piece is part of its text, so that an expression cut short is
    // reported against the token the user wrote, not against an end of file that is not there
    if((Match(TOKEN_SEMI_COLON) || Match(TOKEN_COMMA) || Match(TOKEN_CLOSED_BRACE)) && Match(TOKEN_END_OF_STREAM, SECOND_INDEX)) {
      NextToken();
    }
  }
  if(!Match(TOKEN_END_OF_STREAM) && !diagnostics.HasErrors()) {
    ProcessError(L"Unexpected " + scanner->DescribeToken(scanner->GetToken()) + L" in " + splice.what);
  }

  return expression;
}

Class* Parser::ParseAgent(const std::wstring& bundle_name, int depth)
{
  CheckDepth(depth);
  const std::wstring file_name = GetFileName();
  const int agent_line = GetLineNumber();
  const int agent_pos = GetLinePosition();
  NextToken();

  const std::wstring name = scanner->GetToken()->GetIdentifier();
  const int name_line = GetLineNumber();
  const int name_pos = GetLinePosition();
  NextToken();

  std::vector<AgentSplice> splices;
  std::vector<AgentItem> items;
  std::vector<std::wstring> parameters;
  bool ok = true;

  // reports an error of the declaration itself; the class is then not written
  auto fail = [&](int line, int pos, const std::wstring& message) {
    diagnostics.Error(file_name, line, pos, message);
    ok = false;
  };

  // After an error where a setting starts: on to the next setting, past the ';' that ends this one, or to the
  // '}' that closes the agent. What is inside parentheses, brackets or braces is skipped whole. A ')' or ']'
  // that closes nothing is one more token to skip: counted, it took the depth below zero, the agent's own '}'
  // was then read as nested, and the rest of the file was skipped as part of the agent with its errors unsaid.
  auto next_setting = [&]() {
    int nesting = 0;
    while(!Match(TOKEN_END_OF_STREAM) && !(nesting == 0 && (Match(TOKEN_SEMI_COLON) || Match(TOKEN_CLOSED_BRACE)))) {
      if(Match(TOKEN_OPEN_PAREN) || Match(TOKEN_OPEN_BRACKET) || Match(TOKEN_OPEN_BRACE)) {
        ++nesting;
      }
      else if(nesting > 0 && (Match(TOKEN_CLOSED_PAREN) || Match(TOKEN_CLOSED_BRACKET) || Match(TOKEN_CLOSED_BRACE))) {
        --nesting;
      }
      NextToken();
    }
    if(Match(TOKEN_SEMI_COLON)) {
      NextToken();
    }
  };

  // The tokens from the current one up to a ';' or '}' outside any brace, or a ',' outside any bracket and any
  // type-argument list when 'in_list', are one piece; it starts after (from_line, from_pos). A ';' inside an open parenthesis ends
  // the piece too: the parenthesis is then reported as not closed, where it is. Leaves the scanner on the
  // token that ended the piece and returns the piece's index, or -1 when it is empty. 'shape' is told what
  // the piece is made of, and (first_line, first_pos) where it starts.
  enum Shape { SHAPE_OTHER, SHAPE_NAME, SHAPE_FUNCTION };
  auto piece = [&](int from_line, int from_pos, bool in_list, const std::wstring& what, Shape& shape, std::wstring& shape_name,
                   int& first_line, int& first_pos) -> int {
    int braces = 0;
    int brackets = 0;
    // Type arguments, 'Factory->New()<String, Int>->MakeTool()': the ',' between them is not the end of an entry.
    // A '<' opens them where the expression parser reads it so (ParseVariable, ParseMethodCall): always after
    // 'New(...)' and 'New[...]'; after a name or a call when a name and then '<', '>', ',' or '.' follow.
    int angles = 0;
    std::vector<bool> opened_by_new;   // for each open '(' or '[': whether 'New' is before it
    bool closed_new = false;           // the token before this one closed the '(' or '[' of a 'New'
    ScannerTokenType previous = TOKEN_END_OF_STREAM;
    size_t count = 0;
    bool names_only = true;         // IDENT ('.' IDENT)*
    bool function_only = false;     // the names, then '->' IDENT, then nothing
    size_t after_accessor = 0;
    shape_name.clear();
    // a string's token is placed inside the string, and on its last line: the place after the separator serves
    const bool starts_with_string = Match(TOKEN_CHAR_STRING_LIT) || Match(TOKEN_BAD_CHAR_STRING_LIT);
    first_line = starts_with_string ? from_line : GetLineNumber();
    first_pos = starts_with_string ? from_pos : GetLinePosition();
    while(!Match(TOKEN_END_OF_STREAM)) {
      const ScannerTokenType type = GetToken();
      if(braces == 0 && (type == TOKEN_SEMI_COLON || type == TOKEN_CLOSED_BRACE || (in_list && brackets == 0 && angles == 0 && type == TOKEN_COMMA))) {
        break;
      }
      const bool after_new = closed_new;
      closed_new = false;
      if(type == TOKEN_OPEN_BRACE) {
        ++braces;
      }
      else if(type == TOKEN_CLOSED_BRACE) {
        --braces;
      }
      else if(type == TOKEN_OPEN_PAREN || type == TOKEN_OPEN_BRACKET) {
        ++brackets;
        opened_by_new.push_back(previous == TOKEN_NEW_ID);
      }
      else if((type == TOKEN_CLOSED_PAREN || type == TOKEN_CLOSED_BRACKET) && brackets > 0) {
        --brackets;
        closed_new = opened_by_new.back();
        opened_by_new.pop_back();
      }

      if(angles > 0) {
        // type arguments are names, '.', ',' and nested lists; anything else and this was not a list of them
        switch(type) {
        case TOKEN_LES:
          ++angles;
          break;
        case TOKEN_GTR:
          --angles;
          break;
        case TOKEN_SHR:
          angles = angles > 2 ? angles - 2 : 0;
          break;
        case TOKEN_SHR_UNSIGNED:
          angles = angles > 3 ? angles - 3 : 0;
          break;
        case TOKEN_IDENT:
        case TOKEN_PERIOD:
        case TOKEN_COMMA:
          break;
        default:
          angles = 0;
          break;
        }
      }
      else if(type == TOKEN_LES &&
              (after_new || ((previous == TOKEN_IDENT || previous == TOKEN_CLOSED_PAREN) && Match(TOKEN_IDENT, SECOND_INDEX) &&
                             (Match(TOKEN_LES, THIRD_INDEX) || Match(TOKEN_GTR, THIRD_INDEX) || Match(TOKEN_COMMA, THIRD_INDEX) || Match(TOKEN_PERIOD, THIRD_INDEX))))) {
        angles = 1;
      }
      previous = type;

      if(names_only) {
        const bool want_name = count % 2 == 0;
        if(want_name && type == TOKEN_IDENT) {
          shape_name += scanner->GetToken()->GetIdentifier();
        }
        else if(!want_name && type == TOKEN_PERIOD) {
          shape_name += L'.';
        }
        else {
          names_only = false;
          function_only = !want_name && type == TOKEN_ASSESSOR;
          after_accessor = 0;
        }
      }
      else if(function_only) {
        ++after_accessor;
        if(after_accessor > 1 || type != TOKEN_IDENT) {
          function_only = false;
        }
      }
      ++count;
      NextToken();
    }

    shape = SHAPE_OTHER;
    if(count > 0 && names_only && count % 2 == 1) {
      shape = SHAPE_NAME;
    }
    else if(count > 0 && !names_only && function_only && after_accessor == 1) {
      shape = SHAPE_FUNCTION;
    }

    if(count == 0) {
      fail(GetLineNumber(), GetLinePosition(), L"Expected a value for " + what);
      return -1;
    }

    AgentSplice splice;
    splice.file_name = file_name;
    splice.text = scanner->GetSourceBefore(GetLineNumber(), GetLinePosition() + (Match(TOKEN_END_OF_STREAM) ? 0 : 1));
    splice.line = from_line;
    splice.pos = from_pos;
    splice.what = what;
    splices.push_back(splice);
    return (int)splices.size() - 1;
  };

  // a name the compiler declares for a value, to be placed where the value starts: what the analyzer says about
  // the declaration is then said at the value
  auto declared = [&](const std::wstring& declared_name, int line, int pos) -> int {
    AgentSplice splice;
    splice.file_name = file_name;
    splice.name = declared_name;
    splice.line = line;
    splice.pos = pos;
    splices.push_back(splice);
    return (int)splices.size() - 1;
  };

  if(name.find(L'.') != std::wstring::npos) {
    fail(name_line, name_pos, L"An agent's name is one word; put the declaration in a 'bundle' to give it a bundle's name");
  }

  // the parameter list: kept as written, its names noted
  int parameters_splice = -1;
  if(Match(TOKEN_OPEN_PAREN)) {
    const int open_line = GetLineNumber();
    const int open_pos = GetLinePosition();
    NextToken();
    int nesting = 1;
    bool at_start = true;
    while(nesting > 0 && !Match(TOKEN_END_OF_STREAM)) {
      const ScannerTokenType type = GetToken();
      if(type == TOKEN_OPEN_PAREN) {
        ++nesting;
      }
      else if(type == TOKEN_CLOSED_PAREN) {
        --nesting;
        if(nesting == 0) {
          break;
        }
      }
      else if(nesting == 1 && at_start && type == TOKEN_IDENT) {
        const std::wstring parameter = scanner->GetToken()->GetIdentifier();
        // 'ref name : Type'
        if(!(parameter == L"ref" && Match(TOKEN_IDENT, SECOND_INDEX))) {
          parameters.push_back(parameter);
          if(parameter.compare(0, 7, L"agent__") == 0) {
            fail(GetLineNumber(), GetLinePosition(), L"A parameter of an agent may not start with 'agent__': the names the compiler writes for the agent do");
          }
          at_start = false;
        }
      }
      if(type != TOKEN_IDENT) {
        at_start = nesting == 1 && type == TOKEN_COMMA;
      }
      NextToken();
    }
    if(!Match(TOKEN_CLOSED_PAREN)) {
      fail(open_line, open_pos, L"The parameter list of the agent '" + name + L"' is not closed: expected ')'");
      return nullptr;
    }

    AgentSplice splice;
    splice.file_name = file_name;
    splice.text = scanner->GetSourceBefore(GetLineNumber(), GetLinePosition() + 1);
    splice.line = open_line;
    splice.pos = open_pos;
    splice.what = L"the parameters of the agent '" + name + L"'";
    splices.push_back(splice);
    parameters_splice = (int)splices.size() - 1;
    NextToken();
  }

  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
    return nullptr;
  }
  NextToken();

  // the settings
  const std::wstring settings = L"backend, instructions, tools, mcp, allow, deny, budget and trace";
  std::vector<std::wstring> seen;
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    const int key_line = GetLineNumber();
    const int key_pos = GetLinePosition();
    if(!Match(TOKEN_IDENT)) {
      fail(key_line, key_pos, L"Expected a setting of the agent '" + name + L"', found " + scanner->DescribeToken(scanner->GetToken()) +
           L"\n\tAn agent has " + settings + L", each written 'name : value;'");
      next_setting();
      continue;
    }
    const std::wstring key = scanner->GetToken()->GetIdentifier();
    NextToken();

    const bool is_list = key == L"tools" || key == L"mcp" || key == L"allow" || key == L"deny";
    const bool is_single = key == L"backend" || key == L"instructions" || key == L"trace";
    const bool is_budget = key == L"budget";
    if(!is_list && !is_single && !is_budget) {
      fail(key_line, key_pos, L"Unknown setting '" + key + L"' in the agent '" + name + L"': an agent has " + settings);
    }
    else if(std::find(seen.begin(), seen.end(), key) != seen.end()) {
      fail(key_line, key_pos, L"The agent '" + name + L"' has '" + key + L"' twice; give all of it in one place");
    }
    seen.push_back(key);

    if(is_budget) {
      if(!Match(TOKEN_OPEN_BRACE)) {
        fail(GetLineNumber(), GetLinePosition(), L"Expected '{' after 'budget': the limits are written 'budget { steps : 8; tokens : 5000; }'");
        next_setting();
        continue;
      }
      NextToken();
      std::vector<std::wstring> limits_seen;
      size_t limits = 0;
      while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
        const int limit_line = GetLineNumber();
        const int limit_pos = GetLinePosition();
        std::wstring limit = Match(TOKEN_IDENT) ? scanner->GetToken()->GetIdentifier() : L"";
        if(limit.empty()) {
          fail(limit_line, limit_pos, L"Expected a limit of the budget, found " + scanner->DescribeToken(scanner->GetToken()) +
               L"\n\tA budget has steps, tokens, tool_calls, model_calls, wall_ms, model_call_ms and cost, each written 'name : value;'");
        }
        else {
          NextToken();
          if(!AgentLimitSetter(limit)) {
            fail(limit_line, limit_pos, L"Unknown limit '" + limit + L"' in the budget of the agent '" + name +
                 L"': a budget has steps, tokens, tool_calls, model_calls, wall_ms, model_call_ms and cost");
          }
          else if(std::find(limits_seen.begin(), limits_seen.end(), limit) != limits_seen.end()) {
            fail(limit_line, limit_pos, L"The budget of the agent '" + name + L"' has '" + limit + L"' twice");
          }
          limits_seen.push_back(limit);
        }

        int colon_line = GetLineNumber();
        int colon_pos = GetLinePosition();
        if(Match(TOKEN_COLON)) {
          NextToken();
        }
        else {
          fail(colon_line, colon_pos, L"Expected ':' after the limit '" + limit + L"', found " + scanner->DescribeToken(scanner->GetToken()));
          colon_pos -= 1;
        }

        Shape shape; std::wstring shape_name; int first_line, first_pos;
        AgentItem item;
        item.key = limit;
        item.is_limit = true;
        item.line = limit_line;
        item.pos = limit_pos;
        item.splice = piece(colon_line, colon_pos + 1, false, L"the limit '" + limit + L"' of the agent '" + name + L"'", shape, shape_name, first_line, first_pos);
        if(item.splice >= 0 && AgentLimitSetter(limit)) {
          item.name_splice = declared(L"agent__l" + std::to_wstring(items.size()), first_line, first_pos);
          items.push_back(item);
        }
        ++limits;

        if(Match(TOKEN_SEMI_COLON)) {
          NextToken();
        }
        else if(ok) {
          fail(GetLineNumber(), GetLinePosition(), L"Expected ';' after the limit '" + limit + L"', found " + scanner->DescribeToken(scanner->GetToken()));
        }
      }
      if(!Match(TOKEN_CLOSED_BRACE)) {
        fail(key_line, key_pos, L"The budget of the agent '" + name + L"' is not closed: expected '}'");
        return nullptr;
      }
      NextToken();
      if(limits == 0) {
        fail(key_line, key_pos, L"The budget of the agent '" + name + L"' is empty; give a limit, as in 'budget { steps : 8; }', or leave the budget out");
      }
      // a ';' after the block is allowed
      if(Match(TOKEN_SEMI_COLON)) {
        NextToken();
      }
      continue;
    }

    int sep_line = GetLineNumber();
    int sep_pos = GetLinePosition();
    if(Match(TOKEN_COLON)) {
      NextToken();
    }
    else {
      fail(sep_line, sep_pos, L"Expected ':' after '" + key + L"', found " + scanner->DescribeToken(scanner->GetToken()));
      sep_pos -= 1;
    }

    // one value, or the entries of a list
    bool more = true;
    bool first = true;
    while(more) {
      const std::wstring what = (is_list ? L"an entry of '" : L"'") + key + L"' of the agent '" + name + L"'";
      Shape shape; std::wstring shape_name; int first_line, first_pos;
      AgentItem item;
      item.key = key;
      item.line = first ? key_line : sep_line;
      item.pos = first ? key_pos : sep_pos;
      item.splice = piece(sep_line, sep_pos + 1, is_list, what, shape, shape_name, first_line, first_pos);
      if(item.splice >= 0) {
        if(key == L"tools" && shape == SHAPE_NAME && std::find(parameters.begin(), parameters.end(), shape_name) == parameters.end()) {
          // the name is the piece: it is put back at its own place when the class is parsed
          item.is_class = true;
          item.class_name = shape_name;
          splices[item.splice].name = shape_name;
          splices[item.splice].line = first_line;
          splices[item.splice].pos = first_pos;
          AgentToolClass tool_class;
          tool_class.file_name = file_name;
          tool_class.class_name = shape_name;
          tool_class.agent_name = name;
          tool_class.line = first_line;
          tool_class.pos = first_pos;
          agent_tool_classes.push_back(tool_class);
          if(shape_name.compare(0, 7, L"agent__") == 0) {
            fail(first_line, first_pos, L"A class named in 'tools' may not start with 'agent__': the names the compiler writes for the agent do");
          }
        }
        else if(key == L"tools" && shape == SHAPE_FUNCTION) {
          fail(first_line, first_pos, L"A tool of an agent is a class, not one function: name the class and the agent gets each of its '@tool' functions"
               L"\n\tTo let the agent use only some of them, name the class here and list the tool names in 'allow' or 'deny'");
        }
        if(is_list || is_single) {
          if(!item.is_class) {
            const std::wstring declared_name = key == L"backend" ? L"agent__b" : key == L"instructions" ? L"agent__i" : key == L"trace" ? L"agent__t" :
              (key == L"tools" ? L"agent__t" : key == L"mcp" ? L"agent__m" : L"agent__n") + std::to_wstring(items.size());
            item.name_splice = declared(declared_name, first_line, first_pos);
          }
          items.push_back(item);
        }
      }

      more = is_list && Match(TOKEN_COMMA);
      if(more) {
        sep_line = GetLineNumber();
        sep_pos = GetLinePosition();
        NextToken();
        first = false;
      }
    }

    if(Match(TOKEN_SEMI_COLON)) {
      NextToken();
    }
    else if(ok) {
      fail(GetLineNumber(), GetLinePosition(), L"Expected ';' after '" + key + L"' of the agent '" + name + L"', found " + scanner->DescribeToken(scanner->GetToken()));
    }
  }

  const int end_line = GetLineNumber();
  const int end_pos = GetLinePosition();
  if(!Match(TOKEN_CLOSED_BRACE)) {
    fail(agent_line, agent_pos, L"The agent '" + name + L"' is not closed: expected '}'");
    return nullptr;
  }
  NextToken();

  if(std::find(seen.begin(), seen.end(), std::wstring(L"backend")) == seen.end()) {
    fail(name_line, name_pos, L"The agent '" + name + L"' has no backend: say which model it talks to, as in 'backend : AnthropicBackend->New(model, Nil);'");
  }
  if(!ok) {
    return nullptr;
  }

  // the class, written so that each statement starts where the setting it comes from does
  const std::wstring quoted = L"\"" + name + L"\"";
  bool has_budget = false;
  bool has_instructions = false;
  bool has_trace = false;
  for(size_t i = 0; i < items.size(); ++i) {
    has_budget = has_budget || items[i].is_limit;
    has_instructions = has_instructions || items[i].key == L"instructions";
    has_trace = has_trace || items[i].key == L"trace";
  }

  AgentText code;
  code.At(agent_line, agent_pos);
  code.Add(L"class");
  code.At(name_line, name_pos);
  code.Add(name + L" { function : Build" +
           (parameters_splice >= 0 ? L" " + AGENT_SPLICE_PREFIX + std::to_wstring(parameters_splice) + L" " : std::wstring(L"()")) +
           L" ~ API.Agent.Agent { agent__r := API.Agent.ToolRegistry->New();");
  if(has_budget) {
    code.Add(L" agent__g := API.Agent.AgentBudget->New();");
  }

  for(size_t i = 0; i < items.size(); ++i) {
    const AgentItem& item = items[i];
    const std::wstring n = std::to_wstring(i);
    const std::wstring value = AGENT_SPLICE_PREFIX + std::to_wstring(item.splice);
    if(item.is_class) {
      code.At(item.line, item.pos);
      code.Add(L"agent__s" + n + L" := API.MCP.McpService->New(\"" + item.class_name + L"\", \"1.0\"); " + value + L"->McpTools(agent__s" + n + L"); agent__c" + n + L" := API.MCP.McpClient->InProcess(agent__s" + n + L"); agent__c" + n +
               L"->Initialize(" + quoted + L", \"1.0\"); if(API.MCP.McpAgentTools->AddAll(agent__r, agent__c" + n +
               L") < 0) { Runtime->Raise(Error->New(\"agent '" + name + L"': the tools of the class '" + item.class_name + L"' could not be listed\")); };");
      continue;
    }

    code.At(item.line, item.pos);
    const std::wstring placed = AGENT_SPLICE_PREFIX + std::to_wstring(item.name_splice);
    if(item.is_limit) {
      code.Add(placed + (item.key == L"cost" ? L" : Float := " : L" : Int := ") + value + L"; agent__g->" + AgentLimitSetter(item.key) + L"(agent__l" + n + L");");
    }
    else if(item.key == L"backend") {
      code.Add(placed + L" : API.Agent.ChatBackend := " + value + L";");
    }
    else if(item.key == L"instructions") {
      code.Add(placed + L" : String := " + value + L";");
    }
    else if(item.key == L"trace") {
      code.Add(placed + L" : API.Agent.AgentTraceSink := " + value + L";");
    }
    else if(item.key == L"tools") {
      code.Add(placed + L" : API.Agent.AgentTool := " + value + L"; if(agent__t" + n + L" = Nil) { Runtime->Raise(Error->New(\"agent '" + name +
               L"': a tool is Nil\")); }; agent__r->Add(agent__t" + n + L");");
    }
    else if(item.key == L"mcp") {
      code.Add(placed + L" : API.MCP.McpClient := " + value + L"; if(agent__m" + n + L" = Nil) { Runtime->Raise(Error->New(\"agent '" + name +
               L"': an 'mcp' server is Nil\")); }; if(API.MCP.McpAgentTools->AddAll(agent__r, agent__m" + n +
               L") < 0) { Runtime->Raise(Error->New(\"agent '" + name + L"': the tools of an 'mcp' server could not be listed\")); };");
    }
    else {
      code.Add(placed + L" : String := " + value + L"; agent__r->" + (item.key == L"allow" ? L"Allow" : L"Deny") + L"(agent__n" + n + L");");
    }
  }

  code.At(end_line, end_pos);
  code.Add(L"agent__a := API.Agent.Agent->New(agent__b, agent__r);");
  if(has_instructions) {
    code.Add(L" agent__a->SetSystem(agent__i);");
  }
  if(has_budget) {
    code.Add(L" agent__a->SetBudget(agent__g);");
  }
  if(has_trace) {
    code.Add(L" agent__a->SetTrace(agent__t);");
  }
  code.Add(L" return agent__a; } }");

  // 'splices' is a local of this function: the parser is not left pointing at it
  ScannerScope class_scanner(scanner);
  StateScope<std::vector<AgentSplice>*> class_splices(agent_splices);
  StateScope<Method*> class_method(current_method);
  scanner = new Scanner(file_name, alt_syntax, code.text);
  agent_splices = &splices;
  NextToken();

  Class* klass = nullptr;
  if(Match(TOKEN_CLASS_ID)) {
    klass = ParseClass(bundle_name, depth + 1);
  }
  if(!Match(TOKEN_END_OF_STREAM) && !diagnostics.HasErrors()) {
    ProcessError(L"The code written for the agent '" + name + L"' has an extra " + scanner->DescribeToken(scanner->GetToken()));
  }

  return klass;
}
