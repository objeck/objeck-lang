/***************************************************************************
 * VM memory manager. Implements a simple "mark and sweep" collection algorithm.
 *
 * Copyright (c) 2008-2012, Randy Hollines
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

#ifndef __MEM_MGR_H__
#define __MEM_MGR_H__

#include "../../common.h"
#include "../stx/btree_map.h"
#include "../stx/btree_set.h"
#include <process.h>

using namespace stx;

// basic vm tuning parameters
#define MEM_MAX 1024 * 512
#define UNCOLLECTED_COUNT 4
#define COLLECTED_COUNT 8

struct CollectionInfo {
  long* op_stack;
  long stack_pos;
};

struct ClassMethodId {
  long* self;
  long* mem;
  long cls_id;
  long mthd_id;
};

class MemoryManager {
  static MemoryManager* instance;
  static StackProgram* prgm;
  
  static unordered_map<long*, ClassMethodId*> jit_roots;
  static unordered_map<StackFrame*, StackFrame*> pda_roots; // deleted elsewhere
  static btree_map<long*, long> static_memory;
  static btree_map<long*, long> allocated_memory;
  static btree_set<long*> allocated_int_obj_array;
  static vector<long*> marked_memory;
  
  static CRITICAL_SECTION static_cs;
  static CRITICAL_SECTION jit_cs;
  static CRITICAL_SECTION pda_cs;
  static CRITICAL_SECTION allocated_cs;
  static CRITICAL_SECTION marked_cs;
  static CRITICAL_SECTION marked_sweep_cs;
    
  // note: protected by 'allocated_cs'
  static long allocation_size;
  static long mem_max_size;
  static long uncollected_count;
  static long collected_count;

  MemoryManager() {
  }

  // if return true, trace memory otherwise do not
  static inline bool MarkMemory(long* mem);

  static inline StackClass* GetClassMapping(long* mem) {
    #ifndef _SERIAL
    EnterCriticalSection(&allocated_cs);
#endif
    if(mem) {
      btree_map<long*, long>::iterator result = allocated_memory.find(mem);
      if(result != allocated_memory.end()) {
#ifndef _SERIAL
	      LeaveCriticalSection(&allocated_cs);
#endif
        return prgm->GetClass(-result->second);
      }
    }
#ifndef _SERIAL
    LeaveCriticalSection(&allocated_cs);
#endif
    return NULL;
  }

public:
  ~MemoryManager() {
  }

  static void Initialize(StackProgram* p);
  static MemoryManager* Instance();

  // recover memory
  static void CollectAllMemory(long* op_stack, long stack_pos);
  static uintptr_t WINAPI CollectMemory(LPVOID arg);
  static uintptr_t WINAPI CheckStatic(LPVOID arg);
  static uintptr_t WINAPI CheckStack(LPVOID arg);
  static uintptr_t WINAPI CheckJitRoots(LPVOID arg);
  static uintptr_t WINAPI CheckPdaRoots(LPVOID arg);
  
  static void Clear() {
    unordered_map<long*, ClassMethodId*>::iterator id_iter;
    for(id_iter = jit_roots.begin(); id_iter != jit_roots.end(); id_iter++) {
      ClassMethodId* tmp = id_iter->second;
      // delete
      delete tmp;
      tmp = NULL;
    }

    btree_map<long*, long>::iterator iter;
    for(iter = allocated_memory.begin(); iter != allocated_memory.end(); iter++) {
      long* temp = iter->first;

      temp -= 2;
      free(temp);
      temp = NULL;
    }
    allocated_memory.clear();

    DeleteCriticalSection(&jit_cs);
    DeleteCriticalSection(&pda_cs);
    DeleteCriticalSection(&allocated_cs);
    DeleteCriticalSection(&marked_cs);
    DeleteCriticalSection(&marked_sweep_cs);

    delete instance;
    instance = NULL;
  }

  // add and remove jit roots
  static void AddJitMethodRoot(long cls_id, long mthd_id, long* self, long* mem, long offset);
  static void RemoveJitMethodRoot(long* mem);

  // add and remove pda roots
  void AddPdaMethodRoot(StackFrame* frame);
  void RemovePdaMethodRoot(StackFrame* frame);
  
  static void CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, const long depth);
  static void CheckObject(long* mem, bool is_obj, const long depth);

  // memory allocation
  static long* AllocateObject(const char* obj_name, long* op_stack, long stack_pos, bool collect = true) {
    StackClass* cls = prgm->GetClass(obj_name);
    if(cls) {
      return AllocateObject(cls->GetId(), op_stack, stack_pos);
    }
    
    return NULL;
  }
  static long* AllocateObject(const long obj_id, long* op_stack, long stack_pos, bool collect = true);
  static long* AllocateArray(const long size, const MemoryType type, long* op_stack, long stack_pos, bool collect = true);

  // object verification
  long* ValidObjectCast(long* mem, long to_id, int* cls_hierarchy, int** cls_interfaces);
  
  // returns the class reference for an object instance
  static inline StackClass* GetClass(long* mem) {
    if(mem) {
      return (StackClass*)*(mem - 2);
    }
    return NULL;
  }

  // returns a unique object id for an instance
  static inline long GetObjectID(long* mem) {
    StackClass* klass = GetClass(mem);
    if(klass) {
      return klass->GetId();
    }
    
    return -1;
  }
};

#endif
