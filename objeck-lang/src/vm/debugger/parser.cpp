/***************************************************************************
 * Language parser.
 *
 * Copyright (c) 2008-2010 Randy Hollines
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

#define SELF_ID "@self"

/****************************
 * Loads parsing error codes.
 ****************************/
void Parser::LoadErrorCodes()
{
  error_msgs[TOKEN_IDENT] = "Expected identifier";
  error_msgs[TOKEN_OPEN_PAREN] = "Expected '('";
  error_msgs[TOKEN_CLOSED_PAREN] = "Expected ')'";
  error_msgs[TOKEN_OPEN_BRACKET] = "Expected '['";
  error_msgs[TOKEN_CLOSED_BRACKET] = "Expected ']'";
  error_msgs[TOKEN_OPEN_BRACE] = "Expected '{'";
  error_msgs[TOKEN_CLOSED_BRACE] = "Expected '}'";
  error_msgs[TOKEN_COLON] = "Expected ':'";
  error_msgs[TOKEN_COMMA] = "Expected ','";
  error_msgs[TOKEN_ASSIGN] = "Expected ':='";
  error_msgs[TOKEN_SEMI_COLON] = "Expected ';'";
  error_msgs[TOKEN_ASSESSOR] = "Expected '->'";
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(TokenType type)
{
  string msg = error_msgs[type];
#ifdef _DEBUG
  cout << "\tError: "
       << msg << endl;
#endif

  errors.insert(pair<int, string>("", msg));
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const string &msg)
{
#ifdef _DEBUG
  cout << "\tError: "
       << msg << endl;
#endif

  errors.insert(pair<int, string>("", msg));
}

/****************************
 * Emits parsing error.
 ****************************/
void Parser::ProcessError(const string &msg, TokenType sync)
{
#ifdef _DEBUG
  cout << "\tError: " << msg << endl;
#endif

  errors.insert(pair<int, string>("", msg));
  TokenType token = GetToken();
  while(token != sync && token != TOKEN_END_OF_STREAM) {
    NextToken();
    token = GetToken();
  }
}

/****************************
 * Checks for parsing errors.
 ****************************/
bool Parser::CheckErrors()
{
  // check and process errors
  if(errors.size()) {
    map<int, string>::iterator error;
    for(error = errors.begin(); error != errors.end(); error++) {
      cerr << error->second << endl;
    }

    // clean up    
    return false;
  }

  return true;
}

/****************************
 * Starts the parsing process.
 ****************************/
bool Parser::Parse(const string &line)
{
#ifdef _DEBUG
  cout << "\n---------- Scanning/Parsing ---------" << endl;
#endif

  // parse input
  ParseLine(line);
  return CheckErrors();
}

/****************************
 * Parses a file.
 ****************************/
void Parser::ParseLine(const string &line)
{
  scanner = new Scanner(line);
  NextToken();
  // TODO:

  // clean up
  delete scanner;
  scanner = NULL;
}

/****************************
 * Parses a bundle.
 ****************************/
void Parser::ParseStatement(int depth)
{
  
}


/****************************
 * Parses a expression list.
 ****************************/
ExpressionList* Parser::ParseExpressionList(int depth, TokenType open, TokenType closed)
{
#ifdef _DEBUG
  Show("Calling Parameters", depth);
#endif

  ExpressionList* expressions = TreeFactory::Instance()->MakeExpressionList();
  if(!Match(open)) {
    ProcessError(open);
  }
  NextToken();

  while(!Match(closed) && !Match(TOKEN_END_OF_STREAM)) {
    // expression
    expressions->AddExpression(ParseExpression(depth + 1));

    if(Match(TOKEN_COMMA)) {
      NextToken();
    } else if(!Match(closed)) {
      ProcessError("Expected comma or closed parenthesis", closed);
      NextToken();
    }
  }

  if(!Match(closed)) {
    ProcessError(closed);
  }
  NextToken();

  return expressions;
}

/****************************
 * Parses array indices.
 ****************************/
ExpressionList* Parser::ParseIndices(int depth)
{
  ExpressionList* expressions = NULL;
  if(Match(TOKEN_OPEN_BRACKET)) {
    expressions = TreeFactory::Instance()->MakeExpressionList();
    NextToken();

    while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
      // expression
      expressions->AddExpression(ParseExpression(depth + 1));

      if(Match(TOKEN_COMMA)) {
        NextToken();
      } else if(!Match(TOKEN_CLOSED_BRACKET)) {
        ProcessError("Expected comma or semi-colon", TOKEN_SEMI_COLON);
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
  Show("Expression", depth);
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
  Show("Boolean logic", depth);
#endif

  Expression* left = ParseMathLogic(depth + 1);

  CalculatedExpression* expression = NULL;
  while((Match(TOKEN_AND) || Match(TOKEN_OR)) && !Match(TOKEN_END_OF_STREAM)) {
    if(expression) {
      left = expression;
    }

    switch(GetToken()) {
    case TOKEN_AND:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, AND_EXPR);
      break;
    case TOKEN_OR:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, OR_EXPR);
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
  Show("Boolean math", depth);
#endif

  Expression* left = ParseTerm(depth + 1);

  if(Match(TOKEN_LES) || Match(TOKEN_GTR) ||
      Match(TOKEN_LEQL) || Match(TOKEN_GEQL) ||
      Match(TOKEN_EQL) || Match(TOKEN_NEQL)) {
    CalculatedExpression* expression = NULL;
    switch(GetToken()) {
    case TOKEN_LES:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, LES_EXPR);
      break;
    case TOKEN_GTR:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, GTR_EXPR);
      break;
    case TOKEN_LEQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, LES_EQL_EXPR);
      break;
    case TOKEN_GEQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, GTR_EQL_EXPR);
      break;
    case TOKEN_EQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, EQL_EXPR);
      break;
    case TOKEN_NEQL:
      expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, NEQL_EXPR);
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
  Show("Term", depth);
