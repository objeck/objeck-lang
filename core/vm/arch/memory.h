/***************************************************************************
 * Implements a caching "mark and sweep" collector
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


#include "../common.h"
#include <random>
#include <atomic>
#ifndef _WIN32
#include <sys/mman.h>
#endif

// basic VM tuning parameters

/* FOR DEBUGGING ONLY
#define MEM_MAX 4096 * 2
*/

#define MEM_START_MAX 4096 * 256

#define UNCOLLECTED_COUNT 7
#define COLLECTED_COUNT 23

#define EXTRA_BUF_SIZE 3
#define MARKED_FLAG -1
#define SIZE_OR_CLS -2
#define TYPE -3

#define JIT_TMP_LOOK_BACK 16

#define ALIGN_POOL_MAX (1 << 22)

// Generational GC: bit-packing in MARKED_FLAG slot
// Bit 0:    Mark bit (existing)
// Bit 1:    Old generation flag (0=young, 1=old)
// Bit 2:    Remembered-set flag (0=not in rset, 1=in rset)
#define GC_MARK_BIT    0x1ULL
#define GC_OLD_BIT     0x2ULL
#define GC_RSET_BIT    0x4ULL

// Young generation bump allocator sizing
#define YOUNG_REGION_SIZE  (32 * 1024 * 1024)  // 32MB young region
#define DIRTY_LIST_MAX     65536               // max dirty old-gen objects tracked

struct StackOperMemory {
  size_t* op_stack;
  size_t* stack_pos;
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
  size_t stack_pos;
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
  static std::unordered_set<StackFrameMonitor*> pda_monitors; // deleted elsewhere
  static std::unordered_set<StackFrame**> pda_frames;
  static std::vector<StackFrame*> jit_frames; // deleted elsewhere
  static std::unordered_map<size_t, std::list<size_t*>*> free_memory_cache;
  static size_t free_memory_cache_size;

  // Young generation: contiguous bump-allocated region
  static uint8_t* young_region;
  static size_t young_region_size;
  static std::atomic<size_t> young_offset;

  // Old generation: individually allocated, tracked in set
  static std::unordered_set<size_t*> old_generation;
  static size_t old_allocation_size;

  // Lock-free dirty list (replaces remembered_set + remembered_set_lock)
  static size_t* dirty_list[DIRTY_LIST_MAX];
  static std::atomic<size_t> dirty_count;

  static std::atomic<bool> minor_gc_mode;
  
#ifdef _WIN32
  static CRITICAL_SECTION jit_frame_lock;
  static CRITICAL_SECTION pda_frame_lock;
  static CRITICAL_SECTION pda_monitor_lock;
  static CRITICAL_SECTION allocated_lock;
  static CRITICAL_SECTION marked_lock;
  static CRITICAL_SECTION marked_sweep_lock;
  static CRITICAL_SECTION free_memory_cache_lock;
#else
  static pthread_mutex_t pda_monitor_lock;
  static pthread_mutex_t pda_frame_lock;
  static pthread_mutex_t jit_frame_lock;
  static pthread_mutex_t allocated_lock;
  static pthread_mutex_t marked_lock;
  static pthread_mutex_t marked_sweep_lock;
  static pthread_mutex_t free_memory_cache_lock;
#endif
    
  // note: protected by 'allocated_lock'
  static size_t allocation_size;
  static size_t mem_max_size;
  static size_t uncollected_count;
  static size_t collected_count;

  // if return true, trace memory otherwise do not
  static inline bool MarkMemory(size_t* mem);

  // Generational GC helpers
  static inline bool IsYoung(size_t* mem) {
    uint8_t* p = (uint8_t*)mem;
    return p >= young_region && p < young_region + young_offset.load(std::memory_order_relaxed);
  }

  static inline bool IsAllocated(size_t* mem) {
    return IsYoung(mem) || old_generation.count(mem);
  }

  static inline bool IsOldGen(size_t* mem) {
    return mem && (mem[MARKED_FLAG] & GC_OLD_BIT);
  }

