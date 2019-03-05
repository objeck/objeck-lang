/***************************************************************************
 * Defines the VM execution model.
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
#include <iomanip>
#include <sstream>
#include <fstream>
#include <stack>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <string>
#include <ctime>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"

#ifndef _UTILS
#include <openssl/bio.h>
#include <openssl/ssl.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <process.h>
#include <unordered_map>
#include <unordered_set>
#include <userenv.h>
#include <cstring>
using namespace stdext;
#elif _OSX
#include <mach-o/dyld.h>
#include <unordered_map>
#include <unordered_set>
#include <pthread.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#else
#include <unordered_map>
#include <unordered_set>
#include <pthread.h>
#include <pwd.h>
#include <stdint.h>
#include <iomanip>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#endif


#define SMALL_BUFFER_MAX 383
#define LARGE_BUFFER_MAX 4095
#define CALC_STACK_SIZE 512
#define CACERT_PEM_FILE "cacert.pem"

#ifdef _WIN32
#define MUTEX_LOCK EnterCriticalSection
#define MUTEX_UNLOCK LeaveCriticalSection
#else
#define MUTEX_LOCK pthread_mutex_lock
#define MUTEX_UNLOCK pthread_mutex_unlock
#endif

using namespace std;
using namespace instructions;

class StackClass;

inline const wstring IntToString(int v)
{
  wostringstream str;
  str << v;
  return str.str();
}

/********************************
 * StackDclr struct
 ********************************/
struct StackDclr 
{
  wstring name;
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
#ifdef _ARM32
  uint32_t* code;
#else
  unsigned char* code;
#endif  
  long size;
  FLOAT_VALUE* floats;
  
 public:
#ifdef _ARM32
  NativeCode(uint32_t* c, long s, FLOAT_VALUE* f)
#else
    NativeCode(unsigned char* c, long s, FLOAT_VALUE* f)
#endif
    {
      code = c;
      size = s;
      floats = f;
    }
  
