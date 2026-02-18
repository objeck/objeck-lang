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

std::unordered_map<size_t, std::list<size_t*>*> MemoryManager::free_memory_cache;
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

#ifdef _MEM_LOGGING
ofstream MemoryManager::mem_logger;
long MemoryManager::mem_cycle = 0L;
#endif

// operation locks
#ifdef _WIN32
CRITICAL_SECTION MemoryManager::jit_frame_lock;
CRITICAL_SECTION MemoryManager::pda_frame_lock;
CRITICAL_SECTION MemoryManager::pda_monitor_lock;
CRITICAL_SECTION MemoryManager::allocated_lock;
CRITICAL_SECTION MemoryManager::marked_lock;
CRITICAL_SECTION MemoryManager::marked_sweep_lock;
CRITICAL_SECTION MemoryManager::free_memory_cache_lock;
#else
pthread_mutex_t MemoryManager::pda_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::pda_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::jit_frame_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::allocated_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MemoryManager::marked_lock = PTHREAD_MUTEX_INITIALIZER;
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

  // Old generation
  old_allocation_size = 0;
  old_generation.reserve(4096);

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
  InitializeCriticalSection(&marked_lock);
  InitializeCriticalSection(&marked_sweep_lock);
  InitializeCriticalSection(&free_memory_cache_lock);
#endif

  initialized = true;
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

    // mark & add to list
#ifndef _GC_SERIAL
    MUTEX_LOCK(&marked_lock);
#endif
    mem[MARKED_FLAG] |= GC_MARK_BIT;
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&marked_lock);
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
  StackClass* cls = prgm->GetClass(obj_id);
#ifdef _DEBUG_GC
  assert(cls);
