/***************************************************************************
 * Defines the VM execution model.
 *
 * Copyright (c) 2008-2011, Randy Hollines
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

#ifdef _WIN32
#include <windows.h>
#include <hash_map>
using namespace stdext;
#else
#include <dlfcn.h>
#include <ext/hash_map>
#include <pthread.h>
#include <stdint.h>
namespace std { 
  using namespace __gnu_cxx; 
}
#endif

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
struct StackDclr {
  string name;
  ParamType type;
  long id;
};

/********************************
 * StackInstr class
 ********************************/
class StackInstr {
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
    operand = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o) {
    line_num = l;
    type = t;
    operand = o;
    native_offset = 0;
  }

  StackInstr(int l, InstructionType t, FLOAT_VALUE fo) {
    line_num = l;
    type = t;
    float_operand = fo;
    operand = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o, long o2) {
    line_num = l;
    type = t;
    operand = o;
    operand2 = o2;
    native_offset = 0;
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

  inline InstructionType GetType() {
    return type;
  }

  int GetLineNumber() {
    return line_num;
  }

  inline void SetType(InstructionType t) {
    type = t;
  }

  inline long GetOperand() {
    return operand;
  }

  inline long GetOperand2() {
    return operand2;
  }

  inline long GetOperand3() {
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

  inline FLOAT_VALUE GetFloatOperand() {
    return float_operand;
  }

  inline long GetOffset() {
    return native_offset;
  }

  inline void SetOffset(long o) {
    native_offset = o;
  }
};

/********************************
 * JIT compile code
 ********************************/
class NativeCode {
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

  BYTE_VALUE* GetCode() {
    return code;
  }
  long GetSize() {
    return size;
  }
  FLOAT_VALUE* GetFloats() {
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
  vector<StackInstr*> instrs;
  hash_map<long, long> jump_table;
  
  long param_count;
  long mem_size;
  NativeCode* native_code;
  MemoryType rtrn_type;
  StackDclr** dclrs;
  long num_dclrs;
  StackClass* cls;

  const string& ParseName(const string &name) const {
    int state;
    unsigned int index = name.find_last_of(':');
    if(index > 0) {
      string params_name = name.substr(index + 1);

      // check return type
      index = 0;
      while(index < params_name.size()) {
        ParamType param;
        switch(params_name[index]) {
        case 'l':
          param = INT_PARM;
          state = 0;
          index++;
          break;

        case 'b':
          param = INT_PARM;
          state = 1;
          index++;
          break;

        case 'i':
          param = INT_PARM;
          state = 2;
          index++;
          break;

        case 'f':
          param = FLOAT_PARM;
          state = 3;
          index++;
          break;

        case 'c':
          param = INT_PARM;
          state = 4;
          index++;
          break;

        case 'o':
          param = OBJ_PARM;
          state = 5;
          index++;
          while(index < params_name.size() && params_name[index] != ',') {
            index++;
          }
          break;

	case 'm':
          param = FUNC_PARM;
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
          case 1:
            param = BYTE_ARY_PARM;
            break;

          case 0:
          case 2:
          case 4:
            param = INT_ARY_PARM;
            break;

          case 3:
            param = FLOAT_ARY_PARM;
            break;

          case 5:
            param = OBJ_ARY_PARM;
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
    param_count = 0;
    native_code = NULL;
    dclrs = d;
    num_dclrs = nd;
    param_count = p;
    mem_size = m;
    rtrn_type = r;
    cls = k;
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
    while(!instrs.empty()) {
      StackInstr* tmp = instrs.front();
      instrs.erase(instrs.begin());
      // delete
      delete tmp;
      tmp = NULL;
    }
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

  inline StackClass* GetClass() {
    return cls;
  }

#ifdef _DEBUGGER
  int GetDeclaration(const string& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < num_dclrs; i++) {
	StackDclr* dclr = dclrs[i];
	const string &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
	if(dclr_name == name) {
	  found.name = dclr->name;
	  found.type = dclr->type;
	  found.id = dclr->id;
	  
	  return index;
	}
	// update
	if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
	  index += 2;
	}
	else {
	  index++;
	}
      }
    }
    
    return -1;
  }
#endif
  
  inline StackDclr** GetDeclarations() {
    return dclrs;
  }

  inline const int GetNumberDeclarations() {
    return num_dclrs;
  }

  void SetNativeCode(NativeCode* c) {
    native_code = c;
  }

  NativeCode* GetNativeCode() {
    return native_code;
  }

  MemoryType GetReturn() {
    return rtrn_type;
  }
  
  inline void AddLabel(long label_id, long index) {
    jump_table.insert(pair<long, long>(label_id, index));
  }
  
  inline long GetLabelIndex(long label_id) {
    hash_map<long, long>::iterator find = jump_table.find(label_id);
    if(find != jump_table.end()) {
      return find->second;
    }    
    
    return -1;
  }
  
  void SetInstructions(vector<StackInstr*> i) {
    instrs = i;
  }

  void AddInstruction(StackInstr* i) {
    instrs.push_back(i);
  }

  long GetId() {
    return id;
  }

  long GetParamCount() {
    return param_count;
  }

  void SetParamCount(long c) {
    param_count = c;
  }

  long* NewMemory() {
    // +1 is for instance variable
    const long size = mem_size + 2;
    long* mem = new long[size];
    memset(mem, 0, size * sizeof(long));

    return mem;
  }

  long GetMemorySize() {
    return mem_size;
  }

  long GetInstructionCount() {
    return instrs.size();
  }

  StackInstr* GetInstruction(long i) {
    return instrs[i];
  }
};

/********************************
 * StackClass class
 ********************************/
class StackClass {
  // map<long, StackMethod*> method_map;
  StackMethod** methods;
  int method_num;
  long id;
  string name;
  string file_name;
  long pid;
  bool is_virtual;
  long cls_space;
  long inst_space;
  StackDclr** dclrs;
  long num_dclrs;
  long* cls_mem;
  bool is_debug;

  map<const string, StackMethod*> method_name_map;

  long InitMemory(long size) {
    cls_mem = new long[size];
    memset(cls_mem, 0, size * sizeof(long));

    return size;
  }

public:
  StackClass(long i, const string &ne, const string &fn, long p, 
	     bool v, StackDclr** d, long n, long cs, long is, bool b) {
    id = i;
    name = ne;
    file_name = fn;
    pid = p;
    is_virtual = v;
    dclrs = d;
    num_dclrs = n;
    cls_space = InitMemory(cs);
    inst_space  = is;
    is_debug = b;
  }

  ~StackClass() {
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

  inline long GetId() {
    return id;
  }

  bool IsDebug() {
    return is_debug;
  }

  inline const string& GetName() const {
    return name;
  }

  inline const string& GetFileName() const {
    return file_name;
  }

  inline StackDclr** GetDeclarations() {
    return dclrs;
  }

  inline const int GetNumberDeclarations() {
    return num_dclrs;
  }

  inline long GetParentId() {
    return id;
  }

  inline bool IsVirtual() {
    return is_virtual;
  }

  inline long* GetClassMemory() {
    return cls_mem;
  }

  inline long GetInstanceMemorySize() {
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

  inline StackMethod* GetMethod(long id) {
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
    map<const string, StackMethod*>::iterator iter;
    for(iter = method_name_map.begin(); iter != method_name_map.end(); iter++) {
      StackMethod* mthd = iter->second;
      cout << "  method='" << mthd->GetName() << "'" << endl;
    }
  }
#endif

  inline StackMethod* GetMethod(const string &n) {
    map<const string, StackMethod*>::iterator result = method_name_map.find(n);
    if(result != method_name_map.end()) {
      return result->second;
    }

    return NULL;
  }

  inline StackMethod** GetMethods() {
    return methods;
  }

  inline int GetMethodCount() {
    return method_num;
  }

#ifdef _DEBUGGER
  int GetDeclaration(const string& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < num_dclrs; i++) {
	StackDclr* dclr = dclrs[i];
	const string &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
	if(dclr_name == name) {
	  found.name = dclr->name;
	  found.type = dclr->type;
	  found.id = dclr->id;
	  
	  return index;
	}
	// update
	if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
	  index += 2;
	}
	else {
	  index++;
	}
      }
    }
    
    return -1;
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
  long string_cls_id;
  long mthd_cls_id;
  
  FLOAT_VALUE** float_strings;
  int num_float_strings;

  INT_VALUE** int_strings;
  int num_int_strings;
  
  BYTE_VALUE** char_strings;
  int num_char_strings;

  StackMethod* init_method;
#ifdef _WIN32
  static list<HANDLE> thread_ids;
  static CRITICAL_SECTION program_cs;
#else
  static list<pthread_t> thread_ids;
  static pthread_mutex_t program_mutex;
#endif

public:
  StackProgram() {
    cls_hierarchy = NULL;
    classes = NULL;
    char_strings = NULL;
    mthd_cls_id = -1;
    string_cls_id = -1;
#ifdef _WIN32
    InitializeCriticalSection(&program_cs);
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
        delete[] tmp;
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
#endif
  
  void SetInitializationMethod(StackMethod* i) {
    init_method = i;
  }

  StackMethod* GetInitializationMethod() {
    return init_method;
  }
  
  long GetStringClassId() {
    return string_cls_id;
  }
  
  void SetStringClassId(long id) {
    string_cls_id = id;
  }
  
  long GetMethodClassId() {
    if(mthd_cls_id < 0) {
      StackClass* cls = GetClass("System.Method");
      if(!cls) {
	cerr << ">>> Internal error: unable to find class: System.Method <<<" << endl;
	exit(1);
      }
      mthd_cls_id = cls->GetId();
    }
    
    return mthd_cls_id;
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
  
  FLOAT_VALUE** GetFloatStrings() {
    return float_strings;
  }

  INT_VALUE** GetIntStrings() {
    return int_strings;
  }

  BYTE_VALUE** GetCharStrings() {
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
    for(iter = cls_map.begin(); iter != cls_map.end(); iter++) {
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

  inline int* GetHierarchy() {
    return cls_hierarchy;
  }

  inline StackClass* GetClass(long id) {
    if(id > -1 && id < class_num) {
      return classes[id];
    }

    return NULL;
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
  
  inline StackMethod* GetMethod() {
    return method;
  }

  inline long* GetMemory() {
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

// call back for DLL method calls
typedef void(*DLLTools_MethodCall_Ptr)(long*, long*, long*, const char*, const char*);
void DLLTools_MethodCall(long* op_stack, long *stack_pos, long* instance, 
			 const char* cls_id, const char* mthd_id);

#endif