  static void CollectMinor(size_t* op_stack, size_t stack_pos);
  static void CollectMajor(size_t* op_stack, size_t stack_pos);
  static void ScanDirtyObject(size_t* mem);
  static void FixupObject(size_t* mem);
  static void FixupMemory(size_t* mem, StackDclr** dclrs, const long dcls_size);
  static void FixupRoots(size_t* op_stack, size_t stack_pos);

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
  static void CollectAllMemory(size_t* op_stack, size_t stack_pos);
  static unsigned int WINAPI CollectMemory(LPVOID arg);
#else  
  // mark memory
  static void* CheckStatic(void* arg);
  static void* CheckStack(void* arg);
  static void* CheckPdaRoots(void* arg);
  static void* CheckJitRoots(void* arg);
  // recover memory
  static void CollectAllMemory(size_t* op_stack, size_t stack_pos);
  static void* CollectMemory(void* arg);
#endif
    
  static inline StackClass* GetClassMapping(size_t* mem) {
    if(!mem) {
      return nullptr;
    }

#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    const bool found = IsAllocated(mem);
    if(found && mem[TYPE] == instructions::MemoryType::NIL_TYPE) {
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
  void static inline AddFreeCache(size_t pool, size_t* raw_mem);
  static size_t* GetFreeMemory(size_t size);
  static size_t AlignMemorySize(size_t size);
  static void ClearFreeMemory(bool all = false);
  
 public:
  static void Initialize(StackProgram* p, size_t m);

  static FLOAT_VALUE GetRandomValue();
  
  static void Clear() {
#ifdef _MEM_LOGGING
    mem_logger.close();
#endif

    if(!free_memory_cache.empty()) {
      ClearFreeMemory(true);
      free_memory_cache.clear();
    }

    // Free old generation (individually allocated)
    for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
      size_t* mem = *iter;
      mem -= EXTRA_BUF_SIZE + 1;
      free(mem);
    }
    old_generation.clear();

    // Free young region (single contiguous block)
    if(young_region) {
#ifdef _WIN32
      VirtualFree(young_region, 0, MEM_RELEASE);
#else
      munmap(young_region, young_region_size);
#endif
      young_region = nullptr;
    }
    young_offset.store(0, std::memory_order_relaxed);
    dirty_count.store(0, std::memory_order_relaxed);

#ifdef _WIN32
    DeleteCriticalSection(&jit_frame_lock);
    DeleteCriticalSection(&pda_monitor_lock);
    DeleteCriticalSection(&allocated_lock);
    DeleteCriticalSection(&marked_lock);
    DeleteCriticalSection(&marked_sweep_lock);
    DeleteCriticalSection(&free_memory_cache_lock);
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
  
  static size_t* AllocateObject(const wchar_t* obj_name, size_t* op_stack, size_t stack_pos, bool collect = true) {
    StackClass* cls = prgm->GetClass(obj_name);
    if(cls) {
      return AllocateObject(cls->GetId(), op_stack, stack_pos, collect);
    }
    
    return nullptr;
  }
  
  static size_t* AllocateObject(const long obj_id, size_t* op_stack, size_t stack_pos, bool collect = true);
  static size_t* AllocateArray(const size_t size, const MemoryType type, size_t* op_stack, size_t stack_pos, bool collect = true);

  // Generational GC write barrier: lock-free dirty list append
  static inline void WriteBarrier(size_t* target_obj) {
    if(!target_obj) return;
    if(!(target_obj[MARKED_FLAG] & GC_OLD_BIT)) return;      // young target, skip
    if(target_obj[MARKED_FLAG] & GC_RSET_BIT) return;         // already tracked
    target_obj[MARKED_FLAG] |= GC_RSET_BIT;
    size_t idx = dirty_count.fetch_add(1, std::memory_order_relaxed);
    if(idx < DIRTY_LIST_MAX) {
      dirty_list[idx] = target_obj;
    }
    // If overflow: next minor GC falls back to full old-gen scan
  }
  
  // object verification
  static size_t* ValidObjectCast(size_t* mem, long to_id, long* cls_hierarchy, long** cls_interfaces);
  
  //
  // returns the class reference for an object instance
  //
  static inline StackClass* GetClass(size_t* mem) {
    if(mem && mem[TYPE] == instructions::MemoryType::NIL_TYPE) {
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

#ifdef _DEBUGGER
  static size_t GetAllocationSize() {
    return allocation_size;
  }

  static size_t GetMaxMemory() {
    return mem_max_size;
  }
#endif
};
