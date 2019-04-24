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
      tmp = NULL;
    }
  }

  vector<wstring> GetAlternativeMethodNames() {
    vector<wstring> alt_names;
    for(size_t i = 0; i < matches.size(); ++i) {
      alt_names.push_back(matches[i]->GetLibraryMethod()->GetUserName());
    }

    return alt_names;
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
      tmp = NULL;
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
      return NULL;
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

  void Debug(const wstring &msg, const int line_num, int depth) {
    GetLogger() << setw(4) << line_num << L": ";
    for(int i = 0; i < depth; ++i) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << endl;
  }

  wstring ToString(int v) {
    wostringstream str;
    str << v;
    return str.str();
  }
  
  // returns true if expression is not an array
  inline bool IsScalar(Expression* expression, bool check_last = true) {
    while(check_last && expression->GetMethodCall()) {
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
        if(SearchProgramEnums(eval_type->GetClassName())) {
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
  inline bool DuplicateParentEntries(SymbolEntry* entry, Class* klass) {
    if(klass->GetParent() && klass->GetParent()->GetSymbolTable() && (!entry->IsLocal() || entry->IsStatic())) {
      Class* parent = klass->GetParent();
      do {
        size_t offset = entry->GetName().find(L':');
        if(offset != wstring::npos) {
          ++offset;
          const wstring short_name = entry->GetName().substr(offset, entry->GetName().size() - offset);
          const wstring lookup = parent->GetName() + L":" + short_name;
          SymbolEntry* parent_entry = parent->GetSymbolTable()->GetEntry(lookup);
          if(parent_entry) {
            return true;
          }
        }
        // update
        parent = parent->GetParent();
      } 
      while(parent);
    }

    return false;
  }

  // returns true if this entry is duplicated in parent classes
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
    if(current_method->IsStatic() &&
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
  SymbolEntry* GetEntry(wstring name, bool is_parent = false) {
    if(current_table) {
      // check locally
      SymbolEntry* entry = current_table->GetEntry(current_method->GetName() + L":" + name);
      if(!is_parent && entry) {
        return entry;
      }
      else {
        // check class
        SymbolTable* table = symbol_table->GetSymbolTable(current_class->GetName());
        entry = table->GetEntry(current_class->GetName() + L":" + name);
        if(!is_parent && entry) {
          return entry;
        }
        else {
          // check parents
          entry = NULL;
          const wstring& bundle_name = bundle->GetName();
          Class* parent;
          if(bundle_name.size() > 0) {
            parent = bundle->GetClass(bundle_name + L"." + current_class->GetParentName());
          }
          else {
            parent = bundle->GetClass(current_class->GetParentName());
          }
          while(parent && !entry) {
            SymbolTable* table = symbol_table->GetSymbolTable(parent->GetName());
            entry = table->GetEntry(parent->GetName() + L":" + name);
            if(entry) {
              return entry;
            }
            // get next parent	  
            if(bundle_name.size() > 0) {
              parent = bundle->GetClass(bundle_name + L"." + parent->GetParentName());
            }
            else {
              parent = bundle->GetClass(parent->GetParentName());
            }
          }
        }
      }
    }

    return NULL;
  }

  // returns a symbol table entry by name for a given method
  SymbolEntry* GetEntry(MethodCall* method_call, const wstring &variable_name, int depth) {
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
    Type* type = NULL;

    MethodCall* mthd_call = expression->GetMethodCall();

    if(expression->GetExpressionType() == METHOD_CALL_EXPR && 
       static_cast<MethodCall*>(expression)->GetCallType() == ENUM_CALL) {
      // favor casts
      if(expression->GetCastType()) {
        type = expression->GetCastType();
      }
      else {
        type = expression->GetEvalType();
      }
    }
    else if(mthd_call) {
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

	// TODO: adding generics
	// generic type erasure 
	Type* RelsolveGenericType(Type* generic_type, MethodCall* method_call, Class* klass, LibraryClass* lib_klass) {
		if(generic_type->GetType() == FUNC_TYPE) {
			Type* concrete_rtrn = RelsolveGenericType(generic_type->GetFunctionReturn(), method_call, klass, lib_klass);

			vector<Type*> concrete_params;
			const vector<Type*> type_params = generic_type->GetFunctionParameters();
			for(size_t i = 0; i < type_params.size(); ++i) {
				concrete_params.push_back(RelsolveGenericType(type_params[i], method_call, klass, lib_klass));
			}

			Type* concrete_type = TypeFactory::Instance()->MakeType(concrete_params, concrete_rtrn);
			return concrete_type;
		}
		else {
			int concrete_index = -1;
			const wstring generic_name = generic_type->GetClassName();
			if(klass) {
				concrete_index = klass->GenericIndex(generic_name);
			}
			else if(lib_klass) {
				concrete_index = lib_klass->GenericIndex(generic_name);
			}

			if(klass->HasGenerics()) {
				if(concrete_index > -1) {
					if(method_call->GetEntry()) {
						const vector<Type*> concrete_types = method_call->GetEntry()->GetType()->GetGenerics();
						if(concrete_index < (int)concrete_types.size()) {
							return concrete_types[concrete_index];
						}
					}
					else if(method_call->GetVariable() && method_call->GetVariable()->GetEntry()) {
						const vector<Type*> concrete_types = method_call->GetVariable()->GetEntry()->GetType()->GetGenerics();
						if(concrete_index < (int)concrete_types.size()) {
							return concrete_types[concrete_index];
						}
					}
					else if(method_call->GetCallType() == NEW_INST_CALL && method_call->HasConcreteNames()) {
						const vector<Type*> concrete_types = method_call->GetConcreteNames();
						if(concrete_index < (int)concrete_types.size()) {
							return concrete_types[concrete_index];
						}
					}
					// nested call, maybe reevaluate?
					else if(method_call->GetPreviousExpression() && method_call->GetPreviousExpression()->GetExpressionType() == METHOD_CALL_EXPR) {
						MethodCall* prev_method_call = static_cast<MethodCall*>(method_call->GetPreviousExpression());
						if(prev_method_call->GetEvalType()) {
							const vector<Type*> concrete_types = prev_method_call->GetEvalType()->GetGenerics();
							if(concrete_index < (int)concrete_types.size()) {
								return concrete_types[concrete_index];
							}
						}
					}
				}
			}
		}

		return generic_type;
	}
	
  // checks for a valid downcast
  bool ValidDownCast(const wstring &cls_name, Class* class_tmp, LibraryClass* lib_class_tmp) {
    if(cls_name == L"System.Base") {
      return true;
    }

    while(class_tmp || lib_class_tmp) {
      // get cast name
      wstring cast_name;
      vector<wstring> interface_names;
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
      for(size_t i = 0; i < interface_names.size(); ++i) {
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
  bool ValidUpCast(const wstring &to, Class* from_klass) {
    if(from_klass->GetName() == L"System.Base") {
      return true;
    }

    // parent cast
    if(to == from_klass->GetName()) {
      return true;
    }

    // interface cast
    vector<wstring> interface_names = from_klass->GetInterfaceNames();
    for(size_t i = 0; i < interface_names.size(); ++i) {
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
    for(size_t i = 0; i < children.size(); ++i) {
      if(ValidUpCast(to, children[i])) {
        return true;
      }
    }

    return false;
  }

  // checks for a valid upcast
  bool ValidUpCast(const wstring &to, LibraryClass* from_klass) {
    if(from_klass->GetName() == L"System.Base") {
      return true;
    }

    // parent cast
    if(to == from_klass->GetName()) {
      return true;
    }

    // interface cast
    vector<wstring> interface_names = from_klass->GetInterfaceNames();
    for(size_t i = 0; i < interface_names.size(); ++i) {
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
    for(size_t i = 0; i < children.size(); ++i) {
      if(ValidUpCast(to, children[i])) {
        return true;
      }
    }

    // library updates
    vector<frontend::ParseNode*> lib_children = from_klass->GetChildren();
    for(size_t i = 0; i < lib_children.size(); ++i) {
      if(ValidUpCast(to, static_cast<Class*>(lib_children[i]))) {
        return true;
      }
    }

    return false;
  }

  // TODO: finds the first enum match; note multiple matches may exist
  inline Class* SearchProgramClasses(const wstring &klass_name) {
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

  // TODO: finds the first enum match; note multiple matches may exist
  inline Enum* SearchProgramEnums(const wstring &eenum_name) {
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

  inline const wstring EncodeType(Type* type) {
    wstring encoded_name;

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
        encoded_name += L"o.";

        // search program
        wstring klass_name = type->GetClassName();
        Class* klass = program->GetClass(klass_name);
        if(!klass) {
          vector<wstring> uses = program->GetUses();
          for(size_t i = 0; !klass && i < uses.size(); ++i) {
            klass = program->GetClass(uses[i] + L"." + klass_name);
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
    return ResolveClassEnumType(type, current_class);
  }
  
  inline bool ResolveClassEnumType(Type* type, Class* context_klass) {
    Class* klass = SearchProgramClasses(type->GetClassName());
    if(klass) {
			klass->SetCalled(true);
			type->SetClassName(klass->GetName());
			return true;
    }
		
    LibraryClass* lib_klass = linker->SearchClassLibraries(type->GetClassName(), program->GetUses());
		// TODO: adding generics
    if(lib_klass) {
      lib_klass->SetCalled(true);
      type->SetClassName(lib_klass->GetName());
      return true;
    }

		// look up generic type
		if(context_klass->HasGenerics()) {
			klass = context_klass->GetGenericClass(type->GetClassName());
			if(klass) {
				if(klass->HasGenericInterface()) {
					Type* inf_type = klass->GetGenericInterface();
					if(ResolveClassEnumType(inf_type)) {
						type->SetClassName(inf_type->GetClassName());
						return true;
					}
				}
				else {
					type->SetClassName(type->GetClassName());
					return true;
				}
			}
		}

    Enum* eenum = SearchProgramEnums(type->GetClassName());
    if(eenum) {
      type->SetClassName(type->GetClassName());
      return true;
    }
    else {
      eenum = SearchProgramEnums(context_klass->GetName() + L"#" + type->GetClassName());
      if(eenum) {
        type->SetClassName(context_klass->GetName() + L"#" + type->GetClassName());
        return true;
      }
    }

    LibraryEnum* lib_eenum = linker->SearchEnumLibraries(type->GetClassName(), program->GetUses());
    if(lib_eenum) {
      type->SetClassName(lib_eenum->GetName());
      return true;
    }
    else {
      lib_eenum = linker->SearchEnumLibraries(type->GetClassName(), program->GetUses());
      if(lib_eenum) {
        type->SetClassName(lib_eenum->GetName());
        return true;
      }
    }
    
    return false;
  }

  bool IsClassEnumParameterMatch(Type* calling_type, Type* method_type) {
    const wstring &from_klass_name = calling_type->GetClassName();
    Class* from_klass = SearchProgramClasses(from_klass_name);
		if(!from_klass && current_class->HasGenerics()) {
			from_klass = current_class->GetGenericClass(from_klass_name);
		}
    LibraryClass* from_lib_klass = linker->SearchClassLibraries(from_klass_name, program->GetUses());

    // resolve to class name
    wstring to_klass_name;
    Class* to_klass = SearchProgramClasses(method_type->GetClassName());
		if(!to_klass && current_class->HasGenerics()) {
			to_klass = current_class->GetGenericClass(method_type->GetClassName());
		}

		if(!to_klass) {
      LibraryClass* to_lib_klass = linker->SearchClassLibraries(method_type->GetClassName(), program->GetUses());
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

      wstring to_enum_name;	
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
  
  inline void ResolveEnumCall(LibraryEnum* lib_eenum, const wstring &item_name, MethodCall* method_call) {
    // item_name = method_call->GetMethodCall()->GetVariableName();
    LibraryEnumItem* lib_item = lib_eenum->GetItem(item_name);
    if(lib_item) {
      if(method_call->GetMethodCall()) {
        method_call->GetMethodCall()->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
        method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_eenum->GetName()), false);
        method_call->GetMethodCall()->SetEvalType(method_call->GetEvalType(), false);
      }
      else {
        method_call->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
        method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_eenum->GetName()), false);
      }
    } 
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined enum item: '" + item_name + L"'");
    }
  }
  
  void AnalyzeCharacterStringVariable(SymbolEntry* entry, CharacterString* char_str, int depth) {
#ifdef _DEBUG
    Debug(L"variable=|" + entry->GetName() + L"|", char_str->GetLineNumber(), depth + 1);
#endif
    if(!entry->GetType() || entry->GetType()->GetDimension() > 0) {
      ProcessError(char_str, L"Invalid function variable type or dimension size");
    }
    else if(entry->GetType()->GetType() == CLASS_TYPE && 
            entry->GetType()->GetClassName() != L"System.String" && 
            entry->GetType()->GetClassName() != L"String") {
      const wstring cls_name = entry->GetType()->GetClassName();
      Class* klass = SearchProgramClasses(cls_name);
      if(klass) {
        Method* method = klass->GetMethod(cls_name + L":ToString:");
        if(method && method->GetMethodType() != PRIVATE_METHOD) {
          char_str->AddSegment(entry, method);
        }
        else {
          ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
        }
      }
      else {
        LibraryClass* lib_klass = linker->SearchClassLibraries(cls_name, program->GetUses());
        if(lib_klass) {
          LibraryMethod* lib_method = lib_klass->GetMethod(cls_name + L":ToString:");
          if(lib_method && lib_method->GetMethodType() != PRIVATE_METHOD) {
            char_str->AddSegment(entry, lib_method);
          }
          else {
            ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
          }
        }
        else {
          ProcessError(char_str, L"Class/enum variable does not have a 'ToString' method");
        }
      }
    }
    else if(entry->GetType()->GetType() == FUNC_TYPE) {
      ProcessError(char_str, L"Invalid function variable type");
    }
    else {
      char_str->AddSegment(entry);
    }
  }

  void AnalyzeVariableCast(Type* to_type, Expression* expression) {
    if(to_type && to_type->GetType() == CLASS_TYPE && expression->GetCastType() && to_type->GetDimension() < 1 && 
       to_type->GetClassName() != L"System.Base" &&  to_type->GetClassName() != L"Base") {
      const wstring to_class_name = to_type->GetClassName();
      if(SearchProgramEnums(to_class_name) || 
         linker->SearchEnumLibraries(to_class_name, program->GetUses(current_class->GetFileName()))) {
        return;
      }

      Class* to_class = SearchProgramClasses(to_class_name);
      if(to_class) {
        expression->SetToClass(to_class);
      }
      else {
        LibraryClass* to_lib_class = linker->SearchClassLibraries(to_class_name, program->GetUses());
        if(to_lib_class) {
          expression->SetToLibraryClass(to_lib_class);
        }
        else {
          ProcessError(expression, L"Undefined class: '" + to_class_name + L"'");
        }
      }
    }
  }

	void AnalyzeDynamicFunctionParameters(vector<Type*>& func_params, ParseNode* node) {
		AnalyzeDynamicFunctionParameters(func_params, node, current_class);
	}

  void AnalyzeDynamicFunctionParameters(vector<Type*>& func_params, ParseNode* node, Class* klass) {
		for(size_t i = 0; i < func_params.size(); ++i) {
			Type* type = func_params[i];
			if(type->GetType() == CLASS_TYPE && !ResolveClassEnumType(type, klass)) {
				ProcessError(node, L"Undefined class or enum: '" + type->GetClassName() + L"'");
			}
		}
  }

	void AddMethodParameter(MethodCall* method_call, SymbolEntry* entry, int depth) {
    const wstring &entry_name = entry->GetName();
    const size_t start = entry_name.find_last_of(':');
    if(start != wstring::npos) {
      const wstring &param_name = entry_name.substr(start + 1);
      Variable* variable = TreeFactory::Instance()->MakeVariable(static_cast<Expression*>(method_call)->GetFileName(), 
                                                                 static_cast<Expression*>(method_call)->GetLineNumber(),
                                                                 param_name);
      method_call->SetVariable(variable);
      AnalyzeVariable(variable, entry, depth + 1);
    }
  }

  wstring ReplaceSubstring(wstring s, const wstring& f, const wstring &r) {
    const size_t index = s.find(f);
    if(index != string::npos) {
      s.replace(index, f.size(), r);
    }
    
    return s;
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
  void AnalyzeRightCast(Variable* variable, Expression* expression, bool is_scalar, const int depth);
  void AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, const int depth);
  void AnalyzeRightCast(Type* left, Type* right, Expression* expression, bool is_scalar, const int depth);
  void AnalyzeCalculation(CalculatedExpression* expression, const int depth);
  void AnalyzeCalculationCast(CalculatedExpression* expression, const int depth);
  void AnalyzeDeclaration(Declaration* declaration, Class* klass, const int depth);
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
