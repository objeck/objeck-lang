/***************************************************************************
 * Defines the VM execution model.
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
 * - Neither the name of the Objeck Team nor the names of its
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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <stack>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"

#ifndef _UTILS
#include <openssl/bio.h>
#include <openssl/ssl.h>
#endif

#if defined(_WIN32) && !defined(_MINGW)
#include <windows.h>
#include <unordered_map>
using namespace stdext;
#else
#include <tr1/unordered_map>
#include <pthread.h>
#include <stdint.h>
namespace std {
  using namespace tr1;
}
#ifndef _MINGW
#include <dlfcn.h>
#endif
#endif

#define SMALL_BUFFER_MAX 255

using namespace std;
using namespace instructions;

class StackClass;

inline string IntToString(int v)
{
  ostringstream str;
  str << v;
  return str.str();
}

/********************************
 * StackDclr struct
 ********************************/
struct StackDclr 
{
  string name;
  ParamType type;
  long id;
};

/********************************
 * StackInstr class
 ********************************/
class StackInstr 
{
  InstructionType type;
  long operand;
  long operand2;
  long operand3;
  FLOAT_VALUE float_operand;
  long native_offset;
  int line_num;
  
 public:
  StackInstr(int l, InstructionType t) {
    line_num = l;
    type = t;
    operand = operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o) {
    line_num = l;
    type = t;
    operand = o;
    operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, FLOAT_VALUE fo) {
    line_num = l;
    type = t;
    float_operand = fo;
    operand = operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o, long o2) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
    operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o, long o2, long o3) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
    operand3 = o3;
    native_offset = 0;
  }

  ~StackInstr() {
  }  

  inline InstructionType GetType() const {
    return type;
  }

  int GetLineNumber() const {
    return line_num;
  }

  inline void SetType(InstructionType t) {
    type = t;
  }

  inline long GetOperand() const {
    return operand;
  }

  inline long GetOperand2() const {
    return operand2;
  }

  inline long GetOperand3() const {
    return operand3;
  }

  inline void SetOperand(long o) {
    operand = o;
  }

  inline void SetOperand2(long o2) {
    operand2 = o2;
  }

  inline void SetOperand3(long o3) {
    operand3 = o3;
  }

  inline FLOAT_VALUE GetFloatOperand() const {
    return float_operand;
  }

  inline long GetOffset() const {
    return native_offset;
  }

  inline void SetOffset(long o) {
    native_offset = o;
  }
};

/********************************
 * JIT compile code
 ********************************/
class NativeCode 
{
  BYTE_VALUE* code;
  long size;
  FLOAT_VALUE* floats;

 public:
  NativeCode(BYTE_VALUE* c, long s, FLOAT_VALUE* f) {
    code = c;
    size = s;
    floats = f;
  }

  ~NativeCode() {
#ifdef _WIN32
    // TODO: needs to be fixed... hard to debug
    free(code);
    code = NULL;
#endif

#ifdef _X64
    free(code);
    code = NULL;
#endif

#ifndef _WIN32
    free(floats);
#else
    delete[] floats;
#endif
    floats = NULL;
  }

  BYTE_VALUE* GetCode() const {
    return code;
  }

  long GetSize() {
    return size;
  }
  
  FLOAT_VALUE* GetFloats() const {
    return floats;
  }
};

/********************************
 * StackMethod class
 ********************************/
class StackMethod {
  long id;
  string name;
  bool is_virtual;
  bool has_and_or;
  StackInstr** instrs;  
  int instr_count;  
  unordered_map<long, long> jump_table;
  long param_count;
  long mem_size;
  NativeCode* native_code;
  MemoryType rtrn_type;
  StackDclr** dclrs;
  long num_dclrs;
  StackClass* cls;
#ifdef _WIN32
  static CRITICAL_SECTION virutal_cs;
#else 
  static pthread_mutex_t virtual_mutex;
#endif
  static unordered_map<string, StackMethod*> virutal_cache;
  
