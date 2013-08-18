/***************************************************************************
* Language parse tree.
*
* Copyright (c) 2008-2013, Randy Hollines
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

#ifndef __TREE_H__
#define __TREE_H__

#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <set>
#include <string>
#include <list>
#include <vector>
#include <stack>
#include <map>
#include <exception>
#include <stdlib.h>
#include <assert.h>
#include "linker.h"
#include "../shared/instrs.h"
#ifdef _MEMCHECK
#include <mcheck.h>
#endif

#define SELF_ID L"@self"
#define PARENT_ID L"@parent"
#define BOOL_CLASS_ID L"System.$Bool"
#define BYTE_CLASS_ID L"System.$Byte"
#define INT_CLASS_ID L"System.$Int"
#define FLOAT_CLASS_ID L"System.$Float"
#define CHAR_CLASS_ID L"System.$Char"
#define NIL_CLASS_ID L"System.$Nil"
#define VAR_CLASS_ID L"System.$Var"
#define BASE_ARRAY_CLASS_ID L"System.$BaseArray"

using namespace std;

namespace frontend {
  class TreeFactory;
  class Variable;
  class MethodCall;
  class Class;
  class Method;
  class Enum;
  class ParsedProgram;

  /****************************
  * StatementType enum
  ****************************/
  typedef enum _StatementType {
    DECLARATION_STMT,
    ASSIGN_STMT,
    ADD_ASSIGN_STMT,
    SUB_ASSIGN_STMT,
    MUL_ASSIGN_STMT,
    DIV_ASSIGN_STMT,
    METHOD_CALL_STMT,
    SIMPLE_STMT,
    IF_STMT,
    BREAK_STMT,
    DO_WHILE_STMT,
    WHILE_STMT,
    FOR_STMT,
    SELECT_STMT,
    ENUM_STMT,
    RETURN_STMT,
    CRITICAL_STMT,
    SYSTEM_STMT,
    EMPTY_STMT
  } StatementType;

  /****************************
  * SymbolEntry class
  ****************************/
  class SymbolEntry : public ParseNode {
    friend class TreeFactory;
    vector<Variable*> variables;
    int id;
    std::wstring name;
    Type* type;
    bool is_static;
    bool is_local;
    bool is_self;

    SymbolEntry(const std::wstring &f, int l, const std::wstring &n, Type* t, bool s, bool c, bool e = false) :
      ParseNode(f, l) {
        name = n;
        id = -1;
        type = t;
        is_static = s;
        is_local = c;
        is_self = e;
    }

    ~SymbolEntry() {
    }

  public:

    void SetType(Type* t) {
      type =  t;
    }

    Type* GetType() {
      return type;
    }

    bool IsStatic() {
      return is_static;
    }

    bool IsLocal() {
      return is_local;
    }

    const std::wstring& GetName() const {
      return name;
    }

    int GetId() {
      return id;
    }

    bool IsSelf() {
      return is_self;
    }

    void AddVariable(Variable* v) {
      variables.push_back(v);
    }

    void SetId(int i);
    SymbolEntry* Copy();
  };

  /****************************
  * ScopeTable class
  ****************************/
  class ScopeTable {
    map<const std::wstring, SymbolEntry*> entries;
    ScopeTable* parent;
    vector<ScopeTable*> children;
    int child_pos;

  public:
    ScopeTable(ScopeTable* p) {
      parent = p;
      child_pos = 0;
    }

    ~ScopeTable() {
      // clean up
      while(!children.empty()) {
        ScopeTable* tmp = children.front();
        children.erase(children.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }
    }

    vector<SymbolEntry*> GetEntries() {
      vector<SymbolEntry*> entries_list;
      map<const std::wstring, SymbolEntry*>::iterator iter;
      for(iter = entries.begin(); iter != entries.end(); ++iter) {
        SymbolEntry* entry = iter->second;
        entries_list.push_back(entry);
      }

      return entries_list;
    }

    SymbolEntry* GetEntry(const std::wstring &name) {
      map<const std::wstring, SymbolEntry*>::iterator result = entries.find(name);
      if(result != entries.end()) {
        return result->second;
      }

      return NULL;
    }

    bool AddEntry(SymbolEntry* e) {
      if(GetEntry(e->GetName())) {
        return false;
      }
      // add
      entries.insert(pair<std::wstring, SymbolEntry*>(e->GetName(), e));
      return true;
    }

    ScopeTable* GetParent() {
      return parent;
    }

    ScopeTable* GetNextChild() {
      if(child_pos < (int)children.size()) {
        return children[child_pos++];
      }

      return NULL;
    }

    void AddChild(ScopeTable* c) {
      children.push_back(c);
    }
  };

  /****************************
  * SymbolTable class
  ****************************/
  class SymbolTable {
    ScopeTable *head, *parse_ptr, *iter_ptr;
    vector<SymbolEntry*> entries;

  public:
    SymbolTable() {
      head = parse_ptr = iter_ptr = new ScopeTable(NULL);
    }

    ~SymbolTable() {
      delete head;
      head = NULL;
    }

    vector<SymbolEntry*> GetEntries() {
      return entries;
    }

    SymbolEntry* GetEntry(const std::wstring &name) {
      ScopeTable* tmp = iter_ptr;
      while(tmp) {
        SymbolEntry* entry = tmp->GetEntry(name);
        if(entry) {
          return entry;
        }
        tmp = tmp->GetParent();
      }

      return NULL;
    }

    bool AddEntry(SymbolEntry* e, bool is_var = false) {
      // see of we have this entry
      ScopeTable* tmp;
      if(is_var) {
        tmp = iter_ptr;
      }
      else {
        tmp = parse_ptr;
      }

      while(tmp) {
        SymbolEntry* entry = tmp->GetEntry(e->GetName());
        if(entry) {
          return false;
        }
        tmp = tmp->GetParent();
      }

      // add new entry
      if(is_var) {
        iter_ptr->AddEntry(e);
      }
      else {
        parse_ptr->AddEntry(e);
      }
      entries.push_back(e);
      return true;
    }

    void NewParseScope() {
      ScopeTable* tmp = parse_ptr;
      parse_ptr = new ScopeTable(tmp);
      tmp->AddChild(parse_ptr);
    }

    void PreviousParseScope() {
      if(parse_ptr) {
        parse_ptr = parse_ptr->GetParent();
      }
    }

    void NewScope() {
      if(iter_ptr) {
        iter_ptr = iter_ptr->GetNextChild();
      }
    }

    void PreviousScope() {
      if(iter_ptr) {
        iter_ptr = iter_ptr->GetParent();
      }
    }
  };

  /****************************
  * SymbolTableManager class
  ****************************/
  class SymbolTableManager {
    stack<SymbolTable*> scope;
    map<const std::wstring, SymbolTable*> tables;

  public:
    SymbolTableManager() {
    }

    ~SymbolTableManager() {
      // clean up
      map<const std::wstring, SymbolTable*>::iterator iter;
      for(iter = tables.begin(); iter != tables.end(); ++iter) {
        SymbolTable* tmp = iter->second;
        delete tmp;
        tmp = NULL;
      }
      tables.clear();
    }

    void NewParseScope() {
      scope.push(new SymbolTable);
    }

    void PreviousParseScope(const std::wstring &namescope) {
      if(GetSymbolTable(namescope)) {
        return;
      }

      tables.insert(pair<std::wstring, SymbolTable*>(namescope, scope.top()));
      scope.pop();
    }

    SymbolTable* CurrentParseScope() {
      return scope.top();
    }

    vector<SymbolEntry*> GetEntries(const std::wstring &namescope) {
      vector<SymbolEntry*> entries;
      map<const std::wstring, SymbolTable*>::iterator result = tables.find(namescope);
      if(result != tables.end()) {
        entries = result->second->GetEntries();
      }

      return entries;
    }

    SymbolTable* GetSymbolTable(const std::wstring &namescope) {
      map<const std::wstring, SymbolTable*>::iterator result = tables.find(namescope);
      if(result != tables.end()) {
        return result->second;
      }

      return NULL;
    }
  };

  /****************************
  * Statement base class
  ****************************/
  class Statement : public ParseNode {
  public:
    Statement(const std::wstring &f, const int l) : ParseNode(f, l) {
    }

    virtual ~Statement() {
    }

    virtual const StatementType GetStatementType() = 0;
  };

  /****************************
  * StatementList class
  ****************************/
  class StatementList {
    friend class TreeFactory;
    vector<Statement*> statements;

    StatementList() {
    }

    ~StatementList() {
    }

  public:
    vector<Statement*> GetStatements() {
      return statements;
    }

    void AddStatement(Statement* s) {
      statements.push_back(s);
    }
  };

  /****************************
  * ExpressionType enum
  ****************************/
  typedef enum _ExpressionType {
    METHOD_CALL_EXPR,
    COND_EXPR,
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
    SHL_EXPR,
    SHR_EXPR,
    BIT_AND_EXPR,
    BIT_OR_EXPR,
    BIT_XOR_EXPR,
    CHAR_STR_EXPR,
    STAT_ARY_EXPR
  } ExpressionType;

  /****************************
  * Expression base class
  ****************************/
  class Expression : public ParseNode {
    friend class TreeFactory;

  protected:
    Type* base_type;
    Type* eval_type;
    Type* cast_type;
    Type* type_of;
    MethodCall* method_call;
    Expression* prev_expr;
    Class* to_class;
    LibraryClass* to_lib_class;

    Expression(const std::wstring &f, const int l) : ParseNode(f, l) {
      base_type = eval_type = cast_type = type_of = NULL;
      method_call = NULL;
      prev_expr = NULL;
      to_class = NULL;
      to_lib_class = NULL;
    }

    Expression(const std::wstring &f, const int l, Type* t) : ParseNode(f, l) {
      base_type = eval_type = TypeFactory::Instance()->MakeType(t);
      cast_type = NULL;
      method_call = NULL;
      prev_expr = NULL;
      to_class = NULL;
      to_lib_class = NULL;
      type_of = NULL;
    }

    virtual ~Expression() {
    }

  public:
    void SetToClass(Class* t) {
      to_class = t;
    }

    Class* GetToClass() {
      return to_class;
    }

    void SetToLibraryClass(LibraryClass* t) {
      to_lib_class = t;
    }

    LibraryClass* GetToLibraryClass() {
      return to_lib_class;
    }

    void SetMethodCall(MethodCall* call);

    void SetPreviousExpression(Expression* e) {
      prev_expr = e;
    }

    MethodCall* GetMethodCall() {
      return method_call;
    }

    Expression* GetPreviousExpression() {
      return prev_expr;
    }

    void SetTypes(Type* t) {
      if(t) {
        base_type = eval_type = TypeFactory::Instance()->MakeType(t);
      }
    }

    // used for target emission
    Type* GetBaseType() {
      return base_type;
    }

    // used for contextual casting
    Type* GetEvalType() {
      return eval_type;
    }

    void SetEvalType(Type* e, bool zd) {
      eval_type = TypeFactory::Instance()->MakeType(e);

      if(!base_type) {
        base_type = eval_type;
      }

      if(zd) {
        eval_type->SetDimension(0);
      }
    }

    void SetCastType(Type* c) {
      cast_type = TypeFactory::Instance()->MakeType(c);
    }

    Type* GetCastType() {
      return cast_type;
    }

    void SetTypeOf(Type* c) {
      type_of = TypeFactory::Instance()->MakeType(c);
    }

    Type* GetTypeOf() {
      return type_of;
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
  * StaticArray class
  ****************************/
  class StaticArray : public Expression {
    friend class TreeFactory;
    int id;
    int dim;
    int cur_width;
    int cur_height;
    ExpressionList* elements;
    ExpressionList* all_elements;
    vector<int> sizes;
    bool matching_types;
    ExpressionType cur_type;
    bool matching_lengths;

    void GetAllElements(StaticArray* array, ExpressionList* elems) {
      vector<Expression*> static_array = array->GetElements()->GetExpressions();
      for(size_t i = 0; i < static_array.size(); ++i) { 
        if(static_array[i]) {
          if(static_array[i]->GetExpressionType() == STAT_ARY_EXPR) {
            GetAllElements(static_cast<StaticArray*>(static_array[i]), all_elements);
            cur_height++;
          }
          else {
            elems->AddExpression(static_array[i]);
          }
        }
      } 
    }

    void GetSizes(StaticArray* array, int &count) {
      vector<Expression*> static_array = array->GetElements()->GetExpressions();
      for(size_t i = 0; i < static_array.size(); ++i) { 
        if(static_array[i]) {
          if(static_array[i]->GetExpressionType() == STAT_ARY_EXPR) {
            count++;
            GetSizes(static_cast<StaticArray*>(static_array[i]), count);
          }
        }
      } 
    }

  public:
    StaticArray(const std::wstring &f, int l, ExpressionList* e) : Expression(f, l) {
      elements = e;
      all_elements = NULL;
      matching_types = matching_lengths = true;
      cur_type = VAR_EXPR;
      cur_width = id = -1;
      cur_height = 0;
      dim = 1;

      Validate(this);
    }

    void Validate(StaticArray* array);

    ~StaticArray() {
    }

    const ExpressionType GetExpressionType() {
      return STAT_ARY_EXPR;
    }

    void SetId(int i) {
      id = i;
    }

    int GetId() {
      return id;
    }

    int GetDimension() {
      return dim;
    }

    EntryType GetType() {
      switch(cur_type) {      
      case INT_LIT_EXPR:
        return INT_TYPE;

      case FLOAT_LIT_EXPR:
        return FLOAT_TYPE;

      case CHAR_LIT_EXPR:
        return CHAR_TYPE;

      case CHAR_STR_EXPR:
        return CLASS_TYPE;

      default:
        break;
      }

      return VAR_TYPE;
    }

    ExpressionList* GetElements() {
      return elements;
    }

    ExpressionList* GetAllElements();

    vector<int> GetSizes();

    bool IsMatchingTypes() {
      return matching_types;
    }

    bool IsMatchingLenghts() {
      return matching_lengths;
    }
  };

  /****************************
  * CharacterString class
  ****************************/
  enum CharacterStringSegmentType {
    STRING,
    ENTRY
  };

  class CharacterStringSegment {
    CharacterStringSegmentType type;
    int id;
    std::wstring str;
    SymbolEntry* entry;
    Method* method;
    LibraryMethod* lib_method;

  public:
    CharacterStringSegment(const std::wstring &s) {
      type = STRING;
      str = s;
      entry = NULL;
      method = NULL;
      lib_method = NULL;
      id = -1;
    }

    CharacterStringSegment(SymbolEntry* e) {
      type = ENTRY;
      entry = e;
      method = NULL;
      lib_method = NULL;
      id = -1;
    }

    CharacterStringSegment(SymbolEntry* e, Method* m) {
      type = ENTRY;
      entry = e;
      method = m;
      lib_method = NULL;
      id = -1;
    }

    CharacterStringSegment(SymbolEntry* e, LibraryMethod* m) {
      type = ENTRY;
      entry = e;
      method = NULL;
      lib_method = m;
      id = -1;
    }

    CharacterStringSegmentType GetType() {
      return type;
    }

    ~CharacterStringSegment() {
    }

    const std::wstring GetString() {
      return str;
    }

    SymbolEntry* GetEntry() {
      return entry;
    }

    void SetId(int i) {
      id = i;
    }

    int GetId() {
      return id;
    }

    Method* GetMethod() {
      return method;
    }

    LibraryMethod* GetLibraryMethod() {
      return lib_method;
    }
  };

  class CharacterString : public Expression {
    friend class TreeFactory;
    bool is_processed;
    std::wstring char_string;
    vector<CharacterStringSegment*> segments;
    SymbolEntry* concat;

    CharacterString(const std::wstring &f, int l, const std::wstring &c) :
      Expression(f, l, Type::CharStringType()) {
        char_string = c;
        is_processed = false;
        concat = NULL;
    }

    ~CharacterString() {      
      while(!segments.empty()) {
        CharacterStringSegment* tmp = segments.front();
        segments.erase(segments.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }
    }

  public:
    void SetProcessed() {
      is_processed = true;
    }

    const ExpressionType GetExpressionType() {
      return CHAR_STR_EXPR;
    }

    void SetConcat(SymbolEntry* c) {
      concat = c;
    }

    SymbolEntry* GetConcat() {
      return concat;
    }

    const std::wstring& GetString() const {
      return char_string;
    }

    void AddSegment(const std::wstring &orig) {
      if(!is_processed) {
        std::wstring escaped_str;	
        int skip = 2;
        for(size_t i = 0; i < orig.size(); ++i) {
          wchar_t c = orig[i];
          if(skip > 1 && c == L'\\' && i + 1 < orig.size()) {
            wchar_t cc = orig[i + 1];
            switch(cc) {
            case L'"':
              escaped_str += L'\"';
              skip = 0;
              break;

            case L'\\':
              escaped_str += L'\\';
              skip = 0;
              break;

            case L'n':
              escaped_str += L'\n';
              skip = 0;
              break;

            case L'r':
              escaped_str += L'\r';
              skip = 0;
              break;

            case L't':
              escaped_str += L'\t';
              skip = 0;
              break;

            case L'a':
              escaped_str += L'\a';
              skip = 0;
              break;

            case L'b':
              escaped_str += L'\b';
              skip = 0;
              break;

#ifndef _WIN32
            case L'e':
              escaped_str += L'\e';
              skip = 0;
              break;
#endif

            case L'f':
              escaped_str += L'\f';
              skip = 0;
              break;

            case L'0':
              escaped_str += L'\0';
              skip = 0;
              break;

            default:
              if(skip <= 1) {
                skip++;
              }
              break;
            }
          }

          if(skip > 1) {
            escaped_str += c;
          } else {
            skip++;
          }
        }
        // set string
        segments.push_back(new CharacterStringSegment(escaped_str));
      }
    }

    void AddSegment(SymbolEntry* e) {
      segments.push_back(new CharacterStringSegment(e)); 
    }

    void AddSegment(SymbolEntry* e, Method* m) {
      segments.push_back(new CharacterStringSegment(e, m)); 
    }

    void AddSegment(SymbolEntry* e, LibraryMethod* m) {
      segments.push_back(new CharacterStringSegment(e, m)); 
    }

    vector<CharacterStringSegment*> GetSegments() {
      return segments;
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

    CalculatedExpression(const std::wstring &f, int l, ExpressionType t) :
      Expression(f, l) {
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
  * Variable class
  ****************************/
  class Variable : public Expression {
    friend class TreeFactory;
    int id;
    std::wstring name;
    ExpressionList* indices;
    SymbolEntry* entry;

    Variable(const std::wstring &f, int l, const std::wstring &n) : Expression(f, l) {
      name = n;
      indices = NULL;
      entry = NULL;
      id = -1;
    }

    ~Variable() {
    }

  public:
    const std::wstring& GetName() const {
      return name;
    }

    void SetId(int i) {
      id = i;
    }

    const int GetId() {
      return id;
    }

    void SetEntry(SymbolEntry* e) {
      entry = e;
    }

    SymbolEntry* GetEntry() {
      return entry;
    }

    const ExpressionType GetExpressionType() {
      return VAR_EXPR;
    }

    void SetIndices(ExpressionList* i) {
      indices = i;
    }

    ExpressionList* GetIndices() {
      return indices;
    }

    Variable* Copy();
  };

  /****************************
  * BooleanLiteral class
  ****************************/
  class BooleanLiteral : public Expression {
    friend class TreeFactory;
    bool value;

    BooleanLiteral(const std::wstring &f, const int l, bool v) :
      Expression(f, l, TypeFactory::Instance()->MakeType(BOOLEAN_TYPE)) {
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

    NilLiteral(const std::wstring &f, const int l) : Expression(f, l, TypeFactory::Instance()->MakeType(NIL_TYPE)) {
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
    wchar_t value;

    CharacterLiteral(const std::wstring &f, const int l, wchar_t v) :
      Expression(f, l, TypeFactory::Instance()->MakeType(CHAR_TYPE)) {
        value = v;
    }

    ~CharacterLiteral() {
    }

  public:
    wchar_t GetValue() {
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

    IntegerLiteral(const std::wstring &f, const int l, INT_VALUE v) :
      Expression(f, l, TypeFactory::Instance()->MakeType(INT_TYPE)) {
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

    FloatLiteral(const std::wstring &f, const int l, FLOAT_VALUE v) :
      Expression(f, l, TypeFactory::Instance()->MakeType(FLOAT_TYPE)) {
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
  * Cond class
  ****************************/
  class Cond : public Expression {
    friend class TreeFactory;
    Expression* expression;
    Expression* if_expression;
    Expression* else_expression;
    Cond* next;

    Cond(const std::wstring &f, const int l, Expression* c, Expression* s, Expression* e) : Expression(f, l) {
      expression = c;
      if_expression = s;
      else_expression = e;
    }

    ~Cond() {
    }

  public:
    const ExpressionType GetExpressionType() {
      return COND_EXPR;
    }

    Expression* GetExpression() {
      return if_expression;
    }

    Expression* GetCondExpression() {
      return expression;
    }

    void SetElseExpression(Expression* e) {
      else_expression = e;
    }

    Expression* GetElseExpression() {
      return else_expression;
    }
  };

  /****************************
  * Return class
  ****************************/
  class Return : public Statement {
    friend class TreeFactory;
    Expression* expression;

    Return(const std::wstring &f, const int l, Expression* e) : Statement(f, l) {
      expression = e;
    }

    ~Return() {
    }

  public:
    const StatementType GetStatementType() {
      return RETURN_STMT;
    }

    Expression* GetExpression() {
      return expression;
    }
  };

  /****************************
  * Break class
  ****************************/
  class Break : public Statement {
    friend class TreeFactory;

    Break(const std::wstring &f, const int l) : Statement(f, l) {
    }

    ~Break() {
    }

  public:
    const StatementType GetStatementType() {
      return BREAK_STMT;
    }
  };

  /****************************
  * If class
  ****************************/
  class If : public Statement {
    friend class TreeFactory;
    Expression* expression;
    StatementList* if_statements;
    StatementList* else_statements;
    If* next;

    If(const std::wstring &f, const int l, Expression* e, StatementList* s, If* n = NULL) :
      Statement(f, l) {
        expression = e;
        if_statements = s;
        next = n;
        else_statements = NULL;
    }

    ~If() {
    }

  public:
    const StatementType GetStatementType() {
      return IF_STMT;
    }

    Expression* GetExpression() {
      return expression;
    }

    StatementList* GetIfStatements() {
      return if_statements;
    }

    void SetElseStatements(StatementList* e) {
      else_statements = e;
    }

    StatementList* GetElseStatements() {
      return else_statements;
    }

    If* GetNext() {
      return next;
    }
  };

  /****************************
  * EnumItem class
  ****************************/
  class EnumItem : public ParseNode {
    friend class TreeFactory;
    std::wstring name;
    int id;
    Enum* eenum;

    EnumItem(const std::wstring &f, const int l, const std::wstring &n, Enum* e) :
      ParseNode(f, l) {
        name = n;
        id = -1;
        eenum = e;
    }

    ~EnumItem() {
    }

  public:
    const std::wstring& GetName() const {
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
    std::wstring name;
    int offset;
    int index;
    map<const std::wstring, EnumItem*> items;

    Enum(const std::wstring &f, const int l, std::wstring &n, int o) :
      ParseNode(f, l) {
        name = n;
        index = offset = o;
    }

    ~Enum() {
    }

  public:
    void AddItem(EnumItem* e) {
      e->SetId(index++);
      items.insert(pair<const std::wstring, EnumItem*>(e->GetName(), e));
    }

    EnumItem* GetItem(const std::wstring &i) {
      map<const std::wstring, EnumItem*>::iterator result = items.find(i);
      if(result != items.end()) {
        return result->second;
      }

      return NULL;
    }

    const std::wstring& GetName() const {
      return name;
    }

    int GetOffset() {
      return offset;
    }

    map<const std::wstring, EnumItem*> GetItems() {
      return items;
    }
  };

  /****************************
  * Select class
  ****************************/
  class Select : public Statement {
    friend class TreeFactory;
    Expression* eval_expression;
    map<int, StatementList*> label_statements;
    vector<StatementList*> statement_lists;
    map<ExpressionList*, StatementList*> statement_map;
    StatementList* other;

    Select(const std::wstring &f, const int l, Expression* e, 
      map<ExpressionList*, StatementList*> s, 
      vector<StatementList*> sl, StatementList* o) :
    Statement(f, l) {
      eval_expression = e;
      statement_map = s;
      statement_lists = sl;
      other = o;
    }

    ~Select() {
    }

  public:
    void SetLabelStatements(map<int, StatementList*> s) {
      label_statements = s;
    }

    map<int, StatementList*> GetLabelStatements() {
      return label_statements;
    }

    const StatementType GetStatementType() {
      return SELECT_STMT;
    }

    Expression* GetExpression() {
      return eval_expression;
    }

    map<ExpressionList*, StatementList*> GetStatements() {
      return statement_map;
    }

    vector<StatementList*> GetStatementLists() {
      return statement_lists;
    }

    StatementList* GetOther() {
      return other;
    }
  };

  /****************************
  * CriticalSection class
  ****************************/
  class CriticalSection : public Statement {
    Variable* variable;
    StatementList* statements;

  public:
    CriticalSection(const std::wstring &f, const int l, Variable* v, StatementList* s) : Statement(f, l) {
      variable = v;
      statements = s;
    }

    ~CriticalSection() {
    }

    Variable* GetVariable() {
      return variable;
    }

    StatementList* GetStatements() {
      return statements;
    }

    const StatementType GetStatementType() {
      return CRITICAL_STMT;
    }
  };

  /****************************
  * For class
  ****************************/
  class For : public Statement {
    friend class TreeFactory;
    Statement* pre_stmt;
    Expression* cond_expr;
    Statement* update_stmt;
    StatementList* statements;

    For(const std::wstring &f, const int l, Statement* pre, Expression* cond,
      Statement* update, StatementList* stmts) : Statement(f, l) {
        pre_stmt = pre;
        cond_expr = cond;
        update_stmt = update;
        statements = stmts;
    }

    ~For() {
    }

  public:
    const StatementType GetStatementType() {
      return FOR_STMT;
    }

    Statement* GetPreStatement() {
      return pre_stmt;
    }

    Expression* GetExpression() {
      return cond_expr;
    }

    Statement* GetUpdateStatement() {
      return update_stmt;
    }

    StatementList* GetStatements() {
      return statements;
    }
  };

  /****************************
  * DoWhile class
  ****************************/
  class DoWhile : public Statement {
    friend class TreeFactory;
    Expression* expression;
    StatementList* statements;

    DoWhile(const std::wstring &f, const int l, Expression* e, StatementList* s) : Statement(f, l) {
      expression = e;
      statements = s;
    }

    ~DoWhile() {
    }

  public:
    const StatementType GetStatementType() {
      return DO_WHILE_STMT;
    }

    Expression* GetExpression() {
      return expression;
    }

    StatementList* GetStatements() {
      return statements;
    }
  };

  /****************************
  * While class
  ****************************/
  class While : public Statement {
    friend class TreeFactory;
    Expression* expression;
    StatementList* statements;

    While(const std::wstring &f, const int l, Expression* e, StatementList* s) : Statement(f, l) {
      expression = e;
      statements = s;
    }

    ~While() {
    }

  public:
    const StatementType GetStatementType() {
      return WHILE_STMT;
    }

    Expression* GetExpression() {
      return expression;
    }

    StatementList* GetStatements() {
      return statements;
    }
  };

  /****************************
  * SystemStatement class
  ****************************/
  class SystemStatement : public Statement {
    friend class TreeFactory;
    int id;

    SystemStatement(const std::wstring &f, const int l, int i) : Statement(f, l) {
      id = i;
    }

    ~SystemStatement() {
    }

  public:
    const StatementType GetStatementType() {
      return SYSTEM_STMT;
    }

    int GetId() {
      return id;
    }
  };

  /****************************
  * SimpleStatement class
  ****************************/
  class SimpleStatement : public Statement {
    friend class TreeFactory;
    Expression* expression;

    SimpleStatement(const std::wstring &f, const int l, Expression* e) :
      Statement(f, l) {
        expression = e;
    }

    ~SimpleStatement() {
    }

  public:
    const StatementType GetStatementType() {
      return SIMPLE_STMT;
    }

    Expression* GetExpression() {
      return expression;
    }
  };

  /****************************
   * EmptyStatement class
   ****************************/
  class EmptyStatement : public Statement {
    friend class TreeFactory;

  public:
    EmptyStatement(const std::wstring &f, const int l) : Statement(f, l) {
    }
    
    ~EmptyStatement() {
    }
      
    const StatementType GetStatementType() {
      return EMPTY_STMT;
    }
  };

  /****************************
  * Assignment class
  ****************************/
  class Assignment : public Statement {
  protected:
    friend class TreeFactory;
    Variable* variable;
    Expression* expression;

    Assignment(const std::wstring &f, const int l, Variable* v, Expression* e) :
      Statement(f, l) {
        variable = v;
        expression = e;
    }

    virtual ~Assignment() {
    }

  public:
    Variable* GetVariable() {
      return variable;
    }

    Expression* GetExpression() {
      return expression;
    }

    virtual const StatementType GetStatementType() {
      return ASSIGN_STMT;
    }
  };

  /****************************
  * Assignment class
  ****************************/
  class OperationAssignment : public Assignment {
    friend class TreeFactory;
    StatementType stmt_type;

    OperationAssignment(const std::wstring &f, const int l, Variable* v, 
      Expression* e, StatementType t) : 
    Assignment(f, l, v, e) {
      stmt_type = t;
    }

    ~OperationAssignment() {
    }

  public:
    const StatementType GetStatementType() {
      return stmt_type;
    }
  };

  /****************************
  * Declaration class
  ****************************/
  class Declaration : public Statement {
    friend class TreeFactory;
    SymbolEntry* entry;
    Assignment* assignment;

    Declaration(const std::wstring &f, const int l, SymbolEntry* e, Assignment* a) :
      Statement(f, l) {
        entry = e;
        assignment = a;
    }

    Declaration(const std::wstring &f, const int l, SymbolEntry* e) :
      Statement(f, l) {
        entry = e;
        assignment = NULL;
    }

    ~Declaration() {
    }


  public:
    SymbolEntry* GetEntry() {
      return entry;
    }

    Assignment* GetAssignment() {
      return assignment;
    }

    Declaration* Copy();

    const StatementType GetStatementType() {
      return DECLARATION_STMT;
    }
  };

  /****************************
  * DeclarationList class
  ****************************/
  class DeclarationList {
    friend class TreeFactory;
    vector<Declaration*> declarations;

    DeclarationList() {
    }

    ~DeclarationList() {
    }

  public:
    vector<Declaration*> GetDeclarations() {
      return declarations;
    }

    void AddDeclaration(Declaration* t) {
      declarations.push_back(t);
    }
  };

  /****************************
  * Method class
  ****************************/
  class Method : public ParseNode {
    friend class TreeFactory;
    int id;
    std::wstring name;
    std::wstring parsed_name;
    std::wstring encoded_name;

    std::wstring encoded_return;
    std::wstring parsed_return;

    StatementList* statements;
    DeclarationList* declarations;
    Type* return_type;
    MethodType method_type;
    bool is_static;
    bool is_native;
    bool has_and_or;
    SymbolTable* symbol_table;
    Class* klass;

    Method(const std::wstring &f, const int l, const std::wstring &n, MethodType m, bool s, bool c) :
      ParseNode(f, l) {
        name = n;
        method_type = m;
        is_static = s;
        is_native = c;
        statements = NULL;
        return_type = NULL;
        declarations = NULL;
        id = -1;
        has_and_or = false;
    }

    ~Method() {
    }

    std::wstring EncodeType(Type* type, ParsedProgram* program, Linker* linker);

    /****************************
    * Encodes a function type
    ****************************/
    std::wstring EncodeFunctionType(vector<Type*> func_params, Type* func_rtrn,
      ParsedProgram* program, Linker* linker) {  
        std::wstring encoded_name = L"(";
        for(size_t i = 0; i < func_params.size(); ++i) {
          // encode params
          encoded_name += EncodeType(func_params[i], program, linker);

          // encode dimension   
          for(int j = 0; j < func_params[i]->GetDimension(); j++) {
            encoded_name += L'*';
          }    
          encoded_name += L',';
        }

        // encode return
        encoded_name += L")~";
        encoded_name += EncodeType(func_rtrn, program, linker);

        return encoded_name;
    }

    std::wstring EncodeType(Type* type) {
      std::wstring name;
      if(type) {
        // type
        switch(type->GetType()) {
        case BOOLEAN_TYPE:
          name = L'l';
          break;

        case BYTE_TYPE:
          name = L'b';
          break;

        case INT_TYPE:
          name = L'i';
          break;

        case FLOAT_TYPE:
          name = L'f';
          break;

        case CHAR_TYPE:
          name = L'c';
          break;

        case NIL_TYPE:
          name = L'n';
          break;

        case VAR_TYPE:
          name = L'v';
          break;

        case CLASS_TYPE:
          name = L"o.";
          name += type->GetClassName();
          break;

        case FUNC_TYPE:
          name = L'm';
          break;
        }

        // dimension
        for(int i = 0; i < type->GetDimension(); i++) {
          name += L'*';
        }
      }

      return name;
    }

  public:
    void SetId(int i) {
      id = i;
    }

    int GetId() {
      return id;
    }

    bool IsNative() {
      return is_native;
    }

    void SetReturn(Type* r) {
      return_type = r;
    }

    void EncodeSignature() {
      parsed_return = EncodeType(return_type);

      // name
      parsed_name = name + L':';
      // params
      vector<Declaration*> declaration_list = declarations->GetDeclarations();
      for(size_t i = 0; i < declaration_list.size(); ++i) {
        SymbolEntry* entry = declaration_list[i]->GetEntry();
        if(entry) {
          parsed_name += EncodeType(entry->GetType());
        }
      }
    }

    void EncodeSignature(ParsedProgram* program, Linker* linker) {
      encoded_return = EncodeType(return_type, program, linker);

      // name
      encoded_name = name + L':';
      // params
      vector<Declaration*> declaration_list = declarations->GetDeclarations();
      for(size_t i = 0; i < declaration_list.size(); ++i) {
        SymbolEntry* entry = declaration_list[i]->GetEntry();
        if(entry) {
          encoded_name += EncodeType(entry->GetType(), program, linker) + L',';
        }
      }
    }

    void SetDeclarations(DeclarationList* d) {
      declarations = d;
    }

    bool HasAndOr() {
      return has_and_or;
    }

    void SetAndOr(bool ao) {
      has_and_or = ao;
    }

    bool IsStatic() {
      return is_static;
    }

    bool IsVirtual() {
      return statements == NULL;
    }

    MethodType GetMethodType() {
      return method_type;
    }

    void SetStatements(StatementList* s) {
      statements = s;
    }

    void SetSymbolTable(SymbolTable *t) {
      symbol_table = t;
    }

    SymbolTable* GetSymbolTable() {
      return symbol_table;
    }

    const std::wstring& GetName() const {
      return name;
    }

    const std::wstring& GetParsedName() {
      if(parsed_name.size() == 0) {
        EncodeSignature();
      }

      return parsed_name;
    }

    const std::wstring& GetEncodedName() const {
      return encoded_name;
    }

    const std::wstring& GetParsedReturn() const {
      return parsed_return;
    }

    const std::wstring& GetEncodedReturn() {
      if(encoded_return.size() == 0) {
        EncodeSignature();
      }

      return encoded_return;
    }

    DeclarationList* GetDeclarations() {
      return declarations;
    }

    StatementList* GetStatements() {
      return statements;
    }

    Type* GetReturn() {
      return return_type;
    }

    void SetClass(Class* k) {
      klass = k;
    }

    Class* GetClass() {
      return klass;
    }
  };

  /****************************
  * class Class
  ****************************/
  class Class : public ParseNode {
    friend class TreeFactory;
    int id;
    std::wstring name;
    std::wstring parent_name;
    multimap<const std::wstring, Method*> unqualified_methods;
    map<const std::wstring, Method*> methods;
    vector<Method*> method_list;
    vector<Statement*> statements;
    SymbolTable* symbol_table;
    Class* parent;
    LibraryClass* lib_parent;
    vector<Class*> interfaces;
    vector<LibraryClass*> lib_interfaces;
    vector<Class*> children;
    bool is_virtual;
    bool was_called;
    bool is_interface;
    MethodCall* anonymous_call;
    vector<std::wstring> interface_strings;

    Class(const std::wstring &f, const int l, const std::wstring &n, 
      const std::wstring &p, vector<std::wstring> e, bool i) : ParseNode(f, l) {
        name = n;
        parent_name = p;
        is_interface = i;
        id = -1;
        parent = NULL;
        interface_strings = e;
        lib_parent = NULL;
        is_virtual = false;
        was_called = false;
	anonymous_call = NULL;
    }

    ~Class() {
    } 

  public:
    void SetId(int i) {
      id = i;
    }

    int GetId() {
      return id;
    }

    void SetCalled(bool c) {
      was_called = c;
    }

    bool GetCalled() {
      return was_called;
    }

    vector<std::wstring> GetInterfaceNames() {
      return interface_strings;
    }

    const std::wstring& GetName() const {
      return name;
    }

    const std::wstring& GetParentName() const {
      return parent_name;
    }

    void SetParentName(std::wstring n) {
      parent_name = n;
    }

    void SetSymbolTable(SymbolTable *t) {
      symbol_table = t;
    }

    SymbolTable* GetSymbolTable() {
      return symbol_table;
    }
    
    void SetAnonymousCall(MethodCall* c) {
      anonymous_call = c;
    }
    
    MethodCall* GetAnonymousCall() {
      return anonymous_call;
    }
    
    bool AddMethod(Method* m) {
      const std::wstring &parsed_name = m->GetParsedName();
      for(size_t i = 0; i < method_list.size(); ++i) {
        if(method_list[i]->GetParsedName() == parsed_name) {
          return false;
        }
      }
      method_list.push_back(m);
      m->SetClass(this);

      return true;
    }

    void SetVirtual(bool v) {
      is_virtual = v;
    }

    bool IsVirtual() {
      return is_virtual;
    }

    bool IsInterface() {
      return is_interface;
    }

    void AddStatement(Statement* s) {
      statements.push_back(s);
    }

    vector<Statement*> GetStatements() {
      return statements;
    }

    Method* GetMethod(const std::wstring &n) {
      map<const std::wstring, Method*>::iterator result = methods.find(n);
      if(result != methods.end()) {
        return result->second;
      }

      return NULL;
    }

    vector<Method*> GetUnqualifiedMethods(const std::wstring &n) {
      vector<Method*> results;
      pair<multimap<const std::wstring, Method*>::iterator, 
        multimap<const std::wstring, Method*>::iterator> result;
      result = unqualified_methods.equal_range(n);
      multimap<const std::wstring, Method*>::iterator iter = result.first;
      for(iter = result.first; iter != result.second; ++iter) {
        results.push_back(iter->second);
      }

      return results;
    }

    vector<Method*> GetAllUnqualifiedMethods(const std::wstring &n) {
      if(n == L"New") {
        return GetUnqualifiedMethods(n);
      }

      vector<Method*> results = GetUnqualifiedMethods(n);
      Class* next = parent;
      while(next) {
        vector<Method*> next_results = next->GetUnqualifiedMethods(n);
        for(size_t i = 0; i < next_results.size(); ++i) {
          results.push_back(next_results[i]);
        }
        next = next->GetParent();
      }
      return results;
    }

    const vector<Method*> GetMethods() {
      return method_list;
    }

    void SetParent(Class* p) {
      parent = p;
    }

    Class* GetParent() {
      return parent;
    }

    void SetLibraryParent(LibraryClass* p) {
      lib_parent = p;
    }

    LibraryClass* GetLibraryParent() {
      return lib_parent;
    }

    void SetInterfaces(vector<Class*>& i) {
      interfaces = i;
    }

    vector<Class*> GetInterfaces() {
      return interfaces;
    }

    void SetLibraryInterfaces(vector<LibraryClass*>& i) {
      lib_interfaces = i;
    }

    vector<LibraryClass*> GetLibraryInterfaces() {
      return lib_interfaces;
    }

    void AddChild(Class* c) {
      children.push_back(c);
    }

    vector<Class*> GetChildren() {
      return children;
    }

    void AssociateMethods() {
      for(size_t i = 0; i < method_list.size(); ++i) {
        Method* method = method_list[i];
        methods.insert(pair<std::wstring, Method*>(method->GetEncodedName(), method));

        // add to unqualified names to list
        const std::wstring &encoded_name = method->GetEncodedName();
        const int start = encoded_name.find(':');
        if(start > -1) {
          const int end = encoded_name.find(':', start + 1);
          if(end > -1) {
            const std::wstring &unqualified_name = encoded_name.substr(start + 1, end - start - 1);
            unqualified_methods.insert(pair<std::wstring, Method*>(unqualified_name, method));
          }
        }
      }
    }
  };

  /****************************
  * MethodCall class
  ****************************/
  class MethodCall : public Statement, public Expression {
    friend class TreeFactory;
    EnumItem* enum_item;
    Class* original_klass;
    Class* anonymous_klass;
    Method* method;
    LibraryClass* original_lib_klass;
    LibraryMethod* lib_method;
    LibraryEnumItem* lib_enum_item;
    std::wstring variable_name;
    std::wstring method_name;
    ExpressionList* expressions;
    SymbolEntry* entry;
    MethodCallType call_type;
    Type* array_type;
    Variable* variable;
    bool is_enum_call;
    Type* func_rtrn;
    bool is_func_def;
    bool is_dyn_func_call;
    SymbolEntry* dyn_func_entry;

    MethodCall(const std::wstring &f, const int l, MethodCallType t,
      const std::wstring &v, ExpressionList* e) :
    Statement(f, l), Expression(f, l) {
      variable_name = v;
      call_type = t;
      method_name = L"New";
      expressions = e;
      entry = dyn_func_entry = NULL;
      method = NULL;
      array_type = NULL;
      variable = NULL;
      enum_item = NULL;
      method = NULL;
      lib_method = NULL;
      lib_enum_item = NULL;
      original_klass = NULL;
      original_lib_klass = NULL;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      func_rtrn = NULL;
      anonymous_klass = NULL;

      if(variable_name == BOOL_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(BOOLEAN_TYPE);
      } 
      else if(variable_name == BYTE_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(BYTE_TYPE);
      } 
      else if(variable_name == INT_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(INT_TYPE);
      } 
      else if(variable_name == FLOAT_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(FLOAT_TYPE);
      } 
      else if(variable_name == CHAR_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(CHAR_TYPE);
      } 
      else if(variable_name == NIL_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(NIL_TYPE);
      } 
      else if(variable_name == VAR_CLASS_ID) {
        array_type = TypeFactory::Instance()->MakeType(VAR_TYPE);
      }
      else {
        array_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, variable_name);
      }
      array_type->SetDimension((int)expressions->GetExpressions().size());
      SetEvalType(array_type, false);
    }

    MethodCall(const std::wstring &f, const int l,
      const std::wstring &v, const std::wstring &m,
      ExpressionList* e) :
    Statement(f, l), Expression(f, l) {
      variable_name = v;
      call_type = METHOD_CALL;
      method_name = m;
      expressions = e;
      entry = dyn_func_entry = NULL;
      method = NULL;
      array_type = NULL;
      variable = NULL;
      enum_item = NULL;
      method = NULL;
      lib_method = NULL;
      lib_enum_item = NULL;
      original_klass = NULL;
      original_lib_klass = NULL;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      func_rtrn = NULL;
      anonymous_klass = NULL;
    }

    MethodCall(const std::wstring &f, const int l,
      const std::wstring &v, const std::wstring &m) :
    Statement(f, l), Expression(f, l) {
      variable_name = v;
      call_type = ENUM_CALL;
      method_name = m;
      expressions = NULL;
      entry = dyn_func_entry = NULL;
      method = NULL;
      array_type = NULL;
      variable = NULL;
      enum_item = NULL;
      method = NULL;
      lib_method = NULL;
      lib_enum_item = NULL;
      original_klass = NULL;
      original_lib_klass = NULL;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      func_rtrn = NULL;
      anonymous_klass = NULL;
    }

    MethodCall(const std::wstring &f, const int l,
      Variable* v, const std::wstring &m,
      ExpressionList* e) :
    Statement(f, l), Expression(f, l) {
      variable = v;
      call_type = METHOD_CALL;
      method_name = m;
      expressions = e;
      entry = dyn_func_entry = NULL;
      method = NULL;
      array_type = NULL;
      enum_item = NULL;
      method = NULL;
      lib_method = NULL;
      lib_enum_item = NULL;
      original_klass = NULL;
      original_lib_klass = NULL;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      func_rtrn = NULL;
      anonymous_klass = NULL;
    }

    ~MethodCall() {
    }

  public:
    void SetFunctionReturn(Type* r) {
      func_rtrn = r;
      is_func_def = true;
    }

    Type* GetFunctionReturn() {
      return func_rtrn;
    }

    bool IsFunctionDefinition() {
      return is_func_def;
    }

    void SetDynamicFunctionCall(SymbolEntry* e) {
      dyn_func_entry = e;
      is_dyn_func_call = true;
    }

    bool IsDynamicFunctionCall() {
      return is_dyn_func_call;
    }

    SymbolEntry* GetDynamicFunctionEntry() {
      return dyn_func_entry;
    }

    void SetEnumCall(bool e) {
      is_enum_call = e;
    }

    bool IsEnumCall() {
      return is_enum_call;
    }

    MethodCallType GetCallType() {
      return call_type;
    }

    Type* GetArrayType() {
      return array_type;
    }

    const std::wstring& GetVariableName() const {
      return variable_name;
    }

    void SetVariableName(const std::wstring v) {
      variable_name = v;
    }

    void SetEntry(SymbolEntry* e) {
      entry = e;
    }

    Variable* GetVariable() {
      return variable;
    }

    void SetVariable(Variable* v) {
      variable = v;
    }

    SymbolEntry* GetEntry() {
      return entry;
    }

    const std::wstring& GetMethodName() const {
      return method_name;
    }

    const ExpressionType GetExpressionType() {
      return METHOD_CALL_EXPR;
    }

    const StatementType GetStatementType() {
      return METHOD_CALL_STMT;
    }

    ExpressionList* GetCallingParameters() {
      return expressions;
    }

    void SetEnumItem(EnumItem* i, const std::wstring &enum_name) {
      enum_item = i;
      SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, enum_name), false);
    }

    void SetMethod(Method* m, bool set_rtrn = true) {
      method = m;
      if(set_rtrn) {
        eval_type = m->GetReturn();
        if(method_call) {
          method_call->SetEvalType(eval_type, false);
        }
      }
    }

    void SetLibraryEnumItem(LibraryEnumItem* i, const std::wstring &enum_name) {
      lib_enum_item = i;
      SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, enum_name), false);
    }

    void SetLibraryMethod(LibraryMethod* l, bool set_rtrn = true) {
      lib_method = l;
      if(set_rtrn) {
        eval_type = l->GetReturn();
        if(method_call) {
          method_call->SetEvalType(eval_type, false);
        }
      }
    }

    EnumItem* GetEnumItem() {
      return enum_item;
    }

    LibraryEnumItem* GetLibraryEnumItem() {
      return lib_enum_item;
    }

    Method* GetMethod() {
      return method;
    }

    LibraryMethod* GetLibraryMethod() {
      return lib_method;
    }

    void SetAnonymousClass(Class* c) {
      anonymous_klass = c;
    }

    Class* GetAnonymousClass() {
      return anonymous_klass;
    }
    
    void SetOriginalClass(Class* c) {
      original_klass = c;
    }

    Class* GetOriginalClass() {
      return original_klass;
    }

    void SetOriginalLibraryClass(LibraryClass* c) {
      original_lib_klass = c;
    }

    LibraryClass* GetOriginalLibraryClass() {
      return original_lib_klass;
    }
  };

  /****************************
  * TreeFactory class
  ****************************/
  class TreeFactory {
    static TreeFactory* instance;

    vector<ParseNode*> nodes;
    vector<Statement*> statements;
    vector<Expression*> expressions;
    vector<DeclarationList*> declaration_lists;
    vector<StatementList*> statement_lists;
    vector<ExpressionList*> expression_lists;
    vector<MethodCall*> calls;
    vector<SymbolEntry*> entries;
    vector<Declaration*> declarations;

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

      while(!statements.empty()) {
        Statement* tmp = statements.front();
        statements.erase(statements.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }

      while(!declaration_lists.empty()) {
        DeclarationList* tmp = declaration_lists.front();
        declaration_lists.erase(declaration_lists.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }
      declaration_lists.clear();

      while(!statement_lists.empty()) {
        StatementList* tmp = statement_lists.front();
        statement_lists.erase(statement_lists.begin());
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

      while(!calls.empty()) {
        MethodCall* tmp = calls.front();
        calls.erase(calls.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }

      while(!entries.empty()) {
        SymbolEntry* tmp = entries.front();
        entries.erase(entries.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }

      delete instance;
      instance = NULL;
    }

    Enum* MakeEnum(const std::wstring &file_name, const int line_num, std::wstring &name, int offset) {
      Enum* tmp = new Enum(file_name, line_num, name, offset);
      nodes.push_back(tmp);
      return tmp;
    }

    EnumItem* MakeEnumItem(const std::wstring &file_name, const int line_num, const std::wstring &name, Enum* e) {
      EnumItem* tmp = new EnumItem(file_name, line_num, name, e);
      nodes.push_back(tmp);
      return tmp;
    }

    Class* MakeClass(const std::wstring &file_name, const int line_num, const std::wstring &name, 
      const std::wstring &parent_name, vector<std::wstring> enforces, 
      bool is_interface) {
        Class* tmp = new Class(file_name, line_num, name, parent_name, enforces, is_interface);
        nodes.push_back(tmp);
        return tmp;
    }

    Method* MakeMethod(const std::wstring &file_name, const int line_num, const std::wstring &name, MethodType type, bool is_function, bool is_native) {
      Method* tmp = new Method(file_name, line_num, name, type, is_function, is_native);
      nodes.push_back(tmp);
      return tmp;
    }

    StatementList* MakeStatementList() {
      StatementList* tmp = new StatementList;
      statement_lists.push_back(tmp);
      return tmp;
    }

    DeclarationList* MakeDeclarationList() {
      DeclarationList* tmp = new DeclarationList;
      declaration_lists.push_back(tmp);
      return tmp;
    }

    ExpressionList* MakeExpressionList() {
      ExpressionList* tmp = new ExpressionList;
      expression_lists.push_back(tmp);
      return tmp;
    }

    SystemStatement* MakeSystemStatement(const std::wstring &file_name, const int line_num, instructions::InstructionType instr) {
      SystemStatement* tmp = new SystemStatement(file_name, line_num, instr);
      statements.push_back(tmp);
      return tmp;
    }

    SystemStatement* MakeSystemStatement(const std::wstring &file_name, const int line_num, instructions::Traps trap) {
      SystemStatement* tmp = new SystemStatement(file_name, line_num, trap);
      statements.push_back(tmp);
      return tmp;
    }

    SimpleStatement* MakeSimpleStatement(const std::wstring &file_name, const int line_num, Expression* expression) {
      SimpleStatement* tmp = new SimpleStatement(file_name, line_num, expression);
      statements.push_back(tmp);
      return tmp;
    }

    EmptyStatement* MakeEmptyStatement(const std::wstring &file_name, const int line_num) {
      EmptyStatement*  tmp = new EmptyStatement(file_name, line_num);
      statements.push_back(tmp);
      return tmp;
    }
    
    Variable* MakeVariable(const std::wstring &file_name, int line_num, const std::wstring &name) {
      Variable* tmp = new Variable(file_name, line_num, name);
      expressions.push_back(tmp);
      return tmp;
    }

    Cond* MakeCond(const std::wstring &f, const int l, Expression* c, Expression* s, Expression* e) {
      Cond* tmp = new Cond(f, l, c, s, e);
      expressions.push_back(tmp);
      return tmp;
    }

    StaticArray* MakeStaticArray(const std::wstring &file_name, int line_num, ExpressionList* exprs) {
      StaticArray* tmp = new StaticArray(file_name, line_num, exprs);
      expressions.push_back(tmp);
      return tmp;
    }

    Declaration* MakeDeclaration(const std::wstring &file_name, const int line_num, SymbolEntry* entry, Assignment* assign) {
      Declaration* tmp = new Declaration(file_name, line_num, entry, assign);
      statements.push_back(tmp);
      return tmp;
    }

    Declaration* MakeDeclaration(const std::wstring &file_name, const int line_num, SymbolEntry* entry) {
      Declaration* tmp = new Declaration(file_name, line_num, entry);
      statements.push_back(tmp);
      return tmp;
    }

    CalculatedExpression* MakeCalculatedExpression(const std::wstring &file_name, int line_num, ExpressionType type) {
      CalculatedExpression* tmp = new CalculatedExpression(file_name, line_num, type);
      expressions.push_back(tmp);
      return tmp;
    }

    IntegerLiteral* MakeIntegerLiteral(const std::wstring &file_name, const int line_num, INT_VALUE value) {
      IntegerLiteral* tmp = new IntegerLiteral(file_name, line_num, value);
      expressions.push_back(tmp);
      return tmp;
    }

    FloatLiteral* MakeFloatLiteral(const std::wstring &file_name, const int line_num, FLOAT_VALUE value) {
      FloatLiteral* tmp = new FloatLiteral(file_name, line_num, value);
      expressions.push_back(tmp);
      return tmp;
    }

    CharacterLiteral* MakeCharacterLiteral(const std::wstring &file_name, const int line_num, wchar_t value) {
      CharacterLiteral* tmp = new CharacterLiteral(file_name, line_num, value);
      expressions.push_back(tmp);
      return tmp;
    }

    CharacterString* MakeCharacterString(const std::wstring &file_name, const int line_num, const std::wstring &char_string) {
      CharacterString* tmp = new CharacterString(file_name, line_num, char_string);
      expressions.push_back(tmp);
      return tmp;
    }

    NilLiteral* MakeNilLiteral(const std::wstring &file_name, const int line_num) {
      NilLiteral* tmp = new NilLiteral(file_name, line_num);
      expressions.push_back(tmp);
      return tmp;
    }

    BooleanLiteral* MakeBooleanLiteral(const std::wstring &file_name, const int line_num, bool boolean) {
      BooleanLiteral* tmp = new BooleanLiteral(file_name, line_num, boolean);
      expressions.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring &file_name, const int line_num, MethodCallType type,
      const std::wstring &value, ExpressionList* exprs) {
        MethodCall* tmp = new MethodCall(file_name, line_num, type, value, exprs);
        calls.push_back(tmp);
        return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring &f, const int l, const std::wstring &v, const std::wstring &m, ExpressionList* e) {
      MethodCall* tmp = new MethodCall(f, l, v, m, e);
      calls.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring &f, const int l, const std::wstring &v, const std::wstring &m) {
      MethodCall* tmp = new MethodCall(f, l, v, m);
      calls.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring &f, const int l, Variable* v, const std::wstring &m, ExpressionList* e) {
      MethodCall* tmp = new MethodCall(f, l, v, m, e);
      calls.push_back(tmp);
      return tmp;
    }

    If* MakeIf(const std::wstring &file_name, const int line_num, Expression* expression,
      StatementList* if_statements, If* next = NULL) {
        If* tmp = new If(file_name, line_num, expression, if_statements, next);
        statements.push_back(tmp);
        return tmp;
    }

    Break* MakeBreak(const std::wstring &file_name, const int line_num) {
      Break* tmp = new Break(file_name, line_num);
      statements.push_back(tmp);
      return tmp;
    }

    DoWhile* MakeDoWhile(const std::wstring &file_name, const int line_num,
      Expression* expression, StatementList* stmts) {
        DoWhile* tmp = new DoWhile(file_name, line_num, expression, stmts);
        statements.push_back(tmp);
        return tmp;
    }

    While* MakeWhile(const std::wstring &file_name, const int line_num,
      Expression* expression, StatementList* stmts) {
        While* tmp = new While(file_name, line_num, expression, stmts);
        statements.push_back(tmp);
        return tmp;
    }

    For* MakeFor(const std::wstring &file_name, const int line_num, Statement* pre_stmt, Expression* cond_expr,
      Statement* update_stmt, StatementList* stmts) {
        For* tmp = new For(file_name, line_num, pre_stmt, cond_expr, update_stmt, stmts);
        statements.push_back(tmp);
        return tmp;
    }

    CriticalSection* MakeCriticalSection(const std::wstring &file_name, const int line_num, Variable* var, StatementList* stmts) {
      CriticalSection* tmp = new CriticalSection(file_name, line_num, var, stmts);
      statements.push_back(tmp);
      return tmp;
    }

    Select* MakeSelect(const std::wstring &file_name, const int line_num, Expression* eval_expression,
      map<ExpressionList*, StatementList*> statement_map, 
      vector<StatementList*> statement_lists, StatementList* other) {
        Select* tmp = new Select(file_name, line_num, eval_expression, 
          statement_map, statement_lists, other);
        statements.push_back(tmp);
        return tmp;
    }

    Return* MakeReturn(const std::wstring &file_name, const int line_num, Expression* expression) {
      Return* tmp = new Return(file_name, line_num, expression);
      statements.push_back(tmp);
      return tmp;
    }

    Assignment* MakeAssignment(const std::wstring &file_name, const int line_num,
      Variable* variable, Expression* expression) {
        Assignment* tmp = new Assignment(file_name, line_num, variable, expression);
        statements.push_back(tmp);
        return tmp;
    }

    OperationAssignment* MakeOperationAssignment(const std::wstring &file_name, const int line_num,
      Variable* variable, Expression* expression, 
      StatementType stmt_type) {
        OperationAssignment* tmp = new OperationAssignment(file_name, line_num, variable, 
          expression, stmt_type);
        statements.push_back(tmp);
        return tmp;
    }

    SymbolEntry* MakeSymbolEntry(const std::wstring &f, int l, const std::wstring &n,
      Type* t, bool s, bool c, bool e = false) {
        SymbolEntry* tmp = new SymbolEntry(f, l, n, t, s, c, e);
        entries.push_back(tmp);
        return tmp;
    }
  };

  /****************************
  * ParsedBundle class
  ****************************/
  class ParsedBundle {
    std::wstring name;
    SymbolTableManager* symbol_table;
    map<const std::wstring, Enum*> enums;
    vector<Enum*> enum_list;
    map<const std::wstring, Class*> classes;
    vector<Class*> class_list;

  public:
    ParsedBundle(std::wstring &n, SymbolTableManager *t) {
      name = n;
      symbol_table = t;
    }

    ~ParsedBundle() {
      delete symbol_table;
      symbol_table = NULL;
    }

    const std::wstring& GetName() const {
      return name;
    }

    void AddEnum(Enum* e) {
      enums.insert(pair<std::wstring, Enum*>(e->GetName(), e));
      enum_list.push_back(e);
    }

    Enum* GetEnum(const std::wstring &e) {
      map<const std::wstring, Enum*>::iterator result = enums.find(e);
      if(result != enums.end()) {
        return result->second;
      }

      return NULL;
    }

    void AddClass(Class* cls) {
      classes.insert(pair<std::wstring, Class*>(cls->GetName(), cls));
      class_list.push_back(cls);
    }

    Class* GetClass(const std::wstring &n) {
      map<const std::wstring, Class*>::iterator result = classes.find(n);
      if(result != classes.end()) {
        return result->second;
      }

      return NULL;
    }

    const vector<Enum*> GetEnums() {
      return enum_list;
    }

    const vector<Class*> GetClasses() {
      return class_list;
    }

    SymbolTableManager* GetSymbolTableManager() {
      return symbol_table;
    }
  };

  struct int_string_comp {
    bool operator() (IntStringHolder* lhs, IntStringHolder* rhs) const {
      if(lhs->length != rhs->length) {
        return true;
      }

      for(int i = 0; i < lhs->length; i++) {
        if(lhs->value[i] != rhs->value[i]) {
          return true;
        }
      }

      return false;
    }
  };

  struct float_string_comp {
    bool operator() (FloatStringHolder* lhs, FloatStringHolder* rhs) const {
      if(lhs->length != rhs->length) {
        return true;
      }

      for(int i = 0; i < lhs->length; i++) {
        if(lhs->value[i] != rhs->value[i]) {
          return true;
        }
      }

      return false;
    }
  };

  /****************************
  * ParsedProgram class
  ****************************/
  class ParsedProgram {
    map<IntStringHolder*, int, int_string_comp> int_string_ids;
    vector<IntStringHolder*> int_strings;

    map<FloatStringHolder*, int, float_string_comp> float_string_ids;
    vector<FloatStringHolder*> float_strings;

    map<std::wstring, int> char_string_ids;
    vector<std::wstring> char_strings;

    vector<std::wstring> uses;
    vector<ParsedBundle*> bundles;
    vector<std::wstring> bundle_names;
    Class* start_class;
    Method* start_method;
    Linker* linker; // deleted elsewhere

  public:
    ParsedProgram() {
      linker = NULL;
      start_class = NULL;
      start_method = NULL;
    }

    ~ParsedProgram() {
      // clean up
      while(!bundles.empty()) {
        ParsedBundle* tmp = bundles.front();
        bundles.erase(bundles.begin());
        // delete
        delete tmp;
        tmp = NULL;
      }

      /*
      while(!int_strings.empty()) {
      IntStringHolder* tmp = int_strings.front();
      int_strings.erase(int_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
      }

      while(!float_strings.empty()) {
      FloatStringHolder* tmp = float_strings.front();
      float_strings.erase(float_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
      }
      */

      if(linker) {
        delete linker;
        linker = NULL;
      }

      // clear factories
      TreeFactory::Instance()->Clear();
      TypeFactory::Instance()->Clear();
    }

    void AddUses(vector<std::wstring> u) {
      for(size_t i = 0; i < u.size(); ++i) {
        vector<std::wstring>::iterator found = find(uses.begin(), uses.end(), u[i]);
        if(found == uses.end()) {
          uses.push_back(u[i]);
        }
      }
    }

    bool HasBundleName(const std::wstring& name) {
      vector<std::wstring>::iterator found = find(bundle_names.begin(), bundle_names.end(), name);
      return found != bundle_names.end();
    }

    const vector<std::wstring> GetUses() {
      return uses;
    }

    void AddBundle(ParsedBundle* b) {
      bundle_names.push_back(b->GetName());
      bundles.push_back(b);
    }

    const vector<ParsedBundle*> GetBundles() {
      return bundles;
    }

    void AddIntString(vector<Expression*> &int_elements, int id) {
      INT_VALUE* int_array = new INT_VALUE[int_elements.size()];
      for(size_t i = 0; i < int_elements.size(); ++i) {
        int_array[i] = static_cast<IntegerLiteral*>(int_elements[i])->GetValue();
      }

      IntStringHolder* holder = new IntStringHolder;
      holder->value = int_array;
      holder->length = int_elements.size();

      int_string_ids.insert(pair<IntStringHolder*, int>(holder, id));
      int_strings.push_back(holder);
    }

    int GetIntStringId(vector<Expression*> &int_elements) {
      INT_VALUE* int_array = new INT_VALUE[int_elements.size()];
      for(size_t i = 0; i < int_elements.size(); ++i) {
        int_array[i] = static_cast<IntegerLiteral*>(int_elements[i])->GetValue();
      }

      IntStringHolder* holder = new IntStringHolder;
      holder->value = int_array;
      holder->length = int_elements.size();

      map<IntStringHolder*, int, int_string_comp>::iterator result = int_string_ids.find(holder);
      if(result != int_string_ids.end()) {
        delete[] holder->value;
        holder->value = NULL;  
        delete holder;
        holder = NULL;

        return result->second;
      }

      delete[] holder->value;
      holder->value = NULL;
      delete holder;
      holder = NULL;

      return -1;
    }

    vector<IntStringHolder*> GetIntStrings() {
      return int_strings;
    }

    void AddFloatString(vector<Expression*> &float_elements, int id) {
      FLOAT_VALUE* float_array = new FLOAT_VALUE[float_elements.size()];
      for(size_t i = 0; i < float_elements.size(); ++i) {
        float_array[i] = static_cast<FloatLiteral*>(float_elements[i])->GetValue();
      }

      FloatStringHolder* holder = new FloatStringHolder;
      holder->value = float_array;
      holder->length = float_elements.size();

      float_string_ids.insert(pair<FloatStringHolder*, int>(holder, id));
      float_strings.push_back(holder);
    }

    int GetFloatStringId(vector<Expression*> &float_elements) {
      FLOAT_VALUE* float_array = new FLOAT_VALUE[float_elements.size()];
      for(size_t i = 0; i < float_elements.size(); ++i) {
        float_array[i] = static_cast<FloatLiteral*>(float_elements[i])->GetValue();
      }

      FloatStringHolder* holder = new FloatStringHolder;
      holder->value = float_array;
      holder->length = float_elements.size();


      map<FloatStringHolder*, int, float_string_comp>::iterator result = float_string_ids.find(holder);
      if(result != float_string_ids.end()) {
        delete[] holder->value;
        holder->value = NULL;  
        delete holder;
        holder = NULL;

        return result->second;
      }

      delete[] holder->value;
      holder->value = NULL;    
      delete holder;
      holder = NULL;

      return -1;
    }

    vector<FloatStringHolder*> GetFloatStrings() {
      return float_strings;
    }

    void AddCharString(const std::wstring &s, int id) {
      char_string_ids.insert(pair<std::wstring, int>(s, id));
      char_strings.push_back(s);
    }

    int GetCharStringId(const std::wstring &s) {
      map<std::wstring, int>::iterator result = char_string_ids.find(s);
      if(result != char_string_ids.end()) {
        return result->second;
      }

      return -1;
    }

    vector<std::wstring> GetCharStrings() {
      return char_strings;
    }

    Class* GetClass(const std::wstring &n) {
      for(size_t i = 0; i < bundles.size(); ++i) {
        Class* klass = bundles[i]->GetClass(n);
        if(klass) {
          return klass;
        }
      }

      return NULL;
    }

    Enum* GetEnum(const std::wstring &n) {
      for(size_t i = 0; i < bundles.size(); ++i) {
        Enum* e = bundles[i]->GetEnum(n);
        if(e) {
          return e;
        }
      }

      return NULL;
    }

    void SetLinker(Linker* l) {
      linker = l;
    }

    Linker* GetLinker() {
      return linker;
    }

    void SetStart(Class* c, Method* m) {
      start_class = c;
      start_method = m;
    }

    Class* GetStartClass() {
      return start_class;
    }

    Method* GetStartMethod() {
      return start_method;
    }
  };
}

#endif
