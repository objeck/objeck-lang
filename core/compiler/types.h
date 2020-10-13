/***************************************************************************
 * Defines internal language types.
 *
 * Copyright (c) 2008-2020, Randy Hollines
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

#ifndef __TYPES_H__
#define __TYPES_H__

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <assert.h>
#ifndef _WIN32
#include <stdint.h>
#include <stdlib.h>
#else
#include <windows.h>
#endif
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"

#ifdef _DEBUG
#include "../shared/logger.h"
#endif

using namespace std;

namespace frontend {
  /****************************
   * ParseNode base class
   ****************************/
  class ParseNode {
  protected:
    wstring file_name;
    int line_num;

  public:
    ParseNode(const wstring &f, const int l) {
      file_name = f;
      line_num = l;
    }
    
    virtual ~ParseNode() {
    }
    
    const wstring GetFileName() {
      return file_name;
    }

    const int GetLineNumber() {
      return line_num;
    }
  };

  /****************************
   * Entry types
   ****************************/
  enum EntryType {
    NIL_TYPE = -4000,
    BOOLEAN_TYPE,
    BYTE_TYPE,
    CHAR_TYPE,
    INT_TYPE,
    FLOAT_TYPE,
    CLASS_TYPE,
    FUNC_TYPE,
    ALIAS_TYPE,
    VAR_TYPE
  };

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
    wstring class_name;
    vector<Type*> func_params;
    Type* func_rtrn;
    int func_param_count;
    vector<Type*> generic_types;
    wstring file_name;
    int line_num;

    bool is_resolved;
    void* klass_cache_ptr;
    void* lib_klass_cache_ptr;
    
    Type(Type* t) {
#ifdef _DEBUG
      assert(t);
#endif
      if(t) {
        type = t->type;
        dimension = t->dimension;
        class_name = t->class_name;
        func_rtrn = t->func_rtrn;
        func_params = t->func_params;
        func_param_count = -1;
        line_num = t->line_num;
        generic_types = t->generic_types;
        is_resolved = t->is_resolved;
        klass_cache_ptr = t->klass_cache_ptr;
        lib_klass_cache_ptr = t->lib_klass_cache_ptr;
      } 
    }
    
    Type(EntryType t) {
      type = t;
      dimension = 0;
      func_rtrn = nullptr;
      func_param_count = -1;
      line_num = -1;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    Type(EntryType t, const wstring &n, const wstring& f, int l) {
      type = t;
      class_name = n;
      dimension = 0;
      func_rtrn = nullptr;
      func_param_count = -1;
      file_name = f;
      line_num = l;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    Type(EntryType t, const wstring& n) {
      type = t;
      class_name = n;
      dimension = 0;
      func_rtrn = nullptr;
      func_param_count = -1;
      line_num = -1;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    Type(vector<Type*>& p, Type* r) {
      type = FUNC_TYPE;
      dimension = 0;
      func_params = p;
      func_rtrn = r;
      func_param_count = -1;
      line_num = -1;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    ~Type() {
    }

  public:
    static Type* CharStringType();

    void SetType(EntryType t) {
      type = t;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    const EntryType GetType() {
      return type;
    }

    void SetGenerics(const vector<Type*>& g) {
      generic_types = g;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    vector<Type*> GetGenerics() {
      return generic_types;
    }

    bool HasGenerics() {
      return generic_types.size() > 0;
    }

    void SetDimension(int d) {
      dimension = d;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    void SetResolved(bool r) {
      is_resolved = r;
    }
    
    bool IsResolved() {
      return is_resolved;
    }

    void SetClassPtr(void* k) {
      klass_cache_ptr = k;
    }

    void* GetClassPtr() {
      return klass_cache_ptr;
    }

    void SetLibraryClassPtr(void* l) {
      lib_klass_cache_ptr = l;
    }

    void* GetLibraryClassPtr() {
      return lib_klass_cache_ptr;
    }

    vector<Type*> GetFunctionParameters() {
      return func_params;
    }

    void SetFunctionParameters(const vector<Type*> &p) {
      func_params = p;
    }

    int GetFunctionParameterCount() {
      if(func_param_count < 0) {
        return (int)func_params.size();
      }

      return func_param_count;
    }
    
    void SetFunctionParameterCount(int c) {
      func_param_count = c;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    Type* GetFunctionReturn() {
      return func_rtrn;
    }

    void SetFunctionReturn(Type* r) {
      func_rtrn = r;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }
    
    const int GetDimension() {
      return dimension;
    }

    void SetName(const wstring &n) {
      class_name = n;

      is_resolved = false;
      klass_cache_ptr = nullptr;
      lib_klass_cache_ptr = nullptr;
    }

    const wstring GetName() {
      return class_name;
    }

    inline void Set(Type* t) {
      if(t) {
        type = t->type;
        dimension = t->dimension;
        class_name = t->class_name;
        func_rtrn = t->func_rtrn;
        func_params = t->func_params;
        func_param_count = -1;
        line_num = t->line_num;
        generic_types = t->generic_types;
        is_resolved = t->is_resolved;
        klass_cache_ptr = t->klass_cache_ptr;
        lib_klass_cache_ptr = t->lib_klass_cache_ptr;
      }
    }

    const wstring GetFileName() {
      return file_name;
    }

    const int GetLineNumber() {
      return line_num;
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
        tmp = nullptr;
      }

      delete instance;
      instance = nullptr;
    }

    Type* MakeType(EntryType type) {
      Type* tmp = new Type(type);
      types.push_back(tmp);
      return tmp;
    }

    Type* MakeType(EntryType type, const wstring &name) {
      Type* tmp = new Type(type, name);
      types.push_back(tmp);
      return tmp;
    }

    Type* MakeType(EntryType type, const wstring& name, const wstring& file_name, int line_num) {
      Type* tmp = new Type(type, name, file_name, line_num);
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

    vector<Type*>& GetTypes() {
      return types;
    }
  };

  /********************************
   * Routines for parsing library
   * encode strings
   ********************************/
  class TypeParser {
  public:
    static vector<frontend::Type*> ParseParameters(const wstring& param_str);
    static frontend::Type* ParseType(const wstring& type_name);
    static void ParseFunctionalType(frontend::Type* func_type);
  };
  
  // static array holders
  struct IntStringHolder {
    INT_VALUE* value;
    int length;
  };
  
  struct FloatStringHolder {
    FLOAT_VALUE* value;
    int length;
  };
}

namespace backend {
  /****************************
   * IntermediateDeclaration
   * class
   ****************************/
  class IntermediateDeclaration {
    instructions::ParamType type;
    wstring name;

  public:
    IntermediateDeclaration(const wstring &n, instructions::ParamType t) {
      type = t;
      name = n;
    }

    instructions::ParamType GetType() {
      return type;
    }

    const wstring GetName() {
      return name;
    }
  };

  /****************************
   * IntermediateDeclarations
   * class
   ****************************/
  class IntermediateDeclarations {
    vector<IntermediateDeclaration*> declarations;

    void WriteInt(int value, OutputStream &out_stream) {
      out_stream.WriteInt(value);
    }

    void WriteString(const wstring &in, OutputStream &out_stream) {
      out_stream.WriteString(in);
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
        tmp = nullptr;
      }
    }

    void AddParameter(IntermediateDeclaration* parameter) {
      declarations.push_back(parameter);
    }

    vector<IntermediateDeclaration*> GetParameters() {
      return declarations;
    }
    
    void Debug(bool has_and_or) {
      if(declarations.size() > 0) {
        size_t index = has_and_or ? 1 : 0;
        GetLogger() << L"memory types:" << endl;
        if(has_and_or) {
          GetLogger() << L"  0: INT_PARM" << endl;
        }

        for(size_t i = 0; i < declarations.size(); ++i, ++index) {   
          IntermediateDeclaration* entry = declarations[i];   
    
          switch(entry->GetType()) {  
          case instructions::CHAR_PARM:   
            GetLogger() << L"  " << index << L": CHAR_PARM" << endl;   
            break;
      
          case instructions::INT_PARM:   
            GetLogger() << L"  " << index << L": INT_PARM" << endl;   
            break;   
    
          case instructions::FLOAT_PARM:   
            GetLogger() << L"  " << index << L": FLOAT_PARM" << endl;   
            break;   
    
          case instructions::BYTE_ARY_PARM:   
            GetLogger() << L"  " << index << L": BYTE_ARY_PARM" << endl;   
            break;   
    
          case instructions::CHAR_ARY_PARM:   
            GetLogger() << L"  " << index << L": CHAR_ARY_PARM" << endl;   
            break;

          case instructions::INT_ARY_PARM:   
            GetLogger() << L"  " << index << L": INT_ARY_PARM" << endl;   
            break;   
    
          case instructions::FLOAT_ARY_PARM:   
            GetLogger() << L"  " << index << L": FLOAT_ARY_PARM" << endl;   
            break;   
    
          case instructions::OBJ_PARM:   
            GetLogger() << L"  " << index << L": OBJ_PARM" << endl;   
            break;
       
          case instructions::OBJ_ARY_PARM:   
            GetLogger() << L"  " << index << L": OBJ_ARY_PARM" << endl;   
            break;
      
          case instructions::FUNC_PARM:   
            GetLogger() << L"  " << index << L": FUNC_PARM" << endl;   
            break;
    
          default:   
            break;   
          }   
        }   
      }  
      else {
        GetLogger() << L"memory types: none" << endl;
      }
    }

    void Write(bool is_debug, OutputStream &out_stream) {
      WriteInt((int)declarations.size(), out_stream);
      for(size_t i = 0; i < declarations.size(); ++i) {
        IntermediateDeclaration* entry = declarations[i];
        WriteInt(entry->GetType(), out_stream);
        if(is_debug) {
          WriteString(entry->GetName(), out_stream);
        }
      }
    }
  };
}

#endif
