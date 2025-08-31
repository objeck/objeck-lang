/***************************************************************************
 * Defines the VM execution model.
 *
 * Copyright (c) 2025, Randy Hollines
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

#pragma once

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <stack>
#include <vector>
#include <list>
#include <set>
#include <string>
#include <ctime>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <process.h>
#include <unordered_map>
#include <unordered_set>
#include <userenv.h>
#include <cstring>
#include <bcrypt.h>
#elif _OSX
#include <sys/random.h> 
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#else
#include <sys/random.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <pwd.h>
#include <stdint.h>
#include <iomanip>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#define CALC_STACK_SIZE 16384
#define CACERT_PEM_FILE "cacert.pem"

#ifdef _WIN32
#define MUTEX_LOCK EnterCriticalSection
#define MUTEX_UNLOCK LeaveCriticalSection
#else
#define MUTEX_LOCK pthread_mutex_lock
#define MUTEX_UNLOCK pthread_mutex_unlock
#endif

using namespace instructions;

#define INF_ENDING -2

enum {
  VM_SIGABRT = -40,
  VM_SIGFPE,
  VM_SIGILL,
  VM_SIGINT,
  VM_SIGSEGV,
  VM_SIGTERM
};

class StackClass;

inline const std::wstring IntToString(int v)
{
  return std::to_wstring(v);
}

/********************************
 * StackDclr struct
 ********************************/
struct StackDclr 
{
  std::wstring name;
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
  union {
    long operand2;
    INT64_VALUE int64_operand;
    FLOAT_VALUE float_operand;
  } alt_operand;
  long operand3;
  long native_offset;
  int line_num;

 public:
  StackInstr(int l, INT64_VALUE v) {
    line_num = l;
    type = LOAD_INT_LIT;
    alt_operand.int64_operand = v;
  }

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

  StackInstr(int l, InstructionType t, FLOAT_VALUE f) {
    line_num = l;
    type = t;
    alt_operand.float_operand = f;
    operand = operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o1, long o2) {
    line_num = l;
    type = t;
    operand = o1;
    alt_operand.operand2 = o2;
    operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o1, long o2, long o3) {
    line_num = l;
    type = t;
    operand = o1;
    alt_operand.operand2 = o2;
    operand3 = o3;
    native_offset = 0;
  }

  ~StackInstr() {
  }  

  inline InstructionType GetType() const {
    return type;
  }

  inline long GetOperand() const {
    return operand;
  }

  inline long GetOperand2() const {
    return alt_operand.operand2;
  }

  inline long GetOperand3() const {
    return operand3;
  }

  inline INT64_VALUE GetInt64Operand() const {
    return alt_operand.int64_operand;
  }

  inline FLOAT_VALUE GetFloatOperand() const {
    return alt_operand.float_operand;
  }

  inline long GetOffset() const {
    return native_offset;
  }

  int GetLineNumber() const {
    return line_num;
  }

  inline void SetOperand3(long o3) {
    operand3 = o3;
  }

  inline void SetOffset(long o) {
    native_offset = o;
  }
};

/********************************
 * JIT compile code
 ********************************/
class NativeCode {
#if defined(_ARM64) || defined(_M_ARM64)
  uint32_t* code;
  int64_t* ints;
#else
  unsigned char* code;
#endif

  long size;
  FLOAT_VALUE* floats;
  
 public:
#if defined(_ARM64) || defined(_M_ARM64)
   NativeCode(uint32_t* c, long s, int64_t* i, FLOAT_VALUE* f) {
    code = c;
    size = s;
    ints = i;
    floats = f;
  }
#else
  NativeCode(unsigned char* c, long s, FLOAT_VALUE* f) {
    code = c;
    size = s;
    floats = f;
  }
#endif

  ~NativeCode() {
#ifdef _ARM64
    free(ints);
    ints = nullptr;
#endif
    
#ifdef _WIN64
    VirtualFree(floats, 0, MEM_RELEASE);
#else
    free(floats);
#endif
    floats = nullptr;
  }

#if defined(_ARM64) || defined(_M_ARM64)
  inline uint32_t* GetCode() const {
    return code;
  }
  
  inline int64_t* GetInts() const {
    return ints;
  }
#else
  inline unsigned char* GetCode() const {
    return code;
  }
#endif
  
  inline long GetSize() {
    return size;
  }

  inline FLOAT_VALUE* GetFloats() const {
    return floats;
  }
};

/********************************
 * StackMethod class
 ********************************/
class StackMethod {
  long id;
  std::wstring name;
  bool is_virtual;
  bool has_and_or;
  bool is_lambda;
  StackInstr** instrs;  
  int instr_count;  
  long param_count;
  long mem_size;
  NativeCode* native_code;
  MemoryType rtrn_type;
  StackDclr** dclrs;
  long num_dclrs;
  StackClass* cls;

  const std::wstring ParseName(const std::wstring &name) const;

 public:
  StackMethod(long i, const std::wstring &n, bool v, bool h, bool l, StackDclr** d, long nd, long p, long m, MemoryType r, StackClass* k) {
    id = i;
    name = ParseName(n);
    is_virtual = v;
    has_and_or = h;
    is_lambda = l;
    native_code = nullptr;
    dclrs = d;
    num_dclrs = nd;
    param_count = p;
    mem_size = m;
    rtrn_type = r;
    cls = k;
    instrs = nullptr;
    instr_count = 0;
  }

