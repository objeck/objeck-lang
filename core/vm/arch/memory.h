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

#define POOL_SIZE_16 8192
#define POOL_SIZE_32 8192
#define POOL_SIZE_64 4096
#define POOL_SIZE_128 2048
#define POOL_SIZE_256 2048
#define POOL_SIZE_512 2048
#define POOL_SIZE_1024 1024
#define POOL_SIZE_2048 1024
#define POOL_SIZE_4096 512
#define POOL_SIZE_8192 512
#define POOL_SIZE_16384 256
#define POOL_SIZE_32768 256
#define POOL_SIZE_65536 128
#define POOL_SIZE_131072 128
#define POOL_SIZE_262144 64
#define POOL_SIZE_524288 64
#define POOL_SIZE_1048576 64
#define POOL_SIZE_2097152 32

#define EXTRA_BUF_SIZE 4
#define MARKED_FLAG -1
#define SIZE_OR_CLS -2
#define TYPE -3
#define CACHE_SIZE -4

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
  
  static stack<char*> cache_pool_16;
  static stack<char*> cache_pool_32;
  static stack<char*> cache_pool_64;
  static stack<char*> cache_pool_128;
  static stack<char*> cache_pool_256;
  static stack<char*> cache_pool_512;
  static stack<char*> cache_pool_1024;
  static stack<char*> cache_pool_2048;
  static stack<char*> cache_pool_4096;
  static stack<char*> cache_pool_8192;
  static stack<char*> cache_pool_16384;
  static stack<char*> cache_pool_32768;
  static stack<char*> cache_pool_65536;
  static stack<char*> cache_pool_131072;
  static stack<char*> cache_pool_262144;
  static stack<char*> cache_pool_524288;
  static stack<char*> cache_pool_1048576;
  static stack<char*> cache_pool_2097152;

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

 public:
  static void Initialize(StackProgram* p);

  static void Clear() {
#ifdef _MEM_LOGGING
    mem_logger.close();
#endif

    while(!allocated_memory.empty()) {
      size_t* temp = allocated_memory.front();
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

    while(!cache_pool_128.empty()) {
      char* mem = cache_pool_128.top();
      cache_pool_128.pop();
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

    while(!cache_pool_1024.empty()) {
      char* mem = cache_pool_1024.top();
      cache_pool_1024.pop();
      free(mem);
      mem = NULL;
    }

    while(!cache_pool_2048.empty()) {
      char* mem = cache_pool_2048.top();
      cache_pool_2048.pop();
      free(mem);
      mem = NULL;
    }

    while(!cache_pool_4096.empty()) {
      char* mem = cache_pool_4096.top();
      cache_pool_4096.pop();
      free(mem);
      mem = NULL;
    }
    
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
