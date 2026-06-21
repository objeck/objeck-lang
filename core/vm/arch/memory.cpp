/***************************************************************************
* VM memory manager. Implements a "mark and sweep" collection algorithm.
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

#include "memory.h"
#include <iomanip>

StackProgram* MemoryManager::prgm;

std::unordered_set<StackFrame**> MemoryManager::pda_frames;
std::unordered_set<StackFrameMonitor*> MemoryManager::pda_monitors;
std::vector<StackFrame*> MemoryManager::jit_frames;

size_t* MemoryManager::free_buckets[MemoryManager::FREE_POOL_COUNT];
size_t MemoryManager::free_memory_cache_size;

bool MemoryManager::initialized;
size_t MemoryManager::allocation_size;
size_t MemoryManager::mem_max_size;
size_t MemoryManager::uncollected_count;
size_t MemoryManager::collected_count;

// Young generation bump allocator
uint8_t* MemoryManager::young_region;
size_t MemoryManager::young_region_size;
std::atomic<size_t> MemoryManager::young_offset;

// Old generation
std::unordered_set<size_t*> MemoryManager::old_generation;
size_t MemoryManager::old_allocation_size;

// Lock-free dirty list
size_t* MemoryManager::dirty_list[DIRTY_LIST_MAX];
std::atomic<size_t> MemoryManager::dirty_count;

std::atomic<bool> MemoryManager::minor_gc_mode;
std::atomic<long> MemoryManager::mutator_count(1);   // main thread is mutator #1
std::atomic<long> MemoryManager::parked_count(0);
std::atomic<bool> MemoryManager::stw_active(false);
#ifdef _WIN32
CRITICAL_SECTION MemoryManager::stw_lock;
CONDITION_VARIABLE MemoryManager::stw_cv;
#else
pthread_mutex_t MemoryManager::stw_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t MemoryManager::stw_cv = PTHREAD_COND_INITIALIZER;
#endif

#ifdef _MEM_LOGGING
ofstream MemoryManager::mem_logger;
long MemoryManager::mem_cycle = 0L;
#endif

// operation locks (marked_lock removed — MarkMemory now uses lock-free CAS)
#ifdef _WIN32
CRITICAL_SECTION MemoryManager::jit_frame_lock;
CRITICAL_SECTION MemoryManager::pda_frame_lock;
CRITICAL_SECTION MemoryManager::pda_monitor_lock;
CRITICAL_SECTION MemoryManager::allocated_lock;
CRITICAL_SECTION MemoryManager::marked_sweep_lock;
CRITICAL_SECTION MemoryManager::free_memory_cache_lock;
#else
pthread_mutex_t MemoryManager::pda_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::pda_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::jit_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::allocated_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::marked_sweep_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::free_memory_cache_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

void MemoryManager::Initialize(StackProgram* p, size_t m)
{
  prgm = p;
  if(m <= 0) {
    mem_max_size = MEM_START_MAX;
  }
  else {
    mem_max_size = m;
  }
  allocation_size = 0;
  uncollected_count = 0;
  free_memory_cache_size = 0;
  memset(free_buckets, 0, sizeof(free_buckets));

  // Young generation bump allocator
  young_region_size = YOUNG_REGION_SIZE;
#ifdef _WIN32
  young_region = (uint8_t*)VirtualAlloc(nullptr, young_region_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  young_region = (uint8_t*)mmap(nullptr, young_region_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(young_region == MAP_FAILED) {
    young_region = nullptr;
  }
#endif
  if(!young_region) {
    std::wcerr << L">>> Failed to allocate young generation region (" << young_region_size << L" bytes) <<<" << std::endl;
    exit(1);
  }
  young_offset.store(0, std::memory_order_relaxed);

  // Old generation — reserve larger initial capacity to reduce rehashing
  // for workloads that allocate many objects (e.g. binarytrees creates millions)
  old_allocation_size = 0;
  old_generation.reserve(65536);

  // Dirty list
  memset(dirty_list, 0, sizeof(dirty_list));
  dirty_count.store(0, std::memory_order_relaxed);

  minor_gc_mode.store(false, std::memory_order_relaxed);

#ifdef _MEM_LOGGING
  mem_logger.open("mem_log.csv");
  mem_logger << L"cycle,oper,type,addr,size" << std::endl;
#endif

#ifdef _WIN32
  InitializeCriticalSection(&jit_frame_lock);
  InitializeCriticalSection(&pda_frame_lock);
  InitializeCriticalSection(&pda_monitor_lock);
  InitializeCriticalSection(&allocated_lock);
  InitializeCriticalSection(&marked_sweep_lock);
  InitializeCriticalSection(&free_memory_cache_lock);
  InitializeCriticalSection(&stw_lock);
  InitializeConditionVariable(&stw_cv);
#endif

  mutator_count.store(1, std::memory_order_relaxed);   // main thread
  parked_count.store(0, std::memory_order_relaxed);
  stw_active.store(false, std::memory_order_relaxed);

  initialized = true;
}

// --- Cooperative stop-the-world ----------------------------------------------

void MemoryManager::RegisterMutator()
{
  mutator_count.fetch_add(1, std::memory_order_acq_rel);
}

void MemoryManager::UnregisterMutator()
{
  // If a collection is waiting for this thread to park, leaving counts as parking.
  mutator_count.fetch_sub(1, std::memory_order_acq_rel);
  MUTEX_LOCK(&stw_lock);
  WAKE_ALL_CONDITION(&stw_cv);   // collector may now have all-but-self parked
  MUTEX_UNLOCK(&stw_lock);
}

// Poll point: if a collection is in progress, park here (a clean point where all
// of this thread's live roots are on its op-stack/frames) until it completes.
void MemoryManager::SafePoint()
{
  if(!stw_active.load(std::memory_order_acquire)) {
    return;
  }
  MUTEX_LOCK(&stw_lock);
  parked_count.fetch_add(1, std::memory_order_acq_rel);
  WAKE_ALL_CONDITION(&stw_cv);   // tell the collector we've parked
  while(stw_active.load(std::memory_order_acquire)) {
    SLEEP_CONDITION(&stw_cv, &stw_lock);
  }
  parked_count.fetch_sub(1, std::memory_order_acq_rel);
  MUTEX_UNLOCK(&stw_lock);
}

// A thread about to block in a syscall (sleep / join / I/O) freezes its VM state,
// which is then stable and scannable — so count it as parked for the duration.
void MemoryManager::BeginBlocking()
{
  MUTEX_LOCK(&stw_lock);
  parked_count.fetch_add(1, std::memory_order_acq_rel);
  WAKE_ALL_CONDITION(&stw_cv);
  MUTEX_UNLOCK(&stw_lock);
}

void MemoryManager::EndBlocking()
{
  MUTEX_LOCK(&stw_lock);
  // If a collection is in progress, stay parked (still COUNTED) until it
  // finishes, THEN decrement — like SafePoint. Decrementing first would leave
  // this thread parked-but-uncounted and the collector would wait forever for a
  // count it can never reach (the deadlock). Holding stw_lock across the wait
  // and the decrement means no new collection can start in the gap (the
  // collector takes stw_lock to set stw_active).
  while(stw_active.load(std::memory_order_acquire)) {
    SLEEP_CONDITION(&stw_cv, &stw_lock);
  }
  parked_count.fetch_sub(1, std::memory_order_acq_rel);
  MUTEX_UNLOCK(&stw_lock);
}

FLOAT_VALUE MemoryManager::GetRandomValue() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<FLOAT_VALUE> dis(0.0, 1.0);

  return dis(gen);
}

// if return true, trace memory otherwise do not
// Generational: mark bit is bit 0 of MARKED_FLAG, preserving gen/age/rset bits
inline bool MemoryManager::MarkMemory(size_t* mem)
{
  if(mem) {
    // check if memory has been marked (bit 0)
    if(mem[MARKED_FLAG] & GC_MARK_BIT) {
      return false;
    }

    // mark using lock-free CAS (avoids mutex contention across 3 mark threads)
#ifndef _GC_SERIAL
    size_t old_val, new_val;
    do {
      old_val = mem[MARKED_FLAG];
      if(old_val & GC_MARK_BIT) return false;
      new_val = old_val | GC_MARK_BIT;
#ifdef _WIN32
    } while(InterlockedCompareExchange64((volatile LONG64*)&mem[MARKED_FLAG], (LONG64)new_val, (LONG64)old_val) != (LONG64)old_val);
#else
    } while(!__sync_bool_compare_and_swap(&mem[MARKED_FLAG], old_val, new_val));
#endif
#else
    mem[MARKED_FLAG] |= GC_MARK_BIT;
#endif

    return true;
  }

  return false;
}

void MemoryManager::AddPdaMethodRoot(StackFrame** frame)
{
  if(!initialized) {
    return;
  }

#ifdef _DEBUG_GC
  std::wcout << L"adding PDA frame: addr=" << frame << std::endl;
#endif

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_frame_lock);
#endif
  pda_frames.insert(frame);
  
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_frame_lock);
#endif
}

void MemoryManager::RemovePdaMethodRoot(StackFrame** frame)
{
#ifdef _DEBUG_GC
  std::wcout << L"removing PDA frame: addr=" << frame << std::endl;
#endif
  
#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_frame_lock);
#endif
  pda_frames.erase(frame);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_frame_lock);
#endif
}

void MemoryManager::AddPdaMethodRoot(StackFrameMonitor* monitor)
{
#ifdef _DEBUG_GC
  std::wcout << L"adding PDA method: monitor=" << monitor << std::endl;
#endif

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_monitor_lock);
#endif
  pda_monitors.insert(monitor);
  
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_monitor_lock);
#endif
}

void MemoryManager::RemovePdaMethodRoot(StackFrameMonitor* monitor)
{
  if(!initialized) {
    return;
  }

#ifdef _DEBUG_GC
  std::wcout << L"removing PDA method: monitor=" << monitor << std::endl;
#endif

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_monitor_lock);
#endif
  pda_monitors.erase(monitor);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_monitor_lock);
#endif
}

size_t* MemoryManager::AllocateObject(const long obj_id, size_t* op_stack, size_t stack_pos, bool collect)
{
  // Allocation is a safepoint: park here if another thread is collecting. Reached
  // from JITed code too (it calls back to allocate), so allocation-heavy threads
  // park promptly even without interpreter dispatch polling.
  if(collect) { SafePoint(); }
  StackClass* cls = prgm->GetClass(obj_id);
#ifdef _DEBUG_GC
  assert(cls);
#endif

  size_t* mem = nullptr;
  if(cls) {
    const long size = cls->GetInstanceMemorySize();
    const size_t alloc_size = size * 2 + sizeof(size_t) * EXTRA_BUF_SIZE;

    // Total size including the size header for free cache
    const size_t total_size = alloc_size + sizeof(size_t);
    // Align to sizeof(size_t) boundary for bump allocator
    const size_t aligned_total = (total_size + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);

    // Try young generation bump allocation first. Advance young_offset with a CAS
    // that commits ONLY when the block fits: a plain fetch_add over-advances under
    // concurrency (many threads add past the end without rolling back), leaving
    // young_offset > young_region_size. The collector then uses young_offset as the
    // length of its young walk + reset memset, running off the end of the mapping —
    // promoting garbage as objects (corruption) and overrunning memset (crash).
    if(young_region) {
      size_t offset = young_offset.load(std::memory_order_relaxed);
      while(offset + aligned_total <= young_region_size) {
        if(young_offset.compare_exchange_weak(offset, offset + aligned_total,
                                              std::memory_order_relaxed)) {
          // Bump allocation succeeded
          size_t* raw_mem = (size_t*)(young_region + offset);
          raw_mem[0] = alloc_size;  // store size for promotion
          mem = raw_mem + 1;
          mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
          mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
          mem += EXTRA_BUF_SIZE;
          // Young object: no GC_OLD_BIT, no hash set insert, no mutex.
          // Explicitly clear the GC flag word — do NOT rely on the region being
          // pre-zeroed (the post-GC reset memset only covers the prior high-water mark).
          mem[MARKED_FLAG] = 0;
          // release fence: header stores must be visible before this object can be
          // observed as young (pairs with the acquire load of young_offset in IsYoung).
          std::atomic_thread_fence(std::memory_order_release);
          allocation_size += size;
          return mem;
        }
        // CAS failed: offset was reloaded with the current value — re-test and retry
      }
      // Young gen full — trigger GC to promote survivors and reset
      if(collect) {
        CollectMajor(op_stack, stack_pos);
        // Retry bump allocation after GC (young_offset reset to 0)
        size_t retry_offset = young_offset.load(std::memory_order_relaxed);
        while(retry_offset + aligned_total <= young_region_size) {
          if(young_offset.compare_exchange_weak(retry_offset, retry_offset + aligned_total,
                                                std::memory_order_relaxed)) {
            size_t* raw_mem = (size_t*)(young_region + retry_offset);
            raw_mem[0] = alloc_size;
            mem = raw_mem + 1;
            mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
            mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
            mem += EXTRA_BUF_SIZE;
            mem[MARKED_FLAG] = 0;
            std::atomic_thread_fence(std::memory_order_release);
            allocation_size += size;
            return mem;
          }
        }
      }
    }

    // Fallback: old gen allocation
    if(collect && allocation_size + size > mem_max_size) {
      CollectMajor(op_stack, stack_pos);
    }
    mem = GetMemory(alloc_size);
    if(!mem) {
      std::wcerr << L">>> Unable to allocate memory of size: " << alloc_size << L" <<<" << std::endl;
      exit(1);
    }
    mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
    mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
    mem += EXTRA_BUF_SIZE;
    mem[MARKED_FLAG] |= GC_OLD_BIT;
#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    allocation_size += size;
    old_allocation_size += size;
    old_generation.insert(mem);
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _MEM_LOGGING
    mem_logger << mem_cycle << L",alloc,obj," << mem << L"," << size << std::endl;
#endif

#ifdef _DEBUG_GC
    std::wcout << L"# allocating object: addr=" << mem << L"(" << (size_t)mem
          << L"), size=" << size << L" byte(s), used=" << allocation_size << L" byte(s) #" << std::endl;
#endif
  }

  return mem;
}

size_t* MemoryManager::AllocateArray(const size_t size, const MemoryType type, size_t* op_stack, size_t stack_pos, bool collect)
{
  // Allocation safepoint (see AllocateObject).
  if(collect) { SafePoint(); }
  size_t elem_size;
  size_t* mem;
  switch (type) {
  case BYTE_ARY_TYPE:
    elem_size = sizeof(char);
    break;

  case CHAR_ARY_TYPE:
    elem_size = sizeof(wchar_t);
    break;

  case INT_TYPE:
    elem_size = sizeof(size_t);
    break;

  case FLOAT_TYPE:
    elem_size = sizeof(FLOAT_VALUE);
    break;

  default:
    std::wcerr << L">>> Invalid memory allocation <<<" << std::endl;
    exit(1);
  }

  // Guard against integer overflow in the size computation: a wrapped value
  // would under-allocate while the logical array bound (mem[0]) stays huge,
  // letting in-range indices address memory past the buffer. Fail closed.
  if(size > (~(size_t)0 - sizeof(size_t) * EXTRA_BUF_SIZE) / elem_size) {
    std::wcerr << L">>> Array allocation size overflow <<<" << std::endl;
    exit(1);
  }
  const size_t calc_size = size * elem_size;
  const size_t alloc_size = calc_size + sizeof(size_t) * EXTRA_BUF_SIZE;

  // Allocate in old gen (bump allocator disabled — promotion/fixup cannot safely
  // update all root locations including PDA thread operand stacks)
  if(collect && allocation_size + calc_size > mem_max_size) {
    CollectMajor(op_stack, stack_pos);
  }
  mem = GetMemory(alloc_size);
  if(!mem) {
    std::wcerr << L">>> Unable to allocate memory of size: " << alloc_size << L" <<<" << std::endl;
    exit(1);
  }
  mem[EXTRA_BUF_SIZE + TYPE] = type;
  mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = calc_size;
  mem += EXTRA_BUF_SIZE;
  mem[MARKED_FLAG] |= GC_OLD_BIT;
#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
#endif
  allocation_size += calc_size;
  old_allocation_size += calc_size;
  old_generation.insert(mem);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _MEM_LOGGING
  mem_logger << mem_cycle << L",alloc,array," << mem << L"," << size << std::endl;
#endif

#ifdef _DEBUG_GC
  std::wcout << L"# allocating array: addr=" << mem << L"(" << (size_t)mem
    << L"), size=" << calc_size << L" byte(s), used=" << allocation_size << L" byte(s) #" << std::endl;
#endif

  return mem;
}

size_t* MemoryManager::GetMemory(size_t size) {
  size_t* mem = GetFreeMemory(size);
  if(mem) {
    return mem;
  }

  size_t alloc_size = size + sizeof(size_t);
  size_t* raw_mem = (size_t*)calloc(alloc_size, sizeof(char));
  if(!raw_mem) {
    return nullptr;
  }

#ifdef _DEBUG_GC
  std::wcout << L"*** Raw allocation: address=" << raw_mem << L" ***" << std::endl;
#endif
  raw_mem[0] = size;
  return raw_mem + 1;
}

void MemoryManager::AddFreeMemory(size_t* raw_mem) {
  const size_t size = AlignMemorySize(raw_mem[0]);
  if(size) {
    AddFreeCache(size, raw_mem);
  }
  else {
    free(raw_mem);
    raw_mem = nullptr;
  }
}

void MemoryManager::AddFreeCache(size_t pool, size_t* raw_mem) {
#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  free_memory_cache_size += raw_mem[0];

  // Push onto the bucket's intrusive list: word [0] holds the block's actual
  // size (kept), word [1] becomes the next-free link. No node allocation.
  const size_t idx = PoolIndex(pool);
  raw_mem[1] = (size_t)free_buckets[idx];
  free_buckets[idx] = raw_mem;
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&free_memory_cache_lock);
#endif
}

size_t* MemoryManager::GetFreeMemory(size_t size) {
  size_t cache_size = AlignMemorySize(size);
  if(!cache_size) {
    return nullptr;
  }

#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  // Find first block with actual size >= requested (blocks in the same aligned
  // bucket may have different actual sizes), unlinking it from the list.
  const size_t idx = PoolIndex(cache_size);
  size_t* prev = nullptr;
  size_t* check_mem = free_buckets[idx];
  while(check_mem) {
    size_t* next = (size_t*)check_mem[1];
    if(check_mem[0] >= size) {
      if(prev) {
        prev[1] = (size_t)next;
      }
      else {
        free_buckets[idx] = next;
      }
      free_memory_cache_size -= check_mem[0];
      memset(check_mem + 1, 0, check_mem[0]);   // also clears the next link
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&free_memory_cache_lock);
#endif
      return check_mem + 1;
    }
    prev = check_mem;
    check_mem = next;
  }
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&free_memory_cache_lock);
#endif

  return nullptr;
}

size_t MemoryManager::AlignMemorySize(size_t size) {
  --size;
  size |= size >> 1;
  size |= size >> 2;
  size |= size >> 4;
  size |= size >> 8;
  size |= size >> 16;
  size |= size >> 32;
  ++size;

  return size > ALIGN_POOL_MAX ? 0 : size;
}

void MemoryManager::ClearFreeMemory() {
#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  for(size_t i = 0; i < FREE_POOL_COUNT; ++i) {
    size_t* raw_mem = free_buckets[i];
    while(raw_mem) {
      size_t* next = (size_t*)raw_mem[1];
      free_memory_cache_size -= raw_mem[0];
      free(raw_mem);
      raw_mem = next;
    }
    free_buckets[i] = nullptr;
  }
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&free_memory_cache_lock);
#endif
}

size_t* MemoryManager::ValidObjectCast(size_t* mem, long to_id, long* cls_hierarchy, long** cls_interfaces)
{
  // invalid array cast  
  long id = GetObjectID(mem);
  if(id < 0) {
    return nullptr;
  }

  // upcast
  long virtual_cls_id = id;
  while(virtual_cls_id != -1) {
    if (virtual_cls_id == to_id) {
      return mem;
    }
    // update
    virtual_cls_id = cls_hierarchy[virtual_cls_id];
  }

  // check interfaces
  virtual_cls_id = id;
  while(virtual_cls_id != -1) {
    long* interfaces = cls_interfaces[virtual_cls_id];
    if(interfaces) {
      int i = 0;
      long inf_id = interfaces[i];
      while(inf_id > INF_ENDING) {
        if (inf_id == to_id) {
          return mem;
        }
        inf_id = interfaces[++i];
      }
    }
    // update
    virtual_cls_id = cls_hierarchy[virtual_cls_id];
  }

  return nullptr;
}

void MemoryManager::CollectAllMemory(size_t* op_stack, size_t stack_pos)
{
#ifdef _TIMING
  std::wcout << L"=========================================" << std::endl;
  clock_t start = clock();
#endif

#ifndef _GC_SERIAL
#ifdef _WIN32
  // only one thread at a time can invoke the gargabe collector
  if(!TryEnterCriticalSection(&marked_sweep_lock)) {
    // Another thread is collecting. Count as parked (this thread's VM roots are
    // stable while it blocks here) and wait the collection out, rather than
    // mutating concurrently. A one-shot SafePoint would race the collector
    // setting stw_active just after it won the lock.
    BeginBlocking();
    EnterCriticalSection(&marked_sweep_lock);
    LeaveCriticalSection(&marked_sweep_lock);
    EndBlocking();
    return;
  }
#else
  if(pthread_mutex_trylock(&marked_sweep_lock)) {
    BeginBlocking();
    pthread_mutex_lock(&marked_sweep_lock);
    pthread_mutex_unlock(&marked_sweep_lock);
    EndBlocking();
    return;
  }
#endif

  // Stop the world: wait until every OTHER mutator has parked at a safepoint, so
  // the marker scans a complete, stable root set and never frees a live object
  // whose root is in a running thread's registers / mid-opcode C-locals.
  MUTEX_LOCK(&stw_lock);
  stw_active.store(true, std::memory_order_release);
  while(parked_count.load(std::memory_order_acquire) <
        mutator_count.load(std::memory_order_acquire) - 1) {
    SLEEP_CONDITION(&stw_cv, &stw_lock);
  }
  MUTEX_UNLOCK(&stw_lock);
#endif

  CollectionInfo* info = new CollectionInfo;
  info->op_stack = op_stack;
  info->stack_pos = stack_pos;

  CollectMemory(info);

#ifndef _GC_SERIAL
  // Resume the world.
  MUTEX_LOCK(&stw_lock);
  stw_active.store(false, std::memory_order_release);
  WAKE_ALL_CONDITION(&stw_cv);
  MUTEX_UNLOCK(&stw_lock);

  MUTEX_UNLOCK(&marked_sweep_lock);
#endif

  delete info;
  info = nullptr;

#ifdef _TIMING
  clock_t end = clock();
  std::wcout << L"Collection: size=" << mem_max_size << L", time=" << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
  std::wcout << L"=========================================" << std::endl << std::endl;
#endif
}

#ifdef _WIN32
unsigned int MemoryManager::CollectMemory(void* arg)
#else
void* MemoryManager::CollectMemory(void* arg)
#endif
{
#ifdef _TIMING
  clock_t start = clock();
#endif

  CollectionInfo* info = (CollectionInfo*)arg;
  const size_t saved_stack_pos = info->stack_pos;  // Save before CheckStack modifies it


#ifdef _DEBUG_GC
  size_t start = allocation_size;
  std::wcout << std::dec << std::endl << L"=========================================" << std::endl;
#ifdef _WIN32  
  std::wcout << L"Starting Garbage Collection; thread=" << GetCurrentThread() << std::endl;
#else
  std::wcout << L"Starting Garbage Collection; thread=" << pthread_self() << std::endl;
#endif  
  std::wcout << L"=========================================" << std::endl;
  std::wcout << L"## Marking memory ##" << std::endl;
#endif

#ifndef _GC_SERIAL
#ifdef _WIN32
  const int num_threads = 3;
  HANDLE thread_ids[num_threads];

  thread_ids[0] = (HANDLE)_beginthreadex(nullptr, 0, CheckStatic, info, 0, nullptr);
  if(!thread_ids[0]) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }

  thread_ids[1] = (HANDLE)_beginthreadex(nullptr, 0, CheckStack, info, 0, nullptr);
  if(!thread_ids[1]) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }

  thread_ids[2] = (HANDLE)_beginthreadex(nullptr, 0, CheckPdaRoots, nullptr, 0, nullptr);
  if(!thread_ids[2]) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }

  // join all mark threads
  if(WaitForMultipleObjects(num_threads, thread_ids, TRUE, INFINITE) != WAIT_OBJECT_0) {
    std::wcerr << L"Unable to join garbage collection threads!" << std::endl;
    exit(-1);
  }

  for(int i=0; i < num_threads; ++i) {
    CloseHandle(thread_ids[i]);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
  
  pthread_t static_thread;
  if(pthread_create(&static_thread, &attrs, CheckStatic, (void*)info)) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }

  pthread_t stack_thread;
  if(pthread_create(&stack_thread, &attrs, CheckStack, (void*)info)) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }

  pthread_t pda_thread;
  if(pthread_create(&pda_thread, &attrs, CheckPdaRoots, nullptr)) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }
  
  pthread_attr_destroy(&attrs);
  
  // join all of the mark threads
  void *status;

  if(pthread_join(static_thread, &status)) {
    std::wcerr << L"Unable to join garbage collection threads!" << std::endl;
    exit(-1);
  }
  
  if(pthread_join(stack_thread, &status)) {
    std::wcerr << L"Unable to join garbage collection threads!" << std::endl;
    exit(-1);
  }

  if(pthread_join(pda_thread, &status)) {
    std::wcerr << L"Unable to join garbage collection threads!" << std::endl;
    exit(-1);
  }
#endif  
#else
  CheckStatic(nullptr);
  CheckStack(info);
  CheckPdaRoots(nullptr);
  CheckJitRoots(nullptr);
#endif
  
#ifdef _TIMING
  clock_t end = clock();
  std::wcout << std::dec << L"Mark time: " << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
  start = clock();
#endif
  
  // sweep memory
#ifdef _DEBUG_GC
  std::wcout << L"## Sweeping memory ##" << std::endl;
#endif

  // Sweep phase: mark threads have already joined, so only allocated_lock
  // is needed (protects old_generation set from concurrent allocations).
  // marked_lock is no longer needed here since MarkMemory uses lock-free CAS.
#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
#endif

#ifdef _DEBUG_GC
  std::wcout << L"-----------------------------------------" << std::endl;
  std::wcout << L"Sweeping..." << std::endl;
  std::wcout << L"-----------------------------------------" << std::endl;
#endif

  // --- Promote surviving young objects to old gen ---
  std::vector<size_t*> promoted_objects;
  size_t young_used = young_offset.load(std::memory_order_relaxed);
  size_t dead_young_size = 0;
  size_t promoted_count = 0;

  // Linear walk through young region
  uint8_t* scan_ptr = young_region;
  while(scan_ptr < young_region + young_used) {
    size_t* raw_mem = (size_t*)scan_ptr;
    size_t alloc_size = raw_mem[0];
    size_t total = sizeof(size_t) + alloc_size;

    // Alignment: round up total to sizeof(size_t) boundary (matches allocation alignment)
    total = (total + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);

    size_t* mem = raw_mem + 1 + EXTRA_BUF_SIZE;

    // Get object size for accounting
    size_t mem_size;
    if(mem[TYPE] == NIL_TYPE) {
      StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
      mem_size = cls ? cls->GetInstanceMemorySize() : 0;
    }
    else {
      mem_size = mem[SIZE_OR_CLS];
    }

    if(mem[MARKED_FLAG] & GC_MARK_BIT) {
      // Promote to old gen: allocate, copy, store forwarding pointer
      size_t* new_raw = (size_t*)calloc(total, 1);
      if(new_raw) {
        memcpy(new_raw, raw_mem, total);
        size_t* new_mem = new_raw + 1 + EXTRA_BUF_SIZE;
        new_mem[MARKED_FLAG] = GC_OLD_BIT | GC_MARK_BIT;  // set old bit + keep mark bit so sweep preserves it
        old_generation.insert(new_mem);
        old_allocation_size += mem_size;
        promoted_objects.push_back(new_mem);
        promoted_count++;
        // Store forwarding pointer in young region (safe — region is about to be reclaimed)
        mem[MARKED_FLAG] = (size_t)new_mem;
      }
    }
    else {
      // Dead young object
      allocation_size -= mem_size;
      dead_young_size += mem_size;
    }

    scan_ptr += total;
  }


  // --- Sweep old generation (major GC only) ---
  size_t dead_old_count = 0;
  [[maybe_unused]] const size_t prev_old_size = old_generation.size();

  if(!minor_gc_mode.load(std::memory_order_acquire)) {
    // Major GC: free dead old-gen objects
    for(auto iter = old_generation.begin(); iter != old_generation.end(); ) {
      size_t* mem = *iter;
      if(mem[MARKED_FLAG] & GC_MARK_BIT) {
        mem[MARKED_FLAG] &= ~GC_MARK_BIT;
        ++iter;
      }
      else {
        size_t mem_size;
        if(mem[TYPE] == NIL_TYPE) {
          StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
          mem_size = cls ? cls->GetInstanceMemorySize() : mem[SIZE_OR_CLS];
        }
        else {
          mem_size = mem[SIZE_OR_CLS];
        }
        allocation_size -= mem_size;
        old_allocation_size -= mem_size;
        dead_old_count++;

        size_t* tmp = mem - EXTRA_BUF_SIZE;
        AddFreeMemory(tmp - 1);

#ifdef _DEBUG_GC
        std::wcout << L"# freeing old memory: addr=" << mem << L"(" << (size_t)mem
              << L"), size=" << mem_size << L" byte(s) #" << std::endl;
#endif
        iter = old_generation.erase(iter);
      }
    }
  }
  else {
    // Minor GC: just clear mark bits on old objects
    for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
      size_t* mem = *iter;
      mem[MARKED_FLAG] &= ~GC_MARK_BIT;
    }
  }

  // --- Fixup phase: replace young pointers with forwarded old-gen addresses ---
  if(young_used > 0) {
    // Fix up roots (use saved_stack_pos since CheckStack decremented info->stack_pos to -1)
    FixupRoots(info->op_stack, saved_stack_pos);

    // Fix up old-gen objects
    size_t dc = dirty_count.load(std::memory_order_relaxed);
    bool overflow = dc > DIRTY_LIST_MAX;

    if(overflow || !minor_gc_mode.load(std::memory_order_acquire)) {
      // Overflow or major GC: scan all old-gen objects
      for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
        FixupObject(*iter);
      }
    }
    else {
      // Minor GC: only fix up dirty objects + promoted objects
      for(size_t i = 0; i < dc; ++i) {
        // Dirty object might have been freed during major sweep — check it's still valid
        if(dirty_list[i] && old_generation.count(dirty_list[i])) {
          FixupObject(dirty_list[i]);
        }
      }
      for(size_t i = 0; i < promoted_objects.size(); ++i) {
        FixupObject(promoted_objects[i]);
      }
    }

    // Reset young region
    young_offset.store(0, std::memory_order_relaxed);
    memset(young_region, 0, young_used);
  }

  // --- Clear dirty list and RSET bits ---
  {
    size_t dc = dirty_count.load(std::memory_order_acquire);
    if(dc > DIRTY_LIST_MAX) {
      // Overflow: objects dirtied past DIRTY_LIST_MAX had GC_RSET_BIT set by the
      // write barrier but were never recorded in dirty_list, so the list-based clear
      // would miss them and leave a stale rset bit that permanently suppresses their
      // future write barrier. Clear the bit across all of old gen instead.
      for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
        (*iter)[MARKED_FLAG] &= ~GC_RSET_BIT;
      }
      for(size_t i = 0; i < DIRTY_LIST_MAX; ++i) {
        dirty_list[i] = nullptr;
      }
    }
    else {
      for(size_t i = 0; i < dc; ++i) {
        if(dirty_list[i] && old_generation.count(dirty_list[i])) {
          dirty_list[i][MARKED_FLAG] &= ~GC_RSET_BIT;
        }
        dirty_list[i] = nullptr;
      }
    }
    dirty_count.store(0, std::memory_order_release);
  }

  // Adjust GC constraints based on collection effectiveness
  size_t total_dead = dead_young_size + dead_old_count;
  if(total_dead == 0) {
    if(uncollected_count < UNCOLLECTED_COUNT) {
      uncollected_count++;
    }
    else {
      mem_max_size <<= 4;
      uncollected_count = 0;
    }
  }
  else if(mem_max_size != MEM_START_MAX) {
    if(collected_count < COLLECTED_COUNT) {
      collected_count++;
    }
    else {
      mem_max_size >>= 2;
      if(mem_max_size <= 0) {
        mem_max_size = MEM_START_MAX;
      }
      collected_count = 0;
    }
  }

  // clear free memory cache if oversized
  if(free_memory_cache_size > mem_max_size) {
    ClearFreeMemory();
  }

#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif

#ifdef _MEM_LOGGING
  mem_cycle++;
#endif

#ifdef _DEBUG_GC
  std::wcout << L"===============================================================" << std::endl;
  std::wcout << L"Finished Collection: promoted=" << promoted_count
        << L", dead_young=" << dead_young_size << L" byte(s)"
        << L", dead_old=" << dead_old_count
        << L", alloc=" << allocation_size << L" byte(s)" << std::endl;
  std::wcout << L"===============================================================" << std::endl;
#endif

#ifdef _TIMING
  end = clock();
  std::wcout << std::dec << L"Sweep time: " << (double)(end - start) / CLOCKS_PER_SEC << L" second(s)." << std::endl;
#endif

  return 0;
}

#ifdef _WIN32
unsigned int MemoryManager::CheckStatic([[maybe_unused]] void* arg)
#else
void* MemoryManager::CheckStatic([[maybe_unused]] void* arg)
#endif
{
  StackClass** clss = prgm->GetClasses();
  const int cls_num = static_cast<int>(prgm->GetClassNumber());

  for(int i = 0; i < cls_num; ++i) {
    StackClass* cls = clss[i];
    CheckMemory(cls->GetClassMemory(), cls->GetClassDeclarations(), cls->GetNumberClassDeclarations(), 0);
  }
  
  return 0;
}

#ifdef _WIN32
unsigned int MemoryManager::CheckStack(void* arg)
#else
void* MemoryManager::CheckStack(void* arg)
#endif
{
  CollectionInfo* info = (CollectionInfo*)arg;
#ifdef _DEBUG_GC
  std::wcout << L"----- Marking Stack: stack: pos=" << info->stack_pos 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread() << L" -----" << std::endl;
#else
        << L"; thread=" << pthread_self() << L" -----" << std::endl;
#endif    
#endif

  while((int64_t)info->stack_pos > -1) {
    size_t* check_mem = (size_t*)info->op_stack[info->stack_pos--];
#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    const bool found = IsAllocated(check_mem);
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
#endif
    if(found) {
      CheckObject(check_mem, false, 1);
    }
  }

#ifndef _WIN32
#ifndef _GC_SERIAL
  pthread_exit(nullptr);
#endif
#endif
    
  return 0;  
}

#ifdef _WIN32
unsigned int MemoryManager::CheckJitRoots([[maybe_unused]] void* arg)
#else
void* MemoryManager::CheckJitRoots([[maybe_unused]] void* arg)
#endif
{
#ifndef _GC_SERIAL
  MUTEX_LOCK(&jit_frame_lock);
#endif  

#ifdef _DEBUG_GC
  std::wcout << L"---- Marking JIT method root(s): num=" << jit_frames.size()
#ifdef _WIN32
        << L"; thread=" << GetCurrentThread() << L" ------" << std::endl;
#else
        << L"; thread=" << pthread_self() << L" ------" << std::endl;
#endif    
  std::wcout << L"memory types: " << std::endl;
#endif
  
  for(size_t i = 0; i < jit_frames.size(); ++i) {
    StackFrame* frame = jit_frames[i];
    StackMethod* method = frame->method;
    size_t* mem = frame->jit_mem;
    size_t* self = (size_t*)frame->mem[0];
    const long dclrs_num = method->GetNumberDeclarations();

#ifdef _DEBUG_GC
    std::wcout << L"\t===== JIT method: name=" << method->GetName() << L", id=" << method->GetClass()->GetId()
      << L"," << method->GetId() << L"; addr=" << method << L"; mem=" << mem << L"; self=" << self
      << L"; num=" << method->GetNumberDeclarations() << L" =====" << std::endl;
#endif

    if(mem) {
#ifdef _ARM64
      size_t* start = mem - 1;
#endif
      
      // check self
      if(!method->IsLambda()) {
        CheckObject(self, true, 1);
      }

      StackDclr** dclrs = method->GetDeclarations();
#ifdef _ARM64
      // front to back...
      if(method->HasAndOr()) {
        mem++;
      }
      
      for(int j = 0; j < dclrs_num; ++j) {
#else
      // front to back...
      for(long j = dclrs_num - 1; j >= 0; --j) {
#endif
        // update address based upon type
        switch(dclrs[j]->type) {
        case FUNC_PARM: {
          size_t* lambda_mem = (size_t*) * (mem + 1);
          const size_t mthd_cls_id = *mem;
          const long virtual_cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
          const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": FUNC_PARM: id=(" << virtual_cls_id << L"," << mthd_id << L"), mem=" << lambda_mem << std::endl;
#endif
          std::pair<int, StackDclr**> closure_dclrs = prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(static_cast<int>(mthd_id));
          if(MarkMemory(lambda_mem)) {
            CheckMemory(lambda_mem, closure_dclrs.second, closure_dclrs.first, 1);
          }
          // update
          mem += 2;
        }
          break;

        case CHAR_PARM:
        case INT_PARM:
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": CHAR_PARM/INT_PARM: value=" << (*mem) << std::endl;
#endif
          // update
          mem++;
          break;

        case FLOAT_PARM: {
#ifdef _DEBUG_GC
          FLOAT_VALUE value;
          memcpy(&value, mem, sizeof(FLOAT_VALUE));
          std::wcout << L"\t" << j << L": FLOAT_PARM: value=" << value << std::endl;
#endif
          // update
          mem++;
        }
          break;

        case BYTE_ARY_PARM:
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": BYTE_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << std::endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case CHAR_ARY_PARM:
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": CHAR_ARY_PARM: addr=" << (size_t*)(*mem) << L"(" << (size_t)(*mem)
            << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << std::endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case INT_ARY_PARM:
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": INT_ARY_PARM: addr=" << (size_t*)(*mem)
            << L"(" << (size_t)(*mem) << L"), size="
            << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << std::endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case FLOAT_ARY_PARM:
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": FLOAT_ARY_PARM: addr=" << (size_t*)(*mem)
            << L"(" << (size_t)(*mem) << L"), size=" << L" byte(s)"
            << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) << std::endl;
#endif
          // mark data
          MarkMemory((size_t*)(*mem));
          // update
          mem++;
          break;

        case OBJ_PARM: {
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": OBJ_PARM: addr=" << (size_t*)(*mem)
            << L"(" << (size_t)(*mem) << L"), id=";
          if(*mem) {
            StackClass* tmp = (StackClass*)((size_t*)(*mem))[SIZE_OR_CLS];
           std::wcout << L"'" << tmp->GetName() << L"'" << std::endl;
          }
          else {
            std::wcout << L"Unknown" << std::endl;
          }
#endif
          // check object
          CheckObject((size_t*)(*mem), true, 1);
          // update
          mem++;
        }
          break;

        case OBJ_ARY_PARM:
#ifdef _DEBUG_GC
          std::wcout << L"\t" << j << L": OBJ_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << std::endl;
#endif
          // mark data
          if(MarkMemory((size_t*)(*mem))) {
            size_t* array = (size_t*)(*mem);
            const size_t size = array[0];
            const size_t dim = array[1];
            size_t* objects = (size_t*)(array + 2 + dim);
            for(size_t k = 0; k < size; ++k) {
              CheckObject((size_t*)objects[k], true, 2);
            }
          }
          // update
          mem++;
          break;

        default:
          break;
        }
      }

      // NOTE: this marks temporary variables that are stored in JIT memory
      // during some method calls. There are 6 integer temp addresses
#ifdef _ARM64
      mem = start;
      for(int i = 0; i > -JIT_TMP_LOOK_BACK; --i) {
#else
      for(int i = 0; i < JIT_TMP_LOOK_BACK; ++i) {
#endif
        size_t* check_mem = (size_t*)mem[i];
#ifndef _GC_SERIAL
        MUTEX_LOCK(&allocated_lock);
#endif 
        const bool found = IsAllocated(check_mem);
#ifndef _GC_SERIAL
        MUTEX_UNLOCK(&allocated_lock);
#endif
        if(found) {
          CheckObject(check_mem, false, 1);
        }
      }
    }
#ifdef _DEBUG_GC
    else {
      std::wcout << L"\t\t--- Nil memory ---" << std::endl;
    }
#endif
  }
  jit_frames.clear();

#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&jit_frame_lock);
#ifndef _WIN32
  pthread_exit(nullptr);
#endif
#endif
  
  return 0;
}

#ifdef _WIN32
unsigned int MemoryManager::CheckPdaRoots([[maybe_unused]] void* arg)
#else
void* MemoryManager::CheckPdaRoots([[maybe_unused]] void* arg)
#endif
{
  std::vector<StackFrame*> frames;

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_frame_lock);
#endif

#ifdef _DEBUG_GC
  std::wcout << L"----- PDA frames(s): num=" << pda_frames.size() 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread()<< L" -----" << std::endl;
#else
        << L"; thread=" << pthread_self() << L" -----" << std::endl;
#endif    
  std::wcout << L"memory types:" <<  std::endl;
#endif

  for(std::unordered_set<StackFrame**>::iterator iter = pda_frames.begin(); iter != pda_frames.end(); ++iter) {
    StackFrame** frame = *iter;
    if(*frame) {
      if((*frame)->jit_mem) {
#ifndef _GC_SERIAL
        MUTEX_LOCK(&jit_frame_lock);
#endif
        jit_frames.push_back(*frame);
#ifndef _GC_SERIAL
        MUTEX_UNLOCK(&jit_frame_lock);
#endif
      }
      else {
        frames.push_back(*frame);
      }
    }
  }
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_frame_lock);
#endif 
  
  // ------
#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_monitor_lock);
#endif

#ifdef _DEBUG_GC
  std::wcout << L"----- PDA method root(s): num=" << pda_monitors.size() 
#ifdef _WIN32  
        << L"; thread=" << GetCurrentThread()<< L" -----" << std::endl;
#else
        << L"; thread=" << pthread_self()<< L" -----" << std::endl;
#endif    
  std::wcout << L"memory types:" <<  std::endl;
#endif

    // look at pda methods
  std::unordered_set<StackFrameMonitor*>::iterator pda_iter;
  for(pda_iter = pda_monitors.begin(); pda_iter != pda_monitors.end(); ++pda_iter) {
    StackFrameMonitor* monitor = *pda_iter;
    // gather stack frames — acquire fence pairs with release fence in PushFrame/PopFrame
    // to ensure we see the frame pointers corresponding to the position we read
    long call_stack_pos = *(monitor->call_stack_pos);
    std::atomic_thread_fence(std::memory_order_acquire);

    // >= 0 (not > 0): at call_stack_pos==0 the thread is executing its TOP-LEVEL
    // method, whose frame lives in cur_frame with an empty call_stack. The old
    // `> 0` guard skipped that frame entirely, so a thread parked while running
    // its top method (between inlined ops) had its locals unscanned -> freed.
    // call_stack_pos==-1 is the constructor state (cur_frame uninitialized) — skip.
    if(call_stack_pos >= 0) {
      StackFrame** call_stack = monitor->call_stack;
      StackFrame* cur_frame = *(monitor->cur_frame);

      if(cur_frame->jit_mem) {
#ifndef _GC_SERIAL
        MUTEX_LOCK(&jit_frame_lock);
#endif
        jit_frames.push_back(cur_frame);
#ifndef _GC_SERIAL
        MUTEX_UNLOCK(&jit_frame_lock);
#endif
      }
      else {
        frames.push_back(cur_frame);
      }

      // copy frames locally
      while(--call_stack_pos > -1) {
        StackFrame* frame = call_stack[call_stack_pos];
        if(frame && frame->jit_mem) {
#ifndef _GC_SERIAL
          MUTEX_LOCK(&jit_frame_lock);
#endif
          jit_frames.push_back(frame);
#ifndef _GC_SERIAL
          MUTEX_UNLOCK(&jit_frame_lock);
#endif
        }
        else if(frame) {
          frames.push_back(frame);
        }
      }
    }

    // Mark THIS thread's operand stack. CheckStack only marks the GC-triggering
    // thread's op_stack (info->op_stack), but FixupRoots later relocates refs in
    // EVERY monitor's op_stack. That asymmetry meant an object live solely via a
    // PARKED thread's op_stack slot (a transient mid-expression, not yet stored to
    // a frame local) went unmarked -> swept -> fixup then chased a dangling pointer.
    // Mark exactly the slots fixup will touch, for every thread. (Re-marking the
    // triggering thread's stack here is harmless — CheckObject is idempotent.)
    size_t* mon_op_stack = monitor->op_stack;
    size_t* mon_stack_pos = monitor->stack_pos;
    if(mon_op_stack && mon_stack_pos) {
      const size_t pos = *mon_stack_pos;
      std::atomic_thread_fence(std::memory_order_acquire);
      for(size_t i = 0; i <= pos; ++i) {
        size_t* check_mem = (size_t*)mon_op_stack[i];
#ifndef _GC_SERIAL
        MUTEX_LOCK(&allocated_lock);
#endif
        const bool found = IsAllocated(check_mem);
#ifndef _GC_SERIAL
        MUTEX_UNLOCK(&allocated_lock);
#endif
        if(found) {
          CheckObject(check_mem, false, 1);
        }
      }
    }
  }

#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_monitor_lock);
#endif

  // check JIT roots in separate thread
#ifndef _GC_SERIAL
#ifdef _WIN32
  HANDLE thread_id = (HANDLE)_beginthreadex(nullptr, 0, CheckJitRoots, nullptr, 0, nullptr);
  if(!thread_id) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }
#else
  pthread_attr_t attrs;
  pthread_attr_init(&attrs);
  pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
  
  pthread_t jit_thread;
  if(pthread_create(&jit_thread, &attrs, CheckJitRoots, nullptr)) {
    std::wcerr << L"Unable to create garbage collection thread!" << std::endl;
    exit(-1);
  }
#endif
#endif

  // check PDA roots
  for(size_t i = 0; i < frames.size(); ++i) {
    StackFrame* frame = frames[i];
    StackMethod* method = frame->method;
    size_t* mem = frame->mem;

#ifdef _DEBUG_GC
    std::wcout << L"\t===== PDA method: name=" << method->GetName() << L", addr="
      << method << L", num=" << method->GetNumberDeclarations() << L" =====" << std::endl;
#endif

    // mark self
    if(!method->IsLambda()) {
      CheckObject((size_t*)(*mem), true, 1);
    }

    if(method->HasAndOr()) {
      mem += 2;
    }
    else {
      mem++;
    }

    // mark rest of memory
    CheckMemory(mem, method->GetDeclarations(), method->GetNumberDeclarations(), 0);
  }

#ifndef _GC_SERIAL
#ifdef _WIN32
  // wait for JIT thread
  if(WaitForSingleObject(thread_id, INFINITE) != WAIT_OBJECT_0) {
    std::wcerr << L"Unable to join garbage collection threads!" << std::endl;
    exit(-1);
  }
  CloseHandle(thread_id);
#else
  void *status;
  if(pthread_join(jit_thread, &status)) {
    std::wcerr << L"Unable to join garbage collection threads!" << std::endl;
    exit(-1);
  }
  pthread_exit(nullptr);
#endif
#endif

  return 0;
}

void MemoryManager::CheckMemory(size_t* mem, StackDclr** dclrs, const long dcls_size, long depth)
{
  // check method
  for(long i = 0; i < dcls_size; ++i) {
#ifdef _DEBUG_GC
    for(int j = 0; j < depth; ++j) {
      std::wcout << L"\t";
    }
#endif

    // update address based upon type
    switch(dclrs[i]->type) {
    case FUNC_PARM: {
      size_t* lambda_mem = (size_t*) * (mem + 1);
      const size_t mthd_cls_id = *mem;
      const long virtual_cls_id = (mthd_cls_id >> (16 * (1))) & 0xFFFF;
      const long mthd_id = (mthd_cls_id >> (16 * (0))) & 0xFFFF;
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": FUNC_PARM: id=(" << virtual_cls_id << L"," << mthd_id << L"), mem=" << lambda_mem << std::endl;
#endif
      std::pair<int, StackDclr**> closure_dclrs = prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(static_cast<int>(mthd_id));
      if(MarkMemory(lambda_mem)) {
        CheckMemory(lambda_mem, closure_dclrs.second, closure_dclrs.first, depth + 1);
      }
      // update
      mem += 2;
    }
      break;

    case CHAR_PARM:
    case INT_PARM:
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": CHAR_PARM/INT_PARM: value=" << (*mem) << std::endl;
#endif
      // update
      mem++;
      break;

    case FLOAT_PARM: {
#ifdef _DEBUG_GC
      FLOAT_VALUE value;
      memcpy(&value, mem, sizeof(FLOAT_VALUE));
      std::wcout << L"\t" << i << L": FLOAT_PARM: value=" << value << std::endl;
#endif
      // update
      mem++;
    }
      break;

    case BYTE_ARY_PARM:
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": BYTE_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << std::endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case CHAR_ARY_PARM:
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": CHAR_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) 
            << L" byte(s)" << std::endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case INT_ARY_PARM:
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": INT_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) 
            << L" byte(s)" << std::endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case FLOAT_ARY_PARM:
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": FLOAT_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0) 
            << L" byte(s)" << std::endl;
#endif
      // mark data
      MarkMemory((size_t*)(*mem));
      // update
      mem++;
      break;

    case OBJ_PARM: {
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": OBJ_PARM: addr=" << (size_t*)(*mem) << L"(" << (size_t)(*mem) << L"), id=";
      if(*mem) {
        StackClass* tmp = (StackClass*)((size_t*)(*mem))[SIZE_OR_CLS];
        std::wcout << L"'" << tmp->GetName() << L"'" << std::endl;
      }
      else {
        std::wcout << L"Unknown" << std::endl;
      }
#endif
      // check object
      CheckObject((size_t*)(*mem), true, depth + 1);
      // update
      mem++;
    }
      break;

    case OBJ_ARY_PARM:
#ifdef _DEBUG_GC
      std::wcout << L"\t" << i << L": OBJ_ARY_PARM: addr=" << (size_t*)(*mem) << L"("
            << (size_t)(*mem) << L"), size=" << ((*mem) ? ((size_t*)(*mem))[SIZE_OR_CLS] : 0)
            << L" byte(s)" << std::endl;
#endif
      // mark data
      if(MarkMemory((size_t*)(*mem))) {
        size_t* array = (size_t*)(*mem);
        const size_t size = array[0];
        const size_t dim = array[1];
        size_t* objects = (size_t*)(array + 2 + dim);
        for(size_t k = 0; k < size; ++k) {
          CheckObject((size_t*)objects[k], true, 2);
        }
      }
      // update
      mem++;
      break;

    default:
      break;
    }
  }
}

// ---- Generational GC: Dirty object scanning ----

void MemoryManager::ScanDirtyObject(size_t* mem)
{
  if(!mem) return;

  // Scan one dirty old-gen object's direct fields for young pointers and mark them
  if(mem[TYPE] == NIL_TYPE) {
    StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
    if(!cls) return;
    StackDclr** dclrs = cls->GetInstanceDeclarations();
    const long num_dclrs = cls->GetNumberInstanceDeclarations();
    size_t* field_ptr = mem;
    for(long i = 0; i < num_dclrs; ++i) {
      switch(dclrs[i]->type) {
      case FUNC_PARM: {
        size_t* ref = (size_t*)*(field_ptr + 1);
        if(ref && IsYoung(ref)) {
          CheckObject(ref, true, 0);
        }
        field_ptr += 2;
        break;
      }
      case OBJ_PARM: {
        size_t* ref = (size_t*)(*field_ptr);
        if(ref && IsYoung(ref)) {
          CheckObject(ref, true, 0);
        }
        field_ptr++;
        break;
      }
      case OBJ_ARY_PARM:
      case BYTE_ARY_PARM:
      case CHAR_ARY_PARM:
      case INT_ARY_PARM:
      case FLOAT_ARY_PARM: {
        size_t* ref = (size_t*)(*field_ptr);
        if(ref && IsYoung(ref)) {
          if(MarkMemory(ref)) {
            if(ref[TYPE] == INT_TYPE || ref[TYPE] == NIL_TYPE) {
              size_t size = ref[0];
              size_t dim = ref[1];
              size_t* objects = ref + 2 + dim;
              for(size_t k = 0; k < size; ++k) {
                CheckObject((size_t*)objects[k], false, 1);
              }
            }
          }
        }
        field_ptr++;
        break;
      }
      default:
        field_ptr++;
        break;
      }
    }
  }
  else if(mem[TYPE] == INT_TYPE) {
    size_t size = mem[0];
    size_t dim = mem[1];
    size_t* objects = mem + 2 + dim;
    for(size_t k = 0; k < size; ++k) {
      size_t* ref = (size_t*)objects[k];
      if(ref && IsYoung(ref)) {
        CheckObject(ref, false, 0);
      }
    }
  }
}

// ---- Fixup functions: replace young pointers with forwarded old-gen addresses ----

void MemoryManager::FixupMemory(size_t* mem, StackDclr** dclrs, const long dcls_size)
{
  for(long i = 0; i < dcls_size; ++i) {
    switch(dclrs[i]->type) {
    case FUNC_PARM: {
      // Relocate the closure pointer if it was promoted...
      size_t* lambda_mem = (size_t*)*(mem + 1);
      const size_t fwd = ForwardedAddr(lambda_mem);
      if(fwd) {
        *(mem + 1) = fwd;
        lambda_mem = (size_t*)fwd;
      }
      // ...then descend into the closure's captured memory, mirroring CheckMemory's
      // FUNC_PARM case. The mark side traverses captures; if fixup does not, a young
      // object captured by a closure is promoted but its captured slot keeps pointing
      // at the old nursery address -> dangling after the nursery is reset. The
      // recursion follows closure nesting only (acyclic by construction), so it ends.
      if(lambda_mem) {
        const size_t mthd_cls_id = *mem;
        const long virtual_cls_id = (mthd_cls_id >> 16) & 0xFFFF;
        const long mthd_id = mthd_cls_id & 0xFFFF;
        std::pair<int, StackDclr**> closure_dclrs =
          prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(static_cast<int>(mthd_id));
        FixupMemory(lambda_mem, closure_dclrs.second, closure_dclrs.first);
      }
      mem += 2;
      break;
    }
    case OBJ_PARM:
    case OBJ_ARY_PARM:
    case BYTE_ARY_PARM:
    case CHAR_ARY_PARM:
    case INT_ARY_PARM:
    case FLOAT_ARY_PARM: {
      const size_t fwd = ForwardedAddr((size_t*)(*mem));
      if(fwd) *mem = fwd;
      mem++;
      break;
    }
    case CHAR_PARM:
    case INT_PARM:
    case FLOAT_PARM:
      mem++;
      break;
    default:
      mem++;
      break;
    }
  }
}

void MemoryManager::FixupObject(size_t* mem)
{
  if(!mem) return;

  if(mem[TYPE] == NIL_TYPE) {
    StackClass* cls = (StackClass*)mem[SIZE_OR_CLS];
    if(cls) {
      FixupMemory(mem, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations());
    }
  }
  else if(mem[TYPE] == INT_TYPE) {
    // Int/obj arrays can contain object references
    size_t size = mem[0];
    size_t dim = mem[1];
    size_t* objects = mem + 2 + dim;
    for(size_t i = 0; i < size; ++i) {
      const size_t fwd = ForwardedAddr((size_t*)objects[i]);
      if(fwd) objects[i] = fwd;
    }
  }
}

void MemoryManager::FixupRoots(size_t* op_stack, size_t stack_pos)
{
  // Fix up GC-triggering thread's operand stack. Conservative scan of untyped words:
  // FixupSlot validates the forwarding target so a plain integer that merely aliases
  // the young address range is not misread as a relocatable reference.
  for(size_t i = 0; i <= stack_pos; ++i) {
    FixupSlot(&op_stack[i]);
  }

  // Fix up other threads' operand stacks via monitors
  // (pda_monitor_lock is already held by the caller's CheckPdaRoots path)
  for(auto pda_iter = pda_monitors.begin(); pda_iter != pda_monitors.end(); ++pda_iter) {
    StackFrameMonitor* monitor = *pda_iter;
    size_t* other_op_stack = monitor->op_stack;
    size_t* other_stack_pos = monitor->stack_pos;
    if(other_op_stack && other_stack_pos && other_op_stack != op_stack) {
      size_t pos = *other_stack_pos;
      for(size_t i = 0; i <= pos; ++i) {
        FixupSlot(&other_op_stack[i]);  // conservative scan — validated relocation
      }
    }
  }

  // Fix up static class memory
  StackClass** clss = prgm->GetClasses();
  const int cls_num = static_cast<int>(prgm->GetClassNumber());
  for(int i = 0; i < cls_num; ++i) {
    StackClass* cls = clss[i];
    FixupMemory(cls->GetClassMemory(), cls->GetClassDeclarations(), cls->GetNumberClassDeclarations());
  }

  // Fix up PDA frames (skip JIT frames — they use jit_mem, not mem for locals)
  std::vector<StackFrame*> jit_fixup_frames;

#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_frame_lock);
#endif
  for(auto iter = pda_frames.begin(); iter != pda_frames.end(); ++iter) {
    StackFrame** frame_ptr = *iter;
    if(*frame_ptr) {
      StackFrame* frame = *frame_ptr;
      if(frame->jit_mem) {
        jit_fixup_frames.push_back(frame);
      }
      else {
        StackMethod* method = frame->method;
        size_t* mem = frame->mem;

        // Fix up self
        if(!method->IsLambda()) {
          size_t* self = (size_t*)(*mem);
          if(self && IsYoung(self)) {
            size_t fwd = self[MARKED_FLAG];
            if(fwd) *mem = fwd;
          }
        }

        // Fix up locals
        size_t* local_mem = mem + (method->HasAndOr() ? 2 : 1);
        FixupMemory(local_mem, method->GetDeclarations(), method->GetNumberDeclarations());
      }
    }
  }
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_frame_lock);
#endif

  // Fix up PDA monitor frames
#ifndef _GC_SERIAL
  MUTEX_LOCK(&pda_monitor_lock);
#endif
  for(auto pda_iter = pda_monitors.begin(); pda_iter != pda_monitors.end(); ++pda_iter) {
    StackFrameMonitor* monitor = *pda_iter;
    // acquire fence pairs with release fence in PushFrame/PopFrame
    long call_stack_pos = *(monitor->call_stack_pos);
    std::atomic_thread_fence(std::memory_order_acquire);

    // >= 0: also fix up the top-level method frame (cur_frame at call_stack_pos==0),
    // matching the mark side. -1 is the constructor state (cur_frame invalid).
    if(call_stack_pos >= 0) {
      StackFrame** call_stack = monitor->call_stack;
      StackFrame* cur_frame = *(monitor->cur_frame);

      // Fix up current frame
      if(cur_frame) {
        if(cur_frame->jit_mem) {
          jit_fixup_frames.push_back(cur_frame);
        }
        else {
          StackMethod* method = cur_frame->method;
          size_t* mem = cur_frame->mem;
          if(!method->IsLambda()) {
            size_t* self = (size_t*)(*mem);
            if(self && IsYoung(self)) {
              size_t fwd = self[MARKED_FLAG];
              if(fwd) *mem = fwd;
            }
          }
          size_t* local_mem = mem + (method->HasAndOr() ? 2 : 1);
          FixupMemory(local_mem, method->GetDeclarations(), method->GetNumberDeclarations());
        }
      }

      // Fix up call stack frames (walk from top down, using PushFrame
      // convention where call_stack_pos points past the last valid entry)
      for(long f = call_stack_pos - 1; f >= 0; --f) {
        StackFrame* frame = call_stack[f];
        if(frame) {
          if(frame->jit_mem) {
            jit_fixup_frames.push_back(frame);
          }
          else {
            StackMethod* method = frame->method;
            size_t* mem = frame->mem;
            if(!method->IsLambda()) {
              size_t* self = (size_t*)(*mem);
              if(self && IsYoung(self)) {
                size_t fwd = self[MARKED_FLAG];
                if(fwd) *mem = fwd;
              }
            }
            size_t* local_mem = mem + (method->HasAndOr() ? 2 : 1);
            FixupMemory(local_mem, method->GetDeclarations(), method->GetNumberDeclarations());
          }
        }
      }
    }
  }
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&pda_monitor_lock);
#endif

  // Fix up JIT frames (collected from pda_frames and pda_monitors above)
  for(size_t i = 0; i < jit_fixup_frames.size(); ++i) {
    StackFrame* frame = jit_fixup_frames[i];
    StackMethod* method = frame->method;
    size_t* mem = frame->jit_mem;

    if(mem) {
      // Fix up self
      if(!method->IsLambda()) {
        size_t* self = (size_t*)frame->mem[0];
        if(self && IsYoung(self)) {
          size_t fwd = self[MARKED_FLAG];
          if(fwd) frame->mem[0] = fwd;
        }
      }

      // Fix up JIT locals
#ifdef _ARM64
      if(method->HasAndOr()) {
        mem++;
      }
      StackDclr** dclrs = method->GetDeclarations();
      const long dclrs_num = method->GetNumberDeclarations();
      for(int j = 0; j < dclrs_num; ++j) {
#else
      StackDclr** dclrs = method->GetDeclarations();
      const long dclrs_num = method->GetNumberDeclarations();
      for(long j = dclrs_num - 1; j >= 0; --j) {
#endif
        switch(dclrs[j]->type) {
        case FUNC_PARM: {
          // Relocate the closure pointer, then descend into its captured memory
          // (mirror CheckMemory / FixupMemory FUNC_PARM — see B1 there).
          size_t* lambda_mem = (size_t*)*(mem + 1);
          const size_t fwd = ForwardedAddr(lambda_mem);
          if(fwd) {
            *(mem + 1) = fwd;
            lambda_mem = (size_t*)fwd;
          }
          if(lambda_mem) {
            const size_t mthd_cls_id = *mem;
            const long virtual_cls_id = (mthd_cls_id >> 16) & 0xFFFF;
            const long mthd_id = mthd_cls_id & 0xFFFF;
            std::pair<int, StackDclr**> closure_dclrs =
              prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(static_cast<int>(mthd_id));
            FixupMemory(lambda_mem, closure_dclrs.second, closure_dclrs.first);
          }
          mem += 2;
          break;
        }
        case OBJ_PARM:
        case OBJ_ARY_PARM:
        case BYTE_ARY_PARM:
        case CHAR_ARY_PARM:
        case INT_ARY_PARM:
        case FLOAT_ARY_PARM: {
          FixupSlot(mem);
          mem++;
          break;
        }
        default:
          mem++;
          break;
        }
      }

      // Fix up JIT temps (conservative scan of untyped words — FixupSlot validates
      // the forwarding target, so spilled scratch / float bit-patterns that merely
      // alias the young address range are not misread as relocatable references).
#ifdef _ARM64
      size_t* start = frame->jit_mem - 1;
      for(int k = 0; k > -JIT_TMP_LOOK_BACK; --k) {
        FixupSlot(&start[k]);
      }
#else
      for(int k = 0; k < JIT_TMP_LOOK_BACK; ++k) {
        FixupSlot(&mem[k]);
      }
#endif
    }
  }
}

// ---- Generational GC: Minor and Major collection entry points ----

void MemoryManager::CollectMinor(size_t* op_stack, size_t stack_pos)
{
#ifndef _GC_SERIAL
#ifdef _WIN32
  if(!TryEnterCriticalSection(&marked_sweep_lock)) {
    return;
  }
#else
  if(pthread_mutex_trylock(&marked_sweep_lock)) {
    return;
  }
#endif
#endif

#ifdef _DEBUG_GC
  std::wcout << L"=== Minor GC: young_offset=" << young_offset.load(std::memory_order_relaxed)
        << L", old_count=" << old_generation.size()
        << L", dirty_count=" << dirty_count.load(std::memory_order_relaxed)
        << L" ===" << std::endl;
#endif

  // Phase 1: Scan dirty old-gen objects for young references (with minor_gc_mode=true)
  minor_gc_mode.store(true, std::memory_order_release);

  size_t dc = dirty_count.load(std::memory_order_relaxed);
  bool overflow = dc > DIRTY_LIST_MAX;

  if(overflow) {
    // Dirty list overflowed — scan all old-gen objects
    for(auto iter = old_generation.begin(); iter != old_generation.end(); ++iter) {
      ScanDirtyObject(*iter);
    }
  }
  else {
    for(size_t i = 0; i < dc; ++i) {
      ScanDirtyObject(dirty_list[i]);
    }
  }

  // Phase 2: Mark from roots + sweep (promote-all + fixup + reset)
  CollectionInfo* info = new CollectionInfo;
  info->op_stack = op_stack;
  info->stack_pos = stack_pos;

  CollectMemory(info);

#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&marked_sweep_lock);
#endif

#ifdef _DEBUG_GC
  std::wcout << L"=== Minor GC complete: old_count=" << old_generation.size() << L" ===" << std::endl;
#endif

  delete info;
  info = nullptr;
}

void MemoryManager::CollectMajor(size_t* op_stack, size_t stack_pos)
{
  minor_gc_mode.store(false, std::memory_order_release);
  CollectAllMemory(op_stack, stack_pos);
}

// ---- End Generational GC ----

void MemoryManager::CheckObject(size_t* mem, bool is_obj, long depth)
{
#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
#endif
  const bool is_allocated = IsAllocated(mem);
#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&allocated_lock);
#endif
  if(is_allocated) {
    // Minor GC optimization: mark old objects but don't recurse into them
    if(minor_gc_mode.load(std::memory_order_acquire) && IsOldGen(mem)) {
      MarkMemory(mem);
      return;
    }
    StackClass* cls;
    if(is_obj) {
      cls = GetClass(mem);
    }
    else {
      cls = GetClassMapping(mem);
    }

    if(cls) {
#ifdef _DEBUG_GC
      for(int i = 0; i < depth; ++i) {
        std::wcout << L"\t";
      }
      std::wcout << L"\t----- object: addr=" << mem << L"(" << (size_t)mem << L"), name='"
            << cls->GetName() << L"', num=" << cls->GetNumberInstanceDeclarations() << L" -----" << std::endl;
#endif

      // mark data
      if(MarkMemory(mem)) {
        CheckMemory(mem, cls->GetInstanceDeclarations(), cls->GetNumberInstanceDeclarations(), depth);
      }
    } 
    else {
      // NOTE: this happens when we are trying to mark unidentified memory
      // segments. these segments may be parts of that stack or temp for
      // register variables
#ifdef _DEBUG_GC
      for(int i = 0; i < depth; ++i) {
        std::wcout << L"\t";
      }
      std::wcout <<"$: addr/value=" << mem << std::endl;
      if(is_obj) {
        assert(cls);
      }
#endif
      // primitive or object array
      if(MarkMemory(mem)) {
        // ensure we're only checking int and obj arrays
        if(mem[TYPE] == NIL_TYPE || mem[TYPE] == INT_TYPE) {
            size_t* array = mem;
            const size_t size = array[0];
            const size_t dim = array[1];
            size_t* objects = (size_t*)(array + 2 + dim);
            for(size_t i = 0; i < size; ++i) {
              CheckObject((size_t*)objects[i], false, 2);
            }
        }
      }
    }
  }
}
