/***************************************************************************
 * Debugger parser.
 *
 * Copyright (c) 2010-2013 Randy Hollines
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

#include "parser.h"

 /****************************
  * Loads parsing error codes.
  ****************************/
void Parser::LoadErrorCodes()
{
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
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(enum TokenType type)
{
  std::wstring msg = error_msgs[type];
#ifdef _DEBUG
  std::wcout << L"\tError: "
    << msg << std::endl;
#endif

  errors.push_back(msg);
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const std::wstring &msg)
{
#ifdef _DEBUG
  std::wcout << L"\tError: " << msg << std::endl;
#endif

  errors.push_back(msg);
}

/****************************
 * Checks for parsing errors.
 ****************************/
bool Parser::CheckErrors()
{
  // check and process errors
  if(errors.size()) {
    for(size_t i = 0; i < errors.size(); i++) {
      std::wcerr << errors[i] << std::endl;
    }
    // clean up
    return false;
  }

  return true;
}

/****************************
 * Starts the parsing process.
 ****************************/
Command* Parser::Parse(const std::wstring &line)
{
#ifdef _DEBUG
  std::wcout << L"\n---------- Scanning/Parsing ---------" << std::endl;
#endif
  scanner = new Scanner(line);
  NextToken();

  // parse input
  Command* command = ParseLine(line);
  if(CheckErrors()) {
    return command;
  }

  return nullptr;
}

/****************************
 * Parses a file.
 ****************************/
Command* Parser::ParseLine(const std::wstring &line)
{
  Command* command = ParseStatement(0);
  if(!Match(TOKEN_END_OF_STREAM)) {
    ProcessError(L"Expected statement end");
  }

  // clean up
  delete scanner;
  scanner = nullptr;

  return command;
}

/****************************
 * Parses a bundle.
 ****************************/
Command* Parser::ParseStatement(int depth)
{
  Command* command;
 switch(GetToken()) {
    case TOKEN_EXE_ID:
      command = ParseLoad(EXE_COMMAND, depth + 1);
      break;

    case TOKEN_SRC_ID:
      command = ParseLoad(SRC_COMMAND, depth + 1);
      break;

    case TOKEN_ARGS_ID:
      command = ParseLoad(ARGS_COMMAND, depth + 1);
      break;

    case TOKEN_LIST_ID:
      command = ParseList(depth + 1);
      break;

    case TOKEN_QUIT_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(QUIT_COMMAND);
      break;

    case TOKEN_BREAKS_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(BREAKS_COMMAND);
      break;

    case TOKEN_RUN_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(RUN_COMMAND);
      break;

    case TOKEN_NEXT_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(STEP_IN_COMMAND);
      break;

    case TOKEN_NEXT_LINE_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(NEXT_LINE_COMMAND);
      break;

    case TOKEN_OUT_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(STEP_OUT_COMMAND);
      break;

    case TOKEN_STACK_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(STACK_COMMAND);
      break;

    case TOKEN_CONT_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(CONT_COMMAND);
      break;

    case TOKEN_MEMORY_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(MEMORY_COMMAND);
      break;

    case TOKEN_BREAK_ID:
      command = ParseBreakDelete(true, depth + 1);
      break;

    case TOKEN_DELETE_ID:
      command = ParseBreakDelete(false, depth + 1);
      break;

    case TOKEN_PRINT_ID:
      command = ParsePrint(depth + 1);
      break;

    case TOKEN_INFO_ID:
      command = ParseInfo(depth + 1);
      break;

    case TOKEN_FRAME_ID:
      command = ParseFrame(depth + 1);
      break;

    case TOKEN_CLEAR_ID:
      NextToken();
      command = TreeFactory::Instance()->MakeBasicCommand(CLEAR_COMMAND);
      break;

    default:
      command = nullptr;
      break;
  }

  return command;
}

Command* Parser::ParseList(int depth) {
#ifdef _DEBUG
  Show(L"List", depth);
#endif
  NextToken();

  std::wstring file_name;
  int line_num = -1;
  if(Match(TOKEN_IDENT)) {
    file_name = scanner->GetToken()->GetIdentifier();
    NextToken();
    // colon
    if(!Match(TOKEN_COLON)) {
      ProcessError(TOKEN_COLON);
    }
    NextToken();
    // line number
    if(Match(TOKEN_INT_LIT)) {
      line_num = scanner->GetToken()->GetIntLit();
    }
    else {
      ProcessError(L"Expected line number");
    }
  }
  else if(Match(TOKEN_CHAR_STRING_LIT)) {
    CharacterString* char_string =
      TreeFactory::Instance()->MakeCharacterString(scanner->GetToken()->GetIdentifier());
    file_name = char_string->GetString();
    NextToken();
    // colon
    if(!Match(TOKEN_COLON)) {
      ProcessError(TOKEN_COLON);
    }
    NextToken();
    // line number
    if(Match(TOKEN_INT_LIT)) {
      line_num = scanner->GetToken()->GetIntLit();
    }
    else {
      ProcessError(L"Expected line number");
    }
  }
  NextToken();

  return TreeFactory::Instance()->MakeFilePostion(LIST_COMMAND, file_name, line_num);
}

Command* Parser::ParseLoad(CommandType type, int depth) {
#ifdef _DEBUG
  Show(L"Load", depth);
#endif
  NextToken();

  std::wstring file_name;
  if(Match(TOKEN_IDENT)) {
    file_name = scanner->GetToken()->GetIdentifier();
  }
  else if(Match(TOKEN_CHAR_STRING_LIT)) {
    CharacterString* char_string =
      TreeFactory::Instance()->MakeCharacterString(scanner->GetToken()->GetIdentifier());
    file_name = char_string->GetString();
  }
  else {
    ProcessError(L"Expected filename (ensure the argument is wrapped in double quotes)");
  }
  NextToken();

  return TreeFactory::Instance()->MakeLoad(type, file_name);
}

Command* Parser::ParseBreakDelete(bool is_break, int depth) {
#ifdef _DEBUG
  Show(L"Break", depth);
#endif
  NextToken();

  // file name
  std::wstring file_name;
  if(Match(TOKEN_IDENT)) {
    file_name = scanner->GetToken()->GetIdentifier();
    NextToken();
    if(!Match(TOKEN_COLON)) {
      ProcessError(TOKEN_COLON);
    }
    NextToken();
  }

  // line number
  int line_num = -1;
  if(Match(TOKEN_INT_LIT)) {
    line_num = scanner->GetToken()->GetIntLit();
    NextToken();
  }
  else {
    line_num = -1;
    NextToken();
  }

  if(is_break) {
    return TreeFactory::Instance()->MakeFilePostion(BREAK_COMMAND, file_name, line_num);
  }

  return TreeFactory::Instance()->MakeFilePostion(DELETE_COMMAND, file_name, line_num);
}

Command* Parser::ParsePrint(int depth) {
  NextToken();
  return TreeFactory::Instance()->MakePrint(ParseExpression(depth + 1));
}

Command* Parser::ParseInfo(int depth) {
  std::wstring cls_name;
  std::wstring mthd_name;

  NextToken();

  // class name
  if(Match(TOKEN_CLASS_ID)) {
    NextToken();
    if(!Match(TOKEN_EQL)) {
      ProcessError(L"Expected equal sign");
    }
    NextToken();
    // name
    if(Match(TOKEN_IDENT)) {
      cls_name = scanner->GetToken()->GetIdentifier();
    }
    else if(Match(TOKEN_CHAR_STRING_LIT)) {
      CharacterString* char_string =
        TreeFactory::Instance()->MakeCharacterString(scanner->GetToken()->GetIdentifier());
      cls_name = char_string->GetString();
    }
    else {
      ProcessError(TOKEN_IDENT);
    }
    NextToken();

    // method name
    if(Match(TOKEN_METHOD_ID)) {
      NextToken();
      if(!Match(TOKEN_EQL)) {
        ProcessError(L"Expected equal sign");
      }
      NextToken();
      // name
      if(Match(TOKEN_IDENT)) {
        mthd_name = scanner->GetToken()->GetIdentifier();
      }
      else if(Match(TOKEN_CHAR_STRING_LIT)) {
        CharacterString* char_string =
          TreeFactory::Instance()->MakeCharacterString(scanner->GetToken()->GetIdentifier());
        mthd_name = char_string->GetString();
      }
      else {
        ProcessError(TOKEN_IDENT);
      }
      NextToken();
    }
  }

  return TreeFactory::Instance()->MakeInfo(cls_name, mthd_name);
}

Command* Parser::ParseFrame(int depth) {
  NextToken();
  return nullptr;
}

/****************************
 * Parses array indices.
 ****************************/
ExpressionList* Parser::ParseIndices(int depth)
{
  ExpressionList* expressions = nullptr;
  if(Match(TOKEN_OPEN_BRACKET)) {
    expressions = TreeFactory::Instance()->MakeExpressionList();
    NextToken();

    while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
      // expression
      expressions->AddExpression(ParseExpression(depth + 1));

      if(Match(TOKEN_COMMA)) {
        NextToken();
      }
      else if(!Match(TOKEN_CLOSED_BRACKET)) {
        ProcessError(L"Expected comma or ']'");
        NextToken();
      }
    }

    if(!Match(TOKEN_CLOSED_BRACKET)) {
      ProcessError(TOKEN_CLOSED_BRACKET);
    }
    NextToken();
  }

  return expressions;
}