  ~NativeCode() {
#ifdef _WIN64
    VirtualFree(floats, NULL, MEM_RELEASE);
#elif _X64
    free(floats); 
#else
    delete[] floats;
#endif
    floats = NULL;
  }

#ifdef _ARM32
  uint32_t* GetCode() const
#else
    unsigned char* GetCode() const
#endif
  {
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
  wstring name;
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
  static unordered_map<wstring, StackMethod*> virutal_cache;

  const wstring& ParseName(const wstring &name) const {
    int state;
    size_t index = name.find_last_of(':');
    if(index > 0) {
      wstring params_name = name.substr(index + 1);

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
          param = CHAR_PARM;
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
#ifdef _DEBUG
            param = BYTE_ARY_PARM;
#endif
            break;

          case 4:
#ifdef _DEBUG
            param = CHAR_ARY_PARM;
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
        case CHAR_PARM:
          wcout << L"  CHAR_PARM" << endl;
          break;

        case INT_PARM:
          wcout << L"  INT_PARM" << endl;
          break;

        case FLOAT_PARM:
          wcout << L"  FLOAT_PARM" << endl;
          break;

        case BYTE_ARY_PARM:
          wcout << L"  BYTE_ARY_PARM" << endl;
          break;

        case CHAR_ARY_PARM:
          wcout << L"  CHAR_ARY_PARM" << endl;
          break;

        case INT_ARY_PARM:
          wcout << L"  INT_ARY_PARM" << endl;
          break;

        case FLOAT_ARY_PARM:
          wcout << L"  FLOAT_ARY_PARM" << endl;
          break;

        case OBJ_PARM:
          wcout << L"  OBJ_PARM" << endl;
          break;

        case OBJ_ARY_PARM:
          wcout << L"  OBJ_ARY_PARM" << endl;
          break;

        case FUNC_PARM:
          wcout << L"  FUNC_PARM" << endl;
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
  StackMethod(long i, const wstring &n, bool v, bool h, StackDclr** d, long nd, long p, long m, MemoryType r, StackClass* k) {
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
      for(int i = 0; i < num_dclrs; ++i) {
        StackDclr* tmp = dclrs[i];
        delete tmp;
        tmp = NULL;
      }
      delete[] dclrs;
      dclrs = NULL;
    }

    // clean up
    if(native_code) {
      delete native_code;
      native_code = NULL;
    }

    // clean up
    for(int i = 0; i < instr_count; ++i) {
      StackInstr* tmp = instrs[i];
      delete tmp;
      tmp = NULL;
    }
    delete[] instrs;
    instrs = NULL;
  }

  inline const wstring& GetName() {
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
  // TODO: might have 1 or more variables with the same name
  bool GetLocalDeclaration(const wstring& name, StackDclr& found) {
    vector<int> results;
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < num_dclrs; i++, index++) {
        StackDclr* dclr = dclrs[i];
        const wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
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

  inline size_t* NewMemory() {
    // +1 is for instance variable
    const long size = mem_size + 2;
    size_t* mem = new size_t[size];
    memset(mem, 0, size * sizeof(size_t));

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

  inline StackInstr** GetInstructions() const {
    return instrs;
  }

#ifdef _WIN32
  static void InitVirtualEntry() {
    InitializeCriticalSection(&virutal_cs);
  }
#endif

  static void AddVirtualEntry(const wstring &key, StackMethod* mthd) { 
#ifdef _WIN32
    EnterCriticalSection(&virutal_cs);
#else
    pthread_mutex_lock(&virtual_mutex);
#endif

    virutal_cache.insert(pair<wstring, StackMethod*>(key, mthd));

#ifdef _WIN32
    LeaveCriticalSection(&virutal_cs);
#else
    pthread_mutex_unlock(&virtual_mutex);
#endif
  }

  static StackMethod* GetVirtualEntry(const wstring &key) {
    StackMethod* mthd = NULL;
#ifdef _WIN32
    EnterCriticalSection(&virutal_cs);
#else
    pthread_mutex_lock(&virtual_mutex);
#endif

    unordered_map<wstring, StackMethod*>::iterator found = virutal_cache.find(key);
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
 * StackClass class
 ********************************/
class StackClass {
  unordered_map<wstring, StackMethod*> method_name_map;
  StackMethod** methods;
  int method_num;
  long id;
  wstring name;
  wstring file_name;
  long pid;
  bool is_virtual;
  long cls_space;
  long inst_space;
  StackDclr** cls_dclrs;
  long cls_num_dclrs;
  StackDclr** inst_dclrs;
  long inst_num_dclrs;
  size_t* cls_mem;
  bool is_debug;

  long InitMemory(long size) {
    cls_mem = new size_t[size];
    memset(cls_mem, 0, size * sizeof(size_t));    
    return size;
  }

 public:
  StackClass(long i, const wstring &ne, const wstring &fn, long p, 
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
      for(int i = 0; i < cls_num_dclrs; ++i) {
        StackDclr* tmp = cls_dclrs[i];
        delete tmp;
        tmp = NULL;
      }
      delete[] cls_dclrs;
      cls_dclrs = NULL;
    }

    if(inst_dclrs) {
      for(int i = 0; i < inst_num_dclrs; ++i) {
        StackDclr* tmp = inst_dclrs[i];
        delete tmp;
        tmp = NULL;
      }
      delete[] inst_dclrs;
      inst_dclrs = NULL;
    }

    for(int i = 0; i < method_num; ++i) {
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

  inline const wstring& GetName() {
    return name;
  }

  inline const wstring& GetFileName() {
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

  inline size_t* GetClassMemory() const {
    return cls_mem;
  }

  inline long GetInstanceMemorySize() const {
    return inst_space;
  }

  void SetMethods(StackMethod** mthds, const int num) {
    methods = mthds;
    method_num = num;
    // add method names to map for virutal calls
    for(int i = 0; i < num; ++i) {
      method_name_map.insert(make_pair(mthds[i]->GetName(), mthds[i]));
    }
  }

  inline StackMethod* GetMethod(long id) const {
#ifdef _DEBUG
    assert(id > -1 && id < method_num);
#endif
    return methods[id];
  }

#ifdef _DEBUGGER
  vector<StackMethod*> GetMethods(const wstring &n) {
    vector<StackMethod*> found;
    for(int i = 0; i < method_num; ++i) {
      if(methods[i]->GetName().find(n) != wstring::npos) {
        found.push_back(methods[i]);
      }
    }

    return found;
  }
#endif

#ifdef _UTILS
  void List() {
    unordered_map<wstring, StackMethod*>::iterator iter;
    for(iter = method_name_map.begin(); iter != method_name_map.end(); ++iter) {
      StackMethod* mthd = iter->second;
      wcout << L"  method='" << mthd->GetName() << "'" << endl;
    }
  }
#endif

  inline StackMethod* GetMethod(const wstring &n) {
    unordered_map<wstring, StackMethod*>::iterator result = method_name_map.find(n);
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
  bool GetInstanceDeclaration(const wstring& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < inst_num_dclrs; i++, index++) {
        StackDclr* dclr = inst_dclrs[i];
        const wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
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

  bool GetClassDeclaration(const wstring& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < cls_num_dclrs; i++, index++) {
        StackDclr* dclr = cls_dclrs[i];
        const wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(':') + 1);       
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
  map<wstring, StackClass*> cls_map;
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
  static map<wstring, wstring> properties_map;

  FLOAT_VALUE** float_strings;
  int num_float_strings;

  INT_VALUE** int_strings;
  int num_int_strings;

  wchar_t** char_strings;
  int num_char_strings;

#ifdef _WIN32
  static CRITICAL_SECTION program_cs;
  static CRITICAL_SECTION prop_cs;
#else
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
      for(int i = 0; i < class_num; ++i) {
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
      for(int i = 0; i < class_num; ++i) {
        delete[] cls_interfaces[i];
        cls_interfaces[i] = NULL;
      }
      delete[] cls_interfaces;
      cls_interfaces = NULL;
    }

    if(float_strings) {
      for(int i = 0; i < num_float_strings; ++i) {
        FLOAT_VALUE* tmp = float_strings[i];
        delete[] tmp;
        tmp = NULL;
      }
      delete[] float_strings;
      float_strings = NULL;
    }

    if(int_strings) {
      for(int i = 0; i < num_int_strings; ++i) {
        INT_VALUE* tmp = int_strings[i];
        delete[] tmp;
        tmp = NULL;
      }
      delete[] int_strings;
      int_strings = NULL;
    }

    if(char_strings) {
      for(int i = 0; i < num_char_strings; ++i) {
        wchar_t* tmp = char_strings[i];
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
  static wstring GetProperty(const wstring& key) {
    wstring value;

    EnterCriticalSection(&prop_cs);

    if(properties_map.size() == 0) {
      InitializeProprieties();
    }
    
    map<wstring, wstring>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }
    LeaveCriticalSection(&prop_cs);

    return value;
  }

  static void SetProperty(const wstring& key, const wstring& value) {
    EnterCriticalSection(&prop_cs);
    properties_map.insert(pair<wstring, wstring>(key, value));
    LeaveCriticalSection(&prop_cs);
  }

  static BOOL GetUserDirectory(char* buf, DWORD len) {
    HANDLE handle;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &handle)) {
      return FALSE;
    }

    if (!GetUserProfileDirectory(handle, buf, &len)) {
      return FALSE;
    }

    CloseHandle(handle);
    return TRUE;
  }
#else
  static wstring GetProperty(const wstring& key) {
    wstring value;
    
    pthread_mutex_lock(&prop_mutex);
    
    if(properties_map.size() == 0) {
      InitializeProprieties();
    }
    
    map<wstring, wstring>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }
    pthread_mutex_unlock(&prop_mutex);

    return value;
  }

  static void SetProperty(const wstring& key, const wstring& value) {
    pthread_mutex_lock(&prop_mutex);
    properties_map.insert(pair<wstring, wstring>(key, value));
    pthread_mutex_unlock(&prop_mutex);
  }
#endif

  static void InitializeProprieties() {
    // install directory
#ifdef _DEBUG
#ifdef _WIN32  
  char install_path[MAX_PATH];
  DWORD status = GetModuleFileNameA(NULL, install_path, sizeof(install_path));
  if(status > 0) {
    string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('\\');
    if(install_index != string::npos) {
      wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
      properties_map.insert(pair<wstring, wstring>(L"install_dir", install_dir));
    }
  }
#else
  ssize_t status = 0;
  char install_path[SMALL_BUFFER_MAX];
#ifdef _OSX
  uint32_t size = SMALL_BUFFER_MAX;
  if(_NSGetExecutablePath(install_path, &size) != 0) {
    status = -1;
  }
#else
  status = ::readlink("/proc/self/exe", install_path, sizeof(install_path) - 1);
#endif
  if(status != -1) {
    string exe_path(install_path);
    size_t install_index = exe_path.find_last_of('/');
    if(install_index != string::npos) {
      wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
      properties_map.insert(pair<wstring, wstring>(L"install_dir", install_dir));
    }
  }
#endif
#else
#ifdef _WIN32  
    char install_path[MAX_PATH];
    DWORD status = GetModuleFileNameA(NULL, install_path, sizeof(install_path));
    if(status > 0) {
      string exe_path(install_path);
      size_t install_index = exe_path.find_last_of('\\');
      if(install_index != string::npos) {
        exe_path = exe_path.substr(0, install_index);
        install_index = exe_path.find_last_of('\\');
        if(install_index != string::npos) {
          wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
          properties_map.insert(pair<wstring, wstring>(L"install_dir", install_dir));
        }
      }
    }
#else
		ssize_t status = 0;
    char install_path[SMALL_BUFFER_MAX];
#ifdef _OSX
		uint32_t size = SMALL_BUFFER_MAX;
		if(_NSGetExecutablePath(install_path, &size) != 0) {
			status = -1;
		}
#else
    status = ::readlink("/proc/self/exe", install_path, sizeof(install_path) - 1);
#endif
    if(status != -1) {
      string exe_path(install_path);
      size_t install_index = exe_path.find_last_of('/');
      if(install_index != string::npos) {
        exe_path = exe_path.substr(0, install_index);
        install_index = exe_path.find_last_of('/');
        if(install_index != string::npos) {
          wstring install_dir = BytesToUnicode(exe_path.substr(0, install_index));
          properties_map.insert(pair<wstring, wstring>(L"install_dir", install_dir));
        }
      }
    }
#endif
#endif

    // user and temp directories
#ifdef _WIN32  
    char user_dir[MAX_PATH];
    if(GetUserDirectory(user_dir, MAX_PATH)) {
      wstring user_dir_value = BytesToUnicode(user_dir);
      if(user_dir_value.back() == L'/' || user_dir_value.back() == L'\\') {
        user_dir_value.pop_back();
      }
      properties_map.insert(pair<wstring, wstring>(L"user_dir", user_dir_value));
    }

    char tmp_dir[MAX_PATH];
    if(GetTempPath(MAX_PATH, tmp_dir)) {
      wstring tmp_dir_value = BytesToUnicode(tmp_dir);
      if(tmp_dir_value.back() == L'/' || tmp_dir_value.back() == L'\\') {
        tmp_dir_value.pop_back();
      }
      properties_map.insert(pair<wstring, wstring>(L"tmp_dir", tmp_dir_value));
    }
#else
    struct passwd* user = getpwuid(getuid());
    if(user) {
      properties_map.insert(pair<wstring, wstring>(L"user_dir", BytesToUnicode(user->pw_dir)));
    }

    const char* tmp_dir = P_tmpdir;
    if(tmp_dir) {
      properties_map.insert(pair<wstring, wstring>(L"tmp_dir", BytesToUnicode(tmp_dir)));
    }
#endif

    // read configuration properties
    const int line_max = 80;
    char buffer[line_max + 1];
    fstream config("config.prop", fstream::in);
    config.getline(buffer, line_max);
    if(config.good()) {
      while(strlen(buffer) > 0) {
        // readline ane parse
        wstring line = BytesToUnicode(buffer);
        if(line.size() > 0 && line[0] != L'#') {
          size_t offset = line.find_first_of(L'=');
          // set name/value pairs
          wstring name = line.substr(0, offset);
          wstring value = line.substr(offset + 1);
          if(name.size() > 0 && value.size() > 0) {
            properties_map.insert(pair<wstring, wstring>(name, value));
          }
        }
        // update
        config.getline(buffer, 80);
      }
    }
    config.close();
  }
  
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
      StackClass* cls = GetClass(L"System.Introspection.Class");
      if(!cls) {
        wcerr << L">>> Internal error: unable to find class: System.Introspection.Class <<<" << endl;
        exit(1);
      }
      cls_cls_id = cls->GetId();
    }

    return cls_cls_id;
  }

  int GetMethodObjectId() {
    if(mthd_cls_id < 0) {
      StackClass* cls = GetClass(L"System.Introspection.Method");
      if(!cls) {
        wcerr << L">>> Internal error: unable to find class: System.Introspection.Method <<<" << endl;
        exit(1);
      }
      mthd_cls_id = cls->GetId();
    }

    return mthd_cls_id;
  }

  int GetSocketObjectId() {
    if(sock_cls_id < 0) {
      StackClass* cls = GetClass(L"System.IO.Net.TCPSocket");
      if(!cls) {
        wcerr << L">>> Internal error: unable to find class: Net.TCPSocket <<<" << endl;
        exit(1);
      }
      sock_cls_id = cls->GetId();
    }

    return sock_cls_id;
  }

  int GetDataTypeObjectId() {
    if(data_type_cls_id < 0) {
      StackClass* cls = GetClass(L"System.Introspection.DataType");
      if(!cls) {
        wcerr << L">>> Internal error: unable to find class: System.Introspection.DataType <<<" << endl;
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

  void SetCharStrings(wchar_t** s, int n) {
    char_strings = s;
    num_char_strings = n;
  }  

  FLOAT_VALUE** GetFloatStrings() const {
    return float_strings;
  }

  INT_VALUE** GetIntStrings() const {
    return int_strings;
  }

  wchar_t** GetCharStrings() const {
    return char_strings;
  }

  void SetClasses(StackClass** clss, const int num) {
    classes = clss;
    class_num = num;

    for(int i = 0; i < num; ++i) {
      const wstring &name = clss[i]->GetName();
      if(name.size() > 0) {	
        cls_map.insert(pair<wstring, StackClass*>(name, clss[i]));
      }
    }
  }

#ifdef _UTILS
  void List() {
    map<wstring, StackClass*>::iterator iter;
    for(iter = cls_map.begin(); iter != cls_map.end(); ++iter) {
      StackClass* cls = iter->second;
      wcout << L"==================================" << endl;
      wcout << L"class='" << cls->GetName() << "'" << endl;
      wcout << L"==================================" << endl;
      cls->List();
    }
  }
#endif

  StackClass* GetClass(const wstring &n) {
    if(classes) {
      map<wstring, StackClass*>::iterator find = cls_map.find(n);
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
  bool HasFile(const wstring &fn) {
    for(int i = 0; i < class_num; ++i) {
      if(classes[i]->GetFileName() == fn) {
        return true;
      }
    }

    return false;
  }
#endif
};

struct StackFrame {
  StackMethod* method;
  size_t* mem;
  long ip;
  bool jit_called;
  size_t* jit_mem;
  long jit_offset;
};

/********************************
 * ObjectSerializer class
 ********************************/
class ObjectSerializer 
{
  vector<char> values;
  map<size_t*, long> serial_ids;
  long next_id;
  long cur_id;

  void CheckObject(size_t* mem, bool is_obj, long depth);
  void CheckMemory(size_t* mem, StackDclr** dclrs, const long dcls_size, long depth);
  void Serialize(size_t* inst);

  bool WasSerialized(size_t* mem) {
    map<size_t*, long>::iterator find = serial_ids.find(mem);
    if(find != serial_ids.end()) {
      cur_id = find->second;
      SerializeInt(cur_id);

      return true;
    }
    next_id++;
    cur_id = next_id * -1;
    serial_ids.insert(pair<size_t*, long>(mem, next_id));
    SerializeInt(cur_id);

    return false;
  }

  inline void SerializeByte(const char v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeChar(const wchar_t v) {
    string out;
    CharacterToBytes(v, out);
    SerializeInt((INT_VALUE)out.size());
    for(size_t i = 0; i < out.size(); ++i) {
      values.push_back(out[i]); 
    }
  }

  inline void SerializeInt(const INT_VALUE v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeFloat(const FLOAT_VALUE v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeBytes(const void* array, const long len) {
    char* bp = (char*)array;
    for(long i = 0; i < len; ++i) {
      values.push_back(*(bp + i));
    }
  }

 public:
  ObjectSerializer(size_t* i) {
    Serialize(i);
  }

  ~ObjectSerializer() {
  }

  vector<char>& GetValues() {
    return values;
  }
};

/********************************
 *  ObjectDeserializer class
 ********************************/
class ObjectDeserializer 
{
  const char* buffer;
  long buffer_offset;
  long buffer_array_size;
  size_t* op_stack;
  long* stack_pos;
  StackClass* cls;
  size_t* instance;
  long instance_pos;
  map<INT_VALUE, size_t*> mem_cache;

  char DeserializeByte() {
    char value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }

  wchar_t DeserializeChar() {
    // read
    const int num = DeserializeInt();
    char* in = new char[num + 1];
    memcpy(in, buffer + buffer_offset, num);
    in[num] = '\0';

    // convert
    wchar_t out = L'\0';
    BytesToCharacter(in, out);
    delete[] in;
    in = NULL;

    buffer_offset += num;
    return out;
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
  ObjectDeserializer(const char* b, long s, size_t* stack, long* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = 0;
    cls = NULL;
    instance = NULL;
    instance_pos = 0;
  }

  ObjectDeserializer(const char* b, long o, map<INT_VALUE, size_t*> &c,
		     long s, size_t* stack, long* pos) {
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

  map<INT_VALUE, size_t*> GetMemoryCache() {
    return mem_cache;
  }

  size_t* DeserializeObject();
};

/********************************
 *  TrapManager class
 ********************************/
enum TimeInterval {
  DAY_TIME,
  HOUR_TIME,
  MIN_TIME,
  SEC_TIME
};

class TrapProcessor {
  static inline bool GetTime(struct tm* &curr_time, time_t raw_time, bool is_gmt) {
#ifdef _WIN32
    struct tm temp_time;
    if(is_gmt) {
      if(gmtime_s(&temp_time, &raw_time)) {
        wcerr << L">>> Unable to get GMT time <<<" << endl;
        return false;
      }
    }
    else {
      if(localtime_s(&temp_time, &raw_time)) {
        wcerr << L">>> Unable to get GMT time <<<" << endl;
        return false;
      }
    }
    curr_time = &temp_time;
#else
    if(is_gmt) {
      curr_time = gmtime(&raw_time);
    }
    else {
      curr_time = localtime(&raw_time);
    }
#endif

    return curr_time != NULL;
  }

  //
  // pops an integer from the calculation stack.  this code
  // in normally inlined and there's a macro version available.
  //
  static inline size_t PopInt(size_t* op_stack, long* stack_pos) {    
#ifdef _DEBUG
    size_t v = op_stack[--(*stack_pos)];
    wcout << L"  [pop_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"("
	  << (size_t*)v << L")]" << endl;
    return v;
#else
    return op_stack[--(*stack_pos)];
#endif
  }

  //
  // pushes an integer onto the calculation stack.  this code
  // in normally inlined and there's a macro version available.
  //
  static inline void PushInt(size_t v, size_t* op_stack, long* stack_pos) {
#ifdef _DEBUG
    wcout << L"  [push_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"("
	  << (size_t*)v << L")]" << endl;
#endif
    op_stack[(*stack_pos)++] = v;
  }

  //
  // pushes an double onto the calculation stack.
  //
  static inline void PushFloat(FLOAT_VALUE v, size_t* op_stack, long* stack_pos) {
#ifdef _DEBUG
    wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << endl;
#endif
    memcpy(&op_stack[(*stack_pos)], &v, sizeof(FLOAT_VALUE));

#if defined(_WIN64) || defined(_X64)
    (*stack_pos)++;
#else
    (*stack_pos) += 2;
#endif
  }

  //
  // swaps two integers on the calculation stack
  //
  static inline void SwapInt(size_t* op_stack, long* stack_pos) {
    size_t v = op_stack[(*stack_pos) - 2];
    op_stack[(*stack_pos) - 2] = op_stack[(*stack_pos) - 1];
    op_stack[(*stack_pos) - 1] = v;
  }

  //
  // pops a double from the calculation stack
  //
  static inline FLOAT_VALUE PopFloat(size_t* op_stack, long* stack_pos) {
    FLOAT_VALUE v;

#if defined(_WIN64) || defined(_X64)
    (*stack_pos)--;
#else
    (*stack_pos) -= 2;
#endif

    memcpy(&v, &op_stack[(*stack_pos)], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
    wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << endl;
#endif

    return v;
  }

  //
  // peeks at the integer on the top of the
  // execution stack.
  //
  static inline size_t TopInt(size_t* op_stack, long* stack_pos) {
#ifdef _DEBUG
    size_t v = op_stack[(*stack_pos) - 1];
    wcout << L"  [top_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (void*)v << L")]" << endl;
    return v;
#else
    return op_stack[(*stack_pos) - 1];
#endif
  }

  //
  // peeks at the double on the top of the
  // execution stack.
  //
  static inline FLOAT_VALUE TopFloat(size_t* op_stack, long* stack_pos) {
    FLOAT_VALUE v;

#if defined(_WIN64) || defined(_X64)
    long index = (*stack_pos) - 1;
#else
    long index = (*stack_pos) - 2;
#endif

    memcpy(&v, &op_stack[index], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
    wcout << L"  [top_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << endl;
#endif

    return v;
  }

  // main trap functions
  static bool LoadClsInstId(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool LoadNewObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool LoadClsByInst(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool ConvertBytesToUnicode(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool ConvertUnicodeToBytes(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool LoadMultiArySize(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool CpyCharStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool CpyCharStrArys(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool CpyIntStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool CpyFloatStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdFlush(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutBool(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutChar(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutInt(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutFloat(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutByteAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdOutCharAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdInString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrFlush(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrBool(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrChar(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrInt(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrFloat(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool StdErrByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool Exit(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool GmtTime(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SysTime(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DateTimeSet1(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DateTimeSet2(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DateTimeAddDays(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DateTimeAddHours(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DateTimeAddMins(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DateTimeAddSecs(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool TimerStart(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool TimerEnd(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool TimerElapsed(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool GetPltfrm(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool GetVersion(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool GetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpResolveName(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpHostName(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpConnect(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpBind(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpListen(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpAccept(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpClose(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpOutString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpInString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslConnect(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslCert(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslClose(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlChar(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlInt(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SerlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlChar(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlInt(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DeserlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool CompressBytes(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool UncompressBytes(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool CRC32Bytes(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOpenRead(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOpenAppend(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOpenWrite(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOpenReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileClose(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileFlush(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileInString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOutString(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileRewind(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpIsConnected(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpInByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileInByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileSeek(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileEof(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileIsOpen(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileCanWriteOnly(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileCanReadOnly(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileCanReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileExists(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileSize(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileFullPath(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileAccountOwner(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileGroupOwner(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileDelete(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileRename(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileCreateTime(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileModifiedTime(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool FileAccessedTime(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DirCreate(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DirExists(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
  static bool DirList(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);

  //
  // writes out serialized objects
  // 
  static inline void WriteSerializedBytes(const char* array, const long src_buffer_size, size_t* inst,
					  size_t* &op_stack, long* &stack_pos) {
    size_t* dest_buffer = (size_t*)inst[0];
    if(array && dest_buffer) {
      const long dest_pos = (long)inst[1];

      // expand buffer, if needed
      dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
      inst[0] = (size_t)dest_buffer;

      // copy content
      char* dest_buffer_ptr = (char*)(dest_buffer + 3);
      memcpy(dest_buffer_ptr + dest_pos, array, src_buffer_size);
      inst[1] = dest_pos + src_buffer_size;
    }
  }
  
  //
  // serializes an array
  // 
  static inline void SerializeArray(const size_t* array, ParamType type, size_t* inst,
				    size_t* &op_stack, long* &stack_pos) {
    if(array) {
      SerializeByte(1, inst, op_stack, stack_pos);
      const long array_size = (long)array[0];

      // write values
      switch(type) {
      case BYTE_ARY_PARM: {
	// write metadata
	char* array_ptr = (char*)(array + 3);
	SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);
	// write data
	WriteSerializedBytes(array_ptr, array_size, inst, op_stack, stack_pos);
      }
	break;

      case CHAR_ARY_PARM: {
	// convert
	char* array_ptr = (char*)(array + 3);
	const string buffer = UnicodeToBytes((const wchar_t*)array_ptr);
	// write metadata	
	SerializeInt((INT_VALUE)buffer.size(), inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)buffer.size(), inst, op_stack, stack_pos);
	// write data
	WriteSerializedBytes((const char*)buffer.c_str(), (long)buffer.size(), inst, op_stack, stack_pos);
      }
	break;

      case INT_ARY_PARM: {
	// write metadata
	char* array_ptr = (char*)(array + 3);
	SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);
	// write data
	WriteSerializedBytes(array_ptr, array_size * sizeof(INT_VALUE), inst, op_stack, stack_pos);
      }
	break;
	
      case OBJ_ARY_PARM: {
	SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);

  size_t* array_ptr = (size_t*)(array + 3);
	for(int i = 0; i < array_size; ++i) {
    size_t* obj = (size_t*)array_ptr[i];
	  ObjectSerializer serializer(obj);
	  vector<char> src_buffer = serializer.GetValues();
	  const long src_buffer_size = (long)src_buffer.size();
    size_t* dest_buffer = (size_t*)inst[0];
	  long dest_pos = (long)inst[1];

	  // expand buffer, if needed
	  dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
	  inst[0] = (size_t)dest_buffer;

	  // copy content
	  char* dest_buffer_ptr = ((char*)(dest_buffer + 3) + dest_pos);
	  for(int j = 0; j < src_buffer_size; ++j, dest_pos++) {
	    dest_buffer_ptr[j] = src_buffer[j];
	  }
	  inst[1] = dest_pos;
	}
      }
	break;

      case FLOAT_ARY_PARM: {
	// write metadata
	char* array_ptr = (char*)(array + 3);
	SerializeInt((INT_VALUE)array[0], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[1], inst, op_stack, stack_pos);
	SerializeInt((INT_VALUE)array[2], inst, op_stack, stack_pos);
	// write data
	WriteSerializedBytes(array_ptr, array_size * sizeof(FLOAT_VALUE), inst, op_stack, stack_pos);
      }
	break;

      default:
	break;
      }
    }
    else {
      SerializeByte(0, inst, op_stack, stack_pos);
    }
  }

  //
  // reads a serialized array
  // 
  static inline void ReadSerializedBytes(size_t* dest_array, const size_t* src_array,
					 ParamType type, size_t* inst) {
    if(dest_array && src_array) {
      const long dest_pos = (long)inst[1];
      const long src_array_size = (long)src_array[0];
      long dest_array_size = (long)dest_array[0];

      if(dest_pos < src_array_size) {
	const char* src_array_ptr = (char*)(src_array + 3);	
	char* dest_array_ptr = (char*)(dest_array + 3);

	switch(type) {
	case BYTE_ARY_PARM:
	  memcpy(dest_array_ptr, src_array_ptr + dest_pos, dest_array_size);
	  break;

	case CHAR_ARY_PARM: {
	  // convert
	  const string in((const char*)src_array_ptr + dest_pos, dest_array_size);
	  const wstring out = BytesToUnicode(in);	
	  // copy
	  dest_array[0] = out.size();
	  dest_array[2] = out.size();
	  dest_array_size *= sizeof(wchar_t);
	  memcpy(dest_array_ptr, out.c_str(), out.size() * sizeof(wchar_t));
	}
	  break;

	case INT_ARY_PARM:
	  dest_array_size *= sizeof(INT_VALUE);
	  memcpy(dest_array_ptr, src_array_ptr + dest_pos, dest_array_size);
	  break;

	case FLOAT_ARY_PARM:
	  dest_array_size *= sizeof(FLOAT_VALUE);
	  memcpy(dest_array_ptr, src_array_ptr + dest_pos, dest_array_size);
	  break;

	default:
	  break;
	}

	inst[1] = dest_pos + dest_array_size;
      }
    }
  }

  //
  // deserializes an array of objects
  // 
  static inline size_t* DeserializeArray(ParamType type, size_t* inst, size_t* &op_stack, long* &stack_pos);

  //
  // expand buffer
  //
  static size_t* ExpandSerialBuffer(const long src_buffer_size, size_t* dest_buffer, size_t* inst,
				  size_t* &op_stack, long* &stack_pos);
  
  // 
  // serializes a byte
  // 
  static void SerializeByte(char value, size_t* inst, size_t* &op_stack, long* &stack_pos) {
    const long src_buffer_size = sizeof(value);
    size_t* dest_buffer = (size_t*)inst[0];
    
    if(dest_buffer) {
      const long dest_pos = (long)inst[1];

      // expand buffer, if needed
      dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
      inst[0] = (size_t)dest_buffer;

      // copy content
      char* dest_buffer_ptr = (char*)(dest_buffer + 3);
      memcpy(dest_buffer_ptr + dest_pos, &value, src_buffer_size);
      inst[1] = dest_pos + src_buffer_size;
    }
  }

  // 
  // deserializes a byte
  // 
  static char DeserializeByte(size_t* inst) {
    size_t* byte_array = (size_t*)inst[0];
    const long dest_pos = (long)inst[1];

    if(byte_array && dest_pos < (long)byte_array[0]) {
      const char* byte_array_ptr = (char*)(byte_array + 3);	
      char value;
      memcpy(&value, byte_array_ptr + dest_pos, sizeof(value));
      inst[1] = dest_pos + sizeof(value);

      return value;
    }

    return 0;
  }

  // 
  // serializes a char
  // 
  static void SerializeChar(wchar_t value, size_t* inst, size_t* &op_stack, long* &stack_pos) {
    // convert to bytes
    string out;
    CharacterToBytes(value, out);
    const long src_buffer_size = (long)out.size();
    SerializeInt((INT_VALUE)out.size(), inst, op_stack, stack_pos);

    // prepare copy   
    size_t* dest_buffer = (size_t*)inst[0];
    if(dest_buffer) {
      const long dest_pos = (long)inst[1];
      
      // expand buffer, if needed
      dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
      inst[0] = (size_t)dest_buffer;

      // copy content
      char* dest_buffer_ptr = (char*)(dest_buffer + 3);
      memcpy(dest_buffer_ptr + dest_pos, out.c_str(), src_buffer_size);
      inst[1] = dest_pos + src_buffer_size;
    }
  }

  // 
  // deserializes a char
  // 
  static wchar_t DeserializeChar(size_t* inst) {
    const int num = DeserializeInt(inst);      
    size_t* byte_array = (size_t*)inst[0];
    const long dest_pos = (long)inst[1];

    if(byte_array && dest_pos < (long)byte_array[0]) {
      const char* byte_array_ptr = (char*)(byte_array + 3);	
      char* in = new char[num + 1];
      memcpy(in, byte_array_ptr + dest_pos, num);
      in[num] = '\0';

      wchar_t out = L'\0';
      BytesToCharacter(in, out);

      // clean up
      delete[] in;
      in = NULL;

      inst[1] = dest_pos + num;

      return out;
    }

    return 0;
  }

  // 
  // serializes an int
  // 
  static void SerializeInt(INT_VALUE value, size_t* inst, size_t* &op_stack, long* &stack_pos) {
    const long src_buffer_size = sizeof(value);
    size_t* dest_buffer = (size_t*)inst[0];

    if(dest_buffer) {
      const long dest_pos = (long)inst[1];
      
      // expand buffer, if needed
      dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
      inst[0] = (size_t)dest_buffer;
      
      // copy content
      char* dest_buffer_ptr = (char*)(dest_buffer + 3);
      memcpy(dest_buffer_ptr + dest_pos, &value, src_buffer_size);
      inst[1] = dest_pos + src_buffer_size;
    }
  }

  // 
  // deserializes an int
  // 
  static INT_VALUE DeserializeInt(size_t* inst) {
    size_t* byte_array = (size_t*)inst[0];
    const long dest_pos = (long)inst[1];

    if(byte_array && dest_pos < (long)byte_array[0]) {
      const char* byte_array_ptr = (char*)(byte_array + 3);	
      INT_VALUE value;
      memcpy(&value, byte_array_ptr + dest_pos, sizeof(value));
      inst[1] = dest_pos + sizeof(value);

      return value;
    }

    return 0;
  }

  // 
  // serializes a float
  // 
  static void SerializeFloat(FLOAT_VALUE value, size_t* inst, size_t* &op_stack, long* &stack_pos) {
    const long src_buffer_size = sizeof(value);
    size_t* dest_buffer = (size_t*)inst[0];

    if(dest_buffer) {
      const long dest_pos = (long)inst[1];
      
      // expand buffer, if needed
      dest_buffer = ExpandSerialBuffer(src_buffer_size, dest_buffer, inst, op_stack, stack_pos);
      inst[0] = (size_t)dest_buffer;
      
      // copy content
      char* dest_buffer_ptr = (char*)(dest_buffer + 3);
      memcpy(dest_buffer_ptr + dest_pos, &value, src_buffer_size);
      inst[1] = dest_pos + src_buffer_size;
    }
  }

  // 
  // deserializes a float
  // 
  static FLOAT_VALUE DeserializeFloat(size_t* inst) {
    size_t* byte_array = (size_t*)inst[0];
    const long dest_pos = (long)inst[1];

    if(byte_array && dest_pos < (long)byte_array[0]) {
      const char* byte_array_ptr = (char*)(byte_array + 3);
      FLOAT_VALUE value;
      memcpy(&value, byte_array_ptr + dest_pos, sizeof(value));
      inst[1] = dest_pos + sizeof(value);      
      return value;
    }

    return 0.0;
  }

  //
  // serialize and deserialize object instances
  //
  static void SerializeObject(size_t* inst, StackFrame* frame, size_t* &op_stack, long* &stack_pos);
  static void DeserializeObject(size_t* inst, size_t* &op_stack, long* &stack_pos);

  //
  // time functions
  //
  static inline void ProcessCurrentTime(StackFrame* frame, bool is_gmt);
  static inline void ProcessSetTime1(size_t* &op_stack, long* &stack_pos);
  static inline void ProcessSetTime2(size_t* &op_stack, long* &stack_pos);
  static inline void ProcessSetTime3(size_t* &op_stack, long* &stack_pos);
  static inline void ProcessAddTime(TimeInterval t, size_t* &op_stack, long* &stack_pos);
  static inline void ProcessTimerStart(size_t* &op_stack, long* &stack_pos);
  static inline void ProcessTimerEnd(size_t* &op_stack, long* &stack_pos);
  static inline void ProcessTimerElapsed(size_t* &op_stack, long* &stack_pos);
  
  // 
  // platform string
  //
  static inline void ProcessPlatform(StackProgram* program, size_t* &op_stack, long* &stack_pos);

  // 
  // platform string
  //
  static inline void ProcessFileOwner(const char* name, bool is_account,
				      StackProgram* program, size_t* &op_stack, long* &stack_pos);
  
  // 
  // version string
  //
  static inline void ProcessVersion(StackProgram* program, size_t* &op_stack, long* &stack_pos);

  //
  // creates new object and call default constructor
  //
  static inline void CreateNewObject(const wstring &cls_id, size_t* &op_stack, long* &stack_pos);

  //
  // creates a new class instance
  //
  static inline void CreateClassObject(StackClass* cls, size_t* cls_obj, size_t* &op_stack, 
				       long* &stack_pos, StackProgram* program);

  //
  // creates an instance of the 'Method' class
  //
  static inline size_t* CreateMethodObject(size_t* cls_obj, StackMethod* mthd, StackProgram* program,
					 size_t* &op_stack, long* &stack_pos);

  //
  // creates a wstring object instance
  //
  static inline size_t* CreateStringObject(const wstring &value_str, StackProgram* program, size_t* &op_stack, long* &stack_pos);

 public:

  static bool ProcessTrap(StackProgram* program, size_t* inst, size_t* &op_stack, long* &stack_pos, StackFrame* frame);
};

/********************************
 * Call back for DLL method calls
 ********************************/
void APITools_MethodCall(size_t* op_stack, long *stack_pos, size_t* instance, 
                         const wchar_t* cls_id, const wchar_t* mthd_id);
void APITools_MethodCallId(size_t* op_stack, long *stack_pos, size_t* instance, 
                           const int cls_id, const int mthd_id);			 

#endif