  const string& ParseName(const string &name) const {
    int state;
    size_t index = name.find_last_of(':');
    if(index > 0) {
      string params_name = name.substr(index + 1);

      // check return type
      index = 0;
      while(index < params_name.size()) {
#ifdef _DEBUG
        ParamType param;
#endif
	
        switch(params_name[index]) {
	  // bool
        case 'l':
#ifdef _DEBUG
          param = INT_PARM;
#endif
          state = 0;
          index++;
          break;

	  // byte
        case 'b':
#ifdef _DEBUG
          param = INT_PARM;
#endif
          state = 1;
          index++;
          break;

	  // int
        case 'i':
#ifdef _DEBUG
          param = INT_PARM;
#endif
          state = 2;
          index++;
          break;

	  // float
        case 'f':
#ifdef _DEBUG
          param = FLOAT_PARM;
#endif
          state = 3;
          index++;
          break;

	  // char
        case 'c':
#ifdef _DEBUG
          param = INT_PARM;
#endif
          state = 4;
          index++;
          break;

	  // obj
        case 'o':
#ifdef _DEBUG
          param = OBJ_PARM;
#endif
          state = 5;
          index++;
          while(index < params_name.size() && params_name[index] != ',') {
            index++;
          }
          break;

	  // func
	case 'm':
#ifdef _DEBUG
          param = FUNC_PARM;
#endif
          state = 6;
          index++;
          while(index < params_name.size() && params_name[index] != '~') {
            index++;
          }
	  while(index < params_name.size() && params_name[index] != ',') {
            index++;
          }
          break;
	  
	default:
#ifdef _DEBUG
	  assert(false);
#endif
	  break;
        }
	
        // check array
        int dimension = 0;
        while(index < params_name.size() && params_name[index] == '*') {
          dimension++;
          index++;
        }

        if(dimension) {
          switch(state) {
          case 0:
          case 1:
          case 4:
#ifdef _DEBUG
            param = BYTE_ARY_PARM;
#endif
            break;
	    
          case 2:
#ifdef _DEBUG
            param = INT_ARY_PARM;
#endif
            break;

          case 3:
#ifdef _DEBUG
            param = FLOAT_ARY_PARM;
#endif
            break;

          case 5:
#ifdef _DEBUG
            param = OBJ_ARY_PARM;
#endif
            break;
          }
        }
	
#ifdef _DEBUG
        switch(param) {
        case INT_PARM:
          cout << "  INT_PARM" << endl;
          break;

        case FLOAT_PARM:
          cout << "  FLOAT_PARM" << endl;
          break;

        case BYTE_ARY_PARM:
          cout << "  BYTE_ARY_PARM" << endl;
          break;

        case INT_ARY_PARM:
          cout << "  INT_ARY_PARM" << endl;
          break;

        case FLOAT_ARY_PARM:
          cout << "  FLOAT_ARY_PARM" << endl;
          break;

        case OBJ_PARM:
          cout << "  OBJ_PARM" << endl;
          break;

        case OBJ_ARY_PARM:
          cout << "  OBJ_ARY_PARM" << endl;
          break;

	case FUNC_PARM:
          cout << "  FUNC_PARM" << endl;
          break;

	default:
	  assert(false);
	  break;
        }
#endif

        // match ','
        index++;
      }
    }

    return name;
  }
  
 public:
  // mutex variable used to support 
  // concurrent JIT compiling
#ifdef _WIN32
  CRITICAL_SECTION jit_cs;
#else 
  pthread_mutex_t jit_mutex;
#endif

  StackMethod(long i, const string &n, bool v, bool h, StackDclr** d, long nd,
              long p, long m, MemoryType r, StackClass* k) {
#ifdef _WIN32
    InitializeCriticalSection(&jit_cs);
#else
    pthread_mutex_init(&jit_mutex, NULL);
#endif
    id = i;
    name = ParseName(n);
    is_virtual = v;
    has_and_or = h;
    native_code = NULL;
    dclrs = d;
    num_dclrs = nd;
    param_count = p;
    mem_size = m;
    rtrn_type = r;
    cls = k;
    instrs = NULL;
    instr_count = 0;
  }
  
