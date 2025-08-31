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

bool Parser::IsBasicType(ScannerTokenType type)
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

const std::wstring Parser::GetEnumScopeName(const std::wstring& ident)
{
  std::wstring scope_name;
  if(current_class) {
    scope_name = current_class->GetName() + L"#" + ident;
  }
  else {
    scope_name = ident;
  }

  return scope_name;
}

std::wstring Parser::RandomString(size_t len)
{
  std::random_device gen;
  const wchar_t* values = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  std::wstring output;
  for (size_t i = 0; i < len; ++i) {
    const size_t index = gen() % wcslen(values);
    output += values[index];
  }

  return output;
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
      }
      else if(Match(TOKEN_IDENT)) {
        ProcessError(L"Expected '.'", TOKEN_SEMI_COLON);
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
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(ScannerTokenType type)
{
  std::wstring msg = error_msgs[type];
#ifdef _DEBUG
  GetLogger() << L"\tError: " << GetFileName() << L":(" << GetLineNumber() << L',' << GetLinePosition() << L"): " << msg << std::endl;
#endif

  const std::wstring& str_line_num = ToString(GetLineNumber());
  const std::wstring& str_line_pos = ToString(GetLinePosition());
  errors.insert(std::pair<int, std::wstring>(GetLineNumber(), GetFileName()+ L":(" + str_line_num + L',' + str_line_pos + L"): " + msg));
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << GetFileName() << L":(" << GetLineNumber() << L',' << GetLinePosition() << L"): " << msg << std::endl;
#endif

  const std::wstring &str_line_num = ToString(GetLineNumber());
  const std::wstring& str_line_pos = ToString(GetLinePosition());
  errors.insert(std::pair<int, std::wstring>(GetLineNumber(), GetFileName()+ L":(" + str_line_num + L',' + str_line_pos + L"): " + msg));
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg, ScannerTokenType sync)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << GetFileName() << L":(" << GetLineNumber() << L',' << GetLinePosition() << L"): " << msg << std::endl;
#endif

  const std::wstring &str_line_num = ToString(GetLineNumber());
  const std::wstring& str_line_pos = ToString(GetLinePosition());

  errors.insert(std::pair<int, std::wstring>(GetLineNumber(), GetFileName() + L":(" + str_line_num + L',' + str_line_pos + L"): " + msg));
  ScannerTokenType token = GetToken();
  while(token != sync && token != TOKEN_END_OF_STREAM) {
    NextToken();
    token = GetToken();
  }
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg, ParseNode * node)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << node->GetFileName() << L':' << node->GetLineNumber()
    << L": " << msg << std::endl;
#endif

  const std::wstring &str_line_num = ToString(node->GetLineNumber());
  errors.insert(std::pair<int, std::wstring>(node->GetLineNumber(), node->GetFileName() + L":(" + str_line_num + L",1): " + msg));
}

/****************************
 * Checks for parsing errors.
 ****************************/
bool Parser::CheckErrors()
{
  // check and process errors
  if(errors.size()) {
    const size_t error_max = 8;

    std::map<int, std::wstring>::iterator error = errors.begin();
    if(errors.size() > error_max) {
      for(size_t i = 0; i < error_max; ++error, ++i) {
#if defined(_DIAG_LIB) || defined(_MODULE)
        error_strings.push_back(error->second);
#else
        std::wcerr << error->second << std::endl;
#endif
      }
    }
    else {
      for(; error != errors.end(); ++error) {
#if defined(_DIAG_LIB) || defined(_MODULE)
        error_strings.push_back(error->second);
#else
        std::wcerr << error->second << std::endl;
#endif
      }
    }

#ifdef _DIAG_LIB
    program->SetErrorStrings(error_strings);
#endif

    return false;
  }

  return true;
}

#ifdef _MODULE
std::vector<std::wstring> Parser::GetErrors()
{
  return error_strings;
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

  return CheckErrors();
}

/****************************
 * Parses a file.
 ****************************/
void Parser::ParseFile(const std::wstring &file_name)
{
  scanner = new Scanner(file_name, alt_syntax);
  NextToken();
  ParseBundle(0);
  // clean up
  delete scanner;
  scanner = nullptr;
}

