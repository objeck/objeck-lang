/***************************************************************************
 * Performs contextual analysis.
 *
 * Copyright (c) 2008-2019, Randy Hollines
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

#ifndef __CONTEXT_H__
#define __CONTEXT_H__

#include "linker.h"
#include "types.h"
#include "tree.h"

#define SYSTEM_BASE_NAME L"System.Base" 

using namespace frontend;

/****************************
 * Support for inferred method
 * signatures
 ****************************/
class LibraryMethodCallSelection {
  LibraryMethod* method;
  vector<int> parm_matches; 

 public:
  LibraryMethodCallSelection(LibraryMethod* m) {
    method = m;
  }

  ~LibraryMethodCallSelection() {
  }

  bool IsValid() {
    for(size_t i = 0; i < parm_matches.size(); ++i) {
      if(parm_matches[i] < 0) {
        return false;
      }
    }

    return true;
  }

  void AddParameterMatch(int p) {
    parm_matches.push_back(p);
  }

  vector<int> GetParameterMatches() {
    return parm_matches;
  }

  LibraryMethod* GetLibraryMethod() {
    return method;
  }
};

class LibraryMethodCallSelector {
  MethodCall* method_call;
  vector<LibraryMethodCallSelection*> matches;
  vector<LibraryMethodCallSelection*> valid_matches;

 public: 
  LibraryMethodCallSelector(MethodCall* c, vector<LibraryMethodCallSelection*> &m) {
    method_call = c;
    matches = m;

    // weed out invalid matches     
    for(size_t i = 0; i < matches.size(); ++i) {
      if(matches[i]->IsValid()) {
        valid_matches.push_back(matches[i]);
      }
    }
  }

  ~LibraryMethodCallSelector() {
    while(!matches.empty()) {
      LibraryMethodCallSelection* tmp = matches.front();
      matches.erase(matches.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }
  }

  vector<wstring> GetAlternativeMethodNames() {
    vector<wstring> alt_names;
    for(size_t i = 0; i < matches.size(); ++i) {
      alt_names.push_back(matches[i]->GetLibraryMethod()->GetUserName());
    }

    return alt_names;
  }

  LibraryMethod* GetSelection();
};

/****************************
 * Support for inferred method
 * signatures
 ****************************/
class MethodCallSelection {
  Method* method;
  vector<int> parm_matches; 

 public:
  MethodCallSelection(Method* m) {
    method = m;
  }

  ~MethodCallSelection() {
  }

  bool IsValid() {
    for(size_t i = 0; i < parm_matches.size(); ++i) {
      if(parm_matches[i] < 0) {
        return false;
      }
    }

    return true;
  }

  void AddParameterMatch(int p) {
    parm_matches.push_back(p);
  }

  vector<int> GetParameterMatches() {
    return parm_matches;
  }

  Method* GetMethod() {
    return method;
  }
};

class MethodCallSelector {
  MethodCall* method_call;
  vector<MethodCallSelection*> matches;
  vector<MethodCallSelection*> valid_matches;

 public: 
  MethodCallSelector(MethodCall* c, vector<MethodCallSelection*> &m) {
    method_call = c;
    matches = m;

    // weed out invalid matches
    for(size_t i = 0; i < matches.size(); ++i) {
      if(matches[i]->IsValid()) {
        valid_matches.push_back(matches[i]);
      }
    }
  }

  ~MethodCallSelector() {
    while(!matches.empty()) {
      MethodCallSelection* tmp = matches.front();
      matches.erase(matches.begin());
      // delete
      delete tmp;
      tmp = nullptr;
    }
  }

  vector<wstring> GetAlternativeMethodNames() {
    vector<wstring> alt_names;
    for(size_t i = 0; i < matches.size(); ++i) {
      alt_names.push_back(matches[i]->GetMethod()->GetUserName());
    }

    return alt_names;
  }

  Method* GetSelection() {
    // no match
    if(valid_matches.size() == 0) {
      return nullptr;
    }
    // single match
    else if(valid_matches.size() == 1) {
      return valid_matches[0]->GetMethod();
    }

    int match_index = -1;
    int high_score = 0;
    for(size_t i = 0; i < matches.size(); ++i) {
      // calculate match score
      int match_score = 0;
      bool exact_match = true;
      vector<int> parm_matches = matches[i]->GetParameterMatches();
      for(size_t j = 0; exact_match && j < parm_matches.size(); ++j) {
        if(parm_matches[j] == 0) {
          match_score++;
        }
        else {
          exact_match = false;
        }
      }
      // save the index of the best match
      if(match_score >  high_score) {
        match_index = (int)i;
        high_score = match_score;
      }
    }

    if(match_index == -1) {
      return nullptr;
    }

    return matches[match_index]->GetMethod();    
  }
};

/****************************
 * Performs contextual analysis
 ****************************/
class ContextAnalyzer {
  ParsedProgram* program;
  ParsedBundle* bundle;
  Linker* linker;
  Class* current_class;
  Method* current_method;
  SymbolTable* current_table;
  SymbolTableManager* symbol_table;
  map<int, wstring> errors;
  vector<wstring> alt_error_method_names;
  map<const wstring, EntryType> type_map;
  bool main_found;
  bool web_found;
  bool is_lib;
  bool is_web;
  int char_str_index;
  int int_str_index;
  int float_str_index;
  int in_loop;
  vector<Class*> anonymous_classes;

