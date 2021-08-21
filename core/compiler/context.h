/***************************************************************************
 * Performs contextual analysis.
 *
 * Copyright (c) 2008-2021, Randy Hollines
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
 * Library method call resolution
 ****************************/
class LibraryMethodCallSelection {
  LibraryMethod* method;
  vector<Expression*> boxed_params;
  vector<int> parm_matches; 

 public:
  LibraryMethodCallSelection(LibraryMethod* m, vector<Expression*>& b) {
    method = m;
    boxed_params = b;
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

  const vector<Expression*> GetCallingParameters() {
    return boxed_params;
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

  vector<LibraryMethod*> GetAlternativeMethods() {
    vector<LibraryMethod*> alt_mthds;

    for(size_t i = 0; i < matches.size(); ++i) {
      alt_mthds.push_back(matches[i]->GetLibraryMethod());
    }

    return alt_mthds;
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
 * Method call resolution
 ****************************/
class MethodCallSelection {
  Method* method;
  vector<Expression*> boxed_params;
  vector<int> parm_matches; 

 public:
  MethodCallSelection(Method* m, vector<Expression*> &b) {
    method = m;
    boxed_params = b;
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

  const vector<Expression*> GetCallingParameters() {
    return boxed_params;
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

  vector<Method*> GetAlternativeMethods() {
    vector<Method*> alt_mthds;

    for(size_t i = 0; i < matches.size(); ++i) {
      alt_mthds.push_back(matches[i]->GetMethod());
    }

    return alt_mthds;
  }

  vector<wstring> GetAlternativeMethodNames() {
    vector<wstring> alt_names;
    for(size_t i = 0; i < matches.size(); ++i) {
      alt_names.push_back(matches[i]->GetMethod()->GetUserName());
    }

    return alt_names;
  }

  Method* GetSelection();
};

/****************************
 * Trees to decorated trees
 ****************************/
class ContextAnalyzer {
  ParsedProgram* program;
  ParsedBundle* bundle;
  Linker* linker;
  Class* current_class;
  Method* current_method;
  Method* capture_method;
  SymbolTable* current_table;
  SymbolTable* capture_table;
  SymbolTableManager* symbol_table;
  Lambda* capture_lambda;
  pair<Lambda*, MethodCall*> lambda_inferred;
  map<int, wstring> errors;
  vector<wstring> alt_error_method_names;
  map<const wstring, EntryType> type_map;
  unordered_set<wstring> holder_types;
  bool main_found;
  bool web_found;
  bool is_lib;
  bool is_web;
  int char_str_index;
  int int_str_index;
  int float_str_index;
  int in_loop;
  vector<Class*> anonymous_classes;
#ifdef _DIAG_LIB
  vector<wstring> error_strings;
  vector<Expression*> diagnostic_expressions;
#endif

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

  // string encodes type
  const wstring EncodeType(Type* type);

  // resolves type reference for class or enum
  inline bool ResolveClassEnumType(Type* type) {
    return ResolveClassEnumType(type, current_class);
  }

  // formats a class type string
  wstring FormatTypeString(const wstring name);
  
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
  void AnalyzeVariableFunctionParameters(Type* func_type, ParseNode* node) {
    AnalyzeVariableFunctionParameters(func_type, node, current_class);
  }

  // validate parameters for dynamic function
  void AnalyzeVariableFunctionParameters(Type* func_type, ParseNode* node, Class* klass);

  // add method parameter
  void AddMethodParameter(MethodCall* method_call, SymbolEntry* entry, int depth);

  // resolve generic type
  Type* ResolveGenericType(Type* generic_type, MethodCall* method_call, Class* klass, LibraryClass* lib_klass, bool is_rtrn);

  Type* ResolveGenericType(Type* type, Expression* expression, Class* left_klass, LibraryClass* lib_left_klass);

  // validates mapping of generic to concrete types
  void ValidateGenericConcreteMapping(const vector<Type*> concrete_types, LibraryClass* lib_klass, ParseNode* node);

  void ValidateGenericConcreteMapping(const vector<Type*> concrete_types, Class* klass, ParseNode* node);
  
  // validates the backing class for a generic deceleration
  void ValidateGenericBacking(Type* type, const wstring backing_inf_name, Expression * expression);

  // validate concrete type
  void ValidateConcrete(Type* cls_type, Type* concrete_type, ParseNode* node, const int depth);

  // finds the first class match; note multiple matches may exist
  Class* SearchProgramClasses(const wstring &klass_name);

  // finds the first enum match; note multiple matches may exist
  Enum* SearchProgramEnums(const wstring &eenum_name);

  inline vector<Type*> GetConcreteTypes(MethodCall* method_call) {
    if(!method_call->GetConcreteTypes().empty()) {
      return method_call->GetConcreteTypes();
    }
    else if(method_call->GetEvalType() && method_call->GetEntry()) {
      return method_call->GetEvalType()->GetGenerics();
    }
    else if(method_call->GetEvalType()) {
      return method_call->GetEvalType()->GetGenerics();
    }
    else if(method_call->GetEntry()) {
      return method_call->GetEntry()->GetType()->GetGenerics();
    }

    return vector<Type*>();
  }
  
  // helper function for program enum searches
  inline bool HasProgramLibraryEnum(const wstring &n) {
    return SearchProgramEnums(n) || linker->SearchEnumLibraries(n, program->GetUses(current_class->GetFileName()));
  }

  // helper function for program class searches
  inline bool HasProgramLibraryClass(const wstring &n) {
    return SearchProgramClasses(n) || linker->SearchClassLibraries(n, program->GetUses(current_class->GetFileName()));
  }

  // class query by name
  bool GetProgramLibraryClass(const wstring &cls_name, Class*& klass, LibraryClass* &lib_klass);

  // search and cache class type query
  bool GetProgramLibraryClass(Type* type, Class* &klass, LibraryClass* &lib_klass);

  // resolve program or library class name
  wstring GetProgramLibraryClassName(const wstring &n);

  // determines if name equals class
  bool ClassEquals(const wstring &left_name, Class* right_klass, LibraryClass* right_lib_klass);

  // string utility functions
  wstring ToString(int v) {
    wostringstream str;
    str << v;
    return str.str();
  }

  // string replacement
  wstring ReplaceSubstring(wstring s, const wstring& f, const wstring& r);

  void ReplaceAllSubstrings(wstring &str, const wstring &from, const wstring &to);

  // returns true of signature matches holder type
  inline bool IsHolderType(const wstring &n) {
    unordered_set<wstring>::const_iterator result = holder_types.find(n);
    return result != holder_types.end();
  }
  
  // maps lambda decelerations to parameter list
  ExpressionList* MapLambdaDeclarations(DeclarationList* declarations);
  
  // error processing
  void ProcessError(const wstring& fn, int ln, int lp, const wstring& msg);
  void ProcessError(ParseNode* n, const wstring &msg);
  void ProcessErrorAlternativeMethods(wstring &message);
  void ProcessError(const wstring &fn, const wstring &msg);
  bool CheckErrors();

  // context operations
  void AnalyzeEnum(Enum* eenum, const int depth);
  void AnalyzeClass(Class* klass, const int id, const int depth);
  void CheckParent(Class* klass, const int depth);
  void AnalyzeInterfaces(Class* klass, const int depth);
  void AnalyzeGenerics(Class* klass, const int depth);
  void AnalyzeDuplicateClasses(vector<ParsedBundle*>& bundles);
  void AnalyzeDuplicateEntries(vector<Class*> &classes, const int depth);
  void AddDefaultParameterMethods(ParsedBundle* bundle, Class* klass, Method* method);
  void GenerateParameterMethods(ParsedBundle* bundle, Class* klass, Method* method);
  void AnalyzeMethods(Class* klass, const int depth);
  bool AnalyzeVirtualMethods(Class* impl_class, Class* lib_parent, const int depth);
  bool AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_parent, const int depth);
  void AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
                            bool impl_is_static, bool impl_is_virtual, Method* virtual_method);
  void AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
                            bool impl_is_static, bool impl_is_virtual, LibraryMethod* virtual_method);
  bool AnalyzeReturnPaths(StatementList* statement_list, const int depth);
  bool AnalyzeReturnPaths(If* if_stmt, bool nested, const int depth);
  bool AnalyzeReturnPaths(Select* select_stmt, const int depth);
  void AnalyzeMethod(Method* method, const int depth);
  void AnalyzeStatements(StatementList* statements, const int depth);
  void AnalyzeStatement(Statement* statement, const int depth);
  void AnalyzeIndices(ExpressionList* indices, const int depth);
  void AnalyzeExpressions(ExpressionList* parameters, const int depth);
  void AnalyzeExpression(Expression* expression, const int depth);
  void AnalyzeLambda(Lambda* param1, const int depth);
  Type* ResolveAlias(const wstring& name, const wstring& fn, int ln, int lp);
  Type* ResolveAlias(const wstring& name, ParseNode* node) {
    return ResolveAlias(name, node->GetFileName(), node->GetLineNumber(), node->GetLinePosition());
  }
  LibraryMethod* DerivedLambdaFunction(vector<LibraryMethod*>& alt_mthds);
  Method* DerivedLambdaFunction(vector<Method*>& alt_mthds);
  void BuildLambdaFunction(Lambda* lambda, Type* lambda_type, const int depth);
  bool HasInferredLambdaTypes(const wstring lambda_name);
  void CheckLambdaInferredTypes(MethodCall* method_call, int depth);
  void AnalyzeVariable(Variable* variable, SymbolEntry* entry, const int depth);
  void AnalyzeEnumCall(MethodCall* method_call, bool regress, const int depth);
  void AnalyzeVariable(Variable* variable, const int depth);
  void AnalyzeCharacterString(CharacterString* char_str, const int depth);
  void AnalyzeConditional(Cond* conditional, const int depth);
  void AnalyzeStaticArray(StaticArray* array, const int depth);
  void AnalyzeCast(Expression* expression, const int depth);
  void AnalyzeClassCast(Type* left, Expression* expression, const int depth);
  void AnalyzeClassCast(Type* left, Type* right, Expression* expression, bool generic_check, const int depth);
  bool CheckGenericEqualTypes(Type* left, Type* right, Expression* expression, bool check_only = false);
  void AnalyzeAssignment(Assignment * assignment, StatementType type, const int depth);
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
  Expression* UnboxingExpression(Type* right, Expression* expression, bool is_cast, int depth);
  void AnalyzeCalculation(CalculatedExpression* expression, const int depth);
  void AnalyzeCalculationCast(CalculatedExpression* expression, const int depth);
  bool UnboxingCalculation(Type* type, Expression* expression, CalculatedExpression* calc_expression, bool set_left, const int depth);
  MethodCall* BoxUnboxingReturn(Type* to_type, Expression* from_expr, const int depth);
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
  void AnalyzeMethodCall(Class* klass, MethodCall* method_call,
                         bool is_expr, wstring &encoding, const int depth);
  void AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call,
                         bool is_expr, wstring &encoding, bool is_parent, const int depth);
  void AnalyzeMethodCall(LibraryMethod* lib_method, MethodCall* method_call,
                         bool is_virtual, bool is_expr, const int depth);
  wstring EncodeMethodCall(ExpressionList * calling_params, const int depth);
  Method* ResolveMethodCall(Class* klass, MethodCall* method_call, const int depth);
  LibraryMethod* ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, const int depth);
  int MatchCallingParameter(Expression* calling_param, Type* method_type,
                            Class* klass, LibraryClass* lib_klass, const int depth);
  wstring EncodeFunctionType(vector<Type*> func_params, Type* func_rtrn);
  wstring EncodeFunctionReference(ExpressionList* calling_params, const int depth);
  void AnalyzeVariableFunctionCall(MethodCall* method_call, const int depth);
  void AnalyzeFunctionReference(Class* klass, MethodCall* method_call,
                                wstring &encoding, const int depth);
  void AnalyzeFunctionReference(LibraryClass* klass, MethodCall* method_call,
                                wstring &encoding, const int depth);
  
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
    type_map[L"$Byte"] = frontend::BYTE_TYPE;
    type_map[L"$Char"] = frontend::CHAR_TYPE;
    type_map[L"$Int"] = frontend::INT_TYPE;
    type_map[L"$Float"] = frontend::FLOAT_TYPE;
    type_map[L"$Bool"] = frontend::BOOLEAN_TYPE;

    // type holders
    holder_types.insert(L"System.BoolHolder");
    holder_types.insert(L"System.ByteHolder");
    holder_types.insert(L"System.CharHolder");
    holder_types.insert(L"System.IntHolder");
    holder_types.insert(L"System.FloatHolder");

    capture_method = nullptr;
    capture_table = nullptr;
    capture_lambda = nullptr;
  }