/****************************
 * Parses std::string
 ****************************/
void Parser::ParseText(std::pair<std::wstring, std::wstring> &progam)
{
  scanner = new Scanner(progam.first, alt_syntax, progam.second);
  NextToken();
  ParseBundle(0);
  // clean up
  delete scanner;
  scanner = nullptr;
}

/****************************
 * Parses a bundle.
 ****************************/
void Parser::ParseBundle(int depth)
{
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  if(Match(TOKEN_END_OF_STREAM)) {
    ProcessError(L"Unable to open source file: '" + file_name + L"'");
    return;
  }

  // default system uses
  std::vector<std::wstring> lib_uses;
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
          bundle = new ParsedBundle(file_name, line_num, line_pos, bundle_name, symbol_table);
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
            bundle->AddClass(ParseClass(bundle_name, depth + 1));
            break;

          case TOKEN_INTERFACE_ID:
            bundle->AddClass(ParseInterface(bundle_name, depth + 1));
            break;

          default:
            ProcessError(L"Expected 'class', 'interface', 'enum' or 'alias'", TOKEN_SEMI_COLON);
            NextToken();
            break;
          }
        }

        if(!Match(TOKEN_CLOSED_BRACE)) {
          ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
        }
        NextToken();

      }

      program->AddUses(lib_uses, static_uses, file_name);
    }
    // parse class
    else if(Match(TOKEN_CLASS_ID) || Match(TOKEN_ENUM_ID) || Match(TOKEN_CONSTS_ID) || Match(TOKEN_INTERFACE_ID) || Match(TOKEN_ALIAS_ID)) {
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
          bundle->AddClass(ParseClass(bundle_name, depth + 1));
          break;

        case TOKEN_ALIAS_ID:
          bundle->AddLambdas(ParseLambdas(depth + 1));
          break;

        case TOKEN_INTERFACE_ID:
          bundle->AddClass(ParseInterface(bundle_name, depth + 1));
          break;

        default:
          ProcessError(L"Expected 'class', 'interface', 'enum' or 'alias'", TOKEN_SEMI_COLON);
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
      ProcessError(L"Expected 'use', 'bundle', 'class, 'interface', 'enum' or 'consts'");
      NextToken();
    }
  }
}

/****************************
 * Parses an enum
 ****************************/
Enum* Parser::ParseEnum(int depth)
{
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring enum_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(enum_name) || current_bundle->GetEnum(enum_name) || current_bundle->GetAlias(enum_name)) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();
  const std::wstring enum_scope_name = GetEnumScopeName(enum_name);

  size_t index = enum_scope_name.find('#');
  if(index != std::wstring::npos) {
    const std::wstring use_name = enum_scope_name.substr(0, index + 1);
    program->AddUse(use_name, file_name);
  }

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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring alias_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(alias_name) || current_bundle->GetEnum(alias_name) || current_bundle->GetAlias(alias_name)) {
    ProcessError(L"Class, interface or alias name already defined in this bundle");
  }
  NextToken();
  const std::wstring alias_scope_name = GetEnumScopeName(alias_name);

  size_t index = alias_scope_name.find('#');
  if(index != std::wstring::npos) {
    const std::wstring use_name = alias_scope_name.substr(0, index + 1);
    program->AddUse(use_name, file_name);
  }

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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring enum_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(enum_name) || current_bundle->GetEnum(enum_name) || current_bundle->GetAlias(enum_name)) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();
  const std::wstring enum_scope_name = GetEnumScopeName(enum_name);

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
 * Parses a class.
 ****************************/
Class* Parser::ParseClass(const std::wstring &bundle_name, int depth)
{
  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  bool is_public = true;
  NextToken();

  if(Match(TOKEN_COLON)) {
    NextToken();
    if(Match(TOKEN_PUBLIC_ID)) {
      NextToken();
    }
    else if(Match(TOKEN_PRIVATE_ID)) {
      is_public = false;
      NextToken();
    }
    else {
      ProcessError(L"Expected 'public' or 'private'");
    }

    if(!Match(TOKEN_COLON)) {
      ProcessError(TOKEN_COLON);
    }
    NextToken();
  }

  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  std::wstring cls_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(cls_name) || current_bundle->GetEnum(cls_name) || current_bundle->GetAlias(cls_name)) {
    ProcessError(L"Class, interface, enum or alias name already defined in this bundle");
  }
  NextToken();