  ~StackMethod() {
    // clean up
    if(dclrs) {
      for(int i = 0; i < num_dclrs; ++i) {
        StackDclr* tmp = dclrs[i];
        delete tmp;
        tmp = nullptr;
      }
      delete[] dclrs;
      dclrs = nullptr;
    }

    // clean up
    if(native_code) {
      delete native_code;
      native_code = nullptr;
    }

    // clean up
    for(int i = 0; i < instr_count; ++i) {
      StackInstr* tmp = instrs[i];

      switch(tmp->GetType()) {
      case RTRN:
        // int operations
      case ADD_INT:
      case SUB_INT:
      case MUL_INT:
      case DIV_INT:
      case MOD_INT:
      case LES_INT:
      case GTR_INT:
      case EQL_INT:
      case NEQL_INT:
      case LES_EQL_INT:
      case GTR_EQL_INT:
      case OR_INT:
      case AND_INT:
      case SWAP_INT:
      case BIT_AND_INT:
      case BIT_OR_INT:
      case BIT_XOR_INT:
      case BIT_NOT_INT:
      case SHL_INT:
      case SHR_INT:
        // float operations
      case ADD_FLOAT:
      case SUB_FLOAT:
      case MUL_FLOAT:
      case DIV_FLOAT:
      case MOD_FLOAT:
      case LES_FLOAT:
      case GTR_FLOAT:
      case EQL_FLOAT:
      case NEQL_FLOAT:
      case LES_EQL_FLOAT:
      case GTR_EQL_FLOAT:
      case SQRT_FLOAT:
      case RAND_FLOAT:
      case ASIN_FLOAT:
      case ACOS_FLOAT:
      case ATAN_FLOAT:
      case LOG2_FLOAT:
      case CBRT_FLOAT:
      case ATAN2_FLOAT:
      case ACOSH_FLOAT:
      case ASINH_FLOAT:
      case ATANH_FLOAT:
      case COSH_FLOAT:
      case SINH_FLOAT:
      case TANH_FLOAT:
      case LOG_FLOAT:
      case ROUND_FLOAT:
      case EXP_FLOAT:
      case LOG10_FLOAT:
      case POW_FLOAT:
      case GAMMA_FLOAT:
      case NAN_INT:
      case INF_INT:
      case NEG_INF_INT:
      case NAN_FLOAT:
      case INF_FLOAT:
      case NEG_INF_FLOAT:
      case CEIL_FLOAT:
      case TRUNC_FLOAT:
      case FLOR_FLOAT:
      case SIN_FLOAT:
      case COS_FLOAT:
      case TAN_FLOAT:
        // conversions
      case I2F:
      case F2I:
      case S2I:
      case S2F:
      case I2S:
      case F2S:
        // shared libraries
      case LOAD_ARY_SIZE:
      case EXT_LIB_LOAD:
      case EXT_LIB_UNLOAD:
      case EXT_LIB_FUNC_CALL:
        // thread
      case THREAD_JOIN:
      case THREAD_SLEEP:
      case THREAD_MUTEX:
      case CRITICAL_START:
      case CRITICAL_END:
        // copy and clear
      case CPY_BYTE_ARY:
      case CPY_CHAR_ARY:
      case CPY_INT_ARY:
      case CPY_FLOAT_ARY:
      case ZERO_BYTE_ARY:
      case ZERO_CHAR_ARY:
      case ZERO_INT_ARY:
      case ZERO_FLOAT_ARY:
        // program flow
      case POP_INT:
      case POP_FLOAT:
        break;

      default:
        delete tmp;
        tmp = nullptr;
        break;
      }
    }
    delete[] instrs;
    instrs = nullptr;
  }

  inline const std::wstring& GetName() {
    return name;
  }

  inline bool IsVirtual() {
    return is_virtual;
  }

  inline bool HasAndOr() {
    return has_and_or;
  }

  inline bool IsLambda() {
    return is_lambda;
  }

  inline StackClass* GetClass() const {
    return cls;
  }

#ifdef _DEBUGGER
  // TODO: might have 1 or more variables with the same name
  bool GetDeclaration(const std::wstring& name, StackDclr& found) {
    std::vector<int> results;
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < num_dclrs; i++, index++) {
        StackDclr* dclr = dclrs[i];
        const std::wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(L':') + 1);       
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

  inline const long GetNumberDeclarations() {
    return num_dclrs;
  }

  void SetNativeCode(NativeCode* c) {
    native_code = c;
  }

  inline NativeCode* GetNativeCode() const {
    return native_code;
  }

  MemoryType GetReturn() const {
    return rtrn_type;
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
};

/********************************
 * StackClass class
 ********************************/
using virtual_key_pair = std::pair<size_t, size_t>;

class StackClass {
  std::unordered_map<std::wstring, StackMethod*> method_name_map;
  StackMethod** methods;
  int method_num;
  long id;
  std::wstring name;
  std::wstring file_name;
  StackClass* parent;
  long pid;
  bool is_virtual;
  long cls_space;
  long inst_space;
  StackDclr** cls_dclrs;
  long cls_num_dclrs;
  StackDclr** inst_dclrs;
  std::map<int, std::pair<int, StackDclr**> > closure_dclrs;
  long inst_num_dclrs;
  size_t* cls_mem;
  bool is_debug;
  
