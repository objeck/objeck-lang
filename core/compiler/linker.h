/***************************************************************************
 * Links pre-compiled code into existing program.
 *
 * Copyright (c) 2008-2018, Randy Hollines
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
 * - Neither the name of the VM Team nor the names of its
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

#ifndef __LINKER_H__
#define __LINKER_H__

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "types.h"
#include "../shared/instrs.h"
#include "../shared/sys.h"

#ifdef _DEBUG
#include "../shared/logger.h"
#endif

using namespace std;

class Library;
class LibraryClass;
class LibraryEnum;

/********************************
 * LibraryInstr class.
 ********************************/
class LibraryInstr {
  instructions::InstructionType type;
  int operand;
  int operand2;
  int operand3;
  double operand4;
  wstring operand5;
  wstring operand6;
  int line_num;

 public:
  LibraryInstr(int l, instructions::InstructionType t) {
    line_num = l;
    type = t;
    operand = 0;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o) {
    line_num = l;
    type = t;
    operand = o;
  }

  LibraryInstr(int l, instructions::InstructionType t, double fo) {
    line_num = l;
    type = t;
    operand4 = fo;
    operand = 0;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o, int o2) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o, int o2, int o3) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
    operand3 = o3;
  }

  LibraryInstr(int l, instructions::InstructionType t, wstring o5) {
    line_num = l;
    type = t;
    operand5 = o5;
  }

  LibraryInstr(int l, instructions::InstructionType t, int o3, wstring o5, wstring o6) {
    line_num = l;
    type = t;
    operand3 = o3;
    operand5 = o5;
    operand6 = o6;
  }

  ~LibraryInstr() {
  }

  instructions::InstructionType GetType() {
    return type;
  }

  int GetLineNumber() {
    return line_num;
  }
  
  void SetType(instructions::InstructionType t) {
    type = t;
  }

  int GetOperand() {
    return operand;
  }

  int GetOperand2() {
    return operand2;
  }

  int GetOperand3() {
    return operand3;
  }

  void SetOperand(int o) {
    operand = o;
  }

  void SetOperand2(int o2) {
    operand2 = o2;
  }

  void SetOperand3(int o3) {
    operand3 = o3;
  }

  double GetOperand4() {
    return operand4;
  }

  const wstring& GetOperand5() const {
    return operand5;
  }

  const wstring& GetOperand6() const {
    return operand6;
  }
};

/******************************
 * LibraryMethod class
 ****************************/
class LibraryMethod {
  int id;
  wstring name;
  wstring user_name;
  wstring rtrn_name;
  frontend::Type* rtrn_type;
  vector<LibraryInstr*> instrs;
  LibraryClass* lib_cls;
  frontend::MethodType type;
  bool is_native;
  bool is_static;
  bool is_virtual;
  bool has_and_or;
  int num_params;
  int mem_size;
  vector<frontend::Type*> declarations;
  backend::IntermediateDeclarations* entries;
  
  void ParseParameters() {
    const wstring &method_name = name;
    size_t start = method_name.find_last_of(':'); 	
    if(start != wstring::npos) {
      const wstring &parameters = method_name.substr(start + 1);
      size_t index = 0;

      while(index < parameters.size()) {
	frontend::Type* type = NULL;
	int dimension = 0;
	switch(parameters[index]) {
	case 'l':
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::BOOLEAN_TYPE);
	  index++;
	  break;
	  
	case 'b':
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::BYTE_TYPE);
	  index++;
	  break;

	case 'i':
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::INT_TYPE);
	  index++;
	  break;

	case 'f':
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::FLOAT_TYPE);
	  index++;
	  break;