  ~StackMethod() {
    // clean up
    if(dclrs) {
      for(int i = 0; i < num_dclrs; i++) {
        StackDclr* tmp = dclrs[i];
        delete tmp;
        tmp = NULL;
      }
      delete[] dclrs;
      dclrs = NULL;
    }
    
#ifdef _WIN32
    DeleteCriticalSection(&jit_cs); 
#endif
    
    // clean up
    if(native_code) {
      delete native_code;
      native_code = NULL;
    }
    
    // clean up
    for(int i = 0; i < instr_count; i++) {
      StackInstr* tmp = instrs[i];
      delete tmp;
      tmp = NULL;
    }
    delete[] instrs;
    instrs = NULL;
  }
  
  inline const string& GetName() {
    return name;
  }

  inline bool IsVirtual() {
    return is_virtual;
  }

  inline bool HasAndOr() {
    return has_and_or;
  }

  inline StackClass* GetClass() const {
    return cls;
  }

#ifdef _DEBUGGER
  int GetLocalDeclaration(const string& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < num_dclrs; i++, index++) {
	StackDclr* dclr = dclrs[i];
	const string &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
	if(dclr_name == name) {
	  found.name = dclr->name;
	  found.type = dclr->type;
	  found.id = index;	  
	  return true;
	}
	
	if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
	  index++;
	}
      }
    }
    
    return false;
  }
#endif
  
  inline StackDclr** GetDeclarations() const {
    return dclrs;
  }

  inline const int GetNumberDeclarations() {
    return num_dclrs;
  }
  
  void SetNativeCode(NativeCode* c) {
    native_code = c;
  }

  NativeCode* GetNativeCode() const {
    return native_code;
  }

  MemoryType GetReturn() const {
    return rtrn_type;
  }
  
  inline void AddLabel(long label_id, long index) {
    jump_table.insert(pair<long, long>(label_id, index));
  }
  
  inline long GetLabelIndex(long label_id) {
    unordered_map<long, long>::iterator found = jump_table.find(label_id);
    if(found != jump_table.end()) {
      return found->second;
    }    
    
    return -1;
  }
  
  void SetInstructions(StackInstr** ii, int ic) {
    instrs = ii;
    instr_count = ic;
  }
  
  long GetId() const {
    return id;
  }

  long GetParamCount() const {
    return param_count;
  }

  void SetParamCount(long c) {
    param_count = c;
  }

  inline long* NewMemory() {
    // +1 is for instance variable
    const long size = mem_size + 2;
    long* mem = new long[size];
    memset(mem, 0, size * sizeof(long));

    return mem;
  }

  inline long GetMemorySize() const {
    return mem_size;
  }
  
  inline long GetInstructionCount() const {
    return instr_count;
  }

  inline StackInstr* GetInstruction(long i) const {
    return instrs[i];
  }

  StackInstr** GetInstructions() const {
    return instrs;
  }

#ifdef _WIN32
  static void InitVirtualEntry() {
    InitializeCriticalSection(&virutal_cs);
  }
#endif
  
  static void AddVirtualEntry(const string &key, StackMethod* mthd) { 
#ifdef _WIN32
    EnterCriticalSection(&virutal_cs);
#else
    pthread_mutex_lock(&virtual_mutex);
#endif
    
    virutal_cache.insert(pair<string, StackMethod*>(key, mthd));

#ifdef _WIN32
    LeaveCriticalSection(&virutal_cs);
#else
    pthread_mutex_unlock(&virtual_mutex);
#endif
  }

  static StackMethod* GetVirtualEntry(const string &key) {
    StackMethod* mthd = NULL;
#ifdef _WIN32
    EnterCriticalSection(&virutal_cs);
#else
    pthread_mutex_lock(&virtual_mutex);
#endif
    
    unordered_map<string, StackMethod*>::iterator found = virutal_cache.find(key);
    if(found != virutal_cache.end()) {
      mthd = found->second;
    }    
    
#ifdef _WIN32
    LeaveCriticalSection(&virutal_cs);
#else
    pthread_mutex_unlock(&virtual_mutex);
#endif

    return mthd;
  }