  // Szudzik pairing function
  struct virtual_key_pair_hash {
    template <class T1, class T2>
    size_t operator() (const std::pair<T1, T2>& virtual_key_pair) const {
      const size_t x = virtual_key_pair.first;
      const size_t y = virtual_key_pair.second;

      return x >= y ? x * x + x + y : y * y + x;
    }
  };
  std::unordered_map<virtual_key_pair, StackMethod*, virtual_key_pair_hash> virtual_methods;
  
  long InitializeClassMemory(long size) {
    if(size > 0) {
      cls_mem = (size_t*)calloc(size, sizeof(char));
    }
    else {
      cls_mem = nullptr;
    }
    return size;
  }

 public:
  StackClass(long i, const std::wstring &cn, const std::wstring &fn, long p, bool v, StackDclr** cdclrs, long ccount,
             StackDclr** idclrs, std::map<int, std::pair<int, StackDclr**> > fdclr, long icount, long cspace, long ispace, bool d) {
    id = i;
    name = cn;
    file_name = fn;
    parent = nullptr;
    pid = p;
    is_virtual = v;
    cls_dclrs = cdclrs;
    cls_num_dclrs = ccount;
    inst_dclrs = idclrs;
    closure_dclrs = fdclr;
    inst_num_dclrs = icount;
    cls_space = InitializeClassMemory(cspace);
    inst_space = ispace;
    is_debug = d;
  }

  ~StackClass() {
    // clean up
    if(cls_dclrs) {
      for(int i = 0; i < cls_num_dclrs; ++i) {
        StackDclr* tmp = cls_dclrs[i];
        delete tmp;
        tmp = nullptr;
      }
      delete[] cls_dclrs;
      cls_dclrs = nullptr;
    }

    if(inst_dclrs) {
      for(int i = 0; i < inst_num_dclrs; ++i) {
        StackDclr* tmp = inst_dclrs[i];
        delete tmp;
        tmp = nullptr;
      }
      delete[] inst_dclrs;
      inst_dclrs = nullptr;
    }

    std::map<int, std::pair<int, StackDclr**> >::iterator iter;
    for(iter = closure_dclrs.begin(); iter != closure_dclrs.end(); ++iter) {
      std::pair<int, StackDclr**> tmp = iter->second;
      const int num_dclrs = tmp.first;
      StackDclr** dclrs = tmp.second;
      for(int i = 0; i < num_dclrs; ++i) {
        StackDclr* tmp2 = dclrs[i];
        delete tmp2;
        tmp2 = nullptr;
      }
      delete[] dclrs;
      dclrs = nullptr;
    }
    closure_dclrs.clear();

    for(int i = 0; i < method_num; ++i) {
      StackMethod* method = methods[i];
      delete method;
      method = nullptr;
    }
    delete[] methods;
    methods = nullptr;

    if(cls_mem) {
      free(cls_mem);
      cls_mem = nullptr;
    }
  }

  inline long GetId() const {
    return id;
  }

  bool IsDebug() {
    return is_debug;
  }

  inline const std::wstring& GetName() {
    return name;
  }

  inline const std::wstring& GetFileName() {
    return file_name;
  }

  inline StackDclr** GetClassDeclarations() const {
    return cls_dclrs;
  }

  inline long GetNumberClassDeclarations() const {
    return cls_num_dclrs;
  }

  inline StackDclr** GetInstanceDeclarations() const {
    return inst_dclrs;
  }

  inline std::pair<int, StackDclr**> GetClosureDeclarations(const int id) {
    return closure_dclrs[id];
  }

  inline long GetNumberInstanceDeclarations() const {
    return inst_num_dclrs;
  }

  StackClass* GetParent();

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
    // add method signature to map virtual calls
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
  std::vector<StackMethod*> GetMethods(const std::wstring &n) {
    std::vector<StackMethod*> found;
    for(int i = 0; i < method_num; ++i) {
      if(methods[i]->GetName().find(n) != std::wstring::npos) {
        found.push_back(methods[i]);
      }
    }

    return found;
  }

