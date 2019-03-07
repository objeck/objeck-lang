/***************************************************************************
 * Implements a caching "mark and sweep" collector
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

#ifndef __MEM_MGR_H__
#define __MEM_MGR_H__

#include "../common.h"

// basic vm tuning parameters
#if defined(_WIN64) || defined(_X64)
#define MEM_MAX 1048576 * 2
#else
#define MEM_MAX 1048576
#endif

#define UNCOLLECTED_COUNT 11
#define COLLECTED_COUNT 29

#define EXTRA_BUF_SIZE 3
#define MARKED_FLAG -1
#define SIZE_OR_CLS -2
#define TYPE -3

struct StackOperMemory {
  size_t* op_stack;
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
  size_t* op_stack;
  long stack_pos;
};

struct ClassMethodId {
  size_t* self;
  size_t* mem;
  long cls_id;
  long mthd_id;
};

class MemoryManager {
  static bool initialized;
  static StackProgram* prgm;
  static unordered_set<StackFrameMonitor*> pda_monitors; // deleted elsewhere
  static unordered_set<StackFrame**> pda_frames;
  static vector<StackFrame*> jit_frames; // deleted elsewhere
  static vector<size_t*> allocated_memory;
  
  // TODO
  // static list<size_t*> free_memory_cache;
  static map<size_t, list<size_t*>*> free_memory_cache;

  static size_t free_memory_cache_size;
    
#ifdef _WIN32
  static CRITICAL_SECTION jit_frame_lock;
  static CRITICAL_SECTION pda_frame_lock;
  static CRITICAL_SECTION pda_monitor_lock;
  static CRITICAL_SECTION allocated_lock;
  static CRITICAL_SECTION marked_lock;
  static CRITICAL_SECTION marked_sweep_lock;
#else
  static pthread_mutex_t pda_monitor_lock;
  static pthread_mutex_t pda_frame_lock;
  static pthread_mutex_t jit_frame_lock;
  static pthread_mutex_t allocated_lock;
  static pthread_mutex_t marked_lock;
  static pthread_mutex_t marked_sweep_lock;
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
#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    if(mem && std::binary_search(allocated_memory.begin(), allocated_memory.end(), mem) && 
       mem[TYPE] == NIL_TYPE) {
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&allocated_lock);
#endif
      return (StackClass*)mem[SIZE_OR_CLS];
    }
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
#endif
    
    return NULL;
  }

  static size_t* GetMemory(size_t alloc_size);
  static void ReleaseMemory(size_t* mem);

  static void AddFreeMemory(size_t* raw_mem) {
    if(free_memory_cache_size > mem_max_size) {
      ClearFreeMemory();
    }

    const size_t size = raw_mem[0];
    if(size > 0 && size <= 8) {
      AddFreeCache(8, raw_mem);
    }
    else if(size > 8 && size <= 16) {
      AddFreeCache(16, raw_mem);
    }
    else if(size > 16 && size <= 32) {
      AddFreeCache(32, raw_mem);
    }
    else if(size > 32 && size <= 64) {
      AddFreeCache(64, raw_mem);
    }
    else if(size > 64 && size <= 128) {
      AddFreeCache(128, raw_mem);
    }
    else if(size > 128 && size <= 256) {
      AddFreeCache(256, raw_mem);
    }
    else if(size > 256 && size <= 512) {
      AddFreeCache(512, raw_mem);
    }
    else if(size > 512 && size <= 1024) {
      AddFreeCache(1024, raw_mem);
    }
    else if(size > 1024 && size <= 2048) {
      AddFreeCache(2048, raw_mem);
    }
    else if(size > 2048 && size <= 4096) {
      AddFreeCache(4096, raw_mem);
    }
    else if(size > 4096 && size <= 8192) {
      AddFreeCache(8192, raw_mem);
    }
    else if(size > 8192 && size <= 16384) {
      AddFreeCache(16384, raw_mem);
    }
    else if(size > 16384 && size <= 32768) {
      AddFreeCache(32768, raw_mem);
    }
    else if(size > 32768 && size <= 65536) {
      AddFreeCache(65536, raw_mem);
    }
    else if(size > 65536 && size <= 131072) {
      AddFreeCache(131072, raw_mem);
    }
    else if(size > 131072 && size <= 262144) {
      AddFreeCache(262144, raw_mem);
    }
    else if(size > 262144 && size <= 524288) {
      AddFreeCache(524288, raw_mem);
    }
    else if(size > 524288 && size <= 1048576) {
      AddFreeCache(1048576, raw_mem);
    }
    else if(size > 1048576 && size <= 2097152) {
      AddFreeCache(2097152, raw_mem);
    }
    else if(size > 2097152 && size <= 4194304) {
      AddFreeCache(4194304, raw_mem);
    }
    // > 4MB
    else {
      free(raw_mem);
      raw_mem = NULL;
    }
  }

  void static inline AddFreeCache(size_t pool, size_t* raw_mem) {
    const size_t mem_size = raw_mem[0];
    free_memory_cache_size += mem_size;

    map<size_t, list<size_t*>*>::iterator result = free_memory_cache.find(pool);
    if(result == free_memory_cache.end()) {
      list<size_t*>* pool_list = new list<size_t*>;
      pool_list->push_front(raw_mem);
      free_memory_cache.insert(pair<size_t, list<size_t*>*>(pool, pool_list));
    }
    else {
      result->second->push_front(raw_mem);
    }
  }
  
  static size_t* GetFreeMemory(size_t size) {
    size_t cache_size;
    if(size > 0 && size <= 8) {
      cache_size = 8;
    }
    else if(size > 8 && size <= 16) {
      cache_size = 16;
    }
    else if(size > 16 && size <= 32) {
      cache_size = 32;
    }
    else if(size > 32 && size <= 64) {
      cache_size = 64;
    }
    else if(size > 64 && size <= 128) {
      cache_size = 128;
    }
    else if(size > 128 && size <= 256) {
      cache_size = 256;
    }
    else if(size > 256 && size <= 512) {
      cache_size = 512;
    }
    else if(size > 512 && size <= 1024) {
      cache_size = 1024;
    }
    else if(size > 1024 && size <= 2048) {
      cache_size = 2048;
    }
    else if(size > 2048 && size <= 4096) {
      cache_size = 4096;
    }
    else if(size > 4096 && size <= 8192) {
      cache_size = 8192;
    }
    else if(size > 8192 && size <= 16384) {
      cache_size = 16384;
    }
    else if(size > 16384 && size <= 32768) {
      cache_size = 32768;
    }
    else if(size > 32768 && size <= 65536) {
      cache_size = 65536;
    }
    else if(size > 65536 && size <= 131072) {
      cache_size = 131072;
    }
    else if(size > 131072 && size <= 262144) {
      cache_size = 262144;
    }
    else if(size > 262144 && size <= 524288) {
      cache_size = 524288;
    }
    else if(size > 524288 && size <= 1048576) {
      cache_size = 1048576;
    }
    else if(size > 1048576 && size <= 2097152) {
      cache_size = 2097152;
    }
    else if(size > 2097152 && size <= 4194304) {
      cache_size = 4194304;
    }
    // > 4MB
    else {
      return NULL;
    }

    map<size_t, list<size_t*>*>::iterator result = free_memory_cache.find(cache_size);
    if(result != free_memory_cache.end() && !result->second->empty()) {
      bool found = false;
      list<size_t*>* free_cache = result->second;

      std::list<size_t*>::iterator iter = free_cache->begin();
      for(; !found && iter != free_cache->end(); ++iter) {
        size_t* check_mem = *iter;
        const size_t check_size = check_mem[0];
        if(check_size >= size) {
          found = true;
        }
      }

      if(found) {
        --iter;
        size_t* raw_mem = *iter;
        free_cache->erase(iter);

        const size_t mem_size = raw_mem[0];
        free_memory_cache_size -= mem_size;
        memset(raw_mem + 1, 0, mem_size);
        return raw_mem + 1;
      }
    }
    
    return NULL;
  }

  static void ClearFreeMemory(bool all = false) {
    map<size_t, list<size_t*>*>::iterator iter = free_memory_cache.begin();
    for(; iter != free_memory_cache.end(); ++iter) {
      list<size_t*>* free_cache = iter->second;

      while(!free_cache->empty()) {
        size_t* raw_mem = free_cache->front();
        free_cache->pop_front();

        const size_t size = raw_mem[0];
        free_memory_cache_size -= size;

        free(raw_mem);
        raw_mem = NULL;
      }

      if(all) {
        delete free_cache;
        free_cache = NULL;

        free_memory_cache.clear();
      }
    }
  }
  
 public:
  static void Initialize(StackProgram* p);

  static void Clear() {
#ifdef _MEM_LOGGING
    mem_logger.close();
#endif

    ClearFreeMemory(true);

    while(!allocated_memory.empty()) {
      size_t* temp = allocated_memory.front();
      allocated_memory.erase(allocated_memory.begin());      
      temp -= EXTRA_BUF_SIZE;
      free(temp);
      temp = NULL;
    }
    allocated_memory.clear();
    
#ifdef _WIN32
    DeleteCriticalSection(&jit_frame_lock);
    DeleteCriticalSection(&pda_monitor_lock);
    DeleteCriticalSection(&allocated_lock);
    DeleteCriticalSection(&marked_lock);
    DeleteCriticalSection(&marked_sweep_lock);
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
    
    return NULL;
  }
  
  static size_t* AllocateObject(const long obj_id, size_t* op_stack, long stack_pos, bool collect = true);
  static size_t* AllocateArray(const long size, const MemoryType type, size_t* op_stack, long stack_pos, bool collect = true);
  
  // object verification
  static size_t* ValidObjectCast(size_t* mem, long to_id, int* cls_hierarchy, int** cls_interfaces);
  
  //
  // returns the class reference for an object instance
  //
  static inline StackClass* GetClass(size_t* mem) {
    if(mem && mem[TYPE] == NIL_TYPE) {
      return (StackClass*)mem[SIZE_OR_CLS];
    }
    return NULL;
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
};

#endif