#ifdef _DEBUGGER
  static void ClearVirtualEntries() {
    virutal_cache.clear();
  }
#endif
};

/********************************
 * ByteBuffer class
 ********************************/
class ByteBuffer {
  BYTE_VALUE* buffer;
  int pos;
  int max;

 public:
  ByteBuffer() {
    pos = 0;
    max = 4;
    buffer = new BYTE_VALUE[sizeof(long) * max];
  }

  ~ByteBuffer() {
    delete buffer;
    buffer = NULL;
  }

  inline void AddByte(BYTE_VALUE b) {
    if(pos >= max) {
      max *= 2;
      BYTE_VALUE* temp = new BYTE_VALUE[sizeof(long) * max];
      int i = pos;
      while(--i > -1) { 
        temp[i] = buffer[i];
      }
      delete buffer;
      buffer = temp;
    }
    buffer[pos++] = b;
  }

  inline BYTE_VALUE* GetBuffer() const {
    return buffer;
  }

  inline int GetSize() const {
    return pos;
  }
};

/********************************
 * StackClass class
 ********************************/
class StackClass {
  unordered_map<string, StackMethod*> method_name_map;
  StackMethod** methods;
  int method_num;
  long id;
  string name;
  string file_name;
  long pid;
  bool is_virtual;
  long cls_space;
  long inst_space;
  StackDclr** cls_dclrs;
  long cls_num_dclrs;
  StackDclr** inst_dclrs;
  long inst_num_dclrs;
  long* cls_mem;
  bool is_debug;
  
  long InitMemory(long size) {
    cls_mem = new long[size];
    memset(cls_mem, 0, size * sizeof(long));    
    return size;
  }
  
 public:
  StackClass(long i, const string &ne, const string &fn, long p, 
	     bool v, StackDclr** cdclr, long cn, StackDclr** idclr, long in, 
	     long cs, long is, bool b) {
    id = i;
    name = ne;
    file_name = fn;
    pid = p;
    is_virtual = v;
    cls_dclrs = cdclr;
    cls_num_dclrs = cn;
    inst_dclrs = idclr;
    inst_num_dclrs = in;
    cls_space = InitMemory(cs);
    inst_space  = is;
    is_debug = b;
  }

  ~StackClass() {
    // clean up
    if(cls_dclrs) {
      for(int i = 0; i < cls_num_dclrs; i++) {
        StackDclr* tmp = cls_dclrs[i];
        delete tmp;
        tmp = NULL;
      }
      delete[] cls_dclrs;
      cls_dclrs = NULL;
    }
    
    if(inst_dclrs) {
      for(int i = 0; i < inst_num_dclrs; i++) {
        StackDclr* tmp = inst_dclrs[i];
        delete tmp;
        tmp = NULL;
      }
      delete[] inst_dclrs;
      inst_dclrs = NULL;
    }

    for(int i = 0; i < method_num; i++) {
      StackMethod* method = methods[i];
      delete method;
      method = NULL;
    }
    delete[] methods;
    methods = NULL;

    if(cls_mem) {
      delete[] cls_mem;
      cls_mem = NULL;
    }
  }

  inline long GetId() const {
    return id;
  }

  bool IsDebug() {
    return is_debug;
  }

  inline const string& GetName() {
    return name;
  }

  inline const string& GetFileName() {
    return file_name;
  }

  inline StackDclr** GetClassDeclarations() const {
    return cls_dclrs;
  }

  inline int GetNumberClassDeclarations() const {
    return cls_num_dclrs;
  }

  inline StackDclr** GetInstanceDeclarations() const {
    return inst_dclrs;
  }

  inline int GetNumberInstanceDeclarations() const {
    return inst_num_dclrs;
  }
  
  inline long GetParentId() const {
    return pid;
  }

  inline bool IsVirtual() {
    return is_virtual;
  }

  inline long* GetClassMemory() const {
    return cls_mem;
  }

