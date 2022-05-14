/***************************************************************************
 * Implements a caching "mark and sweep" collector
 *
 * Copyright (c) 2008-2022, Randy Hollines
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

#include "../common.h"

/*
Start: GC tuning parameters
*/

// FOR DEBUGGING ONLY
// #define MEM_MAX 512

// FOR PRODUCTION
#define MEM_MAX 512 * 4096

#define UNCOLLECTED_COUNT 13
#define COLLECTED_COUNT 29

#define EXTRA_BUF_SIZE 3
#define MARKED_FLAG -1
#define SIZE_OR_CLS -2
#define TYPE -3

/*
End: GC tuning parameters
*/

struct StackOperMemory {
  size_t* op_stack;
  long* stack_pos;
};

// used to monitor the state of active stack frames
struct StackFrameMonitor {
  StackFrame** call_stack;
  long* call_stack_pos;
  StackFrame** cur_frame;
};

// holders
struct CollectionInfo {
  size_t* op_stack;
  long stack_pos;
};

struct ClassMethodId {
  size_t* self;
  size_t* mem;
  long cls_id;
  long mthd_id;
};

using cantor_tuple_key = tuple<StackClass*, size_t, size_t>;

class MemoryManager {
  static bool initialized;
  static StackProgram* prgm;
  static unordered_set<StackFrameMonitor*> pda_monitors; // deleted elsewhere
  static unordered_set<StackFrame**> pda_frames;
  static vector<StackFrame*> jit_frames; // deleted elsewhere
  static set<size_t*> allocated_memory;
  static unordered_map<size_t, stack<size_t*>*> free_memory_cache;
  static size_t free_memory_cache_size;
  
  struct cantor_tuple {
      template <class T1, class T2, class T3>

      size_t operator () (const tuple<T1,T2,T3> &t) const {
        const size_t t1 = (size_t)get<0>(t);
        const size_t t2 = get<1>(t);
        const size_t t3 = get<2>(t);

        const size_t p1 = (t2 + t3) * (t2 + t3 + 1) / 2 + t3;
        return (t1 + p1) * (t1 + p1 + 1) / 2 + p1;
      }
  };
  static unordered_map<cantor_tuple_key, StackMethod*, cantor_tuple> virtual_method_table;
  
#ifdef _WIN32
  static CRITICAL_SECTION jit_frame_lock;
  static CRITICAL_SECTION pda_frame_lock;
  static CRITICAL_SECTION pda_monitor_lock;
  static CRITICAL_SECTION allocated_lock;
  static CRITICAL_SECTION marked_lock;
  static CRITICAL_SECTION marked_sweep_lock;
  static CRITICAL_SECTION free_memory_cache_lock;
  static CRITICAL_SECTION virtual_method_lock;
#else
  static pthread_mutex_t pda_monitor_lock;
  static pthread_mutex_t pda_frame_lock;
  static pthread_mutex_t jit_frame_lock;
  static pthread_mutex_t allocated_lock;
  static pthread_mutex_t marked_lock;
  static pthread_mutex_t marked_sweep_lock;
	static pthread_mutex_t free_memory_cache_lock;
	static pthread_mutex_t virtual_method_lock;
#endif
    
  // note: protected by 'allocated_lock'
  static size_t allocation_size;
  static size_t mem_max_size;
  static size_t uncollected_count;
  static size_t collected_count;

  // if return true, trace memory otherwise do not
  static inline bool MarkMemory(size_t* mem);
  static inline bool MarkValidMemory(size_t* mem);

#ifdef _MEM_LOGGING
  static ofstream mem_logger;
  static long mem_cycle;
#endif
  
#ifdef _WIN32
  // mark memory
  static unsigned int WINAPI CheckStatic(LPVOID arg);
  static unsigned int WINAPI CheckStack(LPVOID arg);
  static unsigned int WINAPI CheckPdaRoots(LPVOID arg);
  static unsigned int WINAPI CheckJitRoots(LPVOID arg);
  
  // recover memory
  static void CollectAllMemory(size_t* op_stack, long stack_pos);
  static unsigned int WINAPI CollectMemory(LPVOID arg);
#else  
  // mark memory
  static void* CheckStatic(void* arg);
  static void* CheckStack(void* arg);
  static void* CheckPdaRoots(void* arg);
  static void* CheckJitRoots(void* arg);
  // recover memory
  static void CollectAllMemory(size_t* op_stack, long stack_pos);
  static void* CollectMemory(void* arg);
#endif