#endif

  Expression* left = ParseFactor(depth + 1);
  if(!left) {
    return NULL;
  }

  if(!Match(TOKEN_ADD) && !Match(TOKEN_SUB)) {
    return left;
  }

  CalculatedExpression* expression = NULL;
  while((Match(TOKEN_ADD) || Match(TOKEN_SUB)) && !Match(TOKEN_END_OF_STREAM)) {
    if(expression) {
      CalculatedExpression* right;
      if(Match(TOKEN_ADD)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, ADD_EXPR);
      } else {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, ADD_EXPR);
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
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, ADD_EXPR);
      } else {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, SUB_EXPR);
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
  Show("Factor", depth);
#endif

  Expression* left = ParseSimpleExpression(depth + 1);
  if(!Match(TOKEN_MUL) && !Match(TOKEN_DIV) && !Match(TOKEN_MOD)) {
    return left;
  }

  CalculatedExpression* expression = NULL;
  while((Match(TOKEN_MUL) || Match(TOKEN_DIV) || Match(TOKEN_MOD)) &&
        !Match(TOKEN_END_OF_STREAM)) {
    if(expression) {
      CalculatedExpression* right;
      if(Match(TOKEN_MUL)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, MUL_EXPR);
      } else if(Match(TOKEN_MOD)) {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, MOD_EXPR);
      } else {
        right = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, DIV_EXPR);
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
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, MUL_EXPR);
      } else if(Match(TOKEN_MOD)) {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, MOD_EXPR);
      } else {
        expression = TreeFactory::Instance()->MakeCalculatedExpression(file_name, line_num, DIV_EXPR);
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
  Show("Simple expression", depth);
#endif

  Expression* expression = NULL;
  if(Match(TOKEN_IDENT) || IsBasicType(GetToken())) {
    string ident;
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

    default:
      ident = ParseBundleName();
      break;
    }

    switch(GetToken()) {
      // method call
    case TOKEN_ASSESSOR:
    case TOKEN_OPEN_PAREN:
      if(!Match(TOKEN_AS_ID, SECOND_INDEX)) {
        expression = ParseMethodCall(ident, depth + 1);
      } else {
        expression = ParseVariable(ident, depth + 1);
        ParseCast(expression, depth + 1);
      }
      break;

    default:
      // variable
      expression = ParseVariable(ident, depth + 1);
      break;
    }
  } else if(Match(TOKEN_OPEN_PAREN)) {
    NextToken();
    expression = ParseLogic(depth + 1);
    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(TOKEN_CLOSED_PAREN);
    }
    NextToken();
  } else if(Match(TOKEN_SUB)) {
    NextToken();

    switch(GetToken()) {
    case TOKEN_INT_LIT:
      expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num,
                   -scanner->GetToken()->GetIntLit());
      NextToken();
      break;

    case TOKEN_FLOAT_LIT:
      expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num,
                   -scanner->GetToken()->GetFloatLit());
      NextToken();
      break;

    default:
      ProcessError("Expected expression", TOKEN_SEMI_COLON);
      break;
    }
  } else {
    switch(GetToken()) {
    case TOKEN_CHAR_LIT:
      expression = TreeFactory::Instance()->MakeCharacterLiteral(file_name, line_num,
                   scanner->GetToken()->GetCharLit());
      NextToken();
      break;

    case TOKEN_INT_LIT:
      expression = TreeFactory::Instance()->MakeIntegerLiteral(file_name, line_num,
                   scanner->GetToken()->GetIntLit());
      NextToken();
      break;

    case TOKEN_FLOAT_LIT:
      expression = TreeFactory::Instance()->MakeFloatLiteral(file_name, line_num,
                   scanner->GetToken()->GetFloatLit());
      NextToken();
      break;

    case TOKEN_CHAR_STRING_LIT: {
      const string &ident = scanner->GetToken()->GetIdentifier();
      expression = TreeFactory::Instance()->MakeCharacterString(file_name, line_num, ident);
      NextToken();
    }
    break;

    case TOKEN_NIL_ID:
      expression = TreeFactory::Instance()->MakeNilLiteral(file_name, line_num);
      NextToken();
      break;

    case TOKEN_TRUE_ID:
      expression = TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, true);
      NextToken();
      break;

    case TOKEN_FALSE_ID:
      expression = TreeFactory::Instance()->MakeBooleanLiteral(file_name, line_num, false);
      NextToken();
      break;

    default:
      ProcessError("Expected expression", TOKEN_SEMI_COLON);
      break;
    }
  }

  // subsequent method calls
  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX)) {
    if(expression->GetExpressionType() == VAR_EXPR) {
      expression = ParseMethodCall(static_cast<Variable*>(expression), depth + 1);
    } else {
      ParseMethodCall(expression, depth + 1);
    }
  }
  // type cast
  else {
    ParseCast(expression, depth + 1);
  }

  return expression;
}