	case 'c':
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::CHAR_TYPE);
	  index++;
	  break;

	case 'n':
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::NIL_TYPE);
	  index++;
	  break;
	  
	case 'm': {
	  start = index;
	  while(index < parameters.size() && parameters[index] != '~') {
	    index++;
	  }
	  index++;
	  while(index < parameters.size() && parameters[index] != '*' && 
		parameters[index] != ',') {
	    index++;
	  }	
	  size_t end = index;
	  const wstring &name = parameters.substr(start, end - start);
	  // TODO: convenient alternative/kludge to paring the function types. This
	  // works because the contextual analyzer does string encoding and then 
	  // checking of function types.
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::FUNC_TYPE, name); 
	}
	  break;

	case 'o': {
	  index += 2;
	  start = index;
	  while(index < parameters.size() && parameters[index] != '*' && 
		parameters[index] != ',') {
	    index++;
	  }	
	  size_t end = index;
	  const wstring &name = parameters.substr(start, end - start);
	  type = frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE, name);               
	}
	  break;
	}
	
	// set dimension
	while(index < parameters.size() && parameters[index] == '*') {
	  dimension++;
	  index++;
	}
	
	if(type) {
	  type->SetDimension(dimension);
	}
	
	// add declaration
	declarations.push_back(type);
#ifdef _DEBUG
	assert(parameters[index] == ',');