  static inline StackClass* GetClassMapping(size_t* mem) {
    if(!mem) {
      return nullptr;
    }

#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    set<size_t*>::iterator found = allocated_memory.find(mem);
    if(found != allocated_memory.end() && mem[TYPE] == NIL_TYPE) {
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&allocated_lock);
#endif
      return (StackClass*)mem[SIZE_OR_CLS];
    }
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
#endif
    
    return nullptr;
  }

  static size_t* GetMemory(size_t alloc_size);
  static void AddFreeMemory(size_t* raw_mem);
  static size_t GetAllocSize(size_t size);
  void static inline AddFreeCache(size_t pool, size_t* raw_mem);
  static size_t* GetFreeMemory(size_t cache_size);
  static size_t GetAlignedSize(size_t size);
  static void ClearFreeMemory(bool all = false);
  
 public:
  static void Initialize(StackProgram* p);

  static void Clear() {
#ifdef _MEM_LOGGING
    mem_logger.close();
#endif

    ClearFreeMemory(true);

    for(set<size_t*>::iterator iter = allocated_memory.begin(); iter != allocated_memory.end(); ++iter) {
      size_t* mem = *iter;
      mem -= EXTRA_BUF_SIZE + 1;
      free(mem);
      mem = nullptr;
    }
    allocated_memory.clear();

#ifdef _WIN32
    DeleteCriticalSection(&jit_frame_lock);
    DeleteCriticalSection(&pda_monitor_lock);
    DeleteCriticalSection(&allocated_lock);
    DeleteCriticalSection(&marked_lock);
    DeleteCriticalSection(&marked_sweep_lock);
    DeleteCriticalSection(&free_memory_cache_lock);
    DeleteCriticalSection(&virtual_method_lock);
#endif
      
    initialized = false;
  }
  
  // add and remove pda roots
  static void AddPdaMethodRoot(StackFrame** frame);
  static void RemovePdaMethodRoot(StackFrame** frame);
  static void AddPdaMethodRoot(StackFrameMonitor* monitor);  
  static void RemovePdaMethodRoot(StackFrameMonitor* monitor);
  
  static void CheckMemory(size_t* mem, StackDclr** dclrs, const long dcls_size, const long depth);
  static void CheckObject(size_t* mem, bool is_obj, const long depth);
  
  static size_t* AllocateObject(const wchar_t* obj_name, size_t* op_stack, long stack_pos, bool collect = true) {
    StackClass* cls = prgm->GetClass(obj_name);
    if(cls) {
      return AllocateObject(cls->GetId(), op_stack, stack_pos, collect);
    }
    
    return nullptr;
  }
  
  static size_t* AllocateObject(const long obj_id, size_t* op_stack, long stack_pos, bool collect = true);
  static size_t* AllocateArray(const long size, const MemoryType type, size_t* op_stack, long stack_pos, bool collect = true);
  
  // object verification
  static size_t* ValidObjectCast(size_t* mem, long to_id, long* cls_hierarchy, long** cls_interfaces);
  
  //
  // returns the class reference for an object instance
  //
  static inline StackClass* GetClass(size_t* mem) {
    if(mem && mem[TYPE] == NIL_TYPE) {
      return (StackClass*)mem[SIZE_OR_CLS];
    }
    return nullptr;
  }

  //
  // returns a unique object id for an instance
  //
  static inline long GetObjectID(size_t* mem) {
    StackClass* klass = GetClass(mem);
    if(klass) {
      return klass->GetId();
    }
    
    return -1;
  }

	static StackMethod* GetVirtualEntry(StackClass* concrete_cls, size_t virtual_cls_id, size_t virtual_mthd_id);
	static void AddVirtualEntry(StackClass* concrete_cls, size_t virtual_cls_id, size_t virtual_mthd_id, StackMethod* mthd);

#ifdef _DEBUGGER
  static size_t GetAllocationSize() {
    return allocation_size;
  }

  static size_t GetMaxMemory() {
    return mem_max_size;
  }
#endif
};

#endif