  inline long GetInstanceMemorySize() const {
    return inst_space;
  }

  void SetMethods(StackMethod** mthds, const int num) {
    methods = mthds;
    method_num = num;
    // add method names to map for virutal calls
    for(int i = 0; i < num; i++) {
      method_name_map.insert(make_pair(mthds[i]->GetName(), mthds[i]));
    }
  }

  inline StackMethod* GetMethod(long id) const {
#ifdef _DEBUG
    assert(id > -1 && id < method_num);
#endif
    return methods[id];
  }
  
  vector<StackMethod*> GetMethods(const string &n) {
    vector<StackMethod*> found;
    for(int i = 0; i < method_num; i++) {
      if(methods[i]->GetName().find(n) != string::npos) {
	found.push_back(methods[i]);
      }
    }

    return found;
  }
  
#ifdef _UTILS
  void List() {
    unordered_map<string, StackMethod*>::iterator iter;
    for(iter = method_name_map.begin(); iter != method_name_map.end(); ++iter) {
      StackMethod* mthd = iter->second;
      cout << "  method='" << mthd->GetName() << "'" << endl;
    }
  }
#endif

  inline StackMethod* GetMethod(const string &n) {
    unordered_map<string, StackMethod*>::iterator result = method_name_map.find(n);
    if(result != method_name_map.end()) {
      return result->second;
    }

    return NULL;
  }

  inline StackMethod** GetMethods() const {
    return methods;
  }

  inline int GetMethodCount() const {
    return method_num;
  }

#ifdef _DEBUGGER
  bool GetInstanceDeclaration(const string& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < inst_num_dclrs; i++, index++) {
	StackDclr* dclr = inst_dclrs[i];
	const string &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
	if(dclr_name == name) {
	  found.name = dclr->name;
	  found.type = dclr->type;
	  found.id = index;
	  return true;
	}
	// update
	if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
	  index++;
	}
      }
    }
    
    return false;
  }
  
  bool GetClassDeclaration(const string& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < cls_num_dclrs; i++, index++) {
	StackDclr* dclr = cls_dclrs[i];
	const string &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
	if(dclr_name == name) {
	  found.name = dclr->name;
	  found.type = dclr->type;
	  found.id = index;
	  return true;
	}
	// update
	if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
	  index++;
	}
      }
    }
    
    return false;
  }
#endif
};

/********************************
 * StackProgram class
 ********************************/
class StackProgram {
  map<string, StackClass*> cls_map;
  StackClass** classes;
  int class_num;
  int* cls_hierarchy;
  int** cls_interfaces;
  int string_cls_id;
  int cls_cls_id;
  int mthd_cls_id;
  int sock_cls_id;
  int data_type_cls_id;
  StackMethod* init_method;
  static map<string, string> properties_map;
  
  FLOAT_VALUE** float_strings;
  int num_float_strings;
  
  INT_VALUE** int_strings;
  int num_int_strings;
  
  BYTE_VALUE** char_strings;
  int num_char_strings;
  
#ifdef _WIN32
  static list<HANDLE> thread_ids;
  static CRITICAL_SECTION program_cs;
  static CRITICAL_SECTION prop_cs;
#else
  static list<pthread_t> thread_ids;
  static pthread_mutex_t program_mutex;
  static pthread_mutex_t prop_mutex;
#endif

 public:
  StackProgram() {
    cls_hierarchy = NULL;
    cls_interfaces = NULL;
    classes = NULL;
    char_strings = NULL;
    string_cls_id = cls_cls_id = mthd_cls_id = sock_cls_id = data_type_cls_id = -1;
#ifdef _WIN32
    InitializeCriticalSection(&program_cs);
    InitializeCriticalSection(&prop_cs);
#endif
  }
  