  // TODO: might have 1 or more variables with the same name
  bool GetDeclaration(const std::wstring& name, StackDclr& found, MemoryContext &context) {
    std::vector<int> results;
    if(name.size () > 0) {
      // search for instance name
      int index = 0;
      for(int i = 0; i < inst_num_dclrs; i++, index++) {
        StackDclr* dclr = inst_dclrs[i];
        const std::wstring& dclr_name = dclr->name.substr (dclr->name.find_last_of (L':') + 1);
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;
          context = INST;
          return true;
        }

        if(dclr->type == FLOAT_PARM || dclr->type == FUNC_PARM) {
          index++;
        }
      }

      // search for class name
      index = 0;
      for(int i = 0; i < cls_num_dclrs; i++, index++) {
        StackDclr* dclr = cls_dclrs[i];
        const std::wstring& dclr_name = dclr->name.substr (dclr->name.find_last_of (L':') + 1);
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;
          context = CLS;
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

  inline StackMethod* GetMethod(const std::wstring &n) {
    std::unordered_map<std::wstring, StackMethod*>::iterator result = method_name_map.find(n);
    if(result != method_name_map.end()) {
      return result->second;
    }

    return nullptr;
  }

  inline StackMethod** GetMethods() const {
    return methods;
  }

  inline int GetMethodCount() const {
    return method_num;
  }

#ifdef _DEBUGGER
  bool GetInstanceDeclaration(const std::wstring& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < inst_num_dclrs; i++, index++) {
        StackDclr* dclr = inst_dclrs[i];
        const std::wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(L':') + 1);       
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

  bool GetClassDeclaration(const std::wstring& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < cls_num_dclrs; i++, index++) {
        StackDclr* dclr = cls_dclrs[i];
        const std::wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(L':') + 1);       
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

  StackMethod* GetVirtualMethod(size_t virtual_cls_id, size_t virtual_mthd_id) {
    const auto virtual_key = std::make_pair(virtual_cls_id, virtual_mthd_id);
    const auto result = virtual_methods.find(virtual_key);
    if(result != virtual_methods.end()) {
      return result->second;
    }

    return nullptr;
  }

  void AddVirutalMethod(size_t virtual_cls_id, size_t virtual_mthd_id, StackMethod* mthd) {
    // TODO: thread safe?
    const virtual_key_pair virtual_key = std::make_pair(virtual_cls_id, virtual_mthd_id);
    virtual_methods.insert(std::pair<virtual_key_pair, StackMethod*>(virtual_key, mthd));
  }
};

/********************************
 * StackProgram class
 ********************************/
class StackProgram {
  std::map<std::wstring, StackClass*> cls_map;
  StackClass** classes;
  long class_num;
  long* cls_hierarchy;
  long** cls_interfaces;
  long string_cls_id;
  long cls_cls_id;
  long mthd_cls_id;
  long sock_cls_id;
  long data_type_cls_id;
  long command_output_cls_id;
  StackMethod* init_method;
  static std::map<std::wstring, std::wstring> properties_map;
	static std::unordered_map<long, StackMethod*> signal_handler_func;
  
  FLOAT_VALUE** float_strings;
  int num_float_strings;

  bool** bool_strings;
  int num_bool_strings;

  char** byte_strings;
  int num_byte_strings;

  INT64_VALUE** int_strings;
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
#ifdef _MODULE
   std::wstringstream output_buffer;
#endif

  StackProgram() {
    cls_hierarchy = nullptr;
    cls_interfaces = nullptr;
    classes = nullptr;
    char_strings = nullptr;
    string_cls_id = cls_cls_id = mthd_cls_id = sock_cls_id = data_type_cls_id = command_output_cls_id = -1;
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
        klass = nullptr;
      }
      delete[] classes;
      classes = nullptr;
    }

    if(cls_hierarchy) {
      delete[] cls_hierarchy;
      cls_hierarchy = nullptr;
    }

    if(cls_interfaces) {
      for(int i = 0; i < class_num; ++i) {
        delete[] cls_interfaces[i];
        cls_interfaces[i] = nullptr;
      }
      delete[] cls_interfaces;
      cls_interfaces = nullptr;
    }

    if(float_strings) {
      for(int i = 0; i < num_float_strings; ++i) {
        FLOAT_VALUE* tmp = float_strings[i];
        delete[] tmp;
        tmp = nullptr;
      }
      delete[] float_strings;
      float_strings = nullptr;
    }

    if(int_strings) {
      for(int i = 0; i < num_int_strings; ++i) {
        INT64_VALUE* tmp = int_strings[i];
        delete[] tmp;
        tmp = nullptr;
      }
      delete[] int_strings;
      int_strings = nullptr;
    }

    if(char_strings) {
      for(int i = 0; i < num_char_strings; ++i) {
        wchar_t* tmp = char_strings[i];
        delete [] tmp;
        tmp = nullptr;
      }
      delete[] char_strings;
      char_strings = nullptr;
    }

    if(init_method) {
      delete init_method;
      init_method = nullptr;
    }

#ifdef _WIN32
    DeleteCriticalSection(&program_cs);
    DeleteCriticalSection(&prop_cs);
#endif
  }

#ifdef _WIN32
  static std::wstring GetProperty(const std::wstring& key) {
    std::wstring value;
    EnterCriticalSection(&prop_cs);

    if(properties_map.size() == 0) {
      InitializeProprieties();
    }
    
    std::map<std::wstring, std::wstring>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }

    LeaveCriticalSection(&prop_cs);
    return value;
  }

  static void SetProperty(const std::wstring& key, const std::wstring& value) {
    EnterCriticalSection(&prop_cs);

    if(properties_map.size() == 0) {
      InitializeProprieties();
    }

    properties_map.insert(std::pair<std::wstring, std::wstring>(key, value));
    LeaveCriticalSection(&prop_cs);
  }

  static BOOL GetUserDirectory(wchar_t* buf, DWORD len) {
    HANDLE handle;

    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &handle)) {
      return FALSE;
    }

    if(!GetUserProfileDirectoryW(handle, buf, &len)) {
      return FALSE;
    }

    CloseHandle(handle);
    return TRUE;
  }
#else
  static std::wstring GetProperty(const std::wstring& key) {
    std::wstring value;
    
    pthread_mutex_lock(&prop_mutex);
    
    if(properties_map.size() == 0) {
      InitializeProprieties();
    }
    
    std::map<std::wstring, std::wstring>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }
    pthread_mutex_unlock(&prop_mutex);

    return value;
  }

  static void SetProperty(const std::wstring& key, const std::wstring& value) {
    pthread_mutex_lock(&prop_mutex);
    properties_map.insert(std::pair<std::wstring, std::wstring>(key, value));
    pthread_mutex_unlock(&prop_mutex);
  }
