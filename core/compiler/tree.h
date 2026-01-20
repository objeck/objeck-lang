/***************************************************************************
 * Language parse tree.
 *
 * Copyright (c) 2025, Randy Hollines
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

#pragma once

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
#include <queue>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <exception>
#include <stdlib.h>
#include <assert.h>
#include "linker.h"
#include "../shared/instrs.h"

#ifdef _DEBUG
#include "../shared/logger.h"
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

namespace frontend {
  class TreeFactory;
  class Variable;
  class MethodCall;
  class Class;
  class Method;
  class Enum;
  class OperationAssignment;
  class ParsedProgram;
  class Assignment;

  /****************************
   * StatementType enum
   ****************************/
  enum StatementType {
    DECLARATION_STMT,
    ASSIGN_STMT,
    ADD_ASSIGN_STMT,
    SUB_ASSIGN_STMT,
    MUL_ASSIGN_STMT,
    DIV_ASSIGN_STMT,
    STRING_CONCAT_STMT,
    METHOD_CALL_STMT,
    SIMPLE_STMT,
    IF_STMT,
    BREAK_STMT,
    CONTINUE_STMT,
    DO_WHILE_STMT,
    WHILE_STMT,
    FOR_STMT,
    SELECT_STMT,
    ENUM_STMT,
    RETURN_STMT,
    LEAVING_STMT,
    CRITICAL_STMT,
    SYSTEM_STMT,
    EMPTY_STMT
  };

  /****************************
   * SymbolEntry class
   ****************************/
  class SymbolEntry : public ParseNode {
    friend class TreeFactory;
    std::vector<Variable*> variables;
    int id;
    std::wstring name;
    Type* type;
    bool is_static;
    bool is_local;
    bool is_self;
    bool is_param;
    bool is_loaded;

  SymbolEntry(const std::wstring &file_name, const int line_num, const int line_pos, const std::wstring &n, Type* t, 
        bool s, bool c, bool e = false) : ParseNode(file_name, line_num, line_pos) {
      name = n;
      id = -1;
      type = t;
      is_static = s;
      is_local = c;
      is_self = e;
      is_param = is_loaded = false;
    }

    ~SymbolEntry() {
    }

  public:
    void SetParameter() {
      is_param = true;
    }

    bool IsParameter() {
      return is_param;
    }

    void WasLoaded() {
      is_loaded = true;
    }

    bool IsLoaded() {
      return is_loaded;
    }

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

    const std::wstring GetName() const {
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

    const std::vector<Variable*> GetVariables() {
      return variables;
    }

    void SetId(int i);
    SymbolEntry* Copy();
  };

  /****************************
   * ScopeTable class
   ****************************/
  class ScopeTable {
    std::map<const std::wstring, SymbolEntry*> entries;
    ScopeTable* parent;
    std::vector<ScopeTable*> children;
    int child_pos;

  public:
    ScopeTable(ScopeTable* p) {
      parent = p;
      child_pos = 0;
    }

    ~ScopeTable() {
      // clean up
      while(!children.empty()) {
        ScopeTable* tmp = children.back();
        children.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }
    }

    std::vector<SymbolEntry*> GetEntries();

    SymbolEntry* GetEntry(const std::wstring &name);

    bool AddEntry(SymbolEntry* e) {
      if(GetEntry(e->GetName())) {
        return false;
      }
      // add
      entries.insert(std::pair<std::wstring, SymbolEntry*>(e->GetName(), e));
      return true;
    }

    ScopeTable* GetParent() {
      return parent;
    }

    ScopeTable* GetNextChild() {
      if(child_pos < (int)children.size()) {
        return children[child_pos++];
      }

      return nullptr;
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
    std::vector<SymbolEntry*> entries;

  public:
    SymbolTable() {
      head = parse_ptr = iter_ptr = new ScopeTable(nullptr);
    }

    ~SymbolTable() {
      delete head;
      head = nullptr;
    }

    std::vector<SymbolEntry*> GetEntries() {
      return entries;
    }

    SymbolEntry* GetEntry(const std::wstring &name);

    bool AddEntry(SymbolEntry* e, bool is_var = false);

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

    int GetDepth() {
      int count = 0;
      
      ScopeTable* tmp = iter_ptr;
      while(tmp) {
        count++;
        tmp = tmp->GetParent();
      }
      
      return count;
    }
  };

  /****************************
   * SymbolTableManager class
   ****************************/
  class SymbolTableManager {
    std::stack<SymbolTable*> scope;
    std::map<const std::wstring, SymbolTable*> tables;

  public:
    SymbolTableManager() {
    }

    ~SymbolTableManager() {
      // clean up
      std::map<const std::wstring, SymbolTable*>::iterator iter;
      for(iter = tables.begin(); iter != tables.end(); ++iter) {
        SymbolTable* tmp = iter->second;
        delete tmp;
        tmp = nullptr;
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

      tables.insert(std::pair<std::wstring, SymbolTable*>(namescope, scope.top()));
      scope.pop();
    }

    SymbolTable* CurrentParseScope() {
      return scope.top();
    }

    std::vector<SymbolEntry*> GetEntries(const std::wstring &namescope) {
      std::vector<SymbolEntry*> entries;
      std::map<const std::wstring, SymbolTable*>::iterator result = tables.find(namescope);
      if(result != tables.end()) {
        entries = result->second->GetEntries();
      }

      return entries;
    }

    SymbolTable* GetSymbolTable(const std::wstring &namescope) {
      std::map<const std::wstring, SymbolTable*>::iterator result = tables.find(namescope);
      if(result != tables.end()) {
        return result->second;
      }

      return nullptr;
    }

#ifdef _DIAG_LIB
    std::vector<std::wstring> GetNamescopes() {
      std::vector<std::wstring> namescopes;

      std::map<const std::wstring, SymbolTable*>::iterator iter;
      for(iter = tables.begin(); iter != tables.end(); ++iter) {
        namescopes.push_back(iter->first);
      }

      return namescopes;
    }
#endif
  };

  /****************************
   * Statement base class
   ****************************/
  class Statement : public ParseNode {
    int end_line_num;
    int end_line_pos;
  public:
    Statement(const std::wstring &f, const int l, const int p, const int el, const int ep) : ParseNode(f, l, p) {
      end_line_num = el;
      end_line_pos = ep;
    }

    virtual ~Statement() {
    }

    inline int GetEndLineNumber() {
      return end_line_num;
    }

    inline int GetEndLinePosition() {
      return end_line_pos;
    }

    virtual const StatementType GetStatementType() = 0;
  };

  /****************************
   * StatementList class
   ****************************/
  class StatementList {
    friend class TreeFactory;
    std::vector<Statement*> statements;

    StatementList() {
    }

    ~StatementList() {
    }

  public:
    std::vector<Statement*> GetStatements() {
      return statements;
    }

    void PrependStatement(Statement* s) {
      statements.insert(statements.begin(), s);
    }

    void AddStatement(Statement* s) {
      statements.push_back(s);
    }
  };

  /****************************
   * ExpressionType enum
   ****************************/
  enum ExpressionType {
    METHOD_CALL_EXPR,
    COND_EXPR,
    VAR_EXPR,
    NIL_LIT_EXPR,
    CHAR_LIT_EXPR,
    INT_LIT_EXPR,
    FLOAT_LIT_EXPR,
    BOOLEAN_LIT_EXPR,
    AND_EXPR,
    BIT_XOR_EXPR,
    BIT_NOT_EXPR,
    BIT_AND_EXPR,
    BIT_OR_EXPR,
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
    CHAR_STR_EXPR,
    STAT_ARY_EXPR,
    STR_CONCAT_EXPR,
    LAMBDA_EXPR
  };

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

    Expression(const std::wstring& file_name, const int line_num, const int line_pos) : ParseNode(file_name, line_num, line_pos) {
      base_type = eval_type = cast_type = type_of = nullptr;
      method_call = nullptr;
      prev_expr = nullptr;
      to_class = nullptr;
      to_lib_class = nullptr;
    }

    Expression(const std::wstring& file_name, const int line_num, const int line_pos, Type* t) : ParseNode(file_name, line_num, line_pos) {
      base_type = eval_type = TypeFactory::Instance()->MakeType(t);
      cast_type = nullptr;
      method_call = nullptr;
      prev_expr = nullptr;
      to_class = nullptr;
      to_lib_class = nullptr;
      type_of = nullptr;
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

    void SetCastType(Type* c, bool zd) {
      if(c) {
        cast_type = TypeFactory::Instance()->MakeType(c);
        if(zd) {
          cast_type->SetDimension(0);
        }
      }
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
    std::vector<Expression*> expressions;

    ExpressionList() {
    }

    ~ExpressionList() {
    }

  public:
    const std::vector<Expression*> GetExpressions() {
      return expressions;
    }

    const void SetExpressions(const std::vector<Expression*> &e) {
      expressions = e;
    }

    bool SetExpression(Expression* e, size_t i) {
      if(i < expressions.size()) {
        expressions[i] = e;
        return true;
      }
      
      return false;
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
    std::vector<int> sizes;
    bool matching_types;
    ExpressionType cur_type;
    bool matching_lengths;

    void GetAllElements(StaticArray* array, ExpressionList* elems);

    void GetSizes(StaticArray* array, int &count);

  public:
    StaticArray(const std::wstring& file_name, const int line_num, const int line_pos, ExpressionList* e) : Expression(file_name, line_num, line_pos) {
      elements = e;
      all_elements = nullptr;
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
      case BOOLEAN_LIT_EXPR:
        return BOOLEAN_TYPE;

      case INT_LIT_EXPR: {
        if(GetEvalType()) {
          if(GetEvalType()->GetType() == BYTE_TYPE) {
            return BYTE_TYPE;
          }
          else if(GetEvalType()->GetType() == CHAR_TYPE) {
            return CHAR_TYPE;
          }
        }

        return INT_TYPE;
      }

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

    std::vector<int> GetSizes();

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
    ENTRY,
    EXPRESSION
  };

  class CharacterStringSegment {
    CharacterStringSegmentType type;
    int id;
    std::wstring str;
    SymbolEntry* entry;
    Method* method;
    LibraryMethod* lib_method;
    Expression* expr;

  public:
    CharacterStringSegment(const std::wstring &s) {
      type = STRING;
      str = s;
      entry = nullptr;
      method = nullptr;
      lib_method = nullptr;
      expr = nullptr;
      id = -1;
    }

    CharacterStringSegment(SymbolEntry* e) {
      type = ENTRY;
      entry = e;
      method = nullptr;
      lib_method = nullptr;
      expr = nullptr;
      id = -1;
    }

    CharacterStringSegment(SymbolEntry* e, Method* m) {
      type = ENTRY;
      entry = e;
      method = m;
      lib_method = nullptr;
      expr = nullptr;
      id = -1;
    }

    CharacterStringSegment(SymbolEntry* e, LibraryMethod* m) {
      type = ENTRY;
      entry = e;
      method = nullptr;
      lib_method = m;
      expr = nullptr;
      id = -1;
    }

    CharacterStringSegment(Expression* x, Method* m, LibraryMethod* lm) {
      type = EXPRESSION;
      expr = x;
      method = m;
      lib_method = lm;
      entry = nullptr;
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

    Expression* GetExpression() {
      return expr;
    }
  };

  /*************************
   * CharacterString class
   *************************/
  class CharacterString : public Expression {
    friend class TreeFactory;
    bool is_processed;
    std::wstring char_string;
    std::vector<CharacterStringSegment*> segments;
    SymbolEntry* concat;
    bool is_lit;

    CharacterString(const std::wstring &file_name, const int line_num, const int line_pos, const std::wstring &c, const bool l) : 
        Expression(file_name, line_num, line_pos, Type::CharStringType()) {
      char_string = c;
      is_processed = false;
      concat = nullptr;
      is_lit = l;
    }

    ~CharacterString() {      
      while(!segments.empty()) {
        CharacterStringSegment* tmp = segments.back();
        segments.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
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

    const std::wstring GetString() const {
      return char_string;
    }

    bool AddSegment(const std::wstring &orig);

    void AddSegment(SymbolEntry* e) {
      e->WasLoaded();
      segments.push_back(new CharacterStringSegment(e)); 
    }

    void AddSegment(SymbolEntry* e, Method* m) {
      segments.push_back(new CharacterStringSegment(e, m)); 
    }

    void AddSegment(SymbolEntry* e, LibraryMethod* m) {
      segments.push_back(new CharacterStringSegment(e, m));
    }

    void AddSegment(Expression* x, Method* m, LibraryMethod* lm) {
      segments.push_back(new CharacterStringSegment(x, m, lm));
    }

    std::vector<CharacterStringSegment*> GetSegments() {
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

    CalculatedExpression(const std::wstring& file_name, const int line_num, const int line_pos, ExpressionType t) : Expression(file_name, line_num, line_pos) {
      left = right = nullptr;
      type = t;
    }

    CalculatedExpression(const std::wstring& file_name, const int line_num, const int line_pos, ExpressionType t, Expression* lhs, Expression* rhs) : Expression(file_name, line_num, line_pos) {
      left = lhs;
      right = rhs;
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
  class StringConcat : public Expression {
    friend class TreeFactory;
    std::list<Expression*> concat_exprs;
    SymbolEntry* concat;
    std::unordered_map<Expression*, Method*> method_to_string;
    std::unordered_map<Expression*, LibraryMethod*> lib_method_to_string;
    
    StringConcat(std::list<Expression*> e, std::unordered_map<Expression*, Method*> ms, std::unordered_map<Expression*, LibraryMethod*> ls) : Expression(L"", -1, -1) {
      concat_exprs = e;
      method_to_string = ms;
      lib_method_to_string = ls;
      concat = nullptr;

      SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.String"), true);
    }

    ~StringConcat() {
    }

  public:
    const ExpressionType GetExpressionType() {
      return STR_CONCAT_EXPR;
    }

    std::list<Expression*> GetExpressions() {
      return concat_exprs;
    }

    void SetConcat(SymbolEntry* c) {
      c->WasLoaded();
      concat = c;
    }

    SymbolEntry* GetConcat() {
      return concat;
    }

    Method* GetMethod(Expression* e) {
      return method_to_string[e];
    }

    LibraryMethod* GetLibraryMethod(Expression* e) {
      return lib_method_to_string[e];
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
    OperationAssignment* pre_operation;
    bool checked_pre_operation;
    OperationAssignment* post_operation;
    bool checked_post_operation;
    std::vector<Type*> concrete_types;
    bool is_alt;
    bool is_special;

    Variable(const std::wstring& file_name, const int line_num, const int line_pos, const std::wstring& n) : Expression(file_name, line_num, line_pos) {
      name = n;
      indices = nullptr;
      entry = nullptr;
      id = -1;
      pre_operation = post_operation = nullptr;
      checked_pre_operation = checked_post_operation = true;
      is_alt = is_special = false;
    }

    ~Variable() {
    }

  public:
    const std::wstring GetName() const {
      return name;
    }

    bool IsAlt() {
      return is_alt;
    }

    void WasAlt() {
      is_alt = true;
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

    const std::vector<Type*> GetConcreteTypes() {
      return concrete_types;
    }

    bool HasConcreteTypes() {
      return concrete_types.size() > 0;
    }

    void SetConcreteTypes(std::vector<Type*>& c) {
      concrete_types = c;
    }

    ExpressionList* GetIndices() {
      return indices;
    }

    void SetPreStatement(OperationAssignment* pre) {
      checked_pre_operation = false;
      pre_operation = pre;
    }

    void PreStatementChecked() {
      checked_pre_operation = true;
    }

    bool IsPreStatementChecked() {
      return checked_pre_operation;
    }

    OperationAssignment* GetPreStatement() {
      return pre_operation;
    }

    void SetPostStatement(OperationAssignment* post) {
      checked_post_operation = false;
      post_operation = post;
    }

    void PostStatementChecked() {
      checked_post_operation = true;
    }

    bool IsPostStatementChecked() {
      return checked_post_operation;
    }

    OperationAssignment* GetPostStatement() {
      return post_operation;
    }

    void SetInternalVariable(bool s) {
      is_special = s;
    }

    bool IsInternalVariable() {
      return is_special;
    }

    Variable* Copy();
  };

  /****************************
   * BooleanLiteral class
   ****************************/
  class BooleanLiteral : public Expression {
    friend class TreeFactory;
    bool value;

    BooleanLiteral(const std::wstring &file_name, const int line_num, const int line_pos, bool v) : Expression(file_name, line_num, line_pos, TypeFactory::Instance()->MakeType(BOOLEAN_TYPE)) {
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

    NilLiteral(const std::wstring& file_name, const int line_num, const int line_pos) : Expression(file_name, line_num, line_pos, TypeFactory::Instance()->MakeType(NIL_TYPE)) {
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

    CharacterLiteral(const std::wstring& file_name, const int line_num, const int line_pos, wchar_t v) : Expression(file_name, line_num, line_pos, TypeFactory::Instance()->MakeType(CHAR_TYPE)) {
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
    INT64_VALUE value;

  IntegerLiteral(const std::wstring &file_name, const int line_num, const int line_pos, INT64_VALUE v) : Expression(file_name, line_num, line_pos, TypeFactory::Instance()->MakeType(INT_TYPE)) {
      value = v;
    }

    ~IntegerLiteral() {
    }

  public:
    INT64_VALUE GetValue() {
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

  FloatLiteral(const std::wstring &file_name, const int line_num, const int line_pos, FLOAT_VALUE v) : Expression(file_name, line_num, line_pos, TypeFactory::Instance()->MakeType(FLOAT_TYPE)) {
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

    Cond(const std::wstring &file_name, const int line_num, const int line_pos, Expression* c, Expression* s, Expression* e) : Expression(file_name, line_num, line_pos) {
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

    Return(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Expression* e) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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

    void SetExpression(Expression* e) {
      expression = e;
    }
  };

  /****************************
   * Leaving class
   ****************************/
  class Leaving : public Statement {
    friend class TreeFactory;
    StatementList* statements;
    
    Leaving(const std::wstring& file_name, const int line_num, const int line_pos, 
            const int end_line_num, const int end_line_pos, StatementList* s) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      statements = s;
    }

    ~Leaving() {
    }

  public:
    const StatementType GetStatementType() {
      return LEAVING_STMT;
    }

    StatementList* GetStatements() {
      return statements;
    }
  };

  /****************************
   * Break class
   ****************************/
  class Break : public Statement {
    friend class TreeFactory;
    StatementType type;

    Break(const std::wstring& file_name, const int line_num, const int line_pos,
          const int end_line_num, const int end_line_pos, StatementType t) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      type = t;
    }

    ~Break() {
    }

  public:
    const StatementType GetStatementType() {
      return type;
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

    If(const std::wstring& file_name, const int line_num, const int line_pos, 
       const int end_line_num, const int end_line_pos, Expression* e, StatementList* s, If* n = nullptr) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      expression = e;
      if_statements = s;
      next = n;
      else_statements = nullptr;
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
    INT64_VALUE id;
    Enum* eenum;

    EnumItem(const std::wstring& file_name, const int line_num, const int line_pos, const std::wstring& n, Enum* e) : ParseNode(file_name, line_num, line_pos) {
      name = n;
      id = -1;
      eenum = e;
    }
    
    ~EnumItem() {
    }
    
  public:
    const std::wstring GetName() const {
      return name;
    }

    void SetId(INT64_VALUE i) {
      id = i;
    }

    Enum* GetEnum() {
      return eenum;
    }

    INT64_VALUE GetId() {
      return id;
    }
  };

  /****************************
   * Alias class
   ****************************/
  class Alias : public ParseNode {
    friend class TreeFactory;
    std::wstring name;
    std::map<const std::wstring, Type*> aliases;
    std::wstring encoded_name;
    std::wstring EncodeType(Type* type, ParsedProgram* program, Linker* linker);
    std::wstring EncodeFunctionType(std::vector<Type*> func_params, Type* func_rtrn, ParsedProgram* program, Linker* linker);

    Alias(const std::wstring& file_name, const int line_num, const int line_pos, const std::wstring& n) : ParseNode(file_name, line_num, line_pos) {
      name = n;
    }

    ~Alias() {
    }

  public:
    const std::wstring GetName() const {
      return name;
    }

    void EncodeSignature(ParsedProgram* program, Linker* linker) {
      if(encoded_name.empty()) {
        encoded_name += name;
        encoded_name += L'|';
        std::map<const std::wstring, Type*>::iterator iter;
        for(iter = aliases.begin(); iter != aliases.end(); ++iter) {
          encoded_name += iter->first;
          encoded_name += L'|';
          encoded_name += EncodeType(iter->second, program, linker);
          encoded_name += L';';
        }
      }
    }

    const std::wstring GetEncodedName() {
      return encoded_name;
    }

    bool AddType(const std::wstring &n, Type *t) {
      if(GetType(n)) {
        return false;
      }

      aliases.insert(std::pair<const std::wstring, Type*>(n, t));
      return true;
    }

    Type* GetType(const std::wstring& n) {
      std::map<const std::wstring, Type*>::iterator result = aliases.find(n);
      if(result != aliases.end()) {
        return result->second;
      }
      
      return nullptr;
    }
  };

  /****************************
   * Enum class
   ****************************/
  class Enum : public ParseNode {
    friend class TreeFactory;
    int end_line_num;
    int end_line_pos;
    std::wstring name;
    INT64_VALUE offset;
    INT64_VALUE index;
    std::map<const std::wstring, EnumItem*> items;

    Enum(const std::wstring& f, const int l, const int p, const int el, const int ep, const std::wstring& n, INT64_VALUE o) : ParseNode(f, l, p) {
      end_line_num = el;
      end_line_pos = ep;
      name = n;
      index = offset = o;
    }

    Enum(const std::wstring& f, const int l, const int p, const int el, const int ep, const std::wstring &n) : ParseNode(f, l, p) {
      end_line_num = el;
      end_line_pos = ep;
      name = n;
      index = offset = -1;
    }
    
    ~Enum() {
    }

  public:
    inline int GetEndLineNumber() {
      return end_line_num;
    }

    inline int GetEndLinePosition() {
      return end_line_pos;
    }

    void SetEndLineNumber(int el) {
      end_line_num = el;
    }

    void SetEndLinePosition(int ep) {
      end_line_pos = ep;
    }

    bool AddItem(EnumItem* e) {
      if(GetItem(e->GetName())) {
        return false;
      }
      
      e->SetId(index++);
      items.insert(std::pair<const std::wstring, EnumItem*>(e->GetName(), e));
      return true;
    }
    
    bool AddItem(EnumItem* e, INT64_VALUE value) {
      if(GetItem(e->GetName())) {
        return false;
      }

      e->SetId(value);
      items.insert(std::pair<const std::wstring, EnumItem*>(e->GetName(), e));
      return true;
    }
    
    EnumItem* GetItem(const std::wstring &i) {
      std::map<const std::wstring, EnumItem*>::iterator result = items.find(i);
      if(result != items.end()) {
        return result->second;
      }

      return nullptr;
    }

    const std::wstring GetName() const {
      return name;
    }

    INT64_VALUE GetOffset() {
      return offset;
    }

    std::map<const std::wstring, EnumItem*> GetItems() {
      return items;
    }
    
    bool IsConsts() {
      return offset < 0;
    }
  };

  /****************************
   * Select class
   ****************************/
  class Select : public Statement {
    friend class TreeFactory;
    Assignment* eval_assignment;
    std::map<INT64_VALUE, StatementList*> label_statements;
    std::vector<StatementList*> statement_lists;
    std::map<ExpressionList*, StatementList*> statement_map;
    StatementList* other;

    Select(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, 
           Assignment* e, std::map<ExpressionList*, StatementList*> &s, std::vector<StatementList*> &sl, StatementList* o) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      eval_assignment = e;
      statement_map = s;
      statement_lists = sl;
      other = o;
    }

    ~Select() {
    }

  public:
    void SetLabelStatements(std::map<INT64_VALUE, StatementList*> &s) {
      label_statements = s;
    }

    std::map<INT64_VALUE, StatementList*> GetLabelStatements() {
      return label_statements;
    }

    const StatementType GetStatementType() {
      return SELECT_STMT;
    }

    Assignment* GetAssignment() {
      return eval_assignment;
    }

    std::map<ExpressionList*, StatementList*> GetStatements() {
      return statement_map;
    }

    std::vector<StatementList*> GetStatementLists() {
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
  CriticalSection(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Variable* v, StatementList* s) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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
    StatementList* pre_stmts;
    Expression* cond_expr;
    StatementList* update_stmts;
    Assignment* bind_assign;
    StatementList* statements;
    bool is_range;
    SymbolEntry* range_entry;
    
    For(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
        StatementList* pre, Expression* cond, StatementList* updates, Assignment* bind_assignment, StatementList* stmts) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      pre_stmts = pre;
      cond_expr = cond;
      update_stmts = updates;
      bind_assign = bind_assignment;
      statements = stmts;
      is_range = false;
      range_entry = nullptr;
    }

    For(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, 
        StatementList* pre, Expression* cond, StatementList* updates, StatementList* stmts) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      pre_stmts = pre;
      cond_expr = cond;
      update_stmts = updates;
      bind_assign = nullptr;
      statements = stmts;
      is_range = false;
      range_entry = nullptr;
    }

    ~For() {
    }

  public:
    void SetRangeEntry(SymbolEntry* e) {
      range_entry = e;
    }

    SymbolEntry* GetRangeEntry() {
      return range_entry;
    }

    const bool IsRange() {
      return is_range;
    }

    void SetRange(bool r) {
      is_range = r;
    }

    const StatementType GetStatementType() {
      return FOR_STMT;
    }

    StatementList* GetPreStatements() {
      return pre_stmts;
    }

    Expression* GetExpression() {
      return cond_expr;
    }

    StatementList* GetUpdateStatements() {
      return update_stmts;
    }

    bool IsBoundAssignment() {
      return bind_assign != nullptr;
    }

    Assignment* GetBoundAssignment() {
      return bind_assign;
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

  DoWhile(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, 
          Expression* e, StatementList* s) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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

    While(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Expression* e, StatementList* s) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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

    SystemStatement(const std::wstring& file_name, const int line_num, const int line_pos, 
                    const int end_line_num, const int end_line_pos, int i) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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

    SimpleStatement(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Expression* e) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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
    EmptyStatement(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
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
    Assignment* child;
    Variable* variable;
    Expression* expression;
    
    Assignment(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, 
               Assignment* c, Variable* v, Expression* e) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      child = c;
      variable = v;
      expression = e;
    }

    Assignment(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, 
               Variable* v, Expression* e) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      child = nullptr;
      variable = v;
      expression = e;
    }

    virtual ~Assignment() {
    }

  public:
    Assignment* GetChild() {
      return child;
    }

    Variable* GetVariable() {
      return variable;
    }

    Expression* GetExpression() {
      return expression;
    }

    void SetExpression(Expression* e) {
      expression = e;
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
    bool is_string_concat;

    OperationAssignment(const std::wstring& file_name, const int line_num, const int line_pos, 
                        const int end_line_num, const int end_line_pos, Variable* v, Expression* e, StatementType t) : Assignment(file_name, line_num, line_pos, end_line_num, end_line_pos, v, e) {
      stmt_type = t;
      is_string_concat = false;
    }

    ~OperationAssignment() {
    }

  public:
    const StatementType GetStatementType() {
      return stmt_type;
    }

    void SetStatementType(StatementType t) {
      stmt_type = t;
    }

    bool IsStringConcat() {
      return is_string_concat;
    }

    void SetStringConcat(bool c) {
      is_string_concat = c;
    }
  };

  /****************************
   * Declaration class
   ****************************/
  class Declaration : public Statement {
    friend class TreeFactory;
    SymbolEntry* entry;
    Assignment* assignment;
    Declaration* child;

    Declaration(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                SymbolEntry* e, Declaration* c, Assignment* a) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      entry = e;
      child = c;
      assignment = a;
    }

    Declaration(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                SymbolEntry* e, Declaration* c) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos) {
      entry = e;
      child = c;
      assignment = nullptr;
    }

    ~Declaration() {
    }


  public:
    SymbolEntry* GetEntry() {
      return entry;
    }

    void SetParameter() {
      if(entry) {
        entry->SetParameter();
      }
    }

    Declaration* GetChild() {
      return child;
    }

    Assignment* GetAssignment() {
      return assignment;
    }

    void SetAssignment(Assignment* a) {
      assignment = a;
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
    std::vector<Declaration*> declarations;

    DeclarationList() {
    }

    ~DeclarationList() {
    }

  public:
    std::vector<Declaration*> GetDeclarations() {
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
    int end_line_num;
    int end_line_pos;
    int mid_line_num;
    int mid_line_pos;
    int id;
    std::wstring name;
    std::wstring parsed_name;
    std::wstring user_name;
    std::wstring encoded_name;
    std::wstring encoded_return;
    std::wstring parsed_return;
    StatementList* statements;
    DeclarationList* declarations;
    Type* return_type;
    Leaving* leaving;
    MethodType method_type;
    bool is_lambda;
    bool is_static;
    bool is_native;
    bool has_and_or;
    Method* original;
    SymbolTable* symbol_table;
    Class* klass;
#ifdef _DIAG_LIB
    std::vector<Expression*> diagnostic_expressions;
    std::vector<MethodCall*> diagnostic_method_calls;
#endif
    
    Method(const std::wstring& f, const int l, const int p, const int ml, const int mp, const std::wstring& n,
           MethodType m, bool s, bool c) : ParseNode(f, l, p) {
      mid_line_num = ml;
      mid_line_pos = mp;
      end_line_num = end_line_pos = -1;
      name = n;
      method_type = m;
      is_static = s;
      is_native = c;
      is_lambda = false;
      statements = nullptr;
      return_type = nullptr;
      leaving = nullptr;
      declarations = nullptr;
      id = -1;
      has_and_or = false;
      original = nullptr;
    }

    Method(const std::wstring& f, const int l, const int p, const int ml, const int mp, const std::wstring& n) : ParseNode(f, l, p) {
      mid_line_num = ml;
      mid_line_pos = mp;
      end_line_num = end_line_pos = -1;
      name = n;
      method_type = PRIVATE_METHOD;
      is_static = true;
      is_native = false;
      is_lambda = true;
      statements = nullptr;
      return_type = nullptr;
      leaving = nullptr;
      declarations = nullptr;
      id = -1;
      has_and_or = false;
      original = nullptr;
    }

    ~Method() {
    }

    std::wstring EncodeType(Type* type, Class* klass, ParsedProgram* program, Linker* linker);

    std::wstring EncodeFunctionType(std::vector<Type*> &func_params, Type* func_rtrn,Class* klass, ParsedProgram* program, Linker* linker);

    std::wstring EncodeType(Type* type);

    std::wstring EncodeUserType(Type* type);

    std::wstring EncodeGenerics(Type* type);

    std::wstring ReplaceSubstring(std::wstring s, const std::wstring f, const std::wstring &r) {
      const size_t index = s.find(f);
      if(index != std::string::npos) {
        s.replace(index, f.size(), r);
      }

      return s;
    }
    
  public:
    void SetId();

    int GetId() {
      return id;
    }

    bool IsNative() {
      return is_native;
    }

    bool IsLambda() {
      return is_lambda;
    }

    void SetReturn(Type* r) {
      return_type = r;
    }

    inline int GetMidLineNumber() {
      return mid_line_num;
    }

    inline int GetMidLinePosition() {
      return mid_line_pos;
    }

    inline int GetEndLineNumber() {
      return end_line_num;
    }

    inline int GetEndLinePosition() {
      return end_line_pos;
    }

    void SetEndLineNumber(int el) {
      end_line_num = el;
    }

    void SetEndLinePosition(int ep) {
      end_line_pos = ep;
    }
    
    void EncodeSignature() {
      // return type
      parsed_return = EncodeType(return_type);

      // name
      parsed_name = name + L':';

      // params
      std::vector<Declaration*> declaration_list = declarations->GetDeclarations();
      for(size_t i = 0; i < declaration_list.size(); ++i) {
        SymbolEntry* entry = declaration_list[i]->GetEntry();
        if(entry) {
          parsed_name += EncodeType(entry->GetType());
        }
      }
    }

    void EncodeUserName();

    void EncodeSignature(Class* klass, ParsedProgram* program, Linker* linker) {
      encoded_return = EncodeType(return_type, klass, program, linker);

      // name
      encoded_name = name + L':';

      // params
      std::vector<Declaration*> declaration_list = declarations->GetDeclarations();
      for(size_t i = 0; i < declaration_list.size(); ++i) {
        SymbolEntry* entry = declaration_list[i]->GetEntry();
        if(entry) {
          encoded_name += EncodeType(entry->GetType(), klass, program, linker) + L',';
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

    void SetOriginal(Method* o) {
      original = o;
    }

    bool IsAlt() {
      return original;
    }

    Method* GetOriginal() {
      return original;
    }
    
    bool IsStatic() {
      return is_static;
    }

    bool IsVirtual() {
      return statements == nullptr;
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

    const std::wstring GetName() const {
      return name;
    }

    const std::wstring GetParsedName() {
      if(parsed_name.size() == 0) {
        EncodeSignature();
      }

      return parsed_name;
    }

    const std::wstring GetUserName() {
      if(user_name.size() == 0) {
        EncodeUserName();
      }

      return user_name;
    }

    const std::wstring GetEncodedName() const {
      return encoded_name;
    }

    const std::wstring GetParsedReturn() const {
      return parsed_return;
    }

    const std::wstring GetEncodedReturn() {
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

    Leaving* GetLeaving() {
      return leaving;
    }
    
    void SetLeaving(Leaving* l) {
      leaving = l;
    }
    
    void SetClass(Class* k) {
      klass = k;
    }

    Class* GetClass() {
      return klass;
    }

#ifdef _DIAG_LIB
    void SetExpressions(std::vector<Expression*> e) {
      diagnostic_expressions = e;
    }

    std::vector <Expression*> GetExpressions() {
      return diagnostic_expressions;
    }

    void AddMethodCall(MethodCall* method_call) {
      diagnostic_method_calls.push_back(method_call);
    }

    std::vector <MethodCall*> GetMethodCalls() {
      return diagnostic_method_calls;
    }
#endif
  };

  /****************************
   * class Class
   ****************************/
  class Class : public ParseNode {
    friend class TreeFactory;
    int end_line_num;
    int end_line_pos;
    int id;
    int lambda_id;
    std::wstring name;
    std::wstring parent_name;
    std::multimap<const std::wstring, Method*> unqualified_methods;
    std::map<const std::wstring, Method*> methods;
    std::vector<Method*> method_list;
    int next_method_id;
    std::vector<Statement*> statements;
    SymbolTable* symbol_table;
    Class* parent;
    LibraryClass* lib_parent;
    std::vector<Class*> interfaces;
    std::vector<LibraryClass*> lib_interfaces;
    std::vector<Class*> children;
    bool is_virtual;
    bool is_generic;
    bool was_called;
    bool is_interface;
    bool is_public;
    MethodCall* anonymous_call;
    std::vector<std::wstring> interface_names;
    std::vector<Class*> generic_classes;
    Type* generic_interface;
#ifdef _DIAG_LIB
    std::vector<Expression*> diagnostic_expressions;
#endif

    Class(const std::wstring& f, const int l, const int p, const int el, const int ep, const std::wstring& n, const std::wstring& pn,
          std::vector<std::wstring> &e, std::vector<Class*> &g, bool s, bool i) : ParseNode(f, l, p) {
      end_line_num = el;
      end_line_pos = ep;
      name = n;
      parent_name = pn;
      is_interface = i;
      is_public = s;
      id = -1;
      lambda_id = 0;
      next_method_id = -1;
      parent = nullptr;
      lib_parent = nullptr;
      interface_names = e;
      generic_classes = g;      
      is_virtual = is_generic = was_called = false;
      anonymous_call = nullptr;
      symbol_table = nullptr;
      generic_interface = nullptr;
    }

    Class(const std::wstring& f, const int l, const int p, const int el, const int ep, const std::wstring& n,
          const std::wstring &pn, std::vector<std::wstring> &e) : ParseNode(f, l, p) {
      end_line_num = el;
      end_line_pos = ep;
      name = n;
      parent_name = pn;
      is_interface = false;
      is_public = true;
      id = -1;
      lambda_id = 0;
      next_method_id = -1;
      interface_names = e;
      parent = nullptr;
      lib_parent = nullptr;      
      is_virtual = is_generic = was_called = false;
      anonymous_call = nullptr;
      symbol_table = nullptr;
      generic_interface = nullptr;
    }
    
    Class(const std::wstring& f, const int l, const int p, const int el, const int ep, const std::wstring& n, bool g) : ParseNode(f, l, p) {
      end_line_num = el;
      end_line_pos = ep;
      name = n;
      is_interface = !g;
      is_public = true;
      id = -1;
      lambda_id = 0;
      next_method_id = -1;
      parent = nullptr;
      lib_parent = nullptr;       
      is_virtual = was_called = false;
      is_generic = g;
      anonymous_call = nullptr;
      symbol_table = nullptr;
      generic_interface = nullptr;
    }

    ~Class() {
    } 

  public:
    inline int GetEndLineNumber() {
      return end_line_num;
    }

    inline int GetEndLinePosition() {
      return end_line_pos;
    }

    void SetEndLineNumber(int el) {
      end_line_num = el;
    }

    void SetEndLinePosition(int ep) {
      end_line_pos = ep;
    }

    void SetId(int i) {
      id = i;
    }

    int NextLambda() {
      return lambda_id++;
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

    void SetGenericInterface(const std::wstring &n) {
      generic_interface = TypeFactory::Instance()->MakeType(CLASS_TYPE, n);
      interface_names.push_back(n);
    }

    Type* GetGenericInterface() {
      return generic_interface;
    }

    const std::wstring GetName() const {
      return name;
    }

    std::wstring GetBundleName() {
      const size_t index = name.find_last_of(L'.');
      if(index != std::string::npos) {
        return name.substr(0, index);
      }

      return L"Default";
    }

    const std::wstring GetParentName() const {
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
      const std::wstring parsed_name = m->GetParsedName();
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

    bool IsPublic() {
      return is_public;
    }

    void SetPublic(bool p) {
      is_public = p;
    }

    bool HasGenerics() {
      return !generic_classes.empty();
    }

    int GenericIndex(const std::wstring& n) {
      for(size_t i = 0; i < generic_classes.size(); ++i) {
        if(n == generic_classes[i]->GetName()) {
          return (int)i;
        }
      }

      return -1;
    }

    const std::vector<Class*> GetGenericClasses() {
      return generic_classes;
    }

    void SetGenericClasses(const std::vector<Class*> &g) {
      generic_classes = g;
    }

    bool HasGenericInterface() {
      return generic_interface != nullptr;
    }

    std::vector<std::wstring> GetInterfaceNames() {
      return interface_names;
    }

    const std::vector<std::wstring> GetGenericStrings() {
      std::vector<std::wstring> generic_strings;

      for(size_t i = 0; i < generic_classes.size(); ++i) {
        Class* generic_class = generic_classes[i];
        std::wstring generic_string = generic_class->GetName();
        generic_string += L'|';
        if(generic_class->HasGenericInterface()) {
          generic_string += generic_class->GetGenericInterface()->GetName();
        }
        generic_strings.push_back(generic_string);
      }

      return generic_strings;
    }

    Class* GetGenericClass(const std::wstring& n) {
      const int index = GenericIndex(n);
      if(index > -1) {
        return generic_classes[index];
      }

      return nullptr;
    }

    bool IsGeneric() {
      return is_generic;
    }

    std::vector<Class*> GetGenericNames() {
      return generic_classes;
    }

    void AddStatement(Statement* s) {
      statements.push_back(s);
    }

    std::vector<Statement*> GetStatements() {
      return statements;
    }

    Method* GetMethod(const std::wstring &n) {
      std::map<const std::wstring, Method*>::iterator result = methods.find(n);
      if(result != methods.end()) {
        return result->second;
      }

      return nullptr;
    }

    std::vector<Method*> GetUnqualifiedMethods(const std::wstring &n) {
      std::vector<Method*> results;
      std::pair<std::multimap<const std::wstring, Method*>::iterator, std::multimap<const std::wstring, Method*>::iterator> result;
      result = unqualified_methods.equal_range(n);
      std::multimap<const std::wstring, Method*>::iterator iter = result.first;
      for(iter = result.first; iter != result.second; ++iter) {
        results.push_back(iter->second);
      }

      return results;
    }

    std::vector<Method*> GetAllUnqualifiedMethods(const std::wstring &n) {
      if(n == L"New") {
        return GetUnqualifiedMethods(n);
      }

      std::vector<Method*> results = GetUnqualifiedMethods(n);
      Class* next = parent;
      while(next) {
        std::vector<Method*> next_results = next->GetUnqualifiedMethods(n);
        for(size_t i = 0; i < next_results.size(); ++i) {
          results.push_back(next_results[i]);
        }
        next = next->GetParent();
      }
      return results;
    }

    const std::vector<Method*> GetMethods() {
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

    void SetInterfaces(std::vector<Class*>& i) {
      interfaces = i;
    }

    std::vector<Class*> GetInterfaces() {
      return interfaces;
    }

    void SetLibraryInterfaces(std::vector<LibraryClass*>& i) {
      lib_interfaces = i;
    }

    std::vector<LibraryClass*> GetLibraryInterfaces() {
      return lib_interfaces;
    }

    void AddChild(Class* c) {
      children.push_back(c);
    }

    std::vector<Class*> GetChildren() {
      return children;
    }

    void AssociateMethods();

    void AssociateMethod(Method* method);

    bool HasDefaultNew();

    int NextMethodId() {
      return ++next_method_id;
    }

#ifdef _DIAG_LIB
    void SetExpressions(std::vector<Expression*> &e) {
      diagnostic_expressions = e;
    }

    std::vector < Expression*> GetExpressions() {
      return diagnostic_expressions;
    }
#endif
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
    instructions::MemoryType is_rouge_return;
    SymbolEntry* dyn_func_entry;
    std::vector<Type*> concrete_types;
    int mid_line_num;
    int mid_line_pos;

    MethodCall(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, 
               const int end_line_pos, MethodCallType t, const std::wstring &v, ExpressionList* e);

    MethodCall(const std::wstring& file_name, const int line_num, const int line_pos, const int ml, const int mp,
               const int end_line_num, const int end_line_pos, const std::wstring& v, const std::wstring& m, 
               ExpressionList* e) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos), Expression(file_name, line_num, line_pos) {
      mid_line_num = ml;
      mid_line_pos = mp;
      variable_name = v;
      call_type = METHOD_CALL;
      method_name = m;
      expressions = e;
      entry = dyn_func_entry = nullptr;
      method = nullptr;
      array_type = nullptr;
      variable = nullptr;
      enum_item = nullptr;
      method = nullptr;
      lib_method = nullptr;
      lib_enum_item = nullptr;
      original_klass = nullptr;
      original_lib_klass = nullptr;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      is_rouge_return = instructions::NIL_TYPE;
      func_rtrn = nullptr;
      anonymous_klass = nullptr;
    }

    MethodCall(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, 
               const int end_line_pos, const std::wstring& v, const std::wstring &m) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos), Expression(file_name, line_num, line_pos) {
      mid_line_num = mid_line_pos = -1;
      variable_name = v;
      call_type = ENUM_CALL;
      method_name = m;
      expressions = nullptr;
      entry = dyn_func_entry = nullptr;
      method = nullptr;
      array_type = nullptr;
      variable = nullptr;
      enum_item = nullptr;
      method = nullptr;
      lib_method = nullptr;
      lib_enum_item = nullptr;
      original_klass = nullptr;
      original_lib_klass = nullptr;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      is_rouge_return = instructions::NIL_TYPE;
      func_rtrn = nullptr;
      anonymous_klass = nullptr;
    }

    MethodCall(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num,
               const int end_line_pos, Variable* v) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos), Expression(file_name, line_num, line_pos) {
      mid_line_num = mid_line_pos = -1;
      call_type = ENUM_CALL;
      variable = v;
      expressions = nullptr;
      entry = dyn_func_entry = nullptr;
      method = nullptr;
      array_type = nullptr;
      enum_item = nullptr;
      method = nullptr;
      lib_method = nullptr;
      lib_enum_item = nullptr;
      original_klass = nullptr;
      original_lib_klass = nullptr;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      is_rouge_return = instructions::NIL_TYPE;
      func_rtrn = nullptr;
      anonymous_klass = nullptr;
    }
    
    MethodCall(const std::wstring& file_name, const int line_num, const int line_pos, const int ml, const int mp,
               const int end_line_num, const int end_line_pos, Variable* v, const std::wstring& m,
               ExpressionList* e) : Statement(file_name, line_num, line_pos, end_line_num, end_line_pos), Expression(file_name, line_num, line_pos) {
      mid_line_num = ml;
      mid_line_pos = mp;
      variable = v;
      call_type = METHOD_CALL;
      method_name = m;
      expressions = e;
      entry = dyn_func_entry = nullptr;
      method = nullptr;
      array_type = nullptr;
      enum_item = nullptr;
      method = nullptr;
      lib_method = nullptr;
      lib_enum_item = nullptr;
      original_klass = nullptr;
      original_lib_klass = nullptr;
      is_enum_call = is_func_def = is_dyn_func_call = false;
      func_rtrn = nullptr;
      is_rouge_return = instructions::NIL_TYPE;
      anonymous_klass = nullptr;
    }

    ~MethodCall() {
    }

  public:
    void SetFunctionalReturn(Type* r) {
      func_rtrn = r;
      is_func_def = true;
    }

    Type* GetFunctionalReturn() {
      return func_rtrn;
    }

    bool IsFunctionDefinition() {
      return is_func_def;
    }

    void SetFunctionalCall(SymbolEntry* e) {
      dyn_func_entry = e;
      is_dyn_func_call = true;
    }

    bool IsFunctionalCall() {
      return is_dyn_func_call;
    }

    inline int GetMidLineNumber() {
      return mid_line_num;
    }

    inline int GetMidLinePosition() {
      return mid_line_pos;
    }

    SymbolEntry* GetFunctionalEntry() {
      return dyn_func_entry;
    }

    void SetEnumCall(bool e) {
      is_enum_call = e;
    }

    bool IsEnumCall() {
      return is_enum_call;
    }

    void SetRougeReturn(instructions::MemoryType t) {
      is_rouge_return = t;
    }

    const instructions::MemoryType GetRougeReturn() {
      return is_rouge_return;
    } 

    MethodCallType GetCallType() {
      return call_type;
    }

    void SetCallType(MethodCallType t) {
      call_type = t;
    }
    
    Type* GetArrayType() {
      return array_type;
    }

    const std::wstring GetVariableName() const {
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

    const std::wstring GetMethodName() const {
      return method_name;
    }

    void SetEnumName(const std::wstring &v, const std::wstring& m) {
      method_name = m;
      variable_name = v;
    }

    const std::vector<Type*> GetConcreteTypes() {
      return concrete_types;
    }

    bool HasConcreteTypes() {
      return concrete_types.size() > 0;
    }

    void SetConcreteTypes(std::vector<Type*> &c) {
      concrete_types  = c;
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

    void SetCallingParameters(ExpressionList* e) {
      expressions = e;
    }

    void SetEnumItem(EnumItem* i, const std::wstring &enum_name) {
      enum_item = i;
      SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, enum_name), false);
    }

    void SetMethod(Method* m, bool set_rtrn = true) {
      method = m;
      if(set_rtrn) {
        eval_type = TypeFactory::Instance()->MakeType(m->GetReturn());
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
        eval_type = TypeFactory::Instance()->MakeType(l->GetReturn());
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

  /*************************
   * Lambda expression class
   *************************/
  class Lambda : public Expression {
    friend class TreeFactory;
    int end_line_num;
    int end_line_pos;
    Type* type;
    std::wstring name;
    Method* method;
    ExpressionList* parameters;
    MethodCall* method_call;
    std::vector<std::pair<SymbolEntry*, SymbolEntry*> > copies;
    
  public:
    Lambda(const std::wstring& file_name, const int line_num, const int line_pos, const int el, const int ep, Type* t, const std::wstring &n, Method* m, ExpressionList* p) : Expression(file_name, line_num, line_pos) {
      end_line_num = el;
      end_line_pos = ep;
      type = t;
      name = n;
      method = m;
      parameters = p;
      method_call = nullptr;
    }

    ~Lambda() {
    }

    const ExpressionType GetExpressionType() {
      return LAMBDA_EXPR;
    }

    const std::wstring GetName() {
      return name;
    }

    inline int GetEndLineNumber() {
      return end_line_num;
    }

    inline int GetEndLinePosition() {
      return end_line_pos;
    }

    void SetEndLineNumber(int el) {
      end_line_num = el;
    }

    void SetEndLinePosition(int ep) {
      end_line_pos = ep;
    }

    Type* GetLambdaType() {
      return type;
    }

    Method* GetMethod() {
      return method;
    }

    ExpressionList* GetParameters() {
      return parameters;
    }

    MethodCall* GetMethodCall() {
      return method_call;
    }

    void SetMethodCall(MethodCall* c) {
      method_call = c;
    }
    
    void AddClosure(SymbolEntry* var_entry, SymbolEntry* capture_entry) {
      copies.push_back(std::pair<SymbolEntry*, SymbolEntry*>(var_entry, capture_entry));
    }

    inline bool HasClosure(SymbolEntry* capture_entry) {
      return GetClosure(capture_entry) != nullptr;
    }

    inline SymbolEntry* GetClosure(SymbolEntry* capture_entry) {
      for(size_t i = 0; i < copies.size(); ++i) {
        if(copies[i].second == capture_entry) {
          return copies[i].first;
        }
      }

      return nullptr;
    }

    std::vector<std::pair<SymbolEntry*, SymbolEntry*> > GetClosures() {
      return copies;
    }
  };

  /****************************
   * TreeFactory class
   ****************************/
  class TreeFactory {
    static TreeFactory* instance;
    std::vector<ParseNode*> nodes;
    std::vector<Statement*> statements;
    std::vector<Expression*> expressions;
    std::vector<DeclarationList*> declaration_lists;
    std::vector<StatementList*> statement_lists;
    std::vector<ExpressionList*> expression_lists;
    std::vector<MethodCall*> calls;
    std::vector<SymbolEntry*> entries;
    std::vector<Declaration*> declarations;

    TreeFactory() {
    }

    ~TreeFactory() {
    }

  public:
    static TreeFactory* Instance();

    void Clear() {
      while(!nodes.empty()) {
        ParseNode* tmp = nodes.back();
        nodes.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!expressions.empty()) {
        Expression* tmp = expressions.back();
        expressions.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!statements.empty()) {
        Statement* tmp = statements.back();
        statements.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!declaration_lists.empty()) {
        DeclarationList* tmp = declaration_lists.back();
        declaration_lists.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }
      declaration_lists.clear();

      while(!statement_lists.empty()) {
        StatementList* tmp = statement_lists.back();
        statement_lists.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!expression_lists.empty()) {
        ExpressionList* tmp = expression_lists.back();
        expression_lists.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!calls.empty()) {
        MethodCall* tmp = calls.back();
        calls.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      while(!entries.empty()) {
        SymbolEntry* tmp = entries.back();
        entries.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }

      delete instance;
      instance = nullptr;
    }

    Alias* MakeAlias(const std::wstring& file_name, const int line_num, const int line_pos, const std::wstring& name) {
      Alias* tmp = new Alias(file_name, line_num, line_pos, name);
      nodes.push_back(tmp);
      return tmp;
    }

    Enum* MakeEnum(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, const std::wstring &name, INT64_VALUE offset) {
      Enum* tmp = new Enum(file_name, line_num, line_pos, end_line_num, end_line_pos, name, offset);
      nodes.push_back(tmp);
      return tmp;
    }

    Enum* MakeEnum(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, const std::wstring &name) {
      Enum* tmp = new Enum(file_name, line_num, line_pos, end_line_num, end_line_pos, name);
      nodes.push_back(tmp);
      return tmp;
    }
    
    EnumItem* MakeEnumItem(const std::wstring &file_name, const int line_num, const int line_pos, const std::wstring &name, Enum* e) {
      EnumItem* tmp = new EnumItem(file_name, line_num, line_pos, name, e);
      nodes.push_back(tmp);
      return tmp;
    }

    Class* MakeClass(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, const std::wstring &name,
                     const std::wstring &parent_name, std::vector<std::wstring> &interfaces, std::vector<Class*> generics, bool is_public, bool is_interface) {
      Class* tmp = new Class(file_name, line_num, line_pos, end_line_num, end_line_pos, name, parent_name, interfaces, generics, is_public, is_interface);
      nodes.push_back(tmp);
      return tmp;
    }

    Class* MakeClass(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, const std::wstring &name,
                     const std::wstring &parent_name, std::vector<std::wstring> &interfaces) {
      Class* tmp = new Class(file_name, line_num, line_pos, end_line_num, end_line_pos, name, parent_name, interfaces);
      nodes.push_back(tmp);
      return tmp;
    }

    Class* MakeClass(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, const std::wstring &name, bool is_generic) {
      Class* tmp = new Class(file_name, line_num, line_pos, end_line_num, end_line_pos, name, is_generic);
      nodes.push_back(tmp);
      return tmp;
    }
    
    Method* MakeMethod(const std::wstring &file_name, const int line_num, const int line_pos, const int mid_line_num, const int mid_line_pos, const std::wstring &name,
                       MethodType type, bool is_function, bool is_native) {
      Method* tmp = new Method(file_name, line_num, line_pos, mid_line_num, mid_line_pos, name, type, is_function, is_native);
      nodes.push_back(tmp);
      return tmp;
    }

    Method* MakeMethod(const std::wstring& file_name, const int line_num, const int line_pos, const int mid_line_num, const int mid_line_pos, const std::wstring& name) {
      Method* tmp = new Method(file_name, line_num, line_pos, mid_line_num, mid_line_pos, name);
      nodes.push_back(tmp);
      return tmp;
    }

    Lambda* MakeLambda(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Type* t, const std::wstring &n, Method* m, ExpressionList* p) {
      Lambda* tmp = new Lambda(file_name, line_num, line_pos, end_line_num, end_line_pos, t, n, m, p);
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
    
    SystemStatement* MakeSystemStatement(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, instructions::InstructionType instr) {
      SystemStatement* tmp = new SystemStatement(file_name, line_num, line_pos, end_line_num, end_line_pos, instr);
      statements.push_back(tmp);
      return tmp;
    }

    SystemStatement* MakeSystemStatement(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, instructions::Traps trap) {
      SystemStatement* tmp = new SystemStatement(file_name, line_num, line_pos, end_line_num, end_line_pos, trap);
      statements.push_back(tmp);
      return tmp;
    }

    SimpleStatement* MakeSimpleStatement(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Expression* expression) {
      SimpleStatement* tmp = new SimpleStatement(file_name, line_num, line_pos, end_line_num, end_line_pos, expression);
      statements.push_back(tmp);
      return tmp;
    }

    EmptyStatement* MakeEmptyStatement(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos) {
      EmptyStatement*  tmp = new EmptyStatement(file_name, line_num, line_pos, end_line_num, end_line_pos);
      statements.push_back(tmp);
      return tmp;
    }

    StringConcat* MakeStringConcat(std::list<Expression*> e, std::unordered_map<Expression*, Method*> ms, std::unordered_map<Expression*, LibraryMethod*> ls) {
      StringConcat* tmp = new StringConcat(e, ms, ls);
      expressions.push_back(tmp);
      return tmp;
    }
    
    Variable* MakeVariable(const std::wstring &file_name, const int line_num, const int line_pos, const std::wstring &name) {
      Variable* tmp = new Variable(file_name, line_num, line_pos, name);
      expressions.push_back(tmp);
      return tmp;
    }

    Cond* MakeCond(const std::wstring &file_name, const int line_num, const int line_pos, Expression* c, Expression* s, Expression* e) {
      Cond* tmp = new Cond(file_name, line_num, line_pos, c, s, e);
      expressions.push_back(tmp);
      return tmp;
    }

    StaticArray* MakeStaticArray(const std::wstring &file_name, const int line_num, const int line_pos, ExpressionList* exprs) {
      StaticArray* tmp = new StaticArray(file_name, line_num, line_pos, exprs);
      expressions.push_back(tmp);
      return tmp;
    }

    Declaration* MakeDeclaration(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, SymbolEntry* entry,
                                 Declaration* child, Assignment* assign) {
      Declaration* tmp = new Declaration(file_name, line_num, line_pos, end_line_num, end_line_pos, entry, child, assign);
      statements.push_back(tmp);
      return tmp;
    }

    Declaration* MakeDeclaration(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, SymbolEntry* entry, Assignment* assign) {
      Declaration* tmp = new Declaration(file_name, line_num, line_pos, end_line_num, end_line_pos, entry, nullptr, assign);
      statements.push_back(tmp);
      return tmp;
    }

    Declaration* MakeDeclaration(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, SymbolEntry* entry, Declaration* child) {
      Declaration* tmp = new Declaration(file_name, line_num, line_pos, end_line_num, end_line_pos, entry, child);
      statements.push_back(tmp);
      return tmp;
    }

    CalculatedExpression* MakeCalculatedExpression(const std::wstring &file_name, const int line_num, const int line_pos,
                                                   ExpressionType type, Expression* lhs, Expression* rhs) {
      CalculatedExpression* tmp = new CalculatedExpression(file_name, line_num, line_pos, type, lhs, rhs);
      expressions.push_back(tmp);
      return tmp;
    }

    CalculatedExpression* MakeCalculatedExpression(const std::wstring &file_name, const int line_num, const int line_pos, ExpressionType type) {
      CalculatedExpression* tmp = new CalculatedExpression(file_name, line_num, line_pos, type);
      expressions.push_back(tmp);
      return tmp;
    }

    IntegerLiteral* MakeIntegerLiteral(const std::wstring &file_name, const int line_num, const int line_pos, INT64_VALUE value) {
      IntegerLiteral* tmp = new IntegerLiteral(file_name, line_num, line_pos, value);
      expressions.push_back(tmp);
      return tmp;
    }

    FloatLiteral* MakeFloatLiteral(const std::wstring &file_name, const int line_num, const int line_pos, FLOAT_VALUE value) {
      FloatLiteral* tmp = new FloatLiteral(file_name, line_num, line_pos, value);
      expressions.push_back(tmp);
      return tmp;
    }

    CharacterLiteral* MakeCharacterLiteral(const std::wstring &file_name, const int line_num, const int line_pos, wchar_t value) {
      CharacterLiteral* tmp = new CharacterLiteral(file_name, line_num, line_pos, value);
      expressions.push_back(tmp);
      return tmp;
    }

    CharacterString* MakeCharacterString(const std::wstring &file_name, const int line_num, const int line_pos, 
                                         const std::wstring &char_string, const bool is_lit) {
      CharacterString* tmp = new CharacterString(file_name, line_num, line_pos, char_string, is_lit);
      expressions.push_back(tmp);
      return tmp;
    }

    NilLiteral* MakeNilLiteral(const std::wstring &file_name, const int line_num, const int line_pos) {
      NilLiteral* tmp = new NilLiteral(file_name, line_num, line_pos);
      expressions.push_back(tmp);
      return tmp;
    }

    BooleanLiteral* MakeBooleanLiteral(const std::wstring &file_name, const int line_num, const int line_pos, bool boolean) {
      BooleanLiteral* tmp = new BooleanLiteral(file_name, line_num, line_pos, boolean);
      expressions.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring &file_name, const int line_num, const int line_pos, 
                               const int end_line_num, const int end_line_pos, MethodCallType type,
                               const std::wstring &value, ExpressionList* exprs) {
      MethodCall* tmp = new MethodCall(file_name, line_num, line_pos, end_line_num, end_line_pos, type, value, exprs);
      calls.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring& file_name, const int line_num, const int line_pos, 
                               const int mid_line_num, const int mid_line_pos,
                               const int end_line_num, const int end_line_pos, 
                               const std::wstring& v, const std::wstring& m, ExpressionList* e) {
      MethodCall* tmp = new MethodCall(file_name, line_num, line_pos, mid_line_num, mid_line_pos, end_line_num, end_line_pos, v, m, e);
      calls.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring& file_name, const int line_num, const int line_pos,
                               const int mid_line_num, const int mid_line_pos,
                               const int end_line_num, const int end_line_pos,
                               Variable* v, const std::wstring& m, ExpressionList* e) {
      MethodCall* tmp = new MethodCall(file_name, line_num, line_pos, mid_line_num, mid_line_pos, end_line_num, end_line_pos, v, m, e);
      calls.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring& file_name, const int line_num, const int line_pos, 
                               const int end_line_num, const int end_line_pos, 
                               const std::wstring& v, const std::wstring& m) {
      MethodCall* tmp = new MethodCall(file_name, line_num, line_pos, end_line_num, end_line_pos, v, m);
      calls.push_back(tmp);
      return tmp;
    }

    MethodCall* MakeMethodCall(const std::wstring& file_name, const int line_num, const int line_pos,
                               const int end_line_num, const int end_line_pos, Variable* v) {
      MethodCall* tmp = new MethodCall(file_name, line_num, line_pos, end_line_num, end_line_pos, v);
      calls.push_back(tmp);
      return tmp;
    }

    If* MakeIf(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, 
               Expression* expression, StatementList* if_statements, If* next = nullptr) {
      If* tmp = new If(file_name, line_num, line_pos, end_line_num, end_line_pos, expression, if_statements, next);
      statements.push_back(tmp);
      return tmp;
    }

    Break* MakeBreakContinue(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, StatementType type) {
      Break* tmp = new Break(file_name, line_num, line_pos, end_line_num, end_line_pos, type);
      statements.push_back(tmp);
      return tmp;
    }

    DoWhile* MakeDoWhile(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                         Expression* expression, StatementList* stmts) {
      DoWhile* tmp = new DoWhile(file_name, line_num, line_pos, end_line_num, end_line_pos, expression, stmts);
      statements.push_back(tmp);
      return tmp;
    }

    While* MakeWhile(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                     Expression* expression, StatementList* stmts) {
      While* tmp = new While(file_name, line_num, line_pos, end_line_num, end_line_pos, expression, stmts);
      statements.push_back(tmp);
      return tmp;
    }

    For* MakeFor(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                 StatementList* pre_stmts, Expression* cond_expr, StatementList* update_stmts, StatementList* stmts) {
      For* tmp = new For(file_name, line_num, line_pos, end_line_num, end_line_pos, pre_stmts, cond_expr, update_stmts, stmts);
      statements.push_back(tmp);
      return tmp;
    }

    For* MakeFor(const std::wstring& file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                 StatementList* pre_stmts, Expression* cond_expr, StatementList* update_stmts, Assignment* bind_assignment, StatementList* stmts) {
      For* tmp = new For(file_name, line_num, line_pos, end_line_num, end_line_pos, pre_stmts, cond_expr, update_stmts, bind_assignment, stmts);
      statements.push_back(tmp);
      return tmp;
    }

    CriticalSection* MakeCriticalSection(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Variable* var, StatementList* stmts) {
      CriticalSection* tmp = new CriticalSection(file_name, line_num, line_pos, end_line_num, end_line_pos, var, stmts);
      statements.push_back(tmp);
      return tmp;
    }

    Select* MakeSelect(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Assignment* eval_assignment,
                       std::map<ExpressionList*, StatementList*> &statement_map, std::vector<StatementList*> &statement_lists, StatementList* other) {
      Select* tmp = new Select(file_name, line_num, line_pos, end_line_num, end_line_pos, eval_assignment,
                               statement_map, statement_lists, other);
      statements.push_back(tmp);
      return tmp;
    }

    Return* MakeReturn(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, Expression* expression) {
      Return* tmp = new Return(file_name, line_num, line_pos, end_line_num, end_line_pos, expression);
      statements.push_back(tmp);
      return tmp;
    }
    
    Leaving* MakeLeaving(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos, StatementList* stmts) {
      Leaving* tmp = new Leaving(file_name, line_num, line_pos, end_line_num, end_line_pos, stmts);
      statements.push_back(tmp);
      return tmp;
    }
    
    Assignment* MakeAssignment(const std::wstring &file_name, const int line_num, const int line_pos, const int end_line_num, const int end_line_pos,
                               Assignment* child, Variable* variable, Expression* expression) {
      Assignment* tmp = new Assignment(file_name, line_num, line_pos, end_line_num, end_line_pos, child, variable, expression);
      statements.push_back(tmp);
      return tmp;
    }

    Assignment* MakeAssignment(const std::wstring &file_name, const int line_num, const int line_pos, 
                               const int end_line_num, const int end_line_pos, Variable* variable, Expression* expression) {
      Assignment* tmp = new Assignment(file_name, line_num, line_pos, end_line_num, end_line_pos, variable, expression);
      statements.push_back(tmp);
      return tmp;
    }

    OperationAssignment* MakeOperationAssignment(const std::wstring &file_name, const int line_num, const int line_pos, 
                                                 const int end_line_num, const int end_line_pos, Variable* variable, 
                                                 Expression* expression, StatementType stmt_type) {
      OperationAssignment* tmp = new OperationAssignment(file_name, line_num, line_pos, end_line_num, end_line_pos, variable, expression, stmt_type);
      statements.push_back(tmp);
      return tmp;
    }

    SymbolEntry* MakeSymbolEntry(const std::wstring& file_name, const int line_num, const int line_pos, 
                                 const std::wstring& n, Type* t, bool s, bool c, bool e = false) {
      SymbolEntry* tmp = new SymbolEntry(file_name, line_num, line_pos, n, t, s, c, e);
      entries.push_back(tmp);
      return tmp;
    }
    
    SymbolEntry* MakeSymbolEntry(const std::wstring &n, Type* t, bool s, bool c, bool e = false) {
      SymbolEntry* tmp = new SymbolEntry(t ? t->GetFileName() : L"", t ? t->GetLineNumber() : -1, t ? t->GetLinePosition() : -1, n, t, s, c, e);
      entries.push_back(tmp);
      return tmp;
    }
  };

  /****************************
   * ParsedBundle class
   ****************************/
  class ParsedBundle : public ParseNode {
    std::wstring name;
    int line_num; 
    int line_pos;
    int end_line_num;
    int end_line_pos;

    SymbolTableManager* symbol_table;

    std::unordered_map<std::wstring, Alias*> aliases;
    std::vector<Alias*> aliases_list;

    std::unordered_map<std::wstring, Enum*> enums;
    std::vector<Enum*> enum_list;

    std::unordered_map<std::wstring, Class*> classes;
    std::vector<Class*> class_list;

  public:
    ParsedBundle(const std::wstring& f, const int l, const int p, std::wstring &n, SymbolTableManager *t) : ParseNode(f, l, p) {
      name = n;
      symbol_table = t;
      line_num = l;
      line_pos = p;
    }

    ~ParsedBundle() {
      delete symbol_table;
      symbol_table = nullptr;
    }
    
    inline int GetEndLineNumber() {
      return end_line_num;
    }

    inline int GetEndLinePosition() {
      return end_line_pos;
    }

    void SetEndLineNumber(int el) {
      end_line_num = el;
    }

    void SetEndLinePosition(int ep) {
      end_line_pos = ep;
    }

    const std::wstring GetName() const {
      return name;
    }

    void AddEnum(Enum* e) {
      enums.insert(std::pair<std::wstring, Enum*>(e->GetName(), e));
      enum_list.push_back(e);
    }
    
    Enum* GetEnum(const std::wstring &e) {
      std::unordered_map<std::wstring, Enum*>::iterator result = enums.find(e);
      if(result != enums.end()) {
        return result->second;
      }

      return nullptr;
    }

    void AddLambdas(Alias* a) {
      aliases.insert(std::pair<std::wstring, Alias*>(a->GetName(), a));
      aliases_list.push_back(a);
    }

    Alias* GetAlias(const std::wstring& a) {
      std::unordered_map<std::wstring, Alias*>::iterator result = aliases.find(a);
      if(result != aliases.end()) {
        return result->second;
      }

      return nullptr;
    }

    void AddClass(Class* cls) {
      classes.insert(std::pair<std::wstring, Class*>(cls->GetName(), cls));
      class_list.push_back(cls);
    }

    Class* GetClass(const std::wstring &n) {
      std::unordered_map<std::wstring, Class*>::iterator result = classes.find(n);
      if(result != classes.end()) {
        return result->second;
      }

      return nullptr;
    }

    const std::vector<Alias*> GetAliases() {
      return aliases_list;
    }

    const std::vector<Enum*> GetEnums() {
      return enum_list;
    }

    const std::vector<Class*> GetClasses() {
      return class_list;
    }

    SymbolTableManager* GetSymbolTableManager() {
      return symbol_table;
    }
  };

  struct int_string_comp {
    bool operator() (IntStringHolder* lhs, IntStringHolder* rhs) const {
      return std::tie(lhs->length, lhs->value) < std::tie(rhs->length, rhs->value);
    }
  };

  struct byte_string_comp {
    bool operator() (ByteStringHolder* lhs, ByteStringHolder* rhs) const {
      return std::tie(lhs->length, lhs->value) < std::tie(rhs->length, rhs->value);
    }
  };

  struct float_string_comp {
    bool operator() (FloatStringHolder* lhs, FloatStringHolder* rhs) const {
      return std::tie(lhs->length, lhs->value) < std::tie(rhs->length, rhs->value);
    }
  };

  struct bool_string_comp {
    bool operator() (BoolStringHolder* lhs, BoolStringHolder* rhs) const {
      return std::tie(lhs->length, lhs->value) == std::tie(rhs->length, rhs->value);
    }
  };

  /****************************
   * ParsedProgram class
   ****************************/
  class ParsedProgram {
    std::map<IntStringHolder*, int, int_string_comp> int_string_ids;
    std::vector<IntStringHolder*> int_strings;

    std::map<FloatStringHolder*, int, float_string_comp> float_string_ids;
    std::vector<FloatStringHolder*> float_strings;

    std::map<BoolStringHolder*, int, bool_string_comp> bool_string_ids;
    std::vector<BoolStringHolder*> bool_strings;

    std::map<ByteStringHolder*, int, byte_string_comp> byte_string_ids;
    std::vector<ByteStringHolder*> byte_strings;

    std::map<std::wstring, int> char_string_ids;
    std::vector<std::wstring> char_strings;
    
    std::map<std::wstring, std::vector<std::wstring>> lib_use_mapping;
    std::vector<std::wstring> lib_use_names;
    std::vector<std::wstring> tiered_use_names;

    std::map<std::wstring, std::vector<Type*>> lib_static_mapping;

    std::vector<ParsedBundle*> bundles;
    std::vector<std::wstring> bundle_names;
    Class* start_class;
    Method* start_method;
    Linker* linker; // deleted elsewhere
#ifdef _DIAG_LIB
    std::vector<std::wstring> error_strings;
    std::vector<std::wstring> warning_strings;
#endif

  public:
    ParsedProgram() {
      linker = nullptr;
      start_class = nullptr;
      start_method = nullptr;
    }

    ~ParsedProgram() {
      // clean up
      while(!bundles.empty()) {
        ParsedBundle* tmp = bundles.back();
        bundles.pop_back();
        // delete
        delete tmp;
        tmp = nullptr;
      }
      
      // clear factories
      TreeFactory::Instance()->Clear();
      TypeFactory::Instance()->Clear();
    }

#ifdef _DIAG_LIB
    const std::vector<std::wstring> GetErrorStrings() {
      return error_strings;
    }

    const void SetErrorStrings(std::vector<std::wstring> msgs) {
      error_strings = msgs;
    }
    
    const std::vector<std::wstring> GetWarningStrings() {
      return warning_strings;
    }
    
    const void SetWarningStrings(std::vector<std::wstring> msgs) {
      warning_strings = msgs;
    }

    bool FindMethodOrClass(const std::wstring uri, const int line_num, Class* &found_klass, Method* &found_method, SymbolTable*& table);

#endif
    
    std::wstring GetFileName() const {
      if(lib_use_mapping.size() > 0) {
        return lib_use_mapping.begin()->first;
      }
      
      return L"unknown";
    }

    void AddUse(const std::wstring &u, const std::wstring &f) {
      std::vector<std::wstring> lib_uses;
      lib_uses.push_back(u);

      std::vector<Type*> static_uses;
      AddUses(lib_uses, static_uses, f);
    }

    void AddUses(std::vector<std::wstring> &u, std::vector<Type*> &s, const std::wstring &f) {
      for(size_t i = 0; i < u.size(); ++i) {
        std::vector<std::wstring>::iterator found = find(lib_use_names.begin(), lib_use_names.end(), u[i]);
        if(found == lib_use_names.end()) {
          lib_use_names.push_back(u[i]);
        }
      }

      lib_use_mapping[f] = u;
      lib_static_mapping[f] = s;
    }

    bool HasBundleName(const std::wstring &name) {
      if(name.back() != L'#') {
        std::vector<std::wstring>::iterator found = find(bundle_names.begin(), bundle_names.end(), name);
        return found != bundle_names.end();
      }

      return true;
    }

    const std::vector<std::wstring> GetLibUses();

    const std::vector<std::wstring> GetLibUses(const std::wstring &f) {
      return lib_use_mapping[f];
    }

    const std::vector<Type*> GetStaticUses(const std::wstring& f) {
      return lib_static_mapping[f];
    }

    void AddBundle(ParsedBundle* b) {
      bundle_names.push_back(b->GetName());
      bundles.push_back(b);
    }

    ParsedBundle* GetBundle(const std::wstring n) {
      for(size_t i = 0; i < bundles.size(); ++i) {
        ParsedBundle* bundle = bundles[i];
        if(bundle->GetName() == n) {
          return bundle;
        }
      }

      return nullptr;
    }

    const std::vector<ParsedBundle*> GetBundles() {
      return bundles;
    }

    void AddIntString(std::vector<Expression*> &int_elements, int id) {
      INT64_VALUE* int_array = new INT64_VALUE[int_elements.size()];
      for(size_t i = 0; i < int_elements.size(); ++i) {
        int_array[i] = static_cast<IntegerLiteral*>(int_elements[i])->GetValue();
      }

      IntStringHolder* holder = new IntStringHolder;
      holder->value = int_array;
      holder->length = (int)int_elements.size();

      int_string_ids.insert(std::pair<IntStringHolder*, int>(holder, id));
      int_strings.push_back(holder);
    }

    int GetIntStringId(std::vector<Expression*> &int_elements) {
      INT64_VALUE* int_array = new INT64_VALUE[int_elements.size()];
      for(size_t i = 0; i < int_elements.size(); ++i) {
        int_array[i] = static_cast<IntegerLiteral*>(int_elements[i])->GetValue();
      }

      IntStringHolder* holder = new IntStringHolder;
      holder->value = int_array;
      holder->length = (int)int_elements.size();

      std::map<IntStringHolder*, int, int_string_comp>::iterator result = int_string_ids.find(holder);
      if(result != int_string_ids.end()) {
        delete[] holder->value;
        holder->value = nullptr;  
        delete holder;
        holder = nullptr;

        return result->second;
      }

      delete[] holder->value;
      holder->value = nullptr;
      delete holder;
      holder = nullptr;

      return -1;
    }

    std::vector<IntStringHolder*> GetIntStrings() {
      return int_strings;
    }

    void AddFloatString(std::vector<Expression*> &float_elements, int id) {
      FLOAT_VALUE* float_array = new FLOAT_VALUE[float_elements.size()];
      for(size_t i = 0; i < float_elements.size(); ++i) {
        float_array[i] = static_cast<FloatLiteral*>(float_elements[i])->GetValue();
      }

      FloatStringHolder* holder = new FloatStringHolder;
      holder->value = float_array;
      holder->length = (int)float_elements.size();

      float_string_ids.insert(std::pair<FloatStringHolder*, int>(holder, id));
      float_strings.push_back(holder);
    }

    void AddBoolString(std::vector<Expression*>& bool_elements, int id) {
      bool* bool_array = new bool[bool_elements.size()];
      for(size_t i = 0; i < bool_elements.size(); ++i) {
        bool_array[i] = static_cast<BooleanLiteral*>(bool_elements[i])->GetValue();
      }

      BoolStringHolder* holder = new BoolStringHolder;
      holder->value = bool_array;
      holder->length = (int)bool_elements.size();

      bool_string_ids.insert(std::pair<BoolStringHolder*, int>(holder, id));
      bool_strings.push_back(holder);
    }

    int GetBoolStringId(std::vector<Expression*>& Bool_elements) {
      bool* bool_array = new bool[Bool_elements.size()];
      for(size_t i = 0; i < Bool_elements.size(); ++i) {
        bool_array[i] = static_cast<BooleanLiteral*>(Bool_elements[i])->GetValue();
      }

      BoolStringHolder* holder = new BoolStringHolder;
      holder->value = bool_array;
      holder->length = (int)Bool_elements.size();

      std::map<BoolStringHolder*, int, bool_string_comp>::iterator result = bool_string_ids.find(holder);
      if(result != bool_string_ids.end()) {
        delete[] holder->value;
        holder->value = nullptr;
        delete holder;
        holder = nullptr;

        return result->second;
      }

      delete[] holder->value;
      holder->value = nullptr;
      delete holder;
      holder = nullptr;

      return -1;
    }

    std::vector<BoolStringHolder*> GetBoolStrings() {
      return bool_strings;
    }










    void AddByteString(std::vector<Expression*>& byte_elements, int id) {
      char* byte_array = new char[byte_elements.size()];
      for(size_t i = 0; i < byte_elements.size(); ++i) {
        byte_array[i] = (char)static_cast<IntegerLiteral*>(byte_elements[i])->GetValue();
      }

      ByteStringHolder* holder = new ByteStringHolder;
      holder->value = byte_array;
      holder->length = (int)byte_elements.size();

      byte_string_ids.insert(std::pair<ByteStringHolder*, int>(holder, id));
      byte_strings.push_back(holder);
    }

    int GetByteStringId(std::vector<Expression*>& byte_elements) {
      char* byte_array = new char[byte_elements.size()];
      for(size_t i = 0; i < byte_elements.size(); ++i) {
        byte_array[i] = (char)static_cast<IntegerLiteral*>(byte_elements[i])->GetValue();
      }

      ByteStringHolder* holder = new ByteStringHolder;
      holder->value = byte_array;
      holder->length = (int)byte_elements.size();

      std::map<ByteStringHolder*, int, byte_string_comp>::iterator result = byte_string_ids.find(holder);
      if(result != byte_string_ids.end()) {
        delete[] holder->value;
        holder->value = nullptr;
        delete holder;
        holder = nullptr;

        return result->second;
      }

      delete[] holder->value;
      holder->value = nullptr;
      delete holder;
      holder = nullptr;

      return -1;
    }

    std::vector<ByteStringHolder*> GetByteStrings() {
      return byte_strings;
    }










    int GetFloatStringId(std::vector<Expression*> &float_elements) {
      FLOAT_VALUE* float_array = new FLOAT_VALUE[float_elements.size()];
      for(size_t i = 0; i < float_elements.size(); ++i) {
        float_array[i] = static_cast<FloatLiteral*>(float_elements[i])->GetValue();
      }

      FloatStringHolder* holder = new FloatStringHolder;
      holder->value = float_array;
      holder->length = (int)float_elements.size();

      std::map<FloatStringHolder*, int, float_string_comp>::iterator result = float_string_ids.find(holder);
      if(result != float_string_ids.end()) {
        delete[] holder->value;
        holder->value = nullptr;  
        delete holder;
        holder = nullptr;

        return result->second;
      }

      delete[] holder->value;
      holder->value = nullptr;    
      delete holder;
      holder = nullptr;

      return -1;
    }

    std::vector<FloatStringHolder*> GetFloatStrings() {
      return float_strings;
    }

    void AddCharString(const std::wstring &s, int id) {
      char_string_ids.insert(std::pair<std::wstring, int>(s, id));
      char_strings.push_back(s);
    }

    int GetCharStringId(const std::wstring &s) {
      std::map<std::wstring, int>::iterator result = char_string_ids.find(s);
      if(result != char_string_ids.end()) {
        return result->second;
      }

      return -1;
    }

    std::vector<std::wstring> GetCharStrings() {
      return char_strings;
    }

    Class* GetClass(const std::wstring &n) {
      for(size_t i = 0; i < bundles.size(); ++i) {
        Class* klass = bundles[i]->GetClass(n);
        if(klass) {
          return klass;
        }
      }

      return nullptr;
    }

    Enum* GetEnum(const std::wstring &n) {
      for(size_t i = 0; i < bundles.size(); ++i) {
        Enum* e = bundles[i]->GetEnum(n);
        if(e) {
          return e;
        }
      }

      return nullptr;
    }

    Alias* GetAlias(const std::wstring& n) {
      for(size_t i = 0; i < bundles.size(); ++i) {
        Alias* a = bundles[i]->GetAlias(n);
        if(a) {
          return a;
        }
      }

      return nullptr;
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
