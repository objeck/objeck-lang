/***************************************************************************
 * Defines internal language types.
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

#ifndef __TYPES_H__
#define __TYPES_H__

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#ifndef _WIN32
#include <stdint.h>
#endif
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"

using namespace std;

namespace frontend {
  /****************************
   * ParseNode base class
   ****************************/
  class ParseNode {
    string file_name;
    int line_num;

  public:
    ParseNode(const string &f, const int l) {
      file_name = f;
      line_num = l;
    }
    
    virtual ~ParseNode() {
    }
    
    const string GetFileName() {
      return file_name;
    }

    const int GetLineNumber() {
      return line_num;
    }
  };

  /****************************
   * Entry types
   ****************************/
  typedef enum _EntryType {
    NIL_TYPE = -4000,
    BOOLEAN_TYPE,
    BYTE_TYPE,
    CHAR_TYPE,
    INT_TYPE,
    FLOAT_TYPE,
    CLASS_TYPE,
    FUNC_TYPE,
    VAR_TYPE
  } EntryType;

  /****************************
   * Method types
   ****************************/
  enum MethodType {
    NEW_PUBLIC_METHOD = -5000,
    NEW_PRIVATE_METHOD,
    PUBLIC_METHOD,
    PRIVATE_METHOD
  };

  /****************************
   * Method types
   ****************************/
  enum MethodCallType {
    ENUM_CALL = -6000,
    NEW_INST_CALL,
    NEW_ARRAY_CALL,
    METHOD_CALL,
    PARENT_CALL
  };

  /******************************
   * Type class
   ****************************/
  class Type {
    friend class TypeFactory;
    EntryType type;
    int dimension;
    string class_name;
    vector<Type*> func_params;
    Type* func_rtrn;
    int func_param_count;
    
    Type(Type* t) {
      if(t) {
	type = t->type;
	dimension = t->dimension;
	class_name = t->class_name;
	func_rtrn = t->func_rtrn;
	func_params = t->func_params;
	func_param_count = -1;
      }
    }
    
    Type(EntryType t) {
      type = t;
      dimension = 0;
      func_rtrn = NULL;
      func_param_count = -1;
    }

    Type(EntryType t, const string &n) {
      type = t;
      class_name = n;
      dimension = 0;
      func_rtrn = NULL;
      func_param_count = -1;
    }
    
    Type(vector<Type*>& p, Type* r) {
      type = FUNC_TYPE;
      dimension = 0;
      func_params = p;
      func_rtrn = r;
      func_param_count = -1;
    }
    
    ~Type() {
    }

  public:
    static Type* CharStringType();

    void SetType(EntryType t) {
      type = t;
    }

    const EntryType GetType() {
      return type;
    }

    void SetDimension(int d) {
      dimension = d;
    }
    
    vector<Type*>& GetFunctionParameters() {
      return func_params;
    }

    int GetFunctionParameterCount() {
      if(func_param_count < 0) {
	return func_params.size();
      }

      return func_param_count;
    }
    
    void SetFunctionParameterCount(int c) {
      func_param_count = c;
    }
    
    Type* GetFunctionReturn() {
      return func_rtrn;
    }

    void SetFunctionReturn(Type* r) {
      func_rtrn = r;
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

  /******************************
   * TypeFactory class
   ****************************/
  class TypeFactory {
    static TypeFactory* instance;
    vector<Type*> types;

    TypeFactory() {
    }

    ~TypeFactory() {
    }

  public:
    static TypeFactory* Instance();

    void Clear() {
      while(!types.empty()) {
	Type* tmp = types.front();
	types.erase(types.begin());
	// delete
	delete tmp;
	tmp = NULL;
      }

      delete instance;
      instance = NULL;
    }

    Type* MakeType(EntryType type) {
      Type* tmp = new Type(type);
      types.push_back(tmp);
      return tmp;
    }

    Type* MakeType(EntryType type, const string &name) {
      Type* tmp = new Type(type, name);
      types.push_back(tmp);
      return tmp;
    }
    
    Type* MakeType(vector<Type*>& func_params, Type* rtrn_type) {
      Type* tmp = new Type(func_params, rtrn_type);
      types.push_back(tmp);
      return tmp;
    }
    
    Type* MakeType(Type* type) {
      Type* tmp = new Type(type);
      types.push_back(tmp);
      return tmp;
    }
  };
  
  // static array holders
  typedef struct _IntStringHolder {
    INT_VALUE* value;
    int length;
  } IntStringHolder;
  
  typedef struct _FloatStringHolder {
    FLOAT_VALUE* value;
    int length;
  } FloatStringHolder;
}

namespace backend {
  /****************************
   * IntermediateDeclaration
   * class
   ****************************/
  class IntermediateDeclaration {
    instructions::ParamType type;
    string name;

  public:
    IntermediateDeclaration(const string &n, instructions::ParamType t) {
      type = t;
      name = n;
    }

    instructions::ParamType GetType() {
      return type;
    }

    const string GetName() {
      return name;
    }
  };

  /****************************
   * IntermediateDeclarations
   * class
   ****************************/
  class IntermediateDeclarations {
    vector<IntermediateDeclaration*> declarations;

    void WriteInt(int value, ofstream* file_out) {
      file_out->write((char*)&value, sizeof(value));
    }

    void WriteString(const string &value, ofstream* file_out) {
      int size = (int)value.size();
      file_out->write((char*)&size, sizeof(size));
      file_out->write(value.c_str(), value.size());
    }

  public:
    IntermediateDeclarations() {
    }
  
    ~IntermediateDeclarations() {
      while(!declarations.empty()) {
	IntermediateDeclaration* tmp = declarations.front();
	declarations.erase(declarations.begin());
	// delete
	delete tmp;
	tmp = NULL;
      }
    }

    void AddParameter(IntermediateDeclaration* parameter) {
      declarations.push_back(parameter);
    }

    vector<IntermediateDeclaration*> GetParameters() {
      return declarations;
    }
    
    void Debug() {
      if(declarations.size() > 0) {	 
	cout << "memory types:" << endl;	 
	for(size_t i = 0; i < declarations.size(); i++) {	 
	  IntermediateDeclaration* entry = declarations[i];	 
 	 
	  switch(entry->GetType()) {	 
	  case instructions::INT_PARM:	 
	    cout << "  " << i << ": INT_PARM" << endl;	 
	    break;	 
 	 
	  case instructions::FLOAT_PARM:	 
	    cout << "  " << i << ": FLOAT_PARM" << endl;	 
	    break;	 
 	 
	  case instructions::BYTE_ARY_PARM:	 
	    cout << "  " << i << ": BYTE_ARY_PARM" << endl;	 
	    break;	 
 	 
	  case instructions::INT_ARY_PARM:	 
	    cout << "  " << i << ": INT_ARY_PARM" << endl;	 
	    break;	 
 	 
	  case instructions::FLOAT_ARY_PARM:	 
	    cout << "  " << i << ": FLOAT_ARY_PARM" << endl;	 
	    break;	 
 	 
	  case instructions::OBJ_PARM:	 
	    cout << "  " << i << ": OBJ_PARM" << endl;	 
	    break;
	     
	  case instructions::OBJ_ARY_PARM:	 
	    cout << "  " << i << ": OBJ_ARY_PARM" << endl;	 
	    break;
	    
	  case instructions::FUNC_PARM:	 
	    cout << "  " << i << ": FUNC_PARM" << endl;	 
	    break;
 	 
	  default:	 
	    break;	 
	  }	 
	}	 
      }	
      else {
	cout << "memory types: none" << endl;
      }
    }
    
    void Write(bool is_debug, ofstream* file_out) {
      WriteInt((int)declarations.size(), file_out);
      for(size_t i = 0; i < declarations.size(); i++) {
	IntermediateDeclaration* entry = declarations[i];
	WriteInt(entry->GetType(), file_out);
	if(is_debug) {
	  WriteString(entry->GetName(), file_out);
	}
      }
    }
  };
}

#endif