#endif

  static bool AddSignalHandler(long key, StackMethod* mthd);
  StackMethod* GetSignalHandler(long key);

  static void InitializeProprieties();
  
  void SetInitializationMethod(StackMethod* i) {
    init_method = i;
  }

  StackMethod* GetInitializationMethod() const {
    return init_method;
  }

  const long GetStringObjectId() const {
    return string_cls_id;
  }

  void SetStringObjectId(int id) {
    string_cls_id = id;
  }

   const long GetClassObjectId() {
    if(cls_cls_id < 0) {
      StackClass* cls = GetClass(L"System.Introspection.Class");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.Introspection.Class <<<" << std::endl;
        exit(1);
      }
      cls_cls_id = cls->GetId();
    }

    return cls_cls_id;
  }

   const long GetMethodObjectId() {
    if(mthd_cls_id < 0) {
      StackClass* cls = GetClass(L"System.Introspection.Method");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.Introspection.Method <<<" << std::endl;
        exit(1);
      }
      mthd_cls_id = cls->GetId();
    }

    return mthd_cls_id;
  }

   const long GetSocketObjectId() {
    if(sock_cls_id < 0) {
      StackClass* cls = GetClass(L"System.IO.Net.TCPSocket");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.IO.Net.TCPSocket <<<" << std::endl;
        exit(1);
      }
      sock_cls_id = cls->GetId();
    }

    return sock_cls_id;
  }

	 const long GetSecureSocketObjectId() {
		 if(sock_cls_id < 0) {
			 StackClass* cls = GetClass(L"System.IO.Net.TCPSecureSocket");
			 if(!cls) {
				 std::wcerr << L">>> Internal error: unable to find class: System.IO.Net.TCPSecureSocket <<<" << std::endl;
				 exit(1);
			 }
			 sock_cls_id = cls->GetId();
		 }

		 return sock_cls_id;
	 }

   const long GetDataTypeObjectId() {
     if(data_type_cls_id < 0) {
       StackClass* cls = GetClass(L"System.Introspection.DataType");
       if(!cls) {
         std::wcerr << L">>> Internal error: unable to find class: System.Introspection.DataType <<<" << std::endl;
         exit(1);
       }
       data_type_cls_id = cls->GetId();
     }

     return data_type_cls_id;
   }

   const long GetCommandOutputObjectId() {
     if(command_output_cls_id < 0) {
       StackClass* cls = GetClass(L"System.CommandOutput");
       if(!cls) {
         std::wcerr << L">>> Internal error: unable to find class: System.Introspection.DataType <<<" << std::endl;
         exit(1);
       }
       command_output_cls_id = cls->GetId();
     }

     return command_output_cls_id;
   }

  void SetFloatStrings(FLOAT_VALUE** s, int n) {
    float_strings = s;
    num_float_strings = n;
  }

  void SetBoolStrings(bool** s, int n) {
    bool_strings = s;
    num_bool_strings = n;
  }

  void SetByteStrings(char** s, int n) {
    byte_strings = s;
    num_byte_strings = n;
  }
  
  void SetIntStrings(INT64_VALUE** s, int n) {
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

  INT64_VALUE** GetIntStrings() const {
    return int_strings;
  }

  bool** GetBoolStrings() const {
    return bool_strings;
  }

  char** GetByteStrings() const {
    return byte_strings;
  }

  wchar_t** GetCharStrings() const {
    return char_strings;
  }

  void SetClasses(StackClass** clss, const int num) {
    classes = clss;
    class_num = num;

    for(int i = 0; i < num; ++i) {
      const std::wstring &name = clss[i]->GetName();
      if(name.size() > 0) {  
        cls_map.insert(std::pair<std::wstring, StackClass*>(name, clss[i]));
      }
    }
  }

  StackClass* GetClass(const std::wstring &n) {
    if(classes) {
      std::map<std::wstring, StackClass*>::iterator find = cls_map.find(n);
      if(find != cls_map.end()) {
        return find->second;
      }
    }

    return nullptr;
  }

  void SetHierarchy(long* h) {
    cls_hierarchy = h;
  }

  inline long* GetHierarchy() const {
    return cls_hierarchy;
  }

  void SetInterfaces(long** i) {
    cls_interfaces = i;
  }

  inline long** GetInterfaces() const {
    return cls_interfaces;
  }  

  inline StackClass* GetClass(long id) const {
    if(id > -1 && id < class_num) {
      return classes[id];
    }

    return nullptr;
  }

  inline StackClass** GetClasses() const {
    return classes;
  }

  inline long GetClassNumber() const {
    return class_num;
  }

#ifdef _DEBUGGER
  bool HasFile(const std::wstring &fn) {
    for(int i = 0; i < class_num; ++i) {
      if(classes[i]->GetFileName() == fn) {
        return true;
      }
    }

    return false;
  }
#endif

  public:
		//
		// TODO
		//
		static void SignalHandler(int signal);
};

/********************************
 * Call stack frame
 ********************************/
struct StackFrame {
  StackMethod* method;
  size_t* mem;
  long ip;
  bool jit_called;
  size_t* jit_mem;
  long jit_offset;
};

/********************************
 * Method signature formatter
 ********************************/