  inline void Debug(const wstring &msg, const int line_num, int depth) {
    GetLogger() << setw(4) << line_num << L": ";
    for(int i = 0; i < depth; ++i) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << endl;
  }

  // returns true if expression is not an array
  bool IsScalar(Expression* expression, bool check_last = true);

  // returns true if expression is of boolean type
  bool IsBooleanExpression(Expression* expression);

  // returns true if expression is of boolean type
  bool IsEnumExpression(Expression* expression);

  // returns true if expression is of integer or enum type
  bool IsIntegerExpression(Expression* expression);

  // returns true if entry static context is not valid
  bool DuplicateParentEntries(SymbolEntry* entry, Class* klass);

  // returns true if this entry is duplicated in parent classes
  inline bool InvalidStatic(SymbolEntry* entry) {
    return current_method->IsStatic() && !entry->IsLocal() && !entry->IsStatic();
  }

  // returns true if a duplicate value is found in the list
  bool DuplicateCaseItem(map<int, StatementList*>label_statements, int value);

  // returns true if method static context is not valid
  bool InvalidStatic(MethodCall* method_call, Method* method);

  // returns true if method static context is not valid
  bool InvalidStatic(MethodCall* method_call, LibraryMethod* method);

  // returns a symbol table entry by name
  SymbolEntry* GetEntry(wstring name, bool is_parent = false);

  // returns a symbol table entry by name for a given method
  SymbolEntry* GetEntry(MethodCall* method_call, const wstring &variable_name, int depth);

  // returns a type expression
  Type* GetExpressionType(Expression* expression, int depth);

  // checks for a valid downcast
  bool ValidDownCast(const wstring &cls_name, Class* class_tmp, LibraryClass* lib_class_tmp);

  // checks for a valid upcast
  bool ValidUpCast(const wstring &to, Class* from_klass);

  // checks for a valid upcast
  bool ValidUpCast(const wstring &to, LibraryClass* from_klass);

  // helper function for program class search
  bool GetProgramLibraryClass(const wstring &n, Class* &klass, LibraryClass* &lib_klass);

  // string encodes type
  const wstring EncodeType(Type* type);

  // resolves type reference for class or enum
  inline bool ResolveClassEnumType(Type* type) {
    return ResolveClassEnumType(type, current_class);
  }
  
  // resolves type reference for class or enum
  bool ResolveClassEnumType(Type* type, Class* context_klass);

  // helper function for method call method matching
  bool IsClassEnumParameterMatch(Type* calling_type, Type* method_type);
  
  // resolve enum reference
  void ResolveEnumCall(LibraryEnum* lib_eenum, const wstring &item_name, MethodCall* method_call);
  
  // validate character string
  void AnalyzeCharacterStringVariable(SymbolEntry* entry, CharacterString* char_str, int depth);

  // validate variable cast
  void AnalyzeVariableCast(Type* to_type, Expression* expression);

  // validate parameters for dynamic function
  void AnalyzeDynamicFunctionParameters(Type* func_type, ParseNode* node) {
    AnalyzeDynamicFunctionParameters(func_type, node, current_class);
  }

  // validate parameters for dynamic function
  void AnalyzeDynamicFunctionParameters(Type* func_type, ParseNode* node, Class* klass);

  // add method parameter
  void AddMethodParameter(MethodCall* method_call, SymbolEntry* entry, int depth);

  // validate method call with generics
  Type* RelsolveGenericCall(Type* left, MethodCall* method_call, Class* klass, Method* method, int depth);

  // validate method call with generics
  Type* RelsolveGenericCall(Type* left, MethodCall* method_call, LibraryClass* klass, LibraryMethod* method, int depth);

  // validate generic references against types
  void CheckGenericParameters(const vector<LibraryClass*> generic_klasses, const vector<Type*> concrete_types, ParseNode* node);

  // validate generic references against types
  void CheckGenericParameters(const vector<Class*> generic_klasses, const vector<Type*> concrete_types, ParseNode* node);

  // find generic references
  bool HasGenericClass(const wstring& n);

