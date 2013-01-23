/***************************************************************************
 * Performs contextual analysis.
 *
 * Copyright (c) 2008-2013, Randy Hollines
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

#ifndef __CONTEXT_H__
#define __CONTEXT_H__

#include "linker.h"
#include "types.h"
#include "tree.h"

#define SYSTEM_BASE_NAME "System.Base" 

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
    for(size_t i = 0; i < parm_matches.size(); i++) {
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
    for(size_t i = 0; i < matches.size(); i++) {
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
      tmp = NULL;
    }
  }
  
  LibraryMethod* GetSelection() {
    // no match
    if(valid_matches.size() == 0) {
      return NULL;
    }
    // single match
    else if(valid_matches.size() == 1) {
      return valid_matches[0]->GetLibraryMethod();
    }
    
    int match_index = -1;
    int high_score = 0;
    for(size_t i = 0; i < matches.size(); i++) {
      // calculate match score
      int match_score = 0;
      bool exact_match = true;
      vector<int> parm_matches = matches[i]->GetParameterMatches();
      for(size_t j = 0; exact_match && j < parm_matches.size(); j++) {
	if(parm_matches[j] == 0) {
	  match_score++;
	}
	else {
	  exact_match = false;
	}
      }
      // save the index of the best match
      if(match_score >  high_score) {
	match_index = i;
	high_score = match_score;
      }
    }

    if(match_index == -1) {
      return NULL;
    }
    
    return matches[match_index]->GetLibraryMethod();    
  }
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
    for(size_t i = 0; i < parm_matches.size(); i++) {
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
    for(size_t i = 0; i < matches.size(); i++) {
      // matches[i]->Dump();
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
      tmp = NULL;
    }
  }
  
  Method* GetSelection() {
    // no match
    if(valid_matches.size() == 0) {
      return NULL;
    }
    // single match
    else if(valid_matches.size() == 1) {
      return valid_matches[0]->GetMethod();
    }
    
    int match_index = -1;
    int high_score = 0;
    for(size_t i = 0; i < matches.size(); i++) {
      // calculate match score
      int match_score = 0;
      bool exact_match = true;
      vector<int> parm_matches = matches[i]->GetParameterMatches();
      for(size_t j = 0; exact_match && j < parm_matches.size(); j++) {
	if(parm_matches[j] == 0) {
	  match_score++;
	}
	else {
	  exact_match = false;
	}
      }
      // save the index of the best match
      if(match_score >  high_score) {
	match_index = i;
	high_score = match_score;
      }
    }

    if(match_index == -1) {
      return NULL;
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
  map<int, string> errors;
  map<const string, EntryType> type_map;
  bool main_found;
  bool web_found;
  bool is_lib;
  bool is_web;
  int char_str_index;
  int int_str_index;
  int float_str_index;
  bool in_loop;
  
  void Show(const string &msg, const int line_num, int depth) {
    cout << setw(4) << line_num << ": ";
    for(int i = 0; i < depth; i++) {
      cout << "  ";
    }
    cout << msg << endl;
  }

  string ToString(int v) {
    ostringstream str;
    str << v;
    return str.str();
  }

  // returns true if expression is not an array
  inline bool IsScalar(Expression* expression) {
    while(expression->GetMethodCall()) {
      expression = expression->GetMethodCall();
    }
    
    Type* type;
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }
    
    if(type && type->GetDimension() > 0) {
      ExpressionList* indices = NULL;
      if(expression->GetExpressionType() == VAR_EXPR) {
        indices = static_cast<Variable*>(expression)->GetIndices();
      } 
      else {
        return false;
      }

      return indices != NULL;
    }

    return true;
  }

  // returns true if expression is of boolean type
  inline bool IsBooleanExpression(Expression* expression) {
    while(expression->GetMethodCall()) {
      expression = expression->GetMethodCall();
    }
    Type* eval_type = expression->GetEvalType();
    if(eval_type) {
      return eval_type->GetType() == BOOLEAN_TYPE;
    }

    return false;
  }

  // returns true if expression is of boolean type
  inline bool IsEnumExpression(Expression* expression) {
    while(expression->GetMethodCall()) {
      expression = expression->GetMethodCall();
    }
    Type* eval_type = expression->GetEvalType();
    if(eval_type) {
      if(eval_type->GetType() == CLASS_TYPE) {
        // program
        if(program->GetEnum(eval_type->GetClassName())) {
          return true;
        }
        // library
        if(linker->SearchEnumLibraries(eval_type->GetClassName(), program->GetUses())) {
          return true;
        }
      }
    }

    return false;
  }

  // returns true if expression is of integer or enum type
  inline bool IsIntegerExpression(Expression* expression) {
    while(expression->GetMethodCall()) {
      expression = expression->GetMethodCall();
    }

    Type* eval_type;
    if(expression->GetCastType()) {
      eval_type = expression->GetCastType();
    }
    else {
      eval_type = expression->GetEvalType();
    }

    if(eval_type) {
      // integer types
      if(eval_type->GetType() == INT_TYPE || eval_type->GetType() == CHAR_TYPE || 
	 eval_type->GetType() == BYTE_TYPE) {
        return true;
      }
      // enum types
      if(eval_type->GetType() == CLASS_TYPE) {
        // program
        if(program->GetEnum(eval_type->GetClassName())) {
          return true;
        }
        // library
        if(linker->SearchEnumLibraries(eval_type->GetClassName(), program->GetUses())) {
          return true;
        }
      }
    }

    return false;
  }

  // returns true if entry static cotext is not valid
  inline bool InvalidStatic(SymbolEntry* entry) {
    return current_method->IsStatic() && !entry->IsLocal() && !entry->IsStatic();
  }

  // returns true if a duplicate value is found in the list
  inline bool DuplicateCaseItem(map<int, StatementList*>label_statements, int value) {
    map<int, StatementList*>::iterator result = label_statements.find(value);
    if(result != label_statements.end()) {
      return true;
    }

    return false;
  }

  // returns true if method static cotext is not valid
  inline bool InvalidStatic(MethodCall* method_call, Method* method) {
    // same class, calling method static and called method not static,
    // called method not new, called method not from a varaible
    if(current_class == method->GetClass() && current_method->IsStatic() &&
        !method->IsStatic() && method->GetMethodType() != NEW_PUBLIC_METHOD &&
        method->GetMethodType() != NEW_PRIVATE_METHOD) {

      SymbolEntry* entry = GetEntry(method_call->GetVariableName());
      if(entry && (entry->IsLocal()  || entry->IsStatic())) {
        return false;
      }

      Variable* variable = method_call->GetVariable();
      if(variable) {
        entry = variable->GetEntry();
        if(entry && (entry->IsLocal()  || entry->IsStatic())) {
          return false;
        }
      }

      return true;
    }

    return false;
  }

  // returns true if method static cotext is not valid
  inline bool InvalidStatic(MethodCall* method_call, LibraryMethod* method) {
    // same class, calling method static and called method not static,
    // called method not new, called method not from a varaible
    if(current_method->IsStatic() && !method->IsStatic() &&
        method->GetMethodType() != NEW_PUBLIC_METHOD &&
        method->GetMethodType() != NEW_PRIVATE_METHOD) {

      SymbolEntry* entry = GetEntry(method_call->GetVariableName());
      if(entry && (entry->IsLocal() || entry->IsStatic())) {
        return false;
      }

      Variable* variable = method_call->GetVariable();
      if(variable) {
        entry = variable->GetEntry();
        if(entry && (entry->IsLocal() || entry->IsStatic())) {
          return false;
        }
      }

      return true;
    }

    return false;
  }

  // returns a symbol table entry by name
  SymbolEntry* GetEntry(string name) {
    // check locally
    SymbolEntry* entry = current_table->GetEntry(current_method->GetName() + ":" + name);
    if(entry) {
      return entry;
    } 
    else {
      // check class
      SymbolTable* table = symbol_table->GetSymbolTable(current_class->GetName());
      entry = table->GetEntry(current_class->GetName() + ":" + name);
      if(entry) {
        return entry;
      } 
      else {
        // check parents
	const string& bundle_name = bundle->GetName();
        Class* parent;
	if(bundle_name.size() > 0) {
	  parent = bundle->GetClass(bundle_name + "." + current_class->GetParentName());
	}
	else {
	  parent = bundle->GetClass(current_class->GetParentName());
	}
        while(parent && !entry) {
          SymbolTable* table = symbol_table->GetSymbolTable(parent->GetName());
          entry = table->GetEntry(parent->GetName() + ":" + name);
          if(entry) {
            return entry;
          }
	  // get next parent	  
	  if(bundle_name.size() > 0) {
	    parent = bundle->GetClass(bundle_name + "." + parent->GetParentName());
	  }
	  else {
	    parent = bundle->GetClass(parent->GetParentName());
	  }
        }
      }
    }

    return NULL;
  }

  // returns a symbol table entry by name for a given method
  SymbolEntry* GetEntry(MethodCall* method_call, const string &variable_name, int depth) {
    SymbolEntry* entry;
    if(method_call->GetVariable()) {
      Variable* variable = method_call->GetVariable();
      AnalyzeVariable(variable, depth);
      entry = variable->GetEntry();
    } else {
      entry = GetEntry(variable_name);
      if(entry) {
        method_call->SetEntry(entry);
      }
    }

    return entry;
  }

  // returns a type expression
  Type* GetExpressionType(Expression* expression, int depth) {
    Type* type;
    MethodCall* mthd_call = expression->GetMethodCall();
    if(mthd_call) {
      while(mthd_call) {
        AnalyzeExpressionMethodCall(mthd_call, depth + 1);

	// favor casts
	if(mthd_call->GetCastType()) {
	  type = mthd_call->GetCastType();
	}
	else {
	  type = mthd_call->GetEvalType();
	}
	
        mthd_call = mthd_call->GetMethodCall();
      }
    } 
    else {
      // favor casts
      if(expression->GetCastType()) {
	type = expression->GetCastType();
      }
      else {
	type = expression->GetEvalType();
      }
    }

    return type;
  }

  // checks for a valid downcast
  bool ValidDownCast(const string &cls_name, Class* class_tmp, LibraryClass* lib_class_tmp) {
    while(class_tmp || lib_class_tmp) {
      // get cast name
      string cast_name;
      vector<string> interface_names;
      if(class_tmp) {
        cast_name = class_tmp->GetName();
	interface_names = class_tmp->GetInterfaceNames();
      } 
      else if(lib_class_tmp) {
        cast_name = lib_class_tmp->GetName();
	interface_names = lib_class_tmp->GetInterfaceNames();
      }
      
      // parent cast
      if(cls_name == cast_name) {
	return true;
      }
      
      // interface cast
      for(size_t i = 0; i < interface_names.size(); i++) {
	Class* klass = SearchProgramClasses(interface_names[i]);
	if(klass && klass->GetName() == cls_name) {
	  return true;
	}
	else {
	  LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetUses());
	  if(lib_klass && lib_klass->GetName() == cls_name) {
	    return true;
	  }
	}
      }
      
      // update
      if(class_tmp) {
        if(class_tmp->GetParent()) {
          class_tmp = class_tmp->GetParent();
          lib_class_tmp = NULL;
        } else {
          lib_class_tmp = class_tmp->GetLibraryParent();
          class_tmp = NULL;
        }

      }
      // library parent
      else {
        lib_class_tmp = linker->SearchClassLibraries(lib_class_tmp->GetParentName(), program->GetUses());
        class_tmp = NULL;
      }
    }

    return false;
  }

  // checks for a valid upcast
  bool ValidUpCast(const string &to, Class* from_klass) {
    if(from_klass->GetName() == "System.Base") {
      return true;
    }

    // parent cast
    if(to == from_klass->GetName()) {
      return true;
    }
    
    // interface cast
    vector<string> interface_names = from_klass->GetInterfaceNames();
    for(size_t i = 0; i < interface_names.size(); i++) {
      Class* klass = SearchProgramClasses(interface_names[i]);
      if(klass && klass->GetName() == to) {
	return true;
      }
      else {
	LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetUses());
	if(lib_klass && lib_klass->GetName() == to) {
	  return true;
	}
      }
    }
    
    // updates
    vector<Class*> children = from_klass->GetChildren();
    for(size_t i = 0; i < children.size(); i++) {
      if(ValidUpCast(to, children[i])) {
        return true;
      }
    }

    return false;
  }

  // checks for a valid upcast
  bool ValidUpCast(const string &to, LibraryClass* from_klass) {
    if(from_klass->GetName() == "System.Base") {
      return true;
    }

    // parent cast
    if(to == from_klass->GetName()) {
      return true;
    }
    
    // interface cast
    vector<string> interface_names = from_klass->GetInterfaceNames();
    for(size_t i = 0; i < interface_names.size(); i++) {
      Class* klass = SearchProgramClasses(interface_names[i]);
      if(klass && klass->GetName() == to) {
	return true;
      }
      else {
	LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetUses());
	if(lib_klass && lib_klass->GetName() == to) {
	  return true;
	}
      }
    }

    // program updates
    vector<LibraryClass*> children = from_klass->GetLibraryChildren();
    for(size_t i = 0; i < children.size(); i++) {
      if(ValidUpCast(to, children[i])) {
        return true;
      }
    }

    // library updates
    vector<frontend::ParseNode*> lib_children = from_klass->GetChildren();
    for(size_t i = 0; i < lib_children.size(); i++) {
      if(ValidUpCast(to, static_cast<Class*>(lib_children[i]))) {
        return true;
      }
    }
    
    return false;
  }
  
  // TODO: finds the first enum match; note multiple matches may exist
  inline Class* SearchProgramClasses(const string &klass_name) {
    Class* klass = program->GetClass(klass_name);
    if(!klass) {
      klass = program->GetClass(bundle->GetName() + "." + klass_name);
      if(!klass) {
        vector<string> uses = program->GetUses();
        for(size_t i = 0; !klass && i < uses.size(); i++) {
          klass = program->GetClass(uses[i] + "." + klass_name);
        }
      }
    }

    return klass;
  }

  // TODO: finds the first enum match; note multiple matches may exist
  inline Enum* SearchProgramEnums(const string &eenum_name) {
    Enum* eenum = program->GetEnum(eenum_name);
    if(!eenum) {
      eenum = program->GetEnum(bundle->GetName() + "." + eenum_name);
      if(!eenum) {
        vector<string> uses = program->GetUses();
        for(size_t i = 0; !eenum && i < uses.size(); i++) {
          eenum = program->GetEnum(uses[i] + "." + eenum_name);
        }
      }
    }

    return eenum;
  }

  inline const string EncodeType(Type* type) {
    string encoded_name;
    
    if(type) {
      switch(type->GetType()) {
      case BOOLEAN_TYPE:
	encoded_name += 'l';
	break;

      case BYTE_TYPE:
	encoded_name += 'b';
	break;

      case INT_TYPE:
	encoded_name += 'i';
	break;

      case FLOAT_TYPE:
	encoded_name += 'f';
	break;

      case CHAR_TYPE:
	encoded_name += 'c';
	break;

      case NIL_TYPE:
	encoded_name += 'n';
	break;

      case VAR_TYPE:
	encoded_name += 'v';
	break;

      case CLASS_TYPE: {
	encoded_name += "o.";

	// search program
	string klass_name = type->GetClassName();
	Class* klass = program->GetClass(klass_name);
	if(!klass) {
	  vector<string> uses = program->GetUses();
	  for(size_t i = 0; !klass && i < uses.size(); i++) {
	    klass = program->GetClass(uses[i] + "." + klass_name);
	  }
	}
	if(klass) {
	  encoded_name += klass->GetName();
	}
	// search libaraires
	else {
	  LibraryClass* lib_klass = linker->SearchClassLibraries(klass_name, program->GetUses());
	  if(lib_klass) {
	    encoded_name += lib_klass->GetName();
	  } 
	  else {
	    encoded_name += type->GetClassName();
	  }
	}
      }
	break;
      
      case FUNC_TYPE: {
	if(type->GetClassName().size() == 0) {
	  type->SetClassName(EncodeFunctionType(type->GetFunctionParameters(), 
						type->GetFunctionReturn()));
	}
	encoded_name += type->GetClassName();
      }
	break;
      }
    }
    
    return encoded_name;
  }
  
  inline bool ResolveClassEnumType(Type* type) {
    bool found = false;
      Class* klass = SearchProgramClasses(type->GetClassName());
      if(klass) {
        klass->SetCalled(true);
        type->SetClassName(klass->GetName());
        found = true;
      }

      LibraryClass* lib_klass = linker->SearchClassLibraries(type->GetClassName(), program->GetUses());
      if(lib_klass) {
	lib_klass->SetCalled(true);
        type->SetClassName(lib_klass->GetName());
        found = true;
      }
      
      Enum* eenum = SearchProgramEnums(type->GetClassName());
      if(eenum) {
        type->SetClassName(eenum->GetName());
        found = true;
      }

      LibraryEnum* lib_eenum = linker->SearchEnumLibraries(type->GetClassName(), program->GetUses());
      if(lib_eenum) {
        type->SetClassName(lib_eenum->GetName());
        found = true;
      }
      
      return found;
  }

  bool IsClassEnumParameterMatch(Type* calling_type, Type* method_type) {
    const string &from_klass_name = calling_type->GetClassName();
    Class* from_klass = SearchProgramClasses(from_klass_name);
    LibraryClass* from_lib_klass = linker->SearchClassLibraries(from_klass_name, 
								program->GetUses());
    // resolve to_klass name
    string to_klass_name;
    Class* to_klass = SearchProgramClasses(method_type->GetClassName());
    if(!to_klass) {
      LibraryClass* to_lib_klass = linker->SearchClassLibraries(method_type->GetClassName(),
								program->GetUses());
      if(to_lib_klass) {
	to_klass_name = to_lib_klass->GetName();
      }
    }
    else {
      to_klass_name = to_klass->GetName();
    }
	      
    // check enum types
    if(!from_klass && !from_lib_klass) {
      Enum* from_enum = SearchProgramEnums(from_klass_name);
      LibraryEnum* from_lib_enum = linker->SearchEnumLibraries(from_klass_name, program->GetUses());
	
      string to_enum_name;	
      Enum* to_enum = SearchProgramEnums(method_type->GetClassName());
      if(!to_enum) {
	LibraryEnum* to_lib_enum = linker->SearchEnumLibraries(method_type->GetClassName(),
							       program->GetUses());
	if(to_lib_enum) {
	  to_enum_name = to_lib_enum->GetName();
	}
      }
      else {
	to_enum_name = to_enum->GetName();
      }

      // look for exact class match
      if(from_enum && from_enum->GetName() == to_enum_name) {
	return true;
      }
	      
      // look for exact class library match
      if(from_lib_enum && from_lib_enum->GetName() == to_enum_name) {
	return true;
      }
    }
    else {
      // look for exact class match
      if(from_klass && from_klass->GetName() == to_klass_name) {
	return true;
      }
	      
      // look for exact class library match
      if(from_lib_klass && from_lib_klass->GetName() == to_klass_name) {
	return true;
      }
    }
    
    return false;
  }
  
  // TODO:
  void AnalyzeCharacterStringParameters(CharacterString* char_str) {
    const string &str = char_str->GetString();
    for(size_t i = 0; i < str.size(); i++) {
      if(str[i] == '$' && (i + 1 < str.size() && str[i + 1] != '$')) {
	cout << "## FOO ###" << endl;
      }
    }
  }

  // error processing
  void ProcessError(ParseNode* n, const string &msg);
  void ProcessError(const string &msg);
  bool CheckErrors();
  
  // context operations
  void AnalyzeEnum(Enum* eenum, int depth);
  void AnalyzeClass(Class* klass, int id, int depth);
  void AnalyzeMethods(Class* klass, int depth);
  bool AnalyzeVirtualMethods(Class* impl_class, Class* lib_parent, int depth);
  bool AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_parent, int depth);

  void AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
			    bool impl_is_static, bool impl_is_virtual, Method* virtual_method);
  void AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return, 
			    bool impl_is_static, bool impl_is_virtual, LibraryMethod* virtual_method);
  

  void AnalyzeInterfaces(Class* klass, int depth);
  void AnalyzeMethod(Method* method, int id, int depth);
  void AnalyzeStatements(StatementList* statements, int depth);
  void AnalyzeStatement(Statement* statement, int depth);
  void AnalyzeIndices(ExpressionList* indices, int depth);
  void AnalyzeExpressions(ExpressionList* parameters, int depth);
  void AnalyzeExpression(Expression* expression, int depth);
  void AnalyzeVariable(Variable* variable, int depth);
  void AnalyzeCharacterString(CharacterString* char_str, int depth);
  void AnalyzeConditional(Cond* conditional, int depth);
  void AnalyzeStaticArray(StaticArray* array, int depth);
  void AnalyzeCast(Expression* expression, int depth);
  void AnalyzeClassCast(Type* left, Expression* expression, int depth);
  void AnalyzeAssignment(Assignment* assignment, int depth);
  void AnalyzeSimpleStatement(SimpleStatement* simple, int depth);
  void AnalyzeIf(If* if_stmt, int depth);
  void AnalyzeDoWhile(DoWhile* do_while_stmt, int depth);
  void AnalyzeWhile(While* while_stmt, int depth);
  void AnalyzeSelect(Select* select_stmt, int depth);
  void AnalyzeCritical(CriticalSection* mutex, int depth);
  void AnalyzeFor(For* for_stmt, int depth);
  void AnalyzeReturn(Return* rtrn, int depth);
  void AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, int depth);
  void AnalyzeRightCast(Type* left, Type* right, Expression* expression, bool is_scalar, int depth);
  void AnalyzeCalculation(CalculatedExpression* expression, int depth);
  void AnalyzeCalculationCast(CalculatedExpression* expression, int depth);
  void AnalyzeDeclaration(Declaration* declaration, int depth);
  // checks for method calls, which includes new array and object allocation
  void AnalyzeExpressionMethodCall(Expression* expression, int depth);
  bool AnalyzeExpressionMethodCall(SymbolEntry* entry, string &encoding,
                                   Class* &klass, LibraryClass* &lib_klass);
  bool AnalyzeExpressionMethodCall(Expression* expression, string &encoding,
                                   Class* &klass, LibraryClass* &lib_klass, bool &is_enum_call);
  bool AnalyzeExpressionMethodCall(Type* type, const int dimension, string &encoding,
                                   Class* &klass, LibraryClass* &lib_klass, bool &is_enum_call);
  void AnalyzeMethodCall(MethodCall* method_call, int depth);
  void AnalyzeNewArrayCall(MethodCall* method_call, int depth);
  void AnalyzeParentCall(MethodCall* method_call, int depth);
  LibraryClass* AnalyzeLibraryMethodCall(MethodCall* method_call, string &encoding, int depth);
  Class* AnalyzeProgramMethodCall(MethodCall* method_call, string &encoding, int depth);
  void AnalyzeMethodCall(Class* klass, MethodCall* method_call,
                         bool is_expr, string &encoding, int depth);
  void AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call,
                         bool is_expr, string &encoding, bool is_parent, int depth);
  void AnalyzeMethodCall(LibraryMethod* lib_method, MethodCall* method_call,
                         bool is_virtual, bool is_expr, int depth);
  string EncodeMethodCall(ExpressionList* calling_params, int depth);
  Method* ResolveMethodCall(Class* klass, MethodCall* method_call, int depth);
  LibraryMethod* ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, int depth);
  int MatchCallingParameter(Expression* calling_param, Type* method_type,
			    Class* klass, LibraryClass* lib_klass, int depth);
  string EncodeFunctionType(vector<Type*> func_params, Type* func_rtrn);
  string EncodeFunctionReference(ExpressionList* calling_params, int depth);
  void AnalyzeDynamicFunctionCall(MethodCall* method_call, int depth);
  void AnalyzeFunctionReference(Class* klass, MethodCall* method_call,
				string &encoding, int depth);
  void AnalyzeFunctionReference(LibraryClass* klass, MethodCall* method_call,
				string &encoding, int depth);
  
public:
  ContextAnalyzer(ParsedProgram* p, string lib_path, bool l, bool w) {
    program = p;
    is_lib = l;
    is_web = w;
    main_found = web_found = false;
    // initialize linker
    linker = new Linker(lib_path);
    program->SetLinker(linker);
    char_str_index = int_str_index = float_str_index= 0;
    in_loop = false;
    
    // setup type map
    type_map["$Byte"] = BYTE_TYPE;
    type_map["$Char"] = CHAR_TYPE;
    type_map["$Int"] = INT_TYPE;
    type_map["$Float"] = FLOAT_TYPE;
    type_map["$Bool"] = BOOLEAN_TYPE;
  }

  ~ContextAnalyzer() {
  }

  bool Analyze();
};

#endif