  ~StackProgram() {
    if(classes) {
      for(int i = 0; i < class_num; i++) {
	StackClass* klass = classes[i];
	delete klass;
	klass = NULL;
      }
      delete[] classes;
      classes = NULL;
    }

    if(cls_hierarchy) {
      delete[] cls_hierarchy;
      cls_hierarchy = NULL;
    }

    if(cls_interfaces) {
      for(int i = 0; i < class_num; i++) {
	delete[] cls_interfaces[i];
	cls_interfaces[i] = NULL;
      }
      delete[] cls_interfaces;
      cls_interfaces = NULL;
    }
    
    if(float_strings) {
      for(int i = 0; i < num_float_strings; i++) {
        FLOAT_VALUE* tmp = float_strings[i];
        delete[] tmp;
        tmp = NULL;
      }
      delete[] float_strings;
      float_strings = NULL;
    }

    if(int_strings) {
      for(int i = 0; i < num_int_strings; i++) {
        INT_VALUE* tmp = int_strings[i];
        delete[] tmp;
        tmp = NULL;
      }
      delete[] int_strings;
      int_strings = NULL;
    }

    if(char_strings) {
      for(int i = 0; i < num_char_strings; i++) {
	BYTE_VALUE* tmp = char_strings[i];
	delete [] tmp;
	tmp = NULL;
      }
      delete[] char_strings;
      char_strings = NULL;
    }
    
    if(init_method) {
      delete init_method;
      init_method = NULL;
    }

#ifdef _WIN32
    DeleteCriticalSection(&program_cs);
    DeleteCriticalSection(&prop_cs);
#endif
  }

#ifdef _WIN32
  static void AddThread(HANDLE h) {
    EnterCriticalSection(&program_cs);
    thread_ids.push_back(h);
    LeaveCriticalSection(&program_cs);
  }

  static void RemoveThread(HANDLE h) {
    EnterCriticalSection(&program_cs);
    thread_ids.remove(h);
    LeaveCriticalSection(&program_cs);
  }
  
  static list<HANDLE> GetThreads() {
    list<HANDLE> temp;
    EnterCriticalSection(&program_cs);
    temp = thread_ids;
    LeaveCriticalSection(&program_cs);
    
    return temp;
  }

  static string GetProperty(string key) {
    string value;
    
    EnterCriticalSection(&prop_cs);
    map<string, string>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }
    LeaveCriticalSection(&prop_cs);
    
    return value;
  }

  static void SetProperty(string key, string value) {
    EnterCriticalSection(&prop_cs);
    properties_map.insert(pair<string, string>(key, value));
    LeaveCriticalSection(&prop_cs);
  }

#else
  static void AddThread(pthread_t t) {
    pthread_mutex_lock(&program_mutex);
    thread_ids.push_back(t);
    pthread_mutex_unlock(&program_mutex);
  }

  static void RemoveThread(pthread_t t) {
    pthread_mutex_lock(&program_mutex);
    thread_ids.remove(t);
    pthread_mutex_unlock(&program_mutex);
  }
  
  static list<pthread_t> GetThreads() {
    list<pthread_t> temp;
    pthread_mutex_lock(&program_mutex);
    temp = thread_ids;
    pthread_mutex_unlock(&program_mutex);
    
    return temp;
  }


  static string GetProperty(string key) {
    string value;
    
    pthread_mutex_lock(&prop_mutex);
    map<string, string>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }
    pthread_mutex_unlock(&prop_mutex);
    
    return value;
  }
  
  static void SetProperty(string key, string value) {
    pthread_mutex_lock(&prop_mutex);
    properties_map.insert(pair<string, string>(key, value));
    pthread_mutex_unlock(&prop_mutex);
  }