/****************************
 * Parses an expression.
 ****************************/
Expression* Parser::ParseExpression(int depth)
{
#ifdef _DEBUG
  Show(L"Expression", depth);
#endif

  return ParseLogic(depth + 1);
}

/****************************
 * Parses a logical expression.
 * This method delegates support
 * for other types of expressions.
 ****************************/
Expression* Parser::ParseLogic(int depth)
{
#ifdef _DEBUG
  Show(L"Boolean logic", depth);
#endif

  Expression* left = ParseMathLogic(depth + 1);

  CalculatedExpression* expression = nullptr;
  while((Match(TOKEN_AND) || Match(TOKEN_OR)) && !Match(TOKEN_END_OF_STREAM)) {
    if(expression) {
      left = expression;
    }

    switch(GetToken()) {
      case TOKEN_AND:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(AND_EXPR);
        break;
      case TOKEN_OR:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(OR_EXPR);
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

#ifdef _DEBUG
  Show(L"Boolean math", depth);
#endif

  Expression* left = ParseTerm(depth + 1);

  if(Match(TOKEN_LES) || Match(TOKEN_GTR) ||
     Match(TOKEN_LEQL) || Match(TOKEN_GEQL) ||
     Match(TOKEN_EQL) || Match(TOKEN_NEQL)) {
    CalculatedExpression* expression = nullptr;
    switch(GetToken()) {
      case TOKEN_LES:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(LES_EXPR);
        break;
      case TOKEN_GTR:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(GTR_EXPR);
        break;
      case TOKEN_LEQL:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(LES_EQL_EXPR);
        break;
      case TOKEN_GEQL:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(GTR_EQL_EXPR);
        break;
      case TOKEN_EQL:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(EQL_EXPR);
        break;
      case TOKEN_NEQL:
        expression = TreeFactory::Instance()->MakeCalculatedExpression(NEQL_EXPR);
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

#ifdef _DEBUG
  Show(L"Term", depth);
#endif

  Expression* left = ParseFactor(depth + 1);
  if(!left) {
    return nullptr;
  }

  if(!Match(TOKEN_ADD) && !Match(TOKEN_SUB)) {
    return left;
  }

  CalculatedExpression* expression = nullptr;
  while((Match(TOKEN_ADD) || Match(TOKEN_SUB)) && !Match(TOKEN_END_OF_STREAM)) {
    if(expression) {
      CalculatedExpression* right;
      if(Match(TOKEN_ADD)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(ADD_EXPR);
      }
      else {
        right = TreeFactory::Instance()->MakeCalculatedExpression(SUB_EXPR);
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
        expression = TreeFactory::Instance()->MakeCalculatedExpression(ADD_EXPR);
      }
      else {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(SUB_EXPR);
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

#ifdef _DEBUG
  Show(L"Factor", depth);
#endif

  Expression* left = ParseSimpleExpression(depth + 1);
  if(!Match(TOKEN_MUL) && !Match(TOKEN_DIV) && !Match(TOKEN_MOD)) {
    return left;
  }

  CalculatedExpression* expression = nullptr;
  while((Match(TOKEN_MUL) || Match(TOKEN_DIV) || Match(TOKEN_MOD)) &&
        !Match(TOKEN_END_OF_STREAM)) {
    if(expression) {
      CalculatedExpression* right;
      if(Match(TOKEN_MUL)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(MUL_EXPR);
      }
      else if(Match(TOKEN_MOD)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(MOD_EXPR);
      }
      else {
        right = TreeFactory::Instance()->MakeCalculatedExpression(DIV_EXPR);
      }
      NextToken();

      Expression* temp = ParseSimpleExpression(depth + 1);
      right->SetRight(temp);
      right->SetLeft(expression);
      expression = right;
    }
    // first time in loop
    else {
      if(Match(TOKEN_MUL)) {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(MUL_EXPR);
      }
      else if(Match(TOKEN_MOD)) {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(MOD_EXPR);
      }
      else {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(DIV_EXPR);
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
#ifdef _DEBUG
  Show(L"Simple expression", depth);
#endif
  Expression* expression = nullptr;

  if(Match(TOKEN_IDENT)) {
    const std::wstring &ident = scanner->GetToken()->GetIdentifier();
    NextToken();
    expression = ParseReference(ident, depth + 1);
  }
  else if(Match(TOKEN_SELF_ID)) {
    NextToken();
    expression = ParseReference(depth + 1);
  }
  else if(Match(TOKEN_SUB)) {
    NextToken();

    switch(GetToken()) {
      case TOKEN_INT_LIT:
        expression = TreeFactory::Instance()->MakeIntegerLiteral(-scanner->GetToken()->GetIntLit());
        NextToken();
        break;

      case TOKEN_FLOAT_LIT:
        expression = TreeFactory::Instance()->MakeFloatLiteral(-scanner->GetToken()->GetFloatLit());
        NextToken();
        break;

      default:
        ProcessError(L"Expected expression");
        NextToken();
        break;
    }
  }
  else if(Match(TOKEN_OPEN_PAREN)) {
    NextToken();
    expression = ParseLogic(depth + 1);
    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(TOKEN_CLOSED_PAREN);
    }
    NextToken();
  }
  else {
    switch(GetToken()) {
      case TOKEN_CHAR_LIT:
        expression = TreeFactory::Instance()->MakeCharacterLiteral(scanner->GetToken()->GetCharLit());
        NextToken();
        break;

      case TOKEN_INT_LIT:
        expression = TreeFactory::Instance()->MakeIntegerLiteral(scanner->GetToken()->GetIntLit());
        NextToken();
        break;

      case TOKEN_FLOAT_LIT:
        expression = TreeFactory::Instance()->MakeFloatLiteral(scanner->GetToken()->GetFloatLit());
        NextToken();
        break;

      case TOKEN_CHAR_STRING_LIT:
      {
        const std::wstring &ident = scanner->GetToken()->GetIdentifier();
        expression = TreeFactory::Instance()->MakeCharacterString(ident);
        NextToken();
      }
      break;

      default:
        ProcessError(L"Expected expression");
        NextToken();
        break;
    }
  }

  // subsequent instance references
  if(Match(TOKEN_ASSESSOR)) {
    if(expression && expression->GetExpressionType() == REF_EXPR) {
      ParseReference(static_cast<Reference*>(expression), depth + 1);
    }
    else {
      ProcessError(L"Expected reference");
      NextToken();
    }
  }

  return expression;
}

/****************************
 * Parses a instance reference.
 ****************************/
Reference* Parser::ParseReference(int depth)
{
#ifdef _DEBUG
  Show(L"Instance reference", depth);
#endif

  // self reference
  Reference* inst_ref = TreeFactory::Instance()->MakeReference();

  // subsequent instance references
  if(Match(TOKEN_ASSESSOR)) {
    ParseReference(inst_ref, depth + 1);
  }

  return inst_ref;
}

/****************************
 * Parses a instance reference.
 ****************************/
Reference* Parser::ParseReference(const std::wstring &ident, int depth)
{
#ifdef _DEBUG
  Show(L"Instance reference", depth);
#endif

  Reference* inst_ref = TreeFactory::Instance()->MakeReference(ident);
  if(Match(TOKEN_OPEN_BRACKET)) {
    inst_ref->SetIndices(ParseIndices(depth + 1));
  }

  // subsequent instance references
  if(Match(TOKEN_ASSESSOR)) {
    ParseReference(inst_ref, depth + 1);
  }

  return inst_ref;
}

/****************************
 * Parses an instance reference.
 ****************************/
void Parser::ParseReference(Reference* reference, int depth)
{
#ifdef _DEBUG
  Show(L"Instance reference", depth);
#endif

  NextToken();
  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const std::wstring &ident = scanner->GetToken()->GetIdentifier();
  NextToken();

  if(reference) {
    reference->SetReference(ParseReference(ident, depth + 1));
    // subsequent instance references
    if(Match(TOKEN_ASSESSOR)) {
      ParseReference(reference->GetReference(), depth + 1);
    }
  }
}

