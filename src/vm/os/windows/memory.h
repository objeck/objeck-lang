/***************************************************************************
 * VM memory manager. Implements a simple "mark and sweep" collection algorithm.
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

#ifndef __MEM_MGR_H__
#define __MEM_MGR_H__

#include "../../common.h"

// initial threshold
// #define MEM_MAX 1024
#define MEM_MAX 1048576 * 2
// when this limit is met the garbage collect threshold
// is increased
#define UNCOLLECTED_COUNT 4
// when this limit is met the garbage collect threshold
// is lowered
#define COLLECTED_COUNT 8
#define POOL_SIZE 1024

// memory meta data tags
#define EXTRA_BUF_SIZE 4
#define MARKED_FLAG -1
#define SIZE_OR_CLS -2
#define TYPE -3
#define CACHE_SIZE -4

struct StackOperMemory {
  long* op_stack;
  long* stack_pos;
};

// used to monitor the state of
// active stack frames
struct StackFrameMonitor {
  StackFrame** call_stack;
  long* call_stack_pos;
  StackFrame** cur_frame;
};

// holders
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
  static bool initialized;
  static StackProgram* prgm;
  
  // reference containers
  static unordered_map<long*, ClassMethodId*> jit_roots;
  static set<StackFrame**> pda_frames;
  static unordered_map<StackFrameMonitor*, StackFrameMonitor*> pda_monitors; // deleted elsewhere
  static vector<long*> allocated_memory;
  static vector<long*> marked_memory;

  // memory caches
  static stack<char*> cache_pool_16;
  static stack<char*> cache_pool_32;
  static stack<char*> cache_pool_64;
  static stack<char*> cache_pool_256;
  static stack<char*> cache_pool_512;
  
  static CRITICAL_SECTION jit_cs;
  static CRITICAL_SECTION pda_frame_cs;
  static CRITICAL_SECTION pda_monitor_cs;
  static CRITICAL_SECTION allocated_cs;
  static CRITICAL_SECTION marked_cs;
  static CRITICAL_SECTION marked_sweep_cs;
  
  // note: protected by 'allocated_cs'
  static long allocation_size;
  static long mem_max_size;
  static long uncollected_count;
  static long collected_count;

  // if return true, trace memory otherwise do not
  static inline bool MarkMemory(long* mem);
  static inline bool MarkValidMemory(long* mem);

  static inline StackClass* GetClassMapping(long* mem) {
#ifndef _GC_SERIAL
      EnterCriticalSection(&allocated_cs);
#endif
    if(mem && std::binary_search(allocated_memory.begin(), allocated_memory.end(), mem) && 
       mem[TYPE] == NIL_TYPE) {
#ifndef _GC_SERIAL
      LeaveCriticalSection(&allocated_cs);
#endif
      return (StackClass*)mem[SIZE_OR_CLS];
    }
#ifndef _GC_SERIAL
    LeaveCriticalSection(&allocated_cs);
#endif
    
    return NULL;
  }

public:
  // initialize the runtime system
  static void Initialize(StackProgram* p);

  // free static resources
  static void Clear() {
    unordered_map<long*, ClassMethodId*>::iterator id_iter;
    for(id_iter = jit_roots.begin(); id_iter != jit_roots.end(); ++id_iter) {
      ClassMethodId* tmp = id_iter->second;
      // delete
      delete tmp;
      tmp = NULL;
    }

    while(!allocated_memory.empty()) {
      long* temp = allocated_memory.front();
      allocated_memory.erase(allocated_memory.begin());      
      temp -= EXTRA_BUF_SIZE;
      free(temp);
      temp = NULL;
    }
    allocated_memory.clear();

    while(!cache_pool_16.empty()) {
      char* mem = cache_pool_16.top();
      cache_pool_16.pop();
      free(mem);
      mem = NULL;
    }

    while(!cache_pool_32.empty()) {
      char* mem = cache_pool_32.top();
      cache_pool_32.pop();
      free(mem);
      mem = NULL;
    }

    while(!cache_pool_64.empty()) {
      char* mem = cache_pool_64.top();
      cache_pool_64.pop();
      free(mem);
      mem = NULL;
    }

    while(!cache_pool_256.empty()) {
      char* mem = cache_pool_256.top();
      cache_pool_256.pop();
      free(mem);
      mem = NULL;
    }

    while(!cache_pool_512.empty()) {
      char* mem = cache_pool_512.top();
      cache_pool_512.pop();
      free(mem);
      mem = NULL;
    }
    
    DeleteCriticalSection(&jit_cs);
    DeleteCriticalSection(&pda_monitor_cs);
    DeleteCriticalSection(&allocated_cs);
    DeleteCriticalSection(&marked_cs);
    DeleteCriticalSection(&marked_sweep_cs);

    initialized = false;
  }

  // mark and sweep functions
  static void CollectAllMemory(long* op_stack, long stack_pos);
  static uintptr_t WINAPI CollectMemory(LPVOID arg);
  static uintptr_t WINAPI CheckStatic(LPVOID arg);
  static uintptr_t WINAPI CheckStack(LPVOID arg);
  static uintptr_t WINAPI CheckJitRoots(LPVOID arg);
  static uintptr_t WINAPI CheckPdaRoots(LPVOID arg);
  
  // add and remove jit roots
  static void AddJitMethodRoot(long cls_id, long mthd_id, long* self, long* mem, long offset);
  static void RemoveJitMethodRoot(long* mem);

  // add and remove pda roots
  static void AddPdaMethodRoot(StackFrame** frame);
  static void RemovePdaMethodRoot(StackFrame** frame);
  static void AddPdaMethodRoot(StackFrameMonitor* monitor);
  static void RemovePdaMethodRoot(StackFrameMonitor* monitor);
  
  // sweeps memory
  static void CheckMemory(long* mem, StackDclr** dclrs, const long dcls_size, const long depth);

  // sweeps an object and it's references
  static void CheckObject(long* mem, bool is_obj, const long depth);

  // memory allocation
  static long* AllocateObject(const wchar_t* obj_name, long* op_stack, long stack_pos, bool collect = true) {
    StackClass* cls = prgm->GetClass(obj_name);
    if(cls) {
      return AllocateObject(cls->GetId(), op_stack, stack_pos, collect);
    }

    return NULL;
  }
  
  //
  // object and array allocation
  //
  static long* AllocateObject(const long obj_id, long* op_stack, long stack_pos, bool collect = true);
  static long* AllocateArray(const long size, const MemoryType type, long* op_stack, long stack_pos, bool collect = true);

  //
  // object verification
  //
  static long* ValidObjectCast(long* mem, long to_id, int* cls_hierarchy, int** cls_interfaces);
  
  //
  // returns the class reference for an object instance
  //
  static inline StackClass* GetClass(long* mem) {
    if(mem && mem[TYPE] == NIL_TYPE) {
      return (StackClass*)mem[SIZE_OR_CLS];
    }
    
    return NULL;
  }

  //
  // returns a unique object id for an instance
  //
  static inline long GetObjectID(long* mem) {
    StackClass* klass = GetClass(mem);
    if(klass) {
      return klass->GetId();
    }
    
    return -1;
  }
};

#endif
