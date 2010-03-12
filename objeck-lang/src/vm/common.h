/***************************************************************************
 * Defines the VM execution model.
 *
 * Copyright (c) 2008, 2009 Randy Hollines
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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"

#ifdef _MEMCHECK
#include <mcheck.h>
#endif

#ifdef _WIN32
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#else
#include <stdint.h>
#endif

using namespace std;
using namespace instructions;

/*
// vm data type mapping
#ifdef _X64
#define INT_LIT INT_LIT INT_LIT
#else
#define INT_LIT long
#endif
#define FLOAT_VALUE double
#define BYTE_VALUE unsigned char
*/

class StackClass;

inline string IntToString(int v)
{
  ostringstream str;
  str << v;
  return str.str();
}


/********************************
 * Jump address jump table
 ********************************/
#define NUM_BUCKETS 150061
struct JumpBucket 
{
  int key;
  int value;
  JumpBucket* next;
};

class JumpTable 
{
  JumpBucket* buckets[NUM_BUCKETS];
  
  int hash(int key) {
    key = (key ^ 61) ^ (key >> 16);
    key = key + (key << 3);
    key = key ^ (key >> 4);
    key = key * 0x27d4eb2d;
    key = key ^ (key >> 15);

    return key % NUM_BUCKETS;
  }
  
 public:
  JumpTable() {
    memset(&buckets, 0, sizeof(JumpBucket*) * NUM_BUCKETS);
  }

  ~JumpTable() {
    for(int i = 0; i < NUM_BUCKETS; i++) {
      JumpBucket* bucket = buckets[i];
      if(bucket) {
	JumpBucket* temp = bucket;
	bucket = bucket->next;
	// remove
	delete temp;
	temp = NULL;
      }
    }
  }
  
  int Find(int key) {
    JumpBucket* bucket = buckets[hash(key)];
    if(!bucket) {
      return -1;
    }
    else {
      while(bucket) {
	if(bucket->key == key) {
	  return bucket->value;	
	}
	bucket = bucket->next;
      }
    }

    return -1;
  }
  
  void Insert(int key, int value) {
    const int hash_key = hash(key);
    JumpBucket* bucket = buckets[hash_key];
    if(!bucket) {
      bucket = new JumpBucket;
      bucket->key = key;
      bucket->value = value;
      bucket->next = NULL;	
      buckets[hash_key] = bucket;
    }
    else {
      while(bucket->next) {
	bucket = bucket->next;
      }
      JumpBucket* temp = new JumpBucket;
      temp->key = key;
      temp->value = value;
      temp->next = NULL;
      bucket->next = temp;
    }
  }		
};

/********************************
 * StackDclr struct
 ********************************/
struct StackDclr {
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

public:
  StackInstr(InstructionType t) {
    type = t;
    operand = native_offset = 0;
  }

  StackInstr(InstructionType t, long o) {
    type = t;
    operand = o;
    native_offset = 0;
  }

  StackInstr(InstructionType t, FLOAT_VALUE fo) {
    type = t;
    float_operand = fo;
    operand = native_offset = 0;
  }

  StackInstr(InstructionType t, long o, long o2) {
    type = t;
    operand = o;
    operand2 = o2;
    native_offset = 0;
  }

  StackInstr(InstructionType t, long o, long o2, long o3) {
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
  bool is_compiling;

  //map<long, long> jump_table;
  JumpTable jump_table;

  long param_count;
  long mem_size;
  NativeCode* native_code;
  MemoryType rtrn_type;
  StackDclr** dclrs;
  long num_dclrs;
  StackClass* cls;

  const string ParseName(const string &name) {
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
        }
#endif

        // match ','
        index++;
      }
    }

    return name;
  }
  
public:
  pthread_mutex_t jit_mutex;
  
  StackMethod(long i, string &n, bool v, bool h, StackDclr** d, long nd,
              long p, long m, MemoryType r, StackClass* k) {
    pthread_mutex_init(&jit_mutex, NULL);
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
    is_compiling = false;
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

  inline const string GetName() {
    return name;
  }

  inline void SetCompiling(bool c) {
    is_compiling = c;
  }

  inline bool IsCompiling() {
    return is_compiling;
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

  void AddLabel(long label_id, long index) {
    // jump_table[label_id] = index;
    jump_table.Insert(label_id, index);
  }

  inline long GetLabelIndex(long label_id) {
    /*
    map<long, long>::iterator result = jump_table.find(label_id);
    // not found
    if(result == jump_table.end()) {
      return -1;
    }

    return result->second;
    */

    return jump_table.Find(label_id);
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
    const long size = mem_size + 1;
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
  long pid;
  bool is_virtual;
  long cls_space;
  long inst_space;
  StackDclr** dclrs;
  long num_dclrs;
  long* cls_mem;

  map<const string, StackMethod*> method_name_map;

  long InitMemory(long size) {
    cls_mem = new long[size];
    memset(cls_mem, 0, size * sizeof(long));

    return size;
  }

public:
  StackClass(long i, const string ne, long p, bool v,
             StackDclr** d, long n, long cs, long is) {
    id = i;
    name = ne;
    pid = p;
    is_virtual = v;
    dclrs = d;
    num_dclrs = n;
    cls_space = InitMemory(cs);
    inst_space  = is;
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

  inline const string GetName() {
    return name;
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

  inline StackMethod* GetMethod(const string n) {
    map<const string, StackMethod*>::iterator result = method_name_map.find(n);
    if(result != method_name_map.end()) {
      return result->second;
    }

    return NULL;
  }
};

/********************************
 * StackProgram class
 ********************************/
class StackProgram {
  // map<long, StackClass*> class_map;
  StackClass** classes;
  int class_num;
  int* cls_hierarchy;
  BYTE_VALUE** char_strings;
  int num_char_strings;
  StackMethod* init_method;
  long string_cls_id;

public:
  StackProgram() {
    cls_hierarchy = NULL;
  }

  ~StackProgram() {
    for(int i = 0; i < class_num; i++) {
      StackClass* klass = classes[i];
      delete klass;
      klass = NULL;
    }
    delete[] classes;
    classes = NULL;

    if(cls_hierarchy) {
      delete[] cls_hierarchy;
      cls_hierarchy = NULL;
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
  }

  void SetInitializationMethod(StackMethod* i) {
    init_method = i;
  }

  StackMethod* GetInitializationMethod() {
    return init_method;
  }

  void SetStringClassId(long id) {
    string_cls_id = id;
  }

  long GetStringClassId() {
    return string_cls_id;
  }

  void SetCharStrings(BYTE_VALUE** s, int n) {
    char_strings = s;
    num_char_strings = n;
  }

  BYTE_VALUE** GetCharStrings() {
    return char_strings;
  }

  void SetClasses(StackClass** clss, const int num) {
    classes = clss;
    class_num = num;
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
  StackFrame(StackMethod* md, long* m) {
    method = md;
    mem = m;
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

#endif