#ifdef _DEBUG
  Debug(L"[Class: name='" + cls_name + L"']", depth);
#endif

  // generic ids
  std::vector<Class*> generic_classes = ParseGenericClasses(bundle_name, depth);

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

  Class* klass = TreeFactory::Instance()->MakeClass(file_name, line_num, line_pos, -1, -1, cls_name, parent_cls_name,
                                                    interface_names, generic_classes, is_public, false);
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

  symbol_table->PreviousParseScope(current_class->GetName());
  current_class = nullptr;

  return klass;
}

/****************************
 * Parses an interface
 ****************************/
Class* Parser::ParseInterface(const std::wstring &bundle_name, int depth)
{
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  std::wstring cls_name = scanner->GetToken()->GetIdentifier();
  if(current_bundle->GetClass(cls_name) || current_bundle->GetEnum(cls_name) || current_bundle->GetAlias(cls_name)) {
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
 * Parses lambda expression
 ****************************/
Lambda* Parser::ParseLambda(int depth) {
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
  Type* type = nullptr;
  std::wstring alias_name;
  if(Match(TOKEN_OPEN_PAREN)) {
    type = ParseType(depth + 1);

    if(!Match(TOKEN_COLON)) {
      ProcessError(TOKEN_COLON);
    }
    NextToken();
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
  ExpressionList* parameter_list = ParseExpressionList(end_pos, depth + 1);

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
  if(!Match(TOKEN_LAMBDA)) {
    ProcessError(L"Expected '=>'", TOKEN_SEMI_COLON);
  }
  NextToken();

  // parse statement
  StatementList * statements = TreeFactory::Instance()->MakeStatementList();
  if(Match(TOKEN_OPEN_BRACE)) {
    NextToken();

    Statement* statement = ParseStatement(depth + 1);
    if(!statement) {
      return nullptr;
    }

    if(statement->GetStatementType() != IF_STMT && statement->GetStatementType() != SELECT_STMT) {
      ProcessError(L"Expected 'if' or 'select' statement", TOKEN_SEMI_COLON);
    }
    statements->AddStatement(statement);

    if(!Match(TOKEN_CLOSED_BRACE)) {
      ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
    }
    NextToken();
  }
  // parse expression
  else {
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

/****************************
 * Parses a method.
 ****************************/
Method* Parser::ParseMethod(bool is_function, bool virtual_requried, int depth)
{
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
  }

#ifdef _DEBUG
  Debug(L"(Method/Function/New: name='" + method_name + L"')", depth);
#endif

  Method* method = TreeFactory::Instance()->MakeMethod(file_name, line_num, line_pos, mid_line_num, mid_line_pos, method_name, method_type, is_function, is_native);
  current_method = method;

  // declarations
  symbol_table->NewParseScope();
  method->SetDeclarations(ParseDecelerationList(depth + 1));

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
  }

  method->SetEndLineNumber(GetLineNumber());
  method->SetEndLinePosition(GetLinePosition());

  symbol_table->PreviousParseScope(method->GetParsedName());
  current_method = nullptr;

  return method;
}

/****************************
 * Parses a statement std::list.
 ****************************/
StatementList* Parser::ParseStatementList(int depth)
{
#ifdef _DEBUG
  Debug(L"Statement List", depth);
#endif

  // statement list
  if(!Match(TOKEN_OPEN_BRACE)) {
    ProcessError(L"Expected '{'", TOKEN_OPEN_BRACE);
  }
  NextToken();

  StatementList* statements = TreeFactory::Instance()->MakeStatementList();
  while(!Match(TOKEN_CLOSED_BRACE) && !Match(TOKEN_END_OF_STREAM)) {
    statements->AddStatement(ParseStatement(depth + 1));
  }
  if(!Match(TOKEN_CLOSED_BRACE)) {
    ProcessError(L"Expected '}'", TOKEN_CLOSED_BRACE);
  }
  NextToken();

  return statements;
}

/****************************
 * Parses a statement.
 ****************************/
Statement* Parser::ParseStatement(int depth, bool semi_colon)
{
#ifdef _DEBUG
  Debug(L"Statement", depth);
#endif

  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  Statement* statement = nullptr;
  is_semi_colon = false;

  // identifier
  if(Match(TOKEN_IDENT)) {
    const std::wstring ident = ParseBundleName();

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
      statement = ParseMethodCall(ident_context, depth + 1);
    }
      break;

    case TOKEN_OPEN_BRACKET: {
      IdentifierContext ident_context(ident, line_num, line_pos);
      Variable* variable = ParseVariable(ident_context, depth + 1);

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
        ProcessError(L"Expected statement", TOKEN_SEMI_COLON);
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
  }
  // other
  else {
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

    case COMPRESS_BR_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::COMPRESS_BR_BYTES);
      NextToken();
      break;

    case UNCOMPRESS_BR_BYTES:
      statement = TreeFactory::Instance()->MakeSystemStatement(file_name, line_num, line_pos, GetLineNumber(), GetLinePosition(),
                                                               instructions::UNCOMPRESS_BR_BYTES);
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
          ProcessError(L"Invalid statement expected ';'", TOKEN_SEMI_COLON);
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
 * Parses a static array
 ****************************/
StaticArray* Parser::ParseStaticArray(int depth) {
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
      if(Match(TOKEN_SUB)) {
        NextToken();

        switch(GetToken()) {
        case TOKEN_INT_LIT:
          expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos,
                                                                   -scanner->GetToken()->GetInt64Lit());
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
          expression = TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, ident, is_lit);
          NextToken();
        }
                                    break;

        case TOKEN_BAD_CHAR_STRING_LIT:
          ProcessError(L"Invalid escaped std::string literal", TOKEN_SEMI_COLON);
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

    // next row
    if(Match(TOKEN_COMMA)) {
      NextToken();
    }
  }

  return TreeFactory::Instance()->MakeStaticArray(file_name, line_num, line_pos, expressions);
}

/****************************
 * Parses a variable.
 ****************************/
Variable* Parser::ParseVariable(IdentifierContext& context, int depth)
{
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

  return variable;
}

/****************************
 * Parses generic types
 ****************************/
std::vector<Type*> Parser::ParseGenericTypes(int depth)
{
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
        else {
          generic_types.push_back(type);

          if(expand_generic_def) {
            expand_generic_def = false;
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
        if(expand_generic_def) {
          expand_generic_def = false;
        }
        else if(Match(TOKEN_SHR)) {
          expand_generic_def = true;
          NextToken();
        }
        else {
          expand_generic_def = false;
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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  std::vector<Class*> generic_classes;

  if(Match(TOKEN_LES)) {
    NextToken();

    while(!Match(TOKEN_GTR) && !Match(TOKEN_END_OF_STREAM)) {
      // identifier
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
      std::wstring generic_klass = scanner->GetToken()->GetIdentifier();
      NextToken();

      Class* klass = TreeFactory::Instance()->MakeClass(file_name, line_num, line_pos, -1, -1, generic_klass, true);
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
      }

      if(Match(TOKEN_LES)) {
        std::vector<Class*> generic_klasss = ParseGenericClasses(bundle_name, depth + 1);
        klass->SetGenericClasses(generic_klasss);
      }

      if(Match(TOKEN_COMMA) && !Match(TOKEN_GTR, SECOND_INDEX)) {
        NextToken();
      }
      else if(!Match(TOKEN_GTR)) {
        if(expand_generic_def) {
          expand_generic_def = false;
        }
        else if(Match(TOKEN_SHR)) {
          expand_generic_def = true;
          NextToken();
        }
        else {
          expand_generic_def = false;
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
 * Parses a declaration std::list.
 ****************************/
DeclarationList* Parser::ParseDecelerationList(int depth)
{
#ifdef _DEBUG
  Debug(L"Declaration Parameters", depth);
#endif

  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected '('", TOKEN_OPEN_PAREN);
  }
  NextToken();

  DeclarationList* declarations = TreeFactory::Instance()->MakeDeclarationList();
  while(!Match(TOKEN_CLOSED_PAREN) && !Match(TOKEN_END_OF_STREAM)) {
    if(!Match(TOKEN_IDENT)) {
      ProcessError(TOKEN_IDENT);
    }
    // identifier
    const std::wstring ident = scanner->GetToken()->GetIdentifier();
    const int line_num = GetLineNumber();
    const int line_pos = GetLinePosition();

    IdentifierContext ident_context(ident, line_num, line_pos);
    NextToken();

    declarations->AddDeclaration(ParseDeclaration(ident_context, false, depth + 1));

    if(Match(TOKEN_COMMA)) {
      NextToken();
      if(!Match(TOKEN_IDENT)) {
        ProcessError(TOKEN_IDENT);
      }
    }
    else if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ',' or ')'", TOKEN_CLOSED_BRACE);
      NextToken();
    }
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
ExpressionList* Parser::ParseExpressionList(int &end_pos, int depth, ScannerTokenType open, ScannerTokenType closed)
{
#ifdef _DEBUG
  Debug(L"Calling Parameters", depth);
#endif

  ExpressionList* expressions = TreeFactory::Instance()->MakeExpressionList();
  if(!Match(open)) {
    ProcessError(open);
  }
  NextToken();

  while(!Match(closed) && !Match(TOKEN_END_OF_STREAM)) {
    // expression
    Expression* expression = ParseExpression(depth + 1);
    if(expression) {
      expressions->AddExpression(expression);

      if(Match(TOKEN_COMMA) && !Match(closed, SECOND_INDEX)) {
        NextToken();
      }
      else if((expression->GetExpressionType() == LAMBDA_EXPR && Match(TOKEN_SEMI_COLON))) {
        NextToken();
        break;
      }
    }
    else {
      NextToken();
    }
  }

  if(!Match(closed)) {
    ProcessError(closed);
  }
  end_pos = GetLinePosition() + 1;

  NextToken();

  return expressions;
}

/****************************
 * Parses array indices.
 ****************************/
ExpressionList* Parser::ParseIndices(int depth)
{
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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Expression", depth);
#endif

  Expression* expression = nullptr;
  if(Match(TOKEN_NEQL) || (alt_syntax && Match(TOKEN_NOT))) {
    return ParseLogic(depth + 1);
  }
  else {
    expression = ParseLogic(depth + 1);
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
      return TreeFactory::Instance()->MakeCond(file_name, line_num, line_pos, expression, if_expression, ParseLogic(depth + 1));
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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Factor", depth);
#endif

  Expression* left = ParseSimpleExpression(depth + 1);
  if(!(GetToken() >= TOKEN_MUL && GetToken() <= TOKEN_XOR_ID) && !Match(TOKEN_XOR_ID)) {
    return left;
  }
  
  CalculatedExpression* expression = nullptr;
  while(GetToken() >= TOKEN_MUL && GetToken() <= TOKEN_XOR_ID) {
    if(expression) {
      CalculatedExpression* right;
      switch(GetToken()) {
      case TOKEN_MUL:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MUL_EXPR);
        break;

      case TOKEN_MOD:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MOD_EXPR);
        break;

      case TOKEN_SHL:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SHL_EXPR);
        break;

      case TOKEN_SHR:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SHR_EXPR);
        break;

      case TOKEN_AND_ID:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_AND_EXPR);
        break;

      case TOKEN_OR_ID:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_OR_EXPR);
        break;

      case TOKEN_XOR_ID:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_XOR_EXPR);
        break;

      case TOKEN_DIV:
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, DIV_EXPR);
        break;

      default:
        right = nullptr;
        break;
      }
      NextToken();

      Expression* temp = ParseSimpleExpression(depth + 1);
      if(right) {
        right->SetRight(temp);
        right->SetLeft(expression);
        expression = right;
      }
    }
    // first time in loop
    else {
      switch(GetToken()) {
      case TOKEN_MUL:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MUL_EXPR);
        break;

      case TOKEN_MOD:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, MOD_EXPR);
        break;

      case TOKEN_SHL:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SHL_EXPR);
        break;

      case TOKEN_SHR:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, SHR_EXPR);
        break;

      case TOKEN_AND_ID:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_AND_EXPR);
        break;

      case TOKEN_OR_ID:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_OR_EXPR);
        break;

      case TOKEN_XOR_ID:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, BIT_XOR_EXPR);
        break;

      case TOKEN_DIV:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, line_pos, DIV_EXPR);
        break;

      default:
        expression = nullptr;
        break;
      }
      NextToken();

      Expression* temp = ParseSimpleExpression(depth + 1);
      if(expression) {
        expression->SetRight(temp);
        expression->SetLeft(left);
      }
    }
  }

  return expression;
}