class MethodFormatter {
  static std::wstring FormatParameters(const std::wstring param_str);
  static std::wstring FormatType(const std::wstring type_str);
  static std::wstring FormatFunctionalType(const std::wstring func_str);
  
 public:
  static std::wstring Format(const std::wstring method_sig);
};

/********************************
 * ObjectSerializer class
 ********************************/
class ObjectSerializer 
{
  std::vector<char> values;
  std::map<size_t*, long> serial_ids;
  long next_id;
  long cur_id;
  
  void CheckObject(size_t* mem, bool is_obj, long depth);
  void CheckMemory(size_t* mem, StackDclr** dclrs, const long dcls_size, long depth);
  void Serialize(size_t* inst);

  bool WasSerialized(size_t* mem) {
    std::map<size_t*, long>::iterator find = serial_ids.find(mem);
    if(find != serial_ids.end()) {
      cur_id = find->second;
      SerializeInt((int64_t)cur_id);

      return true;
    }
    next_id++;
    cur_id = next_id * -1;
    serial_ids.insert(std::pair<size_t*, long>(mem, next_id));
    SerializeInt((int64_t)cur_id);

    return false;
  }

  inline void SerializeByte(const char v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeChar(const wchar_t v) {
    std::string out;
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

  std::vector<char>& GetValues() {
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
  size_t* stack_pos;
  StackClass* cls;
  size_t* instance;
  long instance_pos;
  std::map<INT_VALUE, size_t*> mem_cache;

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
    in = nullptr;

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
  ObjectDeserializer(const char* b, long s, size_t* stack, size_t* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = 0;
    cls = nullptr;
    instance = nullptr;
    instance_pos = 0;
  }

  ObjectDeserializer(const char* b, long o, std::map<INT_VALUE, size_t*> &c,
         long s, size_t* stack, size_t* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = o;
    mem_cache = c;
    cls = nullptr;
    instance = nullptr;
    instance_pos = 0;
  }

  ~ObjectDeserializer() {    
  }

  inline long GetOffset() const {
    return buffer_offset;
  }

  std::map<INT_VALUE, size_t*> GetMemoryCache() {
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
   // Returns: 1 = ready, 0 = not ready, -1 = error
#ifdef _WIN32
   static int socket_ready_for_io(SOCKET sock, bool is_write) {
#else
   static int socket_ready_for_io(int sock, bool is_write) {
#endif
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sock, &fds);

      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 0;  // zero timeout = poll

      int ret = -1;
#ifdef _WIN32
      // nfds is ignored on Windows, pass 0
      if(is_write) {
         ret = select(0, nullptr, &fds, nullptr, &tv);
      }
      else {
         ret = select(0, &fds, nullptr, nullptr, &tv);
      }

      if(ret == SOCKET_ERROR) {
         return -1;
      }
#else
      // POSIX: nfds = highest fd + 1
      if(is_write) {
         ret = select(sock + 1, nullptr, &fds, nullptr, &tv);
      }
      else {
         ret = select(sock + 1, &fds, nullptr, nullptr, &tv);
      }

      if(ret < 0) {
         return -1;
      }
#endif

      if(ret > 0 && FD_ISSET(sock, &fds)) {
         return 1; // socket ready
      }

      return 0; // not ready
   }


   // Returns: 1 = ready, 0 = not ready, -1 = error
   static int bio_ready_for_io(BIO* bio, bool is_write) {
      // For reads, see if OpenSSL already has decrypted bytes buffered.
      if(!is_write) {
         size_t pending = (size_t)BIO_ctrl_pending(bio);
         if(pending > 0) {
            return 1;
         }
      }

      // Get the underlying socket/file descriptor from the BIO.
      int fd = -1;
      if(BIO_get_fd(bio, &fd) <= 0 || fd < 0) {
         return -1;
      }

      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 0;

#ifdef _WIN32
      SOCKET s = (SOCKET)fd;

      fd_set rfds;
      FD_ZERO(&rfds);

      fd_set wfds;
      FD_ZERO(&wfds);

      if(is_write) {
         FD_SET(s, &wfds);
      }
      else {
         FD_SET(s, &rfds);
      }

      const int ret = select(0, is_write ? nullptr : &rfds, is_write ? &wfds : nullptr, nullptr, &tv);
      if(ret == SOCKET_ERROR) {
         return -1;
      }

      if(ret > 0) {
         if(!is_write && FD_ISSET(s, &rfds)) {
            return 1;
         }

         if(is_write && FD_ISSET(s, &wfds)) {
            return 1;
         }
      }

      return 0;

#else
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);

      int ret = select(fd + 1, is_write ? nullptr : &fds, is_write ? &fds : nullptr, nullptr, &tv);

      if(ret < 0) {
         return -1;
      }

      if(ret > 0 && FD_ISSET(fd, &fds)) {
         return 1;
      }

      return 0;
#endif
   }

  static inline bool GetTime(struct tm*& curr_time, time_t raw_time, bool is_gmt) {
#ifdef _WIN32
    struct tm temp_time;
    if(is_gmt) {
      if(gmtime_s(&temp_time, &raw_time)) {
        std::wcerr << L">>> Unable to get GMT time <<<" << std::endl;
        return false;
      }
    }
    else {
      if(localtime_s(&temp_time, &raw_time)) {
        std::wcerr << L">>> Unable to get GMT time <<<" << std::endl;
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

    return curr_time != nullptr;
  }

  //
  // pops an integer from the calculation stack.  this code
  // in normally inlined and there's a macro version available.
  //
  static inline size_t PopInt(size_t* op_stack, size_t* stack_pos) {    
#ifdef _DEBUG
    size_t v = op_stack[--(*stack_pos)];
    std::wcout << L"  [pop_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (size_t*)v << L")]" << std::endl;
    return v;
#else
    return op_stack[--(*stack_pos)];
#endif
  }

  //
  // pushes an integer onto the calculation stack. this code
  // in normally inlined and there's a macro version available.
  //
  static inline void PushInt(size_t v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
    std::wcout << L"  [push_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (size_t*)v << L")]" << std::endl;
#endif
    op_stack[(*stack_pos)++] = v;
  }

  //
  // pushes an double onto the calculation stack.
  //
  static inline void PushFloat(FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
    std::wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << std::endl;
#endif
    memcpy(&op_stack[(*stack_pos)], &v, sizeof(FLOAT_VALUE));
    (*stack_pos)++;
  }

  //
  // swaps two integers on the calculation stack
  //
  static inline void SwapInt(size_t* op_stack, size_t* stack_pos) {
    size_t v = op_stack[(*stack_pos) - 2];
    op_stack[(*stack_pos) - 2] = op_stack[(*stack_pos) - 1];
    op_stack[(*stack_pos) - 1] = v;
  }

  //
  // pops a double from the calculation stack
  //
  static inline FLOAT_VALUE PopFloat(size_t* op_stack, size_t* stack_pos) {
    FLOAT_VALUE v;
    (*stack_pos)--;

    memcpy(&v, &op_stack[(*stack_pos)], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
    std::wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << std::endl;
#endif

    return v;
  }

  //
  // peeks at the integer on the top of the
  // execution stack.
  //
  static inline size_t TopInt(size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
    size_t v = op_stack[(*stack_pos) - 1];
    std::wcout << L"  [top_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (void*)v << L")]" << std::endl;
    return v;
#else
    return op_stack[(*stack_pos) - 1];
#endif
  }