#endif
  
  void SetInitializationMethod(StackMethod* i) {
    init_method = i;
  }

  StackMethod* GetInitializationMethod() const {
    return init_method;
  }
  
  int GetStringObjectId() const {
    return string_cls_id;
  }
  
  void SetStringObjectId(int id) {
    string_cls_id = id;
  }
  
  int GetClassObjectId() {
    if(cls_cls_id < 0) {
      StackClass* cls = GetClass("System.Introspection.Class");
      if(!cls) {
	cerr << ">>> Internal error: unable to find class: System.Introspection.Class <<<" << endl;
	exit(1);
      }
      cls_cls_id = cls->GetId();
    }
    
    return cls_cls_id;
  }
  
  int GetMethodObjectId() {
    if(mthd_cls_id < 0) {
      StackClass* cls = GetClass("System.Introspection.Method");
      if(!cls) {
	cerr << ">>> Internal error: unable to find class: System.Introspection.Method <<<" << endl;
	exit(1);
      }
      mthd_cls_id = cls->GetId();
    }
    
    return mthd_cls_id;
  }
  
  int GetSocketObjectId() {
    if(sock_cls_id < 0) {
      StackClass* cls = GetClass("System.IO.Net.TCPSocket");
      if(!cls) {
	cerr << ">>> Internal error: unable to find class: Net.TCPSocket <<<" << endl;
	exit(1);
      }
      sock_cls_id = cls->GetId();
    }
    
    return sock_cls_id;
  }
  
  int GetDataTypeObjectId() {
    if(data_type_cls_id < 0) {
      StackClass* cls = GetClass("System.Introspection.DataType");
      if(!cls) {
	cerr << ">>> Internal error: unable to find class: System.Introspection.DataType <<<" << endl;
	exit(1);
      }
      data_type_cls_id = cls->GetId();
    }
    
    return data_type_cls_id;
  }
  
  void SetFloatStrings(FLOAT_VALUE** s, int n) {
    float_strings = s;
    num_float_strings = n;
  }
  
  void SetIntStrings(INT_VALUE** s, int n) {
    int_strings = s;
    num_int_strings = n;
  }

  void SetCharStrings(BYTE_VALUE** s, int n) {
    char_strings = s;
    num_char_strings = n;
  }  
  
  FLOAT_VALUE** GetFloatStrings() const {
    return float_strings;
  }

  INT_VALUE** GetIntStrings() const {
    return int_strings;
  }

  BYTE_VALUE** GetCharStrings() const {
    return char_strings;
  }
  
  void SetClasses(StackClass** clss, const int num) {
    classes = clss;
    class_num = num;
    
    for(int i = 0; i < num; i++) {
      const string &name = clss[i]->GetName();
      if(name.size() > 0) {	
	cls_map.insert(pair<string, StackClass*>(name, clss[i]));
      }
    }
  }

#ifdef _UTILS
  void List() {
    map<string, StackClass*>::iterator iter;
    for(iter = cls_map.begin(); iter != cls_map.end(); ++iter) {
      StackClass* cls = iter->second;
      cout << "==================================" << endl;
      cout << "class='" << cls->GetName() << "'" << endl;
      cout << "==================================" << endl;
      cls->List();
    }
  }
#endif
  
  StackClass* GetClass(const string &n) {
    if(classes) {
      map<string, StackClass*>::iterator find = cls_map.find(n);
      if(find != cls_map.end()) {
	return find->second;
      }
    }
    
    return NULL;
  }
  
  void SetHierarchy(int* h) {
    cls_hierarchy = h;
  }

  inline int* GetHierarchy() const {
    return cls_hierarchy;
  }

  void SetInterfaces(int** i) {
    cls_interfaces = i;
  }

  inline int** GetInterfaces() const {
    return cls_interfaces;
  }  

  inline StackClass* GetClass(long id) const {
    if(id > -1 && id < class_num) {
      return classes[id];
    }

    return NULL;
  }

  inline StackClass** GetClasses() const {
    return classes;
  }
  
  inline int GetClassNumber() const {  
    return class_num;
  }
  
#ifdef _DEBUGGER
  bool HasFile(const string &fn) {
    for(int i = 0; i < class_num; i++) {
      if(classes[i]->GetFileName() == fn) {
        return true;
      }
    }

    return false;
  }
#endif
};

/********************************
 * StackFrame class
 ********************************/
class StackFrame {
  StackMethod* method;
  long* mem;
  long ip;
  bool jit_called;
  
 public:
  StackFrame(StackMethod* md, long* inst) {
    method = md;
    mem = md->NewMemory();
    mem[0] = (long)inst;
    ip = -1;
    jit_called = false;
  }
  