/****************************
 * Parses a simple expression.
 ****************************/
Expression* Parser::ParseSimpleExpression(int depth)
{
  int line_num = GetLineNumber();
  int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Simple expression", depth);
#endif
  
  Expression* expression = nullptr;

  if(Match(TOKEN_IDENT) || Match(TOKEN_ADD_ADD) || Match(TOKEN_SUB_SUB) || IsBasicType(GetToken())) {
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
    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
    }
    NextToken();
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
      expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num, line_pos, -scanner->GetToken()->GetInt64Lit());
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

    case TOKEN_UNKNOWN:
      ProcessError(L"Unknown token in an invalid expression ", TOKEN_SEMI_COLON);
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
      expression = TreeFactory::Instance()->MakeCharacterString(file_name, line_num, line_pos, ident, is_lit);
      NextToken();
    }
      break;

    case TOKEN_BAD_CHAR_STRING_LIT:
      ProcessError(L"Invalid escaped string literal", TOKEN_SEMI_COLON);
      NextToken();
      break;

    case TOKEN_OPEN_BRACKET:
      expression = ParseStaticArray(depth + 1);
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
      ProcessError(L"Unknown token in an invalid expression ", TOKEN_SEMI_COLON);
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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Parent call", depth);
#endif

  NextToken();

  int end_pos = 0;
  ExpressionList* exprs = ParseExpressionList(end_pos, depth + 1);

  return TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, GetLineNumber(), end_pos, PARENT_CALL, L"", exprs);
}