  // finds the first class match; note multiple matches may exist
  inline Class* SearchProgramClasses(const wstring& klass_name) {
    Class* klass = program->GetClass(klass_name);
    if(!klass) {
      klass = program->GetClass(bundle->GetName() + L"." + klass_name);
      if(!klass) {
        vector<wstring> uses = program->GetUses();
        for(size_t i = 0; !klass && i < uses.size(); ++i) {
          klass = program->GetClass(uses[i] + L"." + klass_name);
        }
      }
    }

    return klass;
  }

  // finds the first enum match; note multiple matches may exist
  inline Enum* SearchProgramEnums(const wstring & eenum_name) {
    Enum* eenum = program->GetEnum(eenum_name);
    if(!eenum) {
      eenum = program->GetEnum(bundle->GetName() + L"." + eenum_name);
      if(!eenum) {
        vector<wstring> uses = program->GetUses();
        for(size_t i = 0; !eenum && i < uses.size(); ++i) {
          eenum = program->GetEnum(uses[i] + L"." + eenum_name);
          if(!eenum) {
            eenum = program->GetEnum(uses[i] + eenum_name);
          }
        }
      }
    }

    return eenum;
  }

  // helper function for program enum search
  inline bool HasProgramLibraryEnum(const wstring & n) {
    return SearchProgramEnums(n) || linker->SearchEnumLibraries(n, program->GetUses(current_class->GetFileName()));
  }

  // helper function for program class search
  inline bool HasProgramLibraryClass(const wstring & n) {
    return SearchProgramClasses(n) || linker->SearchClassLibraries(n, program->GetUses(current_class->GetFileName()));
  }

  // string utility functions
  wstring ToString(int v) {
    wostringstream str;
    str << v;
    return str.str();
  }

  wstring ReplaceSubstring(wstring s, const wstring& f, const wstring& r) {
    const size_t index = s.find(f);
    if(index != string::npos) {
      s.replace(index, f.size(), r);
    }

    return s;
  }

  void ReplaceAllSubstrings(wstring& str, const wstring& from, const wstring& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != wstring::npos) {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length();
    }
  }

  // error processing
  void ProcessError(ParseNode* n, const wstring &msg);
  void ProcessErrorAlternativeMethods(wstring &message);
  void ProcessError(const wstring &fn, const wstring &msg);
  bool CheckErrors();