/****************************
 * Parses an explicit type
 * cast.
 ****************************/
void Parser::ParseCast(Expression* expression, int depth)
{
  if(Match(TOKEN_ASSESSOR)) {
    NextToken();

    if(!Match(TOKEN_AS_ID)) {
      ProcessError(TOKEN_AS_ID);
    }
    NextToken();

    if(!Match(TOKEN_OPEN_PAREN)) {
      ProcessError(TOKEN_OPEN_PAREN);
    }
    NextToken();

    if(expression) {
      expression->SetCastType(ParseType(depth + 1));
    }

    if(!Match(TOKEN_CLOSED_PAREN)) {
      ProcessError(TOKEN_CLOSED_PAREN);
    }
    NextToken();

    // subsequent method calls
    if(Match(TOKEN_ASSESSOR)) {
      ParseMethodCall(expression, depth + 1);
    }
  }
}

/****************************
 * Parses a method call.
 ****************************/
MethodCall* Parser::ParseMethodCall(const string &ident, int depth)
{
#ifdef _DEBUG
  Show("Method call", depth);
#endif

  MethodCall* method_call = NULL;
  if(Match(TOKEN_ASSESSOR)) {
    NextToken();
    // method call
    if(Match(TOKEN_IDENT)) {
      method_call = TreeFactory::Instance()->MakeMethodCall(file_name, line_num, ident, method_ident);
    }    
    else {
      ProcessError("Expected identifier", TOKEN_SEMI_COLON);
    }
  }  
  else {
    ProcessError("Expected identifier", TOKEN_SEMI_COLON);
  }
  
  // subsequent method calls
  if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX)) {
    ParseMethodCall(method_call, depth + 1);
  }
  // type cast
  else {
    ParseCast(method_call, depth + 1);
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
  Show("Method call", depth);
#endif

  NextToken();

  if(!Match(TOKEN_IDENT)) {
    ProcessError(TOKEN_IDENT);
  }
  // identifier
  const string &ident = scanner->GetToken()->GetIdentifier();
  NextToken();

  if(expression) {
    expression->SetMethodCall(ParseMethodCall(ident, depth + 1));
    // subsequent method calls
    if(Match(TOKEN_ASSESSOR) && !Match(TOKEN_AS_ID, SECOND_INDEX)) {
      ParseMethodCall(expression->GetMethodCall(), depth + 1);
    }
    // type cast
    else {
      ParseCast(expression->GetMethodCall(), depth + 1);
    }
  }
}