  ~ContextAnalyzer() {
    delete linker;
    linker = nullptr;
  }
  
  bool Analyze();

  //
  // diagnostics operations
  //
#ifdef _DIAG_LIB
  bool GetCompletion(Method* method, const wstring var_str, const wstring mthd_str, vector<pair<int, wstring> >& found_completion);
  void ContextAnalyzer::FindCompletionMethods(Class* klass, LibraryClass* lib_klass, const wstring mthd_str, vector<Method*>& found_methods, vector<LibraryMethod*>& found_lib_methods);

  bool GetSignature(Method* method, const wstring var_str, const wstring mthd_str, vector<Method*>& found_methods, vector<LibraryMethod*>& found_lib_methods);
  void FindSignatureClass(SymbolEntry* entry, const wstring mthd_str, Class* context_klass, vector<Method*>& found_methods, vector<LibraryMethod*>& found_lib_methods, bool is_completion);
  void FindSignatureMethods(Class* klass, LibraryClass* lib_klass, const wstring mthd_str, vector<Method*>& found_methods, vector<LibraryMethod*>& found_lib_methods);

  vector<Expression*> FindExpressions(Method* method, const int line_num, const int line_pos);

  bool GetDeclaration(Method* method, const int line_num, const int line_pos, wstring& found_name, int& found_line, int& found_start_pos, int& found_end_pos);

  bool LocateExpression(Method* method, const int line_num, const int line_pos, Expression*& found_expression, wstring& found_name, bool& is_alt, vector<Expression*>& all_expressions);
#endif
  //
  // end: diagnostics operations
  //
};

#endif