  //
  // peeks at the double on the top of the
  // execution stack.
  //
  static inline FLOAT_VALUE TopFloat(size_t* op_stack, size_t* stack_pos) {
    FLOAT_VALUE v;

    size_t index = (*stack_pos) - 1;
    memcpy(&v, &op_stack[index], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
    std::wcout << L"  [top_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << std::endl;
#endif

    return v;
  }

  // main trap functions
  static bool LoadClsInstId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool HashStringId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool LoadNewObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool LoadClsByInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool ConvertBytesToUnicode(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool ConvertUnicodeToBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool LoadMultiArySize(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyCharStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyCharStrArys(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyIntStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyBoolStrAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CpyByteStrAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CpyFloatStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutBool(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
	static bool StdOutIntFrmt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutFloatFrmt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutFloatPer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutWidth(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutFill(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutByteAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutCharAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdInByteAryLen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdInCharAryLen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrBool(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdErrByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool AssertTrue(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SysCmd(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool SetSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool RaiseSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SysCmdOut(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool Exit(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GmtTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SysTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeSet1(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeSet2(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeAddDays(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeAddHours(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateTimeAddMins(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeAddSecs(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateToUnixTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateFromUnixTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateFromLocalTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool TimerStart(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool TimerEnd(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool TimerElapsed(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GetPltfrm(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool GetUuid(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool GetVersion(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GetSysEnv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SetSysEnv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpResolveName(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpHostName(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpBind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpListen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
	static bool SockTcpSslError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslIssuer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslCertSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslListen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslCloseSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpBind(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SerlChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool UncompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CompressGzipBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool UncompressGzipBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CompressBrBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool UncompressBrBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CRC32Bytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenRead(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenAppend(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileRewind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  
  static bool PipeCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOpen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* fram);
  
  static bool SockTcpIsConnected(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileSeek(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileEof(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileIsOpen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileCanWriteOnly(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileCanReadOnly(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileCanReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileExists(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileSize(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileFullPath(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileTempName(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileAccountOwner(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileGroupOwner(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileDelete(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileRename(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileCreateTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileModifiedTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileAccessedTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DirCreate(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DirSlash(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirExists(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DirList(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirDelete(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirGetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirSetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkLoc(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkExists(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool HardLinkCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  
  //
  // writes out serialized objects
  // 
  static void WriteSerializedBytes(const char* array, const long src_buffer_size, size_t* inst,
            size_t* &op_stack, size_t* &stack_pos);
  
  //
  // serializes an array
  // 
  static void SerializeArray(const size_t* array, ParamType type, size_t* inst,
            size_t* &op_stack, size_t* &stack_pos);

  //
  // reads a serialized array
  // 
  static void ReadSerializedBytes(size_t* dest_array, const size_t* src_array,
           ParamType type, size_t* inst);

  //
  // deserializes an array of objects
  // 
  static inline size_t* DeserializeArray(ParamType type, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  //
  // expand buffer
  //
  static size_t* ExpandSerialBuffer(const long src_buffer_size, size_t* dest_buffer, size_t* inst,
          size_t* &op_stack, size_t* &stack_pos);
  
  // 
  // serializes a byte
  // 
  static void SerializeByte(char value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes a byte
  // 
  static char DeserializeByte(size_t* inst);

  // 
  // serializes a char
  // 
  static void SerializeChar(wchar_t value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes a char
  // 
  static wchar_t DeserializeChar(size_t* inst);

  // 
  // serializes an int
  // 
  static void SerializeInt(INT_VALUE value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes an int
  // 
  static INT_VALUE DeserializeInt(size_t* inst);

  // 
  // serializes a float
  // 
  static void SerializeFloat(FLOAT_VALUE value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes a float
  // 
  static FLOAT_VALUE DeserializeFloat(size_t* inst);

  //
  // serialize and deserialize object instances
  //
  static void SerializeObject(size_t* inst, StackFrame* frame, size_t* &op_stack, size_t* &stack_pos);
  static void DeserializeObject(size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  //
  // time functions
  //
  static inline void ProcessCurrentTime(StackFrame* frame, bool is_gmt);
  static inline void ProcessSetTime1(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessSetTime2(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessSetTime3(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessAddTime(TimeInterval t, size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessTimerStart(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessTimerEnd(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessTimerElapsed(size_t* &op_stack, size_t* &stack_pos);
  
  // 
  // platform string
  //
  static inline void ProcessPlatform(StackProgram* program, size_t* &op_stack, size_t* &stack_pos);

  // 
  // platform string
  //
  static inline void ProcessFileOwner(const char* name, bool is_account,
                                      StackProgram* program, size_t* &op_stack, size_t* &stack_pos);
  
  // 
  // version string
  //
  static inline void ProcessVersion(StackProgram* program, size_t* &op_stack, size_t* &stack_pos);

  //
  // creates new object and call default constructor
  //
  static inline bool CreateNewObject(const std::wstring &cls_id, size_t* &op_stack, size_t* &stack_pos);

  //
  // creates a new class instance
  //
  static inline void CreateClassObject(StackClass* cls, size_t* cls_obj, size_t* &op_stack, 
               size_t* &stack_pos, StackProgram* program);

  //
  // creates an instance of the 'Method' class
  //
  static inline size_t* CreateMethodObject(size_t* cls_obj, StackMethod* mthd, StackProgram* program,
           size_t* &op_stack, size_t* &stack_pos);

  //
  // creates a string object instance
  //
  static inline size_t* CreateStringObject(const std::wstring &value_str, StackProgram* program, size_t* &op_stack, size_t* &stack_pos);

 public:

  static bool ProcessTrap(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);

#ifdef _MODULE
  static StackProgram* program;

  static void Initialize(StackProgram* p) {
    program = p;
  }
#endif
};

bool EndsWith(std::wstring const& str, std::wstring const& ending);

/********************************
 * Call back for DLL method calls
 ********************************/
void APITools_MethodCall(size_t* op_stack, size_t *stack_pos, size_t* instance, 
                         const wchar_t* cls_id, const wchar_t* mthd_id);
void APITools_MethodCallId(size_t* op_stack, size_t *stack_pos, size_t* instance, 
                           const int cls_id, const int mthd_id);

/********************************
 * SSL password callback
 ********************************/
int pem_passwd_cb(char* buffer, int size, int rw_flag, void* passwd);

/********************************
 * GUUID/UUID generation (version 4)
 ********************************/

inline void secure_random_bytes(std::uint8_t* dst, std::size_t len) {
#if defined(_WIN32)
   if(BCryptGenRandom(nullptr, dst, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      throw std::runtime_error("BCryptGenRandom failed");
   }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
   arc4random_buf(dst, len);
#elif defined(__linux__)
   // Try getrandom first (no file descriptor needed)
   std::size_t done = 0;
   while(done < len) {
      ssize_t r = getrandom(dst + done, len - done, 0);
      if(r > 0) { done += static_cast<std::size_t>(r); continue; }
      if(r < 0 && errno == EINTR) continue;

      // Fallback to /dev/urandom if getrandom not available or blocked
      int fd = open("/dev/urandom", O_RDONLY);
      if(fd < 0) throw std::runtime_error("open(/dev/urandom) failed");
      std::size_t got = 0;
      while(got < (len - done)) {
         ssize_t rr = read(fd, dst + done + got, (len - done) - got);
         if(rr > 0) { got += static_cast<std::size_t>(rr); continue; }
         if(rr < 0 && errno == EINTR) continue;
         close(fd);
         throw std::runtime_error("read(/dev/urandom) failed");
      }
      close(fd);

      done += got;
   }
#else
   // Very portable fallback; quality depends on the implementation.
   // Prefer adding a platform path above if you target a specific OS.
   for(std::size_t i = 0; i < len; ++i) {
      dst[i] = static_cast<std::uint8_t>(std::rand() & 0xFF); \
   }
#endif
 }

 inline std::wstring uuidv4() {
   std::array<std::uint8_t, 16> bytes{};
   secure_random_bytes(bytes.data(), bytes.size());

   // Set version (4) and variant (RFC 4122)
   bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40); // version 4
   bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80); // variant 10xxxxxx

   // Format 8-4-4-4-12
   std::wostringstream oss;
   oss << std::hex << std::nouppercase << std::setfill(L'0');
   for(int i = 0; i < 16; ++i) {
      oss << std::setw(2) << static_cast<int>(bytes[i]);
      if(i == 3 || i == 5 || i == 7 || i == 9) {
         oss << '-';
      }
   }

   return oss.str();
 }