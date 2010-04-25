/***************************************************************************
 * Debugger parse tree.
 *
 * Copyright (c) 2010 Randy Hollines
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

#ifndef __TREE_H__
#define __TREE_H__

#include "../common.h"

using namespace std;

namespace frontend {
  class TreeFactory;
  class Reference;
  class ParsedCommand;
  class ExpressionList;
  
  /****************************
   * ParseNode base class
   ****************************/
  class ParseNode {    
  public:
    ParseNode() {
    }
    
    ~ParseNode() {
    }
  };  

  /****************************
   * ExpressionType enum
   ****************************/
  enum ExpressionType {
    REF_EXPR,
    NIL_LIT_EXPR,
    CHAR_LIT_EXPR,
    INT_LIT_EXPR,
    FLOAT_LIT_EXPR,
    BOOLEAN_LIT_EXPR,
    AND_EXPR,
    OR_EXPR,
    EQL_EXPR,
    NEQL_EXPR,
    LES_EXPR,
    GTR_EQL_EXPR,
    LES_EQL_EXPR,
    GTR_EXPR,
    ADD_EXPR,
    SUB_EXPR,
    MUL_EXPR,
    DIV_EXPR,
    MOD_EXPR,
    CHAR_STR_EXPR,
  };
  
  /****************************
   * Expression base class
   ****************************/
  class Expression : public ParseNode {
    friend class TreeFactory;
    INT_VALUE int_value;
    FLOAT_VALUE float_value;
    
  protected:    
    Expression() : ParseNode() {
      int_value = 0;
      float_value = 0.0;
    }

    ~Expression() {
    }

  public:
    void SetIntValue(INT_VALUE i) {
      int_value = i;
    }

    INT_VALUE GetIntValue() {
      return int_value;
    }

    void SetIntValue(FLOAT_VALUE f) {
      float_value = f;
    }

    INT_VALUE GetFloatValue() {
      return float_value;
    }

    virtual const ExpressionType GetExpressionType() = 0;
  };

  /****************************
   * ExpressionList class
   ****************************/
  class ExpressionList {
    friend class TreeFactory;
    vector<Expression*> expressions;

    ExpressionList() {
    }

    ~ExpressionList() {
    }

  public:
    vector<Expression*> GetExpressions() {
      return expressions;
    }

    void AddExpression(Expression* e) {
      expressions.push_back(e);
    }
  };

  /****************************
   * Command base class
   ****************************/
  class Command : public ParseNode {
    friend class TreeFactory;
    Reference* reference;
    
  protected:    
    Command() : ParseNode() {
    }
    
    ~Command() {
    }
  };
  
  /****************************
   * CharacterString class
   ****************************/
  class CharacterString : public Expression {
    friend class TreeFactory;
    int id;
    string char_string;

  CharacterString(const string &orig) : Expression() {
      int skip = 2;
      for(unsigned int i = 0; i < orig.size(); i++) {
	char c = orig[i];
	if(skip > 1 && c == '\\' && i + 1 < orig.size()) {
	  char cc = orig[i + 1];
	  switch(cc) {
	  case '"':
	    char_string += '\"';
	    skip = 0;
	    break;

	  case '\\':
	    char_string += '\\';
	    skip = 0;
	    break;

	  case 'n':
	    char_string += '\n';
	    skip = 0;
	    break;

	  case 'r':
	    char_string += '\r';
	    skip = 0;
	    break;

	  case 't':
	    char_string += '\t';
	    skip = 0;
	    break;

	  case '0':
	    char_string += '\0';
	    skip = 0;
	    break;

	  default:
	    if(skip > 1) {
	      char_string += c;
	    } else {
	      skip++;
	    }
	    break;
	  }
	}

	if(skip > 1) {
	  char_string += c;
	} else {
	  skip++;
	}
      }
      id = -1;
    }

    ~CharacterString() {
    }

  public:
    const ExpressionType GetExpressionType() {
      return CHAR_STR_EXPR;
    }

    void SetId(int i) {
      id = i;
    }

    int GetId() {
      return id;
    }

    const string& GetString() const {
      return char_string;
    }
  };

  /****************************
   * CalculatedExpression class
   ****************************/
  class CalculatedExpression : public Expression {
    friend class TreeFactory;
    ExpressionType type;
    Expression* left;
    Expression* right;

  CalculatedExpression(ExpressionType t) :
    Expression() {
      left = right = NULL;
      type = t;
    }

    ~CalculatedExpression() {
    }

  public:
    const ExpressionType GetExpressionType() {
      return type;
    }

    void SetLeft(Expression* l) {
      left = l;
    }

    Expression* GetLeft() {
      return left;
    }

    void SetRight(Expression* r) {
      right = r;
    }

    Expression* GetRight() {
      return right;
    }
  };

  /****************************
   * BooleanLiteral class
   ****************************/
  class BooleanLiteral : public Expression {
    friend class TreeFactory;
    bool value;

  BooleanLiteral(bool v) : Expression() {
      value = v;
    }

    ~BooleanLiteral() {
    }

  public:
    const ExpressionType GetExpressionType() {
      return BOOLEAN_LIT_EXPR;
    }

    bool GetValue() {
      return value;
    }
  };

  /****************************
   * NilLiteral class
   ****************************/
  class NilLiteral : public Expression {
    friend class TreeFactory;

  NilLiteral(const string &f, const int l) : Expression() {
    }

    ~NilLiteral() {
    }

  public:
    const ExpressionType GetExpressionType() {
      return NIL_LIT_EXPR;
    }
  };

  /****************************
   * CharacterLiteral class
   ****************************/
  class CharacterLiteral : public Expression {
    friend class TreeFactory;
    CHAR_VALUE value;

  CharacterLiteral(CHAR_VALUE v) : Expression() {
      value = v;
    }

    ~CharacterLiteral() {
    }

  public:
    CHAR_VALUE GetValue() {
      return value;
    }

    const ExpressionType GetExpressionType() {
      return CHAR_LIT_EXPR;
    }
  };

  /****************************
   * IntegerLiteral class
   ****************************/
  class IntegerLiteral : public Expression {
    friend class TreeFactory;
    INT_VALUE value;

  IntegerLiteral(INT_VALUE v) : Expression() {
      value = v;
    }

    ~IntegerLiteral() {
    }

  public:
    INT_VALUE GetValue() {
      return value;
    }

    const ExpressionType GetExpressionType() {
      return INT_LIT_EXPR;
    }
  };

  /****************************
   * FloatLiteral class
   ****************************/
  class FloatLiteral : public Expression {
    friend class TreeFactory;
    FLOAT_VALUE value;

  FloatLiteral(FLOAT_VALUE v) : Expression() {
      value = v;
    }

    ~FloatLiteral() {
    }

  public:
    FLOAT_VALUE GetValue() {
      return value;
    }

    const ExpressionType GetExpressionType() {
      return FLOAT_LIT_EXPR;
    }
  };

  /****************************
   * Reference class
   ****************************/
  class Reference : public Expression {
    friend class TreeFactory;
    string variable_name;
    ExpressionList* indices;
    Reference* reference;
    
    Reference(const string &v) :Expression() {
      variable_name = v;
    }
    
    ~Reference() {
    }

  public:
    const string& GetVariableName() const {
      return variable_name;
    }

    void SetReference(Reference* call) {
      reference = call;
    }
    
    Reference* GetReference() {
      return reference;
    }
    
    void SetIndices(ExpressionList* l) {
      indices = l;
    }
    
    ExpressionList* GetIndices() {
      return indices;
    }
    
    const ExpressionType GetExpressionType() {
      return REF_EXPR;
    }
  };

  /****************************
   * TreeFactory class
   ****************************/
  class TreeFactory {
    static TreeFactory* instance;
  
    vector<ParseNode*> nodes;
    vector<Expression*> expressions;
    vector<Reference*> calls;
    vector<ExpressionList*> expression_lists;
  
    TreeFactory() {
    }

    ~TreeFactory() {
    }

  public:
    static TreeFactory* Instance();

    void Clear() {
      while(!nodes.empty()) {
	ParseNode* tmp = nodes.front();
	nodes.erase(nodes.begin());
	// delete
	delete tmp;
	tmp = NULL;
      }

      while(!expressions.empty()) {
	Expression* tmp = expressions.front();
	expressions.erase(expressions.begin());
	// delete
	delete tmp;
	tmp = NULL;
      }
    
      while(!calls.empty()) {
	Reference* tmp = calls.front();
	calls.erase(calls.begin());
	// delete
	delete tmp;
	tmp = NULL;
      }

      while(!expression_lists.empty()) {
	ExpressionList* tmp = expression_lists.front();
	expression_lists.erase(expression_lists.begin());
	// delete
	delete tmp;
	tmp = NULL;
      }
      
      delete instance;
      instance = NULL;
    }

    ExpressionList* MakeExpressionList() {
      ExpressionList* tmp = new ExpressionList;
      expression_lists.push_back(tmp);
      return tmp;
    }

    CalculatedExpression* MakeCalculatedExpression(ExpressionType type) {
      CalculatedExpression* tmp = new CalculatedExpression(type);
      expressions.push_back(tmp);
      return tmp;
    }

    IntegerLiteral* MakeIntegerLiteral(INT_VALUE value) {
      IntegerLiteral* tmp = new IntegerLiteral(value);
      expressions.push_back(tmp);
      return tmp;
    }

    FloatLiteral* MakeFloatLiteral(FLOAT_VALUE value) {
      FloatLiteral* tmp = new FloatLiteral(value);
      expressions.push_back(tmp);
      return tmp;
    }

    CharacterLiteral* MakeCharacterLiteral(CHAR_VALUE value) {
      CharacterLiteral* tmp = new CharacterLiteral(value);
      expressions.push_back(tmp);
      return tmp;
    }

    CharacterString* MakeCharacterString(const string &char_string) {
      CharacterString* tmp = new CharacterString(char_string);
      expressions.push_back(tmp);
      return tmp;
    }

    NilLiteral* MakeNilLiteral(const string &file_name, const int line_num) {
      NilLiteral* tmp = new NilLiteral(file_name, line_num);
      expressions.push_back(tmp);
      return tmp;
    }

    BooleanLiteral* MakeBooleanLiteral(bool boolean) {
      BooleanLiteral* tmp = new BooleanLiteral(boolean);
      expressions.push_back(tmp);
      return tmp;
    }

    Reference* MakeReference(const string &v) {
      Reference* tmp = new Reference(v);
      calls.push_back(tmp);
      return tmp;
    }
  };

  /****************************
   * ParsedCommand class
   ****************************/
  class ParsedCommand {  
  public:
    ParsedCommand() {
    }
    
    ~ParsedCommand() {
      // clear factories
      TreeFactory::Instance()->Clear();
    }
  };
}

#endif
