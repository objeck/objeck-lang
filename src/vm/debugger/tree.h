/***************************************************************************
 * Debugger parse tree.
 *
 * Copyright (c) 2008-2010 Randy Hollines
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
  class InstanceReference;
  class ParsedCommand;
  class Enum;
  
  /******************************
   * Type class
   ****************************/
  class Type {
    int dimension;
    string class_name;

    Type(Type* t) {
      dimension = t->dimension;
      class_name = t->class_name;
    }

    Type() {
      dimension = 0;
    }

    Type(const string &n) {
      class_name = n;
      dimension = 0;
    }

    ~Type() {
    }

  public:
    static Type* CharStringType();

    void SetDimension(int d) {
      dimension = d;
    }

    const int GetDimension() {
      return dimension;
    }

    void SetClassName(const string &n) {
      class_name = n;
    }

    const string GetClassName() {
      return class_name;
    }
  };

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
   * EnumItem class
   ****************************/
  class EnumItem : public ParseNode {
    friend class TreeFactory;
    string name;
    int id;
    Enum* eenum;

  EnumItem(const string &n, Enum* e) :
    ParseNode() {
      name = n;
      id = -1;
      eenum = e;
    }

    ~EnumItem() {
    }

  public:
    const string& GetName() const {
      return name;
    }

    void SetId(int i) {
      id = i;
    }

    Enum* GetEnum() {
      return eenum;
    }

    int GetId() {
      return id;
    }
  };

  /****************************
   * Enum class
   ****************************/
  class Enum : public ParseNode {
    friend class TreeFactory;
    string name;
    int offset;
    int index;
    map<const string, EnumItem*> items;

  Enum(string &n, int o) :
    ParseNode() {
      name = n;
      index = offset = o;
    }

    ~Enum() {
    }

  public:
    void AddItem(EnumItem* e) {
      e->SetId(index++);
      items.insert(pair<const string, EnumItem*>(e->GetName(), e));
    }

    EnumItem* GetItem(const string &i) {
      map<const string, EnumItem*>::iterator result = items.find(i);
      if(result != items.end()) {
	return result->second;
      }

      return NULL;
    }

    const string& GetName() const {
      return name;
    }

    int GetOffset() {
      return offset;
    }

    map<const string, EnumItem*> GetItems() {
      return items;
    }
  };

  /****************************
   * ExpressionType enum
   ****************************/
  typedef enum _ExpressionType {
    METHOD_CALL_EXPR,
    VAR_EXPR,
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
  } ExpressionType;

  /****************************
   * Expression base class
   ****************************/
  class Expression : public ParseNode {
    friend class TreeFactory;
    InstanceReference* method_call;
    
  protected:
    
  Expression() : ParseNode() {
    }

    ~Expression() {
    }

  public:
    void SetInstanceReference(InstanceReference* call) {
      method_call = call;
    }
    
    InstanceReference* GetInstanceReference() {
      return method_call;
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
   * InstanceReference class
   ****************************/
  class InstanceReference : public Expression {
    friend class TreeFactory;
    EnumItem* enum_item;
    string variable_name;
    string method_name;
 

  InstanceReference(const string &v, const string &m) :Expression() {
      variable_name = v;
      method_name = m;
    }

    ~InstanceReference() {
    }

  public:
    const string& GetVariableName() const {
      return variable_name;
    }

    const string& GetMethodName() const {
      return method_name;
    }

    const ExpressionType GetExpressionType() {
      return METHOD_CALL_EXPR;
    }

    void SetEnumItem(EnumItem* i, const string &enum_name) {
      enum_item = i;
    }

    EnumItem* GetEnumItem() {
      return enum_item;
    }
  };

  /****************************
   * TreeFactory class
   ****************************/
  class TreeFactory {
    static TreeFactory* instance;
  
    vector<ParseNode*> nodes;
    vector<Expression*> expressions;
    vector<InstanceReference*> calls;
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
	InstanceReference* tmp = calls.front();
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

    InstanceReference* MakeInstanceReference(const string &v, const string &m) {
      InstanceReference* tmp = new InstanceReference(v, m);
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