  // context operations
  void AnalyzeEnum(Enum* eenum, const int depth);
  void AnalyzeClass(Class* klass, const int id, const int depth);
  void AnalyzeDuplicateEntries(vector<Class*>& classes, const int depth);
  void AddDefaultParameterMethods(ParsedBundle* bundle, Class* klass, Method* method);
  void GenerateParameterMethods(ParsedBundle* bundle, Class* klass, Method* method);
  void AnalyzeMethods(Class* klass, const int depth);
  bool AnalyzeVirtualMethods(Class* impl_class, Class* lib_parent, const int depth);
  bool AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_parent, const int depth);
  void AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
                            bool impl_is_static, bool impl_is_virtual, Method* virtual_method);
  void AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
                            bool impl_is_static, bool impl_is_virtual, LibraryMethod* virtual_method);
  void AnalyzeInterfaces(Class* klass, const int depth);
  bool AnalyzeReturnPaths(StatementList* statement_list, const int depth);
  bool AnalyzeReturnPaths(If* if_stmt, bool nested, const int depth);
  bool AnalyzeReturnPaths(Select* select_stmt, const int depth);
  void AnalyzeMethod(Method* method, int id, const int depth);
  void AnalyzeStatements(StatementList* statements, const int depth);
  void AnalyzeStatement(Statement* statement, const int depth);
  void AnalyzeIndices(ExpressionList* indices, const int depth);
  void AnalyzeExpressions(ExpressionList* parameters, const int depth);
  void AnalyzeExpression(Expression* expression, const int depth);
  void AnalyzeVariable(Variable* variable, SymbolEntry* entry, const int depth);
  void AnalyzeVariable(Variable* variable, const int depth);
  void AnalyzeCharacterString(CharacterString* char_str, const int depth);
  void AnalyzeConditional(Cond* conditional, const int depth);
  void AnalyzeStaticArray(StaticArray* array, const int depth);
  void AnalyzeCast(Expression* expression, const int depth);
  void AnalyzeClassCast(Type* left, Expression* expression, const int depth);
  void AnalyzeClassCast(Type* left, Type* right, Expression* expression, bool generic_check, const int depth);
  void AnalyzeAssignment(Assignment* assignment, StatementType type, const int depth);
  void AnalyzeSimpleStatement(SimpleStatement* simple, const int depth);
  void AnalyzeIf(If* if_stmt, const int depth);
  void AnalyzeDoWhile(DoWhile* do_while_stmt, const int depth);
  void AnalyzeWhile(While* while_stmt, const int depth);
  void AnalyzeSelect(Select* select_stmt, const int depth);
  void AnalyzeCritical(CriticalSection* mutex, const int depth);
  void AnalyzeFor(For* for_stmt, const int depth);
  void AnalyzeReturn(Return* rtrn, const int depth);
  void AnalyzeLeaving(Leaving* leaving_stmt, const int depth);
  Expression* AnalyzeRightCast(Variable* variable, Expression* expression, bool is_scalar, const int depth);
  Expression* AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, const int depth);
  Expression* AnalyzeRightCast(Type* left, Type* right, Expression* expression, bool is_scalar, const int depth);
  Expression* BoxExpression(Type* type, Expression* expression, int depth);
  Expression* UnboxingExpression(Type* right, Expression* expression, int depth);
  void AnalyzeCalculation(CalculatedExpression* expression, const int depth);
  void AnalyzeCalculationCast(CalculatedExpression* expression, const int depth);
  bool UnboxingCalculation(Type* type, Expression* expression, const int depth, CalculatedExpression* calc_expression, bool set_left);
  void AnalyzeDeclaration(Declaration * declaration, Class * klass, const int depth);
  // checks for method calls, which includes new array and object allocation
  void AnalyzeExpressionMethodCall(Expression* expression, const int depth);
  bool AnalyzeExpressionMethodCall(SymbolEntry* entry, wstring &encoding,
                                   Class* &klass, LibraryClass* &lib_klass);
  bool AnalyzeExpressionMethodCall(Expression* expression, wstring &encoding,
                                   Class* &klass, LibraryClass* &lib_klass, bool &is_enum_call);
  bool AnalyzeExpressionMethodCall(Type* type, const int dimension, wstring &encoding,
                                   Class* &klass, LibraryClass* &lib_klass, bool &is_enum_call);
  void AnalyzeMethodCall(MethodCall* method_call, const int depth);
  void AnalyzeNewArrayCall(MethodCall* method_call, const int depth);
  void AnalyzeParentCall(MethodCall* method_call, const int depth);
  LibraryClass* AnalyzeLibraryMethodCall(MethodCall* method_call, wstring &encoding, const int depth);
  Class* AnalyzeProgramMethodCall(MethodCall* method_call, wstring &encoding, const int depth);
  void ResolveConcreteTypes(vector<Type*> concretes, ParseNode* node, const int depth);
  void AnalyzeMethodCall(Class* klass, MethodCall* method_call,
                         bool is_expr, wstring &encoding, const int depth);
  void AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call,
                         bool is_expr, wstring &encoding, bool is_parent, const int depth);
  void AnalyzeMethodCall(LibraryMethod* lib_method, MethodCall* method_call,
                         bool is_virtual, bool is_expr, const int depth);
  wstring EncodeMethodCall(ExpressionList* calling_params, const int depth);
  Method* ResolveMethodCall(Class* klass, MethodCall* method_call, const int depth);
  LibraryMethod* ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, const int depth);
  int MatchCallingParameter(Expression* calling_param, Type* method_type,
                            Class* klass, LibraryClass* lib_klass, const int depth);
  wstring EncodeFunctionType(vector<Type*> func_params, Type* func_rtrn);
  wstring EncodeFunctionReference(ExpressionList* calling_params, const int depth);
  void AnalyzeDynamicFunctionCall(MethodCall* method_call, const int depth);
  void AnalyzeFunctionReference(Class* klass, MethodCall* method_call,
                                wstring &encoding, const int depth);
  void AnalyzeFunctionReference(LibraryClass* klass, MethodCall* method_call,
                                wstring &encoding, const int depth);
  Type* RelsolveGenericType(Type* generic_type, MethodCall* method_call, Class* klass, 
                            LibraryClass* lib_klass);

 public:
  ContextAnalyzer(ParsedProgram* p, wstring lib_path, bool l, bool w) {
    program = p;
    is_lib = l;
    is_web = w;
    main_found = web_found = false;
    // initialize linker
    linker = new Linker(lib_path);
    program->SetLinker(linker);
    char_str_index = int_str_index = float_str_index= 0;
    in_loop = 0;
    
    // setup type map
    type_map[L"$Byte"] = BYTE_TYPE;
    type_map[L"$Char"] = CHAR_TYPE;
    type_map[L"$Int"] = INT_TYPE;
    type_map[L"$Float"] = FLOAT_TYPE;
    type_map[L"$Bool"] = BOOLEAN_TYPE;
  }

  ~ContextAnalyzer() {
  }

  bool Analyze();
};

#endif