/****************************
 * Parses a method call.
 ****************************/
MethodCall* Parser::ParseMethodCall(IdentifierContext& context, int depth)
{
  const int line_num = context.GetLineNumber();
  const int line_pos = context.GetLinePosition();
  const std::wstring ident = context.GetIdentifier();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Method call", depth);
#endif

  MethodCall* method_call = nullptr;
  if(Match(TOKEN_ASSESSOR)) {
    NextToken();

    // method call
    if(Match(TOKEN_IDENT)) {
      const int mid_line_num = GetLineNumber();
      const int mid_line_pos = GetLinePosition();
      const std::wstring method_ident = scanner->GetToken()->GetIdentifier();
      NextToken();

      if(Match(TOKEN_OPEN_PAREN)) {
        int end_pos = 0;
        ExpressionList* exprs = ParseExpressionList(end_pos, depth + 1);

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
      NextToken();
      // new array
      if(Match(TOKEN_OPEN_BRACKET)) {
        int end_pos = 0;
        ExpressionList* expressions = ParseExpressionList(end_pos, depth + 1, TOKEN_OPEN_BRACKET, TOKEN_CLOSED_BRACKET);

        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, 
                                                              GetLineNumber(), end_pos, NEW_ARRAY_CALL, ident, expressions);
        // array of generics
        if(Match(TOKEN_LES)) {
          std::vector<Type*> generic_dclrs = ParseGenericTypes(depth);
          method_call->SetConcreteTypes(generic_dclrs);
        }
      }
      // new object
      else {
        int end_pos = 0;
        ExpressionList* exprs = ParseExpressionList(end_pos, depth + 1);

        method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, 
                                                              GetLineNumber(), end_pos, NEW_INST_CALL, ident, exprs);
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

      // subsequent method calls
      if(Match(TOKEN_ASSESSOR)) {
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
    const std::wstring klass_name = current_class->GetName();
    
    int end_pos = 0;
    ExpressionList* exprs = ParseExpressionList(end_pos, depth + 1);

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

  // subsequent method calls
  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) &&
     !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
    ParseMethodCall(method_call, depth + 1);
  }
  // type cast
  else {
    ParseCastTypeOf(method_call, depth + 1);
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
#ifdef _DEBUG
  Debug(L"Method call", depth);
#endif

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
    expression->SetMethodCall(ParseMethodCall(ident_context, depth + 1));
    // subsequent method calls
    if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) &&
       !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
      ParseMethodCall(expression->GetMethodCall(), depth + 1);
    }
    // type cast
    else {
      ParseCastTypeOf(expression->GetMethodCall(), depth + 1);
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
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  NextToken();
  const std::wstring &method_ident = scanner->GetToken()->GetIdentifier();
  
  NextToken();
  const int mid_line_num = GetLineNumber();
  const int mid_line_pos = GetLinePosition();
  const std::wstring mid_ident = variable->GetName();
  IdentifierContext ident_context(mid_ident, mid_line_num, mid_line_pos);
  
  int end_pos = 0;
  ExpressionList* exprs = ParseExpressionList(end_pos, depth + 1);

  MethodCall* call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, line_pos, mid_line_num, mid_line_pos,
                                                             GetLineNumber(), end_pos, variable, method_ident, exprs);

  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX) && !Match(TOKEN_TYPE_OF_ID, SECOND_INDEX)) {
    call->SetMethodCall(ParseMethodCall(ident_context, depth + 1));
  }

  return call;
}