/****************************
 * Parses a data type identifier.
 ****************************/
Type* Parser::ParseType(int depth)
{
#ifdef _DEBUG
  Show("Data Type", depth);
#endif

  Type* type = NULL;
  switch(GetToken()) {
  case TOKEN_BYTE_ID:
    type = TypeFactory::Instance()->MakeType(BYTE_TYPE);
    NextToken();
    break;

  case TOKEN_INT_ID:
    type = TypeFactory::Instance()->MakeType(INT_TYPE);
    NextToken();
    break;

  case TOKEN_FLOAT_ID:
    type = TypeFactory::Instance()->MakeType(FLOAT_TYPE);
    NextToken();
    break;

  case TOKEN_CHAR_ID:
    type = TypeFactory::Instance()->MakeType(CHAR_TYPE);
    NextToken();
    break;

  case TOKEN_NIL_ID:
    type = TypeFactory::Instance()->MakeType(NIL_TYPE);
    NextToken();
    break;

  case TOKEN_BOOLEAN_ID:
    type = TypeFactory::Instance()->MakeType(BOOLEAN_TYPE);
    NextToken();
    break;

  case TOKEN_IDENT: {
    const string ident = ParseBundleName();
    type = TypeFactory::Instance()->MakeType(CLASS_TYPE, ident);
  }
  break;

  default:
    ProcessError("Expected type", TOKEN_SEMI_COLON);
    break;
  }

  if(type) {
    int dimension = 0;

    if(Match(TOKEN_OPEN_BRACKET)) {
      NextToken();
      dimension++;
      while(!Match(TOKEN_CLOSED_BRACKET) && !Match(TOKEN_END_OF_STREAM)) {
        dimension++;
        if(Match(TOKEN_COMMA)) {
          NextToken();
        } else if(!Match(TOKEN_CLOSED_BRACKET)) {
          ProcessError("Expected comma or semi-colon", TOKEN_SEMI_COLON);
          NextToken();
        }
      }

      if(!Match(TOKEN_CLOSED_BRACKET)) {
        ProcessError(TOKEN_CLOSED_BRACKET);
      }
      NextToken();
    }
    type->SetDimension(dimension);
  }

  return type;
}