#endif
	index++;
      }
    }
  }
  
  void ParseReturn() {
    size_t index = 0;
    switch(rtrn_name[index]) {
    case 'l':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::BOOLEAN_TYPE);
      index++;
      break;

    case 'b':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::BYTE_TYPE);
      index++;
      break;

    case 'i':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::INT_TYPE);
      index++;
      break;

    case 'f':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::FLOAT_TYPE);
      index++;
      break;

    case 'c':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::CHAR_TYPE);
      index++;
      break;

    case 'n':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::NIL_TYPE);
      index++;
      break;

    case 'm':
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::FUNC_TYPE);
      index++;
      break;

    case 'o':
      index = 2;
      while(index < rtrn_name.size() && rtrn_name[index] != '*') {
        index++;
      }
      rtrn_type = frontend::TypeFactory::Instance()->MakeType(frontend::CLASS_TYPE,
							      rtrn_name.substr(2, index - 2));
      break;
    }

    // set dimension
    int dimension = 0;
    while(index < rtrn_name.size() && rtrn_name[index] == '*') {
      dimension++;
      index++;
    }
    rtrn_type->SetDimension(dimension);
  }

  wstring ReplaceSubstring(wstring s, const wstring& f, const wstring &r) {
    const size_t index = s.find(f);
    if(index != string::npos) {
      s.replace(index, f.size(), r);
    }

    return s;
  }

  wstring EncodeUserType(frontend::Type* type) {
    wstring name;
    if(type) {
      // type
      switch(type->GetType()) {
      case frontend::BOOLEAN_TYPE:
        name = L"Bool";
        break;

      case frontend::BYTE_TYPE:
        name = L"Byte";
        break;

      case frontend::INT_TYPE:
        name = L"Int";
        break;

      case frontend::FLOAT_TYPE:
        name = L"Float";
        break;

      case frontend::CHAR_TYPE:
        name = L"Char";
        break;

      case frontend::NIL_TYPE:
        name = L"Nil";
        break;

      case frontend::VAR_TYPE:
        name = L"Var";
        break;

      case frontend::CLASS_TYPE:
        name = type->GetClassName();
        break;

      case frontend::FUNC_TYPE: {
        name = L'(';
        vector<frontend::Type*> func_params = type->GetFunctionParameters();
        for(size_t i = 0; i < func_params.size(); ++i) {
          name += EncodeUserType(func_params[i]);
        }
        name += L") ~ ";
        name += EncodeUserType(type->GetFunctionReturn());
      }
                                break;
      }

      // dimension
      for(int i = 0; i < type->GetDimension(); ++i) {
        name += L"[]";
      }
    }

    return name;
  }

  void EncodeUserName() {
    bool is_new_private = false;
    if(is_static) {
      user_name = L"function : ";
    }
    else {
      switch(type) {
      case frontend::NEW_PUBLIC_METHOD:
        break;

      case frontend::NEW_PRIVATE_METHOD:
        is_new_private = true;
        break;

      case frontend::PUBLIC_METHOD:
        user_name = L"method : public : ";
        break;

      case frontend::PRIVATE_METHOD:
        user_name = L"method : private : ";
        break;
      }
    }

    if(is_native) {
      user_name += L"native : ";
    }

    // name
    wstring method_name = name.substr(0, name.find_last_of(':'));
    user_name += ReplaceSubstring(method_name, L":", L"->");

    // private new
    if(is_new_private) {
      user_name += L" : private ";
    }

    // params
    user_name += L'(';

    for(size_t i = 0; i < declarations.size(); ++i) {
      user_name += EncodeUserType(declarations[i]);
      if(i + 1 < declarations.size()) {
        user_name += L", "; 
      }
    }
    user_name += L") ~ ";

    user_name += EncodeUserType(rtrn_type);
  }
  
 public:
  LibraryMethod(int i, const wstring &n, const wstring &r, frontend::MethodType t, bool v,  bool h,
                bool nt, bool s, int p, int m, LibraryClass* c, backend::IntermediateDeclarations* e) {
    id = i;
    name = n;
    rtrn_name = r;
    type = t;
    is_virtual = v;
    has_and_or = h;
    is_native = nt;
    is_static = s;
    num_params = p;
    mem_size = m;
    lib_cls = c;
    entries = e;
    rtrn_type = NULL;

    ParseParameters();
    ParseReturn();
  }

  ~LibraryMethod() {
    // clean up
    while(!instrs.empty()) {
      LibraryInstr* tmp = instrs.front();
      instrs.erase(instrs.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
  }

  int GetId() {
    return id;
  }

  bool IsVirtual() {
    return is_virtual;
  }

  bool HasAndOr() {
    return has_and_or;
  }

  const wstring& GetName() const {
    return name;
  }

  const wstring& GetUserName() {
    if(user_name.size() == 0) {
      EncodeUserName();
    }

    return user_name;
  }

  const wstring& GetEncodedReturn() const {
    return rtrn_name;
  }

  frontend::MethodType GetMethodType() {
    return type;
  }
  
  vector<frontend::Type*> GetDeclarationTypes() {
    return declarations;
  }
  
  int GetSpace() {
    return mem_size;
  }

  int GetNumParams() {
    return num_params;
  }

  backend::IntermediateDeclarations* GetEntries() {
    return entries;
  }
  
  bool IsNative() {
    return is_native;
  }

  bool IsStatic() {
    return is_static;
  }

  LibraryClass* GetLibraryClass() {
    return lib_cls;
  }

  void AddInstructions(vector<LibraryInstr*> is) {
    instrs = is;
  }

  frontend::Type* GetReturn() {
    return rtrn_type;
  }

  vector<LibraryInstr*> GetInstructions() {
    return instrs;
  }
};

/****************************
 * LibraryEnumItem class
 ****************************/
class LibraryEnumItem {
  wstring name;
  int id;
  LibraryEnum* lib_eenum;

 public:
  LibraryEnumItem(const wstring &n, int i, LibraryEnum* e) {
    name = n;
    id = i;
    lib_eenum = e;
  }

  ~LibraryEnumItem() {
  }

  const wstring& GetName() const {
    return name;
  }

  int GetId() {
    return id;
  }

  LibraryEnum* GetEnum() {
    return lib_eenum;
  }
};

/****************************
 * LibraryEnum class
 ****************************/
class LibraryEnum {
  wstring name;
  int offset;
  map<const wstring, LibraryEnumItem*> items;

 public:
  LibraryEnum(const wstring &n, const int o) {
    name = n;
    offset = o;
  }

  ~LibraryEnum() {
    // clean up
    map<const wstring, LibraryEnumItem*>::iterator iter;
    for(iter = items.begin(); iter != items.end(); ++iter) {
      LibraryEnumItem* tmp = iter->second;
      delete tmp;
      tmp = NULL;
    }
    items.clear();
  }

  const wstring& GetName() const {
    return name;
  }

  int GetOffset() {
    return offset;
  }

  void AddItem(LibraryEnumItem* i) {
    items.insert(pair<wstring, LibraryEnumItem*>(i->GetName(), i));
  }

  LibraryEnumItem* GetItem(const wstring &n) {
    map<const wstring, LibraryEnumItem*>::iterator result = items.find(n);
    if(result != items.end()) {
      return result->second;
    }

    return NULL;
  }

  map<const wstring, LibraryEnumItem*> GetItems() {
    return items;
  }
};

/****************************
 * LibraryClass class
 ****************************/
class LibraryClass {
  int id;
  wstring name;
  wstring parent_name;
  vector<wstring>interface_names;
  vector<int>interface_ids;
  int cls_space;
  int inst_space;
  map<const wstring, LibraryMethod*> methods;
  multimap<const wstring, LibraryMethod*> unqualified_methods;
  backend::IntermediateDeclarations* cls_entries;
  backend::IntermediateDeclarations* inst_entries;
  bool is_interface;
  bool is_virtual;
  Library* library;
  vector<LibraryClass*> lib_children;
  vector<frontend::ParseNode*> children;
  bool was_called;
  bool is_debug;
  wstring file_name;
  
 public:
  LibraryClass(const wstring &n, const wstring &p, vector<wstring> in, bool is_inf, bool is_vrtl, 
							 int cs, int is, backend::IntermediateDeclarations* ce, backend::IntermediateDeclarations* ie, 
							 Library* l, const wstring &fn, bool d) {
    name = n;
    parent_name = p;
    interface_names = in;
    is_interface = is_inf;
    is_virtual = is_vrtl;
    cls_space = cs;
    inst_space = is;
    cls_entries = ce;
    inst_entries = ie;
    library = l;
    
    // force runtime linking of these classes
    if(name == L"System.Introspection.Class" || 
       name == L"System.Introspection.Method" || 
       name == L"System.Introspection.DataType") {
      was_called = true;
    }
    else {
      was_called = false;
    }
    is_debug = d;
    file_name = fn;
  }

  ~LibraryClass() {
    // clean up
    map<const wstring, LibraryMethod*>::iterator iter;
    for(iter = methods.begin(); iter != methods.end(); ++iter) {
      LibraryMethod* tmp = iter->second;
      delete tmp;
      tmp = NULL;
    }
    methods.clear();
        
    lib_children.clear();
  }
  
  void SetId(int i) {
    id = i;
  }

  void SetCalled(bool c) {
    was_called = c;
  }

  bool GetCalled() {
    return was_called;
  }

  bool IsDebug() {
    return is_debug;
  }

  const wstring& GetFileName() const {
    return file_name;
  }
  
  int GetId() {
    return id;
  }

  const wstring& GetName() const {
    return name;
  }
  
  vector<wstring> GetInterfaceNames() {
    return interface_names;
  }

  vector<int> GetInterfaceIds() {
    return interface_ids;
  }
  
  void AddInterfaceId(int id) {
    interface_ids.push_back(id);
  }

  bool IsInterface() {
    return is_interface;
  }

  const wstring& GetParentName() const {
    return parent_name;
  }

  vector<frontend::ParseNode*> GetChildren() {
    return children;
  }

  void AddChild(frontend::ParseNode* c) {
    children.push_back(c);
  }

  vector<LibraryClass*> GetLibraryChildren();

  bool IsVirtual() {
    return is_virtual;
  }

  int GetClassSpace() {
    return cls_space;
  }

  int GetInstanceSpace() {
    return inst_space;
  }

  backend::IntermediateDeclarations* GetClassEntries() {
    return cls_entries;
  }
  
  backend::IntermediateDeclarations* GetInstanceEntries() {
    return inst_entries;
  }
  
  LibraryMethod* GetMethod(const wstring &name) {
    map<const wstring, LibraryMethod*>::iterator result = methods.find(name);
    if(result != methods.end()) {
      return result->second;
    }

    return NULL;
  }

  vector<LibraryMethod*> GetUnqualifiedMethods(const wstring &n) {
    vector<LibraryMethod*> results;
    pair<multimap<const wstring, LibraryMethod*>::iterator, 
      multimap<const wstring, LibraryMethod*>::iterator> result;
    result = unqualified_methods.equal_range(n);
    multimap<const wstring, LibraryMethod*>::iterator iter = result.first;
    for(iter = result.first; iter != result.second; ++iter) {
      results.push_back(iter->second);
    }
      
    return results;
  }

  map<const wstring, LibraryMethod*> GetMethods() {
    return methods;
  }

  void AddMethod(LibraryMethod* method) {
    const wstring &encoded_name = method->GetName();
    methods.insert(pair<const wstring, LibraryMethod*>(encoded_name, method));
    
    // add to unqualified names to list
    const size_t start = encoded_name.find(':');
    if(start != wstring::npos) {
      const size_t end = encoded_name.find(':', start + 1);
      if(end != wstring::npos) {
        const wstring &unqualified_name = encoded_name.substr(start + 1, end - start - 1);
        unqualified_methods.insert(pair<wstring, LibraryMethod*>(unqualified_name, method));
      }
      else {
        delete method;
        method = NULL;
      }
    }
    else {
      delete method;
      method = NULL;
    }
  }
};

/******************************
 * Library class
 ****************************/
typedef struct _CharStringInstruction {
  wstring value;
  vector<LibraryInstr*> instrs;
} CharStringInstruction;

typedef struct _IntStringInstruction {
  frontend::IntStringHolder* value;
  vector<LibraryInstr*> instrs;
} IntStringInstruction;

typedef struct _FloatStringInstruction {
  frontend::FloatStringHolder* value;
  vector<LibraryInstr*> instrs;
} FloatStringInstruction;

class Library {
  wstring lib_path;
  char* buffer;
  char* alloc_buffer;
  size_t buffer_size;
  long buffer_pos;
  map<const wstring, LibraryEnum*> enums;
  vector<LibraryEnum*> enum_list;
  map<const wstring, LibraryClass*> named_classes;
  vector<LibraryClass*> class_list;
  map<const wstring, const wstring> hierarchies;
  vector<CharStringInstruction*> char_strings;
  vector<IntStringInstruction*> int_strings;
  vector<FloatStringInstruction*> float_strings;
  vector<wstring> bundle_names;
  
  inline int ReadInt() {
    int32_t value = *((int32_t*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline void ReadDummyInt() {
    buffer += sizeof(int32_t);
  }

  inline uint32_t ReadUnsigned() {
    uint32_t value = *((uint32_t*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline int ReadByte() {
    char value = *((char*)buffer);
    buffer += sizeof(value);
    return value;
  }

  inline wchar_t ReadChar() {
    wchar_t out;
    
    int size = ReadInt(); 
    if(size) {
      string in(buffer, size);
      buffer += size;
      if(!BytesToCharacter(in, out)) {
	      wcerr << L">>> Unable to read character <<<" << endl;
	      exit(1);
      }
    }
    else {
      out = L'\0';
    }
    
    return out;
  }
  
  inline wstring ReadString() {
    const int size = ReadInt(); 
    string in(buffer, size);
    buffer += size;    
   
    wstring out;
    if(!BytesToUnicode(in, out)) {
      wcerr << L">>> Unable to read unicode string <<<" << endl;
      exit(1);
    }

    return out;
  }

  inline void ReadDummyString() {
    buffer += ReadInt();
  }

  double ReadDouble() {
    FLOAT_VALUE value = *((FLOAT_VALUE*)buffer);
    buffer += sizeof(value);
    return value;
  }

  // loads a file into memory
  char* LoadFileBuffer(wstring filename, size_t& buffer_size) {
    char* buffer = NULL;
    // open file
    const string open_filename = UnicodeToBytes(filename);
    ifstream in(open_filename.c_str(), ifstream::binary);
    if(in.good()) {
      // get file size
      in.seekg(0, ios::end);
      buffer_size = (size_t)in.tellg();
      in.seekg(0, ios::beg);
      buffer = (char*)calloc(buffer_size + 1, sizeof(char));
      in.read(buffer, buffer_size);
      // close file
      in.close();
      
      uLong dest_len;
      char* out = OutputStream::Uncompress(buffer, (uLong)buffer_size, dest_len);
      if(!out) {
        wcerr << L"Unable to uncompress file: " << filename << endl;
        exit(1);
      }
#ifdef _DEBUG
      GetLogger() << L"--- file in: compressed=" << buffer_size << L", uncompressed=" << dest_len << L" ---" << std::endl;
#endif

      free(buffer);
      buffer = NULL;      
      return out;
    } 
    else {
      wcerr << L"Unable to open file: " << filename << endl;
      exit(1);
    }
    
    return NULL;
  }

  void ReadFile(const wstring &file) {
    buffer_pos = 0;
    alloc_buffer = buffer = LoadFileBuffer(file, buffer_size);
  }

  void AddEnum(LibraryEnum* e) {
    enums.insert(pair<wstring, LibraryEnum*>(e->GetName(), e));
    enum_list.push_back(e);
  }
  
  void AddClass(LibraryClass* cls) {
    if(cls->GetName() == L"System.wstring") {
      cls->SetCalled(true);
    }    
    named_classes.insert(pair<wstring, LibraryClass*>(cls->GetName(), cls));
    class_list.push_back(cls);
  }

  backend::IntermediateDeclarations* LoadEntries(bool is_debug) {
    backend::IntermediateDeclarations* entries = new backend::IntermediateDeclarations;
    int num_params = ReadInt();
    for(int i = 0; i < num_params; ++i) {
      instructions::ParamType type = (instructions::ParamType)ReadInt();
      wstring var_name;
      if(is_debug) {
	var_name = ReadString();
      }
      entries->AddParameter(new backend::IntermediateDeclaration(var_name, type));
    }

    return entries;
  }

  
  // loading functions
  void LoadFile(const wstring &file_name);
  void LoadEnums();
  void LoadClasses();
  void LoadMethods(LibraryClass* cls, bool is_debug);
  void LoadStatements(LibraryMethod* mthd, bool is_debug);

 public:
  Library(const wstring &p) {
    lib_path = p;
    alloc_buffer = NULL;
  }

  ~Library() {
    // clean up
    map<const wstring, LibraryEnum*>::iterator enum_iter;
    for(enum_iter = enums.begin(); enum_iter != enums.end(); ++enum_iter) {
      LibraryEnum* tmp = enum_iter->second;
      delete tmp;
      tmp = NULL;
    }
    enums.clear();
    enum_list.clear();

    map<const wstring, LibraryClass*>::iterator cls_iter;
    for(cls_iter = named_classes.begin(); cls_iter != named_classes.end(); ++cls_iter) {
      LibraryClass* tmp = cls_iter->second;
      delete tmp;
      tmp = NULL;
    }
    named_classes.clear();
    class_list.clear();

    while(!char_strings.empty()) {
      CharStringInstruction* tmp = char_strings.front();
      char_strings.erase(char_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    while(!int_strings.empty()) {
      IntStringInstruction* tmp = int_strings.front();
      int_strings.erase(int_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    while(!float_strings.empty()) {
      FloatStringInstruction* tmp = float_strings.front();
      float_strings.erase(float_strings.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }

    if(alloc_buffer) {
      delete[] alloc_buffer;
      alloc_buffer = NULL;
    }
  }
  
  bool HasBundleName(const wstring& name) {
    vector<wstring>::iterator found = find(bundle_names.begin(), bundle_names.end(), name);
    return found != bundle_names.end();
  }

  LibraryClass* GetClass(const wstring &name) {
    map<const wstring, LibraryClass*>::iterator result = named_classes.find(name);
    if(result != named_classes.end()) {
      return result->second;
    }

    return NULL;
  }

  LibraryEnum* GetEnum(const wstring &name) {
    map<const wstring, LibraryEnum*>::iterator result = enums.find(name);
    if(result != enums.end()) {
      return result->second;
    }

    return NULL;
  }

  vector<LibraryEnum*> GetEnums() {
    return enum_list;
  }

  vector<LibraryClass*> GetClasses() {
    return class_list;
  }

  map<const wstring, const wstring> GetHierarchies() {
    return hierarchies;
  }

  vector<CharStringInstruction*> GetCharStringInstructions() {
    return char_strings;
  }

  vector<IntStringInstruction*> GetIntStringInstructions() {
    return int_strings;
  }
  
  vector<FloatStringInstruction*> GetFloatStringInstructions() {
    return float_strings;
  }

  void Load();
};

/********************************
 * Manages shared libraries
 ********************************/
class Linker {
  map<const wstring, Library*> libraries;
  wstring master_path;
  vector<wstring> paths;

 public:
#ifdef _DEBUG
  static void Debug(const wstring &msg, const int line_num, int depth) {
    GetLogger() << setw(4) << line_num << L": ";
    for(int i = 0; i < depth; ++i) {
      GetLogger() << L"  ";
    }
    GetLogger() << msg << endl;
  }
#endif

  static wstring ToString(int v) {
    wostringstream str;
    str << v;
    return str.str();
  }

 public:
  Linker(const wstring& p) {
    master_path = p;
  }

  ~Linker() {
    // clean up
    map<const wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      Library* tmp = iter->second;
      delete tmp;
      tmp = NULL;
    }
    libraries.clear();

    paths.clear();
  }

  void ResloveExternalClass(LibraryClass* klass);
  void ResloveExternalClasses();
  void ResolveExternalMethodCalls();

  vector<wstring> GetLibraryPaths() {
    return paths;
  }

  Library* GetLibrary(const wstring &name) {
    map<const wstring, Library*>::iterator result = libraries.find(name);
    if(result != libraries.end()) {
      return result->second;
    }

    return NULL;
  }

  // get all libaries
  map<const wstring, Library*> GetAllLibraries() {
    return libraries;
  }

  // returns all classes including duplicates
  vector<LibraryClass*> GetAllClasses() {
    vector<LibraryClass*> all_libraries;

    map<const wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      vector<LibraryClass*> classes = iter->second->GetClasses();
      for(size_t i = 0; i < classes.size(); ++i) {
        all_libraries.push_back(classes[i]);
      }
    }

    return all_libraries;
  }

  // returns all enums including duplicates
  vector<LibraryEnum*> GetAllEnums() {
    vector<LibraryEnum*> all_libraries;

    map<const wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      vector<LibraryEnum*> enums = iter->second->GetEnums();
      for(size_t i = 0; i < enums.size(); ++i) {
        all_libraries.push_back(enums[i]);
      }
    }

    return all_libraries;
  }

  // TODO: finds the first class match; note multiple matches may exist
  LibraryClass* SearchClassLibraries(const wstring &name) {
    vector<LibraryClass*> classes = GetAllClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      if(classes[i]->GetName() == name) {
        return classes[i];
      }
    }

    return NULL;
  }

  bool HasBundleName(const wstring& name) {
    map<const wstring, Library*>::iterator iter;
    for(iter = libraries.begin(); iter != libraries.end(); ++iter) {
      if(iter->second->HasBundleName(name)) {
        return true;
      }
    }

    return false;
  }

  // TODO: finds the first class match; note multiple matches may exist
  LibraryClass* SearchClassLibraries(const wstring &name, vector<wstring> uses) {
    vector<LibraryClass*> classes = GetAllClasses();
    for(size_t i = 0; i < classes.size(); ++i) {
      if(classes[i]->GetName() == name) {
        return classes[i];
      }
    }

    for(size_t i = 0; i < classes.size(); ++i) {
      for(size_t j = 0; j < uses.size(); ++j) {
        if(classes[i]->GetName() == uses[j] + L"." + name) {
          return classes[i];
        }
      }
    }

    return NULL;
  }

  // TODO: finds the first enum match; note multiple matches may exist
  LibraryEnum* SearchEnumLibraries(const wstring &name, vector<wstring> uses) {
    vector<LibraryEnum*> enums = GetAllEnums();
    for(size_t i = 0; i < enums.size(); ++i) {
      if(enums[i]->GetName() == name) {
        return enums[i];
      }
    }

    for(size_t i = 0; i < enums.size(); ++i) {
      for(size_t j = 0; j < uses.size(); ++j) {
        if(enums[i]->GetName() == uses[j] + L"." + name) {
          return enums[i];
        }
      }
    }

    return NULL;
  }

  void Load() {
#ifdef _DEBUG
    GetLogger() << L"--------- Linking Libraries ---------" << endl;
#endif

    // set library path
    wstring path = GetLibraryPath();

    // parses library path
    if(master_path.size() > 0) {
      size_t offset = 0;
      size_t index = master_path.find(',');
      while(index != wstring::npos) {
        // load library
        const wstring &file = master_path.substr(offset, index - offset);
        const wstring file_path = path + file;
        Library* library = new Library(file_path);
        library->Load();
        // insert library
        libraries.insert(pair<wstring, Library*>(file_path, library));
        vector<wstring>::iterator found = find(paths.begin(), paths.end(), file_path);
        if(found == paths.end()) {
          paths.push_back(file_path);
        }
        // update
        offset = index + 1;
        index = master_path.find(',', offset);
      }
      // insert library
      const wstring &file = master_path.substr(offset, master_path.size());
      const wstring file_path = path + file;
      Library* library = new Library(file_path);
      library->Load();
      libraries.insert(pair<wstring, Library*>(file_path, library));
      paths.push_back(file_path);
#ifdef _DEBUG
      GetLogger() << L"--------- End Linking ---------" << endl;
#endif
    }

  }
};

#endif