/****************************
 * Parses an anonymous class
 ****************************/
void Parser::ParseAnonymousClass(MethodCall* method_call, int depth)
{
  const int line_num = GetLineNumber();
  const int line_pos = GetLinePosition();
  const std::wstring file_name = GetFileName();

  if(prev_method && current_method) {
    ProcessError(L"Invalid anonymous nested classes");
    return;
  }

  const std::wstring cls_name = method_call->GetVariableName() + L".#Anonymous." + RandomString(8) + L'#';

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
    
    klass->AddMethod(default_new);
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

  Expression* expression = ParseExpression(depth + 1);
  if(!Match(TOKEN_CLOSED_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_CLOSED_PAREN);
  }
  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  StatementList* if_statements = ParseStatementList(depth + 1);
  symbol_table->CurrentParseScope()->PreviousParseScope();

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
 * Parses a 'do/while' statement
 ****************************/
DoWhile* Parser::ParseDoWhile(int depth)
{
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
CriticalSection* Parser::ParseCritical(int depth)
{
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
  const int line_num = GetLineNumber();
  const std::wstring file_name = GetFileName();

#ifdef _DEBUG
  Debug(L"Each/Reverse", depth);
#endif

  NextToken();
  symbol_table->CurrentParseScope()->NewParseScope();
  if(!Match(TOKEN_OPEN_PAREN)) {
    ProcessError(L"Expected ')'", TOKEN_OPEN_PAREN);
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
 * Parses a 'for' statement
 ****************************/
For* Parser::ParseFor(int depth)
{
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
    pre_statements->AddStatement(ParseStatement(depth + 1));
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
    update_stmts->AddStatement(ParseStatement(depth + 1, false));
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
 * Parses a return statement
 ****************************/
Return* Parser::ParseReturn(int depth)
{
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

  return assignment;
}

/****************************
 * Parses a data type identifier.
 ****************************/
Type* Parser::ParseType(int depth)
{
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
    type = TypeFactory::Instance()->MakeType(CLASS_TYPE, ident, file_name, line_num, line_pos);
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
    type->SetDimension(dimension);

    // generic
    if(Match(TOKEN_LES)) {
      const std::vector<Type*> generic_types = ParseGenericTypes(depth);
      type->SetGenerics(generic_types);
    }
  }

  return type;
}