#endif

  size_t* mem = nullptr;
  if(cls) {
    const long size = cls->GetInstanceMemorySize();
    const size_t alloc_size = size * 2 + sizeof(size_t) * EXTRA_BUF_SIZE;

    // Try young generation bump allocation
    const size_t total = sizeof(size_t) + alloc_size;
    const size_t aligned_total = (total + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
    size_t offset = young_offset.load(std::memory_order_relaxed);

    if(offset + aligned_total <= young_region_size) {
      // Bump allocate in young region
      size_t new_offset = offset + aligned_total;
      young_offset.store(new_offset, std::memory_order_relaxed);

      size_t* raw_mem = (size_t*)(young_region + offset);
      memset(raw_mem, 0, aligned_total);
      raw_mem[0] = alloc_size;
      mem = raw_mem + 1;
      mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
      mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
      mem += EXTRA_BUF_SIZE;

#ifndef _GC_SERIAL
      MUTEX_LOCK(&allocated_lock);
#endif
      allocation_size += size;
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&allocated_lock);
#endif
    }
    else {
      // Young region full — trigger minor GC, then retry
      if(collect) {
        CollectMinor(op_stack, stack_pos);
      }

      offset = young_offset.load(std::memory_order_relaxed);
      if(offset + aligned_total <= young_region_size) {
        size_t new_offset = offset + aligned_total;
        young_offset.store(new_offset, std::memory_order_relaxed);

        size_t* raw_mem = (size_t*)(young_region + offset);
        memset(raw_mem, 0, aligned_total);
        raw_mem[0] = alloc_size;
        mem = raw_mem + 1;
        mem[EXTRA_BUF_SIZE + TYPE] = NIL_TYPE;
        mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = (size_t)cls;
        mem += EXTRA_BUF_SIZE;

#ifndef _GC_SERIAL
        MUTEX_LOCK(&allocated_lock);
#endif
        allocation_size += size;
#ifndef _GC_SERIAL
        MUTEX_UNLOCK(&allocated_lock);
#endif
      }
      else {
        // Still full after minor GC — allocate directly in old gen
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
      }
    }

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
  size_t calc_size;
  size_t* mem;
  switch (type) {
  case BYTE_ARY_TYPE:
    calc_size = size * sizeof(char);
    break;

  case CHAR_ARY_TYPE:
    calc_size = size * sizeof(wchar_t);
    break;

  case INT_TYPE:
    calc_size = size * sizeof(size_t);
    break;

  case FLOAT_TYPE:
    calc_size = size * sizeof(FLOAT_VALUE);
    break;

  default:
    std::wcerr << L">>> Invalid memory allocation <<<" << std::endl;
    exit(1);
  }

  const size_t alloc_size = calc_size + sizeof(size_t) * EXTRA_BUF_SIZE;

  // Try young generation bump allocation
  const size_t total = sizeof(size_t) + alloc_size;
  const size_t aligned_total = (total + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
  size_t offset = young_offset.load(std::memory_order_relaxed);

  if(offset + aligned_total <= young_region_size) {
    // Bump allocate in young region
    size_t new_offset = offset + aligned_total;
    young_offset.store(new_offset, std::memory_order_relaxed);

    size_t* raw_mem = (size_t*)(young_region + offset);
    memset(raw_mem, 0, aligned_total);
    raw_mem[0] = alloc_size;
    mem = raw_mem + 1;
    mem[EXTRA_BUF_SIZE + TYPE] = type;
    mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = calc_size;
    mem += EXTRA_BUF_SIZE;

#ifndef _GC_SERIAL
    MUTEX_LOCK(&allocated_lock);
#endif
    allocation_size += calc_size;
#ifndef _GC_SERIAL
    MUTEX_UNLOCK(&allocated_lock);
#endif
  }
  else {
    // Young region full — trigger minor GC, then retry
    if(collect) {
      CollectMinor(op_stack, stack_pos);
    }

    offset = young_offset.load(std::memory_order_relaxed);
    if(offset + aligned_total <= young_region_size) {
      size_t new_offset = offset + aligned_total;
      young_offset.store(new_offset, std::memory_order_relaxed);

      size_t* raw_mem = (size_t*)(young_region + offset);
      memset(raw_mem, 0, aligned_total);
      raw_mem[0] = alloc_size;
      mem = raw_mem + 1;
      mem[EXTRA_BUF_SIZE + TYPE] = type;
      mem[EXTRA_BUF_SIZE + SIZE_OR_CLS] = calc_size;
      mem += EXTRA_BUF_SIZE;

#ifndef _GC_SERIAL
      MUTEX_LOCK(&allocated_lock);
#endif
      allocation_size += calc_size;
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&allocated_lock);
#endif
    }
    else {
      // Still full after minor GC — allocate directly in old gen
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
    }
  }

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
  const size_t mem_size = raw_mem[0];
  free_memory_cache_size += mem_size;

  std::unordered_map<size_t, std::list<size_t*>*>::iterator result = free_memory_cache.find(pool);
  if(result == free_memory_cache.end()) {
    std::list<size_t*>* pool_list = new std::list<size_t*>;
    pool_list->push_front(raw_mem);
    free_memory_cache.insert(std::pair<size_t, std::list<size_t*>*>(pool, pool_list));
  }
  else {
    result->second->push_front(raw_mem);
  }
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
  std::unordered_map<size_t, std::list<size_t*>*>::iterator result = free_memory_cache.find(cache_size);
  if(result != free_memory_cache.end() && !result->second->empty()) {
    bool found = false;
    std::list<size_t*>* free_cache = result->second;

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
#ifndef _GC_SERIAL
      MUTEX_UNLOCK(&free_memory_cache_lock);
#endif
      return raw_mem + 1;
    }
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

void MemoryManager::ClearFreeMemory(bool all) {
#ifndef _GC_SERIAL
  MUTEX_LOCK(&free_memory_cache_lock);
#endif
  std::unordered_map<size_t, std::list<size_t*>*>::iterator iter = free_memory_cache.begin();
  for(; iter != free_memory_cache.end(); ++iter) {
    std::list<size_t*>* free_cache = iter->second;

    while(!free_cache->empty()) {
      size_t* raw_mem = free_cache->front();
      free_cache->pop_front();

      const size_t size = raw_mem[0];
      free_memory_cache_size -= size;

      free(raw_mem);
      raw_mem = nullptr;
    }

    if(all) {
      delete free_cache;
      free_cache = nullptr;
    }
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
    return;
  }
#else
  if(pthread_mutex_trylock(&marked_sweep_lock)) {
    return;
  }  
#endif
#endif

  CollectionInfo* info = new CollectionInfo;
  info->op_stack = op_stack; 
  info->stack_pos = stack_pos;

  CollectMemory(info);

#ifndef _GC_SERIAL
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

  // sort and search
#ifndef _GC_SERIAL
  MUTEX_LOCK(&allocated_lock);
  MUTEX_LOCK(&marked_lock);
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


#ifndef _GC_SERIAL
  MUTEX_UNLOCK(&marked_lock);
#endif

  // --- Sweep old generation (major GC only) ---
  size_t dead_old_count = 0;
  const size_t prev_old_size = old_generation.size();

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
    size_t dc = dirty_count.load(std::memory_order_relaxed);
    size_t clear_count = dc < DIRTY_LIST_MAX ? dc : DIRTY_LIST_MAX;
    for(size_t i = 0; i < clear_count; ++i) {
      if(dirty_list[i] && old_generation.count(dirty_list[i])) {
        dirty_list[i][MARKED_FLAG] &= ~GC_RSET_BIT;
      }
      dirty_list[i] = nullptr;
    }
    dirty_count.store(0, std::memory_order_relaxed);
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
unsigned int MemoryManager::CheckStatic(void* arg)
#else
void* MemoryManager::CheckStatic(void* arg)
#endif
{
  StackClass** clss = prgm->GetClasses();
  const int cls_num = prgm->GetClassNumber();
  
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
unsigned int MemoryManager::CheckJitRoots(void* arg)
#else
void* MemoryManager::CheckJitRoots(void* arg)
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
      for(int j = dclrs_num - 1; j > -1; --j) {
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
          std::pair<int, StackDclr**> closure_dclrs = prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(mthd_id);
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
unsigned int MemoryManager::CheckPdaRoots(void* arg)
#else
void* MemoryManager::CheckPdaRoots(void* arg)
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
    // gather stack frames
    long call_stack_pos = *(monitor->call_stack_pos);

    if(call_stack_pos > 0) {
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
        if(frame->jit_mem) {
#ifndef _GC_SERIAL
          MUTEX_LOCK(&jit_frame_lock);
#endif    
          jit_frames.push_back(frame);
#ifndef _GC_SERIAL
          MUTEX_UNLOCK(&jit_frame_lock);
#endif
        }
        else {
          frames.push_back(frame);
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
      std::pair<int, StackDclr**> closure_dclrs = prgm->GetClass(virtual_cls_id)->GetClosureDeclarations(mthd_id);
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
      size_t* ref = (size_t*)*(mem + 1);
      if(ref && IsYoung(ref)) {
        size_t fwd = ref[MARKED_FLAG];
        if(fwd) *(mem + 1) = fwd;
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
      size_t* ref = (size_t*)(*mem);
      if(ref && IsYoung(ref)) {
        size_t fwd = ref[MARKED_FLAG];
        if(fwd) *mem = fwd;
      }
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
      size_t* ref = (size_t*)objects[i];
      if(ref && IsYoung(ref)) {
        size_t fwd = ref[MARKED_FLAG];
        if(fwd) objects[i] = fwd;
      }
    }
  }
}

void MemoryManager::FixupRoots(size_t* op_stack, size_t stack_pos)
{
  // Fix up operand stack
  for(size_t i = 0; i <= stack_pos; ++i) {
    size_t* ref = (size_t*)op_stack[i];
    if(ref && IsYoung(ref)) {
      size_t fwd = ref[MARKED_FLAG];
      if(fwd) op_stack[i] = fwd;
    }
  }

  // Fix up static class memory
  StackClass** clss = prgm->GetClasses();
  const int cls_num = prgm->GetClassNumber();
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
    long call_stack_pos = *(monitor->call_stack_pos);

    if(call_stack_pos > 0) {
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

      // Fix up call stack frames
      while(--call_stack_pos > -1) {
        StackFrame* frame = call_stack[call_stack_pos];
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
      for(int j = dclrs_num - 1; j > -1; --j) {
#endif
        switch(dclrs[j]->type) {
        case FUNC_PARM: {
          size_t* ref = (size_t*)*(mem + 1);
          if(ref && IsYoung(ref)) {
            size_t fwd = ref[MARKED_FLAG];
            if(fwd) *(mem + 1) = fwd;
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
          size_t* ref = (size_t*)(*mem);
          if(ref && IsYoung(ref)) {
            size_t fwd = ref[MARKED_FLAG];
            if(fwd) *mem = fwd;
          }
          mem++;
          break;
        }
        default:
          mem++;
          break;
        }
      }

      // Fix up JIT temps
#ifdef _ARM64
      size_t* start = frame->jit_mem - 1;
      for(int k = 0; k > -JIT_TMP_LOOK_BACK; --k) {
        size_t* ref = (size_t*)start[k];
#else
      for(int k = 0; k < JIT_TMP_LOOK_BACK; ++k) {
        size_t* ref = (size_t*)mem[k];
#endif
        if(ref && IsYoung(ref)) {
          size_t fwd = ref[MARKED_FLAG];
          if(fwd) {
#ifdef _ARM64
            start[k] = (size_t)fwd;
#else
            mem[k] = (size_t)fwd;
#endif
          }
        }
      }
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