  ~StackFrame() {
    delete[] mem;
    mem = NULL;
  }
  
  inline StackMethod* GetMethod() const {
    return method;
  }

  inline long* GetMemory() const {
    return mem;
  }

  inline void SetIp(long i) {
    ip = i;
  }

  inline long GetIp() {
    return ip;
  }

  inline void SetJitCalled(bool j) {
    jit_called = j;
  }

  inline bool IsJitCalled() {
    return jit_called;
  }
};

/********************************
 * ObjectSerializer class
 ********************************/
class ObjectSerializer 
{
  vector<BYTE_VALUE> values;
  map<long*, long> serial_ids;
  long next_id;
  long cur_id;
  
  void CheckObject(long* mem, bool is_obj, long depth);
  void CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, long depth);
  void Serialize(long* inst);

  bool WasSerialized(long* mem) {
    map<long*, long>::iterator find = serial_ids.find(mem);
    if(find != serial_ids.end()) {
      cur_id = find->second;
      SerializeInt(cur_id);
      
      return true;
    }
    next_id++;
    cur_id = next_id * -1;
    serial_ids.insert(pair<long*, long>(mem, next_id));
    SerializeInt(cur_id);
    
    return false;
  }
  
  inline void SerializeByte(BYTE_VALUE v) {
    BYTE_VALUE* bp = (BYTE_VALUE*)&v;
    for(size_t i = 0; i < sizeof(v); i++) {
      values.push_back(*(bp + i));
    }
  }
  
  inline void SerializeInt(INT_VALUE v) {
    BYTE_VALUE* bp = (BYTE_VALUE*)&v;
    for(size_t i = 0; i < sizeof(v); i++) {
      values.push_back(*(bp + i));
    }
  }
  
  inline void SerializeFloat(FLOAT_VALUE v) {
    BYTE_VALUE* bp = (BYTE_VALUE*)&v;
    for(size_t i = 0; i < sizeof(v); i++) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeBytes(void* array, const long len) {
    BYTE_VALUE* bp = (BYTE_VALUE*)array;
    for(long i = 0; i < len; i++) {
      values.push_back(*(bp + i));
    }
  }
  
 public:
  ObjectSerializer(long* i) {
    Serialize(i);
  }
  
  ~ObjectSerializer() {
  }
  
  vector<BYTE_VALUE>& GetValues() {
    return values;
  }
};

/********************************
 *  ObjectDeserializer class
 ********************************/
class ObjectDeserializer 
{
  const BYTE_VALUE* buffer;
  long buffer_offset;
  long buffer_array_size;
  long* op_stack;
  long* stack_pos;
  StackClass* cls;
  long* instance;
  long instance_pos;
  map<INT_VALUE, long*> mem_cache;
  
  BYTE_VALUE DeserializeByte() {
    BYTE_VALUE value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }

  INT_VALUE DeserializeInt() {
    INT_VALUE value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }
  
  FLOAT_VALUE DeserializeFloat() {
    FLOAT_VALUE value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }
  
 public:
  ObjectDeserializer(const BYTE_VALUE* b, long s, long* stack, long* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = 0;
    cls = NULL;
    instance = NULL;
    instance_pos = 0;
  }
  
  ObjectDeserializer(const BYTE_VALUE* b, long o, map<INT_VALUE, long*> &c, 
		     long s, long* stack, long* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = o;
    mem_cache = c;
    cls = NULL;
    instance = NULL;
    instance_pos = 0;
  }
  
  ~ObjectDeserializer() {    
  }
  
  inline long GetOffset() const {
    return buffer_offset;
  }

  map<INT_VALUE, long*> GetMemoryCache() {
    return mem_cache;
  }
  
  long* DeserializeObject();
};

// call back for DLL method calls
void APITools_MethodCall(long* op_stack, long *stack_pos, long* instance, 
			 const char* cls_id, const char* mthd_id);
void APITools_MethodCallId(long* op_stack, long *stack_pos, long *instance, 
			   const int cls_id, const int mthd_id);			 

#endif
